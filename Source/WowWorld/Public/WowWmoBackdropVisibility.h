#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Formats/WmoTypes.h"
#include "WowWmoBackdropVisibility.generated.h"

/** Reject portal-only skyline scenery when viewed from behind its authored opening.
 * This is not general room/portal culling: ordinary buildings stay unaffected. */
UCLASS()
class WOWWORLD_API UWowWmoBackdropVisibility : public UActorComponent
{
    GENERATED_BODY()
public:
    UWowWmoBackdropVisibility();
    static bool IsPortalBackdrop(uint32 Flags);
    /** r291: every portal this group references belongs to no other group (Stormwind's
     * duplicate cathedral facade 92 and bazaar skyline 93). */
    static bool IsPortalOnlyGroup(const FWmoRootData& Root,int32 Group);
    static bool IsBackdropGroup(const FWmoRootData& Root,int32 Group,uint32 Flags);
    static TArray<FVector4> GetViewerPlanes(const FWmoRootData& Root,int32 Group);
    static bool CanView(const TArray<FVector4>& Planes,const FVector& WmoCamera);
    void AddGroup(USceneComponent* Mesh,const FWmoRootData& Root,int32 Group);
    void ApplyCamera(const FVector& WorldCamera);
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Function) override;
private:
    struct FEntry { TWeakObjectPtr<USceneComponent> Mesh; TArray<FVector4> Planes; int32 Group = INDEX_NONE; };
    TArray<FEntry> Entries;
};
