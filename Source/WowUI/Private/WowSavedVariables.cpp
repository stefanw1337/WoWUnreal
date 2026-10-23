#include "WowSavedVariables.h"
#include "WowAddonLoader.h"
#include "WowLuaVM.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"

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

DEFINE_LOG_CATEGORY_STATIC(LogWowSV, Log, All);

FString FWowSavedVariables::AccountName = TEXT("WowTestUser");
FString FWowSavedVariables::ServerName = TEXT("WowUnreal");
FString FWowSavedVariables::CharacterName = TEXT("Player");
bool FWowSavedVariables::bHasIdentity = false;
FString FWowSavedVariables::ClientWtfDir;
FWowSavedVariables::EClientWtfMode FWowSavedVariables::ClientMode = FWowSavedVariables::EClientWtfMode::Own;
TMap<FString, FDateTime> FWowSavedVariables::FileStamps;
TMap<FString, FWowTocData> FWowSavedVariables::RegisteredAddons;

void FWowSavedVariables::SetIdentity(const FString& Account, const FString& Server, const FString& Character)
{
	AccountName = Account.IsEmpty() ? TEXT("WowTestUser") : Account;
	ServerName = Server.IsEmpty() ? TEXT("WowUnreal") : Server;
	CharacterName = Character.IsEmpty() ? TEXT("Player") : Character;
	bHasIdentity = !Account.IsEmpty();
}

namespace
{
	FString OwnWtfRoot() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WTF")); }
	FString SessionWtfRoot() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WTF-Session")); }
	FString BackupWtfRoot() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WTF-Backups")); }
}

