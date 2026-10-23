#include "SWowDeathScreen.h"
#include "WowWindowStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Styling/CoreStyle.h"
#include "Engine/Engine.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "WowDeathScreen"

void SWowDeathScreen::Construct(const FArguments& InArgs)
{
    OnReleaseSpirit = InArgs._OnReleaseSpirit;

    ChildSlot
    [
        // Full-screen semi-transparent overlay
        SNew(SBox)
        .WidthOverride(1920.0f)
        .HeightOverride(1080.0f)
        [
            SNew(SBorder)
            .BorderImage(new FSlateColorBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f))) // Dark semi-transparent
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            [
                SNew(SConstraintCanvas)
                +SConstraintCanvas::Slot()
                .Anchors(FAnchors(0.5f, 0.3f))
                .Alignment(FVector2D(0.5f, 0.5f))
                [
                    // "You are dead" text
                    SNew(STextBlock)
                    .Text(LOCTEXT("YouAreDead", "You are dead"))
                    .Font(FSlateFontInfo(FCoreStyle::GetDefaultFontStyle("Bold", 48)))
                    .ColorAndOpacity(FLinearColor(0.8f, 0.1f, 0.1f, 1.0f)) // Red text
                    .Justification(ETextJustify::Center)
                ]
                +SConstraintCanvas::Slot()
                .Anchors(FAnchors(0.5f, 0.4f))
                .Alignment(FVector2D(0.5f, 0.5f))
                [
                    // Instruction text
                    SNew(STextBlock)
                    .Text(LOCTEXT("ReleaseInstruction", "Release spirit to go to the nearest graveyard"))
                    .Font(FSlateFontInfo(FCoreStyle::GetDefaultFontStyle("Regular", 20)))
                    .ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f, 1.0f)) // Light gray text
                    .Justification(ETextJustify::Center)
                ]
                +SConstraintCanvas::Slot()
                .Anchors(FAnchors(0.5f, 0.5f))
                .Alignment(FVector2D(0.5f, 0.5f))
                .Offset(FMargin(0, 20, 0, 0))
                [
                    // Release Spirit button
                    SNew(SButton)
                    .ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center)
                    .OnClicked(this, &SWowDeathScreen::HandleReleaseSpiritClicked)
                    .ContentPadding(FMargin(30, 10))
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("ReleaseSpirit", "Release Spirit"))
                        .Font(FSlateFontInfo(FCoreStyle::GetDefaultFontStyle("Bold", 18)))
                        .ColorAndOpacity(FLinearColor(1.0f, 0.9f, 0.6f, 1.0f)) // Gold text
                        .Justification(ETextJustify::Center)
                    ]
                ]
                +SConstraintCanvas::Slot()
                .Anchors(FAnchors(0.5f, 0.6f))
                .Alignment(FVector2D(0.5f, 0.5f))
                .Offset(FMargin(0, 0, 0, 0))
                [
                    // Timer text (only visible when timer is active)
                    SNew(STextBlock)
                    .Text(this, &SWowDeathScreen::GetTimerText)
                    .Font(FSlateFontInfo(FCoreStyle::GetDefaultFontStyle("Regular", 16)))
                    .ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f, 1.0f))
                    .Justification(ETextJustify::Center)
                    .Visibility(this, &SWowDeathScreen::GetTimerVisibility)
                ]
            ]
        ]
    ];
}

void SWowDeathScreen::SetCorpseReclaimTimer(float TimeInSeconds)
{
    CorpseReclaimTime = TimeInSeconds;
    StartTime = FPlatformTime::Seconds();
    bTimerActive = true;
}

void SWowDeathScreen::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

    // Update timer state
    if (bTimerActive && CorpseReclaimTime > 0.0f)
    {
        float ElapsedTime = InCurrentTime - StartTime;
        if (ElapsedTime >= CorpseReclaimTime)
        {
            bTimerActive = false;
        }
    }
}

FReply SWowDeathScreen::HandleReleaseSpiritClicked()
{
    if (OnReleaseSpirit.IsBound())
    {
        OnReleaseSpirit.Execute();
    }
    return FReply::Handled();
}

FText SWowDeathScreen::GetTimerText() const
{
    if (!bTimerActive || CorpseReclaimTime <= 0.0f)
    {
        return FText::GetEmpty();
    }

    float ElapsedTime = FPlatformTime::Seconds() - StartTime;
    float RemainingTime = FMath::Max(0.0f, CorpseReclaimTime - ElapsedTime);

    if (RemainingTime > 0.0f)
    {
        int32 Minutes = static_cast<int32>(RemainingTime) / 60;
        int32 Seconds = static_cast<int32>(RemainingTime) % 60;

        return FText::Format(
            LOCTEXT("CorpseReclaimTimer", "You may reclaim your corpse in {0}:{1:02d}"),
            FText::AsNumber(Minutes),
            FText::AsNumber(Seconds)
        );
    }

    return LOCTEXT("CorpseReclaimReady", "You may now reclaim your corpse");
}

EVisibility SWowDeathScreen::GetTimerVisibility() const
{
    return (bTimerActive && CorpseReclaimTime > 0.0f) ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
