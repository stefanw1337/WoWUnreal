#include "WowPlayerCharacter.h"
#include "HAL/IConsoleManager.h"
static TAutoConsoleVariable<int32> CVarWowSwimDebug(TEXT("wow.SwimDebug"), 0, TEXT("Log ocean swim bobbing values each frame."));
// First person: the held weapon right in front of the lens needs a far wider near-field blur
// than UE's default cap (2.5% of the screen), otherwise its silhouette keeps a hard edge.
static void WowSetForegroundBlur(bool bEnabled)
{
	static float Default = -1.0f;
	// r456 (Stefan: our character still blurred when the camera looks at it): this started as "already
	// third person", so the third-person value (no foreground blur, r448) was never set at all.
	static int32 Current = -1;
	if (Current == int32(bEnabled)) return;
	if (IConsoleVariable* Radius = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DOF.Kernel.MaxForegroundRadius")))
	{
		if (Default < 0.0f) Default = Radius->GetFloat();
		// r448 (Stefan: "DoF still creates this thick slimy-like barrier around me, and my hair is very
		// protruding"): in third person the focus sits on the scene beyond your character, so the
		// character itself was foreground and got the near-field blur (a smeared halo, hair spread
		// wide). Third person now has no foreground blur: only what lies beyond the focus blurs.
		Radius->Set(bEnabled ? 0.0f : 0.12f, ECVF_SetByCode);
		Current = int32(bEnabled);
	}
}
#include "WowCameraFocusTrace.h"
#include "WowRealmHandshake.h"
#include "WowWaterSedimentComponent.h"
#include "WowWaterMaterial.h"
#include "WowWorldManager.h"
#include "WowTerrainTile.h"
#include "EngineUtils.h"
#include "WowKeyBindings.h"
#include "UnrealClient.h"
#include "WowVRSupport.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "UnrealEngine.h"
#include "WowVRComfort.h"
extern WOWASSETS_API bool GWowSuppressLocalStrafeYaw; // WowAnimationController.cpp
#include "Engine/GameViewportClient.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowM2ParticleComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "WowCharacterBuilder.h"
#include "WowCharacterTexture.h"
#include "WowGameplayController.h"
#include "WowVrPersonalSpace.h"
#include "WowNpcHeadLook.h"
#include "WowPhotoMode.h"
#include "WowAssetCache.h"
#include "WowSessionState.h"
#include "WowAnimationController.h"
#include "WowAudioManager.h"
#include "WowFirstPersonCameraPolicy.h"
#include "WowCharacterInspectionCameraPolicy.h"
#include "WowTerrainMaterial.h"
#include "WowClientPreferences.h"
#include "Components/WidgetComponent.h"
#include "WowWeatherComponent.h"
#include "Coord/WowCoordinate.h"
#include "Mpq/MpqManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MeshComponent.h"
#include "CollisionQueryParams.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "Engine/Scene.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "WowSkyManager.h"
#include "Components/DirectionalLightComponent.h"
#include "WowDuskLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "WowIsometricCamera.h"

namespace
{
const FName HeadShadowProxyTag(TEXT("WowHeadShadowProxy"));

/**
 * First person hides the head bone so the camera never sees the inside of the face, but a
 * hidden bone is hidden for the shadow too (a headless shadow). While the head is hidden the
 * body casts no shadow itself; an invisible copy of the posed body with the head kept casts it.
 */
void UpdateHeadShadowProxy(ACharacter* Self, bool bHeadHidden)
{
    USkeletalMeshComponent* Body = Self ? Self->GetMesh() : nullptr;
    if (!Body) return;
    UPoseableMeshComponent* Proxy = nullptr;
    {
        TInlineComponentArray<UPoseableMeshComponent*> Candidates(Self);
        for (UPoseableMeshComponent* Candidate : Candidates)
            if (Candidate && Candidate->ComponentHasTag(HeadShadowProxyTag)) { Proxy = Candidate; break; }
    }
    if (!bHeadHidden || !Body->GetSkeletalMeshAsset())
    {
        if (Proxy && Proxy->CastShadow)
        {
            Proxy->SetCastShadow(false);
            Proxy->SetVisibility(false);
        }
        if (!Body->CastShadow) Body->SetCastShadow(true);
        return;
    }
    if (!Proxy)
    {
        Proxy = NewObject<UPoseableMeshComponent>(Self, TEXT("FirstPersonHeadShadow"));
        Proxy->ComponentTags.Add(HeadShadowProxyTag);
        Proxy->SetupAttachment(Body);
        Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Proxy->RegisterComponent();
        Self->AddInstanceComponent(Proxy);
    }
    if (Proxy->GetSkinnedAsset() != Body->GetSkinnedAsset())
        Proxy->SetSkinnedAssetAndUpdate(Body->GetSkinnedAsset());
    for (int32 Index = 0; Index < Body->GetNumMaterials(); ++Index)
        if (Proxy->GetMaterial(Index) != Body->GetMaterial(Index)) Proxy->SetMaterial(Index, Body->GetMaterial(Index));
    // Same geosets as the body (the builder keys visibility by section index).
    if (const FSkeletalMeshRenderData* RenderData = Body->GetSkeletalMeshAsset()->GetResourceForRendering())
        if (RenderData->LODRenderData.Num() > 0)
            for (int32 Section = 0; Section < RenderData->LODRenderData[0].RenderSections.Num(); ++Section)
            {
                const bool bShown = Body->IsMaterialSectionShown(Section, 0);
                if (Proxy->IsMaterialSectionShown(Section, 0) != bShown) Proxy->ShowMaterialSection(Section, Section, bShown, 0);
            }
    Proxy->CopyPoseFromSkeletalComponent(Body);
    // CopyPose carries the hidden head's zero scale; the shadow keeps the head.
    const FReferenceSkeleton& Skeleton = Proxy->GetSkinnedAsset()->GetRefSkeleton();
    for (int32 Bone = 0; Bone < Proxy->BoneSpaceTransforms.Num() && Bone < Skeleton.GetNum(); ++Bone)
        if (Body->IsBoneHiddenByName(Skeleton.GetBoneName(Bone)))
            Proxy->BoneSpaceTransforms[Bone].SetScale3D(Skeleton.GetRefBonePose()[Bone].GetScale3D());
    Proxy->RefreshBoneTransforms();
    Proxy->SetVisibility(true);
    Proxy->SetHiddenInGame(true);       // never drawn ...
    Proxy->bCastHiddenShadow = true;    // ... only its shadow
    if (!Proxy->CastShadow) Proxy->SetCastShadow(true);
    if (Body->CastShadow) Body->SetCastShadow(false);
}
}

// r528: set by the offline editor bridge (WowOfflineTestBridge.cpp), offline sessions only.
bool GWowOfflineFly = false;

DEFINE_LOG_CATEGORY_STATIC(LogWowPlayerChar, Log, All);

namespace
{
// WotLK movement units converted to cm: ordinary jump apex ~= 164 cm,
// flight time ~= .825 s. Do not tune jump height by visual preference.
constexpr float WowGameplayGravityScale = 1929.110527f / 980.0f;
constexpr float WowGameplayJumpZVelocity = 795.5547f;
constexpr float WowGameplayAirControl = 0.055f;
constexpr float WowGameplayFallingLateralFriction = 0.10f;
constexpr float WowGameplayBrakingDecelerationFalling = 0.0f;
// Retain a short, pleasant glide after swim input is released, then reliably
// settle below the SwimIdle threshold instead of drifting forever in MOVE_Flying.
constexpr float WowGameplaySwimCoastDeceleration = 220.0f;
// WoW treats a jump from the water surface like a normal ground jump. Reuse
// the exact same launch speed so physics and the authored jump sequence agree.
constexpr float WowGameplayWaterExitJumpZVelocity = WowGameplayJumpZVelocity;
const FName WowFirstPersonHeadSocketName(TEXT("WowFirstPersonHead"));

bool IsIgnoredCameraGroundComponent(const UPrimitiveComponent* Component)
{
	if (!Component)
	{
		return true;
	}

	const FString Name = Component->GetName();
	return Name.Contains(TEXT("TreeTrunkBlocker")) ||
		Name.Contains(TEXT("DoodadBlocker")) ||
		Component->ComponentHasTag(TEXT("WowWaterVolume"));
}

bool SweepCameraObstacle(
	UWorld* World,
	const FVector& Start,
	const FVector& End,
	float ProbeRadius,
	const FCollisionQueryParams& QueryParams,
	FHitResult& OutHit)
{
	if (!World)
	{
		return false;
	}

	static constexpr ECollisionChannel Channels[] =
	{
		ECC_Camera,
		ECC_WorldStatic
	};

	bool bHasHit = false;
	FHitResult BestHit;
	for (ECollisionChannel Channel : Channels)
	{
		FHitResult Hit;
		if (World->SweepSingleByChannel(
			Hit,
			Start,
			End,
			FQuat::Identity,
			Channel,
			FCollisionShape::MakeSphere(ProbeRadius),
			QueryParams) &&
			Hit.bBlockingHit)
		{
			if (!bHasHit || Hit.Time < BestHit.Time)
			{
				BestHit = Hit;
				bHasHit = true;
			}
		}
	}

	if (bHasHit)
	{
		OutHit = BestHit;
	}
	return bHasHit;
}

bool TryFindCameraGroundZ(
	UWorld* World,
	const FVector& CameraLocation,
	const FCollisionQueryParams& QueryParams,
	float& OutGroundZ)
{
	if (!World)
	{
		return false;
	}

	TArray<FHitResult> Hits;
	const FVector Start = CameraLocation + FVector(0.0f, 0.0f, 2200.0f);
	const FVector End = CameraLocation - FVector(0.0f, 0.0f, 7000.0f);
	if (!World->LineTraceMultiByChannel(Hits, Start, End, ECC_WorldStatic, QueryParams) &&
		!World->LineTraceMultiByChannel(Hits, Start, End, ECC_Visibility, QueryParams))
	{
		return false;
	}

	bool bFoundGround = false;
	float BestGroundZ = -FLT_MAX;
	for (const FHitResult& Hit : Hits)
	{
		const UPrimitiveComponent* HitComponent = Hit.GetComponent();
		const bool bReasonableCameraGround =
			Hit.bBlockingHit &&
			Hit.ImpactNormal.Z >= 0.35f &&
			Hit.ImpactPoint.Z <= CameraLocation.Z + 1400.0f &&
			!IsIgnoredCameraGroundComponent(HitComponent);
		if (bReasonableCameraGround && Hit.ImpactPoint.Z > BestGroundZ)
		{
			BestGroundZ = Hit.ImpactPoint.Z;
			bFoundGround = true;
		}
	}

	if (bFoundGround)
	{
		OutGroundZ = BestGroundZ;
	}
	return bFoundGround;
}
}

static UInputAction* CreateInputAction(UObject* Outer, const TCHAR* Name, EInputActionValueType ValueType)
{
	UInputAction* Action = NewObject<UInputAction>(Outer, Name);
	Action->ValueType = ValueType;
	return Action;
}

// r691: per character, the smoothed sideways/back shift of the animated head from the bind pose (body frame).
static TMap<TWeakObjectPtr<const AWowPlayerCharacter>, FVector> GWowSeatedHeadShift;

AWowPlayerCharacter::AWowPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Capsule defaults (roughly human-sized)
	GetCapsuleComponent()->InitCapsuleSize(35.0f, 88.0f);

	// Standard UE5 third person setup
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Movement: WoW-style facing is controlled explicitly by input/camera code,
	// not by Unreal's automatic "face movement direction" behavior.
	UCharacterMovementComponent* Mov = GetCharacterMovement();
	Mov->bOrientRotationToMovement = false;
	Mov->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
	Mov->MaxWalkSpeed = RunSpeed;
	Mov->MaxWalkSpeedCrouched = RunSpeed * WalkSpeedFactor;
	Mov->JumpZVelocity = WowGameplayJumpZVelocity;
	Mov->AirControl = WowGameplayAirControl;
	Mov->GravityScale = WowGameplayGravityScale;
	Mov->FallingLateralFriction = WowGameplayFallingLateralFriction;
	Mov->BrakingDecelerationFalling = WowGameplayBrakingDecelerationFalling;
	Mov->BrakingDecelerationWalking = 2000.0f;
	// About half a second from standing to full run speed: a few walked steps first.
	Mov->MaxAcceleration = 1500.0f;
	Mov->BrakingDecelerationFlying = WowGameplaySwimCoastDeceleration;
	// Keep ordinary WMO stair risers traversable without letting the capsule
	// stair-step up steep tree roots, cliff facets or fence-like geometry.
	// Shattrath's terrace risers stopped the capsule at 75. The server pathing (mmaps) lets
	// units climb ~1 yard, so step a little under that.
	Mov->MaxStepHeight = 95.0f;
	Mov->PerchAdditionalHeight = 20.0f;
	// Bevelled stone step edges have a steep sliver at the lip; the default rounded-base
	// floor test lands on that sliver and refuses the step. A flat base stands on the tread.
	Mov->bUseFlatBaseForFloorChecks = true;
	// 55 degrees made visibly cliff-like terrain walkable.  A 45-degree limit
	// matches the intended WoW separation between slopes and blocking hills.
	Mov->SetWalkableFloorAngle(45.0f);
	Mov->MaxSwimSpeed = RunSpeed * SwimSpeedFactor;
	Mov->MaxFlySpeed = RunSpeed * SwimSpeedFactor;
	Mov->Buoyancy = 1.05f;

	// Spring arm â€” follows controller rotation (standard UE5 3rd person)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 760.0f;
	CameraBoom->TargetOffset = FVector(0.0f, 0.0f, 180.0f);
	CameraBoom->bUsePawnControlRotation = true;
	// Camera collision is resolved in CalcCamera so the boom does not collapse
	// into the character when standing close to a trunk, wall, or terrain lip.
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->ProbeSize = 34.0f;
	CameraBoom->ProbeChannel = ECC_Camera;
	CameraBoom->bEnableCameraLag = false;
	CameraBoom->CameraLagSpeed = 18.0f;
	CameraBoom->CameraLagMaxDistance = 90.0f;

	// Camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->bLockToHmd = false; // the head pose is composed in CalcCamera (ApplyVRHeadPose)

	// No mesh yet â€” character rendering is a later phase

	// Create Enhanced Input actions programmatically
	MoveAction = CreateInputAction(this, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	LookAction = CreateInputAction(this, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	JumpAction = CreateInputAction(this, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	SwimDownAction = CreateInputAction(this, TEXT("IA_SwimDown"), EInputActionValueType::Boolean);
	ZoomAction = CreateInputAction(this, TEXT("IA_Zoom"), EInputActionValueType::Axis1D);
	ToggleWalkAction = CreateInputAction(this, TEXT("IA_ToggleWalk"), EInputActionValueType::Boolean);
	AutoRunAction = CreateInputAction(this, TEXT("IA_AutoRun"), EInputActionValueType::Boolean);

	// Create mapping context and bind keys
	GameplayContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Gameplay"));

    // Keyboard movement is fed by the user's saved binding set.
    // Mouse wheel is routed through the binding profile by the controller.
}

void AWowPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Add input mapping context
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (GameplayContext) Sub->AddMappingContext(GameplayContext, 0);
		}
	}

}

// r642 (Stefan: "the turning is still a bit sharp and sudden"): isometric turns ease out (fast at first,
// settling gently) instead of a constant 720 degrees per second; at least 90 degrees per second.
static float WowIsoSmoothTurnYaw(float Current, float Target, float DeltaSeconds)
{
    const float Delta = FMath::FindDeltaAngleDegrees(Current, Target);
    const float Step = FMath::Max(FMath::Abs(Delta) * (1.0f - FMath::Exp(-9.0f * DeltaSeconds)), 90.0f * DeltaSeconds);
    return FMath::Abs(Delta) <= Step ? Target : Current + FMath::Sign(Delta) * Step;
}
void AWowPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if !UE_BUILD_SHIPPING
    // Opt-in offline render regression: actual movement/overlap detection, no
    // synthetic water plane or prolonged ripple lifetime. Finish at rest.
    static const bool bWaterWakeTest = FParse::Param(FCommandLine::Get(), TEXT("testwaterripples")) &&
        FParse::Param(FCommandLine::Get(), TEXT("offlineworld"));
    FString SwimCase;
    if (FParse::Value(FCommandLine::Get(),TEXT("swimtest="),SwimCase) &&
        FParse::Param(FCommandLine::Get(),TEXT("offlineworld")) && GetWorld())
    {
        static int32 Phase=0;
        static FVector Start;
        static float StartedAt=0;
        static bool Captured=false;
        const float Time=GetWorld()->GetTimeSeconds();
        if(Phase==0 && Time>12 && bWowWaterSwimming)
        {
            Phase=1; StartedAt=Time; Start=GetActorLocation(); SetActorRotation(FRotator::ZeroRotator);
            if(SwimCase==TEXT("ctrl")) FWowKeyBindings::Press(TEXT("RCTRL"));
            if(SwimCase==TEXT("left") || SwimCase==TEXT("diagonal")) FWowKeyBindings::SetCommandHeld(TEXT("STRAFELEFT"),true);
            if(SwimCase==TEXT("right")) FWowKeyBindings::SetCommandHeld(TEXT("STRAFERIGHT"),true);
            if(SwimCase==TEXT("diagonal")) FWowKeyBindings::SetCommandHeld(TEXT("MOVEFORWARD"),true);
            if(SwimCase==TEXT("backward")) FWowKeyBindings::SetCommandHeld(TEXT("MOVEBACKWARD"),true);
            UE_LOG(LogWowPlayerChar,Log,TEXT("[SWIMTEST] start %s pos=%s down=%d"),*SwimCase,*Start.ToString(),FWowKeyBindings::Held(TEXT("SWIMDOWN")));
        }
        if(Phase==1 && !Captured && Time-StartedAt>1.1f)
        {
            FString Shot; FParse::Value(FCommandLine::Get(),TEXT("autoscreenshot="),Shot);
            if(!Shot.IsEmpty()) FScreenshotRequest::RequestScreenshot(Shot,true,false);
            Captured=true;
        }
        if(Phase==1 && Time-StartedAt>1.5f)
        {
            UE_LOG(LogWowPlayerChar,Log,TEXT("[SWIMTEST] finish %s delta=%s submerged=%d swimming=%d state=%d"),
                *SwimCase,*(GetActorLocation()-Start).ToString(),bHeadSubmerged,bWowWaterSwimming,
                AnimationController?static_cast<int32>(AnimationController->GetCurrentState()):-1);
            FWowKeyBindings::ReleaseAll(); Phase=2;
        }
    }
    if (bWaterWakeTest && SwimCase.IsEmpty() && GetWorld())
    {
        const float Time = GetWorld()->GetTimeSeconds();
        if (Time > 12.f && Time < 42.f)
            AddMovementInput(FVector::ForwardVector, FMath::Fmod(Time - 12.f, 8.f) < 4.f ? .2f : -.2f);
    }
