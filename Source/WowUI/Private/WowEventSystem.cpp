#include "WowEventSystem.h"
#include "WowLuaVM.h"
#include "WowFrameTypes.h"
#include "WowFrameManager.h"
#include "LuaApi/LuaApiRegistry.h"

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

bool FWowEventSystem::ExecuteLuaString(const FString& Code, const FString& ChunkName)
{
	return LuaVM && LuaVM->IsInitialized() && LuaVM->ExecuteString(Code, ChunkName);
}

DEFINE_LOG_CATEGORY_STATIC(LogWowEvent, Log, All);

#if HAS_LUA
namespace
{
	constexpr int32 MaxLegacyLuaArgs = 9;
	const char* LegacyArgNames[MaxLegacyLuaArgs] = {
		"arg1", "arg2", "arg3", "arg4", "arg5", "arg6", "arg7", "arg8", "arg9"
	};

	struct FLuaSavedGlobal
	{
		const char* Name = nullptr;
		int32 Ref = LUA_NOREF;
	};

	static void SaveAndSetLuaGlobal(lua_State* L, const char* Name, int32 StackIndex, TArray<FLuaSavedGlobal>& SavedGlobals)
	{
		lua_getglobal(L, Name);
		FLuaSavedGlobal& Saved = SavedGlobals.AddDefaulted_GetRef();
		Saved.Name = Name;
		Saved.Ref = luaL_ref(L, LUA_REGISTRYINDEX);

		if (StackIndex != 0)
		{
			lua_pushvalue(L, StackIndex);
		}
		else
		{
			lua_pushnil(L);
		}
		lua_setglobal(L, Name);
	}

	static int32 GetLuaAbsIndex(lua_State* L, int32 StackIndex)
	{
		return StackIndex > 0 || StackIndex <= LUA_REGISTRYINDEX
			? StackIndex
			: lua_gettop(L) + StackIndex + 1;
	}

	static bool IsNumericEventArgument(const FString& EventName, int32 ArgIndex)
	{
		// WoW's mirror timer FrameXML uses relational numeric comparisons
		// (not merely arithmetic coercion) for value, max, scale and paused.
		// These events were previously pushed entirely as strings, so the stock
		// 3.3.5 MirrorTimer.lua aborted before it could initialise the bar.
		if (EventName == TEXT("MIRROR_TIMER_START"))
		{
			return ArgIndex >= 1 && ArgIndex <= 4;
		}
        if ((EventName == TEXT("BAG_UPDATE") || EventName == TEXT("PLAYER_EQUIPMENT_CHANGED") ||
            EventName == TEXT("EQUIP_BIND_CONFIRM") || EventName == TEXT("AUTOEQUIP_BIND_CONFIRM")) && ArgIndex == 0) return true;
        if (EventName == TEXT("DELETE_ITEM_CONFIRM") && ArgIndex == 1) return true;
        // r330 (Stefan 14.10: Smite dragged onto Bartender's first button kept an empty icon): the slot is a
        // number - ActionButton.lua and LibActionButton test arg1 == 0 or arg1 == tonumber(self.action), which
        // never matched the string "1", so only native-refreshed buttons updated.
        if (EventName == TEXT("ACTIONBAR_SLOT_CHANGED") && ArgIndex == 0) return true;
		return EventName == TEXT("MIRROR_TIMER_PAUSE") && ArgIndex == 1;
	}

	static void PushEventArgument(lua_State* L, const FString& EventName,
		int32 ArgIndex, const FString& Arg)
	{
		// Typed argument from native code: "\x01n<number>", "\x01z" (nil), "\x01t"/"\x01f" (boolean).
		// Used where an event mixes types per position (COMBAT_LOG_EVENT and friends).
		if (Arg.Len() >= 2 && Arg[0] == TCHAR(1))
		{
			switch (Arg[1])
			{
			case TEXT('n'): lua_pushnumber(L, FCString::Atod(*Arg + 2)); return;
			case TEXT('z'): lua_pushnil(L); return;
			case TEXT('t'): lua_pushboolean(L, 1); return;
			case TEXT('f'): lua_pushboolean(L, 0); return;
			default: break;
			}
		}
		if (IsNumericEventArgument(EventName, ArgIndex))
		{
			lua_pushnumber(L, FCString::Atod(*Arg));
			return;
		}

		// CHAT_MSG_ADDON prefix/message are carried byte for byte (one TCHAR per
		// byte) from the packet; hand Lua the original bytes (AIO payloads are binary).
		if (ArgIndex <= 1 && EventName == TEXT("CHAT_MSG_ADDON"))
		{
			TArray<ANSICHAR> Bytes;
			Bytes.Reserve(Arg.Len());
			for (const TCHAR Char : Arg) Bytes.Add(static_cast<ANSICHAR>(Char & 0xFF));
			lua_pushlstring(L, Bytes.GetData(), Bytes.Num());
			return;
		}
		FTCHARToUTF8 UTF8Arg(*Arg);
		lua_pushstring(L, UTF8Arg.Get());
	}

	static void RestoreLuaGlobals(lua_State* L, TArray<FLuaSavedGlobal>& SavedGlobals)
	{
		for (int32 Index = SavedGlobals.Num() - 1; Index >= 0; --Index)
		{
			const FLuaSavedGlobal& Saved = SavedGlobals[Index];
			lua_rawgeti(L, LUA_REGISTRYINDEX, Saved.Ref);
			lua_setglobal(L, Saved.Name);
			luaL_unref(L, LUA_REGISTRYINDEX, Saved.Ref);
		}
		SavedGlobals.Reset();
	}

	static void SetWowHandlerGlobals(lua_State* L, int32 SelfIndex, int32 EventIndex, const TArray<int32>& ArgIndices, TArray<FLuaSavedGlobal>& SavedGlobals)
	{
		SavedGlobals.Reset();
		SavedGlobals.Reserve(2 + MaxLegacyLuaArgs);

		const int32 AbsSelfIndex = SelfIndex != 0 ? GetLuaAbsIndex(L, SelfIndex) : 0;
		const int32 AbsEventIndex = EventIndex != 0 ? GetLuaAbsIndex(L, EventIndex) : 0;

		SaveAndSetLuaGlobal(L, "this", AbsSelfIndex, SavedGlobals);
		SaveAndSetLuaGlobal(L, "event", AbsEventIndex, SavedGlobals);

		for (int32 ArgIndex = 0; ArgIndex < MaxLegacyLuaArgs; ++ArgIndex)
		{
			const int32 StackIndex = ArgIndex < ArgIndices.Num() && ArgIndices[ArgIndex] != 0
				? GetLuaAbsIndex(L, ArgIndices[ArgIndex])
				: 0;
			SaveAndSetLuaGlobal(L, LegacyArgNames[ArgIndex], StackIndex, SavedGlobals);
		}
	}

	static bool RunOnLoadHandler(
		lua_State* L,
		FWowFrameManager* FrameManager,
		TMap<int64, TMap<FString, int32>>& ScriptRefs,
		TMap<int64, int32>& FrameObjectRefs,
		int64 Handle,
		const FString& FrameName)
	{
		TMap<FString, int32>* FrameScripts = ScriptRefs.Find(Handle);
		if (!FrameScripts)
		{
			return false;
		}

		int32* OnLoadRef = FrameScripts->Find(TEXT("OnLoad"));
		if (!OnLoadRef)
		{
			return false;
		}

		lua_rawgeti(L, LUA_REGISTRYINDEX, *OnLoadRef);
		if (!lua_isfunction(L, -1))
		{
			lua_pop(L, 1);
			return false;
		}

		int32* FrameRef = FrameObjectRefs.Find(Handle);
		if (FrameRef)
		{
			lua_rawgeti(L, LUA_REGISTRYINDEX, *FrameRef);
		}
		else
		{
			lua_newtable(L);
		}

		TArray<FLuaSavedGlobal> SavedGlobals;
		SetWowHandlerGlobals(L, lua_gettop(L), 0, TArray<int32>(), SavedGlobals);

		if (FWowLuaVM::ProtectedCall(L, 1, 0, 0) != 0)
		{
			RestoreLuaGlobals(L, SavedGlobals);
			UE_LOG(LogWowEvent, Error, TEXT("OnLoad error [%lld] %s: %hs"),
				Handle, *FrameName, lua_tostring(L, -1));
			lua_pop(L, 1);

			if (FrameManager)
			{
				FrameManager->SetFrameVisible(Handle, false);
			}
		}
		else
		{
			RestoreLuaGlobals(L, SavedGlobals);
		}

		return true;
	}
}
#endif

void FWowEventSystem::RegisterEvent(int64 FrameHandle, const FString& EventName)
{
	EventRegistrations.FindOrAdd(EventName).Add(FrameHandle);
	UE_LOG(LogWowEvent, Verbose, TEXT("Frame %lld registered for event: %s"), FrameHandle, *EventName);
}

