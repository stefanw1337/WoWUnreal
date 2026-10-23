#include "WowCharacterBuilder.h"
#include "WowPerformanceScope.h"
#include "WowGlueMaterialAnimator.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionLinearInterpolate.h"
#endif
#include "WowRuntimeAnimSequence.h"
#include "WowSkeletalMeshBuilder.h"
#include "WowCharacterTexture.h"
#include "Async/Async.h"
#include "WowHDCharacter.h"
#include "WowEquipmentManager.h"
#include "WowM2ParticleComponent.h"
#include "WowAssetCache.h"
#include "WowTextureFactory.h"
#include "WowAnimationController.h"
#include "Mpq/MpqManager.h"
#include "Formats/M2Parser.h"
#include "Formats/M2Types.h"
#include "Formats/BlpParser.h"
#include "Formats/BlpTypes.h"
#include "Formats/Dbc/DbcStore.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/Skeleton.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Coord/WowCoordinate.h"
#include "UObject/SoftObjectPath.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionCustom.h"
#include "UObject/UObjectGlobals.h"
#include "ShaderCompiler.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWowCharacter, Log, All);

// Parsed M2 cache (shared with doodad manager via static)
static TMap<FString, TSharedPtr<FM2Data>> CharM2Cache;
static FCriticalSection CharCacheLock;
static TMap<FString, TWeakObjectPtr<USkeleton>> CharSkeletonCache;
static TMap<FString, TArray<TWeakObjectPtr<UAnimSequence>>> CharAnimCache;
static TMap<FString, TArray<int32>> CharAnimIdCache;
static TMap<FString, TArray<FGeosetSectionInfo>> CharGeosetCache;
static TMap<FString, TWeakObjectPtr<USkeletalMesh>> CreatureMeshCache;
static TMap<FString, TWeakObjectPtr<USkeletalMesh>> CharacterHairMeshCache;
static TMap<FString, TWeakObjectPtr<UTexture2D>> EquippedTextureCache;
static TSet<FString> CharM2InFlight;   // r566: being parsed on a worker (CharCacheLock)
static TSet<FString> CharM2Unparsable; // r566: failed on the worker; the game thread reports it

void FWowCharacterBuilder::ClearCaches()
{
    FScopeLock Lock(&CharCacheLock);
    CharM2Cache.Reset();
    CharM2InFlight.Reset();
    CharM2Unparsable.Reset();
    CharSkeletonCache.Reset();
    CharAnimCache.Reset();
    CharAnimIdCache.Reset();
    CharGeosetCache.Reset();
    CreatureMeshCache.Reset();
    CharacterHairMeshCache.Reset();
    EquippedTextureCache.Reset();
    FWowHDCharacter::ClearCaches();
}

namespace
{
const TSet<int32>& GetRuntimeCharacterAnimationIds()
{
	static const TSet<int32> AnimationIds = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
		11, 12, 13, 14, 16, 17, 18, 19,
		25, 26, 27, 28, 31, 32, 33,
		37, 38, 39, 40, 41, 42, 43, 44, 45,
		51, 52, 53, 54, 91, 95, 124, 125, 126, 229, 233, 234
	};
	return AnimationIds;
}

// r507 (Stefan: Groklord's off-hand dagger sheathed in his chest, the main-hand one on the hip):
// an off-hand slot reports inventory type 22, but a dagger's Item.dbc row is a one-hand weapon (13),
// so the display-id lookup found no row and fell back to the "back" sheath 1 (socket 27, which sits
// in a troll's chest). One-hand, main-hand and off-hand weapons are the same kind of item here.
static bool SameInventoryKind(uint32 SlotType, uint32 ItemType)
{
    auto OneHand = [](uint32 T) { return T == 13 || T == 21 || T == 22; };
    return SlotType == 0 || SlotType == ItemType || (OneHand(SlotType) && OneHand(ItemType));
}

uint8 ResolveEquipmentSheathType(const FWowCharacterBuilder::FEquipmentSlot& Slot)
{
    if (Slot.SheathType > 0)
    {
        return Slot.SheathType;
    }

    // Inventory slots with an unambiguous pose take precedence over display-id
    // reverse lookup. Multiple Item.dbc records can share a display id while
    // carrying a different sheath value, so the first visual match is not a
    // reliable pose source for two-hand, ranged, shield, or wand equipment.
    switch (Slot.InventoryType)
    {
    case 14: return 4; // Shield
    case 15:
    case 17: return 2; // Ranged or two-hand / staff, large weapon on back
    case 26: return 3; // Wand/thrown, hip
    default: break;
    }

    for (const FItemDbcEntry& Item : FDbcStore::Get().Items().GetAll())
    {
        if (Item.DisplayInfoID == Slot.ItemDisplayId &&
            SameInventoryKind(uint32(Slot.InventoryType), uint32(Item.InventoryType)))
        {
            return static_cast<uint8>(Item.SheathType);
        }
    }

    // Item.dbc uses the four Wrath sheath categories, not attachment ids:
    // 1 main-hand back, 2 large weapon, 3 hip weapon, 4 shield.
    // Character-enum equipment exposes display/inventory type but not the item
    // template, so preserve those same categories for custom-realm fallbacks.
    switch (Slot.InventoryType)
    {
    case 13:
    case 21:
    case 22: return 1; // One-hand / main- or off-hand back sheath
    default: return 0;
    }
}

uint32 ResolveEquipmentSubclassId(const FWowCharacterBuilder::FEquipmentSlot& Slot)
{
    for (const FItemDbcEntry& Item : FDbcStore::Get().Items().GetAll())
    {
        if (Item.ClassID == 2 && Item.DisplayInfoID == Slot.ItemDisplayId &&
            SameInventoryKind(uint32(Slot.InventoryType), uint32(Item.InventoryType)))
        {
            return Item.SubclassID;
        }
    }

    return MAX_uint32;
}

FName GetSheathSocketTag(const FWowCharacterBuilder::FEquipmentSlot& Slot, uint8 SheathType)
{
    const bool bLeftHand = Slot.AttachPoint == FWowEquipmentManager::EAttachmentPoint::LeftHand;
    switch (SheathType)
    {
    case 1:
        return bLeftHand ? TEXT("WowSheathOffHand") : TEXT("WowSheathMainHand");
    case 2:
        // The attachment names describe the side occupied by the blade/head,
        // rather than the hand the item came from. Cross dual-wielded weapons
        // so a main-hand weapon uses the opposite diagonal on the back.
        return bLeftHand ? TEXT("WowSheathLargeWeaponRight") : TEXT("WowSheathLargeWeaponLeft");
    case 3:
        return bLeftHand ? TEXT("WowSheathHipWeaponLeft") : TEXT("WowSheathHipWeaponRight");
    case 4:
        return TEXT("WowSheathShield");
    default: return NAME_None;
    }
}

UMaterial* GetCharacterMaterial()
{
    static UMaterial* CachedMat = nullptr;
    if (CachedMat && CachedMat->IsValidLowLevel()) return CachedMat;

    // Try loading a pre-compiled version first
    const TCHAR* MatPaths[] = {
        TEXT("/Game/Wow/Materials/M_WowCharacterV2"),
        TEXT("/Game/Materials/M_WowCharacterV2")
    };
    for (const TCHAR* Path : MatPaths)
    {
        FSoftObjectPath MatPath(FString(Path) + TEXT(".") + FPaths::GetBaseFilename(Path));
        if (MatPath.ResolveObject() || FPackageName::DoesPackageExist(MatPath.GetLongPackageName()))
        {
            CachedMat = LoadObject<UMaterial>(nullptr, Path);
            if (CachedMat) CachedMat->AddToRoot(); // r274: cooked materials are GC'd at runtime; a dangling cache crashed the render thread
            if (CachedMat)
            {
                UE_LOG(LogWowCharacter, Log, TEXT("Loaded character material from %s"), Path);
#if WITH_EDITOR
                if (!CachedMat->IsComplete())
                {
                    CachedMat->ForceRecompileForRendering();
                    if (GShaderCompilingManager)
                        GShaderCompilingManager->FinishAllCompilation();
                }
#endif
                return CachedMat;
            }
        }
    }

#if WITH_EDITOR
    UE_LOG(LogWowCharacter, Log, TEXT("Creating character material (UV0-based, DefaultLit with PBR)"));

    UPackage* MatPackage = CreatePackage(TEXT("/Game/Materials/M_WowCharacterV2"));
    CachedMat = NewObject<UMaterial>(MatPackage, TEXT("M_WowCharacterV2"),
        RF_Public | RF_Standalone);
    CachedMat->SetShadingModel(MSM_DefaultLit);
    CachedMat->BlendMode = BLEND_Masked; // Alpha cutout for hair transparency
    CachedMat->TwoSided = true;          // Hair and some body parts need two-sided
    CachedMat->bUsedWithSkeletalMesh = true;

    UTexture2D* WhiteTex = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
    auto& Expressions = CachedMat->GetExpressionCollection();

    // UV0 for texture sampling
    auto* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(CachedMat);
    TexCoord->CoordinateIndex = 0;
    TexCoord->MaterialExpressionEditorX = -400;
    TexCoord->MaterialExpressionEditorY = 0;
    Expressions.AddExpression(TexCoord);

    auto* TextureSampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(CachedMat);
    TextureSampler->ParameterName = TEXT("BaseTexture");
    TextureSampler->SamplerType = SAMPLERTYPE_Color;
    TextureSampler->Texture = WhiteTex;
    TextureSampler->Coordinates.Connect(0, TexCoord);
    TextureSampler->MaterialExpressionEditorX = -200;
    TextureSampler->MaterialExpressionEditorY = 0;
    Expressions.AddExpression(TextureSampler);

    // DefaultLit: keep authored alpha cutouts and add a temporal dither for the
    // final zoom range. Each local character instance controls this parameter.
    CachedMat->GetEditorOnlyData()->BaseColor.Connect(0, TextureSampler);

    auto* FirstPersonOpacity = NewObject<UMaterialExpressionScalarParameter>(CachedMat);
    FirstPersonOpacity->ParameterName = TEXT("FirstPersonOpacity");
    FirstPersonOpacity->DefaultValue = 1.0f;
    FirstPersonOpacity->MaterialExpressionEditorX = -200;
    FirstPersonOpacity->MaterialExpressionEditorY = 420;
    Expressions.AddExpression(FirstPersonOpacity);

    auto* BlueNoise = NewObject<UMaterialExpressionScalarBlueNoise>(CachedMat);
    BlueNoise->MaterialExpressionEditorX = -20;
    BlueNoise->MaterialExpressionEditorY = 520;
    Expressions.AddExpression(BlueNoise);

    auto* One = NewObject<UMaterialExpressionConstant>(CachedMat);
    One->R = 1.0f;
    Expressions.AddExpression(One);
    auto* Zero = NewObject<UMaterialExpressionConstant>(CachedMat);
    Zero->R = 0.0f;
    Expressions.AddExpression(Zero);

    auto* Dither = NewObject<UMaterialExpressionIf>(CachedMat);
    Dither->A.Connect(0, FirstPersonOpacity);
    Dither->B.Connect(0, BlueNoise);
    Dither->AGreaterThanB.Connect(0, One);
    Dither->AEqualsB.Connect(0, One);
    Dither->ALessThanB.Connect(0, Zero);
    Dither->MaterialExpressionEditorX = 160;
    Dither->MaterialExpressionEditorY = 420;
    Expressions.AddExpression(Dither);

    auto* MaskedOpacity = NewObject<UMaterialExpressionMultiply>(CachedMat);
    MaskedOpacity->A.Connect(4, TextureSampler); // texture alpha
    MaskedOpacity->B.Connect(0, Dither);
    MaskedOpacity->MaterialExpressionEditorX = 180;
    MaskedOpacity->MaterialExpressionEditorY = 80;
    Expressions.AddExpression(MaskedOpacity);
    CachedMat->GetEditorOnlyData()->OpacityMask.Connect(0, MaskedOpacity);

    // Add Roughness constant (1.0 = fully rough, no specular highlights)
    auto* RoughnessConstant = NewObject<UMaterialExpressionConstant>(CachedMat);
    RoughnessConstant->R = 1.0f;
    RoughnessConstant->MaterialExpressionEditorX = -200;
    RoughnessConstant->MaterialExpressionEditorY = 100;
    Expressions.AddExpression(RoughnessConstant);
    CachedMat->GetEditorOnlyData()->Roughness.Connect(0, RoughnessConstant);

    // Add Metallic constant
    auto* MetallicConstant = NewObject<UMaterialExpressionConstant>(CachedMat);
    MetallicConstant->R = 0.0f;
    MetallicConstant->MaterialExpressionEditorX = -200;
    MetallicConstant->MaterialExpressionEditorY = 200;
    Expressions.AddExpression(MetallicConstant);
    CachedMat->GetEditorOnlyData()->Metallic.Connect(0, MetallicConstant);

    // Add Specular constant (0.0 = no specular, WoW-style diffuse only)
    auto* SpecularConstant = NewObject<UMaterialExpressionConstant>(CachedMat);
    SpecularConstant->R = 0.0f;
    SpecularConstant->MaterialExpressionEditorX = -200;
    SpecularConstant->MaterialExpressionEditorY = 300;
    Expressions.AddExpression(SpecularConstant);
    CachedMat->GetEditorOnlyData()->Specular.Connect(0, SpecularConstant);

    CachedMat->PreEditChange(nullptr);
    CachedMat->PostEditChange();
    CachedMat->ForceRecompileForRendering();

    // Block until shader compilation is done — otherwise skeletal meshes render black
    if (GShaderCompilingManager)
    {
        UE_LOG(LogWowCharacter, Log, TEXT("Waiting for character material shader compilation..."));
        GShaderCompilingManager->FinishAllCompilation();
        UE_LOG(LogWowCharacter, Log, TEXT("Character material compiled, IsComplete=%d"),
            CachedMat->IsComplete() ? 1 : 0);
    }

    // Save to disk so it persists for subsequent launches
    {
        FString PackageFilename = FPackageName::LongPackageNameToFilename(
            TEXT("/Game/Materials/M_WowCharacterV2"), FPackageName::GetAssetPackageExtension());
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        UPackage::SavePackage(MatPackage, CachedMat, *PackageFilename, SaveArgs);
        UE_LOG(LogWowCharacter, Log, TEXT("Saved character material to: %s"), *PackageFilename);
    }
#endif

    if (!CachedMat)
    {
        CachedMat = UMaterial::GetDefaultMaterial(MD_Surface);
        if (CachedMat) CachedMat->AddToRoot(); // r274: cooked materials are GC'd at runtime; a dangling cache crashed the render thread
    }
    return CachedMat;
}

// r627 (Stefan: "a line in our login screen going to the middle"): glue textures honour the M2 texture's
// wrap flags like the original client (0x1 wrap U, 0x2 wrap V, unset = clamp). Wrapping a clamped card
// bled its opposite border in as a thin line along the card's edge.
void ApplyGlueTextureWrap(UTexture2D* Texture, const FM2Data* Data, int32 TextureIndex)
{
    if (!Texture || !Data || !Data->TextureFlags.IsValidIndex(TextureIndex)) return;
    const uint32 Flags = Data->TextureFlags[TextureIndex];
    const TextureAddress X = (Flags & 1) ? TA_Wrap : TA_Clamp;
    const TextureAddress Y = (Flags & 2) ? TA_Wrap : TA_Clamp;
    if (Texture->AddressX == X && Texture->AddressY == Y) return;
    Texture->AddressX = X;
    Texture->AddressY = Y;
    Texture->UpdateResource();
}

UMaterial* GetGlueModelMaterial(uint16 WowBlendMode, bool bLit = false, bool bAnimatedUV = false)
{
    // Preserve M2 blend equations: modulate layers must not become light.
    const int32 Variant=FMath::Clamp<int32>(WowBlendMode,0,6);
    if(Variant>=3) bLit=false;
    const int32 CacheIndex=Variant+7*(bAnimatedUV?2:(bLit?1:0));
    static UMaterial* CachedMaterials[21]={};
    if(CachedMaterials[CacheIndex] && CachedMaterials[CacheIndex]->IsValidLowLevel())return CachedMaterials[CacheIndex];
    // r644: V3 - glue passes are never fogged (M2 render flag 0x2 "unfogged"; the original glue screens
    // have no fog at all); fog over the far sky cards washed the login sky out.
    const FString Name=FString::Printf(TEXT("M_WowGluePassV3_%d"),CacheIndex);
    const FString PackagePath=TEXT("/Game/Materials/")+Name;
    if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *PackagePath))
    {
        CachedMaterials[CacheIndex] = Existing;
        if (CachedMaterials[CacheIndex]) CachedMaterials[CacheIndex]->AddToRoot(); // r274: cooked materials are GC'd at runtime; a dangling cache crashed the render thread
#if WITH_EDITOR
        if (!Existing->IsComplete())
        {
            Existing->ForceRecompileForRendering();
            if (GShaderCompilingManager)
            {
                GShaderCompilingManager->FinishAllCompilation();
            }
        }
#endif
        return Existing;
    }

