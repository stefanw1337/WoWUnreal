#pragma once
#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogWowLuaFrame, Log, All);

struct lua_State;
class FWowEntityManager;
class UWowConnectionManager;
class FWowEventSystem;
class FWowFrameManager;
class FMpqManager;
class AWowAudioManager;

struct FWowUnitSpellProgress
{
	bool bChanneling = false;
	FString SpellName;
	FString SpellIcon;
	double StartMs = 0.0;
	double EndMs = 0.0;
	uint32 CastId = 0;
};

/** Context passed to Lua API functions so they can access game state */
struct FWowLuaContext
{
    FWowEntityManager* EntityManager = nullptr;
    UWowConnectionManager* ConnectionManager = nullptr;
    FWowEventSystem* EventSystem = nullptr;
    FWowFrameManager* FrameManager = nullptr;
    AWowAudioManager* AudioManager = nullptr;
    /** Client-only focus selection. Unlike target, focus is never sent to the server. */
    uint64 FocusGuid = 0;
    /** Unit under the mouse in the 3D world ("mouseover"), set by the gameplay controller. */
    uint64 MouseoverGuid = 0;
    TFunction<bool(const FString&,bool)> RunBinding;
	TMap<uint64, FWowUnitSpellProgress> UnitSpellProgress;
};

namespace WowLuaApi
{
    bool ResolveBookSpellSlot(lua_State* L, int32 Slot, const char* BookType, uint32& OutSpellId);
    void RegisterAll(lua_State* L);
    void RegisterGlobals(lua_State* L);
    void RegisterStubs(lua_State* L);
    void RegisterFrameApi(lua_State* L);
    /** Chat/social APIs for the stock slash commands + __WowUnrealSlashCommand dispatcher. */
    void RegisterChatApi(lua_State* L);
    /** Death/corpse/resurrection functions behind the stock StaticPopup dialogs (LuaDeathApi.cpp). */
    void RegisterDeathApi(lua_State* L);
    /** Auction House API (LuaAuctionApi.cpp) for Blizzard_AuctionUI-style addons. */
    void RegisterAuctionApi(lua_State* L);
    /** Macros (LuaMacroApi.cpp): GetNumMacros, GetMacroInfo, CreateMacro, EditMacro, PickupMacro, RunMacro ... */
    void RegisterMacroApi(lua_State* L);
    /** Friends/ignore/guild/PvP/vehicle/repair/targeting actions the stock interface calls (LuaGameActionsApi.cpp). */
    void RegisterGameActionsApi(lua_State* L);
    /** Gossip, quest greeting and quest panel API over the native quest dialog (LuaNpcDialogApi.cpp). */
    void RegisterNpcDialogApi(lua_State* L);
    /** r285: guild bank API for the stock Blizzard_GuildBankUI (LuaGuildBankApi.cpp). */
    void RegisterGuildBankApi(lua_State* L);
    /** Placeholders for the stock-interface functions not implemented yet (LuaApiPlaceholders.cpp); registered very last. */
    void RegisterPlaceholderApi(lua_State* L);
    /** Missing client functions common addons need (LuaAddonCompat.cpp); registered last, only if absent. */
    void RegisterAddonCompat(lua_State* L);

    /** Store game context pointer in Lua registry for API functions to use */
    void SetContext(lua_State* L, FWowLuaContext* Ctx);

    /** Clear context from Lua registry (call before VM shutdown to prevent use-after-free) */
    void ClearContext(lua_State* L);

    /** Retrieve context from Lua registry (returns nullptr if not set) */
    FWowLuaContext* GetContext(lua_State* L);

    /** Update the loaded addons list for GetNumAddOns/GetAddOnInfo */
    void UpdateLoadedAddons(FMpqManager* Mpq);

    /** Key for the frame metatable in the Lua registry */
    extern const char* FRAME_METATABLE;

    /** Create the global Font object for an XML <Font> definition (no-op if it exists). */
    void RegisterFontObject(lua_State* L, const FString& Name, const FString& Inherits, float Height, const FString& Flags,
        const FString& FontFile = FString(), const FLinearColor* Color = nullptr);

    /** Internal name given to frames created without one; GetName() hides it. */
    inline constexpr const TCHAR* ANONYMOUS_FRAME_PREFIX = TEXT("__WowAnon_");
    inline constexpr const char* ANONYMOUS_FRAME_PREFIX_ANSI = "__WowAnon_";
}
