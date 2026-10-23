#include "WowUiVrHost.h"
#include "WowFrameManager.h"
#include "WowUiSurfaces.h"
#include "WowUIManager.h"
#include "WowClientPreferences.h"
#include "Components/Widget.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/HittestGrid.h"
#include "Layout/WidgetPath.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/SViewport.h"
#include "Slate/WidgetRenderer.h"
#include "Containers/Ticker.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/Images/SImage.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowUiVr, Log, All);

namespace
{
/** Pointer index used inside the virtual window, so its hover state never mixes with the
 *  flat viewport's (the real cursor keeps index 0 there). Keyboard focus stays user 0. */
constexpr uint32 VrPointerIndex = 7;

struct FHostState
{
    bool bActive = false;
    bool bForced = false;
    bool bPresenterReady = false;
    TWeakPtr<SWidget> PendingFocus;
    bool bMirror = false;
    bool bHidden = false;
    float PixelScale = 1.5f; // the panels are seen from ~1 m: 1.0 made small text unreadable
    float DrawInterval = 1.f / 45.f;
    double NextDraw = 0.0;
    double LastDraw = 0.0;
    FVector2D ViewportSize = FVector2D::ZeroVector;
    TSharedPtr<SVirtualWindow> Window;
    TSharedPtr<SOverlay> Root;
    TUniquePtr<FWidgetRenderer> Renderer;
    TStrongObjectPtr<UTextureRenderTarget2D> Target;
    TSharedPtr<SWidget> DebugMirror;
    FWowFrameManager* FrameManager = nullptr;
    FTSTicker::FDelegateHandle Ticker;
    TSharedPtr<class FVrUiInput> Input;
    TMap<FKey, bool> ButtonOnUi;
    /** Zones as last handed to the presenter, and how visible the presenter shows each. */
    TArray<FWowUiVrZone> LastZones;
    TMap<uint8, float> ZoneOpacity;
    FVector2D LastPointer = FVector2D::ZeroVector;
    bool bGazeValid = false;
    FVector2D GazePoint = FVector2D::ZeroVector;
    double GazeTime = 0.0;
};
FHostState& State() { static FHostState Value; return Value; }

float ViewportDpiScale(const FVector2D& Size)
{
    const UUserInterfaceSettings* Settings = GetDefault<UUserInterfaceSettings>();
    return Settings ? Settings->GetDPIScaleBasedOnSize(FIntPoint(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y))) : 1.f;
}

/** Mouse position in virtual-window space (same as the flat viewport, times the pixel scale). */
bool ToWindowSpace(const FVector2D& ScreenPos, FVector2D& Out)
{
    if (!GEngine || !GEngine->GameViewport) return false;
    const TSharedPtr<SViewport> Viewport = GEngine->GameViewport->GetGameViewportWidget();
    if (!Viewport.IsValid()) return false;
    const FGeometry& Geometry = Viewport->GetCachedGeometry();
    // Local units of the viewport -> pixels -> render target pixels.
    const FVector2D Local = Geometry.AbsoluteToLocal(ScreenPos);
    const FVector2D LocalSize = Geometry.GetLocalSize();
    if (LocalSize.X <= 0 || LocalSize.Y <= 0) return false;
    Out = Local / LocalSize * State().ViewportSize * State().PixelScale;
    return true;
}

TArray<FWidgetAndPointer> HitPath(const FVector2D& WindowPos)
{
    FHostState& S = State();
    if (!S.Window.IsValid()) return {};
    TArray<FWidgetAndPointer> Path = S.Window->GetHittestGrid().GetBubblePath(WindowPos, 0.f, false, 0);
    const FVirtualPointerPosition Pointer(WindowPos, S.LastPointer);
    for (FWidgetAndPointer& Entry : Path) Entry.SetPointerPosition(Pointer);
    return Path;
}

/** Something of the UI (not just the transparent root) is under the pointer. */
/** The point (window space) lies on a panel the headset shows right now. UI under a faded
 *  panel, or outside every panel, is invisible: such a press belongs to the world. */
bool PointShown(const FVector2D& WindowPos)
{
    FHostState& S = State();
    if (!S.Target.IsValid()) return true;
    const FVector2D TargetSize(S.Target->SizeX, S.Target->SizeY);
    if (TargetSize.X <= 0 || TargetSize.Y <= 0) return true;
    const FVector2f UV(WindowPos / TargetSize);
    for (const FWowUiVrZone& Zone : S.LastZones)
    {
        if (!Zone.bVisible || Zone.Zone == EWowUiZone::Hud) continue;
        if (UV.X < Zone.UV.Min.X || UV.Y < Zone.UV.Min.Y || UV.X > Zone.UV.Max.X || UV.Y > Zone.UV.Max.Y) continue;
        const float* Opacity = S.ZoneOpacity.Find(static_cast<uint8>(Zone.Zone));
        if (!Opacity || *Opacity >= .3f) return true;
    }
    return false;
}

bool HitsUi(const TArray<FWidgetAndPointer>& Path)
{
    FHostState& S = State();
    for (int32 Index = Path.Num() - 1; Index >= 0; --Index)
    {
        const TSharedRef<SWidget>& Widget = Path[Index].Widget;
        if (Widget == S.Window || Widget == S.Root) continue;
        // Transparent full-screen layout roots (the objective tracker's overlay, panel
        // canvases) are not UI: a press there belongs to the world (right-drag camera turn).
        const FVector2D Size = Path[Index].Geometry.GetAbsoluteSize();
        const FVector2D WindowSize = S.ViewportSize * S.PixelScale;
        if (Size.X >= WindowSize.X * .9 && Size.Y >= WindowSize.Y * .9)
        {
            static const TSet<FName> Layout = {FName(TEXT("SOverlay")), FName(TEXT("SConstraintCanvas")), FName(TEXT("SCanvas")),
                FName(TEXT("SBox")), FName(TEXT("SVerticalBox")), FName(TEXT("SHorizontalBox")), FName(TEXT("SDPIScaler")),
                FName(TEXT("SWeakWidget")), FName(TEXT("SPanel")), FName(TEXT("SObjectWidget"))};
            if (Layout.Contains(Widget->GetType())) continue;
        }
        static double NextLog = 0.0;
        if (FPlatformTime::Seconds() >= NextLog)
        {
            NextLog = FPlatformTime::Seconds() + 2.0;
            UE_LOG(LogWowUiVr, Log, TEXT("VR UI press on %s (%s) %.0fx%.0f"), *Widget->GetType().ToString(),
                *Widget->GetReadableLocation(), Size.X, Size.Y);
        }
        return true;
    }
    return false;
}

