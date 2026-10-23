#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WowSpellMissile.generated.h"

/** Moves an authored MPQ spell model and triggers impact when it arrives. */
UCLASS()
class WOWUNREAL_API AWowSpellMissile : public AActor
{
    GENERATED_BODY()
public:
    AWowSpellMissile();
    void InitializeAuthored(AActor* Visual, AActor* Target, float Speed,
        TFunction<void()> OnImpact);
    /** r698: a critical hit turns the bolt toward this socket of the target (smoothly, not a jump). */
    void AimAtSocket(FName Socket);
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UPROPERTY() TObjectPtr<AActor> VisualActor;
    TWeakObjectPtr<AActor> TargetActor;
    FVector StartPosition = FVector::ZeroVector;
    FVector TargetOffset = FVector::ZeroVector;
    float TravelDuration = 0.1f;
    float Elapsed = 0.0f;
    TFunction<void()> ImpactCallback;
    TWeakObjectPtr<class USkeletalMeshComponent> AimMesh;
    FName AimSocket = NAME_None;
};
