#pragma once

#include "CoreMinimal.h"

/**
 * Local client-side preferences that must never be controlled by a game server.
 * Values are stored in the user's GameUserSettings ini file.
 */
class WOWCLIENT_API FWowClientPreferences
{
public:
    /** 0 = hardware, 1 = software; both use the local WoW cursor artwork. */
    static int32 GetCursorMode();
    static float GetCameraMouseSensitivity();
    static void SetCameraMouseSensitivity(float Value);
    static float GetMouseCursorScale();
    static void SetMouseCursorScale(float Value);
    /** GameTooltip size relative to the original client (0.5 - 1.5). */
    static float GetTooltipScale();
    /** Sky: moon disc size (1-5x, default 2.5x) and experimental sun size (1-10x). Set live in Options > Experimental. */
    static float GetMoonScale();
    static float GetSunScale();
    static void SetMoonScale(float Value);
    /** VR dwell click: seconds the crosshair must rest on something before it clicks (0.5-2). */
    static float GetVrDwellTime();
    static void SetVrDwellTime(float Seconds);
    static void SetSunScale(float Value);
    static void SetTooltipScale(float Value);
    /** Seconds a world unit's tooltip stays after the pointer leaves it, before it fades (0 = hide at once). */
    static float GetTooltipFadeTime();
    static void SetTooltipFadeTime(float Value);
    /** Per-group UI sizes on top of the global UI Scale (0.5 - 2.0, default 1). Applied live by the frame manager. */
    static float GetUnitFrameScale();
    static void SetUnitFrameScale(float Value);
    static float GetActionBarScale();
    static void SetActionBarScale(float Value);
    static float GetMinimapScale();
    static void SetMinimapScale(float Value);
    static FString GetDataSource();
    static bool SetDataSource(const FString& Path);
    static int32 GetNameplateMode();
    static void SetNameplateMode(int32 Mode);
    /** Auto Loot Key (3.3.5 Interface > Controls): 0 Shift, 1 Ctrl, 2 Alt, 3 None. Held while looting it reverses Auto Loot. */
    static int32 GetAutoLootKey();
    static void SetAutoLootKey(int32 Key);
    /** Use of the WoW client's own WTF folder for a realm (FWowSavedVariables::EClientWtfMode):
        0 own settings, 1 fresh copy each launch, 2 copy once, 3 shared; -1 not chosen yet. */
    static int32 GetClientWtfMode(const FString& RealmName);
    /** Intro movie before the first character select (FWowTrailerPlayer): 0 every time, 1 first time only
        (per server data folder and movie, the default), 2 never. */
    static int32 GetIntroMovieMode();
    static void SetIntroMovieMode(int32 Mode);
    static void SetClientWtfMode(const FString& RealmName, int32 Mode);
    static bool HasWorldOption(const FString& Name);
    static bool GetWorldOption(const FString& Name);
    static void SetWorldOption(const FString& Name, bool Enabled);
    static float GetGamma();
    static void SetGamma(float Value);
    static bool IsDarkerRealisticLightingEnabled();
    /** Fire/lamp light strength and reach (r287, Stefan: with the darker realistic lighting a
     *  brazier barely lit its corner). 1 with standard lighting. */
    // r294 (Stefan: Stormwind Keep's hall lamps barely light anything, 'maybe 100 times more'):
    // 10x instead of 3x, longer reach, and the lamps now feed indirect (bounce) light so walls
    // and floors between fixtures are lit too, not only the spot under each lamp.
    static float GetFireLightIntensityScale() { return IsDarkerRealisticLightingEnabled() ? 10.0f : 1.0f; }
    static float GetFireLightRadiusScale() { return IsDarkerRealisticLightingEnabled() ? 1.8f : 1.0f; }
    static float GetFireLightIndirectScale() { return IsDarkerRealisticLightingEnabled() ? 4.0f : 0.0f; }
    /** r322 (restored r346): lamp/torch shadow softness multiplier for source radii. */
    static float GetFireLightSoftness() { return IsDarkerRealisticLightingEnabled() ? 6.0f : 1.0f; } // r352: 4 -> 6 (Stefan: still razor-sharp)
    static void SetDarkerRealisticLightingEnabled(bool bEnabled);

    static bool IsNpcAndPlayerCollisionEnabled();
    static void SetNpcAndPlayerCollisionEnabled(bool bEnabled);

    static bool IsFirstPersonImmersionEnabled();
    static void SetFirstPersonImmersionEnabled(bool bEnabled);
    static bool IsFirstPersonHelmetViewEnabled();
    static void SetFirstPersonHelmetViewEnabled(bool bEnabled);

