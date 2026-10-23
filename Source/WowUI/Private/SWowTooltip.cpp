#include "SWowTooltip.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Misc/Paths.h"
#include "WowEntity.h"

#define LOCTEXT_NAMESPACE "WowTooltip"

void SWowTooltip::Construct(const FArguments& InArgs)
{
    TooltipBackgroundBrush = MakeShared<FSlateRoundedBoxBrush>(
        FLinearColor(0.008f, 0.009f, 0.013f, 0.94f), 2.0f,
        FLinearColor(0.48f, 0.50f, 0.55f, 0.98f), 1.0f);
    HealthBackgroundBrush = MakeShared<FSlateColorBrush>(FLinearColor(0.2f, 0.1f, 0.1f, 0.8f));

    ChildSlot
    [
        SNew(SBox)
        .MaxDesiredWidth(300.0f)
        [
            SNew(SBorder)
            .BorderImage(TooltipBackgroundBrush.Get())
            .Padding(FMargin(8, 6))
            [
                SNew(SVerticalBox)
                // Entity name
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0, 0, 0, 2))
                [
                    SNew(STextBlock)
                    .Text(this, &SWowTooltip::GetEntityNameText)
                    .ColorAndOpacity(this, &SWowTooltip::GetEntityNameColor)
                    .Font(FSlateFontInfo(FCoreStyle::GetDefaultFontStyle("Bold", 14)))
                    .Justification(ETextJustify::Center)
                ]
                // Level and type info
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0, 0, 0, 4))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(STextBlock)
                        .Text(this, &SWowTooltip::GetLevelText)
                        .ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f, 1.0f))
                        .Font(FSlateFontInfo(FCoreStyle::GetDefaultFontStyle("Regular", 11)))
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(FMargin(4, 0, 0, 0))
                    [
                        SNew(STextBlock)
                        .Text(this, &SWowTooltip::GetEntityTypeText)
                        .ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f, 1.0f))
                        .Font(FSlateFontInfo(FCoreStyle::GetDefaultFontStyle("Regular", 11)))
                    ]
                ]
                // Health bar
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0, 2, 0, 0))
                [
                    SNew(SVerticalBox)
                    .Visibility(this, &SWowTooltip::GetHealthBarVisibility)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(SProgressBar)
                        .Percent(this, &SWowTooltip::GetHealthPercent)
                        .FillColorAndOpacity(FLinearColor(0.2f, 0.8f, 0.2f, 1.0f)) // Green health bar
                        .BackgroundImage(HealthBackgroundBrush.Get())
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(FMargin(0, 2, 0, 0))
                    [
                        SNew(STextBlock)
                        .Text(this, &SWowTooltip::GetHealthText)
                        .ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f, 1.0f))
                        .Font(FSlateFontInfo(FCoreStyle::GetDefaultFontStyle("Regular", 10)))
                        .Justification(ETextJustify::Center)
                    ]
                ]
            ]
        ]
    ];
}

void SWowTooltip::SetEntity(const FWowEntity* Entity)
{
    CurrentEntity = Entity;
}

void SWowTooltip::Clear()
{
    CurrentEntity = nullptr;
}

FText SWowTooltip::GetEntityNameText() const
{
    if (!CurrentEntity)
    {
        return FText::GetEmpty();
    }

    // For now, just show GUID as name since we don't have name resolution yet
    // In a real implementation, this would lookup the entity name from DBC or server data
    return FText::Format(LOCTEXT("EntityName", "Entity {0}"), FText::AsNumber(CurrentEntity->Guid));
}

FSlateColor SWowTooltip::GetEntityNameColor() const
{
    if (!CurrentEntity)
    {
        return FSlateColor(FLinearColor::White);
    }

    // Color code based on entity type and hostility
    if (CurrentEntity->IsPlayer())
    {
        return FSlateColor(FLinearColor(0.3f, 0.7f, 1.0f, 1.0f)); // Blue for players
    }
    else if (CurrentEntity->IsUnit())
    {
        // For NPCs, we would check faction/hostility here
        // For now, default to neutral yellow
        return FSlateColor(FLinearColor(1.0f, 1.0f, 0.3f, 1.0f)); // Yellow for neutral
    }

    return FSlateColor(FLinearColor::White);
}

FText SWowTooltip::GetLevelText() const
{
    if (!CurrentEntity)
    {
        return FText::GetEmpty();
    }

    int32 Level = CurrentEntity->GetLevel();
    if (Level > 0)
    {
        return FText::Format(LOCTEXT("Level", "Level {0}"), FText::AsNumber(Level));
    }

    return LOCTEXT("LevelUnknown", "Level ?");
}

FText SWowTooltip::GetEntityTypeText() const
{
    if (!CurrentEntity)
    {
        return FText::GetEmpty();
    }

    if (CurrentEntity->IsPlayer())
    {
        return LOCTEXT("Player", "Player");
    }
    else if (CurrentEntity->IsUnit())
    {
        return LOCTEXT("Creature", "Creature");
    }
    else if (CurrentEntity->IsGameObject())
    {
        return LOCTEXT("GameObject", "GameObject");
    }

    return LOCTEXT("Unknown", "Unknown");
}

FText SWowTooltip::GetHealthText() const
{
    if (!CurrentEntity || !CurrentEntity->IsUnit())
    {
        return FText::GetEmpty();
    }

    int32 Health = CurrentEntity->GetHealth();
    int32 MaxHealth = CurrentEntity->GetMaxHealth();

    if (MaxHealth > 0)
    {
        return FText::Format(LOCTEXT("Health", "{0} / {1}"),
            FText::AsNumber(Health),
            FText::AsNumber(MaxHealth)
        );
    }

    return FText::GetEmpty();
}

TOptional<float> SWowTooltip::GetHealthPercent() const
{
    if (!CurrentEntity || !CurrentEntity->IsUnit())
    {
        return TOptional<float>();
    }

    int32 Health = CurrentEntity->GetHealth();
    int32 MaxHealth = CurrentEntity->GetMaxHealth();

    if (MaxHealth > 0)
    {
        return static_cast<float>(Health) / static_cast<float>(MaxHealth);
    }

    return TOptional<float>();
}

EVisibility SWowTooltip::GetHealthBarVisibility() const
{
    if (!CurrentEntity || !CurrentEntity->IsUnit())
    {
        return EVisibility::Collapsed;
    }

    return (CurrentEntity->GetMaxHealth() > 0) ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
