#include "WowAnimationController.h"
#include "GameFramework/Pawn.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimNodeBase.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "CollisionQueryParams.h"
#include "HAL/IConsoleManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WowNetwork/Public/WowEntity.h"
#include "WowData/Public/Formats/M2Types.h"

// r692 (Stefan: "I jumped around and she never did the twirl jump.. Not once", and the night elves never flip):
// the model loader imports one clip per animation id, the variation the client plays most often, so the rare
// JumpStart takes (BloodElfFemale 37-0, 25 %: the twirl; NightElfMale 37-1, 25 % and NightElfFemale 37-1, 50 %:
// the flip) never played. Each JumpStart now rolls a variation by the model's own frequencies, like the client;
// a variation other than the main one is loaded as id | (sub + 1) << 16 (WowCharacterBuilder r692).
struct FWowAnimVariations
{
    TMap<int32, TArray<TPair<int32, int32>>> ById; // id -> (sub id, frequency of 0x7FFF)
    TMap<int32, int32> Main;                       // id -> the sub id the loader imports for the plain id
    bool bLastJumpWasVariation = false;
};
static TMap<TWeakObjectPtr<const UWowAnimationController>, FWowAnimVariations> GWowAnimVariations;

// r694 (Stefan, a blood elf sitting: "they sit in the ground"): foot IK kept planting the feet of the sit pose
// on the ground in front and dropped the pelvis by the lowest foot (up to 50 cm on a slope). Meshes playing a
// posture clip (sit down, sit, stand up, chair, sleep, kneel) blend their foot IK out.
static TSet<TWeakObjectPtr<const USkeletalMeshComponent>> GWowPostureMeshes;

static int32 WowRollVariationKey(const UWowAnimationController* Controller, int32 Id)
{
    FWowAnimVariations* Table = GWowAnimVariations.Find(Controller);
    const TArray<TPair<int32, int32>>* Subs = Table ? Table->ById.Find(Id) : nullptr;
    if (!Subs || Subs->Num() < 2) return Id;
    int32 Total = 0;
    for (const TPair<int32, int32>& Sub : *Subs) Total += FMath::Max(0, Sub.Value);
    if (Total <= 0) return Id;
    int32 Roll = FMath::RandRange(0, Total - 1);
    int32 Picked = (*Subs)[0].Key;
    for (const TPair<int32, int32>& Sub : *Subs)
    {
        Roll -= FMath::Max(0, Sub.Value);
        if (Roll < 0) { Picked = Sub.Key; break; }
    }
    const int32* Main = Table->Main.Find(Id);
    return Main && *Main == Picked ? Id : (Id | ((Picked + 1) << 16));
}


DEFINE_LOG_CATEGORY_STATIC(LogWowAnim, Log, All);

namespace
{
	constexpr float BackwardDiagonalVisualYawDegrees = 45.0f;

	EWowAnimId ResolveAuthoredSpellAnimation(uint32 AuthoredAnimationId, EWowAnimId Fallback)
	{
		return AuthoredAnimationId > 0 && AuthoredAnimationId <= MAX_uint8
			? static_cast<EWowAnimId>(static_cast<uint8>(AuthoredAnimationId))
			: Fallback;
	}
}

EWowAnimId FWowSpellAnimationPolicy::ResolveReadyAnimation(
	uint32 AuthoredAnimationId, bool bOmniDirectional)
{
	return ResolveAuthoredSpellAnimation(AuthoredAnimationId,
		bOmniDirectional ? EWowAnimId::ReadySpellOmni : EWowAnimId::ReadySpellDirected);
}

EWowAnimId FWowSpellAnimationPolicy::ResolveReleaseAnimation(
	uint32 AuthoredAnimationId, bool bOmniDirectional)
{
	return ResolveAuthoredSpellAnimation(AuthoredAnimationId,
		bOmniDirectional ? EWowAnimId::SpellCastOmni : EWowAnimId::SpellCastDirected);
}

EWowAnimId FWowSpellAnimationPolicy::ResolveChannelAnimation(
	uint32 AuthoredAnimationId, bool bOmniDirectional)
{
	return ResolveAuthoredSpellAnimation(AuthoredAnimationId,
		bOmniDirectional ? EWowAnimId::ChannelCastOmni : EWowAnimId::ChannelCast);
}

EWowAnimState FWowMovementAnimationPolicy::ResolveAirborneState(
	bool bPlayerInitiatedJump,
	bool bRising,
	double AirborneSeconds,
	EWowAnimState PreviousState)
{
	if (bPlayerInitiatedJump || bRising)
	{
		return EWowAnimState::Jumping;
	}

	// Walking over a curb or a shallow terrain seam briefly enters UE's
	// falling mode. Preserve locomotion for that short grace period.
	return AirborneSeconds < ShortFallAnimationDelaySeconds
		? PreviousState
		: EWowAnimState::Falling;
}

bool FWowMovementAnimationPolicy::ShouldFreezeJumpStartAtApex(
	bool bPlayingJumpStart,
	bool bPlayerInitiatedJump,
	bool bDescending,
	bool bAlreadyHoldingApex)
{
	return bPlayingJumpStart && bPlayerInitiatedJump && bDescending && !bAlreadyHoldingApex;
}

bool FWowMovementAnimationPolicy::ShouldPlayStationaryLanding(
	bool bHasMovementIntent,
	float HorizontalSpeedCmPerSecond,
	bool bHasJumpEndAnimation)
{
	return bHasJumpEndAnimation &&
		!bHasMovementIntent &&
		HorizontalSpeedCmPerSecond <= MovingLandingSpeedThresholdCm;
}

EWowAnimState FWowMovementAnimationPolicy::ResolveSwimmingState(float SpeedCmPerSecond, uint32 Flags)
{
    if (SpeedCmPerSecond <= SwimmingIdleSpeedThresholdCm) return EWowAnimState::SwimmingIdle;
    const bool Left=(Flags & WowMovementFlags::STRAFE_LEFT)!=0, Right=(Flags & WowMovementFlags::STRAFE_RIGHT)!=0;
    if (Flags & WowMovementFlags::BACKWARD)
        return Left!=Right ? (Left?EWowAnimState::SwimmingBackwardLeft:EWowAnimState::SwimmingBackwardRight) : EWowAnimState::SwimmingBackward;
    if (Left!=Right)
    {
        if (Flags & WowMovementFlags::FORWARD)
            return Left?EWowAnimState::SwimmingForwardLeft:EWowAnimState::SwimmingForwardRight;
        return Left?EWowAnimState::SwimmingLeft:EWowAnimState::SwimmingRight;
    }
    return EWowAnimState::Swimming;
}

UWowAnimationController::UWowAnimationController()
{
}

void UWowAnimationController::Initialize(USkeletalMeshComponent* InMeshComponent, const TArray<UAnimSequence*>& Animations)
{
    MeshComponent = InMeshComponent;
    AnimationCache.Empty();

    if (!MeshComponent)
    {
        UE_LOG(LogWowAnim, Error, TEXT("WowAnimationController: Cannot initialize without valid skeletal mesh component"));
        return;
    }

    // Store all animations by track index
    AllAnimations.Empty();
    for (UAnimSequence* Anim : Animations)
    {
        AllAnimations.Add(Anim);
    }

    UE_LOG(LogWowAnim, Log, TEXT("WowAnimationController initialized with %d animations"), AllAnimations.Num());
    CaptureBaseMeshRelativeRotation();

    // Play idle animation immediately - try mapped idle first, then fallback
    if (MeshComponent)
    {
        UAnimSequence* IdleAnim = nullptr;

        // Try to find mapped idle animation (ID 0)
        if (TObjectPtr<UAnimSequence>* Found = AnimationCache.Find(0))
        {
            IdleAnim = *Found;
            UE_LOG(LogWowAnim, Log, TEXT("WowAnimationController::Initialize: Found mapped Stand animation (ID 0): %s"),
                IdleAnim ? *IdleAnim->GetName() : TEXT("NULL"));
        }

        // Fallback to first animation if no mapped idle found
        if (!IdleAnim && AllAnimations.Num() > 0 && AllAnimations[0])
        {
            IdleAnim = AllAnimations[0];
            UE_LOG(LogWowAnim, Warning, TEXT("WowAnimationController::Initialize: No Stand animation (ID 0) found, falling back to AllAnimations[0]: %s"),
                IdleAnim ? *IdleAnim->GetName() : TEXT("NULL"));
        }

        if (IdleAnim)
        {
            MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
            UWowFootIKInstance::Install(MeshComponent);
            MeshComponent->PlayAnimation(IdleAnim, true);
            UE_LOG(LogWowAnim, Log, TEXT("WowAnimationController::Initialize: Playing initial animation: %s"), *IdleAnim->GetName());
        }
        else
        {
            UE_LOG(LogWowAnim, Error, TEXT("WowAnimationController::Initialize: No animations available to play"));
        }
    }

    // Start with idle animation
    SetAnimationState(EWowAnimState::Idle);
}

float UWowAnimationController::GetVisualYawOffsetDegrees() const
{
    if (!MeshComponent || !bHasBaseMeshRelativeRotation) return 0.f;
    return FRotator::NormalizeAxis(MeshComponent->GetRelativeRotation().Yaw - BaseMeshRelativeRotation.Yaw);
}

void UWowAnimationController::CaptureBaseMeshRelativeRotation()
{
    if (!MeshComponent)
    {
        return;
    }

    BaseMeshRelativeRotation = MeshComponent->GetRelativeRotation();
    bHasBaseMeshRelativeRotation = true;
}

void UWowAnimationController::NotifyLocalJumpStarted(float ActorZ)
{
    const double CurrentTime = FPlatformTime::Seconds();
    bInJumpSequence = true;
    bWasAirborne = true;
    JumpStartTime = CurrentTime;
    AirborneStartTime = CurrentTime;
    JumpStartActorZ = ActorZ;
    bHasJumpStartActorZ = true;
	// An accepted jump owns locomotion immediately, even if another short
	// non-combat clip happened to be ending on this frame.
	bPlayingOneShot = false;
	bPlayingJumpStartClip = false;
	bHoldingJumpApexPose = false;
	bPlayingJumpEndClip = false;
    SetAnimationState(EWowAnimState::Jumping);
}

void UWowAnimationController::NotifyLocalLanded(bool bHasMovementIntent)
{
    bInJumpSequence = false;
    bWasAirborne = false;
    bHasJumpStartActorZ = false;
	bPlayingJumpStartClip = false;
	bHoldingJumpApexPose = false;
	bPlayingOneShot = false;

	// WotLK supplies a distinct landing clip, but it plants the feet. Locking that
	// one-shot while movement input is already carrying the actor forward makes
	// the character visibly slide after landing. Use it only for a stationary
	// landing; moving landings return to locomotion on the next state update.
	const AActor* Owner = MeshComponent ? MeshComponent->GetOwner() : nullptr;
	const float HorizontalSpeed = Owner ? Owner->GetVelocity().Size2D() : 0.0f;
	const bool bHasJumpEndAnimation =
		ResolveAnimation(static_cast<int32>(EWowAnimId::JumpEnd)) != nullptr;
	bPlayingJumpEndClip = FWowMovementAnimationPolicy::ShouldPlayStationaryLanding(
		bHasMovementIntent, HorizontalSpeed, bHasJumpEndAnimation) &&
		PlayAnimationById(EWowAnimId::JumpEnd, false);
}

void UWowAnimationController::NotifyLocalMovementIntent()
{
    StopEmote();
	if (!bPlayingJumpEndClip)
	{
		return;
	}

	// JumpEnd plants both feet. It may start on a stationary landing, but any
	// newly requested movement must release it before the capsule travels.
	bPlayingJumpEndClip = false;
	bPlayingOneShot = false;
	OneShotEndTime = 0.0;
}

void UWowAnimationController::UpdateJumpAnimationPhase(bool bDescending)
{
	if (FWowMovementAnimationPolicy::ShouldFreezeJumpStartAtApex(
		bPlayingJumpStartClip, bInJumpSequence, bDescending, bHoldingJumpApexPose))
	{
		// AnimID 38 is named Jump, but its authored pose reads as an uncontrolled
		// fall on several races (especially Dwarf). Freeze the good take-off clip
		// at the real physics apex and hold that controlled airborne silhouette
		// through descent. Landed() remains the only path into JumpEnd; genuine
		// ledge drops still select Fall (AnimID 40).
		if (UAnimSingleNodeInstance* SingleNode = MeshComponent
			? MeshComponent->GetSingleNodeInstance() : nullptr)
		{
			SingleNode->SetPlaying(false);
		}
		bHoldingJumpApexPose = true;
		bPlayingOneShot = false;
		OneShotEndTime = 0.0;
		CurrentState = EWowAnimState::Jumping;
	}
	else if (bPlayingJumpStartClip && !bInJumpSequence)
	{
		bPlayingJumpStartClip = false;
		bHoldingJumpApexPose = false;
		bPlayingOneShot = false;
	}

	if (bPlayingJumpEndClip && !IsPlayingOneShot())
	{
		bPlayingJumpEndClip = false;
		bPlayingOneShot = false;
	}
}

