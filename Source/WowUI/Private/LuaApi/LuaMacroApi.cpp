// Macro API (3.3.5): GetNumMacros, GetMacroInfo, GetMacroIndexByName, GetMacroBody, CreateMacro,
// EditMacro, DeleteMacro, GetNumMacroIcons, GetMacroIconInfo, GetNumMacroItemIcons,
// GetMacroItemIconInfo, PickupMacro, RunMacro, RunMacroText. Storage and numbering: WowMacros.h.
// Macro lines run exactly like typed chat lines (the gameplay controller's chat input, reached
// through the binding handler with the "CHATLINE:" prefix), so /cast, /use, /say, emotes and
// addon commands behave the same in a macro as in the chat box.
#include "LuaApiRegistry.h"
#include "WowMacros.h"
#include "WowConnectionManager.h"

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
    FString LuaString(lua_State* L, int Index)
    {
        const char* Text = lua_tostring(L, Index);
        return Text ? FString(UTF8_TO_TCHAR(Text)) : FString();
    }

    void PushString(lua_State* L, const FString& Text)
    {
        lua_pushstring(L, TCHAR_TO_UTF8(*Text));
    }

    /** A macro argument is its index or its name. */
    int32 MacroIndexArg(lua_State* L, int Index)
    {
        if (lua_type(L, Index) == LUA_TNUMBER) return int32(lua_tointeger(L, Index));
        if (lua_type(L, Index) == LUA_TSTRING) return FWowMacros::FindByName(LuaString(L, Index));
        return 0;
    }

    /** CreateMacro / EditMacro take the icon as its index in GetMacroIconInfo or as a texture path. */
    FString IconArg(lua_State* L, int Index)
    {
        if (lua_type(L, Index) == LUA_TNUMBER)
        {
            const TArray<FString>& Icons = FWowMacros::Icons();
            const int32 Icon = int32(lua_tointeger(L, Index));
            return Icons.IsValidIndex(Icon - 1) ? Icons[Icon - 1] : FString();
        }
        FString Path = LuaString(L, Index);
        if (!Path.IsEmpty() && !Path.Contains(TEXT("\\"))) Path = TEXT("Interface\\Icons\\") + Path;
        return Path;
    }

    UWowConnectionManager* Connection(lua_State* L)
    {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        return Ctx ? Ctx->ConnectionManager : nullptr;
    }

    int L_GetNumMacros(lua_State* L)
    {
        lua_pushinteger(L, FWowMacros::NumAccount());
        lua_pushinteger(L, FWowMacros::NumCharacter());
        return 2;
    }

    int L_GetMacroInfo(lua_State* L)
    {
        const FWowMacro* Macro = FWowMacros::Get(MacroIndexArg(L, 1));
        if (!Macro) return 0;
        PushString(L, Macro->Name);
        PushString(L, FWowMacros::DisplayIcon(*Macro, Connection(L)));
        PushString(L, Macro->Body);
        lua_pushnil(L); // isLocal
        return 4;
    }

    int L_GetMacroIndexByName(lua_State* L)
    {
        lua_pushinteger(L, FWowMacros::FindByName(LuaString(L, 1)));
        return 1;
    }

    int L_GetMacroBody(lua_State* L)
    {
        const FWowMacro* Macro = FWowMacros::Get(MacroIndexArg(L, 1));
        if (!Macro) return 0;
        PushString(L, Macro->Body);
        return 1;
    }

    int L_CreateMacro(lua_State* L)
    {
        const int32 Index = FWowMacros::Create(LuaString(L, 1), IconArg(L, 2), LuaString(L, 3), lua_toboolean(L, 4) != 0);
        if (!Index) return luaL_error(L, "Macro limit reached");
        lua_pushinteger(L, Index);
        return 1;
    }

    int L_EditMacro(lua_State* L)
    {
        const int32 Index = MacroIndexArg(L, 1);
        const FWowMacro* Macro = FWowMacros::Get(Index);
        if (!Macro) return 0;
        const FString Name = lua_isnoneornil(L, 2) ? Macro->Name : LuaString(L, 2);
        const FString Icon = lua_isnoneornil(L, 3) ? Macro->Icon : IconArg(L, 3);
        const FString Body = lua_isnoneornil(L, 4) ? Macro->Body : LuaString(L, 4);
        lua_pushinteger(L, FWowMacros::Edit(Index, Name, Icon, Body));
        return 1;
    }

    int L_DeleteMacro(lua_State* L)
    {
        FWowMacros::Delete(MacroIndexArg(L, 1), Connection(L));
        return 0;
    }

    int L_GetNumMacroIcons(lua_State* L)
    {
        lua_pushinteger(L, FWowMacros::Icons().Num());
        return 1;
    }

    int L_GetMacroIconInfo(lua_State* L)
    {
        const TArray<FString>& Icons = FWowMacros::Icons();
        const int32 Index = int32(luaL_optinteger(L, 1, 0)) - 1;
        if (!Icons.IsValidIndex(Index)) return 0;
        PushString(L, Icons[Index]);
        return 1;
    }

    int L_GetNumMacroItemIcons(lua_State* L)
    {
        lua_pushinteger(L, 0);
        return 1;
    }

    int L_GetMacroItemIconInfo(lua_State* L)
    {
        (void)L;
        return 0;
    }

    int L_PickupMacro(lua_State* L)
    {
        const int32 Index = MacroIndexArg(L, 1);
        if (UWowConnectionManager* Conn = Connection(L); Conn && FWowMacros::Get(Index))
            Conn->PickupMacroCursor(Index);
        return 0;
    }

    int L_CursorHasMacro(lua_State* L)
    {
        FString Type, Detail; int32 Id = 0;
        UWowConnectionManager* Conn = Connection(L);
        lua_pushboolean(L, Conn && Conn->GetCursorInfo(Type, Id, Detail) && Type == TEXT("macro"));
        return 1;
    }

    /** One macro line through the chat input, as if typed (see the file comment). */
    int L_SubmitChatLine(lua_State* L)
    {
        FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
        const FString Line = LuaString(L, 1).TrimStartAndEnd();
        if (Ctx && Ctx->RunBinding && !Line.IsEmpty()) Ctx->RunBinding(TEXT("CHATLINE:") + Line, true);
        return 0;
    }
}
#endif

