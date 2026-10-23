#include "LuaApiRegistry.h"

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

static const char* WOW_CONTEXT_KEY = "WowLuaContext";

const char* WowLuaApi::FRAME_METATABLE = "WowFrame";

// r328: real quest-log API (LuaQuestLogApi.cpp). Declared here, not in the header, so adding it
// does not rebuild every file that includes LuaApiRegistry.h.
namespace WowLuaApi { void RegisterQuestLogApi(lua_State* L); }
// r340: reputation API (LuaReputationApi.cpp), declared here for the same reason.
namespace WowLuaApi { void RegisterReputationApi(lua_State* L); }
// r341: skill API for the stock SkillFrame (LuaSkillApi.cpp).
namespace WowLuaApi { void RegisterSkillApi(lua_State* L); }
// r342: achievement API for the stock Blizzard_AchievementUI (LuaAchievementApi.cpp).
namespace WowLuaApi { void RegisterAchievementApi(lua_State* L); }
// r354: barber shop API for the stock Blizzard_BarberShopUI (LuaBarberShopApi.cpp).
namespace WowLuaApi { void RegisterBarberShopApi(lua_State* L); }

void WowLuaApi::RegisterAll(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    RegisterGlobals(L);
    RegisterStubs(L);
    RegisterFrameApi(L);
    RegisterChatApi(L);
    RegisterDeathApi(L);
    RegisterAuctionApi(L);
    RegisterMacroApi(L);
    RegisterGameActionsApi(L);
    RegisterNpcDialogApi(L);
    RegisterGuildBankApi(L);
    // After RegisterStubs: replaces the old quest-log stubs ("Quest 123", level 1).
    RegisterQuestLogApi(L);
    RegisterReputationApi(L);
    RegisterSkillApi(L);   // after RegisterQuestLogApi: chains onto its __WowWatchFrameTick
    RegisterAchievementApi(L);   // replaces the GetAchievementInfo stub (RegisterStubs)
    RegisterBarberShopApi(L);   // before RegisterPlaceholderApi (its ApplyBarberShopStyle ... placeholders)
    RegisterAddonCompat(L);
    RegisterPlaceholderApi(L);
#endif
}

void WowLuaApi::SetContext(lua_State* L, FWowLuaContext* Ctx)
{
#if HAS_LUA
    if (!L) return;
    lua_pushlightuserdata(L, Ctx);
    lua_setfield(L, LUA_REGISTRYINDEX, WOW_CONTEXT_KEY);
#endif
}

void WowLuaApi::ClearContext(lua_State* L)
{
#if HAS_LUA
    if (!L) return;
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, WOW_CONTEXT_KEY);
#endif
}

FWowLuaContext* WowLuaApi::GetContext(lua_State* L)
{
#if HAS_LUA
    if (!L) return nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, WOW_CONTEXT_KEY);
    FWowLuaContext* Ctx = static_cast<FWowLuaContext*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return Ctx;
#else
    return nullptr;
#endif
}
