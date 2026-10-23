#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowFrameManager.h"
#include "WowFrameXmlParser.h"
#include "WowEventSystem.h"
#include "WowLuaVM.h"
#include "LuaApi/LuaApiRegistry.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowXPOverlayTest,"WowUnreal.UI.XPOverlayUpdates",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowXPOverlayTest::RunTest(const FString&)
{
    FWowLuaVM VM;
    if (!TestTrue(TEXT("Lua initialized"),VM.Initialize())) return false;
    FWowFrameManager Frames;
    auto* Root=NewObject<UCanvasPanel>(); const auto SlateRoot=Root->TakeWidget(); Frames.Initialize(Root);
    FWowEventSystem Events; Events.SetLuaVM(&VM); Events.SetFrameManager(&Frames); Frames.SetEventSystem(&Events);
    FWowLuaContext Context; Context.FrameManager=&Frames; Context.EventSystem=&Events;
    WowLuaApi::SetContext(VM.GetState(),&Context);
    const FString Xml=TEXT("<Ui><StatusBar name='MainMenuExpBar' parent='UIParent'><Size><AbsDimension x='300' y='15'/></Size><Layers><Layer level='OVERLAY'><FontString name='MainMenuExpBarOverlayFrameText' text='0/1000 XP'/></Layer></Layers></StatusBar></Ui>");
    FTCHARToUTF8 Utf8(*Xml); TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());
    auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("XPTest.xml"));
    if (Parsed.Num()!=1) return false;
    const int64 Handle=Frames.CreateFrame(Parsed[0].FrameDef);
    Events.CreateFrameObject(Handle,TEXT("MainMenuExpBar"));
    auto* Label=Frames.GetNamedFontString(TEXT("MainMenuExpBarOverlayFrameText"));
    auto* Bar=Frames.GetStatusBarWidget(Handle);
    if (!TestNotNull(TEXT("XP overlay exists"),Label) || !TestNotNull(TEXT("XP fill exists"),Bar)) return false;
    TestTrue(TEXT("Initial XP"),VM.ExecuteString(TEXT("MainMenuExpBar:SetMinMaxValues(0,1000); MainMenuExpBar:SetValue(0)")));
    TestTrue(TEXT("Earn XP"),VM.ExecuteString(TEXT("MainMenuExpBar:SetValue(375)")));
    TestEqual(TEXT("Label follows earned XP"),Label->GetText().ToString(),FString(TEXT("XP 375 / 1000")));
    TestEqual(TEXT("Fill agrees with text"),Bar->GetPercent(),.375f);
    TestTrue(TEXT("Level up"),VM.ExecuteString(TEXT("MainMenuExpBar:SetValue(25); MainMenuExpBar:SetMinMaxValues(0,1500)")));
    TestEqual(TEXT("Max-only change updates text"),Label->GetText().ToString(),FString(TEXT("XP 25 / 1500")));
    TestTrue(TEXT("Zero maximum"),VM.ExecuteString(TEXT("MainMenuExpBar:SetValue(0); MainMenuExpBar:SetMinMaxValues(0,0)")));
    TestEqual(TEXT("Zero XP is not stale"),Label->GetText().ToString(),FString(TEXT("XP 0 / 0")));
    TestEqual(TEXT("Zero maximum fill is finite"),Bar->GetPercent(),0.f);
    WowLuaApi::ClearContext(VM.GetState());
    return true;
}
#endif