void FWowSavedVariables::ConfigureClientWtf(const FString& ClientWtfDirectory, EClientWtfMode Mode)
{
	ClientWtfDir = ClientWtfDirectory;
	if (ClientWtfDir.IsEmpty() || !FPaths::DirectoryExists(ClientWtfDir)) { ClientWtfDir.Reset(); Mode = EClientWtfMode::Own; }
	ClientMode = Mode;
	static bool bSessionCleared = false;
	if (Mode == EClientWtfMode::FreshEachLaunch && !bSessionCleared)
	{
		// The previous launch's changes are thrown away: every launch starts from the WoW client's files.
		bSessionCleared = true;
		IFileManager::Get().DeleteDirectory(*SessionWtfRoot(), false, true);
	}
	static bool bBackedUp = false;
	if (Mode == EClientWtfMode::Shared && !bBackedUp)
	{
		bBackedUp = true;
		const FString Backup = FPaths::Combine(BackupWtfRoot(), FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
		IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
		Files.CreateDirectoryTree(*Backup);
		const bool bCopied = Files.CopyDirectoryTree(*Backup, *ClientWtfDir, true);
		UE_LOG(LogWowSV, Log, TEXT("Shared WTF: backup of %s -> %s (%s)"), *ClientWtfDir, *Backup, bCopied ? TEXT("ok") : TEXT("FAILED"));
		TArray<FString> Backups;
		IFileManager::Get().FindFiles(Backups, *FPaths::Combine(BackupWtfRoot(), TEXT("*")), false, true);
		Backups.Sort();
		for (int32 Index = 0; Index + 5 < Backups.Num(); ++Index)
			IFileManager::Get().DeleteDirectory(*FPaths::Combine(BackupWtfRoot(), Backups[Index]), false, true);
	}
	UE_LOG(LogWowSV, Log, TEXT("WTF mode %d, WoW client WTF: %s"), int32(ClientMode), ClientWtfDir.IsEmpty() ? TEXT("(none)") : *ClientWtfDir);
}

FString FWowSavedVariables::GetAccountRelative()
{
	return FPaths::Combine(TEXT("Account"), AccountName);
}

FString FWowSavedVariables::GetCharacterRelative()
{
	return FPaths::Combine(TEXT("Account"), AccountName, ServerName, CharacterName);
}

FString FWowSavedVariables::GetWritePath(const FString& RelativePath)
{
	const FString Root = ClientMode == EClientWtfMode::FreshEachLaunch ? SessionWtfRoot()
		: ClientMode == EClientWtfMode::Shared ? ClientWtfDir : OwnWtfRoot();
	return FPaths::Combine(Root, RelativePath);
}

FString FWowSavedVariables::ResolveReadPath(const FString& RelativePath)
{
	TArray<FString, TInlineAllocator<2>> Roots;
	switch (ClientMode)
	{
	case EClientWtfMode::FreshEachLaunch: Roots = { SessionWtfRoot(), ClientWtfDir }; break;
	case EClientWtfMode::CopyOnce: Roots = { OwnWtfRoot(), ClientWtfDir }; break;
	case EClientWtfMode::Shared: Roots = { ClientWtfDir }; break;
	default: Roots = { OwnWtfRoot() }; break;
	}
	for (const FString& Root : Roots)
	{
		const FString Candidate = FPaths::Combine(Root, RelativePath);
		if (!Root.IsEmpty() && FPaths::FileExists(Candidate)) return Candidate;
	}
	return FString();
}

void FWowSavedVariables::NoteRead(const FString& AbsolutePath)
{
	if (ClientMode == EClientWtfMode::Shared && FPaths::FileExists(AbsolutePath))
		FileStamps.Add(FPaths::ConvertRelativePathToFull(AbsolutePath), IFileManager::Get().GetTimeStamp(*AbsolutePath));
}

void FWowSavedVariables::NoteWritten(const FString& AbsolutePath)
{
	NoteRead(AbsolutePath);
}

bool FWowSavedVariables::CanOverwrite(const FString& AbsolutePath)
{
	if (ClientMode != EClientWtfMode::Shared || !FPaths::FileExists(AbsolutePath)) return true;
	const FDateTime* Seen = FileStamps.Find(FPaths::ConvertRelativePathToFull(AbsolutePath));
	// Created by the other client after we looked, or saved by it since we read it.
	return Seen && IFileManager::Get().GetTimeStamp(*AbsolutePath) <= *Seen + FTimespan::FromSeconds(2.0);
}

bool FWowSavedVariables::SaveWtfFile(const FString& AbsolutePath, const FString& Text)
{
	FString Target = AbsolutePath;
	if (!CanOverwrite(AbsolutePath))
	{
		Target = AbsolutePath + TEXT(".wowunreal");
		UE_LOG(LogWowSV, Warning, TEXT("Shared WTF: %s was saved by another client since we read it - kept it, ours is %s"), *AbsolutePath, *Target);
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Target), true);
	const bool bSaved = FFileHelper::SaveStringToFile(Text, *Target, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (bSaved && Target == AbsolutePath) NoteWritten(AbsolutePath);
	return bSaved;
}

FString FWowSavedVariables::GetAccountDirectory()
{
	return FPaths::GetPath(GetWritePath(FPaths::Combine(GetAccountRelative(), TEXT("x"))));
}

FString FWowSavedVariables::GetCharacterDirectory()
{
	return FPaths::GetPath(GetWritePath(FPaths::Combine(GetCharacterRelative(), TEXT("x"))));
}

FString FWowSavedVariables::GetAccountSVPath()
{
	return FPaths::Combine(GetAccountDirectory(), TEXT("SavedVariables"));
}

FString FWowSavedVariables::GetCharacterSVPath()
{
	return FPaths::Combine(GetCharacterDirectory(), TEXT("SavedVariables"));
}

void FWowSavedVariables::RegisterAddon(const FString& AddonName, const FWowTocData& Toc)
{
	if (Toc.SavedVariables.Num() > 0 || Toc.SavedVariablesPerCharacter.Num() > 0)
	{
		RegisteredAddons.Add(AddonName, Toc);
	}
}

#if HAS_LUA
namespace
{
	// r287 (Stefan 13.10: a new character should get the addon setup the others already have):
	// AceDB databases (Bartender4DB and most Ace addons) keep profileKeys["Name - Realm"] -> profile.
	// A character without a key would start on its own empty default profile; it gets the profile most
	// of the other characters use instead (a per-character profile is copied, a shared one is used),
	// in the database and in each of its namespaces. r287b: also when its key points at its own
	// profile and that profile is still empty (AceDB creates the key on the first login).
	const char* GWowAceDbSeedLua = R"LUA(
local name, me = ...
local function copy(t)
    if type(t) ~= "table" then return t end
    local r = {}
    for k, v in pairs(t) do r[k] = copy(v) end
    return r
end
local function seed(db)
    if type(db) ~= "table" or type(db.profileKeys) ~= "table" or type(db.profiles) ~= "table" then return nil end
    -- r287b: AceDB also gives a new character a key to its own profile that holds nothing yet
    -- (defaults are not saved), so an untouched own profile counts as none.
    -- r287c: how much a profile holds, in the database and in its namespaces (Bartender4 keeps the
    -- bars in namespaces; its main profiles are empty), so a set-up character is never overwritten.
    local function weight(profile)
        local n = 0
        if type(db.profiles[profile]) == "table" and next(db.profiles[profile]) ~= nil then n = n + 1 end
        if type(db.namespaces) == "table" then
            for _, ns in pairs(db.namespaces) do
                if type(ns) == "table" and type(ns.profiles) == "table" and type(ns.profiles[profile]) == "table"
                    and next(ns.profiles[profile]) ~= nil then n = n + 1 end
            end
        end
        return n
    end
    local own = db.profileKeys[me]
    if own ~= nil and (own ~= me or weight(own) > 0) then return nil end
    local count, best, bestCount, bestWeight = {}, nil, 0, 0
    for key, profile in pairs(db.profileKeys) do
        if key ~= me and profile ~= me and type(profile) == "string" then
            count[profile] = (count[profile] or 0) + 1
        end
    end
    for profile, n in pairs(count) do
        local w = weight(profile)
        if w > 0 and (n > bestCount or (n == bestCount and (w > bestWeight or (w == bestWeight and profile < best)))) then
            best, bestCount, bestWeight = profile, n, w
        end
    end
    if not best then return nil end
    if string.find(best, " - ", 1, true) then
        db.profiles[me] = copy(db.profiles[best])
        db.profileKeys[me] = me
    else
        db.profileKeys[me] = best
    end
    if type(db.namespaces) == "table" then
        for _, ns in pairs(db.namespaces) do
            -- AceDB namespaces use the database's profileKeys; their profiles are per profile name.
            if type(ns) == "table" and type(ns.profiles) == "table" and db.profileKeys[me] == me
                and ns.profiles[best] ~= nil and (type(ns.profiles[me]) ~= "table" or next(ns.profiles[me]) == nil) then
                ns.profiles[me] = copy(ns.profiles[best])
            end
            if type(ns) == "table" and type(ns.profileKeys) == "table" then ns.profileKeys[me] = db.profileKeys[me] end
        end
    end
    return best
end
return seed(_G[name])
)LUA";

	void SeedAceDbProfile(lua_State* L, const FString& AddonName, const FString& Variable, const FString& CharacterKey)
	{
		if (luaL_loadbuffer(L, GWowAceDbSeedLua, FCStringAnsi::Strlen(GWowAceDbSeedLua), "=AceDbSeed") != 0) { lua_pop(L, 1); return; }
		lua_pushstring(L, TCHAR_TO_UTF8(*Variable));
		lua_pushstring(L, TCHAR_TO_UTF8(*CharacterKey));
		if (lua_pcall(L, 2, 1, 0) != 0) { lua_pop(L, 1); return; }
		if (lua_isstring(L, -1))
			UE_LOG(LogWowSV, Log, TEXT("%s: %s had no profile in %s - using the other characters' profile \"%s\""),
				*AddonName, *CharacterKey, *Variable, UTF8_TO_TCHAR(lua_tostring(L, -1)));
		lua_pop(L, 1);
	}
}
#endif

