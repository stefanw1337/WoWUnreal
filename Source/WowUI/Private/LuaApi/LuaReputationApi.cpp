// r340 (OPEN_ISSUES: "Reputation: no faction API - GetNumFactions returns 0, GetFactionInfo missing"):
// the 3.3.5 reputation API over the server's standings (FWowPacketHandler::FactionStates, from
// SMSG_INITIALIZE_FACTIONS / SMSG_SET_FACTION_STANDING) and Faction.dbc (names, descriptions,
// race/class base reputation, the parent groups the Reputation tab shows as headers).
//
// The list is built like the client's: the expansion groups (Wrath of the Lich King, The Burning
// Crusade, Classic, then any other top group) as headers, sub-groups (Alliance, Horde Expedition,
// Steamwheedle Cartel ...) as child headers, factions under them, and an "Inactive" group last.
#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntityManager.h"
#include "WowEntity.h"
#include "WowUpdateFields.h"
#include "WowEventSystem.h"
#include "WowAddonSettings.h"
#include "WowWorldManager.h"
#include "WowAudioManager.h"
#include "Mpq/MpqManager.h"
#include "Formats/DbcParser.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

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

DEFINE_LOG_CATEGORY_STATIC(LogWowReputationApi, Log, All);

namespace WowLuaApi { void RegisterReputationApi(lua_State* L); }

#if HAS_LUA
// Named, not anonymous: in a unity build this file shares a translation unit with the other LuaApi files.
namespace WowReputationApiImpl
{
    // Faction flags in SMSG_INITIALIZE_FACTIONS.
    constexpr uint8 FLAG_VISIBLE = 0x01, FLAG_AT_WAR = 0x02, FLAG_HIDDEN = 0x04, FLAG_INVISIBLE_FORCED = 0x08,
        FLAG_PEACE_FORCED = 0x10, FLAG_INACTIVE = 0x20;
    // PLAYER_FIELD_WATCHED_FACTION_INDEX sits right before PLAYER_FIELD_COMBAT_RATING_1 (3.3.5a 12340).
    constexpr uint16 WATCHED_FACTION_FIELD = PlayerField::COMBAT_RATING_1 - 1;
    // Reputation ranks (Hated .. Exalted): the lower bound of each, the top of Exalted.
    constexpr int32 RankMin[8] = { -42000, -6000, -3000, 0, 3000, 9000, 21000, 42000 };
    constexpr int32 RepMax = 42999;

    struct FFaction
    {
        uint32 Id = 0; int32 ListId = -1; uint32 Parent = 0;
        uint32 RaceMask[4] = {}, ClassMask[4] = {}; int32 Base[4] = {};
        FString Name, Description;
    };
    TMap<uint32, FFaction> GFactions;   // by Faction.dbc ID
    TMap<int32, uint32> GByListId;      // ReputationListID -> ID
    bool bGLoaded = false;

    struct FRow { uint32 FactionId = 0; bool bHeader = false; bool bChild = false; bool bInactiveHeader = false; };
    TSet<uint32> GCollapsed;            // faction IDs of collapsed headers (0 = the Inactive group)
    // r383 (Stefan 16.10: after "Move to Inactive" the detail window showed the box unticked): the
    // selection is the faction itself, like the client, not a row number - moving a faction to the
    // Inactive group shifts the rows, and the old number then named another faction.
    uint32 GSelectedFaction = 0;
    bool bGSelectedInactiveHeader = false;

    FWowPacketHandler* Handler(lua_State* L)
    {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        return Ctx && Ctx->ConnectionManager ? &Ctx->ConnectionManager->PacketHandler : nullptr;
    }
    UWowConnectionManager* Connection(lua_State* L)
    {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        return Ctx ? Ctx->ConnectionManager : nullptr;
    }
    void PushString(lua_State* L, const FString& Text) { lua_pushstring(L, TCHAR_TO_UTF8(*Text)); }

