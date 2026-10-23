#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "WowFrameLayout.h"
#include "WowFrameManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

namespace
{
struct FIsolatedFrameSettings
{
    FString Original=GGameUserSettingsIni;
    FString Path=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Automation"),FGuid::NewGuid().ToString()+TEXT("-FrameLayout.ini"));
    FIsolatedFrameSettings(){FConfigFile Config;Config.bCanSaveAllSections=true;Config.NoSave=false;GConfig->Add(Path,Config);GGameUserSettingsIni=Path;FWowFrameLayout::Positions();}
    ~FIsolatedFrameSettings()
    {GGameUserSettingsIni=Original;FWowFrameLayout::Positions();GConfig->UnloadFile(Path);IFileManager::Get().Delete(*Path);}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowFramePositionPersistenceTest,"WowUnreal.UI.FrameLayout.PersistenceAndReset",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowFramePositionPersistenceTest::RunTest(const FString&)
{
    FIsolatedFrameSettings Isolated;
    GConfig->SetString(TEXT("Unrelated"),TEXT("Keep"),TEXT("untouched"),GGameUserSettingsIni);
    FWowFrameLayout::Set(TEXT("PlayerFrame"),FVector2D(.25,.4));
    FWowFrameLayout::Set(TEXT("NativeVendor"),FVector2D(.5,.1));FWowFrameLayout::Save();
    FConfigFile OnDisk;OnDisk.Read(Isolated.Path);FString Stored;
    TestTrue(TEXT("Positions actually written to disk"),OnDisk.GetString(TEXT("WowUnreal.FrameLayout"),TEXT("PlayerFrame"),Stored));
    GGameUserSettingsIni=Isolated.Original;FWowFrameLayout::Positions();
    GGameUserSettingsIni=Isolated.Path;
    TestEqual(TEXT("Positions survive reload"),FWowFrameLayout::Positions().Num(),2);
    const FVector2D* Saved=FWowFrameLayout::Positions().Find(TEXT("PlayerFrame"));
    TestTrue(TEXT("Normalized position preserved"),Saved&&Saved->Equals(FVector2D(.25,.4),.00001));
    FWowFrameLayout::Reset();
    TestEqual(TEXT("All frame overrides cleared"),FWowFrameLayout::Positions().Num(),0);
    FConfigFile AfterReset;AfterReset.Read(Isolated.Path);
    TestFalse(TEXT("Reset persists on disk"),AfterReset.GetString(TEXT("WowUnreal.FrameLayout"),TEXT("PlayerFrame"),Stored));
    FString Value;GConfig->GetString(TEXT("Unrelated"),TEXT("Keep"),Value,GGameUserSettingsIni);
    TestEqual(TEXT("Reset preserves other preferences"),Value,FString(TEXT("untouched")));
    TestTrue(TEXT("Frame stays inside smaller viewport"),FWowFrameLayout::ClampPosition(FVector2D(.95,.95),FVector2D(200,100),FVector2D(800,600)).Equals(FVector2D(600,500)));
    TestTrue(TEXT("Oversized frame origin remains accessible"),FWowFrameLayout::ClampPosition(FVector2D(.5,.5),FVector2D(900,700),FVector2D(800,600)).IsNearlyZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowMovedFrameHitTest,"WowUnreal.UI.FrameLayout.MoveWholeFrameAndRestore",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowMovedFrameHitTest::RunTest(const FString&)
{
    FIsolatedFrameSettings Isolated;
    for(float Scale:{1.f,1.40625f,2.f})
    {
        FWowFrameLayout::Reset();
        FWowFrameManager Frames;Frames.Initialize(NewObject<UCanvasPanel>());Frames.SetUIScale(Scale);
        FWowFrameDef Root;Root.Name=TEXT("MovableBar");Root.Parent=TEXT("UIParent");Root.Width=200;Root.Height=100;
        FWowAnchor A;A.Point=EWowAnchorPoint::TOPLEFT;A.RelativePoint=A.Point;A.RelativeTo=Root.Parent;A.OffsetX=20;A.OffsetY=-30;Root.Anchors.Add(A);
        const int64 R=Frames.CreateFrame(Root);
        FWowFrameDef Child;Child.Name=TEXT("MovableButton");Child.Parent=Root.Name;Child.Width=32;Child.Height=32;Child.bEnableMouse=true;
        A.RelativeTo=Root.Name;A.OffsetX=10;A.OffsetY=-10;Child.Anchors.Add(A);
        const int64 C=Frames.CreateFrame(Child);
        TestEqual(TEXT("Child drag selects entire bar"),Frames.GetMovableFrameAtScreenPosition(35,45),R);
        FWowFrameLayout::Set(Root.Name,FVector2D(.5,.5));Frames.UpdateUserFrameLayouts();
        const auto* Parent=Frames.GetFrameDef(Frames.FindFrame(TEXT("UIParent")));
        const float X=Parent->Width*.5f+15,Y=Parent->Height*.5f+15;
        TestEqual(TEXT("Click follows translated child"),Frames.HitTestFrames(X,Y),C);
        TestTrue(TEXT("Inspector follows translated frame"),Frames.GetFramesAtScreenPosition(X,Y).Contains(R));
        TestTrue(TEXT("Old location has no invisible child hit"),Frames.HitTestFrames(35,45)!=C);
        const auto Anchors=Frames.GetFrameDef(R)->Anchors;
        FWowFrameLayout::Reset();Frames.UpdateUserFrameLayouts();
        TestTrue(TEXT("Reset clears translation"),Frames.GetWidgetForHandle(R)->GetRenderTransform().Translation.IsNearlyZero());
        TestEqual(TEXT("Original anchor retained"),Frames.GetFrameDef(R)->Anchors[0].OffsetX,Anchors[0].OffsetX);
        TestEqual(TEXT("Original click position restored"),Frames.HitTestFrames(35,45),C);
        Frames.SetFrameVisible(R,false);
        TestTrue(TEXT("Hidden frames cannot be grabbed"),Frames.GetMovableFrameAtScreenPosition(35,45)!=R);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowFrameGroupTest,"WowUnreal.UI.FrameLayout.ChatGroup",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowFrameGroupTest::RunTest(const FString&)
{
    FIsolatedFrameSettings Isolated;
    FWowFrameManager Frames;Frames.Initialize(NewObject<UCanvasPanel>());
    auto Create=[&](const TCHAR* Name,float X,float Y,float W,float H)
    {
        FWowFrameDef D;D.Name=Name;D.Parent=TEXT("UIParent");D.Width=W;D.Height=H;D.bEnableMouse=true;
        FWowAnchor A;A.Point=EWowAnchorPoint::TOPLEFT;A.RelativePoint=A.Point;A.RelativeTo=D.Parent;A.OffsetX=X;A.OffsetY=-Y;D.Anchors.Add(A);
        return Frames.CreateFrame(D);
    };
    const int64 Chat=Create(TEXT("ChatFrame1"),50,100,300,100);
    const int64 Tab=Create(TEXT("ChatFrame2Tab"),50,70,90,25);
    TestEqual(TEXT("Separate chat tab belongs to chat group"),Frames.GetMovableFrameAtScreenPosition(60,80),Chat);
    FWowFrameLayout::Set(TEXT("ChatFrame1"),FVector2D(.5,.5));Frames.UpdateUserFrameLayouts();
    TestTrue(TEXT("Tab receives same translation as message area"),
        Frames.GetWidgetForHandle(Chat)->GetRenderTransform().Translation.Equals(Frames.GetWidgetForHandle(Tab)->GetRenderTransform().Translation));
    TestEqual(TEXT("Moved tab remains clickable"),Frames.HitTestFrames(522,364),Tab);
    FWowFrameLayout::Reset();Frames.UpdateUserFrameLayouts();
    TestTrue(TEXT("Reset also restores grouped sibling"),Frames.GetWidgetForHandle(Tab)->GetRenderTransform().Translation.IsNearlyZero());
    return true;
}
#endif
