#include "WowTestGameMode.h"
#include "WowFlyCamera.h"
#include "WowDebugHUD.h"
#include "WowWorldManager.h"
#include "WowSkyManager.h"
#include "WowCharacterBuilder.h"
#include "WowEquipmentManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMeshActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "GameFramework/FloatingPawnMovement.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowTestMode, Log, All);

namespace
{
bool IsNamedTestMap(UWorld* World, const TCHAR* ExpectedName)
{
    if (!World)
    {
        return false;
    }

    FString MapName = World->GetMapName();
    MapName.RemoveFromStart(World->StreamingLevelsPrefix);
    return MapName.Equals(ExpectedName, ESearchCase::IgnoreCase);
}

struct FCharacterTestEquipment
{
    uint32 WeaponDisplayId = 0;
    uint32 HelmetDisplayId = 0;
    uint32 ShoulderDisplayId = 0;
};

FCharacterTestEquipment FindCharacterTestEquipment(FMpqManager* Mpq)
{
    FCharacterTestEquipment Result;
    if (!Mpq)
    {
        return Result;
    }

    FString ResolvedModelPath;
    for (const FItemDisplayInfoDbcEntry& Entry : FDbcStore::Get().ItemDisplayInfo().GetAll())
    {
        if (Result.WeaponDisplayId == 0 &&
            FWowEquipmentManager::ResolveEquipmentPaths(Mpq, Entry.ID, FWowEquipmentManager::EAttachmentPoint::RightHand, ResolvedModelPath))
        {
            Result.WeaponDisplayId = Entry.ID;
        }

        if (Result.HelmetDisplayId == 0 &&
            FWowEquipmentManager::ResolveEquipmentPaths(Mpq, Entry.ID, FWowEquipmentManager::EAttachmentPoint::Helmet, ResolvedModelPath))
        {
            Result.HelmetDisplayId = Entry.ID;
        }

        if (Result.ShoulderDisplayId == 0 &&
            FWowEquipmentManager::ResolveEquipmentPaths(Mpq, Entry.ID, FWowEquipmentManager::EAttachmentPoint::LeftShoulder, ResolvedModelPath))
        {
            Result.ShoulderDisplayId = Entry.ID;
        }

        if (Result.WeaponDisplayId != 0 && Result.HelmetDisplayId != 0 && Result.ShoulderDisplayId != 0)
        {
            break;
        }
    }

    UE_LOG(LogWowTestMode, Log, TEXT("CharacterTest equipment: weapon=%u helmet=%u shoulder=%u"),
        Result.WeaponDisplayId, Result.HelmetDisplayId, Result.ShoulderDisplayId);

    return Result;
}

void AddEquipmentIfAvailable(TArray<FWowCharacterBuilder::FEquipmentSlot>& OutEquipment,
    uint32 ItemDisplayId, FWowEquipmentManager::EAttachmentPoint AttachPoint)
{
    if (ItemDisplayId == 0)
    {
        return;
    }

    FWowCharacterBuilder::FEquipmentSlot Slot;
    Slot.ItemDisplayId = ItemDisplayId;
    Slot.AttachPoint = AttachPoint;
    OutEquipment.Add(Slot);
}
}

AWowTestGameMode::AWowTestGameMode()
{
    DefaultPawnClass = AWowFlyCamera::StaticClass();
    HUDClass = AWowDebugHUD::StaticClass();
}

void AWowTestGameMode::BeginPlay()
{
    Super::BeginPlay();

    UWorld* World = GetWorld();
    if (!World) return;

    SetupPostProcess(World);

    CachedWorldManager = SpawnWorldManager(World);

    SetupTestScene(World, CachedWorldManager);
}

