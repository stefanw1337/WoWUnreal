#include "WowIsometricCamera.h"
#include "WowClientPreferences.h"
#include "WowKeyBindings.h"
#include "WowVRSupport.h"
#include "WowCookedMaterial.h"
#include "Camera/CameraTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionTruncateLWC.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWowIsometric, Log, All);

namespace
{
    // r617 (Stefan: "too far down and not angled right"): 55 degrees looked almost straight down. The
    // angle now follows the zoom like Diablo-style cameras: 35 degrees close in, 50 degrees far out
    // (about 42 at the default zoom), with a slightly wider lens.
    // r617b (Stefan's screenshot: "you don't get the depth of the land, more GTA1 style"): a narrow lens
    // from far away flattens everything into a map. A wider lens closer in keeps the perspective, so the
    // ground recedes into the distance and buildings and trees stand up out of it.
    // r621 (Stefan: "locked in angle"): one fixed angle at every zoom; zoom only changes the distance.
    // r622 (Stefan: "an isometric view uses a 30-degree angle relative to the horizontal baseline for the
    // X and Z axes"): the true isometric elevation. Ground lines along the world axes are drawn at 30
    // degrees on screen when the camera looks down at atan(1/sqrt(2)) = 35.264 degrees.
    constexpr float NearPitch = -35.264f;
    constexpr float FarPitch = -35.264f;
    constexpr float IsoFov = 50.0f;
    constexpr float MinDistance = 900.0f;
    constexpr float MaxDistance = 6000.0f; // r618 (Stefan: "an even longer zoom-out")
    // Cut-away: surfaces above the head, within this radius of the player, while something is overhead.
    constexpr float CutAboveFeet = 300.0f;
    constexpr float CutRadius = 1800.0f;
    constexpr float CutFeather = 150.0f;

    float GYaw = 0.0f;
    bool GHasYaw = false;
    float GDistance = 1600.0f;
    float GTargetDistance = 1600.0f;
    FVector GPivot = FVector::ZeroVector;
    bool GHasPivot = false;