// r287: the newest copy of this addon's per-character file among the account's other characters
// (session, then the client's WTF), so a new character starts with the same settings.
static FString FindOtherCharacterSV(const FString& AccountRelative, const FString& AddonName, const TArray<FString>& Roots)
{
	FString Best; FDateTime BestTime = FDateTime::MinValue();
	for (const FString& Root : Roots)
	{
		if (Root.IsEmpty()) continue;
		const FString AccountDir = FPaths::Combine(Root, AccountRelative);
		TArray<FString> Realms;
		IFileManager::Get().FindFiles(Realms, *FPaths::Combine(AccountDir, TEXT("*")), false, true);
		for (const FString& Realm : Realms)
		{
			if (Realm.Equals(TEXT("SavedVariables"), ESearchCase::IgnoreCase)) continue;
			TArray<FString> Characters;
			IFileManager::Get().FindFiles(Characters, *FPaths::Combine(AccountDir, Realm, TEXT("*")), false, true);
			for (const FString& Character : Characters)
			{
				const FString Candidate = FPaths::Combine(AccountDir, Realm, Character, TEXT("SavedVariables"), AddonName + TEXT(".lua"));
				if (!FPaths::FileExists(Candidate)) continue;
				const FDateTime Time = IFileManager::Get().GetTimeStamp(*Candidate);
				if (Time > BestTime) { BestTime = Time; Best = Candidate; }
			}
		}
		if (!Best.IsEmpty()) break; // the session copy is the newest state
	}
	return Best;
}

