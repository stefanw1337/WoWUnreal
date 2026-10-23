// Game-action Lua API the stock 3.3.5 interface (StaticPopup, UnitPopup, ChatFrame, FriendsFrame,
// MerchantFrame, VehicleMenuBar) and addons call, which the client did not provide yet (list:
// Saved\Reference\MissingLuaApi.txt). Our own implementation: each function sends the 3.3.5
// packet the original sends, or runs the key binding that already does the same thing.
// Every function is registered only when nothing else provides it.
#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowSavedVariables.h"
#include "WowClientPreferences.h"
#include "WowEntityManager.h"
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
// 3.3.5a (12340) client opcodes.
constexpr uint32 CMSG_ADD_FRIEND = 0x069;
constexpr uint32 CMSG_DEL_FRIEND = 0x06A;
constexpr uint32 CMSG_SET_CONTACT_NOTES = 0x06B;
constexpr uint32 CMSG_ADD_IGNORE = 0x06C;
constexpr uint32 CMSG_DEL_IGNORE = 0x06D;
constexpr uint32 CMSG_GUILD_ACCEPT = 0x084;
constexpr uint32 CMSG_GUILD_DECLINE = 0x085;
constexpr uint32 CMSG_GUILD_INFO = 0x087;
constexpr uint32 CMSG_GUILD_DISBAND = 0x08F;
constexpr uint32 CMSG_GUILD_LEADER = 0x090;
constexpr uint32 CMSG_GUILD_SET_PUBLIC_NOTE = 0x234;
constexpr uint32 CMSG_GUILD_SET_OFFICER_NOTE = 0x235;
constexpr uint32 CMSG_TOGGLE_PVP = 0x253;
constexpr uint32 CMSG_REQUEST_VEHICLE_EXIT = 0x476;
constexpr uint32 CMSG_SHOWING_HELM = 0x2B9;
constexpr uint32 CMSG_SHOWING_CLOAK = 0x2BA;

constexpr uint32 ContactFriend = 0x1, ContactIgnored = 0x2;

UWowConnectionManager* ActionConnection(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    return Ctx ? Ctx->ConnectionManager : nullptr;
}

void PutU64(TArray<uint8>& Data, uint64 Value) { Data.Append(reinterpret_cast<const uint8*>(&Value), 8); }
void PutString(TArray<uint8>& Data, const FString& Value)
{
    FTCHARToUTF8 Utf8(*Value);
    Data.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    Data.Add(0);
}
FString ArgText(lua_State* L, int Index)
{
    const char* Text = lua_isstring(L, Index) ? lua_tostring(L, Index) : nullptr;
    return Text ? FString(UTF8_TO_TCHAR(Text)).TrimStartAndEnd() : FString();
}
void SendAction(lua_State* L, uint32 Opcode, const TArray<uint8>& Data = {})
{
    if (UWowConnectionManager* Conn = ActionConnection(L)) Conn->SendRawPacket(Opcode, Data);
}

FString ContactName(const UWowConnectionManager* Conn, const FWowFriendInfo& Contact)
{
    if (!Contact.Name.IsEmpty()) return Contact.Name;
    const FString* Cached = Conn->PacketHandler.PlayerNameCache.Find(Contact.Guid);
    return Cached ? *Cached : FString();
}

/** The friends/ignore list entry an argument names: its 1-based index in that list, or a name. */
const FWowFriendInfo* FindContact(lua_State* L, int Index, uint32 Kind)
{
    UWowConnectionManager* Conn = ActionConnection(L);
    if (!Conn) return nullptr;
    const bool bByIndex = lua_type(L, Index) == LUA_TNUMBER;
    const int32 Wanted = bByIndex ? int32(lua_tointeger(L, Index)) : 0;
    const FString Name = bByIndex ? FString() : ArgText(L, Index);
    int32 Position = 0;
    for (const FWowFriendInfo& Contact : Conn->PacketHandler.FriendsList)
    {
        if (!(Contact.Flags & Kind)) continue;
        ++Position;
        if (bByIndex ? Position == Wanted : ContactName(Conn, Contact).Equals(Name, ESearchCase::IgnoreCase)) return &Contact;
    }
    return nullptr;
}

void SendGuid(lua_State* L, uint32 Opcode, uint64 Guid)
{
    TArray<uint8> Data; PutU64(Data, Guid);
    SendAction(L, Opcode, Data);
}

int L_RemoveFriend(lua_State* L)
{
    if (const FWowFriendInfo* Contact = FindContact(L, 1, ContactFriend)) SendGuid(L, CMSG_DEL_FRIEND, Contact->Guid);
    return 0;
}

