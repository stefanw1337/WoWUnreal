#include "WowUnitPortrait.h"
#include "Engine/Engine.h"
#include "WowPortraitMaterial.h"
#include "WowCookedMaterial.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "TextureResource.h"
#include "WowPlayerCharacter.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionCustom.h"

AWowUnitPortrait::AWowUnitPortrait()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 1.f/30.f;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PortraitCapture"));
    SetRootComponent(Capture);
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    // Persistent view state: the tonemapper LUT is cached between the 30 Hz portrait
    // captures instead of being rebuilt (and logged) every capture, and GPU-scene
    // light data stays valid between captures.
    Capture->bAlwaysPersistRenderingState = true;
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->FOVAngle = 30.0f;
    Capture->ShowFlags.SetLighting(false);
    Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetFog(false);
    Capture->ShowFlags.SetBloom(false);
    Capture->ShowFlags.SetEyeAdaptation(false);
    Capture->ShowFlags.SetMotionBlur(false);
    Capture->ShowFlags.SetTemporalAA(false);
    Capture->ShowFlags.SetTonemapper(true);
    // Render original materials in an unlit, fixed-exposure view for the UI.
    Capture->PostProcessBlendWeight = 1.0f;
    auto& PP = Capture->PostProcessSettings;
    PP.bOverride_AutoExposureMethod = true;
    PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    PP.AutoExposureApplyPhysicalCameraExposure = false;
    PP.bOverride_AutoExposureBias = true;
    // r301 (Stefan's test log: portraits too bright): the portrait is drawn unlit, i.e. at full
    // albedo, while the world model is shaded. About one stop down matches the lit model.
    PP.AutoExposureBias = -1.1f;
    PP.bOverride_ColorSaturation = true;
    PP.ColorSaturation = FVector4(1,1,1,1);
    PP.bOverride_ColorContrast = true;
    PP.ColorContrast = FVector4(1,1,1,1);
    PP.bOverride_ColorGamma = true;
    PP.ColorGamma = FVector4(1,1,1,1);
    PP.bOverride_SceneColorTint = true;
    // r319 (Stefan: portraits still too bright after r301): with the Lighting show flag off the
    // view renders unlit, where the manual exposure bias is not reliably applied. Darken in the
    // tonemapper instead (about 0.8 stop), which every capture path honours.
    PP.SceneColorTint = FLinearColor(0.58f, 0.58f, 0.58f, 1.0f);
    // r351 (Stefan: very pale NPCs such as Harlan Bagley still look washed out): roll off only the
    // brightest tones (light skin, grey hair, white cloth) by up to 18 %, leaving darker faces as they are.
    PP.bOverride_ColorGainHighlights = true;
    PP.ColorGainHighlights = FVector4(0.82f, 0.82f, 0.82f, 1.0f);
    PP.bOverride_ColorCorrectionHighlightsMin = true;
    PP.ColorCorrectionHighlightsMin = 0.3f;
    Capture->ShowFlags.SetDepthOfField(false);
}

// Texture-only materials keep world lighting and first-person material fades out of the portrait.
static UMaterial* PortraitSurface()
{
    static TWeakObjectPtr<UMaterial> Cached;
    if (!Cached.IsValid()) Cached = WowCookedMaterial::Load(TEXT("M_WowPortrait"));
    if (Cached.IsValid()) return Cached.Get();
#if WITH_EDITOR
    if (Cached.IsValid()) return Cached.Get();
    UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
    Material->SetShadingModel(MSM_Unlit);
    Material->BlendMode = BLEND_Masked;
    Material->TwoSided = true;
    auto* Texture = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
    Texture->ParameterName = TEXT("BaseTexture");
    Texture->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
    Material->GetExpressionCollection().AddExpression(Texture);
    Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Texture);
    Material->GetEditorOnlyData()->OpacityMask.Connect(4, Texture);
    bool bNeedsRecompile = false;
    Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_SkeletalMesh);
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Cached = WowCookedMaterial::Save(Material, TEXT("M_WowPortrait"));
    return Cached.Get();
#else
    return nullptr;
#endif
}

