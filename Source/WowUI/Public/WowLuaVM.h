#pragma once
#include "CoreMinimal.h"

struct lua_State;

class WOWUI_API FWowLuaVM
{
public:
    FWowLuaVM();
    ~FWowLuaVM();
    bool Initialize();
    /** Reset the execution budget only when entering Lua from the host, never on reentry. */
    static int ProtectedCall(lua_State* State, int Arguments, int Results, int ErrorFunction);
    void Shutdown();
    /** Save all addon SavedVariables to disk */
    void SaveAllVariables();
    bool ExecuteString(const FString& Code, const FString& ChunkName = TEXT("="));
    bool ExecuteBuffer(const TArray<uint8>& Buffer, const FString& ChunkName);
    /**
     * Run an addon file the way the 3.3.5 client does: the chunk receives
     * (addonName, addonTable) as "...", where addonTable is one private table shared
     * by every file of that addon. Chunk name is "@path" so errors and debugstack()
     * report "Interface\AddOns\X\file.lua:line" like the original client.
     */
    bool ExecuteAddonBuffer(const TArray<uint8>& Buffer, const FString& ChunkName, const FString& AddonName);
    void FireEvent(const FString& EventName, const TArray<FString>& Args = {});
    lua_State* GetState() const { return L; }
    bool IsInitialized() const { return L != nullptr; }
    /** Perform one small incremental Lua GC step, measured in KiB of allocation debt. */
    void StepGarbageCollector(int32 StepSize = 16);
    void CollectGarbage();
    bool CollectGarbageIfNeeded(double PressureThreshold = 0.75);

    /** Memory limit in bytes (default 128 MB, 0 = unlimited) */
    void SetMemoryLimit(SIZE_T LimitBytes) { MemoryLimit = LimitBytes; }
    SIZE_T GetMemoryUsage() const { return MemoryUsed; }
    SIZE_T GetMemoryLimit() const { return MemoryLimit; }

    /** Execution timeout in instructions per script call (default 10M, 0 = unlimited) */
    void SetInstructionLimit(int32 Limit) { InstructionLimit = Limit; }
    int32 GetInstructionLimit() const { return InstructionLimit; }

private:
    void RegisterWowApi();
    void SandboxGlobals();
    void InstallInstructionHook();

    lua_State* L = nullptr;

    // Memory sandbox
    SIZE_T MemoryUsed = 0;
    SIZE_T MemoryLimit = 128 * 1024 * 1024; // 128 MB default

    // Execution timeout
    int32 InstructionLimit = 100000000; // 100M instructions — WoW FrameXML OnUpdate scripts need headroom

    static void* LuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize);
};