#if WITH_EDITOR
    UPackage* Package = CreatePackage(*PackagePath);
    UMaterial* Material = NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone);
    if (!Material)
    {
        return nullptr;
    }

    Material->SetShadingModel(bLit ? MSM_DefaultLit : MSM_Unlit);
    if (bLit) Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
    Material->BlendMode = Variant == 0 ? BLEND_Opaque
        : (Variant == 1 ? BLEND_Masked : (Variant == 2 ? BLEND_Translucent : (Variant >= 5 ? BLEND_Modulate : BLEND_Additive)));
    Material->TwoSided = true;
    Material->bUseTranslucencyVertexFog = false; // r644
    Material->bUsedWithSkeletalMesh = true;
    if (Variant == 1)
    {
        Material->OpacityMaskClipValue = 0.33f;
    }

    UTexture2D* WhiteTexture = LoadObject<UTexture2D>(
        nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
    auto& Expressions = Material->GetExpressionCollection();

    auto* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(Material);
    TexCoord->CoordinateIndex = 0;
    Expressions.AddExpression(TexCoord);

    auto* TextureSampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
    TextureSampler->ParameterName = TEXT("BaseTexture");
    TextureSampler->SamplerType = SAMPLERTYPE_Color;
    TextureSampler->Texture = WhiteTexture;
    TextureSampler->Coordinates.Connect(0, TexCoord);
    Expressions.AddExpression(TextureSampler);
    if (bAnimatedUV)
    {
        auto* Speed=NewObject<UMaterialExpressionVectorParameter>(Material);
        Speed->ParameterName=TEXT("UVScrollSpeed");Speed->DefaultValue=FLinearColor::Transparent;
        Expressions.AddExpression(Speed);
        auto* RG=NewObject<UMaterialExpressionComponentMask>(Material);
        RG->Input.Connect(0,Speed);RG->R=true;RG->G=true;Expressions.AddExpression(RG);
        auto* Panner=NewObject<UMaterialExpressionPanner>(Material);
        Panner->Coordinate.Connect(0,TexCoord);Panner->Speed.Connect(0,RG);Panner->bFractionalPart=true;
        Expressions.AddExpression(Panner);TextureSampler->Coordinates.Connect(0,Panner);
    }


    auto* Tint=NewObject<UMaterialExpressionVectorParameter>(Material);
    Tint->ParameterName=TEXT("PassTint");Tint->DefaultValue=FLinearColor::White;Expressions.AddExpression(Tint);
    auto* Opacity=NewObject<UMaterialExpressionScalarParameter>(Material);
    Opacity->ParameterName=TEXT("PassOpacity");Opacity->DefaultValue=1;Expressions.AddExpression(Opacity);
    auto* Colour=NewObject<UMaterialExpressionMultiply>(Material);
    Colour->A.Connect(0,TextureSampler);Colour->B.Connect(0,Tint);Expressions.AddExpression(Colour);
    auto* Alpha=NewObject<UMaterialExpressionMultiply>(Material);
    Alpha->A.Connect(4,TextureSampler);Alpha->B.Connect(0,Opacity);Expressions.AddExpression(Alpha);
    if(bLit)
    {
        Material->GetEditorOnlyData()->BaseColor.Connect(0,Colour);
        auto* Ambient=NewObject<UMaterialExpressionMultiply>(Material);
        Ambient->A.Connect(0,Colour);Ambient->ConstB=.18f;Expressions.AddExpression(Ambient);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0,Ambient);
        auto* Roughness=NewObject<UMaterialExpressionConstant>(Material);
        Roughness->R=.9f;Expressions.AddExpression(Roughness);Material->GetEditorOnlyData()->Roughness.Connect(0,Roughness);
    }
    else if(Variant>=5)
    {
        auto* Strength=NewObject<UMaterialExpressionMultiply>(Material);
        Strength->A.Connect(0,Colour);Strength->ConstB=Variant==6?2.f:1.f;Expressions.AddExpression(Strength);
        // A disabled modulate pass must tend to white (neutral), never black.
        auto* Modulation=NewObject<UMaterialExpressionLinearInterpolate>(Material);
        Modulation->ConstA=1;Modulation->B.Connect(0,Strength);Modulation->Alpha.Connect(0,Opacity);Expressions.AddExpression(Modulation);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0,Modulation);
    }
    else Material->GetEditorOnlyData()->EmissiveColor.Connect(0,Colour);
    if(Variant==1)Material->GetEditorOnlyData()->OpacityMask.Connect(0,Alpha);
    else if(Variant>=2 && Variant<=4)
        Material->GetEditorOnlyData()->Opacity.Connect(0,Variant==3?static_cast<UMaterialExpression*>(Opacity):static_cast<UMaterialExpression*>(Alpha));

    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Material->ForceRecompileForRendering();
    if (GShaderCompilingManager)
    {
        GShaderCompilingManager->FinishAllCompilation();
    }

    FString Filename = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(Package, Material, *Filename, SaveArgs);
    CachedMaterials[CacheIndex] = Material;
    if (CachedMaterials[CacheIndex]) CachedMaterials[CacheIndex]->AddToRoot(); // r274: cooked materials are GC'd at runtime; a dangling cache crashed the render thread
    return Material;
#else
    return nullptr;
#endif
}

// r615 (Stefan: the HD pack's login lacked the flying rocks and the mirrored tower in the sky): most of
// that scene's cards have two texture layers - the picture and a "_mod2x" layer that scrolls on its own
// and is combined as mod2x (colour x2, alpha multiplied). The second layer's alpha cuts the sky plates
// into floating shards; drawn with the first texture only they were solid lids over the sky.
UMaterial* GetGlueTwoLayerMaterial(uint16 WowBlendMode)
{
    const int32 Variant=FMath::Clamp<int32>(WowBlendMode,1,4);
    static UMaterial* CachedMaterials[5]={};
    if(CachedMaterials[Variant] && CachedMaterials[Variant]->IsValidLowLevel())return CachedMaterials[Variant];
    // r644: V2 - the second layer reads UV set 1 (texture-unit lookup 1, as the original's T1_T2 shaders)
    // and no fog.
    const FString Name=FString::Printf(TEXT("M_WowGlue2LayerV2_%d"),Variant);
    const FString PackagePath=TEXT("/Game/Materials/")+Name;
    if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *PackagePath))
    {
        CachedMaterials[Variant]=Existing;
        Existing->AddToRoot();
        return Existing;
    }
#if WITH_EDITOR
    UPackage* Package = CreatePackage(*PackagePath);
    UMaterial* Material = NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone);
    if (!Material) return nullptr;
    Material->SetShadingModel(MSM_Unlit);
    Material->BlendMode = Variant == 1 ? BLEND_Masked : (Variant == 2 ? BLEND_Translucent : BLEND_Additive);
    Material->TwoSided = true;
    Material->bUsedWithSkeletalMesh = true;
    if (Variant == 1) Material->OpacityMaskClipValue = 0.33f;
    Material->bUseTranslucencyVertexFog = false;
    UTexture2D* WhiteTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
    auto& Expressions = Material->GetExpressionCollection();
    auto* TexCoord0 = NewObject<UMaterialExpressionTextureCoordinate>(Material);
    TexCoord0->CoordinateIndex = 0;
    Expressions.AddExpression(TexCoord0);
    auto* TexCoord1 = NewObject<UMaterialExpressionTextureCoordinate>(Material);
    TexCoord1->CoordinateIndex = 1;
    Expressions.AddExpression(TexCoord1);
    auto Layer = [&](const TCHAR* TextureName, const TCHAR* SpeedName, UMaterialExpressionTextureCoordinate* TexCoord)
    {
        auto* Speed=NewObject<UMaterialExpressionVectorParameter>(Material);
        Speed->ParameterName=SpeedName;Speed->DefaultValue=FLinearColor::Transparent;Expressions.AddExpression(Speed);
        auto* RG=NewObject<UMaterialExpressionComponentMask>(Material);
        RG->Input.Connect(0,Speed);RG->R=true;RG->G=true;Expressions.AddExpression(RG);
        auto* Panner=NewObject<UMaterialExpressionPanner>(Material);
        Panner->Coordinate.Connect(0,TexCoord);Panner->Speed.Connect(0,RG);Panner->bFractionalPart=true;Expressions.AddExpression(Panner);
        auto* Sampler=NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
        Sampler->ParameterName=TextureName;Sampler->SamplerType=SAMPLERTYPE_Color;Sampler->Texture=WhiteTexture;
        Sampler->Coordinates.Connect(0,Panner);Expressions.AddExpression(Sampler);
        return Sampler;
    };
    auto* First = Layer(TEXT("BaseTexture"), TEXT("UVScrollSpeed"), TexCoord0);
    auto* Second = Layer(TEXT("Layer2Texture"), TEXT("UVScrollSpeed2"), TexCoord1);
    auto* Tint=NewObject<UMaterialExpressionVectorParameter>(Material);
    Tint->ParameterName=TEXT("PassTint");Tint->DefaultValue=FLinearColor::White;Expressions.AddExpression(Tint);
    auto* Opacity=NewObject<UMaterialExpressionScalarParameter>(Material);
    Opacity->ParameterName=TEXT("PassOpacity");Opacity->DefaultValue=1;Expressions.AddExpression(Opacity);
    auto Custom = [&](ECustomMaterialOutputType Type, const TCHAR* Code)
    {
        auto* Node=NewObject<UMaterialExpressionCustom>(Material);
        Node->OutputType=Type;Node->Inputs.Reset();Node->Code=Code;Expressions.AddExpression(Node);
        return Node;
    };
    auto In = [](UMaterialExpressionCustom* Node, const TCHAR* InputName, UMaterialExpression* Expression, int32 Output)
    { FCustomInput Value; Value.InputName=InputName; Value.Input.Connect(Output, Expression); Node->Inputs.Add(Value); };
    auto* Colour = Custom(CMOT_Float3, TEXT("return saturate(A * B * 2.0) * T;"));
    In(Colour, TEXT("A"), First, 0); In(Colour, TEXT("B"), Second, 0); In(Colour, TEXT("T"), Tint, 0);
    auto* Alpha = Custom(CMOT_Float1, TEXT("return A * B * O;"));
    In(Alpha, TEXT("A"), First, 4); In(Alpha, TEXT("B"), Second, 4); In(Alpha, TEXT("O"), Opacity, 0);
    Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Colour);
    if (Variant == 1) Material->GetEditorOnlyData()->OpacityMask.Connect(0, Alpha);
    else Material->GetEditorOnlyData()->Opacity.Connect(0, Variant == 3 ? static_cast<UMaterialExpression*>(Opacity) : static_cast<UMaterialExpression*>(Alpha));
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Material->ForceRecompileForRendering();
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    FString Filename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(Package, Material, *Filename, SaveArgs);
    CachedMaterials[Variant] = Material;
    Material->AddToRoot();
    return Material;
#else
    return nullptr;
#endif
}

UTexture2D* LoadBlpTexture(FMpqManager* Mpq, FWowAssetCache* Cache, const FString& TexturePath)
{
    if (!Mpq || TexturePath.IsEmpty())
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

    TArray<uint8> BlpRaw;
    if (!Mpq->ReadFile(TexturePath, BlpRaw))
    {
        return nullptr;
    }

    FBlpTexture BlpData = FBlpParser::Parse(BlpRaw);
    if (!BlpData.bIsValid)
    {
        return nullptr;
    }

    UTexture2D* Texture = FWowTextureFactory::CreateTexture(BlpData, TexturePath);
    if (Texture && Cache)
    {
        Cache->CacheTexture(TexturePath, Texture);
    }
    return Texture;
}

void ApplyTexturedMaterial(UPrimitiveComponent* Component, UTexture2D* Texture)
{
    if (!Component || !Texture)
    {
        return;
    }

    if (UMaterial* BaseMat = GetCharacterMaterial())
    {
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, Component);
        MID->SetTextureParameterValue(TEXT("BaseTexture"), Texture);
        Component->SetMaterial(0, MID);
    }
}

int32 FindPreferredAnimationIndex(const FM2Data& Data)
{
    int32 FirstLooping = INDEX_NONE;
    for (int32 Index = 0; Index < Data.AnimationTracks.Num(); ++Index)
    {
        const FM2AnimationData& Track = Data.AnimationTracks[Index];
        if (Track.AnimationId == 0)
        {
            return Index;
        }

        if (FirstLooping == INDEX_NONE && Track.bIsLooping)
        {
            FirstLooping = Index;
        }
    }

    return FirstLooping != INDEX_NONE ? FirstLooping : 0;
}

UAnimSequence* CreateReferencePoseAnimation(USkeleton* Skeleton, const FString& ModelName)
{
    if (!Skeleton)
    {
        return nullptr;
    }

    if (WowUseRuntimeM2Import())
    {
        auto* Runtime = NewObject<UWowRuntimeAnimSequence>();
        Runtime->InitializeRuntime(Skeleton, 1);
        return Runtime;
    }
    UAnimSequence* AnimSeq = NewObject<UAnimSequence>();
    AnimSeq->SetSkeleton(Skeleton);

#if WITH_EDITOR
    IAnimationDataController& Controller = AnimSeq->GetController();
    Controller.InitializeModel();
    Controller.OpenBracket(FText::FromString(TEXT("CreateReferencePoseAnimation")));
    Controller.SetFrameRate(FFrameRate(30, 1));
    Controller.SetNumberOfFrames(1);

    const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
    const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();
    for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
    {
        const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
        const FTransform& BoneTransform = RefPose[BoneIndex];

        Controller.AddBoneCurve(BoneName);
        Controller.SetBoneTrackKeys(
            BoneName,
            { FVector3f(BoneTransform.GetTranslation()) },
            { FQuat4f(BoneTransform.GetRotation()) },
            { FVector3f(BoneTransform.GetScale3D()) });
    }

    Controller.CloseBracket();
    Controller.NotifyPopulated();
#else
    UE_LOG(LogWowCharacter, Warning, TEXT("Skipping reference-pose anim setup — GetController() requires editor"));
#endif

    UE_LOG(LogWowCharacter, Log, TEXT("Created reference-pose animation for %s"), *ModelName);
    return AnimSeq;
}

UTexture2D* ResolveCreatureTextureOverride(FMpqManager* Mpq, FWowAssetCache* Cache,
    const FString& ModelPath, const FString& TexturePath)
{
    if (TexturePath.IsEmpty())
    {
        return nullptr;
    }

    FString ResolvedPath = TexturePath;
    ResolvedPath.ReplaceInline(TEXT("/"), TEXT("\\"));
    if (!ResolvedPath.EndsWith(TEXT(".blp"), ESearchCase::IgnoreCase))
    {
        ResolvedPath += TEXT(".blp");
    }

    if (UTexture2D* Texture = LoadBlpTexture(Mpq, Cache, ResolvedPath))
    {
        return Texture;
    }

    FString RelativePath = FPaths::Combine(FPaths::GetPath(ModelPath), FPaths::GetCleanFilename(ResolvedPath));
    RelativePath.ReplaceInline(TEXT("/"), TEXT("\\"));
    if (UTexture2D* Texture = LoadBlpTexture(Mpq, Cache, RelativePath))
    {
        return Texture;
    }

    FString BakedNpcPath = FPaths::Combine(
        TEXT("Textures\\BakedNpcTextures"), FPaths::GetCleanFilename(ResolvedPath));
    BakedNpcPath.ReplaceInline(TEXT("/"), TEXT("\\"));
    return LoadBlpTexture(Mpq, Cache, BakedNpcPath);
}
}

// Global entry point for the anonymous-namespace pass material (used by WowEquipmentManager
// for item glow cards: blend variants, UV scrolling, PassTint / PassOpacity).
UMaterial* WowGetM2PassMaterial(uint16 WowBlendMode, bool bLit, bool bAnimatedUV)
{
    return GetGlueModelMaterial(WowBlendMode, bLit, bAnimatedUV);
}