void UWowAnimationController::UpdateAnimationState(const FWowMovementInfo& MovementInfo, bool bIsInCombat, bool bIsCasting, float RenderedSpeedCm)
{
    bSwimPresentation = (MovementInfo.MoveFlags & WowMovementFlags::SWIMMING) != 0;
    LastMoveFlags = MovementInfo.MoveFlags;
    // A cast pose/one-shot that started while standing yields as soon as the unit moves.
    if (IsLocomoting() && (CurrentState == EWowAnimState::Casting || CurrentState == EWowAnimState::Channeling))
    {
        bPlayingOneShot = false;
        OneShotEndTime = 0.0;
        CurrentAnimation = nullptr;
        CurrentState = EWowAnimState::Idle;
    }
    if (bPlayingEmote)
    {
        const uint32 CancelFlags = WowMovementFlags::FORWARD | WowMovementFlags::BACKWARD |
            WowMovementFlags::STRAFE_LEFT | WowMovementFlags::STRAFE_RIGHT |
            WowMovementFlags::FALLING | WowMovementFlags::JUMPING | WowMovementFlags::SWIMMING;
        if (!(MovementInfo.MoveFlags & CancelFlags) && !bIsInCombat && !bIsCasting &&
            (bEmoteLoops || IsPlayingOneShot())) return;
        StopEmote();
    }
    if (bPlayingPosture)
    {
        bPlayingPosture = false;
        CurrentAnimation = nullptr;
    }

    if (!IsInitialized())
    {
        return;
    }

	const bool bDescending = (MovementInfo.MoveFlags & WowMovementFlags::FALLING) != 0;
	UpdateJumpAnimationPhase(bDescending);
	const uint32 GroundMoveFlags = WowMovementFlags::FORWARD |
		WowMovementFlags::BACKWARD |
		WowMovementFlags::STRAFE_LEFT |
		WowMovementFlags::STRAFE_RIGHT;
	if (bPlayingJumpEndClip && (MovementInfo.MoveFlags & GroundMoveFlags) != 0)
	{
		NotifyLocalMovementIntent();
	}

    // Don't override one-shot animations (attack, wound) until they finish.
    // Once one expires, invalidate its final pose so the active locomotion or
    // idle state is forced to restart even when the state enum did not change.
    if (IsPlayingOneShot())
    {
        return;
    }
    if (bPlayingOneShot)
    {
        CurrentAnimation = nullptr;
    }
    bPlayingOneShot = false;

    EWowAnimState NewState = DetermineAnimationState(MovementInfo, bIsInCombat, bIsCasting);
    const double CurrentTime = FPlatformTime::Seconds();
    const bool bInputStrafeLeft = (MovementInfo.MoveFlags & WowMovementFlags::STRAFE_LEFT) != 0;
    const bool bInputStrafeRight = (MovementInfo.MoveFlags & WowMovementFlags::STRAFE_RIGHT) != 0;
    const bool bInputForward = (MovementInfo.MoveFlags & WowMovementFlags::FORWARD) != 0;
    const bool bInputBackward = (MovementInfo.MoveFlags & WowMovementFlags::BACKWARD) != 0;
    if (bInputBackward && bInputStrafeLeft != bInputStrafeRight)
    {
		// Walkbackwards is the only authored ground backpedal loop. Turn only the
		// rendered mesh into the diagonal so its backward foot travel follows the
		// actual S+A/S+D vector; actor/controller facing remains unchanged.
		LastVisualStrafeYawOffsetDegrees = bInputStrafeLeft
			? BackwardDiagonalVisualYawDegrees
			: -BackwardDiagonalVisualYawDegrees;
		LastVisualStrafeInputTime = CurrentTime;
	}
    else if (!bInputBackward && bInputStrafeLeft != bInputStrafeRight)
    {
        const float RequestedStrafeYaw = bInputForward ? 45.0f : 90.0f;
        VisualStrafeYawDegrees = RequestedStrafeYaw;
        LastVisualStrafeYawOffsetDegrees = bInputStrafeLeft ? -RequestedStrafeYaw : RequestedStrafeYaw;
        LastVisualStrafeInputTime = CurrentTime;
    }

    const bool bJumpingFlag = (MovementInfo.MoveFlags & WowMovementFlags::JUMPING) != 0;
    const bool bFallingFlag = (MovementInfo.MoveFlags & WowMovementFlags::FALLING) != 0;
    const bool bAirborne = bJumpingFlag || bFallingFlag;

    if (bAirborne && !bWasAirborne)
    {
        bWasAirborne = true;
        AirborneStartTime = CurrentTime;
    }

    if (bJumpingFlag)
    {
        if (!bInJumpSequence)
        {
            bInJumpSequence = true;
            JumpStartTime = CurrentTime;
            if (const AActor* Owner = MeshComponent ? MeshComponent->GetOwner() : nullptr)
            {
                JumpStartActorZ = Owner->GetActorLocation().Z;
                bHasJumpStartActorZ = true;
            }
        }
        NewState = EWowAnimState::Jumping;
    }
    else if (bFallingFlag)
    {
        // A Space-initiated jump owns both ascent and descent. A genuine ledge
        // drop becomes Fall only after the short curb/terrain grace period.
        NewState = FWowMovementAnimationPolicy::ResolveAirborneState(
            bInJumpSequence, false, CurrentTime - AirborneStartTime, CurrentState);
    }
    else
    {
        bWasAirborne = false;
        bInJumpSequence = false;
        bHasJumpStartActorZ = false;
    }

    const bool bForcedStrafeActive = FPlatformTime::Seconds() < ForcedLocomotionUntil &&
        (ForcedLocomotionState == EWowAnimState::StrafingLeft ||
         ForcedLocomotionState == EWowAnimState::StrafingRight);
    if (bForcedStrafeActive &&
        !(MovementInfo.MoveFlags & (WowMovementFlags::BACKWARD |
                                    WowMovementFlags::JUMPING |
                                    WowMovementFlags::FALLING |
                                    WowMovementFlags::SWIMMING)))
    {
        NewState = ForcedLocomotionState;
    }

    if (NewState != CurrentState || !CurrentAnimation)
    {
        SetAnimationState(NewState);
    }
    else
    {
        ApplyVisualState(NewState);
    }

    if (NewState == EWowAnimState::Walking ||
        NewState == EWowAnimState::WalkingLeft || NewState == EWowAnimState::WalkingRight ||
		NewState == EWowAnimState::Backpedaling ||
		NewState == EWowAnimState::BackpedalingLeft ||
		NewState == EWowAnimState::BackpedalingRight)
    {
        UpdateLocomotionPlayRate(RenderedSpeedCm>=0.f ? RenderedSpeedCm : MovementInfo.WalkSpeed*100.f, RenderedSpeedCm>=0.f);
    }
    else if (NewState == EWowAnimState::Running ||
        NewState == EWowAnimState::StrafingLeft ||
        NewState == EWowAnimState::StrafingRight)
    {
        UpdateLocomotionPlayRate(RenderedSpeedCm>=0.f ? RenderedSpeedCm : MovementInfo.RunSpeed*100.f, RenderedSpeedCm>=0.f);
    }
}

void UWowAnimationController::ForceLocalLocomotionState(
    EWowAnimState NewState,
    float GroundSpeedCmPerSecond,
    float VisualYawDegrees)
{
    StopEmote();
    if (!IsInitialized())
    {
        return;
    }

    if (IsPlayingOneShot())
    {
        return;
    }

    bPlayingOneShot = false;
    if (NewState == EWowAnimState::StrafingLeft || NewState == EWowAnimState::StrafingRight)
    {
        ForcedLocomotionState = NewState;
        // ForceLocalLocomotionState is called every tick while the strafe input
        // is actually held. Do not latch it after release, or W+A/D -> W keeps
        // a stale strafe pose for a few frames.
        ForcedLocomotionUntil = 0.0f;
        VisualStrafeYawDegrees = FMath::Clamp(FMath::Abs(VisualYawDegrees), 1.0f, 90.0f);
    }
    SetAnimationState(NewState);

    if (GroundSpeedCmPerSecond > 0.0f)
    {
        UpdateLocomotionPlayRate(GroundSpeedCmPerSecond);
    }
}

void UWowAnimationController::UpdateLocalPlayerState(ACharacter* PlayerCharacter, bool bIsInCombat)
{
    // This path runs when the local player has no movement flags (standing still). The
    // flags from the last step used to linger here, so casts and sheathing thought the
    // player was still running and skipped their animations ("cast 51 skipped ... 0x1").
    LastMoveFlags = 0;
    bSwimPresentation = false; // swimming always carries the SWIMMING flag, so not here
    if (bPlayingEmote)
    {
        const auto* Move = PlayerCharacter ? PlayerCharacter->GetCharacterMovement() : nullptr;
        if (!bIsInCombat && Move && !Move->IsFalling() && !Move->IsSwimming() && Move->Velocity.SizeSquared() < 1.0f &&
            Move->GetCurrentAcceleration().IsNearlyZero() && (bEmoteLoops || IsPlayingOneShot())) return;
        StopEmote();
    }
    if (bPlayingPosture)
    {
        bPlayingPosture = false;
        CurrentAnimation = nullptr;
    }

    if (!IsInitialized() || !PlayerCharacter)
    {
        return;
    }

	UCharacterMovementComponent* Movement = PlayerCharacter->GetCharacterMovement();
	const bool bDescending = Movement && Movement->IsFalling() && Movement->Velocity.Z <= 0.0f;
	UpdateJumpAnimationPhase(bDescending);
	if (bPlayingJumpEndClip)
	{
		const UCharacterMovementComponent* LocalMovement = PlayerCharacter->GetCharacterMovement();
		const bool bMovementRequested =
			!PlayerCharacter->GetPendingMovementInputVector().IsNearlyZero() ||
			!PlayerCharacter->GetLastMovementInputVector().IsNearlyZero() ||
			(LocalMovement && LocalMovement->GetCurrentAcceleration().SizeSquared2D() > 1.0f);
		if (bMovementRequested)
		{
			NotifyLocalMovementIntent();
		}
	}

    // Don't override one-shot animations (attack, wound) until they finish.
    if (IsPlayingOneShot())
    {
        return;
    }
    if (bPlayingOneShot)
    {
        CurrentAnimation = nullptr;
    }
    bPlayingOneShot = false;

    EWowAnimState NewState = DetermineLocalPlayerState(PlayerCharacter);

    // Handle jump sequence for local player.  This path is used when there are
    // no explicit movement flags, so mirror the same normal-jump/fall timing as
    // UpdateAnimationState rather than classifying all downward motion as Fall.
    if (Movement)
    {
        const double CurrentTime = FPlatformTime::Seconds();
        if (Movement->IsFalling())
        {
            if (!bWasAirborne)
            {
                bWasAirborne = true;
                AirborneStartTime = CurrentTime;
            }

            if (Movement->Velocity.Z > 0.0f)
            {
                NewState = EWowAnimState::Jumping;
                if (!bInJumpSequence)
                {
                    bInJumpSequence = true;
                    JumpStartTime = CurrentTime;
                    JumpStartActorZ = PlayerCharacter->GetActorLocation().Z;
                    bHasJumpStartActorZ = true;
                }
            }
            else
            {
                // r511 (Stefan: running down a hill on a night elf woman she leaned back in the fall pose):
                // a slope steeper than the 45 degree walkable limit puts UE in falling mode while the feet
                // are still on the hillside, and after the short grace the Fall clip took over. While the
                // ground is within 1.2 m below the feet and this is not a jump, keep running.
                if (!bInJumpSequence && PlayerCharacter->GetWorld())
                {
                    const float HalfHeight = PlayerCharacter->GetSimpleCollisionHalfHeight();
                    const FVector From = PlayerCharacter->GetActorLocation();
                    FHitResult Ground;
                    FCollisionQueryParams GroundParams(TEXT("WowDownhillGround"), false, PlayerCharacter);
                    if (PlayerCharacter->GetWorld()->LineTraceSingleByChannel(Ground, From,
                            From - FVector(0.0, 0.0, HalfHeight + 120.0f), ECC_Visibility, GroundParams)
                        && Ground.bBlockingHit && Ground.ImpactNormal.Z > 0.2f)
                    {
                        AirborneStartTime = CurrentTime;
                    }
                }
				NewState = FWowMovementAnimationPolicy::ResolveAirborneState(
					bInJumpSequence,
					Movement->Velocity.Z > 0.0f,
					CurrentTime - AirborneStartTime,
					CurrentState);
            }
        }
        else
        {
            bWasAirborne = false;
            bInJumpSequence = false;
            bHasJumpStartActorZ = false;
            NewState = DetermineLocalPlayerState(PlayerCharacter);
        }

        const bool bForcedStrafeActive = FPlatformTime::Seconds() < ForcedLocomotionUntil &&
            (ForcedLocomotionState == EWowAnimState::StrafingLeft ||
             ForcedLocomotionState == EWowAnimState::StrafingRight);
        if (bForcedStrafeActive && !Movement->IsFalling() && !Movement->IsSwimming() &&
            Movement->Velocity.Size2D() > 10.0f)
        {
            NewState = ForcedLocomotionState;
        }
    }

    // Keep movement, falling, swimming and one-shot attacks authoritative.
    if (bIsInCombat && NewState == EWowAnimState::Idle) NewState = EWowAnimState::Combat;
    if (NewState != CurrentState || !CurrentAnimation)
    {
        SetAnimationState(NewState);
    }
    else
    {
        ApplyVisualState(NewState);
    }

    UpdateLocomotionPlayRate(Movement->Velocity.Size2D());
}

// NOTE: PlayAnimationById defined at end of file (needs AnimationCache + AllAnimations)

EWowAnimState UWowAnimationController::DetermineAnimationState(const FWowMovementInfo& MovementInfo, bool bIsInCombat, bool bIsCasting) const
{
    // Priority: casting/channeling > movement > combat idle > idle.
    // Combat stance should not override locomotion; otherwise strafing and
    // backpedaling collapse into a ready pose while the character slides.

    // Swimming wins over casting: a swimming caster keeps the swim pose (the
    // spell visual still plays on the hands), never the standing cast pose.
    if (MovementInfo.MoveFlags & WowMovementFlags::SWIMMING)
    {
        return FWowMovementAnimationPolicy::ResolveSwimmingState(
            IsMoving(MovementInfo) ? 100.0f : 0.0f, MovementInfo.MoveFlags);
    }

    // Instant casts (buffs) while moving or airborne keep the locomotion/jump loop.
    if (bIsCasting && !IsMoving(MovementInfo) &&
        !(MovementInfo.MoveFlags & (WowMovementFlags::JUMPING | WowMovementFlags::FALLING)))
    {
        return EWowAnimState::Casting;
    }

    // Check for jumping/falling
    if (MovementInfo.MoveFlags & WowMovementFlags::JUMPING)
    {
        return EWowAnimState::Jumping;
    }

    if (MovementInfo.MoveFlags & WowMovementFlags::FALLING)
    {
        return EWowAnimState::Falling;
    }

    // Check for movement
    if (IsMoving(MovementInfo))
    {
        if (MovementInfo.MoveFlags & WowMovementFlags::BACKWARD)
        {
			const bool bStrafeLeft = (MovementInfo.MoveFlags & WowMovementFlags::STRAFE_LEFT) != 0;
			const bool bStrafeRight = (MovementInfo.MoveFlags & WowMovementFlags::STRAFE_RIGHT) != 0;
			if (bStrafeLeft != bStrafeRight)
			{
				return bStrafeLeft
					? EWowAnimState::BackpedalingLeft
					: EWowAnimState::BackpedalingRight;
			}
            return EWowAnimState::Backpedaling;
        }
        else if (MovementInfo.MoveFlags & WowMovementFlags::STRAFE_LEFT)
        {
            return IsWalking(MovementInfo) ? EWowAnimState::WalkingLeft : EWowAnimState::StrafingLeft;
        }
        else if (MovementInfo.MoveFlags & WowMovementFlags::STRAFE_RIGHT)
        {
            return IsWalking(MovementInfo) ? EWowAnimState::WalkingRight : EWowAnimState::StrafingRight;
        }
        else if (IsWalking(MovementInfo))
        {
            return EWowAnimState::Walking;
        }
        else
        {
            return EWowAnimState::Running;
        }
    }

    if (bIsInCombat)
    {
        return EWowAnimState::Combat;
    }

    // Default idle state
    return EWowAnimState::Idle;
}

void UWowAnimationController::NotifyLocalTurn(float YawDeltaDegrees)
{
    if (!FMath::IsFinite(YawDeltaDegrees) || FMath::Abs(YawDeltaDegrees) < .01f) return;
    LocalTurnState = YawDeltaDegrees > 0 ? EWowAnimState::TurningRight : EWowAnimState::TurningLeft;
    LocalTurnUntil = FPlatformTime::Seconds() + .12;
}

