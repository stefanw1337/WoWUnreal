#include "WowAddonLoader.h"
#include "Mpq/MpqManager.h"
#include "WowLuaVM.h"
#include "WowFrameXmlParser.h"
#include "WowSavedVariables.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "LuaApi/LuaApiRegistry.h"
#include "WowAddonSettings.h"
#include "WowAddonErrorLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowAddon, Log, All);

namespace WowLuaApi
{
	void MarkAddonLoaded(const FString& AddonName);
}

namespace
{
TSet<FString> LoadedAddonNames;

FString NormalizeWowPath(const FString& InPath)
{
	FString Normalized = InPath;
	Normalized.ReplaceInline(TEXT("/"), TEXT("\\"));

	while (Normalized.Contains(TEXT("\\\\")))
	{
		Normalized.ReplaceInline(TEXT("\\\\"), TEXT("\\"));
	}

	TArray<FString> Segments;
	Normalized.ParseIntoArray(Segments, TEXT("\\"), true);

	TArray<FString> CleanSegments;
	for (const FString& Segment : Segments)
	{
		if (Segment.IsEmpty() || Segment == TEXT("."))
		{
			continue;
		}

		if (Segment == TEXT(".."))
		{
			if (CleanSegments.Num() > 0)
			{
				CleanSegments.Pop();
			}
			continue;
		}

		CleanSegments.Add(Segment);
	}

	return FString::Join(CleanSegments, TEXT("\\"));
}

FString GetWowDirectory(const FString& InPath)
{
	const FString Normalized = NormalizeWowPath(InPath);
	int32 SlashIndex = INDEX_NONE;
	if (Normalized.FindLastChar(TEXT('\\'), SlashIndex))
	{
		return Normalized.Left(SlashIndex + 1);
	}

	return FString();
}

FString ResolveWowPath(const FString& BaseDirectory, const FString& RelativePath)
{
	const FString Candidate = NormalizeWowPath(RelativePath);
	if (Candidate.StartsWith(TEXT("Interface\\"), ESearchCase::IgnoreCase))
	{
		return Candidate;
	}

	return NormalizeWowPath(BaseDirectory + Candidate);
}

bool TryReadWowFile(FMpqManager* Mpq, const FString& InPath, TArray<uint8>& OutData)
{
	if (!Mpq)
	{
		return false;
	}

	return Mpq->ReadFile(NormalizeWowPath(InPath), OutData);
}

bool IsAddonLoaded(const FString& AddonName)
{
	return LoadedAddonNames.Contains(AddonName.ToLower());
}

void MarkAddonLoadedInternal(const FString& AddonName)
{
	LoadedAddonNames.Add(AddonName.ToLower());
	WowLuaApi::MarkAddonLoaded(AddonName);
}

bool ProcessXmlDirectivesRecursive(
	const FString& XmlPath,
	FMpqManager* Mpq,
	FWowLuaVM* LuaVM,
	FWowFrameManager* FrameManager,
	TSet<FString>& VisitedXmlFiles,
	const FString& AddonName)
{
	const FString NormalizedXmlPath = NormalizeWowPath(XmlPath);
	const FString VisitKey = NormalizedXmlPath.ToLower();
	if (VisitedXmlFiles.Contains(VisitKey))
	{
		UE_LOG(LogWowAddon, Verbose, TEXT("Skipping already processed addon XML include: %s"), *NormalizedXmlPath);
		return true;
	}

	VisitedXmlFiles.Add(VisitKey);

	TArray<uint8> FileData;
	if (!TryReadWowFile(Mpq, NormalizedXmlPath, FileData))
	{
		UE_LOG(LogWowAddon, Warning, TEXT("  Could not load: %s"), *NormalizedXmlPath);
		return false;
	}

	const FString BaseDirectory = GetWowDirectory(NormalizedXmlPath);
	const TArray<FWowXmlDirective> Directives = FWowFrameXmlParser::ParseXml(FileData, NormalizedXmlPath);

	for (const FWowXmlDirective& Dir : Directives)
	{
		switch (Dir.Type)
		{
		case FWowXmlDirective::EType::Script:
			if (!Dir.FilePath.IsEmpty())
			{
				const FString ScriptPath = ResolveWowPath(BaseDirectory, Dir.FilePath);
				TArray<uint8> ScriptData;
				if (TryReadWowFile(Mpq, ScriptPath, ScriptData))
				{
					LuaVM->ExecuteAddonBuffer(ScriptData, ScriptPath, AddonName);
				}
				else
				{
					UE_LOG(LogWowAddon, Warning, TEXT("  Could not load script: %s"), *ScriptPath);
				}
			}
			break;

		case FWowXmlDirective::EType::Include:
			if (!Dir.FilePath.IsEmpty())
			{
				ProcessXmlDirectivesRecursive(ResolveWowPath(BaseDirectory, Dir.FilePath), Mpq, LuaVM, FrameManager, VisitedXmlFiles, AddonName);
			}
			break;

		case FWowXmlDirective::EType::Frame:
			if (FrameManager)
			{
				FrameManager->CreateFrame(Dir.FrameDef);
			}
			break;

		case FWowXmlDirective::EType::Font:
			UE_LOG(LogWowAddon, Verbose, TEXT("  Font directive discovered in addon XML: %s"), *Dir.FontName);
			WowLuaApi::RegisterFontObject(LuaVM->GetState(), Dir.FontName, Dir.FontInherits, Dir.FontHeight, Dir.FontFlags);
			break;
		}
	}

	return true;
}

bool LoadAddonInternal(
	const FString& AddonName,
	FMpqManager* Mpq,
	FWowLuaVM* LuaVM,
	FWowFrameManager* FrameManager,
	FWowEventSystem* EventSystem,
	TSet<FString>& LoadingStack);
}

