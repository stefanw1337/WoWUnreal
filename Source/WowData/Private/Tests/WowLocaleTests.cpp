#include "WowLocale.h"
#include "Formats/DbcParser.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLocaleColumns, "WowUnreal.Locale.DbcColumns",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowLocaleColumns::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("British and American English share a DBC column"), WowLocale::DbcIndexForLocale(TEXT("enGB")), 0u);
    TestEqual(TEXT("Russian uses column eight"), WowLocale::DbcIndexForLocale(TEXT("ruRU")), 8u);
    TestEqual(TEXT("German uses column three"), WowLocale::DbcIndexForLocale(TEXT("deDE")), 3u);
    TestEqual(TEXT("Mexican Spanish uses column seven"), WowLocale::DbcIndexForLocale(TEXT("esMX")), 7u);
    // Exercise a real WDBC record with sparse localized strings.
    TArray<uint8> Bytes; Bytes.SetNumZeroed(20 + 17 * 4 + 9);
    uint32 Header[] = {0x43424457, 1, 17, 17 * 4, 9};
    FMemory::Memcpy(Bytes.GetData(), Header, sizeof(Header));
    const uint32 Offset = 1;
    FMemory::Memcpy(Bytes.GetData() + 20, &Offset, 4);
    FMemory::Memcpy(Bytes.GetData() + 20 + 17 * 4 + 1, "English", 8);
    FDbcParser Parser;
    TestTrue(TEXT("Fixture parses"), Parser.Parse(Bytes));
    TestEqual(TEXT("Absent translation falls back to English"), Parser.GetLocalizedString(0, 0), FString(TEXT("English")));
    TestTrue(TEXT("Invalid record stays empty"), Parser.GetLocalizedString(3, 0).IsEmpty());
    return true;
}
#endif
