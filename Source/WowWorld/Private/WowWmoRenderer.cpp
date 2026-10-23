#include "WowWmoRenderer.h"
#include "WowLampFixtures.h"
#include "WowFixturePolicy.h"
#include "WowWmoCollision.h"
#include "WowWmoBackdropVisibility.h"
#include "WowWmoPortalCulling.h"
#include "WowRuntimeMeshMaterials.h"
#include "WowRuntimeLumen.h"
#include "WowRuntimeStaticMesh.h"
#include "WowDoodadManager.h"
#include "WowWaterMaterial.h"
#include "WowWaterRenderer.h"
#include "Mpq/MpqManager.h"
#include "WowAssetCache.h"
#include "Formats/WmoParser.h"
#include "Formats/WmoTypes.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/BlpParser.h"
#include "Formats/BlpTypes.h"
#include "WowTextureFactory.h"
#include "WowTerrainMaterial.h"
#include "WowM2ParticleComponent.h"
#include "Coord/WowCoordinate.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Formats/AdtTypes.h"
#include "Formats/M2Types.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodySetup.h"
#include "Containers/Ticker.h"
#include "WowClientPreferences.h"
#include "Misc/Paths.h"
#include "WowIndoorFogMask.h"
#include "WowClothMaterial.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowWmo, Log, All);

UMaterial* WowGetCobblestonePomMaterial(); // r704: WowCobblestoneMaterial.cpp (outside the anonymous namespace)

namespace
{
	FVector WmoLocalToUELocal(const FVector& Position)
	{
		return FVector(
			Position.Y * FWowCoordinate::SCALE,
			Position.X * FWowCoordinate::SCALE,
			Position.Z * FWowCoordinate::SCALE);
	}

	FVector WmoAxisToUEAxis(const FVector& Axis)
	{
		return FVector(Axis.Y, Axis.X, Axis.Z);
	}

	FQuat WmoDoodadRotationToUELocal(const FQuat& WowRotation)
	{
		FQuat Source = WowRotation;
		if (!Source.IsNormalized())
		{
			Source.Normalize();
		}

		if (Source.ContainsNaN() || Source.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			return FQuat::Identity;
		}

		// WMO and M2 vertex conversion swaps X/Y into UE local space. A component's
		// UE local X axis therefore corresponds to the original WoW Y axis, UE Y
		// corresponds to WoW X, and UE Z remains WoW Z.
		const FVector UEXAxis = WmoAxisToUEAxis(Source.RotateVector(FVector::YAxisVector)).GetSafeNormal();
		const FVector UEZAxis = WmoAxisToUEAxis(Source.RotateVector(FVector::ZAxisVector)).GetSafeNormal();
		if (UEXAxis.IsNearlyZero() || UEZAxis.IsNearlyZero())
		{
			return FQuat::Identity;
		}

		return FRotationMatrix::MakeFromXZ(UEXAxis, UEZAxis).ToQuat();
	}

	FString NormalizeWmoDoodadPath(FString Path)
	{
		Path.TrimStartAndEndInline();
		Path.ReplaceInline(TEXT("/"), TEXT("\\"));
		if (Path.EndsWith(TEXT(".mdx"), ESearchCase::IgnoreCase) ||
			Path.EndsWith(TEXT(".mdl"), ESearchCase::IgnoreCase))
		{
			Path = Path.LeftChop(4) + TEXT(".m2");
		}
		return Path;
	}

	bool IsSkippableEmbeddedDoodad(const FString& Path)
	{
		FString Lower = Path.ToLower();
		Lower.ReplaceInline(TEXT("/"), TEXT("\\"));
		return Lower.Contains(TEXT("\\waterbasin\\")) ||
			Lower.Contains(TEXT("waterbasin")) ||
			Lower.Contains(TEXT("\\waterfall\\newwaterfall")) ||
			Lower.Contains(TEXT("newwaterfall")) ||
			Lower.Contains(TEXT("lightshaft")) ||
			Lower.Contains(TEXT("volumetriclight"));
	}

	bool ShouldEmbeddedDoodadBlockPlayer(const FString& Path)
	{
		FString Lower = Path.ToLower();
		Lower.ReplaceInline(TEXT("/"), TEXT("\\"));
		if (Lower.Contains(TEXT("bush")) ||
			Lower.Contains(TEXT("grass")) ||
			Lower.Contains(TEXT("flower")) ||
			Lower.Contains(TEXT("fern")) ||
			Lower.Contains(TEXT("lilypad")) ||
			Lower.Contains(TEXT("leaves")) ||
			Lower.Contains(TEXT("lightshaft")) ||
			Lower.Contains(TEXT("volumetriclight")) ||
			Lower.Contains(TEXT("\\particleemitters\\")) ||
			Lower.Contains(TEXT("\\effects\\")) ||
			Lower.Contains(TEXT("flameonly")) ||
			Lower.Contains(TEXT("fireonly")) ||
			Lower.Contains(TEXT("smoke")) ||
			Lower.Contains(TEXT("glow")) ||
			Lower.Contains(TEXT("lightbeam")) ||
			Lower.Contains(TEXT("mist")) ||
			Lower.Contains(TEXT("steam")) ||
			Lower.Contains(TEXT("splash")))
		{
			return false;
		}

		return Lower.Contains(TEXT("tree")) ||
			Lower.Contains(TEXT("trunk")) ||
			Lower.Contains(TEXT("stump")) ||
			Lower.Contains(TEXT("rock")) ||
			Lower.Contains(TEXT("boulder")) ||
			Lower.Contains(TEXT("crate")) ||
			Lower.Contains(TEXT("barrel")) ||
			Lower.Contains(TEXT("wagon")) ||
			Lower.Contains(TEXT("cart")) ||
			Lower.Contains(TEXT("fence")) ||
			Lower.Contains(TEXT("post")) ||
			Lower.Contains(TEXT("bench")) ||
			Lower.Contains(TEXT("chair")) ||
			Lower.Contains(TEXT("table")) ||
			Lower.Contains(TEXT("shelf")) ||
			Lower.Contains(TEXT("bookcase")) ||
			Lower.Contains(TEXT("cupboard")) ||
			Lower.Contains(TEXT("cabinet")) ||
			Lower.Contains(TEXT("chest")) ||
			Lower.Contains(TEXT("bed")) ||
			Lower.Contains(TEXT("anvil")) ||
			Lower.Contains(TEXT("forge")) ||
			Lower.Contains(TEXT("weaponrack")) ||
			Lower.Contains(TEXT("armorstand")) ||
			Lower.Contains(TEXT("well")) ||
			Lower.Contains(TEXT("tent")) ||
			Lower.Contains(TEXT("pillar")) ||
			Lower.Contains(TEXT("column")) ||
			Lower.Contains(TEXT("statue")) ||
			Lower.Contains(TEXT("lantern")) ||
			Lower.Contains(TEXT("streetlamp")) ||
			Lower.Contains(TEXT("fixture")) ||
			Lower.Contains(TEXT("sconce")) ||
			Lower.Contains(TEXT("lanternmount")) ||
			Lower.Contains(TEXT("torch")) ||
			Lower.Contains(TEXT("brazier")) ||
			Lower.Contains(TEXT("pyre")) ||
			Lower.Contains(TEXT("hearth")) ||
			Lower.Contains(TEXT("fireplace")) ||
			Lower.Contains(TEXT("candelabra")) ||
			Lower.Contains(TEXT("chandelier")) ||
			Lower.Contains(TEXT("firepit")) ||
			Lower.Contains(TEXT("campfire"));
	}

	void ConfigureEmbeddedDoodadCollision(UStaticMeshComponent* Component, const FString& Path)
	{
		if (!Component)
		{
			return;
		}

		if (!ShouldEmbeddedDoodadBlockPlayer(Path))
		{
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			return;
		}

		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionObjectType(ECC_WorldStatic);
		Component->SetCollisionResponseToAllChannels(ECR_Block);
		Component->CanCharacterStepUpOn = ECB_No;
	}

	bool ShouldEmbeddedDoodadEmitPointLight(const FString& Path)
	{
		FString Lower = Path.ToLower();
		Lower.ReplaceInline(TEXT("/"), TEXT("\\"));

		if (Lower.Contains(TEXT("lightshaft")) ||
			Lower.Contains(TEXT("volumetriclight")) ||
			Lower.Contains(TEXT("\\particleemitters\\")))
		{
			return false;
		}

		return Lower.Contains(TEXT("\\lights\\")) ||
			Lower.Contains(TEXT("\\lamps\\")) ||
			Lower.Contains(TEXT("\\fire\\")) ||
			Lower.Contains(TEXT("campfire")) ||
			Lower.Contains(TEXT("firepit")) ||
			Lower.Contains(TEXT("bonfire")) ||
			Lower.Contains(TEXT("lantern")) ||
			Lower.Contains(TEXT("streetlamp")) ||
			Lower.Contains(TEXT("torch")) ||
			Lower.Contains(TEXT("candle")) ||
			Lower.Contains(TEXT("candelabra")) ||
			Lower.Contains(TEXT("brazier")) ||
			Lower.Contains(TEXT("chandelier")) ||
			Lower.Contains(TEXT("lamp"));
	}

