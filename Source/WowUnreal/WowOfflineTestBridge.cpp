#include "SWowLoadingScreen.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Components/CapsuleComponent.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowGameplayController.h"
#include "WowLoginController.h"
#include "WowConnectionManager.h"
#include "WowPlayerCharacter.h"
#include "WowWorldManager.h"
#include "WowSkyManager.h"
#include "Coord/WowCoordinate.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

extern bool GWowOfflineFly; // r528, WowPlayerCharacter.cpp

void AWowGameplayController::TickOfflineTestBridge()
{
    const double Now=FPlatformTime::Seconds();
    if(Now<NextOfflineBridgePoll) return;
    NextOfflineBridgePoll=Now+.5;
    // This private tool is opt-in on disk AND requires a disconnected offline
    // session. It never routes a teleport through network packet handlers.
    if(!FParse::Param(FCommandLine::Get(),TEXT("offline")) || !ConnectionManager ||
        ConnectionManager->GetState()!=EWowSessionState::Disconnected)
    { OfflineBridgeSession.Empty(); GWowOfflineFly = false; return; }
    auto* TestPawn=Cast<AWowPlayerCharacter>(GetPawn());
    FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::CollapseRelativeDirectories(Root); FPaths::NormalizeDirectoryName(Root);
    if(FPaths::GetCleanFilename(FPaths::GetPath(Root)).Equals(TEXT("Client"),ESearchCase::IgnoreCase))
        Root=FPaths::GetPath(FPaths::GetPath(FPaths::GetPath(Root)));
    const FString Bridge=Root/TEXT("PrivateTools/OfflineCharacterEditor/Bridge");
    if(!FPaths::FileExists(Bridge/TEXT("enabled.txt"))) return;
    if(OfflineBridgeSession.IsEmpty()) OfflineBridgeSession=FGuid::NewGuid().ToString();
    if (!bGameplayHudVisible)
    {
        FinishOfflineTransferLoading();
        auto* Login=Cast<AWowLoginController>(UGameplayStatics::GetActorOfClass(this,AWowLoginController::StaticClass()));
        auto Idle=MakeShared<FJsonObject>();
        Idle->SetStringField(TEXT("session"),OfflineBridgeSession);
        Idle->SetNumberField(TEXT("timestamp"),double(FDateTime::UtcNow().ToUnixTimestamp()));
        Idle->SetBoolField(TEXT("offline"),true);
        Idle->SetBoolField(TEXT("ready"),false);
        Idle->SetBoolField(TEXT("canEditRoster"),Login && Login->CanEditOfflineRoster());
        Idle->SetStringField(TEXT("characterFile"),FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("OfflineCharacters.json")));
        FString Json; FJsonSerializer::Serialize(Idle,TJsonWriterFactory<>::Create(&Json));
        const FString Temp=Bridge/TEXT("status.tmp");
        if(FFileHelper::SaveStringToFile(Json,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            IFileManager::Get().Move(*(Bridge/TEXT("status.json")),*Temp,true,true);
        return;
    }
    if(!TestPawn) return;
    const int64 CharacterGuid=ConnectionManager->GetSelectedCharacterGuid();
    if(CharacterGuid<=0) return;
    auto* Terrain=Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this,AWowWorldManager::StaticClass()));
    if(!Terrain) return;
    const double Utc=double(FDateTime::UtcNow().ToUnixTimestamp());
    FString Text;
    TSharedPtr<FJsonObject> Request;
    if(FFileHelper::LoadFileToString(Text,*(Bridge/TEXT("request.json"))) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request) && Request.IsValid())
    {
        FString Id,Session,Action;
        double Guid=0,Map=0,X=0,Y=0,Z=0,Expires=0;
        if(Request->TryGetStringField(TEXT("id"),Id) && Id!=OfflineBridgeLastId && !Id.IsEmpty())
        {
            OfflineBridgeLastId=Id; OfflineBridgeError=TEXT("Invalid or expired request.");
            // r528: {"action":"fly","on":true|false} from the editor's "Fly" checkbox.
            bool bFlyOn=false, bFlyHandled=false;
            if (Request->TryGetStringField(TEXT("session"),Session) && Session==OfflineBridgeSession &&
                Request->TryGetStringField(TEXT("action"),Action) && Action==TEXT("fly") &&
                Request->TryGetNumberField(TEXT("guid"),Guid) && Guid==double(CharacterGuid) &&
                Request->TryGetNumberField(TEXT("expires"),Expires) && Expires>=Utc && Expires<=Utc+30 &&
                Request->TryGetBoolField(TEXT("on"),bFlyOn))
            {
                GWowOfflineFly=bFlyOn;
                OfflineBridgeError.Empty();
                bFlyHandled=true;
            }
            const bool Valid=!bFlyHandled && Request->TryGetStringField(TEXT("session"),Session) && Session==OfflineBridgeSession &&
                Request->TryGetStringField(TEXT("action"),Action) && Action==TEXT("teleport") &&
                Request->TryGetNumberField(TEXT("guid"),Guid) && Guid==double(CharacterGuid) &&
                Request->TryGetNumberField(TEXT("expires"),Expires) && Expires>=Utc && Expires<=Utc+30 &&
                Request->TryGetNumberField(TEXT("map"),Map) && (Map==0||Map==1||Map==530||Map==571) &&
                Request->TryGetNumberField(TEXT("x"),X) && Request->TryGetNumberField(TEXT("y"),Y) &&
                Request->TryGetNumberField(TEXT("z"),Z) && FMath::IsFinite(X) && FMath::IsFinite(Y) && FMath::IsFinite(Z) &&
                FMath::Abs(X)<17066 && FMath::Abs(Y)<17066 && Z>-2000 && Z<10000 && !bMapTerrainPending;
            if (Valid && !bOfflineTransferQueued && !OfflineTransferScreen.IsValid())
            {
                const auto* MapEntry=FDbcStore::Get().Maps().GetById(uint32(Map));
                const FVector Feet=FWowCoordinate::WowToUE(float(X),float(Y),float(Z));
                if (!MapEntry) { OfflineBridgeError=TEXT("Unknown destination map."); }
                else
                {
                    OfflineTransferMap=uint32(Map);
                    OfflineTransferWowPosition=FVector(X,Y,Z);
                    bOfflineTransferQueued=true;
                    const bool NeedsLoading=Terrain->GetMapName()!=MapEntry->InternalName ||
                        FVector::DistSquared(TestPawn->GetActorLocation(),Feet)>FMath::Square(50000.f) ||
                        !Terrain->IsTerrainReadyAtPosition(Feet);
                    if (NeedsLoading)
                    {
                        auto* Login=Cast<AWowLoginController>(UGameplayStatics::GetActorOfClass(this,AWowLoginController::StaticClass()));
                        OfflineTransferScreen=Login?Login->CreateTransferLoadingScreen(uint32(Map)):SNew(SWowLoadingScreen);
                        if (GEngine && GEngine->GameViewport)
                            GEngine->GameViewport->AddViewportWidgetContent(OfflineTransferScreen.ToSharedRef(),1000);
                        SetIgnoreMoveInput(true); SetIgnoreLookInput(true);
                        TestPawn->GetCharacterMovement()->StopMovementImmediately();
                        TestPawn->GetCharacterMovement()->DisableMovement();
                    }
                    // Allow the overlay to be drawn before unloading the previous map.
                    OfflineTransferNotBefore=Now+.1;
                    OfflineBridgeError.Empty();
                }
            }
        }
    }
    if (bOfflineTransferQueued && Now>=OfflineTransferNotBefore)
    {
        bOfflineTransferQueued=false;
        if (Terrain->SelectMap(OfflineTransferMap))
        {
                const uint32 Map=OfflineTransferMap;
                const double X=OfflineTransferWowPosition.X,Y=OfflineTransferWowPosition.Y,Z=OfflineTransferWowPosition.Z;
                // Stored positions are feet positions; actors use capsule centres.
                const FVector Destination=FWowCoordinate::WowToUE(float(X),float(Y),float(Z))+
                    FVector(0,0,TestPawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
                if(auto* Sky=Cast<AWowSkyManager>(UGameplayStatics::GetActorOfClass(this,AWowSkyManager::StaticClass())))
                    Sky->LoadLightZones(int32(Map));
                TestPawn->GetCharacterMovement()->StopMovementImmediately();
                TestPawn->GetCharacterMovement()->DisableMovement();
                TestPawn->SetActorLocation(Destination,false,nullptr,ETeleportType::TeleportPhysics);
                LastServerPosition=Destination;
                if (OfflineTransferScreen.IsValid()) Terrain->BeginWorldEntryWarmup(Destination);
                Terrain->LoadTilesAroundPosition(Destination,2);
                bMapTerrainPending=OfflineTransferScreen.IsValid() || !Terrain->IsTerrainReadyAtPosition(Destination);
                if(!bMapTerrainPending) TestPawn->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
                if(auto* Entity=ConnectionManager->PacketHandler.EntityManager.Find(uint64(CharacterGuid)))
                    Entity->Movement.Position=FVector(X,Y,Z);
                OfflineBridgeError.Empty();
                OfflineTransferRevision=Terrain->GetObjectStreamingRevision();
                OfflineTransferStableSince=Now;
        }
        else
        {
            OfflineBridgeError=TEXT("Unable to load destination map.");
            FinishOfflineTransferLoading();
            TestPawn->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        }
    }
    if (OfflineTransferScreen.IsValid() && !bOfflineTransferQueued)
    {
        const auto Revision=Terrain->GetObjectStreamingRevision();
        if (Revision!=OfflineTransferRevision || !Terrain->IsInitialLoadComplete())
        {
            OfflineTransferRevision=Revision;
            OfflineTransferStableSince=Now;
        }
        const float Fraction=float(Terrain->GetInitialTilesLoaded())/FMath::Max(1,Terrain->GetInitialTilesQueued());
        OfflineTransferScreen->SetProgress(.05f+.8f*Fraction,TEXT("Loading destination..."));
        if (Terrain->IsInitialLoadComplete() && Terrain->IsTerrainReadyAtPosition(LastServerPosition) &&
            Terrain->GetObjectStreamingPassCount()>=2 && Now-OfflineTransferStableSince>=1.5)
        {
            Terrain->EndWorldEntryWarmup();
            bMapTerrainPending=false;
            FinishOfflineTransferLoading();
            TestPawn->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        }
    }
    const bool Loading=bMapTerrainPending || bOfflineTransferQueued || OfflineTransferScreen.IsValid();
    auto Status=MakeShared<FJsonObject>();
    Status->SetStringField(TEXT("session"),OfflineBridgeSession);
    Status->SetNumberField(TEXT("timestamp"),Utc);
    Status->SetNumberField(TEXT("guid"),double(CharacterGuid));
    Status->SetBoolField(TEXT("offline"),true);
    Status->SetBoolField(TEXT("ready"),!Loading);
    Status->SetBoolField(TEXT("flying"),GWowOfflineFly);
    Status->SetStringField(TEXT("ack"),OfflineBridgeLastId);
    Status->SetStringField(TEXT("error"),OfflineBridgeError);
    Status->SetStringField(TEXT("result"),!OfflineBridgeError.IsEmpty()?TEXT("error"):Loading?TEXT("loading"):TEXT("moved"));
    FString Json; FJsonSerializer::Serialize(Status,TJsonWriterFactory<>::Create(&Json));
    const FString Temp=Bridge/TEXT("status.tmp");
    if(FFileHelper::SaveStringToFile(Json,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        IFileManager::Get().Move(*(Bridge/TEXT("status.json")),*Temp,true,true);
}

void AWowGameplayController::FinishOfflineTransferLoading()
{
    bOfflineTransferQueued=false;
    if (!OfflineTransferScreen.IsValid()) return;
    if (GEngine && GEngine->GameViewport)
        GEngine->GameViewport->RemoveViewportWidgetContent(OfflineTransferScreen.ToSharedRef());
    OfflineTransferScreen.Reset();
    SetIgnoreMoveInput(false); SetIgnoreLookInput(false);
}
