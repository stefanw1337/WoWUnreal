#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Formats/M2Types.h"
#include "WowFixturePolicy.h"
#include "WowM2ParticleComponent.generated.h"

/** One short-lived element in the pooled, image-free flame volume. */
USTRUCT()
struct FProceduralFlameParticle
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Visual;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> Material;

    FVector StartOffset = FVector::ZeroVector;
    FVector Velocity = FVector::ZeroVector;
    float Age = -1.0f;
    float Lifetime = 0.0f;
    float BaseWidth = 0.0f;
    float BaseHeight = 0.0f;
    float ScaleMultiplier = 1.0f;
    float DriftPhase = 0.0f;
    int32 AtlasFrameOffset = 0;
    int32 CurrentAtlasFrame = INDEX_NONE;
    bool bCore = false;
};

/** Procedural fire effect: flickering point light + pooled animated flame elements. */
USTRUCT()
struct FFireEffect
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UPointLightComponent> Light;

    UPROPERTY()
    TArray<FProceduralFlameParticle> FlameParticles;

    /** All authored flame quads share one primitive and draw call. */
    UPROPERTY()
    TObjectPtr<class UProceduralMeshComponent> AuthoredFlameMesh;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> AuthoredFlameMaterial;

    TArray<FVector> MeshVertices;
    TArray<FVector> MeshNormals;
    TArray<FVector2D> MeshUVs;
    TArray<FLinearColor> MeshColors;

    float FlickerTime = 0.0f;
    float BaseIntensity = 1000.0f;
    FVector BasePosition = FVector::ZeroVector;
    float PhaseOffset = 0.0f;
    int32 TextureRows = 1;
    int32 TextureColumns = 1;
    int32 CurrentFrame = INDEX_NONE;
    bool bUsesAuthoredTexture = false;
    bool bEquipmentEffect = false;
    bool bCandleEffect = false;
    bool bTorchEffect = false;
    bool bSteadyWallTorch = false;
    bool bHearthEffect = false;
    float EmissionSpeedCm = 55.0f;
    float SpeedVariation = 0.0f;
    float VerticalRange = 0.0f;
    float HorizontalRange = 0.0f;
    float ZSourceCm = 0.0f;
    float Lifespan = 1.0f;
    float LifespanVariation = 0.0f;
    float EmissionAreaWidthCm = 0.0f;
    float EmissionAreaLengthCm = 0.0f;
    float MiddleTime = 0.5f;
    float ScaleStart = 0.01f;
    float ScaleMiddle = 0.28f;
    float ScaleEnd = 0.01f;
    float ScaleVariation = 0.0f;
    FLinearColor ColorStart = FLinearColor::White;
    FLinearColor ColorMiddle = FLinearColor::White;
    FLinearColor ColorEnd = FLinearColor::White;
    float AlphaStart = 1.0f;
    float AlphaMiddle = 1.0f;
    float AlphaEnd = 0.0f;
    int32 HeadCellStart = 0;
    int32 HeadCellMiddle = 0;
    int32 HeadCellEnd = 0;
    int32 MergedEmitterCount = 1;
    FRandomStream RandomStream;
    float LightScale = 1.0f;
    float TargetLightScale = 1.0f;
    float LightRetargetTime = 0.0f;
};

/** Simple smoke effect: fading billboard quad */
USTRUCT()
struct FSmokeEffect
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Billboard;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> Material;

    int32 AtlasRows=1, AtlasColumns=1;
    float FadeTime = 0.0f;
    float LifeSpan = 3.0f;
    FVector RiseVelocity = FVector(0, 0, 50); // Rise upward
    FVector BasePosition = FVector::ZeroVector;
};

/** Simple sparkle effect: small emissive billboard */
USTRUCT()
struct FSparkleEffect
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Billboard;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> Material;

    float PulseTime = 0.0f;
    float PulseRate = 2.0f; // Pulses per second
    FVector BasePosition = FVector::ZeroVector;
};

