#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "WowWeatherPolicy.h"
#include "WowWeatherComponent.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class AWowAudioManager;
class UPointLightComponent;
class ULocalFogVolumeComponent;
class UWowTumbleweedComponent;

UCLASS()
class WOWWORLD_API UWowWeatherComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UWowWeatherComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;
    void SetZone(uint32 ZoneId, const FString& Name);
    void SetOnline(bool Value);
    /** Back at the login/character screen (logout or disconnect): no rain, lens drops, puddles or thunder until a zone is entered again. */
    void ResetForGlueScreen();
    void ReceiveServer(uint32 State, float Intensity, bool bAbrupt);
    static UMaterialInterface* PrepareMaterial(UObject* Outer);
    static UMaterialInterface* PrepareGroundWaterMaterial(UObject* Outer);
    const FWowWeatherSample& GetWeather() const { return Current; }
    uint32 GetZone() const { return Zone; }
    float GetLightningFlash() const { return FlashAge<0.14f?1.f:FlashAge>0.22f&&FlashAge<0.42f?0.7f:0.f; }
    // Severe wind differentiates a blizzard from ordinary 100% snowfall.
    float GetVisualStrength() const { return VisualStrength; }
    float GetBlizzardStrength() const { return Current.Kind==EWowWeatherKind::Snow ? FMath::Clamp((Current.Wind-18.f)/38.f,0.f,1.f) : 0.f; }
    bool IsColdClimate() const { return ZoneClimate == EWowClimate::Cold; }
    float GetCameraExposure() const { return bSheltered ? 0.0f : Exposure; }
    /** Inside a building (roof and walls round the camera), not just under a tree or an arch (r234). */
    bool IsIndoors() const { return bIndoors; }
    /** r400: indoors, the share of the view that is outside (doorway ~0.4, deep hall 0); 1 outdoors. */
    float GetIndoorOpenness() const { return bIndoors ? IndoorOpenness : 1.f; }
    /** Depth (cm) of our rain puddle film at this world point, 0 when dry. Footsteps
     *  use it to splash through puddles. */
    static float GetPuddleDepthAt(const FVector& Location);
