#include "WowActorFadeComponent.h"
#include "WowCookedMaterial.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "UObject/StrongObjectPtr.h"

UWowActorFadeComponent::UWowActorFadeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

UMaterialInterface* UWowActorFadeComponent::PrepareMaterial()
{
    static TStrongObjectPtr<UMaterial> Retained;
    if (Retained.IsValid()) return Retained.Get();
    const FString Name = TEXT("M_WowFadeOut_v1");
    UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage());
        Material->BlendMode = BLEND_Translucent;
        Material->TwoSided = true;
        Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
        auto* Sampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
        Sampler->ParameterName = FName(TEXT("Layer0Texture"));
        Sampler->SamplerType = SAMPLERTYPE_Color;
        Sampler->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
        auto* Fade = NewObject<UMaterialExpressionScalarParameter>(Material);
        Fade->ParameterName = FName(TEXT("Fade"));
        Fade->DefaultValue = 1.0f;
        auto* Opacity = NewObject<UMaterialExpressionCustom>(Material);
        // The masked cut-out of the original pass, scaled by the fade.
        Opacity->Code = TEXT("return saturate((A - 0.3) * 20.0) * F;");
        Opacity->OutputType = CMOT_Float1;
        FCustomInput InA; InA.InputName = TEXT("A"); InA.Input.Connect(4, Sampler); Opacity->Inputs.Add(InA);
        FCustomInput InF; InF.InputName = TEXT("F"); InF.Input.Connect(0, Fade); Opacity->Inputs.Add(InF);
        auto* Rough = NewObject<UMaterialExpressionConstant>(Material);
        Rough->R = 0.8f;
        for (UMaterialExpression* E : {static_cast<UMaterialExpression*>(Sampler), static_cast<UMaterialExpression*>(Fade),
            static_cast<UMaterialExpression*>(Opacity), static_cast<UMaterialExpression*>(Rough)})
            Material->GetExpressionCollection().AddExpression(E);
        Material->GetEditorOnlyData()->BaseColor.Connect(0, Sampler);
        Material->GetEditorOnlyData()->Opacity.Connect(0, Opacity);
        Material->GetEditorOnlyData()->Roughness.Connect(0, Rough);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, Name);
    }
#endif
    if (Material) Retained.Reset(Material);
    return Material;
}

bool UWowActorFadeComponent::FadeOutAndDestroy(AActor* Actor, float Seconds)
{
    if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) return false;
    UMaterialInterface* Base = PrepareMaterial();
    if (!Base) return false;
    UWowActorFadeComponent* Fader = NewObject<UWowActorFadeComponent>(Actor, TEXT("WowFadeOut"));
    Fader->Duration = FMath::Max(Seconds, 0.05f);
    Actor->SetActorEnableCollision(false);
    TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
    for (UPrimitiveComponent* Primitive : Primitives)
    {
        if (!Primitive || !Primitive->IsVisible()) continue;
        UMeshComponent* Mesh = Cast<UMeshComponent>(Primitive);
        bool bFaded = false;
        if (Mesh && Mesh->GetNumMaterials() > 0)
        {
            for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
            {
                UMaterialInterface* Source = Mesh->GetMaterial(Slot);
                UTexture* Texture = nullptr;
                if (!Source || !Source->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Layer0Texture")), Texture) || !Texture)
                {
                    // Glows and other untextured passes: just go with the start of the fade.
                    continue;
                }
                UMaterialInstanceDynamic* Fade = UMaterialInstanceDynamic::Create(Base, Fader);
                Fade->SetTextureParameterValue(TEXT("Layer0Texture"), Texture);
                Fade->SetScalarParameterValue(TEXT("Fade"), 1.f);
                Mesh->SetMaterial(Slot, Fade);
                Fader->Fades.Add(Fade);
                bFaded = true;
            }
        }
        if (!bFaded) Primitive->SetVisibility(false);
        else Primitive->SetCastShadow(false); // a translucent pass would leave a solid shadow behind
    }
    Actor->AddInstanceComponent(Fader);
    Fader->RegisterComponent();
    Actor->SetLifeSpan(Fader->Duration + 0.1f);
    return true;
}

void UWowActorFadeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    Age += DeltaTime;
    const float Value = FMath::Clamp(1.f - Age / Duration, 0.f, 1.f);
    for (UMaterialInstanceDynamic* Fade : Fades)
        if (Fade) Fade->SetScalarParameterValue(TEXT("Fade"), Value);
    if (Age >= Duration && GetOwner()) GetOwner()->Destroy();
}
