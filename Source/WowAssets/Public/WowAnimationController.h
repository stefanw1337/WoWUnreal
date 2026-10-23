#pragma once
#include "CoreMinimal.h"
#include "WowEntity.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "WowAnimationController.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;
struct FM2AnimationData;
struct FM2AnimSequence;

/**
 * WoW M2 Animation IDs from AnimationData.dbc
 * Maps to specific character actions for state machine control.
 */
/**
 * WoW 3.3.5a Animation IDs from AnimationData.dbc.
 * These MUST match the actual DBC IDs — they're used to look up
 * animations from M2 model files via the animation cache.
 */
UENUM(BlueprintType)
enum class EWowAnimId : uint8
{
    Stand = 0,              // Idle standing
    Death = 1,              // Dying animation (plays once)
    Spell = 2,              // Generic spell animation
    Stop = 3,               // Stop moving
    Walk = 4,               // Walking
    Run = 5,                // Running
    Dead = 6,               // Dead pose (looping, after Death)
    Rise = 7,               // Resurrect rise
    StandWound = 8,         // Wound while standing
    CombatWound = 9,        // Wound while in combat
    CombatCritical = 10,    // Critical hit reaction
    ShuffleLeft = 11,       // Strafe left
    ShuffleRight = 12,      // Strafe right
    Walkbackwards = 13,     // Walk backwards
    Stun = 14,              // Stunned
    HandsClosed = 15,       // Hands closed idle
    AttackUnarmed = 16,     // Unarmed attack
    Attack1H = 17,          // One-handed melee attack
    Attack2H = 18,          // Two-handed melee attack
    Attack2HL = 19,         // Two-handed large melee attack
    ParryUnarmed = 20,      // Parry unarmed
    Parry1H = 21,           // Parry one-handed
    Parry2H = 22,           // Parry two-handed
    Parry2HL = 23,          // Parry two-handed large
    ShieldBlock = 24,       // Shield block
    ReadyUnarmed = 25,      // Ready stance unarmed
    Ready1H = 26,           // Ready stance one-handed
    Ready2H = 27,           // Ready stance two-handed
    Ready2HL = 28,          // Ready stance two-handed large
    ReadyBow = 29,          // Ready bow
    Dodge = 30,             // Dodge
    SpellPrecast = 31,      // Spell precast
    SpellCast = 32,         // Spell cast (generic)
    SpellCastArea = 33,     // Area spell cast
    NPCWelcome = 34,        // NPC greeting
    NPCGoodbye = 35,        // NPC farewell
    Block = 36,             // Block
    JumpStart = 37,         // Jump takeoff
    Jump = 38,              // Jumping (airborne)
    JumpEnd = 39,           // Jump landing
    Fall = 40,              // Falling
    SwimIdle = 41,          // Idle in water
    Swim = 42,              // Swimming
    SwimLeft = 43,          // Swim strafe left
    SwimRight = 44,         // Swim strafe right
    SwimBackward = 45,      // Swim backward
    AttackBow = 46,         // Bow attack
    FireBow = 47,           // Fire bow (projectile release)
    ReadyRifle = 48,        // Ready rifle
    AttackRifle = 49,       // Rifle attack
    Loot = 50,              // Looting
    ReadySpellDirected = 51, // Ready directed spell
    ReadySpellOmni = 52,    // Ready omni spell
    SpellCastDirected = 53,  // Directed spell cast
    SpellCastOmni = 54,     // Omni-directional spell cast
    SpellCastDirectedNose = 55, // BattleRoar / ReadyAbility
    Special1H = 57,         // Special one-handed attack
    Special2H = 58,         // Special two-handed attack
    ShieldBash = 59,        // Shield bash
    EmoteTalk = 60,         // Talk emote
    EmoteEat = 61,          // Eat emote
    EmoteWork = 62,         // Work emote
    EmoteUseStanding = 63,  // Use standing emote
    ChannelCast = 124,       // Channel cast directed
    ChannelCastOmni = 125,   // Channel cast omni-directional
    Whirlwind = 126,         // Whirlwind
    Kick = 95,              // Kick
    SitGroundDown = 96,     // Authored sit-down transition (AnimationData.dbc)
    SitGroundUp = 98,       // Authored ground sit-to-stand transition
    SitGround = 97,         // Sit on ground
    SitChairLow = 102,      // Low chair pose
    SitChairHigh = 104,     // High chair pose
    SitChair = 103,          // Sit on chair
    Sleep = 100,             // Sleeping
    Kneel = 115,             // Kneeling
	Mount = 91,             // Seated on a mount
    Dance = 69,            // Dance emote
    EmoteBow = 66,         // Bow emote
    FlyRun = 234,           // Flying run
    FlyWalk = 233,          // Flying walk
    FlyStand = 229,         // Flying stand
};