	void CreateEmbeddedDoodadPointLight(AActor* WmoActor, UStaticMeshComponent* DoodadComp,
		const FString& DoodadPath, uint32 UniqueId, int32 DefIndex)
	{
		if (WmoActor && DoodadComp && !ShouldEmbeddedDoodadEmitPointLight(DoodadPath))
		{
			// r431: no fire/lamp light of its own - our light where WoW drew its flat glow card.
			FWowDoodadManager::AddLegacyGlowLights(WmoActor, DoodadComp, DoodadPath, UniqueId, 100000 + DefIndex);
			return;
		}
		if (!WmoActor || !DoodadComp || !ShouldEmbeddedDoodadEmitPointLight(DoodadPath) || FWowFixturePolicy::IsUnlit(DoodadPath))
		{
			return;
		}

		FString Path = DoodadPath.ToLower();
		Path.ReplaceInline(TEXT("/"), TEXT("\\"));

		float Intensity = 4200.0f;
		float Radius = 1100.0f;
		if (Path.Contains(TEXT("chandelier")) || Path.Contains(TEXT("streetlamp")))
		{
			Intensity = 7000.0f;
			Radius = 1700.0f;
		}
		else if (Path.Contains(TEXT("torch")) || Path.Contains(TEXT("brazier")))
		{
			Intensity = 5600.0f;
			Radius = 1350.0f;
		}
		else if (Path.Contains(TEXT("candle")))
		{
			Intensity = 1500.0f;
			Radius = 650.0f;
		}

		const FName LightName = *FString::Printf(TEXT("WmoDoodadLight_%u_%d"), UniqueId, DefIndex);
		UPointLightComponent* LightComp = NewObject<UPointLightComponent>(WmoActor, LightName);
		if (!LightComp)
		{
			return;
		}

		LightComp->SetupAttachment(DoodadComp);
		WmoActor->AddInstanceComponent(LightComp);
		LightComp->SetMobility(EComponentMobility::Movable);
		LightComp->SetIntensity(Intensity * FWowClientPreferences::GetFireLightIntensityScale());
		LightComp->SetAttenuationRadius(Radius * FWowClientPreferences::GetFireLightRadiusScale());
		LightComp->SetLightColor(FLinearColor(1.0f, 0.66f, 0.34f));
		LightComp->SetCastShadows(true);
        // Imported point lights use raster shadows, including translucent wet
        // surfaces. Do not inject unshadowed fog light or approximate Lumen GI
        // from interior flames through thin original building walls.
        LightComp->SetCastRaytracedShadows(ECastRayTracedShadow::Disabled);
        LightComp->SetCastVolumetricShadow(true);
        LightComp->SetVolumetricScatteringIntensity(0.0f);
        LightComp->SetIndirectLightingIntensity(FWowClientPreferences::GetFireLightIndirectScale());
        LightComp->ShadowResolutionScale=.5f;
        // r352 (Stefan: shadows still razor sharp under the lamps): every fire light gets the lamp
        // fixtures' soft area source (r321/r322), not only WowLampFixtures.
        LightComp->SetSourceRadius(8.f*FWowClientPreferences::GetFireLightSoftness());
        LightComp->SetSoftSourceRadius(12.f*FWowClientPreferences::GetFireLightSoftness());
        LightComp->SetSpecularScale(.35f);
        // r701 (Stefan, Tanaris at night: a ship's hull went dark further out while its flames stayed - "these
        // ships are showing lights ... But disappear at longer ranges"): the lamp light ended at 50 m, so the
        // hull it lit vanished into the night. Ship lamps light their ship out to 200 m.
        const bool bShipLight = WmoActor && WmoActor->ActorHasTag(TEXT("WowShipWmo"));
        LightComp->SetMaxDrawDistance(bShipLight ? 20000.f : 5000.f);
        LightComp->MaxDistanceFadeRange = bShipLight ? 4000.f : 1000.f;
		// r355 (Stefan: hanging lanterns glowed but lit nothing): the +120 cm suits standing fixtures; a
		// hanging lantern hangs DOWN from its pivot, so the light sat in the beam above, and the lantern's
		// own base and roof shadowed the rest. Light it from the lantern body, which casts no shadow.
		const bool bHanging = Path.Contains(TEXT("hanging"));
		if (bHanging && DoodadComp->GetStaticMesh())
		{
			LightComp->SetRelativeLocation(DoodadComp->GetStaticMesh()->GetBounds().Origin);
			DoodadComp->SetCastShadow(false);
		}
		else
			LightComp->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
		LightComp->RegisterComponent();
	}

	float ConvertWmoLightRadiusToUE(const FWmoLight& Light)
	{
		float RadiusInWowUnits = FMath::Max(Light.AttenuationEnd, Light.AttenuationStart);
		if (RadiusInWowUnits <= KINDA_SMALL_NUMBER)
		{
			RadiusInWowUnits = 18.0f;
		}

		return FMath::Clamp(RadiusInWowUnits * FWowCoordinate::SCALE, 400.0f, 6500.0f);
	}

	float ConvertWmoLightIntensityToUE(const FWmoLight& Light)
	{
		const float AuthoredIntensity = FMath::Max(0.08f, FMath::Abs(Light.Intensity));
		const float RadiusBoost = FMath::Clamp(ConvertWmoLightRadiusToUE(Light) / 1800.0f, 0.65f, 2.2f);
		return FMath::Clamp(AuthoredIntensity * 4200.0f * RadiusBoost, 350.0f, 22000.0f);
	}

	FLinearColor ConvertWmoLightColor(const FWmoLight& Light)
	{
		const bool bHasAuthoredColor =
			Light.Color.R > 0 || Light.Color.G > 0 || Light.Color.B > 0;
		if (!bHasAuthoredColor)
		{
			return FLinearColor(1.0f, 0.72f, 0.42f);
		}

		FLinearColor Color = FLinearColor::FromSRGBColor(Light.Color);
		Color.A = 1.0f;
		return Color;
	}

	int32 SpawnWmoRootLights(AActor* WmoActor, USceneComponent* RootComp,
		const FWmoRootData& RootData, const FString& WmoPath, uint32 UniqueId)
	{
		if (!WmoActor || !RootComp || RootData.Lights.Num() == 0)
		{
			return 0;
		}

        TArray<UPointLightComponent*> FixtureLights;
        WmoActor->GetComponents(FixtureLights);
		int32 Spawned = 0;
		for (int32 LightIndex = 0; LightIndex < RootData.Lights.Num(); ++LightIndex)
		{
			const FWmoLight& Light = RootData.Lights[LightIndex];
            const FVector WorldPosition=RootComp->GetComponentTransform().TransformPosition(WmoLocalToUELocal(Light.Position));
            bool bHasFixture=false;
            for (const auto* Fixture:FixtureLights)
                if (Fixture->ComponentHasTag(TEXT("WowFixtureLight")) &&
                    FVector::DistSquared(Fixture->GetComponentLocation(),WorldPosition)<FMath::Square(150.f))
                    { bHasFixture=true;break; }
            if (bHasFixture) continue; // Replaced by the correctly anchored, scheduled fixture.

			const FName LightName = *FString::Printf(TEXT("WmoRootLight_%u_%d"), UniqueId, LightIndex);
			UPointLightComponent* LightComp = NewObject<UPointLightComponent>(WmoActor, LightName);
			if (!LightComp)
			{
				continue;
			}

			LightComp->SetupAttachment(RootComp);
			WmoActor->AddInstanceComponent(LightComp);
			LightComp->SetMobility(EComponentMobility::Movable);
			LightComp->SetRelativeLocation(WmoLocalToUELocal(Light.Position));
			LightComp->SetLightColor(ConvertWmoLightColor(Light));
			LightComp->SetIntensity(ConvertWmoLightIntensityToUE(Light) * FWowClientPreferences::GetFireLightIntensityScale());
			LightComp->SetAttenuationRadius(ConvertWmoLightRadiusToUE(Light) * FWowClientPreferences::GetFireLightRadiusScale());
			LightComp->SetCastShadows(true);
        // Imported point lights use raster shadows, including translucent wet
        // surfaces. Do not inject unshadowed fog light or approximate Lumen GI
        // from interior flames through thin original building walls.
        LightComp->SetCastRaytracedShadows(ECastRayTracedShadow::Disabled);
        LightComp->SetCastVolumetricShadow(true);
        LightComp->SetVolumetricScatteringIntensity(0.0f);
        LightComp->SetIndirectLightingIntensity(FWowClientPreferences::GetFireLightIndirectScale());
        LightComp->ShadowResolutionScale=.5f;
        // r352 (Stefan: shadows still razor sharp under the lamps): every fire light gets the lamp
        // fixtures' soft area source (r321/r322), not only WowLampFixtures.
        LightComp->SetSourceRadius(8.f*FWowClientPreferences::GetFireLightSoftness());
        LightComp->SetSoftSourceRadius(12.f*FWowClientPreferences::GetFireLightSoftness());
        LightComp->SetSpecularScale(.35f);
        // r701 (Stefan, Tanaris at night: a ship's hull went dark further out while its flames stayed - "these
        // ships are showing lights ... But disappear at longer ranges"): the lamp light ended at 50 m, so the
        // hull it lit vanished into the night. Ship lamps light their ship out to 200 m.
        const bool bShipLight = WmoActor && WmoActor->ActorHasTag(TEXT("WowShipWmo"));
        LightComp->SetMaxDrawDistance(bShipLight ? 20000.f : 5000.f);
        LightComp->MaxDistanceFadeRange = bShipLight ? 4000.f : 1000.f;
			LightComp->RegisterComponent();
			++Spawned;
		}

		UE_LOG(LogWowWmo, Log, TEXT("  Spawned %d authored WMO lights for %s"),
			Spawned, *WmoPath);
		return Spawned;
	}

