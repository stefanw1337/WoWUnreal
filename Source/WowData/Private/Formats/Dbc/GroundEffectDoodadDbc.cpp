#include "Formats/Dbc/GroundEffectDoodadDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogGroundEffectDoodadDbc, Log, All);

bool FGroundEffectDoodadDbc::Load(const FDbcParser& Parser)
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
        FGroundEffectDoodadDbcEntry Entry;
        Entry.ID = Parser.GetUInt(i, 0);
        Entry.ModelPath = Parser.GetString(i, 1);
        Entry.ModelPath.ReplaceInline(TEXT("/"), TEXT("\\"));
        if (!Entry.ModelPath.Contains(TEXT("\\")))
        {
            Entry.ModelPath = TEXT("World\\NoDXT\\Detail\\") + Entry.ModelPath;
        }
        if (Entry.ModelPath.EndsWith(TEXT(".mdl"), ESearchCase::IgnoreCase) ||
            Entry.ModelPath.EndsWith(TEXT(".mdx"), ESearchCase::IgnoreCase))
        {
            Entry.ModelPath = Entry.ModelPath.LeftChop(4) + TEXT(".m2");
        }
        Entry.Flags = Parser.GetUInt(i, 2);
        IdIndex.Add(Entry.ID, Entries.Num());
        Entries.Add(MoveTemp(Entry));
    }

    UE_LOG(LogGroundEffectDoodadDbc, Log, TEXT("Loaded GroundEffectDoodad.dbc: %d records"), Entries.Num());
    return true;
}

const FGroundEffectDoodadDbcEntry* FGroundEffectDoodadDbc::GetById(uint32 ID) const
{
    const int32* Index = IdIndex.Find(ID);
    return Index ? &Entries[*Index] : nullptr;
}
