#include "WowWmoPortalCulling.h"
#include "WowWmoBackdropVisibility.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Coord/WowCoordinate.h"
#include "WowClientPreferences.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowWmoPortals, Log, All);

namespace
{
    constexpr float NearZ = 0.05f;         // yards
    constexpr float PlaneSlack = 0.3f;     // standing in a portal: both sides pass
    constexpr float RectMargin = 0.03f;    // NDC, against popping at screen edges
    constexpr int32 MaxSteps = 4096;
    constexpr int32 MaxDepth = 64;

    FBox2f FullRect() { return FBox2f(FVector2f(-1.f, -1.f), FVector2f(1.f, 1.f)); }

    bool Intersect(const FBox2f& A, const FBox2f& B, FBox2f& Out)
    {
        Out.Min = FVector2f(FMath::Max(A.Min.X, B.Min.X), FMath::Max(A.Min.Y, B.Min.Y));
        Out.Max = FVector2f(FMath::Min(A.Max.X, B.Max.X), FMath::Min(A.Max.Y, B.Max.Y));
        Out.bIsValid = Out.Min.X < Out.Max.X && Out.Min.Y < Out.Max.Y;
        return Out.bIsValid;
    }

    bool Contains(const FBox2f& Outer, const FBox2f& Inner)
    {
        return Outer.bIsValid && Inner.Min.X >= Outer.Min.X && Inner.Min.Y >= Outer.Min.Y &&
            Inner.Max.X <= Outer.Max.X && Inner.Max.Y <= Outer.Max.Y;
    }
}

UWowWmoPortalCulling::UWowWmoPortalCulling()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UWowWmoPortalCulling::SetRoot(USceneComponent* InRootComp, const FWmoRootData& Root)
{
    RootComp = InRootComp;
    Portals.SetNum(Root.Portals.Num());
    for (int32 I = 0; I < Root.Portals.Num(); ++I)
    {
        const auto& Src = Root.Portals[I];
        FPortal& P = Portals[I];
        const double Len = Src.Normal.Size();
        const double Inv = Len > SMALL_NUMBER ? 1.0 / Len : 0.0;
        P.Normal = FVector3f(Src.Normal * Inv);
        P.Distance = float(Src.Distance * Inv);
        for (const FVector& V : Src.Vertices) P.Verts.Add(FVector3f(V));
    }
    Groups.SetNum(FMath::Max<int32>(int32(Root.NumGroups), 0));
    for (const auto& Ref : Root.PortalReferences)
    {
        if (!Portals.IsValidIndex(Ref.Portal) || !Groups.IsValidIndex(Ref.Group)) continue;
        Portals[Ref.Portal].Groups.AddUnique(Ref.Group);
        Groups[Ref.Group].Portals.Add({ int32(Ref.Portal), int32(Ref.Side) });
    }
}

