#pragma once
#include "CoreMinimal.h"

struct lua_State;
class FWowLuaVM;
class FWowFrameManager;
struct FWowFrameDef;

/**
 * Maps SMSG opcodes to WoW event names and dispatches events to registered frames.
 */
class WOWUI_API FWowEventSystem
{
public:
	/** Set the Lua VM and frame manager for event dispatch */
	void SetLuaVM(FWowLuaVM* InVM) { LuaVM = InVM; }
	void SetFrameManager(FWowFrameManager* InMgr) { FrameManager = InMgr; }

	/** Register a frame to receive an event */
	void RegisterEvent(int64 FrameHandle, const FString& EventName);

	/** Unregister a frame from an event */
	void UnregisterEvent(int64 FrameHandle, const FString& EventName);

	/** Unregister a frame from all events */
	void UnregisterAllEvents(int64 FrameHandle);

	/** frame:IsEventRegistered(event) */
	bool IsEventRegistered(int64 FrameHandle, const FString& EventName) const
	{
		const TSet<int64>* Handles = EventRegistrations.Find(EventName);
		return Handles && Handles->Contains(FrameHandle);
	}

	/** Fire an event to all registered frames, calling their OnEvent Lua handlers */
	void FireEvent(const FString& EventName, const TArray<FString>& Args = {});

	/** Compile a script handler for a frame and store the Lua function reference */
	void SetFrameScript(int64 Handle, const FString& ScriptName, const FString& Code);

	/** Store an already-ref'd Lua function as a script handler (for SetScript with function arg) */
	void SetFrameScriptRef(int64 Handle, const FString& ScriptName, int32 LuaRef);

	/** Create or update a Lua table representing a frame (sets it as a named global if the frame has a name) */
	void CreateFrameObject(int64 Handle, const FString& FrameName, int32 FrameID = 0);
	/** Push the canonical Lua table for a frame (the one scripts receive as self). False if none. */
	bool PushFrameObject(int64 Handle) const;
	/** r267: push a frame's current script handler (XML or SetScript), for HookScript/GetScript. False if none. */
	bool PushFrameScript(int64 Handle, const FString& ScriptName) const;

	/** Create a Lua global table for a named texture/fontstring region */
	void CreateTextureRegionGlobal(const FString& RegionName, int64 ParentHandle = -1, const FString& ObjectType = TEXT("Texture"));

	/** Alias an existing named Lua global object to another global name. */
	void AliasGlobalObject(const FString& ExistingName, const FString& AliasName);

	/** Remove all compiled scripts and the Lua frame object for a handle */
	void RemoveFrameScripts(int64 Handle);

	/** Compile all scripts from a frame definition */
	void CompileFrameScripts(int64 Handle, const FWowFrameDef& Def);

	/** Defer OnLoad execution until EndOnLoadBatch completes. */
	void BeginOnLoadBatch();
	void EndOnLoadBatch();

	/** Dispatch OnUpdate handlers on all frames that have one (call every tick) */
	void TickOnUpdate(float DeltaTime);

	/** Run a named script handler on a specific frame.
	 *  StringArgs are pushed as string arguments after self.
	 *  Returns true if the script existed and ran. */
	bool RunFrameScript(int64 Handle, const FString& ScriptName, const TArray<FString>& StringArgs = {});

	/** Run a click-style handler with a string button arg and a real Lua boolean for `down`. */
	bool RunFrameClickScript(int64 Handle, const FString& ScriptName, const FString& Button, bool bMouseDown);

	/** Execute a small native UI bridge chunk in the sandboxed FrameXML Lua VM. */
	bool ExecuteLuaString(const FString& Code, const FString& ChunkName = TEXT("=NativeUIBridge"));

	/** Read a frame's Lua attribute (__attr_<name>) as a string. Returns empty if not set. */
	FString GetFrameAttribute(int64 Handle, const FString& AttrName) const;

	/** Value of a Lua global string (GlobalStrings.lua), empty if it is not a string. */
	FString GetGlobalString(const FString& Name) const;

	/** Read a frame's __id field as an integer. Returns 0 if not set. */
	int32 GetFrameID(int64 Handle) const;

	/** Map a server opcode to a WoW event name */
	static FString OpcodeToEvent(uint16 Opcode);

private:
	FWowLuaVM* LuaVM = nullptr;
	FWowFrameManager* FrameManager = nullptr;

	/** Event name -> set of registered frame handles */
	TMap<FString, TSet<int64>> EventRegistrations;

	/** Compiled script Lua references: Handle -> {ScriptName -> LuaRef} */
	TMap<int64, TMap<FString, int32>> ScriptRefs;

	/** Lua frame object references: Handle -> LuaRef */
	TMap<int64, int32> FrameObjectRefs;

	/** Frames that have an OnUpdate script handler */
	TSet<int64> OnUpdateFrames;

	int32 DeferredOnLoadDepth = 0;
	TArray<int64> DeferredOnLoadHandles;
};