#endif

	// Auto-run: inject forward movement each tick
	const auto* SitGameplay = Cast<AWowGameplayController>(GetController());
	if (bIsAutoRunning && !(SitGameplay && SitGameplay->IsMovementHeldBySitting()))
	{
		// r638 (Stefan: "auto-run should move us in the direction of the camera, not the character's"): in the
		// isometric camera auto-run runs up the screen, turning the character smoothly towards it.
		if (FWowIsometricCamera::IsActive())
		{
			const FVector ScreenUp = FWowIsometricCamera::ScreenDirection(FVector2D(0.0f, 1.0f));
			if (!ScreenUp.IsNearlyZero())
			{
				SetActorRotation(FRotator(0.0f, WowIsoSmoothTurnYaw(GetActorRotation().Yaw, ScreenUp.Rotation().Yaw, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f), 0.0f));
				AddMovementInput(ScreenUp.GetSafeNormal(), 1.0f);
			}
		}
		else
		{
		const FVector Forward = FRotationMatrix(FRotator(0.0f, GetActorRotation().Yaw, 0.0f)).GetUnitAxis(EAxis::X);
		AddMovementInput(Forward, 1.0f);
		}
	}

	// Fall tracking
	UCharacterMovementComponent* Mov = GetCharacterMovement();
	if (Mov && Mov->IsFalling())
	{
		if (!bIsFalling)
		{
			bIsFalling = true;
			FallStartZ = GetActorLocation().Z;
		}
	}

	if (bNativeFallbackVisuals && FallbackLeftArm && FallbackRightArm
		&& FallbackLeftLeg && FallbackRightLeg)
	{
		const float GroundSpeed = GetVelocity().Size2D();
		FallbackWalkPhase += DeltaTime * (GroundSpeed > 5.0f ? 9.0f : 2.0f);
		const float Swing = GroundSpeed > 5.0f
			? FMath::Sin(FallbackWalkPhase) * 38.0f
			: FMath::Sin(FallbackWalkPhase) * 2.5f;
		if (Mov && Mov->IsFalling())
		{
			FallbackLeftArm->SetRelativeRotation(FRotator(-55.0f, 0.0f, -22.0f));
			FallbackRightArm->SetRelativeRotation(FRotator(-55.0f, 0.0f, 22.0f));
			FallbackLeftLeg->SetRelativeRotation(FRotator(22.0f, 0.0f, -8.0f));
			FallbackRightLeg->SetRelativeRotation(FRotator(-22.0f, 0.0f, 8.0f));
		}
		else
		{
			FallbackLeftArm->SetRelativeRotation(FRotator(Swing, 0.0f, -22.0f));
			FallbackRightArm->SetRelativeRotation(FRotator(-Swing, 0.0f, 22.0f));
			FallbackLeftLeg->SetRelativeRotation(FRotator(-Swing, 0.0f, -8.0f));
			FallbackRightLeg->SetRelativeRotation(FRotator(Swing, 0.0f, 8.0f));
		}
	}

	UpdateWaterMovementMode(DeltaTime);
	// r528 (Stefan, offline in Stormwind: no way up onto the roofs or the city wall to look at the
	// gatehouse): the offline character editor can switch flying on for the offline world only (the
	// bridge refuses it with a server). Jump rises, the swim-down key sinks; off again, you land.
	if (Mov && Mov->MovementMode != MOVE_None)
	{
		static bool bOfflineFlying = false;
		const bool bWantFly = GWowOfflineFly && !bWowWaterSwimming;
		if (bWantFly)
		{
			if (Mov->MovementMode != MOVE_Flying) Mov->SetMovementMode(MOVE_Flying);
			Mov->MaxFlySpeed = RunSpeed * 3.0f;
			Mov->BrakingDecelerationFlying = 4000.0f;
			bOfflineFlying = true;
		}
		else if (bOfflineFlying)
		{
			bOfflineFlying = false;
			if (!bWowWaterSwimming && Mov->MovementMode == MOVE_Flying) Mov->SetMovementMode(MOVE_Falling);
		}
	}
	// Ice underfoot is slippery (r230, Stefan): on terrain painted with ice the run takes longer
	// to stop and turns drift a little. Only the ground's grip changes: speed never goes above the
	// normal run speed, and a stop slides about a metre, so the server sees normal movement.
	if (Mov)
	{
		static TWeakObjectPtr<AWowWorldManager> WorldManager;
		static double NextIceProbe = 0.0;
		static float IceTarget = 0.f, Ice = 0.f;
		const double NowIce = FPlatformTime::Seconds();
		if (NowIce >= NextIceProbe)
		{
			NextIceProbe = NowIce + 0.2;
			if (!WorldManager.IsValid())
				for (TActorIterator<AWowWorldManager> It(GetWorld()); It; ++It) { WorldManager = *It; break; }
			// Only on the terrain itself (not on a bridge, building floor or rock model above it).
			const UPrimitiveComponent* Floor = Mov->CurrentFloor.HitResult.GetComponent();
			const bool bOnTerrain = Mov->IsMovingOnGround() && Floor && Floor->GetOwner() && Floor->GetOwner()->IsA<AWowTerrainTile>();
			IceTarget = bOnTerrain && WorldManager.IsValid()
				? FMath::Clamp((WorldManager->GetTerrainIceAt(GetActorLocation()) - 0.25f) / 0.5f, 0.f, 1.f) : 0.f;
		}
		Ice = FMath::FInterpTo(Ice, IceTarget, DeltaTime, 4.0f);
		Mov->GroundFriction = FMath::Lerp(8.0f, 0.9f, Ice);
		Mov->BrakingDecelerationWalking = FMath::Lerp(2000.0f, 260.0f, Ice);
	}
	// r576 (Stefan 15.10: "my character walked for like 3 seconds after I let go"): the game thread had no
	// long frames then, so a key release went missing (eaten by a widget or lost to focus) and the
	// movement binding stayed held. Movement bindings are key bindings: while one is held but no key or
	// mouse button at all is physically down for 0.15 s, release the bindings (and log it).
#if PLATFORM_WINDOWS
	if (IsLocallyControlled() && !FWowVRSupport::IsActive()
		&& !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen"))) // automated tests hold bindings without keys
	{
		static double NoKeySince = 0.0;
		const TCHAR* MoveCommands[] = { TEXT("MOVEFORWARD"), TEXT("MOVEBACKWARD"), TEXT("STRAFELEFT"), TEXT("STRAFERIGHT"), TEXT("TURNLEFT"), TEXT("TURNRIGHT") };
		FString HeldCommands;
		for (const TCHAR* Command : MoveCommands)
			if (FWowKeyBindings::Held(Command)) HeldCommands += FString::Printf(TEXT(" %s"), Command);
		bool bAnyPhysicalDown = false;
		if (!HeldCommands.IsEmpty())
			for (int32 VirtualKey = 0x01; VirtualKey <= 0xFE && !bAnyPhysicalDown; ++VirtualKey)
				bAnyPhysicalDown = (::GetAsyncKeyState(VirtualKey) & 0x8000) != 0;
		const double NowKeys = FPlatformTime::Seconds();
		if (HeldCommands.IsEmpty() || bAnyPhysicalDown) NoKeySince = 0.0;
		else if (NoKeySince == 0.0) NoKeySince = NowKeys;
		else if (NowKeys - NoKeySince > 0.15)
		{
			UE_LOG(LogTemp, Warning, TEXT("Wow movement key stuck: no key is down but the bindings still hold%s - released"), *HeldCommands);
			FWowKeyBindings::ReleaseAll();
			NoKeySince = 0.0;
		}
	}
#endif
	// r575 (Stefan 15.10: "he keeps walking after I stopped pressing the button", W and strafe): the
	// game thread showed no long frames after world entry, so log each stop that takes longer than
	// it should (input gone -> standing still), with what the movement had at the time.
	if (Mov && IsLocallyControlled())
	{
		static bool bWasPushing = false, bStopping = false;
		static double StopStart = 0.0;
		static FVector StopFrom = FVector::ZeroVector;
		static float StopSpeed = 0.f, StopMaxFrameMs = 0.f;
		const bool bPushing = !Mov->GetCurrentAcceleration().IsNearlyZero();
		const double NowStop = FPlatformTime::Seconds();
		if (bWasPushing && !bPushing && Mov->IsMovingOnGround())
		{
			bStopping = true; StopStart = NowStop; StopFrom = GetActorLocation();
			StopSpeed = float(Mov->Velocity.Size2D()); StopMaxFrameMs = 0.f;
		}
		if (bPushing) bStopping = false;
		if (bStopping)
		{
			StopMaxFrameMs = FMath::Max(StopMaxFrameMs, DeltaTime * 1000.f);
			const double Elapsed = NowStop - StopStart;
			if (Mov->Velocity.Size2D() < 5.0 || Elapsed > 3.0)
			{
				bStopping = false;
				const double Slide = FVector::Dist2D(StopFrom, GetActorLocation());
				if (Elapsed > 0.3 || Slide > 120.0)
					UE_LOG(LogTemp, Warning, TEXT("Wow movement stop: %.2f s and %.0f cm after the input ended (speed %.0f cm/s, friction %.1f, braking %.0f, longest frame %.0f ms, autorun %d, input %s)"),
						Elapsed, Slide, StopSpeed, Mov->GroundFriction, Mov->BrakingDecelerationWalking, StopMaxFrameMs, bIsAutoRunning ? 1 : 0, *CurrentMoveInput2D.ToString());
			}
		}
		bWasPushing = bPushing;
	}
	UpdateSwimVisuals(DeltaTime);
	UpdateWaterRipples(DeltaTime);

	if (bSwimUpPressed && Mov && (bWowWaterSwimming || Mov->IsFlying()))
	{
		AddMovementInput(FVector::UpVector, 1.0f);
	}
	if (bSwimDownPressed && Mov && (bWowWaterSwimming || Mov->IsFlying()))
	{
		AddMovementInput(FVector::DownVector, 1.0f);
	}
}

namespace
{
// VR first person: a held weapon or shield that swings into the clipped zone right in front
// of the eye (a one-hand hammer while running) showed its inside. Hide it while any part of it
// is that close and bring it back a little further out; everything else is untouched.
TArray<TWeakObjectPtr<USceneComponent>> GVRHiddenGear;

void UpdateVRGearClipping(AActor* Self, const FVector& Eye, float NearPlane, bool bActive, bool bSwinging = false)
{
    TArray<USceneComponent*> Gear;
    if (bActive && Self)
    {
        TArray<AActor*> Owners;
        Self->GetAttachedActors(Owners, true, true);
        Owners.Add(Self);
        for (AActor* Owner : Owners)
        {
            if (!IsValid(Owner)) continue;
            TInlineComponentArray<USceneComponent*> Components(Owner);
            for (USceneComponent* C : Components)
                if (C && C->ComponentHasTag(TEXT("WowHeldWeapon"))) Gear.Add(C);
        }
    }
    for (USceneComponent* C : Gear)
    {
        // Distance from the eye to each mesh part's own (rotated) box: the world-axis box of a
        // diagonal hammer reached the face even while it hung at the hip, so it vanished
        // standing still.
        TArray<USceneComponent*> Parts;
        C->GetChildrenComponents(true, Parts);
        Parts.Add(C);
        float Distance = TNumericLimits<float>::Max();
        for (USceneComponent* Part : Parts)
        {
            const UMeshComponent* Mesh = Cast<UStaticMeshComponent>(Part) ? Cast<UMeshComponent>(Part) : Cast<USkeletalMeshComponent>(Part);
            // GetVisibleFlag, not IsVisible: IsVisible is false once we hid it ourselves, so
            // a weapon hidden by one swing past the face was never measured again and stayed
            // gone for good (Stefan: no hammer or shield in the hands at all, r211-r218).
            if (!Mesh || !Mesh->GetVisibleFlag()) continue;
            const FBox Local = Mesh->CalcBounds(FTransform::Identity).GetBox();
            if (!Local.IsValid) continue;
            const FVector LocalEye = Mesh->GetComponentTransform().InverseTransformPosition(Eye);
            const FVector Scale = Mesh->GetComponentTransform().GetScale3D().GetAbs();
            const FVector Out = (LocalEye - Local.GetClosestPointTo(LocalEye)) * Scale;
            Distance = FMath::Min(Distance, float(Out.Size()));
        }
        const bool bHidden = GVRHiddenGear.Contains(C);
        if (Distance == TNumericLimits<float>::Max())
        {
            // Nothing measurable (sheathed away, no mesh yet): never leave it hidden by us.
            if (bHidden) { C->SetHiddenInGame(false, true); GVRHiddenGear.Remove(C); }
            continue;
        }
        // Hidden only at the moment it would be cut open by the near plane (Stefan likes the
        // hammer swinging up in front while running, just never into the face).
        // r415 (Stefan: no 1H mace swing to be seen in VR): during an attack (or any one-shot
        // animation) the weapon stays visible until it is nearly at the lens - a few frames of a
        // cut-open mace beat a swing that is never there.
        const float HideAt = bSwinging ? 3.0f : NearPlane + 1.0f;
        const float ShowAt = bSwinging ? 6.0f : NearPlane + 4.0f;
        static double NextGearLog = 0.0;
        if (!bHidden && Distance < HideAt)
        {
            C->SetHiddenInGame(true, true);
            GVRHiddenGear.Add(C);
            if (FPlatformTime::Seconds() >= NextGearLog)
            {
                NextGearLog = FPlatformTime::Seconds() + 2.0;
                UE_LOG(LogTemp, Log, TEXT("VR gear: %s hidden at %.1f cm from the eye (near %.1f, swinging %d)"), *C->GetName(), Distance, NearPlane, bSwinging ? 1 : 0);
            }
        }
        else if (bHidden && Distance > ShowAt)
        {
            C->SetHiddenInGame(false, true);
            GVRHiddenGear.Remove(C);
        }
    }
    // Gear we hid that is no longer checked (VR off, third person, item swapped): show again.
    for (int32 I = GVRHiddenGear.Num() - 1; I >= 0; --I)
    {
        USceneComponent* C = GVRHiddenGear[I].Get();
        if (!C) { GVRHiddenGear.RemoveAt(I); continue; }
        if (!Gear.Contains(C)) { C->SetHiddenInGame(false, true); GVRHiddenGear.RemoveAt(I); }
    }
}
}

void AWowPlayerCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (!FollowCamera || !CameraBoom)
	{
		Super::CalcCamera(DeltaTime, OutResult);
		return;
	}

	FollowCamera->GetCameraView(DeltaTime, OutResult);
	// r415: VR personal space - units in melee range are drawn a little further back.
	FWowVrPersonalSpace::Update(Cast<AWowGameplayController>(GetController()), this, DeltaTime, FWowVRSupport::IsActive());
	// r419: the targeted NPC looks at you (last frame's camera tells first person from third).
	if (AWowGameplayController* LookController = Cast<AWowGameplayController>(GetController()))
		FWowNpcHeadLook::Update(LookController, this, LookController->PlayerCameraManager
			? LookController->PlayerCameraManager->GetCameraLocation() : OutResult.Location);

	// Ultrawide (21:9, 32:9, 48:9 triple screens): keep the vertical view of 16:9 and
	// widen the horizontal one (Hor+), as WoW does. UE's default keeps the horizontal FOV,
	// which zooms in and crops the top and bottom of the picture on wide screens.
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint ViewSize = GEngine->GameViewport->Viewport->GetSizeXY();
		if (ViewSize.X > 0 && ViewSize.Y > 0)
		{
			const float Aspect = float(ViewSize.X) / float(ViewSize.Y);
			constexpr float DesignAspect = 16.0f / 9.0f;
			if (Aspect > DesignAspect + 0.01f)
			{
				const float HalfVertical = FMath::Atan(FMath::Tan(FMath::DegreesToRadians(OutResult.FOV * 0.5f)) / DesignAspect);
				OutResult.FOV = FMath::Min(FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(HalfVertical) * Aspect)), 150.0f);
			}
		}
	}

	// Photo mode (hold F12) owns the view: free camera, lens and grading, full body visible.
	if (const AWowGameplayController* PhotoOwner = Cast<AWowGameplayController>(GetController());
		PhotoOwner && PhotoOwner->GetPhotoMode() && PhotoOwner->GetPhotoMode()->IsActive())
	{
		WowSetForegroundBlur(true);
		UpdateFirstPersonVisualState(false, 1000.0f);
		UpdateVRGearClipping(this, FVector::ZeroVector, 0.0f, false);
		PhotoOwner->GetPhotoMode()->ApplyView(OutResult);
		LastRenderedCameraLocation = OutResult.Location;
		bHasRenderedCameraLocation = true;
		return;
	}

    auto ApplyCameraFocus = [this, DeltaTime](FMinimalViewInfo& View)
    {
        LastRenderedCameraLocation = View.Location;
        bHasRenderedCameraLocation = true;
        FWowWaterMaterial::SetUnderwaterView(UnderwaterVisualBlend);
        // Seen through water everything looks ~1/3 bigger and closer (refractive index
        // 1.33): narrow the view a little while the camera is under the surface.
        View.FOV *= FMath::Lerp(1.0f, 0.82f, FMath::Clamp(UnderwaterVisualBlend, 0.0f, 1.0f));
        if (!GetWorld()) return;
        // In a headset depth of field follows the same option, gentler (the eyes focus
        // themselves): mainly so the weapon swinging right in front of you is not razor sharp.
        const bool bVRView = FWowVRSupport::IsActive();
        if (bVRView && !FWowVRComfort::GetDepthOfField()) return; // comfort option
        // Below this zoom the body is hidden (UpdateFirstPersonVisualState): nothing to focus on.
        constexpr float PortraitMinZoom = 80.0f;
        // Close third-person zoom is a portrait: the body stays sharp and the background
        // blurs, even with the depth of field option off (that option is for the wide view).
        const bool bCloseUpPortrait = bUseCharacterCameraFraming && CameraBoom
            && CameraBoom->TargetArmLength > PortraitMinZoom
            && FWowCharacterInspectionCameraPolicy::ShouldFocusCharacter(CameraBoom->TargetArmLength);
        if (!FWowClientPreferences::IsDepthOfFieldEnabled() && !bCloseUpPortrait) return;
        FHitResult FocusHit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(WowCameraFocus), true, this);
        TArray<AActor*> Attached;
        GetAttachedActors(Attached, true, true);
        Query.AddIgnoredActors(Attached);
        // Several rays around (and a little above) the centre; the median depth wins, so a
        // single low crate, campfire or rail edge in front of the view no longer pulls the
        // focus to itself. Very near hits in first person (own gear, a doorframe edge) are skipped.
        const FRotationMatrix ViewAxes(View.Rotation);
        const FVector Fwd = ViewAxes.GetUnitAxis(EAxis::X), Rt = ViewAxes.GetUnitAxis(EAxis::Y), Upv = ViewAxes.GetUnitAxis(EAxis::Z);
        const bool bFirstPersonView = CameraBoom && CameraBoom->TargetArmLength <= 20.0f;
        const float MinHit = bFirstPersonView ? 60.0f : 10.0f;
        TArray<float, TInlineAllocator<5>> Depths;
        TArray<FHitResult, TInlineAllocator<5>> Hits;
        TArray<FVector, TInlineAllocator<5>> RayDirs; // r607
        const FVector2D Rays[] = {{0,0},{-.04,0},{.04,0},{0,.05},{0,.10}};
        for (const FVector2D& Ray : Rays)
        {
            const FVector Dir = (Fwd + Rt*Ray.X + Upv*Ray.Y).GetSafeNormal();
            const FVector RayEnd = View.Location + Dir * 50000.0f;
            FHitResult RayHit; float Depth = 50000.0f;
            bool bRay = WowCameraFocus::Trace(GetWorld(), RayHit, View.Location, RayEnd, ECC_Visibility, Query);
            // Authored tree/prop blockers deliberately ignore Visibility for unit picking.
            // Camera collision still represents their solid surface and must participate in focus.
            FHitResult SolidHit;
            if (WowCameraFocus::Trace(GetWorld(), SolidHit, View.Location, RayEnd, ECC_Camera, Query)
                && (!bRay || SolidHit.Distance < RayHit.Distance)) { RayHit = SolidHit; bRay = true; }
            if (bRay && RayHit.Distance >= MinHit) Depth = RayHit.Distance * FVector::DotProduct(Dir, Fwd);
            Depths.Add(Depth); Hits.Add(RayHit); RayDirs.Add(Dir);
        }
        TArray<float, TInlineAllocator<5>> Sorted = Depths; Sorted.Sort();
        const float MedianDepth = Sorted[Sorted.Num()/2];
        const int32 MedianIndex = Depths.IndexOfByKey(MedianDepth);
        FocusHit = Hits[MedianIndex];
        bool bHit = MedianDepth < 49999.0f;
        if (bHit) FocusHit.Distance = MedianDepth;
        float Target = WowCameraFocus::SceneDistance(bHit, FocusHit.Distance, CameraFocusDistance);
        // r607 (Stefan: walking uphill, the NPC ahead is out of focus - "make DoF follow the terrain
        // more"): on a slope the centre of the screen lands on the grass a few metres ahead, so that
        // became the focus and everything over the crest blurred. When the focus point is ground seen
        // at a grazing angle (under ~27 degrees), look higher up the screen for the first thing that is
        // not more of the same slope - the crest, a building, a tree line, the NPC standing in front of
        // them - and focus there. The sharp zone is also widened over the slope (SlopeOpen), so the
        // ground leading up to it stays reasonably sharp too: the focus "lies along" the terrain.
        float SlopeOpen = 0.0f;
        if (bHit && MedianIndex != INDEX_NONE && !FWowClientPreferences::GetWorldOption(TEXT("manualdof")))
        {
            auto Grazing = [](const FHitResult& H, const FVector& Dir)
            { return H.ImpactNormal.Z > 0.6f ? FMath::Abs(FVector::DotProduct(Dir, H.ImpactNormal)) : 1.0f; };
            const float Graze = Grazing(Hits[MedianIndex], RayDirs[MedianIndex]);
            if (Graze < 0.45f && MedianDepth < 4000.0f)
            {
                float CrestDepth = MedianDepth;
                const float Heights[] = {.12f, .20f, .28f, .36f, .44f, .52f};
                for (const float Y : Heights)
                {
                    const FVector Dir = (Fwd + Upv * Y).GetSafeNormal();
                    const FVector RayEnd = View.Location + Dir * 50000.0f;
                    FHitResult Up; bool bUp = WowCameraFocus::Trace(GetWorld(), Up, View.Location, RayEnd, ECC_Visibility, Query);
                    FHitResult Solid;
                    if (WowCameraFocus::Trace(GetWorld(), Solid, View.Location, RayEnd, ECC_Camera, Query)
                        && (!bUp || Solid.Distance < Up.Distance)) { Up = Solid; bUp = true; }
                    const float UpDepth = bUp ? Up.Distance * FVector::DotProduct(Dir, Fwd) : 50000.0f;
                    if (bUp && Grazing(Up, Dir) < 0.45f && UpDepth < CrestDepth * 1.8f) { CrestDepth = FMath::Max(CrestDepth, UpDepth); continue; }
                    // Sky right over the crest: the crest itself is the subject.
                    Target = WowCameraFocus::SceneDistance(true, bUp && UpDepth < 8000.0f ? UpDepth : CrestDepth, CameraFocusDistance);
                    SlopeOpen = FMath::Clamp((0.45f - Graze) / 0.25f, 0.0f, 1.0f);
                    break;
                }
            }
        }
        // Imported characters have no mesh collision. Their oversized picking
        // capsules cannot represent focus, particularly for short races/corpses.
        const AWowGameplayController* Gameplay = Cast<AWowGameplayController>(GetController());
        const float Zoom = CameraBoom ? CameraBoom->TargetArmLength : 760.0f;
        // Only deliberate close zoom selects the body. Requested arm length is
        // unchanged when terrain pushes the rendered camera towards the player.
        const bool bInspectCharacter = bUseCharacterCameraFraming && Zoom > PortraitMinZoom
            && FWowCharacterInspectionCameraPolicy::ShouldFocusCharacter(Zoom);
        const bool bFocusUnit = !bInspectCharacter && Gameplay && Gameplay->FindCameraFocusUnit(View,
            bHit ? FocusHit.Distance : 50000.0f, Target);
        if (bInspectCharacter)
        {
            // Explicit close third-person zoom inspects the body, even when its
            // face sits below screen centre. A wall pushing the camera is not zoom.
            const FVector Chest = GetActorLocation() + FVector(0, 0, CharacterInspectionHeight);
            const float BodyDepth = FVector::DotProduct(Chest - View.Location, View.Rotation.Vector());
            if (BodyDepth > 0.0f) Target = FMath::Max(10.0f, BodyDepth);
        }
        const bool bManualFocus = FWowClientPreferences::GetWorldOption(TEXT("manualdof"));
        if (bManualFocus) Target = FWowClientPreferences::GetFocusTestDistance() * 100.0f;
        CameraFocusDistance = FWowCharacterInspectionCameraPolicy::FocusDistance(
            CameraFocusDistance, Target, DeltaTime, bInspectCharacter && !bManualFocus);
        {
            static double NextFocusLog = 0;
            const double LogNow = FPlatformTime::Seconds();
            if (LogNow >= NextFocusLog)
            {
                NextFocusLog = LogNow + 3.0;
                UE_LOG(LogTemp, Log, TEXT("Camera focus: %.0f cm (scene %.0f, unit=%d, hit=%s)"), CameraFocusDistance,
                    bHit ? FocusHit.Distance : -1.f, bFocusUnit ? 1 : 0,
                    FocusHit.GetComponent() ? *FocusHit.GetComponent()->GetReadableName() : TEXT("none"));
            }
        }
        auto& PP = View.PostProcessSettings;
        // Preserve the existing underwater grade while giving focus full weight.
        const float Blend = View.PostProcessBlendWeight;
        PP.SceneColorTint = FMath::Lerp(FLinearColor::White, PP.SceneColorTint, Blend);
        PP.ColorSaturation = FMath::Lerp(FVector4(1,1,1,1), PP.ColorSaturation, Blend);
        PP.ColorContrast = FMath::Lerp(FVector4(1,1,1,1), PP.ColorContrast, Blend);
        PP.ColorGamma = FMath::Lerp(FVector4(1,1,1,1), PP.ColorGamma, Blend);
        PP.VignetteIntensity = FMath::Lerp(0.4f, PP.VignetteIntensity, Blend);
        PP.bOverride_DepthOfFieldFocalDistance = true;
        // Looking down at one's own body is an inspection view. Fade the effect
        // away there, rather than focusing through the ignored body onto the ground.
        const FVector ToBody = GetActorLocation() - View.Location;
        const bool bNearOwnBody = !bFocusUnit && Zoom <= 20.0f && ToBody.SizeSquared() < FMath::Square(400.0f);
        const float DownPitch = -View.Rotation.GetNormalized().Pitch;
        const float Inspect = !bManualFocus && bNearOwnBody ? FMath::Clamp((DownPitch - 35.0f) / 20.0f, 0.0f, 1.0f) : 0.0f;
        SelfViewFocusFade = FMath::FInterpTo(SelfViewFocusFade, Inspect, DeltaTime, 10.0f);
        const float Strength = FWowClientPreferences::GetDepthOfFieldStrength();
        // Close portraits need enough depth to keep the face and torso readable.
        // Retain the chosen strength, but avoid a sub-f/1 near-focus razor plane.
        const float BaseAperture = bInspectCharacter ? FMath::Max(2.0f, 2.8f / Strength) : 1.4f / Strength;
        const float NearSubject = bFocusUnit ? FMath::Clamp((500.0f-Target)/350.0f,0.0f,1.0f) : 0.0f;
        const float PortraitAperture = FMath::Lerp(BaseAperture,FMath::Max(BaseAperture,2.8f / FMath::Sqrt(Strength)),NearSubject);
        // Test control uses aperture to widen the sharp region on both sides of focus.
        // This is a relative width, not a promise of exact metre boundaries.
        const float TestAperture = FMath::Lerp(1.0f, 32.0f, FWowClientPreferences::GetFocusTestWidth());
        float Aperture = FMath::Lerp(bManualFocus ? TestAperture : PortraitAperture, 5.6f, UnderwaterVisualBlend);
        // r607: over a slope the sharp zone widens (higher f-number), eased so it does not pump.
        static float GSlopeOpen = 0.0f;
        GSlopeOpen = FMath::FInterpTo(GSlopeOpen, (bManualFocus || bInspectCharacter) ? 0.0f : SlopeOpen, DeltaTime, 3.0f);
        Aperture *= FMath::Lerp(1.0f, 2.4f, GSlopeOpen);
        if (bVRView) Aperture = FMath::Max(Aperture, 4.0f);
        PP.DepthOfFieldFocalDistance = SelfViewFocusFade > .99f ? 0.0f : CameraFocusDistance;
        PP.bOverride_DepthOfFieldFstop = true;
        PP.DepthOfFieldFstop = FMath::Lerp(Aperture, 32.0f, SelfViewFocusFade);
        PP.bOverride_DepthOfFieldSensorWidth = true;
        PP.DepthOfFieldSensorWidth = 54.0f;
        View.PostProcessBlendWeight = 1.0f;
