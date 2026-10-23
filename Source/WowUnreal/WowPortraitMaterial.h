#pragma once
#include "Materials/MaterialInstanceDynamic.h"

namespace WowPortraitMaterial
{
    inline UMaterialInstanceDynamic* Create(UMaterialInterface* Source, UObject* Outer)
    {
        if (!Source) return nullptr;
        // Unreal rejects MIDs as parents. Keep the original shader/static variant,
        // then copy the live skin, equipment textures, tint and other overrides.
        UMaterialInterface* Parent = Source;
        while (auto* Dynamic = Cast<UMaterialInstanceDynamic>(Parent))
            Parent = Dynamic->Parent;
        if (!Parent) return nullptr;
        auto* Copy = UMaterialInstanceDynamic::Create(Parent, Outer);
        Copy->CopyMaterialUniformParameters(Source);
        // First-person fading belongs to the world model, never its UI portrait.
        Copy->SetScalarParameterValue(TEXT("FirstPersonOpacity"), 1.0f);
        return Copy;
    }
}
