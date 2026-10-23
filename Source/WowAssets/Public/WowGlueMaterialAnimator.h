#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Formats/M2Types.h"
#include "WowGlueMaterialAnimator.generated.h"
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;

UCLASS(Transient)
class WOWASSETS_API UWowGlueMaterialAnimator : public UActorComponent
{
    GENERATED_BODY()
public:
    UWowGlueMaterialAnimator();
    void Add(USkeletalMeshComponent* Mesh, int32 Section, UMaterialInstanceDynamic* Material, const FM2RenderPass& Pass);
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
private:
    struct FEntry
    {
        TWeakObjectPtr<USkeletalMeshComponent> Mesh;
        TWeakObjectPtr<UMaterialInstanceDynamic> Material;
        FM2MaterialTrack Weight,Alpha,Red,Green,Blue;
        int32 Section=0;
        bool bVisible=true;
    };
    TArray<FEntry> Entries;
    double Elapsed=0;
    void Update(FEntry& Entry);
};