// Well-known Blizzard addon names from WoW 3.3.5 (those shipped inside MPQ)
static const char* BlizzardAddons[] =
{
	"Blizzard_AchievementUI",
	"Blizzard_ArenaUI",
	"Blizzard_AuctionUI",
	"Blizzard_BarbershopUI",
	"Blizzard_BattlefieldMinimap",
	"Blizzard_BindingUI",
	"Blizzard_Calendar",
	"Blizzard_CombatLog",
	"Blizzard_CombatText",
	"Blizzard_DebugTools",
	"Blizzard_GlyphUI",
	"Blizzard_GMChatUI",
	"Blizzard_GMSurveyUI",
	"Blizzard_GuildBankUI",
	"Blizzard_InspectUI",
	"Blizzard_ItemSocketingUI",
	"Blizzard_MacroUI",
	"Blizzard_RaidUI",
	"Blizzard_TalentUI",
	"Blizzard_TimeManager",
	"Blizzard_TokenUI",
	"Blizzard_TradeSkillUI",
	"Blizzard_TrainerUI",
	nullptr
};

FWowTocData FWowAddonLoader::ParseToc(const TArray<uint8>& Data)
{
	FWowTocData Toc;

	FString Content;
	if (Data.Num() >= 3 && Data[0] == 0xEF && Data[1] == 0xBB && Data[2] == 0xBF)
	{
		FUTF8ToTCHAR Conv((const ANSICHAR*)Data.GetData() + 3, Data.Num() - 3);
		Content = FString(Conv.Length(), Conv.Get());
	}
	else
	{
		FUTF8ToTCHAR Conv((const ANSICHAR*)Data.GetData(), Data.Num());
		Content = FString(Conv.Length(), Conv.Get());
	}

	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines);

	for (const FString& Line : Lines)
	{
		FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			continue;
		}

		// Metadata line: ## Key: Value
		if (Trimmed.StartsWith(TEXT("##")))
		{
			FString MetaLine = Trimmed.Mid(2).TrimStartAndEnd();
			FString Key, Value;
			if (MetaLine.Split(TEXT(":"), &Key, &Value))
			{
				Key = Key.TrimStartAndEnd();
				Value = Value.TrimStartAndEnd();
				Toc.Metadata.Add(Key.ToLower(), Value); // GetAddOnMetadata (Title, Version, X-*)

				if (Key.Equals(TEXT("Title"), ESearchCase::IgnoreCase))
				{
					Toc.Title = Value;
				}
				else if (Key.Equals(TEXT("Notes"), ESearchCase::IgnoreCase))
				{
					Toc.Notes = Value;
				}
				else if (Key.Equals(TEXT("Author"), ESearchCase::IgnoreCase))
				{
					Toc.Author = Value;
				}
				else if (Key.Equals(TEXT("Version"), ESearchCase::IgnoreCase))
				{
					Toc.Version = Value;
				}
				else if (Key.Equals(TEXT("Interface"), ESearchCase::IgnoreCase))
				{
					Toc.Interface = FCString::Atoi(*Value);
				}
				else if (Key.Equals(TEXT("RequiredDeps"), ESearchCase::IgnoreCase) ||
				         Key.Equals(TEXT("Dependencies"), ESearchCase::IgnoreCase))
				{
					Value.ParseIntoArray(Toc.RequiredDeps, TEXT(","));
					for (FString& Dep : Toc.RequiredDeps)
					{
						Dep = Dep.TrimStartAndEnd();
					}
				}
				else if (Key.Equals(TEXT("OptionalDeps"), ESearchCase::IgnoreCase))
				{
					Value.ParseIntoArray(Toc.OptionalDeps, TEXT(","));
					for (FString& Dep : Toc.OptionalDeps)
					{
						Dep = Dep.TrimStartAndEnd();
					}
				}
				else if (Key.Equals(TEXT("SavedVariables"), ESearchCase::IgnoreCase))
				{
					Value.ParseIntoArray(Toc.SavedVariables, TEXT(","));
					for (FString& SV : Toc.SavedVariables)
					{
						SV = SV.TrimStartAndEnd();
					}
				}
				else if (Key.Equals(TEXT("SavedVariablesPerCharacter"), ESearchCase::IgnoreCase))
				{
					Value.ParseIntoArray(Toc.SavedVariablesPerCharacter, TEXT(","));
					for (FString& SV : Toc.SavedVariablesPerCharacter)
					{
						SV = SV.TrimStartAndEnd();
					}
				}
				else if (Key.Equals(TEXT("LoadOnDemand"), ESearchCase::IgnoreCase))
				{
					Toc.bLoadOnDemand = Value.Equals(TEXT("1")) || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase);
				}
				else if (Key.Equals(TEXT("Disabled"), ESearchCase::IgnoreCase))
				{
					Toc.bDisabled = Value.Equals(TEXT("1")) || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase);
				}
			}
			continue;
		}

		// Comment lines starting with # (but not ##)
		if (Trimmed.StartsWith(TEXT("#")))
		{
			continue;
		}

		// File entry - normalize path separators
		FString FilePath = Trimmed.Replace(TEXT("/"), TEXT("\\"));
		Toc.Files.Add(FilePath);
	}

	return Toc;
}

