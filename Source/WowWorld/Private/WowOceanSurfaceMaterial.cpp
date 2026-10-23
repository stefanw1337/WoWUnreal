#include "WowWaterMaterial.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWowOcean, Log, All);

// Ocean = UE single-layer water + Gerstner sea swell (wind weighted, shoaling near
// the coast), a shoreline surge that runs up and down the beach, shore foam and
// whitecaps. Weather drives wind direction/height, chop, foam and rain roughness.
// UV1.x carries the water depth in metres (surface - terrain) per vertex.

namespace
{
TWeakObjectPtr<UMaterialInstanceDynamic> OceanInstance;
// CPU mirror of the swell parameters so gameplay visuals (swimmers, camera) can ride the waves.
FVector2D OceanWindDir(1.0f, 0.0f);
float OceanChopValue = 0.35f;
float OceanWaveHeightCm = 0.0f;
float OceanSurgeCm = 0.0f;
TAutoConsoleVariable<float> CVarSeaState(TEXT("wow.SeaState"), -1.0f,
    TEXT("Session-only sea test: -1 follows the weather, 0 = calm ... 1 = storm."));

#if WITH_EDITOR
// Shared wave code. r700 (Stefan, the sea after r595: "seems like the wind is going everywhere? Normally the waves
// go inwards towards land ... following the wind ... in the same direction, maybe slightly turning side to side"):
// 16 fixed directions 22.5 degrees apart (still fixed, so a turning wind never jumps the phases - it only moves
// the weight from one direction to its neighbours) with a narrow lobe round the travel direction: the main
// train, its neighbours at about half height (the crests sway side to side), nothing across or against the
// wind. Over a rising sea floor (about 12 m up to 1.5 m of water) the waves grow and steepen before they break.
// Keep in sync with SampleOceanHeight / SampleOceanDrift (WowOceanWaves below).
const TCHAR* OceanWaveCode = TEXT(R"HLSL(
float Depth = DepthUV.x;
float2 windDir = normalize(Wind.xy + float2(1e-4, 0));
// Near the coast swell bends towards the shore (refraction): ShoreUV carries the
// shoreward direction x strength per vertex (0 far out / on flat sea floors).
float2 shoreV = ShoreUV.xy;
float nearShore = (1.0 - smoothstep(3.0, 18.0, Depth)) * saturate(length(shoreV));
float2 travelDir = normalize(lerp(windDir, shoreV / max(length(shoreV), 1e-4), nearShore) + float2(1e-4, 0));
// Calmer, game-paced swell (real deep-water speed read as rushing).
float WaveClock = Clock * 0.72;
float L[16] = {5200, 3300, 4400, 2300, 3800, 1700, 2900, 1300, 4800, 2600, 3600, 2000, 4100, 1500, 3100, 2700};
float off[16] = {0.05, -0.08, 0.07, -0.04, 0.09, -0.02, 0.06, -0.09, 0.03, -0.06, 0.08, -0.05, 0.02, -0.07, 0.04, -0.03};
float shoal = smoothstep(0.0, 3.5, Depth);
float breakZone = (1.0 - smoothstep(5.0, 12.0, Depth)) * smoothstep(1.0, 3.0, Depth);
float grow = 1.0 + 0.5 * breakZone;
float steep = lerp(1.0, 2.2, breakZone);
float2 D[16];
float Wt[16];
float3 disp = 0;
float2 slope = 0;
float height = 0;
float ampSum = 0;
[unroll] for (int i = 0; i < 16; ++i)
{
    float a = i * 0.3926991 + off[i];
    float2 d = float2(cos(a), sin(a));
    float w = pow(saturate(dot(d, travelDir)), 10.0) * 1.5 + 0.01;
    D[i] = d;
    Wt[i] = w;
    float k = 6.2831853 / L[i];
    float A = WaveHeight * w * (L[i] / 5200.0) * 0.42 * shoal * grow;
    float omega = sqrt(981.0 * k);
    float ph = k * dot(d, Pos.xy) - omega * WaveClock + i * 1.7;
    float s = sin(ph);
    float c = cos(ph);
    float qa = min((Chop * 0.11 * steep) / max(k * A, 1e-4), 1.0) * A;
    disp.xy += d * qa * c;
    disp.z += A * s;
    slope += d * k * A * c;
    height += A * s;
    ampSum += A;
}
float shore = 1.0 - smoothstep(0.0, 2.5, Depth);
float surgePhase = Clock * 0.75 + 2.0 * sin(Pos.x * 0.0009) + 2.0 * sin(Pos.y * 0.0013);
)HLSL");
#endif
}

