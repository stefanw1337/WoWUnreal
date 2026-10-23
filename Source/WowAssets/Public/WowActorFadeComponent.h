#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WowActorFadeComponent.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * r374 (Stefan: a looted chest "popped" away instead of fading): fades an actor out like the
 * original client does when an object despawns, then destroys it. Every textured pass is
 * swapped for a translucent copy with the same texture whose opacity runs 1 -> 0; anything
 * without a Layer0Texture is hidden at once. Collision goes off immediately.
 */
UCLASS()
class WOWASSETS_API UWowActorFadeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UWowActorFadeComponent();
    /** Starts the fade; false (actor untouched) when the fade material is unavailable. */
    static bool FadeOutAndDestroy(AActor* Actor, float Seconds = 0.8f);
    static UMaterialInterface* PrepareMaterial();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Fades;
    float Duration = 0.8f;
    float Age = 0.f;
};
