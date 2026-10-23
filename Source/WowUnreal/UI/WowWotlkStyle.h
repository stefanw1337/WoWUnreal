#pragma once

#include "CoreMinimal.h"
#include "WowColor.h"
#include "Brushes/SlateBoxBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Styling/SlateBrush.h"
#include "Styling/CoreStyle.h"

/**
 * Lightweight, original WotLK-inspired UI skin constants.
 *
 * This deliberately does not reference Blizzard UI textures. Native Slate
 * widgets use these colors/brushes first; later we can replace individual
 * brush internals with our own project-local 9-slice textures without changing
 * every consumer widget.
 */
struct FWowWotlkStyle
{
	static FString SkinPath(const TCHAR* FileName)
	{
		return FPaths::ProjectContentDir() / TEXT("WowUI/Skins/WotlkLike") / FileName;
	}

	static UTexture2D* SkinTexture(const TCHAR* FileName)
	{
		static TMap<FString, UTexture2D*> TextureCache;

		const FString Path = SkinPath(FileName);
		if (UTexture2D** ExistingTexture = TextureCache.Find(Path))
		{
			return *ExistingTexture;
		}

		UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(Path);
		if (Texture)
		{
			Texture->AddToRoot();
			Texture->SRGB = true;
			Texture->UpdateResource();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to load WotLK-like UI skin texture: %s"), *Path);
		}

		TextureCache.Add(Path, Texture);
		return Texture;
	}

	static const FSlateBrush* SolidBrush()
	{
		return FCoreStyle::Get().GetBrush("WhiteBrush");
	}

	static const FSlateBrush* TransparentBrush()
	{
		return FCoreStyle::Get().GetBrush("NoBrush");
	}

	static const FSlateBrush* OuterFrameBrush()
	{
		static UTexture2D* Texture = SkinTexture(TEXT("frame_outer_64.png"));
		static const FSlateBoxBrush Brush(Texture, FVector2D(64.0f, 64.0f), FMargin(0.22f));
		return &Brush;
	}

	static const FSlateBrush* InnerFrameBrush()
	{
		static UTexture2D* Texture = SkinTexture(TEXT("frame_inner_64.png"));
		static const FSlateBoxBrush Brush(Texture, FVector2D(64.0f, 64.0f), FMargin(0.20f));
		return &Brush;
	}

	static const FSlateBrush* PanelDarkBrush()
	{
		static UTexture2D* Texture = SkinTexture(TEXT("panel_dark_64.png"));
		static const FSlateBoxBrush Brush(Texture, FVector2D(64.0f, 64.0f), FMargin(0.08f));
		return &Brush;
	}

	static const FSlateBrush* PanelWarmBrush()
	{
		static UTexture2D* Texture = SkinTexture(TEXT("panel_warm_64.png"));
		static const FSlateBoxBrush Brush(Texture, FVector2D(64.0f, 64.0f), FMargin(0.08f));
		return &Brush;
	}

	static const FSlateBrush* PanelParchmentBrush()
	{
		static UTexture2D* Texture = SkinTexture(TEXT("panel_parchment_64.png"));
		static const FSlateBoxBrush Brush(Texture, FVector2D(64.0f, 64.0f), FMargin(0.08f));
		return &Brush;
	}

	static const FSlateBrush* TrackerPanelBrush()
	{
		static UTexture2D* Texture = SkinTexture(TEXT("tracker_panel_64.png"));
		static const FSlateBoxBrush Brush(Texture, FVector2D(64.0f, 64.0f), FMargin(0.08f));
		return &Brush;
	}

	static const FSlateBrush* RedButtonBrush()
	{
		static UTexture2D* Texture = SkinTexture(TEXT("button_red_32.png"));
		static const FSlateBoxBrush Brush(Texture, FVector2D(32.0f, 32.0f), FMargin(0.25f));
		return &Brush;
	}

	static const FSlateBrush* DarkButtonBrush()
	{
		static UTexture2D* Texture = SkinTexture(TEXT("button_dark_32.png"));
		static const FSlateBoxBrush Brush(Texture, FVector2D(32.0f, 32.0f), FMargin(0.25f));
		return &Brush;
	}

	static const FSlateBrush* SelectionBrush()
	{
		static UTexture2D* Texture = SkinTexture(TEXT("selection_blue_32.png"));
		static const FSlateBoxBrush Brush(Texture, FVector2D(32.0f, 32.0f), FMargin(0.20f));
		return &Brush;
	}

	static const FSlateBrush* SeparatorBrush()
	{
		static UTexture2D* Texture = SkinTexture(TEXT("separator_gold_64x4.png"));
		static const FSlateImageBrush Brush(Texture, FVector2D(64.0f, 4.0f));
		return &Brush;
	}

	static const FSlateBrush* MenuArrowBrush()
	{
		static UTexture2D* Texture = SkinTexture(TEXT("menu_arrow_18.png"));
		static const FSlateImageBrush Brush(Texture, FVector2D(18.0f, 18.0f));
		return &Brush;
	}

	static const FSlateBrush* MenuArrowHoverBrush()
	{
		static UTexture2D* Texture = SkinTexture(TEXT("menu_arrow_hover_18.png"));
		static const FSlateImageBrush Brush(Texture, FVector2D(18.0f, 18.0f));
		return &Brush;
	}

	static FLinearColor OuterMetal()
	{
		return WowColor::FromWow(0.060f, 0.052f, 0.044f, 0.98f);
	}

	static FLinearColor InnerMetal()
	{
		return WowColor::FromWow(0.155f, 0.122f, 0.080f, 0.98f);
	}

	static FLinearColor PanelDark()
	{
		return WowColor::FromWow(0.040f, 0.034f, 0.025f, 0.92f);
	}

	static FLinearColor PanelWarm()
	{
		return WowColor::FromWow(0.240f, 0.205f, 0.155f, 0.92f);
	}

	static FLinearColor PanelParchment()
	{
		return WowColor::FromWow(0.300f, 0.255f, 0.190f, 0.92f);
	}

	static FLinearColor TrackerBackground()
	{
		return WowColor::FromWow(0.006f, 0.006f, 0.006f, 0.26f);
	}

	static FLinearColor TrackerBorder()
	{
		return WowColor::FromWow(0.58f, 0.36f, 0.08f, 0.24f);
	}

	static FLinearColor Gold()
	{
		return WowColor::FromWow(1.0f, 0.82f, 0.10f, 1.0f);
	}

	static FLinearColor GoldDim()
	{
		return WowColor::FromWow(0.82f, 0.60f, 0.18f, 0.88f);
	}

	static FLinearColor TextLight()
	{
		return WowColor::FromWow(0.92f, 0.90f, 0.82f, 1.0f);
	}

	static FLinearColor SelectionBlue()
	{
		return WowColor::FromWow(0.16f, 0.24f, 0.44f, 0.84f);
	}

	static FLinearColor RedButton()
	{
		return WowColor::FromWow(0.50f, 0.025f, 0.010f, 0.96f);
	}

	static FLinearColor Separator()
	{
		return WowColor::FromWow(0.56f, 0.34f, 0.08f, 0.62f);
	}
};
