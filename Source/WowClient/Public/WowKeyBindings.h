#pragma once
#include "CoreMinimal.h"
struct FWowBindingAction { FString Command, Label, Category; int32 ActionSlot=-1; };
struct FWowBindingKeys { FString Primary, Secondary; };
using FWowBindingMap = TMap<FString, FWowBindingKeys>;
struct FWowBindingPresetSet
{
    FWowBindingMap Presets[2];
    int32 Selected=0;
};
struct FWowBindingSnapshot
{
    FWowBindingPresetSet Account, Character;
    bool CharacterSpecific=false;
};
class WOWCLIENT_API FWowKeyBindings
{
public:
    static const TArray<FWowBindingAction>& Actions();
    static TArray<FString> Keys(const FString& Command);
    static FString Command(const FString& Key);
    static bool Bind(const FString& Key,const FString& Command,int32 Slot=-1);
    static void Unbind(const FString& Command,int32 Slot);
    static void Save();
    static void Reload(bool Defaults=false);
    static void UsePreset(int32 Preset); // 1 = standard, 2 = user's screenshots; empty fields remain empty.
    static int32 CurrentPreset();
    static void ResetPreset();
    static FWowBindingSnapshot Snapshot();
    static void Restore(const FWowBindingSnapshot& State);
    static void SetCharacter(const FString& Identity);
    static bool CanUseCharacterProfile();
    static bool IsCharacterSpecific();
    static void SetCharacterSpecific(bool Enabled);
    static FString KeyName(const FString& UnrealName,bool Ctrl=false,bool Shift=false,bool Alt=false);
    static FString Press(const FString& Key);
    static void SetCommandHeld(const FString& Command,bool Down);
    static TArray<FString> Release(const FString& BaseKey);
    static bool Held(const FString& Command);
    static void ReleaseAll();
    static int32 ActionPage();
    static int32 ManualActionPage();
    static int32 BonusBarOffset();
    static bool SetBonusBarOffset(int32 Offset);
    static void SetActionPage(int32 Page); // r373: 0 = page 1 without the bonus bar (stealth/stance)
    /** r373: the page the page arrows / NEXTACTIONPAGE step to (0 = page 1 without the bonus bar);
     *  pages for which bShown is true are skipped (already on screen as extra bars). */
    static int32 StepActionPage(bool bNext, const TFunction<bool(int32)>& bShown);
private:
    static void Initialize();
};
