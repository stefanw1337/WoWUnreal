#include "WowPhotoMode.h"
#include "WowGameplayController.h"
#include "WowPlayerCharacter.h"
#include "WowAnimationController.h"
#include "WowScreenshotManager.h"
#include "WowKeyBindings.h"
#include "WowWindowStyle.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowPhotoMode, Log, All);

namespace
{
const FLinearColor PhotoGold(1.0f, 0.82f, 0.0f);
const FLinearColor PhotoText(0.92f, 0.92f, 0.92f);
const FLinearColor PhotoDim(0.6f, 0.6f, 0.6f);
constexpr float PhotoMaxDistance = 3000.0f; // 30 m around the character

struct FPhotoFilter
{
	const TCHAR* Name;
	float Saturation, Contrast, Temperature, Vignette, Grain, Bloom;
	FLinearColor Tint;
	FVector4 ShadowOffset;
	FVector4 HighlightGain;
};

const FPhotoFilter PhotoFilters[] = {
	{TEXT("Natural"),   1.00f, 1.00f, 6500.f, 0.40f, 0.00f, 0.675f, FLinearColor::White, FVector4(0,0,0,0), FVector4(1,1,1,1)},
	{TEXT("Warm"),      1.10f, 1.05f, 5200.f, 0.45f, 0.00f, 0.80f,  FLinearColor::White, FVector4(0,0,0,0), FVector4(1,1,1,1)},
	{TEXT("Cold"),      0.90f, 1.05f, 8500.f, 0.45f, 0.00f, 0.675f, FLinearColor::White, FVector4(0,0,0,0), FVector4(1,1,1,1)},
	{TEXT("Noir"),      0.00f, 1.35f, 6500.f, 0.80f, 0.35f, 0.50f,  FLinearColor::White, FVector4(0,0,0,0), FVector4(1,1,1,1)},
	// r329 (Stefan: sepia looked black and white): almost no saturation, a strong brown tint and
	// warm-lifted shadows, like an old photograph.
	{TEXT("Sepia"),     0.05f, 1.10f, 6500.f, 0.60f, 0.20f, 0.675f, FLinearColor(1.0f,0.80f,0.56f), FVector4(0.025f,0.012f,0.0f,0), FVector4(1.04f,1.0f,0.9f,1)},
	{TEXT("Vivid"),     1.45f, 1.15f, 6500.f, 0.30f, 0.00f, 0.80f,  FLinearColor::White, FVector4(0,0,0,0), FVector4(1,1,1,1)},
	{TEXT("Faded"),     0.70f, 0.80f, 6800.f, 0.35f, 0.15f, 0.675f, FLinearColor::White, FVector4(0.03f,0.03f,0.035f,0), FVector4(1,1,1,1)},
	{TEXT("Cinematic"), 1.10f, 1.15f, 6300.f, 0.55f, 0.08f, 0.80f,  FLinearColor::White, FVector4(-0.01f,0.004f,0.02f,0), FVector4(1.06f,1.0f,0.9f,1)},
	{TEXT("Dreamy"),    0.90f, 0.90f, 6000.f, 0.50f, 0.00f, 2.20f,  FLinearColor(1.0f,0.97f,1.0f), FVector4(0.01f,0.01f,0.02f,0), FVector4(1,1,1,1)},
};

struct FPhotoPose { const TCHAR* Name; int32 AnimId; bool bLoop; };
const FPhotoPose PhotoPoses[] = {
	{TEXT("Stand"), 0, true}, {TEXT("Ready"), 26, true}, {TEXT("Spell"), 51, true}, {TEXT("Stealth"), 120, true},
	{TEXT("Sit"), 97, true}, {TEXT("Kneel"), 115, true}, {TEXT("Sleep"), 100, true}, {TEXT("Dead"), 6, true},
	{TEXT("Talk"), 60, true}, {TEXT("Dance"), 69, true}, {TEXT("Wave"), 67, false}, {TEXT("Bow"), 66, false},
	{TEXT("Cheer"), 68, false}, {TEXT("Laugh"), 70, false}, {TEXT("Salute"), 113, false}, {TEXT("Flex"), 82, false},
	{TEXT("Point"), 84, false}, {TEXT("Roar"), 74, false},
};

const TCHAR* const WeatherNames[] = { TEXT("Live"), TEXT("Clear"), TEXT("Overcast"), TEXT("Rain"), TEXT("Storm"),
	TEXT("Snow"), TEXT("Blizzard"), TEXT("Sand"), TEXT("Ash"), TEXT("Fog") };

IConsoleVariable* PhotoCVar(const TCHAR* Name) { return IConsoleManager::Get().FindConsoleVariable(Name); }

// Stefan 10.10 (via Claude-Perf): the panel should look like the rest of the WoW UI. Friz Quadrata
// (the window title font) instead of Slate's default, the client's window frame, red panel buttons
// and gold sliders; the camera/lens/grading logic is unchanged.
FSlateFontInfo PhotoFont(int32 Size, bool /*bBold*/ = false)
{
	return FWowWindowStyle::TitleFont(Size + 1);
}
// A choice button: the client's red panel button; the chosen one lit gold.
const FLinearColor PhotoChoiceOn(1.0f, 0.86f, 0.35f);
const FLinearColor PhotoSliderBar(0.30f, 0.24f, 0.10f);

FString FormatDegrees(float V) { return FString::Printf(TEXT("%.0f\u00B0"), V); }
FString FormatMetres(float V) { return FString::Printf(TEXT("%.1f m"), V); }
FString FormatAperture(float V) { return FString::Printf(TEXT("f/%.1f"), V); }
FString FormatEv(float V) { return FString::Printf(TEXT("%+.1f EV"), V); }
FString FormatPlain(float V) { return FString::Printf(TEXT("%.2f"), V); }
FString FormatKelvin(float V) { return FString::Printf(TEXT("%.0f K"), V); }

void ForActorAndAttached(AActor* Actor, TFunctionRef<void(AActor*)> Fn)
{
	if (!IsValid(Actor)) return;
	Fn(Actor);
	TArray<AActor*> Attached;
	Actor->GetAttachedActors(Attached, true, true);
	for (AActor* Child : Attached) if (IsValid(Child)) Fn(Child);
}
}