    struct FCut
    {
        // Strong: once swapped out, nothing else keeps the original material instances alive.
        TArray<TStrongObjectPtr<UMaterialInterface>> Original;
        TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Cut;
    };
    TMap<TWeakObjectPtr<UStaticMeshComponent>, FCut> GCuts;
    // r619 (Stefan: "the fires and the chandelier are kinda strange"): indoors, props that hang above
    // head height (chandeliers, lamps, banners) floated in the air once the ceiling and their chains
    // were cut, and so did high fire/smoke emitters. While cut they are hidden whole.
    TSet<TWeakObjectPtr<UPrimitiveComponent>> GHidden;
    TStrongObjectPtr<UMaterialInterface> GCutMaterial;
    double GNextScan = 0.0;
    bool GCovered = false;
    // r648 (Stefan in the Valley of Trials cave: "it doesn't even show me the insides"): under the ground (the
    // terrain lies above the player's head) nothing is cut - the cave walls are terrain and the cave's own
    // groups, and cutting them showed rock from outside - and the camera comes in under the cave ceiling
    // like a normal third-person camera.
    bool GUnderground = false;
    bool IsUnderground(UWorld* World, const APawn* Pawn, const FVector& Feet)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(WowIsoUnderground), true, Pawn);
        const FVector Top = Feet + FVector(0, 0, 6000.0f);
        const FVector Head = Feet + FVector(0, 0, 250.0f);
        for (int32 Step = 0; Step < 6; ++Step)
        {
            FHitResult Hit;
            if (!World->LineTraceSingleByChannel(Hit, Top, Head, ECC_Visibility, Query)) return false;
            UPrimitiveComponent* Component = Hit.GetComponent();
            if (Hit.GetActor() && Hit.GetActor()->GetName().StartsWith(TEXT("TerrainTile"))
                && Component && !Component->IsA<UInstancedStaticMeshComponent>()) return true;
            if (!Component) return false;
            Query.AddIgnoredComponent(Component); // a tree or a rock above the ground: look past it
        }
        return false;
    }
    // r626 (Stefan: "under a roof here, but not indoors, between two buildings - this also needs to make us
    // visible"): the sight line from the camera to the player is blocked although nothing is straight
    // overhead. Treated like indoors (cut around the player) and held a second, so it does not flicker.
    double GSightBlockedUntil = 0.0;
    // r626 (Stefan: "if not a hole in the roof, then an outline of our character and mobs behind hidden
    // things"): units render into custom depth; a post-process draws them through whatever hides them.
    TSet<TWeakObjectPtr<UPrimitiveComponent>> GSilhouetted;
    TStrongObjectPtr<UMaterialInterface> GSilhouetteMaterial;
    double GNextSilhouetteScan = 0.0;
    // r635 (Stefan: "the sound my character and mobs make needs to be louder at this range - maybe half the
    // way"): the ears sit halfway between the camera and the player (at most 12 m from the player), so
    // combat and footsteps are heard as from close by; left/right still follows the screen.
    TWeakObjectPtr<APlayerController> GListenerOwner;
    UMaterialInterface* GetSilhouetteMaterial(); // r626, defined below ApplyView
    float GCutStrength = 0.0f; // eased 0..1, scales the cut radius in and out

    void Restore(UStaticMeshComponent* Mesh, FCut& Cut)
    {
        if (!Mesh) return;
        for (int32 Slot = 0; Slot < Cut.Original.Num() && Slot < Mesh->GetNumMaterials(); ++Slot)
            Mesh->SetMaterial(Slot, Cut.Original[Slot].Get());
    }

    // r618: which meshes may be cut. Terrain chunks never; on terrain tiles only the instanced doodads
    // and trees (r618, Stefan: "the roof was cut, but not the trees"). Units and the player never.
    bool ShouldConsider(UPrimitiveComponent* Primitive, const APawn* Pawn)
    {
        UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Primitive);
        // r667 (Stefan: "fence lower right blinking"): a prop the cut hid is invisible, so this test turned it down on
        // the next scan - it was shown again, then hidden again, four times a second. What the cut hid still counts.
        if (!Mesh || (!Mesh->IsVisible() && !GHidden.Contains(TWeakObjectPtr<UPrimitiveComponent>(Mesh)))) return false;
        const AActor* Owner = Mesh->GetOwner();
        if (!Owner || Owner == Pawn || Owner->IsA<APawn>()) return false;
        if (Owner->GetName().StartsWith(TEXT("TerrainTile")) && !Mesh->IsA<UInstancedStaticMeshComponent>()) return false;
        const FString Name = Mesh->GetName();
        if (Name.Contains(TEXT("Liq")) || Name.Contains(TEXT("Water")) || Name.Contains(TEXT("Collision"))) return false;
        return true;
    }

    // The cut: a cylinder above CutZ. Indoors (something overhead) around the player, 18 m; outdoors
    // (r618) a smaller one between the player and the camera, so tree crowns and roofs in front of
    // you open up while the rest of the landscape stays whole.
    // r632 (Stefan: "it's also cutting props I'm not even behind"): outdoors the cut is a wedge along the
    // sight line from the player to the camera instead of a disc: only what rises above the line from the
    // camera to the player's waist, within Radius to either side of it, is cut. Indoors it stays a disc.
    struct FCutShape
    {
        FVector Centre; float Radius; float Z;
        FVector2D Dir = FVector2D::ZeroVector; // wedge: towards the camera
        float Slope = 0.0f;                    // wedge: rise of the cut floor per unit towards the camera
        bool bWedge = false;
    };
    FCutShape GShape{ FVector::ZeroVector, 0.f, 1.e9f };
    constexpr float WedgeHalfWidth = 170.0f;
    constexpr float WedgeStart = 50.0f;
    // Distance along the sight line and to the side of it.
    void WedgeCoords(const FCutShape& S, const FVector& P, float& Along, float& Side)
    {
        const FVector2D D(P.X - S.Centre.X, P.Y - S.Centre.Y);
        Along = float(FVector2D::DotProduct(D, S.Dir));
        Side = float((D - S.Dir * Along).Size());
    }
    // Is P inside the cut footprint (Extra widens it), and how high is the cut floor there.
    bool CutFootprint(const FCutShape& S, const FVector& P, float Extra, float& Floor)
    {
        if (!S.bWedge) { Floor = S.Z; return FVector::Dist2D(P, S.Centre) <= S.Radius + Extra; }
        float Along, Side;
        WedgeCoords(S, P, Along, Side);
        Floor = S.Z + FMath::Max(Along - Extra, 0.0f) * S.Slope;
        return Side <= S.Radius + Extra && Along >= WedgeStart - Extra;
    }
    bool GOutdoorShape = true; // r629: which shape the last scan chose (its centre follows the player every frame)
    FCutShape MakeWedge(const FVector& Feet, const FVector& CameraLocation)
    {
        FCutShape Shape{ Feet, WedgeHalfWidth, float(Feet.Z + 100.0f) };
        const FVector2D ToCamera(CameraLocation.X - Feet.X, CameraLocation.Y - Feet.Y);
        const float Flat = float(FMath::Max(ToCamera.Size(), 1.0));
        Shape.Dir = ToCamera / Flat;
        // The line from the camera to the player's waist (1 m above the feet).
        Shape.Slope = float(FMath::Max(CameraLocation.Z - (Feet.Z + 100.0f), 0.0)) / Flat;
        Shape.bWedge = true;
        return Shape;
    }

    bool Touches(UStaticMeshComponent* Mesh, const FCutShape& Shape)
    {
        const FBox Box = Mesh->Bounds.GetBox();
        if (Box.Max.Z < Shape.Z) return false;
        if (Shape.bWedge)
        {
            const float Reach = float(FMath::Max(Box.GetExtent().X, Box.GetExtent().Y)) + CutFeather;
            float Floor = 0.0f;
            if (UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Mesh))
            {
                // Per instance: its root inside the wedge (with its own size) and its top above the cut floor.
                const UStaticMesh* Static = Instanced->GetStaticMesh();
                const FBoxSphereBounds Local = Static ? Static->GetBounds() : FBoxSphereBounds(FVector::ZeroVector, FVector(300.0f), 300.0f);
                const FVector Probe(Shape.Centre.X + Shape.Dir.X * 1500.0f, Shape.Centre.Y + Shape.Dir.Y * 1500.0f, Shape.Z);
                for (const int32 Index : Instanced->GetInstancesOverlappingSphere(Probe, 1700.0f, true))
                {
                    FTransform T;
                    if (!Instanced->GetInstanceTransform(Index, T, true)) continue;
                    const FVector Root = T.GetLocation();
                    const float Size = float(Local.BoxExtent.Size2D() * T.GetScale3D().GetMax());
                    const float Top = float(Root.Z + (Local.Origin.Z + Local.BoxExtent.Z) * T.GetScale3D().Z);
                    if (CutFootprint(Shape, Root, Size + CutFeather, Floor) && Top > Floor) return true;
                }
                return false;
            }
            return CutFootprint(Shape, Box.GetCenter(), Reach, Floor) && Box.Max.Z > Floor;
        }
        if (FVector::Dist2D(Box.GetClosestPointTo(Shape.Centre), Shape.Centre) > Shape.Radius + CutFeather) return false;
        if (UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Mesh))
        {
            // A tile-wide batch: only when one of its instances stands in the cut.
            const FVector Probe(Shape.Centre.X, Shape.Centre.Y, Shape.Z + 400.f);
            return Instanced->GetInstancesOverlappingSphere(Probe, Shape.Radius + 600.f, true).Num() > 0;
        }
        return true;
    }

    // r647 (Stefan: a Durotar cliff rock, bushes and a tent drawn flat dark grey whenever the cut caught them;
    // the log showed "cut:TerrainTile_39_33/ISM_DUROTARCLIFFROCK04"): instanced doodads (rocks, trees, bushes,
    // tents on terrain tiles) are no longer drawn with the cut material, which also changed every other
    // instance of that model on the tile. The instances in the cut are hidden one by one instead (scaled
    // to nothing; these batches carry no collision - their blockers are separate components).
    TMap<TWeakObjectPtr<UInstancedStaticMeshComponent>, TMap<int32, FTransform>> GHiddenInstances;
    // r668 (Stefan: "being cut out and I don't see it, so I can't avoid it - we need to make it cut whenever the
    // camera doesn't have line of sight"): outdoors only what really stands between the camera and the character is
    // hidden or cut: its box must cross one of the sight lines from the camera to the feet, waist, chest, head or
    // either shoulder. A fence or a tree beside the line stays. (Indoors the disc around the player stays.)
    FVector GSightCamera = FVector::ZeroVector;
    TArray<FVector, TInlineAllocator<6>> GSightTargets;
    // r671 (Stefan, a lamp pillar beside him in Deathknell hidden: "this isn't really in the way, or hiding me at that
    // range"): a box is a poor stand-in for a pillar or a post - its corners cross the sight lines though the stone
    // does not. Where the sight-line traces actually hit something (any collision: the props' authored collision
    // included) the hit points are kept, and a prop is in the way only when a hit lies inside it.
    TArray<FVector> GSightHits;
    bool BlocksSight(const FBox& InBox, int32 MinLines = 1)
    {
        if (!InBox.IsValid || GSightTargets.Num() == 0) return false;
        const FBox Box = InBox.ExpandBy(-InBox.GetExtent() * 0.1); // a little inside the bounds: leaves and feathered edges
        int32 Lines = 0;
        for (const FVector& Target : GSightTargets)
            if (FMath::LineBoxIntersection(Box, GSightCamera, Target, Target - GSightCamera) && ++Lines >= MinLines) return true;
        return false;
    }
    bool SightHitInside(const FBox& Box)
    {
        const FBox Grown = Box.ExpandBy(10.0);
        for (const FVector& Hit : GSightHits) if (Grown.IsInside(Hit)) return true;
        return false;
    }
    // r674 (Stefan, Northshire: "why is that being hidden, when the camera has line of sight on my character?" - and
    // then he walked into the hidden tree's trunk): a tree's box holds a lot of air - the crown hangs to one side, the
    // trunk is thin - so a sight line through the box's empty corner hid the whole tree. Each mesh gets a coarse
    // occupancy grid (12 x 12 x 12 cells over its bounds, built once from its triangles), and a sight line only
    // counts where it passes through a filled cell. Meshes without CPU data keep the box test.
    constexpr int32 OccN = 12;
    struct FSightOccluder
    {
        FBox Box = FBox(ForceInit);
        FVector Cell = FVector::OneVector;
        TBitArray<> Filled;
        bool bBuilt = false;
        bool bValid = false;
    };
    TMap<TWeakObjectPtr<const UStaticMesh>, FSightOccluder> GOccluders;
    int32 OccIndex(const FSightOccluder& O, const FVector& X)
    {
        const FVector R = (X - O.Box.Min) / O.Cell;
        const int32 I = FMath::Clamp(FMath::FloorToInt32(R.X), 0, OccN - 1);
        const int32 J = FMath::Clamp(FMath::FloorToInt32(R.Y), 0, OccN - 1);
        const int32 K = FMath::Clamp(FMath::FloorToInt32(R.Z), 0, OccN - 1);
        return (K * OccN + J) * OccN + I;
    }
    const FSightOccluder& GetOccluder(const UStaticMesh* Mesh)
    {
        if (GOccluders.Num() > 4096) GOccluders.Reset();
        FSightOccluder& O = GOccluders.FindOrAdd(TWeakObjectPtr<const UStaticMesh>(Mesh));
        if (O.bBuilt) return O;
        O.bBuilt = true;
        const FStaticMeshRenderData* Render = Mesh ? Mesh->GetRenderData() : nullptr;
        if (!Mesh->bAllowCPUAccess || !Render || Render->LODResources.Num() == 0) return O;
        const FStaticMeshLODResources& LOD = Render->LODResources[0];
        const FPositionVertexBuffer& Positions = LOD.VertexBuffers.PositionVertexBuffer;
        const uint32 NumVerts = Positions.GetNumVertices();
        if (NumVerts == 0) return O;
        TArray<uint32> Indices;
        LOD.IndexBuffer.GetCopy(Indices);
        if (Indices.Num() < 3 || Indices.Num() > 600000) return O;
        for (uint32 V = 0; V < NumVerts; ++V) O.Box += FVector(Positions.VertexPosition(V));
        O.Box = O.Box.ExpandBy(1.0);
        O.Cell = O.Box.GetSize() / double(OccN);
        O.Filled.Init(false, OccN * OccN * OccN);
        const double Step = FMath::Max(O.Cell.GetMin() * 0.5, 1.0);
        for (int32 T = 0; T + 2 < Indices.Num(); T += 3)
        {
            if (Indices[T] >= NumVerts || Indices[T + 1] >= NumVerts || Indices[T + 2] >= NumVerts) continue;
            const FVector A(Positions.VertexPosition(Indices[T]));
            const FVector B(Positions.VertexPosition(Indices[T + 1]));
            const FVector C(Positions.VertexPosition(Indices[T + 2]));
            const int32 S = FMath::Clamp(FMath::CeilToInt32(FMath::Max(FVector::Dist(A, B), FVector::Dist(A, C)) / Step), 1, 48);
            for (int32 I = 0; I <= S; ++I)
                for (int32 J = 0; I + J <= S; ++J)
                    O.Filled[OccIndex(O, A + (B - A) * (double(I) / S) + (C - A) * (double(J) / S))] = true;
        }
        O.bValid = true;
        return O;
    }
    bool OccluderBlocks(const FSightOccluder& O, const FVector& A, const FVector& B) // mesh-local space
    {
        const FVector D = B - A;
        double T0 = 0.0, T1 = 1.0;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (FMath::Abs(D[Axis]) < 1e-6)
            {
                if (A[Axis] < O.Box.Min[Axis] || A[Axis] > O.Box.Max[Axis]) return false;
                continue;
            }
            double Ta = (O.Box.Min[Axis] - A[Axis]) / D[Axis], Tb = (O.Box.Max[Axis] - A[Axis]) / D[Axis];
            if (Ta > Tb) Swap(Ta, Tb);
            T0 = FMath::Max(T0, Ta);
            T1 = FMath::Min(T1, Tb);
            if (T0 > T1) return false;
        }
        const int32 Steps = FMath::Clamp(FMath::CeilToInt32(D.Size() * (T1 - T0) / FMath::Max(O.Cell.GetMin() * 0.5, 1.0)), 1, 256);
        for (int32 I = 0; I < Steps; ++I)
            if (O.Filled[OccIndex(O, A + D * (T0 + (T1 - T0) * (I + 0.5) / Steps))]) return true;
        return false;
    }
    bool MeshBlocksSight(const UStaticMesh* Mesh, const FTransform& T, const FBox& WorldBox, int32 MinLines)
    {
        if (!Mesh || T.GetScale3D().GetAbsMin() < 0.01) return BlocksSight(WorldBox, MinLines);
        const FSightOccluder& O = GetOccluder(Mesh);
        if (!O.bValid) return BlocksSight(WorldBox, MinLines);
        if (!WorldBox.IsValid || GSightTargets.Num() == 0) return false;
        const FVector Camera = T.InverseTransformPosition(GSightCamera);
        int32 Lines = 0;
        for (const FVector& Target : GSightTargets)
            if (FMath::LineBoxIntersection(WorldBox, GSightCamera, Target, Target - GSightCamera)
                && OccluderBlocks(O, Camera, T.InverseTransformPosition(Target)) && ++Lines >= MinLines) return true;
        return false;
    }
    void CollectCutInstances(UInstancedStaticMeshComponent* Instanced, const FCutShape& Shape, TSet<int32>& Out)
    {
        const UStaticMesh* Static = Instanced->GetStaticMesh();
        const FBoxSphereBounds Local = Static ? Static->GetBounds() : FBoxSphereBounds(FVector::ZeroVector, FVector(300.0f), 300.0f);
        // r672 (Stefan, Northshire: the big trees filled the screen and were never hidden): the probe only reached
        // 32 m from the character towards the camera, which stands 25-60 m away - the crowns right in front of the
        // camera were never tested. Outdoors it covers the whole sight line (midpoint, half its length + 15 m).
        const FVector SightMid = (GSightCamera + Shape.Centre) * 0.5;
        const FVector Probe = Shape.bWedge
            ? SightMid
            : FVector(Shape.Centre.X, Shape.Centre.Y, Shape.Z + 400.0f);
        const float ProbeRadius = Shape.bWedge ? float(FVector::Dist(GSightCamera, Shape.Centre) * 0.5 + 1500.0) : Shape.Radius + 600.0f;
        // r661 (Stefan: "trees keep blinking", on screen and on the minimap): a hidden instance is shrunk to
        // nothing, so the overlap query below could miss it (its tiny bounds sit at the root, maybe just outside
        // the probe) - it was restored, found again on the next scan and hidden again, four times a second.
        // Hidden instances are always tested again with their real transform.
        TSet<int32> Candidates(Instanced->GetInstancesOverlappingSphere(Probe, ProbeRadius, true));
        if (const TMap<int32, FTransform>* AlreadyHidden = GHiddenInstances.Find(Instanced))
            for (const auto& Pair : *AlreadyHidden) Candidates.Add(Pair.Key);
        for (const int32 Index : Candidates)
        {
            FTransform T;
            if (!Instanced->GetInstanceTransform(Index, T, true)) continue;
            if (const FTransform* Saved = GHiddenInstances.FindOrAdd(Instanced).Find(Index)) T = *Saved; // already hidden: test its real size
            const FVector Root = T.GetLocation();
            const float Size = float(Local.BoxExtent.Size2D() * T.GetScale3D().GetMax());
            const float Top = float(Root.Z + (Local.Origin.Z + Local.BoxExtent.Z) * T.GetScale3D().Z);
            float Floor = 0.0f;
            if (Shape.bWedge) // r668: outdoors by the sight lines
            {
                if (Static && MeshBlocksSight(Static, T, Static->GetBoundingBox().TransformBy(T), 2)) Out.Add(Index); // r671: two lines at least, r674: through the mesh
                continue;
            }
            if (CutFootprint(Shape, Root, Size + CutFeather, Floor) && Top > Floor) Out.Add(Index);
        }
    }
    void RestoreInstances(UInstancedStaticMeshComponent* Instanced, TMap<int32, FTransform>& Saved, const TSet<int32>* Keep)
    {
        if (!Instanced) return;
        bool bChanged = false;
        for (auto It = Saved.CreateIterator(); It; ++It)
        {
            if (Keep && Keep->Contains(It.Key())) continue;
            if (It.Key() < Instanced->GetInstanceCount()) Instanced->UpdateInstanceTransform(It.Key(), It.Value(), true, false, true);
            It.RemoveCurrent();
            bChanged = true;
        }
        if (bChanged) Instanced->MarkRenderStateDirty();
    }

    void UpdateCutaway(APawn* Pawn, const FVector& Feet, const FVector& CameraLocation, float DeltaTime)
    {
        UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
        UMaterialInterface* Base = FWowIsometricCamera::GetCutawayMaterial();
        if (!World || !Base) { FWowIsometricCamera::ClearCutaway(); return; }
        const double Now = World->GetTimeSeconds();
        if (Now >= GNextScan)
        {
            GNextScan = Now + 0.25;
            // Something overhead (roof, cave ceiling, floor above) within 40 m: indoors.
            FCollisionQueryParams Up(SCENE_QUERY_STAT(WowIsoCovered), true, Pawn);
            FHitResult Roof;
            GCovered = World->LineTraceSingleByChannel(Roof, Feet + FVector(0, 0, 200), Feet + FVector(0, 0, 4000), ECC_Visibility, Up);
            {
                const bool bUnderground = IsUnderground(World, Pawn, Feet); // r648
                if (bUnderground != GUnderground)
                    UE_LOG(LogWowIsometric, Log, TEXT("Iso cut: %s"), bUnderground ? TEXT("underground - no cut, camera under the ceiling") : TEXT("above ground again"));
                GUnderground = bUnderground;
            }
            // r626: or something (an eave, a roof, a bridge) between the camera and the player's chest.
            // Collision stays while a surface is cut, so this keeps seeing it and the cut stays open.
            float SightRadius = 0.0f;
            {
                FHitResult Sight;
                const FVector Chest = Feet + FVector(0, 0, 120.0f);
                if (World->LineTraceSingleByChannel(Sight, Chest, CameraLocation, ECC_Visibility, Up)
                    && Sight.ImpactPoint.Z > Feet.Z + 150.0f
                    && !(Sight.GetActor() && Sight.GetActor()->GetName().StartsWith(TEXT("TerrainTile")) && !(Sight.GetComponent() && Sight.GetComponent()->IsA<UInstancedStaticMeshComponent>())))
                {
                    GSightBlockedUntil = Now + 1.0;
                    SightRadius = FMath::Clamp(float(FVector::Dist2D(Sight.ImpactPoint, Feet)) + 500.0f, 900.0f, 2500.0f);
                }
            }
            const bool bSightBlocked = Now < GSightBlockedUntil;
            const FVector ToCamera = FVector(CameraLocation.X - Feet.X, CameraLocation.Y - Feet.Y, 0.f).GetSafeNormal();
            // r668: outdoors always the line-of-sight test (the r626 sight-blocked disc hid everything round the player).
            GOutdoorShape = !GCovered;
            (void)bSightBlocked; (void)SightRadius;
            GShape = GCovered ? FCutShape{ Feet, CutRadius, float(Feet.Z + CutAboveFeet) } : MakeWedge(Feet, CameraLocation);

            TSet<UStaticMeshComponent*> Wanted;
            TSet<UPrimitiveComponent*> WantHidden;
            TArray<AActor*> FoundOwners; // r636 diagnostics
            TMap<UInstancedStaticMeshComponent*, TSet<int32>> WantInstances; // r647
            // r668: the sight lines, each ending a little in front of the character (its own box is not a blocker).
            GSightCamera = CameraLocation;
            GSightTargets.Reset();
            GSightHits.Reset();
            TSet<UPrimitiveComponent*> WmoBlockers;
            {
                const FVector Side = FVector::CrossProduct((Feet - CameraLocation).GetSafeNormal2D(), FVector::UpVector) * 45.0f;
                for (const FVector& Offset : { FVector(0, 0, 25), FVector(0, 0, 100), FVector(0, 0, 150), FVector(0, 0, 195),
                        FVector(0, 0, 150) + Side, FVector(0, 0, 150) - Side })
                {
                    const FVector Point = Feet + Offset;
                    GSightTargets.Add(Point + (CameraLocation - Point).GetSafeNormal() * 60.0f);
                }
                // Buildings: what the sight lines actually hit (their groups are large boxes, so the box test is too wide).
                FCollisionQueryParams SightQuery(SCENE_QUERY_STAT(WowIsoSight), true, Pawn);
                for (const FVector& Target : GSightTargets)
                    for (int32 Step = 0; Step < 8; ++Step)
                    {
                        FHitResult Hit;
                        if (!World->LineTraceSingleByChannel(Hit, CameraLocation, Target, ECC_Visibility, SightQuery)) break;
                        UPrimitiveComponent* HitPart = Hit.GetComponent();
                        if (!HitPart) break;
                        if (Hit.GetActor() && Hit.GetActor()->GetName().StartsWith(TEXT("TerrainTile")) && !HitPart->IsA<UInstancedStaticMeshComponent>()) break;
                        if (HitPart->GetName().StartsWith(TEXT("WmoGroup_"))) WmoBlockers.Add(HitPart);
                        if (!HitPart->GetOwner() || !HitPart->GetOwner()->IsA<APawn>()) GSightHits.Add(Hit.ImpactPoint); // r671
                        SightQuery.AddIgnoredComponent(HitPart);
                    }
            }
            if (!GUnderground) // r648: nothing is cut under the ground
            {
                TArray<FOverlapResult> Overlaps;
                FCollisionQueryParams Query(SCENE_QUERY_STAT(WowIsoCutaway), false, Pawn);
                FCollisionObjectQueryParams Objects;
                Objects.AddObjectTypesToQuery(ECC_WorldStatic);
                // r672: outdoors the box spans the whole sight line from the camera to the character.
                const FVector SightMid = (CameraLocation + Feet) * 0.5;
                const FVector SightHalf = (CameraLocation - Feet).GetAbs() * 0.5;
                const FVector BoxCentre = GShape.bWedge
                    ? SightMid
                    : FVector(GShape.Centre.X, GShape.Centre.Y, GShape.Z + 1000.0f);
                const FVector BoxHalf = GShape.bWedge
                    ? FVector(SightHalf.X + 1500.0, SightHalf.Y + 1500.0, SightHalf.Z + 1200.0)
                    : FVector(GShape.Radius + CutFeather, GShape.Radius + CutFeather, 1200.0f);
                World->OverlapMultiByObjectType(Overlaps, BoxCentre, FQuat::Identity, Objects,
                    FCollisionShape::MakeBox(BoxHalf), Query);
                TSet<AActor*> Owners;
                for (const FOverlapResult& Overlap : Overlaps)
                {
                    UPrimitiveComponent* Primitive = Overlap.GetComponent();
                    if (Primitive && Primitive->GetOwner()) Owners.Add(Primitive->GetOwner());
                }
                // Every mesh of the actors found (WMO groups and tree batches without their own collision).
                for (AActor* Owner : Owners)
                {
                    TInlineComponentArray<UPrimitiveComponent*> Parts(Owner);
                    for (UPrimitiveComponent* Part : Parts)
                    {
                        if (!Part) continue;
                        UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Part);
                        if (UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Part))
                        {
                            // r647: instance by instance, never the cut material.
                            // r667: a batch whose tall instances are shrunk has lower bounds - keep testing it while it hides any.
                            if (ShouldConsider(Instanced, Pawn) && (Instanced->Bounds.GetBox().Max.Z >= GShape.Z || GHiddenInstances.Contains(TWeakObjectPtr<UInstancedStaticMeshComponent>(Instanced))))
                            {
                                TSet<int32> Indices;
                                CollectCutInstances(Instanced, GShape, Indices);
                                if (Indices.Num()) WantInstances.Add(Instanced, MoveTemp(Indices));
                            }
                            continue;
                        }
                        // r629 (Stefan's screenshots: a lamp post, roof spikes and a dropped sign drawn dark and
                        // untextured): M2 props (outdoor doodads and WMO doodads) lose their own lighting under the
                        // simplified cut material, so they are not cut but hidden whole while they reach into the cut.
                        // r647 (Stefan: "can happen to anything, even this mailbox"): only WMO building groups take the
                        // cut material (it was made for them); every other mesh (doodads, game objects such as mailboxes,
                        // banners, signs) is hidden whole instead.
                        if (Mesh && !Part->GetName().StartsWith(TEXT("WmoGroup_")))
                        {
                            const FBox PropBox = Part->Bounds.GetBox();
                            const bool bHanging = GCovered && PropBox.Min.Z > Feet.Z + 180.0f
                                && FVector::Dist2D(PropBox.GetCenter(), GShape.Centre) < GShape.Radius;
                            // r672: a prop without collision of its own (a tree crown) is tested by its box, two lines at least.
                            const bool bInWay = GCovered ? Touches(Mesh, GShape)
                                : (SightHitInside(PropBox) || (Part->GetCollisionEnabled() == ECollisionEnabled::NoCollision
                                    && MeshBlocksSight(Mesh->GetStaticMesh(), Mesh->GetComponentTransform(), PropBox, 2))); // r668, r671, r674
                            if (bHanging || (ShouldConsider(Mesh, Pawn) && bInWay)) WantHidden.Add(Part);
                            continue;
                        }
                        if (GCovered && !Part->IsA<UInstancedStaticMeshComponent>() && Part->GetName().StartsWith(TEXT("WmoDoodad")))
                        {
                            // Hanging props and high emitters inside the cut: hidden whole.
                            const FBox Box = Part->Bounds.GetBox();
                            const bool bHigh = Mesh ? Box.Min.Z > Feet.Z + 180.0f : Part->GetComponentLocation().Z > Feet.Z + 200.0f;
                            if (bHigh && FVector::Dist2D(Box.GetCenter(), GShape.Centre) < GShape.Radius) { WantHidden.Add(Part); continue; }
                        }
                        // r668: outdoors a building group is cut only when a sight line hits it.
                        if (Mesh && ShouldConsider(Mesh, Pawn) && (GCovered ? Touches(Mesh, GShape) : WmoBlockers.Contains(Mesh))) Wanted.Add(Mesh);
                    }
                }
                FoundOwners = Owners.Array();
            }
            for (auto It = GHidden.CreateIterator(); It; ++It)
            {
                UPrimitiveComponent* Part = It->Get();
                if (Part && WantHidden.Contains(Part)) continue;
                if (Part) Part->SetVisibility(true);
                It.RemoveCurrent();
            }
            for (UPrimitiveComponent* Part : WantHidden)
                if (!GHidden.Contains(Part) && Part->IsVisible()) { Part->SetVisibility(false); GHidden.Add(Part); }
            // r647: hidden instances - restore what left the cut, hide what entered it.
            for (auto It = GHiddenInstances.CreateIterator(); It; ++It)
            {
                UInstancedStaticMeshComponent* Instanced = It.Key().Get();
                if (!Instanced) { It.RemoveCurrent(); continue; }
                RestoreInstances(Instanced, It.Value(), WantInstances.Find(Instanced));
                if (It.Value().Num() == 0) It.RemoveCurrent();
            }
            for (auto& Pair : WantInstances)
            {
                UInstancedStaticMeshComponent* Instanced = Pair.Key;
                TMap<int32, FTransform>& Saved = GHiddenInstances.FindOrAdd(Instanced);
                bool bChanged = false;
                for (const int32 Index : Pair.Value)
                {
                    if (Saved.Contains(Index)) continue;
                    FTransform T;
                    if (!Instanced->GetInstanceTransform(Index, T, true)) continue;
                    Saved.Add(Index, T);
                    FTransform Tiny = T;
                    Tiny.SetScale3D(FVector(0.001));
                    Instanced->UpdateInstanceTransform(Index, Tiny, true, false, true);
                    bChanged = true;
                }
                if (bChanged) Instanced->MarkRenderStateDirty();
            }
            // r636 (Stefan: "trees need to be cut too", a canopy over the player stayed whole): what the scan
            // found, logged when it changes, so a surface that is never cut can be traced to its actor/component.
            {
                static int32 GLastOwners = -1, GLastWanted = -1, GLastHidden = -1;
                if (FoundOwners.Num() != GLastOwners || Wanted.Num() != GLastWanted || WantHidden.Num() != GLastHidden)
                {
                    GLastOwners = FoundOwners.Num(); GLastWanted = Wanted.Num(); GLastHidden = WantHidden.Num();
                    FString Names;
                    int32 Listed = 0;
                    for (UStaticMeshComponent* Mesh : Wanted) if (Listed++ < 8) Names += FString::Printf(TEXT(" cut:%s/%s"), *GetNameSafe(Mesh->GetOwner()), *Mesh->GetName());
                    for (UPrimitiveComponent* Part : WantHidden) if (Listed++ < 16) Names += FString::Printf(TEXT(" hide:%s/%s"), *GetNameSafe(Part->GetOwner()), *Part->GetName());
                    FString OwnerNames;
                    int32 OwnerListed = 0;
                    for (AActor* Owner : FoundOwners) if (OwnerListed++ < 8) OwnerNames += TEXT(" ") + GetNameSafe(Owner);
                    UE_LOG(LogWowIsometric, Log, TEXT("Iso cut: %s covered=%d owners=%d [%s ] cut=%d hidden=%d%s"),
                        GShape.bWedge ? TEXT("wedge") : TEXT("disc"), GCovered ? 1 : 0, FoundOwners.Num(), *OwnerNames, Wanted.Num(), WantHidden.Num(), *Names);
                }
            }
            // Restore what no longer needs cutting, cut what is new.
            for (auto It = GCuts.CreateIterator(); It; ++It)
            {
                UStaticMeshComponent* Mesh = It.Key().Get();
                if (Mesh && Wanted.Contains(Mesh)) continue;
                Restore(Mesh, It.Value());
                It.RemoveCurrent();
            }
            for (UStaticMeshComponent* Mesh : Wanted)
            {
                if (GCuts.Contains(Mesh)) continue;
                FCut& Cut = GCuts.Add(Mesh);
                for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
                {
                    UMaterialInterface* Original = Mesh->GetMaterial(Slot);
                    Cut.Original.Emplace(Original);
                    UTexture* Texture = nullptr;
                    const bool bTextured = Original && Original->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Layer0Texture")), Texture) && Texture;
                    const EBlendMode Blend = Original ? Original->GetBlendMode() : BLEND_Opaque;
                    // Glow, glass and other see-through passes are left as they are.
                    if (!bTextured || (Blend != BLEND_Opaque && Blend != BLEND_Masked)) { Cut.Cut.Add(nullptr); continue; }
                    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Mesh);
                    MID->SetTextureParameterValue(TEXT("Layer0Texture"), Texture);
                    MID->SetScalarParameterValue(TEXT("UseAlpha"), Blend == BLEND_Masked ? 1.0f : 0.0f);
                    Mesh->SetMaterial(Slot, MID);
                    Cut.Cut.Add(MID);
                }
            }
        }
        // r629 (Stefan: "the cutting in houses looks very choppy, like low FPS"): the scan runs 4 times a second,
        // but the cut follows the player every frame.
        if (GOutdoorShape)
        {
            GShape = MakeWedge(Feet, CameraLocation);
        }
        else
        {
            GShape.Centre = Feet;
            GShape.Z = float(Feet.Z + CutAboveFeet);
        }
        GCutStrength = FMath::FInterpConstantTo(GCutStrength, 1.0f, DeltaTime, 2.5f);
        const float Radius = GShape.Radius * FMath::SmoothStep(0.0f, 1.0f, GCutStrength);
        const FLinearColor Centre(GShape.Centre.X, GShape.Centre.Y, GShape.Centre.Z, 0.0f);
        const FLinearColor Dir(GShape.Dir.X, GShape.Dir.Y, GShape.Slope, GShape.bWedge ? 1.0f : 0.0f); // r632
        for (auto& Pair : GCuts)
            for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Weak : Pair.Value.Cut)
                if (UMaterialInstanceDynamic* MID = Weak.Get())
                {
                    MID->SetVectorParameterValue(TEXT("CutCentre"), Centre);
                    MID->SetScalarParameterValue(TEXT("CutZ"), GShape.Z);
                    MID->SetScalarParameterValue(TEXT("CutRadius"), Radius);
                    MID->SetVectorParameterValue(TEXT("CutDir"), Dir);
                }
    }
}

