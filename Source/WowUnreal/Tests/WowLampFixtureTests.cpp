#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowLampFixtures.h"
#include "WowM2ParticleComponent.h"
#include "WowDuskLightComponent.h"
#include "WowFixturePolicy.h"
#include "WowTerrainMaterial.h"
#include "Formats/M2Parser.h"
#include "Formats/WmoParser.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLampClockTest,"WowUnreal.Fixtures.DuskAndIndoorLight",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowLampClockTest::RunTest(const FString&)
{
    auto* Lamp=NewObject<UWowDuskLightComponent>();
    auto* Glass=UMaterialInstanceDynamic::Create(FWowTerrainMaterial::GetLampGlassMaterial(),Lamp);
    Lamp->GlassMaterials.Add(Glass);
    Lamp->ApplyTime(12*60);TestEqual(TEXT("Street lamp off at noon"),Lamp->Intensity,0.f);
    Lamp->ApplyTime(17*60);TestEqual(TEXT("Street lamp starts dusk from off"),Lamp->Intensity,0.f);
    Lamp->ApplyTime(18*60);TestEqual(TEXT("Street lamp fades in halfway"),Lamp->Intensity,2600.f);
    float Glow=0;Glass->GetScalarParameterValue(TEXT("LampGlow"),Glow);TestEqual(TEXT("Glass glow follows local light"),Glow,.5f);
    Lamp->ApplyTime(19*60);TestEqual(TEXT("Street lamp full at 19:00"),Lamp->Intensity,5200.f);
    Lamp->ApplyTime(2*60);TestEqual(TEXT("Street lamp stays on overnight"),Lamp->Intensity,5200.f);
    Lamp->ApplyTime(7*60);TestEqual(TEXT("Street lamp fades off at dawn"),Lamp->Intensity,0.f);
    TestEqual(TEXT("Clock wrapping preserves midnight"),UWowDuskLightComponent::NightFactor(1440),1.f);
    Lamp->bOutdoor=false;Lamp->ApplyTime(12*60);
    TestEqual(TEXT("Roofed lantern stays lit in daylight"),Lamp->Intensity,3200.f);
    Glass->GetScalarParameterValue(TEXT("LampGlow"),Glow);TestEqual(TEXT("Indoor glass stays luminous"),Glow,1.f);
    TestTrue(TEXT("Unlit candle models stay classified as unlit"),FWowFixturePolicy::IsUnlit(TEXT("CANDLEOFF01.M2")));
    TestEqual(TEXT("Flame card bottom meets crown"),FWowFixturePolicy::FlameCentreLift(25,false,true,false)-25,0.f);
    TestEqual(TEXT("Equipment effects retain original placement"),FWowFixturePolicy::FlameCentreLift(25,false,true,true),0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLampSourceTest,"WowUnreal.Fixtures.OriginalLanternAndCandleModels",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowLampSourceTest::RunTest(const FString&)
{
    FString Directory;
    if(!FParse::Value(FCommandLine::Get(),TEXT("-lampfixture="),Directory))
    {AddInfo(TEXT("Original model fixtures not requested"));return true;}
    for (const TCHAR* Name:{TEXT("STORMWINDSTREETLAMP01"),TEXT("GENERALHANGINGLANTERN01"),TEXT("GENERALHANGINGLANTERNLONG"),TEXT("CANDLE02"),TEXT("CANDLEOFF01"),TEXT("TORCH"),TEXT("INNCHANDELIER")})
    {
        TArray<uint8> Raw,Skin;
        if(!FFileHelper::LoadFileToArray(Raw,*(Directory/(FString(Name)+TEXT(".m2")))) ||
            !FFileHelper::LoadFileToArray(Skin,*(Directory/(FString(Name)+TEXT("00.skin")))))
        {AddError(TEXT("Missing local fixture"));return false;}
        const auto Data=FM2Parser::Parse(Raw,Skin);
        TestTrue(TEXT("Original fixture parses"),Data.bIsValid);
        if(FWowLampFixtures::IsSupported(Name))
        {
            FBox Glass;TArray<int32> Slots;
            TestTrue(TEXT("Locate glass independently of model pivot"),FWowLampFixtures::FindM2Glass(Data,Glass,Slots));
            AddInfo(FString::Printf(TEXT("%s glass centre %s, slots=%d"),Name,*Glass.GetCenter().ToString(),Slots.Num()));
            if(FString(Name)==TEXT("STORMWINDSTREETLAMP01")) TestTrue(TEXT("Street light is at the head, not 120cm above ground"),Glass.GetCenter().Z>350);
            if(FString(Name)==TEXT("GENERALHANGINGLANTERNLONG")) TestTrue(TEXT("Hanging light is below ceiling pivot"),Glass.GetCenter().Z< -200);
        }
        else
        {
            int32 Removed=0,Kept=0;
            for(const auto& Pass:Data.RenderPasses)
                if(FWowFixturePolicy::SkipLegacyPass(Data,Name,Pass))++Removed;else ++Kept;
            AddInfo(FString::Printf(TEXT("%s replacement=%d removed=%d kept=%d"),Name,FWowFixturePolicy::HasReplacementFlame(Data,Name),Removed,Kept));
            TestTrue(TEXT("Solid candle/torch geometry is preserved"),Kept>0);
            if(FWowFixturePolicy::IsUnlit(Name)) TestEqual(TEXT("No visual removed from unlit fixture"),Removed,0);
            else
            {
                TestTrue(TEXT("Lit source fixture has an active replacement flame"),FWowFixturePolicy::HasReplacementFlame(Data,Name));
                // These candle/chandelier models use particle emitters, not static flame cards.
                // Only TORCH has a legacy glow mesh among these original fixtures.
                TestEqual(TEXT("Remove the original glow mesh only where it exists"),Removed,FString(Name)==TEXT("TORCH")?1:0);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowWmoLampTest,"WowUnreal.Fixtures.WmoGlassPanelClustering",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowWmoLampTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    ON_SCOPE_EXIT{World->DestroyWorld(false);};
    auto* Actor=World->SpawnActor<AActor>();
    auto* Mesh=NewObject<UStaticMeshComponent>(Actor);Actor->SetRootComponent(Mesh);Actor->AddInstanceComponent(Mesh);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Mesh->RegisterComponent();
    auto* Mat=UMaterialInstanceDynamic::Create(FWowTerrainMaterial::GetLampGlassMaterial(),Mesh);
    Mat->SetTextureParameterValue(TEXT("Layer0Texture"),LoadObject<UTexture2D>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture")));
    Mesh->SetMaterial(0,Mat);
    FWmoRootData Root;Root.Materials.AddDefaulted();Root.Materials[0].TexturePath1=TEXT("STORMWINDLAMPGLASS.BLP");
    FWmoGroupData Group;
    Group.Vertices={FVector(0,0,4),FVector(.4,0,4),FVector(.4,0,4.4),FVector(0,0,4.4),
        FVector(0,10,4),FVector(.4,10,4),FVector(.4,10,4.4),FVector(0,10,4.4)};
    Group.Indices={0,1,2,0,2,3,4,5,6,4,6,7};Group.Batches.AddDefaulted();Group.Batches[0].IndexCount=12;
    FWowLampFixtures::AttachWmo(Actor,Mesh,Group,Root);
    TArray<UWowDuskLightComponent*> Lights;Actor->GetComponents(Lights);
    TestEqual(TEXT("One light per lantern, not per glass triangle"),Lights.Num(),2);
    for(auto* Light:Lights) TestTrue(TEXT("Light sits inside glass"),FMath::IsNearlyEqual(float(Light->GetComponentLocation().Z),420.f,.1f));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowAdditionalFixtureTest,"WowUnreal.Fixtures.AdditionalOriginalModels",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowAdditionalFixtureTest::RunTest(const FString&)
{
    FString Directory;
    if(!FParse::Value(FCommandLine::Get(),TEXT("-lampfixture="),Directory))
    {AddError(TEXT("Original fixture directory is required"));return false;}
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    ON_SCOPE_EXIT{World->DestroyWorld(false);};
    auto* Base=FWowTerrainMaterial::GetLampGlassMaterial();
    TestEqual(TEXT("Glass preserves texture cutouts"),Base->BlendMode,BLEND_Masked);
#if WITH_EDITORONLY_DATA
    TestNotNull(TEXT("Glass has a shadow-pass transmission mask"),Base->GetEditorOnlyData()->OpacityMask.Expression);
#endif
    for (const TCHAR* Name:{TEXT("LAMPPOST"),TEXT("GENERALLANTERN01"),TEXT("GENERALLANTERN02"),TEXT("GENERALLANTERN03"),
        TEXT("KALIDARSTREETLAMP01"),TEXT("KALIDARSTREETLAMP02"),TEXT("NIGHTELFHANGINGLANTERN"),TEXT("NIGHTELFLANTERN01"),TEXT("STORMWINDDWARFBRAZIER")})
    {
        const FString Path(Name);TArray<uint8> Raw,Skin;
        if(!FFileHelper::LoadFileToArray(Raw,*(Directory/(Path+TEXT(".m2")))) ||
            !FFileHelper::LoadFileToArray(Skin,*(Directory/(Path+TEXT("00.skin")))))
        {AddError(TEXT("Missing original fixture"));return false;}
        auto Data=FM2Parser::Parse(Raw,Skin);
        TestTrue(TEXT("Original fixture parses"),Data.bIsValid);
        FVector Position;FLinearColor Color;TArray<int32> Slots;
        if(!FWowLampFixtures::FindM2Source(Data,Path,Position,Color,Slots))
        {AddError(TEXT("No light source for ")+Path);continue;}
        int32 Removed=0;
        for(const auto& Pass:Data.RenderPasses) if(FWowFixturePolicy::SkipLegacyPass(Data,Path,Pass)) ++Removed;
        TestEqual(TEXT("Only Kalidar's two artificial halo/flare panels are removed"),Removed,Path.Contains(TEXT("KALIDAR"))?2:FWowFixturePolicy::IsElwynnRoadLamp(Path)?1:0);
        TestTrue(TEXT("Solid fixture body remains"),Data.RenderPasses.Num()>Removed);
        if(FWowFixturePolicy::IsElwynnRoadLamp(Path))
        {
            TestEqual(TEXT("Only lantern window glows, not post or chain"),Slots,TArray<int32>{2});
            TestTrue(TEXT("Road light uses lantern height"),Position.Z>200 && Position.Z<400);
            TestTrue(TEXT("Road lamp follows dusk"),FWowLampFixtures::IsOutdoor(Path));
        }
        if(Path.Contains(TEXT("GENERALLANTERN")))
        {
            TestTrue(TEXT("Light sits inside the window, not at the handle"),Position.Z>9 && Position.Z<78);
            TestEqual(TEXT("Only window slot glows; body/handle preserved"),Slots,TArray<int32>{1});
        }
        if(Path.Contains(TEXT("KALIDAR")))
        {
            TestTrue(TEXT("Magical core has its own source"),Position.Z>400 && Position.Z<470);
            TestTrue(TEXT("Magic tint is preserved rather than orange fire"),Color.B>Color.R);
            TestTrue(TEXT("Bark receives no glass material"),Slots.IsEmpty());
        }
        if(Path.Contains(TEXT("NIGHTELFHANGING")))
        {
            TestTrue(TEXT("Hanging source is below ceiling pivot"),Position.Z< -200);
            TestEqual(TEXT("Hanging chain keeps its material"),Slots,TArray<int32>{0});
            TestFalse(TEXT("Single magical core is not suppressed as a secondary halo"),FWowFixturePolicy::SuppressHaloEmitter(Path));
        }
        if(Path.Contains(TEXT("NIGHTELFLANTERN01")))
        {
            TestTrue(TEXT("Blue flame remains the light source"),Color.B>Color.R*2 && Position.Z>80 && Position.Z<85);
            TestTrue(TEXT("Extra glow removed while blue flame remains"),FWowFixturePolicy::SuppressHaloEmitter(Path));
        }
        if(FWowFixturePolicy::IsBrazier(Path))
        {
            TestEqual(TEXT("Only authored coal surface gets emissive material"),Slots,TArray<int32>{1});
            TestTrue(TEXT("Light sits above coal surface to avoid self-occlusion"),Position.Z>111 && Position.Z<125);
            TestEqual(TEXT("Original smoke and embers are retained"),Data.ParticleEmitters.Num(),2);
        }
        auto* Actor=World->SpawnActor<AActor>();
        auto* Mesh=NewObject<UStaticMeshComponent>(Actor);Actor->SetRootComponent(Mesh);Actor->AddInstanceComponent(Mesh);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->RegisterComponent();
        auto* Original=UMaterialInstanceDynamic::Create(Base,Mesh);
        Original->SetTextureParameterValue(TEXT("Layer0Texture"),LoadObject<UTexture2D>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture")));
        Mesh->SetMaterial(0,Original);Mesh->SetMaterial(1,Original);
        const FTransform Transform(FRotator(0,90,0),FVector(1000,2000,300),FVector(.7f));
        TestTrue(TEXT("Fixture attachment succeeds"),FWowLampFixtures::AttachM2(Actor,Mesh,Data,Path,Transform));
        auto* Effects=NewObject<UWowM2ParticleComponent>(Actor);Actor->AddInstanceComponent(Effects);
        Effects->SetupAttachment(Mesh);Effects->RegisterComponent();Effects->SetFixturePath(Path);
        Effects->InitializeFromParsedM2Data(&Data);
        TArray<UPointLightComponent*> AllLights;Actor->GetComponents(AllLights);
        TestEqual(TEXT("Particle effect does not add a duplicate orange light"),AllLights.Num(),1);
        TArray<UWowDuskLightComponent*> Lights;Actor->GetComponents(Lights);
        TestEqual(TEXT("One managed light per fixture"),Lights.Num(),1);
        if(Lights.Num()==1)
        {
            TestTrue(TEXT("Source uses placement rotation and scale"),Lights[0]->GetComponentLocation().Equals(Transform.TransformPosition(Position),.1));
            Lights[0]->ApplyTime(120);TestTrue(TEXT("Fixture casts real light at night"),Lights[0]->Intensity>0);
            Lights[0]->ApplyTime(720);TestEqual(TEXT("Only street lamps switch off during the day"),Lights[0]->Intensity==0,FWowLampFixtures::IsOutdoor(Path));
        }
        AddInfo(FString::Printf(TEXT("%s source=%s glowSlots=%d haloPassesRemoved=%d"),Name,*Position.ToString(),Slots.Num(),Removed));
        Actor->Destroy();
    }
    TestFalse(TEXT("Visible magical glowball is not a halo"),FWowFixturePolicy::IsLegacyHalo(TEXT("CREATURE/FIREELEMENTAL/GLOWBALL.BLP")));
    TestFalse(TEXT("Visible blue flame is not a halo"),FWowFixturePolicy::IsLegacyHalo(TEXT("FLAMELICKSMALLBLUE.BLP")));
    TestFalse(TEXT("Glowing embers are not a halo"),FWowFixturePolicy::IsLegacyHalo(TEXT("LAVALUMP2.BLP")));
    return true;
}

#endif
