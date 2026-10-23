#include "WowLoginController.h"
#include "WowUiSurfaces.h"
#include "WowLoginPlacement.h"
#include "WowLoginTheme.h"
#include "Components/PrimitiveComponent.h"
#include "WowStartupProgress.h"
#include "WowConnectionManager.h"
#include "WowLoginWidget.h"
#include "WowRealmSelectWidget.h"
#include "WowCharacterSelectWidget.h"
#include "WowWindowStyle.h"
#include "WowNativeWidgetStyle.h"
#include "WowCharacterCreateWidget.h"
#include "WowCinematicManager.h"
#include "WowTrailerPlayer.h"
#include "Net/WowWorldSocket.h"
#include "WowCharacterPreview.h"
#include "WowCredentialStore.h"
#include "WowOfflineCharacterStore.h"
#include "WowGameplayController.h"
#include "WowClientPreferences.h"
#include "WowPlayerCharacter.h"
#include "UI/SWowLoginOptions.h"
// removed: now using SWowLoadingScreen.h
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WowSkyManager.h"
#include "WowWeatherComponent.h"
#include "Components/CapsuleComponent.h"
#include "WowWorldManager.h"
#include "WowAudioManager.h"
#include "WowUIManager.h"
#include "WowAddonSettings.h"
#include "SWowAddonList.h"
#include "WowFrameManager.h"
#include "SWowLoadingScreen.h"
#include "SWowCombatLog.h"
#include "SWowChatWindow.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameViewportClient.h"
#include "Camera/CameraActor.h"
#include "Components/CanvasPanel.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"
#include "CoreGlobals.h"
// SOverlay.h already included above
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/CharStartOutfitDbc.h"
#include "WowEntity.h"
#include "WowEntityManager.h"
#include "WowAssetCache.h"
#include "Mpq/MpqManager.h"
#include "WowTextureFactory.h"
#include "WowCharacterBuilder.h"
#include "Formats/BlpParser.h"
#include "Coord/WowCoordinate.h"
#include "WowCookedMaterial.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/SecureHash.h"
#include "TimerManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "UnrealClient.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowLogin, Log, All);

namespace
{
bool IsOfflineGlueFlowRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("offlineflow"));
}

void WriteSplashReadyMarker()
{
	FString MarkerPath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-splashreadyfile="), MarkerPath) || MarkerPath.IsEmpty())
	{
		return;
	}

	if (FPaths::IsRelative(MarkerPath))
	{
		MarkerPath = FPaths::Combine(FPaths::ProjectDir(), MarkerPath);
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*FPaths::GetPath(MarkerPath));
	FFileHelper::SaveStringToFile(
		FString::Printf(TEXT("Login screen ready at %s"), *FDateTime::UtcNow().ToIso8601()),
		*MarkerPath);
}

struct FWowM2StaticCamera
{
    FVector Position = FVector::ZeroVector;
    FVector Target = FVector::ZeroVector;
    float FieldOfViewRadians = 0.85f;
    bool bPortraitCamera = false; // r280: camera type 0, the one the glue screen asks for
    bool bIsValid = false;
};

struct FWowM2HeaderForCamera
{
    uint32 Magic;
    uint32 Version;
    uint32 NameLength;
    uint32 NameOffset;
    uint32 GlobalFlags;
    uint32 nGlobalSequences;
    uint32 ofsGlobalSequences;
    uint32 nAnimations;
    uint32 ofsAnimations;
    uint32 nAnimationLookup;
    uint32 ofsAnimationLookup;
    uint32 nBones;
    uint32 ofsBones;
    uint32 nKeyBoneLookup;
    uint32 ofsKeyBoneLookup;
    uint32 nVertices;
    uint32 ofsVertices;
    uint32 nViews;
    uint32 nColors;
    uint32 ofsColors;
    uint32 nTextures;
    uint32 ofsTextures;
    uint32 nTransparency;
    uint32 ofsTransparency;
    uint32 nTexAnims;
    uint32 ofsTexAnims;
    uint32 nTexReplace;
    uint32 ofsTexReplace;
    uint32 nRenderFlags;
    uint32 ofsRenderFlags;
    uint32 nBoneLookup;
    uint32 ofsBoneLookup;
    uint32 nTexLookup;
    uint32 ofsTexLookup;
    uint32 nTexUnits;
    uint32 ofsTexUnits;
    uint32 nTransLookup;
    uint32 ofsTransLookup;
    uint32 nTexAnimLookup;
    uint32 ofsTexAnimLookup;
    float BoundingBox[6];
    float BoundingSphereRadius;
    float CollisionBox[6];
    float CollisionSphereRadius;
    uint32 nBoundingTriangles;
    uint32 ofsBoundingTriangles;
    uint32 nBoundingVertices;
    uint32 ofsBoundingVertices;
    uint32 nBoundingNormals;
    uint32 ofsBoundingNormals;
    uint32 nAttachments;
    uint32 ofsAttachments;
    uint32 nAttachmentLookup;
    uint32 ofsAttachmentLookup;
    uint32 nEvents;
    uint32 ofsEvents;
    uint32 nLights;
    uint32 ofsLights;
    uint32 nCameras;
    uint32 ofsCameras;
    uint32 nCameraLookup;
    uint32 ofsCameraLookup;
    uint32 nRibbonEmitters;
    uint32 ofsRibbonEmitters;
    uint32 nParticleEmitters;
    uint32 ofsParticleEmitters;
};

struct FWowM2CameraRaw
{
    uint32 Type;
    float FieldOfViewRadians;
    float FarClip;
    float NearClip;
    uint8 PositionBlock[20];
    float Position[3];
    uint8 TargetBlock[20];
    float Target[3];
    uint8 RollBlock[20];
};

static FVector WowM2PointToUE(const float In[3])
{
    return FVector(In[1], In[0], In[2]) * FWowCoordinate::SCALE;
}

static FVector TransformM2CameraPointForSpawnedMesh(const AActor* ModelActor, const FVector& ConvertedM2Point)
{
    if (!ModelActor)
    {
        return ConvertedM2Point;
    }

    // FWowCharacterBuilder converts vertices to UE coordinates, then rotates the
    // spawned skeletal mesh -90 degrees around yaw. Camera points need the same
    // visual-space transform or they end up inside/behind the glue model.
    const FVector MeshLocalPoint = FRotator(0.0f, -90.0f, 0.0f).RotateVector(ConvertedM2Point);
    return ModelActor->GetTransform().TransformPosition(MeshLocalPoint);
}

// r383 (Stefan's native 3.3.5a screenshot at 1920x1080): our login scene showed 1.33x more than
// the original client around the same centre (feature-matched), which put the flat edges of the
// side mountain cards on screen. The stored glue camera value matches the original when it is read
// as twice the horizontal angle of a 4:3 screen (Northrend: 120 -> 60 deg). The vertical angle is
// kept and the width follows the screen, so wider screens see more at the sides.
// r280 (Stefan 13.10, Diablo 3 login patch, native screenshot at 1920x1080): a scene with its own
// type 0 camera (the D3 scene: 45 deg) is shown with a vertical angle of half the stored value
// (logo 620 px wide, centred 312 px above the middle = 22.5 deg vertical). The r383 rule above
// stays for scenes without one (stock Northrend: type -1), which made the D3 logo 1.33x too big.
static float GWowLoginGlueRawFov = 0.f;
static bool GWowLoginGluePortraitCamera = false;
static float GlueSceneHorizontalFovDegrees(float RawRadians, float Aspect, bool bPortraitCamera = false)
{
    const float TanHalfVertical = FMath::Tan(RawRadians * 0.25f) * (bPortraitCamera ? 1.f : 0.75f);
    return FMath::RadiansToDegrees(2.f * FMath::Atan(TanHalfVertical * FMath::Max(Aspect, 0.1f)));
}

static FWowM2StaticCamera TryReadFirstM2StaticCamera(FMpqManager* Mpq, const FString& ModelPath)
{
    FWowM2StaticCamera Result;
    if (!Mpq)
    {
        return Result;
    }

    TArray<uint8> M2Raw;
    if (!Mpq->ReadFile(ModelPath, M2Raw) || M2Raw.Num() < static_cast<int32>(sizeof(FWowM2HeaderForCamera)))
    {
        return Result;
    }

    const FWowM2HeaderForCamera& Header = *reinterpret_cast<const FWowM2HeaderForCamera*>(M2Raw.GetData());
    if (Header.Magic != 0x3032444D || Header.nCameras == 0)
    {
        return Result;
    }

    const uint64 CameraEnd = static_cast<uint64>(Header.ofsCameras)
        + static_cast<uint64>(Header.nCameras) * sizeof(FWowM2CameraRaw);
    if (CameraEnd > static_cast<uint64>(M2Raw.Num()))
    {
        return Result;
    }

    const FWowM2CameraRaw* Cameras = reinterpret_cast<const FWowM2CameraRaw*>(M2Raw.GetData() + Header.ofsCameras);
    const FWowM2CameraRaw& Camera = Cameras[0];
    // r608 (Stefan: the HD pack's login screen - a backported Shadowlands/Icecrown scene in place of
    // UI_MainMenu_Northrend.m2 - showed a dome seen from the wrong place): the camera's position and
    // target are the stored base PLUS the value of their animation track (M2Camera: position_base +
    // positions(t)). The original 3.3.5 glue models keep the whole camera in the base and zeros in
    // the tracks, so the base alone worked; the backported model keeps its camera in the tracks
    // (bases on the ground, 55 and 2408 yards apart at the same height). The first key of the first
    // sequence is added to each base.
    auto FirstTrackValue = [&M2Raw](const uint8* Block, float Out[3])
    {
        Out[0] = Out[1] = Out[2] = 0.f;
        uint32 NumAnims = 0, OfsAnims = 0;
        FMemory::Memcpy(&NumAnims, Block + 12, 4);
        FMemory::Memcpy(&OfsAnims, Block + 16, 4);
        if (NumAnims == 0 || uint64(OfsAnims) + 8 > uint64(M2Raw.Num())) return false;
        uint32 NumKeys = 0, OfsKeys = 0;
        FMemory::Memcpy(&NumKeys, M2Raw.GetData() + OfsAnims, 4);
        FMemory::Memcpy(&OfsKeys, M2Raw.GetData() + OfsAnims + 4, 4);
        // Spline keys: value, in-tangent, out-tangent (3 x C3Vector).
        if (NumKeys == 0 || uint64(OfsKeys) + 12 > uint64(M2Raw.Num())) return false;
        FMemory::Memcpy(Out, M2Raw.GetData() + OfsKeys, 12);
        return FMath::IsFinite(Out[0]) && FMath::IsFinite(Out[1]) && FMath::IsFinite(Out[2]);
    };
    float Position[3] = { Camera.Position[0], Camera.Position[1], Camera.Position[2] };
    float Target[3] = { Camera.Target[0], Camera.Target[1], Camera.Target[2] };
    float Offset[3];
    if (FirstTrackValue(Camera.PositionBlock, Offset)) for (int32 I = 0; I < 3; ++I) Position[I] += Offset[I];
    if (FirstTrackValue(Camera.TargetBlock, Offset)) for (int32 I = 0; I < 3; ++I) Target[I] += Offset[I];
    UE_LOG(LogTemp, Log, TEXT("Login glue camera %s: base (%.2f %.2f %.2f) -> (%.2f %.2f %.2f), target base (%.2f %.2f %.2f) -> (%.2f %.2f %.2f), fov %.3f"),
        *ModelPath, Camera.Position[0], Camera.Position[1], Camera.Position[2], Position[0], Position[1], Position[2],
        Camera.Target[0], Camera.Target[1], Camera.Target[2], Target[0], Target[1], Target[2], Camera.FieldOfViewRadians);
    Result.Position = WowM2PointToUE(Position);
    Result.Target = WowM2PointToUE(Target);
    // r383: no 1.6 cap any more; the stored value is converted by GlueSceneHorizontalFovDegrees.
    Result.FieldOfViewRadians = FMath::Clamp(Camera.FieldOfViewRadians, 0.25f, 3.0f);
    // r614 (Stefan: the HD pack's login is "less zoomed in" in its own client): the stock glue models
    // (FOV 1.47-2.09) are framed for the stock exe, which reads the value as a 4:3 horizontal angle.
    // Widescreen-era backports (the HD pack's Icecrown scene: 0.97) are framed for widescreen-fixed exes
    // that keep it as the vertical angle; drawn the stock way they came out 1.33x too close (rendered
    // offline against Stefan's screenshot: a vertical angle of ~28 degrees = 0.97 / 2 fits).
    Result.bPortraitCamera = Camera.Type == 0 || Camera.FieldOfViewRadians < 1.2f;
    Result.bIsValid = true;
    return Result;
}

void ApplyWowLoginUiInputMode(APlayerController* PlayerController, const TSharedPtr<SWidget>& WidgetToFocus)
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
    if (UGameViewportClient* ViewportClient = PlayerController->GetWorld() ? PlayerController->GetWorld()->GetGameViewport() : nullptr)
    {
        ViewportClient->SetMouseLockMode(EMouseLockMode::DoNotLock);
        ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
        ViewportClient->SetHideCursorDuringCapture(false);
    }
    PlayerController->bShowMouseCursor = true;
    PlayerController->DefaultMouseCursor = EMouseCursor::Default;
    PlayerController->CurrentMouseCursor = EMouseCursor::Default;
}

}

AWowLoginController::AWowLoginController()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AWowLoginController::ShowLoadingScreenForTesting()
{
    ClearCurrentScreen();
    ShowLoadingScreen(0);
    if (LoadingScreenWidget.IsValid())
    {
        LoadingScreenWidget->SetProgress(0.63f, FString());
    }
}

void AWowLoginController::SuppressDebugMessagesForGlueScreen()
{
    if (!GEngine)
    {
        return;
    }

    if (!bSavedOnScreenDebugState)
    {
        bPreviousOnScreenDebugEnabled = GEngine->bEnableOnScreenDebugMessages;
        bPreviousGlobalScreenMessagesEnabled = GAreScreenMessagesEnabled;
        bSavedOnScreenDebugState = true;
    }

    GEngine->bEnableOnScreenDebugMessages = false;
    GAreScreenMessagesEnabled = false;

    if (AWowGameplayController* GPC = Cast<AWowGameplayController>(UGameplayStatics::GetPlayerController(this, 0)))
    {
        GPC->SetGameplayHudVisible(false);
    }
}

void AWowLoginController::StartOfflineWorldLoadingForTesting()
{
    ClearCurrentScreen();

    if (!WorldManager)
    {
        UE_LOG(LogWowLogin, Warning, TEXT("Offline loading test requested without a WorldManager"));
        return;
    }

    const uint32 OfflineMapId = bHasSelectedCharacterInfo
		? static_cast<uint32>(FMath::Max(SelectedCharacterInfo.MapId, 0))
		: 0u;
	if (FParse::Param(FCommandLine::Get(), TEXT("testenterworldsound")))
	{
		if (AWowAudioManager* AudioManager = GetOrCreateAudioManager())
		{
			AudioManager->PlayEnterWorldSound();
		}
	}
    ShowLoadingScreen(OfflineMapId);

    if (LoadingScreenWidget.IsValid())
    {
        LoadingScreenWidget->SetProgress(0.03f, TEXT("Preparing offline session..."));
    }

    // Let Slate present the loading screen before the synchronous FrameXML and
    // terrain setup begins. This avoids a blank window during the first boot.
    GetWorldTimerManager().ClearTimer(OfflineWorldInitTimerHandle);
    GetWorldTimerManager().SetTimer(OfflineWorldInitTimerHandle, this,
        &AWowLoginController::BeginOfflineWorldLoading, 0.15f, false);
}

void AWowLoginController::BeginOfflineWorldLoading()
{
    if (!WorldManager)
    {
        return;
    }

    PrepareOfflinePlayerState();
    const uint32 OfflineMap=bHasSelectedCharacterInfo?uint32(FMath::Max(0,SelectedCharacterInfo.MapId)):0u;
    if(!WorldManager->SelectMap(OfflineMap))
    {
        if(LoadingScreenWidget.IsValid()) LoadingScreenWidget->SetProgress(0.f,TEXT("Unable to load the selected map."));
        return;
    }
    WorldManager->EnableTerrainStreaming();
    InitializeWorldSystems();
    if(auto* Sky=Cast<AWowSkyManager>(UGameplayStatics::GetActorOfClass(this,AWowSkyManager::StaticClass()))) Sky->LoadLightZones(OfflineMap);

    float OfflineWowX = bHasSelectedCharacterInfo ? SelectedCharacterInfo.Position.X : -8949.0f;
    float OfflineWowY = bHasSelectedCharacterInfo ? SelectedCharacterInfo.Position.Y : -132.0f;
    float OfflineWowZ = bHasSelectedCharacterInfo ? SelectedCharacterInfo.Position.Z : 84.0f;
    float OfflineYaw = 0.0f;
    for (const auto& Record:OfflineCharacters) if (Record.Guid==SelectedCharacterGuid) {OfflineYaw=Record.Orientation;break;}
    const TCHAR* CmdLine = FCommandLine::Get();
    FParse::Value(CmdLine, TEXT("-offlinewowx="), OfflineWowX);
    FParse::Value(CmdLine, TEXT("-offlinewowy="), OfflineWowY);
    FParse::Value(CmdLine, TEXT("-offlinewowz="), OfflineWowZ);
    FParse::Value(CmdLine, TEXT("-offlineyaw="), OfflineYaw);

    PendingSpawnPosition = FWowCoordinate::WowToUE(OfflineWowX, OfflineWowY, OfflineWowZ);
    PendingSpawnOrientation = OfflineYaw;
    bHasPendingSpawn = true;
    InitialLoadCompleteSettleTime = 0.0f;
    WorldEntryWarmupElapsed = 0.0f;
    WorldEntryWarmupStartedAtSeconds = 0.0;
    WorldEntryWarmupStartPass = 0;
    LastWorldEntryObjectRevision = 0;
    LastWorldEntryEntityRevision = 0;
    bEntryPresentationPrepared = false;
    bInitialLoadSettleLogged = false;
    bWaitingForInitialLoad = true;

    WorldManager->BeginWorldEntryWarmup(PendingSpawnPosition);
    WorldManager->LoadTilesAroundPosition(PendingSpawnPosition, 2);

    if (LoadingScreenWidget.IsValid())
    {
        LoadingScreenWidget->SetProgress(0.1f, FString());
    }

    if (AWowGameplayController* GPC = Cast<AWowGameplayController>(UGameplayStatics::GetPlayerController(this, 0)))
    {
        GPC->bDeferSpawnTeleport = true;
        GPC->bHasDeferredSpawn = true;
        GPC->DeferredSpawnPos = PendingSpawnPosition;
        GPC->DeferredSpawnOrientation = PendingSpawnOrientation;

        if (APawn* Pawn = GPC->GetPawn())
        {
            Pawn->SetActorHiddenInGame(true);
            SuspendPawnUntilGroundReady(Pawn);
        }
    }

    UE_LOG(LogWowLogin, Log,
        TEXT("Started offline world through loading screen at WoW position (%.1f, %.1f, %.1f), yaw %.1f"),
        OfflineWowX,
        OfflineWowY,
        OfflineWowZ,
        OfflineYaw);
}

void AWowLoginController::BeginPlay()
{
    Super::BeginPlay();
    StartLoginFlow();
}

void AWowLoginController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(OfflineWorldInitTimerHandle);
    ClearCurrentScreen();
    if (CinematicManager)
    {
        CinematicManager->StopCinematic();
        delete CinematicManager;
        CinematicManager = nullptr;
    }
    Super::EndPlay(EndPlayReason);
}

void AWowLoginController::StartLoginFlow()
{
	bOfflineFlow = IsOfflineGlueFlowRequested();
    ConnectionManager = NewObject<UWowConnectionManager>(this);
    ConnectionManager->OnStateChanged.AddDynamic(this, &AWowLoginController::OnStateChanged);
    // Realm selection runs directly from AuthHaveRealmList using the native cache.
    ConnectionManager->OnCharacterList.AddDynamic(this, &AWowLoginController::OnCharacterList);
    ConnectionManager->OnError.AddDynamic(this, &AWowLoginController::OnError);
    ConnectionManager->OnWorldDisconnected.AddDynamic(this, &AWowLoginController::OnWorldDisconnected);
    ConnectionManager->OnCharCreateResult.AddDynamic(this, &AWowLoginController::OnCharCreateResult);
    ConnectionManager->OnLogoutComplete.AddDynamic(this, &AWowLoginController::OnLogoutComplete);

    // The world-test launcher is intentionally a direct offline-world entry
    // point. Do not briefly show the login screen or attempt any network flow
    // before the loading screen is presented.
    if (FParse::Param(FCommandLine::Get(), TEXT("offlineloading")))
    {
        StartOfflineWorldLoadingForTesting();
        return;
    }

    ShowLoginScreen();

    // Show mouse cursor
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
    {
        PC->bShowMouseCursor = true;
        ApplyWowLoginUiInputMode(PC, GEngine && GEngine->GameViewport
            ? StaticCastSharedPtr<SWidget>(GEngine->GameViewport->GetGameViewportWidget())
            : nullptr);
    }
}