void FWowIsometricCamera::IgnoreCutSurfaces(FCollisionQueryParams& Query)
{
    for (auto& Pair : GCuts)
        if (UStaticMeshComponent* Mesh = Pair.Key.Get()) Query.AddIgnoredComponent(Mesh);
    for (const TWeakObjectPtr<UPrimitiveComponent>& Weak : GHidden)
        if (UPrimitiveComponent* Part = Weak.Get()) Query.AddIgnoredComponent(Part);
}

bool FWowIsometricCamera::IsCutAway(const FVector& Point)
{
    if (!IsActive() || GCutStrength <= 0.5f || GUnderground) return false;
    float Floor = 0.0f;
    return CutFootprint(GShape, Point, 0.0f, Floor) && Point.Z > Floor - 5.0f;
}

bool FWowIsometricCamera::IsActive()
{
    return FWowClientPreferences::GetWorldOption(TEXT("isometriccamera")) && !FWowVRSupport::IsActive();
}

FVector FWowIsometricCamera::ScreenDirection(const FVector2D& Input)
{
    const FRotator Flat(0.0f, GYaw, 0.0f);
    const FVector Forward = FRotationMatrix(Flat).GetUnitAxis(EAxis::X);
    const FVector Right = FRotationMatrix(Flat).GetUnitAxis(EAxis::Y);
    return (Forward * Input.Y + Right * Input.X).GetClampedToMaxSize(1.0f);
}

