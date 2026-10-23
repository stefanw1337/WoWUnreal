#pragma once
#include "CoreMinimal.h"

struct lua_State;
struct FWowTocData;

/**
 * Manages WoW SavedVariables persistence.
 * Serializes/deserializes Lua globals to disk in WoW's standard Lua table format.
 * Storage: Saved/WTF/Account/{Account}/SavedVariables/{AddonName}.lua
 * Per-character: Saved/WTF/Account/{Account}/{Server}/{Character}/SavedVariables/{AddonName}.lua
 */
class WOWUI_API FWowSavedVariables
{
public:
	/** Set account/server/character for path resolution */
	static void SetIdentity(const FString& Account, const FString& Server = TEXT(""), const FString& Character = TEXT(""));

	/** Load saved variables for an addon before its code runs */
	static void LoadForAddon(lua_State* L, const FString& AddonName, const FWowTocData& Toc);

	/** Save variables for an addon (called on shutdown/logout) */
	static void SaveForAddon(lua_State* L, const FString& AddonName, const FWowTocData& Toc);

	/** Save all registered addon variables */
	static void SaveAll(lua_State* L);

	/** Register an addon's TOC so we can save on shutdown */
	static void RegisterAddon(const FString& AddonName, const FWowTocData& Toc);

	/** Saved/WTF/Account/{Account} and .../{Server}/{Character} (macros-cache.txt lives there, like the client).
	    Where this session WRITES them (see EClientWtfMode). */
	static FString GetAccountDirectory();
	static FString GetCharacterDirectory();

	/**
	 * Use of the WoW client's own WTF folder (the one beside its Data folder), chosen per realm:
	 * - Own: only the client's own Saved/WTF (the WoW client's files are never read).
	 * - FreshEachLaunch: read the WoW client's files every session; changes go to Saved/WTF-Session,
	 *   which is emptied at the next launch. The WoW client's files are never written.
	 * - CopyOnce: read our own files, and the WoW client's for anything we do not have yet; write ours.
	 * - Shared: read and write the WoW client's files. The whole folder is backed up at every launch
	 *   (Saved/WTF-Backups, newest 5), and a file another client saved since we read it is not
	 *   overwritten (ours goes next to it as <file>.wowunreal).
	 */
	enum class EClientWtfMode : int32 { Own = 0, FreshEachLaunch = 1, CopyOnce = 2, Shared = 3 };
	static void ConfigureClientWtf(const FString& ClientWtfDirectory, EClientWtfMode Mode);
	static EClientWtfMode GetClientWtfMode() { return ClientMode; }
	static const FString& GetClientWtfDirectory() { return ClientWtfDir; }
	/** True once SetIdentity was given a real account (not the offline defaults). */
	static bool HasIdentity() { return bHasIdentity; }

	/** "Account/<A>" and "Account/<A>/<Realm>/<Character>", relative to a WTF folder. */
	static FString GetAccountRelative();
	static FString GetCharacterRelative();
	/** The copy of a WTF file to read in this mode (first existing one), or "" when there is none. */
	static FString ResolveReadPath(const FString& RelativePath);
	/** Where this mode writes that WTF file. */
	static FString GetWritePath(const FString& RelativePath);
	/** Shared mode bookkeeping: remember when a file was read / written, and refuse to overwrite a
	    file that changed on disk since (another client saved it). Always true outside Shared mode. */
	static void NoteRead(const FString& AbsolutePath);
	static void NoteWritten(const FString& AbsolutePath);
	static bool CanOverwrite(const FString& AbsolutePath);
	/** Save text to a WTF file through the Shared-mode check (ours goes to <file>.wowunreal when refused). */
	static bool SaveWtfFile(const FString& AbsolutePath, const FString& Text);

private:
	/** Serialize a Lua value at the given stack index to a string */
	static void SerializeLuaValue(lua_State* L, int Idx, FString& Out, int Indent);

	/** Write indentation */
	static void AppendIndent(FString& Out, int Indent);

	/** Get the base path for account-wide saved variables */
	static FString GetAccountSVPath();

	/** Get the base path for per-character saved variables */
	static FString GetCharacterSVPath();

	static FString AccountName;
	static FString ServerName;
	static FString CharacterName;
	static bool bHasIdentity;
	static FString ClientWtfDir;
	static EClientWtfMode ClientMode;
	static TMap<FString, FDateTime> FileStamps;

	/** Registered addon TOCs for SaveAll */
	static TMap<FString, FWowTocData> RegisteredAddons;
};
