#pragma once

#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

/** One row from the WotLK 3.3.5a LFGDungeons.dbc table. */
struct FLfgDungeonDbcEntry
{
    uint32 ID = 0;
    FString Name;
    uint32 MinLevel = 0;
    uint32 MaxLevel = 0;
    uint32 TargetLevel = 0;
    uint32 TargetLevelMin = 0;
    uint32 TargetLevelMax = 0;
    uint32 MapID = 0;
    uint32 Difficulty = 0;
    uint32 Flags = 0;
    uint32 TypeID = 0;
    int32 Faction = -1;
    FString TextureFilename;
    uint32 ExpansionLevel = 0;
    uint32 OrderIndex = 0;
    uint32 GroupID = 0;
    FString Description;

    uint32 GetPackedEntry() const { return ID | (TypeID << 24); }
    bool IsDungeonFinderEntry() const { return TypeID == 1 || TypeID == 5 || TypeID == 6; }
    bool IsRaidBrowserEntry() const { return TypeID == 2; }
};

class WOWDATA_API FLfgDungeonsDbc
{
public:
    bool Load(const FDbcParser& Parser);
    const FLfgDungeonDbcEntry* GetById(uint32 ID) const;
    const TArray<FLfgDungeonDbcEntry>& GetAll() const { return Entries; }
    int32 Num() const { return Entries.Num(); }

private:
    TArray<FLfgDungeonDbcEntry> Entries;
    TMap<uint32, int32> IdIndex;
};
