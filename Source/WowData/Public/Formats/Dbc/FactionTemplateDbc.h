#pragma once
#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FWowFactionTemplate
{
    uint32 Faction=0, Flags=0, Group=0, Friends=0, Enemies=0;
    uint32 EnemyFactions[4]={}, FriendFactions[4]={};
};
class WOWDATA_API FFactionTemplateDbc
{
public:
    bool Load(const FDbcParser& Parser);
    /** WoW reaction ranks: 1 hostile, 4 neutral/unknown, 5 friendly. */
    int32 Reaction(uint32 From, uint32 To) const;
private:
    TMap<uint32, FWowFactionTemplate> Entries;
};
