#pragma once
#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
struct FWowLoginPlacement
{
    static FVector Feet(const ACharacter& Character)
    { return Character.GetActorLocation()-FVector(0,0,Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()); }
    static FVector Resolve(UWorld& World,ACharacter& Character,const FVector& SavedFeet)
    {
        // The server stores feet, not the centre of Unreal's capsule. Only repair
        // small floor discrepancies around that height; never search from the sky.
        FVector FeetPosition=SavedFeet;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(LoginFloor),true,&Character);
        FHitResult Hit;
        if(World.LineTraceSingleByChannel(Hit,SavedFeet+FVector(0,0,40),SavedFeet-FVector(0,0,150),ECC_Pawn,Query)
            && Hit.ImpactNormal.Z>=.7f)
            FeetPosition.Z=Hit.ImpactPoint.Z;
        // No nearby floor can mean swimming or falling. Preserve the saved height
        // instead of moving to a different storey or blocking login indefinitely.
        return FeetPosition+FVector(0,0,Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2);
    }
};