AWowAudioManager* AWowLoginController::GetOrCreateAudioManager()
{
	TArray<AActor*> ExistingAudio;
	UGameplayStatics::GetAllActorsOfClass(this, AWowAudioManager::StaticClass(), ExistingAudio);
	AWowAudioManager* AudioManager = ExistingAudio.Num() > 0
		? Cast<AWowAudioManager>(ExistingAudio[0])
		: nullptr;

	if (!AudioManager && GetWorld())
	{
		AudioManager = GetWorld()->SpawnActor<AWowAudioManager>(AWowAudioManager::StaticClass());
	}
	if (AudioManager && WorldManager && WorldManager->GetMpqManager())
	{
		AudioManager->SetMpqManager(WorldManager->GetMpqManager());
	}
	if (AudioManager)
	{
		AudioManager->SetMasterVolume(FWowClientPreferences::GetMasterVolume());
		AudioManager->SetMusicVolume(FWowClientPreferences::GetMusicVolume());
		AudioManager->SetSfxVolume(FWowClientPreferences::GetSfxVolume());
		AudioManager->SetAmbienceVolume(FWowClientPreferences::GetAmbienceVolume());
	}
	return AudioManager;
}

void AWowLoginController::PlayGlueUiSound(const FName& SoundName)
{
	if (AWowAudioManager* AudioManager = GetOrCreateAudioManager())
	{
		AudioManager->PlayUiSound(SoundName);
	}
}

void AWowLoginController::ShowLoginOptions()
{
	if (!GEngine || !GEngine->GameViewport)
	{
		return;
	}

	PlayGlueUiSound(TEXT("igMainMenuOption"));
	// r225 (Stefan 11.10: the login Options looked flat, unlike the in-game one): the dialog frame,
	// red panel buttons, check boxes, sliders and scroll bars come from the stock art in the user's
	// data. In game the frame manager loads it; at the login screen nothing had yet, so load it here.
	if (!FWowWindowStyle::HasOriginalBorder() || !FWowWindowStyle::ControlTexture(TEXT("Interface\\Buttons\\UI-CheckBox-Up")))
	{
		FWowWindowStyle::LoadTextures([this](const FString& Path) {
			return LoadFirstMpqBlpTexture({Path+TEXT(".blp")}, TEXT("options window"));
		});
	}
	if (!LoginOptionsWidget.IsValid())
	{
		LoginOptionsWidget = SNew(SWowLoginOptions)
            .ArtworkOptions(LoginWidget.IsValid() ? LoginWidget->CreateArtworkOptions() : SNullWidget::NullWidget)
			.AudioManager(GetOrCreateAudioManager())
            .OnArtworkChanged(FSimpleDelegate::CreateLambda([this]
            {
                bool bUseMods = false;
                if (GConfig) GConfig->GetBool(TEXT("WowUnreal.ClientPreferences"), TEXT("LoginUseMods"), bUseMods, GGameUserSettingsIni);
                if (LoginWidget.IsValid()) LoginWidget->SetModdedArtwork(bUseMods);
            }))
			.OnClose(FSimpleDelegate::CreateUObject(this, &AWowLoginController::HideLoginOptions));
		FWowUiSurfaces::Add(LoginOptionsWidget.ToSharedRef(), EWowUiZone::Glue, 5000);
	}
	LoginOptionsWidget->SetVisibility(EVisibility::Visible);

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = true;
		ApplyWowLoginUiInputMode(PC, LoginOptionsWidget);
	}
}

void AWowLoginController::HideLoginOptions()
{
	PlayGlueUiSound(TEXT("igMainMenuClose"));
	if (LoginOptionsWidget.IsValid())
	{
		LoginOptionsWidget->SetVisibility(EVisibility::Collapsed);
	}
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = true;
		ApplyWowLoginUiInputMode(PC, CurrentWidget);
	}
}

void AWowLoginController::HandleLoginExitRequested()
{
	PlayGlueUiSound(TEXT("igMainMenuQuit"));
	FPlatformMisc::RequestExit(false);
}

void AWowLoginController::RefreshOfflineCharacterInfos()
{
	OfflineCharacterInfos.Reset();
	for (const FWowOfflineCharacterRecord& Character : OfflineCharacters)
	{
		FWowCharacterInfo& CharacterInfo = OfflineCharacterInfos.Add_GetRef(Character.ToCharacterInfo());
		PopulateOfflineStarterEquipment(CharacterInfo);
	}

	if (ConnectionManager)
	{
		ConnectionManager->ConfigureOfflineSession(OfflineCharacterInfos, SelectedCharacterGuid);
	}
}

void AWowLoginController::LoadOfflineRoster()
{
	if (!FWowOfflineCharacterStore::Load(OfflineCharacters))
	{
		OfflineCharacters.Reset();
		SetStatusText(TEXT("Could not read the local character roster."));
	}
	RefreshOfflineCharacterInfos();
}

void AWowLoginController::PopulateOfflineStarterEquipment(FWowCharacterInfo& CharacterInfo) const
{
	CharacterInfo.Equipment.Reset();
	const FCharStartOutfitDbcEntry* Outfit = FDbcStore::Get().CharStartOutfit()
		.GetByRaceClassGender(CharacterInfo.Race, CharacterInfo.Class, CharacterInfo.Gender);
	if (!Outfit)
	{
		return;
	}

	for (int32 ItemIndex = 0; ItemIndex < 24; ++ItemIndex)
	{
		if (Outfit->ItemDisplayIds[ItemIndex] <= 0 || Outfit->ItemInventorySlots[ItemIndex] <= 0)
		{
			continue;
		}

		FWowCharacterEquipment& Item = CharacterInfo.Equipment.AddDefaulted_GetRef();
		Item.DisplayId = Outfit->ItemDisplayIds[ItemIndex];
		Item.InventoryType = static_cast<uint8>(Outfit->ItemInventorySlots[ItemIndex]);
	}
}

void AWowLoginController::PrepareOfflinePlayerState()
{
	if (!ConnectionManager || !bHasSelectedCharacterInfo || SelectedCharacterGuid == 0)
	{
		return;
	}

	ConnectionManager->ConfigureOfflineSession(OfflineCharacterInfos, SelectedCharacterGuid);
	FWowEntityManager& EntityManager = ConnectionManager->PacketHandler.EntityManager;
	const uint64 Guid = static_cast<uint64>(SelectedCharacterGuid);
	EntityManager.LocalPlayerGuid = Guid;
	FWowEntity& Entity = EntityManager.PromoteToTyped(
		Guid, WowTypeMask::OBJECT | WowTypeMask::UNIT | WowTypeMask::PLAYER);
	Entity.Movement.Position = SelectedCharacterInfo.Position;
	Entity.SetField(ObjectField::GUID, static_cast<uint32>(Guid));
	Entity.SetField(ObjectField::GUID + 1, static_cast<uint32>(Guid >> 32));
	Entity.SetField(ObjectField::TYPE, WowTypeMask::OBJECT | WowTypeMask::UNIT | WowTypeMask::PLAYER);
	Entity.SetField(UnitField::BYTES_0,
		static_cast<uint32>(SelectedCharacterInfo.Race)
		| (static_cast<uint32>(SelectedCharacterInfo.Class) << 8)
		| (static_cast<uint32>(SelectedCharacterInfo.Gender) << 16));
	Entity.SetField(UnitField::HEALTH, 100);
	Entity.SetField(UnitField::MAXHEALTH, 100);
	Entity.SetField(UnitField::POWER1, 100);
	Entity.SetField(UnitField::MAXPOWER1, 100);
	Entity.SetField(UnitField::LEVEL, FMath::Max<uint8>(SelectedCharacterInfo.Level, 1));
	Entity.SetField(UnitField::STAT0, 23);
	Entity.SetField(UnitField::STAT1, 20);
	Entity.SetField(UnitField::STAT2, 22);
	Entity.SetField(UnitField::STAT3, 20);
	Entity.SetField(UnitField::STAT4, 21);
	Entity.SetField(UnitField::RESISTANCES, 50);
	Entity.SetField(UnitField::ATTACK_POWER, 30);
	Entity.SetField(PlayerField::XP, 0);
	Entity.SetField(PlayerField::NEXT_LEVEL_XP, 400);
	ConnectionManager->PacketHandler.PlayerNameCache.Add(Guid, SelectedCharacterInfo.Name);
}

void AWowLoginController::CloseStartupGameplayPanels() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	UWowUIManager* UIManager = GameInstance ? GameInstance->GetSubsystem<UWowUIManager>() : nullptr;
	FWowFrameManager* FrameManager = UIManager ? UIManager->GetFrameManager() : nullptr;
	if (!FrameManager)
	{
		return;
	}

	static const TCHAR* FramesToClose[] = {
		TEXT("CharacterFrame"), TEXT("FriendsFrame"), TEXT("QuestLogFrame"),
		TEXT("SpellBookFrame"), TEXT("TalentFrame"), TEXT("GuildFrame"),
		TEXT("WorldMapFrame"), TEXT("GossipFrame"), TEXT("QuestFrame"),
		TEXT("MerchantFrame"), TEXT("LootFrame"), TEXT("GameMenuFrame")
	};
	for (const TCHAR* FrameName : FramesToClose)
	{
		const int64 Handle = FrameManager->FindFrame(FrameName);
		if (Handle >= 0)
		{
			FrameManager->SetFrameVisible(Handle, false);
		}
	}
}

void AWowLoginController::ReturnToOfflineCharacterSelect()
{
	bOfflineFlow = true;
	SelectedCharacterGuid = 0;
	bHasSelectedCharacterInfo = false;
	LoadOfflineRoster();
	ShowCharacterSelectScreen(OfflineCharacterInfos);
}

void AWowLoginController::ShowCharacterCreatePreviewForTesting()
{
    ShowCharacterCreateScreen();

    // Allow any race/gender combination to be exercised without a server.
    // Example: -charactercreatepreview -previewrace=3 -previewgender=0
    int32 PreviewRace = 1;
    int32 PreviewGender = 0;
    FParse::Value(FCommandLine::Get(), TEXT("-previewrace="), PreviewRace);
    FParse::Value(FCommandLine::Get(), TEXT("-previewgender="), PreviewGender);
    PreviewGender = FMath::Clamp(PreviewGender, 0, 1);
    const bool bValidPlayableRace = PreviewRace >= 1 && PreviewRace <= 11 && PreviewRace != 9;
    if (!bValidPlayableRace)
    {
        UE_LOG(LogWowLogin, Warning, TEXT("Invalid preview race %d; falling back to Human (1)"), PreviewRace);
        PreviewRace = 1;
    }
    if (CharCreateWidget.IsValid())
    {
        CharCreateWidget->SetPreviewSelection(static_cast<uint8>(PreviewRace), static_cast<uint8>(PreviewGender));
    }
    if (CharacterPreview)
    {
        HandleCharacterHighlighted(static_cast<uint8>(PreviewRace), static_cast<uint8>(PreviewGender));
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("verifycustomization")))
    {
        FTimerHandle AppearanceTimer;
        GetWorldTimerManager().SetTimer(AppearanceTimer, [this]()
        {
            if (CharCreateWidget.IsValid())
                for (int32 Field=0; Field<5; ++Field) CharCreateWidget->CycleAppearance(Field,1);
        }, 3.0f, false);
    }

    // Interactive character-creator sessions must remain open while races and
    // genders are inspected.  Automated verification opts into screenshots
    // and the timed exit explicitly with -autoverifypreview.
    if (!FParse::Param(FCommandLine::Get(), TEXT("autoverifypreview")))
    {
        return;
    }

    FTimerHandle FirstScreenshotTimer;
    GetWorldTimerManager().SetTimer(FirstScreenshotTimer, []()
    {
        const FString ScreenshotDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"));
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*ScreenshotDir);
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(ScreenshotDir, TEXT("CharacterCreatePreviewStart.png")), true, false);
    }, 4.0f, false);

    FTimerHandle ScreenshotTimer;
    GetWorldTimerManager().SetTimer(ScreenshotTimer, []()
    {
        const FString ScreenshotDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"));
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*ScreenshotDir);
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(ScreenshotDir, TEXT("CharacterCreatePreview.png")), true, false);
    }, 6.0f, false);

    FTimerHandle QuitTimer;
    GetWorldTimerManager().SetTimer(QuitTimer, []()
    {
        FGenericPlatformMisc::RequestExit(false);
    }, 7.0f, false);
}

void AWowLoginController::ShowCharacterSelectPreviewForTesting()
{
    TArray<FWowCharacterInfo> PreviewCharacters;
    if (FParse::Param(FCommandLine::Get(), TEXT("emptycharacters")))
    {
        ShowCharacterSelectScreen(PreviewCharacters);
        // Exercise the real New Character handler and repeat the transition
        // without touching the offline roster or contacting a server.
        if (FParse::Param(FCommandLine::Get(), TEXT("verifyemptycreate")))
        {
            FTimerHandle CreateTimer, BackTimer, RepeatTimer;
            GetWorldTimerManager().SetTimer(CreateTimer, this,
                &AWowLoginController::HandleCreateCharacterRequest, 5.0f, false);
            GetWorldTimerManager().SetTimer(BackTimer, [this]()
            {
                ShowCharacterSelectScreen(TArray<FWowCharacterInfo>());
            }, 9.0f, false);
            GetWorldTimerManager().SetTimer(RepeatTimer, this,
                &AWowLoginController::HandleCreateCharacterRequest, 12.0f, false);
        }
        return;
    }

    const bool bVerifyAllRaces = FParse::Param(FCommandLine::Get(), TEXT("verifyallraces"));
    const bool bVerifyAllRaceLocomotion = FParse::Param(
        FCommandLine::Get(), TEXT("verifyallracelocomotion"));
    const bool bVerifyLocomotion = FParse::Param(
        FCommandLine::Get(), TEXT("verifylocomotionpreview"));

    if (bVerifyLocomotion)
    {
        FWowCharacterInfo Dwarf;
        Dwarf.Guid = 1;
        Dwarf.Name = TEXT("DwarfLocomotion");
        Dwarf.Race = 3;
        Dwarf.Class = 1;
        Dwarf.Gender = 0;
        Dwarf.Level = 1;
        Dwarf.MapId = 0;
        Dwarf.ZoneId = 1;
        PreviewCharacters.Add(Dwarf);
    }
    else if (bVerifyAllRaces || bVerifyAllRaceLocomotion)
    {
        struct FPlayableRacePreview
        {
            uint8 Race;
            uint8 Class;
            uint8 Gender;
            const TCHAR* Name;
            uint32 Zone;
        };
        static const FPlayableRacePreview Races[] = {
            { 1,  2, 0, TEXT("Human"),    12 },
            { 2,  1, 1, TEXT("Orc"),      14 },
            { 3,  3, 0, TEXT("Dwarf"),     1 },
            { 4, 11, 1, TEXT("NightElf"), 141 },
            { 5,  9, 0, TEXT("Undead"),   85 },
            { 6,  7, 1, TEXT("Tauren"),  215 },
            { 7,  8, 0, TEXT("Gnome"),     1 },
            { 8,  5, 1, TEXT("Troll"),    14 },
            { 10, 2, 0, TEXT("BloodElf"), 3430 },
            { 11, 7, 1, TEXT("Draenei"),  3524 }
        };

        int64 Guid = 1;
        for (const FPlayableRacePreview& Race : Races)
        {
            FWowCharacterInfo Character;
            Character.Guid = Guid++;
            Character.Name = Race.Name;
            Character.Race = Race.Race;
            Character.Class = Race.Class;
            Character.Gender = Race.Gender;
            Character.Level = 1;
            Character.MapId = Race.Race == 2 || Race.Race == 6 || Race.Race == 8 ? 1 : 0;
            Character.ZoneId = Race.Zone;
            PreviewCharacters.Add(Character);
        }
    }
    else
    {
        FWowCharacterInfo HumanPaladin;
        HumanPaladin.Guid = 1;
        HumanPaladin.Name = TEXT("Unreal");
        HumanPaladin.Race = 1;
        HumanPaladin.Class = 2;
        HumanPaladin.Gender = 0;
        HumanPaladin.Level = 9;
        HumanPaladin.MapId = 0;
        HumanPaladin.ZoneId = 12;
        PreviewCharacters.Add(HumanPaladin);

        FWowCharacterInfo DraeneiHunter;
        DraeneiHunter.Guid = 2;
        DraeneiHunter.Name = TEXT("Preview");
        DraeneiHunter.Race = 11;
        DraeneiHunter.Class = 3;
        DraeneiHunter.Gender = 1;
        DraeneiHunter.Level = 1;
        DraeneiHunter.MapId = 0;
        DraeneiHunter.ZoneId = 3524;
        PreviewCharacters.Add(DraeneiHunter);

        FWowCharacterInfo OrcWarrior;
        OrcWarrior.Guid = 3;
        OrcWarrior.Name = TEXT("Offline");
        OrcWarrior.Race = 2;
        OrcWarrior.Class = 1;
        OrcWarrior.Gender = 0;
        OrcWarrior.Level = 8;
        OrcWarrior.MapId = 1;
        OrcWarrior.ZoneId = 14;
        PreviewCharacters.Add(OrcWarrior);
    }

    ShowCharacterSelectScreen(PreviewCharacters);

    if (!FParse::Param(FCommandLine::Get(), TEXT("autoverifypreview")))
    {
        return;
    }

    if (bVerifyLocomotion)
    {
        struct FAnimCapture
        {
            float Time;
            int32 AnimationId;
            const TCHAR* Filename;
            bool bStartAnimation;
        };
        static const FAnimCapture Captures[] = {
            { 3.00f, 5,  TEXT("Dwarf_Run_A.png"),  true  },
            { 3.55f, 5,  TEXT("Dwarf_Run_B.png"),  false },
            { 4.20f, 38, TEXT("Dwarf_Jump_A.png"), true  },
            { 4.70f, 38, TEXT("Dwarf_Jump_B.png"), false }
        };

        for (const FAnimCapture& Capture : Captures)
        {
            FTimerHandle Timer;
            const FString Filename = Capture.Filename;
            GetWorldTimerManager().SetTimer(Timer, [this, Capture, Filename]()
            {
                if (Capture.bStartAnimation && CharacterPreview)
                {
                    CharacterPreview->PlayAnimationForTesting(Capture.AnimationId, true);
                }
                const FString ScreenshotDir = FPaths::Combine(
                    FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("LocomotionPreview"));
                FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*ScreenshotDir);
                FScreenshotRequest::RequestScreenshot(
                    FPaths::Combine(ScreenshotDir, Filename), true, false);
            }, Capture.Time, false);
        }

        FTimerHandle QuitTimer;
        GetWorldTimerManager().SetTimer(QuitTimer, []()
        {
            FGenericPlatformMisc::RequestExit(false);
        }, 6.0f, false);
        return;
    }

    if (bVerifyAllRaceLocomotion)
    {
        constexpr float RaceInterval = 6.0f;
        struct FLocomotionCapture
        {
            float Offset;
            int32 AnimationId;
            const TCHAR* Suffix;
            bool bStartAnimation;
        };
        static const FLocomotionCapture Captures[] = {
            { 3.00f, 5,  TEXT("Run_A"),  true  },
            { 3.50f, 5,  TEXT("Run_B"),  false },
            { 4.05f, 38, TEXT("Jump_A"), true  },
            { 4.55f, 38, TEXT("Jump_B"), false }
        };

        for (int32 RaceIndex = 0; RaceIndex < PreviewCharacters.Num(); ++RaceIndex)
        {
            const float RaceStart = RaceIndex * RaceInterval;
            FTimerHandle SelectionTimer;
            GetWorldTimerManager().SetTimer(SelectionTimer, [this, RaceIndex]()
            {
                if (CharSelectWidget.IsValid())
                {
                    CharSelectWidget->SelectCharacterForTesting(RaceIndex);
                }
            }, 0.25f + RaceStart, false);

            const FString RaceName = PreviewCharacters[RaceIndex].Name;
            const uint8 RaceId = PreviewCharacters[RaceIndex].Race;
            for (const FLocomotionCapture& Capture : Captures)
            {
                FTimerHandle CaptureTimer;
                const FString Suffix = Capture.Suffix;
                GetWorldTimerManager().SetTimer(CaptureTimer,
                    [this, Capture, RaceName, RaceId, Suffix]()
                {
                    if (Capture.bStartAnimation && CharacterPreview)
                    {
                        CharacterPreview->PlayAnimationForTesting(Capture.AnimationId, true);
                    }
                    const FString ScreenshotDir = FPaths::Combine(
                        FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("AllRaceLocomotion"));
                    FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*ScreenshotDir);
                    FScreenshotRequest::RequestScreenshot(FPaths::Combine(ScreenshotDir,
                        FString::Printf(TEXT("Race_%02u_%s_%s.png"),
                            RaceId, *RaceName, *Suffix)), true, false);
                }, RaceStart + Capture.Offset, false);
            }
        }

        FTimerHandle QuitTimer;
        GetWorldTimerManager().SetTimer(QuitTimer, []()
        {
            FGenericPlatformMisc::RequestExit(false);
        }, 1.0f + PreviewCharacters.Num() * RaceInterval, false);
        return;
    }

    if (bVerifyAllRaces)
    {
        constexpr float RaceInterval = 5.0f;
        for (int32 RaceIndex = 0; RaceIndex < PreviewCharacters.Num(); ++RaceIndex)
        {
            FTimerHandle SelectionTimer;
            GetWorldTimerManager().SetTimer(SelectionTimer, [this, RaceIndex]()
            {
                if (CharSelectWidget.IsValid())
                {
                    CharSelectWidget->SelectCharacterForTesting(RaceIndex);
                }
            }, 0.25f + RaceIndex * RaceInterval, false);

            const FString RaceName = PreviewCharacters[RaceIndex].Name;
            const uint8 RaceId = PreviewCharacters[RaceIndex].Race;
            FTimerHandle RaceScreenshotTimer;
            GetWorldTimerManager().SetTimer(RaceScreenshotTimer, [RaceName, RaceId]()
            {
                const FString ScreenshotDir = FPaths::Combine(
                    FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("CharacterSelectRaces"));
                FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*ScreenshotDir);
                FScreenshotRequest::RequestScreenshot(FPaths::Combine(ScreenshotDir,
                    FString::Printf(TEXT("Race_%02u_%s.png"), RaceId, *RaceName)), true, false);
            }, 4.25f + RaceIndex * RaceInterval, false);
        }

        FTimerHandle QuitTimer;
        GetWorldTimerManager().SetTimer(QuitTimer, []()
        {
            FGenericPlatformMisc::RequestExit(false);
        }, 1.0f + PreviewCharacters.Num() * RaceInterval, false);
        return;
    }

    FTimerHandle ScreenshotTimer;
    GetWorldTimerManager().SetTimer(ScreenshotTimer, []()
    {
        const FString ScreenshotDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"));
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*ScreenshotDir);
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(ScreenshotDir, TEXT("CharacterSelectPreview.png")), true, false);
    }, 6.0f, false);

    FTimerHandle QuitTimer;
    GetWorldTimerManager().SetTimer(QuitTimer, []()
    {
        FGenericPlatformMisc::RequestExit(false);
    }, 7.0f, false);
}