FWowPhotoMode::~FWowPhotoMode()
{
	RemovePanel();
}

void FWowPhotoMode::Toggle()
{
	if (bActive) Exit(); else Enter();
}

void FWowPhotoMode::Enter()
{
	if (bActive || !Owner || !Owner->GetPawn() || !Owner->GetWorld()) return;
	bActive = true;

	// Stop everything the player was doing with the keyboard/mouse.
	if (Owner->bRightMouseLooking) Owner->EndRightMouseLook();
	if (Owner->bLeftMouseLooking) Owner->EndLeftMouseLook();
	Owner->RestoreGameplayInputAfterWorldEntry();
	Owner->bTurnLeftHeld = Owner->bTurnRightHeld = false;
	if (AWowPlayerCharacter* Character = Cast<AWowPlayerCharacter>(Owner->GetPawn()))
	{
		Character->bIsAutoRunning = false;
		Character->ConsumeMovementInputVector();
	}

	if (APlayerCameraManager* Camera = Owner->PlayerCameraManager)
	{
		CameraLocation = Camera->GetCameraLocation();
		CameraRotation = Camera->GetCameraRotation();
		Settings.Fov = FMath::Clamp(Camera->GetFOVAngle(), 20.0f, 120.0f);
	}
	CameraRotation.Roll = 0.0f;
	AutoFocusDistance = FMath::Max(50.0f, FVector::Distance(CameraLocation, FocusAnchor()));

	bPreviousUiHidden = Owner->bUserInterfaceHidden;
	Owner->SetUserInterfaceHidden(true);
	if (IConsoleVariable* Time = PhotoCVar(TEXT("wow.TestTime"))) PreviousTestTime = Time->GetFloat();
	if (IConsoleVariable* Weather = PhotoCVar(TEXT("wow.WeatherPreview"))) PreviousWeatherPreview = Weather->GetInt();
	// r328: the lens's depth of field needs the DoF pass even when the game option has it off.
	if (IConsoleVariable* Dof = PhotoCVar(TEXT("r.DepthOfFieldQuality"))) { PreviousDofQuality = Dof->GetString(); Dof->Set(2, ECVF_SetByCode); }
	Owner->bShowMouseCursor = true;
	HeldKeys.Reset();
	bLooking = false;
	bPanelVisible = true;
	CaptureFramesRemaining = -1;
	bAwaitingCapture = false;
	BuildPanel();
	UE_LOG(LogWowPhotoMode, Log, TEXT("Photo mode on"));
}

void FWowPhotoMode::Exit()
{
	if (!bActive) return;
	bActive = false;
	ApplyFreeze(false);
	ApplyHideCharacter(false);
	ApplyHideOthers(false);
	Settings.bFreeze = Settings.bHideCharacter = Settings.bHideOthers = false;
	if (CurrentPose >= 0)
	{
		if (AWowPlayerCharacter* Character = Owner ? Cast<AWowPlayerCharacter>(Owner->GetPawn()) : nullptr)
			if (Character->AnimationController) Character->AnimationController->StopEmote();
		CurrentPose = -1;
	}
	if (IConsoleVariable* Time = PhotoCVar(TEXT("wow.TestTime"))) Time->Set(PreviousTestTime, ECVF_SetByCode);
	if (IConsoleVariable* Weather = PhotoCVar(TEXT("wow.WeatherPreview"))) Weather->Set(PreviousWeatherPreview, ECVF_SetByCode);
	if (IConsoleVariable* Dof = PhotoCVar(TEXT("r.DepthOfFieldQuality")); Dof && !PreviousDofQuality.IsEmpty()) Dof->Set(*PreviousDofQuality, ECVF_SetByCode);
	if (bLooking && Owner) Owner->SetMouseLocation(int32(LookAnchor.X), int32(LookAnchor.Y));
	bLooking = false;
	HeldKeys.Reset();
	RemovePanel();
	if (Owner)
	{
		Owner->SetUserInterfaceHidden(bPreviousUiHidden);
		Owner->RestoreGameplayInputAfterWorldEntry();
		Owner->bShowMouseCursor = true;
	}
	UE_LOG(LogWowPhotoMode, Log, TEXT("Photo mode off"));
}

void FWowPhotoMode::TakePhoto()
{
	if (!bActive || CaptureFramesRemaining >= 0 || bAwaitingCapture) return;
	// Hide the panel for two frames so the capture only has the scene (and letterbox).
	CaptureFramesRemaining = 2;
}

