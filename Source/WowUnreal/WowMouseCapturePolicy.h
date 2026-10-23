#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"

namespace WowMouseCapturePolicy
{
    inline EMouseCaptureMode Resolve(bool bLooking, bool bSuppressed)
    {
        if (bLooking) return EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown;
        // FSceneViewport does not deliver an ordinary press in NoCapture mode.
        // Temporary capture delivers the first press and releases on mouse-up.
        return bSuppressed ? EMouseCaptureMode::NoCapture : EMouseCaptureMode::CaptureDuringMouseDown;
    }
}
