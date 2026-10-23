#pragma once

#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

/**
 * Client-side visual record for server-spawned game objects.
 *
 * WotLK 3.3.5a layout:
 *   0 ID, 1 model filename, 2-11 sound IDs,
 *   12-17 geometry bounds, 18 transport flag.
 */
struct FGameObjectDisplayInfoDbcEntry
{
	uint32 ID = 0;
	FString ModelPath;
	FVector BoundsMin = FVector::ZeroVector;
	FVector BoundsMax = FVector::ZeroVector;
	uint32 Transport = 0;

	FBox GetBounds() const
	{
		return FBox(BoundsMin, BoundsMax);
	}
};

class WOWDATA_API FGameObjectDisplayInfoDbc
{
public:
	bool Load(const FDbcParser& Parser);
	const FGameObjectDisplayInfoDbcEntry* GetById(uint32 ID) const;
	const TArray<FGameObjectDisplayInfoDbcEntry>& GetAll() const { return Entries; }
	int32 Num() const { return Entries.Num(); }

private:
	TArray<FGameObjectDisplayInfoDbcEntry> Entries;
	TMap<uint32, int32> IdIndex;
};