// r364 (Stefan 16.10: "a bit much freezing" opening the AddOns list and on Reload UI): with 2076
// addon folders, every list opening and every reload walked the folders, checked each .toc and read
// all of them again (the 11:02 log: 9 s to open the list, 12 s for one reload, three passes). The
// folder list is kept per AddOns folder until that folder's timestamp changes (an addon added,
// removed or renamed), and the .toc bytes for the session; LoadAddon still reads the .toc of each
// addon it loads fresh, so an edited .toc takes effect on the next reload.
namespace WowAddonDiscoveryCache
{
	struct FRootEntry
	{
		FDateTime Stamp;
		TArray<FString> Names; // folders with their .toc, sorted
		TArray<FString> Skipped;
		TSet<FString> OnDisk; // names whose .toc is a loose file in this folder
	};
	TMap<FString, FRootEntry> Roots;
	TMap<FString, TArray<uint8>> Tocs; // lower-case addon name -> .toc bytes
	TMap<FString, FString> TocFiles;   // r391: lower-case addon name -> its loose .toc on disk
	const void* TocOwner = nullptr;
}

bool FWowAddonLoader::ReadTocCached(FMpqManager* Mpq, const FString& AddonName, TArray<uint8>& OutData)
{
	if (!Mpq || AddonName.IsEmpty()) return false;
	if (WowAddonDiscoveryCache::TocOwner != Mpq)
	{
		WowAddonDiscoveryCache::Tocs.Reset();
		WowAddonDiscoveryCache::TocOwner = Mpq;
	}
	const FString Key = AddonName.ToLower();
	if (const TArray<uint8>* Cached = WowAddonDiscoveryCache::Tocs.Find(Key))
	{
		OutData = *Cached;
		return OutData.Num() > 0;
	}
	TArray<uint8> Data;
	// r391: a .toc that discovery found on disk is read straight from there (one file read instead
	// of the archive manager's search through every root and archive - 2077 of them took ~10 s).
	const FString* TocFile = WowAddonDiscoveryCache::TocFiles.Find(Key);
	if (!TocFile || !FFileHelper::LoadFileToArray(Data, **TocFile))
		TryReadWowFile(Mpq, FString::Printf(TEXT("Interface\\AddOns\\%s\\%s.toc"), *AddonName, *AddonName), Data);
	WowAddonDiscoveryCache::Tocs.Add(Key, Data);
	OutData = MoveTemp(Data);
	return OutData.Num() > 0;
}

