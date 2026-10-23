// r285 (Stefan 13.10: "guild vaults need the WotLK window, with the rank checks per tab"): the guild bank
// API of the 3.3.5 client for the stock Blizzard_GuildBankUI, on top of the guild bank packets
// (FWowPacketHandler::GuildBank). Tabs are 1-based here and 0-based on the wire; slots 1-98 / 0-97.
// The GUILDBANK* events are fired by the gameplay controller from FWowPacketHandler::OnGuildBankEvent.
#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowItemPresentation.h"
#include "WowEventSystem.h"
#include "WowUpdateFields.h"
#include "Misc/Paths.h"

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
namespace WowGuildBankLua
{
UWowConnectionManager* GBConnection(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    return Ctx ? Ctx->ConnectionManager : nullptr;
}

FWowGuildBankState* GBState(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    return C ? &C->PacketHandler.GuildBank : nullptr;
}

// Lua tab (1-based, from argument Index) -> server tab, or -1.
int32 GBTabArg(lua_State* L, int Index)
{
    const int32 Tab = static_cast<int32>(luaL_optnumber(L, Index, 0)) - 1;
    return Tab >= 0 && Tab < 6 ? Tab : -1;
}

const FWowGuildBankItem* GBItem(lua_State* L, int TabIndex, int SlotIndex)
{
    FWowGuildBankState* S = GBState(L);
    const int32 Tab = GBTabArg(L, TabIndex);
    const int32 Slot = static_cast<int32>(luaL_optnumber(L, SlotIndex, 0)) - 1;
    if (!S || Tab < 0 || !S->Tabs.IsValidIndex(Tab) || Slot < 0 || Slot > 97) return nullptr;
    return S->Tabs[Tab].Items.Find(static_cast<uint8>(Slot));
}

FString GBItemLink(UWowConnectionManager* C, uint32 Entry)
{
    const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(C, Entry);
    static const TCHAR* Colors[] = { TEXT("ff9d9d9d"), TEXT("ffffffff"), TEXT("ff1eff00"), TEXT("ff0070dd"), TEXT("ffa335ee"), TEXT("ffff8000"), TEXT("ffe6cc80"), TEXT("ffe6cc80") };
    const uint32 Quality = Info ? FMath::Min<uint32>(Info->Quality, 7) : 1;
    return FString::Printf(TEXT("|c%s|Hitem:%u:0:0:0:0:0:0:0:0|h[%s]|h|r"), Colors[Quality], Entry,
        Info ? *Info->Name : *FString::Printf(TEXT("Item %u"), Entry));
}

// The player's own guild rank: from MSG_GUILD_PERMISSIONS, else the roster.
int32 GBRank(UWowConnectionManager* C)
{
    if (!C) return -1;
    const FWowPacketHandler& H = C->PacketHandler;
    if (H.GuildBank.bPermissionsKnown) return static_cast<int32>(H.GuildBank.RankId);
    for (const FWowGuildMember& Member : H.GuildRoster)
        if (Member.Guid == H.EntityManager.LocalPlayerGuid) return static_cast<int32>(Member.RankId);
    return -1;
}

uint32 GBTabRights(UWowConnectionManager* C, int32 Tab)
{
    if (!C || Tab < 0 || Tab >= 6) return 0;
    if (GBRank(C) == 0) return 0xFF; // the guild master may do everything
    const FWowPacketHandler& H = C->PacketHandler;
    if (H.GuildBank.bPermissionsKnown) return H.GuildBank.TabRights[Tab];
    const int32 Rank = GBRank(C);
    return H.GuildRanks.IsValidIndex(Rank) ? H.GuildRanks[Rank].TabRights[Tab] : 0;
}

uint32 GBRankRights(UWowConnectionManager* C)
{
    if (!C) return 0;
    const FWowPacketHandler& H = C->PacketHandler;
    if (H.GuildBank.bPermissionsKnown) return H.GuildBank.RankRights;
    const int32 Rank = GBRank(C);
    return H.GuildRanks.IsValidIndex(Rank) ? H.GuildRanks[Rank].Rights : 0;
}

void GBPushTimeAgo(lua_State* L, uint32 Seconds)
{
    // RecentTimeDate(years, months, days, hours)
    const uint32 Hours = Seconds / 3600;
    lua_pushnumber(L, Hours / (24 * 365));
    lua_pushnumber(L, (Hours % (24 * 365)) / (24 * 30));
    lua_pushnumber(L, (Hours % (24 * 30)) / 24);
    lua_pushnumber(L, Hours % 24);
}

void GBPushName(lua_State* L, UWowConnectionManager* C, uint64 Guid)
{
    const FString* Name = C ? C->PacketHandler.PlayerNameCache.Find(Guid) : nullptr;
    if (Name) { lua_pushstring(L, TCHAR_TO_UTF8(**Name)); return; }
    for (const FWowGuildMember& Member : C->PacketHandler.GuildRoster)
        if (Member.Guid == Guid) { lua_pushstring(L, TCHAR_TO_UTF8(*Member.Name)); return; }
    if (Guid) C->SendNameQuery(static_cast<int64>(Guid));
    lua_pushnil(L);
}

int L_GetNumGuildBankTabs(lua_State* L)
{
    FWowGuildBankState* S = GBState(L);
    lua_pushnumber(L, S ? FMath::Max(S->Tabs.Num(), static_cast<int32>(S->PurchasedTabs)) : 0);
    return 1;
}

// name, icon, isViewable, canDeposit, numWithdrawals, remainingWithdrawals
int L_GetGuildBankTabInfo(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    FWowGuildBankState* S = GBState(L);
    const int32 Tab = GBTabArg(L, 1);
    if (!C || !S || Tab < 0 || !S->Tabs.IsValidIndex(Tab)) { lua_pushnil(L); return 1; }
    const FWowGuildBankTab& Info = S->Tabs[Tab];
    const uint32 Rights = GBTabRights(C, Tab);
    const int32 Rank = GBRank(C);
    int32 PerDay = 0;
    if (Rank == 0) PerDay = -1;
    else if (C->PacketHandler.GuildRanks.IsValidIndex(Rank)) PerDay = static_cast<int32>(C->PacketHandler.GuildRanks[Rank].TabSlots[Tab]);
    else PerDay = S->TabWithdrawLimit[Tab];
    lua_pushstring(L, TCHAR_TO_UTF8(*Info.Name));
    FString Icon = Info.Icon;
    if (!Icon.IsEmpty() && !Icon.Contains(TEXT("\\"))) Icon = TEXT("Interface\\Icons\\") + Icon;
    lua_pushstring(L, TCHAR_TO_UTF8(*Icon));
    lua_pushboolean(L, (Rights & 0x01) != 0);
    lua_pushboolean(L, (Rights & 0x02) != 0);
    lua_pushnumber(L, PerDay);
    lua_pushnumber(L, Rank == 0 ? -1 : Info.WithdrawalsRemaining);
    return 6;
}

int L_GetCurrentGuildBankTab(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    lua_pushnumber(L, C ? C->GuildBankCurrentTab : 1);
    return 1;
}

int L_SetCurrentGuildBankTab(lua_State* L)
{
    if (UWowConnectionManager* C = GBConnection(L))
        C->GuildBankCurrentTab = FMath::Clamp(static_cast<int32>(luaL_checknumber(L, 1)), 1, 7);
    return 0;
}

int L_QueryGuildBankTab(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    const int32 Tab = GBTabArg(L, 1);
    if (C && Tab >= 0) C->SendGuildBankQueryTab(static_cast<uint8>(Tab), true);
    return 0;
}

// texture, count, locked
int L_GetGuildBankItemInfo(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    const FWowGuildBankItem* Item = GBItem(L, 1, 2);
    if (!C || !Item) { lua_pushnil(L); return 1; }
    const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(C, Item->Entry);
    lua_pushstring(L, TCHAR_TO_UTF8(*FWowItemPresentation::IconPath(Item->Entry, Info ? Info->DisplayId : 0)));
    lua_pushnumber(L, Item->Count);
    lua_pushboolean(L, 0);
    return 3;
}

int L_GetGuildBankItemLink(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    const FWowGuildBankItem* Item = GBItem(L, 1, 2);
    if (!C || !Item) { lua_pushnil(L); return 1; }
    lua_pushstring(L, TCHAR_TO_UTF8(*GBItemLink(C, Item->Entry)));
    return 1;
}

int GBPickup(lua_State* L, uint32 Split)
{
    UWowConnectionManager* C = GBConnection(L);
    const int32 Tab = GBTabArg(L, 1);
    const int32 Slot = static_cast<int32>(luaL_optnumber(L, 2, 0)) - 1;
    if (!C || Tab < 0 || Slot < 0 || Slot > 97) return 0;
    // An item on the cursor goes into this slot; else this slot's item is picked up.
    if (C->HasCursorItemPayload())
    {
        C->PlaceCursorIntoGuildBankSlot(static_cast<uint8>(Tab), static_cast<uint8>(Slot));
        return 0;
    }
    if (const FWowGuildBankItem* Item = GBItem(L, 1, 2))
        C->PickupGuildBankItemCursor(static_cast<uint8>(Tab), static_cast<uint8>(Slot), Item->Entry, GBItemLink(C, Item->Entry), Split);
    return 0;
}

int L_PickupGuildBankItem(lua_State* L) { return GBPickup(L, 0); }

int L_SplitGuildBankItem(lua_State* L)
{
    const uint32 Split = static_cast<uint32>(FMath::Max(0.0, luaL_optnumber(L, 3, 0)));
    return GBPickup(L, Split);
}

int L_AutoStoreGuildBankItem(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    const int32 Tab = GBTabArg(L, 1);
    const int32 Slot = static_cast<int32>(luaL_optnumber(L, 2, 0)) - 1;
    const FWowGuildBankItem* Item = GBItem(L, 1, 2);
    if (C && Item && Tab >= 0)
        C->SendGuildBankSwapWithInventory(true, static_cast<uint8>(Tab), static_cast<uint8>(Slot), Item->Entry, 0, 0, 0, true);
    return 0;
}

int L_GetGuildBankMoney(lua_State* L)
{
    FWowGuildBankState* S = GBState(L);
    lua_pushnumber(L, S ? static_cast<double>(S->Money) : 0.0);
    return 1;
}

int L_GetGuildBankWithdrawMoney(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    FWowGuildBankState* S = GBState(L);
    lua_pushnumber(L, !S ? 0 : (GBRank(C) == 0 ? -1 : S->RemainingWithdrawMoney));
    return 1;
}

int L_CanWithdrawGuildBankMoney(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    lua_pushboolean(L, C && (GBRank(C) == 0 || (GBRankRights(C) & 0x00080000u) != 0));
    return 1;
}

int L_CanGuildBankRepair(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    lua_pushboolean(L, C && (GBRank(C) == 0 || (GBRankRights(C) & 0x00040000u) != 0));
    return 1;
}

int L_DepositGuildBankMoney(lua_State* L)
{
    if (UWowConnectionManager* C = GBConnection(L))
        C->SendGuildBankDepositMoney(static_cast<uint32>(FMath::Max(0.0, luaL_checknumber(L, 1))));
    return 0;
}

int L_WithdrawGuildBankMoney(lua_State* L)
{
    if (UWowConnectionManager* C = GBConnection(L))
        C->SendGuildBankWithdrawMoney(static_cast<uint32>(FMath::Max(0.0, luaL_checknumber(L, 1))));
    return 0;
}

int L_IsGuildLeader(lua_State* L)
{
    lua_pushboolean(L, GBRank(GBConnection(L)) == 0);
    return 1;
}

int L_CanEditGuildTabInfo(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    const int32 Tab = GBTabArg(L, 1);
    lua_pushboolean(L, C && Tab >= 0 && (GBTabRights(C, Tab) & 0x04) != 0);
    return 1;
}

int L_GetGuildBankTabCost(lua_State* L)
{
    // The 3.3.5 client's prices for tabs 1-6: 100, 250, 500, 1000, 2500, 5000 gold.
    static const int32 Gold[] = { 100, 250, 500, 1000, 2500, 5000 };
    FWowGuildBankState* S = GBState(L);
    const int32 Owned = S ? FMath::Max(S->Tabs.Num(), static_cast<int32>(S->PurchasedTabs)) : 0;
    if (Owned >= 6) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, Gold[Owned] * 10000.0);
    return 1;
}

