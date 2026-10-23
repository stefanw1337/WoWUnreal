#include "WowWaterMaterial.h"
#include "Misc/ScopeExit.h"
#include "WowCookedMaterial.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWowWaterMat, Log, All);

// ── Water Material ─────────────────────────────────────────────────────────────

namespace
{
TWeakObjectPtr<UMaterialInstanceDynamic> RippleMaterial;
int32 NextRipple = 0;
float LastRippleClock = -1;
}

UMaterialInterface* FWowWaterMaterial::GetWaterMaterial()
{
	static TStrongObjectPtr<UMaterialInstanceDynamic> NativeRiver;
	static TStrongObjectPtr<UMaterialInterface> RetainedMaterial;
    UMaterialInterface* CachedMat = RetainedMaterial.Get();
    // Base materials outlive streamed tiles, including gaps during map changes.
    ON_SCOPE_EXIT { RetainedMaterial.Reset(CachedMat); };
	if (CachedMat) return CachedMat;

	if (UMaterialInterface* EngineWaterMaterial = GetRiverSurfaceParent())
	{
        auto* River = UMaterialInstanceDynamic::Create(EngineWaterMaterial, GetTransientPackage());
        // Native Water_Material computes absorption as 1 / (RGB * A), in cm,
        // and scattering as RGB * A / 1000. Longer green transmission gives
        // clear river water without the default ocean's strong blue/red loss.
        // r687 (Stefan, a Northshire pond: "no muddled water? It's not murky enough for me"): the water was
        // clear for about 10 m (the bottom showed in every detail). Now it is silty: red and blue are gone within
        // about a metre, green within 1.6 m, and the suspended mud scatters a brown-green glow back, so the
        // bottom fades out after a step or two from the shore.
        River->SetVectorParameterValue(TEXT("Absorption"), FLinearColor(35.0f, 55.0f, 30.0f, 3.0f));
        River->SetVectorParameterValue(TEXT("Scattering"), FLinearColor(0.90f, 0.80f, 0.45f, 2.5f));
        River->SetScalarParameterValue(TEXT("Water Roughness"), 0.06f);
        River->SetScalarParameterValue(TEXT("Water Specular"), 0.12f);
        // Visible bending of the bottom and plants through the surface (was barely 1.025).
        River->SetScalarParameterValue(TEXT("Refraction"), 1.12f);
        River->SetScalarParameterValue(TEXT("Refraction Far"), 1.04f);
        River->SetScalarParameterValue(TEXT("Default Near Normal Strength"), 0.09f);
        River->SetScalarParameterValue(TEXT("Default Distant Normal Strength"), 0.10f);
        River->SetScalarParameterValue(TEXT("Default Distant Normal StrengthB"), 0.08f);
        NativeRiver.Reset(River); // keep the shared runtime instance alive across tile unloads/GC
        CachedMat = River;
        RippleMaterial = River;
        UE_LOG(LogWowWaterMat, Log, TEXT("Using UE Water plugin with clear green river optics"));
		return CachedMat;
	}

#if WITH_EDITOR
	UMaterial* RuntimeMat = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_WowWater"));
	CachedMat = RuntimeMat;
	RuntimeMat->SetShadingModel(MSM_DefaultLit);
	// Keep the water shader simple enough to compile at runtime, but let scene
	// lighting and specular highlights sell it as water instead of flat blue ink.
	RuntimeMat->BlendMode = BLEND_Translucent;
	RuntimeMat->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
	RuntimeMat->TwoSided = true;
	RuntimeMat->OpacityMaskClipValue = 0.1f;

	auto& Exprs = RuntimeMat->GetExpressionCollection();

	// Keep this runtime material intentionally simple. If it fails to compile,
	// every liquid mesh renders as Unreal's default grey material.
	auto* WaterColor = NewObject<UMaterialExpressionConstant3Vector>(CachedMat);
	WaterColor->Constant = FLinearColor(0.000f, 0.38f, 0.66f, 1.0f);
	WaterColor->MaterialExpressionEditorX = -400;
	WaterColor->MaterialExpressionEditorY = 0;
	Exprs.AddExpression(WaterColor);

	auto* VertexColor = NewObject<UMaterialExpressionVertexColor>(CachedMat);
	VertexColor->MaterialExpressionEditorX = -760;
	VertexColor->MaterialExpressionEditorY = 80;
	Exprs.AddExpression(VertexColor);

	auto* VertexAlpha = NewObject<UMaterialExpressionComponentMask>(CachedMat);
	VertexAlpha->MaterialExpressionEditorX = -580;
	VertexAlpha->MaterialExpressionEditorY = 80;
	VertexAlpha->Input.Connect(0, VertexColor);
	// UE 5.7's material compiler exposes VertexColor as RGB for this runtime
	// path. Store water depth/opacity in R instead of reading alpha, otherwise
	// the shader fails to compile and Unreal renders the water as default grey.
	VertexAlpha->R = true;
	Exprs.AddExpression(VertexAlpha);

	auto* SubtleGlow = NewObject<UMaterialExpressionConstant3Vector>(CachedMat);
	SubtleGlow->Constant = FLinearColor(0.0f, 0.035f, 0.065f, 1.0f);
	SubtleGlow->MaterialExpressionEditorX = -400;
	SubtleGlow->MaterialExpressionEditorY = 120;
	Exprs.AddExpression(SubtleGlow);

	auto* Time = NewObject<UMaterialExpressionTime>(CachedMat);
	Time->MaterialExpressionEditorX = -760;
	Time->MaterialExpressionEditorY = 240;
	Exprs.AddExpression(Time);

	auto* WorldPosition = NewObject<UMaterialExpressionWorldPosition>(CachedMat);
	WorldPosition->MaterialExpressionEditorX = -1160;
	WorldPosition->MaterialExpressionEditorY = 230;
	Exprs.AddExpression(WorldPosition);

	auto* WorldX = NewObject<UMaterialExpressionComponentMask>(CachedMat);
	WorldX->MaterialExpressionEditorX = -980;
	WorldX->MaterialExpressionEditorY = 170;
	WorldX->Input.Connect(0, WorldPosition);
	WorldX->R = true;
	Exprs.AddExpression(WorldX);

	auto* WorldY = NewObject<UMaterialExpressionComponentMask>(CachedMat);
	WorldY->MaterialExpressionEditorX = -980;
	WorldY->MaterialExpressionEditorY = 290;
	WorldY->Input.Connect(0, WorldPosition);
	WorldY->G = true;
	Exprs.AddExpression(WorldY);

	auto* WaveScaleX = NewObject<UMaterialExpressionConstant>(CachedMat);
	WaveScaleX->R = 0.0018f;
	WaveScaleX->MaterialExpressionEditorX = -980;
	WaveScaleX->MaterialExpressionEditorY = 80;
	Exprs.AddExpression(WaveScaleX);

	auto* WaveScaleY = NewObject<UMaterialExpressionConstant>(CachedMat);
	WaveScaleY->R = 0.0026f;
	WaveScaleY->MaterialExpressionEditorX = -980;
	WaveScaleY->MaterialExpressionEditorY = 390;
	Exprs.AddExpression(WaveScaleY);

	auto* WorldXPhase = NewObject<UMaterialExpressionMultiply>(CachedMat);
	WorldXPhase->MaterialExpressionEditorX = -800;
	WorldXPhase->MaterialExpressionEditorY = 160;
	WorldXPhase->A.Connect(0, WorldX);
	WorldXPhase->B.Connect(0, WaveScaleX);
	Exprs.AddExpression(WorldXPhase);

	auto* WorldYPhase = NewObject<UMaterialExpressionMultiply>(CachedMat);
	WorldYPhase->MaterialExpressionEditorX = -800;
	WorldYPhase->MaterialExpressionEditorY = 310;
	WorldYPhase->A.Connect(0, WorldY);
	WorldYPhase->B.Connect(0, WaveScaleY);
	Exprs.AddExpression(WorldYPhase);

	auto* WorldPhase = NewObject<UMaterialExpressionAdd>(CachedMat);
	WorldPhase->MaterialExpressionEditorX = -620;
	WorldPhase->MaterialExpressionEditorY = 200;
	WorldPhase->A.Connect(0, WorldXPhase);
	WorldPhase->B.Connect(0, WorldYPhase);
	Exprs.AddExpression(WorldPhase);

	auto* WaveSpeed = NewObject<UMaterialExpressionConstant>(CachedMat);
	WaveSpeed->R = 0.18f;
	WaveSpeed->MaterialExpressionEditorX = -760;
	WaveSpeed->MaterialExpressionEditorY = 500;
	Exprs.AddExpression(WaveSpeed);

	auto* TimePhase = NewObject<UMaterialExpressionMultiply>(CachedMat);
	TimePhase->MaterialExpressionEditorX = -580;
	TimePhase->MaterialExpressionEditorY = 450;
	TimePhase->A.Connect(0, Time);
	TimePhase->B.Connect(0, WaveSpeed);
	Exprs.AddExpression(TimePhase);

	auto* AnimatedWavePhase = NewObject<UMaterialExpressionAdd>(CachedMat);
	AnimatedWavePhase->MaterialExpressionEditorX = -420;
	AnimatedWavePhase->MaterialExpressionEditorY = 250;
	AnimatedWavePhase->A.Connect(0, WorldPhase);
	AnimatedWavePhase->B.Connect(0, TimePhase);
	Exprs.AddExpression(AnimatedWavePhase);

	auto* WaveSine = NewObject<UMaterialExpressionSine>(CachedMat);
	WaveSine->MaterialExpressionEditorX = -580;
	WaveSine->MaterialExpressionEditorY = 240;
	WaveSine->Input.Connect(0, AnimatedWavePhase);
	Exprs.AddExpression(WaveSine);

	auto* WaveOffsetVector = NewObject<UMaterialExpressionConstant3Vector>(CachedMat);
	WaveOffsetVector->Constant = FLinearColor(0.0f, 0.0f, 10.0f, 1.0f);
	WaveOffsetVector->MaterialExpressionEditorX = -580;
	WaveOffsetVector->MaterialExpressionEditorY = 340;
	Exprs.AddExpression(WaveOffsetVector);

	auto* AnimatedWorldOffset = NewObject<UMaterialExpressionMultiply>(CachedMat);
	AnimatedWorldOffset->MaterialExpressionEditorX = -360;
	AnimatedWorldOffset->MaterialExpressionEditorY = 290;
	AnimatedWorldOffset->A.Connect(0, WaveOffsetVector);
	AnimatedWorldOffset->B.Connect(0, WaveSine);
	Exprs.AddExpression(AnimatedWorldOffset);

	auto* GlowPulseColor = NewObject<UMaterialExpressionConstant3Vector>(CachedMat);
	GlowPulseColor->Constant = FLinearColor(0.0f, 0.018f, 0.035f, 1.0f);
	GlowPulseColor->MaterialExpressionEditorX = -580;
	GlowPulseColor->MaterialExpressionEditorY = 460;
	Exprs.AddExpression(GlowPulseColor);

	auto* AnimatedGlow = NewObject<UMaterialExpressionMultiply>(CachedMat);
	AnimatedGlow->MaterialExpressionEditorX = -360;
	AnimatedGlow->MaterialExpressionEditorY = 430;
	AnimatedGlow->A.Connect(0, GlowPulseColor);
	AnimatedGlow->B.Connect(0, WaveSine);
	Exprs.AddExpression(AnimatedGlow);

	auto* FinalGlow = NewObject<UMaterialExpressionAdd>(CachedMat);
	FinalGlow->MaterialExpressionEditorX = -150;
	FinalGlow->MaterialExpressionEditorY = 250;
	FinalGlow->A.Connect(0, SubtleGlow);
	FinalGlow->B.Connect(0, AnimatedGlow);
	Exprs.AddExpression(FinalGlow);

	auto* Opacity = NewObject<UMaterialExpressionConstant>(CachedMat);
	Opacity->R = 0.88f;
	Opacity->MaterialExpressionEditorX = -400;
	Opacity->MaterialExpressionEditorY = 240;
	Exprs.AddExpression(Opacity);

	auto* DepthOpacity = NewObject<UMaterialExpressionMultiply>(CachedMat);
	DepthOpacity->MaterialExpressionEditorX = -190;
	DepthOpacity->MaterialExpressionEditorY = 170;
	DepthOpacity->A.Connect(0, VertexAlpha);
	DepthOpacity->B.Connect(0, Opacity);
	Exprs.AddExpression(DepthOpacity);

	auto* Specular = NewObject<UMaterialExpressionConstant>(CachedMat);
	Specular->R = 0.85f;
	Specular->MaterialExpressionEditorX = -400;
	Specular->MaterialExpressionEditorY = 360;
	Exprs.AddExpression(Specular);

	auto* Roughness = NewObject<UMaterialExpressionConstant>(CachedMat);
	Roughness->R = 0.045f;
	Roughness->MaterialExpressionEditorX = -400;
	Roughness->MaterialExpressionEditorY = 480;
	Exprs.AddExpression(Roughness);

	// --- Connect outputs ---
	RuntimeMat->GetEditorOnlyData()->BaseColor.Connect(0, WaterColor);
	RuntimeMat->GetEditorOnlyData()->Opacity.Connect(0, DepthOpacity);
	RuntimeMat->GetEditorOnlyData()->Specular.Connect(0, Specular);
	RuntimeMat->GetEditorOnlyData()->Roughness.Connect(0, Roughness);
	RuntimeMat->GetEditorOnlyData()->EmissiveColor.Connect(0, FinalGlow);
	RuntimeMat->GetEditorOnlyData()->WorldPositionOffset.Connect(0, AnimatedWorldOffset);

	RuntimeMat->PreEditChange(nullptr);
	RuntimeMat->PostEditChange();

	UE_LOG(LogWowWaterMat, Log, TEXT("Water material created (lit translucent shallow-water runtime material)"));
#else
	CachedMat = UMaterial::GetDefaultMaterial(MD_Surface);
	UE_LOG(LogWowWaterMat, Warning, TEXT("Non-editor: using default surface material for water"));
#endif

	return CachedMat;
}

