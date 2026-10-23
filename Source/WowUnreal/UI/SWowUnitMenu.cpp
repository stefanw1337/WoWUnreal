#include "SWowUnitMenu.h"
#include "WowColor.h"
#include "WowWindowStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"

void SWowUnitMenu::Construct(const FArguments&)
{
    static const FSlateRoundedBoxBrush Box(FLinearColor(0.f, 0.f, 0.f, .92f), 4.f, WowColor::FromWow(.55f, .55f, .55f, 1.f), 1.f);
    MenuBox = SNew(SBorder).BorderImage(&Box).Padding(FMargin(10, 8, 14, 8))
        [ SAssignNew(Lines, SVerticalBox) ];
    SubBox = SNew(SBorder).BorderImage(&Box).Padding(FMargin(10, 8, 14, 8))
        .Visibility_Lambda([this]() { return bSubOpen ? EVisibility::Visible : EVisibility::Collapsed; })
        [ SAssignNew(SubLines, SVerticalBox) ];

    ChildSlot
    [
        SNew(SOverlay)
        // A click anywhere outside the menu closes it (and is not passed on).
        + SOverlay::Slot()
        [
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
            .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { Close(); return FReply::Handled(); })
        ]
        + SOverlay::Slot()
        [
            SNew(SCanvas)
            + SCanvas::Slot()
            .Position_Lambda([this]() { return MenuPosition(); })
            .Size_Lambda([this]() { return MenuBox.IsValid() ? MenuBox->GetDesiredSize() : FVector2D::ZeroVector; })
            [ MenuBox.ToSharedRef() ]
            + SCanvas::Slot()
            .Position_Lambda([this]() { return SubmenuPosition(); })
            .Size_Lambda([this]() { return SubBox.IsValid() && bSubOpen ? SubBox->GetDesiredSize() : FVector2D::ZeroVector; })
            [ SubBox.ToSharedRef() ]
        ]
    ];
    SetVisibility(EVisibility::Collapsed);
}

FVector2D SWowUnitMenu::MenuPosition() const
{
    const FGeometry& Geometry = GetCachedGeometry();
    FVector2D Local = Geometry.AbsoluteToLocal(AbsoluteAnchor);
    // Keep the whole menu on screen.
    const FVector2D Size = MenuBox.IsValid() ? MenuBox->GetDesiredSize() : FVector2D::ZeroVector;
    const FVector2D Area = Geometry.GetLocalSize();
    Local.X = FMath::Clamp(Local.X, 0.0, FMath::Max(0.0, Area.X - Size.X));
    Local.Y = FMath::Clamp(Local.Y, 0.0, FMath::Max(0.0, Area.Y - Size.Y));
    return Local;
}

FVector2D SWowUnitMenu::SubmenuPosition() const
{
    const FGeometry& Geometry = GetCachedGeometry();
    FVector2D Local = Geometry.AbsoluteToLocal(SubAnchor);
    const FVector2D Size = SubBox.IsValid() ? SubBox->GetDesiredSize() : FVector2D::ZeroVector;
    const FVector2D Area = Geometry.GetLocalSize();
    // No room on the right: open on the left of the main menu instead.
    if (Local.X + Size.X > Area.X)
    {
        const FVector2D Main = MenuPosition();
        Local.X = FMath::Max(0.0, Main.X - Size.X);
    }
    Local.Y = FMath::Clamp(Local.Y, 0.0, FMath::Max(0.0, Area.Y - Size.Y));
    return Local;
}

