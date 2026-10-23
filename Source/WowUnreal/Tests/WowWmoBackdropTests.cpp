#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Formats/WmoParser.h"
#include "WowWmoBackdropVisibility.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowWmoBackdropTest,"WowUnreal.Stormwind.PortalBackdrop",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowWmoBackdropTest::RunTest(const FString&)
{
    FString Directory;
    if (!FParse::Value(FCommandLine::Get(),TEXT("wmocollisionfixture="),Directory))
    { AddError(TEXT("Supply original WMO fixtures"));return false; }
    TArray<uint8> Raw;
    if (!FFileHelper::LoadFileToArray(Raw,*(Directory/TEXT("root.bin"))))return false;
    const auto Root=FWmoParser::ParseRoot(Raw);
    TestTrue(TEXT("Original Stormwind root parses"),Root.bIsValid);
    TestEqual(TEXT("Original portal reference count"),Root.PortalReferences.Num(),627);
    const auto Planes=UWowWmoBackdropVisibility::GetViewerPlanes(Root,93);
    TestEqual(TEXT("Bazaar skyline has one authored portal"),Planes.Num(),1);
    TestTrue(TEXT("Trade district can see its skyline"),UWowWmoBackdropVisibility::CanView(Planes,FVector(-100,0,30)));
    TestFalse(TEXT("Cathedral courtyard cannot see the trade district's duplicate facade"),UWowWmoBackdropVisibility::CanView(Planes,FVector(-380,0,30)));
    TestTrue(TEXT("Original facade is portal-only"),UWowWmoBackdropVisibility::IsPortalBackdrop(0x28c1));
    TestFalse(TEXT("Real cathedral stays visible"),UWowWmoBackdropVisibility::IsPortalBackdrop(0xa09));
    TestFalse(TEXT("Always-drawn mountain scenery is preserved"),UWowWmoBackdropVisibility::IsPortalBackdrop(0x122c1));
    TestTrue(TEXT("r291: duplicate cathedral facade is portal-only"),UWowWmoBackdropVisibility::IsBackdropGroup(Root,92,0xa09));
    TestFalse(TEXT("r291: cathedral square hides the facade duplicate"),UWowWmoBackdropVisibility::CanView(UWowWmoBackdropVisibility::GetViewerPlanes(Root,92),FVector(-380,0,30)));
    TestTrue(TEXT("r291: bazaar still sees the cathedral facade"),UWowWmoBackdropVisibility::CanView(UWowWmoBackdropVisibility::GetViewerPlanes(Root,92),FVector(-100,0,30)));
    TestFalse(TEXT("r291: mountains are not portal-only"),UWowWmoBackdropVisibility::IsBackdropGroup(Root,82,0x122c1));
    TestTrue(TEXT("Missing metadata fails open"),UWowWmoBackdropVisibility::CanView({},FVector(-380,0,30)));
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    ON_SCOPE_EXIT{World->DestroyWorld(false);};
    auto* Actor=World->SpawnActor<AActor>();
    auto* Mesh=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Mesh);Actor->AddInstanceComponent(Mesh);Mesh->RegisterComponent();
    Actor->SetActorTransform(FTransform(FRotator(0,38.5,0),FVector(-866497,-80596,10196),FVector(1.25)));
    auto* Visibility=NewObject<UWowWmoBackdropVisibility>(Actor);Actor->AddInstanceComponent(Visibility);Visibility->AddGroup(Mesh,Root,93);
    const auto Camera=[&](FVector P){return Actor->GetActorTransform().TransformPosition(FVector(P.Y,P.X,P.Z)*100);};
    Visibility->ApplyCamera(Camera(FVector(-380,0,30)));
    TestFalse(TEXT("Runtime component hides reverse view under full placement transform"),Mesh->IsVisible());
    Visibility->ApplyCamera(Camera(FVector(-100,0,30)));
    TestTrue(TEXT("Returning to bazaar restores original model"),Mesh->IsVisible());
    return true;
}
#endif
