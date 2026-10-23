#include "WowCharacterPreview.h"
#include "WowClientPreferences.h"
#include "WowM2ParticleComponent.h"
#include "WowWorldManager.h"
#include "WowCharacterBuilder.h"
#include "WowDeathManager.h"
#include "WowAnimationController.h"
#include "Coord/WowCoordinate.h"
#include "Mpq/MpqManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "SkeletalRenderPublic.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Materials/MaterialInterface.h"
#include "Formats/M2Parser.h"
#include "Formats/Dbc/DbcStore.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowPreview, Log, All);

namespace
{
struct FPreviewM2Header
{
    uint32 Magic; uint32 Version; uint32 NameLength; uint32 NameOffset; uint32 GlobalFlags;
    uint32 nGlobalSequences; uint32 ofsGlobalSequences; uint32 nAnimations; uint32 ofsAnimations;
    uint32 nAnimationLookup; uint32 ofsAnimationLookup; uint32 nBones; uint32 ofsBones;
    uint32 nKeyBoneLookup; uint32 ofsKeyBoneLookup; uint32 nVertices; uint32 ofsVertices;
    uint32 nViews; uint32 nColors; uint32 ofsColors; uint32 nTextures; uint32 ofsTextures;
    uint32 nTransparency; uint32 ofsTransparency; uint32 nTexAnims; uint32 ofsTexAnims;
    uint32 nTexReplace; uint32 ofsTexReplace; uint32 nRenderFlags; uint32 ofsRenderFlags;
    uint32 nBoneLookup; uint32 ofsBoneLookup; uint32 nTexLookup; uint32 ofsTexLookup;
    uint32 nTexUnits; uint32 ofsTexUnits; uint32 nTransLookup; uint32 ofsTransLookup;
    uint32 nTexAnimLookup; uint32 ofsTexAnimLookup;
    float BoundingBox[6]; float BoundingSphereRadius; float CollisionBox[6]; float CollisionSphereRadius;
    uint32 nBoundingTriangles; uint32 ofsBoundingTriangles; uint32 nBoundingVertices; uint32 ofsBoundingVertices;
    uint32 nBoundingNormals; uint32 ofsBoundingNormals; uint32 nAttachments; uint32 ofsAttachments;
    uint32 nAttachmentLookup; uint32 ofsAttachmentLookup; uint32 nEvents; uint32 ofsEvents;
    uint32 nLights; uint32 ofsLights; uint32 nCameras; uint32 ofsCameras;
    uint32 nCameraLookup; uint32 ofsCameraLookup; uint32 nRibbonEmitters; uint32 ofsRibbonEmitters;
    uint32 nParticleEmitters; uint32 ofsParticleEmitters;
};

struct FPreviewM2Camera
{
    uint32 Type;
    float FieldOfViewRadians;
    float FarClip;
    float NearClip;
    uint8 PositionBlock[20];
    float Position[3];
    uint8 TargetBlock[20];
    float Target[3];
    uint8 RollBlock[20];
};

struct FPreviewStaticCamera
{
    FVector Position = FVector::ZeroVector;
    FVector Target = FVector::ZeroVector;
    float FieldOfViewRadians = 0.85f;
    bool bValid = false;
};

static FPreviewStaticCamera ReadStaticCamera(FMpqManager* Mpq, const FString& ModelPath)
{
    FPreviewStaticCamera Result;
    TArray<uint8> Raw;
    if (!Mpq || !Mpq->ReadFile(ModelPath, Raw) || Raw.Num() < static_cast<int32>(sizeof(FPreviewM2Header)))
    {
        return Result;
    }

    const FPreviewM2Header& Header = *reinterpret_cast<const FPreviewM2Header*>(Raw.GetData());
    const uint64 CameraEnd = static_cast<uint64>(Header.ofsCameras)
        + static_cast<uint64>(Header.nCameras) * sizeof(FPreviewM2Camera);
    if (Header.Magic != 0x3032444D || Header.nCameras == 0 || CameraEnd > static_cast<uint64>(Raw.Num()))
    {
        return Result;
    }

    const FPreviewM2Camera& Camera =
        *reinterpret_cast<const FPreviewM2Camera*>(Raw.GetData() + Header.ofsCameras);
    Result.Position = FVector(Camera.Position[1], Camera.Position[0], Camera.Position[2]) * FWowCoordinate::SCALE;
    Result.Target = FVector(Camera.Target[1], Camera.Target[0], Camera.Target[2]) * FWowCoordinate::SCALE;
    Result.FieldOfViewRadians = FMath::Clamp(Camera.FieldOfViewRadians, 0.25f, 1.6f);
    Result.bValid = true;
    return Result;
}

// r385 (Stefan: Dwarf still dark on character select after r359/r379): the original client lights
// the glue character with the race scene's own M2 lights: ambient + diffuse per light, as
// texture * (ambient + diffuse * N.L). UI_Dwarf has a strong cold snow light (diffuse 2.0) and
// ambient 0.3; our fixed rig gave the dwarf about half of that. The rig keeps its flattering
// camera-relative directions but takes strength and colour from the scene's lights, never going
// below the old rig. UE equivalent of a WoW light value v: v * pi / exposure(1/1.2) lux.
struct FPreviewSceneLights
{
    FLinearColor KeyColor = FLinearColor::White;
    float KeyValue = 0.f;      // strongest diffuse light, WoW units (colour max channel folded in)
    float Ambient = 0.f;       // summed ambient, WoW units
    bool bValid = false;
};
// r489 (Stefan: Whiteknight, a dwarf, dark on character select while the gnome in the same scene was
// bright, then every other scene bright): the scene lights were one global shared by every preview.
// Destroying another preview (the in-game paper doll / inspect / dress-up previews go after logout)
// ran its ClearRaceScene and wiped the select screen's lights, so the next character in the same
// scene got the weak neutral fallback (2.6 lux) until a scene change read them again. Each preview
// now keeps its own.
static TMap<const AActor*, FPreviewSceneLights> GWowPreviewSceneLightsByPreview;

static float FirstTrackFloat(const TArray<uint8>& Raw, int64 TrackOffset, int32 Components, float* Out)
{
    // M2Track (WotLK): interp u16, global seq s16, nTimes/ofsTimes, nValues/ofsValues (per sequence).
    if (TrackOffset < 0 || TrackOffset + 20 > Raw.Num()) return 0.f;
    uint32 NumValues = 0, OfsValues = 0;
    FMemory::Memcpy(&NumValues, Raw.GetData() + TrackOffset + 12, 4);
    FMemory::Memcpy(&OfsValues, Raw.GetData() + TrackOffset + 16, 4);
    if (NumValues == 0 || int64(OfsValues) + 8 > Raw.Num()) return 0.f;
    uint32 Count = 0, Data = 0;
    FMemory::Memcpy(&Count, Raw.GetData() + OfsValues, 4);
    FMemory::Memcpy(&Data, Raw.GetData() + OfsValues + 4, 4);
    if (Count == 0 || int64(Data) + 4 * Components > Raw.Num()) return 0.f;
    FMemory::Memcpy(Out, Raw.GetData() + Data, 4 * Components);
    return 1.f;
}

static FPreviewSceneLights ReadSceneLights(FMpqManager* Mpq, const FString& ModelPath)
{
    FPreviewSceneLights Result;
    TArray<uint8> Raw;
    if (!Mpq || !Mpq->ReadFile(ModelPath, Raw) || Raw.Num() < static_cast<int32>(sizeof(FPreviewM2Header))) return Result;
    const FPreviewM2Header& Header = *reinterpret_cast<const FPreviewM2Header*>(Raw.GetData());
    constexpr int64 LightSize = 156; // WotLK M2Light
    if (Header.Magic != 0x3032444D || Header.nLights == 0 || Header.nLights > 16
        || int64(Header.ofsLights) + LightSize * Header.nLights > Raw.Num()) return Result;
    for (uint32 I = 0; I < Header.nLights; ++I)
    {
        const int64 Base = int64(Header.ofsLights) + LightSize * I;
        float AmbColor[3] = {1, 1, 1}, AmbIntensity = 0, DifColor[3] = {1, 1, 1}, DifIntensity = 0;
        FirstTrackFloat(Raw, Base + 16, 3, AmbColor);
        FirstTrackFloat(Raw, Base + 36, 1, &AmbIntensity);
        FirstTrackFloat(Raw, Base + 56, 3, DifColor);
        FirstTrackFloat(Raw, Base + 76, 1, &DifIntensity);
        Result.Ambient += FMath::Clamp(AmbIntensity, 0.f, 4.f) * FMath::Max3(AmbColor[0], AmbColor[1], AmbColor[2]);
        const float Peak = FMath::Max3(DifColor[0], DifColor[1], DifColor[2]);
        const float Value = FMath::Clamp(DifIntensity, 0.f, 4.f) * Peak;
        if (Value > Result.KeyValue && Peak > 0.01f)
        {
            Result.KeyValue = Value;
            Result.KeyColor = FLinearColor(DifColor[0] / Peak, DifColor[1] / Peak, DifColor[2] / Peak);
        }
    }
    Result.bValid = Result.KeyValue > 0.f || Result.Ambient > 0.f;
    UE_LOG(LogWowPreview, Log, TEXT("Scene lights %s: %u lights, key %.2f colour %s, ambient %.2f"), *ModelPath,
        Header.nLights, Result.KeyValue, *Result.KeyColor.ToString(), Result.Ambient);
    return Result;
}

// Only the stock stage uses the small hologram platform at the legacy anchor.
// Replacement MPQs keep the same path but may author an entirely different set.
static bool IsOriginalDraeneiStageCamera(const FPreviewStaticCamera& Camera)
{
    return Camera.bValid
        && Camera.Position.Equals(FVector(1.3808361, 7.2472863, 0.5649618) * FWowCoordinate::SCALE, 0.1)
        && Camera.Target.Equals(FVector(1.4495248, 2.5240128, 0.8884613) * FWowCoordinate::SCALE, 0.1);
}

static FVector TransformCameraPoint(const AActor* SceneActor, const FVector& ConvertedPoint)
{
    const FVector MeshLocalPoint = FRotator(0.0f, -90.0f, 0.0f).RotateVector(ConvertedPoint);
    return SceneActor ? SceneActor->GetTransform().TransformPosition(MeshLocalPoint) : MeshLocalPoint;
}

// Intersect the authored floor itself: a screen-space anchor is not a floor.
// Work in the same swapped/scaled coordinates as the skeletal mesh builder.
static bool FindPreviewGround(AActor* Scene,
    const FVector& RayOrigin, const FVector& RayDirection, FVector& Ground)
{
    if (!Scene) return false;
    double BestDistance = TNumericLimits<double>::Max();
    bool bFound = false;
    TInlineComponentArray<USkeletalMeshComponent*> Meshes(Scene);
    for (USkeletalMeshComponent* Mesh : Meshes)
    {
        if (!Mesh || !Mesh->IsVisible()) continue;
        const FSkeletalMeshRenderData* Render = Mesh->GetSkeletalMeshRenderData();
        if (!Render || Render->LODRenderData.IsEmpty()) continue;
        const FSkeletalMeshLODRenderData& LOD = Render->LODRenderData[0];
        const auto* Indices = LOD.MultiSizeIndexContainer.GetIndexBuffer();
        if (!Indices) continue;
        TArray<FFinalSkinVertex> Vertices;
        Mesh->GetCPUSkinnedVertices(Vertices, 0);
        for (const FSkelMeshRenderSection& Section : LOD.RenderSections)
        {
            UMaterialInterface* Material = Mesh->GetMaterial(Section.MaterialIndex);
            if (!Material || !Mesh->IsMaterialSectionShown(Section.MaterialIndex,0) ||
                (Material->GetBlendMode()!=BLEND_Opaque && Material->GetBlendMode()!=BLEND_Masked &&
                 Material->GetBlendMode()!=BLEND_Translucent)) continue;
            // The original Human street is alpha-blended (M2 blend 2).
            // Rejecting all alpha surfaces selected a wall floor 64 metres away.
            float Opacity=1;
            Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("PassOpacity")),Opacity);
            if (Opacity<=.001f) continue;
            UTexture* Texture=nullptr;
            Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseTexture")),Texture);
            const FString Name=Texture?Texture->GetName():FString();
            if(Name.Contains(TEXT("SHADOW")) || Name.Contains(TEXT("GRADIENT")) ||
                Name.Contains(TEXT("SKY")) || Name.Contains(TEXT("GLOW"))) continue;
            const uint32 End=FMath::Min<uint32>(Section.BaseIndex+Section.NumTriangles*3,Indices->Num());
            for(uint32 I=Section.BaseIndex;I+2<End;I+=3)
            {
                FVector P[3]; bool bValid=true;
                for(int32 C=0;C<3;++C)
                {
                    const uint32 V=Indices->Get(I+C);
                    if(!Vertices.IsValidIndex(V)){bValid=false;break;}
                    P[C]=Mesh->GetComponentTransform().TransformPosition(FVector(Vertices[V].Position));
                }
                if(!bValid)continue;
                const FVector E1=P[1]-P[0],E2=P[2]-P[0];
                if(FMath::Abs(FVector::CrossProduct(E1,E2).GetSafeNormal().Z)<.65)continue;
                const FVector H=FVector::CrossProduct(RayDirection,E2);
                const double Det=FVector::DotProduct(E1,H);
                if(FMath::Abs(Det)<UE_SMALL_NUMBER)continue;
                const FVector S=RayOrigin-P[0],Q=FVector::CrossProduct(S,E1);
                const double U=FVector::DotProduct(S,H)/Det,V=FVector::DotProduct(RayDirection,Q)/Det;
                const double T=FVector::DotProduct(E2,Q)/Det;
                if(U<0 || V<0 || U+V>1 || T<10 || T>=BestDistance)continue;
                BestDistance=T;Ground=RayOrigin+RayDirection*T;bFound=true;
            }
        }
    }
    return bFound;
}