void FWowEventSystem::UnregisterEvent(int64 FrameHandle, const FString& EventName)
{
	if (TSet<int64>* Handles = EventRegistrations.Find(EventName))
	{
		Handles->Remove(FrameHandle);
		if (Handles->Num() == 0)
		{
			EventRegistrations.Remove(EventName);
		}
	}
}

void FWowEventSystem::UnregisterAllEvents(int64 FrameHandle)
{
	TArray<FString> EmptyKeys;
	for (auto& Pair : EventRegistrations)
	{
		Pair.Value.Remove(FrameHandle);
		if (Pair.Value.Num() == 0)
		{
			EmptyKeys.Add(Pair.Key);
		}
	}
	for (const FString& Key : EmptyKeys)
	{
		EventRegistrations.Remove(Key);
	}
}

void FWowEventSystem::FireEvent(const FString& EventName, const TArray<FString>& Args)
{
	const TSet<int64>* Handles = EventRegistrations.Find(EventName);
	if (!Handles || Handles->Num() == 0)
	{
		// Log when important events have no listeners
		if (EventName == TEXT("ACTIONBAR_SLOT_CHANGED") || EventName == TEXT("ACTIONBAR_UPDATE_STATE") ||
		    EventName == TEXT("UNIT_HEALTH") || EventName == TEXT("UNIT_MANA"))
		{
			UE_LOG(LogWowEvent, Warning, TEXT("Event %s has NO registered frames"), *EventName);
		}
		return;
	}

	UE_LOG(LogWowEvent, Log, TEXT("Firing event %s to %d frames (args: %d)"), *EventName, Handles->Num(), Args.Num());

#if HAS_LUA
	if (!LuaVM || !LuaVM->IsInitialized())
	{
		return;
	}

	lua_State* L = LuaVM->GetState();
	if (!L)
	{
		return;
	}

	LuaVM->CollectGarbageIfNeeded(0.60);

	// Copy handles — handlers may modify the registration set during iteration
	TArray<int64> HandlesCopy = Handles->Array();

	for (int32 HandleIndex = 0; HandleIndex < HandlesCopy.Num(); ++HandleIndex)
	{
		const int64 Handle = HandlesCopy[HandleIndex];
		// Look up compiled OnEvent function
		TMap<FString, int32>* FrameScripts = ScriptRefs.Find(Handle);
		if (!FrameScripts) continue;

		int32* ScriptRef = FrameScripts->Find(TEXT("OnEvent"));
		if (!ScriptRef) continue;

		// Push the compiled OnEvent function
		lua_rawgeti(L, LUA_REGISTRYINDEX, *ScriptRef);
		if (!lua_isfunction(L, -1))
		{
			lua_pop(L, 1);
			continue;
		}

		// Push self (frame table)
		int32* FrameRef = FrameObjectRefs.Find(Handle);
		if (FrameRef)
		{
			lua_rawgeti(L, LUA_REGISTRYINDEX, *FrameRef);
		}
		else
		{
			lua_newtable(L); // fallback empty table
		}

		// Push event name
		FTCHARToUTF8 UTF8Event(*EventName);
		lua_pushstring(L, UTF8Event.Get());

		// Push args
		for (int32 ArgIndex = 0; ArgIndex < Args.Num(); ++ArgIndex)
		{
			PushEventArgument(L, EventName, ArgIndex, Args[ArgIndex]);
		}

		// Call: func(self, event, arg1, arg2, ...)
		int32 NumArgs = 2 + Args.Num();
		const int32 FunctionIndex = lua_gettop(L) - NumArgs;
		const int32 SelfIndex = FunctionIndex + 1;
		const int32 EventIndex = FunctionIndex + 2;
		TArray<int32> LegacyArgIndices;
		LegacyArgIndices.Reserve(FMath::Min(Args.Num(), MaxLegacyLuaArgs));
		for (int32 ArgIndex = 0; ArgIndex < Args.Num() && ArgIndex < MaxLegacyLuaArgs; ++ArgIndex)
		{
			LegacyArgIndices.Add(FunctionIndex + 3 + ArgIndex);
		}

		TArray<FLuaSavedGlobal> SavedGlobals;
		SetWowHandlerGlobals(L, SelfIndex, EventIndex, LegacyArgIndices, SavedGlobals);
		if (FWowLuaVM::ProtectedCall(L, NumArgs, 0, 0) != 0)
		{
			RestoreLuaGlobals(L, SavedGlobals);
			UE_LOG(LogWowEvent, Error, TEXT("OnEvent error [frame %lld] %s: %s"),
				Handle, *EventName, UTF8_TO_TCHAR(lua_tostring(L, -1)));
			lua_pop(L, 1);
		}
		else
		{
			RestoreLuaGlobals(L, SavedGlobals);
		}

	}

	// Lua 5.1 interprets LUA_GCSTEP's data argument as KiB of allocation debt.
	// The former 1024 KiB step after every 16 handlers (plus another at the end)
	// could turn one large event dispatch into several megabytes of repeated GC
	// work.  A single small incremental step keeps collection smooth; the pressure
	// guard above still performs a full collection before the VM approaches its
	// memory limit.
	LuaVM->StepGarbageCollector(16);
#endif
}

void FWowEventSystem::SetFrameScript(int64 Handle, const FString& ScriptName, const FString& Code)
{
#if HAS_LUA
	if (!LuaVM) return;

	lua_State* L = LuaVM->GetState();
	if (!L) return;

	if (Code.IsEmpty())
	{
		// frame:SetScript(name, nil) removes the handler. This used to be ignored, so e.g.
		// LibDBIcon's OnDragStop could not stop its OnUpdate and the minimap button kept
		// following the cursor around the minimap.
		if (TMap<FString, int32>* FrameScripts = ScriptRefs.Find(Handle))
		{
			if (const int32* OldRef = FrameScripts->Find(ScriptName))
			{
				luaL_unref(L, LUA_REGISTRYINDEX, *OldRef);
				FrameScripts->Remove(ScriptName);
			}
		}
		const FString GlobalKey = FString::Printf(TEXT("__WowScript_%lld_%s"), Handle, *ScriptName);
		lua_pushnil(L);
		lua_setglobal(L, TCHAR_TO_UTF8(*GlobalKey));
		if (ScriptName == TEXT("OnUpdate"))
		{
			OnUpdateFrames.Remove(Handle);
		}
		return;
	}

	// Wrap the script code with the correct function signature for each script type.
	// WoW scripts have different parameter names depending on their type:
	//   OnEvent:  function(self, event, ...)
	//   OnUpdate: function(self, elapsed)
	//   OnClick:  function(self, button, down)
	//   OnShow/OnHide/OnLoad: function(self)
	//   OnEnter/OnLeave: function(self, motion)
	//   OnMouseDown/Up: function(self, button)
	//   OnValueChanged: function(self, value)
	//   OnTextChanged: function(self)
	//   OnChar: function(self, text)
	//   OnSizeChanged: function(self, w, h)
	//   OnScrollRangeChanged: function(self, xrange, yrange)
	// All WoW script handlers include ... for forward compatibility.
	// FrameXML code frequently uses ... even in handlers that don't formally have varargs.
	FString Params;
	if (ScriptName == TEXT("OnEvent"))
		Params = TEXT("self, event, ...");
	else if (ScriptName == TEXT("OnUpdate"))
		Params = TEXT("self, elapsed, ...");
	else if (ScriptName == TEXT("OnClick") || ScriptName == TEXT("PreClick") || ScriptName == TEXT("PostClick"))
		Params = TEXT("self, button, down, ...");
	else if (ScriptName == TEXT("OnEnter") || ScriptName == TEXT("OnLeave"))
		Params = TEXT("self, motion, ...");
	else if (ScriptName == TEXT("OnMouseDown") || ScriptName == TEXT("OnMouseUp"))
		Params = TEXT("self, button, ...");
	else if (ScriptName == TEXT("OnValueChanged"))
		Params = TEXT("self, value, ...");
	else if (ScriptName == TEXT("OnChar"))
		Params = TEXT("self, text, ...");
	else if (ScriptName == TEXT("OnSizeChanged"))
		Params = TEXT("self, w, h, ...");
	else if (ScriptName == TEXT("OnScrollRangeChanged"))
		Params = TEXT("self, xrange, yrange, ...");
	else if (ScriptName == TEXT("OnDragStart") || ScriptName == TEXT("OnDragStop"))
		Params = TEXT("self, button, ...");
	else if (ScriptName == TEXT("OnReceiveDrag"))
		Params = TEXT("self, ...");
	else
		Params = TEXT("self, ...");

	// For function="..." handlers generated by BuildFunctionHandlerWrapper, use
	// vararg (...) instead of named params so all args are forwarded correctly.
	// Named params like (self, elapsed, ...) consume args, making ... incomplete.
	// Detect by checking for the exact pattern our wrapper generates.
	bool bIsFunctionHandler = Code.StartsWith(TEXT("local fn = _G[\""));
	FString Wrapped = bIsFunctionHandler
		? FString::Printf(TEXT("return function(...) %s end"), *Code)
		: FString::Printf(TEXT("return function(%s) %s end"), *Params, *Code);
	FTCHARToUTF8 UTF8Code(*Wrapped);
	FString ChunkName = FString::Printf(TEXT("=[%lld]:%s"), Handle, *ScriptName);
	FTCHARToUTF8 UTF8Chunk(*ChunkName);

	if (luaL_loadbuffer(L, UTF8Code.Get(), UTF8Code.Length(), UTF8Chunk.Get()) != 0)
	{
		UE_LOG(LogWowEvent, Error, TEXT("Script compile error [%lld] %s: %s"),
			Handle, *ScriptName, UTF8_TO_TCHAR(lua_tostring(L, -1)));
		lua_pop(L, 1);
		return;
	}

	// Execute the chunk to get the function on the stack
	if (FWowLuaVM::ProtectedCall(L, 0, 1, 0) != 0)
	{
		UE_LOG(LogWowEvent, Error, TEXT("Script load error [%lld] %s: %s"),
			Handle, *ScriptName, UTF8_TO_TCHAR(lua_tostring(L, -1)));
		lua_pop(L, 1);
		return;
	}

	// Store the function as a registry reference
	int32 Ref = luaL_ref(L, LUA_REGISTRYINDEX);

	// Remove old ref if any
	TMap<FString, int32>& FrameScripts = ScriptRefs.FindOrAdd(Handle);
	if (int32* OldRef = FrameScripts.Find(ScriptName))
	{
		luaL_unref(L, LUA_REGISTRYINDEX, *OldRef);
	}
	FrameScripts.Add(ScriptName, Ref);

	// Store in a global key so frame:GetScript() can retrieve it.
	// This matches what LF_SetScript does in LuaFrameApi.cpp.
	FString GlobalKey = FString::Printf(TEXT("__WowScript_%lld_%s"), Handle, *ScriptName);
	FTCHARToUTF8 UTF8Key(*GlobalKey);
	lua_rawgeti(L, LUA_REGISTRYINDEX, Ref);
	lua_setglobal(L, UTF8Key.Get());

	// Track frames with OnUpdate handlers for tick dispatch
	if (ScriptName == TEXT("OnUpdate"))
	{
		OnUpdateFrames.Add(Handle);
	}

	UE_LOG(LogWowEvent, Verbose, TEXT("Compiled script [%lld] %s (ref %d)"), Handle, *ScriptName, Ref);
#endif
}