	bool ShouldUseFoliageMaterialForWmo(const FWmoMaterial& WmoMat)
	{
		FString Texture = WmoMat.TexturePath1.ToLower();
		Texture.ReplaceInline(TEXT("/"), TEXT("\\"));
	return (Texture.Contains(TEXT("tree")) && !Texture.Contains(TEXT("street"))) ||
		Texture.Contains(TEXT("leaf")) ||
		Texture.Contains(TEXT("leaves")) ||
		Texture.Contains(TEXT("branch")) ||
		Texture.Contains(TEXT("canopy")) ||
		Texture.Contains(TEXT("foliage")) ||
		Texture.Contains(TEXT("bush")) ||
		Texture.Contains(TEXT("vine"));
}

bool IsLikelyTransparentWmoEffectMaterial(const FString& Texture)
{
	return Texture.Contains(TEXT("water")) ||
		Texture.Contains(TEXT("waterfall")) ||
		Texture.Contains(TEXT("lava")) ||
		Texture.Contains(TEXT("slime")) ||
		Texture.Contains(TEXT("glass")) ||
		Texture.Contains(TEXT("window")) ||
		Texture.Contains(TEXT("glow")) ||
		Texture.Contains(TEXT("beam")) ||
		Texture.Contains(TEXT("fx")) ||
		Texture.Contains(TEXT("fire")) ||
		Texture.Contains(TEXT("smoke")) ||
		Texture.Contains(TEXT("portal")) ||
		Texture.Contains(TEXT("alpha")) ||
		Texture.Contains(TEXT("lightshaft")) ||
		Texture.Contains(TEXT("volumetric"));
}

bool ShouldForceOpaqueBuildingMaterialForWmo(const FWmoMaterial& WmoMat)
{
	FString Texture = WmoMat.TexturePath1.ToLower();
	Texture.ReplaceInline(TEXT("/"), TEXT("\\"));
	if (Texture.IsEmpty() || IsLikelyTransparentWmoEffectMaterial(Texture))
	{
		return false;
	}
	// Two-sided alpha-keyed cards (hides, banners, hut skins) rely on the texture's
	// alpha cut-out; forcing them opaque shows the magenta key colour as solid planes.
	if (WmoMat.BlendMode == 1 && (WmoMat.Flags & 0x04u) != 0)
	{
		return false;
	}

	return Texture.Contains(TEXT("stormwind")) ||
		Texture.Contains(TEXT("human")) ||
		Texture.Contains(TEXT("building")) ||
		Texture.Contains(TEXT("city")) ||
		Texture.Contains(TEXT("castle")) ||
		Texture.Contains(TEXT("tower")) ||
		Texture.Contains(TEXT("wall")) ||
		Texture.Contains(TEXT("stone")) ||
		Texture.Contains(TEXT("brick")) ||
		Texture.Contains(TEXT("wood")) ||
		Texture.Contains(TEXT("plank")) ||
		Texture.Contains(TEXT("beam")) ||
		Texture.Contains(TEXT("trim")) ||
		Texture.Contains(TEXT("roof")) ||
		Texture.Contains(TEXT("tile")) ||
		Texture.Contains(TEXT("floor")) ||
		Texture.Contains(TEXT("door")) ||
		Texture.Contains(TEXT("bridge"));
}

// r704: Stormwind's cobbled streets and sidewalks (WMO floor textures MM_STREET_02/03, MM_HSTREET_01,
// MM_MSTREET_01, MM_MM_SIDEWALK_01, MM_MM_HSIDEWALK_01, MM_MSIDEWALK_01) get the parallax cobblestone material.
// r717 (Stefan, Stormwind's stone walls and the flagstone floors: "These aren't really in 3d either. Where did you
// do it?", "Not these."): r704 only took the street and sidewalk floors. Stone, brick and flagstone floors (indoor
// ones too) and stone and brick walls get the relief now; wood, plaster and the rest keep the plain material.
// r731 (Stefan 22.10: "Add it all to the cobblestone and bricks. I still haven't actually seen it in game yet"):
// the r717 rule only looked in the WALLS and FLOOR folders and took any STRMWND name, so every brick texture in
// DUNGEONS\TEXTURES\BRICK (the inn's red brick, the abbey and crypt bricks, MM_GREYBRICK) never got the relief, while
// Stormwind's timber-and-plaster walls, the wooden and marble-checker interior floors and a grass "sidewalk" did. The
// textures were checked by eye (22.10): bricks, stone blocks, flagstones and cobbles get it now; timber frame, plaster,
// wood, ivy, marble tiles, mosaics, lava, roof slates, trims and carvings do not.
bool IsCobblestoneFloorTexture(const FString& Texture)
{
	const FString Leaf = FPaths::GetBaseFilename(Texture).ToUpper();
	static const TSet<FString> Never = {
		TEXT("MM_STRMWND_WALL_01"), TEXT("MM_STRMWND_WALL_02"), TEXT("MM_STRMWND_WALL_03"),             // timber and plaster
		TEXT("MM_STRMWND_WWALL_01"), TEXT("MM_STRMWND_WWALL_02"), TEXT("MM_STRMWND_WWALL_03"),
		TEXT("MM_STRMWND_INT_WALL_01"), TEXT("MM_STRMWND_INT_WALL_02"), TEXT("MM_STRMWND_MWALL_02"),     // plaster, planks, ivy
		TEXT("MM_STRMWND_INT_FLOOR_01"), TEXT("MM_STRMWND_INT_FLOOR_03"),                                // wood, marble checker
		TEXT("MM_MSIDEWALK_01"),                                                                         // grass
		TEXT("MM_STRMWND_FACADE_02"), TEXT("MM_STRMWND_FACADE_03"),                                      // roof slates, rock
		TEXT("MM_ABBEY_FLOOR_02"), TEXT("MM_ABBEY_WALL_03"), TEXT("MM_CELLAR_WALL_01"), TEXT("MM_INN_WALL_01"), // mosaic, plaster, beams
		TEXT("MM_DRAGON_FLOOR_03")};                                                                     // lava cracks stay bright
	static const TSet<FString> Always = {
		TEXT("MM_NE_FLOOR_01"), TEXT("BH_NE_TOWER_WALL_03"), TEXT("MM_ABBEY_WALL_01"), TEXT("MM_ABBEY_WALL_02A"),
		TEXT("MM_ABBEY_WALL_02B"), TEXT("MM_ABBEY_FLOOR_03"), TEXT("MM_CATHY_FLOOR_01"), TEXT("JACRYPTFLOOR01")};
	if (Never.Contains(Leaf)) return false;
	if (Always.Contains(Leaf)) return true;
	const bool bTrimOrProp = Texture.Contains(TEXT("\\TRIM\\")) || Texture.Contains(TEXT("\\DOODADS\\"))
		|| Texture.Contains(TEXT("\\DEVICES\\")) || Texture.Contains(TEXT("\\DECORATION\\")) || Leaf.Contains(TEXT("TRIM"));
	if (bTrimOrProp) return false;
	// Bricks and cobbles first, so a zone name in the file (DUSKWOOD_...) does not count as wood.
	if (Texture.Contains(TEXT("\\BRICK\\")) || Leaf.Contains(TEXT("BRICK")) || Leaf.Contains(TEXT("COBBLE"))
		|| Leaf.Contains(TEXT("FLAGSTONE")))
		return true;
	for (const TCHAR* Word : {TEXT("WOOD"), TEXT("PLANK"), TEXT("PLASTER"), TEXT("CARPET"), TEXT("RUG"), TEXT("GRASS"),
		TEXT("DIRT"), TEXT("IVY"), TEXT("METAL"), TEXT("ROOF"), TEXT("WINDOW"), TEXT("DOOR"), TEXT("MARBLE"), TEXT("MOSAIC")})
		if (Leaf.Contains(Word)) return false;
	const bool bStone = Leaf.Contains(TEXT("STONE")) || Leaf.Contains(TEXT("STRMWND")) || Leaf.Contains(TEXT("FLAG"));
	if (Texture.Contains(TEXT("\\FLOOR\\")))
		return bStone || Leaf.Contains(TEXT("STREET")) || Leaf.Contains(TEXT("SIDEWALK")) || Leaf.Contains(TEXT("TILE"));
	if (Texture.Contains(TEXT("\\WALLS\\")) || Texture.Contains(TEXT("\\WALL\\")))
		return bStone;
	return false;
}

// r740: ground or wall, for the two stone relief options. Ground: floor folders and floor, street, sidewalk, cobble, flagstone,
// tile and paving textures; everything else that gets the relief (walls, bricks, stone blocks) counts as a wall.
static bool IsStoneReliefGroundTexture(const FString& Texture)
{
	if (Texture.Contains(TEXT("\\FLOOR\\"))) return true;
	const FString Leaf = FPaths::GetBaseFilename(Texture).ToUpper();
	for (const TCHAR* Word : {TEXT("FLOOR"), TEXT("STREET"), TEXT("SIDEWALK"), TEXT("COBBLE"), TEXT("FLAG"), TEXT("TILE"), TEXT("PAV")})
		if (Leaf.Contains(Word)) return true;
	return false;
}

// r737 (Stefan 23.10: "you lowered the stones rather than the fill between"): which side of a stone texture is the
// gap. Checked by eye on all 46 textures the relief has met (23.10): the gaps are the darker side except on these four,
// whose mortar or grout is lighter than the stones. +1 = dark side is the gap, -1 = light side (M_WowCobblePom StoneSign).
float CobblestoneReliefSign(const FString& Texture)
{
	static const TSet<FString> LightMortar = {
		TEXT("MM_REDBRICK_03X"),                              // red bricks, pale grey mortar
		TEXT("JACRYPTFLOOR01"),                               // dark octagon tiles, light grout lines
		TEXT("MM_ABBEY_WALL_02A"), TEXT("MM_ABBEY_WALL_02B")}; // grey stones, white mortar
	return LightMortar.Contains(FPaths::GetBaseFilename(Texture).ToUpper()) ? -1.0f : 1.0f;
}

UMaterial* SelectWmoRuntimeMaterial(const FWmoMaterial& WmoMat)
{
	const FString TexPath = WmoMat.TexturePath1.Replace(TEXT("/"), TEXT("\\"));
	// r740 (Stefan 23.10, before the GitHub release: "looks horrible ... stones are looking weird on their edges ... we need to
	// revert this", then "keep the settings separate, ground and walls" / "Maybe walls is fine but ground needs more work"):
	// two Experimental options (Options > Experimental > World & Sky): "stonereliefwalls" (on by default) and
	// "stonereliefground" (off by default). Off = the plain WMO material, the flat original texture. Read when a building
	// loads (restart to apply).
	const bool bCobble = WmoMat.BlendMode == 0 && IsCobblestoneFloorTexture(TexPath)
		&& FWowClientPreferences::GetWorldOption(IsStoneReliefGroundTexture(TexPath) ? TEXT("stonereliefground") : TEXT("stonereliefwalls"));
	// r715: which floor textures get the cobblestone material (once per texture), to match Stefan's screenshots.
	if (bCobble || TexPath.Contains(TEXT("\\FLOOR\\")) || TexPath.Contains(TEXT("\\WALL")) || TexPath.Contains(TEXT("\\BRICK\\")) || TexPath.Contains(TEXT("STREET")) || TexPath.Contains(TEXT("COBBLE")))
	{
		static TSet<FString> GLoggedFloors;
		bool bAlready = false;
		GLoggedFloors.Add(TexPath, &bAlready);
		if (!bAlready)
			UE_LOG(LogWowWmo, Log, TEXT("Floor/wall texture %s: blend %u, stone relief %s"), *TexPath, WmoMat.BlendMode,
				bCobble ? TEXT("yes") : TEXT("no"));
	}
	if (bCobble)
		if (UMaterial* Cobble = WowGetCobblestonePomMaterial()) return Cobble;
	if (ShouldUseFoliageMaterialForWmo(WmoMat))
	{
		return FWowTerrainMaterial::GetSimpleFoliageObjectMaterial();
	}

	if (ShouldForceOpaqueBuildingMaterialForWmo(WmoMat))
	{
		return FWowTerrainMaterial::GetSimpleOpaqueObjectMaterial((WmoMat.Flags & 0x04u) != 0);
	}

	if (WmoMat.BlendMode == 0)
	{
		return FWowTerrainMaterial::GetSimpleOpaqueObjectMaterial((WmoMat.Flags & 0x04u) != 0);
	}

	if (WmoMat.BlendMode == 2 || WmoMat.BlendMode == 6)
	{
		return FWowTerrainMaterial::GetSimpleTranslucentObjectMaterial();
	}

		if (WmoMat.BlendMode >= 3)
		{
			return FWowTerrainMaterial::GetSimpleAdditiveObjectMaterial();
		}

		return FWowTerrainMaterial::GetSimpleObjectMaterial();
	}

	bool IsWaterfallAssetDebugEnabled()
	{
		static int32 bEnabled = -1;
		if (bEnabled < 0)
		{
			bEnabled = FParse::Param(FCommandLine::Get(), TEXT("debugwaterfallassets")) ? 1 : 0;
		}
		return bEnabled != 0;
	}

	bool IsAbbeyAssetDebugEnabled()
	{
		static int32 bEnabled = -1;
		if (bEnabled < 0)
		{
			bEnabled = FParse::Param(FCommandLine::Get(), TEXT("debugabbeyassets")) ? 1 : 0;
		}
		return bEnabled != 0;
	}

	bool IsNearWaterfallDebugPoint(const FVector& Location, float Radius = 65000.0f)
	{
		static const FVector DebugPoint(-853333.0f, 9375.0f, 13000.0f);
		return FVector::DistSquared(Location, DebugPoint) <= FMath::Square(Radius);
	}

