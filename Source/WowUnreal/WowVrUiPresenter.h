#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WowVrUiPresenter.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

/**
 * VR presenter for FWowUiVrHost: every UI zone gets its own panel around the seated player.
 * Action bars low in front, chat low left, minimap high right, tracker right, unit frames high
 * left; windows, popups, tooltips and the cursor stay straight ahead. Peripheral panels fade
 * in when looked at and out again otherwise. Panels follow the body's turn, not the head,
 * so they stay put while looking around.
 */
UCLASS()
class UWowVrUiPresenter : public USceneComponent
{
    GENERATED_BODY()
public:
    UWowVrUiPresenter();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void OnUnregister() override;
    static UMaterialInterface* GetPanelMaterial();

private:
    struct FPanel
    {
        TObjectPtr<UProceduralMeshComponent> Mesh;
        TObjectPtr<UMaterialInstanceDynamic> Material;
        float Alpha = 0.f;
    };
    TMap<uint8, FPanel> Panels;
    bool bReady = false;
    void HideAll();
};