FPointerEvent RetargetEvent(const FPointerEvent& Event, const FVector2D& WindowPos)
{
    return FPointerEvent(Event.GetUserIndex(), VrPointerIndex, WindowPos, State().LastPointer,
        Event.GetPressedButtons(), Event.GetEffectingButton(), Event.GetWheelDelta(), Event.GetModifierKeys());
}

class FVrUiInput : public IInputProcessor
{
public:
    virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

    virtual bool HandleMouseMoveEvent(FSlateApplication& App, const FPointerEvent& Event) override
    {
        FVector2D Pos;
        if (!State().bActive || State().bHidden || !ToWindowSpace(Event.GetScreenSpacePosition(), Pos)) return false;
        TArray<FWidgetAndPointer> Path = HitPath(Pos);
        App.RoutePointerMoveEvent(FWidgetPath(Path), RetargetEvent(Event, Pos), false);
        State().LastPointer = Pos;
        // A drag that started on the UI (scroll bars, frame moves) stays with the UI.
        return App.HasUserMouseCapture(Event.GetUserIndex()) && HasAnyButtonOnUi();
    }
    virtual bool HandleMouseButtonDownEvent(FSlateApplication& App, const FPointerEvent& Event) override
    {
        FVector2D Pos;
        if (!State().bActive || State().bHidden || !ToWindowSpace(Event.GetScreenSpacePosition(), Pos)) return false;
        TArray<FWidgetAndPointer> Path = HitPath(Pos);
        if (!HitsUi(Path) || !PointShown(Pos)) return false; // the world gets the click (targeting, camera)
        State().ButtonOnUi.Add(Event.GetEffectingButton(), true);
        App.RoutePointerDownEvent(FWidgetPath(Path), RetargetEvent(Event, Pos));
        return true;
    }
    virtual bool HandleMouseButtonUpEvent(FSlateApplication& App, const FPointerEvent& Event) override
    {
        FVector2D Pos;
        if (!State().bActive || !ToWindowSpace(Event.GetScreenSpacePosition(), Pos)) return false;
        bool bWasOnUi = false;
        State().ButtonOnUi.RemoveAndCopyValue(Event.GetEffectingButton(), bWasOnUi);
        if (!bWasOnUi) return false;
        TArray<FWidgetAndPointer> Path = HitPath(Pos);
        App.RoutePointerUpEvent(FWidgetPath(Path), RetargetEvent(Event, Pos));
        return true;
    }
    virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& App, const FPointerEvent& Event) override
    {
        FVector2D Pos;
        if (!State().bActive || State().bHidden || !ToWindowSpace(Event.GetScreenSpacePosition(), Pos)) return false;
        TArray<FWidgetAndPointer> Path = HitPath(Pos);
        if (!HitsUi(Path) || !PointShown(Pos)) return false;
        State().ButtonOnUi.Add(Event.GetEffectingButton(), true);
        App.RoutePointerDoubleClickEvent(FWidgetPath(Path), RetargetEvent(Event, Pos));
        return true;
    }
    virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& App, const FPointerEvent& Event, const FPointerEvent*) override
    {
        FVector2D Pos;
        if (!State().bActive || State().bHidden || !ToWindowSpace(Event.GetScreenSpacePosition(), Pos)) return false;
        TArray<FWidgetAndPointer> Path = HitPath(Pos);
        if (!HitsUi(Path) || !PointShown(Pos)) return false; // camera zoom
        const FReply Reply = App.RouteMouseWheelOrGestureEvent(FWidgetPath(Path), RetargetEvent(Event, Pos), nullptr);
        return Reply.IsEventHandled();
    }
    virtual const TCHAR* GetDebugName() const override { return TEXT("WowUiVrInput"); }

private:
    static bool HasAnyButtonOnUi()
    {
        for (const auto& Pair : State().ButtonOnUi) if (Pair.Value) return true;
        return false;
    }
};

bool WantActive()
{
    FHostState& S = State();
    if (S.bForced || FParse::Param(FCommandLine::Get(), TEXT("vrui"))) return true;
    // Never take the UI off the monitor unless something shows it in the headset.
    return S.bPresenterReady && GEngine && GEngine->IsStereoscopic3D();
}

void UpdateViewportSize()
{
    FHostState& S = State();
    FVector2D Size = FVector2D::ZeroVector;
    if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Size);
    if (Size.X < 64 || Size.Y < 64) Size = FVector2D(1920, 1080);
    S.ViewportSize = Size;
}

void Activate()
{
    FHostState& S = State();
    if (S.bActive || !FSlateApplication::IsInitialized()) return;
    UpdateViewportSize();
    const FVector2D DrawSize = S.ViewportSize * S.PixelScale;
    S.Root = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible);
    S.Window = SNew(SVirtualWindow).Size(DrawSize);
    S.Window->SetContent(S.Root.ToSharedRef());
    FSlateApplication::Get().RegisterVirtualWindow(S.Window.ToSharedRef());
    S.Renderer = MakeUnique<FWidgetRenderer>(true, true);
    S.Target.Reset(FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, true));
    if (S.Target.IsValid()) S.Target->ClearColor = FLinearColor::Transparent;
    S.Input = MakeShared<FVrUiInput>();
    FSlateApplication::Get().RegisterInputPreProcessor(S.Input, 0);
    S.bActive = true;
    S.PendingFocus = FSlateApplication::Get().GetUserFocusedWidget(0);

    // Every root moves from the viewport into the virtual window, in its z order.
    FWowUiSurfaces::SetPresenter([](const FWowUiSurfaces::FEntry& Entry, bool bAdd)
    {
        FHostState& Host = State();
        const TSharedPtr<SWidget> Widget = Entry.Widget.Pin();
        if (!Host.Root.IsValid() || !Widget.IsValid()) return false;
        // r247 (Stefan 12.10: pieces of the CTRL+R performance overlay - "AMD FSR ... / Frame
        // Generation" - on the action-bar panel): no headset panel shows the Debug zone, but in the
        // shared texture it lay over the action bars. It stays on the monitor instead.
        if (Entry.Zone == EWowUiZone::Debug) return false;
        if (bAdd) Host.Root->AddSlot(Entry.ZOrder)[Widget.ToSharedRef()];
        else Host.Root->RemoveSlot(Widget.ToSharedRef());
        return true;
    });

    // -vrui test: show the target on the monitor (full size) so the path can be checked.
    S.bMirror = S.bForced || FParse::Param(FCommandLine::Get(), TEXT("vrui"));
    if (S.bMirror && GEngine && GEngine->GameViewport && S.Target.IsValid())
    {
        static FSlateBrush MirrorBrush;
        MirrorBrush.SetResourceObject(S.Target.Get());
        MirrorBrush.ImageSize = DrawSize;
        MirrorBrush.DrawAs = ESlateBrushDrawType::Image;
        S.DebugMirror = SNew(SImage).Image(&MirrorBrush).Visibility(EVisibility::HitTestInvisible);
        GEngine->GameViewport->AddViewportWidgetContent(S.DebugMirror.ToSharedRef(), 0);
    }
    UE_LOG(LogWowUiVr, Log, TEXT("VR UI host on: %.0fx%.0f, pixel scale %.2f"), DrawSize.X, DrawSize.Y, S.PixelScale);
    // The headset has its own interface preset (UI scale, frame positions): rebuild the world
    // interface in it, so nothing tuned for VR leaks into the monitor layout or back.
    FWowClientPreferences::SetVrUiMode(true);
    if (S.FrameManager) UWowUIManager::RequestReloadUI();
}

