#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Formats/WmoParser.h"
#include "WowWmoCollision.h"
#include "../WowPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowOriginalCurbAudit,"WowUnreal.Stormwind.OriginalCurbAudit",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowOriginalCurbAudit::RunTest(const FString&)
{
    FString Directory;
    if(!FParse::Value(FCommandLine::Get(),TEXT("wmocollisionfixture="),Directory))return false;
    struct FCase {int32 Group;FVector Start,End;};
    const TArray<FCase> Cases={
        {94,FVector(-99.8460464,226.9244843,-3.4157853),FVector(-101.2785755,226.2118427,-3.0632410)},
        {94,FVector(-105.2707520,218.2678223,-4.6507511),FVector(-106.1309657,219.6169106,-4.3150640)},
        {94,FVector(-113.3415909,210.3565521,-5.0239654),FVector(-114.2425160,211.6787924,-4.7009921)},
        {94,FVector(-125.1698608,205.5339813,-5.2461004),FVector(-124.2091092,206.8134250,-4.8580294)},
        {94,FVector(-127.7247620,271.9235229,1.4664180),FVector(-127.5632344,273.5153343,1.8648677)},
        {94,FVector(-116.0569229,271.4382324,1.6471282),FVector(-115.5950268,272.9701023,2.0273945)},
        {94,FVector(-105.4640198,270.1970215,0.5426693),FVector(-103.8655206,270.1278143,0.8617816)},
        {94,FVector(-100.5381470,195.4432068,-3.4114201),FVector(-99.8628262,193.9927073,-2.9486670)},
        {95,FVector(-376.4653320,30.0272732,-5.2397451),FVector(-376.4653313,31.6272724,-4.8743486)},
        {95,FVector(-355.7752686,11.6402664,-5.1690779),FVector(-357.3752564,11.6402664,-4.7008991)},
        {95,FVector(-385.1928406,5.2166677,-5.3063712),FVector(-383.5928528,5.2166677,-4.7919168)},
        {95,FVector(-423.0344543,-32.6277313,-5.2960825),FVector(-423.3187096,-31.0531856,-4.9842930)},
        {95,FVector(-415.0495605,65.3191452,-5.3063698),FVector(-415.1131639,66.9178826,-5.0648136)},
        {95,FVector(-398.6701660,-17.5412560,-5.3063712),FVector(-400.1529629,-16.9401848,-4.9842954)},
        {95,FVector(-438.7459717,-40.0436974,-5.1555247),FVector(-438.7459712,-41.6436966,-4.9092889)},
        {95,FVector(-348.3794861,58.0616951,-5.2981734),FVector(-346.7794983,58.0616951,-4.9842954)},
        {257,FVector(-150.8920135,-42.4932480,-4.0292935),FVector(-150.6531870,-40.9111715,-3.7176778)},
        {257,FVector(-140.4645996,-29.7839317,-5.4876809),FVector(-142.0420805,-30.0514458,-5.1499596)},
        {257,FVector(-108.1700439,38.2070198,-7.4521756),FVector(-106.5725098,38.2958174,-7.1959028)},
        {257,FVector(-54.6207085,-13.2310429,-8.8111076),FVector(-54.5062152,-14.8269409,-8.4797134)},
        {257,FVector(-123.4163742,32.5424728,-5.3411837),FVector(-122.9410169,31.0147174,-4.9987044)},
        {257,FVector(-114.5642166,-44.4587517,-6.1272225),FVector(-113.0245405,-44.8939390,-5.8649888)},
        {257,FVector(-177.7671356,-11.7114916,-6.0812783),FVector(-177.2274019,-13.2177078,-5.7822218)},
        {257,FVector(-194.9368744,-16.5178623,0.3563523),FVector(-194.9947065,-14.9189085,0.6597174)},
    };
    int32 Blocked=0;
    for(int32 GroupIndex:{94,95,257})
    {
        TArray<uint8> Payload,File={'P','G','O','M'};
        if(!FFileHelper::LoadFileToArray(Payload,*(Directory/FString::Printf(TEXT("group-%03d.bin"),GroupIndex))))return false;
        const uint32 Size=Payload.Num();File.Append(reinterpret_cast<const uint8*>(&Size),4);File.Append(Payload);
        const auto Data=FWmoParser::ParseGroup(File);
        const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
        auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        ON_SCOPE_EXIT{World->DestroyWorld(false);};
        auto* Actor=World->SpawnActor<AActor>();auto* Root=NewObject<USceneComponent>(Actor);
        Actor->SetRootComponent(Root);Actor->AddInstanceComponent(Root);Root->RegisterComponent();
        if(!FWowWmoCollision::Attach(Actor,Root,Data,GroupIndex))return false;
        const auto UE=[](FVector P){return FVector(P.Y,P.X,P.Z)*100;};
        for(const auto& C:Cases)
        {
            if(C.Group!=GroupIndex)continue;
            const FVector Start=UE(C.Start),End=UE(C.End);
            const FVector Direction=(End-Start).GetSafeNormal2D();
            FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Pawn=World->SpawnActor<AWowPlayerCharacter>(Start+FVector(0,0,90),Direction.Rotation(),Spawn);
            auto* Movement=Pawn->GetCharacterMovement();Movement->bRunPhysicsWithNoController=true;
            Movement->SetMovementMode(MOVE_Walking);Movement->MaxWalkSpeed=150;
            const double Required=FVector::DotProduct(End-Start,Direction);
            for(int32 Frame=0;Frame<100;++Frame)
            {
                if(FVector::DotProduct(Pawn->GetActorLocation()-Start,Direction)>=Required-10)break;
                Movement->AddInputVector(Direction,true);
                Movement->TickComponent(1.f/60,LEVELTICK_All,&Movement->PrimaryComponentTick);
            }
            const double Progress=FVector::DotProduct(Pawn->GetActorLocation()-Start,Direction);
            const bool Passed=Progress>=Required-10 && Movement->IsMovingOnGround();
            if(!Passed)++Blocked;
            if(GroupIndex==95 && FMath::Abs(C.Start.X+355.7752686)<.01)
                TestTrue(TEXT("Original short steep curb is traversable without jumping"),Passed);
            AddInfo(FString::Printf(TEXT("CURB group=%d start=%s end=%s progress=%.1f/%.1f final=%s mode=%d passed=%d"),GroupIndex,*C.Start.ToString(),*C.End.ToString(),Progress,Required,*Pawn->GetActorLocation().ToString(),int32(Movement->MovementMode),Passed));
            Pawn->Destroy();
        }
    }
    AddInfo(FString::Printf(TEXT("AUDIT ONLY: %d/%d candidate road-to-sidewalk routes blocked; geometry must be inspected before deciding each is a defect."),Blocked,Cases.Num()));
    return true;
}
#endif