EWowAnimState UWowAnimationController::DetermineLocalPlayerState(ACharacter* PlayerCharacter) const
{
    if (!PlayerCharacter)
    {
        return EWowAnimState::Idle;
    }

    UCharacterMovementComponent* Movement = PlayerCharacter->GetCharacterMovement();
    if (!Movement)
    {
        return EWowAnimState::Idle;
    }

    // Check if swimming
    if (Movement->IsSwimming())
    {
        FVector Velocity = Movement->Velocity;
		return FWowMovementAnimationPolicy::ResolveSwimmingState(Velocity.Size());
    }

    // Check if falling
    if (Movement->IsFalling())
    {
        if (Movement->Velocity.Z > 0)
        {
            return EWowAnimState::Jumping;
        }
        else
        {
            return EWowAnimState::Falling;
        }
    }

    // Check movement
    FVector Velocity = Movement->Velocity;
    float Speed = Velocity.Size2D();

    if (Speed > 10.0f) // Moving threshold
    {
        const FVector Forward = PlayerCharacter->GetActorForwardVector().GetSafeNormal2D();
        const FVector Right = PlayerCharacter->GetActorRightVector().GetSafeNormal2D();
        FVector DirectionSource = PlayerCharacter->GetLastMovementInputVector();
        if (DirectionSource.SizeSquared2D() <= KINDA_SMALL_NUMBER)
        {
            DirectionSource = Velocity;
        }
        const FVector MoveDirection = DirectionSource.GetSafeNormal2D();
        if (!Forward.IsNearlyZero() && !MoveDirection.IsNearlyZero())
        {
            const float ForwardDot = FVector::DotProduct(Forward, MoveDirection);
            const float RightDot = !Right.IsNearlyZero()
                ? FVector::DotProduct(Right, MoveDirection)
                : 0.0f;

            if (ForwardDot < -0.3f && FMath::Abs(ForwardDot) >= FMath::Abs(RightDot))
            {
				if (RightDot < -0.25f)
				{
					return EWowAnimState::BackpedalingLeft;
				}
				if (RightDot > 0.25f)
				{
					return EWowAnimState::BackpedalingRight;
				}
                return EWowAnimState::Backpedaling;
            }
            if (FMath::Abs(RightDot) > 0.55f && FMath::Abs(RightDot) > FMath::Abs(ForwardDot))
            {
                return RightDot > 0.0f
                    ? EWowAnimState::StrafingRight
                    : EWowAnimState::StrafingLeft;
            }
        }

        // WoW's normal walk is 2.5 units/s (250 cm/s) and run is 7.0
        // units/s.  The old test compared GetMaxSpeed() with MaxWalkSpeed,
        // which are the same value for MOVE_Walking and classified every run
        // as a walk.
        // While picking up speed (and slowing down) the first steps are walked, so a
        // standing character does not burst straight into the run loop.
        if (Movement->MaxWalkSpeed <= 350.0f || (Movement->IsMovingOnGround() && Velocity.Size2D() < 430.0f))
        {
            return EWowAnimState::Walking;
        }
        else
        {
            return EWowAnimState::Running;
        }
    }

    return Movement->IsMovingOnGround() && FPlatformTime::Seconds() < LocalTurnUntil
        ? LocalTurnState : EWowAnimState::Idle;
}

EWowAnimId UWowAnimationController::GetAnimationForState(EWowAnimState State) const
{
    switch (State)
    {
        case EWowAnimState::Idle:
            return EWowAnimId::Stand;
        case EWowAnimState::Walking:
        case EWowAnimState::WalkingLeft:
        case EWowAnimState::WalkingRight:
            return EWowAnimId::Walk;
        case EWowAnimState::Backpedaling:
		case EWowAnimState::BackpedalingLeft:
		case EWowAnimState::BackpedalingRight:
            return EWowAnimId::Walkbackwards;
        case EWowAnimState::TurningLeft:
        case EWowAnimState::StrafingLeft:
            return EWowAnimId::ShuffleLeft;
        case EWowAnimState::TurningRight:
        case EWowAnimState::StrafingRight:
            return EWowAnimId::ShuffleRight;
        case EWowAnimState::Running:
            return EWowAnimId::Run;
        case EWowAnimState::SwimmingLeft: return EWowAnimId::SwimLeft;
        case EWowAnimState::SwimmingRight: return EWowAnimId::SwimRight;
        case EWowAnimState::SwimmingBackward:
        case EWowAnimState::SwimmingBackwardLeft:
        case EWowAnimState::SwimmingBackwardRight: return EWowAnimId::SwimBackward;
        case EWowAnimState::SwimmingForwardLeft:
        case EWowAnimState::SwimmingForwardRight:
        case EWowAnimState::Swimming:
            return EWowAnimId::Swim;
        case EWowAnimState::SwimmingIdle:
            return EWowAnimId::SwimIdle;
        case EWowAnimState::Jumping:
            return EWowAnimId::Jump;
        case EWowAnimState::Falling:
            return EWowAnimId::Fall;
        case EWowAnimState::Combat:
            return GetMeleeReadyAnimation();
        case EWowAnimState::Casting:
            return EWowAnimId::SpellCastDirected;
        case EWowAnimState::Channeling:
            return EWowAnimId::ChannelCast;
        case EWowAnimState::Dead:
            return EWowAnimId::Death;
        default:
            return EWowAnimId::Stand;
    }
}

void UWowAnimationController::SetAnimationState(EWowAnimState NewState)
{
    if (NewState == CurrentState && CurrentAnimation)
    {
        ApplyVisualState(NewState);
        return;
    }

    // State machine transitions always clear one-shot lock — one-shots are only
    // for explicit combat animations (attack/wound) triggered outside the state machine
    bPlayingOneShot = false;

    const EWowAnimState PreviousState = CurrentState;
    EWowAnimId AnimId = GetAnimationForState(NewState);
	const bool bUseJumpStart = NewState == EWowAnimState::Jumping &&
		bInJumpSequence &&
		FPlatformTime::Seconds() - JumpStartTime <= 0.15 &&
		ResolveAnimation(static_cast<int32>(EWowAnimId::JumpStart)) != nullptr;
	if (bUseJumpStart)
	{
		AnimId = EWowAnimId::JumpStart;
	}
    if (NewState == EWowAnimState::Falling && bInJumpSequence)
    {
        AnimId = EWowAnimId::Jump;
    }

    EWowAnimId PreviousAnimId = GetAnimationForState(PreviousState);
    if (PreviousState == EWowAnimState::Falling && bInJumpSequence)
    {
        PreviousAnimId = EWowAnimId::Jump;
    }

    if ((PreviousState == EWowAnimState::Jumping || PreviousState == EWowAnimState::Falling) &&
        (NewState == EWowAnimState::Jumping || NewState == EWowAnimState::Falling) &&
        PreviousAnimId == AnimId &&
        CurrentAnimation)
    {
        // Do not restart the same airborne clip during takeoff -> descent.
        CurrentState = NewState;
        bPlayingOneShot = false;
        return;
    }

	const bool bPreviousBackpedal = PreviousState == EWowAnimState::Backpedaling ||
		PreviousState == EWowAnimState::BackpedalingLeft ||
		PreviousState == EWowAnimState::BackpedalingRight;
	const bool bNewBackpedal = NewState == EWowAnimState::Backpedaling ||
		NewState == EWowAnimState::BackpedalingLeft ||
		NewState == EWowAnimState::BackpedalingRight;
	if (bPreviousBackpedal && bNewBackpedal && CurrentAnimation)
	{
		// Releasing or pressing A/D during S must not restart the foot cycle.
		CurrentState = NewState;
		bPlayingOneShot = false;
		ApplyVisualState(NewState);
		return;
	}

    CurrentState = NewState;
    ApplyVisualState(NewState);
    // Determine if animation should loop
    bool bShouldLoop = true;
    switch (NewState)
    {
        case EWowAnimState::Dead:
            bShouldLoop = false;
            break;
		case EWowAnimState::Jumping:
			bShouldLoop = !bUseJumpStart;
			break;
        default:
            bShouldLoop = true;
            break;
    }

    // Locomotion may have adjusted the component rate to match ground speed.
    // Every newly selected non-locomotion clip starts at its authored rate.
    MeshComponent->SetPlayRate(1.0f);

    if (PlayAnimationById(AnimId, bShouldLoop))
    {
		// JumpStart is state-machine controlled, not a combat/emote one-shot.
		// Its live apex pose is held through descent and released by Landed().
		// r692: a twirl or flip variation plays to its end instead of freezing at the apex (it would stop
		// halfway round); like the main take-off it is not restarted when the descent begins.
		const FWowAnimVariations* Variations = bUseJumpStart ? GWowAnimVariations.Find(this) : nullptr;
		if (bUseJumpStart && Variations && Variations->bLastJumpWasVariation)
		{
			bPlayingJumpStartClip = false;
			bHoldingJumpApexPose = false;
			bPlayingJumpEndClip = false;
		}
		else if (bUseJumpStart)
		{
			bPlayingJumpStartClip = true;
			bHoldingJumpApexPose = false;
			bPlayingJumpEndClip = false;
		}
		else if (NewState == EWowAnimState::Jumping)
        {
            bPlayingOneShot = false;
        }
        UE_LOG(LogWowAnim, Verbose, TEXT("Animation state changed to %s (AnimID %d)"),
            *UEnum::GetValueAsString(CurrentState), static_cast<int32>(AnimId));
    }
    else
    {
        UE_LOG(LogWowAnim, Warning, TEXT("Failed to play animation for state %s (AnimID %d)"),
            *UEnum::GetValueAsString(CurrentState), static_cast<int32>(AnimId));
    }
}

// Set by the player character while the headset view is first person: the local body then
// does not turn sideways for strafing (the shoulder swung right in front of the eyes).
WOWASSETS_API bool GWowSuppressLocalStrafeYaw = false;

void UWowAnimationController::ApplyVisualState(EWowAnimState State)
{
    if (!MeshComponent)
    {
        return;
    }

    if (!bHasBaseMeshRelativeRotation)
    {
        CaptureBaseMeshRelativeRotation();
    }

    const double CurrentTime = FPlatformTime::Seconds();
    FRotator TargetRotation = BaseMeshRelativeRotation;
    if (State == EWowAnimState::SwimmingForwardLeft || State == EWowAnimState::SwimmingBackwardRight)
        TargetRotation.Yaw -= 45.0f;
    else if (State == EWowAnimState::SwimmingForwardRight || State == EWowAnimState::SwimmingBackwardLeft)
        TargetRotation.Yaw += 45.0f;
    else if (GWowSuppressLocalStrafeYaw && Cast<APawn>(MeshComponent->GetOwner()) && Cast<APawn>(MeshComponent->GetOwner())->IsLocallyControlled())
    {
        // keep facing forward
    }
    else if (State == EWowAnimState::StrafingLeft || State == EWowAnimState::WalkingLeft)
    {
        TargetRotation.Yaw -= VisualStrafeYawDegrees;
    }
    else if (State == EWowAnimState::StrafingRight || State == EWowAnimState::WalkingRight)
    {
        TargetRotation.Yaw += VisualStrafeYawDegrees;
    }
	else if (State == EWowAnimState::BackpedalingLeft)
	{
		TargetRotation.Yaw += BackwardDiagonalVisualYawDegrees;
	}
	else if (State == EWowAnimState::BackpedalingRight)
	{
		TargetRotation.Yaw -= BackwardDiagonalVisualYawDegrees;
	}
    else if ((State == EWowAnimState::Jumping || State == EWowAnimState::Falling) &&
        CurrentTime - LastVisualStrafeInputTime <= 0.22)
    {
        // Jump/fall clips do not encode left/right intent, so keep the
        // latest strafe-facing offset while the player is holding A/D in air.
        TargetRotation.Yaw += LastVisualStrafeYawOffsetDegrees;
    }

    const float DeltaTime = bHasVisualStateUpdateTime
        ? FMath::Clamp(static_cast<float>(CurrentTime - LastVisualStateUpdateTime), 0.0f, 0.05f)
        : 0.0f;
    LastVisualStateUpdateTime = CurrentTime;
    bHasVisualStateUpdateTime = true;

    const FRotator CurrentRotation = MeshComponent->GetRelativeRotation();
    const bool bIsStrafeState =
        State == EWowAnimState::StrafingLeft ||
		State == EWowAnimState::StrafingRight ||
		State == EWowAnimState::BackpedalingLeft ||
		State == EWowAnimState::BackpedalingRight;
    const bool bCurrentlyOffset =
        FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, BaseMeshRelativeRotation.Yaw)) > 1.0f;

    if (DeltaTime <= 0.0f || (!bIsStrafeState && !bCurrentlyOffset))
    {
        MeshComponent->SetRelativeRotation(TargetRotation);
        return;
    }

    const float InterpSpeed = bIsStrafeState ? 12.0f : 10.0f;
    const FRotator SmoothedRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, InterpSpeed);
    MeshComponent->SetRelativeRotation(SmoothedRotation);
}

void UWowAnimationController::UpdateLocomotionPlayRate(float GroundSpeedCmPerSecond, bool bUseAuthoredSpeed)
{
    if (!MeshComponent)
    {
        return;
    }

    float ReferenceSpeed = 0.0f;
    if (CurrentState == EWowAnimState::Walking ||
        CurrentState == EWowAnimState::WalkingLeft || CurrentState == EWowAnimState::WalkingRight ||
		CurrentState == EWowAnimState::Backpedaling ||
		CurrentState == EWowAnimState::BackpedalingLeft ||
		CurrentState == EWowAnimState::BackpedalingRight)
    {
        ReferenceSpeed = 250.0f;
    }
    else if (CurrentState == EWowAnimState::StrafingLeft ||
        CurrentState == EWowAnimState::StrafingRight)
    {
        // ShuffleLeft/ShuffleRight are short loops in WotLK M2s. Running them
        // near 2x speed makes the feet read like sliding/idle in-game.
        ReferenceSpeed = 700.0f;
    }
    else if (CurrentState == EWowAnimState::Running)
    {
        ReferenceSpeed = 700.0f;
    }

    if(bUseAuthoredSpeed && ReferenceSpeed>0.f)
    {
        for(const auto& Pair:AnimationCache)
            if(Pair.Value==CurrentAnimation)
                if(const float* Speed=AuthoredMovementSpeeds.Find(Pair.Key);Speed && *Speed>0.f)
                {
                    const FVector Scale=MeshComponent->GetComponentScale().GetAbs();
                    ReferenceSpeed=*Speed*100.f*FMath::Max(Scale.X,Scale.Y);
                    break;
                }
        // r471 (Stefan: mobs "run in slow motion" on their last step): the rendered speed falls to
        // almost nothing as a path ends, and the run clip followed it down to 10 % speed. Below
        // ~60 % the stride stays at 60 % (the stand takes over a moment later).
        if(FMath::IsFinite(ReferenceSpeed) && ReferenceSpeed>UE_SMALL_NUMBER && FMath::IsFinite(GroundSpeedCmPerSecond))
            MeshComponent->SetPlayRate(FMath::Clamp(GroundSpeedCmPerSecond/ReferenceSpeed,0.6f,4.f));
        return;
    }

    if (ReferenceSpeed > 0.0f)
    {
        const float MaxPlayRate = (CurrentState == EWowAnimState::StrafingLeft ||
            CurrentState == EWowAnimState::StrafingRight) ? 1.15f : 1.5f;
        const float MinPlayRate = (CurrentState == EWowAnimState::StrafingLeft ||
            CurrentState == EWowAnimState::StrafingRight) ? 0.85f : 0.65f;
        const float PlayRate = FMath::Clamp(GroundSpeedCmPerSecond / ReferenceSpeed, MinPlayRate, MaxPlayRate);
        MeshComponent->SetPlayRate(PlayRate);
    }
    else
    {
        MeshComponent->SetPlayRate(1.0f);
    }
}

bool UWowAnimationController::IsMoving(const FWowMovementInfo& MovementInfo) const
{
    return (MovementInfo.MoveFlags & (
        WowMovementFlags::FORWARD |
        WowMovementFlags::BACKWARD |
        WowMovementFlags::STRAFE_LEFT |
        WowMovementFlags::STRAFE_RIGHT
    )) != 0;
}

