#include "WowCursorManager.h"
#include "Formats/BlpParser.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
#include "Mpq/MpqManager.h"
#include "WowAssetCache.h"
#include "Engine/Engine.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowCursor, Log, All);

UWowCursorManager::UWowCursorManager()
{
    CurrentCursorType = EWowCursorType::Point;
}

void UWowCursorManager::Initialize(FMpqManager* Mpq, FWowAssetCache* Cache)
{
    MpqManager = Mpq;
    AssetCache = Cache;

    if (!MpqManager)
    {
        UE_LOG(LogWowCursor, Verbose, TEXT("WowCursorManager initialized without MPQ data; using fallback cursors"));
        return;
    }

    UE_LOG(LogWowCursor, Log, TEXT("WowCursorManager initialized"));
}

void UWowCursorManager::LoadCursors()
{
    ReleaseHardwareCursors();
    CursorPixels.Empty();
    CursorTextures.Empty();

    // Load all cursor types
    TArray<EWowCursorType> CursorTypes = {
        EWowCursorType::Point,
        EWowCursorType::Attack,
        EWowCursorType::Speak,
        EWowCursorType::Buy,
        EWowCursorType::Quest,
        EWowCursorType::Inspect,
        EWowCursorType::Pickup,
        EWowCursorType::UnableAttack,
        EWowCursorType::UnablePoint, EWowCursorType::Cast, EWowCursorType::Interact,
        EWowCursorType::QuestTurnIn, // after Quest: falls back to it if the archive lacks QuestTurnIn.blp
        // r333: out-of-reach variants; UnableSpeak before the quest ones (their fallback).
        EWowCursorType::UnableInteract, EWowCursorType::UnableSpeak, EWowCursorType::UnableBuy,
        EWowCursorType::UnableQuest, EWowCursorType::UnableQuestTurnIn, EWowCursorType::UnablePickup,
        // r353: the neutral / friendly target cursors (after Attack: they borrow it).
        EWowCursorType::AttackNeutral, EWowCursorType::AttackFriendly
    };
    auto IsReactionAttack = [](EWowCursorType Type) { return Type == EWowCursorType::AttackNeutral || Type == EWowCursorType::AttackFriendly; };

    int32 LoadedCount = 0;
    for (EWowCursorType CursorType : CursorTypes)
    {
        FString CursorName = GetCursorFilename(CursorType);
        // r353: the client has one sword for every reaction; the reaction variants only come from cursor sets.
        UTexture2D* CursorTexture = IsReactionAttack(CursorType) ? nullptr : LoadCursorTexture(CursorName);
        if (!CursorTexture && IsReactionAttack(CursorType) && CursorTextures.Contains(EWowCursorType::Attack))
        {
            CursorTexture = CursorTextures[EWowCursorType::Attack];
            if (const FCursorPixels* AttackPixels = CursorPixels.Find(EWowCursorType::Attack)) { const FCursorPixels Copy = *AttackPixels; CursorPixels.Add(CursorType, Copy); }
        }
        if (!CursorTexture && CursorType == EWowCursorType::QuestTurnIn && CursorTextures.Contains(EWowCursorType::Quest))
        {
            CursorTexture = CursorTextures[EWowCursorType::Quest];
            if (const FCursorPixels* QuestPixels = CursorPixels.Find(EWowCursorType::Quest))
                CursorPixels.Add(EWowCursorType::QuestTurnIn, *QuestPixels);
        }
        // r333: an Unable cursor the archive lacks borrows a loaded grey one (UnableSpeak, then UnablePoint).
        const bool bUnableExtra = CursorType >= EWowCursorType::UnableInteract && CursorType <= EWowCursorType::UnablePickup;
        if (!CursorTexture && bUnableExtra)
        {
            for (EWowCursorType Borrow : { EWowCursorType::UnableSpeak, EWowCursorType::UnablePoint })
            {
                if (Borrow == CursorType || !CursorTextures.Contains(Borrow)) continue;
                CursorTexture = CursorTextures[Borrow];
                if (const FCursorPixels* BorrowPixels = CursorPixels.Find(Borrow)) { const FCursorPixels Copy = *BorrowPixels; CursorPixels.Add(CursorType, Copy); }
                break;
            }
        }
        if (!CursorTexture && !bUnableExtra && !IsReactionAttack(CursorType))
        {
            CursorTexture = LoadFallbackCursorTexture(CursorType);
        }

        if (CursorTexture)
        {
            CursorTextures.Add(CursorType, CursorTexture);
            LoadedCount++;
            UE_LOG(LogWowCursor, Log, TEXT("Loaded cursor: %s"), *CursorName);
        }
        else
        {
            UE_LOG(LogWowCursor, Warning, TEXT("Failed to load cursor: %s"), *CursorName);
        }
    }

    UE_LOG(LogWowCursor, Log, TEXT("Loaded %d/%d cursor textures"), LoadedCount, CursorTypes.Num());

    // r351: the chosen cursor set replaces the cursors it has; the others stay the client's own.
    // r352 (Stefan 15.10, "Warcraft 3" with a cursor set per race): a set can hold a folder per race -
    // the WoW race (BloodElf), the Warcraft III race it belongs to (Human, Orc, Undead, NightElf) or the
    // faction (Alliance, Horde) - used before the set's own files; Warcraft II sets name their files
    // h_ / o_ for the two sides.
    AnimTextures.Reset();
    LoadedCursorSet = GetActiveCursorSet();
    LoadedRace = PlayerRace;
    if (!LoadedCursorSet.IsEmpty())
    {
        const FString SetFolder = GetCursorSetsFolder() / LoadedCursorSet;
        struct FRaceFolders { uint8 Race; const TCHAR* WowRace; const TCHAR* Spaced; const TCHAR* Wc3Race; bool bHorde; };
        static const FRaceFolders Races[] = {
            {1, TEXT("Human"), TEXT("Human"), TEXT("Human"), false}, {2, TEXT("Orc"), TEXT("Orc"), TEXT("Orc"), true},
            {3, TEXT("Dwarf"), TEXT("Dwarf"), TEXT("Human"), false}, {4, TEXT("NightElf"), TEXT("Night Elf"), TEXT("NightElf"), false},
            {5, TEXT("Undead"), TEXT("Scourge"), TEXT("Undead"), true}, {6, TEXT("Tauren"), TEXT("Tauren"), TEXT("Orc"), true},
            {7, TEXT("Gnome"), TEXT("Gnome"), TEXT("Human"), false}, {8, TEXT("Troll"), TEXT("Troll"), TEXT("Orc"), true},
            {10, TEXT("BloodElf"), TEXT("Blood Elf"), TEXT("Human"), true}, {11, TEXT("Draenei"), TEXT("Draenei"), TEXT("Human"), false} };
        const FRaceFolders* Race = nullptr;
        for (const FRaceFolders& Each : Races) if (Each.Race == PlayerRace) Race = &Each;
        TArray<FString> Folders;
        auto AddFolder = [&](const FString& Path) { if (FPaths::DirectoryExists(Path)) Folders.AddUnique(Path); };
        if (Race)
        {
            AddFolder(SetFolder / Race->WowRace);
            AddFolder(SetFolder / Race->Spaced);
            AddFolder(SetFolder / Race->Wc3Race);
            if (FCString::Strcmp(Race->Wc3Race, TEXT("NightElf")) == 0) AddFolder(SetFolder / TEXT("Night Elf"));
            AddFolder(SetFolder / (Race->bHorde ? TEXT("Horde") : TEXT("Alliance")));
            // r355: a set without the race's own folder (no Night Elf in a Warcraft III set) uses the
            // faction's first race - Human for the Alliance, Orc for the Horde.
            AddFolder(SetFolder / (Race->bHorde ? TEXT("Orc") : TEXT("Human")));
        }
        AddFolder(SetFolder);
        if (!Race) { AddFolder(SetFolder / TEXT("Human")); AddFolder(SetFolder / TEXT("Alliance")); } // no character yet (log-in screens): the first race / side folder
        const FString FactionPrefix = Race ? FString(Race->bHorde ? TEXT("o") : TEXT("h")) : FString(TEXT("h"));
        int32 FromSet = 0;
        bool bSetAttack = false;
        for (EWowCursorType CursorType : CursorTypes)
        {
            if (LoadSetCursor(CursorType, Folders, FactionPrefix))
            {
                ++FromSet;
                bSetAttack |= CursorType == EWowCursorType::Attack;
            }
            // r353: a set with one target cursor uses it for neutral and friendly targets too.
            else if (IsReactionAttack(CursorType) && bSetAttack)
            {
                if (const FCursorPixels* AttackPixels = CursorPixels.Find(EWowCursorType::Attack)) { const FCursorPixels Copy = *AttackPixels; CursorPixels.Add(CursorType, Copy); }
                if (CursorTextures.Contains(EWowCursorType::Attack)) { UTexture2D* const AttackTexture = CursorTextures[EWowCursorType::Attack]; CursorTextures.Add(CursorType, AttackTexture); }
            }
        }
        UE_LOG(LogWowCursor, Log, TEXT("Cursor set '%s' (race %d): %d of %d cursors from %s"), *LoadedCursorSet, int32(PlayerRace), FromSet,
            CursorTypes.Num(), Folders.Num() ? *FString::Join(Folders, TEXT(", ")) : *SetFolder);
    }

    // Set default cursor
    SetCursorType(EWowCursorType::Point);
}