bool FWowPhotoMode::HandleKey(const FInputKeyEventArgs& Params)
{
	const FKey Key = Params.Key;
	if (Params.Event == IE_Pressed)
	{
		if (Key == EKeys::Escape) { Exit(); return true; }
		if (Key == EKeys::H) { bPanelVisible = !bPanelVisible; return true; }
		if (Key == EKeys::Enter || Key == EKeys::P) { TakePhoto(); return true; }
		if (Key == EKeys::R) { Settings.Roll = 0.0f; return true; }
		// r328 (Stefan): the wheel moves the camera in and out like the game camera (toward the
		// character); Alt + wheel changes the lens (FOV) as before.
		if (Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown)
		{
			const float Dir = Key == EKeys::MouseScrollUp ? 1.0f : -1.0f;
			if (HeldKeys.Contains(EKeys::LeftAlt) || HeldKeys.Contains(EKeys::RightAlt))
				Settings.Fov = FMath::Clamp(Settings.Fov - 2.0f * Dir, 10.0f, 120.0f);
			else
			{
				const FVector Anchor = FocusAnchor();
				const FVector Offset = CameraLocation - Anchor;
				const float Distance = Offset.Size();
				const float NewDistance = FMath::Clamp(Distance * (Dir > 0.0f ? 0.88f : 1.0f / 0.88f), 30.0f, PhotoMaxDistance);
				CameraLocation = Distance > KINDA_SMALL_NUMBER ? Anchor + Offset / Distance * NewDistance
					: CameraLocation + CameraRotation.Vector() * 25.0f * Dir;
			}
			return true;
		}
		if ((Key == EKeys::RightMouseButton || Key == EKeys::LeftMouseButton || Key == EKeys::MiddleMouseButton)
			&& Owner && !bLooking)
		{
			float X = 0, Y = 0;
			if (Owner->GetMousePosition(X, Y))
			{
				bLooking = true;
				DragButton = Key;
				LookAnchor = FVector2D(X, Y);
				Owner->bShowMouseCursor = false;
			}
			return true;
		}
		HeldKeys.Add(Key);
	}
	else if (Params.Event == IE_Released)
	{
		HeldKeys.Remove(Key);
		if (bLooking && Key == DragButton)
		{
			bLooking = false;
			if (Owner)
			{
				Owner->SetMouseLocation(int32(LookAnchor.X), int32(LookAnchor.Y));
				Owner->bShowMouseCursor = true;
			}
		}
	}
	return true;
}

FVector FWowPhotoMode::FocusAnchor() const
{
	const APawn* Pawn = Owner ? Owner->GetPawn() : nullptr;
	return Pawn ? Pawn->GetActorLocation() + FVector(0, 0, 40.0f) : CameraLocation;
}

void FWowPhotoMode::UpdateAutoFocus()
{
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (!World) return;
	float Target = AutoFocusDistance;
	if (Settings.FocusMode == 0)
	{
		Target = FVector::Distance(CameraLocation, FocusAnchor());
	}
	else if (Settings.FocusMode == 1)
	{
		const FVector End = CameraLocation + CameraRotation.Vector() * 50000.0f;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(WowPhotoFocus), true);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, CameraLocation, End, ECC_Visibility, Query)
			|| World->LineTraceSingleByChannel(Hit, CameraLocation, End, ECC_Camera, Query))
			Target = Hit.Distance;
		else
			Target = 50000.0f;
	}
	AutoFocusDistance = FMath::FInterpTo(AutoFocusDistance, FMath::Max(Target, 10.0f), FApp::GetDeltaTime(), 8.0f);
}

