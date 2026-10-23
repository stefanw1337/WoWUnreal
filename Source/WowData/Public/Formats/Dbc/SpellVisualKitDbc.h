#pragma once

#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FSpellVisualKitDbcEntry
{
	uint32 ID;
	uint32 StartAnimID;
	uint32 AnimID;
	uint32 HeadEffectID;
	uint32 ChestEffectID;
	uint32 BaseEffectID;
	uint32 LeftHandEffectID;
	uint32 RightHandEffectID;
	uint32 BreathEffectID;
	uint32 LeftWeaponEffectID;
	uint32 RightWeaponEffectID;
	uint32 SpecialEffectID[3];
	uint32 WorldEffectID;
	uint32 SoundID;
	uint32 ShakeID;
};

class WOWDATA_API FSpellVisualKitDbc
{
public:
	bool Load(const FDbcParser& Parser);
	const FSpellVisualKitDbcEntry* GetById(uint32 ID) const;
	const TArray<FSpellVisualKitDbcEntry>& GetAll() const { return Entries; }
	int32 Num() const { return Entries.Num(); }

private:
	TArray<FSpellVisualKitDbcEntry> Entries;
	TMap<uint32, int32> IdIndex;
};