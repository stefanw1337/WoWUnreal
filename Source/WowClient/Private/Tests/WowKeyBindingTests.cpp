#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowKeyBindings.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowKeyBindingProfilesTest,"WowUnreal.KeyBindings.PresetsAndPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowKeyBindingProfilesTest::RunTest(const FString&)
{
    const auto Original=FWowKeyBindings::Snapshot();
    const FString OriginalIni=GGameUserSettingsIni;
    const FString TestIni=FConfigCacheIni::NormalizeConfigIniPath(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation")/(TEXT("bindings-")+FGuid::NewGuid().ToString()+TEXT(".ini"))));
    FConfigFile TestConfig;TestConfig.bCanSaveAllSections=true;TestConfig.NoSave=false;
    GConfig->Add(TestIni,TestConfig);
    GGameUserSettingsIni=TestIni;
    FWowKeyBindings::SetCharacter(TEXT(""));
    FWowKeyBindings::UsePreset(1);FWowKeyBindings::ResetPreset();
    TestEqual(TEXT("Standard turns on A"),FWowKeyBindings::Command(TEXT("A")),FString(TEXT("TURNLEFT")));
    TestEqual(TEXT("Standard arrow secondary"),FWowKeyBindings::Keys(TEXT("TURNLEFT"))[1],FString(TEXT("LEFT")));
    TestTrue(TEXT("Unassigned actions are valid"),FWowKeyBindings::Keys(TEXT("VEHICLEAIMUP")).IsEmpty());
    FWowKeyBindings::Bind(TEXT("ALT-F9"),TEXT("FOLLOWTARGET"),1);
    auto Keys=FWowKeyBindings::Keys(TEXT("FOLLOWTARGET"));
    TestTrue(TEXT("Key 2 does not move into empty Key 1"),Keys.Num()==2 && Keys[0].IsEmpty() && Keys[1]==TEXT("ALT-F9"));
    FWowKeyBindings::Bind(TEXT("ALT-F9"),TEXT("TARGETFOCUS"),0);
    TestTrue(TEXT("Conflict removes former assignment"),FWowKeyBindings::Keys(TEXT("FOLLOWTARGET")).IsEmpty());
    FWowKeyBindings::UsePreset(2);FWowKeyBindings::ResetPreset();
    TestEqual(TEXT("Personal strafe A"),FWowKeyBindings::Command(TEXT("A")),FString(TEXT("STRAFELEFT")));
    TestEqual(TEXT("Personal autorun primary"),FWowKeyBindings::Keys(TEXT("TOGGLEAUTORUN"))[0],FString(TEXT("BUTTON4")));
    TestEqual(TEXT("Personal autorun secondary"),FWowKeyBindings::Keys(TEXT("TOGGLEAUTORUN"))[1],FString(TEXT("SHIFT-W")));
    TestTrue(TEXT("Personal blank guild key remains blank"),FWowKeyBindings::Keys(TEXT("TOGGLEGUILDTAB")).IsEmpty());
    TestTrue(TEXT("Personal extra bar is not auto-assigned"),FWowKeyBindings::Keys(TEXT("MULTIACTIONBAR1BUTTON1")).IsEmpty());
    FWowKeyBindings::Bind(TEXT("ALT-F8"),TEXT("RAIDTARGET8"),1);
    FWowKeyBindings::UsePreset(1);
    TestEqual(TEXT("Switching presets retains its edits"),FWowKeyBindings::Command(TEXT("ALT-F9")),FString(TEXT("TARGETFOCUS")));
    const auto BeforeEdit=FWowKeyBindings::Snapshot();
    FWowKeyBindings::ResetPreset();FWowKeyBindings::Restore(BeforeEdit);
    TestEqual(TEXT("Cancel restores reset edits"),FWowKeyBindings::Command(TEXT("ALT-F9")),FString(TEXT("TARGETFOCUS")));
    FWowKeyBindings::SetCharacter(TEXT("test-realm|account|first-character"));FWowKeyBindings::SetCharacterSpecific(true);
    FWowKeyBindings::UsePreset(2);FWowKeyBindings::ResetPreset();FWowKeyBindings::Bind(TEXT("ALT-F7"),TEXT("FOLLOWTARGET"),1);
    FWowKeyBindings::Save();
    TestTrue(TEXT("Profile file is written"),IFileManager::Get().FileExists(*TestIni));
    GConfig->UnloadFile(TestIni);GConfig->LoadFile(TestIni);
    FWowKeyBindings::Reload();
    TestTrue(TEXT("Character-specific enabled persists"),FWowKeyBindings::IsCharacterSpecific());
    TestEqual(TEXT("Character selected preset persists"),FWowKeyBindings::CurrentPreset(),2);
    TestEqual(TEXT("Character secondary key persists"),FWowKeyBindings::Command(TEXT("ALT-F7")),FString(TEXT("FOLLOWTARGET")));
    FWowKeyBindings::SetCharacterSpecific(false);FWowKeyBindings::UsePreset(1);
    TestEqual(TEXT("Character changes do not overwrite account preset 1"),FWowKeyBindings::Command(TEXT("ALT-F9")),FString(TEXT("TARGETFOCUS")));
    FWowKeyBindings::UsePreset(2);
    TestEqual(TEXT("Account preset 2 also persists"),FWowKeyBindings::Command(TEXT("ALT-F8")),FString(TEXT("RAIDTARGET8")));
    FWowKeyBindings::SetCharacter(TEXT("test-realm|account|second-character"));FWowKeyBindings::SetCharacterSpecific(true);
    TestTrue(TEXT("Different character does not inherit another character's edits"),FWowKeyBindings::Command(TEXT("ALT-F7")).IsEmpty());
    FWowKeyBindings::UsePreset(2);FWowKeyBindings::ResetPreset();
    FWowKeyBindings::Press(TEXT("W"));FWowKeyBindings::Press(TEXT("UP"));
    FWowKeyBindings::Release(TEXT("W"));TestTrue(TEXT("Movement remains held by secondary key"),FWowKeyBindings::Held(TEXT("MOVEFORWARD")));
    FWowKeyBindings::Release(TEXT("UP"));TestFalse(TEXT("Last key release stops movement"),FWowKeyBindings::Held(TEXT("MOVEFORWARD")));
    TestEqual(TEXT("Wheel naming matches viewport"),FWowKeyBindings::KeyName(TEXT("MouseScrollUp"),false,true),FString(TEXT("SHIFT-MOUSEWHEELUP")));
    TestEqual(TEXT("Numpad naming matches screenshots"),FWowKeyBindings::KeyName(TEXT("NumPadZero")),FString(TEXT("NUMPAD0")));
    FWowKeyBindings::SetCharacter(TEXT(""));FWowKeyBindings::Restore(Original);GGameUserSettingsIni=OriginalIni;
    GConfig->UnloadFile(TestIni);IFileManager::Get().Delete(*TestIni);
    return true;
}
#endif
