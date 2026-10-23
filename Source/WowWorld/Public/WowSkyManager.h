#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WowSkyManager.generated.h"

class UWowWeatherComponent;
class ACameraActor;
class UWowConnectionManager;
class UDirectionalLightComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UVolumetricCloudComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
struct FLightDbcEntry;

/**
 * Manages time-of-day sky rendering with sun/moon positioning,
 * sky gradient colors, fog, and clouds driven by Light.dbc data with zone blending.
 */
UCLASS()
class WOWWORLD_API AWowSkyManager : public AActor
{
	GENERATED_BODY()
public:
    void PrepareStandaloneMaterials();
    /** r384: the login screen's M2 glue scene carries its own painted sky and haze, like the
     *  original client. While it is shown the world height fog, sky atmosphere and volumetric
     *  clouds are hidden (they hazed the scene over and filled its gaps with flat cyan). */
    static void SetLoginGlueSceneActive(bool bActive);
    /** r414: 0..1 spirit world (ghost): a dense cold blue haze that limits the view. */
    static void SetSpiritFog(float Blend);
    /** r438: strength (0..1) of the dawn/ambient haze; a low-lying layer over the whole view, not only around the character. */
    static void SetAmbientHaze(float Amount);
	AWowSkyManager();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Current time in minutes (0-1440, where 720 = noon) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Sky")
	float TimeOfDay = 720.0f; // 12:00 PM

	/** Displayed clock rate: game minutes per real second; 1/60 = realtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Sky")
	float TimeSpeed = 1.0f / 60.0f;

	/** Enable fog */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Sky")
	bool bFogEnabled = true;

	/** Fog density (fallback when LightFloatParams not loaded) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Sky")
	float FogDensity = 0.00024f;

	/** Current map ID for Light.dbc lookups */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Sky")
	int32 CurrentMapId = 0;

	float GetTimeOfDay() const { return TimeOfDay; }
    void SetClockSource(UWowConnectionManager* Source);
    void CreateNightSky();
    void UpdateNightSky();

	/** Load light zones from Light.dbc for the given map */
	void LoadLightZones(int32 MapId);
    void RefreshLightData();
    bool HasZoneLighting() const { return bHasDbcLights; }
    bool HasFogParameters() const { return bHasFloatParams; }

	/** Apply SMSG_WEATHER state and intensity; the last byte requests an abrupt transition. */
	void ApplyWeather(uint32 WeatherState, float Grade, uint8 Abrupt);
    void SetWeatherZone(uint32 ZoneId, const FString& Name);

private:
    UPROPERTY() TObjectPtr<UWowWeatherComponent> Weather;
    float CurrentWeatherCloud = 0.2f;
    FVector2D CloudWindOffset = FVector2D::ZeroVector;
    float CloudWindLastTime = 0;
    UPROPERTY() TObjectPtr<ACameraActor> NightPreviewCamera;
    TWeakObjectPtr<UWowConnectionManager> ClockSource;
    bool bTimeOverride = false;
    /** Test clock: runs forward from the value set in Options (minutes), and the last value written back. */
    double TestClockMinutes = -1.0;
    float TestClockWritten = -2.0f;
    TWeakObjectPtr<APawn> SkyCapturePawn;
    FVector LastWorldSkyCapturePosition = FVector::ZeroVector;
    double LastWorldSkyCaptureSeconds = -1000.0;
    double WorldSkyCaptureSettleUntil = 0.0;
    float LastWorldSkyCaptureMinutes = -1.0f;
    float LastWorldSkyCaptureCloud = -1.0f;
    float LastWorldSkyCaptureWeather = -1.0f;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> StarsMesh;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StarsMaterial;
    float LunarPhase = 0.5f;
    int64 LastLunarUpdateMinute = -1;
	UPROPERTY()
	TObjectPtr<UDirectionalLightComponent> SunLight;
    UPROPERTY()
    TObjectPtr<UDirectionalLightComponent> StandardFillLight;
    UPROPERTY()
    TObjectPtr<UDirectionalLightComponent> StandardBackFillLight;
    UPROPERTY()
    TObjectPtr<UDirectionalLightComponent> StandardLeftFillLight;
    UPROPERTY()
    TObjectPtr<UDirectionalLightComponent> StandardRightFillLight;

	UPROPERTY()
	TObjectPtr<UDirectionalLightComponent> MoonLight;

	UPROPERTY()
	TObjectPtr<USkyAtmosphereComponent> SkyAtmosphere;

	UPROPERTY()
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY()
	TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	UPROPERTY()
	TObjectPtr<UVolumetricCloudComponent> VolumetricClouds;

