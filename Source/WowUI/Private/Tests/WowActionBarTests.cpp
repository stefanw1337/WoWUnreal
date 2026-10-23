#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowFrameManager.h"
#include "WowFrameTypes.h"
#include "WowLuaVM.h"

#include "WowClientPreferences.h"
#include "WowConnectionManager.h"
#include "Components/CanvasPanel.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowActionBarVisibilityTest, "WowUnreal.UI.ActionBars.VisibilityAndPreferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowActionBarVisibilityTest::RunTest(const FString&)
{
    const TCHAR* Keys[] = {TEXT("actionbar3"), TEXT("actionbar4"), TEXT("actionbar5"), TEXT("actionbar6"), TEXT("alwaysshowactionbars")};
    bool Original[5];
    for (int32 I = 0; I < 5; ++I) Original[I] = FWowClientPreferences::GetWorldOption(Keys[I]);
    const FString OriginalIni = GGameUserSettingsIni;
    const FString TestIni = FConfigCacheIni::NormalizeConfigIniPath(FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("Automation") / (TEXT("actionbars-") + FGuid::NewGuid().ToString() + TEXT(".ini"))));
    FConfigFile TestConfig; TestConfig.bCanSaveAllSections = true; TestConfig.NoSave = false;
    GConfig->Add(TestIni, TestConfig);
    GGameUserSettingsIni = TestIni;
    ON_SCOPE_EXIT
    {
        for (int32 I = 0; I < 5; ++I) FWowClientPreferences::SetWorldOption(Keys[I], Original[I]);
        GGameUserSettingsIni = OriginalIni;
        GConfig->UnloadFile(TestIni);
        IFileManager::Get().Delete(*TestIni);
    };
    FWowLuaVM VM;
    if (!TestTrue(TEXT("VM initialized"), VM.Initialize())) return false;

    FWowFrameManager Frames;
    UCanvasPanel* Canvas = NewObject<UCanvasPanel>();
    Frames.Initialize(Canvas);
    UWowConnectionManager* Connection = NewObject<UWowConnectionManager>();
    Connection->PacketHandler.ActionButtons.SetNumZeroed(144);
    Frames.SetItemConnection(Connection);
    auto Create = [&Frames](const FString& Name, const FString& Parent, bool Hidden = false)
    {
        FWowFrameDef Def; Def.Name = Name; Def.Parent = Parent; Def.bHidden = Hidden;
        Def.Width = 36; Def.Height = 36;
        return Frames.CreateFrame(Def);
    };
    const int64 Root = Create(TEXT("ActionBarTestRoot"), TEXT(""));
    const TCHAR* Bars[] = {TEXT("MultiBarBottomLeft"), TEXT("MultiBarBottomRight"), TEXT("MultiBarRight"), TEXT("MultiBarLeft")};
    const int32 Slots[] = {60, 48, 24, 36};
    // Preferences can be processed before FrameXML has created the frames.
    Frames.UpdateOptionalActionBars();
    for (int32 I = 0; I < 4; ++I)
    {
        Create(Bars[I], TEXT("ActionBarTestRoot"), true);
        for (int32 Button = 1; Button <= 12; ++Button)
            Create(FString::Printf(TEXT("%sButton%d"), Bars[I], Button), Bars[I], true);
        Connection->PacketHandler.ActionButtons[Slots[I]] = 6603;
    }
    // All combinations, then reverse: catches stale caches and repeated toggles.
    for (int32 Pass = 0; Pass < 2; ++Pass)
    for (int32 Step = 0; Step < 32; ++Step)
    {
        const int32 Mask = Pass == 0 ? Step : 31 - Step;
        for (int32 I = 0; I < 5; ++I)
            TestTrue(TEXT("Native checkbox CVar accepted"), VM.ExecuteString(FString::Printf(
                TEXT("SetCVar('%s', '%d')"), Keys[I], (Mask >> I) & 1)));
        Frames.UpdateOptionalActionBars();
        for (int32 I = 0; I < 4; ++I)
        {
            const bool Enabled = (Mask & (1 << I)) && (I != 3 || (Mask & 4));
            TestEqual(TEXT("Bar matches effective preference"), Frames.IsFrameActuallyVisible(Frames.FindFrame(Bars[I])), Enabled);
            TestEqual(TEXT("Filled slot follows enabled bar"), Frames.IsFrameActuallyVisible(Frames.FindFrame(
                FString::Printf(TEXT("%sButton1"), Bars[I]))), Enabled);
            TestEqual(TEXT("Empty slot follows Always Show on enabled bar"), Frames.IsFrameActuallyVisible(Frames.FindFrame(
                FString::Printf(TEXT("%sButton2"), Bars[I]))), Enabled && (Mask & 16) != 0);
            TestTrue(TEXT("Lua global matches preference"), VM.ExecuteString(FString::Printf(
                TEXT("assert((not not SHOW_MULTI_ACTIONBAR_%d) == %s)"), I + 1, (Mask & (1 << I)) ? TEXT("true") : TEXT("false"))));
        }
        TestTrue(TEXT("Always Show global matches preference"), VM.ExecuteString(FString::Printf(
            TEXT("assert(ALWAYS_SHOW_MULTIBARS == '%d')"), (Mask >> 4) & 1)));
    }
    TestTrue(TEXT("Stock five-argument setter supports numeric and string zero"), VM.ExecuteString(
        TEXT("SetActionBarToggles(1, 0, '1', '0', 0); local a,b,c,d=GetActionBarToggles(); assert(a and not b and c and not d); assert(ALWAYS_SHOW_MULTIBARS=='0')")));
    TestTrue(TEXT("Four-argument callers preserve Always Show"), VM.ExecuteString(
        TEXT("SetActionBarToggles(true,true,true,true); assert(ALWAYS_SHOW_MULTIBARS=='0')")));
    Frames.UpdateOptionalActionBars();
    const int64 Bar = Frames.FindFrame(Bars[0]);
    const int64 Filled = Frames.FindFrame(TEXT("MultiBarBottomLeftButton1"));
    const int64 Empty = Frames.FindFrame(TEXT("MultiBarBottomLeftButton2"));
    Frames.SetFrameVisible(Bar, false); // A late Lua callback after preferences were applied.
    Frames.UpdateOptionalActionBars();
    TestTrue(TEXT("Late hidden bar recovers without a preference change"), Frames.IsFrameActuallyVisible(Filled));
    Frames.SetFrameVisible(Root, false);
    Frames.SetFrameVisible(Filled, true);
    TestFalse(TEXT("Explicit Show cannot reveal a child of hidden ancestors"), Frames.IsFrameActuallyVisible(Filled));
    Frames.UpdateOptionalActionBars();
    TestFalse(TEXT("Reconciliation respects hidden root"), Frames.IsFrameActuallyVisible(Filled));
    Frames.SetFrameVisible(Bar, false);
    Frames.SetFrameVisible(Root, true);
    TestFalse(TEXT("Showing root preserves hidden intermediate parent"), Frames.IsFrameActuallyVisible(Filled));
    Frames.UpdateOptionalActionBars();
    TestTrue(TEXT("Filled slot restored"), Frames.IsFrameActuallyVisible(Filled));
    TestFalse(TEXT("Empty slot remains hidden"), Frames.IsFrameActuallyVisible(Empty));
    Connection->PacketHandler.ActionButtons[60] = 0;
    Connection->PacketHandler.ActionButtons[61] = 6603;
    Frames.UpdateOptionalActionBars();
    TestFalse(TEXT("Removed action hides empty slot"), Frames.IsFrameActuallyVisible(Filled));
    TestTrue(TEXT("New action reveals filled slot"), Frames.IsFrameActuallyVisible(Empty));
    Connection->PickupSpellCursor(6603);
    Frames.UpdateOptionalActionBars();
    TestTrue(TEXT("Dragging reveals empty drop targets on enabled bars"), Frames.IsFrameActuallyVisible(Filled));
    TestTrue(TEXT("Disable bar while dragging"), VM.ExecuteString(TEXT("SetCVar('actionbar3','0')")));
    Frames.UpdateOptionalActionBars();
    TestFalse(TEXT("Dragging never enables a disabled bar"), Frames.IsFrameActuallyVisible(Filled));
    Connection->ClearCursorPayload();
    TestTrue(TEXT("Re-enable bar"), VM.ExecuteString(TEXT("SetCVar('actionbar3','1')")));
    Frames.UpdateOptionalActionBars();
    TestFalse(TEXT("Drop targets disappear after dragging ends"), Frames.IsFrameActuallyVisible(Filled));
    TestTrue(TEXT("Options saved to isolated test file"), IFileManager::Get().FileExists(*TestIni));
    GConfig->UnloadFile(TestIni); GConfig->LoadFile(TestIni);
    bool Saved = true;
    TestTrue(TEXT("Saved Always Show readable after disk reload"), GConfig->GetBool(TEXT("WowUnreal.ClientPreferences"), Keys[4], Saved, TestIni));
    TestFalse(TEXT("Always Show off persisted"), Saved);
    FWowLuaVM ReloadedVM;
    if (ReloadedVM.Initialize())
    {

        TestTrue(TEXT("New UI VM initializes from saved preferences"), ReloadedVM.ExecuteString(
            TEXT("local a,b,c,d=GetActionBarToggles(); assert(a and b and c and d); assert(ALWAYS_SHOW_MULTIBARS=='0')")));
    }
    return true;
}
#endif