void FWowPhotoMode::Tick(float RealDeltaSeconds)
{
	if (!bActive || !Owner) return;
	APawn* Pawn = Owner->GetPawn();
	if (!Pawn) { Exit(); return; }
	const float Delta = FMath::Clamp(RealDeltaSeconds, 0.0f, 0.1f);

	// r330 (Stefan: Esc did nothing): after a click on a slider or button the panel kept keyboard
	// focus, so Esc (and WASD) went to Slate instead of photo mode. Hand focus back to the game
	// viewport as soon as no mouse button is held.
	if (Panel.IsValid() && FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPressedMouseButtons().Num() == 0)
	{
		for (TSharedPtr<SWidget> Widget = FSlateApplication::Get().GetKeyboardFocusedWidget(); Widget.IsValid(); Widget = Widget->GetParentWidget())
		{
			if (Widget == Panel)
			{
				FSlateApplication::Get().SetAllUserFocusToGameViewport();
				break;
			}
		}
	}

	// Look: right mouse drag, cursor re-centred on its anchor every frame.
	if (bLooking)
	{
		float X = 0, Y = 0;
		if (Owner->GetMousePosition(X, Y))
		{
			const FVector2D Move = FVector2D(X, Y) - LookAnchor;
			const float Sensitivity = 0.12f * FMath::Clamp(Settings.Fov / 60.0f, 0.2f, 1.5f);
			if (DragButton == EKeys::MiddleMouseButton)
			{
				// Pan in the view plane; farther from the character pans faster.
				const float Scale = 0.35f * FMath::Clamp(FVector::Distance(CameraLocation, FocusAnchor()) / 300.0f, 0.5f, 8.0f);
				const FRotationMatrix View(CameraRotation);
				CameraLocation += (-View.GetUnitAxis(EAxis::Y) * Move.X + View.GetUnitAxis(EAxis::Z) * Move.Y) * Scale;
			}
			else
			{
				const FRotator Before = CameraRotation;
				CameraRotation.Yaw += Move.X * Sensitivity;
				CameraRotation.Pitch = FMath::Clamp(CameraRotation.Pitch - Move.Y * Sensitivity, -89.0f, 89.0f);
				if (DragButton == EKeys::LeftMouseButton)
				{
					// Orbit: swing the camera around the character by the same turn, keeping the framing.
					const FVector Anchor = FocusAnchor();
					const FQuat Turn = CameraRotation.Quaternion() * Before.Quaternion().Inverse();
					CameraLocation = Anchor + Turn.RotateVector(CameraLocation - Anchor);
				}
			}
			Owner->SetMouseLocation(int32(LookAnchor.X), int32(LookAnchor.Y));
		}
	}

	// Move: WASD; Space/E up, Ctrl/C/Q down (r327, like jumping/crouching); Shift fast, Alt slow.
	FVector Input = FVector::ZeroVector;
	if (HeldKeys.Contains(EKeys::W) || HeldKeys.Contains(EKeys::Up)) Input.X += 1;
	if (HeldKeys.Contains(EKeys::S) || HeldKeys.Contains(EKeys::Down)) Input.X -= 1;
	if (HeldKeys.Contains(EKeys::D) || HeldKeys.Contains(EKeys::Right)) Input.Y += 1;
	if (HeldKeys.Contains(EKeys::A) || HeldKeys.Contains(EKeys::Left)) Input.Y -= 1;
	if (HeldKeys.Contains(EKeys::E) || HeldKeys.Contains(EKeys::SpaceBar)) Input.Z += 1;
	if (HeldKeys.Contains(EKeys::Q) || HeldKeys.Contains(EKeys::C)
		|| HeldKeys.Contains(EKeys::LeftControl) || HeldKeys.Contains(EKeys::RightControl)) Input.Z -= 1;
	if (!Input.IsNearlyZero())
	{
		float Speed = 250.0f;
		if (HeldKeys.Contains(EKeys::LeftShift) || HeldKeys.Contains(EKeys::RightShift)) Speed *= 4.0f;
		if (HeldKeys.Contains(EKeys::LeftAlt) || HeldKeys.Contains(EKeys::RightAlt)) Speed *= 0.2f;
		const FRotator Flat(0.0f, CameraRotation.Yaw, 0.0f);
		const FVector Move = CameraRotation.Vector() * Input.X + Flat.RotateVector(FVector::RightVector) * Input.Y
			+ FVector::UpVector * Input.Z;
		CameraLocation += Move.GetSafeNormal() * Speed * Delta;
	}
	const FVector Centre = Pawn->GetActorLocation();
	const FVector Offset = CameraLocation - Centre;
	if (Offset.SizeSquared() > FMath::Square(PhotoMaxDistance)) CameraLocation = Centre + Offset.GetSafeNormal() * PhotoMaxDistance;

	UpdateAutoFocus();

	// Capture sequence.
	if (CaptureFramesRemaining > 0 && --CaptureFramesRemaining == 0)
	{
		LastPhotoFile = UWowScreenshotManager::RequestActiveViewport();
		bAwaitingCapture = !LastPhotoFile.IsEmpty();
		CaptureFramesRemaining = -1;
		if (!bAwaitingCapture) PhotoToastUntil = FPlatformTime::Seconds() + 3.0;
	}
	else if (bAwaitingCapture && !FScreenshotRequest::IsScreenshotRequested())
	{
		bAwaitingCapture = false;
		PhotoToastUntil = FPlatformTime::Seconds() + 4.0;
		UE_LOG(LogWowPhotoMode, Log, TEXT("Photo saved: %s"), *LastPhotoFile);
	}
}

