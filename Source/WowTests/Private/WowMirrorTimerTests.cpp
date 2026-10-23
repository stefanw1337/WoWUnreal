#include "Misc/AutomationTest.h"
#include "WowFrameManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirrorTimerStateContract,
	"WowUnreal.UI.MirrorTimer.StateContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMirrorTimerStateContract::RunTest(const FString& Parameters)
{
	FWowFrameManager FrameManager;
	FrameManager.StartMirrorTimer(1, 180000, 180000, -1, false);
	FrameManager.TickMirrorTimers(1.25f);

	FWowMirrorTimerState Breath;
	TestTrue(TEXT("Breath timer is addressable by its numeric id"),
		FrameManager.GetMirrorTimerState(1, Breath));
	TestEqual(TEXT("Breath timer exposes the FrameXML timer name"),
		Breath.Name, FString(TEXT("BREATH")));
	TestEqual(TEXT("Breath timer depletes in milliseconds"),
		Breath.ValueMs, 178750.0f);
	TestEqual(TEXT("GetMirrorTimerProgress uses the milliseconds expected by MirrorTimer.lua"),
		FrameManager.GetMirrorTimerProgress(TEXT("breath")), 178750.0f);

	FrameManager.StartMirrorTimer(1, 170000, 180000, 10, false);
	FrameManager.TickMirrorTimers(0.5f);
	TestTrue(TEXT("Refilling breath remains active"),
		FrameManager.GetMirrorTimerState(1, Breath));
	TestEqual(TEXT("Breath refill scale is applied in milliseconds"),
		Breath.ValueMs, 175000.0f);

	FrameManager.StopMirrorTimer(1);
	TestFalse(TEXT("Stopped breath timer is no longer active"),
		FrameManager.GetMirrorTimerState(1, Breath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirrorTimerPresentationContract,
	"WowUnreal.UI.MirrorTimer.PresentationContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMirrorTimerPresentationContract::RunTest(const FString& Parameters)
{
	UCanvasPanel* RootCanvas = NewObject<UCanvasPanel>();
	FWowFrameManager FrameManager;
	FrameManager.Initialize(RootCanvas);

	FWowFrameDef OuterDef;
	OuterDef.Name = TEXT("MirrorTimer1");
	OuterDef.Width = 206.0f;
	OuterDef.Height = 26.0f;
	OuterDef.bHidden = true;
	const int64 OuterHandle = FrameManager.CreateFrame(OuterDef);

	FWowFrameDef StatusDef;
	StatusDef.Type = EWowFrameType::StatusBar;
	StatusDef.Name = TEXT("MirrorTimer1StatusBar");
	StatusDef.Parent = TEXT("MirrorTimer1");
	StatusDef.Width = 195.0f;
	StatusDef.Height = 13.0f;
	const int64 StatusHandle = FrameManager.CreateFrame(StatusDef);

	FrameManager.StartMirrorTimer(1, 90000, 180000, -1, false);

	TestTrue(TEXT("Active Breath timer makes the mirror frame visible"),
		FrameManager.IsFrameVisible(OuterHandle));
	UProgressBar* Bar = FrameManager.GetStatusBarWidget(StatusHandle);
	TestNotNull(TEXT("Mirror StatusBar owns a progress fill"), Bar);
	if (Bar)
	{
		TestEqual(TEXT("Native Breath fill reflects live progress"), Bar->GetPercent(), 0.5f);
	}

	UWidget* StatusWidget = FrameManager.GetWidgetForHandle(StatusHandle);
	UCanvasPanelSlot* StatusSlot = StatusWidget
		? Cast<UCanvasPanelSlot>(StatusWidget->Slot)
		: nullptr;
	TestNotNull(TEXT("Mirror StatusBar has a canvas slot"), StatusSlot);
	if (StatusSlot)
	{
		TestTrue(TEXT("Breath fill is drawn above the opaque artwork trough"),
			StatusSlot->GetZOrder() > 200);
		TestEqual(TEXT("Breath fill is vertically aligned inside the stock border"),
			StatusSlot->GetPosition(), FVector2D(5.5f, 2.5f));
	}

	UTextBlock* Label = FrameManager.GetNamedFontString(TEXT("MirrorTimer1NativeText"));
	TestNotNull(TEXT("Mirror timer receives exactly one native fallback label"), Label);
	if (Label)
	{
		TestEqual(TEXT("Mirror timer label contains Breath"),
			Label->GetText().ToString(), FString(TEXT("Breath")));
		TestEqual(TEXT("Short mirror-bar text receives its optical vertical correction"),
			Label->GetRenderTransform().Translation, FVector2D(0.0f, -3.0f));
		if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(Label->Slot))
		{
			TestEqual(TEXT("Label covers the same inset rectangle as the fill"),
				LabelSlot->GetSize(), FVector2D(195.0f, 13.0f));
		}
	}
	return true;
}
