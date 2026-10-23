#include "WowM2ParticleComponent.h"
#include "WowActorFadeComponent.h"
#include "UObject/UObjectIterator.h"
#include "Engine/World.h"
#include "WowCookedMaterial.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"
#include "ProceduralMeshComponent.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Formats/M2Types.h"
#include "Formats/M2Parser.h"
#include "WowDifficultyState.h"
#include "Formats/BlpParser.h"
#include "Formats/BlpTypes.h"
#include "Mpq/MpqManager.h"
#include "WowAssetCache.h"
#include "WowTextureFactory.h"
#include "Coord/WowCoordinate.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "AnimationRuntime.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "UObject/StrongObjectPtr.h"
#include "ShaderCompiler.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Particles/ParticleSpriteEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/Lifetime/ParticleModuleLifetime.h"
#include "Particles/Location/ParticleModuleLocation.h"
#include "Particles/Velocity/ParticleModuleVelocity.h"
#include "Particles/Size/ParticleModuleSize.h"
#include "Particles/Size/ParticleModuleSizeScale.h"
#include "Particles/Color/ParticleModuleColorOverLife.h"
#include "Distributions/DistributionFloatConstant.h"
#include "Distributions/DistributionFloatConstantCurve.h"
#include "Distributions/DistributionFloatUniform.h"
#include "Distributions/DistributionVectorConstantCurve.h"
#include "Distributions/DistributionVectorUniform.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowParticles, Log, All);

namespace
{
FVector M2ParticlePositionToUELocal(const FVector& Position)
{
    return FVector(Position.Y, Position.X, Position.Z) * FWowCoordinate::SCALE;
}

UTexture2D* LoadEffectTextureByPath(const FString& TexturePath, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    if (!Mpq || TexturePath.IsEmpty()) return nullptr;
    if (Cache) if (UTexture2D* Cached = Cache->FindTexture(TexturePath)) return Cached;
    TArray<uint8> RawData;
    if (!Mpq->ReadFile(TexturePath, RawData)) return nullptr;
    const FBlpTexture Blp = FBlpParser::Parse(RawData);
    if (!Blp.bIsValid) return nullptr;
    UTexture2D* Texture = FWowTextureFactory::CreateTexture(Blp, TexturePath);
    if (Texture && Cache) Cache->CacheTexture(TexturePath, Texture);
    return Texture;
}

UTexture2D* LoadM2EffectTexture(const FM2Data& M2Data, uint16 TextureIndex,
    FMpqManager* Mpq, FWowAssetCache* Cache)
{
    if (!Mpq || !M2Data.TexturePaths.IsValidIndex(TextureIndex))
    {
        return nullptr;
    }

    const FString& TexturePath = M2Data.TexturePaths[TextureIndex];
    if (TexturePath.IsEmpty())
    {
        return nullptr;
    }
    if (Cache)
    {
        if (UTexture2D* Cached = Cache->FindTexture(TexturePath))
        {
            return Cached;
        }
    }

    TArray<uint8> RawData;
    if (!Mpq->ReadFile(TexturePath, RawData))
    {
        return nullptr;
    }
    const FBlpTexture Blp = FBlpParser::Parse(RawData);
    if (!Blp.bIsValid)
    {
        return nullptr;
    }

    UTexture2D* Texture = FWowTextureFactory::CreateTexture(Blp, TexturePath);
    if (Texture && Cache)
    {
        Cache->CacheTexture(TexturePath, Texture);
    }
    return Texture;
}

enum class EWowParticleVisualKind : uint8
{
    Fire,
    Smoke,
    Sparkle
};

EWowParticleVisualKind ClassifyParticleVisual(
    const FM2ParticleEmitter& Emitter, const FString& TexturePath)
{
    // The authored texture family remains the clearest distinction between
    // flame, smoke and decorative glow emitters.  The parser also exposes the
    // M2 lifetime tracks, which are applied by the individual render paths.
    const FString LowerPath = FWowFixturePolicy::Leaf(TexturePath).ToLower();
    if (LowerPath.Contains(TEXT("flame")) ||
        LowerPath.Contains(TEXT("fire")) ||
        LowerPath.Contains(TEXT("ember")) ||
        LowerPath.Contains(TEXT("burn")) ||
        LowerPath.Contains(TEXT("torch")))
    {
        return EWowParticleVisualKind::Fire;
    }
    if (LowerPath.Contains(TEXT("smoke")) ||
        LowerPath.Contains(TEXT("mist")) ||
        LowerPath.Contains(TEXT("steam")) ||
        LowerPath.Contains(TEXT("fog")) ||
        LowerPath.Contains(TEXT("cloud")))
    {
        return EWowParticleVisualKind::Smoke;
    }
    if (!LowerPath.IsEmpty())
    {
        // Glow, sparkle and other authored non-fire atlases should keep their
        // own art instead of inheriting the parser's temporary orange values.
        return EWowParticleVisualKind::Sparkle;
    }

    if (Emitter.IsSmokeEmitter())
    {
        return EWowParticleVisualKind::Smoke;
    }
    if (Emitter.IsFireEmitter())
    {
        return EWowParticleVisualKind::Fire;
    }
    return EWowParticleVisualKind::Sparkle;
}

void SetVectorCurveKey(UDistributionVectorConstantCurve* Curve, float Time, const FVector& Value)
{
    if (!Curve)
    {
        return;
    }
    const int32 Key = Curve->CreateNewKey(Time);
    Curve->SetKeyOut(0, Key, Value.X);
    Curve->SetKeyOut(1, Key, Value.Y);
    Curve->SetKeyOut(2, Key, Value.Z);
    Curve->SetKeyInterpMode(Key, CIM_CurveAuto);
}

void SetFloatCurveKey(UDistributionFloatConstantCurve* Curve, float Time, float Value)
{
    if (!Curve)
    {
        return;
    }
    const int32 Key = Curve->CreateNewKey(Time);
    Curve->SetKeyOut(0, Key, Value);
    Curve->SetKeyInterpMode(Key, CIM_CurveAuto);
}

void ConfigureProceduralFireEmitter(UParticleSpriteEmitter* Emitter, bool bCore)
{
    if (!Emitter)
    {
        return;
    }

    Emitter->CreateLODLevel(0);
    Emitter->SetToSensibleDefaults();
    UParticleLODLevel* Lod = Emitter->GetLODLevel(0);
    if (!Lod || !Lod->RequiredModule || !Lod->SpawnModule)
    {
        return;
    }

    Lod->RequiredModule->bUseLocalSpace = true;
    Lod->RequiredModule->EmitterLoops = 0;
    Lod->RequiredModule->EmitterDuration = 1.0f;
    Lod->RequiredModule->ScreenAlignment = PSA_Velocity;
    Lod->RequiredModule->Material = LoadObject<UMaterialInterface>(
        nullptr, TEXT("/Engine/EngineMaterials/DefaultParticle.DefaultParticle"));

    if (UDistributionFloatConstant* SpawnRate =
        Cast<UDistributionFloatConstant>(Lod->SpawnModule->Rate.Distribution))
    {
        SpawnRate->Constant = bCore ? 42.0f : 54.0f;
        SpawnRate->bIsDirty = true;
    }

    UParticleModuleLifetime* Lifetime = nullptr;
    UParticleModuleSize* InitialSize = nullptr;
    UParticleModuleVelocity* Velocity = nullptr;
    UParticleModuleColorOverLife* Color = nullptr;
    for (UParticleModule* Module : Lod->Modules)
    {
        Lifetime = Lifetime ? Lifetime : Cast<UParticleModuleLifetime>(Module);
        InitialSize = InitialSize ? InitialSize : Cast<UParticleModuleSize>(Module);
        Velocity = Velocity ? Velocity : Cast<UParticleModuleVelocity>(Module);
        Color = Color ? Color : Cast<UParticleModuleColorOverLife>(Module);
    }

    if (UDistributionFloatUniform* LifetimeRange = Lifetime
        ? Cast<UDistributionFloatUniform>(Lifetime->Lifetime.Distribution) : nullptr)
    {
        LifetimeRange->Min = bCore ? 0.24f : 0.38f;
        LifetimeRange->Max = bCore ? 0.56f : 0.88f;
        LifetimeRange->bIsDirty = true;
    }
    if (UDistributionVectorUniform* SizeRange = InitialSize
        ? Cast<UDistributionVectorUniform>(InitialSize->StartSize.Distribution) : nullptr)
    {
        SizeRange->Min = bCore ? FVector(19.0f, 34.0f, 19.0f) : FVector(30.0f, 48.0f, 30.0f);
        SizeRange->Max = bCore ? FVector(31.0f, 56.0f, 31.0f) : FVector(48.0f, 76.0f, 48.0f);
        SizeRange->bIsDirty = true;
    }
    if (UDistributionVectorUniform* VelocityRange = Velocity
        ? Cast<UDistributionVectorUniform>(Velocity->StartVelocity.Distribution) : nullptr)
    {
        VelocityRange->Min = bCore ? FVector(-14.0f, -14.0f, 105.0f) : FVector(-22.0f, -22.0f, 72.0f);
        VelocityRange->Max = bCore ? FVector(14.0f, 14.0f, 185.0f) : FVector(22.0f, 22.0f, 142.0f);
        VelocityRange->bIsDirty = true;
    }

    UParticleModuleLocation* Location = NewObject<UParticleModuleLocation>(Emitter);
    if (UDistributionVectorUniform* LocationRange =
        Cast<UDistributionVectorUniform>(Location->StartLocation.Distribution))
    {
        const float Radius = bCore ? 8.0f : 13.0f;
        LocationRange->Min = FVector(-Radius, -Radius, 0.0f);
        LocationRange->Max = FVector(Radius, Radius, bCore ? 8.0f : 12.0f);
        LocationRange->bIsDirty = true;
    }
    Location->LODValidity = 1;
    Lod->Modules.Add(Location);

    UParticleModuleSizeScale* SizeOverLife = NewObject<UParticleModuleSizeScale>(Emitter);
    SizeOverLife->SizeScale.Distribution = NewObject<UDistributionVectorConstantCurve>(SizeOverLife);
    if (UDistributionVectorConstantCurve* SizeCurve =
        Cast<UDistributionVectorConstantCurve>(SizeOverLife->SizeScale.Distribution))
    {
        SetVectorCurveKey(SizeCurve, 0.00f, FVector(0.28f, 0.36f, 1.0f));
        SetVectorCurveKey(SizeCurve, 0.18f, FVector(1.00f, 1.00f, 1.0f));
        SetVectorCurveKey(SizeCurve, 0.66f, FVector(0.68f, 0.82f, 1.0f));
        SetVectorCurveKey(SizeCurve, 1.00f, FVector(0.08f, 0.18f, 1.0f));
        SizeCurve->bIsDirty = true;
    }
    SizeOverLife->LODValidity = 1;
    Lod->Modules.Add(SizeOverLife);

    if (Color)
    {
        Color->ColorOverLife.Distribution = NewObject<UDistributionVectorConstantCurve>(Color);
        if (UDistributionVectorConstantCurve* ColorCurve =
            Cast<UDistributionVectorConstantCurve>(Color->ColorOverLife.Distribution))
        {
            SetVectorCurveKey(ColorCurve, 0.00f,
                bCore ? FVector(1.0f, 0.96f, 0.58f) : FVector(1.0f, 0.52f, 0.035f));
            SetVectorCurveKey(ColorCurve, 0.48f,
                bCore ? FVector(1.0f, 0.48f, 0.04f) : FVector(1.0f, 0.18f, 0.006f));
            SetVectorCurveKey(ColorCurve, 1.00f,
                bCore ? FVector(0.95f, 0.12f, 0.005f) : FVector(0.30f, 0.012f, 0.0f));
            ColorCurve->bIsDirty = true;
        }
        Color->AlphaOverLife.Distribution = NewObject<UDistributionFloatConstantCurve>(Color);
        if (UDistributionFloatConstantCurve* AlphaCurve =
            Cast<UDistributionFloatConstantCurve>(Color->AlphaOverLife.Distribution))
        {
            SetFloatCurveKey(AlphaCurve, 0.00f, 0.0f);
            SetFloatCurveKey(AlphaCurve, 0.07f, bCore ? 0.94f : 0.82f);
            SetFloatCurveKey(AlphaCurve, 0.68f, bCore ? 0.78f : 0.62f);
            SetFloatCurveKey(AlphaCurve, 1.00f, 0.0f);
            AlphaCurve->bIsDirty = true;
        }
        Color->bClampAlpha = true;
    }

    Lod->UpdateModuleLists();
    Emitter->UpdateModuleLists();
    Emitter->CalculateMaxActiveParticleCount();
}

UParticleSystem* CreateProceduralFireSystem(UObject* Outer)
{
    UParticleSystem* System = NewObject<UParticleSystem>(Outer);
    if (!System)
    {
        return nullptr;
    }

    for (const bool bCore : {false, true})
    {
        UParticleSpriteEmitter* Emitter = NewObject<UParticleSpriteEmitter>(System);
        ConfigureProceduralFireEmitter(Emitter, bCore);
        System->Emitters.Add(Emitter);
    }
    System->bUseFixedRelativeBoundingBox = true;
    System->FixedRelativeBoundingBox =
        FBox(FVector(-90.0f, -90.0f, -20.0f), FVector(90.0f, 90.0f, 260.0f));
    System->LODDistances.Add(0.0f);
    System->LODSettings.Add(FParticleSystemLOD::CreateParticleSystemLOD());
    #if WITH_EDITOR
    System->UpdateAllModuleLists();
#endif
    System->CalculateMaxActiveParticleCounts();
    return System;
}

void RespawnFlameParticle(FFireEffect& Effect, FProceduralFlameParticle& Particle)
{
    Particle.Age = 0.0f;
    if (Effect.bUsesAuthoredTexture)
    {
        Particle.Lifetime = FMath::Max(0.05f,
            Effect.Lifespan + Effect.RandomStream.FRandRange(
                -Effect.LifespanVariation, Effect.LifespanVariation));
        Particle.StartOffset = FVector(
            Effect.RandomStream.FRandRange(
                -Effect.EmissionAreaLengthCm * 0.24f, Effect.EmissionAreaLengthCm * 0.24f),
            Effect.RandomStream.FRandRange(
                -Effect.EmissionAreaWidthCm * 0.24f, Effect.EmissionAreaWidthCm * 0.24f),
            Effect.ZSourceCm);

        const float Speed = Effect.EmissionSpeedCm * (1.0f +
            Effect.RandomStream.FRandRange(-Effect.SpeedVariation, Effect.SpeedVariation));
        const float Tilt = Effect.RandomStream.FRandRange(
            -Effect.VerticalRange, Effect.VerticalRange);
        const float Azimuth = Effect.HorizontalRange > KINDA_SMALL_NUMBER
            ? Effect.RandomStream.FRandRange(0.0f, Effect.HorizontalRange)
            : 0.0f;
        const float HorizontalSpeed = FMath::Sin(Tilt) * Speed;
        Particle.Velocity = FVector(
            FMath::Cos(Azimuth) * HorizontalSpeed,
            FMath::Sin(Azimuth) * HorizontalSpeed,
            FMath::Cos(Tilt) * Speed);
        Particle.ScaleMultiplier = FMath::Max(0.1f, 1.0f +
            Effect.RandomStream.FRandRange(-Effect.ScaleVariation, Effect.ScaleVariation));
        Particle.DriftPhase = Effect.RandomStream.FRandRange(0.0f, 2.0f * PI);
        Particle.CurrentAtlasFrame = INDEX_NONE;
        if (!Effect.bEquipmentEffect && !Effect.bCandleEffect)
        {
            // Fires must not visibly repeat: vary each new flame's life, size and the
            // atlas frame it starts on, so the pooled sprites never fall into one short cycle.
            Particle.Lifetime *= Effect.RandomStream.FRandRange(0.75f, 1.35f);
            Particle.ScaleMultiplier *= Effect.RandomStream.FRandRange(0.82f, 1.18f);
            Particle.AtlasFrameOffset = Effect.RandomStream.RandRange(0, FMath::Max(0, Effect.TextureRows * Effect.TextureColumns - 1));
        }
        return;
    }

    Particle.Lifetime = Particle.bCore
        ? Effect.RandomStream.FRandRange(0.30f, 0.58f)
        : Effect.RandomStream.FRandRange(0.48f, 0.92f);
    const float Radius = Particle.bCore ? 8.0f : 15.0f;
    Particle.StartOffset = FVector(
        Effect.RandomStream.FRandRange(-Radius, Radius),
        Effect.RandomStream.FRandRange(-Radius, Radius),
        Effect.RandomStream.FRandRange(0.0f, Particle.bCore ? 8.0f : 13.0f));
    Particle.Velocity = FVector(
        Effect.RandomStream.FRandRange(-21.0f, 21.0f),
        Effect.RandomStream.FRandRange(-12.0f, 12.0f),
        Particle.bCore
            ? Effect.RandomStream.FRandRange(110.0f, 188.0f)
            : Effect.RandomStream.FRandRange(78.0f, 148.0f));
    Particle.BaseWidth = Particle.bCore
        ? Effect.RandomStream.FRandRange(0.20f, 0.34f)
        : Effect.RandomStream.FRandRange(0.34f, 0.54f);
    Particle.BaseHeight = Particle.bCore
        ? Effect.RandomStream.FRandRange(0.40f, 0.64f)
        : Effect.RandomStream.FRandRange(0.58f, 0.88f);
    Particle.DriftPhase = Effect.RandomStream.FRandRange(0.0f, 2.0f * PI);
    if (Effect.bCandleEffect)
    {
        Particle.StartOffset*=.05f; Particle.StartOffset.Z+=2.f;
        Particle.Velocity*=.025f; Particle.BaseWidth*=.22f; Particle.BaseHeight*=.22f;
    }
    Particle.AtlasFrameOffset = Effect.RandomStream.RandRange(
        0, FMath::Max(0, Effect.TextureRows * Effect.TextureColumns - 1));
    Particle.CurrentAtlasFrame = INDEX_NONE;
    if (Particle.Visual)
    {
        Particle.Visual->SetVisibility(true);
    }
}

template<typename TValue>
TValue SampleParticleLifetime(
    const TValue& Start, const TValue& Middle, const TValue& End,
    float MiddleTime, float NormalizedAge)
{
    const float SafeMiddle = FMath::Clamp(MiddleTime, 0.01f, 0.99f);
    if (NormalizedAge <= SafeMiddle)
    {
        return FMath::Lerp(Start, Middle, NormalizedAge / SafeMiddle);
    }
    return FMath::Lerp(Middle, End,
        (NormalizedAge - SafeMiddle) / (1.0f - SafeMiddle));
}

void UpdateAuthoredFlameGeometry(FFireEffect& Effect)
{
    if (!Effect.AuthoredFlameMesh || Effect.FlameParticles.IsEmpty())
    {
        return;
    }

    const int32 ParticleCount = Effect.FlameParticles.Num();
    Effect.MeshVertices.SetNumUninitialized(ParticleCount * 4);
    Effect.MeshNormals.SetNumUninitialized(ParticleCount * 4);
    Effect.MeshUVs.SetNumUninitialized(ParticleCount * 4);
    Effect.MeshColors.SetNumUninitialized(ParticleCount * 4);
    const int32 FrameCount = FMath::Max(1, Effect.TextureRows * Effect.TextureColumns);

    for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
    {
        const FProceduralFlameParticle& Particle = Effect.FlameParticles[ParticleIndex];
        const int32 VertexBase = ParticleIndex * 4;
        const bool bAlive = Particle.Age >= 0.0f && Particle.Lifetime > 0.0f;
        const float T = bAlive
            ? FMath::Clamp(Particle.Age / Particle.Lifetime, 0.0f, 1.0f)
            : 0.0f;

        // The fixture-specific torch adjustment must never enlarge equipment
        // smoke, enchants or skull glows. Use their authored size in world units.
        const float WotlkFlameVisualScale = (Effect.bEquipmentEffect || Effect.bCandleEffect) ? 1.0f
            : Effect.bHearthEffect ? 3.2f : 4.8f;
        float AuthoredScale = SampleParticleLifetime(
            Effect.ScaleStart, Effect.ScaleMiddle, Effect.ScaleEnd,
            Effect.MiddleTime, T) * Particle.ScaleMultiplier * FWowCoordinate::SCALE
            * WotlkFlameVisualScale;
        if (Effect.bSteadyWallTorch)
        {
            // Keep the flame rooted and vary its silhouette independently of its fade.
            const float Shape=.92f+.06f*FMath::Sin(Effect.FlickerTime*2.13f+Particle.DriftPhase)
                +.04f*FMath::Sin(Effect.FlickerTime*3.71f+Particle.DriftPhase*1.7f);
            AuthoredScale=FMath::Max(Effect.ScaleStart,Effect.ScaleMiddle)*Particle.ScaleMultiplier
                *FWowCoordinate::SCALE*WotlkFlameVisualScale*Shape;
        }
        if (!Effect.bEquipmentEffect && !Effect.bCandleEffect)
        {
            // Slow, non-repeating "breathing" of the whole fire (incommensurate periods).
            AuthoredScale *= 1.0f + 0.07f * FMath::Sin(Effect.FlickerTime * 0.37f + Effect.PhaseOffset)
                + 0.05f * FMath::Sin(Effect.FlickerTime * 1.13f + Effect.PhaseOffset * 2.3f)
                + 0.03f * FMath::Sin(Effect.FlickerTime * 2.71f + Particle.DriftPhase);
        }
        const float HalfSize = bAlive ? FMath::Max(AuthoredScale * 0.5f, 0.01f) : 0.01f;
        const float Drift = (Effect.bEquipmentEffect || Effect.bCandleEffect) ? 0.0f : FMath::Sin(T * 7.0f + Particle.DriftPhase) * 1.8f;
        const FVector ParticlePosition = Particle.StartOffset + Particle.Velocity * Particle.Age;
        const float CentreX = ParticlePosition.X + Drift;
        // Campfire flames sit in the logs: the card's soft lower third overlaps the pile.
        const float CentreY = ParticlePosition.Z + (Effect.bHearthEffect ? HalfSize * 0.3f : FWowFixturePolicy::FlameCentreLift(
            HalfSize,Effect.bCandleEffect,Effect.bTorchEffect,Effect.bEquipmentEffect));
        const float Depth = ParticlePosition.Y * (Effect.bEquipmentEffect ? 1.0f : 0.2f);

        Effect.MeshVertices[VertexBase + 0] = FVector(CentreX - HalfSize, CentreY - HalfSize, Depth);
        Effect.MeshVertices[VertexBase + 1] = FVector(CentreX + HalfSize, CentreY - HalfSize, Depth);
        Effect.MeshVertices[VertexBase + 2] = FVector(CentreX + HalfSize, CentreY + HalfSize, Depth);
        Effect.MeshVertices[VertexBase + 3] = FVector(CentreX - HalfSize, CentreY + HalfSize, Depth);

        int32 Frame = 0;
        if (Effect.HeadCellStart == 0 && Effect.HeadCellMiddle == 0 &&
            Effect.HeadCellEnd == 0 && FrameCount > 1)
        {
            Frame = (FMath::Min(FMath::FloorToInt(T * FrameCount), FrameCount - 1) + Particle.AtlasFrameOffset) % FrameCount;
        }
        else
        {
            Frame = FMath::RoundToInt(SampleParticleLifetime(
                static_cast<float>(Effect.HeadCellStart),
                static_cast<float>(Effect.HeadCellMiddle),
                static_cast<float>(Effect.HeadCellEnd), Effect.MiddleTime, T));
            Frame = FMath::Clamp(Frame, 0, FrameCount - 1);
        }

        const int32 Column = Frame % Effect.TextureColumns;
        const int32 Row = Frame / Effect.TextureColumns;
        const float U0 = static_cast<float>(Column) / static_cast<float>(Effect.TextureColumns);
        const float V0 = static_cast<float>(Row) / static_cast<float>(Effect.TextureRows);
        const float U1 = static_cast<float>(Column + 1) / static_cast<float>(Effect.TextureColumns);
        const float V1 = static_cast<float>(Row + 1) / static_cast<float>(Effect.TextureRows);
        Effect.MeshUVs[VertexBase + 0] = FVector2D(U0, V1);
        Effect.MeshUVs[VertexBase + 1] = FVector2D(U1, V1);
        Effect.MeshUVs[VertexBase + 2] = FVector2D(U1, V0);
        Effect.MeshUVs[VertexBase + 3] = FVector2D(U0, V0);

        FLinearColor ParticleColor = SampleParticleLifetime(
            Effect.ColorStart, Effect.ColorMiddle, Effect.ColorEnd,
            Effect.MiddleTime, T);
        ParticleColor.A = bAlive ? SampleParticleLifetime(
            Effect.AlphaStart, Effect.AlphaMiddle, Effect.AlphaEnd,
            Effect.MiddleTime, T) : 0.0f;
        if (Effect.bSteadyWallTorch)
        {
            const float FadeIn=FMath::Clamp(T/.22f,0.f,1.f);
            const float FadeOut=FMath::Clamp((1.f-T)/.30f,0.f,1.f);
            ParticleColor.A=bAlive ? FMath::Max(Effect.AlphaStart,Effect.AlphaMiddle)
                *FMath::SmoothStep(0.f,1.f,FadeIn)*FMath::SmoothStep(0.f,1.f,FadeOut) : 0.f;
        }
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            Effect.MeshNormals[VertexBase + Corner] = FVector::UpVector;
            Effect.MeshColors[VertexBase + Corner] = ParticleColor;
        }
    }

