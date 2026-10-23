#pragma once
#include "CoreMinimal.h"
#include "Layout/Geometry.h"

namespace WowViewportCoordinates
{
// Pixel coordinates used by PlayerController versus Slate's DPI-scaled layout.
inline FVector2D PixelToWidget(const FVector2D& Pixel, FIntPoint Extent,
    const FGeometry& Viewport, const FGeometry& Widget)
{
    if (Extent.X <= 0 || Extent.Y <= 0) return FVector2D::ZeroVector;
    const FVector2D Size = Viewport.GetLocalSize();
    return Widget.AbsoluteToLocal(Viewport.LocalToAbsolute(
        FVector2D(Pixel.X * Size.X / Extent.X, Pixel.Y * Size.Y / Extent.Y)));
}
}
