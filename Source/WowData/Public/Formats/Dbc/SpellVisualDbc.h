#pragma once

#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FSpellVisualDbcEntry
{
	uint32 ID;
	uint32 PrecastKit;
	uint32 CastKit;
	uint32 ImpactKit;
	uint32 StateKit;
	uint32 StateDoneKit;
	uint32 ChannelKit;
	uint32 HasMissile;
	int32 MissileModel;
	uint32 MissilePathType;
	uint32 MissileDestinationAttachment;
	uint32 MissileSound;
	uint32 AnimEventSoundID;
	uint32 Flags;
	uint32 CasterImpactKit;
	uint32 TargetImpactKit;
	uint32 MissileAttachment;
	float MissileFollowGroundHeight;
	float MissileFollowGroundDropSpeed;
	float MissileFollowGroundApproach;
	uint32 MissileFollowGroundFlags;
	uint32 MissileMotion;
	uint32 MissileTargetingKit;
	uint32 InstantAreaKit;
	uint32 ImpactAreaKit;
	uint32 PersistentAreaKit;
	float MissileCastOffsetX;
	float MissileCastOffsetY;
	float MissileCastOffsetZ;
	float MissileImpactOffsetX;
	float MissileImpactOffsetY;
	float MissileImpactOffsetZ;
};

class WOWDATA_API FSpellVisualDbc
{
public:
	bool Load(const FDbcParser& Parser);
	const FSpellVisualDbcEntry* GetById(uint32 ID) const;
	const TArray<FSpellVisualDbcEntry>& GetAll() const { return Entries; }
	int32 Num() const { return Entries.Num(); }

private:
	TArray<FSpellVisualDbcEntry> Entries;
	TMap<uint32, int32> IdIndex;
};
