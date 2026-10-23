#include "Formats/Dbc/AreaTableDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogAreaTableDbc, Log, All);

bool FAreaTableDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid()) return false;

    Entries.Reserve(Parser.GetRecordCount());
    for (uint32 i = 0; i < Parser.GetRecordCount(); ++i)
    {
        FAreaTableDbcEntry E;
        E.ID           = Parser.GetUInt(i, 0);
        E.MapID        = Parser.GetUInt(i, 1);
        E.ParentAreaID = Parser.GetUInt(i, 2);
        E.ExploreFlag  = Parser.GetUInt(i, 3);
        E.Flags        = Parser.GetUInt(i, 4);
        E.AmbienceID   = Parser.GetUInt(i, 7);
        E.ZoneMusicID  = Parser.GetUInt(i, 8);
        E.IntroMusicID = Parser.GetUInt(i, 9);
        E.AreaLevel    = static_cast<int32>(Parser.GetUInt(i, 10));
        E.Name         = Parser.GetLocalizedString(i, 11);
        E.Team         = Parser.GetUInt(i, 28);
        IdIndex.Add(E.ID, Entries.Num());
        Entries.Add(MoveTemp(E));
    }

    UE_LOG(LogAreaTableDbc, Log, TEXT("Loaded AreaTable.dbc: %d records"), Entries.Num());
    return true;
}

const FAreaTableDbcEntry* FAreaTableDbc::GetById(uint32 ID) const
{
    const int32* Idx = IdIndex.Find(ID);
    return Idx ? &Entries[*Idx] : nullptr;
}
