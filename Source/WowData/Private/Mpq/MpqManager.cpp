#include "Mpq/MpqManager.h"
#include "WowLocale.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "StormLibWrapper.h"

DEFINE_LOG_CATEGORY_STATIC(LogMpq, Log, All);

namespace
{
// Only direct Data and locale archives are active. Storage/backup subfolders
// must never reactivate a patch that the user moved out of the client layout.
bool IsActiveArchivePath(FString Relative)
{
    Relative.ReplaceInline(TEXT("\\"),TEXT("/"));
    TArray<FString> Parts;Relative.ParseIntoArray(Parts,TEXT("/"),true);
    if (!FPaths::GetExtension(Relative).Equals(TEXT("mpq"),ESearchCase::IgnoreCase)) return false;
    if (Parts.Num()==1) return true;
    if (Parts.Num()!=2) return false;
    for (const TCHAR* Locale : {TEXT("enUS"),TEXT("enGB"),TEXT("deDE"),TEXT("frFR"),TEXT("esES"),TEXT("esMX"),TEXT("ruRU"),TEXT("koKR"),TEXT("zhCN"),TEXT("zhTW"),TEXT("ptBR"),TEXT("itIT")})
        if (Parts[0].Equals(Locale,ESearchCase::IgnoreCase)) return true;
    return false;
}

// r362: set when any HD data was found (Options greys out the HD choices without it).
static bool GAnyHDDataAvailable = false;

// HD character archives (patch-H.MPQ, patch-H-2.MPQ, ...) replace the stock character
// models with retail-format ones. They are never part of the normal lookup.
// r382: patch-HD.MPQ / patch-HD-2.MPQ too. A renamed patch-HD.MPQ was opened as a generic
// overlay, so its HD character models replaced the stock ones with SD skins and geoset rules.
bool IsHDArchiveName(const FString& Path)
{
    const FString Leaf = FPaths::GetCleanFilename(Path).ToLower();
    if (!Leaf.EndsWith(TEXT(".mpq"))) return false;
    for (const TCHAR* Base : { TEXT("patch-h"), TEXT("patch-hd") })
    {
        const FString Prefix = Base;
        if (Leaf == Prefix + TEXT(".mpq") || Leaf.StartsWith(Prefix + TEXT("-"))) return true;
    }
    return false;
}

// r386 (Stefan: HD models showed with the HD option off): an archive in our own User\Data that
// replaces player character models is an HD pack whatever its name, so it is read only through
// HDX\ and follows the HD option. Server data folders are never checked: their models are theirs.
static bool ArchiveHasPlayerCharacterModels(HANDLE Archive)
{
    static const TCHAR* const Races[] = { TEXT("Human"), TEXT("Orc"), TEXT("Dwarf"), TEXT("NightElf"), TEXT("Scourge"),
        TEXT("Tauren"), TEXT("Gnome"), TEXT("Troll"), TEXT("BloodElf"), TEXT("Draenei") };
    for (const TCHAR* Race : Races)
        for (const TCHAR* Gender : { TEXT("Male"), TEXT("Female") })
        {
            const FString Path = FString::Printf(TEXT("Character\\%s\\%s\\%s%s.m2"), Race, Gender, Race, Gender);
            STORMLIB_HANDLE File = nullptr;
            if (StormLib_OpenFile(Archive, TCHAR_TO_UTF8(*Path), &File))
            {
                StormLib_CloseFile(File);
                return true;
            }
        }
    return false;
}

// Virtual asset paths must stay relative to a configured root. In particular,
// reject drive names, UNC paths, streams and parent traversal before filesystem IO.
bool NormalizeAssetPath(const FString& Input, FString& Output)
{
    Output = Input.Replace(TEXT("\\"), TEXT("/"));
    if (Output.IsEmpty() || Output.StartsWith(TEXT("/")) || Output.Contains(TEXT(":"))) return false;
    TArray<FString> Parts;
    Output.ParseIntoArray(Parts, TEXT("/"), true);
    for (const FString& Part : Parts)
        if (Part == TEXT("..") || Part == TEXT(".") || Part.Contains(TEXT("*")) || Part.Contains(TEXT("?"))) return false;
    Output = FString::Join(Parts, TEXT("/"));
    return !Output.IsEmpty();
}

// Numeric runs are compared without integer conversion, including long custom
// version numbers. This orders patch-10 above patch-9 and patch-Z above patch-A.
int32 CompareArchiveNamesNatural(const FString& A, const FString& B)
{
    int32 I = 0, J = 0;
    while (I < A.Len() && J < B.Len())
    {
        if (FChar::IsDigit(A[I]) && FChar::IsDigit(B[J]))
        {
            int32 EndA = I, EndB = J;
            while (EndA < A.Len() && FChar::IsDigit(A[EndA])) ++EndA;
            while (EndB < B.Len() && FChar::IsDigit(B[EndB])) ++EndB;
            while (I < EndA && A[I] == TEXT('0')) ++I;
            while (J < EndB && B[J] == TEXT('0')) ++J;
            if (EndA - I != EndB - J) return EndA - I < EndB - J ? -1 : 1;
            while (I < EndA)
            {
                if (A[I] != B[J]) return A[I] < B[J] ? -1 : 1;
                ++I; ++J;
            }
            I = EndA; J = EndB;
        }
        else
        {
            const TCHAR CA = FChar::ToLower(A[I++]), CB = FChar::ToLower(B[J++]);
            if (CA != CB) return CA < CB ? -1 : 1;
        }
    }
    if (I < A.Len()) return 1;
    if (J < B.Len()) return -1;
    return 0;
}
}