void SWowUnitMenu::FillLines(SVerticalBox& Box, const TArray<FWowUnitMenuEntry>& Entries, bool bSubmenu)
{
    Box.ClearChildren();
    const FSlateFontInfo Font = FWowWindowStyle::TitleFont(FMath::Max(7, FMath::RoundToInt(12.f * Scale * .75f)));
    for (const FWowUnitMenuEntry& Entry : Entries)
    {
        const bool bParent = Entry.Children.IsValid() && Entry.Children->Num() > 0;
        if (Entry.bTitle || (!Entry.Action && !bParent) || !Entry.bEnabled)
        {
            Box.AddSlot().AutoHeight().Padding(FMargin(0, 1, 0, 1))
            [
                SNew(STextBlock).Text(Entry.Label).Font(Font)
                .ColorAndOpacity(Entry.bTitle ? WowColor::FromWow(1.f, .82f, 0.f) : WowColor::FromWow(.5f, .5f, .5f))
                .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
            ];
            continue;
        }
        TFunction<void()> Action = Entry.Action;
        TSharedPtr<TArray<FWowUnitMenuEntry>> Children = Entry.Children;
        TSharedPtr<SButton> Button;
        // Acts on the press: a release-click lost the press to the game's mouse handling and the
        // click went to the world behind the menu (Stefan 10.10: Inspect/Whisper detargeted).
        Box.AddSlot().AutoHeight()
        [
            SAssignNew(Button, SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0, 1, 0, 1))
            .ClickMethod(EButtonClickMethod::MouseDown)
        ];
        TWeakPtr<SButton> WeakButton = Button;
        if (bParent)
        {
            auto OpenHere = [this, WeakButton, Children]()
            {
                const TSharedPtr<SButton> B = WeakButton.Pin();
                if (!B.IsValid()) return;
                const FGeometry& G = B->GetCachedGeometry();
                // Line up with the row, just right of the main box's border.
                const FVector2D TopRight = G.LocalToAbsolute(FVector2D(G.GetLocalSize().X + 18.f, -8.f));
                OpenSubmenu(Children, TopRight);
            };
            Button->SetOnHovered(FSimpleDelegate::CreateLambda(OpenHere));
            Button->SetOnClicked(FOnClicked::CreateLambda([OpenHere]() { OpenHere(); return FReply::Handled(); }));
        }
        else
        {
            Button->SetOnClicked(FOnClicked::CreateLambda([this, Action]() { Close(); Action(); return FReply::Handled(); }));
            if (!bSubmenu)
                Button->SetOnHovered(FSimpleDelegate::CreateLambda([this]() { bSubOpen = false; OpenChildren.Reset(); }));
        }
        static const FSlateRoundedBoxBrush Dot(WowColor::FromWow(1.f, .82f, 0.f), 4.f);
        TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
        if (Entry.bCheckable)
            Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(4, 0, 6, 0))
            [
                SNew(SBox).WidthOverride(8).HeightOverride(8)
                [ SNew(SBorder).BorderImage(&Dot).Padding(0).Visibility(Entry.bChecked ? EVisibility::HitTestInvisible : EVisibility::Hidden) ]
            ];
        Row->AddSlot().FillWidth(1.f)
        [
            SNew(STextBlock).Text(Entry.Label).Font(Font)
            .ColorAndOpacity_Lambda([WeakButton]()
            {
                const TSharedPtr<SButton> B = WeakButton.Pin();
                return FSlateColor(B.IsValid() && B->IsHovered() ? WowColor::FromWow(1.f, .82f, 0.f) : FLinearColor::White);
            })
            .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
        ];
        if (bParent)
            Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(12, 0, 0, 0))
            [ SNew(STextBlock).Text(FText::FromString(TEXT(">"))).Font(Font).ColorAndOpacity(WowColor::FromWow(1.f, .82f, 0.f)) ];
        Button->SetContent(Row);
    }
}

void SWowUnitMenu::OpenSubmenu(const TSharedPtr<TArray<FWowUnitMenuEntry>>& Children, const FVector2D& AbsoluteTopLeft)
{
    if (!Children.IsValid()) return;
    SubAnchor = AbsoluteTopLeft;
    if (OpenChildren == Children && bSubOpen) return;
    OpenChildren = Children;
    FillLines(*SubLines, *Children, true);
    bSubOpen = true;
}

void SWowUnitMenu::Open(const TArray<FWowUnitMenuEntry>& Entries, const FVector2D& AbsolutePosition, float InScale)
{
    Scale = FMath::Max(.5f, InScale);
    AbsoluteAnchor = AbsolutePosition;
    bSubOpen = false;
    OpenChildren.Reset();
    FillLines(*Lines, Entries, false);
    bOpen = true;
    SetVisibility(EVisibility::Visible);
}

void SWowUnitMenu::Close()
{
    if (!bOpen) return;
    bOpen = false;
    bSubOpen = false;
    OpenChildren.Reset();
    SetVisibility(EVisibility::Collapsed);
}