void FWowPhotoMode::ApplyView(FMinimalViewInfo& View) const
{
	View.Location = CameraLocation;
	View.Rotation = FRotator(CameraRotation.Pitch, CameraRotation.Yaw, Settings.Roll);
	View.FOV = Settings.Fov;
	View.PerspectiveNearClipPlane = 5.0f;

	const FPhotoFilter& Filter = PhotoFilters[FMath::Clamp(Settings.Filter, 0, int32(UE_ARRAY_COUNT(PhotoFilters)) - 1)];
	FPostProcessSettings& PP = View.PostProcessSettings;
	PP.bOverride_DepthOfFieldFocalDistance = true;
	PP.bOverride_DepthOfFieldFstop = true;
	PP.bOverride_DepthOfFieldSensorWidth = true;
	PP.DepthOfFieldSensorWidth = 36.0f;
	PP.DepthOfFieldFstop = Settings.Aperture;
	PP.DepthOfFieldFocalDistance = !Settings.bDepthOfField ? 0.0f
		: Settings.FocusMode == 2 ? Settings.FocusDistanceM * 100.0f : AutoFocusDistance;
	// r328 (Stefan: exposure did nothing): the game fixes exposure with
	// r.EyeAdaptation.PreExposureOverride, which ignores the exposure bias. Scale the scene
	// colour instead (applied in the tonemapper on linear light, i.e. a real EV change).
	PP.bOverride_ColorContrast = true;
	PP.ColorContrast = FVector4(Settings.Contrast, Settings.Contrast, Settings.Contrast, 1.0f);
	PP.bOverride_ColorSaturation = true;
	PP.ColorSaturation = FVector4(Settings.Saturation, Settings.Saturation, Settings.Saturation, 1.0f);
	PP.bOverride_WhiteTemp = true;
	// r329 (Stefan: Warm looked cold and Cold warm): UE's WhiteTemp is the white balance, which
	// corrects for light of that colour (5200 K makes the picture bluer). The slider and presets
	// mean the colour of the light, so mirror it around neutral 6500 K.
	PP.WhiteTemp = FMath::Clamp(13000.0f - Settings.Temperature, 1500.0f, 15000.0f);
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = Settings.Vignette;
	PP.bOverride_FilmGrainIntensity = true;
	PP.FilmGrainIntensity = Settings.Grain;
	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = Settings.Bloom;
	PP.bOverride_SceneFringeIntensity = true;
	PP.SceneFringeIntensity = Settings.Chromatic * 5.0f;
	PP.bOverride_SceneColorTint = true;
	const float ExposureScale = FMath::Pow(2.0f, Settings.Exposure);
	PP.SceneColorTint = FLinearColor(ExposureScale, ExposureScale, ExposureScale, 1.0f);
	// r329: the filter tint goes in as a colour gain, which grading applies after saturation;
	// as a scene tint it was desaturated away again (Sepia came out black and white).
	PP.bOverride_ColorGain = true;
	PP.ColorGain = FVector4(Filter.Tint.R, Filter.Tint.G, Filter.Tint.B, 1.0f);
	PP.bOverride_ColorOffsetShadows = true;
	PP.ColorOffsetShadows = Filter.ShadowOffset;
	PP.bOverride_ColorGainHighlights = true;
	PP.ColorGainHighlights = Filter.HighlightGain;
	View.PostProcessBlendWeight = 1.0f;
}

void FWowPhotoMode::ResetSettings()
{
	const float Fov = Settings.Fov;
	Settings = FSettings();
	Settings.Fov = Fov;
	ApplyFreeze(false);
	ApplyHideCharacter(false);
	ApplyHideOthers(false);
}

void FWowPhotoMode::ApplyFilterPreset(int32 Index)
{
	const FPhotoFilter& Filter = PhotoFilters[FMath::Clamp(Index, 0, int32(UE_ARRAY_COUNT(PhotoFilters)) - 1)];
	Settings.Filter = Index;
	Settings.Saturation = Filter.Saturation;
	Settings.Contrast = Filter.Contrast;
	Settings.Temperature = Filter.Temperature;
	Settings.Vignette = Filter.Vignette;
	Settings.Grain = Filter.Grain;
	Settings.Bloom = Filter.Bloom;
}

void FWowPhotoMode::ApplyPose(int32 AnimId, bool bLooping)
{
	AWowPlayerCharacter* Character = Owner ? Cast<AWowPlayerCharacter>(Owner->GetPawn()) : nullptr;
	if (!Character || !Character->AnimationController) return;
	const bool bWasFrozen = Settings.bFreeze;
	if (bWasFrozen) ApplyFreeze(false);
	if (AnimId == 0) Character->AnimationController->StopEmote();
	else Character->AnimationController->PlayEmote(AnimId, bLooping);
	CurrentPose = AnimId;
	if (bWasFrozen) ApplyFreeze(true);
}

void FWowPhotoMode::ApplyFreeze(bool bFreeze)
{
	if (!Owner) return;
	// Actor time dilation freezes animation, particles and interpolation of the
	// character and every visible unit; the session/network keep running.
	const float Dilation = bFreeze ? 0.0f : 1.0f;
	ForActorAndAttached(Owner->GetPawn(), [Dilation](AActor* Actor) { Actor->CustomTimeDilation = Dilation; });
	for (const auto& Pair : Owner->SpawnedEntityActors)
		ForActorAndAttached(Pair.Value.Get(), [Dilation](AActor* Actor) { Actor->CustomTimeDilation = Dilation; });
}

void FWowPhotoMode::ApplyHideCharacter(bool bHide)
{
	if (!Owner) return;
	ForActorAndAttached(Owner->GetPawn(), [bHide](AActor* Actor) { Actor->SetActorHiddenInGame(bHide); });
}

void FWowPhotoMode::ApplyHideOthers(bool bHide)
{
	if (!Owner) return;
	if (bHide)
	{
		HiddenActors.Reset();
		for (const auto& Pair : Owner->SpawnedEntityActors)
		{
			ForActorAndAttached(Pair.Value.Get(), [this](AActor* Actor)
			{
				if (Actor->IsHidden()) return;
				Actor->SetActorHiddenInGame(true);
				HiddenActors.Add(Actor);
			});
		}
	}
	else
	{
		for (const TWeakObjectPtr<AActor>& Actor : HiddenActors) if (Actor.IsValid()) Actor->SetActorHiddenInGame(false);
		HiddenActors.Reset();
	}
}

