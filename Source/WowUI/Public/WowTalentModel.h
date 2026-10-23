#pragma once
#include "CoreMinimal.h"

class UWowConnectionManager;
struct FTalentDbcEntry;
struct FTalentTabDbcEntry;
struct FWowTalentInfo;

struct FWowTalentTooltipLine { FString Text; FLinearColor Color = FLinearColor::White; bool bWrap = false; };

// One glyph socket (1-6) of a spec; Type 1 = major, 2 = minor.
struct FWowGlyphSocket { bool bEnabled = false; int32 Type = 1; int32 UnlockLevel = 0; uint32 GlyphId = 0, SpellId = 0; FString IconPath; };

// Talent data shared by the native talent window and the Lua talent API.
// Ranks are counts (0 = not learned, 1 = first rank); the server's 0-based rank
// indexes are converted when packets are read and written.
struct WOWUI_API FWowTalentModel
{
    static constexpr int32 PointsPerTier = 5;
    static constexpr int32 MaxTiers = 11;
    static constexpr int32 MaxColumns = 4;

    // --- Static data (DBC) ---
    static uint8 PlayerClass(UWowConnectionManager* Connection);
    // Talent trees of a class, ordered by TabPage (the in-game tab order).
    static TArray<const FTalentTabDbcEntry*> Tabs(uint8 ClassId);
    // Talents of one tree in the original UI's index order (tier, then column).
    static TArray<const FTalentDbcEntry*> TalentsInTab(uint32 TabId);
    static int32 MaxRank(const FTalentDbcEntry& Talent);
    // Spell id for a 1-based rank (clamped to the talent's ranks), 0 if none.
    static uint32 SpellForRank(const FTalentDbcEntry& Talent, int32 Rank);
    static FString SpellName(const FTalentDbcEntry& Talent);
    static FString IconPath(const FTalentDbcEntry& Talent);
    static FString IconPath(const FTalentTabDbcEntry& Tab);
    // "Interface\TalentFrame\<BackgroundFile>" without the -TopLeft/... suffix.
    static FString BackgroundPath(const FTalentTabDbcEntry& Tab);

    // --- Learned state (SMSG_TALENTS_INFO) ---
    static int32 NumGroups(UWowConnectionManager* Connection);
    static int32 ActiveGroup(UWowConnectionManager* Connection);
    static const TArray<FWowTalentInfo>* Group(UWowConnectionManager* Connection, int32 GroupIndex);
    // Unspent points of the active spec, without preview spending.
    static int32 Unspent(UWowConnectionManager* Connection);

    // --- Evaluation of one spec, optionally including previewed ranks ---
    // Preview ranks only exist for the active spec; they are cleared whenever the
    // server sends new talent data.
    static int32 Rank(UWowConnectionManager* Connection, int32 GroupIndex, uint32 TalentId, bool bWithPreview);
    static int32 PointsInTab(UWowConnectionManager* Connection, int32 GroupIndex, uint32 TabId, bool bWithPreview);
    static bool TierUnlocked(UWowConnectionManager* Connection, int32 GroupIndex, const FTalentDbcEntry& Talent, bool bWithPreview);
    static bool PrereqMet(UWowConnectionManager* Connection, int32 GroupIndex, const FTalentDbcEntry& Talent, bool bWithPreview);

    // --- Preview (3.3.5 "preview talent changes") ---
    static int32 PreviewRanks(uint32 TalentId);
    static int32 PreviewSpent();
    static bool HasPreview() { return PreviewSpent() > 0; }
    static bool CanAddPreview(UWowConnectionManager* Connection, const FTalentDbcEntry& Talent);
    static bool CanRemovePreview(UWowConnectionManager* Connection, const FTalentDbcEntry& Talent);
    static bool AddPreview(UWowConnectionManager* Connection, const FTalentDbcEntry& Talent);
    static bool RemovePreview(UWowConnectionManager* Connection, const FTalentDbcEntry& Talent);
    static void ClearPreview();
    // Sends CMSG_LEARN_PREVIEW_TALENTS for every previewed rank (lowest tier first).
    static bool LearnPreview(UWowConnectionManager* Connection);
    // Learns the next rank right away (CMSG_LEARN_TALENT), no preview.
    static bool LearnNow(UWowConnectionManager* Connection, const FTalentDbcEntry& Talent);
    // GameTooltip:SetTalent lines (3.3.5 TOOLTIP_TALENT_* strings and colours).
    static TArray<FWowTalentTooltipLine> TooltipLines(UWowConnectionManager* Connection, int32 GroupIndex,
        const FTalentTabDbcEntry& Tab, const FTalentDbcEntry& Talent, bool bWithPreview);
    // Tree with clearly the most points of a spec (spec tab icon), INDEX_NONE if none.
    static int32 PrimaryTab(UWowConnectionManager* Connection, int32 GroupIndex);

    // --- Glyphs (GlyphProperties.dbc + glyph ids from SMSG_TALENTS_INFO) ---
    static FWowGlyphSocket GlyphSocket(UWowConnectionManager* Connection, int32 Socket, int32 GroupIndex);
    static TArray<FWowTalentTooltipLine> GlyphTooltipLines(UWowConnectionManager* Connection, int32 Socket, int32 GroupIndex);

    // Incremented whenever preview or server data changes, for cheap UI refresh checks.
    static uint32 Revision();
    static void Touch();
};
