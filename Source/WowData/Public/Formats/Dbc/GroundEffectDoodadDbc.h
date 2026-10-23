#pragma once

#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FGroundEffectDoodadDbcEntry
{
    uint32 ID = 0;
    FString ModelPath;
    uint32 Flags = 0;
};

class WOWDATA_API FGroundEffectDoodadDbc
{
public:
    bool Load(const FDbcParser& Parser);
    const FGroundEffectDoodadDbcEntry* GetById(uint32 ID) const;
    const TArray<FGroundEffectDoodadDbcEntry>& GetAll() const { return Entries; }
    int32 Num() const { return Entries.Num(); }

private:
    TArray<FGroundEffectDoodadDbcEntry> Entries;
    TMap<uint32, int32> IdIndex;
};
