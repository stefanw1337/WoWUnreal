#pragma once

#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

/** Playable-character appearance and armor baked into a creature display. */
struct FCreatureDisplayInfoExtraDbcEntry
{
    uint32 ID = 0;
    uint32 DisplayRaceID = 0;
    uint32 DisplaySexID = 0;
    uint32 SkinID = 0;
    uint32 FaceID = 0;
    uint32 HairStyleID = 0;
    uint32 HairColorID = 0;
    uint32 FacialHairID = 0;
    uint32 NPCItemDisplay[11] = {};
    uint32 Flags = 0;
    FString BakeName;
};

class WOWDATA_API FCreatureDisplayInfoExtraDbc
{
public:
    bool Load(const FDbcParser& Parser);
    const FCreatureDisplayInfoExtraDbcEntry* GetById(uint32 ID) const;
    const TArray<FCreatureDisplayInfoExtraDbcEntry>& GetAll() const { return Entries; }
    int32 Num() const { return Entries.Num(); }

private:
    TArray<FCreatureDisplayInfoExtraDbcEntry> Entries;
    TMap<uint32, int32> IdIndex;
};
