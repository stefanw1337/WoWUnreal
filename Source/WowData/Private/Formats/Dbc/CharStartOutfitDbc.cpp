#include "Formats/Dbc/CharStartOutfitDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogCharStartOutfitDbc, Log, All);

bool FCharStartOutfitDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid()) return false;

    Entries.Reset();
    IdIndex.Reset();
    RaceClassGenderIndex.Reset();
    Entries.Reserve(Parser.GetRecordCount());

    for (uint32 i = 0; i < Parser.GetRecordCount(); ++i)
    {
        FCharStartOutfitDbcEntry E;
        E.ID = Parser.GetUInt(i, 0);

        // WoW 3.3.5 stores Race/Class/Gender/Outfit as four bytes in field 1.
        // The following 72 fields are ItemID[24], DisplayItemID[24], and
        // InventoryType[24].  ItemID is deliberately skipped here because the
        // character renderer consumes display IDs.
        uint32 PackedCol = Parser.GetUInt(i, 1);
        E.RaceID = PackedCol & 0xFF;         // byte 0
        E.ClassID = (PackedCol >> 8) & 0xFF; // byte 1
        E.GenderID = (PackedCol >> 16) & 0xFF; // byte 2

        // If packed extraction gives invalid values, try regular column layout
        if (E.RaceID == 0 || E.ClassID == 0)
        {
            // Fall back to non-packed format: col 1=Race, col 2=Class, col 3=Gender
            E.RaceID = Parser.GetUInt(i, 1);
            E.ClassID = Parser.GetUInt(i, 2);
            E.GenderID = Parser.GetUInt(i, 3);

            // Columns 2-25: ItemId (not needed for our use case)
            // Columns 26-49: ItemDisplayId
            // Columns 50-73: ItemInventorySlot
            for (int32 j = 0; j < 24; ++j)
            {
                E.ItemDisplayIds[j] = static_cast<int32>(Parser.GetUInt(i, 26 + j));
                E.ItemInventorySlots[j] = static_cast<int32>(Parser.GetUInt(i, 50 + j));
            }
        }
        else
        {
            // Packed format changes only how the race/class/gender header is
            // represented; the three 24-entry item arrays retain their normal
            // offsets.
            for (int32 j = 0; j < 24; ++j)
            {
                E.ItemDisplayIds[j] = static_cast<int32>(Parser.GetUInt(i, 26 + j));
                E.ItemInventorySlots[j] = static_cast<int32>(Parser.GetUInt(i, 50 + j));
            }
        }

        IdIndex.Add(E.ID, Entries.Num());

        // Create lookup key for race/class/gender combination
        FString RCGKey = FString::Printf(TEXT("%d_%d_%d"), E.RaceID, E.ClassID, E.GenderID);
        RaceClassGenderIndex.Add(RCGKey, Entries.Num());

        Entries.Add(MoveTemp(E));
    }

    UE_LOG(LogCharStartOutfitDbc, Log, TEXT("Loaded CharStartOutfit.dbc: %d records"), Entries.Num());

    // Debug: Log first few entries to verify parsing
    for (int32 i = 0; i < FMath::Min(3, Entries.Num()); ++i)
    {
        const FCharStartOutfitDbcEntry& E = Entries[i];
        int32 ItemCount = 0;
        for (int32 j = 0; j < 24; ++j)
        {
            if (E.ItemDisplayIds[j] > 0) ItemCount++;
        }
        UE_LOG(LogCharStartOutfitDbc, Log, TEXT("  CharStartOutfit[%d]: ID=%d Race=%d Class=%d Gender=%d Items=%d"),
            i, E.ID, E.RaceID, E.ClassID, E.GenderID, ItemCount);
    }

    return true;
}

const FCharStartOutfitDbcEntry* FCharStartOutfitDbc::GetById(uint32 ID) const
{
    const int32* Idx = IdIndex.Find(ID);
    return Idx ? &Entries[*Idx] : nullptr;
}

const FCharStartOutfitDbcEntry* FCharStartOutfitDbc::GetByRaceClassGender(uint32 Race, uint32 Class, uint32 Gender) const
{
    FString RCGKey = FString::Printf(TEXT("%d_%d_%d"), Race, Class, Gender);
    const int32* Idx = RaceClassGenderIndex.Find(RCGKey);
    return Idx ? &Entries[*Idx] : nullptr;
}