FMpqManager::FMpqManager() {}
FMpqManager::~FMpqManager() { Shutdown(); }

FString FMpqManager::FindClientRoot(const FString& StartDirectory)
{
    FString Directory = FPaths::ConvertRelativePathToFull(StartDirectory);
    FPaths::NormalizeDirectoryName(Directory);
    const FString Fallback = Directory;
    // The portable launcher owns ClientPaths.ini above Client/WowUnreal.
    for (int32 Depth = 0; Depth < 6; ++Depth)
    {
        if (FPaths::FileExists(Directory / TEXT("ClientPaths.ini"))) return Directory;
        const FString Parent = FPaths::GetPath(Directory);
        if (Parent == Directory || Parent.IsEmpty()) break;
        Directory = Parent;
    }
    return Fallback;
}

FString FMpqManager::FindUserRoot(const FString& StartDirectory)
{
    const FString ClientRoot = FindClientRoot(StartDirectory);
    const FString UserRoot = ClientRoot / TEXT("User");
    return FPaths::DirectoryExists(UserRoot) ? UserRoot : ClientRoot;
}

TArray<FString> FMpqManager::GetLooseFileRoots() const
{
    FScopeLock Lock(&ArchiveLock);
    return LooseFileRoots;
}

bool FMpqManager::Initialize(const FString& InDataPath, const TArray<FString>& OverrideRoots, EWowAssetProfile Profile)
{
    Shutdown(); // also closes archives left open by a failed earlier attempt

    FScopeLock Lock(&ArchiveLock);
    AssetProfile = Profile;
    const bool bStockArchivesOnly = Profile == EWowAssetProfile::OriginalWotlk335;
    DataPath = FPaths::ConvertRelativePathToFull(InDataPath);
    FPaths::NormalizeDirectoryName(DataPath);
    // Accept the client folder itself (e.g. "F:/TormentWoW") as well as its Data
    // folder; otherwise Interface/AddOns beside Data would never become a root.
    if (!FPaths::GetCleanFilename(DataPath).Equals(TEXT("Data"), ESearchCase::IgnoreCase)
        && FPaths::DirectoryExists(DataPath / TEXT("Data")))
    {
        TArray<FString> DirectArchives;
        IFileManager::Get().FindFiles(DirectArchives, *(DataPath / TEXT("*.mpq")), true, false);
        if (DirectArchives.Num() == 0)
        {
            UE_LOG(LogMpq, Log, TEXT("Using %s/Data as the archive folder"), *DataPath);
            DataPath = DataPath / TEXT("Data");
        }
    }
    LooseFileRoots.Reset();
    auto AddRoot = [this](const FString& Root)
    {
        if (Root.IsEmpty()) return;
        FString Full = FPaths::ConvertRelativePathToFull(Root);
        FPaths::NormalizeDirectoryName(Full);
        if (!LooseFileRoots.ContainsByPredicate([&](const FString& Existing)
            { return Existing.Equals(Full, ESearchCase::IgnoreCase); })) LooseFileRoots.Add(Full);
    };
    for (const FString& Root : OverrideRoots) AddRoot(Root);
    // <client root>/Data (dist\\Data) is our generic data folder: its loose files and
    // archives sit above every server's own Data folder, and offline uses it too.
    OverlayDataPath.Reset();
    // (From the project folder in development the same folder is <project>\\dist\\Data.)
    for (const FString& Root : OverrideRoots)
    {
        for (const FString& Relative : {FString(TEXT("Data")), FString(TEXT("dist/User/Data")), FString(TEXT("dist/Data"))})
        {
            FString Candidate = FPaths::ConvertRelativePathToFull(Root / Relative);
            FPaths::NormalizeDirectoryName(Candidate);
            if (FPaths::DirectoryExists(Candidate)) { OverlayDataPath = Candidate; break; }
        }
        if (!OverlayDataPath.IsEmpty()) break;
    }
    const bool bOverlayIsServerData = !OverlayDataPath.IsEmpty() && OverlayDataPath.Equals(DataPath, ESearchCase::IgnoreCase);
    if (!OverlayDataPath.IsEmpty() && !bOverlayIsServerData) AddRoot(OverlayDataPath);
    if (FPaths::GetCleanFilename(DataPath).Equals(TEXT("Data"), ESearchCase::IgnoreCase))
        AddRoot(FPaths::GetPath(DataPath));
    AddRoot(DataPath);
    if (bStockArchivesOnly) LooseFileRoots.Reset();
    for (const FString& Root : LooseFileRoots) UE_LOG(LogMpq, Log, TEXT("Loose file root: %s"), *Root);

    if (!FPaths::DirectoryExists(DataPath))
    {
        UE_LOG(LogMpq, Error, TEXT("WoW Data directory not found: %s"), *DataPath);
        return false;
    }

    // Discover active archives on every initialization.
    // Keep the original archive precedence, with custom archives above it.
    TArray<FString> ArchiveNames;
    TArray<FString> HDArchiveFiles;
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *DataPath, TEXT("*"), true, false, false);
    FString AbsoluteRoot = FPaths::ConvertRelativePathToFull(DataPath);
    FPaths::NormalizeDirectoryName(AbsoluteRoot);
    AbsoluteRoot += TEXT("/");
    TSet<FString> Seen;
    for (const FString& File : Files)
    {
        if (!FPaths::GetExtension(File).Equals(TEXT("mpq"), ESearchCase::IgnoreCase)) continue;
        FString Relative = FPaths::ConvertRelativePathToFull(File);
        FPaths::NormalizeFilename(Relative);
        if (!FPaths::MakePathRelativeTo(Relative, *AbsoluteRoot)) continue;
        if (!IsActiveArchivePath(Relative)) continue;
        if (IsHDArchiveName(Relative))
        {
            HDArchiveFiles.Add(FPaths::Combine(DataPath, Relative));
            continue;
        }
        const FString Key = Relative.ToLower();
        if (!Seen.Contains(Key))
        {
            Seen.Add(Key);
            ArchiveNames.Add(MoveTemp(Relative));
        }
    }

    const TArray<FString> OriginalPriority = {
        TEXT("enUS/patch-enUS-3.MPQ"), TEXT("enUS/patch-enUS-2.MPQ"), TEXT("enUS/patch-enUS.MPQ"),
        TEXT("enGB/patch-enGB-3.MPQ"), TEXT("enGB/patch-enGB-2.MPQ"), TEXT("enGB/patch-enGB.MPQ"),
        TEXT("patch-3.MPQ"), TEXT("patch-2.MPQ"), TEXT("patch.MPQ"),
        TEXT("enUS/lichking-locale-enUS.MPQ"), TEXT("enUS/lichking-speech-enUS.MPQ"),
        TEXT("enGB/lichking-locale-enGB.MPQ"), TEXT("enGB/lichking-speech-enGB.MPQ"), TEXT("lichking.MPQ"),
        TEXT("enUS/expansion-locale-enUS.MPQ"), TEXT("enUS/expansion-speech-enUS.MPQ"),
        TEXT("enGB/expansion-locale-enGB.MPQ"), TEXT("enGB/expansion-speech-enGB.MPQ"), TEXT("expansion.MPQ"),
        TEXT("enUS/locale-enUS.MPQ"), TEXT("enUS/base-enUS.MPQ"), TEXT("enUS/speech-enUS.MPQ"),
        TEXT("enUS/backup-enUS.MPQ"),
        TEXT("enGB/locale-enGB.MPQ"), TEXT("enGB/base-enGB.MPQ"), TEXT("enGB/speech-enGB.MPQ"),
        TEXT("enGB/backup-enGB.MPQ"),
        TEXT("common.MPQ"), TEXT("common-2.MPQ"),
    };


    // The old list is a precedence table, never a discovery allowlist. A stock
    // archive in a subfolder retains its role; other MPQs are custom overrides.
    auto OriginalRank = [&OriginalPriority](const FString& Path) -> int32
    {
        const FString Leaf = FPaths::GetCleanFilename(Path);
        for (int32 I = 0; I < OriginalPriority.Num(); ++I)
            if (Leaf.Equals(FPaths::GetCleanFilename(OriginalPriority[I]), ESearchCase::IgnoreCase)) return I;
        // Other stock localizations are fallback data, not HD overrides. Keep
        // discovering them, but never let ruRU/frFR speech or DBCs replace the
        // English archives this client uses. Locale patch versions retain order.
        const TCHAR* OtherLocales[] = {TEXT("deDE"), TEXT("frFR"), TEXT("esES"), TEXT("esMX"),
            TEXT("ruRU"), TEXT("koKR"), TEXT("zhCN"), TEXT("zhTW"), TEXT("ptBR"), TEXT("itIT")};
        for (int32 Locale = 0; Locale < UE_ARRAY_COUNT(OtherLocales); ++Locale)
        {
            FString EnglishLeaf = Leaf;
            if (EnglishLeaf.ReplaceInline(OtherLocales[Locale], TEXT("enUS"), ESearchCase::IgnoreCase) == 0) continue;
            for (int32 I = 0; I < OriginalPriority.Num(); ++I)
                if (EnglishLeaf.Equals(FPaths::GetCleanFilename(OriginalPriority[I]), ESearchCase::IgnoreCase))
                    return OriginalPriority.Num() + Locale * OriginalPriority.Num() + I;
        }
        return INDEX_NONE;
    };
    const FString SelectedLocale = WowLocale::Get();
    // Filter only recognized stock language archives. Custom packs installed
    // inside another locale folder remain available (for example HD packs).
    const TCHAR* StockLocales[] = {TEXT("enUS"), TEXT("enGB"), TEXT("deDE"), TEXT("frFR"),
        TEXT("esES"), TEXT("esMX"), TEXT("ruRU"), TEXT("koKR"), TEXT("zhCN"), TEXT("zhTW"),
        TEXT("ptBR"), TEXT("itIT")};
    ArchiveNames.RemoveAll([&](const FString& Path)
    {
        if (OriginalRank(Path) == INDEX_NONE) return false;
        const FString Leaf = FPaths::GetCleanFilename(Path);
        for (const TCHAR* Code : StockLocales)
            if (Leaf.Contains(Code, ESearchCase::IgnoreCase)) return SelectedLocale != Code;
        return false;
    });
    UE_LOG(LogMpq, Log, TEXT("Selected asset locale: %s; custom overrides retained"), *SelectedLocale);
    if (bStockArchivesOnly)
        ArchiveNames.RemoveAll([&](const FString& Path) { return OriginalRank(Path) == INDEX_NONE; });
    ArchiveNames.Sort([&](const FString& A, const FString& B)
    {
        // Selected language patches retain the same priority as English patches.
        const int32 RankA = OriginalRank(A.Replace(*SelectedLocale, TEXT("enUS"), ESearchCase::IgnoreCase));
        const int32 RankB = OriginalRank(B.Replace(*SelectedLocale, TEXT("enUS"), ESearchCase::IgnoreCase));
        if (RankA != RankB) return RankA < RankB;
        if (RankA == INDEX_NONE)
        {
            const int32 NameOrder = CompareArchiveNamesNatural(
                FPaths::GetCleanFilename(A), FPaths::GetCleanFilename(B));
            if (NameOrder != 0) return NameOrder > 0;
        }
        // Equal names: the more specific folder overrides the root archive.
        int32 DepthA = 0, DepthB = 0;
        for (TCHAR Character : A) if (Character == TEXT('/')) ++DepthA;
        for (TCHAR Character : B) if (Character == TEXT('/')) ++DepthB;
        if (DepthA != DepthB) return DepthA > DepthB;
        const int32 PathOrder = A.Compare(B, ESearchCase::IgnoreCase);
        return PathOrder != 0 ? PathOrder > 0 : A.Compare(B, ESearchCase::CaseSensitive) > 0;
    });

    int32 OpenedCount = 0;
    // Generic overlay archives: direct MPQs and the selected locale folder, highest
    // letter/number first, all above the server's archives.
    if (!OverlayDataPath.IsEmpty() && !bOverlayIsServerData && !bStockArchivesOnly)
    {
        TArray<FString> OverlayFiles;
        for (const FString& Folder : {OverlayDataPath, OverlayDataPath / SelectedLocale})
        {
            TArray<FString> Found;
            IFileManager::Get().FindFiles(Found, *(Folder / TEXT("*.mpq")), true, false);
            for (const FString& Leaf : Found)
            {
                if (IsHDArchiveName(Leaf)) HDArchiveFiles.Add(Folder / Leaf);
                else OverlayFiles.Add(Folder / Leaf);
            }
        }
        OverlayFiles.Sort([](const FString& A, const FString& B)
        { return CompareArchiveNamesNatural(FPaths::GetCleanFilename(A), FPaths::GetCleanFilename(B)) > 0; });
        for (const FString& File : OverlayFiles)
            if (HANDLE Handle = OpenArchive(File))
            {
                if (ArchiveHasPlayerCharacterModels(Handle))
                {
                    StormLib_CloseArchive(Handle);
                    HDArchiveFiles.Add(File);
                    UE_LOG(LogMpq, Log, TEXT("%s replaces player character models: treated as an HD pack (HD option)"), *File);
                    continue;
                }
                Archives.Add(Handle); ++OpenedCount;
                UE_LOG(LogMpq, Log, TEXT("Opened generic data MPQ (above server data): %s"), *File);
            }
    }
    // HD set, read only through the HDX\\ prefix.
    HDLooseRoots.Reset();
    if (!bStockArchivesOnly)
    {
        TArray<FString> HDFolders;
        for (const FString& Base : {OverlayDataPath, DataPath})
            if (!Base.IsEmpty()) { HDFolders.Add(Base / TEXT("HD")); HDFolders.Add(Base / TEXT("HD") / TEXT("HD_Data")); HDFolders.Add(Base / TEXT("HD_Data")); }
        for (const FString& Folder : HDFolders)
        {
            if (!FPaths::DirectoryExists(Folder)) continue;
            FString Full = FPaths::ConvertRelativePathToFull(Folder);
            FPaths::NormalizeDirectoryName(Full);
            if (HDLooseRoots.ContainsByPredicate([&](const FString& E) { return E.Equals(Full, ESearchCase::IgnoreCase); })) continue;
            HDLooseRoots.Add(Full);
            UE_LOG(LogMpq, Log, TEXT("HD loose root: %s"), *Full);
            TArray<FString> Found;
            IFileManager::Get().FindFiles(Found, *(Full / TEXT("*.mpq")), true, false);
            for (const FString& Leaf : Found) HDArchiveFiles.Add(Full / Leaf);
        }
        TSet<FString> SeenHD;
        HDArchiveFiles.Sort([](const FString& A, const FString& B)
        { return CompareArchiveNamesNatural(FPaths::GetCleanFilename(A), FPaths::GetCleanFilename(B)) > 0; });
        for (const FString& File : HDArchiveFiles)
        {
            FString Key = FPaths::ConvertRelativePathToFull(File).ToLower();
            if (SeenHD.Contains(Key)) continue;
            SeenHD.Add(Key);
            if (HANDLE Handle = OpenArchive(File))
            {
                HDArchives.Add(Handle);
                UE_LOG(LogMpq, Log, TEXT("Opened HD MPQ (HDX\\ paths only): %s"), *File);
            }
        }
        // r388 (Stefan: grey the HD options out when the files are missing): only a real HD
        // character model counts. A loose HD folder with textures alone (no patch-H) used to
        // enable the options although nothing HD could be shown.
        bool bHDModels = false;
        for (HANDLE Handle : HDArchives)
            if (ArchiveHasPlayerCharacterModels(Handle)) { bHDModels = true; break; }
        for (const FString& Root : HDLooseRoots)
            if (!bHDModels && IFileManager::Get().FileExists(*(Root / TEXT("Character/Human/Male/HumanMale.m2")))) bHDModels = true;
        if (bHDModels) GAnyHDDataAvailable = true;
        UE_LOG(LogMpq, Log, TEXT("HD character models %s (Options HD choices %s)"),
            bHDModels ? TEXT("found") : TEXT("not found"), GAnyHDDataAvailable ? TEXT("enabled") : TEXT("greyed out"));
    }
    for (const FString& Name : ArchiveNames)
    {
        FString FullPath = FPaths::Combine(DataPath, Name);
        if (FPaths::FileExists(FullPath))
        {
            HANDLE hArchive = OpenArchive(FullPath);
            if (hArchive)
            {
                Archives.Add(hArchive);
                OpenedCount++;
                UE_LOG(LogMpq, Log, TEXT("Opened MPQ [%d/%d, first match wins]: %s"), OpenedCount, ArchiveNames.Num(), *Name);
            }
            else
            {
                UE_LOG(LogMpq, Warning, TEXT("Failed to open MPQ: %s"), *Name);
            }
        }
        else
        {
            // A discovered file may have been removed while opening the list.
            UE_LOG(LogMpq, Warning, TEXT("MPQ not found: %s (full: %s)"), *Name, *FullPath);
        }
    }
    bInitialized = OpenedCount > 0;
    UE_LOG(LogMpq, Log, TEXT("MPQ Manager: %d archives from %s"), OpenedCount, *DataPath);
    return bInitialized;
}

