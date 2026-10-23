#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeExit.h"
#include "Mpq/MpqManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowAudioManager.h"
#include "WowM2ParticleComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowCombatVoiceTimingTest,"WowUnreal.Combat.Spells.VoicePacing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowCombatVoiceTimingTest::RunTest(const FString&)
{
 TestFalse(TEXT("Rapid injury cannot interrupt an attack voice"),AWowAudioManager::ShouldPlayCombatVoice(1.5,true,0.f));
 TestFalse(TEXT("Ordinary hits can remain silent after cooldown"),AWowAudioManager::ShouldPlayCombatVoice(5,false,.7f));
 TestTrue(TEXT("Ordinary voice is possible after cooldown"),AWowAudioManager::ShouldPlayCombatVoice(3,false,.2f));
 TestTrue(TEXT("Critical injuries get a greater chance"),AWowAudioManager::ShouldPlayCombatVoice(3,true,.4f));
 TestFalse(TEXT("Critical injuries are not guaranteed either"),AWowAudioManager::ShouldPlayCombatVoice(3,true,.8f));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowHealingBurstTest,"WowUnreal.Combat.Spells.HealingBurstLifetime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowHealingBurstTest::RunTest(const FString&)
{
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);ON_SCOPE_EXIT{World->DestroyWorld(false);};
 auto* Actor=World->SpawnActor<AActor>();
 auto* Particles=NewObject<UWowM2ParticleComponent>(Actor);Actor->AddInstanceComponent(Particles);Actor->SetRootComponent(Particles);Particles->RegisterComponent();
 Particles->CreateHolyLightVeil(180.f);
 TArray<USceneComponent*> Children;Particles->GetChildrenComponents(false,Children);
 UProceduralMeshComponent* Burst=nullptr;
 for(auto* Child:Children)if(auto* Mesh=Cast<UProceduralMeshComponent>(Child))Burst=Mesh;
 if(!TestNotNull(TEXT("Healing burst geometry"),Burst))return false;
 const auto* Section=Burst->GetProcMeshSection(0);
 if(!TestNotNull(TEXT("Particle section"),Section))return false;
 TestEqual(TEXT("18 disconnected motes, no body shell"),Section->ProcVertexBuffer.Num(),72);
 TestEqual(TEXT("Two triangles per mote"),Section->ProcIndexBuffer.Num(),108);
 Particles->TickComponent(1.3f,LEVELTICK_All,nullptr);
 TestFalse(TEXT("Heal burst cannot linger after its lifetime"),Burst->IsVisible());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowCharacterSoundTest,"WowUnreal.Combat.Spells.LocalSoundReferences",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowCharacterSoundTest::RunTest(const FString&)
{
 FString Data;FParse::Value(FCommandLine::Get(),TEXT("minimapdata="),Data);
 FMpqManager Mpq;if(!TestTrue(TEXT("Local archives"),Mpq.Initialize(Data)))return false;
 FDbcStore::Get().LoadAll(Mpq);FDbcStore::Get().LoadDeferred(Mpq);
 TestEqual(TEXT("Human male combat voice"),AWowAudioManager::ResolveCharacterCue(1,0,TEXT("PlayerAttack")),uint32(2941));
 TestEqual(TEXT("Gnome male authored naming variant"),AWowAudioManager::ResolveCharacterCue(7,0,TEXT("PlayerAttack")),uint32(3275));
 TestEqual(TEXT("Human male range speech"),AWowAudioManager::ResolveCharacterCue(1,0,TEXT("_CantCastOutofRange")),uint32(1913));
 TestEqual(TEXT("Unknown race has no fabricated fallback"),AWowAudioManager::ResolveCharacterCue(99,0,TEXT("PlayerAttack")),uint32(0));
 const auto* Kit=FDbcStore::Get().SpellVisualKits().GetById(154);
 if(TestNotNull(TEXT("Holy Light impact kit"),Kit))
     TestNotNull(TEXT("Authored healing sound resolves"),FDbcStore::Get().SoundEntries().GetById(Kit->SoundID));
 return true;
}
#endif
