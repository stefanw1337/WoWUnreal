#include "Formats/Dbc/SkillLineDbc.h"

void FSkillLineDbc::LoadLines(const FDbcParser& P)
{
    if (!P.IsValid() || P.GetFieldCount()<56) return;
    Lines.Reset();
    for (uint32 Row=0; Row<P.GetRecordCount(); ++Row)
    {
        FSkillLineInfo E;
        E.Id=P.GetUInt(Row,0); E.Category=P.GetUInt(Row,1); E.Icon=P.GetUInt(Row,37);
        E.Name=P.GetLocalizedString(Row, 3);
        for (uint32 Locale=4; E.Name.IsEmpty() && Locale<19; ++Locale) E.Name=P.GetString(Row,Locale);
        Lines.Add(E.Id,MoveTemp(E));
    }
    ++Revision;
}
void FSkillLineDbc::LoadAbilities(const FDbcParser& P)
{
    if (!P.IsValid() || P.GetFieldCount()<14) return;
    Abilities.Reset();
    for (auto& Pair:Lines) Pair.Value.ClassMask=0;
    for (uint32 Row=0; Row<P.GetRecordCount(); ++Row)
    {
        FSkillAbilityInfo E;
        E.Skill=P.GetUInt(Row,1); E.RaceMask=P.GetUInt(Row,3); E.ClassMask=P.GetUInt(Row,4);
        E.ExcludeRace=P.GetUInt(Row,5); E.ExcludeClass=P.GetUInt(Row,6); E.NextSpell=P.GetUInt(Row,8);
        Abilities.FindOrAdd(P.GetUInt(Row,2)).Add(E);
        if (auto* Line=Lines.Find(E.Skill)) Line->ClassMask |= E.ClassMask;
    }
    ++Revision;
}