	struct FEmbeddedDoodadCounts
	{
		int32 Spawned = 0;
		int32 MissingPath = 0;
		int32 MissingMesh = 0;
	};

	TArray<int32> CollectEmbeddedWmoDoodadIndices(const FWmoRootData& RootData, const FAdtWmoPlacement& Placement)
	{
		TSet<int32> DoodadIndices;
		auto AddDoodadSet = [&RootData, &DoodadIndices](int32 SetIndex)
		{
			if (!RootData.DoodadSets.IsValidIndex(SetIndex))
			{
				return;
			}

			const FWmoDoodadSet& Set = RootData.DoodadSets[SetIndex];
			const int32 First = static_cast<int32>(Set.FirstDoodadIndex);
			const int32 Count = static_cast<int32>(Set.NumDoodads);
			for (int32 LocalIndex = 0; LocalIndex < Count; ++LocalIndex)
			{
				const int32 DefIndex = First + LocalIndex;
				if (RootData.DoodadDefs.IsValidIndex(DefIndex))
				{
					DoodadIndices.Add(DefIndex);
				}
			}
		};

		if (RootData.DoodadSets.Num() > 0)
		{
			AddDoodadSet(0);
			if (Placement.DoodadSet != 0)
			{
				AddDoodadSet(static_cast<int32>(Placement.DoodadSet));
			}
		}
		else
		{
			for (int32 DefIndex = 0; DefIndex < RootData.DoodadDefs.Num(); ++DefIndex)
			{
				DoodadIndices.Add(DefIndex);
			}
		}

		TArray<int32> Result = DoodadIndices.Array();
		Result.Sort();
		return Result;
	}

	// One embedded doodad (MODD def); split out so large WMOs can spawn them over several frames (r246).
	void SpawnEmbeddedWmoDoodad(AActor* WmoActor, USceneComponent* RootComp, const FWmoRootData& RootData,
		const FAdtWmoPlacement& Placement, FMpqManager* Mpq, FWowAssetCache* Cache, bool bHasAuthoredWmoLights,
		int32 DefIndex, FEmbeddedDoodadCounts& Counts)
	{
		if (!WmoActor || !RootComp || !Mpq || !Cache || !RootData.DoodadDefs.IsValidIndex(DefIndex))
		{
			return;
		}
			const FWmoDoodadDef& Def = RootData.DoodadDefs[DefIndex];
			const FString DoodadPath = NormalizeWmoDoodadPath(Def.Path);
			if (DoodadPath.IsEmpty() || IsSkippableEmbeddedDoodad(DoodadPath))
			{
				++Counts.MissingPath;
				return;
			}

			// A number of WMO fixtures (including the Northshire exterior pyres)
			// store the visible stone/metal housing in WMO geometry and place a
			// separate, meshless M2 from ParticleEmitters at the flame location.
			// Parse before asking for a mesh so those authored effect-only doodads
			// can still be instantiated at their exact MODD transform.
			TSharedPtr<FM2Data> ParsedDoodad = FWowDoodadManager::GetOrParseM2(DoodadPath, Mpq);
			const bool bHasParticleEmitters = ParsedDoodad.IsValid() &&
				ParsedDoodad->ParticleEmitters.Num() > 0;
			UStaticMesh* DoodadMesh = FWowDoodadManager::GetOrCreateStaticMesh(DoodadPath, Mpq, Cache);
			if (!DoodadMesh && !bHasParticleEmitters)
			{
				const FVector WorldLoc = WmoActor->GetTransform().TransformPosition(WmoLocalToUELocal(Def.Position));
				if (IsWaterfallAssetDebugEnabled() && IsNearWaterfallDebugPoint(WorldLoc))
				{
					UE_LOG(LogWowWmo, Warning,
						TEXT("Waterfall-near embedded doodad missing mesh: wmo=%s def=%d path=%s loc=%s scale=%.3f"),
						*WmoActor->GetName(),
						DefIndex,
						*DoodadPath,
						*WorldLoc.ToCompactString(),
						FMath::Max(0.001f, Def.Scale));
				}
				++Counts.MissingMesh;
				return;
			}

			const float DoodadScale = FMath::Max(0.001f, Def.Scale);
			const FTransform DoodadTransform(
				WmoDoodadRotationToUELocal(Def.Rotation),
				WmoLocalToUELocal(Def.Position),
				FVector(DoodadScale));

			// Birds and other ambient animals in the WMO's doodad sets loop their flight (r283).
			if (FWowDoodadManager::SpawnAmbientAnimal(WmoActor, RootComp, DoodadPath, DoodadTransform, DoodadMesh,
					Placement.UniqueId * 7919u + uint32(DefIndex), Mpq, Cache))
			{
				++Counts.Spawned;
				return;
			}

			USceneComponent* DoodadRoot = nullptr;
			UStaticMeshComponent* DoodadComp = nullptr;
			if (DoodadMesh)
			{
				const FName ComponentName = *FString::Printf(
					TEXT("WmoDoodad_%u_%d"), Placement.UniqueId, DefIndex);
				DoodadComp = NewObject<UStaticMeshComponent>(WmoActor, ComponentName);
				if (!DoodadComp)
				{
					return;
				}

				DoodadComp->SetMobility(RootComp->Mobility);
				DoodadComp->SetupAttachment(RootComp);
				WmoActor->AddInstanceComponent(DoodadComp);
				DoodadComp->SetStaticMesh(DoodadMesh);
				DoodadComp->SetRelativeTransform(DoodadTransform);
				DoodadComp->SetCastShadow(true);
				ConfigureEmbeddedDoodadCollision(DoodadComp, DoodadPath);
				DoodadComp->RegisterComponent();
                if (FWowDoodadManager::AddAuthoredCollision(WmoActor,DoodadMesh,ParsedDoodad.Get(),
                    DoodadPath,DoodadComp->GetComponentTransform(),Placement.UniqueId,DefIndex))
                    DoodadComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				DoodadRoot = DoodadComp;
			}
			else
			{
				const FName AnchorName = *FString::Printf(
					TEXT("WmoDoodadEffectAnchor_%u_%d"), Placement.UniqueId, DefIndex);
				USceneComponent* EffectAnchor = NewObject<USceneComponent>(WmoActor, AnchorName);
				if (!EffectAnchor)
				{
					return;
				}

				EffectAnchor->SetMobility(RootComp->Mobility);
				EffectAnchor->SetupAttachment(RootComp);
				WmoActor->AddInstanceComponent(EffectAnchor);
				EffectAnchor->SetRelativeTransform(DoodadTransform);
				EffectAnchor->RegisterComponent();
				DoodadRoot = EffectAnchor;
				UE_LOG(LogWowWmo, Verbose,
					TEXT("  Spawned effect-only WMO doodad anchor: %s"), *DoodadPath);
			}

			if (bHasParticleEmitters)
			{
				const FName ParticleName = *FString::Printf(
					TEXT("WmoDoodadParticles_%u_%d"), Placement.UniqueId, DefIndex);
				UWowM2ParticleComponent* ParticleComponent =
					NewObject<UWowM2ParticleComponent>(WmoActor, ParticleName);
				if (ParticleComponent)
				{
					ParticleComponent->SetupAttachment(DoodadRoot);
					WmoActor->AddInstanceComponent(ParticleComponent);
					ParticleComponent->RegisterComponent();
					ParticleComponent->SetFixturePath(DoodadPath);
                    ParticleComponent->InitializeFromParsedM2Data(ParsedDoodad.Get(), Mpq, Cache);
				}
			}
            if (ParsedDoodad.IsValid())
                FWowLampFixtures::AttachM2(WmoActor,DoodadComp,*ParsedDoodad,DoodadPath,DoodadRoot->GetComponentTransform());
			if (DoodadComp && !bHasAuthoredWmoLights && !FWowLampFixtures::IsSupported(DoodadPath))
			{
				CreateEmbeddedDoodadPointLight(WmoActor, DoodadComp, DoodadPath, Placement.UniqueId, DefIndex);
			}
			// r539: hidden with the groups that list it (MODR) when portal culling hides them.
			if (UWowWmoPortalCulling* Culling = WmoActor->FindComponentByClass<UWowWmoPortalCulling>())
				Culling->AddDoodad(DoodadRoot, DefIndex);
			const FVector DoodadWorldLocation = DoodadRoot->GetComponentLocation();
			if (IsWaterfallAssetDebugEnabled() && IsNearWaterfallDebugPoint(DoodadWorldLocation))
			{
				UE_LOG(LogWowWmo, Warning,
					TEXT("Waterfall-near embedded doodad: wmo=%s def=%d path=%s loc=%s extent=%s scale=%.3f"),
					*WmoActor->GetName(),
					DefIndex,
					*DoodadPath,
					*DoodadWorldLocation.ToCompactString(),
					*(DoodadComp ? DoodadComp->Bounds.BoxExtent : FVector::ZeroVector).ToCompactString(),
					DoodadScale);
			}
			++Counts.Spawned;
	}

	void LogEmbeddedWmoDoodads(const FEmbeddedDoodadCounts& Counts, const FWmoRootData& RootData, const FAdtWmoPlacement& Placement)
	{
		if (Counts.Spawned > 0 || Counts.MissingPath > 0 || Counts.MissingMesh > 0)
		{
			UE_LOG(LogWowWmo, Log,
				TEXT("  Spawned %d embedded WMO doodads for set %u (%d skipped path/effect, %d missing mesh, %d available defs, %d sets)"),
				Counts.Spawned,
				Placement.DoodadSet,
				Counts.MissingPath,
				Counts.MissingMesh,
				RootData.DoodadDefs.Num(),
				RootData.DoodadSets.Num());
		}
	}

	int32 SpawnEmbeddedWmoDoodads(AActor* WmoActor, USceneComponent* RootComp, const FWmoRootData& RootData,
		const FAdtWmoPlacement& Placement, FMpqManager* Mpq, FWowAssetCache* Cache, bool bHasAuthoredWmoLights)
	{
		if (!WmoActor || !RootComp || !Mpq || !Cache || RootData.DoodadDefs.Num() == 0)
		{
			return 0;
		}
		FEmbeddedDoodadCounts Counts;
		for (const int32 DefIndex : CollectEmbeddedWmoDoodadIndices(RootData, Placement))
		{
			SpawnEmbeddedWmoDoodad(WmoActor, RootComp, RootData, Placement, Mpq, Cache, bHasAuthoredWmoLights, DefIndex, Counts);
		}
		LogEmbeddedWmoDoodads(Counts, RootData, Placement);
		return Counts.Spawned;
	}
}

