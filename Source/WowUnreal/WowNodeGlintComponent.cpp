#include "WowNodeGlintComponent.h"
#include "WowCookedMaterial.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVertexColor.h"

UWowNodeGlintComponent::UWowNodeGlintComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.f;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetCastShadow(false);
    bReceivesDecals = false;
    bUseAsyncCooking = false;
    ComponentTags.Add(TEXT("WowNoCameraFocus"));
    Random.Initialize(FMath::Rand());
}

UMaterialInterface* UWowNodeGlintComponent::GetGlintMaterial()
{
    // r369: strong reference. The weak cache let GC free the cooked material once the first
    // node despawned, and the old "tried once" guard then returned nullptr for every later
    // node (log: "material MISSING"), so no node glinted after the first one.
    static TStrongObjectPtr<UMaterial> Cached;
    if (Cached.IsValid()) return Cached.Get();
    UMaterial* M = WowCookedMaterial::Load(TEXT("M_WowNodeGlint_v1"));
#if WITH_EDITOR
    if (!M)
    {
        M = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
        M->SetShadingModel(MSM_Unlit);
        M->BlendMode = BLEND_Additive;
        M->TwoSided = true;
        auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(M);
        auto* Color = NewObject<UMaterialExpressionVertexColor>(M);
        auto* Star = NewObject<UMaterialExpressionCustom>(M);
        // One arm of the "+": bright thin core along U, fading to the tips and across V.
        Star->Code = TEXT("float a=1-abs(UV.x*2-1); float c=abs(UV.y*2-1);"
            "float arm=pow(saturate(a),2.5)*exp(-c*c*40);"
            "float core=exp(-((UV.x-0.5)*(UV.x-0.5)+(UV.y-0.5)*(UV.y-0.5))*60);"
            "return Col*(arm+core*0.8)*Alpha*6;");
        Star->OutputType = CMOT_Float3;
        FCustomInput InUV; InUV.InputName = TEXT("UV"); InUV.Input.Connect(0, UV); Star->Inputs.Add(InUV);
        // VertexColor output 0 is RGB only (float3); alpha is output 4.
        FCustomInput InCol; InCol.InputName = TEXT("Col"); InCol.Input.Connect(0, Color); Star->Inputs.Add(InCol);
        FCustomInput InAlpha; InAlpha.InputName = TEXT("Alpha"); InAlpha.Input.Connect(4, Color); Star->Inputs.Add(InAlpha);
        M->GetExpressionCollection().AddExpression(UV);
        M->GetExpressionCollection().AddExpression(Color);
        M->GetExpressionCollection().AddExpression(Star);
        M->GetEditorOnlyData()->EmissiveColor.Connect(0, Star);
        M->PostEditChange();
        M = WowCookedMaterial::Save(M, TEXT("M_WowNodeGlint_v1"));
    }
#endif
    if (M) Cached.Reset(M);
    return M;
}

void UWowNodeGlintComponent::InitFromMesh(UStaticMeshComponent* Mesh)
{
    Edges.Reset();
    const FTransform ToLocal = Mesh ? Mesh->GetComponentTransform().GetRelativeTransform(GetOwner()->GetActorTransform()) : FTransform::Identity;
    UStaticMesh* Static = Mesh ? Mesh->GetStaticMesh() : nullptr;
    // Hard edges: triangle edges whose two faces bend sharply (the ridges a light catches).
    if (Static && Static->bAllowCPUAccess && Static->GetRenderData() && Static->GetRenderData()->LODResources.Num() > 0)
    {
        const FStaticMeshLODResources& LOD = Static->GetRenderData()->LODResources[0];
        const FPositionVertexBuffer& Positions = LOD.VertexBuffers.PositionVertexBuffer;
        TArray<uint32> Indices;
        LOD.IndexBuffer.GetCopy(Indices);
        TMap<TPair<int32, int32>, FVector> EdgeNormal;
        auto Key = [&](uint32 A, uint32 B)
        {
            const FVector PA = FVector(Positions.VertexPosition(A)), PB = FVector(Positions.VertexPosition(B));
            // Weld by position so split vertices still share their edge.
            const int32 HA = GetTypeHash(PA.GridSnap(0.5)), HB = GetTypeHash(PB.GridSnap(0.5));
            return HA < HB ? TPair<int32, int32>(HA, HB) : TPair<int32, int32>(HB, HA);
        };
        for (int32 T = 0; T + 2 < Indices.Num() && Edges.Num() < 64; T += 3)
        {
            const uint32 I[3] = {Indices[T], Indices[T + 1], Indices[T + 2]};
            const FVector P0(Positions.VertexPosition(I[0])), P1(Positions.VertexPosition(I[1])), P2(Positions.VertexPosition(I[2]));
            const FVector N = FVector::CrossProduct(P1 - P0, P2 - P0).GetSafeNormal();
            if (N.IsNearlyZero()) continue;
            for (int32 E = 0; E < 3; ++E)
            {
                const uint32 A = I[E], B = I[(E + 1) % 3];
                const auto K = Key(A, B);
                if (const FVector* Other = EdgeNormal.Find(K))
                {
                    const FVector PA(Positions.VertexPosition(A)), PB(Positions.VertexPosition(B));
                    // Sharp, reasonably long, and on the upper/outer part of the node.
                    if (FVector::DotProduct(*Other, N) < .55f && FVector::Dist(PA, PB) > 8.f && (N + *Other).Z > -.2f)
                        Edges.Add({ToLocal.TransformPosition(PA), ToLocal.TransformPosition(PB)});
                }
                else EdgeNormal.Add(K, N);
            }
        }
    }
    if (Edges.Num() < 3 && Mesh)
    {
        // No CPU geometry (herbs): run along the top rim of the bounds instead.
        const FBoxSphereBounds MeshBounds = Mesh->CalcBounds(ToLocal);
        const FVector C = MeshBounds.Origin, X = FVector(MeshBounds.BoxExtent.X * .7f, 0, 0), Y = FVector(0, MeshBounds.BoxExtent.Y * .7f, 0);
        const FVector Top = C + FVector(0, 0, MeshBounds.BoxExtent.Z * .6f);
        Edges.Add({Top - X, Top + Y}); Edges.Add({Top + Y, Top + X}); Edges.Add({Top + X, Top - Y}); Edges.Add({Top - Y, Top - X});
    }
    // Bigger and more often (r225, Stefan: "no light or glints on this node"): one star every
    // 0.3-1 s instead of 0.6-1.8 s, up to 30 cm.
    if (Mesh) Size = FMath::Clamp(float(Mesh->Bounds.SphereRadius) * .3f, 12.f, 30.f);
    UMaterialInterface* GlintMaterial = GetGlintMaterial();
    UE_LOG(LogTemp, Log, TEXT("Node glint on %s: %d edges, size %.0f, material %s"), GetOwner() ? *GetOwner()->GetName() : TEXT("?"),
        Edges.Num(), Size, GlintMaterial ? *GlintMaterial->GetName() : TEXT("MISSING"));
    if (!GlintMaterial) { Edges.Reset(); return; } // never draw default-material squares
    SetMaterial(0, GlintMaterial);
    Phase = -Random.FRandRange(.1f, .8f);
}

