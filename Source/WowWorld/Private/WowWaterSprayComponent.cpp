#include "WowWaterSprayComponent.h"
#include "WowWeatherComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Components/InstancedStaticMeshComponent.h"

UWowWaterSprayComponent::UWowWaterSprayComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickInterval=1.0f/30.0f;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetCastShadow(false);
    bAffectDistanceFieldLighting=false;
    SetGenerateOverlapEvents(false);
}

// r711 (Stefan, the Northshire fall after r688/r689: "Less mist, that's NOT what I asked for ... the right is STILL
// missing mist", and the pool below: "also missing here, you certainly didn't move it where I told you"): the spray
// was one round puff where the lowest 4 % of the sheet's vertices lay - one corner of a slanted lower edge, the left
// end - and r688 had halved its density. The mist now runs along the whole lower edge of the sheet (the doodad
// manager measures its direction and length), with more puffs on a wide fall, as dense as before r688.
void UWowWaterSprayComponent::InitializeFall(const FVector& Impact, float Width, float Height, const FVector& Along, float Length)
{
    SetUsingAbsoluteRotation(true);
    SetWorldRotation(FRotator::ZeroRotator); // drops and billboards are laid out in world axes
    SetWorldLocation(Impact);
    FallHeight=Height;
    const FVector Along2D=FVector(Along.X,Along.Y,0).GetSafeNormal();
    if(!Along2D.IsNearlyZero() && Length>0)
    {
        AlongAxis=Along2D;
        HalfLength=FMath::Min(Length*.5f,3000.0f);
        Radius=FMath::Clamp(Length*.18f,120.0f,450.0f); // across the edge: in front of and behind the sheet
    }
    else
        Radius=FMath::Clamp(Width*.32f,60.0f,650.0f);
    Rise=FMath::Clamp(Height*.30f,120.0f,1200.0f);
    // Reuse the cooked native soft-particle shader, never a WoW effect card.
    SetMaterial(0,UWowWeatherComponent::PrepareMaterial(this));
    const int32 MistCount=HalfLength>0?FMath::Clamp(FMath::RoundToInt(HalfLength*2/35.0f),64,180):64;
    Drops.SetNum(MistCount+56);
    for(int32 I=0;I<Drops.Num();++I)
    {
        auto& Drop=Drops[I]; Drop.Mist=I<MistCount; ResetDrop(Drop,true);
        const int32 V=Vertices.Num(); Vertices.AddZeroed(4);
        Normals.Append({FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector});
        UVs.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
        Colors.Append({FLinearColor::Transparent,FLinearColor::Transparent,FLinearColor::Transparent,FLinearColor::Transparent});
        Indices.Append({V,V+1,V+2,V,V+2,V+3});
    }
    UE_LOG(LogTemp,Log,TEXT("[WaterSpray] native width=%.1f edge=%.0f rise=%.1f particles=%d"),Width,HalfLength*2,Rise,Drops.Num());
}

void UWowWaterSprayComponent::ResetDrop(FDrop& Drop, bool WarmStart)
{
    if(HalfLength>0)
    {
        const FVector Across(-AlongAxis.Y,AlongAxis.X,0);
        Drop.Position=AlongAxis*Random.FRandRange(-HalfLength,HalfLength)+Across*Random.FRandRange(-Radius,Radius)*(Drop.Mist?.6f:.3f);
    }
    else
    {
        const float Angle=Random.FRandRange(0,2*PI);
        Drop.Position=FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Random.FRandRange(0,Radius);
    }
    Drop.Life=Drop.Mist?Random.FRandRange(2.0f,4.0f):Random.FRandRange(.6f,1.25f);
    Drop.Velocity=FVector(Random.FRandRange(-1,1),Random.FRandRange(-1,1),0)*(Drop.Mist?Radius*.12f:Radius*.6f);
    Drop.Velocity.Z=Drop.Mist?Rise/Drop.Life:Random.FRandRange(160,360);
    // r741: at most 4.2 m half-size - on the tall Northshire fall (Rise 12 m) a puff was up to 14 m across, a big round
    // ball; smaller puffs that overlap read as one veil, like the upper fall's mist.
    Drop.Size=Drop.Mist?FMath::Min(Random.FRandRange(Rise*.30f,Rise*.60f),420.0f):Random.FRandRange(2.5f,6.0f);
    // r741 (Stefan 23.10: the lower mist was "round bubbles", not like the upper mist): a puff started 5-40 cm above the
    // ground with a half-size of up to 0.6 x Rise, so the ground or rock cut off its lower half - a row of grey domes with
    // flat bottoms. Puffs now start half their size up, so at their brightest they float clear of the surface.
    Drop.Position.Z=Drop.Mist?Drop.Size*.5f+Random.FRandRange(0,30):Random.FRandRange(5,40);
    // r743 (Stefan 23.10, the Northshire outflow after r742: "Mist at right location, but doesn't touch the water"): a low
    // sheet's mist (no fall path - only upright falls get one) lay on the water but rose 1.2 m by its brightest, and its puffs
    // started half their size up. Its puffs are now flat (half as tall as wide, see the tick), start just above the surface
    // and rise a quarter as fast, so the mist lies on the water instead of floating over it.
    if(Drop.Mist && FallPath.Num()<2) { Drop.Position.Z=Drop.Size*.4f+Random.FRandRange(0,10); Drop.Velocity.Z*=.25f; }
    Drop.Age=WarmStart?Random.FRandRange(0,Drop.Life):0;
    Drop.Position+=Drop.Velocity*Drop.Age;
    if(!Drop.Mist) Drop.Position.Z-=250.0f*Drop.Age*Drop.Age;
}

void UWowWaterSprayComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime,TickType,Function);
    auto* PC=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr;
    if(!PC || !PC->PlayerCameraManager || Drops.IsEmpty()) return;
    const FVector Eye=PC->PlayerCameraManager->GetCameraLocation();
    const float Distance=FVector::Distance(Eye,GetComponentLocation());
    if(Distance>18000) { SetVisibility(false); return; }
    SetVisibility(true);
    // Source sheets extend beneath the basin. Find their visible contact with
    // terrain/rocks after the tile's collision has finished registering.
    if(!bImpactResolved && (ResolveDelay-=DeltaTime)<=0 && ResolveAttempts<30)
    {
        ResolveDelay=1; ++ResolveAttempts;
        const FVector Base=GetComponentLocation();
        // r677 (Stefan, the Northshire waterfall: mist on top of the boulders in front of the fall, and below them,
        // where the water comes out into the pond, "no water effects"): the trace down from half the fall's height
        // stopped on the first thing it met - the top of a boulder 24 m above the pool (log: 50.5 -> 75.1 m). Only
        // the ground, a building floor or the water surface is the basin now; rocks and other doodads are passed.
        FCollisionQueryParams Query(SCENE_QUERY_STAT(WaterfallImpact),true);
        FCollisionObjectQueryParams Objects;
        Objects.AddObjectTypesToQuery(ECC_WorldStatic);
        Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        // r741 (Stefan 23.10, the Northshire fall again: "This mist is still in the wrong place", it belongs "at the gap"
        // between the two boulders where the water hits): the rules below only look straight down from the sheet's lower
        // edge for ground, floors or water. This fall's sheet goes on behind the boulders and ends deep under the pond, so
        // the mist sat on the pond below and in front of the gap. Now the water is followed down the sheet's own centre
        // line (FallPath, from the doodad manager): the first rock, ground, floor or water surface it meets is where the
        // water visibly lands. Rocks count here - the earlier "mist on the boulder tops" came from a straight-down trace in
        // front of the fall, which this line never takes. Falls without a path, or with nothing on it after 10 s (their
        // collision may still be loading), use the straight-down rule below.
        if(FallPath.Num()>=2)
        {
            for(int32 S=0; S+1<FallPath.Num() && !bImpactResolved; ++S)
            {
                TArray<FHitResult> PathHits;
                GetWorld()->LineTraceMultiByObjectType(PathHits,FallPath[S],FallPath[S+1],Objects,Query);
                const FHitResult* First=nullptr;
                for(const FHitResult& Hit:PathHits)
                {
                    if(Hit.bStartPenetrating || !Hit.GetComponent()) continue; // the line starts inside a rock: look further down
                    // A surface the line leaves (its back face, the line began inside the rock) is not where the water lands.
                    if(FVector::DotProduct(Hit.ImpactNormal,(FallPath[S+1]-FallPath[S]).GetSafeNormal())>.2) continue;
                    if(!First || Hit.Distance<First->Distance) First=&Hit;
                }
                if(First)
                {
                    const UPrimitiveComponent* Part=First->GetComponent();
                    const TCHAR* Kind=Part->ComponentHasTag(TEXT("WowWaterVolume"))?TEXT("water")
                        :Part->GetName().StartsWith(TEXT("TerrainMesh"))?TEXT("ground")
                        :Part->GetName().StartsWith(TEXT("WmoGroup_"))?TEXT("floor"):TEXT("rock");
                    SetWorldLocation(First->ImpactPoint+FVector(0,0,20));
                    bImpactResolved=true;
                    const float Above=FallHeight>1?FMath::Clamp(float(First->ImpactPoint.Z-FallPath.Last().Z)/FallHeight,0.f,1.f):0.f;
                    UE_LOG(LogTemp,Log,TEXT("[WaterSpray] the falling water meets %s/%s (%s) at %.0f%% of the fall's height (try %d)"),
                        *GetNameSafe(First->GetActor()),*GetNameSafe(Part),Kind,Above*100.f,ResolveAttempts);
                }
            }
        }
        if(bImpactResolved || (FallPath.Num()>=2 && ResolveAttempts<10)) { /* resolved on the path, or wait for collision */ }
        else
        {
        TArray<FHitResult> Hits;
        GetWorld()->LineTraceMultiByObjectType(Hits,Base+FVector(0,0,FallHeight*.55f),Base-FVector(0,0,200),Objects,Query);
        // r719 (Stefan, the Northshire falls after r711: "the top mist is GOOD now. The lower mist is still missing"):
        // the first ground met on the way down is not always where this sheet lands - a ledge or the rim above a
        // lower pool caught the trace, and a sheet whose ground had not got its collision yet (it is enabled after
        // the tile builds) gave up after 8 s. Of the ground, floors and water surfaces below, the one nearest the
        // sheet's own lower edge is the basin now (a water surface wins within 3 m), and it keeps trying for 30 s.
        const FHitResult* Best=nullptr; bool bBestWater=false; double BestGap=TNumericLimits<double>::Max();
        for(const FHitResult& Hit:Hits)
        {
            const UPrimitiveComponent* Part=Hit.GetComponent();
            const AActor* Owner=Hit.GetActor();
            if(!Part || !Owner) continue;
            const bool bWater=Part->ComponentHasTag(TEXT("WowWaterVolume"));
            // r689 (Stefan: still no mist where the big Northshire fall lands; log: "resolved basin contact ... Z=8633 on
            // TerrainTile_32_48/DoodadAuthoredCollision_10122_4"): a rock's authored collision belongs to the tile actor
            // too. Only the ground mesh itself counts, so the trace goes on down to the pool's water surface.
            const bool bGround=Part->GetName().StartsWith(TEXT("TerrainMesh"));
            const bool bFloor=Part->GetName().StartsWith(TEXT("WmoGroup_"));
            if(!bWater && !bGround && !bFloor) continue;
            const double Gap=FMath::Abs(Hit.ImpactPoint.Z-Base.Z)-(bWater?300.0:0.0);
            if(Gap<BestGap) { BestGap=Gap; Best=&Hit; bBestWater=bWater; }
        }
        if(Best)
        {
            SetWorldLocation(Best->ImpactPoint+FVector(0,0,FallPath.Num()<2?5:20)); // r743: low sheets' mist sits on the surface
            bImpactResolved=true;
            UE_LOG(LogTemp,Log,TEXT("[WaterSpray] resolved basin contact on %s/%s (%s, %.0f cm from the sheet's lower edge, try %d)"),
                *GetNameSafe(Best->GetActor()),*GetNameSafe(Best->GetComponent()),bBestWater?TEXT("water"):TEXT("ground"),
                FMath::Abs(Best->ImpactPoint.Z-Base.Z),ResolveAttempts);
        }
        else if(ResolveAttempts>=30)
            UE_LOG(LogTemp,Log,TEXT("[WaterSpray] no basin found below a %.0f m fall; the mist stays at its lower edge"),FallHeight/100.f);
        } // r741: straight-down rule
    }
    const FRotator Camera=PC->PlayerCameraManager->GetCameraRotation();
    const FVector Right=Camera.RotateVector(FVector::RightVector), Up=Camera.RotateVector(FVector::UpVector);
    const float Dt=FMath::Min(DeltaTime,.1f);
    const float Fade=1-FMath::Clamp((Distance-14000)/4000,0.0f,1.0f);
    for(int32 I=0;I<Drops.Num();++I)
    {
        auto& Drop=Drops[I]; Drop.Age+=Dt;
        if(Drop.Age>=Drop.Life || Drop.Position.Z<0) ResetDrop(Drop,false);
        if(!Drop.Mist) Drop.Velocity.Z-=500*Dt;
        Drop.Position+=Drop.Velocity*Dt;
        const float Phase=Drop.Age/Drop.Life;
        const float Size=Drop.Size*(Drop.Mist?1+Phase:1);
        const FVector R=Right*Size,U=Up*Size*(Drop.Mist?(FallPath.Num()<2?.45f:1.f):2.5f); // r743: flat mist on low sheets
        const int32 V=I*4;
        Vertices[V]=Drop.Position-R-U; Vertices[V+1]=Drop.Position+R-U;
        Vertices[V+2]=Drop.Position+R+U; Vertices[V+3]=Drop.Position-R+U;
        // r688 (Stefan: the mist at the foot of the fall showed as grey cotton balls): bigger, half as dense puffs that
        // swell in slowly, so they melt into one veil instead of separate balls.
        const float Alpha=(Drop.Mist?FMath::Pow(FMath::Sin(Phase*PI),2.f)*.13f:FMath::Sin(Phase*PI)*.60f)*Fade;
        const FLinearColor Tint(.62f,.69f,.71f,Alpha);
        for(int32 J=0;J<4;++J) Colors[V+J]=Tint;
    }
    if(!bBuilt) { CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UVs,Colors,{},false); bBuilt=true; }
    else UpdateMeshSection_LinearColor(0,Vertices,Normals,UVs,Colors,{});
}