void FWowEventSystem::SetFrameScriptRef(int64 Handle, const FString& ScriptName, int32 LuaRef)
{
#if HAS_LUA
	if (!LuaVM) return;
	lua_State* L = LuaVM->GetState();
	if (!L) return;

	// Remove old ref if any
	TMap<FString, int32>& FrameScripts = ScriptRefs.FindOrAdd(Handle);
	if (int32* OldRef = FrameScripts.Find(ScriptName))
	{
		luaL_unref(L, LUA_REGISTRYINDEX, *OldRef);
	}
	FrameScripts.Add(ScriptName, LuaRef);

	// Track OnUpdate frames
	if (ScriptName == TEXT("OnUpdate"))
	{
		OnUpdateFrames.Add(Handle);
	}

	UE_LOG(LogWowEvent, Verbose, TEXT("Stored script ref [%lld] %s (ref %d)"), Handle, *ScriptName, LuaRef);
#endif
}

bool FWowEventSystem::PushFrameObject(int64 Handle) const
{
#if HAS_LUA
	if (!LuaVM) return false;
	lua_State* L = LuaVM->GetState();
	const int32* Ref = FrameObjectRefs.Find(Handle);
	if (!L || !Ref) return false;
	lua_rawgeti(L, LUA_REGISTRYINDEX, *Ref);
	if (lua_istable(L, -1)) return true;
	lua_pop(L, 1);
#endif
	return false;
}

bool FWowEventSystem::PushFrameScript(int64 Handle, const FString& ScriptName) const
{
#if HAS_LUA
	if (!LuaVM) return false;
	lua_State* L = LuaVM->GetState();
	const TMap<FString, int32>* FrameScripts = ScriptRefs.Find(Handle);
	const int32* Ref = FrameScripts ? FrameScripts->Find(ScriptName) : nullptr;
	if (!L || !Ref) return false;
	lua_rawgeti(L, LUA_REGISTRYINDEX, *Ref);
	if (lua_isfunction(L, -1)) return true;
	lua_pop(L, 1);
#endif
	return false;
}

void FWowEventSystem::CreateFrameObject(int64 Handle, const FString& FrameName, int32 FrameID)
{
#if HAS_LUA
	if (!LuaVM) return;

	lua_State* L = LuaVM->GetState();
	if (!L) return;

	const auto PopulateFrameTable = [&](int32 TableIndex)
	{
		const int32 AbsTableIndex = GetLuaAbsIndex(L, TableIndex);

		lua_pushinteger(L, Handle);
		lua_setfield(L, AbsTableIndex, "__handle");
		// frame[0] is the frame's userdata in the client; SecureHandlers/RestrictedFrames test
		// type(frame[0]) == "userdata" and build surrogates { [0] = frame[0], [1] = frame }.
		lua_pushlightuserdata(L, reinterpret_cast<void*>(static_cast<uintptr_t>(Handle + 1)));
		lua_rawseti(L, AbsTableIndex, 0);

		if (!FrameName.IsEmpty())
		{
			FTCHARToUTF8 UTF8Name(*FrameName);
			lua_pushstring(L, UTF8Name.Get());
			lua_setfield(L, AbsTableIndex, "__name");
		}

		if (FrameID != 0)
		{
			lua_pushnumber(L, FrameID);
			lua_setfield(L, AbsTableIndex, "__id");
		}

		if (FrameName == TEXT("UIParent"))
		{
			const struct
			{
				const TCHAR* Name;
				lua_Number Value;
			} DefaultUiParentAttributes[] = {
				{ TEXT("DEFAULT_FRAME_WIDTH"), 384.0 },
				{ TEXT("TOP_OFFSET"), -104.0 },
				{ TEXT("LEFT_OFFSET"), 0.0 },
				{ TEXT("CENTER_OFFSET"), 384.0 },
				{ TEXT("RIGHT_OFFSET"), 768.0 },
				{ TEXT("RIGHT_OFFSET_BUFFER"), 80.0 },
			};

			for (const auto& Attribute : DefaultUiParentAttributes)
			{
				const FString AttributeKey = FString::Printf(TEXT("__attr_%s"), Attribute.Name);
				FTCHARToUTF8 UTF8AttributeKey(*AttributeKey);
				lua_pushnumber(L, Attribute.Value);
				lua_setfield(L, AbsTableIndex, UTF8AttributeKey.Get());
			}
		}

		luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
		if (!lua_isnil(L, -1))
		{
			lua_setmetatable(L, AbsTableIndex);
		}
		else
		{
			lua_pop(L, 1);
		}
	};

	if (int32* ExistingRef = FrameObjectRefs.Find(Handle))
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, *ExistingRef);
		if (lua_istable(L, -1))
		{
			PopulateFrameTable(-1);

			if (!FrameName.IsEmpty())
			{
				lua_pushvalue(L, -1);
				FTCHARToUTF8 UTF8Name(*FrameName);
				lua_setglobal(L, UTF8Name.Get());
			}

			lua_pop(L, 1);
			UE_LOG(LogWowEvent, Verbose, TEXT("Updated frame object [%lld] %s"), Handle, *FrameName);
			return;
		}

		lua_pop(L, 1);
		luaL_unref(L, LUA_REGISTRYINDEX, *ExistingRef);
		FrameObjectRefs.Remove(Handle);
	}

	lua_newtable(L);
	PopulateFrameTable(-1);

	// Store as registry ref
	int32 Ref = luaL_ref(L, LUA_REGISTRYINDEX);

	FrameObjectRefs.Add(Handle, Ref);

	// Also register as a global variable if the frame has a name
	if (!FrameName.IsEmpty())
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, Ref);
		FTCHARToUTF8 UTF8Name(*FrameName);
		lua_setglobal(L, UTF8Name.Get());
	}

	UE_LOG(LogWowEvent, Verbose, TEXT("Created frame object [%lld] %s"), Handle, *FrameName);
#endif
}

void FWowEventSystem::AliasGlobalObject(const FString& ExistingName, const FString& AliasName)
{
#if HAS_LUA
	if (!LuaVM || ExistingName.IsEmpty() || AliasName.IsEmpty() || ExistingName == AliasName)
	{
		return;
	}

	lua_State* L = LuaVM->GetState();
	if (!L)
	{
		return;
	}

	FTCHARToUTF8 UTF8Alias(*AliasName);
	lua_getglobal(L, UTF8Alias.Get());
	const bool bAliasExists = !lua_isnil(L, -1);
	lua_pop(L, 1);
	if (bAliasExists)
	{
		return;
	}

	FTCHARToUTF8 UTF8Existing(*ExistingName);
	lua_getglobal(L, UTF8Existing.Get());
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		return;
	}

	lua_setglobal(L, UTF8Alias.Get());
