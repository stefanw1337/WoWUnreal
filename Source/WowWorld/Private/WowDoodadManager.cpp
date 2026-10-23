#include "WowDoodadManager.h"
#include "WowLampFixtures.h"
#include "WowFixturePolicy.h"
#include "WowDuskLightComponent.h"
#include "WowWaterSprayComponent.h"
#include "WowRuntimeMeshMaterials.h"
#include "WowRuntimeLumen.h"
#include "WowRuntimeStaticMesh.h"
#include "Mpq/MpqManager.h"
#include "WowAssetCache.h"
#include "WowSkeletalMeshBuilder.h"
#include "Formats/M2Parser.h"
#include "Formats/M2Types.h"
#include "Formats/BlpParser.h"
#include "Formats/BlpTypes.h"
#include "WowTextureFactory.h"
#include "WowTerrainMaterial.h"
#include "Coord/WowCoordinate.h"
#include "Formats/AdtTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WowM2ParticleComponent.h"
#include "WowSpellMeshComponent.h"
#include "WowClientPreferences.h"
#include "WowClothMaterial.h"
UMaterial* WowGetM2LayeredMaterial(int32 Kind); // r739: WowM2LayeredMaterial.cpp (0 opaque, 1 translucent, 2 additive)

DEFINE_LOG_CATEGORY_STATIC(LogWowDoodad, Log, All);

TMap<FString, TSharedPtr<FM2Data>> FWowDoodadManager::ParsedM2Cache;
FCriticalSection FWowDoodadManager::CacheLock;
TMap<FString, TWeakObjectPtr<USkeleton>> FWowDoodadManager::SkeletonCache;
TMap<FString, TArray<TWeakObjectPtr<UAnimSequence>>> FWowDoodadManager::AnimCache;

void FWowDoodadManager::ClearCaches()
{
    FScopeLock Lock(&CacheLock);
    ParsedM2Cache.Reset();
    SkeletonCache.Reset();
    AnimCache.Reset();
}

namespace
{
	FString NormalizeM2AssetPath(FString Path)
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

	bool IsSoftDetailDoodadPath(const FString& NormalizedPath)
	{
		return NormalizedPath.Contains(TEXT("bush")) ||
			NormalizedPath.Contains(TEXT("brush")) ||
			NormalizedPath.Contains(TEXT("underbrush")) ||
			NormalizedPath.Contains(TEXT("shrub")) ||
			NormalizedPath.Contains(TEXT("hedge")) ||
			NormalizedPath.Contains(TEXT("bramble")) ||
			NormalizedPath.Contains(TEXT("briar")) ||
			NormalizedPath.Contains(TEXT("sapling")) ||
			NormalizedPath.Contains(TEXT("grass")) ||
			NormalizedPath.Contains(TEXT("flower")) ||
			NormalizedPath.Contains(TEXT("fern")) ||
			NormalizedPath.Contains(TEXT("lilypad")) ||
			NormalizedPath.Contains(TEXT("leaf")) ||
			NormalizedPath.Contains(TEXT("leaves")) ||
			NormalizedPath.Contains(TEXT("vine")) ||
			NormalizedPath.Contains(TEXT("catail")) ||
			NormalizedPath.Contains(TEXT("cattail")) ||
			NormalizedPath.Contains(TEXT("reed"));
	}

	bool IsTreeLikeDoodadPath(const FString& NormalizedPath)
	{
		return NormalizedPath.Contains(TEXT("\\trees\\")) ||
			NormalizedPath.Contains(TEXT("tree")) ||
			NormalizedPath.Contains(TEXT("oak")) ||
			NormalizedPath.Contains(TEXT("pine")) ||
			NormalizedPath.Contains(TEXT("willow")) ||
			NormalizedPath.Contains(TEXT("maple")) ||
			NormalizedPath.Contains(TEXT("elm")) ||
			NormalizedPath.Contains(TEXT("fir")) ||
			NormalizedPath.Contains(TEXT("trunk")) ||
			NormalizedPath.Contains(TEXT("stump"));
	}

	bool IsSoftPassThroughTreePartPath(const FString& NormalizedPath)
	{
		return NormalizedPath.Contains(TEXT("treefacade")) ||
			NormalizedPath.Contains(TEXT("canopy01")) ||
			NormalizedPath.Contains(TEXT("canopy02")) ||
			NormalizedPath.Contains(TEXT("canopy03")) ||
			NormalizedPath.Contains(TEXT("branches")) ||
			NormalizedPath.Contains(TEXT("branch")) ||
			NormalizedPath.Contains(TEXT("twig")) ||
			NormalizedPath.Contains(TEXT("bush")) ||
			NormalizedPath.Contains(TEXT("brush")) ||
			NormalizedPath.Contains(TEXT("underbrush")) ||
			NormalizedPath.Contains(TEXT("shrub")) ||
			NormalizedPath.Contains(TEXT("hedge")) ||
			NormalizedPath.Contains(TEXT("bramble")) ||
			NormalizedPath.Contains(TEXT("briar")) ||
			NormalizedPath.Contains(TEXT("sapling")) ||
			NormalizedPath.Contains(TEXT("leaf")) ||
			NormalizedPath.Contains(TEXT("leaves")) ||
			NormalizedPath.Contains(TEXT("vine")) ||
			NormalizedPath.Contains(TEXT("grass")) ||
			NormalizedPath.Contains(TEXT("flower")) ||
			NormalizedPath.Contains(TEXT("fern")) ||
			NormalizedPath.Contains(TEXT("reed")) ||
			NormalizedPath.Contains(TEXT("catail")) ||
			NormalizedPath.Contains(TEXT("cattail")) ||
			NormalizedPath.Contains(TEXT("lilypad"));
	}

	bool IsPureVisualEffectDoodadPath(const FString& NormalizedPath)
	{
		return NormalizedPath.Contains(TEXT("lightshaft")) ||
			NormalizedPath.Contains(TEXT("volumetriclight")) ||
			NormalizedPath.Contains(TEXT("\\particleemitters\\")) ||
			NormalizedPath.Contains(TEXT("\\effects\\")) ||
			NormalizedPath.Contains(TEXT("flameonly")) ||
			NormalizedPath.Contains(TEXT("fireonly")) ||
			NormalizedPath.Contains(TEXT("smoke")) ||
			NormalizedPath.Contains(TEXT("glow")) ||
			NormalizedPath.Contains(TEXT("lightbeam")) ||
			NormalizedPath.Contains(TEXT("mist")) ||
			NormalizedPath.Contains(TEXT("steam")) ||
			NormalizedPath.Contains(TEXT("splash"));
	}

	bool IsSolidFixtureDoodadPath(const FString& M2Path)
	{
		FString Path = M2Path.ToLower();
		Path.ReplaceInline(TEXT("/"), TEXT("\\"));
		return Path.Contains(TEXT("lamppost")) ||
			Path.Contains(TEXT("lamp")) ||
			Path.Contains(TEXT("lantern")) ||
			Path.Contains(TEXT("fixture")) ||
			Path.Contains(TEXT("sconce")) ||
			Path.Contains(TEXT("torch")) ||
			Path.Contains(TEXT("brazier")) ||
			Path.Contains(TEXT("pyre")) ||
			Path.Contains(TEXT("hearth")) ||
			Path.Contains(TEXT("fireplace")) ||
			Path.Contains(TEXT("candelabra")) ||
			Path.Contains(TEXT("chandelier")) ||
			Path.Contains(TEXT("firepit")) ||
			Path.Contains(TEXT("campfire"));
	}

	bool IsTreeTrunkDoodadPath(const FString& M2Path)
	{
		FString Path = M2Path.ToLower();
		Path.ReplaceInline(TEXT("/"), TEXT("\\"));

		if (!IsTreeLikeDoodadPath(Path))
		{
			return false;
		}

		if (Path.Contains(TEXT("canopylesstree")) ||
			Path.Contains(TEXT("treecanopy")) ||
			Path.Contains(TEXT("trunk")) ||
			Path.Contains(TEXT("stump")))
		{
			return true;
		}

		if (IsSoftPassThroughTreePartPath(Path))
		{
			return false;
		}

		// These get a separate capsule blocker because runtime complex collision
		// on instanced M2 meshes is unreliable and full tree mesh collision would
		// make the canopy act like an invisible wall.
		return Path.Contains(TEXT("\\trees\\")) ||
			Path.Contains(TEXT("tree")) ||
			Path.Contains(TEXT("oak")) ||
			Path.Contains(TEXT("pine")) ||
			Path.Contains(TEXT("willow")) ||
			Path.Contains(TEXT("maple")) ||
			Path.Contains(TEXT("elm")) ||
			Path.Contains(TEXT("fir"));
	}

	// Ambient animals placed as ADT doodads (bird flocks, butterflies, insects, bats,
	// fish schools): their looping animation is the whole point, so they get the
	// CPU-animated mesh instead of a frozen instanced copy. Matched on the file name.
	bool IsAmbientAnimalDoodadPath(const FString& M2Path)
	{
		// The data keeps ambient animals in World\Critter\ (Birds\Bird01, Fish\Fish01, ...).
		FString Path = M2Path.ToLower();
		Path.ReplaceInline(TEXT("/"), TEXT("\\"));
		if (Path.Contains(TEXT("\\critter\\"))) return true;
		const FString Name = FPaths::GetBaseFilename(M2Path).ToLower();
		static const TCHAR* const Keys[] = {
			TEXT("bird"), TEXT("butterfl"), TEXT("seagull"), TEXT("gull"), TEXT("crow"), TEXT("raven"),
			TEXT("vulture"), TEXT("buzzard"), TEXT("moth"), TEXT("firefl"), TEXT("dragonfl"), TEXT("parrot"),
			TEXT("pigeon"), TEXT("hawk"), TEXT("insect"), TEXT("flies"), TEXT("bats"), TEXT("fishschool"),
			TEXT("schooloffish"), TEXT("swarm"), TEXT("gnats"),
			// r324 (Stefan: the Stormwind gryphon roost bird sat in its bind pose, wings straight out):
			// roost/perch birds of the flight masters idle-animate in the original.
			TEXT("gryphonroost"), TEXT("griffonroost"), TEXT("hippogryphroost"), TEXT("wyvernroost"),
			TEXT("eagle") };
		for (const TCHAR* Key : Keys) if (Name.Contains(Key)) return true;
		return false;
	}

	// Data-driven catch-all for ambient movers that are not named like animals:
	// the looping stand animation carries a bone along a path that is large
	// compared with the model itself (birds circling, fish swimming, bugs
	// orbiting a lamp). Swaying trees and waving flags only rotate/translate a
	// little and stay instanced.
	bool HasTravellingAnimation(const FM2Data& Data)
	{
		if (Data.AnimationTracks.IsEmpty() || Data.Vertices.Num() > 4096) return false;
		float Travel = 0.0f;
		for (const FM2BoneTrack& Track : Data.AnimationTracks[0].BoneTracks)
		{
			if (Track.TransValues.Num() < 2) continue;
			FBox Path(ForceInit);
			for (const FVector& Value : Track.TransValues) Path += Value;
			Travel = FMath::Max(Travel, static_cast<float>(Path.GetSize().GetMax()));
		}
		const float ModelSize = FMath::Max(Data.BoundingSphereRadius, 0.1f);
		return Travel > 1.5f && Travel > ModelSize * 0.6f;
	}

	// r561 (Stefan, Stormwind barber shop: "we also need an animation on this"): doodads whose stand
	// animation turns a bone right round - barber poles (a full turn every 3.2 s on a global
	// sequence), windmill sails, water wheels, gears. A bone whose rotation gets 115 degrees or more away
	// from its first key turns; swaying trees and waving banners stay well below that and stay instanced.
	bool HasSpinningAnimation(const FM2Data& Data)
	{
		if (Data.AnimationTracks.IsEmpty() || Data.Vertices.Num() > 4096) return false;
		// r571 (Stefan: "Portal is now missing"): effect models turn bones to carry their emitters round
		// (the instance portal's swirl). Taking the skinned path left them without their particles, so
		// models with emitters keep the normal path (their particle component animates the bones).
		if (Data.ParticleEmitters.Num() > 0 || Data.RibbonEmitters.Num() > 0) return false;
		for (const FM2BoneTrack& Track : Data.AnimationTracks[0].BoneTracks)
		{
			if (Track.RotValues.Num() < 3) continue;
			const FQuat First = Track.RotValues[0].GetNormalized();
			for (const FQuat& Value : Track.RotValues)
				if (First.AngularDistance(Value.GetNormalized()) >= 2.0) return true;
		}
		return false;
	}

	bool IsWaterfallDoodadPath(const FString& M2Path)
	{
		FString Path = M2Path.ToLower();
		Path.ReplaceInline(TEXT("/"), TEXT("\\"));
		// r739 (Stefan 23.10, TormentWoW's Ulduar: "white ghosted textures"): ZulDrak_WaterFog_01 lives in Zul'Drak's
		// WATERFALLS folder but is a 120-yard additive fog layer (Howling Fjord fog texture); as a "waterfall" it got
		// the streaky sheet material and spray. Fog and mist models keep their authored look.
		const FString Leaf = FPaths::GetBaseFilename(Path);
		if (Leaf.Contains(TEXT("fog")) || Leaf.Contains(TEXT("mist"))) return false;
		// r659 (Stefan, Burning Blade Coven: "water is missing" - "we replace all elements with UE's own material"):
		// ParticleEmitters\\MountainCaveRiver.m2 is the cave's stream (a mesh with WoW's waterfall texture). Like the
		// waterfalls it is drawn with our UE waterfall material and spray, not skipped as an effect card and not with
		// the original texture.
		return Path.Contains(TEXT("waterfall")) ||
			Path.Contains(TEXT("\\falls\\")) ||
			(Path.Contains(TEXT("\\particleemitters\\")) && (Path.Contains(TEXT("river")) || Path.Contains(TEXT("stream"))));
	}

	bool IsLegacyWaterEffectCardPath(const FString& M2Path)
	{
		FString Path = M2Path.ToLower();
		Path.ReplaceInline(TEXT("/"), TEXT("\\"));

		// Waterbasin and similar WoW doodads are mostly camera-facing alpha
		// effect cards. The real river/pond surface is generated from liquid
		// data, so importing these cards as ordinary static meshes creates black
		// papp triangles over otherwise-correct UE water.
		// r437 (Stefan, Northshire: "isn't there water at this fountain?"): WaterBasin.m2 is a wooden
		// water trough (wood, stone sides and a half-transparent water surface), not an effect card;
		// skipping it removed all four troughs around Northshire Abbey. It is drawn again, its water
		// pass blended as authored.
		return Path.Contains(TEXT("\\waterfall\\newwaterfall")) ||
			Path.Contains(TEXT("newwaterfall")) ||
			Path.Contains(TEXT("\\particleemitters\\")) ||
			Path.Contains(TEXT("lightshaft")) ||
			Path.Contains(TEXT("volumetriclight"));
	}

	bool ShouldUseFoliageMaterialForDoodadBatch(const FString& M2Path, const FString& TexturePath)
	{
		FString Path = M2Path.ToLower();
		Path.ReplaceInline(TEXT("/"), TEXT("\\"));

		FString Texture = TexturePath.ToLower();
		Texture.ReplaceInline(TEXT("/"), TEXT("\\"));

		// r604 (Stefan, Tirisfall: "this tree is floating, check the transparency on this" - a stump with
		// the crown hanging in the air above it): bark is never foliage. TirrisFallCanopyTree01_Trunk matched
		// "canopy", and the foliage material cuts out every texel darker than about 1.5 % (its black-padding
		// mask for leaf cards), so the dark middle of the trunk vanished. Trunk and bark passes keep the
		// normal (alpha-key) object material.
		if (Texture.Contains(TEXT("trunk")) || Texture.Contains(TEXT("bark"))) return false;

		return IsSoftDetailDoodadPath(Path) ||
			Path.Contains(TEXT("treecanopy")) ||
			Path.Contains(TEXT("canopylesstree")) ||
			Path.Contains(TEXT("treefacade")) ||
			Texture.Contains(TEXT("leaf")) ||
			Texture.Contains(TEXT("leaves")) ||
			Texture.Contains(TEXT("canopy")) ||
			Texture.Contains(TEXT("foliage")) ||
			Texture.Contains(TEXT("bush"));
	}

	bool ShouldDoodadEmitPointLight(const FString& M2Path)
	{
		FString Path = M2Path.ToLower();
		Path.ReplaceInline(TEXT("/"), TEXT("\\"));

		if (Path.Contains(TEXT("lightshaft")) ||
			Path.Contains(TEXT("volumetriclight")) ||
			Path.Contains(TEXT("\\particleemitters\\")))
		{
			return false;
		}

		// Keywords are matched in the FILE name only (r237): "forge" in the folder name lit every
		// Ironforge/Dun Morogh doodad (trees, stone fences) with a point light at its base -
		// Stefan's glowing snow caps on fence posts and the bright line round tree feet.
		// Folders that really hold light fixtures are still matched as folders.
		const FString File = FPaths::GetBaseFilename(Path);
		// r416: unlit variants ("OrcBonFireOff") stay dark.
		if (File.EndsWith(TEXT("off")) || File.Contains(TEXT("fireoff")))
			return false;
		if (Path.Contains(TEXT("\\lights\\")) || Path.Contains(TEXT("\\lamps\\")) || Path.Contains(TEXT("\\fire\\")) ||
			Path.Contains(TEXT("\\lanterns\\")) || Path.Contains(TEXT("\\braziers\\")) || Path.Contains(TEXT("\\torches\\")) ||
			Path.Contains(TEXT("\\candles\\")) || Path.Contains(TEXT("\\lampposts\\")))
			return true;
		// r416 (Stefan: the orc cooking fire gave almost no light with realistic lighting):
		// OrcTableCooker01Fire and other burning doodads named "...fire" had no light at all;
		// only their flame sprites glowed. Not fireworks, fireflies or plain firewood.
		if (File.Contains(TEXT("fire")) && !File.Contains(TEXT("firework")) && !File.Contains(TEXT("firefl"))
			&& !File.Contains(TEXT("firewood")))
			return true;
		return File.Contains(TEXT("campfire")) ||
			File.Contains(TEXT("firepit")) ||
			File.Contains(TEXT("bonfire")) ||
			File.Contains(TEXT("pyre")) ||
			File.Contains(TEXT("hearth")) ||
			File.Contains(TEXT("fireplace")) ||
			(File.Contains(TEXT("forge")) && !File.Contains(TEXT("ironforge"))) ||
			File.Contains(TEXT("lantern")) ||
			File.Contains(TEXT("streetlamp")) ||
			File.Contains(TEXT("torch")) ||
			File.Contains(TEXT("candle")) ||
			File.Contains(TEXT("candelabra")) ||
			File.Contains(TEXT("brazier")) ||
			File.Contains(TEXT("chandelier")) ||
			File.Contains(TEXT("lamp"));
	}

