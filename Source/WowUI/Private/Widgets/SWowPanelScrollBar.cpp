#include "Widgets/SWowPanelScrollBar.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowWindowStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

namespace
{
// The knob column between the arrows: paints UI-ScrollBar-Knob (18 x 24) at the scroll position,
// click jumps there and drags, the wheel scrolls.
class SWowPanelScrollTrack : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SWowPanelScrollTrack) {} SLATE_END_ARGS()
	TWeakPtr<SWowPanelScrollBar> Owner;
	void Construct(const FArguments&) {}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(16.f, 32.f); }

	float KnobHeight(const FGeometry& Geometry) const
	{
		const TSharedPtr<SWowPanelScrollBar> Bar = Owner.Pin();
		return FMath::Min(Geometry.GetLocalSize().Y, 24.f * (Bar.IsValid() ? Bar->GetScale() : 1.f));
	}

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle&, bool) const override
	{
		const TSharedPtr<SWowPanelScrollBar> Bar = Owner.Pin();
		if (!Bar.IsValid() || !Bar->HasRange()) return Layer; // FauxScrollFrame hides the thumb without a range
		const FSlateBrush* Knob = Bar->Brush(TEXT("Interface\\Buttons\\UI-ScrollBar-Knob"), .2f, .125f, .8f, .875f);
		if (!Knob) return Layer;
		const FVector2D Size = Geometry.GetLocalSize();
		const float Height = KnobHeight(Geometry);
		const float Width = Size.X * 18.f / 16.f;
		const float Y = (Size.Y - Height) * FMath::Clamp(Bar->GetFraction(), 0.f, 1.f);
		FSlateDrawElement::MakeBox(Out, Layer + 1, Geometry.ToPaintGeometry(FVector2f(Width, Height),
			FSlateLayoutTransform(FVector2f((Size.X - Width) * .5f, Y))), Knob);
		return Layer + 1;
	}

	void DragTo(const FGeometry& Geometry, const FVector2D& Screen)
	{
		const TSharedPtr<SWowPanelScrollBar> Bar = Owner.Pin();
		if (!Bar.IsValid()) return;
		const float Height = KnobHeight(Geometry);
		const float Span = FMath::Max(1.f, float(Geometry.GetLocalSize().Y) - Height);
		const float Local = float(Geometry.AbsoluteToLocal(Screen).Y) - GrabOffset;
		Bar->SetFraction(FMath::Clamp(Local / Span, 0.f, 1.f));
	}

	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		const TSharedPtr<SWowPanelScrollBar> Bar = Owner.Pin();
		if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !Bar.IsValid() || !Bar->HasRange()) return FReply::Handled();
		const float Height = KnobHeight(Geometry);
		const float KnobTop = (float(Geometry.GetLocalSize().Y) - Height) * Bar->GetFraction();
		const float Local = float(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()).Y);
		// On the knob: keep the grab point. Elsewhere: centre the knob on the click.
		GrabOffset = Local >= KnobTop && Local <= KnobTop + Height ? Local - KnobTop : Height * .5f;
		bDragging = true;
		DragTo(Geometry, Event.GetScreenSpacePosition());
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		if (bDragging && HasMouseCapture()) { DragTo(Geometry, Event.GetScreenSpacePosition()); return FReply::Handled(); }
		return FReply::Unhandled();
	}
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
	{
		bDragging = false;
		return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Handled();
	}
	virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent& Event) override
	{
		if (const TSharedPtr<SWowPanelScrollBar> Bar = Owner.Pin()) Bar->Step(-Event.GetWheelDelta());
		return FReply::Handled();
	}

private:
	bool bDragging = false;
	float GrabOffset = 0.f;
};
}

SWowPanelScrollBar::~SWowPanelScrollBar() = default;

void SWowPanelScrollBar::Construct(const FArguments& InArgs)
{
	Scale = InArgs._Scale;
	UI = InArgs._UIManager;
	TSharedRef<SWowPanelScrollTrack> Track = SNew(SWowPanelScrollTrack);
	Track->Owner = SharedThis(this);
	ChildSlot
	[
		SNew(SBox).WidthOverride_Lambda([this]() { return ColumnWidth * GetScale(); })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ ArrowButton(true) ]
			+ SVerticalBox::Slot().FillHeight(1.f)[ Track ]
			+ SVerticalBox::Slot().AutoHeight()[ ArrowButton(false) ]
		]
	];
}