bool UWowAnimationController::IsWalking(const FWowMovementInfo& MovementInfo) const
{
    return (MovementInfo.MoveFlags & WowMovementFlags::WALK_MODE) != 0;
}
void UWowAnimationController::SetAnimationIdMap(const TMap<int32, int32>& InMap)
{
    // Build AnimationCache from the map: M2 AnimId -> UAnimSequence
    AnimationCache.Empty();
    for (const auto& Pair : InMap)
    {
        int32 AnimId = Pair.Key;
        int32 TrackIndex = Pair.Value;
        if (AllAnimations.IsValidIndex(TrackIndex) && AllAnimations[TrackIndex])
        {
            AnimationCache.Add(AnimId, AllAnimations[TrackIndex]);
            UE_LOG(LogWowAnim, Log, TEXT("SetAnimationIdMap: Mapped AnimID %d -> TrackIndex %d -> Animation %s"),
                AnimId, TrackIndex, *AllAnimations[TrackIndex]->GetName());
        }
        else
        {
            UE_LOG(LogWowAnim, Warning, TEXT("SetAnimationIdMap: Invalid TrackIndex %d for AnimID %d"), TrackIndex, AnimId);
        }
    }
    UE_LOG(LogWowAnim, Log, TEXT("SetAnimationIdMap: mapped %d animation IDs"), AnimationCache.Num());

    // WoW animation ID 0 is the looping Stand/idle animation.  Earlier code
    // preferred ID 3 (Stop), which left character previews in a static pose.
    UAnimSequence* BestIdleAnim = AnimationCache.FindRef(0);
    const int32 BestIdleAnimId = 0;

    if (BestIdleAnim && MeshComponent)
    {
        MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        UWowFootIKInstance::Install(MeshComponent);
        MeshComponent->PlayAnimation(BestIdleAnim, true);
        CurrentAnimation = BestIdleAnim;
        UE_LOG(LogWowAnim, Log, TEXT("SetAnimationIdMap: Playing Stand animation (ID %d): %s"), BestIdleAnimId, *BestIdleAnim->GetName());
    }
    else if (MeshComponent)
    {
        UE_LOG(LogWowAnim, Warning, TEXT("SetAnimationIdMap: No Stand animation available in AnimationCache"));
    }
}

UAnimSequence* UWowAnimationController::ResolveAnimation(int32 AnimationId)
{
    if (UAnimSequence* Animation = AnimationCache.FindRef(AnimationId)) return Animation;
    if (!AnimationLoader || MissingAnimationIds.Contains(AnimationId)) return nullptr;
    if (UAnimSequence* Animation = AnimationLoader(AnimationId))
    {
        // Strong references live with this controller; the shared model cache is weak.
        AnimationCache.Add(AnimationId, Animation);
        AllAnimations.AddUnique(Animation);
        return Animation;
    }
    MissingAnimationIds.Add(AnimationId);
    return nullptr;
}

bool UWowAnimationController::PlayAnimationById(EWowAnimId AnimId, bool bLooping)
{
    return PlayAnimationValue(static_cast<int32>(AnimId), bLooping);
}

bool UWowAnimationController::PlayAnimationValue(int32 Id, bool bLooping)
{
    bPlayingEmote = false;
    // Fatal hit/cast messages may arrive after health reaches zero. They must
    // not replace Death/Dead with an upright wound, attack or cast animation.
    if (CurrentState == EWowAnimState::Dead && Id != static_cast<int32>(EWowAnimId::Death) && Id != static_cast<int32>(EWowAnimId::Dead))
        return false;
    if (!MeshComponent) return false;

    ResolveAnimation(Id);
    UAnimSequence* AnimToPlay = nullptr;

    // Special handling for Stand animation - AnimID 0 might be incorrect in some models
    if (Id == 0) // Stand animation request
    {
        // First try the mapped AnimID 0
        if (TObjectPtr<UAnimSequence>* Found = AnimationCache.Find(0))
        {
            if (*Found)
            {
                AnimToPlay = *Found;
                // Check if this might be a problematic animation by name
                FString AnimName = (*Found)->GetName();
                if (AnimName.Contains(TEXT("kneel"), ESearchCase::IgnoreCase) ||
                    AnimName.Contains(TEXT("sit"), ESearchCase::IgnoreCase))
                {
                    UE_LOG(LogWowAnim, Warning, TEXT("PlayAnimationById: AnimID 0 appears to be '%s', trying alternatives"), *AnimName);
                    AnimToPlay = nullptr; // Force fallback
                }
            }
        }

        // If AnimID 0 is not available or seems wrong, try alternative stand animations
        if (!AnimToPlay)
        {
            TArray<int32> StandFallbackIds = {3, 25}; // Stop (3) is sometimes used as stand, ReadyUnarmed (25) might work
            for (int32 FallbackId : StandFallbackIds)
            {
                ResolveAnimation(FallbackId);
                if (TObjectPtr<UAnimSequence>* FallbackFound = AnimationCache.Find(FallbackId))
                {
                    if (*FallbackFound)
                    {
                        AnimToPlay = *FallbackFound;
                        UE_LOG(LogWowAnim, Log, TEXT("PlayAnimationById: Using AnimID %d as Stand animation fallback"), FallbackId);
                        break;
                    }
                }
            }
        }

        // Last resort: use the first animation in the array
        if (!AnimToPlay && AllAnimations.Num() > 0 && AllAnimations[0])
        {
            AnimToPlay = AllAnimations[0];
            UE_LOG(LogWowAnim, Warning, TEXT("PlayAnimationById: Using AllAnimations[0] as final Stand fallback"));
        }
    }
    else
    {
        // For non-Stand animations, use normal lookup
        if (TObjectPtr<UAnimSequence>* Found = AnimationCache.Find(Id))
        {
            if (*Found)
            {
                AnimToPlay = *Found;
            }
        }
        // r692: JumpStart rolls one of the model's variations (the blood elf twirl, the night elf flip).
        if (Id == static_cast<int32>(EWowAnimId::JumpStart))
        {
            FWowAnimVariations* Variations = GWowAnimVariations.Find(this);
            if (Variations) Variations->bLastJumpWasVariation = false;
            const int32 Key = WowRollVariationKey(this, Id);
            if (Key != Id)
                if (UAnimSequence* Variation = ResolveAnimation(Key))
                {
                    AnimToPlay = Variation;
                    if (Variations) Variations->bLastJumpWasVariation = true;
                }
            // r709 (Stefan: "she still doesn't twist-jump"): say what each jump rolled.
            UE_LOG(LogWowAnim, Log, TEXT("JumpStart on %s: %d variations, rolled %s (%s)"), *GetNameSafe(MeshComponent->GetOwner()),
                Variations && Variations->ById.Find(Id) ? Variations->ById.Find(Id)->Num() : 0,
                Key == Id ? TEXT("main") : *FString::Printf(TEXT("sub %d"), (Key >> 16) - 1), *GetNameSafe(AnimToPlay));
        }

        // The native WotLK ShuffleLeft/ShuffleRight clips are short shuffle
        // loops, which make the feet read like slow sliding in this prototype.
        // Keep the strafe-facing offset in ApplyVisualState, but drive the legs
        // with the normal run loop so sideways movement still has running steps.
        if ((Id == static_cast<int32>(EWowAnimId::ShuffleLeft) ||
            Id == static_cast<int32>(EWowAnimId::ShuffleRight)) &&
            CurrentState != EWowAnimState::TurningLeft && CurrentState != EWowAnimState::TurningRight)
        {
            ResolveAnimation(static_cast<int32>(EWowAnimId::Run));
            if (TObjectPtr<UAnimSequence>* RunFallback = AnimationCache.Find(static_cast<int32>(EWowAnimId::Run)))
            {
                if (*RunFallback)
                {
                    AnimToPlay = *RunFallback;
                }
            }
        }
    }

    // A missing sit-down clip must immediately fall back to the sit loop in the caller.
    if (!AnimToPlay && (Id == static_cast<int32>(EWowAnimId::SitGroundDown) || Id == static_cast<int32>(EWowAnimId::SitGroundUp))) return false;

    // Fallback chain for missing animations
    if (!AnimToPlay)
    {
        TArray<int32> FallbackIds;

        switch (Id)
        {
            case 4: // Walk
                // Many critters and companion pets only author Run; standing still
                // made them glide around. The play rate follows the rendered speed.
                FallbackIds = {5, 0}; // Run → Stand
                break;
            case 5: // Run
                FallbackIds = {4, 0}; // Walk → Stand
                break;
            case 11: // ShuffleLeft
            case 12: // ShuffleRight
                FallbackIds = (CurrentState == EWowAnimState::TurningLeft || CurrentState == EWowAnimState::TurningRight)
                    ? TArray<int32>{0} : TArray<int32>{5, 0}; // Never run in place for a missing turn clip.
                break;
            case 43: case 44: case 45: // Missing directional swim on creatures
                FallbackIds = {42, 41, 0};
                break;
            case 33: // CombatWound
                FallbackIds = {0}; // Stand
                break;
            case 51: case 52: case 53: case 54: // Spell ready/release variants
            case 124: case 125: // Channel variants
                FallbackIds = {0}; // Stand
                break;
            case 16: case 17: case 18: case 19: // Attack animations
            case 57: case 58: case 59: case 95: // Special attacks
                FallbackIds = {0}; // Stand
                break;
            default:
                if (Id != 0) // Any unknown animation
                {
                    FallbackIds = {0}; // Stand
                }
                break;
        }

        // Try fallback animations
        for (int32 FallbackId : FallbackIds)
        {
            ResolveAnimation(FallbackId);
            if (TObjectPtr<UAnimSequence>* FallbackFound = AnimationCache.Find(FallbackId))
            {
                if (*FallbackFound)
                {
                    AnimToPlay = *FallbackFound;
                    break;
                }
            }
        }
    }

    if (!AnimToPlay) return false;

    bPlayingPosture = Id == static_cast<int32>(EWowAnimId::SitGroundUp) || Id == static_cast<int32>(EWowAnimId::SitGroundDown) || Id == static_cast<int32>(EWowAnimId::SitGround) || Id == static_cast<int32>(EWowAnimId::SitChair)
        || Id == static_cast<int32>(EWowAnimId::SitChairLow) || Id == static_cast<int32>(EWowAnimId::SitChairHigh)
        || Id == static_cast<int32>(EWowAnimId::Sleep) || Id == static_cast<int32>(EWowAnimId::Kneel);
    if (bPlayingPosture)
    {
        CurrentState = EWowAnimState::Idle;
        MeshComponent->SetPlayRate(1.0f);
        ApplyVisualState(EWowAnimState::Idle);
        UWowFootIKInstance* FootIK = Cast<UWowFootIKInstance>(MeshComponent->GetAnimInstance());
        // r709 (Stefan, BE female HD: "on her way TO sit her legs are for a split second the other way around and
        // down into the ground, before it's corrected"): r694 only blended the foot IK out (about 0.3 s), so the
        // first frames of the sit-down still bent its legs onto the ground targets and kept the pelvis ~10 cm down.
        // The IK now drops at once. The loop that follows is logged once, not every frame.
        if (CurrentAnimation != AnimToPlay)
            UE_LOG(LogWowAnim, Log, TEXT("Posture clip %d on %s: %s (%.2f s, %s), foot IK pelvis was %.1f"), Id,
                *GetNameSafe(MeshComponent->GetOwner()), *GetNameSafe(AnimToPlay), AnimToPlay->GetPlayLength(),
                bLooping ? TEXT("loop") : TEXT("once"), FootIK ? FootIK->PelvisOffsetCS : 0.f);
        if (FootIK) FootIK->SnapFootIKOff();
        GWowPostureMeshes.Add(MeshComponent);
    }
    else
        GWowPostureMeshes.Remove(MeshComponent);

    if (bLooping && CurrentAnimation == AnimToPlay)
    {
        // Strafing currently uses the run clip as a readable fallback. When the
        // state changes back to forward run, keep the same looping sequence
        // alive instead of restarting it from frame zero and causing a foot-pop.
        bPlayingOneShot = false;
        return true;
    }

    // r640 (Stefan: "smooth animation blending - male human as a test, the rest follows once that works"):
    // crossfade from the clip that was playing. Only the male human models for now (classic and HD).
    UAnimSequence* FadeFrom = nullptr;
    float FadeFromTime = 0.f;
    const bool bFadeFromLooping = !bPlayingOneShot;
    if (CurrentAnimation && CurrentAnimation != AnimToPlay && !bHoldingJumpApexPose
        && MeshComponent->ComponentHasTag(TEXT("WowAnimCrossfade"))) // set by WowCharacterBuilder for HumanMale
    {
        FadeFrom = CurrentAnimation;
        FadeFromTime = MeshComponent->GetPosition();
    }
    MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    UWowFootIKInstance::Install(MeshComponent);
    MeshComponent->PlayAnimation(AnimToPlay, bLooping);
    if (FadeFrom)
        if (UWowFootIKInstance* FadeInstance = Cast<UWowFootIKInstance>(MeshComponent->GetAnimInstance()))
        {
            FadeInstance->BeginCrossfade(FadeFrom, FadeFromTime, bFadeFromLooping, 0.2f);
            static bool bLoggedFirstFade = false;
            if (!bLoggedFirstFade)
            {
                bLoggedFirstFade = true;
                UE_LOG(LogWowAnim, Log, TEXT("Crossfade (male human test) active on %s: %s -> %s"),
                    *GetNameSafe(MeshComponent->GetOwner()), *GetNameSafe(FadeFrom), *GetNameSafe(AnimToPlay));
            }
        }
    CurrentAnimation = AnimToPlay;

    if (Id == static_cast<int32>(EWowAnimId::ShuffleLeft) ||
        Id == static_cast<int32>(EWowAnimId::ShuffleRight) ||
        Id == static_cast<int32>(EWowAnimId::Run) ||
        Id == static_cast<int32>(EWowAnimId::Walkbackwards))
    {
        UE_LOG(LogWowAnim, Verbose, TEXT("PlayAnimationById: AnimID %d -> %s (%.2fs, looping=%d, mesh=%s)"),
            Id,
            *AnimToPlay->GetName(),
            AnimToPlay->GetPlayLength(),
            bLooping ? 1 : 0,
            *GetNameSafe(MeshComponent));
    }

    // Track one-shot animations so state machine doesn't override them
    if (!bLooping)
    {
        bPlayingOneShot = true;
        const double AnimDuration = static_cast<double>(AnimToPlay->GetPlayLength());
        OneShotEndTime = FPlatformTime::Seconds() + AnimDuration;
    }
    else
    {
        bPlayingOneShot = false;
    }

    return true;
}

void UWowAnimationController::SetDead(bool bDead)
{
    if (!MeshComponent) return;
    SetAnimationState(bDead ? EWowAnimState::Dead : EWowAnimState::Idle);
}

void UWowAnimationController::ShowCorpsePose()
{
    if (!MeshComponent) return;
    CurrentState = EWowAnimState::Dead;
    ApplyVisualState(CurrentState);
    if (ResolveAnimation(static_cast<int32>(EWowAnimId::Dead)))
        PlayAnimationById(EWowAnimId::Dead, true);
    else if (PlayAnimationById(EWowAnimId::Death, false) && CurrentAnimation)
    {
        MeshComponent->SetPosition(CurrentAnimation->GetPlayLength(), false);
        MeshComponent->Stop();
    }
    bPlayingOneShot = false;
}

