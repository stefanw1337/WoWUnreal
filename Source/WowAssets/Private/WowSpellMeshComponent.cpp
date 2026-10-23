#include "WowSpellMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "WowCookedMaterial.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"

namespace
{
template<typename T> T Sample(const TArray<uint32>& Times, const TArray<T>& Values, float Time, T Default)
{
    const int32 Count = FMath::Min(Times.Num(), Values.Num());
    if (!Count) return Default;
    if (Count == 1 || Time <= Times[0]) return Values[0];
    for (int32 I=1; I<Count; ++I)
        if (Time <= Times[I])
            return FMath::Lerp(Values[I-1], Values[I], (Time-Times[I-1])/FMath::Max(1.f,float(Times[I]-Times[I-1])));
    return Values[Count-1];
}
// Looping states play Hold when the model has it (r251), so their fades come from Hold too (r270).
float PassOpacity(const FM2RenderPass& Pass,float Age,bool bLoop,bool bHoldLoop=true);
FVector ToUE(const FVector& V) { return FVector(V.Y,V.X,V.Z)*100.; }
float TrackTime(float Age, uint32 Global, float Local) { return Global ? FMath::Fmod(Age*1000.f,float(Global)) : Local; }
float MaterialValue(const FM2MaterialTrack& Track,float Age,bool bLoop)
{
    float Time=Age*1000.f;
    // A looping state whose fade track only ramps up (Lightning Shield's Stand: 0 -> 1 over
    // 700 ms) is a birth fade-in: hold the end value after the first pass instead of pulsing.
    if (bLoop && Track.Duration && Time>=float(Track.Duration) && !Track.bStep && Track.Values.Num()>=2
        && Track.Values.Last()>Track.Values[0]+0.25f && Track.Times.Last()+50u>=Track.Duration)
    {
        bool bRamp=true;
        for (int32 I=1;I<Track.Values.Num();++I) if (Track.Values[I]+0.001f<Track.Values[I-1]) { bRamp=false; break; }
        if (bRamp) return Track.Values.Last();
    }
    if (bLoop && Track.Duration) Time=FMath::Fmod(Time,float(Track.Duration));
    if (Track.bStep && !Track.Times.IsEmpty())
    {
        int32 I=0;
        while(I+1<Track.Times.Num() && Track.Times[I+1]<=Time) ++I;
        return Track.Values.IsValidIndex(I) ? Track.Values[I] : 1.f;
    }
    return Sample(Track.Times,Track.Values,Time,1.f);
}
float PassOpacity(const FM2RenderPass& Pass,float Age,bool bLoop,bool bHoldLoop)
{
    // r449: only when the state really loops Hold (FM2Data::StateLoopClip); Lightning Shield loops Stand.
    if (bLoop && bHoldLoop && Pass.bHasHoldTracks)
        return MaterialValue(Pass.HoldTextureWeight,Age,bLoop)*MaterialValue(Pass.HoldColorAlpha,Age,bLoop);
    return MaterialValue(Pass.TextureWeight,Age,bLoop)*MaterialValue(Pass.ColorAlpha,Age,bLoop);
}
}

UWowSpellMeshComponent::UWowSpellMeshComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick=true;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetCastShadow(false);
}

int32 UWowSpellMeshComponent::FindSequenceById(const FM2Data& Data, int32 AnimId)
{
    // AnimationTracks skips sequences without keys, so match on the clip's own id (first variant).
    for (int32 I = 0; I < Data.AnimationTracks.Num(); ++I)
        if (int32(Data.AnimationTracks[I].AnimationId) == AnimId) return I;
    return INDEX_NONE;
}

bool UWowSpellMeshComponent::PlaySequenceById(int32 AnimId, bool bLoop, int32 ThenAnimId)
{
    if (!Model) return false;
    const int32 Clip = FindSequenceById(*Model, AnimId);
    if (Clip == INDEX_NONE) return false;
    ForcedClip = Clip;
    bLooping = bLoop;
    FollowAnimId = ThenAnimId;
    AgeSeconds = 0.f;
    UpdatePose();
    return true;
}

void UWowSpellMeshComponent::HoldBindPose()
{
    ForcedClip = BindPoseClip;
    bLooping = true;
    FollowAnimId = INDEX_NONE;
    AgeSeconds = 0.f;
    if (Model) UpdatePose();
}

