#include "WowAddonErrorLog.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include "CoreGlobals.h"

namespace
{
FCriticalSection GLock;
TMap<FString, FWowAddonRuntimeStatus> GStatus; // key: lower-case addon folder name

/** "...\Interface\AddOns\DBM-Core\DBM-Core.lua:1431: ..." -> "DBM-Core". */
FString AddonFromText(const FString& Text)
{
	int32 At = Text.Find(TEXT("AddOns\\"), ESearchCase::IgnoreCase);
	if (At == INDEX_NONE) At = Text.Find(TEXT("AddOns/"), ESearchCase::IgnoreCase);
	int32 Skip = 7;
	if (At == INDEX_NONE)
	{
		// Lua shortens long chunk names: "...dOns\Bartender4\libs\LibDBIcon-1.0.lua".
		const int32 Dots = Text.Find(TEXT("..."));
		const int32 Slash = Dots == INDEX_NONE ? INDEX_NONE : Text.Find(TEXT("\\"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Dots);
		if (Slash == INDEX_NONE || Text.Mid(Dots, Slash - Dots).Contains(TEXT(" "))) return FString();
		At = Slash;
		Skip = 1;
	}
	const int32 Start = At + Skip;
	int32 End = Start;
	while (End < Text.Len() && Text[End] != TEXT('\\') && Text[End] != TEXT('/') && Text[End] != TEXT(':') && Text[End] != TEXT(' ') && Text[End] != TEXT('"'))
		++End;
	return End < Text.Len() && (Text[End] == TEXT('\\') || Text[End] == TEXT('/')) ? Text.Mid(Start, End - Start) : FString();
}

class FAddonLogListener : public FOutputDevice
{
public:
	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		if (!V) return;
		const FString Text(V);
		if (Text.StartsWith(TEXT("[AddOns"))) return; // our own status dump
		static const FName AddonCategory(TEXT("LogWowAddon"));
		if (Category == AddonCategory)
		{
			FString Name, Rest;
			if (Text.StartsWith(TEXT("Addon ")) && Text.Mid(6).Split(TEXT(" "), &Name, &Rest))
			{
				FScopeLock Lock(&GLock);
				FWowAddonRuntimeStatus& S = GStatus.FindOrAdd(Name.ToLower());
				if (Rest.StartsWith(TEXT("loaded successfully"))) S.bLoaded = true;
				else if (Rest.StartsWith(TEXT("not loaded: "))) S.NotLoadedReason = Rest.Mid(12);
				else if (Rest.StartsWith(TEXT("is disabled"))) S.NotLoadedReason = TEXT("disabled");
				return;
			}
			if (Text.Contains(TEXT("Could not load:"))) return; // optional files listed in a .toc
		}
		const int32 Level = int32(Verbosity & ELogVerbosity::VerbosityMask);
		const bool bProblem = (Level != ELogVerbosity::NoLogging && Level <= ELogVerbosity::Warning)
			|| Text.Contains(TEXT("error"), ESearchCase::IgnoreCase);
		if (!bProblem) return;
		const FString Addon = AddonFromText(Text);
		if (Addon.IsEmpty()) return;
		FScopeLock Lock(&GLock);
		FWowAddonRuntimeStatus& S = GStatus.FindOrAdd(Addon.ToLower());
		++S.ErrorCount;
		// Keep the message from the file name on ("DBM-Core.lua:1431: attempt to call ...").
		FString Message = Text;
		const int32 At = Message.Find(Addon + TEXT("\\"), ESearchCase::IgnoreCase);
		if (At != INDEX_NONE) Message = Message.Mid(At + Addon.Len() + 1);
		Message = Message.Left(220);
		if (S.Errors.Num() < 5 && !S.Errors.Contains(Message)) S.Errors.Add(Message);
	}
};

FAddonLogListener* GListener = nullptr;
}

void FWowAddonErrorLog::Start()
{
	{
		FScopeLock Lock(&GLock);
		GStatus.Reset();
	}
	if (!GListener && GLog)
	{
		GListener = new FAddonLogListener(); // lives for the whole process
		GLog->AddOutputDevice(GListener);
	}
}

bool FWowAddonErrorLog::Get(const FString& AddonName, FWowAddonRuntimeStatus& Out)
{
	FScopeLock Lock(&GLock);
	if (const FWowAddonRuntimeStatus* S = GStatus.Find(AddonName.ToLower()))
	{
		Out = *S;
		return true;
	}
	return false;
}

FString FWowAddonErrorLog::Describe(const FString& AddonName)
{
	FWowAddonRuntimeStatus S;
	if (!Get(AddonName, S)) return FString();
	FString Out = FString::Printf(TEXT("%s: %s, %d error(s)"), *AddonName,
		S.bLoaded ? TEXT("loaded") : (S.NotLoadedReason.IsEmpty() ? TEXT("not loaded") : *S.NotLoadedReason), S.ErrorCount);
	for (const FString& E : S.Errors) Out += TEXT("\n  ") + E;
	return Out;
}
