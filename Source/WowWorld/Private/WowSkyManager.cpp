#include "WowSkyManager.h"
#include "WowDuskLightComponent.h"
#include "WowTestControls.h"
#include "WowWeatherComponent.h"
#include "WowConnectionManager.h"
#include "WowLunarPhase.h"
#include "WowRuntimeMeshMaterials.h"
#include "WowCookedMaterial.h"
#include "WowClientPreferences.h"
#include "WowIndoorFogMask.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/PostProcessComponent.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/LightDbc.h"
#include "Formats/Dbc/LightParamsDbc.h"
#include "Formats/Dbc/LightIntParamsDbc.h"
#include "Formats/Dbc/LightFloatParamsDbc.h"
#include "Coord/WowCoordinate.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionTruncateLWC.h"
#include "Materials/MaterialExpressionCloudLayer.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionVolumetricAdvancedMaterialOutput.h"
#include "Materials/MaterialExpressionUtils.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Engine/VolumeTexture.h"
#include "Camera/PlayerCameraManager.h"
#include "Scalability.h"
#include "Engine/DirectionalLight.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowSky, Log, All);

namespace
{
TAutoConsoleVariable<int32> TestMenuEnabled(TEXT("wow.TestMenuEnabled"),1,TEXT("Enable the development Test menu and its overrides. Ignored in Shipping builds."));
TAutoConsoleVariable<float> CVarDistanceFog(TEXT("wow.DistanceFog"),1.f,TEXT("WoW-style distance fog (Light.dbc fog end/start) that hides far mountains in the sky colour: 0 off (old light haze), 1 WoW distance, >1 thicker, <1 thinner."));
TAutoConsoleVariable<float> TestTime(TEXT("wow.TestTime"),-1.f,TEXT("Session-only sky clock in minutes, -1 uses the normal clock."));
	bool UsesDarkerLightingProfile()
	{
		// Explicit test flags must override the saved pre-login preference so the
		// two profiles can be compared without changing persistent settings.
		if (FParse::Param(FCommandLine::Get(), TEXT("standardlighting")))
		{
			return false;
		}

		return FParse::Param(FCommandLine::Get(), TEXT("darkerlighting")) ||
			FWowClientPreferences::IsDarkerRealisticLightingEnabled();
	}

constexpr float WowDaySunriseMinutes = 330.0f;
constexpr float WowDaySunsetMinutes = 1110.0f;
constexpr float WowSkyMaxSunElevationDegrees = 72.0f;
constexpr float WowSkyMinSunElevationDegrees = -14.0f;

float WrapDayMinutes(float TimeOfDay)
{
	float Wrapped = FMath::Fmod(TimeOfDay, 1440.0f);
	return Wrapped < 0.0f ? Wrapped + 1440.0f : Wrapped;
}

float SmoothStep01(float Value)
{
	const float Clamped = FMath::Clamp(Value, 0.0f, 1.0f);
	return Clamped * Clamped * (3.0f - 2.0f * Clamped);
}

// r540: 1 by day, 0 once the night sky is half in (the moon starts there).
float WowSkySunDuskFade(float TimeOfDay)
{
	return 1.f - SmoothStep01(WowLunarPhase::NightVisibility(WrapDayMinutes(TimeOfDay)) * 2.f);
}

void CalculateWowSunAngles(float TimeOfDay, float& OutPitch, float& OutYaw, bool& bOutSunAboveHorizon)
{
	const float T = WrapDayMinutes(TimeOfDay);
	if (T >= WowDaySunriseMinutes && T <= WowDaySunsetMinutes)
	{
		const float DayAlpha = (T - WowDaySunriseMinutes) / (WowDaySunsetMinutes - WowDaySunriseMinutes);
		const float Elevation = 3.0f + FMath::Sin(DayAlpha * PI) * WowSkyMaxSunElevationDegrees;
		OutPitch = -Elevation;
		OutYaw = FMath::Lerp(115.0f, 295.0f, DayAlpha);
		bOutSunAboveHorizon = true;
		return;
	}

	const float NightDuration = 1440.0f - (WowDaySunsetMinutes - WowDaySunriseMinutes);
	const float NightTime = T > WowDaySunsetMinutes
		? T - WowDaySunsetMinutes
		: T + (1440.0f - WowDaySunsetMinutes);
	const float NightAlpha = NightTime / NightDuration;
	const float Elevation = WowSkyMinSunElevationDegrees - FMath::Sin(NightAlpha * PI) * 32.0f;
	OutPitch = -Elevation;
	OutYaw = FMath::Lerp(295.0f, 475.0f, NightAlpha);
	bOutSunAboveHorizon = false;
}

FVector GetSkyViewLocation(const UObject* Context)
{
	if (!Context)
	{
		return FVector::ZeroVector;
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(Context, 0))
	{
		if (PC->PlayerCameraManager)
		{
			return PC->PlayerCameraManager->GetCameraLocation();
		}
	}

	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(Context, 0))
	{
		return Pawn->GetActorLocation();
	}

	return FVector::ZeroVector;
}

FLinearColor ClampSkyColor(const FLinearColor& Color, const FLinearColor& Minimum, float Boost)
{
	return FLinearColor(
		FMath::Clamp(FMath::Max(Color.R, Minimum.R) * Boost, 0.0f, 1.0f),
		FMath::Clamp(FMath::Max(Color.G, Minimum.G) * Boost, 0.0f, 1.0f),
		FMath::Clamp(FMath::Max(Color.B, Minimum.B) * Boost, 0.0f, 1.0f),
		1.0f);
}

void SetSkyCVarIfAvailable(const TCHAR* Name, int32 Value)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		CVar->Set(Value, ECVF_SetByCode);
	}
}

void EnsureModernSkyRenderingEnabled()
{
	SetSkyCVarIfAvailable(TEXT("r.SkyAtmosphere"), 1);
	SetSkyCVarIfAvailable(TEXT("r.VolumetricCloud"), 1);
	SetSkyCVarIfAvailable(TEXT("r.SupportSkyAtmosphereAffectsHeightFog"), 1);
	// Clouds: trace at quarter resolution but reconstruct at FULL resolution and composite behind
	// all opaque geometry (mode 2; our cloud layer starts 3.6 km up, above the terrain). The default
	// mode 0 upsamples a half-resolution cloud image with a depth-aware filter, and along every
	// mountain ridge and tree against the sky some pixels took the cloud colour instead of the
	// fogged ground: Stefan's dotted outlines in snow and fog, unaffected by volumetric fog,
	// Lumen, ray tracing or anti-aliasing (r238).
	SetSkyCVarIfAvailable(TEXT("r.VolumetricRenderTarget.Mode"), 2);
}
}

AWowSkyManager::AWowSkyManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f; // update 10x/sec

	USceneComponent* SkyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SkyRoot"));
	SetRootComponent(SkyRoot);
    Weather=CreateDefaultSubobject<UWowWeatherComponent>(TEXT("Weather"));
}

void AWowSkyManager::BeginPlay()
{
	Super::BeginPlay();

	EnsureModernSkyRenderingEnabled();
	const bool bDarkerRealisticLighting = UsesDarkerLightingProfile();
	UE_LOG(LogWowSky, Log, TEXT("Lighting profile selected: %s (standardlighting=%s, darkerlighting=%s)"),
		bDarkerRealisticLighting ? TEXT("darker realistic") : TEXT("standard"),
		FParse::Param(FCommandLine::Get(), TEXT("standardlighting")) ? TEXT("yes") : TEXT("no"),
		FParse::Param(FCommandLine::Get(), TEXT("darkerlighting")) ? TEXT("yes") : TEXT("no"));

	TArray<AActor*> ExistingSkyManagers;
	UGameplayStatics::GetAllActorsOfClass(this, AWowSkyManager::StaticClass(), ExistingSkyManagers);
	for (AActor* ExistingActor : ExistingSkyManagers)
	{
		if (ExistingActor && ExistingActor != this)
		{
			ExistingActor->Destroy();
		}
	}

	// The SkyManager owns the day/night directional light. Disable any map
	// fallback directional light so forward shading does not print the
	// "multiple directional lights" warning over gameplay.
	TArray<AActor*> ExistingDirectionalLights;
	UGameplayStatics::GetAllActorsOfClass(this, ADirectionalLight::StaticClass(), ExistingDirectionalLights);
	for (AActor* ExistingActor : ExistingDirectionalLights)
	{
		if (ADirectionalLight* ExistingLight = Cast<ADirectionalLight>(ExistingActor))
		{
			if (UDirectionalLightComponent* ExistingComponent =
				Cast<UDirectionalLightComponent>(ExistingLight->GetLightComponent()))
			{
				ExistingComponent->SetVisibility(false);
				ExistingComponent->SetIntensity(0.0f);
				ExistingComponent->SetForwardShadingPriority(0);
			}
			ExistingLight->SetActorHiddenInGame(true);
			ExistingLight->Destroy();
		}
	}

    auto CreateFill = [this](const TCHAR* Name, float Yaw)
    {
        auto* Light = NewObject<UDirectionalLightComponent>(this, Name);
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetIntensity(0.0f);
        Light->SetCastShadows(false);
        Light->SetIndirectLightingIntensity(0.0f);
        Light->SetVolumetricScatteringIntensity(0.0f);
        Light->SetSpecularScale(0.0f);
        // Fill lights must never count as an atmosphere sun: UE then draws a sun disc and
        // a Mie halo where each one points, which showed up as a "sun" in the night sky.
        Light->SetAtmosphereSunLight(false);
        Light->SetAtmosphereSunDiskColorScale(FLinearColor::Black);
        Light->bCastCloudShadows = false;
        Light->CloudScatteredLuminanceScale = FLinearColor::Black;
        Light->RegisterComponent();
        Light->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        Light->SetWorldRotation(FRotator(-55.0f, Yaw, 0.0f));
        return Light;
    };
    StandardFillLight = CreateFill(TEXT("StandardReadabilityFill"), 45.0f);
    StandardBackFillLight = CreateFill(TEXT("StandardReadabilityBackFill"), 225.0f);
    StandardLeftFillLight = CreateFill(TEXT("StandardReadabilityLeftFill"), 135.0f);
    StandardRightFillLight = CreateFill(TEXT("StandardReadabilityRightFill"), 315.0f);

	// Create sun directional light
	SunLight = NewObject<UDirectionalLightComponent>(this, TEXT("SunLight"));
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->RegisterComponent();
	SunLight->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	SunLight->SetIntensity(2.75f);
	SunLight->SetLightColor(FLinearColor(1.0f, 0.95f, 0.85f));
	SunLight->SetCastShadows(true);
	SunLight->SetForwardShadingPriority(1);
	SunLight->SetAtmosphereSunLight(true);
	SunLight->SetAtmosphereSunLightIndex(0);
	SunLight->SetAtmosphereSunDiskColorScale(FLinearColor(1.0f, 0.92f, 0.74f, 1.0f));
	SunLight->SetIndirectLightingIntensity(1.15f);
	SunLight->SetVolumetricScatteringIntensity(0.35f);
	SunLight->SetLightSourceAngle(1.15f);
	SunLight->SetLightSourceSoftAngle(2.0f);
	SunLight->SetShadowAmount(0.54f);
	SunLight->SetShadowBias(0.65f);
	SunLight->ContactShadowLength = 0.0f;
	SunLight->SetDynamicShadowDistanceMovableLight(45000.0f);
	SunLight->SetDynamicShadowCascades(3);
	SunLight->SetCascadeTransitionFraction(0.22f);
	SunLight->SetShadowDistanceFadeoutFraction(0.25f);
	SunLight->bCastCloudShadows = true;
	SunLight->CloudShadowStrength = 0.42f;
	SunLight->CloudShadowOnSurfaceStrength = 0.28f;
	SunLight->CloudShadowOnAtmosphereStrength = 0.22f;
	SunLight->MarkRenderStateDirty(); // raw writes above happen after RegisterComponent

	// Keep only one directional light active for forward shading. Night tint is
	// driven by the sky/fog colors until a dedicated moon path is implemented.
	MoonLight = nullptr;

	// Create sky atmosphere
	SkyAtmosphere = NewObject<USkyAtmosphereComponent>(this, TEXT("SkyAtmosphere"));
	SkyAtmosphere->SetMobility(EComponentMobility::Movable);
	SkyAtmosphere->RegisterComponent();
	SkyAtmosphere->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	// These setters take physical scattering coefficients in inverse kilometers,
	// not normalized strength multipliers. Stay close to Unreal's Earth values
	// so a midday sun produces a blue sky instead of an optically dense brown haze.
	SkyAtmosphere->SetRayleighScatteringScale(0.0358f);
	SkyAtmosphere->SetMieScatteringScale(0.0032f);
	SkyAtmosphere->SetMieAbsorptionScale(0.00038f);
	SkyAtmosphere->SetMieAnisotropy(0.78f);
	SkyAtmosphere->SetHeightFogContribution(0.34f);
	SkyAtmosphere->SetAerialPespectiveViewDistanceScale(0.38f);
	SkyAtmosphere->SetSkyAndAerialPerspectiveLuminanceFactor(FLinearColor(1.08f, 1.10f, 1.14f, 1.0f));
	SkyAtmosphere->SetVisibility(true, true);

	// Create sky light for ambient
	SkyLight = NewObject<USkyLightComponent>(this, TEXT("SkyLight"));
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->RegisterComponent();
	SkyLight->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	SkyLight->SetIntensity(2.05f);
	SkyLight->SetIndirectLightingIntensity(1.0f);
	SkyLight->SetLowerHemisphereColor(bDarkerRealisticLighting
		? FLinearColor(0.08f, 0.10f, 0.15f)
		: FLinearColor(0.12f, 0.15f, 0.22f));
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetRealTimeCapture(false);
	SkyLight->SetRealTimeCaptureEnabled(false);

	// Create height fog
	if (bFogEnabled && !FParse::Param(FCommandLine::Get(), TEXT("noheightfog")))
	{
		HeightFog = NewObject<UExponentialHeightFogComponent>(this, TEXT("HeightFog"));
		HeightFog->SetMobility(EComponentMobility::Movable);
		HeightFog->RegisterComponent();
		HeightFog->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		HeightFog->SetFogDensity(FogDensity);
		HeightFog->SetFogMaxOpacity(0.22f);
		HeightFog->SetFogHeightFalloff(0.00048f);
		HeightFog->SetStartDistance(85000.0f);
	}
	else if (bFogEnabled)
	{
		UE_LOG(LogWowSky, Log, TEXT("Height fog disabled by command line"));
	}

	// The native atmosphere is the default. The procedural dome remains an
	// explicit fallback for diagnosing unsupported rendering paths.
	if (FParse::Param(FCommandLine::Get(), TEXT("legacyskydome")))
	{
		SkyAtmosphere->SetVisibility(false, true);
		CreateSkydomeMesh();
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("engineskydome")))
	{
		CreateAtmosphereSkydomeMesh();
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("noskydome")))
	{
		UE_LOG(LogWowSky, Log, TEXT("Visible sky dome disabled by command line"));
	}
	else
	{
		UE_LOG(LogWowSky, Log, TEXT("Using native SkyAtmosphere background"));
	}

	// Native UE cloud layer for atmospheric lighting plus a lightweight
	// procedural high cloud mesh for a visibly readable sky in early runtime
	// levels. The mesh is non-colliding, camera-following, and cheap.
	CreateVolumetricCloudLayer();
	if (!VolumetricCloudMaterial)
	{
		CreateCloudMesh();
	}

	// Create sun/moon disc billboards
	CreateCelestialDiscs();
    CreateNightSky();

	TimeOfDay = FDateTime::Now().GetTimeOfDay().GetTotalMinutes();
	// Parse -timeofday= command-line override for deterministic verification
	float CmdTimeOfDay = -1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("-timeofday="), CmdTimeOfDay))
	{
		TimeOfDay = FMath::Clamp(CmdTimeOfDay, 0.0f, 1439.9f);
		bTimeOverride = true;
		TimeSpeed = 0.0f; // Freeze time for screenshots
		UE_LOG(LogWowSky, Log, TEXT("Time-of-day override: %.0f minutes (frozen)"), TimeOfDay);
	}

	// Load light zones from DBC
	LoadLightZones(CurrentMapId);

	// Check if float params are available
	bHasFloatParams = FDbcStore::Get().LightFloatParams().Num() > 0;

	// Initial update
	UpdateSunPosition();
	UpdateLightColors();
	UpdateFog();
	UpdateSkydome();
	UpdateClouds();
	UpdateCelestialDiscs();
	UpdateNightSky();
	if (SkyLight)
	{
		SkyLight->RecaptureSky();
        bSkyCapturePending = false;
	}

	UE_LOG(LogWowSky, Log, TEXT("Sky manager initialized: time=%.2f, speed=%.8f, dbcLights=%d, floatParams=%s"),
		TimeOfDay, TimeSpeed, MapLights.Num(), bHasFloatParams ? TEXT("yes") : TEXT("no"));
}

// ── Skydome Mesh Creation ────────────────────────────────────────────────────────

static UStaticMesh* BuildDomeMesh(
	UObject* Outer,
	const TArray<FVector>& Vertices,
	const TArray<int32>& Indices,
	const TArray<FVector4f>& Colors)
{
	FMeshDescription MeshDesc;
	FStaticMeshAttributes Attrs(MeshDesc);
	Attrs.Register();

	TVertexAttributesRef<FVector3f> Positions = Attrs.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attrs.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attrs.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> BinormalSigns = Attrs.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attrs.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector4f> VColors = Attrs.GetVertexInstanceColors();

	FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();
	MeshDesc.ReserveNewVertices(Vertices.Num());
	MeshDesc.ReserveNewVertexInstances(Vertices.Num());
	MeshDesc.ReserveNewPolygons(Indices.Num() / 3);

	TArray<FVertexInstanceID> InstanceIDs;
	InstanceIDs.SetNum(Vertices.Num());

	for (int32 v = 0; v < Vertices.Num(); ++v)
	{
		FVertexID VID = MeshDesc.CreateVertex();
		Positions[VID] = FVector3f(Vertices[v]);

		FVertexInstanceID IID = MeshDesc.CreateVertexInstance(VID);
		InstanceIDs[v] = IID;

		Normals[IID] = FVector3f(0, 0, -1);
		Tangents[IID] = FVector3f(1, 0, 0);
		BinormalSigns[IID] = 1.0f;
		UVs.Set(IID, 0, FVector2f(Colors[v].W, 0.0f)); // Store skydome height in U when provided.
		VColors[IID] = Colors[v];
	}

	for (int32 t = 0; t < Indices.Num(); t += 3)
	{
		TArray<FVertexInstanceID> Tri;
		Tri.Add(InstanceIDs[Indices[t]]);
		Tri.Add(InstanceIDs[Indices[t + 1]]);
		Tri.Add(InstanceIDs[Indices[t + 2]]);
		MeshDesc.CreatePolygon(PolyGroup, Tri);
	}

	UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
	FStaticMaterial StaticMaterial;
	StaticMaterial.UVChannelData.bInitialized = true;
	SM->GetStaticMaterials().Add(MoveTemp(StaticMaterial));
	TArray<const FMeshDescription*> Descs = {&MeshDesc};
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	WowPrepareRuntimeMeshMaterials(SM, const_cast<FMeshDescription&>(*Descs[0]));
	SM->BuildFromMeshDescriptions(Descs, Params);
	SM->NeverStream = true;
	return SM;
}