// ── Lava Material ──────────────────────────────────────────────────────────────

UMaterial* FWowWaterMaterial::GetLavaMaterial()
{
	static TStrongObjectPtr<UMaterial> RetainedMaterial;
    UMaterial* CachedMat = RetainedMaterial.Get();
    // Base materials outlive streamed tiles, including gaps during map changes.
    ON_SCOPE_EXIT { RetainedMaterial.Reset(CachedMat); };
	if (CachedMat) return CachedMat;

    // r433 (Stefan: "the lava in Ironforge looks bad, replace it with our own"): the old material
    // wired the flat HDR orange straight to emissive (its noise was never connected), so every lava
    // pool was one blown-out yellow sheet. Our lava: dark cooled crust in slowly drifting plates,
    // molten orange-white cracks and pools between them, a slow pulse; world-space so WMO liquids
    // without useful UVs (Ironforge's forge channels) get the same scale as terrain lava.
    CachedMat = WowCookedMaterial::Load(TEXT("M_WowLava_v2"));
    if (CachedMat) return CachedMat;
#if WITH_EDITOR
	CachedMat = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_WowLava_v2"));
	CachedMat->SetShadingModel(MSM_DefaultLit);
	CachedMat->BlendMode = BLEND_Opaque;
	CachedMat->TwoSided = false;
	auto& Exprs = CachedMat->GetExpressionCollection();
	auto Const = [&](float V) { auto* E = NewObject<UMaterialExpressionConstant>(CachedMat); E->R = V; Exprs.AddExpression(E); return E; };
	auto Const3 = [&](const FLinearColor& C) { auto* E = NewObject<UMaterialExpressionConstant3Vector>(CachedMat); E->Constant = C; Exprs.AddExpression(E); return E; };
	auto Mul = [&](UMaterialExpression* A, UMaterialExpression* B) { auto* E = NewObject<UMaterialExpressionMultiply>(CachedMat); E->A.Connect(0, A); E->B.Connect(0, B); Exprs.AddExpression(E); return E; };
	auto Add = [&](UMaterialExpression* A, UMaterialExpression* B) { auto* E = NewObject<UMaterialExpressionAdd>(CachedMat); E->A.Connect(0, A); E->B.Connect(0, B); Exprs.AddExpression(E); return E; };
	auto Lerp = [&](UMaterialExpression* A, UMaterialExpression* B, UMaterialExpression* T) { auto* E = NewObject<UMaterialExpressionLinearInterpolate>(CachedMat); E->A.Connect(0, A); E->B.Connect(0, B); E->Alpha.Connect(0, T); Exprs.AddExpression(E); return E; };
	auto Noise = [&](UMaterialExpression* Position, float Scale, int32 Levels, bool bTurbulence, ENoiseFunction Function)
	{
		auto* E = NewObject<UMaterialExpressionNoise>(CachedMat);
		E->Scale = Scale; E->Quality = 1; E->NoiseFunction = Function; E->bTurbulence = bTurbulence; E->Levels = Levels;
		E->OutputMin = 0.f; E->OutputMax = 1.f; E->Position.Connect(0, Position);
		Exprs.AddExpression(E); return E;
	};

	auto* World = NewObject<UMaterialExpressionWorldPosition>(CachedMat); Exprs.AddExpression(World);
	auto* Time = NewObject<UMaterialExpressionTime>(CachedMat); Exprs.AddExpression(Time);
	// Plates drift slowly one way, the molten veins under them another.
	auto* Plates = Noise(Add(Mul(World, Const(0.0022f)), Mul(Time, Const3(FLinearColor(0.010f, 0.006f, 0.004f)))), 1.0f, 3, false, NOISEFUNCTION_VoronoiALU);
	auto* Veins = Noise(Add(Mul(World, Const(0.006f)), Mul(Time, Const3(FLinearColor(-0.012f, 0.017f, 0.009f)))), 1.0f, 4, true, NOISEFUNCTION_GradientALU);
	// Voronoi: 0 at a plate's centre, high at its edges = the molten cracks.
	auto* CrackRaw = Add(Mul(Plates, Const(1.25f)), Mul(Veins, Const(0.55f)));
	auto* Crack = NewObject<UMaterialExpressionClamp>(CachedMat);
	Crack->Input.Connect(0, Add(Mul(CrackRaw, Const(2.4f)), Const(-1.55f))); Crack->MinDefault = 0.f; Crack->MaxDefault = 1.f; Exprs.AddExpression(Crack);
	auto* Heat = Mul(Crack, Crack); // sharp bright cracks, wide dark crust
	// A slow breathing of the whole pool.
	auto* Pulse = NewObject<UMaterialExpressionSine>(CachedMat); Pulse->Period = 7.0f; Pulse->Input.Connect(0, Time); Exprs.AddExpression(Pulse);
	auto* Breath = Add(Mul(Pulse, Const(0.12f)), Const(1.0f));

	auto* CrustColour = Const3(FLinearColor(0.045f, 0.028f, 0.022f));
	auto* GlowColour = Const3(FLinearColor(0.42f, 0.09f, 0.01f));
	CachedMat->GetEditorOnlyData()->BaseColor.Connect(0, Lerp(CrustColour, GlowColour, Crack));
	// Crust embers dull red, cracks orange to near-white at the hottest.
	auto* Ember = Const3(FLinearColor(0.35f, 0.05f, 0.0f));
	auto* Molten = Const3(FLinearColor(5.5f, 1.55f, 0.22f));
	auto* WhiteHot = Const3(FLinearColor(9.0f, 4.5f, 1.2f));
	auto* Hot = Lerp(Molten, WhiteHot, Mul(Heat, Heat));
	CachedMat->GetEditorOnlyData()->EmissiveColor.Connect(0, Mul(Lerp(Mul(Ember, Veins), Hot, Heat), Breath));
	CachedMat->GetEditorOnlyData()->Roughness.Connect(0, Lerp(Const(0.92f), Const(0.4f), Crack));

	bool bNeedsRecompile = false;
	CachedMat->SetMaterialUsage(bNeedsRecompile, MATUSAGE_StaticMesh);
	CachedMat->PreEditChange(nullptr);
	CachedMat->PostEditChange();
    CachedMat = WowCookedMaterial::Save(CachedMat, TEXT("M_WowLava_v2"));
	UE_LOG(LogWowWaterMat, Log, TEXT("Lava material created (r433 crust and molten cracks)"));
#else
    // A package made before r433 has only the old lava.
    CachedMat = WowCookedMaterial::Load(TEXT("M_WowLavaCooked"));
	if (!CachedMat) CachedMat = UMaterial::GetDefaultMaterial(MD_Surface);
	UE_LOG(LogWowWaterMat, Warning, TEXT("Non-editor: M_WowLava_v2 missing, using the old lava"));
#endif

	return CachedMat;
}

