#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowEquipmentManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowMeleeWeaponPresentationTest,
    "WowUnreal.Equipment.MeleeDrawAndSpellRestore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowMeleeWeaponPresentationTest::RunTest(const FString&)
{
    const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated world"), World)) return false;
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    AWowGameplayController* Controller = World->SpawnActor<AWowGameplayController>();
    APawn* Pawn = World->SpawnActor<APawn>();
    if (!TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("Pawn"), Pawn)) return false;
    USkeletalMeshComponent* Body = NewObject<USkeletalMeshComponent>(Pawn);
    Pawn->AddInstanceComponent(Body); Pawn->SetRootComponent(Body); Body->RegisterComponent();
    UStaticMeshComponent* Staff = NewObject<UStaticMeshComponent>(Pawn);
    Pawn->AddInstanceComponent(Staff); Staff->SetupAttachment(Body, TEXT("WowStaffBackLeft")); Staff->RegisterComponent();
    Staff->ComponentTags = {TEXT("WowHeldWeapon"), TEXT("WowStaffWeapon"), TEXT("WowDrawSocket=WowSpellRightHand")};
    Controller->SetPawn(Pawn);
    Controller->ConnectionManager = NewObject<UWowConnectionManager>(Controller);
    Controller->ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid = 1;
    Controller->TargetGuid = 2;
    Controller->LocalSheathState = 0;
    Controller->bLocalSheathStateInitialized = true;
    Controller->StartAutoAttack();
    TestTrue(TEXT("Attack starts"), Controller->IsAutoAttacking());
    TestEqual(TEXT("Attack requests melee sheath state"), Controller->LocalSheathState, uint8(1));
    TestEqual(TEXT("Previously sheathed staff is drawn into right hand"), Staff->GetAttachSocketName(), FName(TEXT("WowSpellRightHand")));
    Controller->LocalSheathState = 0; // An unrelated server update can still contain stale sheath state.
    Controller->ApplyLocalWeaponPresentation();
    TestEqual(TEXT("Active melee attack keeps weapon drawn"), Staff->GetAttachSocketName(), FName(TEXT("WowSpellRightHand")));

    // Reproduce starting melee while a cast has temporarily sheathed the weapon.
    Staff->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("WowStaffBackLeft"));
    AWowGameplayController::FSpellWeaponRestoreState Restore;
    Restore.Component = Staff; Restore.OriginalParent = Body;
    Restore.OriginalSocket = TEXT("WowStaffBackLeft");
    Restore.OriginalRelativeTransform = FTransform::Identity;
    Controller->SpellSheathedWeapons.FindOrAdd(1).Add(Restore);
    Controller->OnServerAttackStart(1, 2);
    TestEqual(TEXT("Casting retains its temporary sheath"), Staff->GetAttachSocketName(), FName(TEXT("WowStaffBackLeft")));
    Controller->RestoreCasterWeaponsAfterSpell(1);
    TestEqual(TEXT("Cast completion restores current attack state, not stale back socket"), Staff->GetAttachSocketName(), FName(TEXT("WowSpellRightHand")));
    Controller->StopAutoAttack();
    TestEqual(TEXT("Stopping an attack does not unexpectedly sheath the weapon"), Staff->GetAttachSocketName(), FName(TEXT("WowSpellRightHand")));
    Controller->LocalSheathState = 0;
    Controller->bLocalSheathStateInitialized = true;
    Controller->OnServerAttackStart(1, 2);
    TestEqual(TEXT("Server-initiated auto-attack draws melee weapon too"), Controller->LocalSheathState, uint8(1));

    FWowEquipmentManager::ApplyDrawnTransform(Staff);
    // Measured from local DwarfMale Stand track and Stave_2H_Other_D_01 geometry.
    // Model +Y (UE +X) is the back of the skull; its face points toward UE -X.
    const FQuat Palm(.13676458, .05915325, -.06546289, .98666659);
    const FQuat Drawn = Palm.GetNormalized() * Staff->GetRelativeRotation().Quaternion();
    TestTrue(TEXT("Skull points toward viewer-left in dwarf front view"), Drawn.RotateVector(-FVector::ForwardVector).X < -.95f);
    const FQuat Previous = Palm.GetNormalized() * FRotator(0, 0, -75).Quaternion();
    const FVector OldAxis = Previous.RotateVector(FVector::RightVector);
    const FVector NewAxis = Drawn.RotateVector(FVector::RightVector);
    TestTrue(TEXT("Staff tips down exactly 90 degrees around its grip"),
        FMath::Abs(FVector::DotProduct(OldAxis, NewAxis)) < .001f && NewAxis.Z < OldAxis.Z);
    TestTrue(TEXT("Drawing preserves the palm grip origin"), Staff->GetRelativeLocation().IsNearlyZero());
    return true;
}
#endif

