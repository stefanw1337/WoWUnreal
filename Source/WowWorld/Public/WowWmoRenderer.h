#pragma once
#include "CoreMinimal.h"

class FMpqManager;
class FWowAssetCache;
class UStaticMesh;
struct FAdtWmoPlacement;
struct FWmoGroupData;
struct FWmoRootData;
struct FWowWmoBuildState;

/**
 * Renders WMO (World Map Object) buildings.
 * Uses UStaticMesh with Nanite when supported.
 */
class WOWWORLD_API FWowWmoRenderer
{
public:
    /** Spawn a WMO instance at the given placement. Returns the parent actor. */
    static AActor* SpawnWmo(UWorld* World, const FString& WmoPath, const FAdtWmoPlacement& Placement,
                            FMpqManager* Mpq, FWowAssetCache* Cache);

    /** Query the number of groups in a WMO root file without spawning anything. Returns 0 on failure. */
    static uint32 GetWmoGroupCount(const FString& WmoPath, FMpqManager* Mpq);

    /** Large WMOs are built a few milliseconds per frame (r246); true while any is unfinished. */
    static bool HasPendingBuilds();
    /** Loading screen: give pending WMO builds a much larger per-frame budget. */
    static void SetFastBuild(bool bFast);
    /** Core ticker callback that advances pending WMO builds. */
    static bool TickPendingBuilds(float DeltaTime);

private:
    /** Build one WMO group (mesh, collision, liquid). Returns false when the group has nothing to show. */
    static bool BuildWmoGroup(FWowWmoBuildState& State, uint32 GroupIdx);
    /** Embedded doodads (unless already spawned incrementally), root lights and the summary log. */
    static void FinishWmoBuild(FWowWmoBuildState& State, bool bSpawnDoodadsNow);

    /** Construct group filename from root WMO path and group index */
    static FString GetGroupPath(const FString& RootPath, int32 GroupIndex);

    /** Create a UStaticMesh from WMO group geometry with Nanite support */
    static UStaticMesh* CreateStaticMeshFromWmoGroup(const FWmoGroupData& GroupData,
        const FWmoRootData& RootData, const FString& WmoPath, int32 GroupIdx,
        FMpqManager* Mpq, FWowAssetCache* Cache);
};
