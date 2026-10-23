#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Widgets/SLeafWidget.h"
#include "WowBackdropWidget.generated.h"

class UTexture2D;

/**
 * Frame backdrop as the client draws it (XML <Backdrop>, frame:SetBackdrop): bgFile inside the
 * insets (stretched, or tiled at tileSize), edgeFile as the 8-piece border atlas
 * (L, R, T, B, TL, TR, BL, BR; T/B stored rotated), with SetBackdropColor/BorderColor tints.
 */
class WOWUI_API SWowBackdrop : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SWowBackdrop) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) {}

	/** Sizes are in Slate units (WoW units already multiplied by the UI scale). */
	void SetBackdrop(UTexture2D* InBackground, UTexture2D* InEdge, float InEdgeSize, bool bInTile,
		float InTileSize, const FMargin& InInsets);
	void SetBackgroundColor(const FLinearColor& Color) { BackgroundColor = Color; Invalidate(EInvalidateWidgetReason::Paint); }
	void SetBorderColor(const FLinearColor& Color) { BorderColor = Color; Invalidate(EInvalidateWidgetReason::Paint); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
	FSlateBrush Background;
	FSlateBrush Edges[8];
	bool bHasBackground = false;
	bool bHasEdges = false;
	float EdgeSize = 0.f;
	FMargin Insets;
	FLinearColor BackgroundColor = FLinearColor::White;
	FLinearColor BorderColor = FLinearColor::White;
};

/** UMG wrapper so FrameXML frame canvases can hold a backdrop; keeps the textures referenced. */
UCLASS()
class WOWUI_API UWowBackdropWidget : public UWidget
{
	GENERATED_BODY()

public:
	void SetBackdrop(UTexture2D* InBackground, UTexture2D* InEdge, float InEdgeSize, bool bInTile,
		float InTileSize, const FMargin& InInsets);
	void SetBackgroundColor(const FLinearColor& Color);
	void SetBorderColor(const FLinearColor& Color);

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BackgroundTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> EdgeTexture;

	float EdgeSize = 0.f;
	float TileSize = 0.f;
	bool bTile = false;
	FMargin Insets;
	FLinearColor BackgroundColor = FLinearColor::White;
	FLinearColor BorderColor = FLinearColor::White;

	TSharedPtr<SWowBackdrop> SlateBackdrop;
};
