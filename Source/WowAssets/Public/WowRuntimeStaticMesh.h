#pragma once
#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "WowRuntimeStaticMesh.generated.h"
/** MPQ geometry has no cooked collision archive. Chaos needs its game world to cook it at runtime. */
UCLASS(Transient)
class WOWASSETS_API UWowRuntimeStaticMesh : public UStaticMesh
{
    GENERATED_BODY()
public:
    virtual UWorld* GetWorld() const override
    {
        return !HasAnyFlags(RF_ClassDefaultObject) && GetOuter() ? GetOuter()->GetWorld() : nullptr;
    }
};
