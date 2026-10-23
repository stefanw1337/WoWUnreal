#pragma once
#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "GameFramework/Character.h"
#include "ProceduralMeshComponent.h"
#include "WowPlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class UWowAnimationController;
class UStaticMeshComponent;
class AWowAudioManager;
struct FInputActionValue;
class FWowCharacterBuilder;

UCLASS()
class WOWUNREAL_API AWowPlayerCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    AWowPlayerCharacter();
    bool IsViewUnderWater() const { return bViewUnderWater; }
private:
    bool bViewUnderWater = false;
public:
    void ApplyBoundMovement(FVector2D Input, bool JumpHeld, bool SwimDownHeld);
    void RunBoundMovementCommand(const FString& Command);
    bool bBoundJumpHeld=false, bBoundSwimHeld=false;

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void Landed(const FHitResult& Hit) override;
    virtual void NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp,
        bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

    // ── Camera ────────────────────────────────────────────────────────────────
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<USpringArmComponent> CameraBoom;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> FollowCamera;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float MinCameraDistance = 0.0f;  // 0 = first-person eye camera

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float MaxCameraDistance = 3000.0f; // ~30 yards

    // ── Input ─────────────────────────────────────────────────────────────────
    UPROPERTY()
    TObjectPtr<UInputMappingContext> GameplayContext;

    UPROPERTY()
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY()
    TObjectPtr<UInputAction> LookAction;

    UPROPERTY()
    TObjectPtr<UInputAction> JumpAction;

    UPROPERTY()
    TObjectPtr<UInputAction> SwimDownAction;

    UPROPERTY()
    TObjectPtr<UInputAction> ZoomAction;

    UPROPERTY()
    TObjectPtr<UInputAction> ToggleWalkAction;

    UPROPERTY()
    TObjectPtr<UInputAction> AutoRunAction;

    // ── Movement ──────────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float RunSpeed = 700.0f;  // 7.0 WoW units/s × 100 cm/unit

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float WalkSpeedFactor = 0.357f; // 2.5 / 7.0

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float BackpedalFactor = 0.42f; // WoW-style backpedaling is much slower than forward running.

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    bool bIsWalking = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    bool bIsAutoRunning = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float SwimSpeedFactor = 0.67f; // 67% of run speed

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float FallDamageMinHeight = 1500.0f; // ~15 WoW yards

    /** Apply speeds received from the server entity */
    void ApplyServerSpeeds(float ServerRunSpeed, float ServerWalkSpeed);

    /** Reapply gameplay movement tuning after loading/spawn code temporarily disables movement. */
    void ApplyGameplayMovementSettings(bool bResetVelocity = false);

    /** Apply server spawn data */
    void ApplyLoginSpawn(const FVector& SpawnPos, float OrientationRadians);

    /** Set the character's visual model based on race/gender/customization */
    void SetCharacterModel(UWorld* World, class FMpqManager* Mpq, class FWowAssetCache* Cache,
        uint8 Race, uint8 Gender, uint8 SkinColor = 0, uint8 Face = 0, uint8 HairStyle = 0,
        uint8 HairColor = 0, uint8 FacialHair = 0);

    /** Set the character's visual model with equipment */
    void SetCharacterModelWithEquipment(UWorld* World, class FMpqManager* Mpq, class FWowAssetCache* Cache,
        uint8 Race, uint8 Gender, uint8 SkinColor = 0, uint8 Face = 0, uint8 HairStyle = 0,
        uint8 HairColor = 0, uint8 FacialHair = 0, const TArray<struct FWowCharacterEquipment>* Equipment = nullptr);

    /** r343: packed guild tabard design applied to a Guild Tabard (set by the gameplay controller). */
    uint32 GuildTabardDesign = 0;

    /** Setup animation controller for local player character model */
    void SetupAnimationController(class UWowAnimationController* AnimController);

    /** Replace data-driven character art with a playable Engine-primitive stick figure. */
    void EnableNativeFallbackVisuals();
    bool HasNativeFallbackVisuals() const { return bNativeFallbackVisuals; }

    /** Last active local movement input in character-relative X=right, Y=forward space. */
    FVector2D GetCurrentMoveInput2D() const { return CurrentMoveInput2D; }

	/** True while the character's head probe is below a rendered water surface. */
	bool IsHeadSubmerged() const { return bHeadSubmerged; }

	/** True while the lightweight WoW liquid overlap is driving three-dimensional swimming. */
	bool IsInWowWaterSwimmingState() const { return bWowWaterSwimming; }