	// r431: WoW's flat light cards removed from the meshes (FWowFixturePolicy::IsLegacyGlowCard),
	// recorded per model when its mesh is built, so the doodad can get our light instead.
	struct FLegacyGlowCard { FVector LocalUE = FVector::ZeroVector; float RadiusCm = 0.f; FLinearColor Color = FLinearColor::White; };
	TMap<FString, TArray<FLegacyGlowCard>> GLegacyGlowCards;
	FCriticalSection GLegacyGlowLock;
	FString LegacyGlowKey(const FString& Path)
	{
		FString Key = Path.ToUpper();
		Key.ReplaceInline(TEXT("/"), TEXT("\\"));
		Key.RemoveFromEnd(TEXT(".MDX")); Key.RemoveFromEnd(TEXT(".MDL")); Key.RemoveFromEnd(TEXT(".M2"));
		return Key;
	}
	FLinearColor LegacyGlowColour(const FString& Texture, const FM2RenderPass& Pass)
	{
		const FString T = Texture.ToUpper();
		FLinearColor C(1.0f, 0.82f, 0.6f);
		if (T.Contains(TEXT("BLUE"))) C = FLinearColor(0.45f, 0.62f, 1.0f);
		else if (T.Contains(TEXT("GREEN"))) C = FLinearColor(0.5f, 1.0f, 0.5f);
		else if (T.Contains(TEXT("PURPLE")) || T.Contains(TEXT("VIOLET"))) C = FLinearColor(0.72f, 0.45f, 1.0f);
		else if (T.Contains(TEXT("RED"))) C = FLinearColor(1.0f, 0.38f, 0.26f);
		else if (T.Contains(TEXT("ORANGE")) || T.Contains(TEXT("YELLOW")) || T.Contains(TEXT("FIRE"))) C = FLinearColor(1.0f, 0.66f, 0.34f);
		auto First = [](const FM2MaterialTrack& Track) { return Track.Values.Num() > 0 && FMath::IsFinite(Track.Values[0]) ? Track.Values[0] : 1.f; };
		const FLinearColor Pass3(First(Pass.ColorRed), First(Pass.ColorGreen), First(Pass.ColorBlue));
		if (Pass3.GetMax() > 0.05f && !Pass3.Equals(FLinearColor(1, 1, 1), 0.05f)) C = C * (Pass3 / Pass3.GetMax());
		return C;
	}
	void RecordLegacyGlowCards(const FM2Data& Data, const FString& M2Path)
	{
		TArray<FLegacyGlowCard> Cards;
		// r444: spell models keep their glow cards (FWowFixturePolicy::IsSpellModel), so no light replaces them.
		for (const FM2RenderPass& Pass : FWowFixturePolicy::IsSpellModel(M2Path) ? TArray<FM2RenderPass>() : Data.RenderPasses)
		{
			if (!FWowFixturePolicy::IsLegacyGlowCard(Data, Pass) || Cards.Num() >= 2) continue;
			const FM2Submesh& Sub = Data.Submeshes[Pass.SubmeshIndex];
			FVector Centre = FVector::ZeroVector; float Reach = 0.f; int32 Count = 0;
			for (int32 V = Sub.StartVertex; V < Sub.StartVertex + Sub.NumVertices && V < Data.Vertices.Num(); ++V) { Centre += FVector(Data.Vertices[V].Position); ++Count; }
			if (!Count) continue;
			Centre /= Count;
			for (int32 V = Sub.StartVertex; V < Sub.StartVertex + Sub.NumVertices && V < Data.Vertices.Num(); ++V)
				Reach = FMath::Max(Reach, float(FVector::Dist(FVector(Data.Vertices[V].Position), Centre)));
			FLegacyGlowCard Card;
			Card.LocalUE = FVector(Centre.Y, Centre.X, Centre.Z) * FWowCoordinate::SCALE;
			Card.RadiusCm = Reach * FWowCoordinate::SCALE;
			Card.Color = LegacyGlowColour(Data.TexturePaths[Pass.TextureIndex], Pass);
			bool bDuplicate = false;
			for (const FLegacyGlowCard& Other : Cards) bDuplicate |= FVector::Dist(Other.LocalUE, Card.LocalUE) < 20.0;
			if (!bDuplicate) Cards.Add(Card);
		}
		FScopeLock Lock(&GLegacyGlowLock);
		if (Cards.Num()) GLegacyGlowCards.Add(LegacyGlowKey(M2Path), Cards); else GLegacyGlowCards.Remove(LegacyGlowKey(M2Path));
	}

	void CreateDoodadPointLight(AActor* ParentActor, USceneComponent* AttachParent, const FString& M2Path,
		const FVector& WorldLocation, uint32 UniqueId, int32 InstanceIndex, const FTransform* DoodadTransform = nullptr)
	{
		if (ParentActor && !ShouldDoodadEmitPointLight(M2Path))
		{
			// r431: no fire/lamp light of its own - our light where WoW drew its flat glow card.
			FWowDoodadManager::AddLegacyGlowLights(ParentActor, AttachParent, M2Path, UniqueId, InstanceIndex, DoodadTransform);
			return;
		}
		if (!ParentActor || !ShouldDoodadEmitPointLight(M2Path) || FWowFixturePolicy::IsUnlit(M2Path) || FWowLampFixtures::IsSupported(M2Path))
		{
			return;
		}

		FString Path = M2Path.ToLower();
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

		const FName LightName = *FString::Printf(TEXT("DoodadLight_%u_%d"), UniqueId, InstanceIndex);
		UPointLightComponent* LightComp = NewObject<UPointLightComponent>(ParentActor, LightName);
		if (!LightComp)
		{
			return;
		}

		LightComp->SetupAttachment(AttachParent ? AttachParent : ParentActor->GetRootComponent());
		ParentActor->AddInstanceComponent(LightComp);
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
        LightComp->SetIndirectLightingIntensity(0.0f);
        LightComp->ShadowResolutionScale=.5f;
        // r352 (Stefan: shadows still razor sharp under the lamps): every fire light gets the lamp
        // fixtures' soft area source (r321/r322), not only WowLampFixtures.
        LightComp->SetSourceRadius(8.f*FWowClientPreferences::GetFireLightSoftness());
        LightComp->SetSoftSourceRadius(12.f*FWowClientPreferences::GetFireLightSoftness());
        LightComp->SetSpecularScale(.35f);
        LightComp->SetMaxDrawDistance(5000.f);
        LightComp->MaxDistanceFadeRange=1000.f;
		// r355: hanging lanterns hang below their pivot - light them from the lantern body (no self-shadow).
		UStaticMeshComponent* HangingMesh = Path.Contains(TEXT("hanging")) ? Cast<UStaticMeshComponent>(AttachParent) : nullptr;
		if (HangingMesh && HangingMesh->GetStaticMesh())
		{
			LightComp->SetWorldLocation(HangingMesh->GetComponentTransform().TransformPosition(HangingMesh->GetStaticMesh()->GetBounds().Origin));
			HangingMesh->SetCastShadow(false);
		}
		// r418 (Stefan: the brazier's light lit its tripod legs from inside): a brazier is lit from just
		// above its bowl, at any scale (game-object braziers are 1.27x), so the bowl shades its legs.
		else if (UStaticMeshComponent* BrazierMesh = Path.Contains(TEXT("brazier")) ? Cast<UStaticMeshComponent>(AttachParent) : nullptr;
			BrazierMesh && BrazierMesh->GetStaticMesh())
		{
			const FBoxSphereBounds Bowl = BrazierMesh->CalcBounds(BrazierMesh->GetComponentTransform());
			LightComp->SetWorldLocation(FVector(Bowl.Origin.X, Bowl.Origin.Y, Bowl.Origin.Z + Bowl.BoxExtent.Z + 25.0f));
		}
		else
			LightComp->SetWorldLocation(WorldLocation + FVector(0.0f, 0.0f, 140.0f));
		LightComp->RegisterComponent();
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

	bool IsDoodadCollisionDebugEnabled()
	{
		static int32 bEnabled = -1;
		if (bEnabled < 0)
		{
			bEnabled = FParse::Param(FCommandLine::Get(), TEXT("debugdoodadcollision")) ? 1 : 0;
		}
		return bEnabled != 0;
	}

	bool IsNearWaterfallDebugPoint(const FVector& Location, float Radius = 65000.0f)
	{
		static const FVector DebugPoint(-853333.0f, 9375.0f, 13000.0f);
		return FVector::DistSquared(Location, DebugPoint) <= FMath::Square(Radius);
	}

	FVector M2VertexToUELocal(const FM2Vertex& Vertex)
	{
		// WoW model space -> UE local: (Y, X, Z) * SCALE.
		// This matches the normal M2 static-mesh conversion below.
		return FVector(
			Vertex.Position.Y * FWowCoordinate::SCALE,
			Vertex.Position.X * FWowCoordinate::SCALE,
			Vertex.Position.Z * FWowCoordinate::SCALE);
	}

	FVector M2PointToUELocal(const FVector& Point)
	{
		return FVector(
			Point.Y * FWowCoordinate::SCALE,
			Point.X * FWowCoordinate::SCALE,
			Point.Z * FWowCoordinate::SCALE);
	}

	// Can the player step up onto this doodad's authored collision? (r269, Stefan: stopped by
	// kerbs and wooden stairs.) Doodad blockers refused every step-up (ECB_No) so the capsule
	// would not climb fences and crates, but low pieces (kerbs, steps, platforms) and stairs
	// built as doodads then became walls. Allow it when the whole piece is no taller than
	// a step, or when it has flat treads at three or more heights (a staircase / terrace).
	bool AuthoredCollisionAllowsStepUp(const FM2Data* ParsedM2, const FVector& AbsScale)
	{
		if (!ParsedM2 || ParsedM2->CollisionVertices.Num() < 3) return false;
		FBox Box(ForceInit);
		for (const FVector& V : ParsedM2->CollisionVertices) Box += M2PointToUELocal(V) * AbsScale;
		if (!Box.IsValid) return false;
		const double Height = Box.Max.Z - Box.Min.Z;
		if (Height <= 60.0) return true;
		TSet<int32> TreadLevels;
		const TArray<FVector>& Verts = ParsedM2->CollisionVertices;
		const TArray<uint16>& Idx = ParsedM2->CollisionIndices;
		for (int32 I = 0; I + 2 < Idx.Num(); I += 3)
		{
			if (!Verts.IsValidIndex(Idx[I]) || !Verts.IsValidIndex(Idx[I + 1]) || !Verts.IsValidIndex(Idx[I + 2])) continue;
			const FVector A = M2PointToUELocal(Verts[Idx[I]]) * AbsScale;
			const FVector B = M2PointToUELocal(Verts[Idx[I + 1]]) * AbsScale;
			const FVector C = M2PointToUELocal(Verts[Idx[I + 2]]) * AbsScale;
			const FVector Cross = FVector::CrossProduct(B - A, C - A);
			const double Area2 = Cross.Size();
			if (Area2 < 2.0 * 400.0) continue; // under ~20x20 cm: rails, trims
			if (FMath::Abs(Cross.Z) / Area2 < 0.9) continue; // not a flat tread
			const double Z = (A.Z + B.Z + C.Z) / 3.0 - Box.Min.Z;
			if (Z < 5.0) continue; // the floor footprint
			TreadLevels.Add(FMath::RoundToInt32(Z / 10.0));
		}
		return TreadLevels.Num() >= 3;
	}

	UStaticMesh* GetOrCreateAuthoredDoodadCollisionMesh(
		const FString& M2Path, const FM2Data* ParsedM2, UObject* Context)
	{
		if (!ParsedM2 || ParsedM2->CollisionVertices.Num() < 3 ||
			ParsedM2->CollisionIndices.Num() < 3)
		{
			return nullptr;
		}

		static TMap<FString, TWeakObjectPtr<UStaticMesh>> CollisionMeshCache;
		const FString CacheKey = NormalizeM2AssetPath(M2Path).ToLower();
		if (const TWeakObjectPtr<UStaticMesh>* Cached = CollisionMeshCache.Find(CacheKey))
		{
			if (Cached->IsValid())
			{
				return Cached->Get();
			}
		}

		UStaticMesh* CollisionMesh = NewObject<UWowRuntimeStaticMesh>(Context->GetWorld());
		CollisionMesh->bAllowCPUAccess = true;

		FMeshDescription MeshDesc;
		FStaticMeshAttributes Attributes(MeshDesc);
		Attributes.Register();
		TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
		TVertexInstanceAttributesRef<FVector4f> VertexInstanceColors = Attributes.GetVertexInstanceColors();
		VertexInstanceUVs.SetNumChannels(1);
		const FPolygonGroupID PolygonGroup = MeshDesc.CreatePolygonGroup();

		TArray<FVertexID> VertexIds;
		TArray<FVector> LocalPositions;
		VertexIds.Reserve(ParsedM2->CollisionVertices.Num());
		LocalPositions.Reserve(ParsedM2->CollisionVertices.Num());
		for (const FVector& SourcePoint : ParsedM2->CollisionVertices)
		{
			const FVector LocalPoint = M2PointToUELocal(SourcePoint);
			const FVertexID VertexId = MeshDesc.CreateVertex();
			VertexPositions[VertexId] = FVector3f(LocalPoint);
			VertexIds.Add(VertexId);
			LocalPositions.Add(LocalPoint);
		}

		int32 CreatedTriangleCount = 0;
		for (int32 Index = 0; Index + 2 < ParsedM2->CollisionIndices.Num(); Index += 3)
		{
			const int32 I0 = ParsedM2->CollisionIndices[Index];
			const int32 I1 = ParsedM2->CollisionIndices[Index + 1];
			const int32 I2 = ParsedM2->CollisionIndices[Index + 2];
			if (!VertexIds.IsValidIndex(I0) || !VertexIds.IsValidIndex(I1) ||
				!VertexIds.IsValidIndex(I2))
			{
				continue;
			}

			const FVector Edge = LocalPositions[I1] - LocalPositions[I0];
			const FVector Normal = FVector::CrossProduct(
				Edge, LocalPositions[I2] - LocalPositions[I0]).GetSafeNormal();
			if (Normal.IsNearlyZero())
			{
				continue;
			}
			const FVector Tangent = Edge.GetSafeNormal();
			TArray<FVertexInstanceID, TInlineAllocator<3>> Instances;
			for (const int32 VertexIndex : { I0, I1, I2 })
			{
				const FVertexInstanceID InstanceId =
					MeshDesc.CreateVertexInstance(VertexIds[VertexIndex]);
				VertexInstanceNormals[InstanceId] = FVector3f(Normal);
				VertexInstanceTangents[InstanceId] = FVector3f(Tangent);
				VertexInstanceBinormalSigns[InstanceId] = 1.0f;
				VertexInstanceUVs.Set(InstanceId, 0, FVector2f::ZeroVector);
				VertexInstanceColors[InstanceId] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
				Instances.Add(InstanceId);
			}
			MeshDesc.CreatePolygon(PolygonGroup, Instances);
			++CreatedTriangleCount;
		}

		if (CreatedTriangleCount == 0)
		{
			return nullptr;
		}

		UMaterialInterface* CollisionMaterial =
			LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
		FStaticMaterial StaticMaterial(CollisionMaterial);
		StaticMaterial.UVChannelData.bInitialized = true;
		CollisionMesh->GetStaticMaterials().Add(MoveTemp(StaticMaterial));

		TArray<const FMeshDescription*> MeshDescriptions;
		MeshDescriptions.Add(&MeshDesc);
		UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
		BuildParams.bBuildSimpleCollision = false;
		BuildParams.bFastBuild = true;
		BuildParams.bAllowCpuAccess = true;
		BuildParams.bCommitMeshDescription = false;
		WowPrepareRuntimeMeshMaterials(CollisionMesh, const_cast<FMeshDescription&>(*MeshDescriptions[0]));
		CollisionMesh->BuildFromMeshDescriptions(MeshDescriptions, BuildParams);
		CollisionMesh->NeverStream = true;
		if (!CollisionMesh->GetBodySetup())
		{
			CollisionMesh->CreateBodySetup();
		}
		if (UBodySetup* BodySetup = CollisionMesh->GetBodySetup())
		{
			BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
			BodySetup->bMeshCollideAll = true;
			BodySetup->bDoubleSidedGeometry = true;
			BodySetup->CreatePhysicsMeshes();
		}
		CollisionMesh->CalculateExtendedBounds();
		CollisionMeshCache.Add(CacheKey, CollisionMesh);

		UE_LOG(LogWowDoodad, Log,
			TEXT("Built authored doodad collision for %s (%d vertices, %d triangles)"),
			*M2Path, LocalPositions.Num(), CreatedTriangleCount);
		return CollisionMesh;
	}

	bool ShouldUsePlayerBlockingCollision(const FString& M2Path)
	{
		FString Path = M2Path.ToLower();
		Path.ReplaceInline(TEXT("/"), TEXT("\\"));

		// Soft detail meshes are visual dressing in WoW and should not become
		// invisible walls just because their filename contains a broad keyword.
		if (IsSoftDetailDoodadPath(Path) && !IsTreeTrunkDoodadPath(Path))
		{
			return false;
		}
		if (IsPureVisualEffectDoodadPath(Path))
		{
			return false;
		}
		if (IsTreeLikeDoodadPath(Path))
		{
			return IsTreeTrunkDoodadPath(Path);
		}

		return Path.Contains(TEXT("fallen")) ||
			Path.Contains(TEXT("log")) ||
			Path.Contains(TEXT("rock")) ||
			Path.Contains(TEXT("boulder")) ||
			Path.Contains(TEXT("crate")) ||
			Path.Contains(TEXT("barrel")) ||
			Path.Contains(TEXT("wagon")) ||
			Path.Contains(TEXT("cart")) ||
			Path.Contains(TEXT("fence")) ||
			Path.Contains(TEXT("post")) ||
			Path.Contains(TEXT("bench")) ||
			Path.Contains(TEXT("chair")) ||
			Path.Contains(TEXT("table")) ||
			Path.Contains(TEXT("shelf")) ||
			Path.Contains(TEXT("bookcase")) ||
			Path.Contains(TEXT("cupboard")) ||
			Path.Contains(TEXT("cabinet")) ||
			Path.Contains(TEXT("chest")) ||
			Path.Contains(TEXT("bed")) ||
			Path.Contains(TEXT("anvil")) ||
			Path.Contains(TEXT("forge")) ||
			Path.Contains(TEXT("weaponrack")) ||
			Path.Contains(TEXT("armorstand")) ||
			Path.Contains(TEXT("well")) ||
			Path.Contains(TEXT("tent")) ||
			Path.Contains(TEXT("dummy")) ||
			Path.Contains(TEXT("tombstone")) ||
			Path.Contains(TEXT("gravestone")) ||
			Path.Contains(TEXT("lamp")) ||
			Path.Contains(TEXT("lantern")) ||
			Path.Contains(TEXT("fixture")) ||
			Path.Contains(TEXT("sconce")) ||
			Path.Contains(TEXT("lanternmount")) ||
			Path.Contains(TEXT("torch")) ||
			Path.Contains(TEXT("brazier")) ||
			Path.Contains(TEXT("pyre")) ||
			Path.Contains(TEXT("hearth")) ||
			Path.Contains(TEXT("fireplace")) ||
			Path.Contains(TEXT("candle")) ||
			Path.Contains(TEXT("sconce")) ||
			Path.Contains(TEXT("pillar")) ||
			Path.Contains(TEXT("column")) ||
			Path.Contains(TEXT("statue")) ||
			Path.Contains(TEXT("sign")) ||
			Path.Contains(TEXT("mailbox")) ||
			Path.Contains(TEXT("bridge")) ||
			Path.Contains(TEXT("gate")) ||
			Path.Contains(TEXT("door")) ||
			Path.Contains(TEXT("collisionwall"));
	}

	void ConfigureDoodadCollision(UPrimitiveComponent* Component, bool bBlocksPlayer)
	{
		if (!Component)
		{
			return;
		}

		if (!bBlocksPlayer)
		{
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			return;
		}

		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionObjectType(ECC_WorldStatic);
		Component->SetCollisionResponseToAllChannels(ECR_Block);
		Component->CanCharacterStepUpOn = ECB_No;
	}

	void ConfigureSimpleBlockerCollision(UPrimitiveComponent* Component, bool bBlockCamera, bool bBlockVisibility = true)
	{
		if (!Component)
		{
			return;
		}

		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionObjectType(ECC_WorldStatic);
		Component->SetCollisionResponseToAllChannels(ECR_Ignore);
		Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Component->SetCollisionResponseToChannel(ECC_Visibility, bBlockVisibility ? ECR_Block : ECR_Ignore);
		Component->SetCollisionResponseToChannel(ECC_Camera, bBlockCamera ? ECR_Block : ECR_Ignore);
		Component->CanCharacterStepUpOn = ECB_No;
		Component->SetGenerateOverlapEvents(false);
	}

	bool CreateDoodadBlocker(AActor* ParentActor, UStaticMesh* StaticMesh, const FM2Data* ParsedM2,
		const FString& M2Path, const FTransform& WorldTransform, uint32 UniqueId, int32 InstanceIndex)
	{
        // Authored collision is the authority, not a filename allowlist.
        // This includes docks, boats and planters through every placement path.
        const bool bHasAuthoredCollision=ParsedM2 && ParsedM2->CollisionVertices.Num()>=3 && ParsedM2->CollisionIndices.Num()>=3;
		if (!ParentActor || !StaticMesh ||
            (!bHasAuthoredCollision && !ShouldUsePlayerBlockingCollision(M2Path)) ||
			IsWaterfallDoodadPath(M2Path) ||
			IsLegacyWaterEffectCardPath(M2Path))
		{
			return false;
		}

		UBoxComponent* FixtureBlocker = nullptr;
		const FBoxSphereBounds Bounds = StaticMesh->GetBounds();
		const FVector AbsScale = WorldTransform.GetScale3D().GetAbs();
		const FVector WorldCenter = WorldTransform.TransformPosition(Bounds.Origin);

		// CharacterMovement sweeps against small, concave complex meshes can pass
		// through freestanding fixtures, particularly while jumping.  Fixtures are
		// intentionally solid in WoW, so use the M2 collision mesh's own local
		// bounds as a closed primitive.  Unlike the old render-bounds fallback this
		// excludes flame cards and stays anchored to the authored collision volume.
		if (IsSolidFixtureDoodadPath(M2Path) && ParsedM2 &&
			ParsedM2->CollisionVertices.Num() >= 3)
		{
			FBox LocalCollisionBounds(ForceInit);
			for (const FVector& CollisionVertex : ParsedM2->CollisionVertices)
			{
				LocalCollisionBounds += M2PointToUELocal(CollisionVertex);
			}
			if (LocalCollisionBounds.IsValid)
			{
				const FVector FixtureExtent = LocalCollisionBounds.GetExtent() * AbsScale;
				if (FixtureExtent.GetMin() > 1.0f && FixtureExtent.GetMax() < 6000.0f)
				{
					const FName BoxName = *FString::Printf(
						TEXT("FixtureBlocker_%u_%d"), UniqueId, InstanceIndex);
					UBoxComponent* Box = NewObject<UBoxComponent>(ParentActor, BoxName);
					if (Box)
					{
						Box->SetupAttachment(ParentActor->GetRootComponent());
						ParentActor->AddInstanceComponent(Box);
						Box->InitBoxExtent(FixtureExtent);
						ConfigureSimpleBlockerCollision(Box, true, false);
						Box->RegisterComponent();
						Box->SetWorldLocationAndRotation(
							WorldTransform.TransformPosition(LocalCollisionBounds.GetCenter()),
							WorldTransform.GetRotation());
						Box->RecreatePhysicsState();
                        FixtureBlocker = Box;
					}
				}
			}
		}

		// Prefer the collision triangles authored in the M2 itself for every solid
		// doodad.  This gives lantern fixtures, furniture, rocks and tree trunks
		// their real WoW collision surface instead of a loose render-bounds box.
		// The same collision mesh is cached per model and only its lightweight
		// component/transform is repeated for each placement.
		if (UStaticMesh* AuthoredCollisionMesh =
			GetOrCreateAuthoredDoodadCollisionMesh(M2Path, ParsedM2, ParentActor))
		{
			const FName CollisionName = *FString::Printf(
				TEXT("DoodadAuthoredCollision_%u_%d"), UniqueId, InstanceIndex);
			UStaticMeshComponent* CollisionComponent =
				NewObject<UStaticMeshComponent>(ParentActor, CollisionName);
			if (CollisionComponent)
			{
				CollisionComponent->SetupAttachment(ParentActor->GetRootComponent());
				ParentActor->AddInstanceComponent(CollisionComponent);
				CollisionComponent->SetStaticMesh(AuthoredCollisionMesh);
				CollisionComponent->SetCastShadow(false);
				CollisionComponent->SetVisibility(false, true);
				CollisionComponent->SetHiddenInGame(true, true);
				ConfigureSimpleBlockerCollision(CollisionComponent, true, false);
                if (FixtureBlocker)
                {
                    // Retain the closed box for movement; autofocus sees authored triangles.
                    FixtureBlocker->ComponentTags.AddUnique(TEXT("WowNoCameraFocus"));
                    CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
                }
                if (IsTreeTrunkDoodadPath(M2Path))
                    CollisionComponent->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.0f));
                else if (!FixtureBlocker && AuthoredCollisionAllowsStepUp(ParsedM2, AbsScale))
                    CollisionComponent->CanCharacterStepUpOn = ECB_Yes;
				CollisionComponent->RegisterComponent();
				CollisionComponent->SetWorldTransform(WorldTransform);
				CollisionComponent->RecreatePhysicsState();
				return true;
			}
		}

