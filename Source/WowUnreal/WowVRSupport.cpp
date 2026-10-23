#include "WowVRSupport.h"
#include "WowClientPreferences.h"
#include "WowVRComfort.h"
#include "Engine/Engine.h"
#include "IXRTrackingSystem.h"
#include "IHeadMountedDisplay.h"
#include "StereoRendering.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "RHI.h"
#include "RenderCore.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWindow.h"
#include "GenericPlatform/GenericWindow.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWowVR, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogWowWindow, Log, All);

namespace
{
bool GOptionSeen = false;
bool GOptionValue = false;
// Our own seat calibration: the first valid head pose after VR starts (or Recenter) is the
// seat. Runtimes differ in what "Local" space means and may not have tracking yet when VR
// switches on, so we never rely on the runtime's recenter alone.
bool GSeatValid = false;
FVector GSeatPosition = FVector::ZeroVector;
float GSeatYaw = 0.f;
bool GHotkeyWasDown = false;
TMap<FString, FString> GSavedCVars;

// A headset runtime that cannot create its swapchain (headset asleep, Virtual Desktop not
// streaming) makes the engine itself crash inside EnableStereo. This marker exists from the
// moment VR starts until the first tracked frame; if it is still there at the next launch,
// that start crashed, and VR is not started automatically again.
FString VRStartMarker() { return FPaths::ProjectSavedDir() / TEXT("VRStarting.flag"); }
bool GStartMarkerWritten = false;
double GVRStartTime = 0.0;

/**
 * Headset frames must arrive at the display rate (72-120 Hz); a frame that misses it halves
 * the rate or judders. While VR is on some monitor features are scaled back according to the
 * VR graphics level (FWowVRComfort). The look stays the monitor's: Stefan's r194 test showed
 * that switching Lumen GI / Lumen reflections / ray tracing off in the headset made far
 * mountains grey, rain puddles and the horizon black through the fog and drew dark pixel
 * outlines round plants - and "Performance" (no Lumen) was even slower than "Balanced".
 * So only things that do not change the picture's lighting are touched:
 *   0 Performance - Balanced plus medium shadows (the upscaler stays the monitor's).
 *   1 Balanced    - no ray-traced shadows (shadow maps), Lumen hit lighting from the surface
 *                   cache and half the GI probes, high shadows, no volumetric cloud shadow map
 *                   (0.9 ms per eye, and the same for both eyes). Default.
 *   2 Full        - the monitor settings.
 * Frame generation and motion blur are always off in the headset; depth of field follows the
 * VR comfort option. Monitor values come back when VR switches off; if the Options page
 * changes a value while VR is on, that becomes the value restored later.
 */
// (vr.PixelDensity is gone in UE 5.8: looking it up every half second only logged warnings.)
// Levels: 0 Performance, 1 Balanced, 2 Full, 3 Potato (stored numbers kept from r194).
// 3 Potato - Performance plus Unreal's own temporal upscaler (TAAU) from 60 % instead of FSR
//            and medium shadows, for weak PCs; FSR features are not available there.
struct FVRCVar { const TCHAR* Name; const TCHAR* Value[4]; }; // nullptr = leave as on the monitor
const FVRCVar GVRProfile[] = {
    { TEXT("r.FidelityFX.FI.Enabled"),                      { TEXT("0"),   TEXT("0"),   TEXT("0"),  TEXT("0") } },
    { TEXT("r.MotionBlurQuality"),                          { TEXT("0"),   TEXT("0"),   TEXT("0"),  TEXT("0") } },
    { TEXT("r.RayTracing.Shadows"),                         { TEXT("0"),   TEXT("0"),   nullptr,    TEXT("0") } },
    { TEXT("r.Lumen.HardwareRayTracing.LightingMode"),      { TEXT("0"),   TEXT("0"),   nullptr,    TEXT("0") } },
    { TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor"),   { TEXT("32"),  TEXT("32"),  nullptr,    TEXT("32") } },
    // Potato on the medium shadow group like Performance (r220): with the low group (0) it
    // showed the black horizon band through the fog that Performance no longer has.
    { TEXT("sg.ShadowQuality"),                             { TEXT("1"),   TEXT("2"),   nullptr,    TEXT("1") } },
    { TEXT("r.Shadow.DistanceScale"),                       { TEXT("0.6"), TEXT("0.8"), nullptr,    TEXT("0.5") } },
    { TEXT("r.VolumetricCloud.ShadowMap"),                  { TEXT("0"),   TEXT("0"),   nullptr,    TEXT("0") } },
    // r411 (Stefan: 25 fps in the headset): the Cinematic effects level traces the volumetric clouds at
    // full resolution (render target mode 3) - 5.5 ms per eye in the headset's GPU profile. Mode 2
    // traces a quarter and reconstructs over time (what every lower effects level uses).
    { TEXT("r.VolumetricRenderTarget.Mode"),                { TEXT("2"),   TEXT("2"),   nullptr,    TEXT("2") } },
    // r411: the Cinematic shadow level gives volumetric fog a 4-pixel grid (449x481x128 froxels per
    // eye, 3 ms each). 8 pixels (the Epic level) is a quarter of that and keeps the fog itself on.
    { TEXT("r.VolumetricFog.GridPixelSize"),                { TEXT("8"),   TEXT("8"),   nullptr,    TEXT("16") } },
    { TEXT("r.FidelityFX.FSR.Enabled"),                     { nullptr,     nullptr,     nullptr,    TEXT("0") } },
    { TEXT("r.AntiAliasingMethod"),                         { nullptr,     nullptr,     nullptr,    TEXT("2") } },
    { TEXT("r.TemporalAA.Upsampling"),                      { nullptr,     nullptr,     nullptr,    TEXT("1") } },
    { TEXT("r.ScreenPercentage"),                           { nullptr,     nullptr,     nullptr,    TEXT("60") } },
    // (Volumetric fog stays as on the monitor at every level: switching it off in the headset
    // drew a black band along the horizon behind the clouds and dark dotted outlines round
    // mountains and palms - Stefan saw them only on "Performance", gone on "Balanced".)
    // The upscaler is never swapped in the headset (except Potato): AMD's FSR features (and later ray/noise
    // regeneration on top of FSR) must keep working at every level. Its cost scales with the
    // headset's output resolution, which is set in the streaming app (Virtual Desktop).
};
int32 GAppliedLevel = -1;
bool GAppliedDoF = false;
double GNextProfileCheck = 0.0;

bool SameValue(const FString& A, const TCHAR* B)
{
    return A.IsNumeric() && FCString::IsNumeric(B) ? FMath::IsNearlyEqual(FCString::Atof(*A), FCString::Atof(B), 1e-4f) : A == B;
}

const TCHAR* DesiredValue(const FVRCVar& Entry, int32 Level)
{
    return Entry.Value[FMath::Clamp(Level, 0, 3)];
}

// Frame generation turned back on in the very frame the headset view ends crashed inside FSR
// frame interpolation (its per-view resources from the stereo frame). Restore it 2 s later.
FString GPendingFrameGeneration;
double GFrameGenerationRestoreTime = 0.0;
// FSR frame interpolation crashes when it is switched back on at any point after a headset
// session (Stefan 21:58: restored 2 s after VR off, crash in InterpolateFrame releasing its
// old resources). Once VR has been on, frame generation stays off until the game restarts.
bool GFrameGenerationRanInVR = false;
int32 GFrameGenerationNoticeSerial = 0;
FString GFrameGenerationNotice;
void SetFrameGenerationNotice(const TCHAR* Text)
{
    if (!FWowClientPreferences::IsFrameGenerationEnabled()) return; // only if the player uses it
    GFrameGenerationNotice = Text;
    ++GFrameGenerationNoticeSerial;
    UE_LOG(LogWowVR, Log, TEXT("Frame generation notice: %s"), Text);
}

void RestorePendingFrameGeneration()
{
    if (GPendingFrameGeneration.IsEmpty() || FPlatformTime::Seconds() < GFrameGenerationRestoreTime) return;
    if (GFrameGenerationRanInVR)
    {
        if (GPendingFrameGeneration != TEXT("0"))
            UE_LOG(LogWowVR, Warning, TEXT("Frame generation stays off until the game is restarted (it crashes when switched back on after VR)"));
        GPendingFrameGeneration.Reset();
        return;
    }
    if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FI.Enabled")))
        CVar->Set(*GPendingFrameGeneration, ECVF_SetByCode);
    GPendingFrameGeneration.Reset();
}

void ApplyVRRenderProfile(bool bOn)
{
    // Back into the headset before a delayed frame-generation restore ran: that value is
    // still the monitor's.
    if (bOn && !GPendingFrameGeneration.IsEmpty())
    {
        GSavedCVars.Add(TEXT("r.FidelityFX.FI.Enabled"), GPendingFrameGeneration);
        GPendingFrameGeneration.Reset();
    }
    const int32 Level = FWowVRComfort::GetGraphicsLevel();
    const bool bDoF = FWowVRComfort::GetDepthOfField();
    auto Restore = [](const TCHAR* Name)
    {
        IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
        if (const FString* Saved = GSavedCVars.Find(Name))
        {
            if (CVar && FCString::Strcmp(Name, TEXT("r.FidelityFX.FI.Enabled")) == 0)
            {
                GPendingFrameGeneration = *Saved;
                GFrameGenerationRestoreTime = FPlatformTime::Seconds() + 2.0;
            }
            else if (CVar) CVar->Set(**Saved, ECVF_SetByCode);
            GSavedCVars.Remove(Name);
        }
    };
    for (const FVRCVar& Entry : GVRProfile)
    {
        IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Entry.Name);
        if (!CVar) continue;
        const TCHAR* Wanted = bOn ? DesiredValue(Entry, Level) : nullptr;
        if (!Wanted) { Restore(Entry.Name); continue; }
        const FString Current = CVar->GetString();
        if (SameValue(Current, Wanted)) continue;
        // Not our value: the monitor setting (first time, or the Options page changed it).
        GSavedCVars.Add(Entry.Name, Current);
        CVar->Set(Wanted, ECVF_SetByCode);
    }
    // Depth of field: off in the headset unless the VR comfort option asks for it.
    if (bOn && !bDoF)
    {
        if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DepthOfFieldQuality")))
            if (!SameValue(CVar->GetString(), TEXT("0")))
            {
                GSavedCVars.Add(TEXT("r.DepthOfFieldQuality"), CVar->GetString());
                CVar->Set(TEXT("0"), ECVF_SetByCode);
            }
    }
    else Restore(TEXT("r.DepthOfFieldQuality"));
    if (!bOn) GSavedCVars.Reset();
    if (bOn && (Level != GAppliedLevel || bDoF != GAppliedDoF))
        UE_LOG(LogWowVR, Log, TEXT("VR graphics level %d (0 performance, 1 balanced, 2 full, 3 potato), depth of field %s"), Level, bDoF ? TEXT("on") : TEXT("off"));
    GAppliedLevel = bOn ? Level : -1;
    GAppliedDoF = bDoF;
}

/** Options applied while in VR: put the VR profile straight back on top (same frame). */
void ReapplyAfterOptions()
{
    if (FWowVRSupport::IsActive()) ApplyVRRenderProfile(true);
    else if (GFrameGenerationRanInVR)
        if (IConsoleVariable* FI = IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FI.Enabled")))
            if (FI->GetInt() != 0) FI->Set(0, ECVF_SetByCode);
}

/** While VR is on: follow a changed VR graphics level / DoF option and keep our values. */
void MaintainVRRenderProfile()
{
    // Frame timing in the headset, every 5 s: frame time vs GPU time shows whether the GPU
    // or the CPU is what keeps us under the display rate.
    static double LogStart = 0.0, WorstFrame = 0.0;
    static int32 Frames = 0;
    const double Delta = FApp::GetDeltaTime();
    if (LogStart == 0.0) LogStart = FPlatformTime::Seconds();
    ++Frames;
    WorstFrame = FMath::Max(WorstFrame, Delta);
    if (FPlatformTime::Seconds() - LogStart >= 5.0)
    {
        const double Elapsed = FPlatformTime::Seconds() - LogStart;
        UE_LOG(LogWowVR, Log, TEXT("VR frame: %.1f fps, worst %.1f ms, gpu %.1f ms, game %.1f ms, render %.1f ms, rhi %.1f ms, level %d"), Frames / Elapsed,
            WorstFrame * 1000.0, FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)), FPlatformTime::ToMilliseconds(GGameThreadTime),
            FPlatformTime::ToMilliseconds(GRenderThreadTime), FPlatformTime::ToMilliseconds(GRHIThreadTime), FWowVRComfort::GetGraphicsLevel());
        LogStart = FPlatformTime::Seconds();
        Frames = 0;
        WorstFrame = 0.0;
    }
    // Frame generation must never run in the headset, not even for the half second until the
    // next profile check (the Options page can switch it on while VR is up).
    if (IConsoleVariable* FI = IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FI.Enabled")))
        if (FI->GetInt() != 0) { GNextProfileCheck = 0.0; GFrameGenerationRanInVR = true; }
    const double Now = FPlatformTime::Seconds();
    if (Now < GNextProfileCheck) return;
    GNextProfileCheck = Now + .5;
    ApplyVRRenderProfile(true);
}

/** Ctrl+Shift+V (or Ctrl+Alt+V), read from the keyboard itself so it works whatever has focus. */
bool ToggleHotkeyPressed(APlayerController* PlayerController)
{
    bool bDown = false;
#if PLATFORM_WINDOWS
    DWORD ForegroundProcess = 0;
    if (HWND Foreground = ::GetForegroundWindow()) ::GetWindowThreadProcessId(Foreground, &ForegroundProcess);
    if (ForegroundProcess == ::GetCurrentProcessId())
        bDown = (::GetAsyncKeyState('V') & 0x8000) && (::GetAsyncKeyState(VK_CONTROL) & 0x8000)
            && ((::GetAsyncKeyState(VK_SHIFT) & 0x8000) || (::GetAsyncKeyState(VK_MENU) & 0x8000));
#else
    bDown = PlayerController && PlayerController->IsInputKeyDown(EKeys::V)
        && (PlayerController->IsInputKeyDown(EKeys::LeftControl) || PlayerController->IsInputKeyDown(EKeys::RightControl))
        && (PlayerController->IsInputKeyDown(EKeys::LeftShift) || PlayerController->IsInputKeyDown(EKeys::RightShift));
#endif
    const bool bPressed = bDown && !GHotkeyWasDown;
    GHotkeyWasDown = bDown;
    return bPressed;
}
}

