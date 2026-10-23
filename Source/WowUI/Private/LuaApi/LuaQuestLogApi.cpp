// r328 (Stefan 14.10: "make them not placeholders, make them look up the real deal"): the 3.3.5
// quest log API over the real quest log (FWowPacketHandler::QuestLog, filled from the player's
// quest-log fields and SMSG_QUEST_QUERY_RESPONSE). It replaces the old stubs ("Quest 123", level 1)
// and the placeholders, so the stock WatchFrame / QuestLogFrame and quest addons see the same quests,
// objectives, watches and quest items as the native quest log and tracker.
//
// Indexing works like the client: the log is a list of zone headers, each followed by its quests
// (in quest-log slot order); a collapsed header hides its quests. Most functions take a log index;
// the ones without one use the selection (SelectQuestLogEntry).
#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntityManager.h"
#include "WowEntity.h"
#include "WowUpdateFields.h"
#include "WowEventSystem.h"
#include "WowWorldManager.h"
#include "WowAudioManager.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogWowQuestLogApi, Log, All);

namespace WowLuaApi { void RegisterQuestLogApi(lua_State* L); }

#if HAS_LUA
// Named, not anonymous: in a unity build this file shares a translation unit with the other LuaApi files.
namespace WowQuestLogApiImpl
{
    constexpr uint16 CMSG_QUESTLOG_REMOVE_QUEST = 0x194;
    constexpr uint16 CMSG_PUSHQUESTTOPARTY = 0x19D;
    constexpr int32 MAX_WATCHABLE_QUESTS = 25;

    /** One line of the quest log: a zone header or a quest (index into PacketHandler.QuestLog). */
    struct FQuestRow
    {
        bool bHeader = false;
        FString Header;
        int32 LogIndex = INDEX_NONE;
    };

    TSet<FString> GCollapsedHeaders;
    uint32 GSelectedQuestId = 0;
    FString GSelectedHeader;
    uint32 GAbandonQuestId = 0;

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

    FString HeaderName(const FWowQuestLogEntry& Entry)
    {
        return Entry.ZoneName.IsEmpty() ? FString(TEXT("Miscellaneous")) : Entry.ZoneName;
    }

    /** Headers in the order their first quest appears in the log, each followed by its quests. */
    TArray<FQuestRow> BuildRows(const FWowPacketHandler& H)
    {
        TArray<FString> Headers;
        for (const FWowQuestLogEntry& Entry : H.QuestLog) Headers.AddUnique(HeaderName(Entry));
        TArray<FQuestRow> Rows;
        for (const FString& Header : Headers)
        {
            FQuestRow& HeaderRow = Rows.AddDefaulted_GetRef();
            HeaderRow.bHeader = true;
            HeaderRow.Header = Header;
            if (GCollapsedHeaders.Contains(Header)) continue;
            for (int32 I = 0; I < H.QuestLog.Num(); ++I)
                if (HeaderName(H.QuestLog[I]) == Header)
                {
                    FQuestRow& QuestRow = Rows.AddDefaulted_GetRef();
                    QuestRow.LogIndex = I;
                }
        }
        return Rows;
    }

    /** Row index (1-based) of a quest, 0 when it is not visible. */
    int32 RowOfQuest(const FWowPacketHandler& H, uint32 QuestId)
    {
        const TArray<FQuestRow> Rows = BuildRows(H);
        for (int32 I = 0; I < Rows.Num(); ++I)
            if (!Rows[I].bHeader && H.QuestLog[Rows[I].LogIndex].QuestId == QuestId) return I + 1;
        return 0;
    }

    /** The quest at log index Arg (1-based); the selected quest when the argument is nil/absent. */
    FWowQuestLogEntry* QuestArg(lua_State* L, int Arg, bool bSelectionIfNil = true)
    {
        FWowPacketHandler* H = Handler(L);
        if (!H) return nullptr;
        if (lua_isnoneornil(L, Arg))
        {
            if (!bSelectionIfNil || !GSelectedQuestId) return nullptr;
            return H->QuestLog.FindByPredicate([](const FWowQuestLogEntry& E) { return E.QuestId == GSelectedQuestId; });
        }
        const int32 Index = int32(lua_tointeger(L, Arg));
        const TArray<FQuestRow> Rows = BuildRows(*H);
        if (!Rows.IsValidIndex(Index - 1) || Rows[Index - 1].bHeader) return nullptr;
        return &H->QuestLog[Rows[Index - 1].LogIndex];
    }

    FString QuestTitle(const FWowPacketHandler& H, const FWowQuestLogEntry& Entry)
    {
        if (!Entry.Title.IsEmpty()) return Entry.Title;
        const FString* Cached = H.QuestTitleCache.Find(Entry.QuestId);
        if (Cached && !Cached->IsEmpty()) { return *Cached; }
        return FString::Printf(TEXT("Quest %u"), Entry.QuestId);
    }

    int32 QuestLevel(const FWowPacketHandler& H, const FWowQuestLogEntry& Entry)
    {
        if (Entry.Level > 0) return Entry.Level;
        if (const FWowQuestLogEntry* Template = H.QuestTemplateCache.Find(Entry.QuestId)) return Template->Level;
        return 0;
    }

    int32 PlayerLevel(FWowPacketHandler& H)
    {
        const FWowPlayerEntity* Player = H.EntityManager.GetLocalPlayer();
        return Player ? int32(Player->GetField(UnitField::LEVEL)) : 1;
    }

    /** GetQuestDifficultyColor (UIParent.lua) as a hex colour for quest links. */
    FString DifficultyHex(lua_State* L, int32 InQuestLevel, int32 InPlayerLevel)
    {
        if (InQuestLevel <= 0) return TEXT("ffffff00");
        const int32 Diff = InQuestLevel - InPlayerLevel;
        if (Diff >= 5) return TEXT("ffff1a1a");
        if (Diff >= 3) return TEXT("ffff8040");
        if (Diff >= -2) return TEXT("ffffff00");
        int32 GreenRange = 8;
        lua_getglobal(L, "GetQuestGreenRange");
        if (lua_isfunction(L, -1) && lua_pcall(L, 0, 1, 0) == 0)
        {
            if (lua_isnumber(L, -1)) GreenRange = int32(lua_tointeger(L, -1));
        }
        lua_pop(L, 1);
        return -Diff <= GreenRange ? FString(TEXT("ff40c040")) : FString(TEXT("ff808080"));
    }

    struct FItemInfo { FString Name, Link, Texture; int32 Quality = 1; bool bValid = false; };

