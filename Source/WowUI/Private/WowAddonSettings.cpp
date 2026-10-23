#include "WowAddonSettings.h"
#include "WowAddonLoader.h"
#include "Mpq/MpqManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "WowSavedVariables.h"

namespace
{
	TMap<FString, bool> GAddonStates;   // key: lower-case name
	TMap<FString, FString> GDisplayNames; // lower-case -> original spelling for saving
	bool GLoaded = false;
	FString GLoadedPath; // r308: the list file read last; another character's file is read anew
	FMpqManager* GMpq = nullptr;
}

namespace
{
	/** Using the WoW client's WTF (any mode but Own) and logged in: the character's own AddOns.txt
	    like the client (WTF\Account\<A>\<Realm>\<Character>\AddOns.txt). Otherwise the one list
	    in Saved/WTF (character select, offline, Own mode). */
	bool UsesCharacterAddonList()
	{
		return FWowSavedVariables::HasIdentity()
			&& FWowSavedVariables::GetClientWtfMode() != FWowSavedVariables::EClientWtfMode::Own;
	}
	FString CharacterAddonListRelative() { return FPaths::Combine(FWowSavedVariables::GetCharacterRelative(), TEXT("AddOns.txt")); }
	/** r308 (Stefan 13.10: the addon choices must be kept per character from session to session): in
	    "fresh each launch" mode WTF writes go to Saved/WTF-Session, which is emptied at the next launch,
	    so the AddOns list lived one session. The character's list is kept in our own Saved/WTF
	    instead and read from there first; the WoW client's file is only the starting point. */
	bool KeepsOwnCharacterList()
	{
		return UsesCharacterAddonList()
			&& FWowSavedVariables::GetClientWtfMode() == FWowSavedVariables::EClientWtfMode::FreshEachLaunch;
	}
	FString OwnCharacterAddonList() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WTF"), CharacterAddonListRelative()); }
}

FString FWowAddonSettings::GetFilePath()
{
	if (KeepsOwnCharacterList()) return OwnCharacterAddonList();
	if (UsesCharacterAddonList()) return FWowSavedVariables::GetWritePath(CharacterAddonListRelative());
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WTF"), TEXT("AddOns.txt"));
}

void FWowAddonSettings::EnsureLoaded()
{
	const FString FilePath = GetFilePath();
	if (GLoaded && GLoadedPath == FilePath) return;
	GLoaded = true;
	GLoadedPath = FilePath;
	GAddonStates.Reset();
	GDisplayNames.Reset();
	TArray<FString> Lines;
	const FString ReadPath = KeepsOwnCharacterList() && FPaths::FileExists(FilePath) ? FilePath
		: UsesCharacterAddonList() ? FWowSavedVariables::ResolveReadPath(CharacterAddonListRelative()) : FilePath;
	if (UsesCharacterAddonList()) FWowSavedVariables::NoteRead(FilePath);
	UE_LOG(LogTemp, Log, TEXT("AddOns list: reading %s (saves to %s)"), ReadPath.IsEmpty() ? TEXT("(none)") : *ReadPath, *FilePath);
	if (ReadPath.IsEmpty() || !FFileHelper::LoadFileToStringArray(Lines, *ReadPath)) return;
	for (const FString& Line : Lines)
	{
		FString Name, State;
		if (!Line.Split(TEXT(":"), &Name, &State)) continue;
		Name = Name.TrimStartAndEnd();
		State = State.TrimStartAndEnd();
		if (Name.IsEmpty()) continue;
		GAddonStates.Add(Name.ToLower(), !State.Equals(TEXT("disabled"), ESearchCase::IgnoreCase));
		GDisplayNames.Add(Name.ToLower(), Name);
	}
}

void FWowAddonSettings::Reload()
{
	GLoaded = false;
	EnsureLoaded();
}

bool FWowAddonSettings::IsEnabled(const FString& AddonName)
{
	EnsureLoaded();
	const bool* State = GAddonStates.Find(AddonName.ToLower());
	return State ? *State : true;
}

void FWowAddonSettings::SetEnabled(const FString& AddonName, bool bEnabled)
{
	EnsureLoaded();
	GAddonStates.Add(AddonName.ToLower(), bEnabled);
	GDisplayNames.Add(AddonName.ToLower(), AddonName);
}

void FWowAddonSettings::Save()
{
	EnsureLoaded();
	TArray<FString> Keys;
	GAddonStates.GetKeys(Keys);
	Keys.Sort();
	FString Out;
	for (const FString& Key : Keys)
	{
		const FString* Display = GDisplayNames.Find(Key);
		Out += FString::Printf(TEXT("%s: %s\r\n"), Display ? **Display : *Key, GAddonStates[Key] ? TEXT("enabled") : TEXT("disabled"));
	}
	FWowSavedVariables::SaveWtfFile(GetFilePath(), Out);
}

void FWowAddonSettings::SetMpq(FMpqManager* Mpq)
{
	GMpq = Mpq;
}

FMpqManager* FWowAddonSettings::GetMpq()
{
	return GMpq;
}

FString FWowAddonSettings::StripColorCodes(const FString& Text)
{
	FString Out;
	Out.Reserve(Text.Len());
	for (int32 Index = 0; Index < Text.Len(); ++Index)
	{
		if (Text[Index] == TEXT('|') && Index + 1 < Text.Len())
		{
			const TCHAR Code = FChar::ToLower(Text[Index + 1]);
			if (Code == TEXT('c') && Index + 9 < Text.Len()) { Index += 9; continue; }
			if (Code == TEXT('r')) { Index += 1; continue; }
		}
		Out.AppendChar(Text[Index]);
	}
	return Out;
}

TArray<FWowAddonListEntry> FWowAddonSettings::GetUserAddons(FMpqManager* Mpq)
{
	TArray<FWowAddonListEntry> Result;
	if (!Mpq) Mpq = GMpq;
	if (!Mpq) return Result;
	for (const FString& Name : FWowAddonLoader::DiscoverAddons(Mpq))
	{
		if (Name.StartsWith(TEXT("Blizzard_"), ESearchCase::IgnoreCase)) continue;
		TArray<uint8> TocData;
		if (!FWowAddonLoader::ReadTocCached(Mpq, Name, TocData)) continue; // r364
		const FWowTocData Toc = FWowAddonLoader::ParseToc(TocData);
		FWowAddonListEntry Entry;
		Entry.Name = Name;
		Entry.Title = StripColorCodes(Toc.Title.IsEmpty() ? Name : Toc.Title);
		Entry.Notes = StripColorCodes(Toc.Notes);
		Entry.Version = Toc.Version;
		Entry.RequiredDeps = Toc.RequiredDeps;
		Entry.bLoadOnDemand = Toc.bLoadOnDemand;
		Entry.Interface = Toc.Interface;
		Entry.bEnabled = IsEnabled(Name);
		Result.Add(MoveTemp(Entry));
	}
	Result.Sort([](const FWowAddonListEntry& A, const FWowAddonListEntry& B) { return A.Title < B.Title; });
	return Result;
}