struct FWowWmoBuildState
{
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<USceneComponent> RootComp;
    TSharedPtr<FWmoRootData> RootData;
    FString WmoPath;
    FAdtWmoPlacement Placement;
    FMpqManager* Mpq = nullptr;
    FWowAssetCache* Cache = nullptr;
    UWowWmoBackdropVisibility* BackdropVisibility = nullptr;
    UWowWmoPortalCulling* PortalCulling = nullptr; // r539
    int32 GroupsLoaded = 0;
    uint32 NextGroup = 0;
    TArray<int32> DoodadIndices;
    int32 NextDoodad = 0;
    FEmbeddedDoodadCounts DoodadCounts;
    bool bDebugAbbeyAssets = false;
    double QueuedSeconds = FPlatformTime::Seconds();
};

namespace
{
    constexpr uint32 IncrementalWmoGroupThreshold = 40;
    TArray<TUniquePtr<FWowWmoBuildState>> GPendingWmoBuilds;
    FTSTicker::FDelegateHandle GWmoBuildTicker;
    bool GWmoFastBuild = false;
}

FString FWowWmoRenderer::GetGroupPath(const FString& RootPath, int32 GroupIndex)
{
    // Replace ".wmo" with "_XXX.wmo" where XXX is zero-padded group index
    FString Base = RootPath;
    if (Base.EndsWith(TEXT(".wmo"), ESearchCase::IgnoreCase))
    {
        Base = Base.Left(Base.Len() - 4);
    }
    return FString::Printf(TEXT("%s_%03d.wmo"), *Base, GroupIndex);
}

AActor* FWowWmoRenderer::SpawnWmo(UWorld* World, const FString& WmoPath, const FAdtWmoPlacement& Placement,
                                   FMpqManager* Mpq, FWowAssetCache* Cache)
{
    if (!World || !Mpq || WmoPath.IsEmpty())
    {
        return nullptr;
    }

    // Read and parse root WMO
    TArray<uint8> RootRaw;
    if (!Mpq->ReadFile(WmoPath, RootRaw))
    {
        UE_LOG(LogWowWmo, Verbose, TEXT("Failed to read WMO root: %s"), *WmoPath);
        return nullptr;
    }

    FWmoRootData RootData = FWmoParser::ParseRoot(RootRaw);
    if (!RootData.bIsValid || RootData.NumGroups == 0)
    {
        UE_LOG(LogWowWmo, Verbose, TEXT("Invalid WMO root or no groups: %s"), *WmoPath);
        return nullptr;
    }

	const bool bDebugAbbeyAssets = IsAbbeyAssetDebugEnabled() &&
		WmoPath.Contains(TEXT("NSABBEY"), ESearchCase::IgnoreCase);
	if (bDebugAbbeyAssets)
	{
		UE_LOG(LogWowWmo, Warning,
			TEXT("[ABBEYASSETDEBUG] root=%s materials=%d doodads=%d lights=%d"),
			*WmoPath, RootData.Materials.Num(), RootData.DoodadDefs.Num(), RootData.Lights.Num());
		for (int32 MaterialIndex = 0; MaterialIndex < RootData.Materials.Num(); ++MaterialIndex)
		{
			const FWmoMaterial& Material = RootData.Materials[MaterialIndex];
			UE_LOG(LogWowWmo, Warning,
				TEXT("[ABBEYASSETDEBUG] material=%d blend=%u flags=0x%08x tex1=%s tex2=%s"),
				MaterialIndex, Material.BlendMode, Material.Flags,
				*Material.TexturePath1, *Material.TexturePath2);
		}
		for (int32 LightIndex = 0; LightIndex < RootData.Lights.Num(); ++LightIndex)
		{
			const FWmoLight& Light = RootData.Lights[LightIndex];
			UE_LOG(LogWowWmo, Warning,
				TEXT("[ABBEYASSETDEBUG] light=%d type=%u atten=%d color=(%u,%u,%u,%u) pos=%s intensity=%.3f range=(%.3f,%.3f)"),
				LightIndex, Light.Type, Light.bUseAttenuation ? 1 : 0,
				Light.Color.R, Light.Color.G, Light.Color.B, Light.Color.A,
				*Light.Position.ToCompactString(), Light.Intensity,
				Light.AttenuationStart, Light.AttenuationEnd);
		}
		for (int32 DoodadIndex = 0; DoodadIndex < RootData.DoodadDefs.Num(); ++DoodadIndex)
		{
			const FWmoDoodadDef& Doodad = RootData.DoodadDefs[DoodadIndex];
			UE_LOG(LogWowWmo, Warning,
				TEXT("[ABBEYASSETDEBUG] doodad=%d path=%s pos=%s scale=%.3f"),
				DoodadIndex, *Doodad.Path, *Doodad.Position.ToCompactString(), Doodad.Scale);
		}
	}

    // Spawn actor for this WMO (use NAME_None to auto-generate unique name)
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* WmoActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (!WmoActor)
    {
        return nullptr;
    }
    // r701: ships (anchored pirate ships, transports) - their lamps are seen from far out at sea.
    if (WmoPath.Contains(TEXT("SHIP")) || WmoPath.Contains(TEXT("TRANSPORT")))
        WmoActor->Tags.AddUnique(TEXT("WowShipWmo"));

    // Create root component
    USceneComponent* RootComp = NewObject<USceneComponent>(WmoActor, TEXT("WmoRoot"));
    WmoActor->SetRootComponent(RootComp);
    RootComp->RegisterComponent();

    // Position in ADT space
    FVector UEPos = FWowCoordinate::AdtToUE(Placement.Position.X, Placement.Position.Y, Placement.Position.Z);
    WmoActor->SetActorLocation(UEPos);

    // Full 3-axis rotation: WoW MODF rotation (Rx, Ry, Rz) → UE FRotator
    WmoActor->SetActorRotation(FWowCoordinate::WowRotationToUE(
        Placement.Rotation.X, Placement.Rotation.Y, Placement.Rotation.Z));

    // Scale (WMOs can have scale too)
    float ScaleVal = (Placement.Scale == 0) ? 1.0f : Placement.Scale / 1024.0f;
    WmoActor->SetActorScale3D(FVector(ScaleVal));

    TUniquePtr<FWowWmoBuildState> State = MakeUnique<FWowWmoBuildState>();
    State->Actor = WmoActor;
    State->RootComp = RootComp;
    State->RootData = MakeShared<FWmoRootData>(MoveTemp(RootData));
    State->WmoPath = WmoPath;
    State->Placement = Placement;
    State->Mpq = Mpq;
    State->Cache = Cache;
    State->bDebugAbbeyAssets = bDebugAbbeyAssets;

    // Large WMOs (Stormwind: 286 groups, 6157 doodads, 575 lights) froze the game for over five
    // seconds when built in one frame (r246). Build them a few milliseconds per frame instead;
    // the loading screen gives them a larger budget and waits for them (HasPendingBuilds).
    if (State->RootData->NumGroups > IncrementalWmoGroupThreshold)
    {
        State->DoodadIndices = CollectEmbeddedWmoDoodadIndices(*State->RootData, Placement);
        UE_LOG(LogWowWmo, Log, TEXT("Queued large WMO %s for incremental build (%u groups, %d doodads)"),
            *WmoPath, State->RootData->NumGroups, State->DoodadIndices.Num());
        GPendingWmoBuilds.Add(MoveTemp(State));
        if (!GWmoBuildTicker.IsValid())
            GWmoBuildTicker = FTSTicker::GetCoreTicker().AddTicker(
                FTickerDelegate::CreateStatic(&FWowWmoRenderer::TickPendingBuilds), 0.0f);
        return WmoActor;
    }

    for (uint32 GroupIdx = 0; GroupIdx < State->RootData->NumGroups; ++GroupIdx)
    {
        BuildWmoGroup(*State, GroupIdx);
    }
    if (State->GroupsLoaded == 0)
    {
        WmoActor->Destroy();
        return nullptr;
    }
    FinishWmoBuild(*State, true);
    return WmoActor;
}