#if !UE_BUILD_SHIPPING
        if (FParse::Param(FCommandLine::Get(), TEXT("testdof")) && !bFocusTestLogged && GetWorld()->GetTimeSeconds() > 15.0f)
        {
            bFocusTestLogged = true;
            UE_LOG(LogTemp, Log, TEXT("[FOCUSTEST] hit=%d focus=%.1f aperture=%.1f weight=%.1f"),
                bHit, CameraFocusDistance, PP.DepthOfFieldFstop, View.PostProcessBlendWeight);
        }
#endif
    };

	// r610: experimental isometric camera (Options > Experimental): high fixed angle, middle mouse
	// turns it, overhead roofs and ceilings are cut away around the player.
	if (FWowIsometricCamera::IsActive())
	{
		FWowIsometricCamera::ApplyView(this, OutResult, DeltaTime);
		WowSetForegroundBlur(false); // r618: no depth-of-field blur in the isometric view
		UpdateFirstPersonVisualState(false, 4000.0f);
		UpdateVRGearClipping(this, FVector::ZeroVector, 0.0f, false);
		GWowSuppressLocalStrafeYaw = false;
		// r618 (Stefan: "DoF also needs to be off for this automatically"): no depth of field in the
		// isometric view (focal distance 0 switches it off).
		OutResult.PostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;
		OutResult.PostProcessSettings.DepthOfFieldFocalDistance = 0.0f;
		OutResult.PostProcessSettings.bOverride_DepthOfFieldFstop = true;
		OutResult.PostProcessSettings.DepthOfFieldFstop = 32.0f;
		OutResult.PostProcessBlendWeight = 1.0f;
		return;
	}
	FWowIsometricCamera::ClearCutaway();

	const AController* OwningController = GetController();
	FRotator ViewRotation = OwningController
		? OwningController->GetControlRotation()
		: FRotator(CameraPitch, GetActorRotation().Yaw, 0.0f);

#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("testwaterripples")) &&
        FParse::Param(FCommandLine::Get(), TEXT("offlineworld")))
    {
        FParse::Value(FCommandLine::Get(), TEXT("-camerapitch="), ViewRotation.Pitch);
        FParse::Value(FCommandLine::Get(), TEXT("-camerayaw="), ViewRotation.Yaw);
    }
#endif
	// A realm running mod-wowunreal can lock first person (CAPS flag, r259): zooming out is ignored.
	if (FWowRealmHandshake::Get().HasFlag(EWowRealmCapsFlag::FirstPersonLock) && CameraBoom->TargetArmLength > 0.0f)
	{
		CameraBoom->TargetArmLength = 0.0f;
		UpdateCameraOffsetsForDistance();
	}
	// r509: the close-zoom pivot and the eye follow the animated head below its bind-pose height
	// (a troll's hunch, sitting), smoothed so the run cycle does not bob the camera.
	if (bHaveRefHeadHeight && GetMesh() && GetMesh()->DoesSocketExist(WowFirstPersonHeadSocketName))
	{
		const FVector AnimatedHead = GetMesh()->GetSocketLocation(WowFirstPersonHeadSocketName);
		if (!AnimatedHead.ContainsNaN())
		{
			const float Drop = FMath::Clamp(float(AnimatedHead.Z - GetActorLocation().Z) - RefHeadHeight,
				-0.45f * FMath::Max(CameraModelHeight, 100.0f), 0.0f);
			SmoothedHeadDrop = FMath::FInterpTo(SmoothedHeadDrop, Drop, DeltaTime, 3.0f);
			// r691 (Stefan: "camera also goes into his chest when he sits down - might be on all races?"): the pivot
			// only followed the head down, not where it goes sideways or back as the body sits, so the close
			// camera stayed over the standing head's spot - inside the seated chest. It follows the head's shift
			// from the bind pose in the body's frame too (smoothed like the drop, at most 60 cm).
			{
				FVector RefHead = AnimatedHead;
				const int32 HeadBone = GetMesh()->GetBoneIndex(GetMesh()->GetSocketBoneName(WowFirstPersonHeadSocketName));
				if (HeadBone != INDEX_NONE && GetMesh()->GetSkinnedAsset())
				{
					const FReferenceSkeleton& Ref = GetMesh()->GetSkinnedAsset()->GetRefSkeleton();
					FTransform Bind = FTransform::Identity;
					for (int32 Bone = HeadBone; Bone != INDEX_NONE && Bone < Ref.GetRefBonePose().Num(); Bone = Ref.GetParentIndex(Bone))
						Bind = Bind * Ref.GetRefBonePose()[Bone];
					RefHead = GetMesh()->GetComponentTransform().TransformPosition(Bind.GetLocation());
				}
				const FTransform Body = GetActorTransform();
				FVector Shift = Body.InverseTransformVectorNoScale(AnimatedHead - RefHead); Shift.Z = 0.0;
				Shift = Shift.GetClampedToMaxSize(60.0);
				FVector& Smoothed = GWowSeatedHeadShift.FindOrAdd(this);
				Smoothed = FMath::VInterpTo(Smoothed, Shift, DeltaTime, 3.0f);
			}
			FirstPersonEyeHeight = BaseFirstPersonEyeHeight + SmoothedHeadDrop;
			CharacterInspectionHeight = BaseCharacterInspectionHeight + SmoothedHeadDrop;
			UpdateCameraOffsetsForDistance();
		}
	}
	const float DesiredDistance = FMath::Clamp(CameraBoom->TargetArmLength, MinCameraDistance, MaxCameraDistance);
	// VR "first person only" (default): the headset view never orbits; the zoom is kept and
	// comes back when VR is switched off.
	const bool bVRFirstPerson = FWowVRSupport::IsActive() && FWowClientPreferences::GetWorldOption(TEXT("vrfirstperson"));
	const bool bFirstPerson = DesiredDistance <= 20.0f || bVRFirstPerson;
	// First person already rides the swell through the animated eye bone (mesh offset).
	FVector Pivot = GetActorLocation() + CameraBoom->TargetOffset +
		FVector(0.0f, 0.0f, bFirstPerson ? 0.0f : SwimWaveOffset * 0.85f);
	// r691: close in, the pivot follows the head's sideways/back shift (full at the eye, none from 3 m out).
	if (const FVector* HeadShift = GWowSeatedHeadShift.Find(this))
		Pivot += GetActorTransform().TransformVectorNoScale(*HeadShift) * (1.0f - FMath::Clamp((DesiredDistance - 20.0f) / 280.0f, 0.0f, 1.0f));

	WowSetForegroundBlur(!bFirstPerson);
	if (bFirstPerson)
	{
        // Also constrain an already-orbited camera on the frame it enters first
        // person, even before another mouse delta arrives.
        ViewRotation.Yaw=FWowFirstPersonCameraPolicy::ClampHeadYaw(ViewRotation.Yaw,GetActorRotation().Yaw);
		UpdateFirstPersonVisualState(true, DesiredDistance);
		// The eye turns with the head about the neck (r240): it sat out in front of the body's
		// facing however far the head turned, so a seated gnome looking down and to the side
		// looked back at the open collar of its own (hidden) head. Both the nose offset and the
		// look-down lean now follow the head's yaw, which keeps the collar behind the eye.
		const FVector HeadForward = FRotator(0.0f, ViewRotation.Yaw, 0.0f).Vector();
		FVector AnimatedEyeLocation;
		OutResult.Location = TryGetAnimatedFirstPersonEyeLocation(AnimatedEyeLocation)
			? AnimatedEyeLocation + (HeadForward - GetActorForwardVector()) * FirstPersonForwardOffset
			: Pivot + HeadForward * FirstPersonForwardOffset;
		// In VR the head (not the mouse) looks down, so the screen look-down offset does not apply.
		if (FWowClientPreferences::IsFirstPersonImmersionEnabled() && !FWowVRSupport::IsActive())
		{
			const FWowFirstPersonLookDownOffset LookDownOffset =
				FWowFirstPersonCameraPolicy::ResolveLookDownOffset(ViewRotation.Pitch);
			// r691: seated (the head 25-45 cm below standing) the chest is right under the eye: no lean into it.
			const float Seated = FMath::Clamp((-SmoothedHeadDrop - 25.0f) / 20.0f, 0.0f, 1.0f);
			OutResult.Location += (HeadForward * LookDownOffset.Forward +
				FVector::UpVector * LookDownOffset.Up) * (1.0f - Seated);
		}
        // Animated eyes and look-down offsets can extend past the movement
        // capsule. Resolve the final eye position, not just the camera boom.
        // Start inside the capsule, independently of last frame's camera, so
        // walking against a wall cannot accumulate a position beyond it.
        constexpr float EyeProbeRadius = 12.0f;
        FVector EyeAnchor = GetActorLocation();
        const float AnchorHeight = FMath::Max(0.0f,
            GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - EyeProbeRadius);
        EyeAnchor.Z += FMath::Clamp(OutResult.Location.Z - EyeAnchor.Z,
            -AnchorHeight, AnchorHeight);
        FCollisionQueryParams EyeQuery(SCENE_QUERY_STAT(WowFirstPersonCamera), true, this);
        TArray<AActor*> AttachedActors;
        GetAttachedActors(AttachedActors, true, true);
        EyeQuery.AddIgnoredActors(AttachedActors);
        EyeQuery.bFindInitialOverlaps = true;
        FHitResult EyeHit;
        if (SweepCameraObstacle(GetWorld(), EyeAnchor, OutResult.Location,
            EyeProbeRadius, EyeQuery, EyeHit))
        {
            // Never accept the requested point on the far side of a blocker.
            // An overlapping anchor falls back inside the character capsule.
            OutResult.Location = EyeHit.bStartPenetrating
                ? EyeAnchor : EyeHit.Location + EyeHit.Normal * 2.0f;
        }
		OutResult.Rotation = ViewRotation;
		OutResult.PerspectiveNearClipPlane =
			FWowFirstPersonCameraPolicy::PerspectiveNearClipPlane;
        GWowSuppressLocalStrafeYaw = false; // r211: legs turn with strafing; the eye is compensated
        if (!FWowVRSupport::IsActive() && GetCharacterMovement()) GetCharacterMovement()->MaxAcceleration = 1500.0f;
        ApplyVRHeadPose(OutResult, true, DeltaTime);
        // In a headset the eye sits over the neck: the animated chest and shoulders sway
        // into that spot while moving. Clip the nearest 18 cm, as the own head would.
        // While walking/running/strafing the shoulders and chest swing closer still: clip
        // up to 30 cm then (nothing else is that close to the eye while moving).
        static float VRNearPlane = 18.0f;
        {
            const float Moving = FMath::Clamp(float(GetVelocity().Size2D()) / 150.0f, 0.0f, 1.0f);
            // Only while looking ahead: looking down while running must still show knees,
            // legs and anything around the body (Lightning Shield orbs).
            const float LookingAhead = FMath::Clamp((OutResult.Rotation.Pitch + 40.0f) / 20.0f, 0.0f, 1.0f);
            VRNearPlane = FMath::FInterpTo(VRNearPlane, FMath::Lerp(18.0f, 30.0f, Moving * LookingAhead), DeltaTime, 6.0f);
        }
        if (FWowVRSupport::IsActive()) OutResult.PerspectiveNearClipPlane = VRNearPlane;
        {
            // Stereo projection uses the global near plane, not the view's own value.
            static float DesktopNearPlane = -1.0f;
            if (DesktopNearPlane < 0.0f) DesktopNearPlane = GNearClippingPlane;
            const float WantNear = FWowVRSupport::IsActive() ? VRNearPlane : DesktopNearPlane;
            if (!FMath::IsNearlyEqual(GNearClippingPlane, WantNear)) GNearClippingPlane = WantNear;
        }
        UpdateVRGearClipping(this, OutResult.Location, VRNearPlane, FWowVRSupport::IsActive(),
            AnimationController && AnimationController->IsPlayingOneShot());
        ApplyCameraFocus(OutResult);
        if (!FWowVRSupport::IsActive())
        {
            // Lens flare from the sun (r289, Stefan: the sun should be bright enough for real
            // lens glare). The engine default only flares above luminance 8 after exposure, which
            // the sun disc never reached; flare from 3 so the sun (and very bright fires) ghost.
            OutResult.PostProcessSettings.bOverride_LensFlareIntensity = true;
            // r353 (Stefan: hexagon ghosts from the moon at night): WoW's glare belongs to the sun.
            // Fade the image-based flare out with the dusk-lamp night curve, so the moon and lamps don't ghost.
            static TWeakObjectPtr<AWowSkyManager> FlareSky;
            static double NextFlareSkyLookup = 0.0;
            if (!FlareSky.IsValid() && FPlatformTime::Seconds() >= NextFlareSkyLookup)
            {
                NextFlareSkyLookup = FPlatformTime::Seconds() + 1.0;
                FlareSky = Cast<AWowSkyManager>(UGameplayStatics::GetActorOfClass(GetWorld(), AWowSkyManager::StaticClass()));
            }
            const float FlareNight = FlareSky.IsValid() ? UWowDuskLightComponent::NightFactor(FlareSky->GetTimeOfDay()) : 0.0f;
            // Stefan: the big hexagon ghosts read as cartoon. Real camera ghosts are faint and small: keep
            // a subtle trace of them and let the soft bloom halo around the sun carry the glare.
            // r524 (Stefan: "even our torch makes lens flare", and the sun itself had none): the flare is
            // image based, so at threshold 6 a torch-lit floor ghosted while the sun, dimmed behind cloud,
            // did not. This camera setting also replaced the sky's r513 flare (threshold 20). Flare only
            // while the sun is in view (fades in from ~45 degrees off the view axis), from very bright
            // pixels only, and only with the "Sun rays and lens flare" option on.
            static TWeakObjectPtr<UDirectionalLightComponent> FlareSun;
            if (FlareSky.IsValid() && !FlareSun.IsValid())
            {
                TInlineComponentArray<UDirectionalLightComponent*> SkyLights(FlareSky.Get());
                for (UDirectionalLightComponent* Light : SkyLights)
                    if (Light && Light->GetFName() == TEXT("SunLight")) { FlareSun = Light; break; }
            }
            float SunInView = 0.0f;
            if (FlareSun.IsValid() && FWowClientPreferences::GetWorldOption(TEXT("sunrays")))
            {
                const FVector ToSun = -FlareSun->GetForwardVector();
                const FVector ViewDir = OutResult.Rotation.Vector();
                if (ToSun.Z > 0.0)
                    SunInView = FMath::SmoothStep(0.70f, 0.92f, float(FVector::DotProduct(ViewDir, ToSun)))
                        * FMath::Clamp(float(ToSun.Z) / 0.05f, 0.0f, 1.0f);
            }
            OutResult.PostProcessSettings.LensFlareIntensity = 0.45f * SunInView * (1.0f - FlareNight);
            OutResult.PostProcessSettings.bOverride_LensFlareThreshold = true;
            OutResult.PostProcessSettings.LensFlareThreshold = 20.0f;
            OutResult.PostProcessSettings.bOverride_LensFlareBokehSize = true;
            OutResult.PostProcessSettings.LensFlareBokehSize = 2.0f;
            OutResult.PostProcessSettings.bOverride_LensFlareTint = true;
            OutResult.PostProcessSettings.LensFlareTint = FLinearColor(1.0f, 0.9f, 0.78f, 0.6f);
            OutResult.PostProcessBlendWeight = 1.0f;
        }
        if (FWowVRSupport::IsActive())
        {
            // Comfort: start/stop acceleration, and a dark vignette while moving or turning.
            if (UCharacterMovementComponent* ComfortMove = GetCharacterMovement())
                ComfortMove->MaxAcceleration = 1500.0f * FWowVRComfort::GetAcceleration();
            static float LastComfortYaw = 0.0f;
            static float ComfortMotion = 0.0f;
            const float YawRate = DeltaTime > 0.0f ? FMath::Abs(FMath::FindDeltaAngleDegrees(LastComfortYaw, GetActorRotation().Yaw)) / DeltaTime : 0.0f;
            LastComfortYaw = GetActorRotation().Yaw;
            const float WantMotion = FMath::Max(FMath::Clamp(float(GetVelocity().Size2D()) / 300.0f, 0.0f, 1.0f),
                FMath::Clamp(YawRate / 60.0f, 0.0f, 1.0f));
            ComfortMotion = FMath::FInterpTo(ComfortMotion, WantMotion, DeltaTime, 6.0f);
            const float Vignette = FWowVRComfort::GetVignette() * ComfortMotion;
            if (Vignette > 0.01f)
            {
                OutResult.PostProcessSettings.bOverride_VignetteIntensity = true;
                OutResult.PostProcessSettings.VignetteIntensity = FMath::Max(OutResult.PostProcessSettings.VignetteIntensity, 1.6f * Vignette);
                OutResult.PostProcessBlendWeight = 1.0f;
            }
            // VR looks down at the own body: keep every body part drawn for the owner (only
            // the head bone stays hidden) and report what is drawn, since it was missing.
            static double NextBodyLog = 0;
            const double BodyNow = FPlatformTime::Seconds();
            TInlineComponentArray<USkeletalMeshComponent*> BodyMeshes(this);
            for (USkeletalMeshComponent* BodyMesh : BodyMeshes)
            {
                if (!BodyMesh || BodyMesh->ComponentHasTag(TEXT("WowHeadShadowProxy"))) continue;
                if (BodyMesh->bOwnerNoSee) BodyMesh->SetOwnerNoSee(false);
                if (BodyNow >= NextBodyLog)
                    UE_LOG(LogWowPlayerChar, Log, TEXT("VR body: %s visible=%d hiddenInGame=%d ownerNoSee=%d dist=%.0f pitch=%.0f near=%.1f"),
                        *BodyMesh->GetName(), BodyMesh->IsVisible() ? 1 : 0, BodyMesh->bHiddenInGame ? 1 : 0,
                        BodyMesh->bOwnerNoSee ? 1 : 0, FVector::Dist(BodyMesh->GetComponentLocation(), OutResult.Location),
                        OutResult.Rotation.Pitch, OutResult.PerspectiveNearClipPlane);
            }
            if (BodyNow >= NextBodyLog) NextBodyLog = BodyNow + 5.0;
        }
		return;
	}
	UWorld* World = GetWorld();
	const float ProbeRadius = FMath::Max(CameraBoom->ProbeSize, 30.0f);
	const FVector ViewForward = ViewRotation.Vector();
	const FVector DesiredLocation = Pivot - ViewForward * DesiredDistance + ViewRotation.RotateVector(CameraBoom->SocketOffset);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WowThirdPersonCamera), true, this);
	QueryParams.AddIgnoredActor(this);
	QueryParams.bReturnPhysicalMaterial = false;
	QueryParams.bFindInitialOverlaps = true;

	// A camera boom should only retract along its intended line. Earlier escape
	// candidates moved above or sideways around door frames, which caused erratic
	// jumps and could place the camera beyond Abbey passage geometry.
	// r589 (Stefan, in the arch between the canal and Cathedral Square, by the wall torch: at some zoom levels
	// the camera "sees through the world"): the boom started at the pivot 1.8 m above the feet. In a narrow
	// arch the curved wall at that height can be closer than the capsule's radius, so the pivot itself sat
	// inside the masonry; a sweep that starts inside a one-sided wall never hits it and the camera ended up
	// on the far side. Reach the pivot from the capsule's centre first and stop at whatever is in the way.
	FVector SweepPivot = Pivot;
	{
		FHitResult PivotHit;
		if (SweepCameraObstacle(World, GetActorLocation(), Pivot, ProbeRadius, QueryParams, PivotHit))
			SweepPivot = PivotHit.bStartPenetrating ? GetActorLocation() : PivotHit.Location + PivotHit.Normal * 2.0f;
	}
	FVector SafeLocation = DesiredLocation;
	FHitResult ObstacleHit;
	if (SweepCameraObstacle(World, SweepPivot, DesiredLocation, ProbeRadius, QueryParams, ObstacleHit))
	{
		SafeLocation = ObstacleHit.Location;
		if (ObstacleHit.bStartPenetrating)
		{
			SafeLocation += ObstacleHit.Normal *
				(FMath::Max(ObstacleHit.PenetrationDepth, 0.0f) + 2.0f);
		}
		else
		{
			SafeLocation += ObstacleHit.Normal * 2.0f;
		}
	}

	OutResult.Location = SafeLocation;
	OutResult.Rotation = ViewRotation;
	// Fade by where the camera actually ends up: a wall behind you pushes it in through
	// the head, which showed the inside of the face. Like WoW, the body fades out instead.
	GWowSuppressLocalStrafeYaw = false;
	if (GetCharacterMovement()) GetCharacterMovement()->MaxAcceleration = 1500.0f; // VR comfort scale off
	UpdateFirstPersonVisualState(false, FMath::Min(DesiredDistance, static_cast<float>(FVector::Dist(SafeLocation, Pivot))));
	UpdateVRGearClipping(this, FVector::ZeroVector, 0.0f, false);
    ApplyVRHeadPose(OutResult, false, DeltaTime);
    ApplyCameraFocus(OutResult);
}

