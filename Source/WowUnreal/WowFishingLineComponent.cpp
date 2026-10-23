#include "WowFishingLineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

UWowFishingLineComponent::UWowFishingLineComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // after the caster's pose for this frame
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetCastShadow(false);
    bUseAsyncCooking = true;
}

bool UWowFishingLineComponent::ResolveRodTip(FVector& OutTip) const
{
    const AActor* Owner = Caster.Get();
    if (!Owner) return false;
    // The rod is the held main-hand item (fishing poles go in the main hand). Its tip is the far
    // end of the model's long axis, away from the grip at the component origin.
    TInlineComponentArray<UStaticMeshComponent*> Items(Owner);
    for (const UStaticMeshComponent* Item : Items)
    {
        if (!Item || !Item->GetStaticMesh() || !Item->ComponentHasTag(TEXT("WowInventorySlot=15"))) continue;
        const FBox Local = Item->GetStaticMesh()->GetBoundingBox();
        const FVector Extent = Local.GetExtent();
        const FVector Center = Local.GetCenter();
        const int32 Axis = Extent.X >= Extent.Y && Extent.X >= Extent.Z ? 0 : (Extent.Y >= Extent.Z ? 1 : 2);
        FVector A = Center, B = Center;
        A[Axis] = Local.Min[Axis];
        B[Axis] = Local.Max[Axis];
        OutTip = Item->GetComponentTransform().TransformPosition(A.SizeSquared() > B.SizeSquared() ? A : B);
        return true;
    }
    if (const USkeletalMeshComponent* Body = Owner->FindComponentByClass<USkeletalMeshComponent>())
    {
        if (Body->DoesSocketExist(TEXT("WowSpellRightHand")))
        {
            OutTip = Body->GetSocketLocation(TEXT("WowSpellRightHand"));
            return true;
        }
    }
    OutTip = Owner->GetActorLocation() + FVector(0, 0, 120.0f);
    return true;
}

void UWowFishingLineComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    FVector Tip;
    if (!ResolveRodTip(Tip))
    {
        ClearAllMeshSections();
        return;
    }
    if (!bMaterialSet)
    {
        bMaterialSet = true;
        if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
        {
            UMaterialInstanceDynamic* Line = UMaterialInstanceDynamic::Create(Base, this);
            Line->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.75f, 0.75f, 0.72f));
            SetMaterial(0, Line);
        }
    }
    // Bobber end: the top of the float. Sag grows with the length, as a slack cord.
    const FTransform& ToLocal = GetComponentTransform();
    const FVector Bobber = GetComponentLocation() + FVector(0, 0, 6.0f);
    const float Length = FVector::Dist(Tip, Bobber);
    const float Sag = FMath::Min(Length * 0.06f, 50.0f);
    constexpr int32 Segments = 16;
    constexpr float HalfWidth = 0.35f;
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    Vertices.Reserve((Segments + 1) * 4);
    const FVector Dir = (Bobber - Tip).GetSafeNormal();
    FVector Side = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
    if (Side.IsNearlyZero()) Side = FVector::RightVector;
    const FVector Up = FVector::CrossProduct(Side, Dir).GetSafeNormal();
    for (int32 I = 0; I <= Segments; ++I)
    {
        const float T = float(I) / Segments;
        const FVector P = FMath::Lerp(Tip, Bobber, T) - FVector(0, 0, Sag * 4.0f * T * (1.0f - T));
        // Two crossed strips so the cord reads from any side.
        for (const FVector& Offset : { Side * HalfWidth, Up * HalfWidth })
        {
            Vertices.Add(ToLocal.InverseTransformPosition(P - Offset));
            Vertices.Add(ToLocal.InverseTransformPosition(P + Offset));
            Normals.Add(FVector::UpVector);
            Normals.Add(FVector::UpVector);
        }
        if (I > 0)
        {
            const int32 Base = (I - 1) * 4, Next = I * 4;
            for (int32 Strip = 0; Strip < 2; ++Strip)
            {
                const int32 A = Base + Strip * 2, B = A + 1, C = Next + Strip * 2, D = C + 1;
                Triangles.Append({ A, C, B, B, C, D, A, B, C, B, D, C }); // both faces
            }
        }
    }
    const TArray<FVector2D> NoUV;
    const TArray<FLinearColor> NoColors;
    const TArray<FProcMeshTangent> NoTangents;
    if (GetNumSections() > 0)
        UpdateMeshSection_LinearColor(0, Vertices, Normals, NoUV, NoColors, NoTangents);
    else
        CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, NoUV, NoColors, NoTangents, false);
}
