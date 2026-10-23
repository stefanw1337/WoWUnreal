#include "WowLuaVM.h"
#include "LuaApi/LuaApiRegistry.h"
#include "WowSavedVariables.h"
#include "WowEventSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowLua, Log, All);

// Lua headers - conditionally include
#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#define HAS_LUA 1
#else
#define HAS_LUA 0
typedef struct lua_State lua_State;
#endif

// r733 (Stefan 23.10 on TormentWoW: "it's loading soo long!"): every UI event (CHAT_MSG_ADDON, ACTIONBAR_SLOT_CHANGED
// ...) took ~42 ms. With ElvUI and the Torment addons the Lua heap sits above 60 % of the old 128 MB sandbox, so
// CollectGarbageIfNeeded(0.60) ran a FULL collection before every event dispatch - 865 AIO addon messages alone cost
// 37 s at login, and the loading screen ran at ~12 fps. The sandbox limit is now 512 MB (the 3.3.5 client has no
// limit at all), and a full collection only repeats once the heap has grown well past what the last one left.
static TMap<const FWowLuaVM*, SIZE_T>& WowLuaLastFullCollect()
{
	static TMap<const FWowLuaVM*, SIZE_T> Usage;
	return Usage;
}

FWowLuaVM::FWowLuaVM() { MemoryLimit = static_cast<SIZE_T>(512) * 1024 * 1024; }
FWowLuaVM::~FWowLuaVM() { Shutdown(); WowLuaLastFullCollect().Remove(this); }

// ── Memory Allocator ─────────────────────────────────────────────────────────────

void* FWowLuaVM::LuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
	FWowLuaVM* VM = static_cast<FWowLuaVM*>(ud);
	const SIZE_T OldSize = ptr ? static_cast<SIZE_T>(osize) : 0;
	const SIZE_T NewSize = static_cast<SIZE_T>(nsize);

	if (nsize == 0)
	{
		// Free
		if (ptr)
		{
			VM->MemoryUsed = VM->MemoryUsed >= OldSize ? VM->MemoryUsed - OldSize : 0;
			FMemory::Free(ptr);
		}
		return nullptr;
	}

	// Only growth counts against the limit. Shrinks are always allowed.
	if (VM->MemoryLimit > 0 && NewSize > OldSize)
	{
		const SIZE_T Growth = NewSize - OldSize;
		if (VM->MemoryUsed + Growth > VM->MemoryLimit)
		{
			UE_LOG(LogWowLua, Error, TEXT("Lua memory limit exceeded (%zu / %zu bytes)"),
				VM->MemoryUsed + Growth, VM->MemoryLimit);
			return nullptr; // Allocation denied — triggers Lua out-of-memory error
		}
	}

	void* NewPtr = FMemory::Realloc(ptr, nsize);
	if (NewPtr)
	{
		const SIZE_T TrimmedUsage = VM->MemoryUsed >= OldSize ? VM->MemoryUsed - OldSize : 0;
		VM->MemoryUsed = TrimmedUsage + NewSize;
	}
	return NewPtr;
}

// ── Instruction Hook ─────────────────────────────────────────────────────────────

#if HAS_LUA
static const char* INSTRUCTION_COUNT_KEY = "__WowInstructionCount";

static void WowLuaInstructionHook(lua_State* LS, lua_Debug* ar)
{
	// Get the VM instance from the registry
	lua_getfield(LS, LUA_REGISTRYINDEX, "__WowLuaVM");
	FWowLuaVM* VM = static_cast<FWowLuaVM*>(lua_touserdata(LS, -1));
	lua_pop(LS, 1);

	if (!VM || VM->GetInstructionLimit() <= 0) return;

	// Get and increment instruction counter
	lua_getfield(LS, LUA_REGISTRYINDEX, INSTRUCTION_COUNT_KEY);
	int32 Count = static_cast<int32>(lua_tointeger(LS, -1)) + 1000; // hook fires every 1000 instructions
	lua_pop(LS, 1);

	if (Count > VM->GetInstructionLimit())
	{
		luaL_error(LS, "Script execution exceeded instruction limit (%d)", VM->GetInstructionLimit());
	}

	lua_pushinteger(LS, Count);
	lua_setfield(LS, LUA_REGISTRYINDEX, INSTRUCTION_COUNT_KEY);
}
#endif

