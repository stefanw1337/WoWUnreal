#pragma once
#include "CoreMinimal.h"
#include "Components/PointLightComponent.h"
#include "WowDuskLightComponent.generated.h"
class AWowSkyManager;
UCLASS()
class WOWWORLD_API UWowDuskLightComponent : public UPointLightComponent
{
    GENERATED_BODY()
public:
    UWowDuskLightComponent();
    bool bOutdoor=true;
    float FixtureIntensity=0;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> GlassMaterials;
    static float NightFactor(float Minutes);
    void ApplyTime(float Minutes);

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction* Function) override;
private:
    void UpdateDuskLight();
    TWeakObjectPtr<AWowSkyManager> Sky;
};