#endif
}

void FWowEventSystem::CreateTextureRegionGlobal(const FString& RegionName, int64 ParentHandle, const FString& ObjectType)
{
#if HAS_LUA
	if (!LuaVM || RegionName.IsEmpty()) return;

	lua_State* L = LuaVM->GetState();
	if (!L) return;

	// Create a Lua table for this texture region with the frame metatable
	lua_newtable(L);

	// Store the name
	FTCHARToUTF8 UTF8Name(*RegionName);
	lua_pushstring(L, UTF8Name.Get());
	lua_setfield(L, -2, "__name");

	if (ParentHandle >= 0)
	{
		lua_pushinteger(L, ParentHandle);
		lua_setfield(L, -2, "__parentHandle");
	}

	if (!ObjectType.IsEmpty())
	{
		FTCHARToUTF8 UTF8ObjectType(*ObjectType);
		lua_pushstring(L, UTF8ObjectType.Get());
		lua_setfield(L, -2, "__objectType");
	}

	// Set the frame metatable so region:SetTexture() etc work
	luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
	if (!lua_isnil(L, -1))
	{
		lua_setmetatable(L, -2);
	}
	else
	{
		lua_pop(L, 1);
	}

	// Register as global
	lua_setglobal(L, UTF8Name.Get());
#endif
}

void FWowEventSystem::RemoveFrameScripts(int64 Handle)
{
#if HAS_LUA
	if (!LuaVM) return;
	lua_State* L = LuaVM->GetState();
	if (!L) return;

	// Remove script refs
	if (TMap<FString, int32>* FrameScripts = ScriptRefs.Find(Handle))
	{
		for (auto& Pair : *FrameScripts)
		{
			luaL_unref(L, LUA_REGISTRYINDEX, Pair.Value);
		}
		ScriptRefs.Remove(Handle);
	}

	// Remove from OnUpdate tracking
	OnUpdateFrames.Remove(Handle);

	// Remove frame object ref
	if (int32* FrameRef = FrameObjectRefs.Find(Handle))
	{
		luaL_unref(L, LUA_REGISTRYINDEX, *FrameRef);
		FrameObjectRefs.Remove(Handle);
	}
#endif
}

bool FWowEventSystem::RunFrameScript(int64 Handle, const FString& ScriptName, const TArray<FString>& StringArgs)
{
#if HAS_LUA
	if (!LuaVM) return false;
	lua_State* L = LuaVM->GetState();
	if (!L) return false;

	TMap<FString, int32>* FrameScripts = ScriptRefs.Find(Handle);
	if (!FrameScripts) return false;

	int32* ScriptRef = FrameScripts->Find(ScriptName);
	if (!ScriptRef) return false;

	// Push the compiled script function
	lua_rawgeti(L, LUA_REGISTRYINDEX, *ScriptRef);
	if (!lua_isfunction(L, -1))
	{
		lua_pop(L, 1);
		return false;
	}

	// Push self (frame table)
	int32* FrameRef = FrameObjectRefs.Find(Handle);
	if (FrameRef)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, *FrameRef);
	}
	else
	{
		lua_newtable(L);
	}

	// Push string args
	for (const FString& Arg : StringArgs)
	{
		FTCHARToUTF8 UTF8Arg(*Arg);
		lua_pushstring(L, UTF8Arg.Get());
	}

	// Call: func(self, arg1, arg2, ...)
	int32 NumArgs = 1 + StringArgs.Num();
	const int32 FunctionIndex = lua_gettop(L) - NumArgs;
	const int32 SelfIndex = FunctionIndex + 1;
	TArray<int32> LegacyArgIndices;
	LegacyArgIndices.Reserve(FMath::Min(StringArgs.Num(), MaxLegacyLuaArgs));
	for (int32 ArgIndex = 0; ArgIndex < StringArgs.Num() && ArgIndex < MaxLegacyLuaArgs; ++ArgIndex)
	{
		LegacyArgIndices.Add(FunctionIndex + 2 + ArgIndex);
	}

	TArray<FLuaSavedGlobal> SavedGlobals;
	SetWowHandlerGlobals(L, SelfIndex, 0, LegacyArgIndices, SavedGlobals);
	if (FWowLuaVM::ProtectedCall(L, NumArgs, 0, 0) != 0)
	{
		RestoreLuaGlobals(L, SavedGlobals);
		const char* Err = lua_tostring(L, -1);
		UE_LOG(LogWowEvent, Error, TEXT("RunFrameScript error [%lld] %s: %s"),
			Handle, *ScriptName, Err ? UTF8_TO_TCHAR(Err) : TEXT("unknown"));
		lua_pop(L, 1);
		return false;
	}
	RestoreLuaGlobals(L, SavedGlobals);
	return true;
#else
	return false;
#endif
}

bool FWowEventSystem::RunFrameClickScript(int64 Handle, const FString& ScriptName, const FString& Button, bool bMouseDown)
{
#if HAS_LUA
	if (!LuaVM) return false;
	lua_State* L = LuaVM->GetState();
	if (!L) return false;

	TMap<FString, int32>* FrameScripts = ScriptRefs.Find(Handle);
	if (!FrameScripts) return false;

	int32* ScriptRef = FrameScripts->Find(ScriptName);
	if (!ScriptRef) return false;

	lua_rawgeti(L, LUA_REGISTRYINDEX, *ScriptRef);
	if (!lua_isfunction(L, -1))
	{
		lua_pop(L, 1);
		return false;
	}

	int32* FrameRef = FrameObjectRefs.Find(Handle);
	if (FrameRef)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, *FrameRef);
	}
	else
	{
		lua_newtable(L);
	}

	FTCHARToUTF8 UTF8Button(*Button);
	lua_pushstring(L, UTF8Button.Get());
	lua_pushboolean(L, bMouseDown ? 1 : 0);

	const int32 NumArgs = 3;
	const int32 FunctionIndex = lua_gettop(L) - NumArgs;
	const int32 SelfIndex = FunctionIndex + 1;
	TArray<int32> LegacyArgIndices;
	LegacyArgIndices.Add(FunctionIndex + 2);
	LegacyArgIndices.Add(FunctionIndex + 3);

	TArray<FLuaSavedGlobal> SavedGlobals;
	SetWowHandlerGlobals(L, SelfIndex, 0, LegacyArgIndices, SavedGlobals);
	if (FWowLuaVM::ProtectedCall(L, NumArgs, 0, 0) != 0)
	{
		RestoreLuaGlobals(L, SavedGlobals);
		const char* Err = lua_tostring(L, -1);
		UE_LOG(LogWowEvent, Error, TEXT("RunFrameClickScript error [%lld] %s: %s"),
			Handle, *ScriptName, Err ? UTF8_TO_TCHAR(Err) : TEXT("unknown"));
		lua_pop(L, 1);
		return false;
	}

	RestoreLuaGlobals(L, SavedGlobals);
	return true;
#else
	return false;
#endif
}

FString FWowEventSystem::GetFrameAttribute(int64 Handle, const FString& AttrName) const
{
#if HAS_LUA
	if (!LuaVM) return FString();
	lua_State* L = LuaVM->GetState();
	if (!L) return FString();

	const int32* FrameRef = FrameObjectRefs.Find(Handle);
	if (!FrameRef) return FString();

	lua_rawgeti(L, LUA_REGISTRYINDEX, *FrameRef);
	FString Key = FString::Printf(TEXT("__attr_%s"), *AttrName);
	FTCHARToUTF8 UTF8Key(*Key);
	lua_getfield(L, -1, UTF8Key.Get());
	FString Result;
	if (lua_isstring(L, -1))
	{
		Result = UTF8_TO_TCHAR(lua_tostring(L, -1));
	}
	lua_pop(L, 2); // pop value and table
	return Result;
#else
	return FString();
#endif
}

FString FWowEventSystem::GetGlobalString(const FString& Name) const
{
#if HAS_LUA
	lua_State* L = LuaVM ? LuaVM->GetState() : nullptr;
	if (!L || Name.IsEmpty()) return FString();
	lua_getglobal(L, TCHAR_TO_UTF8(*Name));
	const FString Value = lua_type(L, -1) == LUA_TSTRING ? FString(UTF8_TO_TCHAR(lua_tostring(L, -1))) : FString();
	lua_pop(L, 1);
	return Value;
#else
	return FString();
#endif
}

int32 FWowEventSystem::GetFrameID(int64 Handle) const
{
#if HAS_LUA
	if (!LuaVM) return 0;
	lua_State* L = LuaVM->GetState();
	if (!L) return 0;

	const int32* FrameRef = FrameObjectRefs.Find(Handle);
	if (!FrameRef) return 0;

	lua_rawgeti(L, LUA_REGISTRYINDEX, *FrameRef);
	lua_getfield(L, -1, "__id");
	int32 Result = lua_isnumber(L, -1) ? static_cast<int32>(lua_tonumber(L, -1)) : 0;
	lua_pop(L, 2);
	return Result;
#else
	return 0;
#endif
}