void AWowLoginController::OnStateChanged(EWowSessionState NewState)
{
    UE_LOG(LogWowLogin, Log, TEXT("Session state: %d"), static_cast<int32>(NewState));

    switch (NewState)
    {
    case EWowSessionState::AuthProofSent:
        if (ConnectionStatusLabel.IsValid()) ConnectionStatusLabel->SetText(FText::FromString(TEXT("Authenticating...")));
        break;
    case EWowSessionState::AuthRequestingRealmList:
        if (ConnectionStatusLabel.IsValid()) ConnectionStatusLabel->SetText(FText::FromString(TEXT("Retrieving realm list")));
        break;
    case EWowSessionState::AuthHaveRealmList:
        if (ConnectionManager)
        {
            // A server with its own data folder switches now (behind the connecting dialog),
            // two frames later so the status text is drawn before the reload blocks.
            if (PrepareServerDataSwitch())
            {
                PendingDataSwitchFrames = 2;
                break;
            }
            OnRealmList(ConnectionManager->GetCachedRealms());
        }
        break;
    case EWowSessionState::WorldAuthenticated:
        if (ConnectionStatusLabel.IsValid()) ConnectionStatusLabel->SetText(FText::FromString(TEXT("Retrieving character list")));
        SetStatusText(TEXT("Requesting character list..."));
        ConnectionManager->RequestCharacterList();
        break;
    case EWowSessionState::WorldHaveCharList:
        if (GConfig && !RealmPreferenceKey.IsEmpty() && PendingRealmId != INDEX_NONE)
        {
            GConfig->SetInt(TEXT("WowUnreal.LastRealm"), *RealmPreferenceKey, PendingRealmId, GGameUserSettingsIni);
            GConfig->Flush(false, GGameUserSettingsIni);
        }
        ActiveRealmIndex = PendingRealmIndex;
        // If -createchar flag AND no characters exist, auto-create a Human Mage
        if (FParse::Param(FCommandLine::Get(), TEXT("createchar")) && !bCharCreateSent
            && ConnectionManager->GetCachedCharacters().Num() == 0)
        {
            bCharCreateSent = true;
            FString CharName;
            if (!FParse::Value(FCommandLine::Get(), TEXT("-charname="), CharName))
            {
                // WoW names must be letters only â€” generate a random valid name
                static const TCHAR* Prefixes[] = { TEXT("Thorn"), TEXT("Ash"), TEXT("Elm"), TEXT("Oak"), TEXT("Frost"), TEXT("Storm"), TEXT("Dawn"), TEXT("Dusk") };
                static const TCHAR* Suffixes[] = { TEXT("wind"), TEXT("blade"), TEXT("fire"), TEXT("song"), TEXT("heart"), TEXT("vale"), TEXT("keep"), TEXT("ward") };
                int32 P = FMath::RandRange(0, UE_ARRAY_COUNT(Prefixes) - 1);
                int32 S = FMath::RandRange(0, UE_ARRAY_COUNT(Suffixes) - 1);
                CharName = FString(Prefixes[P]) + Suffixes[S];
            }
            UE_LOG(LogWowLogin, Log, TEXT("Auto-creating character: %s (Human Mage)"), *CharName);
            // Human=1, Mage=8, Male=0
            ConnectionManager->CreateCharacter(CharName, 1, 8, 0, 0, 0, 0, 0, 0);
            SetStatusText(FString::Printf(TEXT("Creating character %s..."), *CharName));
        }
        else
        {
            ShowCharacterSelectScreen(ConnectionManager->GetCachedCharacters());
        }
        break;
    case EWowSessionState::WorldEnteringWorld:
        // Bind entity events and set defer flag BEFORE the server sends LOGIN_VERIFY_WORLD
        // so we capture the spawn position without teleporting the pawn
        {
            AWowGameplayController* GPC = Cast<AWowGameplayController>(
                UGameplayStatics::GetPlayerController(this, 0));
            if (GPC)
            {
                if (!GPC->ConnectionManager)
                {
                    GPC->ConnectionManager = ConnectionManager;
                    GPC->BindEntityEvents();
                    UE_LOG(LogWowLogin, Log, TEXT("Pre-bound ConnectionManager to GameplayController (before world entry)"));
                }
                // Defer teleport until terrain loads
                GPC->bDeferSpawnTeleport = true;

                // Hide pawn while loading
                if (APawn* Pawn = GPC->GetPawn())
                {
                    Pawn->SetActorHiddenInGame(true);
                    Pawn->SetActorEnableCollision(false);
                    Pawn->SetActorLocation(FVector::ZeroVector);
                    if (ACharacter* Char = Cast<ACharacter>(Pawn))
                    {
                        Char->GetCharacterMovement()->GravityScale = 0.0f;
                        Char->GetCharacterMovement()->Velocity = FVector::ZeroVector;
                    }
                }
            }
        }
        break;
    case EWowSessionState::WorldInGame:
        UE_LOG(LogWowLogin, Log, TEXT("Entered world â€” showing loading screen"));
        if (CinematicManager)
        {
            CinematicManager->StopCinematic();
            delete CinematicManager;
            CinematicManager = nullptr;
        }
        ClearCurrentScreen();
        // The selected character's map is known from the character list: show its loading
        // screen straight away instead of a generic one that is swapped a second later.
        ShowLoadingScreen(bHasSelectedCharacterInfo ? static_cast<uint32>(FMath::Max(SelectedCharacterInfo.MapId, 0)) : 0u);
        InitializeWorldSystems();
        break;
    default:
        break;
    }
}

void AWowLoginController::OnRealmList(const TArray<FWowRealmInfo>& Realms)
{
    UE_LOG(LogWowLogin,Log,TEXT("Choosing from %d cached realms"),Realms.Num());
    int32 LastRealmId = INDEX_NONE;
    if (GConfig && !RealmPreferenceKey.IsEmpty())
        GConfig->GetInt(TEXT("WowUnreal.LastRealm"), *RealmPreferenceKey, LastRealmId, GGameUserSettingsIni);
    int32 Index = Realms.IndexOfByPredicate([LastRealmId](const FWowRealmInfo& Realm) {
        return LastRealmId != INDEX_NONE && Realm.RealmId == LastRealmId && !Realm.bLocked && !(Realm.Flags & 2);
    });
    if (Index == INDEX_NONE && Realms.Num() == 1 && !Realms[0].bLocked && !(Realms[0].Flags & 2)) Index = 0;
    UE_LOG(LogWowLogin,Log,TEXT("Realm choice index=%d savedId=%d"),Index,LastRealmId);
    if (Index != INDEX_NONE) HandleRealmSelected(Index);
    else ShowRealmSelectScreen(Realms);
}

void AWowLoginController::OnCharacterList(const TArray<FWowCharacterInfo>& Characters)
{
    UE_LOG(LogWowLogin, Log, TEXT("Received %d characters"), Characters.Num());

	if (bReturningFromLogout)
	{
		bReturningFromLogout = false;
		ShowCharacterSelectScreen(Characters);
		return;
	}

    // Auto-enter world with first character when -autologin is active
    // Skip if -createchar is set â€” we'll enter after creation completes
    if (FParse::Param(FCommandLine::Get(), TEXT("autologin")) && Characters.Num() > 0
        && !FParse::Param(FCommandLine::Get(), TEXT("createchar")))
    {
        UE_LOG(LogWowLogin, Log, TEXT("Autologin: entering world with '%s' (GUID %lld)"),
            *Characters[0].Name, Characters[0].Guid);
        HandleCharacterSelected(Characters[0].Guid);
        return;
    }

    // If we just created a character and want to auto-enter
    if (bAutoEnterAfterCreate && Characters.Num() > 0)
    {
        bAutoEnterAfterCreate = false;
        // Enter with the last character (the one just created)
        const FWowCharacterInfo& NewChar = Characters.Last();
        UE_LOG(LogWowLogin, Log, TEXT("Auto-entering world with newly created '%s' (GUID %lld)"),
            *NewChar.Name, NewChar.Guid);
        HandleCharacterSelected(NewChar.Guid);
        return;
    }

    // If we're on the create screen (post-creation refresh), go back to select
    if (CharCreateWidget.IsValid())
    {
        ShowCharacterSelectScreen(Characters);
    }
}

void AWowLoginController::ResetWorldWeatherForGlue()
{
	TArray<AActor*> Skies;
	UGameplayStatics::GetAllActorsOfClass(this, AWowSkyManager::StaticClass(), Skies);
	for (AActor* Sky : Skies)
		if (UWowWeatherComponent* Weather = Sky ? Sky->FindComponentByClass<UWowWeatherComponent>() : nullptr)
			Weather->ResetForGlueScreen();
}

void AWowLoginController::OnLogoutComplete()
{
	bReturningFromLogout = true;
	// Leave the world's zone music/ambience/weather behind: character select plays the glue music.
	if (AWowAudioManager* Audio = GetOrCreateAudioManager()) Audio->EnterGlueScreen();
	ResetWorldWeatherForGlue();
	ClearCurrentScreen();
	SuppressDebugMessagesForGlueScreen();

	if (AWowGameplayController* GPC = Cast<AWowGameplayController>(UGameplayStatics::GetPlayerController(this, 0)))
	{
		GPC->SetSoftwareCursorSuppressed(true, true);
		GPC->bShowMouseCursor = true;
		if (APawn* Pawn = GPC->GetPawn())
		{
			Pawn->SetActorHiddenInGame(true);
			Pawn->SetActorEnableCollision(false);
		}
	}

	UE_LOG(LogWowLogin, Log, TEXT("Logout complete; waiting for refreshed character list"));
}

void AWowLoginController::OnWorldDisconnected(const FString& Msg)
{
    // A dead socket cannot return to character selection: a new login is required.
    if (ConnectionManager)
    {
        ConnectionManager->PacketHandler.OnLoginVerifyWorld.Remove(LoginVerifyWorldHandle);
        LoginVerifyWorldHandle.Reset();
    }
    bWaitingForInitialLoad = false;
    bPendingGroundSnap = false;
    bHasPendingSpawn = false;
    bEntryPresentationPrepared = false;
    bDeferredSpawnAppliedForEntry = false;
    bReturningFromLogout = false;
    bAutoEnterAfterCreate = false;
    bHasSelectedCharacterInfo = false;
    SelectedCharacterGuid = 0;
    ActiveRealmIndex = PendingRealmIndex = PendingRealmId = INDEX_NONE;
    if (WorldManager) WorldManager->EndWorldEntryWarmup();
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(OfflineWorldInitTimerHandle);
    if (AWowGameplayController* GPC = Cast<AWowGameplayController>(UGameplayStatics::GetPlayerController(this, 0)))
        GPC->EndDisconnectedWorldSession();
    // Disconnect (e.g. the server's daily restart): leave the world's music, ambience,
    // rain, lens drops and thunder behind, exactly like a normal logout.
    if (AWowAudioManager* Audio = GetOrCreateAudioManager()) Audio->EnterGlueScreen();
    ResetWorldWeatherForGlue();
    ShowLoginScreen();
    ShowConnectingDialog();
    if (ConnectionStatusLabel.IsValid()) ConnectionStatusLabel->SetText(FText::FromString(Msg));
    if (ConnectionButtonLabel.IsValid()) ConnectionButtonLabel->SetText(FText::FromString(TEXT("Okay")));
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ConnectionTimeoutHandle);
    SetStatusText(Msg);
}

void AWowLoginController::OnError(const FString& Msg)
{
    UE_LOG(LogWowLogin, Error, TEXT("Connection error: %s"), *Msg);
    const bool WasConnecting=GlueDialog.IsValid();
    if (WasConnecting)
    {
        if (ConnectionManager) ConnectionManager->Disconnect();
        ShowLoginScreen();
        ShowConnectingDialog();
        ConnectionStatusLabel->SetText(FText::FromString(Msg));
        ConnectionButtonLabel->SetText(FText::FromString(TEXT("Okay")));
        if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ConnectionTimeoutHandle);
    }
    SetStatusText(FString::Printf(TEXT("Error: %s"), *Msg));
}

// The name last sent with CMSG_CHAR_CREATE: a rejected name gets a message about what is wrong with it.
static FString GWowLastCreateName;

void AWowLoginController::OnCharCreateResult(uint8 ResultCode)
{
    if (ResultCode == 0x2F)
    {
        UE_LOG(LogWowLogin, Log, TEXT("Character created successfully"));
        SetStatusText(TEXT("Character created!"));
        // ConnectionManager auto-refreshes char list â†’ OnCharacterList will navigate back

        // If -createchar was used, auto-login with the newly created character
        if (FParse::Param(FCommandLine::Get(), TEXT("createchar")))
        {
            bAutoEnterAfterCreate = true;
        }
    }
    else if (ResultCode == 0x47) // CHAR_DELETE_SUCCESS
    {
        UE_LOG(LogWowLogin, Log, TEXT("Character deleted successfully"));
        SetStatusText(TEXT("Character deleted."));
        // ConnectionManager auto-refreshes the character list after a successful delete.
    }
    else
    {
        // 3.3.5 ResponseCodes (SMSG_CHAR_CREATE / SMSG_CHAR_DELETE).
        FString ErrMsg;
        switch (ResultCode)
        {
        case 0x30: ErrMsg = TEXT("Error creating character"); break;
        case 0x31: ErrMsg = TEXT("Character creation failed"); break;
        case 0x32: ErrMsg = TEXT("That name is unavailable"); break;
        case 0x33: ErrMsg = TEXT("Creation of that race and/or class is currently disabled"); break;
        case 0x34: ErrMsg = TEXT("You cannot have characters of both factions on a PvP realm"); break;
        case 0x35: ErrMsg = TEXT("This realm has reached its character limit"); break;
        case 0x36: ErrMsg = TEXT("You already have the maximum number of characters"); break;
        case 0x39: ErrMsg = TEXT("You need a higher expansion to create that race"); break;
        case 0x3A: ErrMsg = TEXT("You need a higher expansion to create that class"); break;
        case 0x3B: ErrMsg = TEXT("You need a higher level character to create that class"); break;
        case 0x3C: ErrMsg = TEXT("You can only have one character of that class"); break;
        case 0x46: ErrMsg = TEXT("Deleting character..."); break;
        case 0x48: ErrMsg = TEXT("Character deletion failed"); break;
        case 0x49: ErrMsg = TEXT("This character is locked for a transfer"); break;
        case 0x4A: ErrMsg = TEXT("A guild leader cannot be deleted"); break;
        case 0x4B: ErrMsg = TEXT("An arena team captain cannot be deleted"); break;
        case 0x58: ErrMsg = TEXT("Invalid name"); break;
        case 0x59: ErrMsg = TEXT("Enter a name for your character"); break;
        case 0x5A: ErrMsg = TEXT("Names must be at least 2 characters"); break;
        case 0x5B: ErrMsg = TEXT("Names must be no more than 12 characters"); break;
        case 0x5C: ErrMsg = TEXT("Names can only contain letters"); break;
        case 0x5D: ErrMsg = TEXT("Names must contain only one language"); break;
        case 0x5E: ErrMsg = TEXT("That name contains profanity"); break;
        case 0x5F: ErrMsg = TEXT("That name is unavailable"); break;
        case 0x60: ErrMsg = TEXT("You cannot use an apostrophe as the first or last character"); break;
        case 0x61: ErrMsg = TEXT("You can only have one apostrophe"); break;
        case 0x62: ErrMsg = TEXT("You cannot use the same letter three times consecutively"); break;
        case 0x63: ErrMsg = TEXT("You cannot use a space in your name"); break;
        default: ErrMsg = FString::Printf(TEXT("Creation failed (0x%02X)"), ResultCode); break;
        }
        // Whether a name may hold a space is the server's call (some realms allow two names), so the
        // name is always sent. When the server rejects one with a space, say so plainly: AzerothCore
        // answers "Enter a name for your character" (CHAR_NAME_NO_NAME) for "Test tauren" (Stefan 10.10).
        const bool bNameRejected = ResultCode >= 0x58 && ResultCode <= 0x64;
        if (bNameRejected && GWowLastCreateName.TrimStartAndEnd().Contains(TEXT(" ")))
        {
            ErrMsg = TEXT("Surname not allowed by server");
        }
        // On the create screen the error shows like the game's red UIErrorsFrame line (Stefan 11.10).
        if (CharCreateWidget.IsValid() && ResultCode != 0x46) CharCreateWidget->ShowError(ErrMsg);
        else SetStatusText(ResultCode == 0x46 ? ErrMsg : FString::Printf(TEXT("Error: %s"), *ErrMsg));
    }
}

void AWowLoginController::InitializeWorldSystems()
{
    UWorld* World = GetWorld();
    if (!World) return;

    // 1. Enable terrain streaming on the WorldManager (loads WDT/WDL)
    if (WorldManager)
    {
        WorldManager->EnableTerrainStreaming();
    }

    // 2. Ensure ConnectionManager is bound to GameplayController
    //    (may already be bound from WorldEnteringWorld state)
    if (AWowGameplayController* GPC = Cast<AWowGameplayController>(UGameplayStatics::GetPlayerController(this, 0)))
    {
        if (!GPC->ConnectionManager)
        {
            GPC->ConnectionManager = ConnectionManager;
            GPC->BindEntityEvents();
            UE_LOG(LogWowLogin, Log, TEXT("Bound ConnectionManager to GameplayController"));
        }
    }

    // 3. Start AudioManager (find existing or spawn new)
    if (WorldManager && WorldManager->GetMpqManager())
    {
        AWowAudioManager* AudioMgr = GetOrCreateAudioManager();
        if (AudioMgr)
        {
            AudioMgr->SetMpqManager(WorldManager->GetMpqManager());
        }
    }

    // 4. Load UI system (Lua VM + FrameXML)
    if (WorldManager && WorldManager->GetMpqManager())
    {
        if (UGameInstance* GI = GetGameInstance())
        {
            if (UWowUIManager* UIManager = GI->GetSubsystem<UWowUIManager>())
            {
				UIManager->SetConnectionManager(ConnectionManager);
				AWowAudioManager* AudioManager = GetOrCreateAudioManager();
				UIManager->SetAudioManager(AudioManager);
				if (UIManager->IsUILoaded())
				{
					// r427 (Stefan 23.10: logged out of TormentWoW, logged in on TheraWoW and the
					// Torment ElvUI was still on screen until /reload): the OG client unloads the
					// in-game UI at logout and loads FrameXML and the enabled addons fresh for every
					// login. Rebuild it here: the old UI saves its variables for the character, realm
					// and client folder it was loaded for, then this character's addons load.
					AWowGameplayController* ReloadController =
						Cast<AWowGameplayController>(UGameplayStatics::GetPlayerController(this, 0));
					if (ReloadController)
					{
						UE_LOG(LogWowLogin, Log, TEXT("Login after logout: rebuilding the in-game UI for this character"));
						ReloadController->ReloadUiNow();
					}
					CloseStartupGameplayPanels();
					return;
				}

                // Create root canvas for WoW UI frame system
                if (GEngine && GEngine->GameViewport)
                {
                    // Create the root canvas and add it to the viewport via a
                    // full-screen Slate wrapper. The key is that the SConstraintCanvas
                    // must be the TOP widget added to the viewport overlay â€” then the
                    // overlay slot's HAlign_Fill / VAlign_Fill will give it the full
                    // viewport geometry, and the inner anchored slot stretches to match.
                    UCanvasPanel* UIRootCanvas = NewObject<UCanvasPanel>(UIManager, TEXT("WowUIRootCanvas"));
                    UIRootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
                    UIRootCanvas->AddToRoot(); // Prevent GC

                    TSharedRef<SWidget> CanvasWidget = UIRootCanvas->TakeWidget();

                    // Use a simple SOverlay â†’ SConstraintCanvas â†’ Canvas chain.
                    // SOverlay children always stretch to fill their parent.
                    TSharedRef<SOverlay> Wrapper = SNew(SOverlay)
                        + SOverlay::Slot()
                        .HAlign(HAlign_Fill)
                        .VAlign(VAlign_Fill)
                        [
                            CanvasWidget
                        ];

                    FWowUiSurfaces::Add(Wrapper, EWowUiZone::Hud, 50, TEXT("FrameXML"));
                    UIManager->SetRootCanvas(UIRootCanvas);
                    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
                    {
                        ApplyWowLoginUiInputMode(PC, CanvasWidget);
                    }
                    UE_LOG(LogWowLogin, Log, TEXT("Created WoW UI root canvas at z-order 50"));
                }

				// FrameXML construction runs stock OnLoad/OnHide scripts. Those are not
				// user actions and must not leak bag, panel or menu sounds into loading.
				if (AudioManager)
				{
					AudioManager->SetUiSoundsSuppressed(true);
				}
				UIManager->LoadUI(WorldManager->GetMpqManager(), WorldManager->GetAssetCache());
				CloseStartupGameplayPanels();
				if (AudioManager)
				{
					AudioManager->SetUiSoundsSuppressed(false);
				}
                UE_LOG(LogWowLogin, Log, TEXT("Loaded WoW UI system"));
            }
        }
    }

    // Slate chat/combat log disabled â€” using Lua FrameXML UI instead.
    // The WoW FrameXML handles chat windows, combat log, etc.
}