void AWowPlayerCharacter::ApplyVRHeadPose(FMinimalViewInfo& View, bool bFirstPerson, float DeltaTime)
{
	if (!FWowVRSupport::IsActive())
	{
		bVREyeHeightValid = false;
		if (bVRSeatFadeActive)
			if (APlayerController* PC = Cast<APlayerController>(GetController()); PC && PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->StopCameraFade();
				bVRSeatFadeActive = false;
			}
		return;
	}
	FQuat Head;
	FVector HeadPosition;
	if (!FWowVRSupport::GetHeadPose(Head, HeadPosition)) return;
	// Seated VR: the character/camera supplies only the turn. Pitch and roll come from the
	// head alone, so the horizon never tilts or bobs on its own.
	// Comfort: snap turning - the body's turn reaches the view in fixed steps instead of a
	// continuous rotation (the main trigger of motion sickness for many people).
	float BaseYaw = View.Rotation.Yaw;
	{
		static float SnappedYaw = 0.0f;
		static bool bSnapValid = false;
		if (FWowVRComfort::GetSnapTurn())
		{
			const float Step = FWowVRComfort::GetSnapTurnDegrees();
			if (!bSnapValid) { SnappedYaw = BaseYaw; bSnapValid = true; }
			const float Delta = FMath::FindDeltaAngleDegrees(SnappedYaw, BaseYaw);
			if (FMath::Abs(Delta) >= Step) SnappedYaw = FRotator::NormalizeAxis(SnappedYaw + Step * FMath::Sign(Delta));
			BaseYaw = SnappedYaw;
		}
		else bSnapValid = false;
	}
	const FRotator Base(0.0f, BaseYaw, 0.0f);
	if (bFirstPerson)
	{
		// The animated eye bone sways with breathing and bobs with every step; in a headset
		// that shakes the world even while sitting still. Follow it only slowly, in the
		// character's own frame, so the view rides with the body but never wobbles.
		static FVector SmoothedEyeOffset = FVector::ZeroVector;
		const FTransform& Body = GetActorTransform();
		FVector EyeOffset = Body.InverseTransformVectorNoScale(View.Location - Body.GetLocation());
		// Strafing turns the whole mesh sideways (legs run where you go); the head turns with it.
		// The eye stays where an unturned head would be, so the view keeps facing forward.
		if (AnimationController)
			EyeOffset = FRotator(0.0f, -AnimationController->GetVisualYawOffsetDegrees(), 0.0f).RotateVector(EyeOffset);
		// The animated eye sits ~20 cm in front of the face, so the whole body was behind the
		// camera: seated, nobody can bend the neck far enough to see it. Keep the eye over the
		// neck instead, so the chest and feet come into view at a natural downward glance.
		// Walking and running lean the body forward and swing the shoulders up past the neck:
		// then the eye goes back out in front of the face (where the animation puts it, plus a
		// little), so the chest and shoulders stay below and behind it.
		const float RawEyeForward = EyeOffset.X;
		EyeOffset.X = FMath::Min(EyeOffset.X, 4.0f);
		if (!bVREyeHeightValid) SmoothedEyeOffset = EyeOffset;
		else
		{
			// Breathing and the run cycle move the eye bone a few centimetres; in a headset
			// that reads as a heartbeat. Ignore anything smaller than 7 cm, and follow larger
			// changes (sitting, mounting, swimming) only slowly.
			const FVector Delta = EyeOffset - SmoothedEyeOffset;
			if (Delta.Size() > 7.0f)
				SmoothedEyeOffset = FMath::VInterpTo(SmoothedEyeOffset, EyeOffset - Delta.GetSafeNormal() * 7.0f, DeltaTime, 1.0f);
		}
		{
			static float EyeForward = 4.0f;
			static float IdleRawForward = 20.0f;
			const float Speed = float(GetVelocity().Size2D());
			const float Moving = FMath::Clamp(Speed / 150.0f, 0.0f, 1.0f);
			// The usual animated eye position (drops at once, rises only slowly).
			IdleRawForward = RawEyeForward < IdleRawForward ? RawEyeForward : FMath::FInterpTo(IdleRawForward, RawEyeForward, DeltaTime, 0.15f);
			// Casts and attacks that lean the head well forward (orc female's long reach): the
			// eye follows the head out, so the arms and shield stay in front of it.
			const float Lean = FMath::Max(0.0f, RawEyeForward - IdleRawForward - 3.0f);
			// +14: the run cycle swings a big chest (human male) up to ~12 cm ahead of the neck.
			const float WantForward = FMath::Lerp(4.0f, FMath::Max(4.0f, RawEyeForward + 14.0f), Moving) + Lean;
			EyeForward = FMath::FInterpTo(EyeForward, WantForward, DeltaTime, Lean > 0.0f ? 8.0f : 4.0f);
			SmoothedEyeOffset.X = EyeForward;
		}
		bVREyeHeightValid = true;
		VRSmoothedEyeHeight = SmoothedEyeOffset.Z;
		View.Location = Body.GetLocation() + Body.TransformVectorNoScale(SmoothedEyeOffset);
	}
	// Leaning in the chair is followed 1:1 within about half a metre of the seat.
	// The headset picks up the wearer's own pulse and breathing as sub-centimetre and
	// tenth-of-a-degree tremor. A soft dead zone holds the view through that and follows any
	// real head movement at once: tiny changes are ignored, larger ones pass straight through.
	{
		static FVector StillHeadPosition = HeadPosition;
		static FQuat StillHead = Head;
		const float Stillness = FWowVRComfort::GetHeadStillness();
		const float Moved = float(FVector::Dist(HeadPosition, StillHeadPosition));
		// At full stillness: position changes under 1 cm and turns under 0.3 degrees are held,
		// and up to 3 cm / 1.1 degrees are only partly followed (2.5x the r185 values, which
		// still let a strong pulse through). Real head movement is larger and passes at once.
		StillHeadPosition = FMath::Lerp(StillHeadPosition, HeadPosition, FMath::Clamp((Moved - 1.0f * Stillness) / FMath::Max(0.05f, 2.0f * Stillness), 0.0f, 1.0f));
		const float Turned = FMath::RadiansToDegrees(StillHead.AngularDistance(Head));
		StillHead = FQuat::Slerp(StillHead, Head, FMath::Clamp((Turned - 0.3f * Stillness) / FMath::Max(0.02f, 0.8f * Stillness), 0.0f, 1.0f));
		HeadPosition = StillHeadPosition;
		Head = StillHead;
	}
	FVector Lean = HeadPosition;
	if (Lean.SizeSquared() > FMath::Square(50.0f)) Lean = Lean.GetSafeNormal() * 50.0f;
	View.Location += Base.RotateVector(Lean);
	// Seated, the head only tilts down ~40-50 degrees. Below a 15 degree glance the view tilts
	// further than the head (x1.6), so looking down far enough shows the own body and feet.
	FRotator HeadRotation = Head.Rotator();
	if (bFirstPerson && HeadRotation.Pitch < -15.0f)
		HeadRotation.Pitch = FMath::Max(-89.0f, -15.0f + (HeadRotation.Pitch + 15.0f) * FWowVRComfort::GetLookDownGain());
	View.Rotation = (Base.Quaternion() * HeadRotation.Quaternion()).Rotator();
	// Looking down past the chest at your own legs means leaning forward: the further the
	// view tips down (from 25 to 65 degrees), the further the eye moves out over the chest,
	// up to 35 cm (20 cm hid an orc female's toes and a held shield behind the chest), so the
	// legs, feet and off-hand appear in front of it instead of behind it.
	if (bFirstPerson)
	{
		const float Down = FMath::Clamp((-View.Rotation.Pitch - 25.0f) / 40.0f, 0.0f, 1.0f);
		// Moving already puts the eye out in front of the face; leaning further on top of that
		// put the eye ahead of the knees (legs out of view while running and looking down).
		const float LeanMoving = FMath::Clamp(float(GetVelocity().Size2D()) / 150.0f, 0.0f, 1.0f);
		// r240: lean the way the head faces (over the shield arm when looking at it).
		if (FWowVRComfort::GetLeanOver()) View.Location += FRotator(0.0f, View.Rotation.Yaw, 0.0f).Vector() * (35.0f * Down * (1.0f - 0.6f * LeanMoving));
	}

	// Leaving the seat: dim the view (never fully black) instead of letting it leave the body.
	// Staying away for a few seconds means the chair moved: that spot becomes the new seat.
	static float SecondsAwayFromSeat = 0.0f;
	const float SeatDistance = HeadPosition.Size();
	SecondsAwayFromSeat = SeatDistance > 75.0f ? SecondsAwayFromSeat + DeltaTime : 0.0f;
	if (SecondsAwayFromSeat > 2.5f)
	{
		SecondsAwayFromSeat = 0.0f;
		FWowVRSupport::Recenter();
	}
	const float Fade = 0.75f * FMath::Clamp((SeatDistance - 50.0f) / 25.0f, 0.0f, 1.0f);
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
		if (PC->PlayerCameraManager && (Fade > 0.0f || bVRSeatFadeActive))
		{
			PC->PlayerCameraManager->SetManualCameraFade(Fade, FLinearColor::Black, false);
			bVRSeatFadeActive = Fade > 0.0f;
		}

	// First person: looking further than the head can turn (about 40 degrees from where the
	// character faces) turns the body after the head. The view itself never moves for it.
	if (bFirstPerson && GetCharacterMovement() && GetCharacterMovement()->MovementMode != MOVE_None)
	{
		// Looking down, the head may turn much further before the body follows (r224): turning
		// to the shield on the off-hand arm at 40 degrees turned the whole body with the head,
		// so the shield stayed out of view however far Stefan turned. From 40 degrees ahead to
		// 100 degrees while looking 20-45 degrees down.
		const float LookingDown = FMath::Clamp((-View.Rotation.Pitch - 20.0f) / 25.0f, 0.0f, 1.0f);
		const float HeadLimit = FMath::Lerp(40.0f, 100.0f, LookingDown);
		const float ViewYaw = View.Rotation.Yaw;
		const float Delta = FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, ViewYaw);
		if (FMath::Abs(Delta) > HeadLimit)
		{
			const float Excess = Delta - FMath::Sign(Delta) * HeadLimit;
			const float MaxStep = 240.0f * FMath::Max(DeltaTime, 0.001f);
			SetActorRotation(FRotator(0.0f, GetActorRotation().Yaw + FMath::Clamp(Excess, -MaxStep, MaxStep), 0.0f));
		}
	}
}

void AWowPlayerCharacter::EnableNativeFallbackVisuals()
{
	if (bNativeFallbackVisuals) return;
	bNativeFallbackVisuals = true;

	GetMesh()->SetVisibility(false, true);
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	auto AddPart = [this, BaseMaterial](const TCHAR* Name, const TCHAR* MeshPath,
		const FVector& Location, const FVector& Scale, const FLinearColor& Color)
		-> UStaticMeshComponent*
	{
		UStaticMesh* ShapeMesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
		if (!ShapeMesh) return nullptr;
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, Name);
		AddInstanceComponent(Part);
		Part->SetupAttachment(GetCapsuleComponent());
		Part->SetStaticMesh(ShapeMesh);
		Part->SetRelativeLocation(Location);
		Part->SetRelativeScale3D(Scale);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->RegisterComponent();
		if (BaseMaterial)
		{
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, Part);
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Part->SetMaterial(0, Material);
		}
		return Part;
	};

	constexpr const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	constexpr const TCHAR* Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const FLinearColor Wood(0.30f, 0.13f, 0.04f, 1.0f);
	FallbackHead = AddPart(TEXT("FallbackStickHead"), Sphere, FVector(0.0f, 0.0f, 70.0f),
		FVector(0.30f), FLinearColor(0.72f, 0.56f, 0.36f, 1.0f));
	AddPart(TEXT("FallbackStickBody"), Cylinder, FVector(0.0f, 0.0f, 22.0f),
		FVector(0.085f, 0.085f, 0.70f), Wood);
	AddPart(TEXT("FallbackStickLeftShoulder"), Sphere, FVector(0.0f, -8.0f, 53.0f),
		FVector(0.10f), Wood);
	AddPart(TEXT("FallbackStickRightShoulder"), Sphere, FVector(0.0f, 8.0f, 53.0f),
		FVector(0.10f), Wood);
	FallbackLeftArm = AddPart(TEXT("FallbackStickLeftArm"), Cylinder,
		FVector(0.0f, -19.0f, 27.0f), FVector(0.065f, 0.065f, 0.62f), Wood);
	FallbackRightArm = AddPart(TEXT("FallbackStickRightArm"), Cylinder,
		FVector(0.0f, 19.0f, 27.0f), FVector(0.065f, 0.065f, 0.62f), Wood);
	FallbackLeftLeg = AddPart(TEXT("FallbackStickLeftLeg"), Cylinder,
		FVector(0.0f, -10.0f, -50.0f), FVector(0.07f, 0.07f, 0.70f), Wood);
	FallbackRightLeg = AddPart(TEXT("FallbackStickRightLeg"), Cylinder,
		FVector(0.0f, 10.0f, -50.0f), FVector(0.07f, 0.07f, 0.70f), Wood);
	FallbackLeftArm->SetRelativeRotation(FRotator(0.0f, 0.0f, -22.0f));
	FallbackRightArm->SetRelativeRotation(FRotator(0.0f, 0.0f, 22.0f));
	FallbackLeftLeg->SetRelativeRotation(FRotator(0.0f, 0.0f, -8.0f));
	FallbackRightLeg->SetRelativeRotation(FRotator(0.0f, 0.0f, 8.0f));

	UE_LOG(LogWowPlayerChar, Warning, TEXT("Enabled playable native stick-figure fallback"));
}