void AWowSkyManager::CreateSkydomeMesh()
{
	const int32 NumRings = 24;
	const int32 NumSegments = 64;
	const float Radius = 720000.0f; // 7.2km; close enough to avoid far-clip/gray fallback issues
	const FLinearColor InitialTopColor = GetSkyColorFallback();
	const FLinearColor InitialHorizonColor = GetFogColorFallback();
	const FLinearColor InitialMiddleColor = FMath::Lerp(InitialTopColor, InitialHorizonColor, 0.35f);

	TArray<FVector> Verts;
	TArray<FVector4f> Colors;
	TArray<int32> Indices;

	// Generate vertices: rings from horizon (ring 0) to zenith (ring NumRings-1)
	for (int32 Ring = 0; Ring < NumRings; ++Ring)
	{
		float HeightFrac = static_cast<float>(Ring) / (NumRings - 1); // 0=horizon, 1=zenith
		float Phi = (PI / 2.0f) * HeightFrac;
		float Y = FMath::Sin(Phi) * Radius;
		float RingRadius = FMath::Cos(Phi) * Radius;

		for (int32 Seg = 0; Seg < NumSegments; ++Seg)
		{
			float Theta = 2.0f * PI * Seg / NumSegments;
			float X = FMath::Cos(Theta) * RingRadius;
			float Z = FMath::Sin(Theta) * RingRadius;
			Verts.Add(FVector(X, Z, Y));

			const float LowCloudBand = SmoothStep01(1.0f - FMath::Abs(HeightFrac - 0.34f) / 0.24f) * 0.32f;
			const float MidCloudBand = SmoothStep01(1.0f - FMath::Abs(HeightFrac - 0.58f) / 0.30f) * 0.74f;
			const float HighCloudBand = SmoothStep01(1.0f - FMath::Abs(HeightFrac - 0.78f) / 0.18f) * 0.36f;
			const float CloudBand = FMath::Clamp(LowCloudBand + MidCloudBand + HighCloudBand, 0.0f, 1.0f);
			const float CloudNoiseA = FMath::PerlinNoise2D(FVector2D(
				FMath::Cos(Theta) * 2.4f + Ring * 0.09f,
				FMath::Sin(Theta) * 2.4f + HeightFrac * 4.3f));
			const float CloudNoiseB = FMath::PerlinNoise2D(FVector2D(
				FMath::Cos(Theta) * 6.8f - 11.0f,
				FMath::Sin(Theta) * 6.8f + 7.0f + HeightFrac * 3.2f));
			const float CloudNoiseC = FMath::PerlinNoise2D(FVector2D(
				FMath::Cos(Theta) * 14.0f + 21.0f,
				FMath::Sin(Theta) * 14.0f - 5.0f));
			const float RawCloud = CloudNoiseA * 0.52f + CloudNoiseB * 0.32f + CloudNoiseC * 0.16f + 0.18f;
			const float NoiseCloudMask = SmoothStep01((RawCloud - 0.28f) / 0.24f) * CloudBand;
			const float WispyBand = SmoothStep01((FMath::Sin(Theta * 3.0f + HeightFrac * 13.0f) * 0.5f + 0.5f - 0.62f) / 0.22f) * CloudBand * 0.16f;
			const float CloudMask = FMath::Clamp(FMath::Max(NoiseCloudMask, WispyBand), 0.0f, 1.0f);

			// R stores cloud mask for the material; W stores height for gradient lookup.
			Colors.Add(FVector4f(CloudMask, 0.0f, 0.0f, HeightFrac));
		}
	}

	// Zenith cap vertex
	Verts.Add(FVector(0.0f, 0.0f, Radius));
	Colors.Add(FVector4f(0.0f, 0.0f, 0.0f, 1.0f));

	auto GetIdx = [&](int32 Ring, int32 Seg) -> int32
	{
		return Ring * NumSegments + (Seg % NumSegments);
	};

	// Triangles between rings (inverted winding for inside view)
	for (int32 Ring = 0; Ring < NumRings - 1; ++Ring)
	{
		for (int32 Seg = 0; Seg < NumSegments; ++Seg)
		{
			int32 I00 = GetIdx(Ring, Seg);
			int32 I10 = GetIdx(Ring, Seg + 1);
			int32 I01 = GetIdx(Ring + 1, Seg);
			int32 I11 = GetIdx(Ring + 1, Seg + 1);

			// Triangle 1 (inverted winding)
			Indices.Add(I00);
			Indices.Add(I01);
			Indices.Add(I10);

			// Triangle 2 (inverted winding)
			Indices.Add(I10);
			Indices.Add(I01);
			Indices.Add(I11);
		}
	}

	// Cap triangles connecting last ring to zenith
	int32 ZenithIdx = Verts.Num() - 1;
	int32 LastRing = NumRings - 1;
	for (int32 Seg = 0; Seg < NumSegments; ++Seg)
	{
		Indices.Add(GetIdx(LastRing, Seg));
		Indices.Add(ZenithIdx);
		Indices.Add(GetIdx(LastRing, Seg + 1));
	}

	UStaticMesh* Mesh = BuildDomeMesh(this, Verts, Indices, Colors);

	// Create skydome material: unlit, two-sided, UV0.x = height, vertex color R = cloud mask.
    UMaterial* BaseMat = WowCookedMaterial::Load(TEXT("M_WowSky"));
#if WITH_EDITOR
    if (!BaseMat)
    {
        BaseMat = NewObject<UMaterial>(this);

	BaseMat->SetShadingModel(MSM_Unlit);
	BaseMat->TwoSided = true;
	BaseMat->BlendMode = BLEND_Opaque;
	BaseMat->bIsSky = true;

	auto AddVectorParameter = [BaseMat](const FName& Name, const FLinearColor& DefaultValue, int32 X, int32 Y)
	{
		auto* Param = NewObject<UMaterialExpressionVectorParameter>(BaseMat);
		Param->ParameterName = Name;
		Param->DefaultValue = DefaultValue;
		Param->MaterialExpressionEditorX = X;
		Param->MaterialExpressionEditorY = Y;
		BaseMat->GetExpressionCollection().AddExpression(Param);
		return Param;
	};

	auto AddScalarParameter = [BaseMat](const FName& Name, float DefaultValue, int32 X, int32 Y)
	{
		auto* Param = NewObject<UMaterialExpressionScalarParameter>(BaseMat);
		Param->ParameterName = Name;
		Param->DefaultValue = DefaultValue;
		Param->MaterialExpressionEditorX = X;
		Param->MaterialExpressionEditorY = Y;
		BaseMat->GetExpressionCollection().AddExpression(Param);
		return Param;
	};

	auto* PHorizon = AddVectorParameter(TEXT("SkyHorizonColor"), InitialHorizonColor, -980, -220);
	auto* PMiddle = AddVectorParameter(TEXT("SkyMiddleColor"), InitialMiddleColor, -980, -70);
	auto* PTop = AddVectorParameter(TEXT("SkyTopColor"), InitialTopColor, -980, 80);
	auto* PCloudColor = AddVectorParameter(TEXT("SkyCloudColor"), FLinearColor(1.06f, 1.10f, 1.16f, 1.0f), -980, 250);
	auto* PCloudOpacity = AddScalarParameter(TEXT("SkyCloudOpacity"), 0.62f, -980, 390);

	auto* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(BaseMat);
	TexCoord->MaterialExpressionEditorX = -760;
	TexCoord->MaterialExpressionEditorY = -110;
	BaseMat->GetExpressionCollection().AddExpression(TexCoord);

	auto* HeightMask = NewObject<UMaterialExpressionComponentMask>(BaseMat);
	HeightMask->MaterialExpressionEditorX = -560;
	HeightMask->MaterialExpressionEditorY = -110;
	HeightMask->Input.Connect(0, TexCoord);
	HeightMask->R = true;
	BaseMat->GetExpressionCollection().AddExpression(HeightMask);

	auto* HeightSquared = NewObject<UMaterialExpressionMultiply>(BaseMat);
	HeightSquared->MaterialExpressionEditorX = -350;
	HeightSquared->MaterialExpressionEditorY = -20;
	HeightSquared->A.Connect(0, HeightMask);
	HeightSquared->B.Connect(0, HeightMask);
	BaseMat->GetExpressionCollection().AddExpression(HeightSquared);

	auto* HorizonToMiddle = NewObject<UMaterialExpressionLinearInterpolate>(BaseMat);
	HorizonToMiddle->MaterialExpressionEditorX = -340;
	HorizonToMiddle->MaterialExpressionEditorY = -170;
	HorizonToMiddle->A.Connect(0, PHorizon);
	HorizonToMiddle->B.Connect(0, PMiddle);
	HorizonToMiddle->Alpha.Connect(0, HeightMask);
	BaseMat->GetExpressionCollection().AddExpression(HorizonToMiddle);

	auto* SkyGradient = NewObject<UMaterialExpressionLinearInterpolate>(BaseMat);
	SkyGradient->MaterialExpressionEditorX = -110;
	SkyGradient->MaterialExpressionEditorY = -100;
	SkyGradient->A.Connect(0, HorizonToMiddle);
	SkyGradient->B.Connect(0, PTop);
	SkyGradient->Alpha.Connect(0, HeightSquared);
	BaseMat->GetExpressionCollection().AddExpression(SkyGradient);

	auto* VertexColor = NewObject<UMaterialExpressionVertexColor>(BaseMat);
	VertexColor->MaterialExpressionEditorX = -760;
	VertexColor->MaterialExpressionEditorY = 240;
	BaseMat->GetExpressionCollection().AddExpression(VertexColor);

	auto* CloudMask = NewObject<UMaterialExpressionComponentMask>(BaseMat);
	CloudMask->MaterialExpressionEditorX = -560;
	CloudMask->MaterialExpressionEditorY = 240;
	CloudMask->Input.Connect(0, VertexColor);
	CloudMask->R = true;
	BaseMat->GetExpressionCollection().AddExpression(CloudMask);

	auto* CloudAlpha = NewObject<UMaterialExpressionMultiply>(BaseMat);
	CloudAlpha->MaterialExpressionEditorX = -350;
	CloudAlpha->MaterialExpressionEditorY = 250;
	CloudAlpha->A.Connect(0, PCloudOpacity);
	CloudAlpha->B.Connect(0, CloudMask);
	BaseMat->GetExpressionCollection().AddExpression(CloudAlpha);

	auto* SkyWithClouds = NewObject<UMaterialExpressionLinearInterpolate>(BaseMat);
	SkyWithClouds->MaterialExpressionEditorX = 140;
	SkyWithClouds->MaterialExpressionEditorY = 0;
	SkyWithClouds->A.Connect(0, SkyGradient);
	SkyWithClouds->B.Connect(0, PCloudColor);
	SkyWithClouds->Alpha.Connect(0, CloudAlpha);
	BaseMat->GetExpressionCollection().AddExpression(SkyWithClouds);

	BaseMat->GetEditorOnlyData()->EmissiveColor.Connect(0, SkyWithClouds);
	BaseMat->PreEditChange(nullptr);
	BaseMat->PostEditChange();

        BaseMat = WowCookedMaterial::Save(BaseMat, TEXT("M_WowSky"));
    }
#endif
    SkydomeMaterial = UMaterialInstanceDynamic::Create(BaseMat ? BaseMat : UMaterial::GetDefaultMaterial(MD_Surface), this, TEXT("SkydomeMID"));

	SkydomeMesh = NewObject<UStaticMeshComponent>(this, TEXT("SkydomeComp"));
	SkydomeMesh->SetStaticMesh(Mesh);
	if (SkydomeMaterial)
	{
		SkydomeMesh->SetMaterial(0, SkydomeMaterial);
	}
	SkydomeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkydomeMesh->SetCastShadow(false);
	SkydomeMesh->bAffectDistanceFieldLighting = false;
	SkydomeMesh->RegisterComponent();
	SkydomeMesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

	UE_LOG(LogWowSky, Log, TEXT("Skydome mesh created: %d rings x %d segments"), NumRings, NumSegments);
}

void AWowSkyManager::CreateAtmosphereSkydomeMesh()
{
	UStaticMesh* EngineSkySphere = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Engine/EngineSky/SM_SkySphere.SM_SkySphere"));

	UMaterialInterface* EngineSkyMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/EngineSky/M_Sky_Panning_Clouds2_Inst.M_Sky_Panning_Clouds2_Inst"));

	if (!EngineSkyMaterial)
	{
		EngineSkyMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/EngineSky/SkyAtmosphere/SkyAtmosphere_MaterialSkyDome.SkyAtmosphere_MaterialSkyDome"));
	}

	if (!EngineSkySphere || !EngineSkyMaterial)
	{
		UE_LOG(LogWowSky, Warning, TEXT("Engine sky sphere/material not found; using legacy procedural sky dome"));
		CreateSkydomeMesh();
		return;
	}

	SkydomeMesh = NewObject<UStaticMeshComponent>(this, TEXT("AtmosphereSkydomeComp"));
	if (!SkydomeMesh)
	{
		return;
	}

	SkydomeMaterial = UMaterialInstanceDynamic::Create(EngineSkyMaterial, this, TEXT("ModernSkyDomeMID"));
	SkydomeMesh->SetMobility(EComponentMobility::Movable);
	SkydomeMesh->SetStaticMesh(EngineSkySphere);
	SkydomeMesh->SetMaterial(0, SkydomeMaterial ? Cast<UMaterialInterface>(SkydomeMaterial.Get()) : EngineSkyMaterial);
	SkydomeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkydomeMesh->SetGenerateOverlapEvents(false);
	SkydomeMesh->SetCastShadow(false);
	SkydomeMesh->bAffectDistanceFieldLighting = false;
	SkydomeMesh->BoundsScale = 1000.0f;
	SkydomeMesh->SetWorldLocation(GetSkyViewLocation(this));
	SkydomeMesh->SetRelativeScale3D(FVector(12000.0f));
	SkydomeMesh->RegisterComponent();
	SkydomeMesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);

	UE_LOG(LogWowSky, Log, TEXT("UE atmosphere sky dome created from EngineSky assets"));
}

void AWowSkyManager::CreateCloudMesh()
{
	const float CloudSize = 460000.0f;
	const float CloudHeight = 76000.0f;
	const int32 GridSize = 48;

	TArray<FVector> Verts;
	TArray<FVector4f> Colors;
	TArray<int32> Indices;

	for (int32 Y = 0; Y <= GridSize; ++Y)
	{
		for (int32 X = 0; X <= GridSize; ++X)
		{
			float FX = (static_cast<float>(X) / GridSize - 0.5f) * 2.0f * CloudSize;
			float FY = (static_cast<float>(Y) / GridSize - 0.5f) * 2.0f * CloudSize;

			const float U = static_cast<float>(X) / GridSize;
			const float V = static_cast<float>(Y) / GridSize;
			const float EdgeU = FMath::Min(U, 1.0f - U);
			const float EdgeV = FMath::Min(V, 1.0f - V);
			const float EdgeFade = FMath::Clamp(FMath::Min(EdgeU, EdgeV) * 8.0f, 0.0f, 1.0f);
			const float CloudA = FMath::PerlinNoise2D(FVector2D(U * 3.2f + 13.7f, V * 3.2f - 8.1f));
			const float CloudB = FMath::PerlinNoise2D(FVector2D(U * 7.5f - 2.6f, V * 7.5f + 4.9f));
			const float CloudC = FMath::PerlinNoise2D(FVector2D(U * 14.0f + 1.6f, V * 14.0f + 21.3f));
			const float RawCloud = CloudA * 0.58f + CloudB * 0.32f + CloudC * 0.10f + 0.35f;
			const float CloudMask = SmoothStep01((RawCloud - 0.10f) / 0.45f) * EdgeFade;
			const float BillowHeight = CloudMask * 9500.0f + CloudC * 2400.0f;
			Verts.Add(FVector(FX, FY, CloudHeight + BillowHeight));
			Colors.Add(FVector4f(CloudMask, CloudMask, CloudMask, 1.0f));
		}
	}

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			int32 I00 = Y * (GridSize + 1) + X;
			int32 I10 = I00 + 1;
			int32 I01 = I00 + (GridSize + 1);
			int32 I11 = I01 + 1;

			// Inverted winding (viewed from below)
			Indices.Add(I00);
			Indices.Add(I01);
			Indices.Add(I10);

			Indices.Add(I10);
			Indices.Add(I01);
			Indices.Add(I11);
		}
	}

	UStaticMesh* Mesh = BuildDomeMesh(this, Verts, Indices, Colors);

	// Cloud material: translucent, unlit, soft haze.
	// Runtime-generated Noise expressions are brittle across UE versions and
	// failed to compile in UE 5.7, so keep this material deliberately simple.
    UMaterial* BaseMat = WowCookedMaterial::Load(TEXT("M_WowCloud"));
#if WITH_EDITOR
    if (!BaseMat)
    {
        BaseMat = NewObject<UMaterial>(this);

	BaseMat->SetShadingModel(MSM_Unlit);
	BaseMat->TwoSided = true;
	BaseMat->BlendMode = BLEND_Translucent;

	auto* PCloudColor = NewObject<UMaterialExpressionVectorParameter>(BaseMat);
	PCloudColor->ParameterName = TEXT("CloudColor");
	PCloudColor->DefaultValue = FLinearColor(0.9f, 0.92f, 0.95f);
	BaseMat->GetExpressionCollection().AddExpression(PCloudColor);

	auto* PCloudOpacity = NewObject<UMaterialExpressionScalarParameter>(BaseMat);
	PCloudOpacity->ParameterName = TEXT("CloudOpacity");
	PCloudOpacity->DefaultValue = 0.46f;
	BaseMat->GetExpressionCollection().AddExpression(PCloudOpacity);

	auto* VertexCloudColor = NewObject<UMaterialExpressionVertexColor>(BaseMat);
	VertexCloudColor->MaterialExpressionEditorX = -760;
	VertexCloudColor->MaterialExpressionEditorY = 160;
	BaseMat->GetExpressionCollection().AddExpression(VertexCloudColor);

	auto* VertexCloudMask = NewObject<UMaterialExpressionComponentMask>(BaseMat);
	VertexCloudMask->MaterialExpressionEditorX = -580;
	VertexCloudMask->MaterialExpressionEditorY = 160;
	VertexCloudMask->Input.Connect(0, VertexCloudColor);
	VertexCloudMask->R = true;
	BaseMat->GetExpressionCollection().AddExpression(VertexCloudMask);

	auto* CloudOpacityMasked = NewObject<UMaterialExpressionMultiply>(BaseMat);
	CloudOpacityMasked->MaterialExpressionEditorX = -360;
	CloudOpacityMasked->MaterialExpressionEditorY = 160;
	CloudOpacityMasked->A.Connect(0, PCloudOpacity);
	CloudOpacityMasked->B.Connect(0, VertexCloudMask);
	BaseMat->GetExpressionCollection().AddExpression(CloudOpacityMasked);

	BaseMat->GetEditorOnlyData()->EmissiveColor.Connect(0, PCloudColor);
	BaseMat->GetEditorOnlyData()->Opacity.Connect(0, CloudOpacityMasked);
	BaseMat->PreEditChange(nullptr);
	BaseMat->PostEditChange();

        BaseMat = WowCookedMaterial::Save(BaseMat, TEXT("M_WowCloud"));
    }
#endif
    CloudMaterial = UMaterialInstanceDynamic::Create(BaseMat ? BaseMat : UMaterial::GetDefaultMaterial(MD_Surface), this, TEXT("CloudMID"));

	CloudMesh = NewObject<UStaticMeshComponent>(this, TEXT("CloudComp"));
	CloudMesh->SetMobility(EComponentMobility::Movable);
	CloudMesh->SetStaticMesh(Mesh);
	CloudMesh->SetMaterial(0, CloudMaterial);
	CloudMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CloudMesh->SetCastShadow(false);
	CloudMesh->bAffectDistanceFieldLighting = false;
	CloudMesh->RegisterComponent();
	CloudMesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

	UE_LOG(LogWowSky, Log, TEXT("Cloud mesh created"));
}