// r624 (Stefan: "in Don't Starve view, lock the zoom to the maximum zoom out ... and keep it locked for
// turning as well, so it's as 2D bird-view as possible"): world option "isodontstarve" (a style of the
// isometric camera). The first prototype's steep 55 degree view through a narrow 30 degree lens from far
// away, with no zoom and no turning.
namespace
{
    bool IsDontStarveView() { return FWowClientPreferences::GetWorldOption(TEXT("isodontstarve")); }
    constexpr float DontStarvePitch = -55.0f;
    constexpr float DontStarveFov = 30.0f;
    constexpr float DontStarveDistance = 10000.0f;
    // r663 (Stefan: "Don't Starve didn't have that big of a zoom when we first tried this, this is too much"): the
    // mouse wheel zooms this view too (angle and lens stay fixed, no turning); it starts where it was.
    constexpr float DontStarveMinDistance = 3500.0f;
    constexpr float DontStarveMaxDistance = 18000.0f;
    float GDontStarveTarget = DontStarveDistance;
}

void FWowIsometricCamera::Zoom(float Steps)
{
    if (IsDontStarveView())
    {
        GDontStarveTarget = FMath::Clamp(GDontStarveTarget - Steps * GDontStarveTarget * 0.1f, DontStarveMinDistance, DontStarveMaxDistance);
        return;
    }
    GTargetDistance = FMath::Clamp(GTargetDistance - Steps * FMath::Max(150.0f, GTargetDistance * 0.12f), MinDistance, MaxDistance);
}