// What stopped us (r269, Stefan: stopped by kerbs and stairs). Logs blocking hits low on the
// capsule while walking, at most once a second per component: name, height above the feet,
// normal, and whether the component allows step-up.
void AWowPlayerCharacter::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
    bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
    Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
    const UCharacterMovementComponent* Mov = GetCharacterMovement();
    if (!bSelfMoved || !Mov || !Mov->IsMovingOnGround() || !OtherComp || HitNormal.Z > 0.7f) return;
    const float FeetZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float Above = float(Hit.ImpactPoint.Z) - FeetZ;
    if (Above < 2.f || Above > 140.f) return;
    // r589 (Stefan: "unable to walk the last step up from the canal", Stormwind canal stairs): the engine's
    // step-up refused a 11-35 cm riser there (log: blocked on canalB 284, step-up allowed, max step 95 cm),
    // most likely because the top step meets the street piece at a seam or a lip. When walking into a low
    // riser (under the max step height, near-vertical face), try the step ourselves: up, a little forward,
    // and down onto walkable ground, each move swept with the capsule so it never goes into geometry.
    if (UCharacterMovementComponent* StepMov = GetCharacterMovement();
        // r669 (Stefan: "can't walk up these steps from this angle" - Deathknell, a wooden step walked onto from its side;
        // log: "blocked ... 20 cm above feet, normal (0,-0.90,0.43)"): the side of that step is bevelled (64 degrees), not
        // vertical, so neither the engine's step-up nor this assist (|normal.Z| < 0.35) took it. Bevelled lips of
        // buildings and props up to a walkable slope count as risers too; terrain keeps the strict test.
        IsLocallyControlled() && StepMov && Above <= FMath::Min(StepMov->MaxStepHeight, 50.f) // stairs and kerbs, not fences
        && (FMath::Abs(HitNormal.Z) < 0.35f || (HitNormal.Z > 0.f && HitNormal.Z < StepMov->GetWalkableFloorZ()
            && !(Other && Other->GetName().StartsWith(TEXT("TerrainTile")))))
        && OtherComp->CanCharacterStepUpOn != ECB_No
        && FVector::DotProduct(StepMov->GetCurrentAcceleration().GetSafeNormal2D(), -HitNormal.GetSafeNormal2D()) > 0.5f)
    {
        static double LastAssist = 0.0;
        const double AssistNow = FPlatformTime::Seconds();
        if (AssistNow - LastAssist > 0.15)
        {
            LastAssist = AssistNow;
            const UCapsuleComponent* Capsule = GetCapsuleComponent();
            const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
            FCollisionQueryParams StepQuery(SCENE_QUERY_STAT(WowStepAssist), false, this);
            const FCollisionResponseParams StepResponse;
            const ECollisionChannel StepChannel = Capsule->GetCollisionObjectType();
            const FVector Start = GetActorLocation();
            const FVector Up = Start + FVector(0, 0, Above + 4.f);
            const FVector Forward = Up - HitNormal.GetSafeNormal2D() * 30.f;
            FHitResult StepHit;
            const bool bUpBlocked = GetWorld()->SweepSingleByChannel(StepHit, Start, Up, FQuat::Identity, StepChannel, Shape, StepQuery, StepResponse);
            const bool bForwardBlocked = !bUpBlocked && GetWorld()->SweepSingleByChannel(StepHit, Up, Forward, FQuat::Identity, StepChannel, Shape, StepQuery, StepResponse);
            if (!bUpBlocked && !bForwardBlocked
                && GetWorld()->SweepSingleByChannel(StepHit, Forward, Forward - FVector(0, 0, Above + 30.f), FQuat::Identity, StepChannel, Shape, StepQuery, StepResponse)
                && !StepHit.bStartPenetrating && StepHit.ImpactNormal.Z >= StepMov->GetWalkableFloorZ()
                && StepHit.Location.Z > Start.Z + 1.f)
            {
                SetActorLocation(StepHit.Location + FVector(0, 0, 1.f), false);
                UE_LOG(LogWowPlayerChar, Log, TEXT("Movement step assist: up %.0f cm onto %s.%s at %s"),
                    float(StepHit.Location.Z - Start.Z), *GetNameSafe(StepHit.GetActor()), *GetNameSafe(StepHit.GetComponent()), *StepHit.Location.ToCompactString());
                return;
            }
        }
    }
    static TMap<TWeakObjectPtr<UPrimitiveComponent>, double> LastLog;
    const double Now = FPlatformTime::Seconds();
    if (double* Last = LastLog.Find(OtherComp)) { if (Now - *Last < 1.0) return; }
    if (LastLog.Num() > 256) LastLog.Reset();
    LastLog.Add(OtherComp, Now);
    UE_LOG(LogWowPlayerChar, Log, TEXT("Movement blocked: %s.%s (%s) %.0f cm above feet, normal %s, stepUp=%d, at %s"),
        *GetNameSafe(Other), *OtherComp->GetName(), *OtherComp->GetClass()->GetName(), Above, *HitNormal.ToCompactString(),
        int32(OtherComp->CanCharacterStepUpOn.GetValue()), *Hit.ImpactPoint.ToCompactString());
}

void AWowPlayerCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (AnimationController)
	{
		const bool bHasMovementIntent = bIsAutoRunning ||
			!CurrentMoveInput2D.IsNearlyZero() ||
			!GetPendingMovementInputVector().IsNearlyZero() ||
			!GetLastMovementInputVector().IsNearlyZero();
		AnimationController->NotifyLocalLanded(bHasMovementIntent);
	}

	if (bIsFalling)
	{
		bIsFalling = false;
		const float FallDistance = FallStartZ - GetActorLocation().Z;

		if (FallDistance > FallDamageMinHeight)
		{
			// Fall damage: proportional to distance beyond threshold
			const float ExcessFall = FallDistance - FallDamageMinHeight;
			const float DamagePct = FMath::Clamp(ExcessFall / 3000.0f, 0.0f, 1.0f); // max at ~30 extra yards
			UE_LOG(LogWowPlayerChar, Log, TEXT("Fall damage: fell %.0f cm (%.0f excess), %.0f%% damage"),
				FallDistance, ExcessFall, DamagePct * 100.0f);
			// Actual HP damage would be applied via server â€” log it for now
		}
	}
}

void AWowPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EI = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction) EI->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AWowPlayerCharacter::OnMove);
		if (MoveAction) EI->BindAction(MoveAction, ETriggerEvent::Completed, this, &AWowPlayerCharacter::OnMoveCompleted);
		if (JumpAction)
		{
			EI->BindAction(JumpAction, ETriggerEvent::Started, this, &AWowPlayerCharacter::OnJumpPressed);
			EI->BindAction(JumpAction, ETriggerEvent::Completed, this, &AWowPlayerCharacter::OnJumpReleased);
		}
		if (SwimDownAction)
		{
			EI->BindAction(SwimDownAction, ETriggerEvent::Started, this, &AWowPlayerCharacter::OnSwimDownPressed);
			EI->BindAction(SwimDownAction, ETriggerEvent::Completed, this, &AWowPlayerCharacter::OnSwimDownReleased);
		}
		if (ZoomAction) EI->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &AWowPlayerCharacter::OnZoom);
		if (ToggleWalkAction) EI->BindAction(ToggleWalkAction, ETriggerEvent::Started, this, &AWowPlayerCharacter::OnToggleWalk);
		if (AutoRunAction) EI->BindAction(AutoRunAction, ETriggerEvent::Started, this, &AWowPlayerCharacter::OnToggleAutoRun);
	}
}

void AWowPlayerCharacter::OnMove(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	CurrentMoveInput2D = Input;
	if (Input.IsNearlyZero()) return;
	// r610: isometric camera - keys move relative to the screen and the character turns to face
	// the way it walks (the server sees plain forward movement).
	if (FWowIsometricCamera::IsActive())
	{
		const FVector IsoDirection = FWowIsometricCamera::ScreenDirection(Input);
		if (IsoDirection.IsNearlyZero()) return;
		if (AnimationController) AnimationController->NotifyLocalMovementIntent();
		bIsAutoRunning = false;
		if (const auto* IsoGameplay = Cast<AWowGameplayController>(GetController()); IsoGameplay && IsoGameplay->IsMovementHeldBySitting()) return;
		// r625 (Stefan: "the hard turnings of the animations in 8 directions" seen from above): turn
		// towards the new direction at 720 degrees per second instead of snapping to it.
		SetActorRotation(FRotator(0.0f, WowIsoSmoothTurnYaw(GetActorRotation().Yaw, IsoDirection.Rotation().Yaw, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f), 0.0f));
		CurrentMoveInput2D = FVector2D(0.0f, IsoDirection.Size());
		AddMovementInput(IsoDirection.GetSafeNormal(), IsoDirection.Size());
		return;
	}
	if (AnimationController)
	{
		AnimationController->NotifyLocalMovementIntent();
	}

	if (bIsAutoRunning && !FMath::IsNearlyZero(Input.Y))
	{
		bIsAutoRunning = false;
	}

	// WoW movement is relative to character facing. Right mouse look updates the
	// character facing first, while left mouse look can orbit the camera without
	// forcing movement to follow the camera.
	const FRotator ActorYawRot(0.0f, GetActorRotation().Yaw, 0.0f);
    FRotator SwimFacing = ActorYawRot;
    const auto* Gameplay = Cast<AWowGameplayController>(GetController());
    if (bWowWaterSwimming && Gameplay && Gameplay->IsSteeringWithMouse())
        SwimFacing.Pitch = FRotator::NormalizeAxis(Gameplay->GetControlRotation().Pitch);
    const FVector Forward = FRotationMatrix(SwimFacing).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(ActorYawRot).GetUnitAxis(EAxis::Y);
	const bool bBackpedaling = Input.Y < -KINDA_SMALL_NUMBER;
	float ForwardScale = bBackpedaling ? Input.Y * BackpedalFactor : Input.Y;
	float RightScale = Input.X;

	// Backpedal + strafe should remain backpedal-speed, not full strafe speed
	// with a small backwards component. Clamp the combined local vector.
	if (bBackpedaling)
	{
		RightScale *= BackpedalFactor;
		const FVector2D BackpedalInput(RightScale, ForwardScale);
		const FVector2D ClampedBackpedalInput = BackpedalInput.GetClampedToMaxSize(BackpedalFactor);
		RightScale = ClampedBackpedalInput.X;
		ForwardScale = ClampedBackpedalInput.Y;
	}

	// Rooted stand-up: the first key press stands up; it must not nudge the character
	// forward before the root takes hold.
	if (Gameplay && Gameplay->IsMovementHeldBySitting()) return;
	AddMovementInput(Forward, ForwardScale); // W/S
	AddMovementInput(Right, RightScale);   // A/D
}

void AWowPlayerCharacter::OnMoveCompleted(const FInputActionValue& Value)
{
	CurrentMoveInput2D = FVector2D::ZeroVector;
}

void AWowPlayerCharacter::OnLook(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	if (AWowGameplayController* GameplayController = Cast<AWowGameplayController>(GetController()))
	{
		GameplayController->AccumulateMouseLookDelta(Input.X, Input.Y);
	}
}

void AWowPlayerCharacter::OnJumpPressed()
{
	if (bWowWaterSwimming || (GetCharacterMovement() && GetCharacterMovement()->IsFlying()))
	{
		bSwimUpPressed = true;
		AddMovementInput(FVector::UpVector, 1.0f);
		return;
	}

	const bool bCanStartJump = CanJump();
	Jump();
	if (bCanStartJump && AnimationController)
	{
		AnimationController->NotifyLocalJumpStarted(GetActorLocation().Z);
	}
}

void AWowPlayerCharacter::OnJumpReleased()
{
	bSwimUpPressed = false;
	StopJumping();
}

void AWowPlayerCharacter::OnSwimDownPressed()
{
	bSwimDownPressed = true;
	if (bWowWaterSwimming || (GetCharacterMovement() && GetCharacterMovement()->IsFlying()))
	{
		AddMovementInput(FVector::DownVector, 1.0f);
	}
}

void AWowPlayerCharacter::OnSwimDownReleased()
{
	bSwimDownPressed = false;
}

void AWowPlayerCharacter::UpdateWaterMovementMode(float DeltaTime)
{
	UCharacterMovementComponent* Mov = GetCharacterMovement();
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Mov || !Capsule)
	{
		bHeadSubmerged = false;
		bWowWaterSwimming = false;
		return;
	}

	TArray<UPrimitiveComponent*> OverlappingComponents;
	Capsule->GetOverlappingComponents(OverlappingComponents);

	bool bHasWaterVolume = false;
	bool bBestIsOcean = false;
	float BestSurfaceZ = -FLT_MAX;
	static constexpr float WaterVolumeSurfacePaddingCm = 18.0f;
	for (UPrimitiveComponent* Component : OverlappingComponents)
	{
		if (!Component || !Component->ComponentHasTag(TEXT("WowWaterVolume")))
		{
			continue;
		}

		const FVector Origin = Component->Bounds.Origin;
		const FVector Extent = Component->Bounds.BoxExtent;
		const float SurfaceZ = Origin.Z + Extent.Z - WaterVolumeSurfacePaddingCm;
		if (SurfaceZ > BestSurfaceZ)
		{
			BestSurfaceZ = SurfaceZ;
			bBestIsOcean = Component->ComponentHasTag(TEXT("WowOceanWater"));
		}
		bHasWaterVolume = true;
	}
	bInOceanWater = bHasWaterVolume && bBestIsOcean;

	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float FeetZ = GetActorLocation().Z - HalfHeight;
	const float HeadProbeZ = GetActorLocation().Z + HalfHeight * 0.78f;
	bHeadSubmerged = bHasWaterVolume && BestSurfaceZ > HeadProbeZ;
	// Keep shallow river edges as wading/walking, then transition around upper
	// torso depth. HalfHeight is one half of the full capsule, so values near 2.0
	// actually required the water to reach the very top of the capsule and left
	// fully immersed characters running along the bottom.
	const float SwimStartZ = FeetZ + HalfHeight * 1.38f;
	const float SwimStayZ = FeetZ + HalfHeight * 1.22f;
	const bool bShouldSwim = bHasWaterVolume &&
		BestSurfaceZ > (bWowWaterSwimming ? SwimStayZ : SwimStartZ);

    const bool FeetWet = bHasWaterVolume && BestSurfaceZ > FeetZ + 3.0f;
    // Contact is detected at the feet, before changing falling velocity to swim.
    // This also catches jumps into ankle-deep water without firing twice at chest depth.
    if (FeetWet && !bFeetInWater)
        NotifyWaterSurfaceInteraction(false, BestSurfaceZ);
    bFeetInWater = FeetWet;
    UpdateWaterFootsteps(DeltaTime, FeetWet && !bShouldSwim && Mov->IsMovingOnGround());
	UpdateWaterPresentationState(bHasWaterVolume, BestSurfaceZ, DeltaTime);

	// Create a small wake while wading or swimming near the surface. Emit by
	// distance travelled rather than a repeating timer, so a slow-moving or
	// stationary object never produces an artificial looping pulse.
	const float SurfaceDistance = bHasWaterVolume ? FMath::Abs(GetActorLocation().Z - BestSurfaceZ) : FLT_MAX;
	const FVector SurfaceVelocity = GetVelocity();
	if (bHasWaterVolume && SurfaceVelocity.SizeSquared2D() > FMath::Square(45.0f) &&
		SurfaceDistance < HalfHeight * 1.55f && GetWorld())
	{
		const FVector RippleLocation(GetActorLocation().X, GetActorLocation().Y, BestSurfaceZ);
		const float RequiredTravelDistance = bWowWaterSwimming ? 85.0f : 65.0f;
		const bool bSurfaceChanged = bHasLastWaterRippleLocation &&
			FMath::Abs(LastWaterRippleLocation.Z - BestSurfaceZ) > 65.0f;
        // Eight shader slots / 2.3 s lifetime: keep fast running from replacing
        // visibly active wave fronts before they have faded out.
        const float Now = GetWorld()->GetTimeSeconds();
		if (Now - LastWaterWakeTime >= .30f && (!bHasLastWaterRippleLocation || bSurfaceChanged ||
			FVector::DistSquared2D(RippleLocation, LastWaterRippleLocation) >=
			FMath::Square(RequiredTravelDistance)))
		{
            LastWaterWakeTime = Now;
			LastWaterRippleLocation = RippleLocation;
			bHasLastWaterRippleLocation = true;
			SpawnWaterRipple(BestSurfaceZ, false, SurfaceVelocity.GetSafeNormal2D());
		}
	}
	else if (!bHasWaterVolume)
	{
		bHasLastWaterRippleLocation = false;
	}

	if (bShouldSwim)
	{
		const bool bEnteringWater = !bWowWaterSwimming;
		bWowWaterSwimming = true;
		// WoW liquid is represented by inexpensive overlap boxes rather than
		// APhysicsVolume actors. MOVE_Swimming immediately falls back to walking
		// without an APhysicsVolume, so MOVE_Flying supplies the same gravity-free,
		// collision-aware three-dimensional locomotion while this explicit state
		// remains authoritative.
		if (Mov->MovementMode != MOVE_Flying)
		{
			Mov->SetMovementMode(MOVE_Flying);
		}
		Mov->MaxFlySpeed = RunSpeed * SwimSpeedFactor;
		if (bEnteringWater)
		{
			Mov->Velocity.Z = FMath::Max(Mov->Velocity.Z, 0.0f);

		}

		// One downward probe per frame: water depth (ocean shoaling) and whether
		// the bottom is close enough to wade out.
		bool bBottomClose = false;
		WaterDepthMeters = 10.0f;
		if (UWorld* World = GetWorld())
		{
			FHitResult BottomHit;
			FCollisionQueryParams BottomParams(TEXT("WowSwimBottom"), false, this);
			const FVector Start = GetActorLocation();
			const FVector End(Start.X, Start.Y, BestSurfaceZ - 1000.0f);
			if (World->LineTraceSingleByChannel(BottomHit, Start, End, ECC_Visibility, BottomParams) && BottomHit.bBlockingHit)
			{
				WaterDepthMeters = FMath::Max(0.0f, static_cast<float>(BestSurfaceZ - BottomHit.ImpactPoint.Z) / 100.0f);
				bBottomClose = BottomHit.ImpactPoint.Z > FeetZ - 30.0f;
			}
		}
		// Hold a stable float height just under the exit threshold so the swim state
		// never flickers (that flicker read as "falling" when a wave trough passed).
		// Swim-up (jump) still breaches, and close ground lets the player wade out.
		const float FloatMaxZ = BestSurfaceZ - HalfHeight * 0.26f;
		if (!bSwimUpPressed && !bBottomClose && GetActorLocation().Z > FloatMaxZ)
		{
			FVector Clamped = GetActorLocation();
			Clamped.Z = FloatMaxZ;
			SetActorLocation(Clamped, false, nullptr, ETeleportType::None);
			Mov->Velocity.Z = FMath::Min(Mov->Velocity.Z, 0.0f);
		}
		const float BelowFloat = FloatMaxZ - static_cast<float>(GetActorLocation().Z);
		SwimNearSurface = FMath::Clamp(1.0f - BelowFloat / (HalfHeight * 0.6f), 0.0f, 1.0f);
	}
	else if (bWowWaterSwimming)
	{
		bWowWaterSwimming = false;
		const bool bBreachingSurface = bSwimUpPressed && Mov->Velocity.Z > 0.0f;
		bool bGroundClose = !bBreachingSurface && Mov->IsMovingOnGround();
		if (!bGroundClose && GetWorld())
		{
			FHitResult GroundHit;
			FCollisionQueryParams GroundTraceParams(TEXT("WowWaterExitGround"), false, this);
			const FVector TraceStart = GetActorLocation() + FVector(0.0f, 0.0f, 24.0f);
			const FVector TraceEnd = GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight + 80.0f);
			if (!bBreachingSurface &&
				GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, GroundTraceParams) &&
				GroundHit.bBlockingHit &&
				GroundHit.ImpactNormal.Z >= 0.45f)
			{
				bGroundClose = true;
			}
		}

		Mov->SetMovementMode(bGroundClose ? MOVE_Walking : MOVE_Falling);
		if (bBreachingSurface)
		{
			Mov->Velocity.Z = FMath::Max(Mov->Velocity.Z, WowGameplayWaterExitJumpZVelocity);
			bIsFalling = true;
			FallStartZ = GetActorLocation().Z;
			if (AnimationController)
			{
				AnimationController->NotifyLocalJumpStarted(GetActorLocation().Z);
			}
			NotifyWaterSurfaceInteraction(true, BestSurfaceZ);
		}
		else if (bGroundClose)
		{
			Mov->Velocity.Z = 0.0f;
			bIsFalling = false;
			NotifyWaterSurfaceInteraction(true, BestSurfaceZ);
		}
	}
}