void Deactivate()
{
    FHostState& S = State();
    if (!S.bActive) return;
    S.bActive = false;
    FWowUiSurfaces::SetPresenter(nullptr); // everything back to the flat viewport
    if (S.DebugMirror.IsValid() && GEngine && GEngine->GameViewport)
        GEngine->GameViewport->RemoveViewportWidgetContent(S.DebugMirror.ToSharedRef());
    S.DebugMirror.Reset();
    if (FSlateApplication::IsInitialized())
    {
        if (S.Input.IsValid()) FSlateApplication::Get().UnregisterInputPreProcessor(S.Input);
        if (S.Window.IsValid()) FSlateApplication::Get().UnregisterVirtualWindow(S.Window.ToSharedRef());
    }
    S.Input.Reset();
    S.Window.Reset();
    S.Root.Reset();
    S.Renderer.Reset();
    S.Target.Reset();
    S.ButtonOnUi.Reset();
    UE_LOG(LogWowUiVr, Log, TEXT("VR UI host off"));
    FWowClientPreferences::SetVrUiMode(false);
    if (S.FrameManager) UWowUIManager::RequestReloadUI();
}

void Draw(double Now)
{
    FHostState& S = State();
    if (!S.Window.IsValid() || !S.Renderer.IsValid() || !S.Target.IsValid()) return;
    const FVector2D OldSize = S.ViewportSize;
    UpdateViewportSize();
    const FVector2D DrawSize = S.ViewportSize * S.PixelScale;
    if (OldSize != S.ViewportSize || S.Target->SizeX != FMath::RoundToInt(DrawSize.X) || S.Target->SizeY != FMath::RoundToInt(DrawSize.Y))
    {
        S.Window->Resize(DrawSize);
        S.Target->ResizeTarget(FMath::RoundToInt(DrawSize.X), FMath::RoundToInt(DrawSize.Y));
    }
    S.Root->SetVisibility(S.bHidden ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
    const float DeltaTime = S.LastDraw > 0 ? float(FMath::Min(Now - S.LastDraw, 0.25)) : S.DrawInterval;
    S.LastDraw = Now;
    // Same DPI scale as the flat viewport, so every layout matches the monitor.
    const float Scale = ViewportDpiScale(S.ViewportSize) * S.PixelScale;
    S.Renderer->DrawWindow(S.Target.Get(), S.Window->GetHittestGrid(), S.Window.ToSharedRef(), Scale, DrawSize, DeltaTime);
    // Keyboard focus (login fields, Esc handlers) follows the widgets into the virtual window.
    if (const TSharedPtr<SWidget> Focus = S.PendingFocus.Pin())
    {
        FSlateApplication::Get().SetUserFocus(0, Focus, EFocusCause::SetDirectly);
        S.PendingFocus.Reset();
    }
}

bool TickHost(float)
{
    FHostState& S = State();
    const bool bWant = WantActive();
    if (bWant && !S.bActive) Activate();
    else if (!bWant && S.bActive) Deactivate();
    if (!S.bActive) return true;
    const double Now = FPlatformTime::Seconds();
    if (Now >= S.NextDraw)
    {
        S.NextDraw = Now + S.DrawInterval;
        Draw(Now);
    }
    return true;
}

void EnsureTicker()
{
    FHostState& S = State();
    if (!S.Ticker.IsValid())
        S.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickHost));
}

/** Union of the live geometry of named FrameXML frames, in target pixels. */
bool FrameUnion(const TArray<FString>& Names, FBox2D& OutBox, FString* Diag = nullptr)
{
    FHostState& S = State();
    if (!S.FrameManager || !S.Window.IsValid()) { if (Diag) *Diag += TEXT(" no-frame-manager"); return false; }
    OutBox = FBox2D(ForceInit);
    for (const FString& Name : Names)
    {
        const int64 Handle = S.FrameManager->FindFrame(Name);
        if (Handle < 0) continue; // not created (addon bars, other expansions)
        if (!S.FrameManager->IsFrameVisible(Handle)) { if (Diag) *Diag += FString::Printf(TEXT(" %s:hidden"), *Name); continue; }
        UWidget* Widget = S.FrameManager->GetWidgetForHandle(Handle);
        if (!Widget || !Widget->IsVisible()) { if (Diag) *Diag += FString::Printf(TEXT(" %s:%s"), *Name, Widget ? TEXT("umg-hidden") : TEXT("no-widget")); continue; }
        // The frame's laid-out rect in UIParent units (what frame:GetLeft/GetTop report), mapped
        // onto the target: independent of Slate's cached geometry, which for the FrameXML canvas
        // can still hold the flat viewport's desktop coordinates.
        FVector2D Pos, Size;
        float L = 0, B = 0, R = 0, T = 0, PL = 0, PB = 0, PR = 0, PT = 0;
        const int64 ParentHandle = S.FrameManager->FindFrame(TEXT("UIParent"));
        if (ParentHandle >= 0 && S.FrameManager->GetFrameScreenRect(Handle, L, B, R, T)
            && S.FrameManager->GetFrameScreenRect(ParentHandle, PL, PB, PR, PT) && PR - PL > 1 && PT - PB > 1)
        {
            const FVector2D Target = S.ViewportSize * S.PixelScale;
            const FVector2D PerUnit(Target.X / (PR - PL), Target.Y / (PT - PB));
            Pos = FVector2D((L - PL) * PerUnit.X, (PT - T) * PerUnit.Y);
            Size = FVector2D((R - L) * PerUnit.X, (T - B) * PerUnit.Y);
        }
        else
        {
            const FGeometry& Geometry = Widget->GetCachedGeometry();
            Size = Geometry.GetAbsoluteSize();
            Pos = Geometry.GetAbsolutePosition();
        }
        // The drawn rectangle is what the panel must show: the laid-out one ignores render scales
        // (main bar 0.9, unit frames 0.7), lags frames that grow after Show (GameTooltip) and can
        // be far off (ChatFrameMenuButton laid out at y -266 stretched the chat panel over the
        // player frame - a second player frame in the headset). Use it whenever it is a valid
        // rectangle inside the target; the laid-out rect only when nothing was drawn yet.
        {
            const FGeometry& Drawn = Widget->GetCachedGeometry();
            const FVector2D DPos = Drawn.GetAbsolutePosition(), DSize = Drawn.GetAbsoluteSize();
            const FVector2D Target = S.ViewportSize * S.PixelScale;
            if (DSize.X >= 1 && DSize.Y >= 1 && DPos.X >= -1 && DPos.Y >= -1 && DPos.X + DSize.X <= Target.X + 1 && DPos.Y + DSize.Y <= Target.Y + 1)
            {
                Pos = DPos;
                Size = DSize;
            }
            else if (Pos.X + Size.X < 0 || Pos.Y + Size.Y < 0 || Pos.X > Target.X || Pos.Y > Target.Y)
            {
                if (Diag) *Diag += FString::Printf(TEXT(" %s:offscreen"), *Name);
                continue; // laid out off the target and not drawn: nothing to show
            }
        }
        if (Diag)
        {
            const FGeometry& Geometry = Widget->GetCachedGeometry();
            *Diag += FString::Printf(TEXT(" %s:(%.0f,%.0f %.0fx%.0f; slate %.0f,%.0f %.0fx%.0f)"), *Name, Pos.X, Pos.Y, Size.X, Size.Y,
                Geometry.GetAbsolutePosition().X, Geometry.GetAbsolutePosition().Y, Geometry.GetAbsoluteSize().X, Geometry.GetAbsoluteSize().Y);
        }
        if (Size.X < 1 || Size.Y < 1) continue;
        OutBox += Pos;
        OutBox += Pos + Size;
    }
    return OutBox.bIsValid;
}