    /** The client's own GetItemInfo (name, link, quality, ..., texture), so links and icons match the bags. */
    FItemInfo LookupItem(lua_State* L, uint32 ItemId)
    {
        FItemInfo Info;
        if (!ItemId) return Info;
        const int Top = lua_gettop(L);
        lua_getglobal(L, "GetItemInfo");
        if (lua_isfunction(L, -1))
        {
            lua_pushinteger(L, lua_Integer(ItemId));
            if (lua_pcall(L, 1, 10, 0) == 0)
            {
                const int Base = lua_gettop(L) - 9;
                if (const char* Name = lua_tostring(L, Base)) { Info.Name = UTF8_TO_TCHAR(Name); Info.bValid = true; }
                if (const char* Link = lua_tostring(L, Base + 1)) Info.Link = UTF8_TO_TCHAR(Link);
                if (lua_isnumber(L, Base + 2)) Info.Quality = int32(lua_tointeger(L, Base + 2));
                if (const char* Texture = lua_tostring(L, Base + 9)) Info.Texture = UTF8_TO_TCHAR(Texture);
            }
        }
        lua_settop(L, Top);
        UWowConnectionManager* Conn = Connection(L);
        if (!Info.bValid && Conn) { Conn->RequestItemInfo(ItemId); }
        return Info;
    }

    /** The quest's usable item (WatchFrame's quest item button): its source item when it has an on-use spell and is carried. */
    uint32 SpecialItem(lua_State* L, const FWowQuestLogEntry& Entry)
    {
        UWowConnectionManager* Conn = Connection(L);
        if (!Conn || !Entry.SourceItemId) return 0;
        const FWowItemTemplateInfo* Info = Conn->PacketHandler.ItemTemplates.Find(Entry.SourceItemId);
        if (!Info) { Conn->RequestItemInfo(Entry.SourceItemId); return 0; }
        bool bUsable = false;
        for (int32 I = 0; I < 5; ++I) bUsable |= Info->SpellIds[I] != 0 && Info->SpellTriggers[I] == 0;
        return bUsable && Conn->CountPlayerItems(Entry.SourceItemId) > 0 ? Entry.SourceItemId : 0;
    }

    /** Objectives with a required count, as GetQuestLogLeaderBoard lists them. */
    TArray<const FWowQuestObjective*> LeaderBoards(const FWowQuestLogEntry& Entry)
    {
        TArray<const FWowQuestObjective*> Result;
        for (const FWowQuestObjective& Objective : Entry.Objectives)
            if (Objective.Required > 0 || (!Objective.CreatureOrGOId && !Objective.ItemId && !Objective.Text.IsEmpty()))
                Result.Add(&Objective);
        return Result;
    }