void UWowCursorManager::SetCursorType(EWowCursorType CursorType)
{
    CurrentCursorType = CursorType;

    UTexture2D* CursorTexture = GetCursorTexture(CursorType);
    if (!CursorTexture)
    {
        UE_LOG(LogWowCursor, Warning, TEXT("No texture loaded for cursor type %d"), (int32)CursorType);
        return;
    }

    UE_LOG(LogWowCursor, Log, TEXT("Set cursor type to: %s"), *GetCursorFilename(CursorType));
}

// ── r351: cursor sets ───────────────────────────────────────────────────────────

namespace WowCursorSets
{
    FString GActiveSet;
    bool bGActiveSetRead = false;
    const TCHAR* const ConfigSection = TEXT("WowUnreal.Cursor");

    uint16 ReadU16(const TArray<uint8>& D, int64 At) { return At + 2 <= D.Num() ? uint16(D[At] | (D[At + 1] << 8)) : 0; }
    uint32 ReadU32(const TArray<uint8>& D, int64 At)
    {
        return At + 4 <= D.Num() ? uint32(D[At]) | (uint32(D[At + 1]) << 8) | (uint32(D[At + 2]) << 16) | (uint32(D[At + 3]) << 24) : 0;
    }

    bool FromImage(FImage& Image, UWowCursorManager::FCursorPixels& Out)
    {
        if (Image.SizeX <= 0 || Image.SizeY <= 0 || Image.SizeX > 256 || Image.SizeY > 256) return false;
        Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
        Out.Width = Image.SizeX;
        Out.Height = Image.SizeY;
        Out.RGBA.SetNumUninitialized(Out.Width * Out.Height * 4);
        const uint8* Src = Image.RawData.GetData();
        for (int32 Pixel = 0; Pixel < Out.Width * Out.Height; ++Pixel)
        {
            Out.RGBA[Pixel * 4] = Src[Pixel * 4 + 2];
            Out.RGBA[Pixel * 4 + 1] = Src[Pixel * 4 + 1];
            Out.RGBA[Pixel * 4 + 2] = Src[Pixel * 4];
            Out.RGBA[Pixel * 4 + 3] = Src[Pixel * 4 + 3];
        }
        return true;
    }