        if (FixtureBlocker) return true;
		if (!IsTreeTrunkDoodadPath(M2Path))
		{
			const FVector WorldExtent(
				FMath::Abs(Bounds.BoxExtent.X * AbsScale.X),
				FMath::Abs(Bounds.BoxExtent.Y * AbsScale.Y),
				FMath::Abs(Bounds.BoxExtent.Z * AbsScale.Z));
			if (WorldExtent.GetMax() < 18.0f || WorldExtent.GetMax() > 6000.0f)
			{
				return false;
			}
			// r492 (Stefan, Valley of Trials: he and the NPCs around him walked ~40 cm above the
			// ground, and could not step back onto it after walking off): a cluster of flat rock slabs
			// without authored collision got one box over its whole render bounds, ~9 m wide and
			// knee high, so everyone stood on an invisible table over the gaps. In WoW such models
			// have no collision at all; low, wide ones (under ~1 m and over three times wider than tall) now
			// leave the ground to the terrain. Compact objects (crates, barrels) keep their box.
			if (WorldExtent.Z < 50.0f && FMath::Max(WorldExtent.X, WorldExtent.Y) > WorldExtent.Z * 3.0f)
			{
				// r518 (Stefan, Valley of Trials: since r492 the feet sink through the slabs; "find the middle
				// ground"): such flat models now get collision from their own visible triangles - you stand on
				// each slab's top, and the gaps between slabs stay open ground. Step-up is allowed (they are
				// at most knee high), so walking onto and off them is a normal step.
				if (ParsedM2 && ParsedM2->Vertices.Num() >= 3 && ParsedM2->Indices.Num() >= 3)
				{
					FM2Data Surface;
					Surface.CollisionVertices.Reserve(ParsedM2->Vertices.Num());
					for (const FM2Vertex& Vertex : ParsedM2->Vertices) Surface.CollisionVertices.Add(Vertex.Position);
					Surface.CollisionIndices = ParsedM2->Indices;
					if (UStaticMesh* SurfaceMesh = GetOrCreateAuthoredDoodadCollisionMesh(M2Path + TEXT("|surface"), &Surface, ParentActor))
					{
						const FName SurfaceName = *FString::Printf(TEXT("DoodadSurfaceCollision_%u_%d"), UniqueId, InstanceIndex);
						if (UStaticMeshComponent* SurfaceComponent = NewObject<UStaticMeshComponent>(ParentActor, SurfaceName))
						{
							SurfaceComponent->SetupAttachment(ParentActor->GetRootComponent());
							ParentActor->AddInstanceComponent(SurfaceComponent);
							SurfaceComponent->SetStaticMesh(SurfaceMesh);
							SurfaceComponent->SetCastShadow(false);
							SurfaceComponent->SetVisibility(false, true);
							SurfaceComponent->SetHiddenInGame(true, true);
							ConfigureSimpleBlockerCollision(SurfaceComponent, true, false);
							SurfaceComponent->CanCharacterStepUpOn = ECB_Yes;
							SurfaceComponent->RegisterComponent();
							SurfaceComponent->SetWorldTransform(WorldTransform);
							SurfaceComponent->RecreatePhysicsState();
							return true;
						}
					}
				}
				return false;
			}

			const FVector BoxExtent(
				FMath::Clamp(WorldExtent.X, 20.0f, 3200.0f),
				FMath::Clamp(WorldExtent.Y, 20.0f, 3200.0f),
				FMath::Clamp(WorldExtent.Z, 24.0f, 2200.0f));
			const FName BoxName = *FString::Printf(TEXT("DoodadBlocker_%u_%d"), UniqueId, InstanceIndex);
			UBoxComponent* Box = NewObject<UBoxComponent>(ParentActor, BoxName);
			if (!Box)
			{
				return false;
			}

			Box->SetupAttachment(ParentActor->GetRootComponent());
			ParentActor->AddInstanceComponent(Box);
			Box->InitBoxExtent(BoxExtent);
			ConfigureSimpleBlockerCollision(Box, true);
			Box->RegisterComponent();
			Box->SetWorldLocationAndRotation(WorldCenter, WorldTransform.GetRotation());
			Box->RecreatePhysicsState();
			return true;
		}

		const float HorizontalExtentX = FMath::Abs(Bounds.BoxExtent.X * AbsScale.X);
		const float HorizontalExtentY = FMath::Abs(Bounds.BoxExtent.Y * AbsScale.Y);
		const float HorizontalMin = FMath::Min(HorizontalExtentX, HorizontalExtentY);
		const float HorizontalMax = FMath::Max(HorizontalExtentX, HorizontalExtentY);
		const float VerticalExtent = FMath::Abs(Bounds.BoxExtent.Z * AbsScale.Z);
		const bool bLargeTree = HorizontalMax > 2400.0f || VerticalExtent > 3600.0f;
		FString NormalizedTreePath = M2Path.ToLower();
		NormalizedTreePath.ReplaceInline(TEXT("/"), TEXT("\\"));
		const FVector TreePivot = WorldTransform.GetLocation();
		const bool bElwynnBroadTree =
			NormalizedTreePath.Contains(TEXT("canopylesstree")) ||
			NormalizedTreePath.Contains(TEXT("\\bigtree")) ||
			NormalizedTreePath.Contains(TEXT("\\hugetree"));
		const bool bTreeStump =
			NormalizedTreePath.Contains(TEXT("\\stumps\\")) ||
			NormalizedTreePath.Contains(TEXT("stump"));
		const bool bBroadTrunkTree =
			(bElwynnBroadTree && !bTreeStump) ||
			NormalizedTreePath.Contains(TEXT("bigtree")) ||
			NormalizedTreePath.Contains(TEXT("hugetree")) ||
			NormalizedTreePath.Contains(TEXT("gianttree")) ||
			NormalizedTreePath.Contains(TEXT("largetree")) ||
			NormalizedTreePath.Contains(TEXT("ancienttree"));
		const float RadiusFromExtents = FMath::Max(
			HorizontalMin * (bBroadTrunkTree ? 0.74f : (bTreeStump ? 0.48f : 0.34f)),
			HorizontalMax * (bLargeTree ? 0.16f : 0.10f));
		float TrunkRadius = FMath::Clamp(
			RadiusFromExtents,
			bBroadTrunkTree ? 620.0f : (bTreeStump ? 180.0f : (bLargeTree ? 260.0f : 120.0f)),
			bBroadTrunkTree ? 1450.0f : (bTreeStump ? 460.0f : (bLargeTree ? 950.0f : 520.0f)));
		float HalfHeight = FMath::Clamp(
			VerticalExtent * (bTreeStump ? 0.28f : (bLargeTree ? 0.42f : 0.50f)),
			bBroadTrunkTree ? 620.0f : (bTreeStump ? 160.0f : 260.0f),
			3600.0f);
		float TreeBaseOffset = 0.0f;
		float TrunkHalfX = TrunkRadius;
		float TrunkHalfY = TrunkRadius;
		bool bHasAxisProfile = false;

		// The static-mesh bounds include the entire canopy, which made the old
		// filename heuristic turn small Elwynn trees into six-metre invisible
		// walls.  M2 vertices give us the actual trunk footprint: sample only the
		// lower quarter of the model and use a percentile so a few low branches do
		// not make the blocker wider than the visible trunk.
		if (ParsedM2 && ParsedM2->Vertices.Num() >= 8)
		{
			float LocalMinZ = TNumericLimits<float>::Max();
			float LocalMaxZ = TNumericLimits<float>::Lowest();
			for (const FM2Vertex& Vertex : ParsedM2->Vertices)
			{
				const FVector Local = M2VertexToUELocal(Vertex);
				LocalMinZ = FMath::Min(LocalMinZ, Local.Z);
				LocalMaxZ = FMath::Max(LocalMaxZ, Local.Z);
			}
			const float LocalHeight = LocalMaxZ - LocalMinZ;
			if (FMath::IsFinite(LocalHeight) && LocalHeight > 20.0f)
			{
				TArray<float> BaseRadii;
				TArray<float> BaseAbsX;
				TArray<float> BaseAbsY;
				const float SampleTop = LocalMinZ + LocalHeight * 0.28f;
				for (const FM2Vertex& Vertex : ParsedM2->Vertices)
				{
					const FVector Local = M2VertexToUELocal(Vertex);
					if (Local.Z <= SampleTop)
					{
						const float Radius = FVector2D(Local.X, Local.Y).Size();
						if (FMath::IsFinite(Radius))
						{
							BaseRadii.Add(Radius);
							BaseAbsX.Add(FMath::Abs(Local.X));
							BaseAbsY.Add(FMath::Abs(Local.Y));
						}
					}
				}
				if (BaseRadii.Num() >= 4)
				{
					BaseRadii.Sort();
					BaseAbsX.Sort();
					BaseAbsY.Sort();
					const int32 QuantileIndex = FMath::Clamp(
						FMath::FloorToInt(static_cast<float>(BaseRadii.Num() - 1) * 0.82f),
						0, BaseRadii.Num() - 1);
					const float LocalTrunkRadius = BaseRadii[QuantileIndex];
					const float UniformScale = FMath::Max(AbsScale.GetMin(), 0.001f);
					const float GeometryRadius = LocalTrunkRadius * UniformScale *
						(bBroadTrunkTree ? 1.25f : 1.08f);
					const float GeometryHalfHeight = LocalHeight * UniformScale * 0.50f;
					if (FMath::IsFinite(GeometryRadius) && FMath::IsFinite(GeometryHalfHeight) &&
						GeometryRadius > 20.0f && GeometryHalfHeight > 80.0f)
					{
						const float GeometryMinRadius = bTreeStump ? 120.0f
							: (bBroadTrunkTree ? 420.0f : 55.0f);
						const float GeometryMaxRadius = bTreeStump ? 460.0f
							: (bBroadTrunkTree ? 1100.0f : (bLargeTree ? 950.0f : 700.0f));
						TrunkRadius = FMath::Clamp(GeometryRadius,
							GeometryMinRadius, GeometryMaxRadius);
						// Preserve the two independent trunk axes for broad trees. A
						// circular capsule necessarily blocks too far away on the narrow
						// sides of an oval trunk, exactly as seen on the large Elwynn
						// trees. The same robust quantile rejects root/leaf-card outliers.
						const float ProfilePadding = bBroadTrunkTree ? 1.08f : 1.04f;
						TrunkHalfX = FMath::Clamp(
							BaseAbsX[QuantileIndex] * FMath::Max(AbsScale.X, 0.001f) * ProfilePadding,
							bBroadTrunkTree ? 300.0f : 55.0f, GeometryMaxRadius);
						TrunkHalfY = FMath::Clamp(
							BaseAbsY[QuantileIndex] * FMath::Max(AbsScale.Y, 0.001f) * ProfilePadding,
							bBroadTrunkTree ? 300.0f : 55.0f, GeometryMaxRadius);
						bHasAxisProfile = FMath::IsFinite(TrunkHalfX) &&
							FMath::IsFinite(TrunkHalfY);
						HalfHeight = FMath::Clamp(GeometryHalfHeight,
							bTreeStump ? 100.0f : 180.0f, 3600.0f);
						TreeBaseOffset = LocalMinZ * UniformScale;
						UE_LOG(LogWowDoodad, Verbose,
							TEXT("Tree geometry profile: path=%s verts=%d localHeight=%.1f radius=%.1f halfHeight=%.1f baseOffset=%.1f"),
							*M2Path, ParsedM2->Vertices.Num(), LocalHeight, TrunkRadius, HalfHeight, TreeBaseOffset);
					}
				}
			}
		}
		// Use the placement pivot for XY. The visual bounds include broad canopy and
		// root cards, so bounds-centered collision can block empty space beside the trunk.
		FVector CapsuleCenter(TreePivot.X, TreePivot.Y, TreePivot.Z + TreeBaseOffset + HalfHeight);

