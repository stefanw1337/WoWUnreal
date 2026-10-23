#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowKeyBindings.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowActionBarInputTest,"WowUnreal.UI.ActionBars.MouseAndPaging",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowActionBarInputTest::RunTest(const FString&)
{
    const int32 OldPage=FWowKeyBindings::ActionPage();ON_SCOPE_EXIT{FWowKeyBindings::SetActionPage(OldPage);};
    auto* RootCanvas=NewObject<UCanvasPanel>();
    // Match a live viewport: visibility queries require a constructed Slate widget.
    const auto SlateRoot=RootCanvas->TakeWidget();
    FWowFrameManager Frames;Frames.Initialize(RootCanvas);
    FWowEventSystem Events;Events.SetFrameManager(&Frames);Frames.SetEventSystem(&Events);
    auto Create=[&](const FString& Name,const FString& Parent,float X,float Y,float W,float H,bool Button=false,bool Hidden=false)
    {
        FWowFrameDef D;D.Name=Name;D.Parent=Parent;D.Width=W;D.Height=H;D.bHidden=Hidden;
        D.bEnableMouse=Name==TEXT("MainMenuBar");D.Type=Button?EWowFrameType::Button:EWowFrameType::Frame;
        FWowAnchor A;A.Point=EWowAnchorPoint::TOPLEFT;A.RelativePoint=A.Point;A.RelativeTo=Parent;A.OffsetX=X;A.OffsetY=-Y;D.Anchors.Add(A);
        return Frames.CreateFrame(D);
    };
    const auto Bar=Create(TEXT("MainMenuBar"),TEXT("UIParent"),0,400,1024,53);
    const auto Art=Create(TEXT("MainMenuBarArtFrame"),TEXT("MainMenuBar"),0,0,1024,53);
    TArray<int64> Slots;
    for(int32 I=0;I<12;++I)Slots.Add(Create(FString::Printf(TEXT("ActionButton%d"),I+1),TEXT("MainMenuBarArtFrame"),8+I*41,14,36,36,true,true));
    const auto Up=Create(TEXT("ActionBarUpButton"),TEXT("MainMenuBarArtFrame"),506,6,32,32,true);
    const auto Down=Create(TEXT("ActionBarDownButton"),TEXT("MainMenuBarArtFrame"),506,26,32,32,true);
    Frames.SetFrameHitRectInsets(Up,FMargin(6,7,6,7));Frames.SetFrameHitRectInsets(Down,FMargin(6,7,6,7));
    const auto Micro=Create(TEXT("CharacterMicroButton"),TEXT("MainMenuBarArtFrame"),560,14,28,36,true);
    const auto Bag=Create(TEXT("MainMenuBarBackpackButton"),TEXT("MainMenuBarArtFrame"),960,14,36,36,true);
    Frames.RefreshMainActionBarControls();
    for(int32 I=0;I<12;++I)
    {
        TestTrue(TEXT("Native slot is logically visible"),Frames.IsFrameVisible(Slots[I]));
        TestEqual(TEXT("Each action button wins over bar background"),Frames.HitTestFrames(26+I*41,432),Slots[I]);
    }
    TestEqual(TEXT("Micro button wins over bar background"),Frames.HitTestFrames(574,432),Micro);
    TestEqual(TEXT("Backpack wins over bar background"),Frames.HitTestFrames(978,432),Bag);
    TestEqual(TEXT("Up arrow receives mouse click"),Frames.HitTestFrames(522,422),Up);
    TestEqual(TEXT("Down arrow receives mouse click"),Frames.HitTestFrames(522,442),Down);
    auto* Label=Frames.GetNamedFontString(TEXT("MainMenuBarPageNumber"));
    if(!TestNotNull(TEXT("Page label"),Label))return false;
    auto* LabelSlot=Cast<UCanvasPanelSlot>(Label->Slot);
    TestTrue(TEXT("Page number belongs to arrow canvas"),Label->GetParent()==Frames.GetWidgetForHandle(Art));
    TestTrue(TEXT("Page label does not retain center anchor offset"),LabelSlot->GetAnchors().Minimum.Equals(FVector2D::ZeroVector));
    TestTrue(TEXT("Page label sits next to arrows"),LabelSlot->GetPosition().Equals(FVector2D(539,22)));
    int32 Dispatched=-1,Count=0;Frames.OnSecureActionDispatch.BindLambda([&](int32 Slot){Dispatched=Slot;++Count;});
    Frames.SetActionBarPage(1);
    Frames.DispatchClick(Up,TEXT("LeftButton"),true);
    TestEqual(TEXT("Press does not double-advance page"),FWowKeyBindings::ActionPage(),1);
    Frames.DispatchClick(Up,TEXT("LeftButton"),false);
    TestEqual(TEXT("Up selects second bar"),FWowKeyBindings::ActionPage(),2);
    TestEqual(TEXT("Number follows actual page"),Label->GetText().ToString(),FString(TEXT("2")));
    Frames.DispatchClick(Slots[0],TEXT("LeftButton"),false);
    TestEqual(TEXT("Click activates second-page slot"),Dispatched,12);
    TestEqual(TEXT("Single click dispatches once"),Count,1);
    Frames.DispatchClick(Down,TEXT("LeftButton"),false);
    TestEqual(TEXT("Down selects previous bar"),FWowKeyBindings::ActionPage(),1);
    Frames.DispatchClick(Down,TEXT("LeftButton"),false);
    TestEqual(TEXT("Down wraps first bar to second bar"),FWowKeyBindings::ActionPage(),2);
    Frames.DispatchClick(Up,TEXT("LeftButton"),false);
    TestEqual(TEXT("Up wraps second bar to first bar"),FWowKeyBindings::ActionPage(),1);
    Frames.DispatchClick(Slots[0],TEXT("LeftButton"),false);
    TestEqual(TEXT("Returning to first page restores first slot"),Dispatched,0);
    Frames.DispatchClick(Up,TEXT("RightButton"),false);
    TestEqual(TEXT("Right click does not change page"),FWowKeyBindings::ActionPage(),1);
    // Exercise both physical edges on every main/extra bar, not only a direct release.
    for (const TCHAR* Prefix : {TEXT("ActionButton"), TEXT("MultiBarBottomLeftButton"),
        TEXT("MultiBarBottomRightButton"), TEXT("MultiBarRightButton"), TEXT("MultiBarLeftButton")})
    {
        FString Name=FString(Prefix)+TEXT("1");
        int64 Handle=Frames.FindFrame(Name);
        if(Handle<0) Handle=Create(Name,TEXT("MainMenuBarArtFrame"),0,0,36,36,true);
        Frames.SetFrameRegisteredClicks(Handle,{TEXT("LeftButtonUp")});
        for(int32 Click=0;Click<2;++Click)
        {
            const int32 Before=Count;
            Frames.DispatchMouseDown(Handle,TEXT("LeftButton"));
            if(Frames.IsFrameClickRegistered(Handle,TEXT("LeftButton"),true)) Frames.DispatchClick(Handle,TEXT("LeftButton"),true);
            TestEqual(TEXT("Press alone does not activate release-bound ability"),Count,Before);
            Frames.DispatchMouseUp(Handle,TEXT("LeftButton"));
            if(Frames.IsFrameClickRegistered(Handle,TEXT("LeftButton"),false)) Frames.DispatchClick(Handle,TEXT("LeftButton"),false);
            TestEqual(Name+TEXT(" activates once per click, including the first"),Count,Before+1);
            TestEqual(TEXT("Extra bar dispatch uses its own slot"),Dispatched,Frames.GetActionSlotForFrame(Handle));
        }
    }
    Frames.SetFrameVisible(Bar,false);Frames.RefreshMainActionBarControls();
    TestEqual(TEXT("Hidden HUD does not regain mouse interception"),Frames.HitTestFrames(26,432),int64(-1));
    Frames.SetEventSystem(nullptr);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowBonusActionPagingTest,"WowUnreal.Windows.BonusActionPaging",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowBonusActionPagingTest::RunTest(const FString&)
{
 const int32 Page=FWowKeyBindings::ManualActionPage(),Bonus=FWowKeyBindings::BonusBarOffset();ON_SCOPE_EXIT{FWowKeyBindings::SetActionPage(Page);FWowKeyBindings::SetBonusBarOffset(Bonus);};
 FWowKeyBindings::SetActionPage(1);FWowKeyBindings::SetBonusBarOffset(1);
 TestEqual(TEXT("Stealth/cat uses slots 73-84"),FWowKeyBindings::ActionPage(),7);
 FWowKeyBindings::SetBonusBarOffset(2);TestEqual(TEXT("Prowl uses slots 85-96"),FWowKeyBindings::ActionPage(),8);
 FWowKeyBindings::SetBonusBarOffset(3);TestEqual(TEXT("Bear uses slots 97-108"),FWowKeyBindings::ActionPage(),9);
 FWowKeyBindings::SetActionPage(2);TestEqual(TEXT("Manual second bar stays available"),FWowKeyBindings::ActionPage(),2);
 FWowKeyBindings::SetActionPage(1);TestEqual(TEXT("Returning to main restores form bar"),FWowKeyBindings::ActionPage(),9);
 FWowKeyBindings::SetBonusBarOffset(0);TestEqual(TEXT("Leaving form restores normal main bar"),FWowKeyBindings::ActionPage(),1);
 return true;
}
#endif