// r514 (Stefan, summer-sky photos: "we want more real than the 3.3.5 screenshot", "do clouds first"):
// our own volumetric cumulus material instead of UE's m_SimpleVolumetricCloud (built for smooth, flat
// layers). Per ray-march sample: a two-octave coverage map makes separate cloud cells; each cell is a
// heap with a flat base and a rounded top that rises with the cell's strength and "Towering" (fair
// weather: low heaps, storms: towers); a 3-octave Perlin-Worley fbm of the engine's noise volume carves
// the billows; high-frequency erosion makes wispy, torn bottoms and crisp cauliflower tops. Lighting is
// UE's: two-lobe phase (silver lining toward the sun), 2 multi-scattering octaves (bright, milky interiors),
// ray-marched self-shadowing (dark bases). AO darkens the lower third. Cooked as M_WowCumulusCloud.
// r525 (Stefan: overhead the clouds were flat grey haze, the sun dull, "the skybox too harsh" through a
// doorway where the fog was lifted): WoW's distance fog (Light.dbc fog end, ~200-600 m here) was the
// height fog, and UE applies the height fog to volumetric clouds at their 1.5-8 km depth, so every
// cloud was ~99 % fog colour. WoW never fogs its sky. The distance fog is now a post-process pass on
// opaque pixels only; sky pixels (dome, clouds, sun, stars - beyond 30 km of depth) keep their colour.
// The height fog keeps weather fog and haze. Same exp2 law as UE's fog with zero falloff.
static const TCHAR* WowDistanceFogMaterialName = TEXT("M_WowDistanceFog");
static UMaterial* WowLoadOrBuildDistanceFogMaterial()
{
    UMaterial* Material = WowCookedMaterial::Load(WowDistanceFogMaterialName);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
        Material->MaterialDomain = MD_PostProcess;
        Material->BlendableLocation = BL_SceneColorBeforeDOF;
        Material->BlendablePriority = -1; // before the indoor fog mask
        auto& Expressions = Material->GetExpressionCollection();
        auto* Scene = NewObject<UMaterialExpressionSceneTexture>(Material);
        Scene->SceneTextureId = PPI_PostProcessInput0;
        Expressions.AddExpression(Scene);
        auto* Position = NewObject<UMaterialExpressionWorldPosition>(Material);
        Position->WorldPositionShaderOffset = WPT_CameraRelative;
        Expressions.AddExpression(Position);
        auto* Shader = NewObject<UMaterialExpressionCustom>(Material);
        Shader->OutputType = CMOT_Float3;
        Shader->Description = TEXT("WowDistanceFog");
        Shader->Code = TEXT(R"HLSL(
float3 C = WowScene.rgb;
float Dist = length(WowRel);
if (WowP1.w < 0.5 || Dist > WowP0.z) return C;
float T = 1.0 - exp2(-WowP0.x * max(Dist - WowP0.y, 0.0));
T = min(T, WowP0.w);
float3 Fog = (WowP1.rgb + WowP2.rgb * View.SkyAtmosphereHeightFogContribution * View.DistantSkyLightLutBufferSRV[0].rgb) * View.PreExposure;
return lerp(C, Fog, T);
)HLSL");
        Shader->Inputs.Reset();
        auto AddInput = [Shader](const TCHAR* Name, UMaterialExpression* Source, int32 Output)
        {
            FCustomInput Input;
            Input.InputName = Name;
            Input.Input.Connect(Output, Source);
            Shader->Inputs.Add(Input);
        };
        AddInput(TEXT("WowScene"), Scene, 0);
        AddInput(TEXT("WowRel"), Position, 0);
        for (const TCHAR* Name : { TEXT("WowP0"), TEXT("WowP1"), TEXT("WowP2") })
        {
            auto* Parameter = NewObject<UMaterialExpressionVectorParameter>(Material);
            Parameter->ParameterName = Name;
            Parameter->DefaultValue = FLinearColor(0, 0, 0, 0);
            Expressions.AddExpression(Parameter);
            AddInput(Name, Parameter, 5); // RGBA
        }
        Expressions.AddExpression(Shader);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Shader);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, WowDistanceFogMaterialName);
        UE_LOG(LogWowSky, Log, TEXT("Distance fog material built"));
    }
#endif
    return Material;
}

static UMaterial* WowLoadOrBuildCumulusCloudMaterial()
{
    UMaterial* Material = WowCookedMaterial::Load(TEXT("M_WowCumulusCloud"));
#if WITH_EDITOR
    if (!Material)
    {
        UTexture* Noise = LoadObject<UVolumeTexture>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/VT_PerlinWorley_Balanced.VT_PerlinWorley_Balanced"));
        if (!Noise) Noise = LoadObject<UVolumeTexture>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/T_VolumeNoiseShape128.T_VolumeNoiseShape128"));
        if (!Noise)
        {
            UE_LOG(LogWowSky, Warning, TEXT("Cumulus clouds: no engine noise volume texture found; keeping the engine cloud material"));
            return nullptr;
        }
        Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
        Material->MaterialDomain = MD_Volume;
        Material->BlendMode = BLEND_Additive;
        Material->SetShadingModel(MSM_DefaultLit);
        bool bNeedsRecompile = false;
        Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_VolumetricCloud);
        auto& Expressions = Material->GetExpressionCollection();
        auto Scalar = [&](const TCHAR* Name, float Default)
        {
            auto* P = NewObject<UMaterialExpressionScalarParameter>(Material);
            P->ParameterName = Name; P->DefaultValue = Default;
            Expressions.AddExpression(P);
            return P;
        };
        auto Vector = [&](const TCHAR* Name, const FLinearColor& Default)
        {
            auto* P = NewObject<UMaterialExpressionVectorParameter>(Material);
            P->ParameterName = Name; P->DefaultValue = Default;
            Expressions.AddExpression(P);
            return P;
        };
        auto* Tex = NewObject<UMaterialExpressionTextureObject>(Material);
        Tex->Texture = Noise;
        Tex->SamplerType = MaterialExpressionUtils::GetSamplerTypeForTexture(Noise);
        Expressions.AddExpression(Tex);
        auto* Position = NewObject<UMaterialExpressionWorldPosition>(Material);
        Expressions.AddExpression(Position);
        auto* PositionF = NewObject<UMaterialExpressionTruncateLWC>(Material);
        PositionF->Input.Connect(0, Position);
        Expressions.AddExpression(PositionF);
        auto* CloudSample = NewObject<UMaterialExpressionCloudSampleAttribute>(Material);
        Expressions.AddExpression(CloudSample);
        auto* Time = NewObject<UMaterialExpressionTime>(Material);
        Expressions.AddExpression(Time);
        auto* Coverage = Scalar(TEXT("Coverage"), 0.4f);
        auto* Density = Scalar(TEXT("Density"), 0.06f);
        auto* Scale = Scalar(TEXT("CloudScale"), 1.6f);
        auto* Towering = Scalar(TEXT("Towering"), 0.2f);
        auto* Wind = Vector(TEXT("WindOffset"), FLinearColor(0, 0, 0, 0));
        auto* Albedo = Vector(TEXT("Albedo"), FLinearColor(0.93f, 0.93f, 0.93f, 1.0f));
        auto* Lightning = Vector(TEXT("LightningColor"), FLinearColor(0, 0, 0, 1));

        auto* Shader = NewObject<UMaterialExpressionCustom>(Material);
        Shader->OutputType = CMOT_Float4;
        Shader->Description = TEXT("WowCumulusDensity");
        Shader->Code = TEXT(R"HLSL(
float h = saturate(H);
float s = max(Scale, 0.2);
float3 p = P * 0.00001;               // cm -> km
p.xy += Wind.xy;
// Coverage map: where the cloud cells are (two octaves of the noise volume as a 2D field).
float w = Texture3DSampleLevel(Tex, TexSampler, float3(p.xy / (s * 9.0), 0.21), 0).r * 0.6
        + Texture3DSampleLevel(Tex, TexSampler, float3(p.xy / (s * 3.5) + 0.37, 0.63), 0).r * 0.4;
float cov = saturate(Coverage);
float cell = saturate((w - (1.0 - cov)) / max(cov, 0.05) + cov * 0.25);
if (cell <= 0.0) return float4(0.0, 1.0, 0.0, 0.0);
// Heap profile: flat base, rounded top rising with the cell's strength and towering.
float top = lerp(0.22, 1.0, saturate(cell * (0.55 + Towering)));
float profile = smoothstep(0.0, 0.05, h) * (1.0 - smoothstep(top * 0.55, top, h));
float base = saturate(cell * profile - h * 0.2);
if (base <= 0.0) return float4(0.0, 1.0, cell, 0.0);
// Billows: 3-octave fbm, slowly boiling.
float3 q = p / s;
float n1 = Texture3DSampleLevel(Tex, TexSampler, q * 0.55 + float3(0.0, 0.0, T * 0.0015), 0).r;
float n2 = Texture3DSampleLevel(Tex, TexSampler, q * 1.6 + float3(0.31, 0.17, T * 0.003), 0).r;
float n3 = Texture3DSampleLevel(Tex, TexSampler, q * 4.3 + float3(0.73, 0.41, 0.0), 0).r;
float shape = n1 * 0.625 + n2 * 0.25 + n3 * 0.125;
float cloud = saturate((shape - (1.0 - base)) / max(base, 0.02));
if (cloud <= 0.0) return float4(0.0, 1.0, cell, 0.0);
// Erosion: wispy, torn bottoms; crisp cauliflower tops.
float d = Texture3DSampleLevel(Tex, TexSampler, q * 11.0 + float3(0.11, 0.53, T * 0.004), 0).r;
float er = lerp(1.0 - d, d, saturate(h * 3.0)) * 0.35 * (1.0 - cloud * 0.5);
cloud = saturate((cloud - er) / max(1.0 - er, 0.05));
float ao = lerp(0.45, 1.0, saturate(h * 1.6));
return float4(cloud * Density, ao, cell, 0.0);
)HLSL");
        Shader->Inputs.Reset();
        auto AddInput = [Shader](const TCHAR* Name, UMaterialExpression* Source, int32 Output)
        {
            FCustomInput Input;
            Input.InputName = Name;
            Input.Input.Connect(Output, Source);
            Shader->Inputs.Add(Input);
        };
        AddInput(TEXT("P"), PositionF, 0);
        AddInput(TEXT("H"), CloudSample, 2); // NormAltitudeInLayer
        AddInput(TEXT("T"), Time, 0);
        AddInput(TEXT("Tex"), Tex, 0);
        AddInput(TEXT("Coverage"), Coverage, 0);
        AddInput(TEXT("Density"), Density, 0);
        AddInput(TEXT("Scale"), Scale, 0);
        AddInput(TEXT("Towering"), Towering, 0);
        AddInput(TEXT("Wind"), Wind, 0);
        Expressions.AddExpression(Shader);

        auto* Extinction = NewObject<UMaterialExpressionComponentMask>(Material);
        Extinction->Input.Connect(0, Shader); Extinction->R = true;
        Expressions.AddExpression(Extinction);
        auto* AO = NewObject<UMaterialExpressionComponentMask>(Material);
        AO->Input.Connect(0, Shader); AO->G = true;
        Expressions.AddExpression(AO);
        auto* One = NewObject<UMaterialExpressionConstant3Vector>(Material);
        One->Constant = FLinearColor::White;
        Expressions.AddExpression(One);
        auto* Extinction3 = NewObject<UMaterialExpressionMultiply>(Material);
        Extinction3->A.Connect(0, Extinction); Extinction3->B.Connect(0, One);
        Expressions.AddExpression(Extinction3);
        auto* Flash = NewObject<UMaterialExpressionMultiply>(Material);
        Flash->A.Connect(0, Lightning); Flash->B.Connect(0, Extinction);
        Expressions.AddExpression(Flash);

        auto* Advanced = NewObject<UMaterialExpressionVolumetricAdvancedMaterialOutput>(Material);
        Advanced->ConstPhaseG = 0.6f;          // strong forward lobe: silver linings toward the sun
        Advanced->ConstPhaseG2 = -0.3f;        // soft back lobe
        Advanced->ConstPhaseBlend = 0.35f;
        Advanced->MultiScatteringApproximationOctaveCount = 2;
        Advanced->ConstMultiScatteringContribution = 0.6f;
        Advanced->ConstMultiScatteringOcclusion = 0.4f;
        Advanced->ConstMultiScatteringEccentricity = 0.55f;
        Advanced->bRayMarchVolumeShadow = true;
        Expressions.AddExpression(Advanced);

        auto* EditorData = Material->GetEditorOnlyData();
        EditorData->BaseColor.Connect(0, Albedo);
        EditorData->SubsurfaceColor.Connect(0, Extinction3); // Volume domain: extinction (1/m)
        EditorData->EmissiveColor.Connect(0, Flash);
        EditorData->AmbientOcclusion.Connect(0, AO);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, TEXT("M_WowCumulusCloud"));
        UE_LOG(LogWowSky, Log, TEXT("Cumulus cloud material built (noise %s)"), *GetNameSafe(Noise));
    }
#endif
    return Material;
}

void AWowSkyManager::CreateVolumetricCloudLayer()
{
	VolumetricClouds = NewObject<UVolumetricCloudComponent>(this, TEXT("VolumetricClouds"));
	if (!VolumetricClouds)
	{
		return;
	}

	VolumetricClouds->SetMobility(EComponentMobility::Movable);
	VolumetricClouds->RegisterComponent();
	VolumetricClouds->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	// r513 (Stefan: "this is the sun and clouds we want" - fair-weather cumulus): a lower, thinner
	// layer so the clouds read as separate puffy heaps with flat grey bases (was 3.6 km + 5 km).
	VolumetricClouds->SetLayerBottomAltitude(2.2f);
	VolumetricClouds->SetLayerHeight(4.0f);
	VolumetricClouds->SetTracingStartMaxDistance(100.0f);
	VolumetricClouds->SetTracingMaxDistance(180.0f);
	// Cloud render target mode is chosen per quality level in UpdateClouds (r282). This used to
    // force mode 1 (half-resolution tracing, bilinear upsample) after BeginPlay had set mode 2,
    // and with an upscaler's lower internal resolution the clouds came out as visible blocks.
    if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricRenderTarget.ReprojectionBoxConstraint"))) C->Set(1,ECVF_SetByCode);
    VolumetricClouds->SetViewSampleCountScale(1.5f);
	VolumetricClouds->SetReflectionViewSampleCountScale(0.20f);
	VolumetricClouds->SetShadowViewSampleCountScale(0.25f);
	VolumetricClouds->SetShadowReflectionViewSampleCountScale(0.12f);
	VolumetricClouds->SetShadowTracingDistance(15.0f);
	VolumetricClouds->SetSkyLightCloudBottomOcclusion(0.16f);
	VolumetricClouds->SetGroundAlbedo(FColor(96, 110, 82));
	VolumetricClouds->SetbUsePerSampleAtmosphericLightTransmittance(true);
	VolumetricClouds->SetRenderInMainPass(true);
	VolumetricClouds->SetVisibleInRealTimeSkyCaptures(true);

	UMaterialInterface* EngineCloudMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst"));
	if (!EngineCloudMaterial)
	{
		UE_LOG(LogWowSky, Warning, TEXT("Engine volumetric cloud material was not found; using procedural cloud mesh fallback"));
		return;
	}

	VolumetricCloudMaterial = UMaterialInstanceDynamic::Create(EngineCloudMaterial, this, TEXT("VolumetricCloudMID"));
	if (UMaterial* Cumulus = WowLoadOrBuildCumulusCloudMaterial())
		CumulusCloudMaterial = UMaterialInstanceDynamic::Create(Cumulus, this, TEXT("CumulusCloudMID"));
	else
		UE_LOG(LogWowSky, Warning, TEXT("Cumulus clouds: M_WowCumulusCloud is not cooked; using the engine cloud material"));
	VolumetricClouds->SetMaterial(VolumetricCloudMaterial ? Cast<UMaterialInterface>(VolumetricCloudMaterial.Get()) : EngineCloudMaterial);
	UE_LOG(LogWowSky, Log, TEXT("Native UE volumetric cloud layer created"));
}