bool AWowUnitPortrait::RefreshPortraitMeshes(AActor* Unit)
{
    TInlineComponentArray<USkeletalMeshComponent*> Sources(Unit);
    TInlineComponentArray<UStaticMeshComponent*> StaticSources(Unit);
    Sources.RemoveAll([](USkeletalMeshComponent* C) { return !C->GetSkeletalMeshAsset() || C->ComponentHasTag(TEXT("WowHeldWeapon")) || C->ComponentHasTag(TEXT("WowEquipmentEffectGeometry")); });
    StaticSources.RemoveAll([](UStaticMeshComponent* C)
    {
        // r408 (Stefan: the belt torch showed in the portrait with realistic lighting off): leave
        // out hidden parts and the torch (its own visibility follows the option and swimming).
        return !C->GetStaticMesh() || C->ComponentHasTag(TEXT("WowHeldWeapon"))
            || C->ComponentHasTag(TEXT("WowTargetRing")) || C->ComponentHasTag(TEXT("WowBeltTorch"))
            || !C->IsVisible();
    });
    bool Changed = Sources.Num() != PoseSources.Num() || StaticSources.Num() != EquipmentSources.Num();
    for (int32 I=0; !Changed && I<Sources.Num(); ++I)
        Changed = PoseSources[I].Get()!=Sources[I] || PoseMeshes[I]->GetSkinnedAsset()!=Sources[I]->GetSkeletalMeshAsset();
    for (int32 I=0; !Changed && I<StaticSources.Num(); ++I)
        Changed = EquipmentSources[I].Get()!=StaticSources[I] || EquipmentMeshes[I]->GetStaticMesh()!=StaticSources[I]->GetStaticMesh();
    TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
    for (auto* C : Sources) for (int32 I=0; I<C->GetNumMaterials(); ++I) Materials.Add(C->GetMaterial(I));
    for (auto* C : StaticSources) for (int32 I=0; I<C->GetNumMaterials(); ++I) Materials.Add(C->GetMaterial(I));
    Changed |= Materials != SourceMaterials;
    // Composite skins and customization can update a MID in place, including
    // after initial loading. Comparing only the MID pointer freezes an old
    // placeholder texture in the portrait forever.
    // r301: every texture parameter, not only BaseTexture. NPC skins arrive under other
    // parameter names after the first capture (Harlan Bagley's quest portrait stayed white/grey
    // because only BaseTexture was compared, so the late skin never reached the copy).
    TArray<TWeakObjectPtr<UTexture>> Textures;
    for (const auto& SourceMaterial : Materials)
    {
        UTexture* Texture = nullptr;
        if (SourceMaterial.IsValid()) SourceMaterial->GetTextureParameterValue(TEXT("BaseTexture"), Texture);
        Textures.Add(Texture);
        if (const UMaterialInstance* Instance = Cast<UMaterialInstance>(SourceMaterial.Get()))
            for (const FTextureParameterValue& Value : Instance->TextureParameterValues)
                Textures.Add(Value.ParameterValue);
    }
    Changed |= Textures != SourceTextures;
    if (Changed)
    {
        Capture->ShowOnlyComponents.Empty();
        for (const auto& C : PoseMeshes) if (C) C->DestroyComponent();
        for (const auto& C : EquipmentMeshes) if (C) C->DestroyComponent();
        PoseMeshes.Reset(); EquipmentMeshes.Reset(); PoseSources.Reset(); EquipmentSources.Reset();
        SourceMaterials = Materials;
        SourceTextures = Textures;
        auto Configure = [this](UMeshComponent* Copy, UMeshComponent* Source)
        {
            Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Copy->SetCastShadow(false);
            Copy->SetVisibleInSceneCaptureOnly(true);
            Copy->SetOwnerNoSee(false);
            Copy->RegisterComponent();
            for (int32 I=0; I<Source->GetNumMaterials(); ++I)
            {
                UMaterialInterface* Original = Source->GetMaterial(I);
                // Retain the original body's UV/texture/material binding. Replacing
                // every section with a single portrait shader loses material semantics.
                if (Original)
                {
                    if (auto* Material = WowPortraitMaterial::Create(Original, Copy))
                        Copy->SetMaterial(I, Material);
                }
            }
            Capture->ShowOnlyComponents.Add(Copy);
        };
        for (auto* Source : Sources)
        {
            auto* Copy = NewObject<UPoseableMeshComponent>(this);
            Copy->SetSkeletalMesh(Source->GetSkeletalMeshAsset());
            Configure(Copy,Source);
            PoseMeshes.Add(Copy); PoseSources.Add(Source);
        }
        for (auto* Source : StaticSources)
        {
            auto* Copy = NewObject<UStaticMeshComponent>(this);
            Copy->SetStaticMesh(Source->GetStaticMesh()); Configure(Copy,Source);
            EquipmentMeshes.Add(Copy); EquipmentSources.Add(Source);
        }
    }
    for (int32 I=0; I<Sources.Num(); ++I)
    {
        auto* Copy = PoseMeshes[I].Get(); auto* Source = Sources[I];
        Copy->SetWorldTransform(Source->GetComponentTransform());
        // Leader-pose followers (hair/beard) do not evaluate their own animation.
        // CopyPose reads local bone transforms, not the leader used by rendering;
        // copying the follower leaves it in a stale/ref pose while the face runs.
        USkeletalMeshComponent* PoseSource = Source;
        TSet<USkeletalMeshComponent*> Visited;
        while (PoseSource && !Visited.Contains(PoseSource))
        {
            Visited.Add(PoseSource);
            auto* Leader = Cast<USkeletalMeshComponent>(PoseSource->LeaderPoseComponent.Get());
            if (!Leader || Visited.Contains(Leader)) break;
            PoseSource = Leader;
        }
        Copy->CopyPoseFromSkeletalComponent(PoseSource);
        // Each character mesh contains mutually exclusive robe/hair/face
        // sections. CopyPose does not copy their visibility; showing all of
        // them produces a white shell around the correctly textured character.
        if (const FSkeletalMeshRenderData* RenderData = Source->GetSkeletalMeshAsset()->GetResourceForRendering())
            for (int32 LOD = 0; LOD < RenderData->LODRenderData.Num(); ++LOD)
                for (int32 Section = 0; Section < RenderData->LODRenderData[LOD].RenderSections.Num(); ++Section)
                {
                    // Mirror the source exactly: the character builder hides geosets with
                    // ShowMaterialSection(Section, Section, ...), i.e. keyed by the section
                    // index. Keying by the shared material index made hair geosets that
                    // share one hair material flip on and off every capture (blinking hair).
                    const bool bShown = Source->IsMaterialSectionShown(Section, LOD);
                    if (Copy->IsMaterialSectionShown(Section, LOD) != bShown)
                        Copy->ShowMaterialSection(Section, Section, bShown, LOD);
                }
        const auto& Skeleton = Copy->GetSkinnedAsset()->GetRefSkeleton();
        for (int32 Bone=0; Bone<Copy->BoneSpaceTransforms.Num(); ++Bone)
            if (PoseSource->IsBoneHiddenByName(Skeleton.GetBoneName(Bone)))
                Copy->BoneSpaceTransforms[Bone].SetScale3D(Skeleton.GetRefBonePose()[Bone].GetScale3D());
        Copy->RefreshBoneTransforms();
        Copy->UpdateBounds();
    }
    for (int32 I=0; I<StaticSources.Num(); ++I)
        EquipmentMeshes[I]->SetWorldTransform(StaticSources[I]->GetComponentTransform());
    return !PoseMeshes.IsEmpty();
}

