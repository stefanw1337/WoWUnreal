#include "WowKeyBindings.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/SecureHash.h"
namespace
{
TArray<FWowBindingAction> Catalog;
FWowBindingPresetSet Account, Character;
TMap<FString,FString> Lookup, Pressed;
bool Loaded=false, CharacterSpecific=false;
FString CharacterSection;
int32 Page=1, BonusOffset=0;
// r373 (Stefan 16.10: "in stealth I can only go to bar 2 and bar 7 - I want back to bar 1"): page 1
// shown without the bonus bar while one is active. Cleared when the bonus bar changes (stealth/stance).
bool bPlainPageOne=false;
const TCHAR* Section=TEXT("WowUnreal.KeyBindings");
FWowBindingPresetSet& CurrentSet(){return CharacterSpecific ? Character : Account;}
FWowBindingMap& Active(){auto& Set=CurrentSet();return Set.Presets[Set.Selected];}
FString CanonicalCommand(const FString& In)
{
    if(In==TEXT("SITSTAND")) return TEXT("SITORSTAND");
    if(In==TEXT("TOGGLENAMEPLATES")) return TEXT("NAMEPLATES");
    return In.ToUpper();
}
void Reindex()
{
    Lookup.Reset(); Pressed.Reset();
    for(const auto& Pair:Active())
    {
        if(!Pair.Value.Primary.IsEmpty()) Lookup.Add(Pair.Value.Primary,Pair.Key);
        if(!Pair.Value.Secondary.IsEmpty()) Lookup.Add(Pair.Value.Secondary,Pair.Key);
    }
}
FWowBindingMap Preset(int32 Id)
{
    FWowBindingMap Result;
    auto Add=[&](const TCHAR* Command,const TCHAR* First,const TCHAR* Second){Result.Add(Command,{First,Second});};
    #include "WowReferenceBindingKeys.inl"
    // Requested RP-walk shortcut; keep the original numpad divide binding too.
    Result.Add(TEXT("TOGGLERUN"), {TEXT("NUMPADDIVIDE"), TEXT("BACKSPACE")});
    Result.Add(TEXT("SWIMDOWN"), {TEXT("RCTRL"), TEXT("LCTRL")});
    Result.Add(TEXT("TOGGLETORCH"), {TEXT("CTRL-T"), FString()}); // r398 belt torch (T = Attack Target, Shift-T = Pet Attack)
    // r620 isometric camera. Middle mouse (BUTTON3) belongs to Move and Steer by default; while it does, the
    // isometric camera still turns with it (Free Turn Camera only needs a key when middle mouse is used for something else).
    Result.Add(TEXT("ISOCAMERATURN"), {FString(), FString()});
    Result.Add(TEXT("ISOCAMERAROTATELEFT"), {TEXT(","), FString()});
    Result.Add(TEXT("ISOCAMERAROTATERIGHT"), {TEXT("."), FString()});
    if(Id==1)
    {
        // Original movement/UI defaults; preset 2 retains the user's deliberate changes.
        Result.Add(TEXT("TURNLEFT"),{TEXT("A"),TEXT("LEFT")});
        Result.Add(TEXT("TURNRIGHT"),{TEXT("D"),TEXT("RIGHT")});
        Result.Add(TEXT("STRAFELEFT"),{TEXT("Q"),FString()});
        Result.Add(TEXT("STRAFERIGHT"),{TEXT("E"),FString()});
        Result.Add(TEXT("TOGGLEAUTORUN"),{TEXT("NUMLOCK"),TEXT("BUTTON4")});
        Result.Add(TEXT("TOGGLEUI"),{TEXT("ALT-Z"),FString()});
    }
    return Result;
}
void Store(const FString& Name,const FWowBindingMap& Map)
{
    TArray<FString> Rows;
    for(const auto& Action:Catalog)
    {
        const auto* K=Map.Find(Action.Command);
        Rows.Add(Action.Command+TEXT("|")+(K?K->Primary:FString())+TEXT("|")+(K?K->Secondary:FString()));
    }
    GConfig->SetArray(*Name,TEXT("ProfileV2"),Rows,GGameUserSettingsIni);
    GConfig->SetBool(*Name,TEXT("SwimDownDefaultsV1"),true,GGameUserSettingsIni);
    GConfig->SetBool(*Name,TEXT("TorchDefaultsV1"),true,GGameUserSettingsIni);
    GConfig->SetBool(*Name,TEXT("IsoCameraDefaultsV1"),true,GGameUserSettingsIni);
}
bool Read(const FString& Name,FWowBindingMap& Map)
{
    TArray<FString> Rows;
    if(!GConfig || !GConfig->GetArray(*Name,TEXT("ProfileV2"),Rows,GGameUserSettingsIni))return false;
    Map.Reset(); TSet<FString> Used;
    for(const auto& Row:Rows)
    {
        TArray<FString> Parts;Row.ParseIntoArray(Parts,TEXT("|"),false);
        if(Parts.Num()!=3 || !Catalog.ContainsByPredicate([&](const auto& A){return A.Command==Parts[0];}))continue;
        FWowBindingKeys Keys;
        for(int32 I=1;I<=2;++I)
        {
            const FString K=Parts[I].ToUpper();
            if(!K.IsEmpty() && !Used.Contains(K)) {Used.Add(K);(I==1?Keys.Primary:Keys.Secondary)=K;}
        }
        Map.Add(Parts[0],Keys);
    }
    bool Migrated=false;
    GConfig->GetBool(*Name,TEXT("SwimDownDefaultsV1"),Migrated,GGameUserSettingsIni);
    // Old profiles saved an empty placeholder for this previously unbound command.
    // Migrate once, preserve occupied keys and respect later deliberate unbinding.
    auto& Swim=Map.FindOrAdd(TEXT("SWIMDOWN"));
    if(!Migrated && Swim.Primary.IsEmpty() && Swim.Secondary.IsEmpty())
    {
        for(const TCHAR* Key : {TEXT("RCTRL"),TEXT("LCTRL")})
            if(!Used.Contains(Key)) { (Swim.Primary.IsEmpty()?Swim.Primary:Swim.Secondary)=Key; }
    }
    // r398: saved profiles predate "Toggle Torch"; give it Ctrl-T once if that key is free.
    bool TorchMigrated=false;
    GConfig->GetBool(*Name,TEXT("TorchDefaultsV1"),TorchMigrated,GGameUserSettingsIni);
    auto& Torch=Map.FindOrAdd(TEXT("TOGGLETORCH"));
    if(!TorchMigrated && Torch.Primary.IsEmpty() && Torch.Secondary.IsEmpty() && !Used.Contains(TEXT("CTRL-T")))
        Torch.Primary=TEXT("CTRL-T");
    // r620 (Stefan: "did also expect to find a keybinding for free turning camera"): saved profiles
    // predate the isometric camera commands; give them their default keys once where those are free.
    bool IsoMigrated=false;
    GConfig->GetBool(*Name,TEXT("IsoCameraDefaultsV1"),IsoMigrated,GGameUserSettingsIni);
    if(!IsoMigrated)
    {
        for(const auto& Default : {TPair<const TCHAR*,const TCHAR*>(TEXT("ISOCAMERATURN"),TEXT("BUTTON3")),TPair<const TCHAR*,const TCHAR*>(TEXT("ISOCAMERAROTATELEFT"),TEXT(",")),TPair<const TCHAR*,const TCHAR*>(TEXT("ISOCAMERAROTATERIGHT"),TEXT("."))})
        {
            auto& Keys=Map.FindOrAdd(Default.Key);
            if(Keys.Primary.IsEmpty() && Keys.Secondary.IsEmpty() && !Used.Contains(Default.Value)){Keys.Primary=Default.Value;Used.Add(Default.Value);}
        }
    }
    return true;
}
}
void FWowKeyBindings::Initialize()
{
    if(Loaded)return;Loaded=true;
    #include "WowBindingCatalog.inl"
    // Keep earlier project bindings valid when loading existing user preferences.
    Catalog.Add({TEXT("CLEARTARGET"),TEXT("Clear target"),TEXT("Additional commands"),-1});
    Catalog.Add({TEXT("SWIMDOWN"),TEXT("Swim down"),TEXT("Additional commands"),-1});
    Catalog.Add({TEXT("TOGGLETORCH"),TEXT("Toggle torch (realistic lighting)"),TEXT("Additional commands"),-1});
    // r620: isometric camera (Options > Experimental > Camera & Lens), listed with the other camera commands.
    Catalog.Add({TEXT("ISOCAMERATURN"),TEXT("Free Turn Camera (isometric, hold)"),TEXT("Camera Functions"),-1});
    Catalog.Add({TEXT("ISOCAMERAROTATELEFT"),TEXT("Rotate Camera Left (isometric)"),TEXT("Camera Functions"),-1});
    Catalog.Add({TEXT("ISOCAMERAROTATERIGHT"),TEXT("Rotate Camera Right (isometric)"),TEXT("Camera Functions"),-1});
    for(int32 I=1;I<=12;++I)Catalog.Add({FString::Printf(TEXT("BAR2BUTTON%d"),I),FString::Printf(TEXT("Action page 2: button %d"),I),TEXT("Additional commands"),I+11});
    Account.Presets[0]=Preset(1); Account.Presets[1]=Preset(2);
    bool HasFirst=Read(FString(Section)+TEXT(".Preset1"),Account.Presets[0]);
    Read(FString(Section)+TEXT(".Preset2"),Account.Presets[1]);
    if(GConfig)GConfig->GetInt(Section,TEXT("SelectedPreset"),Account.Selected,GGameUserSettingsIni);
    Account.Selected=FMath::Clamp(Account.Selected,0,1);
    if(!HasFirst && !Read(Section,Account.Presets[0]))
    {
        TArray<FString> Legacy;
        if(GConfig && GConfig->GetArray(Section,TEXT("Bindings"),Legacy,GGameUserSettingsIni))
        {
            Account.Presets[0].Reset();
            for(const auto& Entry:Legacy)
            {
                FString Key,Action;
                if(Entry.Split(TEXT("|"),&Key,&Action) && !Key.IsEmpty())
                {
                    const FString Name=CanonicalCommand(Action);
                    if(Catalog.ContainsByPredicate([&](const auto& A){return A.Command==Name;}))
                    {auto& K=Account.Presets[0].FindOrAdd(Name);if(K.Primary.IsEmpty())K.Primary=Key;else if(K.Secondary.IsEmpty())K.Secondary=Key;}
                }
            }
        }
    }
    Reindex();
}
const TArray<FWowBindingAction>& FWowKeyBindings::Actions(){Initialize();return Catalog;}
TArray<FString> FWowKeyBindings::Keys(const FString& Action)
{
    Initialize();const auto* K=Active().Find(CanonicalCommand(Action));
    if(!K || (K->Primary.IsEmpty() && K->Secondary.IsEmpty()))return {};
    if(K->Secondary.IsEmpty())return {K->Primary};
    return {K->Primary,K->Secondary}; // Preserve an empty Key 1 when only Key 2 is assigned.
}
FString FWowKeyBindings::Command(const FString& Key){Initialize();return Lookup.FindRef(Key.ToUpper());}
bool FWowKeyBindings::Bind(const FString& InKey,const FString& InAction,int32 Slot)
{
    Initialize();const FString Key=InKey.ToUpper(),Action=CanonicalCommand(InAction);
    if(Key.IsEmpty() || Key.Contains(TEXT("|")) || Key.Contains(TEXT("\n")) || Key.Contains(TEXT("\r")) || Slot>1)return false;
    if(!Action.IsEmpty() && !Catalog.ContainsByPredicate([&](const auto& A){return A.Command==Action;}))return false;
    auto& Map=Active();
    if(Slot<0 && !Action.IsEmpty())
    {
        auto& K=Map.FindOrAdd(Action);
        Slot=K.Primary==Key?0:K.Secondary==Key?1:K.Primary.IsEmpty()?0:1;
    }
    // One physical key can own one command; retain the other field's position.
    for(auto& Pair:Map){if(Pair.Value.Primary==Key)Pair.Value.Primary.Empty();if(Pair.Value.Secondary==Key)Pair.Value.Secondary.Empty();}
    if(!Action.IsEmpty()){auto& K=Map.FindOrAdd(Action);(Slot==0?K.Primary:K.Secondary)=Key;}
    Reindex();return true;
}
void FWowKeyBindings::Unbind(const FString& Action,int32 Slot)
{
    Initialize();if(Slot<0 || Slot>1)return;
    if(auto* K=Active().Find(CanonicalCommand(Action)))(Slot==0?K->Primary:K->Secondary).Empty();Reindex();
}
void FWowKeyBindings::Save()
{
    Initialize();if(!GConfig)return;
    auto SaveSet=[](const FString& Name,const FWowBindingPresetSet& Set)
    {
        for(int32 I=0;I<2;++I)Store(Name+FString::Printf(TEXT(".Preset%d"),I+1),Set.Presets[I]);
        GConfig->SetInt(*Name,TEXT("SelectedPreset"),Set.Selected,GGameUserSettingsIni);
    };
    SaveSet(Section,Account);
    if(!CharacterSection.IsEmpty()){SaveSet(CharacterSection,Character);GConfig->SetBool(*CharacterSection,TEXT("Enabled"),CharacterSpecific,GGameUserSettingsIni);}
    GConfig->Flush(false,GGameUserSettingsIni);
}
void FWowKeyBindings::Reload(bool Defaults)
{
    Initialize();if(Defaults){ResetPreset();return;}
    const FString SavedCharacter=CharacterSection;
    Loaded=false;Catalog.Reset();CharacterSection.Empty();CharacterSpecific=false;Initialize();
    if(!SavedCharacter.IsEmpty())
    {
        CharacterSection=SavedCharacter;Character=Account;
        for(int32 I=0;I<2;++I)Read(CharacterSection+FString::Printf(TEXT(".Preset%d"),I+1),Character.Presets[I]);
        GConfig->GetInt(*CharacterSection,TEXT("SelectedPreset"),Character.Selected,GGameUserSettingsIni);
        Character.Selected=FMath::Clamp(Character.Selected,0,1);
        GConfig->GetBool(*CharacterSection,TEXT("Enabled"),CharacterSpecific,GGameUserSettingsIni);Reindex();
    }
}
void FWowKeyBindings::UsePreset(int32 Id){Initialize();if(Id!=1 && Id!=2)return;CurrentSet().Selected=Id-1;Reindex();}
int32 FWowKeyBindings::CurrentPreset(){Initialize();return CurrentSet().Selected+1;}
void FWowKeyBindings::ResetPreset(){Initialize();Active()=Preset(CurrentPreset());Reindex();}
FWowBindingSnapshot FWowKeyBindings::Snapshot(){Initialize();return {Account,Character,CharacterSpecific};}
void FWowKeyBindings::Restore(const FWowBindingSnapshot& State){Initialize();Account=State.Account;Character=State.Character;CharacterSpecific=State.CharacterSpecific;Reindex();}
void FWowKeyBindings::SetCharacter(const FString& Identity)
{
    Initialize();const FString Next=Identity.IsEmpty()?FString():FString(Section)+TEXT(".Character.")+FMD5::HashAnsiString(*Identity);
    if(Next==CharacterSection)return;
    CharacterSection=Next;CharacterSpecific=false;Character=Account;
    if(!Next.IsEmpty() && GConfig)
    {
        for(int32 I=0;I<2;++I)Read(Next+FString::Printf(TEXT(".Preset%d"),I+1),Character.Presets[I]);
        GConfig->GetInt(*Next,TEXT("SelectedPreset"),Character.Selected,GGameUserSettingsIni);
        Character.Selected=FMath::Clamp(Character.Selected,0,1);
        GConfig->GetBool(*Next,TEXT("Enabled"),CharacterSpecific,GGameUserSettingsIni);
    }
    Reindex();Page=1;
}
bool FWowKeyBindings::CanUseCharacterProfile(){return !CharacterSection.IsEmpty();}
bool FWowKeyBindings::IsCharacterSpecific(){return CharacterSpecific;}
void FWowKeyBindings::SetCharacterSpecific(bool Enabled){Initialize();CharacterSpecific=Enabled && CanUseCharacterProfile();Reindex();}
FString FWowKeyBindings::KeyName(const FString& UnrealName,bool Ctrl,bool Shift,bool Alt)
{
    FString Key=UnrealName.ToUpper();
    static const TMap<FString,FString> Names={
        {TEXT("ZERO"),TEXT("0")},{TEXT("ONE"),TEXT("1")},{TEXT("TWO"),TEXT("2")},{TEXT("THREE"),TEXT("3")},{TEXT("FOUR"),TEXT("4")},{TEXT("FIVE"),TEXT("5")},{TEXT("SIX"),TEXT("6")},{TEXT("SEVEN"),TEXT("7")},{TEXT("EIGHT"),TEXT("8")},{TEXT("NINE"),TEXT("9")},
        {TEXT("SPACEBAR"),TEXT("SPACE")},{TEXT("HYPHEN"),TEXT("-")},{TEXT("EQUALS"),TEXT("=")},{TEXT("SLASH"),TEXT("/")},{TEXT("BACKSLASH"),TEXT("\\")},{TEXT("PERIOD"),TEXT(".")},{TEXT("COMMA"),TEXT(",")},{TEXT("SEMICOLON"),TEXT(";")},{TEXT("APOSTROPHE"),TEXT("'")},{TEXT("LEFTBRACKET"),TEXT("[")},{TEXT("RIGHTBRACKET"),TEXT("]")},{TEXT("TILDE"),TEXT("`")},
        {TEXT("LEFTCONTROL"),TEXT("LCTRL")},{TEXT("RIGHTCONTROL"),TEXT("RCTRL")},{TEXT("LEFTSHIFT"),TEXT("LSHIFT")},{TEXT("RIGHTSHIFT"),TEXT("RSHIFT")},{TEXT("LEFTALT"),TEXT("LALT")},{TEXT("RIGHTALT"),TEXT("RALT")},
        {TEXT("THUMBMOUSEBUTTON"),TEXT("BUTTON4")},{TEXT("THUMBMOUSEBUTTON2"),TEXT("BUTTON5")},{TEXT("MIDDLEMOUSEBUTTON"),TEXT("BUTTON3")},{TEXT("LEFTMOUSEBUTTON"),TEXT("BUTTON1")},{TEXT("RIGHTMOUSEBUTTON"),TEXT("BUTTON2")},
        {TEXT("NUMPADZERO"),TEXT("NUMPAD0")},{TEXT("NUMPADONE"),TEXT("NUMPAD1")},{TEXT("NUMPADTWO"),TEXT("NUMPAD2")},{TEXT("NUMPADTHREE"),TEXT("NUMPAD3")},{TEXT("NUMPADFOUR"),TEXT("NUMPAD4")},{TEXT("NUMPADFIVE"),TEXT("NUMPAD5")},{TEXT("NUMPADSIX"),TEXT("NUMPAD6")},{TEXT("NUMPADSEVEN"),TEXT("NUMPAD7")},{TEXT("NUMPADEIGHT"),TEXT("NUMPAD8")},{TEXT("NUMPADNINE"),TEXT("NUMPAD9")},
        {TEXT("MOUSESCROLLUP"),TEXT("MOUSEWHEELUP")},{TEXT("MOUSESCROLLDOWN"),TEXT("MOUSEWHEELDOWN")},
        {TEXT("ADD"),TEXT("NUMPADPLUS")},{TEXT("SUBTRACT"),TEXT("NUMPADMINUS")},{TEXT("DIVIDE"),TEXT("NUMPADDIVIDE")},{TEXT("MULTIPLY"),TEXT("NUMPADMULTIPLY")},{TEXT("DECIMAL"),TEXT("NUMPADDECIMAL")}
    };
    if(const auto* N=Names.Find(Key))Key=*N;
    if(Key==TEXT("LCTRL") || Key==TEXT("RCTRL"))Ctrl=false;
    if(Key.Contains(TEXT("SHIFT")))Shift=false;
    if(Key.Contains(TEXT("ALT")))Alt=false;
    return (Alt?TEXT("ALT-"):TEXT(""))+FString(Ctrl?TEXT("CTRL-"):TEXT(""))+(Shift?TEXT("SHIFT-"):TEXT(""))+Key;
}
FString FWowKeyBindings::Press(const FString& Key){const FString A=Command(Key);if(!A.IsEmpty())Pressed.Add(Key,A);return A;}
void FWowKeyBindings::SetCommandHeld(const FString& Command,bool Down)
{Initialize();const FString Key=TEXT("__RunBinding_")+Command;if(Down)Pressed.Add(Key,Command);else Pressed.Remove(Key);}
TArray<FString> FWowKeyBindings::Release(const FString& BaseKey)
{
    TArray<FString> Released;
    for(auto It=Pressed.CreateIterator();It;++It)if(It.Key()==BaseKey || It.Key().EndsWith(TEXT("-")+BaseKey)){Released.AddUnique(It.Value());It.RemoveCurrent();}
    Released.RemoveAll([](const FString& A){return Held(A);});return Released;
}
bool FWowKeyBindings::Held(const FString& Action){return Pressed.FindKey(CanonicalCommand(Action))!=nullptr;}
void FWowKeyBindings::ReleaseAll(){Pressed.Reset();}
int32 FWowKeyBindings::ActionPage(){return Page==1 && BonusOffset && !bPlainPageOne ? 6+BonusOffset : Page;}
int32 FWowKeyBindings::ManualActionPage(){return Page;}
int32 FWowKeyBindings::BonusBarOffset(){return BonusOffset;}
bool FWowKeyBindings::SetBonusBarOffset(int32 Offset){Offset=FMath::Clamp(Offset,0,4);if(Offset==BonusOffset)return false;BonusOffset=Offset;bPlainPageOne=false;return true;}
void FWowKeyBindings::SetActionPage(int32 InPage){bPlainPageOne=InPage==0;Page=FMath::Clamp(InPage,1,6);}
int32 FWowKeyBindings::StepActionPage(bool bNext, const TFunction<bool(int32)>& bShown)
{
    // With a bonus bar (stealth, stances, forms) page 1 comes twice: with the bonus bar (token 1)
    // and plain (token 0), so the plain page stays reachable. Without one, pages 1-6 as before.
    TArray<int32> Cycle;
    if(BonusOffset){Cycle.Add(1);Cycle.Add(0);}else Cycle.Add(1);
    for(int32 P=2;P<=6;++P)if(!bShown||!bShown(P))Cycle.Add(P);
    const int32 Current=Page==1?(BonusOffset&&bPlainPageOne?0:1):Page;
    int32 Index=Cycle.IndexOfByKey(Current);
    if(Index==INDEX_NONE)Index=0;
    Index=(Index+(bNext?1:Cycle.Num()-1))%Cycle.Num();
    return Cycle[Index];
}
