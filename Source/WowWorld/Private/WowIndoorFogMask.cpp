#include "WowIndoorFogMask.h"
#include "WowCookedMaterial.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionTruncateLWC.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowIndoorFog, Log, All);

namespace
{
    constexpr int32 MaxRooms = 16;
    // r478: v2 (new shader below; the cooked v1 keeps the old one).
    const TCHAR* MaterialName = TEXT("M_WowIndoorFogMask_v2");

    TArray<TWeakObjectPtr<UStaticMeshComponent>> GIndoorGroups;
    TWeakObjectPtr<UPostProcessComponent> GPass;
    TWeakObjectPtr<UMaterialInstanceDynamic> GMaterial;
    bool GMaterialMissing = false;

    // The fog of the outdoor part of the ray only. All distances in cm along the unit view ray D.
    // Integral of UE's exponential height fog from a to b (HeightFogCommon.ush: RayOriginTerms *
    // (1 - exp2(-f*dz)) / (f*dz) * length, with exp2 transmittance).
    const TCHAR* ShaderCode = TEXT(R"HLSL(
float4 Rooms[32] = { WowB0,WowB1,WowB2,WowB3,WowB4,WowB5,WowB6,WowB7,WowB8,WowB9,WowB10,WowB11,WowB12,WowB13,WowB14,WowB15,WowB16,WowB17,WowB18,WowB19,WowB20,WowB21,WowB22,WowB23,WowB24,WowB25,WowB26,WowB27,WowB28,WowB29,WowB30,WowB31 };
float3 Result = WowScene.rgb;
float Dist = length(WowRel);
int Count = (int)WowP3.x;
if (WowP2.w < 0.5 || Count <= 0 || Dist < 1.0) return Result;
float3 Dir = WowRel / Dist;
#define WOW_FOG_G(x) (abs(x) > 1e-5 ? (1.0 - exp2(-(x))) / (x) : 0.693147 - 0.240227 * (x))
#define WOW_FOG_I(a, b) (WowP0.x * exp2(-max(-127.0, WowP0.y * (WowCam.z + Dir.z * (a) - WowP0.z))) * ((b) - (a)) * WOW_FOG_G(WowP0.y * Dir.z * ((b) - (a))))
float Start = max(WowP0.w, 0.0);
float Full = Dist > Start ? WOW_FOG_I(Start, Dist) : 0.0;
float Inside = 0.0;
bool Touched = false;
[loop] for (int i = 0; i < 16; ++i)
{
    if (i >= Count) break;
    float4 RoomC = Rooms[2 * i];
    float4 RoomR = Rooms[2 * i + 1];
    float2 O = WowCam.xy - RoomC.xy;
    float3 Lo = float3(dot(O, RoomR.xy), dot(O, RoomR.zw), (WowCam.z - RoomC.z) / RoomC.w);
    float3 Ld = float3(dot(Dir.xy, RoomR.xy), dot(Dir.xy, RoomR.zw), Dir.z / RoomC.w);
    Ld += 1e-7 * (float3)(abs(Ld) < 1e-7);
    float3 T1 = (-1.0 - Lo) / Ld;
    float3 T2 = (1.0 - Lo) / Ld;
    float3 Near = min(T1, T2);
    float3 Far = max(T1, T2);
    float Enter = max(max(Near.x, Near.y), Near.z);
    float Leave = min(min(Far.x, Far.y), Far.z);
    if (Leave < Enter) continue;
    // The pixel lies in this room (its walls included), or the camera stands in it.
    Touched = Touched || (Enter <= Dist + 30.0 && Leave >= Dist - 30.0) || (Enter <= 0.0 && Leave >= 0.0);
    float SegA = max(Enter, Start);
    float SegB = min(Leave, Dist);
    if (SegB > SegA) Inside += WOW_FOG_I(SegA, SegB);
}
#undef WOW_FOG_I
#undef WOW_FOG_G
if (!Touched || Inside <= 0.0) return Result;
Inside = min(Inside, Full);
float Before = max(exp2(-Full), WowP1.w);
float After = max(exp2(-(Full - Inside)), WowP1.w);
// r478 (Stefan, Orgrimmar sandstorm: rooms seen through a door turned black or night blue, flickered,
// with rings): in dense dust a room pixel has almost no surface colour left (Before ~ 0), and
// boosting it up to 6x while subtracting our estimate of the fog (which misses the dust gusts and
// haze) went negative: black, banded rings. Only take back what the pixel still shows: the boost
// fades out below ~35 % remaining transmittance and stays at most 3x, and never below 40 % of
// the original pixel.
float Trust = saturate((Before - 0.04) / 0.30);
float K = lerp(1.0, min(After / max(Before, 1e-4), 3.0), Trust);
float3 Fog = (WowP1.rgb + WowP2.rgb * View.SkyAtmosphereHeightFogContribution * View.DistantSkyLightLutBufferSRV[0].rgb) * View.PreExposure;
return max(Result * K + Fog * (1.0 - K), Result * 0.4);
)HLSL");
}