/** Visible content of a registered root: full-screen layout containers are looked through
 *  (recursively) so only the real window/popup rectangles count, in target pixels. */
void ContentBox(const TSharedRef<SWidget>& Widget, const FVector2D& WindowSize, int32 Depth, FBox2D& OutBox)
{
    if (!Widget->GetVisibility().IsVisible()) return;
    const FGeometry& Geometry = Widget->GetTickSpaceGeometry();
    const FVector2D Size = Geometry.GetAbsoluteSize();
    if (Size.X < 1 || Size.Y < 1) return;
    const bool bFullScreen = Size.X >= WindowSize.X * .85 && Size.Y >= WindowSize.Y * .85;
    FChildren* Children = bFullScreen && Depth < 8 ? Widget->GetChildren() : nullptr;
    if (Children && Children->Num() > 0)
    {
        for (int32 Index = 0; Index < Children->Num(); ++Index)
            ContentBox(Children->GetChildAt(Index), WindowSize, Depth + 1, OutBox);
        return;
    }
    if (bFullScreen && !Children) { OutBox += FVector2D::ZeroVector; OutBox += WindowSize; return; }
    if (bFullScreen) return; // empty full-screen container
    const FVector2D Pos = Geometry.GetAbsolutePosition();
    OutBox += Pos;
    OutBox += Pos + Size;
    // Children drawn outside their window's box (the game menu's title plate sits above it) were
    // cut off: take in every visible child's rectangle too.
    if (Depth < 5) // a few levels: the whole widget tree every frame would cost too much
        if (FChildren* Inner = Widget->GetChildren())
            for (int32 Index = 0; Index < Inner->Num(); ++Index)
            {
                const TSharedRef<SWidget> Child = Inner->GetChildAt(Index);
                if (!Child->GetVisibility().IsVisible()) continue;
                const FGeometry& ChildGeometry = Child->GetTickSpaceGeometry();
                const FVector2D ChildSize = ChildGeometry.GetAbsoluteSize();
                if (ChildSize.X < 1 || ChildSize.Y < 1 || ChildSize.X >= WindowSize.X * .85 || ChildSize.Y >= WindowSize.Y * .85) continue;
                ContentBox(Child, WindowSize, Depth + 1, OutBox);
            }
}

/** Union of the visible content of every registered root in one of the zones. */
bool SurfaceUnion(const TArray<EWowUiZone>& InZones, const FVector2D& WindowSize, FBox2D& OutBox)
{
    OutBox = FBox2D(ForceInit);
    for (const FWowUiSurfaces::FEntry& Entry : FWowUiSurfaces::Entries())
    {
        if (!InZones.Contains(Entry.Zone)) continue;
        if (const TSharedPtr<SWidget> Widget = Entry.Widget.Pin()) ContentBox(Widget.ToSharedRef(), WindowSize, 0, OutBox);
    }
    return OutBox.bIsValid;
}
}

bool FWowUiVrHost::IsActive()
{
    EnsureTicker();
    return State().bActive;
}

UTextureRenderTarget2D* FWowUiVrHost::GetRenderTarget()
{
    EnsureTicker();
    return State().Target.Get();
}

void FWowUiVrHost::SetPixelScale(float Scale)
{
    State().PixelScale = FMath::Clamp(Scale, .5f, 3.f);
}

void FWowUiVrHost::SetDrawRate(float Hz)
{
    State().DrawInterval = 1.f / FMath::Clamp(Hz, 5.f, 144.f);
}

void FWowUiVrHost::SetHidden(bool bHidden)
{
    State().bHidden = bHidden;
}

void FWowUiVrHost::SetFrameManager(FWowFrameManager* FrameManager)
{
    State().FrameManager = FrameManager;
    EnsureTicker();
}

void FWowUiVrHost::ClearFrameManager(FWowFrameManager* FrameManager)
{
    FHostState& S = State();
    if (S.FrameManager == FrameManager) S.FrameManager = nullptr;
    else UE_LOG(LogWowUiVr, Log, TEXT("VR UI host: kept the live frame manager (another UI manager shut down)"));
}

void FWowUiVrHost::SetPresenterReady(bool bReady)
{
    State().bPresenterReady = bReady;
    EnsureTicker();
}

void FWowUiVrHost::SetForced(bool bForced)
{
    State().bForced = bForced;
    EnsureTicker();
}