    Effect.AuthoredFlameMesh->UpdateMeshSection_LinearColor(
        0, Effect.MeshVertices, Effect.MeshNormals, Effect.MeshUVs,
        Effect.MeshColors, TArray<FProcMeshTangent>(), false);
}
}

UWowM2ParticleComponent::UWowM2ParticleComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    // These are ambient effects, not gameplay simulation.  A 30 Hz update is
    // visually smooth while avoiding hundreds of component ticks at 60+ FPS.
    PrimaryComponentTick.TickInterval = 1.0f / 30.0f;
}

void UWowM2ParticleComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UWowM2ParticleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearParticles();
    Super::EndPlay(EndPlayReason);
}

void UWowM2ParticleComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                          FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (PortalKind != 0 && !UpdatePortalDifficulty()) return;

    VisibilityWarmupSecondsRemaining = FMath::Max(
        0.0f, VisibilityWarmupSecondsRemaining - DeltaTime);

    // A WMO can contain hundreds of fixtures. Previously every hidden flame
    // updated dozens of primitive transforms every frame, even behind walls or
    // outside the camera frustum. Let a frozen billboard render once when it
    // enters view; WasRecentlyRendered then wakes its animation on the next
    // update.
    bool bShouldAnimate = HolyLightVeil != nullptr || !RibbonEffects.IsEmpty() || !EmitterEffects.IsEmpty() ||
        VisibilityWarmupSecondsRemaining > 0.0f;
    auto IsRecentlyVisible = [](const UPrimitiveComponent* Billboard)
    {
        return Billboard && Billboard->IsVisible()
            && Billboard->WasRecentlyRendered(0.5f);
    };
    if (HolyLightVeil) UpdateHolyLightVeil(DeltaTime);

    if (!bShouldAnimate)
    {
        for (const FFireEffect& Effect : FireEffects)
        {
            if (IsRecentlyVisible(Effect.AuthoredFlameMesh))
            {
                bShouldAnimate = true;
                break;
            }
            for (const FProceduralFlameParticle& Particle : Effect.FlameParticles)
            {
                if (IsRecentlyVisible(Particle.Visual))
                {
                    bShouldAnimate = true;
                    break;
                }
            }
            if (bShouldAnimate)
            {
                break;
            }
        }
        for (const FSmokeEffect& Effect : SmokeEffects)
        {
            bShouldAnimate |= IsRecentlyVisible(Effect.Billboard);
        }
        for (const FSparkleEffect& Effect : SparkleEffects)
        {
            bShouldAnimate |= IsRecentlyVisible(Effect.Billboard);
        }
    }

    if (!bShouldAnimate)
    {
        return;
    }

    // Update fire effects
    for (FFireEffect& Effect : FireEffects)
    {
        UpdateFireEffect(Effect, DeltaTime);
    }

    // Update smoke effects
    for (FSmokeEffect& Effect : SmokeEffects)
    {
        UpdateSmokeEffect(Effect, DeltaTime);
    }

    // Update sparkle effects
    for (FSparkleEffect& Effect : SparkleEffects)
    {
        UpdateSparkleEffect(Effect, DeltaTime);
    }

    APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
    if (!EmitterEffects.IsEmpty())
    {
        EmitterClock += DeltaTime;
        FVector CameraWorld = GetComponentLocation() + FVector(300.0f, 0.0f, 100.0f);
        FRotator CameraRotation = (GetComponentLocation() - CameraWorld).Rotation();
        if (BillboardView.IsValid())
        {
            CameraWorld = BillboardView->GetComponentLocation();
            CameraRotation = BillboardView->GetComponentRotation();
        }
        else if (CameraManager)
        {
            CameraWorld = CameraManager->GetCameraLocation();
            CameraRotation = CameraManager->GetCameraRotation();
        }
        const FRotationMatrix CameraAxes(CameraRotation);
        const FVector CameraRight = CameraAxes.GetUnitAxis(EAxis::Y);
        const FVector CameraUp = CameraAxes.GetUnitAxis(EAxis::Z);
        for (FWowEmitterEffect& Effect : EmitterEffects)
            UpdateEmitterEffect(Effect, FMath::Min(DeltaTime, 0.1f), CameraWorld, CameraRight, CameraUp);
    }
    if (!RibbonEffects.IsEmpty())
    {
        const FVector CameraWorld = BillboardView.IsValid()
            ? BillboardView->GetComponentLocation()
            : (CameraManager ? CameraManager->GetCameraLocation()
                : GetComponentLocation() + FVector(300.0f, 0.0f, 100.0f));
        const FVector CameraLocal = GetComponentTransform().InverseTransformPosition(CameraWorld);
        for (FWowRibbonEffect& Effect : RibbonEffects)
        {
            UpdateRibbonEffect(Effect, DeltaTime, CameraLocal);
        }
    }
    if (CameraManager || BillboardView.IsValid())
    {
        const FVector CameraLocation = BillboardView.IsValid() ? BillboardView->GetComponentLocation() : CameraManager->GetCameraLocation();
        const FVector ToCamera = (CameraLocation - GetComponentLocation()).GetSafeNormal();
        if (ToCamera.IsNearlyZero())
        {
            return;
        }

        // All particles in one M2 occupy only a small area, so one shared
        // orientation is indistinguishable from calculating it per sprite.
        // Only push transforms after a meaningful angle change; the previous
        // implementation dirtied every primitive every frame even at rest.
        const FRotator DesiredRotation =
            FRotationMatrix::MakeFromZY(ToCamera, FVector::UpVector).Rotator();
        const bool bRotationChanged = ComponentHasTag(TEXT("WowEquipmentEffect")) || !bHasCachedBillboardWorldRotation
            || !DesiredRotation.Equals(CachedBillboardWorldRotation, 1.0f);
        if (bRotationChanged)
        {
            CachedBillboardWorldRotation = DesiredRotation;
            bHasCachedBillboardWorldRotation = true;
            auto FaceCamera = [&DesiredRotation](USceneComponent* Billboard)
            {
                if (Billboard)
                {
                    Billboard->SetWorldRotation(DesiredRotation);
                }
            };
            for (const FFireEffect& Effect : FireEffects)
            {
                FaceCamera(Effect.AuthoredFlameMesh);
                // r337 (Stefan: a wall torch flame seen from the side was flat and cut off): the card
                // faces the camera, so from the side half of it went into the wall. Slide it towards
                // the camera by half its width; along the view ray that does not move it on screen.
                if (Effect.AuthoredFlameMesh && (Effect.bTorchEffect || Effect.bSteadyWallTorch))
                {
                    const float Push = FMath::Min(FMath::Max(Effect.ScaleStart, Effect.ScaleMiddle) * FWowCoordinate::SCALE * 2.4f, 60.0f);
                    Effect.AuthoredFlameMesh->SetWorldLocation(GetComponentTransform().TransformPosition(Effect.BasePosition) + ToCamera * Push);
                }
                for (const FProceduralFlameParticle& Particle : Effect.FlameParticles)
                {
                    FaceCamera(Particle.Visual);
                }
            }
            for (const FSmokeEffect& Effect : SmokeEffects) FaceCamera(Effect.Billboard);
            for (const FSparkleEffect& Effect : SparkleEffects) FaceCamera(Effect.Billboard);
        }
    }
}

void UWowM2ParticleComponent::InitializeFromM2Data(const TArray<uint8>& M2DataRaw)
{
    // TODO: Parse M2 data directly here if needed for blueprint support
    UE_LOG(LogWowParticles, Log, TEXT("Blueprint M2 particle initialization not implemented yet"));
}

