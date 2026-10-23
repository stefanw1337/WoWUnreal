#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Components/WidgetComponent.h"
#include "Components/PointLightComponent.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "WowAnimationController.h"
#include "WowCursorManager.h"
#include "WowGroundTiltComponent.h"
#include "WowGameplayController.generated.h"

class UMeshComponent;
class UDirectionalLightComponent;
class UWowConnectionManager;
class UWowUIManager;
class UWowGameUI;
class AWowWorldManager;
class AWowSkyManager;
class UWowDeathManager;
class UWowTooltipManager;
class FMpqManager;
class FWowAssetCache;
class SWowCombatLog;
class SWowChatWindow;
struct FWowEntity;
struct FWowOutgoingMovementInfo;
struct FWowForcedMovement;
struct FWowCharacterInfo;
struct FWowPetActionBarState;
class SWowActionBar;
class SWowCastBar;
class SWowMinimap;
class ASceneCapture2D;
class AWowCharacterPreview;
class UTextureRenderTarget2D;
class USceneComponent;
class SWowPartyFrame;
class SWowPartyInvite;
class SWowDuelInvite;
class SWowTaxiMap;
class SWowQuestLog;
class SWowGroupFinder;
class SWowGuildRoster;
class SWowMailbox;
class SProgressBar;
class STextBlock;
class SBorder;
struct FWowFloatingTextInfo;

UCLASS()
class WOWUNREAL_API AWowGameplayController : public APlayerController
{
    GENERATED_BODY()
    friend class FWowCombatRegressionTest;
public:
    AWowGameplayController();
    virtual void BeginPlay() override;
    void SaveOfflineLogoutPosition();
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;
private:
    void TickOfflineTestBridge();
    void FinishOfflineTransferLoading();
    // r557 (Claude-Perf): area triggers (dungeon portals, inns, explore objectives) and the offline
    // dungeon transfer, WowGameplayControllerAreaTriggers.cpp.
    void TickAreaTriggers();
    void StartOfflineMapTransfer(uint32 MapId, const FVector& WowFeet);
    void TickOfflineMapTransfer();
    TSharedPtr<class SWowLoadingScreen> OfflineTransferScreen;
    bool bOfflineTransferQueued=false;
    uint32 OfflineTransferMap=0;
    FVector OfflineTransferWowPosition=FVector::ZeroVector;
    uint64 OfflineTransferRevision=0;
    double OfflineTransferStableSince=0;
    double OfflineTransferNotBefore=0;
    double NextOfflineBridgePoll = 0;
    FString OfflineBridgeSession, OfflineBridgeLastId, OfflineBridgeError;
public:
    virtual void SetupInputComponent() override;

    /** Override to catch all input regardless of input mode */
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;

    /** Connection manager for sending movement packets */
    UPROPERTY()
    TObjectPtr<UWowConnectionManager> ConnectionManager;

    /** UI manager for event dispatch */
    UPROPERTY()
    TObjectPtr<UWowUIManager> UIManager;

    /** Death manager for player death handling */
    UPROPERTY()
    TObjectPtr<UWowDeathManager> DeathManager;

    /** Cursor manager for WoW custom cursors */
    UPROPERTY()
    TObjectPtr<UWowCursorManager> CursorManager;

    /** Tooltip manager for entity tooltips */
    UPROPERTY()
    TObjectPtr<UWowTooltipManager> TooltipManager;

    /** Game UI manager for WoW-style windows */
    UPROPERTY()
    TObjectPtr<UWowGameUI> GameUI;

    /** Wire entity events from the packet handler (call after setting ConnectionManager) */
    void BindEntityEvents();

    /** Initialize all managers */
    void InitializeManagers();

    /** Hide or reveal gameplay HUD widgets while glue screens are shown. */
    void SetGameplayHudVisible(bool bVisible);
    void RestoreGameplayInputAfterWorldEntry();
    /** Photo mode (hold F12). Null until the controller enters the world. */
    const class FWowPhotoMode* GetPhotoMode() const { return PhotoMode.Get(); }
    void TogglePhotoMode();
    bool HandlePhotoModeTap();
    friend class FWowPhotoMode;
    friend class UWowDeathManager; // ghost overlays on spawned players, corpse actors
    bool bLastHDOption = false;   // hdcharacters option seen last tick (live model rebuild)
    bool bHDOptionSeen = false;
    TSharedPtr<class FWowPhotoMode> PhotoMode;
    void SetUserInterfaceHidden(bool bHideInterface);
    bool bUserInterfaceHidden = false;
    void HandleKillXPGain(uint32 Amount, uint32 RestedBonus);
    TSharedPtr<class SWowXPGain> XPGainWidget;
    /** VR world cards for the screen overlays (WowGameplayControllerVr.cpp). */
    void UpdateVrWorldCards(float DeltaTime);
    TMap<uint32, TWeakObjectPtr<class UWidgetComponent>> VrDiamondCards;
    TMap<uint64, TWeakObjectPtr<class UWidgetComponent>> VrBubbleCards;
    TWeakObjectPtr<class UWidgetComponent> VrXPGainCard;
    TSharedPtr<class SWowXPGain> VrXPGainWidget;
    /** VR pointer over the world: a gaze dot (default) or the mouse pointer (option "vrmousepointer"). */
    TWeakObjectPtr<class UWidgetComponent> VrPointerCard;
    bool bVrPointerIsMouse = false;
    /** VR dwell click (option "vrdwellclick"): what the crosshair rests on, since when, progress. */
    uint64 VrDwellGuid = 0;
    double VrDwellStart = 0.0;
    double VrDwellFlashUntil = 0.0;
    bool bVrDwellFired = false;
    float VrDwellProgress = 0.f;
    bool bVrDwellUi = false;
    FVector2D VrDwellUiPoint = FVector2D::ZeroVector;
    /** World ray for hover and clicks: the mouse, or in VR without "vrmousepointer" the head's gaze. */
    bool GetWorldPointerRay(FVector& OutOrigin, FVector& OutDirection) const;
    EVisibility UserInterfacePreviousVisibility = EVisibility::SelfHitTestInvisible;

    /** Hide the custom software cursor while glue/loading screens need full cursor suppression. */
    void SetSoftwareCursorSuppressed(bool bSuppressed, bool bShowNativeCursor = false);

    /** Accumulate raw mouse-look movement from either legacy or enhanced input. */
    bool IsSteeringWithMouse() const { return bRightMouseLooking; }
    /** Experimental rooted stand-up: while seated or getting up, movement input must not move
     *  the character (the key press only starts the stand-up). */
    bool IsMovementHeldBySitting() const;
    /** Slope tilt for spawned quadrupeds and mounts (visual only). */
    TMap<TWeakObjectPtr<AActor>, FWowGroundTilt> GroundTilts;
    /** Drain Life / Mind Flay style beams, one per channelling caster. */
    TMap<uint64, TWeakObjectPtr<AActor>> ChannelBeamActors;
    void UpdateChannelBeams();
    /** Loot beams over lootable corpses (world option "lootbeam"). */
    TMap<uint64, TWeakObjectPtr<AActor>> LootBeamActors;
    TMap<uint64, int32> LootBeamQualityHints; // rarest quality seen in a corpse's loot list (9 = money only)
    void UpdateLootBeams();
    /** Fade-in, flash decay, pulse and fire flicker for the lights spell visuals carry. */
    void AnimateSpellVisualLights();
    /** Weapon swaps hand/back halfway through the draw/sheathe gesture. */
    FTimerHandle SheathSwapTimer;
    /** Resolve a visible unit in the central focus area using its animated anatomy. */
    bool FindCameraFocusUnit(const FMinimalViewInfo& View, float SceneDepth, float& OutDepth) const;
    void AccumulateMouseLookDelta(float DeltaYaw, float DeltaPitch);

