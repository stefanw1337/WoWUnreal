#include "WowMpqExtractCommandlet.h"
#include "Mpq/MpqManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Internationalization/Regex.h"

namespace
{
FString MpqExtractNormalize(FString Path)
{
    Path.ReplaceInline(TEXT("/"), TEXT("\\"));
    Path.TrimStartAndEndInline();
    return Path;
}

FString MpqExtractText(const TArray<uint8>& Data)
{
    const int32 Skip = Data.Num() >= 3 && Data[0] == 0xEF && Data[1] == 0xBB && Data[2] == 0xBF ? 3 : 0;
    FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Data.GetData()) + Skip, Data.Num() - Skip);
    return FString(Conv.Length(), Conv.Get());
}
}

int32 UWowMpqExtractCommandlet::Main(const FString& Params)
{
    // r594: the client moved to X:\WoW (the game logs "WoW Data Path: X:\WoW\..."); the first folder that
    // exists is used, data= on the command line still wins.
    FString DataPath = TEXT("E:/World of Warcraft 3.3.5a - TheraWoW/Data");
    for (const TCHAR* Candidate : { TEXT("X:/WoW/World of Warcraft 3.3.5a - TheraWoW/Data"), TEXT("E:/World of Warcraft 3.3.5a - TheraWoW/Data") })
        if (FPaths::DirectoryExists(Candidate)) { DataPath = Candidate; break; }
    FParse::Value(*Params, TEXT("data="), DataPath);
    bool bFullProfile = false;
    // r611: a "data=<folder>" line in mpqextract.txt picks another client's Data folder (e.g. the HD pack
    // install), since the build watcher passes no command-line arguments.
    {
        TArray<FString> Head;
        if (FFileHelper::LoadFileToStringArray(Head, *(FPaths::ProjectSavedDir() / TEXT("ClaudeBuild/mpqextract.txt"))))
            for (const FString& Raw : Head)
            {
                const FString Line = Raw.TrimStartAndEnd();
                if (Line.StartsWith(TEXT("data="))) { DataPath = Line.Mid(5); UE_LOG(LogTemp, Display, TEXT("MpqExtract: data folder %s"), *DataPath); }
                // r611: "profile=full" reads like the game does (lettered patches and loose files too).
                if (Line.Equals(TEXT("profile=full"), ESearchCase::IgnoreCase)) bFullProfile = true;
            }
    }
    FMpqManager Mpq;
    // The locale archives are picked from the client's folders and WTF/Config.wtf through
    // the DataSource preference (empty in an editor commandlet, which would force enUS).
    if (GConfig)
        GConfig->SetString(TEXT("WowUnreal.ClientPreferences"), TEXT("DataSource"), *DataPath, GGameUserSettingsIni);
    if (!Mpq.Initialize(DataPath, {}, bFullProfile ? EWowAssetProfile::WithOverrides : EWowAssetProfile::OriginalWotlk335))
    {
        UE_LOG(LogTemp, Error, TEXT("MpqExtract: cannot open %s"), *DataPath);
        return 1;
    }
    TArray<FString> Lines;
    const FString ListFile = FPaths::ProjectSavedDir() / TEXT("ClaudeBuild/mpqextract.txt");
    if (!FFileHelper::LoadFileToStringArray(Lines, *ListFile))
    {
        UE_LOG(LogTemp, Error, TEXT("MpqExtract: missing %s"), *ListFile);
        return 2;
    }
    const FString OutRoot = FPaths::ProjectSavedDir() / TEXT("Reference/Extracted");
    TSet<FString> Done;
    int32 Written = 0, Missing = 0;

    // Queue entries: path + whether textures named inside should be fetched too.
    TArray<TPair<FString, bool>> Queue;
    for (const FString& Raw : Lines)
    {
        FString Line = Raw.TrimStartAndEnd();
        if (Line.IsEmpty() || Line.StartsWith(TEXT("#")) || Line.StartsWith(TEXT("data=")) || Line.StartsWith(TEXT("profile="))) continue;
        const bool bTex = Line.EndsWith(TEXT("+tex"));
        if (bTex) Line.LeftChopInline(4);
        Queue.Emplace(MpqExtractNormalize(Line), bTex);
    }

    const FRegexPattern XmlRef(TEXT("(?:file|texture)\\s*=\\s*\"([^\"]+)\""));
    const FRegexPattern LuaTex(TEXT("[\"'](Interface\\\\\\\\[^\"']+)[\"']"));
    for (int32 Index = 0; Index < Queue.Num(); ++Index)
    {
        const FString Path = Queue[Index].Key;
        const bool bTex = Queue[Index].Value;
        if (Done.Contains(Path.ToLower())) continue;
        Done.Add(Path.ToLower());

        FString Read = Path;
        TArray<uint8> Data;
        // r611: say when a loose file in the client folder answers instead of the archives.
        for (const FString& Root : { FPaths::GetPath(DataPath), DataPath })
            if (FPaths::FileExists(Root / Path.Replace(TEXT("\\"), TEXT("/"))))
                UE_LOG(LogTemp, Display, TEXT("MpqExtract: loose file %s"), *(Root / Path));
        if (!Mpq.ReadFile(Read, Data))
        {
            // Textures are named without extension in XML/Lua.
            if (FPaths::GetExtension(Read).IsEmpty() && Mpq.ReadFile(Read + TEXT(".blp"), Data)) Read += TEXT(".blp");
            else { UE_LOG(LogTemp, Warning, TEXT("MpqExtract: not found %s"), *Path); ++Missing; continue; }
        }
        const FString Out = OutRoot / Read.Replace(TEXT("\\"), TEXT("/"));
        FFileHelper::SaveArrayToFile(Data, *Out);
        // r309: also a flat copy (Saved/Reference/Flat/<file name>). The device bridge can only
        // stage files up to 7 folders below the project, and World\wmo\...\Stormwind is deeper.
        FFileHelper::SaveArrayToFile(Data, *(FPaths::ProjectSavedDir() / TEXT("Reference/Flat") / FPaths::GetCleanFilename(Read)));
        ++Written;

        const FString Ext = FPaths::GetExtension(Read).ToLower();
        const FString Dir = FPaths::GetPath(Read.Replace(TEXT("\\"), TEXT("/"))).Replace(TEXT("/"), TEXT("\\"));
        if (Ext == TEXT("toc"))
        {
            TArray<FString> TocLines;
            MpqExtractText(Data).ParseIntoArrayLines(TocLines);
            for (FString TocLine : TocLines)
            {
                TocLine.TrimStartAndEndInline();
                if (TocLine.IsEmpty() || TocLine.StartsWith(TEXT("#"))) continue;
                Queue.Emplace(MpqExtractNormalize(Dir / TocLine), bTex);
            }
        }
        else if (Ext == TEXT("xml") || Ext == TEXT("lua"))
        {
            const FString Text = MpqExtractText(Data);
            if (Ext == TEXT("xml"))
            {
                FRegexMatcher Matcher(XmlRef, Text);
                while (Matcher.FindNext())
                {
                    const FString Ref = MpqExtractNormalize(Matcher.GetCaptureGroup(1));
                    const FString RefExt = FPaths::GetExtension(Ref).ToLower();
                    if (RefExt == TEXT("lua") || RefExt == TEXT("xml")) Queue.Emplace(MpqExtractNormalize(Dir / Ref), bTex);
                    else if (bTex && Ref.StartsWith(TEXT("Interface\\"), ESearchCase::IgnoreCase)) Queue.Emplace(Ref, false);
                }
            }
            if (bTex)
            {
                FRegexMatcher Matcher(LuaTex, Text);
                while (Matcher.FindNext())
                    Queue.Emplace(MpqExtractNormalize(Matcher.GetCaptureGroup(1).Replace(TEXT("\\\\"), TEXT("\\"))), false);
            }
        }
    }
    UE_LOG(LogTemp, Display, TEXT("MpqExtract: %d files written to %s, %d not found"), Written, *OutRoot, Missing);
    return 0;
}
