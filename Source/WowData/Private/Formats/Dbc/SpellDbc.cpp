#include "Formats/Dbc/SpellDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpellDbc, Log, All);

bool FSpellDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid())
    {
        return false;
    }

    Entries.Reserve(Parser.GetRecordCount());

    for (uint32 i = 0; i < Parser.GetRecordCount(); ++i)
    {
        FSpellDbcEntry E;

        E.ID = Parser.GetUInt(i, 0);
        E.Category = Parser.GetUInt(i, 1);
        E.Dispel = Parser.GetUInt(i, 2);
        E.Mechanic = Parser.GetUInt(i, 3);
        E.Attributes = Parser.GetUInt(i, 4);
        E.AttributesEx = Parser.GetUInt(i, 5);
        E.AttributesEx2 = Parser.GetUInt(i, 6);
        E.AttributesEx3 = Parser.GetUInt(i, 7);
        E.AttributesEx4 = Parser.GetUInt(i, 8);
        E.AttributesEx5 = Parser.GetUInt(i, 9);
        E.AttributesEx6 = Parser.GetUInt(i, 10);
        E.AttributesEx7 = Parser.GetUInt(i, 11);

        // Combine uint64 fields from two uint32 columns
        E.Stances = static_cast<uint64>(Parser.GetUInt(i, 12)) | (static_cast<uint64>(Parser.GetUInt(i, 13)) << 32);
        E.StancesNot = static_cast<uint64>(Parser.GetUInt(i, 14)) | (static_cast<uint64>(Parser.GetUInt(i, 15)) << 32);

        E.Targets = Parser.GetUInt(i, 16);
        E.TargetCreatureType = Parser.GetUInt(i, 17);
        E.RequiresSpellFocus = Parser.GetUInt(i, 18);
        E.FacingCasterFlags = Parser.GetUInt(i, 19);
        E.CasterAuraState = Parser.GetUInt(i, 20);
        E.TargetAuraState = Parser.GetUInt(i, 21);
        E.CasterAuraStateNot=Parser.GetUInt(i,22); E.CasterAuraSpell=Parser.GetUInt(i,24);
        E.ExcludeCasterAuraSpell=Parser.GetUInt(i,26);
        for(int32 J=0;J<3;++J) { E.SpellFamilyFlags[J]=Parser.GetUInt(i,209+J); E.EffectDieSides[J]=static_cast<int32>(Parser.GetUInt(i,74+J)); E.EffectAmplitude[J]=Parser.GetUInt(i,98+J); }
        // Tooltip tokens $h / $n / $x / $i (3.3.5 Spell.dbc columns).
        E.ProcChance = Parser.GetUInt(i, 35);
        E.ProcCharges = Parser.GetUInt(i, 36);
        E.MaxAffectedTargets = Parser.GetUInt(i, 212);
        for (int32 J = 0; J < 3; ++J) { E.EffectChainTarget[J] = Parser.GetUInt(i, 104 + J); E.EffectRadiusIndex[J] = Parser.GetUInt(i, 92 + J); }
        E.CastingTimeIndex = Parser.GetUInt(i, 28);
        E.RecoveryTime = Parser.GetUInt(i, 29);
        E.CategoryRecoveryTime = Parser.GetUInt(i, 30);
        E.MaxLevel = Parser.GetUInt(i, 37);
        E.BaseLevel = Parser.GetUInt(i, 38);
        E.SpellLevel = Parser.GetUInt(i, 39);
        E.DurationIndex = Parser.GetUInt(i, 40);
        E.PowerType = Parser.GetUInt(i, 41);
        E.ManaCost = Parser.GetUInt(i, 42);
        E.RangeIndex = Parser.GetUInt(i, 46);
        E.Speed = Parser.GetFloat(i, 47);
        E.StackAmount = Parser.GetUInt(i, 49);

        // Effect arrays
        for (int32 j = 0; j < 3; ++j)
        {
            E.Effect[j] = Parser.GetUInt(i, 71 + j);
            E.EffectBasePoints[j] = static_cast<int32>(Parser.GetUInt(i, 80 + j));
            E.EffectImplicitTargetA[j] = Parser.GetUInt(i, 86 + j);
            E.EffectImplicitTargetB[j] = Parser.GetUInt(i, 89 + j);
            E.EffectApplyAuraName[j] = Parser.GetUInt(i, 95 + j);
            E.EffectMiscValue[j] = static_cast<int32>(Parser.GetUInt(i, 110 + j));
            E.EffectTriggerSpell[j] = Parser.GetUInt(i, 116 + j);
        }

        E.SpellVisual[0] = Parser.GetUInt(i, 131);
        E.SpellVisual[1] = Parser.GetUInt(i, 132);
        E.SpellIconID = Parser.GetUInt(i, 133);
        E.ActiveIconID = Parser.GetUInt(i, 134);
        E.SpellName = Parser.GetLocalizedString(i, 136);
        E.Rank = Parser.GetLocalizedString(i, 153);
        E.Description = Parser.GetLocalizedString(i, 170);
        E.ToolTip = Parser.GetLocalizedString(i, 187);
        E.ManaCostPercentage = Parser.GetUInt(i, 204);
        E.StartRecoveryCategory = Parser.GetUInt(i, 205);
        E.StartRecoveryTime = Parser.GetUInt(i, 206);
        E.SpellFamilyName = Parser.GetUInt(i, 208);
        E.DmgClass = Parser.GetUInt(i, 213);
        E.SchoolMask = Parser.GetUInt(i, 225);

        IdIndex.Add(E.ID, Entries.Num());
        Entries.Add(MoveTemp(E));
    }

    UE_LOG(LogSpellDbc, Log, TEXT("Loaded Spell.dbc: %d records"), Entries.Num());
    return true;
}

const FSpellDbcEntry* FSpellDbc::GetById(uint32 ID) const
{
    const int32* Idx = IdIndex.Find(ID);
    return Idx ? &Entries[*Idx] : nullptr;
}
void FSpellDbc::LoadTiming(const FDbcParser& Parser,int32 Kind)
{
    if(!Parser.IsValid()) return;
    for(uint32 I=0;I<Parser.GetRecordCount();++I)
    {
        if(Kind==0) Durations.Add(Parser.GetUInt(I,0),static_cast<int32>(Parser.GetUInt(I,1)));
        else if(Kind==1) CastTimes.Add(Parser.GetUInt(I,0),static_cast<int32>(Parser.GetUInt(I,1)));
        else if(Kind==3) Radii.Add(Parser.GetUInt(I,0),Parser.GetFloat(I,1)); // SpellRadius.dbc: id, radius, per level, max
        else { Ranges.Add(Parser.GetUInt(I,0),Parser.GetFloat(I,3)); FriendlyRanges.Add(Parser.GetUInt(I,0),Parser.GetFloat(I,4)); }
    }
}

void FSpellDbc::LoadLocks(const FDbcParser& Parser)
{
    Locks.Reset();
    for (uint32 Row=0; Row<Parser.GetRecordCount(); ++Row)
    {
        auto& Cases = Locks.Add(Parser.GetUInt(Row,0));
        for (uint32 I=0; I<8; ++I)
            if (Parser.GetUInt(Row,1+I)) Cases.Emplace(Parser.GetUInt(Row,1+I),Parser.GetUInt(Row,9+I));
    }
}

void FSpellDbc::LoadForms(const FDbcParser& P)
{
 FormBars.Reset();if(P.GetFieldCount()<2)return;
 for(uint32 I=0;I<P.GetRecordCount();++I)FormBars.Add(P.GetUInt(I,0),FMath::Clamp(int32(P.GetUInt(I,1)),0,4));
}