void FWowSavedVariables::LoadForAddon(lua_State* L, const FString& AddonName, const FWowTocData& Toc)
{
#if HAS_LUA
	if (!L) return;

	// Load account-wide saved variables
	if (Toc.SavedVariables.Num() > 0)
	{
		const FString FilePath = ResolveReadPath(FPaths::Combine(GetAccountRelative(), TEXT("SavedVariables"), AddonName + TEXT(".lua")));
		NoteRead(GetWritePath(FPaths::Combine(GetAccountRelative(), TEXT("SavedVariables"), AddonName + TEXT(".lua"))));
		if (!FilePath.IsEmpty())
		{
			TArray<uint8> Data;
			if (FFileHelper::LoadFileToArray(Data, *FilePath))
			{
				FTCHARToUTF8 ChunkName(*FString::Printf(TEXT("@SavedVariables/%s"), *AddonName));
				if (luaL_loadbuffer(L, (const char*)Data.GetData(), Data.Num(), ChunkName.Get()) == 0)
				{
					if (lua_pcall(L, 0, 0, 0) == 0)
					{
						UE_LOG(LogWowSV, Log, TEXT("Loaded SavedVariables for %s"), *AddonName);
					}
					else
					{
						UE_LOG(LogWowSV, Warning, TEXT("Error executing SavedVariables for %s: %s"),
							*AddonName, UTF8_TO_TCHAR(lua_tostring(L, -1)));
						lua_pop(L, 1);
					}
				}
				else
				{
					UE_LOG(LogWowSV, Warning, TEXT("Error parsing SavedVariables for %s: %s"),
						*AddonName, UTF8_TO_TCHAR(lua_tostring(L, -1)));
					lua_pop(L, 1);
				}
			}
		}
	}

	// r287: AceDB profiles for a character that has none yet.
	if (bHasIdentity)
		for (const FString& Variable : Toc.SavedVariables)
			SeedAceDbProfile(L, AddonName, Variable.TrimStartAndEnd(), CharacterName + TEXT(" - ") + ServerName);

	// Load per-character saved variables
	if (Toc.SavedVariablesPerCharacter.Num() > 0)
	{
		FString FilePath = ResolveReadPath(FPaths::Combine(GetCharacterRelative(), TEXT("SavedVariables"), AddonName + TEXT(".lua")));
		if (FilePath.IsEmpty() && bHasIdentity)
		{
			// r287: a character that never ran this addon starts from another character's file
			// (saved as its own file on logout).
			TArray<FString> Roots;
			switch (ClientMode)
			{
			case EClientWtfMode::FreshEachLaunch: Roots = { SessionWtfRoot(), ClientWtfDir }; break;
			case EClientWtfMode::CopyOnce: Roots = { OwnWtfRoot(), ClientWtfDir }; break;
			case EClientWtfMode::Shared: Roots = { ClientWtfDir }; break;
			default: Roots = { OwnWtfRoot() }; break;
			}
			FilePath = FindOtherCharacterSV(GetAccountRelative(), AddonName, Roots);
			if (!FilePath.IsEmpty())
				UE_LOG(LogWowSV, Log, TEXT("%s: no per-character settings for %s yet - starting from %s"), *AddonName, *CharacterName, *FilePath);
		}
		NoteRead(GetWritePath(FPaths::Combine(GetCharacterRelative(), TEXT("SavedVariables"), AddonName + TEXT(".lua"))));
		if (!FilePath.IsEmpty())
		{
			TArray<uint8> Data;
			if (FFileHelper::LoadFileToArray(Data, *FilePath))
			{
				FTCHARToUTF8 ChunkName(*FString::Printf(TEXT("@SavedVariablesPerCharacter/%s"), *AddonName));
				if (luaL_loadbuffer(L, (const char*)Data.GetData(), Data.Num(), ChunkName.Get()) == 0)
				{
					if (lua_pcall(L, 0, 0, 0) == 0)
					{
						UE_LOG(LogWowSV, Log, TEXT("Loaded SavedVariablesPerCharacter for %s"), *AddonName);
					}
					else
					{
						UE_LOG(LogWowSV, Warning, TEXT("Error executing SavedVariablesPerCharacter for %s: %s"),
							*AddonName, UTF8_TO_TCHAR(lua_tostring(L, -1)));
						lua_pop(L, 1);
					}
				}
				else
				{
					UE_LOG(LogWowSV, Warning, TEXT("Error parsing SavedVariablesPerCharacter for %s: %s"),
						*AddonName, UTF8_TO_TCHAR(lua_tostring(L, -1)));
					lua_pop(L, 1);
				}
			}
		}
	}
#endif
}

