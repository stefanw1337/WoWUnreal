#pragma once
#include "CoreMinimal.h"

// Asset language selection is independent of the custom/HD asset profile.
namespace WowLocale
{
    WOWDATA_API FString Get();
    WOWDATA_API uint32 DbcIndex();
    WOWDATA_API uint32 DbcIndexForLocale(const FString& Code);
}