TArray<FString> FWowAddonLoader::DiscoverAddons(FMpqManager* Mpq)
{
	TArray<FString> AddonNames;

	if (!Mpq)
	{
		return AddonNames;
	}

	// Discover from the same priority-ordered roots used for every asset read.
	IFileManager& FM = IFileManager::Get();
	TSet<FString> Seen;
	for (const FString& Root : Mpq->GetLooseFileRoots())
	{
		const FString AddOnsPath = Root / TEXT("Interface/AddOns");
		const FDateTime Stamp = FM.GetTimeStamp(*AddOnsPath);
		WowAddonDiscoveryCache::FRootEntry* Cached = WowAddonDiscoveryCache::Roots.Find(AddOnsPath);
		// r391 (Stefan 16.10: AddOns and Okay/ReloadUI froze for ~10 s): a root that does not exist
		// (Data/Interface/AddOns) has no time stamp and was rescanned every time - and every rescan
		// emptied the .toc cache, so all 2077 .toc files were read again. A missing folder that is
		// still missing keeps its (empty) entry; the cache is emptied only when the folder list changed.
		const bool bMissingStill = Cached && Stamp == FDateTime::MinValue() && Cached->Stamp == Stamp
			&& !FM.DirectoryExists(*AddOnsPath);
		if (!bMissingStill && (!Cached || Cached->Stamp != Stamp || Stamp == FDateTime::MinValue()))
		{
			const TArray<FString> OldNames = Cached ? Cached->Names : TArray<FString>();
			const bool bHadEntry = Cached != nullptr;
			WowAddonDiscoveryCache::FRootEntry Fresh;
			Fresh.Stamp = Stamp;
			TArray<FString> Directories;
			FM.FindFiles(Directories, *(AddOnsPath / TEXT("*")), false, true);
			Directories.Sort();
			UE_LOG(LogWowAddon, Log, TEXT("Addon root %s: %d folders"), *AddOnsPath, Directories.Num());
			for (const FString& Name : Directories)
			{
				// The folder is on disk, so its .toc normally is too: one disk check, the archive
				// lookup only when that fails.
				const bool bDiskToc = FM.FileExists(*(AddOnsPath / Name / (Name + TEXT(".toc"))));
				const bool bToc = bDiskToc
					|| Mpq->FileExists(FString::Printf(TEXT("Interface/AddOns/%s/%s.toc"), *Name, *Name));
				(bToc ? Fresh.Names : Fresh.Skipped).Add(Name);
				if (bDiskToc) Fresh.OnDisk.Add(Name);
			}
			// an addon was added, removed or renamed
			if (bHadEntry && OldNames != Fresh.Names)
			{
				WowAddonDiscoveryCache::Tocs.Reset();
				WowAddonDiscoveryCache::TocFiles.Reset();
			}
			Cached = &WowAddonDiscoveryCache::Roots.Add(AddOnsPath, MoveTemp(Fresh));
			for (const FString& Name : Cached->Skipped)
			{
				UE_LOG(LogWowAddon, Warning, TEXT("  %s has no %s.toc, skipped"), *Name, *Name);
			}
		}
		for (const FString& Name : Cached->Names)
		{
			// r428 (Stefan 23.10: "why do we have a WoWUnreal addon?"): the old built-in addon
			// WowUnreal_UI is now part of the client (WowUIManager.cpp); copies still installed in
			// a Data folder are neither listed nor loaded.
			if (Name.StartsWith(TEXT("WowUnreal_"), ESearchCase::IgnoreCase)) continue;
			bool bSeen = false;
			Seen.Add(Name.ToLower(), &bSeen);
			if (!bSeen)
			{
				AddonNames.Add(Name);
				if (Cached->OnDisk.Contains(Name))
					WowAddonDiscoveryCache::TocFiles.Add(Name.ToLower(), AddOnsPath / Name / (Name + TEXT(".toc")));
				else
					WowAddonDiscoveryCache::TocFiles.Remove(Name.ToLower());
			}
		}
	}

	// Check well-known Blizzard addons from MPQ
	for (int32 i = 0; BlizzardAddons[i]; i++)
	{
		FString Name = UTF8_TO_TCHAR(BlizzardAddons[i]);
		if (Seen.Contains(Name.ToLower())) continue;

		TArray<uint8> IgnoredData;
		if (ReadTocCached(Mpq, Name, IgnoredData)) // r364
		{
			AddonNames.AddUnique(Name);
		}
	}

	UE_LOG(LogWowAddon, Log, TEXT("Discovered %d addons"), AddonNames.Num());
	return AddonNames;
}

