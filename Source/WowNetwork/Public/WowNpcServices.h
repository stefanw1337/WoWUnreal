#pragma once
#include "CoreMinimal.h"
struct FWowTrainerOffer{uint32 Spell=0,Cost=0,Skill=0,Rank=0;uint8 State=0,Level=0;};
struct FWowAuctionOffer{uint32 Id=0,Item=0,Count=0,Minimum=0,Increment=0,Buyout=0,Time=0,Bid=0;uint64 Owner=0,Bidder=0;};