void AWowPlayerCharacter::UpdateSwimVisuals(float DeltaTime)
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!MeshComponent || !Capsule || !GetWorld())
	{
		return;
	}
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const bool bSurfaceSwim = bWowWaterSwimming && FMath::IsFinite(LastKnownWaterSurfaceZ) &&
		LastKnownWaterSurfaceZ > -FLT_MAX * 0.5f;
	const float Near = bSurfaceSwim ? SwimNearSurface : 0.0f;
	// Shoulders/upper back at the waterline, head above (3.3.5 swim height).
	const float TargetSink = -HalfHeight * 0.25f * Near;
	const float TargetWave = bSurfaceSwim && bInOceanWater
		? FWowWaterMaterial::SampleOceanHeight(GetActorLocation(), GetWorld()->GetTimeSeconds(), WaterDepthMeters) * Near
		: 0.0f;
	// The wave sample is already smooth in time; only damp entry/exit and depth changes.
	SwimWaveOffset = FMath::FInterpTo(SwimWaveOffset, TargetWave, DeltaTime, 14.0f);
	static IConsoleVariable* SwimDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("wow.SwimDebug"));
	if (SwimDebug && SwimDebug->GetInt() && bSurfaceSwim)
		UE_LOG(LogTemp, Log, TEXT("SwimDebug: ocean=%d near=%.2f depth=%.1fm wave=%.1fcm sink=%.1fcm"),
			bInOceanWater ? 1 : 0, Near, WaterDepthMeters, TargetWave, SwimMeshOffset);
	SwimMeshOffset = FMath::FInterpTo(SwimMeshOffset, TargetSink, DeltaTime, 6.0f);
	const float Total = SwimMeshOffset + SwimWaveOffset;
	// The swell also carries a floating swimmer to and fro along the waves (r229, Stefan: "move
	// us a little with them while we stay still"). Only the body is moved, never the position
	// sent to the server; full while floating still, half while swimming, and in VR a third
	// so the headset view does not slide around.
	static FVector2D SwimDriftLocal = FVector2D::ZeroVector;
	static float SwimDriftAppliedX = 0.0f, SwimDriftAppliedY = 0.0f; // drift currently in the mesh offset
	{
		FVector2D Target = FVector2D::ZeroVector;
		if (bSurfaceSwim && bInOceanWater)
		{
			const FVector2D Drift = FWowWaterMaterial::SampleOceanDrift(GetActorLocation(), GetWorld()->GetTimeSeconds(), WaterDepthMeters) * Near;
			const float Still = 1.0f - 0.5f * FMath::Clamp(float(GetVelocity().Size2D()) / 200.0f, 0.0f, 1.0f);
			const FVector Local = Capsule->GetComponentTransform().InverseTransformVectorNoScale(FVector(Drift.X, Drift.Y, 0.0f));
			Target = FVector2D(Local.X, Local.Y) * Still * (FWowVRSupport::IsActive() ? 0.33f : 1.0f);
		}
		SwimDriftLocal = FMath::Vector2DInterpTo(SwimDriftLocal, Target, DeltaTime, 6.0f);
	}
	if (FMath::Abs(Total) < 0.05f && !bSurfaceSwim)
	{
		if (bSwimMeshOffsetApplied)
		{
			FVector Relative = MeshComponent->GetRelativeLocation();
			Relative.X -= SwimDriftAppliedX; Relative.Y -= SwimDriftAppliedY;
			SwimDriftAppliedX = SwimDriftAppliedY = 0.0f;
			SwimDriftLocal = FVector2D::ZeroVector;
			Relative.Z = -HalfHeight;
			MeshComponent->SetRelativeLocation(Relative);
			bSwimMeshOffsetApplied = false;
		}
		SwimMeshOffset = SwimWaveOffset = 0.0f;
		return;
	}
	FVector Relative = MeshComponent->GetRelativeLocation();
	Relative.Z = -HalfHeight + Total;
	// Swap the previous drift for the new one (X/Y keep whatever else the mesh offset holds).
	Relative.X += SwimDriftLocal.X - SwimDriftAppliedX;
	Relative.Y += SwimDriftLocal.Y - SwimDriftAppliedY;
	SwimDriftAppliedX = SwimDriftLocal.X;
	SwimDriftAppliedY = SwimDriftLocal.Y;
	MeshComponent->SetRelativeLocation(Relative);
	bSwimMeshOffsetApplied = true;
}

void AWowPlayerCharacter::UpdateWaterPresentationState(bool bHasWaterVolume, float BestSurfaceZ, float DeltaTime)
{
	const bool bHasUsableWaterSurface = bHasWaterVolume && BestSurfaceZ > -FLT_MAX * 0.5f;
	if (bHasUsableWaterSurface)
	{
		LastKnownWaterSurfaceZ = BestSurfaceZ;
	}
	const bool bUnderwaterPresentationTest = FParse::Param(
		FCommandLine::Get(), TEXT("testunderwaterpresentation"));
	const FVector CameraLocation = FollowCamera
		? (bHasRenderedCameraLocation ? LastRenderedCameraLocation : FollowCamera->GetComponentLocation())
		: FVector::ZeroVector;
	// The ocean surface moves: test the camera against the swell at its own position.
	const float CameraSurfaceZ = bHasUsableWaterSurface && bInOceanWater && GetWorld()
		? BestSurfaceZ + FWowWaterMaterial::SampleOceanHeight(CameraLocation, GetWorld()->GetTimeSeconds(), WaterDepthMeters)
		: BestSurfaceZ;
	const bool bCameraUnderWater = bUnderwaterPresentationTest ||
		(bHasUsableWaterSurface && FollowCamera && CameraLocation.Z < CameraSurfaceZ - 12.0f);
	bViewUnderWater = bCameraUnderWater;
	const bool bUnderwaterAudioEnabled = FWowUnderwaterAudioPolicy::ShouldEnable(
		bCameraUnderWater, bHeadSubmerged);

	const float TargetBlend = bCameraUnderWater ? 1.0f : 0.0f;
	UnderwaterVisualBlend = FMath::FInterpTo(UnderwaterVisualBlend, TargetBlend, DeltaTime, bCameraUnderWater ? 5.0f : 7.0f);

	if (FollowCamera)
	{
		FPostProcessSettings& Settings = FollowCamera->PostProcessSettings;
		Settings.bOverride_SceneColorTint = true;
		Settings.SceneColorTint = FLinearColor(0.58f, 0.90f, 1.0f, 1.0f);
		Settings.bOverride_ColorSaturation = true;
		Settings.ColorSaturation = FVector4(0.42f, 0.67f, 0.88f, 1.0f);
		Settings.bOverride_ColorContrast = true;
		Settings.ColorContrast = FVector4(0.72f, 0.78f, 0.84f, 1.0f);
		Settings.bOverride_ColorGamma = true;
		Settings.ColorGamma = FVector4(0.82f, 0.88f, 0.94f, 1.0f);
		Settings.bOverride_VignetteIntensity = true;
		Settings.VignetteIntensity = 0.52f;
		FollowCamera->PostProcessBlendWeight = UnderwaterVisualBlend;
	}

	// Reassert this inexpensive state every frame. The audio manager can be
	// created after the character during client-flow transitions; the old
	// edge-trigger permanently missed the mix when that happened.
	if (AWowAudioManager* AudioManager = FindAudioManager())
	{
		AudioManager->SetUnderwaterMixEnabled(bUnderwaterAudioEnabled);
	}

	if (bWasTouchingWater && !bHasWaterVolume)
	{
		NotifyWaterSurfaceInteraction(true, LastKnownWaterSurfaceZ);
	}
	bWasTouchingWater = bHasWaterVolume;
}

void AWowPlayerCharacter::NotifyWaterSurfaceInteraction(bool bLeavingWater, float WaterSurfaceZ)
{
	if (!GetWorld())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastWaterInteractionEffectTime < 0.35f)
	{
		return;
	}

    LastWaterInteractionEffectTime = Now;
    if (!bLeavingWater)
    {
        if (AWowAudioManager* Audio = FindAudioManager())
        {
            const auto* Race = FDbcStore::Get().ChrRaces().GetById(CharacterRaceId);
            Audio->PlaySoundEffect(Race && Race->SplashSoundID ? Race->SplashSoundID : 1096);
        }
    }
	if (FMath::IsFinite(WaterSurfaceZ) && WaterSurfaceZ > -FLT_MAX * 0.5f)
	{
		SpawnWaterRipple(WaterSurfaceZ, true, GetVelocity().GetSafeNormal2D());
	}
	UE_LOG(LogWowPlayerChar, Verbose, TEXT("Water interaction: %s surfaceZ=%.1f"),
		bLeavingWater ? TEXT("exit") : TEXT("enter/swim"), WaterSurfaceZ);
}

void AWowPlayerCharacter::UpdateWaterFootsteps(float DeltaTime, bool bWading)
{
    const float Speed = GetVelocity().Size2D();
    // Our own rain puddles splash too (no sediment: it is a film on the ground).
    bool bPuddle = false;
    if (!bWading && GetCharacterMovement() && GetCharacterMovement()->IsMovingOnGround())
    {
        const FVector Feet = GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        // Any visibly drawn film splashes (0.8 cm missed the thin puddles you can see).
        bPuddle = UWowWeatherComponent::GetPuddleDepthAt(Feet) > 0.25f;
    }
    // r683 (Stefan: walked all over the puddles and heard nothing, "for the bigger one I heard it"): the splash
    // only sounded once a whole stride (3.1 m running) had been covered inside water, so a small puddle was
    // crossed between two checks. Stepping into a puddle now splashes at once, then once a stride while in it.
    static TMap<TWeakObjectPtr<const AWowPlayerCharacter>, bool> GWasInPuddle;
    bool& bWasInPuddle = GWasInPuddle.FindOrAdd(this);
    const bool bEnteredPuddle = bPuddle && !bWasInPuddle;
    bWasInPuddle = bPuddle;
    if ((!bWading && !bPuddle) || Speed < 20.0f) { WaterStepDistance=0; return; }
    WaterStepDistance += Speed * FMath::Min(DeltaTime, .1f);
    const float Stride = bIsWalking ? 150.0f : 310.0f;
    const float Now = GetWorld()->GetTimeSeconds();
    if (bEnteredPuddle && Now-LastWaterStepTime >= .25f) WaterStepDistance = Stride;
    if (WaterStepDistance < Stride || Now-LastWaterStepTime < (bEnteredPuddle ? .25f : .44f)) return;
    WaterStepDistance = FMath::Fmod(WaterStepDistance, Stride);
    LastWaterStepTime = Now;
    if (bPuddle)
    {
        if (AWowAudioManager* Audio=FindAudioManager()) Audio->PlaySoundEffect(CharacterRaceId==7 ? 1063 : 1054);
        return;
    }
    if (!WaterSediment)
    {
        WaterSediment = NewObject<UWowWaterSedimentComponent>(this);
        AddInstanceComponent(WaterSediment);
        WaterSediment->RegisterComponent();
    }
    const float FeetZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    WaterSediment->Disturb(FVector(GetActorLocation().X, GetActorLocation().Y, FeetZ + 8), LastKnownWaterSurfaceZ);
    if (AWowAudioManager* Audio=FindAudioManager())
    {
        // SoundEntries' original small/medium water-footfall families (five variants).
        Audio->PlaySoundEffect(CharacterRaceId==7 ? 1063 : 1054);
    }
}

void AWowPlayerCharacter::SpawnWaterRipple(float WaterSurfaceZ, bool bStrongRipple,
	const FVector& MovementDirection)
{
	if (!GetWorld() || !FMath::IsFinite(WaterSurfaceZ))
	{
		return;
	}

#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("testwaterripples")))
        UE_LOG(LogWowPlayerChar, Log, TEXT("[NATIVEWAKETEST] impulse t=%.2f xy=(%.1f,%.1f) surface=%.1f speed=%.1f splash=%d"),
            GetWorld()->GetTimeSeconds(), GetActorLocation().X, GetActorLocation().Y, WaterSurfaceZ, GetVelocity().Size2D(), bStrongRipple);
#endif
	FWowWaterMaterial::AddSurfaceRipple(
        FVector(GetActorLocation().X, GetActorLocation().Y, WaterSurfaceZ),
        MovementDirection, bStrongRipple, GetWorld()->GetTimeSeconds());
}

void AWowPlayerCharacter::UpdateWaterRipples(float DeltaTime)
{
    if (GetWorld()) FWowWaterMaterial::UpdateSurfaceRipples(GetWorld()->GetTimeSeconds());
}

AWowAudioManager* AWowPlayerCharacter::FindAudioManager()
{
	if (CachedAudioManager)
	{
		return CachedAudioManager.Get();
	}

	if (!GetWorld())
	{
		return nullptr;
	}

	CachedAudioManager = Cast<AWowAudioManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AWowAudioManager::StaticClass()));
	return CachedAudioManager.Get();
}

void AWowPlayerCharacter::RefreshFirstPersonCameraProfile()
{
	const FWowFirstPersonCameraProfile Profile =
		FWowFirstPersonCameraPolicy::Resolve(CharacterRaceId, CharacterGenderId);
	FirstPersonEyeHeight = Profile.FallbackEyeHeight;
	FirstPersonForwardOffset = Profile.ForwardOffset;
    const float FallbackCapsuleHalfHeight = GetCapsuleComponent()
        ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.0f;
    CharacterInspectionHeight = FWowCharacterInspectionCameraPolicy::BodyFocusHeight(
        -FallbackCapsuleHalfHeight, FallbackCapsuleHalfHeight + FirstPersonEyeHeight);

	if (USkeletalMeshComponent* CharacterMesh = GetMesh();
		CharacterMesh && CharacterMesh->GetSkeletalMeshAsset())
	{
		CharacterMesh->UpdateBounds();
		const FBoxSphereBounds& Bounds = CharacterMesh->Bounds;
		const float ModelHeight = Bounds.BoxExtent.Z * 2.0f;
		if (ModelHeight >= 80.0f && ModelHeight <= 1000.0f)
		{
			const float ModelBottomZ = Bounds.Origin.Z - Bounds.BoxExtent.Z - GetActorLocation().Z;
            CharacterInspectionHeight = FWowCharacterInspectionCameraPolicy::BodyFocusHeight(ModelBottomZ, ModelHeight);
            const float ModelTopZ = Bounds.Origin.Z + Bounds.BoxExtent.Z;
			const float BoundsEyeHeight = ModelTopZ - GetActorLocation().Z -
				ModelHeight * Profile.TopClearanceFraction;
			const float CapsuleHalfHeight = GetCapsuleComponent()
				? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.0f;
			// Eye height is measured from the capsule centre. The lower limit was +0.65 x half
			// height (57 cm above the centre, ~1.45 m above the feet for every race), so gnomes
			// (~1 m tall) and dwarves looked out from a human's eye level (Stefan, r239). Now only
			// kept at least 40 cm above the feet.
			FirstPersonEyeHeight = FMath::Clamp(BoundsEyeHeight,
				-CapsuleHalfHeight + 40.0f, FMath::Max(250.0f, CapsuleHalfHeight * 3.0f));
		}

		FirstPersonHeadBoneName = CharacterMesh->GetSocketBoneName(WowFirstPersonHeadSocketName);
		bHaveRefHeadHeight = false;
		SmoothedHeadDrop = 0.0f;
		CameraModelHeight = ModelHeight;
		if (!FirstPersonHeadBoneName.IsNone())
		{
			// r509 (Stefan: two trolls of the same height, but zoomed in he looked out from far above
			// his own head): the bounds are the upright bind pose plus the HD hair (troll male 287 cm),
			// so the eye sat at the hair tips and the close-zoom body focus (72 % of that) above the
			// head of a hunched troll. Measure from the head bone in the bind pose instead; the eye may
			// be at most 15 cm above it. Tick then follows how far the animated head drops below it.
			const int32 HeadBone = CharacterMesh->GetBoneIndex(FirstPersonHeadBoneName);
			// r515 (Stefan: the camera went under the world on the r514 build; log: refHead=-78.8): r509 used
			// GetRefPosePosition, which is the head bone's offset from its PARENT bone, not its place in the
			// mesh. Build the bind-pose transform up the bone chain instead, and only trust a head that lies
			// in the upper half of the model; otherwise keep the bounds-based heights.
			FVector HeadLocation = CharacterMesh->GetSocketLocation(WowFirstPersonHeadSocketName);
			if (HeadBone != INDEX_NONE && CharacterMesh->GetSkinnedAsset())
			{
				const FReferenceSkeleton& RefSkeleton = CharacterMesh->GetSkinnedAsset()->GetRefSkeleton();
				FTransform BindPose = FTransform::Identity;
				for (int32 Bone = HeadBone; Bone != INDEX_NONE && Bone < RefSkeleton.GetRefBonePose().Num(); Bone = RefSkeleton.GetParentIndex(Bone))
					BindPose = BindPose * RefSkeleton.GetRefBonePose()[Bone];
				HeadLocation = CharacterMesh->GetComponentTransform().TransformPosition(BindPose.GetLocation());
			}
			const float HeadAboveFeet = float(HeadLocation.Z - (Bounds.Origin.Z - Bounds.BoxExtent.Z));
			if (HeadBone != INDEX_NONE && !HeadLocation.ContainsNaN()
				&& HeadAboveFeet > ModelHeight * 0.45f && HeadAboveFeet < ModelHeight * 1.02f)
			{
				RefHeadHeight = float(HeadLocation.Z - GetActorLocation().Z);
				// r604 (Stefan, undead male in first person: a blurred shoulder filled the lower left of the
				// view): the hunched undead's bounds put the eye at 100.9, below its own head bone (103.6) -
				// at neck height, between the raised shoulders. The eyes sit above the head bone's pivot.
				FirstPersonEyeHeight = FMath::Clamp(FirstPersonEyeHeight, RefHeadHeight + 7.0f, RefHeadHeight + 15.0f);
				CharacterInspectionHeight = FMath::Min(CharacterInspectionHeight, RefHeadHeight - 10.0f);
				bHaveRefHeadHeight = true;
			}
			FirstPersonHeadToEyeOffset = FMath::Clamp(
				GetActorLocation().Z + FirstPersonEyeHeight - HeadLocation.Z,
				-60.0f, 90.0f);
		}

		UE_LOG(LogWowPlayerChar, Log,
			TEXT("First-person camera profile: race=%u gender=%u eye=%.1f nose=%.1f modelHeight=%.1f refHead=%.1f body=%.1f"),
			CharacterRaceId, CharacterGenderId, FirstPersonEyeHeight,
			FirstPersonForwardOffset, ModelHeight, bHaveRefHeadHeight ? RefHeadHeight : -1.0f, CharacterInspectionHeight);
	}
	else
	{
		UE_LOG(LogWowPlayerChar, Verbose,
			TEXT("First-person camera fallback: race=%u gender=%u eye=%.1f nose=%.1f"),
			CharacterRaceId, CharacterGenderId, FirstPersonEyeHeight, FirstPersonForwardOffset);
	}

	// r515: whatever the measurement said, the eye and the close-zoom pivot never go below the
	// middle of the capsule (a pivot under the ground sent the camera under the world).
	if (const float Half = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.0f; Half > 0.0f)
	{
		FirstPersonEyeHeight = FMath::Max(FirstPersonEyeHeight, 0.0f);
		CharacterInspectionHeight = FMath::Max(CharacterInspectionHeight, 0.0f);
	}
	BaseFirstPersonEyeHeight = FirstPersonEyeHeight;
	BaseCharacterInspectionHeight = CharacterInspectionHeight;
	UpdateCameraOffsetsForDistance();
	bFirstPersonVisualStateInitialized = false;
}

bool AWowPlayerCharacter::TryGetAnimatedFirstPersonEyeLocation(FVector& OutLocation) const
{
	const USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (!CharacterMesh || FirstPersonHeadBoneName.IsNone() ||
		!CharacterMesh->DoesSocketExist(WowFirstPersonHeadSocketName))
	{
		return false;
	}

	const FVector HeadLocation = CharacterMesh->GetSocketLocation(WowFirstPersonHeadSocketName);
	if (HeadLocation.ContainsNaN())
	{
		return false;
	}

	// Follow animated head translation, but keep view rotation under player
	// control so run-cycle head rotation does not cause unwanted camera roll.
	OutLocation = HeadLocation + FVector::UpVector * FirstPersonHeadToEyeOffset +
		GetActorForwardVector() * FirstPersonForwardOffset;
	return true;
}