// r655: WMO interior fog (MFOG). One post-process pass after tonemapping: the original client's linear fog,
// fog colour over the picture from the fog start to the fog end, in display colours.
namespace WowWmoFog
{
    struct FGroupFog { FLinearColor Color; float Start = 0, End = 0; };
    TMap<TWeakObjectPtr<UStaticMeshComponent>, FGroupFog> GFogs;
    TWeakObjectPtr<UPostProcessComponent> GFogPass;
    TWeakObjectPtr<UMaterialInstanceDynamic> GFogMaterial;
    bool GFogMissing = false;
    float GStrength = 0.f;
    FGroupFog GCurrent;
    const TCHAR* FogMaterialName = TEXT("M_WowWmoFog_v1");

    bool Inside(const UStaticMeshComponent* Group, const FVector& Point, double& OutVolume)
    {
        if (!Group || !Group->IsRegistered() || !Group->GetStaticMesh()) return false;
        if (FVector::DistSquared(Group->Bounds.Origin, Point) > FMath::Square(Group->Bounds.SphereRadius)) return false;
        const FBox Local = Group->GetStaticMesh()->GetBoundingBox();
        if (!Local.IsValid) return false;
        const FVector P = Group->GetComponentTransform().InverseTransformPosition(Point);
        if (!Local.IsInside(P)) return false;
        const FVector Scale = Group->GetComponentTransform().GetScale3D().GetAbs();
        OutVolume = double(Local.GetVolume()) * Scale.X * Scale.Y * Scale.Z;
        return true;
    }