		// Broad/mature trunks are visibly non-circular. Use their model-space
		// X/Y profile and authored rotation, while keeping the narrow blocker all
		// the way up the trunk. Leaves remain non-colliding, so dense forests do
		// not turn into a ceiling for the camera.
		if (bHasAxisProfile && (bBroadTrunkTree || bLargeTree))
		{
			const FName BoxName = *FString::Printf(TEXT("TreeTrunkBlocker_%u_%d"), UniqueId, InstanceIndex);
			UBoxComponent* Box = NewObject<UBoxComponent>(ParentActor, BoxName);
			if (!Box)
			{
				return false;
			}

			Box->SetupAttachment(ParentActor->GetRootComponent());
			ParentActor->AddInstanceComponent(Box);
			Box->InitBoxExtent(FVector(TrunkHalfX, TrunkHalfY, HalfHeight));
            Box->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.0f));
			ConfigureSimpleBlockerCollision(Box, true, false);
			Box->RegisterComponent();
			Box->SetWorldLocationAndRotation(CapsuleCenter, WorldTransform.GetRotation());
			Box->RecreatePhysicsState();
			if (IsDoodadCollisionDebugEnabled())
			{
				UE_LOG(LogWowDoodad, Warning,
					TEXT("Tree box blocker: path=%s uid=%u center=%s extent=(%.1f,%.1f,%.1f)"),
					*M2Path, UniqueId, *CapsuleCenter.ToCompactString(),
					TrunkHalfX, TrunkHalfY, HalfHeight);
			}
			return true;
		}

		const FName CapsuleName = *FString::Printf(TEXT("TreeTrunkBlocker_%u_%d"), UniqueId, InstanceIndex);
		UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(ParentActor, CapsuleName);
		if (!Capsule)
		{
			return false;
		}

		Capsule->SetupAttachment(ParentActor->GetRootComponent());
		ParentActor->AddInstanceComponent(Capsule);
		Capsule->InitCapsuleSize(TrunkRadius, HalfHeight);
        Capsule->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.0f));
		// The compact trunk shape blocks both pawn and camera. Visibility traces
		// remain free so leaf/root cards do not interfere with selection.
		ConfigureSimpleBlockerCollision(Capsule, true, false);
		Capsule->RegisterComponent();
		Capsule->SetWorldLocation(CapsuleCenter);
		Capsule->RecreatePhysicsState();
		if (IsDoodadCollisionDebugEnabled())
		{
			UE_LOG(LogWowDoodad, Warning,
				TEXT("Tree blocker: path=%s uid=%u center=%s radius=%.1f halfHeight=%.1f"),
				*M2Path,
				UniqueId,
				*CapsuleCenter.ToCompactString(),
				TrunkRadius,
				HalfHeight);
		}
		return true;
	}


}

FString FWowDoodadManager::GetSkinPath(const FString& M2Path)
{
    // WoW 3.3.5 skin file: replace .m2 or .M2 with 00.skin
    FString SkinPath = NormalizeM2AssetPath(M2Path);
    if (SkinPath.EndsWith(TEXT(".m2"), ESearchCase::IgnoreCase))
    {
        SkinPath = SkinPath.Left(SkinPath.Len() - 3);
    }
    SkinPath += TEXT("00.skin");
    return SkinPath;
}

bool FWowDoodadManager::AddAuthoredCollision(AActor* Owner, UStaticMesh* Mesh, const FM2Data* Data,
    const FString& Path, const FTransform& Transform, uint32 UniqueId, int32 Index)
{
    if (!Data || Data->CollisionVertices.Num()<3 || Data->CollisionIndices.Num()<3) return false;
    return CreateDoodadBlocker(Owner,Mesh,Data,Path,Transform,UniqueId,Index);
}

TSharedPtr<FM2Data> FWowDoodadManager::GetOrParseM2(const FString& M2Path, FMpqManager* Mpq)
{
    if (!Mpq) return nullptr;

    const FString AssetPath = NormalizeM2AssetPath(M2Path);
    if (AssetPath.IsEmpty()) return nullptr;

    FString NormalizedPath = AssetPath.ToLower();
    NormalizedPath.ReplaceInline(TEXT("/"), TEXT("\\"));

    {
        FScopeLock Lock(&CacheLock);
        TSharedPtr<FM2Data>* Found = ParsedM2Cache.Find(NormalizedPath);
        if (Found)
        {
            return *Found;
        }
    }

    // Read M2 file
    TArray<uint8> M2Raw;
    if (!Mpq->ReadFile(AssetPath, M2Raw))
    {
        UE_LOG(LogWowDoodad, Verbose, TEXT("Failed to read M2: %s"), *AssetPath);
        return nullptr;
    }

    // Read skin file
    FString SkinPath = GetSkinPath(AssetPath);
    TArray<uint8> SkinRaw;
    if (!Mpq->ReadFile(SkinPath, SkinRaw))
    {
        UE_LOG(LogWowDoodad, Verbose, TEXT("Failed to read skin: %s"), *SkinPath);
        return nullptr;
    }

    // Parse
    TSharedPtr<FM2Data> Data = MakeShared<FM2Data>(FM2Parser::Parse(M2Raw, SkinRaw));
    if (!Data->bIsValid)
    {
        UE_LOG(LogWowDoodad, Verbose, TEXT("Failed to parse M2: %s"), *AssetPath);
        return nullptr;
    }

    {
        FScopeLock Lock(&CacheLock);
        ParsedM2Cache.Add(NormalizedPath, Data);
    }

    UE_LOG(LogWowDoodad, Verbose, TEXT("Parsed M2: %s (%d verts, %d indices, %d textures)"),
        *AssetPath, Data->Vertices.Num(), Data->Indices.Num(), Data->TexturePaths.Num());

    return Data;
}

AActor* FWowDoodadManager::SpawnWorldM2(UWorld* World, const FString& M2Path,
	const FVector& Location, const FRotator& Rotation, float Scale,
	FMpqManager* Mpq, FWowAssetCache* Cache, uint32 UniqueId, bool bAnimateSpell, bool bLoopSpell)
{
	const FString AssetPath = NormalizeM2AssetPath(M2Path);
	if (!World || !Mpq || !Cache || AssetPath.IsEmpty())
	{
		return nullptr;
	}

	const TSharedPtr<FM2Data> ParsedM2 = GetOrParseM2(AssetPath, Mpq);
	UStaticMesh* StaticMesh = GetOrCreateStaticMesh(AssetPath, Mpq, Cache);
	if (!ParsedM2.IsValid() || !ParsedM2->bIsValid ||
		(!StaticMesh && ParsedM2->ParticleEmitters.IsEmpty() && ParsedM2->RibbonEmitters.IsEmpty()))
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, Rotation, SpawnParams);
	if (!Actor)
	{
		return nullptr;
	}

	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("WorldM2Root"));
	Actor->SetRootComponent(Root);
	Actor->AddInstanceComponent(Root);
	Root->RegisterComponent();

	// A bare AActor has no root during SpawnActor, so its supplied transform
	// must be applied after installing the root (also for particle-only M2s).
	Root->SetWorldLocationAndRotation(Location, Rotation);

	UStaticMeshComponent* MeshComponent = nullptr;
	if (StaticMesh)
	{
		MeshComponent = NewObject<UStaticMeshComponent>(Actor, TEXT("WorldM2Mesh"));
		if (!MeshComponent)
		{
			Actor->Destroy();
			return nullptr;
		}

		MeshComponent->SetupAttachment(Root);
		Actor->AddInstanceComponent(MeshComponent);
		MeshComponent->SetStaticMesh(StaticMesh);
		MeshComponent->SetRelativeScale3D(FVector(FMath::Max(Scale, 0.001f)));
		MeshComponent->SetCastShadow(true);
		ConfigureDoodadCollision(MeshComponent, false);
		MeshComponent->RegisterComponent();
		CreateDoodadBlocker(Actor, StaticMesh, ParsedM2.Get(), AssetPath,
			FTransform(Rotation, Location, FVector(FMath::Max(Scale, 0.001f))), UniqueId, 0);
	}

    if (bAnimateSpell && StaticMesh && ParsedM2->HasAnimationData())
    {
        UWowSpellMeshComponent* Animated = NewObject<UWowSpellMeshComponent>(Actor, TEXT("SpellAnimatedMesh"));
        Animated->SetupAttachment(Root);
        Actor->AddInstanceComponent(Animated);
        Animated->SetRelativeScale3D(FVector(FMath::Max(Scale, 0.001f)));
        Animated->RegisterComponent();
        // WoW lightning (Lightning Bolt hands, Chain Lightning, shocks) is blue shading
        // to violet/pink; its glow textures are white, so tint the glowing passes.
        if (AssetPath.Contains(TEXT("lightning"), ESearchCase::IgnoreCase))
        {
            Animated->GlowTint = FMath::Lerp(FLinearColor(0.22f, 0.40f, 1.0f), FLinearColor(0.62f, 0.30f, 1.0f), FMath::FRand() * 0.6f);
            // Persistent lightning (Lightning Shield's orb) lights its surroundings.
            Animated->bGlowLight = bLoopSpell;
            // The blue tint dims the white glow ball; give the orb its body back.
            if (bLoopSpell) Animated->GlowTint *= 1.5f;
        }
        // r409 (Stefan: the level-up "light" was missing, only the sparks showed): the golden pillar
        // and rings of Spells\LevelUp\LevelUp.m2 are additive passes with a dim texture; at emissive 1
        // they vanished in the HDR scene. Give them the brightness of the original and let the pillar
        // light the character and the ground while it shines.
        if (AssetPath.Contains(TEXT("levelup"), ESearchCase::IgnoreCase))
        {
            Animated->GlowStrength = 4.5f;
            Animated->bGlowLight = true;
            Animated->GlowLightColor = FLinearColor(1.0f, 0.78f, 0.32f);
            Animated->GlowLightIntensity = 9000.f;
            Animated->GlowLightRadius = 800.f;
        }
        if (Animated->Initialize(ParsedM2, StaticMesh, bLoopSpell))
            MeshComponent->SetVisibility(false);
        else
            Animated->DestroyComponent();
    }

    // r374 (Stefan: the chest lid never opened): game objects with Open (148) and Opened (149)
    // sequences - chests, footlockers - are drawn by the CPU-animated mesh so the lid can move.
    // They stand in Stand (closed); the gameplay controller plays Open/Close on loot.
    if (!bAnimateSpell && StaticMesh && MeshComponent
        && UWowSpellMeshComponent::FindSequenceById(*ParsedM2, 148) != INDEX_NONE
        && UWowSpellMeshComponent::FindSequenceById(*ParsedM2, 149) != INDEX_NONE)
    {
        UWowSpellMeshComponent* Animated = NewObject<UWowSpellMeshComponent>(Actor, TEXT("GameObjectAnimatedMesh"));
        Animated->SetupAttachment(Root);
        Actor->AddInstanceComponent(Animated);
        Animated->SetRelativeScale3D(FVector(FMath::Max(Scale, 0.001f)));
        Animated->RegisterComponent();
        if (Animated->Initialize(ParsedM2, StaticMesh, true))
        {
            // r414 (Stefan: the chests spun while idle): TreasureChest01's Stand (0) and Closed (147)
            // carry no keys, so neither was found and the fallback picked a clip by the wrong index
            // (an emote that turns the whole chest). Without them the chest rests in its bind pose.
            if (!Animated->PlaySequenceById(0, true) && !Animated->PlaySequenceById(147, true)) Animated->HoldBindPose();
            Animated->SetCastShadow(true);
            MeshComponent->SetVisibility(false); // its collision stays
            Actor->Tags.Add(TEXT("WowOpenableGameObject"));
        }
        else Animated->DestroyComponent();
    }

    FWowLampFixtures::AttachM2(Actor,MeshComponent,*ParsedM2,AssetPath,MeshComponent?MeshComponent->GetComponentTransform():Root->GetComponentTransform());
	CreateDoodadPointLight(Actor, MeshComponent ? static_cast<USceneComponent*>(MeshComponent) : Root,
		AssetPath, Location, UniqueId, 0);
	if ((ParsedM2->ParticleEmitters.Num() > 0 || ParsedM2->RibbonEmitters.Num() > 0) &&
		!IsWaterfallDoodadPath(AssetPath) && !IsLegacyWaterEffectCardPath(AssetPath))
	{
		UWowM2ParticleComponent* ParticleComponent =
			NewObject<UWowM2ParticleComponent>(Actor, TEXT("WorldM2Particles"));
		if (ParticleComponent)
		{
			ParticleComponent->SetupAttachment(Root);
			Actor->AddInstanceComponent(ParticleComponent);
			ParticleComponent->SetRelativeScale3D(FVector(FMath::Max(Scale, 0.001f)));
			ParticleComponent->RegisterComponent();
			ParticleComponent->SetFixturePath(AssetPath);
            ParticleComponent->bForceLoopAnimation = bLoopSpell; // persistent aura: keep orbiting
            ParticleComponent->bPlayOnce = bAnimateSpell && !bLoopSpell; // impacts strike once
            // Only real lightning spells get the electric spark treatment (blue/violet crackles).
            // Holy and frost effects also have bluish additive sparks (Seal of Righteousness).
            for (const TCHAR* Word : { TEXT("lightning"), TEXT("shock"), TEXT("thunder"), TEXT("storm"), TEXT("zap"), TEXT("static") })
                if (AssetPath.Contains(Word, ESearchCase::IgnoreCase)) ParticleComponent->bElectricSpell = true;
            ParticleComponent->InitializeFromParsedM2Data(ParsedM2.Get(), Mpq, Cache);
		}
	}

	// One play of sequence 0, so callers can retire one-shot spell visuals when it ends.
	if (!ParsedM2->Animations.IsEmpty())
		Actor->Tags.Add(*FString::Printf(TEXT("WowM2Seq0Ms=%u"), ParsedM2->Animations[0].Length));
	UE_LOG(LogWowDoodad, Log,
		TEXT("Spawned standalone world M2 %s at %s (scale=%.3f, emitters=%d)"),
		*AssetPath, *Location.ToCompactString(), Scale, ParsedM2->ParticleEmitters.Num());
	return Actor;
}

void FWowDoodadManager::SpawnDoodads(AActor* ParentActor, const TArray<FAdtDoodadPlacement>& Placements,
                                      const TArray<FString>& DoodadPaths, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    if (!ParentActor || !Mpq || Placements.Num() == 0)
    {
        return;
    }

    UE_LOG(LogWowDoodad, Log, TEXT("Spawning %d doodads"), Placements.Num());

    // Track unique IDs to avoid spawning duplicates (shared across tiles)
    TSet<uint32> SpawnedIds;
    int32 Spawned = 0;

    for (int32 i = 0; i < Placements.Num(); ++i)
    {
        const FAdtDoodadPlacement& Placement = Placements[i];

        // Skip duplicates
        if (SpawnedIds.Contains(Placement.UniqueId))
        {
            continue;
        }
        SpawnedIds.Add(Placement.UniqueId);

        // Resolve path
        if (Placement.NameIndex < 0 || Placement.NameIndex >= DoodadPaths.Num())
        {
            continue;
        }

        const FString M2Path = NormalizeM2AssetPath(DoodadPaths[Placement.NameIndex]);
        if (M2Path.IsEmpty())
        {
            continue;
        }

        // Check if this model should use skeletal mesh (animated)
        TSharedPtr<FM2Data> M2Parsed = GetOrParseM2(M2Path, Mpq);
        if (!M2Parsed || !M2Parsed->bIsValid)
        {
            continue;
        }

        // MDDF positions: convert to ADT space, then AdtToUE (matches terrain)
        float AdtX = Placement.Position.X;
        float AdtY = Placement.Position.Y;
        float AdtZ = Placement.Position.Z;
        FVector UEPos = FWowCoordinate::AdtToUE(AdtX, AdtY, AdtZ);
        float ScaleVal = Placement.GetScaleFloat();
        FRotator Rot = FWowCoordinate::WowRotationToUE(
            Placement.Rotation.X, Placement.Rotation.Y, Placement.Rotation.Z);
		// Every blocking ADT placement receives either its authored M2 collision
		// component or one compact fallback shape below.  Do not also enable the
		// visual ISM's render-mesh collision: that duplicates contacts and can let
		// foliage/effect triangles override the tighter authored surface.
		const bool bMeshBlocksPlayer = false;

        if (ShouldUseSkeletalMesh(*M2Parsed))
        {
            // Animated M2 — use skeletal mesh with animations
            USkeletalMesh* SkelMesh = GetOrCreateSkeletalMesh(M2Path, Mpq, Cache);
            if (!SkelMesh) continue;

            FName CompName = *FString::Printf(TEXT("SkelDoodad_%d_%d"), Placement.UniqueId, i);
            USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(ParentActor, CompName);
            SkelComp->SetSkeletalMesh(SkelMesh);
            SkelComp->SetupAttachment(ParentActor->GetRootComponent());
            SkelComp->SetCastShadow(!IsWaterfallDoodadPath(M2Path));
			ConfigureDoodadCollision(SkelComp, bMeshBlocksPlayer);
            SkelComp->SetWorldLocation(UEPos);
            SkelComp->SetWorldRotation(Rot);
            SkelComp->SetWorldScale3D(FVector(ScaleVal));
            SkelComp->RegisterComponent();
            FWowLampFixtures::AttachM2(ParentActor,SkelComp,*M2Parsed,M2Path,SkelComp->GetComponentTransform());
			CreateDoodadPointLight(ParentActor, SkelComp, M2Path, UEPos, Placement.UniqueId, i);

            // Play first animation if available
            FString NormPath = M2Path.ToLower();
            NormPath.ReplaceInline(TEXT("/"), TEXT("\\"));
            auto* Anims = AnimCache.Find(NormPath);
            if (Anims && Anims->Num() > 0 && (*Anims)[0].IsValid())
            {
                SkelComp->PlayAnimation((*Anims)[0].Get(), true);
            }
        }
        else
        {
            // Static M2 — use static mesh as before
            UStaticMesh* SM = GetOrCreateStaticMesh(M2Path, Mpq, Cache);
            if (!SM) continue;

            FName CompName = *FString::Printf(TEXT("Doodad_%d_%d"), Placement.UniqueId, i);
            UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(ParentActor, CompName);
            MeshComp->SetStaticMesh(SM);
            MeshComp->SetupAttachment(ParentActor->GetRootComponent());
            MeshComp->SetCastShadow(!IsWaterfallDoodadPath(M2Path));
			ConfigureDoodadCollision(MeshComp, bMeshBlocksPlayer);
            MeshComp->SetWorldLocation(UEPos);
            MeshComp->SetWorldRotation(Rot);
            MeshComp->SetWorldScale3D(FVector(ScaleVal));
            MeshComp->RegisterComponent();
            FWowLampFixtures::AttachM2(ParentActor,MeshComp,*M2Parsed,M2Path,MeshComp->GetComponentTransform());
			CreateDoodadPointLight(ParentActor, MeshComp, M2Path, UEPos, Placement.UniqueId, i);
			if (bMeshBlocksPlayer)
			{
				MeshComp->RecreatePhysicsState();
			}
	CreateDoodadBlocker(ParentActor, SM, M2Parsed.Get(), M2Path,
		FTransform(Rot, UEPos, FVector(ScaleVal)), Placement.UniqueId, i);
        }

        // Add particle effects if the M2 has particle emitters
        if ((M2Parsed->ParticleEmitters.Num() > 0 || M2Parsed->RibbonEmitters.Num() > 0) &&
			!IsWaterfallDoodadPath(M2Path) &&
			!IsLegacyWaterEffectCardPath(M2Path))
        {
            FName ParticleCompName = *FString::Printf(TEXT("Particles_%d_%d"), Placement.UniqueId, i);
            UWowM2ParticleComponent* ParticleComp = NewObject<UWowM2ParticleComponent>(ParentActor, ParticleCompName);
            ParticleComp->SetupAttachment(ParentActor->GetRootComponent());
            ParticleComp->SetWorldLocation(UEPos);
            ParticleComp->SetWorldRotation(Rot);
            ParticleComp->SetWorldScale3D(FVector(ScaleVal));
            ParticleComp->RegisterComponent();

            // Initialize particles from M2 data
            ParticleComp->SetFixturePath(M2Path);
            ParticleComp->InitializeFromParsedM2Data(M2Parsed.Get(), Mpq, Cache);

            UE_LOG(LogWowDoodad, Log, TEXT("Added %d particle emitters to doodad %s"),
                M2Parsed->ParticleEmitters.Num(), *M2Path);
        }

        ++Spawned;
    }

    UE_LOG(LogWowDoodad, Log, TEXT("Spawned %d doodad meshes"), Spawned);
}