void UWowM2ParticleComponent::InitializeFromParsedM2Data(const void* M2DataPtr,
                                                        FMpqManager* Mpq, FWowAssetCache* Cache)
{
    if (!M2DataPtr)
    {
        return;
    }

    const FM2Data* M2Data = static_cast<const FM2Data*>(M2DataPtr);
    if (M2Data->ParticleEmitters.Num() == 0 && M2Data->RibbonEmitters.Num() == 0)
    {
        return;
    }
    // r563: a dungeon or raid portal keeps its own emitters to switch back to after Heroic.
    if ((PortalKind == 1 || PortalKind == 2) && !bSwappingPortal)
    {
        PortalNormalData = MakeShared<FM2Data>(*M2Data);
        PortalMpq = Mpq;
        PortalCache = Cache;
        PortalShownHeroic = 0;
    }

    UE_LOG(LogWowParticles, Verbose, TEXT("Initializing %d particle emitters and %d ribbons"),
        M2Data->ParticleEmitters.Num(), M2Data->RibbonEmitters.Num());

    // Clear existing particles
    ClearParticles();

    bool bModelHasFireTexture = false;
    for (const FM2ParticleEmitter& Candidate : M2Data->ParticleEmitters)
    {
        if (M2Data->TexturePaths.IsValidIndex(Candidate.Texture) &&
            ClassifyParticleVisual(Candidate, M2Data->TexturePaths[Candidate.Texture]) ==
                EWowParticleVisualKind::Fire)
        {
            bModelHasFireTexture = true;
            break;
        }
    }

    const bool bSimulate = ShouldSimulateEmitters();
    EmitterBones = M2Data->Bones;
    // Persistent (looping) spell states loop Stand, like the client; one-shots keep sequence 0.
    const int32 EmitterSeq = bForceLoopAnimation ? M2Data->StateLoopClip() : 0;
    EmitterAnimation = M2Data->AnimationTracks.IsValidIndex(EmitterSeq) ? M2Data->AnimationTracks[EmitterSeq]
        : M2Data->AnimationTracks.IsEmpty() ? FM2AnimationData() : M2Data->AnimationTracks[0];
    bEmitterAnimationLoops = !bPlayOnce && (bForceLoopAnimation || M2Data->Animations.IsEmpty() || M2Data->Animations[0].IsLooping());
    EmitterClock = 0.0f;
    for (const FM2ParticleEmitter& Emitter : M2Data->ParticleEmitters)
    {
        if ((ComponentHasTag(TEXT("WowEquipmentEffect")) || ComponentHasTag(TEXT("WowHearthFixture"))) && !Emitter.bEnabledInStand) continue;
        // M2 emitter positions are already in model space at the rest pose. The
        // referenced bone matrix supplies animation deformation; adding its pivot
        // here double-translates static doodad emitters (notably StonePyre01).
        const FVector LocalPosition = M2ParticlePositionToUELocal(Emitter.Position);
        if (LocalPosition.ContainsNaN() ||
            !FMath::IsFinite(LocalPosition.X) ||
            !FMath::IsFinite(LocalPosition.Y) ||
            !FMath::IsFinite(LocalPosition.Z))
        {
            UE_LOG(LogWowParticles, Warning,
                TEXT("Skipping particle emitter %u with invalid local position"), Emitter.Id);
            continue;
        }
        // The authored M2 particle texture is the visual source of truth. In
        // WotLK this is commonly a SubUV atlas (for example 4x4 fire frames),
        // not merely an optional decoration. The old fallback deliberately
        // skipped it and could therefore only draw generic glowing discs.
        UTexture2D* Texture = LoadM2EffectTexture(*M2Data, Emitter.Texture, Mpq, Cache);
        const FString TexturePath = M2Data->TexturePaths.IsValidIndex(Emitter.Texture)
            ? M2Data->TexturePaths[Emitter.Texture]
            : FString();
        UE_LOG(LogWowParticles, Verbose,
            TEXT("Emitter %u texture index=%u path='%s' loaded=%s atlas=%dx%d"),
            Emitter.Id, Emitter.Texture, *TexturePath, *GetNameSafe(Texture),
            Emitter.TextureRows, Emitter.TextureColumns);

        // Determine effect type from authored data. Do not let temporary
        // parser defaults turn water mist and magical glows into flames.
        const bool bMagicalCore=ComponentHasTag(TEXT("WowMagicalLampCore")) && TexturePath.Contains(TEXT("glow"));
        const EWowParticleVisualKind VisualKind = bMagicalCore?EWowParticleVisualKind::Sparkle:ClassifyParticleVisual(Emitter, TexturePath);
        // r345 (Stefan: Frostbrand/Flametongue showed a fixed blinking light beside the weapon):
        // Shaman_Frost/Fire have no mesh; their sparks sit on bones under a spinning root bone and
        // circle the blade. Simulate those emitters along the animated bone chain.
        bool bOrbitingEquipmentEmitter = false;
        if (ComponentHasTag(TEXT("WowEquipmentEffect")) && Texture)
            for (int32 B = Emitter.Bone, Depth = 0; M2Data->Bones.IsValidIndex(B) && Depth < 64; B = M2Data->Bones[B].ParentBone, ++Depth)
                if (EmitterAnimation.BoneTracks.IsValidIndex(B) && (EmitterAnimation.BoneTracks[B].RotTimestamps.Num() > 1
                    || EmitterAnimation.BoneTracks[B].TransTimestamps.Num() > 1)) { bOrbitingEquipmentEmitter = true; break; }
        // r404 (Stefan: a bright white star by the shin with Rockbiter - "this must go"): Shaman_Rock's
        // two FLARE.BLP emitters drew a blown-out white flare under the darker realistic lighting.
        // Rockbiter keeps its rocks and brown dust; its flares are left out.
        // r440 (Stefan: "You removed the wrong effect for Rockbiter"): the piece he wanted gone is the
        // white smoky puff (the CLOUDS5 dust emitter), not the flares. The flares come back (dimmed below),
        // the cloud emitter is left out.
        const bool bRockbiter = ComponentHasTag(TEXT("WowEquipmentEffect"))
            && M2Data->TexturePaths.ContainsByPredicate([](const FString& P) { return P.Contains(TEXT("EARTHSHIELD_ROCK"), ESearchCase::IgnoreCase); });
        if (bRockbiter && TexturePath.Contains(TEXT("CLOUDS"), ESearchCase::IgnoreCase))
            continue;
        if (bOrbitingEquipmentEmitter)
        {
            CreateEmitterEffect(Emitter, Texture);
            // r358 (Stefan: a new dusty cloud over the other arm and leg with Rockbiter): Shaman_Rock's
            // CLOUDS5 emitter trails in world space, and with the weapon swinging (and the x1.8 enchant
            // scale) the puffs were left hanging across the body. Weapon imbue particles stay with the weapon.
            EmitterEffects.Last().bLocalSpace = true;
            // r360 (Stefan: "a bit extreme, and it leaves a ring"): spell emitters get a 2.2x HDR boost for
            // bloom; on a weapon imbue that turned Rockbiter's faint brown dust into a bright smoky ring.
            // Imbue particles render at authored strength, slightly under it.
            if (UMaterialInstanceDynamic* ImbueMaterial = EmitterEffects.Last().Material)
                ImbueMaterial->SetScalarParameterValue(TEXT("Strength"),
                    bRockbiter && TexturePath.Contains(TEXT("FLARE"), ESearchCase::IgnoreCase) ? 0.3f : 0.6f); // r440: Rockbiter's flares stay a soft glint
        }
        else if (ComponentHasTag(TEXT("WowEquipmentEffect")) && Texture)
        {
            // Weapons use the authored particle population, lifetime, atlas and
            // color tracks; ambient-fixture flame offsets/lights do not apply.
            CreateFireEffect(Emitter, LocalPosition, Texture);
        }
        else if (VisualKind == EWowParticleVisualKind::Fire && (Emitter.bEnabledInStand || bPlayOnce || bForceLoopAnimation))
        {
            CreateFireEffect(Emitter, LocalPosition, Texture);
            UE_LOG(LogWowParticles, Verbose,
                TEXT("Created fire effect at %s texture=%s atlas=%ux%u"),
                *LocalPosition.ToString(),
                Texture ? *Texture->GetName() : TEXT("<none>"),
                Emitter.TextureColumns, Emitter.TextureRows);
        }
        // r370 (Stefan: Shrink was only a few blinking lights): spell visuals whose emitters are
        // enabled for a short window (off most of the sequence) are simulated too and gated by
        // that window in UpdateEmitterEffect; they used to fall through to a static twinkle.
        else if (bSimulate && !bMagicalCore && (Emitter.bEnabledInStand
            || ((bPlayOnce || bForceLoopAnimation) && Emitter.HasEnabledWindow())))
        {
            // Spells, creatures and magical doodads: simulate the authored emitter.
            // Without its texture the sprites render as plain pale squares (seen over the
            // Sen'jin cauldron), which is worse than leaving that one emitter out.
            if (Texture)
            {
                CreateEmitterEffect(Emitter, Texture);
                if (EmitterEffects.Last().bLightningArcs)
                    EmitterEffects.Last().CrackleTexture = LoadEffectTextureByPath(TEXT("Spells\\Zap1.blp"), Mpq, Cache);
            }
            else UE_LOG(LogWowParticles, Warning, TEXT("Skipping emitter %u on %s: texture %u '%s' did not load (blend %u flags 0x%x)"),
                Emitter.Id, *GetNameSafe(GetOwner()), Emitter.Texture, *TexturePath, Emitter.BlendMode, Emitter.Flags);
        }
        else if (VisualKind == EWowParticleVisualKind::Smoke)
        {
            CreateSmokeEffect(Emitter, LocalPosition, Texture);
            UE_LOG(LogWowParticles, Verbose, TEXT("Created smoke effect at %s"), *LocalPosition.ToString());
        }
        else if (!bMagicalCore && (bModelHasFireTexture || ComponentHasTag(TEXT("WowLampGlassFixture"))) &&
            TexturePath.Contains(TEXT("glow"), ESearchCase::IgnoreCase))
        {
            // Fire fixtures already have an emissive flame and a local point
            // light.  Their secondary Glow emitter is a subtle halo in WoW;
            // rendering it as a regular billboard created a conspicuous orb
            // floating above the flame, so keep the illumination and omit the
            // fake visible sphere.
            UE_LOG(LogWowParticles, Verbose,
                TEXT("Suppressed attached glow billboard at %s"), *LocalPosition.ToString());
        }
        // r500 (Stefan: "strange things above the totems again, never got fixed" - small 4x4 dot grids
        // over Stoneskin and other totems): the totem's upper emitters are switched off in its Stand
        // animation (they belong to other sequences), so nothing is drawn there in WoW. They fell through
        // to the static sparkle billboard, which showed their whole 4x4 sprite atlas as a grid of dots.
        // A simulated model's emitter that is off in Stand is left out.
        else if (bSimulate && !Emitter.bEnabledInStand)
        {
            UE_LOG(LogWowParticles, Verbose, TEXT("Emitter %u on %s is off in Stand: not drawn"), Emitter.Id, *GetNameSafe(GetOwner()));
        }
        else
        {
            CreateSparkleEffect(Emitter, LocalPosition, Texture);
            UE_LOG(LogWowParticles, Verbose, TEXT("Created sparkle/glow effect at %s"), *LocalPosition.ToString());
        }
    }

    for (const FM2RibbonEmitter& Ribbon : M2Data->RibbonEmitters)
    {
        if (!Ribbon.bVisible)
        {
            continue;
        }
        UTexture2D* Texture = LoadM2EffectTexture(*M2Data, Ribbon.Texture, Mpq, Cache);
        CreateRibbonEffect(Ribbon, *M2Data, Texture);
    }
}

void UWowM2ParticleComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    ClearParticles();
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}

// r563 (Stefan: "when dungeons get harder there's a skull in the middle and the colours are different
// ... add a skull and make the colour more orange"): the 3.3.5 client shows the server's difficulty
// portals (gameobject type 31: InstancePortal_PurpleDifficulty on Normal, ..._PurpleDifficultyIcon with
// the skull on Heroic) by the player's difficulty. Stefan's extension: every other dungeon portal (the
// blue and red ones in the world data) turns into the skull portal, coloured orange, on Heroic; raid
// portals (green) do the same on Heroic raid difficulty.
void UWowM2ParticleComponent::SetPortalKind(const FString& Path)
{
    const FString Name = FPaths::GetBaseFilename(Path).ToUpper();
    if (!Name.StartsWith(TEXT("INSTANCEPORTAL")) || Name.Contains(TEXT("COLLISION"))) { PortalKind = 0; return; }
    if (Name.Contains(TEXT("DIFFICULTYICON"))) PortalKind = 4;
    else if (Name.Contains(TEXT("DIFFICULTY"))) PortalKind = 3;
    else if (Name.Contains(TEXT("GREEN"))) PortalKind = 2;
    else PortalKind = 1;
}

namespace
{
    // Portal particle colours to the heroic skull's pink (r592, Stefan: "Make them the same pink-ish color"
    // - the crossed-sword particles were orange next to the pink skull and rings), keeping each key's brightness.
    void WowMakePortalPink(FM2Data& Data)
    {
        auto Pink = [](uint32 (&Rgb)[3])
        {
            const float Peak = float(FMath::Max3(Rgb[0], Rgb[1], Rgb[2]));
            const bool bWhite = FMath::Min3(Rgb[0], Rgb[1], Rgb[2]) > 200; // light keys stay a pale pink
            Rgb[0] = uint32(FMath::RoundToInt(Peak));
            Rgb[1] = uint32(FMath::RoundToInt(Peak * (bWhite ? 0.72f : 0.24f)));
            Rgb[2] = uint32(FMath::RoundToInt(Peak * (bWhite ? 0.86f : 0.66f)));
        };
        for (FM2ParticleEmitter& Emitter : Data.ParticleEmitters)
        {
            Pink(Emitter.ColorStart);
            Pink(Emitter.ColorMiddle);
            Pink(Emitter.ColorEnd);
        }
    }
}

bool UWowM2ParticleComponent::UpdatePortalDifficulty()
{
    const bool bRaid = PortalKind == 2;
    const bool bHeroic = bRaid ? FWowDifficultyState::IsHeroicRaid() : FWowDifficultyState::IsHeroicDungeon();
    if (PortalKind == 3 || PortalKind == 4)
    {
        // The server's difficulty portals: one of the pair is shown.
        const bool bShow = (PortalKind == 4) == bHeroic;
        if (IsVisible() != bShow) SetVisibility(bShow, true);
        return bShow;
    }
    if (!PortalNormalData.IsValid() || int8(bHeroic) == PortalShownHeroic) return true;
    if (bHeroic && !PortalHeroicData.IsValid() && PortalMpq)
    {
        TArray<uint8> M2Raw, SkinRaw;
        const FString Icon = TEXT("World\\Generic\\ActiveDoodads\\InstancePortal\\InstancePortal_PurpleDifficultyIcon");
        if (PortalMpq->ReadFile(Icon + TEXT(".m2"), M2Raw))
        {
            PortalMpq->ReadFile(Icon + TEXT("00.skin"), SkinRaw);
            PortalHeroicData = MakeShared<FM2Data>(FM2Parser::Parse(M2Raw, SkinRaw));
            // r593 (Stefan: keep the skull, "but make it also DUST6.BLP and small glowing dots GLOWBALL.BLP
            // but in pink"): the skull model's swirl (SKULLPORTAL3, the crossed swords) and dots (the fire
            // elemental's GLOWBALL) use the blue portal's own textures. Same emitter settings in both models.
            FM2Data& Heroic = *PortalHeroicData;
            auto UseTexture = [&Heroic](const TCHAR* From, const TCHAR* To)
            {
                int32 NewIndex = INDEX_NONE;
                for (FM2ParticleEmitter& Emitter : Heroic.ParticleEmitters)
                {
                    if (!Heroic.TexturePaths.IsValidIndex(Emitter.Texture)
                        || !Heroic.TexturePaths[Emitter.Texture].Contains(From, ESearchCase::IgnoreCase)) continue;
                    if (NewIndex == INDEX_NONE)
                    {
                        const int32 Old = Emitter.Texture;
                        NewIndex = Heroic.TexturePaths.Add(To);
                        if (Heroic.TextureTypes.Num() == NewIndex) Heroic.TextureTypes.Add(0);
                        if (Heroic.TextureFlags.Num() == NewIndex)
                            Heroic.TextureFlags.Add(Heroic.TextureFlags.IsValidIndex(Old) ? Heroic.TextureFlags[Old] : 0u);
                    }
                    Emitter.Texture = uint16(NewIndex);
                }
            };
            UseTexture(TEXT("SKULLPORTAL3"), TEXT("World\\Generic\\ActiveDoodads\\InstancePortal\\Dust6.blp"));
            UseTexture(TEXT("FIREELEMENTAL\\GLOWBALL"), TEXT("World\\Dungeon\\CavernsOfTime\\PassiveDoodads\\DarkPortal\\GlowBall.blp"));
            WowMakePortalPink(*PortalHeroicData);
        }
    }
    const FM2Data* Data = bHeroic && PortalHeroicData.IsValid() ? PortalHeroicData.Get() : PortalNormalData.Get();
    bSwappingPortal = true;
    InitializeFromParsedM2Data(Data, PortalMpq, PortalCache);
    bSwappingPortal = false;
    PortalShownHeroic = int8(bHeroic);
    UE_LOG(LogWowParticles, Log, TEXT("Portal %s on %s: %s"), *GetNameSafe(GetOwner()), bRaid ? TEXT("raid") : TEXT("dungeon"),
        bHeroic ? (PortalHeroicData.IsValid() ? TEXT("Heroic, pink skull portal") : TEXT("Heroic, skull model missing")) : TEXT("Normal"));
    return true;
}

void UWowM2ParticleComponent::ClearParticles()
{
    for (USceneComponent* Visual : EquipmentVisuals)
        if (IsValid(Visual)) Visual->DestroyComponent();
    EquipmentVisuals.Reset();
    if (HolyLightVeil) HolyLightVeil->DestroyComponent();
    HolyLightVeil = nullptr;
    HolyLightVeilMaterial = nullptr;
    // Clean up fire effects
    for (FFireEffect& Effect : FireEffects)
    {
        if (Effect.AuthoredFlameMesh)
        {
            Effect.AuthoredFlameMesh->DestroyComponent();
        }
        if (Effect.Light)
        {
            Effect.Light->DestroyComponent();
        }
        for (FProceduralFlameParticle& Particle : Effect.FlameParticles)
        {
            if (Particle.Visual)
            {
                Particle.Visual->DestroyComponent();
            }
        }
    }
    FireEffects.Empty();

    // Clean up smoke effects
    for (FSmokeEffect& Effect : SmokeEffects)
    {
        if (Effect.Billboard)
        {
            Effect.Billboard->DestroyComponent();
        }
    }
    SmokeEffects.Empty();

    // Clean up sparkle effects
    for (FSparkleEffect& Effect : SparkleEffects)
    {
        if (Effect.Billboard)
        {
            Effect.Billboard->DestroyComponent();
        }
    }
    SparkleEffects.Empty();

    for (FWowRibbonEffect& Effect : RibbonEffects)
    {
        if (Effect.Mesh) Effect.Mesh->DestroyComponent();
    }
    RibbonEffects.Empty();

    for (FWowEmitterEffect& Effect : EmitterEffects)
    {
        if (Effect.Mesh) Effect.Mesh->DestroyComponent();
    }
    EmitterEffects.Empty();
}

namespace
{
FVector SampleRibbonVector(const TArray<uint32>& Times, const TArray<FVector>& Values,
    float TimeMs, const FVector& DefaultValue)
{
    const int32 Count = FMath::Min(Times.Num(), Values.Num());
    if (Count == 0) return DefaultValue;
    if (Count == 1 || TimeMs <= Times[0]) return Values[0];
    for (int32 Index = 1; Index < Count; ++Index)
    {
        if (TimeMs <= Times[Index])
        {
            const float Span = FMath::Max(1.0f, static_cast<float>(Times[Index] - Times[Index - 1]));
            return FMath::Lerp(Values[Index - 1], Values[Index],
                FMath::Clamp((TimeMs - Times[Index - 1]) / Span, 0.0f, 1.0f));
        }
    }
    return Values.Last();
}

FQuat SampleRibbonRotation(const TArray<uint32>& Times, const TArray<FQuat>& Values,
    float TimeMs)
{
    const int32 Count = FMath::Min(Times.Num(), Values.Num());
    if (Count == 0) return FQuat::Identity;
    if (Count == 1 || TimeMs <= Times[0]) return Values[0].GetNormalized();
    for (int32 Index = 1; Index < Count; ++Index)
    {
        if (TimeMs <= Times[Index])
        {
            const float Span = FMath::Max(1.0f, static_cast<float>(Times[Index] - Times[Index - 1]));
            return FQuat::Slerp(Values[Index - 1].GetNormalized(), Values[Index].GetNormalized(),
                FMath::Clamp((TimeMs - Times[Index - 1]) / Span, 0.0f, 1.0f)).GetNormalized();
        }
    }
    return Values.Last().GetNormalized();
}
}

