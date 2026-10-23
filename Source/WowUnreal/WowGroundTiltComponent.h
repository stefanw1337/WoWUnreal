#pragma once
#include "CoreMinimal.h"

class AActor;
class USkeletalMeshComponent;

/**
 * Four-legged creatures and mounts: pitch/roll the body mesh to follow the ground under its
 * front/back and left/right feet, so legs on a slope are not left hanging in the air.
 * Purely visual; the actor transform (and the server position) are untouched.
 * Plain struct (no UObject): the gameplay controller owns and ticks one per quadruped.
 */
struct FWowGroundTilt
{
    /** Returns true (and fills Out) when the actor's skeletal mesh is long enough to be a quadruped. */
    static bool MakeForQuadruped(AActor* Actor, FWowGroundTilt& Out);
    /** Returns false once the actor or mesh is gone. */
    bool Tick(AActor* Owner, float DeltaTime);
private:
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;          // body (leader)
    TArray<TWeakObjectPtr<USkeletalMeshComponent>> Followers; // hair/attachments posed by the body
    float HalfLength = 100.f, HalfWidth = 50.f;
    float Pitch = 0.f, Roll = 0.f, Lift = 0.f;
    FVector2D BaseXY = FVector2D::ZeroVector; bool bHaveBaseXY = false; // r488: the body's untilted horizontal place
    static bool Probe(AActor* Owner, const FVector& At, float& OutZ);
};