int L_BuyGuildBankTab(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    FWowGuildBankState* S = GBState(L);
    if (C && S) C->SendGuildBankBuyTab(static_cast<uint8>(FMath::Max(S->Tabs.Num(), static_cast<int32>(S->PurchasedTabs))));
    return 0;
}

int L_SetGuildBankTabInfo(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    const int32 Tab = GBTabArg(L, 1);
    if (!C || Tab < 0) return 0;
    const FString Name = UTF8_TO_TCHAR(luaL_optstring(L, 2, ""));
    // The icon arrives as an index into GetMacroItemIconInfo or as a texture path; the server stores the file name.
    FString Icon;
    if (lua_type(L, 3) == LUA_TNUMBER)
    {
        lua_getglobal(L, "GetMacroItemIconInfo");
        if (lua_isfunction(L, -1))
        {
            lua_pushvalue(L, 3);
            if (lua_pcall(L, 1, 1, 0) == 0 && lua_isstring(L, -1)) Icon = UTF8_TO_TCHAR(lua_tostring(L, -1));
        }
        lua_pop(L, 1);
    }
    else Icon = UTF8_TO_TCHAR(luaL_optstring(L, 3, ""));
    Icon = FPaths::GetCleanFilename(Icon.Replace(TEXT("\\"), TEXT("/")));
    if (!Name.IsEmpty() && !Icon.IsEmpty()) C->SendGuildBankUpdateTab(static_cast<uint8>(Tab), Name, Icon);
    return 0;
}

