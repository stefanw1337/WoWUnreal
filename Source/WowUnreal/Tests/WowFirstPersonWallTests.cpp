#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../WowPlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowFirstPersonWallTest,
    "WowUnreal.Camera.FirstPerson.WallClearance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowFirstPersonWallTest::RunTest(const FString&)
{
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
        nullptr, true, ERHIFeatureLevel::Num, &Init);
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    auto* Pawn = World->SpawnActor<AWowPlayerCharacter>();
    Pawn->CameraBoom->TargetArmLength = 0;
    // Model a running head leaning forward beyond the 35 cm capsule radius.
    // CalcCamera's fallback adds the same forward eye offset as animated eyes.
    Pawn->CameraBoom->TargetOffset = FVector(35,0,60);
    FMinimalViewInfo FreeView;
    Pawn->CalcCamera(1.f/60,FreeView);
    TestTrue(TEXT("Fixture places the requested eye beyond a flush wall"), FreeView.Location.X > 36);
    auto* WallActor = World->SpawnActor<AActor>();
    auto* Wall = NewObject<UBoxComponent>(WallActor);
    WallActor->SetRootComponent(Wall); WallActor->AddInstanceComponent(Wall);
    Wall->SetBoxExtent(FVector(10,200,200));
    Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Wall->SetCollisionResponseToAllChannels(ECR_Block);
    Wall->RegisterComponent(); WallActor->SetActorLocation(FVector(46,0,60));
    for (int32 Frame=0;Frame<120;++Frame)
    {
        FMinimalViewInfo View;
        Pawn->CalcCamera(1.f/60,View);
        TestTrue(TEXT("Holding forward never moves the eye through the wall"),
            View.Location.X <= 24 && View.Location.X >= 0);
        TestTrue(TEXT("Wall contact preserves eye height"), FMath::IsNearlyEqual(View.Location.Z,FreeView.Location.Z,0.1));
    }
    Wall->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    FMinimalViewInfo StaticOnly;
    Pawn->CalcCamera(1.f/60,StaticOnly);
    TestTrue(TEXT("World-static blockers also protect the first-person eye"),StaticOnly.Location.X<=24);
    WallActor->SetActorLocation(FVector(500,0,60));
    FMinimalViewInfo ClearAgain;
    Pawn->CalcCamera(1.f/60,ClearAgain);
    TestTrue(TEXT("Moving away restores full eye placement immediately"),ClearAgain.Location.Equals(FreeView.Location,0.1));
    return true;
}
#endif
