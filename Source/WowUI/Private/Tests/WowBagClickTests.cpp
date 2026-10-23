#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "WowFrameManager.h"
#include "WowFrameXmlParser.h"
#include "WowEventSystem.h"
#include "WowLuaVM.h"
#include "LuaApi/LuaApiRegistry.h"
#include "Components/CanvasPanel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowBagClickTest,"WowUnreal.UI.Bags.StockFrameXmlClickLifecycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowBagClickTest::RunTest(const FString&)
{
    // Locally extracted client data, supplied by the test runner; never ship it.
    FString Fixture;
    if (!FParse::Value(FCommandLine::Get(),TEXT("bagframexml="),Fixture))
    {
        AddWarning(TEXT("Stock bag integration requires -bagframexml=<local FrameXML directory>"));
        return true;
    }
    FWowLuaVM VM;if(!VM.Initialize())return false;
    FWowFrameManager Frames;FWowEventSystem Events;
    Events.SetLuaVM(&VM);Events.SetFrameManager(&Frames);Frames.SetEventSystem(&Events);
    FWowLuaContext Context;Context.FrameManager=&Frames;Context.EventSystem=&Events;
    WowLuaApi::SetContext(VM.GetState(),&Context);
    ON_SCOPE_EXIT{Frames.SetEventSystem(nullptr);WowLuaApi::ClearContext(VM.GetState());};
    Frames.Initialize(NewObject<UCanvasPanel>());
    FString Lua;
    if(!FFileHelper::LoadFileToString(Lua,*(Fixture/TEXT("ContainerFrame.lua"))))return false;
    if(!TestTrue(TEXT("Stock container Lua loads"),VM.ExecuteString(Lua,TEXT("=StockContainerFrame"))))return false;
    // Fixture provides server bag sizes and unrelated UI widgets. Container
    // generation, IDs, geometry, visibility and lifecycle are the real code.
    TestTrue(TEXT("Fixture globals"),VM.ExecuteString(TEXT(R"LUA(
        NUM_BAG_SLOTS=4; KEYRING_CONTAINER=-2
        function GetContainerNumSlots(id) return ({[0]=16,[1]=6,[2]=0,[3]=20,[4]=0})[id] or 0 end
        function GetBagName(id) return id==0 and "Backpack" or "Test bag" end
        function SetItemButtonTexture() end
        function SetItemButtonCount() end
        function SetItemButtonDesaturated() end
        function UpdateMicroButtons() end
        function UpdateBagButtonHighlight() end
        GameTooltip=CreateFrame("Frame","BagTestTooltip",UIParent)
        BankFrame=CreateFrame("Frame","BankFrame",UIParent); BankFrame:Hide()
        MainMenuBarBackpackButton=CreateFrame("CheckButton","MainMenuBarBackpackButton",UIParent)
        for i=0,3 do CreateFrame("CheckButton","CharacterBag"..i.."Slot",UIParent) end
    )LUA")));
    TArray<uint8> Xml;
    if(!FFileHelper::LoadFileToArray(Xml,*(Fixture/TEXT("ContainerFrame.xml"))))return false;
    auto Definitions=FWowFrameXmlParser::ParseXml(Xml,TEXT("ContainerFrame.xml"));
    TFunction<void(FWowFrameDef&)> StripUnrelatedScripts=[&](FWowFrameDef& D)
    {
        if(D.Name!=TEXT("ContainerFrameTemplate"))D.Scripts.Empty();
        for(auto& Child:D.Children)StripUnrelatedScripts(Child);
    };
    Events.BeginOnLoadBatch();
    for(auto& Directive:Definitions)
    {
        if(Directive.Type!=FWowXmlDirective::EType::Frame)continue;
        StripUnrelatedScripts(Directive.FrameDef);
        if(Directive.FrameDef.bVirtual)Frames.RegisterTemplate(Directive.FrameDef.Name,Directive.FrameDef);
        else Frames.CreateFrame(Directive.FrameDef);
    }
    Events.EndOnLoadBatch();
    TestTrue(TEXT("Missing portrait API reproduces the pre-open failure"),VM.ExecuteString(TEXT(R"LUA(
        local api = SetBagPortraitTexture
        assert(type(api)=="function")
        SetBagPortraitTexture=nil
        local ok, err=pcall(ContainerFrame_GenerateFrame,ContainerFrame1,16,0)
        SetBagPortraitTexture=api
        assert(not ok and string.find(err,"SetBagPortraitTexture") and not ContainerFrame1:IsShown())
    )LUA")));
    auto Bag=[&](int32 Id){return Frames.FindFrame(FString::Printf(TEXT("ContainerFrame%d"),Id+1));};
    const int64 BackpackButton=Frames.FindFrame(TEXT("MainMenuBarBackpackButton"));
    Frames.DispatchClick(BackpackButton,TEXT("LeftButton"),true);
    TestFalse(TEXT("Press alone does not open then immediately close"),Frames.IsFrameVisible(Bag(0)));
    Frames.DispatchClick(BackpackButton,TEXT("LeftButton"),false);
    TestTrue(TEXT("Backpack mouse click actually opens window"),Frames.IsFrameVisible(Bag(0)));
    TestTrue(TEXT("All backpack slots and stock IDs generated"),VM.ExecuteString(TEXT("assert(ContainerFrame1:GetID()==0 and ContainerFrame1Item1:GetID()==16 and ContainerFrame1Item16:GetID()==1 and ContainerFrame1Item16:IsShown())")));
    Frames.DispatchClick(BackpackButton,TEXT("LeftButton"),false);
    TestFalse(TEXT("Second click closes"),Frames.IsFrameVisible(Bag(0)));
    Frames.DispatchClick(Frames.FindFrame(TEXT("CharacterBag2Slot")),TEXT("LeftButton"),false);
    TestTrue(TEXT("Equipped bag opens without backpack"),Frames.IsFrameVisible(Bag(3)));
    TestFalse(TEXT("Equipped bag never becomes backpack"),Frames.IsFrameVisible(Bag(0)));
    TestTrue(TEXT("20-slot bag is not truncated to 16 slots"),VM.ExecuteString(TEXT("assert(ContainerFrame4:GetID()==3 and ContainerFrame4Item20:IsShown() and ContainerFrame4Item20:GetID()==1)")));
    Frames.DispatchClick(Frames.FindFrame(TEXT("CharacterBag1Slot")),TEXT("LeftButton"),false);
    TestFalse(TEXT("Empty equipment slot does not open phantom bag"),Frames.IsFrameVisible(Bag(2)));
    Frames.ToggleAllPlayerBags();
    for(int32 I=0;I<5;++I)TestFalse(TEXT("All-bags shortcut closes open bag"),Frames.IsFrameVisible(Bag(I)));
    Frames.ToggleAllPlayerBags();
    TestTrue(TEXT("All-bags shortcut opens only equipped bags"),VM.ExecuteString(TEXT("assert(ContainerFrame1:IsShown() and ContainerFrame2:IsShown() and not ContainerFrame3:IsShown() and ContainerFrame4:IsShown() and not ContainerFrame5:IsShown() and ContainerFrame1.bagsShown==3 and #ContainerFrame1.bags==3)")));
    Frames.ToggleAllPlayerBags();
    for(int32 I=0;I<5;++I){Frames.TogglePlayerBag(0);Frames.TogglePlayerBag(0);}
    TestTrue(TEXT("Repeated toggles keep stock bookkeeping balanced"),VM.ExecuteString(TEXT("assert(ContainerFrame1.bagsShown==0 and #ContainerFrame1.bags==0)")));
    return true;
}
#endif
