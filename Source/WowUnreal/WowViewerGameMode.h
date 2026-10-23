#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "WowSessionState.h"
#include "WowViewerGameMode.generated.h"

class AWowWorldManager;
class AWowLoginController;
class AWowPlayerCharacter;
class UWowConnectionManager;

UCLASS()
class WOWUNREAL_API AWowViewerGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AWowViewerGameMode();
    virtual void BeginPlay() override;
    virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;

private:
    void SetupPostProcess(UWorld* World);
    AWowWorldManager* SpawnWorldManager(UWorld* World);
    void SpawnGroundPlane(UWorld* World, const FVector& Center, float Size);
    void SpawnDirectionalLight(UWorld* World);

    void SetupDefaultScene(UWorld* World);
    void SetupCharacterTestScene(UWorld* World);
    void SetupTerrainTestScene(UWorld* World);
    void SetupWmoTestScene(UWorld* World);
    void SetupUITestScene(UWorld* World);
    void SetupLoginScene(UWorld* World);
    void SetupNetworkTestScene(UWorld* World);

    /** Local-only connection container used to provide player state to FrameXML. */
    UPROPERTY() TObjectPtr<UWowConnectionManager> OfflineConnectionManager;
    UPROPERTY() TObjectPtr<AWowPlayerCharacter> OfflinePlayerPawn;
    FTimerHandle OfflineGroundTimerHandle;
    void TryFinalizeOfflineSpawn();
    void StartOfflineLocomotionVerification();

    /** Network test scene state */
    UPROPERTY() TObjectPtr<UWowConnectionManager> NetworkTestConnMgr;
    FTimerHandle NetworkTestTimerHandle;
    float NetworkTestElapsed = 0.0f;
    float NetworkTestTimeout = 30.0f;
    bool bNetworkTestComplete = false;
    void NetworkTestTick();
    UFUNCTION() void OnNetworkTestStateChanged(EWowSessionState NewState);
    UFUNCTION() void OnNetworkTestError(const FString& Msg);
    UFUNCTION() void OnNetworkTestRealmList(const TArray<FWowRealmInfo>& Realms);
    UFUNCTION() void OnNetworkTestCharacterList(const TArray<FWowCharacterInfo>& Characters);
};