void FMpqManager::Shutdown()
{
    FScopeLock Lock(&ArchiveLock);
    for (HANDLE h : Archives)
    {
        if (h) StormLib_CloseArchive(h);
    }
    Archives.Empty();
    for (HANDLE h : HDArchives)
    {
        if (h) StormLib_CloseArchive(h);
    }
    HDArchives.Empty();
    HDLooseRoots.Empty();
    LooseFileRoots.Empty();
    bInitialized = false;
}

HANDLE FMpqManager::OpenArchive(const FString& ArchivePath) const
{
    STORMLIB_HANDLE hArchive = nullptr;
    if (StormLib_OpenArchive(TCHAR_TO_UTF8(*ArchivePath), &hArchive))
        return hArchive;
    return nullptr;
}

bool FMpqManager::ReadFromSet(const FString& Normalized, const TArray<FString>& Roots, const TArray<HANDLE>& Set, TArray<uint8>& OutData) const
{
    for (const FString& Root : Roots)
    {
        const FString LoosePath = Root / Normalized;
        // An existing override is authoritative, even when empty or unreadable.
        if (IFileManager::Get().FileExists(*LoosePath))
            return FFileHelper::LoadFileToArray(OutData, *LoosePath);
    }
    const FString MpqPath = Normalized.Replace(TEXT("/"), TEXT("\\"));
    for (HANDLE h : Set)
    {
        STORMLIB_HANDLE hFile = nullptr;
        if (StormLib_OpenFile(h, TCHAR_TO_UTF8(*MpqPath), &hFile))
        {
            uint32 Size = StormLib_GetFileSize(hFile);
            if (Size == 0 || Size == 0xFFFFFFFF)
            {
                StormLib_CloseFile(hFile);
                continue;
            }
            OutData.SetNumUninitialized(Size);
            uint32 Read = 0;
            bool Ok = StormLib_ReadFile(hFile, OutData.GetData(), Size, &Read);
            StormLib_CloseFile(hFile);
            if (Ok && Read == Size) return true;
            OutData.Empty();
        }
    }
    return false;
}

