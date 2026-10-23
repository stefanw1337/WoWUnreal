#include "WowWaterSedimentComponent.h"
#include "WowWeatherComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"

// r708 (Stefan, wading back and forth through a shallow stream at night: "walking in shallow water like this
// doesn't make it darker (and lighter again over time, as the water stills)... you can still see the front being
// as clear"): the old puffs were eight small camera-facing wisps that were gone after 2.4 s. Each stride in
// shallow water now stirs up three silt clouds round the feet: flat, soft and dark (they darken the water, they do
// not glow at night), lying low over the bottom, spreading to ~3 m wide and drifting a little, and settling slowly -
// about 20 s until the water is clear again. Walking the same way twice makes the trail darker.
namespace
{
    constexpr int32 PuffCount = 48;
    constexpr int32 PuffsPerStep = 3;
    constexpr float PuffLife = 20.0f;

    float Hash01(uint32 X)
    {
        X ^= X >> 16; X *= 0x7feb352dU; X ^= X >> 15; X *= 0x846ca68bU; X ^= X >> 16;
        return float(X & 0xffffffU) / float(0x1000000);
    }
}

UWowWaterSedimentComponent::UWowWaterSedimentComponent(const FObjectInitializer& Initializer) : Super(Initializer)
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickInterval=1.0f/30.0f;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false);
    SetCastShadow(false);
    bAffectDistanceFieldLighting=false;
}

void UWowWaterSedimentComponent::Disturb(const FVector& Feet, float SurfaceZ)
{
    if (!FMath::IsFinite(SurfaceZ) || SurfaceZ-Feet.Z<5 || SurfaceZ-Feet.Z>120) return;
    if(Puffs.IsEmpty())
    {
        SetWorldLocation(Feet); // Deliberately not attached to the moving pawn.
        SetMaterial(0,UWowWeatherComponent::PrepareMaterial(this));
        Puffs.SetNum(PuffCount);
        for(int32 I=0; I<PuffCount; ++I)
        {
            Puffs[I].Age=PuffLife;
            const int32 V=Vertices.Num(); Vertices.AddZeroed(4);
            Normals.Append({FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector});
            UVs.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
            Colors.Append({FLinearColor::Transparent,FLinearColor::Transparent,FLinearColor::Transparent,FLinearColor::Transparent});
            Indices.Append({V,V+1,V+2,V,V+2,V+3});
        }
    }
    const FVector Origin=GetComponentLocation();
    for(int32 K=0; K<PuffsPerStep; ++K)
    {
        auto& Puff=Puffs[Next]; Next=(Next+1)%Puffs.Num();
        const uint32 Seed=uint32(Next*7919)^uint32(FMath::FloorToInt(Feet.X))^(uint32(FMath::FloorToInt(Feet.Y))<<11);
        const float Angle=Hash01(Seed)*2.f*PI, Radius=Hash01(Seed+17)*45.f;
        Puff.Position=Feet+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0)-Origin;
        Puff.Age=-0.12f*K; // the clouds of one step bloom one after another
        Puff.Surface=SurfaceZ-Origin.Z;
    }
    SetComponentTickEnabled(true); SetVisibility(true);
}

void UWowWaterSedimentComponent::TickComponent(float DeltaTime,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime,Type,Function);
    bool Active=false;
    for(int32 I=0;I<Puffs.Num();++I)
    {
        auto& Puff=Puffs[I]; Puff.Age+=FMath::Min(DeltaTime,.1f);
        const float Age=FMath::Max(Puff.Age,0.f);
        const float Phase=FMath::Clamp(Age/PuffLife,0.0f,1.0f);
        const bool bLive=Puff.Age<PuffLife;
        Active|=bLive;
        const int32 V=I*4;
        if(!bLive)
        {
            for(int32 J=0;J<4;++J) Colors[V+J]=FLinearColor::Transparent;
            continue;
        }
        // Per-cloud variety from its position: drift direction, turn and size.
        const uint32 Seed=uint32(I*104729)^uint32(FMath::FloorToInt(Puff.Position.X*3.0))^(uint32(FMath::FloorToInt(Puff.Position.Y*3.0))<<9);
        const float DriftAngle=Hash01(Seed)*2.f*PI, Turn=Hash01(Seed+5)*2.f*PI, Scale=FMath::Lerp(.75f,1.25f,Hash01(Seed+9));
        const float Drift=70.f*(1.f-FMath::Exp(-Age/6.f));
        const float Depth=FMath::Max(Puff.Surface-Puff.Position.Z,10.f);
        const float Rise=FMath::Min(Depth*.35f,22.f)*(1.f-FMath::Exp(-Age/1.5f));
        const FVector P=Puff.Position+FVector(FMath::Cos(DriftAngle)*Drift,FMath::Sin(DriftAngle)*Drift,Rise);
        const float Size=(30.f+120.f*FMath::Sqrt(Phase))*Scale;
        const FVector X=FVector(FMath::Cos(Turn),FMath::Sin(Turn),0)*Size, Y=FVector(-FMath::Sin(Turn),FMath::Cos(Turn),0)*Size;
        Vertices[V]=P-X-Y; Vertices[V+1]=P+X-Y; Vertices[V+2]=P+X+Y; Vertices[V+3]=P-X+Y;
        // Quick to bloom, slow to settle.
        const float Bloom=FMath::SmoothStep(0.f,.6f,Age);
        const float Settle=FMath::Pow(1.f-Phase,1.6f);
        const FLinearColor Tint(.035f,.028f,.016f,.5f*Bloom*Settle);
        for(int32 J=0;J<4;++J) { Vertices[V+J].Z=FMath::Min(Vertices[V+J].Z,static_cast<double>(Puff.Surface-2)); Colors[V+J]=Tint; }
    }
    if(!bBuilt) { CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UVs,Colors,{},false); bBuilt=true; }
    else UpdateMeshSection_LinearColor(0,Vertices,Normals,UVs,Colors,{});
    if(!Active) { SetVisibility(false); SetComponentTickEnabled(false); }
}
