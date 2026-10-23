#include "WowTerrainMaterial.h"
#include "WowWeatherComponent.h"
#include "WowClothMaterial.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/ScopeExit.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionPixelNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#endif

void FWowTerrainMaterial::SetRainWetness(UWorld* World, float Amount)
{
    static TStrongObjectPtr<UMaterialParameterCollection> Collection(LoadObject<UMaterialParameterCollection>(nullptr,TEXT("/Game/Materials/MPC_WowRain.MPC_WowRain")));
    if (World && Collection.IsValid()) if (auto* Instance=World->GetParameterCollectionInstance(Collection.Get()))
        Instance->SetScalarParameterValue(TEXT("Wetness"),FMath::Clamp(Amount,0.f,1.f));
}
UMaterial* WowGetCobblestonePomMaterial(); // r704: WowCobblestoneMaterial.cpp (built after MPC_WowRain exists)
void WowPrepareTerrainStoneRelief(UMaterial* Terrain); // r732: WowCobblestoneMaterial.cpp
void WowPrepareM2LayeredMaterials(); // r739: WowM2LayeredMaterial.cpp (env-mapped and two-texture M2 passes)

void FWowTerrainMaterial::PrepareRainMaterials()
{
#if WITH_EDITOR
    ON_SCOPE_EXIT { WowGetCobblestonePomMaterial(); };
    ON_SCOPE_EXIT { WowPrepareM2LayeredMaterials(); }; // r739: cooked with the other standalone materials
    const FString Name=TEXT("/Game/Materials/MPC_WowRain");
    auto* Collection=LoadObject<UMaterialParameterCollection>(nullptr,*(Name+TEXT(".MPC_WowRain")));
    if (!Collection)
    {
        auto* Package=CreatePackage(*Name);
        Collection=NewObject<UMaterialParameterCollection>(Package,TEXT("MPC_WowRain"),RF_Public|RF_Standalone);
        FCollectionScalarParameter Wet; Wet.ParameterName=TEXT("Wetness"); Wet.DefaultValue=0.f;
        Collection->ScalarParameters.Add(Wet); Collection->PostEditChange();
    }
    auto Save=[](UObject* Asset)
    {
        FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
        auto* Package=Asset->GetOutermost(); Package->MarkPackageDirty();
        const FString Filename=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
        if (!UPackage::SavePackage(Package,Asset,*Filename,Args)) UE_LOG(LogTemp,Fatal,TEXT("Failed to save rain material asset"));
    };
    Save(Collection);
    UWowWeatherComponent::PrepareGroundWaterMaterial(GetTransientPackage());
    WowCloth::Prepare(); // r535: M_WowClothObject + MPC_WowWind
    // Only terrain and opaque WMO ground: foliage, characters, water and effects stay separate.
    for (UMaterial* Material : {GetBaseMaterial(),GetSimpleOpaqueObjectMaterial(),GetSimpleOpaqueObjectMaterial(false)})
    {
        if (!Material) continue;
        bool Already=false;
        for (UMaterialExpression* E:Material->GetExpressions()) if (auto* P=Cast<UMaterialExpressionScalarParameter>(E))
            if (P->ParameterName==TEXT("WeatherExposure")) Already=true;
        if (Already)
        {
            // Migrate the existing rain layer in-place without stacking another
            // layer or changing the saved dry inputs.
            auto* D=Material->GetEditorOnlyData();
            if(auto* Color=Cast<UMaterialExpressionLinearInterpolate>(D->BaseColor.Expression))
                if(auto* Dark=Cast<UMaterialExpressionMultiply>(Color->B.Expression)) Dark->ConstB=.58f;
            if(auto* Rough=Cast<UMaterialExpressionLinearInterpolate>(D->Roughness.Expression)) Rough->ConstB=.52f;
            if(auto* Spec=Cast<UMaterialExpressionLinearInterpolate>(D->Specular.Expression)) Spec->ConstB=.25f;
            // Break the wet highlight into broad irregular patches; preserve the
            // original dry material, rather than giving the entire road one sheen.
            // r532 (Stefan: wet ground in rain shows a dotted checkerboard): the wet roughness ran
            // 0.24-0.46 across the 0.4 limit below which Lumen traces reflections
            // (r.Lumen.Reflections.MaxRoughnessToTrace). Traced, half-resolution reflections on large flat
            // stone came out as a dotted checker that crawled with the camera, in patches where the
            // noise dipped under 0.4. The wet sheen now stays at 0.42-0.55 (the soft probe reflection,
            // no per-pixel traces); the puddle film keeps the mirror look.
            const TCHAR* WetRoughnessCode=TEXT("float n=0.5+0.25*sin(P.x*.018+sin(P.y*.011))+0.25*cos(P.y*.023); return lerp(.42,.55,saturate(n));");
            if(auto* Rough=Cast<UMaterialExpressionLinearInterpolate>(D->Roughness.Expression))
                if(auto* Existing=Cast<UMaterialExpressionCustom>(Rough->B.Expression)) Existing->Code=WetRoughnessCode;
            if(auto* Rough=Cast<UMaterialExpressionLinearInterpolate>(D->Roughness.Expression))
            {
                if(!Rough->B.Expression)
                {
                    auto* Position=NewObject<UMaterialExpressionWorldPosition>(Material);
                    auto* WetRoughness=NewObject<UMaterialExpressionCustom>(Material);
                    WetRoughness->OutputType=CMOT_Float1;
                    FCustomInput Input;Input.InputName=TEXT("P");Input.Input.Connect(0,Position);WetRoughness->Inputs.Add(Input);
                    WetRoughness->Code=WetRoughnessCode;
                    Material->GetExpressionCollection().AddExpression(Position);
                    Material->GetExpressionCollection().AddExpression(WetRoughness);
                    Rough->B.Connect(0,WetRoughness);
                }
            }
            Material->PostEditChange(); Save(Material);
            continue;
        }
        auto& Graph=Material->GetExpressionCollection(); auto* Data=Material->GetEditorOnlyData();
        auto* Wet=NewObject<UMaterialExpressionCollectionParameter>(Material);
        Wet->Collection=Collection; Wet->ParameterName=TEXT("Wetness"); Wet->ParameterId=Collection->GetParameterId(TEXT("Wetness"));
        auto* Exposure=NewObject<UMaterialExpressionScalarParameter>(Material);
        Exposure->ParameterName=TEXT("WeatherExposure"); Exposure->DefaultValue=1.f;
        auto* Normal=NewObject<UMaterialExpressionPixelNormalWS>(Material);
        auto* Mask=NewObject<UMaterialExpressionCustom>(Material); Mask->OutputType=CMOT_Float1;
        FCustomInput W; W.InputName=TEXT("Wet"); W.Input.Connect(0,Wet); Mask->Inputs.Add(W);
        FCustomInput E; E.InputName=TEXT("Exposure"); E.Input.Connect(0,Exposure); Mask->Inputs.Add(E);
        FCustomInput N; N.InputName=TEXT("Normal"); N.Input.Connect(0,Normal); Mask->Inputs.Add(N);
        Mask->Code=TEXT("return saturate(Wet*Exposure)*smoothstep(0.15,0.8,Normal.z);");
        auto* Darken=NewObject<UMaterialExpressionMultiply>(Material); Darken->A=Data->BaseColor; Darken->ConstB=.58f;
        auto* Color=NewObject<UMaterialExpressionLinearInterpolate>(Material); Color->A=Data->BaseColor; Color->B.Connect(0,Darken); Color->Alpha.Connect(0,Mask);
        auto* Rough=NewObject<UMaterialExpressionLinearInterpolate>(Material); Rough->A=Data->Roughness; Rough->ConstA=Data->Roughness.UseConstant ? Data->Roughness.Constant : .5f; Rough->ConstB=.52f; Rough->Alpha.Connect(0,Mask);
        auto* Spec=NewObject<UMaterialExpressionLinearInterpolate>(Material); Spec->A=Data->Specular; Spec->ConstA=Data->Specular.UseConstant ? Data->Specular.Constant : .5f; Spec->ConstB=.25f; Spec->Alpha.Connect(0,Mask);
        for (UMaterialExpression* X : TArray<UMaterialExpression*>{Wet,Exposure,Normal,Mask,Darken,Color,Rough,Spec}) Graph.AddExpression(X);
        Data->BaseColor.Connect(0,Color); Data->Roughness.Connect(0,Rough); Data->Specular.Connect(0,Spec);
        Material->PostEditChange(); Save(Material);
    }
    // r732 (Stefan: "Add it all to the cobblestone and bricks"): cobble/brick ground layers get the stone relief too.
    WowPrepareTerrainStoneRelief(GetBaseMaterial());
#endif
}
