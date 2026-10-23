#include "LuaApiRegistry.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

DEFINE_LOG_CATEGORY_STATIC(LogWowLuaApi, Log, All);

// ─── print(...) — output to UE log ──────────────────────────────────────────────
static int L_print(lua_State* L)
{
    int n = lua_gettop(L);
    FString Out;
    for (int i = 1; i <= n; i++)
    {
        if (i > 1) Out += TEXT("\t");
        if (lua_isstring(L, i))
            Out += UTF8_TO_TCHAR(lua_tostring(L, i));
        else if (lua_isnil(L, i))
            Out += TEXT("nil");
        else if (lua_isboolean(L, i))
            Out += lua_toboolean(L, i) ? TEXT("true") : TEXT("false");
        else
            Out += FString::Printf(TEXT("%s: %p"), UTF8_TO_TCHAR(luaL_typename(L, i)), lua_topointer(L, i));
    }
    UE_LOG(LogWowLuaApi, Log, TEXT("[Lua] %s"), *Out);
    // The 3.3.5 client prints to the default chat frame (AIO, ElvUI and most addons
    // report errors and status this way).
    lua_getglobal(L, "DEFAULT_CHAT_FRAME");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "AddMessage");
        if (lua_isfunction(L, -1))
        {
            lua_insert(L, -2);
            FTCHARToUTF8 Text(*Out);
            lua_pushlstring(L, Text.Get(), Text.Length());
            if (lua_pcall(L, 2, 0, 0) != 0) lua_pop(L, 1);
            return 0;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return 0;
}

// ─── format(fmt, ...) — alias for string.format ────────────────────────────────
static int L_format(lua_State* L)
{
    lua_getglobal(L, "string");
    lua_getfield(L, -1, "format");
    lua_remove(L, -2);
    // Push all arguments
    int n = lua_gettop(L) - 1; // -1 for the function we just pushed
    for (int i = 1; i <= n; i++)
    {
        lua_pushvalue(L, i);
    }
    lua_call(L, n, 1);
    return 1;
}

// ─── strsplit(delimiter, str, pieces) ───────────────────────────────────────────
static int L_strsplit(lua_State* L)
{
    const char* delim = luaL_checkstring(L, 1);
    const char* str = luaL_checkstring(L, 2);
    int maxPieces = luaL_optinteger(L, 3, 0);

    FString Input = UTF8_TO_TCHAR(str);
    FString Delimiter = UTF8_TO_TCHAR(delim);

    TArray<FString> Parts;
    if (Delimiter.Len() == 1)
    {
        Input.ParseIntoArray(Parts, *Delimiter);
    }
    else
    {
        Parts.Add(Input);
    }

    if (maxPieces > 0 && Parts.Num() > maxPieces)
    {
        // Rejoin excess parts into last element
        FString Last;
        for (int32 i = maxPieces - 1; i < Parts.Num(); i++)
        {
            if (i > maxPieces - 1) Last += Delimiter;
            Last += Parts[i];
        }
        Parts.SetNum(maxPieces);
        Parts.Last() = Last;
    }

    for (const FString& Part : Parts)
    {
        FTCHARToUTF8 Conv(*Part);
        lua_pushstring(L, Conv.Get());
    }
    return Parts.Num();
}

// ─── strtrim(str) ───────────────────────────────────────────────────────────────
static int L_strtrim(lua_State* L)
{
    const char* str = luaL_checkstring(L, 1);
    FString S = UTF8_TO_TCHAR(str);
    S = S.TrimStartAndEnd();
    FTCHARToUTF8 Conv(*S);
    lua_pushstring(L, Conv.Get());
    return 1;
}

// ─── strjoin(delimiter, ...) — WoW-specific string join ──────────────────────
static int L_strjoin(lua_State* L)
{
    const char* delim = luaL_checkstring(L, 1);
    int n = lua_gettop(L);
    FString Result;
    for (int i = 2; i <= n; i++)
    {
        if (i > 2) Result += UTF8_TO_TCHAR(delim);
        if (lua_isstring(L, i))
            Result += UTF8_TO_TCHAR(lua_tostring(L, i));
        else if (lua_isnil(L, i))
            Result += TEXT("");
        else if (lua_isnumber(L, i))
            Result += FString::Printf(TEXT("%g"), lua_tonumber(L, i));
    }
    FTCHARToUTF8 Conv(*Result);
    lua_pushstring(L, Conv.Get());
    return 1;
}

// ─── tconcat — alias for table.concat ────────────────────────────────────────
static int L_tconcat(lua_State* L)
{
    lua_getglobal(L, "table");
    lua_getfield(L, -1, "concat");
    lua_remove(L, -2);
    int n = lua_gettop(L) - 1;
    for (int i = 1; i <= n; i++) lua_pushvalue(L, i);
    lua_call(L, n, 1);
    return 1;
}

// ─── getglobal / setglobal — WoW legacy functions ───────────────────────────
static int L_getglobal(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    lua_getglobal(L, name);
    return 1;
}

static int L_setglobal(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    lua_pushvalue(L, 2);
    lua_setglobal(L, name);
    return 0;
}

// ─── strbyte, strchar etc — aliases for string.* ────────────────────────────────
static int L_string_alias(lua_State* L, const char* method)
{
    lua_getglobal(L, "string");
    lua_getfield(L, -1, method);
    lua_remove(L, -2);
    int n = lua_gettop(L) - 1;
    for (int i = 1; i <= n; i++) lua_pushvalue(L, i);
    lua_call(L, n, LUA_MULTRET);
    return lua_gettop(L) - n;
}

#define DEF_STRING_ALIAS(name, method) \
    static int L_##name(lua_State* L) { return L_string_alias(L, #method); }

DEF_STRING_ALIAS(strbyte, byte)
DEF_STRING_ALIAS(strchar, char)
DEF_STRING_ALIAS(strfind, find)
DEF_STRING_ALIAS(strlen, len)
DEF_STRING_ALIAS(strlower, lower)
DEF_STRING_ALIAS(strupper, upper)
DEF_STRING_ALIAS(strsub, sub)
DEF_STRING_ALIAS(strrep, rep)
DEF_STRING_ALIAS(gsub, gsub)
DEF_STRING_ALIAS(gmatch, gmatch)
DEF_STRING_ALIAS(strmatch, match)
DEF_STRING_ALIAS(strrev, reverse)

// ─── Table functions ────────────────────────────────────────────────────────────

// wipe(table) — clear all keys from table, return it
static int L_wipe(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, 1) != 0)
    {
        lua_pop(L, 1); // pop value
        lua_pushvalue(L, -1); // copy key
        lua_pushnil(L);
        lua_rawset(L, 1); // table[key] = nil
    }
    lua_settop(L, 1);
    return 1;
}

// tinsert — alias for table.insert
static int L_tinsert(lua_State* L)
{
    lua_getglobal(L, "table");
    lua_getfield(L, -1, "insert");
    lua_remove(L, -2);
    int n = lua_gettop(L) - 1;
    for (int i = 1; i <= n; i++) lua_pushvalue(L, i);
    lua_call(L, n, 0);
    return 0;
}

