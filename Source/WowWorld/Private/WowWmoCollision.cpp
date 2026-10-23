#include "WowWmoCollision.h"
#include "Formats/WmoTypes.h"
#include "Coord/WowCoordinate.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodySetup.h"

bool FWowWmoCollision::Attach(AActor* Owner, USceneComponent* Parent,
    const FWmoGroupData& Group, uint32 GroupIndex)
{
    if (!Owner || !Parent || Group.Indices.Num() == 0 ||
        Group.Indices.Num()%3 != 0 || Group.TriangleMaterials.Num()!=Group.Indices.Num()/3)
        return false;
    TArray<int32> Remap; Remap.Init(INDEX_NONE,Group.Vertices.Num());
    TArray<FVector> Vertices;
    TArray<int32> Indices, StepIndices, TreadIndices;
    for (int32 Face=0;Face<Group.TriangleMaterials.Num();++Face)
    {
        const int32 A=Group.Indices[Face*3], B=Group.Indices[Face*3+1], C=Group.Indices[Face*3+2];
        if (!Group.Vertices.IsValidIndex(A) || !Group.Vertices.IsValidIndex(B) || !Group.Vertices.IsValidIndex(C)) return false;
        const FVector& P=Group.Vertices[A]; const FVector& Q=Group.Vertices[B]; const FVector& R=Group.Vertices[C];
        const FVector Normal=FVector::CrossProduct(Q-P,R-P).GetSafeNormal();
        // r335 (Stefan: on stairs the feet hover on the invisible ramp): visible faces without
        // collision that are flat (stair treads drawn over a collision ramp) go into a query-only
        // surface that only the foot IK traces, so a foot stands on the step it is drawn on.
        const bool bTread=!Group.TriangleMaterials[Face].HasCollision() && Group.TriangleMaterials[Face].MaterialId!=0xff
            && FMath::Abs(Normal.Z)>0.92;
        if (!Group.TriangleMaterials[Face].HasCollision() && !bTread) continue;
        const double Rise=FMath::Max3(P.Z,Q.Z,R.Z)-FMath::Min3(P.Z,Q.Z,R.Z);
        // Original collision-only curb wedges may be steeper than ordinary terrain.
        // Restrict the exception to short upward faces; never walls or tall slopes.
        // r340 (Stefan: stuck at the foot of the Trade District wooden stairs, "it worked a moment
        // ago"): the bevelled step edges there are visible, colliding faces at 56 degrees, half a
        // yard high. Small steep faces like that count as steps too, not only collision-only kerbs.
        const bool IsStep=((Group.TriangleMaterials[Face].MaterialId==0xff && Rise<=1.0) || Rise<=0.6) && // r269: taller kerbs (1 yd, under the 95 cm step)
            Normal.Z>FMath::Cos(FMath::DegreesToRadians(80.f)) && Normal.Z<FMath::Cos(FMath::DegreesToRadians(45.f));
        auto& TargetIndices=bTread?TreadIndices:(IsStep?StepIndices:Indices);
        for (int32 Corner=0;Corner<3;++Corner)
        {
            const int32 Source=Group.Indices[Face*3+Corner];
            if (!Group.Vertices.IsValidIndex(Source)) return false;
            if (Remap[Source]==INDEX_NONE)
            {
                const FVector& SourcePosition=Group.Vertices[Source];
                Remap[Source]=Vertices.Add(FVector(SourcePosition.Y,SourcePosition.X,SourcePosition.Z)*FWowCoordinate::SCALE);
            }
            TargetIndices.Add(Remap[Source]);
        }
    }
    auto AttachFaces=[&](const TArray<int32>& Faces, bool IsStep)
    {
    if (Faces.IsEmpty()) return;
    auto* Collision=NewObject<UProceduralMeshComponent>(Owner,
        *FString::Printf(TEXT("WmoAuthoredCollision_%u_%s"),GroupIndex,IsStep?TEXT("Curb"):TEXT("Solid")));
    Owner->AddInstanceComponent(Collision);
    Collision->SetupAttachment(Parent);
    Collision->bUseComplexAsSimpleCollision=true;
    Collision->bUseAsyncCooking=false; // Barrier must exist before the group is playable.
    Collision->SetVisibility(false);
    Collision->SetHiddenInGame(true);
    Collision->SetCastShadow(false);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Collision->SetCollisionObjectType(ECC_WorldStatic);
    Collision->SetCollisionResponseToAllChannels(ECR_Block);
    Collision->CanCharacterStepUpOn=ECB_Yes;
    if (IsStep) Collision->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Increase,80.f));
    // r493 (Stefan, Valley of Trials: "unable to get up here", a ramp up to Ken'jai): the ramp's floor
    // is 48 degrees steep and our characters only walk 45 (kept for terrain, so hillsides stay
    // blocking). Building floors and ramps are made to be walked: up to 52 degrees on WMO geometry,
    // as in WoW. Walls (near vertical) stay walls.
    else Collision->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Increase,52.f));
    // Original authored barriers can be single-sided geometrically.
    Collision->GetBodySetup()->bDoubleSidedGeometry=true;
    Collision->CreateMeshSection(0,Vertices,Faces,TArray<FVector>(),TArray<FVector2D>(),
        TArray<FColor>(),TArray<FProcMeshTangent>(),true);
    Collision->RegisterComponent();
    };
    AttachFaces(Indices,false);
    AttachFaces(StepIndices,true);
    if (!TreadIndices.IsEmpty())
    {
        // Query only, ignored by every channel: movement, the camera and clicks never see it. The
        // foot IK finds it by object type (ECC_Destructible is otherwise unused in this project).
        auto* Treads=NewObject<UProceduralMeshComponent>(Owner,*FString::Printf(TEXT("WmoFootTreads_%u"),GroupIndex));
        Owner->AddInstanceComponent(Treads);
        Treads->SetupAttachment(Parent);
        Treads->bUseComplexAsSimpleCollision=true;
        Treads->bUseAsyncCooking=true;
        Treads->SetVisibility(false);
        Treads->SetHiddenInGame(true);
        Treads->SetCastShadow(false);
        Treads->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Treads->SetCollisionObjectType(ECC_Destructible);
        Treads->SetCollisionResponseToAllChannels(ECR_Ignore);
        Treads->CanCharacterStepUpOn=ECB_No;
        Treads->ComponentTags.Add(TEXT("WowFootTread"));
        Treads->GetBodySetup()->bDoubleSidedGeometry=true;
        Treads->CreateMeshSection(0,Vertices,TreadIndices,TArray<FVector>(),TArray<FVector2D>(),
            TArray<FColor>(),TArray<FProcMeshTangent>(),true);
        Treads->RegisterComponent();
    }
    return true;
}