float UWowAnimationController::PlaySheathAnimation(bool bHipWeapon)
{
    // Moving: play the gesture on the upper body over the run/walk cycle (r268).
    if (IsLocomoting() && !bSwimPresentation && CurrentState != EWowAnimState::Dead
        && CurrentState != EWowAnimState::Casting && CurrentState != EWowAnimState::Channeling && MeshComponent)
    {
        UAnimSequence* MoveClip = ResolveAnimation(bHipWeapon ? 90 : 89);
        if (!MoveClip && bHipWeapon) MoveClip = ResolveAnimation(89);
        if (UWowFootIKInstance* Instance = Cast<UWowFootIKInstance>(MeshComponent->GetAnimInstance()))
            if (MoveClip && Instance->PlayUpperBodyOverlay(MoveClip))
            {
                UE_LOG(LogWowAnim, Log, TEXT("Sheath gesture as upper-body overlay (%.2fs)"), MoveClip->GetPlayLength());
                return MoveClip->GetPlayLength();
            }
    }
    if (IsLocomoting() || bSwimPresentation || CurrentState == EWowAnimState::Dead
        || CurrentState == EWowAnimState::Casting || CurrentState == EWowAnimState::Channeling)
    {
        UE_LOG(LogWowAnim, Log, TEXT("Sheath gesture skipped: moving=%d swim=%d state=%d"), IsLocomoting() ? 1 : 0, bSwimPresentation ? 1 : 0, int32(CurrentState));
        return 0.f;
    }
    const int32 Id = bHipWeapon ? 90 : 89;
    UAnimSequence* Clip = ResolveAnimation(Id);
    if (!Clip && bHipWeapon) Clip = ResolveAnimation(89);
    if (!Clip) { UE_LOG(LogWowAnim, Log, TEXT("Sheath gesture: model has no clip %d"), Id); return 0.f; }
    if (!PlayAnimationById(static_cast<EWowAnimId>(static_cast<uint8>(Clip == ResolveAnimation(90) ? 90 : 89)), false)) return 0.f;
    return Clip->GetPlayLength();
}

bool UWowAnimationController::IsLocomoting() const
{
    return (LastMoveFlags & (WowMovementFlags::FORWARD | WowMovementFlags::BACKWARD |
        WowMovementFlags::STRAFE_LEFT | WowMovementFlags::STRAFE_RIGHT |
        WowMovementFlags::JUMPING | WowMovementFlags::FALLING)) != 0;
}

bool UWowAnimationController::BeginSpellCast(EWowAnimId AnimId)
{
    if (bSwimPresentation || IsLocomoting())
    {
        UE_LOG(LogWowAnim, Log, TEXT("SpellAnim: cast %d skipped on %s (swim=%d moveFlags=0x%x)"),
            int32(AnimId), MeshComponent ? *MeshComponent->GetName() : TEXT("?"), bSwimPresentation ? 1 : 0, LastMoveFlags);
        return true; // keep swimming / running
    }
    // r372 (Stefan: with a cast queued, the release step forward was cut off): let the release
    // clip of the previous cast finish, then start this cast's loop (TickPendingSpellCast).
    if (bPlayingOneShot && CurrentState == EWowAnimState::Casting && FPlatformTime::Seconds() < OneShotEndTime)
    {
        bPendingSpellCast = true;
        PendingSpellCastAnim = AnimId;
        UE_LOG(LogWowAnim, Log, TEXT("SpellAnim: cast %d waits %.2fs for the release clip on %s"), int32(AnimId),
            OneShotEndTime - FPlatformTime::Seconds(), MeshComponent ? *MeshComponent->GetName() : TEXT("?"));
        return true;
    }
    bPendingSpellCast = false;
    const bool bPlayed = PlayAnimationById(AnimId, true);
    UE_LOG(LogWowAnim, Log, TEXT("SpellAnim: cast %d on %s played=%d resolved=%d"), int32(AnimId),
        MeshComponent ? *MeshComponent->GetName() : TEXT("?"), bPlayed ? 1 : 0, AnimationCache.Contains(int32(AnimId)) ? 1 : 0);
    if (bPlayed)
    {
        CurrentState = EWowAnimState::Casting;
        bPlayingOneShot = false;
    }
    return bPlayed;
}

void UWowAnimationController::TickPendingSpellCast()
{
    if (!bPendingSpellCast || FPlatformTime::Seconds() < OneShotEndTime) return;
    bPendingSpellCast = false;
    if (CurrentState != EWowAnimState::Casting) return; // ended or interrupted meanwhile
    bPlayingOneShot = false;
    OneShotEndTime = 0.0;
    BeginSpellCast(PendingSpellCastAnim);
}

bool UWowAnimationController::PlaySpellRelease(EWowAnimId AnimId)
{
    bPendingSpellCast = false;
    if (bSwimPresentation || IsLocomoting()) return true; // run on while the buff goes out
    const bool bPlayed = PlayAnimationById(AnimId, false);
    if (bPlayed)
    {
        CurrentState = EWowAnimState::Casting;
    }
    return bPlayed;
}

bool UWowAnimationController::BeginSpellChannel(EWowAnimId AnimId)
{
    if (bSwimPresentation || IsLocomoting()) return true;
    const bool bPlayed = PlayAnimationById(AnimId, true);
    if (bPlayed)
    {
        CurrentState = EWowAnimState::Channeling;
        bPlayingOneShot = false;
    }
    return bPlayed;
}

void UWowAnimationController::EndSpellPresentation()
{
    bPendingSpellCast = false;
    if (CurrentState == EWowAnimState::Dead) return;
    // A swimmer never left the swim loop; don't flash the planted Stand pose.
    if (bSwimPresentation && CurrentState != EWowAnimState::Casting && CurrentState != EWowAnimState::Channeling) return;
    // Running/jumping casters never entered the cast pose: don't snap them to Stand mid-stride.
    if (IsLocomoting() && CurrentState != EWowAnimState::Casting && CurrentState != EWowAnimState::Channeling) return;
    bPlayingOneShot = false;
    OneShotEndTime = 0.0;
    CurrentAnimation = nullptr;
    CurrentState = EWowAnimState::Idle;
    PlayAnimationById(EWowAnimId::Stand, true);
    ApplyVisualState(EWowAnimState::Idle);
}

EWowAnimId UWowAnimationController::GetMeleeReadyAnimation() const
{
    AActor* Owner = MeshComponent ? MeshComponent->GetOwner() : nullptr;
    if (!Owner) return EWowAnimId::ReadyUnarmed;
    EWowAnimId Ready = EWowAnimId::ReadyUnarmed;
    TInlineComponentArray<UStaticMeshComponent*> Equipment(Owner);
    for (const auto* Weapon : Equipment)
    {
        if (!Weapon->ComponentHasTag(TEXT("WowHeldWeapon")) ||
            Weapon->ComponentHasTag(TEXT("WowRangedWeapon")) ||
            Weapon->ComponentHasTag(TEXT("WowSheathShield"))) continue;
        if (Weapon->ComponentHasTag(TEXT("WowStaffWeapon"))) return EWowAnimId::Ready2HL;
        if (Weapon->ComponentHasTag(TEXT("WowSheathTwoHandWeapon"))) return EWowAnimId::Ready2H;
        Ready = EWowAnimId::Ready1H;
    }
    return Ready;
}

bool UWowAnimationController::PlayAttackAnimation()
{
    const EWowAnimId Ready = GetMeleeReadyAnimation();
    const EWowAnimId Attack = Ready == EWowAnimId::Ready2HL ? EWowAnimId::Attack2HL :
        Ready == EWowAnimId::Ready2H ? EWowAnimId::Attack2H :
        Ready == EWowAnimId::Ready1H ? EWowAnimId::Attack1H : EWowAnimId::AttackUnarmed;
    // r361 (Stefan: auto-attacking while swimming put the legs in a standing pose): swimming or
    // moving, the swing plays on the upper body only and the legs keep the swim/run cycle, as the
    // sheath gesture does (r268).
    if ((bSwimPresentation || IsLocomoting()) && CurrentState != EWowAnimState::Dead && MeshComponent)
        if (UWowFootIKInstance* Instance = Cast<UWowFootIKInstance>(MeshComponent->GetAnimInstance()))
            for (const EWowAnimId Candidate : { Attack, EWowAnimId::Attack1H, EWowAnimId::AttackUnarmed, EWowAnimId::Attack2H })
                if (UAnimSequence* Swing = ResolveAnimation(static_cast<int32>(Candidate)))
                {
                    if (Instance->PlayUpperBodyOverlay(Swing)) return true;
                    break;
                }
    if (PlayAnimationById(Attack, false)) return true;
    // Try attack animations in order of preference until one succeeds
    if (PlayAnimationById(EWowAnimId::Attack1H, false)) return true;
    if (PlayAnimationById(EWowAnimId::AttackUnarmed, false)) return true;
    if (PlayAnimationById(EWowAnimId::Attack2H, false)) return true;
    if (PlayAnimationById(EWowAnimId::Spell, false)) return true;

    UE_LOG(LogWowAnim, Warning, TEXT("PlayAttackAnimation: No attack animations available"));
    return false;
}

bool UWowAnimationController::PlayWoundAnimation()
{
    // Try wound animations in order of preference until one succeeds
    if (PlayAnimationById(EWowAnimId::CombatWound, false)) return true;
    if (PlayAnimationById(EWowAnimId::StandWound, false)) return true;

    UE_LOG(LogWowAnim, Warning, TEXT("PlayWoundAnimation: No wound animations available"));
    return false;
}

bool UWowAnimationController::PlayEmote(int32 AnimId, bool bLooping)
{
    // An unavailable emote must not pretend to be Stand or an arbitrary clip.
    if (AnimId < 0 || !ResolveAnimation(AnimId)) return false;
    StopEmote();
    CurrentState = EWowAnimState::Idle;
    ApplyVisualState(CurrentState);
    if (!PlayAnimationValue(AnimId, bLooping)) return false;
    MeshComponent->SetPlayRate(1.0f);
    bPlayingEmote = true;
    bEmoteLoops = bLooping;
    return true;
}

void UWowAnimationController::StopEmote()
{
    if (!bPlayingEmote) return;
    bPlayingEmote = false;
    bPlayingOneShot = false;
    CurrentAnimation = nullptr;
}

void UWowAnimationController::SyncEmoteState(uint32 ServerEmoteId, int32 AnimId)
{
    if (LastServerEmoteId == ServerEmoteId) return;
    LastServerEmoteId = ServerEmoteId;
    StopEmote();
    if (ServerEmoteId != 0) PlayEmote(AnimId, true);
}

// r709 (Stefan: "she still doesn't twist-jump"): the player character builds its own controller from the model's
// (WowPlayerCharacter) and copied only the clips, id map and loader, so its variation table stayed empty and every
// jump played the main take-off. Its authored run/walk speeds were missing the same way.
void UWowAnimationController::CopyAuthoredDataFrom(const UWowAnimationController* Other)
{
    if (!Other || Other == this) return;
    AuthoredMovementSpeeds = Other->AuthoredMovementSpeeds;
    if (const FWowAnimVariations* Variations = GWowAnimVariations.Find(Other))
    {
        FWowAnimVariations Copy = *Variations;
        Copy.bLastJumpWasVariation = false;
        GWowAnimVariations.Add(this, MoveTemp(Copy));
    }
}

void UWowAnimationController::SetAuthoredMovementSpeeds(const TArray<FM2AnimSequence>& Sequences)
{
    for (auto It = GWowAnimVariations.CreateIterator(); It; ++It)
        if (!It.Key().IsValid()) It.RemoveCurrent();
    FWowAnimVariations& Variations = GWowAnimVariations.FindOrAdd(this);
    Variations.ById.Reset();
    Variations.Main.Reset();
    for (const FM2AnimSequence& Sequence : Sequences)
    {
        const int32 AnimId = Sequence.AnimationId;
        const int32 SubId = Sequence.SubAnimationId;
        const int32 Frequency = Sequence.Probability;
        if (AnimId != static_cast<int32>(EWowAnimId::JumpStart)) continue;
        TArray<TPair<int32, int32>>& Subs = Variations.ById.FindOrAdd(AnimId);
        Subs.Add(TPair<int32, int32>(SubId, Frequency));
        // Same rule as the loader (WowSkeletalMeshBuilder r482): the highest frequency, the first one on a tie.
        int32* Main = Variations.Main.Find(AnimId);
        if (!Main) { Variations.Main.Add(AnimId, SubId); continue; }
        const int32 MainSub = *Main;
        const TPair<int32, int32>* Current = Subs.FindByPredicate([MainSub](const TPair<int32, int32>& Sub) { return Sub.Key == MainSub; });
        if (Current && Frequency > Current->Value) *Main = SubId;
    }
    AuthoredMovementSpeeds.Empty();
    for(const auto& Sequence:Sequences)
        if(FMath::IsFinite(Sequence.MoveSpeed) && Sequence.MoveSpeed>0.f &&
            (Sequence.SubAnimationId==0 || !AuthoredMovementSpeeds.Contains(Sequence.AnimationId)))
            AuthoredMovementSpeeds.Add(Sequence.AnimationId,Sequence.MoveSpeed);
}

// ---------------------------------------------------------------------------
// Foot IK (bipeds)

namespace
{
TAutoConsoleVariable<int32> CVarWowFootIK(TEXT("wow.FootIK"), 1,
    TEXT("Ground foot IK for players and humanoid NPCs: 1 on, 0 off."));

struct FWowFootIKProxy : public FAnimSingleNodeInstanceProxy
{
    FWowFootIKProxy() {}
    FWowFootIKProxy(UAnimInstance* InInstance) : FAnimSingleNodeInstanceProxy(InInstance) {}

    bool bValid = false;
    int32 Hip[2] = { INDEX_NONE, INDEX_NONE }, Knee[2] = { INDEX_NONE, INDEX_NONE }, Foot[2] = { INDEX_NONE, INDEX_NONE };
    float Offset[2] = { 0.f, 0.f };
    FVector Forward[2] = { FVector::ZeroVector, FVector::ZeroVector };
    TArray<int32> KneeTwin[2];
    TArray<int32> KneeChild[2];
    float Pelvis = 0.f, Alpha = 0.f;
    FVector Up = FVector::UpVector;
    FVector OutFoot[2] = { FVector::ZeroVector, FVector::ZeroVector };
    bool bOutValid = false;
    // Crossfade (r640).
    UAnimSequence* FadeSeq = nullptr;
    float FadeTime = 0.f, FadeWeightProxy = 0.f;
    // Upper-body overlay (r268).
    UAnimSequence* OverlaySeq = nullptr;
    float OverlayTime = 0.f, OverlayWeight = 0.f;
    TArray<uint8> OverlayMask;
    // Head look (r292).
    int32 HeadMesh = INDEX_NONE, NeckMesh = INDEX_NONE;
    FQuat LookDelta = FQuat::Identity;
    bool bLook = false;
    FVector LookDir = FVector(0.0, 1.0, 0.0);
    FVector HeadForward = FVector(0.0, 1.0, 0.0);
    float LookAlpha = 0.f;
    bool bHeadOnly = false;

    virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
    {
        FAnimSingleNodeInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
        if (UWowFootIKInstance* Owner = Cast<UWowFootIKInstance>(InAnimInstance))
        {
            // Hand last evaluation's animated feet to the game thread, take its new targets.
            if (bOutValid) { Owner->AnimFootCS[0] = OutFoot[0]; Owner->AnimFootCS[1] = OutFoot[1]; Owner->bHaveAnimFeet = true; }
            bValid = Owner->bLegsValid;
            for (int32 S = 0; S < 2; ++S) { Hip[S] = Owner->HipBone[S]; Knee[S] = Owner->KneeBone[S]; Foot[S] = Owner->FootBone[S]; Offset[S] = Owner->FootOffsetCS[S]; Forward[S] = Owner->FootForwardCS[S];
                if (KneeTwin[S] != Owner->KneeTwinBones[S]) KneeTwin[S] = Owner->KneeTwinBones[S];
                if (KneeChild[S] != Owner->KneeChildBones[S]) KneeChild[S] = Owner->KneeChildBones[S]; }
            Pelvis = Owner->PelvisOffsetCS;
            Alpha = Owner->IKAlpha;
            Up = Owner->UpCS;
            FadeSeq = Owner->bFadeActive ? Owner->FadeFromSequence.Get() : nullptr;
            FadeTime = Owner->FadeFromTime;
            FadeWeightProxy = Owner->FadeWeight;
            OverlaySeq = Owner->bOverlayActive ? Owner->OverlaySequence.Get() : nullptr;
            OverlayTime = Owner->OverlayTime;
            OverlayWeight = Owner->OverlayWeight;
            if (OverlaySeq && OverlayMask.Num() != Owner->UpperBodyMask.Num()) OverlayMask = Owner->UpperBodyMask;
            if (!OverlaySeq) OverlayMask.Reset();
            HeadMesh = Owner->LookHeadBone;
            NeckMesh = Owner->LookNeckBone;
            LookDelta = Owner->LookDeltaCS;
            bLook = Owner->bLookActive;
            LookDir = Owner->LookDirCS;
            HeadForward = Owner->LookHeadForwardLocal;
            LookAlpha = Owner->LookWeight;
            bHeadOnly = Owner->bLookHeadOnly;
        }
    }

    // r640: the previous clip, still playing, blended over the whole body with a falling weight.
    void ApplyCrossfade(FPoseContext& Output)
    {
        if (!FadeSeq || FadeWeightProxy <= 0.001f) return;
        FCompactPose& Pose = Output.Pose;
        FPoseContext FromContext(this);
        FAnimationPoseData FromData(FromContext);
        FadeSeq->GetAnimationPose(FromData, FAnimExtractContext(double(FadeTime), false));
        for (FCompactPoseBoneIndex Index : Pose.ForEachBoneIndex())
        {
            Pose[Index].BlendWith(FromContext.Pose[Index], FadeWeightProxy);
            Pose[Index].NormalizeRotation();
        }
    }

    // Lay the overlay clip over the upper-body bones (local space blend; legs and the
    // root-pelvis chain keep the base clip, so the run cycle carries on underneath).
    void ApplyUpperBodyOverlay(FPoseContext& Output)
    {
        if (!OverlaySeq || OverlayWeight <= 0.001f || OverlayMask.IsEmpty()) return;
        FCompactPose& Pose = Output.Pose;
        const FBoneContainer& Bones = Pose.GetBoneContainer();
        FPoseContext OverlayContext(this);
        FAnimationPoseData OverlayData(OverlayContext);
        OverlaySeq->GetAnimationPose(OverlayData, FAnimExtractContext(double(OverlayTime), false));
        for (FCompactPoseBoneIndex Index : Pose.ForEachBoneIndex())
        {
            const int32 MeshIndex = Bones.MakeMeshPoseIndex(Index).GetInt();
            if (OverlayMask.IsValidIndex(MeshIndex) && OverlayMask[MeshIndex])
            {
                Pose[Index].BlendWith(OverlayContext.Pose[Index], OverlayWeight);
                Pose[Index].NormalizeRotation();
            }
        }
    }

    // Turn the head (60 %) and its neck (40 %) by a component-space rotation about their own
    // pivots; everything below them (helmet, hair, jaw) follows as children.
    void ApplyHeadLook(FPoseContext& Output)
    {
        if (!bLook || HeadMesh == INDEX_NONE) return;
        FCompactPose& Pose = Output.Pose;
        const FBoneContainer& Bones = Pose.GetBoneContainer();
        const FCompactPoseBoneIndex Head = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(HeadMesh));
        if (!Head.IsValid()) return;
        const FCompactPoseBoneIndex Neck = NeckMesh != INDEX_NONE
            ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(NeckMesh)) : FCompactPoseBoneIndex(INDEX_NONE);
        auto ComponentRotation = [&Pose](FCompactPoseBoneIndex Index)
        {
            FQuat Result = FQuat::Identity;
            for (; Index.IsValid(); Index = Pose.GetParentBoneIndex(Index)) Result = Pose[Index].GetRotation() * Result;
            return Result;
        };
        auto Turn = [&](FCompactPoseBoneIndex Index, const FQuat& Delta)
        {
            const FCompactPoseBoneIndex Parent = Pose.GetParentBoneIndex(Index);
            const FQuat ParentCS = Parent.IsValid() ? ComponentRotation(Parent) : FQuat::Identity;
            Pose[Index].SetRotation((ParentCS.Inverse() * Delta * ParentCS * Pose[Index].GetRotation()).GetNormalized());
        };
        // r372 (Stefan: the head looked beside the target): turn the head from where the animation
        // has it pointing (ready stances already twist it) onto the wanted direction, instead of
        // adding the body-relative angle on top of the animated twist.
        const FVector Current = ComponentRotation(Head).RotateVector(HeadForward).GetSafeNormal();
        const FVector Wanted = LookDir.GetSafeNormal();
        if (Current.IsNearlyZero() || Wanted.IsNearlyZero()) return;
        FQuat Full = FQuat::FindBetweenNormals(Current, Wanted);
        FVector Axis; double Angle = 0.0;
        Full.ToAxisAndAngle(Axis, Angle);
        const double MaxAngle = FMath::DegreesToRadians(95.0); // r446: room for 85 degrees of turn plus pitch
        if (Angle > MaxAngle) Full = FQuat(Axis, MaxAngle);
        const FQuat Delta = FQuat::Slerp(FQuat::Identity, Full, FMath::Clamp(LookAlpha, 0.f, 1.f));
        FQuat NeckPart = FQuat::Identity;
        if (Neck.IsValid() && !bHeadOnly)
        {
            NeckPart = FQuat::Slerp(FQuat::Identity, Delta, 0.4f);
            Turn(Neck, NeckPart);
        }
        Turn(Head, Delta * NeckPart.Inverse());
    }

    virtual bool Evaluate(FPoseContext& Output) override
    {
        const bool bOk = FAnimSingleNodeInstanceProxy::Evaluate(Output);
        if (bOk) ApplyCrossfade(Output); // r640
        if (bOk) ApplyUpperBodyOverlay(Output);
        if (bOk) ApplyHeadLook(Output);
        if (!bOk || !bValid) return bOk;
        FCompactPose& Pose = Output.Pose;
        const FBoneContainer& Bones = Pose.GetBoneContainer();
        FCompactPoseBoneIndex H[2] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE) };
        FCompactPoseBoneIndex K[2] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE) };
        FCompactPoseBoneIndex F[2] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE) };
        for (int32 S = 0; S < 2; ++S)
        {
            H[S] = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Hip[S]));
            K[S] = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Knee[S]));
            F[S] = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Foot[S]));
            if (!H[S].IsValid() || !K[S].IsValid() || !F[S].IsValid()) return bOk; // LOD dropped a leg bone
        }
        auto ComponentSpace = [&Pose](FCompactPoseBoneIndex Index)
        {
            FTransform Result = Pose[Index];
            for (FCompactPoseBoneIndex Parent = Pose.GetParentBoneIndex(Index); Parent.IsValid(); Parent = Pose.GetParentBoneIndex(Parent))
                Result = Result * Pose[Parent];
            return Result;
        };
        for (int32 S = 0; S < 2; ++S) OutFoot[S] = ComponentSpace(F[S]).GetLocation();
        bOutValid = true;
        if (Alpha <= 0.001f) return bOk;

        // Lower the whole body so the lower foot can reach its ground.
        const FCompactPoseBoneIndex Root(0);
        Pose[Root].AddToTranslation(Up * (Pelvis * Alpha));

        for (int32 S = 0; S < 2; ++S)
        {
            const FTransform HipCS = ComponentSpace(H[S]);
            const FTransform KneeCS = ComponentSpace(K[S]);
            TArray<TPair<FCompactPoseBoneIndex, FTransform>, TInlineAllocator<4>> Twins;
            for (const int32 TwinMesh : KneeTwin[S])
            {
                const FCompactPoseBoneIndex TwinIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(TwinMesh));
                if (TwinIndex.IsValid()) Twins.Emplace(TwinIndex, ComponentSpace(TwinIndex));
            }
            TArray<TPair<FCompactPoseBoneIndex, FTransform>, TInlineAllocator<4>> Children;
            for (const int32 ChildMesh : KneeChild[S])
            {
                const FCompactPoseBoneIndex ChildIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(ChildMesh));
                if (ChildIndex.IsValid() && Pose.GetParentBoneIndex(ChildIndex) == K[S]) Children.Emplace(ChildIndex, ComponentSpace(ChildIndex));
            }
            const FTransform FootCS = ComponentSpace(F[S]);
            const FVector Hp = HipCS.GetLocation(), Kp = KneeCS.GetLocation(), Fp = FootCS.GetLocation();
            const FVector Target = OutFoot[S] + Up * (Offset[S] * Alpha) + Forward[S] * Alpha;
            const double A = (Kp - Hp).Size(), B = (Fp - Kp).Size();
            FVector ToTarget = Target - Hp;
            double D = ToTarget.Size();
            if (A < 1.0 || B < 1.0 || D < 1.0) continue;
            const FVector U = ToTarget / D;
            D = FMath::Clamp(D, FMath::Abs(A - B) + 0.5, A + B - 0.05);
            // r465 (Stefan: the lower thigh of his orc was very thin just above the knee on a raised
            // foot): the skin there is weighted half thigh, half shin, and linear skinning pulls such
            // vertices into the joint as the knee folds (about 30 % thinner at 90 degrees). The IK may
            // fold the knee to at most 65 degrees, or as far as the animation already folds it; above
            // that the raised foot stays a little lower instead.
            {
                const double D0 = (Fp - Hp).Size();
                const double MaxFold = FMath::DegreesToRadians(65.0);
                const double DFold = FMath::Sqrt(A * A + B * B + 2.0 * A * B * FMath::Cos(MaxFold));
                D = FMath::Max(D, FMath::Min(D0, DFold));
            }
            // Keep the knee bending the way the animation bends it. A standing leg is nearly
            // straight, so that bend is only a few millimetres of noise (or zero): the knee then
            // went sideways (the old fallback crossed with +Y, which is the model's forward) and a
            // foot raised onto a kerb twisted the leg out (r261, Stefan). Knees bend forward:
            // +Y in component space (M2 +X); the animated bend only counts once it is clear.
            const FVector KneeForward = FVector(0.0, 1.0, 0.0) - U * U.Y; // forward, perpendicular to the leg
            FVector Pole = (Kp - Hp) - U * FVector::DotProduct(Kp - Hp, U);
            const double Bend = Pole.Size() / A;
            if (Bend < 0.12 || FVector::DotProduct(Pole, KneeForward) < 0.0)
                Pole = FMath::Lerp(KneeForward.GetSafeNormal(), Pole.GetSafeNormal(), float(FMath::Clamp(Bend / 0.12, 0.0, 1.0)) * (FVector::DotProduct(Pole, KneeForward) < 0.0 ? 0.0f : 1.0f));
            if (Pole.SizeSquared() < 1e-6) Pole = FVector(0.0, 1.0, 0.0);
            Pole.Normalize();
            const double CosHip = FMath::Clamp((A * A + D * D - B * B) / (2.0 * A * D), -1.0, 1.0);
            const double SinHip = FMath::Sqrt(FMath::Max(0.0, 1.0 - CosHip * CosHip));
            const FVector NewKnee = Hp + U * (A * CosHip) + Pole * (A * SinHip);
            const FVector NewFoot = Hp + U * D;
            const FQuat Q1 = FQuat::FindBetweenNormals((Kp - Hp).GetSafeNormal(), (NewKnee - Hp).GetSafeNormal());
            const FQuat Q2 = FQuat::FindBetweenNormals(Q1.RotateVector(Fp - Kp).GetSafeNormal(), (NewFoot - NewKnee).GetSafeNormal());
            FTransform HipNew = HipCS;   HipNew.SetRotation(Q1 * HipCS.GetRotation());
            FTransform KneeNew = KneeCS; KneeNew.SetLocation(NewKnee); KneeNew.SetRotation(Q2 * Q1 * KneeCS.GetRotation());
            FTransform FootNew = FootCS; FootNew.SetLocation(NewFoot); // ankle keeps its animated orientation
            const FCompactPoseBoneIndex HipParent = Pose.GetParentBoneIndex(H[S]);
            const FTransform HipParentCS = HipParent.IsValid() ? ComponentSpace(HipParent) : FTransform::Identity;
            Pose[H[S]] = HipNew.GetRelativeTransform(HipParentCS);
            Pose[K[S]] = KneeNew.GetRelativeTransform(HipNew);
            Pose[F[S]] = FootNew.GetRelativeTransform(KneeNew);
            Pose[H[S]].NormalizeRotation(); Pose[K[S]].NormalizeRotation(); Pose[F[S]].NormalizeRotation();
            // r333 (Stefan: the thigh went flat towards the knee on a high step): the knee helper
            // bones carry the skin around the joint. Turning them the full knee bend dragged the lower
            // thigh with the shin; like a real knee (and the authored animations) they take half of it.
            // r350 (Stefan: the raised leg looked rubbery, one long curve from knee to ankle): half was
            // too soft; three quarters keeps a defined knee without flattening the thigh again.
            const FQuat KneeDelta = FQuat::Slerp(FQuat::Identity, Q2, 0.75f) * Q1;
            for (const auto& Twin : Twins)
            {
                FTransform TwinNew = Twin.Value;
                TwinNew.SetLocation(NewKnee + KneeDelta.RotateVector(Twin.Value.GetLocation() - Kp));
                TwinNew.SetRotation(KneeDelta * Twin.Value.GetRotation());
                Pose[Twin.Key] = TwinNew.GetRelativeTransform(HipNew);
                Pose[Twin.Key].NormalizeRotation();
            }
            // r422: helpers hanging from the knee at its pivot take half of the IK bend (see
            // KneeChildBones), so the joint keeps a corner and the back of the thigh its volume.
            const FQuat ChildDelta = FQuat::Slerp(FQuat::Identity, Q2, 0.5f) * Q1;
            for (const auto& Child : Children)
            {
                FTransform ChildNew = Child.Value;
                ChildNew.SetLocation(NewKnee + ChildDelta.RotateVector(Child.Value.GetLocation() - Kp));
                ChildNew.SetRotation(ChildDelta * Child.Value.GetRotation());
                Pose[Child.Key] = ChildNew.GetRelativeTransform(KneeNew);
                Pose[Child.Key].NormalizeRotation();
            }
        }
        return bOk;
    }
};
}