// Helper: given "PlayerFrameHealthBar" and parent "PlayerFrame", return "healthBar"
static FString GetChildFieldKey(const FString& ChildName, const FString& ParentName)
{
	if (ParentName.IsEmpty() || ChildName.Len() <= ParentName.Len()) return FString();
	if (!ChildName.StartsWith(ParentName)) return FString();
	FString Suffix = ChildName.Mid(ParentName.Len());
	if (Suffix.IsEmpty()) return FString();
	// Lowercase first character of suffix
	Suffix[0] = FChar::ToLower(Suffix[0]);
	return Suffix;
}

static FString MakeChildGlobalName(const FString& ParentName, const FString& FieldKey)
{
	if (ParentName.IsEmpty() || FieldKey.IsEmpty())
	{
		return FString();
	}

	FString Suffix = FieldKey;
	Suffix[0] = FChar::ToUpper(Suffix[0]);
	return ParentName + Suffix;
}

static FString BuildFunctionHandlerWrapper(const FString& FunctionName)
{
	if (FunctionName.IsEmpty())
	{
		return FString();
	}

	FString EscapedName = FunctionName;
	EscapedName.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	EscapedName.ReplaceInline(TEXT("\""), TEXT("\\\""));

	// Create a simple forwarding call. The outer SetFrameScript wrapper uses
	// named params (self, elapsed, ...) or (self, event, ...). To forward ALL
	// args correctly, we use a vararg function that captures everything.
	// This is compiled by SetFrameScriptDirect which uses "..." for all params.
	return FString::Printf(
		TEXT("local fn = _G[\"%s\"]; if type(fn) == \"function\" then return fn(...) end"),
		*EscapedName);
}

static bool ShouldCompileOnLoadFunctionHandler(const FString& FunctionName)
{
	// Keep this intentionally narrow. A broad function="..." implementation
	// wakes up Blizzard bootstrap paths that currently explode our Lua heap.
	return FunctionName == TEXT("BuffFrame_OnLoad") ||
        FunctionName == TEXT("PetCastingBarFrame_OnLoad") ||
		FunctionName == TEXT("RuneFrame_OnLoad") ||
		FunctionName == TEXT("GMChatFrame_OnLoad") ||
		FunctionName == TEXT("Blizzard_CombatLog_QuickButtonFrame_OnLoad") ||
		FunctionName == TEXT("LFRBrowseFrame_OnLoad") ||
		FunctionName == TEXT("CombatFeedback_OnLoad") ||
		FunctionName == TEXT("TextStatusBar_OnLoad") ||
		FunctionName == TEXT("FadingFrame_OnLoad") ||
		// SecureHandler*Template: adds :Execute/:WrapScript/:SetFrameRef. Bartender4's bars
		// call header:WrapScript and failed with "attempt to call method 'WrapScript'".
		FunctionName == TEXT("SecureHandler_OnLoad") ||
		// r354: Blizzard_BarberShopUI (load on demand) - labels and BARBER_SHOP_SUCCESS registration.
		FunctionName == TEXT("BarberShop_OnLoad") ||
		// Blizzard_Calendar (load on demand): CalendarFrame_OnLoad creates the 42 day buttons and
		// the dropdowns/pickers set themselves up in their own OnLoad. Skipped, the window opened
		// with no days at all and OnShow failed on CalendarDayButtons[1] (Stefan 11.10).
		FunctionName.StartsWith(TEXT("Calendar"), ESearchCase::CaseSensitive);
}

#if HAS_LUA
static bool BindDirectGlobalField(lua_State* L, const FString& GlobalName, const char* FieldName)
{
	if (GlobalName.IsEmpty() || !FieldName)
	{
		return false;
	}

	lua_getglobal(L, TCHAR_TO_UTF8(*GlobalName));
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		return false;
	}

	lua_setfield(L, -2, FieldName);
	return true;
}
#endif