FVector UWowM2ParticleComponent::SampleRibbonPosition(
    const FWowRibbonEffect& Effect, float TimeSeconds, FVector* OutUp) const
{
    if (OutUp) *OutUp = FVector::UpVector;
    if (!Effect.Bones.IsValidIndex(Effect.Emitter.Bone))
    {
        return M2ParticlePositionToUELocal(Effect.Emitter.Position);
    }
    const float DurationMs = FMath::Max(static_cast<float>(Effect.Animation.Duration), 1.0f);
    float TimeMs = TimeSeconds * 1000.0f;
    if (Effect.bAnimationLoops) TimeMs = FMath::Fmod(TimeMs, DurationMs);
    else TimeMs = FMath::Min(TimeMs, DurationMs);
    const float RawMs = TimeSeconds * 1000.0f; // r370: global sequences
    auto GTime = [RawMs, &TimeMs](uint32 Global) { return Global ? FMath::Fmod(RawMs, float(Global)) : TimeMs; };

    TArray<FTransform> ComponentTransforms;
    TArray<uint8> Built;
    ComponentTransforms.SetNum(Effect.Bones.Num());
    Built.Init(0, Effect.Bones.Num());
    TFunction<FTransform(int32, int32)> BuildTransform = [&](int32 BoneIndex, int32 Depth)
    {
        if (!Effect.Bones.IsValidIndex(BoneIndex) || Depth > Effect.Bones.Num())
            return FTransform::Identity;
        if (Built[BoneIndex]) return ComponentTransforms[BoneIndex];
        const FM2Bone& Bone = Effect.Bones[BoneIndex];
        FVector ParentPivot = FVector::ZeroVector;
        if (Effect.Bones.IsValidIndex(Bone.ParentBone))
            ParentPivot = Effect.Bones[Bone.ParentBone].PivotPoint;
        FVector Translation = Bone.PivotPoint - ParentPivot;
        FQuat Rotation = FQuat::Identity;
        FVector Scale = FVector::OneVector;
        if (Effect.Animation.BoneTracks.IsValidIndex(BoneIndex))
        {
            const FM2BoneTrack& Track = Effect.Animation.BoneTracks[BoneIndex];
            Translation += SampleRibbonVector(Track.TransTimestamps, Track.TransValues,
                GTime(Track.TransGlobalDuration), FVector::ZeroVector);
            Rotation = SampleRibbonRotation(Track.RotTimestamps, Track.RotValues, GTime(Track.RotGlobalDuration));
            Scale = SampleRibbonVector(Track.ScaleTimestamps, Track.ScaleValues,
                GTime(Track.ScaleGlobalDuration), FVector::OneVector);
        }
        const FQuat UERotation(Rotation.Y, Rotation.X, Rotation.Z, -Rotation.W);
        FTransform Result(UERotation.GetNormalized(),
            M2ParticlePositionToUELocal(Translation), FVector(Scale.Y, Scale.X, Scale.Z));
        if (Effect.Bones.IsValidIndex(Bone.ParentBone))
            Result *= BuildTransform(Bone.ParentBone, Depth + 1);
        Built[BoneIndex] = 1;
        ComponentTransforms[BoneIndex] = Result;
        return Result;
    };
    // Same convention as GetEmitterFrame: the ribbon position is model space, the bone
    // transform maps pivot-relative points, so subtract the bone pivot first.
    const FTransform BoneTransform = BuildTransform(Effect.Emitter.Bone, 0);
    // WoW extends a ribbon along the emitter bone's up axis (above/below), not towards the
    // camera: Smite's ribbons are wide sheets that spread into a V as the bones swing out.
    if (OutUp) *OutUp = BoneTransform.TransformVector(FVector::UpVector);
    return BoneTransform.TransformPosition(
        M2ParticlePositionToUELocal(Effect.Emitter.Position - Effect.Bones[Effect.Emitter.Bone].PivotPoint));
}

void UWowM2ParticleComponent::CreateRibbonEffect(const FM2RibbonEmitter& Emitter,
    const FM2Data& Data, UTexture2D* Texture)
{
    FWowRibbonEffect Effect;
    Effect.Emitter = Emitter;
    Effect.Bones = Data.Bones;
    {
        const int32 Seq = bForceLoopAnimation ? Data.StateLoopClip() : 0;
        if (Data.AnimationTracks.IsValidIndex(Seq)) Effect.Animation = Data.AnimationTracks[Seq];
        else if (!Data.AnimationTracks.IsEmpty()) Effect.Animation = Data.AnimationTracks[0];
    }
    Effect.bAnimationLoops = !bPlayOnce && (bForceLoopAnimation || (!Data.Animations.IsEmpty() && Data.Animations[0].IsLooping()));
    Effect.Mesh = NewObject<UProceduralMeshComponent>(this);
    Effect.Mesh->SetupAttachment(this);
    Effect.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Effect.Mesh->SetCastShadow(false);
    Effect.Mesh->SetTranslucentSortPriority(15);
    Effect.Mesh->RegisterComponent();
    if (UMaterial* Base = GetBaseBillboardMaterial())
    {
        Effect.Material = UMaterialInstanceDynamic::Create(Base, this);
        if (Texture) Effect.Material->SetTextureParameterValue(TEXT("EffectTexture"), Texture);
        Effect.Material->SetVectorParameterValue(TEXT("Tint"), Emitter.Color);
        Effect.Material->SetScalarParameterValue(TEXT("EmissiveStrength"), 3.25f);
        Effect.Material->SetScalarParameterValue(TEXT("Opacity"), Emitter.Color.A);
        Effect.Mesh->SetMaterial(0, Effect.Material);
    }
    RibbonEffects.Add(MoveTemp(Effect));
}

void UWowM2ParticleComponent::UpdateRibbonEffect(FWowRibbonEffect& Effect,
    float DeltaTime, const FVector& CameraLocationLocal)
{
    Effect.Age += DeltaTime;
    for (FWowRibbonEdge& Edge : Effect.Edges) Edge.Age += DeltaTime;
    Effect.Edges.RemoveAll([&Effect](const FWowRibbonEdge& Edge)
    { return Edge.Age > Effect.Emitter.EdgeLifetime; });

    const float DurationSeconds = Effect.Animation.Duration / 1000.0f;
    float SequenceMs = Effect.Age * 1000.0f;
    if (Effect.bAnimationLoops && Effect.Animation.Duration > 0)
        SequenceMs = FMath::Fmod(SequenceMs, float(Effect.Animation.Duration));
    const bool bCanEmit = (Effect.bAnimationLoops || DurationSeconds <= 0.0f ||
        Effect.Age <= DurationSeconds) && Effect.Emitter.IsVisibleAt(SequenceMs);
    if (!bCanEmit)
    {
        // Hidden: the trail fades out; when it shows again it starts a new strip
        // instead of joining the old end to the new start across the scene.
        Effect.bHasLastPosition = false;
    }
    else
    {
        FVector Up = FVector::UpVector;
        const FVector Position = SampleRibbonPosition(Effect, Effect.Age, &Up);
        Effect.EdgeAccumulator += DeltaTime * Effect.Emitter.EdgesPerSecond;
        // A bone that snaps (sequence reset, teleport) would otherwise draw one long
        // straight streak between the two positions.
        if (Effect.bHasLastPosition && FVector::DistSquared(Effect.LastPosition, Position) > FMath::Square(400.0f))
            Effect.bHasLastPosition = false;
        if (!Effect.bHasLastPosition)
        {
            Effect.Edges.Reset();
            Effect.Edges.Insert({Position, 0.0f, Up}, 0);
            Effect.LastUp = Up;
            Effect.LastPosition = Position;
            Effect.bHasLastPosition = true;
            Effect.EdgeAccumulator = 0.0f;
        }
        const int32 NewEdges = FMath::Clamp(FMath::FloorToInt(Effect.EdgeAccumulator), 0, 8);
        for (int32 Index = 0; Index < NewEdges; ++Index)
        {
            const float Alpha = static_cast<float>(Index + 1) / static_cast<float>(NewEdges + 1);
            Effect.Edges.Insert({FMath::Lerp(Effect.LastPosition, Position, Alpha), 0.0f,
                FMath::Lerp(Effect.LastUp, Up, Alpha)}, 0);
        }
        if (NewEdges > 0)
        {
            Effect.EdgeAccumulator -= NewEdges;
            Effect.LastPosition = Position;
            Effect.LastUp = Up;
        }
    }

    if (!Effect.Mesh || Effect.Edges.Num() < 2)
    {
        if (Effect.Mesh) Effect.Mesh->ClearMeshSection(0);
        return;
    }
    TArray<FVector> Vertices, Normals;
    TArray<int32> Triangles;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    const int32 Count = Effect.Edges.Num();
    Vertices.Reserve(Count * 2); Normals.Reserve(Count * 2);
    UVs.Reserve(Count * 2); Colors.Reserve(Count * 2);
    Triangles.Reserve((Count - 1) * 6);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        FWowRibbonEdge& Edge = Effect.Edges[Index];
        FVector Position = Edge.Position;
        Position.Z -= 0.5f * Effect.Emitter.Gravity * FWowCoordinate::SCALE * Edge.Age * Edge.Age;
        const FVector Tangent = (Index == 0 ? Edge.Position - Effect.Edges[1].Position
            : (Index == Count - 1 ? Effect.Edges[Index - 1].Position - Edge.Position
                : Effect.Edges[Index - 1].Position - Effect.Edges[Index + 1].Position)).GetSafeNormal();
        // Authored width direction (bone up, scaled with the bone); camera-facing only as a
        // fallback when the bone gives none.
        FVector Side = Edge.Up;
        if (Side.SizeSquared() < 1e-6f)
        {
            Side = FVector::CrossProduct(Tangent, (CameraLocationLocal - Position).GetSafeNormal()).GetSafeNormal();
            if (Side.IsNearlyZero()) Side = FVector::RightVector;
        }
        Vertices.Add(Position + Side * Effect.Emitter.HeightAbove * FWowCoordinate::SCALE);
        Vertices.Add(Position - Side * Effect.Emitter.HeightBelow * FWowCoordinate::SCALE);
        const FVector FacingNormal = (CameraLocationLocal - Position).GetSafeNormal();
        Normals.Add(FacingNormal);
        Normals.Add(FacingNormal);
        const float U = static_cast<float>(Index) / static_cast<float>(Count - 1);
        UVs.Add(FVector2D(U, 0.0f)); UVs.Add(FVector2D(U, 1.0f));
        const float Fade = 1.0f - FMath::Clamp(Edge.Age / Effect.Emitter.EdgeLifetime, 0.0f, 1.0f);
        const FLinearColor Color(1.0f, 1.0f, 1.0f, Fade);
        Colors.Add(Color); Colors.Add(Color);
        if (Index > 0)
        {
            const int32 Base = (Index - 1) * 2;
            Triangles.Append({Base, Base + 1, Base + 2, Base + 1, Base + 3, Base + 2});
        }
    }
    Effect.Mesh->ClearMeshSection(0);
    Effect.Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs,
        Colors, TArray<FProcMeshTangent>(), false, false);
}

// ── Authored emitter simulation ───────────────────────────────────────────────
namespace
{
FTransform SampleM2BoneTransform(const TArray<FM2Bone>& Bones, const FM2AnimationData& Animation,
    bool bLoops, int32 BoneIndex, float TimeSeconds)
{
    if (!Bones.IsValidIndex(BoneIndex)) return FTransform::Identity;
    const float DurationMs = FMath::Max(static_cast<float>(Animation.Duration), 1.0f);
    const float RawMs = TimeSeconds * 1000.0f;
    float TimeMs = RawMs;
    TimeMs = bLoops ? FMath::Fmod(TimeMs, DurationMs) : FMath::Min(TimeMs, DurationMs);
    // r370: global-sequence tracks run on their own clock (Shrink's star ring spins on an 800 ms
    // global sequence inside a 2000 ms sequence; it stopped after the first turn).
    auto GTime = [RawMs, &TimeMs](uint32 Global) { return Global ? FMath::Fmod(RawMs, float(Global)) : TimeMs; };
    FTransform Result = FTransform::Identity;
    // Walk child -> root; each bone contributes its pivot-relative animated transform.
    int32 Current = BoneIndex;
    for (int32 Depth = 0; Bones.IsValidIndex(Current) && Depth <= Bones.Num(); ++Depth)
    {
        const FM2Bone& Bone = Bones[Current];
        const FVector ParentPivot = Bones.IsValidIndex(Bone.ParentBone) ? Bones[Bone.ParentBone].PivotPoint : FVector::ZeroVector;
        FVector Translation = Bone.PivotPoint - ParentPivot;
        FQuat Rotation = FQuat::Identity;
        FVector Scale = FVector::OneVector;
        if (Animation.BoneTracks.IsValidIndex(Current))
        {
            const FM2BoneTrack& Track = Animation.BoneTracks[Current];
            Translation += SampleRibbonVector(Track.TransTimestamps, Track.TransValues, GTime(Track.TransGlobalDuration), FVector::ZeroVector);
            Rotation = SampleRibbonRotation(Track.RotTimestamps, Track.RotValues, GTime(Track.RotGlobalDuration));
            Scale = SampleRibbonVector(Track.ScaleTimestamps, Track.ScaleValues, GTime(Track.ScaleGlobalDuration), FVector::OneVector);
        }
        const FQuat UERotation(Rotation.Y, Rotation.X, Rotation.Z, -Rotation.W);
        const FTransform Local(UERotation.GetNormalized(), M2ParticlePositionToUELocal(Translation),
            FVector(Scale.Y, Scale.X, Scale.Z));
        Result = Result * Local;
        Current = Bone.ParentBone;
    }
    return Result;
}

float EmitterTrack3(float Start, float Middle, float End, float MiddleTime, float T)
{
    const float Mid = FMath::Clamp(MiddleTime, 0.01f, 0.99f);
    return T < Mid ? FMath::Lerp(Start, Middle, T / Mid) : FMath::Lerp(Middle, End, (T - Mid) / (1.0f - Mid));
}

FVector EmitterWowDirectionToUE(const FVector& Direction)
{
    return FVector(Direction.Y, Direction.X, Direction.Z);
}

constexpr int32 MaxEmitterParticles = 600; // r299: the instance portal keeps ~525 alive (500/s x 1.05 s)
constexpr float EmitterSimulationRange = 9000.0f; // cm from the camera
}

bool UWowM2ParticleComponent::ShouldSimulateEmitters() const
{
    // World fixtures (lamps, torches, candles, hearths) keep their tuned presentation.
    static const TCHAR* const FixtureTags[] = { TEXT("WowEquipmentEffect"), TEXT("WowManagedFixtureLight"),
        TEXT("WowMagicalLampCore"), TEXT("WowHearthFixture"), TEXT("WowCandleFixture"), TEXT("WowTorchFixture"),
        TEXT("WowLampGlassFixture") };
    for (const TCHAR* Tag : FixtureTags) if (ComponentHasTag(Tag)) return false;
    return true;
}

int32 UWowM2ParticleComponent::GetSimulatedParticleCount() const
{
    int32 Count = 0;
    for (const FWowEmitterEffect& Effect : EmitterEffects) Count += Effect.Particles.Num();
    return Count;
}

void UWowM2ParticleComponent::CreateEmitterEffect(const FM2ParticleEmitter& Emitter, UTexture2D* Texture)
{
    FWowEmitterEffect Effect;
    Effect.Emitter = Emitter;
    // 0x10 = particles stay attached to the emitter ("do not trail"); default trails in world space.
    Effect.bLocalSpace = (Emitter.Flags & 0x10) != 0;
    Effect.bAdditive = Emitter.BlendMode == 3 || Emitter.BlendMode == 4;
    {
        // Electric sparks on a spell: lightning texture, or blue additive sparks of a spell visual.
        const FString TextureName = Texture ? Texture->GetName() : FString();
        const bool bElectricTexture = TextureName.Contains(TEXT("Lightning")) || TextureName.Contains(TEXT("Electric")) ||
            TextureName.Contains(TEXT("Spark")) || TextureName.Contains(TEXT("Shock"));
        const float R = Emitter.ColorStart[0], G = Emitter.ColorStart[1], B = Emitter.ColorStart[2];
        const bool bBlue = B > 120.f && B > R * 1.25f && B >= G;
        // Only on spell visuals; the owner is tagged after spawn, so UpdateEmitterEffect confirms it.
        Effect.bLightningArcs = Effect.bAdditive && bElectricSpell && (bElectricTexture || bBlue);
    }
    Effect.Random.Initialize(static_cast<int32>(Emitter.Id * 7919u + GetUniqueID()));
    Effect.Mesh = NewObject<UProceduralMeshComponent>(this);
    Effect.Mesh->SetupAttachment(this);
    // Vertices are written in world space.
    Effect.Mesh->SetUsingAbsoluteLocation(true);
    Effect.Mesh->SetUsingAbsoluteRotation(true);
    Effect.Mesh->SetUsingAbsoluteScale(true);
    Effect.Mesh->SetWorldTransform(FTransform::Identity);
    Effect.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Effect.Mesh->SetCastShadow(false);
    Effect.Mesh->SetTranslucentSortPriority(14);
    Effect.Mesh->bUseAsyncCooking = true;
    Effect.Mesh->RegisterComponent();
    if (UMaterial* Base = GetEmitterMaterial(Effect.bAdditive))
    {
        Effect.Material = UMaterialInstanceDynamic::Create(Base, this);
        if (Texture) Effect.Material->SetTextureParameterValue(TEXT("EffectTexture"), Texture);
        // A little HDR on additive magic gives UE bloom; blended dust/smoke stays natural.
        // Electric sparks get less: strong HDR bloom washes their blue out to white.
        Effect.Material->SetScalarParameterValue(TEXT("Strength"), Effect.bLightningArcs ? 1.0f : Effect.bAdditive ? 2.2f : 1.0f);
        Effect.Material->SetScalarParameterValue(TEXT("UseTextureAlpha"), Emitter.BlendMode == 3 ? 0.0f : 1.0f);
        Effect.Mesh->SetMaterial(0, Effect.Material);
    }
    EmitterEffects.Add(MoveTemp(Effect));
}

bool UWowM2ParticleComponent::GetEmitterFrame(const FWowEmitterEffect& Effect, FTransform& OutFrame) const
{
    const FVector RestPosition = M2ParticlePositionToUELocal(Effect.Emitter.Position);
    if (USkinnedMeshComponent* Skinned = BoneSource.Get(); Skinned && Skinned->GetSkinnedAsset())
    {
        const int32 BoneIndex = Effect.Emitter.Bone >= 0
            ? Skinned->GetBoneIndex(FName(*FString::Printf(TEXT("Bone_%03d"), Effect.Emitter.Bone))) : INDEX_NONE;
        FTransform Delta = FTransform::Identity;
        if (BoneIndex != INDEX_NONE)
        {
            const FTransform Current = Skinned->GetBoneTransform(BoneIndex, FTransform::Identity);
            const FTransform Rest = FAnimationRuntime::GetComponentSpaceTransformRefPose(
                Skinned->GetSkinnedAsset()->GetRefSkeleton(), BoneIndex);
            Delta = Rest.Inverse() * Current;
        }
        const FTransform Mesh = Skinned->GetComponentTransform();
        OutFrame = FTransform(Mesh.GetRotation() * Delta.GetRotation(),
            Mesh.TransformPosition(Delta.TransformPosition(RestPosition)), Mesh.GetScale3D());
        return true;
    }
    const FTransform Bone = SampleM2BoneTransform(EmitterBones, EmitterAnimation, bEmitterAnimationLoops,
        Effect.Emitter.Bone, EmitterClock);
    // SampleM2BoneTransform maps bone-pivot-relative points to model space (it adds the
    // pivots up the chain), while the emitter position is already in model space: take the
    // bone's pivot off first, or emitters on offset bones land twice as far out (the gypsy
    // wagon's lantern glow hung in the tree above it).
    const FVector Pivot = EmitterBones.IsValidIndex(Effect.Emitter.Bone)
        ? M2ParticlePositionToUELocal(EmitterBones[Effect.Emitter.Bone].PivotPoint) : FVector::ZeroVector;
    const FTransform Component = GetComponentTransform();
    OutFrame = FTransform(Component.GetRotation() * Bone.GetRotation(),
        Component.TransformPosition(Bone.TransformPosition(RestPosition - Pivot)), Component.GetScale3D());
    return true;
}

