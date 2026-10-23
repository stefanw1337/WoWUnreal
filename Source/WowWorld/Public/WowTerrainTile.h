#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Formats/AdtTypes.h"
#include "MeshDescription.h"
#include "WowTerrainTile.generated.h"

struct FAdtData;
class FMpqManager;
class FWowAssetCache;
class UInstancedStaticMeshComponent;
class URuntimeVirtualTexture;
class UStaticMeshComponent;

UCLASS()
class WOWWORLD_API AWowTerrainTile : public AActor
{
    GENERATED_BODY()
public:
    AWowTerrainTile();
    void BuildFromAdtData(const FAdtData& Data, int32 TX, int32 TY, FMpqManager* Mpq, FWowAssetCache* Cache, TSet<uint32>* SpawnedWmoIds = nullptr);
    /** Time-sliced build: 0 = terrain mesh + placement data, 1-4 = water (four bands of
        four chunk rows, r366), 5 = doodads.
        The world manager runs one phase per tick so a tile never stalls a whole frame. */
    // r498 (Stefan: FPS drops on the Durotar coast): an ocean band of 4 chunk rows cost ~90 ms (one
    // frame each, ~10 fps for a second while a sea tile streamed in). One chunk row per band (~22 ms);
    // bands without water are skipped in the same tick (AWowWorldManager::AdvanceTileBuild).
    static constexpr int32 NumWaterBands = 16;
    static constexpr int32 NumBuildPhases = 2 + NumWaterBands;
    void BuildPhase(int32 Phase, const FAdtData& Data, int32 TX, int32 TY, FMpqManager* Mpq, FWowAssetCache* Cache);
    /** r565: phase 0 (the ground) in small steps: geometry, chunk materials 64 at a time, the mesh,
     *  then collision cooked off the game thread. Returns true once the ground mesh is built (its
     *  collision may still be cooking: IsTerrainReady). */
    bool BuildTerrainStep(const FAdtData& Data, int32 TX, int32 TY, FMpqManager* Mpq, FWowAssetCache* Cache, bool bSynchronous = false);
    /** r565: the ground mesh exists and its collision is cooked (safe to stand on). */
    bool IsTerrainReady() const { return bTerrainReady; }
    /** r567: the doodad phase in steps of a few models each (it cost up to 90 ms in one frame).
     *  Returns true once every doodad of the tile is placed. */
    bool BuildDoodadStep(const FAdtData& Data, int32 TX, int32 TY, FMpqManager* Mpq, FWowAssetCache* Cache, bool bSynchronous = false);
    /** r565: the ground mesh is visible (the low-detail stand-in can go). */
    bool IsTerrainMeshBuilt() const { return bTerrainReady || TerrainStep >= 6; }
    void ApplyRuntimeVirtualTexture(URuntimeVirtualTexture* RVT);
    FIntPoint GetTileCoord() const { return TileCoord; }

    // ---- Distance-based streaming data ----
    // Parsed placement data stored for deferred spawning
    TArray<FAdtDoodadPlacement> DoodadPlacements;
    TArray<FString> DoodadPaths;
    TArray<FAdtWmoPlacement> WmoPlacements;
    TArray<FString> WmoPaths;

    // Track which placements are currently spawned (legacy per-instance mode)
    UPROPERTY()
    TMap<uint32, TObjectPtr<UStaticMeshComponent>> SpawnedDoodads; // UniqueId -> component

    UPROPERTY()
    TMap<uint32, TObjectPtr<AActor>> SpawnedWmos; // UniqueId -> actor

    /** HISMC components for instanced doodads (one per unique M2 model) */
    UPROPERTY()
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> InstancedDoodads;

    /** Whether this tile's doodads are using HISMC instancing */
    bool bUsesInstancedDoodads = false;

    // Cached pointers for deferred spawning
    FMpqManager* CachedMpq = nullptr;
    FWowAssetCache* CachedCache = nullptr;

    /** Area IDs per chunk (16x16 = 256 entries, row-major: [chunkY * 16 + chunkX]) */
    TArray<uint32> ChunkAreaIds;

    /** Ice under foot (r230): 0-255 per 8x8 cell of each chunk, [(chunkY*16+chunkX)*64 + cellY*8+cellX];
        empty when the tile has no ice texture at all. Read by the player's movement (slippery ice). */
    TArray<uint8> IceCells;

private:
    void BuildDoodadPhase(const FAdtData& Data, int32 TX, int32 TY, FMpqManager* Mpq, FWowAssetCache* Cache);

    UPROPERTY()
    TObjectPtr<USceneComponent> RootScene;

    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> ChunkMeshes;

    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> WaterMeshes;

    FIntPoint TileCoord;

    // r565: ground build state between steps.
    int32 TerrainStep = 0;
    bool bTerrainReady = false;
    TUniquePtr<FMeshDescription> PendingTerrainMesh;
    TArray<int32> PendingTerrainChunks;           // chunk index of each section, in section order
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInterface>> PendingTerrainMaterials;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> PendingTerrainComponent;
    TWeakObjectPtr<URuntimeVirtualTexture> TerrainRVT;
    // r567: doodad build state between steps.
    bool bDoodadsPrepared = false;
    TArray<FAdtDoodadPlacement> PendingDoodads;
    TArray<FString> PendingDoodadPaths;
    TArray<TArray<int32>> PendingDoodadBatches; // placement indices per step, whole models only
    int32 NextDoodadBatch = 0;
    void ApplyRvtTo(UStaticMeshComponent* Mesh) const;

    UMaterialInterface* GetDefaultTerrainMaterial() const;
};