/** Deterministic spell animation choices shared by runtime and automation tests. */
struct WOWASSETS_API FWowSpellAnimationPolicy
{
    static EWowAnimId ResolveReadyAnimation(uint32 AuthoredAnimationId, bool bOmniDirectional);
    static EWowAnimId ResolveReleaseAnimation(uint32 AuthoredAnimationId, bool bOmniDirectional);
    static EWowAnimId ResolveChannelAnimation(uint32 AuthoredAnimationId, bool bOmniDirectional);
};

/**
 * Current animation state for the state machine.
 * Determines which animation should be playing based on entity conditions.
 */
UENUM(BlueprintType)
enum class EWowAnimState : uint8
{
    Idle,           // Standing still
    Walking,        // Moving slowly
    WalkingLeft,    // Walk with strafe-facing offset
    WalkingRight,
    Backpedaling,   // Walking backward
    BackpedalingLeft,  // Walking backward-left while preserving actor facing
    BackpedalingRight, // Walking backward-right while preserving actor facing
    StrafingLeft,   // Shuffling left
    StrafingRight,  // Shuffling right
    Running,        // Moving at normal speed
    Swimming,       // In water
    SwimmingIdle,   // Idle in water
    Jumping,        // Jump sequence
    Falling,        // Falling through air
    Combat,         // Combat animations
    Casting,        // Spell casting
    Channeling,     // Spell channeling
    Dead,           // Death state
    SwimmingLeft, SwimmingRight, SwimmingBackward,
    SwimmingForwardLeft, SwimmingForwardRight,
    SwimmingBackwardLeft, SwimmingBackwardRight,
    TurningLeft, TurningRight
};

/**
 * Pure movement-animation decisions shared by the runtime and automation tests.
 * Keep these rules free of UObject/world state so the accepted jump, landing,
 * fall and swim behaviour cannot be changed accidentally without a failing test.
 */
struct WOWASSETS_API FWowMovementAnimationPolicy
{
    static constexpr double ShortFallAnimationDelaySeconds = 0.20;
    static constexpr float MovingLandingSpeedThresholdCm = 35.0f;
    static constexpr float SwimmingIdleSpeedThresholdCm = 10.0f;

    static EWowAnimState ResolveAirborneState(
        bool bPlayerInitiatedJump,
        bool bRising,
        double AirborneSeconds,
        EWowAnimState PreviousState);

    static bool ShouldFreezeJumpStartAtApex(
        bool bPlayingJumpStart,
        bool bPlayerInitiatedJump,
        bool bDescending,
        bool bAlreadyHoldingApex);

    static bool ShouldPlayStationaryLanding(
        bool bHasMovementIntent,
        float HorizontalSpeedCmPerSecond,
        bool bHasJumpEndAnimation);

    static EWowAnimState ResolveSwimmingState(float SpeedCmPerSecond, uint32 MoveFlags = 0);
};

/**
 * WoW Movement flags for animation state detection.
 * These match the server movement flags from 3.3.5a.
 */
namespace WowMovementFlags
{
    static constexpr uint32 SWIMMING = 0x00200000;
    static constexpr uint32 FLYING = 0x02000000;
    static constexpr uint32 FALLING = 0x00001000;
    static constexpr uint32 FORWARD = 0x00000001;
    static constexpr uint32 BACKWARD = 0x00000002;
    static constexpr uint32 STRAFE_LEFT = 0x00000004;
    static constexpr uint32 STRAFE_RIGHT = 0x00000008;
    static constexpr uint32 TURN_LEFT = 0x00000010;
    static constexpr uint32 TURN_RIGHT = 0x00000020;
    static constexpr uint32 PITCH_UP = 0x00000040;
    static constexpr uint32 PITCH_DOWN = 0x00000080;
    static constexpr uint32 WALK_MODE = 0x00000100;
    static constexpr uint32 JUMPING = 0x00002000;
}