void AWowPlayerCharacter::UpdateFirstPersonVisualState(bool bFirstPerson, float CameraDistance)
{
	// First-person immersion (body visible, head hidden) on the monitor and in VR alike (r222,
	// Stefan): "VR: first person only" switches immersion on with it; with immersion off the
	// close-in view is the classic one in VR too.
	const bool bImmersionRequested = FWowClientPreferences::IsFirstPersonImmersionEnabled() ||
		(FWowVRSupport::IsActive() && FWowClientPreferences::GetWorldOption(TEXT("vrfirstperson")));
	const bool bHelmetViewRequested = FWowClientPreferences::IsFirstPersonHelmetViewEnabled();
	const bool bCanAnchorToHead = bNativeFallbackVisuals || !FirstPersonHeadBoneName.IsNone();
	const bool bUseImmersion = bFirstPerson && bImmersionRequested && bCanAnchorToHead;
	UpdateHeadShadowProxy(this, bUseImmersion && !FirstPersonHeadBoneName.IsNone());
	// Close zoom keeps the body solid (the background goes out of focus instead, see
	// ApplyCameraFocus); only a camera about to enter the head fades it out.
	// The zoom notch just before first person (~50 cm) shows no body at all: a faint
	// see-through figure read as a ghost. One notch further out (~100 cm) is fully solid.
	constexpr float FadeStartDistance = 85.0f;
	constexpr float FadeEndDistance = 70.0f;
	const float BodyOpacity = bUseImmersion
		? 1.0f
		: FMath::Clamp((CameraDistance - FadeEndDistance) /
			(FadeStartDistance - FadeEndDistance), 0.0f, 1.0f);
	if (bFirstPersonVisualStateInitialized &&
		bLastFirstPersonVisualState == bFirstPerson &&
		bLastFirstPersonImmersionState == bUseImmersion &&
		bLastFirstPersonHelmetViewState == bHelmetViewRequested &&
		FMath::IsNearlyEqual(LastFirstPersonOpacity, BodyOpacity, 0.002f))
	{
		return;
	}

	bFirstPersonVisualStateInitialized = true;
	bLastFirstPersonVisualState = bFirstPerson;
	bLastFirstPersonImmersionState = bUseImmersion;
	bLastFirstPersonHelmetViewState = bHelmetViewRequested;
	LastFirstPersonOpacity = BodyOpacity;

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(this);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (PrimitiveComponent && !PrimitiveComponent->ComponentHasTag(TEXT("WowTargetRing")))
		{
			// Not on widget cards (VR pointer, nameplates): a widget component is a mesh
			// component, and SetScalarParameterValueOnMaterials put a dynamic material in its
			// slot 0; the widget then built its own dynamic material from that one ("... is
			// not a valid parent") and drew the default checkerboard - Stefan's black square
			// (r223). Widget materials have no FirstPersonOpacity parameter anyway.
			UMeshComponent* MeshComponent = Cast<UMeshComponent>(PrimitiveComponent);
			if (MeshComponent && !MeshComponent->IsA<UWidgetComponent>())
			{
				MeshComponent->SetScalarParameterValueOnMaterials(
					TEXT("FirstPersonOpacity"), BodyOpacity);
			}
			const bool bHideFromOwner = !bUseImmersion && BodyOpacity <= 0.001f;
			PrimitiveComponent->SetOwnerNoSee(bHideFromOwner);
			SetHiddenFromOwnReflections(PrimitiveComponent, bHideFromOwner);
		}
	}

	TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes(this);
	for (USkeletalMeshComponent* SkeletalMesh : SkeletalMeshes)
	{
		if (!SkeletalMesh || FirstPersonHeadBoneName.IsNone() ||
			SkeletalMesh->GetBoneIndex(FirstPersonHeadBoneName) == INDEX_NONE)
		{
			continue;
		}

		if (bUseImmersion)
		{
			SkeletalMesh->HideBoneByName(FirstPersonHeadBoneName, PBO_None);
		}
		else
		{
			SkeletalMesh->UnHideBoneByName(FirstPersonHeadBoneName);
		}
	}

	if (FallbackHead)
	{
		FallbackHead->SetOwnerNoSee(bFirstPerson);
	}

	// Head and shoulder armor are separate static meshes and are not affected by
	// hiding the skeletal head bone. Keep them equipped in the shared character
	// model, but suppress them only for the owning first-person viewport so the
	// camera cannot enter their shells during strafing animations.
	TInlineComponentArray<UStaticMeshComponent*> StaticMeshes(this);
	for (UStaticMeshComponent* StaticMesh : StaticMeshes)
	{
		if (!StaticMesh)
		{
			continue;
		}

		if (StaticMesh->ComponentHasTag(TEXT("WowHeadEquipment")))
		{
			const bool bHide = FWowFirstPersonCameraPolicy::ShouldHideHeadEquipmentFromOwner(
				bFirstPerson, bHelmetViewRequested) || BodyOpacity <= 0.001f;
			StaticMesh->SetOwnerNoSee(bHide);
			SetHiddenFromOwnReflections(StaticMesh, bHide);
		}
		else if (StaticMesh->ComponentHasTag(TEXT("WowShoulderEquipment")))
		{
			const bool bHide = bFirstPerson || BodyOpacity <= 0.001f;
			StaticMesh->SetOwnerNoSee(bHide);
			SetHiddenFromOwnReflections(StaticMesh, bHide);
		}
	}
}

void AWowPlayerCharacter::SetHiddenFromOwnReflections(UPrimitiveComponent* Component, bool bHide)
{
	// Owner-no-see only removes a primitive from the main view. Lumen (distance
	// fields / ray tracing) and reflection captures still saw the first-person
	// weapon and helmet, so a floating sword showed up in the water.
	if (!Component) return;
	const TWeakObjectPtr<UPrimitiveComponent> Key(Component);
	if (bHide)
	{
		if (FirstPersonReflectionState.Contains(Key)) return;
		const uint8 Bits = (Component->bVisibleInReflectionCaptures ? 1 : 0)
			| (Component->bVisibleInRealTimeSkyCaptures ? 2 : 0)
			| (Component->bVisibleInRayTracing ? 4 : 0)
			| (Component->bAffectDistanceFieldLighting ? 8 : 0);
		FirstPersonReflectionState.Add(Key, Bits);
		Component->bVisibleInReflectionCaptures = false;
		Component->bVisibleInRealTimeSkyCaptures = false;
		Component->SetVisibleInRayTracing(false);
		Component->SetAffectDistanceFieldLighting(false);
		Component->MarkRenderStateDirty();
	}
	else if (const uint8* Bits = FirstPersonReflectionState.Find(Key))
	{
		Component->bVisibleInReflectionCaptures = (*Bits & 1) != 0;
		Component->bVisibleInRealTimeSkyCaptures = (*Bits & 2) != 0;
		Component->SetVisibleInRayTracing((*Bits & 4) != 0);
		Component->SetAffectDistanceFieldLighting((*Bits & 8) != 0);
		Component->MarkRenderStateDirty();
		FirstPersonReflectionState.Remove(Key);
	}
}

void AWowPlayerCharacter::UpdateCameraOffsetsForDistance()
{
	if (!CameraBoom)
	{
		return;
	}

    const float Zoom = CameraBoom->TargetArmLength;
    const float FirstPersonBlend = bUseCharacterCameraFraming
        ? FWowCharacterInspectionCameraPolicy::EyeBlend(Zoom)
        : 1.0f - FMath::Clamp((Zoom - 20.0f) / 280.0f, 0.0f, 1.0f);
    const float PivotHeight = bUseCharacterCameraFraming
        ? FWowCharacterInspectionCameraPolicy::PivotHeight(Zoom, ThirdPersonPivotHeight,
            CharacterInspectionHeight, FirstPersonEyeHeight)
        : FMath::Lerp(ThirdPersonPivotHeight, FirstPersonEyeHeight, FirstPersonBlend);
    CameraBoom->TargetOffset = FVector(0.0f, 0.0f, PivotHeight);
	// Just outside first person, cancel the remaining short arm so crossing the
	// 20 cm threshold does not jump from behind the face to the nose.
	CameraBoom->SocketOffset = FVector(
		FMath::Lerp(0.0f, FirstPersonForwardOffset + 20.0f, FirstPersonBlend), 0.0f, 0.0f);
}

void AWowPlayerCharacter::OnZoom(const FInputActionValue& Value)
{
	if (AWowGameplayController* PC = Cast<AWowGameplayController>(GetController()); PC && PC->IsPointerOverUiForWheel())
	{
		PC->HandleUiWheel(Value.Get<float>()); // r262: e.g. zoom the dressing room model
		return; // r332
	}
	float Delta = Value.Get<float>();
	if (FWowIsometricCamera::IsActive()) { FWowIsometricCamera::Zoom(Delta); return; } // r610
	float Current = CameraBoom->TargetArmLength;
	CameraBoom->TargetArmLength = FMath::Clamp(Current - Delta * 50.0f, MinCameraDistance, MaxCameraDistance);
	UpdateCameraOffsetsForDistance();
}

void AWowPlayerCharacter::OnToggleWalk()
{
	bIsWalking = !bIsWalking;

	UCharacterMovementComponent* Mov = GetCharacterMovement();
	if (Mov)
	{
		Mov->MaxWalkSpeed = bIsWalking ? RunSpeed * WalkSpeedFactor : RunSpeed;
	}

	UE_LOG(LogWowPlayerChar, Log, TEXT("Movement mode: %s (speed: %.0f cm/s)"),
		bIsWalking ? TEXT("Walk") : TEXT("Run"),
		Mov ? Mov->MaxWalkSpeed : 0.0f);
}

void AWowPlayerCharacter::OnToggleAutoRun()
{
	bIsAutoRunning = !bIsAutoRunning;
	UE_LOG(LogWowPlayerChar, Log, TEXT("Auto-run: %s"), bIsAutoRunning ? TEXT("ON") : TEXT("OFF"));
}

void AWowPlayerCharacter::ApplyServerSpeeds(float ServerRunSpeed, float ServerWalkSpeed)
{
	RunSpeed = ServerRunSpeed * 100.0f; // WoW units â†’ cm
	WalkSpeedFactor = (ServerRunSpeed > 0.0f) ? (ServerWalkSpeed / ServerRunSpeed) : 0.357f;

	ApplyGameplayMovementSettings(false);
}

void AWowPlayerCharacter::ApplyGameplayMovementSettings(bool bResetVelocity)
{
	if (UCharacterMovementComponent* Mov = GetCharacterMovement())
	{
		Mov->MaxWalkSpeed = bIsWalking ? RunSpeed * WalkSpeedFactor : RunSpeed;
		Mov->MaxSwimSpeed = RunSpeed * SwimSpeedFactor;
		Mov->MaxFlySpeed = RunSpeed * SwimSpeedFactor;
		Mov->GravityScale = WowGameplayGravityScale;
		Mov->JumpZVelocity = WowGameplayJumpZVelocity;
		Mov->AirControl = WowGameplayAirControl;
		Mov->FallingLateralFriction = WowGameplayFallingLateralFriction;
		Mov->BrakingDecelerationFalling = WowGameplayBrakingDecelerationFalling;
		Mov->BrakingDecelerationFlying = WowGameplaySwimCoastDeceleration;
		if (bResetVelocity)
		{
			Mov->Velocity = FVector::ZeroVector;
		}
	}
}

void AWowPlayerCharacter::ApplyLoginSpawn(const FVector& SpawnPos, float OrientationRadians)
{
	// Aerial mode: used for automated verification screenshots (stable top-down view)
	const TCHAR* CmdLine = FCommandLine::Get();
	const bool bIsAutomatedCapture = FParse::Param(FCommandLine::Get(), TEXT("aerialview"));
	const float CapsuleHalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0f;
	FVector StabilizedPos = SpawnPos + FVector(0.0f, 0.0f, CapsuleHalfHeight + 2.0f);
	const float YawDegrees = FWowCoordinate::WowOrientationToUEYaw(OrientationRadians);
	float OverrideUEX = 0.0f;
	float OverrideUEY = 0.0f;
	float OverrideUEZ = 0.0f;
	const bool bHasExplicitUESpawn =
		FParse::Value(CmdLine, TEXT("-spawnuex="), OverrideUEX) &&
		FParse::Value(CmdLine, TEXT("-spawnuey="), OverrideUEY) &&
		FParse::Value(CmdLine, TEXT("-spawnuez="), OverrideUEZ);

	float OverrideYawDegrees = YawDegrees;
	FParse::Value(CmdLine, TEXT("-spawnyaw="), OverrideYawDegrees);

	if (bIsAutomatedCapture)
	{
		// Verification screenshots need a stable aerial view
		StabilizedPos.Z += 20000.0f;
	}
	if (bHasExplicitUESpawn)
	{
		StabilizedPos = FVector(OverrideUEX, OverrideUEY, OverrideUEZ);
	}

	SetActorLocation(StabilizedPos, false, nullptr, ETeleportType::TeleportPhysics);
	bHasLastWaterRippleLocation = false;
	SetActorRotation(FRotator(0.0f, OverrideYawDegrees, 0.0f));

	// Set controller rotation so spring arm follows
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		float Pitch = bIsAutomatedCapture ? -80.0f : -24.0f;
		float OverrideCameraYaw = OverrideYawDegrees;
		FParse::Value(CmdLine, TEXT("-camerapitch="), Pitch);
		FParse::Value(CmdLine, TEXT("-camerayaw="), OverrideCameraYaw);
		PC->SetControlRotation(FRotator(Pitch, OverrideCameraYaw, 0.0f));
	}
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = bIsAutomatedCapture ? 4500.0f : 760.0f;
		ThirdPersonPivotHeight = bIsAutomatedCapture ? 0.0f : 180.0f;
		CameraBoom->TargetOffset = FVector(0.0f, 0.0f, ThirdPersonPivotHeight);
		FParse::Value(CmdLine, TEXT("-cameradistance="), CameraBoom->TargetArmLength);
		bUseCharacterCameraFraming = !bIsAutomatedCapture;
        float CameraOffsetZ = ThirdPersonPivotHeight;
		if (FParse::Value(CmdLine, TEXT("-cameraoffsetz="), CameraOffsetZ))
		{
			ThirdPersonPivotHeight = CameraOffsetZ;
            bUseCharacterCameraFraming = false;
		}
		RefreshFirstPersonCameraProfile();
	}

	if (UCharacterMovementComponent* Mov = GetCharacterMovement())
	{
		if (bIsAutomatedCapture)
		{
			// Automated captures: keep flying mode for stable aerial screenshots
			Mov->GravityScale = 0.0f;
			Mov->Velocity = FVector::ZeroVector;
			Mov->SetMovementMode(MOVE_Flying);
		}
		else
		{
			// Gameplay mode: enable gravity and terrain following
			ApplyGameplayMovementSettings(true);
			Mov->SetMovementMode(MOVE_Walking);
			UE_LOG(LogWowPlayerChar, Log, TEXT("Login spawn: gravity enabled, walking mode active"));
		}
	}
}

void AWowPlayerCharacter::SetCharacterModel(UWorld* World, FMpqManager* Mpq, FWowAssetCache* Cache,
	uint8 Race, uint8 Gender, uint8 SkinColor, uint8 Face, uint8 HairStyle, uint8 HairColor, uint8 FacialHair)
{
	CharacterRaceId = Race;
	CharacterGenderId = Gender;
	if (!World || !Mpq || !Cache)
	{
		UE_LOG(LogWowPlayerChar, Warning, TEXT("SetCharacterModel: Missing required parameters"));
		return;
	}

	UE_LOG(LogWowPlayerChar, Log, TEXT("Setting character model: Race=%d Gender=%d Skin=%d Face=%d Hair=%d/%d FacialHair=%d"),
		Race, Gender, SkinColor, Face, HairStyle, HairColor, FacialHair);

	// r481: drop the previous model's equipment too; it is socketed to bones the new skeleton may lack.
	TArray<UStaticMeshComponent*> OldEquipment;
	GetComponents<UStaticMeshComponent>(OldEquipment);
	for (UStaticMeshComponent* Old : OldEquipment)
		if (Old && Old->GetName().StartsWith(TEXT("CharacterEquipment_"))) Old->DestroyComponent();
	TArray<UWowM2ParticleComponent*> OldEffects;
	GetComponents<UWowM2ParticleComponent>(OldEffects);
	for (UWowM2ParticleComponent* Effect : OldEffects)
		if (Effect && Effect->ComponentHasTag(TEXT("WowEquipmentEffect"))) { Effect->ClearParticles(); Effect->DestroyComponent(); }

	// Remove any existing skeletal mesh components (except the default one)
	TArray<USkeletalMeshComponent*> ExistingMeshes;
	GetComponents<USkeletalMeshComponent>(ExistingMeshes);
	for (USkeletalMeshComponent* ExistingMesh : ExistingMeshes)
	{
		if (ExistingMesh != GetMesh()) // Keep the default character mesh component
		{
			ExistingMesh->DestroyComponent();
		}
	}

	// Build character parameters
	FWowCharacterBuilder::FCharacterParams Params;
	Params.Race = static_cast<FWowCharacterBuilder::ERace>(Race);
	Params.Gender = static_cast<FWowCharacterBuilder::EGender>(Gender);
	Params.Customization.RaceId = Race;
	Params.Customization.Gender = Gender;
	Params.Customization.SkinColor = SkinColor;
	Params.Customization.FaceVariation = Face;
	Params.Customization.HairStyle = HairStyle;
	Params.Customization.HairColor = HairColor;
	Params.Customization.FacialHairStyle = FacialHair;

	// Get character model path
	FString ModelPath = FWowCharacterBuilder::GetCharacterModelPath(Params.Race, Params.Gender);
	if (ModelPath.IsEmpty())
	{
		UE_LOG(LogWowPlayerChar, Warning, TEXT("No model path found for Race=%d Gender=%d"), Race, Gender);
		return;
	}

	// Build composite texture
	UTexture2D* CompositeTex = FWowCharacterTexture::BuildCompositeTexture(Mpq, Cache, Params.Customization);

	// Build skeletal mesh and components using the same M2 actor logic from CharacterBuilder
	// but attach to our existing character instead of creating a new actor

	// TODO: This is a simplified approach - we need to integrate the full character building logic
	// For now, let's at least set the mesh on the existing character mesh component

	// Create a temporary actor to build the character, then extract its components
	FVector TempLocation = GetActorLocation() + FVector(0, 0, -10000); // Spawn far below to avoid visual artifacts
	FRotator TempRotation = GetActorRotation();

	AActor* TempCharacterActor = nullptr;

	// Try the full equipment version first (use simpler params in the old method)
	if (false) // Disable for simple method compatibility
	{
		TempCharacterActor = FWowCharacterBuilder::SpawnCharacterWithEquipment(
			World, Mpq, Cache, Params, TempLocation, TempRotation);
	}

	// Fallback to simple character spawn without equipment if full version fails
	if (!TempCharacterActor)
	{
		UE_LOG(LogWowPlayerChar, Log, TEXT("Falling back to simple character spawn without equipment"));
		TempCharacterActor = FWowCharacterBuilder::SpawnCharacter(
			World, Mpq, Cache, Params.Race, Params.Gender, TempLocation, TempRotation);
	}

	if (!TempCharacterActor)
	{
		UE_LOG(LogWowPlayerChar, Warning, TEXT("Failed to spawn any character for model extraction"));
		return;
	}

	// Extract skeletal mesh components from the temporary actor
	TArray<USkeletalMeshComponent*> TempMeshes;
	TempCharacterActor->GetComponents<USkeletalMeshComponent>(TempMeshes);

	// Get the animation controller from the temp actor first
	UWowAnimationController* TempAnimController = FWowCharacterBuilder::GetAnimationController(TempCharacterActor);

	if (TempMeshes.Num() > 0)
	{
		// Copy the main skeletal mesh to our character's mesh component
		USkeletalMeshComponent* MainMesh = TempMeshes[0];
		if (MainMesh && MainMesh->GetSkeletalMeshAsset())
		{
			GetMesh()->SetSkeletalMesh(MainMesh->GetSkeletalMeshAsset());
			GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);

			// Copy materials
			for (int32 MatIdx = 0; MatIdx < MainMesh->GetNumMaterials(); ++MatIdx)
			{
				GetMesh()->SetMaterial(MatIdx, MainMesh->GetMaterial(MatIdx));
			}

			// Copy animation controller from temp actor
			if (TempAnimController && TempAnimController->IsInitialized())
			{
				UWowAnimationController* PlayerAnimController = NewObject<UWowAnimationController>(this, TEXT("PlayerAnimationController"));

				// Copy all animations from temp controller
				TArray<UAnimSequence*> Animations;
				for (const TObjectPtr<UAnimSequence>& Anim : TempAnimController->GetAllAnimations())
				{
					Animations.Add(Anim.Get());
				}

				PlayerAnimController->Initialize(GetMesh(), Animations);

				// Copy the animation ID mapping
				TMap<int32, int32> IdMap;
				const TMap<int32, TObjectPtr<UAnimSequence>>& TempCache = TempAnimController->GetAnimationCache();
				for (const auto& CachePair : TempCache)
				{
					// Find the index of this anim in AllAnimations
					for (int32 Idx = 0; Idx < Animations.Num(); ++Idx)
					{
						if (Animations[Idx] == CachePair.Value.Get())
						{
							IdMap.Add(CachePair.Key, Idx);
							break;
						}
					}
				}
				PlayerAnimController->SetAnimationIdMap(IdMap);
                PlayerAnimController->SetAnimationLoader(TempAnimController->GetAnimationLoader());
                PlayerAnimController->CopyAuthoredDataFrom(TempAnimController); // r709: jump variations, authored speeds

				SetupAnimationController(PlayerAnimController);
				UE_LOG(LogWowPlayerChar, Log, TEXT("Player animation controller: %d animations, %d ID mappings"),
					Animations.Num(), IdMap.Num());
			}
			else
			{
				// Fallback: use single animation mode with idle animation if available
				if (UAnimSequence* PlayingAnim = Cast<UAnimSequence>(MainMesh->AnimationData.AnimToPlay.Get()))
				{
					GetMesh()->PlayAnimation(PlayingAnim, true);
					UE_LOG(LogWowPlayerChar, Log, TEXT("Playing fallback animation from temp actor"));
				}
				else
				{
					UE_LOG(LogWowPlayerChar, Log, TEXT("No animation available from temp actor"));
				}
			}

			// Position mesh so feet are at capsule bottom
			GetMesh()->SetRelativeLocation(FVector(0, 0, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));

			// Fix M2 model orientation: M2 models face +Y in WoW space, need -90 degree yaw for UE5
			GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
			if (AnimationController)
			{
				AnimationController->CaptureBaseMeshRelativeRotation();
			}

			UE_LOG(LogWowPlayerChar, Log, TEXT("Applied main character mesh to player"));
		}

		// Handle additional meshes (like separate hair) - attach as child components
		for (int32 i = 1; i < TempMeshes.Num(); ++i)
		{
			USkeletalMeshComponent* ExtraMesh = TempMeshes[i];
			if (ExtraMesh && ExtraMesh->GetSkeletalMeshAsset())
			{
				// Create a new skeletal mesh component on our character
				FString ComponentName = FString::Printf(TEXT("CharacterMesh_%d"), i);
				USkeletalMeshComponent* NewMeshComp = NewObject<USkeletalMeshComponent>(this, *ComponentName);
				NewMeshComp->SetupAttachment(GetMesh());
				NewMeshComp->SetSkeletalMesh(ExtraMesh->GetSkeletalMeshAsset());
				NewMeshComp->SetCastShadow(true);
				NewMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				NewMeshComp->SetLeaderPoseComponent(GetMesh()); // Follow main mesh animations
				// r694 (Stefan: the blood elf's hair is gone when she sits): the hair mesh kept its own bounds
				// around the standing head, and the seated head left them, so the hair was culled.
				NewMeshComp->bUseBoundsFromLeaderPoseComponent = true;

				// Copy materials
				for (int32 MatIdx = 0; MatIdx < ExtraMesh->GetNumMaterials(); ++MatIdx)
				{
					NewMeshComp->SetMaterial(MatIdx, ExtraMesh->GetMaterial(MatIdx));
				}

				NewMeshComp->RegisterComponent();
				UE_LOG(LogWowPlayerChar, Log, TEXT("Added additional character mesh component: %s"), *ComponentName);
			}
		}

	}

	// Clean up the temporary actor
	TempCharacterActor->Destroy();
	RefreshFirstPersonCameraProfile();

	UE_LOG(LogWowPlayerChar, Log, TEXT("Character model set successfully"));
}