    /** A Windows .cur (or .ico): the biggest image, PNG or DIB (1/4/8/24/32 bit), and its hot spot. */
    bool ParseCur(const TArray<uint8>& D, UWowCursorManager::FCursorPixels& Out)
    {
        if (D.Num() < 22 || ReadU16(D, 0) != 0 || (ReadU16(D, 2) != 1 && ReadU16(D, 2) != 2)) return false;
        const bool bCursor = ReadU16(D, 2) == 2;
        const int32 Count = ReadU16(D, 4);
        int64 Best = -1;
        int32 BestSize = 0;
        for (int32 Entry = 0; Entry < Count; ++Entry)
        {
            const int64 At = 6 + Entry * 16;
            if (At + 16 > D.Num()) break;
            const int32 Size = D[At] ? D[At] : 256;
            if (Size > BestSize) { BestSize = Size; Best = At; }
        }
        if (Best < 0) return false;
        const int32 HotXPx = bCursor ? ReadU16(D, Best + 4) : 0;
        const int32 HotYPx = bCursor ? ReadU16(D, Best + 6) : 0;
        const int64 Bytes = ReadU32(D, Best + 8);
        const int64 Offset = ReadU32(D, Best + 12);
        if (Offset <= 0 || Offset + Bytes > D.Num() || Bytes < 40) return false;
        if (D[Offset] == 0x89 && D[Offset + 1] == 'P' && D[Offset + 2] == 'N' && D[Offset + 3] == 'G')
        {
            FImage Image;
            if (!FImageUtils::DecompressImage(D.GetData() + Offset, Bytes, Image) || !FromImage(Image, Out)) return false;
        }
        else
        {
            const int64 Header = ReadU32(D, Offset);
            const int32 W = int32(ReadU32(D, Offset + 4));
            const int32 H = int32(ReadU32(D, Offset + 8)) / 2; // colour + mask
            const int32 Bpp = ReadU16(D, Offset + 14);
            if (W <= 0 || H <= 0 || W > 256 || H > 256 || (Bpp != 1 && Bpp != 4 && Bpp != 8 && Bpp != 24 && Bpp != 32)) return false;
            const int32 PaletteCount = Bpp <= 8 ? (ReadU32(D, Offset + 32) ? int32(ReadU32(D, Offset + 32)) : (1 << Bpp)) : 0;
            const int64 Palette = Offset + Header;
            const int64 Pixels = Palette + PaletteCount * 4;
            const int64 Stride = ((int64(W) * Bpp + 31) / 32) * 4;
            const int64 MaskStride = ((int64(W) + 31) / 32) * 4;
            const int64 Mask = Pixels + Stride * H;
            if (Mask + MaskStride * H > D.Num()) return false;
            Out.Width = W;
            Out.Height = H;
            Out.RGBA.SetNumZeroed(W * H * 4);
            bool bAnyAlpha = false;
            for (int32 Y = 0; Y < H; ++Y)
            {
                const int64 Row = Pixels + Stride * (H - 1 - Y); // bottom-up
                for (int32 X = 0; X < W; ++X)
                {
                    uint8 R = 0, G = 0, B = 0, A = 255;
                    if (Bpp == 32) { B = D[Row + X * 4]; G = D[Row + X * 4 + 1]; R = D[Row + X * 4 + 2]; A = D[Row + X * 4 + 3]; bAnyAlpha |= A != 0; }
                    else if (Bpp == 24) { B = D[Row + X * 3]; G = D[Row + X * 3 + 1]; R = D[Row + X * 3 + 2]; }
                    else
                    {
                        const int64 Bit = int64(X) * Bpp;
                        const int32 Index = (D[Row + Bit / 8] >> (8 - Bpp - int32(Bit % 8))) & ((1 << Bpp) - 1);
                        const int64 Entry = Palette + Index * 4;
                        if (Index < PaletteCount && Entry + 3 < D.Num()) { B = D[Entry]; G = D[Entry + 1]; R = D[Entry + 2]; }
                    }
                    uint8* P = Out.RGBA.GetData() + (Y * W + X) * 4;
                    P[0] = R; P[1] = G; P[2] = B; P[3] = A;
                }
            }
            // Without per-pixel alpha the AND mask says which pixels are see-through (bit set).
            if (!bAnyAlpha)
            {
                for (int32 Y = 0; Y < H; ++Y)
                {
                    const int64 Row = Mask + MaskStride * (H - 1 - Y);
                    for (int32 X = 0; X < W; ++X)
                        if ((D[Row + X / 8] >> (7 - X % 8)) & 1) Out.RGBA[(Y * W + X) * 4 + 3] = 0;
                }
            }
        }
        Out.HotX = Out.Width > 0 ? float(HotXPx) / float(BestSize > 0 ? BestSize : Out.Width) : -1.f;
        Out.HotY = Out.Height > 0 ? float(HotYPx) / float(BestSize > 0 ? BestSize : Out.Height) : -1.f;
        return true;
    }