    void FireQuestLogUpdate(lua_State* L)
    {
        if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->EventSystem)
            Ctx->EventSystem->FireEvent(TEXT("QUEST_LOG_UPDATE"));
    }

    // ── Log ──────────────────────────────────────────────────────────────────────

    int QL_GetNumQuestLogEntries(lua_State* L)
    {
        FWowPacketHandler* H = Handler(L);
        lua_pushinteger(L, H ? BuildRows(*H).Num() : 0); // numEntries (headers + visible quests)
        lua_pushinteger(L, H ? H->QuestLog.Num() : 0);   // numQuests
        return 2;
    }

    int QL_GetQuestLogTitle(lua_State* L)
    {
        // title, level, questTag, suggestedGroup, isHeader, isCollapsed, isComplete, isDaily, questID
        FWowPacketHandler* H = Handler(L);
        if (!H) return 0;
        const int32 Index = int32(luaL_optinteger(L, 1, 0));
        const TArray<FQuestRow> Rows = BuildRows(*H);
        if (!Rows.IsValidIndex(Index - 1)) return 0;
        const FQuestRow& Row = Rows[Index - 1];
        if (Row.bHeader)
        {
            PushString(L, Row.Header);
            lua_pushnil(L); lua_pushnil(L); lua_pushnil(L);
            lua_pushinteger(L, 1);
            if (GCollapsedHeaders.Contains(Row.Header)) lua_pushinteger(L, 1); else lua_pushnil(L);
            lua_pushnil(L); lua_pushnil(L); lua_pushnil(L);
            return 9;
        }
        const FWowQuestLogEntry& Entry = H->QuestLog[Row.LogIndex];
        PushString(L, QuestTitle(*H, Entry));
        lua_pushinteger(L, QuestLevel(*H, Entry));
        lua_pushnil(L);                 // questTag (Elite, Dungeon, ...) - quest type not kept yet
        lua_pushinteger(L, 0);          // suggestedGroup
        lua_pushnil(L);                 // isHeader
        lua_pushnil(L);                 // isCollapsed
        if (Entry.State == 1) lua_pushinteger(L, 1);
        else if (Entry.State == 2) lua_pushinteger(L, -1);
        else lua_pushnil(L);            // isComplete: 1 done, -1 failed
        lua_pushnil(L);                 // isDaily
        lua_pushinteger(L, lua_Integer(Entry.QuestId));
        return 9;
    }

    int QL_GetQuestLogIndexByID(lua_State* L)
    {
        FWowPacketHandler* H = Handler(L);
        lua_pushinteger(L, H ? RowOfQuest(*H, uint32(luaL_optinteger(L, 1, 0))) : 0);
        return 1;
    }

    int QL_SelectQuestLogEntry(lua_State* L)
    {
        FWowPacketHandler* H = Handler(L);
        if (!H) return 0;
        const int32 Index = int32(luaL_optinteger(L, 1, 0));
        const TArray<FQuestRow> Rows = BuildRows(*H);
        GSelectedQuestId = 0;
        GSelectedHeader.Reset();
        if (Rows.IsValidIndex(Index - 1))
        {
            if (Rows[Index - 1].bHeader) GSelectedHeader = Rows[Index - 1].Header;
            else GSelectedQuestId = H->QuestLog[Rows[Index - 1].LogIndex].QuestId;
        }
        return 0;
    }

    int QL_GetQuestLogSelection(lua_State* L)
    {
        FWowPacketHandler* H = Handler(L);
        int32 Row = 0;
        if (H && GSelectedQuestId) Row = RowOfQuest(*H, GSelectedQuestId);
        else if (H && !GSelectedHeader.IsEmpty())
        {
            const TArray<FQuestRow> Rows = BuildRows(*H);
            for (int32 I = 0; I < Rows.Num() && !Row; ++I)
                if (Rows[I].bHeader && Rows[I].Header == GSelectedHeader) Row = I + 1;
        }
        lua_pushinteger(L, Row);
        return 1;
    }

    int QL_GetQuestLogQuestText(lua_State* L)
    {
        // description, objectives of the selected quest
        const FWowQuestLogEntry* Entry = QuestArg(L, 1);
        if (!Entry) { lua_pushstring(L, ""); lua_pushstring(L, ""); return 2; }
        PushString(L, Entry->Details);
        PushString(L, Entry->ObjectiveText);
        return 2;
    }

    int QL_ExpandCollapseHeader(lua_State* L, bool bCollapse)
    {
        FWowPacketHandler* H = Handler(L);
        if (!H) return 0;
        const int32 Index = int32(luaL_optinteger(L, 1, 0));
        const TArray<FQuestRow> Rows = BuildRows(*H);
        if (Index == 0)
        {
            // 0 = every header.
            if (bCollapse) { for (const FWowQuestLogEntry& Entry : H->QuestLog) GCollapsedHeaders.Add(HeaderName(Entry)); }
            else GCollapsedHeaders.Reset();
        }
        else if (Rows.IsValidIndex(Index - 1) && Rows[Index - 1].bHeader)
        {
            if (bCollapse) GCollapsedHeaders.Add(Rows[Index - 1].Header);
            else GCollapsedHeaders.Remove(Rows[Index - 1].Header);
        }
        else return 0;
        FireQuestLogUpdate(L);
        return 0;
    }
    int QL_ExpandQuestHeader(lua_State* L) { return QL_ExpandCollapseHeader(L, false); }
    int QL_CollapseQuestHeader(lua_State* L) { return QL_ExpandCollapseHeader(L, true); }

    int QL_IsQuestComplete(lua_State* L)
    {
        // IsQuestComplete(questID)
        FWowPacketHandler* H = Handler(L);
        const uint32 QuestId = uint32(luaL_optinteger(L, 1, 0));
        const FWowQuestLogEntry* Entry = H ? H->QuestLog.FindByPredicate([QuestId](const FWowQuestLogEntry& E) { return E.QuestId == QuestId; }) : nullptr;
        lua_pushboolean(L, Entry && Entry->State == 1);
        return 1;
    }

    // ── Objectives ───────────────────────────────────────────────────────────────

    int QL_GetNumQuestLeaderBoards(lua_State* L)
    {
        const FWowQuestLogEntry* Entry = QuestArg(L, 1);
        lua_pushinteger(L, Entry ? LeaderBoards(*Entry).Num() : 0);
        return 1;
    }

    int QL_GetQuestLogLeaderBoard(lua_State* L)
    {
        // GetQuestLogLeaderBoard(i [, questIndex]) -> text, type, finished
        FWowPacketHandler* H = Handler(L);
        const FWowQuestLogEntry* Entry = QuestArg(L, 2);
        if (!H || !Entry) return 0;
        const TArray<const FWowQuestObjective*> Boards = LeaderBoards(*Entry);
        const int32 I = int32(luaL_optinteger(L, 1, 0)) - 1;
        if (!Boards.IsValidIndex(I)) return 0;
        const FWowQuestObjective& Objective = *Boards[I];
        const uint32 Count = FMath::Min(Objective.Count, Objective.Required);
        const bool bFinished = Objective.Required > 0 ? Objective.Count >= Objective.Required : Entry->State == 1;
        FString Text, Type;
        if (Objective.ItemId)
        {
            FString Name = Objective.Text;
            if (Name.IsEmpty()) Name = LookupItem(L, Objective.ItemId).Name;
            Text = FString::Printf(TEXT("%s: %u/%u"), *Name, Count, Objective.Required);
            Type = TEXT("item");
        }
        else if (Objective.CreatureOrGOId & 0x80000000u)
        {
            Text = FString::Printf(TEXT("%s: %u/%u"), *Objective.Text, Count, Objective.Required);
            Type = TEXT("object");
        }
        else if (Objective.CreatureOrGOId)
        {
            // The creature's own name reads "<name> slain: x/y"; a quest's custom objective text reads "<text>: x/y".
            const FString* CreatureName = H->CreatureNameCache.Find(Objective.CreatureOrGOId);
            FString Name = Objective.Text;
            const bool bSlain = Name.IsEmpty() || (CreatureName && *CreatureName == Name);
            if (Name.IsEmpty() && CreatureName) Name = *CreatureName;
            if (bSlain) { Text = FString::Printf(TEXT("%s slain: %u/%u"), *Name, Count, Objective.Required); }
            else { Text = FString::Printf(TEXT("%s: %u/%u"), *Name, Count, Objective.Required); }
            Type = TEXT("monster");
        }
        else
        {
            Text = Objective.Required > 0 ? FString::Printf(TEXT("%s: %u/%u"), *Objective.Text, Count, Objective.Required) : Objective.Text;
            Type = TEXT("event");
        }
        PushString(L, Text);
        PushString(L, Type);
        if (bFinished) lua_pushinteger(L, 1); else lua_pushnil(L);
        return 3;
    }

    // ── Watches ──────────────────────────────────────────────────────────────────

    int QL_IsQuestWatched(lua_State* L)
    {
        const FWowQuestLogEntry* Entry = QuestArg(L, 1, false);
        if (Entry && Entry->bIsTracked) lua_pushinteger(L, 1); else lua_pushnil(L);
        return 1;
    }

    int QL_GetNumQuestWatches(lua_State* L)
    {
        FWowPacketHandler* H = Handler(L);
        int32 Count = 0;
        if (H) for (const FWowQuestLogEntry& Entry : H->QuestLog) Count += Entry.bIsTracked ? 1 : 0;
        lua_pushinteger(L, Count);
        return 1;
    }

    int QL_GetQuestIndexForWatch(lua_State* L)
    {
        // n-th watched quest (log order) -> its log index
        FWowPacketHandler* H = Handler(L);
        int32 Wanted = int32(luaL_optinteger(L, 1, 0));
        if (!H || Wanted <= 0) return 0;
        for (const FWowQuestLogEntry& Entry : H->QuestLog)
            if (Entry.bIsTracked && --Wanted == 0)
            {
                int32 Row = RowOfQuest(*H, Entry.QuestId);
                // r406 (Stefan 16.10, build 232522: "Objectives (2)" but empty; log: GetQuestIndexForWatch(1)
                // was nil): a watched quest under a collapsed quest-log header had no index, so the
                // tracker skipped it. Its header is opened, so the tracker always lists what is watched.
                if (!Row && GCollapsedHeaders.Remove(HeaderName(Entry)) > 0)
                {
                    UE_LOG(LogWowQuestLogApi, Log, TEXT("Quest watch %u: its header '%s' was collapsed - opened for the tracker"), Entry.QuestId, *HeaderName(Entry));
                    Row = RowOfQuest(*H, Entry.QuestId);
                }
                if (!Row) return 0;
                lua_pushinteger(L, Row);
                return 1;
            }
        return 0;
    }

    int QL_SetQuestWatch(lua_State* L, bool bWatch)
    {
        FWowPacketHandler* H = Handler(L);
        FWowQuestLogEntry* Entry = QuestArg(L, 1, false);
        if (!H || !Entry || Entry->bIsTracked == bWatch) return 0;
        if (bWatch)
        {
            int32 Count = 0;
            for (const FWowQuestLogEntry& Other : H->QuestLog) Count += Other.bIsTracked ? 1 : 0;
            if (Count >= MAX_WATCHABLE_QUESTS) return 0;
        }
        Entry->bIsTracked = bWatch;
        UE_LOG(LogWowQuestLogApi, Log, TEXT("Lua quest watch: %s %u"), bWatch ? TEXT("add") : TEXT("remove"), Entry->QuestId);
        // The controller saves the choice and rebuilds the tracker and the quest log.
        H->OnQuestWatchChanged.Broadcast();
        return 0;
    }
    int QL_AddQuestWatch(lua_State* L) { return QL_SetQuestWatch(L, true); }
    int QL_RemoveQuestWatch(lua_State* L) { return QL_SetQuestWatch(L, false); }

    // ── Quest item (WatchFrame item buttons) ─────────────────────────────────────

    int QL_GetQuestLogSpecialItemInfo(lua_State* L)
    {
        // link, texture, charges
        const FWowQuestLogEntry* Entry = QuestArg(L, 1, false);
        const uint32 ItemId = Entry ? SpecialItem(L, *Entry) : 0;
        if (!ItemId) return 0;
        const FItemInfo Info = LookupItem(L, ItemId);
        if (!Info.bValid) return 0;
        PushString(L, Info.Link);
        PushString(L, Info.Texture);
        lua_pushinteger(L, 0);
        return 3;
    }

    int QL_GetQuestLogSpecialItemCooldown(lua_State* L)
    {
        // start, duration, enable - the item's own cooldown
        const FWowQuestLogEntry* Entry = QuestArg(L, 1, false);
        const uint32 ItemId = Entry ? SpecialItem(L, *Entry) : 0;
        if (ItemId)
        {
            const int Top = lua_gettop(L);
            lua_getglobal(L, "GetItemCooldown");
            if (lua_isfunction(L, -1))
            {
                lua_pushinteger(L, lua_Integer(ItemId));
                if (lua_pcall(L, 1, 3, 0) == 0) return 3;
            }
            lua_settop(L, Top);
        }
        lua_pushinteger(L, 0); lua_pushinteger(L, 0); lua_pushinteger(L, 1);
        return 3;
    }

    int QL_IsQuestLogSpecialItemInRange(lua_State* L)
    {
        // nil = no range to check (the quest items here are used on yourself or the target by the server).
        lua_pushnil(L);
        return 1;
    }

    int QL_UseQuestLogSpecialItem(lua_State* L)
    {
        const FWowQuestLogEntry* Entry = QuestArg(L, 1, false);
        const uint32 ItemId = Entry ? SpecialItem(L, *Entry) : 0;
        if (UWowConnectionManager* Conn = Connection(L); Conn && ItemId)
        {
            UE_LOG(LogWowQuestLogApi, Log, TEXT("UseQuestLogSpecialItem: quest %u item %u"), Entry->QuestId, ItemId);
            Conn->UseItemByEntry(ItemId);
        }
        return 0;
    }

    // ── Links and rewards ────────────────────────────────────────────────────────

    int QL_GetQuestLink(lua_State* L)
    {
        FWowPacketHandler* H = Handler(L);
        const FWowQuestLogEntry* Entry = QuestArg(L, 1, false);
        if (!H || !Entry) return 0;
        const int32 Level = QuestLevel(*H, *Entry);
        PushString(L, FString::Printf(TEXT("|c%s|Hquest:%u:%d|h[%s]|h|r"), *DifficultyHex(L, Level, PlayerLevel(*H)),
            Entry->QuestId, Level, *QuestTitle(*H, *Entry)));
        return 1;
    }

    const TArray<TPair<uint32, uint32>>* RewardList(const FWowQuestLogEntry* Entry, bool bChoice)
    {
        return Entry ? (bChoice ? &Entry->RewardChoiceItems : &Entry->RewardItems) : nullptr;
    }

    int QL_GetNumQuestLogRewards(lua_State* L)
    {
        const auto* List = RewardList(QuestArg(L, 1), false);
        lua_pushinteger(L, List ? List->Num() : 0);
        return 1;
    }

    int QL_GetNumQuestLogChoices(lua_State* L)
    {
        const auto* List = RewardList(QuestArg(L, 1), true);
        lua_pushinteger(L, List ? List->Num() : 0);
        return 1;
    }

    int RewardInfo(lua_State* L, bool bChoice)
    {
        // name, texture, numItems, quality, isUsable
        const auto* List = RewardList(QuestArg(L, 2), bChoice);
        const int32 I = int32(luaL_optinteger(L, 1, 0)) - 1;
        if (!List || !List->IsValidIndex(I)) return 0;
        const FItemInfo Info = LookupItem(L, (*List)[I].Key);
        PushString(L, Info.Name);
        PushString(L, Info.Texture.IsEmpty() ? FString(TEXT("Interface\\Icons\\INV_Misc_QuestionMark")) : Info.Texture);
        lua_pushinteger(L, lua_Integer((*List)[I].Value));
        lua_pushinteger(L, Info.Quality);
        lua_pushinteger(L, 1);
        return 5;
    }
    int QL_GetQuestLogRewardInfo(lua_State* L) { return RewardInfo(L, false); }
    int QL_GetQuestLogChoiceInfo(lua_State* L) { return RewardInfo(L, true); }

    int QL_GetQuestLogItemLink(lua_State* L)
    {
        // GetQuestLogItemLink("reward" | "choice", i)
        const char* Type = luaL_optstring(L, 1, "reward");
        const auto* List = RewardList(QuestArg(L, 3), FCStringAnsi::Stricmp(Type, "choice") == 0);
        const int32 I = int32(luaL_optinteger(L, 2, 0)) - 1;
        if (!List || !List->IsValidIndex(I)) return 0;
        const FItemInfo Info = LookupItem(L, (*List)[I].Key);
        if (Info.Link.IsEmpty()) return 0;
        PushString(L, Info.Link);
        return 1;
    }

    int QL_GetQuestLogRewardMoney(lua_State* L)
    {
        const FWowQuestLogEntry* Entry = QuestArg(L, 1);
        lua_pushinteger(L, Entry ? FMath::Max(0, Entry->RewardMoney) : 0);
        return 1;
    }

    int QL_GetQuestLogRequiredMoney(lua_State* L)
    {
        const FWowQuestLogEntry* Entry = QuestArg(L, 1);
        lua_pushinteger(L, Entry ? FMath::Max(0, -Entry->RewardMoney) : 0);
        return 1;
    }

    int QL_GetQuestLogRewardSpell(lua_State* L)
    {
        // texture, name, isTradeskillSpell, isSpellLearned
        const FWowQuestLogEntry* Entry = QuestArg(L, 1);
        if (!Entry || !Entry->RewardSpell) return 0;
        const int Top = lua_gettop(L);
        lua_getglobal(L, "GetSpellInfo");
        if (!lua_isfunction(L, -1)) { lua_settop(L, Top); return 0; }
        lua_pushinteger(L, lua_Integer(Entry->RewardSpell));
        if (lua_pcall(L, 1, 3, 0) != 0) { lua_settop(L, Top); return 0; }
        // stack: name, rank, icon
        lua_pushvalue(L, -1);   // texture
        lua_pushvalue(L, -4);   // name
        lua_pushnil(L);         // isTradeskillSpell
        lua_pushnil(L);         // isSpellLearned
        return 4;
    }

    int QL_GetQuestLogSpellLink(lua_State* L)
    {
        const FWowQuestLogEntry* Entry = QuestArg(L, 1);
        if (!Entry || !Entry->RewardSpell) return 0;
        const int Top = lua_gettop(L);
        lua_getglobal(L, "GetSpellLink");
        if (!lua_isfunction(L, -1)) { lua_settop(L, Top); return 0; }
        lua_pushinteger(L, lua_Integer(Entry->RewardSpell));
        if (lua_pcall(L, 1, 1, 0) != 0) { lua_settop(L, Top); return 0; }
        return 1;
    }

    // ── Abandon / share ──────────────────────────────────────────────────────────

    int QuestLogSlot(FWowPacketHandler& H, uint32 QuestId)
    {
        const FWowPlayerEntity* Player = H.EntityManager.GetLocalPlayer();
        if (!Player || !QuestId) return INDEX_NONE;
        for (uint16 Slot = 0; Slot < PlayerField::QUEST_LOG_SLOT_COUNT; ++Slot)
            if (Player->GetField(PlayerField::QUEST_LOG_START + Slot * PlayerField::QUEST_LOG_FIELDS_PER_SLOT) == QuestId)
                return Slot;
        return INDEX_NONE;
    }

    int QL_SetAbandonQuest(lua_State* L)
    {
        GAbandonQuestId = GSelectedQuestId;
        return 0;
    }

    int QL_GetAbandonQuestName(lua_State* L)
    {
        FWowPacketHandler* H = Handler(L);
        const FWowQuestLogEntry* Entry = H && GAbandonQuestId
            ? H->QuestLog.FindByPredicate([](const FWowQuestLogEntry& E) { return E.QuestId == GAbandonQuestId; }) : nullptr;
        PushString(L, Entry ? QuestTitle(*H, *Entry) : FString());
        return 1;
    }

    int QL_AbandonQuest(lua_State* L)
    {
        FWowPacketHandler* H = Handler(L);
        const uint32 QuestId = GAbandonQuestId;
        GAbandonQuestId = 0;
        if (!H || !QuestId) return 0;
        const int32 Slot = QuestLogSlot(*H, QuestId);
        if (Slot == INDEX_NONE || !H->OnSendPacket.IsBound()) return 0;
        UE_LOG(LogWowQuestLogApi, Log, TEXT("AbandonQuest: quest %u slot %d"), QuestId, Slot);
        TArray<uint8> Data = { uint8(Slot) };
        H->OnSendPacket.Execute(CMSG_QUESTLOG_REMOVE_QUEST, Data);
        // Same as the native log: remove now, the field update that follows rebuilds the list.
        H->QuestLog.RemoveAll([QuestId](const FWowQuestLogEntry& E) { return E.QuestId == QuestId; });
        if (GSelectedQuestId == QuestId) GSelectedQuestId = 0;
        H->OnQuestLogUpdated.Broadcast();
        return 0;
    }

    // ── r339: what the stock WatchFrame needs besides the log ──────────────────

    int QL_GetQuestLogCompletionText(lua_State* L)
    {
        // The quest's "complete" line ("Return to Marshal McBride ..."); WatchFrame shows it for a finished quest.
        const FWowQuestLogEntry* Entry = QuestArg(L, 1);
        if (!Entry) return 0;
        PushString(L, Entry->CompletionText.IsEmpty() ? Entry->ObjectiveText : Entry->CompletionText);
        return 1;
    }

    int QL_GetQuestSortIndex(lua_State* L)
    {
        // The log index of the header above a quest (WatchFrame expands it before opening the quest).
        FWowPacketHandler* H = Handler(L);
        const int32 Index = int32(luaL_optinteger(L, 1, 0));
        if (!H) return 0;
        const TArray<FQuestRow> Rows = BuildRows(*H);
        for (int32 I = FMath::Min(Index - 1, Rows.Num() - 1); I >= 0; --I)
            if (Rows[I].bHeader) { lua_pushinteger(L, I + 1); return 1; }
        return 0;
    }

    int QL_GetQuestWatchIndex(lua_State* L)
    {
        // log index -> its position among the watched quests
        FWowPacketHandler* H = Handler(L);
        const FWowQuestLogEntry* Entry = QuestArg(L, 1, false);
        if (!H || !Entry || !Entry->bIsTracked) return 0;
        int32 Watch = 0;
        for (const FWowQuestLogEntry& Other : H->QuestLog)
        {
            if (!Other.bIsTracked) continue;
            ++Watch;
            if (Other.QuestId == Entry->QuestId) { lua_pushinteger(L, Watch); return 1; }
        }
        return 0;
    }

    int QL_SortQuestWatches(lua_State* L)
    {
        // Watches keep the quest log's order here; nothing moved.
        lua_pushboolean(L, 0);
        return 1;
    }

    int QL_ShiftQuestWatches(lua_State* L) { return 0; }

    /** Quests of the zone you are in (WatchFrame's LOCAL_MAP_QUESTS, for its "other zones" filter). */
    TArray<uint32> GMapQuests;

    int QL_QuestMapUpdateAllQuests(lua_State* L)
    {
        GMapQuests.Reset();
        FWowPacketHandler* H = Handler(L);
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        UWorld* World = Ctx && Ctx->AudioManager ? Ctx->AudioManager->GetWorld() : nullptr;
        const AWowWorldManager* Terrain = World ? Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(World, AWowWorldManager::StaticClass())) : nullptr;
        const uint32 ZoneId = Terrain ? Terrain->GetCurrentZoneId() : 0;
        if (H && ZoneId)
            for (const FWowQuestLogEntry& Entry : H->QuestLog)
                if (Entry.ZoneOrSort > 0 && uint32(Entry.ZoneOrSort) == ZoneId) GMapQuests.Add(Entry.QuestId);
        lua_pushinteger(L, GMapQuests.Num());
        return 1;
    }

    int QL_QuestPOIGetQuestIDByVisibleIndex(lua_State* L)
    {
        const int32 I = int32(luaL_optinteger(L, 1, 0)) - 1;
        if (!GMapQuests.IsValidIndex(I)) return 0;
        lua_pushinteger(L, lua_Integer(GMapQuests[I]));
        return 1;
    }

    int QL_QuestLogPushQuest(lua_State* L)
    {
        FWowPacketHandler* H = Handler(L);
        const FWowQuestLogEntry* Entry = QuestArg(L, 1);
        if (!H || !Entry || !H->OnSendPacket.IsBound()) return 0;
        TArray<uint8> Data;
        Data.Append(reinterpret_cast<const uint8*>(&Entry->QuestId), 4);
        H->OnSendPacket.Execute(CMSG_PUSHQUESTTOPARTY, Data);
        return 0;
    }
}
#endif