static FString RaceScenePath(uint8 RaceId, uint8 ClassId)
{
    if (ClassId == 6)
    {
        return TEXT("Interface\\Glues\\Models\\UI_DeathKnight\\UI_DeathKnight.m2");
    }

    switch (RaceId)
    {
    case 1: return TEXT("Interface\\Glues\\Models\\UI_Human\\UI_Human.m2");
    case 2: return TEXT("Interface\\Glues\\Models\\UI_Orc\\UI_Orc.m2");
    case 3: return TEXT("Interface\\Glues\\Models\\UI_Dwarf\\UI_Dwarf.m2");
    case 4: return TEXT("Interface\\Glues\\Models\\UI_NightElf\\UI_NightElf.m2");
    case 5: return TEXT("Interface\\Glues\\Models\\UI_Scourge\\UI_Scourge.m2");
    case 6: return TEXT("Interface\\Glues\\Models\\UI_Tauren\\UI_Tauren.m2");
    case 7: return TEXT("Interface\\Glues\\Models\\UI_Dwarf\\UI_Dwarf.m2");
    case 8: return TEXT("Interface\\Glues\\Models\\UI_Orc\\UI_Orc.m2");
    case 10: return TEXT("Interface\\Glues\\Models\\UI_BloodElf\\UI_BloodElf.m2");
    case 11: return TEXT("Interface\\Glues\\Models\\UI_Draenei\\UI_Draenei.m2");
    default: return TEXT("Interface\\Glues\\Models\\UI_CharacterSelect\\UI_CharacterSelect.m2");
    }
}

static float RaceCharacterScale(uint8 RaceId)
{
    switch (RaceId)
    {
    case 3: return 0.60f; // Dwarf
    case 7: return 0.68f; // Gnome
    case 6: return 0.42f; // Tauren
    case 4:
    case 8:
    case 11: return 0.47f; // Tall races
    case 2: return 0.49f; // Broad orc silhouette
    default: return 0.52f;
    }
}
}

// Place preview scene far below the world
const FVector AWowCharacterPreview::PREVIEW_OFFSET = FVector(0.0f, 0.0f, -500000.0f);

AWowCharacterPreview::AWowCharacterPreview()
{
    PrimaryActorTick.bCanEverTick = true;

    // Scene root
    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    // Scene capture
    CaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
    CaptureComponent->SetupAttachment(Root);
    CaptureComponent->CaptureSource = SCS_FinalColorLDR;
    CaptureComponent->bCaptureEveryFrame = false;
    CaptureComponent->bCaptureOnMovement = false;
    CaptureComponent->FOVAngle = 30.0f;
    CaptureComponent->ShowFlags.SetFog(false);
    CaptureComponent->ShowFlags.SetAtmosphere(false);
    CaptureComponent->ShowFlags.SetVolumetricFog(false);

    // Key light (main illumination from front-left)
    KeyLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("KeyLight"));
    KeyLight->SetupAttachment(Root);
    KeyLight->SetRelativeRotation(FRotator(-35.0f, -30.0f, 0.0f));
    KeyLight->Intensity = 1.6f;
    KeyLight->LightColor = FColor(255, 248, 240);
    KeyLight->SetForwardShadingPriority(0);
    // r410 (Stefan: the sky got brighter with the character sheet open, and stayed so after Esc):
    // UE makes every directional light an atmosphere sun light by default, so this studio key light
    // lit the world's sky. It only lights the preview model (lighting channel 1).
    KeyLight->bAtmosphereSunLight = false;
	KeyLight->LightingChannels.bChannel0 = false;
	KeyLight->LightingChannels.bChannel1 = true;

    // Fill light (softer, from front-right)
    FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
    FillLight->SetupAttachment(Root);
    FillLight->SetRelativeLocation(FVector(-200.0f, 150.0f, 120.0f));
    FillLight->Intensity = 1800.0f;
    FillLight->AttenuationRadius = 1000.0f;
    FillLight->LightColor = FColor(240, 240, 255);
	FillLight->LightingChannels.bChannel0 = false;
	FillLight->LightingChannels.bChannel1 = true;

    // Rim light (from behind for character silhouette)
    RimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
    RimLight->SetupAttachment(Root);
    RimLight->SetRelativeLocation(FVector(200.0f, 0.0f, 150.0f));
    RimLight->Intensity = 2200.0f;
    RimLight->AttenuationRadius = 800.0f;
    RimLight->LightColor = FColor(200, 220, 255);
	RimLight->LightingChannels.bChannel0 = false;
	RimLight->LightingChannels.bChannel1 = true;
}

void AWowCharacterPreview::BeginPlay()
{
    Super::BeginPlay();
}

void AWowCharacterPreview::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearCharacter();
    ClearRaceScene();
    Super::EndPlay(EndPlayReason);
}

void AWowCharacterPreview::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // r301: a closed preview's lights go dark. Directional lights reach every model on their
    // channel, so an idle rig would light another open preview on the same channel twice.
    const bool bLightsOn = bPreviewActive && (CurrentCharacterActor || CurrentRaceSceneActor);
    if (KeyLight && KeyLight->IsVisible() != bLightsOn)
    {
        KeyLight->SetVisibility(bLightsOn);
        if (FillLight) FillLight->SetVisibility(bLightsOn);
        if (RimLight) RimLight->SetVisibility(bLightsOn);
    }
    // Character select / creation follow the HD option live, like the world does.
    if (bHasShownCharacterInfo && CurrentCharacterActor
        && FWowClientPreferences::GetWorldOption(TEXT("hdcharacters")) != bShownWithHDOption)
    {
        const FWowCharacterInfo Info = LastShownCharacterInfo;
        ShowCharacter(Info);
    }
    if ((CurrentCharacterActor || CurrentRaceSceneActor) && bPreviewActive)
    {
        CaptureAccumulator += DeltaSeconds;
        // Both previews run at 60: at 15 the paper doll's idle and weapon enchants
        // (Rockbiter) visibly stuttered. The capture is a show-only list with no
        // Lumen/reflections, so the cost is small.
        const float CaptureInterval = 1.0f / 60.0f;
        if (CaptureAccumulator >= CaptureInterval)
        {
            CaptureAccumulator = FMath::Fmod(CaptureAccumulator, CaptureInterval);
            CaptureNow();
        }
    }
}