// tremove — alias for table.remove
static int L_tremove(lua_State* L)
{
    lua_getglobal(L, "table");
    lua_getfield(L, -1, "remove");
    lua_remove(L, -2);
    int n = lua_gettop(L) - 1;
    for (int i = 1; i <= n; i++) lua_pushvalue(L, i);
    lua_call(L, n, LUA_MULTRET);
    return lua_gettop(L) - n;
}

// sort — alias for table.sort
static int L_sort(lua_State* L)
{
    lua_getglobal(L, "table");
    lua_getfield(L, -1, "sort");
    lua_remove(L, -2);
    int n = lua_gettop(L) - 1;
    for (int i = 1; i <= n; i++) lua_pushvalue(L, i);
    lua_call(L, n, 0);
    return 0;
}

// getn — returns table size (for indexed arrays)
static int L_getn(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnumber(L, luaL_getn(L, 1));
    return 1;
}

// foreach — iterate over table, calling function for each key-value pair
static int L_foreach(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushnil(L); // first key
    while (lua_next(L, 1) != 0) // table is at index 1
    {
        lua_pushvalue(L, 2); // function
        lua_pushvalue(L, -3); // key
        lua_pushvalue(L, -3); // value
        lua_call(L, 2, 1); // call function(key, value)

        if (!lua_isnil(L, -1))
        {
            // If function returns non-nil, stop iteration
            lua_pop(L, 2); // remove value and key
            return 1;
        }
        lua_pop(L, 2); // remove result and value, keep key for next iteration
    }
    return 0;
}

// foreachi — iterate over indexed array, calling function for each index-value pair
static int L_foreachi(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    int n = luaL_getn(L, 1);
    for (int i = 1; i <= n; i++)
    {
        lua_pushvalue(L, 2); // function
        lua_pushnumber(L, i); // index
        lua_rawgeti(L, 1, i); // value
        lua_call(L, 2, 1); // call function(index, value)

        if (!lua_isnil(L, -1))
        {
            // If function returns non-nil, return it and stop
            return 1;
        }
        lua_pop(L, 1); // remove result
    }
    return 0;
}

// unpack — unpack array elements onto the stack
static int L_unpack(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    int n = luaL_getn(L, 1);
    luaL_checkstack(L, n, "table too big to unpack");
    for (int i = 1; i <= n; i++)
    {
        lua_rawgeti(L, 1, i);
    }
    return n;
}

// select — return arguments starting from index (WoW-specific function)
static int L_select(lua_State* L)
{
    int n = lua_gettop(L);
    if (n == 0)
    {
        lua_pushnumber(L, 0);
        return 1;
    }

    if (lua_isstring(L, 1) && strcmp(lua_tostring(L, 1), "#") == 0)
    {
        lua_pushnumber(L, n - 1);
        return 1;
    }

    int start = static_cast<int>(luaL_checknumber(L, 1));
    if (start < 0) start = n + start + 1; // negative indices count from end

    if (start <= 0) start = 1;
    if (start > n) return 0;

    for (int i = start + 1; i <= n; i++)
    {
        lua_pushvalue(L, i);
    }
    return n - start;
}

// pairs — return iterator for table
static int L_pairs(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getglobal(L, "next");
    lua_pushvalue(L, 1);
    lua_pushnil(L);
    return 3;
}

// ipairs — return iterator for indexed array
static int L_ipairs(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushcfunction(L, [](lua_State* L2) -> int {
        int i = static_cast<int>(luaL_checknumber(L2, 2)) + 1;
        lua_pushnumber(L2, i);
        lua_rawgeti(L2, 1, i);
        return lua_isnil(L2, -1) ? 0 : 2;
    });
    lua_pushvalue(L, 1);
    lua_pushnumber(L, 0);
    return 3;
}

// next — basic table iterator
static int L_next(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 2);
    if (lua_next(L, 1))
        return 2;
    else
    {
        lua_pushnil(L);
        return 1;
    }
}

// rawget — get table value without invoking metamethods
static int L_rawget(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_rawget(L, 1);
    return 1;
}

// rawset — set table value without invoking metamethods
static int L_rawset(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_rawset(L, 1);
    lua_pushvalue(L, 1);
    return 1;
}

// ─── Time functions ─────────────────────────────────────────────────────────────

// GetTime() — returns game time in seconds as a float
static int L_GetTime(lua_State* L)
{
    lua_pushnumber(L, FPlatformTime::Seconds());
    return 1;
}

// debugprofilestop() — returns milliseconds since last call
static int L_debugprofilestop(lua_State* L)
{
    static double LastTime = FPlatformTime::Seconds();
    double Now = FPlatformTime::Seconds();
    lua_pushnumber(L, (Now - LastTime) * 1000.0);
    LastTime = Now;
    return 1;
}

// ─── Error handling ─────────────────────────────────────────────────────────────

// r362 (Stefan 15.10: Bartender4 made no bars and nothing in the log said why): the handler
// geterrorhandler() returns always writes the error to the log first (the first 300 a session),
// then calls the handler the UI installed (FrameXML's _ERRORMESSAGE shows it only with the "Show
// Lua errors" option, so AceAddon's safecall failures were invisible).
static int L_WowLoggingErrorHandler(lua_State* L)
{
    static int32 GLoggedErrors = 0;
    if (GLoggedErrors < 300)
    {
        ++GLoggedErrors;
        const char* Msg = lua_tostring(L, 1);
        UE_LOG(LogWowLuaApi, Error, TEXT("[Lua Error] %s"), Msg ? UTF8_TO_TCHAR(Msg) : TEXT("(nil)"));
    }
    static int32 GDepth = 0; // a UI handler that ends in this one again stops here
    lua_getglobal(L, "_ERRORHANDLER");
    if (GDepth == 0 && lua_isfunction(L, -1) && lua_tocfunction(L, -1) != L_WowLoggingErrorHandler)
    {
        lua_insert(L, 1);
        ++GDepth;
        lua_pcall(L, lua_gettop(L) - 1, 0, 0);
        --GDepth;
    }
    return 0;
}

static int L_geterrorhandler(lua_State* L)
{
    lua_pushcfunction(L, L_WowLoggingErrorHandler);
    return 1;
}

static int L_seterrorhandler(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    lua_setglobal(L, "_ERRORHANDLER");
    return 0;
}

// securecall — call function in secure environment
// Accepts securecall(func, ...) or securecall("funcName", ...)
static int L_securecall(lua_State* L)
{
    if (lua_gettop(L) == 0) return 0;

    // If first arg is a string, look it up as a global function
    if (lua_isstring(L, 1))
    {
        const char* FuncName = lua_tostring(L, 1);
        lua_getglobal(L, FuncName);
        lua_replace(L, 1); // replace string with the looked-up function
    }

    if (!lua_isfunction(L, 1)) return 0; // silently fail if not found

    int nargs = lua_gettop(L) - 1;
    int status = lua_pcall(L, nargs, LUA_MULTRET, 0);
    if (status != 0)
    {
        lua_pop(L, 1); // pop error message
        return 0;
    }
    return lua_gettop(L);
}

