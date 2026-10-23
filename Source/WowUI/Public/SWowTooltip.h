#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

struct FWowEntity;

/**
 * Tooltip widget that displays entity information
 */
class WOWUI_API SWowTooltip : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowTooltip)
    {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /** Set the entity to display in the tooltip */
    void SetEntity(const FWowEntity* Entity);

    /** Clear the tooltip content */
    void Clear();

private:
	TSharedPtr<FSlateBrush> TooltipBackgroundBrush;
	TSharedPtr<FSlateBrush> HealthBackgroundBrush;

    /** Get the entity name with color coding based on hostility */
    FText GetEntityNameText() const;
    FSlateColor GetEntityNameColor() const;

    /** Get level text */
    FText GetLevelText() const;

    /** Get entity type text (Player/NPC/Creature) */
    FText GetEntityTypeText() const;

    /** Get health text */
    FText GetHealthText() const;

    /** Get health bar percentage */
    TOptional<float> GetHealthPercent() const;

    /** Visibility functions */
    EVisibility GetHealthBarVisibility() const;

    /** Current entity being displayed */
    const FWowEntity* CurrentEntity = nullptr;
};
