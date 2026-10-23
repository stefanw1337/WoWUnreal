#include "WowCookedMaterial.h"
#include "Materials/Material.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Engine/Texture2D.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionConstant.h"
#endif

// r739 (Stefan 23.10, TormentWoW's "Sanctum of Torment" in Ulduar: "missing chains, is just in white ghosted textures?",
// then "chains need a reflection mapping we don't support yet. Then get it supported?"): WotLK M2 passes take each
// texture's coordinates from the skin's texture-coordinate combo - UV0, UV1, or -1, the sphere environment map, which
// the client computes per vertex from the view-space normal (a reflection: shiny metal, ice, glass). Ulduar's chains
// (UL_Chains_01-05) draw three env-mapped passes (alpha 0.88 statue stone, additive 0.53 blue reflection, additive 0.80
// stone); the ice walls (UL_IceWall) draw a frost mist on UV0 cut by a mask texture on UV1 (two layers, mod2x). Our
// doodad materials only knew UV0 and one texture, so the chains came out as flat blue ghosts and the ice walls as
// unmasked white mist curtains.
//
// This layered M2 material does what the client's combiners do for those passes:
//  - each of up to two layers takes UV0, UV1 or the sphere map (OpenGL sphere-map formula on the view-space reflection);
//  - a second layer combines with the first as mod2x (colour x2, alpha multiplied), as the client does for two textures;
//  - the pass's transparency weight (its texture-weight track) scales the opacity.
// Three variants by the pass's blend mode: opaque (lit, like the plain M2 material), translucent and additive (unlit,
// like the simple effect materials). Parameters: Layer0Texture, Layer1Texture, Layer1On, Env0/Env1 (1 = sphere map),
// Uv1For0/Uv1For1 (1 = UV1), Weight.
namespace
{
    const TCHAR* LayeredName(int32 Kind)
    {
        return Kind == 0 ? TEXT("M_WowM2LayeredOpaque_v1") : Kind == 1 ? TEXT("M_WowM2LayeredTranslucent_v1")
            : TEXT("M_WowM2LayeredAdditive_v1");
    }
}