// Blizzard_ addons are part of the client: always loaded and never "out of date".
static bool IsClientAddon(const FString& Name)
{
	return Name.StartsWith(TEXT("Blizzard_"), ESearchCase::IgnoreCase);
}

TArray<FString> FWowAddonLoader::ResolveLoadOrder(FMpqManager* Mpq, const TArray<FString>& AddonNames)
{
	if (!Mpq) return AddonNames;

	// Parse all TOC files to build dependency graph
	TMap<FString, FWowTocData> TocMap;
	for (const FString& Name : AddonNames)
	{
		TArray<uint8> TocData;

		if (ReadTocCached(Mpq, Name, TocData)) // r364
		{
			TocMap.Add(Name, ParseToc(TocData));
		}
	}

	// AddOns list (Saved/WTF/AddOns.txt): an unchecked addon, or one whose required
	// dependency is unchecked, is not loaded — same as the original client.
	auto IsUserDisabled = [](const FString& Name)
	{
		return !IsClientAddon(Name) && !FWowAddonSettings::IsEnabled(Name);
	};
	for (bool bChanged = true; bChanged;)
	{
		bChanged = false;
		for (auto& Pair : TocMap)
		{
			if (Pair.Value.bDisabled) continue;
			const bool bOutOfDate = !IsClientAddon(Pair.Key)
				&& FWowAddonSettings::IsOutOfDate(Pair.Value.Interface) && !FWowAddonSettings::GetLoadOutOfDate();
			bool bDisable = IsUserDisabled(Pair.Key) || bOutOfDate;
			for (const FString& Dep : Pair.Value.RequiredDeps)
			{
				const FWowTocData* DepToc = TocMap.Find(Dep);
				if (DepToc && DepToc->bDisabled) bDisable = true;
			}
			if (bDisable)
			{
				UE_LOG(LogWowAddon, Log, TEXT("Addon %s not loaded: %s"), *Pair.Key,
					IsUserDisabled(Pair.Key) ? TEXT("disabled in the AddOns list")
					: bOutOfDate ? TEXT("out of date") : TEXT("a required dependency is disabled"));
				Pair.Value.bDisabled = true;
				bChanged = true;
			}
		}
	}

	// Topological sort using Kahn's algorithm
	TSet<FString> Available(AddonNames);
	TMap<FString, TSet<FString>> InDeps; // addon -> set of unresolved required dependencies

	for (const FString& Name : AddonNames)
	{
		FWowTocData* Toc = TocMap.Find(Name);
		if (!Toc) continue;
		if (Toc->bDisabled || Toc->bLoadOnDemand) continue;

		TSet<FString>& Deps = InDeps.FindOrAdd(Name);
		for (const FString& Dep : Toc->RequiredDeps)
		{
			if (Available.Contains(Dep) && Dep != Name)
			{
				Deps.Add(Dep);
			}
		}
		// Optional deps: if available, add as soft dependency for ordering
		for (const FString& Dep : Toc->OptionalDeps)
		{
			if (Available.Contains(Dep) && Dep != Name)
			{
				Deps.Add(Dep);
			}
		}
	}

	TArray<FString> Sorted;
	TSet<FString> Visited;

	// Repeatedly find addons with no unresolved deps
	while (Sorted.Num() < AddonNames.Num())
	{
		bool bProgress = false;

		for (const FString& Name : AddonNames)
		{
			if (Visited.Contains(Name)) continue;

			FWowTocData* Toc = TocMap.Find(Name);
			if (Toc && (Toc->bDisabled || Toc->bLoadOnDemand))
			{
				Visited.Add(Name);
				continue;
			}

			TSet<FString>* Deps = InDeps.Find(Name);
			bool bReady = true;
			if (Deps)
			{
				for (const FString& Dep : *Deps)
				{
					if (!Visited.Contains(Dep))
					{
						bReady = false;
						break;
					}
				}
			}

			if (bReady)
			{
				Sorted.Add(Name);
				Visited.Add(Name);
				bProgress = true;
			}
		}

		if (!bProgress)
		{
			// Circular dependency or missing deps — add remaining addons
			for (const FString& Name : AddonNames)
			{
				if (!Visited.Contains(Name))
				{
					FWowTocData* Toc = TocMap.Find(Name);
					if (Toc && !Toc->bDisabled && !Toc->bLoadOnDemand)
					{
						UE_LOG(LogWowAddon, Warning, TEXT("Unresolved deps for addon: %s"), *Name);
						Sorted.Add(Name);
					}
					Visited.Add(Name);
				}
			}
			break;
		}
	}

	UE_LOG(LogWowAddon, Log, TEXT("Resolved load order for %d addons"), Sorted.Num());
	return Sorted;
}