void FWowCharacterBuilder::PrepareGlueMaterials()
{
    for(uint16 Blend=0;Blend<=6;++Blend)
    {
        GetGlueModelMaterial(Blend);
        GetGlueModelMaterial(Blend,true);
        GetGlueModelMaterial(Blend,false,true);
    }
    for(uint16 Blend=1;Blend<=4;++Blend) GetGlueTwoLayerMaterial(Blend); // r615
}

TMap<uint16, uint16> FWowCharacterBuilder::ComputeDefaultGeosets(const FWowCharacterTexture::FCustomization& Customization)
{
    TMap<uint16, uint16> Result;
    const FDbcStore& Dbc = FDbcStore::Get();

    // WMV defaults ALL groups to variant 1. Variant 0 = hide entire group.
    // GeosetId = group*100 + variant. Group 0 variant 1 = geosetId 1 (body mesh).

    // Group 0 (CG_SKIN_OR_HAIRSTYLE): Hair mesh is in group 0!
    // GeosetId=0 (base body) always visible via special case in mesh filter.
    // Hair geoset ID from CharHairGeosets sets which additional group 0 variant to show.
    {
        const FCharHairGeosetsDbcEntry* HairEntry = Dbc.CharHairGeosets().GetByRaceGenderVariation(
            Customization.RaceId, Customization.Gender, Customization.HairStyle);

        uint16 HairVariant = 1; // Default: variant 1 = basic body
        if (HairEntry)
        {
            HairVariant = static_cast<uint16>(FMath::Max(1u, HairEntry->GeosetID));
            UE_LOG(LogWowCharacter, Log, TEXT("Hair geoset: Race=%d Gender=%d Style=%d → group 0 variant %d"),
                Customization.RaceId, Customization.Gender, Customization.HairStyle, HairVariant);
        }
        Result.Add(0, HairVariant);
    }

    // Groups 1, 2, 3 (Facial features): From CharacterFacialHairStyles DBC.
    // The DBC columns are ordered 1xx, 3xx, 2xx (as WoW Model Viewer reads them):
    // Geosets[0] -> group 1, Geosets[1] -> group 3, Geosets[2] -> group 2. Reading them as
    // 1/2/3 hid troll tusks (3xx) and mixed unrelated dwarf beard parts.
    {
        const FCharacterFacialHairStylesDbcEntry* FacialEntry = Dbc.CharacterFacialHairStyles().GetByRaceGenderVariation(
            Customization.RaceId, Customization.Gender, Customization.FacialHairStyle);

        if (FacialEntry)
        {
            // Zero means that facial component is hidden. Preserve it instead
            // of forcing variant 1, which combines unrelated Dwarf beard parts.
            Result.Add(1, static_cast<uint16>(FacialEntry->Geosets[0]));
            Result.Add(3, static_cast<uint16>(FacialEntry->Geosets[1]));
            Result.Add(2, static_cast<uint16>(FacialEntry->Geosets[2]));
        }
        else
        {
            Result.Add(1, 0);
            Result.Add(2, 0);
            Result.Add(3, 0);
        }
    }
    Result.Add(4, 1);  // Forearms/gloves: variant 1 = bare forearms
    Result.Add(5, 1);  // Shins/boots: variant 1 = bare shins
    Result.Add(6, 1);  // Tail/misc
    Result.Add(7, 2);  // Ears: visible (WMV uses 2)
    Result.Add(8, 1);  // Wristbands/sleeves
    Result.Add(9, 1);  // Kneepads
    Result.Add(10, 1); // Chest
    Result.Add(11, 1); // Pants lower
    Result.Add(12, 1); // Tabard
    Result.Add(13, 1); // Trousers
    Result.Add(14, 1); // Loincloth
    Result.Add(15, 1); // Cape

    for (uint16 g = 16; g <= 18; ++g)
        Result.Add(g, 1);

    return Result;
}

TMap<uint16, uint16> FWowCharacterBuilder::ComputeGeosetWithEquipment(
    const FWowCharacterTexture::FCustomization& Customization,
    const FBodyEquipment& BodyEquipment)
{
    // Start with default geosets based on customization
    TMap<uint16, uint16> Result = ComputeDefaultGeosets(Customization);

    const FDbcStore& Dbc = FDbcStore::Get();

    // Apply equipment geoset changes
    auto ApplyEquipmentGeoset = [&](uint32 DisplayId, auto GeosetModifier) {
        if (DisplayId == 0) return;

        const FItemDisplayInfoDbcEntry* Item = Dbc.ItemDisplayInfo().GetById(DisplayId);
        if (!Item) return;

        GeosetModifier(Item, Result);
    };

    // Check if wearing a robe (determines if pants geosets should be skipped)
    bool bIsWearingRobe = BodyEquipment.bIsRobeEquipped;
    if (!bIsWearingRobe && BodyEquipment.ChestDisplayId > 0)
    {
        const FItemDisplayInfoDbcEntry* ChestItem = Dbc.ItemDisplayInfo().GetById(BodyEquipment.ChestDisplayId);
        if (ChestItem && ChestItem->GeosetGroups[2] > 0)
        {
            bIsWearingRobe = true;
            UE_LOG(LogWowCharacter, Log, TEXT("Chest DisplayID %d detected as robe (GeosetGroups[2]=%d)"),
                BodyEquipment.ChestDisplayId, ChestItem->GeosetGroups[2]);
        }
    }

    // CHEST: Apply chest item geosets according to WMVx formula
    ApplyEquipmentGeoset(BodyEquipment.ChestDisplayId, [](const FItemDisplayInfoDbcEntry* Item, TMap<uint16, uint16>& Geosets) {
        // geosetGroup[0] → CG_SLEEVES (group 8)
        if (Item->GeosetGroups[0] > 0)
        {
            Geosets.Add(8, static_cast<uint16>(Item->GeosetGroups[0] + 1));
        }
        // geosetGroup[1] → CG_CHEST (group 10)
        if (Item->GeosetGroups[1] > 0)
        {
            Geosets.Add(10, static_cast<uint16>(Item->GeosetGroups[1] + 1));
        }
        // geosetGroup[2] → CG_TROUSERS (group 13) for robes
        if (Item->GeosetGroups[2] > 0)
        {
            Geosets.Add(13, static_cast<uint16>(Item->GeosetGroups[2] + 1));
        }
        // geosetGroup[3] → CG_TORSO (group 22)
        if (Item->GeosetGroups[3] > 0)
        {
            Geosets.Add(22, static_cast<uint16>(Item->GeosetGroups[3] + 1));
        }
    });

    // SHIRT: Also affects CG_SLEEVES
    ApplyEquipmentGeoset(BodyEquipment.ShirtDisplayId, [](const FItemDisplayInfoDbcEntry* Item, TMap<uint16, uint16>& Geosets) {
        if (Item->GeosetGroups[0] > 0)
        {
            Geosets.Add(8, static_cast<uint16>(Item->GeosetGroups[0] + 1));
        }
    });

    // PANTS: Only apply if NOT wearing a robe
    if (!bIsWearingRobe)
    {
        ApplyEquipmentGeoset(BodyEquipment.PantsDisplayId, [](const FItemDisplayInfoDbcEntry* Item, TMap<uint16, uint16>& Geosets) {
            // geosetGroup[0] → CG_PANTS (group 11)
            if (Item->GeosetGroups[0] > 0)
            {
                Geosets.Add(11, static_cast<uint16>(Item->GeosetGroups[0] + 1));
            }
            // geosetGroup[1] → CG_KNEEPADS (group 9)
            if (Item->GeosetGroups[1] > 0)
            {
                Geosets.Add(9, static_cast<uint16>(Item->GeosetGroups[1] + 1));
            }
            // geosetGroup[2] → CG_TROUSERS (group 13) for pants
            if (Item->GeosetGroups[2] > 0)
            {
                Geosets.Add(13, static_cast<uint16>(Item->GeosetGroups[2] + 1));
            }
        });
    }

    // GLOVES: CG_GLOVES (group 4)
    ApplyEquipmentGeoset(BodyEquipment.GlovesDisplayId, [](const FItemDisplayInfoDbcEntry* Item, TMap<uint16, uint16>& Geosets) {
        if (Item->GeosetGroups[0] > 0)
        {
            Geosets.Add(4, static_cast<uint16>(Item->GeosetGroups[0] + 1));
        }
    });

    // BOOTS: CG_BOOTS (group 5) and CG_FEET (group 20)
    ApplyEquipmentGeoset(BodyEquipment.BootsDisplayId, [](const FItemDisplayInfoDbcEntry* Item, TMap<uint16, uint16>& Geosets) {
        // geosetGroup[0] → CG_BOOTS (group 5)
        if (Item->GeosetGroups[0] > 0)
        {
            Geosets.Add(5, static_cast<uint16>(Item->GeosetGroups[0] + 1));
        }
        // geosetGroup[1] → CG_FEET (group 20)
        if (Item->GeosetGroups[1] > 0)
        {
            Geosets.Add(20, static_cast<uint16>(Item->GeosetGroups[1] + 1));
        }
    });

    // BRACERS: May have additional geosets beyond just affecting sleeves
    ApplyEquipmentGeoset(BodyEquipment.BracersDisplayId, [](const FItemDisplayInfoDbcEntry* Item, TMap<uint16, uint16>& Geosets) {
        // geosetGroup[0] → CG_SLEEVES (group 8) - bracers can modify sleeves
        if (Item->GeosetGroups[0] > 0)
        {
            Geosets.Add(8, static_cast<uint16>(Item->GeosetGroups[0] + 1));
        }
        // geosetGroup[1] → CG_HAND_ATTACHMENT (group 23) potentially
        if (Item->GeosetGroups[1] > 0)
        {
            Geosets.Add(23, static_cast<uint16>(Item->GeosetGroups[1] + 1));
        }
    });

    // BELT: CG_BELT (group 18)
    ApplyEquipmentGeoset(BodyEquipment.BeltDisplayId, [](const FItemDisplayInfoDbcEntry* Item, TMap<uint16, uint16>& Geosets) {
        if (Item->GeosetGroups[0] > 0)
        {
            Geosets.Add(18, static_cast<uint16>(Item->GeosetGroups[0] + 1));
        }
    });

    // TABARD: CG_TABARD (group 12) - typically variant 1 when equipped
    ApplyEquipmentGeoset(BodyEquipment.TabardDisplayId, [](const FItemDisplayInfoDbcEntry* Item, TMap<uint16, uint16>& Geosets) {
        if (Item->GeosetGroups[0] > 0)
        {
            Geosets.Add(12, static_cast<uint16>(Item->GeosetGroups[0] + 1));
        }
        else
        {
            // Default to variant 1 if no specific geoset
            Geosets.Add(12, 1);
        }
    });

    // CAPE: CG_CLOAK (group 15)
    ApplyEquipmentGeoset(BodyEquipment.CapeDisplayId, [](const FItemDisplayInfoDbcEntry* Item, TMap<uint16, uint16>& Geosets) {
        if (Item->GeosetGroups[0] > 0)
        {
            Geosets.Add(15, static_cast<uint16>(Item->GeosetGroups[0] + 1));
        }
    });

    if (bIsWearingRobe)
    {
        UE_LOG(LogWowCharacter, Log, TEXT("Robe equipped (DisplayID %d): Using robe trousers geoset, skipping pants geosets"),
            BodyEquipment.ChestDisplayId);
    }

    return Result;
}

void FWowCharacterBuilder::ApplyGeosetVisibility(USkeletalMeshComponent* MeshComp,
    const TArray<FGeosetSectionInfo>& GeosetInfo,
    const TMap<uint16, uint16>& VisibleGeosets)
{
    if (!MeshComp || GeosetInfo.Num() == 0)
    {
        return;
    }

    int32 VisibleCount = 0;
    int32 HiddenCount = 0;

    // Debug: log all sections and rules
    UE_LOG(LogWowCharacter, Log, TEXT("ApplyGeosetVisibility: %d sections, %d rules"), GeosetInfo.Num(), VisibleGeosets.Num());
    for (const FGeosetSectionInfo& Info : GeosetInfo)
    {
        UE_LOG(LogWowCharacter, Verbose, TEXT("  Section %d: geosetId=%d group=%d variant=%d"),
            Info.SectionIndex, Info.GeosetId, Info.GeosetGroup, Info.GeosetVariant);
    }
    for (const auto& Rule : VisibleGeosets)
    {
        UE_LOG(LogWowCharacter, Log, TEXT("  Rule: group %d → show variant %d"), Rule.Key, Rule.Value);
    }

    for (const FGeosetSectionInfo& Info : GeosetInfo)
    {
        bool bShouldBeVisible = true; // Default: show unless explicitly filtered

        if (Info.GeosetId == 0)
        {
            // GeosetId 0 = base body mesh in 3.3.5 M2, always visible
            bShouldBeVisible = true;
        }
        else
        {
            const uint16* DesiredVariant = VisibleGeosets.Find(Info.GeosetGroup);
            if (DesiredVariant)
            {
                // For groups with a rule, show only the matching variant
                bShouldBeVisible = (Info.GeosetVariant == *DesiredVariant);
                if (!bShouldBeVisible)
                {
                    UE_LOG(LogWowCharacter, Log, TEXT("  HIDING section %d: group=%d has variant=%d but rule wants %d"),
                        Info.SectionIndex, Info.GeosetGroup, Info.GeosetVariant, *DesiredVariant);
                }
            }
            // else: Groups not in our rule set: show by default (creatures, etc.)
        }

        // MaterialID = SectionIndex since we have one material slot per section.
        // SectionIndex param = INDEX_NONE means use MaterialID directly.
        MeshComp->ShowMaterialSection(Info.SectionIndex, Info.SectionIndex, bShouldBeVisible, 0);

        if (bShouldBeVisible)
        {
            VisibleCount++;
        }
        else
        {
            HiddenCount++;
        }
    }

    UE_LOG(LogWowCharacter, Log, TEXT("Applied geoset visibility: %d visible, %d hidden (of %d sections)"),
        VisibleCount, HiddenCount, GeosetInfo.Num());

    // Debug: Log all geoset groups found for verification
    if (GeosetInfo.Num() > 0)
    {
        TSet<uint16> UniqueGroups;
        for (const FGeosetSectionInfo& Info : GeosetInfo)
        {
            UniqueGroups.Add(Info.GeosetGroup);
        }
        UE_LOG(LogWowCharacter, Verbose, TEXT("Character geoset groups present: %s"),
            *FString::JoinBy(UniqueGroups.Array(), TEXT(", "), [](uint16 Group) { return FString::FromInt(Group); }));
    }
}

FString FWowCharacterBuilder::GetCharacterModelPath(ERace Race, EGender Gender)
{
    const FDbcStore& Dbc = FDbcStore::Get();
    uint32 RaceId = static_cast<uint32>(Race);
    const FChrRacesDbcEntry* RaceEntry = Dbc.ChrRaces().GetById(RaceId);

    if (!RaceEntry)
    {
        UE_LOG(LogWowCharacter, Warning, TEXT("Unknown race ID %d"), RaceId);
        return FString();
    }

    FString RaceStr = RaceEntry->ClientFileString;
    FString GenderStr = (Gender == EGender::Male) ? TEXT("Male") : TEXT("Female");

    return FString::Printf(TEXT("Character\\%s\\%s\\%s%s.m2"), *RaceStr, *GenderStr, *RaceStr, *GenderStr);
}

AActor* FWowCharacterBuilder::SpawnCharacter(UWorld* World, FMpqManager* Mpq, FWowAssetCache* Cache,
    ERace Race, EGender Gender, const FVector& Location, const FRotator& Rotation)
{
    FString ModelPath = GetCharacterModelPath(Race, Gender);
    if (ModelPath.IsEmpty()) return nullptr;

    UE_LOG(LogWowCharacter, Log, TEXT("Spawning character: Race=%d Gender=%d Model=%s"),
        static_cast<int32>(Race), static_cast<int32>(Gender), *ModelPath);

    // Build composite texture with default customization
    FWowCharacterTexture::FCustomization Cust;
    Cust.RaceId = static_cast<uint32>(Race);
    Cust.Gender = static_cast<uint32>(Gender);
    UTexture2D* CompositeTex = FWowCharacterTexture::BuildCompositeTexture(Mpq, Cache, Cust);

    return SpawnM2Actor(World, Mpq, Cache, ModelPath, Location, Rotation, 1.0f, CompositeTex, &Cust);
}