void AWowCharacterPreview::Setup(AWowWorldManager* InWorldManager, int32 Width, int32 Height,
    EWowCharacterPreviewMode InMode)
{
    WorldManager = InWorldManager;
    PreviewMode = InMode;

    // Create render target
    RenderTarget = NewObject<UTextureRenderTarget2D>(this);
    // Slate consumes a linear texture and applies display gamma itself.
    RenderTarget->RenderTargetFormat = RTF_RGBA16f;
    RenderTarget->TargetGamma = 1.0f;
    RenderTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
    RenderTarget->InitAutoFormat(Width, Height);
    RenderTarget->UpdateResourceImmediate(true);

    CaptureComponent->TextureTarget = RenderTarget;
    CaptureComponent->ClearShowOnlyComponents();
    CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    // This isolated, directly lit rig does not need another Lumen view of the world.
    CaptureComponent->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
    CaptureComponent->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
    CaptureComponent->PostProcessSettings.bOverride_ReflectionMethod = true;
    CaptureComponent->PostProcessSettings.ReflectionMethod = EReflectionMethod::None;
    CaptureComponent->ShowFlags.SetMotionBlur(false);
    CaptureComponent->ShowFlags.SetDepthOfField(false);

    if (PreviewMode != EWowCharacterPreviewMode::PaperDoll)
    {
        // The glue backdrops contain painted lighting. Preserve those colours
        // through the linear Slate target instead of applying the world's film
        // curve, colour grading, bloom and adaptive exposure to them again.
        CaptureComponent->ShowFlags.SetTonemapper(false);
        CaptureComponent->ShowFlags.SetColorGrading(false);
        CaptureComponent->ShowFlags.SetBloom(false);
        CaptureComponent->ShowFlags.SetEyeAdaptation(false);
        CaptureComponent->ShowFlags.SetFog(false);
        CaptureComponent->ShowFlags.SetAtmosphere(false);
        CaptureComponent->ShowFlags.SetSkyLighting(false);
        CaptureComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
        CaptureComponent->PostProcessSettings.AutoExposureMethod = AEM_Manual;
        CaptureComponent->PostProcessSettings.bOverride_AutoExposureBias = true;
        CaptureComponent->PostProcessSettings.AutoExposureBias = 0.0f;
        CaptureComponent->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
        CaptureComponent->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
    }

    // Position the entire preview rig far below world
    SetActorLocation(RigOffset);

    // Default camera position (will be adjusted per-race)
    CaptureComponent->SetRelativeLocation(FVector(-450.0f, 0.0f, 90.0f));
    CaptureComponent->SetRelativeRotation(FRotator(-5.0f, 0.0f, 0.0f));

    UE_LOG(LogWowPreview, Log, TEXT("Character preview initialized (%dx%d, mode=%s)"), Width, Height,
        PreviewMode == EWowCharacterPreviewMode::PaperDoll ? TEXT("PaperDoll") : TEXT("CharacterSelection"));
}

void AWowCharacterPreview::SetRig(const FVector& Offset, int32 LightingChannel)
{
    RigOffset = Offset;
    CharacterAnchor = Offset;
    RigLightingChannel = LightingChannel == 2 ? 2 : 1;
    for (ULightComponent* Light : { static_cast<ULightComponent*>(KeyLight.Get()), static_cast<ULightComponent*>(FillLight.Get()), static_cast<ULightComponent*>(RimLight.Get()) })
        if (Light)
        {
            Light->LightingChannels.bChannel0 = false;
            Light->LightingChannels.bChannel1 = RigLightingChannel == 1;
            Light->LightingChannels.bChannel2 = RigLightingChannel == 2;
            Light->MarkRenderStateDirty();
        }
}

void AWowCharacterPreview::ApplyRigLighting(AActor* Actor) const
{
    if (!Actor) return;
    TArray<UPrimitiveComponent*> Primitives;
    Actor->GetComponents(Primitives, true);
    for (UPrimitiveComponent* Primitive : Primitives)
        if (Primitive) Primitive->SetLightingChannels(false, RigLightingChannel == 1, RigLightingChannel == 2);
}

void AWowCharacterPreview::ShowCreatureDisplay(uint32 CreatureDisplayId)
{
    bHasShownCharacterInfo = false; // nothing to rebuild on the HD option
    ClearCharacter();
    ClearRaceScene();
    if (!WorldManager || !WorldManager->GetMpqManager() || !WorldManager->GetAssetCache() || !CaptureComponent) return;
    // Three-quarter view so a mount's length reads.
    const FRotator Facing(0.0f, 145.0f, 0.0f);
    CurrentCharacterActor = FWowCharacterBuilder::SpawnCreatureByDisplayId(GetWorld(),
        WorldManager->GetMpqManager(), WorldManager->GetAssetCache(), CreatureDisplayId, RigOffset, Facing);
    if (!CurrentCharacterActor)
    {
        UE_LOG(LogWowPreview, Warning, TEXT("Dressing room: could not spawn creature display %u"), CreatureDisplayId);
        return;
    }
    CurrentCharacterActor->SetActorLocationAndRotation(RigOffset, Facing);
    if (UWowAnimationController* Animation = FWowCharacterBuilder::GetAnimationController(CurrentCharacterActor))
        Animation->PlayAnimationById(EWowAnimId::Stand, true);
    ApplyRigLighting(CurrentCharacterActor);
    TArray<UWowM2ParticleComponent*> Effects;
    CurrentCharacterActor->GetComponents<UWowM2ParticleComponent>(Effects);
    for (UWowM2ParticleComponent* Effect : Effects) Effect->SetBillboardView(CaptureComponent);
    CaptureComponent->CaptureSource = SCS_SceneColorHDR;
    RenderTarget->ClearColor = FLinearColor(0, 0, 0, 1);
    CaptureComponent->ClearShowOnlyComponents();
    CaptureComponent->ShowOnlyActorComponents(CurrentCharacterActor, true);

    // Frame the whole model (the race framing is for characters).
    FVector Origin, Extent;
    CurrentCharacterActor->GetActorBounds(false, Origin, Extent, true);
    const float Radius = FMath::Clamp(float(Extent.Size()), 60.0f, 2500.0f);
    const float HalfFov = FMath::DegreesToRadians(CaptureComponent->FOVAngle * 0.5f);
    const float Aspect = RenderTarget && RenderTarget->SizeY > 0 ? float(RenderTarget->SizeX) / float(RenderTarget->SizeY) : 1.0f;
    const float VerticalHalfFov = FMath::Atan(FMath::Tan(HalfFov) / FMath::Max(Aspect, 0.1f));
    const float Distance = Radius * 1.05f / FMath::Tan(FMath::Min(HalfFov, VerticalHalfFov));
    const FVector LocalCenter = GetActorTransform().InverseTransformPosition(Origin);
    const FVector CameraLocation = LocalCenter + FVector(-Distance, 0.0f, Radius * 0.25f);
    CaptureComponent->SetRelativeLocation(CameraLocation);
    CaptureComponent->SetRelativeRotation((LocalCenter - CameraLocation).Rotation());
    bPreviewActive = true;
    CaptureAccumulator = 0.0f;
    CaptureNow();
    UE_LOG(LogWowPreview, Log, TEXT("Dressing room: creature display %u (radius %.0f)"), CreatureDisplayId, Radius);
}

void AWowCharacterPreview::ShowEmptySelectionScene()
{
    ClearCharacter();
    if (PreviewMode != EWowCharacterPreviewMode::CharacterSelection) return;
    // With no selected race, use the same Human stage as New Character.
    if (ShowRaceScene(1, 1))
    {
        bPreviewActive = true;
        CaptureComponent->SetWorldLocation(AuthoredCameraLocation);
        CaptureComponent->ShowOnlyActorComponents(CurrentRaceSceneActor, true);
        CaptureNow();
        UE_LOG(LogWowPreview, Log, TEXT("Empty character selection: animated Human backdrop, no character"));
    }
}

void AWowCharacterPreview::ShowCharacter(uint8 RaceId, uint8 GenderId)
{
    FWowCharacterInfo CharacterInfo;
    CharacterInfo.Race = RaceId;
    CharacterInfo.Gender = GenderId;
    ShowCharacter(CharacterInfo);
}