int L_QueryGuildBankLog(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    const int32 Tab = static_cast<int32>(luaL_optnumber(L, 1, 1)) - 1; // MAX_GUILDBANK_TABS + 1 = money log (6)
    if (C && Tab >= 0 && Tab <= 6) C->SendGuildBankLogQuery(static_cast<uint8>(Tab));
    return 0;
}

const TArray<FWowGuildBankLogEntry>* GBLog(lua_State* L, int32 ServerTab)
{
    FWowGuildBankState* S = GBState(L);
    return S ? S->Logs.Find(static_cast<uint8>(ServerTab)) : nullptr;
}

int L_GetNumGuildBankTransactions(lua_State* L)
{
    const TArray<FWowGuildBankLogEntry>* Log = GBLog(L, GBTabArg(L, 1));
    lua_pushnumber(L, Log ? Log->Num() : 0);
    return 1;
}

// type, name, itemLink, count, tab1, tab2, years, months, days, hours
int L_GetGuildBankTransaction(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    const int32 Tab = GBTabArg(L, 1);
    const TArray<FWowGuildBankLogEntry>* Log = GBLog(L, Tab);
    const int32 Index = static_cast<int32>(luaL_optnumber(L, 2, 0)) - 1;
    if (!C || !Log || !Log->IsValidIndex(Index)) { lua_pushnil(L); return 1; }
    const FWowGuildBankLogEntry& E = (*Log)[Index];
    lua_pushstring(L, E.Type == 1 ? "deposit" : E.Type == 2 ? "withdraw" : "move");
    GBPushName(L, C, E.Player);
    lua_pushstring(L, TCHAR_TO_UTF8(*GBItemLink(C, E.Entry)));
    lua_pushnumber(L, E.Count);
    // Moves: from this tab to the other one (types 3 and 7 are the two directions).
    lua_pushnumber(L, (E.Type == 7 ? E.OtherTab : Tab) + 1);
    lua_pushnumber(L, (E.Type == 7 ? Tab : E.OtherTab) + 1);
    GBPushTimeAgo(L, E.SecondsAgo);
    return 10;
}

