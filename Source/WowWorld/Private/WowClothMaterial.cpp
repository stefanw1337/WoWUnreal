#include "WowClothMaterial.h"
#include "WowCookedMaterial.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Misc/Paths.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionTruncateLWC.h"
#include "Materials/MaterialExpressionTime.h"
#include "Engine/Texture2D.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWowCloth, Log, All);

namespace
{
const TCHAR* ClothMaterialName = TEXT("M_WowClothObject");
const TCHAR* WindCollectionPath = TEXT("/Game/Materials/MPC_WowWind.MPC_WowWind");

bool ContainsAny(const FString& Text, std::initializer_list<const TCHAR*> Words)
{
    for (const TCHAR* Word : Words) if (Text.Contains(Word)) return true;
    return false;
}

UMaterialParameterCollection* WindCollection()
{
    static TStrongObjectPtr<UMaterialParameterCollection> Collection(LoadObject<UMaterialParameterCollection>(nullptr, WindCollectionPath));
    return Collection.Get();
}
}

namespace WowCloth
{
EKind Classify(const FString& ModelPath, const FString& TexturePath)
{
    FString Model = ModelPath.ToLower(), Texture = TexturePath.ToLower();
    Model.ReplaceInline(TEXT("/"), TEXT("\\")); Texture.ReplaceInline(TEXT("/"), TEXT("\\"));
    const FString File = FPaths::GetBaseFilename(Texture);
    // Things that only sound like cloth: tree canopies, flagstones, "bannerstone" rocks, sailors.
    if (ContainsAny(Model, {TEXT("canopy"), TEXT("flagstone"), TEXT("flagpole_base")}) || ContainsAny(File, {TEXT("flagstone"), TEXT("canopy")}))
        return EKind::None;
    // Poles, frames, ropes and fittings of a banner or tent model stay rigid.
    if (ContainsAny(File, {TEXT("wood"), TEXT("metal"), TEXT("pole"), TEXT("iron"), TEXT("stone"), TEXT("rope"), TEXT("plank"),
            TEXT("trim"), TEXT("bark"), TEXT("brass"), TEXT("gold"), TEXT("steel"), TEXT("bone"), TEXT("rock")}))
        return EKind::None;
    if (ContainsAny(Model, {TEXT("banner"), TEXT("flag"), TEXT("tapestry"), TEXT("pennant"), TEXT("curtain"), TEXT("drape"), TEXT("windsock")})
        || ContainsAny(File, {TEXT("banner"), TEXT("tapestry"), TEXT("pennant"), TEXT("curtain"), TEXT("drape")}))
        return EKind::Hanging;
    if (ContainsAny(Model, {TEXT("tent"), TEXT("awning"), TEXT("pavilion"), TEXT("tarp"), TEXT("canvas")})
        || ContainsAny(File, {TEXT("tent"), TEXT("awning"), TEXT("canvas"), TEXT("tarp")}))
        return EKind::Stretched;
    if (ContainsAny(Model, {TEXT("rug"), TEXT("carpet"), TEXT("blanket"), TEXT("bedroll"), TEXT("pillow"), TEXT("cushion")})
        || ContainsAny(File, {TEXT("cloth"), TEXT("fabric"), TEXT("rug"), TEXT("carpet"), TEXT("blanket"), TEXT("silk"), TEXT("linen")}))
        return EKind::Flat;
    return EKind::None;
}

UMaterial* GetMaterial()
{
    static TStrongObjectPtr<UMaterial> Retained;
    static bool bTried = false;
    if (Retained.IsValid()) return Retained.Get();
    if (bTried) return nullptr;
    bTried = true;
    UMaterial* Material = WowCookedMaterial::Load(ClothMaterialName);
    if (!Material) UE_LOG(LogWowCloth, Warning, TEXT("%s is not cooked; cloth keeps the ordinary object material"), ClothMaterialName);
    Retained.Reset(Material);
    return Material;
}

void Configure(UMaterialInstanceDynamic* MID, EKind Kind, bool bAlphaCut, float LocalMinZ, float LocalHeight)
{
    if (!MID) return;
    MID->SetScalarParameterValue(TEXT("SwayKind"), Kind == EKind::Hanging ? 1.f : Kind == EKind::Stretched ? 2.f : 0.f);
    MID->SetScalarParameterValue(TEXT("AlphaCut"), bAlphaCut ? 1.f : 0.f);
    MID->SetScalarParameterValue(TEXT("ClothMinZ"), LocalMinZ);
    MID->SetScalarParameterValue(TEXT("ClothHeight"), FMath::Max(LocalHeight, 1.f));
}

void SetWind(UWorld* World, const FVector2D& Direction, float Strength)
{
    UMaterialParameterCollection* Collection = WindCollection();
    if (!World || !Collection) return;
    if (UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection))
    {
        const FVector2D Dir = Direction.GetSafeNormal();
        Instance->SetVectorParameterValue(TEXT("WindDir"), FLinearColor(float(Dir.X), float(Dir.Y), 0.f, 0.f));
        Instance->SetScalarParameterValue(TEXT("WindStrength"), FMath::Clamp(Strength, 0.f, 1.f));
    }
}

