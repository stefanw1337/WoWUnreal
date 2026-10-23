#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Mpq/MpqManager.h"
#include "WowAssetCache.h"
#include "Formats/WdtTypes.h"
#include "Formats/WdlTypes.h"
#include "Formats/AdtTypes.h"
#include "Async/Future.h"
#include "WowWorldManager.generated.h"

class AWowTerrainTile;
class URuntimeVirtualTexture;
class URuntimeVirtualTextureComponent;

/** Result of an async tile load (MPQ read + ADT parse on background thread) */
struct FPendingTileLoad
{
    uint64 Generation = 0;
    int32 TX = 0;
    int32 TY = 0;
    TFuture<TSharedPtr<FAdtData>> Future;
};

UCLASS()
class WOWWORLD_API AWowWorldManager : public AActor
{
    GENERATED_BODY()
    friend class FWowLoginZoneTest;
public:
    AWowWorldManager();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

    FMpqManager* GetMpqManager() const { return MpqManager.Get(); }
    FWowAssetCache* GetAssetCache() const { return AssetCache.Get(); }
    uint32 GetCurrentAreaId() const { return CurrentAudioAreaId; }
    /** Forget the audio area so the next world entry re-sends zone music/ambience. */
    void ResetAudioArea() { CurrentAudioAreaId = 0; }
    /** r377: while a flyby cinematic plays, terrain streams around its camera instead of the pawn. */
    void SetCinematicStreamingFocus(bool bEnable, const FVector& Location = FVector::ZeroVector)
    { bHasCinematicStreamingFocus = bEnable; CinematicStreamingFocus = Location; }
    uint32 GetCurrentZoneId() const;
    FString GetCurrentAreaName() const;
    FString GetCurrentZoneName() const;
    bool HasExternalData() const { return MpqManager && MpqManager->IsInitialized(); }

    /**
     * Re-open the game data from another folder in place (same FMpqManager object, so every
     * stored pointer stays valid), reload all DBC tables and drop data-derived caches.
     * Only valid before world entry (terrain streaming off). On failure the previous folder
     * is restored and false is returned with a readable reason.
     */
    bool SwitchDataFolder(const FString& NewDataPath, FString& OutError);
    /** Block until the background (Tier 2) DBC load has finished. */
    void WaitForDeferredDbcLoad();

    /** Load WDT/WDL and enable terrain streaming. Call after deferred startup (e.g. login scene). */
    void EnableTerrainStreaming();
    bool SelectMap(uint32 MapId);

    /** Queue initial tiles around a UE world position. Returns how many were queued. */
    int32 LoadTilesAroundPosition(const FVector& UEPosition, int32 Radius = 2);

    /** Keep all streaming systems focused on the destination while a loading screen is active. */
    void BeginWorldEntryWarmup(const FVector& UEPosition);

    /** Return streaming focus to the active camera after the player has entered the world. */
    void EndWorldEntryWarmup();

    /** Number of tiles that have been queued for the initial load */
    int32 GetInitialTilesQueued() const { return InitialTilesQueued; }

    /** Number of initial tiles that have been finalized (ready to render) */
    int32 GetInitialTilesLoaded() const { return InitialTilesLoaded; }

    /** Whether every tracked entry tile is still resident and ready to render/collide. */
    bool IsInitialLoadComplete() const;

    /** True when the full-detail terrain tile at a world position is resident. */
    bool IsTerrainReadyAtPosition(const FVector& WorldPosition) const;

    /** r556: the selected map is a single WMO with no terrain tiles (most dungeons: the Stockade,
     *  the Deadmines, Ragefire Chasm). */
    bool IsWmoOnlyMap() const;

    /** Get the terrain RVT for mesh components to write to */
    URuntimeVirtualTexture* GetTerrainRVT() const { return TerrainRVT; }