// ── Panel ─────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> FWowPhotoMode::MakeSlider(const FString& Label, float Min, float Max, float* Value,
	TFunction<FString(float)> Format, bool bLogarithmic)
{
	auto ToSlider = [Min, Max, bLogarithmic](float V)
	{
		return bLogarithmic ? FMath::Loge(FMath::Max(V, Min) / Min) / FMath::Loge(Max / Min) : (V - Min) / (Max - Min);
	};
	auto FromSlider = [Min, Max, bLogarithmic](float S)
	{
		return bLogarithmic ? Min * FMath::Pow(Max / Min, S) : Min + (Max - Min) * S;
	};
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[SNew(STextBlock).Text(FText::FromString(Label)).Font(PhotoFont(10)).ColorAndOpacity(PhotoText)]
			+ SHorizontalBox::Slot().AutoWidth()
			[SNew(STextBlock).Font(PhotoFont(10)).ColorAndOpacity(PhotoGold)
				.Text_Lambda([Value, Format]() { return FText::FromString(Format(*Value)); })]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SSlider).IsFocusable(false).SliderBarColor(PhotoSliderBar).SliderHandleColor(PhotoGold)
			.Value_Lambda([Value, ToSlider]() { return ToSlider(*Value); })
			.OnValueChanged_Lambda([Value, FromSlider](float S) { *Value = FromSlider(S); })
		];
}

TSharedRef<SWidget> FWowPhotoMode::MakeChoiceRow(const TArray<FString>& Labels, TFunction<int32()> Get, TFunction<void(int32)> Set)
{
	TSharedRef<SWrapBox> Box = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(3, 3));
	for (int32 Index = 0; Index < Labels.Num(); ++Index)
	{
		Box->AddSlot()
		[
			SNew(SButton).IsFocusable(false).ContentPadding(FMargin(8, 3))
			.ButtonStyle(FWowWindowStyle::Button())
			.ButtonColorAndOpacity_Lambda([Get, Index]() { return Get() == Index ? PhotoChoiceOn : FLinearColor::White; })
			.OnClicked_Lambda([Set, Index]() { Set(Index); return FReply::Handled(); })
			[SNew(STextBlock).Text(FText::FromString(Labels[Index])).Font(PhotoFont(9))
				.ColorAndOpacity_Lambda([Get, Index]() { return FSlateColor(Get() == Index ? FLinearColor::White : PhotoGold); })]
		];
	}
	return Box;
}

