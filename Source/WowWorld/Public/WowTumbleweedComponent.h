#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WowTumbleweedComponent.generated.h"

class UWowWeatherComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/**
 * Local-only tumbleweeds for dry zones. Purely cosmetic: nothing is sent to the
 * server, other players never see them, and they have no collision at all.
 * They appear ahead of the camera and roll away downwind; when the player gets
 * close they speed off, and if still reached they fade out, so they are never
 * actually caught. Wind drives speed; sandstorms make them race.
 */
UCLASS()
class WOWWORLD_API UWowTumbleweedComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UWowTumbleweedComponent();
    void SetWeather(UWowWeatherComponent* InWeather);
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    static UMaterialInterface* PrepareMaterial();
    /** r367: masked twig material for the M2 tumbleweed with boosted alpha coverage, so thin
        outer twigs stay solid under TAA/TSR instead of dissolving into a see-through halo. */
    static UMaterialInterface* PrepareTwigMaterial();
    static bool IsTumbleweedZone(uint32 ZoneId);

private:
    struct FWeed
    {
        TWeakObjectPtr<UPrimitiveComponent> Visual;
        FVector Position = FVector::ZeroVector;   // ground contact point
        FVector Direction = FVector::ForwardVector;
        FQuat Spin = FQuat::Identity;
        float Speed = 0, Radius = 32, BouncePhase = 0, BounceHeight = 0, GroundZ = 0;
        float Age = 0, Fade = 0, Scale = 1;
        bool bLeaving = false;
        bool bBlocked = false;   // water or a building ahead: stop rolling and fade
        double NextGroundProbe = 0;
    };
    bool Spawn(const FVector& Camera, const FVector& ViewForward, const FVector2D& Wind);
    bool ProbeGround(const FVector& Around, float& OutZ) const;
    /** True in water or under/inside a building (WMO roof overhead). */
    bool IsForbiddenSpot(const FVector& Ground) const;
    /** True if a building wall lies between From and To at rolling height. */
    bool HitsBuilding(const FVector& From, const FVector& To, float Height) const;
    UPrimitiveComponent* CreateVisual(float RadiusCm);
    /** Twig count grows with size and twig thickness stays ~1-2 cm, so big weeds are denser, not coarser. */
    void BuildProceduralWeed(UProceduralMeshComponent* Mesh, float RadiusCm, int32 Seed) const;

    TWeakObjectPtr<UWowWeatherComponent> Weather;
    TArray<FWeed> Weeds;
    FRandomStream Random{551173};
    double NextSpawn = 0;
    bool bResolvedModel = false;
    UPROPERTY() TObjectPtr<UStaticMesh> ModelMesh;
    UPROPERTY() TArray<TObjectPtr<UPrimitiveComponent>> Pool;
    UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> TwigMaterials; // per model slot, shared by the pool
    /** r368: the twig ball as a shared UStaticMesh (per size bucket and variant). The procedural
        mesh wrote no motion vectors, so TAA/TSR smeared the rolling twigs into a see-through halo. */
    UStaticMesh* GetWeedMesh(float RadiusCm, int32 Variant);
    UPROPERTY() TMap<int32, TObjectPtr<UStaticMesh>> WeedMeshes;
};