    void LoadFactions(lua_State* L)
    {
        if (bGLoaded) return;
        FMpqManager* Mpq = FWowAddonSettings::GetMpq();
        if (!Mpq)
        {
            FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
            UWorld* World = Ctx && Ctx->AudioManager ? Ctx->AudioManager->GetWorld() : nullptr;
            const AWowWorldManager* Terrain = World ? Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(World, AWowWorldManager::StaticClass())) : nullptr;
            Mpq = Terrain ? Terrain->GetMpqManager() : nullptr;
        }
        TArray<uint8> Bytes;
        FDbcParser Parser;
        if (!Mpq || !Mpq->ReadFile(TEXT("DBFilesClient\\Faction.dbc"), Bytes) || !Parser.Parse(Bytes)) return; // retried next call
        bGLoaded = true;
        if (Parser.GetFieldCount() < 41)
        {
            UE_LOG(LogWowReputationApi, Warning, TEXT("Faction.dbc: %u fields, expected 57 (3.3.5a)"), Parser.GetFieldCount());
            return;
        }
        // 3.3.5a Faction.dbc: 0 ID, 1 ReputationListID, 2-5 race masks, 6-9 class masks, 10-13 base
        // reputation, 14-17 flags, 18 parent, 19-22 parent spill-over, 23 name (16 locales + mask), 40 description.
        for (uint32 R = 0; R < Parser.GetRecordCount(); ++R)
        {
            FFaction F;
            F.Id = Parser.GetUInt(R, 0);
            F.ListId = static_cast<int32>(Parser.GetUInt(R, 1));
            for (int32 I = 0; I < 4; ++I)
            {
                F.RaceMask[I] = Parser.GetUInt(R, 2 + I);
                F.ClassMask[I] = Parser.GetUInt(R, 6 + I);
                F.Base[I] = static_cast<int32>(Parser.GetUInt(R, 10 + I));
            }
            F.Parent = Parser.GetUInt(R, 18);
            F.Name = Parser.GetLocalizedString(R, 23);
            F.Description = Parser.GetLocalizedString(R, 40);
            if (F.ListId >= 0) GByListId.Add(F.ListId, F.Id);
            GFactions.Add(F.Id, MoveTemp(F));
        }
        UE_LOG(LogWowReputationApi, Log, TEXT("Faction.dbc: %d factions, %d with reputation"), GFactions.Num(), GByListId.Num());
    }

    const FWowPacketHandler::FFactionState* StateOf(const FWowPacketHandler& H, const FFaction& F)
    {
        return F.ListId >= 0 && H.FactionStates.IsValidIndex(F.ListId) ? &H.FactionStates[F.ListId] : nullptr;
    }

    bool IsShown(const FWowPacketHandler::FFactionState* S)
    {
        return S && (S->Flags & FLAG_VISIBLE) && !(S->Flags & (FLAG_HIDDEN | FLAG_INVISIBLE_FORCED));
    }

    /** Reputation shown in the bar: the faction's base for your race and class plus the server's standing. */
    int32 Reputation(lua_State* L, const FWowPacketHandler& H, const FFaction& F)
    {
        const FWowPacketHandler::FFactionState* S = StateOf(H, F);
        int32 Base = 0;
        if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L))
            if (Ctx->ConnectionManager)
                if (const FWowPlayerEntity* Me = Ctx->ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer())
                {
                    const uint32 Race = Me->GetRaceId(), Class = Me->GetClassId();
                    const uint32 RaceBit = Race ? 1u << (Race - 1) : 0, ClassBit = Class ? 1u << (Class - 1) : 0;
                    for (int32 I = 0; I < 4; ++I)
                    {
                        const bool bRace = !F.RaceMask[I] || (F.RaceMask[I] & RaceBit);
                        const bool bClass = !F.ClassMask[I] || (F.ClassMask[I] & ClassBit);
                        if ((F.RaceMask[I] || F.ClassMask[I]) && bRace && bClass) { Base = F.Base[I]; break; }
                    }
                }
        return FMath::Clamp(Base + (S ? S->Standing : 0), RankMin[0], RepMax);
    }

    int32 RankOf(int32 Rep)
    {
        for (int32 I = 7; I >= 0; --I) if (Rep >= RankMin[I]) return I + 1;
        return 1;
    }

    int32 SortKey(uint32 Id)
    {
        // Expansion groups in the client's order: Wrath of the Lich King, The Burning Crusade, Classic.
        return Id == 1097 ? 0 : Id == 980 ? 1 : Id == 1118 ? 2 : 3;
    }

    TArray<FRow> BuildRows(lua_State* L)
    {
        TArray<FRow> Rows;
        FWowPacketHandler* H = Handler(L);
        LoadFactions(L);
        if (!H) return Rows;
        // Visible factions with their top group and sub-group.
        struct FItem { uint32 Id; uint32 Top; uint32 Sub; bool bInactive; };
        TArray<FItem> Items;
        for (const auto& Pair : GFactions)
        {
            const FFaction& F = Pair.Value;
            const FWowPacketHandler::FFactionState* S = StateOf(*H, F);
            if (!IsShown(S)) continue;
            TArray<uint32> Chain;  // parents, nearest first
            for (uint32 P = F.Parent; P && Chain.Num() < 4; )
            {
                Chain.Add(P);
                const FFaction* Parent = GFactions.Find(P);
                P = Parent ? Parent->Parent : 0;
            }
            FItem Item{F.Id, Chain.Num() ? Chain.Last() : 0u, Chain.Num() >= 2 ? Chain[Chain.Num() - 2] : 0u, (S->Flags & FLAG_INACTIVE) != 0};
            Items.Add(Item);
        }
        auto NameOf = [](uint32 Id) { const FFaction* F = GFactions.Find(Id); return F ? F->Name : FString(); };
        // Factions that are themselves group headers (Alliance Vanguard has a reputation of its own).
        TSet<uint32> HeaderIds;
        for (const FItem& Item : Items) { if (Item.Top) HeaderIds.Add(Item.Top); if (Item.Sub) HeaderIds.Add(Item.Sub); }
        TArray<uint32> Tops;
        for (const FItem& Item : Items) if (!Item.bInactive && !HeaderIds.Contains(Item.Id)) Tops.AddUnique(Item.Top);
        Tops.Sort([&](uint32 A, uint32 B) { const int32 KA = SortKey(A), KB = SortKey(B); return KA != KB ? KA < KB : NameOf(A) < NameOf(B); });
        for (uint32 Top : Tops)
        {
            if (Top) Rows.Add({Top, true, false, false});
            if (Top && GCollapsed.Contains(Top)) continue;
            // Direct members first, then each sub-group with its members, by name like the client.
            TArray<uint32> Direct, Subs;
            for (const FItem& Item : Items)
            {
                if (Item.bInactive || Item.Top != Top || HeaderIds.Contains(Item.Id)) continue;
                if (Item.Sub) Subs.AddUnique(Item.Sub); else Direct.Add(Item.Id);
            }
            Direct.Sort([&](uint32 A, uint32 B) { return NameOf(A) < NameOf(B); });
            Subs.Sort([&](uint32 A, uint32 B) { return NameOf(A) < NameOf(B); });
            for (uint32 Id : Direct) Rows.Add({Id, false, false, false});
            for (uint32 Sub : Subs)
            {
                Rows.Add({Sub, true, true, false});
                if (GCollapsed.Contains(Sub)) continue;
                TArray<uint32> Members;
                for (const FItem& Item : Items)
                    if (!Item.bInactive && Item.Top == Top && Item.Sub == Sub && !HeaderIds.Contains(Item.Id)) Members.Add(Item.Id);
                Members.Sort([&](uint32 A, uint32 B) { return NameOf(A) < NameOf(B); });
                for (uint32 Id : Members) Rows.Add({Id, false, true, false});
            }
        }
        TArray<uint32> Inactive;
        for (const FItem& Item : Items) if (Item.bInactive) Inactive.Add(Item.Id);
        if (Inactive.Num())
        {
            Rows.Add({0, true, false, true});
            if (!GCollapsed.Contains(0))
            {
                Inactive.Sort([&](uint32 A, uint32 B) { return NameOf(A) < NameOf(B); });
                for (uint32 Id : Inactive) Rows.Add({Id, false, false, false});
            }
        }
        return Rows;
    }

    const FRow* RowArg(lua_State* L, int Arg, TArray<FRow>& Rows)
    {
        Rows = BuildRows(L);
        const int32 Index = int32(luaL_optinteger(L, Arg, 0));
        return Rows.IsValidIndex(Index - 1) ? &Rows[Index - 1] : nullptr;
    }

    void FireUpdate(lua_State* L)
    {
        if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->EventSystem) Ctx->EventSystem->FireEvent(TEXT("UPDATE_FACTION"));
    }

    int32 WatchedListId(lua_State* L)
    {
        FWowPacketHandler* H = Handler(L);
        const FWowPlayerEntity* Me = H ? H->EntityManager.GetLocalPlayer() : nullptr;
        return Me ? static_cast<int32>(Me->GetField(WATCHED_FACTION_FIELD)) : -1;
    }

    // ── API ─────────────────────────────────────────────────────────────────────

    int RP_GetNumFactions(lua_State* L) { lua_pushinteger(L, BuildRows(L).Num()); return 1; }

    int RP_GetFactionInfo(lua_State* L)
    {
        // name, description, standingID, barMin, barMax, barValue, atWarWith, canToggleAtWar,
        // isHeader, isCollapsed, hasRep, isWatched, isChild
        TArray<FRow> Rows;
        const FRow* Row = RowArg(L, 1, Rows);
        FWowPacketHandler* H = Handler(L);
        if (!Row || !H) return 0;
        if (Row->bInactiveHeader)
        {
            lua_pushstring(L, "Inactive"); lua_pushstring(L, "");
            lua_pushinteger(L, 4); lua_pushinteger(L, 0); lua_pushinteger(L, 3000); lua_pushinteger(L, 0);
            lua_pushnil(L); lua_pushnil(L); lua_pushinteger(L, 1);
            if (GCollapsed.Contains(0)) lua_pushinteger(L, 1); else lua_pushnil(L);
            lua_pushnil(L); lua_pushnil(L); lua_pushnil(L);
            return 13;
        }
        const FFaction* F = GFactions.Find(Row->FactionId);
        if (!F) return 0;
        const FWowPacketHandler::FFactionState* S = StateOf(*H, *F);
        const bool bHasRep = IsShown(S);
        const int32 Rep = Reputation(L, *H, *F);
        const int32 Rank = RankOf(Rep);
        PushString(L, F->Name);
        PushString(L, F->Description);
        lua_pushinteger(L, Rank);
        lua_pushinteger(L, RankMin[Rank - 1]);
        lua_pushinteger(L, Rank < 8 ? RankMin[Rank] : RepMax + 1);
        lua_pushinteger(L, Rep);
        if (S && (S->Flags & FLAG_AT_WAR)) lua_pushinteger(L, 1); else lua_pushnil(L);
        if (S && !(S->Flags & FLAG_PEACE_FORCED)) lua_pushinteger(L, 1); else lua_pushnil(L);
        if (Row->bHeader) lua_pushinteger(L, 1); else lua_pushnil(L);
        if (Row->bHeader && GCollapsed.Contains(F->Id)) lua_pushinteger(L, 1); else lua_pushnil(L);
        if (Row->bHeader && bHasRep) lua_pushinteger(L, 1); else lua_pushnil(L);
        if (F->ListId >= 0 && F->ListId == WatchedListId(L)) lua_pushinteger(L, 1); else lua_pushnil(L);
        if (Row->bChild) lua_pushinteger(L, 1); else lua_pushnil(L);
        return 13;
    }

    int SetCollapsed(lua_State* L, bool bCollapse)
    {
        TArray<FRow> Rows;
        const FRow* Row = RowArg(L, 1, Rows);
        if (!Row || !Row->bHeader) return 0;
        const uint32 Key = Row->bInactiveHeader ? 0u : Row->FactionId;
        if (bCollapse) GCollapsed.Add(Key); else GCollapsed.Remove(Key);
        FireUpdate(L);
        return 0;
    }
    int RP_ExpandFactionHeader(lua_State* L) { return SetCollapsed(L, false); }
    int RP_CollapseFactionHeader(lua_State* L) { return SetCollapsed(L, true); }
    int RP_ExpandAllFactionHeaders(lua_State* L) { GCollapsed.Reset(); FireUpdate(L); return 0; }
    int RP_CollapseAllFactionHeaders(lua_State* L)
    {
        GCollapsed.Reset();
        for (const FRow& Row : BuildRows(L)) if (Row.bHeader) GCollapsed.Add(Row.bInactiveHeader ? 0u : Row.FactionId);
        FireUpdate(L);
        return 0;
    }

    int RP_SetSelectedFaction(lua_State* L)
    {
        TArray<FRow> Rows;
        const FRow* Row = RowArg(L, 1, Rows);
        GSelectedFaction = Row ? Row->FactionId : 0;
        bGSelectedInactiveHeader = Row && Row->bInactiveHeader;
        FireUpdate(L);
        return 0;
    }
    int RP_GetSelectedFaction(lua_State* L)
    {
        int32 Index = 0;
        if (GSelectedFaction || bGSelectedInactiveHeader)
        {
            const TArray<FRow> Rows = BuildRows(L);
            for (int32 I = 0; I < Rows.Num(); ++I)
                if (Rows[I].bInactiveHeader == bGSelectedInactiveHeader && Rows[I].FactionId == GSelectedFaction) { Index = I + 1; break; }
        }
        lua_pushinteger(L, Index);
        return 1;
    }

    void Send(lua_State* L, uint16 Opcode, int32 ListId, int32 Byte)
    {
        UWowConnectionManager* Conn = Connection(L);
        if (!Conn || ListId < 0) return;
        TArray<uint8> Data;
        const uint32 Id = uint32(ListId);
        Data.Append(reinterpret_cast<const uint8*>(&Id), 4);
        if (Byte >= 0) Data.Add(uint8(Byte));
        Conn->SendRawPacket(Opcode, Data);
    }

    int RP_FactionToggleAtWar(lua_State* L)
    {
        TArray<FRow> Rows;
        const FRow* Row = RowArg(L, 1, Rows);
        FWowPacketHandler* H = Handler(L);
        const FFaction* F = Row && !Row->bInactiveHeader ? GFactions.Find(Row->FactionId) : nullptr;
        FWowPacketHandler::FFactionState* S = F && H && H->FactionStates.IsValidIndex(F->ListId) ? &H->FactionStates[F->ListId] : nullptr;
        if (!S || (S->Flags & FLAG_PEACE_FORCED)) return 0;
        const bool bWar = !(S->Flags & FLAG_AT_WAR);
        Send(L, FWowPacketHandler::CMSG_SET_FACTION_ATWAR, F->ListId, bWar ? 1 : 0);
        S->Flags = bWar ? (S->Flags | FLAG_AT_WAR) : (S->Flags & ~FLAG_AT_WAR);
        FireUpdate(L);
        return 0;
    }

    int SetInactive(lua_State* L, bool bInactive)
    {
        TArray<FRow> Rows;
        const FRow* Row = RowArg(L, 1, Rows);
        FWowPacketHandler* H = Handler(L);
        const FFaction* F = Row && !Row->bInactiveHeader ? GFactions.Find(Row->FactionId) : nullptr;
        FWowPacketHandler::FFactionState* S = F && H && H->FactionStates.IsValidIndex(F->ListId) ? &H->FactionStates[F->ListId] : nullptr;
        if (!S) return 0;
        Send(L, FWowPacketHandler::CMSG_SET_FACTION_INACTIVE, F->ListId, bInactive ? 1 : 0);
        S->Flags = bInactive ? (S->Flags | FLAG_INACTIVE) : (S->Flags & ~FLAG_INACTIVE);
        FireUpdate(L);
        return 0;
    }
    int RP_SetFactionInactive(lua_State* L) { return SetInactive(L, true); }
    int RP_SetFactionActive(lua_State* L) { return SetInactive(L, false); }

    int RP_IsFactionInactive(lua_State* L)
    {
        TArray<FRow> Rows;
        const FRow* Row = RowArg(L, 1, Rows);
        FWowPacketHandler* H = Handler(L);
        const FFaction* F = Row && !Row->bInactiveHeader ? GFactions.Find(Row->FactionId) : nullptr;
        const FWowPacketHandler::FFactionState* S = F && H ? StateOf(*H, *F) : nullptr;
        lua_pushboolean(L, S && (S->Flags & FLAG_INACTIVE));
        return 1;
    }

    int RP_SetWatchedFactionIndex(lua_State* L)
    {
        // SetWatchedFactionIndex(index); 0 stops watching. The server sets PLAYER_FIELD_WATCHED_FACTION_INDEX.
        TArray<FRow> Rows;
        const FRow* Row = RowArg(L, 1, Rows);
        const FFaction* F = Row && !Row->bInactiveHeader ? GFactions.Find(Row->FactionId) : nullptr;
        UWowConnectionManager* Conn = Connection(L);
        if (!Conn) return 0;
        const uint32 Id = F && F->ListId >= 0 ? uint32(F->ListId) : 0xFFFFFFFFu;
        TArray<uint8> Data;
        Data.Append(reinterpret_cast<const uint8*>(&Id), 4);
        Conn->SendRawPacket(FWowPacketHandler::CMSG_SET_WATCHED_FACTION, Data);
        // r380 (Stefan 16.10: unticking "Show as Experience Bar" left the reputation bar in place):
        // the check box calls ReputationWatchBar_Update right after this, before the server's field
        // update arrives, so the bar showed the old faction. Like the client, take the new value
        // at once; the server's update writes the same value.
        if (FWowPacketHandler* H = Handler(L))
            if (FWowPlayerEntity* Me = H->EntityManager.GetLocalPlayer())
                Me->SetField(WATCHED_FACTION_FIELD, Id);
        FireUpdate(L);
        return 0;
    }

    int RP_GetWatchedFactionInfo(lua_State* L)
    {
        // name, standingID, barMin, barMax, barValue - nothing when no faction is watched
        // (ReputationWatchBar then shows the XP bar).
        FWowPacketHandler* H = Handler(L);
        const int32 ListId = WatchedListId(L);
        LoadFactions(L);
        const uint32* Id = ListId >= 0 ? GByListId.Find(ListId) : nullptr;
        const FFaction* F = Id ? GFactions.Find(*Id) : nullptr;
        // r346 (Stefan 14.10: a reputation bar sat over the XP bar although no faction was chosen):
        // a character that never watched a faction can carry list index 0 (AzerothCore's
        // watchedFaction column defaults to 0), which named a faction you have never met. Like the
        // client, only a faction in your reputation list counts as watched.
        if (!H || !F || !IsShown(StateOf(*H, *F))) { lua_pushnil(L); return 1; }
        const int32 Rep = Reputation(L, *H, *F);
        const int32 Rank = RankOf(Rep);
        PushString(L, F->Name);
        lua_pushinteger(L, Rank);
        lua_pushinteger(L, RankMin[Rank - 1]);
        lua_pushinteger(L, Rank < 8 ? RankMin[Rank] : RepMax + 1);
        lua_pushinteger(L, Rep);
        return 5;
    }

    int RP_GetText(lua_State* L)
    {
        // GetText(token [, gender]): the global string, the _FEMALE form for gender 3 when it exists
        // (ReputationFrame: FACTION_STANDING_LABEL5 -> "Friendly"); the token itself otherwise.
        const char* Token = luaL_optstring(L, 1, "");
        const lua_Integer Gender = luaL_optinteger(L, 2, 2);
        if (Gender == 3)
        {
            const FString Female = FString(UTF8_TO_TCHAR(Token)) + TEXT("_FEMALE");
            lua_getglobal(L, TCHAR_TO_UTF8(*Female));
            if (lua_isstring(L, -1)) return 1;
            lua_pop(L, 1);
        }
        lua_getglobal(L, Token);
        if (lua_isstring(L, -1)) return 1;
        lua_pop(L, 1);
        lua_pushstring(L, Token);
        return 1;
    }
}
#endif

