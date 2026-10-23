#include "WowVrUiPresenter.h"
#include "WowUiVrHost.h"
#include "WowVRSupport.h"
#include "WowCookedMaterial.h"
#include "ProceduralMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"

UWowVrUiPresenter::UWowVrUiPresenter()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // after the camera has its head pose
}

UMaterialInterface* UWowVrUiPresenter::GetPanelMaterial()
{
    static TWeakObjectPtr<UMaterial> Cached;
    static bool bTried = false;
    if (Cached.IsValid()) return Cached.Get();
#if !WITH_EDITOR
    if (bTried) return nullptr;
#endif
    bTried = true;
    UMaterial* M = WowCookedMaterial::Load(TEXT("M_WowVrUiPanel_v1"));
#if WITH_EDITOR
    if (!M)
    {
        M = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
        M->SetShadingModel(MSM_Unlit);
        M->BlendMode = BLEND_Translucent;
        M->TwoSided = true;
        // The interface is never hidden behind a wall or the player's own arm.
        M->bDisableDepthTest = true;
        M->bEnableResponsiveAA = true;
        auto* Tex = NewObject<UMaterialExpressionTextureSampleParameter2D>(M);
        Tex->ParameterName = TEXT("UiTexture");
        Tex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
        Tex->SamplerType = SAMPLERTYPE_Color;
        auto* Fade = NewObject<UMaterialExpressionVertexColor>(M);
        auto* Opacity = NewObject<UMaterialExpressionMultiply>(M);
        Opacity->A.Connect(4, Tex);   // texture alpha
        Opacity->B.Connect(4, Fade);  // panel fade in vertex alpha
        M->GetExpressionCollection().AddExpression(Tex);
        M->GetExpressionCollection().AddExpression(Fade);
        M->GetExpressionCollection().AddExpression(Opacity);
        M->GetEditorOnlyData()->EmissiveColor.Connect(0, Tex);
        M->GetEditorOnlyData()->Opacity.Connect(0, Opacity);
        M->PostEditChange();
        M = WowCookedMaterial::Save(M, TEXT("M_WowVrUiPanel_v1"));
    }
#endif
    Cached = M;
    return M;
}

void UWowVrUiPresenter::HideAll()
{
    FWowUiVrHost::SetGazePoint(false, FVector2D::ZeroVector);
    for (auto& Pair : Panels) if (Pair.Value.Mesh) Pair.Value.Mesh->SetVisibility(false);
    if (bReady) { FWowUiVrHost::SetPresenterReady(false); bReady = false; }
}

void UWowVrUiPresenter::OnUnregister()
{
    if (bReady) { FWowUiVrHost::SetPresenterReady(false); bReady = false; }
    Super::OnUnregister();
}