/**
 * Animation controller for WoW character models.
 * Manages animation state machine and switches between M2 animations based on
 * movement state, combat state, and other entity conditions.
 */
UCLASS(BlueprintType)
class WOWASSETS_API UWowAnimationController : public UObject
{
    GENERATED_BODY()
    friend class FWowCombatRegressionTest;
    friend class FWowRemoteCombatMotionTest;

public:
    UWowAnimationController();

    /**
     * Initialize the controller with a skeletal mesh component and available animations.
     * Caches animations by their M2 AnimationId for quick lookup.
     */
    UFUNCTION(BlueprintCallable)
    void Initialize(USkeletalMeshComponent* InMeshComponent, const TArray<UAnimSequence*>& Animations);

    /**
     * Update animation state based on movement info and entity conditions.
     * Call this from Tick() or when movement state changes.
     */
    
    void UpdateAnimationState(const struct FWowMovementInfo& MovementInfo, bool bIsInCombat = false, bool bIsCasting = false, float RenderedSpeedCm = -1.f);

    /**
     * Update animation state for local player based on UE5 character movement.
     * Translates UE5 movement component state to WoW animation state.
     */
    UFUNCTION(BlueprintCallable)
    void UpdateLocalPlayerState(class ACharacter* PlayerCharacter, bool bIsInCombat = false);

    EWowAnimId GetMeleeReadyAnimation() const;

    /**
     * Force a locomotion state for the local player when input intent is
     * clearer than inferred velocity. Used for pure strafing, where UE movement
     * can otherwise look like forward running with sideways sliding.
     */
    void ForceLocalLocomotionState(
        EWowAnimState NewState,
        float GroundSpeedCmPerSecond = 0.0f,
        float VisualYawDegrees = 0.0f);

    /** Mark an accepted local jump so its descent keeps the Jump clip until landing. */
    void NotifyLocalJumpStarted(float ActorZ);

    /** Clear the local jump sequence as soon as character movement reports a landing. */
    void NotifyLocalLanded(bool bHasMovementIntent = false);

    /** Interrupt a planted landing pose as soon as the local player requests movement. */
    void NotifyLocalMovementIntent();

    /**
     * Play a specific animation by M2 animation ID.
     * Returns true if animation was found and started.
     */
    UFUNCTION(BlueprintCallable)
    bool PlayAnimationById(EWowAnimId AnimId, bool bLooping = true);
    /** Draw/sheathe gesture (AnimationData 89 Sheath, 90 HipSheath) when standing still.
     *  Returns the clip length in seconds, or 0 when it was not played (moving, missing clip). */
    float PlaySheathAnimation(bool bHipWeapon);
    void ShowCorpsePose();
    /** Enter death once, or clear the corpse state on a server resurrection. */
    void SetDead(bool bDead);

    /** Play and lock an authored spell-ready loop until release/failure. */
    bool BeginSpellCast(EWowAnimId AnimId);

    /** Play an authored spell release once; locomotion resumes when it completes. */
    bool PlaySpellRelease(EWowAnimId AnimId);
    /** r372: a cast that starts while the previous cast's release clip still plays (queued casts)
     *  waits for that clip to finish; the caller ticks this while the unit is casting. */
    void TickPendingSpellCast();

    /** Play and lock an authored channel loop until the server ends the channel. */
    bool BeginSpellChannel(EWowAnimId AnimId);

    /** Release a spell/channel lock without forcing a planted Stand pose. */
    void EndSpellPresentation();

    /**
     * Play attack animation with fallback chain.
     * Tries Attack1H -> AttackUnarmed -> Attack2H -> Spell
     * Returns true if any animation was successfully played.
     */
    UFUNCTION(BlueprintCallable)
    bool PlayAttackAnimation();

    /**
     * Play wound animation with fallback chain.
     * Tries CombatWound -> StandWound
     * Returns true if any animation was successfully played.
     */
    UFUNCTION(BlueprintCallable)
    bool PlayWoundAnimation();