UStaticMeshComponent* FWowDoodadManager::SpawnSingleDoodad(
    AActor* ParentActor, const FAdtDoodadPlacement& Placement,
    const FString& M2Path, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    const FString AssetPath = NormalizeM2AssetPath(M2Path);
    if (!ParentActor || !Mpq || AssetPath.IsEmpty())
    {
        return nullptr;
    }

    // Get or create static mesh (cached)
    UStaticMesh* SM = GetOrCreateStaticMesh(AssetPath, Mpq, Cache);
    if (!SM)
    {
        return nullptr;
    }

    FName CompName = *FString::Printf(TEXT("Doodad_%u"), Placement.UniqueId);
    UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(ParentActor, CompName);
    MeshComp->SetStaticMesh(SM);
    MeshComp->SetupAttachment(ParentActor->GetRootComponent());
    MeshComp->SetCastShadow(!IsWaterfallDoodadPath(AssetPath));
	ConfigureDoodadCollision(MeshComp, false);

    // MDDF positions: convert to ADT space, then AdtToUE (matches terrain)
    float AdtX = Placement.Position.X;
    float AdtY = Placement.Position.Y;
    float AdtZ = Placement.Position.Z;
    FVector UEPos = FWowCoordinate::AdtToUE(AdtX, AdtY, AdtZ);

    float ScaleVal = Placement.GetScaleFloat();

    MeshComp->SetWorldLocation(UEPos);
    MeshComp->SetWorldRotation(FWowCoordinate::WowRotationToUE(
        Placement.Rotation.X, Placement.Rotation.Y, Placement.Rotation.Z));
    MeshComp->SetWorldScale3D(FVector(ScaleVal));
    MeshComp->RegisterComponent();
	CreateDoodadPointLight(ParentActor, MeshComp, AssetPath, UEPos, Placement.UniqueId, 0);
	const TSharedPtr<FM2Data> ParsedM2 = GetOrParseM2(AssetPath, Mpq);
    if (ParsedM2.IsValid()) FWowLampFixtures::AttachM2(ParentActor,MeshComp,*ParsedM2,AssetPath,MeshComp->GetComponentTransform());
	CreateDoodadBlocker(ParentActor, SM, ParsedM2.Get(), AssetPath,
		FTransform(MeshComp->GetComponentQuat(), UEPos, FVector(ScaleVal)), Placement.UniqueId, 0);

    return MeshComp;
}

UTexture2D* FWowDoodadManager::LoadBlpTexture(const FString& Path, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    if (Path.IsEmpty() || !Mpq || !Cache) return nullptr;

    UTexture2D* Tex = Cache->FindTexture(Path);
    if (Tex) return Tex;

    TArray<uint8> BlpRaw;
    if (!Mpq->ReadFile(Path, BlpRaw))
    {
        // Log failed texture reads for debugging
        static int32 sFailLog = 0;
        if (sFailLog < 10)
        {
            sFailLog++;
            UE_LOG(LogWowDoodad, Warning, TEXT("Failed to read BLP from MPQ: '%s' (exists=%d)"),
                *Path, Mpq->FileExists(Path) ? 1 : 0);
        }
        return nullptr;
    }

    FBlpTexture BlpData = FBlpParser::Parse(BlpRaw);
    if (!BlpData.bIsValid) return nullptr;

    Tex = FWowTextureFactory::CreateTexture(BlpData, Path);
    if (Tex) Cache->CacheTexture(Path, Tex);
    return Tex;
}

