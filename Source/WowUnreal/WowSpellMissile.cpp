#include "WowSpellMissile.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

AWowSpellMissile::AWowSpellMissile()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("MissileRoot"));
    SetActorEnableCollision(false);
}

void AWowSpellMissile::InitializeAuthored(AActor* Visual, AActor* Target, float Speed,
    TFunction<void()> OnImpact)
{
    if (!IsValid(Visual) || !IsValid(Target) || !FMath::IsFinite(Speed) || Speed <= 0.0f)
    {
        if (Visual) Visual->Destroy();
        Destroy();
        return;
    }
    VisualActor = Visual;
    TargetActor = Target;
    StartPosition = Visual->GetActorLocation();
    const USkeletalMeshComponent* TargetMesh = Target->FindComponentByClass<USkeletalMeshComponent>();
    const FBox TargetBounds = TargetMesh ? TargetMesh->Bounds.GetBox() : Target->GetComponentsBoundingBox(true);
    TargetOffset = TargetBounds.IsValid ? TargetBounds.GetCenter() - Target->GetActorLocation() : FVector::ZeroVector;
    const FVector EndPosition = Target->GetActorLocation() + TargetOffset;
    TravelDuration = FMath::Clamp(FVector::Distance(StartPosition, EndPosition) / Speed, 0.05f, 15.0f);
    SetActorLocationAndRotation(StartPosition, (EndPosition - StartPosition).Rotation());
    Visual->AttachToComponent(RootComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    Visual->SetActorEnableCollision(false);
    TInlineComponentArray<UPrimitiveComponent*> Primitives(Visual);
    for (UPrimitiveComponent* Primitive : Primitives) Primitive->SetCastShadow(false);
    ImpactCallback = MoveTemp(OnImpact);
    SetLifeSpan(16.0f);
}

void AWowSpellMissile::AimAtSocket(FName Socket)
{
    // r698 (Stefan: "make spells or even ranged attacks hit for the mobs head when it's a critical hit"):
    // the crit is only known from the damage log, which can come while the bolt is still flying.
    AActor* Target = TargetActor.Get();
    if (!Target) return;
    const ACharacter* Character = Cast<ACharacter>(Target);
    USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : Target->FindComponentByClass<USkeletalMeshComponent>();
    if (!Mesh || !Mesh->DoesSocketExist(Socket)) return;
    AimMesh = Mesh;
    AimSocket = Socket;
}

void AWowSpellMissile::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!IsValid(VisualActor) || !TargetActor.IsValid()) { Destroy(); return; }
    Elapsed += DeltaTime;
    if (const USkeletalMeshComponent* Mesh = AimMesh.Get())
    {
        // Ease over about a tenth of a second, so the bolt bends up instead of jumping.
        const FVector Wanted = Mesh->GetSocketLocation(AimSocket) - TargetActor->GetActorLocation();
        TargetOffset = FMath::VInterpTo(TargetOffset, Wanted, DeltaTime, 12.0f);
    }
    const FVector TargetPosition = TargetActor->GetActorLocation() + TargetOffset;
    const FVector Position = FMath::Lerp(StartPosition, TargetPosition,
        FMath::Clamp(Elapsed / TravelDuration, 0.0f, 1.0f));
    const FVector Direction = TargetPosition - GetActorLocation();
    SetActorLocationAndRotation(Position, Direction.IsNearlyZero() ? GetActorRotation() : Direction.Rotation());
    if (Elapsed >= TravelDuration)
    {
        TFunction<void()> Callback = MoveTemp(ImpactCallback);
        if (Callback) Callback();
        Destroy();
    }
}

void AWowSpellMissile::EndPlay(const EEndPlayReason::Type Reason)
{
    if (IsValid(VisualActor)) VisualActor->Destroy();
    VisualActor = nullptr;
    ImpactCallback = nullptr;
    Super::EndPlay(Reason);
}
