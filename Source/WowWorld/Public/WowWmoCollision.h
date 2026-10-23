#pragma once
#include "CoreMinimal.h"
class AActor;
class USceneComponent;
struct FWmoGroupData;

struct WOWWORLD_API FWowWmoCollision
{
    // False means no complete authored data; caller retains legacy collision.
    static bool Attach(AActor* Owner, USceneComponent* Parent,
        const FWmoGroupData& Group, uint32 GroupIndex);
};