UStaticMesh* FWowDoodadManager::CreateStaticMeshFromM2(const FM2Data& Data, const FString& M2Path,
                                                         FMpqManager* Mpq, FWowAssetCache* Cache)
{
    if (Data.Vertices.Num() == 0 || Data.Indices.Num() == 0)
    {
        return nullptr;
    }

	if (IsLegacyWaterEffectCardPath(M2Path) && !IsWaterfallDoodadPath(M2Path))
	{
		UE_LOG(LogWowDoodad, Log, TEXT("Skipped legacy water effect card mesh: %s"), *M2Path);
		return nullptr;
	}

    UStaticMesh* StaticMesh = NewObject<UWowRuntimeStaticMesh>(Cache ? Cache->GetRuntimeContext() : GetTransientPackage());
    StaticMesh->bAllowCPUAccess = true; // Required for HISMC instancing

    FMeshDescription MeshDesc;
    FStaticMeshAttributes Attributes(MeshDesc);
    Attributes.Register();

    const int32 NumVerts = Data.Vertices.Num();
    const bool bUEWaterfall = IsWaterfallDoodadPath(M2Path);
    FBox FallBounds(ForceInit);
    if (bUEWaterfall)
        for (const FM2Vertex& Vertex : Data.Vertices) FallBounds += FVector(Vertex.Position);
    const double FallHeight = FMath::Max(FallBounds.GetSize().Z, 1.0);


    // Determine polygon groups from render passes + submeshes
    // Each render pass maps to a submesh (triangle range) and a texture
    // Group by unique (submesh, texture) pairs to create material slots
    struct FMaterialBatch
    {
        int32 StartIndex;   // Index into Data.Indices
        int32 NumIndices;   // Number of indices (NumTriangles * 3... well, nTriangles from skin is actually index count)
        int32 TextureIndex; // Index into Data.TexturePaths
        uint16 BlendMode = 0; // M2 render flag blend mode: 0/1 masked, 2 translucent, 3+ additive/modulated effects
        uint16 Flags = 0;
        // r739: coordinate source per layer (0 UV0, 1 UV1, -1 sphere env map), the second texture and the pass weight.
        int8 Coord = 0;
        int8 Coord2 = 0;
        int32 TextureIndex2 = -1;
        float Weight = 1.0f;
    };

    TArray<FMaterialBatch> Batches;
    bool bHasValidBatches = Data.RenderPasses.Num() > 0 && Data.Submeshes.Num() > 0;
    RecordLegacyGlowCards(Data, M2Path);

    if (bHasValidBatches)
    {
        for (const FM2RenderPass& Pass : Data.RenderPasses)
        {
            if (Pass.SubmeshIndex >= Data.Submeshes.Num()) continue;
            if (FWowFixturePolicy::SkipLegacyPass(Data,M2Path,Pass)) continue;

            const FM2Submesh& Sub = Data.Submeshes[Pass.SubmeshIndex];
            // StartTriangle and NumTriangles in M2 skin format are index counts, not triangle counts
            int32 Start = static_cast<int32>(Sub.StartTriangle);
            int32 Count = static_cast<int32>(Sub.NumTriangles);

            if (Start + Count > Data.Indices.Num()) continue;
            if (Count < 3) continue;

            FMaterialBatch Batch;
            Batch.StartIndex = Start;
            Batch.NumIndices = Count;
            Batch.TextureIndex = Pass.TextureIndex;
            Batch.BlendMode = Pass.BlendMode;
            Batch.Flags = Pass.Flags;
            Batch.Coord = Pass.TexCoordSource;     // r739
            Batch.Coord2 = Pass.TexCoordSource2;
            Batch.TextureIndex2 = Pass.TextureIndex2 != 0xFFFF ? int32(Pass.TextureIndex2) : -1;
            if (!Pass.TextureWeight.Values.IsEmpty())
            {
                float MaxWeight = 0.0f; // static doodads do not animate it: the most visible value
                for (const float Value : Pass.TextureWeight.Values) MaxWeight = FMath::Max(MaxWeight, Value);
                Batch.Weight = FMath::Clamp(MaxWeight, 0.0f, 1.0f);
            }
            Batches.Add(Batch);
        }
    }

    // r431: a model whose only passes were WoW's old light cards has nothing left to draw. The
    // all-triangles fallback below would draw those cards as flat opaque plates (the torch plate).
    if (Batches.Num() == 0 && bHasValidBatches)
    {
        UE_LOG(LogWowDoodad, Log, TEXT("Only legacy light cards in %s - no mesh"), *M2Path);
        return nullptr;
    }
    // Fallback: single batch with all triangles if no valid render passes
    if (Batches.Num() == 0)
    {
        FMaterialBatch Batch;
        Batch.StartIndex = 0;
        Batch.NumIndices = Data.Indices.Num();
        Batch.TextureIndex = 0;
        Batch.BlendMode = 0;
        Batch.Flags = 0;
        Batches.Add(Batch);
    }

    // Create polygon groups (one per batch)
    TArray<FPolygonGroupID> PolyGroups;
    for (int32 i = 0; i < Batches.Num(); ++i)
    {
        PolyGroups.Add(MeshDesc.CreatePolygonGroup());
    }

    MeshDesc.ReserveNewVertices(NumVerts);
    MeshDesc.ReserveNewVertexInstances(NumVerts);

    TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
    TVertexInstanceAttributesRef<FVector3f> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
    TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
    TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
    // r739: a pass that reads UV1 (UL_IceWall's mask) needs the model's second texture coordinates.
    const bool bNeedsUV1 = !bUEWaterfall && Batches.ContainsByPredicate([](const FMaterialBatch& Each)
        { return Each.Coord == 1 || (Each.TextureIndex2 >= 0 && Each.Coord2 == 1); });
    if (bNeedsUV1) VertexInstanceUVs.SetNumChannels(2);

    // Create vertices and vertex instances
    TArray<FVertexInstanceID> VertexInstanceIDs;
    VertexInstanceIDs.SetNum(NumVerts);

    for (int32 i = 0; i < NumVerts; ++i)
    {
        const FM2Vertex& V = Data.Vertices[i];
        FVertexID VertID = MeshDesc.CreateVertex();

        // WoW model space → UE local: (Y, X, Z) * SCALE
        // Same derivation as WMO: model→ADT is (X,Z,-Y), ADT→UE is (-Z,X,Y)
        VertexPositions[VertID] = FVector3f(M2VertexToUELocal(V));

        FVertexInstanceID InstID = MeshDesc.CreateVertexInstance(VertID);
        VertexInstanceIDs[i] = InstID;

        FVector3f Normal(V.Normal.Y, V.Normal.X, V.Normal.Z);
        Normal.Normalize();
        VertexInstanceNormals[InstID] = Normal;

        // Approximate tangent
        FVector3f T = FVector3f::CrossProduct(Normal, FVector3f(0, 1, 0));
        if (T.SizeSquared() < 0.001f)
        {
            T = FVector3f::CrossProduct(Normal, FVector3f(1, 0, 0));
        }
        T.Normalize();
        VertexInstanceTangents[InstID] = T;
        VertexInstanceBinormalSigns[InstID] = 1.0f;

        // The source contour is only a spatial guide for the UE effect. Use
        // normalized height coordinates, not WoW texture/animation coordinates.
        const FVector2f UV = bUEWaterfall
            ? FVector2f(V.TexCoord.X, 1.0 - (V.Position.Z - FallBounds.Min.Z) / FallHeight)
            : FVector2f(V.TexCoord.X, V.TexCoord.Y);
        VertexInstanceUVs.Set(InstID, 0, UV);
        if (bNeedsUV1) VertexInstanceUVs.Set(InstID, 1, FVector2f(V.TexCoord2.X, V.TexCoord2.Y));
    }

    int32 CreatedTriangleCount = 0;

    // Create triangles per batch (polygon group)
    for (int32 BatchIdx = 0; BatchIdx < Batches.Num(); ++BatchIdx)
    {
        const FMaterialBatch& Batch = Batches[BatchIdx];
        int32 NumBatchTris = Batch.NumIndices / 3;

        for (int32 t = 0; t < NumBatchTris; ++t)
        {
            int32 BaseIdx = Batch.StartIndex + t * 3;
            if (BaseIdx + 2 >= Data.Indices.Num()) break;

            uint16 I0 = Data.Indices[BaseIdx];
            uint16 I1 = Data.Indices[BaseIdx + 1];
            uint16 I2 = Data.Indices[BaseIdx + 2];

            if (I0 >= NumVerts || I1 >= NumVerts || I2 >= NumVerts) continue;

            TArray<FVertexInstanceID> TriVerts;
            TriVerts.Add(VertexInstanceIDs[I0]);
            TriVerts.Add(VertexInstanceIDs[I1]);
            TriVerts.Add(VertexInstanceIDs[I2]);
            MeshDesc.CreatePolygon(PolyGroups[BatchIdx], TriVerts);
			++CreatedTriangleCount;
        }
    }

	if (CreatedTriangleCount == 0)
	{
		UE_LOG(LogWowDoodad, Warning, TEXT("Skipped M2 mesh with no surviving triangles after filtering: %s"), *M2Path);
		return nullptr;
	}
    // Build materials and collect them for post-build assignment
    UMaterial* FallbackMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));

    TArray<UMaterialInterface*> BatchMaterials;
    struct FClothBatch { UMaterialInstanceDynamic* MID; WowCloth::EKind Kind; bool bAlphaCut; };
    TArray<FClothBatch> ClothBatches; // r535: configured with the mesh bounds after the build
    // Textures whose UVs tile past 0..1 on some batch of this model (tree trunks repeat their
    // bark up the trunk). Clamping those stretches the edge texel row over the overhang and
    // draws a light rim at the base, so they keep wrapping on that axis whatever the flags say.
    TMap<int32, uint8> TilingAxes; // bit 0 = U, bit 1 = V
    for (const FMaterialBatch& Each : Batches)
    {
        uint8& Axes = TilingAxes.FindOrAdd(Each.TextureIndex);
        for (int32 I = Each.StartIndex; I < Each.StartIndex + Each.NumIndices && Data.Indices.IsValidIndex(I); ++I)
        {
            const int32 V = Data.Indices[I];
            if (!Data.Vertices.IsValidIndex(V)) continue;
            const FVector2D& UV = Data.Vertices[V].TexCoord;
            if (UV.X < -0.02f || UV.X > 1.02f) Axes |= 1;
            if (UV.Y < -0.02f || UV.Y > 1.02f) Axes |= 2;
        }
    }
    if (Mpq && Cache)
    {
        for (int32 BatchIdx = 0; BatchIdx < Batches.Num(); ++BatchIdx)
        {
            const FMaterialBatch& Batch = Batches[BatchIdx];
            // UE Water owns the complete visual appearance. Do not even load
            // waterfall BLPs or source animation tracks for these surfaces.
            if (bUEWaterfall)
            {
                UMaterial* WaterfallMat = FWowTerrainMaterial::GetSimpleWaterfallObjectMaterial();
                UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(WaterfallMat, StaticMesh);
                MID->SetScalarParameterValue(TEXT("FlowV"), BatchIdx == 0 ? -0.85f : -1.35f);
                MID->SetScalarParameterValue(TEXT("SheetOpacity"), BatchIdx == 0 ? 0.62f : 0.22f);
                MID->SetScalarParameterValue(TEXT("FlowPhase"), BatchIdx == 0 ? 0.0f : 0.37f);
                BatchMaterials.Add(MID);
                continue;
            }
            UTexture2D* Tex = nullptr;

            if (Batch.TextureIndex >= 0 && Batch.TextureIndex < Data.TexturePaths.Num())
            {
                Tex = LoadBlpTexture(Data.TexturePaths[Batch.TextureIndex], Mpq, Cache);
            }

            // If texture failed to load, try alternate paths
            if (!Tex && Batch.TextureIndex >= 0 && Batch.TextureIndex < Data.TexturePaths.Num())
            {
                const FString& FailedPath = Data.TexturePaths[Batch.TextureIndex];
                FString TexFilename = FPaths::GetCleanFilename(FailedPath);

                // Try 1: look in the M2 model's own directory
                FString M2Dir = FPaths::GetPath(M2Path);
                Tex = LoadBlpTexture(M2Dir / TexFilename, Mpq, Cache);

                // Try 2: Interface/ textures often have duplicates in World/ paths
                if (!Tex && FailedPath.Contains(TEXT("Interface"), ESearchCase::IgnoreCase))
                {
                    // Try the StormWind passive doodads directory (common for Elwynn trees)
                    Tex = LoadBlpTexture(FString(TEXT("World/Generic/Human/Passive Doodads/StormWind/")) + TexFilename, Mpq, Cache);
                }

                // Try 3: look in parent directories of the M2
                if (!Tex)
                {
                    FString ParentDir = FPaths::GetPath(M2Dir);
                    Tex = LoadBlpTexture(ParentDir / TexFilename, Mpq, Cache);
                }
            }

            // These cards have transparent borders, but lower mips contain
            // opaque stems at the bottom. Wrapping bleeds them across the top
            // and draws the rectangular outline seen above the cattails.
            const bool bReedCard = M2Path.Contains(TEXT("catail"), ESearchCase::IgnoreCase)
                || M2Path.Contains(TEXT("cattail"), ESearchCase::IgnoreCase)
                || M2Path.Contains(TEXT("reed"), ESearchCase::IgnoreCase);
            if (Tex && bReedCard && (Tex->AddressX != TA_Clamp || Tex->AddressY != TA_Clamp || Tex->Filter != TF_Default))
            {
                Tex->AddressX = TA_Clamp;
                Tex->AddressY = TA_Clamp;
                Tex->LODGroup = TEXTUREGROUP_World;
                Tex->Filter = TF_Default; // world anisotropic filtering at oblique angles
                Tex->UpdateResource();
            }
            else if (Tex && Data.TextureFlags.IsValidIndex(Batch.TextureIndex))
            {
                // Only cut-out/blended cards: an opaque trunk whose UVs end exactly at the
                // texture edge showed its last (light) texel row as a bright rim at the base
                // once clamped (Winterspring trees). Opaque batches keep wrapping.
                // Honour the M2 texture's wrap flags like the original client: foliage
                // cards (snowy pine branches etc.) are authored to clamp, and wrapping
                // bled their opposite border in as thin bright lines along the card edges.
                const uint32 Flags = Data.TextureFlags[Batch.TextureIndex];
                const uint8 Tiles = TilingAxes.FindRef(Batch.TextureIndex);
                // Diagnostics for the light rim at tree bases (one line per batch, trees only).
                if (M2Path.Contains(TEXT("tree"), ESearchCase::IgnoreCase))
                {
                    // Batch extent: a flat ring near the model's lowest point is a ground skirt.
                    float MinZ = FLT_MAX, MaxZ = -FLT_MAX, MaxR = 0.f, ModelMinZ = FLT_MAX;
                    FVector2D MinUV(FLT_MAX), MaxUV(-FLT_MAX);
                    for (const auto& Vert : Data.Vertices) ModelMinZ = FMath::Min(ModelMinZ, float(Vert.Position.Z));
                    for (int32 I = Batch.StartIndex; I < Batch.StartIndex + Batch.NumIndices && Data.Indices.IsValidIndex(I); ++I)
                    {
                        const int32 V = Data.Indices[I];
                        if (!Data.Vertices.IsValidIndex(V)) continue;
                        const FVector P = FVector(Data.Vertices[V].Position);
                        MinZ = FMath::Min(MinZ, float(P.Z)); MaxZ = FMath::Max(MaxZ, float(P.Z));
                        MaxR = FMath::Max(MaxR, float(FVector2D(P.X, P.Y).Size()));
                        MinUV = FVector2D::Min(MinUV, Data.Vertices[V].TexCoord); MaxUV = FVector2D::Max(MaxUV, Data.Vertices[V].TexCoord);
                    }
                    UE_LOG(LogWowDoodad, Log, TEXT("TreeBatch %s #%d tex=%s blend=%u flags=0x%x texflags=0x%x tiles=%u z=%.2f..%.2f (model min %.2f) r=%.2f uv=%.2f..%.2f,%.2f..%.2f"),
                        *FPaths::GetBaseFilename(M2Path), BatchIdx, *FPaths::GetBaseFilename(Data.TexturePaths[Batch.TextureIndex]),
                        Batch.BlendMode, Batch.Flags, Flags, Tiles, MinZ, MaxZ, ModelMinZ, MaxR, MinUV.X, MaxUV.X, MinUV.Y, MaxUV.Y);
                }
                // Cut-out cards (grass tufts, reeds, branches) whose UVs stay inside 0..1 never
                // need wrapping, whatever their flags say; wrapping only bleeds the opposite
                // edge in (the thin lines drawn above water-side grass). Other blend modes keep
                // the authored flags (scrolling water/lava may wrap without tiling geometry).
                const bool bCard = Batch.BlendMode == 1;
                const TextureAddress WantX = ((!bCard && (Flags & 0x1)) || (Tiles & 1)) ? TA_Wrap : TA_Clamp;
                const TextureAddress WantY = ((!bCard && (Flags & 0x2)) || (Tiles & 2)) ? TA_Wrap : TA_Clamp;
                // r469 (Stefan: a Shadowglen tree trunk looked smooth and smeared, with chevron
                // bands): giant trunks wrap their bark 5-7 times around but stretch it over the
                // whole height, so plain trilinear filtering picks a tiny blurred mip. Opaque doodad
                // surfaces get the world's anisotropic filtering, like WMO walls (r310).
                const bool bAniso = Batch.BlendMode == 0 && Tex->Filter != TF_Default;
                if (Tex->AddressX != WantX || Tex->AddressY != WantY || bAniso)
                {
                    Tex->AddressX = WantX;
                    Tex->AddressY = WantY;
                    if (bAniso)
                    {
                        Tex->LODGroup = TEXTUREGROUP_World;
                        Tex->Filter = TF_Default;
                    }
                    Tex->UpdateResource();
                }
            }

            const FString SourceTexturePath = (Batch.TextureIndex >= 0 && Batch.TextureIndex < Data.TexturePaths.Num())
                ? Data.TexturePaths[Batch.TextureIndex]
                : FString();
            UMaterial* BaseMat = ShouldUseFoliageMaterialForDoodadBatch(M2Path, SourceTexturePath)
                ? FWowTerrainMaterial::GetSimpleFoliageObjectMaterial()
                : Batch.BlendMode == 1 && !IsWaterfallDoodadPath(M2Path)
                    ? FWowTerrainMaterial::GetSimpleAlphaKeyObjectMaterial() // WoW alpha reference, no fringe rings
                    : ((Batch.Flags & 0x04) == 0 // r306: render flag 0x04 = two-sided; others are culled
                        ? FWowTerrainMaterial::GetSimpleObjectMaterialSingleSided()
                        : FWowTerrainMaterial::GetSimpleObjectMaterial());
            if (!BaseMat) BaseMat = FWowTerrainMaterial::GetSimpleObjectMaterial();
            if (!IsWaterfallDoodadPath(M2Path) &&
				!ShouldUseFoliageMaterialForDoodadBatch(M2Path, SourceTexturePath) &&
				(Batch.BlendMode == 2 || Batch.BlendMode == 6))
            {
                BaseMat = FWowTerrainMaterial::GetSimpleTranslucentObjectMaterial();
            }
            else if (!IsWaterfallDoodadPath(M2Path) &&
				!ShouldUseFoliageMaterialForDoodadBatch(M2Path, SourceTexturePath) &&
				Batch.BlendMode >= 3)
            {
                BaseMat = FWowTerrainMaterial::GetSimpleAdditiveObjectMaterial();
            }

            // r535: banners, flags, tents and rugs get the cloth material (sheen, wind on hanging and
            // stretched cloth). Effects and foliage keep theirs.
            WowCloth::EKind ClothKind = WowCloth::EKind::None;
            // r544 (Stefan: Stormwind's red griffon banners missing): alpha-keyed cloth passes went
            // invisible with the cloth material; they keep the proven alpha-key material until that
            // is understood. Only opaque passes get cloth.
            if (Batch.BlendMode == 0 && !IsWaterfallDoodadPath(M2Path) && !ShouldUseFoliageMaterialForDoodadBatch(M2Path, SourceTexturePath))
            {
                ClothKind = WowCloth::Classify(M2Path, SourceTexturePath);
                if (ClothKind != WowCloth::EKind::None)
                {
                    if (UMaterial* Cloth = WowCloth::GetMaterial()) BaseMat = Cloth;
                    else ClothKind = WowCloth::EKind::None;
                }
            }

            // r739 (Stefan 23.10, TormentWoW's Ulduar chains: "Then get it supported?"): passes that take the sphere
            // environment map, UV1 or a second texture (chains, ice, glass, masked frost walls) get the layered M2
            // material, which does what the client's combiners do. Alpha-key cards, cloth, foliage and waterfalls keep
            // theirs.
            const bool bLayered = (Batch.Coord != 0 || Batch.TextureIndex2 >= 0) && Batch.BlendMode != 1
                && ClothKind == WowCloth::EKind::None && !IsWaterfallDoodadPath(M2Path)
                && !ShouldUseFoliageMaterialForDoodadBatch(M2Path, SourceTexturePath);
            bool bUseLayered = false;
            if (bLayered)
                if (UMaterial* Layered = WowGetM2LayeredMaterial(Batch.BlendMode == 0 ? 0
                    : (Batch.BlendMode == 2 || Batch.BlendMode == 6) ? 1 : 2))
                {
                    BaseMat = Layered;
                    bUseLayered = true;
                }

            if (!BaseMat)
            {
                BatchMaterials.Add(FallbackMat);
                continue;
            }

            if (Batch.BlendMode >= 2 && M2Path.Contains(TEXT("water"), ESearchCase::IgnoreCase))
            {
                UE_LOG(LogWowDoodad, Log, TEXT("Using alpha effect material for %s batch %d (blend=%u flags=0x%04x texture=%d)"),
                    *M2Path, BatchIdx, Batch.BlendMode, Batch.Flags, Batch.TextureIndex);
            }

            // Always create a proper MID. M2 effect passes must preserve alpha,
            // otherwise waterfall sheets and glow cards render as black planes.
            UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, StaticMesh);
            if (Tex)
            {
                MID->SetTextureParameterValue(FName(TEXT("Layer0Texture")), Tex);
            }
            if (bUseLayered) // r739
            {
                UTexture2D* Tex2 = Data.TexturePaths.IsValidIndex(Batch.TextureIndex2)
                    ? LoadBlpTexture(Data.TexturePaths[Batch.TextureIndex2], Mpq, Cache) : nullptr;
                if (Tex2) MID->SetTextureParameterValue(TEXT("Layer1Texture"), Tex2);
                MID->SetScalarParameterValue(TEXT("Layer1On"), Tex2 ? 1.0f : 0.0f);
                MID->SetScalarParameterValue(TEXT("Env0"), Batch.Coord < 0 ? 1.0f : 0.0f);
                MID->SetScalarParameterValue(TEXT("Env1"), Batch.Coord2 < 0 ? 1.0f : 0.0f);
                MID->SetScalarParameterValue(TEXT("Uv1For0"), Batch.Coord == 1 ? 1.0f : 0.0f);
                MID->SetScalarParameterValue(TEXT("Uv1For1"), Batch.Coord2 == 1 ? 1.0f : 0.0f);
                MID->SetScalarParameterValue(TEXT("Weight"), Batch.Weight);
                static int32 GLoggedLayered = 0;
                if (GLoggedLayered < 64)
                {
                    ++GLoggedLayered;
                    UE_LOG(LogWowDoodad, Log, TEXT("Layered M2 pass %s #%d: blend %u coords %d/%d second texture %s weight %.2f"),
                        *FPaths::GetBaseFilename(M2Path), BatchIdx, Batch.BlendMode, Batch.Coord, Batch.Coord2,
                        Tex2 ? *FPaths::GetBaseFilename(Data.TexturePaths[Batch.TextureIndex2]) : TEXT("-"), Batch.Weight);
                }
            }
            if (ClothKind != WowCloth::EKind::None) ClothBatches.Add({MID, ClothKind, Batch.BlendMode == 1});
            BatchMaterials.Add(MID);
        }
    }
    else
    {
        for (int32 i = 0; i < Batches.Num(); ++i)
        {
            BatchMaterials.Add(FallbackMat);
        }
    }

    // Add materials BEFORE build
    for (UMaterialInterface* Mat : BatchMaterials)
    {
        FStaticMaterial StaticMaterial(Mat);
        // Runtime meshes do not run the editor texture-streaming analysis.
        // Initialize UV metadata before BuildFromMeshDescriptions queries it.
        StaticMaterial.UVChannelData.bInitialized = true;
        StaticMesh->GetStaticMaterials().Add(MoveTemp(StaticMaterial));
    }

	const bool bBuildCollision = ShouldUsePlayerBlockingCollision(M2Path);
	if (bBuildCollision)
	{
		StaticMesh->bAllowCPUAccess = true;
	}

    // Build static mesh from description
    TArray<const FMeshDescription*> MeshDescs;
    MeshDescs.Add(&MeshDesc);

    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bBuildSimpleCollision = false;
    Params.bFastBuild = true;
	Params.bAllowCpuAccess = bBuildCollision;
    // This is a transient runtime mesh. Committing the description in an
    // editor build recreates its material slots immediately before the fast
    // build initializes render resources, discarding the UV metadata above.
    Params.bCommitMeshDescription = false;
    WowPrepareRuntimeMeshMaterials(StaticMesh, const_cast<FMeshDescription&>(*MeshDescs[0]));
    StaticMesh->BuildFromMeshDescriptions(MeshDescs, Params);
    if (!ClothBatches.IsEmpty())
    {
        const FBox Bounds = StaticMesh->GetBoundingBox();
        for (const FClothBatch& Cloth : ClothBatches)
            WowCloth::Configure(Cloth.MID, Cloth.Kind, Cloth.bAlphaCut, float(Bounds.Min.Z), float(Bounds.Max.Z - Bounds.Min.Z));
        UE_LOG(LogWowDoodad, Log, TEXT("Cloth: %s (%d passes)"), *M2Path, ClothBatches.Num());
    }
    WowBuildRuntimeLumenCards(StaticMesh, MeshDesc);
    StaticMesh->NeverStream = true;

	if (bBuildCollision)
	{
		if (!StaticMesh->GetBodySetup())
		{
			StaticMesh->CreateBodySetup();
		}
		if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
		{
			BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
			BodySetup->bMeshCollideAll = true;
			BodySetup->CreatePhysicsMeshes();
		}
	}

    // Log material state after build to diagnose
    static bool bLoggedMatState = false;
    if (!bLoggedMatState)
    {
        bLoggedMatState = true;
        UE_LOG(LogWowDoodad, Warning, TEXT("After build: %d static materials, %d batch materials"),
            StaticMesh->GetStaticMaterials().Num(), BatchMaterials.Num());
        for (int32 i = 0; i < StaticMesh->GetStaticMaterials().Num(); ++i)
        {
            UMaterialInterface* M = StaticMesh->GetStaticMaterials()[i].MaterialInterface;
            UE_LOG(LogWowDoodad, Warning, TEXT("  mat[%d] = %s"), i, M ? *M->GetName() : TEXT("null"));
        }
    }

    // Re-set materials after build in case BuildFromMeshDescriptions resets them
    for (int32 i = 0; i < BatchMaterials.Num(); ++i)
    {
        if (StaticMesh->GetStaticMaterials().Num() <= i)
        {
            StaticMesh->GetStaticMaterials().SetNum(i + 1);
        }
        StaticMesh->GetStaticMaterials()[i].MaterialInterface = BatchMaterials[i];
        StaticMesh->GetStaticMaterials()[i].UVChannelData.bInitialized = true;
    }

    // Ensure bounds are computed — bFastBuild may skip this
    StaticMesh->CalculateExtendedBounds();

    return StaticMesh;
}

UStaticMesh* FWowDoodadManager::GetOrCreateStaticMesh(const FString& M2Path, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    if (!Mpq || !Cache) return nullptr;

    const FString AssetPath = NormalizeM2AssetPath(M2Path);
    if (AssetPath.IsEmpty()) return nullptr;

    FString NormalizedPath = AssetPath.ToLower();
    NormalizedPath.ReplaceInline(TEXT("/"), TEXT("\\"));

    // Original BlacksmithForge has a constant zero alpha track and no collision
    // mesh. It is a tradeskill helper, not visible forge architecture. Never
    // turn its invisible rectangular geometry into a mesh or fallback blocker.
    if (NormalizedPath == TEXT("world\\skillactivated\\tradeskillenablers\\blacksmithforge.m2"))
        return nullptr;

    // Check cache first
    UStaticMesh* Cached = Cache->FindMesh(NormalizedPath);
    if (Cached) return Cached;

    // Parse M2
    TSharedPtr<FM2Data> M2Data = GetOrParseM2(AssetPath, Mpq);
    if (!M2Data || !M2Data->bIsValid) return nullptr;

    // Create static mesh
    UStaticMesh* SM = CreateStaticMeshFromM2(*M2Data, AssetPath, Mpq, Cache);
    if (SM)
    {
        Cache->CacheMesh(NormalizedPath, SM);
        UE_LOG(LogWowDoodad, Log, TEXT("Created StaticMesh for %s (%d verts)"), *AssetPath, M2Data->Vertices.Num());
    }

    return SM;
}

