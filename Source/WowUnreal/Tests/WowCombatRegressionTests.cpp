#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../WowGameplayController.h"
#include "../WowPlayerCharacter.h"
#include "../WowNameplateWidget.h"
#include "../WowCameraFocusTrace.h"
#include "WowRetainedSlateBrush.h"
#include "Widgets/SWowLootWindow.h"
#include "WowWeatherParticlePolicy.h"
#include "WowConnectionManager.h"
#include "WowClientPreferences.h"
#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Texture2D.h"
#include "UObject/GarbageCollection.h"
#include "Misc/ScopeExit.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLootTextureLifetimeTest,
    "WowUnreal.CombatRegression.LootAndVendorTextureLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowLootTextureLifetimeTest::RunTest(const FString&)
{
    TWeakObjectPtr<UTexture2D> Retained, Unowned;
    TSharedPtr<FWowRetainedSlateBrush> Brush = MakeShared<FWowRetainedSlateBrush>();
    FSlateBrush OldBrush;
    Retained = NewObject<UTexture2D>();
    Unowned = NewObject<UTexture2D>();
    Brush->SetResourceObject(Retained.Get());
    OldBrush.SetResourceObject(Unowned.Get());
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Reproduces old untracked brush losing its texture during GC"), Unowned.IsValid());
    TestTrue(TEXT("Loot/vendor brush keeps its resource alive during GC"), Retained.IsValid());
    if (Retained.IsValid()) TestEqual(TEXT("Slate can safely read the resource name after GC"), Brush->GetResourceName(), Retained->GetFName());
    Brush.Reset();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Texture is released when its last brush is released"), Retained.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowCombatRegressionTest,
    "WowUnreal.CombatRegression.StanceTargetAndWeatherFocus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowCombatRegressionTest::RunTest(const FString&)
{
    const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    auto* Controller = World->SpawnActor<AWowGameplayController>();
    auto* Pawn = World->SpawnActor<AWowPlayerCharacter>();
    Controller->SetPawn(Pawn);
    Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    auto* Anim = NewObject<UWowAnimationController>(Pawn);
    Pawn->AnimationController = Anim;
    Anim->MeshComponent = Pawn->GetMesh();
    auto* Clip = NewObject<UAnimSequence>();
    Anim->AllAnimations.Add(Clip);
    for (int32 Id : {0,5,25,26,27,28,41,42}) Anim->AnimationCache.Add(Id, Clip);
    auto* Staff = NewObject<UStaticMeshComponent>(Pawn);
    Pawn->AddInstanceComponent(Staff);
    Staff->ComponentTags = {TEXT("WowHeldWeapon"),TEXT("WowStaffWeapon")};
    TestEqual(TEXT("Staff uses the authored long two-hand ready stance"), Anim->GetMeleeReadyAnimation(), EWowAnimId::Ready2HL);
    Controller->bIsAutoAttacking = true;
    Controller->UpdatePlayerAnimations();
    TestEqual(TEXT("Right-click attack enters combat while stationary before server combat flag"), Anim->GetCurrentState(), EWowAnimState::Combat);
    Anim->UpdateLocalPlayerState(Pawn, true);
    TestEqual(TEXT("Between swings remains combat idle"), Anim->GetCurrentState(), EWowAnimState::Combat);
    Pawn->GetCharacterMovement()->Velocity = FVector(500,0,0);
    Anim->UpdateLocalPlayerState(Pawn, true);
    TestEqual(TEXT("Combat does not override running"), Anim->GetCurrentState(), EWowAnimState::Running);
    Pawn->GetCharacterMovement()->Velocity = FVector::ZeroVector;
    Controller->bIsAutoAttacking = false;
    Controller->bWasInCombat = false;
    Controller->UpdatePlayerAnimations();
    TestEqual(TEXT("Leaving combat restores idle"), Anim->GetCurrentState(), EWowAnimState::Idle);

    Controller->ConnectionManager = NewObject<UWowConnectionManager>(Controller);
    auto& Manager = Controller->ConnectionManager->PacketHandler.EntityManager;
    Manager.LocalPlayerGuid = 1;
    auto& Entity = Manager.PromoteToTyped(2, WowTypeMask::UNIT);
    Entity.Entry = 99;
    Entity.SetField(UnitField::HEALTH, 100); Entity.SetField(UnitField::MAXHEALTH, 100);
    auto* Mob = World->SpawnActor<AActor>();
    auto* Root = NewObject<USceneComponent>(Mob); Mob->SetRootComponent(Root); Mob->AddInstanceComponent(Root); Root->RegisterComponent();
    Mob->SetActorLocation(FVector(500,0,0));
    auto* PickingCapsule = NewObject<UCapsuleComponent>(Mob, TEXT("TargetCapsule"));
    Mob->AddInstanceComponent(PickingCapsule); PickingCapsule->SetupAttachment(Root);
    PickingCapsule->SetCapsuleSize(100,200); PickingCapsule->SetRelativeLocation(FVector(0,0,200));
    PickingCapsule->SetHiddenInGame(true); PickingCapsule->ComponentTags.Add(TEXT("WowTargetProxy"));
    PickingCapsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    PickingCapsule->SetCollisionResponseToAllChannels(ECR_Ignore);
    PickingCapsule->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    PickingCapsule->RegisterComponent();
    Controller->SpawnedEntityActors.Add(2,Mob);
    Controller->ConnectionManager->PacketHandler.CreatureNameCache.Add(99,TEXT("Test Wolf"));
    Controller->bGameplayHudVisible = true;
    Controller->TargetGuid = 2;
    Controller->UpdateTargetHighlight();
    if (TestNotNull(TEXT("Target ring created"), Controller->TargetHighlightCircle.Get()))
    {
        TestFalse(TEXT("Target ring is not owned by the hidden controller"), Controller->TargetHighlightCircle->GetOwner()->IsHidden());
        TestEqual(TEXT("Ring follows the selected unit"), Controller->TargetHighlightCircle->GetAttachParent(), Root);
        TestTrue(TEXT("Selected ring visible"), Controller->TargetHighlightCircle->IsVisible());
        TestEqual(TEXT("NPC click capsule does not bury ring two metres under its feet"),
            Controller->TargetHighlightCircle->GetComponentLocation().Z, Mob->GetActorLocation().Z + 3.0);
    }
    const int32 OriginalMode = FWowClientPreferences::GetNameplateMode();
    const FString OriginalIni = GGameUserSettingsIni;
    const FString TestIni = FPaths::ProjectSavedDir() / TEXT("Automation/CombatNameplateTest.ini");
    FConfigFile TestConfig; TestConfig.bCanSaveAllSections = true; TestConfig.NoSave = false;
    GConfig->Add(TestIni, TestConfig); GGameUserSettingsIni = TestIni;
    ON_SCOPE_EXIT
    {
        FWowClientPreferences::SetNameplateMode(OriginalMode);
        GGameUserSettingsIni = OriginalIni; GConfig->UnloadFile(TestIni);
        IFileManager::Get().Delete(*TestIni);
    };
    FWowClientPreferences::SetNameplateMode(2); // Only the selected target retains its name and health bar.
    Controller->EnsureEntityNameplate(Entity, Mob);
    Controller->RefreshWorldLabels();
    if (auto* Nameplate = Cast<UWowNameplateWidget>(Controller->EntityNameplates.FindRef(2)->GetUserWidgetObject()))
    {
        TestTrue(TEXT("Selected unit has a world name in target-only mode"), Nameplate->bShowName);
        TestTrue(TEXT("Target-only mode retains the selected health bar"), Nameplate->bShowBar);
        TestEqual(TEXT("Correct selected name"), Nameplate->CachedName, FString(TEXT("Test Wolf")));
    }
    else AddError(TEXT("Nameplate was not created"));
    Controller->TargetGuid = 0;
    Controller->RefreshWorldLabels();
    if (auto* Nameplate = Cast<UWowNameplateWidget>(Controller->EntityNameplates.FindRef(2)->GetUserWidgetObject()))
        TestFalse(TEXT("Clearing selection restores hidden names preference"), Nameplate->bShowName);
    Controller->UpdateTargetHighlight();
    TestFalse(TEXT("Clearing target hides ring"), Controller->TargetHighlightCircle->IsVisible());

    auto Box = [World](const FVector& Location)
    {
        AActor* Actor = World->SpawnActor<AActor>();
        auto* Component = NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Component); Actor->AddInstanceComponent(Component);
        Component->SetBoxExtent(FVector(20)); Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Component->SetCollisionResponseToAllChannels(ECR_Block); Component->RegisterComponent();
        Actor->SetActorLocation(Location); return Component;
    };
    const FVector Start(0,10000,0), End(2000,10000,0);
    auto* Weather = Box(Start + FVector(100,0,0));
    Weather->ComponentTags.Add(TEXT("WowNoCameraFocus")); // Intentionally collidable to exercise defense.
    auto* Effect = Box(Start + FVector(300,0,0));
    Effect->GetOwner()->Tags.Add(TEXT("WowSpellVisual"));
    auto* Surface = Box(Start + FVector(1000,0,0));
    for (ECollisionChannel Channel : {ECC_Visibility, ECC_Camera})
    {
        FHitResult Hit;
        TestTrue(TEXT("Focus reaches solid scenery behind weather and spell effects"), WowCameraFocus::Trace(World,Hit,Start,End,Channel,FCollisionQueryParams()));
        TestEqual(TEXT("Weather never wins autofocus"), Hit.GetComponent(), static_cast<UPrimitiveComponent*>(Surface));
    }
    // A dead wolf retains this invisible click capsule until it despawns.
    // Camera is above the visible body, looking through its oversized proxy.
    Mob->SetActorLocation(FVector(500,20000,0));
    const FVector CorpseView(0,20000,200), CorpseEnd(2000,20000,200);
    auto* BeyondCorpse = Box(FVector(1000,20000,200));
    FHitResult PickHit, FocusHit;
    TestTrue(TEXT("Corpse remains clickable for loot"), World->LineTraceSingleByChannel(PickHit,CorpseView,CorpseEnd,ECC_Visibility));
    TestEqual(TEXT("Picking still hits the invisible corpse capsule"), PickHit.GetComponent(), static_cast<UPrimitiveComponent*>(PickingCapsule));
    TestTrue(TEXT("Autofocus passes through corpse picking capsule"), WowCameraFocus::Trace(World,FocusHit,CorpseView,CorpseEnd,ECC_Visibility,FCollisionQueryParams()));
    TestEqual(TEXT("View above corpse focuses on scenery"), FocusHit.GetComponent(), static_cast<UPrimitiveComponent*>(BeyondCorpse));
    TestTrue(TEXT("Camera inside corpse picking capsule still focuses on scenery"), WowCameraFocus::Trace(World,FocusHit,FVector(500,20000,200),CorpseEnd,ECC_Visibility,FCollisionQueryParams()));
    TestEqual(TEXT("Inside capsule does not force near-zero focus"), FocusHit.GetComponent(), static_cast<UPrimitiveComponent*>(BeyondCorpse));
    PickingCapsule->DestroyComponent();
    TestTrue(TEXT("Despawn retains the same scene focus"), WowCameraFocus::Trace(World,FocusHit,CorpseView,CorpseEnd,ECC_Visibility,FCollisionQueryParams()));
    TestEqual(TEXT("Focus is unchanged by corpse proxy removal"), FocusHit.GetComponent(), static_cast<UPrimitiveComponent*>(BeyondCorpse));
    auto* Hidden = Box(FVector(500,20000,200)); Hidden->GetOwner()->SetActorHiddenInGame(true);
    TestTrue(TEXT("Hidden actors cannot steal focus"), WowCameraFocus::Trace(World,FocusHit,CorpseView,CorpseEnd,ECC_Visibility,FCollisionQueryParams()));
    TestEqual(TEXT("Hidden actor is skipped"), FocusHit.GetComponent(), static_cast<UPrimitiveComponent*>(BeyondCorpse));
    TestEqual(TEXT("Empty sky retains the last solid focus"), WowCameraFocus::SceneDistance(false,0,980), 980.0f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLootCursorAnchorTest,
    "WowUnreal.CombatRegression.LootCursorAnchor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowLootCursorAnchorTest::RunTest(const FString&)
{
    auto Window = SNew(SWowLootWindow);
    FWowLootItem Item; Item.ItemId = 1; Item.Count = 1; Item.SlotType = 0;
    Window->UpdateLoot(1,0,0,{Item}); Window->SlatePrepass();
    const FVector2D ItemOnly = Window->GetFirstLootAnchor();
    TestTrue(TEXT("Anchor is below title, inside first row"), ItemOnly.Y > 40 && ItemOnly.Y < Window->GetDesiredSize().Y - 12);
    Window->UpdateLoot(2,0,123,{Item}); Window->SlatePrepass();
    const FVector2D WithMoney = Window->GetFirstLootAnchor();
    TestTrue(TEXT("Money row moves item anchor down instead of leaving pointer on money/title"), WithMoney.Y > ItemOnly.Y + 10);
    Window->UpdateLoot(3,0,123,{}); Window->SlatePrepass();
    const FVector2D MoneyOnly = Window->GetFirstLootAnchor();
    TestTrue(TEXT("Money-only loot uses the money button"), MoneyOnly.Y < WithMoney.Y && MoneyOnly.Y > 30);
    Window->UpdateLoot(4,0,0,{Item}); Window->SlatePrepass();
    TestTrue(TEXT("Reopening uses current layout instead of previous corpse geometry"), Window->GetFirstLootAnchor().Equals(ItemOnly,0.1));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowWeatherCameraContinuityTest,
    "WowUnreal.CombatRegression.WeatherCameraContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowWeatherCameraContinuityTest::RunTest(const FString&)
{
    constexpr int32 Grid = 6; constexpr float CellSize = 2200.f / Grid;
    const FVector Cell(800,500,0);
    const int32 Slot = WowWeatherParticles::SurfaceSlot(Cell,CellSize,Grid);
    TestEqual(TEXT("Camera movement does not reindex a retained world cell"),
        WowWeatherParticles::SurfaceSlot(Cell+FVector(0,0,700),CellSize,Grid), Slot);
    TestEqual(TEXT("Wrapped cells reuse bounded storage"),
        WowWeatherParticles::SurfaceSlot(Cell+FVector(CellSize*Grid,0,0),CellSize,Grid), Slot);
    TestTrue(TEXT("Negative world coordinates address valid slots"),
        WowWeatherParticles::SurfaceSlot(-Cell,CellSize,Grid)>=0);
    TestEqual(TEXT("Newly revealed cells do not hide all snow during orbit"),
        WowWeatherParticles::GroundOpacity(false,true,-100),1.f);
    TestEqual(TEXT("Missing terrain samples do not delete airborne precipitation"),
        WowWeatherParticles::GroundOpacity(true,false,-100),1.f);
    TestEqual(TEXT("Known ground still removes below-ground precipitation"),
        WowWeatherParticles::GroundOpacity(true,true,-100),0.f);
    TestEqual(TEXT("Snow above sampled ground stays visible"),
        WowWeatherParticles::GroundOpacity(true,true,100),1.f);
    return true;
}
#endif

