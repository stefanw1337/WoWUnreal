#include "WowNpcServices.h"
#include "WowPacketHandler.h"
DEFINE_LOG_CATEGORY_STATIC(LogWowTrainerList, Log, All);
void FWowPacketHandler::HandleTrainerList(FPacketReader&R)
{
 const uint64 Guid=R.ReadU64();R.ReadU32();const uint32 Count=R.ReadU32();
 if(Count>4096 || !R.CanRead(Count*38+1))return;
 TArray<FWowTrainerOffer> Offers;
 for(uint32 I=0;I<Count;++I){FWowTrainerOffer O;O.Spell=R.ReadU32();O.State=R.ReadU8();O.Cost=R.ReadU32();R.Skip(8);O.Level=R.ReadU8();O.Skill=R.ReadU32();O.Rank=R.ReadU32();R.Skip(12);Offers.Add(O);}
 const FString Greeting=R.ReadCString();if(R.HasFailed()||!Guid)return;
 // r335 (Stefan 14.10: Maxan Anvol's list was empty): what the server offered, by state.
 {int32 States[3]={0,0,0};int32 Other=0;for(const auto&O:Offers){if(O.State<3)++States[O.State];else ++Other;}
  UE_LOG(LogWowTrainerList,Log,TEXT("Trainer list %llu: %d offers (available %d, unavailable %d, known %d, other %d)"),Guid,Offers.Num(),States[0],States[1],States[2],Other);}
 TrainerGuid=Guid;AuctionOpen=false;TrainerOffers=MoveTemp(Offers);ServiceMessage=Greeting;++ServiceRevision;OnNpcServiceOpened.Broadcast();
}
void FWowPacketHandler::HandleTrainerSuccess(FPacketReader&R)
{
 const uint64 Guid=R.ReadU64();const uint32 Spell=R.ReadU32();if(R.HasFailed()||Guid!=TrainerGuid)return;
 for(auto&O:TrainerOffers)if(O.Spell==Spell)O.State=2;
 ServiceMessage=TEXT("Training complete.");++ServiceRevision;
}
void FWowPacketHandler::HandleTrainerFailure(FPacketReader&R)
{
 const uint64 Guid=R.ReadU64();R.ReadU32();const uint32 Reason=R.ReadU32();if(R.HasFailed()||Guid!=TrainerGuid)return;
 ServiceMessage=FString::Printf(TEXT("Training failed (server reason %u). Check requirements and money."),Reason);++ServiceRevision;
}
void FWowPacketHandler::HandleAuctionHello(FPacketReader&R)
{
 const uint64 Guid=R.ReadU64();R.ReadU32();const bool Enabled=R.ReadU8()!=0;if(R.HasFailed()||!Guid)return;
 AuctionGuid=Guid;AuctionOpen=true;AuctionOffers.Reset();AuctionTotal=0;ServiceMessage=Enabled?TEXT("Search for an item to browse auctions."):TEXT("This auction house is unavailable.");++ServiceRevision;OnNpcServiceOpened.Broadcast();
}
// SMSG_AUCTION_LIST_RESULT / OWNER_LIST_RESULT / BIDDER_LIST_RESULT share one layout:
// u32 count, count x 148-byte entries, u32 total, u32 delay.
static bool ParseAuctionEntries(FPacketReader&R,TArray<FWowAuctionOffer>&Out,uint32&Total)
{
 const uint32 Count=R.ReadU32();if(Count>10000||!R.CanRead(Count*148+4))return false;
 for(uint32 I=0;I<Count;++I)
 {FWowAuctionOffer O;O.Id=R.ReadU32();O.Item=R.ReadU32();R.Skip(84+8);O.Count=R.ReadU32();R.Skip(8);O.Owner=R.ReadU64();O.Minimum=R.ReadU32();O.Increment=R.ReadU32();O.Buyout=R.ReadU32();O.Time=R.ReadU32();O.Bidder=R.ReadU64();O.Bid=R.ReadU32();Out.Add(O);}
 Total=R.ReadU32();return !R.HasFailed();
}
void FWowPacketHandler::HandleAuctionList(FPacketReader&R)
{
 TArray<FWowAuctionOffer> Offers;uint32 Total=0;if(!ParseAuctionEntries(R,Offers,Total))return;
 AuctionOffers=MoveTemp(Offers);AuctionTotal=Total;ServiceMessage=FString::Printf(TEXT("%u auctions found."),Total);++ServiceRevision;++AuctionListRevision;
 for(const auto&O:AuctionOffers)RequestItemTemplate(O.Item);
}
void FWowPacketHandler::HandleAuctionOwnerList(FPacketReader&R)
{
 TArray<FWowAuctionOffer> Offers;uint32 Total=0;if(!ParseAuctionEntries(R,Offers,Total))return;
 AuctionOwnOffers=MoveTemp(Offers);AuctionOwnTotal=Total;++ServiceRevision;++AuctionOwnRevision;
 for(const auto&O:AuctionOwnOffers)RequestItemTemplate(O.Item);
}
void FWowPacketHandler::HandleAuctionBidderList(FPacketReader&R)
{
 TArray<FWowAuctionOffer> Offers;uint32 Total=0;if(!ParseAuctionEntries(R,Offers,Total))return;
 AuctionBidOffers=MoveTemp(Offers);AuctionBidTotal=Total;++ServiceRevision;++AuctionBidRevision;
 for(const auto&O:AuctionBidOffers)RequestItemTemplate(O.Item);
}
void FWowPacketHandler::HandleAuctionResult(FPacketReader&R)
{
 // SMSG_AUCTION_COMMAND_RESULT: u32 auction id, u32 action (0 sell, 1 cancel, 2 bid), u32 error.
 R.ReadU32();const uint32 Action=R.ReadU32();const uint32 Error=R.ReadU32();if(R.HasFailed())return;
 static const TCHAR* Errors[]={TEXT(""),TEXT("Internal error."),TEXT("Internal error."),TEXT("Not enough money."),TEXT("Item not found."),
  TEXT("You cannot bid on your own auction."),TEXT("You do not have enough money."),TEXT("You cannot auction that item."),TEXT("Higher bid exists."),
  TEXT("Bid increment too small."),TEXT("You cannot bid on that auction."),TEXT("Auction restricted.")};
 static const TCHAR* Done[]={TEXT("Auction created."),TEXT("Auction cancelled."),TEXT("Bid accepted.")};
 ServiceMessage=Error?FString(Error<UE_ARRAY_COUNT(Errors)?Errors[Error]:TEXT("Auction request failed.")):FString(Action<3?Done[Action]:TEXT("Done."));
 LastAuctionAction=Action;LastAuctionError=Error;++AuctionResultCount;++ServiceRevision;
}