    UTexture2D* MakeTexture(const UWowCursorManager::FCursorPixels& Pixels)
    {
        UTexture2D* Texture = UTexture2D::CreateTransient(Pixels.Width, Pixels.Height, PF_R8G8B8A8);
        if (!Texture) return nullptr;
        Texture->SRGB = true;
        Texture->NeverStream = true;
        Texture->Filter = TF_Bilinear;
        void* Destination = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(Destination, Pixels.RGBA.GetData(), Pixels.RGBA.Num());
        Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
        Texture->UpdateResource();
        return Texture;
    }
}

FString UWowCursorManager::GetCursorSetsFolder()
{
    return FPaths::Combine(FMpqManager::FindUserRoot(FPaths::ProjectDir()), TEXT("Cursor"));
}

TArray<FString> UWowCursorManager::ListCursorSets()
{
    TArray<FString> Sets;
    IFileManager::Get().FindFiles(Sets, *(GetCursorSetsFolder() / TEXT("*")), false, true);
    Sets.Sort();
    return Sets;
}

FString UWowCursorManager::GetActiveCursorSet()
{
    using namespace WowCursorSets;
    if (!bGActiveSetRead)
    {
        bGActiveSetRead = true;
        if (GConfig) GConfig->GetString(ConfigSection, TEXT("Set"), GActiveSet, GGameUserSettingsIni);
        // A set whose folder was removed falls back to the client's cursors.
        if (!GActiveSet.IsEmpty() && !FPaths::DirectoryExists(GetCursorSetsFolder() / GActiveSet)) GActiveSet.Reset();
    }
    return GActiveSet;
}

