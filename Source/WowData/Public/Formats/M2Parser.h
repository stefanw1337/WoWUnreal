#pragma once
#include "CoreMinimal.h"
#include "Formats/M2Types.h"

class WOWDATA_API FM2Parser
{
public:
    // Bone-track headers always live in M2; external key payloads live in .anim.
    using FAnimationReader = TFunction<bool(uint16, uint16, TArray<uint8>&)>;
    static void LoadAnimationTracks(FM2Data& Model, const TArray<uint8>& Bytes,
        const FAnimationReader& ReadAnimation = {}, const TSet<int32>* AnimationIds = nullptr);
    static FM2Data Parse(const TArray<uint8>& M2Data, const TArray<uint8>& SkinData);
};