void AWowLoginController::HandleLoginSubmit(const FString& Server, int32 Port, const FString& User, const FString& Pass)
{
    UE_LOG(LogWowLogin, Log, TEXT("Login: %s@%s:%d"), *User, *Server, Port);
	PlayGlueUiSound(TEXT("GlueScreenButtonClick"));
	if (bOfflineFlow)
	{
		SetStatusText(TEXT("Loading local characters..."));
		LoadOfflineRoster();
		ShowCharacterSelectScreen(OfflineCharacterInfos);
		return;
	}

    if (LoginWidget.IsValid() && LoginWidget->GetServerVersion() != 2)
    {
        SetStatusText(TEXT("Vanilla and TBC servers can be saved; connecting currently requires WotLK 3.3.5a."));
        return;
    }
	SaveOnlineLoginBookmark(Server, Port, User, Pass);
    LoginServerAddress = Server.TrimStartAndEnd();
    LoginServerPort = Port;
    LoginServerVersion = LoginWidget.IsValid() ? LoginWidget->GetServerVersion() : 2;
    RealmPreferenceKey = FMD5::HashAnsiString(*FString::Printf(TEXT("%s:%d|%s"),
        *Server.TrimStartAndEnd().ToLower(), Port, *User.TrimStartAndEnd().ToUpper()));
    PendingRealmId = PendingRealmIndex = ActiveRealmIndex = INDEX_NONE;
    ShowConnectingDialog();
    ConnectionManager->Login(User, Pass, Server, Port);
}

void AWowLoginController::SaveOnlineLoginBookmark(
	const FString& Server,
	int32 Port,
	const FString& User,
	const FString& Pass)
{
	if (Server.IsEmpty() || User.IsEmpty())
	{
		return;
	}

	UWowCredentialStore* Store = NewObject<UWowCredentialStore>(this);
	Store->LoadCredentials();
	const TArray<FWowCredential> ExistingProfiles = Store->GetAllCredentials();
	FWowCredential Profile;
	Profile.Alias = Server;
	Profile.ServerAddress = Server;
	Profile.AuthPort = Port;
	Profile.Username = User;
	Profile.Password = Pass;
    if(LoginWidget.IsValid())
    {
        Profile.WebsiteUrl=LoginWidget->GetWebsiteUrl();
        Profile.AccountUrl=LoginWidget->GetAccountUrl();
        Profile.TermsUrl=LoginWidget->GetTermsUrl();
    }
    Profile.GameVersion = LoginWidget.IsValid() ? LoginWidget->GetServerVersion() : 2;
	Profile.bIsDefault = ExistingProfiles.IsEmpty();
	for (const FWowCredential& Existing : ExistingProfiles)
	{
		if (Existing.GameVersion == Profile.GameVersion && Existing.ServerAddress.Equals(Server, ESearchCase::IgnoreCase)
			&& Existing.AuthPort == Port
			&& Existing.Username.Equals(User, ESearchCase::IgnoreCase))
		{
			Profile.Alias = Existing.Alias;
			Profile.bIsDefault = Existing.bIsDefault;
			Profile.DataPath = Existing.DataPath; // set in WowUnrealSettings, never in the login screen
			break;
		}
	}
	Store->AddCredential(Profile);
    if (Store->SaveCredentials() && LoginWidget.IsValid())
    {
        LoginWidget->SetSavedProfiles(Store->GetAllCredentials());
        LoginWidget->SetStatusText(TEXT("Server saved"));
    }
}

namespace
{
FString NormalizeDataFolder(FString Path)
{
    Path = FPaths::ConvertRelativePathToFull(Path.TrimStartAndEnd().TrimQuotes());
    FPaths::NormalizeDirectoryName(Path);
    Path.ReplaceInline(TEXT("\\"), TEXT("/"));
    while (Path.EndsWith(TEXT("/"))) Path.LeftChopInline(1);
    if (Path.EndsWith(TEXT("/Data"), ESearchCase::IgnoreCase)) Path.LeftChopInline(5);
    return Path.ToLower();
}
}

// A server without a Data folder chosen in WowUnrealSettings: look for a WoW install next to
// the default one whose folder name carries the server's name ("logon.wow-torment.com" ->
// "World of Warcraft 3.3.5a - TormentWoW"). Only an unambiguous single match is used.
static FString FindServerDataFolder(const FString& ServerAddress, const FString& DefaultData)
{
    static const TSet<FString> Generic = { TEXT("logon"), TEXT("login"), TEXT("auth"), TEXT("wow"), TEXT("www"), TEXT("com"),
        TEXT("org"), TEXT("net"), TEXT("realm"), TEXT("realmlist"), TEXT("play"), TEXT("server"), TEXT("eu"), TEXT("us"),
        TEXT("game"), TEXT("world"), TEXT("warcraft"), TEXT("development"), TEXT("dev"), TEXT("io"), TEXT("gg") };
    TArray<FString> Tokens;
    FString Address = ServerAddress.ToLower();
    Address.ReplaceInline(TEXT("-"), TEXT("."));
    Address.ReplaceInline(TEXT("_"), TEXT("."));
    TArray<FString> Parts;
    Address.ParseIntoArray(Parts, TEXT("."), true);
    for (const FString& Part : Parts)
        if (Part.Len() >= 3 && !Generic.Contains(Part) && !Part.IsNumeric()) Tokens.AddUnique(Part);
    if (Tokens.IsEmpty() || DefaultData.IsEmpty()) return FString();
    FString InstallRoot = FPaths::ConvertRelativePathToFull(DefaultData);
    FPaths::NormalizeDirectoryName(InstallRoot);
    if (FPaths::GetCleanFilename(InstallRoot).Equals(TEXT("Data"), ESearchCase::IgnoreCase)) InstallRoot = FPaths::GetPath(InstallRoot);
    FString SearchRoot = FPaths::GetPath(InstallRoot);
    if (SearchRoot.IsEmpty()) return FString();
    if (SearchRoot.EndsWith(TEXT(":"))) SearchRoot += TEXT("/"); // drive root ("E:" alone is the drive's current folder)
    TArray<FString> Folders;
    IFileManager::Get().FindFiles(Folders, *FPaths::Combine(SearchRoot, TEXT("*")), false, true);
    TArray<FString> Matches;
    for (const FString& Folder : Folders)
    {
        const FString Lower = Folder.ToLower();
        bool bNamed = false;
        for (const FString& Token : Tokens) if (Lower.Contains(Token)) { bNamed = true; break; }
        if (!bNamed) continue;
        // Any WoW Data folder: MPQ archives directly inside it (repacks name them differently).
        const FString Data = FPaths::Combine(SearchRoot, Folder, TEXT("Data"));
        TArray<FString> Archives;
        IFileManager::Get().FindFiles(Archives, *FPaths::Combine(Data, TEXT("*.mpq")), true, false);
        UE_LOG(LogWowLogin, Log, TEXT("Server data search: %s (%d archives)"), *Data, Archives.Num());
        if (Archives.Num() > 0) Matches.Add(Data);
    }
    UE_LOG(LogWowLogin, Log, TEXT("Server data search for %s in %s: tokens [%s], %d folders, %d matches"),
        *ServerAddress, *SearchRoot, *FString::Join(Tokens, TEXT(",")), Folders.Num(), Matches.Num());
    return Matches.Num() == 1 ? Matches[0] : FString();
}

bool AWowLoginController::PrepareServerDataSwitch()
{
    PendingDataPath.Empty();
    if (bOfflineFlow || !WorldManager || !WorldManager->GetMpqManager() || LoginServerAddress.IsEmpty())
    {
        UE_LOG(LogWowLogin, Log, TEXT("Server data switch skipped (offline=%d world=%d address='%s')"),
            bOfflineFlow ? 1 : 0, WorldManager ? 1 : 0, *LoginServerAddress);
        return false;
    }
    UWowCredentialStore* Store = NewObject<UWowCredentialStore>(this);
    Store->LoadCredentials();
    FString Target;
    for (const FWowCredential& Profile : Store->GetAllCredentials())
    {
        if (Profile.GameVersion == LoginServerVersion && Profile.AuthPort == LoginServerPort
            && Profile.ServerAddress.Equals(LoginServerAddress, ESearchCase::IgnoreCase) && !Profile.DataPath.IsEmpty())
        {
            Target = Profile.DataPath;
            break;
        }
    }
    // No folder chosen: a WoW install named after the server, else the default folder
    // (switches back after another server).
    if (Target.IsEmpty())
    {
        Target = FindServerDataFolder(LoginServerAddress, FWowClientPreferences::GetDataSource());
        if (!Target.IsEmpty())
            UE_LOG(LogWowLogin, Log, TEXT("Server %s has no Data folder set; found matching install %s"), *LoginServerAddress, *Target);
    }
    if (Target.IsEmpty()) Target = FWowClientPreferences::GetDataSource();
    if (Target.IsEmpty()) return false;
    if (NormalizeDataFolder(Target) == NormalizeDataFolder(WorldManager->GetMpqManager()->GetDataPath()))
    {
        UE_LOG(LogWowLogin, Log, TEXT("Server %s keeps the current game data %s"), *LoginServerAddress, *Target);
        return false;
    }
    PendingDataPath = Target;
    UE_LOG(LogWowLogin, Log, TEXT("Server %s:%d uses game data %s"), *LoginServerAddress, LoginServerPort, *Target);
    if (ConnectionStatusLabel.IsValid()) ConnectionStatusLabel->SetText(FText::FromString(TEXT("Loading server game data...")));
    SetStatusText(TEXT("Loading server game data..."));
    return true;
}

void AWowLoginController::RunServerDataSwitch()
{
    if (!PendingDataPath.IsEmpty() && WorldManager)
    {
        AWowAudioManager* Audio = bLoginMusicStarted ? GetOrCreateAudioManager() : nullptr;
        if (Audio) Audio->StopMusic();
        FString Error;
        if (!WorldManager->SwitchDataFolder(PendingDataPath, Error))
        {
            SetStatusText(Error);
        }
        // The separate stock-asset archive set used by the login scene follows the new folder.
        LoginOriginalData.Reset();
        LoginOriginalCache.Reset();
        if (Audio) Audio->PlayLoginMusic();
        PendingDataPath.Empty();
    }
    if (ConnectionManager && ConnectionManager->GetState() == EWowSessionState::AuthHaveRealmList)
    {
        OnRealmList(ConnectionManager->GetCachedRealms());
    }
}

void AWowLoginController::HandleRealmSelected(int32 Index)
{
    if (!ConnectionManager) return;
    // The Blueprint getter returns an array by value. Keep its storage alive;
    // a reference to GetCachedRealms()[Index] would dangle at the semicolon.
    const TArray<FWowRealmInfo> Realms = ConnectionManager->GetCachedRealms();
    if (!Realms.IsValidIndex(Index))
    {
        OnError(TEXT("The selected realm is no longer available. Please reconnect."));
        return;
    }
    if (CharSelectWidget.IsValid() && Index == ActiveRealmIndex)
    {
        CloseGlueDialog();
        return;
    }
    const FWowRealmInfo& Realm = Realms[Index];
    UE_LOG(LogWowLogin, Log, TEXT("Selecting cached realm index=%d id=%u flags=%u locked=%d"),
        Index, Realm.RealmId, Realm.Flags, Realm.bLocked ? 1 : 0);
    if (Realm.bLocked || (Realm.Flags & 2))
    {
        OnError(TEXT("The selected realm is locked or offline. Please choose another realm."));
        return;
    }
    PendingRealmId = Realm.RealmId;
    PendingRealmIndex = Index;
    ShowConnectingDialog();
    ConnectionManager->SelectRealm(Index);
}

bool AWowLoginController::CanEditOfflineRoster() const
{
    return bOfflineFlow && CharSelectWidget.IsValid() && CurrentWidget == CharSelectWidget && !LoadingWidget.IsValid();
}

void AWowLoginController::HandleCharacterSelected(int64 Guid)
{
    // The private editor can save while this screen is open. Never enter using
    // the roster snapshot captured before those edits.
    if (bOfflineFlow) LoadOfflineRoster();
    SetStatusText(TEXT("Entering world..."));
    SelectedCharacterGuid = Guid;
    bHasSelectedCharacterInfo = false;
	const TArray<FWowCharacterInfo> AvailableCharacters = bOfflineFlow
		? OfflineCharacterInfos
		: (ConnectionManager ? ConnectionManager->GetCachedCharacters() : TArray<FWowCharacterInfo>());
    if (ConnectionManager)
    {
        ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid = static_cast<uint64>(Guid);
        for (const FWowCharacterInfo& Character : AvailableCharacters)
        {
            if (Character.Guid == Guid)
            {
                SelectedCharacterInfo = Character;
                bHasSelectedCharacterInfo = true;
                break;
            }
        }
    }
	if (bOfflineFlow)
	{
		if (!bHasSelectedCharacterInfo)
		{
			SetStatusText(TEXT("Select a local character first."));
			return;
		}
		if (AWowAudioManager* AudioManager = GetOrCreateAudioManager())
		{
			AudioManager->PlayEnterWorldSound();
		}
		StartOfflineWorldLoadingForTesting();
		return;
	}

    // Start the authored one-shot at the accepted Enter World action. The same
    // audio manager survives the UI transition, so its tail carries naturally
    // across the loading screen instead of being triggered by the loading screen.
    if (AWowAudioManager* AudioManager = GetOrCreateAudioManager())
    {
        AudioManager->PlayEnterWorldSound();
    }

    // The character list already told us the map; LOGIN_VERIFY_WORLD only confirms it.
    ShowLoadingScreen(bHasSelectedCharacterInfo ? static_cast<uint32>(FMath::Max(SelectedCharacterInfo.MapId, 0)) : 0u);

    // Bind to OnLoginVerifyWorld to get the actual map ID for the loading screen
    if (ConnectionManager)
    {
        ConnectionManager->PacketHandler.OnLoginVerifyWorld.Remove(LoginVerifyWorldHandle);
        LoginVerifyWorldHandle = ConnectionManager->PacketHandler.OnLoginVerifyWorld.AddUObject(this, &AWowLoginController::OnLoginVerifyWorld);
    }

    ConnectionManager->EnterWorld(Guid);
}

void AWowLoginController::HandleCreateCharacterRequest()
{
    ShowCharacterCreateScreen();
}

void AWowLoginController::HandleDeleteCharacterRequest(int64 Guid)
{
	if (bOfflineFlow)
	{
		const int32 Removed = OfflineCharacters.RemoveAll(
			[Guid](const FWowOfflineCharacterRecord& Character)
			{
				return Character.Guid == Guid;
			});
		if (Removed > 0 && FWowOfflineCharacterStore::Save(OfflineCharacters))
		{
			RefreshOfflineCharacterInfos();
			ShowCharacterSelectScreen(OfflineCharacterInfos);
		}
		else
		{
			SetStatusText(TEXT("Could not delete the local character."));
		}
		return;
	}

    if (!ConnectionManager || Guid == 0)
    {
        SetStatusText(TEXT("Select a character first."));
        return;
    }

    SetStatusText(TEXT("Deleting character..."));
    ConnectionManager->DeleteCharacter(Guid);
}

void AWowLoginController::HandleCharacterCreated(const FString& Name, uint8 Race, uint8 Class, uint8 Gender,
    uint8 Skin, uint8 Face, uint8 HairStyle, uint8 HairColor, uint8 FacialHair)
{
	if (bOfflineFlow)
	{
		const FString CleanName = Name.TrimStartAndEnd();
		if (CleanName.IsEmpty())
		{
			SetStatusText(TEXT("Enter a character name."));
			return;
		}
		for (const FWowOfflineCharacterRecord& Existing : OfflineCharacters)
		{
			if (Existing.Name.Equals(CleanName, ESearchCase::IgnoreCase))
			{
				SetStatusText(TEXT("That character name is already in use locally."));
				return;
			}
		}

		FWowOfflineCharacterRecord Character;
		Character.Guid = FWowOfflineCharacterStore::AllocateGuid(OfflineCharacters);
		Character.Name = CleanName;
		Character.Race = Race;
		Character.Class = Class;
		Character.Gender = Gender;
		Character.Skin = Skin;
		Character.Face = Face;
		Character.HairStyle = HairStyle;
		Character.HairColor = HairColor;
		Character.FacialHair = FacialHair;
		FWowOfflineCharacterStore::ApplyStartLocation(Character);
		OfflineCharacters.Add(Character);
		if (!FWowOfflineCharacterStore::Save(OfflineCharacters))
		{
			OfflineCharacters.Pop();
			SetStatusText(TEXT("Could not save the local character."));
			return;
		}

		RefreshOfflineCharacterInfos();
		HandleCharacterSelected(Character.Guid);
		return;
	}

    GWowLastCreateName = Name;
    ConnectionManager->CreateCharacter(Name, Race, Class, Gender, Skin, Face, HairStyle, HairColor, FacialHair);
}

void AWowLoginController::HandleBackToCharSelect()
{
	ShowCharacterSelectScreen(bOfflineFlow
		? OfflineCharacterInfos
		: ConnectionManager->GetCachedCharacters());
}

void AWowLoginController::HandleBackFromCharacterSelect()
{
    CloseGlueDialog();
    if (!bOfflineFlow && ConnectionManager) ConnectionManager->Disconnect();
    ActiveRealmIndex = PendingRealmIndex = PendingRealmId = INDEX_NONE;
    bReturningFromLogout = false;
    bAutoEnterAfterCreate = false;
    SelectedCharacterGuid = 0;
    bHasSelectedCharacterInfo = false;
    ShowLoginScreen();
}

void AWowLoginController::HandleChangeRealm()
{
    if (!bOfflineFlow && ConnectionManager)
        ShowRealmSelectScreen(ConnectionManager->GetCachedRealms());
}

void AWowLoginController::CloseGlueDialog()
{
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ConnectionTimeoutHandle);
    if (GlueDialog.IsValid() && GEngine && GEngine->GameViewport)
        FWowUiSurfaces::Remove(GlueDialog.ToSharedRef());
    GlueDialog.Reset();
    ConnectionStatusLabel.Reset();
    ConnectionButtonLabel.Reset();
    RealmSelectWidget.Reset();
    if (CurrentWidget.IsValid()) CurrentWidget->SetEnabled(true);
    if (auto* PC = Cast<AWowGameplayController>(UGameplayStatics::GetPlayerController(this, 0)))
        ApplyWowLoginUiInputMode(PC, CurrentWidget);
}

void AWowLoginController::ShowConnectingDialog()
{
    CloseGlueDialog();
    FWowWindowStyle::LoadTextures([this](const FString& Path) {
        return LoadFirstMpqBlpTexture({Path+TEXT(".blp")}, TEXT("connection dialog"));
    });
    GlueDialog = SNew(SOverlay)
        + SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
            .BorderBackgroundColor(FLinearColor(0,0,0,.35f))]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SBox).WidthOverride(720)
            [SNew(SOverlay)
                + SOverlay::Slot().Padding(8)
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
                    .BorderBackgroundColor(FLinearColor(.012f,.012f,.015f,.97f))]
                + SOverlay::Slot()
            [SNew(SWowWindowFrame).Padding(FMargin(24,12))
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(0,4,0,20)
                    [SAssignNew(ConnectionStatusLabel,STextBlock).Text(FText::FromString(TEXT("Connecting...")))
                        .WrapTextAt(640).Justification(ETextJustify::Center)
                        .Font(FWowWindowStyle::TitleFont(18)).ColorAndOpacity(FWowWindowStyle::Gold())]
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [SNew(SBox).WidthOverride(260).MinDesiredHeight(30)
                        [SNew(SButton).ButtonStyle(FWowWindowStyle::Button())
                            .HAlign(HAlign_Center).OnClicked_Lambda([this]() {
                                HandleBackFromCharacterSelect(); return FReply::Handled(); })
                            [SAssignNew(ConnectionButtonLabel,STextBlock).Text(FText::FromString(TEXT("Cancel")))
                                .Font(FWowWindowStyle::TitleFont(14))]]]]]]];
    if (GetWorld()) GetWorld()->GetTimerManager().SetTimer(ConnectionTimeoutHandle,
        FTimerDelegate::CreateWeakLambda(this,[this]() {
            if (GlueDialog.IsValid()) OnError(TEXT("Connection timed out. Please try again."));
        }),30.f,false);
    if (CurrentWidget.IsValid()) CurrentWidget->SetEnabled(false);
    if (GEngine && GEngine->GameViewport)
        FWowUiSurfaces::Add(GlueDialog.ToSharedRef(), EWowUiZone::Glue, 110);
    if (auto* PC = Cast<AWowGameplayController>(UGameplayStatics::GetPlayerController(this,0)))
        ApplyWowLoginUiInputMode(PC, GlueDialog);
}

void AWowLoginController::HandleCharacterHighlighted(uint8 Race, uint8 Gender)
{
    if (CharacterPreview)
    {
        FWowCharacterInfo Info;
        if (CharCreateWidget.IsValid()) Info = CharCreateWidget->GetPreviewCharacter();
        else { Info.Race = Race; Info.Gender = Gender; }
        PopulateOfflineStarterEquipment(Info);
        CharacterPreview->ShowCharacter(Info);
    }
}

