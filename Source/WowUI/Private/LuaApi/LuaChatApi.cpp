// Chat / social Lua API used by the stock 3.3.5 slash commands in ChatFrame.lua
// (/roll, /invite, /who, /join, /leave, /friend, /ignore, /ginvite, /gquit, /pvp,
// /played, /run ...). Each function is registered only when nothing else
// (LuaStubs/LuaFrameApi) already provides it, so existing behaviour is kept.
#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"

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
// 3.3.5a client opcodes.
constexpr uint32 CMSG_WHO = 0x062;
constexpr uint32 CMSG_ADD_FRIEND = 0x069;
constexpr uint32 CMSG_ADD_IGNORE = 0x06C;
constexpr uint32 CMSG_GUILD_INVITE = 0x082;
constexpr uint32 CMSG_GUILD_PROMOTE = 0x08B;
constexpr uint32 CMSG_GUILD_DEMOTE = 0x08C;
constexpr uint32 CMSG_GUILD_LEAVE = 0x08D;
constexpr uint32 CMSG_GUILD_REMOVE = 0x08E;
constexpr uint32 CMSG_GUILD_MOTD = 0x091;
constexpr uint32 CMSG_JOIN_CHANNEL = 0x097;
constexpr uint32 CMSG_LEAVE_CHANNEL = 0x098;
constexpr uint32 CMSG_PLAYED_TIME = 0x1CC;
constexpr uint32 MSG_RANDOM_ROLL = 0x1FB;
constexpr uint32 CMSG_TOGGLE_PVP = 0x253;

UWowConnectionManager* Connection(lua_State* L)
{
    FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
    return Ctx ? Ctx->ConnectionManager : nullptr;
}

void U32(TArray<uint8>& Data, uint32 Value) { Data.Append(reinterpret_cast<const uint8*>(&Value), 4); }
void Str(TArray<uint8>& Data, const FString& Value)
{
    FTCHARToUTF8 Utf8(*Value);
    Data.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    Data.Add(0);
}
FString Arg(lua_State* L, int Index)
{
    const char* Text = lua_isstring(L, Index) ? lua_tostring(L, Index) : nullptr;
    return Text ? FString(UTF8_TO_TCHAR(Text)).TrimStartAndEnd() : FString();
}
void Send(lua_State* L, uint32 Opcode, const TArray<uint8>& Data)
{
    if (UWowConnectionManager* C = Connection(L)) C->SendRawPacket(Opcode, Data);
}
int SendName(lua_State* L, uint32 Opcode)
{
    const FString Name = Arg(L, 1);
    if (Name.IsEmpty()) return 0;
    TArray<uint8> Data; Str(Data, Name);
    Send(L, Opcode, Data);
    return 0;
}

int L_RandomRoll(lua_State* L)
{
    // RandomRoll(min, max) - also accepts strings like the original ("/roll 1 50").
    const int32 Min = lua_isnoneornil(L, 1) ? 1 : FMath::Max(0, FCString::Atoi(*Arg(L, 1)));
    const int32 Max = lua_isnoneornil(L, 2) ? 100 : FMath::Max(Min, FCString::Atoi(*Arg(L, 2)));
    TArray<uint8> Data; U32(Data, uint32(Min)); U32(Data, uint32(FMath::Min(Max, 10000)));
    Send(L, MSG_RANDOM_ROLL, Data);
    return 0;
}

int L_InviteUnit(lua_State* L)
{
    if (UWowConnectionManager* C = Connection(L)) C->SendGroupInviteByName(Arg(L, 1));
    return 0;
}

