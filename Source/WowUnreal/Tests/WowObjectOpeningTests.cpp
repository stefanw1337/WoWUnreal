#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
#include "WowOpcodes.h"
#include "WowM2ParticleComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowObjectQueryTest,"WowUnreal.Interaction.ObjectQuery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowObjectQueryTest::RunTest(const FString&)
{
 FWowPacketHandler Handler;
 TArray<uint8> Packet;
 auto U32=[&](uint32 Value){ Packet.Append(reinterpret_cast<const uint8*>(&Value),4); };
 U32(1234); U32(3); U32(99);
 for(int I=0;I<7;++I) Packet.Add(0);
 U32(42); for(int I=1;I<24;++I)U32(0);
 bool Notified=false; Handler.OnGameObjectInfo=[&](uint32 Entry){Notified=Entry==1234;};
 Handler.HandlePacket(WowOpcode::SMSG_GAMEOBJECT_QUERY_RESPONSE,Packet);
 TestEqual(TEXT("Chest lock extracted after all seven strings"),Handler.GameObjectLocks.FindRef(1234),uint32(42));
 TestTrue(TEXT("Waiting interaction receives template"),Notified);
 Handler.GameObjectLocks.Reset(); Packet.SetNum(Packet.Num()-4);
 Handler.HandlePacket(WowOpcode::SMSG_GAMEOBJECT_QUERY_RESPONSE,Packet);
 TestEqual(TEXT("Truncated template is not cached as unlocked"),Handler.GameObjectLocks.Num(),0);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowQuestSparkleTest,"WowUnreal.Interaction.QuestSparkleLifetime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowQuestSparkleTest::RunTest(const FString&)
{
 auto* World=UWorld::CreateWorld(EWorldType::Game,false); ON_SCOPE_EXIT{World->DestroyWorld(false);};
 auto* Actor=World->SpawnActor<AActor>();
 auto* Particles=NewObject<UWowM2ParticleComponent>(Actor); Actor->AddInstanceComponent(Particles); Actor->SetRootComponent(Particles); Particles->RegisterComponent();
 Particles->CreateQuestSparkles(90);
 TArray<USceneComponent*> Children; Particles->GetChildrenComponents(false,Children);
 UProceduralMeshComponent* Burst=nullptr;
 for(auto* Child:Children)if(auto* Mesh=Cast<UProceduralMeshComponent>(Child))Burst=Mesh;
 if(!TestNotNull(TEXT("Quest glitter geometry"),Burst))return false;
 Particles->TickComponent(3.5f,LEVELTICK_All,nullptr);
 TestTrue(TEXT("Quest glitter persists beyond healing burst lifetime"),Burst->IsVisible());
 TestEqual(TEXT("No blocking collision"),Burst->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
 return true;
}
#endif