void UWowWmoPortalCulling::AddGroup(UPrimitiveComponent* Mesh, int32 GroupIndex, const FWmoGroupData& Data)
{
    if (!Groups.IsValidIndex(GroupIndex)) Groups.SetNum(GroupIndex + 1);
    FGroup& G = Groups[GroupIndex];
    G.Mesh = Mesh;
    G.Flags = Data.Flags;
    G.Box = Data.BoundingBox;
    G.Verts.Reserve(Data.Vertices.Num());
    for (const FVector& V : Data.Vertices) G.Verts.Add(FVector3f(V));
    G.Indices = Data.Indices;
    G.bValid = Mesh != nullptr;
    // Portal-only skyline backdrops (Stormwind 92 and 93) keep their own viewer-side rule (r291/r376):
    // no other group's portal leads into them, so the walk below would never reach them.
    G.bBackdrop = UWowWmoBackdropVisibility::IsPortalBackdrop(G.Flags);
    if (!G.bBackdrop && (G.Flags & 0x10000u) == 0 && G.Portals.Num() > 0)
    {
        G.bBackdrop = true;
        for (const FGroupRef& Ref : G.Portals)
            if (Portals.IsValidIndex(Ref.Portal) && Portals[Ref.Portal].Groups.Num() > 1) { G.bBackdrop = false; break; }
    }
    // r562 (Stefan: the dark "blob" draped over Stormwind Keep, in the original too, "looks ugly AF"):
    // Stormwind's group 82 (HB03) is a low-detail mountain drawn always (0x10000), 260 yards tall, that
    // makes the keep look as if it sits on a mountain when seen from Elwynn. From inside the city it is
    // the dark smudge over the keep. Always-drawn groups over 150 yards tall are such skyline pieces.
    G.bSkyline = IsAlways(G) && G.Box.GetSize().Z > 150.0;
    bHasInterior |= IsInterior(G);
    LastGroupProbe = FVector3f(FLT_MAX); // re-find the camera's group with the new group in

    // Floor-test grid over the group's XY extent (cells of 16 yards, at most 64 x 64).
    const FVector Size = G.Box.GetSize();
    G.Cell = FMath::Max(16.f, float(FMath::Max(Size.X, Size.Y)) / 64.f);
    G.GridMin = FVector2f(float(G.Box.Min.X), float(G.Box.Min.Y));
    G.GridW = FMath::Clamp(FMath::CeilToInt(float(Size.X) / G.Cell) + 1, 1, 66);
    G.GridH = FMath::Clamp(FMath::CeilToInt(float(Size.Y) / G.Cell) + 1, 1, 66);
    G.Cells.SetNum(G.GridW * G.GridH);
    const int32 NumTris = G.Indices.Num() / 3;
    for (int32 T = 0; T < NumTris; ++T)
    {
        const int32 I0 = G.Indices[T * 3], I1 = G.Indices[T * 3 + 1], I2 = G.Indices[T * 3 + 2];
        if (!G.Verts.IsValidIndex(I0) || !G.Verts.IsValidIndex(I1) || !G.Verts.IsValidIndex(I2)) continue;
        const FVector3f& A = G.Verts[I0]; const FVector3f& B = G.Verts[I1]; const FVector3f& C = G.Verts[I2];
        const int32 X0 = FMath::Clamp(FMath::FloorToInt((FMath::Min3(A.X, B.X, C.X) - G.GridMin.X) / G.Cell), 0, G.GridW - 1);
        const int32 X1 = FMath::Clamp(FMath::FloorToInt((FMath::Max3(A.X, B.X, C.X) - G.GridMin.X) / G.Cell), 0, G.GridW - 1);
        const int32 Y0 = FMath::Clamp(FMath::FloorToInt((FMath::Min3(A.Y, B.Y, C.Y) - G.GridMin.Y) / G.Cell), 0, G.GridH - 1);
        const int32 Y1 = FMath::Clamp(FMath::FloorToInt((FMath::Max3(A.Y, B.Y, C.Y) - G.GridMin.Y) / G.Cell), 0, G.GridH - 1);
        for (int32 Y = Y0; Y <= Y1; ++Y)
            for (int32 X = X0; X <= X1; ++X)
                G.Cells[Y * G.GridW + X].Add(T);
    }

    for (const uint16 Def : Data.DoodadRefs)
    {
        PendingDoodadGroups.FindOrAdd(Def).AddUnique(GroupIndex);
        if (FDoodad* D = Doodads.Find(Def)) D->Groups.AddUnique(GroupIndex);
    }
}

void UWowWmoPortalCulling::AddDoodad(USceneComponent* DoodadRoot, int32 DefIndex)
{
    if (!DoodadRoot) return;
    FDoodad& D = Doodads.FindOrAdd(DefIndex);
    D.Root = DoodadRoot;
    if (const TArray<int32>* Owners = PendingDoodadGroups.Find(DefIndex))
        for (int32 G : *Owners) D.Groups.AddUnique(G);
}