    /**
     * Get current animation state.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure)
    EWowAnimState GetCurrentState() const { return CurrentState; }

    /**
     * Check if the controller has been initialized.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsInitialized() const { return MeshComponent != nullptr && AllAnimations.Num() > 0; }

    /**
     * Check if a one-shot animation (attack, wound) is currently playing.
     * State machine updates should be skipped while this returns true.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool PlayAnimationValue(int32 Id, bool bLooping);
    bool PlayEmote(int32 AnimId, bool bLooping);
    void StopEmote();
    void SyncEmoteState(uint32 ServerEmoteId, int32 AnimId);
    bool bPlayingEmote = false;
    bool bEmoteLoops = false;
    uint32 LastServerEmoteId = 0;
    bool IsPlayingOneShot() const { return bPlayingOneShot && FPlatformTime::Seconds() < OneShotEndTime; }

    /**
     * Get the animation cache for copying to another controller.
     */
    const TMap<int32, TObjectPtr<UAnimSequence>>& GetAnimationCache() const { return AnimationCache; }

    /** Get all animations (ordered by M2 track index) */
    const TArray<TObjectPtr<UAnimSequence>>& GetAllAnimations() const { return AllAnimations; }

    /** Set the animation ID mapping (M2 animID → track index) */
    void SetAnimationIdMap(const TMap<int32, int32>& InMap);
    void SetAuthoredMovementSpeeds(const TArray<FM2AnimSequence>& Sequences);
    /** r709: the player's controller is a copy of the one the model was built with; take its authored
     *  movement speeds and JumpStart variations (the blood elf twirl, the night elf flip) too. */
    void CopyAuthoredDataFrom(const UWowAnimationController* Other);
    /** Import a world actor's clip only when it is first requested. */
    TFunction<UAnimSequence*(int32)> GetAnimationLoader() const { return AnimationLoader; }
    void SetAnimationLoader(TFunction<UAnimSequence*(int32)> InLoader) { AnimationLoader = MoveTemp(InLoader); }


    /** Capture the mesh's normal relative rotation after the character model has been oriented. */
    void CaptureBaseMeshRelativeRotation();
    void NotifyLocalTurn(float YawDeltaDegrees);
    /** How far the mesh is turned away from its normal facing right now (strafe/swim turn), degrees. */
    float GetVisualYawOffsetDegrees() const;

protected:
    TFunction<UAnimSequence*(int32)> AnimationLoader;
    TMap<int32,float> AuthoredMovementSpeeds;
    TSet<int32> MissingAnimationIds;
    UAnimSequence* ResolveAnimation(int32 AnimationId);

    /** Target skeletal mesh component */
    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> MeshComponent;

    /** All animations ordered by M2 track index */
    UPROPERTY()
    TArray<TObjectPtr<UAnimSequence>> AllAnimations;

    /** Cache of animations by M2 animation ID */
    UPROPERTY()
    TMap<int32, TObjectPtr<UAnimSequence>> AnimationCache;

    /** Current animation state */
    UPROPERTY(BlueprintReadOnly)
    EWowAnimState CurrentState = EWowAnimState::Idle;

    /** Currently playing animation */
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UAnimSequence> CurrentAnimation;

    /** Jump sequence tracking */
    // FPlatformTime::Seconds() can already be a large value on machines with
    // long uptime. A float loses enough sub-second precision there to make a
    // normal jump enter Fall several tenths of a second too early.
    double JumpStartTime = 0.0;
    bool bInJumpSequence = false;
    float JumpStartActorZ = 0.0f;
    bool bHasJumpStartActorZ = false;
    double AirborneStartTime = 0.0;
    bool bWasAirborne = false;
    bool bPlayingJumpStartClip = false;
    bool bHoldingJumpApexPose = false;
    bool bPlayingJumpEndClip = false;

    /** One-shot animation lock — prevents state machine from overriding non-looping anims */
    double OneShotEndTime = 0.0;
    bool bPendingSpellCast = false;
    EWowAnimId PendingSpellCastAnim = EWowAnimId::Stand;
    bool bPlayingOneShot = false;
    /** Last movement update had SWIMMING: casts keep the swim pose (3.3.5). */
    bool bSwimPresentation = false;
    /** Movement flags of the last update. Casting while running/jumping keeps the legs moving:
     *  instant buffs must not freeze the locomotion loop and slide the character. */
    uint32 LastMoveFlags = 0;
    bool IsLocomoting() const;