    UMaterialInterface* PrepareFogMaterial()
    {
        UMaterial* Material = WowCookedMaterial::Load(FogMaterialName);
#if WITH_EDITOR
        if (!Material)
        {
            Material = NewObject<UMaterial>(GetTransientPackage());
            Material->MaterialDomain = MD_PostProcess;
            Material->BlendableLocation = BL_SceneColorAfterTonemapping;
            auto* Scene = NewObject<UMaterialExpressionSceneTexture>(Material);
            Scene->SceneTextureId = PPI_PostProcessInput0;
            auto* Depth = NewObject<UMaterialExpressionSceneTexture>(Material);
            Depth->SceneTextureId = PPI_SceneDepth;
            auto* FogParams = NewObject<UMaterialExpressionVectorParameter>(Material);
            FogParams->ParameterName = TEXT("WowFog"); // x = start cm, y = end cm, z = strength
            FogParams->DefaultValue = FLinearColor(0, 100000, 0, 0);
            auto* FogColor = NewObject<UMaterialExpressionVectorParameter>(Material);
            FogColor->ParameterName = TEXT("WowFogColor");
            FogColor->DefaultValue = FLinearColor(0, 0, 0, 1);
            auto* Shader = NewObject<UMaterialExpressionCustom>(Material);
            Shader->OutputType = CMOT_Float3;
            Shader->Code = TEXT(
                "float f = saturate((SD.r - P.x) / max(P.y - P.x, 1.0)) * P.z;\n"
                "return lerp(C.rgb, K.rgb, f);");
            Shader->Inputs.Reset();
            auto AddInput = [Shader](const TCHAR* Name, UMaterialExpression* Source, int32 Output)
            {
                FCustomInput Input; Input.InputName = Name; Input.Input.Connect(Output, Source); Shader->Inputs.Add(Input);
            };
            AddInput(TEXT("C"), Scene, 0);
            AddInput(TEXT("SD"), Depth, 0);
            AddInput(TEXT("P"), FogParams, 5);
            AddInput(TEXT("K"), FogColor, 5);
            for (UMaterialExpression* Expression : TArray<UMaterialExpression*>{ Scene, Depth, FogParams, FogColor, Shader })
                Material->GetExpressionCollection().AddExpression(Expression);
            Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Shader);
            Material->PreEditChange(nullptr);
            Material->PostEditChange();
            Material = WowCookedMaterial::Save(Material, FogMaterialName);
        }
#endif
        return Material;
    }

    void Update(AActor* Owner, const FVector& Point)
    {
        if (!Owner || GFogMissing) return;
        // Real seconds since the last update (the sky ticks about 10 times a second).
        static double Last = 0.0;
        const double Now = FPlatformTime::Seconds();
        const float Step = Last > 0.0 ? float(FMath::Min(Now - Last, 0.5)) : 0.f;
        Last = Now;
        // The smallest fogged group the view point is in.
        const FGroupFog* Found = nullptr;
        double Smallest = TNumericLimits<double>::Max();
        for (auto It = GFogs.CreateIterator(); It; ++It)
        {
            const UStaticMeshComponent* Group = It.Key().Get();
            if (!Group) { It.RemoveCurrent(); continue; }
            double Volume = 0.0;
            if (Inside(Group, Point, Volume) && Volume < Smallest) { Smallest = Volume; Found = &It.Value(); }
        }
        if (Found) GCurrent = *Found;
        GStrength = FMath::FInterpConstantTo(GStrength, Found ? 1.f : 0.f, Step, 1.0f); // about a second in or out
        UPostProcessComponent* Pass = GFogPass.Get();
        UMaterialInstanceDynamic* Material = GFogMaterial.Get();
        if (GStrength <= 0.f && !Pass) return;
        if (!Pass || !Material || Pass->GetOwner() != Owner)
        {
            UMaterialInterface* Base = PrepareFogMaterial();
            if (!Base)
            {
                GFogMissing = true;
                UE_LOG(LogWowIndoorFog, Warning, TEXT("WMO fog: %s is not cooked; caves have no haze"), FogMaterialName);
                return;
            }
            Material = UMaterialInstanceDynamic::Create(Base, Owner);
            Pass = NewObject<UPostProcessComponent>(Owner, TEXT("WowWmoFog"));
            Pass->bUnbound = true;
            Pass->Priority = 11.f;
            Pass->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, Material));
            Owner->AddInstanceComponent(Pass);
            Pass->RegisterComponent();
            GFogPass = Pass;
            GFogMaterial = Material;
        }
        // The fog distances are from the view point; the isometric camera stands further back.
        float Offset = 0.f;
        if (APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(Owner, 0))
            Offset = float(FVector::Dist(CameraManager->GetCameraLocation(), Point));
        Material->SetVectorParameterValue(TEXT("WowFog"), FLinearColor(GCurrent.Start + Offset, GCurrent.End + Offset, GStrength, 0.f));
        Material->SetVectorParameterValue(TEXT("WowFogColor"), GCurrent.Color);
        Pass->bEnabled = GStrength > 0.f;
        static bool bLoggedIn = false;
        if ((Found != nullptr) != bLoggedIn)
        {
            bLoggedIn = Found != nullptr;
            UE_LOG(LogWowIndoorFog, Log, TEXT("WMO fog %s: colour %s, %.0f-%.0f cm"), bLoggedIn ? TEXT("on") : TEXT("off"),
                *GCurrent.Color.ToString(), GCurrent.Start, GCurrent.End);
        }
    }
}