bool FWowVRSupport::IsHeadsetAvailable()
{
    if (!GEngine || !GEngine->XRSystem.IsValid()) return false;
    IHeadMountedDisplay* HMD = GEngine->XRSystem->GetHMDDevice();
    return HMD && HMD->IsHMDConnected();
}

bool FWowVRSupport::IsActive()
{
    return GEngine && GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
}

namespace { FVector GSmoothedGaze = FVector::ZeroVector; }
FVector FWowVRSupport::GetSmoothedGazeDirection() { return IsActive() ? GSmoothedGaze : FVector::ZeroVector; }
void FWowVRSupport::SetSmoothedGazeDirection(const FVector& Direction) { GSmoothedGaze = Direction; }

int32 FWowVRSupport::GetFrameGenerationNoticeSerial() { return GFrameGenerationNoticeSerial; }
FString FWowVRSupport::GetFrameGenerationNotice() { return GFrameGenerationNotice; }
bool FWowVRSupport::IsFrameGenerationBlockedUntilRestart() { return GFrameGenerationRanInVR; }

FString FWowVRSupport::GetSystemName()
{
    return GEngine && GEngine->XRSystem.IsValid() ? GEngine->XRSystem->GetSystemName().ToString() : FString(TEXT("none"));
}

bool FWowVRSupport::SetActive(bool bActive)
{
    if (!GEngine || !GEngine->StereoRenderingDevice.IsValid())
    {
        if (bActive) UE_LOG(LogWowVR, Warning, TEXT("VR requested but no XR system is loaded (OpenXR runtime installed and headset on?)"));
        return false;
    }
    if (bActive && !IsHeadsetAvailable())
    {
        UE_LOG(LogWowVR, Warning, TEXT("VR requested but %s reports no connected headset"), *GetSystemName());
        return false;
    }
    if (GEngine->XRSystem.IsValid())
        GEngine->XRSystem->SetTrackingOrigin(EHMDTrackingOrigin::Local); // seated: origin at the head
    GSeatValid = false;
    if (bActive)
    {
        GStartMarkerWritten = FFileHelper::SaveStringToFile(TEXT("VR start in progress"), *VRStartMarker());
        GVRStartTime = FPlatformTime::Seconds();
        ApplyVRRenderProfile(true);
        // Stays registered after VR too: it keeps frame generation off for the session.
        FWowClientPreferences::SetRenderOverrideHook(&ReapplyAfterOptions);
    }
    GEngine->StereoRenderingDevice->EnableStereo(bActive);
    const bool bResult = IsActive();
    if (!bResult)
    {
        ApplyVRRenderProfile(false);
        if (GStartMarkerWritten) { IFileManager::Get().Delete(*VRStartMarker(), false, true, true); GStartMarkerWritten = false; }
    }
    if (bResult)
    {
        Recenter();
        GFrameGenerationRanInVR = true;
        SetFrameGenerationNotice(TEXT("Frame generation is turned off while VR is on (FSR upscaling stays on)."));
    }
    else if (!bActive && GFrameGenerationRanInVR)
        SetFrameGenerationNotice(TEXT("Frame generation cannot be turned back on after VR. Restart the game to use it again."));
    UE_LOG(LogWowVR, Log, TEXT("VR %s (requested %s, %s)"), bResult ? TEXT("on") : TEXT("off"), bActive ? TEXT("on") : TEXT("off"), *GetSystemName());
    return bResult;
}

