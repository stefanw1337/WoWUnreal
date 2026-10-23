#pragma once
#include "CoreMinimal.h"
#include "Formats/AdtTypes.h"

struct WOWDATA_API FWdtData
{
    bool TileExists[64][64] = {};
    uint32 Flags = 0;
    bool bUseBigAlpha = false;
    FString GlobalWmoPath;
    /** r556: the MODF entry of a WMO-only map (dungeons such as the Stockade): where the global WMO
     *  stands. Same layout as an ADT MODF entry. */
    FAdtWmoPlacement GlobalWmoPlacement;
    bool bHasGlobalWmoPlacement = false;
    bool bIsValid = false;
};