private:
    void OnMove(const FInputActionValue& Value);
    void OnMoveCompleted(const FInputActionValue& Value);
    void OnLook(const FInputActionValue& Value);
    void OnJumpPressed();
    void OnJumpReleased();
    void OnSwimDownPressed();
    void OnSwimDownReleased();
    void OnZoom(const FInputActionValue& Value);
    void OnToggleWalk();
    void OnToggleAutoRun();
    void UpdateWaterMovementMode(float DeltaTime);
    void UpdateWaterPresentationState(bool bHasWaterVolume, float BestSurfaceZ, float DeltaTime);
    void NotifyWaterSurfaceInteraction(bool bLeavingWater, float WaterSurfaceZ);
    void SpawnWaterRipple(float WaterSurfaceZ, bool bStrongRipple,
        const FVector& MovementDirection = FVector::ZeroVector);
    void UpdateWaterRipples(float DeltaTime);
    AWowAudioManager* FindAudioManager();
	void RefreshFirstPersonCameraProfile();
	void UpdateCameraOffsetsForDistance();
	bool TryGetAnimatedFirstPersonEyeLocation(FVector& OutLocation) const;
	void UpdateFirstPersonVisualState(bool bFirstPerson, float CameraDistance);
	void SetHiddenFromOwnReflections(class UPrimitiveComponent* Component, bool bHide);
	TMap<TWeakObjectPtr<class UPrimitiveComponent>, uint8> FirstPersonReflectionState;

    // Camera orbit state
    float CameraYaw = 0.0f;
    float CameraPitch = -20.0f;
	float ThirdPersonPivotHeight = 180.0f;
    float CharacterInspectionHeight = 40.0f;
    bool bUseCharacterCameraFraming = true;
	float FirstPersonEyeHeight = 120.0f;
	float FirstPersonForwardOffset = 20.0f;
	float FirstPersonHeadToEyeOffset = 0.0f;
	FName FirstPersonHeadBoneName = NAME_None;
	// r509: stand-pose eye/body heights and the head's ref-pose height; the camera follows the animated
	// head's drop below it (hunched trolls, sitting) instead of the upright bind pose.
	float BaseFirstPersonEyeHeight = 120.0f;
	float BaseCharacterInspectionHeight = 40.0f;
	float RefHeadHeight = 0.0f;
	float SmoothedHeadDrop = 0.0f;
	float CameraModelHeight = 0.0f;
	bool bHaveRefHeadHeight = false;
	bool bFirstPersonVisualStateInitialized = false;
	bool bLastFirstPersonVisualState = false;
	bool bLastFirstPersonImmersionState = false;
	bool bLastFirstPersonHelmetViewState = false;
	float LastFirstPersonOpacity = -1.0f;
	uint8 CharacterRaceId = 1;
	uint8 CharacterGenderId = 0;

    // Fall tracking
    float FallStartZ = 0.0f;
    bool bIsFalling = false;

    // Left mouse drag turns character
    bool bLeftMouseTurning = false;

    bool bNativeFallbackVisuals = false;
    float FallbackWalkPhase = 0.0f;
    bool bSwimUpPressed = false;
    bool bSwimDownPressed = false;
    bool bWowWaterSwimming = false;
    bool bWasTouchingWater = false;
	bool bHeadSubmerged = false;
    float UnderwaterVisualBlend = 0.0f;
    float CameraFocusDistance = 0.0f;
    float SelfViewFocusFade = 0.0f;
    FVector LastRenderedCameraLocation = FVector::ZeroVector;
    bool bHasRenderedCameraLocation = false;
    UPROPERTY() TObjectPtr<class UWowWaterSedimentComponent> WaterSediment;
    bool bFocusTestLogged = false;
    /** Seated VR: compose the headset pose onto the computed view (see FWowVRSupport). */
    void ApplyVRHeadPose(struct FMinimalViewInfo& View, bool bFirstPerson, float DeltaTime);
    float VRSmoothedEyeHeight = 0.0f;
    bool bVREyeHeightValid = false;
    bool bVRSeatFadeActive = false;
    float LastWaterInteractionEffectTime = -1000.0f;
    float LastKnownWaterSurfaceZ = -FLT_MAX;
    /** Visual-only swimming: the server keeps the flat surface, the mesh sits lower
        (shoulders at the waterline) and rides the ocean swell; the camera follows part of it. */
    void UpdateSwimVisuals(float DeltaTime);
    bool bInOceanWater = false;
    float WaterDepthMeters = 10.0f;
    float SwimNearSurface = 0.0f;
    float SwimMeshOffset = 0.0f;
    float SwimWaveOffset = 0.0f;
    bool bSwimMeshOffsetApplied = false;
    void UpdateWaterFootsteps(float DeltaTime, bool bWading);
    float WaterStepDistance = 0.0f;
    float LastWaterStepTime = -1000.0f;
    bool bFeetInWater = false;
    float LastWaterWakeTime = -1000.0f;
    FVector LastWaterRippleLocation = FVector::ZeroVector;
    bool bHasLastWaterRippleLocation = false;
    FVector2D CurrentMoveInput2D = FVector2D::ZeroVector;

    UPROPERTY(Transient)
    TObjectPtr<AWowAudioManager> CachedAudioManager;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> FallbackLeftArm;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> FallbackRightArm;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> FallbackLeftLeg;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> FallbackRightLeg;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> FallbackHead;

public:
    // Animation controller for character model (if using WoW character model)
    UPROPERTY()
    TObjectPtr<UWowAnimationController> AnimationController;
};