void AWowSkyManager::CreateCelestialDiscs()
{
	const float DiscDistance = 520000.0f; // Inside the visible sky dome.
	const float SunSize = 30000.0f;
	const float SunHaloSize = 220000.0f;
	const float MoonSize = 36000.0f;

	// Helper: create a circular disc mesh so sun/moon billboards never render
	// as square quads if a radial material mask is unavailable.
	auto MakeDiscMesh = [this](float Size, float InnerAlpha, float EdgeAlpha, const FName& Name) -> UStaticMesh*
	{
		TArray<FVector> Verts;
		TArray<FVector4f> Colors;
		TArray<int32> Indices;

		const int32 SegmentCount = 48;
		const float Radius = Size * 0.5f;
		Verts.Add(FVector::ZeroVector);
		Colors.Add(FVector4f(InnerAlpha, 1.0f, 1.0f, 1.0f));

		for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
		{
			const float Angle = static_cast<float>(Segment) / SegmentCount * 2.0f * PI;
			Verts.Add(FVector(0.0f, FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius));
			Colors.Add(FVector4f(EdgeAlpha, 1.0f, 1.0f, 1.0f));
		}

		for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
		{
			const int32 Current = Segment + 1;
			const int32 Next = (Segment + 1) % SegmentCount + 1;
			Indices.Add(0);
			Indices.Add(Next);
			Indices.Add(Current);

			Indices.Add(0);
			Indices.Add(Current);
			Indices.Add(Next);
		}

		return BuildDomeMesh(this, Verts, Indices, Colors);
	};

	// Create emissive material for sun disc
	auto MakeDiscMaterial = [this](const FName& MatName, const FLinearColor& DefaultColor, float DefaultOpacity) -> UMaterialInstanceDynamic*
	{
        const FString CookedName = TEXT("M_Wow") + MatName.ToString();
        UMaterial* BaseMat = WowCookedMaterial::Load(CookedName);
#if WITH_EDITOR
        if (!BaseMat)
        {
            BaseMat = NewObject<UMaterial>(this);

		BaseMat->SetShadingModel(MSM_Unlit);
		BaseMat->TwoSided = true;
		BaseMat->BlendMode = BLEND_Translucent;

		auto* PColor = NewObject<UMaterialExpressionVectorParameter>(BaseMat);
		PColor->ParameterName = TEXT("DiscColor");
		PColor->DefaultValue = DefaultColor;
		BaseMat->GetExpressionCollection().AddExpression(PColor);

		auto* POpacity = NewObject<UMaterialExpressionScalarParameter>(BaseMat);
		POpacity->ParameterName = TEXT("DiscOpacity");
		POpacity->DefaultValue = DefaultOpacity;
		BaseMat->GetExpressionCollection().AddExpression(POpacity);

		auto* VertexColor = NewObject<UMaterialExpressionVertexColor>(BaseMat);
		BaseMat->GetExpressionCollection().AddExpression(VertexColor);

		auto* VertexAlpha = NewObject<UMaterialExpressionComponentMask>(BaseMat);
		VertexAlpha->Input.Connect(0, VertexColor);
		VertexAlpha->R = true;
		BaseMat->GetExpressionCollection().AddExpression(VertexAlpha);

		auto* DiscOpacity = NewObject<UMaterialExpressionMultiply>(BaseMat);
		DiscOpacity->A.Connect(0, POpacity);
		DiscOpacity->B.Connect(0, VertexAlpha);
		BaseMat->GetExpressionCollection().AddExpression(DiscOpacity);

		auto* DiscEmissive = NewObject<UMaterialExpressionMultiply>(BaseMat);
		DiscEmissive->A.Connect(0, PColor);
		DiscEmissive->B.Connect(0, DiscOpacity);
		BaseMat->GetExpressionCollection().AddExpression(DiscEmissive);

		BaseMat->GetEditorOnlyData()->EmissiveColor.Connect(0, DiscEmissive);
		BaseMat->GetEditorOnlyData()->Opacity.Connect(0, DiscOpacity);
		BaseMat->PreEditChange(nullptr);
		BaseMat->PostEditChange();

            BaseMat = WowCookedMaterial::Save(BaseMat, CookedName);
        }
#endif
        return UMaterialInstanceDynamic::Create(BaseMat ? BaseMat : UMaterial::GetDefaultMaterial(MD_Surface), this);
	};

	// Disc with radial alpha rings {radius fraction, alpha}: lets the sun have a solid core with a
	// crisp limb and a thin corona instead of a centre-to-edge fade (which read as a soft cloud,
	// especially with the Sun size option enlarged).
	auto MakeRingedDiscMesh = [this](float Size, std::initializer_list<FVector2f> Rings, const FName& Name) -> UStaticMesh*
	{
		TArray<FVector> Verts;
		TArray<FVector4f> Colors;
		TArray<int32> Indices;
		const int32 SegmentCount = 96;
		const float Radius = Size * 0.5f;
		Verts.Add(FVector::ZeroVector);
		Colors.Add(FVector4f(1.0f, 1.0f, 1.0f, 1.0f));
		int32 RingIndex = 0;
		for (const FVector2f& Ring : Rings)
		{
			for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
			{
				const float Angle = static_cast<float>(Segment) / SegmentCount * 2.0f * PI;
				Verts.Add(FVector(0.0f, FMath::Cos(Angle) * Radius * Ring.X, FMath::Sin(Angle) * Radius * Ring.X));
				Colors.Add(FVector4f(Ring.Y, 1.0f, 1.0f, 1.0f));
			}
			for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
			{
				const int32 Next = (Segment + 1) % SegmentCount;
				const int32 Cur = 1 + RingIndex * SegmentCount;
				if (RingIndex == 0)
				{
					Indices.Append({ 0, Cur + Next, Cur + Segment, 0, Cur + Segment, Cur + Next });
				}
				else
				{
					const int32 Prev = 1 + (RingIndex - 1) * SegmentCount;
					const int32 A = Prev + Segment, B = Prev + Next, C = Cur + Segment, D = Cur + Next;
					Indices.Append({ A, B, D, A, D, C,  A, D, B, A, C, D });
				}
			}
			++RingIndex;
		}
		return BuildDomeMesh(this, Verts, Indices, Colors);
	};

	// Sun disc
	UStaticMesh* SunHaloMeshAsset = MakeDiscMesh(SunHaloSize, 0.16f, 0.0f, TEXT("SunHaloQuad"));
	SunHaloMaterial = MakeDiscMaterial(TEXT("SunHaloMat"), FLinearColor(1.0f, 0.66f, 0.24f, 1.0f), 0.07f);

	SunHaloMesh = NewObject<UStaticMeshComponent>(this, TEXT("SunHaloComp"));
	SunHaloMesh->SetStaticMesh(SunHaloMeshAsset);
	SunHaloMesh->SetMaterial(0, SunHaloMaterial);
	SunHaloMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SunHaloMesh->SetCastShadow(false);
	SunHaloMesh->bAffectDistanceFieldLighting = false;
	SunHaloMesh->RegisterComponent();
	SunHaloMesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

	// Solid flat core (no 3D ball shading), a short anti-aliased limb, then a thin corona that
	// fades out; the wide warm glow is the separate halo disc.
	// r282 (Stefan: the enlarged sun looked cartoony): an HDR core that the tonemapper clips to
	// white and bloom spreads into glare, with a steep exponential limb instead of flat rings.
	UStaticMesh* SunMesh = MakeRingedDiscMesh(SunSize, {
		FVector2f(0.42f, 1.0f), FVector2f(0.46f, 0.30f), FVector2f(0.52f, 0.08f), FVector2f(0.62f, 0.025f),
		FVector2f(0.80f, 0.008f), FVector2f(1.0f, 0.0f) }, TEXT("SunDiscQuad"));
	SunDiscMaterial = MakeDiscMaterial(TEXT("SunDiscMat"), FLinearColor(1.0f, 0.82f, 0.38f, 1.0f), 1.0f);

	SunDiscMesh = NewObject<UStaticMeshComponent>(this, TEXT("SunDiscComp"));
	SunDiscMesh->SetStaticMesh(SunMesh);
	SunDiscMesh->SetMaterial(0, SunDiscMaterial);
	SunDiscMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SunDiscMesh->SetCastShadow(false);
	SunDiscMesh->bAffectDistanceFieldLighting = false;
	SunDiscMesh->RegisterComponent();
	SunDiscMesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

	// r513 (Stefan, photos of a summer sun: "this is the sun and clouds we want", "sun with lens flare",
	// "with rays like these"): a star of glare around the sun. 12 sharp diffraction spikes (long and
	// short alternating) plus 30 soft, wide rays of uneven length and width, each a triangle fan blade
	// whose vertex alpha (R) fades from the core to the tip. Same unlit translucent disc material as the
	// halo (no new cooked asset); HDR colour so the core blooms. Faces the camera with no roll, so the
	// spikes stay put on screen like a camera's aperture star.
	{
		const float RaysRadius = 150000.0f;
		TArray<FVector> Verts; TArray<FVector4f> Colors; TArray<int32> Indices;
		auto AddBlade = [&](float Angle, float Length, float HalfWidthRad, float BaseAlpha, float BaseRadius)
		{
			const int32 Base = Verts.Num();
			const FVector2f Dir(FMath::Cos(Angle), FMath::Sin(Angle));
			const FVector2f L(FMath::Cos(Angle - HalfWidthRad), FMath::Sin(Angle - HalfWidthRad));
			const FVector2f R(FMath::Cos(Angle + HalfWidthRad), FMath::Sin(Angle + HalfWidthRad));
			Verts.Add(FVector(0.0f, L.X * BaseRadius, L.Y * BaseRadius)); Colors.Add(FVector4f(BaseAlpha, 1, 1, 1));
			Verts.Add(FVector(0.0f, R.X * BaseRadius, R.Y * BaseRadius)); Colors.Add(FVector4f(BaseAlpha, 1, 1, 1));
			Verts.Add(FVector(0.0f, Dir.X * Length, Dir.Y * Length)); Colors.Add(FVector4f(0.0f, 1, 1, 1));
			Verts.Add(FVector(0.0f, 0.0f, 0.0f)); Colors.Add(FVector4f(BaseAlpha, 1, 1, 1));
			Indices.Append({ Base + 3, Base + 0, Base + 2, Base + 3, Base + 2, Base + 1,
				Base + 3, Base + 2, Base + 0, Base + 3, Base + 1, Base + 2 });
		};
		FRandomStream Rays(513);
		for (int32 I = 0; I < 12; ++I)
		{
			const float Angle = I * 2.0f * PI / 12.0f + 0.13f;
			AddBlade(Angle, RaysRadius * ((I % 2) ? 0.55f : 1.0f), FMath::DegreesToRadians(0.55f), 0.9f, 4500.0f);
		}
		for (int32 I = 0; I < 30; ++I)
		{
			const float Angle = (I + Rays.FRandRange(-0.35f, 0.35f)) * 2.0f * PI / 30.0f;
			AddBlade(Angle, RaysRadius * Rays.FRandRange(0.35f, 0.95f), FMath::DegreesToRadians(Rays.FRandRange(1.2f, 3.2f)),
				Rays.FRandRange(0.10f, 0.22f), 9000.0f);
		}
		SunRaysMesh = NewObject<UStaticMeshComponent>(this, TEXT("SunRaysComp"));
		SunRaysMesh->SetStaticMesh(BuildDomeMesh(this, Verts, Indices, Colors));
		SunRaysMaterial = MakeDiscMaterial(TEXT("SunHaloMat"), FLinearColor(1.0f, 0.95f, 0.85f, 1.0f), 1.0f);
		SunRaysMesh->SetMaterial(0, SunRaysMaterial);
		SunRaysMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SunRaysMesh->SetCastShadow(false);
		SunRaysMesh->bAffectDistanceFieldLighting = false;
		SunRaysMesh->RegisterComponent();
		SunRaysMesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	}

	// Moon disc
	UStaticMesh* MoonMeshAsset = MakeDiscMesh(MoonSize, 1.0f, 0.42f, TEXT("MoonDiscQuad"));
	MoonDiscMaterial = MakeDiscMaterial(TEXT("MoonDiscMat"), FLinearColor(0.6f, 0.65f, 0.9f), 0.68f); // Cool blue-white

	MoonDiscMesh = NewObject<UStaticMeshComponent>(this, TEXT("MoonDiscComp"));
	MoonDiscMesh->SetStaticMesh(MoonMeshAsset);
	MoonDiscMesh->SetMaterial(0, MoonDiscMaterial);
	MoonDiscMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoonDiscMesh->SetCastShadow(false);
	MoonDiscMesh->bAffectDistanceFieldLighting = false;
	MoonDiscMesh->RegisterComponent();
	MoonDiscMesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

	UE_LOG(LogWowSky, Log, TEXT("Celestial discs created (sun=%.0f, halo=%.0f, moon=%.0f)"), SunSize, SunHaloSize, MoonSize);
}

void AWowSkyManager::UpdateCelestialDiscs()
{
	const float DiscDistance = 520000.0f;

	const FVector CameraPos = GetSkyViewLocation(this);

	float SunPitch = 0.0f;
	float SunYaw = 0.0f;
	bool bSunAboveHorizon = false;
	CalculateWowSunAngles(TimeOfDay, SunPitch, SunYaw, bSunAboveHorizon);

	// r391 (Stefan: "the sun is out, but it is night" in a Dun Morogh snowfall - it was the moon,
	// a hard white dot through the snow): precipitation covers the sky, so the discs fade with
	// the weather like behind an overcast sky.
	const float WeatherGrade = CurrentWeatherType == 0 ? 0.f : FMath::Clamp(CurrentWeatherGrade, 0.f, 1.f);
	// r540 (Stefan: "a sun and the moon at the same time again"): the sky goes dark from 17:30 (the
	// night curve the stars and the moon use), but the sun only sets at 18:30. The sun disc, halo,
	// rays and the atmosphere disc now leave as the night comes (gone at half night); the moon only
	// comes after that, so the two are never in the sky together.
	const float SunDuskFade = WowSkySunDuskFade(TimeOfDay);
	const float DiscVisibility = (1.f - 0.95f * SmoothStep01(WeatherGrade * 4.f)) * SunDuskFade;

	// The atmosphere's own sun disc can't be enlarged (its size is the light's source angle, which
	// would also blur every shadow): a scaled sun uses the billboard disc + halo instead.
	const bool bUseNativeSunDisc = SkyAtmosphere && SkyAtmosphere->IsVisible() && !SkydomeMesh
		&& FWowClientPreferences::GetSunScale() <= 1.01f;
	// r718 (Stefan at 14:00 by the Options time: "Skies seems really dark now ... The sun at the lowest setting is
	// gone?" - a black sky with only the glare star): say what the sky is doing whenever the time jumps or every
	// 30 s, so the next log shows which part goes dark.
	{
		static double GNextSkyLog = 0.0;
		static float GLastSkyLogTime = -1000.f;
		const double NowSeconds = FPlatformTime::Seconds();
		if (NowSeconds >= GNextSkyLog || FMath::Abs(TimeOfDay - GLastSkyLogTime) > 20.f)
		{
			GNextSkyLog = NowSeconds + 30.0;
			GLastSkyLogTime = TimeOfDay;
			UE_LOG(LogWowSky, Log, TEXT("Sky state: time=%.0f sunPitch=%.1f sunUp=%d nativeDisc=%d sunScale=%.2f discVis=%.2f weather=%d/%.2f cloud=%.2f atmosphere=%d skydome=%d sunIntensity=%.2f sunVisible=%d"),
				TimeOfDay, SunPitch, bSunAboveHorizon ? 1 : 0, bUseNativeSunDisc ? 1 : 0, FWowClientPreferences::GetSunScale(), DiscVisibility,
				int32(CurrentWeatherType), WeatherGrade, CurrentWeatherCloud, SkyAtmosphere && SkyAtmosphere->IsVisible() ? 1 : 0, SkydomeMesh ? 1 : 0,
				SunLight ? SunLight->Intensity : -1.f, SunLight && SunLight->IsVisible() ? 1 : 0);
		}
	}

	if (SunDiscMesh)
	{
		// Position sun disc along the sun's direction vector from camera
		FRotator SunRot(SunPitch, SunYaw, 0.0f);
		FVector SunDir = SunRot.Vector();
		FVector SunPos = CameraPos - SunDir * DiscDistance; // Light points toward origin

		SunDiscMesh->SetWorldLocation(SunPos);
		// Billboard: face the camera
		FRotator FaceCamera = (CameraPos - SunPos).Rotation();
		SunDiscMesh->SetWorldRotation(FaceCamera);
		SunDiscMesh->SetWorldScale3D(FVector(FWowClientPreferences::GetSunScale()));
		SunDiscMesh->SetVisibility(bSunAboveHorizon && !bUseNativeSunDisc && DiscVisibility > 0.03f);

		// Tint sun disc color based on time (warm at sunrise/sunset)
		if (SunDiscMaterial)
		{
			FLinearColor SunColor;
			if (bHasDbcLights)
			{
				SunColor = FMath::Lerp(BlendZoneColor(LP_SunColor, CameraPos), FLinearColor(1.0f, 0.82f, 0.38f, 1.0f), 0.90f);
			}
			else
			{
				SunColor = FMath::Lerp(GetSunColorFallback(), FLinearColor(1.0f, 0.82f, 0.38f, 1.0f), 0.78f);
			}
			// Standing in for the HDR atmosphere disc: brighter so bloom still reads as the sun.
			if (SkyAtmosphere && SkyAtmosphere->IsVisible() && !SkydomeMesh) SunColor *= 3.0f;
			// The core must be far brighter than the sky so it clips to white and blooms like a
			// real sun; at 3x it tonemapped to a pale cream plate (r282).
			SunColor *= 30.0f; // r289: bright enough to drive bloom and lens flare
			// r707 (Stefan, Sun size above 100 %: "Bigger suns also don't look as impressive as the smaller one"):
			// the enlarged billboard sun replaced the atmosphere's HDR disc at the same brightness, so it read as a
			// pale plate. A bigger sun burns hotter (up to 2x at 10x size), so bloom and flare grow with it.
			// r712 (Stefan, 145 % next to 100 %: "145 % sun size is less bright than 100% HOW?"): at 100 % the sky
			// atmosphere draws its own HDR sun; any bigger size swaps in this billboard, which stayed far dimmer than
			// that disc, and r707's wider, stronger grey halo lifted the sky round it so the core and the rays lost
			// their contrast. The billboard now burns 3x hotter from the first step above 100 % (4x at 10x).
			if (FWowClientPreferences::GetSunScale() > 1.01f)
				SunColor *= FMath::Lerp(3.0f, 4.0f, FMath::Clamp((FWowClientPreferences::GetSunScale() - 1.0f) / 9.0f, 0.0f, 1.0f));
			SunDiscMaterial->SetVectorParameterValue(TEXT("DiscColor"), SunColor);
			SunDiscMaterial->SetScalarParameterValue(TEXT("DiscOpacity"), DiscVisibility);
		}
	}

	if (SunHaloMesh)
	{
		FRotator SunRot(SunPitch, SunYaw, 0.0f);
		const FVector SunDir = SunRot.Vector();
		const FVector SunPos = CameraPos - SunDir * (DiscDistance - 1500.0f);
		SunHaloMesh->SetWorldLocation(SunPos);
		SunHaloMesh->SetWorldRotation((CameraPos - SunPos).Rotation());
		// r707 (Stefan: "The smaller one is better smoothed out around the sun"): the atmosphere's own sun sits in
		// a wide, soft glow; round an enlarged billboard sun the halo now spreads out further (up to 1.6x its size).
		// r712: no extra spread - the wide grey veil made the bigger sun look dull (see the disc colour above).
		SunHaloMesh->SetWorldScale3D(FVector(FWowClientPreferences::GetSunScale()));
		SunHaloMesh->SetVisibility(bSunAboveHorizon && !bUseNativeSunDisc && DiscVisibility > 0.03f);

		if (SunHaloMaterial)
		{
			const FLinearColor HaloColor = FMath::Lerp(GetSunColorFallback(), FLinearColor(1.0f, 0.62f, 0.22f, 1.0f), 0.78f);
			SunHaloMaterial->SetVectorParameterValue(TEXT("DiscColor"), HaloColor);
			// r707: the glow round a bigger sun is stronger too (sqrt of the size: 3x glow at 10x).
			// r712: back to the plain 5 % glow; the brightness comes from the hotter core now.
			SunHaloMaterial->SetScalarParameterValue(TEXT("DiscOpacity"), 0.05f * DiscVisibility);
		}
	}

	// r513: sun glare rays, light shafts and lens flare (option "Sun rays and lens flare").
	{
		const bool bRaysOption = FWowClientPreferences::GetWorldOption(TEXT("sunrays"));
		// Clouds and weather veil the glare; low sun (dawn, dusk) keeps a shorter, warmer star.
		const float CloudVeil = 1.0f - 0.75f * FMath::Clamp((CurrentWeatherCloud - 0.15f) / 0.6f, 0.0f, 1.0f);
		const float Elevation = FMath::Clamp(-SunPitch / 20.0f, 0.0f, 1.0f);
		const float Glare = bRaysOption && bSunAboveHorizon ? DiscVisibility * CloudVeil * FMath::Lerp(0.35f, 1.0f, Elevation) : 0.0f;
		if (SunRaysMesh)
		{
			const FVector SunDir = FRotator(SunPitch, SunYaw, 0.0f).Vector();
			const FVector SunPos = CameraPos - SunDir * (DiscDistance - 800.0f);
			SunRaysMesh->SetWorldLocation(SunPos);
			SunRaysMesh->SetWorldRotation((CameraPos - SunPos).Rotation());
			// r707: the glare star reaches out further round a bigger sun (sqrt of the Sun size option).
			SunRaysMesh->SetWorldScale3D(FVector(FMath::Lerp(0.7f, 1.0f, Elevation) * FMath::Sqrt(FWowClientPreferences::GetSunScale())));
			SunRaysMesh->SetVisibility(Glare > 0.02f);
			if (SunRaysMaterial)
			{
				const FLinearColor RayColor = FMath::Lerp(FLinearColor(1.0f, 0.96f, 0.88f), GetSunColorFallback(), 0.25f) * 6.0f;
				SunRaysMaterial->SetVectorParameterValue(TEXT("DiscColor"), RayColor);
				SunRaysMaterial->SetScalarParameterValue(TEXT("DiscOpacity"), Glare);
			}
		}
		if (SunLight && bSunRaysApplied != bRaysOption)
		{
			// Light shaft bloom: radial streaks from the bright sky around the sun, cut by trees,
			// mountains and buildings in front of it (the beams in Stefan's third photo).
			SunLight->bEnableLightShaftBloom = bRaysOption;
			SunLight->BloomScale = 0.35f;
			SunLight->BloomThreshold = 2.0f;
			SunLight->BloomMaxBrightness = 12.0f;
			SunLight->BloomTint = FColor(255, 246, 228);
			SunLight->MarkRenderStateDirty();
		}
		if (!SunFlarePass)
		{
			SunFlarePass = NewObject<UPostProcessComponent>(this, TEXT("WowSunFlare"));
			SunFlarePass->bUnbound = true;
			SunFlarePass->Priority = -10.0f;
			SunFlarePass->RegisterComponent();
		}
		if (SunFlarePass && bSunRaysApplied != bRaysOption)
		{
			// UE's image-based lens flare: ghosts and a bokeh shape from the brightest pixels (the
			// HDR sun core); a high threshold keeps torches and spell effects out of it.
			FPostProcessSettings& PP = SunFlarePass->Settings;
			PP.bOverride_LensFlareIntensity = true;
			// r738 (Stefan 23.10: "the lens flare is still a little big and distracting"): weaker (0.45 -> 0.28) and smaller
			// ghosts (bokeh 2.5 -> 1.4 % of the view width) below.
			PP.LensFlareIntensity = bRaysOption ? 0.28f : 0.0f;
			PP.bOverride_LensFlareThreshold = true;
			// r728 (Stefan, a torch at night: "The torch lens flare doesn't have to be this strong either" - big
			// orange ghosts across the screen): torch and fire flames passed the threshold of 20. Only the sun's
			// core (90-360 at the disc) is bright enough to make ghosts now.
			PP.LensFlareThreshold = 80.0f;
			PP.bOverride_LensFlareBokehSize = true;
			PP.LensFlareBokehSize = 1.4f; // r738 (was 2.5)
			PP.bOverride_LensFlareTint = true;
			PP.LensFlareTint = FLinearColor(1.0f, 0.88f, 0.75f);
			// r707 (Stefan, the sun through the trees: "Quality on this lens flare, seems bad, all squared"): no
			// bokeh shape was set, and the flare ran at the default quality - its ghosts came out as soft-edged
			// squares and blocks. A round, smooth ghost (a disc that is a little brighter towards its rim, like a
			// real lens ghost) and the highest flare quality.
			static TStrongObjectPtr<UTexture2D> GFlareGhost;
			if (!GFlareGhost.IsValid())
			{
				constexpr int32 Size = 128;
				if (UTexture2D* Ghost = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8))
				{
					Ghost->SRGB = false;
					Ghost->Filter = TF_Bilinear;
					Ghost->AddressX = TA_Clamp;
					Ghost->AddressY = TA_Clamp;
					FTexture2DMipMap& Mip = Ghost->GetPlatformData()->Mips[0];
					FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
					for (int32 Y = 0; Y < Size; ++Y)
						for (int32 X = 0; X < Size; ++X)
						{
							const float DX = (X + 0.5f) / Size * 2.f - 1.f, DY = (Y + 0.5f) / Size * 2.f - 1.f;
							const float R = FMath::Sqrt(DX * DX + DY * DY);
							const float Disc = 1.f - FMath::SmoothStep(0.80f, 0.98f, R);
							const float Value = Disc * (0.55f + 0.45f * FMath::SmoothStep(0.35f, 0.88f, R));
							const uint8 V = static_cast<uint8>(FMath::Clamp(Value, 0.f, 1.f) * 255.f + 0.5f);
							Pixels[Y * Size + X] = FColor(V, V, V, 255);
						}
					Mip.BulkData.Unlock();
					Ghost->UpdateResource();
					GFlareGhost.Reset(Ghost);
				}
			}
			if (GFlareGhost.IsValid())
			{
				PP.bOverride_LensFlareBokehShape = true;
				PP.LensFlareBokehShape = GFlareGhost.Get();
			}
			if (IConsoleVariable* FlareQuality = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LensFlareQuality")))
				if (FlareQuality->GetInt() < 3) FlareQuality->Set(3, ECVF_SetByCode);
		}
		bSunRaysApplied = bRaysOption;
	}

	if (MoonDiscMesh)
	{
		// The moon trails the sun by its phase (elongation): new moon sits with the sun,
		// full moon opposite it (highest at midnight). It follows the same sky path as
		// the sun, delayed by Phase * 24h, so its position always agrees with its phase.
		float MoonPitch = 0.0f, MoonYaw = 0.0f;
		bool bMoonAboveHorizon = false;
		// Default: the moon sits opposite the sun all night (always visible). Experimental
		// "realisticmoon": real rise/set times for the date's phase (a waning crescent
		// only rises after midnight). The phase/shape follows the calendar either way.
		CalculateWowSunAngles(TimeOfDay - (FWowClientPreferences::GetWorldOption(TEXT("realisticmoon"))
			? LunarPhase : 0.5f) * 1440.0f, MoonPitch, MoonYaw, bMoonAboveHorizon);
		const FVector ToMoon = -FRotator(MoonPitch, MoonYaw, 0.0f).Vector();
		const FVector ToSun = -FRotator(SunPitch, SunYaw, 0.0f).Vector();
		const FVector MoonPos = CameraPos + ToMoon * DiscDistance;
		MoonDiscMesh->SetWorldLocation(MoonPos);

		// Disc local +X faces the camera; the lunar shader lights local -Y while waxing
		// and +Y while waning. Roll the disc so that lit side points at the sun.
		const FVector Facing = -ToMoon;
		FVector SunInPlane = ToSun - Facing * FVector::DotProduct(ToSun, Facing);
		if (!SunInPlane.Normalize())
		{
			SunInPlane = FVector::CrossProduct(Facing, FVector::UpVector).GetSafeNormal();
		}
		const FVector LocalY = LunarPhase < 0.5f ? -SunInPlane : SunInPlane;
		const FMatrix MoonFrame = FRotationMatrix::MakeFromXY(Facing, LocalY);
		MoonDiscMesh->SetWorldRotation(MoonFrame.Rotator());
		MoonDiscMesh->SetWorldScale3D(FVector(FWowClientPreferences::GetMoonScale()));
		MoonDiscMesh->SetVisibility(bMoonAboveHorizon && DiscVisibility > 0.03f);
		if (MoonDiscMaterial)
		{
			MoonDiscMaterial->SetScalarParameterValue(TEXT("DiscOpacity"), 0.68f * DiscVisibility); // r391
			// The disc rolls so its lit side faces the sun; counter-rotate the surface
			// (maria, craters, photo) so the moon's face stays upright in the sky.
			const FVector LocalUp = MoonFrame.GetUnitAxis(EAxis::Z);
			FVector SkyUp = FVector::UpVector - Facing * FVector::DotProduct(FVector::UpVector, Facing);
			if (!SkyUp.Normalize()) SkyUp = LocalUp;
			const float Roll = FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(LocalUp, SkyUp), Facing), FVector::DotProduct(LocalUp, SkyUp));
			MoonDiscMaterial->SetScalarParameterValue(TEXT("TextureRoll"), Roll);
		}
	}
}