// hooksecurefunc — hook a secure function
static int L_hooksecurefunc(lua_State* L)
{
    // For now, just store the hook but don't actually implement secure call hooking
    return 0;
}

// issecurevariable — check if a variable is secure
static int L_issecurevariable(lua_State* L)
{
    lua_pushboolean(L, 1); // Everything is secure for now
    return 1;
}

// forceinsecure — force insecure context
static int L_forceinsecure(lua_State* L)
{
    return 0;
}

// scrub(...) — returns its arguments with anything but nil/boolean/number/string replaced by
// nil. RestrictedExecution.lua captures it at load; without it no secure snippet can run.
static int L_scrub(lua_State* L)
{
    const int N = lua_gettop(L);
    for (int I = 1; I <= N; ++I)
    {
        const int T = lua_type(L, I);
        if (T != LUA_TNIL && T != LUA_TBOOLEAN && T != LUA_TNUMBER && T != LUA_TSTRING)
        {
            lua_pushnil(L);
            lua_replace(L, I);
        }
    }
    return N;
}

// tostringall — convert all arguments to strings and return them
static int L_tostringall(lua_State* L)
{
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++)
    {
        if (lua_isstring(L, i))
        {
            // Already a string or number (auto-coerced), leave it
        }
        else if (lua_isnil(L, i))
        {
            lua_remove(L, i);
            lua_pushstring(L, "nil");
            lua_insert(L, i);
        }
        else if (lua_isboolean(L, i))
        {
            bool val = lua_toboolean(L, i) != 0;
            lua_remove(L, i);
            lua_pushstring(L, val ? "true" : "false");
            lua_insert(L, i);
        }
        else
        {
            // Use tostring() for tables, userdata, functions etc.
            lua_getglobal(L, "tostring");
            lua_pushvalue(L, i);
            lua_call(L, 1, 1);
            lua_remove(L, i);
            lua_insert(L, i);
        }
    }
    return n;
}

// ─── Math aliases ───────────────────────────────────────────────────────────────
static void RegisterMathAliases(lua_State* L)
{
    // WoW provides these as global aliases for math.* functions
    const char* aliases[] = {
        "abs", "ceil", "floor", "max", "min", "mod", "fmod", "random",
        "sqrt", "sin", "cos", "tan", "atan", "atan2", "pow", "log", "exp",
        "deg", "rad", nullptr
    };

    for (int i = 0; aliases[i]; i++)
    {
        lua_getglobal(L, "math");
        const char* name = aliases[i];
        // "mod" maps to math.fmod in Lua 5.1, but also add "fmod" alias
        if (strcmp(name, "mod") == 0)
            lua_getfield(L, -1, "fmod");
        else
            lua_getfield(L, -1, name);
        lua_remove(L, -2); // remove math table
        if (!lua_isnil(L, -1))
            lua_setglobal(L, name);
        else
            lua_pop(L, 1);
    }

    // Add math constants
    lua_getglobal(L, "math");
    lua_getfield(L, -1, "pi");
    if (!lua_isnil(L, -1))
        lua_setglobal(L, "pi");
    else
        lua_pop(L, 1);

    lua_getfield(L, -1, "huge");
    if (!lua_isnil(L, -1))
        lua_setglobal(L, "huge");
    else
        lua_pop(L, 1);

    lua_pop(L, 1); // pop math table
}

// ─── Bit library (proper C implementation) ──────────────────────────────────
static int L_bit_band(lua_State* L)
{
    int n = lua_gettop(L);
    uint32 Result = 0xFFFFFFFF;
    for (int i = 1; i <= n; i++)
        Result &= static_cast<uint32>(static_cast<int64>(lua_tonumber(L, i)));
    lua_pushnumber(L, static_cast<double>(Result));
    return 1;
}
static int L_bit_bor(lua_State* L)
{
    int n = lua_gettop(L);
    uint32 Result = 0;
    for (int i = 1; i <= n; i++)
        Result |= static_cast<uint32>(static_cast<int64>(lua_tonumber(L, i)));
    lua_pushnumber(L, static_cast<double>(Result));
    return 1;
}
static int L_bit_bxor(lua_State* L)
{
    int n = lua_gettop(L);
    uint32 Result = 0;
    for (int i = 1; i <= n; i++)
        Result ^= static_cast<uint32>(static_cast<int64>(lua_tonumber(L, i)));
    lua_pushnumber(L, static_cast<double>(Result));
    return 1;
}
static int L_bit_bnot(lua_State* L)
{
    uint32 A = static_cast<uint32>(static_cast<int64>(lua_tonumber(L, 1)));
	const int32 Result = ~static_cast<int32>(A);
	lua_pushnumber(L, static_cast<lua_Number>(Result));
    return 1;
}
static int L_bit_lshift(lua_State* L)
{
    uint32 A = static_cast<uint32>(static_cast<int64>(lua_tonumber(L, 1)));
    int B = static_cast<int>(lua_tonumber(L, 2));
    lua_pushnumber(L, static_cast<double>(A << B));
    return 1;
}
static int L_bit_rshift(lua_State* L)
{
    uint32 A = static_cast<uint32>(static_cast<int64>(lua_tonumber(L, 1)));
    int B = static_cast<int>(lua_tonumber(L, 2));
    lua_pushnumber(L, static_cast<double>(A >> B));
    return 1;
}
static int L_bit_mod(lua_State* L)
{
    int64 A = static_cast<int64>(lua_tonumber(L, 1));
    int64 B = static_cast<int64>(lua_tonumber(L, 2));
    if (B == 0) { lua_pushnumber(L, 0); return 1; }
    lua_pushnumber(L, static_cast<double>(A % B));
    return 1;
}

static void RegisterBitLibrary(lua_State* L)
{
    lua_getglobal(L, "bit");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "bit");
    }
    lua_pushcfunction(L, L_bit_band);  lua_setfield(L, -2, "band");
    lua_pushcfunction(L, L_bit_bor);   lua_setfield(L, -2, "bor");
    lua_pushcfunction(L, L_bit_bxor);  lua_setfield(L, -2, "bxor");
    lua_pushcfunction(L, L_bit_bnot);  lua_setfield(L, -2, "bnot");
    lua_pushcfunction(L, L_bit_lshift); lua_setfield(L, -2, "lshift");
    lua_pushcfunction(L, L_bit_rshift); lua_setfield(L, -2, "rshift");
    lua_pushcfunction(L, L_bit_mod);   lua_setfield(L, -2, "mod");
    lua_pop(L, 1);
}

// ─── Registration ───────────────────────────────────────────────────────────────

