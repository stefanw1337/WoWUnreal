#pragma once
#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FSpellDbcEntry
{
    uint32 ID;
    uint32 Category;
    uint32 Dispel;
    uint32 Mechanic;
    uint32 Attributes;
    uint32 AttributesEx;
    uint32 AttributesEx2;
    uint32 AttributesEx3;
    uint32 AttributesEx4;
    uint32 AttributesEx5;
    uint32 AttributesEx6;
    uint32 AttributesEx7;
    uint64 Stances;
    uint64 StancesNot;
    uint32 Targets;
    uint32 TargetCreatureType;
    uint32 RequiresSpellFocus;
    uint32 FacingCasterFlags;
    uint32 CasterAuraState;
    uint32 CasterAuraStateNot=0, CasterAuraSpell=0, ExcludeCasterAuraSpell=0;
    uint32 SpellFamilyFlags[3]={};
    int32 EffectDieSides[3]={};
    uint32 EffectAmplitude[3]={};
    uint32 ProcChance=0, ProcCharges=0, MaxAffectedTargets=0;
    uint32 EffectChainTarget[3]={};
    uint32 EffectRadiusIndex[3]={}; // SpellRadius.dbc, tooltip token $a
    uint32 TargetAuraState;
    uint32 CastingTimeIndex;
    uint32 StartRecoveryCategory=0, StartRecoveryTime=0;
    uint32 RecoveryTime;
    uint32 CategoryRecoveryTime;
    uint32 MaxLevel;
    uint32 BaseLevel;
    uint32 SpellLevel;
    uint32 DurationIndex;
    uint32 PowerType;
    uint32 ManaCost;
    uint32 RangeIndex;
    float Speed;
    uint32 StackAmount;
    uint32 Effect[3];
    int32 EffectBasePoints[3];
    uint32 EffectImplicitTargetA[3];
    uint32 EffectImplicitTargetB[3];
    uint32 EffectApplyAuraName[3];
    int32 EffectMiscValue[3];
    uint32 EffectTriggerSpell[3];
    uint32 SpellVisual[2];
    uint32 SpellIconID;
    uint32 ActiveIconID;
    FString SpellName;
    FString Rank;
    FString Description;
    FString ToolTip;
    uint32 ManaCostPercentage;
    uint32 SpellFamilyName;
    uint32 DmgClass;
    uint32 SchoolMask;
};

class WOWDATA_API FSpellDbc
{
public:
    bool Load(const FDbcParser& Parser);
    void LoadLocks(const FDbcParser& Parser);
    void LoadForms(const FDbcParser& Parser);
    int32 BonusBarForForm(uint32 Form)const{return FormBars.FindRef(Form);}
    TMap<uint32,int32> FormBars;
    TMap<uint32, TArray<TPair<uint32,uint32>>> Locks;
    void LoadTiming(const FDbcParser& Parser, int32 Kind);
    int32 DurationMs(uint32 Id) const { return Durations.FindRef(Id); }
    int32 CastTimeMs(uint32 Id) const { return CastTimes.FindRef(Id); }
    float Radius(uint32 Id) const { return Radii.FindRef(Id); }
    float MaxRange(uint32 Id, bool Friendly = false) const { return Friendly ? FriendlyRanges.FindRef(Id) : Ranges.FindRef(Id); }
    const FSpellDbcEntry* GetById(uint32 ID) const;
    const TArray<FSpellDbcEntry>& GetAll() const { return Entries; }
    int32 Num() const { return Entries.Num(); }

private:
    TMap<uint32,int32> Durations, CastTimes;
    TMap<uint32,float> Ranges, FriendlyRanges, Radii;
    TArray<FSpellDbcEntry> Entries;
    TMap<uint32, int32> IdIndex;
};