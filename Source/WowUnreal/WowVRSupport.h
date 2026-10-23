#pragma once
#include "CoreMinimal.h"

/**
 * Seated VR (OpenXR). The headset is the screen; keyboard/gamepad stay the controls.
 * Off unless Options > Experimental "VR headset (preview)" is on, or the game starts with -vr.
 * The camera code asks for the head pose and composes it onto its own view (yaw only, so the
 * horizon never tilts by itself); leaning is followed 1:1 within a small radius of the seat.
 */
struct WOWUNREAL_API FWowVRSupport
{
    /** An OpenXR runtime and a headset are present. */
    static bool IsHeadsetAvailable();
    /** Stereo rendering is on (we are drawing to the headset). */
    static bool IsActive();
    /** Turn the headset view on/off. Returns the resulting state. */
    static bool SetActive(bool bActive);
    /** Make the current head position and facing the seat's "forward" (taken from the next tracked pose). */
    static void Recenter();
    /** Head orientation and position relative to the recentered seat (UE units). */
    static bool GetHeadPose(FQuat& OutOrientation, FVector& OutPosition);
    /**
     * Call once per frame. VR is on only while in the game world and the "vrmode" option is set
     * (glue screens stay on the monitor). Ctrl+Shift+V toggles the option.
     */
    static void SyncWithOption(class APlayerController* PlayerController, bool bInGameWorld);
    static FString GetSystemName();
    /**
     * The head's look direction with natural tremor filtered out (~100 ms exponential filter),
     * for gaze aiming (stare-to-click) only - the camera itself always follows the head 1:1.
     * Zero vector while VR is off. Updated every frame by the VR UI presenter.
     */
    static FVector GetSmoothedGazeDirection();
    static void SetSmoothedGazeDirection(const FVector& Direction);
    /**
     * A message for the player about frame generation and VR, or empty. The serial goes up each
     * time a new message is set; the UI shows it once (e.g. as a system/error message) when the
     * serial changes. Set only when frame generation is switched on in Options:
     *  - VR on:  frame generation is off while the headset is on.
     *  - VR off: it cannot be switched back on after VR - restart the game to use it again.
     */
    static int32 GetFrameGenerationNoticeSerial();
    static FString GetFrameGenerationNotice();
    /** True once VR has been on this session: frame generation stays off until restart. */
    static bool IsFrameGenerationBlockedUntilRestart();
};