    /** HUD info accessors */
    int32 GetLoadedTileCount() const { return LoadedTiles.Num(); }
    /** How much of the terrain at this spot is ice, 0-1 (r230: slippery footing). */
    float GetTerrainIceAt(const FVector& Location) const;
    int32 GetPendingTileCount() const { return PendingTileKeys.Num(); }
    FIntPoint GetCameraTile() const { return LastCameraTile; }
    int32 GetActiveDoodadCount() const { return ActiveDoodadCount; }
    int32 GetActiveWmoGroupCount() const { return ActiveWmoGroupCount; }
    uint64 GetObjectStreamingPassCount() const { return ObjectStreamingPassCount; }
    uint64 GetObjectStreamingRevision() const { return ObjectStreamingRevision; }
    const FString& GetMapName() const { return MapName; }

	/** True when a world position maps to a playable tile in the loaded map WDT. */
	bool IsPositionInsidePlayableWorld(const FVector& WorldPosition) const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW")
    FString MapName = TEXT("Azeroth");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 LoadRadius = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 UnloadRadius = 4;

    /** Radius in tiles for WDL distant terrain (LOD 2) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 WdlRadius = 12;

    /** Distance beyond WdlRadius to unload WDL tiles */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 WdlUnloadRadius = 15;

    /** Radius in tiles for LOD 1 simplified terrain (between LoadRadius and WdlRadius) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 Lod1Radius = 5;

    /** Initial tile to load for testing (Elwynn Forest area) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Debug")
    int32 DebugTileX = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Debug")
    int32 DebugTileY = 48;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Debug")
    bool bStreamingEnabled = false;

    // ---- Object streaming settings ----
    /** Radius (UE cm) within which doodads (M2) are spawned. Default 2000m. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    float DoodadRadius = 200000.0f;

    /** Radius (UE cm) within which WMOs (buildings) are spawned. 600 WoW units. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    float WmoRadius = 60000.0f;

    /** Maximum number of active doodad components. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 MaxActiveDoodads = 200;

    /** Maximum number of active WMO group components. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 MaxActiveWmoGroups = 2000;

    /** Skip WMOs with more groups than this to avoid GPU overload. Stormwind has 286 groups. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 MaxWmoGroupsPerObject = 300;

private:
    TUniquePtr<FMpqManager> MpqManager;
    TUniquePtr<FWowAssetCache> AssetCache;
    TFuture<void> DeferredDbcLoad;
    TUniquePtr<FWdtData> WdtData;
    TUniquePtr<FWdlData> WdlData;

    UPROPERTY()
    TMap<int64, TObjectPtr<AWowTerrainTile>> LoadedTiles;

    /** WDL distant terrain actors (LOD 2) */
    UPROPERTY()
    TMap<int64, TObjectPtr<AActor>> WdlTiles;

    /** LOD 1 simplified terrain tiles (mid-distance) */
    UPROPERTY()
    TMap<int64, TObjectPtr<AActor>> Lod1Tiles;

    /** Runtime Virtual Texture for terrain */
    UPROPERTY()
    TObjectPtr<URuntimeVirtualTexture> TerrainRVT;

    /** RVT volume component defining the virtual texture bounds */
    UPROPERTY()
    TObjectPtr<URuntimeVirtualTextureComponent> RVTVolumeComponent;

    FIntPoint LastCameraTile = FIntPoint(-9999, -9999);

    /** Initial load tracking */
    int32 InitialTilesQueued = 0;
    int32 InitialTilesLoaded = 0;
    TSet<int64> InitialTileKeys;

    /** Recompute entry progress from currently resident tiles, never cumulative reloads. */
    void RefreshInitialTileProgress();

    /** Track spawned WMO unique IDs to avoid duplicates across tiles */
    TSet<uint32> SpawnedWmoIds;

    /** r556: the WDT's global WMO (WMO-only maps), spawned once per map selection. */
    UPROPERTY()
    TObjectPtr<AActor> GlobalWmoActor;
    bool bGlobalWmoTried = false;
    bool bGlobalWmoFailed = false;
    void UpdateGlobalWmo();
    void DestroyGlobalWmo();
    bool IsGlobalWmoReady() const;

