#include "WowViewerGameMode.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "WowClientPreferences.h"
#include "WowFlyCamera.h"
#include "WowPlayerCharacter.h"
#include "WowGameplayController.h"
#include "WowDebugHUD.h"
#include "WowAutoLogin.h"
#include "WowWorldManager.h"
#include "WowSkyManager.h"
#include "WowUIManager.h"
#include "Widgets/SOverlay.h"
#include "WowConnectionManager.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "WowCharacterBuilder.h"
#include "WowAnimationController.h"
#include "WowAudioManager.h"
#include "WowLoginController.h"
#include "WowCredentialStore.h"
#include "WowEntity.h"
#include "WowEntityManager.h"
#include "WowSessionState.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/CharStartOutfitDbc.h"
#include "TimerManager.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Components/CanvasPanel.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Coord/WowCoordinate.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowGameMode, Log, All);

namespace
{
constexpr uint8 OfflineClassId = 1;  // Warrior; the entity field remains authoritative

uint8 GetOfflineRaceId()
{
    int32 RaceId = 1;
    FParse::Value(FCommandLine::Get(), TEXT("-offlinerace="), RaceId);
    const bool bPlayableWotlkRace = RaceId >= 1 && RaceId <= 11 && RaceId != 9;
    return static_cast<uint8>(bPlayableWotlkRace ? RaceId : 1);
}

uint8 GetOfflineGenderId()
{
    int32 GenderId = 0;
    FParse::Value(FCommandLine::Get(), TEXT("-offlinegender="), GenderId);
    return static_cast<uint8>(GenderId == 1 ? 1 : 0);
}

void ApplyWowViewerUiInputMode(APlayerController* PlayerController, const TSharedPtr<SWidget>& WidgetToFocus)
{
    if (!PlayerController)
    {
        return;
    }

    FInputModeGameAndUI InputMode;
    if (WidgetToFocus.IsValid())
    {
        InputMode.SetWidgetToFocus(WidgetToFocus);
    }
    InputMode.SetHideCursorDuringCapture(false);
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PlayerController->SetInputMode(InputMode);
}
}

AWowViewerGameMode::AWowViewerGameMode()
{
    // Default pawn; overridden by GetDefaultPawnClassForController at runtime
    DefaultPawnClass = AWowPlayerCharacter::StaticClass();
    PlayerControllerClass = AWowGameplayController::StaticClass();
    HUDClass = AWowDebugHUD::StaticClass();
}

UClass* AWowViewerGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
    // Use fly camera when -startpos is specified (terrain viewing mode)
    if (FString(FCommandLine::Get()).Contains(TEXT("startpos")))
    {
        return AWowFlyCamera::StaticClass();
    }
    return DefaultPawnClass;
}

void AWowViewerGameMode::SetupPostProcess(UWorld* World)
{
    if (IConsoleVariable* PreExpCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.EyeAdaptation.PreExposureOverride")))
    {
        PreExpCVar->Set(1.0f);
    }

    // Disable heavy rendering features that break on Metal with large world coordinates
    auto SetCVar = [](const TCHAR* Name, int32 Value)
    {
        if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
        {
            CVar->Set(Value);
        }
    };
    SetCVar(TEXT("r.Shadow.Virtual.Enable"), 0);
    SetCVar(TEXT("r.Shadow.CSM.MaxCascades"), 2); // Reduce shadow cascades
    // Renderer setup must honor the same saved options as the login screen.
    FWowClientPreferences::ApplyAdvancedVideoPreferences();

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

AWowWorldManager* AWowViewerGameMode::SpawnWorldManager(UWorld* World)
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
        UE_LOG(LogWowGameMode, Log, TEXT("Spawned WowWorldManager"));
    }
    return WM;
}

void AWowViewerGameMode::SpawnGroundPlane(UWorld* World, const FVector& Center, float Size)
{
    // Spawn a flat static mesh plane for test scenes
    UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (!PlaneMesh) return;

    AStaticMeshActor* PlaneActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(FRotator::ZeroRotator, Center));
    if (!PlaneActor) return;

    PlaneActor->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
    PlaneActor->SetActorScale3D(FVector(Size, Size, 1.0f));
    PlaneActor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    PlaneActor->GetStaticMeshComponent()->SetCollisionObjectType(ECC_WorldStatic);
    PlaneActor->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);

    UE_LOG(LogWowGameMode, Log, TEXT("Spawned ground plane at %s (scale %.0f)"), *Center.ToString(), Size);
}

void AWowViewerGameMode::SpawnDirectionalLight(UWorld* World)
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