int L_SendWho(lua_State* L)
{
    // Free text filter: matched against player names by the server. Level ranges
    // like "10-20" are passed as a level filter, everything else as a name.
    const FString Filter = Arg(L, 1);
    uint32 LevelMin = 0, LevelMax = 100;
    FString Name;
    TArray<FString> Words;
    Filter.ParseIntoArrayWS(Words);
    for (const FString& Word : Words)
    {
        FString A, B;
        if (Word.Split(TEXT("-"), &A, &B) && A.IsNumeric() && B.IsNumeric())
        { LevelMin = FCString::Atoi(*A); LevelMax = FCString::Atoi(*B); }
        else if (Word.IsNumeric()) { LevelMin = LevelMax = FCString::Atoi(*Word); }
        else if (Name.IsEmpty()) { Name = Word.Replace(TEXT("n-"), TEXT("")).Replace(TEXT("\""), TEXT("")); }
    }
    TArray<uint8> Data;
    U32(Data, LevelMin); U32(Data, LevelMax);
    Str(Data, Name); Str(Data, FString());
    U32(Data, 0xFFFFFFFFu); U32(Data, 0xFFFFFFFFu); // all races / classes
    U32(Data, 0);                                    // zones
    U32(Data, 0);                                    // extra strings
    Send(L, CMSG_WHO, Data);
    return 0;
}

int L_JoinChannelByName(lua_State* L)
{
    const FString Name = Arg(L, 1);
    if (Name.IsEmpty()) return 0;
    TArray<uint8> Data;
    U32(Data, 0); Data.Add(0); Data.Add(0);
    Str(Data, Name); Str(Data, Arg(L, 2));
    Send(L, CMSG_JOIN_CHANNEL, Data);
    return 0;
}

int L_LeaveChannelByName(lua_State* L)
{
    FString Name = Arg(L, 1);
    if (Name.IsNumeric())
        if (UWowConnectionManager* C = Connection(L)) Name = C->PacketHandler.GetChannelNameByNumber(FCString::Atoi(*Name));
    if (Name.IsEmpty()) return 0;
    TArray<uint8> Data; U32(Data, 0); Str(Data, Name);
    Send(L, CMSG_LEAVE_CHANNEL, Data);
    return 0;
}

int L_AddFriend(lua_State* L)
{
    const FString Name = Arg(L, 1);
    if (Name.IsEmpty()) return 0;
    TArray<uint8> Data; Str(Data, Name); Str(Data, Arg(L, 2));
    Send(L, CMSG_ADD_FRIEND, Data);
    return 0;
}

int L_AddIgnore(lua_State* L) { return SendName(L, CMSG_ADD_IGNORE); }
int L_GuildInvite(lua_State* L) { return SendName(L, CMSG_GUILD_INVITE); }
int L_GuildUninvite(lua_State* L) { return SendName(L, CMSG_GUILD_REMOVE); }
int L_GuildPromote(lua_State* L) { return SendName(L, CMSG_GUILD_PROMOTE); }
int L_GuildDemote(lua_State* L) { return SendName(L, CMSG_GUILD_DEMOTE); }
int L_GuildLeave(lua_State* L) { Send(L, CMSG_GUILD_LEAVE, {}); return 0; }
int L_GuildSetMOTD(lua_State* L)
{
    TArray<uint8> Data; Str(Data, Arg(L, 1).Left(127));
    Send(L, CMSG_GUILD_MOTD, Data);
    return 0;
}
int L_TogglePVP(lua_State* L) { Send(L, CMSG_TOGGLE_PVP, {}); return 0; }
int L_RequestTimePlayed(lua_State* L)
{
    TArray<uint8> Data; Data.Add(1); // print the result in chat
    Send(L, CMSG_PLAYED_TIME, Data);
    return 0;
}
int L_SetChatWindowDocked(lua_State*) { return 0; } // dock layout is native (FloatingChatFrame OnLoad needs it)

void RegisterIfMissing(lua_State* L, const char* Name, lua_CFunction Function)
{
    lua_getglobal(L, Name);
    const bool bMissing = lua_isnil(L, -1);
    lua_pop(L, 1);
    if (bMissing) lua_register(L, Name, Function);
}
}
#endif