void WowLuaApi::RegisterGlobals(lua_State* L)
{
    lua_register(L, "print", L_print);
    lua_register(L, "format", L_format);
    lua_register(L, "strsplit", L_strsplit);
    lua_register(L, "strtrim", L_strtrim);
    lua_register(L, "strjoin", L_strjoin);
    lua_register(L, "tconcat", L_tconcat);
    lua_register(L, "getglobal", L_getglobal);
    lua_register(L, "setglobal", L_setglobal);

    // String aliases
    lua_register(L, "strbyte", L_strbyte);
    lua_register(L, "strchar", L_strchar);
    lua_register(L, "strfind", L_strfind);
    lua_register(L, "strlen", L_strlen);
    lua_register(L, "strlower", L_strlower);
    lua_register(L, "strupper", L_strupper);
    lua_register(L, "strsub", L_strsub);
    lua_register(L, "strrep", L_strrep);
    lua_register(L, "gsub", L_gsub);
    lua_register(L, "gmatch", L_gmatch);
    lua_register(L, "strmatch", L_strmatch);
    lua_register(L, "strrev", L_strrev);

    // Table functions
    lua_register(L, "wipe", L_wipe);
    lua_register(L, "tinsert", L_tinsert);
    lua_register(L, "tremove", L_tremove);
    lua_register(L, "sort", L_sort);
    lua_register(L, "getn", L_getn);
    lua_register(L, "foreach", L_foreach);
    lua_register(L, "foreachi", L_foreachi);
    lua_register(L, "unpack", L_unpack);
    lua_register(L, "select", L_select);
    lua_register(L, "pairs", L_pairs);
    lua_register(L, "ipairs", L_ipairs);
    lua_register(L, "next", L_next);
    lua_register(L, "rawget", L_rawget);
    lua_register(L, "rawset", L_rawset);

    // Blizzard FrameXML frequently calls table.wipe(...) instead of the global
    // wipe(...) helper, so expose the same function through the table library.
    lua_getglobal(L, "table");
    if (lua_istable(L, -1))
    {
        lua_pushcfunction(L, L_wipe);
        lua_setfield(L, -2, "wipe");
    }
    lua_pop(L, 1);

    // Time
    lua_register(L, "GetTime", L_GetTime);
    lua_register(L, "debugprofilestop", L_debugprofilestop);

    // Error handling
    lua_register(L, "geterrorhandler", L_geterrorhandler);
    lua_register(L, "seterrorhandler", L_seterrorhandler);

    // Security functions
    lua_register(L, "securecall", L_securecall);
    lua_register(L, "hooksecurefunc", L_hooksecurefunc);
    lua_register(L, "issecurevariable", L_issecurevariable);
    lua_register(L, "forceinsecure", L_forceinsecure);
    lua_register(L, "scrub", L_scrub);
    // No in-game cinematics are played; StaticPopup_Show refuses every dialog when this
    // global is missing (AUTOEQUIP_BIND left the equip pending forever).
    lua_register(L, "InCinematic", [](lua_State* L2) -> int { lua_pushboolean(L2, 0); return 1; });
    lua_register(L, "tostringall", L_tostringall);

    // Math aliases
    RegisterMathAliases(L);
    RegisterBitLibrary(L);

    // WoW FrameXML expects many global tables and constants to exist before scripts run.
    // Define them here so FrameXML scripts don't error out on nil globals.
    const char* BootstrapLua = R"LUA(
        -- Required global tables
        SlashCmdList = SlashCmdList or {}
        AUTOCOMPLETE_LIST = AUTOCOMPLETE_LIST or {}
        AUTOCOMPLETE_LIST_TEMPLATES = AUTOCOMPLETE_LIST_TEMPLATES or {}
        QuestDifficultyColors = QuestDifficultyColors or {
            header = {r=0.7, g=0.7, b=0.1},
            trivial = {r=0.5, g=0.5, b=0.5},
            standard = {r=0.25, g=0.75, b=0.25},
            difficult = {r=1, g=1, b=0},
            verydifficult = {r=1, g=0.5, b=0.25},
            impossible = {r=1, g=0.1, b=0.1},
        }
        ITEM_QUALITY_COLORS = ITEM_QUALITY_COLORS or {
            [0] = {r=0.62, g=0.62, b=0.62, hex="|cff9d9d9d"},
            [1] = {r=1.0, g=1.0, b=1.0, hex="|cffffffff"},
            [2] = {r=0.12, g=1.0, b=0.0, hex="|cff1eff00"},
            [3] = {r=0.0, g=0.44, b=0.87, hex="|cff0070dd"},
            [4] = {r=0.64, g=0.21, b=0.93, hex="|cffa335ee"},
            [5] = {r=1.0, g=0.5, b=0.0, hex="|cffff8000"},
            [6] = {r=0.9, g=0.8, b=0.5, hex="|cffe6cc80"},
            [7] = {r=0.0, g=0.8, b=1.0, hex="|cff00ccff"},
        }
        RAID_CLASS_COLORS = RAID_CLASS_COLORS or {
            WARRIOR = {r=0.78, g=0.61, b=0.43},
            PALADIN = {r=0.96, g=0.55, b=0.73},
            HUNTER = {r=0.67, g=0.83, b=0.45},
            ROGUE = {r=1.0, g=0.96, b=0.41},
            PRIEST = {r=1.0, g=1.0, b=1.0},
            DEATHKNIGHT = {r=0.77, g=0.12, b=0.23},
            SHAMAN = {r=0.0, g=0.44, b=0.87},
            MAGE = {r=0.41, g=0.8, b=0.94},
            WARLOCK = {r=0.58, g=0.51, b=0.79},
            DRUID = {r=1.0, g=0.49, b=0.04},
        }
        FACTION_BAR_COLORS = FACTION_BAR_COLORS or {
            [1] = {r=0.8, g=0.13, b=0.13}, [2] = {r=1.0, g=0.0, b=0.0},
            [3] = {r=0.93, g=0.4, b=0.13}, [4] = {r=1.0, g=1.0, b=0.0},
            [5] = {r=0.0, g=0.7, b=0.0}, [6] = {r=0.0, g=1.0, b=0.0},
            [7] = {r=0.0, g=0.6, b=1.0}, [8] = {r=0.0, g=1.0, b=1.0},
        }
        NORMAL_FONT_COLOR = NORMAL_FONT_COLOR or {r=1.0, g=0.82, b=0}
        HIGHLIGHT_FONT_COLOR = HIGHLIGHT_FONT_COLOR or {r=1.0, g=1.0, b=1.0}
        RED_FONT_COLOR = RED_FONT_COLOR or {r=1.0, g=0.1, b=0.1}
        GREEN_FONT_COLOR = GREEN_FONT_COLOR or {r=0.1, g=1.0, b=0.1}
        GRAY_FONT_COLOR = GRAY_FONT_COLOR or {r=0.5, g=0.5, b=0.5}
        YELLOW_FONT_COLOR = YELLOW_FONT_COLOR or {r=1.0, g=1.0, b=0.0}
        NORMAL_FONT_COLOR_CODE = NORMAL_FONT_COLOR_CODE or "|cffffd200"
        HIGHLIGHT_FONT_COLOR_CODE = HIGHLIGHT_FONT_COLOR_CODE or "|cffffffff"
        FONT_COLOR_CODE_CLOSE = FONT_COLOR_CODE_CLOSE or "|r"
        PI = math.pi
        MAX_PLAYER_LEVEL = 80
        MAX_PARTY_MEMBERS = 4
        MAX_RAID_MEMBERS = 40
        INVSLOT_FIRST_EQUIPPED = 1
        INVSLOT_LAST_EQUIPPED = 19
        NUM_BAG_SLOTS = 4
        NUM_BANKBAGSLOTS = 7
        MAX_SKILLLINE_TABS = 8
        COPPER_PER_SILVER = 100
        SILVER_PER_GOLD = 100
        COPPER_PER_GOLD = 10000
        MAX_TALENT_TABS = 3
        MAX_NUM_TALENTS = 30
        SHOW_TALENT_LEVEL = 10
        NUM_CHAT_WINDOWS = 10
        DEFAULT_CHAT_FRAME = {
            AddMessage = function(self, msg) end,
            GetName = function() return "ChatFrame1" end
        }
        SELECTED_CHAT_FRAME = nil
        CHAT_CATEGORY_LIST = {}
        CHAT_FRAMES = { "ChatFrame1" }
        CHAT_TIMESTAMP_FORMAT = nil
        ChatTypeGroup = ChatTypeGroup or {}
        ChatTypeInfo = ChatTypeInfo or {}
        COMBAT_LOG_EVENT_LIST = {}
        hash_SlashCmdList = hash_SlashCmdList or {}

        -- Additional missing global tables and constants
        TOOLTIP_DEFAULT_COLOR = {r=1, g=1, b=1}
        TOOLTIP_DEFAULT_BACKGROUND_COLOR = {r=0, g=0, b=0}
        ATTACHMENTS_MAX_SEND = 12
        MAX_PLAYER_LEVEL_TABLE = { [0] = 60, [1] = 70, [2] = 80 } -- indexed by GetAccountExpansionLevel()

        -- UI Dropdown menu globals
        UIDROPDOWNMENU_OPEN_MENU = nil
        UIDROPDOWNMENU_INIT_MENU = nil
        UIDROPDOWNMENU_MENU_LEVEL = 1
        UIDROPDOWNMENU_MENU_VALUE = nil

        -- Static popup globals
        STATICPOPUP_NUMDIALOGS = 4
        StaticPopupDialogs = {}

        -- Spell book constants
        BOOKTYPE_SPELL = "spell"
        BOOKTYPE_PET = "pet"

        -- Party category constants
        LE_PARTY_CATEGORY_HOME = 1
        LE_PARTY_CATEGORY_INSTANCE = 2

        -- Stub functions that FrameXML expects
        function GetItemQualityColor(quality)
            local c = ITEM_QUALITY_COLORS[quality] or ITEM_QUALITY_COLORS[1]
            return c.r, c.g, c.b, c.hex
        end
        function GetExpansionLevel() return 2 end -- WotLK
        function GetAccountExpansionLevel() return GetExpansionLevel() end -- ElvUI DataBars
        function BNGetMaxPlayersInConversation() return 10 end
        function BNGetInfo() return 0, "", "", "", 0, false end
        function BNGetNumFriends() return 0, 0 end
        function BNFeaturesEnabledAndConnected() return false end
        function IsTrialAccount() return false end
        function GetCVarBool(name) return false end
        function GetCVar(name) return "0" end
        function SetCVar(name, value) end
        function RegisterCVar(name, value) end
        function GetCurrentRegion() return 1 end
        function GetCurrentResolution() return 1 end
        function GetScreenResolutions() return "1920x1080" end
        function GetBillingTimeRested() return 0, 0, 0, 0 end
        function GetRestState() return 1, "", 1 end
        function IsXPUserDisabled() return false end
        -- UnitXP, UnitXPMax, GetXPExhaustion now implemented in C++ (LuaStubs.cpp)
        function GetWatchedFactionInfo() return nil end
        function TextStatusBar_Initialize(bar) end
        function TextStatusBar_UpdateTextString(bar) end
        function TextStatusBar_UpdateTextStringWithValues(bar, ...) end

        -- Missing string constants from error logs
        WIDESCREEN_TAG = " (Widescreen)"
        LARGE_NUMBER_SEPERATOR = ","
        DECIMAL_SEPERATOR = "."
        QUEST_TAG_DUNGEON = "Dungeon"
        QUEST_TAG_RAID = "Raid"
        QUEST_TAG_PVP = "PvP"
        QUEST_TAG_GROUP = "Group"
        QUEST_TAG_HEROIC = "Heroic"
        QUEST_TAG_DAILY = "Daily"
        QUEST_TAG_WEEKLY = "Weekly"

        -- Tutorial globals
        TUTORIAL_QUEST_TO_WATCH = TUTORIAL_QUEST_TO_WATCH or {}

        -- Bit library placeholder table (real functions registered from C)
        bit = bit or {}

        -- FrameXML localized string constants (GlobalStrings.lua)
        -- These are normally loaded from GlobalStrings.lua in the MPQ but we need
        -- fallbacks for any that aren't loaded yet when OnLoad fires.
        -- Options panels
        BRIGHTNESS = BRIGHTNESS or "Brightness"
        GAMMA = GAMMA or "Gamma"
        QUALITY = QUALITY or "Quality"
        SOUND_VOLUME = SOUND_VOLUME or "Sound Volume"
        MUSIC_VOLUME = MUSIC_VOLUME or "Music Volume"
        AMBIENCE_VOLUME = AMBIENCE_VOLUME or "Ambience Volume"
        MASTER_VOLUME = MASTER_VOLUME or "Master Volume"
        ENABLE_HARDWARE = ENABLE_HARDWARE or "Enable Hardware"
        ENABLE_SOUNDFX = ENABLE_SOUNDFX or "Sound Effects"
        ENABLE_MUSIC = ENABLE_MUSIC or "Enable Music"
        ENABLE_AMBIENCE = ENABLE_AMBIENCE or "Enable Ambience"
        SOUND_PLAYBACK = SOUND_PLAYBACK or "Sound Playback"
        SOUND_HARDWARE = SOUND_HARDWARE or "Sound Hardware"
        VOICE_CHAT = VOICE_CHAT or "Voice Chat"
        HERTZ = HERTZ or "Hz"
        RESOLUTION = RESOLUTION or "Resolution"
        REFRESH_RATE = REFRESH_RATE or "Refresh Rate"
        MULTISAMPLE_FORMAT = MULTISAMPLE_FORMAT or "Multisampling"
        WINDOWED_MODE = WINDOWED_MODE or "Windowed Mode"

        -- Battle Score headers
        SCORE = SCORE or "Score"
        KILLS = KILLS or "Kills"
        KILLING_BLOWS = KILLING_BLOWS or "Killing Blows"
        DEATHS = DEATHS or "Deaths"
        HONOR = HONOR or "Honor"
        DAMAGE = DAMAGE or "Damage"
        HEALING = HEALING or "Healing"
        TEAM = TEAM or "Team"
        NAME = NAME or "Name"
        SKILL = SKILL or "Skill"

        -- Quest/Map
        TRACK_QUEST = TRACK_QUEST or "Show Quest Objectives"
        SHOW_QUEST_OBJECTIVES_ON_MAP = SHOW_QUEST_OBJECTIVES_ON_MAP or "Show Objectives on Map"

        -- Tabard
        TABARDFRAME_CUSTOMIZATION1 = TABARDFRAME_CUSTOMIZATION1 or "Icon"
        TABARDFRAME_CUSTOMIZATION2 = TABARDFRAME_CUSTOMIZATION2 or "Icon Border"
        TABARDFRAME_CUSTOMIZATION3 = TABARDFRAME_CUSTOMIZATION3 or "Icon Color"
        TABARDFRAME_CUSTOMIZATION4 = TABARDFRAME_CUSTOMIZATION4 or "Border Color"
        TABARDFRAME_CUSTOMIZATION5 = TABARDFRAME_CUSTOMIZATION5 or "Background"

        -- Token / Currency
        TOKEN_FILTER_LABEL = TOKEN_FILTER_LABEL or "Show on Backpack"
        TOKEN_INACTIVE = TOKEN_INACTIVE or "Inactive"

        -- Skill frame
        COLLAPSE = COLLAPSE or "Collapse"

        -- Character Stats
        STAT_STRENGTH = STAT_STRENGTH or "Strength"
        STAT_AGILITY = STAT_AGILITY or "Agility"
        STAT_STAMINA = STAT_STAMINA or "Stamina"
        STAT_INTELLECT = STAT_INTELLECT or "Intellect"
        STAT_SPIRIT = STAT_SPIRIT or "Spirit"
        ARMOR = ARMOR or "Armor"

        -- Chat
        CHAT_MSG_PREFIX = CHAT_MSG_PREFIX or ""
        CHAT_FRAME_DEFAULT_FONT_SIZE = CHAT_FRAME_DEFAULT_FONT_SIZE or 14

        -- Interface options
        CONTROLS_LABEL = CONTROLS_LABEL or "Controls"
        COMBAT_LABEL = COMBAT_LABEL or "Combat"
        DISPLAY_LABEL = DISPLAY_LABEL or "Display"
        SOCIAL_LABEL = SOCIAL_LABEL or "Social"
        ACTIONBARS_LABEL = ACTIONBARS_LABEL or "Action Bars"
        NAMES_LABEL = NAMES_LABEL or "Names"
        CAMERA_LABEL = CAMERA_LABEL or "Camera"
        MOUSE_LABEL = MOUSE_LABEL or "Mouse"
        BINDING_HEADER_OTHER = BINDING_HEADER_OTHER or "Other"
        AUTO_LOOT_DEFAULT_TEXT = AUTO_LOOT_DEFAULT_TEXT or "Auto Loot"

        -- Reputation panel
        REPUTATION_AT_WAR = REPUTATION_AT_WAR or "At War"
        REPUTATION_INACTIVE = REPUTATION_INACTIVE or "Inactive"
        REPUTATION_SHOW_AS_EXPERIENCE_BAR = REPUTATION_SHOW_AS_EXPERIENCE_BAR or "Show as Experience Bar"

        -- Channel frame
        CHAT_BATTLEGROUND = CHAT_BATTLEGROUND or "Battleground"
        CHAT_PARTY = CHAT_PARTY or "Party"

        -- Combat config
        COMBAT_CONFIG_SHOW_QUICKBUTTON = COMBAT_CONFIG_SHOW_QUICKBUTTON or "Show Quick Button"
        COMBAT_CONFIG_SOLO = COMBAT_CONFIG_SOLO or "Solo"
        COMBAT_CONFIG_SHOW_TIMESTAMP = COMBAT_CONFIG_SHOW_TIMESTAMP or "Show Timestamps"
        COMBAT_CONFIG_SHOW_BRACES = COMBAT_CONFIG_SHOW_BRACES or "Show Braces"
        COMBAT_CONFIG_UNIT_NAMES = COMBAT_CONFIG_UNIT_NAMES or "Unit Names"
        COMBAT_CONFIG_SPELL_NAMES = COMBAT_CONFIG_SPELL_NAMES or "Spell Names"
        COMBAT_CONFIG_ITEM_NAMES = COMBAT_CONFIG_ITEM_NAMES or "Item Names"
        COMBAT_CONFIG_FULL_TEXT = COMBAT_CONFIG_FULL_TEXT or "Full Text"
        COMBAT_CONFIG_COLORIZE_UNIT_NAME = COMBAT_CONFIG_COLORIZE_UNIT_NAME or "Unit Name"
        COMBAT_CONFIG_COLORIZE_SPELL_NAMES = COMBAT_CONFIG_COLORIZE_SPELL_NAMES or "Spell Names"
        COMBAT_CONFIG_COLORIZE_SPELL_NAMES_SCHOOL = COMBAT_CONFIG_COLORIZE_SPELL_NAMES_SCHOOL or "By School"
        COMBAT_CONFIG_COLORIZE_DAMAGE_NUMBER = COMBAT_CONFIG_COLORIZE_DAMAGE_NUMBER or "Damage Number"
        COMBAT_CONFIG_COLORIZE_DAMAGE_NUMBER_SCHOOL = COMBAT_CONFIG_COLORIZE_DAMAGE_NUMBER_SCHOOL or "By School"
        COMBAT_CONFIG_COLORIZE_DAMAGE_SCHOOL = COMBAT_CONFIG_COLORIZE_DAMAGE_SCHOOL or "Damage School"
        COMBAT_CONFIG_COLORIZE_ENTIRE_LINE = COMBAT_CONFIG_COLORIZE_ENTIRE_LINE or "Entire Line"
        COMBAT_CONFIG_COLORIZE_ENTIRE_LINE_BY_SOURCE = COMBAT_CONFIG_COLORIZE_ENTIRE_LINE_BY_SOURCE or "By Source"
        COMBAT_CONFIG_COLORIZE_ENTIRE_LINE_BY_TARGET = COMBAT_CONFIG_COLORIZE_ENTIRE_LINE_BY_TARGET or "By Target"
        COMBAT_CONFIG_HIGHLIGHT_LINE = COMBAT_CONFIG_HIGHLIGHT_LINE or "Line"
        COMBAT_CONFIG_HIGHLIGHT_ABILITY = COMBAT_CONFIG_HIGHLIGHT_ABILITY or "Ability"
        COMBAT_CONFIG_HIGHLIGHT_DAMAGE = COMBAT_CONFIG_HIGHLIGHT_DAMAGE or "Damage"
        COMBAT_CONFIG_HIGHLIGHT_SCHOOL = COMBAT_CONFIG_HIGHLIGHT_SCHOOL or "School"

        -- Ignore list
        IGNORE_LABEL = IGNORE_LABEL or "Ignore"

        -- Friends
        FRIENDS_ONLINE = FRIENDS_ONLINE or "Online"
        WHO_NUM_RESULTS = WHO_NUM_RESULTS or "%d players found"

        -- Raid info
        RAID_ID = RAID_ID or "Raid ID"
        RAID_INSTANCE = RAID_INSTANCE or "Instance"

        -- Spellbook
        SHOW_ALL_SPELL_RANKS = SHOW_ALL_SPELL_RANKS or "Show All Spell Ranks"

        -- PVP
        PVP_BANNER_EMBLEM = PVP_BANNER_EMBLEM or "Emblem"
        PVP_BANNER_BORDER = PVP_BANNER_BORDER or "Border"

        -- Help frame
        HELP_REPORT_ISSUE_BULLET1 = HELP_REPORT_ISSUE_BULLET1 or "Describe the issue"
        HELP_REPORT_ISSUE_BULLET2 = HELP_REPORT_ISSUE_BULLET2 or "Include details"

        -- Send mail
        SEND_MAIL_COD_LABEL = SEND_MAIL_COD_LABEL or "C.O.D."
        SEND_MAIL_MONEY_LABEL = SEND_MAIL_MONEY_LABEL or "Send Money"

        -- World map
        WORLD_MAP_TRACK_QUEST = WORLD_MAP_TRACK_QUEST or "Track Quest"

        -- Mac options
        MAC_OPTIONS_MOVIE_RECORDING = MAC_OPTIONS_MOVIE_RECORDING or "Movie Recording"
        MAC_OPTIONS_ITUNES_REMOTE = MAC_OPTIONS_ITUNES_REMOTE or "iTunes Remote"

        -- Token popup
        TOKEN_SHOW_ON_BACKPACK = TOKEN_SHOW_ON_BACKPACK or "Show on Backpack"

        -- Interface options display
        SHOW_CLOAK = SHOW_CLOAK or "Show Cloak"
        SHOW_HELM = SHOW_HELM or "Show Helm"

        -- Toast duration
        BN_TOAST_DURATION = BN_TOAST_DURATION or "Toast Duration"

        -- Chat config class colors
        CLASS_COLORS = CLASS_COLORS or {}
        CLASS_SORT_ORDER = CLASS_SORT_ORDER or {"WARRIOR","PALADIN","HUNTER","ROGUE","PRIEST","DEATHKNIGHT","SHAMAN","MAGE","WARLOCK","DRUID"}

        -- Localized class names (used by ChatConfig class color legend)
        LOCALIZED_CLASS_NAMES_MALE = LOCALIZED_CLASS_NAMES_MALE or {
            WARRIOR = "Warrior", PALADIN = "Paladin", HUNTER = "Hunter",
            ROGUE = "Rogue", PRIEST = "Priest", DEATHKNIGHT = "Death Knight",
            SHAMAN = "Shaman", MAGE = "Mage", WARLOCK = "Warlock", DRUID = "Druid"
        }
        LOCALIZED_CLASS_NAMES_FEMALE = LOCALIZED_CLASS_NAMES_FEMALE or LOCALIZED_CLASS_NAMES_MALE

        -- Tutorial
        TUTORIALS = TUTORIALS or {}

        -- FriendsFrame
        FRIENDS_BUTTON_TYPE_WOW = FRIENDS_BUTTON_TYPE_WOW or 1
        FRIENDS_BUTTON_TYPE_BNET = FRIENDS_BUTTON_TYPE_BNET or 2
        FRIENDS_BUTTON_TYPE_DIVIDER = FRIENDS_BUTTON_TYPE_DIVIDER or 3

        -- Tutorial system
        TUTORIALS = TUTORIALS or {}
        NUM_TUTORIAL_FRAMES = NUM_TUTORIAL_FRAMES or 0

        -- Rune frame (Death Knight) — only active for DK class
        RUNETYPE_BLOOD = RUNETYPE_BLOOD or 1
        RUNETYPE_UNHOLY = RUNETYPE_UNHOLY or 2
        RUNETYPE_FROST = RUNETYPE_FROST or 3
        RUNETYPE_DEATH = RUNETYPE_DEATH or 4

        -- Chat config settings
        CHAT_CONFIG_CHAT_LEFT = CHAT_CONFIG_CHAT_LEFT or "ChatConfigChatSettings"
        CHAT_CONFIG_CHANNEL_LEFT = CHAT_CONFIG_CHANNEL_LEFT or "ChatConfigChannelSettings"

        -- Voice chat mode global
        VOICE_CHAT_MODE = VOICE_CHAT_MODE or 0

        -- SetFormattedText helper (WoW provides this on FontStrings/Frames)
        -- Add to all frame metatable stubs that might call it

        -- WoW-specific error/debug functions
        if not debugstack then
            function debugstack(start, count1, count2)
                return ""
            end
        end
        function debugprofilestart() end
        function issecure() return true end
        function pcallaliases() end

        -- string.join (WoW FrameXML expects this)
        string.join = strjoin
	)LUA";