void AWowTestGameMode::SetupTestScene(UWorld* World, AWowWorldManager* WorldManager)
{
    if (IsNamedTestMap(World, TEXT("CharacterTest")))
    {
        UE_LOG(LogWowTestMode, Log, TEXT("Setting up CharacterTest scene"));

        // Sky manager provides sun directional light + sky atmosphere + sky light
        SpawnSkyManager(World);

        // Large ground plane so nothing floats in the void
        SpawnGroundPlane(World, FVector(200.0f, 0.0f, -5.0f), 60.0f);

        // Front fill light to reduce harsh shadows on characters
        if (APointLight* FillLight = World->SpawnActor<APointLight>(
            APointLight::StaticClass(), FTransform(FRotator::ZeroRotator, FVector(-600.0f, 0.0f, 400.0f))))
        {
            if (UPointLightComponent* FillLightComponent = Cast<UPointLightComponent>(FillLight->GetLightComponent()))
            {
                FillLightComponent->SetIntensity(20000.0f);
                FillLightComponent->SetAttenuationRadius(2000.0f);
                FillLightComponent->SetLightColor(FLinearColor(1.0f, 0.97f, 0.92f));
            }
        }

        // Back fill light for rim lighting
        if (APointLight* BackLight = World->SpawnActor<APointLight>(
            APointLight::StaticClass(), FTransform(FRotator::ZeroRotator, FVector(600.0f, 0.0f, 250.0f))))
        {
            if (UPointLightComponent* BackLightComponent = Cast<UPointLightComponent>(BackLight->GetLightComponent()))
            {
                BackLightComponent->SetIntensity(10000.0f);
                BackLightComponent->SetAttenuationRadius(2000.0f);
                BackLightComponent->SetLightColor(FLinearColor(0.85f, 0.9f, 1.0f));
            }
        }

        // Position camera to view the full gallery (delayed one frame for pawn to spawn)
        // Also slow down fly speed for this small test scene
        FTimerHandle CameraFrameTimer;
        World->GetTimerManager().SetTimer(CameraFrameTimer, [this]()
        {
            if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
            {
                if (APawn* Pawn = PC->GetPawn())
                {
                    Pawn->SetActorLocation(FVector(-900.0f, 0.0f, 200.0f));
                    PC->SetControlRotation(FRotator(-5.0f, 8.0f, 0.0f));

                    // Slow fly speed for test scene (default 20000 is for terrain)
                    if (AWowFlyCamera* FlyCamera = Cast<AWowFlyCamera>(Pawn))
                    {
                        FlyCamera->FlySpeed = 1500.0f;
                        if (UFloatingPawnMovement* Mov = Cast<UFloatingPawnMovement>(FlyCamera->GetMovementComponent()))
                        {
                            Mov->MaxSpeed = 1500.0f;
                            Mov->Acceleration = 6000.0f;
                            Mov->Deceleration = 6000.0f;
                        }
                    }
                }
            }
        }, 0.1f, false);

        if (!WorldManager || !WorldManager->GetMpqManager() || !WorldManager->GetAssetCache())
        {
            UE_LOG(LogWowTestMode, Warning, TEXT("CharacterTest scene missing MPQ manager or asset cache"));
            return;
        }

        FMpqManager* Mpq = WorldManager->GetMpqManager();
        FWowAssetCache* AssetCache = WorldManager->GetAssetCache();
        const FCharacterTestEquipment Equipment = FindCharacterTestEquipment(Mpq);

        // --- Row 1: Player characters (front row, spaced along Y) ---
        const float CharacterRowX = 0.0f;
        const float CharacterSpacing = 400.0f;
        const FRotator FacingCamera(0.0f, -90.0f, 0.0f);

        // 1. Human Male (center) with equipment
        FWowCharacterBuilder::FCharacterParams HumanMale;
        HumanMale.Race = FWowCharacterBuilder::ERace::Human;
        HumanMale.Gender = FWowCharacterBuilder::EGender::Male;
        HumanMale.Customization.RaceId = 1;
        HumanMale.Customization.Gender = 0;
        HumanMale.Customization.SkinColor = 0;
        HumanMale.Customization.FaceVariation = 1;
        AddEquipmentIfAvailable(HumanMale.Equipment, Equipment.WeaponDisplayId, FWowEquipmentManager::EAttachmentPoint::RightHand);
        AddEquipmentIfAvailable(HumanMale.Equipment, Equipment.HelmetDisplayId, FWowEquipmentManager::EAttachmentPoint::Helmet);
        AddEquipmentIfAvailable(HumanMale.Equipment, Equipment.ShoulderDisplayId, FWowEquipmentManager::EAttachmentPoint::LeftShoulder);
        AddEquipmentIfAvailable(HumanMale.Equipment, Equipment.ShoulderDisplayId, FWowEquipmentManager::EAttachmentPoint::RightShoulder);
        FWowCharacterBuilder::SpawnCharacterWithEquipment(World, Mpq, AssetCache,
            HumanMale, FVector(CharacterRowX, 0.0f, 0.0f), FacingCamera);

        // 2. Human Female (right of center)
        FWowCharacterBuilder::FCharacterParams HumanFemale;
        HumanFemale.Race = FWowCharacterBuilder::ERace::Human;
        HumanFemale.Gender = FWowCharacterBuilder::EGender::Female;
        HumanFemale.Customization.RaceId = 1;
        HumanFemale.Customization.Gender = 1;
        HumanFemale.Customization.SkinColor = 2;
        HumanFemale.Customization.FaceVariation = 2;
        AddEquipmentIfAvailable(HumanFemale.Equipment, Equipment.WeaponDisplayId, FWowEquipmentManager::EAttachmentPoint::RightHand);
        FWowCharacterBuilder::SpawnCharacterWithEquipment(World, Mpq, AssetCache,
            HumanFemale, FVector(CharacterRowX, CharacterSpacing, 0.0f), FacingCamera);

        // 3. Orc Male (left of center)
        FWowCharacterBuilder::FCharacterParams OrcMale;
        OrcMale.Race = FWowCharacterBuilder::ERace::Orc;
        OrcMale.Gender = FWowCharacterBuilder::EGender::Male;
        OrcMale.Customization.RaceId = 2;
        OrcMale.Customization.Gender = 0;
        OrcMale.Customization.SkinColor = 1;
        OrcMale.Customization.FaceVariation = 1;
        FWowCharacterBuilder::SpawnCharacterWithEquipment(World, Mpq, AssetCache,
            OrcMale, FVector(CharacterRowX, -CharacterSpacing, 0.0f), FacingCamera);

        // --- Row 2: Creatures (back row, spaced along Y) ---
        const float CreatureRowX = 400.0f;
        const float CreatureSpacing = 400.0f;

        // Wolf (DisplayId 604)
        FWowCharacterBuilder::SpawnCreatureByDisplayId(World, Mpq, AssetCache,
            604, FVector(CreatureRowX, -CreatureSpacing, 0.0f), FacingCamera);

        // Boar (DisplayId 113)
        FWowCharacterBuilder::SpawnCreatureByDisplayId(World, Mpq, AssetCache,
            113, FVector(CreatureRowX, 0.0f, 0.0f), FacingCamera);

        // Murloc (DisplayId 285)
        FWowCharacterBuilder::SpawnCreatureByDisplayId(World, Mpq, AssetCache,
            285, FVector(CreatureRowX, CreatureSpacing, 0.0f), FacingCamera);

        UE_LOG(LogWowTestMode, Log, TEXT("CharacterTest: spawned 3 characters + 3 creatures"));
        return;
    }

    // Base implementation does nothing — subclasses override this
    UE_LOG(LogWowTestMode, Log, TEXT("Base test scene — no additional setup"));
}