    /** Track spawned doodad unique IDs to avoid duplicates across tiles */
    TSet<uint32> SpawnedDoodadIds;

    /** Current count of active doodad mesh components */
    int32 ActiveDoodadCount = 0;

    /** Current count of active WMO group mesh components */
    int32 ActiveWmoGroupCount = 0;

    /** Loading-screen focus used before the gameplay camera reaches the server spawn point. */
    bool bHasWorldEntryStreamingFocus = false;
    FVector WorldEntryStreamingFocus = FVector::ZeroVector;
    bool bHasCinematicStreamingFocus = false;
    FVector CinematicStreamingFocus = FVector::ZeroVector;

    /** Readiness counters used by the loading screen to detect stable nearby object streaming. */
    uint64 ObjectStreamingPassCount = 0;
    uint64 ObjectStreamingRevision = 0;

    /** Optional command-line driven screenshot path and timing for automated verification runs. */
    FString AutoScreenshotPath;
    float AutoScreenshotDelaySeconds = -1.0f;
    float AutoQuitDelaySeconds = -1.0f;
    double AutoStartWallClockSeconds = 0.0;
    bool bAutoWaitForInitialLoad = false;
    bool bAutoTimerArmed = false;
    bool bAutoScreenshotRequested = false;
    bool bAutoQuitRequested = false;
    int32 AutoLogBucket = -1;

    void LoadTile(int32 TX, int32 TY);
    void LoadTileAsync(int32 TX, int32 TY);
    void FinalizeTileLoad(int32 TX, int32 TY, TSharedPtr<FAdtData> AdtData);
    void ProcessPendingLoads();
    void UnloadTile(int32 TX, int32 TY);
    void UpdateStreaming();
    void UpdateObjectStreaming();
    bool ResolveStreamingViewLocation(FVector& OutLocation) const;
    void UpdateZoneAudio();
    void UpdateWdlStreaming(const FIntPoint& CameraTile);
    void UpdateLod1Streaming(const FIntPoint& CameraTile);
    void SpawnWdlTile(int32 TX, int32 TY);
    void SpawnLod1Tile(int32 TX, int32 TY);
    /** Minimap (md5translate) artwork for one tile, used to colour distant LOD1/WDL terrain. */
    class UTexture2D* LoadDistantTerrainTexture(int32 TX, int32 TY);
    class UMaterialInterface* CreateDistantTerrainMaterial(int32 TX, int32 TY, UObject* Outer, const FLinearColor& Fallback);
    TMap<FString, FString> MinimapTranslate;
    FString MinimapTranslateMap;
    void SetupRuntimeVirtualTexture();
    void UpdateRVTBounds(const FIntPoint& CameraTile);
    bool IsTileLoaded(int32 TX, int32 TY) const;
    /** r565: the tile's ground mesh is built and its collision cooked. */
    bool IsTileTerrainReady(int32 TX, int32 TY) const;
    /** r565: the tile's ground mesh is built (collision may still be cooking). */
    bool IsTileMeshBuilt(int32 TX, int32 TY) const;
    bool IsTilePending(int32 TX, int32 TY) const;
    static int64 TileKey(int32 TX, int32 TY) { return ((int64)TX << 32) | (int64)(uint32)TY; }

    /** Current area ID for zone audio detection */
    uint32 CurrentAudioAreaId = 0;

    /** Tiles currently being loaded on background threads */
    uint64 MapGeneration = 0;
    TArray<FPendingTileLoad> PendingLoads;
    /** Tiles whose terrain exists but whose water/doodad phases still run (one phase per tick). */
    struct FTileBuildInProgress
    {
        TWeakObjectPtr<AWowTerrainTile> Tile;
        TSharedPtr<FAdtData> Data;
        int32 TX = 0, TY = 0, NextPhase = 1;
        uint64 Generation = 0;
    };
    TArray<FTileBuildInProgress> TilesBuilding;
    bool AdvanceTileBuild();

    /** Track tile keys that are in-flight to avoid duplicate requests */
    TSet<int64> PendingTileKeys;
};