void FWowEventSystem::CompileFrameScripts(int64 Handle, const FWowFrameDef& Def)
{
	// Create the Lua frame object first
	CreateFrameObject(Handle, Def.Name, Def.FrameID);

#if HAS_LUA
	// r338 (OPEN_ISSUES: stock XML <Attributes> were not parsed): the frame's XML attributes are its
	// initial attributes (GetAttribute reads __attr_<name>), set before OnLoad and without
	// OnAttributeChanged, as the client creates them.
	if (LuaVM && Def.XmlAttributes.Num() > 0)
	{
		lua_State* AttrState = LuaVM->GetState();
		int32* AttrRef = AttrState ? FrameObjectRefs.Find(Handle) : nullptr;
		if (AttrRef)
		{
			lua_rawgeti(AttrState, LUA_REGISTRYINDEX, *AttrRef);
			if (lua_istable(AttrState, -1))
			{
				for (const FWowFrameDef::FXmlAttribute& Attr : Def.XmlAttributes)
				{
					if (Attr.Name.IsEmpty()) continue;
					if (Attr.Type.Equals(TEXT("nil"), ESearchCase::IgnoreCase)) { lua_pushnil(AttrState); }
					else if (Attr.Type.Equals(TEXT("number"), ESearchCase::IgnoreCase)) { lua_pushnumber(AttrState, FCString::Atod(*Attr.Value)); }
					else if (Attr.Type.Equals(TEXT("boolean"), ESearchCase::IgnoreCase))
					{
						lua_pushboolean(AttrState, Attr.Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Attr.Value == TEXT("1"));
					}
					else { lua_pushstring(AttrState, TCHAR_TO_UTF8(*Attr.Value)); }
					const FString AttrKey = TEXT("__attr_") + Attr.Name;
					lua_setfield(AttrState, -2, TCHAR_TO_UTF8(*AttrKey));
				}
			}
			lua_pop(AttrState, 1);
		}
	}
	// Set child element references on the parent frame table.
	// WoW expects frame.healthBar, frame.text, frame.icon etc. to be set
	// based on named children (ChildName minus ParentName prefix, first letter lowercased).
	if (LuaVM && !Def.Name.IsEmpty())
	{
		lua_State* L = LuaVM->GetState();
		int32* FrameRef = FrameObjectRefs.Find(Handle);
		if (L && FrameRef)
		{
			lua_rawgeti(L, LUA_REGISTRYINDEX, *FrameRef); // push frame table

			auto GetPreferredFieldKey = [&](const FString& ParentKey, const FString& ChildName) -> FString
			{
				if (!ParentKey.IsEmpty())
				{
					return ParentKey;
				}

				return GetChildFieldKey(ChildName, Def.Name);
			};

			auto BindRegionField = [&](const FString& ParentKey, const FString& RegionName, const FString& ObjectType)
			{
				const FString FieldKey = GetPreferredFieldKey(ParentKey, RegionName);
				if (FieldKey.IsEmpty())
				{
					return;
				}

				FString GlobalName = RegionName;
				if (GlobalName.IsEmpty())
				{
					GlobalName = MakeChildGlobalName(Def.Name, FieldKey);
				}
				if (GlobalName.IsEmpty())
				{
					return;
				}

				lua_getglobal(L, TCHAR_TO_UTF8(*GlobalName));
				if (lua_isnil(L, -1))
				{
					lua_pop(L, 1);
					CreateTextureRegionGlobal(GlobalName, Handle, ObjectType);
					lua_getglobal(L, TCHAR_TO_UTF8(*GlobalName));
				}

				if (!lua_isnil(L, -1))
				{
					lua_setfield(L, -2, TCHAR_TO_UTF8(*FieldKey));
				}
				else
				{
					lua_pop(L, 1);
				}
			};

				auto BindFrameField = [&](const FString& ParentKey, const FString& ChildName)
				{
					const FString FieldKey = GetPreferredFieldKey(ParentKey, ChildName);
					if (FieldKey.IsEmpty() || ChildName.IsEmpty())
					{
					return;
				}

				lua_getglobal(L, TCHAR_TO_UTF8(*ChildName));
				if (!lua_isnil(L, -1))
				{
					lua_setfield(L, -2, TCHAR_TO_UTF8(*FieldKey));
				}
				else
				{
						lua_pop(L, 1);
					}
				};

				TFunction<void(const FWowFrameDef&)> BindDescendantFields;
				BindDescendantFields = [&](const FWowFrameDef& CurrentDef)
				{
					for (const FWowLayer& CurrentLayer : CurrentDef.Layers)
					{
						for (const FWowTextureElement& CurrentTex : CurrentLayer.Textures)
						{
							BindRegionField(CurrentTex.ParentKey, CurrentTex.Name, TEXT("Texture"));
						}

						for (const FWowFontStringElement& CurrentFontString : CurrentLayer.FontStrings)
						{
							BindRegionField(CurrentFontString.ParentKey, CurrentFontString.Name, TEXT("FontString"));
						}
					}

					for (const FWowFrameDef& CurrentChild : CurrentDef.Children)
					{
						BindFrameField(CurrentChild.ParentKey, CurrentChild.Name);
						BindDescendantFields(CurrentChild);
					}
				};

				auto BindButtonTextureField = [&](const FString& TextureName, const FString& ParentKey, const TCHAR* DefaultField)
				{
					FString FieldKey = ParentKey;
					if (FieldKey.IsEmpty())
				{
					FieldKey = GetChildFieldKey(TextureName, Def.Name);
				}
				if (FieldKey.IsEmpty() && DefaultField)
				{
					FieldKey = DefaultField;
				}
				if (FieldKey.IsEmpty())
				{
					return;
				}

				FString GlobalName = TextureName;
				if (GlobalName.IsEmpty())
				{
					GlobalName = MakeChildGlobalName(Def.Name, FieldKey);
				}

				if (GlobalName.IsEmpty())
				{
					return;
				}

				lua_getglobal(L, TCHAR_TO_UTF8(*GlobalName));
				if (lua_isnil(L, -1))
				{
					lua_pop(L, 1);
					CreateTextureRegionGlobal(GlobalName, Handle, TEXT("Texture"));
					lua_getglobal(L, TCHAR_TO_UTF8(*GlobalName));
				}

				if (!lua_isnil(L, -1))
				{
					lua_setfield(L, -2, TCHAR_TO_UTF8(*FieldKey));
				}
				else
				{
					lua_pop(L, 1);
				}
			};

			bool bDebugThisFrame = Def.Name.Contains(TEXT("OptionsFrameTemplateCategoryFrameButton1")) ||
			                        Def.Name.Contains(TEXT("TargetFrame")) ||
			                        Def.Name.Contains(TEXT("PartyMemberFrame1"));
			if (bDebugThisFrame)
			{
				UE_LOG(LogWowEvent, Warning, TEXT("CHILDMAP: %s has %d layers, %d children"), *Def.Name, Def.Layers.Num(), Def.Children.Num());
				for (const FWowLayer& DL : Def.Layers) {
					for (const FWowTextureElement& DT : DL.Textures) UE_LOG(LogWowEvent, Warning, TEXT("  TEX: '%s' parentKey='%s'"), *DT.Name, *DT.ParentKey);
					for (const FWowFontStringElement& DFS : DL.FontStrings) UE_LOG(LogWowEvent, Warning, TEXT("  FS: '%s' parentKey='%s'"), *DFS.Name, *DFS.ParentKey);
				}
				for (const FWowFrameDef& DC : Def.Children) UE_LOG(LogWowEvent, Warning, TEXT("  CHILD: '%s' parentKey='%s'"), *DC.Name, *DC.ParentKey);
			}
				for (const FWowLayer& Layer : Def.Layers)
				{
					for (const FWowTextureElement& Tex : Layer.Textures)
					{
						BindRegionField(Tex.ParentKey, Tex.Name, TEXT("Texture"));
				}
				for (const FWowFontStringElement& FS : Layer.FontStrings)
				{
					BindRegionField(FS.ParentKey, FS.Name, TEXT("FontString"));
				}
			}

				// Map named child frames
				for (const FWowFrameDef& Child : Def.Children)
				{
					BindFrameField(Child.ParentKey, Child.Name);
				}

				// Named descendants can live under anonymous wrapper frames.
				// Walk the full subtree so globals like $parentName or nested regions
				// are surfaced to the owning named frame before OnLoad fires.
				for (const FWowFrameDef& Child : Def.Children)
				{
					BindDescendantFields(Child);
				}

				// Map button texture names
				BindButtonTextureField(Def.NormalTextureName, Def.NormalTextureParentKey, TEXT("normalTexture"));
				BindButtonTextureField(Def.PushedTextureName, Def.PushedTextureParentKey, TEXT("pushedTexture"));
				BindButtonTextureField(Def.HighlightTextureName, Def.HighlightTextureParentKey, TEXT("highlightTexture"));
			BindButtonTextureField(Def.DisabledTextureName, Def.DisabledTextureParentKey, TEXT("disabledTexture"));

			// NOTE: Do NOT set self.name = self:GetName() here.
			// In WoW, self.name refers to the child FontString (FrameNameName),
			// NOT the frame name string. self:GetName() returns the name via __name.

			// For buttons: auto-create child references for common WoW patterns.
			// WoW buttons have ButtonText -> creates FrameNameText fontstring
			// Also NormalTexture/PushedTexture/etc. -> creates FrameNameNormalTexture etc.
			if (!Def.Name.IsEmpty())
			{
				// Auto-create "text" field pointing to _G[frameName.."Text"] (button text fontstring)
				FString TextGlobalName = Def.Name + TEXT("Text");
				lua_getglobal(L, TCHAR_TO_UTF8(*TextGlobalName));
				if (lua_isnil(L, -1))
				{
					lua_pop(L, 1);
					// Create a stub FontString table for FrameNameText and set as global + field
					CreateTextureRegionGlobal(TextGlobalName, Handle, TEXT("FontString"));
					lua_getglobal(L, TCHAR_TO_UTF8(*TextGlobalName));
				}
				if (!lua_isnil(L, -1))
				{
					lua_setfield(L, -2, "text");
				}
				else
				{
					lua_pop(L, 1);
				}

				// Auto-create common child fields: icon, background, checkButton, normalTexture, etc.
				auto TrySetChildField = [&](const char* suffix) {
					FString GlobalName = Def.Name + UTF8_TO_TCHAR(suffix);
					lua_getglobal(L, TCHAR_TO_UTF8(*GlobalName));
					if (!lua_isnil(L, -1))
					{
						// Lowercase first char of suffix for the field key
						FString Key = UTF8_TO_TCHAR(suffix);
						Key[0] = FChar::ToLower(Key[0]);
						lua_setfield(L, -2, TCHAR_TO_UTF8(*Key));
					}
					else
					{
						lua_pop(L, 1);
					}
				};
				TrySetChildField("Icon");
				TrySetChildField("Background");
				TrySetChildField("CheckButton");
				TrySetChildField("Check");
				TrySetChildField("NormalText");
				TrySetChildField("HighlightText");
				TrySetChildField("DisabledText");
				TrySetChildField("Count");
				TrySetChildField("Name");
				TrySetChildField("HealthBar");
				TrySetChildField("ManaBar");
				TrySetChildField("Portrait");
				TrySetChildField("Texture");
				TrySetChildField("Border");
				TrySetChildField("Flash");
				TrySetChildField("Cooldown");
				TrySetChildField("HotKey");
				TrySetChildField("StatusBar");
				TrySetChildField("ScrollBar");
				TrySetChildField("ScrollBarBackground");
				TrySetChildField("OverflowButton");
				TrySetChildField("EnableButton");
				TrySetChildField("ButtonFrame");

				// For critical children that FrameXML expects but may not exist as
				// globals (e.g., dock manager overflow button), create stub tables
				// so OnLoad handlers don't crash.
				auto EnsureChildField = [&](const char* fieldName) {
					lua_getfield(L, -1, fieldName);
					if (lua_isnil(L, -1))
					{
						lua_pop(L, 1);
						// Create a stub table with metatable
						lua_newtable(L);
						luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
						if (!lua_isnil(L, -1))
							lua_setmetatable(L, -2);
						else
							lua_pop(L, 1);
						lua_setfield(L, -2, fieldName);
					}
					else
					{
						lua_pop(L, 1);
					}
				};
				EnsureChildField("overflowButton");
					EnsureChildField("buttonFrame");
					EnsureChildField("scrollBar");
					EnsureChildField("scrollBarBackground");
					EnsureChildField("scrollBarArtTop");
					EnsureChildField("scrollBarArtBottom");
				// UnitFrame-critical: name, portrait, healthbar, manabar, etc.
				EnsureChildField("name");
				// Create alias globals for FrameName+Suffix patterns.
				// WoW FrameXML code like UnitFrame_OnLoad does:
				//   self.name = _G[self:GetName().."Name"]
				// But the actual FontString might be nested deeper (e.g.,
				// Boss1TargetFrameTextureFrameName instead of Boss1TargetFrameName).
				// Create an alias or stub if the direct global doesn't exist.
					auto EnsureGlobalOrAlias = [&](const char* suffix) {
						FString DirectName = Def.Name + UTF8_TO_TCHAR(suffix);
						lua_getglobal(L, TCHAR_TO_UTF8(*DirectName));
						if (!lua_isnil(L, -1))
						{
						lua_pop(L, 1);
						return; // Direct global exists
					}
						lua_pop(L, 1);

						// Search the full descendant tree for a nested named child
						// global that ends with the suffix, e.g.
						// Boss1TargetFrameTextureFrameName for suffix "Name".
						bool bFound = false;
						TFunction<bool(const FWowFrameDef&)> TryAliasDescendant = [&](const FWowFrameDef& CurrentDef) -> bool
						{
							if (!CurrentDef.Name.IsEmpty())
							{
								FString ChildGlobal = CurrentDef.Name + UTF8_TO_TCHAR(suffix);
								lua_getglobal(L, TCHAR_TO_UTF8(*ChildGlobal));
								if (!lua_isnil(L, -1))
								{
									lua_setglobal(L, TCHAR_TO_UTF8(*DirectName));
									return true;
								}
								lua_pop(L, 1);
							}

							for (const FWowFrameDef& NestedChild : CurrentDef.Children)
							{
								if (TryAliasDescendant(NestedChild))
								{
									return true;
								}
							}

							return false;
						};

						for (const FWowFrameDef& Child : Def.Children)
						{
							if (TryAliasDescendant(Child))
							{
								bFound = true;
								break;
							}
						}

						if (!bFound)
						{
							CreateTextureRegionGlobal(DirectName);
						}
					};
					EnsureGlobalOrAlias("Name");
					EnsureGlobalOrAlias("HealthBar");
					EnsureGlobalOrAlias("ManaBar");
					EnsureGlobalOrAlias("Portrait");
					EnsureGlobalOrAlias("NumericalThreat");
					EnsureGlobalOrAlias("NormalText");
					EnsureGlobalOrAlias("HighlightText");
					EnsureGlobalOrAlias("DisabledText");
					EnsureGlobalOrAlias("Text");
					EnsureGlobalOrAlias("DropDown");

					// Alias creation happens after the initial direct child binding.
					// Re-bind the common fields now that direct globals may exist.
					TrySetChildField("Name");
					TrySetChildField("HealthBar");
					TrySetChildField("ManaBar");
					TrySetChildField("Portrait");
					TrySetChildField("NormalText");
					TrySetChildField("HighlightText");
					TrySetChildField("DisabledText");
					TrySetChildField("Text");
					TrySetChildField("DropDown");
					BindDirectGlobalField(L, Def.Name + TEXT("DropDown"), "dropdownMenu");

					EnsureChildField("portrait");
					EnsureChildField("deadText");
					EnsureChildField("unconsciousText");
					EnsureChildField("threatIndicator");
					EnsureChildField("leaderIcon");
					EnsureChildField("raidTargetIcon");
					EnsureChildField("readyCheckIcon");
					// ChatFrame/DockManager
					EnsureChildField("list");
					EnsureChildField("editBox");
					// LFD/LFR role buttons
					EnsureChildField("checkButton");
					// Dropdown menu
					EnsureChildField("normalText");
					EnsureChildField("highlightText");
					EnsureChildField("disabledText");
					// Unit popup / dropdown
					EnsureChildField("dropdownMenu");
					EnsureChildField("dropDown");
					// ScrollFrame child
					EnsureChildField("scrollFrame");
					// Class color legend
					EnsureChildField("firstClass");
					// Chat config class color legend
					EnsureChildField("header");
					// ChatFrame selected textures (FloatingChatFrame.lua)
					EnsureChildField("leftSelectedTexture");
					EnsureChildField("middleSelectedTexture");
					EnsureChildField("rightSelectedTexture");
					EnsureChildField("leftHighlightTexture");
					EnsureChildField("middleHighlightTexture");
					EnsureChildField("rightHighlightTexture");
				}

				lua_pop(L, 1); // pop frame table
			}
		}
#endif

	TArray<FString> ScriptEventOrder;
	TMap<FString, FString> FinalScripts;

	// WoW FrameXML treats later same-event handlers as overrides once templates
	// are merged into a concrete frame definition. Chaining duplicate merged
	// handlers breaks controls like ChatFrameEditBox and GMChatFrame, where the
	// child script is expected to replace the inherited template script.
	for (const FWowScriptHandler& Script : Def.Scripts)
	{
		FString ScriptCode;
		if (!Script.Code.IsEmpty())
		{
			ScriptCode = Script.Code;
		}
		else if (!Script.File.IsEmpty())
		{
			if (Script.Event == TEXT("OnLoad"))
			{
				// Narrow support for OnLoad function="..." handlers. Broader
				// OnLoad coverage currently triggers runaway Blizzard UI
				// initialization and exhausts the Lua heap.
				if (ShouldCompileOnLoadFunctionHandler(Script.File))
				{
					ScriptCode = BuildFunctionHandlerWrapper(Script.File);
				}
			}
			else
			{
				// Non-OnLoad handlers with function="..." are safe to compile
				// since they don't fire immediately during frame creation.
				ScriptCode = BuildFunctionHandlerWrapper(Script.File);
			}
		}

		if (!ScriptCode.IsEmpty())
		{
			FString* ExistingCode = FinalScripts.Find(Script.Event);
			if (!ExistingCode)
			{
				ScriptEventOrder.Add(Script.Event);
				FinalScripts.Add(Script.Event, ScriptCode);
			}
			else
			{
				*ExistingCode = ScriptCode;
			}
		}
	}

	for (const FString& ScriptEvent : ScriptEventOrder)
	{
		if (const FString* FinalCode = FinalScripts.Find(ScriptEvent))
		{
			SetFrameScript(Handle, ScriptEvent, *FinalCode);
		}
	}

	UE_LOG(LogWowEvent, Verbose, TEXT("Compiled %d scripts for frame [%lld] %s"),
		Def.Scripts.Num(), Handle, *Def.Name);

	// Fire OnLoad after compilation unless we're inside a batch that needs
	// sibling/top-level globals available before handlers run.
	TMap<FString, int32>* FrameScripts = ScriptRefs.Find(Handle);
	if (FrameScripts)
	{
		int32* OnLoadRef = FrameScripts->Find(TEXT("OnLoad"));
		if (OnLoadRef)
		{
			if (DeferredOnLoadDepth > 0)
			{
				DeferredOnLoadHandles.Add(Handle);
			}
			else
			{
				lua_State* L = LuaVM ? LuaVM->GetState() : nullptr;
				if (L)
				{
					RunOnLoadHandler(L, FrameManager, ScriptRefs, FrameObjectRefs, Handle, Def.Name);
				}
			}
		}
	}
}

