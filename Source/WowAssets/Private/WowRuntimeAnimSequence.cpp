#include "WowRuntimeAnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/Skeleton.h"
#include "BoneContainer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool WowUseRuntimeM2Import()
{
#if WITH_EDITOR
    return FParse::Param(FCommandLine::Get(), TEXT("runtimem2"));
#else
    return true;
#endif
}

void UWowRuntimeAnimSequence::InitializeRuntime(USkeleton* InSkeleton, int32 InFrames)
{
    SetSkeleton(InSkeleton);
    Frames = FMath::Max(1, InFrames);
    Tracks.SetNum(InSkeleton ? InSkeleton->GetReferenceSkeleton().GetNum() : 0);
}

void UWowRuntimeAnimSequence::SetRuntimeTrack(int32 Bone, TArray<FVector3f>&& Positions,
    TArray<FQuat4f>&& Rotations, TArray<FVector3f>&& Scales)
{
    if (!Tracks.IsValidIndex(Bone) || Positions.Num() != Frames + 1 ||
        Rotations.Num() != Positions.Num() || Scales.Num() != Positions.Num()) return;
    Tracks[Bone] = {MoveTemp(Positions), MoveTemp(Rotations), MoveTemp(Scales)};
}

void UWowRuntimeAnimSequence::GetAnimationPose(FAnimationPoseData& OutPoseData, const FAnimExtractContext& Context) const
{
    FCompactPose& Pose = OutPoseData.GetPose();
    Pose.ResetToRefPose();
    const float Sample = FMath::Clamp(float(Context.CurrentTime * 30.0), 0.f, float(Frames));
    const int32 First = FMath::FloorToInt(Sample);
    const int32 Second = FMath::Min(First + 1, Frames);
    const float Alpha = Sample - First;
    for (FCompactPoseBoneIndex Bone : Pose.ForEachBoneIndex())
    {
        const int32 SkeletonBone = Pose.GetBoneContainer().GetSkeletonIndex(Bone);
        if (!Tracks.IsValidIndex(SkeletonBone)) continue;
        const auto& Track = Tracks[SkeletonBone];
        if (!Track.Positions.IsValidIndex(Second)) continue;
        Pose[Bone] = FTransform(
            FQuat(FQuat4f::Slerp(Track.Rotations[First], Track.Rotations[Second], Alpha).GetNormalized()),
            FVector(FMath::Lerp(Track.Positions[First], Track.Positions[Second], Alpha)),
            FVector(FMath::Lerp(Track.Scales[First], Track.Scales[Second], Alpha)));
    }
}