void UWowM2ParticleComponent::UpdateEmitterEffect(FWowEmitterEffect& Effect, float DeltaTime,
    const FVector& CameraWorld, const FVector& CameraRight, const FVector& CameraUp)
{
    if (!Effect.Mesh) return;
    const FM2ParticleEmitter& E = Effect.Emitter;
    FTransform Frame;
    GetEmitterFrame(Effect, Frame);
    const float Scale = FMath::Max(0.001f, static_cast<float>(Frame.GetScale3D().X));
    const bool bVisibleRange = FVector::DistSquared(CameraWorld, Frame.GetLocation()) < FMath::Square(EmitterSimulationRange * FMath::Max(1.0f, Scale))
        && IsVisible() && (!GetOwner() || !GetOwner()->IsHidden());
    if (!bVisibleRange)
    {
        if (Effect.Particles.Num()) { Effect.Particles.Reset(); Effect.Mesh->ClearMeshSection(0); }
        Effect.SpawnAccumulator = 0.0f;
        return;
    }
    Effect.Age += DeltaTime;
    FRandomStream& Rand = Effect.Random;

    // Spawn. r370: window-only emitters (see InitializeFromM2) emit only while enabled.
    bool bEnabledNow = true;
    if (!E.bEnabledInStand && E.EnabledTimes.Num())
    {
        float SeqMs = EmitterClock * 1000.0f;
        if (bEmitterAnimationLoops && EmitterAnimation.Duration > 0) SeqMs = FMath::Fmod(SeqMs, float(EmitterAnimation.Duration));
        bEnabledNow = E.IsEnabledAt(SeqMs);
    }
    const float Rate = !bEnabledNow ? 0.0f
        : FMath::Max(0.0f, E.EmissionRate * (1.0f + E.EmissionRateVariation * Rand.FRandRange(-1.0f, 1.0f)));
    Effect.SpawnAccumulator += Rate * DeltaTime;
    int32 SpawnCount = FMath::Min(FMath::FloorToInt(Effect.SpawnAccumulator), 48);
    Effect.SpawnAccumulator -= SpawnCount;
    SpawnCount = FMath::Min(SpawnCount, MaxEmitterParticles - Effect.Particles.Num());
    const float AreaLength = E.EmissionAreaLength * FWowCoordinate::SCALE;
    const float AreaWidth = E.EmissionAreaWidth * FWowCoordinate::SCALE;
    for (int32 Index = 0; Index < SpawnCount; ++Index)
    {
        FVector LocalPosition, LocalDirection;
        if (E.EmitterType == 2)
        {
            // Sphere: radius between the two area values; the point is limited by the vertical
            // (polar) and horizontal (azimuth) ranges like the client's sphere generator. A zero
            // horizontal range is a vertical ring in the model's X-Z plane, not a full sphere:
            // the instance portal pulls its blue dust inward over that disc (r299, Stefan: the
            // Stockade portal was loose sparkles all over the gate instead of a swirl).
            const float Polar = Rand.FRandRange(-1.0f, 1.0f) * E.VerticalRange;
            const float Azimuth = Rand.FRandRange(-1.0f, 1.0f) * E.HorizontalRange;
            LocalDirection = E.VerticalRange == 0.0f && E.HorizontalRange == 0.0f
                ? Rand.GetUnitVector() // no ranges authored: keep the old full sphere
                // r426 (Stefan, Stockade portal: "the whole ring turns" - a single streak spun in the gate):
                // the zero-azimuth ring lay in the X-Z plane, edge-on to the doorway, so from the front it
                // was a line that turned with the emitter bone. The portal (its own ring mesh too) lies in
                // the model's Y-Z plane, facing +X: azimuth 0 is the Y-Z circle, the dust fills the disc.
                : EmitterWowDirectionToUE(FVector(FMath::Cos(Polar) * FMath::Sin(Azimuth),
                    FMath::Cos(Polar) * FMath::Cos(Azimuth), FMath::Sin(Polar)));
            const float Radius = FMath::Lerp(FMath::Min(AreaLength, AreaWidth), FMath::Max(AreaLength, AreaWidth), Rand.FRand());
            LocalPosition = LocalDirection * Radius;
        }
        else
        {
            LocalPosition = EmitterWowDirectionToUE(FVector(Rand.FRandRange(-0.5f, 0.5f) * AreaLength,
                Rand.FRandRange(-0.5f, 0.5f) * AreaWidth, 0.0f));
            const float Polar = E.VerticalRange != 0.0f ? Rand.FRandRange(-E.VerticalRange, E.VerticalRange) : 0.0f;
            const float Azimuth = E.HorizontalRange != 0.0f ? Rand.FRandRange(-E.HorizontalRange, E.HorizontalRange) : Rand.FRandRange(0.0f, 2.0f * PI);
            LocalDirection = EmitterWowDirectionToUE(FVector(FMath::Sin(Polar) * FMath::Cos(Azimuth),
                FMath::Sin(Polar) * FMath::Sin(Azimuth), FMath::Cos(Polar)));
        }
        if (E.ZSource > 0.0f)
            LocalDirection = (LocalPosition - FVector(0, 0, E.ZSource * FWowCoordinate::SCALE)).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        const float Speed = E.EmissionSpeed * FWowCoordinate::SCALE * (1.0f + E.SpeedVariation * Rand.FRandRange(-1.0f, 1.0f));
        FWowEmitterParticle Particle;
        Particle.Lifetime = FMath::Max(0.05f, E.Lifespan + E.LifespanVariation * Rand.FRandRange(-1.0f, 1.0f));
        Particle.ScaleMultiplier = FMath::Max(0.05f, 1.0f + E.ScaleVariation * Rand.FRandRange(-1.0f, 1.0f));
        const int32 Cells = FMath::Max(1, int32(E.TextureRows) * int32(E.TextureColumns));
        Particle.RandomCell = Rand.RandRange(0, Cells - 1);
        if (Effect.bLocalSpace)
        {
            Particle.Position = LocalPosition * Scale;
            Particle.Velocity = LocalDirection * Speed * Scale;
        }
        else
        {
            Particle.Position = Frame.GetLocation() + Frame.GetRotation().RotateVector(LocalPosition * Scale);
            Particle.Velocity = Frame.GetRotation().RotateVector(LocalDirection) * Speed * Scale;
        }
        // Spread spawns over the frame so fast emitters don't pulse.
        const float Head = Rand.FRand() * DeltaTime;
        Particle.Age = Head;
        Particle.Position += Particle.Velocity * Head;
        Effect.Particles.Add(Particle);
    }

    // Simulate. Positive gravity pulls down (world -Z), negative lifts.
    const FVector Gravity(0, 0, -E.Gravity * FWowCoordinate::SCALE * Scale);
    for (int32 Index = Effect.Particles.Num() - 1; Index >= 0; --Index)
    {
        FWowEmitterParticle& Particle = Effect.Particles[Index];
        Particle.Age += DeltaTime;
        if (Particle.Age >= Particle.Lifetime) { Effect.Particles.RemoveAtSwap(Index, EAllowShrinking::No); continue; }
        Particle.Velocity += (Effect.bLocalSpace ? Frame.GetRotation().UnrotateVector(Gravity) : Gravity) * DeltaTime;
        Particle.Position += Particle.Velocity * DeltaTime;
    }

    // Build camera-facing sprites (and velocity-stretched tails) in world space.
    Effect.Vertices.Reset(); Effect.Triangles.Reset(); Effect.UVs.Reset(); Effect.Colors.Reset();
    if (Effect.Particles.IsEmpty()) { Effect.Mesh->ClearMeshSection(0); return; }
    if (!Effect.bCrackle && Effect.CrackleTexture && Effect.Material && Effect.bLightningArcs
        && GetOwner() && GetOwner()->Tags.Contains(TEXT("WowSpellVisual")))
    {
        Effect.bCrackle = true;
        Effect.Material->SetTextureParameterValue(TEXT("EffectTexture"), Effect.CrackleTexture);
    }
    const int32 Columns = Effect.bCrackle ? 1 : FMath::Max<int32>(1, E.TextureColumns);
    const int32 Rows = Effect.bCrackle ? 1 : FMath::Max<int32>(1, E.TextureRows);
    const bool bRandomCell = (E.Flags & 0x10000) != 0;
    const bool bFlat = (E.Flags & 0x1000) != 0 && !Effect.bCrackle;
    const bool bTail = E.HeadorTail >= 1 && !Effect.bCrackle;
    const FLinearColor C0(E.ColorStart[0] / 255.f, E.ColorStart[1] / 255.f, E.ColorStart[2] / 255.f);
    const FLinearColor C1(E.ColorMiddle[0] / 255.f, E.ColorMiddle[1] / 255.f, E.ColorMiddle[2] / 255.f);
    const FLinearColor C2(E.ColorEnd[0] / 255.f, E.ColorEnd[1] / 255.f, E.ColorEnd[2] / 255.f);
    struct FSpark { FVector Center; float Size; FLinearColor Color; };
    // bLightningArcs: electric sparks are tinted blue/violet. The extra jagged arcs we used to
    // draw between sparks are gone - they read as white crackle and are not in WoW.
    const bool bArcs = Effect.bLightningArcs && GetOwner() && GetOwner()->Tags.Contains(TEXT("WowSpellVisual"));
    const bool bDrawArcs = false;
    TArray<FSpark, TInlineAllocator<64>> Sparks;
    for (const FWowEmitterParticle& Particle : Effect.Particles)
    {
        const float T = FMath::Clamp(Particle.Age / Particle.Lifetime, 0.0f, 1.0f);
        const float Mid = FMath::Clamp(E.MiddleTime, 0.01f, 0.99f);
        FLinearColor Color = T < Mid ? FMath::Lerp(C0, C1, T / Mid) : FMath::Lerp(C1, C2, (T - Mid) / (1.0f - Mid));
        Color.A = EmitterTrack3(E.AlphaStart, E.AlphaMiddle, E.AlphaEnd, E.MiddleTime, T) / 255.0f;
        if (Color.A <= 0.004f) continue;
        if (bArcs)
        {
            // Electric sparks keep their brightness but read blue/violet, not white.
            const float Peak = FMath::Max3(Color.R, Color.G, Color.B);
            const float Hue = float((E.Id * 2654435761u + GetUniqueID()) % 1000u) / 1000.0f;
            const FLinearColor Tone = FMath::Lerp(FLinearColor(0.22f, 0.40f, 1.0f), FLinearColor(0.62f, 0.30f, 1.0f), Hue * 0.7f);
            const float A = Color.A;
            Color = FMath::Lerp(Color, Tone * Peak, 0.85f);
            Color.A = A;
        }
        if (Effect.bAdditive) { Color.R *= Color.A; Color.G *= Color.A; Color.B *= Color.A; }
        const float Size = EmitterTrack3(E.ScaleStart, E.ScaleMiddle, E.ScaleEnd, E.MiddleTime, T)
            * FWowCoordinate::SCALE * Scale * Particle.ScaleMultiplier;
        if (Size <= 0.05f) continue;
        const int32 Cell = bRandomCell ? Particle.RandomCell : FMath::Clamp(FMath::FloorToInt(
            EmitterTrack3(E.HeadCellStart, E.HeadCellMiddle, E.HeadCellEnd, E.MiddleTime, T) + 0.5f), 0, Rows * Columns - 1);
        const float U0 = float(Cell % Columns) / Columns, V0 = float(Cell / Columns) / Rows;
        const float U1 = U0 + 1.0f / Columns, V1 = V0 + 1.0f / Rows;
        const FVector Center = Effect.bLocalSpace
            ? Frame.GetLocation() + Frame.GetRotation().RotateVector(Particle.Position) : Particle.Position;
        FVector Right = CameraRight, Up = CameraUp;
        if (bFlat) { Right = FVector::ForwardVector; Up = FVector::RightVector; }
        if (Effect.bCrackle)
        {
            // Each crackle jumps to a new angle ~14x a second, like an electric discharge.
            const uint32 Seed = uint32(Particle.RandomCell) * 2654435761u + uint32(FMath::FloorToInt(Particle.Age * 14.f)) * 40503u
                + uint32(&Particle - Effect.Particles.GetData()) * 97u;
            const float Angle = float(Seed % 6283u) / 1000.f;
            const FVector Axis = FVector::CrossProduct(Right, Up).GetSafeNormal();
            Right = Right.RotateAngleAxis(FMath::RadiansToDegrees(Angle), Axis);
            Up = Up.RotateAngleAxis(FMath::RadiansToDegrees(Angle), Axis);
        }
        const float Half = Size * 0.5f;
        FVector A, B, C, D; // top-left, top-right, bottom-left, bottom-right
        const FVector WorldVelocity = Effect.bLocalSpace ? Frame.GetRotation().RotateVector(Particle.Velocity) : Particle.Velocity;
        if (bTail && WorldVelocity.SizeSquared() > 1.0f && !bFlat)
        {
            const FVector Axis = WorldVelocity.GetSafeNormal();
            FVector Side = FVector::CrossProduct(Axis, (CameraWorld - Center).GetSafeNormal()).GetSafeNormal();
            if (Side.IsNearlyZero()) Side = CameraRight;
            const float Length = FMath::Min(Half + WorldVelocity.Size() * FMath::Max(E.TailLength, 0.02f), Size * 8.0f);
            A = Center + Axis * Half + Side * Half; B = Center + Axis * Half - Side * Half;
            C = Center - Axis * Length + Side * Half; D = Center - Axis * Length - Side * Half;
        }
        else
        {
            A = Center - Right * Half + Up * Half; B = Center + Right * Half + Up * Half;
            C = Center - Right * Half - Up * Half; D = Center + Right * Half - Up * Half;
        }
        const int32 Base = Effect.Vertices.Num();
        Effect.Vertices.Append({A, B, C, D});
        Effect.UVs.Append({FVector2D(U0, V0), FVector2D(U1, V0), FVector2D(U0, V1), FVector2D(U1, V1)});
        Effect.Colors.Append({Color, Color, Color, Color});
        Effect.Triangles.Append({Base, Base + 2, Base + 1, Base + 1, Base + 2, Base + 3});
        if (bDrawArcs && bArcs && Sparks.Num() < 48) Sparks.Add({Center, Size, Color});
    }
    if (bDrawArcs && bArcs && Sparks.Num() > 1)
    {
        // Crackling arcs from each spark to its nearest neighbour: jagged, re-rolled ~18x a second,
        // drawn with the texel at the centre of the spark texture (its brightest point).
        const int32 Flicker = FMath::FloorToInt(Effect.Age * 18.0f);
        const FVector2D Hot(0.5f / Columns, 0.5f / Rows);
        for (int32 I = 0; I < Sparks.Num(); ++I)
        {
            int32 Best = INDEX_NONE; float BestDist = TNumericLimits<float>::Max();
            for (int32 J = 0; J < Sparks.Num(); ++J)
            {
                if (J == I) continue;
                const float D2 = FVector::DistSquared(Sparks[I].Center, Sparks[J].Center);
                if (D2 < BestDist) { BestDist = D2; Best = J; }
            }
            if (Best == INDEX_NONE) continue;
            const float MaxLength = FMath::Max(Sparks[I].Size, Sparks[Best].Size) * 5.0f + 6.0f;
            if (BestDist > FMath::Square(MaxLength)) continue;
            FRandomStream Jag(Flicker * 7919 + I * 131 + Best);
            if (Jag.FRand() < 0.25f) continue; // arcs blink in and out
            const FVector From = Sparks[I].Center, To = Sparks[Best].Center;
            const float Length = FMath::Sqrt(BestDist);
            const FVector Axis = (To - From) / FMath::Max(Length, 0.01f);
            FVector Side = FVector::CrossProduct(Axis, (CameraWorld - From).GetSafeNormal()).GetSafeNormal();
            if (Side.IsNearlyZero()) Side = CameraRight;
            const float Width = FMath::Clamp(Length * 0.035f, 0.35f, 2.2f);
            // WoW lightning: blue shading into violet/pink, never plain white (the spark
            // texture is often white, so the arc takes its colour from the palette).
            // Saturated and not over-bright: HDR above ~1 blooms and tone-maps to white.
            const FLinearColor Palette = FMath::Lerp(FLinearColor(0.22f, 0.40f, 1.0f), FLinearColor(0.62f, 0.30f, 1.0f), Jag.FRand());
            FLinearColor Arc = Palette * 0.85f;
            Arc.A = FMath::Min(Sparks[I].Color.A, Sparks[Best].Color.A);
            constexpr int32 Segments = 5;
            FVector Prev = From;
            for (int32 S = 1; S <= Segments; ++S)
            {
                FVector Next = FMath::Lerp(From, To, float(S) / Segments);
                if (S < Segments) Next += Side * Jag.FRandRange(-0.22f, 0.22f) * Length + Axis.Cross(Side) * Jag.FRandRange(-0.12f, 0.12f) * Length;
                const int32 Base = Effect.Vertices.Num();
                Effect.Vertices.Append({Prev + Side * Width, Prev - Side * Width, Next + Side * Width, Next - Side * Width});
                Effect.UVs.Append({Hot, Hot, Hot, Hot});
                Effect.Colors.Append({Arc, Arc, Arc, Arc});
                Effect.Triangles.Append({Base, Base + 2, Base + 1, Base + 1, Base + 2, Base + 3});
                Prev = Next;
            }
        }
    }
    if (Effect.Vertices.IsEmpty()) { Effect.Mesh->ClearMeshSection(0); return; }
    Effect.Mesh->CreateMeshSection_LinearColor(0, Effect.Vertices, Effect.Triangles, TArray<FVector>(),
        Effect.UVs, Effect.Colors, TArray<FProcMeshTangent>(), false, false);
}