static UMaterial* PortraitUiSurface()
{
    static TWeakObjectPtr<UMaterial> Shared;
    if (Shared.IsValid()) return Shared.Get();
    UMaterial* Base = WowCookedMaterial::Load(TEXT("M_WowPortraitUI"));
#if WITH_EDITOR
    if (!Base)
    {
            Base = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
            Base->MaterialDomain = MD_UI;
            Base->BlendMode = BLEND_Translucent;
            auto& Expressions = Base->GetExpressionCollection();
            auto* Sample = NewObject<UMaterialExpressionTextureSampleParameter2D>(Base);
            Sample->ParameterName = TEXT("PortraitTexture");
            Sample->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
            Expressions.AddExpression(Sample);
            auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(Base);
            Expressions.AddExpression(UV);
            auto* Circle = NewObject<UMaterialExpressionCustom>(Base);
            Circle->Code = TEXT("return 1.0-smoothstep(0.475,0.49,length(UV-0.5));");
            Circle->OutputType = CMOT_Float1;
            FCustomInput Input; Input.InputName = TEXT("UV"); Input.Input.Connect(0, UV);
            Circle->Inputs.Add(Input);
            Expressions.AddExpression(Circle);
            Base->GetEditorOnlyData()->EmissiveColor.Connect(0, Sample);
            Base->GetEditorOnlyData()->Opacity.Connect(0, Circle);
            Base->PostEditChange();
        Base = WowCookedMaterial::Save(Base, TEXT("M_WowPortraitUI"));
    }
#endif
    Shared = Base;
    return Base;
}