void UWowSpellMeshComponent::EvaluateVertices(const FM2Data& Data, float Age, bool bLoop, TArray<FVector>& Positions, TArray<FVector>& Normals,
    const FVector* CameraCS, int32 ClipIndex)
{
    // Loop the Stand sequence: spell state models (Lightning Shield's orb) list a one-shot
    // fade-out first, which made the orb vanish every 1.5 turns and come back.
    const int32 Stand = Data.AnimationTracks.IsValidIndex(ClipIndex) ? ClipIndex
        : bLoop ? Data.StateLoopClip() : Data.StandSequence(); // looping states: Hold first (r251)
    const FM2AnimationData* Clip = ClipIndex == BindPoseClip ? nullptr
        : Data.AnimationTracks.IsValidIndex(Stand) ? &Data.AnimationTracks[Stand]
        : Data.AnimationTracks.IsEmpty() ? nullptr : &Data.AnimationTracks[0];
    const float Duration = Clip ? FMath::Max(1.f,float(Clip->Duration)) : 1.f;
    const float Time = bLoop ? FMath::Fmod(Age*1000.f,Duration) : FMath::Min(Age*1000.f,Duration);
    TArray<FTransform> Pose; Pose.SetNum(Data.Bones.Num());
    TArray<uint8> State; State.Init(0,Data.Bones.Num());
    TFunction<FTransform(int32)> Build = [&](int32 Index)
    {
        if (!Data.Bones.IsValidIndex(Index) || State[Index]==1) return FTransform::Identity;
        if (State[Index]==2) return Pose[Index];
        State[Index]=1;
        const auto& Bone=Data.Bones[Index];
        const bool bParent=Data.Bones.IsValidIndex(Bone.ParentBone);
        FVector Translation=Bone.PivotPoint-(bParent ? Data.Bones[Bone.ParentBone].PivotPoint : FVector::ZeroVector);
        FQuat Rotation=FQuat::Identity; FVector Scale=FVector::OneVector;
        if (Clip && Clip->BoneTracks.IsValidIndex(Index))
        {
            const auto& T=Clip->BoneTracks[Index];
            Translation+=Sample(T.TransTimestamps,T.TransValues,TrackTime(Age,T.TransGlobalDuration,Time),FVector::ZeroVector);
            Rotation=Sample(T.RotTimestamps,T.RotValues,TrackTime(Age,T.RotGlobalDuration,Time),FQuat::Identity).GetNormalized();
            Scale=Sample(T.ScaleTimestamps,T.ScaleValues,TrackTime(Age,T.ScaleGlobalDuration,Time),FVector::OneVector);
        }
        const FQuat LocalRotation=FQuat(Rotation.Y,Rotation.X,Rotation.Z,-Rotation.W).GetNormalized();
        FTransform Result(LocalRotation,ToUE(Translation),FVector(Scale.Y,Scale.X,Scale.Z));
        if (bParent) Result*=Build(Bone.ParentBone);
        // Billboarded bones (Hunter's Mark, glows, flares) always turn their face (WoW +X,
        // our +Y) to the camera; drawn as fixed planes they were paper-thin from the side.
        if (CameraCS && (Bone.Flags & 0x78))
        {
            FVector ToCamera=*CameraCS-Result.GetLocation();
            if (Bone.Flags & 0x40) ToCamera.Z=0; // cylindrical: stay upright
            if (!ToCamera.IsNearlyZero())
            {
                const FVector Dir=ToCamera.GetSafeNormal();
                const FMatrix Facing=FMath::Abs(Dir.Z)>0.98 ? FRotationMatrix::MakeFromYX(Dir,FVector::ForwardVector)
                    : FRotationMatrix::MakeFromYZ(Dir,FVector::UpVector);
                Result.SetRotation(Facing.ToQuat()*LocalRotation);
            }
        }
        State[Index]=2; Pose[Index]=Result; return Result;
    };
    for (int32 I=0;I<Data.Bones.Num();++I) Build(I);
    Positions.SetNum(Data.Vertices.Num()); Normals.SetNum(Data.Vertices.Num());
    for (int32 I=0;I<Data.Vertices.Num();++I)
    {
        const auto& V=Data.Vertices[I]; FVector P=FVector::ZeroVector,N=FVector::ZeroVector; float Weight=0;
        for(int32 J=0;J<4;++J)
        {
            const int32 Bone=V.BoneIndices[J]; const float W=V.BoneWeights[J];
            if (!W || !Data.Bones.IsValidIndex(Bone)) continue;
            P+=Pose[Bone].TransformPosition(ToUE(V.Position-Data.Bones[Bone].PivotPoint))*W;
            N+=Pose[Bone].TransformVectorNoScale(FVector(V.Normal.Y,V.Normal.X,V.Normal.Z))*W;
            Weight+=W;
        }
        Positions[I]=Weight>0 ? P/Weight : ToUE(V.Position);
        Normals[I]=Weight>0 ? N.GetSafeNormal() : FVector(V.Normal.Y,V.Normal.X,V.Normal.Z);
    }
}

