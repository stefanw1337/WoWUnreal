#pragma once
#include "CoreMinimal.h"

/** What happened to one addon in this UI session (shown in the AddOns list). */
struct FWowAddonRuntimeStatus
{
	bool bLoaded = false;
	FString NotLoadedReason;
	int32 ErrorCount = 0;
	TArray<FString> Errors; // first few distinct messages
};

/**
 * Collects per-addon load results and Lua errors from the log while the UI runs,
 * so the AddOns list can show "Loaded", "Not loaded: ..." or "3 errors: ..." for
 * every addon without enabling them one by one. Any log line that names a file
 * under Interface\AddOns\<Addon>\ at Warning/Error level (or containing "error")
 * is counted for that addon.
 */
class WOWUI_API FWowAddonErrorLog
{
public:
	/** Clears the collected state (called before the addons load) and installs the log listener once. */
	static void Start();
	static bool Get(const FString& AddonName, FWowAddonRuntimeStatus& Out);
	/** All collected errors as text (for copying / the log). */
	static FString Describe(const FString& AddonName);
};