struct FWowRibbonEdge
{
    FVector Position = FVector::ZeroVector;
    float Age = 0.0f;
    /** The emitting bone's up axis when this edge was laid down (ribbon width direction). */
    FVector Up = FVector::ZeroVector;
};

USTRUCT()
struct FWowRibbonEffect
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> Mesh;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    FM2RibbonEmitter Emitter;
    TArray<FM2Bone> Bones;
    FM2AnimationData Animation;
    TArray<FWowRibbonEdge> Edges;
    FVector LastPosition = FVector::ZeroVector;
    FVector LastUp = FVector::UpVector;
    float Age = 0.0f;
    float EdgeAccumulator = 0.0f;
    bool bHasLastPosition = false;
    bool bAnimationLoops = false;
};

/** One simulated sprite of an authored M2 particle emitter. */
struct FWowEmitterParticle
{
    FVector Position = FVector::ZeroVector; // world space, or emitter space for local emitters
    FVector Velocity = FVector::ZeroVector;
    float Age = 0.0f;
    float Lifetime = 1.0f;
    float ScaleMultiplier = 1.0f;
    int32 RandomCell = 0;
};

/**
 * Full simulation of an authored M2 emitter (spells, creatures, magical doodads):
 * emission rate, lifespan, speed/spread, plane/sphere areas, gravity, 3-key colour,
 * alpha, scale and atlas tracks, head/tail sprites, world-space trails, bone following.
 * All sprites of one emitter are one procedural primitive (one draw call).
 */
USTRUCT()
struct FWowEmitterEffect
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> Mesh;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    FM2ParticleEmitter Emitter;
    TArray<FWowEmitterParticle> Particles;
    float Age = 0.0f;
    float SpawnAccumulator = 0.0f;
    bool bLocalSpace = false;
    bool bAdditive = true;
    /** Electric spell sparks (blue additive / lightning texture): crackling arcs from spark to spark. */
    bool bLightningArcs = false;
    /** WoW's own crackle texture (Spells\Zap1) swapped in for electric spell sparks, drawn as
     *  small rotating crackles instead of stretched droplets that read as water splashes. */
    UPROPERTY() TObjectPtr<class UTexture2D> CrackleTexture;
    bool bCrackle = false;
    FRandomStream Random;
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
};

/**
 * Runtime renderer for WoW M2 particle emitters. Authored flame sprites are
 * batched into one procedural primitive per emitter, while smoke/glow effects
 * use lightweight billboards and fire adds a non-shadowing local light.
 */