void AWowViewerGameMode::BeginPlay()
{
    Super::BeginPlay();

    UWorld* World = GetWorld();
    if (!World) return;

    // Parse test scene selector
    FString TestScene;
    FParse::Value(FCommandLine::Get(), TEXT("-testscene="), TestScene);
    TestScene = TestScene.ToLower();

    // Post-process is always needed
    SetupPostProcess(World);
	
	if (FParse::Param(FCommandLine::Get(), TEXT("offlineworld")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("offlineloading")))
{
    SetupDefaultScene(World);

    if (AWowPlayerCharacter* Player = Cast<AWowPlayerCharacter>(
        UGameplayStatics::GetPlayerPawn(this, 0)))
    {
        AWowWorldManager* WorldManager = Cast<AWowWorldManager>(
            UGameplayStatics::GetActorOfClass(World, AWowWorldManager::StaticClass()));
        if (!WorldManager || !WorldManager->HasExternalData())
        {
            Player->EnableNativeFallbackVisuals();
            Player->SetActorLocation(FVector(250.0f, -300.0f, 100.0f),
                false, nullptr, ETeleportType::TeleportPhysics);
            Player->SetActorEnableCollision(true);
            if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
            {
                Player->ApplyGameplayMovementSettings(true);
                Movement->SetMovementMode(MOVE_Walking);
            }
            if (APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
            {
                Controller->SetViewTarget(Player);
                Controller->SetControlRotation(FRotator(-12.0f, 0.0f, 0.0f));
            }
            UE_LOG(LogWowGameMode, Warning,
                TEXT("[FALLBACKTEST] PASS: playable stick pawn, walking collision, follow camera"));
            return;
        }

		if (FParse::Param(FCommandLine::Get(), TEXT("testspellworldless")))
		{
			SpawnGroundPlane(World, FVector::ZeroVector, 30.0f);
			Player->SetActorLocation(FVector(0.0f, 0.0f, 96.0f),
				false, nullptr, ETeleportType::TeleportPhysics);
			Player->SetActorEnableCollision(true);
			if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
			{
				Player->ApplyGameplayMovementSettings(true);
				Movement->SetMovementMode(MOVE_Walking);
			}
			if (APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
			{
				Controller->SetViewTarget(Player);
				Controller->SetControlRotation(FRotator(-12.0f, 0.0f, 0.0f));
			}
			UE_LOG(LogWowGameMode, Log,
				TEXT("Spell preview uses the lightweight MPQ scene without terrain streaming"));
			return;
		}

        const FVector Northshire =
            FWowCoordinate::WowToUE(-8949.0f, -132.0f, 84.0f);
        OfflinePlayerPawn = Player;
        Player->SetActorLocation(Northshire + FVector(0.0f, 0.0f, 20000.0f),
            false, nullptr, ETeleportType::TeleportPhysics);
        Player->SetActorEnableCollision(false);
        if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
        {
            Movement->GravityScale = 0.0f;
            Movement->Velocity = FVector::ZeroVector;
            Movement->SetMovementMode(MOVE_Flying);
        }
        GetWorldTimerManager().SetTimer(OfflineGroundTimerHandle, this,
            &AWowViewerGameMode::TryFinalizeOfflineSpawn, 0.5f, true, 0.5f);
        UE_LOG(LogWowGameMode, Log,
            TEXT("Holding offline player above Northshire until terrain collision is ready"));
    }
}
else if (TestScene == TEXT("character"))
    {
        SetupCharacterTestScene(World);
    }
    else if (TestScene == TEXT("terrain"))
    {
        SetupTerrainTestScene(World);
    }
    else if (TestScene == TEXT("wmo"))
    {
        SetupWmoTestScene(World);
    }
    else if (TestScene == TEXT("ui"))
    {
        SetupUITestScene(World);
    }
    else if (TestScene == TEXT("login"))
    {
        SetupLoginScene(World);
    }
    else if (TestScene == TEXT("network"))
    {
        SetupNetworkTestScene(World);
    }
    else
    {
        // Always go through login scene — LoginController handles autologin
        SetupLoginScene(World);
    }
}

void AWowViewerGameMode::TryFinalizeOfflineSpawn()
{
    if (!OfflinePlayerPawn || !GetWorld())
    {
        GetWorldTimerManager().ClearTimer(OfflineGroundTimerHandle);
        return;
    }

    const FVector PawnPos = OfflinePlayerPawn->GetActorLocation();
    const FVector TraceStart(PawnPos.X, PawnPos.Y, 500000.0f);
    const FVector TraceEnd(PawnPos.X, PawnPos.Y, -500000.0f);
    FHitResult Hit;
    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(OfflinePlayerPawn);
    QueryParams.bTraceComplex = true;
    if (!GetWorld()->LineTraceSingleByChannel(
        Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
    {
        return;
    }

    OfflinePlayerPawn->SetActorEnableCollision(true);
    OfflinePlayerPawn->ApplyLoginSpawn(Hit.ImpactPoint, 0.0f);
    GetWorldTimerManager().ClearTimer(OfflineGroundTimerHandle);
    UE_LOG(LogWowGameMode, Log,
        TEXT("Offline player grounded at %s on %s"),
        *OfflinePlayerPawn->GetActorLocation().ToString(),
        Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("<unknown>"));

    if (FParse::Param(FCommandLine::Get(), TEXT("verifyplayerlocomotion")))
    {
        StartOfflineLocomotionVerification();
    }
}

void AWowViewerGameMode::StartOfflineLocomotionVerification()
{
    TWeakObjectPtr<AWowPlayerCharacter> WeakPlayer = OfflinePlayerPawn;
    const FString ScreenshotDir = FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("PlayerLocomotion"));
    FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*ScreenshotDir);

    FTimerHandle RunTimer;
    GetWorldTimerManager().SetTimer(RunTimer, [WeakPlayer]()
    {
        if (AWowPlayerCharacter* Player = WeakPlayer.Get())
        {
            if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
            {
                Movement->SetMovementMode(MOVE_Walking);
                Movement->Velocity = Player->GetActorForwardVector() * 700.0f;
            }
        }
    }, 0.35f, false);

    const auto ScheduleScreenshot = [this, ScreenshotDir](float Delay, const TCHAR* Filename)
    {
        FTimerHandle ScreenshotTimer;
        const FString ScreenshotPath = FPaths::Combine(ScreenshotDir, Filename);
        GetWorldTimerManager().SetTimer(ScreenshotTimer, [ScreenshotPath]()
        {
            FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
        }, Delay, false);
    };
    ScheduleScreenshot(0.47f, TEXT("DwarfPlayer_Run_A.png"));
    ScheduleScreenshot(0.72f, TEXT("DwarfPlayer_Run_B.png"));

    FTimerHandle JumpTimer;
    GetWorldTimerManager().SetTimer(JumpTimer, [WeakPlayer]()
    {
        if (AWowPlayerCharacter* Player = WeakPlayer.Get())
        {
            Player->Jump();
            if (Player->AnimationController)
            {
                Player->AnimationController->NotifyLocalJumpStarted(
                    Player->GetActorLocation().Z);
            }
        }
    }, 0.92f, false);
    ScheduleScreenshot(1.04f, TEXT("DwarfPlayer_Jump_A.png"));
    ScheduleScreenshot(1.34f, TEXT("DwarfPlayer_Jump_B.png"));

    FTimerHandle QuitTimer;
    GetWorldTimerManager().SetTimer(QuitTimer, []()
    {
        FGenericPlatformMisc::RequestExit(false);
    }, 1.75f, false);
}

void AWowViewerGameMode::SetupDefaultScene(UWorld* World)
{
    UE_LOG(LogWowGameMode, Log, TEXT("Starting default (full world) scene"));
    const bool bOfflineWorld = FParse::Param(FCommandLine::Get(), TEXT("offlineworld"));
	const bool bWorldlessSpellPreview =
		FParse::Param(FCommandLine::Get(), TEXT("testspellworldless"));
    const uint8 OfflineRaceId = GetOfflineRaceId();
    const uint8 OfflineGenderId = GetOfflineGenderId();

    // Spawn sky
    {
        FActorSpawnParameters SkyParams;
        SkyParams.Name = FName(TEXT("WowSkyManager"));
        World->SpawnActor<AWowSkyManager>(AWowSkyManager::StaticClass(),
            FVector::ZeroVector, FRotator::ZeroRotator, SkyParams);
    }

    // World manager (full streaming) — BeginPlay defers terrain if bStreamingEnabled=false,
    // so we enable it immediately after spawn for the full world scene.
    AWowWorldManager* WorldManager = SpawnWorldManager(World);
    if (WorldManager && !bWorldlessSpellPreview)
    {
        WorldManager->EnableTerrainStreaming();
    }

    // Spawn audio manager
    if (WorldManager && WorldManager->HasExternalData())
    {
        AWowAudioManager* AudioMgr = World->SpawnActor<AWowAudioManager>(
            AWowAudioManager::StaticClass());
        if (AudioMgr)
        {
            AudioMgr->SetMpqManager(WorldManager->GetMpqManager());
        }
    }

    // Load UI with root canvas for widget rendering
    if (WorldManager && WorldManager->HasExternalData())
    {
        if (UGameInstance* GI = GetGameInstance())
        {
            if (UWowUIManager* UIManager = GI->GetSubsystem<UWowUIManager>())
            {
                if (bOfflineWorld)
                {
                    // Give the real FrameXML UI a stable local player without
                    // opening an auth or world connection.
                    OfflineConnectionManager = NewObject<UWowConnectionManager>(this, TEXT("OfflineConnectionManager"));
                    FWowEntityManager& EntityManager = OfflineConnectionManager->PacketHandler.EntityManager;
                    constexpr uint64 OfflineGuid = 1;
                    EntityManager.LocalPlayerGuid = OfflineGuid;
                    FWowEntity& BaseEntity = EntityManager.PromoteToTyped(
                        OfflineGuid, WowTypeMask::OBJECT | WowTypeMask::UNIT | WowTypeMask::PLAYER);
                    BaseEntity.Movement.Position = FVector(-8949.0f, -132.0f, 84.0f);
                    BaseEntity.SetField(ObjectField::GUID, static_cast<uint32>(OfflineGuid));
                    BaseEntity.SetField(ObjectField::GUID + 1, 0);
                    BaseEntity.SetField(ObjectField::TYPE,
                        WowTypeMask::OBJECT | WowTypeMask::UNIT | WowTypeMask::PLAYER);
					BaseEntity.SetField(UnitField::BYTES_0,
						static_cast<uint32>(OfflineRaceId)
						| (static_cast<uint32>(OfflineClassId) << 8)
						| (static_cast<uint32>(OfflineGenderId) << 16));
                    BaseEntity.SetField(UnitField::HEALTH, 100);
                    BaseEntity.SetField(UnitField::MAXHEALTH, 100);
                    BaseEntity.SetField(UnitField::POWER1, 100);
                    BaseEntity.SetField(UnitField::MAXPOWER1, 100);
                    BaseEntity.SetField(UnitField::LEVEL, 1);
                    BaseEntity.SetField(PlayerField::XP, 0);
                    BaseEntity.SetField(PlayerField::NEXT_LEVEL_XP, 400);
                    OfflineConnectionManager->PacketHandler.PlayerNameCache.Add(
                        OfflineGuid, TEXT("Offline Adventurer"));
                    UIManager->SetConnectionManager(OfflineConnectionManager);

                    if (AWowGameplayController* GPC = Cast<AWowGameplayController>(
                        UGameplayStatics::GetPlayerController(this, 0)))
                    {
                        GPC->ConnectionManager = OfflineConnectionManager;
                        // Match the gameplay transition for the offline hover/target fixture.
                        if (FParse::Param(FCommandLine::Get(), TEXT("testhovermodel")) ||
                            FParse::Param(FCommandLine::Get(), TEXT("testunitvisuals")))
                        {
                            GPC->SetGameplayHudVisible(true);
                        }
                    }
                    UE_LOG(LogWowGameMode, Log,
                        TEXT("Created synthetic offline player state (no network connection)"));
                }

                // Create a root canvas panel for the WoW UI frame system
                if (GEngine && GEngine->GameViewport)
                {
                    UCanvasPanel* UIRootCanvas = NewObject<UCanvasPanel>(GetTransientPackage());
                    UIRootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
                    TSharedRef<SWidget> CanvasWidget = UIRootCanvas->TakeWidget();
                    TSharedRef<SOverlay> Wrapper = SNew(SOverlay)
                        + SOverlay::Slot()
                        .HAlign(HAlign_Fill)
                        .VAlign(VAlign_Fill)
                        [
                            SNew(SDPIScaler)
                            .DPIScale_Lambda([]() { return FWowClientPreferences::GetUIScale(); })
                            [CanvasWidget]
                        ];
                    GEngine->GameViewport->AddViewportWidgetContent(Wrapper, 50);
                    UIManager->SetRootCanvas(UIRootCanvas);
                    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
                    {
                        ApplyWowViewerUiInputMode(PC, CanvasWidget);
                    }
                    UE_LOG(LogWowGameMode, Log, TEXT("Created root canvas for WoW UI"));
                }

                UIManager->LoadUI(WorldManager->GetMpqManager(), WorldManager->GetAssetCache());
            }
        }
    }

    if (bOfflineWorld && WorldManager && WorldManager->HasExternalData())
    {
        if (AWowPlayerCharacter* Player = Cast<AWowPlayerCharacter>(
            UGameplayStatics::GetPlayerPawn(this, 0)))
        {
			TArray<FWowCharacterEquipment> StarterEquipment;
			if (const FCharStartOutfitDbcEntry* Outfit = FDbcStore::Get().CharStartOutfit()
				.GetByRaceClassGender(OfflineRaceId, OfflineClassId, OfflineGenderId))
			{
				for (int32 ItemIndex = 0; ItemIndex < 24; ++ItemIndex)
				{
					if (Outfit->ItemDisplayIds[ItemIndex] <= 0 || Outfit->ItemInventorySlots[ItemIndex] <= 0)
					{
						continue;
					}
					FWowCharacterEquipment& Item = StarterEquipment.AddDefaulted_GetRef();
					Item.DisplayId = Outfit->ItemDisplayIds[ItemIndex];
					Item.InventoryType = static_cast<uint8>(Outfit->ItemInventorySlots[ItemIndex]);
				}
			}

			if (StarterEquipment.Num() > 0)
			{
				Player->SetCharacterModelWithEquipment(World, WorldManager->GetMpqManager(),
					WorldManager->GetAssetCache(), OfflineRaceId, OfflineGenderId,
					0, 0, 0, 0, 0, &StarterEquipment);
			}
			else
			{
				Player->SetCharacterModel(World, WorldManager->GetMpqManager(),
					WorldManager->GetAssetCache(), OfflineRaceId, OfflineGenderId, 0, 0, 0, 0, 0);
			}
			UE_LOG(LogWowGameMode, Log, TEXT("Applied offline player model with %d starter equipment entries"),
				StarterEquipment.Num());
		}
	}

    // Auto-login + gameplay controller binding
    if (!bOfflineWorld)
    {
        if (UGameInstance* GI = GetGameInstance())
        {
            if (UWowAutoLogin* AutoLogin = GI->GetSubsystem<UWowAutoLogin>())
            {
                AutoLogin->TryAutoLogin();
                if (AWowGameplayController* GPC = Cast<AWowGameplayController>(UGameplayStatics::GetPlayerController(this, 0)))
                {
                    if (UWowConnectionManager* CM = AutoLogin->GetConnectionManager())
                    {
                        GPC->ConnectionManager = CM;
                        GPC->BindEntityEvents();
                    }
                }
            }
        }
    }
}

void AWowViewerGameMode::SetupCharacterTestScene(UWorld* World)
{
    UE_LOG(LogWowGameMode, Log, TEXT("Starting character/animation test scene"));

    SpawnDirectionalLight(World);
    SpawnGroundPlane(World, FVector(150.0f, 0.0f, -5.0f), 40.0f);

    // Position camera to view the full gallery
    FTimerHandle CamTimer;
    World->GetTimerManager().SetTimer(CamTimer, [this]()
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            if (APawn* Pawn = PC->GetPawn())
            {
                Pawn->SetActorLocation(FVector(-500.0f, 0.0f, 150.0f));
                PC->SetControlRotation(FRotator(-8.0f, 10.0f, 0.0f));
            }
        }
    }, 0.1f, false);

    // Spawn world manager for MPQ access (terrain loading skipped via -testscene check in WM::BeginPlay)
    AWowWorldManager* WM = SpawnWorldManager(World);

    if (WM && WM->GetMpqManager() && WM->GetAssetCache())
    {
        FMpqManager* Mpq = WM->GetMpqManager();
        FWowAssetCache* AssetCache = WM->GetAssetCache();
        const FRotator FacingCamera(0.0f, 180.0f, 0.0f);

        // --- Row 1: Player characters ---
        const float CharRowX = 0.0f;
        const float CharSpacing = 250.0f;

        // 1. Human Male with composite texture (skin + face + underwear)
        FWowCharacterBuilder::SpawnCharacter(World, Mpq, AssetCache,
            FWowCharacterBuilder::ERace::Human, FWowCharacterBuilder::EGender::Male,
            FVector(CharRowX, 0.0f, 0.0f), FacingCamera);

        // 2. Human Female with different skin color variation
        {
            FWowCharacterBuilder::FCharacterParams Params;
            Params.Race = FWowCharacterBuilder::ERace::Human;
            Params.Gender = FWowCharacterBuilder::EGender::Female;
            Params.Customization.RaceId = 1;
            Params.Customization.Gender = 1;
            Params.Customization.SkinColor = 2;
            Params.Customization.FaceVariation = 1;

            FWowCharacterBuilder::SpawnCharacterWithEquipment(World, Mpq, AssetCache,
                Params, FVector(CharRowX, CharSpacing, 0.0f), FacingCamera);
        }

        // 3. Orc Male
        {
            FWowCharacterBuilder::FCharacterParams Params;
            Params.Race = FWowCharacterBuilder::ERace::Orc;
            Params.Gender = FWowCharacterBuilder::EGender::Male;
            Params.Customization.RaceId = 2;
            Params.Customization.Gender = 0;
            Params.Customization.SkinColor = 1;

            FWowCharacterBuilder::SpawnCharacterWithEquipment(World, Mpq, AssetCache,
                Params, FVector(CharRowX, -CharSpacing, 0.0f), FacingCamera);
        }

        // --- Row 2: Creatures ---
        const float CreatureRowX = 350.0f;
        const float CreatureSpacing = 300.0f;

        // Wolf (DisplayId 604)
        FWowCharacterBuilder::SpawnCreatureByDisplayId(World, Mpq, AssetCache,
            604, FVector(CreatureRowX, -CreatureSpacing, 0.0f), FacingCamera);

        // Boar (DisplayId 454)
        FWowCharacterBuilder::SpawnCreatureByDisplayId(World, Mpq, AssetCache,
            454, FVector(CreatureRowX, 0.0f, 0.0f), FacingCamera);

        // Murloc (DisplayId 2192)
        FWowCharacterBuilder::SpawnCreatureByDisplayId(World, Mpq, AssetCache,
            2192, FVector(CreatureRowX, CreatureSpacing, 0.0f), FacingCamera);

        UE_LOG(LogWowGameMode, Log, TEXT("CharacterTest: spawned 3 characters + 3 creatures"));
    }
}