    /** Create gameplay HUD pieces that are intentionally skipped on glue screens. */
    void EnsureGameplayHudCreated();

    /** Online world-entry readiness used to keep the loading screen over model conversion. */
    void EndDisconnectedWorldSession();
    int32 GetPendingEntityModelSpawnCount() const { return PendingEntityModelSpawns.Num(); }
    int32 GetSpawnedEntityModelCount() const { return SpawnedEntityActors.Num(); }
    /** r415: the spawned entity actors (guid -> actor), read by the VR personal space. */
    const TMap<uint64, TObjectPtr<AActor>>& GetSpawnedEntityActors() const { return SpawnedEntityActors; }
    int32 GetReadyPendingEntityModelSpawnCount() const;
    uint64 GetEntityModelStreamingRevision() const { return EntityModelStreamingRevision; }

    /** Currently targeted entity GUID */
    UPROPERTY()
    uint64 TargetGuid = 0;

    /** Add a message to the combat log */
    /** bPlayerIsSource / bPlayerIsTarget drive the Combat Log filters (Self, What happened to me?). */
    void AddCombatMessage(const FString& Message, const FLinearColor& Color, bool bPlayerIsSource = false, bool bPlayerIsTarget = false);

    /** Combat log widget for displaying spell casts, damage, etc. */
    TSharedPtr<SWowCombatLog> CombatLog;

    /** Chat window widget with input and channel support */
    TSharedPtr<SWowChatWindow> ChatWindow;

    /** Action bar widget */
    TSharedPtr<SWowActionBar> ActionBarWidget;

    /** Minimap widget */
    TSharedPtr<SWowMinimap> MinimapWidget;

    /** Live top-down render target used by the minimap when world assets are available. */
    UPROPERTY()
    TObjectPtr<UTextureRenderTarget2D> MinimapRenderTarget;

    UPROPERTY()
    TObjectPtr<class UTexture2D> MinimapPlayerArrowTexture;

    UPROPERTY()
    TObjectPtr<ASceneCapture2D> MinimapCaptureActor;

    /** Isolated, throttled renderer for the stock Character Sheet paper doll. */
    UPROPERTY()
    TObjectPtr<AWowCharacterPreview> CharacterSheetPreview;

    /** Dressing room preview (r300, WowGameplayControllerDressUp.cpp). */
    UPROPERTY()
    TObjectPtr<AWowCharacterPreview> DressUpPreview;
    TSharedPtr<FWowCharacterInfo> DressUpInfo;
    uint32 DressUpPendingItem = 0;
    uint32 DressUpPendingCreature = 0;
    double DressUpPendingUntil = 0.0;
    bool bDressUpShowingCreature = false;

	TWeakObjectPtr<AWowSkyManager> CachedSkyManager;
	float MinimapClockUpdateAccumulator = 0.0f;
	double NextMinimapWorldCaptureTimeSeconds = 0.0;
    float LastMinimapCaptureWidth = 0.0f;
    double LastMinimapRenderTime = -1.0;
	FVector2D LastMinimapWorldCaptureCenter = FVector2D::ZeroVector;
	bool bHasMinimapWorldCaptureCenter = false;
	bool bMinimapUiDelegatesBound = false;
	uint8 LastLfgModeForFrameXml = 0;

    /** Party frame widget */
    TSharedPtr<SWowPartyFrame> PartyFrameWidget;

    /** Tracks whether the current raid roster already requested icon sync */
    bool bRaidIconsRequested = false;
    uint64 LastRaidGroupGuid = 0;
    uint64 LastKnownLocalPetGuid = 0;

    /** Party invite dialog widget */
    TSharedPtr<SWowPartyInvite> PartyInviteWidget;
    TSharedPtr<SWowDuelInvite> DuelInviteWidget;
    TSharedPtr<SWidget> DuelInviteViewportWidget;
    /** Item destroy confirmation (drag from bags into the world) and held-item cursor icon. */
    TSharedPtr<class SWowDestroyItemDialog> DestroyItemDialog;
    TSharedPtr<SWidget> DestroyItemViewportWidget;
    TSharedPtr<SWidget> CursorItemIconWidget;
    TSharedPtr<SBorder> DuelStatusBorder;
    TSharedPtr<STextBlock> DuelStatusText;
    TSharedPtr<SWidget> DuelStatusViewportWidget;
    FString DebugDuelPreviewMode;
    bool bDebugDuelPreviewApplied = false;
    TSet<uint64> PendingDuelNameQueries;

    /** Taxi map widget */
    TSharedPtr<SWowTaxiMap> TaxiMapWidget;

    /** Quest log widget */
    TSharedPtr<SWowQuestLog> QuestLogWidget;
    TSharedPtr<class SWowSkills> SkillsWidget;
    TSharedPtr<class SWowNpcServices> NpcServicesWidget;
    /** Auction House window (AuctionFrame replica), opened by MSG_AUCTION_HELLO. */
    TSharedPtr<class SWowAuctionHouse> AuctionHouseWidget;
    /** Unit frame right-click menu (UnitPopup) and the Inspect window with its own model preview. */
    TSharedPtr<class SWowUnitMenu> UnitMenuWidget;
    TSharedPtr<class SWowInspectFrame> InspectWidget;
    UPROPERTY()
    TObjectPtr<AWowCharacterPreview> InspectPreview;
    bool OpenUnitFrameMenu(int64 HitFrame);
    void OpenInspect(uint64 Guid);
    TSharedPtr<class SWowWorldMap> WorldMapWidget;
    void OnSkillsKey();
    void UpdateBonusActionBar();

    /** Wrath Dungeon Finder and manual raid browser. */
    TSharedPtr<SWowGroupFinder> GroupFinderWidget;

    /** Quest tracker widget */
    TSharedPtr<SVerticalBox> QuestTrackerWidget;
    /** Ctrl-click in the objectives tracker: these quests get a diamond in the world. */
    TArray<uint32> WorldTrackedQuests;
    TSharedPtr<class SWowQuestDiamond> QuestDiamondLayer;
    /** Ground height under each quest's marker (world X/Y, WoW Z, next re-trace time). */
    struct FQuestDiamondGround { FVector2D XY = FVector2D::ZeroVector; float Z = 0.f; double NextTrace = 0.0; };
    TMap<uint32, FQuestDiamondGround> QuestDiamondGround;
    bool GetQuestDiamondTarget(uint32 QuestId, FVector& OutWorld, float& OutYards);
    /** r255: the world marker layer (quest diamonds + the corpse skull while a ghost). */
    void EnsureQuestDiamondLayer();
    /** r255: where your corpse lies while you are a ghost on its map (UE units, yards away). */
    bool GetCorpseMarker(FVector& OutWorld, float& OutYards) const;
    TWeakObjectPtr<class UWidgetComponent> VrCorpseCard;
    /** r257 Experimental immersive interface (WowGameplayControllerImmersive.cpp). */
    void UpdateImmersiveInterface(float DeltaTime);
    float ImmersiveAlpha[4] = {1.f, 1.f, 1.f, 1.f}; // minimap, action bars, unit frames, everything else
    double ImmersiveLastMoveTime = 0.0;
    TMap<FString, float> ImmersiveBaseOpacity;
    void OnQuestTrackerClicked(uint32 QuestId, const FPointerEvent& Event);
    TSharedPtr<STextBlock> QuestTrackerTitleText;
    TSharedPtr<SWidget> QuestTrackerContainerWidget;

