#include "Formats/Dbc/ItemDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogItemDbc, Log, All);

bool FItemDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid())
    {
        return false;
    }

    Entries.Reset();
    IdIndex.Reset();
    Entries.Reserve(Parser.GetRecordCount());

    for (uint32 RecordIndex = 0; RecordIndex < Parser.GetRecordCount(); ++RecordIndex)
    {
        FItemDbcEntry Entry;
        Entry.ID = Parser.GetUInt(RecordIndex, 0);
        Entry.ClassID = Parser.GetUInt(RecordIndex, 1);
        Entry.SubclassID = Parser.GetUInt(RecordIndex, 2);
        Entry.SoundOverrideSubclassID = static_cast<int32>(Parser.GetUInt(RecordIndex, 3));
        Entry.MaterialID = static_cast<int32>(Parser.GetUInt(RecordIndex, 4));
        Entry.DisplayInfoID = Parser.GetUInt(RecordIndex, 5);
        Entry.InventoryType = Parser.GetUInt(RecordIndex, 6);
        Entry.SheathType = Parser.GetUInt(RecordIndex, 7);
        IdIndex.Add(Entry.ID, Entries.Num());
        Entries.Add(MoveTemp(Entry));
    }

    UE_LOG(LogItemDbc, Log, TEXT("Loaded Item.dbc: %d records"), Entries.Num());
    return true;
}

const FItemDbcEntry* FItemDbc::GetById(uint32 ID) const
{
    const int32* Index = IdIndex.Find(ID);
    return Index ? &Entries[*Index] : nullptr;
}