namespace
{
// r311: neutral stand-in for a body atlas still being composited on a worker thread.
UTexture2D* GetPendingAtlasPlaceholder()
{
    static UTexture2D* Placeholder = nullptr;
    if (Placeholder) return Placeholder;
    Placeholder = UTexture2D::CreateTransient(4, 4, PF_R8G8B8A8, TEXT("WowPendingCharacterAtlas"));
    if (!Placeholder) return nullptr;
    Placeholder->SRGB = true;
    Placeholder->NeverStream = true;
    uint8* Pixels = static_cast<uint8*>(Placeholder->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
    for (int32 I = 0; Pixels && I < 16; ++I) { Pixels[I*4] = 118; Pixels[I*4+1] = 96; Pixels[I*4+2] = 82; Pixels[I*4+3] = 255; }
    Placeholder->GetPlatformData()->Mips[0].BulkData.Unlock();
    Placeholder->UpdateResource();
    Placeholder->AddToRoot();
    return Placeholder;
}

bool HasBodyOverlays(const FWowCharacterTexture::FBodyEquipment& E)
{
    return E.ChestDisplayId || E.PantsDisplayId || E.BootsDisplayId || E.GlovesDisplayId || E.BracersDisplayId
        || E.ShirtDisplayId || E.TabardDisplayId || E.BeltDisplayId;
}

UTexture2D* CreateAtlasTexture(const TArray<uint8>& Pixels, int32 Width, int32 Height)
{
    UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_R8G8B8A8);
    if (!Texture) return nullptr;
    Texture->Filter = TF_Bilinear; Texture->SRGB = true; Texture->NeverStream = true;
    auto& Mip = Texture->GetPlatformData()->Mips[0];
    if (void* Destination = Mip.BulkData.Lock(LOCK_READ_WRITE)) FMemory::Memcpy(Destination, Pixels.GetData(), FMath::Min<int64>(Pixels.Num(), int64(Width) * Height * 4));
    Mip.BulkData.Unlock();
    Texture->UpdateResource();
    return Texture;
}

// Points every body material of Actor that still shows From at To (hair/eyes/items keep theirs).
void SwapBodyAtlas(AActor* Actor, UTexture* From, UTexture2D* To)
{
    if (!Actor || !From || !To) return;
    TInlineComponentArray<UMeshComponent*> Meshes(Actor);
    for (UMeshComponent* Mesh : Meshes)
        for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
            if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Slot)))
            {
                UTexture* Current = nullptr;
                if (MID->GetTextureParameterValue(TEXT("BaseTexture"), Current) && Current == From)
                    MID->SetTextureParameterValue(TEXT("BaseTexture"), To);
            }
}
}

// r649 (Stefan: "huge drops in FPS down to 8"; the log had model-read-parse of HDX\\Character\\Orc\\Male\\OrcMale.m2
// at 245 ms on the game thread when a nearby player was upgraded to the HD model): the HD model of a race/gender
// is parsed on a worker as well, next to the classic one, so the upgrade finds it ready.
static bool PrewarmM2OnWorker(FMpqManager* Mpq, const FString& ModelPath);
bool FWowCharacterBuilder::PrewarmCharacterModel(FMpqManager* Mpq, ERace Race, EGender Gender)
{
    const FString ModelPath = GetCharacterModelPath(Race, Gender);
    if (!Mpq || ModelPath.IsEmpty()) return true;
    if (Mpq->HasHDData()) PrewarmM2OnWorker(Mpq, FString(TEXT("HDX\\")) + ModelPath.Replace(TEXT("/"), TEXT("\\")));
    return PrewarmM2OnWorker(Mpq, ModelPath);
}

static bool PrewarmM2OnWorker(FMpqManager* Mpq, const FString& ModelPath)
{
    FString NormPath = ModelPath.ToLower();
    NormPath.ReplaceInline(TEXT("/"), TEXT("\\"));
    {
        FScopeLock Lock(&CharCacheLock);
        if (CharM2Cache.Contains(NormPath) || CharM2Unparsable.Contains(NormPath)) return true;
        if (CharM2InFlight.Contains(NormPath)) return false;
        CharM2InFlight.Add(NormPath);
    }
    // Same archive guard as the async gear atlas (r311): a data switch waits for this job.
    FWowCharacterTexture::BeginAsyncWork();
    Async(EAsyncExecution::ThreadPool, [Mpq, ModelPath, NormPath]()
    {
        FWowPerformanceScope ReadTiming(TEXT("model-read-parse-async"), ModelPath, 1000.0);
        TSharedPtr<FM2Data> M2Data;
        TArray<uint8> M2Raw, SkinRaw;
        if (Mpq->ReadFile(ModelPath, M2Raw))
        {
            FString SkinPath = ModelPath;
            if (SkinPath.EndsWith(TEXT(".m2"), ESearchCase::IgnoreCase)) SkinPath = SkinPath.Left(SkinPath.Len() - 3);
            Mpq->ReadFile(SkinPath + TEXT("00.skin"), SkinRaw);
            M2Data = MakeShared<FM2Data>(FM2Parser::Parse(M2Raw, SkinRaw));
            // As SpawnM2Actor does: HD models name their own textures with plain paths that only exist in the HD set.
            if (M2Data->bIsValid && ModelPath.StartsWith(TEXT("HDX"), ESearchCase::IgnoreCase))
                for (FString& TexturePath : M2Data->TexturePaths)
                    if (!TexturePath.IsEmpty() && !TexturePath.StartsWith(TEXT("HDX"), ESearchCase::IgnoreCase))
                        TexturePath = TEXT("HDX\\") + TexturePath;
        }
        FWowCharacterTexture::EndAsyncWork(); // no archive access past this point
        FScopeLock Lock(&CharCacheLock);
        CharM2InFlight.Remove(NormPath);
        if (M2Data.IsValid() && M2Data->bIsValid) { if (!CharM2Cache.Contains(NormPath)) CharM2Cache.Add(NormPath, M2Data); }
        else CharM2Unparsable.Add(NormPath);
    });
    return false;
}

AActor* FWowCharacterBuilder::SpawnCharacterWithEquipment(UWorld* World, FMpqManager* Mpq, FWowAssetCache* Cache,
    const FCharacterParams& Params, const FVector& Location, const FRotator& Rotation)
{
    FString ModelPath = GetCharacterModelPath(Params.Race, Params.Gender);
    if (ModelPath.IsEmpty()) return nullptr;

    UE_LOG(LogWowCharacter, Log, TEXT("Spawning equipped character: Race=%d Gender=%d Equipment=%d slots"),
        static_cast<int32>(Params.Race), static_cast<int32>(Params.Gender), Params.Equipment.Num());

    FWowPerformanceScope Timing(TEXT("equipped-character"), ModelPath);

    // Never modify the cached naked atlas: it is shared by other characters.
    // A mount change can reuse the same finished appearance without re-uploading it.
    const auto& C = Params.Customization;
    const auto& E = Params.BodyEquipment;
    FString AppearanceKey = FString::Printf(TEXT("%s:%d:%u:%u:%u:%u:%u:%u:%u"),
        *ModelPath, int32(Mpq->GetAssetProfile()), C.RaceId,C.Gender,C.SkinColor,
        C.FaceVariation,C.HairStyle,C.HairColor,C.FacialHairStyle);
    for (uint32 Value : {E.RaceId,E.Gender,E.ChestDisplayId,E.PantsDisplayId,E.BootsDisplayId,
        E.GlovesDisplayId,E.BracersDisplayId,E.ShirtDisplayId,E.TabardDisplayId,E.BeltDisplayId,
        E.CapeDisplayId,uint32(E.bIsRobeEquipped),E.GuildTabard})
        AppearanceKey += FString::Printf(TEXT(":%u"),Value);
    UTexture2D* FinalTex = EquippedTextureCache.FindRef(AppearanceKey).Get();
    // HD model (option "hdcharacters", local player): its own atlases, geosets and
    // HDX\\ model path. Missing data falls back to the classic model below.
    FWowHDCharacter::FAppearance HD;
    const bool bHD = Params.bUseHDModel && FWowHDCharacter::Build(Mpq, Cache, ModelPath, Params.Customization,
        Params.BodyEquipment, ComputeDefaultGeosets(Params.Customization),
        ComputeGeosetWithEquipment(Params.Customization, Params.BodyEquipment), HD);
    if (bHD)
    {
        UE_LOG(LogWowCharacter, Log, TEXT("Using HD character model %s"), *HD.ModelPath);
        ModelPath = HD.ModelPath;
        FinalTex = HD.Textures.FindRef(1);
    }
    else if (Params.bUseHDModel)
    {
        UE_LOG(LogWowCharacter, Log, TEXT("HD character model requested but no HD data for %s (put patch-H.MPQ and HD_Data in dist\\Data\\HD)"), *ModelPath);
    }
    // r311 (spreading new players' texture work): other players' gear atlas is composited on a
    // worker thread. The model spawns with the cached plain-skin atlas, or a neutral stand-in,
    // and its body materials switch to the dressed atlas when the job is done.
    bool bAsyncAtlas = Params.bAsyncEquipmentAtlas && !bHD && !FinalTex && HasBodyOverlays(E);
    // r566: a player without body gear needs only the plain skin atlas. Built here it cost ~75 ms
    // ("character-atlas-pixels") the first time a skin was seen; use the cached one, or build it on
    // the worker like the dressed atlas.
    if (Params.bAsyncEquipmentAtlas && !bHD && !FinalTex && !bAsyncAtlas)
    {
        if (UTexture2D* Naked = Cache ? Cache->FindTexture(FWowCharacterTexture::MakeCompositeCacheKey(Params.Customization)) : nullptr)
            FinalTex = Naked;
        else
            bAsyncAtlas = true;
    }
    UTexture2D* AsyncPlaceholder = nullptr;
    TArray<uint8> AsyncBasePixels;
    int32 AsyncWidth = 0, AsyncHeight = 0;
    if (bAsyncAtlas)
    {
        UTexture2D* Naked = Cache ? Cache->FindTexture(FWowCharacterTexture::MakeCompositeCacheKey(Params.Customization)) : nullptr;
        if (auto* Data = Naked ? Naked->GetPlatformData() : nullptr)
            if (Data->PixelFormat == PF_R8G8B8A8 && Data->Mips.Num() > 0)
            {
                AsyncWidth = Naked->GetSizeX(); AsyncHeight = Naked->GetSizeY();
                const void* Source = Data->Mips[0].BulkData.LockReadOnly();
                if (Source && Data->Mips[0].BulkData.GetBulkDataSize() >= int64(AsyncWidth) * AsyncHeight * 4)
                {
                    AsyncBasePixels.SetNumUninitialized(AsyncWidth * AsyncHeight * 4);
                    FMemory::Memcpy(AsyncBasePixels.GetData(), Source, AsyncBasePixels.Num());
                }
                Data->Mips[0].BulkData.Unlock();
            }
        AsyncPlaceholder = !AsyncBasePixels.IsEmpty() ? Naked : GetPendingAtlasPlaceholder();
        if (!AsyncPlaceholder) bAsyncAtlas = false;
        else FinalTex = AsyncPlaceholder;
    }
    // A live finished atlas already includes skin and equipment. Avoid recreating
    // the temporary base atlas when only a mount/actor changed.
    UTexture2D* CompositeTex = FinalTex ? nullptr :
        FWowCharacterTexture::BuildCompositeTexture(Mpq, Cache, Params.Customization);
    if (!FinalTex && CompositeTex)
    {
        FinalTex = CompositeTex;
        auto* Data = CompositeTex->GetPlatformData();
        if (Data && Data->PixelFormat == PF_R8G8B8A8 && Data->Mips.Num() > 0)
        {
            const int32 Width=CompositeTex->GetSizeX(), Height=CompositeTex->GetSizeY();
            TArray<uint8> Pixels;
            const void* Source=Data->Mips[0].BulkData.LockReadOnly();
            if (Source && Data->Mips[0].BulkData.GetBulkDataSize() >= int64(Width)*Height*4)
            { Pixels.SetNumUninitialized(Width*Height*4); FMemory::Memcpy(Pixels.GetData(),Source,Pixels.Num()); }
            Data->Mips[0].BulkData.Unlock();
            if (!Pixels.IsEmpty())
            {
                FWowCharacterTexture::ApplyEquipmentOverlays(Pixels,Width,Height,E,Mpq,Cache);
                if (auto* Equipped=UTexture2D::CreateTransient(Width,Height,PF_R8G8B8A8))
                {
                    Equipped->Filter=TF_Bilinear; Equipped->SRGB=true; Equipped->NeverStream=true;
                    auto& Mip=Equipped->GetPlatformData()->Mips[0];
                    void* Destination=Mip.BulkData.Lock(LOCK_READ_WRITE);
                    if (Destination) FMemory::Memcpy(Destination,Pixels.GetData(),Pixels.Num());
                    Mip.BulkData.Unlock();
                    Equipped->UpdateResource(); FinalTex=Equipped;
                    // Weak references do not retain appearances after their actors disappear.
                    if (EquippedTextureCache.Num() >= 1024) EquippedTextureCache.Reset();
                    EquippedTextureCache.Add(AppearanceKey,Equipped);
                }
            }
        }
    }

    // Spawn base character with geoset visibility and equipment
    AActor* Actor = SpawnM2Actor(World, Mpq, Cache, ModelPath, Location, Rotation, 1.0f, FinalTex, &Params.Customization, &Params.BodyEquipment,
        bHD ? &HD.Textures : nullptr, Params.bLoadAnimationsOnDemand, 0, bHD ? &HD.Geosets : nullptr);
    if (!Actor) return nullptr;

    if (bAsyncAtlas)
    {
        FWowCharacterTexture::BeginAsyncWork();
        TWeakObjectPtr<AActor> WeakActor(Actor);
        TWeakObjectPtr<UTexture2D> WeakPlaceholder(AsyncPlaceholder);
        const FWowCharacterTexture::FCustomization Customization = Params.Customization;
        Async(EAsyncExecution::ThreadPool, [Mpq, Customization, E, AppearanceKey, WeakActor, WeakPlaceholder,
            Pixels = MoveTemp(AsyncBasePixels), Width = AsyncWidth, Height = AsyncHeight]() mutable
        {
            uint32 W = uint32(Width), H = uint32(Height);
            bool bOk = !Pixels.IsEmpty() || FWowCharacterTexture::BuildCompositePixels(Mpq, Customization, Pixels, W, H);
            if (bOk) FWowCharacterTexture::ApplyEquipmentOverlays(Pixels, W, H, E, Mpq, nullptr);
            FWowCharacterTexture::EndAsyncWork(); // no MPQ access past this point
            if (!bOk) return;
            AsyncTask(ENamedThreads::GameThread, [Pixels = MoveTemp(Pixels), W, H, AppearanceKey, WeakActor, WeakPlaceholder]()
            {
                UTexture2D* Dressed = EquippedTextureCache.FindRef(AppearanceKey).Get();
                if (!Dressed)
                {
                    Dressed = CreateAtlasTexture(Pixels, int32(W), int32(H));
                    if (!Dressed) return;
                    if (EquippedTextureCache.Num() >= 1024) EquippedTextureCache.Reset();
                    EquippedTextureCache.Add(AppearanceKey, Dressed);
                }
                SwapBodyAtlas(WeakActor.Get(), WeakPlaceholder.Get(), Dressed);
            });
        });
    }

    // Attach equipment
    if (Params.Equipment.Num() > 0)
    {
        FWowPerformanceScope AttachTiming(TEXT("equipment-attach"), ModelPath, 8.0); // r566: diagnostics
        USkeletalMeshComponent* MeshComp = Actor->FindComponentByClass<USkeletalMeshComponent>();
        if (MeshComp)
        {
            // Get the character's M2 data for attachment point bone lookup
            FString NormPath = ModelPath.ToLower();
            NormPath.ReplaceInline(TEXT("/"), TEXT("\\"));

            TSharedPtr<FM2Data> CharM2;
            {
                FScopeLock Lock(&CharCacheLock);
                TSharedPtr<FM2Data>* Found = CharM2Cache.Find(NormPath);
                if (Found) CharM2 = *Found;
            }

            if (CharM2)
            {
                for (const FEquipmentSlot& Slot : Params.Equipment)
                {
                    if (Slot.ItemDisplayId == 0) continue;

                    USceneComponent* EquipComp = FWowEquipmentManager::AttachEquipment(
                        Mpq, Cache, Slot.ItemDisplayId, Slot.AttachPoint, MeshComp, *CharM2,
                        Slot.EnchantmentVisualId ? Slot.EnchantmentVisualId : FWowEquipmentManager::ResolveEnchantmentVisual(Mpq, Slot.PackedEnchantments));

                    if (EquipComp)
                    {
                        const uint32 AttachmentId = static_cast<uint32>(Slot.AttachPoint);
                        if (AttachmentId <= 2)
                        {
                            EquipComp->ComponentTags.AddUnique(TEXT("WowHeldWeapon"));
                            const int32 InventorySlot = Slot.bRangedWeapon ? 17 : (AttachmentId == 1 ? 15 : 16);
                            EquipComp->ComponentTags.AddUnique(FName(*FString::Printf(TEXT("WowInventorySlot=%d"), InventorySlot)));
                            const FName DrawSocket = EquipComp->GetAttachSocketName();
                            if (!DrawSocket.IsNone())
                            {
                                EquipComp->ComponentTags.AddUnique(FName(*FString::Printf(
                                    TEXT("WowDrawSocket=%s"), *DrawSocket.ToString())));
                            }
                            if (Slot.bRangedWeapon)
                            {
                                EquipComp->ComponentTags.AddUnique(TEXT("WowRangedWeapon"));
                            }
                            FName SheathSocket = GetSheathSocketTag(Slot, ResolveEquipmentSheathType(Slot));
                            // The weapon's own model name (NormPath below is the character model, so the
                            // "_2h_" name checks never matched a weapon before r248).
                            FString WeaponModel;
                            if (const FItemDisplayInfoDbcEntry* WeaponDisplay = FDbcStore::Get().ItemDisplayInfo().GetById(Slot.ItemDisplayId))
                                WeaponModel = WeaponDisplay->ModelNames[0].ToLower();
                            // A two-hander with back sheath type 1 (main/off-hand back sockets, e.g. a bot's
                            // pair of Sword_2H_PvEAlliance) hung blade-up high above the head (Stefan, r248):
                            // the two-hand back transform is authored for the large-weapon diagonal.
                            {
                                const uint32 SheathSubclass = ResolveEquipmentSubclassId(Slot);
                                const bool bTwoHander = Slot.InventoryType == 17 || WeaponModel.Contains(TEXT("_2h_")) ||
                                    SheathSubclass == 1 || SheathSubclass == 5 || SheathSubclass == 8;
                                const bool bLeftHand = Slot.AttachPoint == FWowEquipmentManager::EAttachmentPoint::LeftHand;
                                if (bTwoHander && !EquipComp->ComponentHasTag(TEXT("WowStaffWeapon")) &&
                                    (SheathSocket == TEXT("WowSheathMainHand") || SheathSocket == TEXT("WowSheathOffHand")))
                                    SheathSocket = bLeftHand ? TEXT("WowSheathLargeWeaponRight") : TEXT("WowSheathLargeWeaponLeft");
                            }
                            // Fishing poles hang on the back like a staff (r256, Stefan: the pole stuck out
                            // sideways from the waist with the generic two-hand transform).
                            if (WeaponModel.Contains(TEXT("fishingpole")) || ResolveEquipmentSubclassId(Slot) == 20)
                            {
                                const bool bPoleLeftHand = Slot.AttachPoint == FWowEquipmentManager::EAttachmentPoint::LeftHand;
                                // r305 (Stefan's OG screenshots): seen from behind the top end is over the
                                // LEFT shoulder and the foot at the right hip; ours was mirrored.
                                SheathSocket = bPoleLeftHand ? TEXT("WowStaffBackLeft") : TEXT("WowStaffBackRight");
                                EquipComp->ComponentTags.AddUnique(TEXT("WowSheathFishingPole")); EquipComp->ComponentTags.AddUnique(TEXT("WowSheathTwoHandWeapon"));
                            }
                            // r338 (Stefan: a polearm on the back lay flat across the shoulders): polearms are
                            // held in the middle like a staff and hang diagonally on the back the same way.
                            if (!EquipComp->ComponentHasTag(TEXT("WowStaffWeapon")) &&
                                (WeaponModel.Contains(TEXT("polearm_")) || ResolveEquipmentSubclassId(Slot) == 6))
                            {
                                const bool bPolearmLeftHand = Slot.AttachPoint == FWowEquipmentManager::EAttachmentPoint::LeftHand;
                                SheathSocket = bPolearmLeftHand ? TEXT("WowStaffBackLeft") : TEXT("WowStaffBackRight");
                                EquipComp->ComponentTags.AddUnique(TEXT("WowSheathPolearm"));
                                EquipComp->ComponentTags.AddUnique(TEXT("WowSheathTwoHandWeapon"));
                            }
                            if (EquipComp->ComponentHasTag(TEXT("WowStaffWeapon")))
                            {
                                // r305: mirrored like the fishing pole (OG: top over the left shoulder).
                                if (SheathSocket == TEXT("WowSheathLargeWeaponLeft")) SheathSocket = TEXT("WowStaffBackRight");
                                else if (SheathSocket == TEXT("WowSheathLargeWeaponRight")) SheathSocket = TEXT("WowStaffBackLeft");
                            }
                            if (!SheathSocket.IsNone())
                            {
                                EquipComp->ComponentTags.AddUnique(SheathSocket);
                                // Other players' items often arrive without an inventory type or
                                // an Item.dbc row (custom items): fall back to the model name
                                // (Axe_2H_*, Sword_2H_*), or their axes hung head-up.
                                const bool bModel2H = WeaponModel.Contains(TEXT("_2h_"));
                                const uint32 Subclass = ResolveEquipmentSubclassId(Slot);
                                const bool bDbc2H = Slot.InventoryType == 0 && (Subclass == 1 || Subclass == 5 || Subclass == 8);
                                UE_LOG(LogWowCharacter, Log, TEXT("Sheath: %s socket=%s subclass=%d invtype=%d"), *WeaponModel,
                                    *SheathSocket.ToString(), Subclass == MAX_uint32 ? -1 : int32(Subclass), int32(Slot.InventoryType));
                                if (Slot.InventoryType == 17 || bModel2H || bDbc2H)
                                {
                                    EquipComp->ComponentTags.AddUnique(TEXT("WowSheathTwoHandWeapon"));
                                    // The model name is authoritative when it names the weapon type:
                                    // several Item.dbc rows share one display id (1H and 2H variants,
                                    // custom-realm copies), so the first subclass match can be wrong
                                    // and the axe then hung head-up on the back.
                                    // r511 (Stefan: a night elf's Hammer_2H_Standard_D_03 lay flat across her
                                    // shoulders): two-hand hammers got the mace roll (-75) and lay level. They
                                    // hang diagonally like the two-hand axes, head down.
                                    // r537 (Stefan: a gnome with two two-hand maces (Mace_2H_Stratholme_D_01) had both
                                    // on the back spread sideways at shoulder height): "mace_2h" models had the
                                    // same flat -75 roll as the hammers before r511. They hang like the axes too,
                                    // head down, diagonally, and a pair crosses toward the spine.
                                    const bool bPathAxe = WeaponModel.Contains(TEXT("axe_2h")) || WeaponModel.Contains(TEXT("hammer_2h"))
                                        || WeaponModel.Contains(TEXT("mace_2h"));
                                    const bool bPathSword = WeaponModel.Contains(TEXT("sword_2h"));
                                    const bool bPathKnown = bPathAxe || bPathSword || WeaponModel.Contains(TEXT("mace_2h")) ||
                                        WeaponModel.Contains(TEXT("staff_")) || WeaponModel.Contains(TEXT("polearm_"));
                                    if (bPathAxe || (!bPathKnown && (Subclass == 1 || Subclass == 5)))
                                        EquipComp->ComponentTags.AddUnique(TEXT("WowSheathTwoHandAxe"));
                                    // r714 (Stefan, a night elf's two-hand mace from behind: "needs to have the head in the
                                    // other end"): maces and hammers hang head down; only axes go head up (r693).
                                    if (WeaponModel.Contains(TEXT("hammer_2h")) || WeaponModel.Contains(TEXT("mace_2h"))
                                        || (!bPathKnown && Subclass == 5))
                                        EquipComp->ComponentTags.AddUnique(TEXT("WowSheathTwoHandMace"));
                                    if (bPathSword || (!bPathKnown && Subclass == 8))
                                    {
                                        EquipComp->ComponentTags.AddUnique(TEXT("WowSheathTwoHandSword"));
                                    }
                                }
                            }
                        }
                        UE_LOG(LogWowCharacter, Log, TEXT("Equipped item DisplayID=%d at attachment %d"),
                            Slot.ItemDisplayId, static_cast<uint32>(Slot.AttachPoint));
                    }
                }
            }
            else
            {
                UE_LOG(LogWowCharacter, Warning, TEXT("No cached M2 data for equipment attachment: %s"), *NormPath);
            }
        }
    }

    return Actor;
}

