#include "Formats/Dbc/GameObjectDisplayInfoDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameObjectDisplayInfoDbc, Log, All);

bool FGameObjectDisplayInfoDbc::Load(const FDbcParser& Parser)
{
	if (!Parser.IsValid())
	{
		return false;
	}

	// The WotLK table has 19 fields. Guard the geometry columns so an
	// accidental table from another client version cannot be read as bounds.
	if (Parser.GetFieldCount() < 19)
	{
		UE_LOG(LogGameObjectDisplayInfoDbc, Warning,
			TEXT("GameObjectDisplayInfo.dbc has %u fields; expected at least 19 for 3.3.5a"),
			Parser.GetFieldCount());
		return false;
	}

	Entries.Reset();
	IdIndex.Reset();
	Entries.Reserve(Parser.GetRecordCount());

	for (uint32 RecordIndex = 0; RecordIndex < Parser.GetRecordCount(); ++RecordIndex)
	{
		FGameObjectDisplayInfoDbcEntry Entry;
		Entry.ID = Parser.GetUInt(RecordIndex, 0);
		Entry.ModelPath = Parser.GetString(RecordIndex, 1);
		Entry.BoundsMin = FVector(
			Parser.GetFloat(RecordIndex, 12),
			Parser.GetFloat(RecordIndex, 13),
			Parser.GetFloat(RecordIndex, 14));
		Entry.BoundsMax = FVector(
			Parser.GetFloat(RecordIndex, 15),
			Parser.GetFloat(RecordIndex, 16),
			Parser.GetFloat(RecordIndex, 17));
		Entry.Transport = Parser.GetUInt(RecordIndex, 18);

		IdIndex.Add(Entry.ID, Entries.Num());
		Entries.Add(MoveTemp(Entry));
	}

	UE_LOG(LogGameObjectDisplayInfoDbc, Log,
		TEXT("Loaded GameObjectDisplayInfo.dbc: %d records"), Entries.Num());
	return true;
}

const FGameObjectDisplayInfoDbcEntry* FGameObjectDisplayInfoDbc::GetById(uint32 ID) const
{
	const int32* Index = IdIndex.Find(ID);
	return Index ? &Entries[*Index] : nullptr;
}