void AWowSkyManager::LoadLightZones(int32 MapId)
{
	CurrentMapId = MapId;
	MapLights.Empty();

	const FDbcStore& Dbc = FDbcStore::Get();
    bLightTablesBound = Dbc.IsLoaded();
    bHasFloatParams = Dbc.LightFloatParams().Num() > 0;
    bSkyCapturePending = true;
	if (Dbc.Lights().Num() == 0)
	{
		UE_LOG(LogWowSky, Log, TEXT("Light.dbc not loaded, using fallback colors"));
		bHasDbcLights = false;
		return;
	}

	MapLights = Dbc.Lights().GetByMap(static_cast<uint32>(MapId));
	bHasDbcLights = MapLights.Num() > 0;

	UE_LOG(LogWowSky, Log, TEXT("Loaded %d light zones for map %d"), MapLights.Num(), MapId);
}

void AWowSkyManager::RefreshLightData()
{
    // Sky BeginPlay can precede the world manager's synchronous Tier-1 DBC load.
    // Retry once the complete tables are published, including maps with no zones.
    if (!bLightTablesBound && FDbcStore::Get().IsLoaded())
    {
        LoadLightZones(CurrentMapId);
    }
}

void AWowSkyManager::ApplyWeather(uint32 WeatherState, float Grade, uint8 Abrupt)
{
    if (Weather) Weather->ReceiveServer(WeatherState,Grade,Abrupt!=0);
}

void AWowSkyManager::SetWeatherZone(uint32 ZoneId, const FString& Name)
{
    if (Weather) Weather->SetZone(ZoneId,Name);
}

void AWowSkyManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
    RefreshLightData();
    if (Weather)
    {
        Weather->SetOnline(ClockSource.IsValid() && ClockSource->PacketHandler.RealmClock.bSynchronized);
        const FWowWeatherSample& Sample=Weather->GetWeather();
        CurrentWeatherType=WowWeather::EncodeState(Sample.Kind);
        CurrentWeatherGrade=Sample.Intensity;
        // Clear weather still carries a few moving clouds, including automatic weather.
        CurrentWeatherCloud=FMath::Max(0.12f,Sample.Cloud);
    }

    const float PreviewMinutes=WowTestControls::ClockMinutes();
    if(PreviewMinutes>=0.f)
    {
        // The test clock starts at the chosen time and then keeps running at the
        // normal game speed. A new value from Options/Photo Mode restarts it there;
        // the running time is written back so the slider and label follow it.
        if(TestClockMinutes<0.0 || !FMath::IsNearlyEqual(PreviewMinutes,TestClockWritten,0.01f))
            TestClockMinutes=FMath::Clamp(PreviewMinutes,0.f,1439.99f);
        else
        {
            const double Speed=ClockSource.IsValid() ? ClockSource->PacketHandler.RealmClock.MinutesPerSecond : 1.0/60.0;
            TestClockMinutes=FMath::Fmod(TestClockMinutes+DeltaTime*Speed,1440.0);
        }
        TimeOfDay=static_cast<float>(TestClockMinutes);
        TimeSpeed=0.f;
        if(IConsoleVariable* Test=IConsoleManager::Get().FindConsoleVariable(TEXT("wow.TestTime")))
        {
            TestClockWritten=TimeOfDay;
            Test->Set(TimeOfDay,ECVF_SetByCode);
        }
    }
    else if (!bTimeOverride)
    {
        TestClockMinutes=-1.0;
        const FWowRealmClock LocalClock;
        const FWowRealmClock& Clock = ClockSource.IsValid() ? ClockSource->PacketHandler.RealmClock : LocalClock;
        TimeOfDay = Clock.Sample(FPlatformTime::Seconds(), FDateTime::Now());
        TimeSpeed = Clock.MinutesPerSecond;
    }

	// Keep fallback visual sky layers centered on the active camera. Native UE
	// sky atmosphere and volumetric clouds are world-space components.
	const FVector CamPos = GetSkyViewLocation(this);
	if (!CamPos.IsNearlyZero())
	{
		if (SkydomeMesh)
		{
			SkydomeMesh->SetWorldLocation(CamPos);
		}
		if (CloudMesh)
		{
			CloudMesh->SetWorldLocation(CamPos);
            CloudMesh->SetWorldRotation(FRotator(0,float(FPlatformTime::Seconds()*0.02),0));
		}
	}

	UpdateSunPosition();
	UpdateLightColors();
	UpdateFog();
	UpdateSkydome();
	UpdateClouds();
	UpdateCelestialDiscs();
	UpdateNightSky();
    // Login/selection and world streaming finish after the initial sky capture.
    // Capture at the visible player's location, after a short settling period,
    // and refresh on meaningful movement, time or weather changes. Otherwise
    // gameplay retains ambient irradiance from the login scene indefinitely.
    APawn* CapturePawn = UGameplayStatics::GetPlayerPawn(this, 0);
    const double Now = FPlatformTime::Seconds();
    const bool bWorldVisible = IsValid(CapturePawn) && !CapturePawn->IsHidden();
    bool bRefreshWorldSky = false;
    if (SkyLight && bWorldVisible)
    {
        if (SkyCapturePawn.Get() != CapturePawn)
        {
            SkyCapturePawn = CapturePawn;
            LastWorldSkyCaptureMinutes = -1.0f;
            WorldSkyCaptureSettleUntil = Now + 2.0;
        }
        const FVector Position = CapturePawn->GetActorLocation();
        const float MinutesChanged = FMath::Abs(FMath::FindDeltaAngleDegrees(
            LastWorldSkyCaptureMinutes * 0.25f, TimeOfDay * 0.25f)) * 4.0f;
        const bool bChanged = LastWorldSkyCaptureMinutes < 0.0f ||
            FVector::DistSquared(Position, LastWorldSkyCapturePosition) > FMath::Square(5000.0f) ||
            MinutesChanged >= 5.0f ||
            FMath::Abs(CurrentWeatherCloud - LastWorldSkyCaptureCloud) >= 0.10f ||
            FMath::Abs(CurrentWeatherGrade - LastWorldSkyCaptureWeather) >= 0.10f;
        bRefreshWorldSky = bChanged && Now >= WorldSkyCaptureSettleUntil &&
            Now - LastWorldSkyCaptureSeconds >= 5.0;
        if (bRefreshWorldSky)
        {
            SkyLight->SetWorldLocation(Position);
        }
    }
    else
    {
        SkyCapturePawn.Reset();
    }
    if ((bSkyCapturePending || bRefreshWorldSky) && SkyLight)
    {
        SkyLight->RecaptureSky();
        bSkyCapturePending = false;
        if (bRefreshWorldSky)
        {
            LastWorldSkyCapturePosition = CapturePawn->GetActorLocation();
            LastWorldSkyCaptureSeconds = Now;
            LastWorldSkyCaptureMinutes = TimeOfDay;
            LastWorldSkyCaptureCloud = CurrentWeatherCloud;
            LastWorldSkyCaptureWeather = CurrentWeatherGrade;
            UE_LOG(LogWowSky, Log, TEXT("World ambient capture refreshed: position=%s time=%.1f cloud=%.2f weather=%.2f"),
                *LastWorldSkyCapturePosition.ToString(), TimeOfDay, CurrentWeatherCloud, CurrentWeatherGrade);
        }
    }
}

void AWowSkyManager::UpdateSunPosition()
{
	if (!SunLight) return;

	float SunPitch = 0.0f;
	float SunYaw = 0.0f;
	bool bSunAboveHorizon = false;
	CalculateWowSunAngles(TimeOfDay, SunPitch, SunYaw, bSunAboveHorizon);

	// Reuse the single supported forward-rendering directional light as moonlight
	// at night. Hiding it completely left the world as an unreadable silhouette.
	// At night the key light comes from the moon when it is up, so the bright forward-scatter
	// spot it makes in the clouds sits on the moon instead of a phantom "sun" in an empty sky.
	float MoonPitch = 0.0f, MoonYaw = 0.0f;
	bool bMoonAboveHorizon = false;
	// Default: the moon sits opposite the sun all night (always visible). Experimental
		// "realisticmoon": real rise/set times for the date's phase (a waning crescent
		// only rises after midnight). The phase/shape follows the calendar either way.
		CalculateWowSunAngles(TimeOfDay - (FWowClientPreferences::GetWorldOption(TEXT("realisticmoon"))
			? LunarPhase : 0.5f) * 1440.0f, MoonPitch, MoonYaw, bMoonAboveHorizon);
	SunLight->SetWorldRotation(bSunAboveHorizon
		? FRotator(SunPitch, SunYaw, 0.0f)
		: bMoonAboveHorizon ? FRotator(MoonPitch, MoonYaw, 0.0f)
		: FRotator(-SunPitch, SunYaw + 180.0f, 0.0f));
	// Moonless night: keep the light for readability but no hot spot in the clouds.
	const FLinearColor CloudScatter = bSunAboveHorizon ? FLinearColor::White
		: (bMoonAboveHorizon && WowLunarPhase::Illumination(LunarPhase) > 0.05f) ? FLinearColor(0.32f, 0.36f, 0.42f) : FLinearColor(0.10f, 0.10f, 0.12f); // cool, faint moon halo
	if (!SunLight->CloudScatteredLuminanceScale.Equals(CloudScatter))
	{
		SunLight->CloudScatteredLuminanceScale = CloudScatter;
		SunLight->MarkRenderStateDirty();
	}
	SunLight->SetVisibility(true);
	// With no moon up, the night key light points at the anti-sun and the
	// atmosphere's forward (Mie) scattering painted a bright "halo" there with
	// nothing in it. Moonless nights get a near-isotropic, faint Mie term.
	if (SkyAtmosphere)
	{
		// r354b: a new moon (today, 11 Oct) is up but dark: treat it as a moonless night, no glare.
		const bool bMoonlitNight = !bSunAboveHorizon && bMoonAboveHorizon && WowLunarPhase::Illumination(LunarPhase) > 0.05f;
		const bool bMoonlessNight = !bSunAboveHorizon && !bMoonlitNight;
		// r354 (Stefan: "what is this sun doing at night?" - and no moon to be seen): with the moon up
		// the night key light shines from the moon, and the daytime forward scattering (0.78) painted a
		// bright sun-like glare there that drowned the lunar disc. Moonlit nights get a faint, wide halo.
		const float WantedMie = bMoonlessNight ? 0.0032f * 0.15f : bMoonlitNight ? 0.0032f * 0.35f : 0.0032f;
		const float WantedAnisotropy = bMoonlessNight ? 0.2f : bMoonlitNight ? 0.45f : 0.78f;
		if (!FMath::IsNearlyEqual(SkyAtmosphere->MieScatteringScale, WantedMie)
			|| !FMath::IsNearlyEqual(SkyAtmosphere->MieAnisotropy, WantedAnisotropy))
		{
			SkyAtmosphere->SetMieScatteringScale(WantedMie);
			SkyAtmosphere->SetMieAnisotropy(WantedAnisotropy);
		}
	}
    // Disc luminance only: leave the approved surface lighting and exposure intact.
    // r540: the atmosphere disc (sun size 100 % and below; Stefan: "sun at lowest setting looks the
    // best") keeps its 18x luminance and fades out with the dusk like the billboard sun.
    SunLight->SetAtmosphereSunDiskColorScale(bSunAboveHorizon && FWowClientPreferences::GetSunScale() <= 1.01f
        ? FLinearColor(18.0f,16.56f,13.32f) * WowSkySunDuskFade(TimeOfDay) : FLinearColor::Black);

	if (MoonLight)
	{
		MoonLight->SetWorldRotation(FRotator(-SunPitch, SunYaw + 180.0f, 0.0f));
		MoonLight->SetVisibility(!bSunAboveHorizon);
	}
}

void AWowSkyManager::UpdateLightColors()
{
	if (!SunLight) return;

	const bool bDarkerRealisticLighting = UsesDarkerLightingProfile();
	float SunIntensity = GetSunIntensityForTime();
    if (bDarkerRealisticLighting)
    {
        // Match the clear-weather intensity of 07:20, but retain the actual
        // nighttime moon direction and color. Only nighttime is adjusted.
        const float MinutesIntoNight = TimeOfDay < WowDaySunriseMinutes
            ? WowDaySunriseMinutes - TimeOfDay : TimeOfDay - WowDaySunsetMinutes;
        const float NightReferenceBlend = SmoothStep01(MinutesIntoNight / 30.0f);
        constexpr float Reference0720Intensity = 3.05f;
        // Date-based lunar phase modulates the playable baseline; this is
        // not a location-dependent ephemeris or a physical lux simulation.
        const float PhaseLight = WowLunarPhase::Illumination(LunarPhase);
        // Night is clearly night now: moonlight at roughly a quarter (new moon) to
        // two fifths (full moon) of the 07:20 level instead of matching it. Manual
        // exposure means this also darkens the moonlit sky itself.
        // r416 (Stefan: "the night light is quite generous with us - a bit darker?"): about 60 %
        // of the r3xx level, so fires, lamps and the torch carry the night.
        const float MoonIntensity = Reference0720Intensity * FMath::Lerp(0.14f, 0.24f, PhaseLight);
        SunIntensity = FMath::Lerp(SunIntensity, MoonIntensity, NightReferenceBlend);
    }
	const float WeatherDim = (CurrentWeatherType != 0)
		? FMath::Lerp(1.0f, 0.52f, CurrentWeatherGrade)
		: 1.0f;
	SunIntensity *= WeatherDim;
    // Cloud cover must dim direct light even when there is no precipitation.
    // Keep the existing clear-weather day/night balance as the ambient baseline.
    const float Overcast=SmoothStep01((CurrentWeatherCloud-.35f)/.65f);
    const float ClearWeatherSunIntensity=SunIntensity;
    SunIntensity*=FMath::Lerp(1.f,.25f,Overcast);
    // Experimental larger sun: a wider source also softens the shadows accordingly.
    SunLight->SetLightSourceAngle(FMath::Lerp(1.15f,5.f,Overcast));
    SunLight->SetLightSourceSoftAngle(FMath::Lerp(2.f,6.f,Overcast));
    const bool bNight = TimeOfDay < WowDaySunriseMinutes || TimeOfDay > WowDaySunsetMinutes;
    const float MinutesToNightBoundary = TimeOfDay < WowDaySunriseMinutes
        ? WowDaySunriseMinutes - TimeOfDay : TimeOfDay - WowDaySunsetMinutes;
    const float ExperimentalNightBlend = bDarkerRealisticLighting && bNight
        ? SmoothStep01(MinutesToNightBoundary / 30.0f) : 0.0f;
    // Readability compromise without additional directional fill lights.
    // Standard and daytime shadows retain their previous values.
    SunLight->SetShadowAmount(FMath::Lerp(FMath::Lerp(.54f,.22f,Overcast),
        FMath::Lerp(.15f,.10f,Overcast), ExperimentalNightBlend));
    float DayFactor = FMath::Clamp(ClearWeatherSunIntensity / 3.05f, 0.0f, 1.0f);
    // The darker profile's moon used to count as daylight here, so the sky light
    // stayed at full day strength and day colour all night. Keep a modest night
    // ambient (about a quarter of day) so silhouettes still read.
    if (ExperimentalNightBlend > 0.0f)
        DayFactor = FMath::Lerp(DayFactor, 0.17f, ExperimentalNightBlend); // r416: 0.25 -> 0.17, darker night ambient
	const FLinearColor MoonColor(0.48f, 0.58f, 0.82f, 1.0f);
    // Standard nights retain artistic fill independent of the runtime GI cache.
    // The experimental profile uses the sky and local fixtures instead.
    for (UDirectionalLightComponent* Fill : {StandardFillLight.Get(), StandardBackFillLight.Get(), StandardLeftFillLight.Get(), StandardRightFillLight.Get()})
    {
        if (!Fill) continue;
        // Spread the existing artistic ambient fill across model sides instead
        // of concentrating it on upward-facing terrain during daylight. Preserve
        // its Lambertian contribution I*sin(elevation) on flat ground at every
        // time of day. Standard nights receive extra shadow-free fill below tree canopies;
        // daylight retains its existing balance. This supplements
        // incomplete runtime GI; it is not a replacement for physical bounce.
        const float PreviousElevation = FMath::Lerp(12.0f, 55.0f, DayFactor);
        const float PreviousIntensity = FMath::Lerp(0.04f, 0.10f, DayFactor);
        constexpr float FillElevation = 12.0f;
        const float SideFillIntensity = PreviousIntensity *
            FMath::Sin(FMath::DegreesToRadians(PreviousElevation)) /
            FMath::Sin(FMath::DegreesToRadians(FillElevation));
        Fill->SetWorldRotation(FRotator(-FillElevation, Fill->GetComponentRotation().Yaw, 0.0f));
        const float NightBlend = UWowDuskLightComponent::NightFactor(TimeOfDay);
        const float ProfileFill = bDarkerRealisticLighting ? 0.0f : FMath::Lerp(1.f,2.5f,NightBlend);
        Fill->SetIntensity(SideFillIntensity * FMath::Lerp(1.f,.85f,Overcast) * ProfileFill);
        Fill->SetLightColor(FMath::Lerp(FLinearColor(0.58f,0.67f,0.88f), FLinearColor(0.85f,0.88f,1.0f), DayFactor));
    }

	const float MinimumSkyLightIntensity = 0.18f;
	const float SkyLightIntensity = FMath::Lerp(MinimumSkyLightIntensity, 2.35f, DayFactor) *
		FMath::Lerp(1.0f, 0.88f, CurrentWeatherGrade)*FMath::Lerp(1.f,.82f,Overcast);
	if (SkyLight)
	{
        // UE's legacy field name means "lower hemisphere is solid color".
        // Without it the fill color below is ignored and a midnight capture
        // supplies almost no reflected light on the player's shaded side.
        const bool bUseNightFill = bNight && !bDarkerRealisticLighting;
        if (SkyLight->bLowerHemisphereIsBlack != bUseNightFill)
        {
            SkyLight->bLowerHemisphereIsBlack = bUseNightFill;
            bSkyCapturePending = true;
        }
		SkyLight->SetLowerHemisphereColor(bDarkerRealisticLighting
			? FLinearColor(0.08f, 0.10f, 0.15f)
			: FLinearColor(0.12f, 0.15f, 0.22f));
	}

	if (bHasDbcLights)
	{
		FVector PlayerPos = FVector::ZeroVector;
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			PlayerPos = Pawn->GetActorLocation();
		}

		FLinearColor SunColor = ClampSkyColor(
			BlendZoneColor(LP_SunColor, PlayerPos),
			FLinearColor(0.68f, 0.63f, 0.54f, 1.0f),
			1.03f);
		if (bNight)
		{
			SunColor = MoonColor;
		}
		FLinearColor AmbientColor = ClampSkyColor(
			BlendZoneColor(LP_GlobalAmbient, PlayerPos),
			FLinearColor(0.23f, 0.26f, 0.30f, 1.0f),
			1.18f);
		if (bNight && !bDarkerRealisticLighting)
		{
			AmbientColor = ClampSkyColor(
				AmbientColor,
				FLinearColor(0.30f, 0.34f, 0.46f, 1.0f),
				1.0f);
		}

		SunLight->SetLightColor(SunColor);
		SunLight->SetIntensity(SunIntensity);

		if (SkyLight)
		{
			SkyLight->SetLightColor(AmbientColor);
			// Outdoor shadow still needs reflected sky light even with Lumen disabled.
			// Keep buildings and forest floors readable without flattening direct sun.
			SkyLight->SetIntensity(SkyLightIntensity);
		}
	}
	else
	{
		SunLight->SetLightColor(bNight ? MoonColor : GetSunColorFallback());
		SunLight->SetIntensity(SunIntensity);

		if (SkyLight)
		{
			SkyLight->SetLightColor(FMath::Lerp(
				FLinearColor(0.30f, 0.34f, 0.46f, 1.0f),
				FLinearColor(0.82f, 0.90f, 1.0f, 1.0f),
				DayFactor));
			SkyLight->SetIntensity(SkyLightIntensity);
		}
	}
}

