// Blizzard_AuctionUI / auction addon API (3.3.5) on top of the native auction packets:
// QueryAuctionItems, GetNumAuctionItems, GetAuctionItemInfo/Link/TimeLeft, PlaceAuctionBid,
// CancelAuction, ClickAuctionSellItemButton, GetAuctionSellItemInfo, CalculateAuctionDeposit,
// StartAuction, Get/SetSelectedAuctionItem, CanSendAuctionQuery, SortAuctionItems.
// The AUCTION_* events are fired by the gameplay controller when the lists arrive.
#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowNpcServices.h"
#include "WowItemPresentation.h"
#include "WowEventSystem.h"

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
// GetAuctionItemClasses order -> item class id, and per class the GetAuctionItemSubClasses order -> subclass id.
const uint32 GClassIds[] = { 2, 4, 1, 0, 16, 7, 6, 11, 9, 3, 15, 12 };
const TArray<TArray<uint32>>& SubClassIds()
{
    static const TArray<TArray<uint32>> Table = {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 13, 14, 15, 16, 18, 19, 20 },
        { 0, 1, 2, 3, 4, 6, 7, 8, 9, 10 },
        { 0, 1, 2, 3, 4, 5, 6, 7, 8 },
        { 5, 1, 2, 3, 7, 6, 4, 8 },
        { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11 },
        { 10, 5, 6, 7, 8, 9, 12, 4, 1, 3, 2, 13, 11, 14, 15 },
        { 2, 3 },
        { 2, 3 },
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
        { 0, 1, 2, 3, 4, 5, 6, 7, 8 },
        { 0, 1, 2, 3, 4, 5 },
        { } };
    return Table;
}

uint64 GSellItemGuid = 0;
int32 GSelected[3] = { 0, 0, 0 };

int32 ListIndex(const FString& Type) { return Type.Equals(TEXT("bidder"), ESearchCase::IgnoreCase) ? 1 : Type.Equals(TEXT("owner"), ESearchCase::IgnoreCase) ? 2 : 0; }

UWowConnectionManager* Connection(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    return Ctx ? Ctx->ConnectionManager : nullptr;
}

const TArray<FWowAuctionOffer>* List(UWowConnectionManager* C, int32 Index)
{
    if (!C) return nullptr;
    auto& H = C->PacketHandler;
    return Index == 1 ? &H.AuctionBidOffers : Index == 2 ? &H.AuctionOwnOffers : &H.AuctionOffers;
}

const FWowAuctionOffer* Offer(lua_State* L, int32 TypeArg, int32 IndexArg)
{
    const FString Type = UTF8_TO_TCHAR(luaL_optstring(L, TypeArg, "list"));
    const int32 Index = static_cast<int32>(luaL_optnumber(L, IndexArg, 0)) - 1;
    const TArray<FWowAuctionOffer>* Offers = List(Connection(L), ListIndex(Type));
    return Offers && Offers->IsValidIndex(Index) ? &(*Offers)[Index] : nullptr;
}

FString ItemLink(UWowConnectionManager* C, uint32 Entry)
{
    const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(C, Entry);
    static const TCHAR* Colors[] = { TEXT("ff9d9d9d"), TEXT("ffffffff"), TEXT("ff1eff00"), TEXT("ff0070dd"), TEXT("ffa335ee"), TEXT("ffff8000"), TEXT("ffe6cc80"), TEXT("ffe6cc80") };
    const uint32 Quality = Info ? FMath::Min<uint32>(Info->Quality, 7) : 1;
    return FString::Printf(TEXT("|c%s|Hitem:%u:0:0:0:0:0:0:0:0|h[%s]|h|r"), Colors[Quality], Entry,
        Info ? *Info->Name : *FString::Printf(TEXT("Item %u"), Entry));
}

int L_CanSendAuctionQuery(lua_State* L)
{
    UWowConnectionManager* C = Connection(L);
    lua_pushboolean(L, C && C->PacketHandler.AuctionGuid != 0);
    lua_pushboolean(L, false); // no getAll scans
    return 2;
}

