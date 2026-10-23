// 3.3.5 talent API backed by Talent.dbc/TalentTab.dbc and SMSG_TALENTS_INFO.
// Replaces the placeholder stubs from LuaStubs.cpp (registered later, so these win).
// Inspect and pet talents are not tracked yet: they report empty trees.
#include "LuaApiRegistry.h"
#include "WowTalentModel.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/TalentDbc.h"
#include "Formats/Dbc/TalentTabDbc.h"
#include "Formats/Dbc/SpellDbc.h"
#include "Formats/Dbc/SpellIconDbc.h"
#include "Formats/DbcParser.h"
#include "Mpq/MpqManager.h"
#include "WowAddonSettings.h"
#include "WowSpellPresentation.h"
#include "WowEntity.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#define HAS_LUA 1
#else
#define HAS_LUA 0
#endif

#if HAS_LUA
namespace
{
UWowConnectionManager* TalentConnection(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    return Ctx ? Ctx->ConnectionManager : nullptr;
}

// Player trees only; inspect/pet arguments yield no tree.
const FTalentTabDbcEntry* TalentTabArg(lua_State* L, int Index, int InspectIndex, int PetIndex)
{
    if (lua_toboolean(L, InspectIndex) || lua_toboolean(L, PetIndex)) return nullptr;
    const TArray<const FTalentTabDbcEntry*> Tabs = FWowTalentModel::Tabs(FWowTalentModel::PlayerClass(TalentConnection(L)));
    const int32 Tab = static_cast<int32>(luaL_optinteger(L, Index, 1)) - 1;
    return Tabs.IsValidIndex(Tab) ? Tabs[Tab] : nullptr;
}

const FTalentDbcEntry* TalentTalentArg(lua_State* L, const FTalentTabDbcEntry* Tab, int Index)
{
    if (!Tab) return nullptr;
    const TArray<const FTalentDbcEntry*> Talents = FWowTalentModel::TalentsInTab(Tab->TalentTabID);
    const int32 Talent = static_cast<int32>(luaL_optinteger(L, Index, 1)) - 1;
    return Talents.IsValidIndex(Talent) ? Talents[Talent] : nullptr;
}

int32 TalentGroupArg(lua_State* L, int Index)
{
    UWowConnectionManager* C = TalentConnection(L);
    return lua_isnumber(L, Index) ? static_cast<int32>(lua_tointeger(L, Index)) - 1 : FWowTalentModel::ActiveGroup(C);
}

void TalentPushUtf8(lua_State* L, const FString& Text) { lua_pushstring(L, TCHAR_TO_UTF8(*Text)); }

int LT_GetNumTalentTabs(lua_State* L)
{
    const bool bPet = lua_toboolean(L, 2) != 0;
    lua_pushinteger(L, bPet ? 0 : FWowTalentModel::Tabs(FWowTalentModel::PlayerClass(TalentConnection(L))).Num());
    return 1;
}

// name, iconTexture, pointsSpent, background, previewPointsSpent
int LT_GetTalentTabInfo(lua_State* L)
{
    const FTalentTabDbcEntry* Tab = TalentTabArg(L, 1, 2, 3);
    if (!Tab) return 0;
    UWowConnectionManager* C = TalentConnection(L);
    const int32 Group = TalentGroupArg(L, 4);
    const int32 Spent = FWowTalentModel::PointsInTab(C, Group, Tab->TalentTabID, false);
    TalentPushUtf8(L, Tab->Name);
    TalentPushUtf8(L, FWowTalentModel::IconPath(*Tab));
    lua_pushinteger(L, Spent);
    TalentPushUtf8(L, Tab->InternalName);
    lua_pushinteger(L, FWowTalentModel::PointsInTab(C, Group, Tab->TalentTabID, true) - Spent);
    return 5;
}

int LT_GetNumTalents(lua_State* L)
{
    const FTalentTabDbcEntry* Tab = TalentTabArg(L, 1, 2, 3);
    lua_pushinteger(L, Tab ? FWowTalentModel::TalentsInTab(Tab->TalentTabID).Num() : 0);
    return 1;
}

// name, iconTexture, tier, column, rank, maxRank, isExceptional, meetsPrereq, previewRank, meetsPreviewPrereq
int LT_GetTalentInfo(lua_State* L)
{
    const FTalentTabDbcEntry* Tab = TalentTabArg(L, 1, 3, 4);
    const FTalentDbcEntry* Talent = TalentTalentArg(L, Tab, 2);
    if (!Talent) return 0;
    UWowConnectionManager* C = TalentConnection(L);
    const int32 Group = TalentGroupArg(L, 5);
    TalentPushUtf8(L, FWowTalentModel::SpellName(*Talent));
    TalentPushUtf8(L, FWowTalentModel::IconPath(*Talent));
    lua_pushinteger(L, Talent->Row + 1);
    lua_pushinteger(L, Talent->Col + 1);
    lua_pushinteger(L, FWowTalentModel::Rank(C, Group, Talent->TalentID, false));
    lua_pushinteger(L, FWowTalentModel::MaxRank(*Talent));
    lua_pushnil(L);
    lua_pushboolean(L, FWowTalentModel::PrereqMet(C, Group, *Talent, false));
    lua_pushinteger(L, FWowTalentModel::Rank(C, Group, Talent->TalentID, true));
    lua_pushboolean(L, FWowTalentModel::PrereqMet(C, Group, *Talent, true));
    return 10;
}

// tier, column, isLearnable, isPreviewLearnable (for the prerequisite talent)
int LT_GetTalentPrereqs(lua_State* L)
{
    const FTalentTabDbcEntry* Tab = TalentTabArg(L, 1, 3, 4);
    const FTalentDbcEntry* Talent = TalentTalentArg(L, Tab, 2);
    if (!Talent || !Talent->DependsOn) return 0;
    const FTalentDbcEntry* Prereq = FDbcStore::Get().Talents().GetById(Talent->DependsOn);
    if (!Prereq) return 0;
    UWowConnectionManager* C = TalentConnection(L);
    const int32 Group = TalentGroupArg(L, 5);
    lua_pushinteger(L, Prereq->Row + 1);
    lua_pushinteger(L, Prereq->Col + 1);
    lua_pushboolean(L, FWowTalentModel::PrereqMet(C, Group, *Talent, false));
    lua_pushboolean(L, FWowTalentModel::PrereqMet(C, Group, *Talent, true));
    return 4;
}

int LT_GetUnspentTalentPoints(lua_State* L)
{
    UWowConnectionManager* C = TalentConnection(L);
    if (lua_toboolean(L, 1)) { lua_pushinteger(L, 0); return 1; }
    if (lua_toboolean(L, 2)) { lua_pushinteger(L, C ? C->PacketHandler.PetUnspentTalentPoints : 0); return 1; }
    const int32 Group = TalentGroupArg(L, 3);
    lua_pushinteger(L, Group == FWowTalentModel::ActiveGroup(C) ? FWowTalentModel::Unspent(C) : 0);
    return 1;
}

int LT_UnitCharacterPoints(lua_State* L)
{
    const char* Unit = luaL_optstring(L, 1, "player");
    lua_pushinteger(L, FCStringAnsi::Stricmp(Unit, "player") == 0 ? FWowTalentModel::Unspent(TalentConnection(L)) : 0);
    return 1;
}

int LT_LearnTalent(lua_State* L)
{
    if (const FTalentDbcEntry* Talent = TalentTalentArg(L, TalentTabArg(L, 1, 3, 3), 2))
    {
        FWowTalentModel::LearnNow(TalentConnection(L), *Talent);
    }
    return 0;
}

int LT_AddPreviewTalentPoints(lua_State* L)
{
    const FTalentDbcEntry* Talent = TalentTalentArg(L, TalentTabArg(L, 1, 4, 4), 2);
    UWowConnectionManager* C = TalentConnection(L);
    if (!Talent) return 0;
    int32 Points = static_cast<int32>(luaL_optinteger(L, 3, 1));
    for (; Points > 0 && FWowTalentModel::AddPreview(C, *Talent); --Points) {}
    for (; Points < 0 && FWowTalentModel::RemovePreview(C, *Talent); ++Points) {}
    return 0;
}

int LT_GetGroupPreviewTalentPointsSpent(lua_State* L)
{
    lua_pushinteger(L, lua_toboolean(L, 1) ? 0 : FWowTalentModel::PreviewSpent());
    return 1;
}

int LT_GetPreviewTalentPointsSpent(lua_State* L)
{
    lua_pushinteger(L, lua_toboolean(L, 1) ? 0 : FWowTalentModel::PreviewSpent());
    return 1;
}

int LT_ResetPreviewTalentPoints(lua_State* L)
{
    FWowTalentModel::ClearPreview();
    return 0;
}

int LT_LearnPreviewTalents(lua_State* L)
{
    if (!lua_toboolean(L, 1)) FWowTalentModel::LearnPreview(TalentConnection(L));
    return 0;
}

int LT_GetActiveTalentGroup(lua_State* L)
{
    lua_pushinteger(L, FWowTalentModel::ActiveGroup(TalentConnection(L)) + 1);
    return 1;
}

int LT_GetNumTalentGroups(lua_State* L)
{
    lua_pushinteger(L, FWowTalentModel::NumGroups(TalentConnection(L)));
    return 1;
}

// |cff4e96f7|Htalent:<id>:<rankIndex>|h[Name]|h|r  (rankIndex -1 when not learned)
int LT_GetTalentLink(lua_State* L)
{
    const FTalentDbcEntry* Talent = TalentTalentArg(L, TalentTabArg(L, 1, 3, 4), 2);
    if (!Talent) return 0;
    UWowConnectionManager* C = TalentConnection(L);
    const int32 Rank = FWowTalentModel::Rank(C, TalentGroupArg(L, 5), Talent->TalentID, false);
    TalentPushUtf8(L, FString::Printf(TEXT("|cff4e96f7|Htalent:%u:%d|h[%s]|h|r"),
        Talent->TalentID, Rank - 1, *FWowTalentModel::SpellName(*Talent)));
    return 1;
}

// SetActiveTalentGroup(group): the server switches specs through the "Activate Primary/Secondary
// Spec" spells (63645 / 63644).
int LT_SetActiveTalentGroup(lua_State* L)
{
    if (UWowConnectionManager* C = TalentConnection(L))
    {
        const int32 Group = static_cast<int32>(luaL_optinteger(L, 1, 1));
        C->SendCastSpell(Group <= 1 ? 63645 : 63644, 0);
    }
    return 0;
}

void TalentPushLine(lua_State* L, int32 Index, const FString& Text, const FLinearColor& Color, bool bWrap)
{
    lua_newtable(L);
    TalentPushUtf8(L, Text); lua_rawseti(L, -2, 1);
    lua_pushnumber(L, Color.R); lua_rawseti(L, -2, 2);
    lua_pushnumber(L, Color.G); lua_rawseti(L, -2, 3);
    lua_pushnumber(L, Color.B); lua_rawseti(L, -2, 4);
    lua_pushboolean(L, bWrap); lua_rawseti(L, -2, 5);
    lua_rawseti(L, -2, Index);
}

// __WowTalentTooltipLines(tab, index, inspect, pet, group, preview) -> { {text, r, g, b, wrap}, ... }
// in the order of the 3.3.5 GameTooltip:SetTalent (GlobalStrings TOOLTIP_TALENT_*).
int LT_TalentTooltipLines(lua_State* L)
{
    const FTalentTabDbcEntry* Tab = TalentTabArg(L, 1, 3, 4);
    const FTalentDbcEntry* Talent = TalentTalentArg(L, Tab, 2);
    if (!Talent) return 0;
    UWowConnectionManager* C = TalentConnection(L);
    const int32 Group = TalentGroupArg(L, 5);
    const bool bPreview = lua_toboolean(L, 6) && Group == FWowTalentModel::ActiveGroup(C);
    const int32 Rank = FWowTalentModel::Rank(C, Group, Talent->TalentID, bPreview);
    const int32 MaxRank = FWowTalentModel::MaxRank(*Talent);
    const FLinearColor White = FLinearColor::White, Red(1.f, .125f, .125f), Gold(1.f, .82f, 0.f), Green(.125f, 1.f, .125f);
    lua_newtable(L);
    int32 N = 0;
    TalentPushLine(L, ++N, FWowTalentModel::SpellName(*Talent), White, false);
    TalentPushLine(L, ++N, FString::Printf(TEXT("Rank %d/%d"), Rank, MaxRank), White, false);
    if (!FWowTalentModel::TierUnlocked(C, Group, *Talent, bPreview))
        TalentPushLine(L, ++N, FString::Printf(TEXT("Requires %d points in %s Talents"),
            static_cast<int32>(Talent->Row) * FWowTalentModel::PointsPerTier, *Tab->Name), Red, true);
    if (!FWowTalentModel::PrereqMet(C, Group, *Talent, bPreview))
        if (const FTalentDbcEntry* Prereq = FDbcStore::Get().Talents().GetById(Talent->DependsOn))
        {
            const int32 Need = static_cast<int32>(Talent->DependsOnRank) + 1;
            TalentPushLine(L, ++N, FString::Printf(TEXT("Requires %d %s in %s"), Need, Need == 1 ? TEXT("point") : TEXT("points"),
                *FWowTalentModel::SpellName(*Prereq)), Red, true);
        }
    const FString Current = FWowSpellPresentation::Description(FWowTalentModel::SpellForRank(*Talent, FMath::Max(1, Rank)));
    if (!Current.IsEmpty()) TalentPushLine(L, ++N, Current, Gold, true);
    if (Rank > 0 && Rank < MaxRank)
    {
        TalentPushLine(L, ++N, TEXT(" "), White, false);
        TalentPushLine(L, ++N, TEXT("Next rank:"), White, false);
        const FString Next = FWowSpellPresentation::Description(FWowTalentModel::SpellForRank(*Talent, Rank + 1));
        if (!Next.IsEmpty()) TalentPushLine(L, ++N, Next, Gold, true);
    }
    if (Group == FWowTalentModel::ActiveGroup(C) && FWowTalentModel::CanAddPreview(C, *Talent))
        TalentPushLine(L, ++N, TEXT("Click to learn"), Green, false);
    return 1;
}

// Glyph sockets in 3.3.5 order: type 1 = major, 2 = minor, unlocked at these levels.
const int32 TalentGlyphSocketType[6] = { 1, 2, 2, 1, 2, 1 };
const int32 TalentGlyphSocketLevel[6] = { 15, 15, 50, 30, 70, 80 };

struct FTalentGlyphInfo { uint32 SpellId = 0, IconId = 0; };
const FTalentGlyphInfo* TalentFindGlyph(uint32 GlyphId)
{
    // GlyphProperties.dbc: ID, SpellID, TypeFlags, SpellIconID.
    static TMap<uint32, FTalentGlyphInfo> Glyphs;
    static bool bLoaded = false;
    if (!bLoaded)
        if (FMpqManager* Mpq = FWowAddonSettings::GetMpq())
        {
            bLoaded = true;
            TArray<uint8> Bytes;
            FDbcParser P;
            if (Mpq->ReadFile(TEXT("DBFilesClient\\GlyphProperties.dbc"), Bytes) && P.Parse(Bytes) && P.GetFieldCount() >= 4)
                for (uint32 I = 0; I < P.GetRecordCount(); ++I)
                    Glyphs.Add(P.GetUInt(I, 0), { P.GetUInt(I, 1), P.GetUInt(I, 3) });
        }
    return Glyphs.Find(GlyphId);
}

// Glyph id in a socket (1-based) of a spec, 0 when empty.
uint32 TalentSocketGlyph(UWowConnectionManager* C, int32 Socket, int32 Group)
{
    if (!C || Socket < 1 || Socket > 6) return 0;
    const TArray<TArray<uint16>>& Glyphs = C->PacketHandler.TalentGlyphs;
    return Glyphs.IsValidIndex(Group) && Glyphs[Group].IsValidIndex(Socket - 1) ? Glyphs[Group][Socket - 1] : 0;
}

int32 TalentPlayerLevel(UWowConnectionManager* C)
{
    const FWowPlayerEntity* Player = C ? C->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
    return Player ? Player->GetLevel() : 0;
}

int LT_GetNumGlyphSockets(lua_State* L) { lua_pushinteger(L, 6); return 1; }

// enabled, glyphType, glyphSpell, iconFilename
int LT_GetGlyphSocketInfo(lua_State* L)
{
    UWowConnectionManager* C = TalentConnection(L);
    const int32 Socket = static_cast<int32>(luaL_checkinteger(L, 1));
    if (Socket < 1 || Socket > 6) return 0;
    const int32 Group = TalentGroupArg(L, 2);
    lua_pushboolean(L, TalentPlayerLevel(C) >= TalentGlyphSocketLevel[Socket - 1]);
    lua_pushinteger(L, TalentGlyphSocketType[Socket - 1]);
    const FTalentGlyphInfo* Glyph = TalentFindGlyph(TalentSocketGlyph(C, Socket, Group));
    if (!Glyph || !Glyph->SpellId) { lua_pushnil(L); lua_pushnil(L); return 4; }
    lua_pushinteger(L, Glyph->SpellId);
    const FSpellIconDbcEntry* Icon = FDbcStore::Get().SpellIcons().GetById(Glyph->IconId);
    if (Icon && !Icon->TexturePath.IsEmpty()) TalentPushUtf8(L, Icon->TexturePath); else lua_pushnil(L);
    return 4;
}

FString TalentGlyphName(uint32 SpellId)
{
    const FSpellDbcEntry* Spell = FDbcStore::Get().Spells().GetById(SpellId);
    return Spell ? Spell->SpellName : FString();
}

int LT_GetGlyphLink(lua_State* L)
{
    UWowConnectionManager* C = TalentConnection(L);
    const int32 Socket = static_cast<int32>(luaL_checkinteger(L, 1));
    const uint32 GlyphId = TalentSocketGlyph(C, Socket, TalentGroupArg(L, 2));
    const FTalentGlyphInfo* Glyph = TalentFindGlyph(GlyphId);
    if (!Glyph) return 0;
    TalentPushUtf8(L, FString::Printf(TEXT("|cff66bbff|Hglyph:%d:%u|h[%s]|h|r"), Socket, GlyphId, *TalentGlyphName(Glyph->SpellId)));
    return 1;
}

// __WowGlyphTooltipLines(socket, group) for GameTooltip:SetGlyph.
int LT_GlyphTooltipLines(lua_State* L)
{
    UWowConnectionManager* C = TalentConnection(L);
    const int32 Socket = static_cast<int32>(luaL_checkinteger(L, 1));
    if (Socket < 1 || Socket > 6) return 0;
    const int32 Group = TalentGroupArg(L, 2);
    const FLinearColor White = FLinearColor::White, Gold(1.f, .82f, 0.f), Grey(.5f, .5f, .5f), Red(1.f, .125f, .125f);
    const bool bMajor = TalentGlyphSocketType[Socket - 1] == 1;
    lua_newtable(L);
    int32 N = 0;
    const FTalentGlyphInfo* Glyph = TalentFindGlyph(TalentSocketGlyph(C, Socket, Group));
    if (TalentPlayerLevel(C) < TalentGlyphSocketLevel[Socket - 1])
    {
        TalentPushLine(L, ++N, TEXT("Locked"), Grey, false);
        TalentPushLine(L, ++N, FString::Printf(TEXT("Unlocked at level %d."), TalentGlyphSocketLevel[Socket - 1]), Red, true);
        return 1;
    }
    if (!Glyph || !Glyph->SpellId)
    {
        TalentPushLine(L, ++N, TEXT("Empty"), White, false);
        TalentPushLine(L, ++N, bMajor ? TEXT("Major Glyph") : TEXT("Minor Glyph"), FLinearColor(.6f, .8f, 1.f), false);
        TalentPushLine(L, ++N, TEXT("Use a Glyph from your inventory to inscribe your spellbook."), Gold, true);
        return 1;
    }
    TalentPushLine(L, ++N, TalentGlyphName(Glyph->SpellId), White, false);
    TalentPushLine(L, ++N, bMajor ? TEXT("Major Glyph") : TEXT("Minor Glyph"), FLinearColor(.6f, .8f, 1.f), false);
    const FString Text = FWowSpellPresentation::Description(Glyph->SpellId);
    if (!Text.IsEmpty()) TalentPushLine(L, ++N, Text, Gold, true);
    TalentPushLine(L, ++N, TEXT("<Shift Right Click to Remove>"), Grey, false);
    return 1;
}

int LT_GlyphMatchesSocket(lua_State* L) { lua_pushboolean(L, 0); return 1; }
int LT_PlaceGlyphInSocket(lua_State* L) { return 0; }
int LT_IsCurrentSpell(lua_State* L) { lua_pushboolean(L, 0); return 1; }

// GameTooltip:SetTalent / :SetGlyph, filled from the line helpers above.
const char* TalentTooltipLua = R"LUA(
local methods = ...
local function fill(self, lines)
    if type(lines) ~= "table" then return end
    if self.ClearLines then self:ClearLines() end
    for i, line in ipairs(lines) do
        if i == 1 and self.SetText then
            self:SetText(line[1], line[2], line[3], line[4])
        else
            self:AddLine(line[1], line[2], line[3], line[4], line[5])
        end
    end
    if self.Show then self:Show() end
end
methods.SetTalent = function(self, ...) fill(self, __WowTalentTooltipLines(...)) end
methods.SetGlyph = function(self, socket, group) fill(self, __WowGlyphTooltipLines(socket, group)) end
)LUA";
}
#endif

