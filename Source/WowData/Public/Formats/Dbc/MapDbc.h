#pragma once
#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FMapDbcEntry
{
    uint32 ID;            // 0
    FString InternalName; // 1
    uint32 MapType;       // 2 (0=normal, 1=instance, 2=raid, 3=BG, 4=arena)
    uint32 Flags;         // 3
    FString Name;         // 5 (localized, enUS)
    uint32 LoadingScreenID; // 57 - references LoadingScreens.dbc
    uint32 LinkedZone;    // 22
    uint32 ExpansionID;   // 63
    uint32 MaxPlayers;    // 65
};

class WOWDATA_API FMapDbc
{
public:
    bool Load(const FDbcParser& Parser);
    const FMapDbcEntry* GetById(uint32 ID) const;
    const TArray<FMapDbcEntry>& GetAll() const { return Entries; }
    int32 Num() const { return Entries.Num(); }

private:
    TArray<FMapDbcEntry> Entries;
    TMap<uint32, int32> IdIndex;
};
