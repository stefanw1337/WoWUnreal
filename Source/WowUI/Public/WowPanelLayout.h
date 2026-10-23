#pragma once
#include "CoreMinimal.h"

// Positions are viewport-local Slate units, independent of the OS DPI scale.
struct FWowPanelPlacement { FVector2D Position; float Scale=1; };
struct WOWUI_API FWowPanelLayout
{
    static TArray<FWowPanelPlacement> Arrange(const TArray<FVector2D>& Sizes, FVector2D Viewport)
    {
        TArray<FWowPanelPlacement> Result;
        if(Sizes.IsEmpty())return Result;
        constexpr float Margin=16, Gap=28; // room for the portrait rings that hang outside a window (Stefan 10.10: talents over the character sheet)
        const float Top=FMath::Min(100.0f,Viewport.Y*.12f);
        float Width=0,Height=1;
        for(const auto& Size:Sizes){Width+=Size.X;Height=FMath::Max(Height,(float)Size.Y);}
        const float AvailableWidth=FMath::Max(1.0f,(float)Viewport.X-2*Margin);
        const float AvailableHeight=FMath::Max(1.0f,(float)Viewport.Y-Top-100);
        // Scale the entire row uniformly, so no window or action button goes off screen.
        const float Scale=FMath::Min(1.0f,FMath::Min(AvailableWidth/FMath::Max(1.0f,Width+Gap*(Sizes.Num()-1)),AvailableHeight/Height));
        float X=Margin;
        for(const auto& Size:Sizes){Result.Add({FVector2D(X,Top),Scale});X+=(Size.X+Gap)*Scale;}
        return Result;
    }
};