void FWowEventSystem::BeginOnLoadBatch()
{
	++DeferredOnLoadDepth;
}

void FWowEventSystem::EndOnLoadBatch()
{
	if (DeferredOnLoadDepth <= 0)
	{
		return;
	}

	--DeferredOnLoadDepth;
	if (DeferredOnLoadDepth > 0)
	{
		return;
	}

#if HAS_LUA
	lua_State* L = LuaVM ? LuaVM->GetState() : nullptr;
	if (!L)
	{
		DeferredOnLoadHandles.Reset();
		return;
	}

	const TArray<int64> PendingHandles = MoveTemp(DeferredOnLoadHandles);
	DeferredOnLoadHandles.Reset();

	for (int64 Handle : PendingHandles)
	{
		const FString FrameName = FrameManager ? FrameManager->GetFrameName(Handle) : FString();
		RunOnLoadHandler(L, FrameManager, ScriptRefs, FrameObjectRefs, Handle, FrameName);
	}
#endif
}

void FWowEventSystem::TickOnUpdate(float DeltaTime)
{
#if HAS_LUA
	if (!LuaVM || !LuaVM->IsInitialized() || OnUpdateFrames.Num() == 0)
	{
		return;
	}

	lua_State* L = LuaVM->GetState();
	if (!L) return;

	LuaVM->CollectGarbageIfNeeded(0.60);

	TArray<int64> Handles = OnUpdateFrames.Array();

	for (int32 HandleIndex = 0; HandleIndex < Handles.Num(); ++HandleIndex)
	{
		const int64 Handle = Handles[HandleIndex];
		TMap<FString, int32>* FrameScripts = ScriptRefs.Find(Handle);
		if (!FrameScripts) continue;

		int32* ScriptRef = FrameScripts->Find(TEXT("OnUpdate"));
		if (!ScriptRef) continue;

		if (FrameManager && !FrameManager->IsFrameVisible(Handle))
		{
			continue;
		}

		// Push the compiled OnUpdate function
		lua_rawgeti(L, LUA_REGISTRYINDEX, *ScriptRef);
		if (!lua_isfunction(L, -1))
		{
			lua_pop(L, 1);
			continue;
		}

		// Push self (frame table)
		int32* FrameRef = FrameObjectRefs.Find(Handle);
		if (FrameRef)
		{
			lua_rawgeti(L, LUA_REGISTRYINDEX, *FrameRef);
		}
		else
		{
			lua_newtable(L);
		}

		// Push elapsed time
		lua_pushnumber(L, static_cast<double>(DeltaTime));

		// Call: func(self, elapsed)
		TArray<int32> LegacyArgIndices;
		LegacyArgIndices.Add(lua_gettop(L));
		TArray<FLuaSavedGlobal> SavedGlobals;
		SetWowHandlerGlobals(L, lua_gettop(L) - 1, 0, LegacyArgIndices, SavedGlobals);

		if (FWowLuaVM::ProtectedCall(L, 2, 0, 0) != 0)
		{
			RestoreLuaGlobals(L, SavedGlobals);
			const FString FrameName = FrameManager ? FrameManager->GetFrameName(Handle) : FString();
			const FString ErrorText = UTF8_TO_TCHAR(lua_tostring(L, -1));
			lua_pop(L, 1);

			if (ErrorText.Contains(TEXT("not enough memory")))
			{
				LuaVM->CollectGarbage();
				UE_LOG(LogWowEvent, Warning, TEXT("OnUpdate memory pressure [frame %lld] %s: %s — forced full GC, will retry next tick"),
					Handle, FrameName.IsEmpty() ? TEXT("<unnamed>") : *FrameName, *ErrorText);
				break;
			}

			UE_LOG(LogWowEvent, Warning, TEXT("OnUpdate error [frame %lld] %s: %s — disabling OnUpdate for this frame"),
				Handle, FrameName.IsEmpty() ? TEXT("<unnamed>") : *FrameName, *ErrorText);
			// Disable further OnUpdate calls for this erroring frame to prevent spam
			OnUpdateFrames.Remove(Handle);
			break; // Iterator invalidated
		}
		else
		{
			RestoreLuaGlobals(L, SavedGlobals);
		}

	}

	// One bounded incremental step per rendered frame is sufficient.  The old
	// 1024 KiB default was invoked after every 16 OnUpdate handlers and again
	// here, multiplying GC work with the number of active UI frames.
	LuaVM->StepGarbageCollector(16);
#endif
}

