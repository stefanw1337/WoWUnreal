#include "WowBlendImage.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"

namespace
{
/** Leaf that paints its UWowBlendImage's brush, additively when the image asks for it. */
class SWowBlendImageLeaf : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SWowBlendImageLeaf) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, UWowBlendImage* InOwner)
	{
		Owner = InOwner;
		// Brush and colour live on the UObject and change without telling Slate.
		ForceVolatile(true);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		const UWowBlendImage* Image = Owner.Get();
		return Image ? FVector2D(Image->GetBrush().ImageSize) : FVector2D::ZeroVector;
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		const UWowBlendImage* Image = Owner.Get();
		if (!Image) return LayerId;
		const FSlateBrush& Brush = Image->GetBrush();
		if (Brush.DrawAs == ESlateBrushDrawType::NoDrawType) return LayerId;
		FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint() * Image->GetColorAndOpacity() * Brush.GetTint(InWidgetStyle);
		ESlateDrawEffect Effect = ESlateDrawEffect::None;
		if (Image->IsAdditive())
		{
			// Premultiplied blend (One, InvSrcAlpha) with zero vertex alpha and the texture alpha
			// ignored: dest + rgb, the client's ADD blend.
			Tint.R *= Tint.A; Tint.G *= Tint.A; Tint.B *= Tint.A; Tint.A = 0.f;
			Effect = ESlateDrawEffect::PreMultipliedAlpha | ESlateDrawEffect::IgnoreTextureAlpha;
		}
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), &Brush, Effect, Tint);
		return LayerId;
	}

private:
	TWeakObjectPtr<UWowBlendImage> Owner;
};
}

TSharedRef<SWidget> UWowBlendImage::RebuildWidget()
{
	BlendWidget = SNew(SWowBlendImageLeaf, this);
	return BlendWidget.ToSharedRef();
}

void UWowBlendImage::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	BlendWidget.Reset();
}
