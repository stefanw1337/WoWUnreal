#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimSequence.h"
#include "WowRuntimeAnimSequence.generated.h"

WOWASSETS_API bool WowUseRuntimeM2Import();

struct FWowRuntimeBoneSamples
{
    TArray<FVector3f> Positions;
    TArray<FQuat4f> Rotations;
    TArray<FVector3f> Scales;
};

/** Transient animation sampled from the user's archives. No editor model or compression required. */
UCLASS(Transient)
class WOWASSETS_API UWowRuntimeAnimSequence : public UAnimSequence
{
    GENERATED_BODY()
public:
    void InitializeRuntime(USkeleton* InSkeleton, int32 InFrames);
    void SetRuntimeTrack(int32 SkeletonBone, TArray<FVector3f>&& Positions,
        TArray<FQuat4f>&& Rotations, TArray<FVector3f>&& Scales);
    virtual float GetPlayLength() const override { return Frames / 30.0f; }
    virtual int32 GetNumberOfSampledKeys() const override { return Frames + 1; }
    virtual FFrameRate GetSamplingFrameRate() const override { return FFrameRate(30, 1); }
#if WITH_EDITOR
    virtual bool IsValidToPlay() const override { return GetSkeleton() && Frames > 0; }
#endif
    virtual void GetAnimationPose(FAnimationPoseData& OutPoseData, const FAnimExtractContext& Context) const override;
private:
    int32 Frames = 1;
    TArray<FWowRuntimeBoneSamples> Tracks;
};