void UWowNodeGlintComponent::PickEdge()
{
    if (Edges.IsEmpty()) return;
    const auto& Edge = Edges[Random.RandRange(0, Edges.Num() - 1)];
    const bool bFlip = Random.FRand() < .5f;
    EdgeA = bFlip ? Edge.Value : Edge.Key;
    EdgeB = bFlip ? Edge.Key : Edge.Value;
    Duration = FMath::Clamp(float(FVector::Dist(EdgeA, EdgeB)) / 60.f, .45f, 1.1f);
}

void UWowNodeGlintComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (Edges.IsEmpty() || !GetOwner()) return;
    if (Phase < 0.f)
    {
        Phase += DeltaTime;
        if (Phase >= 0.f) { Phase = 0.f; PickEdge(); }
        else { SetVisibility(false); return; }
    }
    Phase += DeltaTime / Duration;
    if (Phase >= 1.f) { Phase = -Random.FRandRange(.3f, 1.f); SetVisibility(false); return; }
    SetVisibility(true);
    APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
    if (!Camera) return;
    const FTransform Owner = GetOwner()->GetActorTransform();
    const FVector World = Owner.TransformPosition(FMath::Lerp(EdgeA, EdgeB, Phase));
    const FVector ToCamera = (Camera->GetCameraLocation() - World).GetSafeNormal();
    // Rolls a little as it travels; swells in the middle of the run and fades out.
    const FRotator View = Camera->GetCameraRotation();
    const float Roll = Phase * 1.4f;
    const FVector R0 = View.RotateVector(FVector(0, FMath::Cos(Roll), FMath::Sin(Roll)));
    const FVector U0 = View.RotateVector(FVector(0, -FMath::Sin(Roll), FMath::Cos(Roll)));
    const float Swell = FMath::Sin(Phase * PI);
    const float Arm = Size * (.45f + .55f * Swell), Width = Arm * .22f;
    const FVector P = Owner.InverseTransformPosition(World + ToCamera * 4.f); // just in front of the surface
    const FVector R = Owner.InverseTransformVectorNoScale(R0), U = Owner.InverseTransformVectorNoScale(U0);
    TArray<FVector> V = {
        P - R * Arm - U * Width, P + R * Arm - U * Width, P + R * Arm + U * Width, P - R * Arm + U * Width,
        P - U * Arm - R * Width, P + U * Arm - R * Width, P + U * Arm + R * Width, P - U * Arm + R * Width};
    static const TArray<FVector2D> UV = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}, {1, 1}, {0, 1}};
    static const TArray<int32> Tri = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    const FLinearColor Col(1.f, .93f, .75f, Swell);
    TArray<FLinearColor> Colors; Colors.Init(Col, 8);
    if (GetNumSections() == 0) CreateMeshSection_LinearColor(0, V, Tri, {}, UV, Colors, {}, false);
    else UpdateMeshSection_LinearColor(0, V, {}, UV, Colors, {});
}
