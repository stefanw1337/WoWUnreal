#pragma once
#include "CoreMinimal.h"
class UWowConnectionManager;
struct FSpellDbcEntry;
struct FWowSpellUsability { bool Usable=false, NoResource=false; FString Reason; };
/** GameTooltip lines for a spell, laid out like the 3.3.5 client: name (white) | rank (grey),
 *  cost | range, cast time | cooldown (white), the description in gold, problems in red. */
struct FWowSpellTooltipParts { FString Name, Rank, Cost, Range, Cast, Cooldown, Description, Reason; bool bValid=false; };
struct WOWUI_API FWowSpellPresentation
{
    static FWowSpellUsability Usability(UWowConnectionManager* Connection,uint32 Id);
    // -1 means no applicable selected unit; 0 out of range; 1 in range.
    static int32 InRange(UWowConnectionManager* Connection,uint32 Id,uint64 TargetGuid=0);
    // Updated by the gameplay controller (terrain/WMO trace to the current target).
    static void SetTargetInSight(bool bInSight);
    static bool IsTargetInSight();
    static FString Description(uint32 Id, bool bAura = false);
    // Hearthstone home area (AreaTable id) for the $z tooltip token.
    static void SetHomeAreaId(uint32 AreaId);
    static FString Tooltip(UWowConnectionManager* Connection,uint32 Id);
    static FWowSpellTooltipParts TooltipParts(UWowConnectionManager* Connection,uint32 Id);
};
