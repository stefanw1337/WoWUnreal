// r257 (Stefan 12.10) Experimental "immersive interface" (option "immersiveui"):
//  - standing still, out of combat: the whole interface as usual;
//  - moving: the interface fades away, only the minimap stays faintly visible;
//  - in combat: the minimap goes too, the action bars (with the cast bar) and the unit frames stay
//    at 40 % so they can still be read.
// Open windows (bags, character, quest ...) are never touched, and typing in the chat shows everything.
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "WowUpdateFields.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowClientPreferences.h"
#include "WowPlayerCharacter.h"
#include "UI/SWowMinimap.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h" // r442: Link->GetParent() returns UPanelWidget* (C2440 without the full type)

namespace
{
enum class EImmersiveGroup : uint8 { Minimap, Action, Unit, Other };

const TArray<TPair<const TCHAR*, EImmersiveGroup>>& ImmersiveFrames()
{
    static const TArray<TPair<const TCHAR*, EImmersiveGroup>> Frames = {
        {TEXT("MinimapCluster"), EImmersiveGroup::Minimap},
        {TEXT("MainMenuBar"), EImmersiveGroup::Action}, {TEXT("MultiBarBottomLeft"), EImmersiveGroup::Action},
        {TEXT("MultiBarBottomRight"), EImmersiveGroup::Action}, {TEXT("MultiBarRight"), EImmersiveGroup::Action},
        {TEXT("MultiBarLeft"), EImmersiveGroup::Action}, {TEXT("ShapeshiftBarFrame"), EImmersiveGroup::Action},
        {TEXT("PetActionBarFrame"), EImmersiveGroup::Action}, {TEXT("PossessBarFrame"), EImmersiveGroup::Action},
        {TEXT("VehicleMenuBar"), EImmersiveGroup::Action}, {TEXT("MultiCastActionBarFrame"), EImmersiveGroup::Action},
        {TEXT("CastingBarFrame"), EImmersiveGroup::Action},
        {TEXT("BT4Bar1"), EImmersiveGroup::Action}, {TEXT("BT4Bar2"), EImmersiveGroup::Action}, {TEXT("BT4Bar3"), EImmersiveGroup::Action},
        {TEXT("BT4Bar4"), EImmersiveGroup::Action}, {TEXT("BT4Bar5"), EImmersiveGroup::Action}, {TEXT("BT4Bar6"), EImmersiveGroup::Action},
        {TEXT("BT4Bar7"), EImmersiveGroup::Action}, {TEXT("BT4Bar8"), EImmersiveGroup::Action}, {TEXT("BT4Bar9"), EImmersiveGroup::Action},
        {TEXT("BT4Bar10"), EImmersiveGroup::Action}, {TEXT("BT4BarStanceBar"), EImmersiveGroup::Action},
        {TEXT("BT4BarPetBar"), EImmersiveGroup::Action}, {TEXT("BT4BarMicroMenu"), EImmersiveGroup::Action},
        {TEXT("BT4BarBagBar"), EImmersiveGroup::Action},
        {TEXT("PlayerFrame"), EImmersiveGroup::Unit}, {TEXT("TargetFrame"), EImmersiveGroup::Unit},
        {TEXT("TargetFrameToT"), EImmersiveGroup::Unit}, {TEXT("FocusFrame"), EImmersiveGroup::Unit},
        {TEXT("PetFrame"), EImmersiveGroup::Unit}, {TEXT("PartyMemberFrame1"), EImmersiveGroup::Unit},
        {TEXT("PartyMemberFrame2"), EImmersiveGroup::Unit}, {TEXT("PartyMemberFrame3"), EImmersiveGroup::Unit},
        {TEXT("PartyMemberFrame4"), EImmersiveGroup::Unit},
        {TEXT("ChatFrame1"), EImmersiveGroup::Other}, {TEXT("ChatFrame2"), EImmersiveGroup::Other},
        {TEXT("ChatFrame1Tab"), EImmersiveGroup::Other}, {TEXT("ChatFrame2Tab"), EImmersiveGroup::Other},
        {TEXT("ChatFrameMenuButton"), EImmersiveGroup::Other}, {TEXT("FriendsMicroButton"), EImmersiveGroup::Other},
        {TEXT("BuffFrame"), EImmersiveGroup::Other}, {TEXT("TemporaryEnchantFrame"), EImmersiveGroup::Other},
        {TEXT("DurabilityFrame"), EImmersiveGroup::Other}, {TEXT("WatchFrame"), EImmersiveGroup::Other},
    };
    return Frames;
}
}