    /** Short local-input lock so strafe intent is not immediately collapsed back to running. */
    double LocalTurnUntil = 0.0;
    EWowAnimState LocalTurnState = EWowAnimState::Idle;
    double ForcedLocomotionUntil = 0.0;
    EWowAnimState ForcedLocomotionState = EWowAnimState::Idle;
    float VisualStrafeYawDegrees = 90.0f;
    float LastVisualStrafeYawOffsetDegrees = 0.0f;
    double LastVisualStrafeInputTime = -1000.0;

    /** Base mesh orientation plus temporary visual offsets for strafe readability. */
    FRotator BaseMeshRelativeRotation = FRotator::ZeroRotator;
    bool bHasBaseMeshRelativeRotation = false;

    /** Smooth visual-only mesh yaw changes so strafe/run transitions do not snap. */
    double LastVisualStateUpdateTime = 0.0;
    bool bHasVisualStateUpdateTime = false;

private:
    /** Hold the physical-apex pose through a normal jump descent and release JumpEnd when complete. */
    void UpdateJumpAnimationPhase(bool bDescending = false);

    /** Determine animation state from movement info */
    EWowAnimState DetermineAnimationState(const struct FWowMovementInfo& MovementInfo, bool bIsInCombat, bool bIsCasting) const;

    /** Determine animation state from local player movement */
    EWowAnimState DetermineLocalPlayerState(class ACharacter* PlayerCharacter) const;

    /** Get the best animation ID for a given state */
    EWowAnimId GetAnimationForState(EWowAnimState State) const;

    /** Switch to a new animation state */
    void SetAnimationState(EWowAnimState NewState);

    /** Match a locomotion clip's cadence to the character's ground speed. */
    void UpdateLocomotionPlayRate(float GroundSpeedCmPerSecond, bool bUseAuthoredSpeed = false);

    /** Apply visual-only mesh offsets for movement states without changing actor movement. */
    void ApplyVisualState(EWowAnimState State);

    /** Check if character is moving based on movement info */
    bool IsMoving(const struct FWowMovementInfo& MovementInfo) const;

    /** Check if character is walking (vs running) based on movement flags and speed */
    bool IsWalking(const struct FWowMovementInfo& MovementInfo) const;
    bool bPlayingPosture = false;
};

/**
 * Single-node animation player with ground foot IK for bipeds (players, humanoid NPCs).
 * It plays exactly like UE's single-node instance (the controller keeps calling
 * PlayAnimation/SetPosition), then lowers the hips and bends each leg so both feet meet
 * the ground on slopes and steps. Legs are found from the skeleton (WoW bones are unnamed);
 * models whose legs are not a clean hip-knee-ankle chain are left untouched.
 */
UCLASS(Transient, NotBlueprintable)
class WOWASSETS_API UWowFootIKInstance : public UAnimSingleNodeInstance
{
    GENERATED_BODY()
public:
    /** Swap the mesh's single-node instance for this one. Call after SetAnimationMode(AnimationSingleNode). */
    static void Install(USkeletalMeshComponent* Mesh);

    // Game thread -> evaluation (copied by the proxy in PreUpdate).
    bool bLegsResolved = false;
    bool bLegsValid = false;
    int32 HipBone[2] = { INDEX_NONE, INDEX_NONE };
    int32 KneeBone[2] = { INDEX_NONE, INDEX_NONE };
    int32 FootBone[2] = { INDEX_NONE, INDEX_NONE };
    float FootOffsetCS[2] = { 0.f, 0.f };
    /** Step-up stance (r280): component-space shift of a raised foot's target forward, so the
     *  thigh lifts and the shin stands upright on the step instead of the knee folding. */
    FVector FootForwardCS[2] = { FVector::ZeroVector, FVector::ZeroVector };
    /** Thigh and shin lengths from the reference pose (component space). */
    /** Bones beside the knee (same parent and pivot, e.g. HumanMale's knee helper bone 14 with
     *  1160 skinned vertices): they must bend with the knee or the shin skin stays with the
     *  thigh and the leg folds (r281). */
    TArray<int32> KneeTwinBones[2];
    /** r422 (Stefan: the raised knee bends oddly and the thigh looks too thin): helper bones that
     *  hang from the knee at the knee's own pivot (HumanMale bone 21: the ring of skin around the
     *  joint, 73 vertices at about half weight). As children they took the whole IK bend, so the
     *  joint's skin went with the shin; like a real knee they take half of the IK change. */
    TArray<int32> KneeChildBones[2];
    float ThighLength = 0.f;
    float ShinLength = 0.f;
    float PelvisOffsetCS = 0.f;
    float IKAlpha = 0.f;
    FVector UpCS = FVector::UpVector;
    // Evaluation -> game thread: animated (pre-IK) ankle positions, component space.
    FVector AnimFootCS[2] = { FVector::ZeroVector, FVector::ZeroVector };
    bool bHaveAnimFeet = false;