void AWowLoginController::HandleCharacterInfoHighlighted(const FWowCharacterInfo& CharacterInfo)
{
    if (CharacterPreview)
    {
        CharacterPreview->ShowCharacter(CharacterInfo);
    }
}

void AWowLoginController::HandleCharacterPreviewRotated(float YawDeltaDegrees)
{
    if (CharacterPreview)
    {
        CharacterPreview->RotateCharacter(YawDeltaDegrees);
    }
}

void AWowLoginController::InitializeCinematics()
{
    if(!LoginWidget.IsValid() || !WorldManager || !WorldManager->HasExternalData())return;
    bLoginBackgroundInitialized=true;
    // Use local glue models, not loading-screen paintings or intro movies.
    // Artwork initialization selects the available theme and invokes its handler.
    if (!LoginOriginalData.IsValid())
    {
        LoginOriginalData = MakeShared<FMpqManager>();
        if (!LoginOriginalData->Initialize(WorldManager->GetMpqManager()->GetDataPath(), {}, EWowAssetProfile::OriginalWotlk335))
            LoginOriginalData.Reset();
        else
        {
            LoginOriginalCache = MakeShared<FWowAssetCache>();
            LoginOriginalCache->SetRuntimeContext(this);
        }
    }
    LoginWidget->InitializeArtwork(!LoginWidget->UsesModdedArtwork() && LoginOriginalData.IsValid() ? LoginOriginalData.Get() : WorldManager->GetMpqManager());
}

void AWowLoginController::TryInitializeLoginBackgroundWhenReady()
{
    if (!LoginWidget.IsValid())
    {
        return;
    }

    if (!WorldManager || !WorldManager->HasExternalData())
    {
        return;
    }

	if (!bLoginMusicStarted)
	{
		if (AWowAudioManager* AudioManager = GetOrCreateAudioManager())
		{
			AudioManager->PlayLoginMusic();
			bLoginMusicStarted = true;
		}
	}

	if (!bLoginBackgroundInitialized)
	{
		InitializeCinematics();
	}
}

void AWowLoginController::UpdateStartupHandoff()
{
    if (bStartupHandoffComplete || StartupWaitStarted<=0) return;
    const double Now=FPlatformTime::Seconds();
    // MPQ artwork and the world scene must be initialized before telling the
    // external launcher to reveal the window. Missing data retains a bounded fallback.
    const bool bReady=bLoginBackgroundInitialized && LoginWidget.IsValid();
    if (!bReady && Now-StartupWaitStarted<30.0) return;
    if (StartupVisualsReadyAt<=0)
    {
        StartupVisualsReadyAt=Now;
        StartupVisualsReadyFrame=GFrameCounter;
        return;
    }
    // Allow animation, Slate artwork and scene proxies to submit frames, then
    // wait asynchronously for the render thread to consume those commands.
    if (Now-StartupVisualsReadyAt<1.5 || GFrameCounter<StartupVisualsReadyFrame+3) return;
    if (!bStartupFenceStarted)
    {
        StartupRenderFence.BeginFence();
        bStartupFenceStarted=true;
        return;
    }
    if (!StartupRenderFence.IsFenceComplete()) return;
    WriteSplashReadyMarker();
    bStartupHandoffComplete=true;
    UE_LOG(LogWowLogin,Log,TEXT("Startup handoff after artwork initialization and render warmup (%.2fs)"),Now-StartupWaitStarted);
}

bool AWowLoginController::TryApplyMpqLoginBackground(EWowExpansion Expansion)
{
    if (!LoginWidget.IsValid())
    {
        return false;
    }

    TArray<FString> CandidatePaths;
    switch (Expansion)
    {
    case EWowExpansion::Classic:
        CandidatePaths = {
            TEXT("Interface\\Glues\\LoadingScreens\\LoadScreenEasternKingdoms.blp"),
            TEXT("Interface\\Glues\\LoadingScreens\\LoadScreenKalimdor.blp")
        };
        break;
    case EWowExpansion::BurningCrusade:
        CandidatePaths = {
            TEXT("Interface\\Glues\\LoadingScreens\\LoadScreenOutland.blp"),
            TEXT("Interface\\Glues\\LoadingScreens\\LoadScreenHellfire.blp")
        };
        break;
    case EWowExpansion::WrathOfTheLichKing:
    default:
        CandidatePaths = {
            TEXT("Interface\\Glues\\LoadingScreens\\LoadScreenNorthrend.blp"),
            TEXT("Interface\\Glues\\LoadingScreens\\LoadScreenNorthrend"),
            TEXT("Interface\\Glues\\LoadingScreens\\LoadScreenStratholme.blp"),
            TEXT("Interface\\Glues\\LoadingScreens\\LoadScreenEasternKingdoms.blp")
        };
        break;
    }

    UTexture2D* Background = LoadFirstMpqBlpTexture(CandidatePaths, TEXT("login background"));
    LoginWidget->SetBackgroundTexture(Background);
    return Background != nullptr;
}

bool AWowLoginController::TrySpawnLoginGlueScene(EWowExpansion Expansion)
{
    if (!WorldManager || !WorldManager->HasExternalData() || LoginGlueActor)
    {
        return LoginGlueActor != nullptr;
    }

    UWorld* World = GetWorld();
    FMpqManager* Mpq = WorldManager->GetMpqManager();
    FWowAssetCache* Cache = WorldManager->GetAssetCache();
    if (LoginOriginalData.IsValid() && LoginWidget.IsValid() && !LoginWidget->UsesModdedArtwork())
    {
        Mpq = LoginOriginalData.Get();
        Cache = LoginOriginalCache.Get();
    }
    return SpawnLoginGlueScene(Expansion, Mpq, Cache);
}

// r616 (Stefan, HD login vs its own client: "a lot more details, contrast that brings out more black and
// sharper looks", "darker skies overshadowing"): the original client blends its see-through glue layers in
// gamma space (D3D9, sRGB values as stored); UE blends in linear light. Stacked dark cards came out washed
// out and the additive glows weak (measured on the two screenshots: half the contrast). The login backdrop's
// textures are therefore sampled as stored (no sRGB decode), blended like the original, and one
// post-process pass turns the result into linear light (x^2.2) before the usual output.
namespace WowLoginGamma
{
    TStrongObjectPtr<UMaterialInterface> GPostMaterial;
    UMaterialInterface* GetPostMaterial()
    {
        if (GPostMaterial.IsValid()) return GPostMaterial.Get();
        // r643 (Stefan: "more contrast that brings out more black"): v1 decoded with pow(c, 2.2) while the
        // output is encoded with the exact sRGB curve, which lifted the darkest tones (the sRGB curve is a
        // straight line near black). v2 decodes with the exact sRGB curve, so the screen gets the very
        // values the original client blends, blacks included.
        const FString Name = TEXT("M_WowGlueGammaPost_v2");
        UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
        if (!Material)
        {
            Material = NewObject<UMaterial>(GetTransientPackage());
            Material->MaterialDomain = MD_PostProcess;
            Material->BlendableLocation = BL_SceneColorBeforeBloom;
            Material->SetShadingModel(MSM_Unlit);
            auto* Scene = NewObject<UMaterialExpressionSceneTexture>(Material);
            Scene->SceneTextureId = PPI_PostProcessInput0;
            Material->GetExpressionCollection().AddExpression(Scene);
            auto* Decode = NewObject<UMaterialExpressionCustom>(Material);
            Decode->OutputType = CMOT_Float3;
            Decode->Inputs.Reset();
            Decode->Code = TEXT("float3 c = saturate(C.rgb);\n"
                "return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));");
            FCustomInput In; In.InputName = TEXT("C"); In.Input.Connect(0, Scene); Decode->Inputs.Add(In);
            Material->GetExpressionCollection().AddExpression(Decode);
            Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Decode);
            Material->PreEditChange(nullptr);
            Material->PostEditChange();
            Material = WowCookedMaterial::Save(Material, Name);
        }
#endif
        if (Material) GPostMaterial.Reset(Material);
        return Material;
    }

    // Native resolution while the backdrop shows: a temporal upscaler softened the painted cards.
    struct FSavedCVar { FString Name; FString Value; };
    TArray<FSavedCVar> GSaved;
    void Override(const TCHAR* Name, const TCHAR* Value)
    {
        if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
        {
            GSaved.Add({ Name, Var->GetString() });
            Var->Set(Value, ECVF_SetByCode);
        }
    }
    // r645 (Stefan: the login sky washed out next to the original): no fog or atmosphere over the glue scene,
    // like the original client's glue screens. Restored with the console variables.
    bool GFogHidden = false, GSavedFog = true, GSavedAtmosphere = true;
    void EnterNative()
    {
        if (GEngine && GEngine->GameViewport && !GFogHidden)
        {
            FEngineShowFlags& Flags = GEngine->GameViewport->EngineShowFlags;
            GSavedFog = Flags.Fog; GSavedAtmosphere = Flags.Atmosphere;
            Flags.SetFog(false); Flags.SetAtmosphere(false);
            GFogHidden = true;
        }
        if (GSaved.Num()) return;
        Override(TEXT("r.FidelityFX.FSR.Enabled"), TEXT("0"));
        Override(TEXT("r.NGX.DLSS.Enable"), TEXT("0"));
        // r630 (Stefan: "the lines by the floating mountain rocks should be smoothed out"): the backdrop is
        // rendered at twice the resolution and scaled down (supersampling), which smooths the rock and
        // card edges without the temporal blur. The scene is one static model, so this costs little.
        Override(TEXT("r.ScreenPercentage"), TEXT("200"));
        Override(TEXT("r.LocalExposure"), TEXT("0")); // r646
        // r643 (Stefan: "some more sharpness to match the original HD login screen"): a light sharpen in the
        // tonemapper pass on top of the supersampled image.
        Override(TEXT("r.Tonemapper.Sharpen"), TEXT("0.6"));
        Override(TEXT("r.TemporalAA.Upsampling"), TEXT("0"));
        Override(TEXT("r.AntiAliasingMethod"), TEXT("1"));
    }
    void LeaveNative()
    {
        if (GFogHidden && GEngine && GEngine->GameViewport)
        {
            FEngineShowFlags& Flags = GEngine->GameViewport->EngineShowFlags;
            Flags.SetFog(GSavedFog); Flags.SetAtmosphere(GSavedAtmosphere);
        }
        GFogHidden = false;
        for (int32 I = GSaved.Num() - 1; I >= 0; --I)
            if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*GSaved[I].Name))
                Var->Set(*GSaved[I].Value, ECVF_SetByCode);
        GSaved.Reset();
    }
}

void WowPrepareLoginGammaMaterial() { WowLoginGamma::GetPostMaterial(); }

bool AWowLoginController::SpawnLoginGlueScene(EWowExpansion Expansion, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    UWorld* World = GetWorld();
    if (!World || !Mpq || !Cache) return false;
    const TCHAR* GlueModel = WowLoginTheme::Model(Expansion);
    if (!Mpq->FileExists(GlueModel))
    {
        UE_LOG(LogWowLogin, Warning, TEXT("Login glue scene model missing from MPQ: %s"), GlueModel);
        return false;
    }

    const FVector SceneOrigin(0.0f, 0.0f, -50000.0f);
    LoginGlueActor = FWowCharacterBuilder::SpawnM2Model(
        World, Mpq, Cache, GlueModel, SceneOrigin, FRotator(0.0f, 180.0f, 0.0f), 1.0f);

    if (!LoginGlueActor)
    {
        UE_LOG(LogWowLogin, Warning, TEXT("Failed to spawn Login glue scene M2; using static fallback"));
        return false;
    }

#if WITH_EDITOR
    LoginGlueActor->SetActorLabel(TEXT("LoginGlueScene"));
#endif
    // SpawnM2Model installs its root after SpawnActor; restore the requested scene transform.
    LoginGlueActor->SetActorTransform(FTransform(FRotator(0.f,180.f,0.f),SceneOrigin));
    TArray<UPrimitiveComponent*> Primitives;
    LoginGlueActor->GetComponents(Primitives,true);
    for(auto* Primitive:Primitives)
    {
        Primitive->SetLightingChannels(false,true,false);
        Primitive->SetCastShadow(false);
        Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    FVector BoundsOrigin = FVector::ZeroVector;
    FVector BoundsExtent = FVector(1000.0f);
    LoginGlueActor->GetActorBounds(false, BoundsOrigin, BoundsExtent);

    const FWowM2StaticCamera GlueCamera = TryReadFirstM2StaticCamera(Mpq, GlueModel);
    FVector CameraLocation;
    FVector CameraTarget;
    float CameraFovDegrees = 36.0f;
    if (GlueCamera.bIsValid)
    {
        CameraLocation = TransformM2CameraPointForSpawnedMesh(LoginGlueActor, GlueCamera.Position);
        CameraTarget = TransformM2CameraPointForSpawnedMesh(LoginGlueActor, GlueCamera.Target);
        GWowLoginGlueRawFov = GlueCamera.FieldOfViewRadians;
        GWowLoginGluePortraitCamera = GlueCamera.bPortraitCamera;
        CameraFovDegrees = GlueSceneHorizontalFovDegrees(GlueCamera.FieldOfViewRadians, 16.f / 9.f, GlueCamera.bPortraitCamera);
    }
    else
    {
        GWowLoginGlueRawFov = 0.f;
        CameraTarget = SceneOrigin + FVector(0.0f, 0.0f, 200.0f);
        CameraLocation = SceneOrigin + FVector(-1600.0f, -4200.0f, 520.0f);
    }
    FRotator CameraRotation = (CameraTarget - CameraLocation).Rotation();
    LoginBackgroundPlate = FVector::ZeroVector;
    // Some custom realms replace the glue scene with a rectangular image plane.
    // Fit only camera-aligned plates; authored volumetric scenes keep their camera.
    const FRotationMatrix Basis(CameraRotation);
    const float Depth = FVector::DotProduct(BoundsExtent, Basis.GetUnitAxis(EAxis::X).GetAbs());
    const float HalfWidth = FVector::DotProduct(BoundsExtent, Basis.GetUnitAxis(EAxis::Y).GetAbs());
    const float HalfHeight = FVector::DotProduct(BoundsExtent, Basis.GetUnitAxis(EAxis::Z).GetAbs());
    if (HalfWidth > 1.f && HalfHeight > 1.f && Depth < FMath::Min(HalfWidth, HalfHeight) * .001f)
    {
        const float Distance = FVector::DotProduct(BoundsOrigin - CameraLocation, Basis.GetUnitAxis(EAxis::X));
        if (Distance > 1.f)
        {
            CameraLocation = BoundsOrigin - Basis.GetUnitAxis(EAxis::X) * Distance;
            LoginBackgroundPlate = FVector(HalfWidth, HalfHeight, Distance);
        }
    }

    FActorSpawnParameters CameraParams;
    // Destroyed actors retain their names until GC; let the world allocate a unique name.
    CameraParams.Name = NAME_None;
    LoginGlueCamera = World->SpawnActor<ACameraActor>(
        ACameraActor::StaticClass(),
        CameraLocation,
        CameraRotation,
        CameraParams);

    if (LoginGlueCamera)
    {
        auto* Camera=LoginGlueCamera->GetCameraComponent();
        Camera->SetFieldOfView(CameraFovDegrees);
        UpdateLoginBackgroundFit();
        auto& PP=Camera->PostProcessSettings;
        PP.bOverride_DepthOfFieldScale=true;PP.DepthOfFieldScale=0.f;
        PP.bOverride_MotionBlurAmount=true;PP.MotionBlurAmount=0.f;
        PP.bOverride_AutoExposureMinBrightness=true;PP.AutoExposureMinBrightness=1.f;
        PP.bOverride_AutoExposureMaxBrightness=true;PP.AutoExposureMaxBrightness=1.f;
        // r384 (Stefan's native screenshot): the original client shows the glue textures as they
        // are. No filmic curve, gamut expansion, bloom, vignette or lens flare, and a fixed exposure
        // of exactly 1 (manual EV100 0 is 1/1.2; +log2(1.2) compensates).
        PP.bOverride_AutoExposureMethod=true;PP.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
        PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true;PP.AutoExposureApplyPhysicalCameraExposure=0;
        PP.bOverride_AutoExposureBias=true;PP.AutoExposureBias=FMath::Log2(1.2f);
        PP.bOverride_ToneCurveAmount=true;PP.ToneCurveAmount=0.f;
        // r646 (Stefan: "ours are still brighter"): measured against his original screenshot, our darks
        // were lifted (pillars 0.11 -> 0.16) and the bright sky pulled down (0.67 -> 0.56) - the shape of
        // UE's local exposure, which compresses contrast. The offline reference render without it matches
        // the original (pillars 0.107 vs 0.108). Neutral local exposure on the glue scene.
        PP.bOverride_LocalExposureHighlightContrastScale=true;PP.LocalExposureHighlightContrastScale=1.f;
        PP.bOverride_LocalExposureShadowContrastScale=true;PP.LocalExposureShadowContrastScale=1.f;
        PP.bOverride_LocalExposureDetailStrength=true;PP.LocalExposureDetailStrength=1.f;
        PP.bOverride_ExpandGamut=true;PP.ExpandGamut=0.f;
        PP.bOverride_BlueCorrection=true;PP.BlueCorrection=0.f;
        PP.bOverride_BloomIntensity=true;PP.BloomIntensity=0.f;
        PP.bOverride_VignetteIntensity=true;PP.VignetteIntensity=0.f;
        PP.bOverride_LensFlareIntensity=true;PP.LensFlareIntensity=0.f;
        // r616: the login backdrop is blended in gamma space like the original client (see WowLoginGamma).
        if (LoginGlueActor && FString(GlueModel).Contains(TEXT("UI_MainMenu")))
            if (UMaterialInterface* GammaPost = WowLoginGamma::GetPostMaterial())
            {
                TSet<UTexture2D*> Stored;
                TArray<UPrimitiveComponent*> GluePrims;
                LoginGlueActor->GetComponents(GluePrims, true);
                for (UPrimitiveComponent* Prim : GluePrims)
                    for (int32 Slot = 0; Prim && Slot < Prim->GetNumMaterials(); ++Slot)
                        if (UMaterialInterface* Mat = Prim->GetMaterial(Slot))
                            for (const TCHAR* Param : { TEXT("BaseTexture"), TEXT("Layer2Texture") })
                            {
                                UTexture* Texture = nullptr;
                                if (Mat->GetTextureParameterValue(FMaterialParameterInfo(Param), Texture))
                                    if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture); Texture2D && Texture2D->SRGB && !Stored.Contains(Texture2D))
                                    {
                                        Stored.Add(Texture2D);
                                        Texture2D->SRGB = false;
                                        Texture2D->UpdateResource();
                                    }
                            }
                PP.AddBlendable(GammaPost, 1.0f);
                WowLoginGamma::EnterNative();
                UE_LOG(LogWowLogin, Log, TEXT("Login backdrop: gamma-space blending (%d textures), native resolution"), Stored.Num());
            }
        AWowSkyManager::SetLoginGlueSceneActive(true);
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            PC->SetViewTarget(LoginGlueCamera);
        }
    }

    FActorSpawnParameters LightParams;
    LightParams.Name = NAME_None;
    LoginGlueLight = World->SpawnActor<ADirectionalLight>(
        ADirectionalLight::StaticClass(),
        FVector::ZeroVector,
        FRotator(-30.0f, 35.0f, 0.0f),
        LightParams);
    if (UDirectionalLightComponent* LightComponent = LoginGlueLight
        ? Cast<UDirectionalLightComponent>(LoginGlueLight->GetLightComponent())
        : nullptr)
    {
        LightComponent->SetIntensity(3.0f);
        LightComponent->SetLightColor(FLinearColor::White);
        LightComponent->SetLightingChannels(false,true,false);
        LightComponent->SetForwardShadingPriority(0);
    }

    if (LoginWidget.IsValid())
    {
        LoginWidget->SetWorldSceneBackgroundActive(true);
    }

    UE_LOG(LogWowLogin, Log, TEXT("Spawned MPQ-backed 3D login glue scene (bounds origin=%s extent=%s camera=%s target=%s rot=%s fov=%.1f source=%s)"),
        *BoundsOrigin.ToCompactString(),
        *BoundsExtent.ToCompactString(),
        *CameraLocation.ToCompactString(),
        *CameraTarget.ToCompactString(),
        *CameraRotation.ToCompactString(),
        CameraFovDegrees,
        GlueCamera.bIsValid ? TEXT("m2") : TEXT("fallback"));
    return true;
}

