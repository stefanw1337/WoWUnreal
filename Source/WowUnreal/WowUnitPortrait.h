#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WowUnitPortrait.generated.h"

/** Small, isolated view of an existing unit; no additional character or world loading. */
UCLASS()
class AWowUnitPortrait : public AActor
{
    GENERATED_BODY()
public:
    AWowUnitPortrait();
    class UMaterialInterface* CaptureUnit(AActor* Unit);
    virtual void Tick(float DeltaSeconds) override;
    TWeakObjectPtr<AActor> CapturedUnit;
    double LastRequestedAt = 0.0;
private:
    UPROPERTY() TObjectPtr<class USceneCaptureComponent2D> Capture;
    UPROPERTY() TObjectPtr<class UTextureRenderTarget2D> RenderTarget;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> PortraitMaterial;
    UPROPERTY() TArray<TObjectPtr<class UPoseableMeshComponent>> PoseMeshes;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> EquipmentMeshes;
    TArray<TWeakObjectPtr<class USkeletalMeshComponent>> PoseSources;
    TArray<TWeakObjectPtr<class UStaticMeshComponent>> EquipmentSources;
    TArray<TWeakObjectPtr<class UMaterialInterface>> SourceMaterials;
    TArray<TWeakObjectPtr<class UTexture>> SourceTextures;
    bool RefreshPortraitMeshes(AActor* Unit);
    double LastCapturedAt = -1.0;
};