bool UWowWmoPortalCulling::FloorBelow(const FGroup& G, const FVector3f& P, float& OutDown, float& OutUp) const
{
    OutDown = FLT_MAX; OutUp = FLT_MAX;
    if (G.GridW <= 0) return false;
    const int32 X = FMath::FloorToInt((P.X - G.GridMin.X) / G.Cell);
    const int32 Y = FMath::FloorToInt((P.Y - G.GridMin.Y) / G.Cell);
    if (X < 0 || Y < 0 || X >= G.GridW || Y >= G.GridH) return false;
    for (const int32 T : G.Cells[Y * G.GridW + X])
    {
        const FVector3f& A = G.Verts[G.Indices[T * 3]];
        const FVector3f& B = G.Verts[G.Indices[T * 3 + 1]];
        const FVector3f& C = G.Verts[G.Indices[T * 3 + 2]];
        // 2D barycentric in XY.
        const float V0x = C.X - A.X, V0y = C.Y - A.Y, V1x = B.X - A.X, V1y = B.Y - A.Y, V2x = P.X - A.X, V2y = P.Y - A.Y;
        const float D00 = V0x * V0x + V0y * V0y, D01 = V0x * V1x + V0y * V1y, D11 = V1x * V1x + V1y * V1y;
        const float D20 = V2x * V0x + V2y * V0y, D21 = V2x * V1x + V2y * V1y;
        const float Den = D00 * D11 - D01 * D01;
        if (FMath::Abs(Den) < 1e-9f) continue;
        const float U = (D11 * D20 - D01 * D21) / Den, W = (D00 * D21 - D01 * D20) / Den;
        if (U < 0.f || W < 0.f || U + W > 1.f) continue;
        const float Z = A.Z + U * (C.Z - A.Z) + W * (B.Z - A.Z);
        if (Z <= P.Z + 0.05f) OutDown = FMath::Min(OutDown, P.Z - Z);
        else OutUp = FMath::Min(OutUp, Z - P.Z);
    }
    return OutDown < FLT_MAX || OutUp < FLT_MAX;
}

int32 UWowWmoPortalCulling::FindCameraGroup(const FVector3f& P) const
{
    int32 Best = INDEX_NONE;
    float BestScore = FLT_MAX;
    const FVector PD(P);
    for (int32 I = 0; I < Groups.Num(); ++I)
    {
        const FGroup& G = Groups[I];
        if (!G.bValid || !G.Box.ExpandBy(0.5).IsInsideOrOn(PD)) continue;
        float Down, Up;
        if (!FloorBelow(G, P, Down, Up)) continue;
        float Score = FLT_MAX;
        // r550 (Stefan, first person in the arch towards Cathedral Square: the tall outside cathedral
        // stand-in (92) showed and only went away a few steps further): the camera stood in the arch
        // (room 10/40, floor 1.9 below, roof 7 above) but right over the canal walk of exterior group 284
        // (0.2 below), so the nearest floor made it "outside". A room whose floor AND roof enclose the
        // camera wins, like the original client's BSP containment.
        if (IsInterior(G) && Down < FLT_MAX && Up < FLT_MAX) Score = Down - 1000.f;
        else if (Down < FLT_MAX) Score = Down - (IsInterior(G) ? 0.01f : 0.f); // a tie goes to the room
        else if (IsInterior(G)) Score = 1000.f + Up;                      // under its roof, over a gap
        if (Score < BestScore) { BestScore = Score; Best = I; }
    }
    return Best;
}

bool UWowWmoPortalCulling::ProjectPolygon(const TArray<FVector3f>& Verts, FBox2f& OutRect) const
{
    if (Verts.Num() < 3) return false;
    TArray<FVector3f, TInlineAllocator<16>> In, Clipped;
    for (const FVector3f& V : Verts)
    {
        const FVector3f Rel = V - CamPos;
        In.Add(FVector3f(FVector3f::DotProduct(Rel, CamR), FVector3f::DotProduct(Rel, CamU), FVector3f::DotProduct(Rel, CamF)));
    }
    for (int32 I = 0; I < In.Num(); ++I)
    {
        const FVector3f& A = In[I];
        const FVector3f& B = In[(I + 1) % In.Num()];
        const bool AIn = A.Z >= NearZ, BIn = B.Z >= NearZ;
        if (AIn) Clipped.Add(A);
        if (AIn != BIn) Clipped.Add(A + (B - A) * ((NearZ - A.Z) / (B.Z - A.Z)));
    }
    if (Clipped.Num() < 3) return false;
    OutRect = FBox2f(ForceInit);
    for (const FVector3f& C : Clipped)
        OutRect += FVector2f(C.X / (C.Z * TanHalfX), C.Y / (C.Z * TanHalfY));
    OutRect = OutRect.ExpandBy(RectMargin);
    return OutRect.bIsValid;
}