void FWowVRSupport::Recenter()
{
    GSeatValid = false; // the next valid head pose becomes the seat
}

bool FWowVRSupport::GetHeadPose(FQuat& OutOrientation, FVector& OutPosition)
{
    OutOrientation = FQuat::Identity;
    OutPosition = FVector::ZeroVector;
    if (!IsActive() || !GEngine->XRSystem.IsValid()) return false;
    FQuat RawOrientation;
    FVector RawPosition;
    if (!GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, RawOrientation, RawPosition)
        || RawOrientation.ContainsNaN() || RawPosition.ContainsNaN() || (RawOrientation.IsIdentity() && RawPosition.IsNearlyZero()))
        return false; // no tracking yet
    // Several seconds of tracked frames: the headset swapchain exists, the start succeeded.
    if (GStartMarkerWritten && FPlatformTime::Seconds() - GVRStartTime > 5.0)
    {
        IFileManager::Get().Delete(*VRStartMarker(), false, true, true);
        GStartMarkerWritten = false;
    }
    if (!GSeatValid)
    {
        GSeatValid = true;
        GSeatPosition = RawPosition;
        GSeatYaw = RawOrientation.Rotator().Yaw;
        UE_LOG(LogWowVR, Log, TEXT("VR seat set at %s, yaw %.0f"), *RawPosition.ToString(), GSeatYaw);
    }
    const FQuat SeatInverse(FRotator(0.f, -GSeatYaw, 0.f));
    OutOrientation = SeatInverse * RawOrientation;
    OutPosition = SeatInverse.RotateVector(RawPosition - GSeatPosition);
    return true;
}

