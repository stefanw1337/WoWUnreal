#include "WowSpellPresentation.h"
#include "WowConnectionManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Internationalization/Regex.h"

FWowSpellUsability FWowSpellPresentation::Usability(UWowConnectionManager* Connection,uint32 Id)
{
    FWowSpellUsability Result;
    if(!Connection || !Connection->PacketHandler.KnownSpells.Contains(Id)) return Result;
    const auto* Player=Connection->PacketHandler.EntityManager.GetLocalPlayer();
    const auto* Spell=FDbcStore::Get().Spells().GetById(Id);
    if(!Player || !Spell || Player->GetHealth()==0) return Result;
    // r254 (Stefan 12.10: as a ghost Lightning Shield and Blood Fury stayed lit and pressing them
    // gave "Can't do that while charmed"): a ghost (PLAYER_FLAGS_GHOST, health 1) uses nothing that
    // is not castable while dead (SPELL_ATTR0_CASTABLE_WHILE_DEAD 0x00800000).
    if((Connection->PacketHandler.DeathState.bGhost || (Player->GetField(PlayerField::FLAGS)&0x10u)) && !(Spell->Attributes&0x00800000u))
    {
        Result.Reason=TEXT("Can't cast as ghost");
        return Result;
    }
    auto HasAura=[&](uint32 AuraId)
    { return Player->Auras.ContainsByPredicate([&](const FAuraInfo& A){return A.bActive && A.SpellId==AuraId;}); };
    const uint32 State=Player->GetField(UnitField::AURA_STATE);
    auto HasState=[&](uint32 Required)
    {
        if(Required==0) return true;
        if(Required<=32 && (State&(1u<<(Required-1)))) return true;
        if(Required==5) // Judgement: Seal auras also cover arrival order of the two server updates.
            for(const auto& A:Player->Auras) if(A.bActive)
                if(const auto* Aura=FDbcStore::Get().Spells().GetById(A.SpellId))
                    if(Aura->SpellFamilyName==10 && (Aura->SpellFamilyFlags[0]&0x08000000u)) return true;
        return false;
    };
    if(!HasState(Spell->CasterAuraState) || (Spell->CasterAuraStateNot && HasState(Spell->CasterAuraStateNot))
        || (Spell->CasterAuraSpell && !HasAura(Spell->CasterAuraSpell))
        || (Spell->ExcludeCasterAuraSpell && HasAura(Spell->ExcludeCasterAuraSpell)))
    {
        Result.Reason=Spell->CasterAuraState==5 ? TEXT("Requires an active Seal.") : TEXT("Required aura or combat condition is missing.");
        return Result;
    }
    if(Spell->Attributes & 0x40u) return Result; // passive spell
    if(Spell->PowerType<=6 && Id!=6603)
    {
        uint32 Cost=Spell->ManaCost;
        if(Spell->ManaCostPercentage)
            Cost+=uint32(uint64(Spell->PowerType==0?Player->GetField(UnitField::BASE_MANA):Player->GetMaxPower(Spell->PowerType))*Spell->ManaCostPercentage/100);
        if(Player->GetPower(Spell->PowerType)<int32(Cost))
        {
            Result.NoResource=true;
            Result.Reason=Spell->PowerType==0?TEXT("Not enough mana."):Spell->PowerType==1?TEXT("Not enough rage."):Spell->PowerType==2?TEXT("Not enough focus.")
                :Spell->PowerType==3?TEXT("Not enough energy."):Spell->PowerType==6?TEXT("Not enough runic power."):TEXT("Not enough resource.");
            return Result;
        }
    }
    Result.Usable=true; return Result;
}

static uint32 GWowHomeAreaId=0;
void FWowSpellPresentation::SetHomeAreaId(uint32 AreaId) { GWowHomeAreaId=AreaId; }