void AWowLoginController::UpdateLoginBackgroundFit()
{
    if (!LoginGlueCamera) return;
    // r670 (Stefan: "after a while the HD login screen vanishes" - the scene seen from far behind with a wide lens):
    // on the login screen the player's pawn is not on any ground (terrain loading is deferred) and keeps falling (the
    // log's "World ambient capture" position went -6656 -> -249882 cm in a minute). A few minutes later it passes the
    // world's kill height, is destroyed and respawned, and the view target goes back to the new pawn - the default
    // camera, no longer the glue scene's camera. While the glue scene is up its camera stays the view.
    if (LoginGlueActor)
        if (APlayerController* ViewPC = UGameplayStatics::GetPlayerController(this, 0))
        {
            AActor* ViewTarget = ViewPC->GetViewTarget();
            if (ViewTarget != LoginGlueCamera && (!ViewTarget || ViewTarget == ViewPC || ViewTarget->IsA<APawn>()))
            {
                UE_LOG(LogWowLogin, Log, TEXT("Login glue camera restored as the view (view target was %s)"), *GetNameSafe(ViewTarget));
                ViewPC->SetViewTarget(LoginGlueCamera);
            }
            // Keep the pawn from falling out of the world while the login screen is up.
            if (ACharacter* LoginPawn = Cast<ACharacter>(ViewPC->GetPawn()))
                if (UCharacterMovementComponent* LoginMovement = LoginPawn->GetCharacterMovement(); LoginMovement && LoginMovement->IsFalling()
                    && LoginPawn->GetActorLocation().Z < -100000.0)
                {
                    LoginMovement->StopMovementImmediately();
                    LoginPawn->SetActorLocation(FVector(LoginPawn->GetActorLocation().X, LoginPawn->GetActorLocation().Y, -100000.0));
                }
        }
    int32 Width = 0, Height = 0;
    if (auto* PC = UGameplayStatics::GetPlayerController(this, 0)) PC->GetViewportSize(Width, Height);
    const float Aspect = Width > 0 && Height > 0 ? float(Width) / Height : 16.f / 9.f;
    auto* Camera = LoginGlueCamera->GetCameraComponent();
    if (LoginBackgroundPlate.Z <= 0.f)
    {
        // r383: 3D glue scene: the original client's angle for the current screen shape.
        if (GWowLoginGlueRawFov > 0.f && Camera)
        {
            Camera->bConstrainAspectRatio = false;
            Camera->bOverrideAspectRatioAxisConstraint = true;
            Camera->AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV;
            const float Fov = GlueSceneHorizontalFovDegrees(GWowLoginGlueRawFov, Aspect, GWowLoginGluePortraitCamera);
            if (!FMath::IsNearlyEqual(Camera->FieldOfView, Fov, 0.01f)) Camera->SetFieldOfView(Fov);
        }
        return;
    }
    Camera->bConstrainAspectRatio = false;
    Camera->bOverrideAspectRatioAxisConstraint = true;
    Camera->AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV;
    Camera->SetFieldOfView(WowLoginTheme::BackgroundCoverFov(
        LoginBackgroundPlate.X, LoginBackgroundPlate.Y, LoginBackgroundPlate.Z, Aspect));
}

void AWowLoginController::ClearLoginGlueScene()
{
    WowLoginGamma::LeaveNative(); // r616
    AWowSkyManager::SetLoginGlueSceneActive(false); // r384
    LoginBackgroundPlate = FVector::ZeroVector;
    if(LoginWidget.IsValid())LoginWidget->SetWorldSceneBackgroundActive(false);
    if (LoginGlueActor)
    {
        LoginGlueActor->Destroy();
        LoginGlueActor = nullptr;
    }
    if (LoginGlueCamera)
    {
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (PC->GetViewTarget() == LoginGlueCamera && PC->GetPawn())
			{
				PC->SetViewTarget(PC->GetPawn());
			}
		}
        LoginGlueCamera->Destroy();
        LoginGlueCamera = nullptr;
    }
    if (LoginGlueLight)
    {
        LoginGlueLight->Destroy();
        LoginGlueLight = nullptr;
    }
}

UTexture2D* AWowLoginController::LoadFirstMpqBlpTexture(const TArray<FString>& Paths, const FString& LogContext) const
{
    if (!WorldManager || !WorldManager->GetMpqManager())
    {
        return nullptr;
    }

    FMpqManager* Mpq = WorldManager->GetMpqManager();
    for (const FString& Path : Paths)
    {
        TArray<uint8> BlpData;
        if (!Mpq->ReadFile(Path, BlpData))
        {
            continue;
        }

        FBlpTexture Parsed = FBlpParser::Parse(BlpData);
        if (!Parsed.bIsValid)
        {
            UE_LOG(LogWowLogin, Warning, TEXT("Found %s BLP but could not parse it: %s"), *LogContext, *Path);
            continue;
        }

        if (UTexture2D* Texture = FWowTextureFactory::CreateTexture(Parsed, Path))
        {
            UE_LOG(LogWowLogin, Log, TEXT("Loaded MPQ %s texture: %s"), *LogContext, *Path);
            return Texture;
        }
    }

    UE_LOG(LogWowLogin, Warning, TEXT("No MPQ %s texture found"), *LogContext);
    return nullptr;
}

void AWowLoginController::LogWotlkGlueSceneAvailability(FMpqManager* Mpq) const
{
    if (!Mpq)
    {
        return;
    }

    static const TCHAR* WotlkGlueCandidates[] = {
        TEXT("Interface\\Glues\\Models\\UI_MainMenu_Northrend\\UI_MainMenu_Northrend.m2"),
        TEXT("Interface\\Glues\\Models\\UI_MainMenu_Northrend\\UI_MainMenu_Northrend.M2"),
        TEXT("Interface\\Glues\\Models\\UI_MainMenu_Northrend\\UI_MainMenu_Northrend00.skin"),
        TEXT("Interface\\Glues\\Models\\UI_MainMenu_Northrend\\UI_MainMenu_Northrend00.SKIN")
    };

    for (const TCHAR* Candidate : WotlkGlueCandidates)
    {
        if (Mpq->FileExists(Candidate))
        {
            UE_LOG(LogWowLogin, Log, TEXT("Login glue scene asset available in MPQ: %s"), Candidate);
        }
    }
}

void AWowLoginController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    TryInitializeLoginBackgroundWhenReady();
    UpdateLoginBackgroundFit();
    UpdateStartupHandoff();
    if (PendingDataSwitchFrames > 0 && --PendingDataSwitchFrames == 0)
    {
        RunServerDataSwitch();
    }

    if (bWaitingForInitialLoad)
    {
        UpdateLoadingProgress(DeltaTime);
    }
    else if (bPendingGroundSnap)
    {
        TryFinalizeGroundSnap();
    }
}

// ShowLoadingScreen() removed â€” use ShowLoadingScreen(uint32 MapId) instead

void AWowLoginController::UpdateLoadingProgress(float DeltaTime)
{
    if (!WorldManager) return;

    AWowGameplayController* GPC = Cast<AWowGameplayController>(
        UGameplayStatics::GetPlayerController(this, 0));

    // Wait for spawn position from server
    if (GPC && GPC->bHasDeferredSpawn && !bHasPendingSpawn)
    {
        bHasPendingSpawn = true;
        InitialLoadCompleteSettleTime = 0.0f;
        WorldEntryWarmupElapsed = 0.0f;
        WorldEntryWarmupStartedAtSeconds = 0.0;
        WorldEntryWarmupStartPass = 0;
        LastWorldEntryObjectRevision = 0;
        LastWorldEntryEntityRevision = 0;
        bEntryPresentationPrepared = false;
		bDeferredSpawnAppliedForEntry = false;
		GroundValidationStableFrames = 0;
        bInitialLoadSettleLogged = false;
        PendingSpawnPosition = GPC->DeferredSpawnPos;
        PendingSpawnOrientation = GPC->DeferredSpawnOrientation;

        // The hidden pawn and camera have not reached the server position yet. Keep
        // every streaming subsystem focused on the destination during warmup.
        WorldManager->BeginWorldEntryWarmup(PendingSpawnPosition);
        WorldManager->LoadTilesAroundPosition(PendingSpawnPosition, 2);

        if (LoadingScreenWidget.IsValid())
        {
            LoadingScreenWidget->SetProgress(0.12f, TEXT("Loading terrain..."));
        }
        UE_LOG(LogWowLogin, Log, TEXT("Loading terrain around spawn position"));
    }
    // r734 (Stefan 23.10, TormentWoW: the loading screen never went away): the server moved the character (a map
    // transfer during login, see AWowGameplayController::OnMapTransfer) before the entry finished. Follow it: restart
    // the warmup at the new position instead of waiting for objects around, and spawning at, the old one.
    else if (GPC && GPC->bHasDeferredSpawn && bHasPendingSpawn && !bDeferredSpawnAppliedForEntry
        && !GPC->DeferredSpawnPos.Equals(PendingSpawnPosition, 1.0))
    {
        PendingSpawnPosition = GPC->DeferredSpawnPos;
        PendingSpawnOrientation = GPC->DeferredSpawnOrientation;
        InitialLoadCompleteSettleTime = 0.0f;
        WorldEntryWarmupElapsed = 0.0f;
        WorldEntryWarmupStartedAtSeconds = 0.0;
        WorldEntryWarmupStartPass = WorldManager->GetObjectStreamingPassCount();
        LastWorldEntryObjectRevision = 0;
        LastWorldEntryEntityRevision = 0;
        GroundValidationStableFrames = 0;
        bInitialLoadSettleLogged = false;
        WorldManager->BeginWorldEntryWarmup(PendingSpawnPosition);
        UE_LOG(LogWowLogin, Log, TEXT("World entry follows a map transfer: warmup restarted at the new position"));
    }

    if (!bHasPendingSpawn)
    {
        if (LoadingScreenWidget.IsValid())
        {
            LoadingScreenWidget->SetProgress(0.08f, TEXT("Waiting for world position..."));
        }
        return;
    }

    const int32 Queued = WorldManager->GetInitialTilesQueued();
    const int32 Loaded = WorldManager->GetInitialTilesLoaded();
    const float TerrainProgress = Queued > 0
        ? FMath::Clamp(static_cast<float>(Loaded) / static_cast<float>(Queued), 0.0f, 1.0f)
        : 0.0f;

    if (!WorldManager->IsInitialLoadComplete())
    {
        if (LoadingScreenWidget.IsValid())
        {
            LoadingScreenWidget->SetProgress(
                0.12f + TerrainProgress * 0.60f,
                FString::Printf(TEXT("Loading terrain and collision... (%d/%d tiles)"), Loaded, Queued));
        }
        return;
    }

    // Build the local character and gameplay UI while the loading screen still
    // covers the viewport. These operations otherwise create a noticeable hitch
    // immediately after the player becomes visible.
    if (!bEntryPresentationPrepared)
    {
        if (!GPC)
        {
            return;
        }

        // Glue maps intentionally have no PlayerStart, so GameMode cannot
        // create the gameplay pawn during initial map startup.  Create it only
        // after the destination terrain is resident, keep it hidden/frozen,
        // and let the existing stable-ground handoff reveal it later.  Without
        // this, FinalizeWorldEntry returned forever on a null pawn while the
        // loading screen remained at 100 percent.
        if (!GPC->GetPawn())
        {
            UWorld* World = GetWorld();
            if (!World)
            {
                return;
            }

            FActorSpawnParameters PawnSpawnParams;
            PawnSpawnParams.SpawnCollisionHandlingOverride =
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const FRotator SpawnRotation(
                0.0f,
                FWowCoordinate::WowOrientationToUEYaw(PendingSpawnOrientation),
                0.0f);
            AWowPlayerCharacter* SpawnedPawn = World->SpawnActor<AWowPlayerCharacter>(
                AWowPlayerCharacter::StaticClass(),
                PendingSpawnPosition,
                SpawnRotation,
                PawnSpawnParams);
            if (!SpawnedPawn)
            {
                UE_LOG(LogWowLogin, Error,
                    TEXT("Could not create the gameplay pawn for world entry"));
                return;
            }

            GPC->Possess(SpawnedPawn);
            SpawnedPawn->SetActorHiddenInGame(true);
            SuspendPawnUntilGroundReady(SpawnedPawn);
            UE_LOG(LogWowLogin, Log,
                TEXT("Created hidden gameplay pawn after destination terrain became resident"));
        }

        if (ConnectionManager && SelectedCharacterGuid != 0)
        {
            ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid = static_cast<uint64>(SelectedCharacterGuid);
        }

        if (bHasSelectedCharacterInfo)
        {
            GPC->SetupLocalPlayerCharacterModelFromCharacterInfo(SelectedCharacterInfo);
        }
        else
        {
            GPC->EnsureVisibleLocalPlayerCharacterModel();
        }

        if (APawn* Pawn = GPC->GetPawn())
        {
            Pawn->SetActorHiddenInGame(true);
        }

        GPC->EnsureGameplayHudCreated();
        GPC->SetGameplayHudVisible(false);

        bEntryPresentationPrepared = true;
        WorldEntryWarmupElapsed = 0.0f;
        WorldEntryWarmupStartedAtSeconds = FPlatformTime::Seconds();
        InitialLoadCompleteSettleTime = 0.0f;
        WorldEntryWarmupStartPass = WorldManager->GetObjectStreamingPassCount();
        LastWorldEntryObjectRevision = WorldManager->GetObjectStreamingRevision();
        LastWorldEntryEntityRevision = GPC->GetEntityModelStreamingRevision();

        if (LoadingScreenWidget.IsValid())
        {
            LoadingScreenWidget->SetProgress(0.76f, TEXT("Preparing character and interface..."));
        }
        UE_LOG(LogWowLogin, Log, TEXT("Terrain ready; character and gameplay UI prepared behind loading screen"));
        return;
    }

    // Once the deferred spawn has been applied, world-object warmup is over.
    // Subsequent ticks belong exclusively to stable-ground validation; do not
    // rerun the warmup timeout branch or overwrite its loading-screen status.
    if (bDeferredSpawnAppliedForEntry)
    {
        FinalizeWorldEntry();
        return;
    }

    if (WorldEntryWarmupStartedAtSeconds <= 0.0)
    {
        WorldEntryWarmupStartedAtSeconds = FPlatformTime::Seconds();
    }
    WorldEntryWarmupElapsed = static_cast<float>(
        FPlatformTime::Seconds() - WorldEntryWarmupStartedAtSeconds);
    const uint64 CurrentPass = WorldManager->GetObjectStreamingPassCount();
    const uint64 CurrentRevision = WorldManager->GetObjectStreamingRevision();
    const uint64 CurrentEntityRevision = GPC ? GPC->GetEntityModelStreamingRevision() : 0;
    const uint64 CompletedPasses = CurrentPass >= WorldEntryWarmupStartPass
        ? CurrentPass - WorldEntryWarmupStartPass
        : 0;

    // r734: a frame over 50 ms restarted the settle timer, so on a busy login (addon traffic, 10-20 fps behind the
    // loading screen) the world never counted as settled and every entry waited for the full 45 s timeout.
    // Only real hitches (over 150 ms) restart it now.
    if (CurrentRevision != LastWorldEntryObjectRevision
        || CurrentEntityRevision != LastWorldEntryEntityRevision || DeltaTime > 0.15f)
    {
        LastWorldEntryObjectRevision = CurrentRevision;
        LastWorldEntryEntityRevision = CurrentEntityRevision;
        InitialLoadCompleteSettleTime = 0.0f;
    }
    else if (CompletedPasses > 0)
    {
        InitialLoadCompleteSettleTime += DeltaTime;
    }

    constexpr uint64 MinimumObjectStreamingPasses = 2;
    const float PassProgress = FMath::Clamp(
        static_cast<float>(CompletedPasses) / static_cast<float>(MinimumObjectStreamingPasses), 0.0f, 1.0f);
    const float TimeProgress = FMath::Clamp(
        InitialLoadCompleteSettleTime / InitialLoadSettleDuration, 0.0f, 1.0f);
    const float WarmupProgress = FMath::Clamp(PassProgress * 0.45f + TimeProgress * 0.55f, 0.0f, 1.0f);

    if (LoadingScreenWidget.IsValid())
    {
        const int32 ReadyEntityModels = GPC ? GPC->GetReadyPendingEntityModelSpawnCount() : 0;
        LoadingScreenWidget->SetProgress(
            0.76f + WarmupProgress * 0.22f,
            FString::Printf(TEXT("Preparing nearby world... (%d props, %d building groups, %d character models remaining)"),
                WorldManager->GetActiveDoodadCount(), WorldManager->GetActiveWmoGroupCount(), ReadyEntityModels));
    }

    const int32 ReadyEntityModels = GPC ? GPC->GetReadyPendingEntityModelSpawnCount() : 0;
    // A crowded starter area can take longer than two seconds to prepare.
    // Require the actual queue to drain, with the outer timeout as a safety limit.
    const bool bEntityQueueReady = ReadyEntityModels == 0;
    const bool bNearbyWorldStable = CompletedPasses >= MinimumObjectStreamingPasses
        && InitialLoadCompleteSettleTime >= InitialLoadSettleDuration
        && bEntityQueueReady;
    const bool bWarmupTimedOut = WorldEntryWarmupElapsed >= WorldEntryWarmupTimeout;
    if (!bNearbyWorldStable && !bWarmupTimedOut)
    {
        return;
    }

    if (bWarmupTimedOut)
    {
        bInitialLoadSettleLogged = true;
        UE_LOG(LogWowLogin, Warning,
            TEXT("World-entry object warmup reached %.1fs timeout after %llu passes; entering with bounded background streaming"),
            WorldEntryWarmupElapsed, CompletedPasses);
    }
    else if (!bInitialLoadSettleLogged)
    {
        bInitialLoadSettleLogged = true;
        UE_LOG(LogWowLogin, Log,
            TEXT("World-entry warmup complete after %llu object passes and %.2fs wall time"),
            CompletedPasses, WorldEntryWarmupElapsed);
    }

    if (LoadingScreenWidget.IsValid())
    {
        LoadingScreenWidget->SetProgress(1.0f, TEXT("Entering world..."));
    }
    FinalizeWorldEntry();
}

// r706: when the deferred spawn was applied (the server's first wave of nearby creatures and players follows it).
static double GWowEntrySpawnAppliedAt = 0.0;

void AWowLoginController::FinalizeWorldEntry()
{
    // Keep the loading overlay, pawn input and gravity locked until the terrain
    // below the actual pawn has survived several consecutive collision checks.
    AWowGameplayController* GPC = Cast<AWowGameplayController>(
        UGameplayStatics::GetPlayerController(this, 0));
    if (!GPC)
    {
		return;
    }

	APawn* Pawn = GPC->GetPawn();
	if (!Pawn)
	{
		return;
	}

	if (!bDeferredSpawnAppliedForEntry)
	{
        GPC->ApplyDeferredSpawn();
        GPC->bDeferSpawnTeleport = false;
		Pawn->SetActorHiddenInGame(true);
		SuspendPawnUntilGroundReady(Pawn);
		GPC->SetViewTarget(Pawn);
		bDeferredSpawnAppliedForEntry = true;
		GroundValidationStableFrames = 0;
		GWowEntrySpawnAppliedAt = FPlatformTime::Seconds();
		if (LoadingScreenWidget.IsValid())
		{
			LoadingScreenWidget->SetProgress(0.99f, TEXT("Verifying terrain collision..."));
		}
		UE_LOG(LogWowLogin, Log, TEXT("Deferred spawn applied behind loading screen; beginning stable-ground validation"));
		return;
	}

	const bool bTerrainResident = WorldManager && WorldManager->IsTerrainReadyAtPosition(Pawn->GetActorLocation());
	// r734: never wait forever for terrain nobody asked for. If the ground under the pawn is still not resident
	// 15 s after the spawn, request the tiles around where the pawn actually is (once per entry).
	static double GWowEntryTileRequestFor = -1.0;
	if (!bTerrainResident && WorldManager && GWowEntryTileRequestFor != GWowEntrySpawnAppliedAt
		&& FPlatformTime::Seconds() - GWowEntrySpawnAppliedAt > 15.0)
	{
		GWowEntryTileRequestFor = GWowEntrySpawnAppliedAt;
		WorldManager->LoadTilesAroundPosition(Pawn->GetActorLocation(), 2);
		UE_LOG(LogWowLogin, Warning, TEXT("World entry: no terrain under the pawn 15 s after the spawn; loading the tiles around it"));
	}
	if (!bTerrainResident || !TrySnapPawnToGround(Pawn, GroundValidationStableFrames == 0))
	{
		GroundValidationStableFrames = 0;
		SuspendPawnUntilGroundReady(Pawn);
		return;
	}

	++GroundValidationStableFrames;
	constexpr int32 RequiredStableGroundFrames = 5;
	if (GroundValidationStableFrames < RequiredStableGroundFrames)
	{
		return;
	}

	// r706 (Stefan: low FPS for the first minute after login - "if that can be put in the loading screen, or put
	// in the background"): the 18.10 log showed the server's big UPDATE_OBJECT (59 creatures and players) arrive
	// 0.24 s after the loading screen had gone - it follows the deferred spawn, which is applied only at the very
	// end - so all ~67 models (30-320 ms each) were built during play. The loading screen now stays up at least
	// 1.5 s after the spawn and until the nearby models are built (one per frame behind it), at most 12 s.
	{
		const double SinceSpawn = FPlatformTime::Seconds() - GWowEntrySpawnAppliedAt;
		const int32 ReadyModels = GPC->GetReadyPendingEntityModelSpawnCount();
		if (SinceSpawn < 12.0 && (SinceSpawn < 1.5 || ReadyModels > 0))
		{
			if (LoadingScreenWidget.IsValid())
				LoadingScreenWidget->SetProgress(0.99f, ReadyModels > 0
					? FString::Printf(TEXT("Preparing nearby characters... (%d remaining)"), ReadyModels)
					: FString(TEXT("Preparing nearby characters...")));
			return;
		}
		UE_LOG(LogWowLogin, Log, TEXT("World entry: nearby characters ready %.1f s after the spawn (%d still queued)"),
			SinceSpawn, ReadyModels);
	}

	// Finish the handoff only after collision remained valid across multiple ticks.
	bWaitingForInitialLoad = false;
	bHasPendingSpawn = false;
	bPendingGroundSnap = false;
	InitialLoadCompleteSettleTime = 0.0f;
	WorldEntryWarmupElapsed = 0.0f;
	WorldEntryWarmupStartedAtSeconds = 0.0;
	WorldEntryWarmupStartPass = 0;
	LastWorldEntryObjectRevision = 0;
	LastWorldEntryEntityRevision = 0;
	bInitialLoadSettleLogged = false;
	bDeferredSpawnAppliedForEntry = false;
	GroundValidationStableFrames = 0;

	GPC->SetSoftwareCursorSuppressed(false);
	RestorePawnGameplayMovement(Pawn);
	Pawn->SetActorHiddenInGame(false);

	UE_LOG(LogWowLogin, Log, TEXT("Player revealed on stable ground at (%.0f, %.0f, %.0f)"),
		Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y, Pawn->GetActorLocation().Z);

        // WoW-style: cursor visible, game + UI both get input
        GPC->EnsureGameplayHudCreated();
        GPC->SetGameplayHudVisible(true);
		CloseStartupGameplayPanels();
		GPC->ResetIgnoreMoveInput();
		GPC->ResetIgnoreLookInput();
        GPC->bShowMouseCursor = true;
        GPC->bEnableClickEvents = true;
        GPC->bEnableMouseOverEvents = true;


    if (WorldManager)
    {
        WorldManager->EndWorldEntryWarmup();
    }

    if (LoadingWidget.IsValid() && GEngine && GEngine->GameViewport)
    {
        FWowUiSurfaces::Remove(LoadingWidget.ToSharedRef());
        LoadingWidget.Reset();
        LoadingScreenWidget.Reset();
    }
    AWowAudioManager::SetWorldLoading(AWowAudioManager::LoadingLogin, false); // r297

    if (GEngine && bSavedOnScreenDebugState)
    {
        GEngine->bEnableOnScreenDebugMessages = bPreviousOnScreenDebugEnabled;
        GAreScreenMessagesEnabled = bPreviousGlobalScreenMessagesEnabled;
        bSavedOnScreenDebugState = false;
    }

    bEntryPresentationPrepared = false;
    FWowClientPreferences::SetGameplayRendering(true);
    // Focus only after loading teardown and rendering-mode changes have completed.
    GPC->RestoreGameplayInputAfterWorldEntry();

    UE_LOG(LogWowLogin, Log, TEXT("World entry complete â€” player is in the world"));
}