void FWowIsometricCamera::ApplyView(APawn* Pawn, FMinimalViewInfo& View, float DeltaTime)
{
    if (!Pawn) return;
    if (!GHasYaw) { GYaw = Pawn->GetActorRotation().Yaw; GHasYaw = true; }
    // r617 (Stefan: "unable to turn the camera with middle mouse"): the game's UI layer takes the mouse
    // buttons before the player controller sees them, and the controller's mouse delta is only filled
    // while the mouse is captured. Read the button from the system and turn by the cursor's own movement.
    {
        // r620 (Stefan: "expected a keybinding for free turning camera"; "rebound Button3 to T, but middle
        // mouse still doesn't turn"): Key Bindings > Camera Functions > Free Turn Camera. Holding its key and
        // moving the mouse turns the view. Middle mouse turns it when it is bound there, unbound, or left on
        // Move and Steer (the default); bound to any other command, middle mouse does that instead.
        bool bMiddleDown = FWowKeyBindings::Held(TEXT("ISOCAMERATURN"));
        const FString MiddleCommand = FWowKeyBindings::Command(TEXT("BUTTON3"));
        const bool bMouseTurns = FWowKeyBindings::Keys(TEXT("ISOCAMERATURN")).Contains(TEXT("BUTTON3"))
            || MiddleCommand.IsEmpty() || MiddleCommand == TEXT("MOVEANDSTEER");
        if (bMouseTurns)
        {
            if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController())) bMiddleDown = bMiddleDown || PC->IsInputKeyDown(EKeys::MiddleMouseButton);
#if PLATFORM_WINDOWS
            if (FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive())
                bMiddleDown = bMiddleDown || (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
#endif
        }
        // Rotate Camera Left/Right (default , and .): a steady 90 degrees per second.
        // r651 (Stefan: "use the keybindings for turning, like mine Q and E"): Turn Left/Turn Right turn the camera too.
        const float KeyTurn = ((FWowKeyBindings::Held(TEXT("ISOCAMERAROTATERIGHT")) || FWowKeyBindings::Held(TEXT("TURNRIGHT"))) ? 1.0f : 0.0f)
            - ((FWowKeyBindings::Held(TEXT("ISOCAMERAROTATELEFT")) || FWowKeyBindings::Held(TEXT("TURNLEFT"))) ? 1.0f : 0.0f);
        const bool bLockedView = IsDontStarveView();
        if (bLockedView) bMiddleDown = false;
        if (KeyTurn != 0.0f && !bLockedView) GYaw = FRotator::NormalizeAxis(GYaw + KeyTurn * 90.0f * DeltaTime);
        // r665 (Stefan, Don't Starve view in Deathknell: "I used to have the church so that I went in to the right, now
        // I have to go up instead"): the view took its direction from the character's facing at login and could not
        // turn. It now snaps to the nearest quarter turn and a press of the turn keys (Q/E, , and .) turns it a
        // quarter turn, eased, like the classic isometric games.
        {
            static bool GDsInit = false;
            static float GDsTarget = 0.0f;
            static float GLastKeyTurn = 0.0f;
            if (bLockedView)
            {
                if (!GDsInit) { GDsTarget = FMath::RoundToFloat(GYaw / 90.0f) * 90.0f; GDsInit = true; }
                if (KeyTurn != 0.0f && GLastKeyTurn == 0.0f) GDsTarget = FRotator::NormalizeAxis(GDsTarget + 90.0f * KeyTurn);
                const float Delta = FRotator::NormalizeAxis(GDsTarget - GYaw);
                GYaw = FRotator::NormalizeAxis(GYaw + Delta * FMath::Clamp(DeltaTime * 8.0f, 0.0f, 1.0f));
            }
            else GDsInit = false;
            GLastKeyTurn = KeyTurn;
        }
        static FVector2D GLastCursor = FVector2D::ZeroVector;
        static bool GWasDown = false;
        const FVector2D Cursor = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
        // r628 (Stefan: "instead of middle mouse as turn, have right mouse, as before"): dragging with the
        // right button held turns the camera too; a right click without dragging stays click-to-move
        // (WowGameplayController acts on the release when the cursor has not moved 6 px).
#if PLATFORM_WINDOWS
        {
            static bool GRightWasDown = false;
            static bool GRightDragging = false;
            static FVector2D GRightPress = FVector2D::ZeroVector;
            const bool bRightDown = FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive()
                && (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            if (bRightDown && !GRightWasDown) { GRightPress = Cursor; GRightDragging = false; }
            if (bRightDown && FVector2D::Distance(Cursor, GRightPress) > 6.0f) GRightDragging = true;
            GRightWasDown = bRightDown;
            // r637: not while the left button is down too (both buttons walk towards the cursor).
            const bool bLeftDown = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (bRightDown && GRightDragging && !bLockedView && !bLeftDown) bMiddleDown = true;
        }
#endif
        if (bMiddleDown && GWasDown)
            // r637 (Stefan: "middle mouse to turn is very quick - does it use the mouse sensitivity slider?"): it
            // did, on top of 0.35 degrees per pixel; now the normal camera's 0.065 degrees per mouse unit.
            GYaw = FRotator::NormalizeAxis(GYaw + float(Cursor.X - GLastCursor.X) * 0.065f * FWowClientPreferences::GetCameraMouseSensitivity());
        GWasDown = bMiddleDown;
        GLastCursor = Cursor;
    }
    const bool bDontStarve = IsDontStarveView();
    GDistance = FMath::FInterpTo(GDistance, bDontStarve ? GDontStarveTarget : FMath::Min(GTargetDistance, MaxDistance), DeltaTime, 8.0f);
    // The pivot follows the chest; a little smoothing on height so steps and jumps do not shake the view.
    const FVector Target = Pawn->GetActorLocation() + FVector(0, 0, 60.0f);
    if (!GHasPivot || FVector::DistSquared(GPivot, Target) > FMath::Square(3000.0f)) { GPivot = Target; GHasPivot = true; }
    GPivot.X = Target.X; GPivot.Y = Target.Y;
    GPivot.Z = FMath::FInterpTo(GPivot.Z, Target.Z, DeltaTime, 6.0f);
    const float Pitch = FMath::Lerp(NearPitch, FarPitch, FMath::Clamp((GDistance - MinDistance) / (MaxDistance - MinDistance), 0.0f, 1.0f));
    const FRotator Rotation(bDontStarve ? DontStarvePitch : Pitch, GYaw, 0.0f);
    View.Rotation = Rotation;
    // r628 (Stefan's screenshot after turning: the view was a black wall): turned towards a hillside the
    // camera ended up inside or behind the terrain, which is never cut away. Like a spring arm, the camera
    // comes in to just in front of the ground (at once) and eases back out when the way is clear.
    {
        static float GClearDistance = 0.0f;
        float Clear = GDistance;
        if (UWorld* ViewWorld = Pawn->GetWorld())
        {
            // r631 (Stefan, Valley of Trials: the view was a canyon wall): terrain only has complex (per-triangle)
            // collision, so the simple trace of r628 went straight through it.
            FCollisionQueryParams Query(SCENE_QUERY_STAT(WowIsoCameraBlock), true, Pawn);
            IgnoreCutSurfaces(Query);
            FHitResult Hit;
            const FVector Wanted = GPivot - Rotation.Vector() * GDistance;
            if (ViewWorld->LineTraceSingleByChannel(Hit, GPivot, Wanted, ECC_Visibility, Query)
                && Hit.GetActor() && Hit.GetActor()->GetName().StartsWith(TEXT("TerrainTile"))
                && !(Hit.GetComponent() && Hit.GetComponent()->IsA<UInstancedStaticMeshComponent>()))
                Clear = FMath::Max(150.0f, float(Hit.Distance) - 60.0f);
            // r648: in a cave the cave's own ceiling and walls (WMO groups) stop the camera too.
            else if (GUnderground && Hit.bBlockingHit && Hit.GetActor() && !Hit.GetActor()->IsA<APawn>())
                Clear = FMath::Max(150.0f, float(Hit.Distance) - 60.0f);
        }
        GClearDistance = (Clear < GClearDistance || GClearDistance <= 0.0f) ? Clear : FMath::FInterpTo(GClearDistance, Clear, DeltaTime, 3.0f);
        View.Location = GPivot - Rotation.Vector() * FMath::Min(GClearDistance, GDistance);
    }
    View.FOV = bDontStarve ? DontStarveFov : IsoFov;
    // Feet: the capsule's bottom.
    FVector Feet = Pawn->GetActorLocation();
    if (const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Pawn->GetRootComponent()))
        Feet.Z -= Root->Bounds.BoxExtent.Z;
    UpdateCutaway(Pawn, Feet, View.Location, DeltaTime);
    if (APlayerController* ListenerPC = Cast<APlayerController>(Pawn->GetController()))
    {
        FVector Ear = FMath::Lerp(View.Location, GPivot, 0.5f);
        const FVector FromPlayer = Ear - GPivot;
        if (FromPlayer.Size() > 1200.0f) Ear = GPivot + FromPlayer.GetSafeNormal() * 1200.0f;
        ListenerPC->SetAudioListenerOverride(nullptr, Ear, View.Rotation);
        GListenerOwner = ListenerPC;
    }
    // r626: silhouettes of the player and units within 60 m that something hides.
    if (UWorld* World = Pawn->GetWorld())
    {
        const double Now = World->GetTimeSeconds();
        if (Now >= GNextSilhouetteScan)
        {
            GNextSilhouetteScan = Now + 0.5;
            for (TActorIterator<APawn> It(World); It; ++It)
            {
                APawn* Unit = *It;
                if (!Unit || FVector::DistSquared(Unit->GetActorLocation(), Pawn->GetActorLocation()) > FMath::Square(6000.0f)) continue;
                TInlineComponentArray<USkeletalMeshComponent*> Meshes(Unit);
                for (USkeletalMeshComponent* Mesh : Meshes)
                    if (Mesh && !Mesh->bRenderCustomDepth)
                    {
                        Mesh->SetRenderCustomDepth(true);
                        // r652: stencil 1 = you (white-blue outline), 2 = everyone else (orange-red outline).
                        Mesh->SetCustomDepthStencilValue(Unit == Pawn ? 1 : 2);
                        GSilhouetted.Add(Mesh);
                    }
            }
        }
        // r652: the outline colours come from the custom stencil, which needs custom depth with stencil.
        static IConsoleVariable* GCustomDepth = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"));
        if (GCustomDepth && GCustomDepth->GetInt() != 3) GCustomDepth->Set(3, ECVF_SetByCode);
        if (UMaterialInterface* Silhouette = GetSilhouetteMaterial())
            View.PostProcessSettings.AddBlendable(Silhouette, 1.0f);
    }
}

