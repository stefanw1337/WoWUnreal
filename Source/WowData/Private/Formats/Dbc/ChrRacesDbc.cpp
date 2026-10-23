#include "Formats/Dbc/ChrRacesDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogChrRacesDbc, Log, All);

bool FChrRacesDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid()) return false;

    Entries.Reset();
    IdIndex.Reset();
    Entries.Reserve(Parser.GetRecordCount());

    for (uint32 i = 0; i < Parser.GetRecordCount(); ++i)
    {
        FChrRacesDbcEntry E;
        E.ID = Parser.GetUInt(i, 0);
        E.Flags = Parser.GetUInt(i, 1);
        E.FactionID = Parser.GetUInt(i, 2);
        E.ExplorationSoundID = Parser.GetUInt(i, 3);
        E.MaleModelID = Parser.GetUInt(i, 4);
        E.FemaleModelID = Parser.GetUInt(i, 5);
        E.ClientPrefix = Parser.GetString(i, 6);
        E.BaseLanguage = Parser.GetUInt(i, 7);
        E.CreatureTypeID = Parser.GetUInt(i, 8);
        E.ResSicknessSpellID = Parser.GetUInt(i, 9);
        E.SplashSoundID = Parser.GetUInt(i, 10);
        E.ClientFileString = Parser.GetString(i, 11);
        E.CinematicSequenceID = Parser.GetUInt(i, 12);
        E.FactionGroup = Parser.GetUInt(i, 13);
        E.Name = Parser.GetLocalizedString(i, 14);
        E.FemaleName = Parser.GetLocalizedString(i, 31);
        E.MaleName = Parser.GetLocalizedString(i, 48);
        E.FacialHairCustomization1 = Parser.GetString(i, 65);
        E.FacialHairCustomization2 = Parser.GetString(i, 66);
        E.HairCustomization = Parser.GetString(i, 67);
        E.RequiredExpansion = Parser.GetUInt(i, 68);

        IdIndex.Add(E.ID, Entries.Num());
        Entries.Add(MoveTemp(E));
    }

    UE_LOG(LogChrRacesDbc, Log, TEXT("Loaded ChrRaces.dbc: %d records"), Entries.Num());
    return true;
}

const FChrRacesDbcEntry* FChrRacesDbc::GetById(uint32 ID) const
{
    const int32* Idx = IdIndex.Find(ID);
    return Idx ? &Entries[*Idx] : nullptr;
}
