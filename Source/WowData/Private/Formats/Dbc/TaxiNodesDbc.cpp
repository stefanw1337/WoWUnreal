#include "Formats/Dbc/TaxiNodesDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogTaxiNodesDbc, Log, All);

bool FTaxiNodesDbc::Load(const FDbcParser& Parser)
{
    Entries.Empty();
    IdIndex.Empty();

    if (!Parser.IsValid())
    {
        UE_LOG(LogTaxiNodesDbc, Warning, TEXT("Invalid DBC header"));
        return false;
    }

    const int32 NumRecords = Parser.GetRecordCount();
    Entries.Reserve(NumRecords);

    for (int32 i = 0; i < NumRecords; ++i)
    {
        FTaxiNodesDbcEntry Entry;

        Entry.ID = Parser.GetUInt(i, 0);
        Entry.MapID = Parser.GetUInt(i, 1);
        Entry.X = Parser.GetFloat(i, 2);
        Entry.Y = Parser.GetFloat(i, 3);
        Entry.Z = Parser.GetFloat(i, 4);
        Entry.Name = Parser.GetLocalizedString(i, 5);

        IdIndex.Add(Entry.ID, Entries.Num());
        Entries.Add(Entry);
    }

    UE_LOG(LogTaxiNodesDbc, Log, TEXT("Loaded %d taxi nodes"), Entries.Num());
    return true;
}

const FTaxiNodesDbcEntry* FTaxiNodesDbc::GetById(uint32 ID) const
{
    const int32* IndexPtr = IdIndex.Find(ID);
    return IndexPtr ? &Entries[*IndexPtr] : nullptr;
}