namespace
{
UMaterialInterface* GetSilhouetteMaterial()
{
    if (GSilhouetteMaterial.IsValid()) return GSilhouetteMaterial.Get();
    // r652 (Stefan, under a big Durotar tree: "could we add an outline, so we can see ourselves and mobs"):
    // v1 was a faint light-blue fill with a one-pixel rim. v2 draws a solid 2 px outline around every hidden part
    // of a unit plus a stronger fill: white-blue for the player, orange-red for mobs and other players.
    const FString Name = TEXT("M_WowIsoSilhouette_v2");
    UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage());
        Material->MaterialDomain = MD_PostProcess;
        Material->BlendableLocation = BL_SceneColorAfterTonemapping;
        Material->SetShadingModel(MSM_Unlit);
        auto Scene = [Material](ESceneTextureId Id)
        {
            auto* Texture = NewObject<UMaterialExpressionSceneTexture>(Material);
            Texture->SceneTextureId = Id;
            Material->GetExpressionCollection().AddExpression(Texture);
            return Texture;
        };
        auto* Colour = Scene(PPI_PostProcessInput0);
        auto* SceneDepth = Scene(PPI_SceneDepth);
        auto* CustomDepth = Scene(PPI_CustomDepth);
        auto* CustomStencil = Scene(PPI_CustomStencil); // r652
        auto* Draw = NewObject<UMaterialExpressionCustom>(Material);
        Draw->OutputType = CMOT_Float3;
        Draw->Inputs.Reset();
        // Hidden = a unit is in custom depth here and the scene in front of it is more than 20 cm nearer.
        // A light blue fill with a brighter rim, like the silhouettes of isometric games.
        Draw->Code = FString::Printf(TEXT(
            "float2 uv = GetDefaultSceneTextureUV(Parameters, %d);\n"
            "float2 px = View.BufferSizeAndInvSize.zw * 2.0;\n"
            "float hidden = (CD.r < 1.0e6 && CD.r + 20.0 < SD.r) ? 1.0 : 0.0;\n"
            "float own = (ST.r > 0.5 && ST.r < 1.5) ? 1.0 : 0.0;\n"
            "float edge = 0.0; float edgeOwn = 0.0;\n"
            "for (int i = 0; i < 8; ++i)\n"
            "{\n"
            "    float a = i * 0.785398;\n"
            "    float2 o = uv + float2(cos(a), sin(a)) * px;\n"
            "    float cd = SceneTextureLookup(o, %d, false).r;\n"
            "    float sd = SceneTextureLookup(o, %d, false).r;\n"
            "    float st = SceneTextureLookup(o, %d, false).r;\n"
            "    float h = (cd < 1.0e6 && cd + 20.0 < sd) ? 1.0 : 0.0;\n"
            "    edge = max(edge, h);\n"
            "    edgeOwn = max(edgeOwn, h * ((st > 0.5 && st < 1.5) ? 1.0 : 0.0));\n"
            "}\n"
            "float outline = edge * (1.0 - hidden);\n"
            "float3 mine = float3(0.75, 0.92, 1.0);\n"
            "float3 theirs = float3(1.0, 0.45, 0.2);\n"
            "float3 fillTint = lerp(theirs, mine, own);\n"
            "float3 lineTint = lerp(theirs, mine, edgeOwn);\n"
            "float3 c = lerp(C.rgb, fillTint, hidden * 0.45);\n"
            "return lerp(c, lineTint, outline);"),
            int32(PPI_CustomDepth), int32(PPI_CustomDepth), int32(PPI_SceneDepth), int32(PPI_CustomStencil));
        auto In = [Draw](const TCHAR* InputName, UMaterialExpression* Expression)
        { FCustomInput Value; Value.InputName = InputName; Value.Input.Connect(0, Expression); Draw->Inputs.Add(Value); };
        In(TEXT("C"), Colour);
        In(TEXT("SD"), SceneDepth);
        In(TEXT("CD"), CustomDepth);
        In(TEXT("ST"), CustomStencil);
        Material->GetExpressionCollection().AddExpression(Draw);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Draw);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, Name);
    }
