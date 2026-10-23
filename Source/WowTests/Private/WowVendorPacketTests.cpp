#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
#include "WowOpcodes.h"
namespace
{
void U32(TArray<uint8>& P,uint32 V){for(int32 I=0;I<4;++I)P.Add(uint8(V>>(8*I)));}
void U64(TArray<uint8>& P,uint64 V){U32(P,uint32(V));U32(P,uint32(V>>32));}
void String(TArray<uint8>& P,const char* S){while(*S)P.Add(uint8(*S++));P.Add(0);}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVendorPayloadAlignment,"WowUnreal.Followup.VendorPayload",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FVendorPayloadAlignment::RunTest(const FString&)
{
    FWowPacketHandler H;
    TArray<uint8> P; U64(P,0xf13012340000ab01ULL); P.Add(1);
    for(uint32 V:{3u,6948u,6418u,0xffffffffu,125u,0u,5u,0u}) U32(P,V);
    H.HandlePacket(WowOpcode::SMSG_LIST_INVENTORY,P);
    TestEqual(TEXT("Full vendor GUID"),H.ActiveVendorGuid,0xf13012340000ab01ULL);
    TestEqual(TEXT("One item"),H.VendorItems.Num(),1);
    if(H.VendorItems.Num()==1)
    { TestEqual(TEXT("Item ID remains aligned"),H.VendorItems[0].ItemId,6948u); TestEqual(TEXT("Price is copper"),H.VendorItems[0].Price,125u);TestEqual(TEXT("Unlimited stock"),H.VendorItems[0].MaxCount,0xffffffffu);TestEqual(TEXT("Bundle count"),H.VendorItems[0].BuyCount,5u); }
    const auto Revision=H.VendorRevision; P.Pop(); H.HandlePacket(WowOpcode::SMSG_LIST_INVENTORY,P);
    TestEqual(TEXT("Truncated list cannot replace valid inventory"),H.VendorRevision,Revision);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemTooltipPayload,"WowUnreal.Followup.ItemTemplatePayload",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FItemTooltipPayload::RunTest(const FString&)
{
    FWowPacketHandler H; TArray<uint8> P;
    U32(P,12345);U32(P,4);U32(P,4);U32(P,0);String(P,"Test Chest");P.AddZeroed(3);
    for(uint32 V:{999u,2u,0u,0u,400u,100u,5u,0xffffffffu,0xffffffffu,20u,15u,
        0u,0u,0u,0u,0u,0u,0u,0u,1u,0u,2u,7u,10u,4u,6u})U32(P,V);
    P.AddZeroed(8+24);U32(P,72);P.AddZeroed(24+12+120);U32(P,2);String(P,"A useful chestpiece.");
    P.AddZeroed(36);U32(P,0);U32(P,0);U32(P,55);P.AddZeroed(16+24+4+24);
    FWowQuestLogEntry Quest; Quest.QuestId=999;
    FWowQuestObjective Objective; Objective.ItemId=12345; Objective.Required=8; Objective.Count=3;
    Quest.Objectives.Add(Objective); H.QuestLog.Add(Quest); H.QuestTemplateCache.Add(999,Quest);
    int Updates=0; H.OnQuestLogUpdated.AddLambda([&](){++Updates;});
    int Requests=0; H.OnSendPacket.BindLambda([&](uint32 Opcode,const TArray<uint8>& Data){if(Opcode==WowOpcode::CMSG_ITEM_QUERY_SINGLE && Data.Num()==12) ++Requests;});
    H.RequestItemTemplate(12345); H.RequestItemTemplate(12345);
    TestEqual(TEXT("Concurrent consumers share one query"),Requests,1);
    H.PendingItemQueries[12345]-=11;
    H.RequestItemTemplate(12345);
    TestEqual(TEXT("Unanswered item query can retry"),Requests,2);
    H.HandlePacket(WowOpcode::SMSG_ITEM_QUERY_SINGLE_RESPONSE,P);
    TestEqual(TEXT("Late name updates live quest"),H.QuestLog[0].Objectives[0].Text,FString(TEXT("Test Chest")));
    TestEqual(TEXT("Late name updates cached quest too"),H.QuestTemplateCache[999].Objectives[0].Text,FString(TEXT("Test Chest")));
    TestEqual(TEXT("Item count preserved"),H.QuestLog[0].Objectives[0].Count,3u);
    TestEqual(TEXT("UI notified"),Updates,1);
    H.HandlePacket(WowOpcode::SMSG_ITEM_QUERY_SINGLE_RESPONSE,P);
    TestEqual(TEXT("Duplicate response does not rebuild quests"),Updates,1);
    H.RequestItemTemplate(12345); TestEqual(TEXT("Cached item does not query again"),Requests,2);
    const auto* Item=H.ItemTemplates.Find(12345);
    TestNotNull(TEXT("Valid template published"),Item);
    if(Item){TestEqual(TEXT("Server name"),Item->Name,FString(TEXT("Test Chest")));TestEqual(TEXT("Armor"),Item->Armor,72u);TestEqual(TEXT("Stats count"),Item->Stats.Num(),2);TestEqual(TEXT("Sell copper"),Item->SellPrice,100u);TestEqual(TEXT("Durability"),Item->MaxDurability,55u);}
    const auto Revision=H.VendorRevision;P.Pop();H.HandlePacket(WowOpcode::SMSG_ITEM_QUERY_SINGLE_RESPONSE,P);
    TestEqual(TEXT("Incomplete template not published"),H.VendorRevision,Revision);
    return true;
}
