#include "WowWaterMaterial.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

UMaterialInterface* FWowWaterMaterial::GetRiverSurfaceParent()
{
    constexpr const TCHAR* InstancePath = TEXT("/Game/Water/MI_WowRiver.MI_WowRiver");
    if (auto* Existing = LoadObject<UMaterialInterface>(nullptr, InstancePath)) return Existing;
    auto* Native = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Water/Materials/WaterSurface/Water_Material_CustomMesh.Water_Material_CustomMesh"));
    if (!Native) return nullptr;
#if WITH_EDITOR
    // Keep UE's single-layer water, depth absorption, refraction and reflections.
    // Only add local wave slopes to its normal. Save the derivative so packaged
    // games use the same shader without building material graphs at runtime.
    UPackage* Package = CreatePackage(TEXT("/Game/Water/M_WowRiver"));
    UMaterial* Material = DuplicateObject<UMaterial>(Native->GetMaterial(), Package, TEXT("M_WowRiver"));
    Material->SetFlags(RF_Public | RF_Standalone);
    auto& Expressions = Material->GetExpressionCollection();
    auto* Original = NewObject<UMaterialExpressionGetMaterialAttributes>(Material);
    Original->MaterialAttributes = Material->GetEditorOnlyData()->MaterialAttributes;
    const int32 NormalOutput = Original->CreateOrGetOutputAttribute(MP_Normal);
    Expressions.AddExpression(Original);
    auto* Wave = NewObject<UMaterialExpressionCustom>(Material);
    Wave->Description = TEXT("Bounded character waves on native UE water");
    Wave->OutputType = CMOT_Float3;
    Expressions.AddExpression(Wave);
    auto Input = [&](const TCHAR* Name, UMaterialExpression* Expression, int32 Output = 0)
    {
        FCustomInput I; I.InputName = Name; I.Input.Connect(Output, Expression); Wave->Inputs.Add(I);
    };
    Wave->Inputs.Empty();
    Input(TEXT("BaseNormal"), Original, NormalOutput);
    auto* Position = NewObject<UMaterialExpressionWorldPosition>(Material);
    Expressions.AddExpression(Position); Input(TEXT("SurfacePosition"), Position);
    auto* Clock = NewObject<UMaterialExpressionScalarParameter>(Material);
    Clock->ParameterName = TEXT("RippleClock");
    Expressions.AddExpression(Clock); Input(TEXT("Clock"), Clock);
    for (int32 Index = 0; Index < 8; ++Index)
    {
        for (const TCHAR* Prefix : {TEXT("RipplePosition"), TEXT("RippleDirection")})
        {
            const FString Name = FString::Printf(TEXT("%s%d"), Prefix, Index);
            auto* Parameter = NewObject<UMaterialExpressionVectorParameter>(Material);
            Parameter->ParameterName = *Name;
            Parameter->DefaultValue = FLinearColor(0, 0, 0, -1000);
            Expressions.AddExpression(Parameter);
            auto* RGBA = NewObject<UMaterialExpressionAppendVector>(Material);
            RGBA->A.Connect(0, Parameter); RGBA->B.Connect(4, Parameter);
            Expressions.AddExpression(RGBA); Input(*Name, RGBA);
        }
    }
    Wave->Code = TEXT(R"HLSL(
float4 origins[8] = {RipplePosition0,RipplePosition1,RipplePosition2,RipplePosition3,RipplePosition4,RipplePosition5,RipplePosition6,RipplePosition7};
float4 directions[8] = {RippleDirection0,RippleDirection1,RippleDirection2,RippleDirection3,RippleDirection4,RippleDirection5,RippleDirection6,RippleDirection7};
float2 slope = 0;
[unroll] for (int i=0; i<8; ++i)
{
    float age = Clock - origins[i].w;
    if (age >= 0 && age < 2.3 && abs(SurfacePosition.z - origins[i].z) < 24)
    {
        float2 delta = SurfacePosition.xy - origins[i].xy;
        float distance = length(delta);
        float2 radial = delta / max(distance, 1);
        float wavefront = distance - (24 + age * 95);
        float envelope = exp(-pow(wavefront / 27, 2));
        float decay = pow(saturate(1-age/2.3), 2) * saturate(age*9);
        float directional = lerp(.35 + .65*saturate(dot(radial, directions[i].xy)*.5+.5), 1, directions[i].w);
        slope += radial * sin(wavefront * .16) * envelope * decay * directional * directions[i].z;
    }
}
float3 tangentSlope = TransformWorldVectorToTangent(Parameters.TangentToWorld, float3(slope,0));
return normalize(BaseNormal + tangentSlope);
)HLSL");
    auto* Result = NewObject<UMaterialExpressionSetMaterialAttributes>(Material);
    Expressions.AddExpression(Result);
    Result->Inputs[0].Connect(0, Original);
    Result->ConnectInputAttribute(MP_Normal, Wave);
    Material->GetEditorOnlyData()->MaterialAttributes.Connect(0, Result);
    Material->PostEditChange();

    UPackage* InstancePackage = CreatePackage(TEXT("/Game/Water/MI_WowRiver"));
    auto* Instance = NewObject<UMaterialInstanceConstant>(InstancePackage, TEXT("MI_WowRiver"), RF_Public | RF_Standalone);
    Instance->SetParentEditorOnly(Material);
    Instance->CopyMaterialUniformParametersEditorOnly(Native, true);
    Instance->PostEditChange();
    for (UObject* Asset : {static_cast<UObject*>(Material), static_cast<UObject*>(Instance)})
    {
        const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args);
    }
    return Instance;
#else
    return Native;
#endif
}
