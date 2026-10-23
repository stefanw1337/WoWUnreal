#include "WowGlueMaterialAnimator.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SkeletalMeshComponent.h"

static float SampleMaterialTrack(const FM2MaterialTrack& Track,double Milliseconds)
{
    if(Track.Values.IsEmpty())return 1;
    const double Time=Track.Duration?FMath::Fmod(Milliseconds,double(Track.Duration)):Milliseconds;
    if(Time<=Track.Times[0])return Track.Values[0];
    for(int32 I=1;I<Track.Times.Num();++I)
        if(Time<Track.Times[I])
        {
            if(Track.bStep)return Track.Values[I-1];
            const double Span=Track.Times[I]-Track.Times[I-1];
            return Span>0?FMath::Lerp(Track.Values[I-1],Track.Values[I],float((Time-Track.Times[I-1])/Span)):Track.Values[I];
        }
    return Track.Values.Last();
}
UWowGlueMaterialAnimator::UWowGlueMaterialAnimator()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostPhysics;
}
void UWowGlueMaterialAnimator::Add(USkeletalMeshComponent* Mesh,int32 Section,UMaterialInstanceDynamic* Material,const FM2RenderPass& Pass)
{
    FEntry& Entry=Entries.AddDefaulted_GetRef();
    Entry.Mesh=Mesh;Entry.Material=Material;Entry.Section=Section;
    Entry.Weight=Pass.TextureWeight;Entry.Alpha=Pass.ColorAlpha;
    Entry.Red=Pass.ColorRed;Entry.Green=Pass.ColorGreen;Entry.Blue=Pass.ColorBlue;
    Update(Entry);
}
void UWowGlueMaterialAnimator::Update(FEntry& Entry)
{
    if(!Entry.Material.IsValid() || !Entry.Mesh.IsValid())return;
    const float Opacity=SampleMaterialTrack(Entry.Weight,Elapsed)*SampleMaterialTrack(Entry.Alpha,Elapsed);
    Entry.Material->SetScalarParameterValue(TEXT("PassOpacity"),Opacity);
    Entry.Material->SetVectorParameterValue(TEXT("PassTint"),FLinearColor(
        SampleMaterialTrack(Entry.Red,Elapsed),SampleMaterialTrack(Entry.Green,Elapsed),SampleMaterialTrack(Entry.Blue,Elapsed),1));
    // Even opaque passes can be switched off by an M2 material animation.
    // This hides the dragon's inactive overlay passes instead of drawing white.
    const bool bVisible=Opacity>.001f;
    if(bVisible!=Entry.bVisible)
    {
        Entry.Mesh->ShowMaterialSection(Entry.Section,Entry.Section,bVisible,0);
        Entry.bVisible=bVisible;
    }
}
void UWowGlueMaterialAnimator::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    Elapsed+=DeltaTime*1000.0;
    for(FEntry& Entry:Entries)Update(Entry);
}
