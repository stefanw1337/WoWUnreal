// VR world cards for the screen-projected overlays (zone World): quest diamonds, chat bubbles
// and the XP gain text. Nameplates and combat text switch their own widget components.
#include "WowGameplayController.h"
#include "WowVRSupport.h"
#include "WowVrCards.h"
#include "WowConnectionManager.h"
#include "WowEntity.h"
#include "UI/SWowQuestDiamond.h"
#include "UI/SWowChatBubbles.h"
#include "UI/SWowXPGain.h"
#include "Coord/WowCoordinate.h"
#include "Components/WidgetComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "WowClientPreferences.h"
#include "WowUiVrHost.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowVrCards, Log, All);

/** VR dwell click: ring around the crosshair that fills while it rests on something, and
 *  flashes when the click fires. */
class SWowDwellRing : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWowDwellRing) {}
        SLATE_ATTRIBUTE(float, Progress)
        SLATE_ATTRIBUTE(bool, Flash)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Progress = Args._Progress; Flash = Args._Flash; SetVisibility(EVisibility::HitTestInvisible); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(64.f, 64.f); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle&, bool) const override
    {
        const bool bFlash = Flash.Get();
        const float P = bFlash ? 1.f : FMath::Clamp(Progress.Get(), 0.f, 1.f);
        if (P <= 0.f) return Layer;
        const FVector2D C = Geometry.GetLocalSize() * .5f;
        const float R = FMath::Min(C.X, C.Y) - 3.f;
        const int32 Steps = FMath::Max(2, FMath::CeilToInt(48 * P));
        TArray<FVector2f> Arc;
        for (int32 I = 0; I <= Steps; ++I)
        {
            const float A = -PI * .5f + 2.f * PI * P * I / Steps; // from the top, clockwise
            Arc.Add(FVector2f(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R));
        }
        FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Arc, ESlateDrawEffect::None, FLinearColor::Black, true, 6.f);
        FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(), Arc, ESlateDrawEffect::None,
            bFlash ? FLinearColor::White : FLinearColor(1.f, .82f, .1f, 1.f), true, 3.5f);
        return Layer + 2;
    }
private:
    TAttribute<float> Progress;
    TAttribute<bool> Flash;
};

namespace
{
void DestroyCard(TWeakObjectPtr<UWidgetComponent>& Card)
{
    if (UWidgetComponent* Component = Card.Get()) Component->DestroyComponent();
    Card.Reset();
}

/**
 * Our world cards live on an actor of their own, not on the player character: the character
 * re-registers its components (model rebuilds), which rebuilt each card's material from its own
 * dynamic instance ("... is not a valid parent") and the pointer drew as a black checkered square
 * (Stefan, still in 222530; RepairMaterial did not catch it).
 */
TWeakObjectPtr<AActor> GVrCardHost;
AActor* VrCardHost(UWorld* World, AActor* Fallback)
{
    if (AActor* Host = GVrCardHost.Get(); Host && Host->GetWorld() == World && !Host->IsActorBeingDestroyed()) return Host;
    if (!World) return Fallback;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.ObjectFlags |= RF_Transient;
    AActor* Host = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
    if (!Host) return Fallback;
    USceneComponent* Root = NewObject<USceneComponent>(Host, TEXT("VrCardRoot"));
    Host->SetRootComponent(Root);
    Root->RegisterComponent();
    GVrCardHost = Host;
    UE_LOG(LogWowVrCards, Log, TEXT("VR card host actor spawned"));
    return Host;
}
}