UMaterialInterface* AWowUnitPortrait::CaptureUnit(AActor* Unit)
{
    CapturedUnit = Unit;
    LastRequestedAt = FPlatformTime::Seconds();
    USkeletalMeshComponent* Mesh = Unit ? Unit->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
    if (!Mesh || !Mesh->GetSkeletalMeshAsset()) return nullptr;
    if (!RenderTarget)
    {
        RenderTarget = NewObject<UTextureRenderTarget2D>(this);
        RenderTarget->ClearColor = FLinearColor(0.018f, 0.023f, 0.030f, 1.0f);
        RenderTarget->InitCustomFormat(128, 128, PF_B8G8R8A8, false);
        RenderTarget->TargetGamma = 2.2f;
        RenderTarget->UpdateResourceImmediate(true);
        Capture->TextureTarget = RenderTarget;
        // The engine logs "Pixel format for LUT is ..." for every capture whose LUT
        // is rebuilt (~30/s). Keep warnings/errors from LogRenderer, drop that info line.
        static bool bRendererLogQuieted = false;
        if (!bRendererLogQuieted && GEngine)
        {
            bRendererLogQuieted = true;
            GEngine->Exec(GetWorld(), TEXT("log LogRenderer Warning"));
        }
        if (UMaterial* Base = PortraitUiSurface())
        {
            PortraitMaterial = UMaterialInstanceDynamic::Create(Base, this);
            PortraitMaterial->SetTextureParameterValue(TEXT("PortraitTexture"), RenderTarget);
        }
    }
    if (LastRequestedAt - LastCapturedAt >= (1.0 / 30.0))
    {
        if (!RefreshPortraitMeshes(Unit)) return PortraitMaterial;
        UPoseableMeshComponent* PortraitMesh = PoseMeshes[0];
        const FBox Box = PortraitMesh->Bounds.GetBox();
        const float Height = FMath::Max(Box.GetSize().Z, 20.0f);
        const bool bHeadSocket = PortraitMesh->DoesSocketExist(TEXT("WowFirstPersonHead"));
        const bool bHumanoid=bHeadSocket || Unit->IsA<AWowPlayerCharacter>();
        FVector Focus = bHeadSocket ? PortraitMesh->GetSocketLocation(TEXT("WowFirstPersonHead"))
            : bHumanoid ? FVector(Box.GetCenter().X,Box.GetCenter().Y,Box.Max.Z-Height*0.14f) : Box.GetCenter();
        if (bHeadSocket) Focus.Z += Height * 0.025f;
        // r364 (Stefan: a Durotar Tiger's portrait is all face - zoom out on bigger beasts): creatures with
        // a head socket were framed like a humanoid head (18 % of their height). Long, low bodies (beasts,
        // mounts, wolves) get a wider frame that shows the head and neck, as the original portraits do.
        const bool bLongBody = !Unit->IsA<AWowPlayerCharacter>()
            && FMath::Max(Box.GetSize().X, Box.GetSize().Y) > Height * 1.2f;
        const float Radius = bHumanoid ? Height * (bLongBody ? 0.34f : 0.18f) : Box.GetExtent().GetMax() * 1.12f;
        const float Distance = FMath::Max(Radius, 12.0f) / FMath::Tan(FMath::DegreesToRadians(15.0f));
        const FVector Forward = Mesh->GetComponentTransform().TransformVectorNoScale(FVector::RightVector).GetSafeNormal();
        const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
        const FVector Camera = Focus + Forward * Distance + Side * Distance * 0.12f + FVector::UpVector * Distance * 0.04f;
        Capture->SetWorldLocationAndRotation(Camera, (Focus - Camera).Rotation());
        Capture->CaptureScene();
        if (FParse::Param(FCommandLine::Get(), TEXT("verifyruntimeportrait")))
        {
            static double LastDebug = -1;
            if (LastRequestedAt - LastDebug > 5)
            {
                LastDebug = LastRequestedAt;
                TArray<FColor> Pixels;
                RenderTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
                TArray<uint8> Png;
                FImageUtils::CompressImageArray(128, 128, Pixels, Png);
                FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / TEXT("RuntimePortrait.png")));
                UE_LOG(LogTemp, Display, TEXT("Runtime portrait: meshes=%d bounds=%s focus=%s camera=%s texture=%s"),
                    PoseMeshes.Num(), *Box.ToString(), *Focus.ToString(), *Camera.ToString(), *GetNameSafe(PortraitMaterial));
            }
        }
        LastCapturedAt = LastRequestedAt;
    }
    return PortraitMaterial;
}

