#include "Formats/Dbc/ItemDisplayInfoDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogItemDisplayInfoDbc, Log, All);

bool FItemDisplayInfoDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid() || Parser.GetFieldCount() != 25) return false;

    Entries.Reset();
    IdIndex.Reset();
    Entries.Reserve(Parser.GetRecordCount());

    for (uint32 i = 0; i < Parser.GetRecordCount(); ++i)
    {
        FItemDisplayInfoDbcEntry E;
        E.ID = Parser.GetUInt(i, 0);
        E.ModelNames[0] = Parser.GetString(i, 1);
        E.ModelNames[1] = Parser.GetString(i, 2);
        E.ModelTextures[0] = Parser.GetString(i, 3);
        E.ModelTextures[1] = Parser.GetString(i, 4);
        E.Icon1 = Parser.GetString(i, 5);
        E.Icon2 = Parser.GetString(i, 6);
        E.GeosetGroups[0] = Parser.GetUInt(i, 7);
        E.GeosetGroups[1] = Parser.GetUInt(i, 8);
        E.GeosetGroups[2] = Parser.GetUInt(i, 9);
        E.Flags = Parser.GetUInt(i, 10);
        E.SpellVisualID = Parser.GetUInt(i, 11);
        E.GroupSoundIndex = Parser.GetUInt(i, 12);
        E.HelmetGeosetVis[0] = Parser.GetUInt(i, 13);
        E.HelmetGeosetVis[1] = Parser.GetUInt(i, 14);
        for (int32 j = 0; j < 8; ++j)
        {
            E.TextureOverlays[j] = Parser.GetString(i, 15 + j);
        }
        E.ItemVisual = Parser.GetUInt(i, 23);
        E.ParticleColorID = Parser.GetUInt(i, 24);

        IdIndex.Add(E.ID, Entries.Num());
        Entries.Add(MoveTemp(E));
    }

    UE_LOG(LogItemDisplayInfoDbc, Log, TEXT("Loaded ItemDisplayInfo.dbc: %d records"), Entries.Num());
    return true;
}

const FItemDisplayInfoDbcEntry* FItemDisplayInfoDbc::GetById(uint32 ID) const
{
    const int32* Idx = IdIndex.Find(ID);
    return Idx ? &Entries[*Idx] : nullptr;
}