	/** Skydome mesh for gradient band rendering */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SkydomeMesh;

	/** Cloud plane mesh */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> CloudMesh;

	/** Dynamic material for skydome gradient */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SkydomeMaterial;

	/** Dynamic material for cloud layer */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;

	/** Dynamic material for the native UE volumetric cloud layer */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> VolumetricCloudMaterial;

	/** r514: our own cumulus cloud material (M_WowCumulusCloud), option "cumulusclouds" */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CumulusCloudMaterial;
	int32 CumulusCloudsActive = -1; // -1 unknown, 0 engine clouds, 1 cumulus

	/** Sun disc billboard */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SunDiscMesh;

	/** Soft sun halo billboard */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SunHaloMesh;

	/** r513: sun glare - sharp diffraction spikes and soft wide rays (billboard), option "sunrays" */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SunRaysMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SunRaysMaterial;
	/** r513: lens flare ghosts from the bright sun (post-process), option "sunrays" */
	UPROPERTY()
	TObjectPtr<class UPostProcessComponent> SunFlarePass;
	bool bSunRaysApplied = false;

	/** Moon disc billboard */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MoonDiscMesh;

	/** Dynamic material for sun disc */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SunDiscMaterial;

	/** Dynamic material for sun halo */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SunHaloMaterial;

	/** Dynamic material for moon disc */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> MoonDiscMaterial;
	UPROPERTY()
	TObjectPtr<class UTexture2D> MoonPhotoTexture;

	/** Cached light zone entries for the current map */
	TArray<const FLightDbcEntry*> MapLights;

	/** Whether we have DBC-driven light data loaded */
	bool bHasDbcLights = false;
    bool bLightTablesBound = false;
    bool bSkyCapturePending = false;

	/** Whether LightFloatParams loaded */
	bool bHasFloatParams = false;

	uint32 CurrentWeatherType = 0;
	float CurrentWeatherGrade = 0.0f;

	// ── Light property indices in LightIntParams ──
	// Each Light.dbc paramID maps to 18 sequential LightIntParams rows
	enum ELightProperty : uint8
	{
		LP_GlobalAmbient = 0,
		LP_GlobalDiffuse = 1,
		LP_SkyTopColor = 2,
		LP_SkyMiddleColor = 3,
		LP_SkyBand1 = 4,
		LP_SkyBand2 = 5,
		LP_SkySmogColor = 6,
		LP_SkyFogColor = 7,
		LP_SunColor = 8,
		LP_SunHaloColor = 9,
		LP_MAX = 18
	};

	// Float param property indices (LightFloatParams rows per paramID)
	enum ELightFloatProperty : uint8
	{
		FP_FogDistance = 0,
		FP_FogMultiplier = 1,
		FP_CelestialGlowThrough = 2,
		FP_CloudDensity = 3,
		FP_MAX = 6
	};

	/** Interpolate a LightIntParams color band at the current time */
	FLinearColor InterpolateDbcColor(uint32 ParamID, ELightProperty Property) const;

	/** Interpolate a LightFloatParams value at the current time */
	float InterpolateDbcFloat(uint32 ParamID, ELightFloatProperty Property) const;

	/** Blend colors from all nearby light zones weighted by distance */
	FLinearColor BlendZoneColor(ELightProperty Property, const FVector& PlayerPos) const;

	/** Blend float params from all nearby light zones */
	float BlendZoneFloat(ELightFloatProperty Property, const FVector& PlayerPos) const;

	/** Update sun/moon position based on time */
	void UpdateSunPosition();

	/** Update lighting colors based on time */
	void UpdateLightColors();

	/** Update fog based on time */
	void UpdateFog();

	/** Update skydome gradient colors */
	void UpdateSkydome();

	/** Update cloud layer */
	void UpdateClouds();

	/** Create the skydome hemisphere mesh */
	void CreateSkydomeMesh();

	/** Create a UE engine sky sphere that visibly displays SkyAtmosphere */
	void CreateAtmosphereSkydomeMesh();

	/** Create the cloud plane mesh */
	void CreateCloudMesh();

	/** Create the native UE volumetric cloud layer */
	void CreateVolumetricCloudLayer();

	/** Create sun/moon disc billboards */
	void CreateCelestialDiscs();

	/** Update sun/moon disc positions and visibility */
	void UpdateCelestialDiscs();

	/** Fallback interpolation (used when DBC data not available) */
	FLinearColor GetSkyColorFallback() const;
	FLinearColor GetFogColorFallback() const;
	FLinearColor GetSunColorFallback() const;
	float GetSunIntensityForTime() const;
};