bool FWowCharacterBuilder::BuildNpcCharacterParams(uint32 DisplayId, FCharacterParams& OutParams)
{
    const FDbcStore& Dbc = FDbcStore::Get();
    const FCreatureDisplayInfoDbcEntry* DisplayInfo = Dbc.CreatureDisplayInfo().GetById(DisplayId);
    if (!DisplayInfo || DisplayInfo->ExtendedDisplayInfoID == 0) return false;
    const FCreatureDisplayInfoExtraDbcEntry* Extra = Dbc.CreatureDisplayInfoExtra().GetById(DisplayInfo->ExtendedDisplayInfoID);
    if (!Extra) return false;
    const bool bPlayableRace = (Extra->DisplayRaceID >= 1 && Extra->DisplayRaceID <= 8) ||
        Extra->DisplayRaceID == 10 || Extra->DisplayRaceID == 11;
    if (!bPlayableRace || Extra->DisplaySexID > 1) return false;

    FCharacterParams Params;
    Params.Race = static_cast<ERace>(Extra->DisplayRaceID);
    Params.Gender = static_cast<EGender>(Extra->DisplaySexID);
    Params.bLoadAnimationsOnDemand = true;
    Params.Customization.RaceId = Extra->DisplayRaceID;
    Params.Customization.Gender = Extra->DisplaySexID;
    Params.Customization.SkinColor = Extra->SkinID;
    Params.Customization.FaceVariation = Extra->FaceID;
    Params.Customization.HairStyle = Extra->HairStyleID;
    Params.Customization.HairColor = Extra->HairColorID;
    Params.Customization.FacialHairStyle = Extra->FacialHairID;
    Params.BodyEquipment.RaceId = Extra->DisplayRaceID;
    Params.BodyEquipment.Gender = Extra->DisplaySexID;
    Params.BodyEquipment.ShirtDisplayId = Extra->NPCItemDisplay[2];
    Params.BodyEquipment.ChestDisplayId = Extra->NPCItemDisplay[3];
    Params.BodyEquipment.BeltDisplayId = Extra->NPCItemDisplay[4];
    Params.BodyEquipment.PantsDisplayId = Extra->NPCItemDisplay[5];
    Params.BodyEquipment.BootsDisplayId = Extra->NPCItemDisplay[6];
    Params.BodyEquipment.BracersDisplayId = Extra->NPCItemDisplay[7];
    Params.BodyEquipment.GlovesDisplayId = Extra->NPCItemDisplay[8];
    Params.BodyEquipment.TabardDisplayId = Extra->NPCItemDisplay[9];
    Params.BodyEquipment.CapeDisplayId = Extra->NPCItemDisplay[10];
    if (Extra->NPCItemDisplay[0] != 0)
    {
        FEquipmentSlot Helmet;
        Helmet.ItemDisplayId = Extra->NPCItemDisplay[0];
        Helmet.InventoryType = 1;
        Helmet.AttachPoint = FWowEquipmentManager::EAttachmentPoint::Helmet;
        Params.Equipment.Add(Helmet);
    }
    if (Extra->NPCItemDisplay[1] != 0)
    {
        FEquipmentSlot Shoulder;
        Shoulder.ItemDisplayId = Extra->NPCItemDisplay[1];
        Shoulder.InventoryType = 3;
        Shoulder.AttachPoint = FWowEquipmentManager::EAttachmentPoint::LeftShoulder;
        Params.Equipment.Add(Shoulder);
        Shoulder.AttachPoint = FWowEquipmentManager::EAttachmentPoint::RightShoulder;
        Params.Equipment.Add(Shoulder);
    }
    OutParams = MoveTemp(Params);
    return true;
}