    /** Experience bar widgets */
    TSharedPtr<SProgressBar> XPBar;
    TSharedPtr<STextBlock> XPText;

    /** CTRL+R performance readout, positioned above the action bar. */
    TSharedPtr<class SWidget> PerformanceOverlayViewportWidget;
    TSharedPtr<class SWowWeatherLens> WeatherLensWidget;
    TSharedPtr<STextBlock> PerformanceFpsText;
	TSharedPtr<STextBlock> PerformanceTimingText;
	TSharedPtr<STextBlock> PerformanceLimitText;
    TSharedPtr<STextBlock> PerformanceUpscalerText;
    TSharedPtr<STextBlock> PerformanceFrameGenerationText;

    /** Spellbook widget */
    TSharedPtr<class SWowSpellbook> SpellbookWidget;

    /** Talent window widget */
    TSharedPtr<class SWowTalentWindow> TalentWindowWidget;

    /** Guild roster widget */
    TSharedPtr<class SWowGuildRoster> GuildRosterWidget;

    /** Mailbox widget */
    TSharedPtr<class SWowMailbox> MailboxWidget;

    /** Native WotLK social window (Friends / Who / Guild / Chat / Raid). */
    TSharedPtr<class SWowFriendsFrame> FriendsFrameWidget;
    void CreateFriendsFrame();
    /** Native macro window (Game Menu > Macros, /macro): SWowMacroFrame over FWowMacros. */
    TSharedPtr<class SWowMacroFrame> MacroFrameWidget;
    void ToggleMacroFrame();
    /** Any FrameXML path that shows FriendsFrame toggles the native window instead. */
    void RedirectFrameXmlFriendsFrame();

    /** Death / ghost / resurrection events and auto-release (WowGameplayControllerDeath.cpp). */
    void UpdateDeathState(float DeltaTime);
    bool bDeathHooksBound = false;
    double NextDeathRangeCheck = 0.0;

    /** Escape menu overlay */
    TSharedPtr<class SWidget> GameMenuViewportWidget;
    TSharedPtr<class SWidget> KeyBindingsViewport;
    bool ExecuteSavedBinding(const FString& Command,bool Pressed=true);
    TSharedPtr<class SWowKeyBindings> KeyBindingEditor;
    uint64 LastSelectedGuid=0, LastHostileGuid=0;
    void CycleBindingTarget(bool Enemy,bool PlayerOnly,bool Previous);
    int32 BindingCameraView=0;
    bool bBindingMoveAndSteer=false;
    TSharedPtr<class SBorder> GameMenuPanel;
    TSharedPtr<class SButton> GameMenuResumeButton;
    TSharedPtr<class SButton> GameMenuLogoutButton;
    TSharedPtr<class SButton> GameMenuExitButton;
    TSharedPtr<class SWidget> GameMenuCountdownViewportWidget;
    /** The logout / exit countdown dialog itself (hit test for world clicks). */
    TSharedPtr<class SWidget> GameMenuCountdownPanel;
    TSharedPtr<class STextBlock> GameMenuCountdownText;
    /** Native, correctly scaled Video/Sound options shared with the login screen. */
    TSharedPtr<class SWowLoginOptions> GameplayOptionsViewportWidget;
    FTimerHandle GameMenuCountdownTimerHandle;
    double GameMenuCountdownEndTime = 0.0;
    bool bGameMenuCountdownExitsGame = false;

    /** Cast a spell on the current target (or self if no target) */
    UFUNCTION(BlueprintCallable)
    void CastSpell(int32 SpellId);

    /** Start auto-attack on targeted entity (right-click) */
    void StartAutoAttack();

    /** Stop auto-attack */
    void StopAutoAttack();

    /** Get auto-attack state */
    UFUNCTION(BlueprintCallable)
    bool IsAutoAttacking() const { return bIsAutoAttacking; }

private:
    // Movement sync
    void SendMovementUpdate();
    FVector LastSentPosition = FVector::ZeroVector;
    uint32 LastSentMovementFlags = 0;
    float LastSentYawDegrees = TNumericLimits<float>::Lowest();
    float MovementSyncTimer = 0.0f;
    float MovementSyncInterval = 0.2f; // zone/UI bookkeeping cadence

    // Outgoing MovementInfo state (3.3.5 client semantics). Network flags are
    // kept separate from the local animation flags, which use their own
    // jump/fall convention.
    struct FWowOutgoingMoveState
    {
        bool bValid = false;
        uint32 Flags = 0;
        float Orientation = 0.0f;
        double LastSendSeconds = 0.0;
        double LastFacingSendSeconds = 0.0;
        bool bAirborne = false;
        double AirborneStartSeconds = 0.0;
        float JumpZSpeed = 0.0f;
        float JumpSin = 0.0f;
        float JumpCos = 1.0f;
        float JumpXYSpeed = 0.0f;
    } OutgoingMove;
    // Persistent server-applied modifiers that must be echoed in MovementInfo.
    uint32 ServerMovementModifierFlags = 0;
    bool bServerRooted = false;
    void ResetOutgoingMovementState();
    FWowOutgoingMovementInfo BuildLocalMovementInfo(class ACharacter* Char, uint32 Flags) const;
    uint32 ComputeLocalMovementFlags(class ACharacter* Char) const;
    void OnForcedMovement(const FWowForcedMovement& Change);

    // Keep-alive
    float KeepAliveTimer = 0.0f;
    float KeepAliveInterval = 30.0f;

    // Aura display update
    float AuraUpdateTimer = 0.0f;
    float AuraUpdateInterval = 0.5f;

    // Delayed health/mana event firing (to ensure bars update after login)
    FTimerHandle DelayedHealthUpdateTimer;
    bool bDelayedHealthUpdateFired = false;

	// Environmental mirror timers. Online state is server-authoritative; offline
	// state uses the same FrameXML bars and returns the player to a safe position
	// after prolonged drowning or out-of-bounds exhaustion.
	void UpdateEnvironmentalMirrorTimers(float DeltaTime);
	void OnMirrorTimerStarted(uint32 TimerId, int32 ValueMs, int32 MaxValueMs,
		int32 Scale, bool bPaused, uint32 SpellId);
	void OnMirrorTimerPaused(uint32 TimerId, bool bPaused);
	void OnMirrorTimerStopped(uint32 TimerId);
	void FireMirrorTimerStartEvent(uint32 TimerId, int32 ValueMs, int32 MaxValueMs,
		int32 Scale, bool bPaused);
	void FireMirrorTimerStopEvent(uint32 TimerId);
	TWeakObjectPtr<AWowWorldManager> EnvironmentalWorldManager;
    uint32 LastUiAreaId=0,LastUiZoneId=0;
	FVector LastSafeEnvironmentalPosition = FVector::ZeroVector;
	bool bHasLastSafeEnvironmentalPosition = false;
	bool bOfflineBreathHazard = false;
	bool bOfflineBreathRefilling = false;
	bool bOfflineFatigueHazard = false;
	bool bOfflineFatigueRefilling = false;
	bool bServerMirrorTimerActive[3] = { false, false, false };
	/** Set once the realm sends any mirror timer: from then on breath/fatigue bars are server-only. */
	bool bServerProvidesMirrorTimers = false;
	float OfflineBreathExhaustionSeconds = 0.0f;
	float OfflineFatigueExhaustionSeconds = 0.0f;
	int32 OfflineBreathDamageTicks = 0;
	int32 OfflineFatigueDamageTicks = 0;

