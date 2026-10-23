#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "WowLuaVM.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLuaEncodingTest,"WowUnreal.UI.Lua.Utf8SourceEncoding",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowLuaEncodingTest::RunTest(const FString&)
{
    FWowLuaVM VM;
    if(!TestTrue(TEXT("Initialize Lua"),VM.Initialize())) return false;
    const ANSICHAR* Code="function LocalizeFrames() localizationCalled=true end";
    TArray<uint8> Data={0xef,0xbb,0xbf};
    Data.Append(reinterpret_cast<const uint8*>(Code),FCStringAnsi::Strlen(Code));
    TestTrue(TEXT("BOM-prefixed FrameXML defines its functions"),VM.ExecuteBuffer(Data,TEXT("Localization.lua")));
    TestTrue(TEXT("Subsequent UI startup can call localization"),VM.ExecuteString(TEXT("LocalizeFrames(); assert(localizationCalled)")));
    TestTrue(TEXT("FString sources accept the same prefix"),VM.ExecuteString(FString::Chr(TCHAR(0xfeff))+TEXT("stringLoaded=true")));
    TestTrue(TEXT("Prefix-free source still works"),VM.ExecuteString(TEXT("assert(stringLoaded)")));
    TestTrue(TEXT("Empty source is valid"),VM.ExecuteBuffer({},TEXT("empty.lua")));
    TestTrue(TEXT("BOM-only source is valid"),VM.ExecuteBuffer({0xef,0xbb,0xbf},TEXT("bom-only.lua")));
    TestTrue(TEXT("BOM inside string data remains intact"),VM.ExecuteString(TEXT("assert(#'\ufeff' == 3)")));
    AddExpectedError(TEXT("Lua load invalid.lua:"),EAutomationExpectedErrorFlags::Contains,1);
    TestFalse(TEXT("Real syntax errors remain visible"),VM.ExecuteBuffer({'l','o','c','a','l',' ','='},TEXT("invalid.lua")));
    return true;
}
#endif