void AWowCharacterPreview::ShowCharacter(const FWowCharacterInfo& CharacterInfo)
{
    LastShownCharacterInfo = CharacterInfo;
    bHasShownCharacterInfo = true;
    bShownWithHDOption = FWowClientPreferences::GetWorldOption(TEXT("hdcharacters"));
    ClearCharacter();

    if (!WorldManager || !WorldManager->GetMpqManager() || !WorldManager->GetAssetCache())
    {
        UE_LOG(LogWowPreview, Warning, TEXT("Cannot show character preview — no MPQ/AssetCache"));
        return;
    }

    const uint8 RaceId = CharacterInfo.Race > 0 ? CharacterInfo.Race : 1;
    const uint8 GenderId = CharacterInfo.Gender;
    // Character Sheet must never inherit the full-screen racial environment used
    // by character selection. It gets its own isolated, character-only capture.
    const bool bHasRaceScene = PreviewMode != EWowCharacterPreviewMode::PaperDoll
        && ShowRaceScene(RaceId, CharacterInfo.Class);
    if (PreviewMode == EWowCharacterPreviewMode::PaperDoll)
    {
        ClearRaceScene();
        // SceneColor stores inverse coverage in A. FinalColorLDR discards it.
        CaptureComponent->CaptureSource = SCS_SceneColorHDR;
        CaptureComponent->ShowFlags.SetBloom(false);
        CaptureComponent->ShowFlags.SetEyeAdaptation(false);
        CaptureComponent->ShowFlags.SetDepthOfField(false);
        RenderTarget->ClearColor = FLinearColor(0,0,0,1);
    }
    FWowCharacterBuilder::FCharacterParams Params;
    Params.Race = static_cast<FWowCharacterBuilder::ERace>(RaceId);
    Params.Gender = static_cast<FWowCharacterBuilder::EGender>(GenderId);
    Params.Customization.RaceId = RaceId;
    Params.Customization.Gender = GenderId;
    Params.Customization.SkinColor = CharacterInfo.Skin;
    Params.Customization.FaceVariation = CharacterInfo.Face;
    Params.Customization.HairStyle = CharacterInfo.HairStyle;
    Params.Customization.HairColor = CharacterInfo.HairColor;
    Params.Customization.FacialHairStyle = CharacterInfo.FacialHair;
    Params.BodyEquipment.RaceId = RaceId;
    Params.BodyEquipment.Gender = GenderId;

    // r490 (Stefan: the troll hunter on the create screen held his bow AND his axe in one hand, "I
    // think it should JUST be the bow"): the glue screens hid ranged weapons by a tag the preview never
    // set, so the bow joined the melee weapon in the right hand. A character with a bow, crossbow or
    // gun now shows only that, as hunters stand in the original: a bow in the left hand, a gun or
    // crossbow in the right. Wands and thrown weapons stay hidden; the character sheet is unchanged.
    enum class EGlueRanged : uint8 { None, Bow, Gun };
    auto GlueRanged = [](const FWowCharacterEquipment& Item)
    {
        if (Item.InventoryType != 15 && Item.InventoryType != 26) return EGlueRanged::None;
        const FItemDisplayInfoDbcEntry* Display = FDbcStore::Get().ItemDisplayInfo().GetById(Item.DisplayId);
        const FString Model = Display ? Display->ModelNames[0].ToLower() : FString();
        if (Model.Contains(TEXT("crossbow")) || Model.StartsWith(TEXT("firearm")) || Model.Contains(TEXT("_gun"))) return EGlueRanged::Gun;
        if (Model.StartsWith(TEXT("bow"))) return EGlueRanged::Bow;
        return EGlueRanged::None;
    };
    const FWowCharacterEquipment* GlueRangedItem = nullptr;
    if (PreviewMode != EWowCharacterPreviewMode::PaperDoll)
        for (const FWowCharacterEquipment& Equipment : CharacterInfo.Equipment)
            if (Equipment.DisplayId > 0 && GlueRanged(Equipment) != EGlueRanged::None) { GlueRangedItem = &Equipment; break; }

    for (const FWowCharacterEquipment& Equipment : CharacterInfo.Equipment)
    {
        if (Equipment.DisplayId <= 0)
        {
            continue;
        }
        if (GlueRangedItem)
        {
            const uint8 Type = Equipment.InventoryType;
            const bool bHandItem = Type == 13 || Type == 14 || Type == 15 || Type == 17 || Type == 21 || Type == 22
                || Type == 23 || Type == 25 || Type == 26;
            if (bHandItem && &Equipment != GlueRangedItem) continue;
            if (&Equipment == GlueRangedItem)
            {
                FWowCharacterBuilder::FEquipmentSlot Slot;
                Slot.ItemDisplayId = Equipment.DisplayId;
                Slot.InventoryType = Equipment.InventoryType;
                Slot.AttachPoint = GlueRanged(Equipment) == EGlueRanged::Bow
                    ? FWowEquipmentManager::EAttachmentPoint::LeftHand : FWowEquipmentManager::EAttachmentPoint::RightHand;
                Params.Equipment.Add(Slot);
                continue;
            }
        }

		switch (Equipment.InventoryType)
		{
		case 1:
		{
			FWowCharacterBuilder::FEquipmentSlot Slot;
			Slot.ItemDisplayId = Equipment.DisplayId;
			Slot.InventoryType = Equipment.InventoryType;
			Slot.AttachPoint = FWowEquipmentManager::EAttachmentPoint::Helmet;
			Params.Equipment.Add(Slot);
			break;
		}
		case 3:
		{
			FWowCharacterBuilder::FEquipmentSlot LeftSlot;
			LeftSlot.ItemDisplayId = Equipment.DisplayId;
			LeftSlot.InventoryType = Equipment.InventoryType;
			LeftSlot.AttachPoint = FWowEquipmentManager::EAttachmentPoint::LeftShoulder;
			Params.Equipment.Add(LeftSlot);
			FWowCharacterBuilder::FEquipmentSlot RightSlot = LeftSlot;
			RightSlot.AttachPoint = FWowEquipmentManager::EAttachmentPoint::RightShoulder;
			Params.Equipment.Add(RightSlot);
			break;
		}
        case 4:
            Params.BodyEquipment.ShirtDisplayId = Equipment.DisplayId;
            break;
        case 5:
            Params.BodyEquipment.ChestDisplayId = Equipment.DisplayId;
            Params.BodyEquipment.bIsRobeEquipped = false;
            break;
        case 20:
            Params.BodyEquipment.ChestDisplayId = Equipment.DisplayId;
            Params.BodyEquipment.bIsRobeEquipped = true;
            break;
        case 6:
            Params.BodyEquipment.BeltDisplayId = Equipment.DisplayId;
            break;
        case 7:
            Params.BodyEquipment.PantsDisplayId = Equipment.DisplayId;
            break;
        case 8:
            Params.BodyEquipment.BootsDisplayId = Equipment.DisplayId;
            break;
        case 9:
            Params.BodyEquipment.BracersDisplayId = Equipment.DisplayId;
            break;
		case 10:
			Params.BodyEquipment.GlovesDisplayId = Equipment.DisplayId;
			break;
		case 16:
			Params.BodyEquipment.CapeDisplayId = Equipment.DisplayId;
			break;
        case 19:
            Params.BodyEquipment.TabardDisplayId = Equipment.DisplayId;
            break;
        case 13:
        case 14:
        case 15:
        case 17:
        case 21:
        case 22:
        case 23:
        case 26:
        {
            FWowCharacterBuilder::FEquipmentSlot Slot;
            Slot.ItemDisplayId = Equipment.DisplayId;
            // r226 (Stefan 11.10): no weapon enchant effects on character select/create. A Rockbiter
            // (Shaman_Rock.m2) orbit around the sheathed weapon, scaled up with the stage fit, swirled
            // stones and dust far from the weapon. The character sheet (paper doll) keeps them.
            Slot.EnchantmentVisualId = PreviewMode == EWowCharacterPreviewMode::PaperDoll ? Equipment.EnchantAuraId : 0;
            Slot.InventoryType = Equipment.InventoryType;
            // r510 (Stefan: Groklord holds only one of his two daggers on character select): the
            // character list sends the item's own inventory type, and a dagger is "one-hand" (13) in
            // either hand, so the off-hand dagger also went to the right hand. The list is in
            // equipment-slot order: slot 16 is the off hand.
            const int32 EquipmentSlotIndex = int32(&Equipment - CharacterInfo.Equipment.GetData());
            const bool bOffHandSlot = EquipmentSlotIndex == 16
                && (Equipment.InventoryType == 13 || Equipment.InventoryType == 21 || Equipment.InventoryType == 22);
            Slot.AttachPoint = Equipment.InventoryType == 14
                ? FWowEquipmentManager::EAttachmentPoint::ShieldHand
                : ((bOffHandSlot || Equipment.InventoryType == 22 || Equipment.InventoryType == 23)
                    ? FWowEquipmentManager::EAttachmentPoint::LeftHand
                    : FWowEquipmentManager::EAttachmentPoint::RightHand);
            Params.Equipment.Add(Slot);
            break;
        }
        default:
            break;
        }
    }

    // Same HD option as in the world (character select and creation show what you will play).
    Params.bUseHDModel = bAllowHDModel && FWowClientPreferences::GetWorldOption(TEXT("hdcharacters"));

    // Spawn character at the preview location, facing the camera
    const FVector CharPos = bHasRaceScene ? CharacterAnchor : RigOffset;
    const FRotator CharRot(0.0f, bHasRaceScene ? CharacterFacingYaw : 180.0f, 0.0f);

    CurrentCharacterActor = FWowCharacterBuilder::SpawnCharacterWithEquipment(
        GetWorld(), WorldManager->GetMpqManager(), WorldManager->GetAssetCache(),
        Params, CharPos, CharRot);
    if (!CurrentCharacterActor)
    {
        CurrentCharacterActor = FWowCharacterBuilder::SpawnCharacter(
            GetWorld(), WorldManager->GetMpqManager(), WorldManager->GetAssetCache(),
            Params.Race, Params.Gender, CharPos, CharRot);
    }

    if (CurrentCharacterActor)
    {
        if (PreviewMode != EWowCharacterPreviewMode::PaperDoll)
        {
            // Character selection displays equipped melee weapons in their authored hand grip.
            // SpawnCharacterWithEquipment already applied the drawn attachment and rotation.
            TInlineComponentArray<UStaticMeshComponent*> Equipment(CurrentCharacterActor);
            for (UStaticMeshComponent* Item : Equipment)
                if (Item->ComponentHasTag(TEXT("WowRangedWeapon"))) Item->SetVisibility(false, true);
        }
        bPreviewActive = true;
        CaptureAccumulator = 0.0f;
        // SpawnM2Actor creates its root component after spawning the bare actor,
        // so reapply the preview transform once that root exists.
        CurrentCharacterActor->SetActorLocationAndRotation(CharPos, CharRot);
        CurrentCharacterActor->SetActorScale3D(FVector(bHasRaceScene ? CharacterSceneScale : 1.0f));
        bGroundingPending = bHasRaceScene;
        if (bHasRaceScene) CaptureComponent->SetWorldLocation(AuthoredCameraLocation);

        // Both preview modes use the rig's isolated lights. Use the setter on
        // registered meshes so their render proxies receive the channel change.
        TArray<UPrimitiveComponent*> PreviewPrimitives;
        CurrentCharacterActor->GetComponents(PreviewPrimitives, true);
        for (UPrimitiveComponent* Primitive : PreviewPrimitives)
        {
            if (Primitive) Primitive->SetLightingChannels(false, RigLightingChannel == 1, RigLightingChannel == 2);
        }

        // r413 (Stefan: death was not shown in the character list): a character that logged out as
        // a ghost (SMSG_CHAR_ENUM flag 0x2000) stands in character selection with the spirit glow,
        // as in the original. Not in the paper doll: it reuses the list's info while alive.
        if (PreviewMode != EWowCharacterPreviewMode::PaperDoll && (CharacterInfo.CharacterFlags & 0x2000))
            if (UMaterialInterface* Ghost = UWowDeathManager::GetGhostOverlayMaterial())
            {
                TArray<USkeletalMeshComponent*> Bodies;
                CurrentCharacterActor->GetComponents<USkeletalMeshComponent>(Bodies, true);
                for (USkeletalMeshComponent* Body : Bodies)
                    if (Body && Body->GetSkeletalMeshAsset()) Body->SetOverlayMaterial(Ghost);
                UE_LOG(LogWowPreview, Log, TEXT("Character preview: %s is a ghost"), *CharacterInfo.Name);
            }

        TArray<UWowM2ParticleComponent*> EquipmentEffects;
        CurrentCharacterActor->GetComponents<UWowM2ParticleComponent>(EquipmentEffects);
        for (UWowM2ParticleComponent* Effect : EquipmentEffects)
            Effect->SetBillboardView(CaptureComponent);

        CaptureComponent->ClearShowOnlyComponents();
        CaptureComponent->ShowOnlyActorComponents(CurrentCharacterActor, true);
        if (CurrentRaceSceneActor)
            CaptureComponent->ShowOnlyActorComponents(CurrentRaceSceneActor, true);

        // The original 3.3.5 race environment owns the character-selection
        // camera. Use the old portrait framing only when the MPQ scene is absent.
        if (!bHasRaceScene)
        {
            PositionCamera(RaceId);
        }

        if (PreviewMode != EWowCharacterPreviewMode::PaperDoll)
        {
            UpdateCharacterLighting();
        }

        // Capture with a short delay to let shaders compile
        // Weak lambdas (r255, Claude-Perf): logging out and back to character selection crashed
        // when these fired on a preview (or character) that had already been destroyed.
        FTimerHandle CaptureTimer;
        GetWorldTimerManager().SetTimer(CaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            CaptureNow();
            // Capture again after a moment for shader compilation
            FTimerHandle SecondCapture;
            GetWorldTimerManager().SetTimer(SecondCapture, FTimerDelegate::CreateWeakLambda(this, [this]()
            {
                CaptureNow();
            }), 1.0f, false);
        }), 0.2f, false);

        UE_LOG(LogWowPreview, Log, TEXT("Spawned preview character: Race=%d Gender=%d"), RaceId, GenderId);
    }
}