namespace
{
// Diagnostics (r221): at 23:04 on 9 Oct the game window vanished - gone from the taskbar and
// Alt+Tab - while the game kept running and logging normally, with nothing in the log about
// it. Twice a second the window's state is checked and every change is logged, so the next
// time shows when and how it went (hidden, minimized, moved off every screen, destroyed,
// screens added/removed by the headset streamer).
void MonitorGameWindow()
{
    static double NextCheck = 0.0;
    const double Now = FPlatformTime::Seconds();
    if (Now < NextCheck) return;
    NextCheck = Now + 0.5;
    static FString LastState;
    static bool bLastProblem = false;
    FString State;
    bool bProblem = false;
    const TSharedPtr<SWindow> Window = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
    if (!Window.IsValid())
    {
        State = TEXT("no game window");
        bProblem = true;
    }
    else
    {
        State = FString::Printf(TEXT("slate visible=%d minimized=%d maximized=%d mode=%d"),
            Window->IsVisible() ? 1 : 0, Window->IsWindowMinimized() ? 1 : 0, Window->IsWindowMaximized() ? 1 : 0, int32(Window->GetWindowMode()));
        bProblem = !Window->IsVisible();
#if PLATFORM_WINDOWS
        const TSharedPtr<FGenericWindow> Native = Window->GetNativeWindow();
        const HWND Hwnd = Native.IsValid() ? static_cast<HWND>(Native->GetOSWindowHandle()) : nullptr;
        if (!Hwnd || !::IsWindow(Hwnd))
        {
            State += TEXT(", native window GONE");
            bProblem = true;
        }
        else
        {
            RECT Rect = {};
            ::GetWindowRect(Hwnd, &Rect);
            const bool bVisible = ::IsWindowVisible(Hwnd) != 0;
            const bool bOnScreen = ::MonitorFromWindow(Hwnd, MONITOR_DEFAULTTONULL) != nullptr;
            const LONG_PTR ExStyle = ::GetWindowLongPtrW(Hwnd, GWL_EXSTYLE);
            State += FString::Printf(TEXT(", native visible=%d iconic=%d onScreen=%d rect=(%d,%d)-(%d,%d) owner=%d toolwindow=%d foreground=%d"),
                bVisible ? 1 : 0, ::IsIconic(Hwnd) ? 1 : 0, bOnScreen ? 1 : 0, int32(Rect.left), int32(Rect.top), int32(Rect.right), int32(Rect.bottom),
                ::GetWindow(Hwnd, GW_OWNER) != nullptr ? 1 : 0, (ExStyle & WS_EX_TOOLWINDOW) ? 1 : 0, ::GetForegroundWindow() == Hwnd ? 1 : 0);
            bProblem = bProblem || !bVisible || !bOnScreen;
        }
        State += FString::Printf(TEXT(", screens=%d desktop=(%d,%d %dx%d)"), ::GetSystemMetrics(SM_CMONITORS),
            ::GetSystemMetrics(SM_XVIRTUALSCREEN), ::GetSystemMetrics(SM_YVIRTUALSCREEN), ::GetSystemMetrics(SM_CXVIRTUALSCREEN), ::GetSystemMetrics(SM_CYVIRTUALSCREEN));
#endif
    }
    if (State == LastState) return;
    // (Braces: UE_LOG is a block macro, "if (x) UE_LOG(...); else" does not compile.)
    if (bProblem)
    {
        UE_LOG(LogWowWindow, Warning, TEXT("Game window: %s (VR %s)"), *State, FWowVRSupport::IsActive() ? TEXT("on") : TEXT("off"));
    }
    else
    {
        UE_LOG(LogWowWindow, Log, TEXT("Game window: %s (VR %s)"), *State, FWowVRSupport::IsActive() ? TEXT("on") : TEXT("off"));
    }
    if (bLastProblem && !bProblem)
    {
        UE_LOG(LogWowWindow, Log, TEXT("Game window is back"));
    }
    LastState = State;
    bLastProblem = bProblem;
}
}