TSharedRef<SWidget> FWowPhotoMode::MakeCheck(const FString& Label, bool* Value, TFunction<void(bool)> OnChanged)
{
	return SNew(SCheckBox).IsFocusable(false)
		.IsChecked_Lambda([Value]() { return *Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([Value, OnChanged](ECheckBoxState State)
		{
			*Value = State == ECheckBoxState::Checked;
			if (OnChanged) OnChanged(*Value);
		})
		[SNew(STextBlock).Text(FText::FromString(Label)).Font(PhotoFont(10)).ColorAndOpacity(PhotoText)];
}

void FWowPhotoMode::BuildPanel()
{
	RemovePanel();
	TSharedPtr<SWindow> Window = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
	if (!Window.IsValid()) return;
	HostWindow = Window;
	TWeakPtr<FWowPhotoMode> Weak = AsShared();
	FSettings* S = &Settings;

	auto Header = [](const TCHAR* Text)
	{
		return SNew(STextBlock).Text(FText::FromString(Text)).Font(PhotoFont(12, true)).ColorAndOpacity(PhotoGold);
	};

	TArray<FString> FilterNames;
	for (const FPhotoFilter& Filter : PhotoFilters) FilterNames.Add(Filter.Name);
	TArray<FString> PoseNames;
	for (const FPhotoPose& Pose : PhotoPoses) PoseNames.Add(Pose.Name);
	TArray<FString> Weathers;
	for (const TCHAR* Name : WeatherNames) Weathers.Add(Name);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
		[SNew(STextBlock).Text(FText::FromString(TEXT("Photo Mode"))).Font(PhotoFont(16, true)).ColorAndOpacity(PhotoGold)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[SNew(STextBlock).AutoWrapText(true).Font(PhotoFont(9)).ColorAndOpacity(PhotoDim)
			.Text(FText::FromString(TEXT("Right drag: look   Left drag: orbit   Middle drag: pan   WASD: move   Space/Ctrl: up/down   Shift: fast   Alt: slow   Wheel: closer/farther   Alt+wheel: lens zoom   R: level\nF12 / Enter: take photo   H: hide panel   Esc or hold F12: leave")))]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Header(TEXT("Camera"))]
		+ SVerticalBox::Slot().AutoHeight()[MakeSlider(TEXT("Field of view"), 10.f, 120.f, &S->Fov, FormatDegrees)]
		+ SVerticalBox::Slot().AutoHeight()[MakeSlider(TEXT("Roll"), -45.f, 45.f, &S->Roll, FormatDegrees)]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[Header(TEXT("Lens"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[MakeCheck(TEXT("Depth of field"), &S->bDepthOfField, nullptr)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[MakeChoiceRow({TEXT("Focus: character"), TEXT("Centre"), TEXT("Manual")},
			[S]() { return S->FocusMode; }, [S](int32 V) { S->FocusMode = V; })]
		+ SVerticalBox::Slot().AutoHeight()[MakeSlider(TEXT("Focus distance (manual)"), 0.3f, 100.f, &S->FocusDistanceM, FormatMetres, true)]
		+ SVerticalBox::Slot().AutoHeight()[MakeSlider(TEXT("Aperture"), 1.2f, 22.f, &S->Aperture, FormatAperture, true)]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[Header(TEXT("Filter"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[MakeChoiceRow(FilterNames, [S]() { return S->Filter; },
			[Weak](int32 V) { if (auto P = Weak.Pin()) P->ApplyFilterPreset(V); })]
		+ SVerticalBox::Slot().AutoHeight()[MakeSlider(TEXT("Exposure"), -3.f, 3.f, &S->Exposure, FormatEv)]
		+ SVerticalBox::Slot().AutoHeight()[MakeSlider(TEXT("Contrast"), 0.5f, 1.6f, &S->Contrast, FormatPlain)]
		+ SVerticalBox::Slot().AutoHeight()[MakeSlider(TEXT("Saturation"), 0.f, 2.f, &S->Saturation, FormatPlain)]
		+ SVerticalBox::Slot().AutoHeight()[MakeSlider(TEXT("White balance"), 3000.f, 11000.f, &S->Temperature, FormatKelvin)]
		+ SVerticalBox::Slot().AutoHeight()[MakeSlider(TEXT("Vignette"), 0.f, 1.5f, &S->Vignette, FormatPlain)]
		+ SVerticalBox::Slot().AutoHeight()[MakeSlider(TEXT("Film grain"), 0.f, 1.f, &S->Grain, FormatPlain)]
		+ SVerticalBox::Slot().AutoHeight()[MakeSlider(TEXT("Bloom"), 0.f, 4.f, &S->Bloom, FormatPlain)]
		+ SVerticalBox::Slot().AutoHeight()[MakeSlider(TEXT("Chromatic aberration"), 0.f, 1.f, &S->Chromatic, FormatPlain)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[SNew(STextBlock).Text(FText::FromString(TEXT("Letterbox"))).Font(PhotoFont(10)).ColorAndOpacity(PhotoText)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[MakeChoiceRow({TEXT("Off"), TEXT("2.39:1"), TEXT("1.85:1"), TEXT("4:3"), TEXT("1:1")},
			[S]() { return S->Letterbox; }, [S](int32 V) { S->Letterbox = V; })]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[Header(TEXT("Character"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[MakeChoiceRow(PoseNames, [Weak]() { auto P = Weak.Pin(); if (!P) return -1;
				for (int32 I = 0; I < int32(UE_ARRAY_COUNT(PhotoPoses)); ++I) if (PhotoPoses[I].AnimId == P->CurrentPose) return I; return -1; },
			[Weak](int32 V) { if (auto P = Weak.Pin()) P->ApplyPose(PhotoPoses[V].AnimId, PhotoPoses[V].bLoop); })]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[MakeCheck(TEXT("Freeze time (characters, effects)"), &S->bFreeze, [Weak](bool V) { if (auto P = Weak.Pin()) P->ApplyFreeze(V); })]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[MakeCheck(TEXT("Hide my character"), &S->bHideCharacter, [Weak](bool V) { if (auto P = Weak.Pin()) P->ApplyHideCharacter(V); })]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[MakeCheck(TEXT("Hide other players and NPCs"), &S->bHideOthers, [Weak](bool V) { if (auto P = Weak.Pin()) P->ApplyHideOthers(V); })]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[Header(TEXT("Scene"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[SNew(STextBlock).Text(FText::FromString(TEXT("Time of day"))).Font(PhotoFont(10)).ColorAndOpacity(PhotoText)]
			+ SHorizontalBox::Slot().AutoWidth()
			[SNew(STextBlock).Font(PhotoFont(10)).ColorAndOpacity(PhotoGold).Text_Lambda([]()
			{
				const IConsoleVariable* Time = PhotoCVar(TEXT("wow.TestTime"));
				const float Minutes = Time ? Time->GetFloat() : -1.0f;
				if (Minutes < 0.0f) return FText::FromString(TEXT("Live"));
				const int32 M = FMath::RoundToInt(Minutes);
				return FText::FromString(FString::Printf(TEXT("%02d:%02d"), M / 60, M % 60));
			})]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SSlider).IsFocusable(false).SliderBarColor(PhotoSliderBar).SliderHandleColor(PhotoGold)
			.Value_Lambda([]() { const IConsoleVariable* Time = PhotoCVar(TEXT("wow.TestTime")); const float M = Time ? Time->GetFloat() : -1.0f; return M < 0 ? 0.5f : M / 1439.0f; })
			.OnValueChanged_Lambda([](float V) { if (IConsoleVariable* Time = PhotoCVar(TEXT("wow.TestTime"))) Time->Set(FMath::RoundToFloat(V * 1439.0f), ECVF_SetByCode); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SButton).IsFocusable(false).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center)
			.OnClicked_Lambda([Weak]() { if (auto P = Weak.Pin()) if (IConsoleVariable* Time = PhotoCVar(TEXT("wow.TestTime"))) Time->Set(P->PreviousTestTime, ECVF_SetByCode); return FReply::Handled(); })
			[SNew(STextBlock).Text(FText::FromString(TEXT("Live time"))).Font(PhotoFont(9)).ColorAndOpacity(PhotoGold)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[SNew(STextBlock).Text(FText::FromString(TEXT("Weather"))).Font(PhotoFont(10)).ColorAndOpacity(PhotoText)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[MakeChoiceRow(Weathers, []() { const IConsoleVariable* W = PhotoCVar(TEXT("wow.WeatherPreview")); return W ? W->GetInt() : 0; },
			[](int32 V) { if (IConsoleVariable* W = PhotoCVar(TEXT("wow.WeatherPreview"))) W->Set(V, ECVF_SetByCode); })]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 3, 0)
			[SNew(SButton).IsFocusable(false).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center)
				.OnClicked_Lambda([Weak]() { if (auto P = Weak.Pin()) P->TakePhoto(); return FReply::Handled(); })
				[SNew(STextBlock).Text(FText::FromString(TEXT("Take photo"))).Font(PhotoFont(11, true)).ColorAndOpacity(PhotoGold)]]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 3, 0)
			[SNew(SButton).IsFocusable(false).ButtonStyle(FWowWindowStyle::Button())
				.OnClicked_Lambda([Weak]() { if (auto P = Weak.Pin()) P->ResetSettings(); return FReply::Handled(); })
				[SNew(STextBlock).Text(FText::FromString(TEXT("Reset"))).Font(PhotoFont(11)).ColorAndOpacity(PhotoGold)]]
			+ SHorizontalBox::Slot().AutoWidth()
			[SNew(SButton).IsFocusable(false).ButtonStyle(FWowWindowStyle::Button())
				.OnClicked_Lambda([Weak]() { if (auto P = Weak.Pin()) P->Exit(); return FReply::Handled(); })
				[SNew(STextBlock).Text(FText::FromString(TEXT("Exit"))).Font(PhotoFont(11)).ColorAndOpacity(PhotoGold)]]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[SNew(STextBlock).AutoWrapText(true).Font(PhotoFont(9)).ColorAndOpacity(PhotoGold)
			.Text_Lambda([Weak]()
			{
				auto P = Weak.Pin();
				if (!P || FPlatformTime::Seconds() > P->PhotoToastUntil) return FText::GetEmpty();
				return FText::FromString(P->LastPhotoFile.IsEmpty() ? FString(TEXT("Photo failed"))
					: FString::Printf(TEXT("Saved %s"), *FPaths::GetCleanFilename(P->LastPhotoFile)));
			})];

	Panel = SNew(SBox).HAlign(HAlign_Right).VAlign(VAlign_Fill).Padding(FMargin(0, 40, 16, 40))
		.Visibility_Lambda([Weak]()
		{
			auto P = Weak.Pin();
			return P && P->bPanelVisible && P->CaptureFramesRemaining < 0 && !P->bAwaitingCapture
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SBox).WidthOverride(330.0f)
			[
				// The client's window frame (the same one as the other native windows).
				SNew(SWowWindowFrame).Padding(FMargin(14, 12))
				[SNew(SScrollBox) + SScrollBox::Slot()[Content]]
			]
		];

	// Letterbox / pillarbox bars stay visible during capture so they are part of the photo.
	auto BarSize = [Weak](bool bVertical) -> FOptionalSize
	{
		auto P = Weak.Pin();
		TSharedPtr<SWindow> W = P ? P->HostWindow.Pin() : nullptr;
		if (!P || !W.IsValid() || P->Settings.Letterbox == 0) return FOptionalSize(0.0f);
		static const float Ratios[] = { 0.0f, 2.39f, 1.85f, 4.0f / 3.0f, 1.0f };
		const float Ratio = Ratios[FMath::Clamp(P->Settings.Letterbox, 0, 4)];
		const FVector2D Size = W->GetPaintSpaceGeometry().GetLocalSize();
		if (Size.X <= 0 || Size.Y <= 0) return FOptionalSize(0.0f);
		if (bVertical) return FOptionalSize(FMath::Max(0.0f, float(Size.Y - Size.X / Ratio) * 0.5f));
		return FOptionalSize(FMath::Max(0.0f, float(Size.X - Size.Y * Ratio) * 0.5f));
	};
	auto Bar = []() { return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor::Black); };
	Letterbox = SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
		+ SOverlay::Slot().VAlign(VAlign_Top)[SNew(SBox).HeightOverride_Lambda([BarSize]() { return BarSize(true); })[Bar()]]
		+ SOverlay::Slot().VAlign(VAlign_Bottom)[SNew(SBox).HeightOverride_Lambda([BarSize]() { return BarSize(true); })[Bar()]]
		+ SOverlay::Slot().HAlign(HAlign_Left)[SNew(SBox).WidthOverride_Lambda([BarSize]() { return BarSize(false); })[Bar()]]
		+ SOverlay::Slot().HAlign(HAlign_Right)[SNew(SBox).WidthOverride_Lambda([BarSize]() { return BarSize(false); })[Bar()]];

	Window->AddOverlaySlot(100)[Letterbox.ToSharedRef()];
	Window->AddOverlaySlot(101)[Panel.ToSharedRef()];
}

void FWowPhotoMode::RemovePanel()
{
	if (TSharedPtr<SWindow> Window = HostWindow.Pin())
	{
		if (Panel.IsValid()) Window->RemoveOverlaySlot(Panel.ToSharedRef());
		if (Letterbox.IsValid()) Window->RemoveOverlaySlot(Letterbox.ToSharedRef());
	}
	Panel.Reset();
	Letterbox.Reset();
	HostWindow.Reset();
}