bool AWowLoginController::TrySnapPawnToGround(APawn* Pawn, bool bLogFailure)
{
    if (!Pawn)
    {
        return false;
    }

    ACharacter* Character=Cast<ACharacter>(Pawn);
    if (!Character || !GetWorld()) return false;
    const FVector Position=FWowLoginPlacement::Resolve(*GetWorld(),*Character,PendingSpawnPosition);
    Pawn->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
    if (bLogFailure) UE_LOG(LogWowLogin,Log,TEXT("Restored saved feet Z=%.2f; capsule Z=%.2f"),PendingSpawnPosition.Z,Position.Z);
    return true;
}

void AWowLoginController::SuspendPawnUntilGroundReady(APawn* Pawn)
{
    if (!Pawn)
    {
        return;
    }

    Pawn->SetActorEnableCollision(false);
    if (ACharacter* Char = Cast<ACharacter>(Pawn))
    {
        if (UCharacterMovementComponent* Movement = Char->GetCharacterMovement())
        {
            Movement->GravityScale = 0.0f;
            Movement->Velocity = FVector::ZeroVector;
            Movement->SetMovementMode(MOVE_Flying);
        }
    }
}

void AWowLoginController::RestorePawnGameplayMovement(APawn* Pawn)
{
    if (!Pawn)
    {
        return;
    }

    Pawn->SetActorEnableCollision(true);
    if (AWowPlayerCharacter* WowChar = Cast<AWowPlayerCharacter>(Pawn))
    {
        WowChar->ApplyGameplayMovementSettings(true);
        WowChar->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        return;
    }

    if (ACharacter* Char = Cast<ACharacter>(Pawn))
    {
        if (UCharacterMovementComponent* Movement = Char->GetCharacterMovement())
        {
            Movement->GravityScale = 1929.110527f / 980.0f;
            Movement->JumpZVelocity = 795.5547f;
            Movement->AirControl = 0.030f;
            Movement->FallingLateralFriction = 2.9f;
            Movement->BrakingDecelerationFalling = 1750.0f;
            Movement->Velocity = FVector::ZeroVector;
            Movement->SetMovementMode(MOVE_Walking);
        }
    }
}

void AWowLoginController::TryFinalizeGroundSnap()
{
    if (!bPendingGroundSnap)
    {
        return;
    }

    if (WorldManager && WorldManager->GetLoadedTileCount() <= 0)
    {
        return;
    }

    AWowGameplayController* GPC = Cast<AWowGameplayController>(UGameplayStatics::GetPlayerController(this, 0));
    APawn* Pawn = GPC ? GPC->GetPawn() : nullptr;
    if (!Pawn || !TrySnapPawnToGround(Pawn, false))
    {
        return;
    }

    RestorePawnGameplayMovement(Pawn);
    bPendingGroundSnap = false;
    UE_LOG(LogWowLogin, Log, TEXT("Completed deferred ground snap after terrain collision became available"));
}

void AWowLoginController::HandleExpansionChanged(uint8 ExpansionIndex)
{
    if(ExpansionIndex>=3 || !LoginWidget.IsValid())return;
    ClearLoginGlueScene();
    if(CinematicManager)CinematicManager->StopCinematic();
    LoginWidget->SetCinematicTexture(nullptr);
    LoginWidget->SetBackgroundTexture(nullptr);
    const auto Exp=static_cast<EWowExpansion>(ExpansionIndex);
    const bool Spawned=!FParse::Param(FCommandLine::Get(),TEXT("loginstatic")) && TrySpawnLoginGlueScene(Exp);
    if(!Spawned)TryApplyMpqLoginBackground(Exp);
}

void AWowLoginController::ClearCurrentScreen()
{
    AWowAudioManager::SetWorldLoading(AWowAudioManager::LoadingLogin, false); // r297
    FWowClientPreferences::SetGameplayRendering(false);
    CloseGlueDialog();
	if (LoginOptionsWidget.IsValid())
	{
		if (GEngine && GEngine->GameViewport)
		{
			FWowUiSurfaces::Remove(LoginOptionsWidget.ToSharedRef());
		}
		LoginOptionsWidget.Reset();
	}
    if (LoadingScreenWidget.IsValid())
    {
        LoadingScreenWidget->SetBackgroundImage(nullptr);
    }

    if (CurrentWidget.IsValid())
    {
        if (GEngine && GEngine->GameViewport)
        {
            FWowUiSurfaces::Remove(CurrentWidget.ToSharedRef());
        }
        CurrentWidget.Reset();
    }
    LoginWidget.Reset();
    bLoginBackgroundInitialized = false;
    RealmSelectWidget.Reset();
    CharSelectWidget.Reset();
    CharCreateWidget.Reset();
    LoadingScreenWidget.Reset();

    // Clean up character preview when leaving character select
    if (CharacterPreview)
    {
        CharacterPreview->ClearCharacter();
        CharacterPreview->Destroy();
        CharacterPreview = nullptr;
    }

    ClearLoginGlueScene();
}

void AWowLoginController::ShowLoginScreen()
{
    ClearCurrentScreen();
	FWowClientPreferences::ApplyAdvancedVideoPreferences();
    SuppressDebugMessagesForGlueScreen();
    if (AWowGameplayController* GPC = Cast<AWowGameplayController>(UGameplayStatics::GetPlayerController(this, 0)))
    {
        // Glue screens use the native cursor so its visible position and
        // Slate's hit-test position remain identical under Windows DPI scaling.
        GPC->SetSoftwareCursorSuppressed(true, true);
        GPC->bShowMouseCursor = true;
		GPC->DefaultMouseCursor = EMouseCursor::Default;
		GPC->CurrentMouseCursor = EMouseCursor::Default;
    }

    LoginWidget = SNew(SWowLoginWidget);
    LoginWidget->OnNotice.BindLambda([this](const FString& Message)
    {
        ShowConnectingDialog();
        ConnectionStatusLabel->SetText(FText::FromString(Message));
        ConnectionButtonLabel->SetText(FText::FromString(TEXT("Okay")));
        if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ConnectionTimeoutHandle);
    });
    LoginWidget->OnArtworkSourceChanged.BindLambda([this]
    {
        ClearLoginGlueScene();
        InitializeCinematics();
    });
    LoginWidget->OnSaveServer.BindUObject(this, &AWowLoginController::SaveOnlineLoginBookmark);
    LoginWidget->OnLoginSubmit.BindUObject(this, &AWowLoginController::HandleLoginSubmit);
    LoginWidget->OnExpansionChanged.BindUObject(this, &AWowLoginController::HandleExpansionChanged);
	LoginWidget->OnOptionsRequested.BindUObject(this, &AWowLoginController::ShowLoginOptions);
	LoginWidget->OnExitRequested.BindUObject(this, &AWowLoginController::HandleLoginExitRequested);
	LoginWidget->SetOfflineMode(bOfflineFlow);

    CurrentWidget = LoginWidget;

    if (GEngine && GEngine->GameViewport)
    {
        FWowUiSurfaces::Add(CurrentWidget.ToSharedRef(), EWowUiZone::Glue, 100);
    }
	if (AWowGameplayController* GPC = Cast<AWowGameplayController>(
		UGameplayStatics::GetPlayerController(this, 0)))
	{
		GPC->SetSoftwareCursorSuppressed(true, true);
		ApplyWowLoginUiInputMode(GPC, CurrentWidget);
	}

    // Prefill from saved credentials
    UWowCredentialStore* CredStore = NewObject<UWowCredentialStore>(GetTransientPackage());
    if (!bOfflineFlow && CredStore->LoadCredentials())
    {
		LoginWidget->SetSavedProfiles(CredStore->GetAllCredentials());
        FWowCredential Cred = CredStore->GetDefaultCredential();
        if (!Cred.Username.IsEmpty())
        {
            FString ServerStr = FString::Printf(TEXT("%s:%d"), *Cred.ServerAddress, Cred.AuthPort);
            LoginWidget->SetCredentials(ServerStr, Cred.Username, Cred.Password);
            UE_LOG(LogWowLogin, Log, TEXT("Prefilled credentials for %s@%s"), *Cred.Username, *ServerStr);
        }
    }

	// Music and visual glue assets share the same MPQ-readiness gate. Keeping the
	// two operations together prevents a fast data initialization from setting
	// the background-complete flag before login music has had a chance to start.
	bLoginMusicStarted = false;
	WowReportStartupProgress(4);
	TryInitializeLoginBackgroundWhenReady();
	if (StartupWaitStarted <= 0) StartupWaitStarted = FPlatformTime::Seconds();

    // Auto-submit if -autologin flag is set
    if (FParse::Param(FCommandLine::Get(), TEXT("autologin")))
    {
        UE_LOG(LogWowLogin, Log, TEXT("Autologin: auto-submitting login form"));
        LoginWidget->AutoSubmit();
    }
}

void AWowLoginController::ShowRealmSelectScreen(const TArray<FWowRealmInfo>& Realms)
{
    CloseGlueDialog();
    FWowWindowStyle::LoadTextures([this](const FString& Path) {
        return LoadFirstMpqBlpTexture({Path+TEXT(".blp")}, TEXT("realm dialog"));
    });
    RealmSelectWidget = SNew(SWowRealmSelectWidget)
        .TextureLoader([this](const FString& Path) {
            return LoadFirstMpqBlpTexture({Path+TEXT(".blp")}, TEXT("realm list"));
        });
    RealmSelectWidget->OnRealmSelected.BindUObject(this, &AWowLoginController::HandleRealmSelected);
    RealmSelectWidget->OnCancel.BindLambda([this]() {
        if (CharSelectWidget.IsValid()) CloseGlueDialog();
        else HandleBackFromCharacterSelect();
    });
    RealmSelectWidget->PopulateRealms(Realms, ActiveRealmIndex);
    GlueDialog = RealmSelectWidget;
    if (CurrentWidget.IsValid()) CurrentWidget->SetEnabled(false);
    if (GEngine && GEngine->GameViewport)
        FWowUiSurfaces::Add(GlueDialog.ToSharedRef(), EWowUiZone::Glue, 110);
    if (auto* PC = Cast<AWowGameplayController>(UGameplayStatics::GetPlayerController(this, 0)))
        ApplyWowLoginUiInputMode(PC, GlueDialog);
}

/** SimpleHTML server alert -> plain lines with WoW colour codes: headings gold, <br>/<p> line breaks,
 *  other tags dropped, entities decoded, |H hyperlinks reduced to their [text]. */