void FWowSavedVariables::SaveForAddon(lua_State* L, const FString& AddonName, const FWowTocData& Toc)
{
#if HAS_LUA
	if (!L) return;

	// Save account-wide variables
	if (Toc.SavedVariables.Num() > 0)
	{
		FString Output;
		for (const FString& VarName : Toc.SavedVariables)
		{
			FTCHARToUTF8 VarNameUtf8(*VarName);
			lua_getglobal(L, VarNameUtf8.Get());
			if (!lua_isnil(L, -1))
			{
				Output += VarName + TEXT(" = ");
				SerializeLuaValue(L, lua_gettop(L), Output, 0);
				Output += TEXT("\n");
			}
			lua_pop(L, 1);
		}

		if (!Output.IsEmpty())
		{
			SaveWtfFile(FPaths::Combine(GetAccountSVPath(), AddonName + TEXT(".lua")), Output);
			UE_LOG(LogWowSV, Log, TEXT("Saved %d SavedVariables for %s"), Toc.SavedVariables.Num(), *AddonName);
		}
	}

	// Save per-character variables
	if (Toc.SavedVariablesPerCharacter.Num() > 0)
	{
		FString Output;
		for (const FString& VarName : Toc.SavedVariablesPerCharacter)
		{
			FTCHARToUTF8 VarNameUtf8(*VarName);
			lua_getglobal(L, VarNameUtf8.Get());
			if (!lua_isnil(L, -1))
			{
				Output += VarName + TEXT(" = ");
				SerializeLuaValue(L, lua_gettop(L), Output, 0);
				Output += TEXT("\n");
			}
			lua_pop(L, 1);
		}

		if (!Output.IsEmpty())
		{
			SaveWtfFile(FPaths::Combine(GetCharacterSVPath(), AddonName + TEXT(".lua")), Output);
			UE_LOG(LogWowSV, Log, TEXT("Saved %d SavedVariablesPerCharacter for %s"),
				Toc.SavedVariablesPerCharacter.Num(), *AddonName);
		}
	}
#endif
}

void FWowSavedVariables::SaveAll(lua_State* L)
{
#if HAS_LUA
	for (const auto& Pair : RegisteredAddons)
	{
		SaveForAddon(L, Pair.Key, Pair.Value);
	}
	UE_LOG(LogWowSV, Log, TEXT("SaveAll: saved variables for %d addons"), RegisteredAddons.Num());
#endif
}

void FWowSavedVariables::AppendIndent(FString& Out, int Indent)
{
	for (int i = 0; i < Indent; i++)
	{
		Out += TEXT("\t");
	}
}