void WowLuaApi::RegisterMacroApi(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    lua_register(L, "GetNumMacros", L_GetNumMacros);
    lua_register(L, "GetMacroInfo", L_GetMacroInfo);
    lua_register(L, "GetMacroIndexByName", L_GetMacroIndexByName);
    lua_register(L, "GetMacroBody", L_GetMacroBody);
    lua_register(L, "CreateMacro", L_CreateMacro);
    lua_register(L, "EditMacro", L_EditMacro);
    lua_register(L, "DeleteMacro", L_DeleteMacro);
    lua_register(L, "GetNumMacroIcons", L_GetNumMacroIcons);
    lua_register(L, "GetMacroIconInfo", L_GetMacroIconInfo);
    lua_register(L, "GetNumMacroItemIcons", L_GetNumMacroItemIcons);
    lua_register(L, "GetMacroItemIconInfo", L_GetMacroItemIconInfo);
    lua_register(L, "PickupMacro", L_PickupMacro);
    lua_register(L, "CursorHasMacro", L_CursorHasMacro);
    lua_register(L, "__WowSubmitChatLine", L_SubmitChatLine);
    luaL_dostring(L, R"LUA(
MAX_ACCOUNT_MACROS = MAX_ACCOUNT_MACROS or 36
MAX_CHARACTER_MACROS = MAX_CHARACTER_MACROS or 18
-- Every line runs like a typed chat line; "#" lines (#showtooltip ...) are notes for the icon.
function RunMacroText(text)
    for line in string.gmatch(tostring(text or ""), "[^\r\n]+") do
        line = string.gsub(line, "^%s+", "")
        if line ~= "" and string.sub(line, 1, 1) ~= "#" then
            __WowSubmitChatLine(line)
        end
    end
end
function RunMacro(macro)
    local body = GetMacroBody(macro)
    if body then RunMacroText(body) end
end
)LUA");
#endif
}