void FWowIndoorFogMask::SetGroupFog(UStaticMeshComponent* Group, const FColor& Color, float StartCm, float EndCm)
{
    if (!Group || EndCm <= 0.f) return;
    WowWmoFog::FGroupFog& Fog = WowWmoFog::GFogs.FindOrAdd(Group);
    // Display colour, applied after tonemapping like the original's fog: the sRGB bytes as they are.
    Fog.Color = FLinearColor(Color.R / 255.f, Color.G / 255.f, Color.B / 255.f, 1.f);
    Fog.Start = FMath::Clamp(StartCm, 0.f, EndCm - 1.f);
    Fog.End = EndCm;
}

void FWowIndoorFogMask::RegisterIndoorGroup(UStaticMeshComponent* Group)
{
    if (!Group || !Group->GetStaticMesh()) return;
    GIndoorGroups.RemoveAllSwap([](const TWeakObjectPtr<UStaticMeshComponent>& Weak) { return !Weak.IsValid(); });
    GIndoorGroups.Add(Group);
}

bool FWowIndoorFogMask::IsInsideIndoorGroup(const FVector& Point, double Margin)
{
    for (const TWeakObjectPtr<UStaticMeshComponent>& Weak : GIndoorGroups)
    {
        const UStaticMeshComponent* Group = Weak.Get();
        if (!Group || !Group->IsRegistered() || !Group->GetStaticMesh()) continue;
        if (FVector::DistSquared(Group->Bounds.Origin, Point) > FMath::Square(Group->Bounds.SphereRadius + Margin)) continue;
        const FBox Local = Group->GetStaticMesh()->GetBoundingBox();
        if (!Local.IsValid) continue;
        const FVector P = Group->GetComponentTransform().InverseTransformPosition(Point);
        const FVector Scale = Group->GetComponentTransform().GetScale3D().GetAbs();
        const FVector M(Margin / FMath::Max(Scale.X, 1e-3), Margin / FMath::Max(Scale.Y, 1e-3), Margin / FMath::Max(Scale.Z, 1e-3));
        if (Local.ExpandBy(M).IsInside(P)) return true;
    }
    return false;
}

UMaterialInterface* FWowIndoorFogMask::PrepareMaterial()
{
    WowWmoFog::PrepareFogMaterial(); // r655: cooked with the mask (editor / standalone prepare)
    UMaterial* Material = WowCookedMaterial::Load(MaterialName);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage());
        Material->MaterialDomain = MD_PostProcess;
        Material->BlendableLocation = BL_SceneColorBeforeDOF;
        auto* Scene = NewObject<UMaterialExpressionSceneTexture>(Material);
        Scene->SceneTextureId = PPI_PostProcessInput0;
        auto* Position = NewObject<UMaterialExpressionWorldPosition>(Material);
        Position->WorldPositionShaderOffset = WPT_CameraRelative;
        auto* CameraLwc = NewObject<UMaterialExpressionCameraPositionWS>(Material);
        auto* Camera = NewObject<UMaterialExpressionTruncateLWC>(Material);
        Camera->Input.Connect(0, CameraLwc);
        auto* Shader = NewObject<UMaterialExpressionCustom>(Material);
        Shader->OutputType = CMOT_Float3;
        Shader->Code = ShaderCode;
        Shader->Inputs.Reset();
        auto AddInput = [Shader](const TCHAR* Name, UMaterialExpression* Source, int32 Output)
        {
            FCustomInput Input;
            Input.InputName = Name;
            Input.Input.Connect(Output, Source);
            Shader->Inputs.Add(Input);
        };
        TArray<UMaterialExpression*> Expressions{ Scene, Position, CameraLwc, Camera, Shader };
        AddInput(TEXT("WowScene"), Scene, 0);
        AddInput(TEXT("WowRel"), Position, 0);
        AddInput(TEXT("WowCam"), Camera, 0);
        auto AddVector = [&](const FString& Name, const FLinearColor& Default)
        {
            auto* Parameter = NewObject<UMaterialExpressionVectorParameter>(Material);
            Parameter->ParameterName = *Name;
            Parameter->DefaultValue = Default;
            Expressions.Add(Parameter);
            AddInput(*Name, Parameter, 5); // RGBA
        };
        AddVector(TEXT("WowP0"), FLinearColor(0, 0, 0, 0));
        AddVector(TEXT("WowP1"), FLinearColor(0, 0, 0, 1));
        AddVector(TEXT("WowP2"), FLinearColor(0, 0, 0, 0)); // w = enabled
        AddVector(TEXT("WowP3"), FLinearColor(0, 0, 0, 0)); // x = room count
        for (int32 I = 0; I < MaxRooms * 2; ++I)
            AddVector(FString::Printf(TEXT("WowB%d"), I), FLinearColor(0, 0, 0, 1));
        for (UMaterialExpression* Expression : Expressions)
            Material->GetExpressionCollection().AddExpression(Expression);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Shader);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, MaterialName);
    }