void AWowGameplayController::UpdateImmersiveInterface(float DeltaTime)
{
    FWowFrameManager* Frames = UIManager ? UIManager->GetFrameManager() : nullptr;
    const bool bEnabled = FWowClientPreferences::GetWorldOption(TEXT("immersiveui")) && Frames && ConnectionManager && !bUserInterfaceHidden;

    // What each group should be now (1 = as usual).
    float Want[4] = {1.f, 1.f, 1.f, 1.f};
    if (bEnabled)
    {
        const FWowEntity* Me = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
        const bool bCombat = Me && (Me->GetField(UnitField::FLAGS) & 0x00080000u); // UNIT_FLAG_IN_COMBAT
        bool bMoving = false;
        if (const APawn* MyPawn = GetPawn())
        {
            bMoving = MyPawn->GetVelocity().Size2D() > 10.f;
            if (const AWowPlayerCharacter* WowChar = Cast<AWowPlayerCharacter>(MyPawn)) bMoving |= WowChar->bIsAutoRunning;
        }
        // Moving: hide at once; standing still: the interface comes back after half a second.
        const double Now = FPlatformTime::Seconds();
        if (bMoving) ImmersiveLastMoveTime = Now;
        const bool bRecentlyMoved = Now - ImmersiveLastMoveTime < .5;
        const int64 EditBox = Frames->FindFrame(TEXT("ChatFrame1EditBox"));
        const bool bTyping = EditBox >= 0 && Frames->IsFrameVisible(EditBox);
        if (!bTyping)
        {
            if (bCombat) { Want[0] = 0.f; Want[1] = .4f; Want[2] = .4f; Want[3] = 0.f; }
            else if (bRecentlyMoved) { Want[0] = .35f; Want[1] = 0.f; Want[2] = 0.f; Want[3] = 0.f; }
        }
    }
    for (int32 Group = 0; Group < 4; ++Group)
    {
        // Fade out quickly, back in a little slower.
        const float Speed = Want[Group] < ImmersiveAlpha[Group] ? 6.f : 4.f;
        ImmersiveAlpha[Group] = FMath::FInterpConstantTo(ImmersiveAlpha[Group], Want[Group], DeltaTime, Speed * .5f);
    }
    if (!Frames) return;

    // Each touched frame keeps the opacity it had before (a Lua SetAlpha) and gets that times the group's.
    auto Apply = [this](const FString& Key, TFunctionRef<void(float)> Set, TFunctionRef<float()> Get, float Alpha)
    {
        float* Base = ImmersiveBaseOpacity.Find(Key);
        if (Alpha >= .999f)
        {
            if (Base) { Set(*Base); ImmersiveBaseOpacity.Remove(Key); }
            return;
        }
        if (!Base) Base = &ImmersiveBaseOpacity.Add(Key, Get());
        Set(*Base * Alpha);
    };
    // r272 (Stefan 13.10: five yellow squares stayed when everything faded - Bartender's bag bar):
    // addons re-parent frames in Lua (SetParent) while their widgets stay where they were created, so
    // a group frame's widget does not contain them. Every Lua descendant whose widget is not already
    // inside a faded widget is faded with its group too. The list is rebuilt once a second.
    struct FImmersiveTarget { TWeakObjectPtr<UWidget> Widget; FString Key; EImmersiveGroup Group; };
    static TArray<FImmersiveTarget> Targets;
    static double NextTargetScan = 0.0;
    const double ScanNow = FPlatformTime::Seconds();
    if (ScanNow >= NextTargetScan || Targets.IsEmpty())
    {
        NextTargetScan = ScanNow + 1.0;
        Targets.Reset();
        TSet<const UWidget*> Covered;
        auto IsInsideCovered = [&Covered](const UWidget* Widget)
        {
            for (const UWidget* Link = Widget; Link; Link = Link->GetParent())
                if (Covered.Contains(Link)) return true;
            return false;
        };
        for (const auto& Entry : ImmersiveFrames())
        {
            const int64 Root = Frames->FindFrame(Entry.Key);
            UWidget* RootWidget = Frames->GetWidgetForHandle(Root);
            if (!RootWidget) continue;
            if (!IsInsideCovered(RootWidget))
            {
                Targets.Add({RootWidget, Entry.Key, Entry.Value});
                Covered.Add(RootWidget);
            }
            TArray<int64> Pending = Frames->GetChildHandles(Root);
            for (int32 Index = 0; Index < Pending.Num() && Index < 4000; ++Index)
            {
                const int64 Child = Pending[Index];
                if (UWidget* ChildWidget = Frames->GetWidgetForHandle(Child); ChildWidget && !IsInsideCovered(ChildWidget))
                {
                    Targets.Add({ChildWidget, FString::Printf(TEXT("#h%lld"), Child), Entry.Value});
                    Covered.Add(ChildWidget);
                }
                Pending.Append(Frames->GetChildHandles(Child));
            }
        }
    }
    TSet<const UWidget*> FadedOut;
    for (const FImmersiveTarget& Target : Targets)
    {
        UWidget* Widget = Target.Widget.Get();
        if (!Widget) continue;
        const float GroupAlpha = ImmersiveAlpha[static_cast<int32>(Target.Group)];
        Apply(Target.Key, [Widget](float A) { Widget->SetRenderOpacity(A); }, [Widget]() { return Widget->GetRenderOpacity(); },
            GroupAlpha);
        // r272 (Stefan 13.10: hidden action bars still gave tooltips): faded-out parts take no mouse.
        if (GroupAlpha < .15f) FadedOut.Add(Widget);
    }
    Frames->SetMouseBlockedWidgets(FadedOut);
    // Native pieces: the live minimap image (with the ring) and the objectives tracker.
    if (MinimapWidget.IsValid())
    {
        TSharedPtr<SWowMinimap> Map = MinimapWidget;
        Apply(TEXT("#Minimap"), [Map](float A) { Map->SetRenderOpacity(A); }, [Map]() { return Map->GetRenderOpacity(); },
            ImmersiveAlpha[static_cast<int32>(EImmersiveGroup::Minimap)]);
    }
    if (QuestTrackerContainerWidget.IsValid())
    {
        TSharedPtr<SWidget> Tracker = QuestTrackerContainerWidget;
        Apply(TEXT("#Tracker"), [Tracker](float A) { Tracker->SetRenderOpacity(A); }, [Tracker]() { return Tracker->GetRenderOpacity(); },
            ImmersiveAlpha[static_cast<int32>(EImmersiveGroup::Other)]);
    }
}