void UWowFootIKInstance::BeginCrossfade(UAnimSequence* From, float FromTime, bool bFromLooping, float Duration)
{
    if (!From || Duration <= 0.f) return;
    // A change during a fade starts from the clip that was fully playing; the shorter tail is not noticed.
    FadeFromSequence = From;
    FadeFromTime = FMath::Max(FromTime, 0.f);
    bFadeFromLooping = bFromLooping;
    FadeDuration = Duration;
    FadeElapsed = 0.f;
    FadeWeight = 1.f;
    bFadeActive = true;
}

FAnimInstanceProxy* UWowFootIKInstance::CreateAnimInstanceProxy()
{
    return new FWowFootIKProxy(this);
}

void UWowFootIKInstance::Install(USkeletalMeshComponent* Mesh)
{
    if (!Mesh || !Mesh->GetSkinnedAsset() || Mesh->GetAnimationMode() != EAnimationMode::AnimationSingleNode) return;
    if (Cast<UWowFootIKInstance>(Mesh->GetAnimInstance())) return;
    UWowFootIKInstance* Instance = NewObject<UWowFootIKInstance>(Mesh);
    Mesh->ClearAnimScriptInstance();
    Mesh->AnimScriptInstance = Instance;
    Instance->InitializeAnimation();
}

void UWowFootIKInstance::ResolveLegs()
{
    bLegsResolved = true;
    bLegsValid = false;
    const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    if (!Mesh || !Mesh->GetSkinnedAsset()) return;
    // One answer per skeleton: instances are recreated when the mesh re-initialises.
    struct FLegs { bool bValid = false; int32 Hip[2], Knee[2], Foot[2]; float Thigh = 0.f, Shin = 0.f; TArray<int32> Twins[2]; TArray<int32> Children[2]; };
    static TMap<TWeakObjectPtr<const USkinnedAsset>, FLegs> Resolved;
    if (const FLegs* Known = Resolved.Find(Mesh->GetSkinnedAsset()))
    {
        bLegsValid = Known->bValid;
        for (int32 S = 0; S < 2; ++S) { HipBone[S] = Known->Hip[S]; KneeBone[S] = Known->Knee[S]; FootBone[S] = Known->Foot[S]; }
        ThighLength = Known->Thigh; ShinLength = Known->Shin;
        for (int32 S = 0; S < 2; ++S) { KneeTwinBones[S] = Known->Twins[S]; KneeChildBones[S] = Known->Children[S]; }
        return;
    }
    FLegs& Out = Resolved.Add(Mesh->GetSkinnedAsset());
    const FReferenceSkeleton& Ref = Mesh->GetSkinnedAsset()->GetRefSkeleton();
    const int32 Num = Ref.GetNum();
    if (Num < 8) return;
    TArray<FVector> Pos; Pos.SetNum(Num);
    TArray<FTransform> CS; CS.SetNum(Num);
    for (int32 I = 0; I < Num; ++I)
    {
        const int32 P = Ref.GetParentIndex(I);
        CS[I] = P >= 0 ? Ref.GetRefBonePose()[I] * CS[P] : Ref.GetRefBonePose()[I];
        Pos[I] = CS[I].GetLocation();
    }
    FBox Box(ForceInit);
    for (const FVector& P : Pos) Box += P;
    const FVector Size = Box.GetSize();
    const double Height = Size.Z;
    // r341: a biped is taller than it is long. Min() let every long, narrow quadruped
    // (wolves, ghost wolf: 55 x 339 x 236) pass and bent a pair of its legs with biped IK.
    if (Height < 40.0 || Height < FMath::Max(Size.X, Size.Y) * 1.2)
    {
        UE_LOG(LogWowAnim, Log, TEXT("FootIK: %s is not a biped (size %s)"), *GetNameSafe(Mesh->GetSkinnedAsset()), *Size.ToString());
        return;
    }
    auto Rel = [&](int32 I) { return (Pos[I].Z - Box.Min.Z) / Height; };
    FString Why;
    // Legs sit apart along one horizontal axis; try both (the WoW->UE swap faces models +Y).
    for (int32 Axis = 0; Axis < 2 && !Out.bValid; ++Axis)
    {
        bool bAxisOk = true;
        for (int32 S = 0; S < 2 && bAxisOk; ++S)
        {
            const double Side = S == 0 ? 1.0 : -1.0;
            auto Lateral = [&](int32 I) { return (Axis == 0 ? Pos[I].X : Pos[I].Y) * Side; };
            // Try the lowest bones on this side in turn (r288): human, dwarf and most NPC models
            // keep ground-level helper bones parented straight to the root (footstep/attachment
            // points), and taking only the very lowest one meant the leg was never found - foot
            // IK was off for almost every model. Draenei and tauren legs carry an extra hock
            // joint; the knee is still the chain bone nearest mid-leg and the "ankle" its child.
            TArray<int32> Candidates;
            for (int32 I = 0; I < Num; ++I)
                if (Lateral(I) > Height * 0.02 && Rel(I) <= 0.15) Candidates.Add(I);
            Candidates.Sort([&](int32 A, int32 B) { return Pos[A].Z < Pos[B].Z; });
            if (Candidates.IsEmpty()) { Why += FString::Printf(TEXT(" ax%d s%d: no low bone;"), Axis, S); bAxisOk = false; break; }
            bool bSideOk = false;
            FString Rels;
            for (int32 Try = 0; Try < FMath::Min(Candidates.Num(), 10) && !bSideOk; ++Try)
            {
                const int32 Lowest = Candidates[Try];
                // The leg chain: from the low bone up to the last bone still off to this side
                // (the thigh root); above it is the pelvis, on the centre line.
                TArray<int32> Chain;
                for (int32 I = Lowest; I >= 0; I = Ref.GetParentIndex(I))
                {
                    if (Lateral(I) < Height * 0.015 && !Chain.IsEmpty() && Rel(I) > 0.2) break;
                    Chain.Add(I);
                    if (Chain.Num() > 12) break;
                }
                if (Chain.Num() < 3) { Rels += FString::Printf(TEXT("%d/%d "), Lowest, Chain.Num()); continue; }
                // Hip = top of the chain; knee = the chain bone nearest mid-leg; ankle = the bone
                // below the knee.
                const int32 Hip = Chain.Last();
                const double HipR = Rel(Hip);
                int32 Knee = INDEX_NONE, Ankle = INDEX_NONE;
                for (int32 C = 0; C < Chain.Num() - 1; ++C)
                {
                    const double R = Rel(Chain[C]);
                    if (R > HipR * 0.3 && R < HipR * 0.8 && (Knee == INDEX_NONE || FMath::Abs(R - HipR * 0.5) < FMath::Abs(Rel(Knee) - HipR * 0.5))) Knee = Chain[C];
                }
                if (Knee != INDEX_NONE)
                    for (int32 C = 0; C < Chain.Num(); ++C)
                        if (Ref.GetParentIndex(Chain[C]) == Knee) { Ankle = Chain[C]; break; }
                if (Knee == INDEX_NONE || Ankle == INDEX_NONE || HipR < 0.2 || Ref.GetParentIndex(Knee) != Hip)
                {
                    // Allow one twist/roll bone between hip and knee: use the knee's parent as hip.
                    if (Knee != INDEX_NONE && Ankle != INDEX_NONE && Rel(Ref.GetParentIndex(Knee)) > Rel(Knee) + 0.08)
                    {
                        HipBone[S] = Ref.GetParentIndex(Knee); KneeBone[S] = Knee; FootBone[S] = Ankle;
                        bSideOk = true;
                        continue;
                    }
                    for (int32 I : Chain) Rels += FString::Printf(TEXT("%d@%.2f "), I, Rel(I));
                    Rels += TEXT("| ");
                    continue;
                }
                HipBone[S] = Hip; KneeBone[S] = Knee; FootBone[S] = Ankle;
                bSideOk = true;
            }
            if (!bSideOk)
            {
                Why += FString::Printf(TEXT(" ax%d s%d chains[%s];"), Axis, S, *Rels);
                bAxisOk = false;
                break;
            }
        }
        Out.bValid = bAxisOk && HipBone[0] != HipBone[1];
    }
    bLegsValid = Out.bValid;
    for (int32 S = 0; S < 2; ++S) { Out.Hip[S] = HipBone[S]; Out.Knee[S] = KneeBone[S]; Out.Foot[S] = FootBone[S]; }
    if (bLegsValid)
    {
        Out.Thigh = float((Pos[KneeBone[0]] - Pos[HipBone[0]]).Size());
        Out.Shin = float((Pos[FootBone[0]] - Pos[KneeBone[0]]).Size());
        ThighLength = Out.Thigh; ShinLength = Out.Shin;
        for (int32 S = 0; S < 2; ++S)
        {
            Out.Twins[S].Reset();
            for (int32 I = 0; I < Num; ++I)
                if (I != KneeBone[S] && Ref.GetParentIndex(I) == HipBone[S] && (Pos[I] - Pos[KneeBone[S]]).Size() < 1.0)
                    Out.Twins[S].Add(I);
            KneeTwinBones[S] = Out.Twins[S];
            Out.Children[S].Reset();
            for (int32 I = 0; I < Num; ++I)
                if (I != FootBone[S] && Ref.GetParentIndex(I) == KneeBone[S] && (Pos[I] - Pos[KneeBone[S]]).Size() < 1.0)
                    Out.Children[S].Add(I);
            KneeChildBones[S] = Out.Children[S];
        }
    }
    UE_LOG(LogWowAnim, Log, TEXT("FootIK: %s legs %s (hip %d/%d knee %d/%d ankle %d/%d, knee twins %d/%d, knee helpers %d/%d) size %s%s"), *GetNameSafe(Mesh->GetSkinnedAsset()),
        bLegsValid ? TEXT("found") : TEXT("NOT found"), HipBone[0], HipBone[1], KneeBone[0], KneeBone[1], FootBone[0], FootBone[1],
        KneeTwinBones[0].Num(), KneeTwinBones[1].Num(), KneeChildBones[0].Num(), KneeChildBones[1].Num(),
        *Size.ToString(), bLegsValid ? TEXT("") : *Why);
}

void UWowFootIKInstance::BuildUpperBodyMask()
{
    UpperBodyMask.Reset();
    const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    if (!bLegsValid || !Mesh || !Mesh->GetSkinnedAsset()) return;
    const FReferenceSkeleton& Ref = Mesh->GetSkinnedAsset()->GetRefSkeleton();
    const int32 Num = Ref.GetNum();
    if (HipBone[0] < 0 || HipBone[1] < 0 || HipBone[0] >= Num || HipBone[1] >= Num) return;
    UpperBodyMask.Init(1, Num);
    // Legs: each hip and everything under it.
    for (int32 I = 0; I < Num; ++I)
        for (int32 B = I; B >= 0; B = Ref.GetParentIndex(B))
            if (B == HipBone[0] || B == HipBone[1]) { UpperBodyMask[I] = 0; break; }
    // The chain above the hips (root, pelvis) carries the run bob and facing: keep it.
    for (int32 S = 0; S < 2; ++S)
        for (int32 B = Ref.GetParentIndex(HipBone[S]); B >= 0; B = Ref.GetParentIndex(B)) UpperBodyMask[B] = 0;
}

bool UWowFootIKInstance::PlayUpperBodyOverlay(UAnimSequence* Sequence)
{
    if (!Sequence || Sequence->GetPlayLength() <= 0.f) return false;
    if (!bLegsResolved) ResolveLegs();
    if (UpperBodyMask.IsEmpty()) BuildUpperBodyMask();
    if (UpperBodyMask.IsEmpty()) return false;
    OverlaySequence = Sequence;
    OverlayTime = 0.f;
    OverlayWeight = 0.f;
    bOverlayActive = true;
    return true;
}

void UWowFootIKInstance::ResolveHead()
{
    LookHeadBone = LookNeckBone = INDEX_NONE;
    const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    if (!Mesh || !Mesh->GetSkinnedAsset()) return; // try again once the mesh is set
    bHeadResolved = true;
    // M2 key bone 6 (head) carries the WowFirstPersonHead socket for every built character.
    const FName HeadName = Mesh->GetSocketBoneName(TEXT("WowFirstPersonHead"));
    if (HeadName.IsNone() || HeadName == TEXT("WowFirstPersonHead")) return;
    const int32 Head = Mesh->GetBoneIndex(HeadName);
    if (Head <= 0) return;
    LookHeadBone = Head;
    // r372: the head's forward axis in its own bone space (rest pose); M2 rests are usually unrotated.
    {
        const FReferenceSkeleton& RefSkel = Mesh->GetSkinnedAsset()->GetRefSkeleton();
        FQuat RestCS = FQuat::Identity;
        for (int32 B = Head; B != INDEX_NONE; B = RefSkel.GetParentIndex(B))
            RestCS = RefSkel.GetRefBonePose()[B].GetRotation() * RestCS;
        LookHeadRestCS = RestCS; // the forward axis is derived per frame from the body's forward
        if (const AActor* Owner = Mesh->GetOwner())
        {
            const FVector F = Mesh->GetComponentTransform().InverseTransformVectorNoScale(Owner->GetActorForwardVector()).GetSafeNormal2D();
            // Snap to the nearest axis: the mesh may be caught mid strafe-lean when this runs.
            if (!F.IsNearlyZero())
                LookBodyForwardCS = FMath::Abs(F.X) > FMath::Abs(F.Y) ? FVector(FMath::Sign(F.X), 0.0, 0.0) : FVector(0.0, FMath::Sign(F.Y), 0.0);
        }
        UE_LOG(LogWowAnim, Log, TEXT("Head look on %s: head bone %d, model forward in component space %s"),
            *GetNameSafe(Mesh->GetOwner()), Head, *LookBodyForwardCS.ToString());
    }
    // The neck takes part only when it is a plain neck (head and maybe a jaw/helper under it);
    // a spine bone that also carries the shoulders would swing the arms.
    const FReferenceSkeleton& Ref = Mesh->GetSkinnedAsset()->GetRefSkeleton();
    const int32 Neck = Ref.GetParentIndex(Head);
    if (Neck > 0)
    {
        int32 Children = 0;
        for (int32 B = 0; B < Ref.GetNum(); ++B) if (Ref.GetParentIndex(B) == Neck) ++Children;
        if (Children <= 2) LookNeckBone = Neck;
    }
}

