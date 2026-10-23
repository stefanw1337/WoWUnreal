#pragma once
#include "CoreMinimal.h"

class FMpqManager;
class FWowAssetCache;
class AActor;
class UWorld;
class UStaticMesh;
class USkeletalMesh;
class USkeleton;
class UAnimSequence;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UInstancedStaticMeshComponent;
struct FAdtDoodadPlacement;
struct FM2Data;

/**
 * Manages M2 doodad placement.
 * Supports both UStaticMeshComponent (per-instance) and HISMC (instanced) modes.
 */
class WOWWORLD_API FWowDoodadManager
{
public:
    static bool AddAuthoredCollision(AActor* Owner, UStaticMesh* Mesh, const FM2Data* Data,
        const FString& Path, const FTransform& Transform, uint32 UniqueId, int32 Index);
    /** Parse M2 data from MPQ (cached). Returns shared pointer to parsed data. */
    static TSharedPtr<FM2Data> GetOrParseM2(const FString& M2Path, FMpqManager* Mpq);
    /** Forget parsed M2s, skeletons and animations (after switching the game-data folder). */
    static void ClearCaches();
    /**
     * r431: our replacement for WoW's flat light cards (removed from every model): a soft unshadowed
     * point light at each card of M2Path, coloured like the card. DoodadComponent is the doodad's
     * mesh component (its transform places the card); WMO doodads call this too.
     */
    static void AddLegacyGlowLights(AActor* Owner, USceneComponent* DoodadComponent, const FString& M2Path,
        uint32 UniqueId, int32 Index, const FTransform* DoodadTransform = nullptr);

	/**
	 * Spawn one standalone M2 world object at an authoritative UE transform.
	 * Uses the doodad material/particle/light path rather than character skinning.
	 */
	static AActor* SpawnWorldM2(UWorld* World, const FString& M2Path,
		const FVector& Location, const FRotator& Rotation, float Scale,
		FMpqManager* Mpq, FWowAssetCache* Cache, uint32 UniqueId = 0,
        bool bAnimateSpell = false, bool bLoopSpell = false);

    /** Spawn doodad instances for a tile as UStaticMeshComponents on the parent actor */
    static void SpawnDoodads(AActor* ParentActor, const TArray<FAdtDoodadPlacement>& Placements,
                             const TArray<FString>& DoodadPaths, FMpqManager* Mpq, FWowAssetCache* Cache);

    /** Spawn a single doodad and return the UStaticMeshComponent (for distance-based streaming) */
    static UStaticMeshComponent* SpawnSingleDoodad(AActor* ParentActor, const FAdtDoodadPlacement& Placement,
                                                    const FString& M2Path, FMpqManager* Mpq, FWowAssetCache* Cache);

    /**
     * Spawn all doodads for a tile using HISMC instancing.
     * Groups placements by M2 model and creates one HISMC per unique model.
     * Returns the created HISMC components (caller owns them).
     */
    static TArray<UInstancedStaticMeshComponent*> SpawnDoodadsInstanced(
        AActor* ParentActor, const TArray<FAdtDoodadPlacement>& Placements,
        const TArray<FString>& DoodadPaths, FMpqManager* Mpq, FWowAssetCache* Cache);

    /** Get or create a UStaticMesh from M2 data (cached in asset cache) */
    static UStaticMesh* GetOrCreateStaticMesh(const FString& M2Path, FMpqManager* Mpq, FWowAssetCache* Cache);

    /** Check if an M2 model should use skeletal mesh (has bones + animation data) */
    static bool ShouldUseSkeletalMesh(const FM2Data& Data);

    /** Get or create a USkeletalMesh from animated M2 data (cached in asset cache) */
    /** bForce: build even though general doodad skinning is off (small ambient animals). */
    static USkeletalMesh* GetOrCreateSkeletalMesh(const FString& M2Path, FMpqManager* Mpq, FWowAssetCache* Cache, bool bForce = false);

    /**
     * Ambient animals (birds, butterflies, fish...) placed inside a WMO's doodad sets (r283,
     * Stefan: no moving birds over Stormwind - the city WMO's birds were frozen static meshes).
     * Spawns the same GPU-skinned looping mesh the ADT path uses, attached to Parent at
     * RelativeTransform. Returns nullptr when the model is not an ambient animal or cannot animate.
     */
    static class USkeletalMeshComponent* SpawnAmbientAnimal(AActor* Owner, USceneComponent* Parent, const FString& M2Path,
        const FTransform& RelativeTransform, UStaticMesh* StaticForMaterials, uint32 Seed, FMpqManager* Mpq, FWowAssetCache* Cache);

private:
    /** Cache of parsed M2 data (not UObjects, just geometry) */
    static TMap<FString, TSharedPtr<FM2Data>> ParsedM2Cache;
    static FCriticalSection CacheLock;

    /** Cache of skeletons per M2 model (shared between mesh and animations) */
    static TMap<FString, TWeakObjectPtr<USkeleton>> SkeletonCache;

    /** Cache of animation sequences per M2 model */
    static TMap<FString, TArray<TWeakObjectPtr<UAnimSequence>>> AnimCache;

    /** Build skin file path from M2 path */
    static FString GetSkinPath(const FString& M2Path);

    /** Load a BLP texture from MPQ, with caching */
    static UTexture2D* LoadBlpTexture(const FString& Path, FMpqManager* Mpq, FWowAssetCache* Cache);

    /** Create a UStaticMesh from M2 geometry data */
    static UStaticMesh* CreateStaticMeshFromM2(const FM2Data& Data, const FString& M2Path,
                                                FMpqManager* Mpq, FWowAssetCache* Cache);
};
