#pragma once
#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "WowWaterSprayComponent.generated.h"

/** Bounded, project-owned UE spray. No imported water textures or emitters. */
UCLASS()
class WOWWORLD_API UWowWaterSprayComponent : public UProceduralMeshComponent
{
    GENERATED_BODY()
public:
    UWowWaterSprayComponent(const FObjectInitializer& ObjectInitializer);
    /** r711: Along/Length give the fall's lower edge (horizontal direction, length); the mist is spread along it. */
    void InitializeFall(const FVector& Impact, float Width, float Height, const FVector& Along = FVector::ZeroVector, float Length = 0.f);
    /** r741: the falling sheet's centre line in world space, from high up the fall down to its lower edge. When set, the
     *  mist goes where this line first meets rock, ground, a building floor or water - where the water visibly hits. */
    void SetFallPath(const TArray<FVector>& Path) { FallPath = Path; }
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;
private:
    struct FDrop { FVector Position, Velocity; float Age=0, Life=1, Size=1; bool Mist=false; };
    void ResetDrop(FDrop& Drop, bool WarmStart);
    FRandomStream Random{4618};
    TArray<FDrop> Drops;
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<int32> Indices;
    float Radius=150, Rise=350;
    FVector AlongAxis=FVector(1,0,0);
    float HalfLength=0;
    bool bBuilt=false, bImpactResolved=false;
    float FallHeight=0, ResolveDelay=1;
    int32 ResolveAttempts=0;
    TArray<FVector> FallPath; // r741
};