void FWowUiVrHost::GetZones(TArray<FWowUiVrZone>& OutZones)
{
    OutZones.Reset();
    FHostState& S = State();
    if (!S.bActive || !S.Target.IsValid()) return;
    const FVector2D TargetSize(S.Target->SizeX, S.Target->SizeY);
    const float DpPerPixel = 1.f / FMath::Max(.01f, ViewportDpiScale(S.ViewportSize) * S.PixelScale);
    auto Add = [&](EWowUiZone Zone, FBox2D Box, bool bVisible)
    {
        Box.Min = FVector2D(FMath::Clamp(Box.Min.X, 0., TargetSize.X), FMath::Clamp(Box.Min.Y, 0., TargetSize.Y));
        Box.Max = FVector2D(FMath::Clamp(Box.Max.X, 0., TargetSize.X), FMath::Clamp(Box.Max.Y, 0., TargetSize.Y));
        FWowUiVrZone& Out = OutZones.AddDefaulted_GetRef();
        Out.Zone = Zone;
        Out.UV = FBox2f(FVector2f(Box.Min / TargetSize), FVector2f(Box.Max / TargetSize));
        Out.SizeDp = (Box.Max - Box.Min) * DpPerPixel;
        Out.bVisible = bVisible && !S.bHidden && Out.SizeDp.X > 1 && Out.SizeDp.Y > 1;
    };
    // Default WotLK frames per zone, plus the native roots registered in that zone; the live
    // geometry follows moved frames and addons. Each zone is exactly what is open, so no part of
    // the screen is shown on two panels (a big fixed "windows" rectangle doubled the gossip
    // window and cut its left side).
    struct FZoneFrames { EWowUiZone Zone; TArray<FString> Frames; TArray<EWowUiZone> Native; float Magnify = 1.f; };
    TArray<FZoneFrames> Zones = {
        { EWowUiZone::UnitFrames, { TEXT("PlayerFrame"), TEXT("TargetFrame"), TEXT("TargetFrameToT"), TEXT("FocusFrame"), TEXT("PetFrame"),
            TEXT("PartyMemberFrame1"), TEXT("PartyMemberFrame2"), TEXT("PartyMemberFrame3"), TEXT("PartyMemberFrame4") }, { EWowUiZone::UnitFrames }, 2.f },
        { EWowUiZone::ActionBars, { TEXT("MainMenuBar"), TEXT("MultiBarBottomLeft"), TEXT("MultiBarBottomRight"), TEXT("ShapeshiftBarFrame"),
            TEXT("PetActionBarFrame"), TEXT("PossessBarFrame"), TEXT("VehicleMenuBar"), TEXT("MultiCastActionBarFrame"),
            TEXT("BT4Bar1"), TEXT("BT4Bar2"), TEXT("BT4Bar3"), TEXT("BT4Bar4"), TEXT("BT4Bar5"), TEXT("BT4Bar6"), TEXT("BT4BarStanceBar"),
            TEXT("BT4BarPetBar"), TEXT("BT4BarMicroMenu"), TEXT("BT4BarBagBar") }, { EWowUiZone::ActionBars }, 2.f },
        { EWowUiZone::Chat, { TEXT("ChatFrame1"), TEXT("ChatFrame1Tab"), TEXT("ChatFrame1EditBox"), TEXT("ChatFrame2"), TEXT("ChatFrameMenuButton") }, { EWowUiZone::Chat }, 2.f },
        { EWowUiZone::Minimap, { TEXT("MinimapCluster") }, { EWowUiZone::Minimap }, 2.f },
        { EWowUiZone::Auras, { TEXT("BuffFrame"), TEXT("TemporaryEnchantFrame") }, { EWowUiZone::Auras }, 2.f },
        // Stefan (09.10): every panel 2x in the headset. The tracker is the native one only (the
        // FrameXML WatchFrame rect reached up into the minimap's clock).
        { EWowUiZone::Tracker, {}, { EWowUiZone::Tracker }, 2.f },
        { EWowUiZone::CastBar, { TEXT("CastingBarFrame") }, { EWowUiZone::CastBar }, 2.f },
        // The two vertical bars on the right edge: their own panel (in the action-bar rect they
        // would stretch it across the whole screen).
        { EWowUiZone::SideBars, { TEXT("MultiBarRight"), TEXT("MultiBarLeft") }, {}, 2.f },
        // Windows and popups share one panel straight ahead (two centred panels showed a
        // window twice when both rectangles held it).
        { EWowUiZone::Windows, { TEXT("GossipFrame"), TEXT("QuestFrame"), TEXT("QuestLogFrame"), TEXT("CharacterFrame"), TEXT("SpellBookFrame"),
            TEXT("PlayerTalentFrame"), TEXT("FriendsFrame"), TEXT("MerchantFrame"), TEXT("TaxiFrame"), TEXT("TradeFrame"), TEXT("LootFrame"),
            TEXT("MailFrame"), TEXT("OpenMailFrame"), TEXT("BankFrame"), TEXT("ClassTrainerFrame"), TEXT("TradeSkillFrame"), TEXT("WorldMapFrame"),
            TEXT("GameMenuFrame"), TEXT("InterfaceOptionsFrame"), TEXT("VideoOptionsFrame"), TEXT("AudioOptionsFrame"), TEXT("KeyBindingFrame"),
            TEXT("MacroFrame"), TEXT("AchievementFrame"), TEXT("PVPFrame"), TEXT("LFDParentFrame"), TEXT("HelpFrame"), TEXT("GuildRegistrarFrame"),
            TEXT("PetitionFrame"), TEXT("TabardFrame"), TEXT("ItemTextFrame"), TEXT("InspectFrame"), TEXT("StaticPopup1"), TEXT("StaticPopup2"),
            TEXT("StaticPopup3"), TEXT("StaticPopup4"), TEXT("ContainerFrame1"), TEXT("ContainerFrame2"), TEXT("ContainerFrame3"),
            TEXT("ContainerFrame4"), TEXT("ContainerFrame5"), TEXT("ContainerFrame6"), TEXT("ContainerFrame7"), TEXT("ContainerFrame8"),
            TEXT("ContainerFrame9"), TEXT("ContainerFrame10"), TEXT("ContainerFrame11"), TEXT("ContainerFrame12"), TEXT("ContainerFrame13") },
            { EWowUiZone::Windows, EWowUiZone::Popups }, 2.f }, // Stefan: 2x so windows are readable in the headset
        // Error and warning text ("Inventory is full.", raid warnings): their own strip above the
        // view (the zone is called Popups; native popups stay on the windows panel).
        { EWowUiZone::Popups, { TEXT("UIErrorsFrame"), TEXT("RaidWarningFrame"), TEXT("RaidBossEmoteFrame") }, {}, 1.6f },
        { EWowUiZone::Tooltip, { TEXT("GameTooltip"), TEXT("ItemRefTooltip"), TEXT("ShoppingTooltip1"), TEXT("ShoppingTooltip2") }, { EWowUiZone::Tooltip }, 1.6f },
    };
    // A window's tabs hang below or beside it (CharacterFrameTab1-3, SpellBookSkillLineTab1-8):
    // the windows panel takes them in, or they showed up on whichever panel lay under them.
    if (S.FrameManager)
        for (FZoneFrames& Entry : Zones)
        {
            if (Entry.Zone != EWowUiZone::Windows) continue;
            const TArray<FString> Base = Entry.Frames;
            for (const FString& Name : Base)
            {
                const int64 Handle = S.FrameManager->FindFrame(Name);
                if (Handle < 0 || !S.FrameManager->IsFrameVisible(Handle)) continue;
                for (int32 Index = 1; Index <= 8; ++Index)
                    for (const FString& Tab : { FString::Printf(TEXT("%sTab%d"), *Name, Index), FString::Printf(TEXT("%sTabButton%d"), *Name, Index) })
                    {
                        const int64 TabHandle = S.FrameManager->FindFrame(Tab);
                        if (TabHandle >= 0 && S.FrameManager->IsFrameVisible(TabHandle)) Entry.Frames.Add(Tab);
                    }
                if (Name == TEXT("SpellBookFrame"))
                    for (int32 Index = 1; Index <= 8; ++Index)
                    {
                        const FString Tab = FString::Printf(TEXT("SpellBookSkillLineTab%d"), Index);
                        const int64 TabHandle = S.FrameManager->FindFrame(Tab);
                        if (TabHandle >= 0 && S.FrameManager->IsFrameVisible(TabHandle)) Entry.Frames.Add(Tab);
                    }
            }
        }
    FBox2D WindowsBox(ForceInit);
    // Diagnostics every 10 s while the headset UI is on: what each zone found.
    static double NextZoneLog = 0.0;
    const bool bLogZones = FPlatformTime::Seconds() >= NextZoneLog;
    if (bLogZones) NextZoneLog = FPlatformTime::Seconds() + 10.0;
    for (const FZoneFrames& Entry : Zones)
    {
        FBox2D Box(ForceInit), Native(ForceInit);
        FString Diag;
        if (FrameUnion(Entry.Frames, Box, bLogZones ? &Diag : nullptr) == false) Box = FBox2D(ForceInit);
        // The main bar's gryphon end caps hang outside every bar frame (Stefan 09.10: the action
        // bar panel cut off its ends): add their drawn rectangles.
        if (Entry.Zone == EWowUiZone::ActionBars && Box.bIsValid && S.FrameManager)
            for (const TCHAR* Cap : { TEXT("MainMenuBarLeftEndCap"), TEXT("MainMenuBarRightEndCap") })
                if (UImage* Image = S.FrameManager->GetNamedTexture(Cap); Image && Image->IsVisible() && Image->GetRenderOpacity() > .01f)
                {
                    const FGeometry& Drawn = Image->GetCachedGeometry();
                    const FVector2D P = Drawn.GetAbsolutePosition(), Sz = Drawn.GetAbsoluteSize();
                    if (Sz.X >= 1 && Sz.Y >= 1 && P.X >= -1 && P.Y >= -1 && P.X + Sz.X <= TargetSize.X + 1 && P.Y + Sz.Y <= TargetSize.Y + 1)
                    { Box += P; Box += P + Sz; }
                }
        const bool bNative = SurfaceUnion(Entry.Native, TargetSize, Native);
        if (bNative) Box += Native;
        if (bLogZones && Entry.Zone != EWowUiZone::Tooltip)
            UE_LOG(LogWowUiVr, Log, TEXT("VR zone %d: %s native=%s%s |%s"), int32(Entry.Zone),
                Box.bIsValid ? *FString::Printf(TEXT("(%.0f,%.0f)-(%.0f,%.0f)"), Box.Min.X, Box.Min.Y, Box.Max.X, Box.Max.Y) : TEXT("none"),
                bNative ? TEXT("yes") : TEXT("no"),
                bNative ? *FString::Printf(TEXT(" (%.0f,%.0f)-(%.0f,%.0f)"), Native.Min.X, Native.Min.Y, Native.Max.X, Native.Max.Y) : TEXT(""), *Diag);
        bool bVisible = Box.bIsValid;
        if (Entry.Zone == EWowUiZone::Windows && bVisible) WindowsBox = Box.ExpandBy(4.0);
        // A tooltip over an open window is already on the windows panel.
        if (Entry.Zone == EWowUiZone::Tooltip && bVisible && WindowsBox.bIsValid && WindowsBox.IsInside(Box.ExpandBy(-2.0))) bVisible = false;
        const int32 First = OutZones.Num();
        // The cast bar's border art and spell name reach past its frame rect.
        const FVector2D Margin = Entry.Zone == EWowUiZone::CastBar ? FVector2D(16.0, 22.0) : FVector2D(4.0, 4.0);
        Add(Entry.Zone, bVisible ? FBox2D(Box.Min - Margin, Box.Max + Margin) : FBox2D(FVector2D::ZeroVector, FVector2D::ZeroVector), bVisible);
        if (OutZones.IsValidIndex(First)) OutZones[First].SizeDp *= Entry.Magnify;
    }
    // Neighbouring panels share one texture: where the minimap's rect reaches into the tracker's
    // (or the chat's into the action bars'), cut the upper one at the lower one's top, so no
    // panel shows pieces of the other.
    for (FWowUiVrZone& Upper : OutZones)
        for (const FWowUiVrZone& Lower : OutZones)
        {
            if (&Upper == &Lower || !Upper.bVisible || !Lower.bVisible) continue;
            // Only the stacked pairs of the default layout (a cut elsewhere could hide a whole panel).
            const bool bPair = (Upper.Zone == EWowUiZone::Minimap && Lower.Zone == EWowUiZone::Tracker)
                || (Upper.Zone == EWowUiZone::Chat && Lower.Zone == EWowUiZone::ActionBars);
            if (!bPair || Upper.UV.Min.Y >= Lower.UV.Min.Y) continue;
            const bool bOverlapX = Upper.UV.Min.X < Lower.UV.Max.X && Lower.UV.Min.X < Upper.UV.Max.X;
            if (!bOverlapX || Upper.UV.Max.Y <= Lower.UV.Min.Y) continue;
            const float OldH = Upper.UV.Max.Y - Upper.UV.Min.Y;
            Upper.UV.Max.Y = Lower.UV.Min.Y;
            if (OldH > 0.f) Upper.SizeDp.Y *= (Upper.UV.Max.Y - Upper.UV.Min.Y) / OldH;
        }
    // An open window reaching into another panel's rectangle (the Character window's tabs on the
    // chat, a gossip window's title on the unit frames) put that piece of the window on that
    // panel too, as a loose strip. Cut the other panel at the window's edge when only an edge of
    // it is covered; otherwise hide it while the window is open (the chat stays while typing).
    if (const FWowUiVrZone* Win = OutZones.FindByPredicate([](const FWowUiVrZone& Z) { return Z.Zone == EWowUiZone::Windows && Z.bVisible; }))
    {
        bool bTyping = false;
        if (S.FrameManager)
        {
            const int64 Edit = S.FrameManager->FindFrame(TEXT("ChatFrame1EditBox"));
            bTyping = Edit >= 0 && S.FrameManager->IsFrameVisible(Edit);
        }
        const FBox2f W = Win->UV;
        for (FWowUiVrZone& Z : OutZones)
        {
            if (!Z.bVisible || &Z == Win) continue;
            // (Popups - the error-text strip - takes part too: an Options window under it put a copy of
            // the window's title and tabs on that strip above the view.)
            if (Z.Zone == EWowUiZone::Windows || Z.Zone == EWowUiZone::Tooltip
                || Z.Zone == EWowUiZone::Cursor || Z.Zone == EWowUiZone::Hud) continue;
            const FBox2f B = Z.UV;
            if (!(B.Min.X < W.Max.X && W.Min.X < B.Max.X && B.Min.Y < W.Max.Y && W.Min.Y < B.Max.Y)) continue;
            const float BW = B.Max.X - B.Min.X, BH = B.Max.Y - B.Min.Y;
            if (BW <= 0.f || BH <= 0.f) continue;
            // Candidate cuts that leave the panel outside the window: keep the one that keeps most.
            float Keep = 0.f; int32 Side = -1;
            if (W.Min.Y <= B.Min.Y && W.Max.Y < B.Max.Y && (B.Max.Y - W.Max.Y) / BH > Keep) { Keep = (B.Max.Y - W.Max.Y) / BH; Side = 0; } // window over the top
            if (W.Max.Y >= B.Max.Y && W.Min.Y > B.Min.Y && (W.Min.Y - B.Min.Y) / BH > Keep) { Keep = (W.Min.Y - B.Min.Y) / BH; Side = 1; } // over the bottom
            if (W.Min.X <= B.Min.X && W.Max.X < B.Max.X && (B.Max.X - W.Max.X) / BW > Keep) { Keep = (B.Max.X - W.Max.X) / BW; Side = 2; } // over the left
            if (W.Max.X >= B.Max.X && W.Min.X > B.Min.X && (W.Min.X - B.Min.X) / BW > Keep) { Keep = (W.Min.X - B.Min.X) / BW; Side = 3; } // over the right
            if (Side >= 0 && Keep >= .6f)
            {
                if (Side == 0) Z.UV.Min.Y = W.Max.Y;
                else if (Side == 1) Z.UV.Max.Y = W.Min.Y;
                else if (Side == 2) Z.UV.Min.X = W.Max.X;
                else Z.UV.Max.X = W.Min.X;
                if (Side <= 1) Z.SizeDp.Y *= Keep; else Z.SizeDp.X *= Keep;
            }
            else if (!(Z.Zone == EWowUiZone::Chat && bTyping)) Z.bVisible = false;
        }
    }
    // The side bars start under the minimap: where their rect reaches up into the minimap's (the
    // zoom buttons and clock hang below the map), start the side-bar panel at the minimap's bottom
    // (Stefan saw the +/- buttons a second time beside the tracker).
    if (FWowUiVrZone* Side = OutZones.FindByPredicate([](const FWowUiVrZone& Z) { return Z.Zone == EWowUiZone::SideBars && Z.bVisible; }))
        if (const FWowUiVrZone* Map = OutZones.FindByPredicate([](const FWowUiVrZone& Z) { return Z.Zone == EWowUiZone::Minimap && Z.bVisible; }))
            if (Side->UV.Min.X < Map->UV.Max.X && Map->UV.Min.X < Side->UV.Max.X && Side->UV.Min.Y < Map->UV.Max.Y && Side->UV.Max.Y > Map->UV.Max.Y)
            {
                const float OldH = Side->UV.Max.Y - Side->UV.Min.Y;
                Side->UV.Min.Y = Map->UV.Max.Y;
                if (OldH > 0.f) Side->SizeDp.Y *= (Side->UV.Max.Y - Side->UV.Min.Y) / OldH;
            }
    // The right-hand side bars sit beside the tracker; where their rect reaches into the tracker
    // (long objective lines run under them) start the side-bar panel at the tracker's right
    // edge, or hide it when nothing is left (Stefan saw word endings floating beside the tracker).
    if (FWowUiVrZone* Side = OutZones.FindByPredicate([](const FWowUiVrZone& Z) { return Z.Zone == EWowUiZone::SideBars && Z.bVisible; }))
        if (FWowUiVrZone* Tracker = OutZones.FindByPredicate([](const FWowUiVrZone& Z) { return Z.Zone == EWowUiZone::Tracker && Z.bVisible; }))
        {
            const bool bOverlapY = Side->UV.Min.Y < Tracker->UV.Max.Y && Tracker->UV.Min.Y < Side->UV.Max.Y;
            // r247 (Stefan 12.10, VR: the left side bar was cut, and a thin strip of its buttons
            // floated beside the tracker): when the tracker's box only reaches a few pixels into the
            // bars (its widget edge, not text), the tracker panel ends where the bars begin.
            const float Reach = (Tracker->UV.Max.X - Side->UV.Min.X) * float(TargetSize.X);
            const float TrackerW = Tracker->UV.Max.X - Tracker->UV.Min.X;
            if (bOverlapY && Reach > 0.f && Reach <= 40.f && Side->UV.Min.X > Tracker->UV.Min.X && TrackerW > 0.f)
            {
                const float NewW = Side->UV.Min.X - Tracker->UV.Min.X;
                Tracker->SizeDp.X *= NewW / TrackerW;
                Tracker->UV.Max.X = Side->UV.Min.X;
            }
            else if (bOverlapY && Side->UV.Min.X < Tracker->UV.Max.X && Side->UV.Max.X > Tracker->UV.Min.X)
            {
                const float OldW = Side->UV.Max.X - Side->UV.Min.X;
                Side->UV.Min.X = Tracker->UV.Max.X;
                const float NewW = Side->UV.Max.X - Side->UV.Min.X;
                if (NewW * TargetSize.X < 24.f) Side->bVisible = false;
                else if (OldW > 0.f) Side->SizeDp.X *= NewW / OldW;
            }
        }
    // The whole target, for a single big panel or debugging: never shown by the zone presenter
    // (it held every other zone a second time, straight ahead).
    Add(EWowUiZone::Hud, FBox2D(FVector2D::ZeroVector, TargetSize), false);
    S.LastZones = OutZones;
}