void AWowGameplayController::UpdateVrWorldCards(float DeltaTime)
{
    APawn* PlayerPawn = GetPawn();
    const bool bVr = FWowVRSupport::IsActive() && PlayerPawn && ConnectionManager && !bUserInterfaceHidden;
    if (ChatBubbleLayer.IsValid()) ChatBubbleLayer->SetWorldMode(FWowVRSupport::IsActive());
    // In the headset the bags stack further left (Stefan 09.10: opened bags at the right edge
    // landed in the minimap, tracker and side-bar panels): FrameXML places them from the right
    // edge by CONTAINER_OFFSET_X, so add a quarter of the screen to it while the VR UI is on.
    {
        // Every 2 s (and on a change): a UI reload or relog starts a new Lua state without the hook.
        static double GNextBagsCheck = 0.0;
        const double NowBags = FPlatformTime::Seconds();
        static int32 GBagsInVr = -1;
        const bool bHostVr = FWowUiVrHost::IsActive();
        if ((int32(bHostVr) != GBagsInVr || NowBags >= GNextBagsCheck) && UIManager && UIManager->GetEventSystem())
        {
            GBagsInVr = int32(bHostVr);
            GNextBagsCheck = NowBags + 2.0;
            const TCHAR* Flag = bHostVr ? TEXT("true") : TEXT("false");
            UIManager->GetEventSystem()->ExecuteLuaString(FString::Printf(TEXT(
                "local Changed = WOWUNREAL_VR_BAGS ~= %s WOWUNREAL_VR_BAGS = %s "
                "if updateContainerFrameAnchors and not WOWUNREAL_VR_BAGHOOK then WOWUNREAL_VR_BAGHOOK = true Changed = true "
                "local Original = updateContainerFrameAnchors "
                "updateContainerFrameAnchors = function(...) local X = CONTAINER_OFFSET_X "
                "if WOWUNREAL_VR_BAGS then CONTAINER_OFFSET_X = (X or 0) + math.floor(UIParent:GetWidth() * 0.27) end "
                "Original(...) CONTAINER_OFFSET_X = X end end "
                "if Changed and updateContainerFrameAnchors then updateContainerFrameAnchors() end"), Flag, Flag),
                TEXT("=VrBags"));
        }
    }
    // Claude-Perf r217: frame generation notices (turned off for VR / needs a restart after VR),
    // shown once each in the red error text and as a chat line.
    {
        static int32 GShownNoticeSerial = 0;
        const int32 Serial = FWowVRSupport::GetFrameGenerationNoticeSerial();
        if (Serial != GShownNoticeSerial && UIManager && UIManager->GetEventSystem())
        {
            GShownNoticeSerial = Serial;
            const FString Notice = FWowVRSupport::GetFrameGenerationNotice().Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
            if (!Notice.IsEmpty())
                UIManager->GetEventSystem()->ExecuteLuaString(FString::Printf(TEXT(
                    "if UIErrorsFrame then UIErrorsFrame:AddMessage(\"%s\", 1.0, 0.82, 0.0, 1.0, 5) end "
                    "if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage(\"%s\", 1.0, 0.82, 0.0) end"), *Notice, *Notice), TEXT("=VrNotice"));
        }
    }
    if (!bVr)
    {
        for (auto& Pair : VrDiamondCards) DestroyCard(Pair.Value);
        for (auto& Pair : VrBubbleCards) DestroyCard(Pair.Value);
        VrDiamondCards.Reset();
        VrBubbleCards.Reset();
        DestroyCard(VrXPGainCard);
        DestroyCard(VrCorpseCard);
        DestroyCard(VrPointerCard);
        return;
    }
    FVector Eye; FRotator EyeRotation;
    GetPlayerViewPoint(Eye, EyeRotation);
    // Cards on the player character (pointer, nameplates, labels) lose their material when the
    // character re-registers its components; rebuild any that did.
    {
        TInlineComponentArray<UWidgetComponent*> Cards(PlayerPawn);
        for (UWidgetComponent* Card : Cards) WowVrCards::RepairMaterial(Card);
    }
    static const float YardCm = (FWowCoordinate::WowToUE(FVector(1, 0, 0)) - FWowCoordinate::WowToUE(FVector::ZeroVector)).Size();

    // Quest diamonds (Ctrl-click in the tracker). Far ones are drawn at 40 m in their direction
    // so terrain rarely hides them and they keep a readable size; the yards stay the real ones.
    {
        TSet<uint32> Seen;
        for (const uint32 QuestId : TArray<uint32>(WorldTrackedQuests))
        {
            FVector World; float Yards = 0.f;
            if (!GetQuestDiamondTarget(QuestId, World, Yards)) continue;
            Seen.Add(QuestId);
            TWeakObjectPtr<UWidgetComponent>& Card = VrDiamondCards.FindOrAdd(QuestId);
            if (!Card.IsValid())
            {
                Card = WowVrCards::Make(VrCardHost(GetWorld(), PlayerPawn), TEXT("VrQuestDiamond"), FVector2D(440.f, 220.f), FVector2D(.5f, .5f), false);
                if (Card.IsValid()) Card->SetSlateWidget(SNew(SWowQuestDiamondCard));
            }
            UWidgetComponent* Component = Card.Get();
            if (!Component) continue;
            FString Title;
            if (const FWowQuestLogEntry* Q = ConnectionManager->PacketHandler.QuestLog.FindByPredicate(
                [QuestId](const FWowQuestLogEntry& E) { return E.QuestId == QuestId; }))
                Title = Q->Title;
            if (const TSharedPtr<SWidget> Widget = Component->GetSlateWidget())
                StaticCastSharedPtr<SWowQuestDiamondCard>(Widget)->SetMarker(Title, Yards);
            const FVector Lifted = World + FVector(0, 0, 60.f);
            const float Dist = float(FVector::Dist(Eye, Lifted));
            // r247 (Stefan 12.10: the diamonds suddenly vanished in VR): at 40 m a rise in the
            // ground between us and the card hid it. Far markers now sit 18 m out, a little above
            // the line to the target (the scale below keeps their angular size).
            const float MaxCm = 18.f * YardCm;
            FVector Place = Lifted;
            if (Dist > MaxCm)
            {
                const FVector Dir = (Lifted - Eye) / Dist;
                Place = Eye + Dir * MaxCm + FVector(0, 0, 120.f);
            }
            // r249 (Stefan 12.10: the diamonds showed again but under the ground): a hill between us
            // and the card hid it. Bring it in front of whatever the line to it meets, a little up.
            if (UWorld* TraceWorld = GetWorld())
            {
                FHitResult Hit;
                FCollisionQueryParams Query(SCENE_QUERY_STAT(VrQuestDiamond), false, PlayerPawn);
                const float PlaceDist = float(FVector::Dist(Eye, Place));
                if (PlaceDist > 100.f && TraceWorld->LineTraceSingleByChannel(Hit, Eye, Place, ECC_Visibility, Query) && Hit.Distance < PlaceDist - 30.f)
                    Place = Eye + (Place - Eye) / PlaceDist * FMath::Max(100.f, Hit.Distance * .9f) + FVector(0, 0, 60.f);
            }
            Component->SetWorldLocation(Place);
            WowVrCards::Face(Component, Eye, WowVrCards::DistanceScale(float(FVector::Dist(Eye, Place))));
            Component->SetVisibility(true);
        }
        for (auto It = VrDiamondCards.CreateIterator(); It; ++It)
            if (!Seen.Contains(It.Key())) { DestroyCard(It.Value()); It.RemoveCurrent(); }

        // r255 (Stefan 12.10): the corpse while you are a ghost, a skull card like the diamonds.
        FVector CorpseWorld; float CorpseYards = 0.f;
        if (GetCorpseMarker(CorpseWorld, CorpseYards))
        {
            if (!VrCorpseCard.IsValid())
            {
                VrCorpseCard = WowVrCards::Make(VrCardHost(GetWorld(), PlayerPawn), TEXT("VrCorpse"), FVector2D(440.f, 220.f), FVector2D(.5f, .5f), false);
                if (UWidgetComponent* Card = VrCorpseCard.Get())
                {
                    TSharedRef<SWowQuestDiamondCard> Widget = SNew(SWowQuestDiamondCard);
                    if (UIManager && UIManager->GetFrameManager())
                        Widget->SetCorpseIcon(UIManager->GetFrameManager()->LoadUITexture(TEXT("Interface\\TargetingFrame\\UI-RaidTargetingIcon_8")));
                    Card->SetSlateWidget(Widget);
                }
            }
            if (UWidgetComponent* Component = VrCorpseCard.Get())
            {
                if (const TSharedPtr<SWidget> Widget = Component->GetSlateWidget())
                    StaticCastSharedPtr<SWowQuestDiamondCard>(Widget)->SetMarker(TEXT("Corpse"), CorpseYards);
                const FVector Lifted = CorpseWorld + FVector(0, 0, 60.f);
                const float Dist = float(FVector::Dist(Eye, Lifted));
                const float MaxCm = 18.f * YardCm;
                FVector Place = Dist > MaxCm ? Eye + (Lifted - Eye) / Dist * MaxCm + FVector(0, 0, 120.f) : Lifted;
                if (UWorld* World = GetWorld())
                {
                    FHitResult Hit;
                    FCollisionQueryParams Query(SCENE_QUERY_STAT(VrCorpseCard), false, PlayerPawn);
                    const float PlaceDist = float(FVector::Dist(Eye, Place));
                    if (PlaceDist > 100.f && World->LineTraceSingleByChannel(Hit, Eye, Place, ECC_Visibility, Query) && Hit.Distance < PlaceDist - 30.f)
                        Place = Eye + (Place - Eye) / PlaceDist * FMath::Max(100.f, Hit.Distance * .9f) + FVector(0, 0, 60.f);
                }
                Component->SetWorldLocation(Place);
                WowVrCards::Face(Component, Eye, WowVrCards::DistanceScale(float(FVector::Dist(Eye, Place))));
                Component->SetVisibility(true);
            }
        }
        else if (VrCorpseCard.IsValid()) { DestroyCard(VrCorpseCard); VrCorpseCard.Reset(); }
    }

    // Chat bubbles: the bubble widget itself on a card above the speaker's name.
    {
        TArray<TPair<uint64, TSharedRef<SWidget>>> Bubbles;
        if (ChatBubbleLayer.IsValid()) ChatBubbleLayer->GetWorldBubbles(Bubbles);
        TSet<uint64> Seen;
        static const float BubbleRangeCm = 60.f * YardCm;
        for (const auto& Bubble : Bubbles)
        {
            const uint64 Guid = Bubble.Key;
            const AActor* Actor = Guid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid
                ? static_cast<const AActor*>(PlayerPawn) : SpawnedEntityActors.FindRef(Guid).Get();
            if (!IsValid(Actor) || FVector::DistSquared(Actor->GetActorLocation(), PlayerPawn->GetActorLocation()) > FMath::Square(BubbleRangeCm))
                continue;
            float Height = 200.f;
            if (const FWorldLabelAnchor* Anchor = WorldLabelAnchors.Find(Guid)) Height = Anchor->Height;
            else if (const USkeletalMeshComponent* Mesh = Actor->FindComponentByClass<USkeletalMeshComponent>())
                Height = float(Mesh->Bounds.GetBox().Max.Z - Actor->GetActorLocation().Z) + 16.f;
            Seen.Add(Guid);
            TWeakObjectPtr<UWidgetComponent>& Card = VrBubbleCards.FindOrAdd(Guid);
            if (!Card.IsValid())
                Card = WowVrCards::Make(VrCardHost(GetWorld(), PlayerPawn), TEXT("VrChatBubble"), FVector2D(320.f, 160.f), FVector2D(.5f, 1.f), true);
            UWidgetComponent* Component = Card.Get();
            if (!Component) continue;
            if (Component->GetSlateWidget().Get() != &Bubble.Value.Get()) Component->SetSlateWidget(Bubble.Value);
            // Above the nameplate (~45 px of name and bar at the plate's scale).
            const FVector Head = Actor->GetActorLocation() + FVector(0, 0, Height);
            const float Scale = WowVrCards::DistanceScale(float(FVector::Dist(Eye, Head)));
            Component->SetWorldLocation(Head + FVector(0, 0, 70.f * Scale));
            WowVrCards::Face(Component, Eye, Scale);
        }
        for (auto It = VrBubbleCards.CreateIterator(); It; ++It)
            if (!Seen.Contains(It.Key())) { DestroyCard(It.Value()); It.RemoveCurrent(); }
    }

    // World pointer: by default a small gaze dot 3 m ahead (the head is the pointer; clicks on the
    // world act on what it covers), or with "vrmousepointer" the mouse pointer itself where the
    // mouse ray points.
    {
        const bool bMouse = FWowClientPreferences::GetWorldOption(TEXT("vrmousepointer"));
        // Always the crosshair (Stefan: the mouse-pointer image showed as a black square on the
        // world card); with "vrmousepointer" it follows the mouse ray instead of the head. The
        // dwell ring around it only draws while a dwell click is filling.
        bVrPointerIsMouse = bMouse;
        if (!VrPointerCard.IsValid())
        {
            VrPointerCard = WowVrCards::Make(VrCardHost(GetWorld(), PlayerPawn), TEXT("VrPointer"), FVector2D(64.f, 64.f), FVector2D(.5f, .5f), false);
            if (UWidgetComponent* Card = VrPointerCard.Get())
            {
                // (No SetBackgroundColor: it rebuilt the card's material from its own instance - "MID is
                // not a valid parent" in the log - and the card drew as a black checkered square.)
                {
                    // Crosshair (Stefan: the small dot was too hard to see): four white arms with
                    // a black outline around an open centre, plus a white centre dot.
                    const FSlateBrush* White = FCoreStyle::Get().GetBrush("GenericWhiteBox");
                    auto Arm = [White](float W, float H, EHorizontalAlignment HA, EVerticalAlignment VA)
                    {
                        return SNew(SBox).HAlign(HA).VAlign(VA)
                            [ SNew(SBox).WidthOverride(W).HeightOverride(H)
                                [ SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 1.f)).Padding(2.f)
                                    [ SNew(SImage).Image(White).ColorAndOpacity(FLinearColor::White) ] ] ];
                    };
                    TWeakObjectPtr<AWowGameplayController> WeakThis(this);
                    // The game's own pointer shape (bag, cogwheel, sword, speech bubble ...) replaces the
                    // crosshair whenever it is not the plain pointer (Stefan 09.10); its tip (the image's
                    // top-left corner) sits in the middle of the card, where the crosshair centre is.
                    auto SpecialCursor = [WeakThis]()
                    {
                        return WeakThis.IsValid() && WeakThis->CursorManager && WeakThis->CursorManager->GetCursorType() != EWowCursorType::Point
                            && WeakThis->SoftwareCursorBrush.GetResourceObject() != nullptr;
                    };
                    auto CrosshairVisibility = [SpecialCursor]() { return SpecialCursor() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; };
                    Card->SetSlateWidget(SNew(SBox).WidthOverride(64.f).HeightOverride(64.f)
                        [ SNew(SOverlay)
                            + SOverlay::Slot()[ SNew(SWowDwellRing)
                                .Progress_Lambda([WeakThis]() { return WeakThis.IsValid() ? WeakThis->VrDwellProgress : 0.f; })
                                .Flash_Lambda([WeakThis]() { return WeakThis.IsValid() && FPlatformTime::Seconds() < WeakThis->VrDwellFlashUntil; }) ]
                            + SOverlay::Slot()[ SNew(SOverlay).Visibility_Lambda(CrosshairVisibility)
                                + SOverlay::Slot()[ Arm(8.f, 24.f, HAlign_Center, VAlign_Top) ]
                                + SOverlay::Slot()[ Arm(8.f, 24.f, HAlign_Center, VAlign_Bottom) ]
                                + SOverlay::Slot()[ Arm(24.f, 8.f, HAlign_Left, VAlign_Center) ]
                                + SOverlay::Slot()[ Arm(24.f, 8.f, HAlign_Right, VAlign_Center) ]
                                + SOverlay::Slot()[ Arm(10.f, 10.f, HAlign_Center, VAlign_Center) ] ]
                            + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(32.f, 32.f, 0.f, 0.f)
                            [ SNew(SBox).WidthOverride(30.f).HeightOverride(30.f)
                                .Visibility_Lambda([SpecialCursor]() { return SpecialCursor() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                                [ SNew(SImage).ColorAndOpacity(FLinearColor(.5f, .5f, .5f, 1.f)) // the card tint is 4x for the white crosshair
                                    .Image_Lambda([WeakThis]() -> const FSlateBrush* { return WeakThis.IsValid() ? &WeakThis->SoftwareCursorBrush : nullptr; }) ] ] ]);
                    // Brighter than the scene's exposure would leave plain white.
                    Card->SetTintColorAndOpacity(FLinearColor(4.f, 4.f, 4.f, 1.f));
                }
            }
        }
        if (UWidgetComponent* Card = VrPointerCard.Get())
        {
            FVector Origin, Direction;
            const bool bRay = GetWorldPointerRay(Origin, Direction);
            // With "aim with the mouse" the crosshair and the interface pointer are one pointer: over
            // a shown panel only the normal mouse pointer is drawn (on the panel), else the crosshair.
            // Over a whole panel (also between its buttons; the button-only test made the pointer
            // flip between the two in menus) the panel mouse pointer is used, not the crosshair.
            Card->SetVisibility(bRay && !bRightMouseLooking && !bLeftMouseLooking && !(bMouse && FWowUiVrHost::IsPointerOverShownPanel()));
            if (bRay)
            {
                // Looking at a panel (~1 m away) the crosshair at 3 m was hidden behind it, and so was
                // the stare ring: then draw it just in front of the panels, at the same angular size.
                FVector2D GazeOnPanel;
                // In the world: just in front of what the ray meets (Stefan 09.10: the pointer was cut by
                // the mob it pointed at, and by terrain), at most 3 m, so it is never behind it. Moving
                // closer is immediate, moving out again eases so the eyes are not jerked.
                static float GPointerDistance = 300.f;
                float Target = 300.f;
                if (!bMouse && FWowUiVrHost::GetGazePoint(GazeOnPanel)) Target = 85.f;
                else
                {
                    if (HoveredCursorGuid)
                        if (const TObjectPtr<AActor>* Hovered = SpawnedEntityActors.Find(HoveredCursorGuid); Hovered && *Hovered)
                        {
                            FVector Centre, Extent;
                            (*Hovered)->GetActorBounds(true, Centre, Extent);
                            Target = FMath::Min(Target, float(FVector::Dist(Origin, Centre) - Extent.Size()));
                        }
                    FHitResult Hit;
                    FCollisionQueryParams Query(SCENE_QUERY_STAT(VrPointerDistance), false, PlayerPawn);
                    if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * 300.f, ECC_Visibility, Query))
                        Target = FMath::Min(Target, Hit.Distance * .85f);
                    Target = FMath::Clamp(Target, 40.f, 300.f);
                }
                GPointerDistance = Target < GPointerDistance ? Target : FMath::FInterpTo(GPointerDistance, Target, DeltaTime, 6.f);
                const float Distance = GPointerDistance;
                Card->SetWorldLocation(Origin + Direction * Distance);
                WowVrCards::Face(Card, Eye, .25f * Distance / 300.f); // crosshair ~16 cm at 3 m (about 3 degrees)
            }
        }
    }

    // Dwell click (experimental, option "vrdwellclick"): the crosshair resting on the same
    // creature/player/object for the dwell time targets it like a left click. Not while moving or
    // mouse-looking; after a click, look away before the same thing can fire again.
    VrDwellProgress = 0.f;
    // r249 (Stefan 12.10: "stare-to-click does nothing any more"): what the stare sees, every 5 s.
    {
        static double GNextDwellLog = 0.0;
        const double LogNow = FPlatformTime::Seconds();
        if (LogNow >= GNextDwellLog)
        {
            GNextDwellLog = LogNow + 5.0;
            FVector2D LogGaze = FVector2D::ZeroVector; // (C4701)
            const bool bLogGaze = FWowUiVrHost::GetGazePoint(LogGaze);
            UE_LOG(LogWowVrCards, Log, TEXT("VR stare state: option=%d mousePointer=%d speed=%.0f rmb=%d lmb=%d gazeOnUi=%d (%.0f,%.0f) hovered=%llu dwellTime=%.2f"),
                FWowClientPreferences::GetWorldOption(TEXT("vrdwellclick")) ? 1 : 0, bVrPointerIsMouse ? 1 : 0,
                PlayerPawn->GetVelocity().Size2D(), bRightMouseLooking ? 1 : 0, bLeftMouseLooking ? 1 : 0,
                bLogGaze ? 1 : 0, LogGaze.X, LogGaze.Y, HoveredCursorGuid, FWowClientPreferences::GetVrDwellTime());
            // r250 (Stefan 12.10: after Recenter the crosshair sits a little right of and below the
            // middle of the view): the crosshair ray against the composed view and the head pose.
            FVector LogViewLoc; FRotator LogViewRot;
            GetPlayerViewPoint(LogViewLoc, LogViewRot);
            FQuat LogHead = FQuat::Identity; FVector LogHeadPos = FVector::ZeroVector;
            const bool bLogHead = FWowVRSupport::GetHeadPose(LogHead, LogHeadPos);
            FVector LogRayOrigin = FVector::ZeroVector, LogRayDir = FVector::ForwardVector;
            const bool bLogRay = GetWorldPointerRay(LogRayOrigin, LogRayDir);
            UE_LOG(LogWowVrCards, Log, TEXT("VR crosshair: view rot %s loc %s | ray %d rot %s origin %s | head %d rot %s pos %s | body yaw %.1f"),
                *LogViewRot.ToCompactString(), *LogViewLoc.ToCompactString(), bLogRay ? 1 : 0, *LogRayDir.Rotation().ToCompactString(),
                *LogRayOrigin.ToCompactString(), bLogHead ? 1 : 0, *LogHead.Rotator().ToCompactString(), *LogHeadPos.ToCompactString(),
                PlayerPawn->GetActorRotation().Yaw);
        }
    }
    if (FWowClientPreferences::GetWorldOption(TEXT("vrdwellclick")) && !bVrPointerIsMouse)
    {
        const double Now = FPlatformTime::Seconds();
        const bool bStill = !bRightMouseLooking && !bLeftMouseLooking && PlayerPawn->GetVelocity().Size2D() < 20.f;
        // On a panel: the head ray's point on the UI (reported by the presenter) resting within a
        // few pixels clicks there with the left button - window close buttons, tabs, buffs, bars.
        FVector2D Gaze = FVector2D::ZeroVector; // (C4701)
        const bool bGazeOnUi = bStill && FWowUiVrHost::GetGazePoint(Gaze);
        // Forgiving (Stefan: the head is never perfectly still, the ring kept restarting): the
        // gaze may wander 36 px around the point (which follows it slowly) and leave the panel or
        // the target for up to 0.3 s without restarting the ring.
        static double GUiGazeLostAt = 0.0, GWorldGazeLostAt = 0.0, GNextUiRepeat = 0.0;
        if (bGazeOnUi)
        {
            GUiGazeLostAt = 0.0;
            if (!bVrDwellUi || FVector2D::Distance(Gaze, VrDwellUiPoint) > 36.f)
            {
                bVrDwellUi = true;
                VrDwellUiPoint = Gaze;
                VrDwellStart = Now;
                bVrDwellFired = false;
            }
            else VrDwellUiPoint += (Gaze - VrDwellUiPoint) * .1f;
            if (!bVrDwellFired)
            {
                VrDwellProgress = float((Now - VrDwellStart) / FMath::Max(.1f, FWowClientPreferences::GetVrDwellTime()));
                if (VrDwellProgress >= 1.f)
                {
                    bVrDwellFired = true;
                    VrDwellProgress = 0.f;
                    VrDwellFlashUntil = Now + .25;
                    // Click where the gaze is now, not at the slowly following anchor (Stefan 09.10:
                    // the click landed above the Apply button he was looking at).
                    FWowUiVrHost::ClickAtWindowPoint(Gaze);
                    GNextUiRepeat = Now + .45;
                }
            }
            // A step button (slider < >) keeps stepping while you keep looking at it, like holding
            // the mouse button on it; any other button is clicked once.
            else if (Now >= GNextUiRepeat && FWowUiVrHost::IsRepeatButtonAt(Gaze))
            {
                FWowUiVrHost::ClickAtWindowPoint(Gaze);
                GNextUiRepeat = Now + .12;
            }
            VrDwellGuid = 0;
        }
        else if (bVrDwellUi)
        {
            if (GUiGazeLostAt <= 0.0) GUiGazeLostAt = Now;
            if (Now - GUiGazeLostAt > .3 || !bStill)
            {
                bVrDwellUi = false;
                bVrDwellFired = false;
            }
        }
        const bool bUiDwellHeld = bVrDwellUi && !bGazeOnUi; // brief look-off: neither UI nor world restarts
        const uint64 Under = bStill && !bGazeOnUi && !bUiDwellHeld ? HoveredCursorGuid : 0;
        if (Under == VrDwellGuid) GWorldGazeLostAt = 0.0;
        else if (Under == 0 && VrDwellGuid && bStill && (GWorldGazeLostAt <= 0.0 || Now - GWorldGazeLostAt <= .3))
        {
            if (GWorldGazeLostAt <= 0.0) GWorldGazeLostAt = Now; // keep the ring through a short slip off the target
        }
        else
        {
            VrDwellGuid = Under;
            VrDwellStart = Now;
            bVrDwellFired = false;
            GWorldGazeLostAt = 0.0;
        }
        if (VrDwellGuid && !bVrDwellFired && !bGazeOnUi && !bUiDwellHeld && HoveredCursorGuid == VrDwellGuid)
        {
            const float Need = FWowClientPreferences::GetVrDwellTime();
            VrDwellProgress = float((Now - VrDwellStart) / FMath::Max(.1f, Need));
            if (VrDwellProgress >= 1.f)
            {
                bVrDwellFired = true;
                VrDwellProgress = 0.f;
                VrDwellFlashUntil = Now + .25;
                uint64 Clicked = 0;
                TryTargetUnderCursor(false, &Clicked);
                // In the world a stare is a right click (Stefan): attack a monster, loot a body, talk
                // to an NPC, use an object or a mailbox. On the interface it is a left click (below).
                const bool bLoot = Clicked && DeadEntityGuids.Contains(Clicked);
                if (bLoot) SendLootRequest(Clicked);
                else if (Clicked) InteractWithWorldEntity(Clicked);
                UE_LOG(LogWowVrCards, Log, TEXT("VR stare right-click on %llu (hit %llu)%s"), VrDwellGuid, Clicked, bLoot ? TEXT(", loot") : TEXT(""));
            }
        }
    }
    else
    {
        VrDwellGuid = 0;
        bVrDwellFired = false;
        bVrDwellUi = false;
    }

    // Opening a window or the Esc menu in the headset puts the mouse pointer in its middle (Stefan:
    // "I want the cursor there, so I know where it is") - unless it is already on a shown panel.
    {
        static bool GWindowsShown = false;
        FVector2D Centre;
        const bool bShown = FWowUiVrHost::GetVisibleZoneCenter(EWowUiZone::Windows, Centre);
        if (bShown && !GWindowsShown && !FWowUiVrHost::IsPointerOverShownPanel())
            FWowUiVrHost::MoveCursorToWindowPoint(Centre);
        GWindowsShown = bShown;
        // The Esc menu: on its top button (Stefan 10.10: "in the middle of Video, the top choice"),
        // also when a window was already open. Its buttons have a size a frame or two after it shows.
        static bool GMenuShown = false;
        static double GMenuPendingUntil = 0.0;
        const bool bMenu = GameMenuViewportWidget.IsValid() && GameMenuViewportWidget->GetVisibility().IsVisible();
        if (bMenu && !GMenuShown) GMenuPendingUntil = FPlatformTime::Seconds() + .5;
        GMenuShown = bMenu;
        if (bMenu && GMenuPendingUntil > 0.0)
        {
            TFunction<TSharedPtr<SWidget>(const TSharedRef<SWidget>&, int32)> FirstButton =
                [&FirstButton](const TSharedRef<SWidget>& Widget, int32 Depth) -> TSharedPtr<SWidget>
            {
                if (Widget->GetType() == FName(TEXT("SButton")) && Widget->GetVisibility().IsVisible()) return Widget;
                if (Depth > 12) return nullptr;
                if (FChildren* Children = Widget->GetChildren())
                    for (int32 Index = 0; Index < Children->Num(); ++Index)
                        if (TSharedPtr<SWidget> Found = FirstButton(Children->GetChildAt(Index), Depth + 1)) return Found;
                return nullptr;
            };
            const TSharedPtr<SWidget> Button = FirstButton(GameMenuViewportWidget.ToSharedRef(), 0);
            const FGeometry Geometry = Button.IsValid() ? Button->GetCachedGeometry() : FGeometry();
            if (Button.IsValid() && Geometry.GetLocalSize().X > 1.f)
            {
                FWowUiVrHost::MoveCursorToWindowPoint(Geometry.GetAbsolutePositionAtCoordinates(FVector2D(.5f, .5f)));
                GMenuPendingUntil = 0.0;
            }
            else if (FPlatformTime::Seconds() > GMenuPendingUntil) GMenuPendingUntil = 0.0;
        }
    }

    // XP gain text: a card ahead of the body, a little above eye height (the flat one sits at
    // 38% of the screen height).
    if (VrXPGainWidget.IsValid())
    {
        if (!VrXPGainCard.IsValid())
        {
            VrXPGainCard = WowVrCards::Make(VrCardHost(GetWorld(), PlayerPawn), TEXT("VrXPGain"), FVector2D(640.f, 360.f), FVector2D(.5f, .5f), false);
            if (VrXPGainCard.IsValid()) VrXPGainCard->SetSlateWidget(VrXPGainWidget.ToSharedRef());
        }
        if (UWidgetComponent* Component = VrXPGainCard.Get())
        {
            const FVector Ahead = FRotator(0.f, PlayerPawn->GetActorRotation().Yaw, 0.f).Vector();
            Component->SetWorldLocation(Eye + Ahead * 180.f + FVector(0, 0, 25.f));
            WowVrCards::Face(Component, Eye, .16f);
        }
    }
}
