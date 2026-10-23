#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
#include "Formats/Dbc/WorldMapData.h"
namespace{void N32(TArray<uint8>&B,uint32 V){for(int I=0;I<4;++I)B.Add(uint8(V>>(8*I)));}void N64(TArray<uint8>&B,uint64 V){N32(B,uint32(V));N32(B,uint32(V>>32));}}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcServiceWireTest,"WowUnreal.Windows.NpcServices",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FNpcServiceWireTest::RunTest(const FString&)
{
 FWowPacketHandler H;TArray<uint8>B;N64(B,123);N32(B,2);N32(B,1);
 N32(B,2018);B.Add(0);N32(B,100);B.AddZeroed(8);B.Add(5);N32(B,164);N32(B,75);B.AddZeroed(12);B.Add(0);
 H.HandlePacket(0x1B1,B);TestEqual(TEXT("Trainer GUID"),H.TrainerGuid,uint64(123));TestEqual(TEXT("Offer count"),H.TrainerOffers.Num(),1);
 if(H.TrainerOffers.Num()){TestEqual(TEXT("Skill aligned"),H.TrainerOffers[0].Skill,164u);TestEqual(TEXT("Rank aligned"),H.TrainerOffers[0].Rank,75u);TestEqual(TEXT("Cost aligned"),H.TrainerOffers[0].Cost,100u);}
 auto Revision=H.ServiceRevision;B.Pop();H.HandlePacket(0x1B1,B);TestEqual(TEXT("Truncation preserves trainer"),H.ServiceRevision,Revision);
 B.Reset();N32(B,1);N32(B,17);N32(B,6948);B.AddZeroed(92);N32(B,3);B.AddZeroed(8);N64(B,456);N32(B,100);N32(B,5);N32(B,200);N32(B,30000);N64(B,0);N32(B,150);N32(B,99);N32(B,0);
 H.HandlePacket(0x25C,B);TestEqual(TEXT("Auction count"),H.AuctionOffers.Num(),1);
 if(H.AuctionOffers.Num()){TestEqual(TEXT("Auction stack"),H.AuctionOffers[0].Count,3u);TestEqual(TEXT("Auction buyout"),H.AuctionOffers[0].Buyout,200u);TestEqual(TEXT("Auction owner"),H.AuctionOffers[0].Owner,uint64(456));TestEqual(TEXT("Auction bid"),H.AuctionOffers[0].Bid,150u);}
 TestEqual(TEXT("Total auction results"),H.AuctionTotal,99u);
 Revision=H.ServiceRevision;B.SetNum(20);H.HandlePacket(0x25C,B);TestEqual(TEXT("Truncated auction preserves results"),H.ServiceRevision,Revision);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapProjectionDataTest,"WowUnreal.Windows.MapProjection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FMapProjectionDataTest::RunTest(const FString&)
{
 FWowMapArea A;A.Left=1500;A.Right=-2000;A.Top=-8000;A.Bottom=-10000;
 TestTrue(TEXT("Map northwest"),A.Project(-8000,1500).Equals(FVector2D(0,0)));
 TestTrue(TEXT("Map center"),A.Project(-9000,-250).Equals(FVector2D(.5,.5)));
 TestTrue(TEXT("Map southeast"),A.Project(-10000,-2000).Equals(FVector2D(1,1)));
 FWowWorldMapData D;D.LoadSatellite(TEXT("dir: Azeroth\r\nAzeroth\\map32_48.blp\t123.blp\r\r\n"));
 TestEqual(TEXT("Satellite lookup case normalized"),D.Satellite.FindRef(TEXT("azeroth\\map32_48.blp")),FString(TEXT("123.blp")));
 return true;
}

#include "WowConnectionManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Misc/ScopeExit.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponItemTargetTest,"WowUnreal.Windows.WeaponEnchantTarget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FWeaponItemTargetTest::RunTest(const FString&)
{
 auto Old=FDbcStore::Get().Spells();ON_SCOPE_EXIT{FDbcStore::Get().Spells()=MoveTemp(Old);};
 TArray<uint8>B;N32(B,0x43424457);N32(B,1);N32(B,226);N32(B,226*4);N32(B,1);
 for(uint32 I=0;I<226;++I)N32(B,I==0?8017:I==72?54:0);B.Add(0);
 FDbcParser P;if(!TestTrue(TEXT("Spell fixture"),P.Parse(B)))return false;FDbcStore::Get().Spells().Load(P);
 auto*C=NewObject<UWowConnectionManager>();auto&E=C->PacketHandler.EntityManager;E.LocalPlayerGuid=1;
 auto&Player=E.PromoteToTyped(1,WowTypeMask::PLAYER|WowTypeMask::UNIT|WowTypeMask::OBJECT);
 auto&Main=E.PromoteToTyped(50,WowTypeMask::ITEM|WowTypeMask::OBJECT);Main.SetField(ItemField::OWNER,1);
 Player.SetField(PlayerField::INV_SLOT_HEAD+15*2,50);
 TestEqual(TEXT("Rockbiter uses equipped item, not enemy selection"),C->ResolveHelpfulSpellTarget(8017,99),int64(50));
 auto&Off=E.PromoteToTyped(51,WowTypeMask::ITEM|WowTypeMask::OBJECT);Off.SetField(ItemField::OWNER,1);Off.Entry=123;
 C->PacketHandler.ItemTemplates.FindOrAdd(123).Class=2;Player.SetField(PlayerField::INV_SLOT_HEAD+16*2,51);
 TestEqual(TEXT("Dual wield asks for hand"),C->ResolveHelpfulSpellTarget(8017,99),int64(-1));
 TestEqual(TEXT("Explicit offhand preserved"),C->ResolveHelpfulSpellTarget(8017,51),int64(51));
 C->PendingTargetSpell=8017;C->PendingEnchantItemGuid=77;C->CancelSpellTarget();
 TestEqual(TEXT("Escape clears pending poison source"),C->PendingEnchantItemGuid,uint64(0));
 return true;
}