private:
    TArray<FVector4f> PuddlePoints; // XYZ + depth of the drawn film, rebuilt with the mesh
    void RefreshTarget();
    /** r468: the zone's own slow weather fronts (20-minute steps), before climate adaptation. */
    FWowWeatherSample ZoneFront() const;
    /** r477: self-lighting for the dust/mist fog volumes (0..~0.12), daylight-scaled. */
    float FogVolumeSelfLight() const;
    void RequestWeather();
    void UpdateParticles(float DeltaTime);
    void UpdateRainSplashes(float DeltaTime,const FVector& Camera,const FRotator& Rotation,bool bRainVisible);
    struct FRainSplash { FVector Position=FVector::ZeroVector,Normal=FVector::UpVector; float Age=10,Life=.4f,Size=5; bool bWater=false; };
    TArray<FRainSplash> Splashes;
    TArray<FVector> SplashVertices,SplashNormals;
    TArray<FVector2D> SplashUV;
    TArray<FLinearColor> SplashColors;
    FRandomStream SplashRandom{298147};
    float SplashBudget=0;
    double NextSplashWaterSearch=0;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> SplashWater;
    float SurfaceWetness=0.f;
    void UpdateGroundWater(float Dt, float Rain);
    struct FGroundWaterPatch
    {
        FIntPoint Key;
        FVector Origin=FVector::ZeroVector;
        TArray<FVector> Points;
        TArray<float> Spill;
        TArray<float> BasinBottom;
        TArray<uint8> Valid;
        TArray<TWeakObjectPtr<UPrimitiveComponent>> Surfaces;
        TArray<FVector> PreviousPoints;
        TArray<float> PreviousSpill, PreviousBottom;
        TArray<uint8> PreviousValid;
        int32 Sample=0;
        double ShownAt=-1; // first time this patch had water to draw: fades in from here
        bool Ready=false;
        double RetryAfter=0;
    };
    TArray<FGroundWaterPatch> GroundWaterPatches;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> GroundWaterMesh;
    TArray<int32> GroundWaterIndices;
    float GroundWaterDrawTime=0;
    double NextGroundWaterSearch=0, NextGroundWaterDiagnostic=0;
    int32 NextSplash=0;
    bool bThunderstormActive=false;
    void UpdateThunder(float DeltaTime);
    void UpdateStormGusts(float DeltaTime);
    void UpdateWaterMist(float DeltaTime);
    void UpdateAmbientAtmosphere(float DeltaTime);
    struct FAmbientPatch
    {
        FIntPoint Cell = FIntPoint(MAX_int32,MAX_int32);
        FVector Anchor = FVector::ZeroVector;
        bool Valid = false;
        float Fade = 0.f;
    };
    UPROPERTY() TArray<TObjectPtr<ULocalFogVolumeComponent>> AmbientMist;
    TArray<FAmbientPatch> AmbientPatches;
    double NextAmbientProbe = 0;
    int32 AmbientProbe = 0;
    UPROPERTY() TArray<TObjectPtr<ULocalFogVolumeComponent>> WaterMist;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> MistWater;
    double NextMistSearch=0, NextMistDiagnostic=0;
    FVector MistFocus=FVector::ZeroVector;
    FVector2D MistWindOffset=FVector2D::ZeroVector;
    UPROPERTY() TArray<TObjectPtr<ULocalFogVolumeComponent>> StormGusts;
    float VisualStrength=1.f, TargetVisualStrength=1.f;
    void CreateLightning(const FVector& Camera, const FVector& CameraRight);
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> LightningMesh;
    UPROPERTY() TObjectPtr<UPointLightComponent> LightningLight;
    /** Additive material carrying the Lightning Bolt spell's own texture (resolved from the data). */
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> LightningBoltMaterial;
    bool bResolvedBoltTexture = false;
    double NextLightning = 0, ThunderAt = 0;
    float FlashAge = 10, ThunderStrength = 0;
    FRandomStream StormRandom{91371};
    FVector LightningPosition = FVector::ZeroVector;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
    UPROPERTY() TObjectPtr<UWowTumbleweedComponent> Tumbleweeds;
    TWeakObjectPtr<AWowAudioManager> Audio;
    TSharedPtr<IHttpRequest,ESPMode::ThreadSafe> Request;
    FWowWeatherSample Current, Target, Server, Real;
    EWowClimate ZoneClimate = EWowClimate::Temperate;
    uint32 Zone = 0;
    int32 Source = 0;
    bool bOnline = false, bHaveServer = false, bConfigured = false, bFixture = false;
    double Latitude = 0, Longitude = 0, NextRequest = 0, NextTarget = 0, NextAudio = 0, NextShelter = 0;
    int64 RealUtc = 0;
    float Exposure = 0;
    /** r395: flakes/drops only (indoors they keep falling outside the door; Exposure fades the fog). */
    float ParticleExposure = 0;
    float IndoorOpenness = 1.f;
    double NextDiagnostic=0;
    bool bSheltered = false;
    bool bIndoors = false;
    FRandomStream Random{63817};
    TArray<FVector> Drops, Vertices, Normals;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<int32> Triangles;
    struct FSurfaceCell { FVector Position=FVector::ZeroVector; float Height=0; bool Valid=false; };
    TArray<FSurfaceCell> Surfaces;
    int32 SurfaceIndex = 0;
    FVector LastCamera=FVector::ZeroVector;
    bool bHaveCamera=false;
    /** r491: when the camera last jumped far (login, teleport, instance): the weather snaps to its target for a while. */
    double LastJumpAt=-1000.0;
    void SnapToTarget(const TCHAR* Why);
};
