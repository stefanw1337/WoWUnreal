#pragma once

#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FQuestSortDbcEntry
{
    uint32 ID = 0;
    FString Name;
};

class WOWDATA_API FQuestSortDbc
{
public:
    bool Load(const FDbcParser& Parser);
    const FQuestSortDbcEntry* GetById(uint32 ID) const;
    int32 Num() const { return Entries.Num(); }

private:
    TArray<FQuestSortDbcEntry> Entries;
    TMap<uint32, int32> IdIndex;
};
