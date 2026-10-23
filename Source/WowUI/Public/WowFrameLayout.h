#pragma once
#include "CoreMinimal.h"

// Client-owned positions, separate from addon anchors and all other preferences.
// Normalized viewport coordinates survive resolution and UI-scale changes.
class WOWUI_API FWowFrameLayout
{
public:
    static const TMap<FString,FVector2D>& Positions();
    static void Set(const FString& Name,FVector2D Position);
    static void Save();
    static void Reset();
    static FVector2D ClampPosition(FVector2D Normalized,FVector2D Size,FVector2D ViewSize);
};