static UMaterial* PaperDollUiSurface()
{
    if (auto* Loaded=WowCookedMaterial::Load(TEXT("M_WowPaperDollUI"))) return Loaded;
#if WITH_EDITOR
    auto* Material=NewObject<UMaterial>(GetTransientPackage(),NAME_None,RF_Transient);
    Material->MaterialDomain=MD_UI;
    Material->BlendMode=BLEND_Translucent;
    auto* Texture=NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
    Texture->ParameterName=TEXT("PortraitTexture");
    Texture->Texture=LoadObject<UTexture2D>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture"));
    Material->GetExpressionCollection().AddExpression(Texture);
    auto* Color=NewObject<UMaterialExpressionCustom>(Material);
    Color->OutputType=CMOT_Float3;
    FCustomInput RGB; RGB.InputName=TEXT("Color"); RGB.Input.Connect(0,Texture); Color->Inputs.Add(RGB);
    Color->Code=TEXT("float3 c=max(Color,0); return saturate((c*(2.51*c+0.03))/(c*(2.43*c+0.59)+0.14));");
    Material->GetExpressionCollection().AddExpression(Color);
    auto* Alpha=NewObject<UMaterialExpressionCustom>(Material);
    Alpha->OutputType=CMOT_Float1;
    FCustomInput A; A.InputName=TEXT("Coverage"); A.Input.Connect(4,Texture); Alpha->Inputs.Add(A);
    Alpha->Code=TEXT("return saturate(1-Coverage);");
    Material->GetExpressionCollection().AddExpression(Alpha);
    Material->GetEditorOnlyData()->EmissiveColor.Connect(0,Color);
    Material->GetEditorOnlyData()->Opacity.Connect(0,Alpha);
    Material->PreEditChange(nullptr); Material->PostEditChange();
    return WowCookedMaterial::Save(Material,TEXT("M_WowPaperDollUI"));
#else
    return nullptr;
#endif
}

void WowPreparePaperDollMaterial() { PaperDollUiSurface(); }
void WowPreparePortraitMaterial() { PortraitSurface(); PortraitUiSurface(); }

void AWowUnitPortrait::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // UI polls identity slowly. Animate only portraits still requested by it.
    if (CapturedUnit.IsValid() && FPlatformTime::Seconds()-LastRequestedAt<1.0)
    {
        const double RequestedAt=LastRequestedAt;
        CaptureUnit(CapturedUnit.Get());
        LastRequestedAt=RequestedAt;
    }
}
