#include "SWowLoadingScreen.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"

void SWowLoadingScreen::Construct(const FArguments& InArgs)
{
    // An unbound image brush draws a white rectangle over the fallback bar.
    ProgressBorderBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
    ChildSlot
    [
        SNew(SOverlay)
        + SOverlay::Slot()
        .HAlign(HAlign_Fill)
        .VAlign(VAlign_Fill)
        [
            SNew(SImage)
            .Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .ColorAndOpacity(FLinearColor::Black)
        ]
        + SOverlay::Slot()
        .HAlign(HAlign_Fill)
        .VAlign(VAlign_Fill)
        [
            // Never stretch the artwork. The black overlay behind it becomes
            // pillarbox/letterbox space for non-matching display ratios.
            SNew(SScaleBox)
            .Stretch(EStretch::ScaleToFit)
            .StretchDirection(EStretchDirection::Both)
            [
                SAssignNew(BackgroundBox, SBox)
                .WidthOverride(1024.0f)
                .HeightOverride(768.0f)
                [
                    SAssignNew(BackgroundImage, SImage)
                    .Image(FCoreStyle::Get().GetBrush("Black"))
                ]
            ]
        ]
        + SOverlay::Slot()
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Bottom)
        .Padding(FMargin(90.0f, 0.0f, 90.0f, 130.0f))
        [
            SAssignNew(TipTextBlock, STextBlock)
            .Text(FText::GetEmpty())
            .Justification(ETextJustify::Center)
            .ColorAndOpacity(FLinearColor(1.0f, 0.82f, 0.22f, 1.0f))
            .ShadowOffset(FVector2D(1.5f, 1.5f))
            .ShadowColorAndOpacity(FLinearColor::Black)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
        ]
        + SOverlay::Slot()
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Bottom)
        .Padding(FMargin(0.0f, 0.0f, 0.0f, 59.0f))
        [
            SNew(SBox)
            .WidthOverride(1000.0f)
            // Loading-BarBackground, Loading-BarFill and Loading-BarBorder are
            // authored as one 1024x64 stack. Keep that native aspect ratio so
            // the ornate caps are not flattened vertically.
            .HeightOverride(62.5f)
            [
                SNew(SOverlay)
                + SOverlay::Slot()
                // The source fill/background BLPs carry tinted pixels in their
                // transparent outer padding. Confine progress to the physical
                // trough inside the original border artwork.
                .Padding(FMargin(55.0f, 20.0f, 55.0f, 20.0f))
                [
                    SAssignNew(ProgressBar, SProgressBar)
                    .Percent(0.0f)
                    .FillColorAndOpacity(FLinearColor::White)
                    .BackgroundImage(FCoreStyle::Get().GetBrush("ProgressBar.Background"))
                    .FillImage(FCoreStyle::Get().GetBrush("ProgressBar.Fill"))
                ]
                + SOverlay::Slot()
                .HAlign(HAlign_Fill)
                .VAlign(VAlign_Fill)
                // Let the decorative end caps extend slightly beyond the
                // trough. This preserves their sharper, arrow-like profile.
                .Padding(FMargin(-14.0f, 0.0f, -14.0f, 0.0f))
                [
                    SNew(SImage)
                    .Image(&ProgressBorderBrush)
                    .ColorAndOpacity(FLinearColor::White)
                ]
            ]
        ]
    ];
}

void SWowLoadingScreen::SetProgressTextures(UTexture2D* TrackTexture, UTexture2D* FillTexture, UTexture2D* BorderTexture)
{
    auto ConfigureBrush = [](FSlateBrush& Brush, TStrongObjectPtr<UTexture2D>& Storage, UTexture2D* Texture)
    {
        if (!Texture) return;
        Storage.Reset(Texture);
        Brush = FSlateBrush();
        Brush.SetResourceObject(Storage.Get());
        Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
        Brush.DrawAs = ESlateBrushDrawType::Image;
    };

    ConfigureBrush(ProgressFillBrush, ProgressFillTexture, FillTexture);
    ConfigureBrush(ProgressBorderBrush, ProgressBorderTexture, BorderTexture);

    // The fill atlas contains the richer blue texture and lower shadow from
    // the original client, surrounded by transparent/tinted padding. Sample
    // only its physical trough so that padding cannot become side rectangles.
    if (FillTexture)
    {
        ProgressFillBrush.SetUVRegion(FBox2D(
            FVector2D(0.055f, 0.32f),
            FVector2D(0.945f, 0.68f)));
    }

    // Empty progress is genuinely transparent: the loading illustration is
    // visible through the ornate border instead of an artificial black track.
    ProgressTrackBrush = *FCoreStyle::Get().GetBrush("WhiteBrush");
    ProgressTrackBrush.TintColor = FSlateColor(FLinearColor::Transparent);

    if (ProgressBar.IsValid())
    {
        ProgressBar->SetBackgroundImage(&ProgressTrackBrush);
        ProgressBar->SetFillImage(&ProgressFillBrush);
    }
}