int L_SetFriendNotes(lua_State* L)
{
    if (const FWowFriendInfo* Contact = FindContact(L, 1, ContactFriend))
    {
        TArray<uint8> Data; PutU64(Data, Contact->Guid); PutString(Data, ArgText(L, 2).Left(48));
        SendAction(L, CMSG_SET_CONTACT_NOTES, Data);
    }
    return 0;
}

int L_AddOrRemoveFriend(lua_State* L)
{
    if (const FWowFriendInfo* Contact = FindContact(L, 1, ContactFriend)) { SendGuid(L, CMSG_DEL_FRIEND, Contact->Guid); return 0; }
    const FString Name = ArgText(L, 1);
    if (Name.IsEmpty()) return 0;
    TArray<uint8> Data; PutString(Data, Name); PutString(Data, ArgText(L, 2).Left(48));
    SendAction(L, CMSG_ADD_FRIEND, Data);
    return 0;
}

int L_DelIgnore(lua_State* L)
{
    if (const FWowFriendInfo* Contact = FindContact(L, 1, ContactIgnored)) SendGuid(L, CMSG_DEL_IGNORE, Contact->Guid);
    return 0;
}

int L_AddOrDelIgnore(lua_State* L)
{
    if (const FWowFriendInfo* Contact = FindContact(L, 1, ContactIgnored)) { SendGuid(L, CMSG_DEL_IGNORE, Contact->Guid); return 0; }
    const FString Name = ArgText(L, 1);
    if (Name.IsEmpty()) return 0;
    TArray<uint8> Data; PutString(Data, Name);
    SendAction(L, CMSG_ADD_IGNORE, Data);
    return 0;
}

int L_AcceptGuild(lua_State* L) { SendAction(L, CMSG_GUILD_ACCEPT); return 0; }
int L_DeclineGuild(lua_State* L) { SendAction(L, CMSG_GUILD_DECLINE); return 0; }
int L_GuildInfo(lua_State* L) { SendAction(L, CMSG_GUILD_INFO); return 0; }
int L_GuildDisband(lua_State* L) { SendAction(L, CMSG_GUILD_DISBAND); return 0; }

int L_GuildSetLeader(lua_State* L)
{
    const FString Name = ArgText(L, 1);
    if (Name.IsEmpty()) return 0;
    TArray<uint8> Data; PutString(Data, Name);
    SendAction(L, CMSG_GUILD_LEADER, Data);
    return 0;
}

/** GuildRosterSetPublicNote / GuildRosterSetOfficerNote(index, note): the member's name, then the note. */
int SetGuildNote(lua_State* L, uint32 Opcode)
{
    UWowConnectionManager* Conn = ActionConnection(L);
    const int32 Index = int32(luaL_optinteger(L, 1, 0)) - 1;
    if (!Conn || !Conn->PacketHandler.GuildRoster.IsValidIndex(Index)) return 0;
    const FWowGuildMember& Member = Conn->PacketHandler.GuildRoster[Index];
    TArray<uint8> Data; PutString(Data, Member.Name); PutString(Data, ArgText(L, 2).Left(31));
    SendAction(L, Opcode, Data);
    return 0;
}
int L_GuildRosterSetPublicNote(lua_State* L) { return SetGuildNote(L, CMSG_GUILD_SET_PUBLIC_NOTE); }
int L_GuildRosterSetOfficerNote(lua_State* L) { return SetGuildNote(L, CMSG_GUILD_SET_OFFICER_NOTE); }

/** SetPVP(flag): the PvP flag on (1) or off (nil/0) - CMSG_TOGGLE_PVP with the wanted state. */
int L_SetPVP(lua_State* L)
{
    TArray<uint8> Data; Data.Add(lua_toboolean(L, 1) && !(lua_type(L, 1) == LUA_TNUMBER && lua_tointeger(L, 1) == 0) ? 1 : 0);
    SendAction(L, CMSG_TOGGLE_PVP, Data);
    return 0;
}

int L_VehicleExit(lua_State* L) { SendAction(L, CMSG_REQUEST_VEHICLE_EXIT); return 0; }

/** r224: Show Helm / Show Cloak (Interface Options > Display). The state is the player's
    PLAYER_FLAGS_HIDE_HELM (0x400) / HIDE_CLOAK (0x800); the server flips the flag on each
    CMSG_SHOWING_HELM / CMSG_SHOWING_CLOAK, so a packet is sent only when the wanted state differs.
    A request still on its way counts as the state for 2 s, so two quick clicks end where they should. */
struct FGearRequest { bool bActive = false; bool bShown = true; double At = 0; };
FGearRequest GHelmRequest, GCloakRequest;