int FWowLuaVM::ProtectedCall(lua_State* State, int Arguments, int Results, int ErrorFunction)
{
#if HAS_LUA
    lua_Debug Stack;
    if (!lua_getstack(State, 0, &Stack))
    {
        lua_pushinteger(State, 0);
        lua_setfield(State, LUA_REGISTRYINDEX, INSTRUCTION_COUNT_KEY);
    }
    return lua_pcall(State, Arguments, Results, ErrorFunction);
#else
    return -1;
#endif
}

void FWowLuaVM::InstallInstructionHook()
{
#if HAS_LUA
	if (!L || InstructionLimit <= 0) return;

	// Store VM pointer in registry for the hook callback
	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_REGISTRYINDEX, "__WowLuaVM");

	// Initialize counter
	lua_pushinteger(L, 0);
	lua_setfield(L, LUA_REGISTRYINDEX, INSTRUCTION_COUNT_KEY);

	// Set hook: fire every 1000 instructions
	lua_sethook(L, WowLuaInstructionHook, LUA_MASKCOUNT, 1000);

	UE_LOG(LogWowLua, Log, TEXT("Instruction hook installed (limit: %d)"), InstructionLimit);
#endif
}

// ── VM Lifecycle ─────────────────────────────────────────────────────────────────

bool FWowLuaVM::Initialize()
{
#if HAS_LUA
	if (L) Shutdown();

	// Create state with custom memory allocator
	L = lua_newstate(LuaAlloc, this);
	if (!L) return false;

	MemoryUsed = 0; // Reset counter
	WowLuaLastFullCollect().Remove(this); // r733: a reloaded UI starts a new heap

	luaL_openlibs(L);
	SandboxGlobals();
	RegisterWowApi();
	InstallInstructionHook();
	lua_gc(L, LUA_GCSETPAUSE, 110);
	lua_gc(L, LUA_GCSETSTEPMUL, 300);

	UE_LOG(LogWowLua, Log, TEXT("Lua 5.1 VM initialized (memory limit: %zu MB, instruction limit: %d)"),
		MemoryLimit / (1024 * 1024), InstructionLimit);
	return true;
#else
	UE_LOG(LogWowLua, Warning, TEXT("Lua not available"));
	return false;
#endif
}

void FWowLuaVM::Shutdown()
{
#if HAS_LUA
	if (L)
	{
		FWowSavedVariables::SaveAll(L);
		lua_close(L);
		L = nullptr;
		MemoryUsed = 0;
	}
#endif
}

void FWowLuaVM::SaveAllVariables()
{
#if HAS_LUA
	if (L) FWowSavedVariables::SaveAll(L);
#endif
}

void FWowLuaVM::SandboxGlobals()
{
#if HAS_LUA
	if (!L) return;

	// Save safe os functions before removing the module
	// WoW FrameXML needs: date(), time(), difftime(), clock()
	lua_getglobal(L, "os");
	if (lua_istable(L, -1))
	{
		// Save os.time, os.date, os.difftime, os.clock as globals
		lua_getfield(L, -1, "time");
		lua_setglobal(L, "time");

		lua_getfield(L, -1, "date");
		lua_setglobal(L, "date");

		lua_getfield(L, -1, "difftime");
		lua_setglobal(L, "difftime");

		lua_getfield(L, -1, "clock");
		lua_setglobal(L, "clock");
	}
	lua_pop(L, 1);

	// Save debug.traceback before removing debug module
	lua_getglobal(L, "debug");
	if (lua_istable(L, -1))
	{
		lua_getfield(L, -1, "traceback");
		lua_setglobal(L, "debugstack"); // WoW uses debugstack() instead of debug.traceback()
	}
	lua_pop(L, 1);

	// Remove dangerous modules
	for (const char* n : {"io","os","debug","loadfile","dofile","require","package"})
	{ lua_pushnil(L); lua_setglobal(L, n); }
#endif
}

void FWowLuaVM::RegisterWowApi()
{
#if HAS_LUA
	if (!L) return;
	WowLuaApi::RegisterAll(L);
	UE_LOG(LogWowLua, Log, TEXT("WoW Lua API registered"));
#endif
}