FString FWowEventSystem::OpcodeToEvent(uint16 Opcode)
{
	// WoW 3.3.5 (build 12340) SMSG opcodes mapped to UI event names.
	switch (Opcode)
	{
	// Character / login
	// SMSG_LOGIN_VERIFY_WORLD also arrives on every far teleport; PLAYER_LOGIN is fired
	// once by UWowUIManager::LoadUI (addons such as LibDBIcon break when it repeats).
	case 0x0236: return TEXT("");                                // SMSG_LOGIN_VERIFY_WORLD
	case 0x006E: return TEXT("PLAYER_LEAVING_WORLD");            // SMSG_LOGOUT_COMPLETE

	// Targeting
	case 0x0078: return TEXT("");

	// Unit updates
	case 0x0480: return TEXT("UNIT_MANA");                       // SMSG_POWER_UPDATE (correct opcode)
	case 0x0495: return TEXT("UNIT_AURA");                       // SMSG_AURA_UPDATE (correct opcode)
	case 0x0051: return TEXT("UNIT_NAME_UPDATE");                // SMSG_NAME_QUERY_RESPONSE (correct opcode)

	// Chat messages
	case 0x0096: return TEXT("CHAT_MSG_SAY");                    // SMSG_MESSAGECHAT (filtered by type)
	case 0x0097: return TEXT("CHAT_MSG_WHISPER");                // SMSG_MESSAGECHAT
	case 0x0098: return TEXT("CHAT_MSG_PARTY");                  // SMSG_MESSAGECHAT
	case 0x0099: return TEXT("CHAT_MSG_GUILD");                  // SMSG_MESSAGECHAT
	case 0x009A: return TEXT("CHAT_MSG_YELL");                   // SMSG_MESSAGECHAT

	// Spells and actions
	case 0x012A: return TEXT("SPELLS_CHANGED");                  // SMSG_INITIAL_SPELLS (correct opcode)
	case 0x012B: return TEXT("LEARNED_SPELL_IN_TAB");            // SMSG_LEARNED_SPELL
	case 0x0130: return TEXT("SPELL_UPDATE_COOLDOWN");           // SMSG_SPELL_COOLDOWN (correct opcode)
	case 0x0129: return TEXT("ACTIONBAR_SLOT_CHANGED");          // SMSG_ACTION_BUTTONS (correct opcode)

	// Inventory
	case 0x0134: return TEXT("BAG_UPDATE");                      // SMSG_INVENTORY_CHANGE_FAILURE / UPDATE
	case 0x0166: return TEXT("ITEM_LOCK_CHANGED");

	// Quest
	case 0x019B: return TEXT("QUEST_LOG_UPDATE");                // SMSG_QUESTLOG_FULL_UPDATE
	case 0x019C: return TEXT("QUEST_FINISHED");

	// Zone / map
	case 0x02C2: return TEXT("ZONE_CHANGED_NEW_AREA");           // SMSG_ZONE_UNDER_ATTACK / area update
	case 0x02C3: return TEXT("ZONE_CHANGED");
	case 0x02C4: return TEXT("ZONE_CHANGED_INDOORS");

	// Group / raid
	case 0x007D: return TEXT("GROUP_ROSTER_UPDATE");             // SMSG_GROUP_LIST
	case 0x0321: return TEXT("RAID_TARGET_UPDATE");              // MSG_RAID_TARGET_UPDATE
	case 0x0322: return TEXT("READY_CHECK");                     // MSG_RAID_READY_CHECK
	case 0x03AE: return TEXT("READY_CHECK_CONFIRM");             // MSG_RAID_READY_CHECK_CONFIRM
	case 0x03C6: return TEXT("READY_CHECK_FINISHED");            // MSG_RAID_READY_CHECK_FINISHED
	case 0x0286: return TEXT("RAID_GROUP_ONLY");                 // SMSG_RAID_GROUP_ONLY

	// Combat
	case 0x04D2: return TEXT("COMBAT_LOG_EVENT_UNFILTERED");     // SMSG_COMBAT_LOG_MULTIPLE

	// Error / notifications
	case 0x0060: return TEXT("UI_ERROR_MESSAGE");                // SMSG_INVENTORY_CHANGE_FAILURE (contextual)
	case 0x0061: return TEXT("UI_INFO_MESSAGE");

	// Trade / economy
	case 0x010E: return TEXT("TRADE_SHOW");
	case 0x010F: return TEXT("TRADE_CLOSED");
	case 0x0257: return TEXT("PLAYER_MONEY");

	// Guild
	case 0x008A: return TEXT("GUILD_ROSTER_UPDATE");             // SMSG_GUILD_ROSTER (correct opcode)
	case 0x0092: return TEXT("GUILD_MOTD");                      // SMSG_GUILD_EVENT

	// Loot
	case 0x0160: return TEXT("LOOT_OPENED");                     // SMSG_LOOT_RESPONSE
	case 0x0161: return TEXT("LOOT_CLOSED");                     // SMSG_LOOT_RELEASE_RESPONSE

	// Mail
	case 0x0297: return TEXT("MAIL_SHOW");                       // SMSG_SHOW_MAILBOX
	case 0x023B: return TEXT("MAIL_INBOX_UPDATE");               // SMSG_MAIL_LIST_RESULT (correct opcode)

	// Talent / skills
	case 0x012F: return TEXT("CHARACTER_POINTS_CHANGED");
	case 0x04C0: return TEXT("PLAYER_TALENT_UPDATE");            // SMSG_TALENTS_INFO (correct opcode)
	case 0x0127: return TEXT("SKILL_LINES_CHANGED");             // SMSG_SET_PROFICIENCY

	// Merchant / vendor
	case 0x019F: return TEXT("MERCHANT_SHOW");                   // SMSG_LIST_INVENTORY
	case 0x01B4: return TEXT("MERCHANT_CLOSED");

	// Friend list
	case 0x0067: return TEXT("FRIENDLIST_UPDATE");               // SMSG_FRIEND_LIST (correct opcode)

	// Battleground / PvP
	case 0x02E8: return TEXT("UPDATE_BATTLEFIELD_STATUS");       // SMSG_BATTLEFIELD_STATUS
	case 0x02EC: return TEXT("BATTLEFIELDS_SHOW");

	// Achievement
	case 0x0468: return TEXT("ACHIEVEMENT_EARNED");              // SMSG_ACHIEVEMENT_EARNED
	case 0x0436: return TEXT("CALENDAR_UPDATE_EVENT_LIST");      // SMSG_CALENDAR_SEND_CALENDAR (r240)
	case 0x0448: return TEXT("CALENDAR_UPDATE_PENDING_INVITES"); // SMSG_CALENDAR_SEND_NUM_PENDING (r240)

	// Player progression
	case 0x01D0: return TEXT("COMBAT_XP_GAIN");                  // SMSG_LOG_XPGAIN
	case 0x01D4: return TEXT("PLAYER_LEVEL_UP");                 // SMSG_LEVELUP_INFO
	case 0x01F8: return TEXT("ZONE_UNDER_ATTACK");               // SMSG_EXPLORATION_EXPERIENCE

	// Weather/Environment
	case 0x02F4: return TEXT("WEATHER_CHANGED");                 // SMSG_WEATHER
	// Note: 0x02C3 already mapped to ZONE_CHANGED above — UPDATE_WORLD_STATE fires programmatically

	// Death / resurrection events come from AWowGameplayController::UpdateDeathState.

	default:
		return FString();
	}
}