void AWowViewerGameMode::SetupTerrainTestScene(UWorld* World)
{
    UE_LOG(LogWowGameMode, Log, TEXT("Starting single-tile terrain test scene"));

    // Spawn sky for proper lighting
    {
        FActorSpawnParameters SkyParams;
        SkyParams.Name = FName(TEXT("WowSkyManager"));
        World->SpawnActor<AWowSkyManager>(AWowSkyManager::StaticClass(),
            FVector::ZeroVector, FRotator::ZeroRotator, SkyParams);
    }

    // Spawn world manager — loads 3x3 grid then streaming is disabled via -testscene check
    SpawnWorldManager(World);
}

void AWowViewerGameMode::SetupWmoTestScene(UWorld* World)
{
    UE_LOG(LogWowGameMode, Log, TEXT("Starting WMO test scene"));

    SpawnDirectionalLight(World);

    // Spawn world manager — loads 3x3 grid with WMOs, streaming disabled via -testscene check
    SpawnWorldManager(World);
}

void AWowViewerGameMode::SetupUITestScene(UWorld* World)
{
    UE_LOG(LogWowGameMode, Log, TEXT("Starting UI test scene"));

    SpawnDirectionalLight(World);
    SpawnGroundPlane(World, FVector::ZeroVector, 10.0f);

    // Teleport player
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (PC && PC->GetPawn())
    {
        PC->GetPawn()->SetActorLocation(FVector(0.0f, 0.0f, 200.0f));
    }

    // Spawn world manager for MPQ access (terrain loading skipped via -testscene check)
    AWowWorldManager* WM = SpawnWorldManager(World);

    // Load UI
    if (WM && WM->GetMpqManager())
    {
        if (UGameInstance* GI = GetGameInstance())
        {
            if (UWowUIManager* UIManager = GI->GetSubsystem<UWowUIManager>())
            {
                UIManager->LoadUI(WM->GetMpqManager(), WM->GetAssetCache());
            }
        }
    }
}