bool UWowWmoPortalCulling::ProjectBox(const FBox& Box, FBox2f& OutRect) const
{
    if (Box.ExpandBy(1.0).IsInsideOrOn(FVector(CamPos))) { OutRect = FullRect(); return true; }
    OutRect = FBox2f(ForceInit);
    for (int32 I = 0; I < 8; ++I)
    {
        const FVector3f Corner((I & 1) ? Box.Max.X : Box.Min.X, (I & 2) ? Box.Max.Y : Box.Min.Y, (I & 4) ? Box.Max.Z : Box.Min.Z);
        const FVector3f Rel = Corner - CamPos;
        const float Z = FVector3f::DotProduct(Rel, CamF);
        if (Z < NearZ) { OutRect = FullRect(); return true; } // straddles the camera: keep it
        OutRect += FVector2f(FVector3f::DotProduct(Rel, CamR) / (Z * TanHalfX), FVector3f::DotProduct(Rel, CamU) / (Z * TanHalfY));
    }
    OutRect = OutRect.ExpandBy(RectMargin);
    return OutRect.bIsValid;
}

bool UWowWmoPortalCulling::Evaluate(const FVector& WorldCamera, const FRotator& WorldRotation, float FovDegrees, float Aspect, const FVector* WorldPlayerEye)
{
    const USceneComponent* Root = RootComp.Get();
    if (!Root) return false;
    const FTransform& X = Root->GetComponentTransform();
    // UE local = (wmo.Y, wmo.X, wmo.Z) * SCALE, as WmoLocalToUELocal.
    auto ToWmoPos = [&](const FVector& W) { const FVector L = X.InverseTransformPosition(W) / FWowCoordinate::SCALE; return FVector3f(float(L.Y), float(L.X), float(L.Z)); };
    auto ToWmoDir = [&](const FVector& W) { const FVector L = X.InverseTransformVectorNoScale(W); return FVector3f(float(L.Y), float(L.X), float(L.Z)).GetSafeNormal(); };
    const FRotationMatrix M(WorldRotation);
    CamPos = ToWmoPos(WorldCamera);
    CamF = ToWmoDir(M.GetUnitAxis(EAxis::X));
    CamR = ToWmoDir(M.GetUnitAxis(EAxis::Y));
    CamU = ToWmoDir(M.GetUnitAxis(EAxis::Z));
    TanHalfX = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FovDegrees, 10.f, 170.f) * 0.5f));
    TanHalfY = TanHalfX / FMath::Max(Aspect, 0.2f);

    if (FVector3f::DistSquared(CamPos, LastGroupProbe) > 0.25f * 0.25f)
    {
        LastGroupProbe = CamPos;
        CameraGroup = FindCameraGroup(CamPos);
        // r559 (Stefan, Trade District square with the camera zoomed far out: the outer gate towers showed,
        // and his right-clicks on them hit the exterior canal group 282): the camera, about 45 yards over
        // the street, rose above the top of the street group's box (257 ends 44 yards up), so it stood
        // in no group and every exterior group was drawn. The original client's camera never gets that
        // far out. A camera in no group takes the player's own room: if his eyes are in an interior group,
        // the view starts there, with the portals still judged from the camera.
        if (CameraGroup == INDEX_NONE && WorldPlayerEye)
        {
            const int32 PlayerGroup = FindCameraGroup(ToWmoPos(*WorldPlayerEye));
            if (Groups.IsValidIndex(PlayerGroup) && IsInterior(Groups[PlayerGroup])) CameraGroup = PlayerGroup;
        }
    }

    const int32 N = Groups.Num();
    Visible.Init(false, N);
    TArray<FBox2f> Drawn; Drawn.Init(FBox2f(ForceInit), N);
    struct FStep { int32 Group; FBox2f Rect; int32 Depth; };
    TArray<FStep> Stack;
    const bool bInside = Groups.IsValidIndex(CameraGroup) && IsInterior(Groups[CameraGroup]);
    // r579 (Stefan, Stormwind, checked against the original client): the city's districts are large open-air
    // "rooms" (footprint over 10,000 square yards) joined to the canals and the Valley of Heroes only through
    // small arch and bridge rooms. Seen from a district through those openings, the outside groups popped in
    // and out with range and angle (Cathedral Square: whole canal walls and streets gone, bare ground and
    // water showing; "this is bad"), and drew whole where they should not (Trade District: the outer wall
    // behind the Valley gate, 46, and the doubled canal wall, 281, above the district walls). From a district
    // the portal walk no longer decides: every group is drawn, as without portal culling - except in the
    // Trade District, where Stefan's original-client screenshots show no outside piece at all ("they need to
    // be hidden in Trade District and not in the canals"); there the outside groups are never drawn.
    const bool bOpenDistrict = bInside && !IsAlways(Groups[CameraGroup]) && Groups[CameraGroup].Box.GetSize().X * Groups[CameraGroup].Box.GetSize().Y > 10000.0;
    for (int32 I = 0; I < N; ++I)
    {
        const FGroup& G = Groups[I];
        if (!G.bValid) continue;
        if (IsAlways(G)) Visible[I] = true;
        // r562 hid the whole skyline group here while in the city; r577: that took the far keep's towers
        // with it. The option now removes only the painted mountain card when the group is built
        // (WowWmoRenderer), so the group itself is never hidden.
        // r541: Stormwind keeps three cathedrals: the real one (95, about 126 yards), a taller stand-in seen
        // over the Trade District roofs (93, interior, 182) and the tallest, seen over the city wall from
        // outside (92, exterior, 249). An exterior backdrop is for the outside view only, an interior one
        // for the inside view only; within that, UWowWmoBackdropVisibility's viewer-side rule decides.
        if (G.bBackdrop) Visible[I] = IsInterior(G) == bInside;
        if (!bInside && !IsInterior(G)) { Visible[I] = true; Drawn[I] = FullRect(); Stack.Add({ I, FullRect(), 0 }); }
    }
    if (bInside) { Visible[CameraGroup] = true; Drawn[CameraGroup] = FullRect(); Stack.Add({ CameraGroup, FullRect(), 0 }); }

    int32 Steps = 0;
    while (Stack.Num() > 0 && Steps++ < MaxSteps)
    {
        const FStep Step = Stack.Pop(EAllowShrinking::No);
        for (const FGroupRef& Ref : Groups[Step.Group].Portals)
        {
            const FPortal& P = Portals[Ref.Portal];
            const float D = FVector3f::DotProduct(P.Normal, CamPos) + P.Distance;
            // The group lies on the side opposite its reference sign; the camera must be there too.
            FBox2f Rect;
            if (FMath::Abs(D) <= PlaneSlack) Rect = Step.Rect;
            else
            {
                if (D * float(Ref.Side) >= 0.f) continue;
                FBox2f Projected;
                if (!ProjectPolygon(P.Verts, Projected) || !Intersect(Step.Rect, Projected, Rect)) continue;
            }
            for (const int32 Other : P.Groups)
            {
                if (Other == Step.Group || !Groups.IsValidIndex(Other) || !Groups[Other].bValid) continue;
                if (Contains(Drawn[Other], Rect)) continue;
                Drawn[Other] = Drawn[Other].bIsValid ? Drawn[Other] + Rect : Rect;
                Visible[Other] = true;
                if (bInside && !IsInterior(Groups[Other]))
                {
                    // Out through a door or the sky: the other exterior groups seen through this opening.
                    for (int32 E = 0; E < N; ++E)
                    {
                        if (Visible[E] || !Groups[E].bValid || IsInterior(Groups[E]) || Groups[E].bBackdrop) continue;
                        FBox2f BoxRect, Hit;
                        if (ProjectBox(Groups[E].Box, BoxRect) && Intersect(Rect, BoxRect, Hit)) Visible[E] = true;
                    }
                }
                if (Step.Depth < MaxDepth) Stack.Add({ Other, Rect, Step.Depth + 1 });
            }
        }
    }
    // r582 (Stefan, Cathedral Square: a cathedral tower vanished at one camera angle - "this is bad, I never
    // wanted this much hidden or unstable"): the portal walk no longer hides anything anywhere. It still runs
    // (it finds the camera's room and logs what it would draw), but every group is drawn, from inside and
    // outside alike. Only the deliberate, stable rules remain: the cathedral stand-ins (backdrops, inside or
    // outside view) and, in the Trade District, no outside groups.
    {
        // Stormwind.wmo (286 groups): group 257 is "Trade District" (MOGN "bazaar01").
        bool bTradeDistrict = bOpenDistrict && N == 286 && CameraGroup == 257;
        // r727 (Stefan, Trade District at night: "Also found an angle that the extra outer wall wasn't hidden in"):
        // on parts of the street the camera's room is not the district itself but one of its buildings (group 249),
        // whose box reaches out over the street; the log flipped between 257 and 249 as the camera moved, and from
        // 249 the outer wall and canal groups (46, 280-285) were drawn again over the roofs. A camera inside a room
        // that itself lies inside the Trade District's box counts as in the district.
        if (!bTradeDistrict && bInside && N == 286 && Groups[257].bValid && Groups.IsValidIndex(CameraGroup))
        {
            const FBox& District = Groups[257].Box;
            bTradeDistrict = District.IsInside(FVector(CamPos)) && District.IsInside(Groups[CameraGroup].Box.GetCenter());
        }
        // r593 (Stefan, from the canal by the Trade District: dark blades and lamps floating over the roofs,
        // not there in the original client - "whatever this is, it needs to be hidden ... once you get inside
        // the mage district then they can be shown"): the Mage Quarter is one cluster of 47 interior groups
        // around its district room 105, joined to the outside only through the canal groups 282/283. From
        // outside that cluster its groups follow the portal walk again (drawn only when seen through its
        // gates, as in the original client); from inside it everything is drawn as before.
        TArray<bool> MageQuarter;
        if (N == 286 && Groups[105].bValid && IsInterior(Groups[105]))
        {
            MageQuarter.Init(false, N);
            MageQuarter[105] = true;
            TArray<int32, TInlineAllocator<64>> Open; Open.Add(105);
            while (Open.Num() > 0)
            {
                const int32 A = Open.Pop(EAllowShrinking::No);
                for (const FGroupRef& Ref : Groups[A].Portals)
                    if (Portals.IsValidIndex(Ref.Portal))
                        for (const int32 B : Portals[Ref.Portal].Groups)
                            if (Groups.IsValidIndex(B) && !MageQuarter[B] && IsInterior(Groups[B])) { MageQuarter[B] = true; Open.Add(B); }
            }
        }
        const bool bInMageQuarter = MageQuarter.IsValidIndex(CameraGroup) && MageQuarter[CameraGroup];
        for (int32 I = 0; I < N; ++I)
        {
            const FGroup& G = Groups[I];
            if (!G.bValid || G.bBackdrop) continue;
            if (MageQuarter.IsValidIndex(I) && MageQuarter[I] && !bInMageQuarter) continue; // keeps the walk's result
            Visible[I] = IsInterior(G) || !bTradeDistrict;
        }
    }

    // r560: from inside, also log which exterior groups are drawn whenever that list changes, so a
    // right-click on a wrongly shown outside piece (WmoGroup_N) can be matched to how it was reached.
    if (bInside)
    {
        static TMap<const UWowWmoPortalCulling*, FString> LastExterior;
        FString Exterior;
        for (int32 I = 0; I < N; ++I)
            if (Visible[I] && Groups[I].bValid && !IsInterior(Groups[I])) Exterior += FString::Printf(TEXT(" %d"), I);
        FString& Last = LastExterior.FindOrAdd(this);
        if (Exterior != Last)
        {
            Last = Exterior;
            UE_LOG(LogWowWmoPortals, Log, TEXT("WMO portal culling %s: from interior group %d the exterior groups drawn are [%s ] (camera WMO-local %s)"),
                *GetNameSafe(GetOwner()), CameraGroup, *Exterior, *FVector(CamPos).ToCompactString());
        }
    }

    if (CameraGroup != LastLoggedGroup && (bInside || LastLoggedGroup >= 0))
    {
        LastLoggedGroup = CameraGroup;
        int32 Shown = 0; for (int32 I = 0; I < N; ++I) Shown += Visible[I] ? 1 : 0;
        UE_LOG(LogWowWmoPortals, Log, TEXT("WMO portal culling %s: camera in %s group %d, %d/%d groups drawn (camera WMO-local %s)"),
            *GetNameSafe(GetOwner()), bInside ? TEXT("interior") : TEXT("exterior/outside"), CameraGroup, Shown, N,
            *FVector(CamPos).ToCompactString());
    }
    return true;
}