void WowLuaApi::RegisterChatApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    RegisterIfMissing(L, "RandomRoll", L_RandomRoll);
    RegisterIfMissing(L, "InviteUnit", L_InviteUnit);
    RegisterIfMissing(L, "InviteByName", L_InviteUnit);
    RegisterIfMissing(L, "SendWho", L_SendWho);
    RegisterIfMissing(L, "JoinChannelByName", L_JoinChannelByName);
    RegisterIfMissing(L, "JoinPermanentChannel", L_JoinChannelByName);
    RegisterIfMissing(L, "JoinTemporaryChannel", L_JoinChannelByName);
    RegisterIfMissing(L, "LeaveChannelByName", L_LeaveChannelByName);
    RegisterIfMissing(L, "AddFriend", L_AddFriend);
    RegisterIfMissing(L, "AddIgnore", L_AddIgnore);
    RegisterIfMissing(L, "GuildInvite", L_GuildInvite);
    RegisterIfMissing(L, "GuildUninvite", L_GuildUninvite);
    RegisterIfMissing(L, "GuildPromote", L_GuildPromote);
    RegisterIfMissing(L, "GuildDemote", L_GuildDemote);
    RegisterIfMissing(L, "GuildLeave", L_GuildLeave);
    RegisterIfMissing(L, "GuildSetMOTD", L_GuildSetMOTD);
    RegisterIfMissing(L, "TogglePVP", L_TogglePVP);
    RegisterIfMissing(L, "RequestTimePlayed", L_RequestTimePlayed);
    RegisterIfMissing(L, "SetChatWindowDocked", L_SetChatWindowDocked);

    // RunScript and the slash-command dispatcher are plain Lua.
    luaL_dostring(L, R"LUA(
if not RunScript then
    function RunScript(code)
        local f, err = loadstring(code or "")
        if not f then error(err, 0) end
        return f()
    end
end

-- Runs "/command args" the way ChatEdit_ParseText does: addon and FrameXML
-- SlashCmdList entries (SLASH_<KEY><n> aliases), then emotes (EMOTE<i>_CMD<j>).
function __WowUnrealSlashCommand(command, msg)
    local lower = string.lower
    local cmd = "/" .. lower(command or "")
    msg = msg or ""
    local editBox = ChatFrame1EditBox
    local function say(text, r, g, b)
        local frame = DEFAULT_CHAT_FRAME or ChatFrame1
        if frame and frame.AddMessage then frame:AddMessage(text, r or 1, g or 1, b or 0) end
    end
    -- ChatFrame.lua's protected commands (/cast, /use, /target, /focus, /stopcasting, /startattack,
    -- /castsequence, /equip ...) live in SecureCmdList; macros use them most (Stefan 11.10).
    -- r311 (Stefan 13.10: a /cast Smite macro said "Type /help ..."): ChatFrame.lua keeps
    -- SecureCmdList local; reach it through IsSecureCmd's upvalue, as the chat box's parser can.
    local secure = rawget(_G, "SecureCmdList")
    if type(secure) ~= "table" and IsSecureCmd and debug and debug.getupvalue then
        for i = 1, 30 do
            local name, value = debug.getupvalue(IsSecureCmd, i)
            if not name then break end
            if name == "SecureCmdList" and type(value) == "table" then secure = value break end
        end
    end
    for _, list in ipairs({type(secure) == "table" and secure or false, SlashCmdList or false}) do
    if type(list) == "table" then
        for key, func in pairs(list) do
            local i = 1
            local alias = _G["SLASH_" .. key .. i]
            while alias do
                if lower(alias) == cmd then
                    local ok, err = pcall(func, msg, editBox)
                    if not ok then say(tostring(err), 1, 0.2, 0.2) end
                    return true
                end
                i = i + 1
                alias = _G["SLASH_" .. key .. i]
            end
        end
    end
    end
    if DoEmote then
        for i = 1, (MAXEMOTEINDEX or 600) do
            local token = _G["EMOTE" .. i .. "_TOKEN"]
            if token then
                local j = 1
                local alias = _G["EMOTE" .. i .. "_CMD" .. j]
                while alias do
                    if lower(alias) == cmd then
                        DoEmote(token, msg)
                        return true
                    end
                    j = j + 1
                    alias = _G["EMOTE" .. i .. "_CMD" .. j]
                end
            end
        end
    end
    say(HELP_TEXT_SIMPLE or 'Type "/help" for a listing of a few commands.')
    return false
end
)LUA");
#endif
}