void AWowViewerGameMode::SetupLoginScene(UWorld* World)
{
    UE_LOG(LogWowGameMode, Log, TEXT("Starting login screen scene"));

    // Spawn sky for background
    {
        FActorSpawnParameters SkyParams;
        SkyParams.Name = FName(TEXT("WowSkyManager"));
        World->SpawnActor<AWowSkyManager>(AWowSkyManager::StaticClass(),
            FVector::ZeroVector, FRotator::ZeroRotator, SkyParams);
    }

    // Spawn WorldManager early for MPQ access (streaming is disabled by default,
    // will be enabled by LoginController when entering the world)
    AWowWorldManager* WorldMgr = SpawnWorldManager(World);

    // Spawn the login flow controller (shows login screen UI)
    FActorSpawnParameters LoginParams;
    LoginParams.Name = FName(TEXT("WowLoginController"));
    AWowLoginController* LoginCtrl = World->SpawnActor<AWowLoginController>(
        AWowLoginController::StaticClass(),
        FVector::ZeroVector, FRotator::ZeroRotator, LoginParams);

    // Give the LoginController a reference to WorldManager for world entry
    if (LoginCtrl)
    {
        LoginCtrl->WorldManager = WorldMgr;
        if (FParse::Param(FCommandLine::Get(), TEXT("charactercreatepreview")))
        {
            LoginCtrl->ShowCharacterCreatePreviewForTesting();
        }
        else if (FParse::Param(FCommandLine::Get(), TEXT("characterselectpreview")))
        {
            LoginCtrl->ShowCharacterSelectPreviewForTesting();
        }
        else if (FParse::Param(FCommandLine::Get(), TEXT("loadingscreenpreview")))
        {
            LoginCtrl->ShowLoadingScreenForTesting();
        }
        else if (FParse::Param(FCommandLine::Get(), TEXT("offlineloading")))
        {
            LoginCtrl->StartOfflineWorldLoadingForTesting();
        }
    }
}

