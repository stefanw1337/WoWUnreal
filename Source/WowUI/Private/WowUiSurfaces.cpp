#include "WowUiSurfaces.h"
#include "WowUiVrHost.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

namespace
{
TArray<FWowUiSurfaces::FEntry>& Registry() { static TArray<FWowUiSurfaces::FEntry> Value; return Value; }
FWowUiSurfaces::FPresenter& Presenter() { static FWowUiSurfaces::FPresenter Value; return Value; }

void Prune()
{
    Registry().RemoveAll([](const FWowUiSurfaces::FEntry& Entry) { return !Entry.Widget.IsValid(); });
}
}

void FWowUiSurfaces::Add(TSharedRef<SWidget> Widget, EWowUiZone Zone, int32 ZOrder, FName Id)
{
    FWowUiVrHost::IsActive(); // starts the VR host's ticker (it switches on with the headset)
    Prune();
    for (const FEntry& Existing : Registry())
        if (Existing.Widget == Widget) return;
    FEntry& Entry = Registry().AddDefaulted_GetRef();
    Entry.Widget = Widget;
    Entry.Zone = Zone;
    Entry.ZOrder = ZOrder;
    Entry.Id = Id;
    Entry.bPresented = Presenter() && Presenter()(Entry, true);
    if (!Entry.bPresented && GEngine && GEngine->GameViewport)
        GEngine->GameViewport->AddViewportWidgetContent(Widget, ZOrder);
}

void FWowUiSurfaces::Remove(TSharedRef<SWidget> Widget)
{
    const int32 Index = Registry().IndexOfByPredicate([&Widget](const FEntry& Entry) { return Entry.Widget == Widget; });
    bool bPresented = false;
    if (Index != INDEX_NONE)
    {
        const FEntry Entry = Registry()[Index];
        Registry().RemoveAt(Index);
        bPresented = Entry.bPresented && Presenter() && Presenter()(Entry, false);
    }
    // Also removes roots that were added before they went through the registry.
    if (!bPresented && GEngine && GEngine->GameViewport)
        GEngine->GameViewport->RemoveViewportWidgetContent(Widget);
}

bool FWowUiSurfaces::ZoneOf(const TSharedRef<SWidget>& Widget, EWowUiZone& OutZone)
{
    for (const FEntry& Entry : Registry())
        if (Entry.Widget == Widget) { OutZone = Entry.Zone; return true; }
    return false;
}

const TArray<FWowUiSurfaces::FEntry>& FWowUiSurfaces::Entries()
{
    Prune();
    return Registry();
}

void FWowUiSurfaces::SetPresenter(FPresenter InPresenter)
{
    // Move every root between the viewport and the new presenter.
    Prune();
    // The registry only holds weak pointers: for many roots the viewport (or the VR window)
    // is the only owner. Hold them strongly across the move, or a root removed from the old
    // place is destroyed before it reaches the new one (the FrameXML UI, minimap and unit
    // frames vanished in VR and stayed gone on the monitor after VR was turned off).
    TArray<TSharedRef<SWidget>> Hold;
    for (const FEntry& Entry : Registry())
        if (const TSharedPtr<SWidget> Widget = Entry.Widget.Pin()) Hold.Add(Widget.ToSharedRef());
    for (FEntry& Entry : Registry())
    {
        const TSharedPtr<SWidget> Widget = Entry.Widget.Pin();
        if (!Widget.IsValid()) continue;
        if (Entry.bPresented && Presenter()) Presenter()(Entry, false);
        else if (GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
        Entry.bPresented = false;
    }
    Presenter() = MoveTemp(InPresenter);
    for (FEntry& Entry : Registry())
    {
        const TSharedPtr<SWidget> Widget = Entry.Widget.Pin();
        if (!Widget.IsValid()) continue;
        Entry.bPresented = Presenter() && Presenter()(Entry, true);
        if (!Entry.bPresented && GEngine && GEngine->GameViewport)
            GEngine->GameViewport->AddViewportWidgetContent(Widget.ToSharedRef(), Entry.ZOrder);
    }
}
