#pragma once
#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "WowNodeGlintComponent.generated.h"

class UStaticMeshComponent;

/**
 * Gathering-node glint: a sharp "+" star that slides along a hard edge of the node
 * model, swells and fades, then reappears on another edge after a short pause.
 * Replaces the soft floating blobs, which never sat on the rock or herb.
 */
UCLASS()
class UWowNodeGlintComponent : public UProceduralMeshComponent
{
    GENERATED_BODY()
public:
    UWowNodeGlintComponent(const FObjectInitializer& ObjectInitializer);
    void InitFromMesh(UStaticMeshComponent* Mesh);
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    static UMaterialInterface* GetGlintMaterial();

private:
    void PickEdge();
    TArray<TPair<FVector, FVector>> Edges; // actor-local
    FVector EdgeA = FVector::ZeroVector, EdgeB = FVector::ZeroVector;
    float Phase = 0.f;     // 0..1 while travelling, <0 while waiting
    float Duration = .75f;
    float Size = 14.f;
    FRandomStream Random;
};