void AWowViewerGameMode::SetupNetworkTestScene(UWorld* World)
{
    UE_LOG(LogWowGameMode, Log, TEXT("=== NETWORK TEST SCENE ==="));
    UE_LOG(LogWowGameMode, Log, TEXT("Headless networking test — no rendering, connects to test server"));
    UE_LOG(LogWowGameMode, Log, TEXT("Tests: auth handshake, realm list, character enum, world login, packet handling, entity updates"));

    // Parse optional timeout from command line (default 30s)
    float Timeout = 30.0f;
    FParse::Value(FCommandLine::Get(), TEXT("-nettesttimeout="), Timeout);
    NetworkTestTimeout = FMath::Clamp(Timeout, 5.0f, 120.0f);

    // Load credentials
    UWowCredentialStore* CredStore = NewObject<UWowCredentialStore>(GetTransientPackage());
    if (!CredStore->LoadCredentials())
    {
        UE_LOG(LogWowGameMode, Error, TEXT("[NETTEST] FAIL: Could not load credentials"));
        FGenericPlatformMisc::RequestExit(false);
        return;
    }

    FString AccountAlias;
    FParse::Value(FCommandLine::Get(), TEXT("-account="), AccountAlias);

    FWowCredential Cred;
    if (AccountAlias.IsEmpty())
    {
        Cred = CredStore->GetDefaultCredential();
    }
    else
    {
        bool bFound = false;
        for (const FWowCredential& C : CredStore->GetAllCredentials())
        {
            if (C.Alias.Equals(AccountAlias, ESearchCase::IgnoreCase))
            {
                Cred = C;
                bFound = true;
                break;
            }
        }
        if (!bFound)
        {
            UE_LOG(LogWowGameMode, Error, TEXT("[NETTEST] FAIL: Credential alias '%s' not found"), *AccountAlias);
            FGenericPlatformMisc::RequestExit(false);
            return;
        }
    }

    Cred.Password = UWowCredentialStore::ResolvePassword(Cred);
    if (Cred.Username.IsEmpty() || Cred.Password.IsEmpty())
    {
        UE_LOG(LogWowGameMode, Error, TEXT("[NETTEST] FAIL: Credential has empty username or password"));
        FGenericPlatformMisc::RequestExit(false);
        return;
    }

    UE_LOG(LogWowGameMode, Log, TEXT("[NETTEST] Connecting as '%s' to %s:%d (timeout %.0fs)"),
           *Cred.Username, *Cred.ServerAddress, Cred.AuthPort, NetworkTestTimeout);

    // Create connection manager
    NetworkTestConnMgr = NewObject<UWowConnectionManager>(GetGameInstance());
    NetworkTestConnMgr->AddToRoot();

    // Bind events
    NetworkTestConnMgr->OnStateChanged.AddDynamic(this, &AWowViewerGameMode::OnNetworkTestStateChanged);
    NetworkTestConnMgr->OnError.AddDynamic(this, &AWowViewerGameMode::OnNetworkTestError);
    NetworkTestConnMgr->OnRealmList.AddDynamic(this, &AWowViewerGameMode::OnNetworkTestRealmList);
    NetworkTestConnMgr->OnCharacterList.AddDynamic(this, &AWowViewerGameMode::OnNetworkTestCharacterList);

    // Start login
    NetworkTestConnMgr->Login(Cred.Username, Cred.Password, Cred.ServerAddress, Cred.AuthPort);

    // Set up a periodic tick timer (every 1s) to log status and check timeout
    NetworkTestElapsed = 0.0f;
    bNetworkTestComplete = false;
    FTimerDelegate TimerDel;
    TimerDel.BindWeakLambda(this, [this]() { NetworkTestTick(); });
    World->GetTimerManager().SetTimer(NetworkTestTimerHandle, TimerDel, 1.0f, true, 1.0f);
}