void AWowPlayerCharacter::SetupAnimationController(UWowAnimationController* AnimController)
{
	this->AnimationController = AnimController;
	UE_LOG(LogWowPlayerChar, Log, TEXT("Animation controller setup for local player character"));
}

void AWowPlayerCharacter::SetCharacterModelWithEquipment(UWorld* World, FMpqManager* Mpq, FWowAssetCache* Cache,
	uint8 Race, uint8 Gender, uint8 SkinColor, uint8 Face, uint8 HairStyle, uint8 HairColor, uint8 FacialHair, const TArray<FWowCharacterEquipment>* Equipment)
{
	CharacterRaceId = Race;
	CharacterGenderId = Gender;
	if (!World || !Mpq || !Cache)
	{
		UE_LOG(LogWowPlayerChar, Warning, TEXT("SetCharacterModelWithEquipment: Missing required parameters"));
		return;
	}

	// Build character parameters
	FWowCharacterBuilder::FCharacterParams Params;
	Params.Race = static_cast<FWowCharacterBuilder::ERace>(Race);
	Params.Gender = static_cast<FWowCharacterBuilder::EGender>(Gender);
	Params.Customization.RaceId = Race;
	Params.Customization.Gender = Gender;
	Params.Customization.SkinColor = SkinColor;
	Params.Customization.FaceVariation = Face;
	Params.Customization.HairStyle = HairStyle;
	Params.Customization.HairColor = HairColor;
	Params.Customization.FacialHairStyle = FacialHair;

	// Map equipment if provided
	if (Equipment)
	{
		Params.BodyEquipment.RaceId = Race;
		Params.BodyEquipment.Gender = Gender;

		// Map equipment slots based on inventory type
		// WoW 3.3.5 equipment slots: https://wowdev.wiki/Inventory_type
		bool bRightHandWeaponTaken = false; // r515
		for (const FWowCharacterEquipment& EquipItem : *Equipment)
		{
			if (EquipItem.DisplayId > 0)
			{
				switch (EquipItem.InventoryType)
				{
				case 1:  // Head / helmet
				{
					FWowCharacterBuilder::FEquipmentSlot Slot;
					Slot.ItemDisplayId = EquipItem.DisplayId;
					Slot.AttachPoint = FWowEquipmentManager::EAttachmentPoint::Helmet;
					Params.Equipment.Add(Slot);
					break;
				}
				case 3:  // Shoulders
				{
					FWowCharacterBuilder::FEquipmentSlot LeftSlot;
					LeftSlot.ItemDisplayId = EquipItem.DisplayId;
					LeftSlot.InventoryType = EquipItem.InventoryType;
					LeftSlot.AttachPoint = FWowEquipmentManager::EAttachmentPoint::LeftShoulder;
					Params.Equipment.Add(LeftSlot);
					FWowCharacterBuilder::FEquipmentSlot RightSlot = LeftSlot;
					RightSlot.AttachPoint = FWowEquipmentManager::EAttachmentPoint::RightShoulder;
					Params.Equipment.Add(RightSlot);
					break;
				}
				case 4:  // Shirt
					Params.BodyEquipment.ShirtDisplayId = EquipItem.DisplayId;
					break;
				case 5:  // Chest
					Params.BodyEquipment.ChestDisplayId = EquipItem.DisplayId;
					Params.BodyEquipment.bIsRobeEquipped = false;
					break;
				case 20: // Robe (hides leg geosets)
					Params.BodyEquipment.ChestDisplayId = EquipItem.DisplayId;
					Params.BodyEquipment.bIsRobeEquipped = true;
					break;
				case 6:  // Waist/Belt
					Params.BodyEquipment.BeltDisplayId = EquipItem.DisplayId;
					break;
				case 7:  // Legs/Pants
					Params.BodyEquipment.PantsDisplayId = EquipItem.DisplayId;
					break;
				case 8:  // Feet/Boots
					Params.BodyEquipment.BootsDisplayId = EquipItem.DisplayId;
					break;
				case 9:  // Wrists/Bracers
					Params.BodyEquipment.BracersDisplayId = EquipItem.DisplayId;
					break;
				case 10: // Hands/Gloves
					Params.BodyEquipment.GlovesDisplayId = EquipItem.DisplayId;
					break;
				case 16: // Cape
					Params.BodyEquipment.CapeDisplayId = EquipItem.DisplayId;
					break;
				case 19: // Tabard
					Params.BodyEquipment.TabardDisplayId = EquipItem.DisplayId;
					if (EquipItem.DisplayId == FWowCharacterTexture::GuildTabardDisplayId) Params.BodyEquipment.GuildTabard = GuildTabardDesign; // r343
					break;
				case 13: // One-hand weapon
				case 14: // Shield
				case 15: // Ranged bow/gun
				case 17: // Two-hand weapon
				case 21: // Main hand
				case 22: // Off hand
				case 23: // Holdable off hand
				case 25: // Thrown
				case 26: // Ranged wand/thrown
				{
					FWowCharacterBuilder::FEquipmentSlot Slot;
					Slot.ItemDisplayId = EquipItem.DisplayId;
                    Slot.EnchantmentVisualId = EquipItem.EnchantAuraId;
					Slot.InventoryType = EquipItem.InventoryType;
					// r515 (Stefan: Groklord's second dagger sheathed on the same hip as the first until he
					// drew and sheathed again): equipment built from the item templates says "one-hand" (13)
					// for both daggers, so both went to the right hand and the right hip. A second one-hand
					// melee weapon is the off hand (equipment slot 16 when the list is in slot order).
					const int32 EquipIndex = int32(&EquipItem - Equipment->GetData());
					const bool bOneHandMelee = EquipItem.InventoryType == 13 || EquipItem.InventoryType == 22;
					const bool bOffHand = EquipItem.InventoryType == 22 || EquipItem.InventoryType == 23
						|| (bOneHandMelee && (EquipIndex == 16 || bRightHandWeaponTaken));
					Slot.AttachPoint = EquipItem.InventoryType == 14
						? FWowEquipmentManager::EAttachmentPoint::ShieldHand
						: (bOffHand
							? FWowEquipmentManager::EAttachmentPoint::LeftHand
							: FWowEquipmentManager::EAttachmentPoint::RightHand);
					if (!bOffHand && (EquipItem.InventoryType == 13 || EquipItem.InventoryType == 17 || EquipItem.InventoryType == 21))
						bRightHandWeaponTaken = true;
					Slot.bRangedWeapon = EquipItem.InventoryType == 15 ||
						EquipItem.InventoryType == 25 || EquipItem.InventoryType == 26;
					Params.Equipment.Add(Slot);
					break;
				}
				}
				UE_LOG(LogWowPlayerChar, Log, TEXT("  Equip slot: invType=%d displayId=%d"), EquipItem.InventoryType, EquipItem.DisplayId);
			}
		}
	}

	UE_LOG(LogWowPlayerChar, Log, TEXT("Setting character model with equipment: Race=%d Gender=%d Chest=%d Pants=%d"),
		Race, Gender, Params.BodyEquipment.ChestDisplayId, Params.BodyEquipment.PantsDisplayId);

	// Remove any existing skeletal mesh components (except the default one)
	TArray<USkeletalMeshComponent*> ExistingMeshes;
	GetComponents<USkeletalMeshComponent>(ExistingMeshes);
	for (USkeletalMeshComponent* ExistingMesh : ExistingMeshes)
	{
		if (ExistingMesh != GetMesh()) // Keep the default character mesh component
		{
			ExistingMesh->DestroyComponent();
		}
	}

    TArray<UWowM2ParticleComponent*> ExistingEffects;
    GetComponents<UWowM2ParticleComponent>(ExistingEffects);
    for (UWowM2ParticleComponent* Effect : ExistingEffects)
        if (Effect && Effect->ComponentHasTag(TEXT("WowEquipmentEffect")))
        {
            Effect->ClearParticles();
            Effect->DestroyComponent();
        }

	TArray<UStaticMeshComponent*> ExistingEquipmentMeshes;
	GetComponents<UStaticMeshComponent>(ExistingEquipmentMeshes);
	for (UStaticMeshComponent* ExistingEquipment : ExistingEquipmentMeshes)
	{
		if (ExistingEquipment && ExistingEquipment->GetName().StartsWith(TEXT("CharacterEquipment_")))
		{
			ExistingEquipment->DestroyComponent();
		}
	}

	// Create a temporary actor to build the character, then extract its components
	FVector TempLocation = GetActorLocation() + FVector(0, 0, -10000); // Spawn far below to avoid visual artifacts
	FRotator TempRotation = GetActorRotation();

	AActor* TempCharacterActor = nullptr;

	// HD model for our own character (Options > Experimental), when the HD data is installed.
	Params.bUseHDModel = FWowClientPreferences::GetWorldOption(TEXT("hdcharacters"));

	// Try the full equipment version first
	if (Params.bUseHDModel || Params.Equipment.Num() > 0 ||
		(Params.BodyEquipment.ChestDisplayId > 0 || Params.BodyEquipment.PantsDisplayId > 0 ||
		 Params.BodyEquipment.BootsDisplayId > 0 || Params.BodyEquipment.GlovesDisplayId > 0))
	{
		TempCharacterActor = FWowCharacterBuilder::SpawnCharacterWithEquipment(
			World, Mpq, Cache, Params, TempLocation, TempRotation);
	}

	// Fallback to simple character spawn without equipment if full version fails
	if (!TempCharacterActor)
	{
		UE_LOG(LogWowPlayerChar, Log, TEXT("Falling back to simple character spawn without equipment"));
		TempCharacterActor = FWowCharacterBuilder::SpawnCharacter(
			World, Mpq, Cache, Params.Race, Params.Gender, TempLocation, TempRotation);
	}

	if (!TempCharacterActor)
	{
		UE_LOG(LogWowPlayerChar, Warning, TEXT("Failed to spawn any character for model extraction"));
		return;
	}

	// Extract skeletal mesh components from the temporary actor
	TArray<USkeletalMeshComponent*> TempMeshes;
	TempCharacterActor->GetComponents<USkeletalMeshComponent>(TempMeshes);

	// Get the animation controller from the temp actor first
	UWowAnimationController* TempAnimController = FWowCharacterBuilder::GetAnimationController(TempCharacterActor);

	if (TempMeshes.Num() > 0)
	{
		// Copy the main skeletal mesh to our character's mesh component
		USkeletalMeshComponent* MainMesh = TempMeshes[0];
		if (MainMesh && MainMesh->GetSkeletalMeshAsset())
		{
			GetMesh()->SetSkeletalMesh(MainMesh->GetSkeletalMeshAsset());
			GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);

			// Copy materials
			for (int32 MatIdx = 0; MatIdx < MainMesh->GetNumMaterials(); ++MatIdx)
			{
				GetMesh()->SetMaterial(MatIdx, MainMesh->GetMaterial(MatIdx));
			}

			// Copy animation controller from temp actor
			if (TempAnimController && TempAnimController->IsInitialized())
			{
				UWowAnimationController* PlayerAnimController = NewObject<UWowAnimationController>(this, TEXT("PlayerAnimationController"));

				// Copy all animations from temp controller
				TArray<UAnimSequence*> Animations;
				for (const TObjectPtr<UAnimSequence>& Anim : TempAnimController->GetAllAnimations())
				{
					Animations.Add(Anim.Get());
				}

				PlayerAnimController->Initialize(GetMesh(), Animations);

				// Copy the animation ID mapping
				TMap<int32, int32> IdMap;
				const TMap<int32, TObjectPtr<UAnimSequence>>& TempCache = TempAnimController->GetAnimationCache();
				for (const auto& CachePair : TempCache)
				{
					// Find the index of this anim in AllAnimations
					for (int32 Idx = 0; Idx < Animations.Num(); ++Idx)
					{
						if (Animations[Idx] == CachePair.Value.Get())
						{
							IdMap.Add(CachePair.Key, Idx);
							break;
						}
					}
				}
				PlayerAnimController->SetAnimationIdMap(IdMap);
                PlayerAnimController->SetAnimationLoader(TempAnimController->GetAnimationLoader());
                PlayerAnimController->CopyAuthoredDataFrom(TempAnimController); // r709: jump variations, authored speeds

				SetupAnimationController(PlayerAnimController);
				UE_LOG(LogWowPlayerChar, Log, TEXT("Player animation controller: %d animations, %d ID mappings"),
					Animations.Num(), IdMap.Num());
			}
			else
			{
				// Fallback: use single animation mode with idle animation if available
				if (UAnimSequence* PlayingAnim = Cast<UAnimSequence>(MainMesh->AnimationData.AnimToPlay.Get()))
				{
					GetMesh()->PlayAnimation(PlayingAnim, true);
					UE_LOG(LogWowPlayerChar, Log, TEXT("Playing fallback animation from temp actor"));
				}
				else
				{
					UE_LOG(LogWowPlayerChar, Log, TEXT("No animation available from temp actor"));
				}
			}

            // Copy geoset section visibility from temp actor mesh
            // (ShowMaterialSection state does not transfer with SetSkeletalMesh)
            int32 HiddenSections = 0;
            for (int32 SecIdx = 0; SecIdx < MainMesh->GetNumMaterials(); ++SecIdx)
            {
                bool bVisible = MainMesh->IsMaterialSectionShown(SecIdx, 0);
                GetMesh()->ShowMaterialSection(SecIdx, SecIdx, bVisible, 0);
                if (!bVisible) ++HiddenSections;
            }
            if (HiddenSections > 0)
            {
                UE_LOG(LogWowPlayerChar, Log, TEXT("Copied geoset visibility: %d sections hidden (robe/equipment)"), HiddenSections);
            }

			// Position mesh so feet are at capsule bottom
			GetMesh()->SetRelativeLocation(FVector(0, 0, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));

			// Fix M2 model orientation: M2 models face +Y in WoW space, need -90 degree yaw for UE5
			GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
			if (AnimationController)
			{
				AnimationController->CaptureBaseMeshRelativeRotation();
			}

			UE_LOG(LogWowPlayerChar, Log, TEXT("Applied main character mesh to player"));
		}

		// Handle additional meshes (like separate hair) - attach as child components
		for (int32 i = 1; i < TempMeshes.Num(); ++i)
		{
			USkeletalMeshComponent* ExtraMesh = TempMeshes[i];
			if (ExtraMesh && ExtraMesh->GetSkeletalMeshAsset())
			{
				// Create a new skeletal mesh component on our character
				FString ComponentName = FString::Printf(TEXT("CharacterMesh_%d"), i);
				USkeletalMeshComponent* NewMeshComp = NewObject<USkeletalMeshComponent>(this, *ComponentName);
				NewMeshComp->SetupAttachment(GetMesh());
				NewMeshComp->SetSkeletalMesh(ExtraMesh->GetSkeletalMeshAsset());
				NewMeshComp->SetCastShadow(true);
				NewMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				NewMeshComp->SetLeaderPoseComponent(GetMesh()); // Follow main mesh animations
				// r694 (Stefan: the blood elf's hair is gone when she sits): the hair mesh kept its own bounds
				// around the standing head, and the seated head left them, so the hair was culled.
				NewMeshComp->bUseBoundsFromLeaderPoseComponent = true;

				// Copy materials
				for (int32 MatIdx = 0; MatIdx < ExtraMesh->GetNumMaterials(); ++MatIdx)
				{
					NewMeshComp->SetMaterial(MatIdx, ExtraMesh->GetMaterial(MatIdx));
				}

				NewMeshComp->RegisterComponent();
				UE_LOG(LogWowPlayerChar, Log, TEXT("Added additional character mesh component: %s"), *ComponentName);
			}
		}

		// Weapons and shields are static meshes socketed to the temporary
		// character. Copy them onto the playable mesh before destroying that actor.
		TArray<USceneComponent*> TempAttachedComponents;
		MainMesh->GetChildrenComponents(true, TempAttachedComponents);
		int32 EquipmentIndex = 0;
		for (USceneComponent* AttachedComponent : TempAttachedComponents)
		{
			UStaticMeshComponent* TempEquipment = Cast<UStaticMeshComponent>(AttachedComponent);
			if (!TempEquipment || !TempEquipment->GetStaticMesh() || TempEquipment->GetAttachParent() != MainMesh)
			{
				continue;
			}

			// r307: a unique name. The previous build's CharacterEquipment_N are only marked for
			// destruction, and NewObject with the same name reuses that dying object.
			const FName ComponentName = MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(),
				FName(*FString::Printf(TEXT("CharacterEquipment_%d"), EquipmentIndex)));
			UStaticMeshComponent* NewEquipment = NewObject<UStaticMeshComponent>(this, ComponentName);
			NewEquipment->SetupAttachment(GetMesh(), TempEquipment->GetAttachSocketName());
			NewEquipment->SetStaticMesh(TempEquipment->GetStaticMesh());
			NewEquipment->SetRelativeTransform(TempEquipment->GetRelativeTransform());
			NewEquipment->ComponentTags = TempEquipment->ComponentTags;
			NewEquipment->SetCastShadow(TempEquipment->CastShadow);
			NewEquipment->SetCollisionEnabled(ECollisionEnabled::NoCollision);

			for (int32 MatIdx = 0; MatIdx < TempEquipment->GetNumMaterials(); ++MatIdx)
			{
				NewEquipment->SetMaterial(MatIdx, TempEquipment->GetMaterial(MatIdx));
			}

			NewEquipment->RegisterComponent();
            AddInstanceComponent(NewEquipment);
            FWowEquipmentManager::RestoreEquipmentEffects(Mpq, Cache, NewEquipment);
			UE_LOG(LogWowPlayerChar, Log, TEXT("Copied equipment mesh %s to socket %s"),
				*NewEquipment->GetName(), *TempEquipment->GetAttachSocketName().ToString());
			++EquipmentIndex;
		}
	}

	// Clean up the temporary actor
	TempCharacterActor->Destroy();
	RefreshFirstPersonCameraProfile();

	UE_LOG(LogWowPlayerChar, Log, TEXT("Character model with equipment set successfully"));
}


void AWowPlayerCharacter::ApplyBoundMovement(FVector2D Input,bool JumpHeld,bool SwimDownHeld)
{
    OnMove(FInputActionValue(Input));
    if(JumpHeld!=bBoundJumpHeld) { bBoundJumpHeld=JumpHeld; if(JumpHeld) OnJumpPressed(); else OnJumpReleased(); }
    if(SwimDownHeld!=bBoundSwimHeld) { bBoundSwimHeld=SwimDownHeld; if(SwimDownHeld) OnSwimDownPressed(); else OnSwimDownReleased(); }
}
void AWowPlayerCharacter::RunBoundMovementCommand(const FString& Command)
{
    if(Command==TEXT("TOGGLEAUTORUN")) OnToggleAutoRun();
    else if(Command==TEXT("TOGGLERUN")) OnToggleWalk();
    else if(Command==TEXT("CAMERAZOOMIN")) OnZoom(FInputActionValue(1.0f));
    else if(Command==TEXT("CAMERAZOOMOUT")) OnZoom(FInputActionValue(-1.0f));
}

void AWowPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    FWowWaterMaterial::SetUnderwaterView(0);
    Super::EndPlay(EndPlayReason);
}


