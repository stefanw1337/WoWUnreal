#pragma once
#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "WowWaterSedimentComponent.generated.h"

/** Small world-anchored sediment puffs, strictly below the water surface. */
UCLASS()
class WOWWORLD_API UWowWaterSedimentComponent : public UProceduralMeshComponent
{
    GENERATED_BODY()
public:
    UWowWaterSedimentComponent(const FObjectInitializer& Initializer);
    void Disturb(const FVector& Feet, float SurfaceZ);
    virtual void TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction* Function) override;
private:
    struct FPuff { FVector Position=FVector::ZeroVector; float Age=3, Surface=0; };
    TArray<FPuff> Puffs;
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<int32> Indices;
    int32 Next=0;
    bool bBuilt=false;
};