bool FWowUiVrHost::ViewportPixelToWindow(const FVector2D& Pixel, FVector2D& OutWindowPos)
{
    FHostState& S = State();
    if (!S.bActive || !GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport) return false;
    const FIntPoint PixelSize = GEngine->GameViewport->Viewport->GetSizeXY();
    if (PixelSize.X <= 0 || PixelSize.Y <= 0) return false;
    OutWindowPos = Pixel / FVector2D(PixelSize) * S.ViewportSize * S.PixelScale;
    return true;
}

bool FWowUiVrHost::CursorToWindow(FVector2D& OutWindowPos)
{
    if (!State().bActive || !FSlateApplication::IsInitialized()) return false;
    return ToWindowSpace(FSlateApplication::Get().GetCursorPos(), OutWindowPos);
}

void FWowUiVrHost::SetGazePoint(bool bValid, const FVector2D& WindowPos)
{
    FHostState& S = State();
    S.bGazeValid = bValid;
    S.GazePoint = WindowPos;
    S.GazeTime = FPlatformTime::Seconds();
}

bool FWowUiVrHost::GetGazePoint(FVector2D& OutWindowPos)
{
    FHostState& S = State();
    // A presenter that stopped reporting (VR off, panel gone) must not leave a stale point.
    if (!S.bActive || !S.bGazeValid || FPlatformTime::Seconds() - S.GazeTime > .25) return false;
    OutWindowPos = S.GazePoint;
    return true;
}