bool FWowWmoRenderer::BuildWmoGroup(FWowWmoBuildState& State, uint32 GroupIdx)
{
    AActor* WmoActor = State.Actor.Get();
    USceneComponent* RootComp = State.RootComp.Get();
    if (!WmoActor || !RootComp || !State.RootData.IsValid()) return false;
    const FWmoRootData& RootData = *State.RootData;
    const FString& WmoPath = State.WmoPath;
    FMpqManager* Mpq = State.Mpq;
    FWowAssetCache* Cache = State.Cache;
    const bool bDebugAbbeyAssets = State.bDebugAbbeyAssets;
    UWowWmoBackdropVisibility*& BackdropVisibility = State.BackdropVisibility;
    int32& GroupsLoaded = State.GroupsLoaded;

        FString GroupPath = GetGroupPath(WmoPath, GroupIdx);

        TArray<uint8> GroupRaw;
        if (!Mpq->ReadFile(GroupPath, GroupRaw))
        {
            UE_LOG(LogWowWmo, Verbose, TEXT("Failed to read WMO group %d: %s"), GroupIdx, *GroupPath);
            return false;
        }

        FWmoGroupData GroupData = FWmoParser::ParseGroup(GroupRaw);
        if (!GroupData.bIsValid || GroupData.Vertices.Num() == 0 || GroupData.Indices.Num() == 0)
        {
            return false;
        }
		if (bDebugAbbeyAssets)
		{
			for (int32 BatchIndex = 0; BatchIndex < GroupData.Batches.Num(); ++BatchIndex)
			{
				const FWmoGroupData::FBatch& Batch = GroupData.Batches[BatchIndex];
				if (RootData.Materials.IsValidIndex(Batch.MaterialIndex))
				{
					const FWmoMaterial& Material = RootData.Materials[Batch.MaterialIndex];
					UE_LOG(LogWowWmo, Warning,
						TEXT("[ABBEYASSETDEBUG] group=%u batch=%d mat=%u blend=%u indices=%u tex=%s"),
						GroupIdx, BatchIndex, Batch.MaterialIndex, Material.BlendMode,
						Batch.IndexCount, *Material.TexturePath1);
				}
			}
		}

        // Create UStaticMesh with Nanite for this group
        UStaticMesh* GroupMesh = CreateStaticMeshFromWmoGroup(GroupData, RootData, WmoPath, GroupIdx, Mpq, Cache);
        if (!GroupMesh) return false;

        FName CompName = *FString::Printf(TEXT("WmoGroup_%d"), GroupIdx);
        UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(WmoActor, CompName);
        MeshComp->SetupAttachment(RootComp);
        MeshComp->SetStaticMesh(GroupMesh);
        MeshComp->SetCastShadow(true);
        // Original building walls may have only one face; block light from both sides.
        MeshComp->bCastShadowAsTwoSided = true;
        // MOPY contains invisible ramps/barriers omitted from render batches.
        // Keep visible surfaces available to picking/camera traces, but use the
        // authored solid geometry for character movement when it is present.
        const bool bAuthoredCollision = FWowWmoCollision::Attach(
            WmoActor, RootComp, GroupData, GroupIdx);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComp->SetCollisionObjectType(ECC_WorldStatic);
		MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
        if (bAuthoredCollision) MeshComp->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
        MeshComp->CanCharacterStepUpOn = ECB_Yes;
        MeshComp->RegisterComponent();
		MeshComp->RecreatePhysicsState();
        // r539: room/portal visibility like the original client (Stormwind's doubled gate towers).
        if (RootData.Portals.Num() > 0)
        {
            if (!State.PortalCulling)
            {
                State.PortalCulling = NewObject<UWowWmoPortalCulling>(WmoActor);
                WmoActor->AddInstanceComponent(State.PortalCulling);
                State.PortalCulling->SetRoot(RootComp, RootData);
                State.PortalCulling->RegisterComponent();
            }
            State.PortalCulling->AddGroup(MeshComp, int32(GroupIdx), GroupData);
        }
        // r412: indoor groups (MOGP flag 0x2000) are the rooms the weather fog stays out of.
        // r590 (Stefan, Stormwind in fog: "the weather keeps changing ALL the time outside" - thick fog on the
        // canal bridge, a clear night sky a few steps down in the street, then fog again): the city's districts
        // are indoor groups too, so every street counted as "inside a room" - the weather fog went the moment
        // anything was overhead, and the fog mask cleared it from the whole district. Large open-air districts
        // (footprint over 10,000 square yards, as for the wetness and the portal rules) are outdoors for weather.
        const FVector RoomSize = GroupData.BoundingBox.GetSize();
        const bool bOpenAirDistrict = !(GroupData.Flags & 0x10000u) && RoomSize.X * RoomSize.Y > 10000.0;
        if ((GroupData.Flags & 0x2000u) && !bOpenAirDistrict) FWowIndoorFogMask::RegisterIndoorGroup(MeshComp);
        // r656 (Stefan in the Burning Blade cave: "we need a haze in all caves and mines"): the group's own fog
        // from the WMO (MFOG[fogIds[0]]: MD_WarmCave has an orange haze, ends at 97 yd, starts at a quarter of that).
        // A plain white entry is the placeholder (MD_WarmCave's entrance group points at one): no fog of its own.
        if ((GroupData.Flags & 0x2000u) && !bOpenAirDistrict && RootData.Fogs.IsValidIndex(GroupData.FogIds[0]))
        {
            const FWmoRootData::FFog& Fog = RootData.Fogs[GroupData.FogIds[0]];
            const bool bPlaceholder = Fog.Color.R >= 250 && Fog.Color.G >= 250 && Fog.Color.B >= 250;
            if (!bPlaceholder && Fog.End > 1.f)
            {
                const float YardCm = float(FWowCoordinate::SCALE);
                FWowIndoorFogMask::SetGroupFog(MeshComp, Fog.Color, Fog.End * FMath::Clamp(Fog.StartScalar, 0.f, 1.f) * YardCm, Fog.End * YardCm);
            }
        }
        FWowLampFixtures::AttachWmo(WmoActor,MeshComp,GroupData,RootData);
        // r291: portal-only facades (Stormwind group 92) count as backdrops too.
        const bool bPortalBackdrop=UWowWmoBackdropVisibility::IsBackdropGroup(RootData,GroupIdx,GroupData.Flags);
        if (!bPortalBackdrop)
            FWowLampFixtures::AttachWmoWindows(WmoActor,MeshComp,GroupData,RootData);
        if (bPortalBackdrop)
        {
            if (!BackdropVisibility)
            {
                BackdropVisibility=NewObject<UWowWmoBackdropVisibility>(WmoActor);
                WmoActor->AddInstanceComponent(BackdropVisibility);
                BackdropVisibility->RegisterComponent();
            }
            BackdropVisibility->AddGroup(MeshComp,RootData,GroupIdx);
        }
        if (IsWaterfallAssetDebugEnabled() && IsNearWaterfallDebugPoint(
            MeshComp->Bounds.Origin,
            MeshComp->Bounds.SphereRadius + 65000.0f))
        {
            UE_LOG(LogWowWmo, Warning,
                TEXT("Waterfall-near WMO group: path=%s group=%u actor=%s bounds=%s extent=%s mats=%d batches=%d"),
                *WmoPath,
                GroupIdx,
                *WmoActor->GetName(),
                *MeshComp->Bounds.Origin.ToCompactString(),
                *MeshComp->Bounds.BoxExtent.ToCompactString(),
                RootData.Materials.Num(),
                GroupData.Batches.Num());
            for (int32 BatchIdx = 0; BatchIdx < GroupData.Batches.Num(); ++BatchIdx)
            {
                const FWmoGroupData::FBatch& Batch = GroupData.Batches[BatchIdx];
                if (RootData.Materials.IsValidIndex(Batch.MaterialIndex))
                {
                    const FWmoMaterial& WmoMat = RootData.Materials[Batch.MaterialIndex];
                    UE_LOG(LogWowWmo, Warning,
                        TEXT("  batch=%d mat=%u blend=%u flags=0x%08x tex=%s"),
                        BatchIdx,
                        Batch.MaterialIndex,
                        WmoMat.BlendMode,
                        WmoMat.Flags,
                        *WmoMat.TexturePath1);
                }
                else
                {
                    UE_LOG(LogWowWmo, Warning,
                        TEXT("  batch=%d invalid material index=%u"),
                        BatchIdx,
                        Batch.MaterialIndex);
                }
            }
        }
        ++GroupsLoaded;

        // Create liquid mesh for this group if MLIQ data exists
        if (GroupData.Liquid.HasLiquid())
        {
            const FWmoLiquidData& Liq = GroupData.Liquid;
            uint32 XTiles = Liq.XVerts - 1;
            uint32 YTiles = Liq.YVerts - 1;

            TArray<FVector3f> LiqVerts;
            TArray<FVector2f> LiqUVs;
            TArray<FVector4f> LiqColors;
            TArray<int32> LiqIndices;
            LiqVerts.Reserve(Liq.XVerts * Liq.YVerts);
            LiqUVs.Reserve(Liq.XVerts * Liq.YVerts);
            LiqColors.Reserve(Liq.XVerts * Liq.YVerts);

            // MLIQ MaterialId indexes MOMT, not LiquidType.dbc. Use MOGP and
            // the root's legacy/new type flag, matching the WotLK extractor.
            uint32 LiquidEntry=GroupData.LiquidTypeId;
            if(!(RootData.Flags & 4u)) LiquidEntry=LiquidEntry==15u ? 0u : LiquidEntry+1u;
            if(LiquidEntry==0)
                for(uint8 Tile:Liq.TileFlags) if((Tile & 0xFu)!=0xFu) {LiquidEntry=(Tile & 0xFu)+1u;break;}
            if(LiquidEntry>0 && LiquidEntry<21)
            {
                switch((LiquidEntry-1u)&3u)
                {
                    case 0: LiquidEntry=(GroupData.Flags & 0x80000u) ? 14u : 13u; break;
                    case 1: LiquidEntry=14u; break;
                    case 2: LiquidEntry=19u; break;
                    case 3: LiquidEntry=20u; break;
                }
            }
            const auto* LiquidType=FDbcStore::Get().LiquidTypes().GetById(LiquidEntry);
            const int32 Category=LiquidType ? int32(LiquidType->Type) : 0;

            for (uint32 y = 0; y < Liq.YVerts; y++)
            {
                for (uint32 x = 0; x < Liq.XVerts; x++)
                {
                    uint32 Idx = y * Liq.XVerts + x;
                    float Height = (Idx < (uint32)Liq.Vertices.Num()) ? Liq.Vertices[Idx].Height : 0.0f;

                    // MLIQ grid in WMO model space (X=east, Y=north, Z=up)
                    constexpr float LIQUID_UNIT = 4.1667f;
                    float ModelX = Liq.Position.X + LIQUID_UNIT * x;
                    float ModelY = Liq.Position.Y + LIQUID_UNIT * y;
                    float ModelZ = Height;

                    // WMO model → UE local: (Y, X, Z) * SCALE (same as group mesh)
                    LiqVerts.Add(FVector3f(ModelY * FWowCoordinate::SCALE,
                                           ModelX * FWowCoordinate::SCALE,
                                           ModelZ * FWowCoordinate::SCALE));

                    float U = (float)x / FMath::Max(1u, Liq.XVerts - 1);
                    float V = (float)y / FMath::Max(1u, Liq.YVerts - 1);
                    LiqUVs.Add(FVector2f(U, V));

                    // Depth opacity from flow data
                    float DepthAlpha = 0.7f;
                    if (Idx < (uint32)Liq.Vertices.Num())
                    {
                        DepthAlpha = FMath::Lerp(0.3f, 0.9f, Liq.Vertices[Idx].Flow1 / 255.0f);
                    }
                    if (Category == 2 || Category == 3) DepthAlpha = 1.0f;

                    FVector4f VC;
                    switch (Category)
                    {
                    case 0: case 1: VC = FVector4f(0.1f, 0.3f, 0.6f, DepthAlpha); break;
                    case 2: VC = FVector4f(1.0f, 0.4f, 0.1f, 1.0f); break;
                    case 3: VC = FVector4f(0.2f, 0.7f, 0.1f, 1.0f); break;
                    default: VC = FVector4f(0.1f, 0.3f, 0.6f, DepthAlpha); break;
                    }
                    LiqColors.Add(VC);
                }
            }

            // Build triangles, respecting tile visibility flags
            for (uint32 y = 0; y < YTiles; y++)
            {
                for (uint32 x = 0; x < XTiles; x++)
                {
                    uint32 TileIdx = y * XTiles + x;
                    if (TileIdx < (uint32)Liq.TileFlags.Num())
                    {
                        // Low nibble 0xF is absent liquid; high bits encode flow.
                        if ((Liq.TileFlags[TileIdx] & 0x0F) == 0x0F) continue;
                    }

                    int32 TL = y * Liq.XVerts + x;
                    int32 TR = TL + 1;
                    int32 BL = TL + Liq.XVerts;
                    int32 BR = BL + 1;

                    // Model X/Y swap reverses handedness: keep the upper face visible.
                    LiqIndices.Add(TL); LiqIndices.Add(TR); LiqIndices.Add(BL);
                    LiqIndices.Add(TR); LiqIndices.Add(BR); LiqIndices.Add(BL);
                }
            }

            if (LiqIndices.Num() > 0)
            {
                // Build liquid static mesh via FMeshDescription
                FMeshDescription LiqMeshDesc;
                FStaticMeshAttributes LiqAttrs(LiqMeshDesc);
                LiqAttrs.Register();

                auto LiqVertPos = LiqAttrs.GetVertexPositions();
                auto LiqInstNormals = LiqAttrs.GetVertexInstanceNormals();
                auto LiqInstTangents = LiqAttrs.GetVertexInstanceTangents();
                auto LiqInstBinSigns = LiqAttrs.GetVertexInstanceBinormalSigns();
                auto LiqInstUVs = LiqAttrs.GetVertexInstanceUVs();
                auto LiqInstColors = LiqAttrs.GetVertexInstanceColors();

                FPolygonGroupID LiqPolyGroup = LiqMeshDesc.CreatePolygonGroup();
                LiqMeshDesc.ReserveNewVertices(LiqVerts.Num());
                LiqMeshDesc.ReserveNewVertexInstances(LiqVerts.Num());
                LiqMeshDesc.ReserveNewPolygons(LiqIndices.Num() / 3);

                TArray<FVertexInstanceID> LiqInstIDs;
                LiqInstIDs.SetNum(LiqVerts.Num());

                for (int32 v = 0; v < LiqVerts.Num(); v++)
                {
                    FVertexID VID = LiqMeshDesc.CreateVertex();
                    LiqVertPos[VID] = LiqVerts[v];
                    FVertexInstanceID IID = LiqMeshDesc.CreateVertexInstance(VID);
                    LiqInstIDs[v] = IID;
                    LiqInstNormals[IID] = FVector3f(0, 0, 1);
                    LiqInstTangents[IID] = FVector3f(1, 0, 0);
                    LiqInstBinSigns[IID] = 1.0f;
                    LiqInstUVs.Set(IID, 0, (v < LiqUVs.Num()) ? LiqUVs[v] : FVector2f(0, 0));
                    LiqInstColors[IID] = (v < LiqColors.Num()) ? LiqColors[v] : FVector4f(1, 1, 1, 0.7f);
                }

                for (int32 t = 0; t < LiqIndices.Num(); t += 3)
                {
                    TArray<FVertexInstanceID> Tri;
                    Tri.Add(LiqInstIDs[LiqIndices[t]]);
                    Tri.Add(LiqInstIDs[LiqIndices[t + 1]]);
                    Tri.Add(LiqInstIDs[LiqIndices[t + 2]]);
                    LiqMeshDesc.CreatePolygon(LiqPolyGroup, Tri);
                }

                UStaticMesh* LiqSM = NewObject<UStaticMesh>();
                UMaterialInterface* LiqMat = FWowWaterMaterial::GetMaterialForCategory(Category);
                FStaticMaterial LiquidStaticMaterial(LiqMat);
                LiquidStaticMaterial.UVChannelData.bInitialized = true;
                LiqSM->GetStaticMaterials().Add(MoveTemp(LiquidStaticMaterial));
                TArray<const FMeshDescription*> LiqDescs;
                LiqDescs.Add(&LiqMeshDesc);
                UStaticMesh::FBuildMeshDescriptionsParams LiqBuild;
                LiqBuild.bBuildSimpleCollision = false;
                LiqBuild.bFastBuild = true;
                WowPrepareRuntimeMeshMaterials(LiqSM, const_cast<FMeshDescription&>(*LiqDescs[0]));
                LiqSM->BuildFromMeshDescriptions(LiqDescs, LiqBuild);
                LiqSM->NeverStream = true;

                if (LiqMat)
                {
                    if (LiqSM->GetStaticMaterials().Num() <= 0)
                    {
                        LiqSM->GetStaticMaterials().SetNum(1);
                    }
                    LiqSM->GetStaticMaterials()[0].MaterialInterface = LiqMat;
                }

                FName LiqCompName = *FString::Printf(TEXT("WmoLiquid_%d"), GroupIdx);
                UStaticMeshComponent* LiqComp = NewObject<UStaticMeshComponent>(WmoActor, LiqCompName);
                LiqComp->SetupAttachment(RootComp);
                LiqComp->SetStaticMesh(LiqSM);
                LiqComp->SetCastShadow(false);
                LiqComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                LiqComp->RegisterComponent();

                // Use the same water contacts as terrain water. Merge only
                // consecutive visible cells on each row; never fill dry holes.
                if (Category <= 1)
                {
                    int32 VolumeIndex = GroupIdx * 100000;
                    for (uint32 Row = 0; Row < YTiles; ++Row)
                    {
                        FBox Run(ForceInit);
                        auto Flush = [&]() {
                            if (Run.IsValid)
                                FWowWaterRenderer::CreateWaterOverlapVolume(WmoActor, VolumeIndex, Run, Category);
                            Run = FBox(ForceInit);
                        };
                        for (uint32 Col = 0; Col < XTiles; ++Col)
                        {
                            const uint32 Cell = Row * XTiles + Col;
                            if (Cell < uint32(Liq.TileFlags.Num()) && (Liq.TileFlags[Cell] & 0x0F) == 0x0F)
                            { Flush(); continue; }
                            const int32 A = Row * Liq.XVerts + Col;
                            FBox CellBounds(ForceInit);
                            for (int32 I : {A, A+1, A+int32(Liq.XVerts), A+int32(Liq.XVerts)+1})
                                CellBounds += FVector(LiqVerts[I]);
                            if (Run.IsValid && FMath::Abs(Run.Max.Z - CellBounds.Max.Z) > 1.f) Flush();
                            Run += CellBounds;
                        }
                        Flush();
                    }
                }


                UE_LOG(LogWowWmo, Log, TEXT("  Created WMO liquid mesh for group %d (%d tris, cat=%d)"),
                    GroupIdx, LiqIndices.Num() / 3, Category);
            }
        }
        return true;
}

