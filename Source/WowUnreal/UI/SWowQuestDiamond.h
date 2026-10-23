#pragma once
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/StrongObjectPtr.h"
#include "Styling/SlateBrush.h"

class UTexture2D;

class APlayerController;

/**
 * Super-tracked quest markers in the world (Ctrl-click quests in the objectives tracker):
 * a gold diamond over each quest's current objective area with its title above and the
 * distance under it. Off screen a marker sits on the screen edge, pointing the way.
 */
class WOWUNREAL_API SWowQuestDiamond : public SLeafWidget
{
public:
    struct FTarget { FVector World = FVector::ZeroVector; float Yards = 0.f; FString Label; bool bCorpse = false; };
    /** All markers to draw this frame (world position in UE units, distance in yards, quest title). */
    using FTargetFunction = TFunction<void(TArray<FTarget>& /*OutTargets*/)>;

    SLATE_BEGIN_ARGS(SWowQuestDiamond) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, APlayerController* InController, FTargetFunction InTarget);
    /** r255: the skull drawn for the corpse marker (bCorpse) instead of the gem. */
    void SetCorpseIcon(UTexture2D* Icon);

    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
    TWeakObjectPtr<APlayerController> Controller;
    FTargetFunction Target;
    TStrongObjectPtr<UTexture2D> CorpseIcon;
    FSlateBrush CorpseBrush;
};

/** One quest marker on its own VR world card: the gem, the quest title above, yards under it. */
class WOWUNREAL_API SWowQuestDiamondCard : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWowQuestDiamondCard) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&) { SetVisibility(EVisibility::HitTestInvisible); }
    void SetMarker(const FString& InLabel, float InYards) { Label = InLabel; Yards = InYards; }
    /** r255: draw this skull instead of the gem (the corpse marker). */
    void SetCorpseIcon(UTexture2D* Icon);

    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(440.f, 220.f); }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
    FString Label;
    float Yards = 0.f;
    TStrongObjectPtr<UTexture2D> CorpseIcon;
    FSlateBrush CorpseBrush;
};
