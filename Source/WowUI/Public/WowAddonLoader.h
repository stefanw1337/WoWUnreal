#pragma once
#include "CoreMinimal.h"

class FMpqManager;
class FWowLuaVM;
class FWowFrameXmlParser;
class FWowFrameManager;
class FWowEventSystem;

/** TOC file metadata */
struct FWowTocData
{
	FString Title;
	FString Notes;
	FString Author;
	FString Version;
	int32 Interface = 0;
	TArray<FString> RequiredDeps;
	TArray<FString> OptionalDeps;
	TArray<FString> SavedVariables;
	TArray<FString> SavedVariablesPerCharacter;
	TArray<FString> Files; // .lua and .xml files in load order
	bool bLoadOnDemand = false;
	bool bDisabled = false;
	/** Every "## Key: Value" line, key lower-cased. */
	TMap<FString, FString> Metadata;
};

class WOWUI_API FWowAddonLoader
{
public:
	/** Parse a .toc file */
	static FWowTocData ParseToc(const TArray<uint8>& Data);

	/** Discover addons in Interface/AddOns/ (filesystem + well-known Blizzard addons in MPQ) */
	static TArray<FString> DiscoverAddons(FMpqManager* Mpq);

	/** r364: an addon's .toc bytes for the AddOns list, discovery and load order, read once per
	 *  session (and again after an AddOns folder gains or loses an addon). Loading an addon still
	 *  reads its .toc fresh. */
	static bool ReadTocCached(FMpqManager* Mpq, const FString& AddonName, TArray<uint8>& OutData);

	/** Resolve dependency-ordered load list from discovered addons */
	static TArray<FString> ResolveLoadOrder(FMpqManager* Mpq, const TArray<FString>& AddonNames);

	/** Load an addon's files (XML and Lua), creating frames via FrameManager */
	static bool LoadAddon(const FString& AddonName, FMpqManager* Mpq, FWowLuaVM* LuaVM,
		FWowFrameManager* FrameManager = nullptr, FWowEventSystem* EventSystem = nullptr);

	/** Discover, resolve dependencies, and load all addons in order */
	static void LoadAllAddons(FMpqManager* Mpq, FWowLuaVM* LuaVM,
		FWowFrameManager* FrameManager = nullptr, FWowEventSystem* EventSystem = nullptr);
};
