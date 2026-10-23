#include "Formats/Dbc/CreatureDisplayInfoExtraDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogCreatureDisplayInfoExtraDbc, Log, All);

bool FCreatureDisplayInfoExtraDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid() || Parser.GetFieldCount() < 21)
    {
        UE_LOG(LogCreatureDisplayInfoExtraDbc, Warning,
            TEXT("CreatureDisplayInfoExtra.dbc has %u fields; expected at least 21"),
            Parser.GetFieldCount());
        return false;
    }

    Entries.Reset();
    IdIndex.Reset();
    Entries.Reserve(Parser.GetRecordCount());

    for (uint32 Record = 0; Record < Parser.GetRecordCount(); ++Record)
    {
        FCreatureDisplayInfoExtraDbcEntry Entry;
        Entry.ID = Parser.GetUInt(Record, 0);
        Entry.DisplayRaceID = Parser.GetUInt(Record, 1);
        Entry.DisplaySexID = Parser.GetUInt(Record, 2);
        Entry.SkinID = Parser.GetUInt(Record, 3);
        Entry.FaceID = Parser.GetUInt(Record, 4);
        Entry.HairStyleID = Parser.GetUInt(Record, 5);
        Entry.HairColorID = Parser.GetUInt(Record, 6);
        Entry.FacialHairID = Parser.GetUInt(Record, 7);
        for (uint32 Slot = 0; Slot < UE_ARRAY_COUNT(Entry.NPCItemDisplay); ++Slot)
        {
            Entry.NPCItemDisplay[Slot] = Parser.GetUInt(Record, 8 + Slot);
        }
        Entry.Flags = Parser.GetUInt(Record, 19);
        Entry.BakeName = Parser.GetString(Record, 20);

        IdIndex.Add(Entry.ID, Entries.Num());
        Entries.Add(MoveTemp(Entry));
    }

    UE_LOG(LogCreatureDisplayInfoExtraDbc, Log,
        TEXT("Loaded CreatureDisplayInfoExtra.dbc: %d records"), Entries.Num());
    return true;
}

const FCreatureDisplayInfoExtraDbcEntry* FCreatureDisplayInfoExtraDbc::GetById(uint32 ID) const
{
    const int32* Index = IdIndex.Find(ID);
    return Index ? &Entries[*Index] : nullptr;
}
