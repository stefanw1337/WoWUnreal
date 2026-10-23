#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
#include "WowOpcodes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuestObjectiveNameResponse,"WowUnreal.Quest.ObjectiveNames",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FQuestObjectiveNameResponse::RunTest(const FString&)
{
 FWowPacketHandler Handler;
 FWowQuestLogEntry Quest; Quest.QuestId=52;
 FWowQuestObjective Wolf; Wolf.CreatureOrGOId=118; Wolf.Count=1; Wolf.Required=8;
 Quest.Objectives.Add(Wolf);
 Handler.QuestLog.Add(Quest); Handler.QuestTemplateCache.Add(52,Quest);
 int Updates=0; Handler.OnQuestLogUpdated.AddLambda([&](){++Updates;});
 TArray<uint8> Packet; uint32 Entry=118; Packet.Append(reinterpret_cast<uint8*>(&Entry),4);
 const char Name[]="Prowler"; Packet.Append(reinterpret_cast<const uint8*>(Name),sizeof(Name)); Packet.Add(0);
 Handler.HandlePacket(WowOpcode::SMSG_CREATURE_QUERY_RESPONSE,Packet);
 TestEqual(TEXT("Delayed creature response resolves live objective"),Handler.QuestLog[0].Objectives[0].Text,FString(TEXT("Prowler")));
 TestEqual(TEXT("Cached template also resolves"),Handler.QuestTemplateCache[52].Objectives[0].Text,FString(TEXT("Prowler")));
 TestEqual(TEXT("Existing kill count survives"),Handler.QuestLog[0].Objectives[0].Count,uint32(1));
 TestEqual(TEXT("Quest UI notified when name arrives"),Updates,1);
 Handler.HandlePacket(WowOpcode::SMSG_CREATURE_QUERY_RESPONSE,Packet);
 TestEqual(TEXT("Duplicate response does not rebuild UI"),Updates,1);
 return true;
}