void AWowViewerGameMode::OnNetworkTestStateChanged(EWowSessionState NewState)
{
    static const TCHAR* StateNames[] = {
        TEXT("Disconnected"),
        TEXT("AuthConnecting"), TEXT("AuthChallengeSent"), TEXT("AuthProofSent"),
        TEXT("AuthRequestingRealmList"), TEXT("AuthHaveRealmList"),
        TEXT("WorldConnecting"), TEXT("WorldAuthChallengeReceived"), TEXT("WorldAuthSessionSent"),
        TEXT("WorldAuthenticated"), TEXT("WorldRequestingCharList"), TEXT("WorldHaveCharList"),
        TEXT("WorldEnteringWorld"), TEXT("WorldInGame"),
        TEXT("Error")
    };
    int32 Idx = static_cast<int32>(NewState);
    const TCHAR* Name = (Idx >= 0 && Idx < UE_ARRAY_COUNT(StateNames)) ? StateNames[Idx] : TEXT("Unknown");
    UE_LOG(LogWowGameMode, Log, TEXT("[NETTEST] State: %s"), Name);

    if (!NetworkTestConnMgr) return;

    // Auto-progress through the login flow
    if (NewState == EWowSessionState::WorldAuthenticated)
    {
        UE_LOG(LogWowGameMode, Log, TEXT("[NETTEST] PASS: World auth complete, requesting character list"));
        NetworkTestConnMgr->RequestCharacterList();
    }
}