bool FMpqManager::ExistsInSet(const FString& Normalized, const TArray<FString>& Roots, const TArray<HANDLE>& Set) const
{
    for (const FString& Root : Roots)
        if (IFileManager::Get().FileExists(*(Root / Normalized))) return true;
    const FString MpqPath = Normalized.Replace(TEXT("/"), TEXT("\\"));
    for (HANDLE h : Set)
    {
        STORMLIB_HANDLE hFile = nullptr;
        if (StormLib_OpenFile(h, TCHAR_TO_UTF8(*MpqPath), &hFile))
        {
            StormLib_CloseFile(hFile);
            return true;
        }
    }
    return false;
}

bool FMpqManager::AnyHDDataAvailable() { return GAnyHDDataAvailable; }

bool FMpqManager::HasHDData() const
{
    FScopeLock Lock(&ArchiveLock);
    return bInitialized && (HDArchives.Num() > 0 || HDLooseRoots.Num() > 0);
}

bool FMpqManager::ReadFile(const FString& FilePath, TArray<uint8>& OutData) const
{
    FScopeLock Lock(&ArchiveLock);
    if (!bInitialized) return false;

    OutData.Reset();
    FString Normalized;
    if (!NormalizeAssetPath(FilePath, Normalized)) return false;
    if (Normalized.StartsWith(HDPrefix(), ESearchCase::IgnoreCase))
        return ReadFromSet(Normalized.Mid(4), HDLooseRoots, HDArchives, OutData);
    return ReadFromSet(Normalized, LooseFileRoots, Archives, OutData);
}