// ── Slime Material ─────────────────────────────────────────────────────────────

UMaterial* FWowWaterMaterial::GetSlimeMaterial()
{
	static TStrongObjectPtr<UMaterial> RetainedMaterial;
    UMaterial* CachedMat = RetainedMaterial.Get();
    // Base materials outlive streamed tiles, including gaps during map changes.
    ON_SCOPE_EXIT { RetainedMaterial.Reset(CachedMat); };
	if (CachedMat) return CachedMat;

    CachedMat = WowCookedMaterial::Load(TEXT("M_WowSlimeCooked"));
    if (CachedMat) return CachedMat;
#if WITH_EDITOR
	CachedMat = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_WowSlime"));
	CachedMat->SetShadingModel(MSM_DefaultLit);
	CachedMat->BlendMode = BLEND_Opaque;
	CachedMat->TwoSided = false;

	auto& Exprs = CachedMat->GetExpressionCollection();

	// --- Green base color ---
	auto* BaseColor = NewObject<UMaterialExpressionConstant3Vector>(CachedMat);
	BaseColor->Constant = FLinearColor(0.08f, 0.25f, 0.05f);
	BaseColor->MaterialExpressionEditorX = -400;
	BaseColor->MaterialExpressionEditorY = 0;
	Exprs.AddExpression(BaseColor);

	// --- Slight emissive glow ---
	auto* EmissiveColor = NewObject<UMaterialExpressionConstant3Vector>(CachedMat);
	EmissiveColor->Constant = FLinearColor(0.3f, 1.0f, 0.1f); // Slight green glow
	EmissiveColor->MaterialExpressionEditorX = -600;
	EmissiveColor->MaterialExpressionEditorY = 200;
	Exprs.AddExpression(EmissiveColor);

	// Slow animated UV for slime
	auto* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(CachedMat);
	TexCoord->CoordinateIndex = 0;
	TexCoord->UTiling = 2.0f;
	TexCoord->VTiling = 2.0f;
	TexCoord->MaterialExpressionEditorX = -800;
	TexCoord->MaterialExpressionEditorY = 300;
	Exprs.AddExpression(TexCoord);

	auto* Panner = NewObject<UMaterialExpressionPanner>(CachedMat);
	Panner->SpeedX = 0.008f;
	Panner->SpeedY = 0.005f;
	Panner->MaterialExpressionEditorX = -600;
	Panner->MaterialExpressionEditorY = 300;
	Panner->Coordinate.Connect(0, TexCoord);
	Exprs.AddExpression(Panner);

	auto* Noise = NewObject<UMaterialExpressionNoise>(CachedMat);
	Noise->Scale = 8.0f;
	Noise->Quality = 1;
	Noise->NoiseFunction = NOISEFUNCTION_GradientALU;
	Noise->bTurbulence = true;
	Noise->Levels = 2;
	Noise->OutputMin = 0.4f;
	Noise->OutputMax = 1.0f;
	Noise->MaterialExpressionEditorX = -400;
	Noise->MaterialExpressionEditorY = 300;
	Noise->Position.Connect(0, Panner);
	Exprs.AddExpression(Noise);

	auto* EmissiveMul = NewObject<UMaterialExpressionMultiply>(CachedMat);
	EmissiveMul->MaterialExpressionEditorX = -200;
	EmissiveMul->MaterialExpressionEditorY = 200;
	EmissiveMul->A.Connect(0, EmissiveColor);
	EmissiveMul->B.Connect(0, Noise);
	Exprs.AddExpression(EmissiveMul);

	auto* Roughness = NewObject<UMaterialExpressionConstant>(CachedMat);
	Roughness->R = 0.4f;
	Roughness->MaterialExpressionEditorX = -200;
	Roughness->MaterialExpressionEditorY = 400;
	Exprs.AddExpression(Roughness);

	CachedMat->GetEditorOnlyData()->BaseColor.Connect(0, BaseColor);
	CachedMat->GetEditorOnlyData()->EmissiveColor.Connect(0, EmissiveColor);
	CachedMat->GetEditorOnlyData()->Roughness.Connect(0, Roughness);

	CachedMat->PreEditChange(nullptr);
	CachedMat->PostEditChange();
    CachedMat = WowCookedMaterial::Save(CachedMat, TEXT("M_WowSlimeCooked"));

	UE_LOG(LogWowWaterMat, Log, TEXT("Slime material created (simple emissive runtime slime)"));
#else
	CachedMat = UMaterial::GetDefaultMaterial(MD_Surface);
	UE_LOG(LogWowWaterMat, Warning, TEXT("Non-editor: using default surface material for slime"));
#endif

	return CachedMat;
}

