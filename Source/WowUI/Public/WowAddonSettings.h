#pragma once

#include "CoreMinimal.h"

class FMpqManager;

/** One user addon as shown in the AddOns list. */
struct FWowAddonListEntry
{
	FString Name;
	FString Title;      // colour codes stripped
	FString Notes;
	FString Version;
	TArray<FString> RequiredDeps;
	int32 Interface = 0; // "## Interface:" of the .toc (30300 for 3.3.5)
	bool bLoadOnDemand = false;
	bool bEnabled = true;
};

/**
 * Enabled/disabled state of addons, like the client's WTF AddOns.txt
 * ("Name: enabled|disabled", unknown addons are enabled).
 * Stored client-wide in Saved/WTF/AddOns.txt so it can be edited from the
 * character select screen before a character is chosen.
 */
class WOWUI_API FWowAddonSettings
{
public:
	static bool IsEnabled(const FString& AddonName);
	static void SetEnabled(const FString& AddonName, bool bEnabled);
	static void Save();
	/** Re-read the file (drops unsaved changes). */
	static void Reload();

	/** Addons in the client's Interface/AddOns folders, excluding Blizzard_* (same as the original AddOns list). */
	static TArray<FWowAddonListEntry> GetUserAddons(FMpqManager* Mpq);

	/** MPQ manager used by the AddOns list when no UI is loaded yet (character select). */
	static void SetMpq(FMpqManager* Mpq);
	static FMpqManager* GetMpq();

	/** "Load out of date AddOns" (stored in AddOns.txt; on by default because most
	 *  private-server addons still carry an older ## Interface number). */
	static bool GetLoadOutOfDate() { return IsEnabled(TEXT("__LoadOutOfDate")); }
	static void SetLoadOutOfDate(bool bLoad) { SetEnabled(TEXT("__LoadOutOfDate"), bLoad); }
	/** The client's interface version; a lower ## Interface makes an addon "out of date". */
	static constexpr int32 ClientInterface = 30300;
	static bool IsOutOfDate(int32 TocInterface) { return TocInterface > 0 && TocInterface < ClientInterface; }

	/** "|cff1784d1E|r" -> "E". */
	static FString StripColorCodes(const FString& Text);

private:
	static void EnsureLoaded();
	static FString GetFilePath();
};
