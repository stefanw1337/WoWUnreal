#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class UTexture2D;

/**
 * Aspect-correct loading artwork with a silent progress bar.
 */
class SWowLoadingScreen : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowLoadingScreen) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /** Set the background loading screen image */
    void SetBackgroundImage(UTexture2D* Texture);

    /** Apply the original loading-bar artwork when it is available in the data files. */
    void SetProgressTextures(UTexture2D* TrackTexture, UTexture2D* FillTexture, UTexture2D* BorderTexture);

    /** Set the optional gameplay tip shown above the loading bar. */
    void SetTipText(const FString& Text);

    /** Update the progress text */
    void SetProgressText(const FString& Text);

    /** Update progress with fraction and status (convenience) */
    void SetProgress(float Fraction, const FString& StatusText);

private:
    TSharedPtr<class SImage> BackgroundImage;
    TSharedPtr<class SBox> BackgroundBox;
    TSharedPtr<class SProgressBar> ProgressBar;
    TSharedPtr<class STextBlock> TipTextBlock;
    TStrongObjectPtr<UTexture2D> BackgroundTexture;
    FSlateBrush BackgroundBrush;
    TStrongObjectPtr<UTexture2D> ProgressTrackTexture;
    TStrongObjectPtr<UTexture2D> ProgressFillTexture;
    TStrongObjectPtr<UTexture2D> ProgressBorderTexture;
    FSlateBrush ProgressTrackBrush;
    FSlateBrush ProgressFillBrush;
    FSlateBrush ProgressBorderBrush;
    float DisplayedProgress = 0.0f;
};
