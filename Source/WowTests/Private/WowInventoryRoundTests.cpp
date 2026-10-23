#include "Misc/AutomationTest.h"
#include "Misc/Compression.h"
#include "WowConnectionManager.h"
#include "WowOpcodes.h"

namespace
{
void Write32(TArray<uint8>& P, uint32 V) { for (int I=0; I<4; ++I) P.Add(uint8(V>>(8*I))); }
void Write64(TArray<uint8>& P, uint64 V) { Write32(P,uint32(V)); Write32(P,uint32(V>>32)); }
void Values(TArray<uint8>& P,uint64 Guid,const TMap<uint16,uint32>& Fields)
{
    P.Add(0); P.Add(255); Write64(P,Guid);
    uint16 Max=0; for(const auto& F:Fields)Max=FMath::Max(Max,F.Key);
    const int32 Blocks=Max/32+1; P.Add(Blocks);
    TArray<uint32> Mask;Mask.SetNumZeroed(Blocks);
    for(const auto& F:Fields)Mask[F.Key/32]|=1u<<(F.Key%32);
    for(uint32 M:Mask)Write32(P,M);
    for(int32 I=0;I<=Max;++I)if(const auto* V=Fields.Find(I))Write32(P,*V);
}
void GuidField(FWowEntity& E,uint16 Field,uint64 Guid)
{ E.SetField(Field,uint32(Guid)); E.SetField(Field+1,uint32(Guid>>32)); }
FWowEntity& Entity(FWowPacketHandler& H,uint64 Guid,uint32 Type,uint32 Entry=0)
{
    auto& E=H.EntityManager.PromoteToTyped(Guid,Type);E.SetField(ObjectField::TYPE,Type);
    E.Entry=Entry;E.SetField(ObjectField::ENTRY,Entry);return E;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryQuestCounts,"WowUnreal.Followup.InventoryQuestSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FInventoryQuestCounts::RunTest(const FString&)
{
    FWowPacketHandler H;H.EntityManager.LocalPlayerGuid=1;
    auto& P=Entity(H,1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    auto& I=Entity(H,2,WowTypeMask::OBJECT|WowTypeMask::ITEM,100);
    GuidField(I,ItemField::OWNER,1);I.SetField(ItemField::STACK_COUNT,1);
    GuidField(P,PlayerField::PACK_SLOT_START,2);
    FWowQuestLogEntry Q;Q.QuestId=10;FWowQuestObjective O;O.ItemId=100;O.Required=10;O.Text=TEXT("Test pickup");Q.Objectives.Add(O);H.QuestLog.Add(Q);
    TArray<uint8> Initial;Write32(Initial,1);Values(Initial,1,{{PlayerField::COINAGE,5}});
    int32 Notices=0,InventoryEvents=0;
    H.OnUiInfoMessage.AddLambda([&](const FString&){++Notices;});
    H.HandlePacket(WowOpcode::SMSG_UPDATE_OBJECT,Initial);
    TestEqual(TEXT("Existing inventory initializes quest count"),H.QuestLog[0].Objectives[0].Count,1u);
    TestEqual(TEXT("Login snapshot does not announce pickups"),Notices,0);
    H.OnPlayerInventoryUpdate.AddLambda([&]()
    {
        ++InventoryEvents;
        TestEqual(TEXT("Listeners see final stack, never intermediate player block"),H.EntityManager.FindItem(2)->GetStackCount(),4);
    });
    TArray<uint8> Update;Write32(Update,2);Values(Update,1,{{PlayerField::COINAGE,6}});Values(Update,2,{{ItemField::STACK_COUNT,4}});
    TArray<uint8> Compressed;Compressed.SetNum(FCompression::CompressMemoryBound(NAME_Zlib,Update.Num()));
    int32 Size=Compressed.Num();TestTrue(TEXT("Compress fixture"),FCompression::CompressMemory(NAME_Zlib,Compressed.GetData(),Size,Update.GetData(),Update.Num()));Compressed.SetNum(Size);
    TArray<uint8> Packet;Write32(Packet,Update.Num());Packet.Append(Compressed);
    H.HandlePacket(WowOpcode::SMSG_COMPRESSED_UPDATE_OBJECT,Packet);
    TestEqual(TEXT("Compressed inventory update publishes once after all blocks"),InventoryEvents,1);
    TestEqual(TEXT("Collection updates before quest completion"),H.QuestLog[0].Objectives[0].Count,4u);
    TestEqual(TEXT("One progress notification"),Notices,1);
    H.HandlePacket(WowOpcode::SMSG_COMPRESSED_UPDATE_OBJECT,Packet);
    TestEqual(TEXT("Repeated state does not repeat notification"),Notices,1);
    H.OnPlayerInventoryUpdate.Clear();
    TArray<uint8> Removal;Write32(Removal,1);Values(Removal,1,{{PlayerField::PACK_SLOT_START,0},{uint16(PlayerField::PACK_SLOT_START+1),0}});
    H.HandlePacket(WowOpcode::SMSG_UPDATE_OBJECT,Removal);
    TestEqual(TEXT("Selling/removing carried items lowers objective count"),H.QuestLog[0].Objectives[0].Count,0u);
    TestEqual(TEXT("Removal never announces a pickup"),Notices,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryCursorConfirmation,"WowUnreal.Followup.InventoryCursorConfirmation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FInventoryCursorConfirmation::RunTest(const FString&)
{
    auto* C=NewObject<UWowConnectionManager>();auto& H=C->PacketHandler;H.EntityManager.LocalPlayerGuid=1;
    auto& P=Entity(H,1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    auto& I=Entity(H,2,WowTypeMask::OBJECT|WowTypeMask::ITEM,100);GuidField(I,ItemField::OWNER,1);I.SetField(ItemField::STACK_COUNT,1);
    GuidField(P,PlayerField::PACK_SLOT_START,2);
    auto& Info=H.ItemTemplates.FindOrAdd(100);Info.Name=TEXT("Test axe");Info.Quality=3;Info.InventoryType=13;Info.Bonding=2;
    uint32 EquipToken=0;int32 Confirmations=0;FString Event;
    C->OnItemConfirmation.AddLambda([&](const FString& E,const TArray<FString>& Args){Event=E;++Confirmations;if(E.Contains(TEXT("EQUIP")))EquipToken=FCString::Atoi(*Args[0]);});
    C->PickupBagItemCursor(255,0,100);
    TestTrue(TEXT("Backpack source resolves slot 23"),C->HasCursorItemPayload());
    TestEqual(TEXT("Bag 4 maps to network slot 22"),C->NetworkBag(4),uint8(22));
    TestFalse(TEXT("No deletion before a confirmation request"),C->DeleteCursorItem());
    TestTrue(TEXT("BoE drag requests confirmation"),C->EquipCursorIntoSlot(15));
    TestEqual(TEXT("Exact equipment slot uses bind dialog"),Event,FString(TEXT("EQUIP_BIND_CONFIRM")));
    C->CancelPendingEquip(EquipToken);
    TestFalse(TEXT("Cancel releases cursor without changing inventory"),C->HasCursorItemPayload());
    TestEqual(TEXT("Cancel retains axe"),C->GetItemGuidAtPosition(255,23),uint64(2));
    C->PickupBagItemCursor(255,0,100);TestTrue(TEXT("World drop requests confirmation"),C->RequestDeleteCursorItem());
    TestEqual(TEXT("Stock delete dialog event"),Event,FString(TEXT("DELETE_ITEM_CONFIRM")));
    GuidField(P,PlayerField::PACK_SLOT_START,3);
    TestFalse(TEXT("Changed slot cannot delete replacement item"),C->DeleteCursorItem());
    C->ClearCursorPayload();GuidField(P,PlayerField::PACK_SLOT_START,2);
    C->PickupBagItemCursor(255,0,100);
    TestFalse(TEXT("Old deletion approval cannot authorize new pickup"),C->DeleteCursorItem());
    return true;
}