static bool GWowLoginGlueSceneActive = false;
static bool GWowLoginGlueHideApplied = false;
static bool GWowLoginGlueHadAtmosphere = true, GWowLoginGlueHadClouds = true;

static float GWowAmbientHaze = 0.f;
void AWowSkyManager::SetAmbientHaze(float Amount) { GWowAmbientHaze = FMath::Clamp(Amount, 0.f, 1.f); }
static float GWowSpiritFog = 0.f;
// r594: how far dense fog hides the volumetric clouds (0 clear or rain mist, 1 fog / snow / dust).
static float GWowCloudFogHide = 0.f;
void AWowSkyManager::SetSpiritFog(float Blend)
{
    GWowSpiritFog = FMath::Clamp(Blend, 0.f, 1.f);
}

void AWowSkyManager::SetLoginGlueSceneActive(bool bActive)
{
	GWowLoginGlueSceneActive = bActive;
}

void AWowSkyManager::UpdateFog()
{
	if (!HeightFog) return;
	// r384: no world fog, aerial perspective or UE clouds over the login glue scene.
	if (GWowLoginGlueSceneActive != GWowLoginGlueHideApplied)
	{
		GWowLoginGlueHideApplied = GWowLoginGlueSceneActive;
		if (GWowLoginGlueSceneActive)
		{
			GWowLoginGlueHadAtmosphere = SkyAtmosphere && SkyAtmosphere->IsVisible();
			GWowLoginGlueHadClouds = VolumetricClouds && VolumetricClouds->IsVisible();
		}
		HeightFog->SetVisibility(!GWowLoginGlueSceneActive);
		if (SkyAtmosphere) SkyAtmosphere->SetVisibility(!GWowLoginGlueSceneActive && GWowLoginGlueHadAtmosphere);
		if (VolumetricClouds) VolumetricClouds->SetVisibility(!GWowLoginGlueSceneActive && GWowLoginGlueHadClouds);
		UE_LOG(LogTemp, Log, TEXT("Sky: login glue scene %s: world fog, atmosphere and clouds %s"),
			GWowLoginGlueSceneActive ? TEXT("shown") : TEXT("gone"), GWowLoginGlueSceneActive ? TEXT("hidden") : TEXT("restored"));
	}
	if (GWowLoginGlueSceneActive) return;
    HeightFog->SetVolumetricFogScatteringDistribution(0.35f);
    HeightFog->SetVolumetricFogDistance(15000.0f);
    HeightFog->SetVolumetricFogStartDistance(150.0f);
    HeightFog->SetVolumetricFogNearFadeInDistance(300.0f);

	FLinearColor FogColor;
	float FogStartDist = 50000.0f;
	float EffectiveFogDensity = FogDensity;
	float WowFogEndCm = 0.0f, WowFogStartScale = -1.0f;

	if (bHasDbcLights)
	{
		FVector PlayerPos = FVector::ZeroVector;
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			PlayerPos = Pawn->GetActorLocation();
		}
		FogColor = BlendZoneColor(LP_SkyFogColor, PlayerPos);

		if (bHasFloatParams)
		{
			float DbcFogDist = BlendZoneFloat(FP_FogDistance, PlayerPos);
			if (DbcFogDist > 0.0f)
			{
				FogStartDist = DbcFogDist * 100.0f; // yards → UE units
				WowFogEndCm = DbcFogDist * 91.44f;   // Light.dbc: where WoW's fog is complete
			}

			float FogMult = BlendZoneFloat(FP_FogMultiplier, PlayerPos);
			if (FogMult > 0.0f)
			{
				EffectiveFogDensity = FogDensity * FogMult;
				WowFogStartScale = FogMult;          // fog starts at end * this
			}
		}
	}
	else
	{
		FogColor = GetFogColorFallback();
	}
	// r591 (Stefan: "the weather and distance fog ... needs to be less unstable by the meter"): the zone light
	// (Light.dbc fog distance, multiplier and colour) is blended by where you stand, so walking past a light
	// sphere's edge in a city changed the distance fog within a few steps. It now follows over ~4 s instead
	// (snapped after a teleport or a jump of more than 300 m).
	{
		static double LastFogSmooth = 0.0;
		static FVector LastFogPos = FVector::ZeroVector;
		static FLinearColor SFogColor = FLinearColor::Black;
		static float SFogStart = 0.f, SFogEnd = 0.f, SFogScale = -1.f, SDensity = 0.f;
		const double FogNow = FPlatformTime::Seconds();
		FVector FogPos = FVector::ZeroVector;
		if (APawn* FogPawn = UGameplayStatics::GetPlayerPawn(this, 0)) FogPos = FogPawn->GetActorLocation();
		const bool bSnap = LastFogSmooth == 0.0 || FogNow - LastFogSmooth > 2.0 || FVector::DistSquared(FogPos, LastFogPos) > FMath::Square(30000.0);
		const float Alpha = bSnap ? 1.f : 1.f - FMath::Exp(-float(FogNow - LastFogSmooth) / 4.f);
		LastFogSmooth = FogNow; LastFogPos = FogPos;
		SFogColor = FMath::Lerp(SFogColor, FogColor, Alpha); FogColor = SFogColor;
		SFogStart = FMath::Lerp(SFogStart, FogStartDist, Alpha); FogStartDist = SFogStart;
		SDensity = FMath::Lerp(SDensity, EffectiveFogDensity, Alpha); EffectiveFogDensity = SDensity;
		if (WowFogEndCm > 0.f) { SFogEnd = bSnap || SFogEnd <= 0.f ? WowFogEndCm : FMath::Lerp(SFogEnd, WowFogEndCm, Alpha); WowFogEndCm = SFogEnd; }
		if (WowFogStartScale > 0.f) { SFogScale = bSnap || SFogScale <= 0.f ? WowFogStartScale : FMath::Lerp(SFogScale, WowFogStartScale, Alpha); WowFogStartScale = SFogScale; }
	}

	const float DayFactor = FMath::Clamp(GetSunIntensityForTime() / 3.05f, 0.0f, 1.0f);
	FogColor = FMath::Lerp(FogColor, GetFogColorFallback(), FMath::Lerp(0.18f, 0.42f, DayFactor));
	EffectiveFogDensity = FMath::Clamp(EffectiveFogDensity, 0.0f, FMath::Lerp(0.00042f, 0.00028f, DayFactor));
    // Clear weather retains the established fog/light balance. Precipitation adds
    // a camera-altitude layer: the old sea-level layer vanished in mountain zones.
    const float Grade=CurrentWeatherType==0?0.f:FMath::Clamp(CurrentWeatherGrade,0.f,1.f);
    // Indoors the weather's fog fades out (r234, Stefan: still foggy inside after the blizzard).
    static float Outdoors=1.f;
    {
        // Real seconds since the last fog update (the sky ticks 10x a second, not every frame).
        static double LastOutdoorsUpdate = 0.0;
        const double NowOutdoors = FPlatformTime::Seconds();
        const float Step = LastOutdoorsUpdate > 0.0 ? float(FMath::Min(NowOutdoors - LastOutdoorsUpdate, 0.5)) : 0.f;
        LastOutdoorsUpdate = NowOutdoors;
        // r400: indoors the fog keeps the share of the view that is outside (a doorway still looks
        // into the blizzard); deep inside a hall it goes.
        Outdoors=FMath::FInterpConstantTo(Outdoors,(Weather && Weather->IsIndoors())?FMath::Clamp(Weather->GetIndoorOpenness()*2.f,0.f,1.f):1.f,Step,0.35f); // r591: 0.7 -> 0.35/s, ~3 s fades
    }
    // r534: mist mixed into rain/snow (or overcast) weather: same fog as the Fog weather, on top.
    const float Mist=Weather ? FMath::Clamp(Weather->GetWeather().Mist,0.f,1.f) : 0.f;
    const float WeatherBlend=FMath::Max(SmoothStep01(Grade*5.f),SmoothStep01(Mist*5.f))*Outdoors;
    const bool Dust=CurrentWeatherType==41, Ash=CurrentWeatherType==106;
    const bool Snow=CurrentWeatherType==7;
    const float Blizzard=Weather ? Weather->GetBlizzardStrength() : 0.f;
    const float Severe=Grade*Grade*(1.f+Blizzard)*(Weather?Weather->GetVisualStrength():1.f)*Outdoors;
    FLinearColor WeatherColor=Dust?FLinearColor(0.68f,0.43f,0.12f):Ash?FLinearColor(0.29f,0.28f,0.27f):
        Snow?FLinearColor(0.65f,0.70f,0.76f):FLinearColor(0.42f,0.48f,0.54f);
    WeatherColor*=FMath::Lerp(0.22f,1.f,DayFactor);
    FogColor=FMath::Lerp(FogColor,WeatherColor,WeatherBlend*0.9f);
    // UE divides density by 1000 (cm). .085 only removes ~8% of contrast
    // over ten metres: too weak for a sandstorm surrounding the camera.
    // r531 (Stefan: "full rain 200% makes it too foggy to actually see the rain"): rain used 1.28, a
    // white-out of a few metres at full strength (Severe 2 with 200 %), which hid the drops themselves.
    // Rain is mist, not fog: about 125 m of sight at full rain, ~60 m at 200 %. Fog, snow and dust keep theirs.
    const bool RainFog=Weather && (Weather->GetWeather().Kind==EWowWeatherKind::Rain || Weather->GetWeather().Kind==EWowWeatherKind::Storm
        || Weather->GetWeather().Kind==EWowWeatherKind::BlackRain);
    float WeatherDensity=FMath::Max((Dust?3.8f:Snow?1.92f:Ash?3.8f:CurrentWeatherType==1?2.2f:RainFog?0.08f:1.28f)*Severe,
        2.2f*Mist*Mist*(Weather?Weather->GetVisualStrength():1.f)*Outdoors);
    // r653 (Stefan in the isometric camera, Durotar sandstorm: the view a brown haze that kept shifting): that camera
    // sits ~25-40 m from the character, so the weather fog between them hid the character and every change of it
    // (caves fading it out, gusts) changed the whole picture. There the weather fog is a third as thick and lies
    // around the character instead of the camera (below).
    const bool bIsoView=FWowClientPreferences::GetWorldOption(TEXT("isometriccamera"));
    if (bIsoView) WeatherDensity*=0.35f;
    EffectiveFogDensity=FMath::Max(EffectiveFogDensity,WeatherDensity);
    // r414 (Stefan: as a ghost the world should be in a blue fog, so you cannot see far): the spirit
    // world thickens the fog to ~50 m of sight in a cold blue (the death post process keeps it pale).
    const float Spirit=GWowSpiritFog;
    const float FogBlend=FMath::Max(WeatherBlend,Spirit);
    // r594 (Stefan, Stormwind harbour in Fog weather: "a strange horizon" - a hard line across the sky with a
    // band of streaky cloud above it): the fogged sky is one grey, but the volumetric cloud layer, traced out
    // to 180 km, still showed through low over the horizon at a grazing angle and ended in a straight line
    // where the trace stops. In dense fog (fog, snow, dust, ash, the spirit world) the clouds fade out with the
    // fog; rain mist keeps them (r588: the overcast deck should show in rain).
    GWowCloudFogHide=RainFog ? 0.f : SmoothStep01((FogBlend-0.35f)/0.4f);
    FogColor=FMath::Lerp(FogColor,FLinearColor(0.30f,0.43f,0.66f)*FMath::Lerp(0.35f,1.f,DayFactor),Spirit*0.95f);
    // r702 (Stefan, Shadowglen at night: "no haze or anything still", and the Distance fog slider "didn't put
    // more haze on"): at night the zone fog colour is almost black, so haze and distance fog only darkened the
    // dark - nothing showed. Night fog keeps at least a faint moonlit blue-grey, so the mist between the trees
    // and the far forest read as haze. Fades in as the daylight goes.
    {
        const float NightHaze = FMath::Clamp((0.6f - DayFactor) / 0.4f, 0.f, 1.f);
        const FLinearColor MoonHaze = FLinearColor(0.040f, 0.052f, 0.078f) * NightHaze;
        FogColor = FLinearColor(FMath::Max(FogColor.R, MoonHaze.R), FMath::Max(FogColor.G, MoonHaze.G), FMath::Max(FogColor.B, MoonHaze.B), FogColor.A);
    }
    EffectiveFogDensity=FMath::Max(EffectiveFogDensity,0.55f*Spirit);
    // Heavy weather (storm, blizzard, thick fog): no volumetric fog, only the per-pixel height
    // fog (r226). The volumetric grid (one cell per 8-16 px) cannot follow hill and palm
    // outlines in fog this dense: it drew stair-stepped "pixel waves" and a dark band along
    // every silhouette against the sky (Stefan, thunderstorm -> blizzard). Hysteresis so it
    // does not flicker while the weather eases in or out.
    {
        static bool bHeavyWeather = false;
        // r227: by fog density, not storm strength - ordinary Fog and Ash weather (Stefan,
        // fog -> ash) also drew dotted, pixelly outlines round trees and mountains.
        // r231: ordinary snow in Dun Morogh (density ~0.1-0.2) still drew the dotted outlines with
        // the best graphics settings, so any noticeable weather fog now uses the height fog only.
        // r590 (Stefan: the weather "keeps changing ALL the time"): in Stormwind's fog the density went 0.03 <-> 0.08
        // every few seconds and the fog switched between the volumetric and the plain height fog each time - two
        // different looks. A switch now has to hold for 20 s before it happens.
        const bool bWantHeavy = bHeavyWeather ? (WeatherDensity > 0.04f || Spirit > 0.15f) : (WeatherDensity > 0.07f || Spirit > 0.25f);
        static double HeavySince = 0.0;
        const double HeavyNow = FPlatformTime::Seconds();
        if (bWantHeavy == bHeavyWeather) HeavySince = 0.0;
        else if (HeavySince == 0.0) HeavySince = HeavyNow;
        else if (HeavyNow - HeavySince > 20.0 || Spirit > 0.25f) { bHeavyWeather = bWantHeavy; HeavySince = 0.0; }
        HeightFog->SetVolumetricFog(FWowClientPreferences::IsVolumetricFogEnabled() && !bHeavyWeather);
        static int32 LoggedHeavy = -1;
        if (LoggedHeavy != int32(bHeavyWeather))
        {
            LoggedHeavy = int32(bHeavyWeather);
            UE_LOG(LogTemp, Log, TEXT("Weather fog density %.3f (type %d, grade %.2f): volumetric fog %s"), WeatherDensity,
                int32(CurrentWeatherType), Grade, bHeavyWeather ? TEXT("off (height fog only)") : TEXT("as in Options"));
        }
    }
    FVector Camera=GetSkyViewLocation(this);
    if (bIsoView) if (APawn* IsoPawn = UGameplayStatics::GetPlayerPawn(this, 0)) Camera = IsoPawn->GetActorLocation() + FVector(0, 0, 300.0f); // r653
    // WoW fogs by distance, not height: far mountains melt into the sky colour by the
    // zone's fog end (Light.dbc), which our sea-level height fog never reached.
    const float DistanceFogScale=CVarDistanceFog.GetValueOnGameThread();
    const bool bDistanceFog=DistanceFogScale>0.01f;
    // 0.65: our view distance reaches far past the original far clip, so the zone's fog
    // end alone still showed the unfinished mountains outside the playable area.
    const float DistFogEnd=0.65f*(WowFogEndCm>1000.f ? WowFogEndCm : 160000.f)/FMath::Max(DistanceFogScale,0.25f);
    const float DistFogStart=DistFogEnd*FMath::Clamp(WowFogStartScale>0.f ? WowFogStartScale : 0.35f,0.05f,0.9f);
    // ~99% extinction by the fog end (UE extinction per cm = density / 1000).
    const float DistFogDensity=FMath::Clamp(4600.f/FMath::Max(DistFogEnd-DistFogStart,5000.f),0.002f,0.12f);
    HeightFog->SetWorldLocation(FVector(Camera.X,Camera.Y,FMath::Lerp(bDistanceFog ? float(Camera.Z)-2000.f : 0.f,float(Camera.Z)-1800.f,FogBlend)));
    // r588 (Stefan, rain + overcast: "no visible clouds", the whole sky one flat grey): rain mist used the same
    // height falloff as a fog bank (0.035), so looking straight up still crossed about 2.3 optical depths of mist
    // and the cloud layer 1.5 km up vanished. Rain mist now thins out within a few tens of metres of height
    // (falloff 0.25): street-level sight stays misty, the clouds overhead show through. Fog, snow, dust and ash
    // keep their deep fog.
    const bool bRainMist=Weather && (Weather->GetWeather().Kind==EWowWeatherKind::Rain || Weather->GetWeather().Kind==EWowWeatherKind::Storm
        || Weather->GetWeather().Kind==EWowWeatherKind::BlackRain) && CurrentWeatherType!=1;
    HeightFog->SetFogHeightFalloff(FMath::Lerp(bDistanceFog ? 0.00001f : 0.00048f,bRainMist ? 0.25f : 0.035f,FogBlend));
    // r525: the distance fog moves to a post-process pass (sky and clouds stay clear) when the
    // realistic cumulus clouds are on and the pass is cooked.
    static TWeakObjectPtr<UPostProcessComponent> GDistanceFogPass;
    static TWeakObjectPtr<UMaterialInstanceDynamic> GDistanceFogMID;
    static bool GDistanceFogMissing = false;
    bool bPostDistanceFog = false;
    if (bDistanceFog && !GDistanceFogMissing && FWowClientPreferences::GetWorldOption(TEXT("cumulusclouds")))
    {
        if (!GDistanceFogPass.IsValid() || !GDistanceFogMID.IsValid() || GDistanceFogPass->GetOwner() != this)
        {
            if (UMaterial* Base = WowLoadOrBuildDistanceFogMaterial())
            {
                UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
                UPostProcessComponent* Pass = NewObject<UPostProcessComponent>(this, TEXT("WowDistanceFog"));
                Pass->bUnbound = true;
                Pass->Priority = 5.f;
                Pass->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, MID));
                AddInstanceComponent(Pass);
                Pass->RegisterComponent();
                GDistanceFogPass = Pass;
                GDistanceFogMID = MID;
                UE_LOG(LogWowSky, Log, TEXT("Distance fog: post-process pass (sky and clouds unfogged)"));
            }
            else
            {
                GDistanceFogMissing = true;
                UE_LOG(LogWowSky, Warning, TEXT("Distance fog: %s is not cooked; keeping the height fog distance fog"), WowDistanceFogMaterialName);
            }
        }
        bPostDistanceFog = GDistanceFogMID.IsValid();
    }
    HeightFog->SetFogDensity(bDistanceFog ? FMath::Max((bPostDistanceFog ? 0.f : DistFogDensity)*(1.f-FogBlend),EffectiveFogDensity) : EffectiveFogDensity);
    // Fog the world, not the sky dome (~190 km out): terrain ends well inside 30 km.
    // r494 (Stefan, Durotar sandstorm at night: "the skies are strange with this storm"): the cutoff
    // keeps the fog off the sky dome, so in a dust storm the fogged mountains stood as flat ochre
    // cut-outs against a clear starry sky with sharp clouds. In dense weather fog (a storm you cannot
    // see far in) the sky is fogged too: no cutoff, so it takes the storm's colour.
    HeightFog->SetFogCutoffDistance(bDistanceFog && FogBlend < 0.35f ? 3000000.f : 0.f);
    // InscatteringColor affects analytic height fog, not volumetric scattering.
    // Set the actual volume albedo too, so sand is ochre rather than grey/white.
    const FLinearColor VolumeTint=Dust?FLinearColor(0.85f,0.57f,0.22f):Ash?FLinearColor(0.40f,0.38f,0.36f):FLinearColor(0.88f,0.93f,1.f);
    HeightFog->SetVolumetricFogAlbedo(FMath::Lerp(FLinearColor::White,VolumeTint,WeatherBlend).ToFColor(true));
    HeightFog->SetVolumetricFogEmissive(WeatherColor*(WeatherBlend*0.16f));
    HeightFog->SetVolumetricFogStartDistance(FMath::Lerp(150.f,20.f,WeatherBlend));
    HeightFog->SetVolumetricFogNearFadeInDistance(FMath::Lerp(300.f,100.f,WeatherBlend));
    // Fully opaque at distance: 0.85-0.95 left far mountains as grey silhouettes in the sky.
    HeightFog->SetFogMaxOpacity(FMath::Lerp(bDistanceFog ? 1.0f : FMath::Lerp(0.18f,0.018f,DayFactor),0.995f,FogBlend));
    // Override scattering light colours only for the explicit Fog weather preset.
    // Surface/key lighting and the warm Sand/Ash presets retain their existing values.
    const bool NeutralFog=CurrentWeatherType==1 && WeatherBlend>0.001f;
    if(HeightFog->bOverrideLightColorsWithFogInscatteringColors!=NeutralFog)
    {
        HeightFog->bOverrideLightColorsWithFogInscatteringColors=NeutralFog;
        HeightFog->MarkRenderStateDirty();
    }
    if(NeutralFog) FogColor=WeatherColor;
    HeightFog->SetDirectionalInscatteringColor(NeutralFog?WeatherColor:FLinearColor::Black);
    HeightFog->SetFogInscatteringColor(FogColor);
    const float ClearStart=bDistanceFog ? DistFogStart : FMath::Max(FogStartDist,FMath::Lerp(82000.f,180000.f,DayFactor));
    const float WeatherStart=Spirit>WeatherBlend ? 300.f : FMath::Lerp(1600.f,20.f,Grade);
    HeightFog->SetStartDistance(FMath::Lerp(ClearStart,WeatherStart,FogBlend));
    // r542 (Stefan, Stormwind harbour at night with distance fog 100-200 %: far mountains stood out as
    // white, untextured cut-outs against the black sky): the fog adds the atmosphere's distant sky
    // light, and the atmosphere still lit a dusk sky (the sun sets at 18:30) after our night sky took
    // over from 17:30. Its share now follows the same dusk fade as the sun disc, so at night the far
    // land fogs into the night fog colour; with the distance fog off it no longer greys the sky either.
    {
        const float AtmosphereFog = FMath::Lerp(0.08f, 1.f, WowSkySunDuskFade(TimeOfDay));
        const FLinearColor WantedAtmosphereFog(AtmosphereFog, AtmosphereFog, AtmosphereFog);
        if (!HeightFog->SkyAtmosphereAmbientContributionColorScale.Equals(WantedAtmosphereFog, 0.005f))
            HeightFog->SetSkyAtmosphereAmbientContributionColorScale(WantedAtmosphereFog);
    }
    if (UMaterialInstanceDynamic* FogMID = GDistanceFogMID.Get())
    {
        // x: extinction per cm (exp2 law, density / 1000 as UE), y: start, z: sky depth, w: max opacity.
        FogMID->SetVectorParameterValue(TEXT("WowP0"), FLinearColor(DistFogDensity * (1.f - FogBlend) / 1000.f, ClearStart, 3000000.f, 1.f));
        const FLinearColor Inscatter = HeightFog->FogInscatteringLuminance;
        FogMID->SetVectorParameterValue(TEXT("WowP1"), FLinearColor(Inscatter.R, Inscatter.G, Inscatter.B, bPostDistanceFog ? 1.f : 0.f));
        static const IConsoleVariable* AffectsFog = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SupportSkyAtmosphereAffectsHeightFog"));
        const FLinearColor Ambient = (!AffectsFog || AffectsFog->GetInt() != 0) ? HeightFog->SkyAtmosphereAmbientContributionColorScale : FLinearColor::Black;
        FogMID->SetVectorParameterValue(TEXT("WowP2"), FLinearColor(Ambient.R, Ambient.G, Ambient.B, 0.f));
    }
    // r438 (Stefan: "the haze around 06:00 is limited to a bubble around the character"): the ambient
    // mist is local fog volumes in cells near the camera, so it ended ~100 m out. Its strength now
    // also drives the height fog's second layer: a thin haze over the whole landscape, densest low
    // down, gone under heavy weather (which has its own fog).
    {
        const float Haze = GWowAmbientHaze * (1.f - FogBlend);
        HeightFog->SetSecondFogDensity(0.012f * Haze);
        HeightFog->SetSecondFogHeightFalloff(0.12f);
        HeightFog->SetSecondFogHeightOffset(0.f);
    }
    // r412 (r20t223): take the weather fog back out of rooms seen through a door or window.
    FWowIndoorFogMask::Update(this,HeightFog,Camera,FogBlend>0.01f);
}