UMaterial* UWowM2ParticleComponent::GetEmitterMaterial(bool bAdditive)
{
    static TStrongObjectPtr<UMaterial> Cached[2];
    TStrongObjectPtr<UMaterial>& Slot = Cached[bAdditive ? 1 : 0];
    if (Slot.IsValid()) return Slot.Get();
    const TCHAR* Name = bAdditive ? TEXT("M_WowM2EmitterAdd_v1") : TEXT("M_WowM2EmitterBlend_v1");
    if (UMaterial* Loaded = WowCookedMaterial::Load(Name)) { Slot.Reset(Loaded); return Loaded; }
#if WITH_EDITOR
    UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UMaterial::StaticClass(), FName(Name)));
    Material->SetShadingModel(MSM_Unlit);
    Material->BlendMode = bAdditive ? BLEND_Additive : BLEND_Translucent;
    Material->TwoSided = true;
    auto& Expressions = Material->GetExpressionCollection();
    auto* Texture = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
    Texture->ParameterName = TEXT("EffectTexture");
    Texture->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
    Texture->SamplerType = SAMPLERTYPE_Color;
    Expressions.AddExpression(Texture);
    auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(Material);
    Expressions.AddExpression(UV);
    Texture->Coordinates.Connect(0, UV);
    auto* Vertex = NewObject<UMaterialExpressionVertexColor>(Material);
    Expressions.AddExpression(Vertex);
    auto* Strength = NewObject<UMaterialExpressionScalarParameter>(Material);
    Strength->ParameterName = TEXT("Strength"); Strength->DefaultValue = 1.0f;
    Expressions.AddExpression(Strength);
    auto* UseAlpha = NewObject<UMaterialExpressionScalarParameter>(Material);
    UseAlpha->ParameterName = TEXT("UseTextureAlpha"); UseAlpha->DefaultValue = 1.0f;
    Expressions.AddExpression(UseAlpha);
    // Soft particles: fade where a sprite cuts into the ground or a body.
    auto* Fade = NewObject<UMaterialExpressionDepthFade>(Material);
    Fade->FadeDistanceDefault = 28.0f;
    Expressions.AddExpression(Fade);
    auto AddInput = [](UMaterialExpressionCustom* Node, const TCHAR* InputName, UMaterialExpression* Expression, int32 Output)
    {
        FCustomInput Input; Input.InputName = InputName; Input.Input.Connect(Output, Expression); Node->Inputs.Add(Input);
    };
    auto* Color = NewObject<UMaterialExpressionCustom>(Material);
    Color->Description = TEXT("M2 emitter colour");
    Color->OutputType = CMOT_Float3;
    Color->Inputs.Empty();
    AddInput(Color, TEXT("Tex"), Texture, 0);
    AddInput(Color, TEXT("TexA"), Texture, 4);
    AddInput(Color, TEXT("VC"), Vertex, 0);
    AddInput(Color, TEXT("Strength"), Strength, 0);
    AddInput(Color, TEXT("UseTexAlpha"), UseAlpha, 0);
    AddInput(Color, TEXT("Fade"), Fade, 0);
    Color->Code = bAdditive
        ? TEXT("return Tex * VC * Strength * lerp(1.0, TexA, UseTexAlpha) * Fade;")
        : TEXT("return Tex * VC * Strength;");
    Expressions.AddExpression(Color);
    Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Color);
    if (!bAdditive)
    {
        auto* Opacity = NewObject<UMaterialExpressionCustom>(Material);
        Opacity->Description = TEXT("M2 emitter opacity");
        Opacity->OutputType = CMOT_Float1;
        Opacity->Inputs.Empty();
        AddInput(Opacity, TEXT("TexA"), Texture, 4);
        AddInput(Opacity, TEXT("VA"), Vertex, 4);
        AddInput(Opacity, TEXT("Fade"), Fade, 0);
        Opacity->Code = TEXT("return saturate(TexA * VA * Fade);");
        Expressions.AddExpression(Opacity);
        Material->GetEditorOnlyData()->Opacity.Connect(0, Opacity);
    }
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Material = WowCookedMaterial::Save(Material, Name);
    Slot.Reset(Material);
    return Material;
#else
    return nullptr;
#endif
}

void UWowM2ParticleComponent::CreateFireEffect(const FM2ParticleEmitter& Emitter,
                                               const FVector& LocalPosition, UTexture2D* Texture)
{
    // Several WotLK fixtures describe one visible flame with two or more very
    // close emitters.  Drawing a complete sprite for each is what produced the
    // conspicuous pair of identical flames in the Northshire lantern.  Merge
    // nearby emitters into a single layered fire while preserving genuinely
    // separate braziers and lamps.
    // The generated quads are centred on their component origin, whereas the
    // authored M2 emitter marks the base of the flame. Sink the pooled effect a
    // little so its bright core meets the fixture's stone/fire bowl instead of
    // hovering above it.
    const bool bEquipmentEffect = ComponentHasTag(TEXT("WowEquipmentEffect"));
    const bool bCandleEffect = ComponentHasTag(TEXT("WowCandleFixture"));
    const bool bTorchEffect = ComponentHasTag(TEXT("WowTorchFixture")) || ComponentHasTag(TEXT("WowHearthFixture"));
    // r418 (Stefan: the brazier flame sat down in the bowl - "bring it higher"): a brazier's emitter
    // already marks the coals just under the rim; sinking it 24 cm more hid the fire in the bowl.
    const bool bBrazierEffect = ComponentHasTag(TEXT("WowBrazierFixture"));
    const float FlameBaseSink = (bEquipmentEffect || bCandleEffect || bTorchEffect || bBrazierEffect) ? 0.0f : 24.0f;
    const FVector FlameBasePosition = LocalPosition - FVector(0.0f, 0.0f, FlameBaseSink);
    const bool bHearthEffect = ComponentHasTag(TEXT("WowHearthFixture"));
    // A campfire's emitters around the log pile are one fire, not a flame on each side.
    const float FireEmitterMergeRadius = bCandleEffect ? 2.0f : bHearthEffect ? 160.0f : 75.0f;
    for (FFireEffect& Existing : FireEffects)
    {
        if (!bEquipmentEffect && FVector::Dist(Existing.BasePosition, FlameBasePosition) <= FireEmitterMergeRadius)
        {
            const int32 NewCount = Existing.MergedEmitterCount + 1;
            Existing.BasePosition =
                (Existing.BasePosition * static_cast<float>(Existing.MergedEmitterCount) + FlameBasePosition)
                / static_cast<float>(NewCount);
            Existing.MergedEmitterCount = NewCount;
            Existing.BaseIntensity = FMath::Max(Existing.BaseIntensity,
                FMath::Clamp(Emitter.EmissionRate * 220.0f, 1200.0f, 5200.0f));
            if (Existing.Light)
            {
                Existing.Light->SetRelativeLocation(Existing.BasePosition);
            }
            if (Existing.AuthoredFlameMesh)
            {
                Existing.AuthoredFlameMesh->SetRelativeLocation(Existing.BasePosition);
            }
            return;
        }
    }

    FFireEffect Effect;
    Effect.BasePosition = FlameBasePosition;
    Effect.bEquipmentEffect = bEquipmentEffect;
    Effect.bCandleEffect = bCandleEffect;
    Effect.bTorchEffect = bTorchEffect;
    Effect.bHearthEffect = bHearthEffect;
    Effect.bSteadyWallTorch = ComponentHasTag(TEXT("WowTorchFixture")) &&
        !ComponentHasTag(TEXT("WowHearthFixture")) && !bEquipmentEffect && !bCandleEffect;
    Effect.BaseIntensity = FMath::Clamp(Emitter.EmissionRate * 220.0f, 1200.0f, 5200.0f);
    Effect.PhaseOffset = FMath::Fmod(static_cast<float>(Emitter.Id) * 0.6180339f, 2.0f * PI);
    Effect.TextureRows = FMath::Clamp<int32>(Emitter.TextureRows, 1, 16);
    Effect.TextureColumns = FMath::Clamp<int32>(Emitter.TextureColumns, 1, 16);
    Effect.bUsesAuthoredTexture = Texture != nullptr;
    Effect.EmissionSpeedCm = Emitter.EmissionSpeed * FWowCoordinate::SCALE;
    Effect.SpeedVariation = Emitter.SpeedVariation;
    Effect.VerticalRange = Emitter.VerticalRange;
    Effect.HorizontalRange = Emitter.HorizontalRange;
    Effect.ZSourceCm = Emitter.ZSource * FWowCoordinate::SCALE;
    Effect.Lifespan = Emitter.Lifespan;
    Effect.LifespanVariation = Emitter.LifespanVariation;
    Effect.EmissionAreaWidthCm = Emitter.EmissionAreaWidth * FWowCoordinate::SCALE;
    Effect.EmissionAreaLengthCm = Emitter.EmissionAreaLength * FWowCoordinate::SCALE;
    Effect.MiddleTime = Emitter.MiddleTime;
    Effect.ScaleStart = Emitter.ScaleStart;
    Effect.ScaleMiddle = Emitter.ScaleMiddle;
    Effect.ScaleEnd = Emitter.ScaleEnd;
    Effect.ScaleVariation = Emitter.ScaleVariation;
    auto EmitterColor = [](const uint32* Color)
    {
        return FLinearColor(
            static_cast<float>(Color[0]) / 255.0f,
            static_cast<float>(Color[1]) / 255.0f,
            static_cast<float>(Color[2]) / 255.0f, 1.0f);
    };
    Effect.ColorStart = EmitterColor(Emitter.ColorStart);
    Effect.ColorMiddle = EmitterColor(Emitter.ColorMiddle);
    Effect.ColorEnd = EmitterColor(Emitter.ColorEnd);
    Effect.AlphaStart = static_cast<float>(Emitter.AlphaStart) / 255.0f;
    Effect.AlphaMiddle = static_cast<float>(Emitter.AlphaMiddle) / 255.0f;
    Effect.AlphaEnd = static_cast<float>(Emitter.AlphaEnd) / 255.0f;
    Effect.HeadCellStart = Emitter.HeadCellStart;
    Effect.HeadCellMiddle = Emitter.HeadCellMiddle;
    Effect.HeadCellEnd = Emitter.HeadCellEnd;
    Effect.RandomStream.Initialize(HashCombine(GetTypeHash(Emitter.Id), GetTypeHash(LocalPosition)));

    if (Effect.bSteadyWallTorch)
    {
        Effect.RandomStream.Initialize(HashCombine(GetTypeHash(Emitter.Id),
            GetTypeHash(GetComponentTransform().TransformPosition(LocalPosition))));
        // A wall torch burns at its crown; it does not launch burning sprites, but the
        // flame must lick: short overlapping lives run the atlas animation at fire
        // speed with a slight rise. (3.6 s lives played the atlas so slowly it looked static.)
        Effect.EmissionSpeedCm=14.f; Effect.SpeedVariation=.35f;
        Effect.Lifespan=1.05f; Effect.LifespanVariation=.3f;
        Effect.EmissionAreaWidthCm=Effect.EmissionAreaLengthCm=1.2f;
        Effect.ZSourceCm=0.f;
    }
    if (bCandleEffect)
    {
        // Each authored emitter marks a wick, not a source of rising embers.
        Effect.EmissionSpeedCm=3.f; Effect.SpeedVariation=.25f;
        Effect.Lifespan=.45f; Effect.LifespanVariation=.1f;
        Effect.VerticalRange=.08f; Effect.HorizontalRange=2*PI;
        Effect.EmissionAreaWidthCm=Effect.EmissionAreaLengthCm=.3f;
        Effect.ZSourceCm=2.5f;
        Effect.ScaleStart=.055f; Effect.ScaleMiddle=.12f; Effect.ScaleEnd=.04f;
        Effect.BaseIntensity=180.f;
    }
    // r418: braziers light only through their doodad light (shadowed, above the bowl). This unshadowed
    // light sat inside the bowl and lit the tripod's legs from within.
    if (!bEquipmentEffect && !ComponentHasTag(TEXT("WowManagedFixtureLight")) && !bBrazierEffect)
    {
    // Managed fixtures already own a correctly colored and positioned light.
    // Create point light for fire glow
    Effect.Light = NewObject<UPointLightComponent>(this);
    Effect.Light->SetupAttachment(this);
    Effect.Light->SetRelativeLocation(FlameBasePosition);
    Effect.Light->SetLightColor(FLinearColor(1.0f, 0.6f, 0.2f)); // Orange fire color
    Effect.Light->SetIntensity(Effect.BaseIntensity);
    Effect.Light->SetAttenuationRadius(950.0f);
    Effect.Light->SetCastShadows(false); // Performance optimization
    Effect.Light->RegisterComponent();
    }

    if (Effect.bUsesAuthoredTexture)
    {
        // Preserve the emitter's real particle population, but batch every
        // SubUV quad into a single procedural section. GeneralTorch01 authors
        // 20 one-second particles: this is one draw call instead of 20
        // UStaticMeshComponents and remains practical across repeated lamps.
        const int32 AuthoredParticleCount = FMath::Clamp(
            FMath::CeilToInt(Emitter.EmissionRate *
                (Emitter.Lifespan + Emitter.LifespanVariation)), bEquipmentEffect ? 1 : 4, 24);
        Effect.FlameParticles.Reserve(AuthoredParticleCount);
        for (int32 ParticleIndex = 0; ParticleIndex < AuthoredParticleCount; ++ParticleIndex)
        {
            FProceduralFlameParticle Particle;
            RespawnFlameParticle(Effect, Particle);
            Particle.Age = Effect.bSteadyWallTorch
                ? Particle.Lifetime*(ParticleIndex+Effect.RandomStream.FRand())/AuthoredParticleCount
                : Effect.RandomStream.FRandRange(0.0f, Particle.Lifetime);
            Effect.FlameParticles.Add(MoveTemp(Particle));
        }

        Effect.AuthoredFlameMesh = NewObject<UProceduralMeshComponent>(this);
        Effect.AuthoredFlameMesh->SetupAttachment(this);
        Effect.AuthoredFlameMesh->SetRelativeLocation(FlameBasePosition);
        Effect.AuthoredFlameMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Effect.AuthoredFlameMesh->SetCastShadow(false);
        Effect.AuthoredFlameMesh->SetTranslucentSortPriority(12);
        Effect.AuthoredFlameMesh->RegisterComponent();

        const int32 VertexCount = AuthoredParticleCount * 4;
        Effect.MeshVertices.Init(FVector::ZeroVector, VertexCount);
        Effect.MeshNormals.Init(FVector::UpVector, VertexCount);
        Effect.MeshUVs.Init(FVector2D::ZeroVector, VertexCount);
        Effect.MeshColors.Init(FLinearColor::Transparent, VertexCount);
        TArray<int32> Triangles;
        Triangles.Reserve(AuthoredParticleCount * 6);
        for (int32 ParticleIndex = 0; ParticleIndex < AuthoredParticleCount; ++ParticleIndex)
        {
            const int32 VertexBase = ParticleIndex * 4;
            Triangles.Append({
                VertexBase + 0, VertexBase + 1, VertexBase + 2,
                VertexBase + 0, VertexBase + 2, VertexBase + 3});
        }
        Effect.AuthoredFlameMesh->CreateMeshSection_LinearColor(
            0, Effect.MeshVertices, Triangles, Effect.MeshNormals, Effect.MeshUVs,
            Effect.MeshColors, TArray<FProcMeshTangent>(), false, false);

        if (UMaterial* FlameMaterial = GetBaseBillboardMaterial())
        {
            Effect.AuthoredFlameMaterial = UMaterialInstanceDynamic::Create(FlameMaterial, this);
            Effect.AuthoredFlameMaterial->SetTextureParameterValue(TEXT("EffectTexture"), Texture);
            Effect.AuthoredFlameMaterial->SetVectorParameterValue(TEXT("UVScale"), FLinearColor::White);
            Effect.AuthoredFlameMaterial->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor::Transparent);
            Effect.AuthoredFlameMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor::White);
            Effect.AuthoredFlameMaterial->SetScalarParameterValue(TEXT("Opacity"), 1.0f);
            Effect.AuthoredFlameMaterial->SetScalarParameterValue(TEXT("EmissiveStrength"), bEquipmentEffect ? 1.0f : 1.6f);
            Effect.AuthoredFlameMesh->SetMaterial(0, Effect.AuthoredFlameMaterial);
        }
        UpdateAuthoredFlameGeometry(Effect);
    }
    else if (UStaticMesh* ParticleQuad = GetBillboardMesh())
    {
        UMaterial* FlameMaterial = GetProceduralFlameMaterial();
        constexpr int32 CoreParticleCount = 3;
        constexpr int32 OuterParticleCount = 5;
        Effect.FlameParticles.Reserve(CoreParticleCount + OuterParticleCount);
        for (int32 ParticleIndex = 0;
            ParticleIndex < CoreParticleCount + OuterParticleCount; ++ParticleIndex)
        {
            FProceduralFlameParticle Particle;
            Particle.bCore = ParticleIndex < CoreParticleCount;
            Particle.Age = -Effect.RandomStream.FRandRange(0.0f, Particle.bCore ? 0.55f : 0.9f);
            Particle.Visual = NewObject<UStaticMeshComponent>(this);
            Particle.Visual->SetupAttachment(this);
            Particle.Visual->SetRelativeLocation(FlameBasePosition);
            Particle.Visual->SetStaticMesh(ParticleQuad);
            Particle.Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Particle.Visual->SetCastShadow(false);
            Particle.Visual->SetTranslucentSortPriority(Particle.bCore ? 13 : 12);
            Particle.Visual->SetVisibility(false);
            Particle.Material = UMaterialInstanceDynamic::Create(FlameMaterial, this);
            Particle.Visual->SetMaterial(0, Particle.Material);
            Particle.Visual->RegisterComponent();
            Effect.FlameParticles.Add(MoveTemp(Particle));
        }
    }

    FireEffects.Add(Effect);
}

void UWowM2ParticleComponent::CreateSmokeEffect(const FM2ParticleEmitter& Emitter,
                                                const FVector& LocalPosition, UTexture2D* Texture)
{
    FSmokeEffect Effect;
    Effect.BasePosition = LocalPosition;
    Effect.LifeSpan = FMath::Clamp(Emitter.Lifespan, 0.5f, 10.0f);
    Effect.AtlasRows=FMath::Clamp<int32>(Emitter.TextureRows,1,16);
    Effect.AtlasColumns=FMath::Clamp<int32>(Emitter.TextureColumns,1,16);
    Effect.RiseVelocity = FVector(0, 0, FMath::Abs(Emitter.Gravity) * 5.0f); // Rise opposite to gravity

    // Create billboard for smoke sprite
    UStaticMesh* BillboardQuad = GetBillboardMesh();
    if (BillboardQuad)
    {
        Effect.Billboard = NewObject<UStaticMeshComponent>(this);
        Effect.Billboard->SetupAttachment(this);
        Effect.Billboard->SetRelativeLocation(LocalPosition);
        Effect.Billboard->SetRelativeScale3D(FVector(0.85f));
        Effect.Billboard->SetStaticMesh(BillboardQuad);
        Effect.Billboard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Effect.Billboard->SetCastShadow(false);

        // Create material for smoke
        UMaterial* BaseMat = GetBaseBillboardMaterial();
        if (BaseMat)
        {
            Effect.Material = UMaterialInstanceDynamic::Create(BaseMat, this);
            if (Texture) Effect.Material->SetTextureParameterValue(TEXT("EffectTexture"), Texture);
            Effect.Material->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.32f, 0.32f, 0.34f, 1.0f));
            Effect.Material->SetScalarParameterValue(TEXT("Opacity"), 0.f);
            Effect.Material->SetVectorParameterValue(TEXT("UVScale"), FLinearColor(1.f/Effect.AtlasColumns,1.f/Effect.AtlasRows,0,0));
            Effect.Material->SetScalarParameterValue(TEXT("EmissiveStrength"), 0.35f);
            Effect.Billboard->SetMaterial(0, Effect.Material);
        }

        Effect.Billboard->RegisterComponent();
    }

    SmokeEffects.Add(Effect);
}