int L_GetNumGuildBankMoneyTransactions(lua_State* L)
{
    const TArray<FWowGuildBankLogEntry>* Log = GBLog(L, 6);
    lua_pushnumber(L, Log ? Log->Num() : 0);
    return 1;
}

// type, name, amount, years, months, days, hours
int L_GetGuildBankMoneyTransaction(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    const TArray<FWowGuildBankLogEntry>* Log = GBLog(L, 6);
    const int32 Index = static_cast<int32>(luaL_optnumber(L, 1, 0)) - 1;
    if (!C || !Log || !Log->IsValidIndex(Index)) { lua_pushnil(L); return 1; }
    const FWowGuildBankLogEntry& E = (*Log)[Index];
    lua_pushstring(L, E.Type == 4 ? "deposit" : E.Type == 5 ? "withdraw" : E.Type == 6 ? "repair" : E.Type == 8 ? "withdrawForTab" : "buyTab");
    GBPushName(L, C, E.Player);
    lua_pushnumber(L, E.Money);
    GBPushTimeAgo(L, E.SecondsAgo);
    return 7;
}

int L_QueryGuildBankText(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    const int32 Tab = GBTabArg(L, 1);
    if (C && Tab >= 0) C->SendGuildBankTextQuery(static_cast<uint8>(Tab));
    return 0;
}

