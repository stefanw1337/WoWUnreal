#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Formats/WmoTypes.h"
#include "WowWmoPortalCulling.generated.h"

class UPrimitiveComponent;
class APlayerController;

/**
 * r539 (Stefan, Stormwind: "we're not supposed to see the outside one from the inside, and the inside one
 * from the outside"): WMO room/portal visibility the way the 3.3.5 client does it.
 *
 * Stormwind's districts are interior groups (MOGP 0x2000) and Blizzard built some walls twice: the Trade
 * District gate towers exist once in the street group 257 (lower, seen from the inside) and once in the
 * Valley of Heroes group 46 (taller, seen from the outside). The original client draws a group only when
 * the camera is in it or sees it through a chain of portals, so only one copy is ever on screen.
 *
 *  - Camera group: of the groups whose bounding box holds the camera, the one with the nearest floor
 *    straight below (its own triangles). Interior -> start there. Otherwise the camera is outside and
 *    every exterior group is the start.
 *  - From each start group, walk its portals: the camera must be on the group's side of the portal and
 *    the portal must show on screen inside the opening it was reached through (screen-rectangle
 *    narrowing). Groups reached are drawn.
 *  - An exterior group reached from inside also draws the other exterior groups seen through that opening.
 *  - ALWAYSDRAW groups (0x10000) are always drawn; portal-only backdrops (Stormwind 92/93) keep the
 *    UWowWmoBackdropVisibility rule. A WMO without portals or interior groups is left alone.
 *
 * Hiding is per player view (APlayerController::HiddenPrimitiveComponents): collision, scene captures,
 * the minimap and satellite captures still see everything. Doodads follow the groups that list them (MODR).
 * Option "WMO portal culling" (world option "portalculling") switches it off.
 */
UCLASS()
class WOWWORLD_API UWowWmoPortalCulling : public UActorComponent
{
    GENERATED_BODY()
public:
    UWowWmoPortalCulling();

    /** Root portals (shared by all groups). Call once, before the groups. */
    void SetRoot(USceneComponent* InRootComp, const FWmoRootData& Root);
    /** One rendered group: its component, flags, box and triangles (raw WMO coordinates). */
    void AddGroup(UPrimitiveComponent* Mesh, int32 GroupIndex, const FWmoGroupData& Group);
    /** An embedded doodad (MODD index); it is drawn while any group that lists it is drawn. */
    void AddDoodad(USceneComponent* DoodadRoot, int32 DefIndex);

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
    struct FPortal { TArray<FVector3f> Verts; FVector3f Normal = FVector3f::ZeroVector; float Distance = 0; TArray<int32, TInlineAllocator<2>> Groups; };
    struct FGroupRef { int32 Portal = 0; int32 Side = 0; };
    struct FGroup
    {
        TWeakObjectPtr<UPrimitiveComponent> Mesh;
        uint32 Flags = 0;
        FBox Box = FBox(ForceInit);
        TArray<FVector3f> Verts;
        TArray<uint16> Indices;
        // XY grid of triangle indices for the floor test.
        FVector2f GridMin = FVector2f::ZeroVector;
        int32 GridW = 0, GridH = 0;
        float Cell = 16.f;
        TArray<TArray<int32>> Cells;
        TArray<FGroupRef> Portals;
        TArray<int32> DoodadDefs;
        bool bValid = false;
        bool bBackdrop = false;
        bool bSkyline = false; // r562: always-drawn distant backdrop (Stormwind Keep's mountain, group 82)
    };
    struct FDoodad { TWeakObjectPtr<USceneComponent> Root; TArray<int32, TInlineAllocator<2>> Groups; bool bHidden = false; };

    // r547 (Stefan, Trade District street in our client: "we need the top layer towers removed from this
    // angle"): back to the r539 rule, every 0x2000 group is a room. The dark arch in his original-client
    // screenshot is the gate tunnel (groups 29 "The Park" / 45), reached through the gate portal - not
    // the Valley of Heroes group 46, whose taller copy the original hides from the street (r543 was wrong).
    bool IsInterior(const FGroup& G) const { return (G.Flags & 0x2000u) != 0; }
    bool IsAlways(const FGroup& G) const { return (G.Flags & 0x10000u) != 0; }
    bool FloorBelow(const FGroup& G, const FVector3f& P, float& OutDown, float& OutUp) const;
    int32 FindCameraGroup(const FVector3f& P) const;
    bool ProjectPolygon(const TArray<FVector3f>& Verts, FBox2f& OutRect) const;
    bool ProjectBox(const FBox& Box, FBox2f& OutRect) const;
    bool Evaluate(const FVector& WorldCamera, const FRotator& WorldRotation, float FovDegrees, float Aspect, const FVector* WorldPlayerEye = nullptr);
    void ApplyHidden(APlayerController* PC, bool bEnabled);
    void SetPrimHidden(APlayerController* PC, UPrimitiveComponent* Prim, bool bHide);
    void ShowAll(APlayerController* PC);

    TWeakObjectPtr<USceneComponent> RootComp;
    TArray<FPortal> Portals;
    TArray<FGroup> Groups;
    TMap<int32, FDoodad> Doodads;
    TMap<int32, TArray<int32>> PendingDoodadGroups; // doodad def -> groups (from MODR), before the doodad spawns
    TBitArray<> Visible;
    TSet<TWeakObjectPtr<UPrimitiveComponent>> HiddenPrims;
    TWeakObjectPtr<APlayerController> HiddenFor;

    // Camera, in raw WMO coordinates.
    FVector3f CamPos = FVector3f::ZeroVector, CamF = FVector3f::ForwardVector, CamR = FVector3f::RightVector, CamU = FVector3f::UpVector;
    float TanHalfX = 1.f, TanHalfY = 1.f;
    FVector3f LastGroupProbe = FVector3f(FLT_MAX);
    int32 CameraGroup = INDEX_NONE;
    int32 FrameCounter = 0;
    int32 LastLoggedGroup = -2;
    bool bHasInterior = false;
    bool bWasEnabled = false;
};