void FWowVRSupport::SyncWithOption(APlayerController* PlayerController, bool bInGameWorld)
{
    MonitorGameWindow();
    if (!GOptionSeen)
    {
        GOptionSeen = true;
        // VR is never remembered between launches: the game always starts on the monitor and
        // the headset view is switched on by hand (Options checkbox or Ctrl+Shift+V).
        // Only an explicit -vr launch asks for it at the start.
        if (!FParse::Param(FCommandLine::Get(), TEXT("vr")))
            FWowClientPreferences::SetWorldOption(TEXT("vrmode"), false);
        if (IFileManager::Get().FileExists(*VRStartMarker()))
        {
            IFileManager::Get().Delete(*VRStartMarker(), false, true, true);
            if (FWowClientPreferences::GetWorldOption(TEXT("vrmode")))
            {
                FWowClientPreferences::SetWorldOption(TEXT("vrmode"), false);
                UE_LOG(LogWowVR, Warning, TEXT("The last VR start did not finish (headset not streaming?). VR stays off; Ctrl+Shift+V starts it."));
            }
        }
        // Launching with -vr asks for VR; it starts once the world is entered.
        if (FParse::Param(FCommandLine::Get(), TEXT("vr")))
            FWowClientPreferences::SetWorldOption(TEXT("vrmode"), true);
    }
    // Ctrl+Shift+V: the way out (and back in) while wearing the headset.
    if (ToggleHotkeyPressed(PlayerController))
    {
        UE_LOG(LogWowVR, Log, TEXT("VR hotkey"));
        FWowClientPreferences::SetWorldOption(TEXT("vrmode"), !FWowClientPreferences::GetWorldOption(TEXT("vrmode")));
    }
    const bool bWanted = bInGameWorld && FWowClientPreferences::GetWorldOption(TEXT("vrmode"));
    if (IsActive())
    {
        MaintainVRRenderProfile();
        // Diagnostics: one GPU profile per VR session, 40 s in, written to the log (no window),
        // so the headset's GPU time can be broken down by pass. Costs a single slow frame.
        static double ProfiledForStart = -1.0;
        if (ProfiledForStart != GVRStartTime && FPlatformTime::Seconds() - GVRStartTime > 40.0 && GEngine && PlayerController)
        {
            ProfiledForStart = GVRStartTime;
            if (IConsoleVariable* ShowUI = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ProfileGPU.ShowUI")))
                ShowUI->Set(0, ECVF_SetByCode);
            UE_LOG(LogWowVR, Log, TEXT("VR GPU profile follows (level %d)"), FWowVRComfort::GetGraphicsLevel());
            GEngine->Exec(PlayerController->GetWorld(), TEXT("ProfileGPU"));
        }
        // r411: the game thread took 30-45 ms a frame in the headset. A CSV profile (300 frames,
        // Saved\Profiling\CSV) 60 s in breaks the CPU time down so it can be read afterwards.
        static double CsvForStart = -1.0;
        if (CsvForStart != GVRStartTime && FPlatformTime::Seconds() - GVRStartTime > 60.0 && GEngine && PlayerController)
        {
            CsvForStart = GVRStartTime;
            UE_LOG(LogWowVR, Log, TEXT("VR CPU profile: csvprofile frames=300 (Saved/Profiling/CSV)"));
            GEngine->Exec(PlayerController->GetWorld(), TEXT("csvprofile frames=300"));
        }
    }
    else RestorePendingFrameGeneration();
    if (bWanted == GOptionValue && (bWanted || !IsActive())) return;
    GOptionValue = bWanted;
    SetActive(bWanted);
}
