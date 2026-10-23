#include "WowUnitSounds.h"
#include "Mpq/MpqManager.h"
#include "Formats/DbcParser.h"
#include "Formats/Dbc/DbcStore.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowUnitSounds, Log, All);

namespace
{
struct FCreatureSoundRow
{
    uint32 Exertion = 0, ExertionCritical = 0, Injury = 0, InjuryCritical = 0, InjuryCrushing = 0;
    uint32 Death = 0, Stun = 0, Stand = 0, Footstep = 0, Aggro = 0, Alert = 0;
    uint32 Fidget[5] = {};
    uint32 CustomAttack[4] = {};
    uint32 NpcSoundId = 0, Birth = 0, SpellCastDirected = 0;
    int32 ImpactType = 0;
};
struct FImpactRow { int32 Subclass = -1; uint32 Impact[10] = {}; uint32 Crit[10] = {}; };
struct FSwingRow { int32 Type = 0; bool bCrit = false; uint32 Sound = 0; };

bool bLoaded = false;
TMap<uint32, FCreatureSoundRow> CreatureSounds;
TMap<uint32, TArray<uint32>> NpcSounds;
TArray<FImpactRow> Impacts;
TArray<FSwingRow> Swings;
FRandomStream Random(FPlatformTime::Cycles());

bool ReadDbc(FMpqManager* Mpq, const TCHAR* Path, FDbcParser& Parser)
{
    TArray<uint8> Data;
    if (!Mpq || !Mpq->ReadFile(Path, Data) || !Parser.Parse(Data) || !Parser.IsValid())
    {
        UE_LOG(LogWowUnitSounds, Warning, TEXT("Unit sounds: cannot read %s"), Path);
        return false;
    }
    return true;
}

const FCreatureSoundRow* FindCreatureRow(uint32 DisplayId)
{
    const FCreatureDisplayInfoDbcEntry* Display = FDbcStore::Get().CreatureDisplayInfo().GetById(DisplayId);
    if (!Display) return nullptr;
    uint32 SoundDataId = Display->SoundID;
    if (SoundDataId == 0)
        if (const FCreatureModelDataDbcEntry* Model = FDbcStore::Get().CreatureModelData().GetById(Display->ModelID))
            SoundDataId = Model->SoundDataID;
    return SoundDataId ? CreatureSounds.Find(SoundDataId) : nullptr;
}

uint32 PickNonZero(const uint32* Values, int32 Count, int32 Variant)
{
    if (Variant >= 0 && Variant < Count && Values[Variant]) return Values[Variant];
    TArray<uint32, TInlineAllocator<5>> Valid;
    for (int32 I = 0; I < Count; ++I) if (Values[I]) Valid.Add(Values[I]);
    return Valid.Num() ? Valid[Random.RandRange(0, Valid.Num() - 1)] : 0;
}
}

void FWowUnitSounds::Reset()
{
    bLoaded = false;
    CreatureSounds.Reset();
    NpcSounds.Reset();
    Impacts.Reset();
    Swings.Reset();
}