static FString DurationText(int32 Ms)
{
    if(Ms<0) return TEXT("until cancelled");
    return Ms>=60000 ? FString::Printf(TEXT("%g min"),Ms/60000.0) : FString::Printf(TEXT("%g sec"),Ms/1000.0);
}
static FString TokenValue(const FSpellDbc& Dbc, const FSpellDbcEntry* Entry, TCHAR Token, int32 Effect, double Divisor = 1.0)
{
    if(!Entry) return TEXT("varies");
    auto Num=[Divisor](double V){ V/=Divisor; return FMath::IsNearlyEqual(V,FMath::RoundToDouble(V)) ? FString::Printf(TEXT("%lld"),(long long)FMath::RoundToDouble(V)) : FString::Printf(TEXT("%.1f"),V); };
    const int32 Min=FMath::Abs(Entry->EffectBasePoints[Effect]+1);
    const int32 Max=Min+FMath::Max(0,Entry->EffectDieSides[Effect]-1);
    switch(Token)
    {
    case 's': case 'S': return Min==Max?Num(Min):Num(Min)+TEXT(" to ")+Num(Max);
    case 'm': return Num(Min);
    case 'M': return Num(Max);
    case 'd': case 'D': return DurationText(Dbc.DurationMs(Entry->DurationIndex));
    case 't': case 'T': return FString::Printf(TEXT("%g"),Entry->EffectAmplitude[Effect]/1000.0);
    case 'r': case 'R': return FString::Printf(TEXT("%g"),Dbc.MaxRange(Entry->RangeIndex));
    case 'a': case 'A': return FString::Printf(TEXT("%g"),Dbc.Radius(Entry->EffectRadiusIndex[Effect]));
    case 'u': case 'U': return FString::FromInt(Entry->StackAmount);
    case 'n': case 'N': return FString::FromInt(Entry->ProcCharges);           // proc charges ("$n balls")
    case 'h': case 'H': return FString::FromInt(Entry->ProcChance);            // proc chance %
    case 'x': case 'X': return FString::FromInt(Entry->EffectChainTarget[Effect]); // chain targets
    case 'i': case 'I': return FString::FromInt(Entry->MaxAffectedTargets);
    case 'q': case 'Q': return FString::FromInt(Entry->EffectMiscValue[Effect]);
    case 'o': case 'O':
        return Num(Entry->EffectAmplitude[Effect]?double(Min)*Dbc.DurationMs(Entry->DurationIndex)/Entry->EffectAmplitude[Effect]:Min);
    default: return TEXT("varies");
    }
}
FString FWowSpellPresentation::Description(uint32 Id, bool bAura)
{
    const auto& Dbc=FDbcStore::Get().Spells(); const auto* Spell=Dbc.GetById(Id);
    if(!Spell) return FString();
    FString Text=bAura && !Spell->ToolTip.IsEmpty() ? Spell->ToolTip : Spell->Description;
    // "$/1000;S1": the value divided (milliseconds shown as seconds). Spell.dbc placeholders
    // may refer to effects on a different spell ("$12345s1").
    FRegexMatcher Matcher(FRegexPattern(TEXT("\\$(?:/([0-9]+);)?([0-9]*)([sSmMtTodDrRaAuUnNhHxXiIqQ])([1-3]?)")),Text);
    FString Output; int32 Cursor=0;
    while(Matcher.FindNext())
    {
        Output+=Text.Mid(Cursor,Matcher.GetMatchBeginning()-Cursor);
        const FString DivisorText=Matcher.GetCaptureGroup(1);
        const FString Ref=Matcher.GetCaptureGroup(2); const auto* Entry=Ref.IsEmpty()?Spell:Dbc.GetById(FCString::Atoi(*Ref));
        const TCHAR Token=Matcher.GetCaptureGroup(3)[0]; const int32 Effect=FMath::Clamp(FCString::Atoi(*Matcher.GetCaptureGroup(4))-1,0,2);
        const double Divisor=DivisorText.IsEmpty()?1.0:FMath::Max(1.0,FCString::Atod(*DivisorText));
        Output+=TokenValue(Dbc,Entry,Token,Effect,Divisor); Cursor=Matcher.GetMatchEnding();
    }
    Output+=Text.Mid(Cursor);
    // "$z": the hearthstone's home location.
    if(Output.Contains(TEXT("$z"),ESearchCase::CaseSensitive))
    {
        const auto* Area=GWowHomeAreaId?FDbcStore::Get().AreaTable().GetById(GWowHomeAreaId):nullptr;
        Output=Output.Replace(TEXT("$z"),Area && !Area->Name.IsEmpty()?*Area->Name:TEXT("your home location"),ESearchCase::CaseSensitive);
    }
    // "$lball:balls;" picks singular or plural from the number before it.
    {
        FRegexMatcher Plural(FRegexPattern(TEXT("\\$[lL]([^:;]*):([^;]*);")),Output);
        FString Out; int32 C=0;
        while(Plural.FindNext())
        {
            const FString Before=Output.Mid(C,Plural.GetMatchBeginning()-C);
            Out+=Before;
            int32 End=Out.Len()-1; while(End>=0 && !FChar::IsDigit(Out[End])) --End;
            int32 Start=End; while(Start>0 && FChar::IsDigit(Out[Start-1])) --Start;
            const bool bOne=End>=0 && Out.Mid(Start,End-Start+1)==TEXT("1");
            Out+=bOne?Plural.GetCaptureGroup(1):Plural.GetCaptureGroup(2);
            C=Plural.GetMatchEnding();
        }
        Out+=Output.Mid(C); Output=Out;
    }
    // Formula placeholders require live spell coefficients. Keep that limitation readable.
    FRegexMatcher Formula(FRegexPattern(TEXT("\\$\\{[^}]*\\}")),Output);
    FString Resolved; Cursor=0;
    while(Formula.FindNext()) { Resolved+=Output.Mid(Cursor,Formula.GetMatchBeginning()-Cursor)+TEXT("an amount based on your stats"); Cursor=Formula.GetMatchEnding(); }
    Resolved+=Output.Mid(Cursor);
    return Resolved.Replace(TEXT("\r"),TEXT(""));
}
FString FWowSpellPresentation::Tooltip(UWowConnectionManager* Connection,uint32 Id)
{
    const auto& Dbc=FDbcStore::Get().Spells(); const auto* Spell=Dbc.GetById(Id);
    if(!Spell) return TEXT("Spell information is loading...");
    FString Text=Spell->SpellName;
    if(!Spell->Rank.IsEmpty()) Text+=TEXT("  ")+Spell->Rank;
    if(Spell->ManaCost || Spell->ManaCostPercentage)
    {
        const TCHAR* Resource=Spell->PowerType==0?TEXT("mana"):Spell->PowerType==1?TEXT("rage"):Spell->PowerType==2?TEXT("focus")
            :Spell->PowerType==3?TEXT("energy"):Spell->PowerType==6?TEXT("runic power"):TEXT("resource");
        // Rage and runic power costs are stored in tenths (Battle Shout: 100 = 10 rage).
        const uint32 Cost=(Spell->PowerType==1 || Spell->PowerType==6) ? Spell->ManaCost/10 : Spell->ManaCost;
        Text+=Spell->ManaCostPercentage ? FString::Printf(TEXT("\n%u%% base %s"),Spell->ManaCostPercentage,Resource)
            : FString::Printf(TEXT("\n%u %s"),Cost,Resource);
    }
    const int32 CastTime=Dbc.CastTimeMs(Spell->CastingTimeIndex);
    Text+=CastTime>0 ? FString::Printf(TEXT("\n%g sec cast"),CastTime/1000.0) : TEXT("\nInstant");
    const float Range=Dbc.MaxRange(Spell->RangeIndex);
    if(Range>0) Text+=FString::Printf(TEXT("   %g yd range"),Range);
    const uint32 Recovery=FMath::Max(Spell->RecoveryTime,Spell->CategoryRecoveryTime);
    if(Recovery>0) Text+=TEXT("\n")+DurationText(Recovery)+TEXT(" cooldown");
    Text+=TEXT("\n\n")+Description(Id);
    if(Connection) { const auto State=Usability(Connection,Id); if(!State.Reason.IsEmpty()) Text+=TEXT("\n\n")+State.Reason; }
    return Text;
}