bool FMpqManager::FileExists(const FString& FilePath) const
{
    FScopeLock Lock(&ArchiveLock);
    if (!bInitialized) return false;

    FString Normalized;
    if (!NormalizeAssetPath(FilePath, Normalized)) return false;
    if (Normalized.StartsWith(HDPrefix(), ESearchCase::IgnoreCase))
        return ExistsInSet(Normalized.Mid(4), HDLooseRoots, HDArchives);
    return ExistsInSet(Normalized, LooseFileRoots, Archives);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowActiveArchiveTest,"WowUnreal.Mpq.ActiveArchiveLocations",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowActiveArchiveTest::RunTest(const FString&)
{
    TestTrue(TEXT("Root custom patch"),IsActiveArchivePath(TEXT("patch-M.MPQ")));
    TestTrue(TEXT("Active locale custom patch"),IsActiveArchivePath(TEXT("enGB/patch-enGB-T.mpq")));
    TestTrue(TEXT("Other locale HD pack remains available"),IsActiveArchivePath(TEXT("enUS/patch-HD.MPQ")));
    TestFalse(TEXT("Moved tree patch is inactive"),IsActiveArchivePath(TEXT("enGB/Not in use/patch-enGB-T.MPQ")));
    TestFalse(TEXT("Root backup folder is inactive"),IsActiveArchivePath(TEXT("Backup/patch-M.MPQ")));
    TestFalse(TEXT("Renamed extension is inactive"),IsActiveArchivePath(TEXT("enGB/patch-enGB-T.mpq.disabled")));
    return true;
}
#endif
