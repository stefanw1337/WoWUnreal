#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "WowFishingLineComponent.generated.h"

/**
 * Fishing line from the caster's rod tip to the bobber (r254, Stefan: no line was drawn).
 * Attached to the bobber actor; rebuilt each frame as a thin sagging cord.
 */
UCLASS(ClassGroup=(Custom))
class WOWUNREAL_API UWowFishingLineComponent : public UProceduralMeshComponent
{
    GENERATED_BODY()

public:
    UWowFishingLineComponent(const FObjectInitializer& ObjectInitializer);

    void SetCaster(AActor* InCaster) { Caster = InCaster; }

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    bool ResolveRodTip(FVector& OutTip) const;

    TWeakObjectPtr<AActor> Caster;
    bool bMaterialSet = false;
};