#endif
    return Material;
}

void FWowIndoorFogMask::Update(AActor* Owner, const UExponentialHeightFogComponent* Fog, const FVector& Camera, bool bWeatherFog)
{
    WowWmoFog::Update(Owner, Camera); // r655
    if (!Owner || !Fog || GMaterialMissing) return;
    UPostProcessComponent* Pass = GPass.Get();
    UMaterialInstanceDynamic* Material = GMaterial.Get();
    if (!Pass || !Material || Pass->GetOwner() != Owner)
    {
        UMaterialInterface* Base = PrepareMaterial();
        if (!Base)
        {
            GMaterialMissing = true;
            UE_LOG(LogWowIndoorFog, Warning, TEXT("Indoor fog mask: %s is not cooked; rooms seen from outside keep the weather fog"), MaterialName);
            return;
        }
        Material = UMaterialInstanceDynamic::Create(Base, Owner);
        Pass = NewObject<UPostProcessComponent>(Owner, TEXT("WowIndoorFogMask"));
        Pass->bUnbound = true;
        Pass->Priority = 10.f;
        Pass->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, Material));
        Owner->AddInstanceComponent(Pass);
        Pass->RegisterComponent();
        GPass = Pass;
        GMaterial = Material;
        UE_LOG(LogWowIndoorFog, Log, TEXT("Indoor fog mask ready"));
    }

    // Only heavy weather uses the analytic height fog near the camera (light weather keeps the
    // volumetric fog grid, which this formula does not describe).
    static const IConsoleVariable* VolumetricFog = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricFog"));
    const bool bVolumetric = Fog->bEnableVolumetricFog && (!VolumetricFog || VolumetricFog->GetInt() != 0);
    const bool bActive = bWeatherFog && !bVolumetric && Fog->FogDensity > 0.f;
    int32 Count = 0;
    if (bActive)
    {
        struct FRoom { double DistSq; double Volume; FVector Center; FVector2D AxisX, AxisY; FVector Half; };
        TArray<FRoom, TInlineAllocator<64>> Rooms;
        constexpr double Reach = 12000.0; // 120 m
        GIndoorGroups.RemoveAllSwap([](const TWeakObjectPtr<UStaticMeshComponent>& Weak) { return !Weak.IsValid(); });
        for (const TWeakObjectPtr<UStaticMeshComponent>& Weak : GIndoorGroups)
        {
            const UStaticMeshComponent* Group = Weak.Get();
            if (!Group || !Group->IsRegistered() || !Group->GetStaticMesh()) continue;
            const FBoxSphereBounds& World = Group->Bounds;
            const double SphereGap = FMath::Max(0.0, FVector::Dist(World.Origin, Camera) - World.SphereRadius);
            if (SphereGap > Reach) continue;
            const FBox Local = Group->GetStaticMesh()->GetBoundingBox();
            if (!Local.IsValid) continue;
            const FTransform Transform = Group->GetComponentTransform();
            const FVector Scale = Transform.GetScale3D().GetAbs();
            // Distance from the camera to the group's own box, not its bounding sphere: in cities
            // dozens of huge interior groups have spheres around the camera (gap 0) and pushed the
            // room the camera looks into out of the 16 slots (Stormwind Keep hall fogged from its door).
            const FVector LocalCamera = Transform.InverseTransformPosition(Camera);
            const FVector Outside = (LocalCamera - Local.GetClosestPointTo(LocalCamera)) * Scale;
            const double BoxDistSq = Outside.SizeSquared();
            if (BoxDistSq > Reach * Reach) continue;
            const FVector Half = Local.GetExtent() * Scale;
            if (Half.X < 50.0 || Half.Y < 50.0 || Half.Z < 50.0) continue;
            // Yaw only (WMO placements stand upright): the shader works with XY axes plus height.
            FVector X = Transform.GetUnitAxis(EAxis::X); X.Z = 0;
            if (!X.Normalize()) continue;
            const FVector Y(-X.Y, X.X, 0.0);
            FRoom& Room = Rooms.AddDefaulted_GetRef();
            Room.Center = Transform.TransformPosition(Local.GetCenter());
            Room.DistSq = BoxDistSq;
            Room.Volume = Half.X * Half.Y * Half.Z;
            Room.Half = Half;
            Room.AxisX = FVector2D(X.X, X.Y) / Half.X;
            Room.AxisY = FVector2D(Y.X, Y.Y) / Half.Y;
        }
        // Nearest box first; rooms the camera stands in or touches (distance 0) smallest first.
        Rooms.Sort([](const FRoom& A, const FRoom& B)
        {
            constexpr double Touch = 100.0 * 100.0; // within 1 m counts as touching
            const bool bA = A.DistSq <= Touch, bB = B.DistSq <= Touch;
            if (bA != bB) return bA;
            if (bA) return A.Volume < B.Volume;
            return A.DistSq < B.DistSq;
        });
        Count = FMath::Min(Rooms.Num(), MaxRooms);
        for (int32 I = 0; I < Count; ++I)
        {
            const FRoom& Room = Rooms[I];
            Material->SetVectorParameterValue(*FString::Printf(TEXT("WowB%d"), I * 2),
                FLinearColor(float(Room.Center.X), float(Room.Center.Y), float(Room.Center.Z), float(Room.Half.Z)));
            Material->SetVectorParameterValue(*FString::Printf(TEXT("WowB%d"), I * 2 + 1),
                FLinearColor(float(Room.AxisX.X), float(Room.AxisX.Y), float(Room.AxisY.X), float(Room.AxisY.Y)));
        }
        static int32 LoggedCount = -1;
        if ((Count > 0) != (LoggedCount > 0))
        {
            LoggedCount = Count;
            UE_LOG(LogWowIndoorFog, Log, TEXT("Indoor fog mask: %d rooms near the camera (%d indoor groups loaded)"), Count, GIndoorGroups.Num());
        }
    }
    // The fog exactly as the renderer gets it (FExponentialHeightFogSceneInfo: density and
    // falloff / 1000, height = the component's Z).
    Material->SetVectorParameterValue(TEXT("WowP0"), FLinearColor(Fog->FogDensity / 1000.f, Fog->FogHeightFalloff / 1000.f,
        float(Fog->GetComponentLocation().Z), Fog->StartDistance));
    const FLinearColor Inscatter = Fog->FogInscatteringLuminance;
    Material->SetVectorParameterValue(TEXT("WowP1"), FLinearColor(Inscatter.R, Inscatter.G, Inscatter.B, 1.f - Fog->FogMaxOpacity));
    // The sky atmosphere's ambient light joins the fog colour only with this project setting.
    static const IConsoleVariable* AffectsFog = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SupportSkyAtmosphereAffectsHeightFog"));
    const FLinearColor Ambient = (!AffectsFog || AffectsFog->GetInt() != 0) ? Fog->SkyAtmosphereAmbientContributionColorScale : FLinearColor::Black;
    Material->SetVectorParameterValue(TEXT("WowP2"), FLinearColor(Ambient.R, Ambient.G, Ambient.B, bActive && Count > 0 ? 1.f : 0.f));
    Material->SetVectorParameterValue(TEXT("WowP3"), FLinearColor(float(Count), 0.f, 0.f, 0.f));
}