/** Kind: 0 opaque (lit), 1 translucent, 2 additive. Cooked by PrepareRainMaterials (StandalonePrepare). */
UMaterial* WowGetM2LayeredMaterial(int32 Kind)
{
    Kind = FMath::Clamp(Kind, 0, 2);
    static TStrongObjectPtr<UMaterial> Retained[3];
    if (Retained[Kind].IsValid()) return Retained[Kind].Get();
    const FString Name = LayeredName(Kind);
    UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
        Material->SetShadingModel(Kind == 0 ? MSM_DefaultLit : MSM_Unlit);
        Material->BlendMode = Kind == 0 ? BLEND_Opaque : Kind == 1 ? BLEND_Translucent : BLEND_Additive;
        Material->TwoSided = true;
        Material->bUsedWithInstancedStaticMeshes = true;
        Material->bUsedWithSkeletalMesh = true; // animated doodads
        auto& Graph = Material->GetExpressionCollection();
        UTexture2D* White = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));

        auto MakeTexture = [&](const TCHAR* ParameterName)
        {
            auto* Texture = NewObject<UMaterialExpressionTextureObjectParameter>(Material);
            Texture->ParameterName = ParameterName;
            Texture->Texture = White;
            Texture->SamplerType = SAMPLERTYPE_Color;
            Graph.AddExpression(Texture);
            return Texture;
        };
        auto MakeScalar = [&](const TCHAR* ParameterName, float Default)
        {
            auto* Scalar = NewObject<UMaterialExpressionScalarParameter>(Material);
            Scalar->ParameterName = ParameterName;
            Scalar->DefaultValue = Default;
            Graph.AddExpression(Scalar);
            return Scalar;
        };
        auto* T0 = MakeTexture(TEXT("Layer0Texture"));
        auto* T1 = MakeTexture(TEXT("Layer1Texture"));
        auto* UVA = NewObject<UMaterialExpressionTextureCoordinate>(Material);
        UVA->CoordinateIndex = 0;
        auto* UVB = NewObject<UMaterialExpressionTextureCoordinate>(Material);
        UVB->CoordinateIndex = 1;
        auto* NormalWS = NewObject<UMaterialExpressionVertexNormalWS>(Material);
        auto* CameraWS = NewObject<UMaterialExpressionCameraVectorWS>(Material);
        auto* NormalView = NewObject<UMaterialExpressionTransform>(Material);
        NormalView->TransformSourceType = TRANSFORMSOURCE_World;
        NormalView->TransformType = TRANSFORM_View;
        NormalView->Input.Connect(0, NormalWS);
        auto* CameraView = NewObject<UMaterialExpressionTransform>(Material);
        CameraView->TransformSourceType = TRANSFORMSOURCE_World;
        CameraView->TransformType = TRANSFORM_View;
        CameraView->Input.Connect(0, CameraWS);
        for (UMaterialExpression* X : TArray<UMaterialExpression*>{UVA, UVB, NormalWS, CameraWS, NormalView, CameraView})
            Graph.AddExpression(X);

        auto* Node = NewObject<UMaterialExpressionCustom>(Material);
        Node->Description = TEXT("M2 layered pass");
        Node->OutputType = CMOT_Float3;
        Node->Inputs.Empty();
        auto AddInput = [Node](const TCHAR* InputName, UMaterialExpression* Expression)
        {
            FCustomInput Input; Input.InputName = InputName;
            Input.Input.Connect(0, Expression);
            Node->Inputs.Add(Input);
        };
        AddInput(TEXT("T0"), T0);
        AddInput(TEXT("T1"), T1);
        AddInput(TEXT("UVA"), UVA);
        AddInput(TEXT("UVB"), UVB);
        AddInput(TEXT("NV"), NormalView);
        AddInput(TEXT("EV"), CameraView);
        AddInput(TEXT("Layer1On"), MakeScalar(TEXT("Layer1On"), 0.0f));
        AddInput(TEXT("Env0"), MakeScalar(TEXT("Env0"), 0.0f));
        AddInput(TEXT("Env1"), MakeScalar(TEXT("Env1"), 0.0f));
        AddInput(TEXT("Uv1For0"), MakeScalar(TEXT("Uv1For0"), 0.0f));
        AddInput(TEXT("Uv1For1"), MakeScalar(TEXT("Uv1For1"), 0.0f));
        AddInput(TEXT("Weight"), MakeScalar(TEXT("Weight"), 1.0f));
        FCustomOutput AlphaOut; AlphaOut.OutputName = TEXT("Alpha"); AlphaOut.OutputType = CMOT_Float1;
        Node->AdditionalOutputs.Add(AlphaOut);
        Node->Code = TEXT(R"HLSL(
// Sphere map: the reflection of the view ray about the normal, in view space with z towards the viewer (UE's view
// space looks down +z), mapped to the disc as OpenGL's GL_SPHERE_MAP does.
float3 n = normalize(NV);
float3 e = -normalize(EV);           // camera -> pixel
n.z = -n.z; e.z = -e.z;
float3 r = reflect(e, n);
float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
float2 sphere = float2(r.x / max(m, 1e-4) + 0.5, 0.5 - r.y / max(m, 1e-4));
float2 uv0 = Env0 > 0.5 ? sphere : (Uv1For0 > 0.5 ? UVB : UVA);
float2 uv1 = Env1 > 0.5 ? sphere : (Uv1For1 > 0.5 ? UVB : UVA);
float4 c0 = Texture2DSample(T0, T0Sampler, uv0);
float4 c1 = Texture2DSample(T1, T1Sampler, uv1);
// Two layers: the client's mod2x combine (colour x2, alpha multiplied).
float4 c = lerp(c0, float4(c0.rgb * c1.rgb * 2.0, c0.a * c1.a), step(0.5, Layer1On));
Alpha = saturate(c.a * Weight);
return c.rgb;
)HLSL");
        Node->RebuildOutputs();
        Graph.AddExpression(Node);

        auto* Data = Material->GetEditorOnlyData();
        if (Kind == 0)
        {
            Data->BaseColor.Connect(0, Node);
            auto* Roughness = NewObject<UMaterialExpressionConstant>(Material);
            Roughness->R = 0.8f; // as the plain M2 materials
            Graph.AddExpression(Roughness);
            Data->Roughness.Connect(0, Roughness);
        }
        else
        {
            // As M_WowSimpleObjectTranslucent/Additive: unlit, the texture's alpha (here x the pass weight) as opacity.
            Data->EmissiveColor.Connect(0, Node);
            Data->Opacity.Connect(1, Node);
        }
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, Name);
    }
#endif
    if (Material) Retained[Kind].Reset(Material);
    return Material;
}

/** r739: builds and saves the three layered M2 materials (called from PrepareRainMaterials during StandalonePrepare). */
void WowPrepareM2LayeredMaterials()
{
    for (int32 Kind = 0; Kind < 3; ++Kind)
        if (!WowGetM2LayeredMaterial(Kind))
            UE_LOG(LogTemp, Warning, TEXT("M2 layered material %s could not be built"), LayeredName(Kind));
}
