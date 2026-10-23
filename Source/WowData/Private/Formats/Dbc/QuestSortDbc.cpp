#include "Formats/Dbc/QuestSortDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogQuestSortDbc, Log, All);

bool FQuestSortDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid())
    {
        return false;
    }

    Entries.Reset();
    IdIndex.Reset();
    Entries.Reserve(Parser.GetRecordCount());

    for (uint32 Index = 0; Index < Parser.GetRecordCount(); ++Index)
    {
        FQuestSortDbcEntry Entry;
        Entry.ID = Parser.GetUInt(Index, 0);
        Entry.Name = Parser.GetLocalizedString(Index, 1);
        IdIndex.Add(Entry.ID, Entries.Num());
        Entries.Add(MoveTemp(Entry));
    }

    UE_LOG(LogQuestSortDbc, Log, TEXT("Loaded QuestSort.dbc: %d records"), Entries.Num());
    return true;
}

const FQuestSortDbcEntry* FQuestSortDbc::GetById(uint32 ID) const
{
    const int32* Index = IdIndex.Find(ID);
    return Index ? &Entries[*Index] : nullptr;
}