void Prepare()
{
#if WITH_EDITOR
    const FString CollectionName = TEXT("/Game/Materials/MPC_WowWind");
    UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr, WindCollectionPath);
    if (!Collection)
    {
        UPackage* Package = CreatePackage(*CollectionName);
        Collection = NewObject<UMaterialParameterCollection>(Package, TEXT("MPC_WowWind"), RF_Public | RF_Standalone);
        FCollectionVectorParameter Dir; Dir.ParameterName = TEXT("WindDir"); Dir.DefaultValue = FLinearColor(1, 0, 0, 0);
        FCollectionScalarParameter Strength; Strength.ParameterName = TEXT("WindStrength"); Strength.DefaultValue = 0.2f;
        Collection->VectorParameters.Add(Dir);
        Collection->ScalarParameters.Add(Strength);
        Collection->PostEditChange();
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        Package->MarkPackageDirty();
        const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
        if (!UPackage::SavePackage(Package, Collection, *File, Args)) UE_LOG(LogWowCloth, Error, TEXT("Could not save MPC_WowWind"));
    }
    if (WowCookedMaterial::Load(ClothMaterialName)) return;

    UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
    Material->SetShadingModel(MSM_Cloth);
    Material->BlendMode = BLEND_Masked;
    Material->OpacityMaskClipValue = 0.4f;
    Material->TwoSided = true;
    bool bNeedsRecompile = false;
    Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_InstancedStaticMeshes);
    Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_SkeletalMesh); // animated banners are skinned doodads
    auto& G = Material->GetExpressionCollection();
    auto Scalar = [&](const TCHAR* Name, float Default)
    {
        auto* P = NewObject<UMaterialExpressionScalarParameter>(Material);
        P->ParameterName = Name; P->DefaultValue = Default; G.AddExpression(P); return P;
    };
    auto Constant = [&](float Value)
    {
        auto* C = NewObject<UMaterialExpressionConstant>(Material); C->R = Value; G.AddExpression(C); return C;
    };
    struct FIn { const TCHAR* Name; UMaterialExpression* Expr; int32 Output; };
    auto Custom = [&](ECustomMaterialOutputType Type, const TCHAR* Code, std::initializer_list<FIn> Inputs)
    {
        auto* C = NewObject<UMaterialExpressionCustom>(Material);
        C->OutputType = Type; C->Code = Code; C->Inputs.Reset();
        for (const FIn& In : Inputs) { FCustomInput I; I.InputName = In.Name; I.Input.Connect(In.Output, In.Expr); C->Inputs.Add(I); }
        G.AddExpression(C); return C;
    };

    auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(Material); G.AddExpression(UV);
    auto* Sampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
    Sampler->ParameterName = TEXT("Layer0Texture");
    Sampler->SamplerType = SAMPLERTYPE_Color;
    Sampler->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
    Sampler->Coordinates.Connect(0, UV);
    G.AddExpression(Sampler);
    auto* AlphaCut = Scalar(TEXT("AlphaCut"), 0.f);
    auto* SwayKind = Scalar(TEXT("SwayKind"), 0.f);
    auto* MinZ = Scalar(TEXT("ClothMinZ"), 0.f);
    auto* Height = Scalar(TEXT("ClothHeight"), 100.f);

    auto* Data = Material->GetEditorOnlyData();
    Data->BaseColor.Connect(0, Sampler);
    // Fuzz (sheen) colour: a lighter, softer version of the weave, like light caught in the fibres.
    Data->SubsurfaceColor.Connect(0, Custom(CMOT_Float3, TEXT("return saturate(C.rgb*0.55+0.12);"), {{TEXT("C"), Sampler, 0}}));
    Data->ClearCoat.Connect(0, Constant(1.f)); // cloth amount (the Cloth shading model reads it from the CustomData0 slot = ClearCoat)
    Data->Roughness.Connect(0, Constant(0.85f));
    Data->Specular.Connect(0, Constant(0.25f));
    Data->OpacityMask.Connect(0, Custom(CMOT_Float1, TEXT("return lerp(1.0, A, Cut);"), {{TEXT("A"), Sampler, 4}, {TEXT("Cut"), AlphaCut, 0}}));

    // Wind: hanging cloth swings most at its free lower edge, stretched cloth billows in the middle.
    // Two layered sine waves with a per-position phase keep neighbouring banners out of step.
    auto* Local = NewObject<UMaterialExpressionLocalPosition>(Material); G.AddExpression(Local);
    auto* World = NewObject<UMaterialExpressionWorldPosition>(Material); G.AddExpression(World);
    auto* WorldF = NewObject<UMaterialExpressionTruncateLWC>(Material); WorldF->Input.Connect(0, World); G.AddExpression(WorldF);
    auto* Time = NewObject<UMaterialExpressionTime>(Material); G.AddExpression(Time);
    auto* WindDir = NewObject<UMaterialExpressionCollectionParameter>(Material);
    WindDir->Collection = Collection; WindDir->ParameterName = TEXT("WindDir"); WindDir->ParameterId = Collection->GetParameterId(TEXT("WindDir"));
    G.AddExpression(WindDir);
    auto* WindStrength = NewObject<UMaterialExpressionCollectionParameter>(Material);
    WindStrength->Collection = Collection; WindStrength->ParameterName = TEXT("WindStrength"); WindStrength->ParameterId = Collection->GetParameterId(TEXT("WindStrength"));
    G.AddExpression(WindStrength);
    Data->WorldPositionOffset.Connect(0, Custom(CMOT_Float3, TEXT(R"HLSL(
if (Kind < 0.5) return float3(0,0,0);
float h = saturate((L.z - MinZ) / max(Height, 1.0));
float w = Kind > 1.5 ? 4.0*h*(1.0-h) : pow(1.0-h, 1.3);
float s = saturate(S);
float phase = dot(P.xy, float2(0.011, 0.007));
float flutter = sin(T*(2.2+4.0*s) + phase + L.z*0.02)*0.6 + sin(T*(4.7+6.0*s) + phase*1.7 + L.x*0.03 + L.y*0.03)*0.4;
float2 d = D.xy; d = dot(d,d) > 1e-4 ? normalize(d) : float2(1,0);
float amp = (Kind > 1.5 ? 3.0 : 12.0) * (0.25 + s);
float push = 0.55*s + 0.45*flutter*(0.3 + s);
return float3(d*push, 0.15*flutter) * (w*amp);
)HLSL"), {{TEXT("L"), Local, 0}, {TEXT("P"), WorldF, 0}, {TEXT("T"), Time, 0}, {TEXT("D"), WindDir, 0},
          {TEXT("S"), WindStrength, 0}, {TEXT("Kind"), SwayKind, 0}, {TEXT("MinZ"), MinZ, 0}, {TEXT("Height"), Height, 0}}));

    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    if (WowCookedMaterial::Save(Material, ClothMaterialName))
        UE_LOG(LogWowCloth, Log, TEXT("Cloth material built (%s)"), ClothMaterialName);
#endif
}
}