int L_QueryAuctionItems(lua_State* L)
{
    // QueryAuctionItems(name, minLevel, maxLevel, invTypeIndex, classIndex, subclassIndex, page, isUsable, qualityIndex, getAll)
    UWowConnectionManager* C = Connection(L);
    if (!C) return 0;
    const FString Name = UTF8_TO_TCHAR(luaL_optstring(L, 1, ""));
    const int32 MinLevel = lua_isnumber(L, 2) ? static_cast<int32>(lua_tonumber(L, 2)) : 0;
    const int32 MaxLevel = lua_isnumber(L, 3) ? static_cast<int32>(lua_tonumber(L, 3)) : 0;
    const int32 ClassIndex = lua_isnumber(L, 5) ? static_cast<int32>(lua_tonumber(L, 5)) - 1 : -1;
    const int32 SubIndex = lua_isnumber(L, 6) ? static_cast<int32>(lua_tonumber(L, 6)) - 1 : -1;
    const int32 Page = lua_isnumber(L, 7) ? FMath::Max(0, static_cast<int32>(lua_tonumber(L, 7))) : 0;
    const bool bUsable = lua_toboolean(L, 8) != 0;
    const int32 Quality = lua_isnumber(L, 9) ? static_cast<int32>(lua_tonumber(L, 9)) : -1;
    const uint32 ClassId = ClassIndex >= 0 && ClassIndex < int32(UE_ARRAY_COUNT(GClassIds)) ? GClassIds[ClassIndex] : 0xFFFFFFFFu;
    const uint32 SubId = ClassIndex >= 0 && SubClassIds().IsValidIndex(ClassIndex) && SubClassIds()[ClassIndex].IsValidIndex(SubIndex)
        ? SubClassIds()[ClassIndex][SubIndex] : 0xFFFFFFFFu;
    C->SearchAuctionsFiltered(Name, uint32(Page) * 50, uint8(FMath::Clamp(MinLevel, 0, 255)), uint8(FMath::Clamp(MaxLevel, 0, 255)),
        0xFFFFFFFFu, ClassId, SubId, Quality < 0 ? 0xFFFFFFFFu : uint32(Quality), bUsable);
    return 0;
}

int L_GetNumAuctionItems(lua_State* L)
{
    const int32 Index = ListIndex(UTF8_TO_TCHAR(luaL_optstring(L, 1, "list")));
    UWowConnectionManager* C = Connection(L);
    const TArray<FWowAuctionOffer>* Offers = List(C, Index);
    const int32 Batch = Offers ? Offers->Num() : 0;
    int32 Total = Batch;
    if (C) Total = Index == 0 ? int32(C->PacketHandler.AuctionTotal) : Index == 1 ? int32(C->PacketHandler.AuctionBidTotal) : int32(C->PacketHandler.AuctionOwnTotal);
    lua_pushnumber(L, Batch);
    lua_pushnumber(L, FMath::Max(Batch, Total));
    return 2;
}

int L_GetAuctionItemInfo(lua_State* L)
{
    // name, texture, count, quality, canUse, level, minBid, minIncrement, buyoutPrice, bidAmount,
    // highBidder, owner, saleStatus, itemId
    UWowConnectionManager* C = Connection(L);
    const FWowAuctionOffer* O = Offer(L, 1, 2);
    if (!C || !O) { lua_pushnil(L); return 1; }
    const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(C, O->Item);
    const uint64 Me = C->PacketHandler.EntityManager.LocalPlayerGuid;
    const FString* Owner = C->PacketHandler.PlayerNameCache.Find(O->Owner);
    if (!Owner && O->Owner) C->SendNameQuery(static_cast<int64>(O->Owner));
    lua_pushstring(L, TCHAR_TO_UTF8(*(Info ? Info->Name : FString())));
    lua_pushstring(L, TCHAR_TO_UTF8(*FWowItemPresentation::IconPath(O->Item, Info ? Info->DisplayId : 0)));
    lua_pushnumber(L, O->Count);
    lua_pushnumber(L, Info ? Info->Quality : 1);
    lua_pushboolean(L, true);
    lua_pushnumber(L, Info ? Info->RequiredLevel : 0);
    lua_pushnumber(L, O->Minimum);
    lua_pushnumber(L, O->Increment);
    lua_pushnumber(L, O->Buyout);
    lua_pushnumber(L, O->Bid);
    lua_pushboolean(L, O->Bidder != 0 && O->Bidder == Me);
    if (Owner) lua_pushstring(L, TCHAR_TO_UTF8(**Owner)); else lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, O->Item);
    return 14;
}