void FWowUnitSounds::EnsureLoaded(FMpqManager* Mpq)
{
    if (bLoaded || !Mpq || !Mpq->IsInitialized()) return;
    bLoaded = true;

    FDbcParser P;
    // CreatureSoundData.dbc (3.3.5a, 38 columns).
    if (ReadDbc(Mpq, TEXT("DBFilesClient\\CreatureSoundData.dbc"), P) && P.GetFieldCount() >= 26)
    {
        for (uint32 R = 0; R < P.GetRecordCount(); ++R)
        {
            FCreatureSoundRow Row;
            Row.Exertion = P.GetUInt(R, 1);
            Row.ExertionCritical = P.GetUInt(R, 2);
            Row.Injury = P.GetUInt(R, 3);
            Row.InjuryCritical = P.GetUInt(R, 4);
            Row.InjuryCrushing = P.GetUInt(R, 5);
            Row.Death = P.GetUInt(R, 6);
            Row.Stun = P.GetUInt(R, 7);
            Row.Stand = P.GetUInt(R, 8);
            Row.Footstep = P.GetUInt(R, 9);
            Row.Aggro = P.GetUInt(R, 10);
            Row.Alert = P.GetUInt(R, 13);
            for (int32 I = 0; I < 5; ++I) Row.Fidget[I] = P.GetUInt(R, 14 + I);
            for (int32 I = 0; I < 4; ++I) Row.CustomAttack[I] = P.GetUInt(R, 19 + I);
            Row.NpcSoundId = P.GetUInt(R, 23);
            Row.ImpactType = static_cast<int32>(P.GetUInt(R, 25));
            if (P.GetFieldCount() >= 35)
            {
                Row.Birth = P.GetUInt(R, 33);
                Row.SpellCastDirected = P.GetUInt(R, 34);
            }
            CreatureSounds.Add(P.GetUInt(R, 0), Row);
        }
        UE_LOG(LogWowUnitSounds, Log, TEXT("CreatureSoundData: %d rows (%u columns)"), CreatureSounds.Num(), P.GetFieldCount());
    }
    FDbcParser N;
    if (ReadDbc(Mpq, TEXT("DBFilesClient\\NPCSounds.dbc"), N) && N.GetFieldCount() >= 4)
    {
        for (uint32 R = 0; R < N.GetRecordCount(); ++R)
            NpcSounds.Add(N.GetUInt(R, 0), {N.GetUInt(R, 1), N.GetUInt(R, 2), N.GetUInt(R, 3)});
        UE_LOG(LogWowUnitSounds, Log, TEXT("NPCSounds: %d rows"), NpcSounds.Num());
    }
    FDbcParser I;
    if (ReadDbc(Mpq, TEXT("DBFilesClient\\WeaponImpactSounds.dbc"), I) && I.GetFieldCount() >= 23)
    {
        for (uint32 R = 0; R < I.GetRecordCount(); ++R)
        {
            FImpactRow Row;
            Row.Subclass = static_cast<int32>(I.GetUInt(R, 1));
            for (int32 K = 0; K < 10; ++K) { Row.Impact[K] = I.GetUInt(R, 3 + K); Row.Crit[K] = I.GetUInt(R, 13 + K); }
            Impacts.Add(Row);
        }
        UE_LOG(LogWowUnitSounds, Log, TEXT("WeaponImpactSounds: %d rows"), Impacts.Num());
    }
    FDbcParser S;
    if (ReadDbc(Mpq, TEXT("DBFilesClient\\WeaponSwingSounds2.dbc"), S) && S.GetFieldCount() >= 4)
    {
        for (uint32 R = 0; R < S.GetRecordCount(); ++R)
            Swings.Add({static_cast<int32>(S.GetUInt(R, 1)), S.GetUInt(R, 2) != 0, S.GetUInt(R, 3)});
        UE_LOG(LogWowUnitSounds, Log, TEXT("WeaponSwingSounds2: %d rows"), Swings.Num());
    }
}

uint32 FWowUnitSounds::GetCreatureSound(uint32 DisplayId, EWowCreatureSound Kind, int32 Variant)
{
    const FCreatureSoundRow* Row = FindCreatureRow(DisplayId);
    if (!Row) return 0;
    switch (Kind)
    {
    case EWowCreatureSound::Exertion: return Row->Exertion;
    case EWowCreatureSound::ExertionCritical: return Row->ExertionCritical ? Row->ExertionCritical : Row->Exertion;
    case EWowCreatureSound::Injury: return Row->Injury;
    case EWowCreatureSound::InjuryCritical: return Row->InjuryCritical ? Row->InjuryCritical : Row->Injury;
    case EWowCreatureSound::InjuryCrushing: return Row->InjuryCrushing ? Row->InjuryCrushing : Row->Injury;
    case EWowCreatureSound::Death: return Row->Death;
    case EWowCreatureSound::Stun: return Row->Stun;
    case EWowCreatureSound::Stand: return Row->Stand;
    case EWowCreatureSound::Footstep: return Row->Footstep;
    case EWowCreatureSound::Aggro: return Row->Aggro;
    case EWowCreatureSound::Alert: return Row->Alert;
    case EWowCreatureSound::Fidget: return PickNonZero(Row->Fidget, 5, Variant);
    case EWowCreatureSound::CustomAttack: return PickNonZero(Row->CustomAttack, 4, Variant);
    case EWowCreatureSound::Birth: return Row->Birth;
    case EWowCreatureSound::SpellCastDirected: return Row->SpellCastDirected;
    }
    return 0;
}