FWowSpellTooltipParts FWowSpellPresentation::TooltipParts(UWowConnectionManager* Connection,uint32 Id)
{
    FWowSpellTooltipParts Parts;
    const auto& Dbc=FDbcStore::Get().Spells(); const auto* Spell=Dbc.GetById(Id);
    if(!Spell) { Parts.Name=TEXT("Spell information is loading..."); return Parts; }
    Parts.bValid=true;
    Parts.Name=Spell->SpellName;
    Parts.Rank=Spell->Rank;
    if(Spell->ManaCost || Spell->ManaCostPercentage)
    {
        const TCHAR* Resource=Spell->PowerType==0?TEXT("Mana"):Spell->PowerType==1?TEXT("Rage"):Spell->PowerType==2?TEXT("Focus")
            :Spell->PowerType==3?TEXT("Energy"):Spell->PowerType==6?TEXT("Runic Power"):TEXT("Power");
        const uint32 Cost=(Spell->PowerType==1 || Spell->PowerType==6) ? Spell->ManaCost/10 : Spell->ManaCost;
        Parts.Cost=Spell->ManaCostPercentage ? FString::Printf(TEXT("%u%% of base %s"),Spell->ManaCostPercentage,Resource)
            : FString::Printf(TEXT("%u %s"),Cost,Resource);
    }
    const float Range=Dbc.MaxRange(Spell->RangeIndex);
    if(Range>0) Parts.Range=Range<=5.f ? FString(TEXT("Melee Range")) : FString::Printf(TEXT("%g yd range"),Range);
    const int32 CastTime=Dbc.CastTimeMs(Spell->CastingTimeIndex);
    Parts.Cast=CastTime>0 ? FString::Printf(TEXT("%g sec cast"),CastTime/1000.0) : FString(TEXT("Instant"));
    const uint32 Recovery=FMath::Max(Spell->RecoveryTime,Spell->CategoryRecoveryTime);
    if(Recovery>0) Parts.Cooldown=DurationText(Recovery)+TEXT(" cooldown");
    Parts.Description=Description(Id);
    if(Connection) { const auto State=Usability(Connection,Id); Parts.Reason=State.Reason; }
    return Parts;
}