bool FWowAddonLoader::LoadAddon(const FString& AddonName, FMpqManager* Mpq, FWowLuaVM* LuaVM,
	FWowFrameManager* FrameManager, FWowEventSystem* EventSystem)
{
	TSet<FString> LoadingStack;
	return LoadAddonInternal(AddonName, Mpq, LuaVM, FrameManager, EventSystem, LoadingStack);
}

namespace
{
bool LoadAddonInternal(
	const FString& AddonName,
	FMpqManager* Mpq,
	FWowLuaVM* LuaVM,
	FWowFrameManager* FrameManager,
	FWowEventSystem* EventSystem,
	TSet<FString>& LoadingStack)
{
	if (!Mpq || !LuaVM)
	{
		UE_LOG(LogWowAddon, Error, TEXT("LoadAddon %s: null MPQ or Lua VM"), *AddonName);
		return false;
	}

	const FString LoadingKey = AddonName.ToLower();
	if (IsAddonLoaded(AddonName))
	{
		return true;
	}

	if (LoadingStack.Contains(LoadingKey))
	{
		UE_LOG(LogWowAddon, Warning, TEXT("Circular addon dependency detected while loading %s"), *AddonName);
		return false;
	}

	LoadingStack.Add(LoadingKey);

	// Read the TOC file
	FString TocPath = FString::Printf(TEXT("Interface\\AddOns\\%s\\%s.toc"), *AddonName, *AddonName);
	TArray<uint8> TocData;

	if (!TryReadWowFile(Mpq, TocPath, TocData))
	{
		UE_LOG(LogWowAddon, Warning, TEXT("Could not find TOC for addon: %s"), *AddonName);
		LoadingStack.Remove(LoadingKey);
		return false;
	}

	FWowTocData Toc = FWowAddonLoader::ParseToc(TocData);
	if (!IsClientAddon(AddonName) && !FWowAddonSettings::IsEnabled(AddonName))
	{
		UE_LOG(LogWowAddon, Log, TEXT("LoadAddOn %s: disabled in the AddOns list"), *AddonName);
		LoadingStack.Remove(LoadingKey);
		return false;
	}

	if (!AddonName.StartsWith(TEXT("Blizzard_"), ESearchCase::IgnoreCase)
		&& FWowAddonSettings::IsOutOfDate(Toc.Interface) && !FWowAddonSettings::GetLoadOutOfDate())
	{
		UE_LOG(LogWowAddon, Log, TEXT("Addon %s not loaded: out of date"), *AddonName);
		LoadingStack.Remove(LoadingKey);
		return false;
	}

	if (Toc.bDisabled)
	{
		UE_LOG(LogWowAddon, Log, TEXT("Addon %s is disabled, skipping"), *AddonName);
		LoadingStack.Remove(LoadingKey);
		return false;
	}

	for (const FString& Dep : Toc.RequiredDeps)
	{
		if (Dep.IsEmpty())
		{
			continue;
		}

		if (!LoadAddonInternal(Dep, Mpq, LuaVM, FrameManager, EventSystem, LoadingStack))
		{
			UE_LOG(LogWowAddon, Warning, TEXT("Required dependency %s for addon %s failed to load"), *Dep, *AddonName);
			LoadingStack.Remove(LoadingKey);
			return false;
		}
	}

	for (const FString& Dep : Toc.OptionalDeps)
	{
		if (Dep.IsEmpty() || IsAddonLoaded(Dep))
		{
			continue;
		}

		LoadAddonInternal(Dep, Mpq, LuaVM, FrameManager, EventSystem, LoadingStack);
	}

	UE_LOG(LogWowAddon, Log, TEXT("Loading addon: %s (%s) - %d files"), *AddonName, *Toc.Title, Toc.Files.Num());

	// Load saved variables before addon code runs
	FWowSavedVariables::LoadForAddon(LuaVM->GetState(), AddonName, Toc);
	FWowSavedVariables::RegisterAddon(AddonName, Toc);

	FString BasePath = FString::Printf(TEXT("Interface\\AddOns\\%s\\"), *AddonName);
	TSet<FString> VisitedXmlFiles;
	if (EventSystem)
	{
		EventSystem->BeginOnLoadBatch();
	}

	for (const FString& File : Toc.Files)
	{
		const FString FullPath = ResolveWowPath(BasePath, File);
		TArray<uint8> FileData;
		const bool bLoaded = TryReadWowFile(Mpq, FullPath, FileData);

		if (!bLoaded)
		{
			UE_LOG(LogWowAddon, Warning, TEXT("  Could not load: %s"), *FullPath);
			continue;
		}

		if (File.EndsWith(TEXT(".lua"), ESearchCase::IgnoreCase))
		{
			LuaVM->ExecuteAddonBuffer(FileData, FullPath, AddonName);
		}
		else if (File.EndsWith(TEXT(".xml"), ESearchCase::IgnoreCase))
		{
			ProcessXmlDirectivesRecursive(FullPath, Mpq, LuaVM, FrameManager, VisitedXmlFiles, AddonName);
		}
	}

	if (EventSystem)
	{
		EventSystem->EndOnLoadBatch();
	}

	MarkAddonLoadedInternal(AddonName);
	if (EventSystem)
	{
		EventSystem->FireEvent(TEXT("ADDON_LOADED"), { AddonName });
	}

	LoadingStack.Remove(LoadingKey);
	UE_LOG(LogWowAddon, Log, TEXT("Addon %s loaded successfully"), *AddonName);
	return true;
}
} // namespace