TArray<UInstancedStaticMeshComponent*> FWowDoodadManager::SpawnDoodadsInstanced(
    AActor* ParentActor, const TArray<FAdtDoodadPlacement>& Placements,
    const TArray<FString>& DoodadPaths, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    TArray<UInstancedStaticMeshComponent*> Result;

    if (!ParentActor || !Mpq || !Cache || Placements.Num() == 0)
    {
        return Result;
    }

    // Group placements by M2 path
    TMap<FString, TArray<const FAdtDoodadPlacement*>> GroupedPlacements;
    for (const FAdtDoodadPlacement& Placement : Placements)
    {
        if (Placement.NameIndex < 0 || Placement.NameIndex >= DoodadPaths.Num()) continue;
        const FString Path = NormalizeM2AssetPath(DoodadPaths[Placement.NameIndex]);
        if (Path.IsEmpty()) continue;

        GroupedPlacements.FindOrAdd(Path).Add(&Placement);
    }

    int32 TotalInstances = 0;
	int32 TotalSimpleBlockers = 0;
	int32 TotalTreeBlockers = 0;

    for (auto& Pair : GroupedPlacements)
    {
		const FString& M2Path = Pair.Key;
		const TArray<const FAdtDoodadPlacement*>& GroupPlacements = Pair.Value;

        // Get or create static mesh for this model
		UStaticMesh* SM = GetOrCreateStaticMesh(M2Path, Mpq, Cache);
		if (!SM)
		{
			// r563: the dungeon portals in the open world have no mesh but must still be placed: they are
			// InstancePortal.m2 (Deadmines, Ragefire Chasm, Wailing Caverns), whose whole look is two
			// emitters. They were skipped, so those entrances had no portal at all. (Portals only: other
			// meshless models, such as old light cards, stay skipped.)
			const TSharedPtr<FM2Data> EffectOnly = GetOrParseM2(M2Path, Mpq);
			if (EffectOnly.IsValid() && EffectOnly->ParticleEmitters.Num() > 0 && FPaths::GetBaseFilename(M2Path).StartsWith(TEXT("InstancePortal"), ESearchCase::IgnoreCase))
				for (const FAdtDoodadPlacement* Placement : GroupPlacements)
				{
					const FVector UEPos = FWowCoordinate::AdtToUE(Placement->Position.X, Placement->Position.Y, Placement->Position.Z);
					const FRotator Rot = FWowCoordinate::WowRotationToUE(Placement->Rotation.X, Placement->Rotation.Y, Placement->Rotation.Z);
					UWowM2ParticleComponent* Effect = NewObject<UWowM2ParticleComponent>(ParentActor, MakeUniqueObjectName(ParentActor,
						UWowM2ParticleComponent::StaticClass(), *FString::Printf(TEXT("EffectOnlyDoodad_%u"), Placement->UniqueId)));
					if (!Effect) continue;
					Effect->SetupAttachment(ParentActor->GetRootComponent());
					ParentActor->AddInstanceComponent(Effect);
					Effect->SetWorldLocation(UEPos);
					Effect->SetWorldRotation(Rot);
					Effect->SetWorldScale3D(FVector(FMath::Max(Placement->GetScaleFloat(), 0.001f)));
					Effect->RegisterComponent();
					Effect->SetFixturePath(M2Path);
					Effect->InitializeFromParsedM2Data(EffectOnly.Get(), Mpq, Cache);
				}
			continue;
		}
		// Parsed geometry is also used to derive the visible trunk footprint for
		// collision, so keep this lookup independent of particle/light metadata.
		const TSharedPtr<FM2Data> ParsedM2 = GetOrParseM2(M2Path, Mpq);
		const bool bSpawnLightParticles = ParsedM2.IsValid() &&
			(ParsedM2->ParticleEmitters.Num() > 0 || ParsedM2->RibbonEmitters.Num() > 0) &&
			!IsWaterfallDoodadPath(M2Path) &&
			!IsLegacyWaterEffectCardPath(M2Path);
		if (bSpawnLightParticles)
		{
			UE_LOG(LogWowDoodad, Log,
				TEXT("Enabling %d authored light/fire emitters for %s"),
				ParsedM2->ParticleEmitters.Num(), *M2Path);
		}

        // Birds, butterflies and other ambient animals: one animated mesh per placement.
        if (ParsedM2.IsValid() && ParsedM2->HasAnimationData()
            && (IsAmbientAnimalDoodadPath(M2Path) || HasTravellingAnimation(*ParsedM2) || HasSpinningAnimation(*ParsedM2)))
        {
            // Preferred: GPU-skinned skeletal mesh. It writes motion vectors, so the
            // upscaler (FSR/TSR) does not smear a ghost trail behind distant birds the
            // way it does behind the CPU-deformed procedural mesh below.
            USkeletalMesh* Skinned = ParsedM2->Vertices.Num() <= 4096 ? GetOrCreateSkeletalMesh(M2Path, Mpq, Cache, true) : nullptr;
            FString AnimKey = M2Path.ToLower(); AnimKey.ReplaceInline(TEXT("/"), TEXT("\\"));
            const TArray<TWeakObjectPtr<UAnimSequence>>* Clips = AnimCache.Find(AnimKey);
            UAnimSequence* Loop = Clips && Clips->Num() > 0 ? (*Clips)[0].Get() : nullptr;
            if (Skinned && Loop)
            {
                for (const FAdtDoodadPlacement* Placement : GroupPlacements)
                {
                    const FVector UEPos = FWowCoordinate::AdtToUE(Placement->Position.X, Placement->Position.Y, Placement->Position.Z);
                    const FRotator Rot = FWowCoordinate::WowRotationToUE(Placement->Rotation.X, Placement->Rotation.Y, Placement->Rotation.Z);
                    USkeletalMeshComponent* Animal = NewObject<USkeletalMeshComponent>(ParentActor,
                        *FString::Printf(TEXT("AmbientSkin_%s_%u"), *FPaths::GetBaseFilename(M2Path), Placement->UniqueId));
                    Animal->SetSkeletalMesh(Skinned);
                    Animal->SetupAttachment(ParentActor->GetRootComponent());
                    Animal->SetWorldTransform(FTransform(Rot, UEPos, FVector(FMath::Max(Placement->GetScaleFloat(), 0.001f))));
                    Animal->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                    Animal->SetCastShadow(true);
                    Animal->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
                    // Same materials as the static version (identical batch order).
                    if (SM->GetStaticMaterials().Num() == Skinned->GetMaterials().Num())
                        for (int32 Slot = 0; Slot < SM->GetStaticMaterials().Num(); ++Slot)
                            Animal->SetMaterial(Slot, SM->GetMaterial(Slot));
                    ParentActor->AddInstanceComponent(Animal);
                    Animal->RegisterComponent();
                    Animal->PlayAnimation(Loop, true);
                    Animal->SetPosition(static_cast<float>((Placement->UniqueId * 2654435761u) % 10000u) * 0.001f, false);
                    // r703 (Stefan, Teldrassil at night: "these specs, I bet they are fireflies? They should light up
                    // much more"): a firefly swarm glows - a soft green-yellow light in its middle that lights the
                    // ground and leaves round it at night (gone by day, faded out beyond ~45 m, no shadows).
                    if (FPaths::GetBaseFilename(M2Path).Contains(TEXT("firefl")))
                    {
                        UWowDuskLightComponent* Glow = NewObject<UWowDuskLightComponent>(ParentActor,
                            *FString::Printf(TEXT("FireflyGlow_%u"), Placement->UniqueId));
                        Glow->SetupAttachment(Animal);
                        Glow->SetRelativeLocation(FVector(0.0, 0.0, 30.0));
                        Glow->bOutdoor = true;
                        Glow->FixtureIntensity = 260.0f;
                        Glow->SetLightColor(FLinearColor(0.72f, 1.0f, 0.42f));
                        Glow->SetAttenuationRadius(320.0f);
                        Glow->SetSourceRadius(40.0f);
                        Glow->SetCastShadows(false);
                        Glow->SetVolumetricScatteringIntensity(1.5f); // a faint glow in the night mist
                        Glow->SetSpecularScale(0.2f);
                        Glow->SetMaxDrawDistance(4500.0f);
                        Glow->MaxDistanceFadeRange = 1500.0f;
                        ParentActor->AddInstanceComponent(Glow);
                        Glow->RegisterComponent();
                    }
                }
                UE_LOG(LogWowDoodad, Log, TEXT("Animated ambient doodad %s x%d (skinned)"), *M2Path, GroupPlacements.Num());
                continue;
            }

            int32 Animated = 0;
            for (const FAdtDoodadPlacement* Placement : GroupPlacements)
            {
                const FVector UEPos = FWowCoordinate::AdtToUE(Placement->Position.X, Placement->Position.Y, Placement->Position.Z);
                const FRotator Rot = FWowCoordinate::WowRotationToUE(Placement->Rotation.X, Placement->Rotation.Y, Placement->Rotation.Z);
                UWowSpellMeshComponent* Mesh = NewObject<UWowSpellMeshComponent>(ParentActor,
                    *FString::Printf(TEXT("Ambient_%s_%u"), *FPaths::GetBaseFilename(M2Path), Placement->UniqueId));
                Mesh->SetupAttachment(ParentActor->GetRootComponent());
                Mesh->SetWorldTransform(FTransform(Rot, UEPos, FVector(FMath::Max(Placement->GetScaleFloat(), 0.001f))));
                Mesh->SetCastShadow(true);
                Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                ParentActor->AddInstanceComponent(Mesh);
                Mesh->RegisterComponent();
                // Desynchronise neighbouring flocks/butterflies.
                Mesh->SetStartTime(static_cast<float>((Placement->UniqueId * 2654435761u) % 10000u) * 0.001f);
                if (Mesh->Initialize(ParsedM2, SM, true)) ++Animated;
                else { Mesh->DestroyComponent(); Animated = -1; break; }
            }
            if (Animated >= 0)
            {
                UE_LOG(LogWowDoodad, Log, TEXT("Animated ambient doodad %s x%d (%s)"), *M2Path, Animated,
                    IsAmbientAnimalDoodadPath(M2Path) ? TEXT("name") : TEXT("travelling animation"));
                continue;
            }
            // Model too large for CPU animation: fall back to the static instances below.
        }

        // Use plain ISM instead of HISMC — more reliable bounds for runtime meshes
        FName CompName = *FString::Printf(TEXT("ISM_%s"), *FPaths::GetBaseFilename(M2Path));
        UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(ParentActor, CompName);
        ISM->SetupAttachment(ParentActor->GetRootComponent());
        ISM->SetStaticMesh(SM);
        // Solid objects and masked foliage must occlude sun/moon light too.
        // Masked leaf materials use their alpha cutout in the shadow pass;
        // transparent water/glow cards must not cast rectangular shadows.
        bool bHasShadowSurface = false;
        for (const FStaticMaterial& Slot : SM->GetStaticMaterials())
        {
            const UMaterialInterface* Material = Slot.MaterialInterface;
            if (Material && (Material->GetBlendMode() == BLEND_Opaque ||
                Material->GetBlendMode() == BLEND_Masked))
            {
                bHasShadowSurface = true;
                break;
            }
        }
        ISM->SetCastShadow(bHasShadowSurface && !IsWaterfallDoodadPath(M2Path) &&
            !IsLegacyWaterEffectCardPath(M2Path));
		// Per-placement authored/fallback blockers below are the sole collision
		// source for ADT instances.  Keeping collision on the visual ISM as well
		// duplicates physics contacts and brings foliage cards back into play.
		const bool bMeshBlocksPlayer = false;
		ConfigureDoodadCollision(ISM, bMeshBlocksPlayer);
        ISM->SetMobility(EComponentMobility::Static);
        ISM->bNeverDistanceCull = true;

        // Override materials on the ISM component
        for (int32 MatIdx = 0; MatIdx < SM->GetStaticMaterials().Num(); ++MatIdx)
        {
            if (SM->GetStaticMaterials()[MatIdx].MaterialInterface)
            {
                ISM->SetMaterial(MatIdx, SM->GetStaticMaterials()[MatIdx].MaterialInterface);
            }
        }

        // Must register BEFORE adding instances so the component transform is set
        ISM->RegisterComponent();

        // Add all instances for this model
        for (const FAdtDoodadPlacement* Placement : GroupPlacements)
        {
            FVector UEPos = FWowCoordinate::AdtToUE(
                Placement->Position.X, Placement->Position.Y, Placement->Position.Z);
            float ScaleVal = Placement->GetScaleFloat();
            FRotator Rot = FWowCoordinate::WowRotationToUE(
                Placement->Rotation.X, Placement->Rotation.Y, Placement->Rotation.Z);
            FQuat RotationQuat = Rot.Quaternion();
            if (Placement->bAlignToTerrainNormal)
            {
                const FVector TerrainNormal = Placement->TerrainNormalUE.GetSafeNormal();
                if (!TerrainNormal.IsNearlyZero())
                {
                    const FVector CurrentUp = RotationQuat.GetUpVector().GetSafeNormal();
                    const FQuat AlignToTerrain = FQuat::FindBetweenNormals(CurrentUp, TerrainNormal);
                    RotationQuat = AlignToTerrain * RotationQuat;
                }
            }

            FTransform InstanceTransform(RotationQuat, UEPos, FVector(ScaleVal));
            const int32 AddedInstanceIndex = ISM->AddInstance(InstanceTransform, /*bWorldSpace=*/true);
            if (ParsedM2.IsValid()) FWowLampFixtures::AttachM2(ParentActor,ISM,*ParsedM2,M2Path,InstanceTransform);
            if (IsWaterfallDoodadPath(M2Path) && ParsedM2.IsValid())
            {
                // Derive the impact from the actual transformed lower edge, not
                // the model pivot or its axis-aligned bounding-box centre.
                FBox FallBox(ForceInit), LowerEdge(ForceInit);
                for (const auto& Vertex : ParsedM2->Vertices)
                    FallBox += InstanceTransform.TransformPosition(M2VertexToUELocal(Vertex));
                const float Band=FMath::Max(20.0f,static_cast<float>(FallBox.GetSize().Z)*.04f);
                for (const auto& Vertex : ParsedM2->Vertices)
                {
                    const FVector P=InstanceTransform.TransformPosition(M2VertexToUELocal(Vertex));
                    if(P.Z<=FallBox.Min.Z+Band) LowerEdge+=P;
                }
                // r689 (Stefan: the mist on the left "is missing the target" - nothing visibly falls there): a sheet laid
                // almost flat (NewWaterfall at -70 degrees, a chute over rocks) does not plunge; only sheets within
                // about 53 degrees of upright get an impact spray.
                // r722 (Stefan, Northshire: the fall comes down behind the rocks and the water flows out from under them
                // into the pond - "The lower mist you removed, put that there!"): the low-lying sheet r689 dropped is that
                // outflow. Low sheets get their mist again, at their own lower lip (the lowest tenth of the sheet, a line
                // across the flow) where the water leaves the stones, instead of along the middle of the chute.
                const bool bUpright = InstanceTransform.GetRotation().GetUpVector().Z >= 0.6;
                if(LowerEdge.IsValid && (bUpright || FallBox.GetSize().Z > 150.0)) // a flat water card is no outflow
                {
                    auto* Spray=NewObject<UWowWaterSprayComponent>(ParentActor);
                    ParentActor->AddInstanceComponent(Spray);
                    Spray->SetupAttachment(ParentActor->GetRootComponent());
                    Spray->RegisterComponent();
                    const FVector Size=LowerEdge.GetSize();
                    // r711 (Stefan: the right side of the Northshire fall had no mist): the lowest 4 % of a sheet whose
                    // lower edge slants is one corner of it. The mist follows the whole lower third of the sheet: its
                    // main horizontal direction (principal axis of those vertices), its length and its middle.
                    const double LowZ=FallBox.Min.Z+FallBox.GetSize().Z*(bUpright ? .33 : .10);
                    TArray<FVector2D> Low; FVector2D Mean(0,0);
                    for (const auto& Vertex : ParsedM2->Vertices)
                    {
                        const FVector P=InstanceTransform.TransformPosition(M2VertexToUELocal(Vertex));
                        if(P.Z<=LowZ) { Low.Add(FVector2D(P.X,P.Y)); Mean+=FVector2D(P.X,P.Y); }
                    }
                    FVector Along=FVector::ZeroVector; float Length=0.f; FVector Impact=LowerEdge.GetCenter();
                    if(Low.Num()>=3)
                    {
                        Mean/=Low.Num();
                        double Cxx=0,Cyy=0,Cxy=0;
                        for(const FVector2D& Q:Low) { const FVector2D Dv=Q-Mean; Cxx+=Dv.X*Dv.X; Cyy+=Dv.Y*Dv.Y; Cxy+=Dv.X*Dv.Y; }
                        const double Angle=.5*FMath::Atan2(2*Cxy,Cxx-Cyy);
                        const FVector2D Axis(FMath::Cos(Angle),FMath::Sin(Angle));
                        double MinT=TNumericLimits<double>::Max(),MaxT=-TNumericLimits<double>::Max();
                        for(const FVector2D& Q:Low) { const double T=FVector2D::DotProduct(Q-Mean,Axis); MinT=FMath::Min(MinT,T); MaxT=FMath::Max(MaxT,T); }
                        const FVector2D Mid=Mean+Axis*((MinT+MaxT)*.5);
                        Along=FVector(Axis.X,Axis.Y,0); Length=static_cast<float>(MaxT-MinT);
                        Impact=FVector(Mid.X,Mid.Y,FallBox.Min.Z);
                    }
                    // r742 (Stefan 23.10, Northshire after r741: the bottom mist sat at the far end of the outflow chute, in the
                    // pond, and showed as grey domes; it belongs "further in", where he stood - checked against the map tile:
                    // that spot is the middle of the NewWaterfall chute, 0.8 m from its centre at half its height, where the
                    // water comes out from under the boulders; the lowest tenth r722 used is 12.6 m further out in the pond).
                    // A low sheet's mist now sits at the middle of its chute (the centre of its vertices at half its height),
                    // still spread across the flow; the spray then finds the ground or water surface there.
                    if(!bUpright)
                    {
                        const double H=FallBox.GetSize().Z;
                        const double MidZ=FallBox.Min.Z+H*.5, Slice=FMath::Max(30.0,H*.05);
                        FVector Sum=FVector::ZeroVector; int32 Count=0;
                        for (const auto& Vertex : ParsedM2->Vertices)
                        {
                            const FVector P=InstanceTransform.TransformPosition(M2VertexToUELocal(Vertex));
                            if(FMath::Abs(P.Z-MidZ)<=Slice) { Sum+=P; ++Count; }
                        }
                        if(Count>0) Impact=FVector(Sum.X/Count,Sum.Y/Count,MidZ);
                    }
                    // r741 (Stefan 23.10: the Northshire fall's mist belongs "at the gap" between the boulders where the water
                    // hits): the sheet's centre line, from 3/4 of its height down to its lower edge - the centre of the
                    // vertices in a thin slice at each height. The spray follows it down to the first thing it meets.
                    // Upright sheets only; the low outflow sheet (r722) keeps its mist at its own lip.
                    if(bUpright)
                    {
                        const double H=FallBox.GetSize().Z;
                        const double Slice=FMath::Max(30.0,H*.05);
                        TArray<FVector> Path;
                        for(int32 Step=0; Step<=10; ++Step)
                        {
                            const double Z=FallBox.Min.Z+H*(.75-.075*Step);
                            FVector Sum=FVector::ZeroVector; int32 Count=0;
                            for (const auto& Vertex : ParsedM2->Vertices)
                            {
                                const FVector P=InstanceTransform.TransformPosition(M2VertexToUELocal(Vertex));
                                if(FMath::Abs(P.Z-Z)<=Slice) { Sum+=P; ++Count; }
                            }
                            if(Count>0) Path.Add(FVector(Sum.X/Count,Sum.Y/Count,Z));
                        }
                        Spray->SetFallPath(Path);
                    }
                    Spray->InitializeFall(Impact,FMath::Max(Size.X,Size.Y),FallBox.GetSize().Z,Along,Length);
                }
            }
			if (IsWaterfallAssetDebugEnabled() && IsNearWaterfallDebugPoint(UEPos))
			{
				const FBoxSphereBounds Bounds = SM->GetBounds();
				const FVector WorldExtent(
					FMath::Abs(Bounds.BoxExtent.X * ScaleVal),
					FMath::Abs(Bounds.BoxExtent.Y * ScaleVal),
					FMath::Abs(Bounds.BoxExtent.Z * ScaleVal));
				UE_LOG(LogWowDoodad, Warning,
					TEXT("Waterfall-near ADT doodad: path=%s uid=%u loc=%s extent=%s scale=%.3f rot=%s"),
					*M2Path,
					Placement->UniqueId,
					*UEPos.ToCompactString(),
					*WorldExtent.ToCompactString(),
					ScaleVal,
					*Rot.ToCompactString());
			}
			const int32 WorldInstanceIndex = (AddedInstanceIndex >= 0) ? (TotalInstances + AddedInstanceIndex) : TotalInstances;
			CreateDoodadPointLight(ParentActor, ParentActor->GetRootComponent(), M2Path, UEPos, Placement->UniqueId, WorldInstanceIndex, &InstanceTransform);
            if (ParsedM2.IsValid() && M2Path.Contains(TEXT("STORMWINDGYPSYWAGON")))
            {
                // The lantern is embedded in the wagon, not a separate lamp doodad.
                // Its authored flame position agrees with the M2 point-light position.
                for (const auto& Emitter : ParsedM2->ParticleEmitters)
                {
                    auto* Light=NewObject<UWowDuskLightComponent>(ParentActor);
                    ParentActor->AddInstanceComponent(Light);
                    Light->SetupAttachment(ParentActor->GetRootComponent());
                    Light->SetWorldLocation(InstanceTransform.TransformPosition(M2PointToUELocal(Emitter.Position)));
                    Light->RegisterComponent();
                }
            }
			if (bSpawnLightParticles)
			{
				const FName ParticleName = *FString::Printf(
					TEXT("InstancedDoodadParticles_%u_%d"), Placement->UniqueId, WorldInstanceIndex);
				UWowM2ParticleComponent* ParticleComponent =
					NewObject<UWowM2ParticleComponent>(ParentActor, ParticleName);
				if (ParticleComponent)
				{
					ParticleComponent->SetupAttachment(ParentActor->GetRootComponent());
					ParentActor->AddInstanceComponent(ParticleComponent);
					ParticleComponent->SetWorldLocation(UEPos);
					ParticleComponent->SetWorldRotation(Rot);
					ParticleComponent->SetWorldScale3D(FVector(ScaleVal));
					ParticleComponent->RegisterComponent();
					ParticleComponent->SetFixturePath(M2Path);
                    ParticleComponent->InitializeFromParsedM2Data(ParsedM2.Get(), Mpq, Cache);
				}
			}
			if (CreateDoodadBlocker(ParentActor, SM, ParsedM2.Get(), M2Path,
				InstanceTransform, Placement->UniqueId, WorldInstanceIndex))
			{
				++TotalSimpleBlockers;
				if (IsTreeTrunkDoodadPath(M2Path))
				{
					++TotalTreeBlockers;
				}
			}
        }

        ISM->MarkRenderStateDirty();
		if (bMeshBlocksPlayer)
		{
			ISM->RecreatePhysicsState();
		}

        Result.Add(ISM);
        TotalInstances += GroupPlacements.Num();
    }

    UE_LOG(LogWowDoodad, Log, TEXT("Instanced %d doodads across %d ISMs (%d unique models, %d simple blockers, %d tree trunk blockers)"),
        TotalInstances, Result.Num(), GroupedPlacements.Num(), TotalSimpleBlockers, TotalTreeBlockers);

    return Result;
}