    // Server position sync
    void OnLoginVerifyWorld(uint32 MapId, float X, float Y, float Z, float Orientation);
    void OnEntityUpdated(const FWowEntity& Entity);

    // Teleport handling
    void OnTeleportRequest(uint64 Guid, uint32 Flags, uint32 Time, FVector Position, float Orientation);
    void OnMapTransfer(uint32 MapId, float X, float Y, float Z, float Orientation);
    void SendTeleportAck(uint64 Guid, uint32 Flags, uint32 Time);
    void SendWorldportAck();
    bool bHasServerPosition = false;

public:
    /** r332 (Stefan: scrolling the chat also zoomed the camera): the wheel belongs to the UI when the
     *  pointer is over a Lua frame or a native window. */
    bool IsPointerOverUiForWheel() const { return GetUiFrameUnderCursor() >= 0 || IsPointerOverNativeGameUi(); }
    /** r262: the mouse wheel over the interface (the dressing room model zooms). */
    void HandleUiWheel(float Delta);
    /** Deferred spawn: store the position but don't teleport yet */
    bool bDeferSpawnTeleport = false;
    FVector DeferredSpawnPos = FVector::ZeroVector;
    float DeferredSpawnOrientation = 0.0f;
    bool bHasDeferredSpawn = false;
    struct FPendingNamedChat
    { uint8 Type; uint32 Language; uint64 Guid; FString Message,Channel; double Expires; };
    TArray<FPendingNamedChat> PendingNamedChat;
    bool bMapTerrainPending = false;

    /** Apply the deferred spawn position now (called by LoginController after terrain loads) */
    void ApplyDeferredSpawn();

    /** Apply the selected character-list appearance before full server entity data arrives. */
    void SetupLocalPlayerCharacterModelFromCharacterInfo(const FWowCharacterInfo& CharacterInfo);
    /** Rebuild our own model from the current visible items (equipment changes, r258). */
    void RebuildLocalAppearanceFromFields(int32 Attempt);
    bool TrySetupLocalPlayerCharacterModelFromCachedCharacters();
    void EnsureVisibleLocalPlayerCharacterModel();

    /** Create and show the action bar widget */
    void CreateActionBarWidget();
	void TogglePerformanceOverlay();
	void CreatePerformanceOverlay();
	void UpdatePerformanceOverlay(float DeltaTime);

    /** Create and show the minimap widget */
    void CreateMinimapWidget();
    void EnsureMinimapWorldCapture();
    void UpdateMinimapWorldCapture();
	void BindMinimapUiDelegates();
	/** Lua ReloadUI(): rebuild the UI and replay current game state into it. */
	void ReloadUiNow();
	void EnsureCharacterSheetPreview();

    // ── Dressing room (r300, Claude-Perf) ── for DressUpFrame / DressUpModel (Claude-UI wires it).
    /** Creates the preview on first use and resumes its capture; the target to draw in DressUpModel. */
    UTextureRenderTarget2D* EnsureDressUpPreview();
    /** Stop capturing (DressUpFrame hidden). The model is kept for the next open. */
    void CloseDressUpPreview();
    /** Try an item on (gear) or, with Options > Experimental "Mount preview", show a mount.
     *  Waits for the item/creature query when needed. False when the item has nothing to show. */
    bool DressUpItem(uint32 ItemEntry);
    /** Back to the player's current gear (DressUpModel:SetUnit("player") / Reset button). */
    void DressUpReset();
    /** Take everything off (DressUpModel:Undress()). */
    void DressUpUndress();
    void DressUpRotate(float YawDegrees);
    /** Live appearance + visible gear of the local player (also what the paper doll shows). */
    bool BuildLiveCharacterInfo(FWowCharacterInfo& Info) const;
    void UpdateDressUpPreview();
    /** r331: Experimental "NPC/Player HD models": swap nearby models to HD one at a time. */
    void UpdateHdUpgrades();

    /** Create and show the chat window */
    void CreateChatWindow();

    /** Create target frame widget */
    void CreateTargetFrame();

    /** Update target frame display */
    void UpdateTargetFrame();

    /** Set target and update UI highlighting */
    void SetTarget(uint64 NewTargetGuid);

    /** Handle keyboard input for action bar slots */
    void HandleActionBarInput();

private:
    // Action bar input handling
    void OnActionSlot1() { CastSpellFromSlot(0); }
    void OnActionSlot2() { CastSpellFromSlot(1); }
    void OnActionSlot3() { CastSpellFromSlot(2); }
    void OnActionSlot4() { CastSpellFromSlot(3); }
    void OnActionSlot5() { CastSpellFromSlot(4); }
    void OnActionSlot6() { CastSpellFromSlot(5); }
    void OnActionSlot7() { CastSpellFromSlot(6); }
    void OnActionSlot8() { CastSpellFromSlot(7); }
    void OnActionSlot9() { CastSpellFromSlot(8); }
    void OnActionSlot0() { CastSpellFromSlot(9); }
    void OnActionSlotMinus() { CastSpellFromSlot(10); }
    void OnActionSlotEquals() { CastSpellFromSlot(11); }

    void CastSpellFromSlot(int32 SlotIndex);

    // Name query handling
    void OnPlayerNameReceived(uint64 Guid, const FString& Name);
    void OnCreatureNameReceived(uint32 Entry, const FString& Name, const FString& Title);

private:
    void ApplyDeferredSpawn_Internal(const FVector& SpawnPos, float Orientation);

    // Server position correction
    FVector LastServerPosition = FVector::ZeroVector;
    float ServerCorrectionThreshold = 500.0f; // 5 WoW yards — teleport if diverged

    // Opcode → UI event forwarding
    void OnOpcodeReceived(uint16 Opcode);