bool IsGearShown(lua_State* L, uint32 HideFlag, FGearRequest& Request)
{
    UWowConnectionManager* Conn = ActionConnection(L);
    const FWowPlayerEntity* Me = Conn ? Conn->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
    const bool bShown = !Me || (Me->GetPlayerFlags() & HideFlag) == 0;
    if (Request.bActive && bShown != Request.bShown && FPlatformTime::Seconds() - Request.At < 2.0) return Request.bShown;
    Request.bActive = false;
    return bShown;
}

bool ArgIsOn(lua_State* L, int Index)
{
    if (lua_type(L, Index) == LUA_TNUMBER) return lua_tonumber(L, Index) != 0;
    if (lua_type(L, Index) == LUA_TSTRING) { const FString Text = ArgText(L, Index); return !Text.IsEmpty() && Text != TEXT("0"); }
    return lua_toboolean(L, Index) != 0;
}

int SetGearShown(lua_State* L, uint32 HideFlag, uint32 Opcode, FGearRequest& Request)
{
    const bool bWanted = ArgIsOn(L, 1);
    if (bWanted == IsGearShown(L, HideFlag, Request)) return 0;
    TArray<uint8> Data; Data.Add(bWanted ? 1 : 0);
    SendAction(L, Opcode, Data);
    Request.bActive = true; Request.bShown = bWanted; Request.At = FPlatformTime::Seconds();
    return 0;
}

int L_ShowingHelm(lua_State* L) { lua_pushboolean(L, IsGearShown(L, 0x400, GHelmRequest)); return 1; }
int L_ShowingCloak(lua_State* L) { lua_pushboolean(L, IsGearShown(L, 0x800, GCloakRequest)); return 1; }
int L_ShowHelm(lua_State* L) { return SetGearShown(L, 0x400, CMSG_SHOWING_HELM, GHelmRequest); }
int L_ShowCloak(lua_State* L) { return SetGearShown(L, 0x800, CMSG_SHOWING_CLOAK, GCloakRequest); }

/** RepairAllItems(guildBankRepair): every item at the open vendor (item guid 0 = all). */
int L_RepairAllItems(lua_State* L)
{
    UWowConnectionManager* Conn = ActionConnection(L);
    if (Conn && Conn->PacketHandler.ActiveVendorGuid)
        Conn->SendRepairItem(Conn->PacketHandler.ActiveVendorGuid, 0, lua_toboolean(L, 1) != 0);
    return 0;
}

/** __WowWtfInfo() -> chosenMode (-1 not chosen, 0-3), hasClientWtf, realmName, clientWtfPath, activeMode.
    For the WowUnreal_UI prompt "use your WoW client's settings?". */
int L_WtfInfo(lua_State* L)
{
    UWowConnectionManager* Conn = ActionConnection(L);
    const FString Realm = Conn ? Conn->GetSelectedRealmName() : FString();
    lua_pushinteger(L, Realm.IsEmpty() ? 0 : FWowClientPreferences::GetClientWtfMode(Realm));
    lua_pushboolean(L, !FWowSavedVariables::GetClientWtfDirectory().IsEmpty() && FWowSavedVariables::HasIdentity());
    lua_pushstring(L, TCHAR_TO_UTF8(*Realm));
    lua_pushstring(L, TCHAR_TO_UTF8(*FWowSavedVariables::GetClientWtfDirectory()));
    lua_pushinteger(L, int32(FWowSavedVariables::GetClientWtfMode()));
    return 5;
}

/** __WowSetWtfMode(mode): remember the choice for this realm; it applies at the next /reload or login. */
int L_SetWtfMode(lua_State* L)
{
    UWowConnectionManager* Conn = ActionConnection(L);
    const FString Realm = Conn ? Conn->GetSelectedRealmName() : FString();
    if (!Realm.IsEmpty()) FWowClientPreferences::SetClientWtfMode(Realm, int32(luaL_checkinteger(L, 1)));
    return 0;
}

// r337: StaticPopup CONFIRM_BINDER ("Do you want to make %s your new home?"). Accept -> ConfirmBinder()
// sends CMSG_BINDER_ACTIVATE with the innkeeper from SMSG_BINDER_CONFIRM; the server then casts Bind
// (3286) and answers SMSG_PLAYERBOUND. CheckBinderDist() keeps the popup only while you stand at him.
int L_ConfirmBinder(lua_State* L)
{
    UWowConnectionManager* Conn = ActionConnection(L);
    if (!Conn || !Conn->PacketHandler.PendingBinderGuid) return 0;
    TArray<uint8> Data;
    PutU64(Data, Conn->PacketHandler.PendingBinderGuid);
    UE_LOG(LogTemp, Log, TEXT("ConfirmBinder: CMSG_BINDER_ACTIVATE for innkeeper %llu"), Conn->PacketHandler.PendingBinderGuid);
    Conn->SendRawPacket(FWowPacketHandler::CMSG_BINDER_ACTIVATE, Data);
    return 0;
}

