#include "UObject/StrongObjectPtr.h"
#include "Misc/ScopeExit.h"
#include "WowTerrainMaterial.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "Engine/Texture2D.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionShadowReplace.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#endif
static UMaterial* GetFixtureMaterial(bool OpaqueWindow)
{
    static TStrongObjectPtr<UMaterial> Materials[2];
    auto& RetainedMaterial=Materials[OpaqueWindow?1:0];
    UMaterial* Material=RetainedMaterial.Get();
    ON_SCOPE_EXIT { RetainedMaterial.Reset(Material); };
    if (Material) return Material;
    const TCHAR* Name=OpaqueWindow?TEXT("/Game/Materials/M_WowOpaqueWindowLightV1"):TEXT("/Game/Materials/M_WowLampGlassV2");
    if (FPackageName::DoesPackageExist(Name)) Material=LoadObject<UMaterial>(nullptr,Name);
    if (Material) return Material;
#if WITH_EDITOR
    auto* Package=CreatePackage(Name);
    Material=NewObject<UMaterial>(Package,OpaqueWindow?TEXT("M_WowOpaqueWindowLightV1"):TEXT("M_WowLampGlassV2"),RF_Public|RF_Standalone);
    Material->SetShadingModel(MSM_DefaultLit);Material->BlendMode=OpaqueWindow?BLEND_Opaque:BLEND_Masked;Material->TwoSided=true;
    Material->bUsedWithInstancedStaticMeshes=true;Material->bUsedWithSkeletalMesh=true;
    auto* Texture=NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
    Texture->ParameterName=TEXT("Layer0Texture");Texture->SamplerType=SAMPLERTYPE_Color;
    Texture->Texture=LoadObject<UTexture2D>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture"));
    auto* Glow=NewObject<UMaterialExpressionScalarParameter>(Material);
    Glow->ParameterName=TEXT("LampGlow");Glow->DefaultValue=0;
    auto* Mask=NewObject<UMaterialExpressionVectorParameter>(Material);
    Mask->ParameterName=TEXT("LampEmissionMask");Mask->DefaultValue=FLinearColor(0,0,0,1);
    auto* Dot=NewObject<UMaterialExpressionDotProduct>(Material);Dot->A.Connect(0,Texture);Dot->B.Connect(0,Mask);
    auto* Bias=NewObject<UMaterialExpressionAdd>(Material);Bias->A.Connect(0,Dot);Bias->B.Connect(4,Mask);
    auto* Clamp=NewObject<UMaterialExpressionClamp>(Material);Clamp->Input.Connect(0,Bias);
    auto* Selected=NewObject<UMaterialExpressionMultiply>(Material);Selected->A.Connect(0,Texture);Selected->B.Connect(0,Clamp);
    auto* Emission=NewObject<UMaterialExpressionMultiply>(Material);
    Emission->A.Connect(0,Selected);Emission->B.Connect(0,Glow);
    auto& Graph=Material->GetExpressionCollection();Graph.AddExpression(Texture);Graph.AddExpression(Glow);Graph.AddExpression(Emission);
    Graph.AddExpression(Mask);Graph.AddExpression(Dot);Graph.AddExpression(Bias);Graph.AddExpression(Clamp);Graph.AddExpression(Selected);
    // Glass transmits the local raster-shadowed light; frames still cast shadows.
    auto* FrameMask=NewObject<UMaterialExpressionOneMinus>(Material);FrameMask->Input.Connect(0,Clamp);
    auto* ShadowAlpha=NewObject<UMaterialExpressionMultiply>(Material);ShadowAlpha->A.Connect(4,Texture);ShadowAlpha->B.Connect(0,FrameMask);
    auto* ShadowSwitch=NewObject<UMaterialExpressionShadowReplace>(Material);ShadowSwitch->Default.Connect(4,Texture);ShadowSwitch->Shadow.Connect(0,ShadowAlpha);
    Graph.AddExpression(FrameMask);Graph.AddExpression(ShadowAlpha);Graph.AddExpression(ShadowSwitch);
    if (!OpaqueWindow) Material->GetEditorOnlyData()->OpacityMask.Connect(0,ShadowSwitch);
    Material->GetEditorOnlyData()->BaseColor.Connect(0,Texture);
    Material->GetEditorOnlyData()->EmissiveColor.Connect(0,Emission);
    Material->PreEditChange(nullptr);Material->PostEditChange();
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
    const FString Filename=FPackageName::LongPackageNameToFilename(Name,FPackageName::GetAssetPackageExtension());
    UPackage::SavePackage(Package,Material,*Filename,Args);
#endif
    return Material;
}

UMaterial* FWowTerrainMaterial::GetLampGlassMaterial() { return GetFixtureMaterial(false); }
UMaterial* FWowTerrainMaterial::GetOpaqueWindowLightMaterial() { return GetFixtureMaterial(true); }
