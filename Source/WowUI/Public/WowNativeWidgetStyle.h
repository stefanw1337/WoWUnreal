#pragma once
#include "CoreMinimal.h"
#include "WowUIManager.h"
#include "WowFontManager.h"
#include "WowFrameManager.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"
#include "Brushes/SlateNoResource.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

struct FWowNativeWidgetStyle
{
    static FSlateFontInfo Heading(int32 Size = 22)
    {
        return FWowFontManager::GetScrollFont(Size);
    }
    static const FSlateBrush* Parchment(UWowUIManager* UI)
    {
        static FSlateBrush Brush;
        static TStrongObjectPtr<UTexture2D> Resource;
        if (UI && UI->GetFrameManager())
        {
            if (UTexture2D* Texture = UI->GetFrameManager()->LoadUITexture(TEXT("Interface\\QuestFrame\\UI-QuestGreeting-TopLeft")))
            {
                // BackdropBackground is a translucent black 16x16 swatch, not
                // parchment: it made black quest/gossip links unreadable.
                // Reuse only the paper inside the original quest artwork.
                if (Resource.Get() != Texture) Resource.Reset(Texture);
                Brush.SetResourceObject(Texture);
                Brush.DrawAs = ESlateBrushDrawType::Image;
                Brush.SetUVRegion(FBox2f(FVector2f(.125f, .375f), FVector2f(.98f, .98f)));
                Brush.ImageSize = FVector2D(219, 155);
                return &Brush;
            }
        }
        static const FSlateColorBrush Fallback(FLinearColor(0.72f, 0.56f, 0.31f));
        return &Fallback;
    }
    static const FButtonStyle* TextChoice()
    {
        static const FButtonStyle Style = []()
        {
            FButtonStyle Value = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder");
            Value.SetNormal(FSlateNoResource());
            Value.SetHovered(FSlateColorBrush(FLinearColor(0.34f, 0.19f, 0.06f, 0.14f)));
            Value.SetPressed(FSlateColorBrush(FLinearColor(0.34f, 0.19f, 0.06f, 0.24f)));
            Value.SetNormalPadding(FMargin(6, 5));
            Value.SetPressedPadding(FMargin(6, 5));
            return Value;
        }();
        return &Style;
    }
};
