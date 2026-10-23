#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AWowAudioManager;
class SWidgetSwitcher;
DECLARE_DELEGATE_TwoParams(FOnWowInterfaceOptionChanged, FString, bool);

/** Native glue-screen options shared by offline and online login flows. */
class SWowLoginOptions : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWowLoginOptions) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AWowAudioManager>, AudioManager)
		SLATE_EVENT(FSimpleDelegate, OnClose)
        SLATE_ARGUMENT(TSharedPtr<SWidget>, ArtworkOptions)
        SLATE_EVENT(FSimpleDelegate, OnArtworkChanged)
        SLATE_EVENT(FOnWowInterfaceOptionChanged, OnInterfaceOptionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetActivePage(int32 PageIndex);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TSharedRef<SWidget> BuildTestPage();
    TSharedRef<SWidget> BuildExperimentalPage();
	TSharedRef<SWidget> BuildVideoPage();
    TSharedRef<SWidget> BuildInterfacePage();
    FOnWowInterfaceOptionChanged OnInterfaceOptionChanged;
	TSharedRef<SWidget> BuildSoundPage();
	TSharedRef<SWidget> MakeComboRow(
		const FString& Label,
		const TArray<TSharedPtr<FString>>* Options,
		TFunction<void(TSharedPtr<FString>)> OnSelected,
		TAttribute<FText> CurrentText);
	TSharedRef<SWidget> MakeVolumeSlider(
		const FString& Label,
		float InitialValue,
		TFunction<void(float)> OnChanged);
	FReply ShowVideoPage();
	FReply ShowSoundPage();
	FReply ApplyVideoSettings();
	FReply CloseOptions();
	void PopulateDisplayOptions();
	void SyncActiveDisplayState();

	TWeakObjectPtr<AWowAudioManager> AudioManager;
	FSimpleDelegate OnClose;
    FSimpleDelegate OnArtworkChanged;
    bool bPendingLoginMods = false;
    TSharedPtr<SWidget> ArtworkOptions;
	TSharedPtr<SWidgetSwitcher> SectionSwitcher;
	TArray<TSharedPtr<FString>> WeatherPreviewOptions;
	TArray<TSharedPtr<FString>> ResolutionOptions;
	TArray<TSharedPtr<FString>> RefreshRateOptions;
	TArray<TSharedPtr<FString>> WindowModeOptions;
	TArray<TSharedPtr<FString>> QualityOptions;
	TArray<TSharedPtr<FString>> AntiAliasingOptions;
	TArray<TSharedPtr<FString>> FsrQualityOptions;
    TArray<TSharedPtr<FString>> DlssQualityOptions;
    TArray<TSharedPtr<FString>> NvidiaFGOptions;
    TArray<int32> NvidiaFGModes;
    int32 PendingDlssQualityMode=1;
    int32 PendingNvidiaFGMode=17;
    bool bDlssAvailable=false;
    bool bNvidiaFGAvailable=false;
	FIntPoint PendingResolution = FIntPoint(1600, 900);
	FIntPoint LastObservedResolution = FIntPoint::ZeroValue;
	int32 PendingRefreshRate = 60;
	int32 PendingWindowMode = 2;
	int32 LastObservedWindowMode = INDEX_NONE;
	int32 PendingQuality = 2;
	float PendingResolutionScale = 100.0f;
	bool bPendingVSync = true;
	bool bPendingDisplayChange = false;
	bool bPendingDepthOfField = false;
    float PendingDepthOfFieldStrength = 2.0f;
	bool bPendingMotionBlur = false;
    bool bPendingDarkerLighting = false;
    bool bPendingEntityCollision = false;
    bool bPendingFirstPerson = false;
    bool bPendingHelmetView = false;

	bool bPendingVolumetricFog = false;
	bool bPendingLumen = false;
	bool bPendingHardwareRayTracing = false;
	bool bPendingFrameGeneration = false;
	bool bPendingPerformanceTimings = false;
	bool bPendingPerformanceRenderingStatus = false;
	bool bFsrAvailable = false;
	bool bFrameGenerationAvailable = false;
	int32 PendingAntiAliasingMethod = 3;
	int32 PendingFsrQualityMode = 1;
};