bool UWowSpellMeshComponent::Initialize(TSharedPtr<const FM2Data> Data, UStaticMesh* Materials, bool bLoop)
{
    // Keep per-frame work bounded; larger models retain the existing presentation.
    if (!Data || !Materials || Data->Vertices.Num()>4096 || Data->Bones.Num()>128 || !Data->HasAnimationData()) return false;
    Model=Data; bLooping=bLoop;
    EvaluateVertices(*Model,0,bLooping,AnimatedPositions,AnimatedNormals);
    int32 MaterialIndex=0;
    for (int32 P=0;P<Model->RenderPasses.Num();++P)
    {
        const auto& Pass=Model->RenderPasses[P];
        if (!Model->Submeshes.IsValidIndex(Pass.SubmeshIndex)) continue;
        const auto& Sub=Model->Submeshes[Pass.SubmeshIndex];
        if (Sub.NumTriangles<3 || int32(Sub.StartTriangle)+Sub.NumTriangles>Model->Indices.Num()) continue;
        FSection Section; Section.Pass=P;
        TMap<int32,int32> Remap; TArray<int32> Indices; TArray<FVector> Positions,Normals; TArray<FVector2D> UV;
        for (int32 I=Sub.StartTriangle;I<int32(Sub.StartTriangle)+Sub.NumTriangles;++I)
        {
            int32 Source=Model->Indices[I];
            if (!Model->Vertices.IsValidIndex(Source)) { ClearAllMeshSections(); return false; }
            int32* Found=Remap.Find(Source);
            if (!Found)
            {
                int32 Local=Section.Vertices.Add(Source); Remap.Add(Source,Local); Indices.Add(Local);
                Positions.Add(AnimatedPositions[Source]); Normals.Add(AnimatedNormals[Source]); UV.Add(Model->Vertices[Source].TexCoord);
            }
            else Indices.Add(*Found);
        }
        const int32 Index=Sections.Add(MoveTemp(Section));
        CreateMeshSection_LinearColor(Index,Positions,Indices,Normals,UV,{}, {},false);
        UMaterialInterface* Source=Materials->GetMaterial(MaterialIndex++);
        UMaterialInstanceDynamic* Animated=nullptr;
        // Paladin glow passes use M2 blend 4 (additive with texture alpha).
        // This existing cooked shader also honors animated tint and opacity.
        // Additive glows (blend 3/4) use the animated billboard shader so their
        // colour tracks (and GlowTint) apply.
        // Alpha-blended spell meshes (blend 2: Blessing of Might's fist) also need it: the static
        // material ignored their fade tracks, so the fist stayed solid and lingered after the glow.
        if (Source && (Pass.BlendMode==4 || Pass.BlendMode==2 || (Pass.BlendMode==3 && GlowTint!=FLinearColor::White)))
        {
            if (UMaterial* Base=WowCookedMaterial::Load(TEXT("M_WowM2Billboard")))
            {
                UTexture* Texture=nullptr;
                if(Source->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Layer0Texture")),Texture) && Texture)
                {
                    Animated=UMaterialInstanceDynamic::Create(Base,this);
                    Animated->SetTextureParameterValue(TEXT("EffectTexture"),Texture);
                    Animated->SetScalarParameterValue(TEXT("EmissiveStrength"),Pass.BlendMode==2 ? 0.75f : GlowStrength);
                    Source=Animated;
                }
            }
        }
        AnimatedMaterials.Add(Animated);
        SetMaterial(Index,Source);
    }
    if (bGlowLight && GetOwner() && GetWorld() && GetWorld()->IsGameWorld())
    {
        GlowLight=NewObject<UPointLightComponent>(GetOwner());
        GlowLight->SetupAttachment(this);
        // Saturated blue/violet (the tint normalised, no white mixed in): the light itself
        // should colour the floor and walls, not just brighten them.
        const float Peak=FMath::Max3(GlowTint.R,GlowTint.G,GlowTint.B);
        FLinearColor LightColor=Peak>0.f ? GlowTint/Peak : FLinearColor(0.25f,0.4f,1.f);
        LightColor.R*=0.75f; LightColor.G*=0.75f; LightColor.A=1.f;
        if (GlowLightColor.A>0.f) { LightColor=GlowLightColor; LightColor.A=1.f; } // r409: steady coloured glow
        GlowLight->SetLightColor(LightColor,false);
        GlowLight->SetIntensity(GlowLightIntensity);
        GlowLight->SetAttenuationRadius(GlowLightRadius);
        GlowLight->SetSourceRadius(6.f);
        GlowLight->SetCastShadows(false);
        GetOwner()->AddInstanceComponent(GlowLight);
        GlowLight->RegisterComponent();
    }
    UpdatePose();
    return !Sections.IsEmpty();
}

void UWowSpellMeshComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime,TickType,Tick);
    AgeSeconds+=DeltaTime;
    // r374: a one-shot game object sequence (chest lid opening) hands over to its follow-up.
    if (Model && FollowAnimId!=INDEX_NONE && !bLooping && Model->AnimationTracks.IsValidIndex(ForcedClip)
        && AgeSeconds*1000.f>=float(Model->AnimationTracks[ForcedClip].Duration))
    {
        const int32 Next=FollowAnimId;
        FollowAnimId=INDEX_NONE;
        PlaySequenceById(Next,true);
    }
    if (Model && (AgeSeconds<.25f || WasRecentlyRendered(.5f))) UpdatePose();
}