int L_GetAuctionItemLink(lua_State* L)
{
    const FWowAuctionOffer* O = Offer(L, 1, 2);
    if (!O) { lua_pushnil(L); return 1; }
    lua_pushstring(L, TCHAR_TO_UTF8(*ItemLink(Connection(L), O->Item)));
    return 1;
}

int L_GetAuctionItemTimeLeft(lua_State* L)
{
    const FWowAuctionOffer* O = Offer(L, 1, 2);
    if (!O) { lua_pushnil(L); return 1; }
    const uint32 Minutes = O->Time / 60000;
    lua_pushnumber(L, Minutes < 30 ? 1 : Minutes < 120 ? 2 : Minutes < 720 ? 3 : 4);
    return 1;
}

int L_PlaceAuctionBid(lua_State* L)
{
    UWowConnectionManager* C = Connection(L);
    const FWowAuctionOffer* O = Offer(L, 1, 2);
    const uint32 Bid = static_cast<uint32>(luaL_optnumber(L, 3, 0));
    if (C && O && Bid) C->BidAuction(O->Id, Bid);
    return 0;
}

int L_CancelAuction(lua_State* L)
{
    UWowConnectionManager* C = Connection(L);
    const int32 Index = static_cast<int32>(luaL_optnumber(L, 1, 0)) - 1;
    if (C && C->PacketHandler.AuctionOwnOffers.IsValidIndex(Index)) C->CancelAuction(C->PacketHandler.AuctionOwnOffers[Index].Id);
    return 0;
}

int L_ClickAuctionSellItemButton(lua_State* L)
{
    // The item on the cursor goes into the sell slot; an empty cursor takes it back out.
    UWowConnectionManager* C = Connection(L);
    if (!C) return 0;
    if (C->HasCursorItemPayload()) { GSellItemGuid = C->GetCursorItemGuid(); C->ClearCursorPayload(); }
    else GSellItemGuid = 0;
    if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->EventSystem) Ctx->EventSystem->FireEvent(TEXT("NEW_AUCTION_UPDATE"));
    return 0;
}

int L_GetAuctionSellItemInfo(lua_State* L)
{
    // name, texture, count, quality, canUse, price
    UWowConnectionManager* C = Connection(L);
    const FWowItemEntity* Item = C && GSellItemGuid ? C->PacketHandler.EntityManager.FindItem(GSellItemGuid) : nullptr;
    if (!Item) { GSellItemGuid = 0; lua_pushnil(L); return 1; }
    const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(C, Item->Entry);
    lua_pushstring(L, TCHAR_TO_UTF8(*(Info ? Info->Name : FString())));
    lua_pushstring(L, TCHAR_TO_UTF8(*FWowItemPresentation::IconPath(Item->Entry, Info ? Info->DisplayId : 0)));
    lua_pushnumber(L, FMath::Max(1, Item->GetStackCount()));
    lua_pushnumber(L, Info ? Info->Quality : 1);
    lua_pushboolean(L, true);
    lua_pushnumber(L, Info ? Info->SellPrice : 0);
    return 6;
}