    /**
     * Play a one-shot clip on the upper body only (spine, arms, head) on top of whatever the
     * base animation is doing, e.g. the sheath/unsheath gesture while running (r268).
     * The legs and the pelvis chain keep the base clip. Returns false when the skeleton has
     * no resolved legs (no safe upper/lower split) or the clip is missing.
     */
    bool PlayUpperBodyOverlay(UAnimSequence* Sequence);
    bool IsUpperBodyOverlayActive() const { return bOverlayActive; }

    // Game thread -> evaluation (copied by the proxy in PreUpdate).
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> OverlaySequence;
    float OverlayTime = 0.f;
    float OverlayWeight = 0.f;
    bool bOverlayActive = false;
    /** Per mesh bone: 1 = upper body (takes the overlay), 0 = legs / root-pelvis chain. */
    TArray<uint8> UpperBodyMask;

    /** Head look (r292, Stefan): the head (and a little of the neck) turns towards this world
     *  point - the target, or where the camera looks. Set by the game thread every frame;
     *  turned off by clearing bLookEnabled. Points behind the character are ignored. */
    bool bLookEnabled = false;
    FVector LookAtWorld = FVector::ZeroVector;
    /** r296: first person / VR - turn the head bone only. It turns about its own pivot, so the
     *  eye (head socket) stays put and the view never feeds back into the look direction. */
    bool bLookHeadOnly = false;
    // Game thread -> evaluation.
    int32 LookHeadBone = INDEX_NONE;
    int32 LookNeckBone = INDEX_NONE;
    FQuat LookDeltaCS = FQuat::Identity;
    bool bLookActive = false;
    /** r372: wanted look direction (component space) and its weight; the evaluation turns the
     *  ANIMATED head onto it, so stances that already twist the head no longer over-turn it. */
    FVector LookDirCS = FVector(0.0, 1.0, 0.0);
    FVector LookHeadForwardLocal = FVector(0.0, 1.0, 0.0);
    FQuat LookHeadRestCS = FQuat::Identity;
    FVector LookBodyForwardCS = FVector(0.0, 1.0, 0.0);
    float LookWeight = 0.f;

    /** r640 (Stefan: "smooth animation blending ... test it on male human first"): when the clip
     *  changes, the old clip keeps playing underneath and fades out over Duration, so walk, run,
     *  strafe, idle and stops blend instead of snapping. */
    void BeginCrossfade(UAnimSequence* From, float FromTime, bool bFromLooping, float Duration);
    /** r709: a posture clip (sit down, ...) starts: drop the foot IK at once instead of blending it out. */
    void SnapFootIKOff();
    // Game thread -> evaluation (copied by the proxy in PreUpdate).
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> FadeFromSequence;
    float FadeFromTime = 0.f;
    float FadeWeight = 0.f;
    float FadeElapsed = 0.f;
    float FadeDuration = 0.f;
    bool bFadeFromLooping = true;
    bool bFadeActive = false;

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
    void ResolveLegs();
    void BuildUpperBodyMask();
    void ResolveHead();
    void UpdateHeadLook(float DeltaSeconds);
    bool bHeadResolved = false;
    float LookYaw = 0.f;
    float LookPitch = 0.f;
    float FootOffsetWorld[2] = { 0.f, 0.f };
    float FootForwardWorld[2] = { 0.f, 0.f };
    FVector LastOwnerLocation = FVector::ZeroVector;
    float PelvisWorld = 0.f;
};
