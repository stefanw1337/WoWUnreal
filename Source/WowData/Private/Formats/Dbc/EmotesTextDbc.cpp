#include "Formats/Dbc/EmotesTextDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogEmotesTextDbc, Log, All);

bool FEmotesTextDbc::Load(const FDbcParser& Parser)
{
	if (!Parser.IsValid())
	{
		return false;
	}

	Entries.Reset();
	IdIndex.Reset();
	Entries.Reserve(Parser.GetRecordCount());

	for (uint32 i = 0; i < Parser.GetRecordCount(); ++i)
	{
		FEmotesTextDbcEntry E;
		E.ID = Parser.GetUInt(i, 0);
		E.Name = Parser.GetString(i, 1);
		E.EmoteID = Parser.GetUInt(i, 2);

		IdIndex.Add(E.ID, Entries.Num());
		Entries.Add(MoveTemp(E));
	}

	UE_LOG(LogEmotesTextDbc, Log, TEXT("Loaded EmotesText.dbc: %d records"), Entries.Num());
	return true;
}

const FEmotesTextDbcEntry* FEmotesTextDbc::GetById(uint32 ID) const
{
	const int32* Idx = IdIndex.Find(ID);
	return Idx ? &Entries[*Idx] : nullptr;
}
const FEmotesTextDbcEntry* FEmotesTextDbc::GetByCommand(const FString& Command) const
{
    FString Name = Command.ToUpper();
    if (const auto* Exact = Entries.FindByPredicate([&](const FEmotesTextDbcEntry& E) { return E.Name.Equals(Name, ESearchCase::IgnoreCase); })) return Exact;
    // Alternate slash spellings, never numeric IDs from a different DBC.
    static const TMap<FString,FString> Aliases = {
        {TEXT("HELLO"),TEXT("GREET")}, {TEXT("GOODBYE"),TEXT("BYE")},
        {TEXT("CLAP"),TEXT("APPLAUD")}, {TEXT("YES"),TEXT("NOD")},
        {TEXT("CONGRATULATIONS"),TEXT("CONGRATULATE")}
    };
    if (const FString* Alias = Aliases.Find(Name)) Name = *Alias;
    return Entries.FindByPredicate([&](const FEmotesTextDbcEntry& E) { return E.Name.Equals(Name, ESearchCase::IgnoreCase); });
}
