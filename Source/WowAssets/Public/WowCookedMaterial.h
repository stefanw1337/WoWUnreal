#pragma once
#include "CoreMinimal.h"
class UMaterial;
// Persist only project-owned shader graphs. MPQ textures remain runtime parameters.
namespace WowCookedMaterial
{
    WOWASSETS_API UMaterial* Load(const FString& Name);
    WOWASSETS_API UMaterial* Save(UMaterial* Material, const FString& Name);
}