bool AWowCharacterPreview::ShowRaceScene(uint8 RaceId, uint8 ClassId)
{
    if (!WorldManager || !WorldManager->GetMpqManager() || !WorldManager->GetAssetCache())
    {
        return false;
    }

    const FString ScenePath = RaceScenePath(RaceId, ClassId);
    if (CurrentRaceSceneActor && CurrentRaceScenePath.Equals(ScenePath, ESearchCase::IgnoreCase))
    {
        return true;
    }

    ClearRaceScene();
    FMpqManager* Mpq = WorldManager->GetMpqManager();
    if (!Mpq->FileExists(ScenePath))
    {
        UE_LOG(LogWowPreview, Warning, TEXT("Character-select race scene missing: %s"), *ScenePath);
        return false;
    }

    CurrentRaceSceneActor = FWowCharacterBuilder::SpawnM2Model(
        GetWorld(), Mpq, WorldManager->GetAssetCache(), ScenePath,
        RigOffset, FRotator(0.0f, 180.0f, 0.0f), 1.0f);
    if (!CurrentRaceSceneActor)
    {
        UE_LOG(LogWowPreview, Warning, TEXT("Could not spawn character-select race scene: %s"), *ScenePath);
        return false;
    }

    CurrentRaceSceneActor->SetActorLocationAndRotation(RigOffset, FRotator(0.0f, 180.0f, 0.0f));
    CurrentRaceScenePath = ScenePath;
    GWowPreviewSceneLightsByPreview.Add(this, ReadSceneLights(Mpq, ScenePath)); // r385, r489: per preview
    TArray<UPrimitiveComponent*> ScenePrimitives;
    CurrentRaceSceneActor->GetComponents(ScenePrimitives, true);
    for (UPrimitiveComponent* Primitive : ScenePrimitives)
    {
        Primitive->SetLightingChannels(false, RigLightingChannel == 1, RigLightingChannel == 2);
        // The old set contains enclosing sky cards and baked shadow layers.
        // Receive the character's dynamic shadow without those cards occluding the key.
        Primitive->SetCastShadow(false);
        if (USkeletalMeshComponent* SceneMesh = Cast<USkeletalMeshComponent>(Primitive))
            SceneMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    }

    const FPreviewStaticCamera Camera = ReadStaticCamera(Mpq, ScenePath);
    if (!Camera.bValid)
    {
        UE_LOG(LogWowPreview, Warning, TEXT("Race scene has no readable static camera: %s"), *ScenePath);
        return false;
    }

    const FVector CameraLocation = TransformCameraPoint(CurrentRaceSceneActor, Camera.Position);
    const FVector CameraTarget = TransformCameraPoint(CurrentRaceSceneActor, Camera.Target);
    const FVector ViewDirection = (CameraTarget - CameraLocation).GetSafeNormal();
    const FRotator ViewRotation = ViewDirection.Rotation();
    const FVector CameraUp = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Z);
    const float CameraDistance = FVector::Distance(CameraLocation, CameraTarget);

    // Keep the selected asset's camera framing, including replacement scenes.
    const bool bStockHumanCamera = ScenePath.Equals(RaceScenePath(1,1),ESearchCase::IgnoreCase)
        && Camera.Position.Equals(FVector(-81.885597,-221.982773,-2.298278)*FWowCoordinate::SCALE,.1);
    const float SceneFramingScale = bStockHumanCamera ? .76f : 1.0f;

    // SetBackgroundModel in the original client renders the player in front
    // of the embedded race-scene camera. The scene camera target itself is the
    // middle of the backdrop, so anchoring the player there leaves them small
    // and apparently standing on distant scenery. Compose them in the lower
    // centre foreground, matching the 3.3.5 ModelFFX character stage.
    CharacterAnchor = CameraTarget
        - CameraUp * (CameraDistance * 0.078f * SceneFramingScale)
        - ViewDirection * (CameraDistance * 0.72f);
    CharacterFacingYaw = (CameraLocation - CameraTarget).Rotation().Yaw;
    // The embedded ModelFFX cameras are not authored at one common distance:
    // Human/Blood Elf/Dwarf sit roughly 830-900 cm from their target, while
    // Orc/Night Elf/Tauren/Draenei are only about 475-550 cm away. A fixed
    // race scale therefore crops the latter group at the head and shoulders.
    // Preserve the race-height correction, but normalize it to the camera's
    // authored distance (Human is the reference at about 890 cm).
    constexpr float ReferenceCameraDistance = 890.0f;
    const float CameraDistanceScale = FMath::Clamp(
        CameraDistance / ReferenceCameraDistance, 0.45f, 1.15f);
    CharacterSceneScale = RaceCharacterScale(RaceId) * CameraDistanceScale * SceneFramingScale;
    bUsesOriginalDraeneiPlatform = ScenePath.Equals(RaceScenePath(11, 1), ESearchCase::IgnoreCase)
        && IsOriginalDraeneiStageCamera(Camera);
    if (bUsesOriginalDraeneiPlatform)
    {
        // Centre of UI_Draenei's small HOLOGRAM_PLATFORM / PLATFORM_CRYSTAL
        // submeshes, measured in authored M2 coordinates (not the distant stage).
        CharacterAnchor = TransformCameraPoint(CurrentRaceSceneActor,
            FVector(1.443f, 2.177f, -.08f) * FWowCoordinate::SCALE);
        // The former foreground offset placed the model at only 28% of camera
        // depth. Preserve its screen size when standing on the actual platform.
        const float OldDepth = CameraDistance * .28f;
        const float PlatformDepth = FVector::DotProduct(CharacterAnchor-CameraLocation, ViewDirection);
        CharacterSceneScale *= FMath::Clamp(PlatformDepth/FMath::Max(OldDepth,1.0f),1.0f,4.0f);
    }
    if (ScenePath.Equals(RaceScenePath(11, 1), ESearchCase::IgnoreCase) && !bUsesOriginalDraeneiPlatform)
    {
        // A replacement may have a narrower FOV than the original scene.
        // Reserve the bottom 15% for the name/buttons, at any capture aspect.
        const float Aspect = RenderTarget ? float(RenderTarget->SizeX) / FMath::Max(RenderTarget->SizeY, 1) : 16.0f / 9.0f;
        const float Depth = CameraDistance * 0.28f;
        const float HalfHeight = Depth * FMath::Tan(Camera.FieldOfViewRadians * 0.5f) / Aspect;
        CharacterAnchor = CameraLocation + ViewDirection * Depth - CameraUp * HalfHeight * 0.70f;
    }
    // Creation reserves the lower centre for name/accept controls. Keep the
    // full figure above them while retaining the racial camera and scale.
    if (PreviewMode == EWowCharacterPreviewMode::CharacterCreation && !bUsesOriginalDraeneiPlatform)
        CharacterAnchor += CameraUp * (CameraDistance * 0.012f);
    AuthoredCameraLocation = CameraLocation;
    CaptureComponent->SetWorldLocationAndRotation(
        CameraLocation, (CameraTarget - CameraLocation).Rotation());
    // The M2 camera's FOV is diagonal; the client derives the frustum from it, so the horizontal
    // FOV at the capture's aspect is 2*atan(tan(fov/2) * aspect / sqrt(1 + aspect^2)) - about 58
    // instead of 65 degrees at 16:9. Using it as horizontal showed the scene past its sky cards
    // (Stefan 10.10: Tauren create/select corners). The stock Human camera keeps its tuned framing.
    // r390 (Stefan's OG character-select screenshots, Dun Morogh and Durotar, feature-matched):
    // our scene still showed 1.09x too much. The original divides the angle itself, not its
    // tangent: vertical FOV = fov / sqrt(1 + aspect^2) (wowdev's M2 camera rule), so 31.9 deg
    // vertical (53.8 horizontal at 16:9) for the 65 deg race cameras instead of 58.1.
    const float SceneAspect = RenderTarget ? float(RenderTarget->SizeX) / FMath::Max(float(RenderTarget->SizeY), 1.0f) : 16.0f / 9.0f;
    float TanHalfHorizontal = FMath::Tan(Camera.FieldOfViewRadians * 0.5f); // stock Human keeps its tuned framing
    if (!bStockHumanCamera)
    {
        const float Vertical = Camera.FieldOfViewRadians / FMath::Sqrt(1.0f + SceneAspect * SceneAspect);
        TanHalfHorizontal = FMath::Tan(Vertical * 0.5f) * SceneAspect;
    }
    CaptureComponent->FOVAngle = FMath::RadiansToDegrees(2.0f * FMath::Atan(TanHalfHorizontal * SceneFramingScale));

    UE_LOG(LogWowPreview, Log,
        TEXT("Using MPQ race scene %s (camera=%s target=%s distance=%.1f fov=%.1f characterScale=%.3f)"),
        *ScenePath, *CameraLocation.ToCompactString(), *CameraTarget.ToCompactString(),
        CameraDistance, CaptureComponent->FOVAngle, CharacterSceneScale);
    return true;
}