bool FWowUiVrHost::GetVisibleZoneCenter(EWowUiZone Zone, FVector2D& OutWindowPos)
{
    FHostState& S = State();
    if (!S.bActive || !S.Target.IsValid()) return false;
    const FVector2D TargetSize(S.Target->SizeX, S.Target->SizeY);
    for (const FWowUiVrZone& Z : S.LastZones)
        if (Z.Zone == Zone && Z.bVisible)
        {
            OutWindowPos = FVector2D((Z.UV.Min + Z.UV.Max) * .5f) * TargetSize;
            return true;
        }
    return false;
}

bool FWowUiVrHost::MoveCursorToWindowPoint(const FVector2D& WindowPos)
{
    FHostState& S = State();
    if (!S.bActive || !FSlateApplication::IsInitialized() || !GEngine || !GEngine->GameViewport) return false;
    const TSharedPtr<SViewport> Viewport = GEngine->GameViewport->GetGameViewportWidget();
    if (!Viewport.IsValid()) return false;
    const FGeometry& Geometry = Viewport->GetCachedGeometry();
    const FVector2D WindowSize = S.ViewportSize * S.PixelScale;
    if (WindowSize.X <= 0 || WindowSize.Y <= 0) return false;
    const FVector2D Screen = Geometry.LocalToAbsolute(WindowPos / WindowSize * Geometry.GetLocalSize());
    FSlateApplication& App = FSlateApplication::Get();
    App.SetCursorPos(Screen);
    const FPointerEvent Move(0, FSlateApplicationBase::CursorPointerIndex, Screen, Screen, TSet<FKey>(), EKeys::Invalid, 0.f, App.GetModifierKeys());
    App.ProcessMouseMoveEvent(Move);
    return true;
}

