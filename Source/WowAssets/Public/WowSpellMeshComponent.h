#pragma once
#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Formats/M2Types.h"
#include "WowSpellMeshComponent.generated.h"

/** Small spell meshes use CPU deformation, isolated from world doodad skinning. */
UCLASS()
class WOWASSETS_API UWowSpellMeshComponent : public UProceduralMeshComponent
{
    GENERATED_BODY()
public:
    UWowSpellMeshComponent(const FObjectInitializer& ObjectInitializer);
    bool Initialize(TSharedPtr<const FM2Data> Data, class UStaticMesh* Materials, bool bLoop);
    /** Offset into the loop (ambient doodads), call before Initialize. */
    void SetStartTime(float Seconds) { AgeSeconds = FMath::Max(0.0f, Seconds); }
    /** Colour multiplied into the glowing (additive) passes, e.g. blue/violet for lightning. Call before Initialize. */
    FLinearColor GlowTint = FLinearColor::White;
    /** Persistent lightning states (Lightning Shield's orb): a small flickering point light that
     *  follows the orb so it lights the floor and walls around it. Call before Initialize. */
    bool bGlowLight = false;
    /** r409: brightness of the additive (blend 3/4) passes. The HDR scene needs more than 1 for an
     *  authored "light" (the level-up pillar was a faint yellow haze). Call before Initialize. */
    float GlowStrength = 1.f;
    /** r409: a steady glow light in this colour instead of the lightning crackle (A > 0 enables it;
     *  bGlowLight must be set too). Fades with the passes' opacity. Call before Initialize. */
    FLinearColor GlowLightColor = FLinearColor::Transparent;
    float GlowLightIntensity = 2200.f;
    float GlowLightRadius = 450.f;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick) override;
    /** CameraCS (component space) turns billboard bones (M2 flags 0x8-0x40) to face the camera. */
    static void EvaluateVertices(const FM2Data& Data, float Age, bool bLoop, TArray<FVector>& Positions, TArray<FVector>& Normals,
        const FVector* CameraCS = nullptr, int32 ClipIndex = INDEX_NONE);
    /** r374: game objects (chest lids, doors): play the sequence with this AnimationData id from
     *  the start; a one-shot then holds its last frame, or switches to ThenAnimId when it ends.
     *  Returns false when the model has no such sequence. */
    bool PlaySequenceById(int32 AnimId, bool bLoop, int32 ThenAnimId = INDEX_NONE);
    static int32 FindSequenceById(const FM2Data& Data, int32 AnimId);
    /** r414: no sequence at all - the model's rest pose (a chest whose Stand has no keys). */
    void HoldBindPose();
    static constexpr int32 BindPoseClip = -2;
private:
    TSharedPtr<const FM2Data> Model;
    struct FSection { int32 Pass; TArray<int32> Vertices; };
    TArray<FSection> Sections;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> AnimatedMaterials;
    TArray<FVector> AnimatedPositions, AnimatedNormals;
    float AgeSeconds = 0;
    bool bLooping = false;
    int32 ForcedClip = INDEX_NONE;   // r374: sequence chosen by PlaySequenceById
    int32 FollowAnimId = INDEX_NONE;
    UPROPERTY() TObjectPtr<class UPointLightComponent> GlowLight;
    float LightFlicker = 1.f;
    int32 LightStep = -1;
    void UpdatePose();
};
