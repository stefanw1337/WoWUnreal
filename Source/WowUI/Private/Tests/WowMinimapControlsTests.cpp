#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Mpq/MpqManager.h"
#include "WowFrameManager.h"
#include "WowAssetCache.h"
#include "WowFrameXmlParser.h"
#include "WowEventSystem.h"
#include "WowLuaVM.h"
#include "LuaApi/LuaApiRegistry.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowMinimapControlsTest,"WowUnreal.UI.Minimap.StockButtons",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowMinimapControlsTest::RunTest(const FString&)
{
    FString Fixture, DataPath;
    if (!FParse::Value(FCommandLine::Get(),TEXT("minimapframexml="),Fixture) ||
        !FParse::Value(FCommandLine::Get(),TEXT("minimapdata="),DataPath))
    { AddError(TEXT("Supply local -minimapframexml and -minimapdata for stock button integration")); return false; }
    FMpqManager Mpq;
    if (!TestTrue(TEXT("Original UI assets available"),Mpq.Initialize(DataPath)))return false;
    for (const float Scale : {1.f, 1.40625f, 2.f})
    {
        FWowLuaVM VM; if(!VM.Initialize())return false;
        FWowAssetCache Cache;FWowFrameManager Frames;FWowEventSystem Events;
        Events.SetLuaVM(&VM);Events.SetFrameManager(&Frames);Frames.SetEventSystem(&Events);
        FWowLuaContext Context;Context.FrameManager=&Frames;Context.EventSystem=&Events;
        WowLuaApi::SetContext(VM.GetState(),&Context);
        ON_SCOPE_EXIT{Frames.SetEventSystem(nullptr);WowLuaApi::ClearContext(VM.GetState());};
        Frames.Initialize(NewObject<UCanvasPanel>());Frames.SetMpqManager(&Mpq);Frames.SetAssetCache(&Cache);
        Frames.SetUIScale(Scale);Frames.RawViewportScale=Scale;
        FWowFrameDef Dropdown;Dropdown.Name=TEXT("UIDropDownMenuTemplate");Dropdown.bVirtual=true;
        Frames.RegisterTemplate(Dropdown.Name,Dropdown);
        // Native button routing is tested here. Unrelated stock OnLoad functions
        // need the entire FrameXML runtime and are excluded from this fixture.
        TFunction<void(FWowFrameDef&)> StripScripts=[&](FWowFrameDef& D)
        { D.Scripts.Empty();for(auto& Child:D.Children)StripScripts(Child); };
        FWowFrameDef CalendarDefinition;
        for(const TCHAR* File:{TEXT("Minimap.xml"),TEXT("GameTime.xml")})
        {
            TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*(Fixture/File)))return false;
            for(auto& Directive:FWowFrameXmlParser::ParseXml(Bytes,File))
            {
                if(Directive.Type!=FWowXmlDirective::EType::Frame)continue;
                auto& D=Directive.FrameDef;StripScripts(D);
                if(D.Name==TEXT("GameTimeFrame"))CalendarDefinition=D;
                if(D.bVirtual)Frames.RegisterTemplate(D.Name,D);else Frames.CreateFrame(D);
            }
        }
        Frames.EnsureNativeMinimapControls();
        // Test the actual authored atlas rectangle and inherited copy, not a
        // hand-drawn replacement calendar image.
        const int64 Calendar=Frames.FindFrame(TEXT("GameTimeFrame"));
        auto* Button=Frames.GetButtonWidget(Calendar);
        if(!TestNotNull(TEXT("Calendar button exists"),Button))return false;
        const FBox2D Normal=Button->GetStyle().Normal.GetUVRegion();
        const FBox2D Pushed=Button->GetStyle().Pressed.GetUVRegion();
        TestTrue(TEXT("Calendar normal uses one square atlas cell"),Normal.Min.Equals(FVector2D(0,0)) && Normal.Max.Equals(FVector2D(.390625,.78125)));
        TestTrue(TEXT("Calendar pressed uses its own atlas cell"),Pushed.Min.Equals(FVector2D(.5,0)) && Pushed.Max.Equals(FVector2D(.890625,.78125)));
        TestNotNull(TEXT("Calendar image loaded"),Button->GetStyle().Normal.GetResourceObject());
        CalendarDefinition.Layers.Empty();CalendarDefinition.Name=TEXT("CalendarAtlasTemplate");CalendarDefinition.bVirtual=true;
        Frames.RegisterTemplate(CalendarDefinition.Name,CalendarDefinition);
        FWowFrameDef Clone;Clone.Type=EWowFrameType::Button;Clone.Name=TEXT("CalendarAtlasClone");Clone.Inherits=CalendarDefinition.Name;Clone.bHidden=true;
        const int64 CloneHandle=Frames.CreateFrame(Clone);
        if (auto* CloneButton=Frames.GetButtonWidget(CloneHandle))
            TestTrue(TEXT("Inherited button retains UV region"),FBox2D(CloneButton->GetStyle().Normal.GetUVRegion()).Max.Equals(Normal.Max));
        else AddError(TEXT("Inherited button was not created"));
        const auto* Day=Frames.GetPrimaryTextWidget(Calendar);
        TestTrue(TEXT("Calendar date is visible"),Day && !Day->GetText().IsEmpty());
        TestTrue(TEXT("Obsolete day/night image stays hidden"),Frames.GetNamedTexture(TEXT("GameTimeTexture"))->GetVisibility()==ESlateVisibility::Collapsed);
        auto AbsoluteCenter=[&](UWidget* Widget)
        {
            auto* Slot=Cast<UCanvasPanelSlot>(Widget->Slot);
            FVector2D Pos=Slot->GetSize()*.5;
            for(UWidget* W=Widget;W && W!=Frames.RootCanvas.Get();W=W->GetParent())
                if(auto* S=Cast<UCanvasPanelSlot>(W->Slot))Pos+=S->GetPosition();
            return Pos;
        };
        auto Click=[&](const TCHAR* Name)
        {
            const int64 Expected=Frames.FindFrame(Name);
            auto* W=Frames.GetWidgetForHandle(Expected);if(!TestNotNull(Name,W))return;
            const FVector2D Center=AbsoluteCenter(W);
            const int64 Hit=Frames.HitTestFrames(Center.X,Center.Y);
            TestEqual(FString::Printf(TEXT("%s center receives click at scale %.2f"),Name,Scale),Hit,Expected);
            Frames.DispatchClick(Hit,TEXT("LeftButton"),false);
        };
        int32 Zoom=0,Tracking=-1;
        Frames.OnMinimapZoomRequest.BindLambda([&](bool In){Zoom+=In?1:-1;});
        Frames.OnMinimapTrackingChanged.BindLambda([&](int32 Mode){Tracking=Mode;});
        Click(TEXT("MinimapZoomIn"));TestEqual(TEXT("Plus dispatches zoom"),Zoom,1);
        Click(TEXT("MinimapZoomOut"));TestEqual(TEXT("Minus dispatches zoom"),Zoom,0);
        Click(TEXT("MiniMapTrackingButton"));TestTrue(TEXT("Tracking menu opens"),Frames.IsMinimapTrackingMenuOpen());
        Click(TEXT("MinimapNativeTrackingRow10"));TestEqual(TEXT("Mailbox row selects tracking"),Tracking,10);
        TestFalse(TEXT("Selection closes tracking menu"),Frames.IsMinimapTrackingMenuOpen());
        TestTrue(TEXT("Create controlled window targets"),VM.ExecuteString(TEXT(R"LUA(
            WorldMapFrame=CreateFrame("Frame","WorldMapFrame",UIParent); WorldMapFrame:Hide()
            CalendarClicks=0
            function IsAddOnLoaded() return true end
            function Calendar_Toggle() CalendarClicks=CalendarClicks+1 end
        )LUA")));
        Click(TEXT("MiniMapWorldMapButton"));TestTrue(TEXT("World map button opens its window"),Frames.IsFrameVisible(Frames.FindFrame(TEXT("WorldMapFrame"))));
        Click(TEXT("MiniMapWorldMapButton"));TestFalse(TEXT("World map button closes its window"),Frames.IsFrameVisible(Frames.FindFrame(TEXT("WorldMapFrame"))));
        Click(TEXT("GameTimeFrame"));TestTrue(TEXT("Calendar dispatches once"),VM.ExecuteString(TEXT("assert(CalendarClicks==1)")));
        for(const auto& Pair:TArray<TPair<FString,float>>{{TEXT("MiniMapTrackingButtonBorder"),54},{TEXT("MiniMapWorldBorder"),52},{TEXT("MiniMapMailBorder"),52}})
        {
            UImage* Image=Frames.GetNamedTexture(Pair.Key);if(!TestNotNull(*Pair.Key,Image))continue;
            auto* Slot=Cast<UCanvasPanelSlot>(Image->Slot);
            TestTrue(TEXT("Padded ring keeps authored square dimensions"),Slot->GetSize().Equals(FVector2D(Pair.Value*Scale)));
            TestTrue(TEXT("Ring origin preserved"),Slot->GetPosition().IsNearlyZero());
            TestTrue(TEXT("Ring does not stretch between opposing anchors"),Slot->GetAnchors().Minimum==Slot->GetAnchors().Maximum);
            TestNotNull(TEXT("Ring art loaded"),Image->GetBrush().GetResourceObject());
        }
        UImage* TrackingIcon=Frames.GetNamedTexture(TEXT("MiniMapTrackingIcon"));
        const FVector2D IconCenter=AbsoluteCenter(TrackingIcon);
        const auto TrackingHandle=Frames.FindFrame(TEXT("MiniMapTrackingButton"));
        TestEqual(TEXT("Visible icon center belongs to tracking button"),Frames.HitTestFrames(IconCenter.X,IconCenter.Y),TrackingHandle);
    }
    return true;
}
#endif
