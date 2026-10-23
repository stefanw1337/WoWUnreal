#include "WowLocale.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace WowLocale
{
static FString Resolve()
{
    const TCHAR* Codes[] = {TEXT("enUS"), TEXT("enGB"), TEXT("deDE"), TEXT("frFR"),
        TEXT("esES"), TEXT("esMX"), TEXT("ruRU"), TEXT("koKR"), TEXT("zhCN"), TEXT("zhTW")};
    FString Selected, Data;
    if (GConfig)
    {
        GConfig->GetString(TEXT("WowUnreal.ClientPreferences"), TEXT("Locale"), Selected, GGameUserSettingsIni);
        GConfig->GetString(TEXT("WowUnreal.ClientPreferences"), TEXT("DataSource"), Data, GGameUserSettingsIni);
    }
    if (!Data.IsEmpty() && !FPaths::GetCleanFilename(Data).Equals(TEXT("Data"), ESearchCase::IgnoreCase)
        && FPaths::DirectoryExists(Data / TEXT("Data"))) Data /= TEXT("Data");
    auto Available = [&](const FString& Code) { return Data.IsEmpty() || FPaths::DirectoryExists(Data / Code); };
    for (const TCHAR* Code : Codes)
        if (Selected.Equals(Code, ESearchCase::IgnoreCase) && Available(Code)) return Code;
    // Automatic selection honors the source WoW installation without modifying it.
    FString Config;
    if (!Data.IsEmpty() && FFileHelper::LoadFileToString(Config, *(FPaths::GetPath(Data) / TEXT("WTF/Config.wtf"))))
    {
        TArray<FString> Lines; Config.ParseIntoArrayLines(Lines);
        for (FString Line : Lines)
        {
            Line.TrimStartAndEndInline();
            TArray<FString> Tokens; Line.ParseIntoArrayWS(Tokens);
            if (Tokens.Num() != 3 || !Tokens[0].Equals(TEXT("SET"), ESearchCase::IgnoreCase)
                || !Tokens[1].Equals(TEXT("locale"), ESearchCase::IgnoreCase)) continue;
            const FString Value = Tokens[2].Replace(TEXT("\""), TEXT(""));
            for (const TCHAR* Code : Codes)
                if (Value.Equals(Code, ESearchCase::IgnoreCase) && Available(Code)) return Code;
        }
    }
    for (const TCHAR* Code : Codes) if (Available(Code)) return Code;
    return TEXT("enUS");
}
FString Get()
{
    // Language changes apply on restart; avoid filesystem work per DBC string.
    static const FString Selected = Resolve();
    return Selected;
}
uint32 DbcIndexForLocale(const FString& Code)
{
    if (Code == TEXT("koKR")) return 1;
    if (Code == TEXT("frFR")) return 2;
    if (Code == TEXT("deDE")) return 3;
    if (Code == TEXT("zhCN")) return 4;
    if (Code == TEXT("zhTW")) return 5;
    if (Code == TEXT("esES")) return 6;
    if (Code == TEXT("esMX")) return 7;
    if (Code == TEXT("ruRU")) return 8;
    return 0;
}
uint32 DbcIndex() { return DbcIndexForLocale(Get()); }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLocaleSelectionTest, "WowUnreal.Locale.Selection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowLocaleSelectionTest::RunTest(const FString& Parameters)
{
    const TCHAR* Section = TEXT("WowUnreal.ClientPreferences");
    FString OldLocale, OldData;
    const bool HadLocale = GConfig->GetString(Section, TEXT("Locale"), OldLocale, GGameUserSettingsIni);
    const bool HadData = GConfig->GetString(Section, TEXT("DataSource"), OldData, GGameUserSettingsIni);
    GConfig->SetString(Section, TEXT("DataSource"), TEXT(""), GGameUserSettingsIni);
    GConfig->SetString(Section, TEXT("Locale"), TEXT("enGB"), GGameUserSettingsIni);
    TestEqual(TEXT("Explicit British English"), WowLocale::Resolve(), FString(TEXT("enGB")));
    GConfig->SetString(Section, TEXT("Locale"), TEXT("RURU"), GGameUserSettingsIni);
    TestEqual(TEXT("Canonical Russian locale"), WowLocale::Resolve(), FString(TEXT("ruRU")));
    GConfig->SetString(Section, TEXT("Locale"), TEXT("invalid"), GGameUserSettingsIni);
    TestEqual(TEXT("Invalid locale has a safe fallback"), WowLocale::Resolve(), FString(TEXT("enUS")));
    if (HadLocale) GConfig->SetString(Section, TEXT("Locale"), *OldLocale, GGameUserSettingsIni);
    else GConfig->RemoveKey(Section, TEXT("Locale"), GGameUserSettingsIni);
    if (HadData) GConfig->SetString(Section, TEXT("DataSource"), *OldData, GGameUserSettingsIni);
    else GConfig->RemoveKey(Section, TEXT("DataSource"), GGameUserSettingsIni);
    return true;
}
#endif
