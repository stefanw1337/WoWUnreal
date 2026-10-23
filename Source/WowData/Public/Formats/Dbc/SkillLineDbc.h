#pragma once
#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FSkillLineInfo
{
    uint32 Id=0, Category=0, Icon=0, ClassMask=0;
    FString Name;
};
struct FSkillAbilityInfo
{
    uint32 Skill=0, RaceMask=0, ClassMask=0, ExcludeRace=0, ExcludeClass=0, NextSpell=0;
};
class WOWDATA_API FSkillLineDbc
{
public:
    void LoadLines(const FDbcParser& Parser);
    void LoadAbilities(const FDbcParser& Parser);
    const TMap<uint32,FSkillLineInfo>& GetLines() const { return Lines; }
    const TArray<FSkillAbilityInfo>* ForSpell(uint32 Id) const { return Abilities.Find(Id); }
    uint32 Revision=0;
private:
    TMap<uint32,FSkillLineInfo> Lines;
    TMap<uint32,TArray<FSkillAbilityInfo>> Abilities;
};