AActor* FWowCharacterBuilder::SpawnCreatureByDisplayId(UWorld* World, FMpqManager* Mpq, FWowAssetCache* Cache,
    uint32 DisplayId, const FVector& Location, const FRotator& Rotation, bool bLoadAnimationsOnDemand)
{
    const FDbcStore& Dbc = FDbcStore::Get();
    const FCreatureDisplayInfoDbcEntry* DisplayInfo = Dbc.CreatureDisplayInfo().GetById(DisplayId);
    if (!DisplayInfo)
    {
        UE_LOG(LogWowCharacter, Warning, TEXT("Unknown CreatureDisplayInfo ID %d"), DisplayId);
        return nullptr;
    }

    const FCreatureModelDataDbcEntry* ModelData = Dbc.CreatureModelData().GetById(DisplayInfo->ModelID);
    if (!ModelData)
    {
        UE_LOG(LogWowCharacter, Warning, TEXT("Unknown CreatureModelData ID %d for display %d"),
            DisplayInfo->ModelID, DisplayId);
        return nullptr;
    }

    FString ModelPath = ModelData->ModelPath;
    if (ModelPath.IsEmpty()) return nullptr;

    if (ModelPath.EndsWith(TEXT(".mdx"), ESearchCase::IgnoreCase))
    {
        ModelPath = ModelPath.Left(ModelPath.Len() - 4) + TEXT(".m2");
    }

    float Scale = DisplayInfo->Scale * ModelData->Scale;
    // Use the same readable young-wolf size across starting areas. Preserve
    // larger adult/boss variants, replacement skins and authoritative actor scale.
    if (ModelPath.Equals(TEXT("Creature\\Wolf\\Wolf.m2"), ESearchCase::IgnoreCase))
        Scale = FMath::Max(Scale, 0.60f);
    UE_LOG(LogWowCharacter, Log, TEXT("Spawning creature: DisplayID=%d Model=%s Scale=%.2f"),
        DisplayId, *ModelPath, Scale);

    // Humanoid NPC displays do not store their body skin on the M2. Their
    // appearance lives in CreatureDisplayInfoExtra, usually as a pre-baked NPC
    // texture plus the customization values needed for character geosets.
    if (DisplayInfo->ExtendedDisplayInfoID != 0)
    {
        if (const FCreatureDisplayInfoExtraDbcEntry* Extra =
            Dbc.CreatureDisplayInfoExtra().GetById(DisplayInfo->ExtendedDisplayInfoID))
        {
            const bool bPlayableRace =
                (Extra->DisplayRaceID >= 1 && Extra->DisplayRaceID <= 8) ||
                Extra->DisplayRaceID == 10 || Extra->DisplayRaceID == 11;
            const bool bKnownGender = Extra->DisplaySexID <= 1;
            // r521 (Stefan: the Frostmane Troll Whelps in Coldridge are white figures): display 27502 is
            // the ice troll model (race 21, not playable) dressed only by its baked NPC texture
            // (CreatureDisplayExtra-18314.blp); the playable-race test sent it down the bare-model path.
            // A non-playable race with a baked texture uses this path too (the hair/face tables have race 21).
            if ((bPlayableRace || !Extra->BakeName.IsEmpty()) && bKnownGender)
            {
                FWowCharacterTexture::FCustomization Customization;
                Customization.RaceId = Extra->DisplayRaceID;
                Customization.Gender = Extra->DisplaySexID;
                Customization.SkinColor = Extra->SkinID;
                Customization.FaceVariation = Extra->FaceID;
                Customization.HairStyle = Extra->HairStyleID;
                Customization.HairColor = Extra->HairColorID;
                Customization.FacialHairStyle = Extra->FacialHairID;

                // CreatureDisplayInfoExtra armor order in 3.3.5 is head,
                // shoulders, shirt, chest, belt, legs, boots, wrists, gloves,
                // tabard, and cape. Body slots affect texture/geoset selection;
                // head/shoulder attachment geometry is a separate later pass.
                FBodyEquipment BodyEquipment;
                BodyEquipment.RaceId = Extra->DisplayRaceID;
                BodyEquipment.Gender = Extra->DisplaySexID;
                BodyEquipment.ShirtDisplayId = Extra->NPCItemDisplay[2];
                BodyEquipment.ChestDisplayId = Extra->NPCItemDisplay[3];
                BodyEquipment.BeltDisplayId = Extra->NPCItemDisplay[4];
                BodyEquipment.PantsDisplayId = Extra->NPCItemDisplay[5];
                BodyEquipment.BootsDisplayId = Extra->NPCItemDisplay[6];
                BodyEquipment.BracersDisplayId = Extra->NPCItemDisplay[7];
                BodyEquipment.GlovesDisplayId = Extra->NPCItemDisplay[8];
                BodyEquipment.TabardDisplayId = Extra->NPCItemDisplay[9];
                BodyEquipment.CapeDisplayId = Extra->NPCItemDisplay[10];

                UTexture2D* TextureOverride = ResolveCreatureTextureOverride(
                    Mpq, Cache, ModelPath, Extra->BakeName);
                if (!TextureOverride)
                {
                    TextureOverride = ResolveCreatureTextureOverride(
                        Mpq, Cache, ModelPath, DisplayInfo->Texture1);
                }
                if (!TextureOverride)
                {
                    TextureOverride = FWowCharacterTexture::BuildCompositeTexture(
                        Mpq, Cache, Customization);
                }

                UE_LOG(LogWowCharacter, Log,
                    TEXT("Using NPC appearance extra %u for display %u: race=%u gender=%u baked=%s texture=%s"),
                    DisplayInfo->ExtendedDisplayInfoID, DisplayId,
                    Extra->DisplayRaceID, Extra->DisplaySexID,
                    *Extra->BakeName, TextureOverride ? TEXT("resolved") : TEXT("missing"));

                return SpawnM2Actor(World, Mpq, Cache, ModelPath, Location, Rotation,
                    Scale, TextureOverride, &Customization, &BodyEquipment, nullptr, bLoadAnimationsOnDemand);
            }
        }
    }

    // A playable-race character model with neither textures nor appearance extra (custom
    // server NPCs often use such displays): the client draws the race's default look
    // (skin, face and hair 0) instead of an untextured white figure.
    if (DisplayInfo->Texture1.IsEmpty() && DisplayInfo->Texture2.IsEmpty() && DisplayInfo->Texture3.IsEmpty())
    {
        static const TPair<const TCHAR*, uint8> RaceFolders[] = {
            {TEXT("Human"), 1}, {TEXT("Orc"), 2}, {TEXT("Dwarf"), 3}, {TEXT("NightElf"), 4}, {TEXT("Scourge"), 5},
            {TEXT("Tauren"), 6}, {TEXT("Gnome"), 7}, {TEXT("Troll"), 8}, {TEXT("Goblin"), 9}, {TEXT("BloodElf"), 10}, {TEXT("Draenei"), 11} };
        TArray<FString> Parts;
        ModelPath.Replace(TEXT("/"), TEXT("\\")).ParseIntoArray(Parts, TEXT("\\"), true);
        if (Parts.Num() >= 4 && Parts[0].Equals(TEXT("Character"), ESearchCase::IgnoreCase))
        {
            uint8 RaceId = 0;
            for (const auto& Race : RaceFolders) if (Parts[1].Equals(Race.Key, ESearchCase::IgnoreCase)) RaceId = Race.Value;
            const int32 Gender = Parts[2].Equals(TEXT("Male"), ESearchCase::IgnoreCase) ? 0
                : Parts[2].Equals(TEXT("Female"), ESearchCase::IgnoreCase) ? 1 : -1;
            if (RaceId != 0 && Gender >= 0)
            {
                FWowCharacterTexture::FCustomization Customization;
                Customization.RaceId = RaceId;
                Customization.Gender = uint8(Gender);
                FBodyEquipment BodyEquipment;
                BodyEquipment.RaceId = RaceId;
                BodyEquipment.Gender = uint8(Gender);
                UTexture2D* DefaultSkin = FWowCharacterTexture::BuildCompositeTexture(Mpq, Cache, Customization);
                UE_LOG(LogWowCharacter, Log, TEXT("Display %u: character model without textures/extra, default race %u gender %d look (%s)"),
                    DisplayId, RaceId, Gender, DefaultSkin ? TEXT("resolved") : TEXT("missing"));
                if (DefaultSkin)
                    return SpawnM2Actor(World, Mpq, Cache, ModelPath, Location, Rotation,
                        Scale, DefaultSkin, &Customization, &BodyEquipment, nullptr, bLoadAnimationsOnDemand);
            }
        }
    }

    UTexture2D* TextureOverride = ResolveCreatureTextureOverride(
        Mpq, Cache, ModelPath, DisplayInfo->Texture1);

    TMap<uint32, UTexture2D*> CreatureTextures;
    const FString Names[] = {DisplayInfo->Texture1, DisplayInfo->Texture2, DisplayInfo->Texture3};
    for (uint32 Index = 0; Index < 3; ++Index)
        if (UTexture2D* Texture = ResolveCreatureTextureOverride(Mpq, Cache, ModelPath, Names[Index]))
            CreatureTextures.Add(11 + Index, Texture);
    {
        // Diagnostics for grey/untextured creatures: once per display, what the DBC asked for and what loaded.
        static TSet<uint32> Reported;
        bool bMissing = false;
        for (uint32 Index = 0; Index < 3; ++Index) bMissing |= !Names[Index].IsEmpty() && !CreatureTextures.Contains(11 + Index);
        if ((bMissing || CreatureTextures.Num() == 0) && !Reported.Contains(DisplayId))
        {
            Reported.Add(DisplayId);
            UE_LOG(LogWowCharacter, Warning, TEXT("Creature display %u (%s): textures '%s' '%s' '%s' -> loaded %d"),
                DisplayId, *ModelPath, *Names[0], *Names[1], *Names[2], CreatureTextures.Num());
        }
    }
    return SpawnM2Actor(World, Mpq, Cache, ModelPath, Location, Rotation, Scale,
        TextureOverride, nullptr, nullptr, &CreatureTextures, bLoadAnimationsOnDemand,
        DisplayInfo->CreatureGeosetData);
}

AActor* FWowCharacterBuilder::SpawnM2Model(UWorld* World, FMpqManager* Mpq, FWowAssetCache* Cache,
    const FString& ModelPath, const FVector& Location, const FRotator& Rotation, float Scale)
{
    return SpawnM2Actor(World, Mpq, Cache, ModelPath, Location, Rotation, Scale);
}