// Fit measured body/equipment geometry into the central safe area of any
// race camera. The old per-race constants are only an initial placement.
static bool FitPreviewBounds(const FBox& Bounds,const FTransform& Camera,double Fov,double Aspect,
    double& Scale,FVector& Centre)
{
    if(!Bounds.IsValid || Bounds.GetSize().Z<1 || Aspect<=0)return false;
    const double TanX=FMath::Tan(FMath::DegreesToRadians(Fov*.5));
    const double TanY=TanX/Aspect;
    if(TanY<=0)return false;
    const FVector OldCentre=Bounds.GetCenter();
    const double Depth=Camera.InverseTransformPosition(OldCentre).X;
    if(!FMath::IsFinite(Depth) || Depth<1)return false;
    Centre=Camera.TransformPosition(FVector(Depth,0,Depth*TanY*.04));
    auto Fits=[&](double Ratio)
    {
        for(int32 I=0;I<8;++I)
        {
            const FVector Corner((I&1)?Bounds.Max.X:Bounds.Min.X,(I&2)?Bounds.Max.Y:Bounds.Min.Y,(I&4)?Bounds.Max.Z:Bounds.Min.Z);
            const FVector P=Camera.InverseTransformPosition(Centre+(Corner-OldCentre)*Ratio);
            if(P.X<=1 || FMath::Abs(P.Y/(P.X*TanX))>.50 || FMath::Abs(P.Z/(P.X*TanY)-.04)>.67)return false;
        }
        return true;
    };
    double Low=0,High=16;
    for(int32 I=0;I<24;++I){const double Mid=(Low+High)*.5;if(Fits(Mid))Low=Mid;else High=Mid;}
    Scale=Low;return Scale>0;
}

void AWowCharacterPreview::GroundCharacter()
{
    bGroundingPending = false;
    if (!IsValid(CurrentCharacterActor) || !IsValid(CurrentRaceSceneActor) || !IsValid(WorldManager)) return;
    USkeletalMeshComponent* Body = CurrentCharacterActor->FindComponentByClass<USkeletalMeshComponent>();
    if (!Body || !Body->GetSkeletalMeshAsset()) return;
    // Evaluate once after the first animation update. Imported bounds include
    // unused geosets and are not a reliable boot-sole measurement.
    TArray<FFinalSkinVertex> Vertices;
    Body->GetCPUSkinnedVertices(Vertices, 0);
    if (Vertices.IsEmpty()) return;

    FBox VisibleBounds(ForceInit);
    // r460 (Stefan: "tiny scale on characters in the character creator"): the original shows the model
    // the same way on the select and the create screen (same race scene, same camera), so creation now
    // uses the select-screen floor and sizes too (it was drawn at 35 % of the image, select at 82 %).
    const bool bGlueStage=PreviewMode==EWowCharacterPreviewMode::CharacterSelection
        || PreviewMode==EWowCharacterPreviewMode::CharacterCreation;
    const bool EnlargeHuman=bGlueStage &&
        CurrentRaceScenePath.Equals(RaceScenePath(1,0),ESearchCase::IgnoreCase);
    // Measure only the geosets that are shown (hidden variants would inflate the box) and
    // only the body: like the original glue scenes, class gear (a warrior's two-hander, a
    // staff) must never change how big the character is drawn.
    {
        if (const FSkeletalMeshRenderData* Data=Body->GetSkeletalMeshAsset()->GetResourceForRendering())
            if (Data->LODRenderData.Num()>0)
                for (const FSkelMeshRenderSection& Section:Data->LODRenderData[0].RenderSections)
                    if (Body->IsMaterialSectionShown(Section.MaterialIndex,0))
                        for (uint32 V=Section.BaseVertexIndex; V<Section.BaseVertexIndex+Section.NumVertices && V<uint32(Vertices.Num()); ++V)
                            VisibleBounds+=Body->GetComponentTransform().TransformPosition(FVector(Vertices[V].Position));
    }
    if (!VisibleBounds.IsValid)
        for(const FFinalSkinVertex& Vertex:Vertices)
            VisibleBounds+=Body->GetComponentTransform().TransformPosition(FVector(Vertex.Position));
    const FBox BodyBounds = VisibleBounds;
    const FTransform Camera = CaptureComponent->GetComponentTransform();
    const double Aspect=RenderTarget?double(RenderTarget->SizeX)/FMath::Max(RenderTarget->SizeY,1):16.0/9.0;
    const double TanX=FMath::Tan(FMath::DegreesToRadians(CaptureComponent->FOVAngle*.5));
    const double TanY=TanX/Aspect;
    // Fixed authored camera; bottom-centre stage point reserves name/buttons.
    // r392 (Stefan's OG shots): where the feet stand on screen, as a share of the half height
    // below the centre. Dun Morogh (dwarf, gnome) 0.58 (feet at 855 of 1080), Durotar orc 0.80;
    // the others keep 0.70 until we have references.
    const uint8 StageRace = LastShownCharacterInfo.Race;
    const bool bSelectStage = bGlueStage;
    // r393: Stormwind (human) and Mulgore (tauren) stand at the bottom edge, feet near 1000 of 1080.
    const double FloorNdc = !bSelectStage ? .70 : (StageRace==3 || StageRace==7) ? .58 : StageRace==2 ? .80
        : (StageRace==1 || StageRace==6) ? .85 : .70;
    const FVector Ray = Camera.TransformVectorNoScale(FVector(1,0,-FloorNdc*TanY)).GetSafeNormal();
    FVector Floor;
    if (!FindPreviewGround(CurrentRaceSceneActor, Camera.GetLocation(), Ray, Floor))
    {
        UpdateCharacterLighting();
        UE_LOG(LogWowPreview, Warning, TEXT("No solid stage floor at bottom-centre: %s"), *CurrentRaceScenePath);
        return;
    }
    double SoleZ=TNumericLimits<double>::Max();
    for (const FFinalSkinVertex& Vertex:Vertices)
        SoleZ=FMath::Min(SoleZ,Body->GetComponentTransform().TransformPosition(FVector(Vertex.Position)).Z);
    // Fit around the soles, so scaling can never lift the feet off the floor.
    if (EnlargeHuman) SoleZ=BodyBounds.Min.Z;
    const FVector Pivot(BodyBounds.GetCenter().X,BodyBounds.GetCenter().Y,SoleZ);
    const FVector Support=Floor+FVector(0,0,.15);
    // r396 (Stefan: the human stood in the air on character select): the feet stand at FloorNdc,
    // and r393 put human/tauren at 0.85, below this fit's old -0.80 bottom limit, so nothing ever
    // fitted and the character stayed at its ungrounded first placement. The limit follows the floor.
    const double BottomLimit=FMath::Max(.80,FloorNdc+.05);
    auto Fits=[&](double Ratio, double WidthLimit=.44)
    {
        for(int32 I=0;I<8;++I)
        {
            const FVector Corner((I&1)?VisibleBounds.Max.X:VisibleBounds.Min.X,
                (I&2)?VisibleBounds.Max.Y:VisibleBounds.Min.Y,(I&4)?VisibleBounds.Max.Z:VisibleBounds.Min.Z);
            const FVector P=Camera.InverseTransformPosition(Support+(Corner-Pivot)*Ratio);
            if(P.X<=1 || FMath::Abs(P.Y/(P.X*TanX))>WidthLimit || P.Z/(P.X*TanY)>.64 || P.Z/(P.X*TanY)<-BottomLimit) return false;
        }
        return true;
    };
    double Low=0,High=64;
    for(int32 I=0;I<24;++I){const double Mid=(Low+High)*.5;if(Fits(Mid))Low=Mid;else High=Mid;}
    if(Low<=0 || !FMath::IsFinite(Low))
    {
        UE_LOG(LogWowPreview, Warning, TEXT("Stage fit found no size that fits (race %d, floor %.2f): character not grounded"), int32(StageRace), FloorNdc);
        return;
    }
    // Every race used to be blown up until it touched the safe area, so gnomes, dwarves
    // and humans were drawn as big as an orc and a wide tauren ended up the smallest.
    // Draw them at their relative heights instead: a fixed share of the image per race.
    {
        auto BodyHeight = [&](double Ratio)
        {
            const FVector Bottom=Camera.InverseTransformPosition(Support);
            const FVector Top=Camera.InverseTransformPosition(Support+FVector(0,0,BodyBounds.Max.Z-SoleZ)*Ratio);
            return Top.X>1 ? Top.Z/(Top.X*TanY)-Bottom.Z/(Bottom.X*TanY) : 100.0;
        };
        static const float Male[12]  ={1.f,1.f,1.03f,.78f,1.12f,.92f,1.18f,.58f,1.10f,.60f,.98f,1.12f};
        static const float Female[12]={.93f,.93f,.95f,.74f,1.05f,.88f,1.08f,.56f,1.00f,.58f,.92f,1.05f};
        const uint8 Race=LastShownCharacterInfo.Race<12?LastShownCharacterInfo.Race:1;
        // Stefan 10.10: every race filled the frame compared with his OG select/create shots
        // (Human, Tauren, Gnome, Orc, Dwarf). The r167 FOV change could not help: this fit sets
        // the size as a share of the image. Human 44 % -> 35 %, the other races in proportion.
        double Share=.35*(LastShownCharacterInfo.Gender==1?Female[Race]:Male[Race]); // of image height
        // r390/r392 (Stefan: the WotLK reference sizes never went through): measured on his OG
        // character-select screenshots at 1920x1080, feet to top of head as a share of the image
        // height (our own shot of the same dwarf showed this measure equals what is seen; r392
        // re-measured on full-size shots with a pixel grid). The original draws them about twice
        // as big as we did: dwarf 51 %, gnome 36 %, orc woman 72 %. The other gender follows the
        // proportional table; races without a reference keep the proportional rule.
        bool bMeasured = bGlueStage; // measured on select shots (r460: creation shows them the same)
        const bool bFemale = LastShownCharacterInfo.Gender==1;
        if (!bMeasured) {}
        else if (Race==3) Share = .51 * (bFemale ? Female[3]/Male[3] : 1.0);       // dwarf: OG male 51 %
        else if (Race==2) Share = .72 * (bFemale ? 1.0 : Male[2]/Female[2]);  // orc: OG female 72 %
        else if (Race==7) Share = .36 * (bFemale ? Female[7]/Male[7] : 1.0);  // gnome: OG male 36 %
        else if (Race==1) Share = .82 * (bFemale ? Female[1]/Male[1] : 1.0);  // r393 human: OG male 82 % (scene framing already matched)
        else if (Race==6) Share = .90 * (bFemale ? Female[6]/Male[6] : 1.0);  // r393 tauren: OG male ~90 % with horns
        // r460: races without an OG reference (night elf, undead, troll, blood elf, draenei) were drawn
        // at a third of the image (Stefan: his blood elf "tiny" on select). They follow the measured
        // human now, by their relative height in the table.
        else { Share = .82 * (bFemale ? Female[Race] : Male[Race]) / Male[1]; bMeasured = false; }
        // r474 (Stefan: his night elf woman and a tauren in the creator were "too large for the
        // frame", the head and horns cut at the top): the human-relative size is only an estimate
        // for unmeasured races, and the creation screen frames the model between its side panels.
        // Unmeasured races, and every race on the create screen, keep the safe-area fit as the
        // upper limit (it keeps the head and horns inside the image).
        if (PreviewMode==EWowCharacterPreviewMode::CharacterCreation) bMeasured = false;
        const double Before=BodyHeight(Low)*.5;
        double TargetLow=0, TargetHigh=64;
        for(int32 I=0;I<24;++I)
        {
            const double Mid=(TargetLow+TargetHigh)*.5;
            if(BodyHeight(Mid)*.5<=Share) TargetLow=Mid; else TargetHigh=Mid;
        }
        // The safe-area fit stays the upper limit, except for the wide races whose
        // shoulders/horns made that limit shrink them below their real size.
        const bool bWide=(Race==6||Race==11) && PreviewMode!=EWowCharacterPreviewMode::CharacterCreation;
        // A measured OG size wins over the safe area too (the OG orc is taller than it).
        if(TargetLow>0 && (bWide || EnlargeHuman || bMeasured || TargetLow<Low)) Low=TargetLow;
        // r489 (Stefan: the tauren in the creator was cut off before r474 and tiny after it, "either
        // you cut him off, or you make him TINY"): r474 used the select screen's safe-area fit, whose
        // width limit (the middle 44 % of the image width) shrank a wide tauren by his arms and horns
        // to 41 % of the image height. On the create screen the free area between the side panels is
        // wider (the middle ~58 %). There the race's size grows up to that: as big as its share
        // allows without the horns leaving the top or the arms reaching the panels.
        if(PreviewMode==EWowCharacterPreviewMode::CharacterCreation && TargetLow>Low)
        {
            double RoomLow=Low, RoomHigh=64;
            for(int32 I=0;I<24;++I){const double Mid=(RoomLow+RoomHigh)*.5;if(Fits(Mid,.58))RoomLow=Mid;else RoomHigh=Mid;}
            Low=FMath::Min(TargetLow,RoomLow);
        }
        UE_LOG(LogWowPreview,Log,TEXT("Preview size: race=%d gender=%d fill %.2f -> %.2f of image height"),
            Race,LastShownCharacterInfo.Gender,Before,BodyHeight(Low)*.5);
    }
    const FVector Location=Support+(CurrentCharacterActor->GetActorLocation()-Pivot)*Low;
    CurrentCharacterActor->SetActorScale3D(CurrentCharacterActor->GetActorScale3D()*Low);
    CurrentCharacterActor->SetActorLocation(Location);
    UpdateCharacterLighting();
    UE_LOG(LogWowPreview,Log,TEXT("Fixed-camera stage fit: floor=%s scale=%.3f"),*Floor.ToCompactString(),Low);
}