void UWowFootIKInstance::UpdateHeadLook(float DeltaSeconds)
{
    USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    if (!bHeadResolved) ResolveHead();
    float WantYaw = 0.f, WantPitch = 0.f, WantWeight = 0.f;
    // r372 (Stefan: the head looked beside the target, and stayed turned with no target): measure
    // everything against the model's forward in component space as found when the head was
    // resolved (the strafe lean turns the whole mesh, so it stays valid), not an assumed +Y, and
    // let the evaluation turn the animated head onto the wanted direction.
    const FVector BodyForward = LookBodyForwardCS;
    LookHeadForwardLocal = LookHeadRestCS.Inverse().RotateVector(BodyForward).GetSafeNormal();
    if (bLookEnabled && Mesh && LookHeadBone != INDEX_NONE)
    {
        const FVector HeadWorld = Mesh->GetBoneLocation(Mesh->GetBoneName(LookHeadBone));
        const FVector Dir = Mesh->GetComponentTransform().InverseTransformVectorNoScale(LookAtWorld - HeadWorld);
        if (Dir.Size() > 60.0)
        {
            const FVector Flat = Dir.GetSafeNormal2D();
            // Signed angle from the body's forward to the target, about +Z (positive turns +X to +Y).
            const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(
                float(BodyForward.X * Flat.Y - BodyForward.Y * Flat.X), float(FVector::DotProduct(BodyForward, Flat))));
            if (const APawn* PawnOwner = Cast<APawn>(Mesh->GetOwner()); PawnOwner && PawnOwner->IsLocallyControlled())
            {
                static double GNextLookLog = 0.0;
                if (FPlatformTime::Seconds() > GNextLookLog)
                {
                    GNextLookLog = FPlatformTime::Seconds() + 3.0;
                    UE_LOG(LogWowAnim, Log, TEXT("Head look: yaw %.0f pitch %.0f weight %.2f dirCS %s bodyCS %s"),
                        Yaw, LookPitch, LookWeight, *Flat.ToString(), *BodyForward.ToString());
                }
            }
            if (FMath::Abs(Yaw) <= 125.f) // behind the shoulder: look ahead instead
            {
                // r446 (Stefan: the NPC "looks in my direction, not at me"): a tall NPC looking down at a
                // small character next to it needs far more than 30 degrees of pitch, and a target off
                // to the side more than 70 degrees of turn. Neck and head now reach 85 degrees sideways
                // and 55 down / 40 up, and the look keeps full strength out to 85 degrees.
                WantYaw = FMath::Clamp(Yaw, -85.f, 85.f);
                // r296: VR / first person may look further down (own body, the ground at your feet).
                WantPitch = FMath::Clamp(float(FMath::RadiansToDegrees(FMath::Atan2(Dir.Z, Dir.Size2D()))), bLookHeadOnly ? -45.f : -55.f, bLookHeadOnly ? 35.f : 40.f);
                // Fade the look out from 85 to 115 degrees off the body's forward.
                WantWeight = FMath::Clamp((115.f - FMath::Abs(Yaw)) / 30.f, 0.f, 1.f);
            }
        }
    }
    LookYaw = FMath::FInterpTo(LookYaw, WantYaw, DeltaSeconds, 5.f);
    LookPitch = FMath::FInterpTo(LookPitch, WantPitch, DeltaSeconds, 5.f);
    LookWeight = FMath::FInterpTo(LookWeight, WantWeight, DeltaSeconds, 4.f);
    bLookActive = LookHeadBone != INDEX_NONE && LookWeight > 0.01f;
    const FVector FlatLook = FQuat(FVector::UpVector, FMath::DegreesToRadians(LookYaw)).RotateVector(BodyForward);
    const float PitchRad = FMath::DegreesToRadians(LookPitch);
    LookDirCS = (FlatLook * FMath::Cos(PitchRad) + FVector::UpVector * FMath::Sin(PitchRad)).GetSafeNormal();
    LookDeltaCS = FQuat::FindBetweenNormals(BodyForward, LookDirCS); // kept for older readers
}

void UWowFootIKInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    if (!Mesh) return;
    UpdateHeadLook(DeltaSeconds);
    if (!bLegsResolved) ResolveLegs();
    // r451 (Stefan: shaman totems show only a floating top, no totem model): one log of the posed
    // bones of each totem 1.5 s after it appears (component space, cm), to see where the pose goes wrong.
    if (Mesh->GetSkinnedAsset() && Mesh->GetSkinnedAsset()->GetName().Contains(TEXT("Totem")))
    {
        static TMap<TWeakObjectPtr<UWowFootIKInstance>, float> GTotemAge;
        float& Age = GTotemAge.FindOrAdd(this);
        const float Before = Age;
        Age += DeltaSeconds;
        if (Before < 1.5f && Age >= 1.5f)
        {
            FString Line;
            for (int32 Bone = 0; Bone < FMath::Min(Mesh->GetNumBones(), 20); ++Bone)
            {
                const FTransform T = Mesh->GetBoneTransform(Bone, FTransform::Identity);
                Line += FString::Printf(TEXT(" [%d %s p=%s s=%.2f]"), Bone, *Mesh->GetBoneName(Bone).ToString(),
                    *T.GetLocation().ToCompactString(), T.GetScale3D().X);
            }
            UE_LOG(LogWowAnim, Log, TEXT("Totem pose %s anim=%s bounds=%s:%s"), *Mesh->GetSkinnedAsset()->GetName(),
                *GetNameSafe(GetCurrentAsset()), *Mesh->Bounds.BoxExtent.ToCompactString(), *Line);
        }
    }
    if (bFadeActive)
    {
        // r640: the old clip runs on at normal speed while its weight eases out.
        FadeElapsed += DeltaSeconds;
        const float FromLength = FadeFromSequence ? FadeFromSequence->GetPlayLength() : 0.f;
        FadeFromTime += DeltaSeconds;
        if (FromLength > 0.f)
            FadeFromTime = bFadeFromLooping ? FMath::Fmod(FadeFromTime, FromLength) : FMath::Min(FadeFromTime, FromLength - KINDA_SMALL_NUMBER);
        const float Progress = FadeDuration > 0.f ? FMath::Clamp(FadeElapsed / FadeDuration, 0.f, 1.f) : 1.f;
        FadeWeight = 1.f - FMath::SmoothStep(0.f, 1.f, Progress);
        if (Progress >= 1.f || !FadeFromSequence)
        {
            bFadeActive = false;
            FadeWeight = 0.f;
            FadeFromSequence = nullptr;
        }
    }
    if (bOverlayActive)
    {
        const float Length = OverlaySequence ? OverlaySequence->GetPlayLength() : 0.f;
        OverlayTime += DeltaSeconds;
        if (Length <= 0.f || OverlayTime >= Length)
        {
            bOverlayActive = false;
            OverlayWeight = 0.f;
            OverlaySequence = nullptr;
        }
        else
        {
            // Ease in over 0.12 s, out over the last 0.18 s.
            OverlayWeight = FMath::Clamp(FMath::Min(OverlayTime / 0.12f, (Length - OverlayTime) / 0.18f), 0.f, 1.f);
            OverlayTime = FMath::Min(OverlayTime, Length - KINDA_SMALL_NUMBER);
        }
    }
    AActor* Owner = Mesh->GetOwner();
    bool bEnabled = bLegsValid && bHaveAnimFeet && CVarWowFootIK.GetValueOnGameThread() != 0
        && Owner && !Owner->GetAttachParentActor() && Mesh->WasRecentlyRendered(0.3f);
    if (const ACharacter* Character = Cast<ACharacter>(Owner))
        if (const UCharacterMovementComponent* Move = Character->GetCharacterMovement())
            if (Move->IsFalling() || Move->IsSwimming() || Move->IsFlying()) bEnabled = false;
    const bool bPosture = GWowPostureMeshes.Contains(Mesh);
    if (bPosture) bEnabled = false; // r694: seated poses keep their authored height

    float Want[2] = { 0.f, 0.f };
    bool bGrounded = false;
    if (bEnabled && GetWorld())
    {
        const FTransform& Xf = Mesh->GetComponentTransform();
        const double OriginZ = Xf.GetLocation().Z;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(WowFootIK), false, Owner);
        bGrounded = true;
        for (int32 S = 0; S < 2 && bGrounded; ++S)
        {
            const FVector FootWorld = Xf.TransformPosition(AnimFootCS[S]);
            FHitResult Hit;
            if (!GetWorld()->LineTraceSingleByObjectType(Hit, FVector(FootWorld.X, FootWorld.Y, OriginZ + 60.0),
                    FVector(FootWorld.X, FootWorld.Y, OriginZ - 90.0), FCollisionObjectQueryParams(ECC_WorldStatic), Params)
                || (Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(TEXT("WowWaterVolume"))))
            {
                bGrounded = false;
                break;
            }
            double GroundZ = Hit.ImpactPoint.Z;
            // r335: stairs are often a collision ramp under (or over) the drawn steps. When a
            // drawn tread lies close to the ramp under this foot, stand on the tread instead.
            FHitResult Tread;
            if (GetWorld()->LineTraceSingleByObjectType(Tread, FVector(FootWorld.X, FootWorld.Y, OriginZ + 60.0),
                    FVector(FootWorld.X, FootWorld.Y, OriginZ - 90.0), FCollisionObjectQueryParams(ECC_Destructible), Params)
                && FMath::Abs(Tread.ImpactPoint.Z - GroundZ) < 40.0)
                GroundZ = Tread.ImpactPoint.Z;
            Want[S] = FMath::Clamp(float(GroundZ - OriginZ), -50.f, 35.f);
        }
    }
    // Step-up stance (r280, Stefan: a foot on a kerb or patio folded the knee and left the
    // ankle high). A leg raised by s with an upright shin needs its foot ahead of the hip by
    // sqrt(2*A*s - s*s) (A = thigh): the thigh lifts towards horizontal, the knee bends to about
    // 90 degrees and the boot stands flat on the step. Only when the ground there is the same
    // step (no stepping into the air), and not while walking (the cycle moves the feet itself).
    float WantForward[2] = { 0.f, 0.f };
    const float MeshScale = FMath::Max(float(Mesh->GetComponentScale().Z), 0.01f);
    const float Thigh = ThighLength * MeshScale;
    bool bMoving = false;
    if (Owner)
    {
        // NPCs and other players are moved by SetActorLocation (no velocity): use the
        // distance travelled since the last update as well (r288).
        const FVector Here = Owner->GetActorLocation();
        bMoving = Owner->GetVelocity().SizeSquared2D() > 400.0
            || (DeltaSeconds > 0.f && FVector::DistSquared2D(Here, LastOwnerLocation) > FMath::Square(20.0 * DeltaSeconds));
        LastOwnerLocation = Here;
    }
    FVector FacingWorld = Owner ? Owner->GetActorForwardVector().GetSafeNormal2D() : FVector::ZeroVector;
    if (bGrounded && !bMoving && Thigh > 5.f && !FacingWorld.IsNearlyZero() && GetWorld())
    {
        const FTransform& Xf = Mesh->GetComponentTransform();
        const float Lowest = FMath::Min(0.f, FMath::Min(Want[0], Want[1]));
        FCollisionQueryParams Params(SCENE_QUERY_STAT(WowFootStep), false, Owner);
        for (int32 S = 0; S < 2; ++S)
        {
            const float Raise = Want[S] - Lowest;
            if (Raise < 6.f) continue;
            // r425 (Stefan, screenshot after r422: knee and foot still bend oddly - the knee rose above the
            // hip and the thigh arced over like a hose): a foot put up on a step higher than about 60 % of the
            // thigh, or on a tread too short to take the foot under the knee, folds the knee past 90 degrees.
            // Nobody stands like that; such a foot steps back off the edge onto the lower ground instead.
            const float IdealAhead = FMath::Sqrt(FMath::Max(0.f, 2.f * Thigh * FMath::Min(Raise, Thigh) - FMath::Square(FMath::Min(Raise, Thigh))));
            auto StepBack = [&]() -> bool
            {
                const FVector From = Xf.TransformPosition(AnimFootCS[S]);
                const double LowZ = Xf.GetLocation().Z + Lowest;
                for (float Back = 12.f; Back <= 42.f; Back += 6.f)
                {
                    const FVector At = From - FacingWorld * Back;
                    FHitResult Hit;
                    if (GetWorld()->LineTraceSingleByObjectType(Hit, FVector(At.X, At.Y, LowZ + 20.0), FVector(At.X, At.Y, LowZ - 20.0),
                            FCollisionObjectQueryParams(ECC_WorldStatic), Params)
                        && FMath::Abs(Hit.ImpactPoint.Z - LowZ) < 6.0)
                    {
                        WantForward[S] = -Back;
                        Want[S] = float(Hit.ImpactPoint.Z - Xf.GetLocation().Z);
                        return true;
                    }
                }
                return false;
            };
            if (Raise > Thigh * 0.6f && StepBack()) continue;
            const float S2 = FMath::Min(Raise, Thigh * 0.95f);
            // r334 (Stefan's sketch): put the raised foot right under the knee - the thigh swings up
            // around the hip and the shin stays upright - instead of 80 % of that, which bent the knee
            // further back and pinched the thigh. Shorter strides are still tried if the tread is short.
            float Ahead = FMath::Sqrt(FMath::Max(0.f, 2.f * Thigh * S2 - S2 * S2));
            const FVector FootWorld = Xf.TransformPosition(AnimFootCS[S]);
            const double GroundZ = Xf.GetLocation().Z + Want[S];
            // Try the full stride, then shorter ones, keeping the foot on the same tread.
            // r350 (Stefan's sketch: thigh forward, shin straight down): stairs have short treads, and
            // the old 60 % steps fell to a foot near the riser with the shin leaning back, which reads as
            // a bent rubber leg. Shrink in finer steps and let the ankle stand up to 6 cm past the tread
            // check point (the heel is behind it), so the shin stays as upright as the step allows.
            for (int32 Try = 0; Try < 6 && Ahead > 2.f; ++Try, Ahead *= 0.85f)
            {
                const FVector At = FootWorld + FacingWorld * FMath::Max(0.f, Ahead - 6.f);
                FHitResult Hit;
                if (GetWorld()->LineTraceSingleByObjectType(Hit, FVector(At.X, At.Y, GroundZ + 30.0), FVector(At.X, At.Y, GroundZ - 30.0),
                        FCollisionObjectQueryParams(ECC_WorldStatic), Params)
                    && FMath::Abs(Hit.ImpactPoint.Z - GroundZ) < 6.0)
                {
                    WantForward[S] = Ahead;
                    break;
                }
            }
            // r425: a tread too short for the foot to stand under the knee folds the leg: step back instead.
            if (WantForward[S] < IdealAhead * 0.7f && Raise > Thigh * 0.35f) StepBack();
        }
    }
    const float Blend = FMath::Clamp(DeltaSeconds * 12.f, 0.f, 1.f);
    for (int32 S = 0; S < 2; ++S)
    {
        FootForwardWorld[S] = FMath::FInterpTo(FootForwardWorld[S], WantForward[S], DeltaSeconds, 5.f);
        const FVector Shift = FacingWorld * FootForwardWorld[S];
        FootForwardCS[S] = Mesh->GetComponentTransform().InverseTransformVector(Shift);
    }
    for (int32 S = 0; S < 2; ++S) FootOffsetWorld[S] = FMath::Lerp(FootOffsetWorld[S], bGrounded ? Want[S] : 0.f, Blend);
    PelvisWorld = FMath::Lerp(PelvisWorld, bGrounded ? FMath::Min(0.f, FMath::Min(Want[0], Want[1])) : 0.f, Blend);
    IKAlpha = FMath::FInterpTo(IKAlpha, bGrounded ? 1.f : 0.f, DeltaSeconds, 6.f);
    const float Scale = FMath::Max(float(Mesh->GetComponentScale().Z), 0.01f);
    for (int32 S = 0; S < 2; ++S) FootOffsetCS[S] = FootOffsetWorld[S] / Scale;
    PelvisOffsetCS = PelvisWorld / Scale;
    UpCS = Mesh->GetComponentTransform().InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
    if (bPosture) SnapFootIKOff(); // r709: no blend-out while sitting down
}

void UWowFootIKInstance::SnapFootIKOff()
{
    IKAlpha = 0.f;
    PelvisWorld = 0.f;
    PelvisOffsetCS = 0.f;
    for (int32 S = 0; S < 2; ++S)
    {
        FootOffsetWorld[S] = 0.f; FootOffsetCS[S] = 0.f;
        FootForwardWorld[S] = 0.f; FootForwardCS[S] = FVector::ZeroVector;
    }
}
