#include "WowGroundTiltComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarWowGroundTilt(TEXT("wow.GroundTilt"), 1,
    TEXT("Slope tilt for four-legged creatures and mounts: 1 on, 0 off (for comparing)."));

bool FWowGroundTilt::MakeForQuadruped(AActor* Actor, FWowGroundTilt& Out)
{
    if (!Actor) return false;
    // The body is the skeletal mesh that leads the others (hair, beards and attachments of
    // humanoid NPCs are separate meshes posed by it).
    TInlineComponentArray<USkeletalMeshComponent*> Skeletals(Actor);
    USkeletalMeshComponent* Skeletal = nullptr;
    for (USkeletalMeshComponent* Candidate : Skeletals)
        if (Candidate && Candidate->GetSkinnedAsset() && !Candidate->LeaderPoseComponent.IsValid()) { Skeletal = Candidate; break; }
    if (!Skeletal) return false;
    // Footprint in actor space from the mesh's bounds.
    const FBoxSphereBounds Local = Skeletal->CalcBounds(Skeletal->GetRelativeTransform());
    const float Length = Local.BoxExtent.X, Width = Local.BoxExtent.Y, Height = Local.BoxExtent.Z;
    // Quadrupeds are clearly longer than wide, not much taller than long, and not tiny.
    // Bipeds (a broad dwarf is wider than deep and taller than long) stay upright.
    if (Length < 60.f || Length < Width * 1.3f || Length < Height * 0.8f) return false;
    Out.Mesh = Skeletal;
    for (USkeletalMeshComponent* Candidate : Skeletals)
        if (Candidate && Candidate != Skeletal && Candidate->LeaderPoseComponent.Get() == Skeletal
            && Candidate->GetAttachParent() != Skeletal) Out.Followers.Add(Candidate); // attached ones already follow
    Out.HalfLength = Length * 0.75f;
    Out.HalfWidth = FMath::Max(Width * 0.6f, 20.f);
    return true;
}

bool FWowGroundTilt::Probe(AActor* Owner, const FVector& At, float& OutZ)
{
    UWorld* World = Owner->GetWorld();
    if (!World) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(WowGroundTilt), false, Owner);
    TArray<AActor*> Attached; Owner->GetAttachedActors(Attached, true, true);
    Params.AddIgnoredActors(Attached);
    FHitResult Hit;
    if (!World->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, 180.f), At - FVector(0, 0, 260.f),
        FCollisionObjectQueryParams(ECC_WorldStatic), Params)) return false;
    if (Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(TEXT("WowWaterVolume"))) return false;
    OutZ = Hit.ImpactPoint.Z;
    return true;
}

bool FWowGroundTilt::Tick(AActor* Owner, float DeltaTime)
{
    USkeletalMeshComponent* Body = Mesh.Get();
    if (!Owner || !Body) return false;
    // Recover the untilted transform from the current one, so changes others make to the
    // mesh (animation facing offsets, rescale) are kept rather than overwritten.
    const FQuat Undo = FRotator(Pitch, 0.f, -Roll).Quaternion().Inverse();
    const FQuat BaseRelative = Undo * Body->GetRelativeRotation().Quaternion();
    FVector BaseLocation = Undo.RotateVector(Body->GetRelativeLocation() - FVector(0, 0, Lift));
    // r488 (Stefan: one Ragged Young Wolf walked ~2 m beside its own selection ring and nameplate, and
    // clicks on it hit the ground): the NPC ground conform sets the body's relative Z directly, on top of
    // the tilted transform. Undoing the tilt then turned that Z change into a sideways step
    // (dz * sin(pitch)), and on slopes those steps added up frame after frame until the wolf had wandered
    // off its actor. Nothing moves the body sideways on purpose, so its horizontal place is kept from
    // the first (untilted) frame; only height and rotation are recovered.
    if (!bHaveBaseXY) { BaseXY = FVector2D(BaseLocation.X, BaseLocation.Y); bHaveBaseXY = true; }
    BaseLocation.X = BaseXY.X;
    BaseLocation.Y = BaseXY.Y;
    struct FBase { USkeletalMeshComponent* Mesh; FQuat Rot; FVector Loc; };
    TArray<FBase, TInlineAllocator<4>> FollowerBases;
    for (const TWeakObjectPtr<USkeletalMeshComponent>& Weak : Followers)
        if (USkeletalMeshComponent* F = Weak.Get())
            FollowerBases.Add({F, Undo * F->GetRelativeRotation().Quaternion(), Undo.RotateVector(F->GetRelativeLocation() - FVector(0, 0, Lift))});
    if (!Body->WasRecentlyRendered(0.5f)) return true;

    const FTransform ActorXf = Owner->GetActorTransform();
    const FVector Fwd = ActorXf.GetUnitAxis(EAxis::X), Right = ActorXf.GetUnitAxis(EAxis::Y);
    const float ActorScale = FMath::Max(ActorXf.GetScale3D().X, 0.05f);
    const float Len = HalfLength * ActorScale, Wid = HalfWidth * ActorScale;
    const FVector Feet = ActorXf.TransformPosition(BaseLocation); // mesh origin = feet
    float ZF, ZB, ZL, ZR, ZC;
    float WantPitch = 0.f, WantRoll = 0.f, WantLift = 0.f;
    const bool bGround = CVarWowGroundTilt.GetValueOnGameThread() != 0 && Probe(Owner, Feet, ZC) && FMath::Abs(ZC - Feet.Z) < 120.f; // airborne/flying: stay level
    if (bGround && Probe(Owner, Feet + Fwd * Len, ZF) && Probe(Owner, Feet - Fwd * Len, ZB))
    {
        WantPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(ZF - ZB, 2.f * Len)), -24.f, 24.f);
        // On a crest the centre is above the line between the feet: lower the body to the feet.
        WantLift = FMath::Clamp((ZF + ZB) * 0.5f - ZC, -40.f, 0.f);
        if (Probe(Owner, Feet + Right * Wid, ZR) && Probe(Owner, Feet - Right * Wid, ZL))
            WantRoll = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(ZR - ZL, 2.f * Wid)), -12.f, 12.f);
    }
    const float Speed = FMath::Clamp(DeltaTime * 8.f, 0.f, 1.f);
    Pitch = FMath::Lerp(Pitch, WantPitch, Speed);
    Roll = FMath::Lerp(Roll, WantRoll, Speed);
    Lift = FMath::Lerp(Lift, WantLift, Speed);
    // Actor-space tilt: positive pitch lifts the nose; right side higher = negative UE roll.
    const FQuat Tilt = FRotator(Pitch, 0.f, -Roll).Quaternion();
    Body->SetRelativeLocationAndRotation(Tilt.RotateVector(BaseLocation) + FVector(0, 0, Lift), Tilt * BaseRelative);
    // Everything posed by the body (riders' hair, saddles, NPC beards) moves with it.
    for (const FBase& F : FollowerBases)
        F.Mesh->SetRelativeLocationAndRotation(Tilt.RotateVector(F.Loc) + FVector(0, 0, Lift), Tilt * F.Rot);
    return true;
}