UCLASS(ClassGroup=(WowAssets), meta=(BlueprintSpawnableComponent))
class WOWASSETS_API UWowM2ParticleComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    static float GetNearbyFireHeat(UWorld* World, const FVector& Position, const AActor* IgnoreActor);
    void PrepareStandaloneMaterials();
    UWowM2ParticleComponent();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                             FActorComponentTickFunction* ThisTickFunction) override;

    /**
     * Initialize particle effects from M2 particle emitter data.
     * This will create appropriate UE components based on emitter type.
     */
    UFUNCTION(BlueprintCallable, Category = "WoW Particles")
    void InitializeFromM2Data(const TArray<uint8>& M2DataRaw);

    /** Native initialization function using parsed M2 data (not blueprintable) */
    void InitializeFromParsedM2Data(const void* M2Data,
        class FMpqManager* Mpq = nullptr, class FWowAssetCache* Cache = nullptr);

    /** Persistent spell auras (Lightning Shield orb, etc.): emitters and ribbons keep cycling
     *  their animation instead of stopping after one pass. Set before initialization. */
    bool bForceLoopAnimation = false;
    /** One-shot spell visuals (impacts, instant casts): play sequence 0 once, even when it is
     *  flagged looping, so a Smite ribbon strikes once instead of repeating while the actor lives. */
    bool bPlayOnce = false;
    /** Lightning-family spell model: its blue additive sparks become crackles. Set before initialization. */
    bool bElectricSpell = false;
    /** The additive emitter material (texture "EffectTexture", "Strength", "UseTextureAlpha"), for other effects that want the spell look. */
    static UMaterial* GetAdditiveEmitterMaterial() { return GetEmitterMaterial(true); }

    void SetFixturePath(const FString& Path)
    {
        if (FWowFixturePolicy::IsHearth(Path))
            ComponentTags.AddUnique(TEXT("WowHearthFixture"));
        if (FWowFixturePolicy::IsCandle(Path))
            ComponentTags.AddUnique(TEXT("WowCandleFixture"));
        if (Path.Contains(TEXT("torch")) && !FWowFixturePolicy::IsUnlit(Path))
            ComponentTags.AddUnique(TEXT("WowTorchFixture"));
        if (FWowFixturePolicy::IsManagedLamp(Path))
            ComponentTags.AddUnique(TEXT("WowManagedFixtureLight"));
        // r418: braziers burn at the bowl's rim and are lit by their shadowed doodad light.
        if (Path.Contains(TEXT("brazier")) && !FWowFixturePolicy::IsUnlit(Path))
            ComponentTags.AddUnique(TEXT("WowBrazierFixture"));
        if (FWowFixturePolicy::SuppressHaloEmitter(Path))
            ComponentTags.AddUnique(TEXT("WowLampGlassFixture"));
        if (FWowFixturePolicy::HasMagicalCore(Path))
            ComponentTags.AddUnique(TEXT("WowMagicalLampCore"));
        SetPortalKind(Path);
    }

    /** Creatures: emitters follow the live animated bones of this mesh. */
    void SetBoneSource(class USkinnedMeshComponent* Mesh) { BoneSource = Mesh; }
    /** Total simulated sprites alive in all emitters (diagnostics/tests). */
    int32 GetSimulatedParticleCount() const;

    /** Use the preview capture camera for equipment billboards outside the world view. */
    void SetBillboardView(USceneComponent* View) { BillboardView = View; }

    /**
     * Clear all particle effects
     */
    UFUNCTION(BlueprintCallable, Category = "WoW Particles")
    void ClearParticles();
    // Equipment effect geometry shares its owner's lifetime and visibility.
    void OwnEquipmentVisual(USceneComponent* Visual) { EquipmentVisuals.Add(Visual); }
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

    /** Short-lived, depth-tested Holy Light motes around the healed unit. */
    void CreateHolyLightVeil(float CharacterHeight);
    void CreateQuestSparkles(float Height);
    bool bLoopQuestSparkles = false;

protected:
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> EquipmentVisuals;
    TWeakObjectPtr<USceneComponent> BillboardView;

    /** Active particle effects */
    UPROPERTY()
    TArray<FFireEffect> FireEffects;

    UPROPERTY()
    TArray<FSmokeEffect> SmokeEffects;

    UPROPERTY()
    TArray<FSparkleEffect> SparkleEffects;

    UPROPERTY()
    TArray<FWowRibbonEffect> RibbonEffects;

    UPROPERTY()
    TArray<FWowEmitterEffect> EmitterEffects;

    /** r563: dungeon portals and difficulty. 0 none; 1 dungeon portal (blue/red); 2 raid portal (green);
     *  3 the server's difficulty portal shown on Normal only; 4 its skull version shown on Heroic only. */
    uint8 PortalKind = 0;
    int8 PortalShownHeroic = -1;
    TSharedPtr<FM2Data> PortalNormalData, PortalHeroicData;
    class FMpqManager* PortalMpq = nullptr;
    class FWowAssetCache* PortalCache = nullptr;
    bool bSwappingPortal = false;
    void SetPortalKind(const FString& Path);
    /** Returns false while the portal is hidden at the current difficulty. */
    bool UpdatePortalDifficulty();

    /** Skeleton/animation used to move simulated emitters with their bones (spell models). */
    TArray<FM2Bone> EmitterBones;
    FM2AnimationData EmitterAnimation;
    bool bEmitterAnimationLoops = true;
    float EmitterClock = 0.0f;
    /** Animated creature mesh whose Bone_### transforms drive the emitters (preferred). */
    TWeakObjectPtr<class USkinnedMeshComponent> BoneSource;

    /** Materials used for particle effects */
    UPROPERTY()
    TObjectPtr<UMaterial> BaseBillboardMaterial;

    /** Image-free radial emissive material used by pooled flame elements. */
    UPROPERTY()
    TObjectPtr<UMaterial> ProceduralFlameMaterial;

    /** Cached billboard mesh (quad) */
    UPROPERTY()
    TObjectPtr<UStaticMesh> BillboardMesh;