void UWowM2ParticleComponent::CreateSparkleEffect(const FM2ParticleEmitter& Emitter,
                                                  const FVector& LocalPosition, UTexture2D* Texture)
{
    FSparkleEffect Effect;
    Effect.BasePosition = LocalPosition;
    Effect.PulseRate = FMath::Clamp(Emitter.EmissionRate / 5.0f, 1.0f, 4.0f);

    // Create billboard for sparkle
    UStaticMesh* BillboardQuad = GetBillboardMesh();
    if (BillboardQuad)
    {
        Effect.Billboard = NewObject<UStaticMeshComponent>(this);
        Effect.Billboard->SetupAttachment(this);
        Effect.Billboard->SetRelativeLocation(LocalPosition);
        Effect.Billboard->SetStaticMesh(BillboardQuad);
        Effect.Billboard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Effect.Billboard->SetCastShadow(false);

        // Set smaller scale for sparkles
        // A glowing coal is a few centimetres, while a magical lamp core is larger.
        // Preserve authored fixture size instead of turning every detail into a 30cm orb.
        const float Size=ComponentHasTag(TEXT("WowManagedFixtureLight"))?FMath::Clamp(Emitter.ScaleMiddle,.01f,1.f):.3f;
        Effect.Billboard->SetRelativeScale3D(FVector(Size));

        // Create material for sparkle
        UMaterial* BaseMat = GetBaseBillboardMaterial();
        if (BaseMat)
        {
            Effect.Material = UMaterialInstanceDynamic::Create(BaseMat, this);
            if (Texture) Effect.Material->SetTextureParameterValue(TEXT("EffectTexture"), Texture);
            // Use bright colors based on emitter data
            float R = Emitter.ColorStart[0] / 255.0f;
            float G = Emitter.ColorStart[1] / 255.0f;
            float B = Emitter.ColorStart[2] / 255.0f;
            Effect.Material->SetVectorParameterValue(TEXT("Tint"), FLinearColor(R, G, B, 1.0f));
            Effect.Material->SetScalarParameterValue(TEXT("EmissiveStrength"), 3.0f);
            // r500: one cell of a sprite atlas, not the whole sheet as a grid of dots.
            Effect.Material->SetVectorParameterValue(TEXT("UVScale"), FLinearColor(1.f / FMath::Clamp<int32>(Emitter.TextureColumns, 1, 16),
                1.f / FMath::Clamp<int32>(Emitter.TextureRows, 1, 16), 0, 0));
            Effect.Billboard->SetMaterial(0, Effect.Material);
        }

        Effect.Billboard->RegisterComponent();
    }

    SparkleEffects.Add(Effect);
}

void UWowM2ParticleComponent::UpdateFireEffect(FFireEffect& Effect, float DeltaTime)
{
    if (!Effect.Light && Effect.FlameParticles.IsEmpty())
    {
        return;
    }

    Effect.FlickerTime += DeltaTime;

    // Choose irregular light levels at irregular intervals.  Interpolating
    // between stochastic targets avoids the fixed, visibly looping sine dance.
    if (Effect.Light)
    {
        Effect.LightRetargetTime -= DeltaTime;
        if (Effect.LightRetargetTime <= 0.0f)
        {
            Effect.TargetLightScale = Effect.RandomStream.FRandRange(0.86f, 1.16f);
            Effect.LightRetargetTime = Effect.RandomStream.FRandRange(0.055f, 0.19f);
        }
        Effect.LightScale = FMath::FInterpTo(
            Effect.LightScale, Effect.TargetLightScale, DeltaTime, 11.0f);
        Effect.Light->SetIntensity(Effect.BaseIntensity * Effect.LightScale);
    }

    if (Effect.bUsesAuthoredTexture && Effect.AuthoredFlameMesh)
    {
        for (FProceduralFlameParticle& Particle : Effect.FlameParticles)
        {
            Particle.Age += DeltaTime;
            if (Particle.Lifetime <= 0.0f || Particle.Age >= Particle.Lifetime)
            {
                RespawnFlameParticle(Effect, Particle);
            }
        }
        UpdateAuthoredFlameGeometry(Effect);
        return;
    }

    for (FProceduralFlameParticle& Particle : Effect.FlameParticles)
    {
        if (!Particle.Visual || !Particle.Material)
        {
            continue;
        }

        Particle.Age += DeltaTime;
        if (Particle.Age < 0.0f)
        {
            continue;
        }
        if (Particle.Lifetime <= 0.0f || Particle.Age >= Particle.Lifetime)
        {
            RespawnFlameParticle(Effect, Particle);
        }

        const float T = FMath::Clamp(Particle.Age / Particle.Lifetime, 0.0f, 1.0f);
        const float Drift = FMath::Sin(T * 8.0f + Particle.DriftPhase) * (Effect.bCandleEffect ? .25f : Particle.bCore ? 5.0f : 9.0f);
        FVector Position = Effect.BasePosition + Particle.StartOffset
            + Particle.Velocity * Particle.Age
            + FVector(Drift, FMath::Cos(T * 6.0f + Particle.DriftPhase) * (Effect.bCandleEffect ? .15f : 3.0f), 0.0f);

        const float Grow = FMath::Clamp(T / 0.17f, 0.0f, 1.0f);
        const float Shrink = FMath::Pow(1.0f - T, 0.62f);
        const float WidthScale = FMath::Max(0.035f, Grow * Shrink);
        const float HeightScale = FMath::Max(0.06f, FMath::Lerp(0.46f, 1.0f, Grow) * Shrink);
        // One transform update instead of separate location and scale updates.
        const FVector Scale(
            Particle.BaseWidth * WidthScale,
            -Particle.BaseHeight * HeightScale,
            1.0f);
        Position.Z += Effect.bHearthEffect ? Particle.BaseHeight*HeightScale*15.f : FWowFixturePolicy::FlameCentreLift(
            Particle.BaseHeight*HeightScale*50.f,Effect.bCandleEffect,Effect.bTorchEffect,Effect.bEquipmentEffect);
        Particle.Visual->SetRelativeTransform(FTransform(
            Particle.Visual->GetRelativeRotation(), Position, Scale));

        const float FadeIn = FMath::Clamp(T / 0.07f, 0.0f, 1.0f);
        const float FadeOut = FMath::Clamp((1.0f - T) / 0.30f, 0.0f, 1.0f);
        const float Opacity = FadeIn * FadeOut * (Particle.bCore ? 0.92f : 0.72f);
        if (Effect.bUsesAuthoredTexture)
        {
            // Animate the M2's SubUV atlas. Staggering each tongue avoids a
            // synchronized flipbook while retaining the original artwork.
            const int32 FrameCount = Effect.TextureRows * Effect.TextureColumns;
            const int32 AnimatedFrame = FMath::FloorToInt(
                (Effect.FlickerTime + Particle.DriftPhase / (2.0f * PI)) * 14.0f);
            const int32 Frame = (AnimatedFrame + Particle.AtlasFrameOffset) % FrameCount;
            if (Frame != Particle.CurrentAtlasFrame)
            {
                Particle.CurrentAtlasFrame = Frame;
                const int32 Column = Frame % Effect.TextureColumns;
                const int32 Row = Frame / Effect.TextureColumns;
                Particle.Material->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(
                    static_cast<float>(Column) / static_cast<float>(Effect.TextureColumns),
                    static_cast<float>(Row) / static_cast<float>(Effect.TextureRows),
                    0.0f, 0.0f));
            }
            Particle.Material->SetVectorParameterValue(TEXT("Tint"),
                Particle.bCore
                    ? FMath::Lerp(FLinearColor(1.0f, 1.0f, 0.92f), FLinearColor(1.0f, 0.55f, 0.12f), T)
                    : FMath::Lerp(FLinearColor(1.0f, 0.82f, 0.42f), FLinearColor(0.72f, 0.10f, 0.015f), T));
            Particle.Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
            Particle.Material->SetScalarParameterValue(
                TEXT("EmissiveStrength"), Particle.bCore ? 3.2f : 2.0f);
        }
        else
        {
            const FLinearColor StartColor = Particle.bCore
                ? FLinearColor(1.0f, 0.92f, 0.38f, 1.0f)
                : FLinearColor(1.0f, 0.42f, 0.018f, 1.0f);
            const FLinearColor EndColor = Particle.bCore
                ? FLinearColor(1.0f, 0.19f, 0.008f, 1.0f)
                : FLinearColor(0.28f, 0.008f, 0.0f, 1.0f);
            Particle.Material->SetVectorParameterValue(
                TEXT("FlameColor"), FMath::Lerp(StartColor, EndColor, T));
            Particle.Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
            Particle.Material->SetScalarParameterValue(
                TEXT("EmissiveStrength"), Particle.bCore ? 8.0f : 5.0f);
        }
    }

}

void UWowM2ParticleComponent::UpdateSmokeEffect(FSmokeEffect& Effect, float DeltaTime)
{
    if (!Effect.Billboard)
    {
        return;
    }

    Effect.FadeTime = FMath::Fmod(Effect.FadeTime + DeltaTime, Effect.LifeSpan);
    const float Age = Effect.FadeTime / Effect.LifeSpan;
    Effect.Billboard->SetRelativeLocation(Effect.BasePosition + Effect.RiseVelocity * Effect.FadeTime);
    if (Effect.Material)
    {
        // Select one animation cell, not the entire sprite sheet. A soft
        // fade-in also keeps the wrap from appearing as an abrupt new puff.
        const int32 Count = Effect.AtlasRows * Effect.AtlasColumns;
        const int32 Frame = FMath::Min(int32(Age * Count), Count - 1);
        Effect.Material->SetVectorParameterValue(TEXT("UVScale"),
            FLinearColor(1.f/Effect.AtlasColumns,1.f/Effect.AtlasRows,0,0));
        Effect.Material->SetVectorParameterValue(TEXT("UVOffset"),
            FLinearColor(float(Frame%Effect.AtlasColumns)/Effect.AtlasColumns,
                         float(Frame/Effect.AtlasColumns)/Effect.AtlasRows,0,0));
        const float Fade = FMath::SmoothStep(0.f,.15f,Age) * (1.f-FMath::SmoothStep(.55f,1.f,Age));
        Effect.Material->SetScalarParameterValue(TEXT("Opacity"), Fade);
        Effect.Material->SetScalarParameterValue(TEXT("EmissiveStrength"), .35f);
    }
}

void UWowM2ParticleComponent::UpdateSparkleEffect(FSparkleEffect& Effect, float DeltaTime)
{
    if (!Effect.Billboard || !Effect.Material)
    {
        return;
    }

    Effect.PulseTime += DeltaTime;

    // Pulsing emissive intensity
    float PulseScale = 0.5f + 0.5f * FMath::Sin(Effect.PulseTime * Effect.PulseRate * PI * 2.0f);
    Effect.Material->SetScalarParameterValue(TEXT("EmissiveStrength"), 3.0f * PulseScale);
}

UStaticMesh* UWowM2ParticleComponent::GetBillboardMesh()
{
    if (BillboardMesh)
    {
        return BillboardMesh;
    }

    BillboardMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (!BillboardMesh)
    {
        UE_LOG(LogWowParticles, Warning, TEXT("Could not load Engine billboard plane"));
    }
    return BillboardMesh;
}

UMaterial* UWowM2ParticleComponent::GetProceduralFlameMaterial()
{
    if (!ProceduralFlameMaterial) ProceduralFlameMaterial = WowCookedMaterial::Load(TEXT("M_WowProceduralFlame"));
    if (ProceduralFlameMaterial)
    {
        return ProceduralFlameMaterial;
    }

#if WITH_EDITOR
    static TWeakObjectPtr<UMaterial> SharedMaterial;
    if (SharedMaterial.IsValid())
    {
        ProceduralFlameMaterial = SharedMaterial.Get();
        return ProceduralFlameMaterial;
    }

    ProceduralFlameMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
    ProceduralFlameMaterial->SetShadingModel(MSM_Unlit);
    ProceduralFlameMaterial->BlendMode = BLEND_Additive;
    ProceduralFlameMaterial->TwoSided = true;
    auto& Expressions = ProceduralFlameMaterial->GetExpressionCollection();

    auto* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(ProceduralFlameMaterial);
    Expressions.AddExpression(TexCoord);
    auto* Centre = NewObject<UMaterialExpressionConstant2Vector>(ProceduralFlameMaterial);
    Centre->R = 0.5f;
    Centre->G = 0.5f;
    Expressions.AddExpression(Centre);
    auto* CentredUv = NewObject<UMaterialExpressionSubtract>(ProceduralFlameMaterial);
    CentredUv->A.Connect(0, TexCoord);
    CentredUv->B.Connect(0, Centre);
    Expressions.AddExpression(CentredUv);
    auto* RadiusSquared = NewObject<UMaterialExpressionDotProduct>(ProceduralFlameMaterial);
    RadiusSquared->A.Connect(0, CentredUv);
    RadiusSquared->B.Connect(0, CentredUv);
    Expressions.AddExpression(RadiusSquared);
    auto* RadiusMultiplier = NewObject<UMaterialExpressionConstant>(ProceduralFlameMaterial);
    RadiusMultiplier->R = 4.6f;
    Expressions.AddExpression(RadiusMultiplier);
    auto* NormalizedRadius = NewObject<UMaterialExpressionMultiply>(ProceduralFlameMaterial);
    NormalizedRadius->A.Connect(0, RadiusSquared);
    NormalizedRadius->B.Connect(0, RadiusMultiplier);
    Expressions.AddExpression(NormalizedRadius);
    auto* InvertedRadius = NewObject<UMaterialExpressionOneMinus>(ProceduralFlameMaterial);
    InvertedRadius->Input.Connect(0, NormalizedRadius);
    Expressions.AddExpression(InvertedRadius);
    auto* RadialMask = NewObject<UMaterialExpressionSaturate>(ProceduralFlameMaterial);
    RadialMask->Input.Connect(0, InvertedRadius);
    Expressions.AddExpression(RadialMask);
    auto* SoftMask = NewObject<UMaterialExpressionMultiply>(ProceduralFlameMaterial);
    SoftMask->A.Connect(0, RadialMask);
    SoftMask->B.Connect(0, RadialMask);
    Expressions.AddExpression(SoftMask);

    auto* Color = NewObject<UMaterialExpressionVectorParameter>(ProceduralFlameMaterial);
    Color->ParameterName = TEXT("FlameColor");
    Color->DefaultValue = FLinearColor(1.0f, 0.35f, 0.01f, 1.0f);
    Expressions.AddExpression(Color);
    auto* Strength = NewObject<UMaterialExpressionScalarParameter>(ProceduralFlameMaterial);
    Strength->ParameterName = TEXT("EmissiveStrength");
    Strength->DefaultValue = 5.0f;
    Expressions.AddExpression(Strength);
    auto* ColorStrength = NewObject<UMaterialExpressionMultiply>(ProceduralFlameMaterial);
    ColorStrength->A.Connect(0, Color);
    ColorStrength->B.Connect(0, Strength);
    Expressions.AddExpression(ColorStrength);
    auto* Emissive = NewObject<UMaterialExpressionMultiply>(ProceduralFlameMaterial);
    Emissive->A.Connect(0, ColorStrength);
    Emissive->B.Connect(0, SoftMask);
    Expressions.AddExpression(Emissive);
    ProceduralFlameMaterial->GetEditorOnlyData()->EmissiveColor.Connect(0, Emissive);

    auto* Opacity = NewObject<UMaterialExpressionScalarParameter>(ProceduralFlameMaterial);
    Opacity->ParameterName = TEXT("Opacity");
    Opacity->DefaultValue = 0.8f;
    Expressions.AddExpression(Opacity);
    auto* FinalOpacity = NewObject<UMaterialExpressionMultiply>(ProceduralFlameMaterial);
    FinalOpacity->A.Connect(0, SoftMask);
    FinalOpacity->B.Connect(0, Opacity);
    Expressions.AddExpression(FinalOpacity);
    ProceduralFlameMaterial->GetEditorOnlyData()->Opacity.Connect(0, FinalOpacity);

    ProceduralFlameMaterial->PreEditChange(nullptr);
    ProceduralFlameMaterial->PostEditChange();
    ProceduralFlameMaterial->ForceRecompileForRendering();
    ProceduralFlameMaterial = WowCookedMaterial::Save(ProceduralFlameMaterial, TEXT("M_WowProceduralFlame"));
    SharedMaterial = ProceduralFlameMaterial;
#else
    ProceduralFlameMaterial = LoadObject<UMaterial>(
        nullptr, TEXT("/Engine/EngineMaterials/DefaultParticle.DefaultParticle"));
#endif
    return ProceduralFlameMaterial;
}

