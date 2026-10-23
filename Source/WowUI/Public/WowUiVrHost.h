#pragma once
#include "CoreMinimal.h"
#include "WowUiSurfaces.h"

class UTextureRenderTarget2D;
class FWowFrameManager;

/** One zone of the UI as a rectangle of the shared VR render target. */
struct FWowUiVrZone
{
    EWowUiZone Zone = EWowUiZone::Hud;
    /** Sub-rectangle of GetRenderTarget() in 0..1 texture coordinates. */
    FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1));
    /** Size of that rectangle in UI units (Slate units at UI scale 1, ~1 dp). */
    FVector2D SizeDp = FVector2D::ZeroVector;
    /** Anything of the zone is shown right now (hide the quad when false). */
    bool bVisible = false;
};

/**
 * VR host for the whole interface. While the headset is active every root registered with
 * FWowUiSurfaces (FrameXML, native windows, popups, cursor ...) is moved out of the flat game
 * viewport into one off-screen virtual window, drawn into GetRenderTarget() with the same
 * layout as on the monitor. Zones are rectangles of that target; the 3D side (Claude-Perf's
 * presenter) puts each zone on its own quad. The mouse keeps working: pointer events are hit
 * tested against the virtual window with the same coordinates as the flat viewport.
 * It only takes the UI off the monitor when the headset is on AND the 3D presenter has said it
 * shows the zones (SetPresenterReady), so the UI is never lost while the presenter is missing.
 */
struct WOWUI_API FWowUiVrHost
{
    static bool IsActive();
    static UTextureRenderTarget2D* GetRenderTarget();
    static void GetZones(TArray<FWowUiVrZone>& OutZones);
    /** Render target pixels per UI pixel (default 1.0). Higher = sharper text, more GPU. */
    static void SetPixelScale(float Scale);
    /** Draw rate of the target (default 45 Hz). */
    static void SetDrawRate(float Hz);
    /** Hide or show the whole UI (Alt+Z) while in VR. */
    static void SetHidden(bool bHidden);
    /** Used for zone rectangles of FrameXML frames. Set by UWowUIManager. */
    static void SetFrameManager(FWowFrameManager* FrameManager);
    /** Forget the frame manager only if it is this one (another UI manager shutting down,
     *  e.g. the login screen's after the world UI took over, must not clear the live one). */
    static void ClearFrameManager(FWowFrameManager* FrameManager);
    /** The 3D presenter shows the zones in the headset now (Claude-Perf calls this). */
    static void SetPresenterReady(bool bReady);
    /** The presenter reports how visible it shows a zone (0..1, gaze fade). Clicks on a zone
     *  shown below 0.3 go to the world, not the invisible UI. Zones never reported count as shown. */
    static void SetZoneOpacity(EWowUiZone Zone, float Opacity);
    /** VR: the mouse is on UI the headset shows right now (false: it is over the world). */
    static bool IsPointerOverShownUi();
    /** Force on/off for testing without a headset (-vrui on the command line does the same). */
    static void SetForced(bool bForced);
    /** Viewport pixels (GetMousePosition) -> virtual-window absolute coordinates, where the UI
     *  widgets' cached geometry lives while the host is active. False when not active. */
    static bool ViewportPixelToWindow(const FVector2D& Pixel, FVector2D& OutWindowPos);
    /** The OS cursor in virtual-window absolute coordinates. False when not active. */
    static bool CursorToWindow(FVector2D& OutWindowPos);
    /** Gaze on a panel: the presenter (Claude-Perf) reports where the head ray meets a shown
     *  panel, in render-target pixels (= virtual-window coordinates), every frame; bValid false
     *  when the ray meets no panel. Used for VR dwell clicks on the UI. */
    static void SetGazePoint(bool bValid, const FVector2D& WindowPos);
    static bool GetGazePoint(FVector2D& OutWindowPos);
    /** The mouse pointer lies on a panel the headset shows now (anywhere on it, also between its
     *  buttons). Used to switch between the world crosshair and the panel mouse pointer. */
    static bool IsPointerOverShownPanel();
    /** Puts the cursor on a virtual-window point and clicks there with the left button, through
     *  the normal input path (so FrameXML and native UI both get it). */
    static bool ClickAtWindowPoint(const FVector2D& WindowPos);
    /** A button tagged "WowRepeat" (slider step buttons) lies under this virtual-window point:
     *  stare-to-click keeps clicking it while the gaze stays. */
    static bool IsRepeatButtonAt(const FVector2D& WindowPos);
    /** Centre of a zone's rectangle in virtual-window coordinates, if the zone is shown now. */
    static bool GetVisibleZoneCenter(EWowUiZone Zone, FVector2D& OutWindowPos);
    /** Puts the mouse pointer on a virtual-window point (no click). */
    static bool MoveCursorToWindowPoint(const FVector2D& WindowPos);
};