    // Targeting
    void OnLeftClick();
    void OnLeftClickReleased();
	void BeginLeftMouseLook();
	void EndLeftMouseLook();
	void CancelLeftMouseLookForRightMouseChord();
    void BeginRightMouseLook();
    void EndRightMouseLook();
    float QuestSparkleRefresh = 0.0f;
    bool TryTargetUnderCursor(bool bClearTargetOnMiss = true, uint64* ClickedGuid = nullptr);
    /** Dead/feign-dead units lie outside their standing click capsule: pick them by their posed bones. */
    uint64 FindCorpseAlongRay(const FVector& Origin, const FVector& Direction, float MaxDistance) const;
    int64 GetUiFrameUnderCursor() const;
    bool IsPointerOverNativeGameUi() const;
    /** A FrameXML frame (bag, window) is under the cursor where it covers the objectives tracker. */
    bool IsFrameXmlOverTracker() const;
	void CreateSoftwareCursor();
	void UpdateSoftwareCursor(float DeltaTime);
	void RefreshSoftwareCursorBrush();
	void ApplyMouseLookDelta();
	void EnsureGameplayCameraRotationInitialized();
	void ApplyGameplayCameraRotation();
	void PrimeMouseLookAnchor();
	void CenterMouseLookCursor();
	void OnMouseTurn(float Value);
	void OnMouseLookUp(float Value);
	void ApplyKeyboardTurn(float DeltaTime);
	bool IsMouseButtonActuallyDown(const FKey& Button) const;
	void UpdateMouseLookReleaseGuards();
	void HandleApplicationWillDeactivate();
	void HandleApplicationHasReactivated();
	void ForceReleaseMouseLookForFocusChange();
	void SyncCameraYawToPawnFacing();
	void OnTurnLeftPressed();
	void OnTurnLeftReleased();
	void OnTurnRightPressed();
	void OnTurnRightReleased();
	void OnToggleSheathKey();
	void OnToggleSitKey();
	/** bInstantStand: moving while seated pops the character straight up and lets it run (3.3.5);
	    only the sit key plays the slow stand-up clip with movement held. */
	void SetLocalSitting(bool bSitting, bool bInstantStand = false);
	void ApplyLocalWeaponPresentation();
    void DrawLocalMeleeWeapons();
#if WITH_DEV_AUTOMATION_TESTS
    friend class FWowMeleeWeaponPresentationTest;
    friend class FWowRemoteCombatMotionTest;
#endif
    bool HandleUiMousePress(const FString& Button, int64& PressedFrameHandle, bool bAllowDragDrop);
    bool HandleUiMouseRelease(const FString& Button, int64& PressedFrameHandle);

    bool bRightMouseLooking = false;
	bool bLeftMouseLooking = false;
	bool bLeftMouseDragged = false;
	float LeftMouseDragDistance = 0.0f;
	float PendingMouseTurn = 0.0f;
	float PendingMouseLookUp = 0.0f;
	float KeyboardTurnRateDegrees = 150.0f;
	float MouseLookYawSensitivity = 0.065f;
	float MouseLookPitchSensitivity = 0.055f;
	float MinMouseLookPitch = -78.0f;
	float MaxMouseLookPitch = 58.0f;
	bool bTurnLeftHeld = false;
	bool bTurnRightHeld = false;
	bool bLocalSitting = false;
	/** Sat down because of a logout/exit countdown; stand up again if it is cancelled. */
	bool bSatForLogout = false;
	void BeginLogoutSit();
    bool bLocalSitTransition = false;
    // Standing up from a sit roots the player until the stand-up animation has finished.
    bool bLocalStandingUp = false;
    double LocalStandUpLockUntil = 0.0;
	bool bLocalSheathStateInitialized = false;
	uint8 LocalSheathState = 0;
	bool bDebugLeftDragCameraOnlyVerified = false;
	bool bPerformanceOverlayEnabled = false;
	float PerformanceSampleSeconds = 0.0f;
	int32 PerformanceSampleFrames = 0;
	float LastMeasuredFps = 0.0f;
	bool bHasDesiredFacingYaw = false;
	float DesiredFacingYawDegrees = 0.0f;
	bool bHasGameplayCameraRotation = false;
	float GameplayCameraYawDegrees = 0.0f;
	float GameplayCameraPitchDegrees = -24.0f;
	bool bHasLastMouseLookPosition = false;
	FVector2D LastMouseLookPosition = FVector2D::ZeroVector;
	bool bHasMouseLookAnchor = false;
	bool bIgnoreNextMouseLookDelta = false;
	/** After focus returns, ignore the activation click until both mouse buttons have been released. */
	bool bMouseInputNeedsRearmAfterFocusChange = false;
	FVector2D MouseLookAnchorPosition = FVector2D::ZeroVector;
	float LeftMouseLookStartedAt = 0.0f;
	float RightMouseLookStartedAt = 0.0f;
	int32 LeftMouseReleaseMissingFrames = 0;
	int32 RightMouseReleaseMissingFrames = 0;
	/** True while both mouse buttons are held for WoW-style forward movement. */
	bool bMouseChordMovingForward = false;
	bool bHasSavedLeftMousePosition = false;
	int32 SavedLeftMouseX = 0;
	int32 SavedLeftMouseY = 0;
    bool bHasSavedRightMousePosition = false;
    int32 SavedRightMouseX = 0;
    int32 SavedRightMouseY = 0;
	float CursorAssetRetrySeconds = 0.0f;
	TSharedPtr<class SImage> SoftwareCursorImage;
    float CursorHoverAccumulator = 0.05f;
    uint64 HoveredCursorGuid = 0;
    TSharedPtr<class SWidget> SoftwareCursorViewportWidget;
	FSlateBrush SoftwareCursorBrush;
	EWowCursorType LastSoftwareCursorType = EWowCursorType::Point;
	bool bSoftwareCursorSuppressed = false;
	bool bShowNativeCursorWhileSoftwareSuppressed = false;

    // Entity model spawning
    UPROPERTY() TMap<uint64, TObjectPtr<class AWowUnitPortrait>> UnitPortraits;
    class UMaterialInterface* GetUnitPortrait(uint64 Guid);
    void OnEntityCreated(const FWowEntity& Entity);
    void OnEntityDestroyed(uint64 Guid);
    void SpawnEntityModel(const FWowEntity& Entity);
    void QueueEntityModelSpawn(uint64 Guid);
    void ProcessPendingEntityModelSpawns(float DeltaTime);
	void ProcessPendingEntityAppearanceUpdates(float DeltaTime);
	bool IsEntityModelInStreamingRange(const FWowEntity& Entity) const;
    void SetupLocalPlayerCharacterModel(const FWowEntity& Entity);
    void EnsureEntityNameplate(const FWowEntity& Entity, AActor* HostActor);
    void RefreshWorldLabels();
    void OnToggleNameplates();
    bool IsWorldUnitAttackable(const FWowEntity& Entity) const;
    FLinearColor WorldUnitColor(const FWowEntity& Entity) const;
    void CacheWorldResources();

    // Entity movement interpolation
    void UpdateEntitySplineMovement(float DeltaTime);
    void UpdateLocalServerSpline(FWowEntity& Entity, float DeltaTime);
    void UpdateEntityNetworkMovement(float DeltaTime);

    // Animation state management
    void UpdateEntityAnimations();
    void UpdatePlayerAnimations();

    UPROPERTY()
    TMap<uint64, TObjectPtr<AActor>> SpawnedEntityActors;

	UPROPERTY()
	TMap<uint64, TObjectPtr<AActor>> SpawnedEntityRiderActors;

	UPROPERTY()
	TMap<uint64, TObjectPtr<UWowAnimationController>> SpawnedEntityAnimationControllers;

	UPROPERTY()
	TMap<uint64, TObjectPtr<UWowAnimationController>> SpawnedEntityRiderAnimationControllers;

	TMap<uint64, uint32> SpawnedEntityAppearanceSignatures;
	TMap<uint64, uint32> PendingEntityAppearanceSignatures;
	TMap<uint64, float> PendingEntityAppearanceStableAt;
	float EntityAppearanceRebuildTimer = 0.0f;

