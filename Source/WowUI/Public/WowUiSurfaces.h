#pragma once
#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

/**
 * Which part of the interface a top-level UI root belongs to. Zones are the unit for
 * per-part UI scale and, later, for VR placement (head-locked, body-locked, world-locked,
 * wrist, hide-when-not-looked-at). See the project doc claude/vr-ui-plan.md.
 */
enum class EWowUiZone : uint8
{
    Glue,        // login, realm list, character select/create, loading screen
    Hud,         // the FrameXML root (contains its own sub-zones)
    UnitFrames,  // player, target, focus, party, pet, target of target
    ActionBars,  // main bar, multi bars, pet/stance bars, Bartender4
    Chat,
    Minimap,
    Auras,
    Tracker,     // quest/achievement watch
    Windows,     // bags, character, quest log, mail, friends, vendor, talents ...
    Popups,      // StaticPopup, death/resurrect, invites, countdowns
    Tooltip,
    CastBar,
    Cursor,
    World,       // world-anchored overlays (nameplates, combat text, bubbles, quest diamonds)
    Debug,
    SideBars,    // the vertical right-hand action bars (MultiBarRight/Left); VR panel of its own
};

/**
 * Single entry point for every top-level UI root. On the flat screen it adds the widget to
 * the game viewport exactly as before. A presenter (the VR layer) can take over: it then
 * hosts each root on its own surface instead. New UI must be added through here, never with
 * GameViewport->AddViewportWidgetContent directly.
 */
struct WOWUI_API FWowUiSurfaces
{
    static void Add(TSharedRef<SWidget> Widget, EWowUiZone Zone, int32 ZOrder, FName Id = NAME_None);
    static void Remove(TSharedRef<SWidget> Widget);
    static bool ZoneOf(const TSharedRef<SWidget>& Widget, EWowUiZone& OutZone);

    struct FEntry
    {
        TWeakPtr<SWidget> Widget;
        EWowUiZone Zone = EWowUiZone::Hud;
        int32 ZOrder = 0;
        FName Id;
        bool bPresented = false; // hosted by the presenter instead of the viewport
    };
    static const TArray<FEntry>& Entries();

    /** Return true when the presenter hosts (bAdd) or released (!bAdd) the widget itself. */
    using FPresenter = TFunction<bool(const FEntry& Entry, bool bAdd)>;
    static void SetPresenter(FPresenter InPresenter);
};