AActor* FWowCharacterBuilder::SpawnM2Actor(UWorld* World, FMpqManager* Mpq, FWowAssetCache* Cache,
    const FString& ModelPath, const FVector& Location, const FRotator& Rotation, float Scale,
    UTexture2D* OverrideTexture, const FWowCharacterTexture::FCustomization* Customization,
    const FBodyEquipment* BodyEquipment, const TMap<uint32, UTexture2D*>* CreatureTextures, bool bLoadAnimationsOnDemand,
    uint32 CreatureGeosetData, const TMap<uint16, uint16>* GeosetOverride)
{
    if (!World || !Mpq) return nullptr;
    FWowPerformanceScope Timing(TEXT("model-actor"), ModelPath);

    FString NormPath = ModelPath.ToLower();
    NormPath.ReplaceInline(TEXT("/"), TEXT("\\"));

    // Original and modded login scenes share virtual paths but not geometry,
    // skeletons or animations. Keep all static builder caches profile-specific.
    if (NormPath.StartsWith(TEXT("interface\\glues\\")) &&
        Mpq->GetAssetProfile() == EWowAssetProfile::OriginalWotlk335)
        NormPath = TEXT("original335:") + NormPath;

    // Parse M2
    TSharedPtr<FM2Data> M2Data;
    {
        FScopeLock Lock(&CharCacheLock);
        TSharedPtr<FM2Data>* Found = CharM2Cache.Find(NormPath);
        if (Found) M2Data = *Found;
    }

    if (!M2Data)
    {
        FWowPerformanceScope ReadTiming(TEXT("model-read-parse"), ModelPath);
        TArray<uint8> M2Raw;
        if (!Mpq->ReadFile(ModelPath, M2Raw))
        {
            UE_LOG(LogWowCharacter, Warning, TEXT("Failed to read M2: %s"), *ModelPath);
            return nullptr;
        }

        FString SkinPath = ModelPath;
        if (SkinPath.EndsWith(TEXT(".m2"), ESearchCase::IgnoreCase))
            SkinPath = SkinPath.Left(SkinPath.Len() - 3);
        SkinPath += TEXT("00.skin");

        TArray<uint8> SkinRaw;
        Mpq->ReadFile(SkinPath, SkinRaw);

        M2Data = MakeShared<FM2Data>(FM2Parser::Parse(M2Raw, SkinRaw));
        if (!M2Data->bIsValid)
        {
            UE_LOG(LogWowCharacter, Warning, TEXT("Failed to parse M2: %s"), *ModelPath);
            return nullptr;
        }
        // HD models name their own textures (eye reflection, hardcoded parts) with
        // plain paths that only exist in the HD set.
        if (ModelPath.StartsWith(TEXT("HDX\\"), ESearchCase::IgnoreCase) || ModelPath.StartsWith(TEXT("HDX/"), ESearchCase::IgnoreCase))
            for (FString& TexturePath : M2Data->TexturePaths)
                if (!TexturePath.IsEmpty() && !TexturePath.StartsWith(TEXT("HDX"), ESearchCase::IgnoreCase))
                    TexturePath = TEXT("HDX\\") + TexturePath;

        FScopeLock Lock(&CharCacheLock);
        CharM2Cache.Add(NormPath, M2Data);
    }

    if (M2Data->Vertices.Num() == 0 || M2Data->Indices.Num() == 0)
    {
        UE_LOG(LogWowCharacter, Warning, TEXT("M2 has no geometry: %s"), *ModelPath);
        return nullptr;
    }

    AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Rotation, Location));
    if (!Actor) return nullptr;

    USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
    Actor->SetRootComponent(Root);
    Root->RegisterComponent();

    if (M2Data->HasBones())
    {
        USkeleton* Skeleton = nullptr;
        {
            FScopeLock Lock(&CharCacheLock);
            if (TWeakObjectPtr<USkeleton>* CachedSkel = CharSkeletonCache.Find(NormPath))
            {
                Skeleton = CachedSkel->Get();
            }
        }

        if (!Skeleton)
        {
            Skeleton = FWowSkeletalMeshBuilder::CreateSkeleton(*M2Data, ModelPath);
            if (Skeleton)
            {
                FScopeLock Lock(&CharCacheLock);
                CharSkeletonCache.Add(NormPath, Skeleton);
            }
        }

        // Compute geoset visibility BEFORE building mesh
        TMap<uint16, uint16> VisibleGeosets;
        if (Customization && GeosetOverride)
        {
            VisibleGeosets = *GeosetOverride;
        }
        else if (Customization)
        {
            if (BodyEquipment)
            {
                VisibleGeosets = ComputeGeosetWithEquipment(*Customization, *BodyEquipment);
            }
            else
            {
                VisibleGeosets = ComputeDefaultGeosets(*Customization);
            }
        }
        else if (CreatureGeosetData != 0)
        {
            // CreatureDisplayInfo.CreatureGeosetData: one nibble per geoset group
            // (bits 0-3 -> 1xx, 4-7 -> 2xx ...). A non-zero nibble selects that
            // variant and hides the group's alternatives, as in the original client
            // (murloc fins/heads, kobold candles, etc. otherwise all render at once).
            for (uint16 Group = 1; Group <= 8; ++Group)
            {
                const uint16 Variant = static_cast<uint16>((CreatureGeosetData >> (4 * (Group - 1))) & 0xF);
                if (Variant != 0) VisibleGeosets.Add(Group, Variant);
            }
        }

        // Build BODY mesh (excludes hair sections that need different texture)
        // Hair sections are identified by M2 texture type 6
        TMap<uint16, uint16> BodyGeosets = VisibleGeosets;
        TMap<uint16, uint16> HairOnlyGeosets;
        bool bHasHairSections = false;

        // Check if any render passes reference type 6 (hair) texture
        if (Customization && M2Data->TextureTypes.Num() > 0)
        {
            for (const FM2RenderPass& Pass : M2Data->RenderPasses)
            {
                if (Pass.TextureIndex < static_cast<uint16>(M2Data->TextureTypes.Num()) &&
                    M2Data->TextureTypes[Pass.TextureIndex] == 6)
                {
                    bHasHairSections = true;
                    break;
                }
            }
        }

        // Build body mesh
        const TMap<uint16, uint16>* GeosetFilter = Customization ? &BodyGeosets : nullptr;
        TArray<FGeosetSectionInfo> GeosetInfo;
        USkeletalMesh* SkeletalMesh = nullptr;
        // Geometry depends on model and visible geosets, not on skin/equipment textures.
        FString MeshKey=NormPath;
        if (Customization)
        {
            MeshKey += TEXT("|character");
            TArray<uint16> Groups; BodyGeosets.GetKeys(Groups); Groups.Sort();
            for(uint16 Group:Groups) MeshKey+=FString::Printf(TEXT("|%u=%u"),Group,BodyGeosets[Group]);
        }
        {
            FScopeLock Lock(&CharCacheLock);
            SkeletalMesh=CreatureMeshCache.FindRef(MeshKey).Get();
            if(SkeletalMesh) GeosetInfo=CharGeosetCache.FindRef(MeshKey);
        }
        if (!SkeletalMesh && Skeleton)
        {
            SkeletalMesh = FWowSkeletalMeshBuilder::CreateSkeletalMesh(*M2Data, Skeleton, ModelPath, Mpq, Cache, &GeosetInfo, GeosetFilter);
            if (SkeletalMesh)
            {
                FScopeLock Lock(&CharCacheLock);
                if(CreatureMeshCache.Num() >= 2048) { CreatureMeshCache.Reset(); CharGeosetCache.Reset(); }
                CreatureMeshCache.Add(MeshKey,SkeletalMesh);
                CharGeosetCache.Add(MeshKey,GeosetInfo);
            }
        }
        // r303 (HD character hitch): the caches above are weak, so after a gear change or a
        // relog the big HD (HDX\) mesh and skeleton were often collected and rebuilt from
        // scratch. Keep the last few HD meshes (and their skeleton) alive.
        if (NormPath.Contains(TEXT("hdx\\")) || NormPath.Contains(TEXT("hdx/")))
        {
            static TArray<UObject*> GPinnedHD;
            for (UObject* Keep : { static_cast<UObject*>(SkeletalMesh), static_cast<UObject*>(Skeleton) })
                if (Keep && !GPinnedHD.Contains(Keep))
                {
                    Keep->AddToRoot();
                    GPinnedHD.Add(Keep);
                }
            while (GPinnedHD.Num() > 8)
            {
                if (IsValid(GPinnedHD[0])) GPinnedHD[0]->RemoveFromRoot();
                GPinnedHD.RemoveAt(0);
            }
        }

        if (SkeletalMesh && SkeletalMesh->GetResourceForRendering() &&
            SkeletalMesh->GetResourceForRendering()->LODRenderData.Num() > 0)
        {
            // Resolve skin texture
            UTexture2D* SkinTexture = OverrideTexture;
            if (!SkinTexture && M2Data->TexturePaths.Num() > 0 && !M2Data->TexturePaths[0].IsEmpty())
            {
                SkinTexture = LoadBlpTexture(Mpq, Cache, M2Data->TexturePaths[0]);
            }

            // Create body mesh component. Character bodies normally use the
            // composed skin override, while ordinary M2s (including the glue
            // screen ModelFFX race environments) can have a different direct
            // BLP for every render pass. Applying TexturePaths[0] to every
            // material turns those environments into repeated copies of one
            // surface (for example the wooden-crate texture in UI_Human).
            USkeletalMeshComponent* SkelMesh = NewObject<USkeletalMeshComponent>(Actor, TEXT("CharacterMesh"));
            SkelMesh->SetupAttachment(Root);
            SkelMesh->SetSkeletalMesh(SkeletalMesh);
            SkelMesh->SetWorldScale3D(FVector(Scale));
            SkelMesh->SetCastShadow(true);
            SkelMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            SkelMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
            SkelMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
            SkelMesh->SetCollisionObjectType(ECC_Pawn);

            // Fix M2 model orientation: M2 models face +Y in WoW space, need -90 degree yaw for UE5
            SkelMesh->SetRelativeRotation(FRotator(0, -90, 0));

            const bool bIsGlueModel = ModelPath.StartsWith(
                TEXT("Interface\\Glues\\Models\\UI_"), ESearchCase::IgnoreCase);
            UWowGlueMaterialAnimator* GlueMaterials=nullptr;
            if(bIsGlueModel)
            {
                GlueMaterials=NewObject<UWowGlueMaterialAnimator>(Actor);
                Actor->AddInstanceComponent(GlueMaterials);
                GlueMaterials->RegisterComponent();
            }
            if (UMaterial* DefaultBaseMat = bIsGlueModel ? GetGlueModelMaterial(0) : GetCharacterMaterial())
            {
                for (int32 MatIdx = 0; MatIdx < SkeletalMesh->GetMaterials().Num(); ++MatIdx)
                {
                    UMaterial* BaseMat = DefaultBaseMat;
                    if (bIsGlueModel && GeosetInfo.IsValidIndex(MatIdx))
                    {
                        const FGeosetSectionInfo& Section = GeosetInfo[MatIdx];
                        const FString SectionPath = M2Data->TexturePaths.IsValidIndex(Section.TextureIndex)
                            ? M2Data->TexturePaths[Section.TextureIndex] : FString();
                        // Original unlit flags identify painted sky, windows and glows.
                        // Ordinary surfaces can now receive the same UE light as the player.
                        // Login backdrops contain their painted lighting; a world key light
                        // makes their foreground black. Character selection keeps dynamic lighting.
                        const bool bLoginBackdrop = ModelPath.Contains(TEXT("UI_MainMenu"), ESearchCase::IgnoreCase);
                        // Painted sky and cloud layers carry their own light: a UE key light
                        // turns them flat grey and makes the cloud cards read as stage plates.
                        const bool bSkyLayer = SectionPath.Contains(TEXT("SKY")) || SectionPath.Contains(TEXT("CLOUD"));
                        // Like the original client, every glue layer with a texture animation
                        // scrolls (drifting clouds), not only the login backdrop.
                        const bool bAnimatedLayer = !Section.UVScrollSpeed.IsNearlyZero();
                        const bool bLitSurface = !bLoginBackdrop && !bSkyLayer && !bAnimatedLayer && !(Section.RenderFlags & 1)
                            && Section.BlendMode <= 2
                            && !SectionPath.Contains(TEXT("SHADOW")) && !SectionPath.Contains(TEXT("GRADIENT"));
                        if (UMaterial* BlendMaterial = GetGlueModelMaterial(Section.BlendMode, bLitSurface, bAnimatedLayer))
                        {
                            BaseMat = BlendMaterial;
                        }
                        // r615: a second texture layer (mod2x) on an unlit see-through or keyed pass.
                        if (!bLitSurface && Section.BlendMode >= 1 && Section.BlendMode <= 4
                            && M2Data->RenderPasses.IsValidIndex(Section.RenderPassIndex)
                            && M2Data->TexturePaths.IsValidIndex(M2Data->RenderPasses[Section.RenderPassIndex].TextureIndex2))
                            if (UMaterial* TwoLayer = GetGlueTwoLayerMaterial(Section.BlendMode))
                                BaseMat = TwoLayer;
                    }
                    // Creature overlay passes (env-map sheen such as CRABREFLECT / ARMORREFLECT, additive
                    // glows) are a second pass over the same skin: drawn opaque they cover the skin
                    // and the creature looks grey/untextured.
                    float OverlayTint = -1.0f;
                    float ModulateReflectionOpacity = -1.0f; // r480
                    if (!bIsGlueModel && GeosetInfo.IsValidIndex(MatIdx))
                    {
                        const FGeosetSectionInfo& Section = GeosetInfo[MatIdx];
                        const FString OverlayPath = M2Data->TexturePaths.IsValidIndex(Section.TextureIndex)
                            ? M2Data->TexturePaths[Section.TextureIndex] : FString();
                        const bool bReflection = OverlayPath.Contains(TEXT("REFLECT")) || OverlayPath.Contains(TEXT("ENVMAP"));
                        // r394: the HD models' eye highlight ("..._eyereflect.blp", blend 2) is a white
                        // texture whose alpha holds the small glint. Drawn additive it ignored the alpha
                        // and laid a glassy disc over each eye (the HD gnome looked like it wore goggles);
                        // alpha blending keeps only the glint, as in WoW.
                        const bool bEyeGlint = Section.BlendMode == 2 && OverlayPath.Contains(TEXT("EYEREFLECT"));
                        if (bEyeGlint)
                        {
                            if (UMaterial* Translucent = GetGlueModelMaterial(2, false))
                            {
                                BaseMat = Translucent;
                                OverlayTint = 1.0f;
                            }
                        }
                        else if (bReflection || Section.BlendMode >= 3)
                            if (UMaterial* Additive = GetGlueModelMaterial(Section.BlendMode >= 5 ? Section.BlendMode : 4, false))
                            {
                                BaseMat = Additive;
                                OverlayTint = bReflection ? 0.16f : 1.0f; // faint sheen, glows at full strength
                                // r480 (Stefan: another player's Great Elite Elekk was "VERY dark"): its armour's
                                // ORBREFLECT02 pass is Mod2x (blend 6), drawn with the body UVs (we have no sphere
                                // mapping). Dimmed to 0.16 like an additive sheen, the modulate pass multiplied
                                // the whole body by ~0.3. A reflection that modulates keeps the full texture but
                                // only a third of its effect: a light sheen around neutral, never a dark body.
                                if (bReflection && Section.BlendMode >= 5) { OverlayTint = 1.0f; ModulateReflectionOpacity = 0.35f; }
                            }
                    }
                    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, SkelMesh);
                    if (OverlayTint >= 0.0f) MID->SetVectorParameterValue(TEXT("PassTint"), FLinearColor(OverlayTint, OverlayTint, OverlayTint, 1.0f));
                    if (ModulateReflectionOpacity >= 0.0f) MID->SetScalarParameterValue(TEXT("PassOpacity"), ModulateReflectionOpacity);
                    // r420 (Stefan: the Voidwalker had a white glow at its base instead of its dark purple
                    // aura): a creature's additive glow pass is coloured by its M2 colour and transparency
                    // tracks (Voidwalker's GENERICGLOW64 card: deep purple 0.14/0/0.55). It was drawn white.
                    if (!bIsGlueModel && OverlayTint == 1.0f && GeosetInfo.IsValidIndex(MatIdx)
                        && GeosetInfo[MatIdx].BlendMode >= 3 && GeosetInfo[MatIdx].BlendMode < 5
                        && M2Data->RenderPasses.IsValidIndex(GeosetInfo[MatIdx].RenderPassIndex))
                    {
                        const FM2RenderPass& Pass = M2Data->RenderPasses[GeosetInfo[MatIdx].RenderPassIndex];
                        const auto First = [](const FM2MaterialTrack& Track) { return Track.Values.Num() > 0 && FMath::IsFinite(Track.Values[0]) ? FMath::Clamp(Track.Values[0], 0.f, 1.f) : 1.f; };
                        const float Strength = First(Pass.ColorAlpha) * First(Pass.TextureWeight);
                        MID->SetVectorParameterValue(TEXT("PassTint"), FLinearColor(First(Pass.ColorRed) * Strength,
                            First(Pass.ColorGreen) * Strength, First(Pass.ColorBlue) * Strength, 1.0f));
                    }
                    if (bIsGlueModel && GeosetInfo.IsValidIndex(MatIdx))
                    {
                        const FVector2D Speed=GeosetInfo[MatIdx].UVScrollSpeed;
                        MID->SetVectorParameterValue(TEXT("UVScrollSpeed"),FLinearColor(Speed.X,Speed.Y,0,0));
                        // r615: the second layer's texture and its own scroll.
                        if (M2Data->RenderPasses.IsValidIndex(GeosetInfo[MatIdx].RenderPassIndex))
                        {
                            const FM2RenderPass& Pass2 = M2Data->RenderPasses[GeosetInfo[MatIdx].RenderPassIndex];
                            if (M2Data->TexturePaths.IsValidIndex(Pass2.TextureIndex2) && !M2Data->TexturePaths[Pass2.TextureIndex2].IsEmpty())
                                if (UTexture2D* Layer2 = LoadBlpTexture(Mpq, Cache, M2Data->TexturePaths[Pass2.TextureIndex2]))
                                {
                                    ApplyGlueTextureWrap(Layer2, M2Data.Get(), Pass2.TextureIndex2); // r627
                                    MID->SetTextureParameterValue(TEXT("Layer2Texture"), Layer2);
                                    MID->SetVectorParameterValue(TEXT("UVScrollSpeed2"), FLinearColor(Pass2.UVScrollSpeed2.X, Pass2.UVScrollSpeed2.Y, 0, 0));
                                }
                        }
                    }
                    if(GlueMaterials && GeosetInfo.IsValidIndex(MatIdx)
                        && M2Data->RenderPasses.IsValidIndex(GeosetInfo[MatIdx].RenderPassIndex))
                        GlueMaterials->Add(SkelMesh,MatIdx,MID,M2Data->RenderPasses[GeosetInfo[MatIdx].RenderPassIndex]);
                    UTexture2D* SectionTexture = SkinTexture;
                    if (GeosetInfo.IsValidIndex(MatIdx))
                    {
                        const uint16 TextureIndex = GeosetInfo[MatIdx].TextureIndex;
                        if (CreatureTextures && M2Data->TextureTypes.IsValidIndex(TextureIndex))
                            if (UTexture2D* const* Replacement = CreatureTextures->Find(M2Data->TextureTypes[TextureIndex]))
                                SectionTexture = *Replacement;
                        // Texture type 2 is the cloak. It is the equipped cape's own texture
                        // (Item\ObjectComponents\Cape), never the body atlas: the HD models
                        // have UV-unwrapped cloaks that showed the whole skin atlas (faces) on the back.
                        if (BodyEquipment && BodyEquipment->CapeDisplayId && M2Data->TextureTypes.IsValidIndex(TextureIndex)
                            && M2Data->TextureTypes[TextureIndex] == 2)
                            if (const FItemDisplayInfoDbcEntry* Cape = FDbcStore::Get().ItemDisplayInfo().GetById(BodyEquipment->CapeDisplayId))
                                for (const FString& Name : Cape->ModelTextures)
                                    if (!Name.IsEmpty())
                                        if (UTexture2D* CapeTexture = LoadBlpTexture(Mpq, Cache,
                                            TEXT("Item\\ObjectComponents\\Cape\\") + Name + TEXT(".blp")))
                                        {
                                            SectionTexture = CapeTexture;
                                            break;
                                        }
                        if ((!OverrideTexture || !M2Data->TextureTypes.IsValidIndex(TextureIndex)
                            || M2Data->TextureTypes[TextureIndex] == 0)
                            && M2Data->TexturePaths.IsValidIndex(TextureIndex)
                            && !M2Data->TexturePaths[TextureIndex].IsEmpty())
                        {
                            if (UTexture2D* DirectTexture = LoadBlpTexture(
                                Mpq, Cache, M2Data->TexturePaths[TextureIndex]))
                            {
                                SectionTexture = DirectTexture;
                            }
                        }

                        if (bIsGlueModel)
                        {
                            ApplyGlueTextureWrap(SectionTexture, M2Data.Get(), TextureIndex); // r627
                            const FString TexturePath = M2Data->TexturePaths.IsValidIndex(TextureIndex)
                                ? M2Data->TexturePaths[TextureIndex]
                                : TEXT("<invalid>");
                            UE_LOG(LogWowCharacter, Log,
                                TEXT("Glue material %d: texture index %u, blend=%u, flags=0x%04x, uvscroll=(%.3f,%.3f), path '%s', loaded=%s"),
                                MatIdx, TextureIndex, GeosetInfo[MatIdx].BlendMode,
                                GeosetInfo[MatIdx].RenderFlags, GeosetInfo[MatIdx].UVScrollSpeed.X,
                                GeosetInfo[MatIdx].UVScrollSpeed.Y, *TexturePath,
                                SectionTexture ? *SectionTexture->GetName() : TEXT("<none>"));
                        }
                    }
                    if (!bIsGlueModel && CreatureTextures && GeosetInfo.IsValidIndex(MatIdx))
                    {
                        // Diagnostic (once per model): which texture each creature section got.
                        static TSet<FString> LoggedCreatureTextures;
                        const FString LogKey = ModelPath + FString::Printf(TEXT("#%d"), MatIdx);
                        if (!LoggedCreatureTextures.Contains(LogKey) && LoggedCreatureTextures.Num() < 4096)
                        {
                            LoggedCreatureTextures.Add(LogKey);
                            const uint16 TextureIndex = GeosetInfo[MatIdx].TextureIndex;
                            UE_LOG(LogWowCharacter, Log, TEXT("Creature texture %s section %d: texIndex %u type %d path '%s' -> %s"),
                                *ModelPath, MatIdx, TextureIndex,
                                M2Data->TextureTypes.IsValidIndex(TextureIndex) ? int32(M2Data->TextureTypes[TextureIndex]) : -1,
                                M2Data->TexturePaths.IsValidIndex(TextureIndex) ? *M2Data->TexturePaths[TextureIndex] : TEXT("<invalid>"),
                                SectionTexture ? *SectionTexture->GetName() : TEXT("<none>"));
                        }
                    }
                    if (SectionTexture)
                    {
                        MID->SetTextureParameterValue(TEXT("BaseTexture"), SectionTexture);
                    }
                    if (bIsGlueModel && ModelPath.Contains(TEXT("UI_Draenei")) && GeosetInfo.IsValidIndex(MatIdx))
                    {
                        const auto& Section = GeosetInfo[MatIdx];
                        const FString GlowPath = M2Data->TexturePaths.IsValidIndex(Section.TextureIndex)
                            ? M2Data->TexturePaths[Section.TextureIndex] : FString();
                        if (Section.BlendMode >= 3 && (GlowPath.EndsWith(TEXT("FIRERING1A.BLP"))
                            || GlowPath.EndsWith(TEXT("DRAENEI_PLATFORM_CRYSTAL_GLOW.BLP"))))
                        {
                            TArray<uint8> Bytes;
                            if (Mpq->ReadFile(GlowPath, Bytes))
                            {
                                FBlpTexture Dimmed = FBlpParser::Parse(Bytes);
                                if (Dimmed.bIsValid && Dimmed.PixelFormat == EBlpPixelFormat::DXT5)
                                {
                                    for (auto& Mip : Dimmed.MipLevels)
                                        for (int32 Block=0; Block+15<Mip.Data.Num(); Block+=16)
                                        { Mip.Data[Block]/=2; Mip.Data[Block+1]/=2; }
                                    if (auto* Glow = FWowTextureFactory::CreateTexture(Dimmed,TEXT("PreviewPlatformGlow")))
                                        MID->SetTextureParameterValue(TEXT("BaseTexture"), Glow);
                                }
                            }
                        }
                    }
                    SkelMesh->SetMaterial(MatIdx, MID);
                }
            }
            SkelMesh->RegisterComponent();

            // Creature particle emitters/ribbons (fire elementals, wisps, voidwalkers, ...)
            // follow the live animated bones of the mesh.
            if (M2Data.IsValid() && (M2Data->ParticleEmitters.Num() > 0 || M2Data->RibbonEmitters.Num() > 0))
            {
                UWowM2ParticleComponent* Particles = NewObject<UWowM2ParticleComponent>(Actor, TEXT("CreatureM2Particles"));
                Particles->SetupAttachment(SkelMesh);
                Actor->AddInstanceComponent(Particles);
                Particles->ComponentTags.Add(TEXT("WowCreatureEffect"));
                Particles->RegisterComponent();
                Particles->SetBoneSource(SkelMesh);
                // r730 (Stefan, the "Talent Loadout Keeper" ethereal in Stormwind: big peach-pink puffs on the hands,
                // the OG has a soft light-blue glow): EtherialRobe.m2's emitters use replaceable textures (types
                // 11-13, the creature skins from CreatureDisplayInfo), which have no file path in the model, so the
                // emitters loaded no texture and fell back to untextured orange puffs. The skins already resolved for
                // the body are handed to the particles under a neutral cache key; the shared model data is never
                // changed (a private copy is made only when a replacement is needed).
                TUniquePtr<FM2Data> ParticleData;
                if (CreatureTextures && Cache)
                {
                    auto ResolveSkin = [&](uint16 TextureIndex)
                    {
                        if (!M2Data->TexturePaths.IsValidIndex(TextureIndex) || !M2Data->TextureTypes.IsValidIndex(TextureIndex)
                            || !M2Data->TexturePaths[TextureIndex].IsEmpty())
                            return;
                        UTexture2D* const* Skin = CreatureTextures->Find(M2Data->TextureTypes[TextureIndex]);
                        if (!Skin || !*Skin) return;
                        if (!ParticleData) ParticleData = MakeUnique<FM2Data>(*M2Data);
                        if (!ParticleData->TexturePaths[TextureIndex].IsEmpty()) return;
                        // Neutral name: the particle code picks fire/smoke looks from words in the texture name.
                        const FString Key = FString::Printf(TEXT("CreatureSkin\\skin_%u"), (*Skin)->GetUniqueID());
                        if (!Cache->FindTexture(Key)) Cache->CacheTexture(Key, *Skin);
                        ParticleData->TexturePaths[TextureIndex] = Key;
                    };
                    for (const auto& Emitter : M2Data->ParticleEmitters) ResolveSkin(Emitter.Texture);
                    for (const auto& Ribbon : M2Data->RibbonEmitters) ResolveSkin(Ribbon.Texture);
                    static TSet<FString> LoggedSkinParticles;
                    if (ParticleData && !LoggedSkinParticles.Contains(ModelPath) && LoggedSkinParticles.Num() < 512)
                    {
                        LoggedSkinParticles.Add(ModelPath);
                        UE_LOG(LogWowCharacter, Log, TEXT("Creature particles %s: replaceable textures resolved from the creature skins"), *ModelPath);
                    }
                }
                Particles->InitializeFromParsedM2Data(ParticleData ? ParticleData.Get() : M2Data.Get(), Mpq, Cache);
            }

            // Apply geoset visibility to hide/show sections based on equipment
            if (GeosetInfo.Num() > 0 && VisibleGeosets.Num() > 0)
            {
                ApplyGeosetVisibility(SkelMesh, GeosetInfo, VisibleGeosets);
            }

            // Build SEPARATE hair mesh if there are hair-textured sections
            if (bHasHairSections && Customization)
            {
                // Load hair texture
                UTexture2D* HairTexture = CreatureTextures ? CreatureTextures->FindRef(6) : nullptr;
                if (!HairTexture)
                for (const FCharSectionsDbcEntry& Entry : FDbcStore::Get().CharSections().GetAll())
                {
                    if (Entry.RaceID == Customization->RaceId && Entry.SexID == Customization->Gender &&
                        Entry.Type == 3 && Entry.Variation == Customization->HairStyle &&
                        Entry.Color == Customization->HairColor && !Entry.Textures[0].IsEmpty())
                    {
                        HairTexture = LoadBlpTexture(Mpq, Cache, Entry.Textures[0]);
                        break;
                    }
                }

                if (HairTexture)
                {
                    // Build hair-only mesh: include ONLY render passes with texture type 6
                    USkeletalMesh* HairMesh=CharacterHairMeshCache.FindRef(MeshKey).Get();
                    if(!HairMesh)
                    {
                        HairMesh=FWowSkeletalMeshBuilder::CreateSkeletalMeshByTextureType(
                            *M2Data, Skeleton, ModelPath + TEXT("_hair"), Mpq, Cache, 6, GeosetFilter);
                        if(CharacterHairMeshCache.Num() >= 1024) CharacterHairMeshCache.Reset();
                        if(HairMesh) CharacterHairMeshCache.Add(MeshKey,HairMesh);
                    }

                    if (HairMesh)
                    {
                        USkeletalMeshComponent* HairComp = NewObject<USkeletalMeshComponent>(Actor, TEXT("HairMesh"));
                        // Attach to the body itself with an identity offset: leader-pose
                        // bones are in the body's component space, so any later move of
                        // the body (ground conform, foot IK lift, slope tilt) must move
                        // the hair too, or the beard hangs in front of the face.
                        HairComp->SetupAttachment(SkelMesh);
                        HairComp->SetSkeletalMesh(HairMesh);
                        HairComp->SetRelativeTransform(FTransform::Identity);
                        HairComp->SetCastShadow(true);
                        HairComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                        HairComp->SetLeaderPoseComponent(SkelMesh); // Follow body animations
                        // r694/r695: bounds from the body, or a seated or kneeling head leaves the hair's own bounds and is culled.
                        HairComp->bUseBoundsFromLeaderPoseComponent = true;

                        if (UMaterial* BaseMat = GetCharacterMaterial())
                        {
                            for (int32 MatIdx = 0; MatIdx < HairMesh->GetMaterials().Num(); ++MatIdx)
                            {
                                UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, HairComp);
                                MID->SetTextureParameterValue(TEXT("BaseTexture"), HairTexture);
                                HairComp->SetMaterial(MatIdx, MID);
                            }
                        }
                        HairComp->RegisterComponent();
                        UE_LOG(LogWowCharacter, Log, TEXT("Created separate hair mesh with texture: %s"),
                            *HairTexture->GetName());
                    }
                }
            }

            // Do not import an entire race and mount's animation catalogue in
            // their first visible frame. Other world clips are imported on first
            // use and reused by every live actor with this model and skeleton.
            auto LoadAnimation = [M2Data, Mpq, Attempted = MakeShared<TSet<int32>>(), WeakSkeleton = TWeakObjectPtr<USkeleton>(Skeleton), ModelPath, NormPath](int32 Id) -> UAnimSequence*
            {
                USkeleton* AnimSkeleton = WeakSkeleton.Get();
                if (!AnimSkeleton) return nullptr;
                {
                    FScopeLock Lock(&CharCacheLock);
                    const TArray<int32>& Ids = CharAnimIdCache.FindOrAdd(NormPath);
                    const TArray<TWeakObjectPtr<UAnimSequence>>& Clips = CharAnimCache.FindOrAdd(NormPath);
                    const int32 Index = Ids.IndexOfByKey(Id);
                    if (Clips.IsValidIndex(Index) && Clips[Index].IsValid()) return Clips[Index].Get();
                }
                if (Attempted->Contains(Id)) return nullptr;
                // r375 (FPS when characters stream in): a clip the model does not have cost ~50 ms per try
                // (the whole M2 is read and parsed again) and every new actor retried it; the HD human
                // tried six missing clips on each spawn (~300 ms). Remember misses per model.
                static TMap<FString, TSet<int32>> GMissingCharAnims;
                {
                    FScopeLock Lock(&CharCacheLock);
                    if (const TSet<int32>* Missing = GMissingCharAnims.Find(NormPath); Missing && Missing->Contains(Id)) return nullptr;
                }
                // r692 (Stefan: the blood elf never twirls, the night elf never flips on a jump): an id with
                // bits above 16 asks for one variation, (sub id + 1) << 16, instead of the most played one.
                // BloodElfFemale JumpStart 37-0 (25 %) is the twirl, NightElfMale/Female 37-1 the flip.
                const int32 BaseId = Id & 0xFFFF;
                const int32 WantedSub = (Id >> 16) - 1;
                const TSet<int32> RequestedIds = {BaseId};
                if (!M2Data->AnimationTracks.ContainsByPredicate([BaseId](const FM2AnimationData& Clip) { return Clip.AnimationId == BaseId; }))
                {
                    TArray<uint8> Raw;
                    if (Mpq->ReadFile(ModelPath, Raw))
                        FM2Parser::LoadAnimationTracks(*M2Data, Raw,
                            [Mpq, ModelPath](uint16 Anim, uint16 Variant, TArray<uint8>& Out)
                            { return Mpq->ReadFile(ModelPath.LeftChop(3) + FString::Printf(TEXT("%04u-%02u.anim"), Anim, Variant), Out); },
                            &RequestedIds);
                }
                TArray<UAnimSequence*> Imported = FWowSkeletalMeshBuilder::CreateAnimations(
                    *M2Data, AnimSkeleton, ModelPath, &RequestedIds, nullptr, WantedSub < 0);
                if (WantedSub >= 0)
                {
                    // All variations were imported, in track order (CreateAnimations skips empty clips).
                    UAnimSequence* Variation = nullptr;
                    int32 Output = 0;
                    for (const FM2AnimationData& Clip : M2Data->AnimationTracks)
                    {
                        if (Clip.AnimationId != BaseId || Clip.Duration == 0 || Clip.BoneTracks.Num() == 0) continue;
                        if (Clip.SubAnimationId == WantedSub && Imported.IsValidIndex(Output)) Variation = Imported[Output];
                        ++Output;
                    }
                    Imported.Reset();
                    if (Variation) Imported.Add(Variation);
                }
                if (Imported.IsEmpty())
                {
                    UE_LOG(LogWowCharacter, Warning, TEXT("Animation %d could not be imported for %s (tracks=%d)"), Id, *ModelPath,
                        M2Data->AnimationTracks.FilterByPredicate([BaseId](const FM2AnimationData& Clip) { return Clip.AnimationId == BaseId; }).Num());
                    Attempted->Add(Id);
                    { FScopeLock Lock(&CharCacheLock); GMissingCharAnims.FindOrAdd(NormPath).Add(Id); }
                    return nullptr;
                }
                FScopeLock Lock(&CharCacheLock);
                TArray<int32>& Ids = CharAnimIdCache.FindOrAdd(NormPath);
                TArray<TWeakObjectPtr<UAnimSequence>>& Clips = CharAnimCache.FindOrAdd(NormPath);
                const int32 Index = Ids.IndexOfByKey(Id);
                if (Clips.IsValidIndex(Index)) Clips[Index] = Imported[0];
                else { Ids.Add(Id); Clips.Add(Imported[0]); }
                return Imported[0];
            };

            TArray<TWeakObjectPtr<UAnimSequence>> CachedAnimations;
            TArray<int32> CachedAnimationIds;
            auto PreloadAnimation = [&](int32 Id)
            {
                if (UAnimSequence* Animation = LoadAnimation(Id))
                {
                    CachedAnimations.Add(Animation);
                    CachedAnimationIds.Add(Id);
                }
            };
            PreloadAnimation(static_cast<int32>(EWowAnimId::Stand));
            if (!bLoadAnimationsOnDemand)
                for (int32 Id : GetRuntimeCharacterAnimationIds())
                    if (Id != 0) PreloadAnimation(Id);
            if (CachedAnimations.IsEmpty())
            {
                if (UAnimSequence* ReferencePoseAnimation = CreateReferencePoseAnimation(Skeleton, ModelPath))
                {
                    CachedAnimations.Add(ReferencePoseAnimation);
                    CachedAnimationIds.Add(static_cast<int32>(EWowAnimId::Stand));
                }
            }

            // Setup animation controller for state-based animation switching
            if (CachedAnimations.Num() > 0)
            {
                // Convert weak pointers to strong pointers for the animation controller
                TArray<UAnimSequence*> Animations;
                for (const TWeakObjectPtr<UAnimSequence>& WeakAnim : CachedAnimations)
                {
                    if (UAnimSequence* Anim = WeakAnim.Get())
                    {
                        Animations.Add(Anim);
                    }
                }

                if (Animations.Num() > 0)
                {
                    // Create and initialize animation controller
                    UWowAnimationController* AnimController = NewObject<UWowAnimationController>(Actor, TEXT("WowAnimationController"));
                    // r641 (Stefan: crossfade "male human as a test, the rest follows"): the animation
                    // controller blends clip changes on meshes with this tag (WowAnimationController r640).
                    if (ModelPath.Contains(TEXT("HumanMale"), ESearchCase::IgnoreCase))
                        SkelMesh->ComponentTags.AddUnique(FName(TEXT("WowAnimCrossfade")));
                    AnimController->Initialize(SkelMesh, Animations);

                    // Build animation ID map: M2 AnimationId → output animation index
                    // CreateAnimations skips empty tracks, so we need to count which tracks
                    // actually produced output animations
                    TMap<int32, int32> AnimIdMap;
					for (int32 OutputIdx = 0; OutputIdx < CachedAnimationIds.Num(); ++OutputIdx)
                    {
						const int32 AnimationId = CachedAnimationIds[OutputIdx];
						if (OutputIdx < Animations.Num() && !AnimIdMap.Contains(AnimationId))
                        {
							AnimIdMap.Add(AnimationId, OutputIdx);
                        }
                    }
                    AnimController->SetAnimationIdMap(AnimIdMap);
                    AnimController->SetAuthoredMovementSpeeds(M2Data->Animations);
                    AnimController->SetAnimationLoader(MoveTemp(LoadAnimation));

                    Actor->Tags.Add(FName(TEXT("HasWowAnimController")));
                    UE_LOG(LogWowCharacter, Log, TEXT("Created animation controller with %d animations, %d ID mappings"),
                        Animations.Num(), AnimIdMap.Num());
                }
            }
            else
            {
                // Fallback: use legacy single animation mode if no animations available
                int32 AnimIndex = FindPreferredAnimationIndex(*M2Data);
                if (CachedAnimations.IsValidIndex(AnimIndex) && CachedAnimations[AnimIndex].IsValid())
                {
                    SkelMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
                    SkelMesh->PlayAnimation(CachedAnimations[AnimIndex].Get(), true);
                }
            }

            UE_LOG(LogWowCharacter, Log, TEXT("Spawned skeletal M2 actor: %s at %s (scale=%.2f, %d bones, %d anims)"),
                *ModelPath, *Location.ToString(), Scale, M2Data->Bones.Num(), M2Data->AnimationTracks.Num());
            return Actor;
        }
    }

    UProceduralMeshComponent* ProcMesh = NewObject<UProceduralMeshComponent>(Actor, TEXT("CharacterMesh"));
    ProcMesh->SetupAttachment(Root);
    ProcMesh->SetWorldScale3D(FVector(Scale * FWowCoordinate::SCALE));
    ProcMesh->SetCastShadow(true);
    ProcMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // Build mesh section from M2 vertex data
    const int32 NumVerts = M2Data->Vertices.Num();
    const int32 NumIndices = M2Data->Indices.Num();

    TArray<FVector> Vertices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FColor> Colors;
    TArray<int32> Triangles;

    Vertices.SetNum(NumVerts);
    Normals.SetNum(NumVerts);
    UVs.SetNum(NumVerts);
    Colors.SetNum(NumVerts);

    for (int32 i = 0; i < NumVerts; ++i)
    {
        const FM2Vertex& V = M2Data->Vertices[i];
        // Positions in WoW units (yards), component world scale handles UE conversion
        Vertices[i] = FVector(V.Position.Y, V.Position.X, V.Position.Z);
        Normals[i] = FVector(V.Normal.Y, V.Normal.X, V.Normal.Z).GetSafeNormal();
        UVs[i] = FVector2D(V.TexCoord.X, V.TexCoord.Y);
        Colors[i] = FColor::White;
    }

    Triangles.SetNum(NumIndices);
    for (int32 i = 0; i < NumIndices; ++i)
    {
        Triangles[i] = M2Data->Indices[i];
    }

    ProcMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, TArray<FProcMeshTangent>(), false);

    // Load and apply texture
    UTexture2D* TexToApply = OverrideTexture;
    if (!TexToApply && M2Data->TexturePaths.Num() > 0 && !M2Data->TexturePaths[0].IsEmpty())
    {
        TexToApply = LoadBlpTexture(Mpq, Cache, M2Data->TexturePaths[0]);
    }

    ApplyTexturedMaterial(ProcMesh, TexToApply);

    ProcMesh->RegisterComponent();

    UE_LOG(LogWowCharacter, Log, TEXT("Spawned M2 actor: %s at %s (scale=%.1f, %d verts, %d tris)"),
        *ModelPath, *Location.ToString(), Scale, NumVerts, NumIndices / 3);

    return Actor;
}

UWowAnimationController* FWowCharacterBuilder::GetAnimationController(AActor* CharacterActor)
{
    if (!CharacterActor) return nullptr;

    // Check if this actor has an animation controller
    if (!CharacterActor->Tags.Contains(FName(TEXT("HasWowAnimController"))))
    {
        return nullptr;
    }

    // Find the animation controller by name
    return FindObject<UWowAnimationController>(CharacterActor, TEXT("WowAnimationController"));
}