TSharedRef<SWidget> SWowPanelScrollBar::ArrowButton(bool bUp)
{
	// UIPanelScrollUpButtonTemplate / ...Down: 18 x 16 of the 32 x 32 art (TexCoords .20-.80, .25-.75);
	// Up, Down (pushed), Disabled at the end of the range.
	const FString Base = bUp ? TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-") : TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-");
	const float Direction = bUp ? -1.f : 1.f;
	TSharedPtr<SButton> Button;
	SAssignNew(Button, SButton)
		.ButtonStyle(FCoreStyle::Get(), "NoBorder")
		.ContentPadding(0)
		.ClickMethod(EButtonClickMethod::MouseDown)
		.IsEnabled_Lambda([this, Direction]() { return CanScroll(Direction); })
		.OnClicked_Lambda([this, Direction]() { Step(Direction); return FReply::Handled(); });
	TWeakPtr<SButton> WeakButton = Button;
	Button->SetContent(SNew(SImage).Image_Lambda([this, Base, Direction, WeakButton]()
	{
		const TSharedPtr<SButton> Pinned = WeakButton.Pin();
		const TCHAR* State = !CanScroll(Direction) ? TEXT("Disabled") : Pinned.IsValid() && Pinned->IsPressed() ? TEXT("Down") : TEXT("Up");
		return Brush(Base + State, .2f, .25f, .8f, .75f);
	}));
	return SNew(SBox).HeightOverride_Lambda([this]() { return 16.f * GetScale(); })[ Button.ToSharedRef() ];
}

const FSlateBrush* SWowPanelScrollBar::Brush(const FString& Path, float U0, float V0, float U1, float V1) const
{
	const FString Key = FString::Printf(TEXT("%s|%.3f,%.3f,%.3f,%.3f"), *Path, U0, V0, U1, V1);
	if (const TSharedPtr<FSlateBrush>* Found = Brushes.Find(Key)) return Found->Get();
	// Windows built without the UI manager find it through the game instance.
	UWowUIManager* Manager = UI.Get();
	if (!Manager && GEngine && GEngine->GameViewport)
		if (UWorld* World = GEngine->GameViewport->GetWorld())
			if (UGameInstance* Instance = World->GetGameInstance())
				Manager = Instance->GetSubsystem<UWowUIManager>();
	UTexture2D* Texture = Manager && Manager->GetFrameManager() ? Manager->GetFrameManager()->LoadUITexture(Path) : nullptr;
	// The login screen's options window has no frame manager: the window style preloads the art.
	if (!Texture) Texture = FWowWindowStyle::ControlTexture(Path);
	if (!Texture) return nullptr; // retried on the next paint
	Textures.Emplace(Texture);
	TSharedPtr<FSlateBrush> NewBrush = MakeShared<FSlateBrush>();
	NewBrush->SetResourceObject(Texture);
	NewBrush->DrawAs = ESlateBrushDrawType::Image;
	NewBrush->ImageSize = FVector2D(Texture->GetSizeX() * (U1 - U0), Texture->GetSizeY() * (V1 - V0));
	NewBrush->SetUVRegion(FBox2f(FVector2f(U0, V0), FVector2f(U1, V1)));
	Brushes.Add(Key, NewBrush);
	return NewBrush.Get();
}

bool SWowPanelScrollBar::HasRange() const
{
	const TSharedPtr<SScrollBox> Pinned = Box.Pin();
	return Pinned.IsValid() && Pinned->GetScrollOffsetOfEnd() > 0.5f;
}

float SWowPanelScrollBar::GetFraction() const
{
	const TSharedPtr<SScrollBox> Pinned = Box.Pin();
	const float End = Pinned.IsValid() ? Pinned->GetScrollOffsetOfEnd() : 0.f;
	return End > 0.5f ? Pinned->GetScrollOffset() / End : 0.f;
}

void SWowPanelScrollBar::SetFraction(float Fraction)
{
	if (const TSharedPtr<SScrollBox> Pinned = Box.Pin())
		Pinned->SetScrollOffset(FMath::Clamp(Fraction, 0.f, 1.f) * Pinned->GetScrollOffsetOfEnd());
}

bool SWowPanelScrollBar::CanScroll(float Direction) const
{
	const TSharedPtr<SScrollBox> Pinned = Box.Pin();
	if (!Pinned.IsValid()) return false;
	return Direction < 0.f ? Pinned->GetScrollOffset() > 0.5f : Pinned->GetScrollOffsetOfEnd() - Pinned->GetScrollOffset() > 0.5f;
}

void SWowPanelScrollBar::Step(float Direction)
{
	// The arrows step by half the visible height (ScrollFrameTemplate_OnMouseWheel uses half too).
	const TSharedPtr<SScrollBox> Pinned = Box.Pin();
	if (!Pinned.IsValid()) return;
	const float StepSize = FMath::Max(16.f * GetScale(), float(Pinned->GetCachedGeometry().GetLocalSize().Y) * .5f);
	Pinned->SetScrollOffset(FMath::Clamp(Pinned->GetScrollOffset() + Direction * StepSize, 0.f, Pinned->GetScrollOffsetOfEnd()));
}

TSharedRef<SWidget> SWowPanelScrollBar::Wrap(const TSharedRef<SScrollBox>& InBox, TAttribute<float> InScale, UWowUIManager* InUIManager)
{
	// Stefan 10.10: lists showed Slate's thin grey bar inside the list instead of the client's bar
	// with arrows at the right.
	InBox->SetScrollBarVisibility(EVisibility::Collapsed);
	TSharedRef<SWowPanelScrollBar> Bar = SNew(SWowPanelScrollBar).Scale(InScale).UIManager(InUIManager);
	Bar->SetScrollBox(InBox);
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)[ InBox ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(4.f, 0.f, 0.f, 0.f))[ Bar ];
}