void AWowCharacterPreview::UpdateCharacterLighting()
{
    if (!CurrentCharacterActor || !CaptureComponent) return;

    // r505 (Stefan 14.10: Whiteknight, an HD dwarf priest, still made the whole Dun Morogh scene dark,
    // sky included, while a gnome in the same scene was fine; the scene lights were read correctly).
    // The bounds came from GetComponentsBoundingBox(true), which also counts particle emitters and
    // other non-colliding attachments (held items, HD effects). One far-reaching component moved the
    // centre away from the body, so the key direction was built from a wrong "to camera" vector and
    // lit the backdrop from behind. Use only the skeletal body meshes for the bounds, take the key
    // direction from the camera axis itself, and keep the height in a sane range.
    FBox Bounds(ForceInit);
    int32 NumPrimitives = 0, NumSkeletal = 0;
    {
        TInlineComponentArray<UPrimitiveComponent*> Parts(CurrentCharacterActor);
        for (UPrimitiveComponent* Part : Parts)
        {
            if (!Part || !Part->IsRegistered()) continue;
            ++NumPrimitives;
            if (Part->IsA<USkeletalMeshComponent>() && Part->IsVisible())
            {
                ++NumSkeletal;
                Bounds += Part->Bounds.GetBox();
            }
            else
            {
                // Only the body casts the key light's shadow (weapons, off-hands, particles,
                // procedural parts do not), like the original glue scenes that draw no shadows.
                Part->SetCastShadow(false);
            }
        }
    }
    if (!Bounds.IsValid) Bounds = CurrentCharacterActor->GetComponentsBoundingBox(false);
    const float Height = Bounds.IsValid ? FMath::Clamp(Bounds.GetSize().Z, 60.0f, 600.0f) : 180.0f;
    const FVector Centre = Bounds.IsValid ? Bounds.GetCenter()
        : CurrentCharacterActor->GetActorLocation() + FVector::UpVector * Height * 0.5f;
    const FVector ToCamera = -CaptureComponent->GetForwardVector();
    const FVector CameraRight = CaptureComponent->GetRightVector();
    UE_LOG(LogWowPreview, Log, TEXT("Character lighting: height=%.0f centre=%s (%d primitives, %d skeletal), all-bounds=%s"),
        Height, *Centre.ToCompactString(), NumPrimitives, NumSkeletal,
        *CurrentCharacterActor->GetComponentsBoundingBox(true).GetSize().ToCompactString());

    // Position relative to the displayed character, not PREVIEW_OFFSET: each
    // race scene has a different camera and foreground anchor.
    const FVector KeyDirection = -(ToCamera - CameraRight * 0.4f + FVector::UpVector * 0.65f).GetSafeNormal();
    KeyLight->SetWorldRotation(KeyDirection.Rotation());
    // r385: strength from the race scene's strongest light, never below the old 2.6 lux.
    constexpr float WowLightToLux = PI * 1.2f;
    const FPreviewSceneLights SceneLights = GWowPreviewSceneLightsByPreview.FindRef(this);
    const float KeyLux = SceneLights.bValid ? FMath::Clamp(SceneLights.KeyValue * WowLightToLux, 2.6f, 9.0f) : 2.6f;
    KeyLight->SetIntensity(KeyLux);
    KeyLight->SetCastShadows(true);
    // r399 (Stefan: Whiteknight's face stayed dark on Dun Morogh, the orc woman fine): the key comes
    // from above, and tall shoulder pads / helmets threw their shadow over the face. The original
    // glue scenes draw no shadows at all; keep the body's shadow on the ground, but shoulders and
    // head gear no longer cast one.
    {
        TInlineComponentArray<UStaticMeshComponent*> Gear(CurrentCharacterActor);
        for (UStaticMeshComponent* Item : Gear)
            if (Item && (Item->ComponentHasTag(TEXT("WowShoulderEquipment")) || Item->ComponentHasTag(TEXT("WowHeadEquipment"))))
                Item->SetCastShadow(false);
    }
    KeyLight->SetDynamicShadowDistanceMovableLight(6000.0f);
    // r359 (Stefan: Dwarf/Orc characters dark and flat on character select, Human fine): the contact
    // shadow length was 12 % of the SCREEN. Races drawn smaller (dwarf ~34 % of the image height, orc
    // and all races after the 44 -> 35 % size change) had arms, chest and legs self-shadowed by that
    // screen-space march. r379: UE 5.8 has no world-space switch on the light, so keep it in screen
    // space but short (3 % of the screen), which only darkens real contact (armpits, under the chin).
    if (KeyLight->ContactShadowLength != 0.03f)
    {
        // Raw property on a registered light: the GPU-scene copy must be rebuilt or
        // the renderer's validation reports "GPU Scene Lights is stale".
        KeyLight->ContactShadowLength = 0.03f;
        KeyLight->MarkRenderStateDirty();
    }
    // r385: the scene light's colour (cold snow light in Dun Morogh, warm sun in Elwynn), mixed
    // with the old warm white so a strongly coloured light (Orc fire) does not dye the character.
    KeyLight->SetLightColor(SceneLights.bValid && SceneLights.KeyValue > 0.f
        ? FMath::Lerp(FLinearColor(1.0f, 0.94f, 0.86f), SceneLights.KeyColor, 0.6f) : FLinearColor(1.0f, 0.94f, 0.86f));

    // Scale point-light power with distance squared so short races receive
    // the same soft fill as taller races rather than being overexposed.
    const float LightScale = FMath::Square(Height / 180.0f);
    // r226 (Stefan 11.10: the orc on character select still too dark, HD models on or off): the
    // fill sits closer to the camera axis and is stronger, so the side facing the viewer is lit
    // like the original's evenly lit glue characters; the key still models the form.
    FillLight->SetWorldLocation(Centre + (ToCamera * 1.25f + CameraRight * 0.5f
        + FVector::UpVector * 0.35f) * Height);
    FillLight->SetUseInverseSquaredFalloff(true);
    FillLight->SetIntensityUnits(ELightUnits::Candelas);
    // r385: the fill near the camera stands in for the scene's ambient light (about 1 lux at 6.5 cd).
    const float AmbientBoost = SceneLights.bValid ? FMath::Clamp(SceneLights.Ambient * WowLightToLux / 1.0f, 1.0f, 3.0f) : 1.0f;
    FillLight->SetIntensity(6.5f * LightScale * AmbientBoost);
    FillLight->SetAttenuationRadius(Height * 4.0f);
    FillLight->SetSourceRadius(Height * 0.25f);
    FillLight->SetLightColor(FLinearColor(0.87f, 0.93f, 1.0f));
    FillLight->SetCastShadows(false);

    RimLight->SetWorldLocation(Centre + (-ToCamera * 0.95f - CameraRight * 0.8f
        + FVector::UpVector * 0.7f) * Height);
    RimLight->SetUseInverseSquaredFalloff(true);
    RimLight->SetIntensityUnits(ELightUnits::Candelas);
    RimLight->SetIntensity(7.0f * LightScale);
    RimLight->SetAttenuationRadius(Height * 4.0f);
    RimLight->SetSourceRadius(Height * 0.2f);
    RimLight->SetLightColor(FLinearColor(0.80f, 0.88f, 1.0f));
    RimLight->SetCastShadows(false);
}