UMaterial* UWowM2ParticleComponent::GetBaseBillboardMaterial()
{
    if (!BaseBillboardMaterial) BaseBillboardMaterial = WowCookedMaterial::Load(TEXT("M_WowM2Billboard"));
    if (BaseBillboardMaterial)
    {
        return BaseBillboardMaterial;
    }

#if WITH_EDITOR
    // Every particle component uses the same shader graph. Creating a material
    // with the same transient object name for every WMO doodad caused Unreal to
    // replace and recompile it repeatedly, stalling the game thread for minutes
    // in large WMOs such as Stormwind.
    static TWeakObjectPtr<UMaterial> SharedEditorBillboardMaterial;
    if (SharedEditorBillboardMaterial.IsValid())
    {
        BaseBillboardMaterial = SharedEditorBillboardMaterial.Get();
        return BaseBillboardMaterial;
    }

    const FName MaterialName = MakeUniqueObjectName(
        GetTransientPackage(), UMaterial::StaticClass(), TEXT("M_WowM2ParticleRuntime"));
    BaseBillboardMaterial = NewObject<UMaterial>(GetTransientPackage(), MaterialName);
    if (BaseBillboardMaterial)
    {
        BaseBillboardMaterial->SetShadingModel(MSM_Unlit);
        BaseBillboardMaterial->BlendMode = BLEND_Additive;
        BaseBillboardMaterial->TwoSided = true;

        UTexture2D* WhiteTexture = LoadObject<UTexture2D>(nullptr,
            TEXT("/Engine/EngineResources/WhiteSquareTexture"));
        auto& Expressions = BaseBillboardMaterial->GetExpressionCollection();

        auto* TextureSample = NewObject<UMaterialExpressionTextureSampleParameter2D>(BaseBillboardMaterial);
        TextureSample->ParameterName = TEXT("EffectTexture");
        TextureSample->Texture = WhiteTexture;
        TextureSample->SamplerType = SAMPLERTYPE_Color;
        Expressions.AddExpression(TextureSample);

        auto* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(BaseBillboardMaterial);
        Expressions.AddExpression(TexCoord);

        auto* UVScale = NewObject<UMaterialExpressionVectorParameter>(BaseBillboardMaterial);
        UVScale->ParameterName = TEXT("UVScale");
        UVScale->DefaultValue = FLinearColor(1.0f, 1.0f, 0.0f, 0.0f);
        Expressions.AddExpression(UVScale);

        auto* UVScaleRG = NewObject<UMaterialExpressionComponentMask>(BaseBillboardMaterial);
        UVScaleRG->Input.Connect(0, UVScale);
        UVScaleRG->R = true;
        UVScaleRG->G = true;
        Expressions.AddExpression(UVScaleRG);

        auto* ScaledUV = NewObject<UMaterialExpressionMultiply>(BaseBillboardMaterial);
        ScaledUV->A.Connect(0, TexCoord);
        ScaledUV->B.Connect(0, UVScaleRG);
        Expressions.AddExpression(ScaledUV);

        auto* UVOffset = NewObject<UMaterialExpressionVectorParameter>(BaseBillboardMaterial);
        UVOffset->ParameterName = TEXT("UVOffset");
        UVOffset->DefaultValue = FLinearColor::Transparent;
        Expressions.AddExpression(UVOffset);

        auto* UVOffsetRG = NewObject<UMaterialExpressionComponentMask>(BaseBillboardMaterial);
        UVOffsetRG->Input.Connect(0, UVOffset);
        UVOffsetRG->R = true;
        UVOffsetRG->G = true;
        Expressions.AddExpression(UVOffsetRG);

        auto* AtlasUV = NewObject<UMaterialExpressionAdd>(BaseBillboardMaterial);
        AtlasUV->A.Connect(0, ScaledUV);
        AtlasUV->B.Connect(0, UVOffsetRG);
        Expressions.AddExpression(AtlasUV);
        TextureSample->Coordinates.Connect(0, AtlasUV);

        auto* Tint = NewObject<UMaterialExpressionVectorParameter>(BaseBillboardMaterial);
        Tint->ParameterName = TEXT("Tint");
        Tint->DefaultValue = FLinearColor::White;
        Expressions.AddExpression(Tint);

        auto* Strength = NewObject<UMaterialExpressionScalarParameter>(BaseBillboardMaterial);
        Strength->ParameterName = TEXT("EmissiveStrength");
        Strength->DefaultValue = 1.0f;
        Expressions.AddExpression(Strength);

        auto* TintedTexture = NewObject<UMaterialExpressionMultiply>(BaseBillboardMaterial);
        TintedTexture->A.Connect(0, TextureSample);
        TintedTexture->B.Connect(0, Tint);
        Expressions.AddExpression(TintedTexture);

        // Procedural flame batches carry the M2 color and alpha lifetime
        // tracks per vertex. Engine plane assets use white vertex colour, so
        // the same shared material remains valid for smoke/glow billboards.
        auto* VertexColor = NewObject<UMaterialExpressionVertexColor>(BaseBillboardMaterial);
        Expressions.AddExpression(VertexColor);

        auto* AuthoredColor = NewObject<UMaterialExpressionMultiply>(BaseBillboardMaterial);
        AuthoredColor->A.Connect(0, TintedTexture);
        AuthoredColor->B.Connect(0, VertexColor);
        Expressions.AddExpression(AuthoredColor);

        auto* Emissive = NewObject<UMaterialExpressionMultiply>(BaseBillboardMaterial);
        Emissive->A.Connect(0, AuthoredColor);
        Emissive->B.Connect(0, Strength);
        Expressions.AddExpression(Emissive);
        BaseBillboardMaterial->GetEditorOnlyData()->EmissiveColor.Connect(0, Emissive);

        auto* Opacity = NewObject<UMaterialExpressionScalarParameter>(BaseBillboardMaterial);
        Opacity->ParameterName = TEXT("Opacity");
        Opacity->DefaultValue = 1.0f;
        Expressions.AddExpression(Opacity);

        auto* TexturedOpacity = NewObject<UMaterialExpressionMultiply>(BaseBillboardMaterial);
        TexturedOpacity->A.Connect(4, TextureSample);
        TexturedOpacity->B.Connect(0, Opacity);
        Expressions.AddExpression(TexturedOpacity);

        auto* AuthoredOpacity = NewObject<UMaterialExpressionMultiply>(BaseBillboardMaterial);
        AuthoredOpacity->A.Connect(0, TexturedOpacity);
        AuthoredOpacity->B.Connect(4, VertexColor);
        Expressions.AddExpression(AuthoredOpacity);
        BaseBillboardMaterial->GetEditorOnlyData()->Opacity.Connect(0, AuthoredOpacity);

        BaseBillboardMaterial->PreEditChange(nullptr);
        BaseBillboardMaterial->PostEditChange();
        BaseBillboardMaterial->ForceRecompileForRendering();
    BaseBillboardMaterial = WowCookedMaterial::Save(BaseBillboardMaterial, TEXT("M_WowM2Billboard"));
        SharedEditorBillboardMaterial = BaseBillboardMaterial;
    }
#else
    BaseBillboardMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
#endif

    return BaseBillboardMaterial;
}

void UWowM2ParticleComponent::CreateHolyLightVeil(float CharacterHeight)
{
    // Replace the old continuous shell with separated, depth-tested motes.
    VeilHeight=FMath::Clamp(CharacterHeight,40.f,1000.f); VeilAge=0;
    SetAbsolute(false,true,true); SetWorldRotation(FRotator::ZeroRotator); SetWorldScale3D(FVector::OneVector);
    HolyLightVeil=NewObject<UProceduralMeshComponent>(this);
    HolyLightVeil->SetupAttachment(this); HolyLightVeil->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HolyLightVeil->SetCastShadow(false);
    HolyLightVeilMaterial=UMaterialInstanceDynamic::Create(GetProceduralFlameMaterial(),this);
    HolyLightVeilMaterial->SetVectorParameterValue(TEXT("FlameColor"),FLinearColor(1.f,.82f,.32f));
    HolyLightVeilMaterial->SetScalarParameterValue(TEXT("EmissiveStrength"),2.f);
    HolyLightVeil->SetMaterial(0,HolyLightVeilMaterial); HolyLightVeil->RegisterComponent();
    for(auto& Smoke:SmokeEffects) if(Smoke.Billboard) Smoke.Billboard->SetVisibility(false);
    for(auto& Spark:SparkleEffects) if(Spark.Billboard) Spark.Billboard->SetVisibility(false);
    UpdateHolyLightVeil(0);
}

namespace
{
// Additive star glint for usable-object sparkles: tiny hot core, four long and four
// short rays, tinted and faded by vertex colour (alpha = brightness). No texture.
UMaterial* GetGlintMaterial()
{
    static TWeakObjectPtr<UMaterial> Cached;
    if (Cached.IsValid()) return Cached.Get();
    UMaterial* Material = WowCookedMaterial::Load(TEXT("M_WowSparkleGlint"));
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage());
        Material->SetShadingModel(MSM_Unlit);
        Material->BlendMode = BLEND_Additive;
        Material->TwoSided = true;
        auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(Material);
        auto* Color = NewObject<UMaterialExpressionVertexColor>(Material);
        auto* Shader = NewObject<UMaterialExpressionCustom>(Material);
        Shader->OutputType = CMOT_Float3;
        Shader->Inputs.Reset();
        FCustomInput InUV; InUV.InputName = TEXT("UV"); InUV.Input.Connect(0, UV); Shader->Inputs.Add(InUV);
        FCustomInput InTint; InTint.InputName = TEXT("Tint"); InTint.Input.Connect(0, Color); Shader->Inputs.Add(InTint);
        // Vertex colour output 0 is RGB only; alpha (brightness) comes from output 4.
        FCustomInput InAlpha; InAlpha.InputName = TEXT("TintAlpha"); InAlpha.Input.Connect(4, Color); Shader->Inputs.Add(InAlpha);
        Shader->Code = TEXT(R"(
float2 p = UV * 2 - 1;
float r = length(p);
float core = exp(-r * r * 60);
float rays = exp(-abs(p.x) * 34) * exp(-abs(p.y) * 2.6) + exp(-abs(p.y) * 34) * exp(-abs(p.x) * 2.6);
float diag = (exp(-abs(p.x - p.y) * 30) + exp(-abs(p.x + p.y) * 30)) * exp(-r * 4.5) * 0.4;
float halo = exp(-r * r * 9) * 0.25;
float v = (core * 2.0 + rays * 0.9 + diag + halo) * (1 - smoothstep(0.85, 1.0, r));
return Tint.rgb * v * 3.0 * TintAlpha;
)");
        for (UMaterialExpression* Expression : TArray<UMaterialExpression*>{UV, Color, Shader})
            Material->GetExpressionCollection().AddExpression(Expression);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Shader);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, TEXT("M_WowSparkleGlint"));
    }
#endif
    Cached = Material;
    return Material;
}
}

void UWowM2ParticleComponent::CreateQuestSparkles(float Height)
{
    CreateHolyLightVeil(Height);
    bLoopQuestSparkles = true;
    // Glittering dust, not the flame shader the Holy Light veil uses.
    if (UMaterial* Glint = GetGlintMaterial())
    {
        HolyLightVeilMaterial = UMaterialInstanceDynamic::Create(Glint, this);
        HolyLightVeil->SetMaterial(0, HolyLightVeilMaterial);
    }
    PrimaryComponentTick.TickInterval = 1.0f / 30.0f;
}

void UWowM2ParticleComponent::UpdateHolyLightVeil(float DeltaTime)
{
    if (bLoopQuestSparkles)
    {
        // Quest/usable glitter: short-lived four-point glints scattered over the
        // object that flash on and off independently, rather than the drifting
        // round motes of the Holy Light veil (which read as bubbles).
        VeilAge += DeltaTime;
        HolyLightVeilMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.95f);
        const auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);
        const FRotator View=Camera ? Camera->GetCameraRotation() : FRotator::ZeroRotator;
        const FVector Right=FRotationMatrix(View).GetUnitAxis(EAxis::Y), Up=FRotationMatrix(View).GetUnitAxis(EAxis::Z);
        TArray<FVector> Vertices,Normals; TArray<FVector2D> UVs; TArray<FLinearColor> Colors; TArray<int32> Triangles;
        constexpr int32 Count = 14;
        constexpr float Period = 0.9f;
        for (int32 I = 0; I < Count; ++I)
        {
            const float Phase = VeilAge / Period + I * 0.618034f;
            const int32 Cycle = FMath::FloorToInt(Phase);
            const float T = Phase - Cycle;
            // Bright for roughly a third of each cycle, with a sharp peak.
            const float Pulse = T < 0.35f ? FMath::Pow(FMath::Sin(T / 0.35f * UE_PI), 2.f) : 0.f;
            if (Pulse <= 0.02f) continue;
            // New position every cycle, deterministic per sparkle.
            const uint32 Seed = static_cast<uint32>(I * 7919 + Cycle * 104729);
            auto Rand = [Seed](uint32 Salt) { return float(((Seed ^ (Salt * 2654435761u)) * 2246822519u) >> 8) / float(1 << 24); };
            const float Angle = Rand(1) * 2.f * UE_PI;
            const float Radius = VeilHeight * (0.12f + 0.33f * Rand(2));
            const FVector Center(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, VeilHeight * (-0.35f + 0.8f * Rand(3)));
            const float Size = VeilHeight * (0.05f + 0.035f * Rand(4));
            const FLinearColor Color(1.f, 0.93f + 0.07f * Rand(5), 0.72f + 0.28f * Rand(6), Pulse);
            const int32 Base = Vertices.Num();
            Vertices.Append({Center - Right * Size - Up * Size, Center + Right * Size - Up * Size, Center + Right * Size + Up * Size, Center - Right * Size + Up * Size});
            UVs.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
            for (int32 J = 0; J < 4; ++J) { Normals.Add(-View.Vector()); Colors.Add(Color); }
            Triangles.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
        }
        // Fine dust: small motes drifting slowly upwards, twinkling.
        for (int32 I = 0; I < 12; ++I)
        {
            const float Life = 3.2f;
            const float Phase = VeilAge / Life + I * 0.381966f;
            const int32 Cycle = FMath::FloorToInt(Phase);
            const float T = Phase - Cycle;
            const uint32 Seed = static_cast<uint32>(I * 15485863 + Cycle * 32452843);
            auto Rand = [Seed](uint32 Salt) { return float(((Seed ^ (Salt * 2654435761u)) * 2246822519u) >> 8) / float(1 << 24); };
            const float Angle = Rand(1) * 2.f * UE_PI + T * 0.6f;
            const float Radius = VeilHeight * (0.1f + 0.3f * Rand(2));
            const FVector Center(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, VeilHeight * (-0.4f + 0.5f * Rand(3) + 0.35f * T));
            const float Twinkle = 0.45f + 0.55f * FMath::Square(FMath::Sin(VeilAge * (5.f + 4.f * Rand(4)) + I));
            const float Fade = FMath::Sin(T * UE_PI);
            const float Size = VeilHeight * 0.022f;
            const FLinearColor Color(1.f, 0.9f, 0.62f, 0.55f * Twinkle * Fade);
            const int32 Base = Vertices.Num();
            Vertices.Append({Center - Right * Size - Up * Size, Center + Right * Size - Up * Size, Center + Right * Size + Up * Size, Center - Right * Size + Up * Size});
            UVs.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
            for (int32 J = 0; J < 4; ++J) { Normals.Add(-View.Vector()); Colors.Add(Color); }
            Triangles.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
        }
        if (Vertices.Num() == 0)
        {
            // Keep a degenerate section so the mesh can be updated next tick.
            Vertices = {FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector};
            Normals = {FVector::UpVector, FVector::UpVector, FVector::UpVector};
            UVs = {FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector};
            Colors = {FLinearColor::Transparent, FLinearColor::Transparent, FLinearColor::Transparent};
            Triangles = {0, 1, 2};
        }
        // Topology changes as glints appear and fade, so rebuild the section.
        HolyLightVeil->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, {}, false);
        HolyLightVeil->SetVisibility(true);
        return;
    }
    VeilAge+=DeltaTime;
    if(VeilAge>=1.2f) { HolyLightVeil->SetVisibility(false); return; }
    const float Fade=FMath::Clamp(VeilAge/.08f,0.f,1.f)*FMath::Clamp((1.2f-VeilAge)/.65f,0.f,1.f);
    HolyLightVeilMaterial->SetScalarParameterValue(TEXT("Opacity"),Fade*.7f);
    const auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);
    const FRotator View=Camera ? Camera->GetCameraRotation() : FRotator::ZeroRotator;
    const FVector Right=FRotationMatrix(View).GetUnitAxis(EAxis::Y), Up=FRotationMatrix(View).GetUnitAxis(EAxis::Z);
    TArray<FVector> Vertices,Normals; TArray<FVector2D> UVs; TArray<FLinearColor> Colors; TArray<int32> Triangles;
    for(int32 I=0;I<18;++I)
    {
        const float Angle=I*2.399963f+VeilAge*.65f;
        const float Radius=VeilHeight*(.12f+.07f*FMath::Sin(float(I)*3.f));
        const FVector Center(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,
            VeilHeight*(-.75f+.9f*FMath::Frac(I*.618034f)+VeilAge*.12f));
        const float Size=VeilHeight*(.009f+.004f*(I%3));
        const int32 Base=Vertices.Num();
        Vertices.Append({Center-Right*Size-Up*Size,Center+Right*Size-Up*Size,Center+Right*Size+Up*Size,Center-Right*Size+Up*Size});
        UVs.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
        for(int32 J=0;J<4;++J){ Normals.Add(-View.Vector());Colors.Add(FLinearColor::White); }
        Triangles.Append({Base,Base+1,Base+2,Base,Base+2,Base+3});
    }
    if(!HolyLightVeil->GetProcMeshSection(0)) HolyLightVeil->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UVs,Colors,{},false);
    else HolyLightVeil->UpdateMeshSection_LinearColor(0,Vertices,Normals,UVs,Colors,{});
}

void UWowM2ParticleComponent::PrepareStandaloneMaterials()
{
    GetProceduralFlameMaterial();
    GetBaseBillboardMaterial();
    GetGlintMaterial();
    GetEmitterMaterial(true);
    GetEmitterMaterial(false);
    UWowActorFadeComponent::PrepareMaterial(); // r374: despawn fade (M_WowFadeOut_v1)
}


float UWowM2ParticleComponent::GetNearbyFireHeat(UWorld* World, const FVector& Position, const AActor* IgnoreActor)
{
    if (!World) return 0;
    float Heat=0;
    for(TObjectIterator<UWowM2ParticleComponent> It; It; ++It)
    {
        if(It->GetWorld()!=World || !It->IsRegistered() || !It->IsVisible()) continue;
        for(const auto& Fire : It->FireEffects)
        {
            if(Fire.bEquipmentEffect || !IsValid(Fire.Light) || !Fire.Light->IsVisible() || Fire.Light->Intensity<=0) continue;
            const FVector Source=Fire.Light->GetComponentLocation();
            const float Candidate=FMath::Clamp(1.f-float(FVector::Distance(Source,Position))/450.f,0.f,1.f);
            if(Candidate<=Heat) continue;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(LensFireHeat),true);
            if(IgnoreActor) Params.AddIgnoredActor(IgnoreActor);
            if(It->GetOwner()) Params.AddIgnoredActor(It->GetOwner());
            FHitResult Hit;
            if(!World->LineTraceSingleByObjectType(Hit,Position,Source,FCollisionObjectQueryParams(ECC_WorldStatic),Params)) Heat=Candidate;
        }
    }
    return Heat;
}