void UWowSpellMeshComponent::UpdatePose()
{
    FVector CameraCS=FVector::ZeroVector;
    bool bHaveCamera=false;
    if (const UWorld* World=GetWorld())
        if (const APlayerController* PC=World->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
        { CameraCS=GetComponentTransform().InverseTransformPosition(PC->PlayerCameraManager->GetCameraLocation()); bHaveCamera=true; }
    EvaluateVertices(*Model,AgeSeconds,bLooping,AnimatedPositions,AnimatedNormals,bHaveCamera?&CameraCS:nullptr,ForcedClip);
    const int32 LoopClip=Model->StateLoopClip();
    const bool bHoldLoop=Model->AnimationTracks.IsValidIndex(LoopClip) && Model->AnimationTracks[LoopClip].AnimationId==158;
    for(int32 I=0;I<Sections.Num();++I)
    {
        const auto& Section=Sections[I];
        const auto& Pass=Model->RenderPasses[Section.Pass];
        if (AnimatedMaterials.IsValidIndex(I) && AnimatedMaterials[I])
        {
            AnimatedMaterials[I]->SetScalarParameterValue(TEXT("Opacity"),
                PassOpacity(Pass,AgeSeconds,bLooping,bHoldLoop));
            AnimatedMaterials[I]->SetVectorParameterValue(TEXT("Tint"),FLinearColor(
                MaterialValue(Pass.ColorRed,AgeSeconds,bLooping)*GlowTint.R,MaterialValue(Pass.ColorGreen,AgeSeconds,bLooping)*GlowTint.G,
                MaterialValue(Pass.ColorBlue,AgeSeconds,bLooping)*GlowTint.B,1.f));
        }
        TArray<FVector> Positions,Normals; TArray<FVector2D> UV;
        Positions.Reserve(Section.Vertices.Num()); Normals.Reserve(Section.Vertices.Num()); UV.Reserve(Section.Vertices.Num());
        for(int32 Source:Section.Vertices)
        {
            Positions.Add(AnimatedPositions[Source]); Normals.Add(AnimatedNormals[Source]);
            UV.Add(Model->Vertices[Source].TexCoord+Model->RenderPasses[Section.Pass].UVScrollSpeed*AgeSeconds);
        }
        UpdateMeshSection_LinearColor(I,Positions,Normals,UV,{},{});
    }
    if (GlowLight)
    {
        // Follow the orb (centre of the animated vertices) and crackle like the shield does.
        FVector Centre=FVector::ZeroVector;
        for (const FVector& P : AnimatedPositions) Centre+=P;
        if (!AnimatedPositions.IsEmpty()) GlowLight->SetRelativeLocation(Centre/AnimatedPositions.Num());
        float Opacity=0.f;
        for (const FM2RenderPass& Pass : Model->RenderPasses)
            Opacity=FMath::Max(Opacity,PassOpacity(Pass,AgeSeconds,bLooping,bHoldLoop));
        const int32 Step=FMath::FloorToInt(AgeSeconds*14.f);
        if (GlowLightColor.A>0.f) LightFlicker=1.f; // r409: steady (level-up pillar), no crackle
        else if (Step!=LightStep)
        {
            LightStep=Step;
            const float Noise=float((uint32(Step)*2654435761u)>>8 & 0xFFFF)/65535.f;
            LightFlicker=FMath::Lerp(LightFlicker,0.6f+0.8f*Noise,0.6f);
        }
        GlowLight->SetIntensity(GlowLightIntensity*LightFlicker*FMath::Clamp(Opacity,0.f,1.f));
    }
}