void UWowVrUiPresenter::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UMaterialInterface* Base = GetPanelMaterial();
    APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
    if (!FWowVRSupport::IsActive() || !Base || !Camera || !GetOwner()) { HideAll(); return; }
    if (!bReady) { FWowUiVrHost::SetPresenterReady(true); bReady = true; }
    UTextureRenderTarget2D* Target = FWowUiVrHost::GetRenderTarget();
    if (!Target) { for (auto& Pair : Panels) if (Pair.Value.Mesh) Pair.Value.Mesh->SetVisibility(false); FWowUiVrHost::SetGazePoint(false, FVector2D::ZeroVector); return; }

    TArray<FWowUiVrZone> Zones;
    FWowUiVrHost::GetZones(Zones);
    const FVector Eye = Camera->GetCameraLocation();
    const FVector Look = Camera->GetCameraRotation().Vector();
    // Gaze aiming uses a smoothed head direction (~100 ms): head tremor kept restarting the
    // stare ring. The panels themselves still use the raw view for their gaze fade.
    static FVector SmoothLook = FVector::ZeroVector;
    if (SmoothLook.IsNearlyZero() || FVector::DotProduct(SmoothLook, Look) < .7f) SmoothLook = Look;
    else SmoothLook = FMath::Lerp(SmoothLook, Look, 1.f - FMath::Exp(-DeltaTime / .1f)).GetSafeNormal();
    FWowVRSupport::SetSmoothedGazeDirection(SmoothLook);
    const float BodyYaw = GetOwner()->GetActorRotation().Yaw;
    constexpr float CmPerDp = .055f;
    TSet<uint8> Seen;
    // Gaze point for VR dwell clicks on the interface: the nearest shown panel the head ray
    // (centre of view) meets, as render-target pixels (= virtual-window coordinates).
    bool bGazeHit = false;
    float GazeDistance = TNumericLimits<float>::Max();
    FVector2D GazePixel = FVector2D::ZeroVector;
    for (const FWowUiVrZone& Zone : Zones)
    {
        const uint8 Key = static_cast<uint8>(Zone.Zone);
        if (Zone.Zone == EWowUiZone::Glue || Zone.Zone == EWowUiZone::World || Zone.Zone == EWowUiZone::Debug) continue;
        Seen.Add(Key);
        FPanel& Panel = Panels.FindOrAdd(Key);
        if (!Panel.Mesh)
        {
            Panel.Mesh = NewObject<UProceduralMeshComponent>(GetOwner());
            Panel.Mesh->SetUsingAbsoluteLocation(true);
            Panel.Mesh->SetUsingAbsoluteRotation(true);
            Panel.Mesh->SetUsingAbsoluteScale(true);
            Panel.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Panel.Mesh->SetCastShadow(false);
            Panel.Mesh->bUseAsyncCooking = false;
            Panel.Mesh->ComponentTags.Add(TEXT("WowNoCameraFocus"));
            Panel.Mesh->TranslucencySortPriority = 100;
            Panel.Mesh->SetupAttachment(this);
            Panel.Mesh->RegisterComponent();
            Panel.Material = UMaterialInstanceDynamic::Create(Base, Panel.Mesh);
            Panel.Mesh->SetMaterial(0, Panel.Material);
        }
        if (Panel.Material) Panel.Material->SetTextureParameterValue(TEXT("UiTexture"), Target);

        // Where the zone sits around the player (pitch, yaw relative to the body), and
        // whether it is always shown or only when looked at.
        float Pitch = 0.f, Yaw = 0.f, Distance = 110.f; bool bGaze = true;
        switch (Zone.Zone)
        {
        case EWowUiZone::ActionBars: Pitch = -38.f; Yaw = 0.f; break;
        case EWowUiZone::CastBar:    Pitch = -22.f; Yaw = 0.f; bGaze = false; break;
        case EWowUiZone::Chat:       Pitch = -28.f; Yaw = -38.f; break;
        case EWowUiZone::Minimap:    Pitch = 24.f;  Yaw = 34.f; break;
        case EWowUiZone::Tracker:    Pitch = 6.f;   Yaw = 42.f; break;
        case EWowUiZone::UnitFrames: Pitch = 22.f;  Yaw = -30.f; break;
        case EWowUiZone::Auras:      Pitch = 30.f;  Yaw = 9.f; break; // Claude-UI r114: was 14, buffs touched the minimap
        case EWowUiZone::SideBars:   Pitch = -14.f; Yaw = 56.f; break; // Claude-UI r78: right-hand vertical bars
        case EWowUiZone::Popups:     Pitch = 14.f;  Yaw = 0.f; Distance = 120.f; bGaze = false; break; // Claude-UI r96: error/warning text strip
        case EWowUiZone::Tooltip:    Pitch = 10.f;  Yaw = 22.f; Distance = 95.f; bGaze = false; break; // Claude-UI r114: fixed, beside the minimap (Stefan)
        default: Pitch = -6.f; Yaw = 0.f; Distance = 95.f; bGaze = false; break; // windows, popups, tooltip, cursor, HUD
        }
        const FVector Dir = FRotator(Pitch, BodyYaw + Yaw, 0.f).Vector();
        const FVector Centre = Eye + Dir * Distance;
        // Gaze fade: fully shown within ~18 degrees of the panel, gone beyond ~38.
        const float Cos = FVector::DotProduct(Look, Dir);
        const float Want = bGaze ? FMath::Clamp((Cos - FMath::Cos(FMath::DegreesToRadians(38.f))) /
            (FMath::Cos(FMath::DegreesToRadians(18.f)) - FMath::Cos(FMath::DegreesToRadians(38.f))), 0.f, 1.f) : 1.f;
        Panel.Alpha = FMath::FInterpTo(Panel.Alpha, Zone.bVisible ? Want : 0.f, DeltaTime, 8.f);
        FWowUiVrHost::SetZoneOpacity(Zone.Zone, Panel.Alpha); // Claude-UI r83: faded panels take no clicks
        const bool bShow = Panel.Alpha > .01f && Zone.SizeDp.X > 1 && Zone.SizeDp.Y > 1;
        Panel.Mesh->SetVisibility(bShow);
        if (!bShow) continue;

        const float W = float(Zone.SizeDp.X) * CmPerDp * (Distance / 110.f), H = float(Zone.SizeDp.Y) * CmPerDp * (Distance / 110.f);
        Panel.Mesh->SetWorldLocationAndRotation(Centre, (Eye - Centre).Rotation());
        // Local +X faces the eye; for the viewer local +Y is to the left.
        const TArray<FVector> V = {FVector(0, W * .5f, H * .5f), FVector(0, -W * .5f, H * .5f),
            FVector(0, -W * .5f, -H * .5f), FVector(0, W * .5f, -H * .5f)};
        const FVector2D Min(Zone.UV.Min.X, Zone.UV.Min.Y), Max(Zone.UV.Max.X, Zone.UV.Max.Y);
        const TArray<FVector2D> UV = {Min, FVector2D(Max.X, Min.Y), Max, FVector2D(Min.X, Max.Y)};
        TArray<FLinearColor> Colors; Colors.Init(FLinearColor(1, 1, 1, Panel.Alpha), 4);
        if (Panel.Alpha >= .3f && W > 0.f && H > 0.f)
        {
            const FVector Normal = (Eye - Centre).GetSafeNormal();
            const float Facing = FVector::DotProduct(SmoothLook, -Normal);
            if (Facing > .05f)
            {
                const float T = FVector::DotProduct(Centre - Eye, -Normal) / Facing;
                const FVector Local = (Eye - Centre).Rotation().UnrotateVector(Eye + SmoothLook * T - Centre);
                const float U = (W * .5f - float(Local.Y)) / W, VV = (H * .5f - float(Local.Z)) / H;
                if (T > 0.f && T < GazeDistance && U >= 0.f && U <= 1.f && VV >= 0.f && VV <= 1.f)
                {
                    GazeDistance = T;
                    bGazeHit = true;
                    GazePixel = FVector2D(FMath::Lerp(Min.X, Max.X, double(U)) * Target->SizeX,
                        FMath::Lerp(Min.Y, Max.Y, double(VV)) * Target->SizeY);
                }
            }
        }
        static const TArray<int32> Tri = {0, 2, 1, 0, 3, 2};
        if (Panel.Mesh->GetNumSections() == 0) Panel.Mesh->CreateMeshSection_LinearColor(0, V, Tri, {}, UV, Colors, {}, false);
        else Panel.Mesh->UpdateMeshSection_LinearColor(0, V, {}, UV, Colors, {});
    }
    for (auto& Pair : Panels)
        if (!Seen.Contains(Pair.Key) && Pair.Value.Mesh) Pair.Value.Mesh->SetVisibility(false);
    FWowUiVrHost::SetGazePoint(bGazeHit, GazePixel);
}
