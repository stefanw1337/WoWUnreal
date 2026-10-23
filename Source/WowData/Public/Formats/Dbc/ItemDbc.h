#pragma once

#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

/** Client-side item metadata used to resolve an item template to its display. */
struct FItemDbcEntry
{
    uint32 ID = 0;
    uint32 ClassID = 0;
    uint32 SubclassID = 0;
    int32 SoundOverrideSubclassID = 0;
    int32 MaterialID = 0;
    uint32 DisplayInfoID = 0;
    uint32 InventoryType = 0;
    uint32 SheathType = 0;
};

class WOWDATA_API FItemDbc
{
public:
    bool Load(const FDbcParser& Parser);
    const FItemDbcEntry* GetById(uint32 ID) const;
    const TArray<FItemDbcEntry>& GetAll() const { return Entries; }
    int32 Num() const { return Entries.Num(); }

private:
    TArray<FItemDbcEntry> Entries;
    TMap<uint32, int32> IdIndex;
};