void FWowLuaVM::StepGarbageCollector(int32 StepSize)
{
#if HAS_LUA
	if (!L) return;
	lua_gc(L, LUA_GCSTEP, FMath::Max(StepSize, 1));
#endif
}

void FWowLuaVM::CollectGarbage()
{
#if HAS_LUA
	if (!L) return;

	const SIZE_T MemoryBefore = MemoryUsed;
	const double StartSeconds = FPlatformTime::Seconds();
	lua_gc(L, LUA_GCCOLLECT, 0);
	const SIZE_T MemoryAfter = MemoryUsed;
	WowLuaLastFullCollect().Add(this, MemoryAfter);
	static double LastLoggedAt = 0.0;
	if (StartSeconds - LastLoggedAt > 10.0)
	{
		LastLoggedAt = StartSeconds;
		UE_LOG(LogWowLua, Log, TEXT("Lua full GC: %.1f -> %.1f MB in %.1f ms (limit %.0f MB)"),
			static_cast<double>(MemoryBefore) / (1024.0 * 1024.0), static_cast<double>(MemoryAfter) / (1024.0 * 1024.0),
			(FPlatformTime::Seconds() - StartSeconds) * 1000.0, static_cast<double>(MemoryLimit) / (1024.0 * 1024.0));
	}
	if (MemoryAfter < MemoryBefore)
	{
		UE_LOG(LogWowLua, Verbose, TEXT("Lua GC reclaimed %.2f MB (%.2f -> %.2f MB)"),
			static_cast<double>(MemoryBefore - MemoryAfter) / (1024.0 * 1024.0),
			static_cast<double>(MemoryBefore) / (1024.0 * 1024.0),
			static_cast<double>(MemoryAfter) / (1024.0 * 1024.0));
	}
#endif
}

bool FWowLuaVM::CollectGarbageIfNeeded(double PressureThreshold)
{
#if HAS_LUA
	if (!L || MemoryLimit == 0)
	{
		return false;
	}

	const double ClampedThreshold = FMath::Clamp(PressureThreshold, 0.0, 1.0);
	const SIZE_T PressureLimit = static_cast<SIZE_T>(static_cast<double>(MemoryLimit) * ClampedThreshold);
	if (MemoryUsed < PressureLimit)
	{
		return false;
	}
	// r733: when the live data itself is above the threshold, a full collection frees little and would run again
	// on the very next event. Wait until the heap has grown 16 MB (or an eighth of the limit) past what the last
	// full collection left; Lua's own incremental collector keeps running in between.
	if (const SIZE_T* LastUsage = WowLuaLastFullCollect().Find(this))
	{
		const SIZE_T Hysteresis = FMath::Max<SIZE_T>(static_cast<SIZE_T>(16) * 1024 * 1024, MemoryLimit / 8);
		if (MemoryUsed < *LastUsage + Hysteresis)
		{
			return false;
		}
	}

	CollectGarbage();
	return true;
#else
	return false;
#endif
}

bool FWowLuaVM::ExecuteString(const FString& Code, const FString& ChunkName)
{
#if HAS_LUA
	if (!L) return false;

	// Reset instruction counter for this execution
	lua_pushinteger(L, 0);
	lua_setfield(L, LUA_REGISTRYINDEX, INSTRUCTION_COUNT_KEY);

	FTCHARToUTF8 C(*Code); FTCHARToUTF8 N(*ChunkName);
	// Lua 5.1's buffer loader does not consume a UTF-8 BOM. FrameXML and
	// custom addon files may include one; remove only the leading signature.
	const int32 Skip=Code.Len()>0 && Code[0]==TCHAR(0xfeff) ? 3 : 0;
	if (luaL_loadbuffer(L, C.Get()+Skip, C.Length()-Skip, N.Get()) != 0)
	{ UE_LOG(LogWowLua, Error, TEXT("Lua load: %s"), UTF8_TO_TCHAR(lua_tostring(L,-1))); lua_pop(L,1); return false; }
	if (lua_pcall(L, 0, 0, 0) != 0)
	{ UE_LOG(LogWowLua, Error, TEXT("Lua exec: %s"), UTF8_TO_TCHAR(lua_tostring(L,-1))); lua_pop(L,1); return false; }
	return true;
#else
	return false;
#endif
}