void SWowLoadingScreen::SetBackgroundImage(UTexture2D* Texture)
{
    if (!BackgroundImage.IsValid())
    {
        return;
    }

    if (Texture)
    {
        BackgroundTexture.Reset(Texture);
        BackgroundBrush = FSlateBrush();
        BackgroundBrush.SetResourceObject(BackgroundTexture.Get());
        BackgroundBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
        BackgroundBrush.DrawAs = ESlateBrushDrawType::Image;
        BackgroundImage->SetImage(&BackgroundBrush);

        // Vanilla's legacy artwork is close to 4:3, while the Wrath catalog
        // stores several paintings in a square canvas and expects the client
        // to present them in a wide frame.  Preserving that square source
        // ratio creates the very large pillarbox bars seen on a 16:9 display.
        // Give square/near-square artwork the Wrath 16:9 presentation box;
        // genuine 4:3 and wider images still retain their source framing.
        if (BackgroundBox.IsValid() && Texture->GetSizeX() > 0 && Texture->GetSizeY() > 0)
        {
            const float AspectRatio = static_cast<float>(Texture->GetSizeX()) /
                static_cast<float>(Texture->GetSizeY());
            // The Wrath client frame is wider than the legacy 4:3 canvas but
            // not a full 16:9 stretch.  1.6:1 leaves the small side fields
            // visible in the original 1920x1080 reference screenshot.
            constexpr float WideReferenceWidth = 1536.0f;
            constexpr float WideReferenceHeight = 960.0f;
            constexpr float VanillaReferenceHeight = 768.0f;
            constexpr float MaxReferenceWidth = 1536.0f;
            float ReferenceWidth = 0.0f;
            float ReferenceHeight = 0.0f;
            if (AspectRatio <= 1.20f)
            {
                ReferenceWidth = WideReferenceWidth;
                ReferenceHeight = WideReferenceHeight;
            }
            else
            {
                ReferenceHeight = VanillaReferenceHeight;
                ReferenceWidth = ReferenceHeight * AspectRatio;
                if (ReferenceWidth > MaxReferenceWidth)
                {
                    ReferenceWidth = MaxReferenceWidth;
                    ReferenceHeight = ReferenceWidth / AspectRatio;
                }
            }
            BackgroundBox->SetWidthOverride(ReferenceWidth);
            BackgroundBox->SetHeightOverride(ReferenceHeight);
        }
    }
    else
    {
        BackgroundTexture.Reset();
        BackgroundBrush = FSlateBrush();
        BackgroundImage->SetImage(FCoreStyle::Get().GetBrush("Black"));
    }
}

void SWowLoadingScreen::SetTipText(const FString& Text)
{
    if (TipTextBlock.IsValid())
    {
        TipTextBlock->SetText(FText::FromString(Text));
    }
}

void SWowLoadingScreen::SetProgressText(const FString& Text)
{
    // Intentionally silent: loading status is represented only by the bar.
}

void SWowLoadingScreen::SetProgress(float Fraction, const FString& StatusText)
{
    const float ClampedProgress = FMath::Clamp(Fraction, 0.0f, 1.0f);
    DisplayedProgress = ClampedProgress <= KINDA_SMALL_NUMBER
        ? 0.0f
        : FMath::Max(DisplayedProgress, ClampedProgress);

    if (ProgressBar.IsValid())
    {
        ProgressBar->SetPercent(DisplayedProgress);
    }
}