UMaterialInterface* FWowWaterMaterial::GetOceanSurfaceParent()
{
    constexpr const TCHAR* InstancePath = TEXT("/Game/Water/MI_WowOcean_v7.MI_WowOcean_v7");
    if (auto* Existing = LoadObject<UMaterialInterface>(nullptr, InstancePath)) return Existing;
    auto* Native = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Water/Materials/WaterSurface/Water_Material_CustomMesh.Water_Material_CustomMesh"));
    if (!Native) return nullptr;
#if WITH_EDITOR
    UPackage* Package = CreatePackage(TEXT("/Game/Water/M_WowOcean_v7"));
    UMaterial* Material = DuplicateObject<UMaterial>(Native->GetMaterial(), Package, TEXT("M_WowOcean_v7"));
    Material->SetFlags(RF_Public | RF_Standalone);
    auto& Expressions = Material->GetExpressionCollection();

    auto* Original = NewObject<UMaterialExpressionGetMaterialAttributes>(Material);
    Original->MaterialAttributes = Material->GetEditorOnlyData()->MaterialAttributes;
    const int32 NormalOut = Original->CreateOrGetOutputAttribute(MP_Normal);
    const int32 BaseColorOut = Original->CreateOrGetOutputAttribute(MP_BaseColor);
    const int32 OpacityOut = Original->CreateOrGetOutputAttribute(MP_Opacity);
    const int32 RoughnessOut = Original->CreateOrGetOutputAttribute(MP_Roughness);
    const int32 OffsetOut = Original->CreateOrGetOutputAttribute(MP_WorldPositionOffset);
    Expressions.AddExpression(Original);

    auto Scalar = [&](const TCHAR* Name, float Default)
    {
        auto* P = NewObject<UMaterialExpressionScalarParameter>(Material);
        P->ParameterName = Name; P->DefaultValue = Default;
        Expressions.AddExpression(P);
        return P;
    };
    auto* Wind = NewObject<UMaterialExpressionVectorParameter>(Material);
    Wind->ParameterName = TEXT("OceanWind");
    Wind->DefaultValue = FLinearColor(1, 0, 0, 0);
    Expressions.AddExpression(Wind);
    auto* WaveHeight = Scalar(TEXT("OceanWaveHeight"), 30.0f);
    auto* Chop = Scalar(TEXT("OceanChop"), 0.35f);
    auto* Surge = Scalar(TEXT("OceanSurge"), 10.0f);
    auto* FoamAmount = Scalar(TEXT("OceanFoam"), 1.0f);
    auto* Rain = Scalar(TEXT("OceanRain"), 0.0f);
    auto* Caustics = Scalar(TEXT("OceanCaustics"), 0.0f); // r596: tropical seas (Tanaris, Stranglethorn)
    auto* Position = NewObject<UMaterialExpressionWorldPosition>(Material);
    Expressions.AddExpression(Position);
    auto* Time = NewObject<UMaterialExpressionTime>(Material);
    Expressions.AddExpression(Time);
    auto* DepthUV = NewObject<UMaterialExpressionTextureCoordinate>(Material);
    DepthUV->CoordinateIndex = 1;
    Expressions.AddExpression(DepthUV);
    auto* ShoreUV = NewObject<UMaterialExpressionTextureCoordinate>(Material);
    ShoreUV->CoordinateIndex = 2;
    Expressions.AddExpression(ShoreUV);
    auto* FoamTexture = NewObject<UMaterialExpressionTextureObject>(Material);
    FoamTexture->Texture = LoadObject<UTexture2D>(nullptr,
        TEXT("/Water/Textures/Foam/T_WaterFlow_03_Foam_Tiled.T_WaterFlow_03_Foam_Tiled"));
    if (!FoamTexture->Texture)
        FoamTexture->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
    Expressions.AddExpression(FoamTexture);

    auto MakeCustom = [&](const TCHAR* Description, ECustomMaterialOutputType Type, const FString& Body)
    {
        auto* Node = NewObject<UMaterialExpressionCustom>(Material);
        Node->Description = Description;
        Node->OutputType = Type;
        Node->Inputs.Empty();
        Node->Code = Body;
        Expressions.AddExpression(Node);
        return Node;
    };
    auto AddInput = [](UMaterialExpressionCustom* Node, const TCHAR* Name, UMaterialExpression* Expression, int32 Output = 0)
    {
        FCustomInput Input; Input.InputName = Name; Input.Input.Connect(Output, Expression); Node->Inputs.Add(Input);
    };
    auto AddWaveInputs = [&](UMaterialExpressionCustom* Node)
    {
        AddInput(Node, TEXT("Pos"), Position);
        AddInput(Node, TEXT("Clock"), Time);
        AddInput(Node, TEXT("Wind"), Wind);
        AddInput(Node, TEXT("WaveHeight"), WaveHeight);
        AddInput(Node, TEXT("Chop"), Chop);
        AddInput(Node, TEXT("DepthUV"), DepthUV);
        AddInput(Node, TEXT("ShoreUV"), ShoreUV);
    };

    // Vertex displacement: swell + shoreline surge, added to the native offset.
    auto* Offset = MakeCustom(TEXT("Ocean swell and shore surge"), CMOT_Float3, FString(OceanWaveCode) + TEXT(R"HLSL(
disp.z += Surge * shore * (0.55 + 0.45 * sin(surgePhase));
return BaseOffset + disp;
)HLSL"));
    AddInput(Offset, TEXT("BaseOffset"), Original, OffsetOut);
    AddWaveInputs(Offset);
    AddInput(Offset, TEXT("Surge"), Surge);

    // Per-pixel swell normal on top of the native ripple normal; rain adds fine pitting.
    auto* Normal = MakeCustom(TEXT("Ocean swell normal"), CMOT_Float3, FString(OceanWaveCode) + TEXT(R"HLSL(
struct FN { float h(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); } };
FN fn;
// Rain: expanding ring ripples from scattered drops (two offset layers), smooth slopes -
// the old per-cell random normals rendered as a blocky checkerboard in storms.
float2 pit = 0;
[unroll] for (int layer = 0; layer < 2; ++layer)
{
    float2 p = Pos.xy * 0.028 + layer * float2(0.5, 0.37);
    float2 c = floor(p);
    float2 f = frac(p) - 0.5;
    float h = fn.h(c + layer * 31.7);
    float t = frac(Clock * (1.1 + 0.3 * layer) + h);
    float2 d = f - (float2(fn.h(c + 3.1), fn.h(c + 7.7)) - 0.5) * 0.5;
    float r = length(d);
    float ring = sin((r - t * 0.45) * 46.0) * exp(-r * 9.0) * (1.0 - t) * smoothstep(0.0, 0.05, t);
    pit += d / max(r, 1e-3) * ring;
}
pit *= Rain * 0.28;
float3 tangentSlope = TransformWorldVectorToTangent(Parameters.TangentToWorld, float3(-slope + pit, 0));
return normalize(BaseNormal + tangentSlope);
)HLSL"));
    AddInput(Normal, TEXT("BaseNormal"), Original, NormalOut);
    AddWaveInputs(Normal);
    AddInput(Normal, TEXT("Rain"), Rain);

    // Foam mask: shore band that follows the surge + whitecaps on crests in wind.
    auto* Foam = MakeCustom(TEXT("Ocean foam"), CMOT_Float1, FString(OceanWaveCode) + TEXT(R"HLSL(
float2 uvA = Pos.xy * 0.0016 + windDir * Clock * 0.012;
float2 uvB = Pos.xy * 0.0041 - windDir.yx * Clock * 0.02;
float tex = dot(Texture2DSample(FoamTex, FoamTexSampler, uvA).rgb, 0.3333) * 0.6
          + dot(Texture2DSample(FoamTex, FoamTexSampler, uvB).rgb, 0.3333) * 0.4;
float wash = 0.6 + 0.4 * sin(surgePhase + 1.2);
// Warp the per-vertex depth with large-scale noise so the foam line follows a
// natural, wavy coast instead of the square liquid-cell grid.
float warp = dot(Texture2DSample(FoamTex, FoamTexSampler, Pos.xy * 0.00045 + float2(0.37, 0.11)).rgb, 0.3333)
           + 0.5 * dot(Texture2DSample(FoamTex, FoamTexSampler, Pos.xy * 0.0011 - float2(0.21, 0.63)).rgb, 0.3333) - 0.75;
float wDepth = max(Depth + warp * 1.1, 0.0);
float band = (1.0 - smoothstep(0.05, 1.1 + Surge * 0.012, wDepth)) * wash;
float shoreFoam = saturate(band * 1.8 - (1.0 - tex) * 0.9);
float crest = height / max(ampSum, 1.0);
// r595 (Stefan, Stormwind sea: the whitecaps were "too thick and bulky" round patches; white belongs
// where the tip of a wave turns over, and it dissipates behind it - "more towards land, but randomly out
// at sea"): foam now comes from the breaking crests themselves. Per swell train, a thin broken line just
// behind the crest (the curl spilling over) and a lacy trail that thins out down the back of the wave.
// Only the tallest crests break (where the trains add up); near the shore (under ~20 m of water) most
// of them do and the surf leaves a longer wash, out at sea a slow large-scale noise picks scattered ones.
float2 crestDir = float2(-windDir.y, windDir.x);
float2 wf = float2(dot(Pos.xy, crestDir), dot(Pos.xy, windDir));
float streak = dot(Texture2DSample(FoamTex, FoamTexSampler, float2(wf.x * 0.0011, wf.y * 0.0042) + float2(0.13, -Clock * 0.01)).rgb, 0.3333);
float breakup = dot(Texture2DSample(FoamTex, FoamTexSampler, Pos.xy * 0.00021 + float2(0.71, 0.29) + windDir * Clock * 0.0015).rgb, 0.3333);
float lace = saturate((lerp(tex, streak, 0.6) - 0.40) * 3.2);
float coast = 1.0 - smoothstep(4.0, 20.0, Depth);
float decay = lerp(11.0, 5.0, coast);
float breakers = 0;
// r700 (Stefan: "it's always the tops that will be white, following the waves ... but not as wide, more thin and
// breaking when they crash down"): a thin white line on the very top of each crest, broken up by the lace, and
// only a short, thin wash behind it; in the break zone the crests that crash leave a little more.
[unroll] for (int j = 0; j < 16; ++j)
{
    float kj = 6.2831853 / L[j];
    float Aj = WaveHeight * Wt[j] * (L[j] / 5200.0) * 0.42 * shoal * grow;
    float phj = kj * dot(D[j], Pos.xy) - sqrt(981.0 * kj) * WaveClock + j * 1.7;
    float behind = frac((1.5707963 - phj) / 6.2831853); // 0 on the crest, growing down its back
    float tip = (1.0 - smoothstep(0.0, lerp(0.012, 0.022, breakZone), behind)) * (0.3 + 0.7 * lace);
    float trail = exp(-behind * decay * lerp(2.2, 1.4, breakZone)) * lace * lerp(0.55, 0.8, breakZone);
    breakers += (Aj / max(ampSum, 1.0)) * max(tip, trail);
}
float seaPick = smoothstep(0.50, 0.68, breakup); // scattered breakers out at sea
float capLine = lerp(lerp(0.70, 0.55, seaPick), 0.30, coast);
float windCaps = lerp(0.5, 1.0, saturate((Chop - 0.3) / 0.45));
float caps = saturate(breakers * 2.4) * smoothstep(capLine, capLine + 0.16, crest) * windCaps * max(max(seaPick, coast), breakZone);
return saturate(shoreFoam + caps) * FoamAmount;
)HLSL"));
    AddWaveInputs(Foam);
    AddInput(Foam, TEXT("Surge"), Surge);
    AddInput(Foam, TEXT("FoamAmount"), FoamAmount);
    AddInput(Foam, TEXT("FoamTex"), FoamTexture);

    auto* Color = MakeCustom(TEXT("Ocean foam colour"), CMOT_Float3, TEXT("return lerp(Base, float3(0.92, 0.95, 0.97), saturate(Foam * 1.15));"));
    AddInput(Color, TEXT("Base"), Original, BaseColorOut);
    AddInput(Color, TEXT("Foam"), Foam);
    // Feather the waterline: the mesh meets the beach in a hard polygon line, so water and foam
    // fade out over the last ~40 cm of depth (per-vertex depth).
    auto* Opacity = MakeCustom(TEXT("Ocean foam opacity + soft shore"), CMOT_Float1, TEXT(R"HLSL(
float edge = smoothstep(0.0, 0.42, DepthUV.x);
return max(Base, Foam * 0.85) * edge;
)HLSL"));
    AddInput(Opacity, TEXT("Base"), Original, OpacityOut);
    AddInput(Opacity, TEXT("Foam"), Foam);
    AddInput(Opacity, TEXT("DepthUV"), DepthUV);
    auto* Roughness = MakeCustom(TEXT("Ocean foam/rain roughness"), CMOT_Float1,
        TEXT("return lerp(lerp(Base, 0.22, saturate(Rain) * 0.6), 0.85, saturate(Foam * 1.4));"));
    AddInput(Roughness, TEXT("Base"), Original, RoughnessOut);
    AddInput(Roughness, TEXT("Foam"), Foam);
    AddInput(Roughness, TEXT("Rain"), Rain);

    // r596 (Stefan's photos of clear tropical water: "this pattern of the sandy ocean floor"): caustics -
    // the bright moving net of sunlight on the sea floor. The single-layer-water output scales what is seen
    // behind the water (the lit floor), so the pattern follows the floor's own lighting (none at night).
    // Strongest in the first metre or two, gone by ~6 m; only where OceanCaustics is set (warm seas).
    for (UMaterialExpression* Expr : Material->GetExpressions())
    {
        auto* WaterOut = Cast<UMaterialExpressionSingleLayerWaterMaterialOutput>(Expr);
        if (!WaterOut) continue;
        const bool bHasBase = WaterOut->ColorScaleBehindWater.Expression != nullptr;
        auto* Behind = MakeCustom(TEXT("Ocean floor caustics"), CMOT_Float3, FString(bHasBase ? TEXT("float3 scale = Base;\n") : TEXT("float3 scale = float3(1, 1, 1);\n")) + TEXT(R"HLSL(
float strength = Caustics * smoothstep(0.05, 0.45, DepthUV.x) * (1.0 - smoothstep(0.6, 6.0, DepthUV.x));
if (strength <= 0.001) return scale;
// Tileable water caustic (iterated domain warp), ~3 m tile, slowly drifting.
float2 p = frac(Pos.xy / 300.0) * 6.2831853 - 250.0;
float t = Clock * 0.45 + 23.0;
float2 q = p;
float c = 1.0;
const float inten = 0.005;
[unroll] for (int n = 0; n < 5; ++n)
{
    float tt = t * (1.0 - (3.5 / float(n + 1)));
    q = p + float2(cos(tt - q.x) + sin(tt + q.y), sin(tt - q.y) + cos(tt + q.x));
    c += 1.0 / length(float2(p.x / (sin(q.x + tt) / inten), p.y / (cos(q.y + tt) / inten)));
}
c /= 5.0;
c = 1.17 - pow(c, 1.4);
float caus = saturate(pow(abs(c), 8.0));
return scale * (1.0 + caus * 1.8 * strength);
)HLSL"));
        if (bHasBase) AddInput(Behind, TEXT("Base"), WaterOut->ColorScaleBehindWater.Expression, WaterOut->ColorScaleBehindWater.OutputIndex);
        AddInput(Behind, TEXT("Caustics"), Caustics);
        AddInput(Behind, TEXT("DepthUV"), DepthUV);
        AddInput(Behind, TEXT("Pos"), Position);
        AddInput(Behind, TEXT("Clock"), Time);
        WaterOut->ColorScaleBehindWater.Connect(0, Behind);
        break;
    }

    auto* Result = NewObject<UMaterialExpressionSetMaterialAttributes>(Material);
    Expressions.AddExpression(Result);
    Result->Inputs[0].Connect(0, Original);
    Result->ConnectInputAttribute(MP_Normal, Normal);
    Result->ConnectInputAttribute(MP_BaseColor, Color);
    Result->ConnectInputAttribute(MP_Opacity, Opacity);
    Result->ConnectInputAttribute(MP_Roughness, Roughness);
    Result->ConnectInputAttribute(MP_WorldPositionOffset, Offset);
    Material->GetEditorOnlyData()->MaterialAttributes.Connect(0, Result);
    Material->PostEditChange();

    UPackage* InstancePackage = CreatePackage(TEXT("/Game/Water/MI_WowOcean_v7"));
    auto* Instance = NewObject<UMaterialInstanceConstant>(InstancePackage, TEXT("MI_WowOcean_v7"), RF_Public | RF_Standalone);
    Instance->SetParentEditorOnly(Material);
    Instance->CopyMaterialUniformParametersEditorOnly(Native, true);
    Instance->PostEditChange();
    for (UObject* Asset : {static_cast<UObject*>(Material), static_cast<UObject*>(Instance)})
    {
        const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args);
    }
    UE_LOG(LogWowOcean, Log, TEXT("Ocean material built (swell, shore surge, foam, whitecaps)"));
    return Instance;
#else
    return nullptr;
#endif
}

UMaterialInterface* FWowWaterMaterial::GetOceanMaterial()
{
    static TStrongObjectPtr<UMaterialInstanceDynamic> Retained;
    if (Retained.IsValid()) return Retained.Get();
    UMaterialInterface* Parent = GetOceanSurfaceParent();
    if (!Parent)
    {
        UE_LOG(LogWowOcean, Warning, TEXT("Ocean material unavailable; using the river water"));
        return GetWaterMaterial();
    }
    auto* Ocean = UMaterialInstanceDynamic::Create(Parent, GetTransientPackage());
    // Deep sea optics: stronger blue transmission than the clear green rivers.
    Ocean->SetVectorParameterValue(TEXT("Absorption"), FLinearColor(70.0f, 180.0f, 260.0f, 6.0f));
    Ocean->SetVectorParameterValue(TEXT("Scattering"), FLinearColor(0.25f, 0.55f, 0.65f, 0.25f));
    Ocean->SetScalarParameterValue(TEXT("Water Roughness"), 0.07f);
    Ocean->SetScalarParameterValue(TEXT("Water Specular"), 0.14f);
    Retained.Reset(Ocean);
    OceanInstance = Ocean;
    SetOceanWeather(FVector2D(1, 0), 4.0f, 0.0f, false);
    return Ocean;
}

// r596 (Stefan): sea colour by zone. Tanaris and Stranglethorn get clear turquoise water with caustics on
// the sandy floor, the Barrens coast (Ratchet) a little of it, Feralas a dark rainforest sea, Northrend and the
// other cold zones a clear icy blue; everywhere else (Stormwind) the deep blue ocean. Set by the weather component on a zone change and
// eased over ~8 s in SetOceanWeather.
namespace
{
    uint32 OceanZone = 0;
    float OceanZoneWaveScale = 1.0f;
    // r701: WaveScale - sheltered warm seas are calmer (Stefan: Tanaris "not as wave'y").
    struct FOceanLook { FLinearColor Absorption, Scattering; float Caustics; float WaveScale = 1.0f; };
    FOceanLook OceanLookFor(uint32 Zone)
    {
        switch (Zone)
        {
        // r701 (Stefan, Tanaris coast: "more green and clear", "It's green, but only in shallow waters" - further out
        // it was the same dark navy as everywhere): green carries much further (the floor shows through deeper, and
        // deep water keeps a teal-green body colour), and the swell is about half as high.
        case 440: // Tanaris
        case 33:  // Stranglethorn Vale
            return { FLinearColor(150.0f, 1100.0f, 650.0f, 6.0f), FLinearColor(0.32f, 0.95f, 0.72f, 0.05f), 1.0f, 0.5f };
        case 17:  // The Barrens (Ratchet): only a hint
            return { FLinearColor(95.0f, 320.0f, 330.0f, 6.0f), FLinearColor(0.27f, 0.68f, 0.72f, 0.18f), 0.35f, 0.8f };
        case 3537: // Borean Tundra   - cold seas (Stefan: "colder climate it's blue again"): clear icy blue
        case 495:  // Howling Fjord
        case 65:   // Dragonblight
        case 210:  // Icecrown
        case 67:   // The Storm Peaks
        case 618:  // Winterspring
        case 1:    // Dun Morogh
            return { FLinearColor(85.0f, 250.0f, 430.0f, 6.0f), FLinearColor(0.18f, 0.52f, 0.80f, 0.18f), 0.0f };
        case 357: // Feralas: dark, deep rainforest sea
            return { FLinearColor(45.0f, 140.0f, 160.0f, 6.0f), FLinearColor(0.08f, 0.30f, 0.32f, 0.30f), 0.0f };
        default:
            return { FLinearColor(70.0f, 180.0f, 260.0f, 6.0f), FLinearColor(0.25f, 0.55f, 0.65f, 0.25f), 0.0f };
        }
    }
}

void WowSetOceanZone(uint32 Zone) { OceanZone = Zone; }

void FWowWaterMaterial::SetOceanWeather(const FVector2D& WindTravelDirection, float WindSpeed, float Rain, bool bStorm)
{
    UMaterialInstanceDynamic* Ocean = OceanInstance.Get();
    if (!Ocean) return;
    {
        static FOceanLook Shown = OceanLookFor(0);
        static double LastLook = 0.0;
        const double Now = FPlatformTime::Seconds();
        const float Alpha = LastLook > 0.0 ? FMath::Clamp(float(Now - LastLook) / 2.5f, 0.0f, 1.0f) : 1.0f; // ~8 s to settle
        LastLook = Now;
        const FOceanLook Want = OceanLookFor(OceanZone);
        Shown.Absorption = FMath::Lerp(Shown.Absorption, Want.Absorption, Alpha);
        Shown.Scattering = FMath::Lerp(Shown.Scattering, Want.Scattering, Alpha);
        Shown.Caustics = FMath::Lerp(Shown.Caustics, Want.Caustics, Alpha);
        Shown.WaveScale = FMath::Lerp(Shown.WaveScale, Want.WaveScale, Alpha);
        OceanZoneWaveScale = Shown.WaveScale;
        Ocean->SetVectorParameterValue(TEXT("Absorption"), Shown.Absorption);
        Ocean->SetVectorParameterValue(TEXT("Scattering"), Shown.Scattering);
        Ocean->SetScalarParameterValue(TEXT("OceanCaustics"), Shown.Caustics);
    }
    float Wind = FMath::Clamp(FMath::IsFinite(WindSpeed) ? WindSpeed : 4.0f, 0.0f, 40.0f);
    // Test override (Options > Test > Sea state).
    const float SeaState = CVarSeaState.GetValueOnGameThread();
    if (SeaState >= 0.0f)
    {
        Wind = FMath::Lerp(1.0f, 26.0f, FMath::Clamp(SeaState, 0.0f, 1.0f));
        bStorm = SeaState > 0.75f;
    }
    const FVector2D Direction = WindTravelDirection.GetSafeNormal();
    // r595 (Stefan: "biggest waves we got, it's coming from a BIG ocean"): a long ocean swell is always
    // running (~1.6 m), the wind builds it to ~4 m; storms a bit more.
    const float WaveHeight = FMath::Clamp(150.0f + Wind * 10.0f, 160.0f, 400.0f) * (bStorm ? 1.2f : 1.0f) * OceanZoneWaveScale;
    const float Chop = FMath::Clamp(0.25f + Wind / 22.0f, 0.25f, 0.95f) * FMath::Lerp(0.8f, 1.0f, OceanZoneWaveScale);
    OceanWindDir = Direction.IsNearlyZero() ? FVector2D(1.0f, 0.0f) : Direction;
    OceanChopValue = Chop;
    OceanWaveHeightCm = WaveHeight;
    OceanSurgeCm = 6.0f + WaveHeight * 0.18f;
    Ocean->SetVectorParameterValue(TEXT("OceanWind"), FLinearColor(Direction.IsNearlyZero() ? 1.0f : Direction.X, Direction.Y, 0, 0));
    Ocean->SetScalarParameterValue(TEXT("OceanWaveHeight"), WaveHeight);
    Ocean->SetScalarParameterValue(TEXT("OceanChop"), Chop);
    Ocean->SetScalarParameterValue(TEXT("OceanSurge"), 6.0f + WaveHeight * 0.18f);
    Ocean->SetScalarParameterValue(TEXT("OceanRain"), FMath::Clamp(Rain, 0.0f, 1.0f));
    // Wind roughens the fine native ripples as well.
    const float Ripple = FMath::Clamp(Wind / 16.0f, 0.0f, 1.0f);
    Ocean->SetScalarParameterValue(TEXT("Default Near Normal Strength"), FMath::Lerp(0.12f, 0.45f, Ripple));
    Ocean->SetScalarParameterValue(TEXT("Default Distant Normal Strength"), FMath::Lerp(0.10f, 0.32f, Ripple));
}

// CPU mirror of OceanWaveCode (r700: 16 directions, wind lobe, shoaling) - without the shore refraction.
namespace WowOceanWaves
{
    static const float L[16] = {5200, 3300, 4400, 2300, 3800, 1700, 2900, 1300, 4800, 2600, 3600, 2000, 4100, 1500, 3100, 2700};
    static const float Off[16] = {0.05f, -0.08f, 0.07f, -0.04f, 0.09f, -0.02f, 0.06f, -0.09f, 0.03f, -0.06f, 0.08f, -0.05f, 0.02f, -0.07f, 0.04f, -0.03f};
    struct FWave { FVector2D D; float K, A, QA, Omega, Phase0; };
    static void Build(float DepthMeters, TArray<FWave, TInlineAllocator<16>>& Out)
    {
        const float Depth = FMath::Max(DepthMeters, 0.0f);
        const float Shoal = FMath::SmoothStep(0.0f, 3.5f, Depth);
        const float BreakZone = (1.0f - FMath::SmoothStep(5.0f, 12.0f, Depth)) * FMath::SmoothStep(1.0f, 3.0f, Depth);
        const float Grow = 1.0f + 0.5f * BreakZone;
        const float Steep = FMath::Lerp(1.0f, 2.2f, BreakZone);
        for (int32 i = 0; i < 16; ++i)
        {
            const float A0 = i * 0.3926991f + Off[i];
            FWave W;
            W.D = FVector2D(FMath::Cos(A0), FMath::Sin(A0));
            const float Weight = FMath::Pow(FMath::Clamp(static_cast<float>(FVector2D::DotProduct(W.D, OceanWindDir)), 0.0f, 1.0f), 10.0f) * 1.5f + 0.01f;
            W.K = 6.2831853f / L[i];
            W.A = OceanWaveHeightCm * Weight * (L[i] / 5200.0f) * 0.42f * Shoal * Grow;
            W.QA = FMath::Min((OceanChopValue * 0.11f * Steep) / FMath::Max(W.K * W.A, 1e-4f), 1.0f) * W.A;
            W.Omega = FMath::Sqrt(981.0f * W.K);
            W.Phase0 = i * 1.7f;
            Out.Add(W);
        }
    }
}

FVector2D FWowWaterMaterial::SampleOceanDrift(const FVector& WorldPosition, float WorldTime, float DepthMeters)
{
    // Mirror of disp.xy in OceanWaveCode (r229): what moves a floating swimmer back and forth.
    if (!OceanInstance.IsValid() || OceanWaveHeightCm <= 0.0f) return FVector2D::ZeroVector;
    TArray<WowOceanWaves::FWave, TInlineAllocator<16>> Waves;
    WowOceanWaves::Build(DepthMeters, Waves);
    const FVector2D P(WorldPosition.X, WorldPosition.Y);
    FVector2D Drift = FVector2D::ZeroVector;
    for (const WowOceanWaves::FWave& W : Waves)
    {
        const double Ph = double(W.K) * FVector2D::DotProduct(W.D, P) - double(W.Omega) * double(WorldTime) * 0.72 + W.Phase0;
        Drift += W.D * (W.QA * static_cast<float>(FMath::Cos(Ph)));
    }
    return Drift.ContainsNaN() ? FVector2D::ZeroVector : Drift;
}

float FWowWaterMaterial::SampleOceanHeight(const FVector& WorldPosition, float WorldTime, float DepthMeters)
{
    // Must stay in sync with OceanWaveCode (vertical part of the swell + shore surge).
    if (!OceanInstance.IsValid() || OceanWaveHeightCm <= 0.0f) return 0.0f;
    const float Depth = FMath::Max(DepthMeters, 0.0f);
    TArray<WowOceanWaves::FWave, TInlineAllocator<16>> Waves;
    WowOceanWaves::Build(DepthMeters, Waves);
    const FVector2D P(WorldPosition.X, WorldPosition.Y);
    float Height = 0.0f;
    for (const WowOceanWaves::FWave& W : Waves)
    {
        // Phase in double: world positions are large and the GPU uses the same expression.
        const double Ph = double(W.K) * FVector2D::DotProduct(W.D, P) - double(W.Omega) * double(WorldTime) * 0.72 + W.Phase0;
        Height += W.A * static_cast<float>(FMath::Sin(Ph));
    }
    const float Shore = 1.0f - FMath::SmoothStep(0.0f, 2.5f, Depth);
    const float SurgePhase = static_cast<float>(WorldTime * 0.75 + 2.0 * FMath::Sin(P.X * 0.0009) + 2.0 * FMath::Sin(P.Y * 0.0013));
    Height += OceanSurgeCm * Shore * (0.55f + 0.45f * FMath::Sin(SurgePhase));
    return FMath::IsFinite(Height) ? Height : 0.0f;
}
