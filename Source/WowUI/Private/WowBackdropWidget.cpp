#include "WowBackdropWidget.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"

void SWowBackdrop::SetBackdrop(UTexture2D* InBackground, UTexture2D* InEdge, float InEdgeSize, bool bInTile,
	float InTileSize, const FMargin& InInsets)
{
	Background = FSlateBrush();
	bHasBackground = InBackground != nullptr;
	if (InBackground)
	{
		Background.SetResourceObject(InBackground);
		Background.DrawAs = ESlateBrushDrawType::Image;
		if (bInTile)
		{
			Background.Tiling = ESlateBrushTileType::Both;
			const float Tile = InTileSize > 0.f ? InTileSize : float(FMath::Max(1, InBackground->GetSizeX()));
			Background.ImageSize = FVector2D(Tile, Tile);
		}
		else
		{
			Background.ImageSize = FVector2D(InBackground->GetSizeX(), InBackground->GetSizeY());
		}
	}

	bHasEdges = InEdge != nullptr && InEdgeSize > 0.f;
	EdgeSize = InEdgeSize;
	// Proper border files are an 8:1 strip; anything else (WHITE8X8, a plain texture) is
	// used whole for every piece, which gives a solid line like the client does.
	const bool bAtlas = InEdge && InEdge->GetSizeX() == InEdge->GetSizeY() * 8;
	for (int32 I = 0; I < 8; ++I)
	{
		Edges[I] = FSlateBrush();
		if (!InEdge) continue;
		Edges[I].SetResourceObject(InEdge);
		Edges[I].DrawAs = ESlateBrushDrawType::Image;
		Edges[I].ImageSize = FVector2D(InEdgeSize, InEdgeSize);
		if (bAtlas)
			Edges[I].SetUVRegion(FBox2f(FVector2f(I / 8.f, 0.f), FVector2f((I + 1) / 8.f, 1.f)));
	}
	Insets = InInsets;
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SWowBackdrop::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& CullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2D Size = G.GetLocalSize();
	if (Size.X <= 0.f || Size.Y <= 0.f) return LayerId;

	if (bHasBackground)
	{
		const FVector2D Inner(Size.X - Insets.Left - Insets.Right, Size.Y - Insets.Top - Insets.Bottom);
		if (Inner.X > 0.f && Inner.Y > 0.f)
		{
			FSlateDrawElement::MakeBox(Out, LayerId,
				G.ToPaintGeometry(FVector2f(Inner), FSlateLayoutTransform(FVector2f(Insets.Left, Insets.Top))),
				&Background, ESlateDrawEffect::None,
				Style.GetColorAndOpacityTint() * BackgroundColor);
		}
	}

	if (!bHasEdges) return LayerId;
	const float E = FMath::Min(EdgeSize, float(FMath::Min(Size.X, Size.Y) / 2));
	if (E <= 0.f) return LayerId;
	const FColor Tint = (Style.GetColorAndOpacityTint() * BorderColor).ToFColor(true);
	auto Box = [&](int32 I, FVector2D Pos, FVector2D Extent, bool bRotate = false)
	{
		if (Extent.X <= 0 || Extent.Y <= 0) return;
		const FSlateBrush& Brush = Edges[I];
		const FBox2f Region = Brush.GetUVRegion();
		FVector2f Min = Region.bIsValid ? Region.Min : FVector2f(0, 0);
		FVector2f Max = Region.bIsValid ? Region.Max : FVector2f(1, 1);
		if (const UTexture2D* Texture = Cast<UTexture2D>(Brush.GetResourceObject()))
		{
			const FVector2f Inset(.5f / FMath::Max(1, Texture->GetSizeX()), .5f / FMath::Max(1, Texture->GetSizeY()));
			Min += Inset; Max -= Inset;
		}
		// T/B pieces are stored like L/R in the atlas: rotate their UVs, not the quad.
		const FVector2D Points[4] = { Pos, Pos + FVector2D(Extent.X, 0), Pos + Extent, Pos + FVector2D(0, Extent.Y) };
		const FVector2f UV[4] = { bRotate ? FVector2f(Min.X, Max.Y) : Min,
			bRotate ? Min : FVector2f(Max.X, Min.Y),
			bRotate ? FVector2f(Max.X, Min.Y) : Max,
			bRotate ? Max : FVector2f(Min.X, Max.Y) };
		TArray<FSlateVertex> Vertices;
		for (int32 V = 0; V < 4; ++V)
			Vertices.Add(FSlateVertex::Make(G.GetAccumulatedRenderTransform(), FVector2f(Points[V]), UV[V], Tint));
		const TArray<SlateIndex> Indices = { 0, 1, 2, 0, 2, 3 };
		FSlateDrawElement::MakeCustomVerts(Out, LayerId + 1, Brush.GetRenderingResource(), Vertices, Indices, nullptr, 0, 0);
	};
	Box(0, {0, E}, {E, Size.Y - 2 * E}); Box(1, {Size.X - E, E}, {E, Size.Y - 2 * E});
	Box(2, {E, 0}, {Size.X - 2 * E, E}, true); Box(3, {E, Size.Y - E}, {Size.X - 2 * E, E}, true);
	Box(4, {0, 0}, {E, E}); Box(5, {Size.X - E, 0}, {E, E});
	Box(6, {0, Size.Y - E}, {E, E}); Box(7, {Size.X - E, Size.Y - E}, {E, E});
	return LayerId + 1;
}

void UWowBackdropWidget::SetBackdrop(UTexture2D* InBackground, UTexture2D* InEdge, float InEdgeSize, bool bInTile,
	float InTileSize, const FMargin& InInsets)
{
	BackgroundTexture = InBackground;
	EdgeTexture = InEdge;
	EdgeSize = InEdgeSize;
	bTile = bInTile;
	TileSize = InTileSize;
	Insets = InInsets;
	if (SlateBackdrop.IsValid())
		SlateBackdrop->SetBackdrop(BackgroundTexture, EdgeTexture, EdgeSize, bTile, TileSize, Insets);
}

void UWowBackdropWidget::SetBackgroundColor(const FLinearColor& Color)
{
	BackgroundColor = Color;
	if (SlateBackdrop.IsValid()) SlateBackdrop->SetBackgroundColor(Color);
}

void UWowBackdropWidget::SetBorderColor(const FLinearColor& Color)
{
	BorderColor = Color;
	if (SlateBackdrop.IsValid()) SlateBackdrop->SetBorderColor(Color);
}

TSharedRef<SWidget> UWowBackdropWidget::RebuildWidget()
{
	SlateBackdrop = SNew(SWowBackdrop);
	return SlateBackdrop.ToSharedRef();
}

void UWowBackdropWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (!SlateBackdrop.IsValid()) return;
	SlateBackdrop->SetBackdrop(BackgroundTexture, EdgeTexture, EdgeSize, bTile, TileSize, Insets);
	SlateBackdrop->SetBackgroundColor(BackgroundColor);
	SlateBackdrop->SetBorderColor(BorderColor);
}

void UWowBackdropWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	SlateBackdrop.Reset();
}