namespace WowLuaApi
{
void RegisterTalentApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    lua_register(L, "GetNumTalentTabs", LT_GetNumTalentTabs);
    lua_register(L, "GetTalentTabInfo", LT_GetTalentTabInfo);
    lua_register(L, "GetNumTalents", LT_GetNumTalents);
    lua_register(L, "GetTalentInfo", LT_GetTalentInfo);
    lua_register(L, "GetTalentPrereqs", LT_GetTalentPrereqs);
    lua_register(L, "GetUnspentTalentPoints", LT_GetUnspentTalentPoints);
    lua_register(L, "UnitCharacterPoints", LT_UnitCharacterPoints);
    lua_register(L, "LearnTalent", LT_LearnTalent);
    lua_register(L, "AddPreviewTalentPoints", LT_AddPreviewTalentPoints);
    lua_register(L, "GetGroupPreviewTalentPointsSpent", LT_GetGroupPreviewTalentPointsSpent);
    lua_register(L, "GetPreviewTalentPointsSpent", LT_GetPreviewTalentPointsSpent);
    lua_register(L, "ResetGroupPreviewTalentPoints", LT_ResetPreviewTalentPoints);
    lua_register(L, "ResetPreviewTalentPoints", LT_ResetPreviewTalentPoints);
    lua_register(L, "LearnPreviewTalents", LT_LearnPreviewTalents);
    lua_register(L, "GetActiveTalentGroup", LT_GetActiveTalentGroup);
    lua_register(L, "GetNumTalentGroups", LT_GetNumTalentGroups);
    lua_register(L, "GetTalentLink", LT_GetTalentLink);
    lua_register(L, "SetActiveTalentGroup", LT_SetActiveTalentGroup);
    lua_register(L, "__WowTalentTooltipLines", LT_TalentTooltipLines);
    lua_register(L, "__WowGlyphTooltipLines", LT_GlyphTooltipLines);
    lua_register(L, "GetNumGlyphSockets", LT_GetNumGlyphSockets);
    lua_register(L, "GetGlyphSocketInfo", LT_GetGlyphSocketInfo);
    lua_register(L, "GetGlyphLink", LT_GetGlyphLink);
    lua_register(L, "GlyphMatchesSocket", LT_GlyphMatchesSocket);
    lua_register(L, "PlaceGlyphInSocket", LT_PlaceGlyphInSocket);
    lua_getglobal(L, "IsCurrentSpell");
    const bool bNoCurrentSpell = lua_isnil(L, -1);
    lua_pop(L, 1);
    if (bNoCurrentSpell) lua_register(L, "IsCurrentSpell", LT_IsCurrentSpell);
    luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
    if (lua_istable(L, -1) && luaL_loadstring(L, TalentTooltipLua) == 0)
    {
        lua_pushvalue(L, -2);
        if (lua_pcall(L, 1, 0, 0) != 0) lua_pop(L, 1);
    }
    else if (lua_istable(L, -1))
    {
        lua_pop(L, 1); // load error message
    }
    lua_pop(L, 1);
#endif
}
}