void WowLuaApi::RegisterQuestLogApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    using namespace WowQuestLogApiImpl;
    lua_register(L, "GetNumQuestLogEntries", QL_GetNumQuestLogEntries);
    lua_register(L, "GetQuestLogTitle", QL_GetQuestLogTitle);
    lua_register(L, "GetQuestLogIndexByID", QL_GetQuestLogIndexByID);
    lua_register(L, "SelectQuestLogEntry", QL_SelectQuestLogEntry);
    lua_register(L, "GetQuestLogSelection", QL_GetQuestLogSelection);
    lua_register(L, "GetQuestLogQuestText", QL_GetQuestLogQuestText);
    lua_register(L, "ExpandQuestHeader", QL_ExpandQuestHeader);
    lua_register(L, "CollapseQuestHeader", QL_CollapseQuestHeader);
    lua_register(L, "IsQuestComplete", QL_IsQuestComplete);
    lua_register(L, "GetNumQuestLeaderBoards", QL_GetNumQuestLeaderBoards);
    lua_register(L, "GetQuestLogLeaderBoard", QL_GetQuestLogLeaderBoard);
    lua_register(L, "IsQuestWatched", QL_IsQuestWatched);
    lua_register(L, "GetNumQuestWatches", QL_GetNumQuestWatches);
    lua_register(L, "GetQuestIndexForWatch", QL_GetQuestIndexForWatch);
    lua_register(L, "AddQuestWatch", QL_AddQuestWatch);
    lua_register(L, "RemoveQuestWatch", QL_RemoveQuestWatch);
    lua_register(L, "GetQuestLogSpecialItemInfo", QL_GetQuestLogSpecialItemInfo);
    lua_register(L, "GetQuestLogSpecialItemCooldown", QL_GetQuestLogSpecialItemCooldown);
    lua_register(L, "IsQuestLogSpecialItemInRange", QL_IsQuestLogSpecialItemInRange);
    lua_register(L, "UseQuestLogSpecialItem", QL_UseQuestLogSpecialItem);
    lua_register(L, "GetQuestLink", QL_GetQuestLink);
    lua_register(L, "GetNumQuestLogRewards", QL_GetNumQuestLogRewards);
    lua_register(L, "GetNumQuestLogChoices", QL_GetNumQuestLogChoices);
    lua_register(L, "GetQuestLogRewardInfo", QL_GetQuestLogRewardInfo);
    lua_register(L, "GetQuestLogChoiceInfo", QL_GetQuestLogChoiceInfo);
    lua_register(L, "GetQuestLogItemLink", QL_GetQuestLogItemLink);
    lua_register(L, "GetQuestLogRewardMoney", QL_GetQuestLogRewardMoney);
    lua_register(L, "GetQuestLogRequiredMoney", QL_GetQuestLogRequiredMoney);
    lua_register(L, "GetQuestLogRewardSpell", QL_GetQuestLogRewardSpell);
    lua_register(L, "GetQuestLogSpellLink", QL_GetQuestLogSpellLink);
    lua_register(L, "SetAbandonQuest", QL_SetAbandonQuest);
    lua_register(L, "GetAbandonQuestName", QL_GetAbandonQuestName);
    lua_register(L, "AbandonQuest", QL_AbandonQuest);
    lua_register(L, "QuestLogPushQuest", QL_QuestLogPushQuest);
    lua_register(L, "GetQuestLogCompletionText", QL_GetQuestLogCompletionText);
    lua_register(L, "GetQuestSortIndex", QL_GetQuestSortIndex);
    lua_register(L, "GetQuestWatchIndex", QL_GetQuestWatchIndex);
    lua_register(L, "SortQuestWatches", QL_SortQuestWatches);
    lua_register(L, "ShiftQuestWatches", QL_ShiftQuestWatches);
    lua_register(L, "QuestMapUpdateAllQuests", QL_QuestMapUpdateAllQuests);
    lua_register(L, "QuestPOIGetQuestIDByVisibleIndex", QL_QuestPOIGetQuestIDByVisibleIndex);
    // r339 (Stefan 14.10: the tracker should be a real frame for /fstack and addons): the stock
    // WatchFrame is the tracker. Its clicks go to the client's own windows: a quest opens the native
    // quest log, Ctrl toggles its world diamond, "show on map" opens the native map. Hooked once,
    // from the UI tick, after FrameXML (WatchFrame.lua, QuestLogFrame.lua, WorldMapFrame.lua) loaded.
    luaL_dostring(L, R"LUA(
-- r383 (Stefan 16.10: still no objective tracker): run WatchFrame_Update and log why it failed. An
-- error inside it leaves WatchFrame.updating set, and WatchFrame_Update returns at once for good.
local wowWatchLastError
function __WowWatchFrameRun(why)
    if not (WatchFrame and WatchFrame_Update) then return end
    local ok, err = pcall(WatchFrame_Update, WatchFrame)
    if not ok then
        WatchFrame.updating = nil
        err = tostring(err)
        if err ~= wowWatchLastError and __WowLog then __WowLog("WatchFrame_Update failed (" .. why .. "): " .. err) end
        wowWatchLastError = err
    end
end
-- Every 2 s: free a stuck update, redraw when quests are tracked but no line shows, and log the
-- tracker's state once per change.
local wowWatchNext, wowWatchState, wowWatchBusySince = 0, nil, nil
-- r391 (Stefan 16.10: still no objective tracker): WatchFrame.xml uses <OnLoad function="WatchFrame_OnLoad"/>,
-- which the frame loader skips (its function= OnLoad list is narrow), so WatchFrame never got its
-- objective handlers (quest timers, achievements, quests), its line caches or its events: every
-- WatchFrame_Update drew nothing. Run the stock OnLoad once, then update.
-- r406: no quest timers here; the API placeholder returned one nil, which drew an empty
-- "Quest Timers" block at the top of the tracker.
local function WowEnsureWatchFrameLoaded(wf)
    GetQuestTimers = function() end
    if wf.__wowOnLoadDone or not WatchFrame_OnLoad or not WATCHFRAME_OBJECTIVEHANDLERS then return end
    wf.__wowOnLoadDone = true
    if #WATCHFRAME_OBJECTIVEHANDLERS > 0 and wf.lineCache then return end
    local ok, err = pcall(WatchFrame_OnLoad, wf)
    if __WowLog then
        __WowLog(string.format("WatchFrame: ran WatchFrame_OnLoad (%s), %d objective handlers", ok and "ok" or tostring(err), #WATCHFRAME_OBJECTIVEHANDLERS))
    end
    wf.updating = nil
    __WowWatchFrameRun("onload")
end
-- r397 (Stefan 16.10, build 210646: "Objectives (4)" but no quest lines): WatchFrameLines' own OnLoad
-- moves it to UIParent ("Freeedoom") and WatchFrame_Update anchors it to WatchFrame across parents.
-- Here it stayed at its creation place (the line rects were registered near the screen centre,
-- behind the quest log), so the lines were drawn there, not under the header. The line frame is
-- kept inside WatchFrame, where its anchors to the tracker work.
local function WowKeepWatchLinesInTracker(wf)
    local lf = WatchFrameLines
    if wf.__wowLinesReparented or not lf or not lf.GetParent or not lf.SetParent then return end
    wf.__wowLinesReparented = true
    local parent = lf:GetParent()
    local parentName = parent and parent.GetName and parent:GetName()
    if parentName == "WatchFrame" then return end
    lf:SetParent(wf)
    lf.keepRight, lf.keepLeft = nil, nil -- WatchFrame_Update anchors it again
    if __WowLog then __WowLog("WatchFrame: WatchFrameLines moved back into WatchFrame (was " .. tostring(parentName) .. ")") end
    wf.updating = nil
    __WowWatchFrameRun("lines")
end
function __WowWatchFrameWatchdog()
    local wf = WatchFrame
    if not wf or not GetTime then return end
    WowEnsureWatchFrameLoaded(wf)
    WowKeepWatchLinesInTracker(wf)
    local now = GetTime()
    if now < wowWatchNext then return end
    wowWatchNext = now + 2
    if wf.updating then
        if wowWatchBusySince and now - wowWatchBusySince >= 2 then
            wf.updating = nil
            wowWatchBusySince = nil
            __WowWatchFrameRun("stuck")
        else
            wowWatchBusySince = wowWatchBusySince or now
        end
    else
        wowWatchBusySince = nil
    end
    local function CountLines()
        local shown = 0
        for i = 1, 60 do
            local line = _G["WatchFrameLine" .. i]
            if not line then break end
            if line:IsVisible() then shown = shown + 1 end
        end
        return shown
    end
    local watches = GetNumQuestWatches and GetNumQuestWatches() or 0
    local lines = CountLines()
    if watches > 0 and lines == 0 and not wf.collapsed and not wf.updating then
        __WowWatchFrameRun("empty")
        lines = CountLines()
    end
    -- r400 (Stefan 16.10, build 225858: still "Objectives (3)" and no quests; log: WatchFrameLines
    -- 0 x 0 and the visible line empty): WatchFrame_Update sizes the line frame to its widest line
    -- (0 when nothing was measured) and the two-point height did not hold, so every line was 0 wide.
    -- The line frame gets the tracker's width and height when it collapsed to nothing.
    local lf = WatchFrameLines
    if lf and not wf.collapsed and watches > 0 then
        local wantW = (wf:GetWidth() or 0) - (WATCHFRAMELINES_XOFFSET or 24)
        local wantH = (wf:GetHeight() or 0) - (WATCHFRAMELINES_YOFFSET or 42)
        local fixed = false
        if (lf:GetWidth() or 0) < 1 and wantW > 1 then lf:SetWidth(wantW); fixed = true end
        if (lf:GetHeight() or 0) < 1 and wantH > 1 then lf:SetHeight(wantH); fixed = true end
        if fixed and not wf.updating then __WowWatchFrameRun("lineframe") end
    end
    -- Lines that really show a quest: quest lines (WATCHFRAME_QUESTLINES - r406: the quest-timer
    -- lines counted too), shown, with text and a width.
    local questLineSet = {}
    if WATCHFRAME_QUESTLINES then for _, ql in pairs(WATCHFRAME_QUESTLINES) do if ql.GetName and ql:GetName() then questLineSet[ql:GetName()] = true end end end
    local function CountTextLines()
        local n, list = 0, {}
        for i = 1, 60 do
            local line = _G["WatchFrameLine" .. i]
            if not line then break end
            local t = line.text and line.text:GetText()
            if questLineSet["WatchFrameLine" .. i] and line:IsShown() and t and t ~= "" and (line:GetWidth() or 0) > 1 then n = n + 1 end
            if #list < 6 and line:IsVisible() then
                list[#list + 1] = string.format("%d:'%s' %s,%s %sx%s", i, tostring(t), tostring(line:GetLeft()), tostring(line:GetTop()),
                    tostring(line:GetWidth()), tostring(line:GetHeight()))
            end
        end
        return n, table.concat(list, " | ")
    end
    local textLines, lineList = CountTextLines()
    -- While the stock tracker draws no quest at all, the whole stock frame is hidden (its header and
    -- collapse button too - Stefan 16.10: the button vanished after a click), so the client's own
    -- objectives list takes over (GWowUseStockWatchFrame); it comes back once the stock lines work.
    -- The lines keep updating while hidden, so IsShown (not IsVisible) tells.
    if watches > 0 and textLines == 0 and not wf.collapsed then
        if wf:IsShown() then
            wf:Hide()
            if not wf.__wowStockEmpty and __WowLog then __WowLog("WatchFrame: no quest lines drawn - the native objectives list is used") end
            wf.__wowStockEmpty = true
        end
    elseif wf.__wowStockEmpty and (textLines > 0 or watches == 0) then
        wf.__wowStockEmpty = nil; wf:Show()
        if __WowLog then __WowLog("WatchFrame: quest lines drawn - stock tracker shown again") end
    end
    -- r413 (Stefan 17.10, build 010741: "Objectives (3)" with no quests and no button; log: the
    -- tracker was collapsed, 131 wide): WatchFrame_Collapse narrows the frame, but the collapse/
    -- expand button kept its place from the full width (x 248), off the right edge of the screen,
    -- so the tracker could not be opened again. It is pinned to the frame's top right again
    -- (WatchFrame.xml: TOPRIGHT 2,1) whenever the tracker's width or collapsed state changes.
    local button = WatchFrameCollapseExpandButton
    -- r419 (Stefan 18.10, build 165450: "Button is back, but greyed out. Quests are not back"): the
    -- button was disabled (simple mode / ignore cursor) while the tracker stayed collapsed from an
    -- earlier click, so nothing could open it again. A collapsed tracker whose button cannot be used
    -- is expanded, as the stock simple mode does (WatchFrame_ToggleSimpleWatch expands). The settings
    -- that decide it go to the log once.
    if button and not wf.__wowCvarsLogged and __WowLog then
        wf.__wowCvarsLogged = true
        local okEnabled, enabled = pcall(button.IsEnabled, button)
        __WowLog(string.format("WatchFrame settings: advancedWatchFrame=%s watchFrameIgnoreCursor=%s watchFrameState=%s ADVANCED_WATCH_FRAME=%s WATCHFRAME_IGNORECURSOR=%s simpleMode=%s locked=%s buttonEnabled=%s",
            tostring(GetCVar and GetCVar("advancedWatchFrame")), tostring(GetCVar and GetCVar("watchFrameIgnoreCursor")),
            tostring(GetCVar and GetCVar("watchFrameState")), tostring(ADVANCED_WATCH_FRAME), tostring(WATCHFRAME_IGNORECURSOR),
            tostring(wf.simpleMode), tostring(wf.locked), tostring(okEnabled and enabled)))
    end
    if button and not button.__wowClickLogged and button.GetScript and button.SetScript then
        button.__wowClickLogged = true
        local stockClick = button:GetScript("OnClick")
        button:SetScript("OnClick", function(self, ...)
            if __WowLog then __WowLog("WatchFrame: collapse button clicked (collapsed=" .. tostring(WatchFrame.collapsed) .. ")") end
            if stockClick then return stockClick(self, ...) end
            if WatchFrame_CollapseExpandButton_OnClick then return WatchFrame_CollapseExpandButton_OnClick(self, ...) end
        end)
    end
    if button and wf.collapsed and WatchFrame_Expand then
        local okEnabled, enabled = pcall(button.IsEnabled, button)
        if wf.simpleMode or (okEnabled and enabled == false) or (okEnabled and enabled == nil) then
            WatchFrame_Expand(wf)
            if __WowLog then __WowLog("WatchFrame: was collapsed with no usable button - expanded") end
        end
    end
    if button then
        local key = tostring(wf.collapsed) .. ":" .. string.format("%.0f", wf:GetWidth() or 0)
        if key ~= wf.__wowButtonKey then
            wf.__wowButtonKey = key
            button:ClearAllPoints()
            button:SetPoint("TOPRIGHT", wf, "TOPRIGHT", 2, 1)
            if not wf.simpleMode then button:Show() end
            if __WowLog then
                __WowLog(string.format("WatchFrame: collapse button pinned (collapsed=%s width=%s left=%s)",
                    tostring(wf.collapsed), tostring(wf:GetWidth()), tostring(button:GetLeft())))
            end
        end
    end
    -- r384: what the first tracked quest's lines look like, when there are none to see.
    local detail = ""
    if watches > 0 and lines == 0 then
        local qi = GetQuestIndexForWatch and GetQuestIndexForWatch(1)
        local first = WATCHFRAME_QUESTLINES and WATCHFRAME_QUESTLINES[1]
        local calls, last = __WowWatchFrameSetLineInfo()
        detail = string.format(" setline=%d last=[%s] handlers=%s linesTop=%s", calls, last,
            tostring(WATCHFRAME_OBJECTIVEHANDLERS and #WATCHFRAME_OBJECTIVEHANDLERS), tostring(WatchFrameLines and WatchFrameLines:GetTop()))
            .. string.format(" first=%s objectives=%s questlines=%d line1=%s/%s wfBottom=%s",
            tostring(qi), tostring(qi and GetNumQuestLeaderBoards and GetNumQuestLeaderBoards(qi)),
            WATCHFRAME_QUESTLINES and #WATCHFRAME_QUESTLINES or -1,
            tostring(first and first:GetTop()), tostring(first and first:GetBottom()), tostring(wf:GetBottom()))
    end
    -- r397: where the first line and the line frame are, while lines show.
    if lines > 0 then
        local l1, lf = WatchFrameLine1, WatchFrameLines
        local function R(f) return f and string.format("%s,%s %sx%s", tostring(f:GetLeft()), tostring(f:GetTop()), tostring(f:GetWidth()), tostring(f:GetHeight())) or "-" end
        detail = string.format(" line1=[%s] text=[%s] '%s' lineframe=[%s] wf=[%s] textlines=%d fontH=%s spacing=%s need1=%s lines: %s", R(l1), R(l1 and l1.text),
            tostring(l1 and l1.text and l1.text:GetText()), R(lf), R(wf), textLines, tostring(WATCHFRAMELINES_FONTHEIGHT), tostring(WATCHFRAMELINES_FONTSPACING),
            tostring(GetQuestIndexForWatch and GetQuestIndexForWatch(1) and WatchFrame_GetHeightNeededForQuest and WatchFrame_GetHeightNeededForQuest(GetQuestIndexForWatch(1))), lineList)
    end
    local state = string.format("watches=%d lines=%d shown=%s lineframe=%s collapsed=%s size=%.0fx%.0f",
        watches, lines, tostring(wf:IsVisible()), tostring(WatchFrameLines and WatchFrameLines:IsVisible()),
        tostring(wf.collapsed), wf:GetWidth() or 0, wf:GetHeight() or 0) .. detail
    if state ~= wowWatchState then
        wowWatchState = state
        if __WowLog then __WowLog("WatchFrame: " .. state) end
    end
end
-- r386: count WatchFrame_SetLine calls and keep the last line's rect at the moment it is placed.
local wowSetLineCalls, wowSetLineLast = 0, ""
local function WowWrapSetLine()
    if __WowSetLineWrapped or not WatchFrame_SetLine then return end
    __WowSetLineWrapped = true
    local stock = WatchFrame_SetLine
    WatchFrame_SetLine = function(line, anchor, ...)
        stock(line, anchor, ...)
        wowSetLineCalls = wowSetLineCalls + 1
        wowSetLineLast = string.format("%s top=%s bottom=%s anchor=%s", tostring(line and line:GetName()),
            tostring(line and line:GetTop()), tostring(line and line:GetBottom()), tostring(anchor and anchor.GetName and anchor:GetName()))
    end
end
function __WowWatchFrameSetLineInfo() return wowSetLineCalls, wowSetLineLast end
function __WowWatchFrameTick()
    WowWrapSetLine()
    if __WowWatchHooked then return __WowWatchFrameWatchdog() end
    if not WatchFrame or not WatchFrameLinkButtonTemplate_OnLeftClick or not RunBinding then return end
    __WowWatchHooked = true
    local function Act(questID, what) if questID then RunBinding("QUESTTRACKERCLICK:" .. questID .. ":" .. what) end end
    local function WatchQuestID(watchIndex)
        local logIndex = watchIndex and GetQuestIndexForWatch(watchIndex)
        return logIndex and select(9, GetQuestLogTitle(logIndex))
    end
    local stockLeft = WatchFrameLinkButtonTemplate_OnLeftClick
    WatchFrameLinkButtonTemplate_OnLeftClick = function(self, button)
        if self.type == "QUEST" and not IsModifiedClick("QUESTWATCHTOGGLE") then
            local questID = WatchQuestID(self.index)
            if questID then
                if CloseDropDownMenus then CloseDropDownMenus() end
                Act(questID, IsControlKeyDown() and "ctrl" or "log")
                return
            end
        end
        return stockLeft(self, button)
    end
    QuestLog_OpenToQuest = function(logIndex) Act(logIndex and select(9, GetQuestLogTitle(logIndex)), "log") end
    WorldMap_OpenToQuest = function(questID) Act(questID, "map") end
    __WowWatchFrameRun("hook")
    if __WowLog then __WowLog("WatchFrame hooked: quest clicks go to the native quest log / map") end
end
)LUA");
    UE_LOG(LogWowQuestLogApi, Log, TEXT("Quest log Lua API registered (real quest log data)"));
#endif
}