    /** Units whose create packet arrived before world resources/model fields were ready. */
    TSet<uint64> PendingEntityModelSpawns;
    TMap<uint64, int32> EntityModelSpawnAttempts;
	TMap<uint64, float> EntityModelSpawnFirstQueuedAt;
    TMap<uint64, float> EntityModelSpawnNextAttemptAt;
    float EntityModelReconcileTimer = 2.0f;
    float EntityModelSpawnRetryTimer = 0.0f;
    uint64 EntityModelStreamingRevision = 0;

    /** Entities still travelling toward their latest network sample this frame. */
    TSet<uint64> VisuallyMovingEntityGuids;
    TMap<uint64,float> EntityVisualSpeedsCm;
    TMap<uint64,uint64> ServerMeleeVictims;
    void ForgetEntityCombatMotion(uint64 Guid);

    UPROPERTY()
    TMap<uint64, TObjectPtr<UWidgetComponent>> EntityNameplates;
    struct FWorldLabelAnchor { TWeakObjectPtr<AActor> Root; float Height=150; };
    TMap<uint64,FWorldLabelAnchor> WorldLabelAnchors;

    /** Quest givers already queried during the current visibility lifetime. */
    TSet<uint64> QuestStatusQueriesSent;
    double NextQuestMarkerRefresh=0;

    FMpqManager* CachedMpq = nullptr;
    FWowAssetCache* CachedAssetCache = nullptr;

    // Cast bar widget
    TSharedPtr<SWowCastBar> CastBarWidget;

    /** Target frame widgets */
    TSharedPtr<class STextBlock> TargetFrameText;
    TSharedPtr<class SProgressBar> TargetHealthBar;
    TSharedPtr<class STextBlock> TargetHealthText;

    /** Player frame widgets */
    TSharedPtr<class SProgressBar> PlayerHealthBar;
    TSharedPtr<class STextBlock> PlayerHealthText;
    TSharedPtr<class STextBlock> PlayerNameText;

    /** Aura display widgets */
    TSharedPtr<class SHorizontalBox> PlayerBuffBar;
    TSharedPtr<class SHorizontalBox> TargetBuffBar;
    void UpdateAuraDisplay();

    // Target selection highlight
    UPROPERTY()
    TObjectPtr<class UProceduralMeshComponent> TargetHighlightCircle;
    UPROPERTY()
    TObjectPtr<class UProceduralMeshComponent> HoverHighlightCircle;
    /** Ring geometry is re-traced onto the ground only when its centre or size changes. */
    FVector RingBuiltFeet = FVector(BIG_NUMBER);
    float RingBuiltRadius = 0.f;
    UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> HoverModelLights;
    TMap<TWeakObjectPtr<UMeshComponent>, uint8> HoverMeshChannels;
    TWeakObjectPtr<AActor> HoverLitActor;
    void UpdateHoveredModel(uint64 Guid);
    float TargetHighlightAccumulator = 0.1f;
    void UpdateTargetHighlight();
    void UpdateUnitHighlight(uint64 Guid, TObjectPtr<class UProceduralMeshComponent>& Circle, bool bHover);

    // Death + looting
    void OnEntityDeath(uint64 Guid);
    TSet<uint64> DeadEntityGuids; // Entities that died and are lootable corpses
    TMap<uint64, TArray<TWeakObjectPtr<class UAudioComponent>>> CasterSpellSounds; // stopped on interrupt
    TSet<uint64> FeignDeadEntityGuids; // Alive units the server shows as corpses (feign death / dead flag)

    // Mouse click polling (reliable across all input modes)
    bool bLeftMouseWasDown = false;
    bool bRightMouseWasDown = false;
    bool bNativeUiOwnsLeftMousePress = false;
	bool bDebugChatMessageSubmitted = false;
	bool bPlayerEnteredWorld = false;
	bool bGameplayHudVisible = false;
    int32 WorldEntryFocusFramesRemaining = 0;

    // Combat state
    bool bIsAutoAttacking = false;
    uint64 AutoAttackTargetGuid = 0;
    bool bWasInCombat = false;
    bool bPlayerHealthInitialized = false;
    TSet<uint64> PendingFriendNameQueries;

    // Spell casting
    bool bIsCasting = false;
    bool bIsChanneling = false;
    int32 CurrentSpellId = 0;
    float CastStartTime = 0.0f;
    float CastDuration = 0.0f;
    float SpellBarEndTime = 0.0f;
    uint64 ActiveLocalChannelCasterGuid = 0;
    TMap<uint64, uint32> ActiveTimedCastSpells;
    TMap<uint64, uint32> ActiveChannelSpells;

    struct FSpellWeaponRestoreState
    {
        TWeakObjectPtr<USceneComponent> Component;
        TWeakObjectPtr<USceneComponent> OriginalParent;
        FName OriginalSocket = NAME_None;
        FTransform OriginalRelativeTransform = FTransform::Identity;
    };
    TMap<uint64, TArray<FSpellWeaponRestoreState>> SpellSheathedWeapons;
    TMap<uint64, double> PendingSpellWeaponRestoreAt;
    void SheathCasterWeaponsForSpell(uint64 CasterGuid, uint32 SpellId, float SafetyDurationSeconds);
    void ScheduleCasterWeaponRestore(uint64 CasterGuid, float DelaySeconds);
    void RestoreCasterWeaponsAfterSpell(uint64 CasterGuid);
    void UpdateSpellWeaponSheaths();

    // Opt-in rendered spell verification. These fields remain inert unless
    // -testspell=<id> is present on the command line.
    int32 DebugSpellPreviewId = 0;
    FString DebugSpellPreviewPhase;
    bool bDebugSpellPreviewStarted = false;
    double DebugSpellPreviewLastImpactTime = -1.0;
    void MaybeApplyDebugSpellPreview();