int L_GetGuildBankText(lua_State* L)
{
    FWowGuildBankState* S = GBState(L);
    const int32 Tab = GBTabArg(L, 1);
    if (!S || Tab < 0 || !S->Tabs.IsValidIndex(Tab)) { lua_pushnil(L); return 1; }
    lua_pushstring(L, TCHAR_TO_UTF8(*S->Tabs[Tab].Text));
    return 1;
}

int L_SetGuildBankText(lua_State* L)
{
    UWowConnectionManager* C = GBConnection(L);
    const int32 Tab = GBTabArg(L, 1);
    if (!C || Tab < 0) return 0;
    const FString Text = UTF8_TO_TCHAR(luaL_optstring(L, 2, ""));
    C->SendGuildBankSetText(static_cast<uint8>(Tab), Text);
    FWowGuildBankState& S = C->PacketHandler.GuildBank;
    if (S.Tabs.IsValidIndex(Tab)) S.Tabs[Tab].Text = Text;
    return 0;
}

int L_CloseGuildBankFrame(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    if (!Ctx || !Ctx->ConnectionManager || !Ctx->ConnectionManager->ActiveGuildBankGuid) return 0;
    Ctx->ConnectionManager->ActiveGuildBankGuid = 0;
    if (Ctx->EventSystem) Ctx->EventSystem->FireEvent(TEXT("GUILDBANKFRAME_CLOSED"));
    return 0;
}
}
#endif

void WowLuaApi::RegisterGuildBankApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    using namespace WowGuildBankLua;
    lua_register(L, "GetNumGuildBankTabs", L_GetNumGuildBankTabs);
    lua_register(L, "GetGuildBankTabInfo", L_GetGuildBankTabInfo);
    lua_register(L, "GetCurrentGuildBankTab", L_GetCurrentGuildBankTab);
    lua_register(L, "SetCurrentGuildBankTab", L_SetCurrentGuildBankTab);
    lua_register(L, "QueryGuildBankTab", L_QueryGuildBankTab);
    lua_register(L, "GetGuildBankItemInfo", L_GetGuildBankItemInfo);
    lua_register(L, "GetGuildBankItemLink", L_GetGuildBankItemLink);
    lua_register(L, "PickupGuildBankItem", L_PickupGuildBankItem);
    lua_register(L, "SplitGuildBankItem", L_SplitGuildBankItem);
    lua_register(L, "AutoStoreGuildBankItem", L_AutoStoreGuildBankItem);
    lua_register(L, "GetGuildBankMoney", L_GetGuildBankMoney);
    lua_register(L, "GetGuildBankWithdrawMoney", L_GetGuildBankWithdrawMoney);
    lua_register(L, "CanWithdrawGuildBankMoney", L_CanWithdrawGuildBankMoney);
    lua_register(L, "CanGuildBankRepair", L_CanGuildBankRepair);
    lua_register(L, "DepositGuildBankMoney", L_DepositGuildBankMoney);
    lua_register(L, "WithdrawGuildBankMoney", L_WithdrawGuildBankMoney);
    lua_register(L, "IsGuildLeader", L_IsGuildLeader);
    lua_register(L, "CanEditGuildTabInfo", L_CanEditGuildTabInfo);
    lua_register(L, "GetGuildBankTabCost", L_GetGuildBankTabCost);
    lua_register(L, "BuyGuildBankTab", L_BuyGuildBankTab);
    lua_register(L, "SetGuildBankTabInfo", L_SetGuildBankTabInfo);
    lua_register(L, "QueryGuildBankLog", L_QueryGuildBankLog);
    lua_register(L, "GetNumGuildBankTransactions", L_GetNumGuildBankTransactions);
    lua_register(L, "GetGuildBankTransaction", L_GetGuildBankTransaction);
    lua_register(L, "GetNumGuildBankMoneyTransactions", L_GetNumGuildBankMoneyTransactions);
    lua_register(L, "GetGuildBankMoneyTransaction", L_GetGuildBankMoneyTransaction);
    lua_register(L, "QueryGuildBankText", L_QueryGuildBankText);
    lua_register(L, "GetGuildBankText", L_GetGuildBankText);
    lua_register(L, "SetGuildBankText", L_SetGuildBankText);
    lua_register(L, "CloseGuildBankFrame", L_CloseGuildBankFrame);
#endif
}