private:
    /**
     * Particle doodads can number in the hundreds in a single WMO.  Keep a
     * short warm-up so newly-created sprites become visible, then stop doing
     * per-frame transform/material work while none of this component's
     * billboards have been rendered recently.
     */
    float VisibilityWarmupSecondsRemaining = 1.5f;

    /** Billboard orientation is shared by the very small local effect. */
    FRotator CachedBillboardWorldRotation = FRotator::ZeroRotator;
    bool bHasCachedBillboardWorldRotation = false;

    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> HolyLightVeil;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HolyLightVeilMaterial;
    float VeilHeight = 180.0f;
    float VeilAge = 0.0f;
    void UpdateHolyLightVeil(float DeltaTime);

    /** Create a fire effect from M2 emitter data */
    void CreateFireEffect(const struct FM2ParticleEmitter& Emitter,
        const FVector& LocalPosition, class UTexture2D* Texture);

    /** Create a smoke effect from M2 emitter data */
    void CreateSmokeEffect(const struct FM2ParticleEmitter& Emitter,
        const FVector& LocalPosition, class UTexture2D* Texture);

    /** Create a sparkle effect from M2 emitter data */
    void CreateSparkleEffect(const struct FM2ParticleEmitter& Emitter,
        const FVector& LocalPosition, class UTexture2D* Texture);

    /** Update fire effect animation */
    void UpdateFireEffect(FFireEffect& Effect, float DeltaTime);

    /** Update smoke effect animation */
    void UpdateSmokeEffect(FSmokeEffect& Effect, float DeltaTime);

    /** Update sparkle effect animation */
    void UpdateSparkleEffect(FSparkleEffect& Effect, float DeltaTime);
    /** Create and update an authored M2 trail/ribbon. */
    void CreateRibbonEffect(const FM2RibbonEmitter& Emitter, const FM2Data& Data,
        class UTexture2D* Texture);
    void UpdateRibbonEffect(FWowRibbonEffect& Effect, float DeltaTime,
        const FVector& CameraLocationLocal);
    FVector SampleRibbonPosition(const FWowRibbonEffect& Effect, float TimeSeconds, FVector* OutUp = nullptr) const;


    /** Get or create the billboard mesh (simple quad) */
    UStaticMesh* GetBillboardMesh();

    /** Get or create base billboard material */
    UMaterial* GetBaseBillboardMaterial();

    /** Authored-emitter simulation (see FWowEmitterEffect). */
    bool ShouldSimulateEmitters() const;
    void CreateEmitterEffect(const struct FM2ParticleEmitter& Emitter, class UTexture2D* Texture);
    void UpdateEmitterEffect(FWowEmitterEffect& Effect, float DeltaTime, const FVector& CameraWorld,
        const FVector& CameraRight, const FVector& CameraUp);
    bool GetEmitterFrame(const FWowEmitterEffect& Effect, FTransform& OutFrame) const;
    static UMaterial* GetEmitterMaterial(bool bAdditive);

    /** Get or create the image-free flame-particle material. */
    UMaterial* GetProceduralFlameMaterial();

};
