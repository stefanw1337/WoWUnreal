#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "../WowGameplayController.h"
#include "WowAnimationController.h"
#include "WowConnectionManager.h"
#include "WowSplineSampling.h"
#include "Coord/WowCoordinate.h"
#include "Formats/M2Types.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowRemoteCombatMotionTest,"WowUnreal.CombatMotion.PathFacingAndStride",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowRemoteCombatMotionTest::RunTest(const FString&)
{
    FWowMovementInfo Path;Path.SplineStartPosition=FVector(10,10,1);
    Path.SplineWaypoints={FWowSplineWaypoint(FVector(11,10,1)),FWowSplineWaypoint(FVector(11,19,1))};
    FVector Tangent;
    TestTrue(TEXT("Unequal segments use travelled distance, not equal time"),SampleWowLinearSpline(Path,.5f,Tangent).Equals(FVector(11,14,1)));
    TestTrue(TEXT("Current path segment determines facing"),Tangent.GetSafeNormal().Equals(FVector(0,1,0)));
    TestTrue(TEXT("Before corner stays on short segment"),SampleWowLinearSpline(Path,.05f,Tangent).Equals(FVector(10.5,10,1)));
    TestTrue(TEXT("Does not look sideways toward following segment prematurely"),Tangent.GetSafeNormal().Equals(FVector(1,0,0)));
    Path.SplineWaypoints.Insert(FWowSplineWaypoint(Path.SplineStartPosition),0);
    TestTrue(TEXT("Duplicate waypoint does not pause or divide by zero"),SampleWowLinearSpline(Path,.5f,Tangent).Equals(FVector(11,14,1)));
    TestTrue(TEXT("Endpoint exact"),SampleWowLinearSpline(Path,1.f,Tangent).Equals(FVector(11,19,1)));

    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if(!TestNotNull(TEXT("Isolated world"),World))return false;
    ON_SCOPE_EXIT{World->DestroyWorld(false);};
    auto* Controller=World->SpawnActor<AWowGameplayController>();
    auto* Pawn=World->SpawnActor<APawn>();
    auto AddRoot=[](AActor* Actor)
    {
        auto* Root=NewObject<USceneComponent>(Actor);Actor->AddInstanceComponent(Root);Actor->SetRootComponent(Root);Root->RegisterComponent();
    };
    AddRoot(Pawn);Controller->SetPawn(Pawn);
    Controller->ConnectionManager=NewObject<UWowConnectionManager>(Controller);
    auto& Entities=Controller->ConnectionManager->PacketHandler.EntityManager;Entities.LocalPlayerGuid=1;
    auto& Creature=Entities.PromoteToTyped(2,WowTypeMask::UNIT);
    Creature.Movement.Position=FVector(10,10,1);Creature.Movement.Orientation=2.f;
    auto* Mob=World->SpawnActor<AActor>();AddRoot(Mob);
    Mob->SetActorLocation(FWowCoordinate::WowToUE(Creature.Movement.Position));
    Controller->SpawnedEntityActors.Add(2,Mob);
    Pawn->SetActorLocation(Mob->GetActorLocation()+FVector(100,0,0));
    Controller->OnServerAttackStart(2,1);
    Controller->UpdateEntityNetworkMovement(1.f);
    TestTrue(TEXT("Stationary attacker faces confirmed victim"),FMath::Abs(FMath::FindDeltaAngleDegrees(Mob->GetActorRotation().Yaw,0.f))<.01f);
    Pawn->SetActorLocation(Mob->GetActorLocation()+FVector(0,-100,0));
    Controller->UpdateEntityNetworkMovement(1.f);
    TestTrue(TEXT("Victim circling attacker updates facing without new movement packet"),FMath::Abs(FMath::FindDeltaAngleDegrees(Mob->GetActorRotation().Yaw,-90.f))<.01f);
    Controller->OnServerAttackStart(2,3);
    Controller->OnServerAttackStop(2,1);
    TestEqual(TEXT("Late stop does not erase new victim"),Controller->ServerMeleeVictims.FindRef(2),uint64(3));
    Controller->OnServerAttackStop(2,3);
    Controller->UpdateEntityNetworkMovement(1.f);
    TestTrue(TEXT("Stop restores server orientation"),FMath::Abs(FMath::FindDeltaAngleDegrees(Mob->GetActorRotation().Yaw,FWowCoordinate::WowOrientationToUEYaw(2.f)))<.01f);
    Creature.SetField(UnitField::FLAGS,0x00080000);
    Creature.SetField(UnitField::TARGET,1);
    Controller->UpdateEntityNetworkMovement(1.f);
    TestTrue(TEXT("Pet entering view mid-fight uses replicated victim"),FMath::Abs(FMath::FindDeltaAngleDegrees(Mob->GetActorRotation().Yaw,-90.f))<.01f);
    Creature.SetField(UnitField::FLAGS,0);Creature.SetField(UnitField::TARGET,0);
    Creature.Movement.Position+=FVector(2,0,0);
    Controller->UpdateEntityNetworkMovement(1.f);
    const float TravelYaw=(FWowCoordinate::WowToUE(FVector(1,0,0))-FWowCoordinate::WowToUE(FVector::ZeroVector)).Rotation().Yaw;
    TestTrue(TEXT("Pet position updates face travel direction"),FMath::Abs(FMath::FindDeltaAngleDegrees(Mob->GetActorRotation().Yaw,TravelYaw))<.01f);
    Controller->VisuallyMovingEntityGuids.Reset();
    Controller->OnServerAttackStart(2,1);
    Creature.Movement.SplineFlags=0x00004000;
    Controller->UpdateEntityNetworkMovement(1.f);
    TestTrue(TEXT("Fixed facing is not overridden"),FMath::Abs(FMath::FindDeltaAngleDegrees(Mob->GetActorRotation().Yaw,FWowCoordinate::WowOrientationToUEYaw(2.f)))<.01f);
    Creature.Movement=Path;Creature.Movement.SplineFlags=0;Creature.Movement.SplineDuration=4000;
    Creature.Movement.bHasActiveSpline=true;Creature.Movement.UpdateSplineSpeed();
    Mob->SetActorLocation(FWowCoordinate::WowToUE(Path.SplineStartPosition));
    Controller->UpdateEntitySplineMovement(.2f);
    TestTrue(TEXT("Runtime actor follows distance-sampled path"),Mob->GetActorLocation().Equals(FWowCoordinate::WowToUE(FVector(10.5,10,1)),.01));
    TestTrue(TEXT("Runtime records actual ground speed"),FMath::IsNearlyEqual(Controller->EntityVisualSpeedsCm.FindRef(2),250.f));
    Controller->ForgetEntityCombatMotion(1);
    TestFalse(TEXT("Dead/despawned victim stops being tracked"),Controller->ServerMeleeVictims.Contains(2));

    auto* Mesh=NewObject<USkeletalMeshComponent>(Mob);Mob->AddInstanceComponent(Mesh);Mesh->SetupAttachment(Mob->GetRootComponent());Mesh->RegisterComponent();
    Mesh->AnimScriptInstance=NewObject<UAnimSingleNodeInstance>(Mesh);
    Mesh->SetWorldScale3D(FVector(1.5f));
    auto* Anim=NewObject<UWowAnimationController>(Mob);auto* Clip=NewObject<UAnimSequence>();
    Anim->MeshComponent=Mesh;Anim->CurrentAnimation=Clip;Anim->CurrentState=EWowAnimState::Walking;Anim->AnimationCache.Add(4,Clip);
    FM2AnimSequence Walk;Walk.AnimationId=4;Walk.MoveSpeed=2.5f;
    Anim->SetAuthoredMovementSpeeds({Walk});
    Anim->UpdateLocomotionPlayRate(750.f,true);
    TestTrue(TEXT("Stride follows actual speed and creature model scale"),FMath::IsNearlyEqual(Mesh->GetPlayRate(),2.f));
    Anim->UpdateLocomotionPlayRate(187.5f,true);
    TestTrue(TEXT("Slow movement slows the feet instead of sliding"),FMath::IsNearlyEqual(Mesh->GetPlayRate(),.5f));
    Anim->CurrentState=EWowAnimState::Combat;Anim->UpdateLocomotionPlayRate(750.f,true);
    TestTrue(TEXT("Stride correction does not speed up attacks/combat idle"),FMath::IsNearlyEqual(Mesh->GetPlayRate(),1.f));
    return true;
}
#endif