int L_CheckBinderDist(lua_State* L)
{
    UWowConnectionManager* Conn = ActionConnection(L);
    bool bNear = false;
    if (Conn && Conn->PacketHandler.PendingBinderGuid)
    {
        const FWowEntity* Binder = Conn->PacketHandler.EntityManager.Find(Conn->PacketHandler.PendingBinderGuid);
        const FWowEntity* Me = Conn->PacketHandler.EntityManager.GetLocalPlayer();
        // The innkeeper's interaction reach (5 yards plus both bodies), with a little slack.
        bNear = Binder && Me && FVector::Dist(FVector(Binder->Movement.Position), FVector(Me->Movement.Position)) <= 10.0;
    }
    lua_pushboolean(L, bNear);
    return 1;
}

void RegisterActionIfMissing(lua_State* L, const char* Name, lua_CFunction Function)
{
    lua_getglobal(L, Name);
    const bool bExists = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!bExists) lua_register(L, Name, Function);
}
}
#endif

void WowLuaApi::RegisterGameActionsApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    RegisterActionIfMissing(L, "RemoveFriend", L_RemoveFriend);
    RegisterActionIfMissing(L, "SetFriendNotes", L_SetFriendNotes);
    RegisterActionIfMissing(L, "AddOrRemoveFriend", L_AddOrRemoveFriend);
    RegisterActionIfMissing(L, "DelIgnore", L_DelIgnore);
    RegisterActionIfMissing(L, "AddOrDelIgnore", L_AddOrDelIgnore);
    RegisterActionIfMissing(L, "AcceptGuild", L_AcceptGuild);
    RegisterActionIfMissing(L, "DeclineGuild", L_DeclineGuild);
    RegisterActionIfMissing(L, "GuildInfo", L_GuildInfo);
    RegisterActionIfMissing(L, "GuildDisband", L_GuildDisband);
    RegisterActionIfMissing(L, "GuildSetLeader", L_GuildSetLeader);
    RegisterActionIfMissing(L, "GuildRosterSetPublicNote", L_GuildRosterSetPublicNote);
    RegisterActionIfMissing(L, "GuildRosterSetOfficerNote", L_GuildRosterSetOfficerNote);
    RegisterActionIfMissing(L, "SetPVP", L_SetPVP);
    RegisterActionIfMissing(L, "VehicleExit", L_VehicleExit);
    RegisterActionIfMissing(L, "RepairAllItems", L_RepairAllItems);
    RegisterActionIfMissing(L, "ShowingHelm", L_ShowingHelm);
    RegisterActionIfMissing(L, "ShowingCloak", L_ShowingCloak);
    RegisterActionIfMissing(L, "ShowHelm", L_ShowHelm);
    RegisterActionIfMissing(L, "ShowCloak", L_ShowCloak);
    RegisterActionIfMissing(L, "ConfirmBinder", L_ConfirmBinder);
    RegisterActionIfMissing(L, "CheckBinderDist", L_CheckBinderDist);
    lua_register(L, "__WowWtfInfo", L_WtfInfo);
    lua_register(L, "__WowSetWtfMode", L_SetWtfMode);
    // Targeting and attack go through the key bindings that already do this (ExecuteSavedBinding).
    luaL_dostring(L, R"LUA(
local function Bind(name) if RunBinding then RunBinding(name) end end
if not TargetNearestEnemy then function TargetNearestEnemy(reverse) Bind(reverse and "TARGETPREVIOUSENEMY" or "TARGETNEARESTENEMY") end end
if not TargetNearestEnemyPlayer then function TargetNearestEnemyPlayer(reverse) Bind(reverse and "TARGETPREVIOUSENEMYPLAYER" or "TARGETNEARESTENEMYPLAYER") end end
if not TargetNearestFriend then function TargetNearestFriend(reverse) Bind(reverse and "TARGETPREVIOUSFRIEND" or "TARGETNEARESTFRIEND") end end
if not TargetNearestFriendPlayer then function TargetNearestFriendPlayer(reverse) Bind(reverse and "TARGETPREVIOUSFRIENDPLAYER" or "TARGETNEARESTFRIENDPLAYER") end end
if not TargetLastTarget then function TargetLastTarget() Bind("TARGETLASTTARGET") end end
if not TargetLastEnemy then function TargetLastEnemy() Bind("TARGETLASTHOSTILE") end end
if not StartAttack then function StartAttack() Bind("STARTATTACK") end end
if not AssistUnit then
    function AssistUnit(unit)
        if unit and unit ~= "" and unit ~= "target" and TargetUnit then TargetUnit(unit .. "target") else Bind("ASSISTTARGET") end
    end
end
)LUA");
#endif
}
