#pragma once
#include "CoreMinimal.h"

/**
 * VR comfort settings against motion sickness. Three presets plus individual values;
 * changing any value turns the preset into Custom. Stored in GameUserSettings
 * ([WowUnreal.VRComfort]). Engine side by Claude-Perf; the Options page (preset buttons and
 * sliders) is Claude-UI's.
 */
enum class EWowVRComfortPreset : uint8 { Comfortable = 0, Balanced = 1, Immersive = 2, Custom = 3 };

struct FWowVRComfort
{
    static EWowVRComfortPreset GetPreset();
    /** Comfortable, Balanced or Immersive: sets every value below. Custom is ignored. */
    static void ApplyPreset(EWowVRComfortPreset Preset);

    /** Turning with mouse/keys jumps in steps instead of rotating smoothly. */
    static bool GetSnapTurn();             static void SetSnapTurn(bool bValue);
    /** Step size for snap turning, 15-90 degrees. */
    static float GetSnapTurnDegrees();     static void SetSnapTurnDegrees(float Value);
    /** Dark edge around the view while moving or turning, 0 (off) - 1 (strong). */
    static float GetVignette();            static void SetVignette(float Value);
    /** How much further the view tilts than the head when looking down, 1.0 (1:1) - 3.0. */
    static float GetLookDownGain();        static void SetLookDownGain(float Value);
    /** The eye leans out over the chest when looking far down. */
    static bool GetLeanOver();             static void SetLeanOver(bool bValue);
    /** Start/stop acceleration scale, 0.5 (gentle) - 1.5 (snappy). */
    static float GetAcceleration();        static void SetAcceleration(float Value);
    /** Depth of field inside the headset (still needs the general DoF option). */
    static bool GetDepthOfField();         static void SetDepthOfField(bool bValue);
    /** Head stillness: how much of the wearer's own pulse/breathing tremor is filtered, 0 - 1. */
    static float GetHeadStillness();       static void SetHeadStillness(float Value);

    /**
     * Headset graphics, independent of the comfort preset: 0 = Performance (Balanced plus no
     * volumetric fog, medium shadows), 1 = Balanced (default: shadow maps instead of ray-traced
     * shadows, cheaper Lumen GI; same lighting look as the monitor), 2 = Full (the monitor
     * settings), 3 = Potato (Performance with Unreal's TAAU from 60 % instead
     * of FSR - for weak PCs, no FSR features). Applied live.
     */
    static int32 GetGraphicsLevel();       static void SetGraphicsLevel(int32 Level);
};
