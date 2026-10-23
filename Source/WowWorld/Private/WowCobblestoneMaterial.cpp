#include "WowCookedMaterial.h"
#include "Materials/Material.h"
#include "Materials/MaterialParameterCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#endif

// r704 (Stefan: "lowering the sand between the rocks a little to get like a more 3D look of the cobblestones"):
// Stormwind's streets and sidewalks are WMO floors (MM_STREET_02/03, MM_HSTREET_01, MM_MSTREET_01,
// MM_*SIDEWALK_01). This opaque material reads a height from the texture itself - stones are lighter than
// the sand and grout between them, measured against the local average (a low mip) so it works on dark and
// light streets alike - and draws it with parallax occlusion: the sand lies up to 2 cm below the stone tops,
// stones hide the sand behind them at low angles, the light shades their rounded edges (normal from the
// height) and the gaps are a little darker. It fades out beyond ~25 m and at very low angles. Rain wets it
// like the other ground (MPC_WowRain Wetness x WeatherExposure), and the gaps stay wetter and darker.
// Texture, UVs and slots are those of the plain opaque WMO material, so the MID setup is unchanged.
// r715 (Stefan, Stormwind streets on the r704 build: "I see either of these having like an indent between the
// cobblestones?", and a flagstone floor with light grout: "Nor this"): v1 took lighter-than-average as stone, so
// on floors with pale mortar the mortar stood up and the stones sank; and 2 cm was too shallow to notice. v2 finds
// out per texture which way round it is - with the grout a narrow minority, more of the texture lies on the
// stones' side of its average (a 6 x 6 sample of the texture) - and the sand and grout lie 4 cm below the tops.
// r725 (Stefan on v2, a brown cobbled street close up: "wanted these more in 3d, the sand between the stones to be
// lowered a little bit", and a brick wall: "Same with these bricks, the black inbetween"): v2's height followed
// the texture's brightness almost linearly, so the gaps were shallow slopes rather than gaps, and 4 cm hardly
// showed. v3: the height is shaped into flat-topped stones with steep sides and the gaps at the bottom (a
// smoothstep on a stronger contrast), the gaps lie 7 cm down, sit in deeper shadow, and the stone edges catch
// more light (stronger normal tilt).
// r737 (Stefan 23.10, Stormwind street at night: "The deepened 3D effect failed, you lowered the stones rather than the
// fill between"): the shader guessed per texture which side is the gap from the share of texels brighter than the
// average. On these painted textures that share is 13-22 of 36 - a coin toss - and 15 of the 46 stone textures got it
// the wrong way round, Stormwind's MM_STREET_03 among them. Every texture was checked by eye (23.10): the gaps are the
// darker side on all but four (light mortar or grout), so the side is now a material parameter, StoneSign: +1 = the
// dark side is the gap (default), -1 = the light side is the gap. WowWmoRenderer sets -1 for those four (v4).
// r737 also (Stefan: "The stones are the ones that should be higher no matter", "Some stones darker than others" were
// lowered): the height followed brightness against a wide average (mip 5), so a whole darker stone, or the shaded half
// of one, sank like a gap. Now only the gaps sink: a texel is gap where it is clearly darker than the brighter of its
// close surroundings (mips 3 and 4, about 8-16 texels; the texel itself one mip softer so the painted speckle on a stone
// does not pit it). Every stone, light or dark, stays at the top; tested on the Stormwind, Elwynn and Westfall textures.
// Light mortar (StoneSign -1) mirrors it: clearly lighter than the darker of the surroundings.
UMaterial* WowGetCobblestonePomMaterial()
{
    static TStrongObjectPtr<UMaterial> Retained;
    if (Retained.IsValid()) return Retained.Get();
    const FString Name = TEXT("M_WowCobblePom_v4"); // r737: StoneSign parameter
    UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
        Material->SetShadingModel(MSM_DefaultLit);
        Material->BlendMode = BLEND_Opaque;
        Material->TwoSided = false;
        Material->bTangentSpaceNormal = false; // the node returns a world-space normal
        auto& Graph = Material->GetExpressionCollection();

        auto* Tex = NewObject<UMaterialExpressionTextureObjectParameter>(Material);
        Tex->ParameterName = TEXT("Layer0Texture");
        Tex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
        Tex->SamplerType = SAMPLERTYPE_Color;
        auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(Material);
        UV->CoordinateIndex = 0;
        auto* Pos = NewObject<UMaterialExpressionWorldPosition>(Material);
        Pos->WorldPositionShaderOffset = WPT_CameraRelative; // precise derivatives far from the origin
        auto* Normal = NewObject<UMaterialExpressionVertexNormalWS>(Material);
        auto* Depth = NewObject<UMaterialExpressionScalarParameter>(Material);
        Depth->ParameterName = TEXT("CobbleDepth"); Depth->DefaultValue = 7.0f; // cm the sand lies below the stones
        auto* Exposure = NewObject<UMaterialExpressionScalarParameter>(Material);
        Exposure->ParameterName = TEXT("WeatherExposure"); Exposure->DefaultValue = 1.0f;
        auto* StoneSign = NewObject<UMaterialExpressionScalarParameter>(Material);
        StoneSign->ParameterName = TEXT("StoneSign"); StoneSign->DefaultValue = 1.0f; // r737
        UMaterialExpressionCollectionParameter* Wet = nullptr;
        if (UMaterialParameterCollection* Rain = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Materials/MPC_WowRain.MPC_WowRain")))
        {
            Wet = NewObject<UMaterialExpressionCollectionParameter>(Material);
            Wet->Collection = Rain;
            Wet->ParameterName = TEXT("Wetness");
            Wet->ParameterId = Rain->GetParameterId(TEXT("Wetness"));
        }

        auto* Node = NewObject<UMaterialExpressionCustom>(Material);
        Node->Description = TEXT("Cobblestone parallax");
        Node->OutputType = CMOT_Float3;
        Node->Inputs.Empty();
        auto AddInput = [Node](const TCHAR* InputName, UMaterialExpression* Expression)
        {
            FCustomInput Input; Input.InputName = InputName;
            Input.Input.Connect(0, Expression);
            Node->Inputs.Add(Input);
        };
        AddInput(TEXT("Tex"), Tex);
        AddInput(TEXT("UV"), UV);
        AddInput(TEXT("P"), Pos);
        AddInput(TEXT("Nv"), Normal);
        AddInput(TEXT("DepthCm"), Depth);
        AddInput(TEXT("Exposure"), Exposure);
        AddInput(TEXT("StoneSign"), StoneSign);
        if (Wet) AddInput(TEXT("Wet"), Wet);
        for (const TCHAR* Out : {TEXT("NormalOut"), TEXT("RoughOut"), TEXT("SpecOut")})
        {
            FCustomOutput Output; Output.OutputName = Out;
            Output.OutputType = FCString::Strcmp(Out, TEXT("NormalOut")) == 0 ? CMOT_Float3 : CMOT_Float1;
            Node->AdditionalOutputs.Add(Output);
        }
        Node->Code = FString(Wet ? TEXT("") : TEXT("float Wet = 0;\n")) + TEXT(R"HLSL(
struct FCobble
{
    float Sign;
    float H(Texture2D T, SamplerState S, float2 uv, float2 gx, float2 gy)
    {
        // r737: 1 on every stone, 0 in the gaps (see above).
        float l = dot(Texture2DSampleGrad(T, S, uv, gx * 2.0, gy * 2.0).rgb, float3(0.30, 0.59, 0.11));
        float m3 = dot(Texture2DSampleLevel(T, S, uv, 3.0).rgb, float3(0.30, 0.59, 0.11));
        float m4 = dot(Texture2DSampleLevel(T, S, uv, 4.0).rgb, float3(0.30, 0.59, 0.11));
        float d = Sign > 0.0 ? 1.0 - l / max(max(m3, m4), 1e-3) : l / max(min(m3, m4), 1e-3) - 1.0;
        return 1.0 - smoothstep(0.10, 0.32, d);
    }
};
FCobble C;
C.Sign = StoneSign < 0.0 ? -1.0 : 1.0; // r737: +1 the dark side is the gap, -1 the light side (light mortar)
float3 N = normalize(Nv);
float3 E = normalize(-P);                       // towards the camera (P is camera-relative)
float2 gx = ddx(UV), gy = ddy(UV);
float3 dpx = ddx(P), dpy = ddy(P);
float3 dp2perp = cross(dpy, N);
float3 dp1perp = cross(N, dpx);
float det = dot(dpx, dp2perp);
det = abs(det) < 1e-8 ? 1e-8 : det;
float3 GU = (dp2perp * gx.x + dp1perp * gy.x) / det; // world gradient of u (uv per cm)
float3 GV = (dp2perp * gx.y + dp1perp * gy.y) / det; // world gradient of v
float facing = dot(E, N);
float fade = saturate((2500.0 - length(P)) / 1200.0) * saturate((facing - 0.08) / 0.22);
float depth = DepthCm * fade;
float2 hitUV = UV;
if (depth > 0.01)
{
    float3 R = -E;
    float2 uvPerDepth = float2(dot(R, GU), dot(R, GV)) / max(facing, 0.2);
    int steps = (int)lerp(20.0, 8.0, saturate(facing));
    float stepD = depth / steps;
    float d = 0;
    float2 uv = UV;
    float2 prevUV = UV;
    float prevGap = 0;
    [loop] for (int i = 0; i <= steps; ++i)
    {
        float gap = (1.0 - C.H(Tex, TexSampler, uv, gx, gy)) * depth - d;
        if (gap <= 0)
        {
            float w = i == 0 ? 1.0 : prevGap / max(prevGap - gap, 1e-5);
            hitUV = lerp(prevUV, uv, w);
            break;
        }
        prevGap = gap;
        prevUV = uv;
        d += stepD;
        uv = UV + uvPerDepth * d;
        hitUV = uv;
    }
}
float3 color = Texture2DSampleGrad(Tex, TexSampler, hitUV, gx, gy).rgb;
float h = C.H(Tex, TexSampler, hitUV, gx, gy);
// Rounded stone edges: the height's slope, in cm per cm, tilts the normal.
float e = 1.0 / 512.0;
float dhu = (C.H(Tex, TexSampler, hitUV + float2(e, 0), gx, gy) - C.H(Tex, TexSampler, hitUV - float2(e, 0), gx, gy)) / (2.0 * e);
float dhv = (C.H(Tex, TexSampler, hitUV + float2(0, e), gx, gy) - C.H(Tex, TexSampler, hitUV - float2(0, e), gx, gy)) / (2.0 * e);
float3 slope = depth * (dhu * GU + dhv * GV);
NormalOut = normalize(N - slope * 1.15);
// Sand and grout between the stones lie in shadow a little.
color *= lerp(1.0, lerp(0.42, 1.0, h), fade);
// Rain: the same wet ground as the other floors, the gaps wetter and darker.
float wet = saturate(Wet * Exposure) * smoothstep(0.15, 0.8, N.z);
float gapWet = wet * (1.0 - h) * fade;
color *= lerp(1.0, 0.58, wet) * lerp(1.0, 0.85, gapWet);
float n = 0.5 + 0.25 * sin(UV.x * 7.1 + sin(UV.y * 4.3)) + 0.25 * cos(UV.y * 9.7);
RoughOut = lerp(lerp(0.80, 0.86, 1.0 - h), lerp(0.55, 0.42, saturate(n + gapWet)), wet);
SpecOut = lerp(0.35, 0.25, wet);
return color;
)HLSL");
        Node->RebuildOutputs();

        for (UMaterialExpression* X : TArray<UMaterialExpression*>{Tex, UV, Pos, Normal, Depth, Exposure, StoneSign, Node})
            Graph.AddExpression(X);
        if (Wet) Graph.AddExpression(Wet);
        auto* Data = Material->GetEditorOnlyData();
        Data->BaseColor.Connect(0, Node);
        Data->Normal.Connect(1, Node);
        Data->Roughness.Connect(2, Node);
        Data->Specular.Connect(3, Node);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, Name);
    }