void AWowSkyManager::UpdateSkydome()
{
	if (!SkydomeMaterial) return;

	FLinearColor TopColor, MiddleColor, HorizonColor, Band1Color, SmogColor;
	const float DayFactor = FMath::Clamp(GetSunIntensityForTime() / 3.05f, 0.0f, 1.0f);

	if (bHasDbcLights)
	{
		FVector PlayerPos = FVector::ZeroVector;
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			PlayerPos = Pawn->GetActorLocation();
		}

		TopColor = BlendZoneColor(LP_SkyTopColor, PlayerPos);
		MiddleColor = BlendZoneColor(LP_SkyMiddleColor, PlayerPos);
		Band1Color = BlendZoneColor(LP_SkyBand1, PlayerPos);
		SmogColor = BlendZoneColor(LP_SkySmogColor, PlayerPos);
		HorizonColor = BlendZoneColor(LP_SkyFogColor, PlayerPos);

		TopColor = ClampSkyColor(TopColor, FLinearColor(0.12f, 0.25f, 0.55f, 1.0f), 1.18f);
		MiddleColor = ClampSkyColor(MiddleColor, FLinearColor(0.25f, 0.42f, 0.70f, 1.0f), 1.10f);
		Band1Color = ClampSkyColor(Band1Color, FLinearColor(0.34f, 0.48f, 0.72f, 1.0f), 1.06f);
		SmogColor = ClampSkyColor(SmogColor, FLinearColor(0.46f, 0.54f, 0.68f, 1.0f), 1.03f);
		HorizonColor = ClampSkyColor(HorizonColor, FLinearColor(0.52f, 0.62f, 0.78f, 1.0f), 1.0f);

		// DBC outdoor colors are valuable for zone mood, but some fallback
		// entries are very neutral gray. Blend toward a readable UE-style sky
		// so Elwynn does not look like an empty gray backdrop.
		const FLinearColor ModernTop = GetSkyColorFallback();
		const FLinearColor ModernHorizon = GetFogColorFallback();
		const FLinearColor ModernMiddle = FMath::Lerp(ModernTop, ModernHorizon, 0.28f);
		const FLinearColor ModernBand = FMath::Lerp(ModernTop, ModernHorizon, 0.52f);
		const FLinearColor ModernSmog = FMath::Lerp(ModernTop, ModernHorizon, 0.72f);
		const float OutdoorSkyInfluence = FMath::Lerp(0.32f, 0.84f, DayFactor);
		TopColor = FMath::Lerp(TopColor, ModernTop, OutdoorSkyInfluence);
		MiddleColor = FMath::Lerp(MiddleColor, ModernMiddle, OutdoorSkyInfluence);
		Band1Color = FMath::Lerp(Band1Color, ModernBand, OutdoorSkyInfluence);
		SmogColor = FMath::Lerp(SmogColor, ModernSmog, OutdoorSkyInfluence);
		HorizonColor = FMath::Lerp(HorizonColor, ModernHorizon, OutdoorSkyInfluence);
	}
	else
	{
		FLinearColor Sky = GetSkyColorFallback();
		FLinearColor Fog = GetFogColorFallback();

		TopColor = Sky;
		MiddleColor = FMath::Lerp(Sky, Fog, 0.3f);
		Band1Color = FMath::Lerp(Sky, Fog, 0.5f);
		SmogColor = FMath::Lerp(Sky, Fog, 0.7f);
		HorizonColor = Fog;
	}

	if (DayFactor > 0.35f)
	{
		const float BluePush = FMath::Clamp((DayFactor - 0.35f) / 0.65f, 0.0f, 1.0f);
		TopColor = FMath::Lerp(TopColor, FLinearColor(0.025f, 0.16f, 0.64f, 1.0f), BluePush * 0.98f);
		MiddleColor = FMath::Lerp(MiddleColor, FLinearColor(0.05f, 0.30f, 0.82f, 1.0f), BluePush * 0.92f);
		Band1Color = FMath::Lerp(Band1Color, FLinearColor(0.16f, 0.48f, 0.94f, 1.0f), BluePush * 0.82f);
		SmogColor = FMath::Lerp(SmogColor, FLinearColor(0.34f, 0.62f, 0.94f, 1.0f), BluePush * 0.70f);
		HorizonColor = FMath::Lerp(HorizonColor, FLinearColor(0.46f, 0.70f, 0.94f, 1.0f), BluePush * 0.62f);
	}

    const float Night = WowLunarPhase::NightVisibility(TimeOfDay);
    TopColor = FMath::Lerp(TopColor,FLinearColor(0.004f,0.009f,0.027f),Night);
    MiddleColor = FMath::Lerp(MiddleColor,FLinearColor(0.012f,0.021f,0.047f),Night);
    HorizonColor = FMath::Lerp(HorizonColor,FLinearColor(0.030f,0.040f,0.065f),Night);
	SkydomeMaterial->SetVectorParameterValue(TEXT("SkyTopColor"), TopColor);
	SkydomeMaterial->SetVectorParameterValue(TEXT("SkyMiddleColor"), MiddleColor);
	SkydomeMaterial->SetVectorParameterValue(TEXT("SkyHorizonColor"), HorizonColor);
	SkydomeMaterial->SetVectorParameterValue(TEXT("SkyBand1Color"), Band1Color);
	SkydomeMaterial->SetVectorParameterValue(TEXT("SkySmogColor"), SmogColor);

	// Also drive common EngineSky material parameters when the visible dome uses
	// one of Epic's stock sky materials instead of the legacy procedural graph.
	FLinearColor CloudTint = FMath::Lerp(FLinearColor(1.02f, 1.05f, 1.10f, 1.0f), HorizonColor, 0.025f);
	CloudTint = FMath::Lerp(CloudTint,FLinearColor(0.055f,0.075f,0.12f),Night);
	// Cloud cover is independent of precipitation (overcast is weather code 3).
    const float Coverage = FMath::Clamp(CurrentWeatherCloud,0.0f,1.0f);
    float SkyCloudOpacity = FMath::Lerp(0.02f,0.95f,Coverage);
    CloudTint = FMath::Lerp(CloudTint, FLinearColor(0.56f,0.60f,0.64f,1), Coverage * (1.0f-Night) * 0.35f);
	if (CurrentWeatherType != 0 && CurrentWeatherGrade > 0.01f)
	{
		CloudTint = FMath::Lerp(CloudTint, FLinearColor(0.56f, 0.60f, 0.64f, 1.0f), CurrentWeatherGrade * 0.46f);
		SkyCloudOpacity = FMath::Max(SkyCloudOpacity, FMath::Lerp(0.42f, 0.72f, CurrentWeatherGrade));
	}

	SkydomeMaterial->SetVectorParameterValue(TEXT("SkyCloudColor"), CloudTint);
	SkydomeMaterial->SetScalarParameterValue(TEXT("SkyCloudOpacity"), SkyCloudOpacity);
	SkydomeMaterial->SetVectorParameterValue(TEXT("Zenith Color"), TopColor);
	SkydomeMaterial->SetVectorParameterValue(TEXT("Horizon Color"), HorizonColor);
	SkydomeMaterial->SetVectorParameterValue(TEXT("Cloud Color"), CloudTint);
	SkydomeMaterial->SetVectorParameterValue(TEXT("Overall Color"), FLinearColor(1.0f, 1.04f, 1.08f, 1.0f));
	SkydomeMaterial->SetScalarParameterValue(TEXT("Cloud Opacity"), SkyCloudOpacity);
	SkydomeMaterial->SetScalarParameterValue(TEXT("Cloud Speed"), 0.08f);
	SkydomeMaterial->SetScalarParameterValue(TEXT("Sun Height"), FMath::Clamp(GetSunIntensityForTime() / 3.05f, 0.0f, 1.0f));
	// Epic's sky sphere draws its own sun disc at "Light direction" (never set, so it
	// sat at a fixed spot and showed at night). Our sun disc/halo meshes are the only
	// sun; keep the dome's disc off and its sun-side glow aligned with the real sun.
	{
		float SunPitch = 0.0f, SunYaw = 0.0f;
		bool bSunUp = false;
		CalculateWowSunAngles(TimeOfDay, SunPitch, SunYaw, bSunUp);
		const FVector LightDir = FRotator(SunPitch, SunYaw, 0.0f).Vector();
		SkydomeMaterial->SetVectorParameterValue(TEXT("Light direction"), FLinearColor(LightDir.X, LightDir.Y, LightDir.Z, 0.0f));
		SkydomeMaterial->SetScalarParameterValue(TEXT("Sun brightness"), 0.0f);
	}
}

