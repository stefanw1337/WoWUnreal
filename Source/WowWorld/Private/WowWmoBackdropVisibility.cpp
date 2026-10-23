#include "WowWmoBackdropVisibility.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Coord/WowCoordinate.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowWmoBackdrop, Log, All);

UWowWmoBackdropVisibility::UWowWmoBackdropVisibility()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
}
bool UWowWmoBackdropVisibility::IsPortalBackdrop(uint32 Flags)
{
    // UNREACHABLE, but neither EXTERIOR nor ALWAYSDRAW. Do not hide the
    // always-drawn mountain scenery that uses the same UNREACHABLE bit.
    return (Flags&0x80u)!=0 && (Flags&(0x8u|0x10000u))==0;
}
bool UWowWmoBackdropVisibility::IsPortalOnlyGroup(const FWmoRootData& Root,int32 Group)
{
    // r291: an exterior LOD facade (cathy_facade01) carries portals that lead nowhere; a real
    // room/exterior shares each portal with the group on its other side.
    bool bAny=false;
    for (const auto& Ref:Root.PortalReferences)
    {
        if (Ref.Group!=Group) continue;
        bAny=true;
        for (const auto& Other:Root.PortalReferences)
            if (Other.Portal==Ref.Portal && Other.Group!=Group) return false;
    }
    return bAny;
}
bool UWowWmoBackdropVisibility::IsBackdropGroup(const FWmoRootData& Root,int32 Group,uint32 Flags)
{
    return IsPortalBackdrop(Flags) || ((Flags&0x10000u)==0 && IsPortalOnlyGroup(Root,Group));
}
TArray<FVector4> UWowWmoBackdropVisibility::GetViewerPlanes(const FWmoRootData& Root,int32 Group)
{
    TArray<FVector4> Planes;
    for (const auto& Ref:Root.PortalReferences)
    {
        if (Ref.Group!=Group || !Root.Portals.IsValidIndex(Ref.Portal) || Ref.Side==0) continue;
        const auto& Portal=Root.Portals[Ref.Portal];
        const double Length=Portal.Normal.Size();
        if (Length<SMALL_NUMBER || !FMath::IsFinite(Portal.Distance) || Portal.Normal.ContainsNaN()) continue;
        const double Factor=(Ref.Side<0?-1.:1.)/Length;
        Planes.Add(FVector4(Portal.Normal*Factor,Portal.Distance*Factor));
    }
    return Planes;
}
bool UWowWmoBackdropVisibility::CanView(const TArray<FVector4>& Planes,const FVector& WmoCamera)
{
    if (Planes.IsEmpty()) return true; // Missing metadata must not punch new holes.
    for (const auto& Plane:Planes)
        if (Plane.X*WmoCamera.X+Plane.Y*WmoCamera.Y+Plane.Z*WmoCamera.Z+Plane.W>=-.02) return true;
    return false;
}
void UWowWmoBackdropVisibility::AddGroup(USceneComponent* Mesh,const FWmoRootData& Root,int32 Group)
{
    FEntry Entry;Entry.Mesh=Mesh;Entry.Planes=GetViewerPlanes(Root,Group);Entry.Group=Group;
    UE_LOG(LogWowWmoBackdrop,Log,TEXT("WMO backdrop group %d on %s: %d viewer planes"),Group,*GetNameSafe(GetOwner()),Entry.Planes.Num());
    if (Mesh && !Entry.Planes.IsEmpty()) Entries.Add(MoveTemp(Entry));
}
void UWowWmoBackdropVisibility::ApplyCamera(const FVector& WorldCamera)
{
    if (!GetOwner()) return;
    const FVector Local=GetOwner()->GetActorTransform().InverseTransformPosition(WorldCamera)/FWowCoordinate::SCALE;
    const FVector WmoCamera(Local.Y,Local.X,Local.Z);
    for (const auto& Entry:Entries)
        if (auto* Mesh=Entry.Mesh.Get())
        {
            const bool Visible=CanView(Entry.Planes,WmoCamera);
            if (Mesh->IsVisible()!=Visible)
            {
                Mesh->SetVisibility(Visible);
                // r376 (Stefan: holes in Stormwind - tower by the bank, inside the city gate): say when
                // a backdrop group is switched, with the camera, to see whether these holes are ours.
                UE_LOG(LogWowWmoBackdrop,Log,TEXT("WMO backdrop group %d on %s %s (camera WMO-local %s, world %s)"),
                    Entry.Group,*GetNameSafe(GetOwner()),Visible?TEXT("shown"):TEXT("hidden"),
                    *WmoCamera.ToCompactString(),*WorldCamera.ToCompactString());
            }
        }
}
void UWowWmoBackdropVisibility::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime,TickType,Function);
    const auto* Controller=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr;
    if (Controller && Controller->PlayerCameraManager)
        ApplyCamera(Controller->PlayerCameraManager->GetCameraLocation());
}