    // Combat log event handlers
    void OnSpellStart(uint64 CasterGuid, uint32 SpellId, uint32 CastFlags, int32 CastTime);
    void OnSpellGo(uint64 CasterGuid, uint64 SpellTargetGuid, uint32 SpellId, uint32 CastFlags);
    void OnSpellFailure(uint64 CasterGuid, uint32 SpellId, uint8 FailureReason);
    void OnChannelStart(uint64 CasterGuid, uint32 SpellId, uint32 DurationMs);
    void OnChannelUpdate(uint64 CasterGuid, uint32 RemainingMs);
    void OnSpellDelayed(uint64 CasterGuid, uint32 DelayMs);
	void FireUnitSpellcastEventForGuid(const FString& EventName, uint64 CasterGuid);
    void ShowSpellProgressBar(const FString& SpellName, float DurationSeconds, bool bChannel);
    void UpdateSpellProgressBar();
    void HideSpellProgressBar();
    class UWowAnimationController* FindSpellAnimationController(uint64 CasterGuid) const;
    void EndCasterSpellAnimation(uint64 CasterGuid);
    void OnAttackerStateUpdate(uint64 AttackerGuid, uint64 VictimGuid, uint32 HitInfo, uint32 Damage);
    /** Server combat log entry -> COMBAT_LOG_EVENT_UNFILTERED / COMBAT_LOG_EVENT (3.3.5 arguments) + spell combat text. */
    void OnCombatLogEntry(const struct FWowCombatLogEntry& Entry);
    uint32 CombatLogUnitFlags(uint64 Guid);
    FString CombatLogUnitName(uint64 Guid);
    void ShowCombatLogText(const struct FWowCombatLogEntry& Entry);
    TSet<uint64> CombatLogAliveUnits;
    void OnServerAttackStart(uint64 AttackerGuid, uint64 VictimGuid);
    void OnServerAttackStop(uint64 AttackerGuid, uint64 VictimGuid);
    void OnChatMessage(uint8 Type, uint32 Language, uint64 SenderGuid, const FString& SenderName, const FString& Message, const FString& Channel);
    TMap<FString, int32> ChatChannelNumbers;
	void OnFriendListUpdated();
    void OnSocialPlayerNameReceived(uint64 Guid, const FString& Name);
    void OnEmote(uint64 EntityGuid, uint32 EmoteId);
    // Unit + server-driven sounds (WowGameplayControllerSounds.cpp).
    void BindUnitSounds();
    void OnServerSound(uint32 SoundId, uint64 SourceGuid);
    void OnServerMusic(uint32 SoundId);
    void OnAIReaction(uint64 Guid, uint32 Reaction);
    void PlayCombatLogSounds(const struct FWowCombatLogEntry& Entry);
    void PlayNpcClickSound(uint64 NpcGuid);
    bool GetUnitSoundLocation(uint64 Guid, FVector& OutLocation) const;
    TMap<uint64, double> UnitSoundCooldowns;
    TMap<uint64, double> UnitAggroSoundTimes;
    TMap<uint64, TPair<double, int32>> NpcClickCounts;
    // Visual ground conform for NPCs (WowGameplayControllerGround.cpp): the body mesh is
    // nudged onto our terrain where the server's Z leaves a small gap or overlap.
    void ConformEntitiesToGround(float DeltaTime);
    // r599 (WowGameplayControllerTransports.cpp): boats/zeppelins (GO type 15) and elevators (type 11) moved
    // by the client along their paths from the server's path timer; UpdateEntityNetworkMovement skips them.
    void UpdateTransports(float DeltaTime);
    bool IsClientDrivenTransport(uint64 Guid) const;
    // r601: riding. The transport the pawn stands on (or jumps from), the ONTRANSPORT block for our movement
    // packets, and SMSG_NEW_WORLD on board (deck offset -> world position on the new map).
    uint64 LocalTransportGuid(class ACharacter* Char) const;
    void ApplyLocalTransport(class ACharacter* Char, FWowOutgoingMovementInfo& Info) const;
    bool ResolveTransportTransfer(uint32 MapId, float& X, float& Y, float& Z, float& Orientation);
    struct FGroundConform { double NextProbe = 0; float Target = 0, Offset = 0, BaseZ = 0; bool bHasBase = false;
        TWeakObjectPtr<class USceneComponent> Mesh; };
    TMap<uint64, FGroundConform> GroundConform;
    /** SMSG_TEXT_EMOTE: chat text from EmotesText/EmotesTextData + race/gender voice line (WowGameplayControllerChat.cpp). */
    void OnTextEmote(uint64 SenderGuid, uint32 TextEmoteId, uint32 EmoteNum, const FString& TargetName);
    void OnInitialSpells(const TArray<uint32>& SpellIds);
    void OnActionButtonsUpdated();
    void OnPetBarUpdated(const FWowPetActionBarState& PetActionBar);

    /** Play emote animation on a character actor */
    void PlayEmoteAnimation(AActor* CharacterActor, uint32 EmoteId);

    /** Map emote ID to animation ID */
    int32 GetAnimationForEmoteId(uint32 EmoteId);

    /** Map stand state to animation ID */
    EWowAnimId GetAnimationIdForStandState(uint8 StandState);

    // Party/Group event handlers
    void OnGroupUpdated();
    void OnRaidTargetsUpdated(const FWowRaidTargetState& RaidTargets);
    void OnReadyCheckUpdated(const FWowReadyCheckState& ReadyCheck);
    void OnDuelUpdated(const FWowDuelState& DuelState);
    void OnGroupInviteReceived(const FString& InviterName);
    void OnPartyCommandResult(uint8 Command, const FString& PlayerName, uint8 Result);

    // Taxi event handlers
    void OnTaxiNodesShown(uint64 NpcGuid, uint32 CurrentNodeId);
    void OnTaxiActivateReply(uint8 Result);

    /** Create and show party frame */
    void CreatePartyFrame();
    void CreateDuelStatusOverlay();
    void HideDuelInvite();
    void ShowDestroyItemDialog(const TArray<FString>& Args);
    void HideDestroyItemDialog(bool bConfirmed);
    bool IsDestroyItemDialogVisible() const;
    void CreateCursorItemIcon();
    void RefreshDuelUi();
    FString GetPlayerDisplayName(uint64 Guid) const;
    void MaybeRequestDuelPlayerName(const FWowDuelState& DuelState);
    void MaybeApplyDebugDuelPreview();

    /** Create experience bar */
    void CreateXPBar();

    /** Update experience bar (throttled) */
    void UpdateXPBar();

    /** Handle Enter key for chat input */
    void OnEnterKey();

    /** FrameXML chat bridge used by Enter/Escape and incoming server messages. */
    void SubmitFrameXmlChat(const FString& InputText);
    /** Unknown "/command": run it through SlashCmdList / emotes in Lua (WowGameplayControllerChat.cpp). */
    void RunLuaSlashCommand(const FString& Command, const FString& Arguments);
    FString LastWhisperSender;
    uint64 LastWhisperGuid = 0;
    void AddFrameXmlChatLine(const FString& Message, const FLinearColor& Color, bool bCombatLog = false);
    void CloseFrameXmlChatInput(bool bClearText = true);

    /** Handle B key for bag window */
    void OnBagKey();

	/** Handle Shift+B for all bags; currently constrained to live backpack data only. */
	void OnAllBagsKey();
	void PlayContainerToggleSound(bool bOpening) const;

    /** Handle C key for character panel */
    void OnCharacterKey();

    /** Handle L key for quest log */
    void OnQuestLogKey();
    /** Y / AchievementMicroButton: the native Achievement window. */
    void OnAchievementKey();
    TSharedPtr<class SWowAchievementFrame> AchievementWidget;
	/** Handle I key and minimap LFG button. */
	void OnGroupFinderKey();
	void CreateGroupFinder();
	void OnLfgUpdated();
	/** Toggle the classic Friends/Social window (default key: O). */
	void OnSocialKey();

    /** Create and show quest log */
    void CreateQuestLog();

    /** Create quest tracker widget */
    void CreateQuestTracker();

    /** Update quest tracker widget */
    void UpdateQuestTracker();
    /** Quest tracking kept per character between sessions (plus optional zone auto-tracking). */
    /** World chat bubbles over speakers. */
    void ShowChatBubble(uint64 Guid, const FString& Text, const FLinearColor& Color);
    bool GetChatBubbleAnchor(uint64 Guid, FVector2D& Out) const;
    TSharedPtr<class SWowChatBubbles> ChatBubbleLayer;
    void LoadTrackedQuests();
    void SaveTrackedQuests() const;
    void ApplyQuestTracking();
    void SyncTrackedQuestsFromLog();
    TSet<uint32> ManualTrackedQuests;
    TSet<uint32> ZoneUntrackedQuests;
    FString TrackedQuestsCharacter;

