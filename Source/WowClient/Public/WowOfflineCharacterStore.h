#pragma once

#include "CoreMinimal.h"
#include "WowSessionState.h"

struct WOWCLIENT_API FWowOfflineCharacterRecord
{
	int64 Guid = 0;
	FString Name;
	uint8 Race = 1;
	uint8 Class = 1;
	uint8 Gender = 0;
	uint8 Skin = 0;
	uint8 Face = 0;
	uint8 HairStyle = 0;
	uint8 HairColor = 0;
	uint8 FacialHair = 0;
	uint8 Level = 1;
	float Orientation = 0.f;
	int32 MapId = 0;
	int32 ZoneId = 12;
	FVector Position = FVector(-8949.0f, -132.0f, 84.0f);

	FWowCharacterInfo ToCharacterInfo() const;
};

/** Persistent, local-only character roster used by the complete offline glue flow. */
class WOWCLIENT_API FWowOfflineCharacterStore
{
public:
	static bool Load(TArray<FWowOfflineCharacterRecord>& OutCharacters);
	static bool Save(const TArray<FWowOfflineCharacterRecord>& Characters);
	static int64 AllocateGuid(const TArray<FWowOfflineCharacterRecord>& Characters);
	/** The race's (or death knight's) starting map, zone, position and facing (3.3.5 playercreateinfo). */
	static void ApplyStartLocation(FWowOfflineCharacterRecord& Character);
};