int32 FWowSpellPresentation::InRange(UWowConnectionManager* Connection,uint32 Id,uint64 TargetGuid)
{
    if (!Connection) return -1;
    const auto& Dbc=FDbcStore::Get().Spells();
    const auto* Spell=Dbc.GetById(Id);
    if (!Spell) return -1;
    bool Enemy=Id==6603, Ally=false, Any=false;
    for (int I=0;I<3;++I)
        for (uint32 Type : {Spell->EffectImplicitTargetA[I],Spell->EffectImplicitTargetB[I]})
        { Enemy|=Type==6; Ally|=Type==21; Any|=Type==25; }
    if (!Enemy && !Ally && !Any) return -1;
    auto& Entities=Connection->PacketHandler.EntityManager;
    const auto* Player=Entities.GetLocalPlayer();
    const auto* Target=Entities.Find(TargetGuid?TargetGuid:Connection->GetTargetGuid());
    if (!Player || !Target || !Target->IsUnit()) return -1;
    const bool Friendly=Connection->GetUnitReaction(Target,Player)>4;
    if ((Enemy && !Ally && !Any && Friendly) || (Ally && !Enemy && !Any && !Friendly)) return -1;
    const float Range=Id==6603?5.f:Dbc.MaxRange(Spell->RangeIndex,Friendly);
    if (Range<=0) return -1;
    // Network positions and DBC ranges are both in WoW yards.
    if (FVector::Dist(Player->Movement.Position,Target->Movement.Position)>Range) return 0;
    // Out of line of sight shows like out of range (red), only known for the current target.
    if (!TargetGuid && !IsTargetInSight()) return 0;
    return 1;
}

static bool GWowTargetInSight=true;
void FWowSpellPresentation::SetTargetInSight(bool bInSight) { GWowTargetInSight=bInSight; }
bool FWowSpellPresentation::IsTargetInSight() { return GWowTargetInSight; }
