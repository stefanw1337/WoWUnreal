#include "Misc/AutomationTest.h"
#include "WowSkyManager.h"
#include "WowClientPreferences.h"
#include "HAL/IConsoleManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Mpq/MpqManager.h"
#include "WowCharacterBuilder.h"
#include "WowAssetCache.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "RenderingThread.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLateSkyDataTest,"WowUnreal.Lighting.LateZoneData",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowLateSkyDataTest::RunTest(const FString&)
{
    // Run in a fresh automation process to reproduce the real startup order.
    if (!TestFalse(TEXT("Fresh process has not loaded DBCs"),FDbcStore::Get().IsLoaded())) return false;
    FString DataPath, ImagePrefix;
    if(!FParse::Value(FCommandLine::Get(),TEXT("minimapdata="),DataPath))return false;
    FParse::Value(FCommandLine::Get(),TEXT("lightpreview="),ImagePrefix);
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    const bool bModern=FParse::Param(FCommandLine::Get(),TEXT("lightingmodern"));
    FWowClientPreferences::ApplyAdvancedVideoPreferences();
    if(bModern)
    {
        TestEqual(TEXT("Modern comparison actually activates Lumen"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod"))->GetInt(),1);
        TestEqual(TEXT("Modern comparison actually activates hardware RT"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.HardwareRayTracing"))->GetInt(),1);
    }
    auto* Sky=World->SpawnActor<AWowSkyManager>();
    Sky->DispatchBeginPlay();
    TestFalse(TEXT("Early sky correctly starts with fallback"),Sky->HasZoneLighting());
    FMpqManager Mpq;
    if(!Mpq.Initialize(DataPath)) { World->DestroyWorld(false); return false; }
    FDbcStore::Get().LoadAll(Mpq);
    TestFalse(TEXT("Loading tables alone does not bind old sky"),Sky->HasZoneLighting());

    FWowAssetCache Cache; Cache.SetRuntimeContext(Sky);
    auto* Human=FWowCharacterBuilder::SpawnCharacter(World,&Mpq,&Cache,FWowCharacterBuilder::ERace::Human,FWowCharacterBuilder::EGender::Male,FVector(0,-85,0));
    auto* Troll=FWowCharacterBuilder::SpawnCharacter(World,&Mpq,&Cache,FWowCharacterBuilder::ERace::Troll,FWowCharacterBuilder::EGender::Male,FVector(0,85,0));
    TestNotNull(TEXT("Actual Human material/geometry loaded"),Human);
    TestNotNull(TEXT("Actual Troll material/geometry loaded"),Troll);
    // Character builder installs its root after SpawnActor; set fixture transforms afterwards.
    if(Human)Human->SetActorLocation(FVector(0,-115,0));
    if(Troll)Troll->SetActorLocation(FVector(0,115,0));
    TArray<UDirectionalLightComponent*> Lights;Sky->GetComponents(Lights);
    float MinimumFace=100;
    auto GroundLight=[&]()
    {
        float Value=0;
        for(auto* Light:Lights)if(Light->IsVisible())
            Value+=Light->Intensity*FMath::Max(0.f,-Light->GetForwardVector().Z);
        return Value;
    };
    const float NightGround=GroundLight();
    Sky->Tick(1.f/60.f);
    TestTrue(TEXT("First tick after DBC load restores zone lighting"),Sky->HasZoneLighting());
    TestTrue(TEXT("Late fog bands also become available"),Sky->HasFogParameters());
    // Full scene night must remain substantially darker than noon, while
    // every vertical orientation still receives some fill.
    const float NightTime=Sky->TimeOfDay;
    Sky->TimeOfDay=720;Sky->Tick(1.f/60.f);
    TestTrue(TEXT("Night ground receives less than half the noon direct illumination"),NightGround<GroundLight()*.5f);
    Sky->TimeOfDay=NightTime;Sky->Tick(1.f/60.f);
    for(int32 Angle=0;Angle<360;Angle+=15)
    {
        const FVector Normal=FRotator(0,Angle,0).Vector();float Fill=0;
        for(auto* Light:Lights)if(Light->GetName().StartsWith(TEXT("StandardReadability")))
            Fill+=Light->Intensity*FMath::Max(0.f,FVector::DotProduct(Normal,-Light->GetForwardVector()));
        MinimumFace=FMath::Min(MinimumFace,Fill);
    }
    TestTrue(TEXT("No unlit vertical side through a full character turn"),MinimumFace>.2f);
    // Render the prior two-source fill as the comparison baseline.
    for(auto* Light:Lights)if(Light->GetName().StartsWith(TEXT("StandardReadability")))
    {
        const bool OldPair=Light->GetName()==TEXT("StandardReadabilityFill") || Light->GetName()==TEXT("StandardReadabilityBackFill");
        Light->SetIntensity(OldPair?.55f:0.f);
        Light->SetWorldRotation(FRotator(-55,Light->GetComponentRotation().Yaw,0));
    }
    auto* Floor=World->SpawnActor<AActor>();
    auto* Mesh=NewObject<UStaticMeshComponent>(Floor);
    Floor->SetRootComponent(Mesh);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Mesh->SetWorldScale3D(FVector(12,12,0.1)); Mesh->SetWorldLocation(FVector(0,0,-5)); Mesh->RegisterComponent();
    auto* RT=NewObject<UTextureRenderTarget2D>(Sky);RT->InitAutoFormat(800,600);
    auto* Capture=NewObject<USceneCaptureComponent2D>(Sky);
    Capture->TextureTarget=RT;Capture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->FOVAngle=55;
    const FVector Eye(650,0,180),Target(0,0,140);
    Capture->SetWorldLocation(Eye);Capture->SetWorldRotation((Target-Eye).Rotation());
    Capture->PostProcessSettings.bOverride_AutoExposureMethod=true;
    Capture->PostProcessSettings.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;
    Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;
    Capture->PostProcessSettings.bOverride_AutoExposureBias=true;
    Capture->PostProcessSettings.AutoExposureBias=0;
    Capture->bAlwaysPersistRenderingState=true;
    Capture->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod=true;
    Capture->PostProcessSettings.DynamicGlobalIlluminationMethod=bModern?EDynamicGlobalIlluminationMethod::Lumen:EDynamicGlobalIlluminationMethod::None;
    Capture->RegisterComponent();World->UpdateWorldComponents(false,false);
    auto Save=[&](const TCHAR* Suffix)
    {
        for(int32 I=0;I<6;++I){World->SendAllEndOfFrameUpdates();FlushRenderingCommands();Capture->CaptureScene();FlushRenderingCommands();}
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
        TestTrue(TEXT("Read rendered lighting fixture"),RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));
        int32 Visible=0;for(const auto& P:Pixels)if(FMath::Max3(P.R,P.G,P.B)>10)++Visible;
        TestTrue(TEXT("Lighting fixture is not black"),Visible>Pixels.Num()/100);
        if(!ImagePrefix.IsEmpty()){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(800,600,Pixels,PNG);TestTrue(TEXT("Save lighting comparison"),FFileHelper::SaveArrayToFile(PNG,*(ImagePrefix+Suffix+TEXT(".png"))));}
    };
    Save(TEXT("-fallback"));
    Sky->Tick(1.f/60.f);
    TestTrue(TEXT("First tick after DBC load restores zone lighting"),Sky->HasZoneLighting());
    TestTrue(TEXT("Late fog bands also become available"),Sky->HasFogParameters());
    Save(TEXT("-zones"));
    Sky->LoadLightZones(1);
    TestTrue(TEXT("Map transfer binds Kalimdor zones"),Sky->HasZoneLighting());
    Sky->LoadLightZones(999999);
    TestFalse(TEXT("Map without lighting uses fallback without stale zone entries"),Sky->HasZoneLighting());
    Sky->RefreshLightData();
    TestEqual(TEXT("Refresh preserves explicitly selected map"),Sky->CurrentMapId,999999);
    Sky->LoadLightZones(0);
    TestTrue(TEXT("Return to Eastern Kingdoms restores zones"),Sky->HasZoneLighting());
    Capture->DestroyComponent();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    return true;
}
#endif