void UWowWmoPortalCulling::SetPrimHidden(APlayerController* PC, UPrimitiveComponent* Prim, bool bHide)
{
    if (!Prim) return;
    const TWeakObjectPtr<UPrimitiveComponent> Key(Prim);
    if (bHide)
    {
        bool bAlready = false;
        HiddenPrims.Add(Key, &bAlready);
        if (!bAlready) PC->HiddenPrimitiveComponents.Add(Key);
    }
    else if (HiddenPrims.Remove(Key) > 0)
    {
        PC->HiddenPrimitiveComponents.RemoveSingleSwap(Key, EAllowShrinking::No);
    }
}

void UWowWmoPortalCulling::ShowAll(APlayerController* PC)
{
    if (PC)
        for (const auto& Key : HiddenPrims) PC->HiddenPrimitiveComponents.RemoveSingleSwap(Key, EAllowShrinking::No);
    HiddenPrims.Reset();
}

void UWowWmoPortalCulling::ApplyHidden(APlayerController* PC, bool bEnabled)
{
    for (int32 I = 0; I < Groups.Num(); ++I)
        if (UPrimitiveComponent* Mesh = Groups[I].Mesh.Get())
            SetPrimHidden(PC, Mesh, bEnabled && Groups[I].bValid && Visible.IsValidIndex(I) && !Visible[I]);
    for (auto& Pair : Doodads)
    {
        FDoodad& D = Pair.Value;
        USceneComponent* DRoot = D.Root.Get();
        if (!DRoot) continue;
        bool bHide = bEnabled && D.Groups.Num() > 0;
        for (int32 G : D.Groups) if (!Visible.IsValidIndex(G) || Visible[G]) { bHide = false; break; }
        if (bHide == D.bHidden) continue;
        D.bHidden = bHide;
        TArray<USceneComponent*> Children;
        DRoot->GetChildrenComponents(true, Children);
        Children.Add(DRoot);
        for (USceneComponent* C : Children)
            if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(C)) SetPrimHidden(PC, Prim, bHide);
    }
}

