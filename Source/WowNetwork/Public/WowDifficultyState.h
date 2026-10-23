#pragma once
#include "CoreMinimal.h"

/**
 * r563 (Claude-Perf; Stefan: dungeon portals show a skull and turn orange on Heroic): the player's
 * dungeon and raid difficulty, readable from any module (world visuals, Lua).
 * 0-based like the server: dungeon 0 normal, 1 heroic; raid 0 10-man, 1 25-man, 2 10 heroic, 3 25 heroic.
 * The server sets it with MSG_SET_DUNGEON_DIFFICULTY / MSG_SET_RAID_DIFFICULTY (on login, on change,
 * when the group leader changes it); the Lua setters update it at once as well.
 */
struct WOWNETWORK_API FWowDifficultyState
{
    static int32 GetDungeon();
    static int32 GetRaid();
    static void SetDungeon(int32 Difficulty);
    static void SetRaid(int32 Difficulty);
    static bool IsHeroicDungeon() { return GetDungeon() >= 1; }
    static bool IsHeroicRaid() { return GetRaid() >= 2; }
};