void AWowSkyManager::UpdateClouds()
{
	// Volumetric cloud resolution (r282, Stefan: "very pixelly clouds", better with native AA).
	// Cinematic effects quality traces the clouds at full resolution (mode 3, no upsampling);
	// below that mode 2 (quarter-resolution trace, full-resolution temporal reconstruction,
	// composited behind opaque geometry - see r238). Only set when it changes.
	{
		static int32 LastMode = -1;
		const int32 Effects = Scalability::GetQualityLevels().EffectsQuality;
		const int32 Mode = Effects >= 4 ? 3 : 2;
		if (Mode != LastMode)
		{
			if (auto* C = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricRenderTarget.Mode"))) C->Set(Mode, ECVF_SetByCode);
			LastMode = Mode;
		}
	}
	if (!CloudMaterial && !VolumetricCloudMaterial) return;

	float CloudOpacity = 0.48f;

	if (bHasDbcLights && bHasFloatParams)
	{
		FVector PlayerPos = FVector::ZeroVector;
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			PlayerPos = Pawn->GetActorLocation();
		}

		float Density = BlendZoneFloat(FP_CloudDensity, PlayerPos);
		if (Density > 0.0f)
		{
			CloudOpacity = FMath::Clamp(Density * 0.82f, 0.32f, 0.62f);
		}
	}

	// Reduce cloud visibility at night
	float NightFactor = GetSunIntensityForTime() / 3.05f;
	CloudOpacity = FMath::Clamp(CloudOpacity * FMath::Lerp(0.38f, 1.0f, NightFactor), 0.10f, 0.68f);
	if (CurrentWeatherType != 0 && CurrentWeatherGrade > 0.01f)
	{
		CloudOpacity = FMath::Max(CloudOpacity, FMath::Lerp(0.42f, 0.86f, CurrentWeatherGrade));
	}

    CloudOpacity=FMath::Lerp(0.10f,0.90f,CurrentWeatherCloud);

	// Cloud color tinted by sky fog
	FLinearColor CloudColor;
	if (bHasDbcLights)
	{
		FVector PlayerPos = FVector::ZeroVector;
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			PlayerPos = Pawn->GetActorLocation();
		}
		CloudColor = FMath::Lerp(BlendZoneColor(LP_SkyFogColor, PlayerPos), FLinearColor::White, 0.64f);
	}
	else
	{
		CloudColor = FMath::Lerp(GetFogColorFallback(), FLinearColor::White, 0.64f);
	}

	CloudColor = FMath::Lerp(CloudColor,FLinearColor(0.08f,0.11f,0.18f),WowLunarPhase::NightVisibility(TimeOfDay));
    // Only the cloud material is graded; preserve the approved world lighting.
    if(CurrentWeatherType==86 || (Weather && (Weather->GetWeather().Kind==EWowWeatherKind::Rain || Weather->GetWeather().Kind==EWowWeatherKind::BlackRain)))
    {
        const float Wet=FMath::Clamp(.45f+CurrentWeatherGrade*.55f,0.f,1.f);
        CloudColor=FMath::Lerp(CloudColor,CloudColor*(CurrentWeatherType==86?.42f:.64f),Wet);
    }
	if (CloudMaterial)
	{
		CloudMaterial->SetVectorParameterValue(TEXT("CloudColor"), CloudColor);
		CloudMaterial->SetScalarParameterValue(TEXT("CloudOpacity"), CloudOpacity);
	}

	if (VolumetricCloudMaterial)
	{
		// Names are from UE 5.7's m_SimpleVolumetricCloud material; the old guessed names were no-ops.
        // Audited UE 5.7 graph: coverage is a signed bias, density defaults to .008,
        // and the stock pattern spans 256 km. Do not feed opacity into density.
        VolumetricCloudMaterial->SetScalarParameterValue(TEXT("Layout_CloudGlobalScale"),32.f);
        // Keep the broad wind drift, but avoid fast opposing erosion layers
        // repeatedly cutting bright holes through an otherwise overcast sky.
        VolumetricCloudMaterial->SetVectorParameterValue(TEXT("Noise1_Coordinates"),FLinearColor(4.167f,4.444f,9.091f,.35f));
        VolumetricCloudMaterial->SetVectorParameterValue(TEXT("Noise2_Coordinates"),FLinearColor(60.f,60.f,75.f,-.25f));
        VolumetricCloudMaterial->SetVectorParameterValue(TEXT("Noise3_Coordinates"),FLinearColor(30.f,40.f,25.f,.45f));
        // r513 (Stefan's cumulus photos): fair weather (CurrentWeatherCloud ~0.12) used to give coverage
        // -0.66 and density .005 - a few thin wisps. Now scattered, dense, white heaps with blue gaps
        // between them; overcast still closes up at the top end.
        VolumetricCloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), FMath::Lerp(-0.30f,0.8f,CurrentWeatherCloud));
        VolumetricCloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), FMath::Lerp(0.016f,0.03f,CloudOpacity));
        VolumetricCloudMaterial->SetVectorParameterValue(TEXT("Cloud_AlbedoColor"), FMath::Lerp(CloudColor, FLinearColor(0.97f,0.97f,0.97f), 0.6f*FMath::Clamp(NightFactor,0.f,1.f)*(1.f-FMath::Clamp(CurrentWeatherGrade,0.f,1.f))));
        const float Storm=CurrentWeatherType==86?CurrentWeatherGrade:0.f;
        VolumetricCloudMaterial->SetScalarParameterValue(TEXT("StormClouds"),Storm);
        // Lightning is timed by the weather component, not an unrelated shader clock.
        const float Flash=Weather?Weather->GetLightningFlash():0.f;
        VolumetricCloudMaterial->SetVectorParameterValue(TEXT("Storm_LightningColor"),FLinearColor(12.f*Flash,15.f*Flash,20.f*Flash,1.f));
        VolumetricCloudMaterial->SetVectorParameterValue(TEXT("Storm_LightningAnim"),FLinearColor(0.f,0.f,0.5f,1.f));
        if(Weather) {
            const auto& W=Weather->GetWeather();
            const float A=FMath::DegreesToRadians(W.Direction+180.f);
            const float Now=GetWorld()->GetTimeSeconds();
            const float Dt=FMath::Clamp(Now-CloudWindLastTime,0.f,0.25f);
            CloudWindLastTime=Now;
            CloudWindOffset+=FVector2D(FMath::Cos(A),FMath::Sin(A))*FMath::Clamp(W.Wind/30.f,0.05f,1.f)*Dt;
            const float Divisor=FMath::Max(Now,0.001f);
            VolumetricCloudMaterial->SetVectorParameterValue(TEXT("Layout_WindControls"),FLinearColor(CloudWindOffset.X/Divisor,CloudWindOffset.Y/Divisor,0.f,1.f));
        }
	}

	// r514: cumulus material (option "Realistic cumulus clouds"); the engine material stays the fallback.
	if (VolumetricClouds)
	{
		const int32 WantCumulus = (CumulusCloudMaterial && FWowClientPreferences::GetWorldOption(TEXT("cumulusclouds"))) ? 1 : 0;
		if (WantCumulus != CumulusCloudsActive)
		{
			CumulusCloudsActive = WantCumulus;
			VolumetricClouds->SetMaterial(WantCumulus ? Cast<UMaterialInterface>(CumulusCloudMaterial.Get()) : Cast<UMaterialInterface>(VolumetricCloudMaterial.Get()));
			// Cumulus: base 1.5 km, room for towers up to 7.5 km; engine layer as r513.
			VolumetricClouds->SetLayerBottomAltitude(WantCumulus ? 1.5f : 2.2f);
			VolumetricClouds->SetLayerHeight(WantCumulus ? 6.0f : 4.0f);
			// r525 (Stefan: clouds "pixelly and not as 3D"): the cumulus heaps need finer steps through
			// the 6 km layer and their own shadowing, and a less coarse trace (VRT mode 2 traces at
			// quarter resolution and reconstructs with nearest samples): 1.5x the trace resolution.
			VolumetricClouds->SetViewSampleCountScale(WantCumulus ? 2.5f : 1.5f);
			VolumetricClouds->SetShadowViewSampleCountScale(WantCumulus ? 0.6f : 0.25f);
			if (auto* C = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricRenderTarget.Scale"))) C->Set(WantCumulus ? 1.5f : 1.0f, ECVF_SetByCode);
			UE_LOG(LogWowSky, Log, TEXT("Volumetric clouds: %s"), WantCumulus ? TEXT("cumulus (M_WowCumulusCloud)") : TEXT("engine simple clouds"));
		}
		if (WantCumulus && CumulusCloudMaterial)
		{
			const float Grade = FMath::Clamp(CurrentWeatherGrade, 0.f, 1.f);
			const float Overcast = FMath::Clamp((CurrentWeatherCloud - 0.35f) / 0.6f, 0.f, 1.f);
			// Fair weather (cloud ~0.12): scattered heaps over about a third of the sky; overcast closes up.
			// r588 (Stefan, rain + overcast at 50-200 %: "skies remain the same", one flat grey carpet; "should have
			// had some clouds being white, some grey and some darker"): at 0.97 coverage the heaps merged into one
			// even sheet. Overcast now stops at 0.84, so the deck keeps its lumps, seams and thinner patches, and
			// the heaps are denser (darker bases, brighter sunlit tops) instead of a thin uniform veil.
			CumulusCloudMaterial->SetScalarParameterValue(TEXT("Coverage"), FMath::Lerp(0.36f, 0.84f, FMath::Clamp(CurrentWeatherCloud, 0.f, 1.f)));
			// r525: softer edges (light reaches into the heaps, less binary per trace pixel).
			CumulusCloudMaterial->SetScalarParameterValue(TEXT("Density"), FMath::Lerp(0.025f, 0.09f, FMath::Max(Overcast, Grade)));
			// r525 (Stefan: clouds passing the sun should shade the ground more, at least with darker
			// lighting): cloud shadows on the ground, stronger with the darker lighting profile.
			if (SunLight)
			{
				const float Surface = UsesDarkerLightingProfile() ? 0.9f : 0.65f;
				if (!FMath::IsNearlyEqual(SunLight->CloudShadowOnSurfaceStrength, Surface))
				{
					SunLight->CloudShadowStrength = 1.0f;
					SunLight->CloudShadowOnSurfaceStrength = Surface;
					SunLight->MarkRenderStateDirty();
				}
			}
			CumulusCloudMaterial->SetScalarParameterValue(TEXT("Towering"), FMath::Lerp(0.15f, 1.0f, FMath::Max(Overcast * 0.6f, CurrentWeatherType == 86 ? Grade : Grade * 0.5f)));
			CumulusCloudMaterial->SetScalarParameterValue(TEXT("CloudScale"), 1.6f);
			// Physical albedo (clouds are near-white; darkness comes from light and self-shadow), a bit
			// lower for rain clouds.
			// r531 (Stefan: "rain is often bringing grey clouds, not white ones"): rain clouds go grey from
			// light rain on (0.94 -> 0.62 at the start of rain, 0.45 in a downpour), not only at full grade.
			const bool bRainClouds = CurrentWeatherType == 86 || (Weather && (Weather->GetWeather().Kind == EWowWeatherKind::Rain
				|| Weather->GetWeather().Kind == EWowWeatherKind::Storm || Weather->GetWeather().Kind == EWowWeatherKind::BlackRain));
			const float A = bRainClouds && Grade > 0.01f ? FMath::Lerp(0.62f, 0.45f, Grade) : FMath::Lerp(0.94f, 0.8f, Overcast);
			CumulusCloudMaterial->SetVectorParameterValue(TEXT("Albedo"), FLinearColor(A, A, A, 1.f));
			CumulusCloudMaterial->SetVectorParameterValue(TEXT("WindOffset"), FLinearColor(float(CloudWindOffset.X) * 0.02f, float(CloudWindOffset.Y) * 0.02f, 0.f, 0.f));
			const float Flash = Weather ? Weather->GetLightningFlash() : 0.f;
			CumulusCloudMaterial->SetVectorParameterValue(TEXT("LightningColor"), FLinearColor(40.f * Flash, 48.f * Flash, 60.f * Flash, 1.f));
		}
	}

	// r594: dense fog hides the cloud layer (eased over ~2 s so a fog change does not snap the sky).
	{
		static float CloudFogHide = 0.f;
		static double LastHideTime = 0.0;
		const double NowHide = FPlatformTime::Seconds();
		const float HideStep = LastHideTime > 0.0 ? float(FMath::Min(NowHide - LastHideTime, 0.5)) : 1.f;
		LastHideTime = NowHide;
		CloudFogHide = FMath::FInterpConstantTo(CloudFogHide, GWowCloudFogHide, HideStep, 0.5f);
		const float Keep = 1.f - CloudFogHide;
		if (VolumetricCloudMaterial)
			VolumetricCloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), FMath::Lerp(0.016f, 0.03f, CloudOpacity) * Keep);
		if (CumulusCloudMaterial)
		{
			const float Overcast = FMath::Clamp((CurrentWeatherCloud - 0.35f) / 0.6f, 0.f, 1.f);
			CumulusCloudMaterial->SetScalarParameterValue(TEXT("Density"), FMath::Lerp(0.025f, 0.09f, FMath::Max(Overcast, FMath::Clamp(CurrentWeatherGrade, 0.f, 1.f))) * Keep);
		}
	}

	if (VolumetricClouds)
	{
		VolumetricClouds->SetSkyLightCloudBottomOcclusion(FMath::Lerp(0.28f, 0.38f, FMath::Clamp(NightFactor, 0.f, 1.f))); // r513: greyer cumulus bases by day (NightFactor here is daylight, 1 = noon)
	}
}

// ── DBC Color Interpolation ────────────────────────────────────────────────────

FLinearColor AWowSkyManager::InterpolateDbcColor(uint32 ParamID, ELightProperty Property) const
{
	if (ParamID == 0) return FLinearColor::White;

	const FDbcStore& Dbc = FDbcStore::Get();

	uint32 IntParamID = (ParamID - 1) * 18 + static_cast<uint32>(Property) + 1;
	const FLightIntParamsDbcEntry* Entry = Dbc.LightIntParams().GetById(IntParamID);
	if (!Entry || Entry->EntryCount == 0)
	{
		return FLinearColor::White;
	}

	uint32 DbcTime = static_cast<uint32>(TimeOfDay * 2.0f);
	uint32 Count = FMath::Min(Entry->EntryCount, static_cast<uint32>(FLightIntParamsDbcEntry::MaxEntries));

	// Find bracketing entries
	uint32 LowIdx = Count - 1;
	uint32 HighIdx = 0;
	bool bFoundHigh = false;

	for (uint32 i = 0; i < Count; i++)
	{
		if (Entry->Times[i] <= DbcTime)
		{
			LowIdx = i;
		}
		if (!bFoundHigh && Entry->Times[i] > DbcTime)
		{
			HighIdx = i;
			bFoundHigh = true;
		}
	}

	// If no entry found above current time, wrap to first entry (next day)
	if (!bFoundHigh)
	{
		HighIdx = 0;
	}

	auto DecodeColor = [](uint32 Packed) -> FLinearColor
	{
		float B = ((Packed >> 0) & 0xFF) / 255.0f;
		float G = ((Packed >> 8) & 0xFF) / 255.0f;
		float R = ((Packed >> 16) & 0xFF) / 255.0f;
		return FLinearColor(R, G, B);
	};

	FLinearColor LowColor = DecodeColor(Entry->Values[LowIdx]);
	FLinearColor HighColor = DecodeColor(Entry->Values[HighIdx]);

	uint32 LowTime = Entry->Times[LowIdx];
	uint32 HighTime = Entry->Times[HighIdx];

	if (HighTime <= LowTime)
	{
		HighTime += 2880;
		uint32 AdjTime = (DbcTime < LowTime) ? DbcTime + 2880 : DbcTime;
		float Alpha = (HighTime > LowTime) ? static_cast<float>(AdjTime - LowTime) / (HighTime - LowTime) : 0.0f;
		return FMath::Lerp(LowColor, HighColor, FMath::Clamp(Alpha, 0.0f, 1.0f));
	}

	float Alpha = static_cast<float>(DbcTime - LowTime) / FMath::Max(1u, HighTime - LowTime);
	return FMath::Lerp(LowColor, HighColor, FMath::Clamp(Alpha, 0.0f, 1.0f));
}

float AWowSkyManager::InterpolateDbcFloat(uint32 ParamID, ELightFloatProperty Property) const
{
	if (ParamID == 0) return 0.0f;

	const FDbcStore& Dbc = FDbcStore::Get();

	uint32 FloatParamID = (ParamID - 1) * 6 + static_cast<uint32>(Property) + 1;
	const FLightFloatParamsDbcEntry* Entry = Dbc.LightFloatParams().GetById(FloatParamID);
	if (!Entry || Entry->EntryCount == 0)
	{
		return 0.0f;
	}

	uint32 DbcTime = static_cast<uint32>(TimeOfDay * 2.0f);
	uint32 Count = FMath::Min(Entry->EntryCount, static_cast<uint32>(FLightFloatParamsDbcEntry::MaxEntries));

	uint32 LowIdx = Count - 1;
	uint32 HighIdx = 0;
	bool bFoundHigh = false;

	for (uint32 i = 0; i < Count; i++)
	{
		if (Entry->Times[i] <= DbcTime)
		{
			LowIdx = i;
		}
		if (!bFoundHigh && Entry->Times[i] > DbcTime)
		{
			HighIdx = i;
			bFoundHigh = true;
		}
	}

	if (!bFoundHigh)
	{
		HighIdx = 0;
	}

	float LowVal = Entry->Values[LowIdx];
	float HighVal = Entry->Values[HighIdx];
	uint32 LowTime = Entry->Times[LowIdx];
	uint32 HighTime = Entry->Times[HighIdx];

	if (HighTime <= LowTime)
	{
		HighTime += 2880;
		uint32 AdjTime = (DbcTime < LowTime) ? DbcTime + 2880 : DbcTime;
		float Alpha = (HighTime > LowTime) ? static_cast<float>(AdjTime - LowTime) / (HighTime - LowTime) : 0.0f;
		return FMath::Lerp(LowVal, HighVal, FMath::Clamp(Alpha, 0.0f, 1.0f));
	}

	float Alpha = static_cast<float>(DbcTime - LowTime) / FMath::Max(1u, HighTime - LowTime);
	return FMath::Lerp(LowVal, HighVal, FMath::Clamp(Alpha, 0.0f, 1.0f));
}

FLinearColor AWowSkyManager::BlendZoneColor(ELightProperty Property, const FVector& PlayerPos) const
{
	if (MapLights.Num() == 0)
	{
		switch (Property)
		{
		case LP_SunColor: return GetSunColorFallback();
		case LP_SkyFogColor: return GetFogColorFallback();
		default: return GetSkyColorFallback();
		}
	}

	FVector WowPos = FWowCoordinate::UEToWow(PlayerPos);
	FLinearColor Result = FLinearColor::Black;
	float TotalWeight = 0.0f;
	const FLightDbcEntry* DefaultLight = nullptr;

	for (const FLightDbcEntry* Light : MapLights)
	{
		if (Light->FalloffEnd == 0.0f)
		{
			DefaultLight = Light;
			continue;
		}

		FVector LightPos(Light->X, Light->Y, Light->Z);
		float Dist = FVector::Dist(WowPos, LightPos);
		if (Dist > Light->FalloffEnd) continue;

		float Weight = 1.0f;
		if (Dist > Light->FalloffStart && Light->FalloffEnd > Light->FalloffStart)
		{
			Weight = 1.0f - (Dist - Light->FalloffStart) / (Light->FalloffEnd - Light->FalloffStart);
		}

		uint32 ParamID = Light->ParamIDs[0];
		if (ParamID == 0) continue;

		Result += InterpolateDbcColor(ParamID, Property) * Weight;
		TotalWeight += Weight;
	}

	if (TotalWeight < 1.0f && DefaultLight)
	{
		uint32 DefaultParamID = DefaultLight->ParamIDs[0];
		if (DefaultParamID != 0)
		{
			float DefaultWeight = 1.0f - TotalWeight;
			Result += InterpolateDbcColor(DefaultParamID, Property) * DefaultWeight;
			TotalWeight += DefaultWeight;
		}
	}

	if (TotalWeight > 0.0f)
	{
		Result /= TotalWeight;
	}
	else
	{
		switch (Property)
		{
		case LP_SunColor: return GetSunColorFallback();
		case LP_SkyFogColor: return GetFogColorFallback();
		default: return GetSkyColorFallback();
		}
	}

	return Result;
}

float AWowSkyManager::BlendZoneFloat(ELightFloatProperty Property, const FVector& PlayerPos) const
{
	if (MapLights.Num() == 0 || !bHasFloatParams) return 0.0f;

	FVector WowPos = FWowCoordinate::UEToWow(PlayerPos);
	float Result = 0.0f;
	float TotalWeight = 0.0f;
	const FLightDbcEntry* DefaultLight = nullptr;

	for (const FLightDbcEntry* Light : MapLights)
	{
		if (Light->FalloffEnd == 0.0f)
		{
			DefaultLight = Light;
			continue;
		}

		FVector LightPos(Light->X, Light->Y, Light->Z);
		float Dist = FVector::Dist(WowPos, LightPos);
		if (Dist > Light->FalloffEnd) continue;

		float Weight = 1.0f;
		if (Dist > Light->FalloffStart && Light->FalloffEnd > Light->FalloffStart)
		{
			Weight = 1.0f - (Dist - Light->FalloffStart) / (Light->FalloffEnd - Light->FalloffStart);
		}

		uint32 ParamID = Light->ParamIDs[0];
		if (ParamID == 0) continue;

		Result += InterpolateDbcFloat(ParamID, Property) * Weight;
		TotalWeight += Weight;
	}

	if (TotalWeight < 1.0f && DefaultLight)
	{
		uint32 DefaultParamID = DefaultLight->ParamIDs[0];
		if (DefaultParamID != 0)
		{
			float DefaultWeight = 1.0f - TotalWeight;
			Result += InterpolateDbcFloat(DefaultParamID, Property) * DefaultWeight;
			TotalWeight += DefaultWeight;
		}
	}

	return (TotalWeight > 0.0f) ? Result / TotalWeight : 0.0f;
}

// ── Fallback (hardcoded) color functions ────────────────────────────────────────

FLinearColor AWowSkyManager::GetSunColorFallback() const
{
	float T = TimeOfDay;
	if (T >= 300.0f && T < 420.0f)
	{
		float A = (T - 300.0f) / 120.0f;
		return FMath::Lerp(FLinearColor(1.0f, 0.50f, 0.24f), FLinearColor(1.0f, 0.985f, 0.95f), A);
	}
	else if (T >= 420.0f && T < 1020.0f)
	{
		return FLinearColor(1.0f, 0.985f, 0.95f);
	}
	else if (T >= 1020.0f && T < 1140.0f)
	{
		float A = (T - 1020.0f) / 120.0f;
		return FMath::Lerp(FLinearColor(1.0f, 0.985f, 0.95f), FLinearColor(1.0f, 0.44f, 0.22f), A);
	}
	return FLinearColor(0.22f, 0.28f, 0.48f);
}

float AWowSkyManager::GetSunIntensityForTime() const
{
	float T = TimeOfDay;
	const bool bDarkerRealisticLighting = UsesDarkerLightingProfile();
	// Standard nights use the previous 05:45 light level as their floor.
    // Keep the old dawn/dusk curve above that floor, avoiding a dawn jump.
    const float NightDirectionalIntensity = bDarkerRealisticLighting ? 0.22f : 0.55f;
    // The experimental floor now matches its former 05:45 dawn intensity.
    // The experimental profile does not use the standard artistic fill lights.
    const float NightFloor = FMath::Lerp(NightDirectionalIntensity, 3.05f, 45.f/120.f);
	if (T >= 300.0f && T < 420.0f)
	{
		return FMath::Max(NightFloor, FMath::Lerp(NightDirectionalIntensity, 3.05f, (T - 300.0f) / 120.0f));
	}
	else if (T >= 420.0f && T < 1020.0f)
	{
		return 3.05f;
	}
	else if (T >= 1020.0f && T < 1140.0f)
	{
		return FMath::Max(NightFloor, FMath::Lerp(3.05f, NightDirectionalIntensity, (T - 1020.0f) / 120.0f));
	}
	return NightFloor;
}

FLinearColor AWowSkyManager::GetFogColorFallback() const
{
	float T = TimeOfDay;
	if (T >= 300.0f && T < 420.0f)
	{
		float A = (T - 300.0f) / 120.0f;
		return FMath::Lerp(FLinearColor(0.08f, 0.09f, 0.15f), FLinearColor(0.58f, 0.70f, 0.92f), A);
	}
	else if (T >= 420.0f && T < 1020.0f)
	{
		return FLinearColor(0.45f, 0.66f, 1.0f);
	}
	else if (T >= 1020.0f && T < 1140.0f)
	{
		float A = (T - 1020.0f) / 120.0f;
		return FMath::Lerp(FLinearColor(0.45f, 0.66f, 1.0f), FLinearColor(0.08f, 0.09f, 0.15f), A);
	}
	return FLinearColor(0.08f, 0.09f, 0.15f);
}

FLinearColor AWowSkyManager::GetSkyColorFallback() const
{
	float T = TimeOfDay;
	if (T >= 420.0f && T < 1020.0f)
	{
		return FLinearColor(0.08f, 0.34f, 1.0f);
	}
	else if (T >= 300.0f && T < 420.0f)
	{
		float A = (T - 300.0f) / 120.0f;
		return FMath::Lerp(FLinearColor(0.05f, 0.06f, 0.16f), FLinearColor(0.08f, 0.34f, 1.0f), A);
	}
	else if (T >= 1020.0f && T < 1140.0f)
	{
		float A = (T - 1020.0f) / 120.0f;
		return FMath::Lerp(FLinearColor(0.08f, 0.34f, 1.0f), FLinearColor(0.05f, 0.06f, 0.16f), A);
	}
	return FLinearColor(0.05f, 0.06f, 0.16f);
}

void AWowSkyManager::PrepareStandaloneMaterials()
{
    UWowWeatherComponent::PrepareMaterial(this);
    FWowIndoorFogMask::PrepareMaterial(); // r412/r478: M_WowIndoorFogMask_v2
    WowLoadOrBuildCumulusCloudMaterial(); // r514: M_WowCumulusCloud
    WowLoadOrBuildDistanceFogMaterial(); // r525: M_WowDistanceFog
    CreateSkydomeMesh();
    CreateCloudMesh();
    CreateCelestialDiscs();
    CreateNightSky();
}

void AWowSkyManager::SetClockSource(UWowConnectionManager* Source)
{
    ClockSource = Source;
}