void UWowWmoPortalCulling::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime, TickType, Function);
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (!PC || !PC->PlayerCameraManager) return;
    if (HiddenFor.Get() != PC)
    {
        HiddenPrims.Reset(); // a new controller starts with an empty list
        for (auto& Pair : Doodads) Pair.Value.bHidden = false;
        HiddenFor = PC;
    }

    const bool bEnabled = bHasInterior && Portals.Num() > 0 && FWowClientPreferences::GetWorldOption(TEXT("portalculling"));
    if (!bEnabled)
    {
        if (bWasEnabled)
        {
            ShowAll(PC);
            for (auto& Pair : Doodads) Pair.Value.bHidden = false;
        }
        bWasEnabled = false;
        return;
    }
    bWasEnabled = true;

    // Far away (camera outside the box by more than 600 yards): every 8th frame is plenty.
    const FVector Camera = PC->PlayerCameraManager->GetCameraLocation();
    if (const USceneComponent* Root = RootComp.Get())
    {
        FBox Box(ForceInit);
        for (const FGroup& G : Groups) if (G.bValid) Box += G.Box;
        const FVector L = Root->GetComponentTransform().InverseTransformPosition(Camera) / FWowCoordinate::SCALE;
        const double Dist = Box.IsValid ? FMath::Sqrt(Box.ComputeSquaredDistanceToPoint(FVector(L.Y, L.X, L.Z))) : 0.0;
        if (Dist > 600.0 && (++FrameCounter & 7) != 0) return;
    }

    float Fov = PC->PlayerCameraManager->GetFOVAngle();
    float Aspect = 16.f / 9.f;
    if (GEngine && GEngine->GameViewport)
    {
        FVector2D Size; GEngine->GameViewport->GetViewportSize(Size);
        if (Size.Y > 1.0) Aspect = float(Size.X / Size.Y);
    }
    if (GEngine && GEngine->IsStereoscopic3D()) { Fov = FMath::Max(Fov, 120.f); Aspect = 1.f; } // VR: generous per-eye frustum
    FVector PlayerEye;
    const FVector* PlayerEyePtr = nullptr;
    if (const APawn* Pawn = PC->GetPawn()) { PlayerEye = Pawn->GetPawnViewLocation(); PlayerEyePtr = &PlayerEye; }
    if (Evaluate(Camera, PC->PlayerCameraManager->GetCameraRotation(), Fov, Aspect, PlayerEyePtr)) ApplyHidden(PC, true);
}

void UWowWmoPortalCulling::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    ShowAll(HiddenFor.Get());
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}
