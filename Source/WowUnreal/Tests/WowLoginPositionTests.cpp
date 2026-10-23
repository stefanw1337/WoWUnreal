#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../WowLoginPlacement.h"
#include "Components/BoxComponent.h"
#include "Misc/ScopeExit.h"
#include "WowWorldManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/DbcParser.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLoginFloorsTest,"WowUnreal.LoginPosition.StoreysAndFeet",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowLoginFloorsTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    ON_SCOPE_EXIT{World->DestroyWorld(false);};
    for(double Z:{-350.,0.,350.,700.})
    {
        auto* Actor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Box);Actor->AddInstanceComponent(Box);Box->SetBoxExtent(FVector(500,500,10));
        Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->SetWorldLocation(FVector(0,0,Z-10));Box->RegisterComponent();
    }
    auto* Character=World->SpawnActor<ACharacter>();
    const double Half=Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    for(double Z:{-350.,0.,350.,700.})
    {
        const FVector Saved(0,0,Z);
        const auto Position=FWowLoginPlacement::Resolve(*World,*Character,Saved);
        TestTrue(TEXT("Saved floor is selected below the roof"),FMath::IsNearlyEqual(Position.Z,Z+Half+2,.1));
        Character->SetActorLocation(Position);
        TestTrue(TEXT("Outgoing feet exclude capsule height"),FWowLoginPlacement::Feet(*Character).Equals(Saved+FVector(0,0,2),.1));
    }
    const auto Corrected=FWowLoginPlacement::Resolve(*World,*Character,FVector(0,0,400));
    TestTrue(TEXT("Small old capsule-height discrepancy repaired to same floor"),FMath::IsNearlyEqual(Corrected.Z,350+Half+2,.1));
    const auto Air=FWowLoginPlacement::Resolve(*World,*Character,FVector(0,0,1500));
    TestTrue(TEXT("No distant-floor teleport when airborne"),FMath::IsNearlyEqual(Air.Z,1500+Half+2,.1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowLoginZoneTest,"WowUnreal.LoginPosition.ZoneNames",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowLoginZoneTest::RunTest(const FString&)
{
    const auto Previous=FDbcStore::Get().AreaTable();
    ON_SCOPE_EXIT{FDbcStore::Get().AreaTable()=Previous;};
    FDbcStore::Get().AreaTable()=FAreaTableDbc();
    const ANSICHAR Strings[]="\0Elwynn Forest\0Durotar\0Valley of Trials\0";
    TArray<uint8> Raw;Raw.Append(reinterpret_cast<const uint8*>("WDBC"),4);
    const uint32 Header[]={3,29,29*4,sizeof(Strings)};
    Raw.Append(reinterpret_cast<const uint8*>(Header),sizeof(Header));
    for(int32 I=0;I<3;++I)
    {
        uint32 Fields[29]={};Fields[0]=I+1;Fields[1]=I?1:0;Fields[2]=I==2?2:0;
        Fields[11]=I==0?1:I==1?15:23;
        Raw.Append(reinterpret_cast<const uint8*>(Fields),sizeof(Fields));
    }
    Raw.Append(reinterpret_cast<const uint8*>(Strings),sizeof(Strings));
    FDbcParser Parser;TestTrue(TEXT("Zone fixture parses"),Parser.Parse(Raw));
    TestTrue(TEXT("Areas load"),FDbcStore::Get().AreaTable().Load(Parser));
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    ON_SCOPE_EXIT{World->DestroyWorld(false);};
    auto* Manager=World->SpawnActor<AWowWorldManager>();
    Manager->CurrentAudioAreaId=1;
    TestEqual(TEXT("Eastern zone"),Manager->GetCurrentZoneName(),FString(TEXT("Elwynn Forest")));
    Manager->CurrentAudioAreaId=2;
    TestEqual(TEXT("Changing zone does not retain Elwynn"),Manager->GetCurrentZoneName(),FString(TEXT("Durotar")));
    Manager->CurrentAudioAreaId=3;
    TestEqual(TEXT("Minimap gets specific area"),Manager->GetCurrentAreaName(),FString(TEXT("Valley of Trials")));
    TestEqual(TEXT("Parent zone stays Durotar"),Manager->GetCurrentZoneName(),FString(TEXT("Durotar")));
    Manager->CurrentAudioAreaId=0;
    TestTrue(TEXT("Map reset clears old zone name"),Manager->GetCurrentZoneName().IsEmpty());
    return true;
}
#endif