bool FWowUiVrHost::IsRepeatButtonAt(const FVector2D& WindowPos)
{
    if (!State().bActive) return false;
    static const FName RepeatTag(TEXT("WowRepeat"));
    for (const FWidgetAndPointer& Entry : HitPath(WindowPos))
        if (Entry.Widget->GetTag() == RepeatTag) return true;
    return false;
}

bool FWowUiVrHost::ClickAtWindowPoint(const FVector2D& WindowPos)
{
    FHostState& S = State();
    if (!S.bActive || !FSlateApplication::IsInitialized() || !GEngine || !GEngine->GameViewport) return false;
    const TSharedPtr<SViewport> Viewport = GEngine->GameViewport->GetGameViewportWidget();
    if (!Viewport.IsValid()) return false;
    const FGeometry& Geometry = Viewport->GetCachedGeometry();
    const FVector2D WindowSize = S.ViewportSize * S.PixelScale;
    if (WindowSize.X <= 0 || WindowSize.Y <= 0) return false;
    const FVector2D Screen = Geometry.LocalToAbsolute(WindowPos / WindowSize * Geometry.GetLocalSize());
    FSlateApplication& App = FSlateApplication::Get();
    App.SetCursorPos(Screen);
    const FModifierKeysState Modifiers = App.GetModifierKeys();
    const FPointerEvent Move(0, FSlateApplicationBase::CursorPointerIndex, Screen, Screen, TSet<FKey>(), EKeys::Invalid, 0.f, Modifiers);
    App.ProcessMouseMoveEvent(Move);
    const FPointerEvent Down(0, FSlateApplicationBase::CursorPointerIndex, Screen, Screen, TSet<FKey>({EKeys::LeftMouseButton}), EKeys::LeftMouseButton, 0.f, Modifiers);
    App.ProcessMouseButtonDownEvent(nullptr, Down);
    const FPointerEvent Up(0, FSlateApplicationBase::CursorPointerIndex, Screen, Screen, TSet<FKey>(), EKeys::LeftMouseButton, 0.f, Modifiers);
    App.ProcessMouseButtonUpEvent(Up);
    UE_LOG(LogWowUiVr, Log, TEXT("VR dwell UI click at window (%.0f,%.0f)"), WindowPos.X, WindowPos.Y);
    return true;
}

void FWowUiVrHost::SetZoneOpacity(EWowUiZone Zone, float Opacity)
{
    State().ZoneOpacity.Add(static_cast<uint8>(Zone), Opacity);
}

bool FWowUiVrHost::IsPointerOverShownPanel()
{
    FHostState& S = State();
    if (!S.bActive || S.bHidden || !FSlateApplication::IsInitialized()) return false;
    FVector2D Pos;
    if (!ToWindowSpace(FSlateApplication::Get().GetCursorPos(), Pos) || !S.Target.IsValid()) return false;
    const FVector2D TargetSize(S.Target->SizeX, S.Target->SizeY);
    if (TargetSize.X <= 0 || TargetSize.Y <= 0) return false;
    const FVector2f UV(Pos / TargetSize);
    for (const FWowUiVrZone& Zone : S.LastZones)
    {
        if (!Zone.bVisible || Zone.Zone == EWowUiZone::Hud || Zone.Zone == EWowUiZone::Popups) continue;
        if (UV.X < Zone.UV.Min.X || UV.Y < Zone.UV.Min.Y || UV.X > Zone.UV.Max.X || UV.Y > Zone.UV.Max.Y) continue;
        const float* Opacity = S.ZoneOpacity.Find(static_cast<uint8>(Zone.Zone));
        if (!Opacity || *Opacity >= .3f) return true;
    }
    return false;
}

bool FWowUiVrHost::IsPointerOverShownUi()
{
    FHostState& S = State();
    if (!S.bActive || S.bHidden || !FSlateApplication::IsInitialized()) return false;
    FVector2D Pos;
    if (!ToWindowSpace(FSlateApplication::Get().GetCursorPos(), Pos)) return false;
    return HitsUi(HitPath(Pos)) && PointShown(Pos);
}