#endif
    if (Material) Retained.Reset(Material);
    return Material;
}

// r732 (Stefan 22.10: "Add it all to the cobblestone and bricks"): the cobbled roads outside the city (Elwynn's
// ElwynnCobbleStoneBase and any other ground layer named cobble/brick/flagstone) are terrain, drawn by the shared
// 4-layer splat material M_WowTerrainRuntime, so the WMO material above never reached them. This adds the same stone
// relief to that material in place (like the rain layer in PrepareRainMaterials): per chunk, CreateChunkMaterial
// sets Layer<N>Relief = 1 on the layers that are stone, and within ~25 m those layers' gaps sink 7 cm - the light
// shades the stone edges (normal from the height) and the gaps lie in shadow. No parallax on terrain (the four
// layer samplers stay untouched), so the ground keeps its exact look everywhere else. Chunks without a stone layer
// leave the shader after a few instructions. Idempotent: a second run only refreshes the shader code.
// r737: no per-pixel guess of the gap side any more (it was wrong on 1 of the 4 cobble ground layers, the dock dirt);
// Layer<N>Relief = -1 marks a layer whose light side is the gap (none so far: every cobble ground layer has dark gaps).
void WowPrepareTerrainStoneRelief(UMaterial* Terrain)
{
#if WITH_EDITOR
    if (!Terrain) return;
    static const TCHAR* Description = TEXT("Terrain stone relief");
    const FString Code = TEXT(R"HLSL(
struct FStone
{
    float4 W;
    float4 Sg;
    float H(Texture2D T, SamplerState S, float2 uv, float2 gx, float2 gy, float sgn)
    {
        // r737: only the gaps sink, as in M_WowCobblePom_v4 (every stone, light or dark, stays at the top).
        float l = dot(Texture2DSampleGrad(T, S, uv, gx * 2.0, gy * 2.0).rgb, float3(0.30, 0.59, 0.11));
        float m3 = dot(Texture2DSampleLevel(T, S, uv, 3.0).rgb, float3(0.30, 0.59, 0.11));
        float m4 = dot(Texture2DSampleLevel(T, S, uv, 4.0).rgb, float3(0.30, 0.59, 0.11));
        float d = sgn > 0.0 ? 1.0 - l / max(max(m3, m4), 1e-3) : l / max(min(m3, m4), 1e-3) - 1.0;
        return 1.0 - smoothstep(0.10, 0.32, d);
    }
    float All(Texture2D T0, SamplerState S0, Texture2D T1, SamplerState S1, Texture2D T2, SamplerState S2,
        Texture2D T3, SamplerState S3, float2 uv, float2 gx, float2 gy)
    {
        float h = 1.0 - (W.x + W.y + W.z + W.w); // non-stone layers stay at the top
        if (W.x > 0.001) h += W.x * H(T0, S0, uv, gx, gy, Sg.x);
        if (W.y > 0.001) h += W.y * H(T1, S1, uv, gx, gy, Sg.y);
        if (W.z > 0.001) h += W.z * H(T2, S2, uv, gx, gy, Sg.z);
        if (W.w > 0.001) h += W.w * H(T3, S3, uv, gx, gy, Sg.w);
        return h;
    }
};
float3 N = normalize(Nv);
Shade = 1.0;
// Derivatives first, outside any branch.
float2 gx = ddx(UV), gy = ddy(UV);
float3 dpx = ddx(P), dpy = ddy(P);
// The splat chain is lerp(lerp(lerp(L0, L1, A1), L2, A2), L3, A3): each layer's share of the pixel.
float4 W = float4((1.0 - A1) * (1.0 - A2) * (1.0 - A3), A1 * (1.0 - A2) * (1.0 - A3), A2 * (1.0 - A3), A3)
    * saturate(abs(float4(R0, R1, R2, R3)));
float3 E = normalize(-P); // P is camera-relative
float facing = dot(E, N);
float fade = saturate((2500.0 - length(P)) / 1200.0) * saturate((facing - 0.05) / 0.2);
if (W.x + W.y + W.z + W.w < 0.02 || fade <= 0.0) return N;
FStone St;
St.W = W;
// r737: the layer's relief value carries the side: +1 the dark side is the gap, -1 the light side (no more guessing).
St.Sg = float4(R0 < 0.0 ? -1.0 : 1.0, R1 < 0.0 ? -1.0 : 1.0, R2 < 0.0 ? -1.0 : 1.0, R3 < 0.0 ? -1.0 : 1.0);
float3 dp2perp = cross(dpy, N);
float3 dp1perp = cross(N, dpx);
float det = dot(dpx, dp2perp);
det = abs(det) < 1e-8 ? 1e-8 : det;
float3 GU = (dp2perp * gx.x + dp1perp * gy.x) / det; // world gradient of u (uv per cm)
float3 GV = (dp2perp * gx.y + dp1perp * gy.y) / det;
float e = 1.0 / 512.0;
float hu = (St.All(T0, T0Sampler, T1, T1Sampler, T2, T2Sampler, T3, T3Sampler, UV + float2(e, 0), gx, gy)
    - St.All(T0, T0Sampler, T1, T1Sampler, T2, T2Sampler, T3, T3Sampler, UV - float2(e, 0), gx, gy)) / (2.0 * e);
float hv = (St.All(T0, T0Sampler, T1, T1Sampler, T2, T2Sampler, T3, T3Sampler, UV + float2(0, e), gx, gy)
    - St.All(T0, T0Sampler, T1, T1Sampler, T2, T2Sampler, T3, T3Sampler, UV - float2(0, e), gx, gy)) / (2.0 * e);
float h = St.All(T0, T0Sampler, T1, T1Sampler, T2, T2Sampler, T3, T3Sampler, UV, gx, gy);
float depth = DepthCm * fade;
float3 slope = depth * (hu * GU + hv * GV);
// Sand and grout between the stones lie in shadow, like the WMO streets.
Shade = lerp(1.0, lerp(0.42, 1.0, h), fade);
return normalize(N - slope * 1.15);
)HLSL");

    auto Save = [Terrain]()
    {
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        UPackage* Package = Terrain->GetOutermost(); Package->MarkPackageDirty();
        const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
        if (!UPackage::SavePackage(Package, Terrain, *Filename, Args))
            UE_LOG(LogTemp, Error, TEXT("Terrain stone relief: failed to save %s"), *Package->GetName());
    };
    for (UMaterialExpression* Expression : Terrain->GetExpressions())
        if (auto* Existing = Cast<UMaterialExpressionCustom>(Expression))
            if (Existing->Description == Description)
            {
                if (!Existing->Code.Equals(Code, ESearchCase::CaseSensitive))
                {
                    Existing->Code = Code;
                    Terrain->PostEditChange();
                    Save();
                    UE_LOG(LogTemp, Log, TEXT("Terrain stone relief: shader code updated in %s"), *Terrain->GetName());
                }
                return;
            }

    UMaterialExpressionTextureSampleParameter2D* Layers[4] = {};
    UMaterialExpressionTextureSampleParameter2D* Alphas[3] = {};
    for (UMaterialExpression* Expression : Terrain->GetExpressions())
        if (auto* Sampler = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
        {
            for (int32 Index = 0; Index < 4; ++Index)
                if (!Layers[Index] && Sampler->ParameterName == FName(*FString::Printf(TEXT("Layer%dTexture"), Index)))
                    Layers[Index] = Sampler;
            for (int32 Index = 0; Index < 3; ++Index)
                if (!Alphas[Index] && Sampler->ParameterName == FName(*FString::Printf(TEXT("AlphaMap%d"), Index + 1)))
                    Alphas[Index] = Sampler;
        }
    for (int32 Index = 0; Index < 4; ++Index)
        if (!Layers[Index] || (Index < 3 && !Alphas[Index]))
        {
            UE_LOG(LogTemp, Warning, TEXT("Terrain stone relief: %s has no Layer%dTexture/AlphaMap sampler - skipped"), *Terrain->GetName(), Index);
            return;
        }

    auto& Graph = Terrain->GetExpressionCollection();
    UMaterialEditorOnlyData* Data = Terrain->GetEditorOnlyData();
    auto* Node = NewObject<UMaterialExpressionCustom>(Terrain);
    Node->Description = Description;
    Node->OutputType = CMOT_Float3;
    Node->Inputs.Empty();
    auto AddInput = [Node](const FString& InputName, UMaterialExpression* Expression, int32 OutputIndex)
    {
        FCustomInput Input; Input.InputName = FName(*InputName);
        Input.Input.Connect(OutputIndex, Expression);
        Node->Inputs.Add(Input);
    };
    for (int32 Index = 0; Index < 4; ++Index)
    {
        // Same parameter name as the layer's sampler, so the chunk's MID feeds both.
        auto* Tex = NewObject<UMaterialExpressionTextureObjectParameter>(Terrain);
        Tex->ParameterName = Layers[Index]->ParameterName;
        Tex->Texture = Layers[Index]->Texture;
        Tex->SamplerType = Layers[Index]->SamplerType;
        Tex->SamplerSource = Layers[Index]->SamplerSource;
        Graph.AddExpression(Tex);
        AddInput(FString::Printf(TEXT("T%d"), Index), Tex, 0);
    }
    for (int32 Index = 0; Index < 3; ++Index)
        AddInput(FString::Printf(TEXT("A%d"), Index + 1), Alphas[Index], 1); // the R output, as the splat lerps use
    for (int32 Index = 0; Index < 4; ++Index)
    {
        auto* Relief = NewObject<UMaterialExpressionScalarParameter>(Terrain);
        Relief->ParameterName = FName(*FString::Printf(TEXT("Layer%dRelief"), Index));
        Relief->DefaultValue = 0.0f;
        Graph.AddExpression(Relief);
        AddInput(FString::Printf(TEXT("R%d"), Index), Relief, 0);
    }
    UMaterialExpression* UV = Layers[0]->Coordinates.Expression;
    int32 UVOutput = Layers[0]->Coordinates.OutputIndex;
    if (!UV)
    {
        auto* Coord = NewObject<UMaterialExpressionTextureCoordinate>(Terrain);
        Coord->CoordinateIndex = Layers[0]->ConstCoordinate; // what the unconnected layer sampler uses
        Graph.AddExpression(Coord);
        UV = Coord; UVOutput = 0;
    }
    AddInput(TEXT("UV"), UV, UVOutput);
    auto* Pos = NewObject<UMaterialExpressionWorldPosition>(Terrain);
    Pos->WorldPositionShaderOffset = WPT_CameraRelative;
    auto* Normal = NewObject<UMaterialExpressionVertexNormalWS>(Terrain);
    auto* Depth = NewObject<UMaterialExpressionScalarParameter>(Terrain);
    Depth->ParameterName = TEXT("TerrainReliefDepth"); Depth->DefaultValue = 7.0f;
    for (UMaterialExpression* X : TArray<UMaterialExpression*>{Pos, Normal, Depth}) Graph.AddExpression(X);
    AddInput(TEXT("P"), Pos, 0);
    AddInput(TEXT("Nv"), Normal, 0);
    AddInput(TEXT("DepthCm"), Depth, 0);
    FCustomOutput ShadeOut; ShadeOut.OutputName = TEXT("Shade"); ShadeOut.OutputType = CMOT_Float1;
    Node->AdditionalOutputs.Add(ShadeOut);
    Node->Code = Code;
    Node->RebuildOutputs();
    Graph.AddExpression(Node);

    // Darken the gaps before the rain layer, so wet stone darkens on top of it.
    auto* Shade = NewObject<UMaterialExpressionMultiply>(Terrain);
    Shade->B.Connect(1, Node);
    Graph.AddExpression(Shade);
    auto* Color = Cast<UMaterialExpressionLinearInterpolate>(Data->BaseColor.Expression);
    auto* Dark = Color ? Cast<UMaterialExpressionMultiply>(Color->B.Expression) : nullptr;
    if (Color && Dark && Color->A.Expression) // the rain layer: lerp(dry, dry * 0.58, wet)
    {
        Shade->A = Color->A;
        Color->A.Connect(0, Shade);
        Dark->A.Connect(0, Shade);
    }
    else
    {
        Shade->A = Data->BaseColor;
        Data->BaseColor.Connect(0, Shade);
    }
    if (!Data->Normal.Expression)
    {
        Data->Normal.Connect(0, Node);
        Terrain->bTangentSpaceNormal = false; // the node returns a world-space normal
    }
    else
        UE_LOG(LogTemp, Warning, TEXT("Terrain stone relief: %s already has a normal input - gap shading only"), *Terrain->GetName());
    Terrain->PreEditChange(nullptr);
    Terrain->PostEditChange();
    Save();
    UE_LOG(LogTemp, Log, TEXT("Terrain stone relief added to %s (shading model lit=%d)"), *Terrain->GetName(),
        Terrain->GetShadingModels().IsLit() ? 1 : 0);
#else
    (void)Terrain;
#endif
}