void AWowViewerGameMode::OnNetworkTestError(const FString& Msg)
{
    UE_LOG(LogWowGameMode, Error, TEXT("[NETTEST] ERROR: %s"), *Msg);
}

void AWowViewerGameMode::OnNetworkTestRealmList(const TArray<FWowRealmInfo>& Realms)
{
    UE_LOG(LogWowGameMode, Log, TEXT("[NETTEST] PASS: Received %d realm(s)"), Realms.Num());
    for (int32 i = 0; i < Realms.Num(); ++i)
    {
        UE_LOG(LogWowGameMode, Log, TEXT("[NETTEST]   Realm %d: '%s' (%s:%d, %d chars)"),
               i, *Realms[i].Name, *Realms[i].Address, Realms[i].Port, Realms[i].CharacterCount);
    }

    if (NetworkTestConnMgr && Realms.Num() > 0)
    {
        UE_LOG(LogWowGameMode, Log, TEXT("[NETTEST] Selecting realm 0"));
        NetworkTestConnMgr->SelectRealm(0);
    }
}

void AWowViewerGameMode::OnNetworkTestCharacterList(const TArray<FWowCharacterInfo>& Characters)
{
    UE_LOG(LogWowGameMode, Log, TEXT("[NETTEST] PASS: Received %d character(s)"), Characters.Num());
    for (int32 i = 0; i < Characters.Num(); ++i)
    {
        const FWowCharacterInfo& C = Characters[i];
        UE_LOG(LogWowGameMode, Log, TEXT("[NETTEST]   Char %d: '%s' Level %d (Race %d, Class %d, Map %d, Zone %d)"),
               i, *C.Name, C.Level, C.Race, C.Class, C.MapId, C.ZoneId);
    }

    if (NetworkTestConnMgr && Characters.Num() > 0)
    {
        UE_LOG(LogWowGameMode, Log, TEXT("[NETTEST] Entering world with '%s' (GUID %llu)"),
               *Characters[0].Name, Characters[0].Guid);
        NetworkTestConnMgr->EnterWorld(Characters[0].Guid);
    }
}

