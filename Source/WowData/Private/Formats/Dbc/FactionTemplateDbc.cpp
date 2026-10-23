#include "Formats/Dbc/FactionTemplateDbc.h"
bool FFactionTemplateDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid() || Parser.GetFieldCount()<14) return false;
    Entries.Reset();
    for(uint32 I=0;I<Parser.GetRecordCount();++I)
    {
        FWowFactionTemplate E;
        E.Faction=Parser.GetUInt(I,1); E.Flags=Parser.GetUInt(I,2);
        E.Group=Parser.GetUInt(I,3); E.Friends=Parser.GetUInt(I,4); E.Enemies=Parser.GetUInt(I,5);
        for(uint32 J=0;J<4;++J) { E.EnemyFactions[J]=Parser.GetUInt(I,6+J); E.FriendFactions[J]=Parser.GetUInt(I,10+J); }
        Entries.Add(Parser.GetUInt(I,0),E);
    }
    return true;
}
int32 FFactionTemplateDbc::Reaction(uint32 From,uint32 To) const
{
    const auto* A=Entries.Find(From); const auto* B=Entries.Find(To);
    if(!A || !B) return 4;
    if(From==To || (A->Faction && A->Faction==B->Faction)) return 5;
    if(B->Faction)
    {
        for(uint32 F:A->EnemyFactions) if(F==B->Faction) return 1;
        for(uint32 F:A->FriendFactions) if(F==B->Faction) return 5;
    }
    if(A->Enemies & B->Group) return 1;
    return ((A->Friends & B->Group) || (B->Friends & A->Group)) ? 5 : 4;
}