void FWowWmoRenderer::FinishWmoBuild(FWowWmoBuildState& State, bool bSpawnDoodadsNow)
{
    AActor* WmoActor = State.Actor.Get();
    USceneComponent* RootComp = State.RootComp.Get();
    if (!WmoActor || !RootComp || !State.RootData.IsValid()) return;
    const FWmoRootData& RootData = *State.RootData;
    const FString& WmoPath = State.WmoPath;
    const FAdtWmoPlacement& Placement = State.Placement;
    FMpqManager* Mpq = State.Mpq;
    FWowAssetCache* Cache = State.Cache;


	const int32 EmbeddedDoodadsLoaded = bSpawnDoodadsNow
        ? SpawnEmbeddedWmoDoodads(WmoActor, RootComp, RootData, Placement, Mpq, Cache, !RootData.Lights.IsEmpty())
        : State.DoodadCounts.Spawned;
    if (!bSpawnDoodadsNow) LogEmbeddedWmoDoodads(State.DoodadCounts, RootData, Placement);
    SpawnWmoRootLights(WmoActor, RootComp, RootData, WmoPath, Placement.UniqueId);

    UE_LOG(LogWowWmo, Log, TEXT("Spawned WMO %s with %d/%d groups at %s"),
        *WmoPath, State.GroupsLoaded, RootData.NumGroups, *WmoActor->GetActorLocation().ToString());
	if (EmbeddedDoodadsLoaded > 0)
	{
		UE_LOG(LogWowWmo, Log, TEXT("  WMO embedded doodads visible: %d for %s"),
			EmbeddedDoodadsLoaded, *WmoPath);
	}

}

bool FWowWmoRenderer::TickPendingBuilds(float)
{
    const double Start = FPlatformTime::Seconds();
    const double Budget = (GWmoFastBuild ? 60.0 : 4.0) / 1000.0;
    // At least one step per frame, then until the budget is spent.
    do
    {
        if (GPendingWmoBuilds.Num() == 0) break;
        FWowWmoBuildState& State = *GPendingWmoBuilds[0];
        AActor* WmoActor = State.Actor.Get();
        if (!WmoActor || !State.RootComp.IsValid() || !State.RootData.IsValid())
        {
            GPendingWmoBuilds.RemoveAt(0);
            continue;
        }
        if (State.NextGroup < State.RootData->NumGroups)
        {
            BuildWmoGroup(State, State.NextGroup++);
            continue;
        }
        if (State.NextDoodad < State.DoodadIndices.Num())
        {
            SpawnEmbeddedWmoDoodad(WmoActor, State.RootComp.Get(), *State.RootData, State.Placement, State.Mpq, State.Cache,
                !State.RootData->Lights.IsEmpty(), State.DoodadIndices[State.NextDoodad++], State.DoodadCounts);
            continue;
        }
        FinishWmoBuild(State, false);
        UE_LOG(LogWowWmo, Log, TEXT("Incremental WMO build finished: %s in %.1f s"),
            *State.WmoPath, FPlatformTime::Seconds() - State.QueuedSeconds);
        GPendingWmoBuilds.RemoveAt(0);
    } while (FPlatformTime::Seconds() - Start < Budget);
    return true;
}

bool FWowWmoRenderer::HasPendingBuilds()
{
    return GPendingWmoBuilds.Num() > 0;
}

void FWowWmoRenderer::SetFastBuild(bool bFast)
{
    GWmoFastBuild = bFast;
}

