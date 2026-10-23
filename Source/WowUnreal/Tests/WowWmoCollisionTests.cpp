#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Formats/WmoParser.h"
#include "WowWmoCollision.h"
#include "../WowPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowAuthoredWmoCollisionTest,
    "WowUnreal.Stormwind.AuthoredCollision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowAuthoredWmoCollisionTest::RunTest(const FString&)
{
    FWmoGroupData Data;
    // WMO coordinates become UE (Y,X,Z)*100: invisible walk ramp, barrier,
    // followed by a decorative face that must not block movement.
    Data.Vertices = {FVector(-1,0,0),FVector(1,0,0),FVector(1,2,.5),FVector(-1,2,.5),
        FVector(-1,3,0),FVector(1,3,0),FVector(1,3,2),FVector(-1,3,2),
        FVector(-1,5,0),FVector(1,5,0),FVector(1,5,2),FVector(-1,5,2)};
    Data.Indices={0,1,2,0,2,3,4,5,6,4,6,7,8,9,10,8,10,11};
    Data.TriangleMaterials={{0x48,0xff},{0x08,0xff},{0x08,0xff},{0x08,0xff},{0x64,0},{0x64,0}};
    // Exercise the byte parser, not just hand-populated flags.
    TArray<uint8> Payload; Payload.SetNumZeroed(68);
    auto Chunk=[](TArray<uint8>& Out,const ANSICHAR* Tag,const TArray<uint8>& Bytes)
    {
        Out.Append(reinterpret_cast<const uint8*>(Tag),4);
        const uint32 Size=Bytes.Num(); Out.Append(reinterpret_cast<const uint8*>(&Size),4); Out.Append(Bytes);
    };
    TArray<uint8> Verts,Indices,Flags;
    for (const auto& P:Data.Vertices)
    {
        const float XYZ[]={float(P.X),float(P.Y),float(P.Z)};
        Verts.Append(reinterpret_cast<const uint8*>(XYZ),sizeof(XYZ));
    }
    Indices.Append(reinterpret_cast<const uint8*>(Data.Indices.GetData()),Data.Indices.Num()*2);
    for (const auto& M:Data.TriangleMaterials) {Flags.Add(M.Flags);Flags.Add(M.MaterialId);}
    Chunk(Payload,"TVOM",Verts); Chunk(Payload,"IVOM",Indices); Chunk(Payload,"YPOM",Flags);
    TArray<uint8> File; Chunk(File,"PGOM",Payload);
    Data=FWmoParser::ParseGroup(File);
    TestTrue(TEXT("Authored collision fixture parses"),Data.bIsValid);
    TestEqual(TEXT("Every triangle retains its MOPY record"),Data.TriangleMaterials.Num(),6);
    if (Data.TriangleMaterials.Num()!=6) return false;
    TestTrue(TEXT("Invisible ramp survives despite no render batch"),Data.TriangleMaterials[0].HasCollision());
    TestFalse(TEXT("Render detail must not produce movement snags"),Data.TriangleMaterials[4].HasCollision());
    FWmoGroupData::FTriangleMaterial Solid{0x20,0},Explicit{0x0c,0},CollisionMaterial{0,0xff};
    TestTrue(TEXT("Ordinary solid render face collides"),Solid.HasCollision());
    TestTrue(TEXT("Explicit collision overrides detail"),Explicit.HasCollision());
    TestTrue(TEXT("Collision-only material collides independently"),CollisionMaterial.HasCollision());
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    ON_SCOPE_EXIT {World->DestroyWorld(false);};
    auto* Actor=World->SpawnActor<AActor>();
    auto* Root=NewObject<USceneComponent>(Actor); Actor->SetRootComponent(Root); Actor->AddInstanceComponent(Root); Root->RegisterComponent();
    TestTrue(TEXT("Authored collision attaches without visible triangles"),FWowWmoCollision::Attach(Actor,Root,Data,0));
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WmoCollisionTest),false);
    for (float X: {20.f,80.f,160.f})
    {
        FHitResult Hit;
        TestTrue(TEXT("Movement finds original invisible ramp"),World->LineTraceSingleByChannel(Hit,FVector(X,0,150),FVector(X,0,-50),ECC_Pawn,Query));
        TestTrue(TEXT("Ramp elevation is continuous"),FMath::IsNearlyEqual(float(Hit.ImpactPoint.Z),X*.25f,.1f));
    }
    FHitResult Barrier;
    TestTrue(TEXT("Capsule-width sweep is blocked by the invisible barrier"),
        World->SweepSingleByChannel(Barrier,FVector(230,0,100),FVector(370,0,100),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(35),Query));
    TestTrue(TEXT("Stops on the near side of barrier"),Barrier.Location.X<=266);
    FHitResult Detail;
    TestFalse(TEXT("Decorative geometry does not block walking"),World->LineTraceSingleByChannel(Detail,FVector(450,0,100),FVector(550,0,100),ECC_Pawn,Query));
    TArray<UProceduralMeshComponent*> Components; Actor->GetComponents(Components);
    TestEqual(TEXT("Single compact collision component per group"),Components.Num(),1);
    if (Components.Num()) TestFalse(TEXT("Collision-only ramps and walls remain invisible"),Components[0]->IsVisible());
    Data.TriangleMaterials.Empty();
    TestFalse(TEXT("Missing metadata retains caller's legacy fallback"),FWowWmoCollision::Attach(Actor,Root,Data,1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowStormwindOriginalCollisionTest,
    "WowUnreal.Stormwind.OriginalCollisionData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowStormwindOriginalCollisionTest::RunTest(const FString&)
{
    FString Directory;
    if (!FParse::Value(FCommandLine::Get(),TEXT("-wmocollisionfixture="),Directory))
    { AddInfo(TEXT("Local original-data fixture not requested")); return true; }
    int32 InvisibleFaces=0,DetailFaces=0;
    for (int32 Group=0;Group<286;++Group)
    {
        TArray<uint8> Payload;
        if (!FFileHelper::LoadFileToArray(Payload,*(Directory/FString::Printf(TEXT("group-%03d.bin"),Group))))
        { AddError(TEXT("Missing original Stormwind group fixture"));return false; }
        TArray<uint8> File={'P','G','O','M'};const uint32 Size=Payload.Num();
        File.Append(reinterpret_cast<const uint8*>(&Size),4); File.Append(Payload);
        const auto Data=FWmoParser::ParseGroup(File);
        TestTrue(TEXT("Original group parses"),Data.bIsValid);
        TestEqual(TEXT("Original triangle flags are complete"),Data.TriangleMaterials.Num(),Data.Indices.Num()/3);
        for (const auto& Face:Data.TriangleMaterials)
        {
            if (Face.MaterialId==0xff && Face.HasCollision()) ++InvisibleFaces;
            if (!Face.HasCollision()) ++DetailFaces;
        }
    }
    TestTrue(TEXT("Stormwind contains omitted invisible collision faces"),InvisibleFaces>100);
    TestTrue(TEXT("Stormwind contains non-solid rendered detail"),DetailFaces>100);
    AddInfo(FString::Printf(TEXT("Original Stormwind: %d collision-only faces and %d non-solid detail faces"),InvisibleFaces,DetailFaces));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowWmoWalkingTest,
    "WowUnreal.Stormwind.WalkingAcrossAuthoredRamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowWmoWalkingTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    ON_SCOPE_EXIT {World->DestroyWorld(false);};
    auto* Actor=World->SpawnActor<AActor>();
    auto* Root=NewObject<USceneComponent>(Actor); Actor->SetRootComponent(Root); Actor->AddInstanceComponent(Root); Root->RegisterComponent();
    FWmoGroupData Data;
    // Ground, invisible authored stair ramp, and landing. 50cm total rise.
    Data.Vertices={FVector(-2,-3,0),FVector(2,-3,0),FVector(2,0,0),FVector(-2,0,0),
        FVector(2,2,.5),FVector(-2,2,.5),FVector(2,5,.5),FVector(-2,5,.5)};
    Data.Indices={0,1,2,0,2,3,3,2,4,3,4,5,5,4,6,5,6,7};
    Data.TriangleMaterials={{0x20,0},{0x20,0},{0x08,0xff},{0x08,0xff},{0x20,0},{0x20,0}};
    TestTrue(TEXT("Walkable fixture attaches"),FWowWmoCollision::Attach(Actor,Root,Data,0));
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Pawn=World->SpawnActor<AWowPlayerCharacter>(FVector(-100,0,90),FRotator::ZeroRotator,Spawn);
    auto* Movement=Pawn->GetCharacterMovement();
    Movement->bRunPhysicsWithNoController=true;
    Movement->SetMovementMode(MOVE_Walking);
    Movement->MaxWalkSpeed=150;
    for (int32 Frame=0;Frame<180;++Frame)
    {
        Movement->AddInputVector(FVector::ForwardVector,true);
        Movement->TickComponent(1.f/60,LEVELTICK_All,&Movement->PrimaryComponentTick);
    }
    AddInfo(FString::Printf(TEXT("Walked ramp: %s mode=%d"),*Pawn->GetActorLocation().ToString(),int32(Movement->MovementMode)));
    TestTrue(TEXT("Walking crosses the stairs without jumping or getting stuck"),Pawn->GetActorLocation().X>250);
    TestTrue(TEXT("Character stands on raised landing, not through the floor"),FMath::IsNearlyEqual(float(Pawn->GetActorLocation().Z),140.f,5.f));
    TestEqual(TEXT("Character remains grounded"),Movement->MovementMode.GetValue(),MOVE_Walking);
    return true;
}

#endif
