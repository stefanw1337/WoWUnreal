#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "../WowLoginWidget.h"
#include "../WowLoginTheme.h"
#include "Mpq/MpqManager.h"
#include "Formats/M2Parser.h"
#include "Formats/BlpParser.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "../WowLoginController.h"
#include "WowAssetCache.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "RenderingThread.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLoginPlateFitTest,"WowUnreal.Login.BackgroundCover",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowLoginPlateFitTest::RunTest(const FString&)
{
    for (const FVector2D Size : {FVector2D(3840,2160), FVector2D(1920,1080), FVector2D(3440,1440), FVector2D(1280,1024)})
    {
        const float Aspect = Size.X / Size.Y;
        const float Fov = WowLoginTheme::BackgroundCoverFov(368.24f,207.135f,1000.f,Aspect);
        const float VisibleWidth = 1000.f * FMath::Tan(FMath::DegreesToRadians(Fov * .5f));
        TestTrue(TEXT("Image covers both viewport axes"),VisibleWidth <= 368.25f && VisibleWidth/Aspect <= 207.145f);
        TestTrue(TEXT("At least one image edge fits exactly"),FMath::IsNearlyEqual(VisibleWidth,368.24f,.01f) || FMath::IsNearlyEqual(VisibleWidth/Aspect,207.135f,.01f));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLoginThemeTest,"WowUnreal.Login.LocalThemesAndCredentials",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowLoginThemeTest::RunTest(const FString&)
{
    TestEqual(TEXT("Auto picks available newest theme"),WowLoginTheme::Resolve(-1,{true,true,true}),EWowExpansion::WrathOfTheLichKing);
    TestEqual(TEXT("Missing saved theme falls back locally"),WowLoginTheme::Resolve(2,{true,true,false}),EWowExpansion::BurningCrusade);
    TestEqual(TEXT("Explicit Vanilla stays Vanilla"),WowLoginTheme::Resolve(0,{true,true,true}),EWowExpansion::Classic);
    FString DataPath;
    if(!FParse::Value(FCommandLine::Get(),TEXT("minimapdata="),DataPath)){AddError(TEXT("Local data path required"));return false;}
    FMpqManager Mpq;if(!TestTrue(TEXT("Local archives mounted"),Mpq.Initialize(DataPath,{},EWowAssetProfile::OriginalWotlk335)))return false;
    TestEqual(TEXT("Original login ignores loose overrides"),Mpq.GetLooseFileRoots().Num(),0);
    FMpqManager Modded;
    if(!TestTrue(TEXT("Gameplay still mounts all HD/loose overrides"),Modded.Initialize(DataPath)))return false;
    TestTrue(TEXT("Gameplay still supports loose overrides"),Modded.GetLooseFileRoots().Num()>0);
    auto Widget=SNew(SWowLoginWidget);
    Widget->SetCredentials(TEXT("example.invalid:3725"),TEXT("TestAccount"),TEXT("test-only"));
    int32 Submitted=0;FString Host,User,Pass;int32 Port=0;
    Widget->OnLoginSubmit.BindLambda([&](const FString& H,int32 P,const FString& U,const FString& W){++Submitted;Host=H;Port=P;User=U;Pass=W;});
    AddExpectedError(TEXT("may need system installation"),EAutomationExpectedErrorFlags::Contains,1);
    Widget->InitializeArtwork(&Mpq);
    TestTrue(TEXT("Button atlas crop removes uneven transparent margins"),FBox2f(Widget->LoginButtonStyle.Normal.GetUVRegion()).Max.Equals(FVector2f(138.f/256.f,37.f/64.f)));
    TestEqual(TEXT("Original interface font"),Widget->LoginFont(14).TypefaceFontName,FName(TEXT("FRIZQT__")));
    for(int32 I=0;I<3;++I)
    {
        const auto Theme=static_cast<EWowExpansion>(I);
        TestTrue(TEXT("Theme model available"),Widget->ThemeAvailable[I]);
        TestTrue(TEXT("Theme logo loaded"),Widget->LogoBrushes[I].IsValid());
        TArray<uint8> Model,Skin;const FString Path=WowLoginTheme::Model(Theme);
        TestTrue(TEXT("Model readable"),Mpq.ReadFile(Path,Model));
        TestTrue(TEXT("Skin readable"),Mpq.ReadFile(Path.LeftChop(3)+TEXT("00.skin"),Skin));
        const auto Parsed=FM2Parser::Parse(Model,Skin);
        TestTrue(TEXT("Original login model parses"),Parsed.bIsValid);
        TestTrue(TEXT("Login model contains geometry"),Parsed.Vertices.Num()>0 && Parsed.Indices.Num()>0);
        if(I==2)TestTrue(TEXT("Original Wrath contains authored scrolling layers"),Parsed.RenderPasses.ContainsByPredicate([](const FM2RenderPass& P){return !P.UVScrollSpeed.IsNearlyZero();}));
        TArray<uint8> ModModel,ModSkin;
        TestTrue(TEXT("Mod-aware model readable"),Modded.ReadFile(Path,ModModel));
        TestTrue(TEXT("Mod-aware skin readable"),Modded.ReadFile(Path.LeftChop(3)+TEXT("00.skin"),ModSkin));
        TestTrue(TEXT("Mod-aware login model also parses"),FM2Parser::Parse(ModModel,ModSkin).bIsValid);
        Widget->SetExpansion(Theme);
        TestEqual(TEXT("Theme selected"),Widget->GetCurrentExpansion(),Theme);
        TestEqual(TEXT("Theme change preserves account"),Widget->UsernameBox->GetText().ToString(),FString(TEXT("TestAccount")));
    }
    FWowCredential A,B;A.Alias=TEXT("Vanilla realm");A.GameVersion=0;B.Alias=TEXT("Wrath realm");B.GameVersion=2;
    Widget->SetSavedProfiles({A,B});
    TestEqual(TEXT("Only current version bookmarks visible"),Widget->BookmarkList->GetChildren()->Num(),1);
    Widget->SetServerVersion(0);
    TestEqual(TEXT("Vanilla list remains separate"),Widget->BookmarkList->GetChildren()->Num(),1);
    TestTrue(TEXT("Version switch clears prior account"),Widget->UsernameBox->GetText().IsEmpty());
    Widget->SetServerVersion(1);
    TestEqual(TEXT("No unrelated bookmarks in TBC list"),Widget->BookmarkList->GetChildren()->Num(),0);
    Widget->SetServerVersion(2);
    Widget->SetCredentials(TEXT("example.invalid:3725"),TEXT("TestAccount"),TEXT("test-only"));
    TestTrue(TEXT("HTTPS website accepted"),SWowLoginWidget::IsWebUrl(TEXT("https://example.invalid/account")));
    TestFalse(TEXT("Local file URI rejected"),SWowLoginWidget::IsWebUrl(TEXT("file:///C:/program.exe")));
    TestFalse(TEXT("Command URI rejected"),SWowLoginWidget::IsWebUrl(TEXT("javascript:alert(1)")));
    TestFalse(TEXT("Missing URL host rejected"),SWowLoginWidget::IsWebUrl(TEXT("https:///account")));
    TestEqual(TEXT("Theme switching does not log in"),Submitted,0);
    TestEqual(TEXT("Drawer starts closed"),Widget->BookmarkDrawer->GetVisibility(),EVisibility::Collapsed);
    Widget->ToggleServerDrawer();
    TestEqual(TEXT("Drawer opens on demand"),Widget->BookmarkDrawer->GetVisibility(),EVisibility::Visible);
    TestFalse(TEXT("Address editor starts collapsed"),Widget->bServerEditorOpen);
    FString Preview;
    if(FParse::Value(FCommandLine::Get(),TEXT("loginpreview="),Preview))
    {
        FWidgetRenderer Renderer(true);
        for (const FVector2D Size : {FVector2D(1920,1080),FVector2D(1280,1024)})
        {
            auto* RT=Renderer.DrawWidget(Widget,Size);
            TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
            TestTrue(TEXT("Login UI renders offscreen"),RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));
            const auto Geo=Widget->UsernameBox->GetCachedGeometry();
            const FVector2D Top=Geo.GetAbsolutePosition(), Bottom=Top+Geo.GetAbsoluteSize();
            TestTrue(TEXT("Account field inside viewport"),Top.X>0 && Top.Y>0 && Bottom.X<Size.X && Bottom.Y<Size.Y);
            TestTrue(TEXT("Account field horizontally centered"),FMath::Abs((Top.X+Bottom.X)*.5-Size.X*.5)<5);
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
            TestTrue(TEXT("Login UI preview saved"),FFileHelper::SaveArrayToFile(PNG,*(Preview+FString::Printf(TEXT("-ui-%d.png"),int32(Size.X)))));
        }
    }
    Widget->AutoSubmit();
    TestEqual(TEXT("Login action still submits"),Submitted,1);
    TestEqual(TEXT("Host preserved"),Host,FString(TEXT("example.invalid")));
    TestEqual(TEXT("Port preserved"),Port,3725);
    TestEqual(TEXT("Password preserved and passed only on submit"),Pass,FString(TEXT("test-only")));
    TestTrue(TEXT("Password field masked"),Widget->PasswordBox->IsPassword());
    Widget->SetOfflineMode(true);
    TestEqual(TEXT("Offline mode label stays hidden"),Widget->ModeLabel->GetVisibility(),EVisibility::Collapsed);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLoginBlpBoundsTest,"WowUnreal.Login.BlpMipBounds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowLoginBlpBoundsTest::RunTest(const FString&)
{
    TArray<uint8> Raw;Raw.SetNumZeroed(156);
    auto Put=[&](int32 Offset,uint32 Value){FMemory::Memcpy(Raw.GetData()+Offset,&Value,4);};
    Put(0,0x32504c42);Put(4,1);Raw[8]=2;Put(12,4);Put(16,4);
    Put(20,148);Put(84,8);
    TestTrue(TEXT("Valid DXT mip accepted"),FBlpParser::Parse(Raw).bIsValid);
    Put(24,400000);Put(88,0xfff90000);
    const auto WithBadTail=FBlpParser::Parse(Raw);
    TestTrue(TEXT("Valid base mip survives corrupt unused tail"),WithBadTail.bIsValid);
    TestEqual(TEXT("Corrupt tail does not allocate or copy"),WithBadTail.MipLevels.Num(),1);
    Put(20,400000);Put(84,0xfff90000);
    TestFalse(TEXT("Overflowing first mip rejected"),FBlpParser::Parse(Raw).bIsValid);
    Put(12,0xffffffff);TestFalse(TEXT("Invalid dimensions rejected"),FBlpParser::Parse(Raw).bIsValid);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLoginSceneRecoveryTest,"WowUnreal.Login.SceneRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowLoginSceneRecoveryTest::RunTest(const FString&)
{
    FString DataPath,Preview;
    if(!FParse::Value(FCommandLine::Get(),TEXT("minimapdata="),DataPath))return false;
    FParse::Value(FCommandLine::Get(),TEXT("loginpreview="),Preview);
    FMpqManager Mpq;if(!Mpq.Initialize(DataPath,{},EWowAssetProfile::OriginalWotlk335))return false;
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Controller=World->SpawnActor<AWowLoginController>();
    FWowAssetCache Cache;Cache.SetRuntimeContext(Controller);
    for(int32 Iteration=0;Iteration<3;++Iteration)
    {
        TestTrue(TEXT("Repeated Wrath click rebuilds scene without name collision"),Controller->SpawnLoginGlueScene(EWowExpansion::WrathOfTheLichKing,&Mpq,&Cache));
        auto* Actor=Controller->LoginGlueActor.Get();
        TestTrue(TEXT("Login model isolated from gameplay origin"),Actor->GetActorLocation().Equals(FVector(0,0,-50000)));
        TArray<USkeletalMeshComponent*> Meshes;Actor->GetComponents(Meshes);
        for(auto* Component:Meshes)
            if(auto* Mesh=Component->GetSkeletalMeshAsset())
                TestTrue(TEXT("All scene sections fit GPU bone limit"),Mesh->GetResourceForRendering()->GetMaxBonesPerSection(0)<=256);
        if(Iteration==0 && !Preview.IsEmpty())
        {
            auto* RT=NewObject<UTextureRenderTarget2D>(Controller);RT->InitAutoFormat(1280,720);
            auto* Capture=NewObject<USceneCaptureComponent2D>(Controller);
            Capture->TextureTarget=RT;Capture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;
            Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
            Capture->SetWorldTransform(Controller->LoginGlueCamera->GetActorTransform());
            Capture->FOVAngle=Controller->LoginGlueCamera->GetCameraComponent()->FieldOfView;
            Capture->PostProcessSettings=Controller->LoginGlueCamera->GetCameraComponent()->PostProcessSettings;
            Capture->RegisterComponent();
            World->UpdateWorldComponents(false,false);
            for (int32 Step=0;Step<=900;++Step)
            {
                World->Tick(LEVELTICK_All,1.f/30.f);
                for (auto* Mesh:Meshes)
                {
                    Mesh->TickAnimation(1.f/30.f,false);
                    Mesh->RefreshBoneTransforms();
                    Mesh->MarkRenderDynamicDataDirty();
                }
                if(Step!=0 && Step!=60 && Step!=300 && Step!=900)continue;
                World->SendAllEndOfFrameUpdates();
            FlushRenderingCommands();Capture->CaptureScene();FlushRenderingCommands();
            TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
            TestTrue(TEXT("Scene pixels readable"),RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1280,720,Pixels,PNG);
            TestTrue(TEXT("Scene preview saved"),FFileHelper::SaveArrayToFile(PNG,*(Preview+FString::Printf(TEXT("-scene-%d.png"),Step/30))));
            }
            Capture->DestroyComponent();
        }
        Controller->ClearLoginGlueScene();
    }
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