// ── Category Lookup ────────────────────────────────────────────────────────────

UMaterialInterface* FWowWaterMaterial::GetMaterialForCategory(int32 Category)
{
	switch (Category)
	{
	case 0: return GetWaterMaterial();  // water
	case 1: return GetOceanMaterial();  // ocean: swell, surge, beach foam
	case 2: return GetLavaMaterial();   // magma
	case 3: return GetSlimeMaterial();  // slime
	default: return GetWaterMaterial();
	}
}

void FWowWaterMaterial::UpdateSurfaceRipples(float WorldTime)
{
    auto* Material = RippleMaterial.Get();
    if (!Material || !FMath::IsFinite(WorldTime)) return;
    // Clear surviving shared material state when a new world starts.
    if (WorldTime < LastRippleClock)
    {
        for (int32 I = 0; I < 8; ++I)
            Material->SetVectorParameterValue(*FString::Printf(TEXT("RipplePosition%d"), I), FLinearColor(0,0,0,-1000));
        NextRipple = 0;
    }
    LastRippleClock = WorldTime;
    Material->SetScalarParameterValue(TEXT("RippleClock"), WorldTime);
}

void FWowWaterMaterial::AddSurfaceRipple(const FVector& Position, const FVector& Direction, bool bSplash, float WorldTime)
{
    if (Position.ContainsNaN() || !FMath::IsFinite(WorldTime)) return;
    UpdateSurfaceRipples(WorldTime);
    auto* Material = RippleMaterial.Get();
    if (!Material) return;
    const FVector Facing = Direction.GetSafeNormal2D();
    Material->SetVectorParameterValue(*FString::Printf(TEXT("RipplePosition%d"), NextRipple),
        FLinearColor(Position.X, Position.Y, Position.Z, WorldTime));
    Material->SetVectorParameterValue(*FString::Printf(TEXT("RippleDirection%d"), NextRipple),
        FLinearColor(Facing.X, Facing.Y, bSplash ? 1.05f : .72f, bSplash ? 1.f : 0.f));
    NextRipple = (NextRipple + 1) % 8;
}

void FWowWaterMaterial::SetUnderwaterView(float Blend)
{
    if (auto* Material = RippleMaterial.Get())
    {
        const float Weight = FMath::Clamp(Blend, 0.0f, 1.0f);
        // Keep above-water highlights, but remove the mirror-like underside.
        Material->SetScalarParameterValue(TEXT("Water Specular"), FMath::Lerp(.12f, 0.0f, Weight));
        // r553 (Stefan: "get rid of my own reflection when I'm under water"): the single-layer water
        // underside still mirrored the scene below it, the character included, through screen and
        // Lumen reflection traces. A fully rough underside is not traced (Lumen traces up to roughness
        // 0.4), so the surface seen from below keeps its light and colour but no mirror image.
        Material->SetScalarParameterValue(TEXT("Water Roughness"), FMath::Lerp(.06f, 1.0f, Weight));
        Material->SetScalarParameterValue(TEXT("Refraction"), FMath::Lerp(1.12f, 1.0f, Weight));
        Material->SetScalarParameterValue(TEXT("Refraction Far"), FMath::Lerp(1.04f, 1.0f, Weight));
    }
}
