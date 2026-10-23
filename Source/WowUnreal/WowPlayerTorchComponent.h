#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WowPlayerTorchComponent.generated.h"

class FMpqManager;
class FWowAssetCache;
class UPointLightComponent;
class USceneComponent;

/**
 * r398 (Stefan): a burning torch on the belt, toggled with the "Toggle Torch" binding (Ctrl-T by
 * default). Only with Options > "Darker, more realistic lighting" on: there the nights are dark
 * enough to need it. The torch is the 3.3.5 item model Club_1H_Torch_A_01 (with its flame
 * particles) on the left hip socket, plus a flickering warm point light at the flame.
 *
 * Local only for now: the server does not know about it, so other players do not see it (that needs
 * the mod-wowunreal server module). On/off is remembered (GameUserSettings, WowUnreal.Torch).
 */
UCLASS(ClassGroup=(Custom))
class WOWUNREAL_API UWowPlayerTorchComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UWowPlayerTorchComponent();

    /** Toggle (key binding). Creates the component on the pawn when needed. */
    static void Toggle(APawn* Pawn, FMpqManager* Mpq, FWowAssetCache* Cache);
    /** Every frame from the gameplay controller: adds the component when the saved state is on. */
    static void Ensure(APawn* Pawn, FMpqManager* Mpq, FWowAssetCache* Cache);
    static bool IsWanted();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void OnUnregister() override;

private:
    void Attach();
    void Detach();

    FMpqManager* Mpq = nullptr;
    FWowAssetCache* Cache = nullptr;
    TWeakObjectPtr<USceneComponent> Torch;
    TWeakObjectPtr<UPointLightComponent> Light;
    TWeakObjectPtr<UObject> AttachedMeshAsset;   // the skeletal mesh the torch was attached to
    FVector FlameLocal = FVector(0, 0, 30);      // flame position in the torch model's space
    FVector FlameAxisLocal = FVector(0, 0, 1);   // handle -> flame direction in the torch's space
    double NextAttachTry = 0.0;
    float FlickerTime = 0.f;
};