void FWowSavedVariables::SerializeLuaValue(lua_State* L, int Idx, FString& Out, int Indent)
{
#if HAS_LUA
	// Prevent excessive recursion
	if (Indent > 20)
	{
		Out += TEXT("nil -- max depth exceeded");
		return;
	}

	int Type = lua_type(L, Idx);
	switch (Type)
	{
	case LUA_TNIL:
		Out += TEXT("nil");
		break;

	case LUA_TBOOLEAN:
		Out += lua_toboolean(L, Idx) ? TEXT("true") : TEXT("false");
		break;

	case LUA_TNUMBER:
	{
		double Val = lua_tonumber(L, Idx);
		// Use integer format if it's a whole number
		if (Val == FMath::FloorToDouble(Val) && FMath::Abs(Val) < 1e15)
		{
			Out += FString::Printf(TEXT("%lld"), (int64)Val);
		}
		else
		{
			Out += FString::Printf(TEXT("%.17g"), Val);
		}
		break;
	}

	case LUA_TSTRING:
	{
		const char* Str = lua_tostring(L, Idx);
		Out += TEXT("\"");
		// Escape special characters
		for (const char* p = Str; *p; p++)
		{
			switch (*p)
			{
			case '\\': Out += TEXT("\\\\"); break;
			case '"':  Out += TEXT("\\\""); break;
			case '\n': Out += TEXT("\\n"); break;
			case '\r': Out += TEXT("\\r"); break;
			case '\t': Out += TEXT("\\t"); break;
			case '\0': Out += TEXT("\\0"); break;
			default:
				if ((uint8)*p < 32)
				{
					Out += FString::Printf(TEXT("\\%d"), (int)(uint8)*p);
				}
				else
				{
					Out += (TCHAR)*p;
				}
				break;
			}
		}
		Out += TEXT("\"");
		break;
	}

	case LUA_TTABLE:
	{
		Out += TEXT("{\n");
		int NewIndent = Indent + 1;

		// Check if table is array-like (sequential integer keys starting at 1)
		int ArrayLen = 0;
#if LUA_VERSION_NUM >= 502
		ArrayLen = (int)lua_rawlen(L, Idx);
#else
		ArrayLen = (int)lua_objlen(L, Idx);
#endif

		// Serialize array part
		for (int i = 1; i <= ArrayLen; i++)
		{
			lua_rawgeti(L, Idx, i);
			if (lua_isnil(L, -1))
			{
				lua_pop(L, 1);
				break;
			}
			AppendIndent(Out, NewIndent);
			SerializeLuaValue(L, lua_gettop(L), Out, NewIndent);
			Out += TEXT(", -- [");
			Out += FString::FromInt(i);
			Out += TEXT("]\n");
			lua_pop(L, 1);
		}

		// Serialize hash part (non-integer or out-of-range keys)
		lua_pushnil(L);
		while (lua_next(L, Idx) != 0)
		{
			// Skip array keys we already serialized
			if (lua_type(L, -2) == LUA_TNUMBER)
			{
				double Key = lua_tonumber(L, -2);
				if (Key == FMath::FloorToDouble(Key) && Key >= 1 && Key <= ArrayLen)
				{
					lua_pop(L, 1);
					continue;
				}
			}

			AppendIndent(Out, NewIndent);

			// Key
			if (lua_type(L, -2) == LUA_TSTRING)
			{
				const char* KeyStr = lua_tostring(L, -2);
				// Check if key is a valid Lua identifier
				bool bValidIdent = true;
				if (KeyStr[0] == '\0' || (KeyStr[0] != '_' && !FChar::IsAlpha(KeyStr[0])))
					bValidIdent = false;
				for (const char* p = KeyStr + 1; *p && bValidIdent; p++)
				{
					if (*p != '_' && !FChar::IsAlnum(*p))
						bValidIdent = false;
				}

				if (bValidIdent)
				{
					Out += TEXT("[\"");
					Out += UTF8_TO_TCHAR(KeyStr);
					Out += TEXT("\"]");
				}
				else
				{
					Out += TEXT("[\"");
					// Escape the key string
					for (const char* p = KeyStr; *p; p++)
					{
						switch (*p)
						{
						case '\\': Out += TEXT("\\\\"); break;
						case '"':  Out += TEXT("\\\""); break;
						case '\n': Out += TEXT("\\n"); break;
						default: Out += (TCHAR)*p; break;
						}
					}
					Out += TEXT("\"]");
				}
			}
			else if (lua_type(L, -2) == LUA_TNUMBER)
			{
				Out += TEXT("[");
				double Key = lua_tonumber(L, -2);
				if (Key == FMath::FloorToDouble(Key))
					Out += FString::Printf(TEXT("%lld"), (int64)Key);
				else
					Out += FString::Printf(TEXT("%.17g"), Key);
				Out += TEXT("]");
			}
			else
			{
				// Skip non-string/number keys (can't be serialized)
				lua_pop(L, 1);
				continue;
			}

			Out += TEXT(" = ");
			SerializeLuaValue(L, lua_gettop(L), Out, NewIndent);
			Out += TEXT(",\n");
			lua_pop(L, 1); // pop value, keep key for next iteration
		}

		AppendIndent(Out, Indent);
		Out += TEXT("}");
		break;
	}

	default:
		// Functions, userdata, threads — skip with nil
		Out += TEXT("nil");
		break;
	}
#endif
}