UStaticMesh* FWowWmoRenderer::CreateStaticMeshFromWmoGroup(
    const FWmoGroupData& GroupData, const FWmoRootData& RootData,
    const FString& WmoPath, int32 GroupIdx,
    FMpqManager* Mpq, FWowAssetCache* Cache)
{
    if (GroupData.Vertices.Num() == 0 || GroupData.Indices.Num() == 0)
    {
        return nullptr;
    }

    UStaticMesh* SM = NewObject<UWowRuntimeStaticMesh>(Cache ? Cache->GetRuntimeContext() : GetTransientPackage());

    FMeshDescription MeshDesc;
    FStaticMeshAttributes Attributes(MeshDesc);
    Attributes.Register();

    const int32 NumVerts = GroupData.Vertices.Num();

    TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
    TVertexInstanceAttributesRef<FVector3f> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
    TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
    TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
    TVertexInstanceAttributesRef<FVector4f> VertexInstanceColors = Attributes.GetVertexInstanceColors();

    // Create vertices and instances
    TArray<FVertexInstanceID> VertexInstanceIDs;
    VertexInstanceIDs.SetNum(NumVerts);

    // If we have batches, create one polygon group per batch for separate materials
    int32 NumPolyGroups = FMath::Max(1, GroupData.Batches.Num());
    TArray<FPolygonGroupID> PolyGroups;
    for (int32 i = 0; i < NumPolyGroups; ++i)
    {
        PolyGroups.Add(MeshDesc.CreatePolygonGroup());
    }

    MeshDesc.ReserveNewVertices(NumVerts);
    MeshDesc.ReserveNewVertexInstances(NumVerts);

    // r577 (Stefan: "we were supposed to only hide the black thing on the SW keep, now the keep is mostly
    // gone"): Stormwind's always-drawn far keep (group 82) is the keep's towers and roofs plus one painted
    // mountain card (MM_STRMWND_FACADE_03) - the dark blob over the keep. r562 hid the whole group.
    // r580 (Stefan: with the card gone "there's nothing else there, so we can see through ... add some other
    // walls" - he chose a stone wall): with the option on, the card keeps its shape but is drawn with the
    // keep's own stone wall (MM_STRMWND_WALL_04, the texture of the keep shell's batch 0), its UVs scaled to
    // that batch's stone size (about 54 x 20 yards per tile; the card's painting spans 67 x 51 per unit).
    TSet<int32> StoneWallBatches; TBitArray<> StoneWallVertex(false, NumVerts);
    if ((GroupData.Flags & 0x10000u) && FWowClientPreferences::GetWorldOption(TEXT("hidekeepbackdrop")))
    {
        for (int32 BatchIdx = 0; BatchIdx < GroupData.Batches.Num(); ++BatchIdx)
        {
            const FWmoGroupData::FBatch& Batch = GroupData.Batches[BatchIdx];
            if (!RootData.Materials.IsValidIndex(Batch.MaterialIndex)
                || !FPaths::GetBaseFilename(RootData.Materials[Batch.MaterialIndex].TexturePath1).Equals(TEXT("MM_STRMWND_FACADE_03"), ESearchCase::IgnoreCase))
                continue;
            StoneWallBatches.Add(BatchIdx);
            for (int32 V = Batch.VertexStart; V <= Batch.VertexEnd && V < NumVerts; ++V) StoneWallVertex[V] = true;
        }
        if (StoneWallBatches.Num() > 0)
            UE_LOG(LogTemp, Log, TEXT("WMO %s group %d: painted keep backdrop drawn as stone wall (option hidekeepbackdrop)"), *WmoPath, GroupIdx);
    }

    for (int32 i = 0; i < NumVerts; ++i)
    {
        const FVector& P = GroupData.Vertices[i];
        FVertexID VertID = MeshDesc.CreateVertex();
        // WMO model space → UE local space: (Y, X, Z) * SCALE
        // Derivation: WMO→ADT is (X,Z,-Y) [noggit3], ADT→UE is (-Z,X,Y)
        // Combined: UE.X=WMO.Y, UE.Y=WMO.X, UE.Z=WMO.Z (det=-1 handles RH→LH winding)
        VertexPositions[VertID] = FVector3f(P.Y * FWowCoordinate::SCALE, P.X * FWowCoordinate::SCALE, P.Z * FWowCoordinate::SCALE);

        FVertexInstanceID InstID = MeshDesc.CreateVertexInstance(VertID);
        VertexInstanceIDs[i] = InstID;

        FVector3f N = (i < GroupData.Normals.Num())
            ? FVector3f(GroupData.Normals[i].Y, GroupData.Normals[i].X, GroupData.Normals[i].Z)
            : FVector3f(0, 0, 1);
        N.Normalize();
        VertexInstanceNormals[InstID] = N;

        FVector3f T = FVector3f::CrossProduct(N, FVector3f(0, 1, 0));
        if (T.SizeSquared() < 0.001f)
        {
            T = FVector3f::CrossProduct(N, FVector3f(1, 0, 0));
        }
        T.Normalize();
        VertexInstanceTangents[InstID] = T;
        VertexInstanceBinormalSigns[InstID] = 1.0f;

        FVector2f UV = (i < GroupData.TexCoords.Num())
            ? FVector2f(GroupData.TexCoords[i].X, GroupData.TexCoords[i].Y)
            : FVector2f(0, 0);
        if (StoneWallVertex[i]) UV *= FVector2f(1.25f, 2.5f); // r580
        VertexInstanceUVs.Set(InstID, 0, UV);

        if (i < GroupData.VertexColors.Num())
        {
            FLinearColor LC(GroupData.VertexColors[i]);
            VertexInstanceColors[InstID] = FVector4f(LC.R, LC.G, LC.B, LC.A);
        }
    }

    // Create triangles
    if (GroupData.Batches.Num() > 0)
    {
        for (int32 BatchIdx = 0; BatchIdx < GroupData.Batches.Num(); ++BatchIdx)
        {
            const FWmoGroupData::FBatch& Batch = GroupData.Batches[BatchIdx];
            for (uint16 j = 0; j + 2 < Batch.IndexCount; j += 3)
            {
                int32 Idx0 = Batch.IndexStart + j;
                int32 Idx1 = Batch.IndexStart + j + 1;
                int32 Idx2 = Batch.IndexStart + j + 2;
                if (Idx2 >= GroupData.Indices.Num()) break;

                TArray<FVertexInstanceID> TriVerts;
                TriVerts.Add(VertexInstanceIDs[GroupData.Indices[Idx0]]);
                TriVerts.Add(VertexInstanceIDs[GroupData.Indices[Idx1]]);
                TriVerts.Add(VertexInstanceIDs[GroupData.Indices[Idx2]]);
                MeshDesc.CreatePolygon(PolyGroups[BatchIdx], TriVerts);
            }
        }
    }
    else
    {
        int32 NumTris = GroupData.Indices.Num() / 3;
        for (int32 i = 0; i < NumTris; ++i)
        {
            TArray<FVertexInstanceID> TriVerts;
            TriVerts.Add(VertexInstanceIDs[GroupData.Indices[i * 3]]);
            TriVerts.Add(VertexInstanceIDs[GroupData.Indices[i * 3 + 1]]);
            TriVerts.Add(VertexInstanceIDs[GroupData.Indices[i * 3 + 2]]);
            MeshDesc.CreatePolygon(PolyGroups[0], TriVerts);
        }
    }

    // Build materials BEFORE mesh so sections are initialized correctly
    UMaterial* FallbackMat = FWowTerrainMaterial::GetSimpleOpaqueObjectMaterial();
    if (!FallbackMat)
    {
        FallbackMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
    }

    TArray<UMaterialInterface*> SectionMaterials;
    if (GroupData.Batches.Num() > 0)
    {
        for (int32 BatchIdx = 0; BatchIdx < GroupData.Batches.Num(); ++BatchIdx)
        {
            const FWmoGroupData::FBatch& Batch = GroupData.Batches[BatchIdx];
            UMaterialInterface* Mat = FallbackMat;

            if (Batch.MaterialIndex < RootData.Materials.Num() && Cache)
            {
                const FWmoMaterial& WmoMat = RootData.Materials[Batch.MaterialIndex];
                UMaterial* BaseMat = SelectWmoRuntimeMaterial(WmoMat);
                // r535 (Stefan: the red cloth under Stormwind's arches should be real cloth): building
                // cloth gets the cloth shading only (no wind, it is fixed to the stone and timber).
                // r544: opaque passes only (alpha-keyed cloth went invisible, see WowDoodadManager).
                const WowCloth::EKind ClothKind = WmoMat.BlendMode == 0 ? WowCloth::Classify(FString(), WmoMat.TexturePath1) : WowCloth::EKind::None;
                UMaterial* ClothMat = ClothKind != WowCloth::EKind::None ? WowCloth::GetMaterial() : nullptr;
                if (ClothMat) BaseMat = ClothMat;
                UTexture2D* Tex = nullptr;
                if (StoneWallBatches.Contains(BatchIdx)) // r580
                {
                    Tex = FWowTerrainMaterial::LoadBlpTexture(TEXT("DUNGEONS\\TEXTURES\\WALLS\\MM_STRMWND_WALL_04.BLP"), Mpq, Cache);
                }
                else if (!WmoMat.TexturePath1.IsEmpty())
                {
                    Tex = FWowTerrainMaterial::LoadBlpTexture(WmoMat.TexturePath1, Mpq, Cache);
                }

                // r310: anisotropic filtering for WMO surfaces too (streets, floors and walls seen
                // at grazing angles smeared with trilinear only).
                if (Tex && Tex->Filter != TF_Default && WmoMat.BlendMode == 0)
                {
                    Tex->LODGroup = TEXTUREGROUP_World;
                    Tex->Filter = TF_Default;
                    Tex->UpdateResource();
                }
                if (Tex && BaseMat)
                {
                    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, SM);
                    MID->SetTextureParameterValue(FName(TEXT("Layer0Texture")), Tex);
                    // r586 (Stefan, Stormwind in rain: "no effects for rain on the ground"): indoor groups never got wet, and
                    // the city's districts are indoor groups - large open-air "rooms" (footprint over 10,000 square
                    // yards, the same rule as the portal culling's districts). Those get the outdoor wetness too.
                    const FVector DistrictSize = GroupData.BoundingBox.GetSize();
                    const bool bOpenDistrict = (GroupData.Flags & 0x2000u) && !(GroupData.Flags & 0x10000u)
                        && DistrictSize.X * DistrictSize.Y > 10000.0;
                    MID->SetScalarParameterValue(TEXT("WeatherExposure"),((GroupData.Flags & 0x2000u) && !bOpenDistrict) ? 0.f : 1.f);
                    if (ClothMat) WowCloth::Configure(MID, WowCloth::EKind::Flat, WmoMat.BlendMode == 1, 0.f, 1.f);
                    // r737: the stone relief's gap side for the texture actually drawn (r580 walls draw MM_STRMWND_WALL_04).
                    if (!ClothMat && WmoMat.BlendMode == 0 && IsCobblestoneFloorTexture(WmoMat.TexturePath1.Replace(TEXT("/"), TEXT("\\"))))
                        MID->SetScalarParameterValue(TEXT("StoneSign"), CobblestoneReliefSign(StoneWallBatches.Contains(BatchIdx)
                            ? FString(TEXT("MM_STRMWND_WALL_04")) : WmoMat.TexturePath1.Replace(TEXT("/"), TEXT("\\"))));
                    Mat = MID;
                }
            }
            SectionMaterials.Add(Mat);
        }
    }
    else
    {
        SectionMaterials.Add(FallbackMat);
    }

    // Add materials to static mesh before building
    for (UMaterialInterface* Mat : SectionMaterials)
    {
        FStaticMaterial StaticMaterial(Mat);
        StaticMaterial.UVChannelData.bInitialized = true;
        SM->GetStaticMaterials().Add(MoveTemp(StaticMaterial));
    }
	SM->bAllowCPUAccess = true;

    // Build mesh
    TArray<const FMeshDescription*> MeshDescs;
    MeshDescs.Add(&MeshDesc);
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bBuildSimpleCollision = false;
    Params.bFastBuild = true;
    Params.bCommitMeshDescription = false;
    WowPrepareRuntimeMeshMaterials(SM, const_cast<FMeshDescription&>(*MeshDescs[0]));
    SM->BuildFromMeshDescriptions(MeshDescs, Params);
    WowBuildRuntimeLumenCards(SM, MeshDesc);
    SM->NeverStream = true;

	if (!SM->GetBodySetup())
	{
		SM->CreateBodySetup();
	}
	if (UBodySetup* BodySetup = SM->GetBodySetup())
	{
		BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
		BodySetup->bMeshCollideAll = true;
		BodySetup->CreatePhysicsMeshes();
	}

    return SM;
}

uint32 FWowWmoRenderer::GetWmoGroupCount(const FString& WmoPath, FMpqManager* Mpq)
{
    if (!Mpq || WmoPath.IsEmpty())
    {
        return 0;
    }

    TArray<uint8> RootRaw;
    if (!Mpq->ReadFile(WmoPath, RootRaw))
    {
        return 0;
    }

    FWmoRootData RootData = FWmoParser::ParseRoot(RootRaw);
    if (!RootData.bIsValid)
    {
        return 0;
    }

    return RootData.NumGroups;
}