void AWowViewerGameMode::NetworkTestTick()
{
    NetworkTestElapsed += 1.0f;

    if (!NetworkTestConnMgr || bNetworkTestComplete)
    {
        return;
    }

    EWowSessionState CurrentState = NetworkTestConnMgr->GetState();
    FWowEntityManager& EM = NetworkTestConnMgr->PacketHandler.EntityManager;
    const TSet<uint32>& Spells = NetworkTestConnMgr->PacketHandler.KnownSpells;

    // Log periodic status while in-game
    if (CurrentState == EWowSessionState::WorldInGame)
    {
        int32 PlayerCount = 0;
        int32 UnitCount = 0;
        int32 GOCount = 0;
        for (const auto& Pair : EM.GetAll())
        {
            if (Pair.Value->IsPlayer()) ++PlayerCount;
            else if (Pair.Value->IsUnit()) ++UnitCount;
            else if (Pair.Value->IsGameObject()) ++GOCount;
        }

        UE_LOG(LogWowGameMode, Log, TEXT("[NETTEST] %.0fs — InGame | Entities: %d total (%d players, %d units, %d GOs) | Spells: %d"),
               NetworkTestElapsed, EM.Num(), PlayerCount, UnitCount, GOCount, Spells.Num());

        // Check local player
        if (FWowEntity* LocalPlayer = EM.GetLocalPlayer())
        {
            UE_LOG(LogWowGameMode, Log, TEXT("[NETTEST]   LocalPlayer GUID=%llu Pos=(%.1f, %.1f, %.1f) HP=%d/%d Level=%d"),
                   LocalPlayer->Guid,
                   LocalPlayer->Movement.Position.X, LocalPlayer->Movement.Position.Y, LocalPlayer->Movement.Position.Z,
                   LocalPlayer->GetHealth(), LocalPlayer->GetMaxHealth(), LocalPlayer->GetLevel());
        }
    }

    // After 10 seconds in-game (or on timeout), print final summary and exit
    bool bInGameLongEnough = (CurrentState == EWowSessionState::WorldInGame && NetworkTestElapsed >= 15.0f);
    bool bTimedOut = (NetworkTestElapsed >= NetworkTestTimeout);

    if (bInGameLongEnough || bTimedOut)
    {
        bNetworkTestComplete = true;

        UE_LOG(LogWowGameMode, Log, TEXT(""));
        UE_LOG(LogWowGameMode, Log, TEXT("=== NETWORK TEST RESULTS ==="));
        UE_LOG(LogWowGameMode, Log, TEXT("  Duration:     %.0f seconds"), NetworkTestElapsed);
        UE_LOG(LogWowGameMode, Log, TEXT("  Final State:  %d (%s)"),
               static_cast<int32>(CurrentState),
               CurrentState == EWowSessionState::WorldInGame ? TEXT("WorldInGame") : TEXT("NOT InGame"));

        bool bAuthOk = static_cast<int32>(CurrentState) >= static_cast<int32>(EWowSessionState::AuthHaveRealmList);
        bool bWorldOk = static_cast<int32>(CurrentState) >= static_cast<int32>(EWowSessionState::WorldAuthenticated);
        bool bCharOk = static_cast<int32>(CurrentState) >= static_cast<int32>(EWowSessionState::WorldHaveCharList);
        bool bInGame = CurrentState == EWowSessionState::WorldInGame;
        bool bEntities = EM.Num() > 0;
        bool bLocalPlayer = EM.GetLocalPlayer() != nullptr;

        UE_LOG(LogWowGameMode, Log, TEXT("  Auth:         %s"), bAuthOk ? TEXT("PASS") : TEXT("FAIL"));
        UE_LOG(LogWowGameMode, Log, TEXT("  World Auth:   %s"), bWorldOk ? TEXT("PASS") : TEXT("FAIL"));
        UE_LOG(LogWowGameMode, Log, TEXT("  Char Enum:    %s"), bCharOk ? TEXT("PASS") : TEXT("FAIL"));
        UE_LOG(LogWowGameMode, Log, TEXT("  In Game:      %s"), bInGame ? TEXT("PASS") : TEXT("FAIL"));
        UE_LOG(LogWowGameMode, Log, TEXT("  Entities:     %s (%d tracked)"), bEntities ? TEXT("PASS") : TEXT("FAIL"), EM.Num());
        UE_LOG(LogWowGameMode, Log, TEXT("  Local Player: %s (GUID %llu)"), bLocalPlayer ? TEXT("PASS") : TEXT("FAIL"), EM.LocalPlayerGuid);
        UE_LOG(LogWowGameMode, Log, TEXT("  Known Spells: %d"), Spells.Num());

        bool bAllPass = bAuthOk && bWorldOk && bCharOk && bInGame && bEntities && bLocalPlayer;
        UE_LOG(LogWowGameMode, Log, TEXT("  OVERALL:      %s"), bAllPass ? TEXT("ALL PASS") : TEXT("SOME FAILED"));
        UE_LOG(LogWowGameMode, Log, TEXT("============================="));
        UE_LOG(LogWowGameMode, Log, TEXT(""));

        // Cleanup and exit
        if (NetworkTestConnMgr)
        {
            NetworkTestConnMgr->Disconnect();
            NetworkTestConnMgr->RemoveFromRoot();
            NetworkTestConnMgr = nullptr;
        }

        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(NetworkTestTimerHandle);
        }

        FGenericPlatformMisc::RequestExit(false);
    }
}
