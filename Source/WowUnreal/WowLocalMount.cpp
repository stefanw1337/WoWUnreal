#include "WowLocalMount.h"
#include "WowCharacterBuilder.h"
#include "WowAnimationController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowLocalMount, Log, All);

namespace
{
    TWeakObjectPtr<ACharacter> GRider;
    TWeakObjectPtr<AActor> GMount;
    uint32 GMountId = 0;
    FVector GMeshRelative = FVector::ZeroVector;
    bool bHaveMeshRelative = false;
    float GSeatLift = 0.f;
    FVector GLastLocation = FVector::ZeroVector;
    float GSpeed = 0.f;
    EWowAnimId GMountAnim = EWowAnimId::Stand;
    double GRiderPoseAt = 0.0;

    void Dismount()
    {
        if (AActor* Mount = GMount.Get()) Mount->Destroy();
        GMount.Reset();
        if (ACharacter* Rider = GRider.Get())
            if (USkeletalMeshComponent* Body = Rider->GetMesh(); Body && bHaveMeshRelative)
                Body->SetRelativeLocation(GMeshRelative);
        bHaveMeshRelative = false;
        GMountId = 0;
    }
}

bool FWowLocalMount::Update(ACharacter* Rider, UWowAnimationController* RiderAnimation, uint32 MountDisplayId,
    FMpqManager* Mpq, FWowAssetCache* Cache, float DeltaTime)
{
    if (GRider.Get() != Rider) { Dismount(); GRider = Rider; }
    if (!Rider || !Rider->GetWorld() || !Rider->GetMesh()) { if (GMountId) Dismount(); return false; }
    if (MountDisplayId != GMountId || (MountDisplayId && !GMount.IsValid()))
    {
        const bool bWasMounted = GMountId != 0;
        Dismount();
        if (bWasMounted && RiderAnimation && !MountDisplayId) RiderAnimation->PlayAnimationById(EWowAnimId::Stand, true);
        GMountId = MountDisplayId;
        if (!MountDisplayId) return false;
        if (!Mpq || !Cache) return false;
        const float HalfHeight = Rider->GetCapsuleComponent() ? Rider->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
        const FVector Feet = Rider->GetActorLocation() - FVector(0, 0, HalfHeight);
        AActor* Mount = FWowCharacterBuilder::SpawnCreatureByDisplayId(Rider->GetWorld(), Mpq, Cache, MountDisplayId,
            Feet, FRotator(0.f, Rider->GetActorRotation().Yaw, 0.f), true);
        if (!Mount)
        {
            UE_LOG(LogWowLocalMount, Warning, TEXT("Mount display %u could not be spawned"), MountDisplayId);
            return false;
        }
        Mount->SetActorEnableCollision(false);
        Mount->AttachToComponent(Rider->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
        Mount->SetActorRelativeLocation(FVector(0, 0, -HalfHeight));
        Mount->SetActorRelativeRotation(FRotator::ZeroRotator);
        GMount = Mount;
        GMeshRelative = Rider->GetMesh()->GetRelativeLocation();
        bHaveMeshRelative = true;
        FVector Origin, Extent;
        Mount->GetActorBounds(false, Origin, Extent, true);
        GSeatLift = FMath::Max(Extent.Z * 1.35f, 100.f);
        GLastLocation = Rider->GetActorLocation();
        GSpeed = 0.f;
        GMountAnim = EWowAnimId::Death; // force the first choice below
        GRiderPoseAt = 0.0;
        USkeletalMeshComponent* MountMesh = Mount->FindComponentByClass<USkeletalMeshComponent>();
        UE_LOG(LogWowLocalMount, Log, TEXT("Mounted on display %u (%s, seat socket %s)"), MountDisplayId, *Mount->GetName(),
            MountMesh && MountMesh->DoesSocketExist(TEXT("WowMountSeat")) ? TEXT("yes") : TEXT("no - above the bounds"));
    }
    AActor* Mount = GMount.Get();
    if (!Mount) return false;

    // The rider's body on the saddle.
    USkeletalMeshComponent* Body = Rider->GetMesh();
    USkeletalMeshComponent* MountMesh = Mount->FindComponentByClass<USkeletalMeshComponent>();
    if (MountMesh && MountMesh->DoesSocketExist(TEXT("WowMountSeat")))
        Body->SetWorldLocation(MountMesh->GetSocketLocation(TEXT("WowMountSeat")));
    else
        Body->SetRelativeLocation(GMeshRelative + FVector(0, 0, GSeatLift));
    const double Now = FPlatformTime::Seconds();
    if (RiderAnimation && Now >= GRiderPoseAt)
    {
        // Spells and emotes may play over the seat; take the pose back now and then.
        RiderAnimation->PlayAnimationById(EWowAnimId::Mount, true);
        GRiderPoseAt = Now + 1.0;
    }

    // The mount moves with the player: stands, runs, or flies (taxi gryphons, flying mounts).
    const FVector Here = Rider->GetActorLocation();
    if (DeltaTime > 0.f) GSpeed = FMath::Lerp(GSpeed, float(FVector::Dist(Here, GLastLocation)) / DeltaTime, 0.2f);
    GLastLocation = Here;
    // Airborne = no ground within 3 m under the mount (a taxi flight moves the player along its path
    // whatever the movement mode says).
    bool bAirborne = false;
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(WowLocalMountGround), false, Rider);
        Query.AddIgnoredActor(Mount);
        FHitResult Hit;
        const FVector Feet = Mount->GetActorLocation();
        bAirborne = !Rider->GetWorld()->LineTraceSingleByChannel(Hit, Feet + FVector(0, 0, 50), Feet - FVector(0, 0, 300), ECC_Visibility, Query);
    }
    EWowAnimId Want = GSpeed > 60.f ? (bAirborne ? EWowAnimId::FlyRun : EWowAnimId::Run) : (bAirborne ? EWowAnimId::FlyStand : EWowAnimId::Stand);
    if (Want != GMountAnim)
        if (UWowAnimationController* MountAnimation = FWowCharacterBuilder::GetAnimationController(Mount))
        {
            // Not every mount has the flying clips: fall back to the ground ones.
            if (!MountAnimation->PlayAnimationById(Want, true))
                MountAnimation->PlayAnimationById(Want == EWowAnimId::FlyRun ? EWowAnimId::Run : EWowAnimId::Stand, true);
            GMountAnim = Want;
        }
    return true;
}