void FWowAddonLoader::LoadAllAddons(FMpqManager* Mpq, FWowLuaVM* LuaVM,
	FWowFrameManager* FrameManager, FWowEventSystem* EventSystem)
{
	if (!Mpq || !LuaVM)
	{
		UE_LOG(LogWowAddon, Error, TEXT("LoadAllAddons: null MPQ or Lua VM"));
		return;
	}

	FWowAddonErrorLog::Start(); // per-addon load results and errors for the AddOns list
	TArray<FString> Discovered = DiscoverAddons(Mpq);
	// WowUnreal_ addons first (order is kept otherwise), so the player's addons can restyle after them.
	Discovered.StableSort([](const FString& A, const FString& B)
	{
		return A.StartsWith(TEXT("WowUnreal_"), ESearchCase::IgnoreCase) && !B.StartsWith(TEXT("WowUnreal_"), ESearchCase::IgnoreCase);
	});
	TArray<FString> Ordered = ResolveLoadOrder(Mpq, Discovered);
	LoadedAddonNames.Reset();

	int32 Loaded = 0;
	for (const FString& AddonName : Ordered)
	{
		if (LoadAddon(AddonName, Mpq, LuaVM, FrameManager, EventSystem))
		{
			Loaded++;
		}
	}

	UE_LOG(LogWowAddon, Log, TEXT("Loaded %d/%d addons"), Loaded, Ordered.Num());
}