static FString ServerAlertToNewsText(FString Html)
{
    FString Out;
    for (int32 I = 0; I < Html.Len(); ++I)
    {
        if (Html[I] == TEXT('<'))
        {
            const int32 End = Html.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I);
            if (End == INDEX_NONE) break;
            FString Tag = Html.Mid(I + 1, End - I - 1).TrimStartAndEnd().ToLower();
            const bool bClose = Tag.StartsWith(TEXT("/"));
            if (bClose) Tag.RightChopInline(1);
            Tag = Tag.Left(Tag.Find(TEXT(" ")) == INDEX_NONE ? Tag.Len() : Tag.Find(TEXT(" "))).Replace(TEXT("/"), TEXT(""));
            if (Tag == TEXT("br")) Out += TEXT("\n");
            else if (Tag == TEXT("p")) { if (bClose) Out += TEXT("\n"); }
            else if (Tag == TEXT("h1") || Tag == TEXT("h2") || Tag == TEXT("h3"))
                Out += bClose ? TEXT("|r\n") : TEXT("\n|cffffd100");
            I = End;
            continue;
        }
        Out.AppendChar(Html[I]);
    }
    Out = Out.Replace(TEXT("&nbsp;"), TEXT(" ")).Replace(TEXT("&lt;"), TEXT("<")).Replace(TEXT("&gt;"), TEXT(">"))
        .Replace(TEXT("&quot;"), TEXT("\"")).Replace(TEXT("&amp;"), TEXT("&")).Replace(TEXT("\r"), TEXT(""));
    // |Hlink|h[text]|h -> [text]
    for (int32 Start = Out.Find(TEXT("|H"), ESearchCase::CaseSensitive); Start != INDEX_NONE; Start = Out.Find(TEXT("|H"), ESearchCase::CaseSensitive))
    {
        const int32 Mid = Out.Find(TEXT("|h"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 2);
        if (Mid == INDEX_NONE) break;
        Out.RemoveAt(Start, Mid + 2 - Start);
        const int32 Tail = Out.Find(TEXT("|h"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
        if (Tail != INDEX_NONE) Out.RemoveAt(Tail, 2);
    }
    // Collapse runs of blank lines and trim each line.
    TArray<FString> Lines;
    Out.ParseIntoArray(Lines, TEXT("\n"), false);
    FString Result;
    bool bLastBlank = true;
    for (FString& Line : Lines)
    {
        Line.TrimStartAndEndInline();
        const bool bBlank = Line.IsEmpty();
        if (bBlank && bLastBlank) continue;
        Result += Line + TEXT("\n");
        bLastBlank = bBlank;
    }
    return Result.TrimStartAndEnd();
}

void AWowLoginController::RequestServerAlert(TWeakPtr<SWowCharacterSelectWidget> Widget)
{
    // The alert host: "SET patchlist" in the server client's Config.wtf (beside its Data folder),
    // else the logon server this login went to.
    FString Host;
    if (WorldManager && WorldManager->GetMpqManager())
    {
        FString Config;
        const FString ConfigPath = FPaths::Combine(FPaths::GetPath(WorldManager->GetMpqManager()->GetDataPath()), TEXT("WTF"), TEXT("Config.wtf"));
        if (FFileHelper::LoadFileToString(Config, *ConfigPath))
        {
            TArray<FString> Lines;
            Config.ParseIntoArrayLines(Lines);
            for (const FString& Line : Lines)
            {
                FString Rest = Line.TrimStartAndEnd();
                if (!Rest.StartsWith(TEXT("SET patchlist"), ESearchCase::IgnoreCase)) continue;
                Rest = Rest.Mid(13).TrimStartAndEnd().TrimChar(TEXT('"'));
                if (!Rest.IsEmpty()) Host = Rest;
            }
        }
    }
    if (Host.IsEmpty()) Host = LoginServerAddress;
    if (Host.IsEmpty()) return;
    // One download per host per session; character select reuses it.
    static TMap<FString, FString> GAlertCache;
    if (const FString* Cached = GAlertCache.Find(Host))
    {
        if (TSharedPtr<SWowCharacterSelectWidget> Pinned = Widget.Pin(); Pinned.IsValid() && !Cached->IsEmpty()) Pinned->SetServerNews(*Cached);
        return;
    }
    const FString Url = Host.Contains(TEXT("://")) ? Host + TEXT("/alert") : FString::Printf(TEXT("http://%s/alert"), *Host);
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(8.0f);
    Request->OnProcessRequestComplete().BindLambda([Widget, Host, Url](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
    {
        FString Text = bOk && Response.IsValid() && Response->GetResponseCode() == 200 ? Response->GetContentAsString() : FString();
        Text.TrimStartInline();
        const bool bAlert = Text.StartsWith(TEXT("SERVERALERT:"), ESearchCase::IgnoreCase);
        const FString News = bAlert ? ServerAlertToNewsText(Text.Mid(12)) : FString();
        GAlertCache.Add(Host, News);
        UE_LOG(LogWowLogin, Log, TEXT("Server alert %s: %s"), *Url,
            bAlert ? *FString::Printf(TEXT("%d characters"), News.Len()) : (bOk && Response.IsValid() ? *FString::Printf(TEXT("none (HTTP %d)"), Response->GetResponseCode()) : TEXT("no answer")));
        if (TSharedPtr<SWowCharacterSelectWidget> Pinned = Widget.Pin(); Pinned.IsValid() && !News.IsEmpty()) Pinned->SetServerNews(News);
    });
    Request->ProcessRequest();
}

void AWowLoginController::ShowCharacterSelectScreen(const TArray<FWowCharacterInfo>& Characters)
{
    // r378: the intro movie for this server (its own ServerIntro, else the account expansion's
    // trailer) plays once before the first character select of a session. Not offline.
    static bool GWowIntroMovieChecked = false;
    if (!bOfflineFlow && !GWowIntroMovieChecked && WorldManager && WorldManager->GetMpqManager())
    {
        GWowIntroMovieChecked = true;
        TWeakObjectPtr<AWowLoginController> WeakThis(this);
        if (FWowTrailerPlayer::PlayIntroOnce(WorldManager->GetMpqManager(), FWowWorldSocket::GetLastAccountExpansion(),
            [WeakThis, Characters]() { if (WeakThis.IsValid()) WeakThis->ShowCharacterSelectScreen(Characters); }))
            return;
    }
    ClearCurrentScreen();
    SuppressDebugMessagesForGlueScreen();

    // Character selection precedes the in-game FrameManager. Load shared artwork
    // here as well so the native button style never depends on entering the world first.
    FWowWindowStyle::LoadTextures([this](const FString& Path) {
        return LoadFirstMpqBlpTexture({Path+TEXT(".blp")},TEXT("character-select shared chrome"));
    });
    // The AddOns list on this screen reads TOCs before any UI is loaded.
    if (WorldManager && WorldManager->GetMpqManager())
    {
        FWowAddonSettings::SetMpq(WorldManager->GetMpqManager());
    }
    CharSelectWidget = SNew(SWowCharacterSelectWidget);
    CharSelectWidget->OnCharacterSelected.BindUObject(this, &AWowLoginController::HandleCharacterSelected);
    CharSelectWidget->OnCreateCharacterRequest.BindUObject(this, &AWowLoginController::HandleCreateCharacterRequest);
    CharSelectWidget->OnDeleteCharacterRequest.BindUObject(this, &AWowLoginController::HandleDeleteCharacterRequest);
    CharSelectWidget->OnBackRequest.BindUObject(this, &AWowLoginController::HandleBackFromCharacterSelect);
    if (!bOfflineFlow) CharSelectWidget->OnChangeRealmRequest.BindUObject(this, &AWowLoginController::HandleChangeRealm);
    CharSelectWidget->OnCharacterHighlighted.BindUObject(this, &AWowLoginController::HandleCharacterInfoHighlighted);
    CharSelectWidget->OnCharacterRotated.BindUObject(this, &AWowLoginController::HandleCharacterPreviewRotated);
    CharSelectWidget->SetGlueTextures(
        LoadFirstMpqBlpTexture({TEXT("Interface\\Glues\\Common\\Glues-WoW-WotLKLogo.blp")}, TEXT("character-select logo")),
        LoadFirstMpqBlpTexture({TEXT("Interface\\Glues\\CharacterSelect\\Glue-CharacterSelect-Highlight.blp")}, TEXT("character-select highlight")));
    CharSelectWidget->PopulateCharacters(Characters);
    // r232: the realm's name on top of the list, and the server's own news if its data has any.
    if (ConnectionManager) CharSelectWidget->SetRealmName(ConnectionManager->GetSelectedRealmName());
    bool bHasNewsFile = false;
    if (WorldManager && WorldManager->GetMpqManager())
    {
        TArray<uint8> NewsRaw;
        if (WorldManager->GetMpqManager()->ReadFile(TEXT("Interface\\Glues\\ServerNews.txt"), NewsRaw) && NewsRaw.Num() > 0)
        {
            FString News;
            FFileHelper::BufferToString(News, NewsRaw.GetData(), NewsRaw.Num());
            CharSelectWidget->SetServerNews(News);
            bHasNewsFile = true;
        }
    }
    // r233 (Stefan 12.10: TheraWoW's "Server News"): the 3.3.5 server alert. The original client
    // downloads http://<patchlist>/alert (Config.wtf "SET patchlist"; private servers point it at
    // their logon host) and shows it when the text starts with "SERVERALERT:" (SimpleHTML).
    if (!bHasNewsFile && !bOfflineFlow) RequestServerAlert(TWeakPtr<SWowCharacterSelectWidget>(CharSelectWidget));
    CurrentWidget = CharSelectWidget;

    if (GEngine && GEngine->GameViewport)
    {
        FWowUiSurfaces::Add(CurrentWidget.ToSharedRef(), EWowUiZone::Glue, 100);
    }
	if (AWowGameplayController* GPC = Cast<AWowGameplayController>(
		UGameplayStatics::GetPlayerController(this, 0)))
	{
		GPC->SetSoftwareCursorSuppressed(true, true);
		ApplyWowLoginUiInputMode(GPC, CurrentWidget);
	}

    // Create character preview actor if we have a world manager
    if (WorldManager && !CharacterPreview)
    {
        UWorld* World = GetWorld();
        if (World)
        {
            FActorSpawnParameters PreviewParams;
            // ClearCurrentScreen destroys the previous preview immediately before
            // this transition. Unreal keeps the old object name reserved until
            // the pending-kill actor is purged, so a fixed name crashes when the
            // user moves quickly between character select and creation. Let the
            // world generate a unique transient actor name instead.
            CharacterPreview = World->SpawnActor<AWowCharacterPreview>(
                AWowCharacterPreview::StaticClass(),
                FVector::ZeroVector, FRotator::ZeroRotator, PreviewParams);
            if (CharacterPreview)
            {
                CharacterPreview->Setup(WorldManager, 1920, 1080);
                CharSelectWidget->SetPreviewRenderTarget(CharacterPreview->GetRenderTarget());
            }
        }
    }

    // Auto-highlight first character
    if (Characters.Num() > 0 && CharacterPreview)
    {
        CharacterPreview->ShowCharacter(Characters[0]);
    }
    else if (CharacterPreview)
    {
        CharacterPreview->ShowEmptySelectionScene();
    }
}

void AWowLoginController::ShowCharacterCreateScreen()
{
    ClearCurrentScreen();

    CharCreateWidget = SNew(SWowCharacterCreateWidget);
    CharCreateWidget->OnCharacterCreated.BindUObject(this, &AWowLoginController::HandleCharacterCreated);
    CharCreateWidget->OnBackToCharSelect.BindUObject(this, &AWowLoginController::HandleBackToCharSelect);
    CharCreateWidget->OnAppearanceChanged.BindUObject(this, &AWowLoginController::HandleCharacterHighlighted);
    CharCreateWidget->OnPreviewRotated.BindUObject(this, &AWowLoginController::HandleCharacterPreviewRotated);
    const TCHAR* TextureKeys[] = {TEXT("Logo"),TEXT("Races"),TEXT("Classes"),TEXT("Gender"),TEXT("Banners")};
    const TCHAR* TexturePaths[] = {
        TEXT("Interface\\Glues\\Common\\Glues-WoW-WotLKLogo.blp"),
        TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Races.blp"),
        TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Classes.blp"),
        TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Gender.blp"),
        TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Banners.blp")};
    for (int32 Index=0; Index<UE_ARRAY_COUNT(TextureKeys); ++Index)
        CharCreateWidget->SetGlueTexture(TextureKeys[Index], LoadLoadingScreenTexture(TexturePaths[Index]));
    CharCreateWidget->SetTextureLoader([this](const FString& Path) { return LoadLoadingScreenTexture(Path + TEXT(".blp")); });
    // Race/class descriptions and labels come from the user's own GlueStrings.lua.
    if (WorldManager && WorldManager->GetMpqManager())
    {
        TArray<uint8> Data;
        if (WorldManager->GetMpqManager()->ReadFile(TEXT("Interface\\GlueXML\\GlueStrings.lua"), Data))
        {
            const int32 Skip = Data.Num() >= 3 && Data[0] == 0xEF && Data[1] == 0xBB && Data[2] == 0xBF ? 3 : 0;
            FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Data.GetData()) + Skip, Data.Num() - Skip);
            TArray<FString> Lines;
            FString(Conv.Length(), Conv.Get()).ParseIntoArrayLines(Lines);
            TMap<FString, FString> Strings;
            for (const FString& Line : Lines)
            {
                // NAME = "text";
                int32 Eq = INDEX_NONE;
                if (!Line.FindChar(TEXT('='), Eq)) continue;
                const FString Key = Line.Left(Eq).TrimStartAndEnd();
                FString Value = Line.Mid(Eq + 1).TrimStartAndEnd();
                if (Key.IsEmpty() || !Value.StartsWith(TEXT("\"")) || !Value.EndsWith(TEXT("\";"))) continue;
                Value = Value.Mid(1, Value.Len() - 3);
                Value.ReplaceInline(TEXT("\\\""), TEXT("\""));
                Value.ReplaceInline(TEXT("\\n"), TEXT("\n"));
                Value.ReplaceInline(TEXT("|n"), TEXT("\n"));
                Value.ReplaceInline(TEXT("%%"), TEXT("%"));
                Strings.Add(Key, Value);
            }
            CharCreateWidget->SetGlueStrings(MoveTemp(Strings));
        }
    }
    CurrentWidget = CharCreateWidget;

    if (GEngine && GEngine->GameViewport)
    {
        FWowUiSurfaces::Add(CurrentWidget.ToSharedRef(), EWowUiZone::Glue, 100);
    }
	if (AWowGameplayController* GPC = Cast<AWowGameplayController>(
		UGameplayStatics::GetPlayerController(this, 0)))
	{
		GPC->SetSoftwareCursorSuppressed(true, true);
		ApplyWowLoginUiInputMode(GPC, CurrentWidget);
	}

    if (WorldManager)
    {
        if (UWorld* World = GetWorld())
        {
            FActorSpawnParameters PreviewParams;
            // Do not reserve a fixed name here either. Repeated create/back
            // transitions can happen within one GC cycle.
            CharacterPreview = World->SpawnActor<AWowCharacterPreview>(
                AWowCharacterPreview::StaticClass(), FVector::ZeroVector,
                FRotator::ZeroRotator, PreviewParams);
            if (CharacterPreview)
            {
                CharacterPreview->Setup(WorldManager, 1920, 1080, EWowCharacterPreviewMode::CharacterCreation);
                CharCreateWidget->SetPreviewRenderTarget(CharacterPreview->GetRenderTarget());
                // Starts on the remembered unfinished character, if any (human male otherwise).
                const FWowCharacterInfo Draft = CharCreateWidget->GetPreviewCharacter();
                HandleCharacterHighlighted(Draft.Race ? Draft.Race : 1, Draft.Gender);
            }
        }
    }
}

void AWowLoginController::SetStatusText(const FString& Text)
{
    if (LoginWidget.IsValid()) LoginWidget->SetStatusText(Text);
    if (RealmSelectWidget.IsValid()) RealmSelectWidget->SetStatusText(Text);
    if (CharSelectWidget.IsValid()) CharSelectWidget->SetStatusText(Text);
    if (CharCreateWidget.IsValid()) CharCreateWidget->SetStatusText(Text);
    if (LoadingScreenWidget.IsValid()) LoadingScreenWidget->SetProgressText(Text);
}

UTexture2D* AWowLoginController::LoadLoadingScreenTexture(const FString& SourcePath) const
{
    if (!WorldManager || !WorldManager->GetMpqManager() || SourcePath.IsEmpty())
    {
        return nullptr;
    }

    FString LoadingScreenPath = SourcePath;
    LoadingScreenPath.TrimStartAndEndInline();
    LoadingScreenPath.ReplaceInline(TEXT("/"), TEXT("\\"));
    if (!LoadingScreenPath.StartsWith(TEXT("Interface\\"), ESearchCase::IgnoreCase))
    {
        LoadingScreenPath = TEXT("Interface\\Glues\\LoadingScreens\\") + LoadingScreenPath;
    }
    if (!LoadingScreenPath.EndsWith(TEXT(".blp"), ESearchCase::IgnoreCase))
    {
        LoadingScreenPath += TEXT(".blp");
    }

    TArray<uint8> BlpData;
    if (!WorldManager->GetMpqManager()->ReadFile(LoadingScreenPath, BlpData))
    {
        return nullptr;
    }

    const FBlpTexture BlpTexture = FBlpParser::Parse(BlpData);
    if (BlpTexture.MipLevels.IsEmpty())
    {
        UE_LOG(LogWowLogin, Warning, TEXT("Failed to parse loading screen: %s"), *LoadingScreenPath);
        return nullptr;
    }

    UTexture2D* Texture = FWowTextureFactory::CreateTexture(BlpTexture, LoadingScreenPath);
    if (Texture)
    {
        UE_LOG(LogWowLogin, Log, TEXT("Loaded loading screen: %s"), *LoadingScreenPath);
    }
    return Texture;
}

UTexture2D* AWowLoginController::ResolveLoadingScreenTexture(uint32 MapId, bool bAllowFallback) const
{
    const FDbcStore& DbcStore = FDbcStore::Get();
    if (DbcStore.IsLoaded())
    {
        if (const FMapDbcEntry* MapEntry = DbcStore.Maps().GetById(MapId))
        {
            UE_LOG(LogWowLogin, Log, TEXT("Resolving loading screen for map %u with ID %u"),
                MapId, MapEntry->LoadingScreenID);

            if (const FLoadingScreensDbcEntry* LoadingEntry =
                DbcStore.LoadingScreens().GetById(MapEntry->LoadingScreenID))
            {
                if (UTexture2D* Texture = LoadLoadingScreenTexture(LoadingEntry->FileName))
                {
                    return Texture;
                }
            }

            const TArray<FString> MapNameCandidates = {
                FString::Printf(TEXT("%s.blp"), *MapEntry->InternalName),
                FString::Printf(TEXT("LoadingScreen_%s.blp"), *MapEntry->InternalName),
                FString::Printf(TEXT("LoadScreen_%s.blp"), *MapEntry->InternalName)
            };
            for (const FString& Candidate : MapNameCandidates)
            {
                if (UTexture2D* Texture = LoadLoadingScreenTexture(Candidate))
                {
                    return Texture;
                }
            }
        }
    }

    if (!bAllowFallback)
    {
        return nullptr;
    }

    TArray<FString> FallbackCandidates;
    FallbackCandidates.Add(TEXT("Interface\\Glues\\LoadingScreens\\LoadingScreen.blp"));

    if (DbcStore.IsLoaded())
    {
        for (const FLoadingScreensDbcEntry& Entry : DbcStore.LoadingScreens().GetAll())
        {
            if (!Entry.FileName.IsEmpty())
            {
                FallbackCandidates.AddUnique(Entry.FileName);
            }
        }
    }

    FallbackCandidates.Append({
        TEXT("LoadScreenEasternKingdoms.blp"),
        TEXT("LoadScreenKalimdor.blp"),
        TEXT("LoadScreenOutland.blp"),
        TEXT("LoadScreenNorthrend.blp")
    });

    const uint64 Seed = static_cast<uint64>(FDateTime::UtcNow().GetTicks())
        ^ (static_cast<uint64>(MapId) * 0x9E3779B97F4A7C15ull);
    const int32 StartIndex = FallbackCandidates.IsEmpty()
        ? 0
        : static_cast<int32>(Seed % static_cast<uint64>(FallbackCandidates.Num()));

    for (int32 Offset = 0; Offset < FallbackCandidates.Num(); ++Offset)
    {
        const FString& Candidate = FallbackCandidates[(StartIndex + Offset) % FallbackCandidates.Num()];
        if (UTexture2D* Texture = LoadLoadingScreenTexture(Candidate))
        {
            UE_LOG(LogWowLogin, Log, TEXT("Using fallback loading screen candidate %d of %d"),
                Offset + 1, FallbackCandidates.Num());
            return Texture;
        }
    }

    return nullptr;
}

void AWowLoginController::UpdateLoadingScreenBackground(uint32 MapId, bool bAllowFallback)
{
    if (!LoadingScreenWidget.IsValid())
    {
        return;
    }

    if (UTexture2D* Texture = ResolveLoadingScreenTexture(MapId, bAllowFallback))
    {
        LoadingScreenWidget->SetBackgroundImage(Texture);
    }
    else
    {
        UE_LOG(LogWowLogin, Warning, TEXT("No loading screen image was available for map %u"), MapId);
    }
}

void AWowLoginController::ShowLoadingScreen(uint32 MapId)
{
    ClearCurrentScreen();

    if (WorldManager)
    {
        WorldManager->EndWorldEntryWarmup();
    }
    bHasPendingSpawn = false;
    InitialLoadCompleteSettleTime = 0.0f;
    WorldEntryWarmupElapsed = 0.0f;
    WorldEntryWarmupStartedAtSeconds = 0.0;
    WorldEntryWarmupStartPass = 0;
    LastWorldEntryObjectRevision = 0;
    LastWorldEntryEntityRevision = 0;
    bEntryPresentationPrepared = false;
    bInitialLoadSettleLogged = false;

    if (AWowGameplayController* GPC = Cast<AWowGameplayController>(UGameplayStatics::GetPlayerController(this, 0)))
    {
        GPC->SetGameplayHudVisible(false);
        ApplyWowLoginUiInputMode(GPC, GEngine && GEngine->GameViewport
            ? StaticCastSharedPtr<SWidget>(GEngine->GameViewport->GetGameViewportWidget())
            : nullptr);
        GPC->SetSoftwareCursorSuppressed(true);
        GPC->bShowMouseCursor = false;
        GPC->DefaultMouseCursor = EMouseCursor::None;
        GPC->CurrentMouseCursor = EMouseCursor::None;
    }

    if (GEngine && !bSavedOnScreenDebugState)
    {
        bPreviousOnScreenDebugEnabled = GEngine->bEnableOnScreenDebugMessages;
        bPreviousGlobalScreenMessagesEnabled = GAreScreenMessagesEnabled;
        bSavedOnScreenDebugState = true;
        GEngine->bEnableOnScreenDebugMessages = false;
        GAreScreenMessagesEnabled = false;
    }

    LoadingScreenWidget = SNew(SWowLoadingScreen);
    CurrentWidget = LoadingScreenWidget;

    static const TCHAR* LoadingTips[] = {
        TEXT("You can right-click a target to interact with it."),
        TEXT("Press C to open your character sheet."),
        TEXT("Press O to open the social window."),
        TEXT("You can hold the right mouse button to turn the camera."),
        TEXT("Use the mouse wheel to zoom the camera in and out."),
        TEXT("Some UI windows can be moved or resized once unlocked."),
        TEXT("Quest objectives are tracked on the right side of the screen."),
        TEXT("Shift-click a quest in your log to track or untrack it."),
        TEXT("Resting in an inn or a capital city builds up rested experience."),
        TEXT("Set your hearthstone at an innkeeper to return there quickly."),
        TEXT("Flight masters connect the towns you have discovered."),
        TEXT("Repair your gear at a vendor with a repair icon before heading into a dungeon."),
        TEXT("Hold Shift while looting to loot everything at once."),
        TEXT("Press Alt+Z to hide the interface and take a clean screenshot."),
        TEXT("Your talents can be reset at your class trainer."),
        TEXT("Mailboxes are found in every major town and most inns."),
        TEXT("Grey items are vendor trash: sell them for some extra coin."),
        TEXT("Experimental options in the client settings let you try new visual features."),
        TEXT("If the data folder is not linked, the client falls back to placeholder assets.")
    };
    const int32 TipCount = UE_ARRAY_COUNT(LoadingTips);
    // Entering the world shows the loading screen twice (Enter World, then world session):
    // keep the same tip for that one load instead of switching it a second later.
    static int32 LastTipIndex = -1;
    static double LastTipTime = -1000.0;
    const double TipNow = FPlatformTime::Seconds();
    if (LastTipIndex < 0 || TipNow - LastTipTime > 15.0)
        LastTipIndex = TipCount > 0 ? FMath::RandRange(0, TipCount - 1) : 0;
    LastTipTime = TipNow;
    const int32 TipIndex = LastTipIndex;
    if (TipCount > 0)
    {
        LoadingScreenWidget->SetTipText(FString::Printf(TEXT("Tip: %s"), LoadingTips[TipIndex]));
    }

    if (WorldManager && WorldManager->GetMpqManager())
    {
        const FDbcStore& DbcStore = FDbcStore::Get();
        UE_LOG(LogWowLogin, Log, TEXT("ShowLoadingScreen: MapId=%u, DbcStore loaded=%s"), MapId, DbcStore.IsLoaded() ? TEXT("true") : TEXT("false"));
        UpdateLoadingScreenBackground(MapId, true);
        if (DbcStore.IsLoaded())
        {
            ConfigureLoadingBar(LoadingScreenWidget.ToSharedRef());
        }
        else
        {
            UE_LOG(LogWowLogin, Warning, TEXT("DBC Store not loaded - cannot load loading screen image"));
        }
    }
    else
    {
        UE_LOG(LogWowLogin, Warning, TEXT("WorldManager or MpqManager not available - cannot load loading screen image"));
    }

    LoadingScreenWidget->SetProgress(0.0f, TEXT("Loading terrain..."));

    LoadingWidget = LoadingScreenWidget;
    AWowAudioManager::SetWorldLoading(AWowAudioManager::LoadingLogin, true); // r297: world sounds mute while loading
    if (GEngine && GEngine->GameViewport)
    {
        FWowUiSurfaces::Add(LoadingWidget.ToSharedRef(), EWowUiZone::Glue, 200);
    }
    bWaitingForInitialLoad = true;
}

void AWowLoginController::OnLoginVerifyWorld(uint32 MapId, float X, float Y, float Z, float Orientation)
{
    if(WorldManager && !WorldManager->SelectMap(MapId))
    {
        UE_LOG(LogWowLogin,Error,TEXT("Server map %u could not be loaded"),MapId);
        return;
    }
    if (LoadingScreenWidget.IsValid() && WorldManager && WorldManager->GetMpqManager())
    {
        UE_LOG(LogWowLogin, Log, TEXT("LoginVerifyWorld: applying loading screen for map %u"), MapId);
        UpdateLoadingScreenBackground(MapId, false);
    }

    // Unbind the event since we only need it once
    if (ConnectionManager && LoginVerifyWorldHandle.IsValid())
    {
        ConnectionManager->PacketHandler.OnLoginVerifyWorld.Remove(LoginVerifyWorldHandle);
        LoginVerifyWorldHandle.Reset();
    }
}

void AWowLoginController::SaveOfflineLocation(const FVector& Feet,float Orientation)
{
    if (!bOfflineFlow || !bHasSelectedCharacterInfo || !SelectedCharacterGuid || Feet.ContainsNaN()) return;
    TArray<FWowOfflineCharacterRecord> Records;
    if(!FWowOfflineCharacterStore::Load(Records)) return;
    for(auto& Record:Records) if(Record.Guid==SelectedCharacterGuid)
    {
        Record.Position=FWowCoordinate::UEToWow(Feet);Record.Orientation=Orientation;
        if (WorldManager)
        {
            Record.ZoneId=WorldManager->GetCurrentZoneId();
            // A live cross-continent move must save its new map together with XYZ.
            for (const auto& Map : FDbcStore::Get().Maps().GetAll())
                if (Map.InternalName==WorldManager->GetMapName()) { Record.MapId=Map.ID; break; }
        }
        if(!FWowOfflineCharacterStore::Save(Records)) { UE_LOG(LogWowLogin,Warning,TEXT("Could not save offline logout position")); }
        else OfflineCharacters=MoveTemp(Records);
        return;
    }
}

TSharedRef<SWowLoadingScreen> AWowLoginController::CreateTransferLoadingScreen(uint32 MapId)
{
    // Reuse loading artwork without restarting login or recreating the player.
    auto Screen=SNew(SWowLoadingScreen);
    if (auto* Texture=ResolveLoadingScreenTexture(MapId,true)) Screen->SetBackgroundImage(Texture);
    ConfigureLoadingBar(Screen);
    Screen->SetTipText(TEXT("Loading the destination..."));
    Screen->SetProgress(0.f,FString());
    return Screen;
}

void AWowLoginController::ConfigureLoadingBar(TSharedRef<SWowLoadingScreen> Screen)
{
    if (!WorldManager || !WorldManager->GetMpqManager()) return;
            auto LoadLoadingBarTexture = [this](const TArray<FString>& Paths) -> UTexture2D*
            {
                for (const FString& Path : Paths)
                {
                    TArray<uint8> Data;
                    if (!WorldManager->GetMpqManager()->ReadFile(Path, Data)) continue;
                    const FBlpTexture Parsed = FBlpParser::Parse(Data);
                    if (Parsed.MipLevels.IsEmpty()) continue;
                    if (UTexture2D* Texture = FWowTextureFactory::CreateTexture(Parsed, Path))
                    {
                        UE_LOG(LogWowLogin, Log, TEXT("Loaded loading bar artwork: %s"), *Path);
                        return Texture;
                    }
                }
                return nullptr;
            };

            UTexture2D* BarTrack = LoadLoadingBarTexture({
                TEXT("Interface\\Glues\\LoadingBar\\Loading-BarBackground.blp"),
                TEXT("Interface\\Glues\\LoadingBar\\Loading-BarBackground")
            });
            UTexture2D* BarFill = LoadLoadingBarTexture({
                TEXT("Interface\\Glues\\LoadingBar\\Loading-BarFill.blp"),
                TEXT("Interface\\Glues\\LoadingBar\\Loading-BarFill")
            });
            UTexture2D* BarBorder = LoadLoadingBarTexture({
                TEXT("Interface\\Glues\\LoadingBar\\Loading-BarBorder.blp"),
                TEXT("Interface\\Glues\\LoadingBar\\Loading-BarBorder")
            });
            Screen->SetProgressTextures(BarTrack, BarFill, BarBorder);
}
