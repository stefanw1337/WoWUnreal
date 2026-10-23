#pragma once
#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "WowDeathManager.generated.h"

class UWowConnectionManager;
class SWidget;
class APostProcessVolume;
class UMaterialInterface;
class UMeshComponent;
class AActor;
struct FWowEntity;

/**
 * The world side of death (the dialogs and packets are the FrameXML death flow in
 * WowGameplayControllerDeath.cpp, which keeps FWowPacketHandler::DeathState current):
 *  - dead, not released: the body plays its death animation and stays down, movement is
 *    locked and the world drains to grey;
 *  - ghost: the spirit world (pale, cold, blue-white, everything desaturated), our own spirit
 *    and other ghosts glow with a soft rim, and corpses lie where they died;
 *  - alive again: everything fades back.
 */
UCLASS()
class WOWUNREAL_API UWowDeathManager : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(UWowConnectionManager* ConnectionMgr, TSharedPtr<SWidget> RootWidget);
    void Shutdown();
    void Tick(float DeltaTime);

    /** Corpse lying down, not released yet: the local animation and movement stay locked. */
    bool IsLocalCorpse() const { return bLocalDead; }
    bool IsLocalGhost() const { return bLocalGhost; }

    /** Cooks M_WowGhostOverlay (WowPrepareStandalone). */
    static void PrepareStandaloneMaterials();
    /** r413: the spirit rim overlay, also for ghost characters in character selection. */
    static UMaterialInterface* GetGhostOverlayMaterial();
    /** r414: draws every textured pass of the actor's meshes as see-through spirit (character
     *  selection; no way back - the preview actor is rebuilt for every character). */
    static void MakeSpiritBody(AActor* Actor);

private:
    void UpdateLocalBody(bool bDead, bool bGhost);
    void UpdatePostProcess(float DeltaTime);
    void UpdateGhostOverlays();
    void UpdateCorpses();
    void SetOverlay(AActor* Actor, bool bOn);
    /** r414: see-through spirit body (and back to the real materials). */
    void SetSpiritBody(AActor* Actor, bool bOn);
    AActor* SpawnCorpseActor(const FWowEntity& Corpse);

    UPROPERTY() TObjectPtr<UWowConnectionManager> ConnectionManager;
    UPROPERTY() TObjectPtr<APostProcessVolume> DeathVolume;
    UPROPERTY() TObjectPtr<UMaterialInterface> GhostOverlay;
    UPROPERTY() TMap<uint64, TObjectPtr<AActor>> CorpseActors;
    TSet<TWeakObjectPtr<AActor>> OverlaidActors;
    /** r414: the materials a spirit body replaced, per mesh and slot (nullptr = slot untouched). */
    TMap<TWeakObjectPtr<UMeshComponent>, TArray<UMaterialInterface*>> SpiritOriginals;
    TMap<TWeakObjectPtr<UMeshComponent>, bool> SpiritShadows;
    /** Keeps the replaced materials alive until they go back on. */
    UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> SpiritKept;

    bool bLocalDead = false;
    bool bLocalGhost = false;
    float DeadBlend = 0.f;   // grey world while lying dead
    float GhostBlend = 0.f;  // spirit world
    double NextCorpseScan = 0.0;
};