int32 FWowUnitSounds::GetImpactType(uint32 DisplayId)
{
    const FCreatureSoundRow* Row = FindCreatureRow(DisplayId);
    return Row ? FMath::Clamp(Row->ImpactType, 0, 9) : 0;
}

uint32 FWowUnitSounds::GetNpcSound(uint32 DisplayId, EWowNpcSound Kind)
{
    const FCreatureDisplayInfoDbcEntry* Display = FDbcStore::Get().CreatureDisplayInfo().GetById(DisplayId);
    uint32 NpcId = Display ? Display->NPCSoundID : 0;
    if (!NpcId)
        if (const FCreatureSoundRow* Row = FindCreatureRow(DisplayId)) NpcId = Row->NpcSoundId;
    const TArray<uint32>* Sounds = NpcId ? NpcSounds.Find(NpcId) : nullptr;
    const int32 Index = static_cast<int32>(Kind);
    if (!Sounds || !Sounds->IsValidIndex(Index)) return 0;
    // Pissed falls back to the greeting like the original client.
    return (*Sounds)[Index] ? (*Sounds)[Index] : (Kind == EWowNpcSound::Pissed ? (*Sounds)[0] : 0);
}

uint32 FWowUnitSounds::GetWeaponSwing(int32 SwingType, bool bCrit)
{
    TArray<uint32, TInlineAllocator<8>> Pool;
    for (const FSwingRow& Row : Swings) if (Row.Type == SwingType && Row.bCrit == bCrit && Row.Sound) Pool.Add(Row.Sound);
    if (Pool.Num() == 0 && bCrit) return GetWeaponSwing(SwingType, false);
    return Pool.Num() ? Pool[Random.RandRange(0, Pool.Num() - 1)] : 0;
}

uint32 FWowUnitSounds::GetWeaponImpact(int32 WeaponSubclass, int32 ImpactType, bool bCrit)
{
    const int32 Subclass = WeaponSubclass < 0 ? 13 : WeaponSubclass; // unarmed hits use the fist rows
    const int32 Material = FMath::Clamp(ImpactType, 0, 9);
    for (const FImpactRow& Row : Impacts)
    {
        if (Row.Subclass != Subclass) continue;
        const uint32* Set = bCrit ? Row.Crit : Row.Impact;
        if (Set[Material]) return Set[Material];
        for (int32 K = 0; K < 10; ++K) if (Set[K]) return Set[K];
    }
    return 0;
}

int32 FWowUnitSounds::GetWeaponSubclass(uint32 ItemEntry)
{
    const FItemDbcEntry* Item = ItemEntry ? FDbcStore::Get().Items().GetById(ItemEntry) : nullptr;
    if (!Item || Item->ClassID != 2) return -1;
    return Item->SoundOverrideSubclassID > 0 ? Item->SoundOverrideSubclassID : static_cast<int32>(Item->SubclassID);
}

int32 FWowUnitSounds::GetSwingType(int32 WeaponSubclass)
{
    switch (WeaponSubclass)
    {
    case 1: case 5: case 6: case 8: case 10: case 17: case 20: return 2; // two-handers, staves, polearms
    case 13: case 15: case -1: return 0;                                // fist, dagger, unarmed
    default: return 1;
    }
}