bool FWowLuaVM::ExecuteBuffer(const TArray<uint8>& Buffer, const FString& ChunkName)
{
#if HAS_LUA
	if (!L) return false;

	// Reset instruction counter for this execution
	lua_pushinteger(L, 0);
	lua_setfield(L, LUA_REGISTRYINDEX, INSTRUCTION_COUNT_KEY);

	FTCHARToUTF8 N(*ChunkName);
	const int32 Skip=Buffer.Num()>=3 && Buffer[0]==0xef && Buffer[1]==0xbb && Buffer[2]==0xbf ? 3 : 0;
	const char* Source=Buffer.IsEmpty() ? "" : reinterpret_cast<const char*>(Buffer.GetData())+Skip;
	if (luaL_loadbuffer(L, Source, Buffer.Num()-Skip, N.Get()) != 0)
	{ UE_LOG(LogWowLua, Error, TEXT("Lua load %s: %s"), *ChunkName, UTF8_TO_TCHAR(lua_tostring(L,-1))); lua_pop(L,1); return false; }
	// Discard return values: LUA_MULTRET left every chunk's results on the host stack.
	if (lua_pcall(L, 0, 0, 0) != 0)
	{ UE_LOG(LogWowLua, Error, TEXT("Lua %s: %s"), *ChunkName, UTF8_TO_TCHAR(lua_tostring(L,-1))); lua_pop(L,1); return false; }
	return true;
#else
	return false;
#endif
}

bool FWowLuaVM::ExecuteAddonBuffer(const TArray<uint8>& Buffer, const FString& ChunkName, const FString& AddonName)
{
#if HAS_LUA
	if (!L) return false;
	if (AddonName.IsEmpty()) return ExecuteBuffer(Buffer, ChunkName);

	lua_pushinteger(L, 0);
	lua_setfield(L, LUA_REGISTRYINDEX, INSTRUCTION_COUNT_KEY);

	const FString Chunk = TEXT("@") + ChunkName;
	FTCHARToUTF8 N(*Chunk);
	const int32 Skip=Buffer.Num()>=3 && Buffer[0]==0xef && Buffer[1]==0xbb && Buffer[2]==0xbf ? 3 : 0;
	const char* Source=Buffer.IsEmpty() ? "" : reinterpret_cast<const char*>(Buffer.GetData())+Skip;
	if (luaL_loadbuffer(L, Source, Buffer.Num()-Skip, N.Get()) != 0)
	{ UE_LOG(LogWowLua, Error, TEXT("Lua load %s: %s"), *ChunkName, UTF8_TO_TCHAR(lua_tostring(L,-1))); lua_pop(L,1); return false; }

	// registry["WowAddonNamespaces"][lower(name)] = private table for this addon
	FTCHARToUTF8 Name(*AddonName);
	FTCHARToUTF8 Key(*AddonName.ToLower());
	lua_getfield(L, LUA_REGISTRYINDEX, "WowAddonNamespaces");
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, "WowAddonNamespaces");
	}
	lua_getfield(L, -1, Key.Get());
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, -3, Key.Get());
	}
	lua_remove(L, -2);                 // chunk, ns
	lua_pushstring(L, Name.Get());     // chunk, ns, name
	lua_insert(L, -2);                 // chunk, name, ns
	if (lua_pcall(L, 2, 0, 0) != 0)
	{ UE_LOG(LogWowLua, Error, TEXT("Lua %s: %s"), *ChunkName, UTF8_TO_TCHAR(lua_tostring(L,-1))); lua_pop(L,1); return false; }
	return true;
#else
	return false;
#endif
}

void FWowLuaVM::FireEvent(const FString& Name, const TArray<FString>& Args)
{
	UE_LOG(LogWowLua, Verbose, TEXT("Event: %s (%d args)"), *Name, Args.Num());

#if HAS_LUA
	if (L)
	{
		// Get the EventSystem from the Lua context to dispatch the event
		FWowLuaContext* Context = WowLuaApi::GetContext(L);
		if (Context && Context->EventSystem)
		{
			Context->EventSystem->FireEvent(Name, Args);
		}
		else
		{
			UE_LOG(LogWowLua, Warning, TEXT("Cannot fire event '%s': EventSystem not available in Lua context"), *Name);
		}
	}
#endif
}
