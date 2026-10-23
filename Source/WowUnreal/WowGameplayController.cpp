#include "WowGameplayController.h"
#include "WowLocalMount.h"
#include "WowPlayerTorchComponent.h"
#include "WowSpellMeshComponent.h"
#include "WowActorFadeComponent.h"
#include "WowFlybyCinematic.h"
#include "WowTrailerPlayer.h"
#include "WowColor.h"
#include "WowUiSurfaces.h"
#include "WowUiVrHost.h"
#include "ProceduralMeshComponent.h"
#include "WowPhotoMode.h"
#include "Widgets/SWowDestroyItemDialog.h"
#include "WowScreenshotManager.h"
#include "Misc/App.h"
#include "UI/SWowWorldMap.h"
#include "UI/SWowSkills.h"
#include "UI/SWowNpcServices.h"
#include "UI/SWowAuctionHouse.h"
#include "Widgets/SWowMacroFrame.h"
#include "WowMacros.h"
#include "UI/SWowUnitMenu.h"
#include "UI/SWowInspectFrame.h"
#include "UI/SWowAchievementFrame.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "WowMouseCapturePolicy.h"
#include "WowPerformanceScope.h"
#include "WowRemoteMovement.h"
#include "HAL/IConsoleManager.h"
#include "WowLoginPlacement.h"
#include "Components/Button.h"
#include "UI/SWowSpellbook.h"
#include "WowCameraFocusTrace.h"
#include "Camera/CameraTypes.h"
#include "WowWindowStyle.h"
#include "Widgets/SWowSessionCountdown.h"
#include "WowSplineSampling.h"
#include "UI/SWowWeatherLens.h"
#if PLATFORM_WINDOWS
#include "FFXFrameInterpolationModule.h"
#endif
#include "WowCookedMaterial.h"
#include "Camera/CameraActor.h"
#include "Slate/SGameLayerManager.h"
#include "Framework/Application/IInputProcessor.h"
#include "Components/CanvasPanelSlot.h"
#include "WowUnitPortrait.h"
#include "WowSpellPresentation.h"
#include "WowM2ParticleComponent.h"
#include "WowNodeGlintComponent.h"
#include "WowVrUiPresenter.h"
#include "WowSpellMeshComponent.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "WowUpdateFields.h"
#include "WowOpcodes.h"
#include "WowMovementPacket.h"
#include "WowPlayerCharacter.h"
#include "WowFirstPersonCameraPolicy.h"
#include "WowUIManager.h"
#include "Components/AudioComponent.h"
#include "SWowAddonList.h"
#include "WowFrameManager.h"
#include "WowItemPresentation.h"
#include "UObject/StrongObjectPtr.h"
#include "WowGameUI.h"
#include "WowEventSystem.h"
#include "WowBarberShop.h"
#include "WowWorldManager.h"
#include "WowSkyManager.h"
#include "WowClientPreferences.h"
#include "WowKeyBindings.h"
#include "WowScreenshotManager.h"
#include "UI/SWowKeyBindings.h"
#include "WowAudioManager.h"
#include "WowCharacterBuilder.h"
#include "WowCharacterPreview.h"
#include "WowAssetCache.h"
#include "WowTextureFactory.h"
#include "WowAnimationController.h"
#include "WowNameplateWidget.h"
#include "SWowCombatLog.h"
#include "SWowChatWindow.h"
#include "SWowDuelInvite.h"
#include "SWowPartyFrame.h"
#include "SWowPartyInvite.h"
#include "SWowTaxiMap.h"
#include "UI/SWowQuestDiamond.h"
#include "UI/SWowQuestLog.h"
#include "UI/SWowGroupFinder.h"
#include "UI/SWowGuildRoster.h"
#include "UI/SWowMailbox.h"
#include "UI/SWowFriendsFrame.h"
#include "UI/SWowChatBubbles.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "UI/SWowTalentWindow.h"
#include "WowDeathManager.h"
#include "WowStealthVisual.h"
#include "WowIsometricCamera.h"
static void WowAutoRetaliateTick(class AWowGameplayController* C); // r605, defined by OnServerAttackStart
#include "WowVRSupport.h"
#include "WowSatelliteTiles.h"
#include "WowCursorManager.h"
#include "WowSpellMissile.h"
#include "EngineUtils.h"
#include "WowDoodadManager.h"
#include "WowLightningShieldComponent.h"
#include "WowGroundTiltComponent.h"
#include "WowWmoRenderer.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/AdtTypes.h"
#include "Formats/BlpParser.h"
#include "Mpq/MpqManager.h"
#include "WowTooltipManager.h"
#include "WowSessionState.h"
#include "WowLoginController.h"
#include "SWowLoadingScreen.h"
#include "UI/SWowActionBar.h"
#include "UI/SWowMinimap.h"
#include "UI/SWowLoginOptions.h"
#include "UI/WowWotlkStyle.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Coord/WowCoordinate.h"
#include "Components/WidgetComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "WowFishingLineComponent.h"
#include "Components/BoxComponent.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Components/DecalComponent.h"
#include "Widgets/SViewport.h"
#include "SWowCastBar.h"
#include "WowFloatingText.h"
#include "UI/SWowXPGain.h"
#include "Formats/Dbc/DbcStore.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SceneCapture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "GenericPlatform/ICursor.h"
#include "Styling/AppStyle.h"
#include "Widgets/SWeakWidget.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "HAL/PlatformFileManager.h"
#include "DynamicRHI.h"
#include "RenderTimer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CoreDelegates.h"
#include "UnrealClient.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWowGameplay, Log, All);

// Object-type traces also hit the query-only water volumes (WorldDynamic boxes that fill
// lakes and rivers), so a unit under water could not be clicked. Skip those volumes.
// Enter/Escape for FrameXML edit boxes. The focused SEditableText consumes Enter itself
// (commit + focus loss), so the key never reached InputKey/OnEnterKey: chat text was not
// sent, /commands never ran and the box stayed open. Slate pre-processes the key here,
// before the text widget sees it, but only while a FrameXML edit box has keyboard focus.
class FWowEditBoxKeyInput final : public IInputProcessor
{
public:
    explicit FWowEditBoxKeyInput(TFunction<bool(const FKeyEvent&)> InHandler) : Handler(MoveTemp(InHandler)) {}
    virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
    {
        return Handler && Handler(Event);
    }
    virtual const TCHAR* GetDebugName() const override { return TEXT("WowEditBoxKeyInput"); }
private:
    TFunction<bool(const FKeyEvent&)> Handler;
};
// r374: the game object whose loot window (and lid) is open.
static uint64 GWowOpenedChestGuid = 0;
// r377: the race intro flyby while it plays.
static TWeakObjectPtr<AWowFlybyCinematic> GWowActiveFlyby;
static TSharedPtr<FWowEditBoxKeyInput> GWowEditBoxKeyInput;
// World mouseover tooltip state (one gameplay controller at a time).
static uint64 GWowWorldTooltipGuid = 0;
// Held item drawn as the pointer itself (original client): entry, its icon, and whether the
// software cursor brush currently shows it instead of the cursor art.
static uint32 GWowHeldIconEntry = 0;
static TWeakObjectPtr<UTexture2D> GWowHeldIconTexture;
static bool bGWowHeldIconCursor = false;
static bool bGWowWorldTooltipShown = false;
// World tooltip fade (GameTooltip:FadeOut): seconds when the mouse left the unit, 0 = none.
static double GWowWorldTooltipFadeStart = 0.0;

static bool WowTraceIgnoringWater(UWorld* World, FHitResult& Hit, const FVector& Start, const FVector& End,
    const FCollisionObjectQueryParams& Objects, FCollisionQueryParams Params)
{
    for (int32 Attempt = 0; Attempt < 8; ++Attempt)
    {
        if (!World->LineTraceSingleByObjectType(Hit, Start, End, Objects, Params)) return false;
        UPrimitiveComponent* Component = Hit.GetComponent();
        if (!Component || !Component->ComponentHasTag(TEXT("WowWaterVolume"))) return true;
        Params.AddIgnoredComponent(Component);
    }
    return false;
}

// A click that only grazes the first unit's edge (a Felhunter's wide body box) goes to a unit
// just behind it that the ray passes through near the middle (r271, Stefan: could not target the
// player behind the Felhunter). Graze = distance from the ray to the unit's click-capsule axis,
// in capsule radii (0 = dead centre, 1 = the edge).
static float WowRayGraze(AActor* Actor, const FVector& Start, const FVector& End)
{
    if (!Actor) return 99.f;
    TArray<UCapsuleComponent*> Capsules;
    Actor->GetComponents<UCapsuleComponent>(Capsules);
    for (UCapsuleComponent* Capsule : Capsules)
    {
        if (!Capsule || Capsule->GetFName() != TEXT("TargetCapsule")) continue;
        const float Radius = FMath::Max(1.f, Capsule->GetScaledCapsuleRadius());
        const float Half = FMath::Max(0.f, Capsule->GetScaledCapsuleHalfHeight() - Radius);
        const FVector Centre = Capsule->GetComponentLocation(), Up = Capsule->GetUpVector();
        FVector OnRay, OnAxis;
        FMath::SegmentDistToSegmentSafe(Start, End, Centre - Up * Half, Centre + Up * Half, OnRay, OnAxis);
        return float(FVector::Distance(OnRay, OnAxis)) / Radius;
    }
    return 99.f;
}

// r436 (Stefan: "pets still have a size problem" - a tamed worg twice a man's height): a creature's
// drawn size is the server's object scale times its display's scale (CreatureDisplayInfo). For
// hunter pets the server's scale already is the family's size by level (CreatureFamily), so a pet
// tamed from a big display (Saberworg 26331: 3.0) came out 3-4x too large. A unit owned by a player
// does not take a display scale above 1, like exotic pets on the server.
static float WowUnitActorScale(const FWowEntity& Entity)
{
    float Scale = Entity.Scale;
    const uint64 Owner = Entity.GetField64(UnitField::SUMMONEDBY);
    if (Owner != 0 && (Owner >> 48) == 0)
        if (const auto* Display = FDbcStore::Get().CreatureDisplayInfo().GetById(Entity.GetDisplayId()))
            if (Display->Scale > 1.f) Scale /= Display->Scale;
    return Scale;
}

static float WowTargetRadius(AActor* Actor)
{
    TArray<UCapsuleComponent*> Capsules;
    if (Actor) Actor->GetComponents<UCapsuleComponent>(Capsules);
    for (UCapsuleComponent* Capsule : Capsules)
        if (Capsule && Capsule->GetFName() == TEXT("TargetCapsule")) return FMath::Max(1.f, Capsule->GetScaledCapsuleRadius());
    return 0.f;
}

// r675 (Stefan: "unable to click this NPC, because the player steals the focus" - a gnome on a mechanostrider stood
// over a vendor; the cursor lay on the vendor's body, the mounted player's big click capsule took it): the original
// client picks the drawn models. A unit "covers" the cursor where the ray passes within a limb's thickness of one of
// its bones (its own skeleton and the skeletons attached to it - mount, rider); the depth along the ray is returned.
static bool WowRayBoneCover(AActor* Actor, const FVector& Start, const FVector& End, float& OutDepth)
{
    OutDepth = TNumericLimits<float>::Max();
    if (!Actor) return false;
    TArray<AActor*> Parts;
    Parts.Add(Actor);
    Actor->GetAttachedActors(Parts, false, true);
    const FVector Dir = (End - Start).GetSafeNormal();
    bool bCovered = false;
    for (AActor* Part : Parts)
    {
        if (!Part) continue;
        TInlineComponentArray<USkeletalMeshComponent*> Meshes(Part);
        for (USkeletalMeshComponent* Mesh : Meshes)
        {
            if (!Mesh || !Mesh->IsVisible() || !Mesh->GetSkeletalMeshAsset()) continue;
            const TArray<FTransform>& Space = Mesh->GetComponentSpaceTransforms();
            const FReferenceSkeleton& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
            const FTransform ToWorld = Mesh->GetComponentTransform();
            const float Thick = 22.f * FMath::Clamp(float(ToWorld.GetScale3D().GetMax()), 0.5f, 3.f);
            const int32 Count = FMath::Min(Space.Num(), Ref.GetNum());
            for (int32 Bone = 1; Bone < Count; ++Bone)
            {
                const int32 Parent = Ref.GetParentIndex(Bone);
                if (Parent < 0 || Parent >= Count) continue;
                if (Space[Parent].GetLocation().SizeSquared() < 1.0) continue; // a link from the model's origin on the ground
                const FVector A = ToWorld.TransformPosition(Space[Parent].GetLocation());
                const FVector B = ToWorld.TransformPosition(Space[Bone].GetLocation());
                FVector OnRay, OnBone;
                FMath::SegmentDistToSegmentSafe(Start, End, A, B, OnRay, OnBone);
                if (FVector::Distance(OnRay, OnBone) > Thick) continue;
                const float Depth = float(FVector::DotProduct(OnRay - Start, Dir));
                if (Depth < OutDepth) { OutDepth = Depth; bCovered = true; }
            }
        }
    }
    return bCovered;
}

static void WowPreferCentredUnit(UWorld* World, FHitResult& Hit, const FVector& Start, const FVector& End,
    const FCollisionObjectQueryParams& Objects, const FCollisionQueryParams& Params)
{
    if (!World || !Hit.GetActor()) return;
    // r675: among the units whose click capsules the ray meets close together, the one whose drawn body is in
    // front under the cursor wins. When no unit's body is under the cursor the capsule rules below decide.
    if (const float FirstReach = WowTargetRadius(Hit.GetActor()); FirstReach > 0.f)
    {
        TArray<FHitResult> Near;
        World->LineTraceMultiByObjectType(Near, Start, End, Objects, Params);
        float FrontDepth = 0.f;
        const bool bFirstCovers = WowRayBoneCover(Hit.GetActor(), Start, End, FrontDepth);
        const FHitResult* Front = nullptr;
        TSet<AActor*> Tested;
        for (const FHitResult& Other : Near)
        {
            AActor* Actor = Other.GetActor();
            if (!Actor || Actor == Hit.GetActor() || Tested.Contains(Actor) || WowTargetRadius(Actor) <= 0.f) continue;
            Tested.Add(Actor);
            if (Other.Distance > Hit.Distance + 2.f * FirstReach + 300.f) continue;
            float Depth = 0.f;
            if (WowRayBoneCover(Actor, Start, End, Depth) && Depth < FrontDepth) { FrontDepth = Depth; Front = &Other; }
        }
        if (Front) { Hit = *Front; return; }
        if (bFirstCovers) return;
    }
    const float First = WowRayGraze(Hit.GetActor(), Start, End);
    const float FirstRadius = WowTargetRadius(Hit.GetActor());
    // r430 (Stefan: could not click the flight master while a big hunter pet stood over him): a
    // large creature's click capsule swallows the small units standing in or around it, so the ray
    // always met the pet first. A unit much smaller than the first one that the ray passes through
    // near its middle, within the big one's reach, wins - the original client picks the drawn models.
    const bool bBigFirst = FirstRadius > 60.f;
    // r457 (Stefan: hard to target an NPC standing by a bonfire, "Bonfire" took all the attention):
    // a game object's model (bonfire, brazier, mailbox) caught the ray in front of a unit. When the
    // first hit is a game object, a unit the ray passes through (near its middle) behind it wins.
    const bool bObjectFirst = FirstRadius <= 0.f && Hit.GetActor()->Tags.Contains(TEXT("WowGameObjectActor"));
    if (First < 0.4f && !bBigFirst && !bObjectFirst) return;
    TArray<FHitResult> Hits;
    World->LineTraceMultiByObjectType(Hits, Start, End, Objects, Params);
    float Best = FMath::Min(First * 0.6f, 0.5f);
    float BestInside = 0.8f;
    float BestBehind = 0.85f;
    const FHitResult* Choice = nullptr;
    const FHitResult* Inside = nullptr;
    const FHitResult* Behind = nullptr;
    for (const FHitResult& Other : Hits)
    {
        AActor* Actor = Other.GetActor();
        if (!Actor || Actor == Hit.GetActor() || Other.Distance > Hit.Distance + FMath::Max(500.f, 2.f * FirstRadius + 100.f)) continue;
        if (Other.GetComponent() && Other.GetComponent()->ComponentHasTag(TEXT("WowWaterVolume"))) continue;
        const float Graze = WowRayGraze(Actor, Start, End);
        const float Radius = WowTargetRadius(Actor);
        if (bBigFirst && Radius > 0.f && Radius < FirstRadius * 0.6f && Graze < BestInside) { BestInside = Graze; Inside = &Other; }
        if (bObjectFirst && Radius > 0.f && Other.Distance <= Hit.Distance + 1500.f && Graze < BestBehind) { BestBehind = Graze; Behind = &Other; }
        if (Other.Distance <= Hit.Distance + 500.f && Graze < Best) { Best = Graze; Choice = &Other; }
    }
    if (Behind) Hit = *Behind;
    else if (Inside) Hit = *Inside;
    else if (Choice && First >= 0.4f) Hit = *Choice;
}

// r435 (Stefan: "I hearthed, but was thrown under the map"): after a long teleport (hearthstone,
// summons, portals on the same map) the pawn fell before the destination's terrain and buildings had
// streamed in, through the not-yet-built floor of the Goldshire inn. The pawn now hangs at the
// destination (no gravity) until there is ground under it, at most 20 s.
// r339 (Stefan 14.10: the objective tracker should be a real frame, for /fstack and addons): the
// stock FrameXML WatchFrame is the tracker now; the native Slate tracker stays as the fallback (cvar
// nativeQuestTracker 1) and in VR, where the FrameXML UI is not shown.
static bool GWowUseStockWatchFrame(const UWowUIManager* UI)
{
	if (FWowClientPreferences::GetWorldOption(TEXT("nativequesttracker")) || FWowUiVrHost::IsActive()) return false;
	const FWowFrameManager* Frames = UI ? UI->GetFrameManager() : nullptr;
	if (!Frames || Frames->FindFrame(TEXT("WatchFrame")) < 0) return false;
	// r386 (Stefan 16.10: "still no objective tracker, how do I get it?"): the stock WatchFrame shows
	// its header only while it lists something. While it shows nothing - it drew no line for the
	// tracked quests in 162236 - the native tracker is the fallback, as promised in r339.
	const int64 WatchHeader = Frames->FindFrame(TEXT("WatchFrameHeader"));
	return WatchHeader >= 0 && Frames->IsFrameEffectivelyShown(WatchHeader);
}

static bool GWowTeleportHold = false;
static double GWowTeleportHoldSince = 0.0;
static FVector GWowTeleportHoldAt = FVector::ZeroVector;
// r439 (Stefan: "might need a loading screen for it"): a far teleport (> 500 m, or into terrain that
// is not loaded) shows the transfer loading screen, like the original client, until the destination
// has streamed in and the floor exists.
static bool GWowTeleportScreen = false;
static double GWowTeleportStableSince = 0.0;
static uint64 GWowTeleportRevision = 0;

// Right-click on a body: the loot request waits for the release (no loot when the click was a turn).
static TFunction<void()> GWowPendingRightLoot;
static FRotator GWowRightLootPressRotation = FRotator::ZeroRotator;
static double GWowRightLootPressSeconds = 0.0;
// Follow (unit menu): the local player walks after the followed player, like the client's FollowUnit.
static uint64 GWowFollowGuid = 0;
// Objectives tracker: the quest pressed with Ctrl, toggled on release when it was not a Ctrl-drag.
static uint32 GWowPendingCtrlQuest = 0;
// The tracker's quest blocks (rebuilt with the tracker): a Ctrl-click anywhere on a quest's lines hits it.
static TArray<TPair<uint32, TWeakPtr<SWidget>>> GWowTrackerBlocks;
// The tracker's collapse button and the quest under the cursor (both driven from Tick).
static TWeakPtr<SWidget> GWowTrackerCollapseButton;
static uint32 GWowTrackerHoverQuest = 0;
// r324: the text given to the last /afk or /dnd (shown in "You are now AFK: <text>").
static FString GWowAwayMessage;
// Set when the window came back with a click on a UI button: input mode is restored after the release.
static bool GWowReapplyInputModeAfterActivationClick = false;
// First click after a focus change (Alt-Tab, task bar, snip tool). A press that reaches the game as
// an input event while it is foreground is a real click and is accepted everywhere. A press only
// seen by polling the physical button (the event was eaten by the activation) is routed too:
// on a FrameXML button it presses the button, over the world it targets on release (no camera).
static bool GWowSyntheticActivationPress = false;
// Set while asking which FrameXML frame is under the cursor regardless of native windows
// (bags drawn over the objectives tracker must get their clicks, not the tracker behind them).
static bool GWowSkipNativeUiCheck = false;
// The last refused cast whose reason was shown (one red line per cast).
static uint32 GWowLastCastErrorSpell = 0;
static double GWowLastCastErrorTime = 0.0;

// WotLK SpellCastResult (TrinityCore 3.3.5 SharedDefines.h order) -> the client's own GlobalStrings
// SPELL_FAILED_<NAME> text ("Interrupted", "Out of range", ...); empty for DONT_REPORT / CAST_OK.
// The built-in chat channels the client joins by itself (Stefan 10.10: "don't you automatically
// join channels? Especially in cities like Stormwind?"). Like the 3.3.5 client: every
// ChatChannels.dbc entry flagged INITIAL, "%s" filled with the zone's name (city-only ones,
// Trade and GuildRecruitment, use "City" and only inside a capital). Sent on login and on every
// zone change for the ones not joined yet; the server moves joined zone channels itself.
static void WowAutoJoinZoneChannels(FMpqManager* Mpq, UWowConnectionManager* Connection, uint32 ZoneId)
{
    if (!Mpq || !Connection || !ZoneId) return;
    struct FChannelDef { uint32 Id = 0; uint32 Flags = 0; FString Pattern; };
    static TArray<FChannelDef> Defs;
    static bool bLoaded = false;
    if (!bLoaded)
    {
        bLoaded = true;
        TArray<uint8> Bytes; FDbcParser Parser;
        // ChatChannels.dbc (3.3.5): ID, Flags, FactionGroup, Name_lang[17], Shortcut_lang[17].
        if (Mpq->ReadFile(TEXT("DBFilesClient\\ChatChannels.dbc"), Bytes) && Parser.Parse(Bytes) && Parser.GetFieldCount() >= 4)
            for (uint32 Row = 0; Row < Parser.GetRecordCount(); ++Row)
                Defs.Add({Parser.GetUInt(Row, 0), Parser.GetUInt(Row, 1), Parser.GetString(Row, 3)});
        UE_LOG(LogWowGameplay, Log, TEXT("ChatChannels.dbc: %d channels"), Defs.Num());
    }
    const FAreaTableDbcEntry* Zone = FDbcStore::Get().AreaTable().GetById(ZoneId);
    for (int32 Guard = 0; Zone && Zone->ParentAreaID && Guard < 4; ++Guard) Zone = FDbcStore::Get().AreaTable().GetById(Zone->ParentAreaID);
    if (!Zone) return;
    const bool bCity = (Zone->Flags & (0x8u | 0x200u)) != 0; // AREA_FLAG_SLAVE_CAPITAL / CAPITAL
    constexpr uint32 FlagInitial = 0x1, FlagZoneDep = 0x2, FlagCityOnly = 0x10 | 0x20, FlagLfg = 0x40000;
    for (const FChannelDef& Def : Defs)
    {
        if (!(Def.Flags & FlagInitial) || (Def.Flags & FlagLfg) || Def.Pattern.IsEmpty()) continue;
        if ((Def.Flags & FlagCityOnly) && !bCity) continue;
        if (Connection->PacketHandler.JoinedChannelIds.Contains(Def.Id)) continue;
        FString Name = Def.Pattern;
        if (Name.Contains(TEXT("%s")))
            Name = Name.Replace(TEXT("%s"), (Def.Flags & FlagCityOnly) ? TEXT("City") : *Zone->Name);
        else if (!(Def.Flags & FlagZoneDep) && Connection->PacketHandler.GetChannelNumber(Name) != 0) continue;
        // CMSG_JOIN_CHANNEL: uint32 ChatChannels id, uint8 voice, uint8 joined-by-zone, name, password.
        TArray<uint8> Data;
        for (int32 Byte = 0; Byte < 4; ++Byte) Data.Add(uint8(Def.Id >> (8 * Byte)));
        Data.Add(0); Data.Add(0);
        const FTCHARToUTF8 Utf8(*Name);
        Data.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()); Data.Add(0);
        Data.Add(0);
        Connection->SendRawPacket(0x097, Data);
        UE_LOG(LogWowGameplay, Log, TEXT("Auto-joining channel %u '%s'"), Def.Id, *Name);
    }
}

static FString WowSpellCastResultText(const UWowUIManager* UI, uint8 Reason)
{
    // r277: SpellCastResult as the 3.3.5 server sends it (SharedDefines.h): 0 is SUCCESS and there is no
    // FALLING, so the old list read every code below 33 one too low (12 BAD_TARGETS showed "Target can't
    // be charmed", 27 DONT_REPORT showed "Must have the proper item equipped" - Stefan 13.10).
    static const TCHAR* const SpellCastResultNames[] = {TEXT("SUCCESS"),TEXT("AFFECTING_COMBAT"),TEXT("ALREADY_AT_FULL_HEALTH"),TEXT("ALREADY_AT_FULL_MANA"),TEXT("ALREADY_AT_FULL_POWER"),TEXT("ALREADY_BEING_TAMED"),TEXT("ALREADY_HAVE_CHARM"),TEXT("ALREADY_HAVE_SUMMON"),TEXT("ALREADY_OPEN"),TEXT("AURA_BOUNCED"),TEXT("AUTOTRACK_INTERRUPTED"),TEXT("BAD_IMPLICIT_TARGETS"),TEXT("BAD_TARGETS"),TEXT("CANT_BE_CHARMED"),TEXT("CANT_BE_DISENCHANTED"),TEXT("CANT_BE_DISENCHANTED_SKILL"),TEXT("CANT_BE_MILLED"),TEXT("CANT_BE_PROSPECTED"),TEXT("CANT_CAST_ON_TAPPED"),TEXT("CANT_DUEL_WHILE_INVISIBLE"),TEXT("CANT_DUEL_WHILE_STEALTHED"),TEXT("CANT_STEALTH"),TEXT("CASTER_AURASTATE"),TEXT("CASTER_DEAD"),TEXT("CHARMED"),TEXT("CHEST_IN_USE"),TEXT("CONFUSED"),TEXT("DONT_REPORT"),TEXT("EQUIPPED_ITEM"),TEXT("EQUIPPED_ITEM_CLASS"),TEXT("EQUIPPED_ITEM_CLASS_MAINHAND"),TEXT("EQUIPPED_ITEM_CLASS_OFFHAND"),TEXT("ERROR"),TEXT("FIZZLE"),TEXT("FLEEING"),TEXT("FOOD_LOWLEVEL"),TEXT("HIGHLEVEL"),TEXT("HUNGER_SATIATED"),TEXT("IMMUNE"),TEXT("INCORRECT_AREA"),TEXT("INTERRUPTED"),TEXT("INTERRUPTED_COMBAT"),TEXT("ITEM_ALREADY_ENCHANTED"),TEXT("ITEM_GONE"),TEXT("ITEM_NOT_FOUND"),TEXT("ITEM_NOT_READY"),TEXT("LEVEL_REQUIREMENT"),TEXT("LINE_OF_SIGHT"),TEXT("LOWLEVEL"),TEXT("LOW_CASTLEVEL"),TEXT("MAINHAND_EMPTY"),TEXT("MOVING"),TEXT("NEED_AMMO"),TEXT("NEED_AMMO_POUCH"),TEXT("NEED_EXOTIC_AMMO"),TEXT("NEED_MORE_ITEMS"),TEXT("NOPATH"),TEXT("NOT_BEHIND"),TEXT("NOT_FISHABLE"),TEXT("NOT_FLYING"),TEXT("NOT_HERE"),TEXT("NOT_INFRONT"),TEXT("NOT_IN_CONTROL"),TEXT("NOT_KNOWN"),TEXT("NOT_MOUNTED"),TEXT("NOT_ON_TAXI"),TEXT("NOT_ON_TRANSPORT"),TEXT("NOT_READY"),TEXT("NOT_SHAPESHIFT"),TEXT("NOT_STANDING"),TEXT("NOT_TRADEABLE"),TEXT("NOT_TRADING"),TEXT("NOT_UNSHEATHED"),TEXT("NOT_WHILE_GHOST"),TEXT("NOT_WHILE_LOOTING"),TEXT("NO_AMMO"),TEXT("NO_CHARGES_REMAIN"),TEXT("NO_CHAMPION"),TEXT("NO_COMBO_POINTS"),TEXT("NO_DUELING"),TEXT("NO_ENDURANCE"),TEXT("NO_FISH"),TEXT("NO_ITEMS_WHILE_SHAPESHIFTED"),TEXT("NO_MOUNTS_ALLOWED"),TEXT("NO_PET"),TEXT("NO_POWER"),TEXT("NOTHING_TO_DISPEL"),TEXT("NOTHING_TO_STEAL"),TEXT("ONLY_ABOVEWATER"),TEXT("ONLY_DAYTIME"),TEXT("ONLY_INDOORS"),TEXT("ONLY_MOUNTED"),TEXT("ONLY_NIGHTTIME"),TEXT("ONLY_OUTDOORS"),TEXT("ONLY_SHAPESHIFT"),TEXT("ONLY_STEALTHED"),TEXT("ONLY_UNDERWATER"),TEXT("OUT_OF_RANGE"),TEXT("PACIFIED"),TEXT("POSSESSED"),TEXT("REAGENTS"),TEXT("REQUIRES_AREA"),TEXT("REQUIRES_SPELL_FOCUS"),TEXT("ROOTED"),TEXT("SILENCED"),TEXT("SPELL_IN_PROGRESS"),TEXT("SPELL_LEARNED"),TEXT("SPELL_UNAVAILABLE"),TEXT("STUNNED"),TEXT("TARGETS_DEAD"),TEXT("TARGET_AFFECTING_COMBAT"),TEXT("TARGET_AURASTATE"),TEXT("TARGET_DUELING"),TEXT("TARGET_ENEMY"),TEXT("TARGET_ENRAGED"),TEXT("TARGET_FRIENDLY"),TEXT("TARGET_IN_COMBAT"),TEXT("TARGET_IS_PLAYER"),TEXT("TARGET_IS_PLAYER_CONTROLLED"),TEXT("TARGET_NOT_DEAD"),TEXT("TARGET_NOT_IN_PARTY"),TEXT("TARGET_NOT_LOOTED"),TEXT("TARGET_NOT_PLAYER"),TEXT("TARGET_NO_POCKETS"),TEXT("TARGET_NO_WEAPONS"),TEXT("TARGET_NO_RANGED_WEAPONS"),TEXT("TARGET_UNSKINNABLE"),TEXT("THIRST_SATIATED"),TEXT("TOO_CLOSE"),TEXT("TOO_MANY_OF_ITEM"),TEXT("TOTEM_CATEGORY"),TEXT("TOTEMS"),TEXT("TRY_AGAIN"),TEXT("UNIT_NOT_BEHIND"),TEXT("UNIT_NOT_INFRONT"),TEXT("WRONG_PET_FOOD"),TEXT("NOT_WHILE_FATIGUED"),TEXT("TARGET_NOT_IN_INSTANCE"),TEXT("NOT_WHILE_TRADING"),TEXT("TARGET_NOT_IN_RAID"),TEXT("TARGET_FREEFORALL"),TEXT("NO_EDIBLE_CORPSES"),TEXT("ONLY_BATTLEGROUNDS"),TEXT("TARGET_NOT_GHOST"),TEXT("TRANSFORM_UNUSABLE"),TEXT("WRONG_WEATHER"),TEXT("DAMAGE_IMMUNE"),TEXT("PREVENTED_BY_MECHANIC"),TEXT("PLAY_TIME"),TEXT("REPUTATION"),TEXT("MIN_SKILL"),TEXT("NOT_IN_ARENA"),TEXT("NOT_ON_SHAPESHIFT"),TEXT("NOT_ON_STEALTHED"),TEXT("NOT_ON_DAMAGE_IMMUNE"),TEXT("NOT_ON_MOUNTED"),TEXT("TOO_SHALLOW"),TEXT("TARGET_NOT_IN_SANCTUARY"),TEXT("TARGET_IS_TRIVIAL"),TEXT("BM_OR_INVISGOD"),TEXT("EXPERT_RIDING_REQUIREMENT"),TEXT("ARTISAN_RIDING_REQUIREMENT"),TEXT("NOT_IDLE"),TEXT("NOT_INACTIVE"),TEXT("PARTIAL_PLAYTIME"),TEXT("NO_PLAYTIME"),TEXT("NOT_IN_BATTLEGROUND"),TEXT("NOT_IN_RAID_INSTANCE"),TEXT("ONLY_IN_ARENA"),TEXT("TARGET_LOCKED_TO_RAID_INSTANCE"),TEXT("ON_USE_ENCHANT"),TEXT("NOT_ON_GROUND"),TEXT("CUSTOM_ERROR"),TEXT("CANT_DO_THAT_RIGHT_NOW"),TEXT("TOO_MANY_SOCKETS"),TEXT("INVALID_GLYPH"),TEXT("UNIQUE_GLYPH"),TEXT("GLYPH_SOCKET_LOCKED"),TEXT("NO_VALID_TARGETS"),TEXT("ITEM_AT_MAX_CHARGES"),TEXT("NOT_IN_BARBERSHOP"),TEXT("FISHING_TOO_LOW"),TEXT("ITEM_ENCHANT_TRADE_WINDOW"),TEXT("SUMMON_PENDING"),TEXT("MAX_SOCKETS"),TEXT("PET_CAN_RENAME"),TEXT("TARGET_CANNOT_BE_RESURRECTED"),TEXT("UNKNOWN")};
    if (Reason == 0 || Reason == 27 || Reason == 255) return FString(); // SUCCESS / DONT_REPORT / CAST_OK
    const FString Name = Reason < UE_ARRAY_COUNT(SpellCastResultNames) ? SpellCastResultNames[Reason] : TEXT("UNKNOWN");
    FString Message = UI ? UI->GetGlobalString(TEXT("SPELL_FAILED_") + Name) : FString();
    if (Message.IsEmpty())
    {
        Message = Name.Replace(TEXT("_"), TEXT(" ")).ToLower();
        if (!Message.IsEmpty()) Message[0] = FChar::ToUpper(Message[0]);
    }
    return Message;
}

static bool GWowActivationWorldClick = false;
static bool GWowActivationUiPress = false;
static FVector2D GWowActivationClickPosition = FVector2D::ZeroVector;

static bool IsWowWorldOccluded(UWorld* World, const FVector& From, const FVector& To,
    AActor* Viewer, AActor* Subject)
{
    if (!World) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WowWorldOcclusion), true);
    Query.AddIgnoredActor(Viewer);
    Query.AddIgnoredActor(Subject);
    FWowIsometricCamera::IgnoreCutSurfaces(Query); // r618: cut-away roofs and trees do not block clicks
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    // r619 (Stefan: "I can't click anyone inside"): in the isometric camera a hit on geometry inside the
    // cut-away (roof, ceiling, collision shells that are not drawn there) does not count; look on past it.
    FVector Start = From;
    for (int32 Step = 0; Step < 6; ++Step)
    {
        FHitResult Hit;
        if (!WowTraceIgnoringWater(World, Hit, Start, To, Objects, Query)) return false;
        if (Hit.Distance + 2.0f >= FVector::Distance(Start, To)) return false;
        if (!FWowIsometricCamera::IsCutAway(Hit.ImpactPoint)) return true;
        Start = Hit.ImpactPoint + (To - From).GetSafeNormal() * 5.0f;
        if (UPrimitiveComponent* Blocker = Hit.GetComponent()) Query.AddIgnoredComponent(Blocker);
    }
    return false;
}

// r621 (Stefan: "right clicking is turning my character the wrong way ... WoW had click-to-move, so it
// turns when you right-click"; "right-drag while moving causes a lot of sliding"; "disable camera
// right-drag with this camera"): in the isometric camera the right button no longer turns the camera
// or the character. Right-click on the ground walks there (hold the button to keep steering towards
// the cursor); right-click on an NPC, object, enemy or body walks up to it and then interacts, loots
// or attacks. Any movement key, jumping or getting stuck stops it.
namespace WowIsoClickMove
{
    bool GActive = false;
    bool GHolding = false;
    FVector GTarget = FVector::ZeroVector;
    uint64 GGuid = 0;
    FVector GLastProgressPos = FVector::ZeroVector;
    double GLastProgressSeconds = 0.0;
    double GLastSteerSeconds = 0.0;
    double GArrivedAt = 0.0; // r633: arrived at the clicked unit/object; acting once the character has stopped
    void Stop() { GActive = false; GHolding = false; GGuid = 0; GArrivedAt = 0.0; }
    // r628: the right button acts on release, and only when it was a click (a right drag turns the camera).
    bool GPressPending = false;
    FVector2D GPressCursor = FVector2D::ZeroVector;
    uint64 GPressGuid = 0;
    // r650 (Stefan: "left click in isometric view also must target"): r637 took left-drag camera look out of the
    // isometric camera, and with it the target click that look mode resolved on release. A left press over the
    // world now targets on release when the mouse stayed put and both buttons were never held together.
    bool GLeftPress = false;
    bool GLeftChorded = false;
    FVector2D GLeftPressCursor = FVector2D::ZeroVector;
}

/** The ground (terrain, buildings) under the cursor, looking past the isometric cut-away. */
static bool WowIsoGroundUnderCursor(APlayerController* PC, FVector& OutPoint)
{
    UWorld* World = PC ? PC->GetWorld() : nullptr;
    FVector Origin, Direction;
    if (!World || !PC->DeprojectMousePositionToWorld(Origin, Direction)) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WowIsoClickMove), false);
    if (APawn* Pawn = PC->GetPawn()) Query.AddIgnoredActor(Pawn);
    FWowIsometricCamera::IgnoreCutSurfaces(Query);
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    FVector Start = Origin;
    const FVector End = Origin + Direction * 200000.0f;
    for (int32 Step = 0; Step < 8; ++Step)
    {
        FHitResult Hit;
        if (!WowTraceIgnoringWater(World, Hit, Start, End, Objects, Query)) return false;
        if (!FWowIsometricCamera::IsCutAway(Hit.ImpactPoint)) { OutPoint = Hit.ImpactPoint; return true; }
        Start = Hit.ImpactPoint + Direction * 5.0f;
        if (UPrimitiveComponent* Blocker = Hit.GetComponent()) Query.AddIgnoredComponent(Blocker);
    }
    return false;
}

bool WowIsHdUpgraded(uint64 Guid); // r331, WowGameplayControllerHd.cpp (global, outside the anonymous namespaces)

namespace
{
// Typed event arguments understood by FWowEventSystem::PushEventArgument.
FString CombatLogNumber(double Value)
{
	FString Arg; Arg.AppendChar(TCHAR(1)); Arg.AppendChar(TEXT('n'));
	Arg += FString::Printf(TEXT("%.3f"), Value);
	return Arg;
}
FString CombatLogNil()
{
	FString Arg; Arg.AppendChar(TCHAR(1)); Arg.AppendChar(TEXT('z'));
	return Arg;
}
FString CombatLogOptional(int32 Value) { return Value > 0 ? CombatLogNumber(Value) : CombatLogNil(); }
FString CombatLogFlag(bool bValue) { return bValue ? CombatLogNumber(1) : CombatLogNil(); }
FString CombatLogGuid(uint64 Guid) { return FString::Printf(TEXT("0x%016llX"), Guid); }
}

namespace
{
constexpr int32 MinimapCaptureResolution = 256;
constexpr float MinimapCaptureOrthoWidth = 14500.0f;
constexpr float MinimapCaptureLodDistanceFactor = 4.0f;
constexpr float EntityModelStreamingRangeCm = 30000.0f;

// SpellVisualKitModelAttach.dbc (3.3.5): ID, ParentSpellVisualKitID, SpellVisualEffectNameID,
// AttachmentID, Offset xyz, Yaw, Pitch, Roll. Read straight from the MPQs on first use (r262).
struct FWowSpellModelAttach
{
    uint32 EffectNameId = 0;
    uint32 AttachmentId = 0;
    FVector Offset = FVector::ZeroVector;
    float Yaw = 0, Pitch = 0, Roll = 0;
};

const TArray<FWowSpellModelAttach>& GetSpellModelAttaches(FMpqManager* Mpq, uint32 KitId)
{
    static TMap<uint32, TArray<FWowSpellModelAttach>> ByKit;
    static bool bLoaded = false;
    static const TArray<FWowSpellModelAttach> None;
    if (!bLoaded && Mpq)
    {
        bLoaded = true;
        TArray<uint8> Data;
        if (Mpq->ReadFile(TEXT("DBFilesClient\\SpellVisualKitModelAttach.dbc"), Data) && Data.Num() >= 20
            && FMemory::Memcmp(Data.GetData(), "WDBC", 4) == 0)
        {
            uint32 Header[4];
            FMemory::Memcpy(Header, Data.GetData() + 4, 16); // records, fields, record size, string size
            const uint32 Records = Header[0], RecordSize = Header[2];
            if (Header[1] >= 10 && RecordSize >= 40 && 20 + uint64(Records) * RecordSize <= uint64(Data.Num()))
                for (uint32 Index = 0; Index < Records; ++Index)
                {
                    const uint8* Row = Data.GetData() + 20 + Index * RecordSize;
                    uint32 U[4]; float F[6];
                    FMemory::Memcpy(U, Row, 16);
                    FMemory::Memcpy(F, Row + 16, 24);
                    FWowSpellModelAttach Attach;
                    Attach.EffectNameId = U[2];
                    Attach.AttachmentId = U[3];
                    Attach.Offset = FVector(F[0], F[1], F[2]);
                    Attach.Yaw = F[3]; Attach.Pitch = F[4]; Attach.Roll = F[5];
                    ByKit.FindOrAdd(U[1]).Add(Attach);
                }
            UE_LOG(LogTemp, Log, TEXT("SpellVisualKitModelAttach: %u rows for %d kits"), Records, ByKit.Num());
        }
    }
    const TArray<FWowSpellModelAttach>* Found = ByKit.Find(KitId);
    return Found ? *Found : None;
}

FString NormalizeGameObjectModelPath(FString ModelPath)
{
	ModelPath.TrimStartAndEndInline();
	ModelPath.ReplaceInline(TEXT("/"), TEXT("\\"));
	if (ModelPath.EndsWith(TEXT(".mdx"), ESearchCase::IgnoreCase) ||
		ModelPath.EndsWith(TEXT(".mdl"), ESearchCase::IgnoreCase))
	{
		ModelPath = ModelPath.LeftChop(4) + TEXT(".m2");
	}
	return ModelPath;
}

// Build-12340 aura types (SpellAuraDefines): actual concealment, not talents
// that merely increase stealth level, and not the mage invisibility fade-in.
constexpr uint8 ConcealmentStealth = 1;
constexpr uint8 ConcealmentInvisibility = 2;
constexpr uint32 AuraModStealth = 16;
constexpr uint32 AuraModStealthDetect = 17;
constexpr uint32 AuraModInvisibility = 18;
constexpr uint32 AuraModInvisibilityDetect = 19;
constexpr uint32 AuraDetectStealth = 228;

uint8 GetUnitConcealmentMask(const FWowUnitEntity* Unit)
{
    uint8 Mask = 0;
    if (!Unit || Unit->GetHealth() == 0) return Mask;
    for (const FAuraInfo& Aura : Unit->Auras)
    {
        if (!Aura.bActive) continue;
        const auto* Spell = FDbcStore::Get().Spells().GetById(Aura.SpellId);
        if (!Spell) continue;
        for (uint32 Effect = 0; Effect < 3; ++Effect)
        {
            if ((Aura.Flags & (1u << Effect)) == 0) continue;
            if (Spell->EffectApplyAuraName[Effect] == AuraModStealth) Mask |= ConcealmentStealth;
            if (Spell->EffectApplyAuraName[Effect] == AuraModInvisibility) Mask |= ConcealmentInvisibility;
        }
    }
    return Mask;
}

bool ShouldShowNpcAuraVisual(const FWowUnitEntity& Unit, const FAuraInfo& Aura, uint8 ViewerConcealment)
{
    if (Unit.IsPlayer()) return true;
    const FDbcStore& Dbc = FDbcStore::Get();
    const auto* Spell = Dbc.Spells().GetById(Aura.SpellId);
    if (!Spell) return true;
    uint8 Detection = 0;
    for (uint32 Effect = 0; Effect < 3; ++Effect)
    {
        if ((Aura.Flags & (1u << Effect)) == 0) continue;
        const uint32 Type = Spell->EffectApplyAuraName[Effect];
        if (Type == AuraModStealthDetect || Type == AuraDetectStealth) Detection |= ConcealmentStealth;
        if (Type == AuraModInvisibilityDetect) Detection |= ConcealmentInvisibility;
    }
    if (Detection == 0) return true;
    const auto* Visual = Dbc.SpellVisuals().GetById(Spell->SpellVisual[0] ? Spell->SpellVisual[0] : Spell->SpellVisual[1]);
    const auto* Kit = Visual ? Dbc.SpellVisualKits().GetById(Visual->StateKit) : nullptr;
    const auto* Head = Kit ? Dbc.SpellVisualEffectNames().GetById(Kit->HeadEffectID) : nullptr;
    // The model is also used for quest tagging: require a real detection aura.
    // Do not suppress other buff effects or special player spell visuals.
    if (!Head || !Head->FilePath.Contains(TEXT("DetectStealth_State_Head"), ESearchCase::IgnoreCase)) return true;
    return Unit.GetHealth() > 0 && (ViewerConcealment & Detection) != 0;
}

FVector GameObjectModelPointToUELocal(const FVector& ModelPoint, float Scale)
{
	return FVector(ModelPoint.Y, ModelPoint.X, ModelPoint.Z) * FWowCoordinate::SCALE * Scale;
}

uint32 GetPlayerAppearanceSignature(const FWowPlayerEntity& Player)
{
	uint32 Signature = HashCombineFast(Player.GetPlayerBytes(), Player.GetPlayerBytes2() & 0xffu);
	Signature = HashCombineFast(Signature, Player.GetMountDisplayId());
	for (uint8 Slot = 0; Slot < 19; ++Slot)
	{
		Signature = HashCombineFast(Signature,
			Player.GetField(PlayerField::VISIBLE_ITEM_BASE + Slot * 2));
        Signature = HashCombineFast(Signature, Player.GetField(PlayerField::VISIBLE_ITEM_BASE + Slot * 2 + 1));
	}
	// r224: Show Helm / Show Cloak (PLAYER_FLAGS_HIDE_HELM 0x400, HIDE_CLOAK 0x800) rebuild the model.
	Signature = HashCombineFast(Signature, Player.GetPlayerFlags() & 0xC00u);
	// r343: a Guild Tabard is rebuilt once its guild's design arrives.
	if (Player.GetField(PlayerField::VISIBLE_ITEM_BASE + PlayerField::EQUIPMENT_SLOT_TABARD * 2) == 5976)
		Signature = HashCombineFast(Signature, FWowPacketHandler::GetGuildTabardDesign(Player.GetField(0x97))); // PLAYER_GUILDID
	return Signature;
}

// Dressed NPCs (r257, Stefan: Torment's "Rift Keeper" stood naked): units flagged
// UNIT_FLAG2_MIRROR_IMAGE are drawn as a player character from SMSG_MIRRORIMAGE_DATA.
TMap<uint64, FWowMirrorImageData> GMirrorImages;
TSet<uint64> GMirrorImageRequests;

FWowCharacterBuilder::FCharacterParams BuildMirrorImageCharacterParams(const FWowMirrorImageData& Mirror, const FWowEntity& Unit)
{
    FWowCharacterBuilder::FCharacterParams Params;
    Params.bLoadAnimationsOnDemand = true;
    Params.bAsyncEquipmentAtlas = true; // r311
    Params.Race = static_cast<FWowCharacterBuilder::ERace>(Mirror.Race);
    Params.Gender = static_cast<FWowCharacterBuilder::EGender>(Mirror.Gender);
    Params.Customization.RaceId = Mirror.Race;
    Params.Customization.Gender = Mirror.Gender;
    Params.Customization.SkinColor = Mirror.Skin;
    Params.Customization.FaceVariation = Mirror.Face;
    Params.Customization.HairStyle = Mirror.Hair;
    Params.Customization.HairColor = Mirror.HairColor;
    Params.Customization.FacialHairStyle = Mirror.FacialHair;
    Params.BodyEquipment.RaceId = Mirror.Race;
    Params.BodyEquipment.Gender = Mirror.Gender;
    const uint32* D = Mirror.ItemDisplay;
    auto AddAttached = [&Params](uint32 Display, FWowEquipmentManager::EAttachmentPoint Point)
    {
        FWowCharacterBuilder::FEquipmentSlot Slot;
        Slot.ItemDisplayId = Display;
        Slot.AttachPoint = Point;
        Params.Equipment.Add(Slot);
    };
    if (D[0]) AddAttached(D[0], FWowEquipmentManager::EAttachmentPoint::Helmet);
    if (D[1])
    {
        AddAttached(D[1], FWowEquipmentManager::EAttachmentPoint::LeftShoulder);
        AddAttached(D[1], FWowEquipmentManager::EAttachmentPoint::RightShoulder);
    }
    Params.BodyEquipment.ShirtDisplayId = D[2];
    Params.BodyEquipment.ChestDisplayId = D[3];
    if (const FItemDisplayInfoDbcEntry* Chest = D[3] ? FDbcStore::Get().ItemDisplayInfo().GetById(D[3]) : nullptr)
        Params.BodyEquipment.bIsRobeEquipped = Chest->GeosetGroups[2] > 0;
    Params.BodyEquipment.BeltDisplayId = D[4];
    Params.BodyEquipment.PantsDisplayId = D[5];
    Params.BodyEquipment.BootsDisplayId = D[6];
    Params.BodyEquipment.BracersDisplayId = D[7];
    Params.BodyEquipment.GlovesDisplayId = D[8];
    Params.BodyEquipment.CapeDisplayId = D[9];
    Params.BodyEquipment.TabardDisplayId = D[10];
    // Weapons come from the unit's virtual item slots (item entries), as for any creature.
    for (uint16 WeaponSlot = 0; WeaponSlot < 3; ++WeaponSlot)
    {
        const FItemDbcEntry* Item = FDbcStore::Get().Items().GetById(Unit.GetField(UnitField::BASE + 0x0032 + WeaponSlot));
        if (!Item || !Item->DisplayInfoID) continue;
        FWowCharacterBuilder::FEquipmentSlot Slot;
        Slot.ItemDisplayId = Item->DisplayInfoID;
        Slot.InventoryType = static_cast<uint8>(Item->InventoryType);
        Slot.SheathType = static_cast<uint8>(Item->SheathType);
        Slot.bRangedWeapon = WeaponSlot == 2;
        Slot.AttachPoint = WeaponSlot == 1
            ? (Item->InventoryType == 14 ? FWowEquipmentManager::EAttachmentPoint::ShieldHand : FWowEquipmentManager::EAttachmentPoint::LeftHand)
            : FWowEquipmentManager::EAttachmentPoint::RightHand;
        Params.Equipment.Add(Slot);
    }
    return Params;
}

FWowCharacterBuilder::FCharacterParams BuildPlayerCharacterParams(const FWowPlayerEntity& Player, uint8 NativeGender)
{
	FWowCharacterBuilder::FCharacterParams Params;
    Params.bLoadAnimationsOnDemand = true;
    Params.bAsyncEquipmentAtlas = true; // r311: other players' gear atlas off the game thread
	Params.Race = static_cast<FWowCharacterBuilder::ERace>(Player.GetRaceId());
	Params.Gender = static_cast<FWowCharacterBuilder::EGender>(NativeGender);
	Params.Customization.RaceId = Player.GetRaceId();
	Params.Customization.Gender = NativeGender;
	Params.Customization.SkinColor = Player.GetFieldByte(PlayerField::BYTES, 0);
	Params.Customization.FaceVariation = Player.GetFieldByte(PlayerField::BYTES, 1);
	Params.Customization.HairStyle = Player.GetFieldByte(PlayerField::BYTES, 2);
	Params.Customization.HairColor = Player.GetFieldByte(PlayerField::BYTES, 3);
	Params.Customization.FacialHairStyle = Player.GetFieldByte(PlayerField::BYTES_2, 0);
	Params.BodyEquipment.RaceId = Player.GetRaceId();
	Params.BodyEquipment.Gender = NativeGender;
	const uint32 HiddenGear = Player.GetPlayerFlags(); // r224: Show Helm / Show Cloak off

	for (uint8 SlotIndex = 0; SlotIndex < 19; ++SlotIndex)
	{
		if ((SlotIndex == PlayerField::EQUIPMENT_SLOT_HEAD && (HiddenGear & 0x400u)) ||
			(SlotIndex == PlayerField::EQUIPMENT_SLOT_BACK && (HiddenGear & 0x800u))) continue;
		const uint32 ItemEntry = Player.GetField(PlayerField::VISIBLE_ITEM_BASE + SlotIndex * 2);
		const FItemDbcEntry* Item = FDbcStore::Get().Items().GetById(ItemEntry);
		if (!Item || Item->DisplayInfoID == 0)
		{
			continue;
		}

		switch (SlotIndex)
		{
		case PlayerField::EQUIPMENT_SLOT_HEAD:
		{
			FWowCharacterBuilder::FEquipmentSlot Slot;
			Slot.ItemDisplayId = Item->DisplayInfoID;
			Slot.InventoryType = static_cast<uint8>(Item->InventoryType);
			Slot.AttachPoint = FWowEquipmentManager::EAttachmentPoint::Helmet;
			Params.Equipment.Add(Slot);
			break;
		}
		case PlayerField::EQUIPMENT_SLOT_SHOULDERS:
		{
			FWowCharacterBuilder::FEquipmentSlot LeftSlot;
			LeftSlot.ItemDisplayId = Item->DisplayInfoID;
			LeftSlot.InventoryType = static_cast<uint8>(Item->InventoryType);
			LeftSlot.AttachPoint = FWowEquipmentManager::EAttachmentPoint::LeftShoulder;
			Params.Equipment.Add(LeftSlot);
			FWowCharacterBuilder::FEquipmentSlot RightSlot = LeftSlot;
			RightSlot.AttachPoint = FWowEquipmentManager::EAttachmentPoint::RightShoulder;
			Params.Equipment.Add(RightSlot);
			break;
		}
		case PlayerField::EQUIPMENT_SLOT_SHIRT:
			Params.BodyEquipment.ShirtDisplayId = Item->DisplayInfoID;
			break;
		case PlayerField::EQUIPMENT_SLOT_CHEST:
			Params.BodyEquipment.ChestDisplayId = Item->DisplayInfoID;
			Params.BodyEquipment.bIsRobeEquipped = Item->InventoryType == 20;
			break;
		case PlayerField::EQUIPMENT_SLOT_WAIST:
			Params.BodyEquipment.BeltDisplayId = Item->DisplayInfoID;
			break;
		case PlayerField::EQUIPMENT_SLOT_LEGS:
			Params.BodyEquipment.PantsDisplayId = Item->DisplayInfoID;
			break;
		case PlayerField::EQUIPMENT_SLOT_FEET:
			Params.BodyEquipment.BootsDisplayId = Item->DisplayInfoID;
			break;
		case PlayerField::EQUIPMENT_SLOT_WRISTS:
			Params.BodyEquipment.BracersDisplayId = Item->DisplayInfoID;
			break;
		case PlayerField::EQUIPMENT_SLOT_HANDS:
			Params.BodyEquipment.GlovesDisplayId = Item->DisplayInfoID;
			break;
		case PlayerField::EQUIPMENT_SLOT_BACK:
			Params.BodyEquipment.CapeDisplayId = Item->DisplayInfoID;
			break;
		case PlayerField::EQUIPMENT_SLOT_TABARD:
			Params.BodyEquipment.TabardDisplayId = Item->DisplayInfoID;
			if (ItemEntry == 5976) Params.BodyEquipment.GuildTabard = FWowPacketHandler::GetGuildTabardDesign(Player.GetField(0x97)); // r343
			break;
		case PlayerField::EQUIPMENT_SLOT_MAINHAND:
		case PlayerField::EQUIPMENT_SLOT_OFFHAND:
		case PlayerField::EQUIPMENT_SLOT_RANGED:
		{
			FWowCharacterBuilder::FEquipmentSlot Slot;
			Slot.ItemDisplayId = Item->DisplayInfoID;
			Slot.InventoryType = static_cast<uint8>(Item->InventoryType);
			Slot.SheathType = static_cast<uint8>(Item->SheathType);
            Slot.PackedEnchantments = Player.GetField(PlayerField::VISIBLE_ITEM_BASE + SlotIndex * 2 + 1);
			Slot.bRangedWeapon = SlotIndex == PlayerField::EQUIPMENT_SLOT_RANGED;
			if (SlotIndex == PlayerField::EQUIPMENT_SLOT_OFFHAND)
			{
				Slot.AttachPoint = Item->InventoryType == 14
					? FWowEquipmentManager::EAttachmentPoint::ShieldHand
					: FWowEquipmentManager::EAttachmentPoint::LeftHand;
			}
			else
			{
				Slot.AttachPoint = FWowEquipmentManager::EAttachmentPoint::RightHand;
			}
			Params.Equipment.Add(Slot);
			break;
		}
		default:
			break;
		}
	}
	return Params;
}

// r686: two-hand axes on the back are placed in the body's frame from the live socket (FWowEquipmentManager::
// ApplySheathedTransform), so they are re-applied every frame while sheathed (Stefan: "follow the back").
static TArray<TPair<TWeakObjectPtr<UStaticMeshComponent>, TWeakObjectPtr<USkeletalMeshComponent>>> GWowSheathFollowers;

void ApplySheathedWeaponTransform(UStaticMeshComponent* Equipment, USkeletalMeshComponent* CharacterMesh)
{
    FWowEquipmentManager::ApplySheathedTransform(Equipment, CharacterMesh);
    if (Equipment && CharacterMesh && (Equipment->ComponentHasTag(TEXT("WowSheathTwoHandAxe"))
        || Equipment->ComponentHasTag(TEXT("WowSheathTwoHandSword"))) // r716: swords follow the back too
        && !GWowSheathFollowers.ContainsByPredicate([Equipment](const auto& P) { return P.Key.Get() == Equipment; }))
        GWowSheathFollowers.Emplace(Equipment, CharacterMesh);
}

static void UpdateSheathFollowers()
{
    for (int32 I = GWowSheathFollowers.Num() - 1; I >= 0; --I)
    {
        UStaticMeshComponent* Equipment = GWowSheathFollowers[I].Key.Get();
        USkeletalMeshComponent* Mesh = GWowSheathFollowers[I].Value.Get();
        if (!Equipment || !Mesh) { GWowSheathFollowers.RemoveAtSwap(I); continue; }
        // Only while it hangs on a back socket of that body (drawn weapons are in the hand).
        if (Equipment->GetAttachParent() != Mesh || !Equipment->GetAttachSocketName().ToString().StartsWith(TEXT("WowSheath"))
            || !Equipment->IsVisible()) continue;
        FWowEquipmentManager::ApplySheathedTransform(Equipment, Mesh);
    }
}

void ApplyRemoteWeaponPresentation(AActor* CharacterActor, uint8 SheathState, bool bMounted)
{
	if (!CharacterActor)
	{
		return;
	}

	USkeletalMeshComponent* CharacterMesh =
		CharacterActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!CharacterMesh)
	{
		return;
	}

	static const FName SheathSockets[] = {
		TEXT("WowSheathMainHand"), TEXT("WowSheathOffHand"), TEXT("WowSheathShield"),
		TEXT("WowStaffBackLeft"), TEXT("WowStaffBackRight"),
		TEXT("WowSheathLargeWeaponLeft"), TEXT("WowSheathLargeWeaponRight"),
		TEXT("WowSheathHipWeaponLeft"), TEXT("WowSheathHipWeaponRight")
	};

	TInlineComponentArray<UStaticMeshComponent*> EquipmentComponents(CharacterActor);
	for (UStaticMeshComponent* Equipment : EquipmentComponents)
	{
		if (!Equipment || !Equipment->ComponentHasTag(TEXT("WowHeldWeapon")))
		{
			continue;
		}

        const bool bRangedWeapon = Equipment->ComponentHasTag(TEXT("WowRangedWeapon"));
        // Ranged inventory is only displayed while the server reports it drawn.
        if (bRangedWeapon && (bMounted || SheathState != 2))
        {
            Equipment->SetVisibility(false, true);
            continue;
        }
		const bool bShouldRemainDrawn = !bMounted &&
			((SheathState == 1 && !bRangedWeapon) || (SheathState == 2 && bRangedWeapon));
		if (bShouldRemainDrawn)
		{
			FName DrawSocket = NAME_None;
			for (const FName& Tag : Equipment->ComponentTags)
			{
				FString TagText = Tag.ToString();
				if (TagText.RemoveFromStart(TEXT("WowDrawSocket=")))
				{
					DrawSocket = FName(*TagText);
					break;
				}
			}
			if (!DrawSocket.IsNone())
			{
				Equipment->AttachToComponent(CharacterMesh,
					FAttachmentTransformRules::SnapToTargetNotIncludingScale, DrawSocket);
                FWowEquipmentManager::ApplyDrawnTransform(Equipment);
			}
			Equipment->SetVisibility(true, true);
			continue;
		}

		FName SheathSocket = NAME_None;
		for (const FName Candidate : SheathSockets)
		{
			if (Equipment->ComponentHasTag(Candidate) && CharacterMesh->DoesSocketExist(Candidate))
			{
				SheathSocket = Candidate;
				break;
			}
		}

		if (SheathSocket.IsNone())
		{
			Equipment->SetVisibility(false, true);
			continue;
		}

		Equipment->AttachToComponent(CharacterMesh,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, SheathSocket);
        ApplySheathedWeaponTransform(Equipment, CharacterMesh);
        Equipment->SetVisibility(true, true);
	}
}

bool HasHipSheathedWeapon(const AActor* Actor)
{
    if (!Actor) return false;
    TInlineComponentArray<UStaticMeshComponent*> Items(Actor);
    for (const UStaticMeshComponent* Item : Items)
        if (Item && Item->ComponentHasTag(TEXT("WowHeldWeapon")) && !Item->ComponentHasTag(TEXT("WowRangedWeapon"))
            && (Item->ComponentHasTag(TEXT("WowSheathHipWeaponLeft")) || Item->ComponentHasTag(TEXT("WowSheathHipWeaponRight"))))
            return true;
    return false;
}

/** Sheath/draw like WoW: the reach-to-back (or hip) gesture, weapons move partway through it. */
void ApplyWeaponPoseWithGesture(AActor* Actor, UWowAnimationController* Anim, TFunction<void()> Apply)
{
    const float Length = (Actor && Anim) ? Anim->PlaySheathAnimation(HasHipSheathedWeapon(Actor)) : 0.f;
    if (Length > 0.1f)
    {
        FTimerHandle Handle;
        Actor->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Actor, [Apply]() { Apply(); }),
            Length * 0.45f, false);
    }
    else Apply();
}

UTexture2D* LoadOriginalUiTexture(FMpqManager* MpqManager, FWowAssetCache* AssetCache,
	const FString& TexturePath, const FString& ObjectName)
{
	if (!MpqManager || !AssetCache)
	{
		return nullptr;
	}

	if (UTexture2D* CachedTexture = AssetCache->FindTexture(TexturePath))
	{
		return CachedTexture;
	}

	TArray<uint8> FileData;
	if (!MpqManager->ReadFile(TexturePath, FileData))
	{
		return nullptr;
	}

	const FBlpTexture ParsedTexture = FBlpParser::Parse(FileData);
	if (!ParsedTexture.bIsValid)
	{
		return nullptr;
	}

	UTexture2D* Texture = FWowTextureFactory::CreateTexture(ParsedTexture, ObjectName);
	if (Texture)
	{
		AssetCache->CacheTexture(TexturePath, Texture);
	}
	return Texture;
}

TSharedPtr<SWidget> GetWowGameViewportWidget()
{
	if (!GEngine || !GEngine->GameViewport)
	{
		return nullptr;
	}

	return StaticCastSharedPtr<SWidget>(GEngine->GameViewport->GetGameViewportWidget());
}

bool IsWowGlueScreenMode()
{
	const TCHAR* CmdLine = FCommandLine::Get();
	FString TestScene;
	FParse::Value(CmdLine, TEXT("-testscene="), TestScene);
	TestScene = TestScene.ToLower();

	return TestScene == TEXT("login")
		|| FParse::Param(CmdLine, TEXT("charactercreatepreview"))
		|| FParse::Param(CmdLine, TEXT("loadingscreenpreview"))
		|| FParse::Param(CmdLine, TEXT("offlineloading"));
}

bool IsWowLoadingScreenMode()
{
	const TCHAR* CmdLine = FCommandLine::Get();
	FString TestScene;
	FParse::Value(CmdLine, TEXT("-testscene="), TestScene);
	TestScene = TestScene.ToLower();

	return TestScene == TEXT("loadingscreenpreview")
		|| FParse::Param(CmdLine, TEXT("loadingscreenpreview"))
		|| FParse::Param(CmdLine, TEXT("offlineloading"));
}

bool IsWowOfflineSession()
{
	const TCHAR* CmdLine = FCommandLine::Get();
	return FParse::Param(CmdLine, TEXT("offlineflow"))
		|| FParse::Param(CmdLine, TEXT("offlineloading"))
		|| FParse::Param(CmdLine, TEXT("offlineworld"));
}

bool IsPassiveChatCameraSurface(FWowFrameManager* FrameManager, int64 HitFrame)
{
	if (!FrameManager || HitFrame < 0)
	{
		return false;
	}

	bool bInsideChatFrame = false;
	int64 CurrentFrame = HitFrame;
	int32 Guard = 0;
	while (CurrentFrame >= 0 && Guard++ < 32)
	{
		const FString FrameName = FrameManager->GetFrameName(CurrentFrame);
		if (FrameName.StartsWith(TEXT("ChatFrame"), ESearchCase::IgnoreCase))
		{
			bInsideChatFrame = true;
		}
		// Empty dock/button-frame containers are camera surfaces, not controls.
		if (FrameName == TEXT("GeneralDockManager") ||
			(FrameName.StartsWith(TEXT("ChatFrame")) && FrameName.EndsWith(TEXT("ButtonFrame"))))
		{
			bInsideChatFrame = true;
			CurrentFrame = FrameManager->GetParentHandle(CurrentFrame);
			continue;
		}
		if (FrameName.Contains(TEXT("Tab"), ESearchCase::IgnoreCase) ||
			FrameName.Contains(TEXT("EditBox"), ESearchCase::IgnoreCase) ||
			FrameName.Contains(TEXT("Button"), ESearchCase::IgnoreCase) ||
			FrameName.Contains(TEXT("Menu"), ESearchCase::IgnoreCase) ||
			FrameName.Contains(TEXT("DropDown"), ESearchCase::IgnoreCase) ||
			FrameName.Contains(TEXT("Resize"), ESearchCase::IgnoreCase) ||
			FrameName.Contains(TEXT("ScrollBar"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		CurrentFrame = FrameManager->GetParentHandle(CurrentFrame);
	}

	return bInsideChatFrame;
}

void SetNativePlatformCursorVisible(bool bVisible)
{
	if (FSlateApplication::IsInitialized())
	{
		if (TSharedPtr<ICursor> PlatformCursor = FSlateApplication::Get().GetPlatformCursor())
		{
			PlatformCursor->Show(bVisible);
		}
	}
}

void FocusWowGameViewport();

void ApplyWowGameAndUiInputMode(APlayerController* PlayerController, const TSharedPtr<SWidget>& WidgetToFocus)
{
	if (!PlayerController)
	{
		return;
	}

	FInputModeGameAndUI InputMode;
	if (WidgetToFocus.IsValid())
	{
		InputMode.SetWidgetToFocus(WidgetToFocus);
	}
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
	if (UGameViewportClient* ViewportClient = PlayerController->GetWorld() ? PlayerController->GetWorld()->GetGameViewport() : nullptr)
	{
		ViewportClient->SetMouseLockMode(EMouseLockMode::DoNotLock);
		ViewportClient->SetMouseCaptureMode(WowMouseCapturePolicy::Resolve(false, false));
		ViewportClient->SetHideCursorDuringCapture(false);
	}
	PlayerController->bShowMouseCursor = true;
	PlayerController->DefaultMouseCursor = EMouseCursor::None;
	PlayerController->CurrentMouseCursor = EMouseCursor::None;
}

void ApplyWowMouseLookInputMode(APlayerController* PlayerController, bool bRightMouseDriven)
{
	if (!PlayerController)
	{
		return;
	}

	FInputModeGameOnly InputMode;
	InputMode.SetConsumeCaptureMouseDown(false);
	PlayerController->SetInputMode(InputMode);
	if (UGameViewportClient* ViewportClient = PlayerController->GetWorld() ? PlayerController->GetWorld()->GetGameViewport() : nullptr)
	{
		// Camera look must own the physical pointer for the complete drag.  Leaving
		// it uncaptured allowed the hidden cursor to travel across chat tabs and
		// other Slate widgets, producing hover highlights while the camera moved.
		ViewportClient->SetMouseLockMode(EMouseLockMode::LockAlways);
		ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
		ViewportClient->SetHideCursorDuringCapture(true);
	}
	PlayerController->bShowMouseCursor = false;
	PlayerController->DefaultMouseCursor = EMouseCursor::None;
	PlayerController->CurrentMouseCursor = EMouseCursor::None;
	FocusWowGameViewport();
}

void FocusWowGameViewport()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport(EFocusCause::SetDirectly);
	}
}

bool IsWowUiConsumingKeyboardInput()
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}

	const TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (!FocusedWidget.IsValid())
	{
		return false;
	}
	if (!FocusedWidget->GetVisibility().IsVisible())
	{
		return false;
	}

	// Only suppress gameplay hotkeys when a text input widget has focus.
	// In WoW, only chat edit boxes and similar text fields consume keyboard input.
	// Regular frame clicks don't steal keyboard focus from gameplay.
	const FString WidgetType = FocusedWidget->GetType().ToString();
	return WidgetType.Contains(TEXT("EditableText")) || WidgetType.Contains(TEXT("SEditableText"));
}

bool ShouldSuppressGameplayHotkey(const FKey& Key)
{
	return Key == EKeys::V || Key == EKeys::Tab
		|| Key == EKeys::Enter
		|| Key == EKeys::One
		|| Key == EKeys::Two
		|| Key == EKeys::Three
		|| Key == EKeys::Four
		|| Key == EKeys::Five
		|| Key == EKeys::Six
		|| Key == EKeys::Seven
		|| Key == EKeys::Eight
		|| Key == EKeys::Nine
		|| Key == EKeys::Zero
		|| Key == EKeys::Hyphen
		|| Key == EKeys::Equals
		|| Key == EKeys::M
		|| Key == EKeys::B
		|| Key == EKeys::C
		|| Key == EKeys::L
		|| Key == EKeys::O
		|| Key == EKeys::J
		|| Key == EKeys::N
		|| Key == EKeys::P
		|| Key == EKeys::H
		|| Key == EKeys::X
		|| Key == EKeys::Z;
}

bool TryGetPhysicalInputDown(const FKey& Key, bool& bOutIsDown)
{
#if PLATFORM_WINDOWS
	int32 VirtualKey = 0;
	if (Key == EKeys::LeftMouseButton)
	{
		VirtualKey = VK_LBUTTON;
	}
	else if (Key == EKeys::RightMouseButton)
	{
		VirtualKey = VK_RBUTTON;
	}
	else if (Key == EKeys::Q)
	{
		VirtualKey = 'Q';
	}
	else if (Key == EKeys::E)
	{
		VirtualKey = 'E';
	}
	else if (Key == EKeys::W)
	{
		VirtualKey = 'W';
	}
	else if (Key == EKeys::A)
	{
		VirtualKey = 'A';
	}
	else if (Key == EKeys::S)
	{
		VirtualKey = 'S';
	}
	else if (Key == EKeys::D)
	{
		VirtualKey = 'D';
	}

	if (VirtualKey != 0)
	{
		bOutIsDown = (::GetAsyncKeyState(VirtualKey) & 0x8000) != 0;
		return true;
	}
#endif

	bOutIsDown = false;
	return false;
}

bool IsWowClientProcessForeground()
{
#if PLATFORM_WINDOWS
	// Windows reports no foreground window for a moment while switching. Treating that as
	// "another program is in front" dropped the mouse state and swallowed the next click
	// (the page arrows that needed two clicks), so keep the last known answer instead.
	static bool bLastForeground = true;
	const HWND ForegroundWindow = ::GetForegroundWindow();
	if (!ForegroundWindow)
	{
		return bLastForeground;
	}
	DWORD ForegroundProcessId = 0;
	::GetWindowThreadProcessId(ForegroundWindow, &ForegroundProcessId);
	bLastForeground = ForegroundProcessId == ::GetCurrentProcessId();
	return bLastForeground;
#else
	return true;
#endif
}
}

AWowGameplayController::AWowGameplayController()
{
	bSoftwareCursorSuppressed = IsWowLoadingScreenMode();
	bShowMouseCursor = !bSoftwareCursorSuppressed;
	DefaultMouseCursor = EMouseCursor::None;
	CurrentMouseCursor = EMouseCursor::None;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	PrimaryActorTick.bCanEverTick = true;

	// Create Game UI component
	GameUI = CreateDefaultSubobject<UWowGameUI>(TEXT("WowGameUI"));
}

void AWowGameplayController::BeginPlay()
{
	Super::BeginPlay();
    PhotoMode = MakeShared<FWowPhotoMode>(this);
    if (FSlateApplication::IsInitialized())
    {
        if (GWowEditBoxKeyInput.IsValid()) FSlateApplication::Get().UnregisterInputPreProcessor(GWowEditBoxKeyInput);
        TWeakObjectPtr<AWowGameplayController> WeakThis(this);
        GWowEditBoxKeyInput = MakeShared<FWowEditBoxKeyInput>([WeakThis](const FKeyEvent& Event) -> bool
        {
            AWowGameplayController* Self = WeakThis.Get();
            const FKey Key = Event.GetKey();
            if (!Self || (Key != EKeys::Enter && Key != EKeys::Escape)) return false;
            FWowFrameManager* FrameManager = Self->UIManager ? Self->UIManager->GetFrameManager() : nullptr;
            int64 Handle = -1;
            FString Text;
            if (!FrameManager || !FrameManager->GetFocusedEditBox(Handle, Text)) return false;
            UEditableTextBox* EditBox = FrameManager->GetEditBoxWidget(Handle);
            if (!EditBox || !(EditBox->HasKeyboardFocus() || EditBox->HasFocusedDescendants())) return false;
            if (Event.IsRepeat()) return true;
            if (Key == EKeys::Enter) Self->OnEnterKey();
            else Self->OnEscapeKey();
            return true;
        });
        FSlateApplication::Get().RegisterInputPreProcessor(GWowEditBoxKeyInput);
    }
    GWowWorldTooltipGuid = 0;
    bGWowWorldTooltipShown = false;
    UWowScreenshotManager::OnPhotoModeHold().AddUObject(this, &AWowGameplayController::TogglePhotoMode);
    UWowScreenshotManager::OnPhotoModeTap().BindUObject(this, &AWowGameplayController::HandlePhotoModeTap);
    if(GameUI)GameUI->CanMoveFrames=[this]()
    {return !bUserInterfaceHidden&&IsWowClientProcessForeground()&&!bMouseInputNeedsRearmAfterFocusChange&&!IsNativeModalUiVisible()
        &&!bLeftMouseLooking&&!bRightMouseLooking&&!(ConnectionManager&&ConnectionManager->HasCursorPayload());};
    if(GameUI)GameUI->EnableFrameMoving();
    FWowClientPreferences::ApplyAdvancedVideoPreferences();
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("testdof")) && IsWowOfflineSession()) TogglePerformanceOverlay();
#endif
	FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(
		this, &AWowGameplayController::HandleApplicationWillDeactivate);
	FCoreDelegates::ApplicationHasReactivatedDelegate.AddUObject(
		this, &AWowGameplayController::HandleApplicationHasReactivated);
	if (IsWowLoadingScreenMode())
	{
		SetSoftwareCursorSuppressed(true);
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
		if (UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			ViewportClient->SetMouseLockMode(EMouseLockMode::DoNotLock);
			ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
			ViewportClient->SetHideCursorDuringCapture(true);
		}
		FocusWowGameViewport();
	}
	else
	{
		ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
		FocusWowGameViewport();
		bShowMouseCursor = true;
	}
	DefaultMouseCursor = EMouseCursor::None;
	CurrentMouseCursor = EMouseCursor::None;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	if (PlayerCameraManager)
	{
		PlayerCameraManager->ViewYawMin = -360000.0f;
		PlayerCameraManager->ViewYawMax = 360000.0f;
		PlayerCameraManager->ViewPitchMin = -89.0f;
		PlayerCameraManager->ViewPitchMax = 89.0f;
	}

	// Cache UIManager early so OnUpdate ticks even without networking
	if (UGameInstance* GI = GetGameInstance())
	{
		UIManager = GI->GetSubsystem<UWowUIManager>();
		if (UIManager && UIManager->GetFrameManager())
		{
            UIManager->SetBindingHandler([WeakThis=TWeakObjectPtr<AWowGameplayController>(this)](const FString& Command,bool Down)
            {return WeakThis.IsValid() && WeakThis->ExecuteSavedBinding(Command,Down);});
			// Bind native secure action dispatch â€” emulates WoW's C++ action button handling
			UIManager->GetFrameManager()->OnSecureActionDispatch.BindLambda([this](int32 ActionSlot)
			{
				CastSpellFromSlot(ActionSlot);
			});
			UIManager->GetFrameManager()->OnSecureSpellDispatch.BindLambda([this](int32 SpellId)
			{
				if (ConnectionManager)
				{
					ConnectionManager->SendCastSpell(SpellId, static_cast<int64>(TargetGuid));
				}
			});
			BindMinimapUiDelegates();
		}
	}

	// Create managers
	DeathManager = NewObject<UWowDeathManager>(this);
	CursorManager = NewObject<UWowCursorManager>(this);
	TooltipManager = NewObject<UWowTooltipManager>(this);
	CreateSoftwareCursor();
	if (CursorManager)
	{
		CursorManager->Initialize(nullptr, nullptr);
		CursorManager->LoadCursors();
		RefreshSoftwareCursorBrush();
	}
	if (!IsWowGlueScreenMode())
	{
		CreateQuestTracker();
	}

	// Cast bar â€” kept as fallback for spell feedback
	if (FParse::Param(FCommandLine::Get(), TEXT("nativecastbar")))
	{
		TSharedRef<SWidget> CastBarContainer =
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			.Padding(0, 0, 0, 80)
			[
				SAssignNew(CastBarWidget, SWowCastBar)
			];
		if (GEngine && GEngine->GameViewport)
		{
			FWowUiSurfaces::Add(CastBarContainer, EWowUiZone::CastBar, 1000);
		}
	}

	CreateDuelStatusOverlay();
	FParse::Value(FCommandLine::Get(), TEXT("-testduel="), DebugDuelPreviewMode);
	FParse::Value(FCommandLine::Get(), TEXT("-testspell="), DebugSpellPreviewId);
	FParse::Value(FCommandLine::Get(), TEXT("-testspellphase="), DebugSpellPreviewPhase);
	DebugSpellPreviewPhase = DebugSpellPreviewPhase.ToLower();

	// Local visual verification only.  This never runs in a normal session;
	// it lets the rendered smoke-test open the same character sheet that the C
	// key opens, without a realm connection or synthetic input injection.
	if (FParse::Param(FCommandLine::Get(), TEXT("charactersheetpreview")))
	{
		FTimerHandle PreviewTimer;
		GetWorldTimerManager().SetTimer(PreviewTimer, [this]()
		{
			OnCharacterKey();
		}, 5.0f, false);
	}
}

void AWowGameplayController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GWowEditBoxKeyInput.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().UnregisterInputPreProcessor(GWowEditBoxKeyInput);
    GWowEditBoxKeyInput.Reset();
    if (PhotoMode) PhotoMode->Exit();
    UWowScreenshotManager::OnPhotoModeHold().RemoveAll(this);
    if (UWowScreenshotManager::OnPhotoModeTap().IsBoundToObject(this)) UWowScreenshotManager::OnPhotoModeTap().Unbind();
    FinishOfflineTransferLoading();
    SaveOfflineLogoutPosition();
    if (ConnectionManager) ConnectionManager->PacketHandler.OnKillXPGain.RemoveAll(this);
    if (XPGainWidget.IsValid() && GEngine && GEngine->GameViewport)
        FWowUiSurfaces::Remove(XPGainWidget.ToSharedRef());
    XPGainWidget.Reset();
    UpdateHoveredModel(0);
    for(auto Light:HoverModelLights) if(IsValid(Light)) Light->DestroyComponent();
    HoverModelLights.Empty();
    SetUserInterfaceHidden(false);
    if (WeatherLensWidget.IsValid() && GEngine && GEngine->GameViewport)
        FWowUiSurfaces::Remove(WeatherLensWidget.ToSharedRef());
    if(UIManager && UIManager->GetFrameManager()) UIManager->GetFrameManager()->SetMinimapFrostOverlay(nullptr);
    WeatherLensWidget.Reset();
    if (CursorManager) CursorManager->ReleaseHardwareCursors();
	FCoreDelegates::ApplicationWillDeactivateDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationHasReactivatedDelegate.RemoveAll(this);
	ForceReleaseMouseLookForFocusChange();
	GWowReapplyInputModeAfterActivationClick = false; // statics outlive this controller (loading screen next)
	GWowActivationWorldClick = false;
	GWowActivationUiPress = false;
	if (PerformanceOverlayViewportWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		FWowUiSurfaces::Remove(PerformanceOverlayViewportWidget.ToSharedRef());
	}
    for (auto& Pair : UnitPortraits) if (IsValid(Pair.Value)) Pair.Value->Destroy();
    UnitPortraits.Empty();
    if (UIManager && UIManager->GetFrameManager()) UIManager->GetFrameManager()->UnitPortraitProvider = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AWowGameplayController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Left click for targeting and WoW UI hit testing
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AWowGameplayController::OnLeftClick);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AWowGameplayController::OnLeftClickReleased);

	// Right click for auto-attack - use IE_Pressed for immediate responsiveness
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AWowGameplayController::OnRightClick);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AWowGameplayController::OnRightClickReleased);
	InputComponent->BindAxis(TEXT("MouseX"), this, &AWowGameplayController::OnMouseTurn);
	InputComponent->BindAxis(TEXT("MouseY"), this, &AWowGameplayController::OnMouseLookUp);
	InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AWowGameplayController::OnTurnLeftPressed);
	InputComponent->BindKey(EKeys::Q, IE_Released, this, &AWowGameplayController::OnTurnLeftReleased);
	InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AWowGameplayController::OnTurnRightPressed);
	InputComponent->BindKey(EKeys::E, IE_Released, this, &AWowGameplayController::OnTurnRightReleased);

	// Action bar keybinds - use IE_Pressed for immediate responsiveness like WoW
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AWowGameplayController::OnActionSlot1);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AWowGameplayController::OnActionSlot2);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AWowGameplayController::OnActionSlot3);
	InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &AWowGameplayController::OnActionSlot4);
	InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &AWowGameplayController::OnActionSlot5);
	InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &AWowGameplayController::OnActionSlot6);
	InputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &AWowGameplayController::OnActionSlot7);
	InputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &AWowGameplayController::OnActionSlot8);
	InputComponent->BindKey(EKeys::Nine, IE_Pressed, this, &AWowGameplayController::OnActionSlot9);
	InputComponent->BindKey(EKeys::Zero, IE_Pressed, this, &AWowGameplayController::OnActionSlot0);
	InputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &AWowGameplayController::OnActionSlotMinus);
	InputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &AWowGameplayController::OnActionSlotEquals);

    InputComponent->BindKey(EKeys::V, IE_Pressed, this, &AWowGameplayController::OnToggleNameplates);
	// Tab targeting â€” select closest enemy
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AWowGameplayController::OnTabTarget);

	// Minimap toggle
	InputComponent->BindKey(EKeys::M, IE_Pressed, this, &AWowGameplayController::OnToggleMap);

	// Chat input
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AWowGameplayController::OnEnterKey);

	// Escape â€” clear target, stop auto-attack, close UI panels
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AWowGameplayController::OnEscapeKey);

	// Inventory UI keys. B is handled in InputKey() so Shift+B can open all
	// bags without also triggering the backpack-only binding.
	InputComponent->BindKey(EKeys::C, IE_Pressed, this, &AWowGameplayController::OnCharacterKey);
	InputComponent->BindKey(EKeys::L, IE_Pressed, this, &AWowGameplayController::OnQuestLogKey);
	InputComponent->BindKey(EKeys::K, IE_Pressed, this, &AWowGameplayController::OnSkillsKey);
	InputComponent->BindKey(EKeys::I, IE_Pressed, this, &AWowGameplayController::OnGroupFinderKey);
	InputComponent->BindKey(EKeys::O, IE_Pressed, this, &AWowGameplayController::OnSocialKey);
	InputComponent->BindKey(EKeys::J, IE_Pressed, this, &AWowGameplayController::OnGuildRosterKey);
	InputComponent->BindKey(EKeys::N, IE_Pressed, this, &AWowGameplayController::OnTalentKey);
	InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AWowGameplayController::OnSpellbookKey);
	// H: TOGGLECHARACTER4, the PvP window (UIParent.lua TogglePVPFrame). Stefan 11.10: H did nothing.
	{
		FInputKeyBinding PvpBinding(FInputChord(EKeys::H), IE_Pressed);
		PvpBinding.KeyDelegate.GetDelegateForManualSet().BindLambda([this]()
		{
			if (ShouldSuppressGameplayHotkey(EKeys::H)) return; // typing an "h" in chat
			if (UIManager && UIManager->GetLuaVM() && UIManager->GetLuaVM()->IsInitialized())
				UIManager->GetLuaVM()->ExecuteString(TEXT("if TogglePVPFrame then TogglePVPFrame() elseif PVPParentFrame then ToggleFrame(PVPParentFrame) end"), TEXT("=keybind_H"));
		});
		InputComponent->KeyBindings.Add(PvpBinding);
	}
	InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &AWowGameplayController::OnToggleSheathKey);
	InputComponent->BindKey(EKeys::X, IE_Pressed, this, &AWowGameplayController::OnToggleSitKey);
}

void AWowGameplayController::BindEntityEvents()
{
	if (!ConnectionManager) return;
	ConnectionManager->OnLogoutComplete.AddUniqueDynamic(this, &AWowGameplayController::HandleServerLogoutComplete);
    ConnectionManager->PacketHandler.OnKillXPGain.RemoveAll(this);
    ConnectionManager->PacketHandler.OnKillXPGain.AddUObject(this,&AWowGameplayController::HandleKillXPGain);

	// The stock FrameXML minimap supplies the ring/buttons. This native overlay
	// supplies live top-down map content plus entity and POI markers.
	CreateMinimapWidget();

	// Listen for login verify world â€” teleport pawn to spawn position
	ConnectionManager->PacketHandler.OnLoginVerifyWorld.AddUObject(
		this, &AWowGameplayController::OnLoginVerifyWorld);

	// Listen for teleport requests from server
	ConnectionManager->PacketHandler.OnTeleportRequest.AddUObject(
		this, &AWowGameplayController::OnTeleportRequest);
	ConnectionManager->PacketHandler.OnForcedMovement.AddUObject(
		this, &AWowGameplayController::OnForcedMovement);

	// Listen for map transfers
	ConnectionManager->PacketHandler.OnMapTransfer.AddUObject(
		this, &AWowGameplayController::OnMapTransfer);

	// Listen for entity updates â€” sync local player position from server
	ConnectionManager->PacketHandler.EntityManager.OnEntityUpdated.AddUObject(
		this, &AWowGameplayController::OnEntityUpdated);

	// Listen for entity creation/destruction â€” spawn/destroy character models
	ConnectionManager->PacketHandler.EntityManager.OnEntityCreated.AddUObject(
		this, &AWowGameplayController::OnEntityCreated);
	ConnectionManager->PacketHandler.EntityManager.OnEntityDestroyed.AddUObject(
		this, &AWowGameplayController::OnEntityDestroyed);
	ConnectionManager->PacketHandler.OnEntityDeath.AddUObject(
		this, &AWowGameplayController::OnEntityDeath);

	// Forward SMSG opcodes to UI event system
	ConnectionManager->PacketHandler.OnOpcodeReceived.AddUObject(
		this, &AWowGameplayController::OnOpcodeReceived);

	// Listen for player inventory updates
	ConnectionManager->PacketHandler.OnPlayerInventoryUpdate.AddUObject(
		this, &AWowGameplayController::OnPlayerInventoryUpdated);

	// Listen for unit health/power updates to fire UI events
	ConnectionManager->PacketHandler.OnUnitHealthUpdate.AddUObject(
		this, &AWowGameplayController::OnUnitHealthUpdate);
	ConnectionManager->PacketHandler.OnUnitPowerUpdate.AddUObject(
		this, &AWowGameplayController::OnUnitPowerUpdate);

	// Bind combat log events
	ConnectionManager->PacketHandler.OnSpellStart.AddUObject(
		this, &AWowGameplayController::OnSpellStart);
	ConnectionManager->PacketHandler.OnSpellGo.AddUObject(
		this, &AWowGameplayController::OnSpellGo);
	ConnectionManager->PacketHandler.OnSpellHitTargets.AddLambda(
		[this](uint64 Caster, uint32 Spell, const TArray<uint64>& HitTargets)
		{
			for (uint64 HitTarget : HitTargets) SpawnSpellMissile(Caster, HitTarget, Spell);
		});
    ConnectionManager->PacketHandler.OnUiErrorMessage.AddWeakLambda(this,[this](const FString& Message)
    {FireUIEvent(TEXT("UI_ERROR_MESSAGE"),{Message});});
    ConnectionManager->OnItemConfirmation.AddWeakLambda(this, [this](const FString& Event, const TArray<FString>& Args)
    {
        // Destroy confirmation is native (Yes/No or typed DELETE); FrameXML's StaticPopup is not used.
        if (Event == TEXT("DELETE_ITEM_CONFIRM")) { ShowDestroyItemDialog(Args); return; }
        FireUIEvent(Event, Args);
    });
    ConnectionManager->PacketHandler.OnUiInfoMessage.AddWeakLambda(this,[this](const FString& Message)
    {FireUIEvent(TEXT("UI_INFO_MESSAGE"),{Message});});
	ConnectionManager->PacketHandler.OnSpellFailure.AddUObject(
		this, &AWowGameplayController::OnSpellFailure);
	// A cast the server refused before it started (no fishable water, out of range, not facing):
	// the red error text, like the original client (Stefan 10.10: fishing on land said nothing).
	ConnectionManager->PacketHandler.OnCastFailed.AddWeakLambda(this, [this](uint32 SpellId, uint8 Result)
	{
		// Fishing on dry land: the server answers NOT_HERE (60); say what is wrong, like
		// SPELL_FAILED_NOT_FISHABLE (58) "Your cast didn't land in fishable water" (Stefan 10.10).
		static const uint32 FishingSpells[] = {7620, 7731, 7732, 18248, 33095, 51294, 62734, 63275};
		bool bFishing = false;
		for (const uint32 Fishing : FishingSpells) bFishing |= Fishing == SpellId;
		FString Message = WowSpellCastResultText(UIManager, bFishing && Result == 60 ? 58 : Result);
		// r271 (Stefan 13.10: "Must have a %s equipped in the main hand" when fishing without a pole):
		// SPELL_FAILED_EQUIPPED_ITEM_CLASS(_MAINHAND/_OFFHAND) take the item subclass name; the
		// fishing spells require a Fishing Pole.
		if (Message.Contains(TEXT("%s")))
			Message = Message.Replace(TEXT("%s"), bFishing ? TEXT("Fishing Pole") : TEXT("weapon"));
		// One message per refused cast: the server can send a second CAST_FAILED (e.g. 11 "Invalid
		// target" after 60) and a SPELL_FAILURE for the same cast; the original client shows the first.
		const double CastErrorNow = FPlatformTime::Seconds();
		const bool bRepeat = GWowLastCastErrorSpell == SpellId && CastErrorNow - GWowLastCastErrorTime < 0.5;
		UE_LOG(LogWowGameplay, Log, TEXT("Cast refused: spell %u result %u '%s'%s"), SpellId, Result, *Message, bRepeat ? TEXT(" (repeat, not shown)") : TEXT(""));
		if (bRepeat) return;
		GWowLastCastErrorSpell = SpellId;
		GWowLastCastErrorTime = CastErrorNow;
		if (!Message.IsEmpty()) FireUIEvent(TEXT("UI_ERROR_MESSAGE"), {Message});
	});
	ConnectionManager->PacketHandler.OnChannelStart.AddUObject(
		this, &AWowGameplayController::OnChannelStart);
	ConnectionManager->PacketHandler.OnChannelUpdate.AddUObject(
		this, &AWowGameplayController::OnChannelUpdate);
	ConnectionManager->PacketHandler.OnSpellDelayed.AddUObject(
		this, &AWowGameplayController::OnSpellDelayed);
	ConnectionManager->PacketHandler.OnAttackerStateUpdate.AddUObject(
		this, &AWowGameplayController::OnAttackerStateUpdate);
	// Dressed NPCs: rebuild the unit as a character once its outfit arrives (r257).
	ConnectionManager->PacketHandler.OnMirrorImageData.AddWeakLambda(this, [this](const FWowMirrorImageData& Mirror)
	{
		if (!Mirror.bHasAppearance) return;
		GMirrorImages.Add(Mirror.Guid, Mirror);
		if (TObjectPtr<AActor>* Actor = SpawnedEntityActors.Find(Mirror.Guid); Actor && *Actor)
		{
			(*Actor)->Destroy();
			SpawnedEntityActors.Remove(Mirror.Guid);
			SpawnedEntityAnimationControllers.Remove(Mirror.Guid);
			EntityNameplates.Remove(Mirror.Guid); WorldLabelAnchors.Remove(Mirror.Guid);
			QueueEntityModelSpawn(Mirror.Guid);
		}
	});
	ConnectionManager->PacketHandler.OnCombatLog.AddUObject(
		this, &AWowGameplayController::OnCombatLogEntry);
	// PLAYER_LEVEL_UP: level, health, mana, talentPoints, strength, agility, stamina, intellect, spirit
	ConnectionManager->PacketHandler.OnLevelUp.AddWeakLambda(this, [this](uint32 Level)
	{
		const TArray<int32>& Gains = ConnectionManager->PacketHandler.LastLevelUpGains;
		auto Gain = [&Gains](int32 Index) { return CombatLogNumber(Gains.IsValidIndex(Index) ? Gains[Index] : 0); };
		FireUIEvent(TEXT("PLAYER_LEVEL_UP"), { CombatLogNumber(Level), Gain(0), Gain(1),
			CombatLogNumber(Level >= 10 ? 1 : 0), Gain(2), Gain(3), Gain(4), Gain(5), Gain(6) });
		// The client's own level-up presentation: the golden LevelUp model at the feet and
		// the LEVELUP sound (SoundEntries 888), like 3.3.5's hardcoded unit level-up effect.
		if (AWowAudioManager* Audio = Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(GetWorld(), AWowAudioManager::StaticClass())))
			Audio->PlaySoundEffect(888);
		if (ConnectionManager)
		{
			const uint64 Self = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
			FPendingSpellVisual& Request = PendingSpellVisuals.AddDefaulted_GetRef();
			Request.OwnerGuid = Self;
			Request.AnchorGuid = Self;
			Request.SpellId = 47292; // "Level Up"
			Request.ModelPath = TEXT("Spells\\LevelUp\\LevelUp.m2");
			Request.Scale = 1.0f;
			Request.Slot = ESpellVisualSlot::Base;
			Request.Phase = ESpellVisualPhase::Impact;
			Request.bPersistent = false;
		}
	});
	ConnectionManager->PacketHandler.OnAttackStart.AddUObject(
		this, &AWowGameplayController::OnServerAttackStart);
	ConnectionManager->PacketHandler.OnAttackStop.AddUObject(
		this, &AWowGameplayController::OnServerAttackStop);
	ConnectionManager->PacketHandler.OnChatMessage.AddUObject(
		this, &AWowGameplayController::OnChatMessage);
	ConnectionManager->PacketHandler.OnWeatherUpdate.AddLambda([this](uint32 WeatherType, float Grade, uint8 Abrupt)
	{
		if (UWorld* World = GetWorld())
		{
			if (AWowSkyManager* SkyManager = Cast<AWowSkyManager>(
				UGameplayStatics::GetActorOfClass(World, AWowSkyManager::StaticClass())))
			{
				SkyManager->ApplyWeather(WeatherType, Grade, Abrupt);
			}
		}
	});
	ConnectionManager->PacketHandler.OnMirrorTimerStarted.AddUObject(
		this, &AWowGameplayController::OnMirrorTimerStarted);
	ConnectionManager->PacketHandler.OnMirrorTimerPaused.AddUObject(
		this, &AWowGameplayController::OnMirrorTimerPaused);
	ConnectionManager->PacketHandler.OnMirrorTimerStopped.AddUObject(
		this, &AWowGameplayController::OnMirrorTimerStopped);
	ConnectionManager->PacketHandler.OnFriendListUpdated.AddUObject(
		this, &AWowGameplayController::OnFriendListUpdated);
	ConnectionManager->PacketHandler.OnPlayerNameReceived.AddUObject(
		this, &AWowGameplayController::OnSocialPlayerNameReceived);

	// Bind spell and action bar events
	ConnectionManager->PacketHandler.OnInitialSpells.AddUObject(
		this, &AWowGameplayController::OnInitialSpells);
    ConnectionManager->PacketHandler.OnKnownSpellsChanged.RemoveAll(this);
    ConnectionManager->PacketHandler.OnKnownSpellsChanged.AddWeakLambda(this, [this](bool bLearned)
    {
        FireUIEvent(TEXT("SPELLS_CHANGED"));
        if (bLearned) FireUIEvent(TEXT("LEARNED_SPELL_IN_TAB"), {TEXT("1")});
        FireUIEvent(TEXT("SPELL_UPDATE_USABLE"));
        FireUIEvent(TEXT("ACTIONBAR_UPDATE_USABLE"));
    });
	ConnectionManager->PacketHandler.OnActionButtonsUpdated.AddUObject(
		this, &AWowGameplayController::OnActionButtonsUpdated);
	ConnectionManager->PacketHandler.OnPetBarUpdated.AddUObject(
		this, &AWowGameplayController::OnPetBarUpdated);

	// Bind emote events
	ConnectionManager->PacketHandler.OnEmote.AddUObject(
		this, &AWowGameplayController::OnEmote);
	ConnectionManager->PacketHandler.OnTextEmote.AddUObject(
		this, &AWowGameplayController::OnTextEmote);
	BindUnitSounds();

	// Bind party/group events
	ConnectionManager->PacketHandler.OnGroupUpdated.AddUObject(
		this, &AWowGameplayController::OnGroupUpdated);
	ConnectionManager->PacketHandler.OnRaidTargetsUpdated.AddUObject(
		this, &AWowGameplayController::OnRaidTargetsUpdated);
	ConnectionManager->PacketHandler.OnReadyCheckUpdated.AddUObject(
		this, &AWowGameplayController::OnReadyCheckUpdated);
	ConnectionManager->PacketHandler.OnDuelUpdated.AddUObject(
		this, &AWowGameplayController::OnDuelUpdated);
	ConnectionManager->PacketHandler.OnGroupInviteReceived.AddUObject(
		this, &AWowGameplayController::OnGroupInviteReceived);
	ConnectionManager->PacketHandler.OnPartyCommandResult.AddUObject(
		this, &AWowGameplayController::OnPartyCommandResult);
	ConnectionManager->PacketHandler.OnLfgUpdated.AddUObject(
		this, &AWowGameplayController::OnLfgUpdated);

	// Bind taxi events
	ConnectionManager->PacketHandler.OnTaxiNodesShown.AddUObject(
		this, &AWowGameplayController::OnTaxiNodesShown);
	ConnectionManager->PacketHandler.OnTaxiActivateReply.AddUObject(
		this, &AWowGameplayController::OnTaxiActivateReply);

	// Listen for name query responses
	ConnectionManager->PacketHandler.OnPlayerNameReceived.AddUObject(
		this, &AWowGameplayController::OnPlayerNameReceived);
    ConnectionManager->PacketHandler.OnAurasChanged.AddWeakLambda(this, [this](uint64 Guid)
    {
        if (!ConnectionManager) return;
        SyncAuraVisuals(Guid);
        const auto& Manager = ConnectionManager->PacketHandler.EntityManager;
        if (Guid == Manager.LocalPlayerGuid) {RefreshDetectionAuraVisuals();UpdateBonusActionBar();}
        if (Guid == Manager.LocalPlayerGuid) { FireUIEvent(TEXT("UNIT_AURA"), {TEXT("player")}); FireUIEvent(TEXT("SPELL_UPDATE_USABLE")); FireUIEvent(TEXT("ACTIONBAR_UPDATE_USABLE")); }
        if (Guid == TargetGuid) FireUIEvent(TEXT("UNIT_AURA"), {TEXT("target")});
        if (UIManager && Guid == UIManager->GetFocusGuid()) FireUIEvent(TEXT("UNIT_AURA"), {TEXT("focus")});
    });
	ConnectionManager->PacketHandler.OnCreatureNameReceived.AddUObject(
		this, &AWowGameplayController::OnCreatureNameReceived);

	// Listen for action button updates to refresh action bar
	ConnectionManager->PacketHandler.OnOpcodeReceived.AddLambda([this](uint16 Opcode)
	{
		if (Opcode == WowOpcode::SMSG_ACTION_BUTTONS && ActionBarWidget.IsValid())
		{
			ActionBarWidget->RefreshActionButtons();
		}
	});

	// Listen for spell cooldowns to update action bar
	ConnectionManager->PacketHandler.OnSpellCooldown.AddLambda([this](uint32 SpellId, float Duration)
	{
		if (ActionBarWidget.IsValid())
		{
			ActionBarWidget->UpdateCooldown(SpellId, Duration);
		}
	});

	// Bind game UI events
	ConnectionManager->PacketHandler.OnLootOpened.AddLambda(
		[this](uint64 LootGuid, uint8 LootType, uint32 Gold, const TArray<FWowLootItem>& Items)
		{
			if (GameUI)
			{
				GameUI->ShowLootWindow(LootGuid, LootType, Gold, Items);
			}
			// r374 (Stefan: no animation when opening a chest): the lid opens, then stays open.
			GWowOpenedChestGuid = 0;
			if (AActor* Chest = SpawnedEntityActors.FindRef(LootGuid).Get(); Chest && Chest->Tags.Contains(TEXT("WowOpenableGameObject")))
				if (UWowSpellMeshComponent* Lid = FindObject<UWowSpellMeshComponent>(Chest, TEXT("GameObjectAnimatedMesh")))
					if (Lid->PlaySequenceById(148, false, 149)) GWowOpenedChestGuid = LootGuid;
		});

	ConnectionManager->PacketHandler.OnLootSlotRemoved.AddLambda(
		[this](uint8 Slot) { if (GameUI) GameUI->RemoveLootSlot(Slot); });
	ConnectionManager->PacketHandler.OnLootMoneyCleared.AddLambda(
		[this]()
		{
			if (GameUI) GameUI->ClearLootMoney();
			// Picking up coins plays the coin sound, as in the original client.
			if (AWowAudioManager* Audio = Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(this, AWowAudioManager::StaticClass())))
				Audio->PlayUiSound(TEXT("LOOTWINDOWCOINSOUND"));
		});
	ConnectionManager->PacketHandler.OnLootClosed.AddLambda(
		[this]()
		{
			if (GameUI)
			{
				GameUI->HideLootWindow();
			}
			// r374: a chest left with loot in it closes its lid again (an emptied one despawns and fades).
			if (GWowOpenedChestGuid)
			{
				if (AActor* Chest = SpawnedEntityActors.FindRef(GWowOpenedChestGuid).Get())
					if (UWowSpellMeshComponent* Lid = FindObject<UWowSpellMeshComponent>(Chest, TEXT("GameObjectAnimatedMesh")))
						Lid->PlaySequenceById(146, false, 0);
				GWowOpenedChestGuid = 0;
			}
		});

    ConnectionManager->PacketHandler.OnNpcServiceOpened.AddLambda([this]()
    {
        if(!GameUI || !ConnectionManager)return;
        PrepareForQuestDialogue();
        GameUI->HideQuestDialog();
        // Auctioneers get the Auction House window (AuctionFrame), trainers the trainer window.
        if(ConnectionManager->PacketHandler.AuctionOpen)
        {
            if(!AuctionHouseWidget.IsValid())
            {
                AuctionHouseWidget=SNew(SWowAuctionHouse,ConnectionManager).UIManager(UIManager);
                GameUI->AddManagedPanel(TEXT("NativeAuctionHouse"),AuctionHouseWidget.ToSharedRef(),AuctionHouseWidget->GetPanelSize(),
                    [this]() { if (AuctionHouseWidget.IsValid() && AuctionHouseWidget->IsOpen()) AuctionHouseWidget->Close(); });
            }
            if(NpcServicesWidget.IsValid())NpcServicesWidget->Close();
            AuctionHouseWidget->Open();
            return;
        }
        if(!NpcServicesWidget.IsValid())
        {
            NpcServicesWidget=SNew(SWowNpcServices,ConnectionManager);
            NpcServicesWidget->SetUIManager(UIManager);
            // ClassTrainerFrame art: 384 x 512 drawn at the quest window's 600/512; its art ends at
            // about 354 x 442 (HitRectInsets 0,30,0,70), so clicks beyond reach the world.
            GameUI->AddManagedPanel(TEXT("NativeNpcServices"),NpcServicesWidget.ToSharedRef(),FVector2D(450,600),
                [this]() { if (NpcServicesWidget.IsValid()) NpcServicesWidget->Close(); });
            GameUI->SetPanelHitInsets(TEXT("NativeNpcServices"),FMargin(0.f,0.f,30.f/384.f,70.f/512.f));
        }
        NpcServicesWidget->Open();
    });
	ConnectionManager->PacketHandler.OnVendorOpened.AddLambda(
		[this](uint64 VendorGuid, const TArray<FWowVendorItem>& Items)
		{
			if (GameUI)
			{
				GameUI->ShowVendorWindow(VendorGuid, Items);
			}
		});

	ConnectionManager->PacketHandler.OnTradeUpdated.AddLambda(
		[this](const FWowTradeState& TradeState)
		{
			if (!GameUI)
			{
				return;
			}

			const bool bShowTrade = TradeState.bTradeOpen
				|| TradeState.Status == WowTradeStatus::BEGIN_TRADE
				|| TradeState.Status == WowTradeStatus::BACK_TO_TRADE
				|| TradeState.Status == WowTradeStatus::TRADE_ACCEPT;

			if (bShowTrade)
			{
				GameUI->ShowTradeWindow(TradeState);
				if (UIManager && UIManager->GetEventSystem())
				{
					UIManager->GetEventSystem()->FireEvent(TEXT("TRADE_SHOW"));
				}
			}
			else
			{
				GameUI->HideTradeWindow();
				if (UIManager && UIManager->GetEventSystem())
				{
					UIManager->GetEventSystem()->FireEvent(TEXT("TRADE_CLOSED"));
				}
			}
		});

	ConnectionManager->PacketHandler.OnMailboxShown.AddLambda(
		[this](uint64 MailboxGuid)
		{
			ShowMailbox(static_cast<int64>(MailboxGuid));
		});

	ConnectionManager->PacketHandler.OnBankOpened.AddLambda(
		[this](uint64 BankerGuid)
		{
			// r282 (Stefan 13.10: "Bankbox needs the proper UI from WotLK"): the stock FrameXML BankFrame
			// opens on BANKFRAME_OPENED, like the client; the native placeholder window is no longer used.
			if (ConnectionManager) ConnectionManager->ActiveBankerGuid = BankerGuid;
			FireUIEvent(TEXT("BANKFRAME_OPENED"));
			UE_LOG(LogWowGameplay, Log, TEXT("Opened bank (BankFrame) for banker %llu"), BankerGuid);
		});

	// r285: guild bank packets -> the GUILDBANK* events Blizzard_GuildBankUI listens to.
	ConnectionManager->PacketHandler.OnGuildBankEvent.AddLambda(
		[this](const FString& EventName, int32 Argument)
		{
			if (Argument >= 0) FireUIEvent(EventName, {FString::FromInt(Argument)});
			else FireUIEvent(EventName);
		});

	ConnectionManager->PacketHandler.OnQuestDialog.AddLambda(
		[this](const FWowQuestDetails& QuestDetails)
		{
			PrepareForQuestDialogue();
			if (GameUI)
			{
				GameUI->ShowQuestDialog(QuestDetails);
			}
		});

	ConnectionManager->PacketHandler.OnQuestProgressDialog.AddLambda(
		[this](const FWowQuestDetails& QuestDetails)
		{
			PrepareForQuestDialogue();
			if (GameUI)
			{
				GameUI->ShowQuestProgressDialog(QuestDetails);
			}
		});

	ConnectionManager->PacketHandler.OnQuestRewardDialog.AddLambda(
		[this](const FWowQuestDetails& QuestDetails)
		{
			PrepareForQuestDialogue();
			if (GameUI)
			{
				GameUI->ShowQuestRewardDialog(QuestDetails);
			}
		});

	ConnectionManager->PacketHandler.OnGossipMessage.AddLambda(
		[this](const FWowGossipMenu& Menu)
		{
			if (GameUI)
			{
				PrepareForQuestDialogue();
				GameUI->ShowQuestList(Menu);
			}
		});
	ConnectionManager->PacketHandler.OnNpcText.AddLambda(
		[this](uint32 TextId, const FString& Greeting)
		{
			if (GameUI) GameUI->UpdateGossipGreeting(TextId, Greeting);
		});
	ConnectionManager->PacketHandler.OnGossipComplete.AddLambda(
		[this]()
		{
			if (GameUI)
			{
				GameUI->HideQuestDialog();
			}
		});
	ConnectionManager->PacketHandler.OnQuestGiverStatus.AddLambda(
		[this](uint64 QuestGiverGuid, uint8 Status)
		{
			if (TObjectPtr<UWidgetComponent>* Component = EntityNameplates.Find(QuestGiverGuid);
				Component && *Component)
			{
				if (UWowNameplateWidget* Widget = Cast<UWowNameplateWidget>((*Component)->GetUserWidgetObject()))
				{
					Widget->UpdateQuestStatus(Status);
				}
			}
		});
	ConnectionManager->PacketHandler.OnQuestAccepted.AddLambda(
		[this](uint32 QuestId)
		{
			// autoQuestWatch: a newly accepted quest goes straight onto the tracker.
			LoadTrackedQuests();
			if (QuestId && FWowClientPreferences::GetWorldOption(TEXT("autoquestwatch")) && ManualTrackedQuests.Num() < 25)
			{
				ManualTrackedQuests.Add(QuestId);
				SaveTrackedQuests();
			}
			ApplyQuestTracking();
            if (ConnectionManager)
                for (const auto& Quest : ConnectionManager->PacketHandler.QuestLog)
                    for (const auto& Objective : Quest.Objectives)
                        if (Objective.ItemId) ConnectionManager->RequestItemInfo(Objective.ItemId);
			UpdateQuestTracker();
			if (QuestLogWidget.IsValid()) QuestLogWidget->UpdateQuestList();
			if (ConnectionManager) ConnectionManager->SendQuestStatusMultipleQuery();
		});
	ConnectionManager->PacketHandler.OnQuestComplete.AddLambda(
		[this](uint32)
		{
			UpdateQuestTracker();
			if (QuestLogWidget.IsValid()) QuestLogWidget->UpdateQuestList();
			if (ConnectionManager) ConnectionManager->SendQuestStatusMultipleQuery();
		});
	ConnectionManager->PacketHandler.OnQuestLogUpdated.AddLambda(
		[this]()
		{
            if (ConnectionManager)
                for (const auto& Quest : ConnectionManager->PacketHandler.QuestLog)
                    for (const auto& Objective : Quest.Objectives)
                        if (Objective.ItemId) ConnectionManager->RequestItemInfo(Objective.ItemId);
			ApplyQuestTracking();
			FireUIEvent(TEXT("QUEST_LOG_UPDATE"));
			UpdateQuestTracker();
			if (QuestLogWidget.IsValid()) QuestLogWidget->UpdateQuestList();
			// Objective progress can turn an existing grey '?' into a yellow
			// turn-in marker without accepting or rewarding a quest. Refresh the
			// visible quest-giver statuses whenever the quest log changes so the
			// world markers do not stay at the login-time snapshot.
			if (ConnectionManager) ConnectionManager->SendQuestStatusMultipleQuery();
		});
	ConnectionManager->PacketHandler.OnQuestWatchChanged.AddLambda(
		[this]()
		{
			SyncTrackedQuestsFromLog();
			UpdateQuestTracker();
			if (QuestLogWidget.IsValid()) QuestLogWidget->UpdateQuestList();
			// r339: the stock WatchFrame redraws on QUEST_LOG_UPDATE.
			FireUIEvent(TEXT("QUEST_LOG_UPDATE"));
		});
	// r337 (Stefan 14.10, with Claude-Perf's r527): "Make this inn your home". SMSG_BINDER_CONFIRM fires
	// CONFIRM_BINDER with the place's name - UIParent shows StaticPopup "Do you want to make %s your new
	// home?" and Accept calls ConfirmBinder() (CMSG_BINDER_ACTIVATE, LuaGameActionsApi.cpp).
	ConnectionManager->PacketHandler.OnBinderConfirm.AddLambda(
		[this](uint64 InnkeeperGuid)
		{
			FString Place;
			if (auto* Terrain = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this, AWowWorldManager::StaticClass())))
			{
				Place = Terrain->GetCurrentAreaName();
				if (Place.IsEmpty()) Place = Terrain->GetCurrentZoneName();
			}
			if (Place.IsEmpty()) Place = TEXT("this inn");
			UE_LOG(LogWowGameplay, Log, TEXT("Binder confirm from %llu: asking to make %s the new home"), InnkeeperGuid, *Place);
			FireUIEvent(TEXT("CONFIRM_BINDER"), {Place});
		});
	// SMSG_PLAYERBOUND: the client's ERR_DEATHBIND_SUCCESS_S "%s is now your home." in yellow.
	ConnectionManager->PacketHandler.OnPlayerBound.AddLambda(
		[this](uint64, uint32 AreaId)
		{
			const auto* Area = AreaId ? FDbcStore::Get().AreaTable().GetById(AreaId) : nullptr;
			const FString Place = Area && !Area->Name.IsEmpty() ? Area->Name : FString(TEXT("This place"));
			AddFrameXmlChatLine(FString::Printf(TEXT("%s is now your home."), *Place), FLinearColor(1.0f, 1.0f, 0.0f, 1.0f));
		});

	// Setup GameUI connection manager
	if (GameUI)
	{
		GameUI->SetConnectionManager(ConnectionManager);
	}

	// Cache UIManager for tick dispatch
	if (UGameInstance* GI = GetGameInstance())
	{
		UIManager = GI->GetSubsystem<UWowUIManager>();
	}

	// Initialize managers once connection is established
	InitializeManagers();
}

void AWowGameplayController::OnLoginVerifyWorld(uint32 MapId, float X, float Y, float Z, float Orientation)
{
    auto* Terrain=Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this,AWowWorldManager::StaticClass()));
    if(!Terrain || !Terrain->SelectMap(MapId))
    {
        UE_LOG(LogWowGameplay,Error,TEXT("Cannot enter server map %u; refusing wrong-continent teleport"),MapId);
        return;
    }
    if (auto* Sky = Cast<AWowSkyManager>(UGameplayStatics::GetActorOfClass(this, AWowSkyManager::StaticClass())))
    {
        Sky->LoadLightZones(static_cast<int32>(MapId));
    }
	FVector SpawnPos = FWowCoordinate::WowToUE(X, Y, Z);

	UE_LOG(LogWowGameplay, Log, TEXT("LoginVerifyWorld: map=%d wow=(%.1f,%.1f,%.1f) orient=%.2f ue=(%.0f,%.0f,%.0f)"),
		MapId, X, Y, Z, Orientation, SpawnPos.X, SpawnPos.Y, SpawnPos.Z);

	if (bDeferSpawnTeleport)
	{
		// Store for later â€” LoginController will call ApplyDeferredSpawn() after terrain loads
		DeferredSpawnPos = SpawnPos;
		DeferredSpawnOrientation = Orientation;
		bHasDeferredSpawn = true;
		UE_LOG(LogWowGameplay, Log, TEXT("Spawn deferred â€” waiting for terrain to load"));
		return;
	}

	// Immediate teleport (legacy path)
	ApplyDeferredSpawn_Internal(SpawnPos, Orientation);

	// Fire UI events for entering the world
	FireUIEvent(TEXT("PLAYER_ENTERING_WORLD"));
	bPlayerEnteredWorld = true;
	FireUIEvent(TEXT("PLAYER_TARGET_CHANGED"));
	if (ConnectionManager)
	{
		ConnectionManager->SendQuestStatusMultipleQuery();
	}

	// Re-fire action bar events â€” action data may have arrived before UI was ready
	if (ConnectionManager)
	{
		const TArray<uint32>& AB = ConnectionManager->PacketHandler.ActionButtons;
		for (int32 i = 0; i < FMath::Min(AB.Num(), 120); ++i)
		{
			if (AB[i] != 0)
			{
				FireUIEvent(TEXT("ACTIONBAR_SLOT_CHANGED"), {FString::Printf(TEXT("%d"), i + 1)});
			}
		}
		FireUIEvent(TEXT("ACTIONBAR_UPDATE_STATE"));
		if (UIManager && UIManager->GetFrameManager())
		{
			UIManager->GetFrameManager()->RefreshActionButtonIcons(AB);
		}
	}

	// Re-fire unit frame events â€” entity health/mana data arrives after UI loads
	FireUIEvent(TEXT("UNIT_HEALTH"), {TEXT("player")});
	FireUIEvent(TEXT("UNIT_MAXHEALTH"), {TEXT("player")});
	FireUIEvent(TEXT("UNIT_MANA"), {TEXT("player")});
	FireUIEvent(TEXT("UNIT_MAXMANA"), {TEXT("player")});
	FireUIEvent(TEXT("UNIT_POWER"), {TEXT("player")});
	FireUIEvent(TEXT("PLAYER_FLAGS_CHANGED"));
	FireUIEvent(TEXT("UPDATE_SHAPESHIFT_FORM"));

	// Schedule delayed health/mana events to ensure bars get updated with real data
	// after the UI has had time to initialize and StatusBar widgets are created
	if (!bDelayedHealthUpdateFired)
	{
		GetWorldTimerManager().SetTimer(DelayedHealthUpdateTimer, [this]()
		{
			UE_LOG(LogWowGameplay, Log, TEXT("Firing delayed health/mana events for status bar updates"));
			FireUIEvent(TEXT("UNIT_HEALTH"), {TEXT("player")});
			FireUIEvent(TEXT("UNIT_MAXHEALTH"), {TEXT("player")});
			FireUIEvent(TEXT("UNIT_MANA"), {TEXT("player")});
			FireUIEvent(TEXT("UNIT_MAXMANA"), {TEXT("player")});
			FireUIEvent(TEXT("UNIT_POWER"), {TEXT("player")});
			if (TargetGuid != 0)
			{
				FireUIEvent(TEXT("UNIT_HEALTH"), {TEXT("target")});
				FireUIEvent(TEXT("UNIT_MAXHEALTH"), {TEXT("target")});
				FireUIEvent(TEXT("UNIT_MANA"), {TEXT("target")});
				FireUIEvent(TEXT("UNIT_MAXMANA"), {TEXT("target")});
				FireUIEvent(TEXT("UNIT_POWER"), {TEXT("target")});
			}
			bDelayedHealthUpdateFired = true;
		}, 3.0f, false); // Fire once after 3 seconds
	}
}

void AWowGameplayController::ApplyDeferredSpawn()
{
	if (!bHasDeferredSpawn) return;
	bHasDeferredSpawn = false;
	ApplyDeferredSpawn_Internal(DeferredSpawnPos, DeferredSpawnOrientation);

	// Fire UI events for entering the world
	FireUIEvent(TEXT("PLAYER_ENTERING_WORLD"));
	bPlayerEnteredWorld = true;
	FireUIEvent(TEXT("PLAYER_TARGET_CHANGED"));
	if (ConnectionManager)
	{
		ConnectionManager->SendQuestStatusMultipleQuery();
	}

	// Re-fire action bar events â€” action data may have arrived before UI was ready
	if (ConnectionManager)
	{
		const TArray<uint32>& AB = ConnectionManager->PacketHandler.ActionButtons;
		for (int32 i = 0; i < FMath::Min(AB.Num(), 120); ++i)
		{
			if (AB[i] != 0)
			{
				FireUIEvent(TEXT("ACTIONBAR_SLOT_CHANGED"), {FString::Printf(TEXT("%d"), i + 1)});
			}
		}
		FireUIEvent(TEXT("ACTIONBAR_UPDATE_STATE"));
		if (UIManager && UIManager->GetFrameManager())
		{
			UIManager->GetFrameManager()->RefreshActionButtonIcons(AB);
		}
	}

	// Re-fire unit frame events â€” entity health/mana data arrives after UI loads
	FireUIEvent(TEXT("UNIT_HEALTH"), {TEXT("player")});
	FireUIEvent(TEXT("UNIT_MAXHEALTH"), {TEXT("player")});
	FireUIEvent(TEXT("UNIT_MANA"), {TEXT("player")});
	FireUIEvent(TEXT("UNIT_MAXMANA"), {TEXT("player")});
	FireUIEvent(TEXT("UNIT_POWER"), {TEXT("player")});
	FireUIEvent(TEXT("PLAYER_FLAGS_CHANGED"));
	FireUIEvent(TEXT("UPDATE_SHAPESHIFT_FORM"));
}

void AWowGameplayController::ApplyDeferredSpawn_Internal(const FVector& SpawnPos, float Orientation)
{
	APawn* P = GetPawn();
	if (P)
	{
		if (AWowPlayerCharacter* PlayerCharacter = Cast<AWowPlayerCharacter>(P))
		{
			PlayerCharacter->ApplyLoginSpawn(SpawnPos, Orientation);
		}
		else
		{
			P->SetActorLocation(SpawnPos);
			float Pitch = 0.0f;
			const float UEYaw = FWowCoordinate::WowOrientationToUEYaw(Orientation);
			P->SetActorRotation(FRotator(Pitch, UEYaw, 0.0f));
			if (APlayerController* PC = Cast<APlayerController>(P->GetController()))
			{
				PC->SetControlRotation(FRotator(Pitch, UEYaw, 0.0f));
			}
		}

		UE_LOG(LogWowGameplay, Log, TEXT("Teleported to spawn: ue=(%.0f,%.0f,%.0f)"),
			SpawnPos.X, SpawnPos.Y, SpawnPos.Z);
	}

	bHasServerPosition = true;
	LastServerPosition = SpawnPos;
	LastSentYawDegrees = TNumericLimits<float>::Lowest();
		ResetOutgoingMovementState();
    LastSentMovementFlags = 0;
	DesiredFacingYawDegrees = FWowCoordinate::WowOrientationToUEYaw(Orientation);
	bHasDesiredFacingYaw = true;
	GameplayCameraYawDegrees = DesiredFacingYawDegrees;
	GameplayCameraPitchDegrees = FMath::Clamp(GetControlRotation().Pitch, MinMouseLookPitch, MaxMouseLookPitch);
	bHasGameplayCameraRotation = true;
	ApplyGameplayCameraRotation();

	// Lua FrameXML is the primary UI â€” no Slate fallbacks.
}

// Other players fighting with their weapons sheathed (r244, Stefan): a player whose client (or
// bot) never sends CMSG_SETSHEATHED keeps sheath state 0 on the server while auto-attacking, and
// was shown punching with the weapons on the hip. While a remote player swings, show the melee
// weapons drawn; 8 s after the last swing the server's sheath state applies again.
static TMap<uint64, double> GRemoteCombatDrawnUntil;
static constexpr double RemoteCombatDrawSeconds = 8.0;

void AWowGameplayController::OnEntityUpdated(const FWowEntity& Entity)
{
	if (!ConnectionManager) return;
	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
    if(Entity.Guid==LocalGuid)UpdateBonusActionBar();
	if (Entity.IsPlayer() && Entity.Guid != LocalGuid && SpawnedEntityActors.Contains(Entity.Guid))
	{
		const FWowPlayerEntity& RemotePlayer = static_cast<const FWowPlayerEntity&>(Entity);
        // Pose flags (sheath/rest/PvP) must never destroy and respawn the model.
        // Refresh equipment in place, except while a spell temporarily owns it.
        if (!ActiveTimedCastSpells.Contains(Entity.Guid))
        {
            AActor* PresentationActor = SpawnedEntityRiderActors.FindRef(Entity.Guid);
            if (!PresentationActor) PresentationActor = SpawnedEntityActors.FindRef(Entity.Guid);
            uint8 SheathForPose = RemotePlayer.GetSheathState();
            if (SheathForPose == 0)
                if (const double* DrawnUntil = GRemoteCombatDrawnUntil.Find(Entity.Guid);
                    DrawnUntil && FPlatformTime::Seconds() < *DrawnUntil) SheathForPose = 1;
            const FName PoseTag(*FString::Printf(TEXT("WowWeaponPose=%u:%u"),
                SheathForPose, RemotePlayer.GetMountDisplayId()));
            if (PresentationActor && !PresentationActor->Tags.Contains(PoseTag))
            {
                // Only a sheath change on foot (same mount state as before) plays the gesture;
                // the first presentation and mounting/dismounting stay instant.
                uint32 OldSheath = 0, OldMount = 0;
                bool bHadPose = false;
                for (const FName& Tag : PresentationActor->Tags)
                {
                    FString Value;
                    if (Tag.ToString().Split(TEXT("WowWeaponPose="), nullptr, &Value))
                    {
                        FString SheathText, MountText;
                        if (Value.Split(TEXT(":"), &SheathText, &MountText))
                        {
                            OldSheath = FCString::Atoi(*SheathText);
                            OldMount = FCString::Atoi(*MountText);
                            bHadPose = true;
                        }
                    }
                }
                const uint8 NewSheath = SheathForPose;
                const uint32 NewMount = RemotePlayer.GetMountDisplayId();
                PresentationActor->Tags.RemoveAll([](FName Tag) { return Tag.ToString().StartsWith(TEXT("WowWeaponPose=")); });
                PresentationActor->Tags.Add(PoseTag);
                const bool bGesture = bHadPose && NewMount == 0 && OldMount == 0 && OldSheath != NewSheath;
                TWeakObjectPtr<AActor> WeakActor(PresentationActor);
                ApplyWeaponPoseWithGesture(PresentationActor,
                    bGesture ? SpawnedEntityAnimationControllers.FindRef(Entity.Guid).Get() : nullptr,
                    [WeakActor, NewSheath, NewMount]()
                    {
                        if (AActor* Actor = WeakActor.Get()) ApplyRemoteWeaponPresentation(Actor, NewSheath, NewMount != 0);
                    });
            }
        }
        const uint32 NewSignature = GetPlayerAppearanceSignature(RemotePlayer);
		uint32* PreviousSignature = SpawnedEntityAppearanceSignatures.Find(Entity.Guid);
		if (PreviousSignature && *PreviousSignature != NewSignature)
		{
			uint32* PendingSignature = PendingEntityAppearanceSignatures.Find(Entity.Guid);
			if (!PendingSignature || *PendingSignature != NewSignature)
			{
				PendingEntityAppearanceSignatures.Add(Entity.Guid, NewSignature);
				const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
				PendingEntityAppearanceStableAt.Add(Entity.Guid, Now + 0.75f);
			}
		}
		else if (!PreviousSignature)
		{
			SpawnedEntityAppearanceSignatures.Add(Entity.Guid, NewSignature);
		}
	}

	if (Entity.IsUnit() && Entity.Guid != ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
	{
		const FWowUnitEntity& Unit = static_cast<const FWowUnitEntity&>(Entity);
		if (Unit.IsQuestGiver() && !QuestStatusQueriesSent.Contains(Entity.Guid))
		{
			QuestStatusQueriesSent.Add(Entity.Guid);
			ConnectionManager->SendQuestStatusQuery(static_cast<int64>(Entity.Guid));
		}
	}

    if (Entity.IsUnit() && !Entity.IsPlayer() && FMath::IsFinite(Entity.Scale) && Entity.Scale > 0.0f)
    {
        if (TObjectPtr<AActor>* Actor = SpawnedEntityActors.Find(Entity.Guid); Actor && IsValid(Actor->Get()))
            (*Actor)->SetActorScale3D(FVector(WowUnitActorScale(Entity)));
    }
	// Track health changes for combat log and death detection
	static TMap<uint64, int32> LastKnownHealth;
	static TMap<uint64, int32> LastKnownMana;
	int32 CurrentHealth = Entity.GetHealth();
	int32 CurrentMana = 0;
	if (Entity.IsUnit())
	{
		const FWowUnitEntity* UnitEntity = static_cast<const FWowUnitEntity*>(&Entity);
		CurrentMana = UnitEntity->GetPower(0); // 0 = mana
	}
	int32* OldHealthPtr = LastKnownHealth.Find(Entity.Guid);
	int32* OldManaPtr = LastKnownMana.Find(Entity.Guid);

	bool HealthChanged = (OldHealthPtr && *OldHealthPtr != CurrentHealth) || (!OldHealthPtr && CurrentHealth > 0);
	bool ManaChanged = Entity.IsUnit() && ((OldManaPtr && *OldManaPtr != CurrentMana) || (!OldManaPtr && CurrentMana > 0));

	if (HealthChanged && OldHealthPtr)
	{
		OnEntityHealthChanged(Entity, *OldHealthPtr, CurrentHealth);
	}

	// Fire UI events for health/mana changes
	if (Entity.Guid == LocalGuid)
	{
        // Visible-item enchantments include permanent and spell-applied temporary
        // IDs. Change only their effect roots; the item's own visual stays attached.
        if (Entity.IsPlayer() && CachedMpq && CachedAssetCache && GetPawn())
        {
            TArray<UStaticMeshComponent*> Equipment;
            GetPawn()->GetComponents<UStaticMeshComponent>(Equipment);
            for (UStaticMeshComponent* Mesh : Equipment)
                for (FName Tag : Mesh->ComponentTags)
                {
                    FString SlotText = Tag.ToString();
                    if (!SlotText.RemoveFromStart(TEXT("WowInventorySlot="))) continue;
                    const uint16 Field = PlayerField::VISIBLE_ITEM_BASE + FCString::Atoi(*SlotText) * 2 + 1;
                    if (Entity.Fields.Contains(Field))
                        FWowEquipmentManager::UpdateEquipmentEnchantment(CachedMpq, CachedAssetCache, Mesh,
                            FWowEquipmentManager::ResolveEnchantmentVisual(CachedMpq, Entity.GetField(Field)));
                    break;
                }
        }
		// Keep re-firing all unit events on every entity update until MaxHealth is valid.
		// Entity fields arrive across multiple SMSG_UPDATE_OBJECT packets; health/mana
		// fields may not be present in the first update.
		if (!bPlayerHealthInitialized)
		{
			FireUIEvent(TEXT("UNIT_HEALTH"), {TEXT("player")});
			FireUIEvent(TEXT("UNIT_MAXHEALTH"), {TEXT("player")});
			FireUIEvent(TEXT("UNIT_MANA"), {TEXT("player")});
			FireUIEvent(TEXT("UNIT_MAXMANA"), {TEXT("player")});
			FireUIEvent(TEXT("UNIT_POWER"), {TEXT("player")});
			if (Entity.GetMaxHealth() > 0)
			{
				bPlayerHealthInitialized = true;
				FireUIEvent(TEXT("UNIT_LEVEL"), {TEXT("player")});
				FireUIEvent(TEXT("PLAYER_FLAGS_CHANGED"));
				UE_LOG(LogWowGameplay, Log, TEXT("Player health initialized (MaxHP=%d HP=%d)"),
					Entity.GetMaxHealth(), Entity.GetHealth());
			}
		}

		if (HealthChanged)
		{
			FireUIEvent(TEXT("UNIT_HEALTH"), {TEXT("player")});
			FireUIEvent(TEXT("UNIT_MAXHEALTH"), {TEXT("player")});
		}
		if (ManaChanged)
		{
			FireUIEvent(TEXT("UNIT_MANA"), {TEXT("player")});
			FireUIEvent(TEXT("UNIT_MAXMANA"), {TEXT("player")});
			FireUIEvent(TEXT("UNIT_POWER"), {TEXT("player")});
		}

		// Check for combat state changes on local player
		if (Entity.IsUnit())
		{
			const FWowUnitEntity* UnitEntity = static_cast<const FWowUnitEntity*>(&Entity);
			const uint8 ServerSheathState = UnitEntity->GetSheathState();
			if (!bLocalSheathStateInitialized || LocalSheathState != ServerSheathState)
			{
				// A server-side change (leaving combat, spells, macros) plays the same gesture as Z.
				const bool bGesture = bLocalSheathStateInitialized && !bIsAutoAttacking;
				LocalSheathState = ServerSheathState;
				bLocalSheathStateInitialized = true;
				AWowPlayerCharacter* SheathChar = Cast<AWowPlayerCharacter>(GetPawn());
				GetWorldTimerManager().ClearTimer(SheathSwapTimer);
				const float GestureLength = bGesture && SheathChar && SheathChar->AnimationController
					? SheathChar->AnimationController->PlaySheathAnimation(HasHipSheathedWeapon(SheathChar)) : 0.f;
				if (GestureLength > 0.1f)
					GetWorldTimerManager().SetTimer(SheathSwapTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { ApplyLocalWeaponPresentation(); }),
						GestureLength * 0.45f, false);
				else
					ApplyLocalWeaponPresentation();
			}
			// Server stand-state changes (logout, /sit from macros, chairs) also play the
			// authored sit-down / stand-up transition instead of snapping to the loop.
			const bool bServerSitting = UnitEntity->IsSitting();
			if (bServerSitting != bLocalSitting)
			{
				AWowPlayerCharacter* SitChar = Cast<AWowPlayerCharacter>(GetPawn());
				const bool bGroundSit = UnitEntity->GetFieldByte(UnitField::BYTES_1, 0) == WowStandState::SIT;
				if (SitChar && SitChar->AnimationController && (!bServerSitting || bGroundSit))
				{
					bLocalSitTransition = SitChar->AnimationController->PlayAnimationById(
						bServerSitting ? EWowAnimId::SitGroundDown : EWowAnimId::SitGroundUp, false);
				}
			}
			bLocalSitting = bServerSitting;
			const uint64 CurrentPetGuid = UnitEntity->GetField64(UnitField::SUMMON);
			if (CurrentPetGuid != LastKnownLocalPetGuid)
			{
				LastKnownLocalPetGuid = CurrentPetGuid;
				FireUIEvent(TEXT("UNIT_PET"), {TEXT("player")});
				FireUIEvent(TEXT("PET_UI_UPDATE"));

				if (CurrentPetGuid != 0)
				{
					ConnectionManager->SendRequestPetInfo();
				}
				else
				{
					ConnectionManager->PacketHandler.PetActionBar.Clear();
					OnPetBarUpdated(ConnectionManager->PacketHandler.PetActionBar);
				}
			}

			uint32 UnitFlags = UnitEntity->GetUnitFlags();
			bool bIsInCombat = (UnitFlags & 0x00080000) != 0; // UNIT_FLAG_IN_COMBAT

			if (bIsInCombat != bWasInCombat)
			{
				if (bIsInCombat)
				{
					FireUIEvent(TEXT("PLAYER_ENTER_COMBAT"));
					FireUIEvent(TEXT("PLAYER_REGEN_DISABLED"));
				}
				else
				{
					FireUIEvent(TEXT("PLAYER_LEAVE_COMBAT"));
					FireUIEvent(TEXT("PLAYER_REGEN_ENABLED"));
				}
				bWasInCombat = bIsInCombat;
			}

			// Check for player flags changes
			static uint32 LastPlayerFlags = 0;
			if (LastPlayerFlags != UnitFlags && LastPlayerFlags != 0)
			{
				FireUIEvent(TEXT("PLAYER_FLAGS_CHANGED"));
			}
			LastPlayerFlags = UnitFlags;
		}
	}
	else if (Entity.Guid == TargetGuid)
	{
		if (HealthChanged)
		{
			FireUIEvent(TEXT("UNIT_HEALTH"), {TEXT("target")});
			FireUIEvent(TEXT("UNIT_MAXHEALTH"), {TEXT("target")});
		}
		if (ManaChanged)
		{
			FireUIEvent(TEXT("UNIT_MANA"), {TEXT("target")});
			FireUIEvent(TEXT("UNIT_MAXMANA"), {TEXT("target")});
		}
	}

	// Only start tracking once we've seen a positive health value
	// This prevents false death triggers from entities whose fields aren't populated yet
	if (CurrentHealth > 0 || OldHealthPtr)
	{
		LastKnownHealth.Add(Entity.Guid, CurrentHealth);
	}
	if (Entity.IsUnit() && (CurrentMana > 0 || OldManaPtr))
	{
		LastKnownMana.Add(Entity.Guid, CurrentMana);
	}

	if (Entity.Guid == LocalGuid)
	{
		// Our own equipment changes (r258, Stefan: new shoulders never showed). The local model was
		// built once from the character list and never followed VISIBLE_ITEM changes. Rebuild it
		// 0.6 s after the visible items (or customization) settle.
		if (Entity.IsPlayer())
		{
			static uint64 GLocalLookGuid = 0;
			static uint32 GLocalLookSignature = 0;
			static FTimerHandle GLocalLookTimer;
			const uint32 Look = GetPlayerAppearanceSignature(static_cast<const FWowPlayerEntity&>(Entity));
			// r307 (Stefan: an axe equipped right after entering the world still showed the fishing
			// pole): the model is first built from the character-list snapshot, and gear changed
			// before this first sighting (or during login) was taken as the baseline and never
			// applied. Rebuild once from the live fields when the local player first shows up.
			// Per pawn, so a relog (new pawn, same character) also gets its one rebuild.
			static TWeakObjectPtr<APawn> GLocalLookPawn;
			// r224: Show Helm / Show Cloak follow the server's player flags (also when another
			// client or the server changes them): the Interface options mirror them and the stock
			// Interface Options panel hears PLAYER_FLAGS_CHANGED("player").
			{
				static uint32 GLastHiddenGear = MAX_uint32;
				const uint32 HiddenGear = static_cast<const FWowPlayerEntity&>(Entity).GetPlayerFlags() & 0xC00u;
				if (HiddenGear != GLastHiddenGear)
				{
					GLastHiddenGear = HiddenGear;
					FWowClientPreferences::SetWorldOption(TEXT("showhelm"), (HiddenGear & 0x400u) == 0);
					FWowClientPreferences::SetWorldOption(TEXT("showcloak"), (HiddenGear & 0x800u) == 0);
					FireUIEvent(TEXT("PLAYER_FLAGS_CHANGED"), {TEXT("player")});
				}
			}
			// r324: AFK (0x2) / DND (0x4) changes print the client's system lines (MARKED_AFK_MESSAGE ...).
			{
				static uint64 GAwayGuid = 0;
				static uint32 GLastAway = 0;
				const uint32 Away = static_cast<const FWowPlayerEntity&>(Entity).GetPlayerFlags() & 0x6u;
				if (GAwayGuid != Entity.Guid) { GAwayGuid = Entity.Guid; GLastAway = Away; }
				else if (Away != GLastAway)
				{
					const FLinearColor SystemYellow(1.0f, 1.0f, 0.0f, 1.0f);
					if ((Away & 0x2u) && !(GLastAway & 0x2u))
						AddFrameXmlChatLine(FString::Printf(TEXT("You are now AFK: %s"),
							GWowAwayMessage.IsEmpty() ? TEXT("Away from Keyboard") : *GWowAwayMessage), SystemYellow);
					else if (!(Away & 0x2u) && (GLastAway & 0x2u))
						AddFrameXmlChatLine(TEXT("You are no longer AFK."), SystemYellow);
					if ((Away & 0x4u) && !(GLastAway & 0x4u))
						AddFrameXmlChatLine(FString::Printf(TEXT("You are now DND: %s."),
							GWowAwayMessage.IsEmpty() ? TEXT("Do not Disturb") : *GWowAwayMessage), SystemYellow);
					else if (!(Away & 0x4u) && (GLastAway & 0x4u))
						AddFrameXmlChatLine(TEXT("You are no longer marked DND."), SystemYellow);
					GLastAway = Away;
					FireUIEvent(TEXT("PLAYER_FLAGS_CHANGED"), {TEXT("player")});
				}
			}
			const bool bFirstSighting = GLocalLookGuid != Entity.Guid || GLocalLookPawn.Get() != GetPawn();
			if (bFirstSighting)
			{
				GLocalLookGuid = Entity.Guid;
				GLocalLookPawn = GetPawn();
				GLocalLookSignature = Look;
				GetWorldTimerManager().SetTimer(GLocalLookTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					RebuildLocalAppearanceFromFields(0);
				}), 1.5f, false);
			}
			else if (Look != GLocalLookSignature)
			{
				GLocalLookSignature = Look;
				GetWorldTimerManager().SetTimer(GLocalLookTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					RebuildLocalAppearanceFromFields(0);
				}), 0.6f, false);
			}
		}
		// Apply server speeds to character
		if (Entity.Movement.RunSpeed > 0.0f)
		{
			if (AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(GetPawn()))
			{
				PlayerChar->ApplyServerSpeeds(Entity.Movement.RunSpeed, Entity.Movement.WalkSpeed);
			}
		}

		// Check if this is the first time we're receiving entity data for the local player
		// and set up character model if we haven't already
		if (AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(GetPawn()))
		{
			// Check if the player already has a skeletal mesh set
			if (!PlayerChar->GetMesh()->GetSkeletalMeshAsset())
			{
				SetupLocalPlayerCharacterModel(Entity);
			}
		}

		// Server position tracking (correction disabled â€” causes rubber-banding)
		// The client is authoritative for movement; server corrections only
		// needed for teleports which are handled via OnTeleportRequest
		if (Entity.Movement.Position != FVector::ZeroVector)
		{
			LastServerPosition = FWowCoordinate::WowToUE(Entity.Movement.Position);
		}
	}
	else
	{
		// CREATE_OBJECT can precede usable model fields or the world asset cache.
		// Keep the entity pending; the throttled spawn queue will retry without
		// loading a crowd of models on one frame.
		if ((Entity.IsUnit() || Entity.IsPlayer() || Entity.IsGameObject()) &&
			!SpawnedEntityActors.Contains(Entity.Guid))
		{
			QueueEntityModelSpawn(Entity.Guid);
		}

		// Check for death â€” entity health=0 with valid MaxHealth, or StandState=DEAD
		if (Entity.IsUnit() && Entity.Guid != LocalGuid)
		{
			int32 EntityHealth = Entity.GetHealth();
			int32 EntityMaxHealth = Entity.GetMaxHealth();
			uint8 StandState = Entity.GetFieldByte(UnitField::BYTES_1, 0);

			// Only mark as dead via standState (authoritative) or confirmed zero health
			// with valid maxHealth. Don't mark dead when both are 0 (no data yet).
			bool bIsDead = (StandState == WowStandState::DEAD) ||
				(EntityMaxHealth > 0 && EntityHealth <= 0);
			if (bIsDead && !DeadEntityGuids.Contains(Entity.Guid))
			{
				UE_LOG(LogWowGameplay, Log, TEXT("OnEntityUpdated: Entity %llu is dead (health=%d, maxHealth=%d, standState=%d)"),
					Entity.Guid, EntityHealth, EntityMaxHealth, StandState);
				OnEntityDeath(Entity.Guid);
			}
			// Resurrect if previously dead but now has health
			else if (!bIsDead && DeadEntityGuids.Contains(Entity.Guid) && EntityHealth > 0)
			{
                DeadEntityGuids.Remove(Entity.Guid);
                if (auto* ResurrectedActor = SpawnedEntityActors.Find(Entity.Guid); ResurrectedActor && *ResurrectedActor)
                    if (auto* Anim = FWowCharacterBuilder::GetAnimationController(*ResurrectedActor))
                        Anim->SetDead(false);
				UE_LOG(LogWowGameplay, Log, TEXT("Entity %llu resurrected (health=%d)"), Entity.Guid, EntityHealth);
			}
		}

		// Update nameplate health if needed
		TObjectPtr<UWidgetComponent>* NameplatePtr = EntityNameplates.Find(Entity.Guid);
		if (NameplatePtr && *NameplatePtr)
		{
			UWidgetComponent* NameplateComponent = *NameplatePtr;
			if (UWowNameplateWidget* NameplateWidget = Cast<UWowNameplateWidget>(NameplateComponent->GetUserWidgetObject()))
			{
				int32 Health = Entity.GetHealth();
				int32 MaxHealth = Entity.GetMaxHealth();
				if (Health > 0 && MaxHealth > 0)
				{
					NameplateWidget->UpdateHealth(Health, MaxHealth);
				}
			}
		}
	}

	// Party member health/power changes drive PartyMemberFrameN (UNIT_* with "partyN").
	if (ConnectionManager && Entity.IsPlayer() && Entity.Guid != ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
	{
		const FWowGroupInfo& Group = ConnectionManager->PacketHandler.GroupInfo;
		if (!Group.IsRaidGroup())
			for (int32 Index = 0; Index < FMath::Min(4, Group.Members.Num()); ++Index)
				if (Group.Members[Index].Guid == Entity.Guid)
				{
					const FWowUnitEntity& Unit = static_cast<const FWowUnitEntity&>(Entity);
					static TMap<uint64, uint64> LastPartyStamp;
					const uint64 Stamp = (uint64(uint32(Unit.GetHealth())) << 32) ^ uint64(uint32(Unit.GetMaxHealth())) * 31
						^ uint64(uint32(Unit.GetPower(Unit.GetPowerTypeId()))) * 131;
					if (LastPartyStamp.FindRef(Entity.Guid) == Stamp) break;
					LastPartyStamp.Add(Entity.Guid, Stamp);
					const FString Token = FString::Printf(TEXT("party%d"), Index + 1);
					for (const TCHAR* Event : {TEXT("UNIT_HEALTH"), TEXT("UNIT_MAXHEALTH"), TEXT("UNIT_POWER"), TEXT("UNIT_MAXPOWER")})
						FireUIEvent(Event, {Token});
					break;
				}
	}
}

void AWowGameplayController::TogglePhotoMode()
{
    if (!PhotoMode) return;
    if (PhotoMode->IsActive()) { PhotoMode->Exit(); return; }
    // Only in the world with a character, and not while a modal native window is up.
    if (!GetPawn() || !(bPlayerEnteredWorld || IsWowOfflineSession()) || IsNativeModalUiVisible()) return;
    PhotoMode->Enter();
}

bool AWowGameplayController::HandlePhotoModeTap()
{
    if (!PhotoMode || !PhotoMode->IsActive()) return false;
    PhotoMode->TakePhoto();
    return true;
}

// r642 (Stefan: "the turning is still a bit sharp and sudden"): isometric turns ease out (fast at first,
// settling gently) instead of a constant 720 degrees per second; at least 90 degrees per second.
static float WowIsoSmoothTurnYaw(float Current, float Target, float DeltaSeconds)
{
    const float Delta = FMath::FindDeltaAngleDegrees(Current, Target);
    const float Step = FMath::Max(FMath::Abs(Delta) * (1.0f - FMath::Exp(-9.0f * DeltaSeconds)), 90.0f * DeltaSeconds);
    return FMath::Abs(Delta) <= Step ? Target : Current + FMath::Sign(Delta) * Step;
}
void AWowGameplayController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
    UpdateSheathFollowers(); // r686
    if (PhotoMode && PhotoMode->IsActive()) PhotoMode->Tick(FApp::GetDeltaTime());
    // Particles and spell effects (skulls, sparks, rain) are translucent: by default they are
    // drawn after depth of field and stayed razor sharp in a blurred scene. With depth of
    // field on, draw them before it so they blur with the world around them.
    if (IConsoleVariable* SeparateTranslucency = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SeparateTranslucency")))
    {
        // Set once per change, at code priority (game-setting priority was refused every frame
        // and the warning flooded the log, a small hitch each time).
        static int32 LastWant = -1;
        const int32 Want = FWowClientPreferences::IsDepthOfFieldEnabled() ? 0 : 1;
        if (Want != LastWant) { LastWant = Want; SeparateTranslucency->Set(Want, ECVF_SetByCode); }
    }
    // VR: the interface zones go on panels around the player (created once VR is used).
    if (APawn* VrPawn = GetPawn(); VrPawn && FWowVRSupport::IsActive() && !VrPawn->FindComponentByClass<UWowVrUiPresenter>())
    {
        UWowVrUiPresenter* Presenter = NewObject<UWowVrUiPresenter>(VrPawn, TEXT("WowVrUiPresenter"));
        Presenter->SetupAttachment(VrPawn->GetRootComponent());
        Presenter->RegisterComponent();
    }
    {
        // The NPC/player collision option applies to units already in the world when it is
        // toggled, not only to ones spawned afterwards.
        // Corpses never block (you could stand on a dead mob); living units follow the option.
        // Re-checked twice a second so deaths and resurrections are picked up too.
        static double NextEntityCollisionCheck = 0.0;
        const double CollisionNow = FPlatformTime::Seconds();
        if (CollisionNow >= NextEntityCollisionCheck)
        {
            NextEntityCollisionCheck = CollisionNow + 0.5;
            const bool bEntityCollision = FWowClientPreferences::IsNpcAndPlayerCollisionEnabled();
            for (const auto& Pair : SpawnedEntityActors)
            {
                AActor* Unit = Pair.Value.Get();
                if (!Unit) continue;
                TInlineComponentArray<UPrimitiveComponent*> Prims(Unit);
                bool bIsUnit = false;
                for (UPrimitiveComponent* Prim : Prims) if (Prim && Prim->ComponentHasTag(TEXT("WowTargetProxy"))) bIsUnit = true;
                if (!bIsUnit) continue; // game objects always block
                const bool bDead = DeadEntityGuids.Contains(Pair.Key) || FeignDeadEntityGuids.Contains(Pair.Key);
                const ECollisionResponse Want = bEntityCollision && !bDead ? ECR_Block : ECR_Ignore;
                for (UPrimitiveComponent* Prim : Prims)
                    if (Prim && Prim->GetCollisionResponseToChannel(ECC_Pawn) != Want)
                        Prim->SetCollisionResponseToChannel(ECC_Pawn, Want);
            }
        }
    }
    // Standing up or moving during the logout/exit countdown cancels it, as in WoW.
    if (bSatForLogout && !bLocalSitting && GameMenuCountdownViewportWidget.IsValid()
        && GameMenuCountdownViewportWidget->GetVisibility().IsVisible())
        CancelGameMenuCountdown();
    for (auto It = GroundTilts.CreateIterator(); It; ++It)
        if (!It.Key().IsValid() || !It.Value().Tick(It.Key().Get(), DeltaTime)) It.RemoveCurrent();
    UpdateChannelBeams();
    UpdateLootBeams();
    AnimateSpellVisualLights();
    RedirectFrameXmlFriendsFrame();
    // FrameXML's GameMenuFrame (mostly empty buttons here) never shows: Esc pressed while our Esc
    // menu was open closed ours and opened it (Stefan 10.10). Opened on its own (an addon, a
    // binding) it becomes our menu; right after ours was closed it is simply hidden.
    if (UIManager && UIManager->GetFrameManager())
    {
        static bool GNativeMenuWasOpen = false;
        FWowFrameManager* Frames = UIManager->GetFrameManager();
        const int64 GameMenu = Frames->FindFrame(TEXT("GameMenuFrame"));
        if (GameMenu >= 0 && Frames->IsFrameVisible(GameMenu))
        {
            Frames->SetFrameVisible(GameMenu, false);
            if (!IsGameMenuVisible() && !GNativeMenuWasOpen) ShowGameMenu();
        }
        GNativeMenuWasOpen = IsGameMenuVisible();
        // The micro buttons open FrameXML's QuestLogFrame / PlayerTalentFrame directly (ToggleFrame),
        // which draw empty here (Stefan 10.10: empty quest log and talents next to each other):
        // show our windows instead, like the L and N keys do.
        // Auction addons: AUCTION_HOUSE_SHOW/CLOSED with the native window and the list events when
        // the server's lists arrive; Blizzard's AuctionFrame (UIParent loads it on SHOW) stays hidden
        // because the native window is the auction house here.
        {
            static bool bAuctionWasOpen = false;
            static uint32 SeenList = 0, SeenBids = 0, SeenOwn = 0;
            const bool bAuctionOpen = AuctionHouseWidget.IsValid() && AuctionHouseWidget->IsOpen();
            if (bAuctionOpen != bAuctionWasOpen) FireUIEvent(bAuctionOpen ? TEXT("AUCTION_HOUSE_SHOW") : TEXT("AUCTION_HOUSE_CLOSED"));
            bAuctionWasOpen = bAuctionOpen;
            if (ConnectionManager)
            {
                const auto& Handler = ConnectionManager->PacketHandler;
                if (Handler.AuctionListRevision != SeenList) { SeenList = Handler.AuctionListRevision; FireUIEvent(TEXT("AUCTION_ITEM_LIST_UPDATE")); }
                if (Handler.AuctionBidRevision != SeenBids) { SeenBids = Handler.AuctionBidRevision; FireUIEvent(TEXT("AUCTION_BIDDER_LIST_UPDATE")); }
                if (Handler.AuctionOwnRevision != SeenOwn) { SeenOwn = Handler.AuctionOwnRevision; FireUIEvent(TEXT("AUCTION_OWNED_LIST_UPDATE")); }
            }
            const int64 BlizzardAuctionFrame = Frames->FindFrame(TEXT("AuctionFrame"));
            if (BlizzardAuctionFrame >= 0 && Frames->IsFrameVisible(BlizzardAuctionFrame)) Frames->SetFrameVisible(BlizzardAuctionFrame, false);
        }
        // Blizzard_AchievementUI's AchievementFrame is the achievement window now (r342,
        // LuaAchievementApi.cpp); only the VR UI still swaps it for the native window.
        const int64 FrameXmlAchievements = Frames->FindFrame(TEXT("AchievementFrame"));
        if (FWowUiVrHost::IsActive() && FrameXmlAchievements >= 0 && Frames->IsFrameVisible(FrameXmlAchievements))
        {
            Frames->SetFrameVisible(FrameXmlAchievements, false);
            OnAchievementKey();
        }
        const int64 FrameXmlQuestLog = Frames->FindFrame(TEXT("QuestLogFrame"));
        if (FrameXmlQuestLog >= 0 && Frames->IsFrameVisible(FrameXmlQuestLog))
        {
            // FrameXML's ToggleFrame always finds it hidden (we hide it at once), so every press
            // of the micro button arrives here: toggle ours so the same button closes it again.
            Frames->SetFrameVisible(FrameXmlQuestLog, false);
            OnQuestLogKey();
        }
        static IConsoleVariable* NativeTalentsVar = IConsoleManager::Get().FindConsoleVariable(TEXT("wow.NativeTalents"));
        if (!NativeTalentsVar || NativeTalentsVar->GetInt() != 0)
            for (const TCHAR* TalentFrameName : { TEXT("PlayerTalentFrame"), TEXT("TalentFrame") })
            {
                const int64 FrameXmlTalents = Frames->FindFrame(TalentFrameName);
                if (FrameXmlTalents >= 0 && Frames->IsFrameVisible(FrameXmlTalents))
                {
                    Frames->SetFrameVisible(FrameXmlTalents, false);
                    OnTalentKey(); // toggles, like the quest log above
                    break;
                }
            }
        // Repair a PvP window whose tab page was hidden on its own (older Esc handling): the parent
        // then shows with nothing inside it.
        const int64 PvpParent = Frames->FindFrame(TEXT("PVPParentFrame"));
        if (PvpParent >= 0 && Frames->IsFrameVisible(PvpParent))
        {
            const int64 PvpPage = Frames->FindFrame(TEXT("PVPFrame"));
            const int64 BattlegroundPage = Frames->FindFrame(TEXT("PVPBattlegroundFrame"));
            if (PvpPage >= 0 && !Frames->IsFrameVisible(PvpPage) &&
                (BattlegroundPage < 0 || !Frames->IsFrameVisible(BattlegroundPage)))
                if (FWowEventSystem* PvpEvents = UIManager->GetEventSystem())
                    PvpEvents->ExecuteLuaString(TEXT("if PVPFrame then PVPFrame:Show() end"), TEXT("=PvpPageRepair"));
        }
        // The paper-doll model was built once from the character-list snapshot and never
        // followed gear changes (Stefan 10.10: new staff, gloves). Rebuild it from the live
        // VISIBLE_ITEM fields whenever they change while the character sheet is open.
        if (CharacterSheetPreview && ConnectionManager)
        {
            const int64 CharacterFrame = Frames->FindFrame(TEXT("CharacterFrame"));
            const FWowPlayerEntity* Me = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
            static TWeakObjectPtr<AWowCharacterPreview> GPaperDollPreview;
            static uint32 GPaperDollSignature = 0;
            // r411 (Stefan: the sky stayed brighter after closing the character sheet with Esc): Esc
            // hides CharacterFrame through Lua, not through the character key, so the preview kept
            // rendering its scene capture every frame with its studio lights on. Follow the frame.
            static bool GPaperDollShown = false;
            const bool bSheetShown = CharacterFrame >= 0 && Frames->IsFrameVisible(CharacterFrame);
            if (bSheetShown != GPaperDollShown)
            {
                GPaperDollShown = bSheetShown;
                CharacterSheetPreview->SetPreviewActive(bSheetShown);
            }
            if (Me && CharacterFrame >= 0 && Frames->IsFrameVisible(CharacterFrame))
            {
                const uint32 Signature = GetPlayerAppearanceSignature(*Me);
                if (GPaperDollPreview.Get() != CharacterSheetPreview || Signature != GPaperDollSignature)
                {
                    GPaperDollPreview = CharacterSheetPreview;
                    GPaperDollSignature = Signature;
                    FWowCharacterInfo Info;
                    ConnectionManager->GetSelectedCharacterInfo(Info);
                    Info.Race = Me->GetRaceId();
                    Info.Gender = Me->GetGenderId();
                    Info.Skin = Me->GetFieldByte(PlayerField::BYTES, 0);
                    Info.Face = Me->GetFieldByte(PlayerField::BYTES, 1);
                    Info.HairStyle = Me->GetFieldByte(PlayerField::BYTES, 2);
                    Info.HairColor = Me->GetFieldByte(PlayerField::BYTES, 3);
                    Info.FacialHair = Me->GetFieldByte(PlayerField::BYTES_2, 0);
                    Info.Equipment.Reset();
                    for (uint8 Slot = 0; Slot < 19; ++Slot)
                    {
                        // The paper doll holds the main/off-hand weapons; a wand or bow would take the right hand.
                        if (Slot == PlayerField::EQUIPMENT_SLOT_RANGED) continue;
                        // r224: the paper doll follows Show Helm / Show Cloak like the original.
                        if ((Slot == PlayerField::EQUIPMENT_SLOT_HEAD && (Me->GetPlayerFlags() & 0x400u)) ||
                            (Slot == PlayerField::EQUIPMENT_SLOT_BACK && (Me->GetPlayerFlags() & 0x800u))) continue;
                        const uint32 Entry = Me->GetField(PlayerField::VISIBLE_ITEM_BASE + Slot * 2);
                        if (!Entry) continue;
                        uint32 DisplayId = 0, InventoryType = 0;
                        if (const FWowItemTemplateInfo* Template = ConnectionManager->PacketHandler.ItemTemplates.Find(Entry))
                        {
                            DisplayId = Template->DisplayId;
                            InventoryType = Template->InventoryType;
                        }
                        else if (const FItemDbcEntry* Item = FDbcStore::Get().Items().GetById(Entry))
                        {
                            DisplayId = Item->DisplayInfoID;
                            InventoryType = uint32(Item->InventoryType);
                        }
                        if (!DisplayId)
                        {
                            GPaperDollSignature = 0; // retry once the item template arrives
                            continue;
                        }
                        // A one-hand weapon in the off-hand slot belongs in the left hand.
                        if (Slot == PlayerField::EQUIPMENT_SLOT_OFFHAND && (InventoryType == 13 || InventoryType == 21))
                            InventoryType = 22;
                        FWowCharacterEquipment& Equipment = Info.Equipment.AddDefaulted_GetRef();
                        Equipment.DisplayId = int32(DisplayId);
                        Equipment.InventoryType = uint8(InventoryType);
                    }
                    CharacterSheetPreview->ShowCharacter(Info);
                    CharacterSheetPreview->SetPreviewActive(true);
                    UE_LOG(LogWowGameplay, Log, TEXT("Paper doll rebuilt from live gear (%d items)"), Info.Equipment.Num());
                }
            }
        }
    }
    UpdateDeathState(DeltaTime);
    // Follow (unit menu): walk after the followed player; stops on own movement keys, when the
    // player is gone or beyond 40 yards (ERR_TOO_FAR_TO_FOLLOW), like the client's auto-follow.
    if (GWowFollowGuid && ConnectionManager && GetPawn())
    {
        static const float YardCm = (FWowCoordinate::WowToUE(FVector(1, 0, 0)) - FWowCoordinate::WowToUE(FVector::ZeroVector)).Size();
        const TObjectPtr<AActor>* Followed = SpawnedEntityActors.Find(GWowFollowGuid);
        bool bStop = !Followed || !IsValid(Followed->Get());
        for (const FKey& Key : { EKeys::W, EKeys::A, EKeys::S, EKeys::D, EKeys::Q, EKeys::E, EKeys::SpaceBar, EKeys::Up, EKeys::Down })
            bStop |= IsInputKeyDown(Key);
        if (!bStop)
        {
            FVector Delta = Followed->Get()->GetActorLocation() - GetPawn()->GetActorLocation();
            Delta.Z = 0.0;
            const double Distance = Delta.Size();
            if (Distance > 40.0 * YardCm) bStop = true;
            else if (Distance > 3.0 * YardCm)
            {
                FRotator Facing = GetControlRotation();
                Facing.Yaw = Delta.Rotation().Yaw;
                SetControlRotation(Facing);
                GetPawn()->AddMovementInput(Delta.GetSafeNormal());
            }
        }
        if (bStop)
        {
            const FString* FollowedName = ConnectionManager->PacketHandler.PlayerNameCache.Find(GWowFollowGuid);
            AddFrameXmlChatLine(FString::Printf(TEXT("You are no longer following %s."), FollowedName ? **FollowedName : TEXT("your target")),
                FLinearColor(1.f, 1.f, 0.f));
            GWowFollowGuid = 0;
        }
    }
    if (DeathManager) DeathManager->Tick(DeltaTime); // death/ghost world visuals
    FWowStealthVisual::Tick(this, DeltaTime); // r605: stealth look, enter/leave sounds, detection range
    WowAutoRetaliateTick(this); // r605: detarget stops the automatic attack-back
    // r637 (Stefan: "both mouse buttons to move forward broke with this"): in the isometric camera, holding
    // both buttons walks towards the cursor (steering as it moves) and stops when either button is let go.
    {
        static bool GIsoChord = false;
        bool bLeftHeld = false, bRightHeld = false;
        const bool bChord = FWowIsometricCamera::IsActive() && IsWowClientProcessForeground()
            && TryGetPhysicalInputDown(EKeys::LeftMouseButton, bLeftHeld) && TryGetPhysicalInputDown(EKeys::RightMouseButton, bRightHeld)
            && bLeftHeld && bRightHeld;
        if (bChord)
        {
            WowIsoClickMove::GPressPending = false; // releasing the buttons is not a right click
            WowIsoClickMove::GLeftChorded = true;   // r650: nor a left click
            FVector ChordGround;
            if (WowIsoGroundUnderCursor(this, ChordGround))
            {
                if (!GIsoChord || !WowIsoClickMove::GActive)
                {
                    if (APawn* ChordPawn = GetPawn()) WowIsoClickMove::GLastProgressPos = ChordPawn->GetActorLocation();
                    WowIsoClickMove::GLastProgressSeconds = FPlatformTime::Seconds();
                }
                if (AWowPlayerCharacter* ChordRunner = Cast<AWowPlayerCharacter>(GetPawn())) ChordRunner->bIsAutoRunning = false; // r662
                WowIsoClickMove::GActive = true;
                WowIsoClickMove::GHolding = true;
                WowIsoClickMove::GGuid = 0;
                WowIsoClickMove::GArrivedAt = 0.0;
                WowIsoClickMove::GTarget = ChordGround;
                GWowFollowGuid = 0;
            }
        }
        else if (GIsoChord) WowIsoClickMove::Stop();
        GIsoChord = bChord;
    }
    // r621: isometric click-to-move (started in OnRightClick).
    if (WowIsoClickMove::GActive)
    {
        AWowPlayerCharacter* MoveChar = Cast<AWowPlayerCharacter>(GetPawn());
        static const float IsoYardCm = (FWowCoordinate::WowToUE(FVector(1, 0, 0)) - FWowCoordinate::WowToUE(FVector::ZeroVector)).Size();
        const double NowMove = FPlatformTime::Seconds();
        bool bStopMove = !MoveChar || !FWowIsometricCamera::IsActive() || IsMovementHeldBySitting()
            || !MoveChar->GetCurrentMoveInput2D().IsNearlyZero() || MoveChar->bIsAutoRunning
            || FWowKeyBindings::Held(TEXT("MOVEFORWARD")) || FWowKeyBindings::Held(TEXT("MOVEBACKWARD"))
            || FWowKeyBindings::Held(TEXT("STRAFELEFT")) || FWowKeyBindings::Held(TEXT("STRAFERIGHT"))
            || FWowKeyBindings::Held(TEXT("JUMP")); // r651: the turn keys turn the camera, they do not stop the walk
        float StopDistance = 0.6f * IsoYardCm;
        // r623 (Stefan: "right-click will use slot 1 by default ... else auto-attack"): right-clicking an
        // enemy walks into range of action slot 1 (on the current page) and uses it; an empty slot 1
        // starts auto-attack. Melee abilities also start auto-attack, like the original client.
        auto IsoSlotOne = [this](uint32& OutActionId, uint8& OutType) -> int32
        {
            const int32 Slot = 12 * (FWowKeyBindings::ActionPage() - 1);
            OutActionId = 0; OutType = 0;
            if (ConnectionManager && ConnectionManager->PacketHandler.ActionButtons.IsValidIndex(Slot))
            {
                const uint32 Packed = ConnectionManager->PacketHandler.ActionButtons[Slot];
                OutActionId = Packed & 0x00FFFFFF;
                OutType = uint8((Packed >> 24) & 0xFF);
            }
            return Slot;
        };
        // SpellRange.dbc ids of 3.3.5 (0 = melee / self). Unknown ids walk to 10 yards to be safe.
        auto IsoSpellRangeYards = [](uint32 SpellId) -> float
        {
            const auto* Spell = FDbcStore::Get().Spells().GetById(SpellId);
            if (!Spell) return 0.0f;
            switch (Spell->RangeIndex)
            {
            case 1: case 2: return 0.0f;
            case 3: return 20.0f;
            case 4: return 30.0f;
            case 5: return 40.0f;
            case 6: return 100.0f;
            case 7: return 10.0f;
            case 34: return 25.0f;
            default: return 10.0f;
            }
        };
        auto IsoEnemy = [this](uint64 Guid) -> bool
        {
            const FWowEntity* Entity = ConnectionManager ? ConnectionManager->PacketHandler.EntityManager.Find(Guid) : nullptr;
            return Entity && Entity->IsUnit() && !DeadEntityGuids.Contains(Guid) && IsWorldUnitAttackable(*Entity);
        };
        if (!bStopMove && WowIsoClickMove::GGuid)
        {
            const TObjectPtr<AActor>* Goal = SpawnedEntityActors.Find(WowIsoClickMove::GGuid);
            if (!Goal || !IsValid(Goal->Get())) bStopMove = true;
            else
            {
                WowIsoClickMove::GTarget = Goal->Get()->GetActorLocation();
                StopDistance = 2.5f * IsoYardCm;
                uint32 SlotAction = 0; uint8 SlotType = 0;
                IsoSlotOne(SlotAction, SlotType);
                if (SlotAction && SlotType == 0 && IsoEnemy(WowIsoClickMove::GGuid))
                {
                    const float Yards = IsoSpellRangeYards(SlotAction);
                    if (Yards > 0.0f) StopDistance = FMath::Max(2.5f, Yards - 3.0f) * IsoYardCm;
                }
            }
        }
        else if (!bStopMove && WowIsoClickMove::GHolding && NowMove - WowIsoClickMove::GLastSteerSeconds > 0.1)
        {
            // Holding the right button steers towards the cursor.
            WowIsoClickMove::GLastSteerSeconds = NowMove;
            bool bRightHeld = false;
            if (TryGetPhysicalInputDown(EKeys::RightMouseButton, bRightHeld) && !bRightHeld) WowIsoClickMove::GHolding = false;
            FVector Ground;
            if (WowIsoClickMove::GHolding && WowIsoGroundUnderCursor(this, Ground)) WowIsoClickMove::GTarget = Ground;
        }
        if (!bStopMove)
        {
            const FVector Here = MoveChar->GetActorLocation();
            FVector Delta = WowIsoClickMove::GTarget - Here;
            Delta.Z = 0.0;
            if (Delta.Size() <= StopDistance && !WowIsoClickMove::GHolding)
            {
                const uint64 Arrived = WowIsoClickMove::GGuid;
                if (Arrived && WowIsoClickMove::GArrivedAt <= 0.0)
                {
                    // r623 (Stefan via Claude-UI: "turns its back to the NPC while the quest window opens"):
                    // face what was clicked before talking to it, looting it or attacking it.
                    if (!Delta.IsNearlyZero()) MoveChar->SetActorRotation(FRotator(0.0f, Delta.Rotation().Yaw, 0.0f));
                    // r633 (Stefan: walking up to a Cactus Apple gave "Can't do that while moving"): stop first and
                    // act a moment later, once the stop has gone to the server.
                    if (UCharacterMovementComponent* ArriveMove = MoveChar->GetCharacterMovement()) ArriveMove->StopMovementImmediately();
                    WowIsoClickMove::GArrivedAt = NowMove;
                }
                else if (!Arrived || (NowMove - WowIsoClickMove::GArrivedAt > 0.3 && MoveChar->GetVelocity().Size2D() < 5.0))
                {
                WowIsoClickMove::Stop();
                if (Arrived)
                {
                    if (DeadEntityGuids.Contains(Arrived)) SendLootRequest(Arrived);
                    else if (IsoEnemy(Arrived))
                    {
                        if (TargetGuid != Arrived) SetTarget(Arrived);
                        uint32 SlotAction = 0; uint8 SlotType = 0;
                        const int32 Slot = IsoSlotOne(SlotAction, SlotType);
                        if (!SlotAction) StartAutoAttack();
                        else
                        {
                            CastSpellFromSlot(Slot);
                            if (SlotType == 0 && IsoSpellRangeYards(SlotAction) <= 0.0f) StartAutoAttack();
                        }
                    }
                    else InteractWithWorldEntity(Arrived);
                }
                }
            }
            else if (Delta.Size() > StopDistance)
            {
                // Stuck against a wall or a cliff: give up after 1.5 s without progress.
                if (FVector::DistSquared2D(Here, WowIsoClickMove::GLastProgressPos) > FMath::Square(0.5f * IsoYardCm))
                {
                    WowIsoClickMove::GLastProgressPos = Here;
                    WowIsoClickMove::GLastProgressSeconds = NowMove;
                }
                if (NowMove - WowIsoClickMove::GLastProgressSeconds > 1.5) bStopMove = true;
                else
                {
                    // r625: turn smoothly (720 degrees per second) instead of snapping, like the keys.
                    MoveChar->SetActorRotation(FRotator(0.0f, WowIsoSmoothTurnYaw(MoveChar->GetActorRotation().Yaw, Delta.Rotation().Yaw, DeltaTime), 0.0f));
                    MoveChar->AddMovementInput(Delta.GetSafeNormal(), 1.0f);
                }
            }
        }
        if (bStopMove) WowIsoClickMove::Stop();
    }
    {
        // Options > Experimental "HD character model": rebuild our own model when it is toggled.
        FWowVRSupport::SyncWithOption(this, bPlayerEnteredWorld && bGameplayHudVisible); // Options > Experimental "VR headset (preview)", Ctrl+Shift+V
        const bool bHDOption = FWowClientPreferences::GetWorldOption(TEXT("hdcharacters"));
        if (bHDOptionSeen && bHDOption != bLastHDOption && ConnectionManager)
            if (const FWowEntity* Me = ConnectionManager->PacketHandler.EntityManager.Find(
                ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid))
            {
                // r481 (Stefan: HD off left the orc naked, shoulders and sheathed weapons on the floor):
                // rebuild from the live VISIBLE_ITEM / PLAYER_BYTES fields like a gear change. The login
                // path without cached equipment built a bare model and left the HD gear components
                // socketed to HD-only bones, which fall to the mesh root on the classic skeleton.
                if (Me->IsPlayer()) RebuildLocalAppearanceFromFields(0);
                else SetupLocalPlayerCharacterModel(*Me);
            }
        bLastHDOption = bHDOption;
        bHDOptionSeen = true;
    }
    {
        // r354: barber shop - our model wears the look chosen in the window, and the camera turns to
        // the face (as the client does in the chair); leaving the chair gives the old zoom back.
        static bool bGBarberPreview = false;
        static uint32 GBarberLook = 0;
        uint8 BarberSkin = 0, BarberHair = 0, BarberColor = 0, BarberFacial = 0;
        const bool bPreview = FWowBarberShop::GetPreview(BarberSkin, BarberHair, BarberColor, BarberFacial);
        const uint32 Look = bPreview ? (uint32(BarberSkin) | (uint32(BarberHair) << 8) | (uint32(BarberColor) << 16) | (uint32(BarberFacial) << 24)) : 0u;
        if (bPreview != bGBarberPreview || Look != GBarberLook)
        {
            bGBarberPreview = bPreview;
            GBarberLook = Look;
            if (ConnectionManager && Cast<AWowPlayerCharacter>(GetPawn())) RebuildLocalAppearanceFromFields(0);
        }
        static bool bGBarberCamera = false;
        static float GBarberSavedZoom = 0.f;
        static double GBarberCameraSince = 0.0;
        AWowPlayerCharacter* Seated = Cast<AWowPlayerCharacter>(GetPawn());
        const bool bBarberOpen = FWowBarberShop::IsOpen();
        if (Seated && Seated->CameraBoom)
        {
            if (bBarberOpen && !bGBarberCamera)
            {
                bGBarberCamera = true;
                GBarberSavedZoom = Seated->CameraBoom->TargetArmLength;
                GBarberCameraSince = FPlatformTime::Seconds();
                Seated->CameraBoom->TargetArmLength = 260.0f;
            }
            else if (!bBarberOpen && bGBarberCamera)
            {
                bGBarberCamera = false;
                Seated->CameraBoom->TargetArmLength = GBarberSavedZoom;
            }
            // Face the character while the server seats it (it turns us to the chair's facing).
            if (bGBarberCamera && FPlatformTime::Seconds() - GBarberCameraSince < 2.0)
                SetControlRotation(FRotator(-8.0f, Seated->GetActorRotation().Yaw + 180.0f, 0.0f));
        }
    }
    if (bLocalStandingUp)
    {
        // Input from this frame is dropped before the movement component consumes it.
        if (ACharacter* Standing = Cast<ACharacter>(GetPawn()))
        {
            Standing->ConsumeMovementInputVector();
            if (UCharacterMovementComponent* Movement = Standing->GetCharacterMovement())
                if (!Movement->IsFalling()) Movement->StopMovementImmediately();
        }
        if (FPlatformTime::Seconds() >= LocalStandUpLockUntil) bLocalStandingUp = false;
    }
	// Lua ShowAddonManager() / ReloadUI() are requested from inside Lua and handled here.
	if (UWowUIManager::ConsumeAddonManagerRequest())
	{
		SWowAddonList::OpenInViewport(true);
	}
	// A reload requested at character select (AddOns list changed) waits until the world UI is up.
	// r359 (Stefan 15.10: Okay / Reload UI in the AddOns list did nothing): the offline world (-offline)
	// has no server session, so its state never reaches WorldInGame and the request waited forever.
	static const bool bOfflineWorldSession = FParse::Param(FCommandLine::Get(), TEXT("offline"));
	if (((ConnectionManager && ConnectionManager->GetState() == EWowSessionState::WorldInGame) || (bOfflineWorldSession && bPlayerEnteredWorld))
		&& UIManager && UIManager->IsUILoaded() && UWowUIManager::ConsumeReloadRequest())
	{
		ReloadUiNow();
	}
	if (ConnectionManager)
	{
		// The local player entity only got a position at login; keep it in sync with the pawn
		// so spell range checks (action bar tint, IsActionInRange) use where we actually stand.
		auto& EntityMgr = ConnectionManager->PacketHandler.EntityManager;
		APawn* LocalPawn = GetPawn();
		if (ACharacter* LocalChar = Cast<ACharacter>(LocalPawn))
			if (FWowEntity* Me = EntityMgr.GetLocalPlayer())
				Me->Movement.Position = FWowCoordinate::UEToWow(FWowLoginPlacement::Feet(*LocalChar));
		static double NextSightCheck = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now >= NextSightCheck)
		{
			NextSightCheck = Now + 0.2;
			bool bSight = true;
			AActor* TargetActor = nullptr;
			if (const TObjectPtr<AActor>* Found = SpawnedEntityActors.Find(static_cast<uint64>(ConnectionManager->GetTargetGuid())))
				TargetActor = Found->Get();
			if (LocalPawn && TargetActor && GetWorld())
			{
				// Line of sight: terrain/WMO (WorldStatic) only, from eye height to the target's chest.
				FCollisionQueryParams Params(SCENE_QUERY_STAT(WowTargetSight), false);
				Params.AddIgnoredActor(LocalPawn);
				Params.AddIgnoredActor(TargetActor);
				FCollisionObjectQueryParams Objects(ECC_WorldStatic);
				FHitResult Hit;
				bSight = !GetWorld()->LineTraceSingleByObjectType(Hit,
					LocalPawn->GetActorLocation() + FVector(0, 0, 60),
					TargetActor->GetActorLocation() + FVector(0, 0, 40), Objects, Params);
			}
			FWowSpellPresentation::SetTargetInSight(bSight);
		}
	}
    TickOfflineTestBridge();
    TickAreaTriggers();
    if (!ConnectionManager || ConnectionManager->GetState()!=EWowSessionState::WorldInGame) PendingNamedChat.Empty();
    if (ConnectionManager && !PendingNamedChat.IsEmpty())
    {
        TArray<FPendingNamedChat> Ready;
        for (int32 I=0;I<PendingNamedChat.Num();)
        {
            const auto& Chat=PendingNamedChat[I];
            if (ConnectionManager->PacketHandler.PlayerNameCache.Contains(Chat.Guid) || FPlatformTime::Seconds()>=Chat.Expires)
            { Ready.Add(Chat); PendingNamedChat.RemoveAt(I); } else ++I;
        }
        for (const auto& Chat:Ready)
            OnChatMessage(Chat.Type,Chat.Language,Chat.Guid,GetPlayerDisplayName(Chat.Guid),Chat.Message,Chat.Channel);
    }
    // r435: release the teleport hold once the floor under the destination exists (see GWowTeleportHold).
    if (GWowTeleportHold)
    {
        ACharacter* HeldCharacter = Cast<ACharacter>(GetPawn());
        const double Held = FPlatformTime::Seconds() - GWowTeleportHoldSince;
        bool bGround = false;
        if (HeldCharacter && GetWorld())
        {
            HeldCharacter->SetActorLocation(GWowTeleportHoldAt, false, nullptr, ETeleportType::TeleportPhysics);
            const float Half = HeldCharacter->GetCapsuleComponent() ? HeldCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(WowTeleportGround), false, HeldCharacter);
            FHitResult Hit;
            // The server's height is the floor's: wait for a floor within 1.5 m of it (an inn's floor,
            // not the terrain under it); after 8 s any ground within 6 m will do.
            const float Reach = Held < 8.0 ? 150.f : 600.f;
            bGround = GetWorld()->LineTraceSingleByChannel(Hit, GWowTeleportHoldAt + FVector(0, 0, 50),
                GWowTeleportHoldAt - FVector(0, 0, Half + Reach), ECC_Pawn, Query) && Hit.ImpactNormal.Z > 0.5f;
        }
        // r439: with the loading screen up, also wait until the area's buildings and doodads have
        // streamed in and stayed settled for a second (the transfer screen's rule), at most 30 s.
        bool bAreaReady = true;
        if (GWowTeleportScreen && OfflineTransferScreen.IsValid())
            if (auto* Terrain = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this, AWowWorldManager::StaticClass())))
            {
                const double Now = FPlatformTime::Seconds();
                const uint64 Revision = Terrain->GetObjectStreamingRevision();
                if (Revision != GWowTeleportRevision || !Terrain->IsInitialLoadComplete()) { GWowTeleportRevision = Revision; GWowTeleportStableSince = Now; }
                const float Fraction = float(Terrain->GetInitialTilesLoaded()) / FMath::Max(1, Terrain->GetInitialTilesQueued());
                OfflineTransferScreen->SetProgress(.05f + .9f * Fraction * (bGround ? 1.f : .9f), TEXT("Loading..."));
                bAreaReady = Held > 30.0 || (Terrain->IsInitialLoadComplete() && Terrain->GetObjectStreamingPassCount() >= 2 && Now - GWowTeleportStableSince >= 1.0);
            }
        if (!HeldCharacter || (bGround && bAreaReady) || Held > (GWowTeleportScreen ? 30.0 : 20.0) || bMapTerrainPending)
        {
            GWowTeleportHold = false;
            if (GWowTeleportScreen)
            {
                GWowTeleportScreen = false;
                if (auto* Terrain = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this, AWowWorldManager::StaticClass())))
                    Terrain->EndWorldEntryWarmup();
                FinishOfflineTransferLoading();
            }
            if (HeldCharacter && HeldCharacter->GetCharacterMovement() && !bServerRooted && !bMapTerrainPending)
                HeldCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
            UE_LOG(LogWowGameplay, Log, TEXT("Teleport hold released after %.1f s (%s)"), Held, bGround ? TEXT("ground found") : TEXT("timeout"));
        }
    }
    // r297: world audio mutes during transfers and map changes (UI sounds keep playing).
    AWowAudioManager::SetWorldLoading(AWowAudioManager::LoadingTransfer, OfflineTransferScreen.IsValid() || bMapTerrainPending);
    if (OfflineTransferScreen.IsValid()) return;
    if(bMapTerrainPending)
    {
        auto* Terrain=Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this,AWowWorldManager::StaticClass()));
        if(!Terrain || !Terrain->IsTerrainReadyAtPosition(LastServerPosition)) return;
        bMapTerrainPending=false;
        if(auto* MovingPawn=Cast<ACharacter>(GetPawn())) MovingPawn->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    }

    // Keep the stock computer/menu button tied to actual network health.
    if (bGameplayHudVisible && UIManager && UIManager->GetFrameManager())
    {
        auto* Frames = UIManager->GetFrameManager();
        if (UButton* Button = Frames->GetButtonWidget(Frames->FindFrame(TEXT("MainMenuMicroButton"))))
        {
            const int32 Latency = ConnectionManager ? ConnectionManager->GetWorldLatencyMs() : 0;
            Button->SetBackgroundColor(Latency >= 500 ? FLinearColor(1.0f, 0.12f, 0.12f)
                : Latency >= 250 ? FLinearColor(1.0f, 0.8f, 0.2f) : FLinearColor::White);
        }
    }
	UpdatePerformanceOverlay(DeltaTime);
	// r365 (Stefan 16.10: dragging a spell on Bartender's bar did not light up the other slots, so it
	// could not be put anywhere and was lost): the client fires ACTIONBAR_SHOWGRID when the cursor
	// picks up something an action button can take (spell, action, item, macro) and
	// ACTIONBAR_HIDEGRID when it is put down or cleared, plus CURSOR_UPDATE on every change. Action
	// bars (the stock multi-bars, Bartender4, Dominos ...) show their empty buttons on SHOWGRID; nothing
	// fired them here, so empty buttons stayed hidden and there was nothing to drop onto.
	if (ConnectionManager && UIManager && UIManager->IsUILoaded() && UIManager->GetEventSystem())
	{
		static FString GLastCursorKey;
		static bool bGridShown = false;
		FString CursorType, CursorDetail;
		int32 CursorId = 0;
		const bool bHasCursor = ConnectionManager->GetCursorInfo(CursorType, CursorId, CursorDetail);
		const FString CursorKey = bHasCursor ? FString::Printf(TEXT("%s:%d:%s"), *CursorType, CursorId, *CursorDetail) : FString();
		if (CursorKey != GLastCursorKey)
		{
			GLastCursorKey = CursorKey;
			FWowEventSystem* CursorEvents = UIManager->GetEventSystem();
			const bool bGridType = bHasCursor && (CursorType == TEXT("spell") || CursorType == TEXT("action")
				|| CursorType == TEXT("item") || CursorType == TEXT("macro"));
			if (bGridShown && !bGridType) { bGridShown = false; CursorEvents->FireEvent(TEXT("ACTIONBAR_HIDEGRID")); }
			CursorEvents->FireEvent(TEXT("CURSOR_UPDATE"));
			if (bGridType && !bGridShown) { bGridShown = true; CursorEvents->FireEvent(TEXT("ACTIONBAR_SHOWGRID")); }
		}
	}
	// Drop hidden windows from UIParent's panel slots (see __WowSanitizeUIPanels in WowUIManager).
	{
		static double NextPanelSanitize = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now >= NextPanelSanitize && UIManager && UIManager->GetEventSystem())
		{
			NextPanelSanitize = Now + .25;
			UIManager->GetEventSystem()->ExecuteLuaString(TEXT("if __WowSanitizeUIPanels then __WowSanitizeUIPanels() end"), TEXT("=SanitizeUIPanels"));
			// r282: walking away from the banker closes the bank, like the client.
			if (ConnectionManager && ConnectionManager->ActiveBankerGuid && GetPawn())
			{
				const TObjectPtr<AActor>* Banker = SpawnedEntityActors.Find(ConnectionManager->ActiveBankerGuid);
				if (!Banker || !*Banker || FVector::Dist((*Banker)->GetActorLocation(), GetPawn()->GetActorLocation()) > 1200.0f)
					UIManager->GetEventSystem()->ExecuteLuaString(TEXT("if CloseBankFrame then CloseBankFrame() end"), TEXT("=CloseBankFrame"));
			}
			// r285: the same for the guild vault.
			if (ConnectionManager && ConnectionManager->ActiveGuildBankGuid && GetPawn())
			{
				const TObjectPtr<AActor>* Vault = SpawnedEntityActors.Find(ConnectionManager->ActiveGuildBankGuid);
				if (!Vault || !*Vault || FVector::Dist((*Vault)->GetActorLocation(), GetPawn()->GetActorLocation()) > 1200.0f)
					UIManager->GetEventSystem()->ExecuteLuaString(TEXT("if CloseGuildBankFrame then CloseGuildBankFrame() end"), TEXT("=CloseGuildBankFrame"));
			}
		}
	}
	// ItemTextFrame (readable plaques and books): events for the page state the packet handler and the
	// ItemText* Lua functions changed; walking away (more than ~10 yards) closes it like the client.
	if (ConnectionManager)
	{
		auto& ItemText = ConnectionManager->PacketHandler.ItemText;
		if (ItemText.bOpen && GetPawn())
			if (const FWowEntity* Source = ConnectionManager->PacketHandler.EntityManager.Find(ItemText.SourceGuid))
				if (FVector::Dist(FWowCoordinate::WowToUE(Source->Movement.Position), GetPawn()->GetActorLocation()) > 1000.f)
					ConnectionManager->PacketHandler.CloseItemText();
		if (ItemText.bBeginPending) { ItemText.bBeginPending = false; FireUIEvent(TEXT("ITEM_TEXT_BEGIN")); }
		if (ItemText.bReadyPending && ItemText.bOpen) { ItemText.bReadyPending = false; FireUIEvent(TEXT("ITEM_TEXT_READY")); }
		if (ItemText.bClosedPending) { ItemText.bClosedPending = false; ItemText.bReadyPending = false; FireUIEvent(TEXT("ITEM_TEXT_CLOSED")); }
	}
	// Objectives tracker input, read from Slate's button state and the tracker's own geometry
	// (Stefan 10.10: the rows only lit up and took clicks on parts of the text, and the collapse
	// button never fired - a FrameXML frame drawn over the tracker took the hit tests there).
	// Hover, left (open in the quest log), Shift-left (untrack), right (map), Ctrl-left
	// (diamond, on release; a Ctrl-drag of 5 px or a frame move moves the tracker instead) and
	// the collapse button all work anywhere on a quest's lines.
	if (FSlateApplication::IsInitialized())
	{
		static bool bTrackerWasLeftDown = false;
		static bool bTrackerWasRightDown = false;
		static bool bTrackerCtrlMoved = false;
		static FVector2D TrackerCtrlPressAt = FVector2D::ZeroVector;
		FSlateApplication& App = FSlateApplication::Get();
		// The physical buttons when they can be read: Slate keeps a button "pressed" when its
		// release happened in another window (a screenshot tool, Alt-Tab while holding it), and
		// the tracker then never lit up or took a click again (Stefan 11.10: "Can't ctrl-click
		// here again"). Clicks while the client is in the background are not ours.
		bool bPhysicalLeft = false, bPhysicalRight = false;
		const bool bTrackerForeground = IsWowClientProcessForeground();
		const bool bLeftDown = bTrackerForeground && (TryGetPhysicalInputDown(EKeys::LeftMouseButton, bPhysicalLeft)
			? bPhysicalLeft : App.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton));
		const bool bRightDown = bTrackerForeground && (TryGetPhysicalInputDown(EKeys::RightMouseButton, bPhysicalRight)
			? bPhysicalRight : App.GetPressedMouseButtons().Contains(EKeys::RightMouseButton));
		const FVector2D Cursor = App.GetCursorPos();
		const FModifierKeysState Mods = App.GetModifierKeys();
		const bool bTrackerShown = QuestTrackerContainerWidget.IsValid() && QuestTrackerContainerWidget->GetVisibility().IsVisible()
			&& !(UnitMenuWidget.IsValid() && UnitMenuWidget->IsOpen()) && !IsNativeModalUiVisible()
			&& !bLeftMouseLooking && !bRightMouseLooking;
		auto TrackerBlockAt = [this, &Cursor, bTrackerShown]() -> uint32
		{
			if (!bTrackerShown || !QuestTrackerWidget.IsValid() || !QuestTrackerWidget->GetVisibility().IsVisible()) return 0;
			// r326 (Stefan 14.10: "it still picks what the mouse is over, not what it points at"; the log
			// had the pointer ~40 px below where he pointed): Slate's own hover (from the real pointer
			// and the drawn widgets) decides first; the rect tests below use the drawn (render) rects.
			for (const TPair<uint32, TWeakPtr<SWidget>>& Block : GWowTrackerBlocks)
				if (const TSharedPtr<SWidget> W = Block.Value.Pin(); W.IsValid() && W->IsHovered())
					return IsFrameXmlOverTracker() ? 0 : Block.Key;
			const FSlateRect Box = QuestTrackerWidget->GetCachedGeometry().GetRenderBoundingRect();
			if (Cursor.X < Box.Left || Cursor.X > Box.Right) return 0;
			// r323 (Stefan 14.10: with the pointer on one quest the one below it lit up): the block the
			// pointer is really inside wins; the 7 px gap above a block only counts when the pointer is
			// in no block at all.
			for (const TPair<uint32, TWeakPtr<SWidget>>& Block : GWowTrackerBlocks)
				if (const TSharedPtr<SWidget> W = Block.Value.Pin())
				{
					const FSlateRect R = W->GetCachedGeometry().GetRenderBoundingRect();
					if (Cursor.Y >= R.Top && Cursor.Y <= R.Bottom) return IsFrameXmlOverTracker() ? 0 : Block.Key;
				}
			for (const TPair<uint32, TWeakPtr<SWidget>>& Block : GWowTrackerBlocks)
				if (const TSharedPtr<SWidget> W = Block.Value.Pin())
				{
					const FGeometry& G = W->GetCachedGeometry();
					const FSlateRect R = G.GetRenderBoundingRect();
					// Up to the previous quest too: the 7 px gap above a block belongs to it.
					if (Cursor.Y >= R.Top - 7.0f * G.Scale && Cursor.Y <= R.Bottom)
					{
						if (!IsFrameXmlOverTracker()) return Block.Key;
						// Which FrameXML frame covers the tracker here (an invisible one would block it).
						static double LastTrackerBlockLog = 0.0;
						if (FPlatformTime::Seconds() - LastTrackerBlockLog > 5.0 && UIManager && UIManager->GetFrameManager())
						{
							LastTrackerBlockLog = FPlatformTime::Seconds();
							TGuardValue<bool> SkipNative(GWowSkipNativeUiCheck, true);
							UE_LOG(LogWowGameplay, Log, TEXT("Tracker hover blocked by FrameXML frame '%s' at (%.0f,%.0f)"),
								*UIManager->GetFrameManager()->GetFrameName(GetUiFrameUnderCursor()), Cursor.X, Cursor.Y);
						}
						return 0;
					}
				}
			return 0;
		};
		auto OverCollapseButton = [this, &Cursor, bTrackerShown]() -> bool
		{
			const TSharedPtr<SWidget> CollapseWidget = GWowTrackerCollapseButton.Pin();
			return bTrackerShown && CollapseWidget.IsValid() && CollapseWidget->GetCachedGeometry().IsUnderLocation(Cursor) && !IsFrameXmlOverTracker();
		};
		// Hover (the title turns white) - only while no button is held.
		if (!bLeftDown && !bRightDown)
		{
			const uint32 NewHover = TrackerBlockAt();
			// r323 diagnostic: which block the pointer was matched to, with the block rects (first 20 changes).
			static int32 HoverLogs = 0;
			if (NewHover != GWowTrackerHoverQuest && NewHover && HoverLogs < 20)
			{
				++HoverLogs;
				FString Rects;
				for (const TPair<uint32, TWeakPtr<SWidget>>& Block : GWowTrackerBlocks)
					if (const TSharedPtr<SWidget> W = Block.Value.Pin())
					{
						const FSlateRect R = W->GetCachedGeometry().GetRenderBoundingRect();
						Rects += FString::Printf(TEXT(" %u:%.0f-%.0f"), Block.Key, R.Top, R.Bottom);
					}
				UE_LOG(LogWowGameplay, Log, TEXT("Tracker hover quest %u at cursor (%.0f,%.0f); blocks%s"), NewHover, Cursor.X, Cursor.Y, *Rects);
			}
			GWowTrackerHoverQuest = NewHover;
		}
		if (bLeftDown && !bTrackerWasLeftDown && !bRightDown)
		{
			GWowPendingCtrlQuest = 0;
			bTrackerCtrlMoved = false;
			TrackerCtrlPressAt = Cursor;
			if (OverCollapseButton())
			{
				if (QuestTrackerWidget.IsValid())
				{
					const bool bWasOpen = QuestTrackerWidget->GetVisibility() != EVisibility::Collapsed;
					QuestTrackerWidget->SetVisibility(bWasOpen ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
					UE_LOG(LogWowGameplay, Log, TEXT("Objectives tracker %s"), bWasOpen ? TEXT("collapsed") : TEXT("expanded"));
				}
			}
			else if (const uint32 Quest = TrackerBlockAt())
			{
				if (Mods.IsControlDown())
				{
					GWowPendingCtrlQuest = Quest;
					UE_LOG(LogWowGameplay, Log, TEXT("Tracker Ctrl press at (%.0f,%.0f): quest %u"), Cursor.X, Cursor.Y, Quest);
				}
				else
				{
					UE_LOG(LogWowGameplay, Log, TEXT("Tracker click: quest %u shift=%d"), Quest, Mods.IsShiftDown() ? 1 : 0);
					OnQuestTrackerClicked(Quest, FPointerEvent(0, 0, Cursor, Cursor, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, Mods));
				}
			}
		}
		else if (bLeftDown && GWowPendingCtrlQuest
			&& ((GameUI && GameUI->IsMovingFrame()) || FVector2D::Distance(Cursor, TrackerCtrlPressAt) >= 5.0))
		{
			bTrackerCtrlMoved = true;
		}
		else if (!bLeftDown && bTrackerWasLeftDown && GWowPendingCtrlQuest)
		{
			const uint32 Quest = GWowPendingCtrlQuest;
			GWowPendingCtrlQuest = 0;
			UE_LOG(LogWowGameplay, Log, TEXT("Tracker Ctrl release: quest %u moved=%d"), Quest, bTrackerCtrlMoved ? 1 : 0);
			if (!bTrackerCtrlMoved && !(GameUI && GameUI->IsMovingFrame()))
			{
				const FPointerEvent CtrlClick(0, 0, Cursor, Cursor, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f,
					FModifierKeysState(false, false, true, false, false, false, false, false, false));
				OnQuestTrackerClicked(Quest, CtrlClick);
			}
		}
		if (bRightDown && !bTrackerWasRightDown && !bLeftDown)
		{
			if (const uint32 Quest = TrackerBlockAt())
			{
				UE_LOG(LogWowGameplay, Log, TEXT("Tracker right-click: quest %u"), Quest);
				OnQuestTrackerClicked(Quest, FPointerEvent(0, 0, Cursor, Cursor, TSet<FKey>(), EKeys::RightMouseButton, 0.0f, Mods));
			}
		}
		bTrackerWasLeftDown = bLeftDown;
		bTrackerWasRightDown = bRightDown;
	}
	const bool bClientIsForeground = IsWowClientProcessForeground();
    // One delayed handoff after loading/Slate teardown, not permanent focus stealing.
    if (WorldEntryFocusFramesRemaining > 0)
    {
        if (!bGameplayHudVisible || bSoftwareCursorSuppressed || IsNativeModalUiVisible() ||
            IsWowUiConsumingKeyboardInput() || bLeftMouseLooking || bRightMouseLooking)
        {
            WorldEntryFocusFramesRemaining = 0;
        }
        else if (bClientIsForeground && FSlateApplication::IsInitialized() && GetWowGameViewportWidget().IsValid())
        {
            if (--WorldEntryFocusFramesRemaining == 0)
            {
                const TSharedPtr<SWidget> PreviousFocus = FSlateApplication::Get().GetKeyboardFocusedWidget();
                ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
                FocusWowGameViewport();
                UE_LOG(LogWowGameplay, Log, TEXT("World-entry input handoff complete: previousFocus=%s viewportFocused=%d"),
                    PreviousFocus.IsValid() ? *PreviousFocus->GetType().ToString() : TEXT("none"),
                    GetWowGameViewportWidget()->HasKeyboardFocus());
            }
        }
    }
	if (!bClientIsForeground &&
		(bLeftMouseLooking || bRightMouseLooking || !bMouseInputNeedsRearmAfterFocusChange))
	{
		ForceReleaseMouseLookForFocusChange();
	}

	bool bSanityLeftMouseDown = false;
	bool bSanityRightMouseDown = false;
	const bool bHasSanityLeftMouse = TryGetPhysicalInputDown(EKeys::LeftMouseButton, bSanityLeftMouseDown);
	const bool bHasSanityRightMouse = TryGetPhysicalInputDown(EKeys::RightMouseButton, bSanityRightMouseDown);
	if (bMouseInputNeedsRearmAfterFocusChange)
	{
		// Windows can reactivate the game while the click that completed a snip,
		// task-bar switch, or Alt-Tab interaction is still held.  Do not let the
		// global physical-button recovery path turn that outside click into a new
		// in-game camera capture.  A fresh press is accepted only after both buttons
		// have first been observed released while the game is foreground.
		// The click that brought the window back may never reach OnLeftClick (Windows activates
		// first). If it is held over a UI frame, route it now so a single click presses the
		// button (its release fires the click) instead of needing a second click.
		if (bClientIsForeground && bHasSanityLeftMouse && bSanityLeftMouseDown && !bLeftMouseWasDown
			&& !(bHasSanityRightMouse && bSanityRightMouseDown) && PressedLeftMouseFrameHandle < 0
			&& !bLeftMouseLooking && !GWowActivationWorldClick && !IsPointerOverNativeGameUi())
		{
			UE_LOG(LogWowGameplay, Log, TEXT("Activation click recovered from the physical button: frame=%lld"),
				GetUiFrameUnderCursor());
			GWowSyntheticActivationPress = true;
			OnLeftClick();
			GWowSyntheticActivationPress = false;
			GWowActivationUiPress = PressedLeftMouseFrameHandle >= 0;
			bLeftMouseWasDown = true;
		}
		bLeftMouseWasDown = bHasSanityLeftMouse && bSanityLeftMouseDown;
		bRightMouseWasDown = bHasSanityRightMouse && bSanityRightMouseDown;
		if (bClientIsForeground && bHasSanityLeftMouse && bHasSanityRightMouse &&
			!bSanityLeftMouseDown && !bSanityRightMouseDown)
		{
			bMouseInputNeedsRearmAfterFocusChange = false;
			bLeftMouseWasDown = false;
			bRightMouseWasDown = false;
		}
	}

	// A recovered press whose release event never arrived: finish it from the physical button,
	// so the button click / world target still happens (a later real release finds nothing to do).
	if ((GWowActivationUiPress || GWowActivationWorldClick) && bClientIsForeground && bHasSanityLeftMouse && !bSanityLeftMouseDown)
	{
		if (GWowActivationWorldClick || PressedLeftMouseFrameHandle >= 0)
		{
			UE_LOG(LogWowGameplay, Log, TEXT("Activation click release completed from the physical button"));
			OnLeftClickReleased();
		}
		GWowActivationUiPress = false;
		GWowActivationWorldClick = false;
	}
	// Restore the normal input mode once the window is back and no button is held. SetInputMode
	// releases Slate's mouse capture, so doing it while the activation click is still down
	// cancelled that click. Also covers a focus loss that never sent a reactivation.
	if (GWowReapplyInputModeAfterActivationClick && bClientIsForeground && bHasSanityLeftMouse && !bSanityLeftMouseDown
		&& !(bHasSanityRightMouse && bSanityRightMouseDown) && !bLeftMouseLooking && !bRightMouseLooking)
	{
		GWowReapplyInputModeAfterActivationClick = false;
		if (IsWowUiConsumingKeyboardInput() || IsNativeModalUiVisible())
		{
			if (UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
			{
				ViewportClient->SetMouseLockMode(EMouseLockMode::DoNotLock);
				ViewportClient->SetMouseCaptureMode(WowMouseCapturePolicy::Resolve(false, false));
			}
		}
		else
		{
			ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
		}
		bShowMouseCursor = !bSoftwareCursorSuppressed || bShowNativeCursorWhileSoftwareSuppressed;
		SetNativePlatformCursorVisible(bShowNativeCursorWhileSoftwareSuppressed);
	}
	if (!bMouseInputNeedsRearmAfterFocusChange)
	{
		UpdateMouseLookReleaseGuards();
	}
	const bool bNativeModalUiVisible = IsNativeModalUiVisible();
    bool bAutomatedSwimmingInput=false;
#if !UE_BUILD_SHIPPING
    bAutomatedSwimmingInput=FParse::Param(FCommandLine::Get(),TEXT("RenderOffscreen")) &&
        FParse::Param(FCommandLine::Get(),TEXT("offlineworld")) && FCString::Strifind(FCommandLine::Get(),TEXT("-swimtest="))!=nullptr;
#endif
    if((!bClientIsForeground && !bAutomatedSwimmingInput) || bNativeModalUiVisible || IsWowUiConsumingKeyboardInput()) FWowKeyBindings::ReleaseAll();
    if(ConnectionManager)FWowKeyBindings::SetCharacter(ConnectionManager->GetBindingCharacterIdentity());
    // r621: no Move and Steer in the isometric camera (middle mouse turns the camera there).
    const bool Steer=FWowKeyBindings::Held(TEXT("MOVEANDSTEER")) && !FWowIsometricCamera::IsActive();
    if(bBindingMoveAndSteer!=Steer)
    {
        bBindingMoveAndSteer=Steer;
        if(Steer)BeginRightMouseLook();else EndRightMouseLook();
    }
    const float PitchInput=float(FWowKeyBindings::Held(TEXT("PITCHUP")))-float(FWowKeyBindings::Held(TEXT("PITCHDOWN")));
    if(!FMath::IsNearlyZero(PitchInput))
    {EnsureGameplayCameraRotationInitialized();GameplayCameraPitchDegrees=FMath::Clamp(GameplayCameraPitchDegrees+PitchInput*60.f*DeltaTime,MinMouseLookPitch,MaxMouseLookPitch);ApplyGameplayCameraRotation();}
    if(auto* BoundPawn=Cast<AWowPlayerCharacter>(GetPawn()))
        BoundPawn->ApplyBoundMovement(FVector2D(FMath::Clamp(float(FWowKeyBindings::Held(TEXT("STRAFERIGHT")))-float(FWowKeyBindings::Held(TEXT("STRAFELEFT")))
            // r651 (Stefan: "use the keybindings for turning, like mine Q and E"): in the isometric camera the turn
            // keys turn the camera (WowIsometricCamera.cpp); r610 had them walk left/right on the screen.
            ,-1.f,1.f),
            float(FWowKeyBindings::Held(TEXT("MOVEFORWARD")) || Steer)-float(FWowKeyBindings::Held(TEXT("MOVEBACKWARD")))),
            // r742 (Stefan 23.10: "you can jump from a sit position, please block that, need to stand up first"): no jump
            // while seated or while the stand-up clip plays.
            FWowKeyBindings::Held(TEXT("JUMP")) && !bLocalSitting && !bLocalStandingUp,FWowKeyBindings::Held(TEXT("SWIMDOWN")) || FWowKeyBindings::Held(TEXT("SITORSTAND")));

	if ((bLeftMouseLooking && bHasSanityLeftMouse && !bSanityLeftMouseDown) ||
		(bRightMouseLooking && !bBindingMoveAndSteer && bHasSanityRightMouse && !bSanityRightMouseDown))
	{
		bMouseChordMovingForward = false;

		if (bLeftMouseLooking)
		{
			EndLeftMouseLook();
		}

		if (bRightMouseLooking)
		{
			EndRightMouseLook();
		}
	}

	if (!bLeftMouseLooking &&
		!bRightMouseLooking &&
		bHasSanityLeftMouse &&
		bHasSanityRightMouse &&
		!bSanityLeftMouseDown &&
		!bSanityRightMouseDown &&
		(bHasSavedLeftMousePosition ||
			bHasSavedRightMousePosition ||
			bMouseChordMovingForward ||
			bIgnoreNextMouseLookDelta))
	{
		bMouseChordMovingForward = false;
		bHasSavedLeftMousePosition = false;
		bHasSavedRightMousePosition = false;
		bIgnoreNextMouseLookDelta = false;
		bHasLastMouseLookPosition = false;
		bHasMouseLookAnchor = false;
		PendingMouseTurn = 0.0f;
		PendingMouseLookUp = 0.0f;
		LeftMouseReleaseMissingFrames = 0;
		RightMouseReleaseMissingFrames = 0;
		PressedLeftMouseFrameHandle = -1;
		PressedRightMouseFrameHandle = -1;
		PendingActionDragSlot = -1;
		PendingUiDragFrame = -1;
		bUiDragActive = false;

		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().ReleaseAllPointerCapture();
		}

		ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
		FocusWowGameViewport();
	}

	if (bSoftwareCursorSuppressed && !bShowNativeCursorWhileSoftwareSuppressed)
	{
		SetNativePlatformCursorVisible(false);
	}

	// Slate can occasionally lose a mouse press/release while switching between
	// GameAndUI and captured GameOnly input. Poll the physical edge as a recovery
	// path, but route it through the complete handlers so world interaction is
	// not silently reduced to camera capture. The handlers update the remembered
	// state, preventing a normally delivered event from being processed twice.
	bool bPhysicalRightMouseDown = false;
	const bool bPhotoModeActive = PhotoMode && PhotoMode->IsActive();
	if (bClientIsForeground && !bMouseInputNeedsRearmAfterFocusChange && !bPhotoModeActive &&
		TryGetPhysicalInputDown(EKeys::RightMouseButton, bPhysicalRightMouseDown))
	{
		if (bPhysicalRightMouseDown && !bRightMouseWasDown)
		{
			OnRightClick();
		}
		else if (!bPhysicalRightMouseDown && bRightMouseWasDown)
		{
			OnRightClickReleased();
		}
	}

    // A press begun on a native NPC window belongs to it until release, even if
    // the cursor leaves the window. The physical-input recovery must not steal it.
    if (bHasSanityLeftMouse && !bSanityLeftMouseDown)
        bNativeUiOwnsLeftMousePress = false;
    else if (bHasSanityLeftMouse && bSanityLeftMouseDown && !bLeftMouseLooking && !bRightMouseLooking
        && IsPointerOverNativeGameUi())
        bNativeUiOwnsLeftMousePress = true;

	bool bPhysicalLeftMouseDown = false;
	if (bClientIsForeground && !bMouseInputNeedsRearmAfterFocusChange && !bPhotoModeActive &&
		!bNativeModalUiVisible && !(GameUI&&GameUI->IsMovingFrame()) && !bNativeUiOwnsLeftMousePress && !IsPointerOverNativeGameUi() &&
		!bRightMouseLooking && !bLeftMouseLooking &&
		PressedLeftMouseFrameHandle < 0 &&
		TryGetPhysicalInputDown(EKeys::LeftMouseButton, bPhysicalLeftMouseDown) &&
		bPhysicalLeftMouseDown)
	{
		bool bCanStartCameraOnlyDrag = GetUiFrameUnderCursor() < 0;
		if (!bCanStartCameraOnlyDrag && UIManager && UIManager->GetFrameManager())
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			bCanStartCameraOnlyDrag = IsPassiveChatCameraSurface(FrameManager, GetUiFrameUnderCursor());
		}
		if (bCanStartCameraOnlyDrag)
		{
			BeginLeftMouseLook();
		}
	}

	const bool bCursorAvailable = !bSoftwareCursorSuppressed || bShowNativeCursorWhileSoftwareSuppressed;
	bShowMouseCursor = bPhotoModeActive ? PhotoMode->WantsCursor()
		: bCursorAvailable && !bRightMouseLooking && !bLeftMouseLooking;
	DefaultMouseCursor = bShowNativeCursorWhileSoftwareSuppressed ? EMouseCursor::Default : EMouseCursor::None;
	CurrentMouseCursor = DefaultMouseCursor;
    const uint64 PreviousHoverGuid = HoveredCursorGuid;
	UpdateSoftwareCursor(DeltaTime);
	UpdateMailboxRange();
	// The world map / minimap arrow reads the local entity's facing; keyboard turning,
	// click-to-move and server facing changes only rotate the pawn, so mirror it every frame.
	if (ConnectionManager && GetPawn())
		if (FWowEntity* LocalEntity = ConnectionManager->PacketHandler.EntityManager.Find(ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid))
			LocalEntity->Movement.Orientation = FWowCoordinate::UEYawToWowOrientation(GetPawn()->GetActorRotation().Yaw);
    TargetHighlightAccumulator += DeltaTime;
    if (TargetHighlightAccumulator >= 0.1f || PreviousHoverGuid != HoveredCursorGuid)
    {
        TargetHighlightAccumulator = 0;
        UpdateTargetHighlight();
    }

	// The FrameXML subsystem may finish constructing after the player controller's
	// BeginPlay.  Keep the reference recoverable so keyboard shortcuts and native
	// verification hooks work in offline sessions as well as after an online login.
	if (!UIManager)
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			UIManager = GameInstance->GetSubsystem<UWowUIManager>();
		}
	}
	BindMinimapUiDelegates();

	if (ConnectionManager && !MinimapWidget.IsValid())
	{
		CreateMinimapWidget();
	}
	EnsureGameplayCameraRotationInitialized();
	ApplyKeyboardTurn(DeltaTime);
	UpdateMinimapWorldCapture();
	// r295 Satellite v2: photograph the tile you stand in for the world map (once per tile).
	if (APawn* SatellitePawn = GetPawn()) FWowSatelliteTiles::Tick(GetWorld(), SatellitePawn->GetActorLocation(), DeltaTime, SatellitePawn);
	UpdateDressUpPreview(); // r300: finishes a try-on waiting for its item/creature query
	UpdateHdUpgrades(); // r331: Experimental NPC/Player HD models, one at a time
	MinimapClockUpdateAccumulator += DeltaTime;
	if (MinimapClockUpdateAccumulator >= 0.5f && UIManager && UIManager->GetFrameManager())
	{
		MinimapClockUpdateAccumulator = 0.0f;
		if (!CachedSkyManager.IsValid() && GetWorld())
		{
			CachedSkyManager = Cast<AWowSkyManager>(
				UGameplayStatics::GetActorOfClass(GetWorld(), AWowSkyManager::StaticClass()));
		}
		if (CachedSkyManager.IsValid())
		{
			CachedSkyManager->SetClockSource(ConnectionManager);
			UIManager->GetFrameManager()->UpdateMinimapTime(CachedSkyManager->GetTimeOfDay());
		}
	}

    // Recover a release swallowed by Slate before considering another drag.
    if (bHasSanityLeftMouse && !bSanityLeftMouseDown && PressedLeftMouseFrameHandle >= 0)
        OnLeftClickReleased();

    // A single threshold owns action, spellbook and inventory drags. Clicks are
    // suppressed after a drag, even when the destination cannot accept it.
    if (PendingUiDragFrame >= 0 && !bUiDragActive && PressedLeftMouseFrameHandle >= 0 &&
        bHasSanityLeftMouse && bSanityLeftMouseDown && UIManager && UIManager->GetFrameManager())
    {
        float MouseX = 0.0f, MouseY = 0.0f;
        if (GetMousePosition(MouseX, MouseY) &&
            FVector2D::DistSquared(ActionDragStartPosition, FVector2D(MouseX, MouseY)) >= 36.0f)
        {
            const int64 Source = PendingUiDragFrame;
            PendingUiDragFrame = -1; // At most one pickup per physical press.
            if (PendingActionDragSlot >= 0 && ConnectionManager)
            {
                ConnectionManager->PickupActionCursor(PendingActionDragSlot);
                bUiDragActive = true;
            }
            else
            {
                FWowFrameManager* DragFrames = UIManager->GetFrameManager();
                const bool bHadPayload = ConnectionManager && ConnectionManager->HasCursorPayload();
                const bool bScript = DragFrames->DispatchDragStart(Source, TEXT("LeftButton"));
                bUiDragActive = bScript;
                // r306 (Stefan 13.10: abilities could not be moved on Bartender bars): an addon action
                // button (type "action", e.g. BT4ButtonN) picks up its action like the client does when
                // its secure OnDragStart did not: the "action" attribute, unless "buttonlock" is set
                // without the PICKUPACTION modifier (Bartender4's own wrapped snippet).
                const FString SourceName = DragFrames->GetFrameName(Source);
                FWowEventSystem* DragEvents = UIManager->GetEventSystem();
                if (!bHadPayload && ConnectionManager && !ConnectionManager->HasCursorPayload() && DragEvents &&
                    !SourceName.IsEmpty() && DragEvents->GetFrameAttribute(Source, TEXT("type")).Equals(TEXT("action"), ESearchCase::IgnoreCase))
                {
                    DragEvents->ExecuteLuaString(FString::Printf(TEXT(
                        "local b=_G[\"%s\"] if b and b.GetAttribute and not GetCursorInfo() then "
                        "local a=tonumber(b:GetAttribute(\"action\")) "
                        "if a and (not b:GetAttribute(\"buttonlock\") or IsModifiedClick(\"PICKUPACTION\")) then PickupAction(a) end end"),
                        *SourceName), TEXT("=ActionButtonDragFallback"));
                    if (ConnectionManager->HasCursorPayload()) bUiDragActive = true;
                }
                UE_LOG(LogTemp, Log, TEXT("UI drag start '%s': script=%d cursor=%d"), *SourceName, bScript ? 1 : 0,
                    ConnectionManager && ConnectionManager->HasCursorPayload() ? 1 : 0);
            }
        }
    }
    if ((CharacterSheetPreview || DressUpPreview) && PressedLeftMouseFrameHandle >= 0 &&
        bHasSanityLeftMouse && bSanityLeftMouseDown && !IsNativeModalUiVisible() &&
        UIManager && UIManager->GetFrameManager())
    {
        auto* Frames = UIManager->GetFrameManager();
        int64 Handle = PressedLeftMouseFrameHandle;
        for (int32 Guard = 0; Handle >= 0 && Guard < 32; ++Guard, Handle = Frames->GetParentHandle(Handle))
        {
            const FString Name = Frames->GetFrameName(Handle);
            const float Direction = Name == TEXT("CharacterModelFrameRotateLeftButton") ? -1.f
                : Name == TEXT("CharacterModelFrameRotateRightButton") ? 1.f : 0.f;
            if (Direction != 0.f)
            {
                if (CharacterSheetPreview && Frames->IsFrameActuallyVisible(Handle)) CharacterSheetPreview->RotateCharacter(Direction * 90.f * DeltaTime);
                break;
            }
            // r270 (Stefan 13.10: the dressing room's turn buttons did nothing): Model_OnUpdate turns
            // the model while a button's state is PUSHED, which this client does not report; turn it
            // natively while the button is held, like the character sheet's.
            const float DressDirection = Name == TEXT("DressUpModelRotateLeftButton") ? -1.f
                : Name == TEXT("DressUpModelRotateRightButton") ? 1.f : 0.f;
            if (DressDirection != 0.f)
            {
                if (Frames->IsFrameActuallyVisible(Handle)) DressUpRotate(DressDirection * 90.f * DeltaTime);
                break;
            }
        }
    }
	if (bDressUpModelDragging)
	{
		float DressMouseX = 0.0f;
		float DressMouseY = 0.0f;
		if (!bHasSanityLeftMouse || !bSanityLeftMouseDown)
		{
			bDressUpModelDragging = false;
		}
		else if (GetMousePosition(DressMouseX, DressMouseY))
		{
			DressUpRotate((LastDressUpDragPosition.X - DressMouseX) * 0.35f); // same feel as the character sheet
			LastDressUpDragPosition = FVector2D(DressMouseX, DressMouseY);
		}
	}
	if (bCharacterSheetModelDragging)
	{
		if (!bHasSanityLeftMouse || !bSanityLeftMouseDown || !CharacterSheetPreview)
		{
			bCharacterSheetModelDragging = false;
		}
		else
		{
			float MouseX = 0.0f;
			float MouseY = 0.0f;
			if (GetMousePosition(MouseX, MouseY))
			{
				const FVector2D MousePosition(MouseX, MouseY);
				CharacterSheetPreview->RotateCharacter(
					(LastCharacterSheetDragPosition.X - MousePosition.X) * 0.35f); // drag right turns the model like WoW
				LastCharacterSheetDragPosition = MousePosition;
			}
		}
	}

	if (bLeftMouseLooking && !bIgnoreNextMouseLookDelta)
	{
		float MouseDeltaX = 0.0f;
		float MouseDeltaY = 0.0f;
		GetInputMouseDelta(MouseDeltaX, MouseDeltaY);
		LeftMouseDragDistance += FMath::Abs(MouseDeltaX) + FMath::Abs(MouseDeltaY);
		bLeftMouseDragged = bLeftMouseDragged || LeftMouseDragDistance >= 4.0f;
	}
	if (bLeftMouseLooking || bRightMouseLooking)
	{
		ApplyMouseLookDelta();
	}
	ApplyGameplayCameraRotation();

	if (!bDebugLeftDragCameraOnlyVerified &&
		FParse::Param(FCommandLine::Get(), TEXT("testleftdragcameraonly")) &&
		GetWorld() &&
		GetWorld()->GetTimeSeconds() >= 1.0f)
	{
		bDebugLeftDragCameraOnlyVerified = true;
		if (APawn* ControlledPawn = GetPawn())
		{
			EnsureGameplayCameraRotationInitialized();
			const float InitialPawnYaw = ControlledPawn->GetActorRotation().Yaw;
			const float InitialCameraYaw = GameplayCameraYawDegrees;

			BeginLeftMouseLook();
			PendingMouseTurn += 240.0f;
			PendingMouseLookUp += 0.0f;
			ApplyMouseLookDelta();
			EndLeftMouseLook();

			const float PawnYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
				InitialPawnYaw,
				ControlledPawn->GetActorRotation().Yaw));
			const float CameraYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
				InitialCameraYaw,
				GameplayCameraYawDegrees));
			const bool bPassed = PawnYawDelta <= 0.5f && CameraYawDelta >= 1.0f;
			UE_LOG(LogWowGameplay, Display,
				TEXT("[LEFTDRAG] %s cameraYawDelta=%.2f pawnYawDelta=%.2f"),
				bPassed ? TEXT("PASS") : TEXT("FAIL"),
				CameraYawDelta,
				PawnYawDelta);
		}
		else
		{
			UE_LOG(LogWowGameplay, Error, TEXT("[LEFTDRAG] FAIL no controlled pawn"));
		}
	}

	// Rendered verification hook: exercise the same submission/formatting path
	// as the Enter key without requiring platform-level input automation.
	if (!bDebugChatMessageSubmitted)
	{
		FString DebugChatMessage;
		if (FParse::Param(FCommandLine::Get(), TEXT("testgamemenu")) && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f)
		{
			ShowGameMenu();
			const bool bOpened = IsGameMenuVisible() && IsNativeModalUiVisible();
			OnLeftClick();
			const bool bCameraStayedReleased = !bLeftMouseLooking && !bRightMouseLooking;

			if (GameMenuResumeButton.IsValid())
			{
				GameMenuResumeButton->SimulateClick();
			}
			const bool bResumeWorked = !IsGameMenuVisible();

			ShowGameMenu();
			if (GameMenuLogoutButton.IsValid())
			{
				GameMenuLogoutButton->SimulateClick();
			}
			const bool bLogoutWorked = !IsGameMenuVisible() &&
				GameMenuCountdownViewportWidget.IsValid() &&
				GameMenuCountdownViewportWidget->GetVisibility().IsVisible() &&
				!bGameMenuCountdownExitsGame;
			CancelGameMenuCountdown();

			ShowGameMenu();
			if (GameMenuExitButton.IsValid())
			{
				GameMenuExitButton->SimulateClick();
			}
			const bool bExitWorked = !IsGameMenuVisible() &&
				GameMenuCountdownViewportWidget.IsValid() &&
				GameMenuCountdownViewportWidget->GetVisibility().IsVisible() &&
				bGameMenuCountdownExitsGame;
			CancelGameMenuCountdown();
			ShowGameMenu();

			const bool bPassed = bOpened && bCameraStayedReleased && bResumeWorked &&
				bLogoutWorked && bExitWorked;
			if (bPassed)
			{
				UE_LOG(LogWowGameplay, Display,
					TEXT("[GAMEMENUTEST] PASS opened=%d cameraReleased=%d resume=%d logout=%d exit=%d"),
					bOpened, bCameraStayedReleased, bResumeWorked, bLogoutWorked, bExitWorked);
			}
			else
			{
				UE_LOG(LogWowGameplay, Error,
					TEXT("[GAMEMENUTEST] FAIL opened=%d cameraReleased=%d resume=%d logout=%d exit=%d"),
					bOpened, bCameraStayedReleased, bResumeWorked, bLogoutWorked, bExitWorked);
			}
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("testquestdialog")) && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && GameUI)
		{
			FWowGossipMenu TestMenu;
			TestMenu.NpcGuid = 0xF130000000001234ULL;
			TestMenu.Greeting = TEXT("The abbey has work for an able adventurer.");
			FWowQuestMenuItem& FirstQuest = TestMenu.Quests.AddDefaulted_GetRef();
			FirstQuest.QuestId = 783;
			FirstQuest.Level = 4;
			FirstQuest.Title = TEXT("A Threat Within");
			FWowQuestMenuItem& SecondQuest = TestMenu.Quests.AddDefaulted_GetRef();
			SecondQuest.QuestId = 7;
			SecondQuest.Level = 2;
			SecondQuest.Title = TEXT("Kobold Camp Cleanup");
			GameUI->ShowQuestList(TestMenu);
			UE_LOG(LogWowGameplay, Log, TEXT("[QUESTDIALOGTEST] PASS: rendered NPC quest list with %d entries"),
				TestMenu.Quests.Num());
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("testgameobjectflame")) && bPlayerEnteredWorld &&
			GetWorld() && GetWorld()->GetTimeSeconds() >= 2.0f && GetPawn() &&
			GetPawn()->GetActorLocation().SizeSquared2D() > FMath::Square(10000.0f) &&
			CachedMpq && CachedAssetCache)
		{
			const FGameObjectDisplayInfoDbcEntry* TorchDisplay = nullptr;
			for (const FGameObjectDisplayInfoDbcEntry& Entry :
				FDbcStore::Get().GameObjectDisplayInfo().GetAll())
			{
				if (Entry.ModelPath.Contains(TEXT("GeneralTorch01"), ESearchCase::IgnoreCase))
				{
					TorchDisplay = &Entry;
					break;
				}
			}

			if (TorchDisplay)
			{
				const FVector PawnLocation = GetPawn()->GetActorLocation();
				const FVector Forward = GetPawn()->GetActorForwardVector().GetSafeNormal2D();
				const FVector Right = GetPawn()->GetActorRightVector().GetSafeNormal2D();
				FVector TestSpawnLocation = PawnLocation + Forward * 500.0f + Right * 170.0f;
				FHitResult GroundHit;
				FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GameObjectFlameTest), false, GetPawn());
				if (GetWorld()->LineTraceSingleByChannel(
					GroundHit,
					TestSpawnLocation + FVector(0.0f, 0.0f, 1500.0f),
					TestSpawnLocation - FVector(0.0f, 0.0f, 3000.0f),
					ECC_Visibility,
					QueryParams))
				{
					TestSpawnLocation.Z = GroundHit.ImpactPoint.Z;
				}

				FWowGameObjectEntity TestGameObject;
				TestGameObject.Guid = 0xF110FFFFFFFF0001ULL;
				TestGameObject.TypeMask = WowTypeMask::OBJECT | WowTypeMask::GAMEOBJECT;
				TestGameObject.Entry = 1;
				TestGameObject.Scale = 1.0f;
				TestGameObject.Movement.Position = FWowCoordinate::UEToWow(TestSpawnLocation);
				TestGameObject.Movement.Orientation = FWowCoordinate::UEYawToWowOrientation(
					GetPawn()->GetActorRotation().Yaw);
				TestGameObject.SetField(GameObjectField::DISPLAY_ID, TorchDisplay->ID);
				SpawnEntityModel(TestGameObject);

				const bool bSpawned = SpawnedEntityActors.Contains(TestGameObject.Guid);
				if (bSpawned)
				{
					UE_LOG(LogWowGameplay, Display,
						TEXT("[GAMEOBJECTVISUALTEST] PASS display=%u path='%s' at %s"),
						TorchDisplay->ID, *TorchDisplay->ModelPath, *TestSpawnLocation.ToCompactString());
				}
				else
				{
					UE_LOG(LogWowGameplay, Error,
						TEXT("[GAMEOBJECTVISUALTEST] FAIL display=%u path='%s' at %s"),
						TorchDisplay->ID, *TorchDisplay->ModelPath, *TestSpawnLocation.ToCompactString());
				}
			}
			else
			{
				UE_LOG(LogWowGameplay, Error,
					TEXT("[GAMEOBJECTVISUALTEST] FAIL: GeneralTorch01 was absent from GameObjectDisplayInfo.dbc"));
			}
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("testquesttracker")) && bPlayerEnteredWorld && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && ConnectionManager && QuestTrackerWidget.IsValid())
		{
			FWowPacketHandler& Handler = ConnectionManager->PacketHandler;
			Handler.QuestLog.Reset();
			auto AddQuest = [&Handler](uint32 Id, const TCHAR* Title, const TCHAR* Objective,
				uint32 CreatureId, uint32 Count, uint32 Required)
			{
				FWowQuestLogEntry& Entry = Handler.QuestLog.AddDefaulted_GetRef();
				Entry.QuestId = Id;
				Entry.Title = Title;
				Entry.ObjectiveText = Objective;
				Entry.ZoneName = TEXT("Undercity");
				Entry.bIsTracked = true;
				if (Required > 0)
				{
					FWowQuestObjective& Obj = Entry.Objectives.AddDefaulted_GetRef();
					Obj.CreatureOrGOId = CreatureId;
					Obj.Count = Count;
					Obj.Required = Required;
				}
			};
			AddQuest(1, TEXT("A Recipe For Death"),
				TEXT("Return to Master Apothecary Faranell at the Apothecarium in Undercity."), 0, 0, 0);
			AddQuest(2, TEXT("The Weaver"), TEXT("0/1 Ataeric's Staff"), 0, 0, 0);
			AddQuest(3, TEXT("Arugal's Folly"), TEXT("0/6 Pyrewood Shackle"), 0, 0, 0);
			UpdateQuestTracker();
			UE_LOG(LogWowGameplay, Log, TEXT("[QUESTTRACKERTEST] PASS: rendered %d tracked objectives"),
				Handler.QuestLog.Num());
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("testquestlog")) && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && ConnectionManager)
		{
			FWowPacketHandler& Handler = ConnectionManager->PacketHandler;
			Handler.QuestLog.Reset();
			auto AddQuest = [&Handler](uint32 Id, const TCHAR* Title, const TCHAR* Objective,
				const TCHAR* ZoneName, uint32 State, bool bTracked)
			{
				FWowQuestLogEntry& Entry = Handler.QuestLog.AddDefaulted_GetRef();
				Entry.QuestId = Id;
				Entry.Title = Title;
				Entry.ObjectiveText = Objective;
				Entry.ZoneName = ZoneName;
				Entry.State = State;
				Entry.bIsTracked = bTracked;
			};
			AddQuest(1, TEXT("A Recipe For Death"),
				TEXT("Return to Master Apothecary Faranell at the Apothecarium in Undercity."),
				TEXT("Undercity"), 0, true);
			AddQuest(2, TEXT("The Weaver"), TEXT("0/1 Ataeric's Staff"), TEXT("Silverpine Forest"), 0, false);
			AddQuest(3, TEXT("Arugal's Folly"), TEXT("0/6 Pyrewood Shackle"), TEXT("Silverpine Forest"), 0, true);
			UpdateQuestTracker();
			if (!QuestLogWidget.IsValid())
			{
				CreateQuestLog();
			}
			if (QuestLogWidget.IsValid() && !QuestLogWidget->IsVisible())
			{
				QuestLogWidget->ToggleVisibility();
			}
			UE_LOG(LogWowGameplay, Log, TEXT("[QUESTLOGTEST] PASS: rendered quest log with %d entries"),
				Handler.QuestLog.Num());
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("testactionbars")) && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && ConnectionManager && UIManager &&
			UIManager->GetFrameManager() &&
			UIManager->GetFrameManager()->FindFrame(TEXT("MultiBarBottomLeftButton1")) >= 0)
		{
			TArray<uint32>& Buttons = ConnectionManager->PacketHandler.ActionButtons;
			Buttons.SetNumZeroed(FMath::Max(Buttons.Num(), 72));
			Buttons[0] = 6603;  // Attack
			Buttons[1] = 133;   // Fireball
			Buttons[24] = 116;  // Frostbolt, right bar
			Buttons[36] = 585;  // Smite, second right bar
			Buttons[48] = 133;  // Bottom-right bar
			Buttons[60] = 116;  // Bottom-left bar
			ConnectionManager->PickupActionCursor(0);
			const bool bMoved = ConnectionManager->PlaceCursorIntoActionSlot(5) &&
				Buttons[0] == 0 && Buttons[5] == 6603;
			OnActionButtonsUpdated();
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			const bool bBarsVisible = FrameManager->IsFrameVisible(FrameManager->FindFrame(TEXT("MultiBarBottomLeft"))) &&
				FrameManager->IsFrameVisible(FrameManager->FindFrame(TEXT("MultiBarBottomRight"))) &&
				FrameManager->IsFrameVisible(FrameManager->FindFrame(TEXT("MultiBarRight"))) &&
				FrameManager->IsFrameVisible(FrameManager->FindFrame(TEXT("MultiBarLeft")));
			if (bMoved && bBarsVisible)
			{
				UE_LOG(LogWowGameplay, Log, TEXT("[ACTIONBARTEST] PASS: all bars visible and action moved between slots"));
			}
			else
			{
				UE_LOG(LogWowGameplay, Error, TEXT("[ACTIONBARTEST] FAIL: moved=%d bars=%d"), bMoved, bBarsVisible);
			}
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("testminimapcontrols")) && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && UIManager && UIManager->GetFrameManager() &&
			MinimapWidget.IsValid() &&
			UIManager->GetFrameManager()->FindFrame(TEXT("MiniMapTrackingButton")) >= 0)
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			const float InitialZoom = MinimapWidget->GetZoomYards();
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("MinimapZoomIn")),
				TEXT("LeftButton"), false);
			const bool bZoomWorked = MinimapWidget->GetZoomYards() < InitialZoom;

			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("MiniMapTrackingButton")),
				TEXT("LeftButton"), false);
			const bool bMenuOpened = FrameManager->IsMinimapTrackingMenuOpen();
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("MinimapNativeTrackingRow10")),
				TEXT("LeftButton"), false);
			const bool bTrackingWorked = !FrameManager->IsMinimapTrackingMenuOpen() &&
				FrameManager->GetMinimapTrackingMode() == 10 && MinimapWidget->GetTrackingMode() == 10;

			FrameManager->UpdateMinimapTime(120.0f);
			const bool bNightWorked = FrameManager->GetMinimapDayNightState() == 1;
			FrameManager->UpdateMinimapTime(720.0f);
			const bool bDayWorked = FrameManager->GetMinimapDayNightState() == 0;

			// Leave the real tracking menu open for the rendered verification image.
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("MiniMapTrackingButton")),
				TEXT("LeftButton"), false);
			const bool bPassed = bZoomWorked && bMenuOpened && bTrackingWorked &&
				bNightWorked && bDayWorked && FrameManager->IsMinimapTrackingMenuOpen();
			if (bPassed)
			{
				UE_LOG(LogWowGameplay, Display,
					TEXT("[MINIMAPTEST] PASS zoom=%d menu=%d tracking=%d night=%d day=%d"),
					bZoomWorked, bMenuOpened, bTrackingWorked, bNightWorked, bDayWorked);
			}
			else
			{
				UE_LOG(LogWowGameplay, Error,
					TEXT("[MINIMAPTEST] FAIL zoom=%d menu=%d tracking=%d night=%d day=%d"),
					bZoomWorked, bMenuOpened, bTrackingWorked, bNightWorked, bDayWorked);
			}
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("testchatresize")) && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && UIManager && UIManager->GetFrameManager() &&
			UIManager->GetFrameManager()->FindFrame(TEXT("ChatFrame1Tab")) >= 0)
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			FrameManager->DispatchMouseDown(FrameManager->FindFrame(TEXT("ChatFrame1Tab")), TEXT("RightButton"));
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("ChatTabContextRow1")), TEXT("LeftButton"), false);
			FrameManager->DispatchMouseDown(FrameManager->FindFrame(TEXT("ChatFrame1Tab")), TEXT("RightButton"));
			const bool bPassed = FrameManager->IsFrameVisible(
				FrameManager->FindFrame(TEXT("ChatFrameNativeResizeButton")));
			if (bPassed)
			{
				UE_LOG(LogWowGameplay, Log, TEXT("[CHATRESIZETEST] PASS"));
			}
			else
			{
				UE_LOG(LogWowGameplay, Error, TEXT("[CHATRESIZETEST] FAIL"));
			}
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("testsocial")) && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && UIManager && UIManager->GetFrameManager() &&
			UIManager->GetFrameManager()->FindFrame(TEXT("FriendsFrame")) >= 0)
		{
			OnSocialKey();
			UIManager->GetFrameManager()->SetSocialContactEntries(
				{TEXT("NorthshireFriend  â€¢  Level 12")}, {TEXT("IgnoredTroublemaker")}, 1);
			OnChatMessage(7, 0, 0, TEXT("NorthshireFriend"),
				TEXT("Are you ready to enter the abbey?"), FString());
			UIManager->GetFrameManager()->DispatchMouseDown(
				UIManager->GetFrameManager()->FindFrame(TEXT("SocialNativeFriendRow1")), TEXT("RightButton"));
			const bool bPassed = UIManager->GetFrameManager()->IsFrameVisible(
				UIManager->GetFrameManager()->FindFrame(TEXT("FriendsFrame")));
			if (bPassed)
			{
				UE_LOG(LogWowGameplay, Log, TEXT("[SOCIALTEST] PASS: O-path opened FriendsFrame and whisper was routed"));
			}
			else
			{
				UE_LOG(LogWowGameplay, Error, TEXT("[SOCIALTEST] FAIL: FriendsFrame remained hidden"));
			}
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("testallbags")) && bPlayerEnteredWorld && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && UIManager && UIManager->GetFrameManager() &&
			UIManager->GetFrameManager()->FindFrame(TEXT("ContainerFrame1")) >= 0)
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			const bool bGenerated = UIManager->GetLuaVM() && UIManager->GetLuaVM()->IsInitialized() &&
				UIManager->GetLuaVM()->ExecuteString(TEXT(R"LUA(
					ContainerFrame_GenerateFrame(ContainerFrame1, 16, 0)
					ContainerFrame_GenerateFrame(ContainerFrame2, 12, 1)
					ContainerFrame_GenerateFrame(ContainerFrame3, 10, 2)
					ContainerFrame_GenerateFrame(ContainerFrame4, 8, 3)
				)LUA"), TEXT("=test_all_bag_stack"));
			FrameManager->SanitizeBackpackOnlyContainers();
			FrameManager->DebugDumpLayout();
			bool bAllVisible = bGenerated;
			bool bAllSizesCorrect = true;
			const float ExpectedHeights[] = {256.0f, 177.0f, 155.0f, 136.0f};
			for (int32 ContainerIndex = 1; ContainerIndex <= 4; ++ContainerIndex)
			{
				const int64 Handle = FrameManager->FindFrame(
					FString::Printf(TEXT("ContainerFrame%d"), ContainerIndex));
				bAllVisible &= Handle >= 0 && FrameManager->IsFrameActuallyVisible(Handle);
				const FWowFrameDef* FrameDef = Handle >= 0 ? FrameManager->GetFrameDef(Handle) : nullptr;
				bAllSizesCorrect &= FrameDef &&
					FMath::IsNearlyEqual(FrameDef->Height, ExpectedHeights[ContainerIndex - 1], 0.5f);
			}
			if (bAllVisible && bAllSizesCorrect)
			{
				UE_LOG(LogWowGameplay, Log,
					TEXT("[ALLBAGTEST] PASS: 16, 12, 10, and 8-slot bags generated with matching frame heights"));
			}
			else
			{
				UE_LOG(LogWowGameplay, Error,
					TEXT("[ALLBAGTEST] FAIL: generated bag visibility or size did not match slot count"));
			}
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("testbags")) && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && UIManager && UIManager->GetFrameManager() &&
			UIManager->GetFrameManager()->FindFrame(TEXT("ContainerFrame1")) >= 0)
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			OnBagKey();
			const int64 BackpackFrame = FrameManager->FindFrame(TEXT("ContainerFrame1"));
			bool bOnlyBackpackVisible = FrameManager->IsFrameVisible(BackpackFrame) &&
				FrameManager->IsFrameActuallyVisible(BackpackFrame);
			for (int32 ContainerIndex = 2; ContainerIndex <= 13; ++ContainerIndex)
			{
				const int64 ContainerFrame = FrameManager->FindFrame(
					FString::Printf(TEXT("ContainerFrame%d"), ContainerIndex));
				bOnlyBackpackVisible &= ContainerFrame < 0 ||
					(!FrameManager->IsFrameVisible(ContainerFrame) &&
					 !FrameManager->IsFrameActuallyVisible(ContainerFrame));
			}
			if (bOnlyBackpackVisible)
			{
				UE_LOG(LogWowGameplay, Log,
					TEXT("[BAGTEST] PASS: B opened one bottom-right backpack and no reusable extra containers"));
			}
			else
			{
				UE_LOG(LogWowGameplay, Error,
					TEXT("[BAGTEST] FAIL: B did not isolate the bottom-right backpack"));
			}
			bDebugChatMessageSubmitted = true;
		}
        else if (IsWowOfflineSession() && FParse::Param(FCommandLine::Get(), TEXT("testunitvisuals")) &&
            GetWorld() && GetWorld()->GetTimeSeconds() >= 12.0f && ConnectionManager && GetPawn())
        {
            const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
            TargetGuid=0; HoveredCursorGuid=LocalGuid; UpdateTargetHighlight();
            const bool bHoverWorks=HoverLitActor.Get()==GetPawn() && !HoverMeshChannels.IsEmpty();
            SetTarget(LocalGuid); UpdateTargetHighlight();
            const bool bNoDuplicate=!HoverHighlightCircle || !HoverHighlightCircle->IsVisible();
            const bool bSelected=TargetHighlightCircle && TargetHighlightCircle->IsVisible();
            SetTarget(0);
            const bool bCleared=TargetHighlightCircle && !TargetHighlightCircle->IsVisible();
            UpdateHoveredModel(0);
            const bool bHoverCleared=!HoverLitActor.IsValid() && HoverMeshChannels.IsEmpty();
            if(!bHoverCleared) UE_LOG(LogWowGameplay,Error,TEXT("[UNITVISUALTEST] Hover state did not clear"));
            SetTarget(LocalGuid);
            FWowFloatingTextInfo Info;
            Info.Text=TEXT("491"); Info.Color=FLinearColor(1.0f,0.82f,0.05f);
            Info.FontSize=40; Info.Duration=30; Info.Speed=0;
            UWowFloatingText::SpawnFloatingText(GetPawn(),Info);
            UE_LOG(LogWowGameplay, Log, TEXT("[UNITVISUALTEST] %s hover=%d selected=%d duplicateHidden=%d clear=%d"),
                bHoverWorks && bNoDuplicate && bSelected && bCleared && bHoverCleared ? TEXT("PASS") : TEXT("FAIL"),
                bHoverWorks,bSelected,bNoDuplicate,bCleared);
            bDebugChatMessageSubmitted=true;
        }
		else if (FParse::Param(FCommandLine::Get(), TEXT("testcharacter")) &&
            ((bPlayerEnteredWorld && bGameplayHudVisible) || IsWowOfflineSession()) &&
            GetWorld() && GetWorld()->GetTimeSeconds() >= 10.0f &&
			UIManager && UIManager->GetFrameManager() &&
			UIManager->GetFrameManager()->FindFrame(TEXT("CharacterFrame")) >= 0 &&
			UIManager->GetFrameManager()->FindFrame(TEXT("CharacterMicroButton")) >= 0)
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			OnCharacterKey();
			const int64 CharacterFrame = FrameManager->FindFrame(TEXT("CharacterFrame"));
			const bool bOpenedByKey = FrameManager->IsFrameVisible(CharacterFrame);

			// Exercise all three pages through the same release path as a pointer
			// click, then return to Character for the rendered screenshot.
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("CharacterFrameTab2")),
				TEXT("LeftButton"), false);
			const bool bReputationSelected =
				FrameManager->IsFrameVisible(FrameManager->FindFrame(TEXT("ReputationFrame"))) &&
				!FrameManager->IsFrameVisible(FrameManager->FindFrame(TEXT("PaperDollFrame")));
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("CharacterFrameTab3")),
				TEXT("LeftButton"), false);
			const bool bSkillsSelected =
				FrameManager->IsFrameVisible(FrameManager->FindFrame(TEXT("SkillFrame"))) &&
				!FrameManager->IsFrameVisible(FrameManager->FindFrame(TEXT("ReputationFrame")));
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("CharacterFrameTab1")),
				TEXT("LeftButton"), false);
			const bool bCharacterSelected =
				FrameManager->IsFrameVisible(FrameManager->FindFrame(TEXT("PaperDollFrame"))) &&
				!FrameManager->IsFrameVisible(FrameManager->FindFrame(TEXT("SkillFrame")));

			// The Character micro-menu release path calls this same direct toggle.
			// Close, then reopen so both states are checked and the sheet remains
			// visible for the rendered screenshot.
			OnCharacterKey();
			const bool bClosedByButton = !FrameManager->IsFrameVisible(CharacterFrame);
			OnCharacterKey();
			const bool bReopenedByButton = FrameManager->IsFrameVisible(CharacterFrame);

			if (bOpenedByKey && bReputationSelected && bSkillsSelected && bCharacterSelected &&
				bClosedByButton && bReopenedByButton)
			{
				UE_LOG(LogWowGameplay, Log,
					TEXT("[CHARACTERTEST] PASS: C/micro-menu toggle and Character/Reputation/Skills tabs work"));
			}
			else
			{
				UE_LOG(LogWowGameplay, Error,
					TEXT("[CHARACTERTEST] FAIL: key=%d rep=%d skills=%d character=%d close=%d reopen=%d"),
					bOpenedByKey, bReputationSelected, bSkillsSelected, bCharacterSelected,
					bClosedByButton, bReopenedByButton);
			}
			const FString ScreenshotDir = FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("Screenshots"));
			FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*ScreenshotDir);
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(
				ScreenshotDir, TEXT("CharacterSheetTabsVerified.png")), true, false);
            if (IsWowOfflineSession() && FParse::Param(FCommandLine::Get(), TEXT("testcombattext")))
            {
                FWowFloatingTextInfo Info;
                Info.Text = TEXT("71!"); Info.Color = FLinearColor::Yellow;
                Info.Duration = 30.0f; Info.Speed = 0.0f; Info.FontSize = 40.0f;
                UWowFloatingText* Text = UWowFloatingText::SpawnFloatingText(GetPawn(), Info);
                UE_LOG(LogWowGameplay, Log, TEXT("[COMBATTEXTTEST] root=%d location=%s"),
                    Text && Text->GetOwner()->GetRootComponent() == Text,
                    Text ? *Text->GetComponentLocation().ToString() : TEXT("missing"));
            }
            bDebugChatMessageSubmitted = true;
        }
        else if (FParse::Param(FCommandLine::Get(), TEXT("testtargetclear")) && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && ConnectionManager && UIManager &&
			UIManager->GetFrameManager() &&
			ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid != 0 &&
			UIManager->GetFrameManager()->FindFrame(TEXT("TargetFrame")) >= 0)
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			const int64 TargetFrameHandle = FrameManager->FindFrame(TEXT("TargetFrame"));
			SetTarget(ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid);
			const bool bShown = FrameManager->IsFrameVisible(TargetFrameHandle) &&
				FrameManager->IsFrameActuallyVisible(TargetFrameHandle);
			SetTarget(0);
			bool bAllTargetUiHidden = !FrameManager->IsFrameVisible(TargetFrameHandle) &&
				!FrameManager->IsFrameActuallyVisible(TargetFrameHandle);
			for (const TCHAR* FrameName : {TEXT("TargetFrameToT"), TEXT("TargetFrameSpellBar"),
				TEXT("TargetFrameBuffs"), TEXT("TargetFrameDebuffs"),
				TEXT("TargetFrameNumericalThreat"), TEXT("TargetFrameDropDown")})
			{
				const int64 Handle = FrameManager->FindFrame(FrameName);
				bAllTargetUiHidden &= Handle < 0 || (!FrameManager->IsFrameVisible(Handle) &&
					!FrameManager->IsFrameActuallyVisible(Handle));
			}
			if (bShown && bAllTargetUiHidden)
			{
				UE_LOG(LogWowGameplay, Log,
					TEXT("[TARGETCLEARTEST] PASS: target and all conditional target UI cleared"));
			}
			else
			{
				UE_LOG(LogWowGameplay, Error,
					TEXT("[TARGETCLEARTEST] FAIL: shown=%d hidden=%d"), bShown, bAllTargetUiHidden);
			}
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("testunitframes")) &&
			bPlayerEnteredWorld && bGameplayHudVisible && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && UIManager &&
			UIManager->GetFrameManager() &&
			UIManager->GetFrameManager()->FindFrame(TEXT("TargetFrame")) >= 0 &&
			UIManager->GetFrameManager()->FindFrame(TEXT("FocusFrame")) >= 0)
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			UWowConnectionManager* UnitFrameConnection = UIManager->GetConnectionManager();
			if (UnitFrameConnection &&
				(!ConnectionManager || ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid == 0))
			{
				ConnectionManager = UnitFrameConnection;
			}
			uint64 LocalGuid = UnitFrameConnection
				? UnitFrameConnection->PacketHandler.EntityManager.LocalPlayerGuid : 0;
			if (UnitFrameConnection && LocalGuid == 0)
			{
				FWowEntityManager& EntityManager = UnitFrameConnection->PacketHandler.EntityManager;
				LocalGuid = 1;
				EntityManager.LocalPlayerGuid = LocalGuid;
				FWowEntity& BaseEntity = EntityManager.PromoteToTyped(
					LocalGuid, WowTypeMask::OBJECT | WowTypeMask::UNIT | WowTypeMask::PLAYER);
				BaseEntity.SetField(ObjectField::GUID, static_cast<uint32>(LocalGuid));
				BaseEntity.SetField(ObjectField::GUID + 1, 0);
				BaseEntity.SetField(ObjectField::TYPE,
					WowTypeMask::OBJECT | WowTypeMask::UNIT | WowTypeMask::PLAYER);
				BaseEntity.SetField(UnitField::BYTES_0, 1u | (1u << 8));
				BaseEntity.SetField(UnitField::HEALTH, 100);
				BaseEntity.SetField(UnitField::MAXHEALTH, 100);
				BaseEntity.SetField(UnitField::POWER1, 100);
				BaseEntity.SetField(UnitField::MAXPOWER1, 100);
				BaseEntity.SetField(UnitField::LEVEL, 1);
				UnitFrameConnection->PacketHandler.PlayerNameCache.Add(
					LocalGuid, TEXT("Offline Adventurer"));
			}
			if (LocalGuid != 0 && UIManager->GetLuaVM())
			{
				FWowEntityManager& EntityManager = UnitFrameConnection->PacketHandler.EntityManager;
				const uint64 TestTargetGuid = LocalGuid + 1;
				FWowEntity& TestTarget = EntityManager.PromoteToTyped(
					TestTargetGuid, WowTypeMask::OBJECT | WowTypeMask::UNIT | WowTypeMask::PLAYER);
				TestTarget.SetField(ObjectField::GUID, static_cast<uint32>(TestTargetGuid));
				TestTarget.SetField(ObjectField::GUID + 1, static_cast<uint32>(TestTargetGuid >> 32));
				TestTarget.SetField(ObjectField::TYPE,
					WowTypeMask::OBJECT | WowTypeMask::UNIT | WowTypeMask::PLAYER);
				TestTarget.SetField(UnitField::BYTES_0, 2u | (1u << 8));
				TestTarget.SetField(UnitField::HEALTH, 75);
				TestTarget.SetField(UnitField::MAXHEALTH, 100);
				TestTarget.SetField(UnitField::POWER1, 80);
				TestTarget.SetField(UnitField::MAXPOWER1, 100);
				TestTarget.SetField(UnitField::LEVEL, 10);
				TestTarget.SetField(UnitField::TARGET, static_cast<uint32>(LocalGuid));
				TestTarget.SetField(UnitField::TARGET + 1, static_cast<uint32>(LocalGuid >> 32));
				UnitFrameConnection->PacketHandler.PlayerNameCache.Add(
					TestTargetGuid, TEXT("Test Target"));
				if (FWowEntity* TestPlayerEntity = EntityManager.Find(LocalGuid))
				{
					TestPlayerEntity->SetField(UnitField::HEALTH, 87);
					TestPlayerEntity->SetField(UnitField::MAXHEALTH, 100);
					TestPlayerEntity->SetField(UnitField::POWER1, 42);
					TestPlayerEntity->SetField(UnitField::MAXPOWER1, 100);
				}
				SetTarget(TestTargetGuid);
				UIManager->GetLuaVM()->ExecuteString(TEXT("FocusUnit('target')"), TEXT("=testunitframes"));
				FireUIEvent(TEXT("UNIT_TARGET"), {TEXT("player")});
				FireUIEvent(TEXT("UNIT_TARGET"), {TEXT("target")});
				FireUIEvent(TEXT("UNIT_TARGET"), {TEXT("focus")});
				UIManager->GetLuaVM()->ExecuteString(TEXT(
					"if PlayerName then PlayerName:SetText(UnitName('player') or 'Player') end "
					"local portraits = {{PlayerPortrait,'player'},{TargetFramePortrait,'target'},"
					"{TargetFrameToTPortrait,'targettarget'},{FocusFramePortrait,'focus'},"
					"{FocusFrameToTPortrait,'focustarget'}} "
					"for _,p in ipairs(portraits) do if p[1] then SetPortraitTexture(p[1],p[2]) end end"),
					TEXT("=testunitframeportraits"));
				const bool bPreferencesPassed = UIManager->GetLuaVM()->ExecuteString(TEXT(R"LUA(
                    assert(UnitIsUnit("focus-target", "focustarget"), "Focus target aliases differ")
                    local function updateTargets()
                        TargetofTarget_Update(TargetFrameToT, 0)
                        TargetofTarget_Update(FocusFrameToT, 0)
                    end
                    SetCVar("showTargetOfTarget", "0", "SHOW_TARGET_OF_TARGET_TEXT")
                    updateTargets()
                    assert(not TargetFrameToT:IsShown() and not FocusFrameToT:IsShown(), "ToT disable failed")
                    SetCVar("targetOfTargetMode", "5")
                    SetCVar("showTargetOfTarget", "1", "SHOW_TARGET_OF_TARGET_TEXT")
                    updateTargets()
                    assert(TargetFrameToT:IsShown() and FocusFrameToT:IsShown(), "ToT enable failed")
                    SetCVar("playerStatusText", "0", "STATUS_TEXT_PLAYER")
                    TextStatusBar_UpdateTextString(PlayerFrameHealthBar)
                    TextStatusBar_UpdateTextString(PlayerFrameManaBar)
                    assert(not PlayerFrameHealthBarText:IsShown(), "Player status hide failed")
                    SetCVar("playerStatusText", "1", "STATUS_TEXT_PLAYER")
                    UnitFrame_Update(PlayerFrame)
                    TextStatusBar_UpdateTextString(PlayerFrameHealthBar)
                    TextStatusBar_UpdateTextString(PlayerFrameManaBar)
                    assert(PlayerFrameHealthBarText:IsShown() and PlayerFrameHealthBarText:GetText() ~= "", "Health text missing")
                    assert(PlayerFrameManaBarText:IsShown() and PlayerFrameManaBarText:GetText() ~= "", "Power text missing")
                    assert(PlayerFrameHealthBarText:GetText() == "87 / 100", "Health value did not update")
                    assert(PlayerFrameManaBarText:GetText() == "42 / 100", "Mana value did not update")
                )LUA"), TEXT("=unitframe_preferences_test"));
				UE_LOG(LogWowGameplay, Warning, TEXT("[UNITFRAMEPREFTEST] %s"),
					bPreferencesPassed ? TEXT("PASS") : TEXT("FAIL"));
				UIManager->BeginUnitSpellProgress(
					TestTargetGuid, 635, TEXT("Holy Light"), 600.0f, false);
				FireUnitSpellcastEventForGuid(TEXT("UNIT_SPELLCAST_START"), TestTargetGuid);
				bool bSparkPassed = true;
				for (const TCHAR* CastFrame : {TEXT("TargetFrameSpellBar"), TEXT("FocusFrameSpellBar")})
				{
					const int64 CastHandle = FrameManager->FindFrame(CastFrame);
					float PreviousX = TNumericLimits<float>::Max();
					for (const float Fraction : {0.75f, 0.50f, 0.25f})
					{
						const FString Script = FString::Printf(TEXT(
							"local b=%s; b:SetMinMaxValues(0,100); b.maxValue=100; b.value=%.0f; "
							"b.casting=nil; b.channeling=1; CastingBarFrame_OnUpdate(b,25); "
							"_G[b:GetName()..'Spark']:SetPoint('CENTER',b,'LEFT',b:GetValue(),2)"),
							CastFrame, Fraction * 100.0f + 25.0f);
						bSparkPassed &= UIManager->GetLuaVM()->ExecuteString(Script, TEXT("=spark_drain_test"));
						UImage* Spark = FrameManager->GetNamedTexture(FString(CastFrame) + TEXT("Spark"));
						UProgressBar* Fill = FrameManager->GetStatusBarWidget(CastHandle);
						const UCanvasPanelSlot* SparkSlot = Spark ? Cast<UCanvasPanelSlot>(Spark->Slot) : nullptr;
						const UCanvasPanelSlot* FillSlot = Fill ? Cast<UCanvasPanelSlot>(Fill->Slot) : nullptr;
						if (SparkSlot && FillSlot)
						{
							const float X = SparkSlot->GetPosition().X + SparkSlot->GetSize().X * 0.5f;
							const float ExpectedX = FillSlot->GetPosition().X + FillSlot->GetSize().X * Fraction;
							bSparkPassed &= FMath::IsNearlyEqual(X, ExpectedX, 0.5f) && X < PreviousX;
							PreviousX = X;
						}
						else bSparkPassed = false;
					}
				}
				UE_LOG(LogWowGameplay, Warning, TEXT("[SPARKDRAINTEST] %s: 75 -> 50 -> 25 percent"),
					bSparkPassed ? TEXT("PASS") : TEXT("FAIL"));
				// Leave a real channel draining from the middle for the screenshot.
				UIManager->BeginUnitSpellProgress(TestTargetGuid, 635, TEXT("Channel test"), 20.0f, true);
				FireUnitSpellcastEventForGuid(TEXT("UNIT_SPELLCAST_CHANNEL_START"), TestTargetGuid);
				UIManager->GetLuaVM()->ExecuteString(TEXT(
					"CastingBarFrame_OnUpdate(TargetFrameSpellBar,7); CastingBarFrame_OnUpdate(FocusFrameSpellBar,7)"),
					TEXT("=spark_channel_preview"));
			}
			else
			{
				for (const TCHAR* FrameName : {
					TEXT("TargetFrame"), TEXT("TargetFrameToT"),
					TEXT("FocusFrame"), TEXT("FocusFrameToT")})
				{
					FrameManager->SetFrameVisible(FrameManager->FindFrame(FrameName), true);
				}
				for (const TPair<const TCHAR*, const TCHAR*>& Label : {
					TPair<const TCHAR*, const TCHAR*>(TEXT("TargetFrameTextureFrameName"), TEXT("Target")),
					TPair<const TCHAR*, const TCHAR*>(TEXT("TargetFrameTextureFrameLevelText"), TEXT("1")),
					TPair<const TCHAR*, const TCHAR*>(TEXT("FocusFrameTextureFrameName"), TEXT("Focus")),
					TPair<const TCHAR*, const TCHAR*>(TEXT("FocusFrameTextureFrameLevelText"), TEXT("1")),
					TPair<const TCHAR*, const TCHAR*>(TEXT("TargetFrameToTTextureFrameName"), TEXT("Player")),
					TPair<const TCHAR*, const TCHAR*>(TEXT("FocusFrameToTTextureFrameName"), TEXT("Player"))})
				{
					if (UTextBlock* Text = FrameManager->GetNamedFontString(Label.Key))
					{
						Text->SetText(FText::FromString(Label.Value));
						Text->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
					}
				}
				for (const TCHAR* BarName : {
					TEXT("TargetFrameHealthBar"), TEXT("TargetFrameManaBar"),
					TEXT("TargetFrameToTHealthBar"), TEXT("TargetFrameToTManaBar"),
					TEXT("FocusFrameHealthBar"), TEXT("FocusFrameManaBar"),
					TEXT("FocusFrameToTHealthBar"), TEXT("FocusFrameToTManaBar")})
				{
					if (UProgressBar* Bar = FrameManager->GetStatusBarWidget(FrameManager->FindFrame(BarName)))
					{
						Bar->SetPercent(0.75f);
					}
				}
			}
			const auto IsShown = [FrameManager](const TCHAR* FrameName)
			{
				const int64 Handle = FrameManager->FindFrame(FrameName);
				return Handle >= 0 && FrameManager->IsFrameVisible(Handle) &&
					FrameManager->IsFrameActuallyVisible(Handle);
			};
			const auto IsTextureShown = [FrameManager](const TCHAR* TextureName)
			{
				const UImage* Texture = FrameManager->GetNamedTexture(TextureName);
				return Texture && Texture->GetRenderOpacity() > 0.0f &&
					Texture->GetVisibility() != ESlateVisibility::Collapsed;
			};
			const auto IsPortraitShown = [FrameManager](const TCHAR* TextureName)
			{
				const UImage* Portrait = FrameManager->GetNamedTexture(TextureName);
				return Portrait && Portrait->GetBrush().GetResourceObject() &&
					Portrait->GetRenderOpacity() > 0.0f &&
					Portrait->GetVisibility() != ESlateVisibility::Collapsed;
			};
			const bool bPortraitsVisible =
				IsPortraitShown(TEXT("PlayerPortrait")) &&
				IsPortraitShown(TEXT("TargetFramePortrait")) &&
				IsPortraitShown(TEXT("TargetFrameToTPortrait")) &&
				IsPortraitShown(TEXT("FocusFramePortrait")) &&
				IsPortraitShown(TEXT("FocusFrameToTPortrait"));
			const bool bTargetOfTargetPlatesVisible =
				IsTextureShown(TEXT("TargetFrameToTTextureFrameTexture")) &&
				IsTextureShown(TEXT("FocusFrameToTTextureFrameTexture"));
			const bool bUnitCastBarsVisible =
				IsShown(TEXT("TargetFrameSpellBar")) && IsShown(TEXT("FocusFrameSpellBar"));
			const bool bPassed = IsShown(TEXT("PlayerFrame")) &&
				IsShown(TEXT("TargetFrame")) && IsShown(TEXT("TargetFrameToT")) &&
				IsShown(TEXT("FocusFrame")) && IsShown(TEXT("FocusFrameToT")) &&
				IsTextureShown(TEXT("TargetFrameTextureFrameTexture")) &&
				IsTextureShown(TEXT("FocusFrameTextureFrameTexture")) &&
				bPortraitsVisible && bTargetOfTargetPlatesVisible && bUnitCastBarsVisible;
			FrameManager->DebugDumpLayout();
			if (bPassed)
			{
				UE_LOG(LogWowGameplay, Log,
					TEXT("[UNITFRAMETEST] PASS: player, target, target-of-target, focus, focus-target, and unit cast bars"));
			}
			else
			{
				UE_LOG(LogWowGameplay, Error,
					TEXT("[UNITFRAMETEST] FAIL: player=%d target=%d targettarget=%d focus=%d focustarget=%d targetplate=%d focusplate=%d portraits=%d targettargetplates=%d castbars=%d"),
					IsShown(TEXT("PlayerFrame")), IsShown(TEXT("TargetFrame")),
					IsShown(TEXT("TargetFrameToT")), IsShown(TEXT("FocusFrame")),
					IsShown(TEXT("FocusFrameToT")),
					IsTextureShown(TEXT("TargetFrameTextureFrameTexture")),
					IsTextureShown(TEXT("FocusFrameTextureFrameTexture")),
					bPortraitsVisible, bTargetOfTargetPlatesVisible, bUnitCastBarsVisible);
			}
			const FString ScreenshotDir = FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("Screenshots"));
			FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*ScreenshotDir);
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(
				ScreenshotDir, TEXT("UnitFramesAllVerified.png")), true, false);
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("testcombatlogfilters")) && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && UIManager && UIManager->GetFrameManager() &&
			UIManager->GetFrameManager()->FindFrame(TEXT("CombatLogFilterDropDownButton")) >= 0)
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("ChatFrame2Tab")), TEXT("LeftButton"), false);
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("CombatLogFilterDropDownButton")), TEXT("LeftButton"), false);
			const bool bPassed = FrameManager->IsFrameVisible(FrameManager->FindFrame(TEXT("ChatFrame2"))) &&
				FrameManager->FindFrame(TEXT("CombatLogFilterKills")) >= 0;
			if (bPassed)
			{
				UE_LOG(LogWowGameplay, Log, TEXT("[COMBATLOGFILTERTEST] PASS"));
			}
			else
			{
				UE_LOG(LogWowGameplay, Error, TEXT("[COMBATLOGFILTERTEST] FAIL"));
			}
			bDebugChatMessageSubmitted = true;
		}
		else if ((FParse::Param(FCommandLine::Get(), TEXT("testchatoverflow")) ||
			FParse::Param(FCommandLine::Get(), TEXT("testchatten"))) && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && UIManager && UIManager->GetFrameManager() &&
			UIManager->GetFrameManager()->FindFrame(TEXT("ChatFrame1Tab")) >= 0)
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			FrameManager->ConfigureChatTab(3, TEXT("Party"));
			FrameManager->ConfigureChatTab(4, TEXT("test1"));
			FrameManager->ConfigureChatTab(5, TEXT("test2"));
			FrameManager->ConfigureChatTab(6, TEXT("test3"));
			FrameManager->ConfigureChatTab(7, TEXT("test4"));
			if (FParse::Param(FCommandLine::Get(), TEXT("testchatten")))
			{
				FrameManager->ConfigureChatTab(8, TEXT("test5"));
				FrameManager->ConfigureChatTab(9, TEXT("test6"));
				FrameManager->ConfigureChatTab(10, TEXT("test7"));
			}
			FrameManager->DispatchClick(FrameManager->FindFrame(
				TEXT("ChatTabOverflowNativeButton")), TEXT("LeftButton"), false);
			UE_LOG(LogWowGameplay, Log, TEXT("[CHATTABTEST] configured %s responsive chat tabs"),
				FParse::Param(FCommandLine::Get(), TEXT("testchatten")) ? TEXT("ten") : TEXT("seven"));
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Value(FCommandLine::Get(), TEXT("-testchat="), DebugChatMessage) &&
			!DebugChatMessage.IsEmpty() && ConnectionManager &&
			ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid != 0 && UIManager &&
			UIManager->GetFrameManager() &&
			UIManager->GetFrameManager()->FindFrame(TEXT("ChatFrame1")) >= 0)
		{
			SubmitFrameXmlChat(DebugChatMessage);
			bDebugChatMessageSubmitted = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("testchatfunctional")) && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && UIManager && UIManager->GetFrameManager() &&
			UIManager->GetFrameManager()->FindFrame(TEXT("ChatFrame1")) >= 0)
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			const int64 GeneralHandle = FrameManager->FindFrame(TEXT("ChatFrame1"));
			const int64 CombatHandle = FrameManager->FindFrame(TEXT("ChatFrame2"));
			for (int32 Index = 1; Index <= 10; ++Index)
			{
				FrameManager->AddScrollingMessage(GeneralHandle,
					FString::Printf(TEXT("Offline Adventurer: chat history line %d"), Index), FLinearColor::White);
			}
			for (int32 Index = 1; Index <= 9; ++Index)
			{
				FrameManager->AddScrollingMessage(CombatHandle,
					FString::Printf(TEXT("Combat test event %d"), Index), FLinearColor(1.0f, 0.65f, 0.25f));
			}
			FrameManager->AddScrollingMessage(GeneralHandle,
				TEXT("|cffffd200Server MOTD:|r Welcome to |cff35ff62Northshire|r."),
				FLinearColor::White);
			FrameManager->AddScrollingMessage(GeneralHandle,
				TEXT("|cffffd200Wrapped chat verification:|r this deliberately long message must stay inside the chat frame without overlapping adjacent messages.\n|cff35ff62The authored second line must also keep its own vertical space.|r"),
				FLinearColor::White);

			bool bPassed = FrameManager->GetScrollingMessageCount(GeneralHandle) >= 11 &&
				FrameManager->GetScrollingMessageCount(CombatHandle) >= 9;
			FrameManager->ScrollMessagesToBottom(GeneralHandle);
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("ChatFrame1ButtonFrameUpButton")), TEXT("LeftButton"), false);
			bPassed &= !FrameManager->AreMessagesAtBottom(GeneralHandle);
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("ChatFrame1ButtonFrameDownButton")), TEXT("LeftButton"), false);
			bPassed &= FrameManager->AreMessagesAtBottom(GeneralHandle);
			FrameManager->ScrollMessagesToTop(GeneralHandle);
			bPassed &= FrameManager->AreMessagesAtTop(GeneralHandle);
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("ChatFrame1ButtonFrameBottomButton")), TEXT("LeftButton"), false);
			bPassed &= FrameManager->AreMessagesAtBottom(GeneralHandle);

			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("ChatFrame2Tab")), TEXT("LeftButton"), false);
			bPassed &= FrameManager->IsFrameVisible(CombatHandle) && !FrameManager->IsFrameVisible(GeneralHandle);
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("ChatFrame1Tab")), TEXT("LeftButton"), false);
			bPassed &= FrameManager->IsFrameVisible(GeneralHandle) && !FrameManager->IsFrameVisible(CombatHandle);

			auto VerifyToggle = [FrameManager](const TCHAR* ButtonName, const TCHAR* FrameName)
			{
				const int64 ButtonHandle = FrameManager->FindFrame(ButtonName);
				const int64 TargetHandle = FrameManager->FindFrame(FrameName);
				if (ButtonHandle < 0 || TargetHandle < 0) return false;
				const bool bInitiallyVisible = FrameManager->IsFrameVisible(TargetHandle);
				FrameManager->DispatchClick(ButtonHandle, TEXT("LeftButton"), false);
				const bool bToggled = FrameManager->IsFrameVisible(TargetHandle) != bInitiallyVisible;
				FrameManager->DispatchClick(ButtonHandle, TEXT("LeftButton"), false);
				return bToggled && FrameManager->IsFrameVisible(TargetHandle) == bInitiallyVisible;
			};
			bPassed &= VerifyToggle(TEXT("FriendsMicroButton"), TEXT("FriendsFrame"));
			bPassed &= VerifyToggle(TEXT("ChatFrameMenuButton"), TEXT("ChatMenu"));

			// Verify the tab popup's native row path, then leave it open for the
			// rendered screenshot so geometry, text alignment and hit priority are visible.
			FrameManager->DispatchMouseDown(FrameManager->FindFrame(TEXT("ChatFrame1Tab")), TEXT("RightButton"));
			FrameManager->DispatchClick(FrameManager->FindFrame(TEXT("ChatTabContextRow1")), TEXT("LeftButton"), false);
			bPassed &= FrameManager->IsFrameVisible(FrameManager->FindFrame(TEXT("ChatFrameNativeResizeButton")));
			FrameManager->DispatchMouseDown(FrameManager->FindFrame(TEXT("ChatFrame1Tab")), TEXT("RightButton"));

			FrameManager->AddScrollingMessage(GeneralHandle,
				bPassed ? TEXT("Chat tabs, history and side controls verified offline.")
					: TEXT("Chat functional verification failed; inspect the log."),
				bPassed ? FLinearColor(0.35f, 1.0f, 0.35f) : FLinearColor::Red);
			FrameManager->FocusEditBox(FrameManager->FindFrame(TEXT("ChatFrame1EditBox")), FString(), false);
			if (bPassed)
			{
				UE_LOG(LogWowGameplay, Log, TEXT("[CHATTEST] PASS"));
			}
			else
			{
				UE_LOG(LogWowGameplay, Error, TEXT("[CHATTEST] FAIL"));
			}
			bDebugChatMessageSubmitted = true;
		}
		else if ((FParse::Param(FCommandLine::Get(), TEXT("testchatinput")) ||
			FParse::Param(FCommandLine::Get(), TEXT("testchathover"))) && GetWorld() &&
			GetWorld()->GetTimeSeconds() >= 1.0f && UIManager &&
			UIManager->GetFrameManager() &&
			UIManager->GetFrameManager()->FindFrame(TEXT("ChatFrame1EditBox")) >= 0)
		{
			// Opt-in rendered verification only. Show the real input widget without
			// changing keyboard focus from inside actor Tick; Slate focus changes are
			// intentionally reserved for the normal Enter-key input event path.
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			FrameManager->FocusEditBox(FrameManager->FindFrame(TEXT("ChatFrame1EditBox")), FString(), false);
			if (FParse::Param(FCommandLine::Get(), TEXT("testchathover")))
			{
				// 1920x1080 verification viewport: center of Combat Log tab.
				SetMouseLocation(210, 760);
			}
			bDebugChatMessageSubmitted = true;
		}
	}

	// Remote players whose combat window ran out put their weapons back (r244).
	if (GRemoteCombatDrawnUntil.Num() > 0 && ConnectionManager)
	{
		const double Now = FPlatformTime::Seconds();
		TArray<uint64, TInlineAllocator<4>> Expired;
		for (const TPair<uint64, double>& Drawn : GRemoteCombatDrawnUntil)
			if (Now >= Drawn.Value) Expired.Add(Drawn.Key);
		for (const uint64 Guid : Expired)
		{
			GRemoteCombatDrawnUntil.Remove(Guid);
			if (const FWowEntity* DrawnPlayer = ConnectionManager->PacketHandler.EntityManager.Find(Guid))
				OnEntityUpdated(*DrawnPlayer);
		}
	}

	// AddMovementInput is consumed once per frame, so the character stops
	// immediately when either mouse button releases without a separate braking
	// or key-up command.
	// r434 (Stefan: going from running with both mouse buttons to auto-run should be easy): the
	// chord cleared auto-run on EVERY frame, so pressing auto-run while holding both buttons was
	// undone at once and letting go stopped the character. Now only the start of the chord takes
	// over from an auto-run that was already on (r242); auto-run switched on during the chord
	// stays on, so you let go of the buttons and keep running, like the original.
	static bool bWasMouseChordRunning = false;
	const bool bMouseChordRunning = bMouseChordMovingForward && bRightMouseLooking && !bLocalStandingUp;
	const bool bMouseChordStarted = bMouseChordRunning && !bWasMouseChordRunning;
	bWasMouseChordRunning = bMouseChordRunning;
	if (bMouseChordRunning)
	{
		if (APawn* ControlledPawn = GetPawn())
		{
			// Both mouse buttons take over from auto-run (r242, Stefan): as in WoW, letting go
			// of them stops the character instead of auto-run carrying on.
			if (AWowPlayerCharacter* ChordCharacter = Cast<AWowPlayerCharacter>(ControlledPawn); ChordCharacter && bMouseChordStarted)
				ChordCharacter->bIsAutoRunning = false;
			const FRotator ForwardYaw(0.0f, ControlledPawn->GetActorRotation().Yaw, 0.0f);
			ControlledPawn->AddMovementInput(FRotationMatrix(ForwardYaw).GetUnitAxis(EAxis::X), 1.0f);
		}
	}

	// Mirror timers update before FrameXML OnUpdate so status bars observe the
	// latest value during this frame.
	UpdateEnvironmentalMirrorTimers(DeltaTime);

	// Dispatch OnUpdate to all WoW UI frames each tick
	if (UIManager && UIManager->GetEventSystem())
	{
		UIManager->GetEventSystem()->TickOnUpdate(DeltaTime);
	}

	// Update mouse hover state for OnEnter/OnLeave events
	if (UIManager && UIManager->GetFrameManager())
	{
		float MX, MY;
		if (!bLeftMouseLooking && !bRightMouseLooking && GetMousePosition(MX, MY))
		{
			UIManager->GetFrameManager()->UpdateMouseHover(MX, MY);
		}

		// Poll EditBox/Slider for value changes and dispatch Lua events
        if (!UIManager->GetFrameManager()->UnitPortraitProvider)
        {
            TWeakObjectPtr<AWowGameplayController> WeakThis(this);
            UIManager->GetFrameManager()->UnitPortraitProvider = [WeakThis](uint64 Guid) -> UMaterialInterface*
            { return WeakThis.IsValid() ? WeakThis->GetUnitPortrait(Guid) : nullptr; };
        }
        // Dressing room (r300 native side): DressUpModel's Lua methods and IsDressableItem.
        if (!UIManager->GetFrameManager()->DressUpHandler)
        {
            TWeakObjectPtr<AWowGameplayController> WeakDressUp(this);
            UIManager->GetFrameManager()->DressUpHandler = [WeakDressUp](const FString& Action, uint32 ItemEntry, float Value) -> bool
            { return WeakDressUp.IsValid() && WeakDressUp->HandleDressUpAction(Action, ItemEntry, Value); };
        }
        // DressUpFrame opened/closed: draw the preview in DressUpModel, stop capturing when hidden.
        {
            FWowFrameManager* DressFrames = UIManager->GetFrameManager();
            const int64 DressUpFrameHandle = DressFrames->FindFrame(TEXT("DressUpFrame"));
            const bool bDressUpShown = DressUpFrameHandle >= 0 && DressFrames->IsFrameActuallyVisible(DressUpFrameHandle);
            if (bDressUpShown != bDressUpFrameWasShown)
            {
                bDressUpFrameWasShown = bDressUpShown;
                if (bDressUpShown)
                {
                    if (UTextureRenderTarget2D* DressUpTarget = EnsureDressUpPreview())
                        DressFrames->SetDressUpRenderTarget(DressUpTarget);
                }
                else
                {
                    CloseDressUpPreview();
                    bDressUpModelDragging = false;
                }
            }
        }
		UIManager->GetFrameManager()->TickWidgetEvents();

		// Update cooldown overlay animations
		UIManager->GetFrameManager()->TickCooldowns();

		// Target selection is authoritative here. Some stock FrameXML OnUpdate
		// handlers can re-show the frame after PLAYER_TARGET_CHANGED, so correct
		// only a mismatched visibility state instead of rewriting it every tick.
		const int64 TargetFrameHandle = UIManager->GetFrameManager()->FindFrame(TEXT("TargetFrame"));
		if (TargetFrameHandle >= 0)
		{
			const bool bShouldShowTarget = TargetGuid != 0;
			const bool bActuallyVisible = UIManager->GetFrameManager()->IsFrameActuallyVisible(TargetFrameHandle);
			if (UIManager->GetFrameManager()->IsFrameVisible(TargetFrameHandle) != bShouldShowTarget ||
				bActuallyVisible != bShouldShowTarget)
			{
				UIManager->GetFrameManager()->SetFrameVisible(TargetFrameHandle, bShouldShowTarget);
			}

			// Stock target Lua can reveal conditional children after the target-change
			// handler. Keep every adjunct collapsed when no target exists, including
			// the otherwise persistent grey target-of-target plate.
			if (!bShouldShowTarget)
			{
				static const TCHAR* TargetAdjunctFrames[] = {
					TEXT("TargetFrameToT"), TEXT("TargetFrameSpellBar"),
					TEXT("TargetFrameBuffs"), TEXT("TargetFrameDebuffs"),
					TEXT("TargetFrameNumericalThreat"), TEXT("TargetFrameDropDown")
				};
				for (const TCHAR* FrameName : TargetAdjunctFrames)
				{
					const int64 AdjunctHandle = UIManager->GetFrameManager()->FindFrame(FrameName);
					if (AdjunctHandle >= 0 &&
						(UIManager->GetFrameManager()->IsFrameVisible(AdjunctHandle) ||
						 UIManager->GetFrameManager()->IsFrameActuallyVisible(AdjunctHandle)))
					{
						UIManager->GetFrameManager()->SetFrameVisible(AdjunctHandle, false);
					}
				}
			}
		}

		// Delayed re-fire of health/mana events â€” entity data may arrive after UI initializes.
		// Fire once, 5 seconds after first entity data arrives.
		if (!bDelayedHealthUpdateFired && ConnectionManager &&
			ConnectionManager->PacketHandler.EntityManager.Num() > 0)
		{
			static float DelayAccumulator = 0.0f;
			DelayAccumulator += DeltaTime;
			if (DelayAccumulator > 5.0f)
			{
				bDelayedHealthUpdateFired = true;
				UE_LOG(LogWowGameplay, Log, TEXT("Firing delayed health/mana re-init (entities=%d)"),
					ConnectionManager->PacketHandler.EntityManager.Num());
				FireUIEvent(TEXT("UNIT_HEALTH"), {TEXT("player")});
				FireUIEvent(TEXT("UNIT_MAXHEALTH"), {TEXT("player")});
				FireUIEvent(TEXT("UNIT_MANA"), {TEXT("player")});
				FireUIEvent(TEXT("UNIT_MAXMANA"), {TEXT("player")});
				FireUIEvent(TEXT("UNIT_POWER"), {TEXT("player")});
				FireUIEvent(TEXT("UNIT_LEVEL"), {TEXT("player")});
				FireUIEvent(TEXT("PLAYER_FLAGS_CHANGED"));

				// Direct C++ health bar update â€” bypass FrameXML Lua in case it doesn't call SetValue
				if (UIManager && UIManager->GetFrameManager())
				{
					FWowEntity* PlayerEntity = ConnectionManager->PacketHandler.EntityManager.Find(
						ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid);
					if (PlayerEntity)
					{
						int32 HP = PlayerEntity->GetHealth();
						int32 MaxHP = PlayerEntity->GetMaxHealth();
						auto* FM = UIManager->GetFrameManager();

						// Update PlayerFrameHealthBar directly
						int64 HBHandle = FM->FindFrame(TEXT("PlayerFrameHealthBar"));
						UProgressBar* HB = HBHandle >= 0 ? FM->GetStatusBarWidget(HBHandle) : nullptr;
						if (HB && MaxHP > 0)
						{
							HB->SetPercent(static_cast<float>(HP) / static_cast<float>(MaxHP));
							HB->SetFillColorAndOpacity(FLinearColor(0.0f, 1.0f, 0.0f)); // Green
							UE_LOG(LogWowGameplay, Log, TEXT("Direct health bar update: %d/%d = %.1f%%"),
								HP, MaxHP, 100.0f * HP / MaxHP);
						}

						// Update PlayerFrameManaBar directly
						if (PlayerEntity->IsUnit())
						{
							const FWowUnitEntity* UnitEnt = static_cast<const FWowUnitEntity*>(PlayerEntity);
							int32 Power = UnitEnt->GetPower(0);
							int32 MaxPower = UnitEnt->GetMaxPower(0);
							int64 MBHandle = FM->FindFrame(TEXT("PlayerFrameManaBar"));
							UProgressBar* MB = MBHandle >= 0 ? FM->GetStatusBarWidget(MBHandle) : nullptr;
							if (MB && MaxPower > 0)
							{
								MB->SetPercent(static_cast<float>(Power) / static_cast<float>(MaxPower));
								MB->SetFillColorAndOpacity(FLinearColor(0.0f, 0.0f, 1.0f)); // Blue
								UE_LOG(LogWowGameplay, Log, TEXT("Direct mana bar update: %d/%d = %.1f%%"),
									Power, MaxPower, 100.0f * Power / MaxPower);
							}
						}
					}
				}
			}
		}
	}

	// Update tooltip manager
	if (TooltipManager && FSlateApplication::IsInitialized())
	{
		FVector2D MousePosition = FSlateApplication::Get().GetCursorPos();
		TooltipManager->Update(MousePosition);
	}

	MaybeApplyDebugDuelPreview();
	RefreshDuelUi();

	// Local player animation must keep running in offline/world-test sessions.
	// Network/entity animation is handled below once a connection exists.
	UpdatePlayerAnimations();
	MaybeApplyDebugSpellPreview();
	UpdateSpellWeaponSheaths();

	if (!ConnectionManager) return;

	// Rebuild this once for the complete movement pass. Spline actors are moved
	// before network-interpolated actors, so clearing it inside the latter pass
	// discarded every spline actor's measured movement before animation updated.
	VisuallyMovingEntityGuids.Reset();
    EntityVisualSpeedsCm.Reset();

	// Update entity spline movement
	ProcessPendingEntityAppearanceUpdates(DeltaTime);
	ProcessPendingEntityModelSpawns(DeltaTime);
	UpdateEntitySplineMovement(DeltaTime);
	UpdateTransports(DeltaTime); // r599: before the network lerp, which leaves transports alone
	UpdateEntityNetworkMovement(DeltaTime);
	ConformEntitiesToGround(DeltaTime);

	// Movement heartbeat â€” send position to server while moving
	SendMovementUpdate();
	MovementSyncTimer += DeltaTime;
	if (MovementSyncTimer >= MovementSyncInterval)
	{
		MovementSyncTimer = 0.0f;
        if (!EnvironmentalWorldManager.IsValid()) EnvironmentalWorldManager=Cast<AWowWorldManager>(
            UGameplayStatics::GetActorOfClass(GetWorld(),AWowWorldManager::StaticClass()));
        if (EnvironmentalWorldManager.IsValid())
        {
            const uint32 Area=EnvironmentalWorldManager->GetCurrentAreaId();
            const uint32 Zone=EnvironmentalWorldManager->GetCurrentZoneId();
            if (Area!=LastUiAreaId || Zone!=LastUiZoneId)
            {
                const bool NewZone=Zone!=LastUiZoneId;
                LastUiAreaId=Area;LastUiZoneId=Zone;
                if (NewZone) WowAutoJoinZoneChannels(CachedMpq, ConnectionManager, Zone);
                FireUIEvent(TEXT("ZONE_CHANGED"),{});
                FireUIEvent(TEXT("ZONE_CHANGED_INDOORS"),{});
                if (NewZone)
                {
                    FireUIEvent(TEXT("ZONE_CHANGED_NEW_AREA"),{});
                    ZoneUntrackedQuests.Reset();
                    ApplyQuestTracking();
                    UpdateQuestTracker();
                    if (QuestLogWidget.IsValid()) QuestLogWidget->UpdateQuestList();
                }
            }
        }
    }

	// Keep-alive
	KeepAliveTimer += DeltaTime;
	if (KeepAliveTimer >= KeepAliveInterval)
	{
		KeepAliveTimer = 0.0f;
		ConnectionManager->SendKeepAlive();
	}

	// Update non-local character animations
	UpdateEntityAnimations();

	// Spell visual M2 creation is limited to one request per tick. Cast/channel
	// progress remains driven by authoritative packet timing and pushback updates.
	ProcessPendingSpellVisuals();
	UpdateSpellProgressBar();

	// Update XP bar (throttled to avoid excessive updates)
	static float XPUpdateTimer = 0.0f;
	XPUpdateTimer += DeltaTime;
	if (XPUpdateTimer >= 0.5f) // Update every 500ms
	{
		XPUpdateTimer = 0.0f;
		UpdateXPBar();
	}

	// Update quest tracker (throttled to avoid excessive updates)
	static float QuestUpdateTimer = 0.0f;
	QuestUpdateTimer += DeltaTime;
	if (QuestUpdateTimer >= 2.0f) // Update every 2 seconds
	{
		QuestUpdateTimer = 0.0f;
		UpdateQuestTracker();
	}

	// Update target frame display
	UpdateTargetFrame();

	// VR: screen-space nameplates are drawn into one eye only, so in the headset they become
	// world-space cards over the unit that face the head every frame (both eyes, real depth).
	{
		const bool bVrPlates = FWowVRSupport::IsActive();
		FVector PlateEye; FRotator PlateEyeRot;
		GetPlayerViewPoint(PlateEye, PlateEyeRot);
		for (auto& PlatePair : EntityNameplates)
		{
			UWidgetComponent* Plate = PlatePair.Value;
			if (!IsValid(Plate)) continue;
			const EWidgetSpace Want = bVrPlates ? EWidgetSpace::World : EWidgetSpace::Screen;
			if (Plate->GetWidgetSpace() != Want)
			{
				Plate->SetWidgetSpace(Want);
				Plate->MarkRenderStateDirty(); // the scene proxy exists only in world space
				if (!bVrPlates) Plate->SetWorldScale3D(FVector(PlatePair.Key == TargetGuid ? 1.2f : 1.0f));
				static bool bLoggedVrPlates = false; if (bLoggedVrPlates != bVrPlates) { bLoggedVrPlates = bVrPlates; UE_LOG(LogWowGameplay, Log, TEXT("VR nameplates: %s"), bVrPlates ? TEXT("world space") : TEXT("screen space")); }
			}
			if (!bVrPlates || !Plate->IsVisible()) continue;
			const FVector PlatePos = Plate->GetComponentLocation();
			const float PlateDist = float(FVector::Dist(PlateEye, PlatePos));
			// 280 px card: ~35 cm wide up close, growing with distance so names stay readable
			// (about 4 degrees wide at 10 m, 2.5 degrees at 40 m).
			const float PlateScale = FMath::Clamp(.1f + PlateDist * .00022f, .12f, 1.f) * (PlatePair.Key == TargetGuid ? 1.2f : 1.f);
			Plate->SetWorldScale3D(FVector(PlateScale));
			Plate->SetWorldRotation((PlateEye - PlatePos).Rotation());
		}
	}

	// VR: quest diamonds, chat bubbles and XP gain as world cards.
	UpdateVrWorldCards(DeltaTime);
	UpdateImmersiveInterface(DeltaTime); // r257 experimental: fade the interface while moving / in combat

	// The live minimap image is native; it follows the FrameXML MinimapCluster (ring, buttons)
	// when that was Ctrl-dragged somewhere else.
	if (MinimapWidget.IsValid() && UIManager && UIManager->GetFrameManager())
		if (UWidget* Cluster = UIManager->GetFrameManager()->GetWidgetForHandle(UIManager->GetFrameManager()->FindFrame(TEXT("MinimapCluster"))))
		{
			// Stefan 11.10: Minimap Size scaled the ring but not the map. The cluster is drawn at
			// scale S about its top-left with translation T (frame manager, ApplyElementScales), so
			// a layout point p lands on Pos + T + (p - Pos) * S. The map scales about its own
			// top-right corner Q (42 in from the screen's right edge, 40 down: the overlay padding);
			// moving Q where the cluster moves it keeps the map inside the ring at every size.
			const FWidgetTransform& ClusterTransform = Cluster->GetRenderTransform();
			const float MapScale = FMath::Max(0.05f, float(ClusterTransform.Scale.X));
			FVector2D ClusterPos = FVector2D::ZeroVector;
			if (const UCanvasPanelSlot* ClusterSlot = Cast<UCanvasPanelSlot>(Cluster->Slot))
				ClusterPos = ClusterSlot->GetPosition();
			if (const UPanelWidget* ClusterParent = Cluster->GetParent())
				if (const UCanvasPanelSlot* ClusterParentSlot = Cast<UCanvasPanelSlot>(ClusterParent->Slot))
					ClusterPos += ClusterParentSlot->GetPosition();
			const TSharedPtr<SWidget> MapContainer = MinimapWidget->GetParentWidget();
			const float ContainerWidth = MapContainer.IsValid() ? float(MapContainer->GetCachedGeometry().GetLocalSize().X) : 0.f;
			FVector2D MapOffset = ClusterTransform.Translation;
			if (ContainerWidth > 1.f)
			{
				const FVector2D MapCorner(ContainerWidth - 42.f, 40.f);
				MapOffset = ClusterPos + ClusterTransform.Translation + (MapCorner - ClusterPos) * MapScale - MapCorner;
			}
			MinimapWidget->SetRenderTransformPivot(FVector2D(1.f, 0.f));
			MinimapWidget->SetRenderTransform(FSlateRenderTransform(MapScale, FVector2f(MapOffset)));
		}

	// Update aura display (throttled)
	AuraUpdateTimer += DeltaTime;
	if (AuraUpdateTimer >= AuraUpdateInterval)
	{
		AuraUpdateTimer = 0.0f;
		UpdateAuraDisplay();
        RefreshDetectionAuraVisuals();
        UpdateBonusActionBar();
        RefreshWorldLabels();
        if (UIManager) UIManager->RefreshUnitFrames();
	}
}

void AWowGameplayController::FireMirrorTimerStartEvent(uint32 TimerId, int32 ValueMs,
	int32 MaxValueMs, int32 Scale, bool bPaused)
{
	const FString TimerName = FWowFrameManager::GetMirrorTimerName(static_cast<int32>(TimerId));
	const FString Label = TimerId == 0 ? TEXT("Fatigue") : TimerId == 1 ? TEXT("Breath") : TEXT("Fire");
	FireUIEvent(TEXT("MIRROR_TIMER_START"), {
		TimerName,
		FString::FromInt(ValueMs),
		FString::FromInt(MaxValueMs),
		FString::FromInt(Scale),
		bPaused ? TEXT("1") : TEXT("0"),
		Label
	});
}

void AWowGameplayController::FireMirrorTimerStopEvent(uint32 TimerId)
{
	FireUIEvent(TEXT("MIRROR_TIMER_STOP"), {
		FWowFrameManager::GetMirrorTimerName(static_cast<int32>(TimerId))
	});

	// FrameXML is allowed to rearrange the remaining mirror timers, but the
	// native timer state is authoritative. Apply the hidden state after the Lua
	// callback as well so a stop event cannot leave an empty casting-bar frame
	// behind until the next timer update.
	if (UIManager && UIManager->GetFrameManager())
	{
		UIManager->GetFrameManager()->StopMirrorTimer(static_cast<int32>(TimerId));
	}
}

void AWowGameplayController::OnMirrorTimerStarted(uint32 TimerId, int32 ValueMs,
	int32 MaxValueMs, int32 Scale, bool bPaused, uint32 SpellId)
{
	if (!UIManager || !UIManager->GetFrameManager())
	{
		return;
	}

	if (!bServerProvidesMirrorTimers)
	{
		// First server timer: drop any locally predicted bars so the client and the
		// realm never show their own breath/fatigue bar at the same time (water surface).
		bServerProvidesMirrorTimers = true;
		for (uint32 LocalId = 0; LocalId < 2; ++LocalId)
		{
			if (LocalId != TimerId && !bServerMirrorTimerActive[LocalId])
			{
				UIManager->GetFrameManager()->StopMirrorTimer(static_cast<int32>(LocalId));
				FireMirrorTimerStopEvent(LocalId);
			}
		}
		bOfflineBreathHazard = bOfflineBreathRefilling = false;
		bOfflineFatigueHazard = bOfflineFatigueRefilling = false;
	}
	UIManager->GetFrameManager()->StartMirrorTimer(
		static_cast<int32>(TimerId), ValueMs, MaxValueMs, Scale, bPaused, SpellId);
	if (TimerId < UE_ARRAY_COUNT(bServerMirrorTimerActive))
	{
		bServerMirrorTimerActive[TimerId] = true;
	}
	FireMirrorTimerStartEvent(TimerId, ValueMs, MaxValueMs, Scale, bPaused);
}

void AWowGameplayController::OnMirrorTimerPaused(uint32 TimerId, bool bPaused)
{
	if (!UIManager || !UIManager->GetFrameManager())
	{
		return;
	}

	UIManager->GetFrameManager()->PauseMirrorTimer(static_cast<int32>(TimerId), bPaused);
	FireUIEvent(TEXT("MIRROR_TIMER_PAUSE"), {
		FWowFrameManager::GetMirrorTimerName(static_cast<int32>(TimerId)),
		bPaused ? TEXT("1") : TEXT("0")
	});
}

void AWowGameplayController::OnMirrorTimerStopped(uint32 TimerId)
{
	if (!UIManager || !UIManager->GetFrameManager())
	{
		return;
	}

	UIManager->GetFrameManager()->StopMirrorTimer(static_cast<int32>(TimerId));
	if (TimerId < UE_ARRAY_COUNT(bServerMirrorTimerActive))
	{
		bServerMirrorTimerActive[TimerId] = false;
	}
	if (TimerId == 1)
	{
		bOfflineBreathHazard = false;
		bOfflineBreathRefilling = false;
	}
	else if (TimerId == 0)
	{
		bOfflineFatigueHazard = false;
		bOfflineFatigueRefilling = false;
	}
	FireMirrorTimerStopEvent(TimerId);
}

void AWowGameplayController::UpdateEnvironmentalMirrorTimers(float DeltaTime)
{
	if (!UIManager || !UIManager->GetFrameManager())
	{
		return;
	}

	FWowFrameManager* FrameManager = UIManager->GetFrameManager();
	FrameManager->TickMirrorTimers(DeltaTime);

	const bool bOnline = ConnectionManager &&
		ConnectionManager->GetState() == EWowSessionState::WorldInGame;

	AWowPlayerCharacter* PlayerCharacter = Cast<AWowPlayerCharacter>(GetPawn());
	if (!PlayerCharacter)
	{
		return;
	}

	if (!EnvironmentalWorldManager.IsValid() && GetWorld())
	{
		EnvironmentalWorldManager = Cast<AWowWorldManager>(
			UGameplayStatics::GetActorOfClass(GetWorld(), AWowWorldManager::StaticClass()));
	}

	const bool bHeadSubmerged = PlayerCharacter->IsHeadSubmerged() ||
		FParse::Param(FCommandLine::Get(), TEXT("testbreathbar"));
	const bool bOutsidePlayableWorld = EnvironmentalWorldManager.IsValid() &&
		!EnvironmentalWorldManager->IsPositionInsidePlayableWorld(PlayerCharacter->GetActorLocation());

	if (!bHeadSubmerged && !bOutsidePlayableWorld)
	{
		const UCharacterMovementComponent* Movement = PlayerCharacter->GetCharacterMovement();
		if (!Movement || Movement->IsMovingOnGround() || PlayerCharacter->IsInWowWaterSwimmingState())
		{
			LastSafeEnvironmentalPosition = PlayerCharacter->GetActorLocation();
			bHasLastSafeEnvironmentalPosition = true;
		}
	}

	const bool bFastMirrorTimerTest = FParse::Param(FCommandLine::Get(), TEXT("fastmirrortimers"));
	const int32 BreathMaxMs = bFastMirrorTimerTest ? 12000 : 180000;
	const int32 FatigueMaxMs = bFastMirrorTimerTest ? 12000 : 60000;
	static constexpr int32 DepletionScale = -1;
	static constexpr int32 RefillScale = 10;

	auto UpdateOfflineTimer = [this, FrameManager, BreathMaxMs, FatigueMaxMs, bOnline](int32 TimerId, bool bHazard,
		bool& bWasHazard, bool& bRefilling)
	{
		// Online the realm is authoritative: once it has sent any mirror timer, the
		// local prediction is off (it disagreed at the water surface and showed its own bar).
		if (bOnline && (bServerProvidesMirrorTimers || (TimerId >= 0 && TimerId < UE_ARRAY_COUNT(bServerMirrorTimerActive) &&
			bServerMirrorTimerActive[TimerId])))
		{
			return;
		}

		FWowMirrorTimerState State;
		const bool bHasState = FrameManager->GetMirrorTimerState(TimerId, State);
		const int32 TimerMaxMs = TimerId == 1 ? BreathMaxMs : FatigueMaxMs;

		if (bHazard && !bWasHazard)
		{
			const int32 StartValueMs = bHasState
				? FMath::RoundToInt(State.ValueMs)
				: TimerMaxMs;
			FrameManager->StartMirrorTimer(TimerId, StartValueMs, TimerMaxMs,
				DepletionScale, false);
			FireMirrorTimerStartEvent(TimerId, StartValueMs, TimerMaxMs,
				DepletionScale, false);
			bWasHazard = true;
			bRefilling = false;
		}
		else if (!bHazard && bWasHazard)
		{
			const int32 StartValueMs = bHasState
				? FMath::RoundToInt(State.ValueMs)
				: TimerMaxMs;
			if (StartValueMs < TimerMaxMs)
			{
				FrameManager->StartMirrorTimer(TimerId, StartValueMs, TimerMaxMs,
					RefillScale, false);
				FireMirrorTimerStartEvent(TimerId, StartValueMs, TimerMaxMs,
					RefillScale, false);
				bRefilling = true;
			}
			else
			{
				FrameManager->StopMirrorTimer(TimerId);
				FireMirrorTimerStopEvent(TimerId);
				bRefilling = false;
			}
			bWasHazard = false;
		}
		else if (!bHazard && bRefilling && bHasState &&
			State.ValueMs >= State.MaxValueMs - 0.5f)
		{
			FrameManager->StopMirrorTimer(TimerId);
			FireMirrorTimerStopEvent(TimerId);
			bRefilling = false;
		}
	};

	UpdateOfflineTimer(1, bHeadSubmerged, bOfflineBreathHazard, bOfflineBreathRefilling);
	UpdateOfflineTimer(0, bOutsidePlayableWorld, bOfflineFatigueHazard, bOfflineFatigueRefilling);

	auto ApplyExhaustion = [this, PlayerCharacter, FrameManager, DeltaTime](int32 TimerId,
		bool bHazard, float& ExhaustionSeconds, int32& DamageTicks, const TCHAR* DamageMessage)
	{
		FWowMirrorTimerState State;
		const bool bEmpty = bHazard && FrameManager->GetMirrorTimerState(TimerId, State) &&
			State.ValueMs <= 0.5f;
		if (!bEmpty)
		{
			ExhaustionSeconds = 0.0f;
			DamageTicks = 0;
			return;
		}

		ExhaustionSeconds += FMath::Max(0.0f, DeltaTime);
		const int32 NewDamageTicks = FMath::FloorToInt(ExhaustionSeconds / 2.0f);
		if (NewDamageTicks > DamageTicks)
		{
			DamageTicks = NewDamageTicks;
			AddCombatMessage(DamageMessage, FLinearColor(1.0f, 0.15f, 0.1f, 1.0f), false, true);
		}

		if (ExhaustionSeconds < 10.0f || !bHasLastSafeEnvironmentalPosition)
		{
			return;
		}

		PlayerCharacter->SetActorLocation(
			LastSafeEnvironmentalPosition, false, nullptr, ETeleportType::TeleportPhysics);
		if (UCharacterMovementComponent* Movement = PlayerCharacter->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		AddFrameXmlChatLine(
			TimerId == 1
				? TEXT("You have drowned and were returned to the last safe location.")
				: TEXT("You became exhausted and were returned to the last safe location."),
			FLinearColor(1.0f, 0.15f, 0.1f, 1.0f));
		ExhaustionSeconds = 0.0f;
		DamageTicks = 0;
	};

	// Online servers remain authoritative for damage and death. The client-side
	// timers above are visual prediction/fallback only when a realm omits the
	// corresponding mirror-timer packet.
	if (!bOnline)
	{
		ApplyExhaustion(1, bHeadSubmerged, OfflineBreathExhaustionSeconds,
			OfflineBreathDamageTicks, TEXT("Drowning damage"));
		ApplyExhaustion(0, bOutsidePlayableWorld, OfflineFatigueExhaustionSeconds,
			OfflineFatigueDamageTicks, TEXT("Fatigue damage"));
	}
}

void AWowGameplayController::ResetOutgoingMovementState()
{
	OutgoingMove = FWowOutgoingMoveState();
}

uint32 AWowGameplayController::ComputeLocalMovementFlags(ACharacter* Char) const
{
	uint32 Flags = ServerMovementModifierFlags;
	if (bServerRooted) Flags |= WowMoveFlag::ROOT;
	UCharacterMovementComponent* Movement = Char ? Char->GetCharacterMovement() : nullptr;
	if (!Movement) return Flags;

	const AWowPlayerCharacter* WowCharacter = Cast<AWowPlayerCharacter>(Char);
	const FVector Velocity = Movement->Velocity;
	if (!bServerRooted && Velocity.Size2D() > 10.0f)
	{
		FVector DirectionSource = Char->GetLastMovementInputVector();
		if (DirectionSource.SizeSquared2D() <= KINDA_SMALL_NUMBER) DirectionSource = Velocity;
		const FVector MoveDirection = DirectionSource.GetSafeNormal2D();
		const float Dot = FVector::DotProduct(Char->GetActorForwardVector(), MoveDirection);
		if (Dot > 0.3f) Flags |= WowMoveFlag::FORWARD;
		else if (Dot < -0.3f) Flags |= WowMoveFlag::BACKWARD;
		const float SideDot = FVector::DotProduct(Char->GetActorRightVector(), MoveDirection);
		if (SideDot > 0.5f) Flags |= WowMoveFlag::STRAFE_RIGHT;
		else if (SideDot < -0.5f) Flags |= WowMoveFlag::STRAFE_LEFT;
	}
	const bool bSwimming = WowCharacter && WowCharacter->IsInWowWaterSwimmingState();
	if (bSwimming) Flags |= WowMoveFlag::SWIMMING;
	// 3.3.5 uses a single FALLING flag for the whole jump/fall arc; it obliges
	// the client to send the jump block (z speed, direction, xy speed).
	if (!bSwimming && Movement->IsFalling()) Flags |= WowMoveFlag::FALLING;
	if (WowCharacter && WowCharacter->bIsWalking) Flags |= WowMoveFlag::WALKING;
	return Flags;
}

FWowOutgoingMovementInfo AWowGameplayController::BuildLocalMovementInfo(ACharacter* Char, uint32 Flags) const
{
	FWowOutgoingMovementInfo Info;
	Info.Flags = Flags;
	Info.TimeMs = WowMovementPacket::ClientTimeMs();
	if (!Char) return Info;
	Info.Position = FWowCoordinate::UEToWow(FWowLoginPlacement::Feet(*Char));
	Info.Orientation = FWowCoordinate::UEYawToWowOrientation(Char->GetActorRotation().Yaw);
	if (Flags & (WowMoveFlag::SWIMMING | WowMoveFlag::FLYING))
	{
		if (const UCharacterMovementComponent* Movement = Char->GetCharacterMovement())
		{
			const FVector V = Movement->Velocity;
			Info.Pitch = V.Size() > 10.0f
				? FMath::Clamp(FMath::Atan2(static_cast<float>(V.Z), static_cast<float>(V.Size2D())), -UE_HALF_PI, UE_HALF_PI)
				: 0.0f;
		}
	}
	// Fall time runs for the whole arc and is still reported by MSG_MOVE_FALL_LAND.
	if (OutgoingMove.bAirborne)
	{
		Info.FallTimeMs = static_cast<uint32>(FMath::Max(0.0,
			(FPlatformTime::Seconds() - OutgoingMove.AirborneStartSeconds) * 1000.0));
	}
	if (Flags & WowMoveFlag::FALLING)
	{
		Info.JumpZSpeed = OutgoingMove.JumpZSpeed;
		Info.JumpSinAngle = OutgoingMove.JumpSin;
		Info.JumpCosAngle = OutgoingMove.JumpCos;
		Info.JumpXYSpeed = OutgoingMove.JumpXYSpeed;
	}
	// r601: on a boat, zeppelin or elevator every packet carries the transport and our offset on it.
	ApplyLocalTransport(Char, Info);
	return Info;
}

void AWowGameplayController::SendMovementUpdate()
{
	ACharacter* Char = Cast<ACharacter>(GetPawn());
	if (!Char || !ConnectionManager || !bHasServerPosition || !bPlayerEnteredWorld || !bGameplayHudVisible) return;
	if (const FWowEntity* Self = ConnectionManager->PacketHandler.EntityManager.Find(ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid);
		Self && Self->Movement.bHasActiveSpline) return; // server-driven (Charge): it does not expect our packets
	UCharacterMovementComponent* Movement = Char->GetCharacterMovement();
	if (!Movement) return;

	const double Now = FPlatformTime::Seconds();
	const uint32 Flags = ComputeLocalMovementFlags(Char);
	const float Orientation = FWowCoordinate::UEYawToWowOrientation(Char->GetActorRotation().Yaw);
	FWowOutgoingMoveState& State = OutgoingMove;
	const uint32 PrevFlags = State.bValid ? State.Flags : (Flags & ~(WowMoveFlag::MOVING_MASK | WowMoveFlag::FALLING));

	// Airborne bookkeeping: capture launch velocity once, like the original
	// client, and keep it for every packet of this arc.
	const bool bAirborne = (Flags & WowMoveFlag::FALLING) != 0;
	bool bJumpStarted = false;
	if (bAirborne && !State.bAirborne)
	{
		const FVector V = Movement->Velocity / 100.0f; // UE cm/s -> yards/s
		const FVector2D WowXY(V.X, -V.Y);              // UE (x, y) -> WoW (x, -y)
		const float XY = WowXY.Size();
		State.bAirborne = true;
		State.AirborneStartSeconds = Now;
		State.JumpZSpeed = -static_cast<float>(V.Z);    // WoW: negative while rising
		State.JumpXYSpeed = XY;
		State.JumpCos = XY > KINDA_SMALL_NUMBER ? static_cast<float>(WowXY.X) / XY : FMath::Cos(Orientation);
		State.JumpSin = XY > KINDA_SMALL_NUMBER ? static_cast<float>(WowXY.Y) / XY : FMath::Sin(Orientation);
		bJumpStarted = V.Z > 0.5f;
	}

	// r522: where the server last heard we were (any movement packet carries the position).
	static FVector GLastPacketPosition = FVector::ZeroVector;
	const bool bForced = State.LastSendSeconds == 0.0; // r373/r504: "tell the server where we are now"
	auto Send = [&](uint16 Opcode, uint32 SendFlags)
	{
		ConnectionManager->SendMovementInfo(Opcode, BuildLocalMovementInfo(Char, SendFlags));
		State.LastSendSeconds = Now;
		GLastPacketPosition = Char->GetActorLocation();
	};

	bool bSentAny = false;
	if (bJumpStarted)
	{
		Send(WowOpcode::MSG_MOVE_JUMP, Flags);
		bSentAny = true;
	}
	else if (!bAirborne && State.bAirborne)
	{
		// Landing: fall time and the airborne flags of the arc we just left.
		Send(WowOpcode::MSG_MOVE_FALL_LAND, Flags);
		State.bAirborne = false;
		bSentAny = true;
	}
	if (!bAirborne) State.bAirborne = false;

	const uint32 Changed = Flags ^ PrevFlags;
	if (Changed & WowMoveFlag::SWIMMING)
	{
		Send((Flags & WowMoveFlag::SWIMMING) ? WowOpcode::MSG_MOVE_START_SWIM : WowOpcode::MSG_MOVE_STOP_SWIM, Flags);
		bSentAny = true;
	}
	if (Changed & WowMoveFlag::WALKING)
	{
		Send((Flags & WowMoveFlag::WALKING) ? WowOpcode::MSG_MOVE_SET_WALK_MODE : WowOpcode::MSG_MOVE_SET_RUN_MODE, Flags);
		bSentAny = true;
	}
	if (Changed & (WowMoveFlag::FORWARD | WowMoveFlag::BACKWARD))
	{
		const uint16 Op = (Flags & WowMoveFlag::FORWARD) ? WowOpcode::MSG_MOVE_START_FORWARD
			: (Flags & WowMoveFlag::BACKWARD) ? WowOpcode::MSG_MOVE_START_BACKWARD
			: WowOpcode::MSG_MOVE_STOP;
		Send(Op, Flags);
		bSentAny = true;
	}
	if (Changed & (WowMoveFlag::STRAFE_LEFT | WowMoveFlag::STRAFE_RIGHT))
	{
		const uint16 Op = (Flags & WowMoveFlag::STRAFE_LEFT) ? WowOpcode::MSG_MOVE_START_STRAFE_LEFT
			: (Flags & WowMoveFlag::STRAFE_RIGHT) ? WowOpcode::MSG_MOVE_START_STRAFE_RIGHT
			: WowOpcode::MSG_MOVE_STOP_STRAFE;
		Send(Op, Flags);
		bSentAny = true;
	}

	const bool bMoving = (Flags & WowMoveFlag::MOVING_MASK) != 0 || bAirborne
		|| ((Flags & WowMoveFlag::SWIMMING) && Movement->Velocity.SizeSquared() > 100.0f);
	const float FacingDelta = State.bValid ? FMath::Abs(FMath::FindDeltaAngleRadians(State.Orientation, Orientation)) : UE_PI;
	if (!bSentAny)
	{
		if (bMoving)
		{
			// The original client heartbeats every 500 ms while moving; turn
			// while moving is folded into an earlier heartbeat.
			const double Interval = FacingDelta > FMath::DegreesToRadians(5.0f) ? 0.2 : 0.5;
			if (Now - State.LastSendSeconds >= Interval) { Send(WowOpcode::MSG_MOVE_HEARTBEAT, Flags); bSentAny = true; }
		}
		else if (FacingDelta > FMath::DegreesToRadians(1.0f) && Now - State.LastFacingSendSeconds >= 0.2)
		{
			Send(WowOpcode::MSG_MOVE_SET_FACING, Flags);
			State.LastFacingSendSeconds = Now;
			bSentAny = true;
		}
		// r522 (Stefan: Felix's Box needed several clicks, "Out of range" standing right at it; Gornek and
		// Galgar earlier): standing still, the "send our position first" of r373/r504 sent nothing at all,
		// because only moving sends heartbeats. And MSG_MOVE_STOP goes out when the key is released, while
		// the character still slides about a metre to a halt, so the server kept us that far back. Standing
		// still, a heartbeat now goes out when forced, and once after a stop if we ended up more than 30 cm
		// from the last position the server got.
		else if (!bSentAny && State.bValid
			&& (bForced ? FVector::DistSquared(Char->GetActorLocation(), GLastPacketPosition) > 1.0
				: (Movement->Velocity.SizeSquared2D() < 25.0 && Now - State.LastSendSeconds >= 0.1
					&& FVector::DistSquared(Char->GetActorLocation(), GLastPacketPosition) > FMath::Square(30.0))))
		{
			Send(WowOpcode::MSG_MOVE_HEARTBEAT, Flags);
			bSentAny = true;
		}
	}
	else
	{
		State.LastFacingSendSeconds = Now;
	}

	State.Flags = Flags;
	if (bSentAny || !State.bValid) State.Orientation = Orientation;
	State.bValid = true;
	// Retained for the remaining position bookkeeping elsewhere in the controller.
	LastSentPosition = Char->GetActorLocation();
	LastSentYawDegrees = Char->GetActorRotation().Yaw;
	LastSentMovementFlags = Flags;
}

void AWowGameplayController::OnForcedMovement(const FWowForcedMovement& Change)
{
	if (!ConnectionManager) return;
	ACharacter* Char = Cast<ACharacter>(GetPawn());
	AWowPlayerCharacter* WowChar = Cast<AWowPlayerCharacter>(Char);
	UCharacterMovementComponent* Movement = Char ? Char->GetCharacterMovement() : nullptr;

	bool bAckFloat = false, bAckU32 = false;
	float AckFloat = 0.0f;
	uint32 AckU32 = 0;
	switch (Change.Kind)
	{
	case EWowForcedMoveKind::Speed:
		bAckFloat = true;
		AckFloat = Change.Speed;
		if (WowChar && Change.SpeedType == EWowSpeedType::Swim && WowChar->RunSpeed > 1.0f)
		{
			WowChar->SwimSpeedFactor = FMath::Clamp(Change.Speed * 100.0f / WowChar->RunSpeed, 0.05f, 10.0f);
			WowChar->ApplyGameplayMovementSettings(false);
		}
		// Run/walk speeds are applied through OnEntityUpdated -> ApplyServerSpeeds.
		break;
	case EWowForcedMoveKind::Root:
		bServerRooted = true;
		if (Movement) { Movement->StopMovementImmediately(); Movement->DisableMovement(); }
		break;
	case EWowForcedMoveKind::Unroot:
		bServerRooted = false;
		if (Movement && !bMapTerrainPending)
		{
			Movement->SetMovementMode(MOVE_Falling);
			if (WowChar) WowChar->ApplyGameplayMovementSettings(false);
		}
		break;
	case EWowForcedMoveKind::KnockBack:
		if (Char)
		{
			// WoW (x, y) -> UE (x, -y); yards -> cm.
			const FVector Launch(Change.KnockCos * Change.KnockSpeedXY * 100.0f,
				-Change.KnockSin * Change.KnockSpeedXY * 100.0f,
				Change.KnockSpeedZ * 100.0f);
			Char->LaunchCharacter(Launch, true, true);
			// The ack reports the launch as the start of a fall arc.
			OutgoingMove.bAirborne = true;
			OutgoingMove.AirborneStartSeconds = FPlatformTime::Seconds();
			OutgoingMove.JumpZSpeed = -Change.KnockSpeedZ;
			OutgoingMove.JumpCos = Change.KnockCos;
			OutgoingMove.JumpSin = Change.KnockSin;
			OutgoingMove.JumpXYSpeed = Change.KnockSpeedXY;
		}
		break;
	case EWowForcedMoveKind::WaterWalk:   ServerMovementModifierFlags |= WowMoveFlag::WATERWALKING; bAckU32 = true; AckU32 = 1; break;
	case EWowForcedMoveKind::LandWalk:    ServerMovementModifierFlags &= ~WowMoveFlag::WATERWALKING; bAckU32 = true; AckU32 = 0; break;
	case EWowForcedMoveKind::Hover:       ServerMovementModifierFlags |= WowMoveFlag::HOVER; bAckU32 = true; AckU32 = 1; break;
	case EWowForcedMoveKind::Unhover:     ServerMovementModifierFlags &= ~WowMoveFlag::HOVER; bAckU32 = true; AckU32 = 0; break;
	case EWowForcedMoveKind::SetCanFly:   ServerMovementModifierFlags |= WowMoveFlag::CAN_FLY; bAckU32 = true; AckU32 = 1; break;
	case EWowForcedMoveKind::UnsetCanFly: ServerMovementModifierFlags &= ~(WowMoveFlag::CAN_FLY | WowMoveFlag::FLYING); bAckU32 = true; AckU32 = 0; break;
	case EWowForcedMoveKind::FeatherFall:
	case EWowForcedMoveKind::NormalFall:
	{
		const bool bSlow = Change.Kind == EWowForcedMoveKind::FeatherFall;
		if (bSlow) ServerMovementModifierFlags |= WowMoveFlag::FALLING_SLOW;
		else ServerMovementModifierFlags &= ~WowMoveFlag::FALLING_SLOW;
		if (WowChar) WowChar->ApplyGameplayMovementSettings(false);
		if (Movement && bSlow) Movement->GravityScale *= 0.15f; // slow fall, applied after the reset above
		bAckU32 = true; AckU32 = bSlow ? 1u : 0u;
		break;
	}
	}

	uint32 Flags = Char ? ComputeLocalMovementFlags(Char) : ServerMovementModifierFlags;
	if (Change.Kind == EWowForcedMoveKind::KnockBack) Flags |= WowMoveFlag::FALLING;
	FWowOutgoingMovementInfo Info = BuildLocalMovementInfo(Char, Flags);
	ConnectionManager->SendMovementAck(Change.AckOpcode, Change.Counter, Info, bAckFloat, AckFloat, bAckU32, AckU32);
	if (OutgoingMove.bValid) OutgoingMove.Flags = Flags & ~WowMoveFlag::FALLING;
}

void AWowGameplayController::OnOpcodeReceived(uint16 Opcode)
{
	// r354: barber chair - SMSG_ENABLE_BARBER_SHOP (0x427) opens the stock Blizzard_BarberShopUI
	// (UIParent loads it on BARBER_SHOP_OPEN), SMSG_BARBER_SHOP_RESULT (0x428) ends a haircut.
	if (Opcode == 0x0427)
	{
		FWowBarberShop::NotifyOpened();
		if (FWowEventSystem* BarberEvents = UIManager ? UIManager->GetEventSystem() : nullptr) BarberEvents->FireEvent(TEXT("BARBER_SHOP_OPEN"));
		return;
	}
	if (Opcode == 0x0428) { FWowBarberShop::NotifyResult(); return; }
	// r377: first login of a new character - the race intro flyby with narration.
	// r380: or the server's class intro movie (FWowTrailerPlayer rules), once per character.
	if (Opcode == WowOpcode::SMSG_TRIGGER_CINEMATIC && ConnectionManager)
	{
		const FWowEntity* Self = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
		uint8 Race = Self ? Self->GetFieldByte(UnitField::BYTES_0, 0) : 0;
		uint8 Class = Self ? Self->GetFieldByte(UnitField::BYTES_0, 1) : 0;
		uint8 Gender = Self ? Self->GetFieldByte(UnitField::BYTES_0, 2) : 0;
		uint64 CharacterGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
		// r693 (Stefan: "Blood Elves didn't get any starting video..?"): the server sends the trigger right after
		// the login, before our own player object exists, so the log read "race 0 class 0 gender 0 -> sequence 0"
		// and nothing played. The character picked on the character screen has the same race, class and gender.
		FWowCharacterInfo Selected;
		if (Race == 0 && ConnectionManager->GetSelectedCharacterInfo(Selected))
		{
			Race = Selected.Race;
			Class = Selected.Class;
			Gender = Selected.Gender;
			if (CharacterGuid == 0) CharacterGuid = static_cast<uint64>(Selected.Guid);
		}
		AWowWorldManager* WorldManager = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this, AWowWorldManager::StaticClass()));
		FMpqManager* Mpq = WorldManager ? WorldManager->GetMpqManager() : CachedMpq;
		AWowFlybyCinematic::HandleTrigger(this, Mpq, Race, Class, Gender, CharacterGuid,
			ConnectionManager->GetSelectedRealmName(), &GWowActiveFlyby);
	}
	if (!UIManager) return;

	FWowEventSystem* EventSystem = UIManager->GetEventSystem();
	if (!EventSystem) return;

	FString EventName = FWowEventSystem::OpcodeToEvent(Opcode);
	// This generic opcode -> event path has no packet data. Events whose handlers need
	// arguments are fired with real arguments by their own handlers; firing them empty
	// gave FrameXML/addons nil arguments ("string expected, got nil", blank chat lines).
	static const TCHAR* const NeedsArguments[] = {
		TEXT("CHAT_MSG_"), TEXT("COMBAT_LOG_EVENT"), TEXT("UI_ERROR_MESSAGE"), TEXT("UI_INFO_MESSAGE"),
		TEXT("UNIT_"), TEXT("BAG_UPDATE"), TEXT("ITEM_LOCK_CHANGED"), TEXT("COMBAT_XP_GAIN"),
		TEXT("ACHIEVEMENT_EARNED"), TEXT("ZONE_UNDER_ATTACK"), TEXT("LEARNED_SPELL_IN_TAB"), TEXT("PLAYER_LEVEL_UP") };
	for (const TCHAR* Prefix : NeedsArguments)
	{
		if (EventName.StartsWith(Prefix)) return;
	}
	if (!EventName.IsEmpty())
	{
		EventSystem->FireEvent(EventName);

		// Fire initial unit stats events when player logs in
		if (EventName == TEXT("PLAYER_LOGIN"))
		{
			TArray<FString> PlayerArgs;
			PlayerArgs.Add(TEXT("player"));
			EventSystem->FireEvent(TEXT("UNIT_HEALTH"), PlayerArgs);
			EventSystem->FireEvent(TEXT("UNIT_POWER"), PlayerArgs);
			EventSystem->FireEvent(TEXT("UNIT_MANA"), PlayerArgs);  // WoW 3.3.5 compatibility alias
		}
	}
}

bool AWowGameplayController::InputKey(const FInputKeyEventArgs& Params)
{
    // Consume new actions while loading; releases still clear held bindings.
    if (OfflineTransferScreen.IsValid() && Params.Event!=IE_Released) return true;
    // r380: a full-screen movie owns input (its overlay normally has focus); Esc skips it.
    if (FWowTrailerPlayer::IsPlaying())
    {
        if (Params.Event==IE_Pressed && Params.Key==EKeys::Escape) FWowTrailerPlayer::Skip();
        return true;
    }
    // r377: the intro flyby owns input; Esc skips it, like the original client.
    if (GWowActiveFlyby.IsValid() && GWowActiveFlyby->IsPlaying())
    {
        if (Params.Event==IE_Pressed && Params.Key==EKeys::Escape) GWowActiveFlyby->Skip();
        return true;
    }
    // Photo mode owns keyboard and mouse (its panel gets clicks through Slate first).
    if (PhotoMode && PhotoMode->IsActive())
    {
        if (Params.Event==IE_DoubleClick) { FInputKeyEventArgs Press=Params; Press.Event=IE_Pressed; return PhotoMode->HandleKey(Press); }
        return PhotoMode->HandleKey(Params);
    }
    // Slate reports the second quick mouse press as DoubleClick. Gameplay bindings
    // listen for Pressed/Released, so normalize it instead of losing alternate clicks.
    if (Params.Event==IE_DoubleClick && (Params.Key==EKeys::LeftMouseButton || Params.Key==EKeys::RightMouseButton))
    { FInputKeyEventArgs Press=Params; Press.Event=IE_Pressed; return InputKey(Press); }

    if(Params.Key!=EKeys::LeftMouseButton && Params.Key!=EKeys::RightMouseButton
        && (Params.Event==IE_Pressed || Params.Event==IE_Released || Params.Event==IE_Repeat))
    {
        const FString Base=FWowKeyBindings::KeyName(Params.Key.GetFName().ToString());
        if(Params.Event==IE_Released) { for(const auto& Command:FWowKeyBindings::Release(Base))ExecuteSavedBinding(Command,false); return true; }
        const auto Mods=FSlateApplication::Get().GetModifierKeys();
        const FString Key=FWowKeyBindings::KeyName(Params.Key.GetFName().ToString(),Mods.IsControlDown(),Mods.IsShiftDown(),Mods.IsAltDown());
        if (Params.Event==IE_Pressed && FWowKeyBindings::Command(Key)==TEXT("TOGGLEUI")
            && (bUserInterfaceHidden || !IsWowUiConsumingKeyboardInput()))
        { SetUserInterfaceHidden(!bUserInterfaceHidden); return true; }
        if (Params.Event==IE_Pressed && FWowKeyBindings::Command(Key)==TEXT("TOGGLEFPS")
            && !IsWowUiConsumingKeyboardInput())
        { TogglePerformanceOverlay(); return true; }
        if(IsWowUiConsumingKeyboardInput())
        {
            if(Params.Key==EKeys::Enter) OnEnterKey();
            else if(Params.Key==EKeys::Escape) OnEscapeKey();
            return true;
        }
        if(IsNativeModalUiVisible()) { if(Params.Key==EKeys::Escape) OnEscapeKey(); return true; }
        if(Params.Event==IE_Repeat)
        {
            // Hold to recast (Interface > Action Bars): a held action key casts again whenever
            // the action is ready, instead of needing a new key press.
            FString RepeatCommand=FWowKeyBindings::Command(Key);
            if(RepeatCommand.IsEmpty()) RepeatCommand=FWowKeyBindings::Command(Base);
            const auto* RepeatAction=FWowKeyBindings::Actions().FindByPredicate([&](const auto& A){return A.Command==RepeatCommand;});
            static double LastRepeatCast=0.0;
            const double RepeatNow=FPlatformTime::Seconds();
            if(RepeatAction && RepeatAction->ActionSlot>=0 && FWowClientPreferences::GetWorldOption(TEXT("holdtorecast"))
                && RepeatNow-LastRepeatCast>=0.25 && UIManager && UIManager->GetEventSystem())
            {
                LastRepeatCast=RepeatNow;
                const int32 Slot=RepeatAction->ActionSlot+(RepeatCommand.StartsWith(TEXT("ACTIONBUTTON"))?12*(FWowKeyBindings::ActionPage()-1):0);
                FWowEventSystem* Events=UIManager->GetEventSystem();
                Events->ExecuteLuaString(FString::Printf(TEXT(R"LUA(
                    local s, d = GetActionCooldown(%d)
                    local busy = (UnitCastingInfo and UnitCastingInfo("player")) or (UnitChannelInfo and UnitChannelInfo("player"))
                    __WowRepeatReady = (not busy and ((s or 0) == 0 or GetTime() >= s + (d or 0))) and "1" or "0"
                )LUA"), Slot+1), TEXT("=HoldToRecast"));
                if(Events->GetGlobalString(TEXT("__WowRepeatReady"))==TEXT("1")) ExecuteSavedBinding(RepeatCommand);
            }
            return true;
        }
        FString EffectiveKey=Key;
        // Keep Ctrl+W/A/D usable while diving, but never steal an assigned chord.
        const AWowPlayerCharacter* Swimmer=Cast<AWowPlayerCharacter>(GetPawn());
        if (Swimmer && Swimmer->IsInWowWaterSwimmingState() && Mods.IsControlDown() &&
            FWowKeyBindings::Command(Key).IsEmpty())
        {
            const FString BaseAction=FWowKeyBindings::Command(Base);
            if (BaseAction==TEXT("MOVEFORWARD") || BaseAction==TEXT("MOVEBACKWARD") ||
                BaseAction==TEXT("STRAFELEFT") || BaseAction==TEXT("STRAFERIGHT")) EffectiveKey=Base;
        }
        // A modified key without its own binding uses the plain key's binding, like the client.
        // After Alt-Tab Windows can leave Alt "held" (its release went to the other window), and
        // then W/A/S/D were ALT-W... with no binding: the character stood still.
        if (EffectiveKey!=Base && FWowKeyBindings::Command(EffectiveKey).IsEmpty() && !FWowKeyBindings::Command(Base).IsEmpty())
            EffectiveKey=Base;
        // r291 (Stefan 13.10): an addon's override binding (Bartender's "CLICK BT4Button1:LeftButton"
        // for the keys of ACTIONBUTTON1 ...) wins over the saved binding, like the client.
        if (UIManager && UIManager->GetEventSystem())
        {
            FWowEventSystem* Events = UIManager->GetEventSystem();
            auto RunOverride = [Events](const FString& OverrideKey)
            {
                Events->ExecuteLuaString(FString::Printf(
                    TEXT("__WowOverrideResult = __WowRunOverrideBinding and __WowRunOverrideBinding([==[%s]==]) or ''"),
                    *OverrideKey), TEXT("=OverrideBinding"));
                return Events->GetGlobalString(TEXT("__WowOverrideResult"));
            };
            FString Override = RunOverride(Key);
            if (Override.IsEmpty() && Key != Base && FWowKeyBindings::Command(Key).IsEmpty()) Override = RunOverride(Base);
            if (Override == TEXT("__handled")) return true;
            if (!Override.IsEmpty()) { ExecuteSavedBinding(Override); return true; }
        }
        ExecuteSavedBinding(FWowKeyBindings::Press(EffectiveKey));
        return true;
    }


	const bool bIsTurnKeyEvent = Params.Event == IE_Pressed || Params.Event == IE_Released || Params.Event == IE_Repeat;
	if (bIsTurnKeyEvent && (Params.Key == EKeys::Q || Params.Key == EKeys::E))
	{
		const bool bPressed = Params.Event != IE_Released;
		if (Params.Key == EKeys::Q)
		{
			bTurnLeftHeld = bPressed;
		}
		else
		{
			bTurnRightHeld = bPressed;
		}
		FocusWowGameViewport();
		Super::InputKey(Params);
		return true;
	}

	if ((bLeftMouseLooking || bRightMouseLooking) && Params.Event == IE_Axis)
	{
		if (Params.Key == EKeys::MouseX)
		{
			PendingMouseTurn += Params.AmountDepressed;
			return true;
		}
		if (Params.Key == EKeys::MouseY)
		{
			PendingMouseLookUp += Params.AmountDepressed;
			return true;
		}
	}

	if (Params.Event == IE_Pressed && IsWowUiConsumingKeyboardInput())
	{
		// Text fields consume gameplay keys, but chat still needs its commit/cancel
		// keys delivered while the editable widget owns keyboard focus.
		if (Params.Key == EKeys::Enter)
		{
			OnEnterKey();
			return true;
		}
		if (Params.Key == EKeys::Escape)
		{
			OnEscapeKey();
			return true;
		}
		if (ShouldSuppressGameplayHotkey(Params.Key))
		{
			return true;
		}
	}

	if (Params.Event == IE_Pressed && Params.Key == EKeys::B)
	{
		const bool bShiftHeld = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
		if (bShiftHeld)
		{
			OnAllBagsKey();
		}
		else
		{
			OnBagKey();
		}
		return true;
	}

	return Super::InputKey(Params);
}

bool AWowGameplayController::IsPointerOverNativeGameUi() const
{
    // VR: only UI the headset shows right now counts; a pointer over a faded panel or outside
    // every panel is over the world (hover highlight and clicks use the gaze or mouse ray).
    if (FWowUiVrHost::IsActive() && !FWowUiVrHost::IsPointerOverShownUi()) return false;
    // The AddOns list dims the whole screen and is modal.
    if (SWowAddonList::IsOpen()) return true;
    // An open unit menu covers the screen (a click outside closes it): the game must not treat
    // that click as a world click (it cleared the target and stole the menu button's press).
    if (UnitMenuWidget.IsValid() && UnitMenuWidget->IsOpen()) return true;
    // Static popups outside the panel layout (group / duel invites) own clicks over them too.
    if (FSlateApplication::IsInitialized())
    {
        const FVector2D Cursor = FSlateApplication::Get().GetCursorPos();
        // The logout/exit countdown too: a press over it started camera look and its buttons never got the click.
        // The whole tracker box (title and its collapse button too, not just the list: Stefan 10.10,
        // the button did nothing) - unless a FrameXML frame such as a bag is drawn over it there.
        const TSharedPtr<SWidget> Tracker = QuestTrackerContainerWidget.IsValid() && QuestTrackerContainerWidget->GetVisibility().IsVisible()
                && QuestTrackerContainerWidget->GetChildren() && QuestTrackerContainerWidget->GetChildren()->Num() > 0
                ? TSharedPtr<SWidget>(QuestTrackerContainerWidget->GetChildren()->GetChildAt(0)) : TSharedPtr<SWidget>();
        const bool bTrackerUnderCursor = Tracker.IsValid() && Tracker->GetCachedGeometry().IsUnderLocation(Cursor);
        const TSharedPtr<SWidget> Popups[] = {PartyInviteWidget, DuelInviteViewportWidget,
            bTrackerUnderCursor && !IsFrameXmlOverTracker() ? Tracker : TSharedPtr<SWidget>(),
            GameMenuCountdownViewportWidget.IsValid() && GameMenuCountdownViewportWidget->GetVisibility().IsVisible()
                ? GameMenuCountdownPanel : TSharedPtr<SWidget>(),
            // The Group Finder is its own surface, not a managed panel: a press over it started
            // camera look, so its check boxes and scroll bars never got the mouse.
            GroupFinderWidget.IsValid() && GroupFinderWidget->IsVisible()
                ? TSharedPtr<SWidget>(GroupFinderWidget) : TSharedPtr<SWidget>(),
            // The quest log's "Abandon <quest>?" popup.
            QuestLogWidget.IsValid() ? QuestLogWidget->GetAbandonPopupFrame() : TSharedPtr<SWidget>()};
        for (const TSharedPtr<SWidget>& Popup : Popups)
            if (Popup.IsValid() && Popup->GetVisibility().IsVisible() && Popup->GetCachedGeometry().IsUnderLocation(Cursor))
            {
                // Only while it and its parents are shown (a stale rect of a hidden popup took clicks).
                bool bShown = true;
                for (TSharedPtr<SWidget> Walk = Popup->GetParentWidget(); Walk.IsValid() && bShown; Walk = Walk->GetParentWidget())
                    bShown = Walk->GetVisibility().IsVisible();
                if (bShown) return true;
            }
    }
    return GameUI && FSlateApplication::IsInitialized()
        && GameUI->IsPointerOverWindow(FSlateApplication::Get().GetCursorPos());
}

bool AWowGameplayController::IsFrameXmlOverTracker() const
{
	// A FrameXML frame (bag, character window, ...) under the cursor where it overlaps the
	// objectives tracker: the frame is drawn on top, so it gets the click (Stefan 10.10: right-click
	// on an item in a bag over the tracker opened the quest on the map instead).
	TGuardValue<bool> Guard(GWowSkipNativeUiCheck, true);
	const int64 Frame = GetUiFrameUnderCursor();
	if (Frame < 0 || !UIManager || !UIManager->GetFrameManager()) return false;
	const FString Name = UIManager->GetFrameManager()->GetFrameName(Frame);
	return Name != TEXT("WorldFrame") && Name != TEXT("UIParent") && !Name.StartsWith(TEXT("WatchFrame"));
}

int64 AWowGameplayController::GetUiFrameUnderCursor() const
{
    if (!GWowSkipNativeUiCheck && IsPointerOverNativeGameUi()) return -1; // Never route clicks to Lua frames behind native windows.
    if (FWowUiVrHost::IsActive() && !FWowUiVrHost::IsPointerOverShownUi()) return -1; // VR: faded / no panel there
	if (!UIManager || !UIManager->GetFrameManager())
	{
		return -1;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		// Right after the window is reactivated (Alt-Tab, clicking back in) the player
		// controller has no viewport mouse position yet: map the OS cursor ourselves so the
		// activation click on an action button or frame is not lost.
		if (!FSlateApplication::IsInitialized() || !GEngine || !GEngine->GameViewport) return -1;
		const TSharedPtr<SViewport> ViewportWidget = GEngine->GameViewport->GetGameViewportWidget();
		if (!ViewportWidget.IsValid()) return -1;
		const FGeometry& Geometry = ViewportWidget->GetCachedGeometry();
		const FVector2D LocalSize = Geometry.GetLocalSize();
		FVector2D PixelSize;
		GEngine->GameViewport->GetViewportSize(PixelSize);
		if (LocalSize.X <= 0 || LocalSize.Y <= 0 || PixelSize.X <= 0 || PixelSize.Y <= 0) return -1;
		const FVector2D Local = Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
		if (Local.X < 0 || Local.Y < 0 || Local.X > LocalSize.X || Local.Y > LocalSize.Y) return -1;
		MouseX = static_cast<float>(Local.X * PixelSize.X / LocalSize.X);
		MouseY = static_cast<float>(Local.Y * PixelSize.Y / LocalSize.Y);
	}

	return UIManager->GetFrameManager()->HitTestFrames(MouseX, MouseY);
}

void AWowGameplayController::CreateSoftwareCursor()
{
	if (!GEngine || !GEngine->GameViewport || SoftwareCursorViewportWidget.IsValid())
	{
		return;
	}

	TSharedRef<SWidget> CursorWidget =
		SAssignNew(SoftwareCursorViewportWidget, SOverlay)
		.Visibility(EVisibility::HitTestInvisible)
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			SAssignNew(SoftwareCursorImage, SImage)
			.Image(&SoftwareCursorBrush)
			.Visibility(EVisibility::Collapsed)
			.RenderTransformPivot(FVector2D::ZeroVector)
		];

	FWowUiSurfaces::Add(CursorWidget, EWowUiZone::Cursor, 50000);
	// The held item icon is drawn by the software cursor itself (UpdateSoftwareCursor);
	// SWowCursorItemIcon depended on Slate ticking it and never showed in game.
}

void AWowGameplayController::RefreshSoftwareCursorBrush()
{
	if (!CursorManager)
	{
		return;
	}

	UTexture2D* CursorTexture = CursorManager->GetCursorTexture(CursorManager->GetCursorType());
	if (!CursorTexture)
	{
		return;
	}

	SoftwareCursorBrush = FSlateBrush();
	SoftwareCursorBrush.SetResourceObject(CursorTexture);
	SoftwareCursorBrush.ImageSize = FVector2D(38.0f, 38.0f);
	SoftwareCursorBrush.DrawAs = ESlateBrushDrawType::Image;

	if (SoftwareCursorImage.IsValid())
	{
		SoftwareCursorImage->SetImage(&SoftwareCursorBrush);
	}
	LastSoftwareCursorType = CursorManager->GetCursorType();
}

void AWowGameplayController::UpdateSoftwareCursor(float DeltaTime)
{
    // r351: a cursor set chosen under Interface > Mouse > Experimental is loaded right away.
    // r352: sets can have a folder per race, so the character's race is passed on.
    if (CursorManager)
    {
        if (ConnectionManager)
            if (const FWowPlayerEntity* CursorOwner = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer())
                CursorManager->SetPlayerRace(CursorOwner->GetRaceId());
        if (CursorManager->NeedsReload())
        {
            CursorManager->LoadCursors();
            RefreshSoftwareCursorBrush();
        }
        // r352: an animated set cursor (.ani) shows its current frame.
        else if (CursorManager->IsAnimated(CursorManager->GetCursorType()) && SoftwareCursorImage.IsValid())
        {
            UTexture2D* FrameTexture = CursorManager->GetCursorTexture(CursorManager->GetCursorType());
            if (FrameTexture && SoftwareCursorBrush.GetResourceObject() != FrameTexture)
            {
                SoftwareCursorBrush.SetResourceObject(FrameTexture);
                SoftwareCursorImage->SetImage(&SoftwareCursorBrush);
            }
        }
    }
    // This flag describes modal screen ownership. Login/character screens
    // still display the same WoW cursor, using the user's selected backend.
    const bool bCursorAllowed = !bSoftwareCursorSuppressed || bShowNativeCursorWhileSoftwareSuppressed;
    if (!bCursorAllowed)
    {
        if (SoftwareCursorImage.IsValid()) SoftwareCursorImage->SetVisibility(EVisibility::Collapsed);
        bShowMouseCursor = false;
        SetNativePlatformCursorVisible(false);
        DefaultMouseCursor = CurrentMouseCursor = EMouseCursor::None;
        return;
    }
    if (CursorManager && !CursorManager->HasOriginalCursors())
    {
        CursorAssetRetrySeconds -= DeltaTime;
        if (CursorAssetRetrySeconds <= 0.0f)
        {
            CursorAssetRetrySeconds = 0.5f;
            CacheWorldResources();
            if (CachedMpq && CachedMpq->IsInitialized())
            {
                CursorManager->Initialize(CachedMpq, CachedAssetCache);
                CursorManager->LoadCursors();
                RefreshSoftwareCursorBrush();
            }
        }
    }
    if (!SoftwareCursorImage.IsValid()) CreateSoftwareCursor();
    if (!SoftwareCursorImage.IsValid()) return;

	if (CursorManager)
	{
        EWowCursorType DesiredCursor = EWowCursorType::Point;
        if (!bSoftwareCursorSuppressed && ConnectionManager && ConnectionManager->PendingTargetSpell)
            { DesiredCursor=EWowCursorType::Cast; HoveredCursorGuid=0; }
        else if (!bSoftwareCursorSuppressed && ConnectionManager && ConnectionManager->HasCursorPayload())
            { DesiredCursor = EWowCursorType::Pickup; HoveredCursorGuid = 0; }
        else if (!bSoftwareCursorSuppressed && !bRightMouseLooking && !bLeftMouseLooking && !IsNativeModalUiVisible() && !IsPointerOverNativeGameUi() && GetUiFrameUnderCursor()<0)
        {
            CursorHoverAccumulator += DeltaTime;
            if (CursorHoverAccumulator >= 0.05f)
            {
                CursorHoverAccumulator = 0.0f;
                HoveredCursorGuid = 0;
                float X=0, Y=0; FVector Origin, Direction;
                if (GetWorld() && GetWorldPointerRay(Origin,Direction))
                {
                    FHitResult Hit; FCollisionQueryParams Query; Query.AddIgnoredActor(GetPawn());
                    // r273 (Stefan 13.10: his own unit tooltip stayed on screen while he walked, the pointer
                    // on the road ahead): actors attached to or owned by the pawn (weapons, aura and spell
                    // visuals around the player) were hit and matched the player through their owner.
                    if (APawn* HoverPawn = GetPawn())
                    {
                        TArray<AActor*> PawnActors;
                        HoverPawn->GetAttachedActors(PawnActors, true, true);
                        Query.AddIgnoredActors(PawnActors);
                        for (AActor* Owned : HoverPawn->Children) if (Owned) Query.AddIgnoredActor(Owned);
                    }
                    FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_Pawn); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
                    if (WowTraceIgnoringWater(GetWorld(),Hit,Origin,Origin+Direction*100000.0f,Objects,Query)
                        && (WowPreferCentredUnit(GetWorld(),Hit,Origin,Origin+Direction*100000.0f,Objects,Query), true)
                        && !IsWowWorldOccluded(GetWorld(), Origin, Hit.ImpactPoint, GetPawn(), Hit.GetActor()))
                        for (const auto& Pair : SpawnedEntityActors)
                            if (Pair.Value && (Pair.Value==Hit.GetActor() || (Hit.GetActor() && Pair.Value==Hit.GetActor()->GetOwner())))
                            { HoveredCursorGuid=Pair.Key; break; }
                    if (!HoveredCursorGuid)
                    {
                        FHitResult Ground;
                        const float Limit = GetWorld()->LineTraceSingleByChannel(Ground,Origin,Origin+Direction*100000.0f,ECC_Visibility,Query)
                            ? FVector::Distance(Origin, Ground.ImpactPoint) + 60.f : 100000.f;
                        HoveredCursorGuid = FindCorpseAlongRay(Origin, Direction, Limit);
                    }
                }
            }
            const auto* Hovered = ConnectionManager ? ConnectionManager->PacketHandler.EntityManager.Find(HoveredCursorGuid) : nullptr;
            if (Hovered && Hovered->IsGameObject())
            {
                const auto& Object=static_cast<const FWowGameObjectEntity&>(*Hovered);
                if (Object.CanInteractNow()) DesiredCursor=EWowCursorType::Interact;
            }
            if (Hovered && Hovered->IsUnit())
            {
                const auto& Unit = static_cast<const FWowUnitEntity&>(*Hovered);
                if (DeadEntityGuids.Contains(Unit.Guid) || (Unit.GetMaxHealth()>0 && Unit.GetHealth()<=0))
                    DesiredCursor = Unit.IsLootable() ? EWowCursorType::Pickup : EWowCursorType::Point;
                else if (IsWorldUnitAttackable(Unit))
                {
                    // r353: cursor sets can have a target cursor per reaction (red / yellow / green); the
                    // client's own sword is the same for all three.
                    const auto* LocalForReaction = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
                    const int32 CursorReaction = LocalForReaction ? ConnectionManager->GetUnitReaction(&Unit, LocalForReaction) : 0;
                    DesiredCursor = CursorReaction == 4 ? EWowCursorType::AttackNeutral
                        : CursorReaction > 4 ? EWowCursorType::AttackFriendly : EWowCursorType::Attack;
                }
                // Friendly players keep the normal pointer (3.3.5); the inspect glass is not shown.
                else if (Unit.IsPlayer()) DesiredCursor = EWowCursorType::Point;
                else if (Unit.IsVendor()) DesiredCursor = EWowCursorType::Buy;
                else if (Unit.IsQuestGiver())
                {
                    // QuestGiverStatus: hand-in (yellow/blue ?) beats available (!); only an
                    // incomplete quest leaves the plain speak bubble.
                    const uint8 Status = ConnectionManager->PacketHandler.QuestGiverStatuses.FindRef(Unit.Guid);
                    DesiredCursor = (Status == 3 || Status == 6 || Status == 9 || Status == 10) ? EWowCursorType::QuestTurnIn
                        : (Status == 2 || Status == 4 || Status == 7 || Status == 8) ? EWowCursorType::Quest
                        : EWowCursorType::Speak;
                }
                else if (Unit.IsInteractable()) DesiredCursor = EWowCursorType::Speak;
            }
            // r333 (Stefan 14.10: Felix's Chest showed the gear cursor from far away, the click only
            // said "Out of range"): like the client, the cursor goes grey ("Unable...") while the
            // thing is out of reach. The server's reach is INTERACTION_DISTANCE (5 yards) between
            // the two bodies: plus both combat reaches for a unit, plus ours (and a little for the
            // object's own size) for a game object.
            const FWowEntity* MeForReach = Hovered && ConnectionManager ? ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
            if (MeForReach && DesiredCursor != EWowCursorType::Point && DesiredCursor != EWowCursorType::Attack
                && DesiredCursor != EWowCursorType::AttackNeutral && DesiredCursor != EWowCursorType::AttackFriendly)
            {
                auto Reach = [](const FWowEntity& Entity)
                {
                    if (!Entity.IsUnit()) return 1.0f;
                    const float Value = Entity.GetFieldFloat(UnitField::DISPLAYID - 1); // UNIT_FIELD_COMBATREACH
                    return Value > 0.1f && Value < 20.0f ? Value : 1.5f;
                };
                const float Allowed = 5.0f + Reach(*MeForReach) + Reach(*Hovered);
                const float Distance = float(FVector::Distance(FVector(MeForReach->Movement.Position), FVector(Hovered->Movement.Position)));
                if (Distance > Allowed)
                {
                    switch (DesiredCursor)
                    {
                    case EWowCursorType::Interact: DesiredCursor = EWowCursorType::UnableInteract; break;
                    case EWowCursorType::Speak: DesiredCursor = EWowCursorType::UnableSpeak; break;
                    case EWowCursorType::Buy: DesiredCursor = EWowCursorType::UnableBuy; break;
                    case EWowCursorType::Quest: DesiredCursor = EWowCursorType::UnableQuest; break;
                    case EWowCursorType::QuestTurnIn: DesiredCursor = EWowCursorType::UnableQuestTurnIn; break;
                    case EWowCursorType::Pickup: DesiredCursor = EWowCursorType::UnablePickup; break;
                    default: break;
                    }
                }
            }
        }
        else { HoveredCursorGuid=0; CursorHoverAccumulator=0.05f; }

        // Pointer moved onto a native window (inspect, quest log, auction house ...): the world
        // unit's tooltip goes at once instead of fading. GameTooltip is drawn by FrameXML, under the
        // native windows, so it showed behind the window (Stefan 10.10).
        if ((bGWowWorldTooltipShown || GWowWorldTooltipFadeStart > 0.0) && UIManager && UIManager->GetEventSystem()
            && IsPointerOverNativeGameUi())
        {
            UIManager->GetEventSystem()->ExecuteLuaString(
                TEXT("if GameTooltip:GetOwner() == UIParent then GameTooltip:Hide() end GameTooltip:SetAlpha(1)"), TEXT("=WorldMouseover"));
            bGWowWorldTooltipShown = false;
            GWowWorldTooltipFadeStart = 0.0;
            GWowWorldTooltipGuid = 0;
            UIManager->SetMouseoverGuid(0);
        }

        // World mouseover tooltip, as the client does natively: units get the full
        // GameTooltip:SetUnit("mouseover") tooltip, game objects (herbs, ore, chests) their name.
        // Mouse-look (either button held): no world tooltip, as in the client - it stayed and followed
        // the turning camera (Stefan 10.10). It comes back for whatever is under the pointer after.
        if ((bRightMouseLooking || bLeftMouseLooking) && (bGWowWorldTooltipShown || GWowWorldTooltipFadeStart > 0.0)
            && UIManager && UIManager->GetEventSystem())
        {
            UIManager->GetEventSystem()->ExecuteLuaString(
                TEXT("if GameTooltip:GetOwner() == UIParent then GameTooltip:Hide() end GameTooltip:SetAlpha(1)"), TEXT("=WorldMouseover"));
            bGWowWorldTooltipShown = false;
            GWowWorldTooltipFadeStart = 0.0;
            GWowWorldTooltipGuid = 0;
        }
        if (HoveredCursorGuid != GWowWorldTooltipGuid && !bRightMouseLooking && !bLeftMouseLooking && UIManager && UIManager->GetEventSystem())
        {
            GWowWorldTooltipGuid = HoveredCursorGuid;
            const FWowEntity* Hovered = HoveredCursorGuid && ConnectionManager
                ? ConnectionManager->PacketHandler.EntityManager.Find(HoveredCursorGuid) : nullptr;
            UIManager->SetMouseoverGuid(Hovered && Hovered->IsUnit() ? HoveredCursorGuid : 0);
            FString Lua;
            if (Hovered && Hovered->IsUnit())
            {
                Lua = TEXT("GameTooltip_SetDefaultAnchor(GameTooltip, UIParent) GameTooltip:SetUnit('mouseover') GameTooltip:Show()");
            }
            else if (Hovered && Hovered->IsGameObject())
            {
                auto& Handler = ConnectionManager->PacketHandler;
                if (const FString* Name = Handler.GameObjectNames.Find(Hovered->Entry))
                {
                    const FString Escaped = Name->Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
                    Lua = FString::Printf(TEXT("GameTooltip_SetDefaultAnchor(GameTooltip, UIParent) GameTooltip:SetText(\"%s\", 1, 1, 1)"), *Escaped);
                    // Gathering nodes: the lock's skill (Lock.dbc LOCK_KEY_SKILL) as "Requires Herbalism".
                    if (const uint32* LockId = Handler.GameObjectLocks.Find(Hovered->Entry); LockId && *LockId)
                        if (const auto* Cases = FDbcStore::Get().Spells().Locks.Find(*LockId))
                            for (const TPair<uint32, uint32>& Case : *Cases)
                                if (Case.Key == 2)
                                {
                                    const TCHAR* Skill = Case.Value == 1 ? TEXT("Lockpicking") : Case.Value == 2 ? TEXT("Herbalism")
                                        : Case.Value == 3 ? TEXT("Mining") : Case.Value == 20 ? TEXT("Inscription") : nullptr;
                                    if (Skill) Lua += FString::Printf(TEXT(" GameTooltip:AddLine(\"Requires %s\", 1, 1, 1)"), Skill);
                                    break;
                                }
                    Lua += TEXT(" GameTooltip:Show()");
                }
                else
                {
                    // Name not known yet: query the template once and retry until it arrives.
                    static TSet<uint32> QueriedObjects;
                    if (!QueriedObjects.Contains(Hovered->Entry))
                    {
                        QueriedObjects.Add(Hovered->Entry);
                        TArray<uint8> Query;
                        const uint32 Entry = Hovered->Entry; const uint64 Guid = Hovered->Guid;
                        Query.Append(reinterpret_cast<const uint8*>(&Entry), 4);
                        Query.Append(reinterpret_cast<const uint8*>(&Guid), 8);
                        ConnectionManager->SendRawPacket(WowOpcode::CMSG_GAMEOBJECT_QUERY, Query);
                    }
                    GWowWorldTooltipGuid = 0;
                }
            }
            if (!Lua.IsEmpty())
            {
                if (GWowWorldTooltipFadeStart > 0.0) Lua = TEXT("GameTooltip:SetAlpha(1) ") + Lua;
                GWowWorldTooltipFadeStart = 0.0;
                UIManager->GetEventSystem()->ExecuteLuaString(Lua, TEXT("=WorldMouseover"));
                bGWowWorldTooltipShown = true;
            }
            else if (bGWowWorldTooltipShown)
            {
                // Like GameTooltip:FadeOut(): the unit tooltip stays a moment and fades, so a
                // moving mob's tooltip can still be read (Stefan's request).
                GWowWorldTooltipFadeStart = FPlatformTime::Seconds();
                bGWowWorldTooltipShown = false;
            }
        }
        if (GWowWorldTooltipFadeStart > 0.0 && UIManager && UIManager->GetEventSystem())
        {
            // Stefan: the unit tooltip should stay a little longer and fade slower (moving mobs).
            // How long it stays is Options > Interface > Tooltips > Tooltip Fade Time (0 = at once).
            // r403 (Stefan 16.10: set to 0.1 it still took a second or two): the setting is the whole
            // time the tooltip takes to go - it stays, then fades over the last part (at most 0.4 s);
            // the fade was a fixed extra second on top.
            const double FadeTotal = FWowClientPreferences::GetTooltipFadeTime();
            const double Fade = FMath::Min(FadeTotal, 0.4);
            const double Hold = FadeTotal - Fade;
            const double Age = FPlatformTime::Seconds() - GWowWorldTooltipFadeStart;
            // Only our own world tooltip: a UI frame may already own GameTooltip.
            if (Age >= Hold + Fade)
            {
                UIManager->GetEventSystem()->ExecuteLuaString(
                    TEXT("if GameTooltip:GetOwner() == UIParent then GameTooltip:Hide() end GameTooltip:SetAlpha(1)"), TEXT("=WorldMouseover"));
                GWowWorldTooltipFadeStart = 0.0;
            }
            else if (Age > Hold)
            {
                UIManager->GetEventSystem()->ExecuteLuaString(FString::Printf(
                    TEXT("if GameTooltip:GetOwner() == UIParent then GameTooltip:SetAlpha(%.2f) else GameTooltip:SetAlpha(1) end"),
                    1.0 - (Age - Hold) / Fade), TEXT("=WorldMouseover"));
            }
        }

		if (CursorManager->GetCursorType() != DesiredCursor)
		{
			CursorManager->SetCursorType(DesiredCursor);
		}
		if (CursorManager->GetCursorType() != LastSoftwareCursorType)
		{
			RefreshSoftwareCursorBrush();
		}
	}
	if (!SoftwareCursorBrush.GetResourceObject())
	{
		RefreshSoftwareCursorBrush();
		if (!SoftwareCursorBrush.GetResourceObject())
		{
			SoftwareCursorImage->SetVisibility(EVisibility::Collapsed);
			return;
		}
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	float CursorViewportWidth = 0.0f;
	float CursorViewportHeight = 0.0f;
	bool bHasMousePosition = false;
	if (FSlateApplication::IsInitialized() && GEngine && GEngine->GameViewport)
	{
		const TSharedPtr<SWidget> ViewportWidget = SoftwareCursorViewportWidget;
		if (ViewportWidget.IsValid())
		{
			// Use the actual drawing parent's geometry, including the game-layer
			// DPI scaler, window origin and current window size.
			const FGeometry& ViewportGeometry = ViewportWidget->GetCachedGeometry();
			FVector2D LocalMousePosition = ViewportGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
			const FVector2D LocalViewportSize = ViewportGeometry.GetLocalSize();
			// VR: the cursor layer is in the headset UI's off-screen window, whose geometry has
			// nothing to do with the desktop. Map the pointer through the game viewport instead
			// (the same mapping the VR host uses for clicks).
			if (FWowUiVrHost::IsActive())
				if (const TSharedPtr<SViewport> GameViewportWidget = GEngine->GameViewport->GetGameViewportWidget())
				{
					const FGeometry& Flat = GameViewportWidget->GetCachedGeometry();
					const FVector2D FlatSize = Flat.GetLocalSize();
					if (FlatSize.X > 0 && FlatSize.Y > 0)
						LocalMousePosition = Flat.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos()) / FlatSize * LocalViewportSize;
				}
			MouseX = LocalMousePosition.X;
			MouseY = LocalMousePosition.Y;
			CursorViewportWidth = LocalViewportSize.X;
			CursorViewportHeight = LocalViewportSize.Y;
			bHasMousePosition = CursorViewportWidth > 0.0f && CursorViewportHeight > 0.0f;
		}
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	GetViewportSize(ViewportWidth, ViewportHeight);
	if (!bHasMousePosition)
	{
		bHasMousePosition = GetMousePosition(MouseX, MouseY);
		CursorViewportWidth = static_cast<float>(ViewportWidth);
		CursorViewportHeight = static_cast<float>(ViewportHeight);
	}
	if (!bHasMousePosition && ViewportWidth > 0 && ViewportHeight > 0)
	{
		MouseX = static_cast<float>(ViewportWidth) * 0.5f;
		MouseY = static_cast<float>(ViewportHeight) * 0.5f;
		CursorViewportWidth = static_cast<float>(ViewportWidth);
		CursorViewportHeight = static_cast<float>(ViewportHeight);
		bHasMousePosition = true;
	}
	if (!bHasMousePosition)
	{
		SoftwareCursorImage->SetVisibility(EVisibility::Collapsed);
		const bool bShouldUseNativeCursor = !bRightMouseLooking && !bLeftMouseLooking;
		SetNativePlatformCursorVisible(bShouldUseNativeCursor);
		DefaultMouseCursor = bShouldUseNativeCursor ? EMouseCursor::Default : EMouseCursor::None;
		CurrentMouseCursor = DefaultMouseCursor;
		return;
	}

    // Offscreen presentation fixture: paint at a fixed local position without
    // moving the user's OS pointer. Only enabled explicitly in rendered tests.
    static const bool bCursorPreview = FParse::Param(FCommandLine::Get(), TEXT("cursorpreview"))
        && FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen"));
    if (bCursorPreview) { MouseX = CursorViewportWidth * 0.72f; MouseY = CursorViewportHeight * 0.65f; }
	// Holding an item: the item icon itself is the pointer, as in the original client
	// (top-left corner on the hotspot), instead of the pickup bag. Falls back to the
	// pickup cursor while the icon can't be loaded.
	// Spells too (from the spellbook or picked off a bar): their icon, not the pickup bag
	// (Stefan 10.10). Key: item entry, or the spell id with the top bit set.
	uint32 HeldEntry = 0;
	if (ConnectionManager && ConnectionManager->HasCursorPayload() && !bSoftwareCursorSuppressed)
	{
		FString HeldType, HeldDetail; int32 HeldId = 0;
		if (ConnectionManager->GetCursorInfo(HeldType, HeldId, HeldDetail) && HeldId > 0)
		{
			if (HeldType == TEXT("item")) HeldEntry = static_cast<uint32>(HeldId);
			else if (HeldType == TEXT("spell")) HeldEntry = 0x80000000u | static_cast<uint32>(HeldId);
			// r309 (Stefan 13.10: dragging a macro showed the pickup bag): a macro is its icon too,
			// from the macro window or off a bar. Key: macro index with bit 30 set.
			else if (HeldType == TEXT("macro")) HeldEntry = 0x40000000u | static_cast<uint32>(HeldId);
			else if (HeldType == TEXT("action"))
			{
				const TArray<uint32>& Buttons = ConnectionManager->PacketHandler.ActionButtons;
				const uint32 Action = Buttons.IsValidIndex(HeldId - 1) ? Buttons[HeldId - 1] : 0u;
				const uint8 ActionType = static_cast<uint8>((Action >> 24) & 0xFF);
				const uint32 ActionId = Action & 0x00FFFFFFu;
				if (ActionId && ActionType == 0) HeldEntry = 0x80000000u | ActionId;
				else if (ActionId && ActionType == 128) HeldEntry = ActionId;
				else if (ActionId && ActionType == 64) HeldEntry = 0x40000000u | ActionId;
			}
		}
	}
	if (HeldEntry != GWowHeldIconEntry)
	{
		GWowHeldIconEntry = HeldEntry;
		GWowHeldIconTexture.Reset();
		if (HeldEntry && UIManager && UIManager->GetFrameManager())
		{
			FString IconPath;
			if ((HeldEntry & 0xC0000000u) == 0x40000000u)
			{
				if (const FWowMacro* Macro = FWowMacros::Get(static_cast<int32>(HeldEntry & 0x3FFFFFFFu)))
					IconPath = FWowMacros::DisplayIcon(*Macro, ConnectionManager.Get());
			}
			else if (HeldEntry & 0x80000000u)
			{
				if (const FSpellDbcEntry* Spell = FDbcStore::Get().Spells().GetById(HeldEntry & 0x7FFFFFFFu))
					if (const FSpellIconDbcEntry* Icon = FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID))
						IconPath = Icon->TexturePath;
			}
			else
			{
				const FWowItemTemplateInfo* HeldInfo = ConnectionManager->PacketHandler.ItemTemplates.Find(HeldEntry);
				IconPath = FWowItemPresentation::IconPath(HeldEntry, HeldInfo ? HeldInfo->DisplayId : 0);
			}
			if (!IconPath.IsEmpty())
				GWowHeldIconTexture = UIManager->GetFrameManager()->LoadUITexture(IconPath);
		}
	}
	const bool bIconCursor = HeldEntry && GWowHeldIconTexture.IsValid();
	if (bIconCursor && SoftwareCursorBrush.GetResourceObject() != GWowHeldIconTexture.Get())
	{
		SoftwareCursorBrush.SetResourceObject(GWowHeldIconTexture.Get());
		SoftwareCursorBrush.ImageSize = FVector2D::ZeroVector; // re-applied below
	}
	else if (!bIconCursor && bGWowHeldIconCursor)
	{
		RefreshSoftwareCursorBrush();
		SoftwareCursorBrush.ImageSize = FVector2D::ZeroVector;
	}
	bGWowHeldIconCursor = bIconCursor;
	const bool bShouldShow = !bRightMouseLooking && !bLeftMouseLooking;
    const float LayoutScale = SoftwareCursorViewportWidget.IsValid()
        ? FMath::Max(.01f, SoftwareCursorViewportWidget->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale()) : 1.0f;
    // Cursor size is in physical pixels, independent of UI and desktop DPI.
    // VR: the pointer sits on panels ~1 m away and was impossible to find at monitor size.
    // r248 (Stefan 12.10: at 5x the hand covered the loot window's item names): 3x.
    const float UserScale = FWowClientPreferences::GetMouseCursorScale() * (FWowUiVrHost::IsActive() ? 3.f : 1.f);
    const float LogicalScale = UserScale / LayoutScale;
    const float CursorPixels = bIconCursor ? 32.0f : 38.0f;
    const FVector2D CursorSize(CursorPixels * LogicalScale, CursorPixels * LogicalScale);
    if (!FVector2D(SoftwareCursorBrush.ImageSize).Equals(CursorSize, .01f))
    {
        SoftwareCursorBrush.ImageSize = CursorSize;
        SoftwareCursorImage->SetImage(&SoftwareCursorBrush);
        SoftwareCursorImage->Invalidate(EInvalidateWidgetReason::Layout);
    }
    // Point art: the fingertip is pixel (7,5) of 64 = (4,3) at 38 px (matches WowCursorManager).
    // r351: a cursor set's own click point (UWowCursorManager::GetHotspot).
    const FVector2D CursorHotspot = bIconCursor ? FVector2D(2.0f, 2.0f) * LogicalScale
        : (CursorManager ? CursorManager->GetHotspot(CursorManager->GetCursorType()) * CursorPixels * LogicalScale : FVector2D(4.0f, 3.0f) * LogicalScale);
	FVector2D CursorDrawPosition(MouseX - CursorHotspot.X, MouseY - CursorHotspot.Y);
	const bool bHasViewportBounds = CursorViewportWidth > 0.0f && CursorViewportHeight > 0.0f;
	const bool bCursorInsideViewport = !bHasViewportBounds
		|| (MouseX >= 0.0f && MouseY >= 0.0f
			&& MouseX <= CursorViewportWidth
			&& MouseY <= CursorViewportHeight);
    // The held item icon is always drawn in software (the hardware cursor is the cursor art).
    // VR: a hardware cursor only exists on the monitor; the headset needs the drawn one.
    const bool bHardware = !bIconCursor && CursorManager && FWowClientPreferences::GetCursorMode() == 0 && !FWowUiVrHost::IsActive()
        && CursorManager->ApplyHardwareCursor(FMath::Clamp(FMath::RoundToInt(38.0f * UserScale), 16, 128));
    if (CursorManager && FWowClientPreferences::GetCursorMode() == 1) CursorManager->ReleaseHardwareCursors();
    const bool bUseNativeCursor = bShouldShow && (bHardware || !bCursorInsideViewport);
	SetNativePlatformCursorVisible(bUseNativeCursor);
	DefaultMouseCursor = bUseNativeCursor ? EMouseCursor::Default : EMouseCursor::None;
	CurrentMouseCursor = DefaultMouseCursor;
	if (bHasViewportBounds)
	{
		// Keep the cursor hotspot at the real pointer position. The image is
		// intentionally allowed to extend beyond the viewport edge, just like the
		// native OS cursor; clamping the complete image made the pointer appear to
		// stop roughly one icon-height above the action bar and bottom controls.
		const float ClampedMouseX = FMath::Clamp(
			MouseX, 0.0f, FMath::Max(0.0f, CursorViewportWidth - 1.0f));
		const float ClampedMouseY = FMath::Clamp(
			MouseY, 0.0f, FMath::Max(0.0f, CursorViewportHeight - 1.0f));
		CursorDrawPosition = FVector2D(
			ClampedMouseX - CursorHotspot.X, ClampedMouseY - CursorHotspot.Y);
	}
	SoftwareCursorImage->SetVisibility((bShouldShow && bCursorInsideViewport && !bHardware) ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	SoftwareCursorImage->SetRenderTransform(FSlateRenderTransform(CursorDrawPosition));
}

void AWowGameplayController::ApplyMouseLookDelta()
{
	const float AxisMouseDeltaX = PendingMouseTurn;
	const float AxisMouseDeltaY = PendingMouseLookUp;
	PendingMouseTurn = 0.0f;
	PendingMouseLookUp = 0.0f;

	if (bIgnoreNextMouseLookDelta)
	{
		bIgnoreNextMouseLookDelta = false;
		return;
	}

	float RawMouseDeltaX = 0.0f;
	float RawMouseDeltaY = 0.0f;
	GetInputMouseDelta(RawMouseDeltaX, RawMouseDeltaY);

	float MouseDeltaX = AxisMouseDeltaX;
	float MouseDeltaY = AxisMouseDeltaY;
	if (!FMath::IsNearlyZero(RawMouseDeltaX) || !FMath::IsNearlyZero(RawMouseDeltaY))
	{
		if (FMath::IsNearlyZero(MouseDeltaX))
		{
			MouseDeltaX = RawMouseDeltaX;
		}
		if (FMath::IsNearlyZero(MouseDeltaY))
		{
			MouseDeltaY = RawMouseDeltaY;
		}
	}

	if (FMath::IsNearlyZero(MouseDeltaX) && FMath::IsNearlyZero(MouseDeltaY))
	{
		return;
	}

	MouseDeltaX = FMath::Clamp(MouseDeltaX, -80.0f, 80.0f);
	MouseDeltaY = FMath::Clamp(MouseDeltaY, -80.0f, 80.0f);

	EnsureGameplayCameraRotationInitialized();
	GameplayCameraYawDegrees = FRotator::NormalizeAxis(
		GameplayCameraYawDegrees + MouseDeltaX * MouseLookYawSensitivity * FWowClientPreferences::GetCameraMouseSensitivity());
	// Raw mouse Y is delivered with the opposite sign from screen-space Y here.
	// Add it so WoW-style mouse-look moves vertically in the expected direction.
	GameplayCameraPitchDegrees = FMath::Clamp(
		GameplayCameraPitchDegrees + (MouseDeltaY * MouseLookPitchSensitivity * FWowClientPreferences::GetCameraMouseSensitivity()),
		MinMouseLookPitch,
		MaxMouseLookPitch);
	ApplyGameplayCameraRotation();

	// Sitting: right-drag only orbits the camera like left-drag. Turning the seated body
	// slid it round on the spot.
	if (bRightMouseLooking && !bLocalSitting)
	{
		DesiredFacingYawDegrees = GameplayCameraYawDegrees;
		bHasDesiredFacingYaw = true;
		if (APawn* ControlledPawn = GetPawn())
		{
			if (AWowPlayerCharacter* TurningCharacter = Cast<AWowPlayerCharacter>(ControlledPawn))
			    if (TurningCharacter->AnimationController)
			        TurningCharacter->AnimationController->NotifyLocalTurn(FMath::FindDeltaAngleDegrees(
			            ControlledPawn->GetActorRotation().Yaw, GameplayCameraYawDegrees));
			ControlledPawn->SetActorRotation(FRotator(0.0f, GameplayCameraYawDegrees, 0.0f));
		}
	}
}

void AWowGameplayController::EnsureGameplayCameraRotationInitialized()
{
	if (bHasGameplayCameraRotation)
	{
		return;
	}

	const FRotator CurrentRotation = GetControlRotation();
	GameplayCameraYawDegrees = CurrentRotation.Yaw;
	GameplayCameraPitchDegrees = FMath::Clamp(CurrentRotation.Pitch, MinMouseLookPitch, MaxMouseLookPitch);
	bHasGameplayCameraRotation = true;
}

void AWowGameplayController::ApplyGameplayCameraRotation()
{
	if (!bHasGameplayCameraRotation)
	{
		return;
	}

    if(!bRightMouseLooking || bLocalSitting)
        if(const auto* ViewPawn=Cast<AWowPlayerCharacter>(GetPawn()); ViewPawn && ViewPawn->CameraBoom && ViewPawn->CameraBoom->TargetArmLength<=20.f)
            GameplayCameraYawDegrees=FWowFirstPersonCameraPolicy::ClampHeadYaw(GameplayCameraYawDegrees,ViewPawn->GetActorRotation().Yaw);
	SetControlRotation(FRotator(GameplayCameraPitchDegrees, GameplayCameraYawDegrees, 0.0f));
}

void AWowGameplayController::PrimeMouseLookAnchor()
{
	PendingMouseTurn = 0.0f;
	PendingMouseLookUp = 0.0f;
	bHasLastMouseLookPosition = false;
	bHasMouseLookAnchor = false;
}

void AWowGameplayController::CenterMouseLookCursor()
{
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	GetViewportSize(ViewportWidth, ViewportHeight);
	if (ViewportWidth <= 0 || ViewportHeight <= 0)
	{
		return;
	}

	SetMouseLocation(ViewportWidth / 2, ViewportHeight / 2);
	// Discard only the synthetic delta generated by centering. Subsequent input
	// is relative while the viewport owns the pointer.
	bIgnoreNextMouseLookDelta = true;
}

void AWowGameplayController::AccumulateMouseLookDelta(float DeltaYaw, float DeltaPitch)
{
	if (bLeftMouseLooking || bRightMouseLooking)
	{
		PendingMouseTurn += DeltaYaw;
		PendingMouseLookUp += DeltaPitch;
	}
}

void AWowGameplayController::OnMouseTurn(float Value)
{
	AccumulateMouseLookDelta(Value, 0.0f);
}

void AWowGameplayController::OnMouseLookUp(float Value)
{
	AccumulateMouseLookDelta(0.0f, Value);
}

bool AWowGameplayController::IsMouseButtonActuallyDown(const FKey& Button) const
{
	bool bPhysicalButtonDown = false;
	if (TryGetPhysicalInputDown(Button, bPhysicalButtonDown))
	{
		return bPhysicalButtonDown;
	}

	const bool bControllerSaysDown = IsInputKeyDown(Button);
	if (bLeftMouseLooking || bRightMouseLooking)
	{
		// During captured mouse-look, Slate's pressed-button set can get stale if
		// the viewport changes focus. Trust the controller key state so a missed
		// release cannot leave mouse-look permanently stuck.
		return bControllerSaysDown;
	}

	if (FSlateApplication::IsInitialized())
	{
		if (FSlateApplication::Get().GetPressedMouseButtons().Contains(Button))
		{
			return true;
		}
	}

	return bControllerSaysDown;
}

void AWowGameplayController::UpdateMouseLookReleaseGuards()
{
	constexpr float MinimumLookAgeSeconds = 0.05f;
	constexpr int32 RequiredMissingFrames = 2;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	if (bRightMouseLooking)
	{
		const bool bOldEnough = (Now - RightMouseLookStartedAt) >= MinimumLookAgeSeconds;
		if (bOldEnough && !IsMouseButtonActuallyDown(EKeys::RightMouseButton))
		{
			++RightMouseReleaseMissingFrames;
			if (RightMouseReleaseMissingFrames >= RequiredMissingFrames)
			{
				OnRightClickReleased();
			}
		}
		else
		{
			RightMouseReleaseMissingFrames = 0;
		}
	}
	else
	{
		RightMouseReleaseMissingFrames = 0;
	}

	if (bLeftMouseLooking)
	{
		const bool bOldEnough = (Now - LeftMouseLookStartedAt) >= MinimumLookAgeSeconds;
		if (bOldEnough && !IsMouseButtonActuallyDown(EKeys::LeftMouseButton))
		{
			++LeftMouseReleaseMissingFrames;
			if (LeftMouseReleaseMissingFrames >= RequiredMissingFrames)
			{
				OnLeftClickReleased();
			}
		}
		else
		{
			LeftMouseReleaseMissingFrames = 0;
		}
	}
	else
	{
		LeftMouseReleaseMissingFrames = 0;
	}
}

void AWowGameplayController::ForceReleaseMouseLookForFocusChange()
{
    FWowKeyBindings::ReleaseAll();
	// A mouse-up message is not guaranteed when Windows changes the foreground
	// application. Clear every part of the drag state without restoring focus or
	// moving the system pointer. Restoring the pre-drag position is correct for a
	// normal in-game button release, but on Alt-Tab it can run after another app
	// receives the click and visibly yank the pointer away from that click.

	bLeftMouseLooking = false;
	bRightMouseLooking = false;
	bLeftMouseWasDown = false;
	bRightMouseWasDown = false;
	bMouseChordMovingForward = false;
	bLeftMouseDragged = false;
	bHasLastMouseLookPosition = false;
	bHasMouseLookAnchor = false;
	bIgnoreNextMouseLookDelta = false;
	bHasSavedLeftMousePosition = false;
	bHasSavedRightMousePosition = false;
	LeftMouseReleaseMissingFrames = 0;
	RightMouseReleaseMissingFrames = 0;
	PendingMouseTurn = 0.0f;
	PendingMouseLookUp = 0.0f;
	PressedLeftMouseFrameHandle = -1;
	PressedRightMouseFrameHandle = -1;
	PendingActionDragSlot = -1;
	PendingUiDragFrame = -1;
	bUiDragActive = false;
	bMouseInputNeedsRearmAfterFocusChange = true;
	GWowReapplyInputModeAfterActivationClick = true;
	GWowActivationWorldClick = false;
	GWowActivationUiPress = false;

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}
	if (UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		ViewportClient->SetMouseLockMode(EMouseLockMode::DoNotLock);
		ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
	}
	if (UIManager && UIManager->GetFrameManager())
	{
		UIManager->GetFrameManager()->SetMouseInteractionSuppressed(false);
	}
	bShowMouseCursor = false;
	SetNativePlatformCursorVisible(true);
}

void AWowGameplayController::HandleApplicationWillDeactivate()
{
	ForceReleaseMouseLookForFocusChange();
}

void AWowGameplayController::HandleApplicationHasReactivated()
{
	// Restore the normal hybrid mode without recapturing. The activation click
	// remains ignored until Tick has observed both physical buttons released.
	bool bLeftDown = false;
	TryGetPhysicalInputDown(EKeys::LeftMouseButton, bLeftDown);
	const bool bOverUi = GetUiFrameUnderCursor() >= 0 || IsPointerOverNativeGameUi();
	UE_LOG(LogWowGameplay, Log, TEXT("Application reactivated: foreground=%d frame=%lld leftDown=%d overUi=%d"),
		IsWowClientProcessForeground() ? 1 : 0, GetUiFrameUnderCursor(), bLeftDown ? 1 : 0, bOverUi ? 1 : 0);
	bool bRightDown = false;
	TryGetPhysicalInputDown(EKeys::RightMouseButton, bRightDown);
	if (bLeftDown && bOverUi)
	{
		// The click that brought the window back is pressing a UI button right now.
		// SetInputMode releases Slate's mouse capture, which cancelled that button's
		// press (the "double click after Alt-Tab"). Let the click finish; Tick restores
		// the input mode once the button is up.
		bMouseInputNeedsRearmAfterFocusChange = false;
		GWowReapplyInputModeAfterActivationClick = true;
		// The viewport was left in NoCapture while inactive, which drops ordinary presses:
		// restore the capture mode (not SetInputMode) so the release arrives, and route the
		// press that already happened to the FrameXML button under the cursor.
		if (UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			ViewportClient->SetMouseLockMode(EMouseLockMode::DoNotLock);
			ViewportClient->SetMouseCaptureMode(WowMouseCapturePolicy::Resolve(false, false));
		}
		if (GetUiFrameUnderCursor() >= 0 && PressedLeftMouseFrameHandle < 0)
		{
			OnLeftClick();
			bLeftMouseWasDown = true;
		}
		bShowMouseCursor = !bSoftwareCursorSuppressed || bShowNativeCursorWhileSoftwareSuppressed;
		SetNativePlatformCursorVisible(bShowNativeCursorWhileSoftwareSuppressed);
		return;
	}
	bMouseInputNeedsRearmAfterFocusChange = true;
	if (bLeftDown || bRightDown)
	{
		// The activation click is held over the world: restore capture now so its events arrive,
		// and leave SetInputMode to Tick after the release (it would cancel the press).
		GWowReapplyInputModeAfterActivationClick = true;
		if (UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			ViewportClient->SetMouseLockMode(EMouseLockMode::DoNotLock);
			ViewportClient->SetMouseCaptureMode(WowMouseCapturePolicy::Resolve(false, false));
		}
	}
	else
	{
		GWowReapplyInputModeAfterActivationClick = false;
		ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
	}
	bShowMouseCursor = !bSoftwareCursorSuppressed || bShowNativeCursorWhileSoftwareSuppressed;
	SetNativePlatformCursorVisible(bShowNativeCursorWhileSoftwareSuppressed);
}

void AWowGameplayController::SyncCameraYawToPawnFacing()
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	EnsureGameplayCameraRotationInitialized();
	GameplayCameraYawDegrees = ControlledPawn->GetActorRotation().Yaw;
	GameplayCameraPitchDegrees = FMath::Clamp(GameplayCameraPitchDegrees, MinMouseLookPitch, MaxMouseLookPitch);
	ApplyGameplayCameraRotation();
	DesiredFacingYawDegrees = GameplayCameraYawDegrees;
	bHasDesiredFacingYaw = true;
}

void AWowGameplayController::ApplyKeyboardTurn(float DeltaTime)
{
	float Direction = 0.0f;
    const bool bLeftDown=FWowKeyBindings::Held(TEXT("TURNLEFT"));
    const bool bRightDown=FWowKeyBindings::Held(TEXT("TURNRIGHT"));
	if (bLeftDown)
	{
		Direction -= 1.0f;
	}
	if (bRightDown)
	{
		Direction += 1.0f;
	}
	// r610: in the isometric camera the turn keys move left/right on the screen instead.
	// r713 (Stefan: "Don't make anyone able to move or directional turn when sitting"): the turn keys do nothing
	// while seated.
	if (FMath::IsNearlyZero(Direction) || FWowIsometricCamera::IsActive() || bLocalSitting)
	{
		return;
	}

	EnsureGameplayCameraRotationInitialized();
	// Turn from the camera's current yaw instead of an older desired-facing value.
	// Otherwise Q/E can snap the view back after a camera-only orbit.
	const float BaseYaw = GameplayCameraYawDegrees;
	GameplayCameraYawDegrees = FRotator::NormalizeAxis(BaseYaw + Direction * KeyboardTurnRateDegrees * DeltaTime);
	ApplyGameplayCameraRotation();
	DesiredFacingYawDegrees = GameplayCameraYawDegrees;
	bHasDesiredFacingYaw = true;
	if (APawn* ControlledPawn = GetPawn())
	{
		if (AWowPlayerCharacter* TurningCharacter = Cast<AWowPlayerCharacter>(ControlledPawn))
		    if (TurningCharacter->AnimationController)
		        TurningCharacter->AnimationController->NotifyLocalTurn(FMath::FindDeltaAngleDegrees(
		            ControlledPawn->GetActorRotation().Yaw, GameplayCameraYawDegrees));
		ControlledPawn->SetActorRotation(FRotator(0.0f, GameplayCameraYawDegrees, 0.0f));
	}
	if (ConnectionManager)
	{
		const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
		if (FWowEntity* LocalEntity = ConnectionManager->PacketHandler.EntityManager.Find(LocalGuid))
		{
			LocalEntity->Movement.Orientation = FWowCoordinate::UEYawToWowOrientation(GameplayCameraYawDegrees);
		}
	}
}

void AWowGameplayController::OnTurnLeftPressed()
{
	bTurnLeftHeld = true;
}

void AWowGameplayController::OnTurnLeftReleased()
{
	bTurnLeftHeld = false;
}

void AWowGameplayController::OnTurnRightPressed()
{
	bTurnRightHeld = true;
}

void AWowGameplayController::OnTurnRightReleased()
{
	bTurnRightHeld = false;
}

void AWowGameplayController::ApplyLocalWeaponPresentation()
{
    if (!bLocalSheathStateInitialized) return;
    // Casting owns the temporary sheath until its restore path completes.
    if (ConnectionManager && SpellSheathedWeapons.Contains(ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)) return;
    ApplyRemoteWeaponPresentation(GetPawn(), bIsAutoAttacking ? 1 : LocalSheathState, false);
}

void AWowGameplayController::DrawLocalMeleeWeapons()
{
    const bool bChanged = !bLocalSheathStateInitialized || LocalSheathState != 1;
    LocalSheathState = 1;
    bLocalSheathStateInitialized = true;
    if (ConnectionManager)
    {
        if (FWowEntity* LocalEntity = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer())
        {
            const uint32 Previous = LocalEntity->GetField(UnitField::BYTES_2);
            LocalEntity->SetField(UnitField::BYTES_2, (Previous & 0xffffff00u) | 1u);
        }
        if (bChanged) ConnectionManager->SendSetSheathed(1);
    }
    ApplyLocalWeaponPresentation();
}

void AWowGameplayController::OnToggleSheathKey()
{
	if (IsWowUiConsumingKeyboardInput()) return;

	AActor* CharacterActor = GetPawn();
	if (!CharacterActor) return;

	bool bHasMeleeWeapon = false;
	bool bHasRangedWeapon = false;
	TInlineComponentArray<UStaticMeshComponent*> EquipmentComponents(CharacterActor);
	for (const UStaticMeshComponent* Equipment : EquipmentComponents)
	{
		if (!Equipment || !Equipment->ComponentHasTag(TEXT("WowHeldWeapon"))) continue;
		if (Equipment->ComponentHasTag(TEXT("WowRangedWeapon")))
		{
			bHasRangedWeapon = true;
		}
		else
		{
			bHasMeleeWeapon = true;
		}
	}
	if (!bHasMeleeWeapon && !bHasRangedWeapon) return;

	if (!bLocalSheathStateInitialized)
	{
		LocalSheathState = bHasMeleeWeapon ? 1 : 2;
		bLocalSheathStateInitialized = true;
	}
	LocalSheathState = LocalSheathState == 0
		? (bHasMeleeWeapon ? 1 : 2)
		: 0;

	if (ConnectionManager)
	{
		if (FWowEntity* LocalEntity = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer())
		{
			const uint32 Previous = LocalEntity->GetField(UnitField::BYTES_2);
			LocalEntity->SetField(UnitField::BYTES_2,
				(Previous & 0xffffff00u) | static_cast<uint32>(LocalSheathState));
		}
		ConnectionManager->SendSetSheathed(LocalSheathState);
	}
	// WoW plays a reach-over-the-shoulder (or to the hip) gesture and moves the weapon
	// partway through it. Moving/casting keeps the instant swap.
	bool bHipWeapon = false;
	for (const UStaticMeshComponent* Equipment : EquipmentComponents)
		if (Equipment && Equipment->ComponentHasTag(TEXT("WowHeldWeapon")) && !Equipment->ComponentHasTag(TEXT("WowRangedWeapon"))
			&& (Equipment->ComponentHasTag(TEXT("WowSheathHipWeaponLeft")) || Equipment->ComponentHasTag(TEXT("WowSheathHipWeaponRight"))))
			bHipWeapon = true;
	float GestureLength = 0.f;
	if (const AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(CharacterActor); PlayerChar && PlayerChar->AnimationController)
		GestureLength = PlayerChar->AnimationController->PlaySheathAnimation(bHipWeapon);
	UE_LOG(LogWowGameplay, Log, TEXT("Sheath toggle: state=%d hip=%d gesture=%.2fs"), LocalSheathState, bHipWeapon ? 1 : 0, GestureLength);
	GetWorldTimerManager().ClearTimer(SheathSwapTimer);
	if (GestureLength > 0.1f)
		GetWorldTimerManager().SetTimer(SheathSwapTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { ApplyLocalWeaponPresentation(); }),
			GestureLength * 0.45f, false);
	else
		ApplyLocalWeaponPresentation();
}

bool AWowGameplayController::IsMovementHeldBySitting() const
{
    if (bLocalStandingUp) return true;
    if (!bLocalSitting || !FWowClientPreferences::GetWorldOption(TEXT("rootedstandup"))) return false;
    const ACharacter* Seated = Cast<ACharacter>(GetPawn());
    return !Seated || !Seated->GetCharacterMovement() || !Seated->GetCharacterMovement()->IsFalling();
}

void AWowGameplayController::SetLocalSitting(bool bSitting, bool bInstantStand)
{
	if (bLocalSitting == bSitting) return;
    // Experimental realism: moving while seated stands up first (rooted for the
    // stand-up clip) instead of WoW's instant stand. Never mid-air.
    if (!bSitting && bInstantStand && FWowClientPreferences::GetWorldOption(TEXT("rootedstandup")))
    {
        const ACharacter* Seated = Cast<ACharacter>(GetPawn());
        if (!Seated || !Seated->GetCharacterMovement() || !Seated->GetCharacterMovement()->IsFalling())
            bInstantStand = false;
    }
	bLocalSitting = bSitting;
    bLocalSitTransition = false;
    bLocalStandingUp = false;
    if (bSitting)
    {
        StopAutoAttack();
        LocalSheathState = 0;
        bLocalSheathStateInitialized = true;
        if (ConnectionManager)
        {
            auto* Local = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
            if(Local) Local->SetField(UnitField::BYTES_2,Local->GetField(UnitField::BYTES_2)&0xffffff00u);
            ConnectionManager->SendSetSheathed(0);
            SpellSheathedWeapons.Remove(ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid);
        }
        ApplyLocalWeaponPresentation();
    }

	if (AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(GetPawn()))
	{
		if (bSitting)
		{
			PlayerChar->bIsAutoRunning = false;
			if (UCharacterMovementComponent* Movement = PlayerChar->GetCharacterMovement())
			{
				Movement->StopMovementImmediately();
			}
		}
		if (PlayerChar->AnimationController)
		{
            bLocalSitTransition=(!bSitting && bInstantStand) ? false
                : PlayerChar->AnimationController->PlayAnimationById(bSitting ? EWowAnimId::SitGroundDown : EWowAnimId::SitGroundUp,false);
            if (!bSitting && bLocalSitTransition)
            {
                // Root in place while getting up (safety cap in case the clip never reports done).
                bLocalStandingUp = true;
                LocalStandUpLockUntil = FPlatformTime::Seconds() + 1.5;
                if (UCharacterMovementComponent* Movement = PlayerChar->GetCharacterMovement()) Movement->StopMovementImmediately();
            }
            if(!bLocalSitTransition) PlayerChar->AnimationController->PlayAnimationById(
                bSitting ? EWowAnimId::SitGround : EWowAnimId::Stand, true);
		}
	}

	if (ConnectionManager)
	{
		const uint8 StandState = bSitting ? WowStandState::SIT : WowStandState::STAND;
		if (FWowEntity* LocalEntity = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer())
		{
			const uint32 Previous = LocalEntity->GetField(UnitField::BYTES_1);
			LocalEntity->SetField(UnitField::BYTES_1,
				(Previous & 0xffffff00u) | static_cast<uint32>(StandState));
		}
		ConnectionManager->SendStandStateChange(StandState);
	}
}

void AWowGameplayController::OnToggleSitKey()
{
	if (IsWowUiConsumingKeyboardInput()) return;
	if (const AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(GetPawn()))
	{
		const UCharacterMovementComponent* Movement = PlayerChar->GetCharacterMovement();
		if (!Movement || Movement->IsFalling() || PlayerChar->IsInWowWaterSwimmingState()) return;
		// r293 (Stefan): X while running sat down and the held W stood straight back up. Like
		// the original client, sitting needs you to stand still.
		if (!bLocalSitting && (PlayerChar->bIsAutoRunning || Movement->Velocity.SizeSquared2D() > 100.0)) return;
	}
	SetLocalSitting(!bLocalSitting);
}

bool AWowGameplayController::HandleUiMousePress(const FString& Button, int64& PressedFrameHandle, bool bAllowDragDrop)
{
	PressedFrameHandle = -1;

	if (!UIManager || !UIManager->GetFrameManager())
	{
		return false;
	}

	int64 HitFrame = GetUiFrameUnderCursor();
	// VR: the press must land on what the player sees highlighted (Stefan 09.10: Healing Wave
	// showed its tooltip under the pointer but the click found no frame and nothing was cast).
	if (HitFrame < 0 && FWowUiVrHost::IsActive())
	{
		const int64 Hovered = UIManager->GetFrameManager()->GetHoveredFrame();
		if (Hovered >= 0 && UIManager->GetFrameManager()->IsFrameVisible(Hovered))
		{
			UE_LOG(LogWowGameplay, Log, TEXT("VR press found no frame under the pointer; using the hovered frame '%s'"),
				*UIManager->GetFrameManager()->GetFrameName(Hovered));
			HitFrame = Hovered;
		}
	}
	if (HitFrame < 0)
	{
		// Diagnostic (Stefan 09.10: micro menu buttons do not react): where a press that found
		// no UI frame was, next to the laid-out rect of CharacterMicroButton, once a second.
		static double NextMissLog = 0.0;
		float MissX = 0.f, MissY = 0.f;
		if (FPlatformTime::Seconds() >= NextMissLog && GetMousePosition(MissX, MissY))
		{
			NextMissLog = FPlatformTime::Seconds() + 1.0;
			FWowFrameManager* Frames = UIManager->GetFrameManager();
			float L = 0, B = 0, R = 0, T = 0;
			const int64 Micro = Frames->FindFrame(TEXT("CharacterMicroButton"));
			const bool bRect = Micro >= 0 && Frames->GetFrameScreenRect(Micro, L, B, R, T);
			UE_LOG(LogWowGameplay, Log, TEXT("UI press found no frame at viewport (%.0f,%.0f) nativeUi=%d; CharacterMicroButton rect=%s (%.0f,%.0f)-(%.0f,%.0f) shown=%d"),
				MissX, MissY, IsPointerOverNativeGameUi() ? 1 : 0, bRect ? TEXT("yes") : TEXT("no"), L, B, R, T,
				Micro >= 0 && Frames->IsFrameVisible(Micro) ? 1 : 0);
		}
		UIManager->GetFrameManager()->DismissChatMenus();
		return false;
	}

	FWowFrameManager* FrameManager = UIManager->GetFrameManager();
	{
		// Diagnostic: which UI frame takes a mouse press (an invisible addon frame
		// here blocks targeting and camera drags). Logged once per frame handle.
		static TSet<int64> LoggedPressFrames;
		if (!LoggedPressFrames.Contains(HitFrame) && LoggedPressFrames.Num() < 256)
		{
			LoggedPressFrames.Add(HitFrame);
			const FWowFrameDef* Def = FrameManager->GetFrameDef(HitFrame);
			UE_LOG(LogTemp, Log, TEXT("UI mouse press %s taken by frame [%lld] '%s' size=%.0fx%.0f anchors=%d allPoints=%d"),
				*Button, HitFrame, *FrameManager->GetFrameName(HitFrame), Def ? Def->Width : 0.f, Def ? Def->Height : 0.f,
				Def ? Def->Anchors.Num() : 0, Def && Def->bSetAllPoints ? 1 : 0);
		}
	}
	const int32 HitActionSlot = FrameManager->GetActionSlotForFrame(HitFrame);
	if (bAllowDragDrop && ConnectionManager && ConnectionManager->HasCursorPayload() && HitActionSlot >= 0)
	{
		ConnectionManager->PlaceCursorIntoActionSlot(HitActionSlot);
		return true;
	}
	if (bAllowDragDrop && ConnectionManager && ConnectionManager->HasCursorPayload())
	{
		// r312 (Stefan 13.10: a macro clicked onto a Bartender button did not appear there): an addon
		// action button (type "action", e.g. BT4ButtonN) whose OnReceiveDrag left the cursor as it
		// was takes it into its "action" slot, like r306 does for a drag released over it.
		FString TypeBefore, DetailBefore;
		int32 IdBefore = 0;
		ConnectionManager->GetCursorInfo(TypeBefore, IdBefore, DetailBefore);
		const bool bReceived = FrameManager->DispatchReceiveDrag(HitFrame);
		FString TypeAfter, DetailAfter;
		int32 IdAfter = 0;
		const bool bStill = ConnectionManager->GetCursorInfo(TypeAfter, IdAfter, DetailAfter)
			&& TypeAfter == TypeBefore && IdAfter == IdBefore && DetailAfter == DetailBefore;
		int32 AddonSlot = -1;
		if (FWowEventSystem* DropEvents = UIManager->GetEventSystem(); DropEvents && bStill)
		{
			int64 Target = HitFrame;
			for (int32 Depth = 0; Target >= 0 && Depth < 4; ++Depth, Target = FrameManager->GetParentHandle(Target))
				if (DropEvents->GetFrameAttribute(Target, TEXT("type")).Equals(TEXT("action"), ESearchCase::IgnoreCase)) break;
			const int32 Action = Target >= 0 ? FCString::Atoi(*DropEvents->GetFrameAttribute(Target, TEXT("action"))) : 0;
			if (Action >= 1 && Action <= 144)
			{
				AddonSlot = Action - 1;
				ConnectionManager->PlaceCursorIntoActionSlot(AddonSlot);
			}
		}
		if (bReceived || AddonSlot >= 0)
		{
			UE_LOG(LogTemp, Log, TEXT("UI click-drop on '%s': %s %d -> script=%d nativeSlot=%d"),
				*FrameManager->GetFrameName(HitFrame), *TypeBefore, IdBefore, bReceived ? 1 : 0, AddonSlot + 1);
			return true;
		}
	}

	PressedFrameHandle = HitFrame;
	// The press can land on a child of the draggable button (item cooldown, count or
	// icon frame); the drag belongs to the nearest ancestor that registered for it.
	int64 DragFrame = -1;
	if (bAllowDragDrop && Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) && HitActionSlot < 0)
	{
		int64 Candidate = HitFrame;
		for (int32 Depth = 0; Candidate >= 0 && Depth < 4; ++Depth, Candidate = FrameManager->GetParentHandle(Candidate))
		{
			if (FrameManager->IsFrameDragRegistered(Candidate, Button)) { DragFrame = Candidate; break; }
		}
	}
	if (bAllowDragDrop && Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) &&
        (HitActionSlot >= 0 || DragFrame >= 0))
	{
		PendingActionDragSlot = HitActionSlot;
        PendingUiDragFrame = HitActionSlot >= 0 ? HitFrame : DragFrame;
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		if (GetMousePosition(MouseX, MouseY))
		{
			ActionDragStartPosition = FVector2D(MouseX, MouseY);
		}
	}
	FrameManager->DispatchMouseDown(HitFrame, Button);
	if (FrameManager->IsFrameClickRegistered(HitFrame, Button, true))
	{
		FrameManager->DispatchClick(HitFrame, Button, true);
	}

	return true;
}

bool AWowGameplayController::HandleUiMouseRelease(const FString& Button, int64& PressedFrameHandle)
{
	if (!UIManager || !UIManager->GetFrameManager())
	{
		PressedFrameHandle = -1;
		return false;
	}

	if (PressedFrameHandle < 0)
	{
		return false;
	}

	FWowFrameManager* FrameManager = UIManager->GetFrameManager();
	const int64 ReleasedFrameHandle = PressedFrameHandle;
	PressedFrameHandle = -1;

	FrameManager->DispatchMouseUp(ReleasedFrameHandle, Button);
	auto ResolveAncestorNamed = [FrameManager](int64 Handle, const TCHAR* TargetName) -> int64
	{
		int32 Guard = 0;
		while (Handle >= 0 && Guard++ < 32)
		{
			if (FrameManager->GetFrameName(Handle).Equals(TargetName, ESearchCase::IgnoreCase))
			{
				return Handle;
			}
			Handle = FrameManager->GetParentHandle(Handle);
		}
		return -1;
	};
	int64 HoveredFrameHandle = GetUiFrameUnderCursor();
	// r251 (Stefan 12.10, VR: bag items and action buttons did nothing when clicked with the
	// mouse): in the headset no frame is found under the pointer, so the release never matched
	// the pressed frame and OnClick never ran. Use the highlighted frame, as the press does.
	if (HoveredFrameHandle < 0 && FWowUiVrHost::IsActive())
	{
		const int64 Hovered = FrameManager->GetHoveredFrame();
		if (Hovered >= 0 && FrameManager->IsFrameVisible(Hovered)) HoveredFrameHandle = Hovered;
	}
	auto WasReleasedOverNamed = [&ResolveAncestorNamed, ReleasedFrameHandle, HoveredFrameHandle](const TCHAR* TargetName)
	{
		return ResolveAncestorNamed(ReleasedFrameHandle, TargetName) >= 0 &&
			ResolveAncestorNamed(HoveredFrameHandle, TargetName) >= 0;
	};
	if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) &&
		WasReleasedOverNamed(TEXT("CharacterMicroButton")))
	{
		OnCharacterKey();
		return true;
	}
	if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) &&
		WasReleasedOverNamed(TEXT("AchievementMicroButton")))
	{
		OnAchievementKey();
		return true;
	}
	if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) &&
		WasReleasedOverNamed(TEXT("CharacterFrameCloseButton")))
	{
		OnCharacterKey();
		return true;
	}
	if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase))
	{
		for (int32 TabIndex = 1; TabIndex <= 3; ++TabIndex)
		{
			const FString TabName = FString::Printf(TEXT("CharacterFrameTab%d"), TabIndex);
			if (WasReleasedOverNamed(*TabName))
			{
				FrameManager->SetActiveCharacterSheetTab(TabIndex);
				return true;
			}
		}
		if (WasReleasedOverNamed(TEXT("CharacterNativeRightStatDropDown")))
		{
			FrameManager->DispatchClick(
				FrameManager->FindFrame(TEXT("CharacterNativeRightStatDropDown")), Button, false);
			return true;
		}
	}
	if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) &&
		WasReleasedOverNamed(TEXT("CharacterModelFrameRotateLeftButton")))
	{
        // Rotation is handled on press and while held.
		return true;
	}
	if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) &&
		WasReleasedOverNamed(TEXT("CharacterModelFrameRotateRightButton")))
	{
        // Rotation is handled on press and while held.
		return true;
	}
	if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) &&
		WasReleasedOverNamed(TEXT("MainMenuMicroButton")))   // the ? button keeps its own OnClick (help), not the game menu
	{
		if (GameMenuCountdownViewportWidget.IsValid() &&
			GameMenuCountdownViewportWidget->GetVisibility().IsVisible())
		{
			CancelGameMenuCountdown();
		}
		else if (IsGameMenuVisible())
		{
			HideGameMenu();
		}
		else
		{
			ShowGameMenu();
		}
		return true;
	}
	if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase))
	{
		if (WasReleasedOverNamed(TEXT("MainMenuBarBackpackButton")))
		{
			OnBagKey();
			return true;
		}

		static const TCHAR* EquippedBagButtonNames[] = {
			TEXT("CharacterBag0Slot"),
			TEXT("CharacterBag1Slot"),
			TEXT("CharacterBag2Slot"),
			TEXT("CharacterBag3Slot"),
		};
		for (int32 BagButtonIndex = 0; BagButtonIndex < UE_ARRAY_COUNT(EquippedBagButtonNames); ++BagButtonIndex)
		{
			if (WasReleasedOverNamed(EquippedBagButtonNames[BagButtonIndex]))
			{
				FrameManager->TogglePlayerBag(BagButtonIndex + 1);
				return true;
			}
		}
	}
	if (FrameManager->IsFrameClickRegistered(ReleasedFrameHandle, Button, false)
		&& HoveredFrameHandle == ReleasedFrameHandle)
	{
		FrameManager->DispatchClick(ReleasedFrameHandle, Button, false);
	}

	return true;
}

void AWowGameplayController::OnLeftClick()
{
    if (PressedLeftMouseFrameHandle >= 0) return;
    if(GameUI&&GameUI->IsMovingFrame())return;
	// The click that brings the window back (Alt-Tab, screenshot tool) must not start a
	// camera drag, but a click on a UI button is safe: accept it instead of making the
	// player click twice (close buttons, tabs).
	if (bMouseInputNeedsRearmAfterFocusChange)
	{
		// Diagnostic for the "double click after Alt-Tab" report.
		UE_LOG(LogWowGameplay, Log, TEXT("First left click after focus change: foreground=%d frame=%lld nativeUi=%d"),
			IsWowClientProcessForeground() ? 1 : 0, GetUiFrameUnderCursor(), IsPointerOverNativeGameUi() ? 1 : 0);
	}
	if (bMouseInputNeedsRearmAfterFocusChange && IsWowClientProcessForeground())
	{
		// Stefan 10.10: the first click after Alt-Tab must never be lost, anywhere. A press that
		// arrived as an input event is a real click on the game window (Windows delivers the
		// activation click): accept it fully, camera included. A press only recovered by polling
		// the button may have started outside the game, so over the world it only targets.
		if (GWowSyntheticActivationPress && GetUiFrameUnderCursor() < 0 && !IsPointerOverNativeGameUi())
		{
			GWowActivationWorldClick = true;
			float MouseX = 0.f, MouseY = 0.f;
			GetMousePosition(MouseX, MouseY);
			GWowActivationClickPosition = FVector2D(MouseX, MouseY);
			return;
		}
		bMouseInputNeedsRearmAfterFocusChange = false;
	}
	if (bMouseInputNeedsRearmAfterFocusChange || !IsWowClientProcessForeground())
	{
		UE_LOG(LogWowGameplay, Log, TEXT("Left click ignored after focus change: foreground=%d frame=%lld"),
			IsWowClientProcessForeground() ? 1 : 0, GetUiFrameUnderCursor());
		return;
	}

	if (IsNativeModalUiVisible())
	{
		return;
	}

	if (bRightMouseLooking && !IsMouseButtonActuallyDown(EKeys::RightMouseButton))
	{
		bMouseChordMovingForward = false;
		EndRightMouseLook();
	}

	// WoW mouse chord: while Mouse2 owns camera look, Mouse1 becomes forward
	// movement instead of targeting or clicking through the hidden cursor.
	if (bRightMouseLooking)
	{
		bMouseChordMovingForward = true;
		return;
	}

    if (IsPointerOverNativeGameUi())
    {
        bNativeUiOwnsLeftMousePress = true;
        static double NextNativeLog = 0.0;
        if (FPlatformTime::Seconds() >= NextNativeLog)
        {
            NextNativeLog = FPlatformTime::Seconds() + 1.0;
            float X = 0.f, Y = 0.f; GetMousePosition(X, Y);
            UE_LOG(LogWowGameplay, Log, TEXT("Left press taken by a native window at viewport (%.0f,%.0f)"), X, Y);
        }
        return;
    }

	bool bAllowUiPress = true;
	if (UIManager && UIManager->GetFrameManager())
	{
		FWowFrameManager* FrameManager = UIManager->GetFrameManager();
		bAllowUiPress = !IsPassiveChatCameraSurface(FrameManager, GetUiFrameUnderCursor());
	}
	if (bAllowUiPress && HandleUiMousePress(TEXT("LeftButton"), PressedLeftMouseFrameHandle, true))
	{
		if (UIManager && UIManager->GetFrameManager() && CharacterSheetPreview)
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			int64 Handle = PressedLeftMouseFrameHandle;
			int32 Guard = 0;
			while (Handle >= 0 && Guard++ < 32)
			{
                const FString Name = FrameManager->GetFrameName(Handle);
                if (Name == TEXT("CharacterModelFrameRotateLeftButton") ||
                    Name == TEXT("CharacterModelFrameRotateRightButton"))
                {
                    CharacterSheetPreview->RotateCharacter(Name.EndsWith(TEXT("LeftButton")) ? -15.f : 15.f);
                    break; // Arrow presses must not also begin dragging the model.
                }
				if (Name.Equals(TEXT("CharacterModelFrame"), ESearchCase::IgnoreCase))
				{
					float MouseX = 0.0f;
					float MouseY = 0.0f;
					if (GetMousePosition(MouseX, MouseY))
					{
						bCharacterSheetModelDragging = true;
						LastCharacterSheetDragPosition = FVector2D(MouseX, MouseY);
					}
					break;
				}
				// Dressing room: drag on the model turns it, like the character sheet. Its rotate
				// buttons go through Lua (Model_RotateLeft/Right -> SetRotation).
				if (Name == TEXT("DressUpModelRotateLeftButton") || Name == TEXT("DressUpModelRotateRightButton"))
				{
					break;
				}
				// r262 (Stefan 12.10: left-drag did not turn the model): the press is found on
				// DressUpFrame itself (DressUpModel takes no mouse), so a press inside the model's
				// rect starts the drag too.
				// r270 (Stefan 13.10: still no left-drag): DressUpModel's own widget can be hidden behind
				// the preview image, so also test the model's XML area inside DressUpFrame (TOPLEFT 22,-76,
				// BOTTOMRIGHT -46,134 of the 384 x 512 frame).
				if (Name == TEXT("DressUpModel") || (Name == TEXT("DressUpFrame")
					&& (FrameManager->IsMouseOverFrame(FrameManager->FindFrame(TEXT("DressUpModel")))
						|| FrameManager->IsMouseOverFrame(Handle, -76.f, 134.f, 22.f, -46.f))))
				{
					float DressMouseX = 0.0f;
					float DressMouseY = 0.0f;
					if (GetMousePosition(DressMouseX, DressMouseY))
					{
						bDressUpModelDragging = true;
						LastDressUpDragPosition = FVector2D(DressMouseX, DressMouseY);
					}
					break;
				}
				Handle = FrameManager->GetParentHandle(Handle);
			}
		}
		return; // Don't target through UI
	}

    if (ConnectionManager && ConnectionManager->HasCursorItemPayload())
    {
        ConnectionManager->RequestDeleteCursorItem();
        return;
    }
    // A held spell or action clicked onto the world is dropped (an action taken off a bar is removed).
    if (ConnectionManager && ConnectionManager->HasCursorPayload())
    {
        if (!ConnectionManager->DropCursorActionOutside()) ConnectionManager->ClearCursorPayload();
        return;
    }
	// r650: the isometric camera has no left-drag look; the press targets on release.
	if (FWowIsometricCamera::IsActive())
	{
		float PressX = 0.f, PressY = 0.f;
		GetMousePosition(PressX, PressY);
		WowIsoClickMove::GLeftPress = true;
		WowIsoClickMove::GLeftChorded = false;
		WowIsoClickMove::GLeftPressCursor = FVector2D(PressX, PressY);
		return;
	}
	// A world press begins camera-only orbit. A stationary press is resolved as
	// a target click on release; dragging never moves or rotates the character.
	BeginLeftMouseLook();
}

// When the current left press started camera look, and the camera rotation then. A press that
// turned the camera (or was held) is a drag even when the mouse-delta count missed it: on a
// flight path hitches dropped the delta and a camera drag cleared the target on release.
static double GLeftLookPressSeconds = 0.0;
static FRotator GLeftLookPressRotation = FRotator::ZeroRotator;

void AWowGameplayController::OnLeftClickReleased()
{
    bNativeUiOwnsLeftMousePress = false;
	bCharacterSheetModelDragging = false;
	if (IsNativeModalUiVisible())
	{
		bMouseChordMovingForward = false;
		PressedLeftMouseFrameHandle = -1;
        PendingUiDragFrame = -1;
        PendingActionDragSlot = -1;
        bUiDragActive = false;
		if (bLeftMouseLooking)
		{
			EndLeftMouseLook();
		}
		return;
	}

	bMouseChordMovingForward = false;
	if (bUiDragActive && UIManager && UIManager->GetFrameManager())
	{
		FWowFrameManager* FrameManager = UIManager->GetFrameManager();
        int64 Destination = GetUiFrameUnderCursor();
        // VR: drop on what the player sees highlighted, as for presses (a drop that found no frame
        // counted as "into the world" and removed a bar spell meant for another slot).
        if (Destination < 0 && FWowUiVrHost::IsActive())
        {
            const int64 Hovered = FrameManager->GetHoveredFrame();
            if (Hovered >= 0 && FrameManager->IsFrameVisible(Hovered)) Destination = Hovered;
        }
        const int32 DestinationSlot = FrameManager->GetActionSlotForFrame(Destination);
        if (ConnectionManager && ConnectionManager->HasCursorPayload())
        {
            const bool bOverWorld = Destination < 0 || FrameManager->GetFrameName(Destination) == TEXT("WorldFrame") ||
                FrameManager->GetFrameName(Destination) == TEXT("UIParent");
            if (DestinationSlot >= 0) ConnectionManager->PlaceCursorIntoActionSlot(DestinationSlot);
            else if (bOverWorld && ConnectionManager->DropCursorActionOutside()) {} // bar spell dragged off: removed
            // A spell dragged out of the spellbook onto the world is simply let go (Stefan 10.10:
            // it stayed on the pointer until it was put on an action bar).
            else if (bOverWorld && !ConnectionManager->HasCursorItemPayload()) ConnectionManager->ClearCursorPayload();
            else if (ConnectionManager->HasCursorItemPayload() &&
                (Destination < 0 || FrameManager->GetFrameName(Destination) == TEXT("WorldFrame") ||
                 FrameManager->GetFrameName(Destination) == TEXT("UIParent")))
                ConnectionManager->RequestDeleteCursorItem();
            else
            {
                FString TypeBefore, DetailBefore;
                int32 IdBefore = 0;
                ConnectionManager->GetCursorInfo(TypeBefore, IdBefore, DetailBefore);
                int64 Receiver = -1;
                for (int64 Target = Destination; Target >= 0; Target = FrameManager->GetParentHandle(Target))
                    if (FrameManager->DispatchReceiveDrag(Target)) { Receiver = Target; break; }
                // r306: an addon action button (type "action", e.g. BT4ButtonN) whose OnReceiveDrag left
                // the cursor untouched takes the drop into its "action" slot, as the client's buttons do.
                FString TypeAfter, DetailAfter;
                int32 IdAfter = 0;
                const bool bStill = ConnectionManager->GetCursorInfo(TypeAfter, IdAfter, DetailAfter);
                int32 AddonSlot = -1;
                if (FWowEventSystem* DropEvents = UIManager->GetEventSystem(); DropEvents && bStill &&
                    TypeAfter == TypeBefore && IdAfter == IdBefore && DetailAfter == DetailBefore)
                {
                    int64 Target = Destination;
                    for (int32 Depth = 0; Target >= 0 && Depth < 4; ++Depth, Target = FrameManager->GetParentHandle(Target))
                        if (DropEvents->GetFrameAttribute(Target, TEXT("type")).Equals(TEXT("action"), ESearchCase::IgnoreCase)) break;
                    const int32 Action = Target >= 0 ? FCString::Atoi(*DropEvents->GetFrameAttribute(Target, TEXT("action"))) : 0;
                    if (Action >= 1 && Action <= 144)
                    {
                        AddonSlot = Action - 1;
                        ConnectionManager->PlaceCursorIntoActionSlot(AddonSlot);
                    }
                }
                UE_LOG(LogTemp, Log, TEXT("UI drop on '%s': %s %d -> receiver '%s', nativeSlot=%d"),
                    *FrameManager->GetFrameName(Destination), *TypeBefore, IdBefore,
                    *FrameManager->GetFrameName(Receiver), AddonSlot + 1);
            }
        }
        FrameManager->DispatchDragStop(PressedLeftMouseFrameHandle, TEXT("LeftButton"));
		if (PressedLeftMouseFrameHandle >= 0)
		{
			FrameManager->DispatchMouseUp(PressedLeftMouseFrameHandle, TEXT("LeftButton"));
		}
		PressedLeftMouseFrameHandle = -1;
		PendingActionDragSlot = -1;
		PendingUiDragFrame = -1;
		bUiDragActive = false;
		return;
	}
	PendingActionDragSlot = -1;
	PendingUiDragFrame = -1;
	const bool bActivationWorldClick = GWowActivationWorldClick;
	GWowActivationWorldClick = false;
	GWowActivationUiPress = false;
	if (HandleUiMouseRelease(TEXT("LeftButton"), PressedLeftMouseFrameHandle))
	{
		return;
	}

	if (bActivationWorldClick && !bLeftMouseLooking)
	{
		float MouseX = 0.f, MouseY = 0.f;
		GetMousePosition(MouseX, MouseY);
		if (FVector2D::Distance(FVector2D(MouseX, MouseY), GWowActivationClickPosition) < 8.f && GetUiFrameUnderCursor() < 0)
		{
			if (TryTargetUnderCursor() && ConnectionManager && ConnectionManager->PendingTargetSpell)
				ConnectionManager->CompleteSpellTarget(TargetGuid);
		}
		return;
	}

	if (WowIsoClickMove::GLeftPress) // r650
	{
		WowIsoClickMove::GLeftPress = false;
		const bool bChorded = WowIsoClickMove::GLeftChorded;
		WowIsoClickMove::GLeftChorded = false;
		float ReleaseX = 0.f, ReleaseY = 0.f;
		GetMousePosition(ReleaseX, ReleaseY);
		bool bRightHeldNow = false;
		TryGetPhysicalInputDown(EKeys::RightMouseButton, bRightHeldNow);
		if (!bChorded && !bRightHeldNow && FWowIsometricCamera::IsActive() && GetUiFrameUnderCursor() < 0
			&& FVector2D::Distance(FVector2D(ReleaseX, ReleaseY), WowIsoClickMove::GLeftPressCursor) < 8.f)
		{
			if (TryTargetUnderCursor() && ConnectionManager && ConnectionManager->PendingTargetSpell)
				ConnectionManager->CompleteSpellTarget(TargetGuid);
		}
		return;
	}

	if (bLeftMouseLooking)
	{
		const FRotator NowRotation = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation() : GetControlRotation();
		const bool bCameraTurned = FMath::Abs(FRotator::NormalizeAxis(NowRotation.Yaw - GLeftLookPressRotation.Yaw)) > 1.5f
			|| FMath::Abs(FRotator::NormalizeAxis(NowRotation.Pitch - GLeftLookPressRotation.Pitch)) > 1.5f;
		const bool bWasDrag = bLeftMouseDragged || bCameraTurned
			|| (GLeftLookPressSeconds > 0.0 && FPlatformTime::Seconds() - GLeftLookPressSeconds > 0.6);
		EndLeftMouseLook();
		if (!bWasDrag && !bRightMouseLooking)
		{
            if (TryTargetUnderCursor() && ConnectionManager && ConnectionManager->PendingTargetSpell)
                ConnectionManager->CompleteSpellTarget(TargetGuid);
		}
	}
}

void AWowGameplayController::BeginLeftMouseLook()
{
	if (bMouseInputNeedsRearmAfterFocusChange || !IsWowClientProcessForeground())
	{
		return;
	}
	// r637: no left-drag camera look in the isometric camera (it hid the cursor that click-to-move and the
	// both-buttons walk steer by); the isometric camera turns with right-drag or middle mouse.
	if (FWowIsometricCamera::IsActive()) return;
	// Dragging a held item/spell over the world (button still down) is not camera look:
	// look mode hid the item icon and showed the plain pickup cursor instead.
	if (ConnectionManager && ConnectionManager->HasCursorPayload())
	{
		return;
	}

	if (bLeftMouseLooking)
	{
		// A missed release can leave the old look state alive. Start fresh rather
		// than ignoring the user's next click.
		bLeftMouseLooking = false;
		LeftMouseReleaseMissingFrames = 0;
		bHasSavedLeftMousePosition = false;
		bHasLastMouseLookPosition = false;
		bHasMouseLookAnchor = false;
		PendingMouseTurn = 0.0f;
		PendingMouseLookUp = 0.0f;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	bHasSavedLeftMousePosition = GetMousePosition(MouseX, MouseY);
	if (bHasSavedLeftMousePosition)
	{
		SavedLeftMouseX = FMath::RoundToInt(MouseX);
		SavedLeftMouseY = FMath::RoundToInt(MouseY);
	}

	GLeftLookPressSeconds = FPlatformTime::Seconds();
	GLeftLookPressRotation = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation() : GetControlRotation();
	bLeftMouseLooking = true;
	bLeftMouseDragged = false;
	LeftMouseDragDistance = 0.0f;
	LeftMouseLookStartedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	LeftMouseReleaseMissingFrames = 0;
	if (UIManager && UIManager->GetFrameManager())
	{
		UIManager->GetFrameManager()->SetMouseInteractionSuppressed(true);
	}
	bHasLastMouseLookPosition = false;
	ApplyWowMouseLookInputMode(this, false);
	PrimeMouseLookAnchor();
	CenterMouseLookCursor();
	bShowMouseCursor = false;
	CurrentMouseCursor = EMouseCursor::None;
	SetNativePlatformCursorVisible(false);
}

void AWowGameplayController::EndLeftMouseLook()
{
	if (!bLeftMouseLooking) return;

	bLeftMouseLooking = false;
	LeftMouseReleaseMissingFrames = 0;
	bHasLastMouseLookPosition = false;
	bHasMouseLookAnchor = false;
	bIgnoreNextMouseLookDelta = false;
	PendingMouseTurn = 0.0f;
	PendingMouseLookUp = 0.0f;
	if (!bRightMouseLooking && UIManager && UIManager->GetFrameManager())
	{
		UIManager->GetFrameManager()->SetMouseInteractionSuppressed(false);
	}
	bShowMouseCursor = true;
	CurrentMouseCursor = EMouseCursor::None;
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}
	ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
	FocusWowGameViewport();
	if (bHasSavedLeftMousePosition)
	{
		SetMouseLocation(SavedLeftMouseX, SavedLeftMouseY);
		bIgnoreNextMouseLookDelta = true;
	}
	bHasSavedLeftMousePosition = false;
	SetNativePlatformCursorVisible(false);
}

void AWowGameplayController::CancelLeftMouseLookForRightMouseChord()
{
	if (!bLeftMouseLooking)
	{
		return;
	}

	// Left mouse look is camera-only. When right mouse takes over, clear the
	// left state without restoring the cursor or copying camera yaw to pawn yaw.
	bLeftMouseLooking = false;
	bLeftMouseDragged = false;
	LeftMouseDragDistance = 0.0f;
	LeftMouseReleaseMissingFrames = 0;
	bHasLastMouseLookPosition = false;
	bHasMouseLookAnchor = false;
	bIgnoreNextMouseLookDelta = false;
	PendingMouseTurn = 0.0f;
	PendingMouseLookUp = 0.0f;
	bHasSavedLeftMousePosition = false;
}

namespace
{
// Box around a unit's current (animated) skeleton. Mesh bounds keep the standing pose,
// but a corpse lies on the ground next to it.
bool GetPosedBoneBox(const AActor* Actor, FBox& OutBox)
{
    OutBox = FBox(ForceInit);
    const USkeletalMeshComponent* Mesh = Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
    if (!Mesh || Mesh->GetNumBones() == 0) return false;
    for (int32 Bone = 0; Bone < Mesh->GetNumBones(); ++Bone)
        OutBox += Mesh->GetBoneTransform(Bone).GetLocation();
    return OutBox.IsValid != 0;
}
}

uint64 AWowGameplayController::FindCorpseAlongRay(const FVector& Origin, const FVector& Direction, float MaxDistance) const
{
    uint64 Best = 0;
    float BestDistance = MaxDistance;
    const FVector End = Origin + Direction * MaxDistance;
    for (const auto& Pair : SpawnedEntityActors)
    {
        if (!DeadEntityGuids.Contains(Pair.Key) && !FeignDeadEntityGuids.Contains(Pair.Key)) continue;
        FBox Box;
        if (!IsValid(Pair.Value) || !GetPosedBoneBox(Pair.Value, Box)) continue;
        Box = Box.ExpandBy(FVector(18.f, 18.f, 25.f));
        FVector HitLocation, HitNormal; float HitTime = 0.f;
        if (!FMath::LineExtentBoxIntersection(Box, Origin, End, FVector::ZeroVector, HitLocation, HitNormal, HitTime)) continue;
        const float Distance = HitTime * MaxDistance;
        if (Distance < BestDistance) { BestDistance = Distance; Best = Pair.Key; }
    }
    return Best;
}

bool AWowGameplayController::GetWorldPointerRay(FVector& OutOrigin, FVector& OutDirection) const
{
	if (FWowVRSupport::IsActive() && !FWowClientPreferences::GetWorldOption(TEXT("vrmousepointer")))
	{
		FRotator Rotation;
		GetPlayerViewPoint(OutOrigin, Rotation);
		// Claude-Perf r209: ~100 ms smoothed head direction, so natural head tremor does not move
		// the crosshair / stare target (zero when not available yet).
		const FVector Smoothed = FWowVRSupport::GetSmoothedGazeDirection();
		OutDirection = Smoothed.IsNearlyZero() ? Rotation.Vector() : Smoothed.GetSafeNormal();
		return true;
	}
	float MouseX = 0.f, MouseY = 0.f;
	return GetMousePosition(MouseX, MouseY) && DeprojectScreenPositionToWorld(MouseX, MouseY, OutOrigin, OutDirection);
}

bool AWowGameplayController::TryTargetUnderCursor(bool bClearTargetOnMiss, uint64* ClickedGuid)
{
    if (ClickedGuid) *ClickedGuid = 0;
	if (!ConnectionManager) return false;

	// Mouse position deprojected to the world (in VR by default: where the head looks).
	FVector WorldLoc, WorldDir;
	if (!GetWorldPointerRay(WorldLoc, WorldDir))
	{
		UE_LOG(LogWowGameplay, Log, TEXT("TryTarget: no pointer ray"));
		return false;
	}

	// Line trace 10000 cm (~100 WoW yards)
	FVector TraceEnd = WorldLoc + WorldDir * 1000000.0f;
	FHitResult Hit;
	FCollisionQueryParams TraceParams;
	TraceParams.AddIgnoredActor(GetPawn());
	TraceParams.bTraceComplex = false; // Use simple collision (faster, more reliable)

	UWorld* World = GetWorld();
	if (!World) return false;

	// Targeting must remain available when the optional physical collision with
	// NPCs/players is disabled. An object query selects the click capsule by its
	// ECC_Pawn object type without depending on its response to the Pawn channel.
	FCollisionObjectQueryParams EntityObjectQuery;
	EntityObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
    EntityObjectQuery.AddObjectTypesToQuery(ECC_WorldDynamic);
	bool bHit = WowTraceIgnoringWater(World, Hit, WorldLoc, TraceEnd, EntityObjectQuery, TraceParams);
	if (bHit) WowPreferCentredUnit(World, Hit, WorldLoc, TraceEnd, EntityObjectQuery, TraceParams);
	if (!bHit)
	{
		bHit = World->LineTraceSingleByChannel(Hit, WorldLoc, TraceEnd, ECC_Visibility, TraceParams);
	}

    if (bHit && IsWowWorldOccluded(World, WorldLoc, Hit.ImpactPoint, GetPawn(), Hit.GetActor()))
        bHit = false;
    if (bHit && Hit.GetActor())
    {
        AActor* HitActor = Hit.GetActor();

		// Find GUID from spawned entity actors map (reverse lookup â€” more reliable than tag parsing)
		for (const auto& Pair : SpawnedEntityActors)
		{
			if (Pair.Value == HitActor)
			{
				uint64 HitGuid = Pair.Key;
                if (ClickedGuid) *ClickedGuid = HitGuid;
				if (HitGuid != TargetGuid)
				{
					SetTarget(HitGuid);
				}
				return true;
			}
		}

		// Also check if we hit a child component (capsule) of an entity actor
		AActor* HitOwner = HitActor->GetOwner();
		if (HitOwner)
		{
			for (const auto& Pair : SpawnedEntityActors)
			{
				if (Pair.Value == HitOwner)
				{
					uint64 HitGuid = Pair.Key;
                if (ClickedGuid) *ClickedGuid = HitGuid;
					if (HitGuid != TargetGuid)
					{
						SetTarget(HitGuid);
					}
					return true;
				}
			}
		}

		UE_LOG(LogWowGameplay, Log, TEXT("TryTarget: hit non-entity actor: %s (class: %s) component: %s"),
			*HitActor->GetName(), *HitActor->GetClass()->GetName(),
			Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("none"));
	}
	else
	{
		// Count entities with capsule components for debugging
		int32 EntitiesWithCapsules = 0;
		for (const auto& Pair : SpawnedEntityActors)
		{
			if (Pair.Value && Pair.Value->FindComponentByClass<UCapsuleComponent>())
				EntitiesWithCapsules++;
		}
		UE_LOG(LogWowGameplay, Log, TEXT("TryTarget: no hit entities=%d withCapsules=%d traceFrom=%s"),
			SpawnedEntityActors.Num(), EntitiesWithCapsules, *WorldLoc.ToString());
	}

    // r653 (Stefan, Thazz'ril's Pick in the Burning Blade cave: "unable to use the item from too far out" - the
    // first right-click from afar found no entity and only walked): from the isometric camera small objects
    // (a pick, a chest, a quest scroll) are a few pixels big. When the ray hits no unit or object, the nearest
    // usable object within ~30 px of the cursor on screen is taken.
    if (FWowIsometricCamera::IsActive())
    {
        float CursorX = 0.f, CursorY = 0.f;
        int32 ViewX = 0, ViewY = 0;
        GetViewportSize(ViewX, ViewY);
        if (GetMousePosition(CursorX, CursorY) && ViewY > 0)
        {
            const float Reach = 30.f * float(ViewY) / 1080.f;
            uint64 Best = 0;
            float BestDistance = Reach;
            for (const auto& Pair : SpawnedEntityActors)
            {
                AActor* Object = Pair.Value.Get();
                const FWowEntity* Entity = IsValid(Object) ? ConnectionManager->PacketHandler.EntityManager.Find(Pair.Key) : nullptr;
                if (!Entity || !Entity->IsGameObject()) continue;
                // Doors, buttons, quest givers, chests, goobers, mailboxes, spell casters, fishing holes.
                const uint8 Type = Entity->GetFieldByte(GameObjectField::BYTES_1, 1);
                if (!(Type <= 3 || Type == 10 || Type == 19 || Type == 22 || Type == 25)) continue;
                FVector Origin, Extent;
                Object->GetActorBounds(true, Origin, Extent);
                FVector2D Screen;
                if (!ProjectWorldLocationToScreen(Origin, Screen, true)) continue;
                const float Distance = float(FVector2D::Distance(Screen, FVector2D(CursorX, CursorY)));
                if (Distance < BestDistance) { BestDistance = Distance; Best = Pair.Key; }
            }
            if (Best)
            {
                if (ClickedGuid) *ClickedGuid = Best;
                if (Best != TargetGuid) SetTarget(Best);
                return true;
            }
        }
    }
    {
        // Corpses: select by the posed body, up to just behind the ground the ray hit.
        const float Limit = bHit ? FVector::Distance(WorldLoc, Hit.ImpactPoint) + 60.f : 100000.f;
        if (const uint64 CorpseGuid = FindCorpseAlongRay(WorldLoc, WorldDir, Limit))
        {
            if (ClickedGuid) *ClickedGuid = CorpseGuid;
            if (CorpseGuid != TargetGuid) SetTarget(CorpseGuid);
            return true;
        }
    }
	// Clicked on nothing or non-entity â€” clear target and stop attacking
	if (bClearTargetOnMiss && TargetGuid != 0)
	{
		if (bIsAutoAttacking)
		{
			StopAutoAttack();
		}
		SetTarget(0);
	}
	return false;
}

void AWowGameplayController::CacheWorldResources()
{
	if (CachedMpq && CachedAssetCache) return;

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AWowWorldManager::StaticClass(), Found);
	if (Found.Num() > 0)
	{
		AWowWorldManager* WM = Cast<AWowWorldManager>(Found[0]);
		if (WM)
		{
			CachedMpq = WM->GetMpqManager();
			CachedAssetCache = WM->GetAssetCache();

			// Initialize audio system
			InitializeAudioSystem(WM);
		}
	}
}

void AWowGameplayController::OnEntityCreated(const FWowEntity& Entity)
{
	if (!ConnectionManager) return;

	// Handle local player differently - apply character model to existing pawn
	if (Entity.Guid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
	{
		SetupLocalPlayerCharacterModel(Entity);
		return;
	}

	// Query names for newly created entities
	if (Entity.IsPlayer())
	{
		// Check if we already have the name
		if (!ConnectionManager->PacketHandler.PlayerNameCache.Contains(Entity.Guid))
		{
			ConnectionManager->SendNameQuery(Entity.Guid);
		}
	}
	else if (Entity.IsUnit() && Entity.Entry > 0)
	{
		// Check if we already have the creature name
		if (!ConnectionManager->PacketHandler.CreatureNameCache.Contains(Entity.Entry))
		{
			ConnectionManager->SendCreatureQuery(Entity.Entry, Entity.Guid);
		}
	}

	// Server-spawned game objects use GameObjectDisplayInfo.dbc to resolve
	// their client model. This includes doors, mailboxes, fires, lanterns and
	// other interactive world fixtures.
	if (!Entity.IsUnit() && !Entity.IsPlayer() && !Entity.IsGameObject()) return;

	// Skip if already spawned
	if (SpawnedEntityActors.Contains(Entity.Guid)) return;

	// UPDATE_OBJECT can contain a whole crowd. Queue the conversion so only one
	// expensive character/creature model is built per throttled pass.
	QueueEntityModelSpawn(Entity.Guid);
}

void AWowGameplayController::OnEntityDeath(uint64 Guid)
{
    ForgetEntityCombatMotion(Guid);
	UE_LOG(LogWowGameplay, Log, TEXT("Entity %llu died"), Guid);

	// Prevent double-death
	if (DeadEntityGuids.Contains(Guid)) return;

	// Track as dead/lootable
	DeadEntityGuids.Add(Guid);
	ActiveTimedCastSpells.Remove(Guid);
	ActiveChannelSpells.Remove(Guid);
	RestoreCasterWeaponsAfterSpell(Guid);
	RemoveSpellVisuals(Guid, false);
	RemoveCastGlowEffects(Guid);

	// Stop any active spline movement â€” dead entities don't walk
	if (ConnectionManager)
	{
		FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Guid);
		if (Entity)
		{
			Entity->Movement.bHasActiveSpline = false;
			Entity->Movement.SplineWaypoints.Empty();
			Entity->Movement.MoveFlags = 0;
		}
	}

	// Play death animation on the actor
	TObjectPtr<AActor>* ActorPtr = SpawnedEntityActors.Find(Guid);
	if (ActorPtr && *ActorPtr)
	{
		UWowAnimationController* AnimController = FWowCharacterBuilder::GetAnimationController(*ActorPtr);
		if (AnimController)
		{
			AnimController->SetDead(true);
			UE_LOG(LogWowGameplay, Log, TEXT("Playing death animation for entity %llu"), Guid);
		}
	}

	// Stop auto-attack if we were attacking this entity
	if (bIsAutoAttacking && AutoAttackTargetGuid == Guid)
	{
		StopAutoAttack();
	}

    // Corpse lifetime, loot flags and respawn belong to the server. Retain the
    // entity so target frames, aura/health updates and right-clicks still resolve.
    RefreshWorldLabels();
}

void AWowGameplayController::OnEntityDestroyed(uint64 Guid)
{
    DeadEntityGuids.Remove(Guid);

	// A destroyed/despawned unit cannot remain selected. Route this through the
	// normal target setter so the server selection and stock unit frame clear too.
	if (TargetGuid == Guid)
	{
		SetTarget(0);
	}
	ActiveTimedCastSpells.Remove(Guid);
	ActiveChannelSpells.Remove(Guid);
	RestoreCasterWeaponsAfterSpell(Guid);
	RemoveSpellVisuals(Guid, false);
	RemoveCastGlowEffects(Guid);
	if (TObjectPtr<AActor>* RiderPtr = SpawnedEntityRiderActors.Find(Guid); RiderPtr && *RiderPtr)
	{
		(*RiderPtr)->Destroy();
	}
	SpawnedEntityRiderActors.Remove(Guid);
	SpawnedEntityRiderAnimationControllers.Remove(Guid);
	SpawnedEntityAnimationControllers.Remove(Guid);
	SpawnedEntityAppearanceSignatures.Remove(Guid);
	PendingEntityAppearanceSignatures.Remove(Guid);
	PendingEntityAppearanceStableAt.Remove(Guid);

	TObjectPtr<AActor>* ActorPtr = SpawnedEntityActors.Find(Guid);
	const bool bHadSpawnedActor = ActorPtr != nullptr;
	if (ActorPtr && *ActorPtr)
	{
		// r374 (Stefan: the looted chest popped away): game objects fade out like in the original
		// client. The fading actor lives on by itself for under a second; the entity is gone now.
		if (!((*ActorPtr)->Tags.Contains(TEXT("WowGameObjectActor")) && UWowActorFadeComponent::FadeOutAndDestroy(*ActorPtr, 0.8f)))
			(*ActorPtr)->Destroy();
		if (GWowOpenedChestGuid == Guid) GWowOpenedChestGuid = 0;
		UE_LOG(LogWowGameplay, Log, TEXT("Destroyed entity model for GUID %llu"), Guid);
	}
	SpawnedEntityActors.Remove(Guid);
	const bool bRemovedPendingSpawn = PendingEntityModelSpawns.Remove(Guid) > 0;
	EntityModelSpawnAttempts.Remove(Guid);
    EntityModelSpawnNextAttemptAt.Remove(Guid);
	EntityModelSpawnFirstQueuedAt.Remove(Guid);
	VisuallyMovingEntityGuids.Remove(Guid);
    ForgetEntityCombatMotion(Guid);
	if (bHadSpawnedActor || bRemovedPendingSpawn)
	{
		++EntityModelStreamingRevision;
	}

	// Clean up nameplate. The local player's label lives on the persistent pawn, so it must be
	// destroyed explicitly (a relog showed the previous character's name above the new one).
	if (UWidgetComponent* Nameplate = EntityNameplates.FindRef(Guid); IsValid(Nameplate))
		Nameplate->DestroyComponent();
	EntityNameplates.Remove(Guid); WorldLabelAnchors.Remove(Guid);
	QuestStatusQueriesSent.Remove(Guid);
	if (ConnectionManager)
	{
		ConnectionManager->PacketHandler.QuestGiverStatuses.Remove(Guid);
	}

}

void AWowGameplayController::SpawnEntityModel(const FWowEntity& Entity)
{
	CacheWorldResources();
	if (!CachedMpq || !CachedAssetCache) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// Convert entity position to UE coordinates
	FVector UEPos = FVector::ZeroVector;
	if (Entity.Movement.Position != FVector::ZeroVector)
	{
		UEPos = FWowCoordinate::WowToUE(Entity.Movement.Position);
	}
	// Guard against NaN from bad server data
	if (UEPos.ContainsNaN()) UEPos = FVector::ZeroVector;

	const float Orientation = FMath::IsFinite(Entity.Movement.Orientation)
		? Entity.Movement.Orientation : 0.0f;
	FRotator Rot(0.0f, FWowCoordinate::WowOrientationToUEYaw(Orientation), 0.0f);

	AActor* SpawnedActor = nullptr;

	if (Entity.IsPlayer())
	{
		const FWowPlayerEntity& PlayerEntity = static_cast<const FWowPlayerEntity&>(Entity);
		const uint8 RaceId = PlayerEntity.GetRaceId();
		uint8 GenderId = PlayerEntity.GetGenderId();
        // Shapeshifts can set UNIT_FIELD_BYTES_0 gender to neutral (2).
        // Skin and equipment atlases always need the player's native gender.
        if (ConnectionManager)
        {
            if (const uint8* Known = ConnectionManager->PacketHandler.PlayerGenderCache.Find(Entity.Guid))
                GenderId = *Known;
        }
        if (const auto* Race = FDbcStore::Get().ChrRaces().GetById(RaceId))
        {
            const uint32 NativeDisplay = PlayerEntity.GetNativeDisplayId();
            if (NativeDisplay && NativeDisplay == Race->MaleModelID) GenderId = 0;
            else if (NativeDisplay && NativeDisplay == Race->FemaleModelID) GenderId = 1;
        }
        if (GenderId > 1)
        {
            if (const auto* Display = FDbcStore::Get().CreatureDisplayInfo().GetById(PlayerEntity.GetNativeDisplayId()))
                if (const auto* Model = FDbcStore::Get().CreatureModelData().GetById(Display->ModelID))
                {
                    if (Model->ModelPath.Contains(TEXT("Female"))) GenderId = 1;
                    else if (Model->ModelPath.Contains(TEXT("Male"))) GenderId = 0;
                }
        }
        if (GenderId > 1) return; // Name response will retry; never build a white neutral-gender atlas.
        if (ConnectionManager) ConnectionManager->PacketHandler.PlayerGenderCache.Add(Entity.Guid, GenderId);

		if (RaceId > 0)
		{
			FWowCharacterBuilder::FCharacterParams Params =
				BuildPlayerCharacterParams(PlayerEntity, GenderId);
			Params.bUseHDModel = WowIsHdUpgraded(Entity.Guid); // r331: upgraded to HD over time
			SpawnedActor = FWowCharacterBuilder::SpawnCharacterWithEquipment(
				World, CachedMpq, CachedAssetCache, Params, UEPos, Rot);
			ApplyRemoteWeaponPresentation(SpawnedActor, PlayerEntity.GetSheathState(),
				PlayerEntity.GetMountDisplayId() > 0);
			UWowAnimationController* RiderAnimation =
				FWowCharacterBuilder::GetAnimationController(SpawnedActor);

			if (SpawnedActor && PlayerEntity.GetMountDisplayId() > 0)
			{
				AActor* RiderActor = SpawnedActor;
				AActor* MountActor = FWowCharacterBuilder::SpawnCreatureByDisplayId(
					World, CachedMpq, CachedAssetCache, PlayerEntity.GetMountDisplayId(), UEPos, Rot, true);
				if (MountActor)
				{
					if (USkeletalMeshComponent* MountMesh =
						MountActor->FindComponentByClass<USkeletalMeshComponent>())
					{
						if (MountMesh->DoesSocketExist(TEXT("WowMountSeat")))
						{
							RiderActor->AttachToComponent(MountMesh,
								FAttachmentTransformRules::SnapToTargetNotIncludingScale,
								TEXT("WowMountSeat"));
						}
						else
						{
							FVector BoundsOrigin;
							FVector BoundsExtent;
							MountActor->GetActorBounds(false, BoundsOrigin, BoundsExtent, true);
							RiderActor->AttachToActor(MountActor,
								FAttachmentTransformRules::KeepWorldTransform);
							RiderActor->SetActorRelativeLocation(FVector(0.0f, 0.0f,
								FMath::Max(BoundsExtent.Z * 1.35f, 100.0f)));
						}
						if (RiderActor->GetAttachParentActor() == MountActor)
						{
							RiderActor->SetActorRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
						}
					}
					if (RiderAnimation)
					{
						RiderAnimation->PlayAnimationById(EWowAnimId::Mount, true);
					}
					SpawnedEntityRiderActors.Add(Entity.Guid, RiderActor);
					SpawnedEntityRiderAnimationControllers.Add(Entity.Guid, RiderAnimation);
					SpawnedActor = MountActor;
				}
			}

			// r679 (Stefan: two players hanging in the air at the same spot - "could they be on a gryphon we don't
			// draw?"): the spawn line also says how the server has them moving - movement flags (0x02000000 flying,
			// 0x08000000 on a path, 0x200 on a transport), unit flags (0x00100000 on a flight path), and the path.
			UE_LOG(LogWowGameplay, Log,
				TEXT("Spawned player model: GUID=%llu Race=%d Gender=%d Mount=%u Equipment=%d at %s moveFlags=0x%08X unitFlags=0x%08X spline=%d pts %u/%u ms transport=%llu"),
				Entity.Guid, RaceId, GenderId, PlayerEntity.GetMountDisplayId(),
				Params.Equipment.Num(), *UEPos.ToString(), Entity.Movement.MoveFlags, PlayerEntity.GetUnitFlags(),
				Entity.Movement.bHasActiveSpline ? Entity.Movement.SplineWaypoints.Num() : 0,
				uint32(Entity.Movement.SplineElapsed), Entity.Movement.SplineDuration, Entity.Movement.TransportGuid);
		}
	}
	else if (Entity.IsGameObject())
	{
		const FWowGameObjectEntity* GameObject = static_cast<const FWowGameObjectEntity*>(&Entity);
		const uint32 DisplayId = GameObject->GetGameObjectDisplayId();
		const FGameObjectDisplayInfoDbcEntry* DisplayInfo =
			FDbcStore::Get().GameObjectDisplayInfo().GetById(DisplayId);
		if (!DisplayInfo || DisplayInfo->ModelPath.IsEmpty())
		{
			UE_LOG(LogWowGameplay, Warning,
				TEXT("GameObject visual unavailable: GUID=%llu Entry=%u DisplayId=%u"),
				Entity.Guid, Entity.Entry, DisplayId);
			return;
		}

		const FString ModelPath = NormalizeGameObjectModelPath(DisplayInfo->ModelPath);
		const float GameObjectScale = FMath::IsFinite(Entity.Scale) && Entity.Scale > 0.0f
			? Entity.Scale : 1.0f;
		if (ModelPath.EndsWith(TEXT(".wmo"), ESearchCase::IgnoreCase))
		{
			// The WMO renderer consumes ADT-space placement coordinates. Build the
			// equivalent point from the network's centered world coordinates, then
			// apply the authoritative server-facing angle after construction.
			FAdtWmoPlacement Placement;
			Placement.UniqueId = static_cast<uint32>(Entity.Guid & 0xffffffffULL);
			Placement.Position = FVector(
				FWowCoordinate::MAP_ORIGIN - Entity.Movement.Position.Y,
				Entity.Movement.Position.Z,
				FWowCoordinate::MAP_ORIGIN - Entity.Movement.Position.X);
			Placement.Scale = static_cast<uint16>(FMath::Clamp(
				FMath::RoundToInt(GameObjectScale * 1024.0f), 1, 65535));
			SpawnedActor = FWowWmoRenderer::SpawnWmo(
				World, ModelPath, Placement, CachedMpq, CachedAssetCache);
			if (SpawnedActor)
			{
				SpawnedActor->SetActorLocation(UEPos);
				SpawnedActor->SetActorRotation(Rot);
				SpawnedActor->SetActorScale3D(FVector(GameObjectScale));
			}
		}
		else
		{
			// Fishing bobbers (GO type 17) bob on their Stand animation and need something to click:
			// the model has no collision, so the cursor went straight through it (r253, Stefan).
			const bool bFishingBobber = GameObject->GetGameObjectType() == 17;
			// M2 vertices convert with the model's forward (+X) on UE +Y; skeletal models turn their
			// mesh -90 for that, gameobjects did not, so signpost arms and every other gameobject
			// faced 90 degrees off their server orientation (r266, Stefan).
			// r290 (Stefan: shop signs hang along the wall, banners stand into it - all still 90
			// degrees off to the left): game objects take the server facing as is. r266's -90 came
			// from the skeletal-mesh convention, but those meshes are built differently; signpost
			// arms looked right either way because they turn about the pole.
			// r326 (Stefan: signpost arms now float beside the pole): r290 was wrong. M2 vertices go
			// to UE as (Y,X,Z) and world positions as (X,-Y,Z); together that is a -90 degree turn in
			// the object's own frame, the same as ADT doodads (RY - 90). Restored.
			FRotator GameObjectRot(Rot.Pitch, Rot.Yaw - 90.0f, Rot.Roll);
			// Full server rotation when the object has one (r285, Stefan: signpost arms still
			// wrong). WoW world axes map to UE as (X, -Y, Z), a mirror, so the quaternion becomes
			// (-x, y, -z, w); then the same -90 model turn in the object's own frame.
			// r236 (Stefan 12.10: the King Llane / Daelin plaques stand up instead of lying tilted on
			// their pedestals): the spawn's GAMEOBJECT_PARENTROTATION is applied on top of the local
			// rotation (world = parent * local). Identity or empty parent rotation changes nothing.
			FQuat ParentRot(GameObject->GetParentRotationComponent(0), GameObject->GetParentRotationComponent(1),
				GameObject->GetParentRotationComponent(2), GameObject->GetParentRotationComponent(3));
			const bool bHasParentRot = ParentRot.SizeSquared() > 0.5f
				&& (FMath::Abs(ParentRot.X) > 1e-3f || FMath::Abs(ParentRot.Y) > 1e-3f || FMath::Abs(ParentRot.Z) > 1e-3f);
			UE_LOG(LogWowGameplay, Log, TEXT("GameObject %u rotation: packed=%s (%.3f %.3f %.3f %.3f) parent=(%.3f %.3f %.3f %.3f) orientation=%.3f"),
				Entity.Entry, Entity.Movement.bHasPackedRotation ? TEXT("yes") : TEXT("no"),
				Entity.Movement.PackedRotation.X, Entity.Movement.PackedRotation.Y, Entity.Movement.PackedRotation.Z, Entity.Movement.PackedRotation.W,
				ParentRot.X, ParentRot.Y, ParentRot.Z, ParentRot.W, Orientation);
			if (bHasParentRot)
			{
				ParentRot.Normalize();
				const FQuat Local = Entity.Movement.bHasPackedRotation ? Entity.Movement.PackedRotation
					: FQuat(FVector::UpVector, Orientation);
				const FQuat W = ParentRot * Local;
				const FQuat UeQuat(-W.X, W.Y, -W.Z, W.W);
				GameObjectRot = (UeQuat * FQuat(FRotator(0.0f, -90.0f, 0.0f))).Rotator();
			}
			else if (Entity.Movement.bHasPackedRotation)
			{
				const FQuat& Q = Entity.Movement.PackedRotation;
				const FQuat UeQuat(-Q.X, Q.Y, -Q.Z, Q.W);
				const FRotator FromQuat = (UeQuat * FQuat(FRotator(0.0f, -90.0f, 0.0f))).Rotator();
				// r356 (Stefan: sitting sideways on a chair): many spawns carry an identity (or stale)
				// rotation quaternion and only a real orientation. The server seats the player with
				// that orientation, so an upright object whose quaternion yaw disagrees with it takes
				// the orientation. Tilted objects (signpost arms, leaning props) keep the quaternion.
				const bool bUpright = FMath::Abs(FromQuat.Pitch) < 1.0f && FMath::Abs(FromQuat.Roll) < 1.0f;
				const float YawGap = FMath::Abs(FRotator::NormalizeAxis(FromQuat.Yaw - GameObjectRot.Yaw));
				if (!bUpright || YawGap < 2.0f || FMath::IsNearlyZero(Orientation))
					GameObjectRot = FromQuat;
				else
					UE_LOG(LogWowGameplay, Log, TEXT("GameObject %u: quaternion yaw %.1f disagrees with orientation yaw %.1f - using the orientation"),
						Entity.Entry, FromQuat.Yaw, GameObjectRot.Yaw);
			}
			SpawnedActor = FWowDoodadManager::SpawnWorldM2(
				World, ModelPath, UEPos, GameObjectRot, GameObjectScale,
				CachedMpq, CachedAssetCache,
				static_cast<uint32>(Entity.Guid & 0xffffffffULL), bFishingBobber, bFishingBobber);
			if (SpawnedActor && bFishingBobber)
			{
				if (USphereComponent* Click = NewObject<USphereComponent>(SpawnedActor, TEXT("BobberClick")))
				{
					Click->SetupAttachment(SpawnedActor->GetRootComponent());
					SpawnedActor->AddInstanceComponent(Click);
					Click->SetSphereRadius(40.0f);
					Click->SetRelativeLocation(FVector(0, 0, 10.0f));
					Click->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
					Click->SetCollisionObjectType(ECC_WorldDynamic);
					Click->SetCollisionResponseToAllChannels(ECR_Ignore);
					Click->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
					Click->ComponentTags.AddUnique(TEXT("WowTargetProxy"));
					Click->RegisterComponent();
				}
				// Fishing line from the caster's rod tip to the bobber (r254).
				const uint64 CasterGuid = Entity.GetField64(GameObjectField::CREATED_BY);
				AActor* Fisher = CasterGuid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid
					? GetPawn() : SpawnedEntityActors.FindRef(CasterGuid).Get();
				if (Fisher)
					if (UWowFishingLineComponent* Line = NewObject<UWowFishingLineComponent>(SpawnedActor, TEXT("FishingLine")))
					{
						Line->SetupAttachment(SpawnedActor->GetRootComponent());
						SpawnedActor->AddInstanceComponent(Line);
						Line->SetCaster(Fisher);
						Line->RegisterComponent();
					}
			}
			// Server helper objects (spell focus posts, Midsummer "Fury Trap"s, quest triggers) use
			// SpellObject_InvisibleTrap.m2: a plain white quad (SPELLS\WHITE1.BLP, fully opaque) that
			// the original client never shows. They stood all over Stormwind as grey squares (r247).
			if (SpawnedActor && ModelPath.Contains(TEXT("SpellObject_InvisibleTrap"), ESearchCase::IgnoreCase))
			{
				SpawnedActor->SetActorHiddenInGame(true);
				SpawnedActor->SetActorEnableCollision(false);
			}
		}

		if (SpawnedActor) SpawnedActor->Tags.AddUnique(TEXT("WowGameObjectActor")); // r374: despawn fade
		UE_LOG(LogWowGameplay, Log,
			TEXT("Spawned gameobject model: GUID=%llu Entry=%u DisplayId=%u Path='%s' at %s"),
			Entity.Guid, Entity.Entry, DisplayId, *ModelPath, *UEPos.ToString());
	}
	else
	{
		// NPCs/creatures: spawn by DisplayId
		uint32 DisplayId = Entity.GetDisplayId();
		if (DisplayId == 0)
		{
			DisplayId = Entity.GetNativeDisplayId();
		}

		const FWowMirrorImageData* Mirror = GMirrorImages.Find(Entity.Guid);
		if (!Mirror && (Entity.GetField(UnitField::FLAGS_2) & 0x10u) && !GMirrorImageRequests.Contains(Entity.Guid))
		{
			GMirrorImageRequests.Add(Entity.Guid); // UNIT_FLAG2_MIRROR_IMAGE: ask how it is dressed
			if (ConnectionManager) ConnectionManager->SendGetMirrorImageData(Entity.Guid);
		}
		if (Mirror && Mirror->bHasAppearance)
		{
			SpawnedActor = FWowCharacterBuilder::SpawnCharacterWithEquipment(
				World, CachedMpq, CachedAssetCache, BuildMirrorImageCharacterParams(*Mirror, Entity), UEPos, Rot);
			UE_LOG(LogWowGameplay, Log, TEXT("Spawned dressed (mirror image) unit: GUID=%llu race=%u gender=%u"),
				Entity.Guid, Mirror->Race, Mirror->Gender);
		}
		else if (DisplayId > 0)
		{
			// r331: a humanoid NPC upgraded to HD is built like a player from its display extra.
			FWowCharacterBuilder::FCharacterParams NpcHd;
			if (WowIsHdUpgraded(Entity.Guid) && FWowCharacterBuilder::BuildNpcCharacterParams(DisplayId, NpcHd))
			{
				NpcHd.bUseHDModel = true;
				SpawnedActor = FWowCharacterBuilder::SpawnCharacterWithEquipment(
					World, CachedMpq, CachedAssetCache, NpcHd, UEPos, Rot);
			}
			if (!SpawnedActor)
			SpawnedActor = FWowCharacterBuilder::SpawnCreatureByDisplayId(
				World, CachedMpq, CachedAssetCache, DisplayId, UEPos, Rot, true);
            if (SpawnedActor && FMath::IsFinite(Entity.Scale) && Entity.Scale > 0.0f)
                SpawnedActor->SetActorScale3D(FVector(WowUnitActorScale(Entity)));

			UE_LOG(LogWowGameplay, Log, TEXT("Spawned creature model: GUID=%llu DisplayId=%d at %s"),
				Entity.Guid, DisplayId, *UEPos.ToString());
		}
	}

	if (SpawnedActor)
	{
		if (UWowAnimationController* AnimationController =
			FWowCharacterBuilder::GetAnimationController(SpawnedActor))
		{
			SpawnedEntityAnimationControllers.Add(Entity.Guid, AnimationController);
		}
		// Quadrupeds and mounts lean with the slope so their legs reach the ground.
		if (FWowGroundTilt Tilt; FWowGroundTilt::MakeForQuadruped(SpawnedActor, Tilt)) GroundTilts.Add(SpawnedActor, Tilt);
		// Tag actor with GUID for targeting
		SpawnedActor->Tags.Add(FName(*FString::Printf(TEXT("%llu"), Entity.Guid)));
        SpawnedEntityActors.Add(Entity.Guid, SpawnedActor);
        if (Entity.IsUnit() && (DeadEntityGuids.Contains(Entity.Guid)
            || Entity.GetFieldByte(UnitField::BYTES_1, 0)==WowStandState::DEAD
            || (Entity.GetMaxHealth()>0 && Entity.GetHealth()<=0)))
        {
            DeadEntityGuids.Add(Entity.Guid);
            if (auto* Anim = FWowCharacterBuilder::GetAnimationController(SpawnedActor)) Anim->ShowCorpsePose();
        }
        SyncAuraVisuals(Entity.Guid);
		if (Entity.IsPlayer())
		{
			SpawnedEntityAppearanceSignatures.Add(Entity.Guid,
				GetPlayerAppearanceSignature(static_cast<const FWowPlayerEntity&>(Entity)));
			PendingEntityAppearanceSignatures.Remove(Entity.Guid);
			PendingEntityAppearanceStableAt.Remove(Entity.Guid);
		}
		PendingEntityModelSpawns.Remove(Entity.Guid);
		EntityModelSpawnAttempts.Remove(Entity.Guid);
        EntityModelSpawnNextAttemptAt.Remove(Entity.Guid);
		EntityModelSpawnFirstQueuedAt.Remove(Entity.Guid);
		++EntityModelStreamingRevision;

		const bool bPhysicalEntityCollision = Entity.IsGameObject() ||
			FWowClientPreferences::IsNpcAndPlayerCollisionEnabled();
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(SpawnedActor);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (PrimitiveComponent)
			{
				PrimitiveComponent->SetCollisionResponseToChannel(
					ECC_Pawn, bPhysicalEntityCollision ? ECR_Block : ECR_Ignore);
				if (!Entity.IsGameObject())
				{
					PrimitiveComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
				}
			}
		}

		if (Entity.IsGameObject())
		{
			const FWowGameObjectEntity* GameObject = static_cast<const FWowGameObjectEntity*>(&Entity);
			const FGameObjectDisplayInfoDbcEntry* DisplayInfo =
				FDbcStore::Get().GameObjectDisplayInfo().GetById(GameObject->GetGameObjectDisplayId());
			FVector BoxCenter(0.0f, 0.0f, 100.0f);
			FVector BoxExtent(100.0f, 100.0f, 100.0f);
			if (DisplayInfo)
			{
				const float GameObjectScale = FMath::IsFinite(Entity.Scale) && Entity.Scale > 0.0f
					? Entity.Scale : 1.0f;
				const float ActorRootScale = FMath::Max(
					SpawnedActor->GetActorScale3D().GetAbsMax(), KINDA_SMALL_NUMBER);
				const float LocalBoundsScale = GameObjectScale / ActorRootScale;
				BoxCenter = GameObjectModelPointToUELocal(
					(DisplayInfo->BoundsMin + DisplayInfo->BoundsMax) * 0.5f,
					LocalBoundsScale);
				BoxExtent = GameObjectModelPointToUELocal(
					(DisplayInfo->BoundsMax - DisplayInfo->BoundsMin).GetAbs() * 0.5f,
					LocalBoundsScale).GetAbs();
				BoxExtent.X = FMath::Clamp(BoxExtent.X, 40.0f, 10000.0f);
				BoxExtent.Y = FMath::Clamp(BoxExtent.Y, 40.0f, 10000.0f);
				BoxExtent.Z = FMath::Clamp(BoxExtent.Z, 40.0f, 10000.0f);
			}

			UBoxComponent* TargetBox = NewObject<UBoxComponent>(SpawnedActor, TEXT("TargetBox"));
			if (TargetBox)
			{
				TargetBox->SetupAttachment(SpawnedActor->GetRootComponent());
				TargetBox->SetRelativeLocation(BoxCenter);
				TargetBox->SetBoxExtent(BoxExtent);
				TargetBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				TargetBox->SetCollisionObjectType(ECC_WorldDynamic);
				TargetBox->SetCollisionResponseToAllChannels(ECR_Ignore);
				TargetBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
				TargetBox->SetHiddenInGame(true);
                TargetBox->ComponentTags.AddUnique(TEXT("WowTargetProxy"));
				TargetBox->RegisterComponent();
			}
		}
		else
		{
			// Large creatures (wings, long arms, dragons) were only clickable on a thin
			// 55 cm capsule around the middle. Give them a box over their whole body in
			// actor space; it never blocks pawns, only the click trace.
			if(auto* Body=SpawnedActor->FindComponentByClass<USkeletalMeshComponent>())
			{
				const FBoxSphereBounds Local=Body->CalcBounds(Body->GetRelativeTransform());
				const FVector Ext=Local.BoxExtent;
				if(FMath::Max(Ext.X,Ext.Y)>70.f)
				{
					if(UBoxComponent* WideBox=NewObject<UBoxComponent>(SpawnedActor,TEXT("TargetBodyBox")))
					{
						WideBox->SetupAttachment(SpawnedActor->GetRootComponent());
						WideBox->SetRelativeLocation(Local.Origin);
						WideBox->SetBoxExtent(FVector(Ext.X*.85f,Ext.Y*.85f,Ext.Z*.9f));
						WideBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
						WideBox->SetCollisionObjectType(ECC_WorldDynamic);
						WideBox->SetCollisionResponseToAllChannels(ECR_Ignore);
						WideBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
						WideBox->SetHiddenInGame(true);
						WideBox->ComponentTags.AddUnique(TEXT("WowTargetProxy"));
						WideBox->RegisterComponent();
					}
				}
			}
			// Add a capsule for unit click targeting. Pawn blocking remains an
			// opt-in client experiment and does not affect visibility traces.
			UCapsuleComponent* TargetCapsule = NewObject<UCapsuleComponent>(SpawnedActor, TEXT("TargetCapsule"));
			if (TargetCapsule)
			{
				TargetCapsule->SetupAttachment(SpawnedActor->GetRootComponent());
                float Radius=35.f,HalfHeight=90.f;
                FVector Center(0,0,90);
                // Small units (r243, Stefan): a flower creature next to a player got a 25 cm
                // minimum radius (or the 35x90 cm human default when its model is not a
                // skeletal mesh), so its click capsule covered the player standing behind it.
                // Use the model of either kind, and let tiny models keep a tiny capsule.
                UMeshComponent* Body=SpawnedActor->FindComponentByClass<USkeletalMeshComponent>();
                if(!Body) Body=SpawnedActor->FindComponentByClass<UStaticMeshComponent>();
                if(Body)
                {
                    const FBoxSphereBounds Bounds=Body->CalcBounds(Body->GetComponentTransform());
                    const float Scale=FMath::Max(.01f,SpawnedActor->GetActorScale3D().GetAbsMax());
                    Center=SpawnedActor->GetActorTransform().InverseTransformPosition(Bounds.Origin);
                    const FVector Extent=Bounds.BoxExtent/Scale;
                    // Cover the visible torso/arms, not only the thinner body depth.
                    const float MinRadius=FMath::Clamp(float(Extent.Z)*.6f,10.f,25.f);
                    Radius=FMath::Clamp(float(FMath::Max(Extent.X,Extent.Y))*.8f,MinRadius,55.f);
                    HalfHeight=FMath::Max(Radius,float(Extent.Z));
                }
                TargetCapsule->SetCapsuleSize(Radius,HalfHeight);
                TargetCapsule->SetRelativeLocation(Center);
				TargetCapsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				TargetCapsule->SetCollisionObjectType(ECC_Pawn);
				TargetCapsule->SetCollisionResponseToAllChannels(ECR_Ignore);
				TargetCapsule->SetCollisionResponseToChannel(
					ECC_Pawn, bPhysicalEntityCollision ? ECR_Block : ECR_Ignore);
				TargetCapsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
				TargetCapsule->ShapeColor = FColor::Green;
				TargetCapsule->SetHiddenInGame(true);
                TargetCapsule->ComponentTags.AddUnique(TEXT("WowTargetProxy"));
				TargetCapsule->RegisterComponent();
			}

			EnsureEntityNameplate(Entity, SpawnedActor);
		}
	}
}

void AWowGameplayController::ProcessPendingEntityAppearanceUpdates(float DeltaTime)
{
	// r343: guild tabard designs. Ask for the design of every guild whose tabard is in view and
	// rebuild the wearers once it arrives (remote players through the pending-appearance queue).
	static uint32 GSeenGuildTabardRevision = 0;
	static float GGuildTabardScan = 0.0f;
	GGuildTabardScan += DeltaTime;
	if (ConnectionManager && (FWowPacketHandler::GetGuildTabardRevision() != GSeenGuildTabardRevision || GGuildTabardScan > 2.0f))
	{
		GSeenGuildTabardRevision = FWowPacketHandler::GetGuildTabardRevision();
		GGuildTabardScan = 0.0f;
		auto& Handler = ConnectionManager->PacketHandler;
		const float ScanTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		auto WearsGuildTabard = [](const FWowPlayerEntity& Wearer)
		{
			return Wearer.GetField(PlayerField::VISIBLE_ITEM_BASE + PlayerField::EQUIPMENT_SLOT_TABARD * 2) == 5976;
		};
		for (const TPair<uint64, TObjectPtr<AActor>>& Spawned : SpawnedEntityActors)
		{
			const FWowEntity* Entity = Handler.EntityManager.Find(Spawned.Key);
			if (!Entity || !Entity->IsPlayer()) continue;
			const FWowPlayerEntity& TabardPlayer = static_cast<const FWowPlayerEntity&>(*Entity);
			if (!WearsGuildTabard(TabardPlayer)) continue;
			if (const uint32 GuildId = TabardPlayer.GetField(0x97); GuildId && !FWowPacketHandler::GetGuildTabardDesign(GuildId))
				Handler.ResolveGuildName(GuildId);
			const uint32 Signature = GetPlayerAppearanceSignature(TabardPlayer);
			const uint32* Built = SpawnedEntityAppearanceSignatures.Find(Spawned.Key);
			if (Built && *Built != Signature && !PendingEntityAppearanceSignatures.Contains(Spawned.Key))
			{
				PendingEntityAppearanceSignatures.Add(Spawned.Key, Signature);
				PendingEntityAppearanceStableAt.Add(Spawned.Key, ScanTime);
			}
		}
		if (const FWowPlayerEntity* Me = Handler.EntityManager.GetLocalPlayer())
			if (AWowPlayerCharacter* LocalPawn = Cast<AWowPlayerCharacter>(GetPawn()))
			{
				const bool bTabard = WearsGuildTabard(*Me);
				const uint32 GuildId = Me->GetField(0x97);
				if (bTabard && GuildId && !FWowPacketHandler::GetGuildTabardDesign(GuildId)) Handler.ResolveGuildName(GuildId);
				const uint32 Design = bTabard ? FWowPacketHandler::GetGuildTabardDesign(GuildId) : 0u;
				if (Design != LocalPawn->GuildTabardDesign)
				{
					LocalPawn->GuildTabardDesign = Design;
					if (bTabard) RebuildLocalAppearanceFromFields(0);
				}
			}
	}

	if (!ConnectionManager || PendingEntityAppearanceSignatures.IsEmpty())
	{
		return;
	}

	EntityAppearanceRebuildTimer += DeltaTime;
	if (EntityAppearanceRebuildTimer < 0.25f)
	{
		return;
	}
	EntityAppearanceRebuildTimer = 0.0f;

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	uint64 GuidToRebuild = 0;
	uint64 GuidToDiscard = 0;
	uint64 GuidToRestabilize = 0;
	uint32 SignatureToRestabilize = 0;
	for (const TPair<uint64, uint32>& Pending : PendingEntityAppearanceSignatures)
	{
		if (Now < PendingEntityAppearanceStableAt.FindRef(Pending.Key))
		{
			continue;
		}

		const FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Pending.Key);
		if (!Entity || !Entity->IsPlayer())
		{
			GuidToDiscard = Pending.Key;
			break;
		}

		const uint32 CurrentSignature = GetPlayerAppearanceSignature(
			static_cast<const FWowPlayerEntity&>(*Entity));
		if (CurrentSignature != Pending.Value)
		{
			GuidToRestabilize = Pending.Key;
			SignatureToRestabilize = CurrentSignature;
			break;
		}

		if (SpawnedEntityAppearanceSignatures.FindRef(Pending.Key) != CurrentSignature)
		{
			GuidToRebuild = Pending.Key;
			break;
		}
		GuidToDiscard = Pending.Key;
		break;
	}

	if (GuidToRestabilize != 0)
	{
		PendingEntityAppearanceSignatures.Add(GuidToRestabilize, SignatureToRestabilize);
		PendingEntityAppearanceStableAt.Add(GuidToRestabilize, Now + 0.75f);
		return;
	}

	if (GuidToDiscard != 0)
	{
		PendingEntityAppearanceSignatures.Remove(GuidToDiscard);
		PendingEntityAppearanceStableAt.Remove(GuidToDiscard);
		return;
	}

	if (GuidToRebuild == 0)
	{
		return;
	}

	PendingEntityAppearanceSignatures.Remove(GuidToRebuild);
	PendingEntityAppearanceStableAt.Remove(GuidToRebuild);
	if (TObjectPtr<AActor>* Rider = SpawnedEntityRiderActors.Find(GuidToRebuild); Rider && *Rider)
	{
		(*Rider)->Destroy();
	}
	SpawnedEntityRiderActors.Remove(GuidToRebuild);
	SpawnedEntityRiderAnimationControllers.Remove(GuidToRebuild);
	if (TObjectPtr<AActor>* Actor = SpawnedEntityActors.Find(GuidToRebuild); Actor && *Actor)
	{
		(*Actor)->Destroy();
	}
	SpawnedEntityActors.Remove(GuidToRebuild);
	SpawnedEntityAnimationControllers.Remove(GuidToRebuild);
	SpawnedEntityAppearanceSignatures.Remove(GuidToRebuild);
	EntityNameplates.Remove(GuidToRebuild); WorldLabelAnchors.Remove(GuidToRebuild);
	QueueEntityModelSpawn(GuidToRebuild);
}

int32 AWowGameplayController::GetReadyPendingEntityModelSpawnCount() const
{
	if (!ConnectionManager)
	{
		return 0;
	}

	int32 ReadyCount = 0;
	const TMap<uint64, TSharedPtr<FWowEntity>>& Entities =
		ConnectionManager->PacketHandler.EntityManager.GetAll();
	for (const uint64 Guid : PendingEntityModelSpawns)
	{
		const TSharedPtr<FWowEntity>* FoundEntity = Entities.Find(Guid);
		const FWowEntity* Entity = FoundEntity && FoundEntity->IsValid() ? FoundEntity->Get() : nullptr;
		if (!Entity || (!Entity->IsUnit() && !Entity->IsPlayer() && !Entity->IsGameObject()))
		{
			continue;
		}

		const bool bHasModelIdentity = Entity->IsGameObject()
			? static_cast<const FWowGameObjectEntity*>(Entity)->GetGameObjectDisplayId() > 0
			: (Entity->IsPlayer()
				? static_cast<const FWowUnitEntity*>(Entity)->GetRaceId() > 0
				: (Entity->GetDisplayId() > 0 || Entity->GetNativeDisplayId() > 0));
		if (bHasModelIdentity && IsEntityModelInStreamingRange(*Entity)
            && (!EntityModelSpawnNextAttemptAt.Contains(Guid)
                || EntityModelSpawnNextAttemptAt.FindRef(Guid) <= (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f)))
		{
			++ReadyCount;
		}
	}
    for(const auto& Pending:PendingEntityAppearanceSignatures)
    {
        if(PendingEntityModelSpawns.Contains(Pending.Key)) continue;
        const auto* Entity=ConnectionManager->PacketHandler.EntityManager.Find(Pending.Key);
        if(Entity && Entity->IsPlayer() && IsEntityModelInStreamingRange(*Entity) &&
            SpawnedEntityAppearanceSignatures.FindRef(Pending.Key)!=GetPlayerAppearanceSignature(static_cast<const FWowPlayerEntity&>(*Entity)))
            ++ReadyCount;
    }
	return ReadyCount;
}

bool AWowGameplayController::IsEntityModelInStreamingRange(const FWowEntity& Entity) const
{
	const APawn* PlayerPawn = GetPawn();
	if (!PlayerPawn || Entity.Movement.Position.IsNearlyZero())
	{
		return true;
	}

	const FVector EntityPosition = FWowCoordinate::WowToUE(Entity.Movement.Position);
	return FVector::DistSquared2D(PlayerPawn->GetActorLocation(), EntityPosition) <=
		FMath::Square(EntityModelStreamingRangeCm);
}

void AWowGameplayController::QueueEntityModelSpawn(uint64 Guid)
{
	if (Guid == 0 || SpawnedEntityActors.Contains(Guid)) return;
	const bool bAlreadyPending = PendingEntityModelSpawns.Contains(Guid);
	if (!EntityModelSpawnFirstQueuedAt.Contains(Guid))
	{
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		EntityModelSpawnFirstQueuedAt.Add(Guid, Now);
	}
	PendingEntityModelSpawns.Add(Guid);
	if (!bAlreadyPending)
	{
		++EntityModelStreamingRevision;
	}
}

void AWowGameplayController::ProcessPendingEntityModelSpawns(float DeltaTime)
{
    FWowPerformanceScope Timing(TEXT("ProcessPendingEntityModelSpawns"), TEXT("world"));
    if (!ConnectionManager) return;
    QuestSparkleRefresh += DeltaTime;
    if (QuestSparkleRefresh >= 0.25f && GetPawn())
    {
        QuestSparkleRefresh = 0;
        int32 Created = 0;
        const bool Enabled = FWowClientPreferences::GetWorldOption(TEXT("questsparkles"));
        for (const auto& Pair : SpawnedEntityActors)
        {
            AActor* Actor = Pair.Value.Get();
            const auto* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Pair.Key);
            // Lootable corpses glitter softly, so loot left behind is easy to spot.
            if (IsValid(Actor) && Entity && Entity->IsUnit())
            {
                const auto& Unit = static_cast<const FWowUnitEntity&>(*Entity);
                const bool bLoot = Enabled && (DeadEntityGuids.Contains(Unit.Guid) || (Unit.GetMaxHealth() > 0 && Unit.GetHealth() <= 0))
                    && Unit.IsLootable()
                    && FVector::DistSquared(Actor->GetActorLocation(), GetPawn()->GetActorLocation()) < FMath::Square(4000.f);
                UWowM2ParticleComponent* LootSparkles = nullptr;
                TInlineComponentArray<UWowM2ParticleComponent*> Existing(Actor);
                for (auto* Component : Existing)
                    if (Component->ComponentHasTag(TEXT("WowLootSparkles"))) { LootSparkles = Component; break; }
                if (!bLoot) { if (LootSparkles) LootSparkles->DestroyComponent(); continue; }
                if (LootSparkles || Created >= 2) continue;
                FVector Origin, Extent;
                Actor->GetActorBounds(true, Origin, Extent);
                const float Height = FMath::Clamp(Extent.Z * 1.2f, 50.f, 140.f);
                LootSparkles = NewObject<UWowM2ParticleComponent>(Actor);
                LootSparkles->ComponentTags.Add(TEXT("WowLootSparkles"));
                LootSparkles->SetupAttachment(Actor->GetRootComponent());
                LootSparkles->RegisterComponent();
                LootSparkles->CreateQuestSparkles(Height);
                LootSparkles->SetWorldLocation(Origin - FVector(0, 0, Extent.Z * .5f));
                ++Created;
                continue;
            }
            if (!IsValid(Actor) || !Entity || !Entity->IsGameObject()) continue;
            const uint32 Flags = static_cast<const FWowGameObjectEntity*>(Entity)->GetGameObjectFlags();
            const uint32 Dynamic = Entity->GetField(GameObjectField::DYNAMIC) & 0xffffu;
            const uint8 Type = Entity->GetFieldByte(GameObjectField::BYTES_1, 1);
            // GO_DYNFLAG_LO_SPARKLE (0x08): the server marks objects a quest still
            // needs. Ordinary chests, herbs and ore do not glitter in the original client.
            (void)Type;
            const bool Show = Enabled && !(Flags & 0x11u)
                && (Dynamic & 0x08u) != 0
                && FVector::DistSquared(Actor->GetActorLocation(), GetPawn()->GetActorLocation()) < FMath::Square(5000.f);
            // Mining veins and herbs: a sharp star running along the model's hard edges instead
            // of the model's own soft glow blobs, which floated beside the rock.
            {
                bool bNode = false;
                if (const auto* Display = FDbcStore::Get().GameObjectDisplayInfo().GetById(
                        static_cast<const FWowGameObjectEntity*>(Entity)->GetGameObjectDisplayId()))
                {
                    // r415 (Stefan: only mining nodes glitter - chests and herbs do not): ore veins
                    // only (Copper_MiningNode_01, Thorium..., Saronite..., Fel_Iron_Ore...).
                    const FString& Path = Display->ModelPath;
                    bNode = Path.Contains(TEXT("SkillActivated"), ESearchCase::IgnoreCase)
                        && (Path.Contains(TEXT("MiningNode"), ESearchCase::IgnoreCase) || Path.Contains(TEXT("Mineral"), ESearchCase::IgnoreCase)
                            || Path.Contains(TEXT("_Ore"), ESearchCase::IgnoreCase) || Path.Contains(TEXT("Vein"), ESearchCase::IgnoreCase));
                }
                const bool bNear = FVector::DistSquared(Actor->GetActorLocation(), GetPawn()->GetActorLocation()) < FMath::Square(4000.f);
                UWowNodeGlintComponent* Glint = Actor->FindComponentByClass<UWowNodeGlintComponent>();
                if (bNode && bNear && !Glint && Created < 2)
                    if (UStaticMeshComponent* NodeMesh = Actor->FindComponentByClass<UStaticMeshComponent>())
                    {
                        Glint = NewObject<UWowNodeGlintComponent>(Actor);
                        Glint->SetupAttachment(Actor->GetRootComponent());
                        Glint->RegisterComponent();
                        Glint->InitFromMesh(NodeMesh);
                        TInlineComponentArray<UWowM2ParticleComponent*> OwnGlow(Actor);
                        for (UWowM2ParticleComponent* Glow : OwnGlow)
                            if (!Glow->ComponentHasTag(TEXT("WowQuestSparkles"))) Glow->SetVisibility(false);
                        ++Created;
                    }
                if (Glint && (!bNode || !bNear)) Glint->DestroyComponent();
            }
            UWowM2ParticleComponent* Sparkles = nullptr;
            TInlineComponentArray<UWowM2ParticleComponent*> Components(Actor);
            for (auto* Component : Components)
                if (Component->ComponentHasTag(TEXT("WowQuestSparkles"))) { Sparkles = Component; break; }
            if (!Show)
            {
                if (Sparkles) Sparkles->DestroyComponent();
                continue;
            }
            if (Sparkles || Created >= 2) continue;
            FVector Origin, Extent;
            Actor->GetActorBounds(true, Origin, Extent);
            const float Height = FMath::Clamp(Extent.Z * 2.f, 60.f, 180.f);
            Sparkles = NewObject<UWowM2ParticleComponent>(Actor);
            Sparkles->ComponentTags.Add(TEXT("WowQuestSparkles"));
            Sparkles->SetupAttachment(Actor->GetRootComponent());
            Sparkles->RegisterComponent();
            Sparkles->CreateQuestSparkles(Height);
            Sparkles->SetWorldLocation(Origin + FVector(0,0,Height*.3f));
            ++Created;
        }
    }
    // Reconcile authoritative entities even if CREATE preceded event binding or
    // an actor was destroyed locally. Static NPCs need not send another update.
    EntityModelReconcileTimer += DeltaTime;
    if (EntityModelReconcileTimer >= 2.0f)
    {
        EntityModelReconcileTimer = 0.0f;
        int32 Recovered = 0;
        for (const auto& Pair : ConnectionManager->PacketHandler.EntityManager.GetAll())
        {
            const FWowEntity* Entity = Pair.Value.Get();
            if (!Entity || Entity->Guid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid
                || (!Entity->IsUnit() && !Entity->IsGameObject())) continue;
            const auto* Actor = SpawnedEntityActors.Find(Entity->Guid);
            if (Actor && IsValid(Actor->Get())) continue;
            if (Actor)
            {
                SpawnedEntityActors.Remove(Entity->Guid);
                SpawnedEntityAnimationControllers.Remove(Entity->Guid);
                EntityNameplates.Remove(Entity->Guid);
                WorldLabelAnchors.Remove(Entity->Guid);
            }
            if (!PendingEntityModelSpawns.Contains(Entity->Guid))
            {
                OnEntityCreated(*Entity);
                ++Recovered;
            }
        }
        if (Recovered > 0)
            UE_LOG(LogWowGameplay, Log, TEXT("Entity visual reconciliation: queued %d missing server entities"), Recovered);
    }
    if (PendingEntityModelSpawns.IsEmpty()) return;

	EntityModelSpawnRetryTimer += DeltaTime;
	// Drain the initial queue one model per frame behind the loading screen.
	if (bGameplayHudVisible && EntityModelSpawnRetryTimer < 0.10f) return;
	EntityModelSpawnRetryTimer = 0.0f;

	// World/login initialization can deliver CREATE_OBJECT before the MPQ and
	// converted-asset caches are attached. Waiting here must not consume one of
	// the model's real failure attempts.
	CacheWorldResources();
	if (!CachedMpq || !CachedAssetCache || !GetWorld()) return;

	// Resolve at most one model per pass. Character/creature conversion is heavy,
	// and a crowded UPDATE_OBJECT packet must never stall the game thread.
	uint64 GuidToTry = 0;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	for (const uint64 Guid : PendingEntityModelSpawns)
	{
		const FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Guid);
		if (!Entity || (!Entity->IsUnit() && !Entity->IsPlayer() && !Entity->IsGameObject()))
		{
			GuidToTry = Guid;
			break;
		}

		const bool bHasModelIdentity = Entity->IsGameObject()
			? static_cast<const FWowGameObjectEntity*>(Entity)->GetGameObjectDisplayId() > 0
			: (Entity->IsPlayer()
				? static_cast<const FWowUnitEntity*>(Entity)->GetRaceId() > 0
				: (Entity->GetDisplayId() > 0 || Entity->GetNativeDisplayId() > 0));
		if (bHasModelIdentity && IsEntityModelInStreamingRange(*Entity)
            && (!EntityModelSpawnNextAttemptAt.Contains(Guid) || EntityModelSpawnNextAttemptAt.FindRef(Guid) <= Now))
		{
			const APawn* PlayerPawn = GetPawn();
			const float DistanceSquared = PlayerPawn && !Entity->Movement.Position.IsNearlyZero()
				? FVector::DistSquared2D(PlayerPawn->GetActorLocation(),
					FWowCoordinate::WowToUE(Entity->Movement.Position))
				: 0.0f;
			if (DistanceSquared < ClosestDistanceSquared)
			{
				ClosestDistanceSquared = DistanceSquared;
				GuidToTry = Guid;
			}
		}


	}

	if (GuidToTry == 0) return;
	const FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(GuidToTry);
	if (!Entity || (!Entity->IsUnit() && !Entity->IsPlayer() && !Entity->IsGameObject()))
	{
		if (PendingEntityModelSpawns.Remove(GuidToTry) > 0)
		{
			++EntityModelStreamingRevision;
		}
		EntityModelSpawnAttempts.Remove(GuidToTry);
        EntityModelSpawnNextAttemptAt.Remove(GuidToTry);
		EntityModelSpawnFirstQueuedAt.Remove(GuidToTry);
		return;
	}

	const bool bHasModelIdentity = Entity->IsGameObject()
		? static_cast<const FWowGameObjectEntity*>(Entity)->GetGameObjectDisplayId() > 0
		: (Entity->IsPlayer()
			? static_cast<const FWowUnitEntity*>(Entity)->GetRaceId() > 0
			: (Entity->GetDisplayId() > 0 || Entity->GetNativeDisplayId() > 0));
	if (!bHasModelIdentity || !IsEntityModelInStreamingRange(*Entity)) return;

    // r566: a player whose race/gender model is not parsed yet waits for the worker that parses it
    // (no failed attempt counted), instead of parsing it here on the game thread.
    if (Entity->IsPlayer() && !FWowCharacterBuilder::PrewarmCharacterModel(CachedMpq,
            static_cast<FWowCharacterBuilder::ERace>(static_cast<const FWowUnitEntity*>(Entity)->GetRaceId()),
            static_cast<FWowCharacterBuilder::EGender>(Entity->GetFieldByte(UnitField::BYTES_0, 2))))
    {
        EntityModelSpawnNextAttemptAt.Add(GuidToTry, Now + 0.05f);
        return;
    }
    const double SpawnStarted=FPlatformTime::Seconds();
	SpawnEntityModel(*Entity);
    const double SpawnMs=(FPlatformTime::Seconds()-SpawnStarted)*1000.0;
    if(SpawnMs > 25.0)
        UE_LOG(LogWowGameplay, Log, TEXT("Entity visual build: guid=%llu %.1f ms, pending=%d, loading=%d"),
            GuidToTry,SpawnMs,PendingEntityModelSpawns.Num(),!bGameplayHudVisible);
	if (!SpawnedEntityActors.Contains(GuidToTry))
	{
		int32& Attempts = EntityModelSpawnAttempts.FindOrAdd(GuidToTry);
		++Attempts;
        // A transient conversion failure must not permanently hide a server entity.
        // Back off per GUID so a broken nearest model cannot starve the queue.
        const float Delay = FMath::Min(30.0f, FMath::Pow(2.0f, FMath::Min(Attempts, 5)));
        EntityModelSpawnNextAttemptAt.Add(GuidToTry, Now + Delay);
        if (Attempts == 1 || Attempts == 3 || Attempts % 10 == 0)
            UE_LOG(LogWowGameplay, Warning,
                TEXT("Entity visual retry: guid=%llu kind=%s entry=%u display=%u attempt=%d delay=%.1fs"),
                GuidToTry, Entity->GetEntityKindName(), Entity->Entry, Entity->GetDisplayId(), Attempts, Delay);
	}
}

void AWowGameplayController::EnsureEntityNameplate(const FWowEntity& Entity, AActor* HostActor)
{
	if (!ConnectionManager || !HostActor || EntityNameplates.Contains(Entity.Guid)) return;
	// A reused host (the local pawn after a relog or character switch) can still carry the
	// label of the previous character: remove stale labels before adding this one.
	{
		TArray<UWidgetComponent*> Existing;
		HostActor->GetComponents<UWidgetComponent>(Existing);
		for (UWidgetComponent* Old : Existing)
			if (IsValid(Old) && Old->GetName().StartsWith(TEXT("EntityNameplate_")))
			{
				for (auto It = EntityNameplates.CreateIterator(); It; ++It)
					if (It.Value() == Old) It.RemoveCurrent();
				Old->DestroyComponent();
			}
	}

	UWidgetComponent* Component = NewObject<UWidgetComponent>(HostActor, MakeUniqueObjectName(HostActor,
		UWidgetComponent::StaticClass(), FName(*FString::Printf(TEXT("EntityNameplate_%llu"), Entity.Guid))));
	if (!Component) return;

	Component->SetupAttachment(HostActor->GetRootComponent());
	Component->SetWidgetSpace(EWidgetSpace::Screen);
	Component->SetDrawSize(FVector2D(280.0f, 108.0f));
	Component->SetPivot(FVector2D(0.5f,1.0f));
	// For VR the plate becomes a world-space card (see Tick). Default masked one-sided material:
	// SetTwoSided/SetBlendMode rebuild the material instance from the existing instance
	// ("not a valid parent" warnings) and the card then draws nothing.
	Component->SetCastShadow(false);
	if (FWowVRSupport::IsActive()) Component->SetWidgetSpace(EWidgetSpace::World);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // Stay hidden until RefreshWorldLabels applies the current V/interface mode.
    Component->SetVisibility(false);
	Component->SetWidgetClass(UWowNameplateWidget::StaticClass());
	Component->RegisterComponent();
	Component->InitWidget();

	FString EntityName = Entity.IsPlayer() ? TEXT("Unknown player") : TEXT("Unknown");
	if (Entity.IsPlayer())
	{
		if (const FString* CachedName = ConnectionManager->PacketHandler.PlayerNameCache.Find(Entity.Guid))
		{
			EntityName = *CachedName;
		}
	}
	else if (const FString* CachedName = ConnectionManager->PacketHandler.CreatureNameCache.Find(Entity.Entry))
	{
		EntityName = *CachedName;
	}

    const bool bIsHostile = IsWorldUnitAttackable(Entity);

	if (UWowNameplateWidget* Widget = Cast<UWowNameplateWidget>(Component->GetUserWidgetObject()))
	{
		Widget->SetupNameplate(EntityName, Entity.GetHealth(), Entity.GetMaxHealth(), bIsHostile, Entity.IsPlayer());
		if (const uint8* QuestStatus = ConnectionManager->PacketHandler.QuestGiverStatuses.Find(Entity.Guid))
		{
			Widget->UpdateQuestStatus(*QuestStatus);
		}
	}
	EntityNameplates.Add(Entity.Guid, Component);
	if (Entity.IsUnit() && static_cast<const FWowUnitEntity&>(Entity).IsQuestGiver() &&
		!QuestStatusQueriesSent.Contains(Entity.Guid))
	{
		QuestStatusQueriesSent.Add(Entity.Guid);
		ConnectionManager->SendQuestStatusQuery(static_cast<int64>(Entity.Guid));
	}
	UE_LOG(LogWowGameplay, Log, TEXT("Created visible nameplate for entity %llu (%s)"), Entity.Guid, *EntityName);
}

bool AWowGameplayController::IsWorldUnitAttackable(const FWowEntity& Entity) const
{
    if(!Entity.IsUnit() || !ConnectionManager) return false;
    const auto* LocalUnit=ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
    if(!LocalUnit || LocalUnit->Guid==Entity.Guid) return false;
    const auto& Unit=static_cast<const FWowUnitEntity&>(Entity);
    // Non-attackable, player-immune and not-selectable server flags.
    if(Unit.GetUnitFlags() & (0x2u|0x100u|0x02000000u)) return false;
    return FDbcStore::Get().FactionTemplates().Reaction(LocalUnit->GetFactionTemplate(),Unit.GetFactionTemplate())<=4;
}
FLinearColor AWowGameplayController::WorldUnitColor(const FWowEntity& Entity) const
{
    const auto* Local = ConnectionManager ? ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
    if (!Entity.IsUnit() || !Local) return FLinearColor(1.0f,0.8f,0.05f);
    const int32 Reaction = ConnectionManager->GetUnitReaction(&Entity,Local);
    if (Reaction < 4) return FLinearColor(1.0f,0.09f,0.025f);
    if (Reaction == 4) return FLinearColor(1.0f,0.8f,0.05f);
    if (Entity.IsPlayer() && (Entity.GetFieldByte(UnitField::BYTES_2, 1) & 0x05) == 0)
        return FLinearColor(0.0f,0.5f,1.0f);
    return FLinearColor(0.15f,1.0f,0.2f);
}
void AWowGameplayController::OnToggleNameplates()
{
    if (IsWowUiConsumingKeyboardInput()) return;
    FWowClientPreferences::SetNameplateMode((FWowClientPreferences::GetNameplateMode()+1)%3);
    FWowClientPreferences::SetWorldOption(TEXT("nameplateshowenemies"),true);
    FWowClientPreferences::SetWorldOption(TEXT("nameplateshowfriends"),true);
    RefreshWorldLabels();
}
void AWowGameplayController::RefreshWorldLabels()
{
    if(!ConnectionManager || !GetPawn()) return;
    auto& Handler=ConnectionManager->PacketHandler;
    const double MarkerNow=FPlatformTime::Seconds();
    if(MarkerNow>=NextQuestMarkerRefresh) {
        NextQuestMarkerRefresh=MarkerNow+5;
        // Creation can arrive before the session accepts queries, and entering
        // a new area changes the server's visible quest givers without a quest click.
        ConnectionManager->SendQuestStatusMultipleQuery();
    }
    FVector Eye; FRotator EyeRotation;
    GetPlayerViewPoint(Eye, EyeRotation);
    // Labels whose unit is gone (out of range after a teleport, despawned) were skipped here and
    // kept their last screen position and visibility; V did not touch them either (Stefan 10.10:
    // a pile of Coldridge names over the screen in another zone). Remove them.
    TArray<uint64> StaleLabels;
    for(auto& Pair:EntityNameplates)
    {
        UWidgetComponent* Component=Pair.Value;
        const auto* Entity=Handler.EntityManager.Find(Pair.Key);
        if(!IsValid(Component) || !Entity || !Component->GetOwner()) { StaleLabels.Add(Pair.Key); continue; }
        // Names and quest marks over units show within about 40 yards, like the 3.3.5 client
        // (60 m showed whole camps of names through the trees).
        static const float YardCm=(FWowCoordinate::WowToUE(FVector(1,0,0))-FWowCoordinate::WowToUE(FVector::ZeroVector)).Size();
        static const float LabelRangeCm=40.f*YardCm;
        // Quest givers with a yellow "!" or "?" are seen from much farther (100 yards), like the
        // quest marks on the original models; beyond 40 yards only the mark shows, not the name.
        const uint8* GiverStatus=Handler.QuestGiverStatuses.Find(Entity->Guid);
        const bool bKeyQuestMark=GiverStatus && Entity->GetHealth()>0
            && (WowQuestGiverStatus::IsAvailable(*GiverStatus) || WowQuestGiverStatus::IsReadyForTurnIn(*GiverStatus));
        const double LabelDistSq=FVector::DistSquared(Component->GetOwner()->GetActorLocation(),GetPawn()->GetActorLocation());
        const bool bBeyondNameRange=LabelDistSq>=FMath::Square(LabelRangeCm);
        const bool InRange=!bBeyondNameRange || (bKeyQuestMark && LabelDistSq<FMath::Square(100.f*YardCm));
        Component->SetVisibility(InRange);
        if(!InRange) continue;
        AActor* Actor=Component->GetOwner();
        // Anchor on the movement root, not the changing animated mesh bounds.
        AActor* Root=SpawnedEntityActors.FindRef(Entity->Guid).Get();
        if(!IsValid(Root)) Root=Actor;
        auto& Anchor=WorldLabelAnchors.FindOrAdd(Entity->Guid);
        if(Anchor.Root.Get()!=Root)
        {
            Anchor.Root=Root; Anchor.Height=150;
            if(auto* Mesh=Actor->FindComponentByClass<USkeletalMeshComponent>())
                Anchor.Height=FMath::Max(20.f,float(Mesh->Bounds.GetBox().Max.Z-Root->GetActorLocation().Z)+16.f);
            // Mounted players: the name goes above the rider sitting on the mount, not inside it.
            if(AActor* Rider=SpawnedEntityRiderActors.FindRef(Entity->Guid).Get(); IsValid(Rider) && Rider!=Actor)
                if(auto* RiderMesh=Rider->FindComponentByClass<USkeletalMeshComponent>())
                    Anchor.Height=FMath::Max(Anchor.Height,float(RiderMesh->Bounds.GetBox().Max.Z-Root->GetActorLocation().Z)+16.f);
        }
        Component->SetWorldLocation(Root->GetActorLocation()+FVector(0,0,Anchor.Height));
        const FVector SightPoint = Root->GetActorLocation()+FVector(0,0,FMath::Max(10.f,Anchor.Height-20.f));
        if (IsWowWorldOccluded(GetWorld(), Eye, SightPoint, GetPawn(), Root))
        {
            Component->SetVisibility(false);
            continue;
        }
        const bool Attackable=IsWorldUnitAttackable(*Entity);
        const int32 PlateMode=FWowClientPreferences::GetNameplateMode();
        const bool Selected=Entity->Guid==TargetGuid;
        const bool KnownDead=DeadEntityGuids.Contains(Entity->Guid) || FeignDeadEntityGuids.Contains(Entity->Guid) || (Entity->GetMaxHealth()>0 && Entity->GetHealth()<=0);
        const bool ShowName=(PlateMode!=2 || Selected) && !bBeyondNameRange;
        // Grey marks ("?" in progress, "!" not yet available) with names off only close by.
        if(!ShowName && !Selected && !bKeyQuestMark && LabelDistSq>FMath::Square(25.f*YardCm))
        { Component->SetVisibility(false); continue; }
        const bool ShowBar=!KnownDead && (PlateMode==1 || Selected) && !bBeyondNameRange;
        if(auto* Widget=Cast<UWowNameplateWidget>(Component->GetUserWidgetObject()))
        {
            const FString* Name=Entity->IsPlayer()?Handler.PlayerNameCache.Find(Entity->Guid):Handler.CreatureNameCache.Find(Entity->Entry);
            Widget->SetupNameplate(Name?*Name:TEXT(""),KnownDead?0:Entity->GetHealth(),Entity->GetMaxHealth(),Attackable,Entity->IsPlayer());
            FString Subtitle;
            if(Entity->IsPlayer()) Subtitle=Handler.ResolveGuildName(Entity->GetField(0x97)); // PLAYER_GUILDID, build 12340
            else if(const FString* Title=Handler.CreatureTitleCache.Find(Entity->Entry)) Subtitle=*Title;
            Widget->SetReactionColor(WorldUnitColor(*Entity));
            Widget->UpdateSubtitle(Subtitle);
            Widget->SetDisplayOptions(ShowName,ShowBar);
            // Level box and cast bar (WoW: Forever style nameplate).
            const FWowEntity* LocalPlate=Handler.EntityManager.GetLocalPlayer();
            Widget->UpdateLevel(int32(Entity->GetField(UnitField::LEVEL)),LocalPlate?int32(LocalPlate->GetField(UnitField::LEVEL)):0);
            // "?" by the name: this creature counts for an unfinished objective of one of your quests.
            bool bObjectiveTarget=false;
            if(!KnownDead && Entity->Entry)
                for(const FWowQuestLogEntry& Quest:Handler.QuestLog)
                {
                    if(Quest.State==1) continue;
                    for(const FWowQuestObjective& Objective:Quest.Objectives)
                        if(Objective.CreatureOrGOId==Entity->Entry && !(Objective.CreatureOrGOId&0x80000000u)
                            && Objective.Required>0 && Objective.Count<Objective.Required) { bObjectiveTarget=true; break; }
                    if(bObjectiveTarget) break;
                }
            Widget->SetQuestObjective(bObjectiveTarget);
            FString CastName,CastIconPath; double CastStart=0,CastEnd=0; bool bChannel=false;
            const bool bCasting=ShowBar && UIManager && UIManager->GetUnitSpellProgress(Entity->Guid,CastName,CastIconPath,CastStart,CastEnd,bChannel);
            UTexture2D* CastIcon=bCasting && UIManager->GetFrameManager()?UIManager->GetFrameManager()->LoadUITexture(CastIconPath):nullptr;
            Widget->UpdateCast(bCasting,CastName,CastIcon,CastStart,CastEnd,bChannel);
            const uint8* Status=Handler.QuestGiverStatuses.Find(Entity->Guid);
            Widget->UpdateQuestStatus(Entity->GetHealth()==0 ? 0 : Status?*Status:0);
        }
        if(Entity->IsUnit() && static_cast<const FWowUnitEntity*>(Entity)->IsQuestGiver() && !QuestStatusQueriesSent.Contains(Pair.Key))
        { QuestStatusQueriesSent.Add(Pair.Key); ConnectionManager->SendQuestStatusQuery(static_cast<int64>(Pair.Key)); }
    }
    for(const uint64 Guid:StaleLabels)
    {
        if(UWidgetComponent* Component=EntityNameplates.FindRef(Guid); IsValid(Component))
        { Component->SetVisibility(false); Component->DestroyComponent(); }
        EntityNameplates.Remove(Guid); WorldLabelAnchors.Remove(Guid); QuestStatusQueriesSent.Remove(Guid);
    }
    if(StaleLabels.Num()>0)
        UE_LOG(LogWowGameplay,Log,TEXT("World labels: removed %d without a live unit"),StaleLabels.Num());
}

// r606 (Stefan: "mobs still stop weirdly, this Young Scavenger does a little hop in slow motion"):
// the ground lift below was only applied by the network-movement pass, which skips units walking a
// server spline. A wandering mob therefore walked at the bare server height and, the moment its path
// ended, rose to the lifted height through the 14/s smoothing - a slow little hop at every stop (and
// dropped back when the next path began). The lift is now one function, used by both passes.
static void WowApplyRemoteGroundLift(UWorld* World, AActor* Actor, AActor* Rider, uint64 Guid, const FWowEntity& Entity, double Now, FVector& TargetLocation)
{
    if (!(Entity.IsUnit() && World && (Entity.Movement.MoveFlags & (WowMovementFlags::FALLING
			| WowMovementFlags::SWIMMING | WowMovementFlags::FLYING)) == 0
			&& (Entity.IsPlayer() || (Entity.Movement.TransportGuid == 0
				&& (Entity.Movement.MoveFlags & (0x00000200u | 0x00000400u | 0x01000000u | 0x40000000u)) == 0)))) return;
			struct FRemoteLift { FVector2D XY; double Z = 0.0; double At = 0.0; float Lift = 0.0f; };
			static TMap<uint64, FRemoteLift> GRemoteLift;
			FRemoteLift& Lift = GRemoteLift.FindOrAdd(Guid);
			if (FVector2D::DistSquared(Lift.XY, FVector2D(TargetLocation)) > 900.0 || FMath::Abs(Lift.Z - TargetLocation.Z) > 30.0 || Now - Lift.At > 1.0)
			{
				Lift.XY = FVector2D(TargetLocation); Lift.Z = TargetLocation.Z; Lift.At = Now; Lift.Lift = 0.0f;
				FCollisionQueryParams Query(SCENE_QUERY_STAT(WowRemoteGroundLift), false, Actor);
				// r479 (Stefan: another player's Great Elite Elekk floated ~1.5 m over the bank floor):
				// a mounted player's actor is the mount, and the rider is a separate actor attached on
				// top. Only the mount was ignored, so the downward trace landed on the rider (or gear
				// attached to them) and "lifted" the mount onto its own passenger. Ignore everything
				// that rides on this actor.
				{
					TArray<AActor*> Riding;
					Actor->GetAttachedActors(Riding, true, true);
					Query.AddIgnoredActors(Riding);
					if (Rider) Query.AddIgnoredActor(Rider);
				}
				FHitResult Hit;
				bool bOnBuiltFloor = false;
				if (World->LineTraceSingleByChannel(Hit, TargetLocation + FVector(0, 0, 110), TargetLocation - FVector(0, 0, 5), ECC_Pawn, Query))
				{
					// r523: 0.8 -> 0.65, a standing mount sank into the steeper Dun Morogh snow slopes.
					if (Hit.ImpactNormal.Z > 0.65f && Hit.ImpactPoint.Z > TargetLocation.Z + 3.0)
						Lift.Lift = float(Hit.ImpactPoint.Z - TargetLocation.Z);
					else
						bOnBuiltFloor = !GetNameSafe(Hit.GetActor()).StartsWith(TEXT("TerrainTile"));
				}
				// r349 (Stefan: Ruhun stood shoulder-deep in a doorway floor): deeper than 1.1 m the trace
				// started under the floor and passed through its one-sided collision. When the player is
				// not already on a building floor, look again from 2.1 m: a walkable floor at least 0.6 m up
				// with 1.6 m of headroom (never onto a table, lintel or the floor above a cellar).
				if (Lift.Lift <= 0.0f && !bOnBuiltFloor
					&& World->LineTraceSingleByChannel(Hit, TargetLocation + FVector(0, 0, 210), TargetLocation - FVector(0, 0, 5), ECC_Pawn, Query)
					&& Hit.ImpactNormal.Z > 0.8f && Hit.ImpactPoint.Z > TargetLocation.Z + 60.0)
				{
					FHitResult Ceiling;
					const FVector Floor = Hit.ImpactPoint + FVector(0, 0, 5);
					if (!World->LineTraceSingleByChannel(Ceiling, Floor, Floor + FVector(0, 0, 160), ECC_Pawn, Query))
						Lift.Lift = float(Hit.ImpactPoint.Z - TargetLocation.Z);
				}
			}
			TargetLocation.Z += Lift.Lift;
}

void AWowGameplayController::UpdateEntitySplineMovement(float DeltaTime)
{
	if (!ConnectionManager) return;

	// Iterate through all entities and update those with active splines
	FWowEntityManager& EntityManager = ConnectionManager->PacketHandler.EntityManager;

	for (const auto& EntityPair : EntityManager.GetAll())
	{
		FWowEntity* EntityPtr = EntityPair.Value.Get();
		if (!EntityPtr) continue;
		FWowEntity& Entity = *EntityPtr;

		// The server moves the local player itself with a spline for Charge,
		// knockbacks into a path, Heroic Leap and similar: follow it with the pawn,
		// otherwise the character never moves and the server snaps it later.
		if (Entity.Guid == EntityManager.LocalPlayerGuid)
		{
			if (Entity.Movement.bHasActiveSpline && Entity.Movement.SplineWaypoints.Num() > 0)
				UpdateLocalServerSpline(Entity, DeltaTime);
			continue;
		}

		// Skip dead entities â€” corpses don't move
		if (DeadEntityGuids.Contains(Entity.Guid)) continue;

		// Skip entities without active splines
		if (!Entity.Movement.bHasActiveSpline || Entity.Movement.SplineWaypoints.Num() == 0)
			continue;

		// Update spline elapsed time
		Entity.Movement.SplineElapsed += DeltaTime * 1000.0f; // Convert to milliseconds

		const bool bCyclicSpline = (Entity.Movement.SplineFlags & 0x00080000) != 0;
		const bool bOrientationFixed = (Entity.Movement.SplineFlags & 0x00004000) != 0;

		// Check if spline is complete
		if (Entity.Movement.SplineElapsed >= Entity.Movement.SplineDuration && !bCyclicSpline)
		{
			// Move to final position
			if (Entity.Movement.SplineWaypoints.Num() > 0)
			{
				Entity.Movement.Position = Entity.Movement.SplineWaypoints.Last().Position;
			}

			// Preserve the final path tangent unless the packet supplied explicit final facing.
			if (Entity.Movement.SplineWaypoints.Num() > 0)
			{
				const FVector LastWP = Entity.Movement.SplineWaypoints.Last().Position;
				const FVector SecondLastWP = Entity.Movement.SplineWaypoints.Num() > 1
					? Entity.Movement.SplineWaypoints[Entity.Movement.SplineWaypoints.Num() - 2].Position
					: Entity.Movement.SplineStartPosition;
				FVector Dir = FVector(LastWP.X - SecondLastWP.X, LastWP.Y - SecondLastWP.Y, 0.0f);
				if (!Dir.IsNearlyZero())
				{
					if (!bOrientationFixed)
					{
						Entity.Movement.Orientation = FMath::Atan2(Dir.Y, Dir.X);
					}
				}
			}

			if (Entity.Movement.SplineMoveType == 2)
			{
				const FVector FaceDirection = Entity.Movement.SplineFacingPoint - Entity.Movement.Position;
				if (!FaceDirection.IsNearlyZero())
				{
					Entity.Movement.Orientation = FMath::Atan2(FaceDirection.Y, FaceDirection.X);
				}
			}
			else if (Entity.Movement.SplineMoveType == 3)
			{
				if (const FWowEntity* FacingTarget = EntityManager.Find(Entity.Movement.SplineFacingTargetGuid))
				{
					const FVector FaceDirection = FacingTarget->Movement.Position - Entity.Movement.Position;
					if (!FaceDirection.IsNearlyZero())
					{
						Entity.Movement.Orientation = FMath::Atan2(FaceDirection.Y, FaceDirection.X);
					}
				}
			}
			else if (Entity.Movement.SplineMoveType == 4)
			{
				Entity.Movement.Orientation = Entity.Movement.SplineFacingAngle;
			}

			// Clear spline
			Entity.Movement.bHasActiveSpline = false;
			Entity.Movement.SplineWaypoints.Empty();

			UE_LOG(LogWowGameplay, Log, TEXT("Spline movement completed for entity %llu"), Entity.Guid);
		}
		else
		{
			if (bCyclicSpline && Entity.Movement.SplineDuration > 0 &&
				Entity.Movement.SplineElapsed >= Entity.Movement.SplineDuration)
			{
				Entity.Movement.SplineElapsed = FMath::Fmod(
					Entity.Movement.SplineElapsed,
					static_cast<float>(Entity.Movement.SplineDuration));
			}

            FVector Tangent;
            const float Progress=Entity.Movement.SplineDuration>0
                ? Entity.Movement.SplineElapsed/float(Entity.Movement.SplineDuration) : 1.f;
            Entity.Movement.Position=SampleWowLinearSpline(Entity.Movement,Progress,Tangent);
            if(!bOrientationFixed && Tangent.SizeSquared2D()>UE_SMALL_NUMBER)
                Entity.Movement.Orientation=FMath::Atan2(Tangent.Y,Tangent.X);
        }

		// Update the visual actor position
		TObjectPtr<AActor>* ActorPtr = SpawnedEntityActors.Find(Entity.Guid);
		if (ActorPtr && *ActorPtr)
		{
			FVector UEPos = FWowCoordinate::WowToUE(Entity.Movement.Position);
			if (UEPos.ContainsNaN()) continue; // Skip invalid positions
			WowApplyRemoteGroundLift(GetWorld(), *ActorPtr, SpawnedEntityRiderActors.FindRef(Entity.Guid).Get(), Entity.Guid, Entity, FPlatformTime::Seconds(), UEPos); // r606
			const FVector PreviousVisualPosition = (*ActorPtr)->GetActorLocation();
			(*ActorPtr)->SetActorLocation(UEPos);
			if (FVector::DistSquared2D(PreviousVisualPosition, UEPos) > FMath::Square(0.25f))
			{
				VisuallyMovingEntityGuids.Add(Entity.Guid);
                if(DeltaTime>UE_SMALL_NUMBER)
                {
                    // r373 (Stefan: mobs twitch with their legs out when they change direction): a new
                    // spline starts where the server has the mob, not where we drew it, so the first frame
                    // jumps and the measured speed spiked many times over; the stride rate followed and the
                    // run cycle skipped ahead in one frame. Cap it to the unit's own top speed and smooth it.
                    float Speed=FVector::Dist2D(PreviousVisualPosition,UEPos)/DeltaTime;
                    const float Top=FMath::Max3(Entity.Movement.SplineSpeed,Entity.Movement.RunSpeed,Entity.Movement.WalkSpeed)*100.f;
                    if(Top>1.f) Speed=FMath::Min(Speed,Top*1.15f);
                    static TMap<uint64,float> GSmoothedSplineSpeed; // the per-frame map is rebuilt every pass
                    if(const float* Prev=GSmoothedSplineSpeed.Find(Entity.Guid); Prev && *Prev>1.f) Speed=FMath::Lerp(*Prev,Speed,0.35f);
                    GSmoothedSplineSpeed.Add(Entity.Guid,Speed);
                    EntityVisualSpeedsCm.Add(Entity.Guid,Speed);
                }
			}
			const float Orientation = FMath::IsFinite(Entity.Movement.Orientation)
				? Entity.Movement.Orientation : 0.0f;
			const FRotator TargetRotation(
				0.0f, FWowCoordinate::WowOrientationToUEYaw(Orientation), 0.0f);
			(*ActorPtr)->SetActorRotation(FMath::RInterpTo(
				(*ActorPtr)->GetActorRotation(), TargetRotation, DeltaTime, 14.0f));
		}
	}
}

void AWowGameplayController::UpdateLocalServerSpline(FWowEntity& Entity, float DeltaTime)
{
	ACharacter* Char = Cast<ACharacter>(GetPawn());
	if (!Char)
	{
		Entity.Movement.bHasActiveSpline = false;
		Entity.Movement.SplineWaypoints.Empty();
		return;
	}
	Entity.Movement.SplineElapsed += DeltaTime * 1000.0f;
	const bool bDone = Entity.Movement.SplineDuration <= 0 ||
		Entity.Movement.SplineElapsed >= Entity.Movement.SplineDuration;
	const float Progress = bDone ? 1.f : Entity.Movement.SplineElapsed / float(Entity.Movement.SplineDuration);
	FVector Tangent = FVector::ZeroVector;
	const FVector WowPos = bDone ? Entity.Movement.SplineWaypoints.Last().Position
		: SampleWowLinearSpline(Entity.Movement, Progress, Tangent);
	FVector Target = FWowCoordinate::WowToUE(WowPos);
	if (Target.ContainsNaN()) return;
	const float HalfHeight = Char->GetCapsuleComponent() ? Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
	Target.Z += HalfHeight;
	const FVector Previous = Char->GetActorLocation();
	Char->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
	{
		// Rendered speed drives the run animation while the server owns the motion.
		Move->Velocity = DeltaTime > UE_SMALL_NUMBER && !bDone ? (Target - Previous) / DeltaTime : FVector::ZeroVector;
		if (Move->IsFalling()) Move->SetMovementMode(MOVE_Walking);
	}
	const FVector Flat(Target.X - Previous.X, Target.Y - Previous.Y, 0.f);
	if (!bDone && Flat.SizeSquared() > 1.f && (Entity.Movement.SplineFlags & 0x00004000) == 0)
	{
		const float Yaw = Flat.Rotation().Yaw;
		Char->SetActorRotation(FRotator(0.f, Yaw, 0.f));
		DesiredFacingYawDegrees = Yaw;
		bHasDesiredFacingYaw = true;
	}
	if (bDone)
	{
		Entity.Movement.Position = WowPos;
		Entity.Movement.bHasActiveSpline = false;
		Entity.Movement.SplineWaypoints.Empty();
		bHasServerPosition = true;
		LastServerPosition = Target;
		ResetOutgoingMovementState();
		UE_LOG(LogWowGameplay, Log, TEXT("Local server spline finished (spell movement) at %s"), *Target.ToString());
	}
}

void AWowGameplayController::UpdateEntityNetworkMovement(float DeltaTime)
{
	if (!ConnectionManager) return;

	const double Now = FPlatformTime::Seconds();
	FWowEntityManager& EntityManager = ConnectionManager->PacketHandler.EntityManager;
	for (auto& Pair : SpawnedEntityActors)
	{
		const uint64 Guid = Pair.Key;
		AActor* Actor = Pair.Value;
		if (!Actor || !IsValid(Actor) || Guid == EntityManager.LocalPlayerGuid ||
			DeadEntityGuids.Contains(Guid) || IsClientDrivenTransport(Guid)) // r599: transports move in UpdateTransports
		{
			continue;
		}

		const FWowEntity* Entity = EntityManager.Find(Guid);
		if (!Entity || Entity->Movement.bHasActiveSpline ||
			Entity->Movement.Position == FVector::ZeroVector)
		{
			continue;
		}

		FVector TargetLocation = FWowCoordinate::WowToUE(Entity->IsPlayer()
            ? PredictWowRemoteGroundPosition(Entity->Movement, Now) : Entity->Movement.Position);
		if (TargetLocation.ContainsNaN()) continue;
		// r523 (Stefan: a gnome bot "teleporting all over", standing in chairs and appearing to walk
		// through walls): between heartbeats a running player is extrapolated up to 1 s ahead with no
		// regard for walls, then snapped back by the next packet. Sweep from the last known position
		// to the prediction and stop just short of static world geometry in the way.
		if (Entity->IsPlayer() && GetWorld())
		{
			const FVector Known = FWowCoordinate::WowToUE(Entity->Movement.Position);
			if (FVector::DistSquared2D(Known, TargetLocation) > 100.0)
			{
				FCollisionQueryParams Query(SCENE_QUERY_STAT(WowRemotePredictSweep), false, Actor);
				TArray<AActor*> Riding;
				Actor->GetAttachedActors(Riding, true, true);
				Query.AddIgnoredActors(Riding);
				if (AActor* Rider = SpawnedEntityRiderActors.FindRef(Guid).Get()) Query.AddIgnoredActor(Rider);
				FHitResult Block;
				const FVector Up(0, 0, 70);
				if (GetWorld()->SweepSingleByObjectType(Block, Known + Up, TargetLocation + Up, FQuat::Identity,
						FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(25.0f), Query)
					&& !Block.bStartPenetrating && Block.ImpactNormal.Z < 0.7f)
				{
					TargetLocation = FMath::Lerp(Known, TargetLocation, FMath::Max(0.0f, Block.Time - 0.05f));
				}
				// r523 (Stefan: Gyra, on a mechanostrider and on foot, sank into the snow slope and then
				// floated, "teleporting around"): the prediction moves only X/Y and keeps the last packet's
				// height, so running uphill the body went into the ground and downhill it hung in the air
				// until the next heartbeat snapped it. Follow the walkable ground under the predicted spot.
				if ((Entity->Movement.MoveFlags & (WowMovementFlags::FALLING | WowMovementFlags::SWIMMING
					| WowMovementFlags::FLYING)) == 0)
				{
					// Up to ~40 degrees of slope over the predicted run; small enough not to climb onto a
					// bridge deck overhead. Static world only, so nobody stands on another unit's head.
					const double Reach = FMath::Clamp(0.85 * FVector::Dist2D(Known, TargetLocation), 100.0, 400.0);
					FHitResult Ground;
					if (GetWorld()->LineTraceSingleByObjectType(Ground, TargetLocation + FVector(0, 0, Reach),
							TargetLocation - FVector(0, 0, Reach + 100.0), FCollisionObjectQueryParams(ECC_WorldStatic), Query)
						&& Ground.ImpactNormal.Z > 0.6f)
					{
						TargetLocation.Z = Ground.ImpactPoint.Z;
					}
				}
			}
		}
		// r339 (Stefan: a player - a bot - stood waist-deep in a wooden floor, a mount with its legs in
		// the ground): server-moved players report the navmesh height, which can lie well under the
		// drawn floor. Lift a grounded remote player onto walkable ground just above that height
		// (never down, at most 1.1 m; 2 m off building floors since r349), re-checked when they move or once a second.
		// r523 (Stefan: an NPC dwarf stood sunk through a stair): creatures get the same lift as players,
		// except ones on a transport or that the server lets fly, hover or float (0x200 on transport,
		// 0x400 disable gravity, 0x01000000 can fly, 0x40000000 hover).
		WowApplyRemoteGroundLift(GetWorld(), Actor, SpawnedEntityRiderActors.FindRef(Guid).Get(), Guid, *Entity, Now, TargetLocation);

		const FVector CurrentLocation = Actor->GetActorLocation();
		const float Distance = FVector::Dist(CurrentLocation, TargetLocation);
		// r601: a passenger rides exactly with its (client-driven) boat, zeppelin or elevator, without lag.
		const bool bRidingTransport = Entity->Movement.TransportGuid != 0 && IsClientDrivenTransport(Entity->Movement.TransportGuid);
		if (Distance > 5000.0f || bRidingTransport)
		{
			// A large discontinuity is a teleport/map correction, not locomotion.
			Actor->SetActorLocation(TargetLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}
		else if (Distance > 3.0f || (Entity->IsPlayer() && Distance > KINDA_SMALL_NUMBER))
		{
			// Exponential correction has the same response at different frame rates.
            // The player target continues moving between network snapshots.
            // r502 (Stefan: other players - bots - "rubberband and teleport around at times"): between
            // heartbeats a running player is predicted ahead (up to 1 s); when they stop or turn, the
            // next packet lies behind the drawn body and it was pulled back hard (9/s). A small
            // correction against the direction the body faces now eases back slowly (2.5/s), so a
            // stop reads as a stop instead of a jump backwards. Large corrections stay quick.
            // r523 (Stefan: "tighten the netcode insanely much more"): 9/s -> 14/s (half the gap in 50 ms).
            float CorrectionRate = 14.0f;
            if (Entity->IsPlayer() && Distance < 250.0f)
            {
                const FVector2D Back = FVector2D(TargetLocation - CurrentLocation).GetSafeNormal();
                const FVector2D Facing = FVector2D(Actor->GetActorForwardVector()).GetSafeNormal();
                if (FVector2D::DotProduct(Back, Facing) < -0.3f) CorrectionRate = 2.5f;
            }
            const FVector Smoothed = FMath::Lerp(CurrentLocation, TargetLocation,
                1.0f - FMath::Exp(-CorrectionRate * FMath::Max(0.0f, DeltaTime)));
			Actor->SetActorLocation(Smoothed);
			if (FVector::DistSquared2D(CurrentLocation, Smoothed) > FMath::Square(0.25f))
			{
				VisuallyMovingEntityGuids.Add(Guid);
                if(DeltaTime>UE_SMALL_NUMBER) EntityVisualSpeedsCm.Add(Guid,FVector::Dist2D(CurrentLocation,Smoothed)/DeltaTime);
			}
		}
		else if (Distance > KINDA_SMALL_NUMBER)
		{
			Actor->SetActorLocation(TargetLocation);
			if (FVector::DistSquared2D(CurrentLocation, TargetLocation) > FMath::Square(0.25f))
			{
				VisuallyMovingEntityGuids.Add(Guid);
                if(DeltaTime>UE_SMALL_NUMBER) EntityVisualSpeedsCm.Add(Guid,FVector::Dist2D(CurrentLocation,TargetLocation)/DeltaTime);
			}
		}

		// r423 (Stefan: signpost arms wrong, reported many times; also shop signs, chairs, plaques):
		// game objects were turned here every frame to their bare orientation, which drops the -90
		// model turn and the tilt of the spawn rotation (see the gameobject spawn). Each arm slowly
		// swung from pointing out of the post to lying across it. Game objects keep the rotation
		// they were spawned with; only units face their orientation.
		if (!Entity->IsUnit()) continue;

		float Orientation = FMath::IsFinite(Entity->Movement.Orientation)
			? Entity->Movement.Orientation : 0.0f;
        // Pets/creatures receiving position updates need the same path-facing
        // behavior as spline movement; keep player strafing and fixed facing intact.
        if(!Entity->IsPlayer() && Entity->IsUnit() && Distance<5000.f &&
            (Entity->Movement.SplineFlags & 0x00004000)==0 &&
            FVector::DistSquared2D(CurrentLocation,TargetLocation)>9.f)
            Orientation=FWowCoordinate::UEYawToWowOrientation((TargetLocation-CurrentLocation).Rotation().Yaw);
        // ATTACKSTART applies to creatures too. Once they reach melee range,
        // no movement packet is guaranteed while the victim circles them.
        // Only a stationary, attacking NPC follows its confirmed victim; remote
        // players, moving paths, casts and explicitly fixed facing retain authority.
        // Remote players swinging at a target (ATTACKSTART) face it too, as in the original
        // client; while they move their own facing packets stay authoritative.
        // A unit swinging at a victim within melee reach always faces it, even while it
        // shuffles/circles (remote players and player bots are often flagged as moving).
        bool bFacingMeleeVictim = false;
        if (const uint64 MeleeVictim = ServerMeleeVictims.FindRef(Guid);
            MeleeVictim && !ActiveTimedCastSpells.Contains(Guid) && !ActiveChannelSpells.Contains(Guid))
        {
            const AActor* MeleeActor = MeleeVictim==EntityManager.LocalPlayerGuid ? GetPawn() : SpawnedEntityActors.FindRef(MeleeVictim).Get();
            bFacingMeleeVictim = IsValid(MeleeActor) && !DeadEntityGuids.Contains(MeleeVictim)
                && FVector::DistSquared2D(MeleeActor->GetActorLocation(), Actor->GetActorLocation()) < FMath::Square(650.0f);
        }
        if (bFacingMeleeVictim || ((!Entity->IsPlayer() || ServerMeleeVictims.Contains(Guid)) && !VisuallyMovingEntityGuids.Contains(Guid) &&
            (Entity->Movement.SplineFlags & 0x00004000)==0 &&
            !ActiveTimedCastSpells.Contains(Guid) && !ActiveChannelSpells.Contains(Guid)))
        {
            uint64 Victim=ServerMeleeVictims.FindRef(Guid);
            // Pets already fighting when they enter visibility may have no ATTACKSTART.
            if(!Victim && Entity->IsUnit() && (Entity->GetField(UnitField::FLAGS)&0x00080000)!=0)
                Victim=Entity->GetField64(UnitField::TARGET);
            AActor* VictimActor=Victim==EntityManager.LocalPlayerGuid ? GetPawn() : SpawnedEntityActors.FindRef(Victim).Get();
            if(Victim!=0 && IsValid(VictimActor) && !DeadEntityGuids.Contains(Victim))
            {
                const FVector Direction=VictimActor->GetActorLocation()-Actor->GetActorLocation();
                if(Direction.SizeSquared2D()>1.f)
                    Orientation=FWowCoordinate::UEYawToWowOrientation(Direction.Rotation().Yaw);
            }
        }
		const FRotator TargetRotation(
			0.0f, FWowCoordinate::WowOrientationToUEYaw(Orientation), 0.0f);
		Actor->SetActorRotation(FMath::RInterpTo(
			Actor->GetActorRotation(), TargetRotation, DeltaTime, 12.0f));
	}
}

void AWowGameplayController::OnTeleportRequest(uint64 Guid, uint32 Flags, uint32 Time, FVector Position, float Orientation)
{
	if (!ConnectionManager) return;

	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;

	// Only handle teleports for our local player
	if (Guid != LocalGuid)
	{
		UE_LOG(LogWowGameplay, Verbose, TEXT("Ignoring teleport for non-local entity GUID %llu"), Guid);
		return;
	}

	// Convert from WoW coordinates to UE coordinates
	FVector UEPosition = FWowCoordinate::WowToUE(Position);

	// Teleport the local pawn
	APawn* LocalPawn = GetPawn();
	if (LocalPawn)
	{
		// The server sends the feet position; the pawn's location is the capsule centre. Placing
		// the centre on the feet sank half the body into the floor, and on a thin WMO floor over a
		// terrain hole the player fell out of the world (Stefan sat on a chair in Kharanos: the chair
		// "use" is a teleport onto the seat). r236 (Claude-Perf), same lift as ApplyLoginSpawn.
		FVector PawnPosition = UEPosition;
		const bool bLongTeleport = FVector::Dist(LocalPawn->GetActorLocation(), UEPosition) > 6000.0;
		if (const ACharacter* TeleportedCharacter = Cast<ACharacter>(LocalPawn))
		{
			if (const UCapsuleComponent* Capsule = TeleportedCharacter->GetCapsuleComponent())
				PawnPosition.Z += Capsule->GetScaledCapsuleHalfHeight() + 2.0f;
			if (UCharacterMovementComponent* TeleportMove = TeleportedCharacter->GetCharacterMovement())
				TeleportMove->Velocity = FVector::ZeroVector;
		}
		LocalPawn->SetActorLocation(PawnPosition, false, nullptr, ETeleportType::TeleportPhysics);
		if (bLongTeleport)
			if (ACharacter* HeldCharacter = Cast<ACharacter>(LocalPawn); HeldCharacter && HeldCharacter->GetCharacterMovement())
			{
				HeldCharacter->GetCharacterMovement()->DisableMovement();
				GWowTeleportHold = true;
				GWowTeleportHoldSince = FPlatformTime::Seconds();
				GWowTeleportHoldAt = PawnPosition;
				UE_LOG(LogWowGameplay, Log, TEXT("Teleport hold: waiting for ground at the destination"));
				auto* Terrain = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this, AWowWorldManager::StaticClass()));
				// r452 (Stefan: "often a lot of lag after hearthing - a loading screen no matter how long the
				// distance"): every long teleport (> 60 m: hearthstone, summons, same-map portals) now shows the
				// loading screen until the destination has streamed in. (The old 500 m test measured after the
				// pawn had already moved, so it was always 0 and only unloaded terrain showed the screen.)
				if (Terrain && !OfflineTransferScreen.IsValid())
				{
					auto* Login = Cast<AWowLoginController>(UGameplayStatics::GetActorOfClass(this, AWowLoginController::StaticClass()));
					OfflineTransferScreen = Login ? Login->CreateTransferLoadingScreen(ConnectionManager->PacketHandler.DeathState.CurrentMap) : SNew(SWowLoadingScreen);
					if (GEngine && GEngine->GameViewport)
						GEngine->GameViewport->AddViewportWidgetContent(OfflineTransferScreen.ToSharedRef(), 1000);
					SetIgnoreMoveInput(true); SetIgnoreLookInput(true);
					Terrain->BeginWorldEntryWarmup(PawnPosition);
					Terrain->LoadTilesAroundPosition(PawnPosition, 2);
					GWowTeleportScreen = true;
					GWowTeleportStableSince = FPlatformTime::Seconds();
					GWowTeleportRevision = Terrain->GetObjectStreamingRevision();
					UE_LOG(LogWowGameplay, Log, TEXT("Teleport loading screen shown"));
				}
			}
		const float UEYaw = FWowCoordinate::WowOrientationToUEYaw(Orientation);
		LocalPawn->SetActorRotation(FRotator(0.0f, UEYaw, 0.0f));

		if (APlayerController* PC = Cast<APlayerController>(LocalPawn->GetController()))
		{
			PC->SetControlRotation(FRotator(0.0f, UEYaw, 0.0f));
		}

		UE_LOG(LogWowGameplay, Log, TEXT("Teleported to: WoW=(%.1f,%.1f,%.1f) UE=(%.0f,%.0f,%.0f) orient=%.2f"),
			Position.X, Position.Y, Position.Z, UEPosition.X, UEPosition.Y, UEPosition.Z, Orientation);

		// Update server position tracking
		bHasServerPosition = true;
		LastServerPosition = UEPosition;
		LastSentYawDegrees = TNumericLimits<float>::Lowest();
		ResetOutgoingMovementState();
		DesiredFacingYawDegrees = UEYaw;
		bHasDesiredFacingYaw = true;
		GameplayCameraYawDegrees = DesiredFacingYawDegrees;
		GameplayCameraPitchDegrees = FMath::Clamp(GetControlRotation().Pitch, MinMouseLookPitch, MaxMouseLookPitch);
		bHasGameplayCameraRotation = true;
		ApplyGameplayCameraRotation();
	}

	// Send acknowledgment to server
	SendTeleportAck(Guid, Flags, Time);
}

void AWowGameplayController::OnMapTransfer(uint32 MapId, float X, float Y, float Z, float Orientation)
{
    auto* Terrain=Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this,AWowWorldManager::StaticClass()));
    if(!Terrain || !Terrain->SelectMap(MapId))
    {
        UE_LOG(LogWowGameplay,Error,TEXT("Cannot enter server map %u; refusing wrong-continent teleport"),MapId);
        return;
    }
    if (auto* Sky = Cast<AWowSkyManager>(UGameplayStatics::GetActorOfClass(this, AWowSkyManager::StaticClass())))
    {
        Sky->LoadLightZones(static_cast<int32>(MapId));
    }
	// r601: a continent change on board sends the deck offset; turn it into the world position on the new map.
	ResolveTransportTransfer(MapId, X, Y, Z, Orientation);
	UE_LOG(LogWowGameplay, Log, TEXT("Map transfer to: map=%u pos=(%.1f,%.1f,%.1f) orient=%.2f"),
		MapId, X, Y, Z, Orientation);
	// r734 (Stefan 23.10, TormentWoW: "it's loading soo long!" - the loading screen never went away): Torment moves
	// the character to another map (NEW_WORLD) while our login loading screen is still up. The entry kept waiting
	// for, and finally spawned at, the login position of the OLD map - no terrain there, so the stable-ground check
	// never passed. While the login spawn is still deferred, the transfer destination becomes the spawn point;
	// AWowLoginController::UpdateLoadingProgress sees the change and restarts its warmup there.
	if (bHasDeferredSpawn)
	{
		DeferredSpawnPos = FWowCoordinate::WowToUE(X, Y, Z);
		DeferredSpawnOrientation = Orientation;
		UE_LOG(LogWowGameplay, Log, TEXT("Map transfer during world entry: the loading screen now waits for map %u"), MapId);
	}

    // Clear old visible entities, preserving the local player and inventory.
    if(ConnectionManager)
    {
        auto& Entities=ConnectionManager->PacketHandler.EntityManager;
        TArray<uint64> Retired;
        for(const auto& Pair:Entities.GetAll())
            if(Pair.Key!=Entities.LocalPlayerGuid && (Pair.Value->IsUnit() || Pair.Value->IsGameObject())) Retired.Add(Pair.Key);
        for(uint64 Guid:Retired) Entities.Remove(Guid);
    }
    // SelectMap has discarded terrain from the previous continent.
    Terrain->LoadTilesAroundPosition(FWowCoordinate::WowToUE(X,Y,Z),2);
    // Move to the authoritative destination, not the old map's ground height.
	FVector Position(X, Y, Z);
	FVector UEPosition = FWowCoordinate::WowToUE(Position);

	// Teleport the local pawn
	APawn* LocalPawn = GetPawn();
	if (LocalPawn)
	{
		// The server sends the feet position; the pawn's location is the capsule centre. Placing
		// the centre on the feet sank half the body into the floor, and on a thin WMO floor over a
		// terrain hole the player fell out of the world (Stefan sat on a chair in Kharanos: the chair
		// "use" is a teleport onto the seat). r236 (Claude-Perf), same lift as ApplyLoginSpawn.
		FVector PawnPosition = UEPosition;
		if (const ACharacter* TeleportedCharacter = Cast<ACharacter>(LocalPawn))
		{
			if (const UCapsuleComponent* Capsule = TeleportedCharacter->GetCapsuleComponent())
				PawnPosition.Z += Capsule->GetScaledCapsuleHalfHeight() + 2.0f;
			if (UCharacterMovementComponent* TeleportMove = TeleportedCharacter->GetCharacterMovement())
				TeleportMove->Velocity = FVector::ZeroVector;
		}
		LocalPawn->SetActorLocation(PawnPosition, false, nullptr, ETeleportType::TeleportPhysics);
		const float UEYaw = FWowCoordinate::WowOrientationToUEYaw(Orientation);
		LocalPawn->SetActorRotation(FRotator(0.0f, UEYaw, 0.0f));

		if (APlayerController* PC = Cast<APlayerController>(LocalPawn->GetController()))
		{
			PC->SetControlRotation(FRotator(0.0f, UEYaw, 0.0f));
		}

		// Update server position tracking
		bHasServerPosition = true;
		LastServerPosition = UEPosition;
		LastSentYawDegrees = TNumericLimits<float>::Lowest();
		ResetOutgoingMovementState();
		DesiredFacingYawDegrees = UEYaw;
		bHasDesiredFacingYaw = true;
		GameplayCameraYawDegrees = DesiredFacingYawDegrees;
		GameplayCameraPitchDegrees = FMath::Clamp(GetControlRotation().Pitch, MinMouseLookPitch, MaxMouseLookPitch);
		bHasGameplayCameraRotation = true;
		ApplyGameplayCameraRotation();
	}

    bMapTerrainPending=!Terrain->IsTerrainReadyAtPosition(UEPosition);
    if(bMapTerrainPending)
        if(auto* MovingPawn=Cast<ACharacter>(LocalPawn)) MovingPawn->GetCharacterMovement()->DisableMovement();
    // Send worldport acknowledgment
	SendWorldportAck();


}

void AWowGameplayController::SendTeleportAck(uint64 Guid, uint32 Flags, uint32 Time)
{
	if (!ConnectionManager) return;

	// 3.3.5 ACK: packed mover GUID, echoed order counter, movement timestamp.
	TArray<uint8> Data;
	Data.Add(0);
	for (uint8 I = 0; I < 8; ++I)
	{
		const uint8 Byte = static_cast<uint8>(Guid >> (I * 8));
		if (Byte != 0) { Data[0] |= (1 << I); Data.Add(Byte); }
	}
	Data.Append(reinterpret_cast<const uint8*>(&Flags), sizeof(Flags));
	Data.Append(reinterpret_cast<const uint8*>(&Time), sizeof(Time));

	ConnectionManager->SendRawPacket(WowOpcode::MSG_MOVE_TELEPORT_ACK, Data);

	UE_LOG(LogWowGameplay, Log, TEXT("Sent CMSG_MOVE_TELEPORT_ACK: guid=%llu flags=0x%08X time=%u"),
		Guid, Flags, Time);
}

void AWowGameplayController::SendWorldportAck()
{
	if (!ConnectionManager) return;

	// MSG_MOVE_WORLDPORT_ACK has no payload
	ConnectionManager->SendRawPacket(WowOpcode::MSG_MOVE_WORLDPORT_ACK, {});

	UE_LOG(LogWowGameplay, Log, TEXT("Sent MSG_MOVE_WORLDPORT_ACK"));
}

void AWowGameplayController::AddCombatMessage(const FString& Message, const FLinearColor& Color, bool bPlayerIsSource, bool bPlayerIsTarget)
{
	// The active HUD uses ChatFrame2 as its Combat Log tab (filtered: Self / Everything / What happened to me?).
	if (UIManager && UIManager->GetFrameManager())
	{
		UIManager->GetFrameManager()->AddCombatLogMessage(Message, WowColor::FromWow(Color), bPlayerIsSource, bPlayerIsTarget);
		return;
	}

	// Legacy native fallbacks remain available when FrameXML is not loaded.
	if (ChatWindow.IsValid())
	{
		ChatWindow->AddCombatMessage(Message, Color);
	}
	else if (CombatLog.IsValid())
	{
		CombatLog->AddCombatMessage(Message, Color);
	}
}

// â”€â”€ Combat System â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void AWowGameplayController::CastSpell(int32 SpellId)
{
	if (!ConnectionManager) return;

	// Don't cast if already casting
	if (bIsCasting)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Already casting spell %u"), CurrentSpellId);
		return;
	}

	int64 TargetGuidSigned = static_cast<int64>(TargetGuid);
	ConnectionManager->SendCastSpell(SpellId, TargetGuidSigned);

	UE_LOG(LogWowGameplay, Log, TEXT("Cast spell %u on target %llu"), SpellId, TargetGuid);
}

void AWowGameplayController::StartAutoAttack()
{
	if (!ConnectionManager || TargetGuid == 0) return;

	// Stop any existing auto-attack
	if (bIsAutoAttacking && AutoAttackTargetGuid != TargetGuid)
	{
		StopAutoAttack();
	}

	// Start auto-attack on new target
	if (!bIsAutoAttacking)
	{
		bIsAutoAttacking = true;
		AutoAttackTargetGuid = TargetGuid;
		DrawLocalMeleeWeapons();
		ConnectionManager->SendAttackSwing(static_cast<int64>(TargetGuid));
		UE_LOG(LogWowGameplay, Log, TEXT("Started auto-attack on target %llu"), TargetGuid);

		// Get target name for combat message
		// The unit's real name (it said "Player <guid>" / "Creature <entry>").
		FString TargetName = CombatLogUnitName(TargetGuid);
		if (TargetName.IsEmpty()) TargetName = TEXT("Unknown");

		// Add combat message
		FString Message = FString::Printf(TEXT("You begin attacking %s"), *TargetName);
		AddCombatMessage(Message, FLinearColor::Red, true, false);
	}
}

void AWowGameplayController::StopAutoAttack()
{
	if (!ConnectionManager || !bIsAutoAttacking) return;

	bIsAutoAttacking = false;
	AutoAttackTargetGuid = 0;
	ConnectionManager->SendAttackStop();
	UE_LOG(LogWowGameplay, Log, TEXT("Stopped auto-attack"));
}

// r605 (Stefan: "If something attacks you, we should auto-attack back automatically. Unless we
// detarget, then turn off the automated auto-attack for that mob"): when a hostile unit starts
// meleeing us (SMSG_ATTACKSTART / a swing) while we are not auto-attacking, it becomes the target
// (when we have no living enemy targeted) and we swing back. Clearing the target stops that attack
// and leaves that mob alone until it dies or we attack it ourselves. World option "noautoretaliate"
// turns it off (Options).
namespace WowAutoRetaliate
{
    uint64 GActive = 0;
    TSet<uint64> GDeclined;
}

static void WowAutoRetaliateOnAttacked(AWowGameplayController* C, uint64 AttackerGuid)
{
    using namespace WowAutoRetaliate;
    if (!C || !C->ConnectionManager || AttackerGuid == 0 || C->IsAutoAttacking() || GDeclined.Contains(AttackerGuid)) return;
    if (FWowClientPreferences::GetWorldOption(TEXT("noautoretaliate"))) return;
    FWowEntityManager& Manager = C->ConnectionManager->PacketHandler.EntityManager;
    const FWowUnitEntity* Local = Manager.FindUnit(Manager.LocalPlayerGuid);
    const FWowUnitEntity* Attacker = Manager.FindUnit(AttackerGuid);
    if (!Local || !Attacker || AttackerGuid == Manager.LocalPlayerGuid || Local->GetHealth() == 0 || Attacker->GetHealth() == 0) return;
    if (Local->GetField(UnitField::MOUNTDISPLAYID) != 0) return;
    if (C->ConnectionManager->GetUnitReaction(Local, Attacker) >= 4) return;
    if (C->TargetGuid != 0 && C->TargetGuid != AttackerGuid)
    {
        // Busy with another living enemy: keep it (the player chose it).
        const FWowUnitEntity* Current = Manager.FindUnit(C->TargetGuid);
        if (Current && Current->GetHealth() > 0 && C->ConnectionManager->GetUnitReaction(Local, Current) < 4) return;
    }
    if (C->TargetGuid != AttackerGuid) C->SetTarget(AttackerGuid);
    if (C->TargetGuid != AttackerGuid) return;
    C->StartAutoAttack();
    GActive = AttackerGuid;
    UE_LOG(LogWowGameplay, Log, TEXT("Auto-retaliate: attacking back %llu"), AttackerGuid);
}

static void WowAutoRetaliateTick(AWowGameplayController* C)
{
    using namespace WowAutoRetaliate;
    if (!C || !C->ConnectionManager) return;
    FWowEntityManager& Manager = C->ConnectionManager->PacketHandler.EntityManager;
    if (GActive != 0 && C->TargetGuid != GActive)
    {
        if (C->TargetGuid == 0)
        {
            // Detargeted: stop swinging at it and leave it alone.
            if (C->IsAutoAttacking()) C->StopAutoAttack();
            GDeclined.Add(GActive);
            UE_LOG(LogWowGameplay, Log, TEXT("Auto-retaliate: detargeted %llu, not attacking it back"), GActive);
        }
        GActive = 0;
    }
    else if (GActive != 0 && !C->IsAutoAttacking()) GActive = 0;
    for (auto It = GDeclined.CreateIterator(); It; ++It)
    {
        const FWowUnitEntity* Unit = Manager.FindUnit(*It);
        if (!Unit || Unit->GetHealth() == 0 || (C->IsAutoAttacking() && C->TargetGuid == *It)) It.RemoveCurrent();
    }
}

void AWowGameplayController::OnServerAttackStart(uint64 AttackerGuid, uint64 VictimGuid)
{
    if(AttackerGuid!=0 && VictimGuid!=0) ServerMeleeVictims.Add(AttackerGuid,VictimGuid);
    if (ConnectionManager && VictimGuid != 0 && VictimGuid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
        WowAutoRetaliateOnAttacked(this, AttackerGuid); // r605
	// Server confirms attack started â€” sync local state
	if (ConnectionManager && AttackerGuid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
	{
		bIsAutoAttacking = VictimGuid != 0;
		AutoAttackTargetGuid = VictimGuid;
        if (bIsAutoAttacking) DrawLocalMeleeWeapons();
		UE_LOG(LogWowGameplay, Log, TEXT("Server confirmed auto-attack on %llu"), VictimGuid);
	}
}

void AWowGameplayController::OnServerAttackStop(uint64 AttackerGuid, uint64 VictimGuid)
{
    if(VictimGuid==0 || ServerMeleeVictims.FindRef(AttackerGuid)==VictimGuid) ServerMeleeVictims.Remove(AttackerGuid);
	// Server tells us to stop attacking (target died, out of range, etc.)
	if (ConnectionManager && AttackerGuid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
	{
		// A delayed stop for the previous victim must not cancel a new attack.
		if (VictimGuid == 0 || VictimGuid == AutoAttackTargetGuid)
		{
			bIsAutoAttacking = false;
			AutoAttackTargetGuid = 0;
			UE_LOG(LogWowGameplay, Log, TEXT("Server stopped our auto-attack on %llu"), VictimGuid);
		}
	}
}

void AWowGameplayController::SendLootRequest(uint64 ClickedGuid)
{
		UE_LOG(LogWowGameplay, Log, TEXT("Right-click: looting dead entity %llu"), ClickedGuid);
		if (ConnectionManager)
		{
            const auto* Corpse = ConnectionManager->PacketHandler.EntityManager.Find(ClickedGuid);
            if (Corpse && Corpse->IsUnit() && static_cast<const FWowUnitEntity*>(Corpse)->IsLootable())
                if (GameUI) GameUI->PrepareLootRequest(ClickedGuid);
            // The server checks loot range against the last position we reported. Standing
            // still after the kill (or being nudged by a body) can leave that stale, and the
            // request was then silently released. Report where we are first.
            if (ACharacter* Looter = Cast<ACharacter>(GetPawn()); Looter && bHasServerPosition && bPlayerEnteredWorld)
            {
                const uint32 LootFlags = ComputeLocalMovementFlags(Looter);
                if ((LootFlags & (WowMoveFlag::MOVING_MASK | WowMoveFlag::FALLING)) == 0)
                    ConnectionManager->SendMovementInfo(WowOpcode::MSG_MOVE_HEARTBEAT, BuildLocalMovementInfo(Looter, LootFlags));
                if (Corpse)
                    UE_LOG(LogWowGameplay, Log, TEXT("Loot request %llu at %.1f yd"), ClickedGuid,
                        FVector::Dist(Looter->GetActorLocation(), FWowCoordinate::WowToUE(Corpse->Movement.Position)) / 91.44f);
            }
		ConnectionManager->SendLoot(static_cast<int64>(ClickedGuid));
		}
}

void AWowGameplayController::OnRightClick()
{
    if (ConnectionManager && ConnectionManager->PendingTargetSpell) { ConnectionManager->CancelSpellTarget(); return; }
    if(GameUI&&GameUI->IsMovingFrame())return;
	if (bMouseInputNeedsRearmAfterFocusChange && IsWowClientProcessForeground())
		bMouseInputNeedsRearmAfterFocusChange = false; // a real right-click event after reactivation (see OnLeftClick)
	if (bMouseInputNeedsRearmAfterFocusChange || !IsWowClientProcessForeground())
	{
		return;
	}

	bRightMouseWasDown = true;
	if (IsNativeModalUiVisible())
	{
		return;
	}

	if (bLeftMouseLooking)
	{
		// WoW chord transition: left-drag by itself is camera-only, but adding
		// right mouse turns the active look into character-facing mouse look plus
		// forward movement. Do not run UI hit testing while the cursor is hidden.
		const bool bHadLeftSavedPosition = bHasSavedLeftMousePosition;
		const int32 LeftSavedX = SavedLeftMouseX;
		const int32 LeftSavedY = SavedLeftMouseY;
		CancelLeftMouseLookForRightMouseChord();
		BeginRightMouseLook();
		if (bHadLeftSavedPosition)
		{
			bHasSavedRightMousePosition = true;
			SavedRightMouseX = LeftSavedX;
			SavedRightMouseY = LeftSavedY;
		}
		bMouseChordMovingForward = true;
		return;
	}

    // Quest/gossip is non-modal: capture over the world, never through its controls.
    if (!bRightMouseLooking && IsPointerOverNativeGameUi()) return;

	bool bAllowUiRightClick = true;
	if (UIManager && UIManager->GetFrameManager())
	{
		FWowFrameManager* FrameManager = UIManager->GetFrameManager();
		const int64 HitFrame = GetUiFrameUnderCursor();
		const FString HitFrameName = HitFrame >= 0 ? FrameManager->GetFrameName(HitFrame) : FString();
		if (HitFrameName.StartsWith(TEXT("ChatFrame")) && HitFrameName.Contains(TEXT("Tab")))
		{
			float MouseX = 0.0f;
			float MouseY = 0.0f;
			int32 ViewportWidth = 0;
			int32 ViewportHeight = 0;
			GetViewportSize(ViewportWidth, ViewportHeight);
			if (GetMousePosition(MouseX, MouseY) &&
				ViewportWidth > 0 && ViewportHeight > 0 &&
				(MouseX > ViewportWidth * 0.45f || MouseY < ViewportHeight * 0.55f))
			{
				bAllowUiRightClick = false;
				PressedRightMouseFrameHandle = -1;
				UE_LOG(LogWowGameplay, Verbose,
					TEXT("Ignoring stray chat-tab right-click hit %s at %.0f,%.0f"),
					*HitFrameName, MouseX, MouseY);
			}
		}
	}

	// Right-clicking the player or target frame opens the unit menu (Whisper, Inspect, Invite ...).
	if (bAllowUiRightClick && OpenUnitFrameMenu(GetUiFrameUnderCursor()))
	{
		PressedRightMouseFrameHandle = -1;
		return;
	}
	if (bAllowUiRightClick && HandleUiMousePress(TEXT("RightButton"), PressedRightMouseFrameHandle, false))
	{
		return; // Don't interact through UI
	}

	UE_LOG(LogWowGameplay, Log, TEXT("OnRightClick: TargetGuid=%llu DeadEntities=%d"), TargetGuid, DeadEntityGuids.Num());

	// Always resolve the world entity under this click. Reusing an older target
	// made right-clicking a different neutral/hostile mob attack the wrong unit,
	// while right-clicking empty ground could interact with the old target.
	uint64 ClickedGuid = 0;
	const bool bClickedEntity = TryTargetUnderCursor(false, &ClickedGuid);

	// r621: isometric camera - click-to-move instead of right-drag turning (see WowIsoClickMove).
	// r628: decided on the release (OnRightClickReleased): a click walks/interacts, a drag turns the camera.
	if (FWowIsometricCamera::IsActive())
	{
		GWowPendingRightLoot = nullptr;
		float PressX = 0.0f, PressY = 0.0f;
		GetMousePosition(PressX, PressY);
		WowIsoClickMove::GPressPending = true;
		WowIsoClickMove::GPressCursor = FVector2D(PressX, PressY);
		WowIsoClickMove::GPressGuid = bClickedEntity ? ClickedGuid : 0;
		return;
	}
	// WoW-style camera look: a right-button press over the world hides the
	// cursor while Mouse2D rotates the spring-arm camera.  Keep the original
	// position so the pointer returns to the same UI location on release.
	BeginRightMouseLook();

	if (!bClickedEntity) return;
	UE_LOG(LogWowGameplay, Log, TEXT("OnRightClick: clicked entity ClickedGuid=%llu"), ClickedGuid);

	// Check if this is a dead NPC corpse â€” loot it
	UE_LOG(LogWowGameplay, Log, TEXT("OnRightClick: checking DeadEntityGuids for %llu, contains=%d"),
		ClickedGuid, DeadEntityGuids.Contains(ClickedGuid) ? 1 : 0);
	if (DeadEntityGuids.Contains(ClickedGuid))
	{
		// Loot on release, and only when the right button was not used to turn: a right-drag
		// over a body to turn the character looted it.
		GWowRightLootPressRotation = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation() : GetControlRotation();
		GWowRightLootPressSeconds = FPlatformTime::Seconds();
		GWowPendingRightLoot = [this, ClickedGuid]() { SendLootRequest(ClickedGuid); };
		return;
	}

	// Interact on release too, and only for a click: a right-drag that started over a mailbox,
	// NPC or other usable object turned the camera AND opened its window (Stefan 10.10).
	GWowRightLootPressRotation = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation() : GetControlRotation();
	GWowRightLootPressSeconds = FPlatformTime::Seconds();
	GWowPendingRightLoot = [this, ClickedGuid]() { InteractWithWorldEntity(ClickedGuid); };
}

bool AWowGameplayController::OpenUnitFrameMenu(int64 HitFrame)
{
	if (!UIManager || !UIManager->GetFrameManager() || !ConnectionManager || HitFrame < 0) return false;
	FWowFrameManager* Frames = UIManager->GetFrameManager();
	auto& Handler = ConnectionManager->PacketHandler;
	FString Unit;
	uint64 Guid = 0;
	const uint64 Me = Handler.EntityManager.LocalPlayerGuid;
	for (int64 Handle = HitFrame; Handle >= 0 && Unit.IsEmpty(); Handle = Frames->GetParentHandle(Handle))
	{
		const FString FrameName = Frames->GetFrameName(Handle);
		if (FrameName == TEXT("PlayerFrame")) { Unit = TEXT("player"); Guid = Me; }
		else if (FrameName == TEXT("TargetFrame")) { Unit = TEXT("target"); Guid = TargetGuid; }
		else if (FrameName.StartsWith(TEXT("PartyMemberFrame")) && FrameName.Len() == 17 && FChar::IsDigit(FrameName[16]))
		{
			// PartyMemberFrame1-4 = party1-4 = the group members other than you, in list order.
			const int32 Index = FrameName[16] - TEXT('1');
			Unit = FString::Printf(TEXT("party%d"), Index + 1);
			if (Handler.GroupInfo.Members.IsValidIndex(Index)) Guid = Handler.GroupInfo.Members[Index].Guid;
		}
	}
	if (Unit.IsEmpty()) return false;
	const FWowEntity* Entity = Guid ? Handler.EntityManager.Find(Guid) : nullptr;
	if (!Guid) return false;
	FString Name;
	if (const FString* PlayerName = Handler.PlayerNameCache.Find(Guid)) Name = *PlayerName;
	else if (Entity)
		if (const FString* CreatureName = Handler.CreatureNameCache.Find(Entity->Entry)) Name = *CreatureName;
	if (Name.IsEmpty() && Unit.StartsWith(TEXT("party")))
		if (const FWowGroupMember* Member = Handler.GroupInfo.FindMember(Guid)) Name = Member->Name;
	const bool bIsPlayer = (Entity && Entity->IsPlayer()) || Unit.StartsWith(TEXT("party"));
	const FWowGroupInfo& Group = Handler.GroupInfo;
	const bool bInGroup = !Group.IsEmpty();
	const bool bLeader = bInGroup && Group.LocalPlayerIsLeader(Me);
	auto Lua = [this](const FString& Code)
	{
		return [this, Code]()
		{
			if (UIManager && UIManager->GetEventSystem()) UIManager->GetEventSystem()->ExecuteLuaString(Code, TEXT("=UnitMenu"));
		};
	};
	auto Line = [](const TCHAR* Text, TFunction<void()> Action, bool bEnabled = true, bool bCheckable = false, bool bChecked = false)
	{
		FWowUnitMenuEntry Entry;
		Entry.Label = FText::FromString(Text);
		Entry.Action = MoveTemp(Action);
		Entry.bEnabled = bEnabled;
		Entry.bCheckable = bCheckable;
		Entry.bChecked = bChecked;
		return Entry;
	};
	// UnitPopup nested menus (Raid Target Icon, Dungeon Difficulty ...): one line with an arrow,
	// the choices in a submenu (Stefan 10.10).
	auto Nested = [](const TCHAR* Text, TArray<FWowUnitMenuEntry>&& SubEntries, bool bEnabled = true)
	{
		FWowUnitMenuEntry Entry;
		Entry.Label = FText::FromString(Text);
		Entry.bEnabled = bEnabled && SubEntries.Num() > 0;
		Entry.Children = MakeShared<TArray<FWowUnitMenuEntry>>(MoveTemp(SubEntries));
		return Entry;
	};

	// UnitPopup menus (3.3.5): SELF / PLAYER / PARTY / TARGET; commands this client can carry out.
	TArray<FWowUnitMenuEntry> Entries;
	{
		FWowUnitMenuEntry Title;
		Title.Label = FText::FromString(Name.IsEmpty() ? FString(TEXT("Unknown")) : Name);
		Title.bTitle = true;
		Entries.Add(Title);
	}
	if (bIsPlayer && Guid != Me)
	{
		const int64 Target = static_cast<int64>(Guid);
		const bool bInMyGroup = Group.FindMember(Guid) != nullptr;
		Entries.Add(Line(TEXT("Whisper"), [this, Name]()
		{
			if (UIManager && UIManager->GetEventSystem() && !Name.IsEmpty())
				// Open the chat already in whisper mode, like ChatFrame_ReplyTell: chat type WHISPER and the
				// tell target set before the header updates (Stefan 10.10: ChatFrame_SendTell's "/w Name"
				// text was not parsed here and the box opened as "Say:").
				UIManager->GetEventSystem()->ExecuteLuaString(FString::Printf(TEXT(R"LUA(
					local name = "%s"
					local eb = ChatEdit_ChooseBoxForSend and ChatEdit_ChooseBoxForSend()
					if eb and ChatEdit_ActivateChat and ChatEdit_UpdateHeader then
						ChatEdit_ActivateChat(eb)
						eb:SetAttribute("chatType", "WHISPER")
						eb:SetAttribute("tellTarget", name)
						ChatEdit_UpdateHeader(eb)
					elseif ChatFrame_SendTell then
						ChatFrame_SendTell(name)
					end
				)LUA"), *Name), TEXT("=UnitMenuWhisper"));
		}, !Name.IsEmpty()));
		Entries.Add(Line(TEXT("Inspect"), [this, Guid]() { OpenInspect(Guid); }, Entity != nullptr));
		if (!bInMyGroup)
			Entries.Add(Line(TEXT("Invite"), [this, Name]() { if (ConnectionManager && !Name.IsEmpty()) ConnectionManager->SendGroupInviteByName(Name); },
				!Name.IsEmpty() && (!bInGroup || bLeader)));
		Entries.Add(Line(TEXT("Trade"), [this, Target]() { if (ConnectionManager) ConnectionManager->SendInitiateTrade(Target); }, Entity != nullptr));
		Entries.Add(Line(TEXT("Follow"), [this, Guid, Name]()
		{
			GWowFollowGuid = Guid;
			AddFrameXmlChatLine(FString::Printf(TEXT("You are now following %s."), *Name), FLinearColor(1.f, 1.f, 0.f));
		}, Entity != nullptr));
		// Duel is the spell 7266 cast on the other player.
		Entries.Add(Line(TEXT("Duel"), [this, Target]() { if (ConnectionManager) ConnectionManager->SendCastSpell(7266, Target); }, Entity != nullptr));
	}
	if (Guid != Me)
		Entries.Add(Line(TEXT("Set Focus"), Lua(FString::Printf(TEXT("FocusUnit(\"%s\")"), *Unit))));
	if (Guid == Me)
	{
		// UnitPopup SELF (Stefan): leave group (grey outside a group), reset instances, difficulty,
		// loot method (leader only). The Lua functions send the 3.3.5 packets.
		int32 Dungeon = 1, Raid = 1;
		if (UIManager && UIManager->GetEventSystem())
		{
			FWowEventSystem* Events = UIManager->GetEventSystem();
			Events->ExecuteLuaString(TEXT("__WowMenuDD = tostring(GetDungeonDifficulty and GetDungeonDifficulty() or 1) __WowMenuRD = tostring(GetRaidDifficulty and GetRaidDifficulty() or 1)"), TEXT("=UnitMenuDifficulty"));
			Dungeon = FCString::Atoi(*Events->GetGlobalString(TEXT("__WowMenuDD")));
			Raid = FCString::Atoi(*Events->GetGlobalString(TEXT("__WowMenuRD")));
		}
		Entries.Add(Line(TEXT("Leave Party"), Lua(TEXT("LeaveParty()")), bInGroup));
		Entries.Add(Line(TEXT("Reset all instances"), Lua(TEXT("ResetInstances()")), !bInGroup || bLeader));
		{
			TArray<FWowUnitMenuEntry> Choices;
			Choices.Add(Line(TEXT("Normal (5)"), Lua(TEXT("SetDungeonDifficulty(1)")), !bInGroup || bLeader, true, Dungeon == 1));
			Choices.Add(Line(TEXT("Heroic (5)"), Lua(TEXT("SetDungeonDifficulty(2)")), !bInGroup || bLeader, true, Dungeon == 2));
			Entries.Add(Nested(TEXT("Dungeon Difficulty"), MoveTemp(Choices)));
		}
		{
			TArray<FWowUnitMenuEntry> Choices;
			Choices.Add(Line(TEXT("10 Player"), Lua(TEXT("SetRaidDifficulty(1)")), !bInGroup || bLeader, true, Raid == 1));
			Choices.Add(Line(TEXT("25 Player"), Lua(TEXT("SetRaidDifficulty(2)")), !bInGroup || bLeader, true, Raid == 2));
			Entries.Add(Nested(TEXT("Raid Difficulty"), MoveTemp(Choices)));
		}
		if (bLeader)
		{
			TArray<FWowUnitMenuEntry> Choices;
			Choices.Add(Line(TEXT("Free for All"), Lua(TEXT("SetLootMethod('freeforall')"))));
			Choices.Add(Line(TEXT("Round Robin"), Lua(TEXT("SetLootMethod('roundrobin')"))));
			Choices.Add(Line(TEXT("Master Looter"), Lua(TEXT("SetLootMethod('master')"))));
			Choices.Add(Line(TEXT("Group Loot"), Lua(TEXT("SetLootMethod('group')"))));
			Choices.Add(Line(TEXT("Need before Greed"), Lua(TEXT("SetLootMethod('needbeforegreed')"))));
			Entries.Add(Nested(TEXT("Loot Method"), MoveTemp(Choices)));
		}
	}
	// Raid Target Icon (everyone outside a group, the leader/assistants in one): MSG_RAID_TARGET_UPDATE.
	if (!bInGroup || bLeader || Group.LocalPlayerIsAssistant())
	{
		static const TCHAR* Icons[] = { TEXT("Star"), TEXT("Circle"), TEXT("Diamond"), TEXT("Triangle"), TEXT("Moon"), TEXT("Square"), TEXT("Cross"), TEXT("Skull") };
		const int32 Current = Handler.RaidTargets.GetIconIndex(Guid);
		TArray<FWowUnitMenuEntry> IconChoices;
		const int64 Target = static_cast<int64>(Guid);
		for (int32 Icon = 0; Icon < 8; ++Icon)
			IconChoices.Add(Line(Icons[Icon], [this, Icon, Target, bInGroup]()
			{
				if (!ConnectionManager) return;
				ConnectionManager->SendSetRaidTargetIcon(Icon, Target);
				// Outside a group the server ignores raid target icons; the client keeps them locally
				// (Stefan 10.10: Skull on himself showed nothing).
				if (!bInGroup)
				{
					auto& IconPackets = ConnectionManager->PacketHandler;
					IconPackets.RaidTargets.SetTarget(static_cast<uint8>(Icon), static_cast<uint64>(Target));
					IconPackets.OnRaidTargetsUpdated.Broadcast(IconPackets.RaidTargets);
				}
			}, true, true, Current == Icon));
		IconChoices.Add(Line(TEXT("None"), [this, Guid, Current, bInGroup]()
		{
			if (!ConnectionManager) return;
			if (!bInGroup && Current != INDEX_NONE)
			{
				auto& IconPackets = ConnectionManager->PacketHandler;
				IconPackets.RaidTargets.SetTarget(static_cast<uint8>(Current), 0);
				IconPackets.OnRaidTargetsUpdated.Broadcast(IconPackets.RaidTargets);
			}
			TArray<uint8> Data;
			Data.Add(0xFF); // clear
			Data.Append(reinterpret_cast<const uint8*>(&Guid), sizeof(Guid));
			ConnectionManager->SendRawPacket(WowOpcode::MSG_RAID_TARGET_UPDATE, Data);
		}, Current != INDEX_NONE));
		Entries.Add(Nested(TEXT("Raid Target Icon"), MoveTemp(IconChoices)));
	}
	Entries.Add(Line(TEXT("Cancel"), []() {}));

	if (!UnitMenuWidget.IsValid())
	{
		UnitMenuWidget = SNew(SWowUnitMenu);
		FWowUiSurfaces::Add(UnitMenuWidget.ToSharedRef(), EWowUiZone::Popups, 3000, TEXT("NativeUnitMenu"));
	}
	const FVector2D Cursor = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
	// The unit tooltip of the frame that was right-clicked would stay on the pointer: the menu
	// covers the screen, so the frame never gets its OnLeave (Stefan 10.10).
	if (UIManager && UIManager->GetEventSystem())
		UIManager->GetEventSystem()->ExecuteLuaString(TEXT("if GameTooltip then GameTooltip:Hide() end"), TEXT("=UnitMenuTooltip"));
	UnitMenuWidget->Open(Entries, Cursor + FVector2D(4, 4), Frames->GetUIScale());
	return true;
}

void AWowGameplayController::OpenInspect(uint64 Guid)
{
	if (!ConnectionManager || !GameUI || !UIManager || !GetWorld()) return;
	const FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Guid);
	if (!Entity || !Entity->IsPlayer()) return;
	const FWowPlayerEntity& InspectedPlayer = *static_cast<const FWowPlayerEntity*>(Entity);

	// The inspected player's model, from their live appearance and visible gear.
	AWowWorldManager* WorldManager = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(GetWorld(), AWowWorldManager::StaticClass()));
	if (WorldManager && WorldManager->GetMpqManager() && WorldManager->GetAssetCache())
	{
		if (!InspectPreview)
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = FName(TEXT("WowInspectPreview"));
			InspectPreview = GetWorld()->SpawnActor<AWowCharacterPreview>(AWowCharacterPreview::StaticClass(),
				FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
			// r301: own rig spot and channel 2 (the character sheet's rig sits at the default spot
			// on channel 1; sharing it put two key lights on both models).
			if (InspectPreview) { InspectPreview->SetRig(FVector(-40000.0f, 0.0f, -500000.0f), 2); InspectPreview->SetAllowHDModel(false); } // r315 (restored r347): no 1 s HD hitch on inspect
			if (InspectPreview) InspectPreview->Setup(WorldManager, 512, 660, EWowCharacterPreviewMode::PaperDoll);
		}
		if (InspectPreview)
		{
			FWowCharacterInfo Info;
			Info.Race = InspectedPlayer.GetRaceId();
			Info.Gender = InspectedPlayer.GetGenderId();
			Info.Skin = InspectedPlayer.GetFieldByte(PlayerField::BYTES, 0);
			Info.Face = InspectedPlayer.GetFieldByte(PlayerField::BYTES, 1);
			Info.HairStyle = InspectedPlayer.GetFieldByte(PlayerField::BYTES, 2);
			Info.HairColor = InspectedPlayer.GetFieldByte(PlayerField::BYTES, 3);
			Info.FacialHair = InspectedPlayer.GetFieldByte(PlayerField::BYTES_2, 0);
			for (uint8 Slot = 0; Slot < 19; ++Slot)
			{
				if (Slot == PlayerField::EQUIPMENT_SLOT_RANGED) continue;
				const uint32 Entry = InspectedPlayer.GetField(PlayerField::VISIBLE_ITEM_BASE + Slot * 2);
				if (!Entry) continue;
				uint32 DisplayId = 0, InventoryType = 0;
				if (const FWowItemTemplateInfo* Template = ConnectionManager->PacketHandler.ItemTemplates.Find(Entry))
				{ DisplayId = Template->DisplayId; InventoryType = Template->InventoryType; }
				else if (const FItemDbcEntry* Item = FDbcStore::Get().Items().GetById(Entry))
				{ DisplayId = Item->DisplayInfoID; InventoryType = uint32(Item->InventoryType); }
				if (!DisplayId) continue;
				if (Slot == PlayerField::EQUIPMENT_SLOT_OFFHAND && (InventoryType == 13 || InventoryType == 21)) InventoryType = 22;
				FWowCharacterEquipment& Equipment = Info.Equipment.AddDefaulted_GetRef();
				Equipment.DisplayId = int32(DisplayId);
				Equipment.InventoryType = uint8(InventoryType);
			}
			InspectPreview->ShowCharacter(Info);
			InspectPreview->SetPreviewActive(true);
		}
	}
	if (!InspectWidget.IsValid())
	{
		InspectWidget = SNew(SWowInspectFrame, ConnectionManager).UIManager(UIManager);
		InspectWidget->OnRotateModel = [this](float Degrees) { if (InspectPreview) InspectPreview->RotateCharacter(Degrees); };
		GameUI->AddManagedPanel(TEXT("NativeInspect"), InspectWidget.ToSharedRef(), InspectWidget->GetPanelSize(),
			[this]() { if (InspectWidget.IsValid() && InspectWidget->IsOpen()) InspectWidget->Close(); });
	}
	InspectWidget->Open(Guid, InspectPreview ? InspectPreview->GetRenderTarget() : nullptr);
}

void AWowGameplayController::InteractWithWorldEntity(uint64 ClickedGuid)
{
	const FWowEntity* TargetEntity = nullptr;
	if (ConnectionManager)
	{
		TargetEntity = ConnectionManager->PacketHandler.EntityManager.Find(ClickedGuid);
	}

	if (!TargetEntity)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Right-click: target %llu not found in entity manager"), ClickedGuid);
		return;
	}

	UE_LOG(LogWowGameplay, Log, TEXT("Right-click: target=%llu IsUnit=%d IsPlayer=%d TypeMask=0x%08X Kind=%d"),
		ClickedGuid, TargetEntity->IsUnit() ? 1 : 0, TargetEntity->IsPlayer() ? 1 : 0,
		TargetEntity->TypeMask, static_cast<int32>(TargetEntity->GetEntityKind()));

	if (TargetEntity->IsGameObject())
	{
		const uint8 GameObjectType = TargetEntity->GetFieldByte(GameObjectField::BYTES_1, 1);
		UE_LOG(LogWowGameplay, Log, TEXT("Right-click gameobject %llu: GoType=%u"), ClickedGuid, GameObjectType);

		if (GameObjectType == WowGameObjectType::MAILBOX)
		{
			UE_LOG(LogWowGameplay, Log, TEXT("  -> Opening mailbox"));
			ShowMailbox(static_cast<int64>(ClickedGuid));
		}
		else if (GameObjectType == 34)
		{
			// r285: the guild vault. Like the client, open Blizzard_GuildBankUI (UIParent loads it on
			// GUILDBANKFRAME_OPENED) and activate the banker; the server checks guild and range.
			UE_LOG(LogWowGameplay, Log, TEXT("  -> Opening guild bank"));
			OutgoingMove.LastSendSeconds = 0.0;
			SendMovementUpdate();
			ConnectionManager->OpenGuildBank(ClickedGuid);
			FireUIEvent(TEXT("GUILDBANKFRAME_OPENED"));
		}
        else if (static_cast<const FWowGameObjectEntity*>(TargetEntity)->CanInteractNow())
        {
            // Remember Shift (manual loot with auto loot on) for nodes and chests too.
            if (GameUI) GameUI->PrepareLootRequest(ClickedGuid);
            // r373 (Stefan: "Out of range" right next to a chest): while running, the server only has
            // our position from the last heartbeat (up to 0.5 s / 3.5 yd old). Send the current
            // position first, so the server measures the reach from where we really stand.
            OutgoingMove.LastSendSeconds = 0.0;
            SendMovementUpdate();
            if (const APawn* UsePawn = GetPawn())
                UE_LOG(LogWowGameplay, Log, TEXT("Use object %llu: %.1f yd from us (our entity record %.1f yd)"), ClickedGuid,
                    FVector::Dist(FWowCoordinate::UEToWow(UsePawn->GetActorLocation()), TargetEntity->Movement.Position),
                    [&]() { const FWowEntity* Me = ConnectionManager->PacketHandler.EntityManager.Find(ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid);
                        return Me ? FVector::Dist(Me->Movement.Position, TargetEntity->Movement.Position) : -1.0; }());
            ConnectionManager->SendGameObjectUse(static_cast<int64>(ClickedGuid));
        }
		return;
	}

	if (!TargetEntity->IsUnit()) return;

	// Right-clicking another player only selects them. Trading remains an
	// explicit menu/UI action and must never happen from the default world click.
	if (TargetEntity->IsPlayer())
	{
		UE_LOG(LogWowGameplay, Verbose,
			TEXT("Right-click player %llu: target selected, no default interaction"), ClickedGuid);
		return;
	}

	// Check if entity is dead (health = 0) â€” loot instead of interact
	int32 Health = TargetEntity->GetHealth();
	if (Health <= 0)
	{
		UE_LOG(LogWowGameplay, Log, TEXT("Right-click: entity %llu has 0 health, looting"), ClickedGuid);
		if (GameUI) GameUI->PrepareLootRequest(ClickedGuid);
		ConnectionManager->SendLoot(static_cast<int64>(ClickedGuid));
		return;
	}

	// Reaction is authoritative for the default world action. Both hostile
	// (red) and attackable neutral (yellow) units start the persistent attack;
	// their quest/vendor flags must not turn an enemy click into conversation.
	if (IsWorldUnitAttackable(*TargetEntity))
	{
		UE_LOG(LogWowGameplay, Log, TEXT("Right-click attackable unit %llu: starting auto-attack"), ClickedGuid);
		StartAutoAttack();
		return;
	}

    if(!GameUI || !GameUI->IsNpcInteractionInRange(ClickedGuid)) return;
	// r504 (Stefan: Gornek's and Galgar's quest windows only came up after "mass clicking"): the
	// server measures the reach from the position it last got from us (a heartbeat up to 0.5 s / 3.5 yd
	// old while walking up), and ignores a hello from out of its range without an answer. Send the
	// current position first, as for objects since r373.
	OutgoingMove.LastSendSeconds = 0.0;
	SendMovementUpdate();

	// Check friendly NPC flags to determine interaction type.
	uint32 NpcFlags = TargetEntity->GetField(UnitField::NPC_FLAGS);
	if (NpcFlags != 0 && !(NpcFlags & WowNpcFlags::MAILBOX)) PlayNpcClickSound(ClickedGuid);
	UE_LOG(LogWowGameplay, Log, TEXT("Right-click NPC %llu: NpcFlags=0x%08X"), ClickedGuid, NpcFlags);

	if (NpcFlags & WowNpcFlags::MAILBOX)
	{
		UE_LOG(LogWowGameplay, Log, TEXT("  -> Opening mailbox"));
		ShowMailbox(static_cast<int64>(ClickedGuid));
	}
	else if (NpcFlags & (WowNpcFlags::GOSSIP | WowNpcFlags::QUESTGIVER))
	{
		ConnectionManager->SendGossipHello(static_cast<int64>(ClickedGuid));
	}
	else if (NpcFlags & WowNpcFlags::BANKER)
	{
		UE_LOG(LogWowGameplay, Log, TEXT("  -> Sending banker activate"));
		ConnectionManager->SendBankerActivate(static_cast<int64>(ClickedGuid));
	}
	else if (NpcFlags & WowNpcFlags::VENDOR)
	{
		UE_LOG(LogWowGameplay, Log, TEXT("  -> Requesting vendor inventory"));
		ConnectionManager->SendListInventory(static_cast<int64>(ClickedGuid));
	}
	else if (NpcFlags & WowNpcFlags::INTERACTABLE)
	{
		// This NPC has interactable flags â€” send gossip/questgiver hello
		UE_LOG(LogWowGameplay, Log, TEXT("  -> Sending gossip hello (interactable NPC)"));
		ConnectionManager->SendGossipHello(static_cast<int64>(ClickedGuid));
	}
	else
	{
		UE_LOG(LogWowGameplay, Verbose,
			TEXT("  -> Friendly NPC has no default right-click interaction"));
	}
}

void AWowGameplayController::OnRightClickReleased()
{
	bRightMouseWasDown = false;
	WowIsoClickMove::GHolding = false; // r621: the walk goes on to the last point; steering stops
	if (WowIsoClickMove::GPressPending)
	{
		// r628: a right click (cursor moved less than 6 px) walks there / to the unit; a drag turned the camera.
		WowIsoClickMove::GPressPending = false;
		float ReleaseX = 0.0f, ReleaseY = 0.0f;
		GetMousePosition(ReleaseX, ReleaseY);
		FVector Ground = FVector::ZeroVector;
		if (FWowIsometricCamera::IsActive()
			&& FVector2D::Distance(FVector2D(ReleaseX, ReleaseY), WowIsoClickMove::GPressCursor) <= 6.0f)
		{
			bool bStart = false;
			if (WowIsoClickMove::GPressGuid)
			{
				WowIsoClickMove::GGuid = WowIsoClickMove::GPressGuid;
				bStart = true;
			}
			else if (WowIsoGroundUnderCursor(this, Ground))
			{
				WowIsoClickMove::GGuid = 0;
				WowIsoClickMove::GTarget = Ground;
				bStart = true;
			}
			if (bStart)
			{
				// r662 (Stefan: "if I have auto-run on and right click, the right-click must be prioritized"): the
				// click ends auto-run (which stopped the walk at once), like a right-click move in the original.
				if (AWowPlayerCharacter* ClickRunner = Cast<AWowPlayerCharacter>(GetPawn())) ClickRunner->bIsAutoRunning = false;
				WowIsoClickMove::GActive = true;
				WowIsoClickMove::GHolding = false;
				if (APawn* ClickPawn = GetPawn()) WowIsoClickMove::GLastProgressPos = ClickPawn->GetActorLocation();
				WowIsoClickMove::GLastProgressSeconds = FPlatformTime::Seconds();
				GWowFollowGuid = 0;
			}
		}
		return;
	}

	if (IsNativeModalUiVisible())
	{
		bMouseChordMovingForward = false;
		PressedRightMouseFrameHandle = -1;
		if (bRightMouseLooking)
		{
			EndRightMouseLook();
		}
		return;
	}

	bMouseChordMovingForward = false;
	if (GWowPendingRightLoot)
	{
		// A click, not a turn: camera barely moved and the button was not held long.
		const FRotator Now = PlayerCameraManager ? PlayerCameraManager->GetCameraRotation() : GetControlRotation();
		const bool bTurned = FMath::Abs(FRotator::NormalizeAxis(Now.Yaw - GWowRightLootPressRotation.Yaw)) > 2.f
			|| FMath::Abs(FRotator::NormalizeAxis(Now.Pitch - GWowRightLootPressRotation.Pitch)) > 2.f
			|| FPlatformTime::Seconds() - GWowRightLootPressSeconds > 0.8;
		TFunction<void()> Loot = MoveTemp(GWowPendingRightLoot);
		GWowPendingRightLoot = nullptr;
		if (!bTurned) Loot();
	}
	HandleUiMouseRelease(TEXT("RightButton"), PressedRightMouseFrameHandle);
	EndRightMouseLook();
}

void AWowGameplayController::BeginRightMouseLook()
{
	if (bMouseInputNeedsRearmAfterFocusChange || !IsWowClientProcessForeground())
	{
		return;
	}

	if (bRightMouseLooking)
	{
		// A stale captured state makes later right-clicks look dead. Reset the old
		// state in-place and let this press establish a new anchor.
		bRightMouseLooking = false;
		bMouseChordMovingForward = false;
		RightMouseReleaseMissingFrames = 0;
		bHasSavedRightMousePosition = false;
		bHasLastMouseLookPosition = false;
		bHasMouseLookAnchor = false;
		PendingMouseTurn = 0.0f;
		PendingMouseLookUp = 0.0f;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	bHasSavedRightMousePosition = GetMousePosition(MouseX, MouseY);
	if (bHasSavedRightMousePosition)
	{
		SavedRightMouseX = FMath::RoundToInt(MouseX);
		SavedRightMouseY = FMath::RoundToInt(MouseY);
	}

	bRightMouseLooking = true;
	bMouseChordMovingForward = IsInputKeyDown(EKeys::LeftMouseButton);
	RightMouseLookStartedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	RightMouseReleaseMissingFrames = 0;
	if (UIManager && UIManager->GetFrameManager())
	{
		UIManager->GetFrameManager()->SetMouseInteractionSuppressed(true);
	}
	bHasLastMouseLookPosition = false;
	ApplyWowMouseLookInputMode(this, true);
	PrimeMouseLookAnchor();
	CenterMouseLookCursor();
	bShowMouseCursor = false;
	CurrentMouseCursor = EMouseCursor::None;
	SetNativePlatformCursorVisible(false);
}

void AWowGameplayController::EndRightMouseLook()
{
	if (!bRightMouseLooking)
	{
		return;
	}

	EnsureGameplayCameraRotationInitialized();
	const FRotator FinalControlRotation(GameplayCameraPitchDegrees, GameplayCameraYawDegrees, 0.0f);
	bRightMouseLooking = false;
	RightMouseReleaseMissingFrames = 0;
	bHasLastMouseLookPosition = false;
	bHasMouseLookAnchor = false;
	bIgnoreNextMouseLookDelta = false;
	PendingMouseTurn = 0.0f;
	PendingMouseLookUp = 0.0f;
	if (!bLeftMouseLooking && UIManager && UIManager->GetFrameManager())
	{
		UIManager->GetFrameManager()->SetMouseInteractionSuppressed(false);
	}
	bShowMouseCursor = true;
	CurrentMouseCursor = EMouseCursor::None;
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}
	ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
	FocusWowGameViewport();
	ApplyGameplayCameraRotation();
	if (bHasSavedRightMousePosition)
	{
		SetMouseLocation(SavedRightMouseX, SavedRightMouseY);
		bIgnoreNextMouseLookDelta = true;
	}
	// r713 (Stefan: "if you sit and you rightclick in another direction, she will insta-turn in that direction, stop
	// that"): the drag already only orbited the camera while sitting, but letting go of the button snapped the
	// seated body round to the camera. A sitting character keeps its facing.
	if (!bLocalSitting)
	{
		DesiredFacingYawDegrees = FinalControlRotation.Yaw;
		bHasDesiredFacingYaw = true;
		if (APawn* ControlledPawn = GetPawn())
		{
			ControlledPawn->SetActorRotation(FRotator(0.0f, FinalControlRotation.Yaw, 0.0f));
		}
	}
	bHasSavedRightMousePosition = false;
	SetNativePlatformCursorVisible(false);
}

void AWowGameplayController::OnTabTarget()
{
	if (!ConnectionManager) return;

	APawn* MyPawn = GetPawn();
	if (!MyPawn) return;

	FVector MyPos = MyPawn->GetActorLocation();
	float ClosestDist = MAX_FLT;
	uint64 ClosestGuid = 0;
	AActor* ClosestActor = nullptr;

	// Find closest NPC/hostile entity (skip current target to cycle)
	for (const auto& Pair : SpawnedEntityActors)
	{
		if (!Pair.Value || !IsValid(Pair.Value)) continue;
		if (Pair.Key == TargetGuid) continue; // Skip current target to cycle

		// Only target units (not players for tab target)
		const FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Pair.Key);
		if (!Entity || !Entity->IsUnit() || Entity->IsPlayer()) continue;

		float Dist = FVector::Dist(MyPos, Pair.Value->GetActorLocation());
		if (Dist < ClosestDist && Dist < 4000000.0f) // ~400 yards max range
		{
			ClosestDist = Dist;
			ClosestGuid = Pair.Key;
			ClosestActor = Pair.Value;
		}
	}

	if (ClosestGuid != 0)
	{
		SetTarget(ClosestGuid);
	}
	else if (TargetGuid != 0)
	{
		// No other targets â€” clear
		SetTarget(0);
	}
}

UWowAnimationController* AWowGameplayController::FindSpellAnimationController(uint64 CasterGuid) const
{
	if (!ConnectionManager)
	{
		return nullptr;
	}

	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	if (CasterGuid == LocalGuid)
	{
		if (const AWowPlayerCharacter* PlayerCharacter = Cast<AWowPlayerCharacter>(GetPawn()))
		{
			return PlayerCharacter->AnimationController;
		}
		return nullptr;
	}

	const TObjectPtr<AActor>* FoundActor = SpawnedEntityActors.Find(CasterGuid);
	return FoundActor && FoundActor->Get()
		? FWowCharacterBuilder::GetAnimationController(FoundActor->Get())
		: nullptr;
}

void AWowGameplayController::EndCasterSpellAnimation(uint64 CasterGuid)
{
	if (UWowAnimationController* AnimationController = FindSpellAnimationController(CasterGuid))
	{
		AnimationController->EndSpellPresentation();
	}
}

void AWowGameplayController::SheathCasterWeaponsForSpell(
	uint64 CasterGuid, uint32 SpellId, float SafetyDurationSeconds)
{
	static constexpr uint32 SpellAttrDoNotSheath = 0x00040000;
	if (const FSpellDbcEntry* SpellEntry = FDbcStore::Get().Spells().GetById(SpellId);
		SpellEntry && (SpellEntry->Attributes & SpellAttrDoNotSheath) != 0)
	{
		return;
	}

	const double SafetyRestoreAt = GetWorld()
		? GetWorld()->GetTimeSeconds() + FMath::Max(SafetyDurationSeconds, 1.0f)
		: FPlatformTime::Seconds() + FMath::Max(SafetyDurationSeconds, 1.0f);
	if (SpellSheathedWeapons.Contains(CasterGuid))
	{
		PendingSpellWeaponRestoreAt.FindOrAdd(CasterGuid) = SafetyRestoreAt;
		return;
	}

	AActor* CasterActor = ResolveSpellActor(CasterGuid);
	USkeletalMeshComponent* CharacterMesh = CasterActor
		? CasterActor->FindComponentByClass<USkeletalMeshComponent>()
		: nullptr;
	if (!CasterActor || !CharacterMesh)
	{
		return;
	}

	static const FName SheathSockets[] = {
		TEXT("WowSheathMainHand"), TEXT("WowSheathOffHand"), TEXT("WowSheathShield"),
		TEXT("WowStaffBackLeft"), TEXT("WowStaffBackRight"),
		TEXT("WowSheathLargeWeaponLeft"), TEXT("WowSheathLargeWeaponRight"),
		TEXT("WowSheathHipWeaponLeft"), TEXT("WowSheathHipWeaponRight")
	};
	TInlineComponentArray<UStaticMeshComponent*> EquipmentComponents(CasterActor);
	TArray<FSpellWeaponRestoreState> RestoreStates;
	for (UStaticMeshComponent* Equipment : EquipmentComponents)
	{
		if (!Equipment || !Equipment->ComponentHasTag(TEXT("WowHeldWeapon")))
		{
			continue;
		}

		FName SheathSocket = NAME_None;
		for (const FName Candidate : SheathSockets)
		{
			if (Equipment->ComponentHasTag(Candidate) && CharacterMesh->DoesSocketExist(Candidate))
			{
				SheathSocket = Candidate;
				break;
			}
		}
		if (SheathSocket.IsNone())
		{
			continue;
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("testspellworldless")))
		{
			int32 TestSocket = 0;
			if (FParse::Value(FCommandLine::Get(), TEXT("-testsheathsocket="), TestSocket))
			{
				switch (TestSocket)
				{
				case 26: SheathSocket = TEXT("WowSheathMainHand"); break;
				case 27: SheathSocket = TEXT("WowSheathOffHand"); break;
				case 28: SheathSocket = TEXT("WowSheathShield"); break;
				case 30: SheathSocket = TEXT("WowSheathLargeWeaponLeft"); break;
				case 31: SheathSocket = TEXT("WowSheathLargeWeaponRight"); break;
				case 32: SheathSocket = TEXT("WowSheathHipWeaponLeft"); break;
				case 33: SheathSocket = TEXT("WowSheathHipWeaponRight"); break;
				default: break;
				}
			}
		}

		FSpellWeaponRestoreState& State = RestoreStates.AddDefaulted_GetRef();
		State.Component = Equipment;
		State.OriginalParent = Equipment->GetAttachParent();
		State.OriginalSocket = Equipment->GetAttachSocketName();
		State.OriginalRelativeTransform = Equipment->GetRelativeTransform();
		Equipment->AttachToComponent(CharacterMesh,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, SheathSocket);
        ApplySheathedWeaponTransform(Equipment, CharacterMesh);

		// Visual-verification override used only by the deterministic spell preview.
		// It lets attachment axes be calibrated without changing live equipment data.
		if (FParse::Param(FCommandLine::Get(), TEXT("testspellworldless")))
		{
			float TestPitch = 0.0f;
			float TestYaw = 0.0f;
			float TestRoll = 0.0f;
			const bool bHasTestRotation =
				FParse::Value(FCommandLine::Get(), TEXT("-testsheathpitch="), TestPitch) |
				FParse::Value(FCommandLine::Get(), TEXT("-testsheathyaw="), TestYaw) |
				FParse::Value(FCommandLine::Get(), TEXT("-testsheathroll="), TestRoll);
			if (bHasTestRotation)
			{
				Equipment->SetRelativeRotation(FRotator(TestPitch, TestYaw, TestRoll));
			}
			float TestOffsetX = 0.0f;
			float TestOffsetY = 0.0f;
			float TestOffsetZ = 0.0f;
			const bool bHasTestOffset =
				FParse::Value(FCommandLine::Get(), TEXT("-testsheathx="), TestOffsetX) |
				FParse::Value(FCommandLine::Get(), TEXT("-testsheathy="), TestOffsetY) |
				FParse::Value(FCommandLine::Get(), TEXT("-testsheathz="), TestOffsetZ);
			if (bHasTestOffset)
			{
				Equipment->AddWorldOffset(FVector(TestOffsetX, TestOffsetY, TestOffsetZ));
			}
		}
		UE_LOG(LogWowGameplay, Log, TEXT("Spell sheath component %s -> %s"),
			*Equipment->GetName(), *SheathSocket.ToString());
	}

	if (!RestoreStates.IsEmpty())
	{
		SpellSheathedWeapons.Add(CasterGuid, MoveTemp(RestoreStates));
		PendingSpellWeaponRestoreAt.Add(CasterGuid, SafetyRestoreAt);
		UE_LOG(LogWowGameplay, Log, TEXT("Temporarily sheathed %d weapon(s) for spell %u by %llu"),
			SpellSheathedWeapons[CasterGuid].Num(), SpellId, CasterGuid);
	}
}

void AWowGameplayController::ScheduleCasterWeaponRestore(uint64 CasterGuid, float DelaySeconds)
{
	if (SpellSheathedWeapons.Contains(CasterGuid) && GetWorld())
	{
		PendingSpellWeaponRestoreAt.FindOrAdd(CasterGuid) =
			GetWorld()->GetTimeSeconds() + FMath::Max(DelaySeconds, 0.0f);
	}
}

void AWowGameplayController::RestoreCasterWeaponsAfterSpell(uint64 CasterGuid)
{
	TArray<FSpellWeaponRestoreState> States;
	if (!SpellSheathedWeapons.RemoveAndCopyValue(CasterGuid, States))
	{
		PendingSpellWeaponRestoreAt.Remove(CasterGuid);
		return;
	}

	for (const FSpellWeaponRestoreState& State : States)
	{
		USceneComponent* Equipment = State.Component.Get();
		USceneComponent* OriginalParent = State.OriginalParent.Get();
		if (!Equipment || !OriginalParent)
		{
			continue;
		}
		Equipment->AttachToComponent(OriginalParent,
			FAttachmentTransformRules::KeepRelativeTransform, State.OriginalSocket);
		Equipment->SetRelativeTransform(State.OriginalRelativeTransform);
	}
    PendingSpellWeaponRestoreAt.Remove(CasterGuid);
    // A cast may have begun with a sheathed staff, then auto-attack started.
    // Do not resurrect the stale saved socket after the cast ends.
    if (ConnectionManager && CasterGuid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
        ApplyLocalWeaponPresentation();
}

void AWowGameplayController::UpdateSpellWeaponSheaths()
{
	if (!GetWorld() || PendingSpellWeaponRestoreAt.IsEmpty())
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	TArray<uint64> ReadyToRestore;
	for (const TPair<uint64, double>& Pair : PendingSpellWeaponRestoreAt)
	{
		if (Now >= Pair.Value &&
			!ActiveTimedCastSpells.Contains(Pair.Key) &&
			!ActiveChannelSpells.Contains(Pair.Key))
		{
			ReadyToRestore.Add(Pair.Key);
		}
	}
	for (const uint64 CasterGuid : ReadyToRestore)
	{
		RestoreCasterWeaponsAfterSpell(CasterGuid);
	}
}

void AWowGameplayController::ShowSpellProgressBar(
	const FString& SpellName, float DurationSeconds, bool bChannel)
{
	if (CastBarWidget.IsValid())
	{
		CastBarWidget->StartCast(SpellName, DurationSeconds);
		CastBarWidget->UpdateProgress(bChannel ? 1.0f : 0.0f);
	}

	if (!UIManager || !UIManager->GetFrameManager())
	{
		return;
	}

	FWowFrameManager* FrameManager = UIManager->GetFrameManager();
	const int64 FrameHandle = FrameManager->FindFrame(TEXT("CastingBarFrame"));
	if (FrameHandle < 0)
	{
		return;
	}

	FrameManager->SetFrameVisible(FrameHandle, true);
	if (UProgressBar* ProgressBar = FrameManager->GetStatusBarWidget(FrameHandle))
	{
		ProgressBar->SetPercent(bChannel ? 1.0f : 0.0f);
		FrameManager->UpdateStatusBarSpark(FrameHandle, bChannel ? 1.0f : 0.0f);
		ProgressBar->SetFillColorAndOpacity(bChannel
			? FLinearColor(0.15f, 0.75f, 0.25f, 1.0f)
			: FLinearColor(1.0f, 0.65f, 0.05f, 1.0f));
	}

	for (const TCHAR* LabelName : {TEXT("CastingBarFrameText"), TEXT("CastingBarText")})
	{
		if (UTextBlock* Label = FrameManager->GetNamedFontString(LabelName))
		{
            Label->SetText(FText::FromString(SpellName));
            Label->SetJustification(ETextJustify::Center);
            auto Font=Label->GetFont(); Font.Size=FMath::RoundToInt(9.f*FrameManager->GetUIScale()); Label->SetFont(Font);
            // r275 (Stefan 13.10: "Hearthstone" sat low on the bar): the label's box is centred on the
            // bar by its own line height, and the capitals' optical offset is measured for THIS font
            // (the offset set at load was for the font before this resize, which pushed it down).
            if (auto* Slot=Cast<UCanvasPanelSlot>(Label->Slot))
            {
                Slot->SetAutoSize(true); Slot->SetAnchors(FAnchors(.5f,.5f));
                Slot->SetAlignment(FVector2D(.5f,.5f)); Slot->SetPosition(FVector2D::ZeroVector);
            }
            float CapOffset=0.f;
            if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
            {
                const TSharedRef<FSlateFontMeasure> Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
                const float LineH=static_cast<float>(Measure->GetMaxCharacterHeight(Font));
                const float Descender=-static_cast<float>(Measure->GetBaseline(Font));
                const float CapH=0.70f*Font.Size*(96.f/72.f);
                // Middle of the line box minus the middle of the capitals (baseline at LineH - Descender).
                if (LineH>0.f) CapOffset=FMath::RoundToFloat(LineH*.5f-((LineH-Descender)-CapH*.5f));
            }
            Label->SetRenderTranslation(FVector2D(0.f,CapOffset));
		}
	}
}

void AWowGameplayController::UpdateSpellProgressBar()
{
	if (!bIsCasting || CastDuration <= KINDA_SMALL_NUMBER || !GetWorld())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	const float Progress = bIsChanneling
		? FMath::Clamp((SpellBarEndTime - Now) / CastDuration, 0.0f, 1.0f)
		: FMath::Clamp((Now - CastStartTime) / CastDuration, 0.0f, 1.0f);

	if (CastBarWidget.IsValid())
	{
		CastBarWidget->UpdateProgress(Progress);
	}
	if (UIManager && UIManager->GetFrameManager())
	{
		FWowFrameManager* FrameManager = UIManager->GetFrameManager();
		const int64 FrameHandle = FrameManager->FindFrame(TEXT("CastingBarFrame"));
		if (FrameHandle >= 0)
		{
			FrameManager->SetFrameVisible(FrameHandle, true);
			if (UProgressBar* ProgressBar = FrameManager->GetStatusBarWidget(FrameHandle))
			{
				ProgressBar->SetPercent(Progress);
				FrameManager->UpdateStatusBarSpark(FrameHandle, Progress);
			}
		}
	}
}

void AWowGameplayController::HideSpellProgressBar()
{
	if (CastBarWidget.IsValid())
	{
		CastBarWidget->StopCast();
	}
	if (UIManager && UIManager->GetFrameManager())
	{
		FWowFrameManager* FrameManager = UIManager->GetFrameManager();
		FrameManager->SetFrameVisible(FrameManager->FindFrame(TEXT("CastingBarFrame")), false);
	}
}

void AWowGameplayController::FireUnitSpellcastEventForGuid(
	const FString& EventName, uint64 CasterGuid)
{
	if (!ConnectionManager || CasterGuid == 0)
	{
		return;
	}

	if (CasterGuid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
	{
		FireUIEvent(EventName, {TEXT("player")});
	}
	const bool bStarts = EventName == TEXT("UNIT_SPELLCAST_START") ||
		EventName == TEXT("UNIT_SPELLCAST_CHANNEL_START");
	const bool bStops = EventName == TEXT("UNIT_SPELLCAST_STOP") ||
		EventName == TEXT("UNIT_SPELLCAST_CHANNEL_STOP") ||
		EventName == TEXT("UNIT_SPELLCAST_FAILED") ||
		EventName == TEXT("UNIT_SPELLCAST_INTERRUPTED");
	auto FireForFrame = [this, &EventName, CasterGuid, bStarts, bStops](
		uint64 UnitGuid, const TCHAR* UnitToken, const TCHAR* SpellBarName)
	{
		if (CasterGuid != UnitGuid)
		{
			return;
		}
		FireUIEvent(EventName, {UnitToken});
		if (UIManager && UIManager->GetFrameManager() && (bStarts || bStops))
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			FrameManager->SetFrameVisible(FrameManager->FindFrame(SpellBarName), bStarts);
		}
	};
	if (CasterGuid == TargetGuid)
	{
		FireForFrame(TargetGuid, TEXT("target"), TEXT("TargetFrameSpellBar"));
	}
	if (UIManager && CasterGuid == UIManager->GetFocusGuid())
	{
		FireForFrame(UIManager->GetFocusGuid(), TEXT("focus"), TEXT("FocusFrameSpellBar"));
	}
}

// r499 (Stefan: "throwing animations appear as casting animation"): ranged weapon attacks have no
// animation in their spell visual kits; the original client picks it from the weapon. Throw, Shoot
// (bow / gun / crossbow), Auto Shot and the creatures' Shoot: the ranged weapon's own ready and attack
// animations (AnimationData 107 AttackThrown / 108 ReadyThrown, 29 ReadyBow / 46 AttackBow, 48
// ReadyRifle / 49 AttackRifle). 0 = not a ranged weapon attack (wands keep the spell pose).
static uint32 RangedWeaponSpellAnimation(UWowConnectionManager* Connection, uint64 CasterGuid, uint32 SpellId, bool bReady)
{
    enum class EKind : uint8 { None, Thrown, Bow, Gun };
    auto FromSubclass = [](uint32 Subclass)
    {
        return Subclass == 16 ? EKind::Thrown : (Subclass == 2 || Subclass == 18) ? EKind::Bow : Subclass == 3 ? EKind::Gun : EKind::None;
    };
    EKind Kind = SpellId == 2764 ? EKind::Thrown : (SpellId == 2480 || SpellId == 7919) ? EKind::Bow : SpellId == 7918 ? EKind::Gun : EKind::None;
    if (Kind == EKind::None && (SpellId == 75 || SpellId == 6660) && Connection)
    {
        // Auto Shot / creature Shoot: the caster's ranged weapon.
        if (const FWowEntity* Caster = Connection->PacketHandler.EntityManager.Find(CasterGuid))
        {
            const uint32 Entry = Caster->IsPlayer()
                ? Caster->GetField(PlayerField::VISIBLE_ITEM_BASE + PlayerField::EQUIPMENT_SLOT_RANGED * 2)
                : Caster->GetField(UnitField::BASE + 0x0032 + 2);
            if (const FWowItemTemplateInfo* Template = Entry ? Connection->PacketHandler.ItemTemplates.Find(Entry) : nullptr)
                Kind = FromSubclass(Template->SubClass);
            else if (const FItemDbcEntry* Item = Entry ? FDbcStore::Get().Items().GetById(Entry) : nullptr)
                Kind = FromSubclass(Item->SubclassID);
            if (Kind == EKind::None) Kind = EKind::Bow; // Auto Shot without a known weapon: a bow
        }
    }
    switch (Kind)
    {
    case EKind::Thrown: return bReady ? 108u : 107u;
    case EKind::Bow:    return bReady ? uint32(EWowAnimId::ReadyBow) : uint32(EWowAnimId::AttackBow);
    case EKind::Gun:    return bReady ? uint32(EWowAnimId::ReadyRifle) : uint32(EWowAnimId::AttackRifle);
    default:            return 0u;
    }
}

void AWowGameplayController::OnSpellStart(uint64 CasterGuid, uint32 SpellId, uint32 CastFlags, int32 CastTime)
{
	if (!ConnectionManager)
	{
		return;
	}

	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	const bool bLocalCaster = CasterGuid == LocalGuid;
	if (CastTime > 0)
	{
		FWowCombatLogEntry CastStart;
		CastStart.Event = TEXT("SPELL_CAST_START");
		CastStart.SourceGuid = CasterGuid;
		CastStart.SpellId = SpellId;
		OnCombatLogEntry(CastStart);
	}
	FString SpellName = TEXT("an ability");
	uint32 AuthoredReadyAnimation = 0;
	bool bOmniDirectional = false;
    bool bOpeningObject = false;
	const FDbcStore& DbcStore = FDbcStore::Get();
	if (const auto* SpellEntry = DbcStore.Spells().GetById(SpellId))
	{
		SpellName = SpellEntry->SpellName;
        for (uint32 Effect : SpellEntry->Effect) bOpeningObject |= Effect == 33;
        // r450 (Stefan: other players crafting played a spell-casting pose): tradeskill spells
        // (SPELL_ATTR0_TRADESPELL 0x20; not conjuring, which is a real spell) use the same "use"
        // pose as opening objects in the original client - the kit's own looping AnimID when it has one.
        bOpeningObject |= (SpellEntry->Attributes & 0x20u) != 0;
		if (const auto* VisualEntry = DbcStore.SpellVisuals().GetById(SpellEntry->SpellVisual[0]))
		{
			if (const auto* PrecastKit = DbcStore.SpellVisualKits().GetById(VisualEntry->PrecastKit))
			{
				AuthoredReadyAnimation = PrecastKit->StartAnimID;
				// Opening/gathering kits author their looping "use" pose in AnimID.
				if (bOpeningObject && PrecastKit->AnimID != 0) AuthoredReadyAnimation = PrecastKit->AnimID;
				bOmniDirectional = AuthoredReadyAnimation == static_cast<uint32>(EWowAnimId::ReadySpellOmni);
			}
		}
	}
	if (const uint32 RangedReady = RangedWeaponSpellAnimation(ConnectionManager, CasterGuid, SpellId, true))
	{
		AuthoredReadyAnimation = RangedReady; // r499
		bOmniDirectional = false;
	}
	SheathCasterWeaponsForSpell(CasterGuid, SpellId, CastTime / 1000.0f + 3.0f);

	if (CastTime > 0)
	{
		ActiveTimedCastSpells.Add(CasterGuid, SpellId);
		if (UIManager)
		{
			UIManager->BeginUnitSpellProgress(
				CasterGuid, SpellId, SpellName, CastTime / 1000.0f, false);
			AuraUpdateTimer = AuraUpdateInterval; // nameplate cast bars refresh next tick
		}
		FireUnitSpellcastEventForGuid(TEXT("UNIT_SPELLCAST_START"), CasterGuid);
		if (UWowAnimationController* AnimationController = FindSpellAnimationController(CasterGuid))
		{
			// Opening chests, herbs, ore and quest objects uses the character's
			// "use standing" animation in the original client, never a spell-casting
			// pose; only accept an authored opening animation that is not a spell pose.
			uint32 ReadyAnimation = AuthoredReadyAnimation;
			if (bOpeningObject)
			{
				const bool bSpellPose = ReadyAnimation == uint32(EWowAnimId::ReadySpellDirected)
					|| ReadyAnimation == uint32(EWowAnimId::ReadySpellOmni)
					|| ReadyAnimation == uint32(EWowAnimId::SpellPrecast)
					|| ReadyAnimation == uint32(EWowAnimId::SpellCast)
					|| ReadyAnimation == uint32(EWowAnimId::SpellCastDirected)
					|| ReadyAnimation == uint32(EWowAnimId::SpellCastOmni)
					|| ReadyAnimation > MAX_uint8;
				if (ReadyAnimation == 0 || bSpellPose) ReadyAnimation = uint32(EWowAnimId::EmoteUseStanding);
				bOmniDirectional = false;
			}
			AnimationController->BeginSpellCast(FWowSpellAnimationPolicy::ResolveReadyAnimation(ReadyAnimation, bOmniDirectional));
		}

		const int32 QueuedEffects = bOpeningObject ? 0 : SpawnSpellVisualEffect(
			CasterGuid, 0, SpellId, ESpellVisualPhase::Precast);
		if (QueuedEffects == 0 && !bOpeningObject)
		{
			SpawnCastGlowEffects(CasterGuid, SpellId);
		}
	}

	if (bLocalCaster)
	{
		if (CastTime > 0)
		{
			// r372: a cast started right after letting go of the keys slid on with the braking
			// momentum; the original client stops dead when the cast begins.
			if (ACharacter* CastChar = Cast<ACharacter>(GetPawn()))
				if (UCharacterMovementComponent* CastMove = CastChar->GetCharacterMovement();
					CastMove && CastMove->IsMovingOnGround() && CastChar->GetPendingMovementInputVector().IsNearlyZero()
					&& CastMove->GetCurrentAcceleration().IsNearlyZero())
					CastMove->StopMovementImmediately();
			bIsCasting = true;
			bIsChanneling = false;
			CurrentSpellId = SpellId;
			CastStartTime = GetWorld()->GetTimeSeconds();
			CastDuration = FMath::Max(CastTime / 1000.0f, KINDA_SMALL_NUMBER);
			SpellBarEndTime = CastStartTime + CastDuration;
			FireUIEvent(TEXT("CURRENT_SPELL_CAST_CHANGED"));
			ShowSpellProgressBar(SpellName, CastDuration, false);
		}

		AddCombatMessage(FString::Printf(TEXT("You cast %s"), *SpellName), FLinearColor::Yellow, true, false);
	}
	else
	{
		AddCombatMessage(FString::Printf(TEXT("%s casts %s"), *GetPlayerDisplayName(CasterGuid), *SpellName),
			FLinearColor(0.8f, 0.8f, 0.8f, 1.0f));
	}

	UE_LOG(LogWowGameplay, Log, TEXT("Spell start: caster=%llu spell=%u (%s) cast=%dms effects=%s"),
		CasterGuid, SpellId, *SpellName, CastTime,
		CastTime > 0 ? TEXT("precast") : TEXT("instant"));
}

void AWowGameplayController::OnSpellGo(
	uint64 CasterGuid, uint64 SpellTargetGuid, uint32 SpellId, uint32 CastFlags)
{
	if (!ConnectionManager)
	{
		return;
	}

	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	const bool bLocalCaster = CasterGuid == LocalGuid;
	{
		FWowCombatLogEntry CastSuccess;
		CastSuccess.Event = TEXT("SPELL_CAST_SUCCESS");
		CastSuccess.SourceGuid = CasterGuid;
		CastSuccess.DestGuid = SpellTargetGuid;
		CastSuccess.SpellId = SpellId;
		OnCombatLogEntry(CastSuccess);
	}
	const uint32* TimedSpell = ActiveTimedCastSpells.Find(CasterGuid);
	const bool bWasTimedCast = TimedSpell && *TimedSpell == SpellId;
	ActiveTimedCastSpells.Remove(CasterGuid);
	if (bWasTimedCast && UIManager)
	{
		UIManager->EndUnitSpellProgress(CasterGuid);
		AuraUpdateTimer = AuraUpdateInterval; // nameplate cast bars refresh next tick
	}
	if (bWasTimedCast)
	{
		FireUnitSpellcastEventForGuid(TEXT("UNIT_SPELLCAST_SUCCEEDED"), CasterGuid);
		FireUnitSpellcastEventForGuid(TEXT("UNIT_SPELLCAST_STOP"), CasterGuid);
	}
	ScheduleCasterWeaponRestore(CasterGuid, 0.85f);
	RemoveSpellVisuals(CasterGuid, true);
	RemoveCastGlowEffects(CasterGuid);

	uint32 AuthoredReleaseAnimation = 0;
	bool bOmniDirectional = !bWasTimedCast;
	bool bOpeningObject = false;
	// r612 (Stefan: "we're not supposed to take hands up when stealthing, that's for Vanish"): an instant
	// spell whose visual has no cast kit (Stealth 1784: visual 184, only a state kit) plays no release
	// flourish in the original client. Vanish has its own cast kit (404) with its animation and smoke.
	bool bNoReleaseFlourish = false;
	bool bLearnSpellCast = false; // r664
	const FDbcStore& DbcStore = FDbcStore::Get();
	if (const auto* SpellEntry = DbcStore.Spells().GetById(SpellId))
	{
		for (uint32 Effect : SpellEntry->Effect) bOpeningObject |= Effect == 33; // SPELL_EFFECT_OPEN_LOCK
		if (!bWasTimedCast && SpellEntry->SpellVisual[0] == 0) bNoReleaseFlourish = true; // r658: no visual at all
		// r664 (Stefan: "my characters still do the hands-up emote when we level up"): at a level-up TheraWoW teaches
		// the new spells by casting spell 59906 on the player twice (log: "Spell go: ... spell=59906" right after
		// LEVELUP_INFO). Its visual has a cast kit with the release animation. Learning a spell (effect 36
		// SPELL_EFFECT_LEARN_SPELL, 57 LEARN_PET_SPELL) never plays a pose.
		for (uint32 Effect : SpellEntry->Effect) bLearnSpellCast |= Effect == 36 || Effect == 57;
		// The level-up cast on TheraWoW is spell 59906 (Swift Hand of Justice, an aura) - a server-triggered spell.
		// Triggered spells arrive with CAST_FLAG_PENDING (0x01) in SMSG_SPELL_GO; the original client plays no
		// caster animation for them (procs, level-up gifts, item triggers).
		bLearnSpellCast |= (CastFlags & 0x01u) != 0;
		if (const auto* VisualEntry = DbcStore.SpellVisuals().GetById(SpellEntry->SpellVisual[0]))
		{
			// r658 (Stefan: characters "emote" when they level up - "the ding is good enough"): the server's level-up
			// spell has no cast kit either. Every instant spell whose visual has no cast or precast kit and no missile
			// plays no release flourish (r612 only did this for visuals with a state kit).
			bNoReleaseFlourish = !bWasTimedCast && VisualEntry->CastKit == 0 && VisualEntry->PrecastKit == 0
				&& VisualEntry->HasMissile == 0;
			if (const auto* CastKit = DbcStore.SpellVisualKits().GetById(VisualEntry->CastKit))
			{
				AuthoredReleaseAnimation = CastKit->AnimID;
				bOmniDirectional = bOmniDirectional ||
					AuthoredReleaseAnimation == static_cast<uint32>(EWowAnimId::SpellCastOmni);
			}
		}
	}
	if (const uint32 RangedRelease = RangedWeaponSpellAnimation(ConnectionManager, CasterGuid, SpellId, false))
	{
		AuthoredReleaseAnimation = RangedRelease; // r499
		bOmniDirectional = false;
	}
	// Opening an object ends the "use" pose without a spell-release flourish.
	if (UWowAnimationController* AnimationController = FindSpellAnimationController(CasterGuid))
	{
		if (bOpeningObject || bLearnSpellCast || (bNoReleaseFlourish && AuthoredReleaseAnimation == 0)) AnimationController->EndSpellPresentation();
		else AnimationController->PlaySpellRelease(FWowSpellAnimationPolicy::ResolveReleaseAnimation(
			AuthoredReleaseAnimation, bOmniDirectional));
	}

	if (bLocalCaster)
	{
		bIsCasting = false;
		bIsChanneling = false;
		CurrentSpellId = 0;
		CastStartTime = 0.0f;
		CastDuration = 0.0f;
		SpellBarEndTime = 0.0f;
		HideSpellProgressBar();
		FireUIEvent(TEXT("SPELL_UPDATE_COOLDOWN"));
		FireUIEvent(TEXT("ACTIONBAR_UPDATE_COOLDOWN"));
	}

	SpawnSpellVisualEffect(CasterGuid, CasterGuid, SpellId, ESpellVisualPhase::Cast);
	// Successful hit targets are dispatched separately after this release.
	// An all-miss cast must not invent an impact on the current target.

	UE_LOG(LogWowGameplay, Log, TEXT("Spell go: caster=%llu target=%llu spell=%u timed=%d"),
		CasterGuid, SpellTargetGuid, SpellId, bWasTimedCast ? 1 : 0);
}

void AWowGameplayController::OnChannelStart(uint64 CasterGuid, uint32 SpellId, uint32 DurationMs)
{
	if (!ConnectionManager || DurationMs == 0)
	{
		return;
	}

	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	const bool bLocalCaster = CasterGuid == LocalGuid;
	ActiveTimedCastSpells.Remove(CasterGuid);
	ActiveChannelSpells.Add(CasterGuid, SpellId);
	SheathCasterWeaponsForSpell(CasterGuid, SpellId, DurationMs / 1000.0f + 3.0f);
	RemoveSpellVisuals(CasterGuid, true);
	RemoveCastGlowEffects(CasterGuid);

	FString SpellName = TEXT("an ability");
	uint32 AuthoredChannelAnimation = 0;
	bool bOmniDirectional = false;
	const FDbcStore& DbcStore = FDbcStore::Get();
	if (const auto* SpellEntry = DbcStore.Spells().GetById(SpellId))
	{
		SpellName = SpellEntry->SpellName;
		if (const auto* VisualEntry = DbcStore.SpellVisuals().GetById(SpellEntry->SpellVisual[0]))
		{
			if (const auto* ChannelKit = DbcStore.SpellVisualKits().GetById(VisualEntry->ChannelKit))
			{
				AuthoredChannelAnimation = ChannelKit->AnimID;
				bOmniDirectional = AuthoredChannelAnimation == static_cast<uint32>(EWowAnimId::ChannelCastOmni);
			}
		}
	}
	if (UWowAnimationController* AnimationController = FindSpellAnimationController(CasterGuid))
	{
		AnimationController->BeginSpellChannel(FWowSpellAnimationPolicy::ResolveChannelAnimation(
			AuthoredChannelAnimation, bOmniDirectional));
	}
	if (SpawnSpellVisualEffect(CasterGuid, 0, SpellId, ESpellVisualPhase::Channel) == 0)
	{
		SpawnCastGlowEffects(CasterGuid, SpellId);
	}
	if (UIManager)
	{
		UIManager->BeginUnitSpellProgress(
			CasterGuid, SpellId, SpellName, DurationMs / 1000.0f, true);
		AuraUpdateTimer = AuraUpdateInterval; // nameplate cast bars refresh next tick
	}
	FireUnitSpellcastEventForGuid(TEXT("UNIT_SPELLCAST_CHANNEL_START"), CasterGuid);

	if (bLocalCaster)
	{
		bIsCasting = true;
		bIsChanneling = true;
		CurrentSpellId = SpellId;
		ActiveLocalChannelCasterGuid = CasterGuid;
		CastStartTime = GetWorld()->GetTimeSeconds();
		CastDuration = FMath::Max(DurationMs / 1000.0f, KINDA_SMALL_NUMBER);
		SpellBarEndTime = CastStartTime + CastDuration;
		ShowSpellProgressBar(SpellName, CastDuration, true);
	}
}

void AWowGameplayController::OnChannelUpdate(uint64 CasterGuid, uint32 RemainingMs)
{
	const uint32 SpellId = ActiveChannelSpells.FindRef(CasterGuid);
	if (RemainingMs > 0)
	{
		if (UIManager)
		{
			UIManager->UpdateUnitSpellProgress(CasterGuid, RemainingMs / 1000.0f);
		}
		FireUnitSpellcastEventForGuid(TEXT("UNIT_SPELLCAST_CHANNEL_UPDATE"), CasterGuid);
		if (ConnectionManager &&
			CasterGuid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid && GetWorld())
		{
			SpellBarEndTime = GetWorld()->GetTimeSeconds() + RemainingMs / 1000.0f;
		}
		return;
	}

	ActiveChannelSpells.Remove(CasterGuid);
	if (UIManager)
	{
		UIManager->EndUnitSpellProgress(CasterGuid);
		AuraUpdateTimer = AuraUpdateInterval; // nameplate cast bars refresh next tick
	}
	FireUnitSpellcastEventForGuid(TEXT("UNIT_SPELLCAST_CHANNEL_STOP"), CasterGuid);
	ScheduleCasterWeaponRestore(CasterGuid, 0.5f);
	EndCasterSpellAnimation(CasterGuid);
	RemoveSpellVisuals(CasterGuid, true);
	RemoveCastGlowEffects(CasterGuid);
	if (SpellId != 0)
	{
		SpawnSpellVisualEffect(CasterGuid, 0, SpellId, ESpellVisualPhase::ChannelDone);
	}

	if (ConnectionManager &&
		CasterGuid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
	{
		bIsCasting = false;
		bIsChanneling = false;
		CurrentSpellId = 0;
		ActiveLocalChannelCasterGuid = 0;
		CastStartTime = 0.0f;
		CastDuration = 0.0f;
		SpellBarEndTime = 0.0f;
		HideSpellProgressBar();
	}
}

void AWowGameplayController::OnSpellDelayed(uint64 CasterGuid, uint32 DelayMs)
{
	if (!ConnectionManager || DelayMs == 0 || !ActiveTimedCastSpells.Contains(CasterGuid))
	{
		return;
	}

	const float DelaySeconds = DelayMs / 1000.0f;
	if (UIManager)
	{
		UIManager->DelayUnitSpellProgress(CasterGuid, DelaySeconds);
		AuraUpdateTimer = AuraUpdateInterval; // nameplate cast bars refresh next tick
	}
	FireUnitSpellcastEventForGuid(TEXT("UNIT_SPELLCAST_DELAYED"), CasterGuid);
	if (CasterGuid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid &&
		bIsCasting && !bIsChanneling)
	{
		CastDuration += DelaySeconds;
		SpellBarEndTime += DelaySeconds;
		UE_LOG(LogWowGameplay, Log, TEXT("Local cast pushback: +%ums, new duration %.3fs"),
			DelayMs, CastDuration);
	}
}

void AWowGameplayController::MaybeApplyDebugSpellPreview()
{
    int32 TestEmote = -1;
    if (FParse::Param(FCommandLine::Get(), TEXT("offlineworld")) &&
        FParse::Value(FCommandLine::Get(), TEXT("-testemote="), TestEmote))
    {
        auto* PreviewCharacter = Cast<AWowPlayerCharacter>(GetPawn());
        if (!bDebugSpellPreviewStarted && PreviewCharacter && PreviewCharacter->AnimationController &&
            ConnectionManager && FDbcStore::Get().IsDeferredLoaded())
        {
            bDebugSpellPreviewStarted = true;
            const FVector LookAt = PreviewCharacter->GetActorLocation() + FVector(0,0,-30);
            const FVector CameraLocation = LookAt + FVector(320,320,170);
            if (ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(CameraLocation, (LookAt-CameraLocation).Rotation()))
                SetViewTarget(Camera);
            const auto* Emote = FDbcStore::Get().Emotes().GetById(TestEmote);
            auto* Entity = ConnectionManager->PacketHandler.EntityManager.Find(
                ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid);
            if (Emote && Entity)
            {
                if (Emote->Type == 1)
                    Entity->SetField(UnitField::BYTES_1, (Entity->GetField(UnitField::BYTES_1) & ~0xffu) | Emote->StandState);
                else if (Emote->IsLooping()) Entity->SetField(UnitField::NPC_EMOTESTATE, TestEmote);
                else PlayEmoteAnimation(PreviewCharacter, TestEmote);
                UE_LOG(LogWowGameplay, Log, TEXT("Authored emote preview: emote=%d animation=%u type=%u"), TestEmote, Emote->AnimationID, Emote->Type);
            }
        }
        return;
    }

	if (DebugSpellPreviewId <= 0 || !ConnectionManager || !GetWorld() ||
		!FDbcStore::Get().IsDeferredLoaded())
	{
		return;
	}

	CacheWorldResources();
	AWowPlayerCharacter* PlayerCharacter = Cast<AWowPlayerCharacter>(GetPawn());
	if (!PlayerCharacter || !PlayerCharacter->AnimationController ||
		!CachedMpq || !CachedAssetCache)
	{
		return;
	}

	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	if (LocalGuid == 0)
	{
		return;
	}

	if (!bDebugSpellPreviewStarted)
	{
		bDebugSpellPreviewStarted = true;
		const bool bFirstPerson = FParse::Param(FCommandLine::Get(), TEXT("testspellfirstperson"));
		if (PlayerCharacter->CameraBoom)
		{
			PlayerCharacter->CameraBoom->TargetArmLength = bFirstPerson ? 0.0f : 550.0f;
		}
		SetControlRotation(FRotator(bFirstPerson ? -58.0f : -12.0f,
			PlayerCharacter->GetActorRotation().Yaw, 0.0f));

		if (DebugSpellPreviewPhase == TEXT("impact"))
		{
			// Re-issue the short-lived impact at a low frequency so an automated
			// viewport capture remains deterministic even after terrain warm-up.
			DebugSpellPreviewLastImpactTime = -1.0;
		}
		else if (DebugSpellPreviewPhase == TEXT("channel"))
		{
			OnChannelStart(LocalGuid, static_cast<uint32>(DebugSpellPreviewId), 600000);
		}
		else
		{
			// A long debug-only cast keeps both the stock cast bar and persistent
			// authored precast kit alive through asynchronous world loading.
			OnSpellStart(LocalGuid, static_cast<uint32>(DebugSpellPreviewId), 0, 600000);
		}

		UE_LOG(LogWowGameplay, Log,
			TEXT("[SPELLPREVIEW] Started spell=%d phase=%s firstPerson=%d"),
			DebugSpellPreviewId, *DebugSpellPreviewPhase, bFirstPerson ? 1 : 0);
	}

	if (DebugSpellPreviewPhase == TEXT("impact"))
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (DebugSpellPreviewLastImpactTime < 0.0 || Now - DebugSpellPreviewLastImpactTime >= 1.5)
		{
			DebugSpellPreviewLastImpactTime = Now;
			OnSpellGo(LocalGuid, LocalGuid, static_cast<uint32>(DebugSpellPreviewId), 0);
			SpawnSpellMissile(LocalGuid, LocalGuid, static_cast<uint32>(DebugSpellPreviewId));
		}
	}
}

void AWowGameplayController::InitializeManagers()
{
	if (!ConnectionManager)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Cannot initialize managers: ConnectionManager is null"));
		return;
	}

	CacheWorldResources();

	// TODO: Get root widget for UI overlay
	// In a real implementation, this would get the viewport widget or main UI overlay
	TSharedPtr<SWidget> RootWidget; // Placeholder

	// Initialize death manager
	if (DeathManager)
	{
		DeathManager->Initialize(ConnectionManager, RootWidget);
	}

	// Initialize cursor manager. The manager can load project-local fallback
	// cursors without MPQ data; MPQ is only needed for optional local overrides.
	if (CursorManager)
	{
		CursorManager->Initialize(CachedMpq, CachedAssetCache);
		CursorManager->LoadCursors();
		RefreshSoftwareCursorBrush();
	}

	// Initialize tooltip manager
	if (TooltipManager)
	{
		TooltipManager->Initialize(ConnectionManager, RootWidget);
	}

	// Pass connection manager to UI manager for character stats
	if (UIManager)
	{
		UIManager->SetConnectionManager(ConnectionManager);
		if (AWowAudioManager* AudioManager = Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(GetWorld(), AWowAudioManager::StaticClass())))
		{
			UIManager->SetAudioManager(AudioManager);
		}
	}

	UE_LOG(LogWowGameplay, Log, TEXT("All managers initialized"));
}

void AWowGameplayController::SetUserInterfaceHidden(bool bHideInterface)
{
    if (bUserInterfaceHidden == bHideInterface) return;
    bUserInterfaceHidden = bHideInterface;
    FWowUiVrHost::SetHidden(bHideInterface);
    if (UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
        if (TSharedPtr<IGameLayerManager> GameLayers = ViewportClient->GetGameLayerManager())
        {
            // This is the UI host, not SViewport/the rendered scene. One parent
            // gate covers FrameXML, native panels, cast bars, labels and new
            // widgets created while hidden; each child's own state is retained.
            TSharedRef<SWidget> Host = StaticCastSharedPtr<SGameLayerManager>(GameLayers).ToSharedRef();
            if (bHideInterface) UserInterfacePreviousVisibility = Host->GetVisibility();
            Host->SetVisibility(bHideInterface ? EVisibility::Collapsed : UserInterfacePreviousVisibility);
        }
    // r487 (Stefan: Ctrl+X also hid the blue frost tint and the frozen screen edges): the weather lens
    // is the camera's glass, not interface. While the interface is hidden it moves from the UI host
    // (collapsed above) to the window's overlay, and back when the interface returns.
    if (WeatherLensWidget.IsValid() && GEngine && GEngine->GameViewport)
        if (TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
        {
            const TSharedRef<SWidget> Lens = WeatherLensWidget.ToSharedRef();
            if (bHideInterface)
            {
                FWowUiSurfaces::Remove(Lens);
                Window->AddOverlaySlot()[Lens];
            }
            else
            {
                Window->RemoveOverlaySlot(Lens);
                FWowUiSurfaces::Add(Lens, EWowUiZone::World, -5);
            }
        }
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
}

void AWowGameplayController::RestoreGameplayInputAfterWorldEntry()
{
    // Release stale loading/login keys and use the same hybrid mode as reactivation.
    FWowKeyBindings::ReleaseAll();
    FlushPressedKeys();
    WorldEntryFocusFramesRemaining = 2;
    if (IsWowClientProcessForeground() && !IsNativeModalUiVisible())
    {
        ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
        FocusWowGameViewport();
    }
}

void AWowGameplayController::SetGameplayHudVisible(bool bVisible)
{
    if (!bVisible) WorldEntryFocusFramesRemaining = 0;
    if (!bVisible) SetUserInterfaceHidden(false); // leaving the world must restore login/selection UI
    if (GEngine && GEngine->GameViewport)
    {
        if (bVisible && !WeatherLensWidget.IsValid())
        {
            SAssignNew(WeatherLensWidget, SWowWeatherLens).World(GetWorld())
                .Minimap_Lambda([this](){return StaticCastSharedPtr<SWidget>(MinimapWidget);});
            FWowUiSurfaces::Add(WeatherLensWidget.ToSharedRef(), EWowUiZone::World, -5);
            if(UIManager && UIManager->GetFrameManager()) UIManager->GetFrameManager()->SetMinimapFrostOverlay(WeatherLensWidget->CreateMinimapFrostOverlay());
        }
        else if (!bVisible && WeatherLensWidget.IsValid())
        {
            FWowUiSurfaces::Remove(WeatherLensWidget.ToSharedRef());
            if(UIManager && UIManager->GetFrameManager()) UIManager->GetFrameManager()->SetMinimapFrostOverlay(nullptr);
            WeatherLensWidget.Reset();
        }
    }
    if (UIManager && UIManager->GetFrameManager()) UIManager->GetFrameManager()->SetRootVisible(bVisible);
	bGameplayHudVisible = bVisible;
	const EVisibility Visibility = bVisible ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;

	if (ActionBarWidget.IsValid()) ActionBarWidget->SetVisibility(Visibility);
	if (MinimapWidget.IsValid()) MinimapWidget->SetVisibility(Visibility);
	if (PartyFrameWidget.IsValid()) PartyFrameWidget->SetVisibility(Visibility);
	if (ChatWindow.IsValid()) ChatWindow->SetVisibility(Visibility);
	if (CombatLog.IsValid()) CombatLog->SetVisibility(Visibility);
	if (QuestTrackerContainerWidget.IsValid())
		QuestTrackerContainerWidget->SetVisibility(GWowUseStockWatchFrame(UIManager) ? EVisibility::Collapsed : Visibility);
	if (XPBar.IsValid()) XPBar->SetVisibility(Visibility);
	if (XPText.IsValid()) XPText->SetVisibility(Visibility);
	if (PerformanceOverlayViewportWidget.IsValid())
	{
		PerformanceOverlayViewportWidget->SetVisibility(
			bPerformanceOverlayEnabled ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	}
}

void AWowGameplayController::SetSoftwareCursorSuppressed(bool bSuppressed, bool bShowNativeCursor)
{
	bSoftwareCursorSuppressed = bSuppressed;
	bShowNativeCursorWhileSoftwareSuppressed = bSuppressed && bShowNativeCursor;
	bShowMouseCursor = (!bSoftwareCursorSuppressed || bShowNativeCursorWhileSoftwareSuppressed)
		&& !bRightMouseLooking && !bLeftMouseLooking;
    const bool bNativeFallback = bShowNativeCursorWhileSoftwareSuppressed && bShowMouseCursor;
	DefaultMouseCursor = bNativeFallback ? EMouseCursor::Default : EMouseCursor::None;
	CurrentMouseCursor = DefaultMouseCursor;
	SetNativePlatformCursorVisible(bNativeFallback);
	if (bSoftwareCursorSuppressed && SoftwareCursorImage.IsValid())
	{
		SoftwareCursorImage->SetVisibility(EVisibility::Collapsed);
	}
	if (UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
        const bool bLooking = bRightMouseLooking || bLeftMouseLooking;
        ViewportClient->SetHideCursorDuringCapture(bLooking);
        ViewportClient->SetMouseCaptureMode(WowMouseCapturePolicy::Resolve(bLooking, bSoftwareCursorSuppressed));
        ViewportClient->SetMouseLockMode(bLooking ? EMouseLockMode::LockAlways : EMouseLockMode::DoNotLock);
	}
    UpdateSoftwareCursor(0.0f);
}

void AWowGameplayController::EnsureGameplayHudCreated()
{
	CreateQuestTracker();
	if (IsWowOfflineSession() &&
		UIManager && UIManager->GetFrameManager())
	{
		if (ConnectionManager)
		{
			if (FWowPlayerEntity* LocalPlayer =
				ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer())
			{
				UIManager->GetFrameManager()->UpdateCharacterSheet(
					*LocalPlayer, ConnectionManager->GetCharacterName());
			}
		}
	}
}

void AWowGameplayController::SetupLocalPlayerCharacterModel(const FWowEntity& Entity)
{
	CacheWorldResources();
	if (!CachedMpq || !CachedAssetCache)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Cannot setup local player model - missing world resources. Retrying later..."));

		// Try again in 1 second - the world manager might not be ready yet
		FTimerHandle RetryHandle;
		GetWorldTimerManager().SetTimer(
			RetryHandle,
			[this, Entity]() { SetupLocalPlayerCharacterModel(Entity); },
			1.0f,
			false
		);
		return;
	}

	AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(GetPawn());
	if (!PlayerChar)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Cannot setup local player model - no AWowPlayerCharacter pawn"));
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	// Extract race/gender from entity BYTES_0 or cached character info
	uint8 RaceId = 1;  // Default to Human
	uint8 Gender = 0;  // Default to Male
	uint8 SkinColor = 0;
	uint8 Face = 0;
	uint8 HairStyle = 0;
	uint8 HairColor = 0;
	uint8 FacialHair = 0;

	if (Entity.IsUnit())
	{
		const FWowUnitEntity* UnitEntity = static_cast<const FWowUnitEntity*>(&Entity);
		RaceId = UnitEntity->GetRaceId();
		Gender = UnitEntity->GetGenderId();
		UE_LOG(LogWowGameplay, Log, TEXT("Local player race/gender from entity: Race=%d Gender=%d"), RaceId, Gender);
	}

	// Always look up cached character info (for equipment data + customization fallback)
	const FWowCharacterInfo* CachedCharInfo = nullptr;
	if (ConnectionManager)
	{
		const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
		const TArray<FWowCharacterInfo>& CachedChars = ConnectionManager->GetCachedCharacters();

		for (const FWowCharacterInfo& CharInfo : CachedChars)
		{
			if (static_cast<uint64>(CharInfo.Guid) == LocalGuid)
			{
				CachedCharInfo = &CharInfo;
				if (RaceId == 0)
				{
					RaceId = CharInfo.Race;
					Gender = CharInfo.Gender;
				}
				UE_LOG(LogWowGameplay, Log, TEXT("Found cached character: Race=%d Gender=%d Equipment=%d slots"),
					CharInfo.Race, CharInfo.Gender, CharInfo.Equipment.Num());
				break;
			}
		}
	}

	// Fallback to default values if still not found
	if (RaceId == 0)
	{
		RaceId = 1; // Human
		Gender = 0; // Male
		UE_LOG(LogWowGameplay, Warning, TEXT("Using fallback race/gender for local player: Human Male"));
	}

	// TODO: Extract customization data from player entity fields (PLAYER_BYTES, etc.)
	// For now, use default customization values


	// Use equipment-aware character model if we have equipment data
	if (CachedCharInfo && CachedCharInfo->Equipment.Num() > 0)
	{
		PlayerChar->SetCharacterModelWithEquipment(World, CachedMpq, CachedAssetCache,
			RaceId, Gender, SkinColor, Face, HairStyle, HairColor, FacialHair, &CachedCharInfo->Equipment);
	}
	else
	{
		// Fallback to simple character model without equipment
		PlayerChar->SetCharacterModel(World, CachedMpq, CachedAssetCache,
			RaceId, Gender, SkinColor, Face, HairStyle, HairColor, FacialHair);
	}

	ApplyLocalWeaponPresentation();
	EnsureEntityNameplate(Entity, PlayerChar);
}

void AWowGameplayController::RebuildLocalAppearanceFromFields(int32 Attempt)
{
	// r258: the local model from the current VISIBLE_ITEM fields and PLAYER_BYTES. Custom server
	// items (not in Item.dbc) come from item query responses; wait for them (up to ~3 s).
	if (!ConnectionManager) return;
	CacheWorldResources();
	AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(GetPawn());
	auto& Handler = ConnectionManager->PacketHandler;
	const FWowEntity* Me = Handler.EntityManager.Find(Handler.EntityManager.LocalPlayerGuid);
	if (!PlayerChar || !Me || !Me->IsPlayer() || !CachedMpq || !CachedAssetCache || !GetWorld()) return;
	const FWowPlayerEntity& LocalPlayerEntity = static_cast<const FWowPlayerEntity&>(*Me);
	TArray<FWowCharacterEquipment> Equipment;
	bool bWaiting = false;
	const uint32 HiddenGear = LocalPlayerEntity.GetPlayerFlags(); // r224: Show Helm / Show Cloak off
	for (uint8 Slot = 0; Slot < 19; ++Slot)
	{
		if ((Slot == PlayerField::EQUIPMENT_SLOT_HEAD && (HiddenGear & 0x400u)) ||
			(Slot == PlayerField::EQUIPMENT_SLOT_BACK && (HiddenGear & 0x800u))) continue;
		const uint32 Entry = LocalPlayerEntity.GetField(PlayerField::VISIBLE_ITEM_BASE + Slot * 2);
		if (!Entry) continue;
		uint32 DisplayId = 0, InventoryType = 0;
		if (const FWowItemTemplateInfo* Template = Handler.ItemTemplates.Find(Entry))
		{ DisplayId = Template->DisplayId; InventoryType = Template->InventoryType; }
		else if (const FItemDbcEntry* Item = FDbcStore::Get().Items().GetById(Entry))
		{ DisplayId = Item->DisplayInfoID; InventoryType = uint32(Item->InventoryType); }
		else { Handler.RequestItemTemplate(Entry); bWaiting = true; continue; }
		if (!DisplayId) continue;
		if (Slot == PlayerField::EQUIPMENT_SLOT_OFFHAND && (InventoryType == 13 || InventoryType == 21)) InventoryType = 22;
		FWowCharacterEquipment& Item = Equipment.AddDefaulted_GetRef();
		Item.DisplayId = int32(DisplayId);
		Item.InventoryType = uint8(InventoryType);
	}
	if (bWaiting && Attempt < 3)
	{
		FTimerHandle Retry;
		GetWorldTimerManager().SetTimer(Retry, FTimerDelegate::CreateWeakLambda(this, [this, Attempt]()
		{
			RebuildLocalAppearanceFromFields(Attempt + 1);
		}), 1.0f, false);
		return;
	}
	UE_LOG(LogWowGameplay, Log, TEXT("Local appearance rebuilt from visible items: %d pieces"), Equipment.Num());
	// r354: in the barber chair, the look chosen in the barber window.
	uint8 LookSkin = LocalPlayerEntity.GetFieldByte(PlayerField::BYTES, 0);
	uint8 LookHair = LocalPlayerEntity.GetFieldByte(PlayerField::BYTES, 2);
	uint8 LookColor = LocalPlayerEntity.GetFieldByte(PlayerField::BYTES, 3);
	uint8 LookFacial = LocalPlayerEntity.GetFieldByte(PlayerField::BYTES_2, 0);
	FWowBarberShop::GetPreview(LookSkin, LookHair, LookColor, LookFacial);
	PlayerChar->SetCharacterModelWithEquipment(GetWorld(), CachedMpq, CachedAssetCache,
		LocalPlayerEntity.GetRaceId(), LocalPlayerEntity.GetGenderId(),
		LookSkin, LocalPlayerEntity.GetFieldByte(PlayerField::BYTES, 1),
		LookHair, LookColor, LookFacial, &Equipment);
	ApplyLocalWeaponPresentation();
	EnsureEntityNameplate(*Me, PlayerChar);
}

void AWowGameplayController::SetupLocalPlayerCharacterModelFromCharacterInfo(const FWowCharacterInfo& CharacterInfo)
{
	CacheWorldResources();
	if (!CachedMpq || !CachedAssetCache)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Cannot setup local player model from character list - missing world resources"));
		return;
	}

	AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(GetPawn());
	if (!PlayerChar)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Cannot setup local player model from character list - no AWowPlayerCharacter pawn"));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// r696 (Stefan, a new blood elf priest: the world model wore a black and orange robe while the paper doll
	// showed the purple one; "took it off and put it back on, now she's" right): the login controller applies
	// the character-screen look when the destination terrain is resident, 15 s after world entry, and that
	// replaced the model already built from the live VISIBLE_ITEM fields with the character list's gear. Once
	// our own player object exists, its fields are the truth.
	if (ConnectionManager)
	{
		auto& Handler = ConnectionManager->PacketHandler;
		const uint64 LiveGuid = CharacterInfo.Guid != 0 ? static_cast<uint64>(CharacterInfo.Guid) : Handler.EntityManager.LocalPlayerGuid;
		const FWowEntity* Live = LiveGuid != 0 ? Handler.EntityManager.Find(LiveGuid) : nullptr;
		if (Live && Live->IsPlayer())
		{
			if (CharacterInfo.Guid != 0 && !CharacterInfo.Name.IsEmpty())
			{
				Handler.EntityManager.LocalPlayerGuid = LiveGuid;
				Handler.PlayerNameCache.Add(LiveGuid, CharacterInfo.Name);
			}
			UE_LOG(LogWowGameplay, Log, TEXT("Selected character appearance for %s: the live player object exists, rebuilding from its visible items"),
				*CharacterInfo.Name);
			RebuildLocalAppearanceFromFields(0);
			return;
		}
	}

	const TArray<FWowCharacterEquipment>* Equipment =
		CharacterInfo.Equipment.Num() > 0 ? &CharacterInfo.Equipment : nullptr;

	PlayerChar->SetCharacterModelWithEquipment(
		World,
		CachedMpq,
		CachedAssetCache,
		CharacterInfo.Race > 0 ? CharacterInfo.Race : 1,
		CharacterInfo.Gender,
		CharacterInfo.Skin,
		CharacterInfo.Face,
		CharacterInfo.HairStyle,
		CharacterInfo.HairColor,
		CharacterInfo.FacialHair,
		Equipment);
	ApplyLocalWeaponPresentation();

	if (ConnectionManager && CharacterInfo.Guid != 0 && !CharacterInfo.Name.IsEmpty())
	{
		const uint64 Guid = static_cast<uint64>(CharacterInfo.Guid);
		ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid = Guid;
		ConnectionManager->PacketHandler.PlayerNameCache.Add(Guid, CharacterInfo.Name);
	}

	UE_LOG(LogWowGameplay, Log, TEXT("Applied selected character appearance: %s Race=%d Gender=%d Equipment=%d"),
		*CharacterInfo.Name, CharacterInfo.Race, CharacterInfo.Gender, CharacterInfo.Equipment.Num());
}

bool AWowGameplayController::TrySetupLocalPlayerCharacterModelFromCachedCharacters()
{
	if (!ConnectionManager)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Cannot setup local player model from cached characters - no connection manager"));
		return false;
	}

	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	if (LocalGuid == 0 && ConnectionManager->GetSelectedCharacterGuid() != 0)
	{
		ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid =
			static_cast<uint64>(ConnectionManager->GetSelectedCharacterGuid());
	}

	FWowCharacterInfo SelectedCharacter;
	if (ConnectionManager->GetSelectedCharacterInfo(SelectedCharacter))
	{
		SetupLocalPlayerCharacterModelFromCharacterInfo(SelectedCharacter);
		return true;
	}

	const uint64 EffectiveLocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	if (EffectiveLocalGuid == 0)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Cannot setup local player model from cached characters - local player GUID is zero"));
		return false;
	}

	const TArray<FWowCharacterInfo> CachedCharacters = ConnectionManager->GetCachedCharacters();
	for (const FWowCharacterInfo& CharacterInfo : CachedCharacters)
	{
		if (static_cast<uint64>(CharacterInfo.Guid) == EffectiveLocalGuid)
		{
			SetupLocalPlayerCharacterModelFromCharacterInfo(CharacterInfo);
			return true;
		}
	}

	UE_LOG(LogWowGameplay, Warning, TEXT("No cached character info found for local player GUID %llu (%d cached characters)"),
		EffectiveLocalGuid, CachedCharacters.Num());
	return false;
}

void AWowGameplayController::EnsureVisibleLocalPlayerCharacterModel()
{
	if (TrySetupLocalPlayerCharacterModelFromCachedCharacters())
	{
		return;
	}

	CacheWorldResources();
	AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(GetPawn());
	if (!PlayerChar)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Cannot ensure local player visual - no AWowPlayerCharacter pawn"));
		return;
	}

	if (CachedMpq && CachedAssetCache)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("No selected/cached character info available; applying visible default Human Male model"));
		PlayerChar->SetCharacterModel(GetWorld(), CachedMpq, CachedAssetCache, 1, 0, 0, 0, 0, 0, 0);
		return;
	}

	UE_LOG(LogWowGameplay, Warning, TEXT("No character MPQ resources available; applying native stick-figure fallback"));
	PlayerChar->EnableNativeFallbackVisuals();
}

void AWowGameplayController::UpdatePlayerAnimations()
{
	// A corpse stays down until released or resurrected (UWowDeathManager plays the death clip).
	if (DeathManager && DeathManager->IsLocalCorpse()) return;
	// Update local player animation based on character movement and stand state
	if (AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(GetPawn()))
	{
		// If the player has an animation controller, update it
		if (UWowAnimationController* AnimController = PlayerChar->AnimationController)
		{
			UWowPlayerTorchComponent::Ensure(PlayerChar, CachedMpq, CachedAssetCache); // r398 belt torch (saved on)
			// r397 (Stefan: the head did not turn to the target): this block had ended up inside the
			// swimming branch, so the head look only ran while swimming. Now every tick, casting too.
			// r292 head look (Stefan): towards the target's head, else where the camera looks.
			// r296: also in first person and VR (head bone only, so the eye stays put); in VR
			// the camera direction is the headset, so your shadow and reflection follow it.
			if (USkeletalMeshComponent* Body = PlayerChar->GetMesh())
				if (UWowFootIKInstance* Look = Cast<UWowFootIKInstance>(Body->GetAnimInstance()))
				{
					FVector At = FVector::ZeroVector;
					const bool bWant = FWowClientPreferences::GetWorldOption(TEXT("headlook")) && PlayerCameraManager;
					const bool bHeadOnly = FWowVRSupport::IsActive() || (PlayerCameraManager
						&& FVector::Dist(PlayerCameraManager->GetCameraLocation(), PlayerChar->GetActorLocation()) <= 120.0);
					// VR / first person: where you look, not the target (the view is yours).
					const bool bUseTarget = !bHeadOnly;
					if (bWant && bUseTarget && TargetGuid != 0)
						if (const TObjectPtr<AActor>* Found = SpawnedEntityActors.Find(TargetGuid); Found && *Found && *Found != PlayerChar)
						{
							TInlineComponentArray<USkeletalMeshComponent*> Meshes(*Found);
							for (const USkeletalMeshComponent* M : Meshes)
								if (M && M->DoesSocketExist(TEXT("WowFirstPersonHead"))) { At = M->GetSocketLocation(TEXT("WowFirstPersonHead")); break; }
							if (At.IsZero()) At = (*Found)->GetActorLocation() + FVector(0.0, 0.0, 120.0);
						}
					const bool bLookFromCamera = bWant && At.IsZero();
					if (bLookFromCamera)
						At = PlayerCameraManager->GetCameraLocation() + PlayerCameraManager->GetCameraRotation().Vector() * 3000.0;
					// r681 (Stefan, running with the camera close behind and above: "I'm able to watch my back,
					// unnaturally"): the head followed the camera up to 85 degrees sideways and 55 down, so a camera
					// looking down at your own back made the running character crane round and stare at the ground.
					// Following the camera, the head now dips at most 20 degrees (15 up); while moving it turns at
					// most 40 degrees off the way you face (50 for a target, 30 down). Standing still keeps the full
					// turn sideways. Looking behind the shoulder still means looking ahead (the controller decides).
					// r690 (Stefan, camera zoomed in close behind his head: "still able to look behind when running and
					// strafing"): within 1.2 m the head-only look skipped these limits. Now only VR (the headset is your
					// head) is free; the close camera is limited like any other.
					const bool bVrLook = FWowVRSupport::IsActive();
					if (bWant && !bVrLook)
					{
						FVector Head = Body->DoesSocketExist(TEXT("WowFirstPersonHead"))
							? Body->GetSocketLocation(TEXT("WowFirstPersonHead")) : PlayerChar->GetActorLocation() + FVector(0.0, 0.0, 100.0);
						const FVector Dir = At - Head;
						const FVector Forward = PlayerChar->GetActorForwardVector().GetSafeNormal2D();
						const FVector Flat = Dir.GetSafeNormal2D();
						if (!Forward.IsNearlyZero() && !Flat.IsNearlyZero() && Dir.Size() > 60.0)
						{
							const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Forward.X * Flat.Y - Forward.Y * Flat.X, FVector::DotProduct(Forward, Flat)));
							const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(Dir.Z, Dir.Size2D()));
							if (FMath::Abs(Yaw) <= 125.0)
							{
								const bool bMoving = PlayerChar->GetVelocity().Size2D() > 50.0;
								const double MaxYaw = bMoving ? (bLookFromCamera ? 40.0 : 50.0) : 85.0;
								const double MinPitch = bLookFromCamera ? (bHeadOnly && !bMoving ? -45.0 : -20.0) : (bMoving ? -30.0 : -55.0);
								const double MaxPitch = bLookFromCamera ? 15.0 : 40.0;
								const double NewYaw = FMath::Clamp(Yaw, -MaxYaw, MaxYaw), NewPitch = FMath::Clamp(Pitch, MinPitch, MaxPitch);
								if (NewYaw != Yaw || NewPitch != Pitch)
								{
									const FVector Side = FQuat(FVector::UpVector, FMath::DegreesToRadians(NewYaw)).RotateVector(Forward);
									const double PitchRad = FMath::DegreesToRadians(NewPitch);
									At = Head + (Side * FMath::Cos(PitchRad) + FVector::UpVector * FMath::Sin(PitchRad)) * Dir.Size();
								}
							}
						}
					}
					// r681 (Stefan: "strafing doesn't lock head"): while strafing the body turns toward the side it
					// runs to, and the head kept looking back toward the camera - it stays with the body now.
					bool bStrafing = false;
					{
						const FVector Velocity = PlayerChar->GetVelocity().GetSafeNormal2D();
						if (PlayerChar->GetVelocity().Size2D() > 50.0 && !Velocity.IsNearlyZero())
							bStrafing = FVector::DotProduct(Velocity, PlayerChar->GetActorForwardVector().GetSafeNormal2D()) < 0.94; // > ~20 degrees off
					}
					Look->bLookEnabled = bWant && !(bStrafing && !bVrLook); // r690: the close camera too
					Look->bLookHeadOnly = bHeadOnly;
					Look->LookAtWorld = At;
				}
			// r432 (Stefan: a taxi flight showed no gryphon): the local player's mount - taxi gryphon or
			// mount spell - stands under the player and the rider sits on it (FWowLocalMount).
			{
				const FWowPlayerEntity* Me = ConnectionManager ? ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
				if (FWowLocalMount::Update(PlayerChar, AnimController, Me ? Me->GetMountDisplayId() : 0, CachedMpq, CachedAssetCache,
						GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f))
					return;
			}
			// Don't override casting animations with movement animations
			// The casting animation will be explicitly set/unset in spell handlers
			if (bIsCasting)
			{
				AnimController->TickPendingSpellCast(); // r372: queued cast after the release clip
				// r372 (Stefan: the cast went on while she slid along): moving or jumping cancels a
				// cast or channel at once, like the original client (the server also interrupts it
				// when its movement packet arrives, but the bar, pose and slide went on until then).
				const UCharacterMovementComponent* CastMove = PlayerChar->GetCharacterMovement();
				const bool bMoveIntent = FWowKeyBindings::Held(TEXT("MOVEFORWARD")) || FWowKeyBindings::Held(TEXT("MOVEBACKWARD"))
					|| FWowKeyBindings::Held(TEXT("STRAFELEFT")) || FWowKeyBindings::Held(TEXT("STRAFERIGHT"))
					|| bMouseChordMovingForward || (IsInputKeyDown(EKeys::LeftMouseButton) && bRightMouseLooking)
					|| PlayerChar->bIsAutoRunning || !PlayerChar->GetCurrentMoveInput2D().IsNearlyZero()
					|| WowIsoClickMove::GActive // r621 isometric click-to-move
					|| (CastMove && CastMove->IsFalling() && CastMove->Velocity.Z > 50.0);
				static double GLastMoveCancel = 0.0;
				const double NowCancel = FPlatformTime::Seconds();
				if (bMoveIntent && CurrentSpellId != 0 && ConnectionManager && NowCancel - GLastMoveCancel > 0.5
					&& GetWorld() && GetWorld()->GetTimeSeconds() - CastStartTime > 0.05)
				{
					GLastMoveCancel = NowCancel;
					const uint32 Cancelled = CurrentSpellId;
					UE_LOG(LogWowGameplay, Log, TEXT("Movement cancels local %s %u"), bIsChanneling ? TEXT("channel") : TEXT("cast"), Cancelled);
					ConnectionManager->SendCancelCast(Cancelled, bIsChanneling);
					OnSpellFailure(ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid, Cancelled, 40);
				}
			}
			if (!bIsCasting)
			{
				auto IsGameplayKeyDown = [this](const FKey& Key)
				{
					bool bPhysicalDown = false;
					const bool bHasPhysicalState = TryGetPhysicalInputDown(Key, bPhysicalDown);
					return (bHasPhysicalState && bPhysicalDown) ||
						IsInputKeyDown(Key) ||
						GetInputKeyTimeDown(Key) > 0.0f;
				};

				uint32 LocalMoveFlags = 0;
				const FVector2D MoveInput = PlayerChar->GetCurrentMoveInput2D();
				// r610: in the isometric camera every key walks forward along its screen direction
				// (the character turns to it), so only the move input decides the animation.
				const bool bIsoKeys = FWowIsometricCamera::IsActive();
				const bool bKeyboardForward = !bIsoKeys && (
					FWowKeyBindings::Held(TEXT("MOVEFORWARD")) ||
					bMouseChordMovingForward ||
					(IsInputKeyDown(EKeys::LeftMouseButton) && bRightMouseLooking));
				const bool bKeyboardBackward = !bIsoKeys && FWowKeyBindings::Held(TEXT("MOVEBACKWARD"));
				const bool bKeyboardStrafeLeft = !bIsoKeys && FWowKeyBindings::Held(TEXT("STRAFELEFT"));
				const bool bKeyboardStrafeRight = !bIsoKeys && FWowKeyBindings::Held(TEXT("STRAFERIGHT"));
				const bool bKeyboardDirectionalInput =
					bKeyboardForward || bKeyboardBackward || bKeyboardStrafeLeft || bKeyboardStrafeRight;
				// Auto-run counts as moving forward for the animation (r241, Stefan): auto-run plus
				// a strafe key ran diagonally but played the pure sideways strafe clip.
				const bool bAutoRunForward = PlayerChar->bIsAutoRunning && !IsMovementHeldBySitting();
				const bool bForward = MoveInput.Y > 0.1f || bKeyboardForward || bAutoRunForward
					|| (WowIsoClickMove::GActive && WowIsoClickMove::GArrivedAt <= 0.0); // r621 isometric click-to-move walks forward
				const bool bBackward = MoveInput.Y < -0.1f || bKeyboardBackward;
				const bool bStrafeLeft = bKeyboardStrafeLeft || (!bKeyboardDirectionalInput && MoveInput.X < -0.1f);
				const bool bStrafeRight = bKeyboardStrafeRight || (!bKeyboardDirectionalInput && MoveInput.X > 0.1f);
				const bool bHasExplicitDirectionalInput =
					bForward || bBackward || bStrafeLeft || bStrafeRight;

                const auto* SitMovement=PlayerChar->GetCharacterMovement();
                const bool CancelSit=bHasExplicitDirectionalInput || (SitMovement && (SitMovement->IsFalling() || SitMovement->Velocity.Size2D()>10.f));
                if(bLocalSitting && CancelSit) SetLocalSitting(false, true);
                if(bLocalStandingUp)
                {
                    // Keep the stand-up clip playing; movement is held until it ends.
                    if(AnimController->IsPlayingOneShot() && FPlatformTime::Seconds() < LocalStandUpLockUntil) return;
                    bLocalStandingUp=false; bLocalSitTransition=false;
                }
                if(bLocalSitTransition)
                {
                    if(!CancelSit && AnimController->GetCurrentState()!=EWowAnimState::Dead && AnimController->IsPlayingOneShot()) return;
                    bLocalSitTransition=false;
                }
                if (ConnectionManager)
                {
                    const auto* LocalEntity = ConnectionManager->PacketHandler.EntityManager.Find(
                        ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid);
                    if (LocalEntity)
                    {
                        const uint32 EmoteState = LocalEntity->GetField(UnitField::NPC_EMOTESTATE);
                        AnimController->SyncEmoteState(EmoteState, GetAnimationForEmoteId(EmoteState));
                        const uint8 Posture = LocalEntity->GetFieldByte(UnitField::BYTES_1, 0);
                        if (!CancelSit && AnimController->GetCurrentState()!=EWowAnimState::Dead && Posture != WowStandState::STAND && Posture != WowStandState::DEAD)
                        {
                            AnimController->PlayAnimationById(GetAnimationIdForStandState(Posture), true);
                            return;
                        }
                    }
                }
				if (bLocalSitting)
				{
					const UCharacterMovementComponent* Movement = PlayerChar->GetCharacterMovement();
					const bool bShouldStand = bHasExplicitDirectionalInput ||
						(Movement && (Movement->IsFalling() || Movement->Velocity.Size2D() > 10.0f));
					if (bShouldStand)
					{
						SetLocalSitting(false, true);
					}
					else
					{
						AnimController->PlayAnimationById(EWowAnimId::SitGround, true);
						return;
					}
				}

				const bool bAnyStrafeLeft = bStrafeLeft && !bStrafeRight && !bBackward;
				const bool bAnyStrafeRight = bStrafeRight && !bStrafeLeft && !bBackward;
				if ((bAnyStrafeLeft || bAnyStrafeRight) && !PlayerChar->bIsWalking)
				{
					if (UCharacterMovementComponent* Movement = PlayerChar->GetCharacterMovement())
					{
						if (!Movement->IsFalling() && !PlayerChar->IsInWowWaterSwimmingState())
						{
							const float StrafeSpeed = FMath::Max(Movement->Velocity.Size2D(), PlayerChar->RunSpeed);
							const float VisualStrafeYaw = bForward ? 45.0f : 90.0f;
							AnimController->ForceLocalLocomotionState(
								bAnyStrafeLeft ? EWowAnimState::StrafingLeft : EWowAnimState::StrafingRight,
								StrafeSpeed,
								VisualStrafeYaw);
							return;
						}
					}
				}

				if (bForward && !bBackward)
				{
					LocalMoveFlags |= WowMovementFlags::FORWARD;
				}
				else if (bBackward && !bForward)
				{
					LocalMoveFlags |= WowMovementFlags::BACKWARD;
				}

				if (bStrafeLeft && !bStrafeRight)
				{
					LocalMoveFlags |= WowMovementFlags::STRAFE_LEFT;
				}
				else if (bStrafeRight && !bStrafeLeft)
				{
					LocalMoveFlags |= WowMovementFlags::STRAFE_RIGHT;
				}

				if (UCharacterMovementComponent* Movement = PlayerChar->GetCharacterMovement())
				{
					const FVector Velocity = Movement->Velocity.GetSafeNormal2D();
					if (!bHasExplicitDirectionalInput && !Velocity.IsNearlyZero())
					{
						const FVector Forward = PlayerChar->GetActorForwardVector().GetSafeNormal2D();
						const FVector Right = PlayerChar->GetActorRightVector().GetSafeNormal2D();
						const float ForwardDot = FVector::DotProduct(Forward, Velocity);
						const float RightDot = FVector::DotProduct(Right, Velocity);
						uint32 VelocityMoveFlags = 0;

						if (FMath::Abs(RightDot) > 0.65f && FMath::Abs(RightDot) > FMath::Abs(ForwardDot) + 0.10f)
						{
							VelocityMoveFlags = RightDot > 0.0f
								? WowMovementFlags::STRAFE_RIGHT
								: WowMovementFlags::STRAFE_LEFT;
						}
						else if (ForwardDot < -0.35f)
						{
							VelocityMoveFlags = WowMovementFlags::BACKWARD;
						}
						else if (ForwardDot > 0.35f)
						{
							VelocityMoveFlags = WowMovementFlags::FORWARD;
						}

						if (VelocityMoveFlags != 0)
						{
							LocalMoveFlags &= ~(WowMovementFlags::FORWARD |
								WowMovementFlags::BACKWARD |
								WowMovementFlags::STRAFE_LEFT |
								WowMovementFlags::STRAFE_RIGHT);
							LocalMoveFlags |= VelocityMoveFlags;
						}
					}

					if (PlayerChar->IsInWowWaterSwimmingState())
					{
						const uint32 DirectionalFlags = WowMovementFlags::FORWARD |
							WowMovementFlags::BACKWARD |
							WowMovementFlags::STRAFE_LEFT |
							WowMovementFlags::STRAFE_RIGHT;
						const bool bActuallyMovingInWater =
							Movement->Velocity.SizeSquared() > FMath::Square(10.0f);

						// The lightweight water mode uses MOVE_Flying, so movement-mode
						// state alone cannot select WoW's SwimIdle clip. Resolve it from
						// full 3D velocity: clear stale input flags at rest, and supply a
						// locomotion flag for pure vertical swimming which Size2D misses.
						if (!bActuallyMovingInWater)
						{
							LocalMoveFlags &= ~DirectionalFlags;
						}
						else if ((LocalMoveFlags & DirectionalFlags) == 0)
						{
							LocalMoveFlags |= WowMovementFlags::FORWARD;
						}

						LocalMoveFlags |= WowMovementFlags::SWIMMING;
					}
					if (Movement->IsFalling())
					{
						LocalMoveFlags |= Movement->Velocity.Z > 0.0f
							? WowMovementFlags::JUMPING
							: WowMovementFlags::FALLING;
					}
					if (PlayerChar->bIsWalking && LocalMoveFlags != 0)
					{
						LocalMoveFlags |= WowMovementFlags::WALK_MODE;
					}
				}

				if (LocalMoveFlags != 0)
				{
					FWowMovementInfo LocalAnimMovement;
					LocalAnimMovement.MoveFlags = LocalMoveFlags;
					LocalAnimMovement.RunSpeed = PlayerChar->RunSpeed / 100.0f;
					LocalAnimMovement.WalkSpeed = (PlayerChar->RunSpeed * PlayerChar->WalkSpeedFactor) / 100.0f;
					AnimController->UpdateAnimationState(LocalAnimMovement, bWasInCombat || bIsAutoAttacking, false);
				}
				else
				{
					AnimController->UpdateLocalPlayerState(PlayerChar, bWasInCombat || bIsAutoAttacking);
				}
			}
		}
	}
}

void AWowGameplayController::UpdateEntityAnimations()
{
    FWowPerformanceScope Timing(TEXT("UpdateEntityAnimations"), TEXT("world"));
	if (!ConnectionManager) return;

	// Update animations for all spawned entity actors based on their movement state
	// Note: Local player animation is handled by UpdatePlayerAnimations()
	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;

	for (auto& Pair : SpawnedEntityActors)
	{
		uint64 Guid = Pair.Key;
		AActor* Actor = Pair.Value;

		if (!Actor || !IsValid(Actor)) continue;

		// Skip local player â€” handled by UpdatePlayerAnimations()
		if (Guid == LocalGuid) continue;

		// Skip dead entities (corpses) â€” keep their death animation
		if (DeadEntityGuids.Contains(Guid)) continue;

		// Get the animation controller for this actor
		UWowAnimationController* AnimController = FWowCharacterBuilder::GetAnimationController(Actor);
		if (!AnimController) continue;

		// Get the entity data to determine animation state
		const FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Guid);
		if (!Entity) continue;

        const uint32 EmoteState = Entity->GetField(UnitField::NPC_EMOTESTATE);
        AnimController->SyncEmoteState(EmoteState, GetAnimationForEmoteId(EmoteState));

		// Alive units the server presents as corpses: feign death (UNIT_FLAG2_FEIGN_DEATH),
		// UNIT_DYNFLAG_DEAD or EMOTE_STATE_DEAD — e.g. quest NPCs like Henze Faulk that lie
		// "dead" until a player uses an item on them. Pose only: they are not real deaths
		// (no loot, no death handling) and stand up again when the server clears the flag.
		{
			constexpr uint32 UNIT_FLAG2_FEIGN_DEATH = 0x00000001;
			constexpr uint32 UNIT_DYNFLAG_DEAD = 0x00000020;
			constexpr uint32 EMOTE_STATE_DEAD = 65;
			const bool bShownAsCorpse = Entity->IsUnit() && !DeadEntityGuids.Contains(Guid) &&
				((Entity->GetField(UnitField::FLAGS_2) & UNIT_FLAG2_FEIGN_DEATH) != 0 ||
				 (Entity->GetField(UnitField::DYNAMIC_FLAGS) & UNIT_DYNFLAG_DEAD) != 0 ||
				 EmoteState == EMOTE_STATE_DEAD);
			if (bShownAsCorpse)
			{
				if (!FeignDeadEntityGuids.Contains(Guid) || AnimController->GetCurrentState() != EWowAnimState::Dead)
				{
					FeignDeadEntityGuids.Add(Guid);
					AnimController->ShowCorpsePose();
				}
				continue;
			}
			if (FeignDeadEntityGuids.Remove(Guid) > 0 && !DeadEntityGuids.Contains(Guid))
			{
				AnimController->SetDead(false);
			}
		}

		// Check server-authoritative stand state from UNIT_FIELD_BYTES_1
		uint8 StandState = Entity->GetFieldByte(UnitField::BYTES_1, 0);
		if (StandState == WowStandState::DEAD)
		{
			// Server says this entity is dead â€” trigger death handling
			if (!DeadEntityGuids.Contains(Guid))
			{
				OnEntityDeath(Guid);
			}
			continue;
		}

		// If already marked dead by OnEntityHealthChanged, skip
		// Note: Do NOT check GetHealth()==0 here â€” fields may not be populated yet
		// for newly spawned entities. Death is detected in OnEntityHealthChanged
		// when health transitions from >0 to 0, or by StandState==DEAD above.
		if (Entity->GetHealth() <= 0 && Entity->GetMaxHealth() > 0 && DeadEntityGuids.Contains(Guid))
		{
			continue;
		}

		// Determine movement state for animation. Creature UPDATE_OBJECT flags can
		// be stale, so their locomotion is derived from rendered displacement.
        // Animation consumes flags and gait speeds, never the waypoint array.
        // Copying full movement here allocates every frame for every pathing unit.
        FWowMovementInfo AnimMovement;
        AnimMovement.MoveFlags = Entity->Movement.MoveFlags;
        AnimMovement.WalkSpeed = Entity->Movement.WalkSpeed;
        AnimMovement.RunSpeed = Entity->Movement.RunSpeed;
        AnimMovement.bHasActiveSpline = Entity->Movement.bHasActiveSpline;
        AnimMovement.SplineSpeed = Entity->Movement.SplineSpeed;
		bool bIsVisuallyMoving = VisuallyMovingEntityGuids.Contains(Guid);
		// r298 (Stefan: the Stormwind orphans ran, stopped, ran, stopped): an NPC walking a chain of
		// short splines (waypoints, formation followers) stood still for the network gap between one
		// spline ending and the next arriving, which flipped Run -> Stand -> Run every few steps.
		// Keep the gait for a short grace after the last rendered step; a real pause is longer.
		bool bInStopGrace = false;
		{
			static TMap<uint64, double> GLastVisualStep;
			// r372 (Stefan: every mob that stops does a slow-motion walk first): the 1 s grace kept the
			// gait on for every stop, with ~0 rendered speed, so the walk played in slow motion. It is
			// now only for units seen resuming within a second (chain movers like the orphans); any
			// other unit, and anything in combat, gets 0.25 s and keeps its last stride rate meanwhile.
			static TMap<uint64, int32> GChainResumes;
			const double Now = FPlatformTime::Seconds();
			if (bIsVisuallyMoving)
			{
				if (const double* Last = GLastVisualStep.Find(Guid))
				{
					const double Gap = Now - *Last;
					if (Gap > 0.12 && Gap < 1.0) GChainResumes.FindOrAdd(Guid) = FMath::Min(GChainResumes.FindRef(Guid) + 1, 4);
					else if (Gap >= 2.0) GChainResumes.Remove(Guid);
				}
				GLastVisualStep.Add(Guid, Now);
			}
			else if (!Entity->IsPlayer())
				if (const double* Last = GLastVisualStep.Find(Guid))
				{
					const bool bCombat = (Entity->GetField(UnitField::FLAGS) & 0x00080000) != 0;
					// r453 (Stefan: "the strange stops NPCs and mobs do when they stop"): a unit whose path has
					// ended (no active spline) and that is no chain mover stops at once; for 0.25 s it still
					// ran its legs in place. The short grace stays only while a spline is still running.
					// r521 (Stefan: Frostmane whelps "walking in place when they stop"): a wandering mob that
					// once resumed within a second counted as a chain mover and kept walking in place for a
					// full second at every real stop. The network gap between chained splines is well under
					// 0.4 s, so that is enough grace.
					const double Grace = (!bCombat && GChainResumes.FindRef(Guid) > 0) ? 0.4
						: (Entity->Movement.bHasActiveSpline ? 0.25 : 0.0);
					if (Now - *Last < Grace) { bIsVisuallyMoving = true; bInStopGrace = true; }
					else if (Now - *Last > 3.0) GLastVisualStep.Remove(Guid);
				}
		}
		const uint32 DirectionalFlags = WowMovementFlags::FORWARD |
			WowMovementFlags::BACKWARD |
			WowMovementFlags::STRAFE_LEFT |
			WowMovementFlags::STRAFE_RIGHT;

		// 3.3.5 clients flag the whole jump arc with FALLING (0x1000). Split it
		// into the animation controller's rising (JUMPING) and descending phases
		// from the reported launch speed and elapsed fall time (gravity 19.29 y/s^2).
		if (Entity->IsPlayer() && (AnimMovement.MoveFlags & WowMovementFlags::FALLING))
		{
			const double Elapsed = Entity->Movement.FallTime / 1000.0
				+ FMath::Max(0.0, FPlatformTime::Seconds() - Entity->Movement.MovementReceivedAtSeconds);
			const double UpwardSpeed = -Entity->Movement.JumpZSpeed - 19.29 * Elapsed;
			if (UpwardSpeed > 0.0)
			{
				AnimMovement.MoveFlags &= ~WowMovementFlags::FALLING;
				AnimMovement.MoveFlags |= WowMovementFlags::JUMPING;
			}
		}

		// Derive motion from displacement, but preserve the server locomotion mode.
		if (!Entity->IsPlayer())
		{
            AnimMovement.MoveFlags &= WowMovementFlags::WALK_MODE | WowMovementFlags::SWIMMING | WowMovementFlags::FLYING;
            // A WotLK spline may choose walking speed without setting WALK_MODE.
            // Infer its gait from the server path/duration, never from smoothed actor jitter.
            if (AnimMovement.bHasActiveSpline && AnimMovement.SplineSpeed > 0.0f
                && AnimMovement.RunSpeed > AnimMovement.WalkSpeed && AnimMovement.WalkSpeed > 0.0f)
            {
                const float GaitBoundary = (AnimMovement.WalkSpeed + AnimMovement.RunSpeed) * 0.5f;
                if (AnimMovement.SplineSpeed <= GaitBoundary) AnimMovement.MoveFlags |= WowMovementFlags::WALK_MODE;
                else AnimMovement.MoveFlags &= ~WowMovementFlags::WALK_MODE;
            }
		}
		else if (!bIsVisuallyMoving)
		{
			AnimMovement.MoveFlags &= ~DirectionalFlags;
		}

		if (bIsVisuallyMoving)
		{
			// Drive locomotion from actual rendered displacement. This covers remote
			// players interpolating between samples while allowing a stationary or
			// zero-length NPC spline to return to Stand instead of running in place.
			if ((AnimMovement.MoveFlags & DirectionalFlags) == 0)
			{
				AnimMovement.MoveFlags |= WowMovementFlags::FORWARD;
			}
		}

		// Don't override an active cast/channel with movement updates. If its
		// network bookkeeping has already ended, release a stale final pose now.
		EWowAnimState CurrentAnimState = AnimController->GetCurrentState();
		if (CurrentAnimState == EWowAnimState::Casting || CurrentAnimState == EWowAnimState::Channeling)
		{
			if (ActiveTimedCastSpells.Contains(Guid) || ActiveChannelSpells.Contains(Guid))
			{
				AnimController->TickPendingSpellCast(); // r372: queued cast after the release clip
				continue;
			}
			AnimController->EndSpellPresentation();
		}

		// r713 (Stefan: "when others log out, make sure they also sit for us"): another character whose stand state
		// changes from standing to sitting on the ground (logging out, /sit) plays the authored sit-down first, and
		// the stand-up when it gets up again, like our own character; before, it snapped straight into the loop.
		{
			static TMap<uint64, uint8> GLastRemoteStand;
			static TMap<uint64, double> GRemoteSitTransitionUntil;
			const double Now = FPlatformTime::Seconds();
			if (const uint8* Previous = GLastRemoteStand.Find(Guid); Previous && *Previous != StandState && !bIsVisuallyMoving)
			{
				const bool bSitDown = *Previous == WowStandState::STAND && StandState == WowStandState::SIT;
				const bool bStandUp = *Previous == WowStandState::SIT && StandState == WowStandState::STAND;
				if ((bSitDown || bStandUp) && AnimController->PlayAnimationById(bSitDown ? EWowAnimId::SitGroundDown : EWowAnimId::SitGroundUp, false))
					GRemoteSitTransitionUntil.Add(Guid, Now + 1.6);
			}
			GLastRemoteStand.Add(Guid, StandState);
			if (const double* Until = GRemoteSitTransitionUntil.Find(Guid))
			{
				if (Now < *Until && !bIsVisuallyMoving) continue; // let the transition play out
				GRemoteSitTransitionUntil.Remove(Guid);
			}
		}

		// Check for stand state-based animations (sit, kneel, sleep) that override movement
		if (!bIsVisuallyMoving && (AnimMovement.MoveFlags & DirectionalFlags) == 0) // Only for non-moving entities
		{
			EWowAnimId StandStateAnim = GetAnimationIdForStandState(StandState);
			// r499 (Stefan: the Lazy Peons stood upright under their "Zzz"): the sleep is an aura
			// ("Peon Sleeping"), not a stand state. An aura whose spell visual's state kit loops a
			// posture (sleep, sit, kneel, stun) holds that pose, as in the original client.
			if (StandStateAnim == EWowAnimId::Stand)
			{
				const FDbcStore& AuraDbc = FDbcStore::Get();
				for (const FAuraInfo& Aura : Entity->Auras)
				{
					if (!Aura.SpellId || !Aura.bActive) continue;
					const auto* AuraSpell = AuraDbc.Spells().GetById(Aura.SpellId);
					if (!AuraSpell) continue;
					const auto* AuraVisual = AuraDbc.SpellVisuals().GetById(AuraSpell->SpellVisual[0] ? AuraSpell->SpellVisual[0] : AuraSpell->SpellVisual[1]);
					const auto* StateKit = AuraVisual ? AuraDbc.SpellVisualKits().GetById(AuraVisual->StateKit) : nullptr;
					const uint32 Pose = StateKit ? StateKit->AnimID : 0;
					if (Pose == uint32(EWowAnimId::Sleep) || Pose == uint32(EWowAnimId::SitGround)
						|| Pose == uint32(EWowAnimId::Kneel) || Pose == uint32(EWowAnimId::Stun))
					{ StandStateAnim = static_cast<EWowAnimId>(Pose); break; }
					if (AuraSpell->SpellName.Contains(TEXT("Sleeping"))) { StandStateAnim = EWowAnimId::Sleep; break; }
				}
			}
			if (StandStateAnim != EWowAnimId::Stand) // Non-default stand state
			{
				// Play the stand state animation (sit, kneel, sleep, etc.)
				if (AnimController->PlayAnimationById(StandStateAnim, true))
				{
					continue; // Skip regular movement-based animation update
				}
			}
		}

		bool bEntityInCombat = (Entity->GetField(UnitField::FLAGS) & 0x00080000) != 0;
		bool bEntityCasting = false;

		// r372: inside the stop grace the rendered speed is ~0; keep the last stride rate instead
		// of a slow-motion walk.
		static TMap<uint64, float> GLastStrideSpeed;
		float StrideSpeed = EntityVisualSpeedsCm.FindRef(Guid);
		if (!bInStopGrace && StrideSpeed > 30.f) GLastStrideSpeed.Add(Guid, StrideSpeed);
		else if (bInStopGrace) StrideSpeed = GLastStrideSpeed.Contains(Guid) ? GLastStrideSpeed[Guid] : -1.f;
		AnimController->UpdateAnimationState(AnimMovement, bEntityInCombat || ServerMeleeVictims.Contains(Guid), bEntityCasting, StrideSpeed);
		if (TObjectPtr<AActor>* Rider = SpawnedEntityRiderActors.Find(Guid);
			Rider && *Rider && !ActiveTimedCastSpells.Contains(Guid) && !ActiveChannelSpells.Contains(Guid))
		{
			if (UWowAnimationController* RiderAnimation =
				FWowCharacterBuilder::GetAnimationController(*Rider))
			{
				RiderAnimation->PlayAnimationById(EWowAnimId::Mount, true);
			}
		}
	}
}

void AWowGameplayController::CreateActionBarWidget()
{
	if (!ConnectionManager || ActionBarWidget.IsValid())
	{
		return;
	}

	// Create the action bar widget
	ActionBarWidget = SNew(SWowActionBar)
		.ConnectionManager(ConnectionManager);

	// Add to viewport with proper positioning container
	if (GEngine && GEngine->GameViewport)
	{
		// Wrap in positioned overlay that passes clicks through
		TSharedRef<SWidget> ActionBarContainer =
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible) // Pass clicks through to 3D world
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)  // Bottom center positioning
			.VAlign(VAlign_Bottom)
			.Padding(0, 0, 0, 20)  // 20px margin from bottom edge
			[
				ActionBarWidget.ToSharedRef()
			];

		FWowUiSurfaces::Add(
			ActionBarContainer, EWowUiZone::ActionBars,
			60 // Z-order
		);
	}

	UE_LOG(LogWowGameplay, Log, TEXT("Created action bar widget"));
}

void AWowGameplayController::CreatePerformanceOverlay()
{
	if (PerformanceOverlayViewportWidget.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	PerformanceOverlayViewportWidget =
		SNew(SOverlay)
		.Visibility(EVisibility::Collapsed)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(0.0f, 0.0f, 0.0f, 135.0f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.BorderBackgroundColor(FLinearColor(0.015f, 0.015f, 0.012f, 0.82f))
			.Padding(FMargin(10.0f, 6.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SAssignNew(PerformanceFpsText, STextBlock)
					.Text(FText::FromString(TEXT("FPS: --")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					.ColorAndOpacity(FLinearColor::White)
					.ShadowOffset(FVector2D(1.0f, 1.0f))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SAssignNew(PerformanceTimingText, STextBlock)
					.Text(FText::FromString(TEXT("Frame -- | Game -- | Render -- | RHI -- | GPU --")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.ColorAndOpacity(FLinearColor(0.88f, 0.88f, 0.82f, 1.0f))
					.Visibility(EVisibility::Collapsed)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SAssignNew(PerformanceLimitText, STextBlock)
					.Text(FText::FromString(TEXT("Limit: -- | VSync: -- | Foreground: --")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.ColorAndOpacity(FLinearColor(0.72f, 0.72f, 0.68f, 1.0f))
					.Visibility(EVisibility::Collapsed)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SAssignNew(PerformanceUpscalerText, STextBlock)
					.Text(FText::FromString(TEXT("AMD FSR 4.1: INACTIVE")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
					.ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f, 1.0f))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SAssignNew(PerformanceFrameGenerationText, STextBlock)
					.Text(FText::FromString(TEXT("Frame Generation: OFF")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f, 1.0f))
				]
			]
		];

	FWowUiSurfaces::Add(PerformanceOverlayViewportWidget.ToSharedRef(), EWowUiZone::Debug, 6000);
}

void AWowGameplayController::TogglePerformanceOverlay()
{
	CreatePerformanceOverlay();
	if (!PerformanceOverlayViewportWidget.IsValid())
	{
		return;
	}

	bPerformanceOverlayEnabled = !bPerformanceOverlayEnabled;
	PerformanceSampleSeconds = 0.0f;
	PerformanceSampleFrames = 0;
	PerformanceOverlayViewportWidget->SetVisibility(
		bPerformanceOverlayEnabled
			? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	if (bPerformanceOverlayEnabled)
	{
		UpdatePerformanceOverlay(0.0f);
	}
	UE_LOG(LogWowGameplay, Log, TEXT("CTRL+R performance overlay %s"),
		bPerformanceOverlayEnabled ? TEXT("enabled") : TEXT("disabled"));
}

void AWowGameplayController::UpdatePerformanceOverlay(float DeltaTime)
{
	if (!bPerformanceOverlayEnabled || !PerformanceOverlayViewportWidget.IsValid())
	{
		return;
	}

	bool bRefreshDisplay = DeltaTime <= 0.0f;
	if (DeltaTime > 0.0f)
	{
		PerformanceSampleSeconds += DeltaTime;
		++PerformanceSampleFrames;
	}
	if (PerformanceSampleSeconds >= 0.25f)
	{
		LastMeasuredFps = static_cast<float>(PerformanceSampleFrames) / PerformanceSampleSeconds;
		PerformanceSampleSeconds = 0.0f;
		PerformanceSampleFrames = 0;
		bRefreshDisplay = true;
	}
	if (!bRefreshDisplay)
	{
		return;
	}

	if (PerformanceFpsText.IsValid() && LastMeasuredFps > 0.0f)
	{
		const FLinearColor FpsColor = LastMeasuredFps >= 60.0f
			? FLinearColor(0.25f, 1.0f, 0.25f, 1.0f)
			: (LastMeasuredFps >= 30.0f
				? FLinearColor(1.0f, 0.82f, 0.12f, 1.0f)
				: FLinearColor(1.0f, 0.25f, 0.18f, 1.0f));
		// The game thread only sees rendered frames. With our FSR frame generation presenting
		// through its own DX12 swapchain, every rendered frame is followed by one generated
		// frame, so show what reaches the screen and the rendered rate separately.
		static const IConsoleVariable* const FsrFrameInterpolation =
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FI.Enabled"));
		static const IConsoleVariable* const FsrFrameInterpolationSwapchain =
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FI.OverrideSwapChainDX12"));
		const bool bFsrFramesGenerated = FWowClientPreferences::IsFrameGenerationEnabled()
			&& FsrFrameInterpolation && FsrFrameInterpolation->GetInt() != 0
			&& FsrFrameInterpolationSwapchain && FsrFrameInterpolationSwapchain->GetInt() != 0;
		// NVIDIA DLSS frame generation presents 2-4 frames per rendered one through Streamline;
		// the counter showed only the rendered rate, so turning it on looked like a loss (r286).
		const int32 NvidiaPresented = FWowClientPreferences::IsFrameGenerationEnabled()
			? FWowClientPreferences::GetNvidiaPresentedFramesPerRender() : 1;
		const int32 PresentedPerRender = bFsrFramesGenerated ? 2 : NvidiaPresented;
		PerformanceFpsText->SetText(FText::FromString(PresentedPerRender > 1
			? FString::Printf(TEXT("FPS: %.0f (%.0f rendered)"), LastMeasuredFps * PresentedPerRender, LastMeasuredFps)
			: FString::Printf(TEXT("FPS: %.0f"), LastMeasuredFps)));
		PerformanceFpsText->SetColorAndOpacity(FpsColor);
	}

	const bool bShowTimings = FWowClientPreferences::ArePerformanceTimingsEnabled();
	if (PerformanceTimingText.IsValid())
	{
		PerformanceTimingText->SetVisibility(bShowTimings ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (bShowTimings && LastMeasuredFps > 0.0f)
		{
			const float FrameMs = 1000.0f / LastMeasuredFps;
			const float GameMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
			const float RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
			const float RhiMs = FPlatformTime::ToMilliseconds(GRHIThreadTime);
			const float GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
			PerformanceTimingText->SetText(FText::FromString(FString::Printf(
				TEXT("Frame %.1f ms | Game %.1f | Render %.1f | RHI %.1f | GPU %.1f"),
				FrameMs, GameMs, RenderMs, RhiMs, GpuMs)));
		}
	}

	const bool bShowRenderingStatus = FWowClientPreferences::IsPerformanceRenderingStatusEnabled();
	if (PerformanceLimitText.IsValid())
	{
		PerformanceLimitText->SetVisibility(
			bShowRenderingStatus ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		if (bShowRenderingStatus)
		{
			static const IConsoleVariable* const VSync =
				IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
			static const IConsoleVariable* const FsrSwapchain =
				IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FI.OverrideSwapChainDX12"));
			const float FrameLimit = GEngine ? GEngine->GetMaxFPS() : 0.0f;
			const FString FrameLimitText = FrameLimit > 0.0f
				? FString::Printf(TEXT("%.0f"), FrameLimit)
				: TEXT("OFF");
			PerformanceLimitText->SetText(FText::FromString(FString::Printf(
				TEXT("Limit: %s | Sync: %s (VSync option %s, presenting %s) | Foreground: %s | Present: %s"),
				*FrameLimitText,
				*FWowClientPreferences::GetSyncStatusLabel(),
				// r503 (Stefan: "says VSync OFF, but the option is on"): the option and what is presenting now.
				GEngine && GEngine->GetGameUserSettings() && GEngine->GetGameUserSettings()->IsVSyncEnabled() ? TEXT("ON") : TEXT("OFF"),
				VSync && VSync->GetInt() != 0 ? TEXT("VSync") : TEXT("free"),
				IsWowClientProcessForeground() ? TEXT("YES") : TEXT("NO"),
				FsrSwapchain && FsrSwapchain->GetInt() != 0 ? TEXT("FSR DX12") : TEXT("UE DXGI"))));
		}
	}

	static IConsoleVariable* const FsrEnabled =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FSR.Enabled"));
	static IConsoleVariable* const FsrQuality =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FSR.QualityMode"));
	static IConsoleVariable* const NativeAa =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"));
	static IConsoleVariable* const ScreenPercentage =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
    const auto* Dlss=IConsoleManager::Get().FindConsoleVariable(TEXT("r.NGX.DLSS.Enable"));
    const bool bDlssActive=FWowClientPreferences::IsDlssAvailable() && Dlss && Dlss->GetInt()!=0;
	const bool bFsrActive = FsrEnabled && FsrEnabled->GetInt() != 0 &&
		NativeAa && NativeAa->GetInt() == 4;
	static const TCHAR* QualityNames[] = {
		TEXT("Native AA"), TEXT("Quality"), TEXT("Balanced"),
		TEXT("Performance"), TEXT("Ultra Performance")
	};
	const int32 QualityIndex = FMath::Clamp(FsrQuality ? FsrQuality->GetInt() : 1, 0, 4);
	if (PerformanceUpscalerText.IsValid())
	{
		PerformanceUpscalerText->SetVisibility(
			bShowRenderingStatus ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		const float RenderPercentage = ScreenPercentage ? ScreenPercentage->GetFloat() : 100.0f;
		PerformanceUpscalerText->SetText(FText::FromString(bDlssActive
            ? FString::Printf(TEXT("NVIDIA %s: ACTIVE (%.0f%% render)"),RenderPercentage>=99.9f ? TEXT("DLAA") : TEXT("DLSS"),RenderPercentage)
            : bFsrActive
			? FString::Printf(TEXT("AMD FSR 4.1: ACTIVE - %s (%.0f%% render)"),
				QualityNames[QualityIndex], RenderPercentage)
			: FString(TEXT("AMD FSR 4.1: INACTIVE"))));
		PerformanceUpscalerText->SetColorAndOpacity(bFsrActive || bDlssActive
			? FLinearColor(0.20f, 1.0f, 0.25f, 1.0f)
			: FLinearColor(0.55f, 0.55f, 0.55f, 1.0f));
	}

	static IConsoleVariable* const FrameGeneration =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FI.Enabled"));
    const bool bFrameGenerationRequested = FWowClientPreferences::IsFrameGenerationEnabled();
    const auto* NvidiaFG=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streamline.DLSSG.Enable"));
    const bool bFrameGenerationEnabled = (FrameGeneration && FrameGeneration->GetInt() != 0)
        || (FWowClientPreferences::IsNvidiaFrameGenerationAvailable() && NvidiaFG && NvidiaFG->GetInt()!=0);
    bool bFrameGenerationActive = FWowClientPreferences::IsNvidiaFrameGenerationActive();
#if PLATFORM_WINDOWS
// Not anything for 5.8
	bFrameGenerationActive |= bFrameGenerationEnabled;
#endif
    const TCHAR* FrameGenerationStatus = !bFrameGenerationRequested ? TEXT("Frame Generation: OFF")
        : bFrameGenerationActive ? TEXT("Frame Generation: ACTIVE")
        : bFrameGenerationEnabled ? TEXT("Frame Generation: ON - waiting for generated frames")
        : TEXT("Frame Generation: requested, unavailable");
	if (PerformanceFrameGenerationText.IsValid())
	{
		PerformanceFrameGenerationText->SetVisibility(
			bShowRenderingStatus ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		PerformanceFrameGenerationText->SetText(FText::FromString(
			FrameGenerationStatus));
		PerformanceFrameGenerationText->SetColorAndOpacity(bFrameGenerationActive
			? FLinearColor(0.20f, 1.0f, 0.25f, 1.0f)
			: FLinearColor(0.55f, 0.55f, 0.55f, 1.0f));
	}
}

void AWowGameplayController::CreateXPBar()
{
	if (!ConnectionManager || XPBar.IsValid())
	{
		return;
	}

	// Create XP bar with overlay for text
	TSharedRef<SWidget> XPBarContainer =
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible) // Pass clicks through to 3D world
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)  // Full width
		.VAlign(VAlign_Bottom)
		.Padding(0, 0, 0, 70)  // Position above action bar (70px from bottom)
		[
			SNew(SBox)
			.HeightOverride(10) // Fixed height of 10px
			[
				// XP progress bar
				SAssignNew(XPBar, SProgressBar)
				.Style(&FCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>("ProgressBar"))
				.BarFillType(EProgressBarFillType::LeftToRight)
				.FillColorAndOpacity(FLinearColor(0.5f, 0.0f, 1.0f, 0.8f)) // Purple fill like WoW
				.BackgroundImage(FCoreStyle::Get().GetBrush("ProgressBar.Background"))
				.Percent(0.0f)
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(0, 0, 0, 70)
		[
			// XP text overlay
			SAssignNew(XPText, STextBlock)
			.Text(FText::FromString(TEXT("XP: 0 / 0 (0%)")))
			.ColorAndOpacity(FLinearColor::White)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
			.Justification(ETextJustify::Center)
		];

	// Add to viewport
	if (GEngine && GEngine->GameViewport)
	{
		FWowUiSurfaces::Add(
			XPBarContainer, EWowUiZone::ActionBars,
			50 // Z-order (below action bar)
		);
	}

	UE_LOG(LogWowGameplay, Log, TEXT("Created XP bar widget"));
}

void AWowGameplayController::UpdateXPBar()
{
	if (!ConnectionManager || !XPBar.IsValid() || !XPText.IsValid())
	{
		return;
	}

	// Get local player entity
	const FWowEntity* PlayerEntity = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
	if (!PlayerEntity)
	{
		return;
	}

	uint32 CurrentXP = PlayerEntity->GetField(PlayerField::XP);
	uint32 NextLevelXP = PlayerEntity->GetField(PlayerField::NEXT_LEVEL_XP);
	int32 Level = PlayerEntity->GetLevel();

	// Track XP changes and fire events
	static uint32 LastKnownXP = 0;
	if (CurrentXP != LastKnownXP && LastKnownXP > 0)
	{
		FireUIEvent(TEXT("PLAYER_XP_UPDATE"));
		LastKnownXP = CurrentXP;
	}
	else if (LastKnownXP == 0)
	{
		LastKnownXP = CurrentXP;
	}

	// Handle max level case
	if (NextLevelXP == 0 || Level >= 80) // Max level for 3.3.5a
	{
		XPBar->SetPercent(1.0f);
		XPText->SetText(FText::FromString(TEXT("Max Level")));
	}
	else
	{
		// Calculate percentage
		float Percent = NextLevelXP > 0 ? static_cast<float>(CurrentXP) / static_cast<float>(NextLevelXP) : 0.0f;
		int32 PercentInt = FMath::RoundToInt(Percent * 100.0f);

		XPBar->SetPercent(Percent);

		FString XPString = FString::Printf(TEXT("XP: %u / %u (%d%%)"), CurrentXP, NextLevelXP, PercentInt);
		XPText->SetText(FText::FromString(XPString));
	}
}

void AWowGameplayController::CreateMinimapWidget()
{
	if (!ConnectionManager || MinimapWidget.IsValid())
	{
		return;
	}

	// Get world manager
	AWowWorldManager* WorldManager = nullptr;
	if (UWorld* World = GetWorld())
	{
		TArray<AActor*> WorldManagers;
		UGameplayStatics::GetAllActorsOfClass(World, AWowWorldManager::StaticClass(), WorldManagers);
		if (WorldManagers.Num() > 0)
		{
			WorldManager = Cast<AWowWorldManager>(WorldManagers[0]);
		}
	}

	if (!WorldManager)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Could not find WorldManager for minimap"));
		return;
	}

	// Create the minimap widget
	MinimapWidget = SNew(SWowMinimap)
		.EntityManager(&ConnectionManager->PacketHandler.EntityManager)
		.QuestGiverStatuses(&ConnectionManager->PacketHandler.QuestGiverStatuses)
        .GroupInfo(&ConnectionManager->PacketHandler.GroupInfo)
		.WorldManager(WorldManager);

	if (!MinimapPlayerArrowTexture)
	{
		MinimapPlayerArrowTexture = LoadOriginalUiTexture(
			WorldManager->GetMpqManager(), WorldManager->GetAssetCache(),
			TEXT("Interface\\Minimap\\MinimapArrow.blp"), TEXT("MinimapArrow"));
		if (!MinimapPlayerArrowTexture)
		{
			UE_LOG(LogWowGameplay, Error,
				TEXT("Required WoW UI asset is missing: Interface\\Minimap\\MinimapArrow.blp"));
		}
	}
	MinimapWidget->SetPlayerArrowTexture(MinimapPlayerArrowTexture);
	MinimapWidget->SetQuestSource(&ConnectionManager->PacketHandler, [this]()
	{
		const auto* Area = LastUiZoneId ? FDbcStore::Get().AreaTable().GetById(LastUiZoneId) : nullptr;
		return Area ? Area->MapID : MAX_uint32;
	});
	// Quest diamonds on the minimap: a gem on the map, or the gold rim arrow when off it (only for
	// quests with a diamond - Stefan 11.10).
	MinimapWidget->SetDiamondSource([WeakMinimapOwner = TWeakObjectPtr<AWowGameplayController>(this)](TArray<FVector>& Out)
	{
		AWowGameplayController* Owner = WeakMinimapOwner.Get();
		if (!Owner) return;
		for (const uint32 DiamondQuestId : TArray<uint32>(Owner->WorldTrackedQuests))
		{
			FVector DiamondWorld; float DiamondYards = 0.f;
			if (Owner->GetQuestDiamondTarget(DiamondQuestId, DiamondWorld, DiamondYards)) Out.Add(DiamondWorld);
		}
	});

	EnsureMinimapWorldCapture();
	if (MinimapRenderTarget)
	{
		MinimapWidget->SetMapRenderTarget(MinimapRenderTarget);
	}

	// Add to viewport with proper positioning container
	if (GEngine && GEngine->GameViewport)
	{
		// Wrap in positioned overlay that passes clicks through
		TSharedRef<SWidget> MinimapContainer =
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible) // Pass clicks through to 3D world
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)  // Top right positioning
			.VAlign(VAlign_Top)
			// The live map is four pixels left of its previous placement so its
			// circular mask is concentric with the authored WotLK ring.
			.Padding(0, 40, 42, 0)
			[
				MinimapWidget.ToSharedRef()
			];

		FWowUiSurfaces::Add(
			MinimapContainer, EWowUiZone::Minimap,
			// FrameXML is mounted at 50. The captured world belongs underneath
			// that layer so the ring, tracking, time and queue controls remain
			// visible and clickable above the map image.
			45
		);
	}

	UE_LOG(LogWowGameplay, Log, TEXT("Created minimap widget"));
}

void AWowGameplayController::ReloadUiNow()
{
	if (!UIManager || !UIManager->ReloadUI())
	{
		return;
	}
	PressedLeftMouseFrameHandle = -1;
	PressedRightMouseFrameHandle = -1;
	PendingUiDragFrame = -1;

	// The frame manager is new: rebind the native hooks the old one had.
	if (FWowFrameManager* FrameManager = UIManager->GetFrameManager())
	{
		FrameManager->OnSecureActionDispatch.BindLambda([this](int32 ActionSlot)
		{
			CastSpellFromSlot(ActionSlot);
		});
		FrameManager->OnSecureSpellDispatch.BindLambda([this](int32 SpellId)
		{
			if (ConnectionManager)
			{
				ConnectionManager->SendCastSpell(SpellId, static_cast<int64>(TargetGuid));
			}
		});
	}
	bMinimapUiDelegatesBound = false;
	BindMinimapUiDelegates();

	// LoadUI already fired VARIABLES_LOADED / PLAYER_LOGIN / PLAYER_ENTERING_WORLD;
	// replay the state the old UI had received piece by piece.
	FireUIEvent(TEXT("PLAYER_TARGET_CHANGED"));
	FireUIEvent(TEXT("SPELLS_CHANGED"));
	if (ConnectionManager)
	{
		const TArray<uint32>& AB = ConnectionManager->PacketHandler.ActionButtons;
		for (int32 i = 0; i < FMath::Min(AB.Num(), 120); ++i)
		{
			if (AB[i] != 0)
			{
				FireUIEvent(TEXT("ACTIONBAR_SLOT_CHANGED"), {FString::Printf(TEXT("%d"), i + 1)});
			}
		}
		FireUIEvent(TEXT("ACTIONBAR_UPDATE_STATE"));
		if (UIManager->GetFrameManager())
		{
			UIManager->GetFrameManager()->RefreshActionButtonIcons(AB);
		}
	}
	for (const TCHAR* Unit : {TEXT("player"), TEXT("target"), TEXT("pet")})
	{
		for (const TCHAR* Event : {TEXT("UNIT_HEALTH"), TEXT("UNIT_MAXHEALTH"), TEXT("UNIT_MANA"), TEXT("UNIT_MAXMANA"),
			TEXT("UNIT_POWER"), TEXT("UNIT_AURA"), TEXT("UNIT_PORTRAIT_UPDATE"), TEXT("UNIT_NAME_UPDATE")})
		{
			FireUIEvent(Event, {Unit});
		}
	}
	for (int32 Bag = 0; Bag <= 4; ++Bag)
	{
		FireUIEvent(TEXT("BAG_UPDATE"), {FString::FromInt(Bag)});
	}
	for (const TCHAR* Event : {TEXT("PLAYER_FLAGS_CHANGED"), TEXT("UPDATE_SHAPESHIFT_FORM"), TEXT("PLAYER_MONEY"),
		TEXT("PLAYER_XP_UPDATE"), TEXT("UPDATE_EXHAUSTION"), TEXT("QUEST_LOG_UPDATE"), TEXT("UPDATE_FACTION"),
		TEXT("PARTY_MEMBERS_CHANGED"), TEXT("ZONE_CHANGED_NEW_AREA"), TEXT("MINIMAP_UPDATE_ZOOM")})
	{
		FireUIEvent(Event);
	}
	FireUIEvent(TEXT("UNIT_INVENTORY_CHANGED"), {TEXT("player")});
	// r252 (Stefan 12.10: dead in VR, turned VR off - which rebuilds the UI - and the Release
	// Spirit dialog was gone, so he could not release): the new UI opens its death dialogs from
	// these events like the old one did (DEATH, RECOVER_CORPSE, AREA_SPIRIT_HEAL, RESURRECT).
	if (ConnectionManager)
	{
		const FWowDeathState& Death = ConnectionManager->PacketHandler.DeathState;
		if (Death.bDead) FireUIEvent(TEXT("PLAYER_DEAD"));
		if (Death.bGhost && Death.bCorpseInRange) FireUIEvent(TEXT("CORPSE_IN_RANGE"));
		if (Death.bGhost && Death.bAreaSpiritHealerInRange) FireUIEvent(TEXT("AREA_SPIRIT_HEALER_IN_RANGE"));
		if (Death.ResurrecterGuid && !Death.ResurrecterName.IsEmpty()) FireUIEvent(TEXT("RESURRECT_REQUEST"), {Death.ResurrecterName});
	}
	UIManager->UpdateInventory();
	UIManager->RefreshUnitFrames();
	if (UIManager->GetFrameManager())
	{
		UIManager->GetFrameManager()->SyncChildVisibility();
	}
	UE_LOG(LogWowGameplay, Log, TEXT("ReloadUI complete"));
}

void AWowGameplayController::BindMinimapUiDelegates()
{
	if (bMinimapUiDelegatesBound || !UIManager || !UIManager->GetFrameManager())
	{
		return;
	}

	FWowFrameManager* FrameManager = UIManager->GetFrameManager();
	FrameManager->OnMinimapZoomRequest.BindLambda([this](bool bZoomIn)
	{
		if (!MinimapWidget.IsValid())
		{
			return;
		}
		if (bZoomIn)
		{
			MinimapWidget->ZoomIn();
		}
		else
		{
			MinimapWidget->ZoomOut();
		}
	});
	FrameManager->OnMinimapTrackingChanged.BindLambda([this](int32 TrackingMode)
	{
		if (MinimapWidget.IsValid())
		{
			MinimapWidget->SetTrackingMode(TrackingMode);
		}
	});
	FrameManager->OnDungeonFinderToggle.BindUObject(this, &AWowGameplayController::OnGroupFinderKey);
    FrameManager->OnSkillsToggle.BindUObject(this, &AWowGameplayController::OnSkillsKey);
	OnLfgUpdated();
	bMinimapUiDelegatesBound = true;
}

void AWowGameplayController::EnsureMinimapWorldCapture()
{
	if (!GetWorld())
	{
		return;
	}

	if (!MinimapRenderTarget)
	{
		MinimapRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("MinimapWorldRenderTarget"));
		MinimapRenderTarget->RenderTargetFormat = RTF_RGBA8;
		MinimapRenderTarget->ClearColor = FLinearColor(0.095f, 0.125f, 0.055f, 1.0f);
		MinimapRenderTarget->Filter = TF_Trilinear;
		MinimapRenderTarget->bAutoGenerateMips = true;
		MinimapRenderTarget->MipsSamplerFilter = TF_Trilinear;
		MinimapRenderTarget->InitAutoFormat(MinimapCaptureResolution, MinimapCaptureResolution);
		MinimapRenderTarget->UpdateResourceImmediate(true);
	}

	if (!MinimapCaptureActor)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		MinimapCaptureActor = GetWorld()->SpawnActor<ASceneCapture2D>(
			ASceneCapture2D::StaticClass(),
			FVector::ZeroVector,
			FRotator(-90.0f, 0.0f, 0.0f),
			SpawnParams);

		if (MinimapCaptureActor && MinimapCaptureActor->GetCaptureComponent2D())
		{
			USceneCaptureComponent2D* Capture = MinimapCaptureActor->GetCaptureComponent2D();
			Capture->TextureTarget = MinimapRenderTarget;
			Capture->ProjectionType = ECameraProjectionMode::Orthographic;
			Capture->OrthoWidth = MinimapCaptureOrthoWidth;
			Capture->LODDistanceFactor = MinimapCaptureLodDistanceFactor;
			Capture->bAutoCalculateOrthoPlanes = false;
			Capture->bUpdateOrthoPlanes = false;
			Capture->bUseCameraHeightAsViewTarget = false;
			Capture->bCaptureEveryFrame = false;
			// CaptureScene below is the single authority for minimap refreshes.  Enabling
			// capture-on-movement as well caused every SetActorLocation call to enqueue a
			// second complete scene render.
			Capture->bCaptureOnMovement = false;
			Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
			Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
			Capture->CompositeMode = ESceneCaptureCompositeMode::SCCM_Overwrite;
			Capture->ShowFlags.SetFog(false);
			Capture->ShowFlags.SetAtmosphere(false);
			Capture->ShowFlags.SetCloud(false);
			Capture->ShowFlags.SetVolumetricFog(false);
			Capture->ShowFlags.SetPostProcessing(false);
			// r365 (Stefan: the minimap is choppy - the sea moves in jumps - and FPS is low while islands
			// stream in): the original minimap is flat terrain art without units, shadows or glints. Drop
			// everything it never showed, so each top-down render is cheap enough to refresh often.
			Capture->ShowFlags.SetSkeletalMeshes(false);
			Capture->ShowFlags.SetParticles(false);
			Capture->ShowFlags.SetDynamicShadows(false);
			Capture->ShowFlags.SetContactShadows(false);
			Capture->ShowFlags.SetAmbientOcclusion(false);
			Capture->ShowFlags.SetGlobalIllumination(false);
			Capture->ShowFlags.SetReflectionEnvironment(false);
			Capture->ShowFlags.SetScreenSpaceReflections(false);
			Capture->ShowFlags.SetSpecular(false);
			Capture->ShowFlags.SetPointLights(false);
			Capture->ShowFlags.SetSpotLights(false);
			Capture->ShowFlags.SetRectLights(false);
		}
	}
}

void AWowGameplayController::UpdateMinimapWorldCapture()
{
	if (!MinimapWidget.IsValid())
	{
		return;
	}

	EnsureMinimapWorldCapture();
	if (!MinimapCaptureActor || !ConnectionManager || !GetWorld())
	{
		return;
	}

	// The minimap is a north-locked terrain view, not a second gameplay camera.
	// A full scene capture every rendered frame doubled scene submission work and
	// kept a stationary client CPU-bound.  Ten update opportunities per second are
	// smooth at run speed; stationary frames require no capture at all.
	const double NowSeconds = GetWorld()->GetTimeSeconds();
	if (NowSeconds < NextMinimapWorldCaptureTimeSeconds)
	{
		return;
	}
	// r365: 20 refreshes a second (was 10), now that a capture is cheap (see the show flags above).
	NextMinimapWorldCaptureTimeSeconds = NowSeconds + 0.05;

	APawn* ControlledPawn = GetPawn();
	FWowPlayerEntity* LocalPlayer = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
	FVector PlayerLocation = ControlledPawn ? ControlledPawn->GetActorLocation() : FVector::ZeroVector;
	float PlayerYawDegrees = ControlledPawn ? ControlledPawn->GetActorRotation().Yaw : 0.0f;

	if (!ControlledPawn && LocalPlayer)
	{
		PlayerLocation = FWowCoordinate::WowToUE(LocalPlayer->Movement.Position);
		PlayerYawDegrees = FWowCoordinate::WowOrientationToUEYaw(LocalPlayer->Movement.Orientation);
	}

	const FVector2D RequestedCaptureCenter(PlayerLocation.X, PlayerLocation.Y);
	const float CaptureWidth = MinimapWidget->GetCaptureWidth();
    if (bHasMinimapWorldCaptureCenter && FMath::IsNearlyEqual(CaptureWidth, LastMinimapCaptureWidth) &&
		FVector2D::DistSquared(RequestedCaptureCenter, LastMinimapWorldCaptureCenter) < FMath::Square(50.0f) &&
        NowSeconds-LastMinimapRenderTime < 0.2) // r365: standing still, animated water refreshes 5x a second (was once)
	{
		return;
	}

	if (PlayerCameraManager)
	{
		if (AActor* ViewTarget = PlayerCameraManager->GetViewTarget())
		{
			if (!ControlledPawn || FVector::DistSquared2D(ViewTarget->GetActorLocation(), PlayerLocation) > FMath::Square(30000.0f))
			{
				PlayerLocation = ViewTarget->GetActorLocation();
				PlayerYawDegrees = ViewTarget->GetActorRotation().Yaw;
			}
		}

		const FVector CameraLocation = PlayerCameraManager->GetCameraLocation();
		if (!ControlledPawn || FVector::DistSquared2D(CameraLocation, PlayerLocation) > FMath::Square(120000.0f))
		{
			PlayerLocation.X = CameraLocation.X;
			PlayerLocation.Y = CameraLocation.Y;
		}

		FHitResult GroundHit;
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(MinimapCaptureGroundTrace), false);
		TraceParams.AddIgnoredActor(MinimapCaptureActor);
		if (ControlledPawn)
		{
			TraceParams.AddIgnoredActor(ControlledPawn);
		}

		const FVector TraceStart(PlayerLocation.X, PlayerLocation.Y, CameraLocation.Z + 50000.0f);
		const FVector TraceEnd(PlayerLocation.X, PlayerLocation.Y, CameraLocation.Z - 50000.0f);
		if (GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, TraceParams) ||
			GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
		{
			PlayerLocation.Z = GroundHit.Location.Z;
		}
	}

	MinimapCaptureActor->SetActorLocation(PlayerLocation + FVector(0.0f, 0.0f, 9000.0f));
	// WoW's default minimap is north-locked. Rotating minimap is an opt-in
	// setting, so keep the live capture fixed until that option exists.
	MinimapCaptureActor->SetActorRotation(FRotator(-90.0f, 0.0f, 0.0f));

	if (USceneCaptureComponent2D* Capture = MinimapCaptureActor->GetCaptureComponent2D())
	{
		Capture->OrthoWidth = CaptureWidth;
        Capture->bUseCustomProjectionMatrix = true;
        Capture->CustomProjectionMatrix = FReversedZOrthoMatrix(CaptureWidth*.5f,CaptureWidth*.5f,1.f/60000.f,0.f);
		Capture->LODDistanceFactor = MinimapCaptureLodDistanceFactor;
		Capture->bAutoCalculateOrthoPlanes = false;
		Capture->bUpdateOrthoPlanes = false;
		Capture->bUseCameraHeightAsViewTarget = false;
		Capture->MaxViewDistanceOverride = 60000.0f;
		Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		Capture->ShowFlags.SetFog(false);
		Capture->ShowFlags.SetAtmosphere(false);
		Capture->ShowFlags.SetCloud(false);
		Capture->ShowFlags.SetVolumetricFog(false);
		Capture->ShowFlags.SetPostProcessing(false);
		Capture->TextureTarget = MinimapRenderTarget;
		// The minimap is the terrain seen from above: spell effects (Lightning Shield
		// orbs, auras, missiles) around the player must not be drawn into it.
		// Hiding the actors is not enough: their point lights (the shield's flicker and
		// strike flashes) still lit the ground under the player, so the minimap capture
		// also skips dynamic local lights - the minimap is not lit by them in WoW either.
		Capture->ShowFlags.SetPointLights(false);
		Capture->ShowFlags.SetSpotLights(false);
		Capture->ShowFlags.SetRectLights(false);
		Capture->HiddenActors.Reset();
		for (const FActiveSpellVisual& Visual : ActiveSpellVisuals)
			if (AActor* Effect = Visual.Actor.Get()) Capture->HiddenActors.AddUnique(Effect);
		// Effects still fading out (one-shot casts, impacts) live only in the retained list.
		for (AActor* Effect : RetainedSpellVisualActors)
			if (IsValid(Effect)) Capture->HiddenActors.AddUnique(Effect);
		Capture->CaptureScene();
	}

	if (MinimapRenderTarget)
	{
		MinimapWidget->SetMapRenderTarget(MinimapRenderTarget);
	}

	LastMinimapWorldCaptureCenter = FVector2D(PlayerLocation.X, PlayerLocation.Y);
	bHasMinimapWorldCaptureCenter = true;
    LastMinimapCaptureWidth = CaptureWidth;
    LastMinimapRenderTime = NowSeconds;
}

void AWowGameplayController::CreateTargetFrame()
{
	// Legacy Slate unit frames were useful before the FrameXML/Lua UI was
	// rendering, but they now duplicate PlayerFrame/TargetFrame and can sit
	// directly over the stock-styled player unit frame.  Keep target state in
	// FrameXML and nameplates instead.
	return;

	if (!ConnectionManager || TargetFrameText.IsValid())
	{
		return;
	}

	// Target name text
	TargetFrameText = SNew(STextBlock)
		.Text(FText::GetEmpty())
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
		.ColorAndOpacity(FLinearColor::White);

	// Target health bar
	TargetHealthBar = SNew(SProgressBar)
		.Percent(1.0f)
		.FillColorAndOpacity(FLinearColor::Red)
		.BackgroundImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"));

	// Target health text
	TargetHealthText = SNew(STextBlock)
		.Text(FText::GetEmpty())
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
		.ColorAndOpacity(FLinearColor::White)
		.Justification(ETextJustify::Center);

	// Player health bar
	PlayerHealthBar = SNew(SProgressBar)
		.Percent(1.0f)
		.FillColorAndOpacity(FLinearColor::Green)
		.BackgroundImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"));

	PlayerHealthText = SNew(STextBlock)
		.Text(FText::GetEmpty())
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
		.ColorAndOpacity(FLinearColor::White)
		.Justification(ETextJustify::Center);

	PlayerNameText = SNew(STextBlock)
		.Text(FText::FromString(TEXT("Player")))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
		.ColorAndOpacity(FLinearColor(0.5f, 1.0f, 0.5f));

	// Initialize aura display bars
	PlayerBuffBar = SNew(SHorizontalBox);
	TargetBuffBar = SNew(SHorizontalBox);

	if (GEngine && GEngine->GameViewport)
	{
		// Target frame â€” top center
		TSharedRef<SWidget> TargetContainer =
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Top)
			.Padding(0, 20, 0, 0)
			[
				SNew(SBorder)
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f))
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.Padding(FMargin(8, 4))
				.Visibility_Lambda([this]() { return TargetGuid != 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
					[
						TargetFrameText.ToSharedRef()
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).WidthOverride(200).HeightOverride(16)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()
							[
								TargetHealthBar.ToSharedRef()
							]
							+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
							[
								TargetHealthText.ToSharedRef()
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[
						SNew(SBox).WidthOverride(200).HeightOverride(20)
						[
							TargetBuffBar.ToSharedRef()
						]
					]
				]
			];

		FWowUiSurfaces::Add(TargetContainer, EWowUiZone::UnitFrames, 80);

		// Player frame â€” top left
		TSharedRef<SWidget> PlayerContainer =
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(20, 20, 0, 0)
			[
				SNew(SBorder)
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f))
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.Padding(FMargin(8, 4))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
					[
						PlayerNameText.ToSharedRef()
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).WidthOverride(200).HeightOverride(16)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()
							[
								PlayerHealthBar.ToSharedRef()
							]
							+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
							[
								PlayerHealthText.ToSharedRef()
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[
						SNew(SBox).WidthOverride(200).HeightOverride(20)
						[
							PlayerBuffBar.ToSharedRef()
						]
					]
				]
			];

		FWowUiSurfaces::Add(PlayerContainer, EWowUiZone::UnitFrames, 80);

		UE_LOG(LogWowGameplay, Log, TEXT("Created player and target frame widgets"));
	}
}

void AWowGameplayController::UpdateTargetFrame()
{
	if (!ConnectionManager) return;

	// Update player health bar
	if (PlayerHealthBar.IsValid())
	{
		const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
		const FWowEntity* LocalEntity = ConnectionManager->PacketHandler.EntityManager.Find(LocalGuid);
		if (LocalEntity && LocalEntity->GetMaxHealth() > 0)
		{
			int32 HP = LocalEntity->GetHealth();
			int32 MaxHP = LocalEntity->GetMaxHealth();
			float Pct = FMath::Clamp((float)HP / (float)MaxHP, 0.0f, 1.0f);
			PlayerHealthBar->SetPercent(Pct);
			if (PlayerHealthText.IsValid())
			{
				PlayerHealthText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), HP, MaxHP)));
			}
		}
	}

	if (!TargetFrameText.IsValid()) return;

	// If no target, bars remain but target section hides via visibility lambda
	if (TargetGuid == 0) return;

	// Find the target entity
	const FWowEntity* TargetEntity = ConnectionManager->PacketHandler.EntityManager.Find(TargetGuid);
	if (!TargetEntity) return;

	// Update target health bar
	if (TargetHealthBar.IsValid())
	{
		int32 HP = TargetEntity->GetHealth();
		int32 MaxHP = TargetEntity->GetMaxHealth();
		float Pct = (MaxHP > 0) ? FMath::Clamp((float)HP / (float)MaxHP, 0.0f, 1.0f) : 1.0f;
		TargetHealthBar->SetPercent(Pct);
		if (TargetHealthText.IsValid())
		{
			TargetHealthText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), HP, MaxHP)));
		}
	}

	// Get target name
	FString TargetName = TEXT("Unknown");
	if (TargetEntity->IsPlayer())
	{
		// For players, try to get name from cache (would need proper name resolution)
		TargetName = FString::Printf(TEXT("Player %llu"), TargetGuid);
	}
	else if (TargetEntity->IsUnit())
	{
		// For creatures, try to get name from DBC
		TargetName = FString::Printf(TEXT("Creature %u"), TargetEntity->Entry);

		const FDbcStore& DbcStore = FDbcStore::Get();
		if (DbcStore.IsLoaded())
		{
			// Try to get creature name from DBC if available
			// Note: This assumes there's a CreatureTemplate DBC, adjust as needed
			TargetName = FString::Printf(TEXT("Creature %u"), TargetEntity->Entry);
		}
	}

	// Get health info
	int32 Health = TargetEntity->GetHealth();
	int32 MaxHealth = TargetEntity->GetMaxHealth();
	int32 Level = TargetEntity->GetLevel();

	// Create target display text
	FString TargetText = FString::Printf(TEXT("Target: %s Level %d HP: %d/%d"),
		*TargetName, Level, Health, MaxHealth);

	// Update the widget
	TargetFrameText->SetText(FText::FromString(TargetText));
	TargetFrameText->SetVisibility(EVisibility::SelfHitTestInvisible);

	// Color code based on target type
	FLinearColor TargetColor = FLinearColor::White;
	if (TargetEntity->IsPlayer())
	{
		TargetColor = FLinearColor::Green; // Friendly player
	}
	else if (TargetEntity->IsUnit())
	{
		// Simplified hostility check - assume creatures are hostile
		TargetColor = FLinearColor::Red; // Hostile creature
	}

	TargetFrameText->SetColorAndOpacity(TargetColor);
}

void AWowGameplayController::SetTarget(uint64 NewTargetGuid)
{
    // World objects and missing entities must never populate a unit frame.
    if (NewTargetGuid && ConnectionManager &&
        !ConnectionManager->PacketHandler.EntityManager.FindUnit(NewTargetGuid)) return;
	uint64 OldTargetGuid = TargetGuid;
    if(OldTargetGuid!=NewTargetGuid && OldTargetGuid)
    {LastSelectedGuid=OldTargetGuid;if(ConnectionManager)if(const auto* E=ConnectionManager->PacketHandler.EntityManager.Find(OldTargetGuid);E && IsWorldUnitAttackable(*E))LastHostileGuid=OldTargetGuid;}
	TargetGuid = NewTargetGuid;

	// Unit APIs read the selected GUID from the connection manager. Update that
	// shared selection before FrameXML receives PLAYER_TARGET_CHANGED so the
	// first refresh can populate name, portrait, level, health, and power.
	if (ConnectionManager)
	{
		ConnectionManager->SendSetSelection(static_cast<int64>(TargetGuid));
	}

	// Fire target change event
	if (OldTargetGuid != NewTargetGuid)
	{
		FireUIEvent(TEXT("PLAYER_TARGET_CHANGED"));
		if (UIManager && UIManager->GetFrameManager())
		{
			FWowFrameManager* FrameManager = UIManager->GetFrameManager();
			const int64 TargetFrameHandle = FrameManager->FindFrame(TEXT("TargetFrame"));
			if (TargetFrameHandle >= 0)
			{
				FrameManager->SetFrameVisible(TargetFrameHandle, NewTargetGuid != 0);
			}
			if (NewTargetGuid == 0)
			{
				static const TCHAR* TargetAdjunctFrames[] = {
					TEXT("TargetFrameToT"), TEXT("TargetFrameSpellBar"),
					TEXT("TargetFrameBuffs"), TEXT("TargetFrameDebuffs"),
					TEXT("TargetFrameNumericalThreat"), TEXT("TargetFrameDropDown")
				};
				for (const TCHAR* FrameName : TargetAdjunctFrames)
				{
					const int64 AdjunctHandle = FrameManager->FindFrame(FrameName);
					if (AdjunctHandle >= 0)
					{
						FrameManager->SetFrameVisible(AdjunctHandle, false);
					}
				}
			}
		}
		if (NewTargetGuid != 0)
		{
			FireUIEvent(TEXT("UNIT_PORTRAIT_UPDATE"), {TEXT("target")});
			FireUIEvent(TEXT("UNIT_HEALTH"), {TEXT("target")});
			FireUIEvent(TEXT("UNIT_MAXHEALTH"), {TEXT("target")});
			FireUIEvent(TEXT("UNIT_POWER"), {TEXT("target")});
			FireUIEvent(TEXT("UNIT_MANA"), {TEXT("target")});
			FireUIEvent(TEXT("UNIT_MAXMANA"), {TEXT("target")});
		}
	}

	// Update nameplate highlighting

	// Unhighlight previous target's nameplate
	if (OldTargetGuid != 0)
	{
		TObjectPtr<UWidgetComponent>* OldNameplatePtr = EntityNameplates.Find(OldTargetGuid);
		if (OldNameplatePtr && *OldNameplatePtr)
		{
			UWidgetComponent* NameplateComponent = *OldNameplatePtr;
			if (UWowNameplateWidget* NameplateWidget = Cast<UWowNameplateWidget>(NameplateComponent->GetUserWidgetObject()))
			{
				// Reset nameplate to normal state (would need to add this method to nameplate)
				// For now, just change the scale back to normal
				NameplateComponent->SetWorldScale3D(FVector(1.0f, 1.0f, 1.0f));
			}
		}
	}

	// Highlight new target's nameplate
	if (NewTargetGuid != 0)
	{
		TObjectPtr<UWidgetComponent>* NewNameplatePtr = EntityNameplates.Find(NewTargetGuid);
		if (NewNameplatePtr && *NewNameplatePtr)
		{
			UWidgetComponent* NameplateComponent = *NewNameplatePtr;
			if (UWowNameplateWidget* NameplateWidget = Cast<UWowNameplateWidget>(NameplateComponent->GetUserWidgetObject()))
			{
				// Highlight the nameplate (simple approach: scale it up slightly)
				NameplateComponent->SetWorldScale3D(FVector(1.2f, 1.2f, 1.2f));
			}
		}
	}

	// Update selected names immediately, even when nameplates are disabled.
	RefreshWorldLabels();
	UpdateTargetHighlight();

	// Log the change
	if (NewTargetGuid != 0)
	{
		UE_LOG(LogWowGameplay, Log, TEXT("Targeted entity GUID: %llu"), NewTargetGuid);
	}
	else
	{
		UE_LOG(LogWowGameplay, Log, TEXT("Target cleared"));
	}
}

static UMaterial* TargetRingSurface()
{
    static TWeakObjectPtr<UMaterial> Shared;
    if (Shared.IsValid()) return Shared.Get();
    UMaterial* Base = WowCookedMaterial::Load(TEXT("M_WowTargetRing_v2"));
#if WITH_EDITOR
    if (!Base)
    {
            UMaterial* M=NewObject<UMaterial>(GetTransientPackage(),NAME_None,RF_Transient);
            M->SetShadingModel(MSM_Unlit); M->BlendMode=BLEND_Translucent; M->TwoSided=true;
            auto* Color=NewObject<UMaterialExpressionVectorParameter>(M); Color->ParameterName=TEXT("RingColor");
            auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(M);
            auto* Mask=NewObject<UMaterialExpressionCustom>(M);
            // WoW: Forever style: a bright rim with a soft glow filling the disc toward it.
            Mask->Code=TEXT("float r=length(UV-0.5)*2; float rim=smoothstep(0.78,0.86,r)*(1-smoothstep(0.92,0.98,r)); float fill=smoothstep(0.0,0.86,r)*0.30*(1-smoothstep(0.86,0.90,r)); return saturate(rim*0.95+fill);");
            Mask->OutputType=CMOT_Float1;
            FCustomInput Input; Input.InputName=TEXT("UV"); Input.Input.Connect(0,UV); Mask->Inputs.Add(Input);
            M->GetExpressionCollection().AddExpression(Color); M->GetExpressionCollection().AddExpression(UV);
            M->GetExpressionCollection().AddExpression(Mask);
            M->GetEditorOnlyData()->EmissiveColor.Connect(0,Color); M->GetEditorOnlyData()->Opacity.Connect(0,Mask);
        M->PostEditChange();
        Base = WowCookedMaterial::Save(M, TEXT("M_WowTargetRing_v2"));
    }
#endif
    Shared = Base;
    return Base;
}
void WowPrepareTargetRingMaterial() { TargetRingSurface(); }

void AWowGameplayController::UpdateTargetHighlight()
{
    if (!bGameplayHudVisible || !GetPawn() || GetPawn()->IsHidden())
    {
        if (TargetHighlightCircle) TargetHighlightCircle->SetVisibility(false);
        if (HoverHighlightCircle) HoverHighlightCircle->SetVisibility(false);
        UpdateHoveredModel(0);
        return;
    }
    UpdateUnitHighlight(TargetGuid, TargetHighlightCircle, false);
    // Hover brightens the model; only the selected target gets a ground ring.
    if (HoverHighlightCircle) HoverHighlightCircle->SetVisibility(false);
    uint64 HoverGuid = IsWowOfflineSession() && ConnectionManager &&
        FParse::Param(FCommandLine::Get(),TEXT("testhovermodel"))
        ? ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid : HoveredCursorGuid;
    // Without a pointer on the object (mouse-look, first person, VR) the usable object the view
    // centre rests on, within reach, takes the highlight instead.
    static double NextLookAtScan = 0.0;
    static uint64 LookAtGuid = 0;
    if (!HoverGuid && ConnectionManager && PlayerCameraManager && FPlatformTime::Seconds() < NextLookAtScan)
        HoverGuid = LookAtGuid; // scanned ten times a second, not every frame
    else if (!HoverGuid && ConnectionManager && PlayerCameraManager)
    {
        NextLookAtScan = FPlatformTime::Seconds() + 0.1;
        const FVector Eye = PlayerCameraManager->GetCameraLocation();
        const FVector Look = PlayerCameraManager->GetCameraRotation().Vector();
        float BestAngle = .985f; // ~10 degrees
        for (const auto& Pair : SpawnedEntityActors)
        {
            const AActor* Object = Pair.Value;
            if (!IsValid(Object) || Object->IsHidden()) continue;
            const FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Pair.Key);
            if (!Entity || !Entity->IsGameObject() || !static_cast<const FWowGameObjectEntity*>(Entity)->CanInteractNow()) continue;
            FVector Centre, Extent; Object->GetActorBounds(false, Centre, Extent, true);
            const FVector To = Centre - Eye;
            if (To.SizeSquared() > FMath::Square(900.f) || To.SizeSquared() < 1.f) continue;
            const float Dot = FVector::DotProduct(To.GetSafeNormal(), Look);
            if (Dot > BestAngle && !IsWowWorldOccluded(GetWorld(), Eye, Centre, GetPawn(), const_cast<AActor*>(Object)))
            { BestAngle = Dot; HoverGuid = Pair.Key; }
        }
        LookAtGuid = HoverGuid;
    }
    UpdateHoveredModel(HoverGuid);
}

bool AWowGameplayController::FindCameraFocusUnit(const FMinimalViewInfo& View,
    float SceneDepth, float& OutDepth) const
{
    if (!ConnectionManager || !GetWorld()) return false;
    const FVector Forward = View.Rotation.Vector();
    const FRotationMatrix Rotation(View.Rotation);
    const FVector Right = Rotation.GetUnitAxis(EAxis::Y);
    const FVector Up = Rotation.GetUnitAxis(EAxis::Z);
    const double TanX = FMath::Tan(FMath::DegreesToRadians(View.FOV * .5));
    const double TanY = TanX / FMath::Max(double(View.AspectRatio), .1);
    // A narrow vertical focus area with a slightly wider circular centre.
    // These select a subject; depth still controls the optical blur everywhere.
    const FVector2D Samples[] = {{0,0}, {0,-.12}, {0,.12}, {0,-.25}, {0,.25},
        {-.065,0}, {.065,0}, {-.045,-.06}, {.045,-.06}, {-.045,.06}, {.045,.06}};
    const AActor* OwnPawn = GetPawn();
    const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
    // Our own character (and anything riding on it) is never the subject in third person.
    auto IsOwnBody = [OwnPawn](const AActor* A)
    {
        for (const AActor* P = A; P; P = P->GetAttachParentActor()) if (P == OwnPawn) return true;
        return false;
    };
    // The selected target wins whenever it is on screen (inner 85%) and nothing solid
    // stands in front of it - also when the camera is pitched down over the player.
    if (TargetGuid && TargetGuid != LocalGuid)
        if (const AActor* TargetActor = SpawnedEntityActors.FindRef(TargetGuid).Get(); IsValid(TargetActor) && !IsOwnBody(TargetActor))
        {
            const FBox Box = TargetActor->GetComponentsBoundingBox(false, true);
            const FVector Centre = Box.IsValid ? Box.GetCenter() : TargetActor->GetActorLocation();
            const FVector ToTarget = Centre - View.Location;
            const float Depth = FVector::DotProduct(ToTarget, Forward);
            const double ScreenX = Depth > 1.f ? FVector::DotProduct(ToTarget, Right) / (Depth * TanX) : 9.0;
            const double ScreenY = Depth > 1.f ? FVector::DotProduct(ToTarget, Up) / (Depth * TanY) : 9.0;
            if (Depth > 50.f && Depth < 8000.f && FMath::Abs(ScreenX) < .85 && FMath::Abs(ScreenY) < .85)
            {
                FCollisionQueryParams Query(SCENE_QUERY_STAT(WowTargetCameraFocus), true, GetPawn());
                Query.AddIgnoredActor(TargetActor);
                FHitResult Blocker;
                const bool bBlocked = WowCameraFocus::Trace(GetWorld(), Blocker, View.Location, Centre, ECC_Camera, Query)
                    && Blocker.Distance < ToTarget.Size() - 60.f;
                if (!bBlocked) { OutDepth = Depth; return true; }
            }
        }
    struct FCandidate { FVector Direction; float Distance; float Depth; double Score; const AActor* Actor = nullptr; };
    TArray<FCandidate, TInlineAllocator<16>> Candidates;
    // Analytic model-sized envelopes avoid CPU skinning/readbacks every frame.
    // Unlike click capsules these follow the head bone, race and current scale.
    auto Intersect = [&](const FVector& Direction, const FVector& Centre, const FVector& Extent, float& Distance)
    {
        if (Extent.GetMin() <= 1) return false;
        const FVector O = (View.Location-Centre) / Extent;
        const FVector D = Direction / Extent;
        const double A = D.SizeSquared(), B = FVector::DotProduct(O,D), C = O.SizeSquared()-1;
        const double Discriminant = B*B-A*C;
        if (C <= 0 || A <= SMALL_NUMBER || Discriminant < 0) return false;
        const double T = (-B-FMath::Sqrt(Discriminant))/A;
        if (T <= 1 || T >= Distance) return false;
        Distance = float(T);
        return true;
    };
    for (const auto& Pair : SpawnedEntityActors)
    {
        AActor* Actor = Pair.Value;
        const FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Pair.Key);
        if (!IsValid(Actor) || Pair.Key == LocalGuid || IsOwnBody(Actor) || Actor->IsHidden() || !Entity ||
            !Entity->IsUnit() || static_cast<const FWowUnitEntity*>(Entity)->IsDead() || DeadEntityGuids.Contains(Pair.Key)) continue;
        // Invisible trigger units (a campfire's fire bunny, spell targets) carry effect
        // models whose particle bounds cover half the screen: never focus on them.
        if (static_cast<const FWowUnitEntity*>(Entity)->GetUnitFlags() & 0x02000000u) continue;
        TArray<AActor*> FocusParts;
        Actor->GetAttachedActors(FocusParts,true,true);
        FocusParts.Add(Actor);
        for (AActor* FocusPart : FocusParts)
        {
        if (!IsValid(FocusPart) || FocusPart->IsHidden() || IsOwnBody(FocusPart)) continue;
        USkeletalMeshComponent* Mesh = FocusPart->FindComponentByClass<USkeletalMeshComponent>();
        if (!Mesh || !Mesh->IsVisible()) continue;
        const FVector Offset = Mesh->Bounds.Origin-View.Location;
        if (Offset.SizeSquared() > FMath::Square(5000.0 + Mesh->Bounds.SphereRadius)) continue;
        if (FVector::DotProduct(Offset,Forward) + Mesh->Bounds.SphereRadius <= 0) continue;
        FVector Head, HeadExtent, BodyCentre, BodyExtent;
        const bool bHasHead = Mesh->DoesSocketExist(TEXT("WowFirstPersonHead"));
        if (bHasHead)
        {
            Head = Mesh->GetSocketLocation(TEXT("WowFirstPersonHead"));
            const FVector Feet = Mesh->GetComponentLocation();
            const double Height = Head.Z-Feet.Z;
            if (Height <= 20) continue;
            HeadExtent = FVector(Height*.12);
            Head.Z += Height*.03;
            BodyCentre = FMath::Lerp(Feet,Head,.52);
            BodyExtent = FVector(Height*.18, Height*.18, Height*.39);
        }
        else
        {
            BodyCentre = Mesh->Bounds.Origin;
            // Bounds include particle emitters; a creature body is rarely wider than this.
            BodyExtent = Mesh->Bounds.BoxExtent.ComponentMin(FVector(260.0, 260.0, 260.0));
        }
        FCandidate Best{};
        Best.Score = TNumericLimits<double>::Max();
        for (const FVector2D& Sample : Samples)
        {
            const double Score = FMath::Square(Sample.X) + FMath::Square(Sample.Y*.35);
            if (Score > Best.Score) continue;
            const FVector Direction = (Forward+Right*(Sample.X*TanX)+Up*(Sample.Y*TanY)).GetSafeNormal();
            float Distance = 5000;
            bool bHit = Intersect(Direction,BodyCentre,BodyExtent,Distance);
            if (bHasHead) bHit = Intersect(Direction,Head,HeadExtent,Distance) || bHit;
            const float Depth = Distance * FVector::DotProduct(Direction,Forward);
            // A central foreground wall must win over a unit off to its side.
            if (!bHit || Depth >= SceneDepth) continue;
            if (Score < Best.Score || Depth < Best.Depth)
                Best = {Direction,Distance,float(FVector::DotProduct(BodyCentre-View.Location,Forward)),Score,FocusPart};
        }
        if (Best.Score < TNumericLimits<double>::Max()) Candidates.Add(Best);
        }
    }
    Candidates.Sort([](const FCandidate& A, const FCandidate& B)
    { return A.Score == B.Score ? A.Depth < B.Depth : A.Score < B.Score; });
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WowUnitCameraFocus),true,GetPawn());
    TArray<AActor*> Attached;
    if (GetPawn()) GetPawn()->GetAttachedActors(Attached,true,true);
    Query.AddIgnoredActors(Attached);
    for (const FCandidate& Candidate : Candidates)
    {
        const FVector End = View.Location+Candidate.Direction*Candidate.Distance;
        FHitResult Blocker;
        if ((WowCameraFocus::Trace(GetWorld(),Blocker,View.Location,End,ECC_Visibility,Query) &&
                Blocker.Distance+2 < Candidate.Distance) ||
            (WowCameraFocus::Trace(GetWorld(),Blocker,View.Location,End,ECC_Camera,Query) &&
                Blocker.Distance+2 < Candidate.Distance)) continue;
        OutDepth = Candidate.Depth;
        static double NextUnitLog = 0;
        if (FPlatformTime::Seconds() >= NextUnitLog)
        {
            NextUnitLog = FPlatformTime::Seconds() + 3.0;
            UE_LOG(LogTemp, Log, TEXT("Camera focus unit: %s at %.0f cm"), Candidate.Actor ? *Candidate.Actor->GetName() : TEXT("?"), Candidate.Depth);
        }
        return true;
    }
    return false;
}

void AWowGameplayController::UpdateHoveredModel(uint64 Guid)
{
    AActor* Actor=nullptr;
    const FWowEntity* Entity=ConnectionManager?ConnectionManager->PacketHandler.EntityManager.Find(Guid):nullptr;
    if(Guid && Entity && Entity->IsUnit())
    {
        if(Guid==ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid) Actor=GetPawn();
        else if(auto* Found=SpawnedEntityActors.Find(Guid)) Actor=*Found;
    }
    // Herbs, ore, chests and other usable objects light up like units when pointed or looked at.
    else if(Guid && Entity && Entity->IsGameObject() && static_cast<const FWowGameObjectEntity*>(Entity)->CanInteractNow())
        if(auto* Found=SpawnedEntityActors.Find(Guid)) Actor=*Found;
    if(!IsValid(Actor) || Actor->IsHidden()) Actor=nullptr;
    if(HoverLitActor.Get()!=Actor || !Actor)
    {
        for(auto& Pair:HoverMeshChannels) if(auto* Mesh=Pair.Key.Get())
            Mesh->SetLightingChannels((Pair.Value&1)!=0,(Pair.Value&2)!=0,(Pair.Value&4)!=0);
        HoverMeshChannels.Empty();
        HoverLitActor=Actor;
    }
    // The controller itself is hidden; visible lights belong to the pawn.
    HoverModelLights.RemoveAll([this](TObjectPtr<UPointLightComponent> Light)
    {
        if(!IsValid(Light)) return true;
        if(Light->GetOwner()!=GetPawn()) { Light->DestroyComponent(); return true; }
        return false;
    });
    for(auto Light:HoverModelLights) Light->SetVisibility(Actor!=nullptr);
    if(!Actor || !GetPawn()) return;
    // Channel 2 isolates this cosmetic highlight from the ground and other units.
    // Preserve channel membership and restore it on leave, despawn or logout.
    TArray<AActor*> Parts; Actor->GetAttachedActors(Parts,true,true); Parts.Add(Actor);
    for(AActor* Part:Parts)
    {
        if(!IsValid(Part)) continue;
        TInlineComponentArray<UMeshComponent*> Meshes(Part);
        for(UMeshComponent* Mesh:Meshes)
        {
            if(!Mesh->IsVisible() || Mesh->GetNumMaterials()==0 ||
                Mesh->ComponentHasTag(TEXT("WowTargetRing")) ||
                Mesh->ComponentHasTag(TEXT("WowEquipmentEffectGeometry")) ||
                Cast<UWidgetComponent>(Mesh)) continue;
            if(!HoverMeshChannels.Contains(Mesh))
            {
                const auto Channels=Mesh->LightingChannels;
                HoverMeshChannels.Add(Mesh,(Channels.bChannel0?1:0)|(Channels.bChannel1?2:0)|(Channels.bChannel2?4:0));
                Mesh->SetLightingChannels(Channels.bChannel0,Channels.bChannel1,true);
            }
        }
    }
    if(HoverModelLights.IsEmpty())
    {
        for(int32 I=0;I<2;++I)
        {
            auto* Light=NewObject<UPointLightComponent>(GetPawn());
            GetPawn()->AddInstanceComponent(Light);
            Light->SetMobility(EComponentMobility::Movable);
            Light->SetLightingChannels(false,false,true);
            Light->SetCastShadows(false);
            // Lumen/reflection lighting is scene-wide and does not isolate
            // recipients by direct-light channels. Hover must not enter either.
            Light->bAffectGlobalIllumination=false;
            Light->bAffectReflection=false;
            Light->SetIndirectLightingIntensity(0);
            Light->SetVolumetricScatteringIntensity(0);
            Light->SetSpecularScale(0);
            Light->bUseInverseSquaredFalloff=false;
            Light->SetLightFalloffExponent(2.f);
            Light->SetIntensity(1.8f);
            Light->SetLightColor(FLinearColor::White);
            Light->RegisterComponent();
            HoverModelLights.Add(Light);
        }
    }
    const FRotator Camera=PlayerCameraManager?PlayerCameraManager->GetCameraRotation():GetControlRotation();
    FVector Centre, Extent; Actor->GetActorBounds(false,Centre,Extent,true);
    const float Radius=FMath::Clamp(float(Extent.Size())*2.5f,300.f,1800.f);
    for(int32 I=0;I<HoverModelLights.Num();++I)
    {
        HoverModelLights[I]->SetAttenuationRadius(Radius);
        const FVector Offset=FRotator(-20,Camera.Yaw+(I==0?-35:35),0).Vector()*(-Radius*.25f);
        HoverModelLights[I]->SetWorldLocation(Centre+Offset);
    }
}

void AWowGameplayController::UpdateUnitHighlight(uint64 Guid, TObjectPtr<UProceduralMeshComponent>& Circle, bool bHover)
{
    AActor* Actor=nullptr;
    const FWowEntity* Entity=ConnectionManager?ConnectionManager->PacketHandler.EntityManager.Find(Guid):nullptr;
    if(Entity && Entity->IsUnit())
    {
        if(Guid==ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid) Actor=GetPawn();
        else if(auto* Found=SpawnedEntityActors.Find(Guid)) Actor=*Found;
    }
    if(!IsValid(Actor))
    {
        if(Circle) Circle->SetVisibility(false);
        return;
    }
    if(!IsValid(Circle))
    {
        UMaterial* Surface = TargetRingSurface();
        if (!Surface) { UE_LOG(LogWowGameplay, Warning, TEXT("Selection ring material unavailable")); return; }
        // PlayerControllers are hidden actors: components owned by them do not render.
        // A procedural disc whose vertices are traced onto the ground, so the ring follows
        // slopes and bumps instead of a flat plane cutting into (or floating over) the terrain.
        Circle=NewObject<UProceduralMeshComponent>(GetPawn());
        Circle->bUseAsyncCooking=false;
        Circle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Circle->SetCastShadow(false);
        Circle->bReceivesDecals=false;
        Circle->ComponentTags.Add(TEXT("WowTargetRing"));
        Circle->SetMaterial(0, UMaterialInstanceDynamic::Create(Surface, this));
        GetPawn()->AddInstanceComponent(Circle);
        Circle->RegisterComponent();
    }
    if(Circle->GetAttachParent()!=Actor->GetRootComponent())
    {
        Circle->AttachToComponent(Actor->GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform);
        Circle->SetAbsolute(false,true,true);
    }
    FVector Feet=Actor->GetActorLocation();
    float Radius=45;
    if(auto* Mesh=Actor->FindComponentByClass<USkeletalMeshComponent>())
        Radius=FMath::Clamp(FMath::Max(Mesh->Bounds.BoxExtent.X,Mesh->Bounds.BoxExtent.Y)*0.85f,35.0f,140.0f);
    // Only a root capsule places ActorLocation at the body's centre. NPC roots
    // already sit at ground level; their offset click capsule is not a feet origin.
    if(auto* Capsule=Cast<UCapsuleComponent>(Actor->GetRootComponent())) Feet.Z-=Capsule->GetScaledCapsuleHalfHeight();
    // A corpse lies beside its standing origin: ring the posed body instead.
    FBox CorpseBox;
    if((DeadEntityGuids.Contains(Guid) || FeignDeadEntityGuids.Contains(Guid)) && GetPosedBoneBox(Actor, CorpseBox))
    {
        Feet.X=CorpseBox.GetCenter().X; Feet.Y=CorpseBox.GetCenter().Y;
        Radius=FMath::Clamp(FMath::Max(CorpseBox.GetExtent().X,CorpseBox.GetExtent().Y)+15.f,35.0f,160.0f);
    }
    FCollisionQueryParams Params(SCENE_QUERY_STAT(TargetRingGround),true,Actor);
    auto GroundZ=[&](const FVector& At, float Fallback)
    {
        FHitResult Hit;
        return GetWorld()->LineTraceSingleByObjectType(Hit,At+FVector(0,0,160),At-FVector(0,0,500),
            FCollisionObjectQueryParams(ECC_WorldStatic),Params) ? float(Hit.ImpactPoint.Z) : Fallback;
    };
    Feet.Z=GroundZ(Feet,Feet.Z);
    Circle->SetWorldLocationAndRotation(Feet,FRotator::ZeroRotator);
    Circle->SetWorldScale3D(FVector::OneVector);
    if(Circle->GetNumSections()==0 || !Feet.Equals(RingBuiltFeet,4.f) || !FMath::IsNearlyEqual(Radius,RingBuiltRadius,1.f))
    {
        RingBuiltFeet=Feet; RingBuiltRadius=Radius;
        // Polar grid in the ring material's UV space (rim at 0.86-0.98 of the radius).
        static const float Rings[]={0.f,.4f,.7f,.82f,.88f,.94f,1.f};
        constexpr int32 Segments=72, NumRings=UE_ARRAY_COUNT(Rings); // r368: 72 (was 40) - a rounder rim on large rings
        TArray<FVector> Vertices; TArray<int32> Triangles; TArray<FVector> Normals; TArray<FVector2D> UVs;
        TArray<FColor> Colors; TArray<FProcMeshTangent> Tangents;
        for(int32 R=0;R<NumRings;++R)
            for(int32 S=0;S<(R==0?1:Segments);++S)
            {
                const float A=2.f*PI*S/Segments;
                const FVector2D Dir(FMath::Cos(A),FMath::Sin(A));
                const FVector Local(Dir.X*Rings[R]*Radius,Dir.Y*Rings[R]*Radius,0.f);
                // Ground only: start just above the feet (no table tops or ceilings), skip
                // walls and crate sides (steep hits) and keep bumps within a third of the radius,
                // so the ring stays flat on the floor beside boxes instead of standing up.
                float Z=Feet.Z;
                if(R>0)
                {
                    FHitResult Hit;
                    const FVector At=Feet+Local;
                    const float Limit=FMath::Max(15.f,Radius*.33f);
                    if(GetWorld()->LineTraceSingleByObjectType(Hit,At+FVector(0,0,Limit),At-FVector(0,0,Limit*2.f),
                        FCollisionObjectQueryParams(ECC_WorldStatic),Params) && Hit.ImpactNormal.Z>.6f)
                        Z=FMath::Clamp(float(Hit.ImpactPoint.Z),Feet.Z-Limit,Feet.Z+Limit);
                }
                Vertices.Add(FVector(Local.X,Local.Y,Z-Feet.Z+3.f)); // 3 cm above the ground
                UVs.Add(FVector2D(.5f+Dir.X*Rings[R]*.5f,.5f+Dir.Y*Rings[R]*.5f));
                Normals.Add(FVector::UpVector);
            }
        auto Index=[](int32 R,int32 S){return R==0?0:1+(R-1)*Segments+(S%Segments);};
        // r368 (Stefan 16.10: the ring around a corpse had a jagged, stepped edge): each ground sample
        // stood on its own, so a rock, a bone or a tuft of collision under one sample lifted that
        // vertex up to a third of the radius and the rim showed teeth. The rings now rise no steeper
        // than ~35 degrees from the ring inside them, and the heights are smoothed along each ring.
        for(int32 R=1;R<NumRings;++R)
        {
            const float MaxRise=FMath::Max(2.f,(Rings[R]-Rings[R-1])*Radius*.7f);
            for(int32 S=0;S<Segments;++S)
            {
                const float Inner=Vertices[Index(R-1,S)].Z;
                FVector& V=Vertices[Index(R,S)];
                V.Z=FMath::Clamp(V.Z,Inner-MaxRise,Inner+MaxRise);
            }
        }
        for(int32 Pass=0;Pass<2;++Pass)
            for(int32 R=1;R<NumRings;++R)
            {
                TArray<float,TInlineAllocator<Segments>> Smoothed;
                for(int32 S=0;S<Segments;++S)
                    Smoothed.Add(.25f*Vertices[Index(R,S+Segments-1)].Z+.5f*Vertices[Index(R,S)].Z+.25f*Vertices[Index(R,S+1)].Z);
                for(int32 S=0;S<Segments;++S) Vertices[Index(R,S)].Z=Smoothed[S];
            }
        for(int32 R=1;R<NumRings;++R)
            for(int32 S=0;S<Segments;++S)
            {
                if(R==1){ Triangles.Append({0,Index(1,S+1),Index(1,S)}); continue; }
                const int32 A0=Index(R-1,S),A1=Index(R-1,S+1),B0=Index(R,S),B1=Index(R,S+1);
                Triangles.Append({A0,A1,B1}); Triangles.Append({A0,B1,B0});
            }
        Circle->CreateMeshSection(0,Vertices,Triangles,Normals,UVs,Colors,Tangents,false);
        if(UMaterial* Surface=TargetRingSurface())
            if(!Cast<UMaterialInstanceDynamic>(Circle->GetMaterial(0))) Circle->SetMaterial(0,UMaterialInstanceDynamic::Create(Surface,this));
    }
    FLinearColor RingColor=WorldUnitColor(*Entity) * (bHover ? 0.65f : 1.0f);
    // r472 (Stefan: "when I right-click to attack a target, it should blink more red and its usual
    // colour, fading smoothly in and out"): while auto-attacking this target the ring breathes
    // between its own colour and a strong red, once every 2 seconds (r316, Stefan 14.10: blink slower).
    if(!bHover && bIsAutoAttacking && Guid==TargetGuid)
    {
        const float Pulse=0.5f-0.5f*FMath::Cos(float(FMath::Fmod(FPlatformTime::Seconds(),2.0)/2.0)*2.f*PI);
        RingColor=FMath::Lerp(RingColor,FLinearColor(1.f,0.05f,0.02f),Pulse);
    }
    if(auto* M=Cast<UMaterialInstanceDynamic>(Circle->GetMaterial(0))) M->SetVectorParameterValue(TEXT("RingColor"),RingColor);
    Circle->SetVisibility(true);
}

void AWowGameplayController::CreatePartyFrame()
{
	if (!ConnectionManager || PartyFrameWidget.IsValid())
	{
		return;
	}

	// Create the party frame widget
	PartyFrameWidget = SNew(SWowPartyFrame)
		.ConnectionManager(ConnectionManager);

	// Add to viewport with proper positioning container
	if (GEngine && GEngine->GameViewport)
	{
		// Wrap in positioned overlay that passes clicks through
		TSharedRef<SWidget> PartyFrameContainer =
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible) // Pass clicks through to 3D world
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)   // Top left positioning
			.VAlign(VAlign_Top)
			.Padding(20, 100, 0, 0)  // 20px from left, 100px from top (below minimap area)
			[
				PartyFrameWidget.ToSharedRef()
			];

		FWowUiSurfaces::Add(
			PartyFrameContainer, EWowUiZone::UnitFrames,
			50 // Z-order
		);
	}

	UE_LOG(LogWowGameplay, Log, TEXT("Created party frame widget"));
}

void AWowGameplayController::OnGroupUpdated()
{
	if (!ConnectionManager)
	{
		return;
	}

	// The 3.3.5 party frames are FrameXML's PartyMemberFrame1-4 (PartyFrame.xml /
	// PartyMemberFrame.lua). They rebuild from GetNumPartyMembers / UnitName("partyN") on
	// these events; the old native placeholder frame is no longer shown.
	if (PartyFrameWidget.IsValid()) PartyFrameWidget->SetVisibility(EVisibility::Collapsed);
	// SMSG_GROUP_LIST carries every member's name: UnitName("partyN") works out of range too.
	for (const FWowGroupMember& Member : ConnectionManager->PacketHandler.GroupInfo.Members)
		if (Member.Guid && !Member.Name.IsEmpty() && !ConnectionManager->PacketHandler.PlayerNameCache.Contains(Member.Guid))
			ConnectionManager->PacketHandler.PlayerNameCache.Add(Member.Guid, Member.Name);
	FireUIEvent(TEXT("PARTY_MEMBERS_CHANGED"));
	FireUIEvent(TEXT("PARTY_LEADER_CHANGED"));
	FireUIEvent(TEXT("PARTY_LOOT_METHOD_CHANGED"));
	for (int32 Index = 1; Index <= 4; ++Index)
	{
		const FString Unit = FString::Printf(TEXT("party%d"), Index);
		for (const TCHAR* Event : {TEXT("UNIT_NAME_UPDATE"), TEXT("UNIT_HEALTH"), TEXT("UNIT_MAXHEALTH"), TEXT("UNIT_DISPLAYPOWER"),
			TEXT("UNIT_POWER"), TEXT("UNIT_MAXPOWER"), TEXT("UNIT_LEVEL"), TEXT("UNIT_PORTRAIT_UPDATE"), TEXT("UNIT_AURA")})
			FireUIEvent(Event, {Unit});
	}

	if (ConnectionManager->PacketHandler.GroupInfo.IsRaidGroup())
	{
		FireUIEvent(TEXT("RAID_ROSTER_UPDATE"));

		if (ConnectionManager->PacketHandler.GroupInfo.GroupGuid != LastRaidGroupGuid)
		{
			LastRaidGroupGuid = ConnectionManager->PacketHandler.GroupInfo.GroupGuid;
			bRaidIconsRequested = false;
		}

		if (!bRaidIconsRequested)
		{
			ConnectionManager->SendRequestRaidTargetIcons();
			bRaidIconsRequested = true;
		}
	}
	else
	{
		bRaidIconsRequested = false;
		LastRaidGroupGuid = 0;
	}

	UE_LOG(LogWowGameplay, Log, TEXT("Group updated"));
}

void AWowGameplayController::OnRaidTargetsUpdated(const FWowRaidTargetState& RaidTargets)
{
	static_cast<void>(RaidTargets);
	if (!ConnectionManager || !PartyFrameWidget.IsValid())
	{
		return;
	}

	PartyFrameWidget->UpdatePartyInfo(ConnectionManager->PacketHandler.GroupInfo);
	UE_LOG(LogWowGameplay, Verbose, TEXT("Raid target icons updated"));
}

void AWowGameplayController::OnReadyCheckUpdated(const FWowReadyCheckState& ReadyCheck)
{
	static_cast<void>(ReadyCheck);
	if (!ConnectionManager || !PartyFrameWidget.IsValid())
	{
		return;
	}

	PartyFrameWidget->UpdatePartyInfo(ConnectionManager->PacketHandler.GroupInfo);
	UE_LOG(LogWowGameplay, Verbose, TEXT("Ready check state updated"));
}

void AWowGameplayController::CreateDuelStatusOverlay()
{
	if (DuelStatusViewportWidget.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	DuelStatusViewportWidget =
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(0.0f, 90.0f, 0.0f, 0.0f)
		[
			SAssignNew(DuelStatusBorder, SBorder)
			.Visibility(EVisibility::Collapsed)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(FLinearColor(0.08f, 0.04f, 0.04f, 0.92f))
			.Padding(FMargin(12.0f, 8.0f))
			[
				SAssignNew(DuelStatusText, STextBlock)
				.ColorAndOpacity(FLinearColor::White)
				.Font(FAppStyle::GetFontStyle("NormalFont"))
				.Justification(ETextJustify::Center)
			]
		];

	FWowUiSurfaces::Add(DuelStatusViewportWidget.ToSharedRef(), EWowUiZone::Popups, 190);
}

void AWowGameplayController::HideDuelInvite()
{
	if (DuelInviteViewportWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		FWowUiSurfaces::Remove(DuelInviteViewportWidget.ToSharedRef());
	}

	DuelInviteViewportWidget.Reset();
	DuelInviteWidget.Reset();
}

FString AWowGameplayController::GetPlayerDisplayName(uint64 Guid) const
{
	if (!ConnectionManager || Guid == 0)
	{
		return TEXT("");
	}

	const uint64 LocalPlayerGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	if (Guid == LocalPlayerGuid)
	{
		const FString LocalPlayerName = ConnectionManager->GetCharacterName();
		if (!LocalPlayerName.IsEmpty())
		{
			return LocalPlayerName;
		}
	}

	if (const FString* CachedName = ConnectionManager->PacketHandler.PlayerNameCache.Find(Guid))
	{
		return *CachedName;
	}

    if (const auto* Entity=ConnectionManager->PacketHandler.EntityManager.Find(Guid))
        if (!Entity->IsPlayer())
        {
            if (const auto* Name=ConnectionManager->PacketHandler.CreatureNameCache.Find(Entity->Entry)) return *Name;
            return TEXT("Unknown creature");
        }
    return TEXT("Unknown player");
}

void AWowGameplayController::MaybeRequestDuelPlayerName(const FWowDuelState& DuelState)
{
	if (!ConnectionManager || DuelState.InitiatorGuid == 0)
	{
		return;
	}

	const uint64 LocalPlayerGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	if (DuelState.InitiatorGuid != LocalPlayerGuid
		&& !ConnectionManager->PacketHandler.PlayerNameCache.Contains(DuelState.InitiatorGuid)
		&& !PendingDuelNameQueries.Contains(DuelState.InitiatorGuid))
	{
		ConnectionManager->SendNameQuery(static_cast<int64>(DuelState.InitiatorGuid));
		PendingDuelNameQueries.Add(DuelState.InitiatorGuid);
	}
}

void AWowGameplayController::RefreshDuelUi()
{
	CreateDuelStatusOverlay();

	if (!ConnectionManager)
	{
		HideDuelInvite();
		if (DuelStatusBorder.IsValid())
		{
			DuelStatusBorder->SetVisibility(EVisibility::Collapsed);
		}
		return;
	}

	const FWowDuelState& DuelState = ConnectionManager->PacketHandler.CurrentDuel;
	MaybeRequestDuelPlayerName(DuelState);

	const uint64 LocalPlayerGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	const bool bIncomingRequest = DuelState.IsRequestPending()
		&& DuelState.InitiatorGuid != 0
		&& DuelState.InitiatorGuid != LocalPlayerGuid;

	FString OpponentName;
	if (DuelState.bHasWinner)
	{
		const FString LocalPlayerName = ConnectionManager->GetCharacterName();
		if (!LocalPlayerName.IsEmpty())
		{
			if (DuelState.WinnerName == LocalPlayerName)
			{
				OpponentName = DuelState.LoserName;
			}
			else if (DuelState.LoserName == LocalPlayerName)
			{
				OpponentName = DuelState.WinnerName;
			}
		}
	}

	if (OpponentName.IsEmpty())
	{
		if (DuelState.InitiatorGuid != 0 && DuelState.InitiatorGuid != LocalPlayerGuid)
		{
			OpponentName = GetPlayerDisplayName(DuelState.InitiatorGuid);
		}
		else if (TargetGuid != 0 && TargetGuid != LocalPlayerGuid)
		{
			OpponentName = GetPlayerDisplayName(TargetGuid);
		}
	}

	if (bIncomingRequest)
	{
		if (!DuelInviteWidget.IsValid() && GEngine && GEngine->GameViewport)
		{
			DuelInviteWidget = SNew(SWowDuelInvite)
				.ConnectionManager(ConnectionManager)
				.ChallengerName(OpponentName.IsEmpty() ? TEXT("Unknown player") : OpponentName)
				.ArbiterGuid(static_cast<int64>(DuelState.ArbiterGuid))
				.OnClosed(FSimpleDelegate::CreateUObject(this, &AWowGameplayController::HideDuelInvite));

			DuelInviteViewportWidget =
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.001f))
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					DuelInviteWidget.ToSharedRef()
				];

			FWowUiSurfaces::Add(DuelInviteViewportWidget.ToSharedRef(), EWowUiZone::Popups, 210);
		}
	}
	else
	{
		HideDuelInvite();
	}

	if (!DuelStatusBorder.IsValid() || !DuelStatusText.IsValid())
	{
		return;
	}

	FString StatusMessage;
	if (DuelState.IsRequestPending() && DuelState.InitiatorGuid == LocalPlayerGuid)
	{
		StatusMessage = OpponentName.IsEmpty()
			? TEXT("Duel request sent.")
			: FString::Printf(TEXT("Duel request sent to %s."), *OpponentName);
	}
	else if (DuelState.IsCountdownActive())
	{
		const int32 SecondsRemaining = FMath::Max(1, FMath::CeilToInt(DuelState.GetCountdownRemainingSeconds()));
		StatusMessage = OpponentName.IsEmpty()
			? FString::Printf(TEXT("Duel begins in %d."), SecondsRemaining)
			: FString::Printf(TEXT("Duel with %s begins in %d."), *OpponentName, SecondsRemaining);
	}
	else if (DuelState.IsInProgress())
	{
		StatusMessage = DuelState.bInBounds
			? (OpponentName.IsEmpty() ? TEXT("Duel in progress.") : FString::Printf(TEXT("Dueling %s."), *OpponentName))
			: TEXT("Return to the duel area.");
	}
	else if (DuelState.ShouldShowCompletionText())
	{
		if (DuelState.bHasWinner)
		{
			const FString LocalPlayerName = ConnectionManager->GetCharacterName();
			if (!LocalPlayerName.IsEmpty() && DuelState.WinnerName == LocalPlayerName)
			{
				StatusMessage = DuelState.ResultReason == WowDuelResultReason::FLED
					? FString::Printf(TEXT("You win. %s fled the duel."), *DuelState.LoserName)
					: FString::Printf(TEXT("You defeat %s in a duel."), *DuelState.LoserName);
			}
			else if (!LocalPlayerName.IsEmpty() && DuelState.LoserName == LocalPlayerName)
			{
				StatusMessage = DuelState.ResultReason == WowDuelResultReason::FLED
					? FString::Printf(TEXT("You fled the duel with %s."), *DuelState.WinnerName)
					: FString::Printf(TEXT("%s defeats you in a duel."), *DuelState.WinnerName);
			}
			else
			{
				StatusMessage = DuelState.ResultReason == WowDuelResultReason::FLED
					? FString::Printf(TEXT("%s wins the duel. %s fled."), *DuelState.WinnerName, *DuelState.LoserName)
					: FString::Printf(TEXT("%s defeats %s in a duel."), *DuelState.WinnerName, *DuelState.LoserName);
			}
		}
		else if (DuelState.bInterrupted)
		{
			StatusMessage = TEXT("Duel canceled.");
		}
		else
		{
			StatusMessage = TEXT("Duel complete.");
		}
	}

	const bool bShowStatus = !StatusMessage.IsEmpty();
	DuelStatusText->SetText(FText::FromString(StatusMessage));
	DuelStatusBorder->SetVisibility(bShowStatus ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
}

void AWowGameplayController::MaybeApplyDebugDuelPreview()
{
	if (bDebugDuelPreviewApplied || DebugDuelPreviewMode.IsEmpty() || !ConnectionManager || !GetWorld())
	{
		return;
	}

	if (GetWorld()->GetTimeSeconds() < 5.0f)
	{
		return;
	}

	if (ConnectionManager->PacketHandler.CurrentDuel.Phase != EWowDuelPhase::None)
	{
		return;
	}

	const uint64 PreviewInitiatorGuid = 0x1122334455667788ULL;
	ConnectionManager->PacketHandler.PlayerNameCache.FindOrAdd(PreviewInitiatorGuid) = TEXT("DuelPreview");

	FWowDuelState& DuelState = ConnectionManager->PacketHandler.CurrentDuel;
	DuelState.BeginRequest(0x8877665544332211ULL, PreviewInitiatorGuid);

	if (DebugDuelPreviewMode.Equals(TEXT("countdown"), ESearchCase::IgnoreCase))
	{
		DuelState.BeginCountdown(5.0);
	}
	else if (DebugDuelPreviewMode.Equals(TEXT("winner"), ESearchCase::IgnoreCase))
	{
		DuelState.SetWinner(WowDuelResultReason::WON,
			ConnectionManager->GetCharacterName().IsEmpty() ? TEXT("Player") : ConnectionManager->GetCharacterName(),
			TEXT("DuelPreview"));
	}
	else if (DebugDuelPreviewMode.Equals(TEXT("forfeit"), ESearchCase::IgnoreCase))
	{
		DuelState.SetWinner(WowDuelResultReason::FLED, TEXT("DuelPreview"),
			ConnectionManager->GetCharacterName().IsEmpty() ? TEXT("Player") : ConnectionManager->GetCharacterName());
	}

	ConnectionManager->PacketHandler.OnDuelUpdated.Broadcast(DuelState);
	bDebugDuelPreviewApplied = true;

	UE_LOG(LogWowGameplay, Log, TEXT("Applied debug duel preview mode '%s'"), *DebugDuelPreviewMode);
}

void AWowGameplayController::OnDuelUpdated(const FWowDuelState& DuelState)
{
	RefreshDuelUi();

	if (!ChatWindow.IsValid())
	{
		return;
	}

	const uint64 LocalPlayerGuid = ConnectionManager
		? ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid
		: 0;
	const FString ChallengerName = GetPlayerDisplayName(DuelState.InitiatorGuid);

	if (DuelState.IsRequestPending())
	{
		if (DuelState.InitiatorGuid == LocalPlayerGuid)
		{
			ChatWindow->AddCombatMessage(TEXT("Duel request sent."), FLinearColor::Yellow);
		}
		else
		{
			ChatWindow->AddCombatMessage(
				FString::Printf(TEXT("%s has challenged you to a duel."), *ChallengerName),
				FLinearColor(1.0f, 0.75f, 0.2f));
		}
	}
	else if (DuelState.IsCountdownActive())
	{
		ChatWindow->AddCombatMessage(TEXT("Duel countdown started."), FLinearColor(1.0f, 0.75f, 0.2f));
	}
	else if (DuelState.IsInProgress() && !DuelState.bInBounds)
	{
		ChatWindow->AddCombatMessage(TEXT("You have left the duel area."), FLinearColor::Red);
	}
	else if (DuelState.IsInProgress() && DuelState.bInBounds)
	{
		ChatWindow->AddCombatMessage(TEXT("Duel in progress."), FLinearColor::Yellow);
	}
	else if (DuelState.bHasWinner)
	{
		const FString ResultMessage = DuelState.ResultReason == WowDuelResultReason::FLED
			? FString::Printf(TEXT("%s wins the duel after %s fled."), *DuelState.WinnerName, *DuelState.LoserName)
			: FString::Printf(TEXT("%s defeats %s in a duel."), *DuelState.WinnerName, *DuelState.LoserName);
		ChatWindow->AddCombatMessage(ResultMessage, FLinearColor::Yellow);
	}
	else if (DuelState.IsComplete() && DuelState.bInterrupted)
	{
		ChatWindow->AddCombatMessage(TEXT("Duel canceled."), FLinearColor::Yellow);
	}
}

void AWowGameplayController::OnGroupInviteReceived(const FString& InviterName)
{
	UE_LOG(LogWowGameplay, Log, TEXT("Group invite received from %s"), *InviterName);

	// Close any existing invite dialog
	if (PartyInviteWidget.IsValid())
	{
		// For party invites, the widget manages its own lifecycle and will auto-remove
		PartyInviteWidget.Reset();
	}

	// Create and show new invite dialog
	PartyInviteWidget = SNew(SWowPartyInvite)
		.ConnectionManager(ConnectionManager)
		.InviterName(InviterName);

	if (GEngine && GEngine->GameViewport)
	{
		// StaticPopup1 position (top centre, 140 down) like the logout/exit and destroy
		// dialogs, and above the FrameXML/native UI layers so Accept/Decline get the click
		// (at z 100 the buttons sat under the UI root and never received it).
		static TSharedPtr<SWidget> GPartyInviteContainer;
		if (GPartyInviteContainer.IsValid())
			FWowUiSurfaces::Remove(GPartyInviteContainer.ToSharedRef());
		GPartyInviteContainer =
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible) // only the dialog takes clicks
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Top)
			.Padding(0, 140, 0, 0)
			[
				PartyInviteWidget.ToSharedRef()
			];
		FWowUiSurfaces::Add(GPartyInviteContainer.ToSharedRef(), EWowUiZone::Popups, 215);
	}
}

void AWowGameplayController::OnPartyCommandResult(uint8 Command, const FString& PlayerName, uint8 Result)
{
	// Log the result and potentially show a message
	const TCHAR* CommandName = TEXT("Unknown");
	switch (Command)
	{
	case 0: CommandName = TEXT("Invite"); break;
	case 1: CommandName = TEXT("Leave"); break;
	case 2: CommandName = TEXT("Remove"); break;
	default: break;
	}

	const TCHAR* ResultName = TEXT("Unknown");
	switch (Result)
	{
	case 0: ResultName = TEXT("Success"); break;
	case 1: ResultName = TEXT("Cannot find player"); break;
	case 2: ResultName = TEXT("Player not in your group"); break;
	case 3: ResultName = TEXT("Player already in group"); break;
	default: break;
	}

	UE_LOG(LogWowGameplay, Log, TEXT("Party command result: %s for %s - %s"),
		CommandName, *PlayerName, ResultName);

	// Add message to chat window
	if (ChatWindow.IsValid())
	{
		FString Message = FString::Printf(TEXT("%s %s: %s"), CommandName, *PlayerName, ResultName);
		ChatWindow->AddCombatMessage(Message, FLinearColor::Yellow);
	}
}

void AWowGameplayController::OnTaxiNodesShown(uint64 NpcGuid, uint32 CurrentNodeId)
{
	UE_LOG(LogWowGameplay, Log, TEXT("Taxi nodes shown by NPC %llu, current node: %d"),
		NpcGuid, CurrentNodeId);

	// The flight master window is the world map in taxi mode (continent art, discovered flight
	// points, route + price on hover, close button top right); the old SWowTaxiMap overlay drew
	// every node of map 0 as grey/green boxes over the world (Stefan 10.10).
	if (!ConnectionManager || !GameUI || !UIManager || !CachedMpq) return;
	if (!WorldMapWidget.IsValid())
	{
		WorldMapWidget = SNew(SWowWorldMap, UIManager, CachedMpq, &ConnectionManager->PacketHandler.EntityManager, [this]() { return LastUiZoneId; });
		WorldMapWidget->SetQuestSource(&ConnectionManager->PacketHandler);
		GameUI->AddManagedPanel(TEXT("NativeWorldMap"), WorldMapWidget.ToSharedRef(), FVector2D(830, 600));
	}
	TWeakObjectPtr<UWowConnectionManager> WeakConnection(ConnectionManager.Get());
	WorldMapWidget->ShowTaxi(ConnectionManager->PacketHandler.TaxiData, [WeakConnection, NpcGuid](const TArray<uint32>& Route)
	{
		UWowConnectionManager* Connection = WeakConnection.Get();
		if (!Connection || Route.Num() < 2) return;
		// CMSG_ACTIVATETAXIEXPRESS: npc guid, node count, the nodes (current first). One packet for
		// direct and multi-hop flights, like the client (ACTIVATETAXI only takes a direct path).
		TArray<uint8> Data;
		auto Put32 = [&Data](uint32 Value) { for (int32 Byte = 0; Byte < 4; ++Byte) Data.Add(uint8(Value >> (8 * Byte))); };
		for (int32 Byte = 0; Byte < 8; ++Byte) Data.Add(uint8(NpcGuid >> (8 * Byte)));
		Put32(uint32(Route.Num()));
		for (uint32 Node : Route) Put32(Node);
		UE_LOG(LogWowGameplay, Log, TEXT("Taxi: flying route of %d nodes (%u -> %u)"), Route.Num(), Route[0], Route.Last());
		Connection->SendRawPacket(WowOpcode::CMSG_ACTIVATETAXIEXPRESS, Data);
	});
}

void AWowGameplayController::OnTaxiActivateReply(uint8 Result)
{
	const TCHAR* ResultName = TEXT("Unknown");
	switch (Result)
	{
	case 0: ResultName = TEXT("Success"); break;
	case 1: ResultName = TEXT("Unspecified error"); break;
	case 2: ResultName = TEXT("No such path"); break;
	case 3: ResultName = TEXT("Not enough money"); break;
	case 4: ResultName = TEXT("Too far away"); break;
	case 5: ResultName = TEXT("No vendor nearby"); break;
	default: break;
	}

	UE_LOG(LogWowGameplay, Log, TEXT("Taxi activate result: %s"), ResultName);

	if (Result != 0)
	{
		// Show error message
		if (ChatWindow.IsValid())
		{
			FString Message = FString::Printf(TEXT("Cannot use taxi: %s"), ResultName);
			ChatWindow->AddCombatMessage(Message, FLinearColor::Red);
		}
	}
	else
	{
		// Success - player should start flying
		// Close the flight master map
		if (WorldMapWidget.IsValid() && WorldMapWidget->IsTaxiOpen())
		{
			WorldMapWidget->CloseTaxi();
			WorldMapWidget->SetVisibility(EVisibility::Hidden);
		}

		// Add flight message
		if (ChatWindow.IsValid())
		{
			ChatWindow->AddCombatMessage(TEXT("You are now on a flight path"), FLinearColor::Green);
		}
	}
}

void AWowGameplayController::OnToggleMap()
{
    if(!ConnectionManager || !GameUI || !UIManager || !CachedMpq)return;
    if(!WorldMapWidget.IsValid())
    {
        WorldMapWidget=SNew(SWowWorldMap,UIManager,CachedMpq,&ConnectionManager->PacketHandler.EntityManager,[this](){return LastUiZoneId;});
        WorldMapWidget->SetQuestSource(&ConnectionManager->PacketHandler);
        GameUI->AddManagedPanel(TEXT("NativeWorldMap"),WorldMapWidget.ToSharedRef(),FVector2D(830,600));
    }
    WorldMapWidget->Toggle();
}

void AWowGameplayController::CastSpellFromSlot(int32 SlotIndex)
{
	if (!ConnectionManager || !ConnectionManager->PacketHandler.ActionButtons.IsValidIndex(SlotIndex))
	{
		return;
	}

	uint32 PackedAction = ConnectionManager->PacketHandler.ActionButtons[SlotIndex];
	uint32 ActionId = PackedAction & 0x00FFFFFF;
	uint8 ActionType = (PackedAction >> 24) & 0xFF;

	UE_LOG(LogWowGameplay, Log, TEXT("CastSpellFromSlot: slot=%d packed=0x%08X actionId=%u type=%u"),
		SlotIndex, PackedAction, ActionId, ActionType);

	if (ActionId == 0) return;

	if (ConnectionManager->UseActionSlot(SlotIndex))
	{
		FireUIEvent(TEXT("ACTIONBAR_UPDATE_STATE"));
	}
	else if (ActionType == 128) // Item
	{
		UE_LOG(LogWowGameplay, Log, TEXT("CastSpellFromSlot: item use not implemented (item=%u)"), ActionId);
	}
	else if (ActionType == 64) // Macro: run its lines (RunMacro, LuaMacroApi.cpp)
	{
		if (UIManager && UIManager->GetEventSystem())
			UIManager->GetEventSystem()->ExecuteLuaString(FString::Printf(TEXT("if RunMacro then RunMacro(%u) end"), ActionId), TEXT("=ActionMacro"));
	}
}

void AWowGameplayController::OnPlayerNameReceived(uint64 Guid, const FString& Name)
{
	UE_LOG(LogWowGameplay, Log, TEXT("Received player name: GUID=%llu Name=%s"), Guid, *Name);
	if (!SpawnedEntityActors.Contains(Guid))
	{
		QueueEntityModelSpawn(Guid);
	}
	if (TObjectPtr<UWidgetComponent>* Component = EntityNameplates.Find(Guid); Component && *Component)
	{
		if (UWowNameplateWidget* Widget = Cast<UWowNameplateWidget>((*Component)->GetUserWidgetObject()))
		{
			Widget->UpdateName(Name);
		}
	}
	if (Guid == TargetGuid)
	{
		FireUIEvent(TEXT("UNIT_NAME_UPDATE"), {TEXT("target")});
	}
	if (ConnectionManager &&
		Guid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
	{
		FireUIEvent(TEXT("UNIT_NAME_UPDATE"), {TEXT("player")});
		if (UIManager)
		{
			UIManager->UpdateInventory();
		}
	}
	PendingDuelNameQueries.Remove(Guid);
	if (DuelInviteWidget.IsValid())
	{
		HideDuelInvite();
	}
	RefreshDuelUi();
}

void AWowGameplayController::OnCreatureNameReceived(uint32 Entry, const FString& Name, const FString& Title)
{
	UE_LOG(LogWowGameplay, Log, TEXT("Received creature name: Entry=%u Name=%s Title=%s"), Entry, *Name, *Title);
	if (!ConnectionManager) return;
	for (const auto& Pair : ConnectionManager->PacketHandler.EntityManager.GetAll())
	{
		const FWowEntity* Entity = Pair.Value.Get();
		if (!Entity || Entity->Entry != Entry) continue;
		if (!SpawnedEntityActors.Contains(Entity->Guid) && Entity->IsUnit())
		{
			QueueEntityModelSpawn(Entity->Guid);
		}
		if (TObjectPtr<UWidgetComponent>* Component = EntityNameplates.Find(Entity->Guid); Component && *Component)
		{
			if (UWowNameplateWidget* Widget = Cast<UWowNameplateWidget>((*Component)->GetUserWidgetObject()))
			{
				Widget->UpdateName(Name);
			}
		}
		if (Entity->Guid == TargetGuid)
		{
			FireUIEvent(TEXT("UNIT_NAME_UPDATE"), {TEXT("target")});
		}
	}
}

void AWowGameplayController::OnSpellFailure(uint64 CasterGuid, uint32 SpellId, uint8 FailureReason)
{
	if (!ConnectionManager) return;

	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	ActiveTimedCastSpells.Remove(CasterGuid);
	ActiveChannelSpells.Remove(CasterGuid);
	if (UIManager)
	{
		UIManager->EndUnitSpellProgress(CasterGuid);
		AuraUpdateTimer = AuraUpdateInterval; // nameplate cast bars refresh next tick
	}
	FireUnitSpellcastEventForGuid(TEXT("UNIT_SPELLCAST_FAILED"), CasterGuid);
	// Interrupted/failed: the cast sound stops immediately, like the original client.
	if (TArray<TWeakObjectPtr<UAudioComponent>>* Sounds = CasterSpellSounds.Find(CasterGuid))
	{
		for (const TWeakObjectPtr<UAudioComponent>& Sound : *Sounds)
		{
			if (Sound.IsValid()) Sound->Stop();
		}
		CasterSpellSounds.Remove(CasterGuid);
	}
	RestoreCasterWeaponsAfterSpell(CasterGuid);
	EndCasterSpellAnimation(CasterGuid);
	RemoveSpellVisuals(CasterGuid, true);
	RemoveCastGlowEffects(CasterGuid);

	if (CasterGuid == LocalGuid)
	{
		// Our spell cast failed
		bIsCasting = false;
		CurrentSpellId = 0;
		CastStartTime = 0.0f;
		CastDuration = 0.0f;

		UE_LOG(LogWowGameplay, Warning, TEXT("Spell %u failed with reason %u"), SpellId, FailureReason);

		bIsChanneling = false;
		SpellBarEndTime = 0.0f;
		ActiveLocalChannelCasterGuid = 0;
		HideSpellProgressBar();

        const FString Message = WowSpellCastResultText(UIManager, FailureReason);
        // Interrupts also mark the cast bar "Interrupted" like the original client.
        if (FailureReason == 40 || FailureReason == 41)
        {
            FireUnitSpellcastEventForGuid(TEXT("UNIT_SPELLCAST_INTERRUPTED"), CasterGuid);
        }
        // SPELL_FAILURE follows the CAST_FAILED that already showed the reason (or carries 0 when a
        // channel simply ended, which read "You are in combat" - Stefan 10.10): no second red line,
        // and interrupts show on the cast bar only.
        const bool bAlreadyShown = GWowLastCastErrorSpell == SpellId && FPlatformTime::Seconds() - GWowLastCastErrorTime < 1.0;
        if (!Message.IsEmpty() && FailureReason != 0 && FailureReason != 40 && FailureReason != 41 && !bAlreadyShown)
            FireUIEvent(TEXT("UI_ERROR_MESSAGE"), {Message});
        if (!Message.IsEmpty() && FailureReason != 0)
        {
            FWowCombatLogEntry Failed;
            Failed.Event = TEXT("SPELL_CAST_FAILED");
            Failed.SourceGuid = CasterGuid;
            Failed.SpellId = SpellId;
            Failed.MissType = Message;
            OnCombatLogEntry(Failed);
        }
        FString Voice;
        switch(FailureReason) {
            case 67:case 45:Voice=TEXT("_CantUseAbilityCooling");break;
            case 97:Voice=TEXT("_CantCastOutofRange");break;
            case 85:Voice=TEXT("_CantCastNoMana");break;
            case 10:case 11:Voice=TEXT("_CantCastGenericNoTarg");break;
            default:break;
        }
        const auto* LocalUnit=ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
        if(LocalUnit && GetPawn() && !Voice.IsEmpty())
            if(auto* Audio=Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(GetWorld(),AWowAudioManager::StaticClass())))
                Audio->PlayCharacterCue(LocalUnit->GetRaceId(),LocalUnit->GetGenderId(),Voice,GetPawn()->GetActorLocation(),true);
	}
	else
	{
		UE_LOG(LogWowGameplay, Log, TEXT("Entity %llu spell %u failed"), CasterGuid, SpellId);
	}
}

void AWowGameplayController::OnAttackerStateUpdate(uint64 AttackerGuid, uint64 VictimGuid, uint32 HitInfo, uint32 Damage)
{
	if (!ConnectionManager) return;

	UE_LOG(LogWowGameplay, Log, TEXT("Combat: Attacker %llu hit target %llu for %u damage (hitInfo=0x%08X)"),
		AttackerGuid, VictimGuid, Damage, HitInfo);
    if (VictimGuid != 0 && VictimGuid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
        WowAutoRetaliateOnAttacked(this, AttackerGuid); // r605

    const auto* Local=ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
    if(Local && GetPawn() && Damage>0 && (AttackerGuid==Local->Guid || VictimGuid==Local->Guid))
        if(auto* Audio=Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(GetWorld(),AWowAudioManager::StaticClass())))
            Audio->PlayCharacterCue(Local->GetRaceId(),Local->GetGenderId(),
                VictimGuid==Local->Guid ? ((HitInfo&0x200)?TEXT("PlayerWoundCrit"):TEXT("PlayerWound")) : TEXT("PlayerAttack"),GetPawn()->GetActorLocation());
	// Show floating combat text
	TObjectPtr<AActor>* TargetActorPtr = SpawnedEntityActors.Find(VictimGuid);
    AActor* DamageTarget = VictimGuid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid
        ? GetPawn() : (TargetActorPtr ? TargetActorPtr->Get() : nullptr);
    if (DamageTarget && Damage > 0)
	{
		FWowFloatingTextInfo TextInfo;
		TextInfo.Text = FString::Printf(TEXT("%u"), Damage);
		TextInfo.Duration = 2.0f;
		TextInfo.Speed = 100.0f;
		TextInfo.FontSize = 32.0f;

		// Color based on hit info (simplified)
		const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
		if (AttackerGuid == LocalGuid)
		{
			// Our damage - white for physical, colored for schools
			TextInfo.Color = FLinearColor::White;
		}
		else if (VictimGuid == LocalGuid)
		{
			// Damage to us - red
			TextInfo.Color = FLinearColor::Red;
		}
		else
		{
			// Other damage - gray
			TextInfo.Color = FLinearColor(0.7f, 0.7f, 0.7f, 1.0f);
		}

		// HITINFO_CRITICALHIT in build 12340 (0x2 is AFFECTS_VICTIM).
		if (HitInfo & 0x200)
		{
			TextInfo.Text += TEXT("!");
			TextInfo.Color = FLinearColor::Yellow;
			TextInfo.FontSize = 40.0f;
		}

		UWowFloatingText::SpawnFloatingText(DamageTarget, TextInfo);

		UE_LOG(LogWowGameplay, Log, TEXT("Spawned floating damage text: %s"), *TextInfo.Text);
	}

	// Play attack animation on the attacker
	const uint64 LocalGuid2 = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	if (AttackerGuid == LocalGuid2)
	{
		// Local player attack animation
		if (AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(GetPawn()))
		{
			if (UWowAnimationController* AnimController = PlayerChar->AnimationController)
			{
				AnimController->PlayAttackAnimation();
			}
		}
	}
	else
	{
		// A remote player swinging draws the melee weapons (see GRemoteCombatDrawnUntil).
		if (const FWowEntity* AttackerEntity = ConnectionManager->PacketHandler.EntityManager.Find(AttackerGuid);
			AttackerEntity && AttackerEntity->IsPlayer())
		{
			const double Now = FPlatformTime::Seconds();
			const double* Until = GRemoteCombatDrawnUntil.Find(AttackerGuid);
			const bool bWasDrawn = Until && Now < *Until;
			GRemoteCombatDrawnUntil.Add(AttackerGuid, Now + RemoteCombatDrawSeconds);
			if (!bWasDrawn)
			{
				OnEntityUpdated(*AttackerEntity); // sheathed again from Tick when the window runs out
			}
		}
		// Other entity attack animation
		if (TObjectPtr<AActor>* AttackerActorPtr = SpawnedEntityActors.Find(AttackerGuid))
		{
			if (AActor* AttackerActor = AttackerActorPtr->Get())
			{
				if (UWowAnimationController* AnimController = FWowCharacterBuilder::GetAnimationController(AttackerActor))
				{
					AnimController->PlayAttackAnimation();
				}
			}
		}
	}

	// Play wound/hit reaction animation on the victim
	if (Damage > 0 && VictimGuid != LocalGuid2)
	{
		if (TObjectPtr<AActor>* VictimActorPtr = SpawnedEntityActors.Find(VictimGuid))
		{
			if (AActor* VictimActor = VictimActorPtr->Get())
			{
				if (UWowAnimationController* AnimController = FWowCharacterBuilder::GetAnimationController(VictimActor))
				{
					AnimController->PlayWoundAnimation();
				}
			}
		}
	}
	else if (Damage > 0 && VictimGuid == LocalGuid2)
	{
		// Local player hit reaction
		if (AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(GetPawn()))
		{
			if (UWowAnimationController* AnimController = PlayerChar->AnimationController)
			{
				AnimController->PlayWoundAnimation();
			}
		}
	}

	// Check if our auto-attack target died â€” stop auto-attack
	if (bIsAutoAttacking && VictimGuid == AutoAttackTargetGuid)
	{
		const FWowEntity* VictimEntity = ConnectionManager->PacketHandler.EntityManager.Find(VictimGuid);
		if (VictimEntity && VictimEntity->GetMaxHealth() > 0 && VictimEntity->GetHealth() <= 0)
		{
			StopAutoAttack();
			UE_LOG(LogWowGameplay, Log, TEXT("Auto-attack target %llu died, stopping attack"), VictimGuid);
		}
	}

	// COMBAT_LOG_EVENT(_UNFILTERED) with full arguments comes from OnCombatLogEntry (SWING_DAMAGE / SWING_MISSED).
}

FString AWowGameplayController::CombatLogUnitName(uint64 Guid)
{
	if (!Guid || !ConnectionManager) return FString();
	auto& Handler = ConnectionManager->PacketHandler;
	if (Guid == Handler.EntityManager.LocalPlayerGuid)
	{
		const FString Name = ConnectionManager->GetCharacterName();
		if (!Name.IsEmpty()) return Name;
	}
	if (const FString* Name = Handler.PlayerNameCache.Find(Guid)) return *Name;
	if (const FWowEntity* Entity = Handler.EntityManager.Find(Guid))
	{
		if (!Entity->IsPlayer())
			if (const FString* Name = Handler.CreatureNameCache.Find(Entity->Entry)) return *Name;
	}
	return FString();
}

uint32 AWowGameplayController::CombatLogUnitFlags(uint64 Guid)
{
	// COMBATLOG_OBJECT_*: affiliation 0x1 mine / 0x2 party / 0x4 raid / 0x8 outsider,
	// reaction 0x10 friendly / 0x20 neutral / 0x40 hostile, control 0x100 player / 0x200 npc,
	// type 0x400 player / 0x800 npc / 0x1000 pet / 0x2000 guardian / 0x4000 object, 0x10000 target.
	if (!Guid || !ConnectionManager) return 0x80000000u;
	auto& Handler = ConnectionManager->PacketHandler;
	const uint64 LocalGuid = Handler.EntityManager.LocalPlayerGuid;
	const FWowEntity* Entity = Handler.EntityManager.Find(Guid);
	const FWowEntity* Local = Handler.EntityManager.GetLocalPlayer();
	const uint32 High = uint32(Guid >> 48) & 0xFFF0u;
	const bool bPlayer = High == 0 || (Entity && Entity->IsPlayer());
	const bool bObject = High == 0xF110 || High == 0xF120 || (Entity && Entity->IsGameObject());
	uint64 UnitOwner = 0;
	if (Entity && Entity->IsUnit() && !bPlayer)
	{
		UnitOwner = Entity->GetField64(UnitField::SUMMONEDBY);
		if (!UnitOwner) UnitOwner = Entity->GetField64(UnitField::CHARMEDBY);
		if (!UnitOwner) UnitOwner = Entity->GetField64(UnitField::CREATEDBY);
	}
	const uint64 Controller = bPlayer ? Guid : UnitOwner;
	uint32 Flags = 0;
	if (Controller && Controller == LocalGuid) Flags |= 0x1;
	else if (Controller && Handler.GroupInfo.FindMember(Controller)) Flags |= Handler.GroupInfo.IsRaidGroup() ? 0x4 : 0x2;
	else Flags |= 0x8;
	if (Flags & 0x7) Flags |= 0x10;
	else if (Entity && Local && Entity->IsUnit())
	{
		const int32 Reaction = ConnectionManager->GetUnitReaction(Entity, Local);
		Flags |= Reaction < 4 ? 0x40 : Reaction == 4 ? 0x20 : 0x10;
	}
	else Flags |= 0x20;
	Flags |= (bPlayer || (UnitOwner && (uint32(UnitOwner >> 48) & 0xFFF0u) == 0)) ? 0x100 : 0x200;
	if (bObject) Flags |= 0x4000;
	else if (bPlayer) Flags |= 0x400;
	else if (UnitOwner) Flags |= High == 0xF140 ? 0x1000 : 0x2000;
	else Flags |= 0x800;
	if (Guid == TargetGuid) Flags |= 0x10000;
	return Flags;
}

// r699 (Stefan: "make spells or even ranged attacks hit for the mobs head when it's a critical hit"; "we can try
// and see if it's something we can actually notice"): the server tells the crit only in the damage log, which
// arrives about when the bolt lands. A crit (SPELL_DAMAGE / RANGE_DAMAGE) turns a bolt still in flight toward the
// target's head socket, moves an impact visual spawned in the last 0.6 s to the head, and makes an impact that
// is still to come use the head instead of the chest.
namespace
{
struct FWowCritKey { uint64 Caster = 0; uint64 Target = 0; uint32 Spell = 0; double Time = 0.0; };
struct FWowRecentImpact : FWowCritKey { TWeakObjectPtr<AActor> Actor; };
struct FWowFlyingMissile : FWowCritKey { TWeakObjectPtr<AWowSpellMissile> Missile; };
TArray<FWowCritKey> GWowCritHits;
TArray<FWowRecentImpact> GWowRecentImpacts;
TArray<FWowFlyingMissile> GWowFlyingMissiles;
const FName WowCritHeadSocket(TEXT("WowSpellHead"));

bool WowSameHit(const FWowCritKey& A, uint64 Caster, uint64 Target, uint32 Spell)
{
	return A.Caster == Caster && A.Target == Target && A.Spell == Spell;
}

bool WowConsumeCritHit(uint64 Caster, uint64 Target, uint32 Spell)
{
	const double Now = FPlatformTime::Seconds();
	GWowCritHits.RemoveAll([Now](const FWowCritKey& K) { return Now - K.Time > 1.5; });
	const int32 Index = GWowCritHits.IndexOfByPredicate([&](const FWowCritKey& K) { return WowSameHit(K, Caster, Target, Spell); });
	if (Index == INDEX_NONE) return false;
	GWowCritHits.RemoveAt(Index);
	return true;
}
}

void AWowGameplayController::OnCombatLogEntry(const FWowCombatLogEntry& Entry)
{
	if (Entry.bCritical && Entry.SpellId != 0 && (Entry.Event == TEXT("SPELL_DAMAGE") || Entry.Event == TEXT("RANGE_DAMAGE")))
	{
		const double Now = FPlatformTime::Seconds();
		GWowFlyingMissiles.RemoveAll([](const FWowFlyingMissile& M) { return !M.Missile.IsValid(); });
		GWowRecentImpacts.RemoveAll([Now](const FWowRecentImpact& I) { return !I.Actor.IsValid() || Now - I.Time > 0.6; });
		bool bHandled = false;
		for (const FWowFlyingMissile& Flying : GWowFlyingMissiles)
			if (WowSameHit(Flying, Entry.SourceGuid, Entry.DestGuid, Entry.SpellId))
			{
				Flying.Missile->AimAtSocket(WowCritHeadSocket);
				bHandled = true;
			}
		AActor* TargetActor = ResolveSpellActor(Entry.DestGuid);
		USkeletalMeshComponent* TargetMesh = Cast<ACharacter>(TargetActor) ? Cast<ACharacter>(TargetActor)->GetMesh()
			: TargetActor ? TargetActor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
		for (const FWowRecentImpact& Recent : GWowRecentImpacts)
			if (WowSameHit(Recent, Entry.SourceGuid, Entry.DestGuid, Entry.SpellId) && TargetMesh && TargetMesh->DoesSocketExist(WowCritHeadSocket))
			{
				Recent.Actor->AttachToComponent(TargetMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, WowCritHeadSocket);
				bHandled = true;
			}
		// No bolt in the air and no impact yet: the impact still to come goes to the head.
		if (!bHandled) GWowCritHits.Add({Entry.SourceGuid, Entry.DestGuid, Entry.SpellId, Now});
		UE_LOG(LogWowGameplay, Log, TEXT("Crit head hit: spell %u (%s) - %s"), Entry.SpellId, *Entry.Event,
			bHandled ? TEXT("bolt/impact moved to the head") : TEXT("next impact goes to the head"));
	}
	ShowCombatLogText(Entry);
	PlayCombatLogSounds(Entry);
	if (!UIManager || !UIManager->GetEventSystem()) return;

	// 3.3.5: timestamp, event, sourceGUID, sourceName, sourceFlags, destGUID, destName, destFlags, ...
	const FDateTime Now = FDateTime::UtcNow();
	TArray<FString> Args;
	Args.Reserve(20);
	Args.Add(CombatLogNumber(double(Now.ToUnixTimestamp()) + Now.GetMillisecond() / 1000.0));
	Args.Add(Entry.Event);
	auto AddUnit = [this, &Args](uint64 Guid)
	{
		Args.Add(CombatLogGuid(Guid));
		const FString Name = CombatLogUnitName(Guid);
		Args.Add(Name.IsEmpty() ? CombatLogNil() : Name);
		Args.Add(CombatLogNumber(double(CombatLogUnitFlags(Guid))));
	};
	AddUnit(Entry.SourceGuid);
	AddUnit(Entry.DestGuid);

	const FString& Event = Entry.Event;
	if (Event.StartsWith(TEXT("SPELL_")) || Event.StartsWith(TEXT("RANGE_")) || Event == TEXT("DAMAGE_SHIELD"))
	{
		const FSpellDbcEntry* Spell = FDbcStore::Get().Spells().GetById(Entry.SpellId);
		Args.Add(CombatLogNumber(Entry.SpellId));
		Args.Add(Spell && !Spell->SpellName.IsEmpty() ? Spell->SpellName : CombatLogNil());
		Args.Add(CombatLogNumber(Spell && Spell->SchoolMask ? Spell->SchoolMask : Entry.School));
	}
	else if (Event == TEXT("ENVIRONMENTAL_DAMAGE"))
	{
		Args.Add(Entry.EnvironmentalType);
	}

	if (Event.EndsWith(TEXT("_DAMAGE")) || Event == TEXT("DAMAGE_SHIELD"))
	{
		Args.Add(CombatLogNumber(Entry.Amount));
		Args.Add(CombatLogNumber(Entry.Overkill));
		Args.Add(CombatLogNumber(Entry.School));
		Args.Add(CombatLogOptional(Entry.Resisted));
		Args.Add(CombatLogOptional(Entry.Blocked));
		Args.Add(CombatLogOptional(Entry.Absorbed));
		Args.Add(CombatLogFlag(Entry.bCritical));
		Args.Add(CombatLogFlag(Entry.bGlancing));
		Args.Add(CombatLogFlag(Entry.bCrushing));
	}
	else if (Event.EndsWith(TEXT("_MISSED")))
	{
		Args.Add(Entry.MissType);
		Args.Add(CombatLogOptional(Entry.Amount));
	}
	else if (Event.EndsWith(TEXT("_HEAL")))
	{
		Args.Add(CombatLogNumber(Entry.Amount));
		Args.Add(CombatLogNumber(Entry.Overkill));
		Args.Add(CombatLogNumber(Entry.Absorbed));
		Args.Add(CombatLogFlag(Entry.bCritical));
	}
	else if (Event.EndsWith(TEXT("_ENERGIZE")))
	{
		Args.Add(CombatLogNumber(Entry.Amount));
		Args.Add(CombatLogNumber(Entry.PowerType));
	}
	else if (Event.EndsWith(TEXT("_LEECH")) || Event.EndsWith(TEXT("_DRAIN")))
	{
		Args.Add(CombatLogNumber(Entry.Amount));
		Args.Add(CombatLogNumber(Entry.PowerType));
		Args.Add(CombatLogNumber(Entry.ExtraAmount));
	}
	else if (Event.StartsWith(TEXT("SPELL_AURA_")))
	{
		Args.Add(Entry.AuraType);
		if (Event.EndsWith(TEXT("_DOSE"))) Args.Add(CombatLogNumber(Entry.Amount));
	}
	else if (Event == TEXT("SPELL_CAST_FAILED"))
	{
		Args.Add(Entry.MissType);
	}

	FWowEventSystem* EventSystem = UIManager->GetEventSystem();
	EventSystem->FireEvent(TEXT("COMBAT_LOG_EVENT_UNFILTERED"), Args);
	EventSystem->FireEvent(TEXT("COMBAT_LOG_EVENT"), Args);
}

void AWowGameplayController::ShowCombatLogText(const FWowCombatLogEntry& Entry)
{
	// Spell numbers like the original client: your spell damage yellow on the target, heals green,
	// your misses as words ("Miss", "Resist", "Immune"). Melee damage numbers come from OnAttackerStateUpdate.
	if (!ConnectionManager || Entry.Event.StartsWith(TEXT("SWING_DAMAGE"))) return;
	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	const bool bByLocal = Entry.SourceGuid == LocalGuid;
	const bool bOnLocal = Entry.DestGuid == LocalGuid;
	if (!bByLocal && !bOnLocal) return;
	const bool bDamage = Entry.Event.EndsWith(TEXT("_DAMAGE")) || Entry.Event == TEXT("DAMAGE_SHIELD");
	const bool bHeal = Entry.Event.EndsWith(TEXT("_HEAL"));
	const bool bMiss = Entry.Event.EndsWith(TEXT("_MISSED"));
	// Resource gains on you (r279, Stefan): "+25 Mana" in the power colour, sinking into you.
	const bool bEnergize = bOnLocal && Entry.Event.EndsWith(TEXT("_ENERGIZE"))
		&& FWowClientPreferences::GetWorldOption(TEXT("combattextresource"));
	if (!bDamage && !bHeal && !bMiss && !bEnergize) return;

	AActor* Actor = nullptr;
	if (bOnLocal) Actor = GetPawn();
	else if (const TObjectPtr<AActor>* Found = SpawnedEntityActors.Find(Entry.DestGuid)) Actor = Found->Get();
	if (!IsValid(Actor)) return;

	FWowFloatingTextInfo Info;
	Info.Duration = 2.0f;
	Info.Speed = 100.0f;
	Info.FontSize = 32.0f;
	if (bEnergize)
	{
		// 3.3.5 power types; rage and runic power travel in tenths.
		static const TCHAR* const PowerNames[] = { TEXT("Mana"), TEXT("Rage"), TEXT("Focus"), TEXT("Energy"), TEXT("Happiness"), TEXT("Runes"), TEXT("Runic Power") };
		static const FLinearColor PowerColors[] = { FLinearColor(0.35f, 0.55f, 1.0f), FLinearColor(1.0f, 0.15f, 0.15f), FLinearColor(1.0f, 0.5f, 0.25f),
			FLinearColor(1.0f, 1.0f, 0.1f), FLinearColor(0.0f, 1.0f, 1.0f), FLinearColor(0.5f, 0.5f, 0.5f), FLinearColor(0.0f, 0.82f, 1.0f) };
		const int32 Power = int32(Entry.PowerType);
		if (Power < 0 || Power >= int32(UE_ARRAY_COUNT(PowerNames))) return;
		const int32 Amount = (Power == 1 || Power == 6) ? Entry.Amount / 10 : Entry.Amount;
		if (Amount <= 0) return;
		Info.Text = FString::Printf(TEXT("%d"), Amount);
		Info.Prefix = TEXT("+");
		Info.Suffix = FString(TEXT(" ")) + PowerNames[Power];
		Info.Color = PowerColors[Power];
		Info.FontSize = 26.0f;
		Info.bDownward = true;
		Info.SumOption = TEXT("combattextsumheals");
		UWowFloatingText::SpawnFloatingText(Actor, Info);
		return;
	}
	if (bMiss)
	{
		if (!bByLocal) return;
		FString Word = Entry.MissType.ToLower();
		if (!Word.IsEmpty()) Word[0] = FChar::ToUpper(Word[0]);
		Info.Text = Word;
		Info.Color = FLinearColor::White;
	}
	else
	{
		if (Entry.Amount <= 0) return;
		Info.Text = FString::Printf(TEXT("%d"), Entry.Amount);
		if (bHeal) Info.Color = FLinearColor(0.1f, 1.0f, 0.1f);
		if (bHeal && bOnLocal)
		{
			// Heals you receive come in: "+123", sinking, with their own combine option (r279).
			Info.Prefix = TEXT("+");
			Info.bDownward = true;
			Info.SumOption = TEXT("combattextsumheals");
		}
		else if (bOnLocal) Info.Color = FLinearColor::Red;
		else Info.Color = FLinearColor(1.0f, 1.0f, 0.0f);
		if (Entry.bCritical)
		{
			Info.Text += TEXT("!");
			Info.FontSize = 40.0f;
		}
	}
	UWowFloatingText::SpawnFloatingText(Actor, Info);
}


namespace
{
// 3.3.5 ChatTypeInfo defaults (FrameXML ChatConfigFrame / Blizzard defaults).
struct FWowChatStyle { const TCHAR* Event; float R, G, B; };
FWowChatStyle WowChatStyleFor(uint8 Type)
{
    switch (Type)
    {
    case 0x00: return { TEXT("CHAT_MSG_SYSTEM"), 1.0f, 1.0f, 0.0f };
    case 0x01: return { TEXT("CHAT_MSG_SAY"), 1.0f, 1.0f, 1.0f };
    case 0x02: return { TEXT("CHAT_MSG_PARTY"), 0.667f, 0.667f, 1.0f };
    case 0x03: return { TEXT("CHAT_MSG_RAID"), 1.0f, 0.498f, 0.0f };
    case 0x04: return { TEXT("CHAT_MSG_GUILD"), 0.251f, 1.0f, 0.251f };
    case 0x05: return { TEXT("CHAT_MSG_OFFICER"), 0.251f, 0.753f, 0.251f };
    case 0x06: return { TEXT("CHAT_MSG_YELL"), 1.0f, 0.251f, 0.251f };
    case 0x07: case 0x08: return { TEXT("CHAT_MSG_WHISPER"), 1.0f, 0.502f, 1.0f };
    case 0x09: return { TEXT("CHAT_MSG_WHISPER_INFORM"), 1.0f, 0.502f, 1.0f };
    case 0x0A: return { TEXT("CHAT_MSG_EMOTE"), 1.0f, 0.502f, 0.251f };
    case 0x0B: return { TEXT("CHAT_MSG_TEXT_EMOTE"), 1.0f, 0.502f, 0.251f };
    case 0x0C: return { TEXT("CHAT_MSG_MONSTER_SAY"), 1.0f, 1.0f, 0.624f };
    case 0x0D: return { TEXT("CHAT_MSG_MONSTER_PARTY"), 0.667f, 0.667f, 1.0f };
    case 0x0E: return { TEXT("CHAT_MSG_MONSTER_YELL"), 1.0f, 0.251f, 0.251f };
    case 0x0F: return { TEXT("CHAT_MSG_MONSTER_WHISPER"), 1.0f, 0.71f, 0.922f };
    case 0x10: return { TEXT("CHAT_MSG_MONSTER_EMOTE"), 1.0f, 0.502f, 0.251f };
    case 0x11: return { TEXT("CHAT_MSG_CHANNEL"), 1.0f, 0.753f, 0.753f };
    case 0x17: return { TEXT("CHAT_MSG_AFK"), 1.0f, 0.502f, 1.0f };
    case 0x18: return { TEXT("CHAT_MSG_DND"), 1.0f, 0.502f, 1.0f };
    case 0x19: return { TEXT("CHAT_MSG_IGNORED"), 1.0f, 1.0f, 0.0f };
    case 0x1A: return { TEXT("CHAT_MSG_SKILL"), 0.333f, 0.333f, 1.0f };
    case 0x1B: return { TEXT("CHAT_MSG_LOOT"), 0.0f, 0.667f, 0.0f };
    case 0x1C: return { TEXT("CHAT_MSG_MONEY"), 1.0f, 1.0f, 0.0f };
    case 0x1D: return { TEXT("CHAT_MSG_OPENING"), 0.502f, 0.502f, 1.0f };
    case 0x1E: return { TEXT("CHAT_MSG_TRADESKILLS"), 1.0f, 1.0f, 1.0f };
    case 0x1F: return { TEXT("CHAT_MSG_PET_INFO"), 1.0f, 1.0f, 1.0f };
    case 0x20: return { TEXT("CHAT_MSG_COMBAT_MISC_INFO"), 0.502f, 0.502f, 1.0f };
    case 0x21: return { TEXT("CHAT_MSG_COMBAT_XP_GAIN"), 0.435f, 0.435f, 1.0f };
    case 0x22: return { TEXT("CHAT_MSG_COMBAT_HONOR_GAIN"), 0.878f, 0.792f, 0.039f };
    case 0x23: return { TEXT("CHAT_MSG_COMBAT_FACTION_CHANGE"), 0.502f, 0.502f, 1.0f };
    case 0x24: return { TEXT("CHAT_MSG_BG_SYSTEM_NEUTRAL"), 1.0f, 0.471f, 0.039f };
    case 0x25: return { TEXT("CHAT_MSG_BG_SYSTEM_ALLIANCE"), 0.0f, 0.682f, 0.937f };
    case 0x26: return { TEXT("CHAT_MSG_BG_SYSTEM_HORDE"), 1.0f, 0.0f, 0.0f };
    case 0x27: return { TEXT("CHAT_MSG_RAID_LEADER"), 1.0f, 0.282f, 0.035f };
    case 0x28: return { TEXT("CHAT_MSG_RAID_WARNING"), 1.0f, 0.282f, 0.0f };
    case 0x29: return { TEXT("CHAT_MSG_RAID_BOSS_EMOTE"), 1.0f, 0.867f, 0.0f };
    case 0x2A: return { TEXT("CHAT_MSG_RAID_BOSS_WHISPER"), 1.0f, 0.71f, 0.922f };
    case 0x2B: return { TEXT("CHAT_MSG_FILTERED"), 1.0f, 0.0f, 0.0f };
    case 0x2C: return { TEXT("CHAT_MSG_BATTLEGROUND"), 1.0f, 0.498f, 0.0f };
    case 0x2D: return { TEXT("CHAT_MSG_BATTLEGROUND_LEADER"), 1.0f, 0.859f, 0.718f };
    case 0x2E: return { TEXT("CHAT_MSG_RESTRICTED"), 1.0f, 0.0f, 0.0f };
    case 0x30: return { TEXT("CHAT_MSG_ACHIEVEMENT"), 1.0f, 1.0f, 0.0f };
    case 0x31: return { TEXT("CHAT_MSG_GUILD_ACHIEVEMENT"), 0.251f, 1.0f, 0.251f };
    case 0x33: return { TEXT("CHAT_MSG_PARTY_LEADER"), 0.463f, 0.784f, 1.0f };
    default:   return { TEXT("CHAT_MSG_SAY"), 1.0f, 1.0f, 1.0f };
    }
}

const TCHAR* WowAddonDistribution(uint8 Type)
{
    switch (Type)
    {
    case 0x02: case 0x33: return TEXT("PARTY");
    case 0x03: case 0x27: case 0x28: return TEXT("RAID");
    case 0x04: case 0x05: return TEXT("GUILD");
    case 0x2C: case 0x2D: return TEXT("BATTLEGROUND");
    case 0x07: case 0x08: case 0x09: return TEXT("WHISPER");
    default: return TEXT("");
    }
}

// Lua-style "%s" substitution used by emote/achievement strings.
FString WowFormatNamedMessage(const FString& Message, const FString& Name)
{
    return Message.Contains(TEXT("%s")) ? Message.Replace(TEXT("%s"), *Name) : FString::Printf(TEXT("%s %s"), *Name, *Message);
}
}

void AWowGameplayController::OnChatMessage(uint8 Type, uint32 Language, uint64 SenderGuid, const FString& SenderName, const FString& InMessage, const FString& Channel)
{
	if ((Type == 0x07 || Type == 0x08) && Language != 0xFFFFFFFFu)
	{
		// Remember who whispered us last for /r (addon whispers do not count).
		LastWhisperSender = SenderName;
		LastWhisperGuid = SenderGuid;
	}
	FString Message = InMessage;
	// Creature speech (MONSTER_SAY/PARTY/YELL/WHISPER/EMOTE, RAID_BOSS_EMOTE/WHISPER) carries
	// the same tokens as quest text; the original client resolves $N/$C/$R/$G male:female; here.
	if ((Type >= 0x0C && Type <= 0x10) || Type == 0x29 || Type == 0x2A)
	{
		FWowCharacterInfo SpeakerTarget;
		if (ConnectionManager && ConnectionManager->GetSelectedCharacterInfo(SpeakerTarget) && Message.Contains(TEXT("$")))
		{
			int32 Start = Message.Find(TEXT("$G"), ESearchCase::IgnoreCase);
			while (Start != INDEX_NONE)
			{
				const int32 End = Message.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 2);
				const int32 Split = Message.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 2);
				if (End == INDEX_NONE || Split == INDEX_NONE || Split > End) break;
				const FString Choice = (SpeakerTarget.Gender == 1 ? Message.Mid(Split + 1, End - Split - 1) : Message.Mid(Start + 2, Split - Start - 2)).TrimStartAndEnd();
				Message = Message.Left(Start) + Choice + Message.Mid(End + 1);
				Start = Message.Find(TEXT("$G"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Start + Choice.Len());
			}
			static const TCHAR* Classes[] = {TEXT(""),TEXT("Warrior"),TEXT("Paladin"),TEXT("Hunter"),
				TEXT("Rogue"),TEXT("Priest"),TEXT("Death Knight"),TEXT("Shaman"),TEXT("Mage"),
				TEXT("Warlock"),TEXT(""),TEXT("Druid")};
			if (SpeakerTarget.Class < UE_ARRAY_COUNT(Classes) && *Classes[SpeakerTarget.Class])
				Message.ReplaceInline(TEXT("$C"), Classes[SpeakerTarget.Class], ESearchCase::IgnoreCase);
			if (const auto* Race = FDbcStore::Get().ChrRaces().GetById(SpeakerTarget.Race))
				Message.ReplaceInline(TEXT("$R"), *Race->Name, ESearchCase::IgnoreCase);
			Message.ReplaceInline(TEXT("$N"), *SpeakerTarget.Name, ESearchCase::IgnoreCase);
		}
	}
	// LANG_ADDON (-1): hidden addon traffic, "prefix<TAB>payload". The original
	// client never prints these; it only raises CHAT_MSG_ADDON.
	if (Language == 0xFFFFFFFFu)
	{
		FString Prefix, Payload;
		if (!Message.Split(TEXT("\t"), &Prefix, &Payload)) { Prefix = Message; Payload.Empty(); }
		const FString Sender = SenderName.StartsWith(TEXT("Player-")) ? GetPlayerDisplayName(SenderGuid) : SenderName;
		{
			// Diagnostic for addon handshakes (AIO etc.): first messages per prefix.
			static TMap<FString, int32> LoggedAddonPrefixes;
			int32& Seen = LoggedAddonPrefixes.FindOrAdd(Prefix);
			if (Seen++ < 5)
			{
				UE_LOG(LogTemp, Log, TEXT("Addon message in: prefix='%s' type=%d sender='%s' bytes=%d"),
					*Prefix, Type, *Sender, Payload.Len());
			}
		}
		// WHISPER_INFORM is the echo of our own outgoing addon whisper; the original client drops it.
		if (Type != 0x09)
			FireUIEvent(TEXT("CHAT_MSG_ADDON"), { Prefix, Payload, WowAddonDistribution(Type), Sender });
		return;
	}

	// Chat bubbles (chatBubbles / chatBubblesParty): /say, /yell and creature say/yell over the
	// speaker's head, in the chat type's colour (ChatTypeInfo SAY, YELL, MONSTER_SAY, PARTY).
	if (SenderGuid)
	{
		const bool bSay = Type == 0x01 || Type == 0x06 || Type == 0x0C || Type == 0x0E;
		const bool bParty = Type == 0x02 || Type == 0x33;
		if ((bSay && FWowClientPreferences::GetWorldOption(TEXT("chatbubbles")))
			|| (bParty && FWowClientPreferences::GetWorldOption(TEXT("chatbubblesparty"))))
		{
			const FLinearColor Color = (Type == 0x06 || Type == 0x0E) ? FLinearColor(1.f, .25f, .25f)
				: Type == 0x0C ? FLinearColor(1.f, 1.f, .62f)
				: bParty ? FLinearColor(.67f, .67f, 1.f) : FLinearColor::White;
			ShowChatBubble(SenderGuid, Message, WowColor::FromWow(Color));
		}
	}

    const bool bPlayerChat=Type==1 || Type==2 || Type==3 || Type==4 || Type==5 || Type==6 || Type==7 || Type==9 || Type==10 || Type==17
        || Type==0x27 || Type==0x28 || Type==0x2C || Type==0x2D || Type==0x33 || Type==0x30 || Type==0x31 || Type==0x17 || Type==0x18;
    if (bPlayerChat && SenderGuid && ConnectionManager &&
        (SenderName.IsEmpty() || SenderName.StartsWith(TEXT("Player-"))) &&
        !ConnectionManager->PacketHandler.PlayerNameCache.Contains(SenderGuid))
    {
        if (!PendingNamedChat.ContainsByPredicate([SenderGuid](const FPendingNamedChat& C){return C.Guid==SenderGuid;}))
            ConnectionManager->SendNameQuery(static_cast<int64>(SenderGuid));
        if (PendingNamedChat.Num()<256)
        { PendingNamedChat.Add({Type,Language,SenderGuid,Message,Channel,FPlatformTime::Seconds()+10}); return; }
    }
    FString ResolvedName = SenderName.StartsWith(TEXT("Player-"))?GetPlayerDisplayName(SenderGuid):SenderName;
	if (ResolvedName.IsEmpty()) ResolvedName = GetPlayerDisplayName(SenderGuid);
	if (ResolvedName.IsEmpty() && Type != 0) ResolvedName = TEXT("Unknown");

    const FString PlainSender=ResolvedName;
    if (bPlayerChat && ConnectionManager)
    {
        uint8 ClassId=ConnectionManager->PacketHandler.PlayerClassCache.FindRef(SenderGuid);
        if (const auto* ChatPlayer=ConnectionManager->PacketHandler.EntityManager.FindPlayer(SenderGuid)) ClassId=ChatPlayer->GetClassId();
        static const TCHAR* Colors[]={TEXT(""),TEXT("C79C6E"),TEXT("F58CBA"),TEXT("ABD473"),TEXT("FFF569"),TEXT("FFFFFF"),TEXT("C41F3B"),TEXT("0070DE"),TEXT("69CCF0"),TEXT("9482C9"),TEXT(""),TEXT("FF7D0A")};
        if (ClassId<UE_ARRAY_COUNT(Colors) && *Colors[ClassId])
            ResolvedName=FString::Printf(TEXT("|cff%s%s|r"),Colors[ClassId],*ResolvedName);
    }
    // Player names are bracketed links in the 3.3.5 chat frame; AFK/DND/GM tags precede them.
    const uint8 ChatTag = ConnectionManager ? ConnectionManager->PacketHandler.LastChatTag : 0;
    FString TagPrefix;
    if (ChatTag & 0x04) TagPrefix = TEXT("<GM>");
    else if (ChatTag & 0x01) TagPrefix = TEXT("<AFK>");
    else if (ChatTag & 0x02) TagPrefix = TEXT("<DND>");
    const FString PlayerLink = TagPrefix + FString::Printf(TEXT("[%s]"), *ResolvedName);

	const FWowChatStyle Style = WowChatStyleFor(Type);
	const FLinearColor Color(Style.R, Style.G, Style.B, 1.0f);
	FString Display;
	FString ChannelString;
	int32 ChatChannelNumber = 0;
	FString ChatChannelName;
	switch (Type)
	{
	case 0x01: Display = FString::Printf(TEXT("%s says: %s"), *PlayerLink, *Message); break;
	case 0x02: Display = FString::Printf(TEXT("[Party] %s: %s"), *PlayerLink, *Message); break;
	case 0x33: Display = FString::Printf(TEXT("[Party Leader] %s: %s"), *PlayerLink, *Message); break;
	case 0x03: Display = FString::Printf(TEXT("[Raid] %s: %s"), *PlayerLink, *Message); break;
	case 0x27: Display = FString::Printf(TEXT("[Raid Leader] %s: %s"), *PlayerLink, *Message); break;
	case 0x28: Display = FString::Printf(TEXT("[Raid Warning] %s: %s"), *PlayerLink, *Message); break;
	case 0x2C: Display = FString::Printf(TEXT("[Battleground] %s: %s"), *PlayerLink, *Message); break;
	case 0x2D: Display = FString::Printf(TEXT("[Battleground Leader] %s: %s"), *PlayerLink, *Message); break;
	case 0x04: Display = FString::Printf(TEXT("[Guild] %s: %s"), *PlayerLink, *Message); break;
	case 0x05: Display = FString::Printf(TEXT("[Officer] %s: %s"), *PlayerLink, *Message); break;
	case 0x06: Display = FString::Printf(TEXT("%s yells: %s"), *PlayerLink, *Message); break;
	case 0x07: case 0x08: Display = FString::Printf(TEXT("%s whispers: %s"), *PlayerLink, *Message); break;
	case 0x09: Display = FString::Printf(TEXT("To %s: %s"), *PlayerLink, *Message); break;
	case 0x0A: Display = FString::Printf(TEXT("%s %s"), *ResolvedName, *Message); break;
	case 0x0B: Display = Message; break; // server already embeds the actor name
	case 0x0C: Display = FString::Printf(TEXT("%s says: %s"), *ResolvedName, *Message); break;
	case 0x0D: Display = FString::Printf(TEXT("[Party] %s: %s"), *ResolvedName, *Message); break;
	case 0x0E: Display = FString::Printf(TEXT("%s yells: %s"), *ResolvedName, *Message); break;
	case 0x0F: case 0x2A: Display = FString::Printf(TEXT("%s whispers: %s"), *ResolvedName, *Message); break;
	case 0x10: case 0x29: Display = WowFormatNamedMessage(Message, ResolvedName); break;
	case 0x30: case 0x31: Display = WowFormatNamedMessage(Message, PlayerLink); break;
	case 0x17: Display = FString::Printf(TEXT("%s is Away: %s"), *PlayerLink, *Message); break;
	case 0x18: Display = FString::Printf(TEXT("%s does not wish to be disturbed: %s"), *PlayerLink, *Message); break;
	case 0x11:
	{
		FString BaseName = Channel;
		BaseName.Split(TEXT(" - "), &BaseName, nullptr);
		int32 Number = ConnectionManager ? ConnectionManager->PacketHandler.GetChannelNumber(Channel) : 0;
		if (Number <= 0)
		{
			int32* Known = ChatChannelNumbers.Find(BaseName);
			if (!Known)
			{
				int32 Assigned = 1;
				for (const auto& Pair : ChatChannelNumbers) Assigned = FMath::Max(Assigned, Pair.Value + 1);
				Known = &ChatChannelNumbers.Add(BaseName, Assigned);
			}
			Number = *Known;
		}
		ChannelString = FString::Printf(TEXT("%d. %s"), Number, Channel.IsEmpty() ? TEXT("Channel") : *Channel);
		ChatChannelNumber = Number;
		ChatChannelName = Channel;
		Display = FString::Printf(TEXT("[%d. %s] %s: %s"), Number, *BaseName, *PlayerLink, *Message);
		break;
	}
	default: // SYSTEM, LOOT, MONEY, SKILL, BG system and other server text
		Display = Message;
		break;
	}

	AddFrameXmlChatLine(Display, Color);
	if ((Type == 7 || Type == 8) && UIManager && UIManager->GetFrameManager())
	{
		UIManager->GetFrameManager()->SetRecentWhisperContact(PlainSender, Message);
	}

	// 3.3.5 CHAT_MSG_* arguments: message, sender, language, channelString, target, flags,
	// zoneChannelID, channelNumber, channelName, (unused), lineID, senderGUID. ChatFrame.lua
	// builds "CHANNEL"..arg8 for channel lines and failed on every one without them (log 11.10).
	static int32 ChatLineId = 0;
	const FString Flags = (ChatTag & 0x04) ? TEXT("GM") : (ChatTag & 0x01) ? TEXT("AFK") : (ChatTag & 0x02) ? TEXT("DND") : TEXT("");
	FireUIEvent(Style.Event, {Message, PlainSender, TEXT(""), ChannelString.IsEmpty() ? Channel : ChannelString, TEXT(""), Flags,
		CombatLogNumber(0), CombatLogNumber(ChatChannelNumber), ChatChannelName, TEXT(""), CombatLogNumber(++ChatLineId),
		SenderGuid ? CombatLogGuid(SenderGuid) : FString()});
}

void AWowGameplayController::OnFriendListUpdated()
{
	if (!ConnectionManager || !UIManager || !UIManager->GetFrameManager()) return;

	TArray<FString> FriendEntries;
	TArray<FString> IgnoreEntries;
	int32 OnlineCount = 0;
	for (FWowFriendInfo& Friend : ConnectionManager->PacketHandler.FriendsList)
	{
		if (const FString* CachedName = ConnectionManager->PacketHandler.PlayerNameCache.Find(Friend.Guid))
		{
			Friend.Name = *CachedName;
		}
		else if (!PendingFriendNameQueries.Contains(Friend.Guid))
		{
			PendingFriendNameQueries.Add(Friend.Guid);
			ConnectionManager->SendNameQuery(static_cast<int64>(Friend.Guid));
		}

		const FString DisplayName = Friend.Name.IsEmpty() ? TEXT("Resolving name...") : Friend.Name;
		if ((Friend.Flags & 0x1) != 0)
		{
			FriendEntries.Add(Friend.Status > 0
				? FString::Printf(TEXT("%s  â€¢  Level %d"), *DisplayName, Friend.Level)
				: FString::Printf(TEXT("%s  â€¢  Offline"), *DisplayName));
			if (Friend.Status > 0) ++OnlineCount;
		}
		if ((Friend.Flags & 0x2) != 0)
		{
			IgnoreEntries.Add(DisplayName);
		}
	}
	UIManager->GetFrameManager()->SetSocialContactEntries(FriendEntries, IgnoreEntries, OnlineCount);
}

void AWowGameplayController::OnSocialPlayerNameReceived(uint64 Guid, const FString& Name)
{
	if (!ConnectionManager) return;
	PendingFriendNameQueries.Remove(Guid);
	for (FWowFriendInfo& Contact : ConnectionManager->PacketHandler.FriendsList)
	{
		if (Contact.Guid == Guid)
		{
			Contact.Name = Name;
			break;
		}
	}
	OnFriendListUpdated();
}

void AWowGameplayController::OnEmote(uint64 EntityGuid, uint32 EmoteId)
{
	if (!ConnectionManager) return;

	UE_LOG(LogWowGameplay, Log, TEXT("Entity %llu performed emote %u"), EntityGuid, EmoteId);

	// Find the entity that performed the emote
	const FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(EntityGuid);
	if (!Entity)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("OnEmote: Entity %llu not found"), EntityGuid);
		return;
	}

	// Try to find the associated character actor
	AActor* CharacterActor = nullptr;
	if (TObjectPtr<AActor>* ActorPtr = SpawnedEntityActors.Find(EntityGuid))
	{
		CharacterActor = *ActorPtr;
	}

	// If this is the local player, use the player character pawn instead
	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	if (EntityGuid == LocalGuid)
	{
		if (APawn* LocalPawn = GetPawn())
		{
			if (AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(LocalPawn))
			{
				PlayEmoteAnimation(PlayerChar, EmoteId);
				return;
			}
		}
	}

	// For other entities, play the animation on their character actor
	if (CharacterActor)
	{
		PlayEmoteAnimation(CharacterActor, EmoteId);
	}
}

void AWowGameplayController::PlayEmoteAnimation(AActor* CharacterActor, uint32 EmoteId)
{
	if (!CharacterActor)
	{
		return;
	}

	// Get the animation controller using the standard approach
	UWowAnimationController* AnimController = nullptr;

	// Try AWowPlayerCharacter first (for local player)
	if (AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(CharacterActor))
	{
		AnimController = PlayerChar->AnimationController;
	}
	else
	{
		// For spawned entities, use FWowCharacterBuilder
		AnimController = FWowCharacterBuilder::GetAnimationController(CharacterActor);
	}

	if (!AnimController)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("PlayEmoteAnimation: No animation controller found on character"));
		return;
	}

    if (EmoteId == 0) { AnimController->StopEmote(); return; }
    const auto* Entry = FDbcStore::Get().Emotes().GetById(EmoteId);
    if (!Entry) return;
    const int32 Anim = Entry->Type == 1
        ? static_cast<int32>(GetAnimationIdForStandState(uint8(Entry->StandState)))
        : int32(Entry->AnimationID);
    AnimController->PlayEmote(Anim, Entry->IsLooping() || Entry->Type == 1);
}

int32 AWowGameplayController::GetAnimationForEmoteId(uint32 EmoteId)
{
    const auto* Entry = FDbcStore::Get().Emotes().GetById(EmoteId);
    return Entry ? int32(Entry->AnimationID) : INDEX_NONE;
}

EWowAnimId AWowGameplayController::GetAnimationIdForStandState(uint8 StandState)
{
	// Map WoW stand states to animation IDs
	// Reference: WowStandState enum in WowEntity.h
	switch (StandState)
	{
		case WowStandState::STAND:
			return EWowAnimId::Stand;
        case WowStandState::SIT:
            return EWowAnimId::SitGround;
        case WowStandState::SIT_LOW:
            return EWowAnimId::SitChairLow;
        case WowStandState::SIT_CHAIR:
        case WowStandState::SIT_MED:
            return EWowAnimId::SitChair;
        case WowStandState::SIT_HIGH:
            return EWowAnimId::SitChairHigh;
		case WowStandState::SLEEP:
			return EWowAnimId::Sleep;
		case WowStandState::KNEEL:
			return EWowAnimId::Kneel;
		case WowStandState::DEAD:
			return EWowAnimId::Dead;
		default:
			return EWowAnimId::Stand; // Fallback to stand
	}
}

void AWowGameplayController::OnEntityHealthChanged(const FWowEntity& Entity, int32 OldHealth, int32 NewHealth)
{
	if (!ConnectionManager) return;

	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	int32 HealthDiff = NewHealth - OldHealth;

	if (HealthDiff == 0) return;

	FLinearColor MessageColor;
	FString Message;
	bool bHealthLineOnPlayer = false;

	if (Entity.Guid == LocalGuid)
	{
		// Player health change
		if (HealthDiff > 0)
		{
			MessageColor = FLinearColor::Green; // Healing
			Message = FString::Printf(TEXT("You are healed for %d"), HealthDiff);
			bHealthLineOnPlayer = true;
		}
		else
		{
			MessageColor = FLinearColor::Red; // Damage
			Message = FString::Printf(TEXT("You take %d damage"), -HealthDiff);
			bHealthLineOnPlayer = true;
		}
	}
	else
	{
		// Other entity health change
		MessageColor = FLinearColor(0.8f, 0.8f, 0.8f, 1.0f); // Gray
		// The unit's name (Stefan 10.10: "Entity 6624 is healed for 43"); a unit whose name is not
		// known yet gets no line rather than a GUID.
		const FString EntityName = CombatLogUnitName(Entity.Guid);
		if (!EntityName.IsEmpty())
		{
			Message = HealthDiff > 0
				? FString::Printf(TEXT("%s is healed for %d"), *EntityName, HealthDiff)
				: FString::Printf(TEXT("%s takes %d damage"), *EntityName, -HealthDiff);
		}
	}

	if (!Message.IsEmpty()) AddCombatMessage(Message, MessageColor, false, bHealthLineOnPlayer);

	// Detect death: health dropped FROM positive TO 0 on a non-player entity
	// Only trigger if OldHealth was positive â€” this ensures we actually saw it alive first
	if (NewHealth <= 0 && OldHealth > 0 && Entity.Guid != LocalGuid && !DeadEntityGuids.Contains(Entity.Guid))
	{
		OnEntityDeath(Entity.Guid);

		// Fire health event if it's our target
		if (Entity.Guid == TargetGuid)
		{
			FireUIEvent(TEXT("UNIT_HEALTH"), {TEXT("target")});
		}
	}
}

void AWowGameplayController::CreateChatWindow()
{
	if (!ConnectionManager || ChatWindow.IsValid())
	{
		return;
	}

	// Create the chat window widget
	ChatWindow = SNew(SWowChatWindow)
		.ConnectionManager(ConnectionManager);

	// Bind input mode change delegate
	ChatWindow->OnInputModeChanged.BindUObject(this, &AWowGameplayController::OnChatInputModeChanged);
}

void AWowGameplayController::OnEnterKey()
{
	FWowFrameManager* FrameManager = UIManager ? UIManager->GetFrameManager() : nullptr;
	if (FrameManager)
	{
        FrameManager->SetChatWindowHidden(false);
		int64 FocusedHandle = -1;
		FString FocusedText;
		if (FrameManager->GetFocusedEditBox(FocusedHandle, FocusedText))
		{
			if (FrameManager->GetFrameName(FocusedHandle).StartsWith(TEXT("ChatFrame")))
			{
				// Once "/y " (or /p, /g, /w name, /2 ...) is typed, the stock edit box removes the
				// prefix and keeps the type in its chatType attribute (header "Yell:"). Put it back
				// in front of the text so SubmitFrameXmlChat sends the right type, not Say.
				if (!FocusedText.TrimStart().StartsWith(TEXT("/")) && UIManager->GetLuaVM() && UIManager->GetLuaVM()->IsInitialized())
				{
					const FString EditName = FrameManager->GetFrameName(FocusedHandle);
					UIManager->GetLuaVM()->ExecuteString(TEXT("local eb = _G[\"") + EditName + TEXT(R"LUA("]
if eb and eb.GetAttribute then
    local t, p = eb:GetAttribute("chatType"), nil
    if t == "WHISPER" then
        local n = eb:GetAttribute("tellTarget")
        if n and n ~= "" then p = "/w " .. n .. " " end
    elseif t == "CHANNEL" then
        local c = eb:GetAttribute("channelTarget")
        if c then p = "/" .. c .. " " end
    else
        p = ({YELL = "/y ", PARTY = "/p ", GUILD = "/g ", OFFICER = "/o ", RAID = "/ra ",
              RAID_WARNING = "/rw ", BATTLEGROUND = "/bg ", EMOTE = "/e "})[t or ""]
    end
    if p then eb:SetText(p .. (eb:GetText() or "")) end
end)LUA"), TEXT("=ChatTypePrefix"));
					FrameManager->GetFocusedEditBox(FocusedHandle, FocusedText);
				}
				SubmitFrameXmlChat(FocusedText);
				CloseFrameXmlChatInput(true);
			}
			else
			{
				FrameManager->DispatchEditBoxEnterPressed();
			}
			return;
		}

		const int64 ChatEditHandle = FrameManager->FindFrame(TEXT("ChatFrame1EditBox"));
		if (FrameManager->FocusEditBox(ChatEditHandle))
		{
			if (UEditableTextBox* ChatEdit = FrameManager->GetEditBoxWidget(ChatEditHandle))
			{
				ApplyWowGameAndUiInputMode(this, ChatEdit->TakeWidget());
			}
			bShowMouseCursor = true;
			CurrentMouseCursor = EMouseCursor::None;
			return;
		}
	}

	if (ChatWindow.IsValid())
	{
		ChatWindow->ToggleInputFocus();
	}
}

void AWowGameplayController::CloseFrameXmlChatInput(bool bClearText)
{
	if (UIManager && UIManager->GetFrameManager())
	{
		UIManager->GetFrameManager()->CloseEditBox(
			UIManager->GetFrameManager()->FindFrame(TEXT("ChatFrame1EditBox")), bClearText);
	}
	ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
	FocusWowGameViewport();
}

void AWowGameplayController::AddFrameXmlChatLine(const FString& Message, const FLinearColor& Color, bool bCombatLog)
{
	if (!UIManager || !UIManager->GetFrameManager()) return;
	FWowFrameManager* FrameManager = UIManager->GetFrameManager();
	// WoW chat colours (ChatTypeInfo values) are sRGB; Slate is linear.
	FrameManager->AddScrollingMessage(
		FrameManager->FindFrame(bCombatLog ? TEXT("ChatFrame2") : TEXT("ChatFrame1")), Message, WowColor::FromWow(Color));
}

void AWowGameplayController::SubmitFrameXmlChat(const FString& InputText)
{
	FString Text = InputText.TrimStartAndEnd();
	if (UIManager && UIManager->GetFrameManager() &&
		UIManager->GetFrameManager()->CommitPendingChatTabRename(Text))
	{
		return;
	}
	if (Text.IsEmpty()) return;

	int32 ChatType = 1; // SAY
	FString Target;
	FString Channel;
	FString Message = Text;

	if (Text.StartsWith(TEXT("/")))
	{
		FString CommandLine = Text.Mid(1);
		FString Command;
		if (!CommandLine.Split(TEXT(" "), &Command, &Message))
		{
			Command = CommandLine;
			Message.Empty();
		}
		Command = Command.ToLower();
		Message.TrimStartAndEndInline();

        if(Command==TEXT("macro") || Command==TEXT("m"))
        {
            ToggleMacroFrame();
            return;
        }
        if(Command==TEXT("fstack") || Command==TEXT("framestack"))
        {
            if(UIManager && UIManager->GetFrameManager())UIManager->GetFrameManager()->ToggleFrameStack();
            return;
        }
		if (Command == TEXT("s") || Command == TEXT("say"))
		{
			ChatType = 1;
		}
		else if (Command == TEXT("p") || Command == TEXT("party"))
		{
			ChatType = 2;
		}
		else if (Command == TEXT("g") || Command == TEXT("guild"))
		{
			ChatType = 4;
		}
		else if (Command == TEXT("y") || Command == TEXT("yell"))
		{
			ChatType = 6;
		}
		else if (Command == TEXT("r") || Command == TEXT("reply"))
		{
			// Reply to the last player who whispered us (original /r).
			ChatType = 7;
			Target = LastWhisperSender;
			if (Target.IsEmpty() || Target.StartsWith(TEXT("Player-")))
				Target = LastWhisperGuid ? GetPlayerDisplayName(LastWhisperGuid) : FString();
			if (Target.IsEmpty())
			{
				AddFrameXmlChatLine(TEXT("No one has whispered you yet."), FLinearColor(1.0f, 1.0f, 0.0f, 1.0f));
				return;
			}
		}
		else if (Command == TEXT("w") || Command == TEXT("whisper"))
		{
			ChatType = 7;
			if (!Message.Split(TEXT(" "), &Target, &Message) || Target.IsEmpty())
			{
				AddFrameXmlChatLine(TEXT("Usage: /whisper <player> <message>"),
					FLinearColor(1.0f, 0.2f, 0.2f, 1.0f));
				return;
			}
			Message.TrimStartAndEndInline();
		}
		else if (Command.IsNumeric() && FCString::Atoi(*Command) >= 1 && FCString::Atoi(*Command) <= 10)
		{
			ChatType = 17;
			const int32 Number = FCString::Atoi(*Command);
			Channel = ConnectionManager ? ConnectionManager->PacketHandler.GetChannelNameByNumber(Number) : FString();
			if (Channel.IsEmpty() && Number <= 2) Channel = Number == 1 ? TEXT("General") : TEXT("Trade");
			if (Channel.IsEmpty())
			{
				AddFrameXmlChatLine(TEXT("You are not in a channel by that number."), FLinearColor(1.0f, 1.0f, 0.0f, 1.0f));
				return;
			}
			Target = Channel;
		}
		else if (Command == TEXT("o") || Command == TEXT("officer"))
		{
			ChatType = 5;
		}
		else if (Command == TEXT("ra") || Command == TEXT("raid"))
		{
			ChatType = 3;
		}
		else if (Command == TEXT("rw") || Command == TEXT("raidwarning"))
		{
			ChatType = 0x28;
		}
		else if (Command == TEXT("bg") || Command == TEXT("battleground"))
		{
			ChatType = 0x2C;
		}
		else if (Command == TEXT("e") || Command == TEXT("em") || Command == TEXT("me") || Command == TEXT("emote"))
		{
			ChatType = 10;
		}
		// r324 (Stefan 14.10: "/afk - nothing happened", "/dnd - nothing happened"): an empty /afk or
		// /dnd toggles it on the server (a filled-in "Away" never let you leave AFK again); the
		// "You are now AFK: ..." lines come from the player flags (see the PLAYER_FLAGS handling).
		else if (Command == TEXT("afk") || Command == TEXT("away"))
		{
			ChatType = 0x17;
			GWowAwayMessage = Message;
		}
		else if (Command == TEXT("dnd") || Command == TEXT("busy"))
		{
			ChatType = 0x18;
			GWowAwayMessage = Message;
		}
		else
		{
			// Everything else (/invite, /roll, /who, /join, /reload, /dance, addon
			// commands such as /aio ...) goes to the stock SlashCmdList and emotes.
			RunLuaSlashCommand(Command, Message);
			return;
		}
	}
	// GM ".commands" are sent unchanged as normal chat; the server parses them and
	// answers with system messages.

	if (Message.IsEmpty() && ChatType != 0x17 && ChatType != 0x18) return; // r324: empty /afk, /dnd toggle

	const bool bOnline = ConnectionManager && ConnectionManager->GetState() == EWowSessionState::WorldInGame;
	if (bOnline)
	{
		ConnectionManager->SendChatMessage(Message, ChatType, Target, static_cast<int32>(ConnectionManager->GetDefaultChatLanguage()));
		return; // The world server echoes the authoritative message back to OnChatMessage.
	}

	uint64 LocalGuid = ConnectionManager ? ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid : 0;
	FString PlayerName = GetPlayerDisplayName(LocalGuid);
	if (PlayerName.IsEmpty()) PlayerName = TEXT("Offline Adventurer");

	FString Display;
	FLinearColor Color = FLinearColor::White;
	switch (ChatType)
	{
	case 2:
		Display = FString::Printf(TEXT("[Party] %s: %s"), *PlayerName, *Message);
		Color = FLinearColor(0.67f, 0.67f, 1.0f, 1.0f);
		break;
	case 4:
		Display = FString::Printf(TEXT("[Guild] %s: %s"), *PlayerName, *Message);
		Color = FLinearColor(0.25f, 1.0f, 0.25f, 1.0f);
		break;
	case 6:
		Display = FString::Printf(TEXT("%s yells: %s"), *PlayerName, *Message);
		Color = FLinearColor(1.0f, 0.25f, 0.25f, 1.0f);
		break;
	case 7:
		Display = FString::Printf(TEXT("To [%s]: %s"), *Target, *Message);
		Color = FLinearColor(1.0f, 0.5f, 1.0f, 1.0f);
		break;
	case 17:
		Display = FString::Printf(TEXT("[%s] %s: %s"), *Channel, *PlayerName, *Message);
		Color = FLinearColor(1.0f, 0.75f, 0.35f, 1.0f);
		break;
	default:
		Display = FString::Printf(TEXT("%s: %s"), *PlayerName, *Message);
		break;
	}
	AddFrameXmlChatLine(Display, Color);
}

void AWowGameplayController::OnChatInputModeChanged(bool bChatActive)
{
	if (bChatActive && FSlateApplication::IsInitialized())
	{
		ApplyWowGameAndUiInputMode(this, FSlateApplication::Get().GetKeyboardFocusedWidget());
		return;
	}

	ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
	FocusWowGameViewport();
}

void AWowGameplayController::PlayContainerToggleSound(bool bOpening) const
{
	AWowAudioManager* AudioManager = Cast<AWowAudioManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AWowAudioManager::StaticClass()));
	if (!AudioManager)
	{
		return;
	}

	if (bOpening)
	{
		AudioManager->PlayContainerOpenSound();
	}
	else
	{
		AudioManager->PlayContainerCloseSound();
	}
}

void AWowGameplayController::OnBagKey()
{
    if (!UIManager || !UIManager->GetFrameManager()) return;
    if (UIManager->IsBagWindowVisible()) UIManager->HideBagWindow();
    UIManager->GetFrameManager()->TogglePlayerBag(0);
}

void AWowGameplayController::OnAllBagsKey()
{
    if (!UIManager || !UIManager->GetFrameManager()) return;
    if (UIManager->IsBagWindowVisible()) UIManager->HideBagWindow();
    UIManager->GetFrameManager()->ToggleAllPlayerBags();
}

void AWowGameplayController::OnCharacterKey()
{
	if (!UIManager || !UIManager->GetFrameManager())
	{
		return;
	}

	// Toggle the stock 3.3.5 CharacterFrame directly. UIPanelTemplates.lua still
	// expects several unimplemented layout helpers, so routing through
	// ToggleCharacter() can fail before it reaches the frame visibility change.
	FWowFrameManager* FrameManager = UIManager->GetFrameManager();
	const int64 CharacterFrame = FrameManager->FindFrame(TEXT("CharacterFrame"));
	if (CharacterFrame < 0)
	{
		return;
	}

	const bool bShow = !FrameManager->IsFrameVisible(CharacterFrame);
	if (bShow)
	{
		FrameManager->SetFrameVisible(CharacterFrame, true);
		if (GroupFinderWidget.IsValid() && GroupFinderWidget->IsVisible()) GroupFinderWidget->SetFinderVisibility(false);
		auto RefreshCharacterSheet = [this, FrameManager]()
		{
			if (ConnectionManager)
			{
				if (FWowPlayerEntity* LocalPlayer =
					ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer())
				{
					FrameManager->UpdateCharacterSheet(*LocalPlayer,
						ConnectionManager->GetCharacterName());
					return;
				}
			}

			// Standalone world tests do not always construct the network entity
			// manager. Keep the stock paper doll useful with a deterministic local
			// level-one human warrior profile until a realm supplies live fields.
			FWowPlayerEntity OfflinePlayer;
			OfflinePlayer.SetField(UnitField::BYTES_0, 1u | (1u << 8));
			OfflinePlayer.SetField(UnitField::LEVEL, 1);
			OfflinePlayer.SetField(UnitField::STAT0, 23);
			OfflinePlayer.SetField(UnitField::STAT1, 20);
			OfflinePlayer.SetField(UnitField::STAT2, 22);
			OfflinePlayer.SetField(UnitField::STAT3, 20);
			OfflinePlayer.SetField(UnitField::STAT4, 21);
			OfflinePlayer.SetField(UnitField::RESISTANCES, 50);
			OfflinePlayer.SetField(UnitField::ATTACK_POWER, 30);
			FrameManager->UpdateCharacterSheet(OfflinePlayer, TEXT("Offline Adventurer"));
		};

		UIManager->UpdateInventory();
		RefreshCharacterSheet();
		FireUIEvent(TEXT("UNIT_STATS"), {TEXT("player")});
		FireUIEvent(TEXT("UNIT_INVENTORY_CHANGED"), {TEXT("player")});
		// Stock FrameXML may replace paper-doll text and empty-slot art while
		// processing those events. Re-apply the live client state afterward.
		UIManager->UpdateInventory();
		RefreshCharacterSheet();

		// Currency/pet remain separate unfinished features. The three original
		// Wrath character-info pages are connected by the native tab bridge.
		const TCHAR* HiddenCharacterPanels[] = {
			TEXT("TokenFrame"),
			TEXT("PetPaperDollFrame"),
			TEXT("CompanionFrame"),
			TEXT("EquipmentManagerPane")
		};
		for (const TCHAR* PanelName : HiddenCharacterPanels)
		{
			FrameManager->SetFrameVisible(FrameManager->FindFrame(PanelName), false);
		}
		for (int32 TabIndex = 4; TabIndex <= 8; ++TabIndex)
		{
			FrameManager->SetFrameVisible(FrameManager->FindFrame(
				FString::Printf(TEXT("CharacterFrameTab%d"), TabIndex)), false);
		}
	}
	FrameManager->SetFrameVisible(CharacterFrame, bShow);
	if (bShow)
	{
		FrameManager->SetActiveCharacterSheetTab(1);
		EnsureCharacterSheetPreview();
		if (CharacterSheetPreview)
		{
			CharacterSheetPreview->SetPreviewActive(true);
		}
	}
	else if (CharacterSheetPreview)
	{
		CharacterSheetPreview->SetPreviewActive(false);
	}
}

void AWowGameplayController::EnsureCharacterSheetPreview()
{
	if (!UIManager || !UIManager->GetFrameManager() || !GetWorld())
	{
		return;
	}

	AWowWorldManager* WorldManager = Cast<AWowWorldManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AWowWorldManager::StaticClass()));
	if (!WorldManager || !WorldManager->GetMpqManager() || !WorldManager->GetAssetCache())
	{
		return;
	}

	if (!CharacterSheetPreview)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = FName(TEXT("WowCharacterSheetPreview"));
		CharacterSheetPreview = GetWorld()->SpawnActor<AWowCharacterPreview>(
			AWowCharacterPreview::StaticClass(), FVector::ZeroVector,
			FRotator::ZeroRotator, SpawnParameters);
		if (!CharacterSheetPreview)
		{
			return;
		}

		// Match CharacterModelFrame's 256x286 portrait aspect ratio so the live
		// model is neither stretched nor reduced by a landscape capture target.
		CharacterSheetPreview->Setup(WorldManager, 512, 572,
			EWowCharacterPreviewMode::PaperDoll);
		FWowCharacterInfo CharacterInfo;
		if (ConnectionManager && ConnectionManager->GetSelectedCharacterInfo(CharacterInfo))
		{
			CharacterSheetPreview->ShowCharacter(CharacterInfo);
		}
		else if (ConnectionManager)
		{
			if (const FWowPlayerEntity* LocalPlayer =
				ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer())
			{
				CharacterInfo.Race = LocalPlayer->GetRaceId();
				CharacterInfo.Gender = LocalPlayer->GetGenderId();
				CharacterSheetPreview->ShowCharacter(CharacterInfo);
			}
			else
			{
				CharacterSheetPreview->ShowCharacter(1, 0);
			}
		}
		else
		{
			CharacterSheetPreview->ShowCharacter(1, 0);
		}
	}
	const int64 ModelFrameHandle = UIManager->GetFrameManager()->FindFrame(TEXT("CharacterModelFrame"));
	if (ModelFrameHandle >= 0)
	{
		UIManager->GetFrameManager()->SetFrameMouseEnabled(ModelFrameHandle, true);
	}

	UIManager->GetFrameManager()->SetCharacterSheetRenderTarget(
		CharacterSheetPreview->GetRenderTarget());
}

void AWowGameplayController::OnAchievementKey()
{
	// r342 (Stefan 14.10, chose the stock window): Y / the micro button open the original
	// Blizzard_AchievementUI like the client (ToggleAchievementFrame loads it on demand). The native
	// window stays for VR and for a data set without the addon.
	if (!FWowUiVrHost::IsActive() && UIManager && UIManager->GetEventSystem() && UIManager->GetFrameManager())
	{
		// r348 (Stefan 15.10: Y still opened the native window; the log had no "Loading addon:
		// Blizzard_AchievementUI", so ToggleAchievementFrame never reached LoadAddOn): load the addon
		// and toggle its frame directly, and log why when that fails.
		FWowEventSystem* AchEvents = UIManager->GetEventSystem();
		AchEvents->ExecuteLuaString(TEXT(R"LUA(
__WowAchievementLoad = ""
local ok, err = pcall(function()
    if not AchievementFrame_ToggleAchievementFrame then
        local loaded, reason = LoadAddOn("Blizzard_AchievementUI")
        if not loaded then error("LoadAddOn: " .. tostring(reason)) end
    end
    if not AchievementFrame_ToggleAchievementFrame then error("AchievementFrame_ToggleAchievementFrame missing after loading") end
    AchievementFrame_ToggleAchievementFrame()
end)
if not ok then __WowAchievementLoad = tostring(err) end
)LUA"), TEXT("=ToggleAchievementFrame"));
		const FString AchError = AchEvents->GetGlobalString(TEXT("__WowAchievementLoad"));
		if (AchError.IsEmpty() && UIManager->GetFrameManager()->FindFrame(TEXT("AchievementFrame")) >= 0)
		{
			ReleaseGameplayMouseLookForModalUi();
			return;
		}
		UE_LOG(LogWowGameplay, Warning, TEXT("Blizzard_AchievementUI did not open (%s); using the native achievement window"), *AchError);
	}
	if (!GameUI || !ConnectionManager) return;
	if (!AchievementWidget.IsValid())
	{
		AchievementWidget = SNew(SWowAchievementFrame, ConnectionManager).UIManager(UIManager);
		GameUI->AddManagedPanel(TEXT("NativeAchievements"), AchievementWidget.ToSharedRef(), AchievementWidget->GetPanelSize(),
			[this]() { if (AchievementWidget.IsValid() && AchievementWidget->IsOpen()) AchievementWidget->Close(); });
	}
	AchievementWidget->Toggle();
}

void AWowGameplayController::OnQuestLogKey()
{
	if (!QuestLogWidget.IsValid())
	{
		CreateQuestLog();
	}

	if (QuestLogWidget.IsValid())
	{
		if (!QuestLogWidget->IsVisible() && GroupFinderWidget.IsValid() && GroupFinderWidget->IsVisible())
		{
			GroupFinderWidget->SetFinderVisibility(false);
		}
		QuestLogWidget->ToggleVisibility();
	}
}

void AWowGameplayController::OnGroupFinderKey()
{
	if (!GroupFinderWidget.IsValid())
	{
		CreateGroupFinder();
	}
	if (!GroupFinderWidget.IsValid()) return;

	const bool bOpening = !GroupFinderWidget->IsVisible();
	if (bOpening)
	{
		ReleaseGameplayMouseLookForModalUi();
		if (QuestLogWidget.IsValid() && QuestLogWidget->IsVisible()) QuestLogWidget->ToggleVisibility();
		if (UIManager)
		{
			UIManager->HideCharacterPanel();
			if (FWowFrameManager* FrameManager = UIManager->GetFrameManager())
			{
				for (const TCHAR* Name : { TEXT("CharacterFrame"), TEXT("FriendsFrame"), TEXT("WorldMapFrame"), TEXT("SpellBookFrame"), TEXT("TalentFrame"), TEXT("PlayerTalentFrame") })
				{
					const int64 Handle = FrameManager->FindFrame(Name);
					if (Handle >= 0) FrameManager->SetFrameVisible(Handle, false);
				}
			}
		}
	}
	GroupFinderWidget->ToggleVisibility();
}

void AWowGameplayController::CreateGroupFinder()
{
	if (GroupFinderWidget.IsValid() || !ConnectionManager) return;
	GroupFinderWidget = SNew(SWowGroupFinder, ConnectionManager).UIManager(UIManager);
	// A managed window like the others: placed beside them instead of over them, above the FrameXML
	// UI so its close button takes the click, and counted in the two-window limit (Stefan 10.10: it
	// lay over the talent window and its X could not be clicked).
	if (GameUI)
	{
		GameUI->AddManagedPanel(TEXT("NativeGroupFinder"), GroupFinderWidget.ToSharedRef(), FVector2D(930, 600),
			[this]() { if (GroupFinderWidget.IsValid() && GroupFinderWidget->IsVisible()) GroupFinderWidget->SetFinderVisibility(false); });
	}
	else if (GEngine && GEngine->GameViewport)
	{
		TSharedRef<SWidget> Container =
			SNew(SOverlay)
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[ GroupFinderWidget.ToSharedRef() ];
		FWowUiSurfaces::Add(Container, EWowUiZone::Windows, 150);
	}
}

void AWowGameplayController::OnLfgUpdated()
{
	if (!ConnectionManager) return;
	const FWowLfgState& LfgState = ConnectionManager->PacketHandler.LfgState;
	const EWowLfgMode PreviousMode = static_cast<EWowLfgMode>(LastLfgModeForFrameXml);
	if (UIManager && UIManager->GetEventSystem())
	{
		FWowEventSystem* EventSystem = UIManager->GetEventSystem();
		EventSystem->FireEvent(TEXT("LFG_UPDATE"));
		EventSystem->FireEvent(TEXT("LFG_ROLE_UPDATE"));
		if (LfgState.Mode == EWowLfgMode::RoleCheck)
		{
			if (PreviousMode != EWowLfgMode::RoleCheck) EventSystem->FireEvent(TEXT("LFG_ROLE_CHECK_SHOW"));
			EventSystem->FireEvent(TEXT("LFG_ROLE_CHECK_UPDATE"));
		}
		else if (PreviousMode == EWowLfgMode::RoleCheck)
		{
			EventSystem->FireEvent(TEXT("LFG_ROLE_CHECK_HIDE"));
		}

		if (LfgState.Mode == EWowLfgMode::Proposal)
		{
			if (PreviousMode != EWowLfgMode::Proposal) EventSystem->FireEvent(TEXT("LFG_PROPOSAL_SHOW"));
			EventSystem->FireEvent(TEXT("LFG_PROPOSAL_UPDATE"));
		}
	}
	LastLfgModeForFrameXml = static_cast<uint8>(LfgState.Mode);
	const bool bShowQueueEye = LfgState.bQueued
		|| LfgState.Mode == EWowLfgMode::RoleCheck
		|| LfgState.Mode == EWowLfgMode::Proposal
		|| LfgState.Mode == EWowLfgMode::Dungeon;
	if (UIManager && UIManager->GetFrameManager())
	{
		UIManager->GetFrameManager()->SetLfgQueueVisible(bShowQueueEye);
	}

	if (GroupFinderWidget.IsValid())
	{
		GroupFinderWidget->RefreshFromState();
	}
	if (LfgState.Mode == EWowLfgMode::Proposal)
	{
		if (!GroupFinderWidget.IsValid()) CreateGroupFinder();
		if (GroupFinderWidget.IsValid() && !GroupFinderWidget->IsVisible()) OnGroupFinderKey();
	}
}

void AWowGameplayController::OnSocialKey()
{
	if (!UIManager || !UIManager->GetFrameManager()) return;

	FWowFrameManager* FrameManager = UIManager->GetFrameManager();
	if (!FParse::Param(FCommandLine::Get(), TEXT("testsocial")))
	{
		CreateFriendsFrame();
		if (FriendsFrameWidget.IsValid())
		{
			if (!FriendsFrameWidget->IsOpen())
			{
				ReleaseGameplayMouseLookForModalUi();
				if (GroupFinderWidget.IsValid() && GroupFinderWidget->IsVisible())
					GroupFinderWidget->SetFinderVisibility(false);
			}
			FriendsFrameWidget->Toggle();
			return;
		}
	}

	// FrameXML social window (the -testsocial automation still drives it).
	const int64 FriendsHandle = FrameManager->FindFrame(TEXT("FriendsFrame"));
	if (FriendsHandle < 0) return;
	if (!FrameManager->IsFrameVisible(FriendsHandle) && GroupFinderWidget.IsValid() && GroupFinderWidget->IsVisible())
	{
		GroupFinderWidget->SetFinderVisibility(false);
	}
	OnFriendListUpdated();

	FrameManager->SetFrameVisible(FriendsHandle, !FrameManager->IsFrameVisible(FriendsHandle));
}

void AWowGameplayController::CreateFriendsFrame()
{
	if (FriendsFrameWidget.IsValid() || !ConnectionManager || !GameUI) return;
	FriendsFrameWidget = SNew(SWowFriendsFrame, ConnectionManager).UIManager(UIManager);
	TWeakObjectPtr<AWowGameplayController> WeakThis(this);
	FriendsFrameWidget->OnWhisper = [WeakThis](const FString& Name)
	{
		if (!WeakThis.IsValid() || !WeakThis->UIManager) return;
		if (FWowFrameManager* FrameManager = WeakThis->UIManager->GetFrameManager())
			FrameManager->FocusEditBox(FrameManager->FindFrame(TEXT("ChatFrame1EditBox")), FString::Printf(TEXT("/w %s "), *Name));
	};
	FriendsFrameWidget->OnOpenGuildWindow = [WeakThis]()
	{
		if (WeakThis.IsValid()) WeakThis->OnGuildRosterKey();
	};
	GameUI->AddManagedPanel(TEXT("NativeFriends"), FriendsFrameWidget.ToSharedRef(), FriendsFrameWidget->GetPanelSize(),
		[this]() { if (FriendsFrameWidget.IsValid() && FriendsFrameWidget->IsOpen()) FriendsFrameWidget->Close(); });
	// FriendsFrame's art (with its bottom tabs) ends about 30 units from the right and 60 from the
	// bottom of 384 x 512: that margin lay over the chat friends button, so it could not close the window.
	GameUI->SetPanelHitInsets(TEXT("NativeFriends"), FMargin(0.f, 0.f, 30.f / 384.f, 60.f / 512.f));
}

void AWowGameplayController::ToggleMacroFrame()
{
    if (!GameUI || !ConnectionManager) return;
    if (!MacroFrameWidget.IsValid())
    {
        TWeakObjectPtr<AWowGameplayController> WeakThis(this);
        MacroFrameWidget = SNew(SWowMacroFrame)
            .Connection(ConnectionManager)
            .LoadTexture([WeakThis](const FString& Path) -> UTexture2D*
            {
                return WeakThis.IsValid() && WeakThis->UIManager && WeakThis->UIManager->GetFrameManager()
                    ? WeakThis->UIManager->GetFrameManager()->LoadUITexture(Path) : nullptr;
            })
            .GetScale([WeakThis]() -> float
            {
                return WeakThis.IsValid() && WeakThis->UIManager && WeakThis->UIManager->GetFrameManager()
                    ? WeakThis->UIManager->GetFrameManager()->GetUIScale() : 1.f;
            })
            .GetCharacterName([WeakThis]() -> FString
            {
                if (!WeakThis.IsValid() || !WeakThis->ConnectionManager) return FString();
                FWowCharacterInfo Info;
                return WeakThis->ConnectionManager->GetSelectedCharacterInfo(Info) ? Info.Name : FString();
            })
            .UiSound([WeakThis](const TCHAR* Sound)
            {
                if (WeakThis.IsValid() && WeakThis->UIManager && WeakThis->UIManager->GetEventSystem())
                    WeakThis->UIManager->GetEventSystem()->ExecuteLuaString(FString::Printf(TEXT("if PlaySound then PlaySound(\"%s\") end"), Sound), TEXT("=MacroSound"));
            });
        GameUI->AddManagedPanel(TEXT("NativeMacros"), MacroFrameWidget.ToSharedRef(), MacroFrameWidget->GetPanelSize(),
            [this]() { if (MacroFrameWidget.IsValid() && MacroFrameWidget->IsOpen()) MacroFrameWidget->Close(); });
        // Action buttons show macro icons and names: redraw them when a macro changes.
        static FDelegateHandle MacrosChangedHandle;
        FWowMacros::OnChanged().Remove(MacrosChangedHandle);
        MacrosChangedHandle = FWowMacros::OnChanged().AddWeakLambda(this, [this]()
        {
            FireUIEvent(TEXT("UPDATE_MACROS"));
            FireUIEvent(TEXT("ACTIONBAR_SLOT_CHANGED"), {TEXT("0")});
        });
    }
    if (!MacroFrameWidget->IsOpen()) ReleaseGameplayMouseLookForModalUi();
    MacroFrameWidget->Toggle();
}

void AWowGameplayController::RedirectFrameXmlFriendsFrame()
{
	// FriendsMicroButton, the chat friends button and ToggleFriendsFrame() all show the
	// FrameXML FriendsFrame. Hide it at once and toggle the native window, so one click opens
	// (or closes) the social window no matter which path showed the frame.
	if (!UIManager || FParse::Param(FCommandLine::Get(), TEXT("testsocial")) || FParse::Param(FCommandLine::Get(), TEXT("testchatfunctional"))) return;
	FWowFrameManager* FrameManager = UIManager->GetFrameManager();
	if (!FrameManager) return;
	const int64 FriendsHandle = FrameManager->FindFrame(TEXT("FriendsFrame"));
	if (FriendsHandle < 0 || !FrameManager->IsFrameVisible(FriendsHandle)) return;
	// Hide it the way the client does (HideUIPanel), so UIParent's panel slots forget it. Hidden
	// behind UIParent's back it stayed in a slot: every Esc / panel close moved it back in and
	// showed it again, which toggled the native window open and shut (the social window "blinked")
	// and, while it counted as an open panel, hid the chat tabs and buttons (Stefan 11.10).
	if (UIManager->GetEventSystem())
		UIManager->GetEventSystem()->ExecuteLuaString(TEXT("if FriendsFrame and HideUIPanel then HideUIPanel(FriendsFrame) end"), TEXT("=RedirectFriendsFrame"));
	if (FrameManager->IsFrameVisible(FriendsHandle)) FrameManager->SetFrameVisible(FriendsHandle, false);
	CreateFriendsFrame();
	if (!FriendsFrameWidget.IsValid()) return;
	if (!FriendsFrameWidget->IsOpen()) ReleaseGameplayMouseLookForModalUi();
	FriendsFrameWidget->Toggle();
}

void AWowGameplayController::OnSkillsKey()
{
    // r341 (Stefan 14.10): like the client, K opens the character sheet on its Skills tab (the stock
    // SkillFrame, LuaSkillApi.cpp) and closes it when that tab is already showing. The native
    // skills window stays only for UIs without SkillFrame (VR).
    if (UIManager && UIManager->GetFrameManager() && !FWowUiVrHost::IsActive())
    {
        FWowFrameManager* SheetFrames = UIManager->GetFrameManager();
        const int64 SkillPage = SheetFrames->FindFrame(TEXT("SkillFrame"));
        const int64 Sheet = SheetFrames->FindFrame(TEXT("CharacterFrame"));
        if (SkillPage >= 0 && Sheet >= 0)
        {
            const bool bSheetOpen = SheetFrames->IsFrameVisible(Sheet);
            if (bSheetOpen && SheetFrames->IsFrameVisible(SkillPage))
            {
                OnCharacterKey();   // closes the sheet
                return;
            }
            if (!bSheetOpen) OnCharacterKey();
            SheetFrames->SetActiveCharacterSheetTab(3);
            return;
        }
    }
    if (!ConnectionManager || !GameUI) return;
    if (!SkillsWidget.IsValid())
    {
        SkillsWidget=SNew(SWowSkills, &ConnectionManager->PacketHandler);
        GameUI->AddManagedPanel(TEXT("NativeSkills"),SkillsWidget.ToSharedRef(),FVector2D(384,512),
            [this]() { if (SkillsWidget.IsValid()) SkillsWidget->SetVisibility(EVisibility::Hidden); });
    }
    SkillsWidget->Toggle();
}

void AWowGameplayController::CreateQuestLog()
{
	if (QuestLogWidget.IsValid())
	{
		return; // Already created
	}

	// Get the packet handler for quest data
	FWowPacketHandler* PacketHandler = nullptr;
	if (ConnectionManager)
	{
		PacketHandler = &ConnectionManager->PacketHandler;
	}

	if (!PacketHandler)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Cannot create quest log: no packet handler available"));
		return;
	}

	// Create the quest log widget
	QuestLogWidget = SNew(SWowQuestLog, PacketHandler).UIManager(UIManager)
		.OnShowMap(FSimpleDelegate::CreateWeakLambda(this, [this]()
		{
			// QuestLogFrameShowMapButton: open the map on the selected quest (its zone, its POI).
			const uint32 QuestId = QuestLogWidget.IsValid() ? QuestLogWidget->GetSelectedQuestId() : 0;
			if (!WorldMapWidget.IsValid() || !WorldMapWidget->GetVisibility().IsVisible()) OnToggleMap();
			if (QuestId && WorldMapWidget.IsValid()) WorldMapWidget->ShowQuest(QuestId);
		}));

    if(GameUI)GameUI->AddManagedPanel(TEXT("NativeQuestLog"),QuestLogWidget.ToSharedRef(),QuestLogWidget->GetPanelSize(),
        [this]() { if (QuestLogWidget.IsValid() && QuestLogWidget->IsVisible()) QuestLogWidget->ToggleVisibility(); });
}

// The FrameXML engine cannot run Blizzard_TalentUI correctly yet (OnLoad function= scripts,
// panel templates), so the native replica of PlayerTalentFrame is the default.
static TAutoConsoleVariable<int32> CVarWowNativeTalents(TEXT("wow.NativeTalents"), 1,
	TEXT("0 = try the original PlayerTalentFrame (Blizzard_TalentUI) on FrameXML; 1 = native replica (default)."));

void AWowGameplayController::OnTalentKey()
{
	// ToggleTalentFrame: the talent frame cannot be opened before level 10 (SHOW_TALENT_LEVEL).
	if (ConnectionManager)
		if (const FWowPlayerEntity* Me = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer(); Me && Me->GetLevel() < 10)
		{
			if (TalentWindowWidget.IsValid() && TalentWindowWidget->IsVisible()) TalentWindowWidget->ToggleVisibility();
			return;
		}
	// The original 3.3.5 talent frame (Blizzard_TalentUI from the MPQs, with its glyph tab)
	// runs on our FrameXML so it looks and behaves like Wrath and addons can hook it.
	// The native window is the fallback when the addon cannot be loaded.
	if (CVarWowNativeTalents.GetValueOnGameThread() == 0 && UIManager && UIManager->IsUILoaded()
		&& UIManager->GetLuaVM() && UIManager->GetLuaVM()->IsInitialized() && UIManager->GetFrameManager())
	{
		if (TalentWindowWidget.IsValid() && TalentWindowWidget->IsVisible()) TalentWindowWidget->ToggleVisibility();
		UIManager->GetLuaVM()->ExecuteString(TEXT(
			"if TalentFrame_LoadUI then TalentFrame_LoadUI() elseif LoadAddOn then LoadAddOn('Blizzard_TalentUI') end "
			"if PlayerTalentFrame_Toggle then PlayerTalentFrame_Toggle(false, GetActiveTalentGroup()) end"),
			TEXT("=ToggleTalentFrame"));
		if (UIManager->GetFrameManager()->FindFrame(TEXT("PlayerTalentFrame")) >= 0)
		{
			return;
		}
		UE_LOG(LogWowGameplay, Warning, TEXT("Blizzard_TalentUI did not load; using the native talent window"));
	}
	if (!TalentWindowWidget.IsValid())
	{
		CreateTalentWindow();
	}

	if (TalentWindowWidget.IsValid())
	{
		TalentWindowWidget->ToggleVisibility();
	}
}

void AWowGameplayController::CreateTalentWindow()
{
	if (TalentWindowWidget.IsValid())
	{
		return; // Already created
	}

	if (!ConnectionManager)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Cannot create talent window: no connection manager available"));
		return;
	}

	// Create the talent window widget
	TalentWindowWidget = SNew(SWowTalentWindow, ConnectionManager).UIManager(UIManager);

	// Managed like the other native panels: placed left to right with the open windows,
	// above FrameXML (Z 50). The old Z 10 put it underneath the whole FrameXML layer.
	if (GameUI)
	{
		GameUI->AddManagedPanel(TEXT("NativeTalents"), TalentWindowWidget.ToSharedRef(), TalentWindowWidget->GetPanelSize(),
			[this]() { if (TalentWindowWidget.IsValid() && TalentWindowWidget->IsVisible()) TalentWindowWidget->ToggleVisibility(); });
	}
	else if (GEngine && GEngine->GameViewport)
	{
		FWowUiSurfaces::Add(TalentWindowWidget.ToSharedRef(), EWowUiZone::Windows, 210);
	}
}

void AWowGameplayController::CreateQuestTracker()
{
	if (QuestTrackerWidget.IsValid())
	{
		return; // Already created
	}

	// WotLK objectives are a clean text overlay below the minimap. The default
	// tracker has no panel background or outer frame.
	TSharedPtr<SButton> TrackerCollapseButton;
	TSharedRef<SWidget> QuestTrackerContainer =
		SAssignNew(QuestTrackerContainerWidget, SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible /* full-screen layout: only the tracker itself takes the mouse */)
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(TAttribute<FMargin>::CreateLambda([]()
        {
            const float Scale=FWowClientPreferences::GetUIScale();
            const int32 Columns=(FWowClientPreferences::GetWorldOption(TEXT("actionbar5"))?1:0)
                +((FWowClientPreferences::GetWorldOption(TEXT("actionbar5")) && FWowClientPreferences::GetWorldOption(TEXT("actionbar6")))?1:0);
            // VR: start below the whole minimap cluster (clock included); the headset shows the
            // tracker and minimap as separate panels cut from one texture.
            return FMargin(0.0f,FWowUiVrHost::IsActive()?300.0f:220.0f,16.0f+Columns*44.0f*Scale,0.0f);
        }))
		[
			SNew(SBox)
			.WidthOverride(330.0f)
			.MaxDesiredHeight(470.0f)
			[
				SNew(SVerticalBox)

				// Title
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					// The collapse button sits right after the title (Stefan 10.10: it was far off at the
					// right edge of the box).
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SAssignNew(QuestTrackerTitleText, STextBlock)
						.Text(FText::FromString(TEXT("Objectives (0)")))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
						.ShadowOffset(FVector2D(1.0f, 1.0f))
						.ShadowColorAndOpacity(FLinearColor::Black)
						.ColorAndOpacity(FWowWotlkStyle::Gold())
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SAssignNew(TrackerCollapseButton, SButton)
						.ButtonStyle(FCoreStyle::Get(), "NoBorder")
						.ContentPadding(0.0f)
						.Cursor(EMouseCursor::Hand)
						// The toggle runs in Tick from this button's geometry: a FrameXML frame drawn over
						// the tracker took Slate's hit test here, so the button never fired (Stefan 10.10).
						.ClickMethod(EButtonClickMethod::MouseDown)
						.OnClicked_Lambda([]() -> FReply { return FReply::Handled(); })
						[
							// The client's own WatchFrame button art (Interface\Buttons\UI-Panel-QuestHideButton,
							// a 2x2 atlas: top row = collapsed, bottom row = expanded; left = up, right = pushed)
							// instead of "^"/"v" text (Stefan 10.10: "get some real buttons"). 24 units, about
							// 1.5x the stock 16 (Stefan wanted it bigger).
							SNew(SBox)
							.WidthOverride(24.0f)
							.HeightOverride(24.0f)
							[
								SNew(SImage)
								.Image_Lambda([this]() -> const FSlateBrush*
								{
									static FSlateBrush Brushes[4];
									static TWeakObjectPtr<UTexture2D> BrushTexture;
									UTexture2D* Texture = BrushTexture.Get();
									if (!Texture && UIManager && UIManager->GetFrameManager())
									{
										Texture = UIManager->GetFrameManager()->LoadUITexture(TEXT("Interface\\Buttons\\UI-Panel-QuestHideButton"));
										BrushTexture = Texture;
										for (int32 Index = 0; Index < 4; ++Index)
										{
											Brushes[Index] = FSlateBrush();
											Brushes[Index].SetResourceObject(Texture);
											Brushes[Index].ImageSize = FVector2D(24.0f);
											const float U0 = (Index & 1) ? 0.5f : 0.0f, V0 = (Index & 2) ? 0.5f : 0.0f;
											Brushes[Index].SetUVRegion(FBox2f(FVector2f(U0, V0), FVector2f(U0 + 0.5f, V0 + 0.5f)));
										}
									}
									if (!Texture) return FCoreStyle::Get().GetBrush("NoBrush");
									const bool bCollapsed = QuestTrackerWidget.IsValid() && QuestTrackerWidget->GetVisibility() == EVisibility::Collapsed;
									const TSharedPtr<SWidget> CollapseButtonWidget = GWowTrackerCollapseButton.Pin();
									const bool bPushed = CollapseButtonWidget.IsValid() && CollapseButtonWidget->GetCachedGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos())
										&& FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
									return &Brushes[(bCollapsed ? 0 : 2) + (bPushed ? 1 : 0)];
								})
								.ColorAndOpacity_Lambda([this]()
								{
									const TSharedPtr<SWidget> CollapseButtonWidget = GWowTrackerCollapseButton.Pin();
									const bool bHovered = CollapseButtonWidget.IsValid() && CollapseButtonWidget->GetCachedGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos());
									return FSlateColor(bHovered ? FLinearColor(1.2f, 1.2f, 1.2f, 1.0f) : FLinearColor::White);
								})
							]
						]
					]
				]

				// Quest list container
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(QuestTrackerWidget, SVerticalBox)
				]
			]
		];
	GWowTrackerCollapseButton = TrackerCollapseButton;

	// Add to viewport
	if (GEngine && GEngine->GameViewport)
	{
		FWowUiSurfaces::Add(
			QuestTrackerContainer, EWowUiZone::Tracker,
			5 // Z-order: below minimap but above 3D world
		);
		// Ctrl-drag moves the objectives tracker too (it is native, not a FrameXML frame).
		if (GameUI && QuestTrackerContainerWidget.IsValid())
			if (FChildren* TrackerChildren = QuestTrackerContainerWidget->GetChildren(); TrackerChildren && TrackerChildren->Num() > 0)
				GameUI->AddMovableWidget(TEXT("NativeObjectives"), TrackerChildren->GetChildAt(0));
	}

	// Trigger initial update
	UpdateQuestTracker();
}

void AWowGameplayController::UpdateQuestTracker()
{
    FWowPerformanceScope Timing(TEXT("UpdateQuestTracker"), TEXT("world"));
	if (!QuestTrackerWidget.IsValid() || !ConnectionManager)
	{
		return;
	}
	if (GWowUseStockWatchFrame(UIManager))
	{
		// r339: the stock WatchFrame draws the tracked quests (QUEST_LOG_UPDATE refreshes it).
		QuestTrackerWidget->ClearChildren();
		GWowTrackerBlocks.Reset();
		if (QuestTrackerContainerWidget.IsValid()) QuestTrackerContainerWidget->SetVisibility(EVisibility::Collapsed);
		return;
	}

	// Clear existing quest entries
	QuestTrackerWidget->ClearChildren();
	GWowTrackerBlocks.Reset();

	// Get quest log from packet handler
	const TArray<FWowQuestLogEntry>& QuestLog = ConnectionManager->PacketHandler.QuestLog;

	int32 QuestsShown = 0;
	const int32 MaxQuestsToShow = 25;

	for (const FWowQuestLogEntry& QuestEntry : QuestLog)
	{
		if (!QuestEntry.bIsTracked)
		{
			continue;
		}

		if (QuestsShown >= MaxQuestsToShow)
		{
			break;
		}

		// Get quest name (simplified - in a real implementation, you'd look this up from Quest.dbc)
		FString QuestName = QuestEntry.Title;
		if (QuestName.IsEmpty())
		{
			if (const FString* CachedTitle = ConnectionManager->PacketHandler.QuestTitleCache.Find(QuestEntry.QuestId))
			{
				QuestName = *CachedTitle;
			}
			else
			{
				QuestName = FString::Printf(TEXT("Quest %u"), QuestEntry.QuestId);
			}
		}

		// Title colour = the quest log's difficulty colour (yellow/orange/red/green/grey).
		const FWowEntity* TrackerPlayer = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
		const FLinearColor QuestColor = SWowQuestLog::DifficultyColor(QuestEntry.Level,
			TrackerPlayer ? static_cast<int32>(TrackerPlayer->GetField(UnitField::LEVEL)) : 1);
		QuestName = SWowQuestLog::TitleWithLevel(QuestName, QuestEntry.Level);

		// Each quest is one clickable block (WatchFrameLinkButton): left-click opens it in the
		// quest log, Shift untracks, right-click shows it on the map, Ctrl super-tracks it.
		TSharedRef<SVerticalBox> QuestBlock = SNew(SVerticalBox);
		TSharedRef<TWeakPtr<SBorder>> BlockHolder = MakeShared<TWeakPtr<SBorder>>();
		const uint32 BlockQuestId = QuestEntry.QuestId;
		// The super-tracked (Ctrl-click) quest's title is drawn white.
		const bool bWorldTracked = WorldTrackedQuests.Contains(BlockQuestId);
		// The diamond in front of the title marks Ctrl-click tracking; the title keeps its difficulty colour.
		const FLinearColor TitleColor = QuestColor;
		TSharedRef<SBorder> BlockBorder = SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
			.Padding(0.0f)
			.Cursor(EMouseCursor::Hand)
			.OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&)
			{
				// A bag (or another FrameXML window) drawn over the tracker owns the click: pass it on to the game.
				if (IsFrameXmlOverTracker()) return FReply::Unhandled();
				// Every click is handled in Tick from the block's geometry (anywhere on its lines);
				// the widget only keeps the press from reaching the world.
				return FReply::Handled();
			})
			.OnMouseButtonUp_Lambda([this](const FGeometry&, const FPointerEvent&)
			{
				return IsFrameXmlOverTracker() ? FReply::Unhandled() : FReply::Handled();
			})
			[ QuestBlock ];
		*BlockHolder = BlockBorder;
		GWowTrackerBlocks.Emplace(BlockQuestId, TWeakPtr<SWidget>(BlockBorder));
		// r320/r321 (Stefan 14.10: "the usable quest item should appear on the objective tracker"): a
		// quest whose source item is carried (Foreman's Blackjack for Lazy Peons) gets the item as a
		// button left of its block, like WatchFrame's quest item buttons; a click uses it.
		TSharedRef<SHorizontalBox> BlockRow = SNew(SHorizontalBox);
		const uint32 SourceItem = QuestEntry.SourceItemId;
		// r327 (Stefan 14.10: "Verdant Sigil" sat far in from the others): only an item you can use
		// (an on-use spell, like GetQuestLogSpecialItemInfo) gets the button - Verdant Sigil is only
		// the quest's objective - and the icon texture is held, so it is not collected (the empty
		// button was left as the indent).
		const FWowItemTemplateInfo* SourceInfo = SourceItem ? ConnectionManager->PacketHandler.ItemTemplates.Find(SourceItem) : nullptr;
		bool bUsableSource = false;
		for (int32 SpellIndex = 0; SourceInfo && SpellIndex < 5; ++SpellIndex)
			bUsableSource |= SourceInfo->SpellIds[SpellIndex] != 0 && SourceInfo->SpellTriggers[SpellIndex] == 0;
		if (SourceItem && !SourceInfo) ConnectionManager->RequestItemInfo(SourceItem);
		if (bUsableSource && ConnectionManager->CountPlayerItems(SourceItem) > 0 && UIManager && UIManager->GetFrameManager())
		{
			static TMap<uint32, TSharedPtr<FSlateBrush>> GWowTrackerItemBrushes;
			static TMap<uint32, TStrongObjectPtr<UTexture2D>> GWowTrackerItemTextures;
			TSharedPtr<FSlateBrush>& Brush = GWowTrackerItemBrushes.FindOrAdd(SourceItem);
			const FWowItemTemplateInfo* ItemInfo = SourceInfo;
			if (!Brush.IsValid() || !Brush->GetResourceObject())
				if (UTexture2D* ItemTexture = UIManager->GetFrameManager()->LoadUITexture(
					FWowItemPresentation::IconPath(SourceItem, ItemInfo ? ItemInfo->DisplayId : 0)))
				{
					GWowTrackerItemTextures.Add(SourceItem, TStrongObjectPtr<UTexture2D>(ItemTexture));
					Brush = MakeShared<FSlateBrush>();
					Brush->SetResourceObject(ItemTexture);
					Brush->ImageSize = FVector2D(26.0f, 26.0f);
					Brush->DrawAs = ESlateBrushDrawType::Image;
				}
			if (Brush.IsValid())
			{
				const FSlateBrush* ItemBrush = Brush.Get();
				BlockRow->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 1.0f, 5.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(26.0f).HeightOverride(26.0f)
					[
						SNew(SButton)
						.ButtonStyle(FCoreStyle::Get(), "NoBorder")
						.ContentPadding(0.0f)
						.ToolTipText(FText::FromString(ItemInfo ? ItemInfo->Name : FString()))
						.OnClicked_Lambda([this, SourceItem]()
						{
							if (ConnectionManager) ConnectionManager->UseItemByEntry(SourceItem);
							return FReply::Handled();
						})
						[ SNew(SImage).Image(ItemBrush) ]
					]
				];
			}
		}
		BlockRow->AddSlot().FillWidth(1.0f)[ BlockBorder ];
		QuestTrackerWidget->AddSlot()
		.AutoHeight()
		.Padding(0.0f, QuestsShown == 0 ? 1.0f : 7.0f, 0.0f, 0.0f)
		[ BlockRow ];

		// Add quest title (white while hovered, like the watch frame; a diamond marks the super-tracked quest)
		// Quests with a diamond in the world (Ctrl-click) show a small gold diamond before the title.
		TSharedRef<SHorizontalBox> TitleRow = SNew(SHorizontalBox);
		if (bWorldTracked)
		{
			TitleRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(11.0f).HeightOverride(11.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(7.0f).HeightOverride(7.0f)
					.RenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(45.0f))))
					.RenderTransformPivot(FVector2D(0.5f, 0.5f))
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor(1.0f, 0.82f, 0.0f, 1.0f))
						.Padding(0.0f)
					]
				]
			];
		}
		TitleRow->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(QuestName))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
			// r329 (Stefan 14.10: grey titles hard to read on snow): the client's tracker has no
			// background; its fonts (GameFontHighlight/GameFontNormal) carry a black 1 px drop shadow.
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor::Black)
			.ColorAndOpacity_Lambda([BlockQuestId, TitleColor]()
			{
				return FSlateColor(GWowTrackerHoverQuest == BlockQuestId ? FLinearColor::White : TitleColor);
			})
		];
		QuestBlock->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 1.0f)
		[ TitleRow ];

		// Add quest objectives
		bool bAddedStructuredObjective = false;
		for (int32 i = 0; i < QuestEntry.Objectives.Num(); ++i)
		{
			const FWowQuestObjective& Objective = QuestEntry.Objectives[i];

			if (Objective.Required > 0) // Only show objectives that have requirements
			{
				bAddedStructuredObjective = true;
				bool bObjectiveComplete = (Objective.Count >= Objective.Required);
				FLinearColor ObjectiveColor = bObjectiveComplete ? FLinearColor::Green : FLinearColor::White;

				FString ObjectiveText;
				if (!Objective.Text.IsEmpty())
				{
					ObjectiveText = FString::Printf(TEXT("- %s: %u/%u"),
						*Objective.Text, Objective.Count, Objective.Required);
				}
				else if (Objective.ItemId > 0)
				{
					ConnectionManager->RequestItemInfo(Objective.ItemId);
                    const auto* Item=ConnectionManager->PacketHandler.ItemTemplates.Find(Objective.ItemId);
                    ObjectiveText = FString::Printf(TEXT("- %s: %u/%u"),
                        Item?*Item->Name:TEXT("Loading item..."), Objective.Count, Objective.Required);
				}
				else if (Objective.CreatureOrGOId > 0)
				{
					ObjectiveText = FString::Printf(TEXT("- %u/%u Objective %u"),
						Objective.Count, Objective.Required, Objective.CreatureOrGOId);
				}
				else
				{
					ObjectiveText = FString::Printf(TEXT("- Objective %d: %u / %u"),
						i + 1, Objective.Count, Objective.Required);
				}

				QuestBlock->AddSlot()
				.AutoHeight()
				.Padding(8.0f, 0.0f, 0.0f, 1.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(ObjectiveText))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.WrapTextAt(325.0f)
					.ShadowOffset(FVector2D(1.0f, 1.0f))
					.ShadowColorAndOpacity(FLinearColor::Black)
					.ColorAndOpacity(ObjectiveColor)
				];
			}
		}

		if (!bAddedStructuredObjective && !QuestEntry.ObjectiveText.IsEmpty())
		{
			QuestBlock->AddSlot()
			.AutoHeight()
			.Padding(8.0f, 0.0f, 0.0f, 1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("- %s"), *QuestEntry.ObjectiveText)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.WrapTextAt(325.0f)
				.ShadowOffset(FVector2D(1.0f, 1.0f))
				.ShadowColorAndOpacity(FLinearColor::Black)
					.ColorAndOpacity(FWowWotlkStyle::TextLight())
			];
		}

		QuestsShown++;
	}

	if (QuestTrackerTitleText.IsValid())
	{
		QuestTrackerTitleText->SetText(FText::FromString(
			FString::Printf(TEXT("Objectives (%d)"), QuestsShown)));
	}
	if (QuestTrackerContainerWidget.IsValid())
	{
		QuestTrackerContainerWidget->SetVisibility(
			QuestsShown > 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
	}

	// The real tracker is empty when no quests are watched; no placeholder text.
}

void AWowGameplayController::OnQuestTrackerClicked(uint32 QuestId, const FPointerEvent& Event)
{
	if (!ConnectionManager) return;
	const bool bRight = Event.GetEffectingButton() == EKeys::RightMouseButton;
	const bool bShift = Event.IsShiftDown(), bCtrl = Event.IsControlDown();
	// Rebuilding the tracker destroys the clicked widget: act on the next tick.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, QuestId, bRight, bShift, bCtrl]()
	{
		if (!ConnectionManager) return;
		if (bRight)
		{
			if (!WorldMapWidget.IsValid() || !WorldMapWidget->GetVisibility().IsVisible()) OnToggleMap();
			if (WorldMapWidget.IsValid()) WorldMapWidget->ShowQuest(QuestId);
			return;
		}
		if (bCtrl)
		{
			// Ctrl-click adds a quest's diamond, Ctrl-click again removes it (several at once).
			if (WorldTrackedQuests.Remove(QuestId) == 0) WorldTrackedQuests.Add(QuestId);
			QuestDiamondGround.Remove(QuestId);
			if (WorldTrackedQuests.Num()) EnsureQuestDiamondLayer();
			UpdateQuestTracker();
			return;
		}
		if (bShift)
		{
			for (FWowQuestLogEntry& Quest : ConnectionManager->PacketHandler.QuestLog)
				if (Quest.QuestId == QuestId) Quest.bIsTracked = false;
			WorldTrackedQuests.Remove(QuestId);
			ConnectionManager->PacketHandler.OnQuestWatchChanged.Broadcast();
			return;
		}
		if (!QuestLogWidget.IsValid()) CreateQuestLog();
		if (QuestLogWidget.IsValid()) QuestLogWidget->ShowQuest(QuestId);
	}));
}

void AWowGameplayController::EnsureQuestDiamondLayer()
{
	if (QuestDiamondLayer.IsValid() || !GEngine || !GEngine->GameViewport) return;
	TWeakObjectPtr<AWowGameplayController> WeakThis(this);
	SAssignNew(QuestDiamondLayer, SWowQuestDiamond, this,
		[WeakThis](TArray<SWowQuestDiamond::FTarget>& Out)
		{
			if (!WeakThis.IsValid() || !WeakThis->ConnectionManager) return;
			for (const uint32 Id : TArray<uint32>(WeakThis->WorldTrackedQuests))
			{
				SWowQuestDiamond::FTarget T;
				if (!WeakThis->GetQuestDiamondTarget(Id, T.World, T.Yards)) continue;
				if (const FWowQuestLogEntry* Q = WeakThis->ConnectionManager->PacketHandler.QuestLog.FindByPredicate(
					[Id](const FWowQuestLogEntry& E) { return E.QuestId == Id; }))
					T.Label = Q->Title;
				Out.Add(MoveTemp(T));
			}
			// r255: the corpse while you are a ghost (Stefan 12.10: a skull in the world where you died).
			SWowQuestDiamond::FTarget Corpse;
			if (WeakThis->GetCorpseMarker(Corpse.World, Corpse.Yards))
			{
				Corpse.bCorpse = true;
				Corpse.Label = TEXT("Corpse");
				Out.Add(MoveTemp(Corpse));
			}
		});
	if (UIManager && UIManager->GetFrameManager())
		QuestDiamondLayer->SetCorpseIcon(UIManager->GetFrameManager()->LoadUITexture(TEXT("Interface\\TargetingFrame\\UI-RaidTargetingIcon_8")));
	// Above the world and nameplates, under the UI windows.
	FWowUiSurfaces::Add(QuestDiamondLayer.ToSharedRef(), EWowUiZone::World, 41);
}

bool AWowGameplayController::GetCorpseMarker(FVector& OutWorld, float& OutYards) const
{
	if (!ConnectionManager || bUserInterfaceHidden) return false;
	const FWowDeathState& Death = ConnectionManager->PacketHandler.DeathState;
	if (!Death.bGhost || !Death.bHasCorpse || Death.CorpseMap != Death.CurrentMap) return false;
	const FWowEntity* Me = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
	if (!Me) return false;
	OutWorld = FWowCoordinate::WowToUE(FVector(Death.CorpsePos) + FVector(0.0, 0.0, 2.0));
	OutYards = float(FVector2D::Distance(FVector2D(Death.CorpsePos.X, Death.CorpsePos.Y),
		FVector2D(Me->Movement.Position.X, Me->Movement.Position.Y)));
	return true;
}

bool AWowGameplayController::GetQuestDiamondTarget(uint32 QuestId, FVector& OutWorld, float& OutYards)
{
	if (!QuestId || !ConnectionManager || bUserInterfaceHidden || !GetPawn()) return false;
	auto& Handler = ConnectionManager->PacketHandler;
	const FWowQuestLogEntry* Quest = Handler.QuestLog.FindByPredicate([QuestId](const FWowQuestLogEntry& Q) { return Q.QuestId == QuestId; });
	if (!Quest) { WorldTrackedQuests.Remove(QuestId); QuestDiamondGround.Remove(QuestId); return false; }
	const TArray<FWowQuestPOI>* Pois = Handler.QuestPOIs.Find(QuestId);
	const FWowEntity* Me = Handler.EntityManager.GetLocalPlayer();
	if (!Me) return false;
	const auto* Area = LastUiZoneId ? FDbcStore::Get().AreaTable().GetById(LastUiZoneId) : nullptr;
	const uint32 Map = Area ? Area->MapID : MAX_uint32;
	const bool bComplete = Quest->State == 1;
	// Nearest area of the current step: objectives while unfinished, the turn-in once complete.
	const FVector2D PlayerXY(Me->Movement.Position.X, Me->Movement.Position.Y);
	double Best = TNumericLimits<double>::Max();
	FVector2D Target = FVector2D::ZeroVector;
	if (Pois) for (const FWowQuestPOI& Poi : *Pois)
	{
		if (Poi.Points.IsEmpty() || (Map != MAX_uint32 && Poi.MapId != Map)) continue;
		if (bComplete ? Poi.ObjectiveIndex >= 0 : Poi.ObjectiveIndex < 0) continue;
		// r334 (Stefan 14.10: the diamond stayed on Felix's Bucket of Bolts after it was looted):
		// an area whose objective is done is skipped, so the diamond moves on to the nearest
		// unfinished one. POI objective index 0-3 = the creature/object objectives, 4-9 = the
		// item objectives, in the quest's order.
		if (!bComplete && Poi.ObjectiveIndex >= 0)
		{
			const bool bItemPoi = Poi.ObjectiveIndex >= 4;
			int32 Wanted = bItemPoi ? Poi.ObjectiveIndex - 4 : Poi.ObjectiveIndex;
			const FWowQuestObjective* PoiObjective = nullptr;
			for (const FWowQuestObjective& Objective : Quest->Objectives)
			{
				if ((Objective.ItemId != 0) != bItemPoi) continue;
				if (Wanted-- == 0) { PoiObjective = &Objective; break; }
			}
			if (PoiObjective && PoiObjective->Required > 0 && PoiObjective->Count >= PoiObjective->Required) continue;
		}
		FVector2D Sum = FVector2D::ZeroVector;
		for (const FVector2D& P : Poi.Points) Sum += P;
		const FVector2D Mid = Sum / Poi.Points.Num();
		const double D = FVector2D::DistSquared(Mid, PlayerXY);
		if (D < Best) { Best = D; Target = Mid; }
	}
	// No quest POI from the server (some servers send none): the diamond can still sit on the
	// nearest objective creature/object or the turn-in NPC around you (Stefan: diamonds never showed).
	const bool bNoPoi = Best == TNumericLimits<double>::Max();
	if (bNoPoi) Target = PlayerXY;
	// Exact spot when the server has shown us what the step points at: the objective's creature
	// or object, or (quest complete) the NPC waiting for the turn-in. Their height is exact,
	// indoors, in caves and on towers, so no ground guess is needed (Stefan: "check ALL
	// diamonds"). Only within 80 yards of the POI centre, nearest first.
	{
		const FWowEntity* Anchor = nullptr;
		// r473 (Stefan: two diamonds merged into one when he walked behind a hill): a "?" only
		// says the NPC has *some* quest to turn in, not which one, so every complete quest took the
		// nearest "?" within 80 yards of its turn-in area: two quests landed on the same NPC.
		// With a turn-in POI (placed on the quest's ender) the NPC must stand at it (20 yards).
		double AnchorDistSq = bNoPoi ? 150.0 * 150.0 : bComplete ? 20.0 * 20.0 : 80.0 * 80.0;
		for (const auto& Pair : Handler.EntityManager.GetAll())
		{
			const FWowEntity* E = Pair.Value.Get();
			if (!E || E == Me || (!E->IsUnit() && !E->IsGameObject())) continue;
			bool bMatch = false;
			if (bComplete)
			{
				const uint8* Status = Handler.QuestGiverStatuses.Find(E->Guid);
				bMatch = E->IsUnit() && Status && WowQuestGiverStatus::IsReadyForTurnIn(*Status);
			}
			else
			{
				if (E->IsUnit() && E->GetMaxHealth() > 0 && E->GetHealth() == 0) continue; // dead mobs
				for (const FWowQuestObjective& Objective : Quest->Objectives)
				{
					if (!Objective.CreatureOrGOId || (Objective.Required > 0 && Objective.Count >= Objective.Required)) continue;
					// Spell/kill objectives on game objects are sent negative (high bit set).
					const int32 Raw = static_cast<int32>(Objective.CreatureOrGOId);
					const bool bObject = Raw < 0;
					const uint32 Entry = static_cast<uint32>(bObject ? -Raw : Raw);
					if (E->Entry == Entry && (bObject ? E->IsGameObject() : E->IsUnit())) { bMatch = true; break; }
				}
			}
			if (!bMatch) continue;
			const double DistSq = FVector2D::DistSquared(FVector2D(E->Movement.Position.X, E->Movement.Position.Y), Target);
			if (DistSq < AnchorDistSq) { AnchorDistSq = DistSq; Anchor = E; }
		}
		if (Anchor && !Anchor->Movement.Position.IsZero())
		{
			OutWorld = FWowCoordinate::WowToUE(FVector(Anchor->Movement.Position) + FVector(0.0, 0.0, 3.0));
			OutYards = float(FVector2D::Distance(FVector2D(Anchor->Movement.Position.X, Anchor->Movement.Position.Y), PlayerXY));
			return true;
		}
	}
	if (bNoPoi) return false;
	// Ground height under the target (re-traced each second, or when the target moves).
	const double Now = FPlatformTime::Seconds();
	FQuestDiamondGround& Ground = QuestDiamondGround.FindOrAdd(QuestId);
	if (Now >= Ground.NextTrace || !Target.Equals(Ground.XY, 0.5))
	{
		Ground.NextTrace = Now + 1.0;
		Ground.XY = Target;
		Ground.Z = Me->Movement.Position.Z;
		// Every floor under the target, top to bottom (mountain top, cave roof, cave floor,
		// building roof, building floor ...); take the one closest to our own height. A single
		// trace from high above put the diamond on the mountain over a cave or in a ceiling.
		const double MyZ = Me->Movement.Position.Z;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(QuestDiamondGround), true, GetPawn());
		double BestZ = MyZ; double BestDelta = TNumericLimits<double>::Max();
		double StartZ = MyZ + 500.0;
		for (int32 Layer = 0; Layer < 8 && GetWorld(); ++Layer)
		{
			const FVector Top = FWowCoordinate::WowToUE(FVector(Target.X, Target.Y, StartZ));
			const FVector Bottom = FWowCoordinate::WowToUE(FVector(Target.X, Target.Y, MyZ - 500.0));
			FHitResult Hit;
			if (!GetWorld()->LineTraceSingleByObjectType(Hit, Top, Bottom, FCollisionObjectQueryParams(ECC_WorldStatic), Params)) break;
			const double HitZ = FWowCoordinate::UEToWow(Hit.ImpactPoint).Z;
			if (FMath::Abs(HitZ - MyZ) < BestDelta) { BestDelta = FMath::Abs(HitZ - MyZ); BestZ = HitZ; }
			StartZ = HitZ - 1.0; // continue below this surface
		}
		Ground.Z = static_cast<float>(BestZ);
	}
	OutWorld = FWowCoordinate::WowToUE(FVector(Target.X, Target.Y, Ground.Z + 3.f));
	OutYards = float(FVector2D::Distance(Target, PlayerXY));
	return true;
}

void AWowGameplayController::OnPlayerInventoryUpdated()
{
	if (UIManager)
	{
		UIManager->UpdateInventory();
	}

	// Selling to a merchant: the original client plays the coin sound when the
	// sale's money arrives. Money rising while standing at the open vendor.
	if (ConnectionManager)
	{
		auto& Entities = ConnectionManager->PacketHandler.EntityManager;
		static uint64 MoneyOwner = 0;
		static uint32 LastMoney = 0;
		if (const FWowEntity* Me = Entities.GetLocalPlayer())
		{
			const uint32 Money = Me->GetField(PlayerField::COINAGE);
			const uint64 VendorGuid = ConnectionManager->PacketHandler.ActiveVendorGuid;
			bool bAtVendor = false;
			if (VendorGuid && GetPawn())
				if (const TObjectPtr<AActor>* Vendor = SpawnedEntityActors.Find(VendorGuid))
					bAtVendor = *Vendor && FVector::Dist((*Vendor)->GetActorLocation(), GetPawn()->GetActorLocation()) < 1200.0f;
			if (MoneyOwner == Me->Guid && Money > LastMoney && bAtVendor)
				if (AWowAudioManager* Audio = Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(this, AWowAudioManager::StaticClass())))
					Audio->PlayUiSound(TEXT("LOOTWINDOWCOINSOUND"));
			MoneyOwner = Me->Guid;
			LastMoney = Money;
		}
	}

	// FrameXML handlers require the bag/equipment slot, not an empty event.
	for (int32 Bag = 0; Bag <= 4; ++Bag)
		FireUIEvent(TEXT("BAG_UPDATE"), {FString::FromInt(Bag)});
	// r282: the bank's slots (1-28 items, 29-35 bags) and bank bags while the bank is open.
	if (ConnectionManager && ConnectionManager->ActiveBankerGuid)
	{
		for (int32 Slot = 1; Slot <= 35; ++Slot)
			FireUIEvent(TEXT("PLAYERBANKSLOTS_CHANGED"), {FString::FromInt(Slot)});
		FireUIEvent(TEXT("PLAYERBANKBAGSLOTS_CHANGED"));
		for (int32 Bag = 5; Bag <= 11; ++Bag)
			FireUIEvent(TEXT("BAG_UPDATE"), {FString::FromInt(Bag)});
	}
	// Item buttons on the action bars grey out at 0 left (IsUsableAction): refresh them with the bags
	// (Stefan 10.10: a used-up potion's icon stayed bright).
	FireUIEvent(TEXT("ACTIONBAR_UPDATE_USABLE"));
	FireUIEvent(TEXT("PLAYER_MONEY"));
	for (int32 Slot = 1; Slot <= 19; ++Slot)
		FireUIEvent(TEXT("PLAYER_EQUIPMENT_CHANGED"), {FString::FromInt(Slot)});
	FireUIEvent(TEXT("UNIT_INVENTORY_CHANGED"), {TEXT("player")});
	FireUIEvent(TEXT("UPDATE_INVENTORY_DURABILITY"));
}

void AWowGameplayController::OnUnitHealthUpdate(uint64 EntityGuid)
{
	// UNIT_DIED for the combat log on an alive -> 0 HP transition (units spawning dead don't count).
	if (ConnectionManager)
	{
		if (const FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(EntityGuid); Entity && Entity->IsUnit())
		{
			const bool bDead = Entity->GetMaxHealth() > 0 && Entity->GetHealth() <= 0;
			if (!bDead) { if (Entity->GetMaxHealth() > 0) CombatLogAliveUnits.Add(EntityGuid); }
			else if (CombatLogAliveUnits.Remove(EntityGuid) > 0)
			{
				FWowCombatLogEntry Died;
				Died.Event = TEXT("UNIT_DIED");
				Died.DestGuid = EntityGuid;
				OnCombatLogEntry(Died);
			}
		}
	}
	if (!UIManager) return;

	FWowEventSystem* EventSystem = UIManager->GetEventSystem();
	if (!EventSystem) return;

	// Determine which unit was updated
	FString UnitToken;
	if (ConnectionManager && EntityGuid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
	{
		UnitToken = TEXT("player");
	}
	else if (ConnectionManager && EntityGuid == static_cast<uint64>(ConnectionManager->GetTargetGuid()))
	{
		UnitToken = TEXT("target");
	}
	else
	{
		// For other units, we don't currently fire UNIT_HEALTH events
		// TODO: Add support for party/raid members if needed
		return;
	}

	// Fire UNIT_HEALTH event with unit token
	TArray<FString> Args;
	Args.Add(UnitToken);
	EventSystem->FireEvent(TEXT("UNIT_HEALTH"), Args);
}

void AWowGameplayController::OnUnitPowerUpdate(uint64 EntityGuid)
{
	if (!UIManager) return;

	FWowEventSystem* EventSystem = UIManager->GetEventSystem();
	if (!EventSystem) return;

	// Determine which unit was updated
	FString UnitToken;
	if (ConnectionManager && EntityGuid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid)
	{
		UnitToken = TEXT("player");
	}
	else if (ConnectionManager && EntityGuid == static_cast<uint64>(ConnectionManager->GetTargetGuid()))
	{
		UnitToken = TEXT("target");
	}
	else
	{
		// For other units, we don't currently fire UNIT_POWER events
		// TODO: Add support for party/raid members if needed
		return;
	}

	// Fire UNIT_POWER and UNIT_MANA events with unit token
	TArray<FString> Args;
	Args.Add(UnitToken);
	EventSystem->FireEvent(TEXT("UNIT_POWER"), Args);
	EventSystem->FireEvent(TEXT("UNIT_MANA"), Args);  // WoW 3.3.5 compatibility alias
}

void AWowGameplayController::OnInitialSpells(const TArray<uint32>& SpellIds)
{
	UE_LOG(LogWowGameplay, Log, TEXT("Received initial spells: %d spells"), SpellIds.Num());

	if (ConnectionManager)
	{
		ConnectionManager->SendRequestPetInfo();
	}

	// Fire spell book events
	FireUIEvent(TEXT("SPELLS_CHANGED"));
}

void AWowGameplayController::OnActionButtonsUpdated()
{
	UE_LOG(LogWowGameplay, Log, TEXT("Action buttons updated"));

	if (ActionBarWidget.IsValid())
	{
		ActionBarWidget->RefreshActionButtons();
	}
	// Refresh every server-backed action slot, not just the first main bar.
	const int32 SlotCount = ConnectionManager
		? FMath::Min(ConnectionManager->PacketHandler.ActionButtons.Num(), 144)
		: 0;
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		FireUIEvent(TEXT("ACTIONBAR_SLOT_CHANGED"), {FString::Printf(TEXT("%d"), SlotIndex + 1)});
	}
	FireUIEvent(TEXT("ACTIONBAR_UPDATE_STATE"));

	// Apply the authoritative packed-slot data after FrameXML has processed its
	// events. Some stock multi-bar scripts depend on saved CVar state and reset
	// their icons to UI-QuickslotRed when those CVars are absent.
	if (UIManager && UIManager->GetFrameManager() && ConnectionManager)
	{
		UIManager->GetFrameManager()->RefreshActionButtonIcons(
			ConnectionManager->PacketHandler.ActionButtons);
	}
}

void AWowGameplayController::OnPetBarUpdated(const FWowPetActionBarState& PetActionBar)
{
	UE_LOG(LogWowGameplay, Log, TEXT("Pet bar updated: pet=%llu hasBar=%d"),
		PetActionBar.PetGuid, PetActionBar.HasActionBar() ? 1 : 0);

	FireUIEvent(TEXT("UNIT_PET"), {TEXT("player")});
	FireUIEvent(TEXT("PET_UI_UPDATE"));
	FireUIEvent(TEXT("PET_BAR_UPDATE"));
	FireUIEvent(TEXT("PET_BAR_UPDATE_USABLE"));
	FireUIEvent(TEXT("PET_BAR_UPDATE_COOLDOWN"));
	FireUIEvent(TEXT("ACTIONBAR_UPDATE_USABLE"));
	FireUIEvent(TEXT("ACTIONBAR_UPDATE_COOLDOWN"));
	FireUIEvent(TEXT("ACTIONBAR_UPDATE_STATE"));
	FireUIEvent(TEXT("SPELL_UPDATE_USABLE"));

	if (PetActionBar.HasActionBar())
	{
		FireUIEvent(TEXT("PET_BAR_SHOWGRID"));
	}
	else
	{
		FireUIEvent(TEXT("PET_BAR_HIDEGRID"));
	}
}

int32 AWowGameplayController::QueueSpellVisualKit(uint64 OwnerGuid, uint64 AnchorGuid,
	uint32 SpellId, uint32 KitId, ESpellVisualPhase Phase, bool bPersistent)
{
	if (KitId == 0)
	{
		return 0;
	}

	const FDbcStore& DbcStore = FDbcStore::Get();
	const auto* KitEntry = DbcStore.SpellVisualKits().GetById(KitId);
	if (!KitEntry)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("SpellVisualKit %u is missing for spell %u"), KitId, SpellId);
		return 0;
	}
	// The server sometimes sends the same visual twice in one burst (a level-up cast
	// 59915 twice). Playing the same kit on the same unit within 0.3 s doubled the effect.
	{
		static TMap<uint32, double> RecentKits;
		const double Now = FPlatformTime::Seconds();
		const uint32 Key = HashCombineFast(GetTypeHash(OwnerGuid), HashCombineFast(GetTypeHash(AnchorGuid), KitId));
		if (const double* Last = RecentKits.Find(Key); Last && Now - *Last < 0.3)
			return 0;
		RecentKits.Add(Key, Now);
		if (RecentKits.Num() > 256)
			for (auto It = RecentKits.CreateIterator(); It; ++It) if (Now - It.Value() > 2.0) It.RemoveCurrent();
	}

    // The kit sound is independent of whether it has renderable mesh slots.
    if(KitEntry->SoundID && Phase!=ESpellVisualPhase::Aura)
    {
        AActor* Source=ConnectionManager && OwnerGuid==ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid
            ? GetPawn() : SpawnedEntityActors.FindRef(AnchorGuid).Get();
        if(IsValid(Source) && GetPawn() && FVector::DistSquared(Source->GetActorLocation(),GetPawn()->GetActorLocation())<FMath::Square(5000.f))
            if(auto* Audio=Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(GetWorld(),AWowAudioManager::StaticClass())))
            {
                // Remember the caster's spell sound so an interrupt silences it at once.
                if(UAudioComponent* Played=Audio->PlayWorldSoundEffect(KitEntry->SoundID,Source->GetActorLocation()))
                {
                    TArray<TWeakObjectPtr<UAudioComponent>>& Sounds=CasterSpellSounds.FindOrAdd(OwnerGuid);
                    Sounds.RemoveAll([](const TWeakObjectPtr<UAudioComponent>& S){return !S.IsValid();});
                    Sounds.Add(Played);
                }
            }
    }
	struct FKitSlot
	{
		uint32 EffectId;
		ESpellVisualSlot Slot;
	};
	const FKitSlot Slots[] = {
		{KitEntry->HeadEffectID, ESpellVisualSlot::Head},
		{KitEntry->ChestEffectID, ESpellVisualSlot::Chest},
		{KitEntry->BaseEffectID, ESpellVisualSlot::Base},
		{KitEntry->LeftHandEffectID, ESpellVisualSlot::LeftHand},
		{KitEntry->RightHandEffectID, ESpellVisualSlot::RightHand},
		{KitEntry->BreathEffectID, ESpellVisualSlot::Breath},
		{KitEntry->LeftWeaponEffectID, ESpellVisualSlot::LeftWeapon},
		{KitEntry->RightWeaponEffectID, ESpellVisualSlot::RightWeapon},
		{KitEntry->SpecialEffectID[0], ESpellVisualSlot::Base},
		{KitEntry->SpecialEffectID[1], ESpellVisualSlot::Base},
		{KitEntry->SpecialEffectID[2], ESpellVisualSlot::Base},
		{KitEntry->WorldEffectID, ESpellVisualSlot::Base}
	};

	int32 QueuedCount = 0;
	for (const FKitSlot& Slot : Slots)
	{
		if (Slot.EffectId == 0)
		{
			continue;
		}
		const auto* EffectEntry = DbcStore.SpellVisualEffectNames().GetById(Slot.EffectId);
		if (!EffectEntry || EffectEntry->FilePath.IsEmpty())
		{
			continue;
		}

		FPendingSpellVisual& Request = PendingSpellVisuals.AddDefaulted_GetRef();
		Request.OwnerGuid = OwnerGuid;
		Request.AnchorGuid = AnchorGuid;
		Request.SpellId = SpellId;
		Request.ModelPath = NormalizeGameObjectModelPath(EffectEntry->FilePath);
		Request.Scale = FMath::Clamp(EffectEntry->Scale > 0.0f ? EffectEntry->Scale : 1.0f, 0.05f, 8.0f);
		Request.Slot = Slot.Slot;
		Request.Phase = Phase;
		Request.bPersistent = bPersistent;
		++QueuedCount;
	}

	// Models hung on attachment points (SpellVisualKitModelAttach.dbc), e.g. custom "Angelic
	// Wings" auras whose kit has no head/chest/base effect at all (r262).
	for (const FWowSpellModelAttach& Attach : GetSpellModelAttaches(CachedMpq, KitId))
	{
		const auto* EffectEntry = DbcStore.SpellVisualEffectNames().GetById(Attach.EffectNameId);
		if (!EffectEntry || EffectEntry->FilePath.IsEmpty()) continue;
		FPendingSpellVisual& Request = PendingSpellVisuals.AddDefaulted_GetRef();
		Request.OwnerGuid = OwnerGuid;
		Request.AnchorGuid = AnchorGuid;
		Request.SpellId = SpellId;
		Request.ModelPath = NormalizeGameObjectModelPath(EffectEntry->FilePath);
		Request.Scale = FMath::Clamp(EffectEntry->Scale > 0.0f ? EffectEntry->Scale : 1.0f, 0.05f, 8.0f);
		Request.Slot = ESpellVisualSlot::Attachment;
		Request.AttachmentId = uint8(FMath::Min<uint32>(Attach.AttachmentId, 255u));
		Request.AttachOffset = FVector(Attach.Offset.Y, Attach.Offset.X, Attach.Offset.Z) * FWowCoordinate::SCALE;
		Request.AttachRotation = FRotator(FMath::RadiansToDegrees(Attach.Pitch), FMath::RadiansToDegrees(Attach.Yaw),
			FMath::RadiansToDegrees(Attach.Roll));
		Request.Phase = Phase;
		Request.bPersistent = bPersistent;
		++QueuedCount;
	}

	UE_LOG(LogWowGameplay, Log,
		TEXT("Queued %d authored spell effects: spell=%u kit=%u phase=%d owner=%llu anchor=%llu"),
		QueuedCount, SpellId, KitId, static_cast<int32>(Phase), OwnerGuid, AnchorGuid);
	return QueuedCount;
}

int32 AWowGameplayController::SpawnSpellVisualEffect(uint64 CasterGuid, uint64 SpellTargetGuid,
	uint32 SpellId, ESpellVisualPhase Phase)
{
	const FDbcStore& DbcStore = FDbcStore::Get();
	if (!DbcStore.IsDeferredLoaded())
	{
		UE_LOG(LogWowGameplay, Warning,
			TEXT("Spell visual DBCs are not ready for spell %u"), SpellId);
		return 0;
	}

	const auto* SpellEntry = DbcStore.Spells().GetById(SpellId);
	if (!SpellEntry)
	{
		return 0;
	}
	// r302 (Stefan, OG comparison): passive spells show nothing in the original client. The
	// heirloom trinkets' passive auras (Swift Hand of Justice 59906, Discerning Eye of the Beast
	// 59915, both SpellVisual 7578) drew a green shield and purple hand swirls on every
	// re-application; their actual procs (59913 heal, 59914 mana) have no visual at all.
	if ((SpellEntry->Attributes & 0x40u) != 0) // SPELL_ATTR0_PASSIVE
	{
		return 0;
	}
	// UE-made effects for reworked spells replace the authored kit for that phase.
	if (TrySpawnCustomSpellEffect(CasterGuid, SpellTargetGuid, SpellId, Phase))
	{
		return 1;
	}

    if(ConnectionManager)
        if(const auto* Target=ConnectionManager->PacketHandler.EntityManager.Find(SpellTargetGuid))
            if(Target->IsItem())SpellTargetGuid=CasterGuid;
	const uint32 SpellVisualId = SpellEntry->SpellVisual[0] != 0
		? SpellEntry->SpellVisual[0]
		: SpellEntry->SpellVisual[1];
	const auto* VisualEntry = DbcStore.SpellVisuals().GetById(SpellVisualId);
	if (!VisualEntry)
	{
		return 0;
	}

	uint32 KitId = 0;
	bool bPersistent = false;
	uint64 AnchorGuid = CasterGuid;
	switch (Phase)
	{
	case ESpellVisualPhase::Precast:
		KitId = VisualEntry->PrecastKit;
		bPersistent = true;
		break;
	case ESpellVisualPhase::Cast:
		KitId = VisualEntry->CastKit;
		break;
	case ESpellVisualPhase::Impact:
		KitId = VisualEntry->ImpactKit;
		AnchorGuid = SpellTargetGuid;
		break;
	case ESpellVisualPhase::Channel:
		KitId = VisualEntry->ChannelKit;
		bPersistent = true;
		break;
    case ESpellVisualPhase::Aura:
        KitId = VisualEntry->StateKit;
        AnchorGuid = SpellTargetGuid;
        bPersistent = true;
        break;
	case ESpellVisualPhase::ChannelDone:
		KitId = VisualEntry->StateDoneKit;
		break;
	}

	if (AnchorGuid == 0 && ConnectionManager)
	{
		if (const FWowEntity* CasterEntity =
			ConnectionManager->PacketHandler.EntityManager.Find(CasterGuid))
		{
			AnchorGuid = CasterEntity->GetField64(UnitField::TARGET);
		}
	}
	if (AnchorGuid == 0)
	{
		// Self-heals frequently omit an explicit target in client-visible data.
		// Anchoring to the caster keeps the impact visible over the first-person body.
		AnchorGuid = CasterGuid;
	}

	int32 QueuedEffects = QueueSpellVisualKit(
		CasterGuid, AnchorGuid, SpellId, KitId, Phase, bPersistent);
	TSet<uint32> QueuedKits;
	if (KitId != 0) QueuedKits.Add(KitId);
	auto QueueAdditional = [&](uint32 AdditionalKit, uint64 AdditionalAnchor, bool bAdditionalPersistent)
	{
		if (AdditionalKit == 0 || QueuedKits.Contains(AdditionalKit)) return;
		QueuedKits.Add(AdditionalKit);
		QueuedEffects += QueueSpellVisualKit(CasterGuid, AdditionalAnchor, SpellId,
			AdditionalKit, Phase, bAdditionalPersistent);
	};
	if (Phase == ESpellVisualPhase::Cast)
	{
		QueueAdditional(VisualEntry->InstantAreaKit, AnchorGuid, false);
		// r605 (Stefan: Vanish should have the big smoke, "but with more smoke"): Vanish's cast kit
		// (404, vanish_cast_base) is played a second time, so the puff is twice as dense.
		if (KitId == 404)
			QueuedEffects += QueueSpellVisualKit(CasterGuid, AnchorGuid, SpellId, KitId, Phase, false);
	}
	else if (Phase == ESpellVisualPhase::Impact)
	{
		QueueAdditional(VisualEntry->TargetImpactKit, AnchorGuid, false);
		QueueAdditional(VisualEntry->ImpactAreaKit, AnchorGuid, false);
		QueueAdditional(VisualEntry->CasterImpactKit, CasterGuid, false);
	}
	else if (Phase == ESpellVisualPhase::Aura)
	{
		QueueAdditional(VisualEntry->PersistentAreaKit, AnchorGuid, true);
	}
	return QueuedEffects;
}

AActor* AWowGameplayController::ResolveSpellActor(uint64 Guid) const
{
	if (!ConnectionManager)
	{
		return nullptr;
	}
	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	if (Guid == LocalGuid)
	{
		return GetPawn();
	}
	if (const TObjectPtr<AActor>* RiderActor = SpawnedEntityRiderActors.Find(Guid))
	{
		return RiderActor->Get();
	}
	if (const TObjectPtr<AActor>* FoundActor = SpawnedEntityActors.Find(Guid))
	{
		return FoundActor->Get();
	}
	return nullptr;
}

void AWowGameplayController::ProcessPendingSpellVisuals()
{
	const double Now = FPlatformTime::Seconds();
	TArray<uint64> OrphanedAuraOwners;
	for (int32 Index = ActiveSpellVisuals.Num() - 1; Index >= 0; --Index)
	{
		FActiveSpellVisual& Active = ActiveSpellVisuals[Index];
		// r406: a persistent aura whose parent mesh was destroyed (model rebuild after login, gear
		// or HD change) hangs where it was, often at the world origin. Drop it so the aura sync
		// spawns it again on the current model.
		bool bOrphaned = false;
		if (Active.bPersistent && Active.Phase == ESpellVisualPhase::Aura)
			if (const AActor* Visual = Active.Actor.Get(); Visual && Visual->Tags.Contains(TEXT("WowAttachedVisual")))
			{
				const USceneComponent* Parent = Visual->GetRootComponent() ? Visual->GetRootComponent()->GetAttachParent() : nullptr;
				bOrphaned = !Parent || !IsValid(Parent) || !IsValid(Parent->GetOwner());
				if (bOrphaned) OrphanedAuraOwners.AddUnique(Active.OwnerGuid);
			}
		if (!Active.Actor.IsValid() || bOrphaned || (!Active.bPersistent && Now >= Active.ExpireAt))
		{
			if (AActor* Actor = Active.Actor.Get())
			{
				RetainedSpellVisualActors.Remove(Actor);
				Actor->Destroy();
			}
			ActiveSpellVisuals.RemoveAtSwap(Index);
		}
	}
	for (const uint64 AuraOwner : OrphanedAuraOwners) SyncAuraVisuals(AuraOwner);
	OrphanedAuraOwners.Reset();

	if (PendingSpellVisuals.IsEmpty())
	{
		return;
	}

	const FPendingSpellVisual Request = MoveTemp(PendingSpellVisuals[0]);
	PendingSpellVisuals.RemoveAt(0, 1, EAllowShrinking::No);
	if ((Request.Phase == ESpellVisualPhase::Precast &&
		!ActiveTimedCastSpells.Contains(Request.OwnerGuid)) ||
		(Request.Phase == ESpellVisualPhase::Channel &&
		!ActiveChannelSpells.Contains(Request.OwnerGuid)))
	{
		return;
	}

	CacheWorldResources();
	AActor* AnchorActor = ResolveSpellActor(Request.Phase == ESpellVisualPhase::Missile
		? Request.OwnerGuid : Request.AnchorGuid);
	if (!AnchorActor || !CachedMpq || !CachedAssetCache || !GetWorld())
	{
		return;
	}

	// r406 (Stefan: Lightning Shield's orb was gone): a character's extra skeletal meshes (separate
	// hair, HD parts) are rebuilt with every model change; an aura attached to one of them was left
	// behind when it was destroyed. Always use the character's main mesh.
	USkeletalMeshComponent* CharacterMesh = Cast<ACharacter>(AnchorActor) ? Cast<ACharacter>(AnchorActor)->GetMesh()
		: AnchorActor->FindComponentByClass<USkeletalMeshComponent>();
	FName SocketName = NAME_None;
	const FBox Bounds = CharacterMesh ? CharacterMesh->Bounds.GetBox()
		: AnchorActor->GetComponentsBoundingBox(true);
	FVector EffectLocation = AnchorActor->GetActorLocation();
	if (Bounds.IsValid) EffectLocation.Z = Bounds.Min.Z;
	const float Height = Bounds.IsValid ? FMath::Max(Bounds.GetSize().Z, 100.0f) : 180.0f;

	// r699: a crit's impact hits the head instead of the chest.
	ESpellVisualSlot Slot = Request.Slot;
	if (Request.Phase == ESpellVisualPhase::Impact && Slot == ESpellVisualSlot::Chest
		&& WowConsumeCritHit(Request.OwnerGuid, Request.AnchorGuid, Request.SpellId))
		Slot = ESpellVisualSlot::Head;
	switch (Slot)
	{
	case ESpellVisualSlot::Head:
		SocketName = TEXT("WowSpellHead");
		EffectLocation += FVector(0.0f, 0.0f, Height * 0.9f);
		break;
	case ESpellVisualSlot::Chest:
		SocketName = TEXT("WowSpellChest");
		EffectLocation += FVector(0.0f, 0.0f, Height * 0.62f);
		break;
	case ESpellVisualSlot::LeftHand:
	case ESpellVisualSlot::LeftWeapon:
		SocketName = TEXT("WowSpellLeftHand");
		EffectLocation += AnchorActor->GetActorRightVector() * -35.0f + FVector(0.0f, 0.0f, Height * 0.55f);
		break;
	case ESpellVisualSlot::RightHand:
	case ESpellVisualSlot::RightWeapon:
		SocketName = TEXT("WowSpellRightHand");
		EffectLocation += AnchorActor->GetActorRightVector() * 35.0f + FVector(0.0f, 0.0f, Height * 0.55f);
		break;
	case ESpellVisualSlot::Breath:
		SocketName = TEXT("WowSpellBreath");
		EffectLocation += AnchorActor->GetActorForwardVector() * 30.0f + FVector(0.0f, 0.0f, Height * 0.85f);
		break;
	case ESpellVisualSlot::Base:
		SocketName = TEXT("WowSpellBase");
		break;
	case ESpellVisualSlot::Attachment:
		SocketName = FName(*FString::Printf(TEXT("WowAttach_%d"), Request.AttachmentId));
		EffectLocation += FVector(0.0f, 0.0f, Height * 0.65f);
		break;
	default:
		break;
	}

	if (CharacterMesh && SocketName != NAME_None && CharacterMesh->DoesSocketExist(SocketName))
	{
		EffectLocation = CharacterMesh->GetSocketLocation(SocketName);
	}

	const uint32 UniqueId = HashCombineFast(GetTypeHash(Request.OwnerGuid),
		HashCombineFast(GetTypeHash(Request.SpellId), GetTypeHash(Request.ModelPath)));
    const auto* PresentationSpell = FDbcStore::Get().Spells().GetById(Request.SpellId);
    // Holy Light's cylindrical billboard needs a separate camera-facing pass.
    // Preserve the existing veil until that authored constraint is supported.
    const bool bHolyLightHead = Request.ModelPath.Contains(TEXT("HolyLight_"), ESearchCase::IgnoreCase)
        && Request.ModelPath.EndsWith(TEXT("_Head.m2"), ESearchCase::IgnoreCase);
    // Bone/texture/colour animation for every spell M2 (first used for paladins). Without it
    // auras such as Dazed (orbiting stars) render as a frozen bind pose.
    const bool bAnimatedPresentation = PresentationSpell && !bHolyLightHead;
	AActor* EffectActor = FWowDoodadManager::SpawnWorldM2(GetWorld(), Request.ModelPath,
		EffectLocation, AnchorActor->GetActorRotation(), Request.Scale,
		CachedMpq, CachedAssetCache, UniqueId, bAnimatedPresentation, Request.bPersistent);
	if (!EffectActor)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Failed to render spell visual %s for spell %u"),
			*Request.ModelPath, Request.SpellId);
		if (Request.Phase == ESpellVisualPhase::Missile)
			SpawnSpellVisualEffect(Request.OwnerGuid, Request.AnchorGuid, Request.SpellId, ESpellVisualPhase::Impact);
		return;
	}

	if (Request.Phase == ESpellVisualPhase::Missile)
	{
		AActor* Target = ResolveSpellActor(Request.AnchorGuid);
		const auto* Spell = FDbcStore::Get().Spells().GetById(Request.SpellId);
		AWowSpellMissile* Missile = Target && Spell
			? GetWorld()->SpawnActor<AWowSpellMissile>() : nullptr;
		if (!Missile)
		{
			EffectActor->Destroy();
			return;
		}
		// The bolt carries its own light: a Fireball or Shadow Bolt lights the ground and the
		// caster as it flies (the original only had the additive sprite).
		if (UPointLightComponent* MissileLight = NewObject<UPointLightComponent>(EffectActor, TEXT("SpellMissileLight")))
		{
			MissileLight->SetupAttachment(EffectActor->GetRootComponent());
			EffectActor->AddInstanceComponent(MissileLight);
			MissileLight->SetLightColor(GetSpellSchoolColor(Spell->SchoolMask));
			MissileLight->SetIntensity(0.0f);
			MissileLight->SetAttenuationRadius(380.0f);
			MissileLight->SetCastShadows(false);
			MissileLight->ComponentTags.Add(TEXT("WowSpellGlow"));
			MissileLight->ComponentTags.Add(TEXT("WowSpellPeak=3200"));
			if (Spell->SchoolMask & 0x04) MissileLight->ComponentTags.Add(TEXT("WowSpellFire"));
			MissileLight->RegisterComponent();
		}
		const TWeakObjectPtr<AWowGameplayController> WeakThis(this);
		GWowFlyingMissiles.RemoveAll([](const FWowFlyingMissile& M) { return !M.Missile.IsValid(); });
		FWowFlyingMissile& Flying = GWowFlyingMissiles.AddDefaulted_GetRef(); // r699: a crit can still turn it
		Flying.Caster = Request.OwnerGuid; Flying.Target = Request.AnchorGuid; Flying.Spell = Request.SpellId;
		Flying.Time = FPlatformTime::Seconds(); Flying.Missile = Missile;
		Missile->InitializeAuthored(EffectActor, Target, Spell->Speed * FWowCoordinate::SCALE,
			[WeakThis, Request]()
			{
				if (AWowGameplayController* Controller = WeakThis.Get())
					Controller->SpawnSpellVisualEffect(Request.OwnerGuid, Request.AnchorGuid,
						Request.SpellId, ESpellVisualPhase::Impact);
			});
		return;
	}

	EffectActor->SetActorEnableCollision(false);
	EffectActor->Tags.AddUnique(TEXT("WowSpellVisual"));
	TInlineComponentArray<UPrimitiveComponent*> EffectPrimitives(EffectActor);
	for (UPrimitiveComponent* Primitive : EffectPrimitives)
	{
		Primitive->SetCastShadow(false);
	}
	if (CharacterMesh && SocketName != NAME_None && CharacterMesh->DoesSocketExist(SocketName))
	{
		EffectActor->AttachToComponent(CharacterMesh,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
		EffectActor->Tags.AddUnique(TEXT("WowAttachedVisual")); // r406: lets a lost parent be noticed
		if (Request.Slot == ESpellVisualSlot::Attachment)
			EffectActor->SetActorRelativeTransform(FTransform(Request.AttachRotation, Request.AttachOffset, EffectActor->GetActorRelativeScale3D()));
		// r699: a crit log arriving just after this chest impact moves it to the head.
		if (Request.Phase == ESpellVisualPhase::Impact && Slot == ESpellVisualSlot::Chest)
		{
			FWowRecentImpact& Recent = GWowRecentImpacts.AddDefaulted_GetRef();
			Recent.Caster = Request.OwnerGuid; Recent.Target = Request.AnchorGuid; Recent.Spell = Request.SpellId;
			Recent.Time = FPlatformTime::Seconds(); Recent.Actor = EffectActor;
		}
	}
	else if (AnchorActor->GetRootComponent())
	{
		EffectActor->AttachToComponent(AnchorActor->GetRootComponent(),
			FAttachmentTransformRules::KeepWorldTransform);
		EffectActor->Tags.AddUnique(TEXT("WowAttachedVisual"));
	}

    if (Request.Phase == ESpellVisualPhase::Impact &&
        !EffectActor->FindComponentByClass<UWowSpellMeshComponent>() &&
        Request.ModelPath.Contains(TEXT("HolyLight_"), ESearchCase::IgnoreCase) &&
        Request.ModelPath.EndsWith(TEXT("_Head.m2"), ESearchCase::IgnoreCase))
    {
        if (UWowM2ParticleComponent* Particles = EffectActor->FindComponentByClass<UWowM2ParticleComponent>())
        {
            // The static M2 pass is a slanted card. Replace that pass with a
            // short-lived particle burst with no continuous body shell.
            TInlineComponentArray<UStaticMeshComponent*> Cards(EffectActor);
            for (UStaticMeshComponent* Card : Cards)
                if (Card->GetName() == TEXT("WorldM2Mesh")) Card->SetVisibility(false);
            EffectActor->GetRootComponent()->SetAbsolute(false, true, true);
            EffectActor->SetActorRotation(FRotator::ZeroRotator);
            EffectActor->SetActorScale3D(FVector::OneVector);
            Particles->CreateHolyLightVeil(Height);
        }
    }

	// The original M2 particle texture supplies the spell identity. A restrained
	// UE point light lets that authored colour actually fall across the target's
	// body, including the locally visible first-person torso and hands.
	// Auras stay unlit, except protective shells whose glow sells the bubble.
	static const TCHAR* LitAuras[] = {TEXT("Power Word: Shield"), TEXT("Mana Shield"), TEXT("Ice Barrier"),
		TEXT("Ice Block"), TEXT("Divine Shield"), TEXT("Shadow Ward"), TEXT("Fire Ward"), TEXT("Frost Ward")};
	bool bLitAura = false;
	if (Request.Phase == ESpellVisualPhase::Aura && PresentationSpell)
		for (const TCHAR* Name : LitAuras) bLitAura |= PresentationSpell->SpellName.Equals(Name, ESearchCase::IgnoreCase);
	if (UPointLightComponent* Light = Request.Phase != ESpellVisualPhase::Aura || bLitAura
        ? NewObject<UPointLightComponent>(EffectActor, TEXT("SpellVisualLight")) : nullptr)
	{
		const auto* SpellEntry = FDbcStore::Get().Spells().GetById(Request.SpellId);
		Light->SetupAttachment(EffectActor->GetRootComponent());
		EffectActor->AddInstanceComponent(Light);
		const uint32 School = SpellEntry ? SpellEntry->SchoolMask : 1u;
		// The light takes the colour of what is drawn, not the spell school: lightning is
		// Nature school, which lit a green spot under a blue Lightning Shield.
		bool bElectric = false;
		for (const TCHAR* Word : {TEXT("lightning"), TEXT("shock"), TEXT("thunder"), TEXT("storm"), TEXT("zap")})
			bElectric |= Request.ModelPath.Contains(Word, ESearchCase::IgnoreCase);
		Light->SetLightColor(bElectric ? FLinearColor(0.45f, 0.52f, 1.0f) : GetSpellSchoolColor(School));
		// Feet-level effects: lift the light off the ground so it washes the body and the
		// ground around it instead of burning a bright spot between the feet.
		if (Request.Slot == ESpellVisualSlot::Base) Light->SetRelativeLocation(FVector(0.0f, 0.0f, 70.0f));
		// Impacts flash bright and decay; hand glows stay low and breathe (AnimateSpellVisualLights).
		const bool bFlash = Request.Phase == ESpellVisualPhase::Impact || Request.Phase == ESpellVisualPhase::Cast;
		Light->SetIntensity(0.0f);
		Light->SetAttenuationRadius(Request.Phase == ESpellVisualPhase::Impact ? 340.0f : bFlash ? 220.0f : 170.0f);
		Light->SetCastShadows(false);
		Light->ComponentTags.Add(bFlash ? FName(TEXT("WowSpellFlash")) : FName(TEXT("WowSpellGlow")));
		if (School & 0x04) Light->ComponentTags.Add(TEXT("WowSpellFire"));
		Light->ComponentTags.Add(*FString::Printf(TEXT("WowSpellPeak=%d"),
			Request.Phase == ESpellVisualPhase::Impact ? 5200 : bFlash ? 2600 : bLitAura ? 900 : 1400));
		if (bLitAura) Light->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
		Light->RegisterComponent();
	}

	FActiveSpellVisual& Active = ActiveSpellVisuals.AddDefaulted_GetRef();
	Active.OwnerGuid = Request.OwnerGuid;
    Active.SpellId = Request.SpellId;
	Active.Phase = Request.Phase;
	Active.Actor = EffectActor;
	Active.bPersistent = Request.bPersistent;
	Active.ExpireAt = Now + (bHolyLightHead && Request.Phase==ESpellVisualPhase::Impact ? 1.2 : Request.Phase == ESpellVisualPhase::Impact ? 4.0 : 2.5);
	// Like the client, a one-shot kit model lives for one play of its animation (plus a
	// short tail for its last particles), not a fixed 4 s of repeating ribbons.
	if (!Request.bPersistent)
		for (const FName& Tag : EffectActor->Tags)
		{
			const FString Text = Tag.ToString();
			if (!Text.StartsWith(TEXT("WowM2Seq0Ms="))) continue;
			const int32 LengthMs = FCString::Atoi(*Text + 12);
			if (LengthMs > 0) Active.ExpireAt = FMath::Min(Active.ExpireAt, Now + FMath::Clamp(LengthMs / 1000.0 + 0.6, 0.6, 4.0));
		}
	RetainedSpellVisualActors.Add(EffectActor);
}

void AWowGameplayController::RemoveSpellVisuals(uint64 OwnerGuid, bool bPersistentOnly)
{
	PendingSpellVisuals.RemoveAll([OwnerGuid, bPersistentOnly](const FPendingSpellVisual& Request)
	{
		return Request.OwnerGuid == OwnerGuid && (!bPersistentOnly ||
            (Request.bPersistent && Request.Phase != ESpellVisualPhase::Aura));
	});

	for (int32 Index = ActiveSpellVisuals.Num() - 1; Index >= 0; --Index)
	{
		const FActiveSpellVisual& Active = ActiveSpellVisuals[Index];
		if (Active.OwnerGuid != OwnerGuid || (bPersistentOnly && (!Active.bPersistent || Active.Phase == ESpellVisualPhase::Aura)))
		{
			continue;
		}
		if (AActor* Actor = Active.Actor.Get())
		{
			RetainedSpellVisualActors.Remove(Actor);
			Actor->Destroy();
		}
		ActiveSpellVisuals.RemoveAtSwap(Index);
	}
}

namespace
{
bool IsShamanSpellNamed(const FSpellDbcEntry* Spell, const TCHAR* Name)
{
	return Spell && Spell->SpellFamilyName == 11 && Spell->SpellName.Equals(Name, ESearchCase::IgnoreCase);
}

// Point on a unit for spell effects: a mesh socket when present, else a height fraction.
FVector SpellAnchorOffset(AActor* Actor, FName Socket, float HeightFraction)
{
	if (!Actor) return FVector::ZeroVector;
	// The body mesh carries the spell sockets; weapons/effects are skeletal meshes too.
	USkeletalMeshComponent* Mesh = Actor->FindComponentByClass<USkeletalMeshComponent>();
	TInlineComponentArray<USkeletalMeshComponent*> Meshes(Actor);
	for (USkeletalMeshComponent* Candidate : Meshes)
		if (Socket != NAME_None && Candidate && Candidate->DoesSocketExist(Socket)) { Mesh = Candidate; break; }
	if (Mesh)
	{
		if (Socket != NAME_None && Mesh->DoesSocketExist(Socket))
			return Actor->GetActorTransform().InverseTransformPosition(Mesh->GetSocketLocation(Socket));
		const FBox Bounds = Mesh->Bounds.GetBox();
		if (Bounds.IsValid)
			return Actor->GetActorTransform().InverseTransformPosition(
				FVector(Actor->GetActorLocation().X, Actor->GetActorLocation().Y, Bounds.Min.Z + Bounds.GetSize().Z * HeightFraction));
	}
	return FVector(0, 0, 90.f * HeightFraction);
}
}

bool AWowGameplayController::TrySpawnCustomSpellEffect(uint64 CasterGuid, uint64 EffectTargetGuid, uint32 SpellId, ESpellVisualPhase Phase)
{
	const auto* Spell = FDbcStore::Get().Spells().GetById(SpellId);
	if (!Spell || !GetWorld()) return false;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Default: the original WoW missile (single bolt from the hand) with improved lightning;
	// wow.CustomLightningBolt 1 brings back the UE-drawn streak.
	if (UWowLightningBoltComponent::IsCustomBoltEnabled() && IsShamanSpellNamed(Spell, TEXT("Lightning Bolt"))
		&& (Phase == ESpellVisualPhase::Missile || Phase == ESpellVisualPhase::Impact))
	{
		// Real lightning from the caster's hand to the target instead of the missile M2 + impact.
		AActor* Caster = ResolveSpellActor(CasterGuid);
		AActor* Target = ResolveSpellActor(EffectTargetGuid ? EffectTargetGuid : CasterGuid);
		if (!Caster || !Target || Caster == Target) return Phase == ESpellVisualPhase::Impact;
		AActor* BoltActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), Caster->GetActorLocation(), FRotator::ZeroRotator, Params);
		if (!BoltActor) return false;
		auto* Bolt = NewObject<UWowLightningBoltComponent>(BoltActor, TEXT("LightningBolt"));
		BoltActor->SetRootComponent(Bolt);
		BoltActor->AddInstanceComponent(Bolt);
		Bolt->RegisterComponent();
		Bolt->Fire(Caster, SpellAnchorOffset(Caster, TEXT("WowSpellRightHand"), 0.55f),
			Target, SpellAnchorOffset(Target, TEXT("WowSpellChest"), 0.6f),
			// Blue to violet-pink per cast, as WoW's lightning varies.
			FMath::Lerp(FLinearColor(0.42f, 0.55f, 1.0f), FLinearColor(0.78f, 0.42f, 1.0f), FMath::FRand() * 0.7f), 0.5f);
		return true;
	}
	if (IsShamanSpellNamed(Spell, TEXT("Earth Shock")) && Phase == ESpellVisualPhase::Impact)
	{
		AActor* Target = ResolveSpellActor(EffectTargetGuid ? EffectTargetGuid : CasterGuid);
		if (!Target) return true;
		FVector Feet = Target->GetActorLocation();
		float Radius = 120.f;
		if (USkeletalMeshComponent* Mesh = Target->FindComponentByClass<USkeletalMeshComponent>())
		{
			const FBox Bounds = Mesh->Bounds.GetBox();
			if (Bounds.IsValid) { Feet.Z = Bounds.Min.Z; Radius = FMath::Clamp(FMath::Max(Bounds.GetSize().X, Bounds.GetSize().Y) * 1.4f, 90.f, 400.f); }
		}
		AActor* WaveActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), Feet, FRotator::ZeroRotator, Params);
		if (!WaveActor) return false;
		auto* Wave = NewObject<UWowShockwaveComponent>(WaveActor, TEXT("EarthShock"));
		WaveActor->SetRootComponent(Wave);
		WaveActor->AddInstanceComponent(Wave);
		Wave->RegisterComponent();
		Wave->SetWorldLocation(Feet + FVector(0, 0, 2));
		Wave->Fire(Radius, FLinearColor(0.95f, 0.62f, 0.25f), 0.85f);
		// Second, tighter earthen burst at the body: a short inner ring.
		return true;
	}
	return false;
}

void AWowGameplayController::SpawnSpellMissile(uint64 CasterGuid, uint64 SpellTargetGuid, uint32 SpellId)
{
	if (TrySpawnCustomSpellEffect(CasterGuid, SpellTargetGuid, SpellId, ESpellVisualPhase::Missile)) return;
	const FDbcStore& Store = FDbcStore::Get();
	const auto* Spell = Store.Spells().GetById(SpellId);
	if (Spell && (Spell->Attributes & 0x40u) != 0) return; // r302: passive spells show nothing
	const auto* Visual = Spell ? Store.SpellVisuals().GetById(
		Spell->SpellVisual[0] ? Spell->SpellVisual[0] : Spell->SpellVisual[1]) : nullptr;
	const auto* Model = Visual && Visual->HasMissile && Visual->MissileModel > 0
		? Store.SpellVisualEffectNames().GetById(static_cast<uint32>(Visual->MissileModel)) : nullptr;
	if (Visual && Visual->MissileTargetingKit != 0)
	{
		QueueSpellVisualKit(CasterGuid, SpellTargetGuid, SpellId,
			Visual->MissileTargetingKit, ESpellVisualPhase::Impact, false);
	}
	if (!Model || Model->FilePath.IsEmpty() || CasterGuid == SpellTargetGuid ||
		!Spell || !FMath::IsFinite(Spell->Speed) || Spell->Speed <= 0.0f)
	{
		SpawnSpellVisualEffect(CasterGuid, SpellTargetGuid, SpellId, ESpellVisualPhase::Impact);
		return;
	}
	FPendingSpellVisual& Request = PendingSpellVisuals.AddDefaulted_GetRef();
	Request.OwnerGuid = CasterGuid;
	Request.AnchorGuid = SpellTargetGuid;
	Request.SpellId = SpellId;
	Request.ModelPath = NormalizeGameObjectModelPath(Model->FilePath);
	Request.Scale = Model->Scale > 0.0f ? Model->Scale : 1.0f;
	Request.Slot = ESpellVisualSlot::RightHand;
	Request.Phase = ESpellVisualPhase::Missile;
}

FLinearColor AWowGameplayController::GetSpellSchoolColor(uint32 SchoolMask) const
{
	// Spell school color mapping based on SchoolMask - vibrant colors for missile visibility
	// Light colours: saturated enough to read on the ground, not so pure that a
	// single channel (shadow's blue-only purple) lights nothing.
	if (SchoolMask & 0x04) // Fire
		return FLinearColor(1.0f, 0.42f, 0.10f); // warm orange
	else if (SchoolMask & 0x10) // Frost
		return FLinearColor(0.35f, 0.65f, 1.0f); // ice blue
	else if (SchoolMask & 0x08) // Nature
		return FLinearColor(0.3f, 1.0f, 0.3f); // green
	else if (SchoolMask & 0x20) // Shadow
		return FLinearColor(0.55f, 0.22f, 1.0f); // violet
	else if (SchoolMask & 0x40) // Arcane
		return FLinearColor(0.85f, 0.38f, 1.0f); // pink-violet
	else if (SchoolMask & 0x02) // Holy
		return FLinearColor(1.0f, 0.85f, 0.45f); // warm gold
	else // Physical or unknown
		return FLinearColor(1.0f, 1.0f, 1.0f); // white
}

void AWowGameplayController::AnimateSpellVisualLights()
{
	// Spell visual lights were constant until their actor died, so impacts did not
	// flash and hand glows popped. Shape them here from their tags.
	const double Now = FPlatformTime::Seconds();
	auto Shape = [Now](UPointLightComponent* Light, float Age)
	{
		int32 Peak = 1400;
		bool bFlash = false, bFire = false;
		for (const FName& Tag : Light->ComponentTags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(TEXT("WowSpellPeak="))) Peak = FCString::Atoi(*Text + 13);
			else if (Tag == TEXT("WowSpellFlash")) bFlash = true;
			else if (Tag == TEXT("WowSpellFire")) bFire = true;
		}
		float Scale;
		if (bFlash)
			// Fast attack, ~0.4 s decay, a faint afterglow while the visual lingers.
			Scale = FMath::Clamp(Age / 0.06f, 0.f, 1.f) * (FMath::Exp(-Age / 0.4f) * 0.9f + 0.1f * FMath::Exp(-Age / 1.5f));
		else
			Scale = FMath::Clamp(Age / 0.25f, 0.f, 1.f) * (0.86f + 0.14f * FMath::Sin(Age * 6.3f));
		if (bFire)
		{
			const float N = FMath::PerlinNoise1D(static_cast<float>(Now) * 9.f + Light->GetUniqueID() * 0.37f);
			Scale *= 0.85f + 0.3f * N;
		}
		Light->SetIntensity(Peak * FMath::Max(0.f, Scale));
	};
	auto ShapeActor = [&](AActor* Actor)
	{
		if (!Actor) return;
		const float Age = Actor->GetGameTimeSinceCreation();
		TInlineComponentArray<UPointLightComponent*> Lights(Actor);
		for (UPointLightComponent* Light : Lights)
			if (Light && (Light->ComponentHasTag(TEXT("WowSpellFlash")) || Light->ComponentHasTag(TEXT("WowSpellGlow"))))
				Shape(Light, Age);
	};
	for (const FActiveSpellVisual& Active : ActiveSpellVisuals) ShapeActor(Active.Actor.Get());
	// Missile visuals are owned by their AWowSpellMissile, not by ActiveSpellVisuals.
	for (AActor* Retained : RetainedSpellVisualActors) ShapeActor(Retained);
	if (UWorld* World = GetWorld())
		for (TActorIterator<AWowSpellMissile> It(World); It; ++It)
		{
			TArray<AActor*> Attached;
			It->GetAttachedActors(Attached);
			for (AActor* Visual : Attached) ShapeActor(Visual);
		}
}

void AWowGameplayController::UpdateLootBeams()
{
	if (!ConnectionManager || !GetWorld()) return;
	static double NextCheck = 0.0;
	static bool bHooked = false;
	if (!bHooked)
	{
		bHooked = true;
		// Remember the rarest quality of a corpse's loot once its window has been opened.
		ConnectionManager->PacketHandler.OnLootOpened.AddWeakLambda(this,
			[this](uint64 LootGuid, uint8, uint32 Gold, const TArray<FWowLootItem>& Items)
			{
				int32 Best = -1;
				for (const FWowLootItem& Item : Items)
				{
					if (Item.bLooted) continue;
					int32 Quality = Item.Quality;
					if (const FWowItemTemplateInfo* Template = ConnectionManager ? ConnectionManager->PacketHandler.ItemTemplates.Find(Item.ItemId) : nullptr)
						Quality = int32(Template->Quality);
					Best = FMath::Max(Best, Quality);
				}
				if (Best < 0 && Gold > 0) Best = 9;
				if (Best >= 0) LootBeamQualityHints.Add(LootGuid, Best);
			});
	}
	const double Now = FPlatformTime::Seconds();
	if (Now < NextCheck) return;
	NextCheck = Now + 0.25;
	const bool bEnabled = FWowClientPreferences::GetWorldOption(TEXT("lootbeam"));
	auto QualityColor = [](int32 Quality)
	{
		switch (Quality)
		{
		case 0: return FLinearColor(0.62f, 0.62f, 0.62f);
		case 2: return FLinearColor(0.12f, 1.0f, 0.0f);
		case 3: return FLinearColor(0.0f, 0.44f, 0.87f);
		case 4: return FLinearColor(0.64f, 0.21f, 0.93f);
		case 5: return FLinearColor(1.0f, 0.5f, 0.0f);
		case 6: case 7: return FLinearColor(0.9f, 0.8f, 0.5f);
		case 9: return FLinearColor(1.0f, 0.82f, 0.2f); // money only
		default: return FLinearColor(1.0f, 1.0f, 1.0f);
		}
	};
	FWowEntityManager& Entities = ConnectionManager->PacketHandler.EntityManager;
	TSet<uint64> Wanted;
	if (bEnabled)
		for (const uint64 Guid : DeadEntityGuids)
		{
			FWowEntity* Entity = Entities.Find(Guid);
			if (!Entity || !Entity->IsUnit() || !static_cast<const FWowUnitEntity*>(Entity)->IsLootable()) continue;
			AActor* Corpse = SpawnedEntityActors.FindRef(Guid).Get();
			if (!Corpse) continue;
			// Server hint (AzerothCore mod-loot-beam): rarest quality + 1 in UNIT_FIELD_PADDING.
			const uint32 ServerHint = Entity->GetField(UnitField::END - 1);
			const int32* SeenQuality = LootBeamQualityHints.Find(Guid);
			const int32 Quality = ServerHint > 0 && ServerHint <= 9 ? int32(ServerHint) - 1 : SeenQuality ? *SeenQuality : 1;
			const FLinearColor Color = QualityColor(ServerHint == 9 ? 9 : Quality);
			Wanted.Add(Guid);
			if (AActor* Existing = LootBeamActors.FindRef(Guid).Get())
			{
				const UWowLootBeamComponent* Beam = Existing->FindComponentByClass<UWowLootBeamComponent>();
				if (Beam && Beam->GetBeamColor().Equals(Color)) continue;
				if (UWowLootBeamComponent* Old = Existing->FindComponentByClass<UWowLootBeamComponent>()) Old->Stop();
			}
			FVector Foot = Corpse->GetActorLocation();
			if (USkeletalMeshComponent* Mesh = Corpse->FindComponentByClass<USkeletalMeshComponent>())
			{
				const FBox Bounds = Mesh->Bounds.GetBox();
				if (Bounds.IsValid) Foot.Z = Bounds.Min.Z;
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* BeamActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), Foot, FRotator::ZeroRotator, Params);
			if (!BeamActor) continue;
			auto* Beam = NewObject<UWowLootBeamComponent>(BeamActor, TEXT("LootBeam"));
			BeamActor->SetRootComponent(Beam);
			BeamActor->AddInstanceComponent(Beam);
			Beam->RegisterComponent();
			Beam->SetWorldLocation(Foot);
			Beam->Start(Color);
			LootBeamActors.Add(Guid, BeamActor);
		}
	for (auto It = LootBeamActors.CreateIterator(); It; ++It)
	{
		AActor* BeamActor = It.Value().Get();
		if (!BeamActor) { It.RemoveCurrent(); continue; }
		if (!Wanted.Contains(It.Key()))
		{
			if (UWowLootBeamComponent* Beam = BeamActor->FindComponentByClass<UWowLootBeamComponent>()) Beam->Stop();
			LootBeamQualityHints.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
}

void AWowGameplayController::UpdateChannelBeams()
{
	if (!ConnectionManager || !GetWorld()) return;
	struct FBeamStyle { const TCHAR* Spell; FLinearColor Color; float Flow; float Writhe; const TCHAR* Socket; };
	// Flow -1: drained from the target into the caster's hand.
	static const FBeamStyle Styles[] = {
		{TEXT("Drain Life"), FLinearColor(1.0f, 0.10f, 0.06f), -1.f, 1.0f, TEXT("WowSpellLeftHand")},
		{TEXT("Drain Soul"), FLinearColor(0.62f, 0.25f, 1.0f), -1.f, 1.0f, TEXT("WowSpellLeftHand")},
		{TEXT("Drain Mana"), FLinearColor(0.25f, 0.55f, 1.0f), -1.f, 0.8f, TEXT("WowSpellLeftHand")},
		{TEXT("Health Funnel"), FLinearColor(0.40f, 1.0f, 0.30f), 1.f, 0.7f, TEXT("WowSpellLeftHand")},
		{TEXT("Mind Flay"), FLinearColor(0.45f, 0.18f, 0.95f), 1.f, 1.4f, TEXT("WowSpellRightHand")},
	};
	FWowEntityManager& Entities = ConnectionManager->PacketHandler.EntityManager;
	TSet<uint64> Wanted;
	for (const TPair<uint64, uint32>& Channel : ActiveChannelSpells)
	{
		const auto* Spell = FDbcStore::Get().Spells().GetById(Channel.Value);
		if (!Spell) continue;
		const FBeamStyle* Style = nullptr;
		for (const FBeamStyle& Candidate : Styles)
			if (Spell->SpellName.Equals(Candidate.Spell, ESearchCase::IgnoreCase)) { Style = &Candidate; break; }
		if (!Style) continue;
		const FWowEntity* CasterEntity = Entities.Find(Channel.Key);
		uint64 BeamTargetGuid = CasterEntity ? CasterEntity->GetField64(UnitField::CHANNEL_OBJECT) : 0;
		if (!BeamTargetGuid && CasterEntity) BeamTargetGuid = CasterEntity->GetField64(UnitField::TARGET);
		AActor* Caster = ResolveSpellActor(Channel.Key);
		AActor* Target = BeamTargetGuid && BeamTargetGuid != Channel.Key ? ResolveSpellActor(BeamTargetGuid) : nullptr;
		if (!Caster || !Target || Caster == Target) continue;
		Wanted.Add(Channel.Key);
		if (AActor* Existing = ChannelBeamActors.FindRef(Channel.Key).Get())
		{
			const UWowChannelBeamComponent* Beam = Existing->FindComponentByClass<UWowChannelBeamComponent>();
			if (Beam && !Beam->IsStopping() && Beam->GetBeamTarget() == Target) continue;
			if (UWowChannelBeamComponent* Old = Existing->FindComponentByClass<UWowChannelBeamComponent>()) Old->Stop();
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* BeamActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), Caster->GetActorLocation(), FRotator::ZeroRotator, Params);
		if (!BeamActor) continue;
		auto* Beam = NewObject<UWowChannelBeamComponent>(BeamActor, TEXT("ChannelBeam"));
		BeamActor->SetRootComponent(Beam);
		BeamActor->AddInstanceComponent(Beam);
		Beam->RegisterComponent();
		Beam->Start(Caster, Style->Socket, FVector(30.f, 0.f, 95.f), Target, TEXT("WowSpellChest"), FVector(0.f, 0.f, 100.f),
			Style->Color, Style->Flow, Style->Writhe);
		ChannelBeamActors.Add(Channel.Key, BeamActor);
	}
	for (auto It = ChannelBeamActors.CreateIterator(); It; ++It)
	{
		AActor* BeamActor = It.Value().Get();
		if (!BeamActor) { It.RemoveCurrent(); continue; }
		if (!Wanted.Contains(It.Key()))
		{
			if (UWowChannelBeamComponent* Beam = BeamActor->FindComponentByClass<UWowChannelBeamComponent>()) Beam->Stop();
			It.RemoveCurrent();
		}
	}
}

void AWowGameplayController::SpawnCastGlowEffects(uint64 CasterGuid, uint32 SpellId)
{
	// Fallback only: authored SpellVisualKit models are preferred. Keep fallback
	// lights isolated per caster so simultaneous players never clear each other.
	RemoveCastGlowEffects(CasterGuid);

	if (!ConnectionManager) return;

	// Check if deferred DBCs are loaded
	const FDbcStore& DbcStore = FDbcStore::Get();
	if (!DbcStore.IsDeferredLoaded())
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("SpawnCastGlowEffects: Deferred DBCs not loaded yet, spell %u"), SpellId);
		return;
	}

	// Get spell school color
	const auto* SpellEntry = DbcStore.Spells().GetById(SpellId);
	if (!SpellEntry)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("SpawnCastGlowEffects: Spell %u not found in DBC"), SpellId);
		return;
	}

	// Physical-school casts (fishing, bandages, hearthstone) have no hand glow (r263, lost
	// in a later commit and restored in r272).
	if (SpellEntry->SchoolMask <= 1) return;
	// r302: passive spells (item auras) have no hand glow in the original client.
	if ((SpellEntry->Attributes & 0x40u) != 0) return;

	FLinearColor SpellColor = GetSpellSchoolColor(SpellEntry->SchoolMask);

	// Find the caster actor
	AActor* CasterActor = nullptr;
	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;

	if (CasterGuid == LocalGuid)
	{
		// Local player
		CasterActor = GetPawn();
	}
	else
	{
		// Other entity
		if (TObjectPtr<AActor>* FoundActor = SpawnedEntityActors.Find(CasterGuid))
		{
			CasterActor = FoundActor->Get();
		}
	}

	if (!CasterActor)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("SpawnCastGlowEffects: Could not find caster actor for GUID %llu"), CasterGuid);
		return;
	}

	// Find skeletal mesh component
	USkeletalMeshComponent* MeshComp = CasterActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!MeshComp)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("SpawnCastGlowEffects: No skeletal mesh found on caster actor"));
		return;
	}

	// Get world
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Define hand positions (race-independent sockets, then approximate fallback)
	TArray<FVector> HandPositions;
	for (const FName SocketName : {FName(TEXT("WowSpellRightHand")), FName(TEXT("WowSpellLeftHand"))})
	{
		if (MeshComp->DoesSocketExist(SocketName))
		{
			HandPositions.Add(MeshComp->GetSocketLocation(SocketName));
		}
	}

	if (HandPositions.IsEmpty())
	{
		FVector ActorLocation = CasterActor->GetActorLocation();
		FVector ActorRight = CasterActor->GetActorRightVector();

		// Approximate hand positions
		HandPositions.Add(ActorLocation + ActorRight * 40.0f + FVector(0, 0, 80));  // Right hand
		HandPositions.Add(ActorLocation + ActorRight * -40.0f + FVector(0, 0, 80)); // Left hand

		UE_LOG(LogWowGameplay, Log, TEXT("SpawnCastGlowEffects: Using approximate hand positions for spell %u"), SpellId);
	}

	// Create light components for each hand position
	for (const FVector& HandPosition : HandPositions)
	{
		UPointLightComponent* LightComp = NewObject<UPointLightComponent>(CasterActor);
		if (LightComp)
		{
			// Configure the light
			LightComp->SetLightColor(SpellColor);
			LightComp->SetIntensity(5000.0f);
			LightComp->SetAttenuationRadius(150.0f);
			LightComp->SetCastShadows(false);

			// Set position
			LightComp->SetWorldLocation(HandPosition);

			// Attach to the actor
			LightComp->AttachToComponent(CasterActor->GetRootComponent(),
				FAttachmentTransformRules::KeepWorldTransform);

			// Register and activate
			LightComp->RegisterComponent();

			// Store in our active effects array
			ActiveCastEffects.Add(LightComp);
			ActiveCastEffectsByCaster.FindOrAdd(CasterGuid).Add(LightComp);
		}
	}

	UE_LOG(LogWowGameplay, Log, TEXT("SpawnCastGlowEffects: Created %d hand glow effects for spell %u (caster %llu)"),
		ActiveCastEffectsByCaster.FindRef(CasterGuid).Num(), SpellId, CasterGuid);
}

void AWowGameplayController::RemoveCastGlowEffects(uint64 CasterGuid)
{
	TArray<TWeakObjectPtr<UPointLightComponent>>* CasterEffects =
		ActiveCastEffectsByCaster.Find(CasterGuid);
	if (!CasterEffects)
	{
		return;
	}

	for (const TWeakObjectPtr<UPointLightComponent>& WeakLight : *CasterEffects)
	{
		if (UPointLightComponent* LightComp = WeakLight.Get())
		{
			ActiveCastEffects.Remove(LightComp);
			LightComp->DestroyComponent();
		}
	}
	ActiveCastEffectsByCaster.Remove(CasterGuid);
}

void AWowGameplayController::CreateSpellbook()
{
	if (!ConnectionManager || SpellbookWidget.IsValid())
	{
		return;
	}

	// Create the spellbook widget
	// SpellbookWidget = SNew(SWowSpellbook)
	//	.ConnectionManager(ConnectionManager);

	// Add to viewport with proper positioning
	/*
	if (GEngine && GEngine->GameViewport)
	{
		// Wrap in overlay that allows interaction
		TSharedRef<SWidget> SpellbookContainer =
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible) // Pass clicks through when collapsed
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SpellbookWidget.ToSharedRef()
			];

		FWowUiSurfaces::Add(
			SpellbookContainer, EWowUiZone::Windows,
			100 // High Z-order to appear on top
		);
	}

	UE_LOG(LogWowGameplay, Log, TEXT("Created spellbook widget"));
	*/
}

void AWowGameplayController::OnSpellbookKey()
{
	// Fire WoW Lua â€” ToggleSpellBook(BOOKTYPE_SPELL)
	if (UIManager && UIManager->GetLuaVM() && UIManager->GetLuaVM()->IsInitialized())
	{
		UIManager->GetLuaVM()->ExecuteString(TEXT("ToggleSpellBook(BOOKTYPE_SPELL)"), TEXT("=keybind_P"));
	}
}

TSharedRef<SWidget> AWowGameplayController::CreateGameMenuButton(const FString& Label, FSimpleDelegate Action,
	bool bGoldText, TSharedPtr<SButton>* OutButton) const
{

	const FLinearColor TextColor = bGoldText
		? FLinearColor(1.0f, 0.82f, 0.0f, 1.0f)
		: FLinearColor(0.96f, 0.94f, 0.90f, 1.0f);
	TSharedPtr<SButton> Button;
	TSharedRef<SWidget> Result = SNew(SBox)
		.WidthOverride(226.0f)
		.MinDesiredHeight(30.0f)
		[
			SAssignNew(Button, SButton)
			.ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center)
			.ContentPadding(FMargin(0.0f))
			.OnClicked_Lambda([Label, Action]() mutable
			{
				UE_LOG(LogWowGameplay, Log, TEXT("Game menu clicked: %s"), *Label);
				Action.ExecuteIfBound();
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))

				.Padding(FMargin(8.0f, 4.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Label))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
					.ColorAndOpacity(TextColor)
					.Justification(ETextJustify::Center)
					.ShadowOffset(FVector2D(1.0f, 1.0f))
					.ShadowColorAndOpacity(FLinearColor::Black)
				]
			]
		];
	if (OutButton)
	{
		*OutButton = Button;
	}
	return Result;
}

void AWowGameplayController::ShowGameMenu()
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	ReleaseGameplayMouseLookForModalUi();

	// r306 (Stefan 13.10, MoveAnything.lua:5636 "attempt to index global 'EscapeMenu'"): addons may add
	// game menu buttons with EscapeMenu:AddButton (LuaAddonCompat keeps their labels); rebuild the menu
	// when that list changed since it was built.
	static FString GBuiltEscapeMenuLabels;
	const FString EscapeMenuLabels = UIManager && UIManager->GetEventSystem()
		? UIManager->GetEventSystem()->GetGlobalString(TEXT("__WowEscapeMenuLabels")) : FString();
	if (GameMenuViewportWidget.IsValid() && EscapeMenuLabels != GBuiltEscapeMenuLabels)
	{
		FWowUiSurfaces::Remove(GameMenuViewportWidget.ToSharedRef());
		GameMenuViewportWidget.Reset();
	}
	GBuiltEscapeMenuLabels = EscapeMenuLabels;

	if (!GameMenuViewportWidget.IsValid())
	{
		TSharedPtr<SVerticalBox> ButtonList;
		SAssignNew(ButtonList, SVerticalBox);

		auto AddButton = [this, ButtonList](const FString& Label, FSimpleDelegate Action,
			bool bGoldText = false, TSharedPtr<SButton>* OutButton = nullptr)
		{
			ButtonList->AddSlot()
			.AutoHeight()
			.Padding(0, 1.5f)
			[
				CreateGameMenuButton(Label, Action, bGoldText, OutButton)
			];
		};

		AddButton(TEXT("Video"), FSimpleDelegate::CreateLambda([this]()
		{
			OnGameMenuPlaceholderClicked(TEXT("Video"));
		}));
		AddButton(TEXT("Sound"), FSimpleDelegate::CreateLambda([this]()
		{
			OnGameMenuPlaceholderClicked(TEXT("Sound"));
		}));
		AddButton(TEXT("Interface"), FSimpleDelegate::CreateLambda([this]()
		{
			OnGameMenuPlaceholderClicked(TEXT("Interface"));
		}));
		AddButton(TEXT("Key Bindings"), FSimpleDelegate::CreateLambda([this]()
		{
			OnGameMenuPlaceholderClicked(TEXT("Key Bindings"));
		}));
		AddButton(TEXT("Macros"), FSimpleDelegate::CreateLambda([this]()
		{
			OnGameMenuPlaceholderClicked(TEXT("Macros"));
		}));
		AddButton(TEXT("AddOns"), FSimpleDelegate::CreateLambda([this]()
		{
			OnGameMenuResumeClicked();
			SWowAddonList::OpenInViewport(true);
		}));
		TArray<FString> AddonMenuLabels;
		EscapeMenuLabels.ParseIntoArray(AddonMenuLabels, TEXT("\n"));
		for (int32 Index = 0; Index < AddonMenuLabels.Num(); ++Index)
		{
			AddButton(AddonMenuLabels[Index], FSimpleDelegate::CreateLambda([this, Index]()
			{
				OnGameMenuResumeClicked();
				if (UIManager && UIManager->GetLuaVM() && UIManager->GetLuaVM()->IsInitialized())
					UIManager->GetLuaVM()->ExecuteString(FString::Printf(TEXT("__WowRunEscapeMenuButton(%d)"), Index + 1),
						TEXT("=EscapeMenuButton"));
			}));
		}
		AddButton(TEXT("Logout"), FSimpleDelegate::CreateLambda([this]()
		{
			OnGameMenuLogoutClicked();
		}), false, &GameMenuLogoutButton);
		AddButton(TEXT("Exit Game"), FSimpleDelegate::CreateLambda([this]()
		{
			OnGameMenuExitClicked();
		}), false, &GameMenuExitButton);

		ButtonList->AddSlot()
		.AutoHeight()
		.Padding(0, 8.0f, 0, 3.0f)
		[
			SNew(SBox)
			.HeightOverride(1.0f)
		];
		AddButton(TEXT("Return to Game"), FSimpleDelegate::CreateLambda([this]()
		{
			OnGameMenuResumeClicked();
		}), false, &GameMenuResumeButton);

		SAssignNew(GameMenuPanel, SWowWindowFrame)
		.BorderBackgroundColor(FWowWotlkStyle::OuterMetal())
		.Padding(FMargin(8.0f))
		[
			SNew(SBorder)
			.BorderImage(FWowWotlkStyle::PanelDarkBrush())
			.BorderBackgroundColor(FWowWotlkStyle::PanelDark())
			.Padding(FMargin(16.0f, 12.0f))
			[
				ButtonList.ToSharedRef()
			]
		];

		GameMenuViewportWidget =
			SNew(SOverlay)
			.Visibility(EVisibility::Collapsed)
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				GameMenuPanel.ToSharedRef()
			];

		if (GEngine && GEngine->GameViewport)
		{
			FWowUiSurfaces::Add(GameMenuViewportWidget.ToSharedRef(), EWowUiZone::Windows, 5000);
		}
	}

	GameMenuViewportWidget->SetVisibility(EVisibility::Visible);
	bShowMouseCursor = true;
	ApplyWowGameAndUiInputMode(this, GameMenuViewportWidget);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(GameMenuViewportWidget, EFocusCause::SetDirectly);
	}
}

void AWowGameplayController::HideGameMenu()
{
	if (GameMenuViewportWidget.IsValid())
	{
		GameMenuViewportWidget->SetVisibility(EVisibility::Collapsed);
	}

	ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
	FocusWowGameViewport();
}

bool AWowGameplayController::IsGameMenuVisible() const
{
	return GameMenuViewportWidget.IsValid() && GameMenuViewportWidget->GetVisibility().IsVisible();
}

bool AWowGameplayController::IsNativeModalUiVisible() const
{
    if (bUserInterfaceHidden) return false;
    if(KeyBindingsViewport.IsValid() && KeyBindingsViewport->GetVisibility().IsVisible()) return true;
    if (IsDestroyItemDialogVisible()) return true;
	// Login, realm, character-select, character-create, and loading screens all
	// suppress the gameplay software cursor. Treat the entire glue flow as modal
	// so world mouse-look cannot steal a press from its Slate buttons.
	return bSoftwareCursorSuppressed
		|| IsGameMenuVisible()
		// The logout/exit countdown is not modal (like WoW): the camera can still be turned
		// and moving cancels it; only its own panel takes clicks.
		|| (GameplayOptionsViewportWidget.IsValid() && GameplayOptionsViewportWidget->GetVisibility().IsVisible());
}

void AWowGameplayController::PrepareForQuestDialogue()
{
	ReleaseGameplayMouseLookForModalUi();
	bShowMouseCursor = true;

	if (QuestLogWidget.IsValid() && QuestLogWidget->IsVisible())
	{
		QuestLogWidget->ToggleVisibility();
	}
	if (GroupFinderWidget.IsValid() && GroupFinderWidget->IsVisible())
	{
		GroupFinderWidget->SetFinderVisibility(false);
	}

	if (!UIManager)
	{
		return;
	}

	UIManager->HideCharacterPanel();
	if (FWowFrameManager* FrameManager = UIManager->GetFrameManager())
	{
		static const TCHAR* ConflictingPanels[] = {
			TEXT("CharacterFrame"), TEXT("FriendsFrame"), TEXT("QuestLogFrame"),
			TEXT("SpellBookFrame"), TEXT("TalentFrame"), TEXT("PlayerTalentFrame"), TEXT("GuildFrame"),
			TEXT("MailFrame"), TEXT("MerchantFrame"), TEXT("BankFrame"),
			TEXT("TradeFrame"), TEXT("WorldMapFrame")
		};
		for (const TCHAR* PanelName : ConflictingPanels)
		{
			const int64 Handle = FrameManager->FindFrame(PanelName);
			if (Handle >= 0 && FrameManager->IsFrameVisible(Handle))
			{
				FrameManager->SetFrameVisible(Handle, false);
			}
		}
	}
}

void AWowGameplayController::ReleaseGameplayMouseLookForModalUi()
{
	bMouseChordMovingForward = false;
	PressedLeftMouseFrameHandle = -1;
	PressedRightMouseFrameHandle = -1;
	PendingActionDragSlot = -1;
	PendingUiDragFrame = -1;
	bUiDragActive = false;

	if (bLeftMouseLooking)
	{
		EndLeftMouseLook();
	}
	if (bRightMouseLooking)
	{
		EndRightMouseLook();
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}
}

bool AWowGameplayController::CloseOpenUiPanelsForEscape()
{
	// Esc closes every open window at once, like the client's CloseAllWindows.
	bool bClosedAnyWindow = GameUI && GameUI->CloseNpcWindows();
	if (QuestLogWidget.IsValid() && QuestLogWidget->CancelAbandonPopup()) bClosedAnyWindow = true;
	if(NpcServicesWidget.IsValid() && NpcServicesWidget->GetVisibility().IsVisible()){NpcServicesWidget->Close();bClosedAnyWindow=true;}
	if(WorldMapWidget.IsValid() && WorldMapWidget->GetVisibility().IsVisible()){WorldMapWidget->SetVisibility(EVisibility::Hidden);bClosedAnyWindow=true;}
	if(SkillsWidget.IsValid() && SkillsWidget->GetVisibility().IsVisible()){SkillsWidget->SetVisibility(EVisibility::Hidden);bClosedAnyWindow=true;}
	if(AuctionHouseWidget.IsValid() && AuctionHouseWidget->IsOpen()){AuctionHouseWidget->Close();bClosedAnyWindow=true;}
	if(MacroFrameWidget.IsValid() && MacroFrameWidget->IsOpen()){MacroFrameWidget->Close();bClosedAnyWindow=true;}
	if(UnitMenuWidget.IsValid() && UnitMenuWidget->IsOpen()){UnitMenuWidget->Close();bClosedAnyWindow=true;}
	if(InspectWidget.IsValid() && InspectWidget->IsOpen()){InspectWidget->Close();bClosedAnyWindow=true;}
	if(AchievementWidget.IsValid() && AchievementWidget->IsOpen()){AchievementWidget->Close();bClosedAnyWindow=true;}
	if (FriendsFrameWidget.IsValid() && FriendsFrameWidget->IsOpen())
	{
		FriendsFrameWidget->Close();
		bClosedAnyWindow = true;
	}
	if (MailboxWidget.IsValid() && MailboxWidget->IsVisible())
	{
		MailboxWidget->Close();
		bClosedAnyWindow = true;
	}
	if (GroupFinderWidget.IsValid() && GroupFinderWidget->IsVisible())
	{
		GroupFinderWidget->SetFinderVisibility(false);
		bClosedAnyWindow = true;
	}
	if (GameUI && GameUI->IsQuestDialogVisible())
	{
		GameUI->HideQuestDialog();
		bClosedAnyWindow = true;
	}
	if (GameplayOptionsViewportWidget.IsValid() && GameplayOptionsViewportWidget->GetVisibility().IsVisible())
	{
		HideGameplayOptionsMenu();
		bClosedAnyWindow = true;
	}

	if (QuestLogWidget.IsValid() && QuestLogWidget->IsVisible())
	{
		QuestLogWidget->ToggleVisibility();
		bClosedAnyWindow = true;
	}
	if (TalentWindowWidget.IsValid() && TalentWindowWidget->IsVisible())
	{
		TalentWindowWidget->ToggleVisibility();
		bClosedAnyWindow = true;
	}
	if (SpellbookWidget.IsValid() && SpellbookWidget->IsSpellbookVisible())
	{
		SpellbookWidget->SetSpellbookVisible(false);
		bClosedAnyWindow = true;
	}

	if (UIManager)
	{
		if (UIManager->IsBagWindowVisible())
		{
			UIManager->HideBagWindow();
			bClosedAnyWindow = true;
		}

		if (UIManager->IsCharacterPanelVisible())
		{
			UIManager->HideCharacterPanel();
			bClosedAnyWindow = true;
		}

		if (UIManager->IsBankWindowVisible())
		{
			UIManager->HideBankWindow();
			bClosedAnyWindow = true;
		}
	}

	if (UIManager && UIManager->GetFrameManager())
	{
		FWowFrameManager* FrameManager = UIManager->GetFrameManager();
		const TCHAR* SingletonFrames[] = {
			TEXT("CharacterFrame"),
			TEXT("FriendsFrame"),
			TEXT("QuestLogFrame"),
			TEXT("SpellBookFrame"),
			TEXT("TalentFrame"),TEXT("PlayerTalentFrame"),
			TEXT("GuildFrame"),
			TEXT("MailFrame"),
			TEXT("MerchantFrame"),
			TEXT("GossipFrame"),
			TEXT("QuestFrame"),
			TEXT("BankFrame"),
			TEXT("LootFrame"),
			TEXT("TradeFrame"),
			TEXT("InterfaceOptionsFrame"),
			TEXT("VideoOptionsFrame"),
			TEXT("AudioOptionsFrame"),
			TEXT("KeyBindingFrame"),
			TEXT("MacroFrame")
		};

		for (const TCHAR* FrameName : SingletonFrames)
		{
			const int64 FrameHandle = FrameManager->FindFrame(FrameName);
			if (FrameHandle >= 0 && FrameManager->IsFrameVisible(FrameHandle))
			{
				FrameManager->SetFrameVisible(FrameHandle, false);
				bClosedAnyWindow = true;
			}
		}

		// The speech-bubble chat menu (native) closes with Esc.
		if (FrameManager->CloseNativeChatMenu()) bClosedAnyWindow = true;

		// Addon windows registered in UISpecialFrames close with Esc too.
		if (FWowEventSystem* Events = UIManager->GetEventSystem())
		{
			Events->ExecuteLuaString(TEXT(R"LUA(
				__WowEscClosed = "0"
				-- Open menus first, like CloseMenus(): the chat menu and dropdowns (Stefan: Esc did not close the chat menu).
				for _, name in ipairs(UIMenus or {"ChatMenu", "EmoteMenu", "LanguageMenu", "VoiceMacroMenu"}) do
					local m = _G[name]
					if m and m.IsShown and m:IsShown() then m:Hide(); __WowEscClosed = "1" end
				end
				if DropDownList1 and DropDownList1:IsShown() and CloseDropDownMenus then CloseDropDownMenus(); __WowEscClosed = "1" end
				for _, name in ipairs(UISpecialFrames or {}) do
					local f = _G[name]
					if f and f.IsShown and f:IsShown() then f:Hide(); __WowEscClosed = "1" end
				end
				-- Every FrameXML panel window too, like CloseWindows() (Stefan 10.10: the PvP and Help
				-- windows did not close with Esc).
				for name in pairs(UIPanelWindows or {}) do
					local f = _G[name]
					-- HideUIPanel, not Hide: a plain Hide left the window in UIParent's panel slots.
					if f and f.IsShown and f:IsShown() then
						if HideUIPanel then HideUIPanel(f) end
						if f:IsShown() then f:Hide() end
						__WowEscClosed = "1"
					end
				end
				if __WowSanitizeUIPanels then __WowSanitizeUIPanels() end
				-- PVPFrame is a tab page inside PVPParentFrame: hiding it as well left the parent empty
				-- on the next open (Stefan 10.10: PvP button pushed in, no window).
				for _, name in ipairs({"PVPParentFrame", "HelpFrame", "KnowledgeBaseFrame", "LFDParentFrame",
					"LFRParentFrame", "AchievementFrame", "CalendarFrame", "TradeSkillFrame", "ClassTrainerFrame",
					"InspectFrame", "DressUpFrame", "TimeManagerFrame", "ItemTextFrame", "TabardFrame", "PetitionFrame"}) do
					local f = _G[name]
					if f and f.IsShown and f:IsShown() then f:Hide(); __WowEscClosed = "1" end
				end
			)LUA"), TEXT("=EscapeCloseSpecialFrames"));
			if (Events->GetGlobalString(TEXT("__WowEscClosed")) == TEXT("1")) bClosedAnyWindow = true;
		}

		for (int32 ContainerIndex = 1; ContainerIndex <= 13; ++ContainerIndex)
		{
			const int64 ContainerHandle = FrameManager->FindFrame(
				FString::Printf(TEXT("ContainerFrame%d"), ContainerIndex));
			if (ContainerHandle >= 0 && FrameManager->IsFrameVisible(ContainerHandle))
			{
				FrameManager->SetFrameVisible(ContainerHandle, false);
				bClosedAnyWindow = true;
			}
		}
	}

	if (bClosedAnyWindow)
	{
		ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
		FocusWowGameViewport();
	}

	return bClosedAnyWindow;
}

FReply AWowGameplayController::OnGameMenuResumeClicked()
{
	HideGameMenu();
	return FReply::Handled();
}

FReply AWowGameplayController::OnGameMenuLogoutClicked()
{
	if (ConnectionManager && ConnectionManager->GetState() == EWowSessionState::WorldInGame)
	{
		SendMovementUpdate();
		ConnectionManager->RequestLogout();
	}
	BeginLogoutSit();
	ShowGameMenuCountdown(false);
	return FReply::Handled();
}

FReply AWowGameplayController::OnGameMenuExitClicked()
{
	BeginLogoutSit();
	ShowGameMenuCountdown(true);
	return FReply::Handled();
}

void AWowGameplayController::BeginLogoutSit()
{
	// Like the original client: the character sits down for the logout/exit countdown.
	const AWowPlayerCharacter* PlayerChar = Cast<AWowPlayerCharacter>(GetPawn());
	if (!PlayerChar || bLocalSitting) return;
	const UCharacterMovementComponent* Movement = PlayerChar->GetCharacterMovement();
	if (!Movement || Movement->IsFalling() || PlayerChar->IsInWowWaterSwimmingState()) return;
	if (const FWowEntity* Local = ConnectionManager ? ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer() : nullptr)
		if (Local->GetMaxHealth() > 0 && Local->GetHealth() <= 0) return;
	SetLocalSitting(true);
	bSatForLogout = true;
}

void AWowGameplayController::ShowGameMenuCountdown(bool bExitGame)
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	HideGameMenu();
	ReleaseGameplayMouseLookForModalUi();
	bGameMenuCountdownExitsGame = bExitGame;
	GameMenuCountdownEndTime = FPlatformTime::Seconds() + 20.0;

	if (!GameMenuCountdownViewportWidget.IsValid())
	{
		const FSlateFontInfo DialogFont = FWowWindowStyle::TitleFont(14);
		SAssignNew(GameMenuCountdownText, STextBlock)
			.Text(FText::FromString(TEXT("20 Seconds until logout")))
			.Font(DialogFont)
			.ColorAndOpacity(FLinearColor(0.96f, 0.94f, 0.90f, 1.0f))
			.Justification(ETextJustify::Center)
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor::Black);

		TSharedRef<SWidget> CountdownPanel = SNew(SWowSessionCountdown)
            .Message(GameMenuCountdownText)
            .ShowExit_Lambda([this](){ return bGameMenuCountdownExitsGame; })
            .OnExit_Lambda([this]()
            {
                // Skip the timer, retaining normal engine/FSR shutdown.
                if (bGameMenuCountdownExitsGame) CompleteGameMenuCountdown();
                return FReply::Handled();
            })
            .OnCancel_Lambda([this](){ return CancelGameMenuCountdown(); });
        GameMenuCountdownPanel = CountdownPanel;

        // Use a relative Slate anchor. Physical viewport pixels multiplied by
        // UI/DPI scaling placed this dialog twice as low on a 4K display.
        GameMenuCountdownViewportWidget =
            SNew(SConstraintCanvas)
            .Visibility(EVisibility::Collapsed)
            + SConstraintCanvas::Slot()
            .Anchors(FAnchors(.5f,.18f))
            .Alignment(FVector2D(.5f,0.f))
            .Offset(FMargin(0.f))
            .AutoSize(true)
            [ CountdownPanel ];

		if (GEngine && GEngine->GameViewport)
		{
			FWowUiSurfaces::Add(GameMenuCountdownViewportWidget.ToSharedRef(), EWowUiZone::Popups, 5100);
		}
	}

	UpdateGameMenuCountdown();
	if (GameMenuCountdownViewportWidget.IsValid())
	{
		GameMenuCountdownViewportWidget->SetVisibility(EVisibility::SelfHitTestInvisible);
	}

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(GameMenuCountdownTimerHandle);
		GetWorldTimerManager().SetTimer(
			GameMenuCountdownTimerHandle,
			this,
			&AWowGameplayController::UpdateGameMenuCountdown,
			0.10f,
			true);
	}

	bShowMouseCursor = true;
	ApplyWowGameAndUiInputMode(this, GameMenuCountdownViewportWidget);
	if (GameMenuCountdownViewportWidget.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(GameMenuCountdownViewportWidget, EFocusCause::SetDirectly);
	}
}

void AWowGameplayController::UpdateGameMenuCountdown()
{
	const double Remaining = GameMenuCountdownEndTime - FPlatformTime::Seconds();
	const int32 SecondsRemaining = FMath::Max(0, FMath::CeilToInt(Remaining));
	if (GameMenuCountdownText.IsValid())
	{
		GameMenuCountdownText->SetText(FText::FromString(FString::Printf(
			TEXT("%d seconds until %s"),
			SecondsRemaining,
			bGameMenuCountdownExitsGame ? TEXT("exit game") : TEXT("logout"))));
	}

	if (Remaining <= 0.0)
	{
		CompleteGameMenuCountdown();
	}
}

FReply AWowGameplayController::CancelGameMenuCountdown()
{
	if (bSatForLogout)
	{
		bSatForLogout = false;
		if (bLocalSitting) SetLocalSitting(false);
	}
	if (!bGameMenuCountdownExitsGame && ConnectionManager &&
		ConnectionManager->GetState() == EWowSessionState::WorldInGame)
	{
		ConnectionManager->CancelLogout();
	}

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(GameMenuCountdownTimerHandle);
	}

	if (GameMenuCountdownViewportWidget.IsValid())
	{
		GameMenuCountdownViewportWidget->SetVisibility(EVisibility::Collapsed);
	}

	ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
	FocusWowGameViewport();
	return FReply::Handled();
}

void AWowGameplayController::CompleteGameMenuCountdown()
{
    SaveOfflineLogoutPosition();
    SendMovementUpdate();
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(GameMenuCountdownTimerHandle);
	}
	if (GameMenuCountdownViewportWidget.IsValid())
	{
		GameMenuCountdownViewportWidget->SetVisibility(EVisibility::Collapsed);
	}

	if (bGameMenuCountdownExitsGame)
	{
		ConsoleCommand(TEXT("quit"));
		return;
	}

	if (IsWowOfflineSession())
	{
		if (AWowLoginController* LoginController = Cast<AWowLoginController>(
			UGameplayStatics::GetActorOfClass(GetWorld(), AWowLoginController::StaticClass())))
		{
			SetGameplayHudVisible(false);
			if (APawn* ControlledPawn = GetPawn())
			{
				ControlledPawn->SetActorHiddenInGame(true);
				ControlledPawn->SetActorEnableCollision(false);
			}
			LoginController->ReturnToOfflineCharacterSelect();
		}
		return;
	}

	if (ConnectionManager)
	{
		// The logout request was sent when the countdown opened. AzerothCore
		// completes it after the same delay and the server callback refreshes the
		// character list without tearing down the authenticated world socket.
		return;
	}

	// Standalone previews have no server character list.
	if (AWowLoginController* LoginController = Cast<AWowLoginController>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AWowLoginController::StaticClass())))
	{
		SetGameplayHudVisible(false);
		if (APawn* ControlledPawn = GetPawn())
		{
			ControlledPawn->SetActorHiddenInGame(true);
			ControlledPawn->SetActorEnableCollision(false);
		}
		LoginController->ShowCharacterSelectPreviewForTesting();
	}
}

void AWowGameplayController::EndDisconnectedWorldSession()
{
    HandleServerLogoutComplete();
    bPlayerEnteredWorld = false;
    bLocalSitting = bLocalSitTransition = bLocalStandingUp = false;
    bHasDeferredSpawn = false;
    bDeferSpawnTeleport = false;
    bIsAutoAttacking = false;
    AutoAttackTargetGuid = 0;
    bIsCasting = bIsChanneling = false;
    CurrentSpellId = 0;
    bWasInCombat = false;
    bPlayerHealthInitialized = false;
    FWowKeyBindings::ReleaseAll();
    ReleaseGameplayMouseLookForModalUi();
    CloseFrameXmlChatInput();
    if (GameplayOptionsViewportWidget.IsValid()) GameplayOptionsViewportWidget->SetVisibility(EVisibility::Collapsed);
    if (KeyBindingsViewport.IsValid()) KeyBindingsViewport->SetVisibility(EVisibility::Collapsed);
    if (GameUI)
    {
    if (AuctionHouseWidget.IsValid()) AuctionHouseWidget->Close();
    if (UnitMenuWidget.IsValid()) UnitMenuWidget->Close();
    if (InspectWidget.IsValid()) InspectWidget->Close();
    if (AchievementWidget.IsValid()) AchievementWidget->Close();
        GameUI->EndFrameMove();
        GameUI->CloseNpcWindows();
        GameUI->HideLootWindow();
        GameUI->HideTradeWindow();
    }
    const TSharedPtr<SWidget> SessionWindows[]={PartyInviteWidget,DuelInviteViewportWidget,DuelStatusViewportWidget,
        NpcServicesWidget,WorldMapWidget,SkillsWidget,TaxiMapWidget,QuestLogWidget,GroupFinderWidget,SpellbookWidget,TalentWindowWidget,GuildRosterWidget,MailboxWidget,CastBarWidget};
    if (MailboxWidget.IsValid()) MailboxWidget->Close();
    if (FriendsFrameWidget.IsValid()) FriendsFrameWidget->Close();
    for(const auto& Window : SessionWindows) if(Window.IsValid()) Window->SetVisibility(EVisibility::Collapsed);
    HideDestroyItemDialog(false);
    if (UIManager)
    {
        UIManager->EndLocalSpellProgress();
        UIManager->HideBagWindow();
        UIManager->HideCharacterPanel();
        UIManager->HideBankWindow();
    }
    TArray<uint64> Guids;
    SpawnedEntityActors.GetKeys(Guids);
    for (uint64 Guid : Guids) OnEntityDestroyed(Guid);
    PendingEntityModelSpawns.Empty();
    EntityModelSpawnAttempts.Empty();
    EntityModelSpawnFirstQueuedAt.Empty();
    EntityModelSpawnNextAttemptAt.Empty();
    EntityModelReconcileTimer = 2.0f;
    EntityModelSpawnRetryTimer = 0.0f;
    PendingEntityAppearanceSignatures.Empty();
    PendingEntityAppearanceStableAt.Empty();
    TargetGuid = 0;
    UpdateHoveredModel(0);
    if (TargetHighlightCircle) TargetHighlightCircle->SetVisibility(false);
    if (HoverHighlightCircle) HoverHighlightCircle->SetVisibility(false);
    if (APawn* ControlledPawn = GetPawn())
    {
        ControlledPawn->SetActorHiddenInGame(true);
        ControlledPawn->SetActorEnableCollision(false);
        if (ACharacter* PlayerCharacter = Cast<ACharacter>(ControlledPawn)) PlayerCharacter->GetCharacterMovement()->StopMovementImmediately();
    }
}

void AWowGameplayController::HandleServerLogoutComplete()
{
    if (XPGainWidget.IsValid()) XPGainWidget->Clear();
    if (VrXPGainWidget.IsValid()) VrXPGainWidget->Clear();
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(GameMenuCountdownTimerHandle);
	}
	if (GameMenuCountdownViewportWidget.IsValid())
	{
		GameMenuCountdownViewportWidget->SetVisibility(EVisibility::Collapsed);
	}
	if (GameMenuViewportWidget.IsValid())
	{
		GameMenuViewportWidget->SetVisibility(EVisibility::Collapsed);
	}
	SetGameplayHudVisible(false);
	UE_LOG(LogWowGameplay, Log, TEXT("Server logout completed; gameplay menu closed"));
}

FReply AWowGameplayController::OnGameMenuPlaceholderClicked(FString FeatureName)
{
    if(FeatureName==TEXT("Macros"))
    {
        HideGameMenu();
        ToggleMacroFrame();
        return FReply::Handled();
    }
    if(FeatureName==TEXT("Key Bindings") && GEngine && GEngine->GameViewport)
    {
        HideGameMenu(); ReleaseGameplayMouseLookForModalUi();
        if(!KeyBindingsViewport.IsValid())
        {
            KeyBindingsViewport=SNew(SOverlay)
                +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SAssignNew(KeyBindingEditor,SWowKeyBindings).OnClose(FSimpleDelegate::CreateWeakLambda(this,[this]()
                {
                    KeyBindingsViewport->SetVisibility(EVisibility::Collapsed);
                    FireUIEvent(TEXT("UPDATE_BINDINGS")); FocusWowGameViewport();
                }))];
            FWowUiSurfaces::Add(KeyBindingsViewport.ToSharedRef(), EWowUiZone::Windows,600);
        }
        KeyBindingEditor->OpenSession();
        KeyBindingsViewport->SetVisibility(EVisibility::Visible);
        FSlateApplication::Get().SetKeyboardFocus(KeyBindingEditor);
        return FReply::Handled();
    }
	if (FeatureName == TEXT("Video") || FeatureName == TEXT("System") ||
		FeatureName == TEXT("Options") || FeatureName == TEXT("Sound") || FeatureName == TEXT("Interface"))
	{
        ShowGameplayOptionsMenu(FeatureName == TEXT("Sound"));
        if (FeatureName == TEXT("Interface") && GameplayOptionsViewportWidget.IsValid())
            GameplayOptionsViewportWidget->SetActivePage(2);
		return FReply::Handled();
	}
	if (UIManager && UIManager->GetFrameManager())
	{
		FWowFrameManager* FrameManager = UIManager->GetFrameManager();
		const TCHAR* FrameName = nullptr;
		if (FeatureName == TEXT("Interface"))
		{
			FrameName = TEXT("InterfaceOptionsFrame");
		}
		else if (FeatureName == TEXT("Key Bindings"))
		{
			FrameName = TEXT("KeyBindingFrame");
		}
		else if (FeatureName == TEXT("Macros"))
		{
			FrameName = TEXT("MacroFrame");
		}

		if (FrameName)
		{
			const int64 FrameHandle = FrameManager->FindFrame(FrameName);
			if (FrameHandle >= 0)
			{
				HideGameMenu();
				FrameManager->SetFrameVisible(FrameHandle, true);
				return FReply::Handled();
			}
		}
	}

	AddFrameXmlChatLine(
		FString::Printf(TEXT("%s window is not implemented yet."), *FeatureName),
		FLinearColor(1.0f, 0.82f, 0.10f, 1.0f));
	HideGameMenu();
	return FReply::Handled();
}

void AWowGameplayController::ShowGameplayOptionsMenu(bool bOpenSoundPage)
{
	if (!GEngine || !GEngine->GameViewport)
	{
		return;
	}

	HideGameMenu();
	ReleaseGameplayMouseLookForModalUi();

	// The original FrameXML VideoOptionsFrame depends on UI layout machinery that
	// is not complete yet and can expand raw localization labels across the whole
	// viewport. Always close those legacy frames before showing the native panel.
	if (UIManager && UIManager->GetFrameManager())
	{
		FWowFrameManager* FrameManager = UIManager->GetFrameManager();
		for (const TCHAR* FrameName : { TEXT("VideoOptionsFrame"), TEXT("AudioOptionsFrame"), TEXT("InterfaceOptionsFrame") })
		{
			const int64 FrameHandle = FrameManager->FindFrame(FrameName);
			if (FrameHandle >= 0)
			{
				FrameManager->SetFrameVisible(FrameHandle, false);
			}
		}
	}

	AWowAudioManager* AudioManager = Cast<AWowAudioManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AWowAudioManager::StaticClass()));
	if (!GameplayOptionsViewportWidget.IsValid())
	{
		GameplayOptionsViewportWidget = SNew(SWowLoginOptions)
            .AudioManager(AudioManager)
            .OnInterfaceOptionChanged(FOnWowInterfaceOptionChanged::CreateWeakLambda(this, [this](FString Key, bool Enabled)
            {
                if (UIManager && UIManager->GetLuaVM())
                    UIManager->GetLuaVM()->ExecuteString(FString::Printf(TEXT("SetCVar('%s','%d')"), *Key, Enabled?1:0), TEXT("=interfaceoptions"));
                // r224: Show Helm / Show Cloak are server-side (player flags), sent like the original.
                if (UIManager && UIManager->GetLuaVM() && (Key == TEXT("showhelm") || Key == TEXT("showcloak")))
                    UIManager->GetLuaVM()->ExecuteString(FString::Printf(TEXT("if %s then %s('%d') end"),
                        Key == TEXT("showhelm") ? TEXT("ShowHelm") : TEXT("ShowCloak"),
                        Key == TEXT("showhelm") ? TEXT("ShowHelm") : TEXT("ShowCloak"), Enabled ? 1 : 0), TEXT("=interfaceoptions"));
                RefreshWorldLabels();
                if (Key == TEXT("autotrackzonequests"))
                {
                    ZoneUntrackedQuests.Reset();
                    ApplyQuestTracking();
                    UpdateQuestTracker();
                    if (QuestLogWidget.IsValid()) QuestLogWidget->UpdateQuestList();
                }
            }))
			.OnClose(FSimpleDelegate::CreateUObject(this, &AWowGameplayController::HideGameplayOptionsMenu));
		FWowUiSurfaces::Add(GameplayOptionsViewportWidget.ToSharedRef(), EWowUiZone::Windows, 5050);
	}

	GameplayOptionsViewportWidget->SetActivePage(bOpenSoundPage ? 1 : 0);
	GameplayOptionsViewportWidget->SetVisibility(EVisibility::Visible);
	bShowMouseCursor = true;
	ApplyWowGameAndUiInputMode(this, GameplayOptionsViewportWidget);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(GameplayOptionsViewportWidget, EFocusCause::SetDirectly);
	}
}

void AWowGameplayController::HideGameplayOptionsMenu()
{
	if (GameplayOptionsViewportWidget.IsValid())
	{
		GameplayOptionsViewportWidget->SetVisibility(EVisibility::Collapsed);
	}
	ApplyWowGameAndUiInputMode(this, GetWowGameViewportWidget());
	FocusWowGameViewport();
}

void AWowGameplayController::OnEscapeKey()
{
    // Windows close first; ESC only opens the game menu when nothing else is open.
    if (SWowAddonList::IsOpen()) { SWowAddonList::CloseViewportInstance(); return; }
    if (IsDestroyItemDialogVisible()) { HideDestroyItemDialog(false); return; }
    if (ConnectionManager && ConnectionManager->PendingTargetSpell) { ConnectionManager->CancelSpellTarget(); return; }
    if(KeyBindingsViewport.IsValid() && KeyBindingsViewport->GetVisibility().IsVisible())
    { if(KeyBindingEditor)KeyBindingEditor->Cancel(); return; }

	// Steering with the mouse (one or both buttons held) never stops the character or drops the
	// camera, but open windows still close (Stefan 11.10: Esc did nothing while moving with bags
	// open). Only the steps after the windows - auto-attack, target, game menu - wait until the
	// mouse is released, as in the original client.
	const bool bSteering = bLeftMouseLooking || bRightMouseLooking;

	if (GameMenuCountdownViewportWidget.IsValid() && GameMenuCountdownViewportWidget->GetVisibility().IsVisible())
	{
		CancelGameMenuCountdown();
		return;
	}

	if (UIManager && UIManager->GetFrameManager())
	{
		int64 FocusedHandle = -1;
		FString FocusedText;
		if (UIManager->GetFrameManager()->GetFocusedEditBox(FocusedHandle, FocusedText) &&
			UIManager->GetFrameManager()->GetFrameName(FocusedHandle).StartsWith(TEXT("ChatFrame")))
		{
			CloseFrameXmlChatInput(true);
			return;
		}

		if (UIManager->GetFrameManager()->DispatchEditBoxEscapePressed())
		{
			return;
		}
	}

	if (IsWowUiConsumingKeyboardInput())
	{
		return; // Don't process escape when typing in chat
	}

	if (IsGameMenuVisible())
	{
		HideGameMenu();
		return;
	}

	if (CloseOpenUiPanelsForEscape())
	{
		return;
	}
	if (bSteering)
	{
		return;
	}

	// Stop auto-attack before opening the menu, matching the "escape backs out"
	// feel of the original client.
	if (bIsAutoAttacking)
	{
		StopAutoAttack();
		return;
	}

	if (TargetGuid != 0)
	{
		SetTarget(0);
		return;
	}

	// The game menu only exists in the world, never at login, character select or creation.
	if (!bGameplayHudVisible)
	{
		return;
	}
	ShowGameMenu();
}

void AWowGameplayController::UpdateAuraDisplay()
{
	if (!ConnectionManager) return;

	// Clear existing aura icons
	if (PlayerBuffBar.IsValid())
	{
		PlayerBuffBar->ClearChildren();
	}
	if (TargetBuffBar.IsValid())
	{
		TargetBuffBar->ClearChildren();
	}

	// Get the DBC store for spell lookups
	const FDbcStore& DbcStore = FDbcStore::Get();
	if (!DbcStore.IsDeferredLoaded())
	{
		// Skip aura display if spell DBC is not loaded yet
		return;
	}

	// Update player auras
	const uint64 LocalGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
	const FWowEntity* LocalEntity = ConnectionManager->PacketHandler.EntityManager.Find(LocalGuid);
	if (LocalEntity && PlayerBuffBar.IsValid())
	{
		int32 IconsAdded = 0;
		for (int32 i = 0; i < LocalEntity->Auras.Num() && IconsAdded < 16; ++i)
		{
			const FAuraInfo& Aura = LocalEntity->Auras[i];
			if (Aura.bActive && Aura.SpellId > 0)
			{
				// Look up spell information
				const FSpellDbcEntry* SpellEntry = DbcStore.Spells().GetById(Aura.SpellId);
				FString SpellName = TEXT("Unknown");
				FLinearColor AuraColor = FLinearColor::White;

				if (SpellEntry)
				{
					SpellName = SpellEntry->SpellName;
					AuraColor = GetSpellSchoolColor(SpellEntry->SchoolMask);
				}

				// Determine if this is a buff or debuff (simplified - player's own auras are buffs)
				FLinearColor BorderColor = FLinearColor(0.0f, 0.8f, 0.0f); // Green for buffs

				// Create aura icon
				TSharedRef<SWidget> AuraIcon = SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
					.BorderBackgroundColor(AuraColor)
					.Padding(FMargin(1))
					.ToolTipText(FText::FromString(FString::Printf(TEXT("%s%s"),
						*SpellName,
						Aura.Charges > 1 ? *FString::Printf(TEXT(" (%d)"), Aura.Charges) : TEXT(""))))
					[
						SNew(SBox)
						.WidthOverride(18)
						.HeightOverride(18)
						[
							SNew(SBorder)
							.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
							.BorderBackgroundColor(BorderColor)
							.Padding(FMargin(0))
							[
								SNew(STextBlock)
								.Text(Aura.Charges > 1 ? FText::FromString(FString::Printf(TEXT("%d"), Aura.Charges)) : FText::GetEmpty())
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
								.ColorAndOpacity(FLinearColor::White)
								.Justification(ETextJustify::Center)
							]
						]
					];

				PlayerBuffBar->AddSlot()
				.AutoWidth()
				.Padding(1, 0)
				[
					AuraIcon
				];

				IconsAdded++;
			}
		}
	}

	// Update target auras
	if (TargetGuid != 0)
	{
		const FWowEntity* TargetEntity = ConnectionManager->PacketHandler.EntityManager.Find(TargetGuid);
		if (TargetEntity && TargetBuffBar.IsValid())
		{
			int32 IconsAdded = 0;
			for (int32 i = 0; i < TargetEntity->Auras.Num() && IconsAdded < 16; ++i)
			{
				const FAuraInfo& Aura = TargetEntity->Auras[i];
				if (Aura.bActive && Aura.SpellId > 0)
				{
					// Look up spell information
					const FSpellDbcEntry* SpellEntry = DbcStore.Spells().GetById(Aura.SpellId);
					FString SpellName = TEXT("Unknown");
					FLinearColor AuraColor = FLinearColor::White;

					if (SpellEntry)
					{
						SpellName = SpellEntry->SpellName;
						AuraColor = GetSpellSchoolColor(SpellEntry->SchoolMask);
					}

					// Determine if this is a buff or debuff
					// If caster is the local player, it's a debuff we cast (red border)
					// Otherwise, it's likely a debuff on our target (also red border)
					FLinearColor BorderColor = FLinearColor(0.8f, 0.0f, 0.0f); // Red for debuffs
					if (Aura.CasterGuid == LocalGuid)
					{
						// This is our spell on the target - could be buff or debuff
						// For simplicity, show as green (our beneficial spells)
						BorderColor = FLinearColor(0.0f, 0.8f, 0.0f);
					}

					// Create aura icon
					TSharedRef<SWidget> AuraIcon = SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
						.BorderBackgroundColor(AuraColor)
						.Padding(FMargin(1))
						.ToolTipText(FText::FromString(FString::Printf(TEXT("%s%s"),
							*SpellName,
							Aura.Charges > 1 ? *FString::Printf(TEXT(" (%d)"), Aura.Charges) : TEXT(""))))
						[
							SNew(SBox)
							.WidthOverride(18)
							.HeightOverride(18)
							[
								SNew(SBorder)
								.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
								.BorderBackgroundColor(BorderColor)
								.Padding(FMargin(0))
								[
									SNew(STextBlock)
									.Text(Aura.Charges > 1 ? FText::FromString(FString::Printf(TEXT("%d"), Aura.Charges)) : FText::GetEmpty())
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
									.ColorAndOpacity(FLinearColor::White)
									.Justification(ETextJustify::Center)
								]
							]
						];

					TargetBuffBar->AddSlot()
					.AutoWidth()
					.Padding(1, 0)
					[
						AuraIcon
					];

					IconsAdded++;
				}
			}
		}
	}
}

// â”€â”€ Guild Roster Methods â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void AWowGameplayController::OnGuildRosterKey()
{
	if (!GuildRosterWidget.IsValid())
	{
		CreateGuildRoster();
	}

	if (GuildRosterWidget.IsValid())
	{
		GuildRosterWidget->ToggleVisibility();
	}
}

void AWowGameplayController::CreateGuildRoster()
{
	if (GuildRosterWidget.IsValid())
	{
		return; // Already created
	}

	// Get the packet handler for guild data
	FWowPacketHandler* PacketHandler = nullptr;
	if (ConnectionManager)
	{
		PacketHandler = &ConnectionManager->PacketHandler;
	}

	if (!PacketHandler)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Cannot create guild roster: no packet handler available"));
		return;
	}

	// Create the guild roster widget
	GuildRosterWidget = SNew(SWowGuildRoster, PacketHandler);

	// Add to viewport
	if (GEngine && GEngine->GameViewport)
	{
		FWowUiSurfaces::Add(
			GuildRosterWidget.ToSharedRef(), EWowUiZone::Windows,
			10 // Z-order: above most UI but below tooltips
		);
	}

	// Bind to guild roster updates
	if (PacketHandler)
	{
		PacketHandler->OnGuildRosterUpdated.AddUObject(this, &AWowGameplayController::OnGuildRosterUpdated);
	}
}

void AWowGameplayController::OnGuildRosterUpdated()
{
	if (GuildRosterWidget.IsValid())
	{
		GuildRosterWidget->UpdateGuildRoster();
	}
}

// â”€â”€ Mailbox Methods â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void AWowGameplayController::ShowMailbox(int64 MailboxGuid)
{
	if (!MailboxWidget.IsValid())
	{
		CreateMailbox();
	}

	if (MailboxWidget.IsValid())
	{
		MailboxWidget->Show(static_cast<uint64>(MailboxGuid));
	}
}

void AWowGameplayController::CreateMailbox()
{
	if (MailboxWidget.IsValid())
	{
		return; // Already created
	}

	if (!ConnectionManager)
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Cannot create mailbox: no connection manager available"));
		return;
	}

	// MailFrame and OpenMailFrame are UI panels: the panel layout puts the letter beside the inbox.
	MailboxWidget = SNew(SWowMailbox, ConnectionManager).UIManager(UIManager);
	if (GameUI)
	{
		GameUI->AddManagedPanel(TEXT("NativeMailbox"), MailboxWidget.ToSharedRef(), MailboxWidget->GetPanelSize(),
			[this]() { if (MailboxWidget.IsValid()) MailboxWidget->Close(); });
		GameUI->AddManagedPanel(TEXT("NativeOpenMail"), MailboxWidget->GetOpenMailWidget(), MailboxWidget->GetPanelSize());
	}
}

void AWowGameplayController::ShowChatBubble(uint64 Guid, const FString& Text, const FLinearColor& Color)
{
	if (!ChatBubbleLayer.IsValid() && GEngine && GEngine->GameViewport)
	{
		TWeakObjectPtr<AWowGameplayController> WeakThis(this);
		ChatBubbleLayer = SNew(SWowChatBubbles, [WeakThis](uint64 SpeakerGuid, FVector2D& Out)
			{ return WeakThis.IsValid() && WeakThis->GetChatBubbleAnchor(SpeakerGuid, Out); })
			.UIManager(UIManager);
		// Under the UI windows, above the world.
		FWowUiSurfaces::Add(ChatBubbleLayer.ToSharedRef(), EWowUiZone::World, 40);
	}
	if (ChatBubbleLayer.IsValid()) ChatBubbleLayer->AddBubble(Guid, Text, Color);
}

bool AWowGameplayController::GetChatBubbleAnchor(uint64 Guid, FVector2D& Out) const
{
	const APawn* PlayerPawn = GetPawn();
	if (!PlayerPawn || !ConnectionManager || bUserInterfaceHidden) return false;
	const AActor* Actor = Guid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid
		? static_cast<const AActor*>(PlayerPawn) : SpawnedEntityActors.FindRef(Guid).Get();
	if (!IsValid(Actor)) return false;
	static const float BubbleRangeCm = 60.f * (FWowCoordinate::WowToUE(FVector(1, 0, 0)) - FWowCoordinate::WowToUE(FVector::ZeroVector)).Size();
	if (FVector::DistSquared(Actor->GetActorLocation(), PlayerPawn->GetActorLocation()) > FMath::Square(BubbleRangeCm)) return false;
	float Height = 200.f;
	if (const FWorldLabelAnchor* Anchor = WorldLabelAnchors.Find(Guid)) Height = Anchor->Height;
	else if (const USkeletalMeshComponent* Mesh = Actor->FindComponentByClass<USkeletalMeshComponent>())
		Height = float(Mesh->Bounds.GetBox().Max.Z - Actor->GetActorLocation().Z) + 16.f;
	FVector2D Screen;
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(const_cast<AWowGameplayController*>(this),
		Actor->GetActorLocation() + FVector(0, 0, Height), Screen, false)) return false;
	Out = Screen - FVector2D(0.f, 30.f); // above the name
	return true;
}

static FString TrackedQuestsFile(const FString& Character)
{
    FString Safe = Character;
    for (TCHAR& C : Safe) if (!FChar::IsAlnum(C)) C = TEXT('_');
    return FPaths::ProjectSavedDir() / TEXT("Characters") / Safe / TEXT("TrackedQuests.txt");
}

void AWowGameplayController::LoadTrackedQuests()
{
    const FString CharName = ConnectionManager ? ConnectionManager->GetCharacterName() : FString();
    if (CharName.IsEmpty() || CharName == TrackedQuestsCharacter) return;
    TrackedQuestsCharacter = CharName;
    ManualTrackedQuests.Reset();
    ZoneUntrackedQuests.Reset();
    FString Text;
    if (FFileHelper::LoadFileToString(Text, *TrackedQuestsFile(CharName)))
    {
        TArray<FString> Ids;
        Text.ParseIntoArrayWS(Ids);
        for (const FString& Id : Ids) if (const uint32 Quest = FCString::Atoi(*Id)) ManualTrackedQuests.Add(Quest);
    }
    UE_LOG(LogWowGameplay, Log, TEXT("Loaded %d tracked quests for %s"), ManualTrackedQuests.Num(), *CharName);
}

void AWowGameplayController::SaveTrackedQuests() const
{
    if (TrackedQuestsCharacter.IsEmpty()) return;
    TArray<uint32> Ids = ManualTrackedQuests.Array();
    Ids.Sort();
    FString Text;
    for (uint32 Id : Ids) Text += FString::Printf(TEXT("%u\n"), Id);
    FFileHelper::SaveStringToFile(Text, *TrackedQuestsFile(TrackedQuestsCharacter));
}

void AWowGameplayController::ApplyQuestTracking()
{
    if (!ConnectionManager) return;
    LoadTrackedQuests();
    if (TrackedQuestsCharacter.IsEmpty()) return;
    const bool bZone = FWowClientPreferences::GetWorldOption(TEXT("autotrackzonequests"));
    for (FWowQuestLogEntry& Quest : ConnectionManager->PacketHandler.QuestLog)
    {
        const bool bZoneQuest = bZone && LastUiZoneId && Quest.ZoneOrSort > 0 && uint32(Quest.ZoneOrSort) == LastUiZoneId
            && !ZoneUntrackedQuests.Contains(Quest.QuestId);
        Quest.bIsTracked = ManualTrackedQuests.Contains(Quest.QuestId) || bZoneQuest;
    }
}

void AWowGameplayController::SyncTrackedQuestsFromLog()
{
    // The quest log / Shift-click changed bIsTracked: remember the player's choice.
    if (!ConnectionManager) return;
    LoadTrackedQuests();
    if (TrackedQuestsCharacter.IsEmpty()) return;
    const bool bZone = FWowClientPreferences::GetWorldOption(TEXT("autotrackzonequests"));
    for (const FWowQuestLogEntry& Quest : ConnectionManager->PacketHandler.QuestLog)
    {
        const bool bInZone = bZone && LastUiZoneId && Quest.ZoneOrSort > 0 && uint32(Quest.ZoneOrSort) == LastUiZoneId;
        if (Quest.bIsTracked)
        {
            ZoneUntrackedQuests.Remove(Quest.QuestId);
            if (!bInZone) ManualTrackedQuests.Add(Quest.QuestId);
        }
        else
        {
            ManualTrackedQuests.Remove(Quest.QuestId);
            if (bInZone) ZoneUntrackedQuests.Add(Quest.QuestId);
        }
    }
    // Quests no longer in the log (turned in / abandoned) are forgotten.
    if (!ConnectionManager->PacketHandler.QuestLog.IsEmpty())
        for (auto It = ManualTrackedQuests.CreateIterator(); It; ++It)
        {
            const uint32 Id = *It;
            if (!ConnectionManager->PacketHandler.QuestLog.ContainsByPredicate([Id](const FWowQuestLogEntry& Q) { return Q.QuestId == Id; }))
                It.RemoveCurrent();
        }
    SaveTrackedQuests();
}

void AWowGameplayController::UpdateMailboxRange()
{
	// MAIL_CLOSED: the mailbox closes once the player walks out of interaction range.
	if (!MailboxWidget.IsValid() || !MailboxWidget->IsVisible() || !ConnectionManager) return;
	const uint64 Guid = MailboxWidget->GetMailboxGuid();
	const FWowEntity* Box = Guid ? ConnectionManager->PacketHandler.EntityManager.Find(Guid) : nullptr;
	const APawn* PlayerPawn = GetPawn();
	if (!PlayerPawn) return;
	bool bInRange = false;
	if (Box)
	{
		FVector BoxLocation = FWowCoordinate::WowToUE(Box->Movement.Position);
		if (Box->Movement.Position.IsNearlyZero())
		{
			const TObjectPtr<AActor>* Actor = SpawnedEntityActors.Find(Guid);
			if (!Actor || !*Actor) return; // position not known yet
			BoxLocation = (*Actor)->GetActorLocation();
		}
		FVector Feet = PlayerPawn->GetActorLocation();
		if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(PlayerPawn->GetRootComponent()))
			Feet.Z -= Capsule->GetScaledCapsuleHalfHeight();
		const float CmPerYard = (FWowCoordinate::WowToUE(FVector(1, 0, 0)) - FWowCoordinate::WowToUE(FVector::ZeroVector)).Size();
		bInRange = FVector::DistSquared(Feet, BoxLocation) <= FMath::Square(7.f * CmPerYard);
	}
	if (!bInRange)
	{
		UE_LOG(LogWowGameplay, Log, TEXT("Mailbox %llu out of range: closing mail"), Guid);
		MailboxWidget->Close();
	}
}

void AWowGameplayController::InitializeAudioSystem(AWowWorldManager* WorldManager)
{
	if (!WorldManager || !GetWorld())
	{
		UE_LOG(LogWowGameplay, Warning, TEXT("Cannot initialize audio system: missing WorldManager or World"));
		return;
	}

	// Check if audio manager already exists
	TArray<AActor*> AudioManagers;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AWowAudioManager::StaticClass(), AudioManagers);

	AWowAudioManager* AudioManager = nullptr;
	if (AudioManagers.Num() > 0)
	{
		AudioManager = Cast<AWowAudioManager>(AudioManagers[0]);
		UE_LOG(LogWowGameplay, Log, TEXT("Found existing audio manager"));
	}
	else
	{
		// Spawn the audio manager (no fixed name to avoid duplicate name crashes)
		AudioManager = GetWorld()->SpawnActor<AWowAudioManager>(AWowAudioManager::StaticClass());
		if (AudioManager)
		{
			UE_LOG(LogWowGameplay, Log, TEXT("Spawned new audio manager"));
		}
		else
		{
			UE_LOG(LogWowGameplay, Error, TEXT("Failed to spawn audio manager"));
			return;
		}
	}

	// Initialize the audio manager with the MPQ manager
	if (AudioManager && WorldManager->GetMpqManager())
	{
		AudioManager->SetMpqManager(WorldManager->GetMpqManager());
		AudioManager->SetMasterVolume(FWowClientPreferences::GetMasterVolume());
		AudioManager->SetMusicVolume(FWowClientPreferences::GetMusicVolume());
		AudioManager->SetSfxVolume(FWowClientPreferences::GetSfxVolume());
		AudioManager->SetAmbienceVolume(FWowClientPreferences::GetAmbienceVolume());

		if (UIManager)
		{
			UIManager->SetAudioManager(AudioManager);
		}

		UE_LOG(LogWowGameplay, Log, TEXT("Audio system initialized successfully"));
	}
}

void AWowGameplayController::FireUIEvent(const FString& EventName, const TArray<FString>& Args)
{
	if (UIManager && UIManager->GetEventSystem())
	{
		UIManager->GetEventSystem()->FireEvent(EventName, Args);
	}
}

UMaterialInterface* AWowGameplayController::GetUnitPortrait(uint64 Guid)
{
    AActor* Unit = ResolveSpellActor(Guid);
    if (!IsValid(Unit) || !GetWorld()) return nullptr;
    const double Now = FPlatformTime::Seconds();
    for (auto It = UnitPortraits.CreateIterator(); It; ++It)
    {
        if (!IsValid(It.Value()) || Now - It.Value()->LastRequestedAt > 5.0)
        {
            if (IsValid(It.Value())) It.Value()->Destroy();
            It.RemoveCurrent();
        }
    }
    TObjectPtr<AWowUnitPortrait>* Existing = UnitPortraits.Find(Guid);
    if (!Existing)
    {
        if (UnitPortraits.Num() >= 8) return nullptr;
        FActorSpawnParameters Params;
        Params.Owner = this;
        AWowUnitPortrait* Portrait = GetWorld()->SpawnActor<AWowUnitPortrait>(Params);
        if (!Portrait) return nullptr;
        Existing = &UnitPortraits.Add(Guid, Portrait);
    }
    return (*Existing)->CaptureUnit(Unit);
}

void AWowGameplayController::RefreshDetectionAuraVisuals()
{
    if (!ConnectionManager) return;
    auto& Manager = ConnectionManager->PacketHandler.EntityManager;
    const uint8 Concealment = GetUnitConcealmentMask(Manager.FindUnit(Manager.LocalPlayerGuid));
    if (Concealment == LastDetectionViewerConcealment) return;
    LastDetectionViewerConcealment = Concealment;
    // Local aura changes must also remove/recreate indicators on already loaded
    // NPCs, cancelling queued loads before an eye can appear after stealth ends.
    for (const auto& Pair : SpawnedEntityActors)
        if (IsValid(Pair.Value)) SyncAuraVisuals(Pair.Key);
}

void AWowGameplayController::SyncAuraVisuals(uint64 UnitGuid)
{
    if (!ConnectionManager) return;
    auto& Manager = ConnectionManager->PacketHandler.EntityManager;
    const FWowUnitEntity* Entity = Manager.FindUnit(UnitGuid);
    const uint8 ViewerConcealment = GetUnitConcealmentMask(Manager.FindUnit(Manager.LocalPlayerGuid));
    TSet<uint32> Spells;
    if (Entity)
        for (const FAuraInfo& Aura : Entity->Auras)
            if (Aura.bActive && Aura.SpellId && ShouldShowNpcAuraVisual(*Entity, Aura, ViewerConcealment))
                Spells.Add(Aura.SpellId);
    PendingSpellVisuals.RemoveAll([UnitGuid, &Spells](const FPendingSpellVisual& Request)
    { return Request.Phase == ESpellVisualPhase::Aura && Request.OwnerGuid == UnitGuid && !Spells.Contains(Request.SpellId); });
    for (int32 Index = ActiveSpellVisuals.Num() - 1; Index >= 0; --Index)
    {
        const FActiveSpellVisual& Active = ActiveSpellVisuals[Index];
        if (Active.Phase != ESpellVisualPhase::Aura || Active.OwnerGuid != UnitGuid || Spells.Contains(Active.SpellId)) continue;
        if (AActor* Actor = Active.Actor.Get()) { RetainedSpellVisualActors.Remove(Actor); Actor->Destroy(); }
        ActiveSpellVisuals.RemoveAtSwap(Index);
    }
    AActor* UnitActor = ResolveSpellActor(UnitGuid);
    if (!UnitActor) return;
    for (uint32 SpellId : Spells)
    {
        // Shaman Lightning Shield: UE plasma orbs and lightning arcs instead of the legacy M2.
        const auto* ShieldSpell = FDbcStore::Get().Spells().GetById(SpellId);
        if (ShieldSpell && ShieldSpell->SpellFamilyName == 11 && UWowLightningShieldComponent::IsCustomShieldEnabled() &&
            ((ShieldSpell->SpellFamilyFlags[0] & 0x400u) != 0 || ShieldSpell->SpellName.Equals(TEXT("Lightning Shield"), ESearchCase::IgnoreCase)))
        {
            int32 Charges = 0;
            for (const FAuraInfo& Aura : Entity->Auras)
                if (Aura.bActive && Aura.SpellId == SpellId) Charges = Aura.Charges;
            FActiveSpellVisual* Existing = ActiveSpellVisuals.FindByPredicate([UnitGuid, SpellId](const FActiveSpellVisual& Active)
            { return Active.Phase == ESpellVisualPhase::Aura && Active.OwnerGuid == UnitGuid && Active.SpellId == SpellId && Active.Actor.IsValid(); });
            if (Existing)
            {
                if (auto* Shield = Existing->Actor->FindComponentByClass<UWowLightningShieldComponent>()) Shield->SetCharges(Charges);
                continue;
            }
            USkeletalMeshComponent* Body = UnitActor->FindComponentByClass<USkeletalMeshComponent>();
            const FBox Bounds = Body ? Body->Bounds.GetBox() : UnitActor->GetComponentsBoundingBox(true);
            const float Height = Bounds.IsValid ? FMath::Max(Bounds.GetSize().Z, 80.f) : 180.f;
            const float Radius = Bounds.IsValid ? FMath::Clamp(FMath::Max(Bounds.GetSize().X, Bounds.GetSize().Y) * 0.5f, 25.f, 120.f) : 40.f;
            FVector Waist = UnitActor->GetActorLocation();
            if (Bounds.IsValid) Waist.Z = Bounds.Min.Z + Height * 0.55f;
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            AActor* ShieldActor = GetWorld() ? GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), Waist, FRotator::ZeroRotator, Params) : nullptr;
            if (!ShieldActor) continue;
            auto* Shield = NewObject<UWowLightningShieldComponent>(ShieldActor, TEXT("LightningShield"));
            ShieldActor->SetRootComponent(Shield);
            ShieldActor->AddInstanceComponent(Shield);
            Shield->RegisterComponent();
            Shield->SetWorldLocation(Waist);
            Shield->Setup(Height, Radius, FLinearColor(0.42f, 0.52f, 1.0f));
            Shield->SetCharges(Charges);
            ShieldActor->Tags.AddUnique(TEXT("WowSpellVisual"));
            if (UnitActor->GetRootComponent())
                ShieldActor->AttachToComponent(UnitActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
            FActiveSpellVisual& Active = ActiveSpellVisuals.AddDefaulted_GetRef();
            Active.OwnerGuid = UnitGuid;
            Active.SpellId = SpellId;
            Active.Phase = ESpellVisualPhase::Aura;
            Active.Actor = ShieldActor;
            Active.bPersistent = true;
            RetainedSpellVisualActors.Add(ShieldActor);
            continue;
        }
        const bool bActive = ActiveSpellVisuals.ContainsByPredicate([UnitGuid, SpellId](const FActiveSpellVisual& Active)
        { return Active.Phase == ESpellVisualPhase::Aura && Active.OwnerGuid == UnitGuid && Active.SpellId == SpellId && Active.Actor.IsValid(); });
        const bool bPending = PendingSpellVisuals.ContainsByPredicate([UnitGuid, SpellId](const FPendingSpellVisual& Request)
        { return Request.Phase == ESpellVisualPhase::Aura && Request.OwnerGuid == UnitGuid && Request.SpellId == SpellId; });
        if (!bActive && !bPending) SpawnSpellVisualEffect(UnitGuid, UnitGuid, SpellId, ESpellVisualPhase::Aura);
    }
}

bool AWowGameplayController::ExecuteSavedBinding(const FString& Command,bool Pressed)
{
    if(Command.IsEmpty())return false;
    // Macro lines (RunMacroText, LuaMacroApi.cpp) arrive here as "CHATLINE:<line>" and run exactly
    // like a line typed into the chat box.
    if(Command.StartsWith(TEXT("CHATLINE:")))
    {
        if(Pressed) SubmitFrameXmlChat(Command.Mid(9));
        return true;
    }
    auto Lua=[&](const FString& Code){return UIManager && UIManager->GetLuaVM() && UIManager->GetLuaVM()->ExecuteString(Code,TEXT("=savedbinding"));};
    const auto* Action=FWowKeyBindings::Actions().FindByPredicate([&](const auto& A){return A.Command==Command;});
    if(Action && Action->ActionSlot>=0)
    {
        // r650 (Stefan: "if we press 4 it will target and use the skill"): in the isometric camera an action key
        // first targets the unit under the cursor (if there is one; the current target stays otherwise).
        if(Pressed && FWowIsometricCamera::IsActive() && IsWowClientProcessForeground()
            && GetUiFrameUnderCursor()<0 && !IsPointerOverNativeGameUi())
            TryTargetUnderCursor(false);
        if(Pressed)CastSpellFromSlot(Action->ActionSlot+(Command.StartsWith(TEXT("ACTIONBUTTON"))?12*(FWowKeyBindings::ActionPage()-1):0));
        return true;
    }
    // r339: the stock WatchFrame's quest clicks (LuaQuestLogApi.cpp __WowWatchFrameTick):
    // "QUESTTRACKERCLICK:<questID>:log|ctrl|map" - open it in the native quest log, toggle its world
    // diamond (Ctrl-click, as on the native tracker), or show it on the native world map.
    if(Command.StartsWith(TEXT("QUESTTRACKERCLICK:")))
    {
        if(!Pressed || !ConnectionManager) return true;
        TArray<FString> Parts; Command.ParseIntoArray(Parts, TEXT(":"), true);
        const uint32 QuestId = Parts.IsValidIndex(1) ? uint32(FCString::Atoi(*Parts[1])) : 0;
        const FString What = Parts.IsValidIndex(2) ? Parts[2] : FString();
        if(!QuestId) return true;
        UE_LOG(LogWowGameplay, Log, TEXT("WatchFrame click: quest %u (%s)"), QuestId, *What);
        if(What == TEXT("map"))
        {
            if (!WorldMapWidget.IsValid() || !WorldMapWidget->GetVisibility().IsVisible()) OnToggleMap();
            if (WorldMapWidget.IsValid()) WorldMapWidget->ShowQuest(QuestId);
        }
        else if(What == TEXT("ctrl"))
        {
            if (WorldTrackedQuests.Remove(QuestId) == 0) WorldTrackedQuests.Add(QuestId);
            QuestDiamondGround.Remove(QuestId);
            if (WorldTrackedQuests.Num()) EnsureQuestDiamondLayer();
            UpdateQuestTracker();
        }
        else
        {
            if (!QuestLogWidget.IsValid()) CreateQuestLog();
            if (QuestLogWidget.IsValid()) QuestLogWidget->ShowQuest(QuestId);
        }
        return true;
    }
    if(Command==TEXT("SCREENSHOT"))
    { if(Pressed) UWowScreenshotManager::RequestActiveViewport(); return true; }
    if(Command==TEXT("TOGGLEUI"))
    { if(Pressed) SetUserInterfaceHidden(!bUserInterfaceHidden); return true; }
    if(!Pressed)
    {
        if(Command.StartsWith(TEXT("BONUSACTIONBUTTON")) || Command==TEXT("MOVEFORWARD") || Command==TEXT("MOVEBACKWARD") || Command==TEXT("STRAFELEFT") || Command==TEXT("STRAFERIGHT") || Command==TEXT("TURNLEFT") || Command==TEXT("TURNRIGHT") || Command==TEXT("JUMP") || Command==TEXT("SITORSTAND") || Command==TEXT("SWIMDOWN") || Command==TEXT("MOVEANDSTEER") || Command==TEXT("PITCHUP") || Command==TEXT("PITCHDOWN"))return true;
        return UIManager && UIManager->RunOriginalBinding(Command,false);
    }
    if(Command.StartsWith(TEXT("TARGETNEAREST")) || Command.StartsWith(TEXT("TARGETPREVIOUS")))
        CycleBindingTarget(Command.Contains(TEXT("ENEMY")),Command.EndsWith(TEXT("PLAYER")),Command.StartsWith(TEXT("TARGETPREVIOUS")));
    else if(Command==TEXT("TARGETSELF") && ConnectionManager)SetTarget(ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid);
    else if(Command==TEXT("CLEARTARGET"))SetTarget(0);
    else if(Command==TEXT("TARGETLASTTARGET"))SetTarget(LastSelectedGuid);
    else if(Command==TEXT("TARGETLASTHOSTILE"))SetTarget(LastHostileGuid);
    else if(Command==TEXT("TARGETFOCUS") && UIManager)SetTarget(UIManager->GetFocusGuid());
    else if(Command==TEXT("FOCUSTARGET"))Lua(TEXT("FocusUnit('target')"));
    else if(Command==TEXT("ASSISTTARGET") && ConnectionManager)
    {if(const auto* E=ConnectionManager->PacketHandler.EntityManager.Find(TargetGuid))SetTarget(E->GetField64(UnitField::TARGET));}
    else if(Command.StartsWith(TEXT("TARGETPARTYMEMBER")) && ConnectionManager)
    {
        int32 Index=FCString::Atoi(*Command.Mid(17))-1;
        for(const auto& Member:ConnectionManager->PacketHandler.GroupInfo.Members)
            if(Member.Guid!=ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid && Index--==0){SetTarget(Member.Guid);break;}
    }
    else if(Command==TEXT("TARGETPET") && ConnectionManager)SetTarget(ConnectionManager->PacketHandler.PetActionBar.PetGuid);
    else if(Command==TEXT("STARTATTACK"))StartAutoAttack();
    else if(Command==TEXT("ATTACKTARGET")){if(bIsAutoAttacking)StopAutoAttack();else StartAutoAttack();}
    else if(Command==TEXT("STOPATTACK"))StopAutoAttack();
    else if(Command.StartsWith(TEXT("BONUSACTIONBUTTON")) && ConnectionManager)ConnectionManager->SendPetActionBarSlot(FCString::Atoi(*Command.Mid(17))-1,TargetGuid);
    else if(Command==TEXT("PETATTACK") && ConnectionManager)
    {
        const auto& Slots=ConnectionManager->PacketHandler.PetActionBar.ActionSlots;
        for(int32 I=0;I<Slots.Num();++I)if(Slots[I].IsCommand() && Slots[I].ActionId==2){ConnectionManager->SendPetActionBarSlot(I,TargetGuid);break;}
    }
    else if(Command.StartsWith(TEXT("RAIDTARGET")) && ConnectionManager)
    {ConnectionManager->SendSetRaidTargetIcon(Command==TEXT("RAIDTARGETNONE")?0:FCString::Atoi(*Command.Mid(10)),TargetGuid);}
    else if(Command.StartsWith(TEXT("ACTIONPAGE")) || Command==TEXT("NEXTACTIONPAGE") || Command==TEXT("PREVIOUSACTIONPAGE"))
    {
        int32 Page=FWowKeyBindings::ManualActionPage();
        // Next/previous page skip pages already on screen as extra bars (as the page arrows do).
        auto Shown=[](int32 P)
        {
            const bool B5=FWowClientPreferences::GetWorldOption(TEXT("actionbar5"));
            return (P==3&&B5)||(P==4&&B5&&FWowClientPreferences::GetWorldOption(TEXT("actionbar6")))
                ||(P==5&&FWowClientPreferences::GetWorldOption(TEXT("actionbar4")))||(P==6&&FWowClientPreferences::GetWorldOption(TEXT("actionbar3")));
        };
        if(Command==TEXT("NEXTACTIONPAGE")||Command==TEXT("PREVIOUSACTIONPAGE"))
        {
            // r373/r374 (Stefan 16.10: in stealth only bars 2 and 7 could be reached): while a bonus bar
            // is active, page 1 without it is part of the cycle too (token 0).
            Page=FWowKeyBindings::StepActionPage(Command==TEXT("NEXTACTIONPAGE"),Shown);
        }
        else Page=FCString::Atoi(*Command.Mid(10));
        if (UIManager && UIManager->GetFrameManager()) UIManager->GetFrameManager()->SetActionBarPage(Page);
        else FWowKeyBindings::SetActionPage(Page);
        OnActionButtonsUpdated();
    }
    else if(Command==TEXT("TOGGLEBACKPACK"))OnBagKey();
    else if(Command==TEXT("OPENALLBAGS"))OnAllBagsKey();
    else if(Command==TEXT("TOGGLECHARACTER0"))OnCharacterKey();
    else if(Command==TEXT("TOGGLEQUESTLOG"))OnQuestLogKey();
    else if(Command==TEXT("TOGGLEACHIEVEMENT"))OnAchievementKey();
    else if(Command==TEXT("TOGGLECHARACTER2") || Command==TEXT("TOGGLESKILLS"))OnSkillsKey();
    else if(Command==TEXT("TOGGLESPELLBOOK"))OnSpellbookKey();
    else if(Command==TEXT("TOGGLESOCIAL"))OnSocialKey();
    else if(Command==TEXT("TOGGLEGUILDTAB"))OnGuildRosterKey();
    else if(Command==TEXT("TOGGLETALENTS"))OnTalentKey();
    else if(Command==TEXT("TOGGLELFGPARENT"))OnGroupFinderKey();
    else if(Command==TEXT("TOGGLEWORLDMAP"))OnToggleMap();
    else if(Command==TEXT("OPENWORLDMAP")){if(!WorldMapWidget.IsValid() || !WorldMapWidget->GetVisibility().IsVisible())OnToggleMap();}
    else if(Command==TEXT("CLOSEWORLDMAP")){if(WorldMapWidget.IsValid())WorldMapWidget->SetVisibility(EVisibility::Hidden);}
    else if(Command==TEXT("TOGGLEGAMEMENU"))OnEscapeKey();
    else if(Command==TEXT("OPENCHAT"))OnEnterKey();
    else if(Command==TEXT("NAMEPLATES"))OnToggleNameplates();
    else if(Command==TEXT("FRIENDNAMEPLATES"))
    {
        const FString Option=Command==TEXT("NAMEPLATES")?TEXT("nameplateshowenemies"):TEXT("nameplateshowfriends");
        FWowClientPreferences::SetWorldOption(Option,!FWowClientPreferences::GetWorldOption(Option));RefreshWorldLabels();
    }
    else if(Command==TEXT("ALLNAMEPLATES") || Command==TEXT("TOGGLENAMEPLATES"))OnToggleNameplates();
    else if(Command==TEXT("TOGGLESHEATH"))OnToggleSheathKey();
    else if(Command==TEXT("TOGGLETORCH"))UWowPlayerTorchComponent::Toggle(GetPawn(),CachedMpq,CachedAssetCache); // r398 belt torch
    else if(Command==TEXT("SITORSTAND") || Command==TEXT("SITSTAND"))
    {if(const auto* P=Cast<AWowPlayerCharacter>(GetPawn());!P || !P->IsInWowWaterSwimmingState())OnToggleSitKey();}
    else if(Command==TEXT("TOGGLEFPS"))TogglePerformanceOverlay();
    else if(Command==TEXT("CAMERAZOOMIN") || Command==TEXT("CAMERAZOOMOUT"))
    {if(auto* P=Cast<AWowPlayerCharacter>(GetPawn()))P->RunBoundMovementCommand(Command);}
    else if(Command==TEXT("TOGGLEAUTORUN") || Command==TEXT("TOGGLERUN"))
    {if(auto* P=Cast<AWowPlayerCharacter>(GetPawn()))P->RunBoundMovementCommand(Command);}
    else if(Command==TEXT("MOVEFORWARD") || Command==TEXT("MOVEBACKWARD") || Command==TEXT("STRAFELEFT") || Command==TEXT("STRAFERIGHT") || Command==TEXT("TURNLEFT") || Command==TEXT("TURNRIGHT") || Command==TEXT("JUMP") || Command==TEXT("SWIMDOWN") || Command==TEXT("MOVEANDSTEER") || Command==TEXT("PITCHUP") || Command==TEXT("PITCHDOWN"))return true;
    else return UIManager && UIManager->RunOriginalBinding(Command,Pressed);
    return true;
}
void AWowGameplayController::CycleBindingTarget(bool Enemy,bool PlayerOnly,bool Previous)
{
    if(!ConnectionManager || !GetPawn())return;
    struct FCandidate{uint64 Guid;float Distance;};TArray<FCandidate> Candidates;
    for(const auto& Pair:SpawnedEntityActors)
    {
        if(!IsValid(Pair.Value))continue;
        const auto* Unit=ConnectionManager->PacketHandler.EntityManager.FindUnit(Pair.Key);
        if(!Unit || Unit->GetHealth()<=0 || (PlayerOnly && !Unit->IsPlayer()) || IsWorldUnitAttackable(*Unit)!=Enemy)continue;
        const float Distance=FVector::DistSquared(Pair.Value->GetActorLocation(),GetPawn()->GetActorLocation());
        if(Distance<=FMath::Square(5000.f))Candidates.Add({Pair.Key,Distance});
    }
    Candidates.Sort([](const FCandidate& A,const FCandidate& B){return A.Distance==B.Distance?A.Guid<B.Guid:A.Distance<B.Distance;});
    if(Candidates.IsEmpty())return;
    int32 Index=Candidates.IndexOfByPredicate([&](const auto& C){return C.Guid==TargetGuid;});
    Index=Index<0?(Previous?Candidates.Num()-1:0):(Index+(Previous?Candidates.Num()-1:1))%Candidates.Num();
    SetTarget(Candidates[Index].Guid);
}

void AWowGameplayController::ForgetEntityCombatMotion(uint64 Guid)
{
    EntityVisualSpeedsCm.Remove(Guid);
    ServerMeleeVictims.Remove(Guid);
    for(auto It=ServerMeleeVictims.CreateIterator();It;++It)
        if(It.Value()==Guid) It.RemoveCurrent();
}

void AWowGameplayController::HandleKillXPGain(uint32 Amount,uint32 RestedBonus)
{
    if (!Amount || !ConnectionManager || ConnectionManager->GetState()!=EWowSessionState::WorldInGame) return;
    if (!XPGainWidget.IsValid() && GEngine && GEngine->GameViewport)
    {
        XPGainWidget=SNew(SWowXPGain);
        XPGainWidget->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() {
            return !bUserInterfaceHidden && ConnectionManager && ConnectionManager->GetState()==EWowSessionState::WorldInGame
                ?EVisibility::HitTestInvisible:EVisibility::Hidden;
        }));
        FWowUiSurfaces::Add(XPGainWidget.ToSharedRef(), EWowUiZone::World,8);
    }
    if (XPGainWidget.IsValid()) XPGainWidget->AddGain(Amount,RestedBonus>0);
    // The headset shows its own copy on a world card (a widget can only have one parent).
    if (FWowVRSupport::IsActive() && !VrXPGainWidget.IsValid()) VrXPGainWidget=SNew(SWowXPGain);
    if (VrXPGainWidget.IsValid()) VrXPGainWidget->AddGain(Amount,RestedBonus>0);
}

void AWowGameplayController::SaveOfflineLogoutPosition()
{
    if (!bPlayerEnteredWorld || !bGameplayHudVisible) return;
    auto* LocalCharacter=Cast<ACharacter>(GetPawn());
    auto* Login=Cast<AWowLoginController>(UGameplayStatics::GetActorOfClass(GetWorld(),AWowLoginController::StaticClass()));
    if(LocalCharacter && Login) Login->SaveOfflineLocation(FWowLoginPlacement::Feet(*LocalCharacter),
        FWowCoordinate::UEYawToWowOrientation(LocalCharacter->GetActorRotation().Yaw));
}

void AWowGameplayController::UpdateBonusActionBar()
{
 int32 Offset=0;
 if(ConnectionManager)if(const auto* P=ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer())
 {
  const uint32 Form=P->GetFieldByte(UnitField::BYTES_2,3);
  Offset=FDbcStore::Get().Spells().BonusBarForForm(Form);
  bool Stealth=false;
  for(const auto&A:P->Auras)if(A.SpellId)if(const auto*S=FDbcStore::Get().Spells().GetById(A.SpellId))
   for(int I=0;I<3;++I)Stealth|=S->EffectApplyAuraName[I]==16;
  if(P->GetClassId()==4 && Stealth && Form!=13)Offset=1;
  if(P->GetClassId()==11 && Form==1 && Stealth)Offset=2;
  // Only warrior stances, druid forms, rogue stealth and priest Shadowform swap the main bar in
  // 3.3.5. A level-12 priest got bonus page 7 (Stefan 10.10: the page read "7", arrows showed 2/7).
  const uint8 ClassId=P->GetClassId();
  if(ClassId!=1 && ClassId!=4 && ClassId!=11 && !(ClassId==5 && Form==28))Offset=0;
  static int32 LoggedOffset=-1;
  if(Offset!=LoggedOffset){LoggedOffset=Offset;UE_LOG(LogWowGameplay,Log,TEXT("Bonus action bar: class %u form %u -> offset %d"),ClassId,Form,Offset);}
 }
 if(!FWowKeyBindings::SetBonusBarOffset(Offset))return;
 if(UIManager && UIManager->GetFrameManager())UIManager->GetFrameManager()->SetActionBarPage(FWowKeyBindings::ManualActionPage());
 FireUIEvent(TEXT("UPDATE_BONUS_ACTIONBAR"));
 FireUIEvent(TEXT("UPDATE_SHAPESHIFT_FORM"));
}

bool AWowGameplayController::HandleDressUpAction(const FString& Action, uint32 ItemEntry, float Value)
{
	// Claude-UI: the Lua side of the dressing room (DressUpModel, IsDressableItem) on Claude-Perf's
	// native preview (WowGameplayControllerDressUp.cpp).
	if (Action == TEXT("reset")) { DressUpReset(); return true; }
	if (Action == TEXT("undress")) { DressUpUndress(); return true; }
	if (Action == TEXT("rotate")) { DressUpRotate(Value); return true; }
	if (Action == TEXT("tryon")) return DressUpItem(ItemEntry);
	if (Action == TEXT("dressable"))
	{
		if (!ConnectionManager || !ItemEntry) return false;
		const FWowItemTemplateInfo* DressTemplate = ConnectionManager->PacketHandler.ItemTemplates.Find(ItemEntry);
		if (!DressTemplate)
		{
			// Not known yet: let it through; DressUpItem waits for the template.
			ConnectionManager->PacketHandler.RequestItemTemplate(ItemEntry);
			return true;
		}
		if (DressTemplate->Class == 15 && DressTemplate->SubClass == 5)
			return FWowClientPreferences::GetWorldOption(TEXT("mountpreview"));
		switch (DressTemplate->InventoryType)
		{
		case 1: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 13: case 14: case 15:
		case 16: case 17: case 19: case 20: case 21: case 22: case 23: case 25: case 26:
			return DressTemplate->DisplayId != 0;
		default:
			return false; // rings, trinkets, necks, bags: nothing to show
		}
	}
	return false;
}

// r520: the HD background prefetch (WowGameplayControllerHd.cpp) needs a player's character params.
FWowCharacterBuilder::FCharacterParams WowBuildPlayerParamsForHd(const FWowPlayerEntity& Player, uint8 NativeGender)
{
	return BuildPlayerCharacterParams(Player, NativeGender);
}
