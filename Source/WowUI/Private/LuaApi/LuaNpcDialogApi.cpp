// NPC conversation Lua API (3.3.5): gossip (GetGossipText, GetGossipOptions, SelectGossipOption,
// GetGossipAvailableQuests ...), the quest greeting list (GetNumAvailableQuests, SelectActiveQuest
// ...) and the quest panels (GetTitleText, GetQuestText, AcceptQuest, DeclineQuest, CompleteQuest,
// GetQuestReward, GetQuestItemInfo ...). Reads the conversation the native dialog shows
// (FWowNpcDialogState) and acts through the dialog's own button code, so stock frames and quest
// addons drive the same conversation the player sees.
#include "LuaApiRegistry.h"
#include "WowNpcDialogState.h"

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

FWowNpcDialogState& FWowNpcDialogState::Get()
{
    static FWowNpcDialogState State;
    return State;
}

#if HAS_LUA
namespace
{
using EDialogMode = FWowNpcDialogState::EMode;

FWowNpcDialogState& Dialog() { return FWowNpcDialogState::Get(); }
EDialogMode Shown() { return Dialog().ShownMode(); }
bool IsListMode() { return Shown() == EDialogMode::Gossip || Shown() == EDialogMode::Greeting; }
bool IsQuestMode() { const EDialogMode M = Shown(); return M == EDialogMode::Detail || M == EDialogMode::Progress || M == EDialogMode::Reward; }

void PushText(lua_State* L, const FString& Text) { lua_pushstring(L, TCHAR_TO_UTF8(*Text)); }

/** The list's quests that are in the player's log (bActive) or on offer. */
TArray<const FWowQuestMenuItem*> ListQuests(bool bActive)
{
    TArray<const FWowQuestMenuItem*> Out;
    if (IsListMode())
        for (const FWowQuestMenuItem& Item : Dialog().Menu.Quests)
            if (FWowNpcDialogState::IsActiveQuest(Item) == bActive) Out.Add(&Item);
    return Out;
}

const FWowQuestMenuItem* ListQuestAt(lua_State* L, bool bActive)
{
    const TArray<const FWowQuestMenuItem*> Quests = ListQuests(bActive);
    const int32 Index = int32(luaL_optinteger(L, 1, 0)) - 1;
    return Quests.IsValidIndex(Index) ? Quests[Index] : nullptr;
}

// ---- Gossip ----
int L_GetGossipText(lua_State* L) { PushText(L, IsListMode() ? Dialog().Menu.Greeting : FString()); return 1; }
int L_GetNumGossipOptions(lua_State* L) { lua_pushinteger(L, IsListMode() ? Dialog().Menu.Options.Num() : 0); return 1; }

int L_GetGossipOptions(lua_State* L)
{
    // GOSSIP_ICON_* -> the type names GossipFrame.lua expects.
    static const char* Types[] = { "gossip", "vendor", "taxi", "trainer", "healer", "binder", "banker", "petition", "tabard", "battlemaster", "gossip" };
    if (!IsListMode()) return 0;
    int Pushed = 0;
    for (const FWowGossipOption& Option : Dialog().Menu.Options)
    {
        if (!lua_checkstack(L, 2)) break;
        PushText(L, Option.Text);
        lua_pushstring(L, Types[Option.Icon < UE_ARRAY_COUNT(Types) ? Option.Icon : 0]);
        Pushed += 2;
    }
    return Pushed;
}

int L_SelectGossipOption(lua_State* L)
{
    const int32 Index = int32(luaL_optinteger(L, 1, 0)) - 1;
    if (IsListMode() && Dialog().Menu.Options.IsValidIndex(Index) && Dialog().SelectOption) Dialog().SelectOption(Index);
    return 0;
}

int L_GetNumGossipAvailableQuests(lua_State* L) { lua_pushinteger(L, ListQuests(false).Num()); return 1; }
int L_GetNumGossipActiveQuests(lua_State* L) { lua_pushinteger(L, ListQuests(true).Num()); return 1; }

/** title, level, isTrivial, isDaily, isRepeatable per quest on offer. */
int L_GetGossipAvailableQuests(lua_State* L)
{
    int Pushed = 0;
    for (const FWowQuestMenuItem* Item : ListQuests(false))
    {
        if (!lua_checkstack(L, 5)) break;
        PushText(L, Item->Title); lua_pushinteger(L, Item->Level); lua_pushnil(L); lua_pushnil(L);
        if (Item->bRepeatable) lua_pushboolean(L, 1); else lua_pushnil(L);
        Pushed += 5;
    }
    return Pushed;
}

/** title, level, isTrivial, isComplete per quest in the log. */
int L_GetGossipActiveQuests(lua_State* L)
{
    int Pushed = 0;
    for (const FWowQuestMenuItem* Item : ListQuests(true))
    {
        if (!lua_checkstack(L, 4)) break;
        PushText(L, Item->Title); lua_pushinteger(L, Item->Level); lua_pushnil(L); lua_pushnil(L);
        Pushed += 4;
    }
    return Pushed;
}

int SelectListQuest(lua_State* L, bool bActive)
{
    if (const FWowQuestMenuItem* Item = ListQuestAt(L, bActive))
        if (Dialog().SelectQuest) Dialog().SelectQuest(Item->QuestId, bActive);
    return 0;
}
int L_SelectAvailableQuest(lua_State* L) { return SelectListQuest(L, false); }
int L_SelectActiveQuest(lua_State* L) { return SelectListQuest(L, true); }

int L_GetAvailableTitle(lua_State* L) { const FWowQuestMenuItem* Item = ListQuestAt(L, false); PushText(L, Item ? Item->Title : FString()); return 1; }
int L_GetActiveTitle(lua_State* L) { const FWowQuestMenuItem* Item = ListQuestAt(L, true); PushText(L, Item ? Item->Title : FString()); return 1; }
int L_GetAvailableLevel(lua_State* L) { const FWowQuestMenuItem* Item = ListQuestAt(L, false); lua_pushinteger(L, Item ? Item->Level : 0); return 1; }
int L_GetActiveLevel(lua_State* L) { const FWowQuestMenuItem* Item = ListQuestAt(L, true); lua_pushinteger(L, Item ? Item->Level : 0); return 1; }

int L_CloseDialog(lua_State* L)
{
    (void)L;
    if (Shown() != EDialogMode::None && Dialog().Close) Dialog().Close();
    return 0;
}

// ---- Quest panels ----
int L_GetTitleText(lua_State* L) { PushText(L, IsQuestMode() ? Dialog().Quest.Title : FString()); return 1; }
int L_GetQuestText(lua_State* L) { PushText(L, Shown() == EDialogMode::Detail ? Dialog().Quest.Details : FString()); return 1; }
int L_GetObjectiveText(lua_State* L) { PushText(L, IsQuestMode() ? Dialog().Quest.Objectives : FString()); return 1; }
int L_GetProgressText(lua_State* L) { PushText(L, Shown() == EDialogMode::Progress ? Dialog().Quest.Details : FString()); return 1; }
int L_GetRewardText(lua_State* L) { PushText(L, Shown() == EDialogMode::Reward ? Dialog().Quest.Details : FString()); return 1; }
int L_GetQuestID(lua_State* L) { lua_pushinteger(L, IsQuestMode() ? Dialog().Quest.QuestId : 0); return 1; }
int L_IsQuestCompletable(lua_State* L) { lua_pushboolean(L, Shown() == EDialogMode::Progress && Dialog().Quest.bCanComplete); return 1; }
int L_GetNumQuestRewards(lua_State* L) { lua_pushinteger(L, IsQuestMode() ? Dialog().Quest.RewardItems.Num() : 0); return 1; }
int L_GetNumQuestChoices(lua_State* L) { lua_pushinteger(L, IsQuestMode() ? Dialog().Quest.ChoiceRewards.Num() : 0); return 1; }
int L_GetNumQuestItems(lua_State* L) { lua_pushinteger(L, Shown() == EDialogMode::Progress ? Dialog().Quest.RequiredItems.Num() : 0); return 1; }
int L_GetRewardMoney(lua_State* L) { lua_pushinteger(L, IsQuestMode() ? Dialog().Quest.RewardMoney : 0); return 1; }
int L_GetRewardXP(lua_State* L) { lua_pushinteger(L, IsQuestMode() ? Dialog().Quest.RewardXP : 0); return 1; }
int L_GetQuestMoneyToGet(lua_State* L) { lua_pushinteger(L, Shown() == EDialogMode::Progress ? Dialog().Quest.RequiredMoney : 0); return 1; }

/** __WowQuestItem(type, index) -> itemId, count: "required", "reward" or "choice". */
int L_QuestItem(lua_State* L)
{
    if (!IsQuestMode()) return 0;
    const FString Type = UTF8_TO_TCHAR(luaL_optstring(L, 1, ""));
    const int32 Index = int32(luaL_optinteger(L, 2, 0)) - 1;
    const FWowQuestDetails& Quest = Dialog().Quest;
    const TArray<FWowQuestRewardItem>& Items = Type == TEXT("required") ? Quest.RequiredItems
        : Type == TEXT("choice") ? Quest.ChoiceRewards : Quest.RewardItems;
    if (!Items.IsValidIndex(Index) || !Items[Index].ItemId) return 0;
    lua_pushinteger(L, Items[Index].ItemId);
    lua_pushinteger(L, FMath::Max<uint32>(1, Items[Index].Count));
    return 2;
}

int L_AcceptQuest(lua_State* L) { (void)L; if (Shown() == EDialogMode::Detail && Dialog().Accept) Dialog().Accept(); return 0; }
int L_DeclineQuest(lua_State* L) { (void)L; if (IsQuestMode() && Dialog().Decline) Dialog().Decline(); return 0; }
/** CompleteQuest: from the progress panel on to the reward panel (the "Continue" button). */
int L_CompleteQuest(lua_State* L) { (void)L; if (Shown() == EDialogMode::Progress && Dialog().Continue) Dialog().Continue(); return 0; }
/** GetQuestReward(choice): hand the quest in with reward choice (1-based; nil when there is none). */
int L_GetQuestReward(lua_State* L)
{
    if (Shown() != EDialogMode::Reward || !Dialog().Complete) return 0;
    const int32 Choices = Dialog().Quest.ChoiceRewards.Num();
    const int32 Choice = int32(luaL_optinteger(L, 1, 0)) - 1;
    if (Choices > 0 && (Choice < 0 || Choice >= Choices)) return 0; // a choice is required, as in the client
    Dialog().Complete(Choices > 0 ? Choice : 0);
    return 0;
}

void RegisterNpcIfMissing(lua_State* L, const char* Name, lua_CFunction Function)
{
    lua_getglobal(L, Name);
    const bool bExists = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!bExists) lua_register(L, Name, Function);
}
}
#endif

