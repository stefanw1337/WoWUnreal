#pragma once
#include "CoreMinimal.h"
class AActor;
class UMeshComponent;
struct FM2Data;
struct FWmoGroupData;
struct FWmoRootData;
struct WOWWORLD_API FWowLampFixtures
{
    static bool IsSupported(const FString& Path);
    static bool IsGlass(const FString& Path);
    static bool IsOutdoor(const FString& Path);
    static bool IsNightWindow(uint32 Flags,const FString& Path);
    static void AttachWmoWindows(AActor* Owner,UMeshComponent* Mesh,const FWmoGroupData& Data,const FWmoRootData& Root);
    static bool FindM2Glass(const FM2Data& Data,FBox& Bounds,TArray<int32>& Slots,const FString& Path=FString());
    static bool FindM2Source(const FM2Data& Data,const FString& Path,FVector& Position,FLinearColor& Color,TArray<int32>& Slots);
    static bool AttachM2(AActor* Owner,UMeshComponent* Mesh,const FM2Data& Data,
        const FString& Path,const FTransform& Transform);
    static void AttachWmo(AActor* Owner,UMeshComponent* Mesh,const FWmoGroupData& Data,
        const FWmoRootData& Root);
};
