#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "WowLampFixtures.h"
#include "Formats/M2Parser.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "WowTerrainMaterial.h"
#include "Materials/Material.h"
#include "WowDuskLightComponent.h"
#include "Formats/WmoTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowWindowLightTest,"WowUnreal.Fixtures.WindowLightPlacement",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowWindowLightTest::RunTest(const FString&)
{
    FString Fixture;
    if (FParse::Value(FCommandLine::Get(),TEXT("hdlampfixture="),Fixture))
    {
        TArray<uint8> Raw,Skin;
        if (!FFileHelper::LoadFileToArray(Raw,*(Fixture/TEXT("STORMWINDSTREETLAMP01.m2"))) ||
            !FFileHelper::LoadFileToArray(Skin,*(Fixture/TEXT("STORMWINDSTREETLAMP0100.skin"))))
        { AddError(TEXT("HD lamp fixtures missing")); return false; }
        const auto Data=FM2Parser::Parse(Raw,Skin);
        TestTrue(TEXT("Installed HD street-lamp model parses"),Data.bIsValid);
        FBox Bounds;TArray<int32> Slots;
        TestTrue(TEXT("HD glass gets an actual light source"),FWowLampFixtures::FindM2Glass(Data,Bounds,Slots,TEXT("STORMWINDSTREETLAMP01.m2")));
        TestEqual(TEXT("Only the HD glass pass glows"),Slots.Num(),1);
        TestTrue(TEXT("Source is at the lamp head"),Bounds.IsValid && Bounds.GetCenter().Z>200);
        auto* Light=NewObject<UWowDuskLightComponent>();Light->ApplyTime(3*60+38);
        TestEqual(TEXT("HD lamp is on at reported 03:38"),Light->Intensity,5200.f);
    }
    auto* WindowMaterial=FWowTerrainMaterial::GetOpaqueWindowLightMaterial();
    if (!TestNotNull(TEXT("Window material available"),WindowMaterial)) return false;
    TestEqual(TEXT("Opaque window alpha never cuts out its frame"),WindowMaterial->GetBlendMode(),BLEND_Opaque);
    TestEqual(TEXT("Lantern cutouts remain masked"),FWowTerrainMaterial::GetLampGlassMaterial()->GetBlendMode(),BLEND_Masked);
    TestTrue(TEXT("Authored night window selected"),FWowLampFixtures::IsNightWindow(0xd0,TEXT("MM_STRMWND_HWINDOWS_01.BLP")));
    TestFalse(TEXT("Ordinary window excluded"),FWowLampFixtures::IsNightWindow(0,TEXT("MM_STRMWND_HWINDOWS_01.BLP")));
    TestFalse(TEXT("Unrelated night texture excluded"),FWowLampFixtures::IsNightWindow(0xd0,TEXT("LAMPGLASS.BLP")));
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    ON_SCOPE_EXIT {World->DestroyWorld(false);};
    auto* Actor=World->SpawnActor<AActor>(); auto* Mesh=NewObject<UStaticMeshComponent>(Actor);
    Actor->SetRootComponent(Mesh);Actor->AddInstanceComponent(Mesh);Mesh->RegisterComponent();
    FWmoRootData Root; FWmoMaterial Material; Material.Flags=0xd0;Material.TexturePath1=TEXT("MM_STRMWND_HWINDOWS_01.BLP");Root.Materials.Add(Material);
    FWmoGroupData Group;
    Group.Vertices={FVector(0,0,4),FVector(.6,0,4),FVector(.6,0,5),FVector(0,0,5),
        FVector(3,0,4),FVector(3.6,0,4),FVector(3.6,0,5),FVector(3,0,5)};
    Group.Indices={0,1,2,0,2,3,4,5,6,4,6,7};Group.Normals.Init(FVector(0,1,0),8);
    FWmoGroupData::FBatch Batch;Batch.IndexCount=12;Group.Batches.Add(Batch);
    FWowLampFixtures::AttachWmoWindows(Actor,Mesh,Group,Root);
    TArray<UWowDuskLightComponent*> Lights;Actor->GetComponents(Lights);
    TestEqual(TEXT("One light per pane, not per triangle"),Lights.Num(),2);
    for(auto* Light:Lights)
    {
        TestTrue(TEXT("Light outside wall"),FMath::IsNearlyEqual(Light->GetComponentLocation().X,12.));
        TestTrue(TEXT("Soft source"),Light->SourceRadius>0);
        TestTrue(TEXT("Window light respects walls"),bool(Light->CastShadows));
        Light->ApplyTime(12*60);TestEqual(TEXT("Off at noon"),Light->Intensity,0.f);
        Light->ApplyTime(23*60);TestEqual(TEXT("Lit at night"),Light->Intensity,450.f);
    }
    return true;
}
#endif
