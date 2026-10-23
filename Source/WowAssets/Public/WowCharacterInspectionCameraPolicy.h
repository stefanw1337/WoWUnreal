#pragma once
#include "CoreMinimal.h"

/** Stable body framing from the base model, excluding attached weapons/armor. */
struct FWowCharacterInspectionCameraPolicy
{
    static constexpr float FocusZoom = 300.0f; // one extra 50 cm zoom notch keeps the body sharp (user request)
    static FORCEINLINE float SmoothBlend(float Zoom, float Near, float Far)
    {
        const float Alpha = FMath::Clamp((Far - Zoom) / (Far - Near), 0.0f, 1.0f);
        return Alpha * Alpha * (3.0f - 2.0f * Alpha);
    }
    static FORCEINLINE float BodyFocusHeight(float ModelBottom, float ModelHeight)
    {
        return ModelBottom + ModelHeight * 0.72f;
    }
    static FORCEINLINE float EyeBlend(float Zoom)
    {
        return SmoothBlend(Zoom, 20.0f, 100.0f);
    }
    static FORCEINLINE float PivotHeight(float Zoom, float FarHeight, float BodyHeight, float EyeHeight)
    {
        const float BodyPivot = FMath::Lerp(FarHeight, BodyHeight, SmoothBlend(Zoom, 500.0f, 760.0f));
        return FMath::Lerp(BodyPivot, EyeHeight, EyeBlend(Zoom));
    }
    static FORCEINLINE bool ShouldFocusCharacter(float RequestedZoom)
    {
        // Use the requested zoom, never the distance shortened by wall collision.
        return RequestedZoom > 20.0f && RequestedZoom <= FocusZoom;
    }
    static FORCEINLINE float FocusDistance(float Previous, float Target, float DeltaTime, bool Inspecting)
    {
        // The tracked body is stable; it must not wait for a distant scene focus to catch up.
        if (Inspecting || Previous <= 0.0f) return Target;
        return FMath::FInterpTo(Previous, Target, DeltaTime, Target < Previous ? 14.0f : 5.0f);
    }
};
