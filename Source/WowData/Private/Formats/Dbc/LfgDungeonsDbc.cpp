#include "Formats/Dbc/LfgDungeonsDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogLfgDungeonsDbc, Log, All);

bool FLfgDungeonsDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid() || Parser.GetFieldCount() < 49)
    {
        UE_LOG(LogLfgDungeonsDbc, Warning,
            TEXT("LFGDungeons.dbc has an invalid 3.3.5a layout (%u fields)"),
            Parser.GetFieldCount());
        return false;
    }

    Entries.Reset();
    IdIndex.Reset();
    Entries.Reserve(Parser.GetRecordCount());

    for (uint32 Index = 0; Index < Parser.GetRecordCount(); ++Index)
    {
        FLfgDungeonDbcEntry Entry;
        Entry.ID = Parser.GetUInt(Index, 0);
        Entry.Name = Parser.GetLocalizedString(Index, 1);
        Entry.MinLevel = Parser.GetUInt(Index, 18);
        Entry.MaxLevel = Parser.GetUInt(Index, 19);
        Entry.TargetLevel = Parser.GetUInt(Index, 20);
        Entry.TargetLevelMin = Parser.GetUInt(Index, 21);
        Entry.TargetLevelMax = Parser.GetUInt(Index, 22);
        Entry.MapID = Parser.GetUInt(Index, 23);
        Entry.Difficulty = Parser.GetUInt(Index, 24);
        Entry.Flags = Parser.GetUInt(Index, 25);
        Entry.TypeID = Parser.GetUInt(Index, 26);
        Entry.Faction = static_cast<int32>(Parser.GetUInt(Index, 27));
        Entry.TextureFilename = Parser.GetString(Index, 28);
        Entry.ExpansionLevel = Parser.GetUInt(Index, 29);
        Entry.OrderIndex = Parser.GetUInt(Index, 30);
        Entry.GroupID = Parser.GetUInt(Index, 31);
        Entry.Description = Parser.GetLocalizedString(Index, 32);

        IdIndex.Add(Entry.ID, Entries.Num());
        Entries.Add(MoveTemp(Entry));
    }

    Entries.Sort([](const FLfgDungeonDbcEntry& A, const FLfgDungeonDbcEntry& B)
    {
        if (A.OrderIndex != B.OrderIndex) return A.OrderIndex < B.OrderIndex;
        return A.ID < B.ID;
    });
    IdIndex.Reset();
    for (int32 Index = 0; Index < Entries.Num(); ++Index)
    {
        IdIndex.Add(Entries[Index].ID, Index);
    }

    UE_LOG(LogLfgDungeonsDbc, Log, TEXT("Loaded LFGDungeons.dbc: %d records"), Entries.Num());
    return true;
}

const FLfgDungeonDbcEntry* FLfgDungeonsDbc::GetById(uint32 ID) const
{
    const int32* Index = IdIndex.Find(ID);
    return Index ? &Entries[*Index] : nullptr;
}
