#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowFrameManager.h"
#include "WowFrameXmlParser.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowWindowHitRectTest,"WowUnreal.UI.WindowHitRectInsets",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowWindowHitRectTest::RunTest(const FString&)
{
    // Same dimensions and transparent margins as stock Character/PaperDollFrame.
    const FString Xml=TEXT("<Ui><Frame name='PortraitPanel' parent='UIParent' enableMouse='true'><Size><AbsDimension x='384' y='512'/></Size><Anchors><Anchor point='TOPLEFT' relativeTo='UIParent' relativePoint='TOPLEFT'/></Anchors><HitRectInsets><AbsInset left='0' right='30' top='0' bottom='45'/></HitRectInsets></Frame></Ui>");
    FTCHARToUTF8 Utf8(*Xml);TArray<uint8> Bytes;Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());
    auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("HitRectTest.xml"));
    if(!TestEqual(TEXT("XML frame parsed"),Parsed.Num(),1))return false;
    if(!TestTrue(TEXT("XML insets retained"),Parsed[0].FrameDef.HitRectInsets.IsSet()))return false;
    TestEqual(TEXT("Right transparent border"),Parsed[0].FrameDef.HitRectInsets->Right,30.f);
    TestEqual(TEXT("Bottom transparent border"),Parsed[0].FrameDef.HitRectInsets->Bottom,45.f);
    auto* RootCanvas=NewObject<UCanvasPanel>();
    // IsVisible() requires a constructed Slate widget, just as the live viewport does.
    const auto SlateRoot=RootCanvas->TakeWidget();
    FWowFrameManager Frames;Frames.Initialize(RootCanvas);
    const int64 Panel=Frames.CreateFrame(Parsed[0].FrameDef);
    TestEqual(TEXT("Visible panel body consumes clicks"),Frames.HitTestFrames(100,100),Panel);
    TestEqual(TEXT("Right border lets camera drag start in world"),Frames.HitTestFrames(370,100),int64(-1));
    TestEqual(TEXT("Bottom border does not block world"),Frames.HitTestFrames(100,490),int64(-1));
    FWowFrameDef Tab;Tab.Name=TEXT("InsetTestTab");Tab.Parent=TEXT("PortraitPanel");
    Tab.Type=EWowFrameType::Button;Tab.Width=80;Tab.Height=20;
    FWowAnchor A;A.Point=EWowAnchorPoint::TOPLEFT;A.RelativeTo=Tab.Parent;A.RelativePoint=A.Point;A.OffsetY=-480;Tab.Anchors.Add(A);
    const int64 TabHandle=Frames.CreateFrame(Tab);
    TestEqual(TEXT("Tabs beyond parent's hit rectangle stay clickable"),Frames.HitTestFrames(40,490),TabHandle);
    Frames.SetFrameVisible(Panel,false);
    TestEqual(TEXT("Hidden parent's child never intercepts camera input"),Frames.HitTestFrames(40,490),int64(-1));
    Frames.SetFrameVisible(Panel,true);
    Frames.SetFrameHitRectInsets(Panel,FMargin(0));
    TestEqual(TEXT("Runtime setter updates mouse region"),Frames.HitTestFrames(370,100),Panel);
    // Docking moves the live slot without rewriting the original FrameXML rect.
    // The old position must not eat world/right-drag clicks after a live miss.
    auto* Slot=Cast<UCanvasPanelSlot>(Frames.GetWidgetForHandle(Panel)->Slot);
    if (!TestNotNull(TEXT("Panel has a live slot"),Slot)) return false;
    Slot->SetPosition(FVector2D(500,0));
    TestEqual(TEXT("Old panel area is now world"),Frames.HitTestFrames(100,100),int64(-1));
    TestEqual(TEXT("Moved panel remains clickable"),Frames.HitTestFrames(600,100),Panel);
    auto Template=Parsed[0].FrameDef;Template.bVirtual=true;Frames.RegisterTemplate(TEXT("InsetTemplate"),Template);
    FWowFrameDef Derived;Derived.Name=TEXT("DerivedInsetFrame");Derived.Inherits=TEXT("InsetTemplate");Derived.Parent=TEXT("UIParent");Derived.bHidden=true;
    const int64 DerivedHandle=Frames.CreateFrame(Derived);
    TestEqual(TEXT("Template insets are inherited"),Frames.GetFrameDef(DerivedHandle)->HitRectInsets.Get(FMargin(0)).Right,30.f);
    return true;
}
#endif