void AWowTestGameMode::SetupPostProcess(UWorld* World)
{
    if (IConsoleVariable* PreExpCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.EyeAdaptation.PreExposureOverride")))
    {
        PreExpCVar->Set(1.0f);
    }

    const auto SetCVar = [](const TCHAR* Name, int32 Value)
    {
        if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
        {
            CVar->Set(Value);
        }
    };
    SetCVar(TEXT("r.DefaultFeature.MotionBlur"), 0);
    SetCVar(TEXT("r.MotionBlurQuality"), 0);
    SetCVar(TEXT("r.DefaultFeature.DepthOfField"), 0);
    SetCVar(TEXT("r.DepthOfFieldQuality"), 0);

    APostProcessVolume* PPV = World->SpawnActor<APostProcessVolume>();
    if (PPV)
    {
        PPV->bUnbound = true;
        PPV->Settings.bOverride_AutoExposureMethod = true;
        PPV->Settings.AutoExposureMethod = AEM_Manual;
        PPV->Settings.bOverride_AutoExposureBias = true;
        PPV->Settings.AutoExposureBias = 0.0f;
        PPV->Settings.bOverride_AutoExposureMinBrightness = true;
        PPV->Settings.AutoExposureMinBrightness = 1.0f;
        PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
        PPV->Settings.AutoExposureMaxBrightness = 1.0f;
    }
}

AWowWorldManager* AWowTestGameMode::SpawnWorldManager(UWorld* World)
{
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, AWowWorldManager::StaticClass(), Found);

    if (Found.Num() > 0)
    {
        return Cast<AWowWorldManager>(Found[0]);
    }

    FActorSpawnParameters Params;
    Params.Name = FName(TEXT("WowWorldManager"));
    AWowWorldManager* WM = World->SpawnActor<AWowWorldManager>(
        AWowWorldManager::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
    if (WM)
    {
        UE_LOG(LogWowTestMode, Log, TEXT("Spawned WowWorldManager for MPQ access"));
    }
    return WM;
}

void AWowTestGameMode::SpawnGroundPlane(UWorld* World, const FVector& Center, float Size)
{
    UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (!PlaneMesh) return;

    AStaticMeshActor* PlaneActor = World->SpawnActor<AStaticMeshActor>(
        AStaticMeshActor::StaticClass(), FTransform(FRotator::ZeroRotator, Center));
    if (!PlaneActor) return;

    PlaneActor->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
    PlaneActor->SetActorScale3D(FVector(Size, Size, 1.0f));
    PlaneActor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    PlaneActor->GetStaticMeshComponent()->SetCollisionObjectType(ECC_WorldStatic);
    PlaneActor->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
}

void AWowTestGameMode::SpawnDirectionalLight(UWorld* World)
{
    ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
        ADirectionalLight::StaticClass(), FTransform(FRotator(-45.0f, 30.0f, 0.0f), FVector::ZeroVector));
    if (UDirectionalLightComponent* LightComponent = Sun
        ? Cast<UDirectionalLightComponent>(Sun->GetLightComponent())
        : nullptr)
    {
        LightComponent->SetIntensity(3.14159f);
        LightComponent->SetLightColor(FLinearColor(1.0f, 0.95f, 0.85f));
        LightComponent->SetForwardShadingPriority(0);
    }
}

void AWowTestGameMode::SpawnSkyManager(UWorld* World)
{
    FActorSpawnParameters SkyParams;
    SkyParams.Name = FName(TEXT("WowSkyManager"));
    World->SpawnActor<AWowSkyManager>(AWowSkyManager::StaticClass(),
        FVector::ZeroVector, FRotator::ZeroRotator, SkyParams);
}