uint32 RunTimeMinutes(lua_State* L, int32 Arg)
{
    const int32 Value = static_cast<int32>(luaL_optnumber(L, Arg, 1));
    return Value <= 3 ? uint32(720 * (Value <= 1 ? 1 : Value == 2 ? 2 : 4)) : uint32(Value);
}

int L_CalculateAuctionDeposit(lua_State* L)
{
    UWowConnectionManager* C = Connection(L);
    const FWowItemEntity* Item = C && GSellItemGuid ? C->PacketHandler.EntityManager.FindItem(GSellItemGuid) : nullptr;
    const FWowItemTemplateInfo* Info = Item ? FWowItemPresentation::Info(C, Item->Entry) : nullptr;
    const uint32 Minutes = RunTimeMinutes(L, 1);
    lua_pushnumber(L, Info ? FMath::Max(1.0, double(Info->SellPrice) * FMath::Max(1, Item->GetStackCount()) * .15 * (Minutes / 720)) : 0.0);
    return 1;
}

int L_StartAuction(lua_State* L)
{
    UWowConnectionManager* C = Connection(L);
    const uint32 MinBid = static_cast<uint32>(luaL_optnumber(L, 1, 0));
    const uint32 Buyout = static_cast<uint32>(luaL_optnumber(L, 2, 0));
    if (C && GSellItemGuid && C->SellAuctionItem(GSellItemGuid, MinBid, Buyout, RunTimeMinutes(L, 3)))
    {
        GSellItemGuid = 0;
        if (FWowLuaContext* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->EventSystem) Ctx->EventSystem->FireEvent(TEXT("NEW_AUCTION_UPDATE"));
    }
    return 0;
}

int L_GetSelectedAuctionItem(lua_State* L)
{
    lua_pushnumber(L, GSelected[ListIndex(UTF8_TO_TCHAR(luaL_optstring(L, 1, "list")))]);
    return 1;
}

int L_SetSelectedAuctionItem(lua_State* L)
{
    GSelected[ListIndex(UTF8_TO_TCHAR(luaL_optstring(L, 1, "list")))] = static_cast<int32>(luaL_optnumber(L, 2, 0));
    return 0;
}

int L_NoOp(lua_State*) { return 0; }
int L_DepositRate(lua_State* L) { lua_pushnumber(L, 15); return 1; }
}
#endif

void WowLuaApi::RegisterAuctionApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    lua_register(L, "CanSendAuctionQuery", L_CanSendAuctionQuery);
    lua_register(L, "QueryAuctionItems", L_QueryAuctionItems);
    lua_register(L, "GetNumAuctionItems", L_GetNumAuctionItems);
    lua_register(L, "GetAuctionItemInfo", L_GetAuctionItemInfo);
    lua_register(L, "GetAuctionItemLink", L_GetAuctionItemLink);
    lua_register(L, "GetAuctionItemTimeLeft", L_GetAuctionItemTimeLeft);
    lua_register(L, "PlaceAuctionBid", L_PlaceAuctionBid);
    lua_register(L, "CancelAuction", L_CancelAuction);
    lua_register(L, "ClickAuctionSellItemButton", L_ClickAuctionSellItemButton);
    lua_register(L, "GetAuctionSellItemInfo", L_GetAuctionSellItemInfo);
    lua_register(L, "CalculateAuctionDeposit", L_CalculateAuctionDeposit);
    lua_register(L, "StartAuction", L_StartAuction);
    lua_register(L, "GetSelectedAuctionItem", L_GetSelectedAuctionItem);
    lua_register(L, "SetSelectedAuctionItem", L_SetSelectedAuctionItem);
    lua_register(L, "SortAuctionItems", L_NoOp);
    lua_register(L, "SortAuctionClearSort", L_NoOp);
    lua_register(L, "SortAuctionApplySort", L_NoOp);
    lua_register(L, "SortAuctionSetSort", L_NoOp);
    lua_register(L, "CloseAuctionHouse", L_NoOp);
    lua_register(L, "GetAuctionHouseDepositRate", L_DepositRate);
#endif
}
