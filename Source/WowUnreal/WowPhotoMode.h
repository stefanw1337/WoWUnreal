#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Camera/CameraTypes.h"

class AWowGameplayController;
class SWidget;
class SWindow;
struct FInputKeyEventArgs;

/**
 * Photo mode (hold F12; tap F12 inside it takes a photo, hold F12 or Esc leaves).
 * Free camera around the character, lens (FOV, roll, aperture, focus), grading
 * filters and effects, letterbox, poses, freeze, hide character/others, time of
 * day and weather. Purely local: nothing is sent to the server.
 */
class FWowPhotoMode : public TSharedFromThis<FWowPhotoMode>
{
public:
	struct FSettings
	{
		float Fov = 60.0f;
		float Roll = 0.0f;
		bool bDepthOfField = true;
		int32 FocusMode = 0;          // 0 character, 1 screen centre, 2 manual
		float FocusDistanceM = 3.0f;  // manual focus, metres
		float Aperture = 2.8f;        // f-stop
		int32 Filter = 0;
		float Exposure = 0.0f;        // EV bias
		float Contrast = 1.0f;
		float Saturation = 1.0f;
		float Temperature = 6500.0f;
		float Vignette = 0.4f;
		float Grain = 0.0f;
		float Bloom = 0.675f;
		float Chromatic = 0.0f;
		int32 Letterbox = 0;          // 0 off, 1 2.39:1, 2 1.85:1, 3 4:3, 4 1:1
		bool bFreeze = false;
		bool bHideCharacter = false;
		bool bHideOthers = false;
	};

	explicit FWowPhotoMode(AWowGameplayController* InOwner) : Owner(InOwner) {}
	~FWowPhotoMode();

	bool IsActive() const { return bActive; }
	bool WantsCursor() const { return !bLooking; }
	void Toggle();
	void Enter();
	void Exit();
	/** Tap of F12 while active. */
	void TakePhoto();
	/** All keyboard/mouse input while active; returns true (consumed). */
	bool HandleKey(const FInputKeyEventArgs& Params);
	void Tick(float RealDeltaSeconds);
	/** Called from the character's CalcCamera while active. */
	void ApplyView(FMinimalViewInfo& View) const;

private:
	void BuildPanel();
	void RemovePanel();
	void ResetSettings();
	void ApplyFilterPreset(int32 Index);
	void ApplyPose(int32 AnimId, bool bLooping);
	void ApplyFreeze(bool bFreeze);
	void ApplyHideCharacter(bool bHide);
	void ApplyHideOthers(bool bHide);
	void UpdateAutoFocus();
	FVector FocusAnchor() const;
	TSharedRef<SWidget> MakeSlider(const FString& Label, float Min, float Max, float* Value,
		TFunction<FString(float)> Format, bool bLogarithmic = false);
	TSharedRef<SWidget> MakeChoiceRow(const TArray<FString>& Labels, TFunction<int32()> Get, TFunction<void(int32)> Set);
	TSharedRef<SWidget> MakeCheck(const FString& Label, bool* Value, TFunction<void(bool)> OnChanged);

	AWowGameplayController* Owner = nullptr;
	bool bActive = false;
	FSettings Settings;
	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	float AutoFocusDistance = 300.0f;
	TSet<FKey> HeldKeys;
	bool bLooking = false;
	/** r327 (Stefan): mouse like the game camera. Right drag looks around, left drag orbits
	 *  around the character, middle drag pans. */
	FKey DragButton;
	FVector2D LookAnchor = FVector2D::ZeroVector;
	bool bPanelVisible = true;
	int32 CaptureFramesRemaining = -1;
	bool bAwaitingCapture = false;
	FString LastPhotoFile;
	double PhotoToastUntil = 0.0;
	bool bPreviousUiHidden = false;
	float PreviousTestTime = -1.0f;
	int32 PreviousWeatherPreview = 0;
	FString PreviousDofQuality;
	int32 CurrentPose = -1;
	TSharedPtr<SWidget> Panel;
	TSharedPtr<SWidget> Letterbox;
	TWeakPtr<SWindow> HostWindow;
	TArray<TWeakObjectPtr<AActor>> HiddenActors;
};