    /** Overall FrameXML UI size. 1.0 is the original 768-line reference. */
    static float GetUIScale();
    static void SetUIScale(float Scale);
    /** The interface is in the VR headset: UI scale and frame positions use the VR preset. */
    static void SetVrUiMode(bool bVr);
    static bool IsVrUiMode();

    static float GetMasterVolume();
    static void SetMasterVolume(float Volume);
    static float GetMusicVolume();
    static void SetMusicVolume(float Volume);
    static float GetSfxVolume();
    static void SetSfxVolume(float Volume);
    static float GetAmbienceVolume();
    static void SetAmbienceVolume(float Volume);

    /** Preferred monitor refresh used by the native video options and FPS cap. */
    static int32 GetPreferredRefreshRate();
    static void SetPreferredRefreshRate(int32 RefreshRate);

    static float GetFocusTestDistance();
    static void SetFocusTestDistance(float Value);
    static float GetFocusTestWidth();
    static void SetFocusTestWidth(float Value);
    static float GetDepthOfFieldStrength();
    static void SetDepthOfFieldStrength(float Strength);
    static bool IsDepthOfFieldEnabled();
    static void SetDepthOfFieldEnabled(bool bEnabled);
    /** 0=None, 1=FXAA, 2=TAA, 3=TSR, 4=AMD FSR, 5=NVIDIA DLSS, 6=NVIDIA DLAA. Hardware checked at runtime. */
    static int32 GetAntiAliasingMethod();
    static void SetAntiAliasingMethod(int32 Method);
    /** AMD FSR quality: 0=Native AA, 1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance. */
    static int32 GetFsrQualityMode();
    static void SetFsrQualityMode(int32 Mode);
    static bool IsDlssAvailable();
    static int32 GetDlssQualityMode();
    static void SetDlssQualityMode(int32 Mode);
    static bool IsNvidiaFrameGenerationAvailable();
    static bool IsNvidiaFrameGenerationActive();
    /** Frames shown per rendered frame with NVIDIA frame generation (1 when off), r286. */
    static int32 GetNvidiaPresentedFramesPerRender();
    static TArray<int32> GetNvidiaFrameGenerationModes();
    static int32 GetNvidiaFrameGenerationMode();
    static void SetNvidiaFrameGenerationMode(int32 Mode);
    static FString GetNvidiaFrameGenerationModeLabel(int32 Mode);
    static void SuspendFrameGeneration();
    /** "Sync" = G-Sync / FreeSync first, VSync as fallback. VSync stays on (tear-free; with
        G-Sync/FreeSync active it adds no halving inside the monitor's range), but when the
        frame work no longer fits one refresh it is released briefly instead of dropping to
        half rate (60 -> 30), and re-engaged once frames fit again. Ticked every frame. */
    static void TickAdaptiveSync(float DeltaTime);
    /** Short label for the CTRL+R overlay, e.g. "G-Sync/FreeSync + VSync" or "released (slow frames)". */
    static FString GetSyncStatusLabel();
    static void SetGameplayRendering(bool bActive);
    static void ApplyFrameGeneration();
    static bool IsFrameGenerationEnabled();
    static void SetFrameGenerationEnabled(bool bEnabled);
    /** Add Game/Render/RHI/GPU frame times to the otherwise FPS-only overlay. */
    static bool ArePerformanceTimingsEnabled();
    static void SetPerformanceTimingsEnabled(bool bEnabled);
    /** Add active upscaler, frame generation, frame cap and VSync state to the FPS overlay. */
    static bool IsPerformanceRenderingStatusEnabled();
    static void SetPerformanceRenderingStatusEnabled(bool bEnabled);
    static bool IsVolumetricFogEnabled();
    static void SetVolumetricFogEnabled(bool bEnabled);
    static bool IsLumenEnabled();
    static void SetLumenEnabled(bool bEnabled);
    static bool IsHardwareRayTracingEnabled();
    static void SetHardwareRayTracingEnabled(bool bEnabled);
    static bool IsLumenAvailable();
    static bool IsHardwareRayTracingAvailable();
    static void ApplyAdvancedVideoPreferences();
    /**
     * Called at the end of every video/frame-generation apply. The VR headset view (Claude-Perf,
     * FWowVRSupport) puts its own render profile back on top here, in the same frame, so an
     * Options change while in VR never switches Lumen / ray tracing / frame generation on for a
     * frame (that cost 0.5-0.7 s hitches and crashed FSR frame interpolation). nullptr = none.
     */
    static void SetRenderOverrideHook(void (*Hook)());

    static bool IsAntiLag2Enabled();
    static void SetAntiLag2Enabled(bool bEnabled);
    static void ApplySavedAntiLag2();

    static bool IsMotionBlurEnabled();
    static void SetMotionBlurEnabled(bool bEnabled);
};