void UWowCursorManager::SetActiveCursorSet(const FString& SetName)
{
    using namespace WowCursorSets;
    bGActiveSetRead = true;
    GActiveSet = SetName;
    if (GConfig)
    {
        GConfig->SetString(ConfigSection, TEXT("Set"), *SetName, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
    UE_LOG(LogWowCursor, Log, TEXT("Cursor set chosen: '%s'"), *SetName);
}

FVector2D UWowCursorManager::GetHotspot(EWowCursorType Type) const
{
    const FCursorPixels* Pixels = CursorPixels.Find(Type);
    if (!Pixels) Pixels = CursorPixels.Find(EWowCursorType::Point);
    if (Pixels && Pixels->HotX >= 0.f && Pixels->HotY >= 0.f) return FVector2D(Pixels->HotX, Pixels->HotY);
    // The gauntlet's fingertip: pixel (7,5) of the 64 px art = (4,3) at 38 px.
    return FVector2D(4.0f / 38.0f, 3.0f / 38.0f);
}

/** r352: a .ani (RIFF "ACON"): every frame (each a .cur/.ico), in the sequence and at the rates it gives. */
static bool WowParseAni(const TArray<uint8>& D, UWowCursorManager::FCursorPixels& Out)
{
    using namespace WowCursorSets;
    if (D.Num() < 12 || FMemory::Memcmp(D.GetData(), "RIFF", 4) != 0 || FMemory::Memcmp(D.GetData() + 8, "ACON", 4) != 0) return false;
    TArray<UWowCursorManager::FCursorPixels> Icons;
    TArray<uint32> Rates, Sequence;
    uint32 DefaultRate = 6; // jiffies (1/60 s)
    int64 At = 12;
    while (At + 8 <= D.Num())
    {
        const uint8* Id = D.GetData() + At;
        const int64 Size = ReadU32(D, At + 4);
        const int64 Body = At + 8;
        if (Body + Size > D.Num()) break;
        if (FMemory::Memcmp(Id, "anih", 4) == 0 && Size >= 36)
        {
            DefaultRate = FMath::Max<uint32>(1, ReadU32(D, Body + 28));
        }
        else if (FMemory::Memcmp(Id, "rate", 4) == 0)
        {
            for (int64 Each = 0; Each + 4 <= Size; Each += 4) Rates.Add(ReadU32(D, Body + Each));
        }
        else if (FMemory::Memcmp(Id, "seq ", 4) == 0)
        {
            for (int64 Each = 0; Each + 4 <= Size; Each += 4) Sequence.Add(ReadU32(D, Body + Each));
        }
        else if (FMemory::Memcmp(Id, "LIST", 4) == 0 && Size >= 4 && FMemory::Memcmp(D.GetData() + Body, "fram", 4) == 0)
        {
            int64 Sub = Body + 4;
            while (Sub + 8 <= Body + Size)
            {
                const int64 SubSize = ReadU32(D, Sub + 4);
                if (Sub + 8 + SubSize > D.Num()) break;
                if (FMemory::Memcmp(D.GetData() + Sub, "icon", 4) == 0)
                {
                    TArray<uint8> Icon(D.GetData() + Sub + 8, int32(SubSize));
                    UWowCursorManager::FCursorPixels Frame;
                    if (ParseCur(Icon, Frame)) Icons.Add(MoveTemp(Frame));
                }
                Sub += 8 + SubSize + (SubSize & 1);
            }
        }
        At = Body + Size + (Size & 1);
    }
    if (Icons.IsEmpty()) return false;
    Out = Icons[0];
    if (Sequence.IsEmpty()) for (int32 Index = 0; Index < Icons.Num(); ++Index) Sequence.Add(uint32(Index));
    for (int32 Step = 0; Step < Sequence.Num(); ++Step)
    {
        const UWowCursorManager::FCursorPixels* Icon = Icons.IsValidIndex(int32(Sequence[Step])) ? &Icons[int32(Sequence[Step])] : nullptr;
        if (!Icon || Icon->Width != Out.Width || Icon->Height != Out.Height) continue;
        UWowCursorManager::FCursorFrame& Frame = Out.Frames.AddDefaulted_GetRef();
        Frame.RGBA = Icon->RGBA;
        Frame.Seconds = float(Rates.IsValidIndex(Step) && Rates[Step] > 0 ? Rates[Step] : DefaultRate) / 60.0f;
    }
    if (Out.Frames.Num() < 2) Out.Frames.Reset();
    return true;
}

namespace WowCursorSets
{
    /** "NORMAL .ani" -> "normal", "h_arrow" -> "harrow", "Link Select" -> "linkselect". */
    FString Normalize(const FString& Name)
    {
        FString Out;
        for (const TCHAR Ch : Name.ToLower()) if (FChar::IsAlnum(Ch)) Out.AppendChar(Ch);
        return Out;
    }

    /** File names a cursor set may use for each cursor, best first: the client's own name, then the
     *  Windows roles cursor sets are made of (rw-designer: Normal, Link Select, Unavailable ...). */
    TArray<FString> Candidates(EWowCursorType Type, const FString& OwnName)
    {
        TArray<FString> Names = { Normalize(OwnName) };
        const TArray<FString> Unable = { TEXT("unablepoint"), TEXT("unavailable"), TEXT("unavabible"), TEXT("unavaible"), TEXT("unavailible"), TEXT("notallowed"), TEXT("no") };
        switch (Type)
        {
        case EWowCursorType::Point: Names.Append({ TEXT("normal"), TEXT("normalcursor"), TEXT("normalselect"), TEXT("arrow"), TEXT("pointer"), TEXT("cursor") }); break;
        // r353: hostile = red (Warcraft II HD: target2), neutral = yellow (target), friendly = green (target3).
        case EWowCursorType::Attack: Names.Append({ TEXT("red"), TEXT("attackhostile"), TEXT("hostile"), TEXT("target2"), TEXT("precision"), TEXT("precisionselect"), TEXT("cross"), TEXT("crosshair"), TEXT("target") }); break;
        case EWowCursorType::AttackNeutral: Names.Append({ TEXT("yellow"), TEXT("neutral"), TEXT("target") }); break;
        case EWowCursorType::AttackFriendly: Names.Append({ TEXT("green"), TEXT("friendly"), TEXT("target3") }); break;
        case EWowCursorType::Cast: Names.Append({ TEXT("target"), TEXT("precision"), TEXT("precisionselect"), TEXT("cross"), TEXT("crosshair") }); break;
        case EWowCursorType::Interact: Names.Append({ TEXT("linkselect"), TEXT("link"), TEXT("hand") }); break;
        case EWowCursorType::Pickup: Names.Append({ TEXT("loot") }); break;
        case EWowCursorType::UnablePoint: case EWowCursorType::UnableAttack: case EWowCursorType::UnableInteract:
        case EWowCursorType::UnableSpeak: case EWowCursorType::UnableBuy: case EWowCursorType::UnableQuest:
        case EWowCursorType::UnableQuestTurnIn: case EWowCursorType::UnablePickup: Names.Append(Unable); break;
        default: break;
        }
        return Names;
    }

    const TCHAR* const Extensions[] = { TEXT("png"), TEXT("tga"), TEXT("bmp"), TEXT("jpg"), TEXT("blp"), TEXT("cur"), TEXT("ico"), TEXT("ani") };
}

bool UWowCursorManager::LoadSetCursor(EWowCursorType CursorType, const TArray<FString>& Folders, const FString& FactionPrefix)
{
    using namespace WowCursorSets;
    const FString Name = GetCursorFilename(CursorType);
    const TArray<FString> Names = Candidates(CursorType, Name);
    FString File;
    FString Folder;
    for (const FString& Candidate : Folders)
    {
        // cursors.txt: "<CursorName> = <file>" lines choose a file for a cursor by hand.
        TArray<FString> Lines;
        if (FFileHelper::LoadFileToStringArray(Lines, *(Candidate / TEXT("cursors.txt"))))
        {
            for (const FString& Line : Lines)
            {
                FString Key, Value;
                if (Line.Split(TEXT("="), &Key, &Value) && Normalize(Key) == Normalize(Name))
                {
                    const FString Chosen = Candidate / Value.TrimStartAndEnd();
                    if (FPaths::FileExists(Chosen)) { File = Chosen; Folder = Candidate; break; }
                }
            }
        }
        if (!File.IsEmpty()) break;
        TArray<FString> Files;
        IFileManager::Get().FindFiles(Files, *(Candidate / TEXT("*.*")), true, false);
        TMap<FString, FString> ByName; // normalized base name -> file (supported formats only)
        for (const FString& Each : Files)
        {
            const FString Ext = FPaths::GetExtension(Each).ToLower();
            bool bSupported = false;
            for (const TCHAR* Known : Extensions) bSupported |= Ext == Known;
            if (bSupported) ByName.FindOrAdd(Normalize(FPaths::GetBaseFilename(Each)), Each);
        }
        for (const FString& Wanted : Names)
        {
            // Warcraft II style sets: h_arrow / o_arrow for Alliance / Horde.
            const FString* Found = FactionPrefix.IsEmpty() ? nullptr : ByName.Find(FactionPrefix + Wanted);
            if (!Found) Found = ByName.Find(Wanted);
            if (Found) { File = Candidate / *Found; Folder = Candidate; break; }
        }
        if (!File.IsEmpty()) break;
    }
    if (File.IsEmpty()) return false;

    FCursorPixels Pixels;
    TArray<uint8> Raw;
    bool bLoaded = FFileHelper::LoadFileToArray(Raw, *File);
    const FString Ext = FPaths::GetExtension(File).ToLower();
    if (!bLoaded) {}
    else if (Ext == TEXT("ani")) bLoaded = WowParseAni(Raw, Pixels);
    else if (Ext == TEXT("cur") || Ext == TEXT("ico")) bLoaded = ParseCur(Raw, Pixels);
    else if (Ext == TEXT("blp"))
    {
        const FBlpTexture Blp = FBlpParser::Parse(Raw);
        bLoaded = Blp.bIsValid && Blp.PixelFormat == EBlpPixelFormat::RGBA8 && !Blp.MipLevels.IsEmpty()
            && Blp.Width <= 256 && Blp.Height <= 256 && Blp.MipLevels[0].Data.Num() == int32(Blp.Width * Blp.Height * 4);
        if (bLoaded) { Pixels.Width = Blp.Width; Pixels.Height = Blp.Height; Pixels.RGBA = Blp.MipLevels[0].Data; }
    }
    else
    {
        FImage Image;
        bLoaded = FImageUtils::DecompressImage(Raw.GetData(), Raw.Num(), Image) && FromImage(Image, Pixels);
    }
    if (!bLoaded)
    {
        UE_LOG(LogWowCursor, Warning, TEXT("Cursor set file could not be read (or is bigger than 256x256): %s"), *File);
        return false;
    }
    // hotspot.txt: "<CursorName> <x> <y>" in pixels of the image, one per line (overrides a .cur's own).
    TArray<FString> HotLines;
    if (FFileHelper::LoadFileToStringArray(HotLines, *(Folder / TEXT("hotspot.txt"))))
    {
        for (const FString& Line : HotLines)
        {
            TArray<FString> Parts;
            Line.ParseIntoArrayWS(Parts);
            if (Parts.Num() >= 3 && Normalize(Parts[0]) == Normalize(Name) && Pixels.Width > 0 && Pixels.Height > 0)
            {
                Pixels.HotX = FCString::Atof(*Parts[1]) / float(Pixels.Width);
                Pixels.HotY = FCString::Atof(*Parts[2]) / float(Pixels.Height);
            }
        }
    }
    // A set's image without a stated hot spot clicks at its top-left corner, like a Windows cursor.
    if (Pixels.HotX < 0.f || Pixels.HotY < 0.f) { Pixels.HotX = 0.f; Pixels.HotY = 0.f; }
    UTexture2D* Texture = MakeTexture(Pixels);
    if (!Texture) return false;
    for (const FCursorFrame& Frame : Pixels.Frames)
    {
        FCursorPixels Still;
        Still.Width = Pixels.Width; Still.Height = Pixels.Height; Still.RGBA = Frame.RGBA;
        UTexture2D* FrameTexture = MakeTexture(Still);
        Pixels.FrameTextures.Add(FrameTexture ? AnimTextures.Add(FrameTexture) : INDEX_NONE);
    }
    UE_LOG(LogWowCursor, Log, TEXT("Cursor %s from %s (%dx%d%s)"), *Name, *File, Pixels.Width, Pixels.Height,
        Pixels.Frames.Num() > 1 ? *FString::Printf(TEXT(", %d frames"), Pixels.Frames.Num()) : TEXT(""));
    CursorTextures.Add(CursorType, Texture);
    CursorPixels.Add(CursorType, MoveTemp(Pixels));
    return true;
}

bool UWowCursorManager::IsAnimated(EWowCursorType Type) const
{
    const FCursorPixels* Pixels = CursorPixels.Find(Type);
    return Pixels && Pixels->Frames.Num() > 1;
}

int32 UWowCursorManager::CurrentFrame(const FCursorPixels& Pixels) const
{
    if (Pixels.Frames.Num() < 2) return 0;
    float Total = 0.f;
    for (const FCursorFrame& Frame : Pixels.Frames) Total += FMath::Max(0.01f, Frame.Seconds);
    float T = float(FMath::Fmod(FPlatformTime::Seconds(), double(Total)));
    for (int32 Index = 0; Index < Pixels.Frames.Num(); ++Index)
    {
        T -= FMath::Max(0.01f, Pixels.Frames[Index].Seconds);
        if (T < 0.f) return Index;
    }
    return Pixels.Frames.Num() - 1;
}

UTexture2D* UWowCursorManager::GetCursorTexture(EWowCursorType CursorType) const
{
    // r352: an animated set cursor shows its current frame.
    if (const FCursorPixels* Animated = CursorPixels.Find(CursorType); Animated && Animated->FrameTextures.Num() > 1)
    {
        const int32 Index = Animated->FrameTextures[FMath::Clamp(CurrentFrame(*Animated), 0, Animated->FrameTextures.Num() - 1)];
        if (AnimTextures.IsValidIndex(Index) && AnimTextures[Index]) return AnimTextures[Index];
    }
    const TObjectPtr<UTexture2D>* Found = CursorTextures.Find(CursorType);
    if (Found) return *Found;
    Found = CursorTextures.Find(EWowCursorType::Point);
    return Found ? *Found : nullptr;
}

UTexture2D* UWowCursorManager::LoadCursorTexture(const FString& CursorName)
{
    if (!MpqManager)
    {
        return nullptr;
    }

    FString CursorPath = FString::Printf(TEXT("Interface/Cursor/%s.blp"), *CursorName);

    TArray<uint8> Raw;
    if (!MpqManager->ReadFile(CursorPath, Raw)) return nullptr;
    const FBlpTexture Blp = FBlpParser::Parse(Raw);
    // The original 3.3.5 cursor icons are tiny paletted BLPs, decoded to RGBA8.
    if (!Blp.bIsValid || Blp.PixelFormat != EBlpPixelFormat::RGBA8 || Blp.MipLevels.IsEmpty()
        || Blp.Width > 256 || Blp.Height > 256 || Blp.MipLevels[0].Data.Num() != int32(Blp.Width * Blp.Height * 4)) return nullptr;
    UTexture2D* Texture = UTexture2D::CreateTransient(Blp.Width, Blp.Height, PF_R8G8B8A8);
    if (!Texture) return nullptr;
    Texture->SRGB = true;
    Texture->NeverStream = true;
    Texture->Filter = TF_Bilinear;
    void* Destination = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(Destination, Blp.MipLevels[0].Data.GetData(), Blp.MipLevels[0].Data.Num());
    Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
    Texture->UpdateResource();
    for (int32 Value = 0; Value <= int32(EWowCursorType::UnablePickup); ++Value)
    {
        const EWowCursorType Type = static_cast<EWowCursorType>(Value);
        if (GetCursorFilename(Type) == CursorName)
        {
            FCursorPixels& Pixels = CursorPixels.Add(Type);
            Pixels.Width = Blp.Width; Pixels.Height = Blp.Height; Pixels.RGBA = Blp.MipLevels[0].Data;
            break;
        }
    }
    return Texture;
}

UTexture2D* UWowCursorManager::LoadFallbackCursorTexture(EWowCursorType CursorType)
{
    const FString CursorPath = FPaths::ProjectContentDir()
        / TEXT("WowUI/Skins/WotlkLike/Cursors")
        / GetFallbackCursorFilename(CursorType);

    UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(CursorPath);
    if (!Texture)
    {
        UE_LOG(LogWowCursor, Warning, TEXT("Failed to load fallback cursor texture: %s"), *CursorPath);
        return nullptr;
    }

    Texture->SRGB = true;
    Texture->UpdateResource();
    return Texture;
}

FString UWowCursorManager::GetCursorFilename(EWowCursorType CursorType) const
{
    switch (CursorType)
    {
        case EWowCursorType::Point:         return TEXT("Point");
        case EWowCursorType::Attack:        return TEXT("Attack");
        case EWowCursorType::Speak:         return TEXT("Speak");
        case EWowCursorType::Buy:           return TEXT("Buy");
        case EWowCursorType::Quest:         return TEXT("Quest");
        case EWowCursorType::Inspect:       return TEXT("Inspect");
        case EWowCursorType::Pickup:        return TEXT("Pickup");
        case EWowCursorType::UnableAttack:  return TEXT("UnableAttack");
        case EWowCursorType::UnablePoint:   return TEXT("UnablePoint");
        case EWowCursorType::Cast: return TEXT("Cast");
        case EWowCursorType::Interact: return TEXT("Interact");
        case EWowCursorType::QuestTurnIn: return TEXT("QuestTurnIn");
        case EWowCursorType::UnableInteract: return TEXT("UnableInteract");
        case EWowCursorType::UnableSpeak: return TEXT("UnableSpeak");
        case EWowCursorType::UnableBuy: return TEXT("UnableBuy");
        case EWowCursorType::UnableQuest: return TEXT("UnableQuest");
        case EWowCursorType::UnableQuestTurnIn: return TEXT("UnableQuestTurnIn");
        case EWowCursorType::UnablePickup: return TEXT("UnablePickup");
        case EWowCursorType::AttackNeutral: return TEXT("AttackNeutral");
        case EWowCursorType::AttackFriendly: return TEXT("AttackFriendly");
        default:                            return TEXT("Point");
    }
}

FString UWowCursorManager::GetFallbackCursorFilename(EWowCursorType CursorType) const
{
    switch (CursorType)
    {
        case EWowCursorType::Point:         return TEXT("cursor_point.png");
        case EWowCursorType::Attack:        return TEXT("cursor_attack.png");
        case EWowCursorType::Speak:         return TEXT("cursor_speak.png");
        case EWowCursorType::Buy:           return TEXT("cursor_buy.png");
        case EWowCursorType::Quest:         return TEXT("cursor_quest.png");
        case EWowCursorType::Inspect:       return TEXT("cursor_inspect.png");
        case EWowCursorType::Pickup:        return TEXT("cursor_pickup.png");
        case EWowCursorType::UnableAttack:  return TEXT("cursor_unable_attack.png");
        case EWowCursorType::UnablePoint:   return TEXT("cursor_unable_point.png");
        default:                            return TEXT("cursor_point.png");
    }
}

bool UWowCursorManager::ApplyHardwareCursor(int32 Size)
{
#if PLATFORM_WINDOWS
    if (!FSlateApplication::IsInitialized()) return false;
    const TSharedPtr<ICursor> Cursor = FSlateApplication::Get().GetPlatformCursor();
    if (!Cursor.IsValid()) return false;
    Size = FMath::Clamp(Size, 16, 128);
    if (HardwareSize != Size) { ReleaseHardwareCursors(); HardwareSize = Size; }
    EWowCursorType Type = CurrentCursorType;
    const FCursorPixels* Pixels = CursorPixels.Find(Type);
    if (!Pixels) { Type = EWowCursorType::Point; Pixels = CursorPixels.Find(Type); }
    if (!Pixels) return false;
    // r352: an animated cursor has a hardware cursor per frame.
    const int32 Frame = FMath::Clamp(CurrentFrame(*Pixels), 0, 255);
    const TArray<uint8>& FrameRGBA = Pixels->Frames.IsValidIndex(Frame) ? Pixels->Frames[Frame].RGBA : Pixels->RGBA;
    const int32 HandleKey = (int32(Type) << 8) | Frame;
    void*& Handle = HardwareCursors.FindOrAdd(HandleKey);
    if (!Handle)
    {
        TArray<uint8> Scaled;
        Scaled.SetNumUninitialized(Size * Size * 4);
        for (int32 Y = 0; Y < Size; ++Y)
        for (int32 X = 0; X < Size; ++X)
        {
            const int32 Source = ((Y * Pixels->Height / Size) * Pixels->Width + X * Pixels->Width / Size) * 4;
            FMemory::Memcpy(Scaled.GetData() + (Y * Size + X) * 4, FrameRGBA.GetData() + Source, 4);
        }
        // Own the Windows resource explicitly. This also allows offscreen tests
        // to validate real native pixels without showing a window or moving the
        // user's pointer; the offscreen Slate cursor itself has no native factory.
        BITMAPINFO Bitmap = {};
        Bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        Bitmap.bmiHeader.biWidth = Size;
        Bitmap.bmiHeader.biHeight = -Size;
        Bitmap.bmiHeader.biPlanes = 1;
        Bitmap.bmiHeader.biBitCount = 32;
        Bitmap.bmiHeader.biCompression = BI_RGB;
        void* Bits = nullptr;
        HBITMAP Colour = ::CreateDIBSection(nullptr, &Bitmap, DIB_RGB_COLORS, &Bits, nullptr, 0);
        TArray<uint8> MaskBits;
        const int32 MaskStride = ((Size + 15) / 16) * 2;
        MaskBits.SetNumZeroed(MaskStride * Size);
        if (Colour && Bits)
        {
            uint8* BGRA = static_cast<uint8*>(Bits);
            for (int32 Pixel = 0; Pixel < Size * Size; ++Pixel)
            {
                BGRA[Pixel * 4] = Scaled[Pixel * 4 + 2];
                BGRA[Pixel * 4 + 1] = Scaled[Pixel * 4 + 1];
                BGRA[Pixel * 4 + 2] = Scaled[Pixel * 4];
                BGRA[Pixel * 4 + 3] = Scaled[Pixel * 4 + 3];
                if (Scaled[Pixel * 4 + 3] == 0)
                    MaskBits[(Pixel / Size) * MaskStride + (Pixel % Size) / 8] |= 1 << (7 - (Pixel % Size) % 8);
            }
            HBITMAP Mask = ::CreateBitmap(Size, Size, 1, 1, MaskBits.GetData());
            if (Mask)
            {
                ICONINFO Icon = {};
                Icon.fIcon = false;
                // The gauntlet's fingertip: pixel (7,5) of the 64 px art = (4,3) at 38 px.
                // (10,5) put the click point 6 px right of the tip (small map POIs missed).
                // r351: a cursor set's own hot spot (GetHotspot).
                const FVector2D Hot = GetHotspot(Type);
                Icon.xHotspot = FMath::Clamp(FMath::RoundToInt(Size * float(Hot.X)), 0, Size - 1);
                Icon.yHotspot = FMath::Clamp(FMath::RoundToInt(Size * float(Hot.Y)), 0, Size - 1);
                Icon.hbmColor = Colour;
                Icon.hbmMask = Mask;
                Handle = ::CreateIconIndirect(&Icon);
                ::DeleteObject(Mask);
            }
        }
        if (Colour) ::DeleteObject(Colour);
        if (!Handle) { HardwareCursors.Remove(HandleKey); return false; }
        UE_LOG(LogWowCursor, Log, TEXT("Created hardware WoW cursor %s (%dx%d)"), *GetCursorFilename(Type), Size, Size);
    }
    if (ActiveHardware != Handle)
    {
        for (const EMouseCursor::Type Shape : {EMouseCursor::Default, EMouseCursor::Hand, EMouseCursor::TextEditBeam})
            Cursor->SetTypeShape(Shape, Handle);
        // r352: the next frame of an animated cursor shows without waiting for the pointer to move.
        if (ActiveHardware && Pixels->Frames.Num() > 1) ::SetCursor(static_cast<HCURSOR>(Handle));
        ActiveHardware = Handle;
    }
    return true;
#else
    return false;
#endif
}

void UWowCursorManager::ReleaseHardwareCursors()
{
    if (ActiveHardware && FSlateApplication::IsInitialized())
    {
        if (const TSharedPtr<ICursor> Cursor = FSlateApplication::Get().GetPlatformCursor())
            for (const EMouseCursor::Type Shape : {EMouseCursor::Default, EMouseCursor::Hand, EMouseCursor::TextEditBeam})
                Cursor->SetTypeShape(Shape, nullptr);
    }
    ActiveHardware = nullptr;
#if PLATFORM_WINDOWS
    for (const auto& Pair : HardwareCursors) if (Pair.Value) ::DestroyCursor(static_cast<HCURSOR>(Pair.Value));
#endif
    HardwareCursors.Empty();
    HardwareSize = 0;
}

void UWowCursorManager::BeginDestroy()
{
    ReleaseHardwareCursors();
    Super::BeginDestroy();
}