void WowLuaApi::RegisterNpcDialogApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    RegisterNpcIfMissing(L, "GetGossipText", L_GetGossipText);
    RegisterNpcIfMissing(L, "GetNumGossipOptions", L_GetNumGossipOptions);
    RegisterNpcIfMissing(L, "GetGossipOptions", L_GetGossipOptions);
    RegisterNpcIfMissing(L, "SelectGossipOption", L_SelectGossipOption);
    RegisterNpcIfMissing(L, "GetNumGossipAvailableQuests", L_GetNumGossipAvailableQuests);
    RegisterNpcIfMissing(L, "GetNumGossipActiveQuests", L_GetNumGossipActiveQuests);
    RegisterNpcIfMissing(L, "GetGossipAvailableQuests", L_GetGossipAvailableQuests);
    RegisterNpcIfMissing(L, "GetGossipActiveQuests", L_GetGossipActiveQuests);
    RegisterNpcIfMissing(L, "SelectGossipAvailableQuest", L_SelectAvailableQuest);
    RegisterNpcIfMissing(L, "SelectGossipActiveQuest", L_SelectActiveQuest);
    RegisterNpcIfMissing(L, "GetGreetingText", L_GetGossipText);
    RegisterNpcIfMissing(L, "GetNumAvailableQuests", L_GetNumGossipAvailableQuests);
    RegisterNpcIfMissing(L, "GetNumActiveQuests", L_GetNumGossipActiveQuests);
    RegisterNpcIfMissing(L, "GetAvailableTitle", L_GetAvailableTitle);
    RegisterNpcIfMissing(L, "GetActiveTitle", L_GetActiveTitle);
    RegisterNpcIfMissing(L, "GetAvailableLevel", L_GetAvailableLevel);
    RegisterNpcIfMissing(L, "GetActiveLevel", L_GetActiveLevel);
    RegisterNpcIfMissing(L, "SelectAvailableQuest", L_SelectAvailableQuest);
    RegisterNpcIfMissing(L, "SelectActiveQuest", L_SelectActiveQuest);
    RegisterNpcIfMissing(L, "GetTitleText", L_GetTitleText);
    RegisterNpcIfMissing(L, "GetQuestText", L_GetQuestText);
    RegisterNpcIfMissing(L, "GetObjectiveText", L_GetObjectiveText);
    RegisterNpcIfMissing(L, "GetProgressText", L_GetProgressText);
    RegisterNpcIfMissing(L, "GetRewardText", L_GetRewardText);
    RegisterNpcIfMissing(L, "GetQuestID", L_GetQuestID);
    RegisterNpcIfMissing(L, "IsQuestCompletable", L_IsQuestCompletable);
    RegisterNpcIfMissing(L, "GetNumQuestRewards", L_GetNumQuestRewards);
    RegisterNpcIfMissing(L, "GetNumQuestChoices", L_GetNumQuestChoices);
    RegisterNpcIfMissing(L, "GetNumQuestItems", L_GetNumQuestItems);
    RegisterNpcIfMissing(L, "GetRewardMoney", L_GetRewardMoney);
    RegisterNpcIfMissing(L, "GetRewardXP", L_GetRewardXP);
    RegisterNpcIfMissing(L, "GetQuestMoneyToGet", L_GetQuestMoneyToGet);
    RegisterNpcIfMissing(L, "AcceptQuest", L_AcceptQuest);
    RegisterNpcIfMissing(L, "DeclineQuest", L_DeclineQuest);
    RegisterNpcIfMissing(L, "CompleteQuest", L_CompleteQuest);
    RegisterNpcIfMissing(L, "GetQuestReward", L_GetQuestReward);
    lua_register(L, "__WowQuestItem", L_QuestItem);
    // CloseGossip had a no-op stand-in (LuaDeathApi); these close the native dialog.
    lua_register(L, "CloseGossip", L_CloseDialog);
    lua_register(L, "CloseQuest", L_CloseDialog);
    luaL_dostring(L, R"LUA(
-- GetQuestItemInfo(type, index) -> name, texture, numItems, quality, isUsable; GetQuestItemLink.
if not GetQuestItemInfo then
    function GetQuestItemInfo(itemType, index)
        local id, count = __WowQuestItem(itemType, index)
        if not id then return nil end
        local name, _, quality, _, _, _, _, _, _, texture = GetItemInfo(id)
        return name, texture, count, quality, true
    end
end
if not GetQuestItemLink then
    function GetQuestItemLink(itemType, index)
        local id = __WowQuestItem(itemType, index)
        if not id then return nil end
        local _, link = GetItemInfo(id)
        return link
    end
end
)LUA");
#endif
}