    /** Handle P key for spellbook */
    void OnSpellbookKey();
    void OnEscapeKey();
    void ShowGameMenu();
    void HideGameMenu();
    bool IsGameMenuVisible() const;
    bool IsNativeModalUiVisible() const;
    void PrepareForQuestDialogue();
    void ReleaseGameplayMouseLookForModalUi();
    bool CloseOpenUiPanelsForEscape();
    TSharedRef<class SWidget> CreateGameMenuButton(const FString& Label, FSimpleDelegate Action,
        bool bGoldText = false, TSharedPtr<class SButton>* OutButton = nullptr) const;
    FReply OnGameMenuResumeClicked();
    FReply OnGameMenuLogoutClicked();
    FReply OnGameMenuExitClicked();
    FReply OnGameMenuPlaceholderClicked(FString FeatureName);
    void ShowGameplayOptionsMenu(bool bOpenSoundPage = false);
    void HideGameplayOptionsMenu();
    void ShowGameMenuCountdown(bool bExitGame);
    void UpdateGameMenuCountdown();
    FReply CancelGameMenuCountdown();
    void CompleteGameMenuCountdown();
    UFUNCTION() void HandleServerLogoutComplete();

    /** Create spellbook widget */
    void CreateSpellbook();

    /** Handle N key for talent window */
    void OnTalentKey();

    /** Create talent window widget */
    void CreateTalentWindow();

    /** Handle J key for guild roster */
    void OnGuildRosterKey();

    /** Create guild roster widget */
    void CreateGuildRoster();

    /** Show mailbox for a mailbox NPC or mailbox gameobject */
    void ShowMailbox(int64 MailboxGuid);

    /** Create mailbox widget */
    void CreateMailbox();

    /** Close the mailbox when the player walks away from it. */
    void UpdateMailboxRange();

    /** Handle player inventory updates from server */
    UFUNCTION()
    void OnPlayerInventoryUpdated();

    /** Handle unit health updates from server */
    UFUNCTION()
    void OnUnitHealthUpdate(uint64 EntityGuid);

    /** Handle unit power updates from server */
    UFUNCTION()
    void OnUnitPowerUpdate(uint64 EntityGuid);

    /** Handle guild roster updates from server */
    UFUNCTION()
    void OnGuildRosterUpdated();

    /** Handle input mode changes from chat window */
    void OnChatInputModeChanged(bool bGameAndUI);
    void OnEntityHealthChanged(const FWowEntity& Entity, int32 OldHealth, int32 NewHealth);


    void OnRightClick();
    /** Right-click action on a world entity already resolved under the cursor (gameobject use,
     *  attack, NPC gossip/vendor/bank/mailbox), without the mouse-look a real right press starts. */
    void InteractWithWorldEntity(uint64 Guid);
    /** Loot a body (reports the position first so the server's range check passes). */
    void SendLootRequest(uint64 Guid);
    void OnRightClickReleased();
    void OnTabTarget();

    // Minimap input
    void OnToggleMap(); // 'M' key handler

    enum class ESpellVisualPhase : uint8
    {
        Precast,
        Cast,
        Impact,
        Channel,
        ChannelDone,
        Missile,
        Aura
    };

    enum class ESpellVisualSlot : uint8
    {
        Head,
        Chest,
        Base,
        LeftHand,
        RightHand,
        Breath,
        LeftWeapon,
        RightWeapon,
        Attachment // SpellVisualKitModelAttach: model on an M2 attachment point (r262)
    };

    struct FPendingSpellVisual
    {
        uint64 OwnerGuid = 0;
        uint64 AnchorGuid = 0;
        uint32 SpellId = 0;
        FString ModelPath;
        float Scale = 1.0f;
        ESpellVisualSlot Slot = ESpellVisualSlot::Base;
        ESpellVisualPhase Phase = ESpellVisualPhase::Impact;
        bool bPersistent = false;
        uint8 AttachmentId = 0;       // Slot == Attachment
        FVector AttachOffset = FVector::ZeroVector; // UE units, attachment space
        FRotator AttachRotation = FRotator::ZeroRotator;
    };

    struct FActiveSpellVisual
    {
        uint64 OwnerGuid = 0;
        uint32 SpellId = 0;
        ESpellVisualPhase Phase = ESpellVisualPhase::Impact;
        TWeakObjectPtr<AActor> Actor;
        double ExpireAt = 0.0;
        bool bPersistent = false;
    };

    // Spell visual effects. Requests are deliberately throttled to one M2 load
    // per tick so the first cast of a new spell cannot stall the game thread.
    /** Lightning Bolt / Earth Shock and other reworked spells drawn with UE effects. True when handled. */
    bool TrySpawnCustomSpellEffect(uint64 CasterGuid, uint64 EffectTargetGuid, uint32 SpellId, ESpellVisualPhase Phase);
    int32 SpawnSpellVisualEffect(uint64 CasterGuid, uint64 SpellTargetGuid,
        uint32 SpellId, ESpellVisualPhase Phase);
    int32 QueueSpellVisualKit(uint64 OwnerGuid, uint64 AnchorGuid, uint32 SpellId,
        uint32 KitId, ESpellVisualPhase Phase, bool bPersistent);
    void ProcessPendingSpellVisuals();
    void SyncAuraVisuals(uint64 UnitGuid);
    void RefreshDetectionAuraVisuals();
    uint8 LastDetectionViewerConcealment = 0;
    void RemoveSpellVisuals(uint64 OwnerGuid, bool bPersistentOnly = true);
    AActor* ResolveSpellActor(uint64 Guid) const;
    void SpawnSpellMissile(uint64 CasterGuid, uint64 SpellTargetGuid, uint32 SpellId);

    TArray<FPendingSpellVisual> PendingSpellVisuals;
    TArray<FActiveSpellVisual> ActiveSpellVisuals;

    UPROPERTY()
    TArray<TObjectPtr<AActor>> RetainedSpellVisualActors;

    // Active spell cast effects (destroyed when cast completes)
    UPROPERTY()
    TArray<TObjectPtr<UPointLightComponent>> ActiveCastEffects;

    TMap<uint64, TArray<TWeakObjectPtr<UPointLightComponent>>> ActiveCastEffectsByCaster;

    int64 PressedLeftMouseFrameHandle = -1;
    int64 PressedRightMouseFrameHandle = -1;
	int32 PendingActionDragSlot = -1;
    int64 PendingUiDragFrame = -1;
	FVector2D ActionDragStartPosition = FVector2D::ZeroVector;
	bool bUiDragActive = false;
	bool bCharacterSheetModelDragging = false;
	FVector2D LastCharacterSheetDragPosition = FVector2D::ZeroVector;
	/** Dressing room UI wiring (Claude-UI): DressUpFrame shown last tick, model drag-rotate. */
	bool bDressUpFrameWasShown = false;
	bool bDressUpModelDragging = false;
	FVector2D LastDressUpDragPosition = FVector2D::ZeroVector;
	/** FWowFrameManager::DressUpHandler: DressUpModel Lua methods and IsDressableItem. */
	bool HandleDressUpAction(const FString& Action, uint32 ItemEntry, float Value);

    void SpawnCastGlowEffects(uint64 CasterGuid, uint32 SpellId);
    void RemoveCastGlowEffects(uint64 CasterGuid);

    /** Initialize the audio system with the world manager */
    void InitializeAudioSystem(class AWowWorldManager* WorldManager);

private:
    /** Get spell school color based on SchoolMask */
    FLinearColor GetSpellSchoolColor(uint32 SchoolMask) const;

    /** Fire a UI event through the event system (helper method) */
    void FireUIEvent(const FString& EventName, const TArray<FString>& Args = {});
};