#endif
    if (Material) GSilhouetteMaterial.Reset(Material);
    return Material;
}
}

void FWowIsometricCamera::ClearCutaway()
{
    if (APlayerController* ListenerPC = GListenerOwner.Get()) ListenerPC->ClearAudioListenerOverride(); // r635
    GListenerOwner.Reset();
    if (GCuts.Num() == 0 && GHidden.Num() == 0 && GSilhouetted.Num() == 0 && GHiddenInstances.Num() == 0 && !GHasPivot) return;
    for (const TWeakObjectPtr<UPrimitiveComponent>& Weak : GSilhouetted) if (UPrimitiveComponent* Part = Weak.Get()) Part->SetRenderCustomDepth(false);
    GSilhouetted.Reset();
    GSightBlockedUntil = 0.0;
    for (auto& Pair : GCuts) Restore(Pair.Key.Get(), Pair.Value);
    GCuts.Reset();
    for (const TWeakObjectPtr<UPrimitiveComponent>& Weak : GHidden) if (UPrimitiveComponent* Part = Weak.Get()) Part->SetVisibility(true);
    GHidden.Reset();
    for (auto& Pair : GHiddenInstances) RestoreInstances(Pair.Key.Get(), Pair.Value, nullptr); // r647
    GHiddenInstances.Reset();
    GCovered = false;
    GUnderground = false; // r648
    GCutStrength = 0.0f;
    GHasPivot = false;
}