bool FWowDoodadManager::ShouldUseSkeletalMesh(const FM2Data& Data)
{
    // Temporarily disabled: skeletal mesh GPU skin crashes on some M2 models
    return false;
    // return Data.HasBones() && Data.HasAnimationData() && Data.Bones.Num() > 1;
}

USkeletalMeshComponent* FWowDoodadManager::SpawnAmbientAnimal(AActor* Owner, USceneComponent* Parent, const FString& M2Path,
    const FTransform& RelativeTransform, UStaticMesh* StaticForMaterials, uint32 Seed, FMpqManager* Mpq, FWowAssetCache* Cache)
{
    if (!Owner || !Parent) return nullptr;
    const TSharedPtr<FM2Data> Parsed = GetOrParseM2(M2Path, Mpq);
    if (!Parsed.IsValid() || !Parsed->HasAnimationData()) return nullptr;
    if (!IsAmbientAnimalDoodadPath(M2Path) && !HasTravellingAnimation(*Parsed) && !HasSpinningAnimation(*Parsed)) return nullptr;
    // r597 (Stefan, Stormwind harbour lighthouse: a grey solid cone "flinging around"; "we also need to make
    // this an actual light ... the strongest spotlight"): the beam (Stormwind_LighthouseBeam_01, one bone
    // turning once every 13.3 s) went down the skinned path, and its additive beam material has no skeletal
    // usage, so the engine drew the grey default material instead. One-bone spinning models whose materials
    // are see-through now turn as an ordinary static mesh (their own additive/translucent materials), and the
    // lighthouse beam carries real spotlights along each beam: very strong, long reach, lighting the fog.
    if (Parsed->Bones.Num() == 1 && StaticForMaterials && HasSpinningAnimation(*Parsed))
    {
        bool bSeeThrough = false;
        for (const FStaticMaterial& Slot : StaticForMaterials->GetStaticMaterials())
            if (Slot.MaterialInterface && Slot.MaterialInterface->GetBlendMode() != BLEND_Opaque
                && Slot.MaterialInterface->GetBlendMode() != BLEND_Masked) bSeeThrough = true;
        if (bSeeThrough)
        {
            const FM2BoneTrack* Track = Parsed->AnimationTracks[0].BoneTracks.IsValidIndex(0) ? &Parsed->AnimationTracks[0].BoneTracks[0] : nullptr;
            float Sweep = 0.f, AxisZ = 1.f;
            if (Track && Track->RotValues.Num() >= 2)
            {
                for (int32 K = 1; K < Track->RotValues.Num(); ++K)
                    Sweep += float(Track->RotValues[K - 1].GetNormalized().AngularDistance(Track->RotValues[K].GetNormalized()));
                Sweep += float(Track->RotValues.Last().GetNormalized().AngularDistance(Track->RotValues[0].GetNormalized()));
                const FQuat Delta = Track->RotValues[0].GetNormalized().Inverse() * Track->RotValues[1].GetNormalized();
                FVector Axis; float Angle; Delta.ToAxisAndAngle(Axis, Angle);
                AxisZ = Axis.Z;
            }
            const uint32 PeriodMs = Track && Track->RotGlobalDuration > 0 ? Track->RotGlobalDuration : Parsed->AnimationTracks[0].Duration;
            const float Period = FMath::Max(float(PeriodMs) / 1000.f, 0.5f);
            // WoW model space -> UE local swaps X and Y (a mirror), so a turn runs the other way round.
            const float YawRate = -FMath::Sign(AxisZ) * FMath::RadiansToDegrees(Sweep) / Period;

            USkeletalMeshComponent* Handle = NewObject<USkeletalMeshComponent>(Owner,
                *FString::Printf(TEXT("WmoAmbientSkin_%s_%u"), *FPaths::GetBaseFilename(M2Path), Seed));
            Handle->SetupAttachment(Parent);
            Handle->SetRelativeTransform(RelativeTransform);
            Handle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Handle->SetMobility(EComponentMobility::Movable);
            Owner->AddInstanceComponent(Handle);
            Handle->RegisterComponent();

            UStaticMeshComponent* Spin = NewObject<UStaticMeshComponent>(Owner,
                *FString::Printf(TEXT("WmoSpinMesh_%s_%u"), *FPaths::GetBaseFilename(M2Path), Seed));
            Spin->SetMobility(EComponentMobility::Movable);
            Spin->SetupAttachment(Handle);
            Spin->SetStaticMesh(StaticForMaterials);
            Spin->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Spin->SetCastShadow(false);
            Owner->AddInstanceComponent(Spin);
            Spin->RegisterComponent();
            const FVector Pivot = M2PointToUELocal(Parsed->Bones[0].PivotPoint);
            URotatingMovementComponent* Turn = NewObject<URotatingMovementComponent>(Owner,
                *FString::Printf(TEXT("WmoSpin_%s_%u"), *FPaths::GetBaseFilename(M2Path), Seed));
            Turn->RotationRate = FRotator(0.f, YawRate, 0.f);
            Turn->PivotTranslation = Pivot;
            Turn->bRotationInLocalSpace = true;
            // Never let it fall back to the owner's root (that would turn the whole WMO).
            Turn->bAutoRegisterUpdatedComponent = false;
            Turn->SetUpdatedComponent(Spin);
            Owner->AddInstanceComponent(Turn);
            Turn->RegisterComponent();

            int32 Lights = 0;
            if (M2Path.Contains(TEXT("LighthouseBeam"), ESearchCase::IgnoreCase))
            {
                // Each beam is a cone from the lamp outwards: its far vertices give the direction, the spread
                // of those vertices the cone angle.
                TArray<FVector> Far;
                float Reach = 0.f;
                for (const FM2Vertex& V : Parsed->Vertices) Reach = FMath::Max(Reach, float(FVector::Dist(M2VertexToUELocal(V), Pivot)));
                for (const FM2Vertex& V : Parsed->Vertices)
                {
                    const FVector D = M2VertexToUELocal(V) - Pivot;
                    if (D.Size() > Reach * 0.55f) Far.Add(D);
                }
                TArray<FVector> Lobes;
                if (Far.Num() > 0)
                {
                    FVector First = Far[0];
                    for (const FVector& D : Far) if (D.SizeSquared() > First.SizeSquared()) First = D;
                    const FVector A = First.GetSafeNormal();
                    FVector SumA = FVector::ZeroVector, SumB = FVector::ZeroVector;
                    for (const FVector& D : Far) (FVector::DotProduct(D.GetSafeNormal(), A) >= 0.f ? SumA : SumB) += D.GetSafeNormal();
                    Lobes.Add(SumA.GetSafeNormal());
                    if (!SumB.IsNearlyZero()) Lobes.Add(SumB.GetSafeNormal());
                }
                for (const FVector& Dir : Lobes)
                {
                    float Half = 4.f;
                    for (const FVector& D : Far)
                        if (FVector::DotProduct(D.GetSafeNormal(), Dir) > 0.f)
                            Half = FMath::Max(Half, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(FVector::DotProduct(D.GetSafeNormal(), Dir)), -1.f, 1.f))));
                    Half = FMath::Clamp(Half, 4.f, 30.f);
                    USpotLightComponent* Spot = NewObject<USpotLightComponent>(Owner,
                        *FString::Printf(TEXT("WmoLighthouseSpot_%u_%d"), Seed, Lights));
                    Spot->SetMobility(EComponentMobility::Movable);
                    Spot->SetupAttachment(Spin);
                    Spot->SetRelativeLocation(Pivot);
                    Spot->SetRelativeRotation(Dir.Rotation());
                    Spot->SetIntensityUnits(ELightUnits::Candelas);
                    Spot->SetIntensity(1500000.f);
                    Spot->SetLightColor(FLinearColor(1.0f, 0.93f, 0.78f));
                    Spot->SetAttenuationRadius(FMath::Clamp(Reach * RelativeTransform.GetScale3D().X * 1.4f, 30000.f, 120000.f));
                    Spot->SetOuterConeAngle(Half);
                    Spot->SetInnerConeAngle(Half * 0.45f);
                    Spot->SetVolumetricScatteringIntensity(6.f);
                    Spot->SetCastShadows(true);
                    Owner->AddInstanceComponent(Spot);
                    Spot->RegisterComponent();
                    ++Lights;
                }
            }
            UE_LOG(LogWowDoodad, Log, TEXT("Spinning see-through doodad %s: static mesh turning %.1f deg/s about its pivot, %d spotlights"),
                *M2Path, YawRate, Lights);
            return Handle;
        }
    }
    USkeletalMesh* Skinned = Parsed->Vertices.Num() <= 4096 ? GetOrCreateSkeletalMesh(M2Path, Mpq, Cache, true) : nullptr;
    FString AnimKey = M2Path.ToLower(); AnimKey.ReplaceInline(TEXT("/"), TEXT("\\"));
    const TArray<TWeakObjectPtr<UAnimSequence>>* Clips = AnimCache.Find(AnimKey);
    UAnimSequence* Loop = Clips && Clips->Num() > 0 ? (*Clips)[0].Get() : nullptr;
    if (!Skinned || !Loop) return nullptr;
    USkeletalMeshComponent* Animal = NewObject<USkeletalMeshComponent>(Owner,
        *FString::Printf(TEXT("WmoAmbientSkin_%s_%u"), *FPaths::GetBaseFilename(M2Path), Seed));
    Animal->SetSkeletalMesh(Skinned);
    Animal->SetupAttachment(Parent);
    Animal->SetRelativeTransform(RelativeTransform);
    Animal->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Animal->SetCastShadow(true);
    Animal->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    if (StaticForMaterials && StaticForMaterials->GetStaticMaterials().Num() == Skinned->GetMaterials().Num())
        for (int32 Slot = 0; Slot < StaticForMaterials->GetStaticMaterials().Num(); ++Slot)
            Animal->SetMaterial(Slot, StaticForMaterials->GetMaterial(Slot));
    Owner->AddInstanceComponent(Animal);
    Animal->RegisterComponent();
    Animal->PlayAnimation(Loop, true);
    Animal->SetPosition(static_cast<float>((Seed * 2654435761u) % 10000u) * 0.001f, false);
    return Animal;
}

USkeletalMesh* FWowDoodadManager::GetOrCreateSkeletalMesh(const FString& M2Path, FMpqManager* Mpq, FWowAssetCache* Cache, bool bForce)
{
    if (!Mpq || !Cache) return nullptr;

    const FString AssetPath = NormalizeM2AssetPath(M2Path);
    if (AssetPath.IsEmpty()) return nullptr;

    FString NormalizedPath = AssetPath.ToLower();
    NormalizedPath.ReplaceInline(TEXT("/"), TEXT("\\"));

    // Check cache first
    USkeletalMesh* Cached = Cache->FindSkelMesh(NormalizedPath);
    if (Cached) return Cached;

    // Parse M2
    TSharedPtr<FM2Data> M2Data = GetOrParseM2(AssetPath, Mpq);
    if (!M2Data || !M2Data->bIsValid || !(bForce ? (M2Data->HasBones() && M2Data->HasAnimationData()) : ShouldUseSkeletalMesh(*M2Data)))
    {
        return nullptr;
    }

    FString ModelName = FPaths::GetBaseFilename(AssetPath);

    // Create or find skeleton
    USkeleton* Skeleton = nullptr;
    {
        auto* CachedSkel = SkeletonCache.Find(NormalizedPath);
        if (CachedSkel && CachedSkel->IsValid())
        {
            Skeleton = CachedSkel->Get();
        }
        else
        {
            Skeleton = FWowSkeletalMeshBuilder::CreateSkeleton(*M2Data, ModelName);
            if (Skeleton)
            {
                SkeletonCache.Add(NormalizedPath, Skeleton);
            }
        }
    }

    if (!Skeleton) return nullptr;

    // Create skeletal mesh
    USkeletalMesh* SkelMesh = FWowSkeletalMeshBuilder::CreateSkeletalMesh(*M2Data, Skeleton, ModelName, Mpq, Cache);
    if (SkelMesh)
    {
        Cache->CacheSkelMesh(NormalizedPath, SkelMesh);

        // Create and cache animations
        auto* CachedAnims = AnimCache.Find(NormalizedPath);
        if (!CachedAnims)
        {
            TArray<UAnimSequence*> Anims = FWowSkeletalMeshBuilder::CreateAnimations(*M2Data, Skeleton, ModelName);
            TArray<TWeakObjectPtr<UAnimSequence>> WeakAnims;
            for (UAnimSequence* Anim : Anims)
            {
                WeakAnims.Add(Anim);
            }
            AnimCache.Add(NormalizedPath, MoveTemp(WeakAnims));
        }

        UE_LOG(LogWowDoodad, Log, TEXT("Created skeletal mesh for %s with %d bones, %d animations"),
            *AssetPath, M2Data->Bones.Num(), M2Data->AnimationTracks.Num());
    }

    return SkelMesh;
}

// r431: our light in place of each of WoW's flat light cards on this model (see RecordLegacyGlowCards).
void FWowDoodadManager::AddLegacyGlowLights(AActor* Owner, USceneComponent* DoodadComponent, const FString& M2Path,
    uint32 UniqueId, int32 Index, const FTransform* DoodadTransform)
{
    if (!Owner || !DoodadComponent) return;
    TArray<FLegacyGlowCard> Cards;
    {
        FScopeLock Lock(&GLegacyGlowLock);
        if (const TArray<FLegacyGlowCard>* Found = GLegacyGlowCards.Find(LegacyGlowKey(M2Path))) Cards = *Found;
    }
    for (int32 C = 0; C < Cards.Num(); ++C)
    {
        const FLegacyGlowCard& Card = Cards[C];
        const FName Name = *FString::Printf(TEXT("GlowCardLight_%u_%d_%d"), UniqueId, Index, C);
        UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner, Name);
        if (!Light) continue;
        Light->SetupAttachment(DoodadComponent);
        Owner->AddInstanceComponent(Light);
        Light->SetMobility(EComponentMobility::Movable);
        const float Reach = FMath::Clamp(Card.RadiusCm * 6.f, 300.f, 1000.f);
        Light->SetIntensity(FMath::Clamp(Card.RadiusCm * 40.f, 600.f, 2600.f) * FWowClientPreferences::GetFireLightIntensityScale());
        Light->SetAttenuationRadius(Reach);
        Light->SetLightColor(Card.Color);
        // A soft glow: no shadows, no fog scattering or GI, faded out at a distance (cheap).
        Light->SetCastShadows(false);
        Light->SetVolumetricScatteringIntensity(0.0f);
        Light->SetIndirectLightingIntensity(0.0f);
        Light->SetSourceRadius(FMath::Clamp(Card.RadiusCm * 0.3f, 4.f, 30.f));
        Light->SetSpecularScale(0.3f);
        Light->SetMaxDrawDistance(4000.f);
        Light->MaxDistanceFadeRange = 800.f;
        if (DoodadTransform) Light->SetWorldLocation(DoodadTransform->TransformPosition(Card.LocalUE));
        else Light->SetRelativeLocation(Card.LocalUE);
        Light->RegisterComponent();
    }
}
