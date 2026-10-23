#pragma once
#include "CoreMinimal.h"

class UMaterial;
class UMaterialInstanceDynamic;
class UWorld;

// r535 (Stefan: "replace all cloth material with actual cloth material", "cloth can be used to indicate
// wind as well", "also tents"): banners, flags, tapestries, tents, awnings and rugs get UE's cloth
// shading (soft sheen, no hard plastic highlight). Hanging and stretched cloth on doodads (M2) also
// moves in the weather's wind; building (WMO) cloth only gets the shading, so it never parts from
// the stone and timber it is fixed to.
namespace WowCloth
{
    enum class EKind : uint8
    {
        None,      // not cloth
        Hanging,   // banners, flags, tapestries, curtains: fixed at the top, the lower edge swings
        Stretched, // tents, awnings, canvas: fixed at the bottom and the top, the middle billows
        Flat       // rugs, blankets, bedrolls: cloth shading, no movement
    };

    /** Cloth or not, from the model path and the texture path (lower or mixed case). */
    WOWWORLD_API EKind Classify(const FString& ModelPath, const FString& TexturePath);

    /** Cooked M_WowClothObject (two-sided, masked, cloth shading, wind offset). Null if not cooked. */
    WOWWORLD_API UMaterial* GetMaterial();

    /** Sets kind, alpha cut-out and the mesh's local height range (cm) for the sway weighting. */
    WOWWORLD_API void Configure(UMaterialInstanceDynamic* MID, EKind Kind, bool bAlphaCut, float LocalMinZ, float LocalHeight);

    /** Wind for all cloth (unit direction in the XY plane, strength 0..1). Cheap; call every tick. */
    WOWWORLD_API void SetWind(UWorld* World, const FVector2D& Direction, float Strength);

    /** Editor/commandlet: build and save MPC_WowWind and M_WowClothObject. */
    WOWWORLD_API void Prepare();
}