if (luaL_dostring(L, BootstrapLua) != 0)
{
    UE_LOG(LogWowLuaApi, Error, TEXT("Bootstrap Lua part 1 error: %hs"), lua_tostring(L, -1));
    lua_pop(L, 1);
}

const char* BootstrapLuaPart2 = R"LUA(

        -- Additional constants needed by FrameXML
        NONE = "NONE"
        UNKNOWN = "UNKNOWN"
        ACCEPT = "Accept"
        CANCEL = "Cancel"
        OKAY = "Okay"
        YES = "Yes"
        NO = "No"
        CONTINUE = "Continue"
        GUILD = "Guild"
        WHISPER = "Whisper"
        SAY = "Say"
        YELL = "Yell"
        PARTY = "Party"
        RAID = "Raid"
        OFFICER = "Officer"
        CHANNEL = "Channel"
        EMOTE = "Emote"
        SYSTEM_MESSAGES = "System"

        -- Chat type info table used by chat frame system
        for _, v in ipairs({"SAY","YELL","EMOTE","WHISPER","PARTY","RAID","GUILD","OFFICER","CHANNEL","SYSTEM","LOOT","MONEY","COMBAT_XP_GAIN","COMBAT_HONOR_GAIN"}) do
            ChatTypeInfo[v] = ChatTypeInfo[v] or {r=1, g=1, b=1, sticky=0}
        end

        -- Default chat colours (the client sets these from its own defaults; without them
        -- every chat type was white, e.g. the "Yell:" header and yells).
        do
            local defaults = {
                SAY={1,1,1}, YELL={1,0.251,0.251}, EMOTE={1,0.502,0.251}, TEXT_EMOTE={1,0.502,0.251},
                WHISPER={1,0.502,1}, WHISPER_INFORM={1,0.502,1}, PARTY={0.667,0.667,1}, PARTY_LEADER={0.463,0.784,1},
                RAID={1,0.498,0}, RAID_LEADER={1,0.282,0.035}, RAID_WARNING={1,0.282,0},
                GUILD={0.251,1,0.251}, OFFICER={0.251,0.753,0.251}, CHANNEL={1,0.753,0.753},
                SYSTEM={1,1,0}, MONSTER_SAY={1,1,0.624}, MONSTER_YELL={1,0.251,0.251}, MONSTER_EMOTE={1,0.502,0.251},
                MONSTER_WHISPER={1,0.71,0.922}, RAID_BOSS_EMOTE={1,0.867,0}, RAID_BOSS_WHISPER={1,0.71,0.922},
                BATTLEGROUND={1,0.498,0}, BATTLEGROUND_LEADER={1,0.859,0.718},
                LOOT={0,0.667,0}, MONEY={1,1,0}, SKILL={0.333,0.333,1}, OPENING={0.502,0.502,1}, TRADESKILLS={1,1,1},
                COMBAT_XP_GAIN={0.435,0.435,1}, COMBAT_HONOR_GAIN={0.878,0.792,0.039}, COMBAT_FACTION_CHANGE={0.502,0.502,1},
                ACHIEVEMENT={1,1,0}, GUILD_ACHIEVEMENT={0.251,1,0.251}, AFK={1,0.502,1}, DND={1,0.502,1}, IGNORED={1,0,0},
                BG_SYSTEM_NEUTRAL={1,0.471,0.039}, BG_SYSTEM_ALLIANCE={0,0.682,0.937}, BG_SYSTEM_HORDE={1,0,0},
                RESTRICTED={1,0,0}, FILTERED={1,0,0}, BN_WHISPER={0,1,0.965}, BN_WHISPER_INFORM={0,1,0.965},
            }
            for i = 1, 10 do defaults["CHANNEL" .. i] = {1,0.753,0.753} end
            for chatType, c in pairs(defaults) do
                local info = ChatTypeInfo[chatType]
                if not info then info = {sticky = 0}; ChatTypeInfo[chatType] = info end
                if info.r == nil or (info.r == 1 and info.g == 1 and info.b == 1) then
                    info.r, info.g, info.b = c[1], c[2], c[3]
                end
            end
        end

        -- Missing event table entries
        CHAT_CATEGORY_LIST["BN_WHISPER"] = "BN_WHISPER"
        CHAT_CATEGORY_LIST["BN_CONVERSATION"] = "BN_CONVERSATION"

        -- Addon count table for GetNumAddOns
        WOW_LOADED_ADDONS = WOW_LOADED_ADDONS or {}

        -- UIParent strata constants (used by FrameXML for z-ordering)
        UIPARENT_LEVEL = 0
        UIPARENT_MANAGED_FRAME_POSITIONS = UIPARENT_MANAGED_FRAME_POSITIONS or {}
        UISpecialFrames = UISpecialFrames or {}
        UIPanelWindows = UIPanelWindows or {}

        -- Money frame helpers
        MONEY_ICON_WIDTH = 13
        MONEY_ICON_WIDTH_SMALL = 13

        -- ActionBar constants
        NUM_ACTIONBAR_BUTTONS = 12
        NUM_OVERRIDE_BUTTONS = 6
        NUM_PET_ACTION_SLOTS = 10
        NUM_STANCE_SLOTS = 10
        NUM_POSSESS_SLOTS = 2
        RANGE_INDICATOR = "●"

        -- Inventory constants
        NUM_CONTAINER_FRAMES = 5
        MAX_CONTAINER_ITEMS = 36
        CONTAINER_OFFSET_X = 0
        CONTAINER_OFFSET_Y = 70
        CONTAINER_SCALE = 0.75
        CONTAINER_WIDTH = 192

        -- Additional missing globals from FrameXML
        ATTACK_BUTTON_FLASH_TIME = 0.4
        CASTING_BAR_ALPHA_STEP = 0.05
        CASTING_BAR_FLASH_STEP = 0.2
        CASTING_BAR_HOLD_TIME = 1

        -- Minimap constants
        MINIMAPPING_TIMER = 5.5
        MINIMAP_BOTTOM_EDGE_EXTENT = 192

        -- Unit reaction colors
        FACTION_BAR_COLORS = FACTION_BAR_COLORS or {
            [1] = {r=0.8, g=0.13, b=0.13}, [2] = {r=1.0, g=0.0, b=0.0},
            [3] = {r=0.93, g=0.4, b=0.13}, [4] = {r=1.0, g=1.0, b=0.0},
            [5] = {r=0.0, g=0.7, b=0.0}, [6] = {r=0.0, g=1.0, b=0.0},
            [7] = {r=0.0, g=0.6, b=1.0}, [8] = {r=0.0, g=1.0, b=1.0},
        }

        -- Power type colors
        PowerBarColor = PowerBarColor or {
            [0] = {r=0.0, g=0.0, b=1.0},   -- Mana
            [1] = {r=1.0, g=0.0, b=0.0},   -- Rage
            [2] = {r=1.0, g=0.5, b=0.25},  -- Focus
            [3] = {r=1.0, g=1.0, b=0.0},   -- Energy
            [4] = {r=0.0, g=1.0, b=1.0},   -- Happiness
            [5] = {r=0.5, g=0.5, b=0.5},   -- Runes
            [6] = {r=0.0, g=0.82, b=1.0},  -- Runic Power
        }
    )LUA";

    if (luaL_dostring(L, BootstrapLua) != 0)
    {
        UE_LOG(LogWowLuaApi, Error, TEXT("Bootstrap Lua error: %hs"), lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    // Create stub globals for FontStrings/frames that live in special frame types
    // (e.g. WorldFrame) where widget creation is skipped. Without these, OnUpdate
    // scripts crash trying to index nil globals.
    const char* StubGlobalsLua = R"LUA(
        local function MakeStubWidget(name)
            local t = {}
            local mt = {
                __index = function(self, key)
                    if key == "SetText" or key == "SetFormattedText" or key == "Show" or key == "Hide"
                       or key == "SetAlpha" or key == "SetTextColor" or key == "SetPoint"
                       or key == "SetWidth" or key == "SetHeight" or key == "SetFont"
                       or key == "GetText" or key == "GetStringWidth" or key == "SetVertexColor" then
                        return function() end
                    elseif key == "IsShown" or key == "IsVisible" then
                        return function() return false end
                    elseif key == "GetText" then
                        return function() return "" end
                    end
                    return nil
                end
            }
            setmetatable(t, mt)
            _G[name] = t
        end

        -- WorldFrame FontStrings (widget=0x0 so never created)
        MakeStubWidget("FramerateText")
        MakeStubWidget("FramerateLabelText")
        MakeStubWidget("WorldFrameDropDown")
    )LUA";

    if (luaL_dostring(L, StubGlobalsLua) != 0)
    {
        UE_LOG(LogWowLuaApi, Error, TEXT("Stub globals Lua error: %hs"), lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    UE_LOG(LogWowLuaApi, Log, TEXT("Registered WoW Lua globals (Phase 1 bootstrap + FrameXML globals)"));
}

#else
void WowLuaApi::RegisterGlobals(lua_State*) {}
#endif