void WowLuaApi::RegisterReputationApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    using namespace WowReputationApiImpl;
    lua_register(L, "GetNumFactions", RP_GetNumFactions);
    lua_register(L, "GetFactionInfo", RP_GetFactionInfo);
    lua_register(L, "ExpandFactionHeader", RP_ExpandFactionHeader);
    lua_register(L, "CollapseFactionHeader", RP_CollapseFactionHeader);
    lua_register(L, "ExpandAllFactionHeaders", RP_ExpandAllFactionHeaders);
    lua_register(L, "CollapseAllFactionHeaders", RP_CollapseAllFactionHeaders);
    lua_register(L, "SetSelectedFaction", RP_SetSelectedFaction);
    lua_register(L, "GetSelectedFaction", RP_GetSelectedFaction);
    lua_register(L, "FactionToggleAtWar", RP_FactionToggleAtWar);
    lua_register(L, "SetFactionInactive", RP_SetFactionInactive);
    lua_register(L, "SetFactionActive", RP_SetFactionActive);
    lua_register(L, "IsFactionInactive", RP_IsFactionInactive);
    lua_register(L, "SetWatchedFactionIndex", RP_SetWatchedFactionIndex);
    lua_register(L, "GetWatchedFactionInfo", RP_GetWatchedFactionInfo);
    lua_register(L, "GetText", RP_GetText);
    // r346: ReputationFrame's OnLoad (function="ReputationFrame_OnLoad") is not run by this client,
    // so the frame never registered UPDATE_FACTION and did not refresh while open. Register it from
    // the UI tick once the frame exists.
    luaL_dostring(L, R"LUA(
local previous = __WowWatchFrameTick
local registered = false
function __WowWatchFrameTick(...)
    if previous then pcall(previous, ...) end
    if not registered and ReputationFrame and ReputationFrame.RegisterEvent then
        registered = true
        ReputationFrame:RegisterEvent("UPDATE_FACTION")
    end
end
)LUA");
    UE_LOG(LogWowReputationApi, Log, TEXT("Reputation Lua API registered"));
#endif
}