void AWowCharacterPreview::ClearRaceScene()
{
    if (CurrentRaceSceneActor && CurrentRaceSceneActor->IsValidLowLevel())
    {
        CurrentRaceSceneActor->Destroy();
    }
    CurrentRaceSceneActor = nullptr;
    CurrentRaceScenePath.Reset();
    GWowPreviewSceneLightsByPreview.Remove(this); // r385, r489: only this preview's lights
    bUsesOriginalDraeneiPlatform = false;
    CharacterAnchor = RigOffset;
    CharacterFacingYaw = 180.0f;
    CharacterSceneScale = 1.0f;
}

void AWowCharacterPreview::RotateCharacter(float YawDeltaDegrees)
{
    if (!CurrentCharacterActor)
    {
        return;
    }

    CurrentCharacterActor->AddActorWorldRotation(FRotator(0.0f, YawDeltaDegrees, 0.0f));
    // Coalesce mouse events into the next scheduled capture.
    CaptureAccumulator = FMath::Max(CaptureAccumulator, 1.0f / 60.0f);
}

void AWowCharacterPreview::AddZoom(float Steps)
{
    // r262 (Stefan 12.10: zoom the dressing room model with the scroll wheel): narrow the capture's
    // field of view (30 degrees = the framed whole body) down to a third.
    ZoomFactor = FMath::Clamp(ZoomFactor * FMath::Pow(1.15f, Steps), 1.0f, 3.0f);
    if (CaptureComponent) CaptureComponent->FOVAngle = 30.0f / ZoomFactor;
    CaptureAccumulator = FMath::Max(CaptureAccumulator, 1.0f / 60.0f);
}

bool AWowCharacterPreview::PlayAnimationForTesting(int32 AnimationId, bool bLooping)
{
    if (!CurrentCharacterActor)
    {
        return false;
    }

    UWowAnimationController* Controller =
        FWowCharacterBuilder::GetAnimationController(CurrentCharacterActor);
    return Controller && Controller->PlayAnimationById(
        static_cast<EWowAnimId>(AnimationId), bLooping);
}

void AWowCharacterPreview::PositionCamera(uint8 RaceId)
{
    // Race-specific values are retained as a safe fallback, but the normal
    // paper-doll path frames the actual spawned model bounds. That keeps short
    // races large and tall races complete without clipping their feet.
    float CamDist = -365.0f;
    float CamHeight = 125.0f;

    switch (RaceId)
    {
    case 6: // Tauren — tallest
        CamDist = -470.0f;
        CamHeight = 150.0f;
        break;
    case 4: // Night Elf — tall
    case 11: // Draenei — tall
        CamDist = -420.0f;
        CamHeight = 140.0f;
        break;
    case 8: // Troll — tall and lanky
        CamDist = -430.0f;
        CamHeight = 142.0f;
        break;
    case 7: // Gnome — shortest
        CamDist = -270.0f;
        CamHeight = 82.0f;
        break;
    case 3: // Dwarf — short
        CamDist = -315.0f;
        CamHeight = 100.0f;
        break;
    case 2: // Orc — broad
    case 10: // Blood Elf
        CamDist = -390.0f;
        CamHeight = 130.0f;
        break;
    case 5: // Undead
        CamDist = -365.0f;
        CamHeight = 120.0f;
        break;
    default:
        break;
    }

    FVector CameraLocation(CamDist, 0.0f, CamHeight);
    FVector CameraTarget(0.0f, 0.0f, CamHeight);
    if (CurrentCharacterActor && RenderTarget && RenderTarget->SizeX > 0 && RenderTarget->SizeY > 0)
    {
        FVector BoundsOrigin = FVector::ZeroVector;
        FVector BoundsExtent = FVector::ZeroVector;
        CurrentCharacterActor->GetActorBounds(false, BoundsOrigin, BoundsExtent, true);
        // Frame the body itself: from the soles to just above the head bone. Actor bounds also
        // hold hidden geosets, capes and weapons, which put a dwarf high in the frame with
        // empty space under his feet.
        if (USkeletalMeshComponent* Body = CurrentCharacterActor->FindComponentByClass<USkeletalMeshComponent>())
            if (Body->DoesSocketExist(TEXT("WowFirstPersonHead")))
            {
                const FVector Feet = Body->GetComponentLocation();
                const FVector Head = Body->GetSocketLocation(TEXT("WowFirstPersonHead"));
                const float Height = float(Head.Z - Feet.Z) * 1.16f;
                if (Height > 40.0f)
                {
                    BoundsOrigin = FVector(BoundsOrigin.X, BoundsOrigin.Y, Feet.Z + Height * 0.5f);
                    BoundsExtent.Z = Height * 0.5f;
                }
            }

        const bool bUsableBounds = BoundsExtent.Z >= 35.0f && BoundsExtent.Z <= 400.0f
            && BoundsExtent.Y >= 5.0f && BoundsExtent.Y <= 350.0f;
        if (bUsableBounds)
        {
            const FVector LocalCenter = GetActorTransform().InverseTransformPosition(BoundsOrigin);
            const float HorizontalHalfFov = FMath::DegreesToRadians(CaptureComponent->FOVAngle * 0.5f);
            const float AspectRatio = static_cast<float>(RenderTarget->SizeX) / static_cast<float>(RenderTarget->SizeY);
            const float VerticalHalfFov = FMath::Atan(FMath::Tan(HorizontalHalfFov) / AspectRatio);
            const float FitMargin = (RaceId == 6 || RaceId == 11) ? 1.22f : 1.10f;
            const float VerticalDistance = BoundsExtent.Z * FitMargin / FMath::Tan(VerticalHalfFov);
            const float HorizontalDistance = BoundsExtent.Y * FitMargin / FMath::Tan(HorizontalHalfFov);
            const float FitDistance = FMath::Max(VerticalDistance, HorizontalDistance);

            CameraTarget = LocalCenter;
            CameraLocation = CameraTarget - FVector(FitDistance, 0.0f, 0.0f);
        }
    }

    CaptureComponent->SetRelativeLocation(CameraLocation);
    CaptureComponent->SetRelativeRotation((CameraTarget - CameraLocation).Rotation());
}

void AWowCharacterPreview::ClearCharacter()
{
    CaptureAccumulator = 0.0f;
    bGroundingPending = false;
    if (CaptureComponent)
    {
        CaptureComponent->bCaptureEveryFrame = false;
        CaptureComponent->ClearShowOnlyComponents();
    }
    if (CurrentCharacterActor && CurrentCharacterActor->IsValidLowLevel())
    {
        CurrentCharacterActor->Destroy();
        CurrentCharacterActor = nullptr;
    }
}

void AWowCharacterPreview::CaptureNow()
{
    if (!IsValid(this)) return;
    if (CurrentCharacterActor && !IsValid(CurrentCharacterActor)) CurrentCharacterActor = nullptr;
    if (CurrentRaceSceneActor && !IsValid(CurrentRaceSceneActor)) CurrentRaceSceneActor = nullptr;
    if (bPreviewActive && (CurrentCharacterActor || CurrentRaceSceneActor) && CaptureComponent && RenderTarget)
    {
        if (bGroundingPending) GroundCharacter();
        CaptureComponent->CaptureScene();
    }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowPreviewFitTest,"WowUnreal.Preview.MeasuredFit",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowPreviewFitTest::RunTest(const FString&)
{
    for(double Aspect:{16.0/9.0,4.0/3.0,21.0/9.0})
    for(double Height:{85.0,180.0,320.0,520.0})
    {
        const FBox Bounds(FVector(180,-Height*.28,0),FVector(240,Height*.28,Height));
        const FTransform Camera(FRotator(-3,0,0),FVector(-100,0,120));
        double Scale=0;FVector Centre;
        TestTrue(TEXT("Measured model fits"),FitPreviewBounds(Bounds,Camera,40,Aspect,Scale,Centre));
        const double TanX=FMath::Tan(FMath::DegreesToRadians(20.0)),TanY=TanX/Aspect;
        double Low=1,High=-1;
        for(int32 I=0;I<8;++I)
        {
            FVector P((I&1)?Bounds.Max.X:Bounds.Min.X,(I&2)?Bounds.Max.Y:Bounds.Min.Y,(I&4)?Bounds.Max.Z:Bounds.Min.Z);
            P=Camera.InverseTransformPosition(Centre+(P-Bounds.GetCenter())*Scale);
            TestTrue(TEXT("Figure stays between side panels"),FMath::Abs(P.Y/(P.X*TanX))<=.501);
            const double Y=P.Z/(P.X*TanY);Low=FMath::Min(Low,Y);High=FMath::Max(High,Y);
        }
        TestTrue(TEXT("Head/feet leave room for controls"),Low>=-.631 && High<=.711);
        TestTrue(TEXT("Small and large races occupy comparable height"),High-Low>1.0);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowDraeneiStageCameraTest,
    "WowUnreal.Preview.DraeneiReplacementCamera",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowDraeneiStageCameraTest::RunTest(const FString&)
{
    FPreviewStaticCamera Camera;
    Camera.bValid = true;
    Camera.Position = FVector(1.3808361, 7.2472863, 0.5649618) * FWowCoordinate::SCALE;
    Camera.Target = FVector(1.4495248, 2.5240128, 0.8884613) * FWowCoordinate::SCALE;
    TestTrue(TEXT("Original hologram platform keeps its authored placement"), IsOriginalDraeneiStageCamera(Camera));
    // Camera values read from the user's replacement patch-L scene.
    Camera.Position = FVector(-4.3772087, 1.6647305, 0.7174819) * FWowCoordinate::SCALE;
    Camera.Target = FVector(-0.7923225, 0.3008050, 0.6809786) * FWowCoordinate::SCALE;
    TestFalse(TEXT("HD hall uses camera-relative foreground placement"), IsOriginalDraeneiStageCamera(Camera));
    return true;
}
#endif