UMaterialInterface* FWowIsometricCamera::GetCutawayMaterial()
{
    if (GCutMaterial.IsValid()) return GCutMaterial.Get();
    // r629 (Stefan: "big trees too" - cut trees drawn flat dark): v2 lacked the instanced-mesh usage, so on
    // the tree batches (instanced static meshes) the engine drew its default material instead.
    // r660 (Stefan: buildings, rocks, trees and props drawn flat dark grey whenever the cut caught them - the game log
    // said "Failed to compile Material ... M_WowIsoCutaway_v4 ... Default Material will be used in game"): the mask
    // read the absolute world position, which is a large-world (double) value in UE5, as a plain float3 in its HLSL,
    // so the material never compiled and every cut surface got the engine's grey default material (v1-v4 alike).
    // v5 passes the position through TruncateLWC.
    // r666 (Stefan: "this wasn't an issue the very first time - think, what did we do?"): the cook log has the real
    // error - "Material.ush: error: vector swizzle 'w' is out of bounds". r632 (v4, the wedge) added CutDir and read
    // D.w (the wedge/disc switch), but the parameter was connected through its RGB output (float3). v1-v3 compiled;
    // from v4 on the cut material never did and every cut surface was the grey default material. CutDir now comes
    // through the RGBA output (5).
    const FString Name = TEXT("M_WowIsoCutaway_v6");
    UMaterial* Material = WowCookedMaterial::Load(Name);
#if WITH_EDITOR
    if (!Material)
    {
        Material = NewObject<UMaterial>(GetTransientPackage());
        Material->BlendMode = BLEND_Masked;
        Material->TwoSided = true;
        Material->bUsedWithInstancedStaticMeshes = true; // r629
        Material->OpacityMaskClipValue = 0.5f;
        auto Add = [Material](UMaterialExpression* Expression) { Material->GetExpressionCollection().AddExpression(Expression); return Expression; };
        auto* Sampler = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
        Sampler->ParameterName = TEXT("Layer0Texture");
        Sampler->SamplerType = SAMPLERTYPE_Color;
        Sampler->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));
        Add(Sampler);
        auto Scalar = [&](const TCHAR* ParamName, float Default)
        {
            auto* P = NewObject<UMaterialExpressionScalarParameter>(Material);
            P->ParameterName = ParamName; P->DefaultValue = Default; Add(P); return P;
        };
        auto* UseAlpha = Scalar(TEXT("UseAlpha"), 0.0f);
        auto* CutZ = Scalar(TEXT("CutZ"), 1.0e9f);
        auto* Radius = Scalar(TEXT("CutRadius"), 0.0f);
        auto* Centre = NewObject<UMaterialExpressionVectorParameter>(Material);
        Centre->ParameterName = TEXT("CutCentre");
        Add(Centre);
        auto* Dir = NewObject<UMaterialExpressionVectorParameter>(Material);
        Dir->ParameterName = TEXT("CutDir");
        Dir->DefaultValue = FLinearColor(0, 0, 0, 0);
        Add(Dir);
        auto* WorldPos = NewObject<UMaterialExpressionWorldPosition>(Material);
        Add(WorldPos);
        auto* Pos = NewObject<UMaterialExpressionTruncateLWC>(Material); // r660
        Pos->Input.Connect(0, WorldPos);
        Add(Pos);
        auto* Mask = NewObject<UMaterialExpressionCustom>(Material);
        Mask->OutputType = CMOT_Float1;
        Mask->Inputs.Reset();
        // Cut a cylinder above CutZ around the player, with a soft rim; keep the pass's own cut-out.
        Mask->Code = TEXT(
            "float keep = lerp(1.0, saturate((A - 0.3) * 20.0), UseAlpha);\n"
            "float2 d = Pos.xy - C.xy;\n"
            "float along = dot(d, D.xy);\n"
            "float side = length(d - along * D.xy);\n"
            "float disc = 1.0 - saturate((length(d) - R) / 150.0);\n"
            "float wedge = (1.0 - saturate((side - R) / 100.0)) * saturate((along - 50.0) / 50.0);\n"
            "float inside = lerp(disc, wedge, D.w);\n"
            "float floorZ = Z + max(along, 0.0) * D.z * D.w;\n"
            "float above = saturate((Pos.z - floorZ) / 20.0);\n"
            "inside *= step(1.0, R);\n"
            "return keep * (1.0 - inside * above);");
        auto In = [Mask](const TCHAR* InputName, UMaterialExpression* Expression, int32 Output)
        { FCustomInput Value; Value.InputName = InputName; Value.Input.Connect(Output, Expression); Mask->Inputs.Add(Value); };
        In(TEXT("A"), Sampler, 4);
        In(TEXT("UseAlpha"), UseAlpha, 0);
        In(TEXT("Pos"), Pos, 0);
        In(TEXT("C"), Centre, 0);
        In(TEXT("R"), Radius, 0);
        In(TEXT("Z"), CutZ, 0);
        In(TEXT("D"), Dir, 5); // r666: RGBA - the code reads D.w
        Add(Mask);
        auto* Rough = NewObject<UMaterialExpressionConstant>(Material);
        Rough->R = 0.85f;
        Add(Rough);
        Material->GetEditorOnlyData()->BaseColor.Connect(0, Sampler);
        Material->GetEditorOnlyData()->OpacityMask.Connect(0, Mask);
        Material->GetEditorOnlyData()->Roughness.Connect(0, Rough);
        // r619: a little of the texture's own colour, so cut tree crowns and dark corners do not go black
        // under the simplified lighting.
        auto* Lift = NewObject<UMaterialExpressionCustom>(Material);
        Lift->OutputType = CMOT_Float3; Lift->Inputs.Reset(); Lift->Code = TEXT("return T * 0.15;");
        { FCustomInput Value; Value.InputName = TEXT("T"); Value.Input.Connect(0, Sampler); Lift->Inputs.Add(Value); }
        Add(Lift);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Lift);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
        Material = WowCookedMaterial::Save(Material, Name);
    }
#endif
    if (Material) GCutMaterial.Reset(Material);
    return Material;
}

void FWowIsometricCamera::PrepareStandaloneMaterials()
{
    GetCutawayMaterial();
    GetSilhouetteMaterial(); // r626
}
