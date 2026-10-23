#pragma once
#include "CoreMinimal.h"

class UStaticMeshComponent;
class UPrimitiveComponent;
class UWorld;
struct FAdtData;
class AActor;

class WOWWORLD_API FWowWaterRenderer
{
public:
    static void CreateWaterOverlapVolume(AActor* Owner, int32& Index, const FBox& Bounds, int32 Category);
    static void GatherMistWater(UWorld* World,const FVector& Position,float Radius,TArray<UPrimitiveComponent*>& Out,bool bIncludeOcean=false);
	/** Clockwise faces in UE coordinates; vertices are ADT top-left/right, bottom-left/right. */
	static void AppendSurfaceCellIndices(TArray<int32>& Indices, int32 A, int32 B, int32 C, int32 D)
	{
		Indices.Append({A, C, B, B, C, D});
	}

	/** Create water mesh components for all water chunks in an ADT, attach to Owner */
	static TArray<UStaticMeshComponent*> CreateWaterMeshes(
		AActor* Owner, const FAdtData& AdtData, int32 TileX, int32 TileY,
		bool bCreatePhysicsVolumes = true, int32 ChunkRowBegin = 0, int32 ChunkRowEnd = 16);

	/** r705: prepare the tile's water geometry (all wet chunk rows) on a worker thread; CreateWaterMeshes for a
	 *  single row of the same Owner then uses it. Holds its own reference to the parsed tile. */
	static void PrefetchWaterMeshes(const AActor* Owner, TSharedPtr<FAdtData> AdtData, int32 TileX, int32 TileY,
		bool bCreatePhysicsVolumes = true);

	/** Create a large ocean plane at the given height, centered on the tile */
	static UStaticMeshComponent* CreateOceanPlane(
		AActor* Owner, float OceanHeight, int32 TileX, int32 TileY);
};
