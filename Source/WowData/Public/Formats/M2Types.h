#pragma once
#include "CoreMinimal.h"

struct WOWDATA_API FM2Vertex
{
    FVector Position;
    FVector Normal;
    FVector2D TexCoord;
    FVector2D TexCoord2;
    uint8 BoneWeights[4] = {};
    uint8 BoneIndices[4] = {};
};

// Scalar material tracks use signed normalized 16-bit values in M2.
struct WOWDATA_API FM2MaterialTrack
{
    TArray<uint32> Times;
    TArray<float> Values;
    uint32 Duration = 0;
    bool bStep = false;
};

struct WOWDATA_API FM2RenderPass
{
    FM2MaterialTrack TextureWeight;
    FM2MaterialTrack ColorAlpha;
    FM2MaterialTrack ColorRed, ColorGreen, ColorBlue;
    /** The same fades in the Hold (158) sequence, for looping spell states that play Hold
     *  (r270). A sequence with no keys of its own evaluates to 0 in the client, so Water
     *  Shield's bubble and flash cards are invisible while it holds; only its orbs show. */
    FM2MaterialTrack HoldTextureWeight;
    FM2MaterialTrack HoldColorAlpha;
    bool bHasHoldTracks = false;
    uint16 SubmeshIndex = 0;
    uint16 TextureIndex = 0;
    uint16 BlendMode = 0;
    uint16 Flags = 0;
    FVector2D UVScrollSpeed = FVector2D::ZeroVector; // authored global linear UV translation, cycles/second
    /** r613: the texture unit's priority plane; the client draws a model's passes in this order. */
    int8 PriorityPlane = 0;
    /** r615: a second texture layer (texture count 2): its texture index (0xFFFF = none) and UV scroll.
     *  The client combines it with the first as mod2x (colour x2, alpha multiplied). */
    uint16 TextureIndex2 = 0xFFFF;
    FVector2D UVScrollSpeed2 = FVector2D::ZeroVector;
    /** r739 (Stefan 23.10, TormentWoW's Ulduar: the chains were blue ghosts - "Then get it supported?"): where each
     *  texture layer takes its coordinates, from the skin's texture-coordinate combo: 0 = UV0, 1 = UV1, -1 = the
     *  sphere environment map (reflection from the view-space normal - shiny metal chains, ice, glass). */
    int8 TexCoordSource = 0;
    int8 TexCoordSource2 = 0;
};

struct WOWDATA_API FM2Bone
{
	int32 KeyBoneId = -1;
	uint32 Flags = 0;
	int16 ParentBone = -1;
	FVector PivotPoint = FVector::ZeroVector;
};

struct WOWDATA_API FM2AnimSequence
{
	uint16 AnimationId = 0;
	uint16 SubAnimationId = 0;
	uint32 Length = 0; // duration in ms
	float MoveSpeed = 0.0f;
	uint32 Flags = 0;
	int16 NextAnimation = -1;
	uint16 AliasNext = 0;
	int16 Probability = 0x7FFF; // r482: how often the client picks this variation (of 0x7FFF)
	bool IsLooping() const { return AnimationId != 1 && (Flags & 0x80) == 0; }
};

// Animation keyframe for a single bone
struct WOWDATA_API FM2AnimKeyframe
{
    uint32 Time = 0; // ms
    FVector Translation = FVector::ZeroVector;
    FQuat Rotation = FQuat::Identity;
    FVector Scale = FVector::OneVector;
};

// Animation track for one bone across one animation sequence
struct WOWDATA_API FM2BoneTrack
{
    uint32 TransGlobalDuration = 0;
    uint32 RotGlobalDuration = 0;
    uint32 ScaleGlobalDuration = 0;
    TArray<uint32> TransTimestamps;
    TArray<FVector> TransValues;
    TArray<uint32> RotTimestamps;
    TArray<FQuat> RotValues;
    TArray<uint32> ScaleTimestamps;
    TArray<FVector> ScaleValues;

    bool HasTranslation() const { return TransTimestamps.Num() > 0; }
    bool HasRotation() const { return RotTimestamps.Num() > 0; }
    bool HasScale() const { return ScaleTimestamps.Num() > 0; }
    bool IsEmpty() const { return !HasTranslation() && !HasRotation() && !HasScale(); }
};

// All animation tracks for all bones for one animation sequence
struct WOWDATA_API FM2AnimationData
{
    uint16 AnimationId = 0;
    uint16 SubAnimationId = 0;
    uint32 Duration = 0; // ms
    bool bIsLooping = false;
    float MoveSpeed = 0.0f;
    TArray<FM2BoneTrack> BoneTracks; // one per bone
};

struct WOWDATA_API FM2Attachment
{
    uint32 Id = 0;           // Attachment point ID (1=right palm, 2=left palm, 5=right shoulder, 11=helmet, etc.)
    int32 Bone = -1;         // Bone index this attachment is parented to
    FVector Position = FVector::ZeroVector; // Position relative to the bone
};

struct WOWDATA_API FM2RibbonEmitter
{
    uint32 Id = 0;
    int32 Bone = -1;
    FVector Position = FVector::ZeroVector;
    uint16 Texture = 0;
    uint16 Material = 0;
    FLinearColor Color = FLinearColor::White;
    float HeightAbove = 0.15f;
    float HeightBelow = 0.15f;
    float EdgesPerSecond = 20.0f;
    float EdgeLifetime = 0.5f;
    float Gravity = 0.0f;
    uint16 TextureRows = 1;
    uint16 TextureColumns = 1;
    bool bVisible = true;
    /** Visibility keys of the played sequence (step track). WoW stops emitting while 0:
     *  Lightning Shield's impact strikes are on for 0-600 ms, then their bones snap back up. */
    TArray<uint32> VisibilityTimes;
    TArray<uint8> VisibilityValues;
    bool IsVisibleAt(float TimeMs) const
    {
        bool bOn = bVisible;
        for (int32 I = 0; I < VisibilityTimes.Num() && I < VisibilityValues.Num(); ++I)
        {
            if (float(VisibilityTimes[I]) > TimeMs) break;
            bOn = VisibilityValues[I] != 0;
        }
        return bOn;
    }
};

struct WOWDATA_API FM2ParticleEmitter
{
    bool bEnabledInStand = true;             // Authored idle emission gate (muzzle smoke is false).
    /** r370: Stand-sequence enabled keys (step track). Spell visuals open short emission windows:
     *  Shrink's ShadowWordBefuddle_Head emits its spiralling stars only at 233-867 ms of 2000. */
    TArray<uint32> EnabledTimes;
    TArray<uint8> EnabledValues;
    bool HasEnabledWindow() const { for (uint8 V : EnabledValues) if (V) return true; return false; }
    bool IsEnabledAt(float TimeMs) const
    {
        bool bOn = EnabledValues.Num() ? EnabledValues[0] != 0 : bEnabledInStand;
        for (int32 I = 0; I < EnabledTimes.Num() && I < EnabledValues.Num(); ++I)
        {
            if (float(EnabledTimes[I]) > TimeMs) break;
            bOn = EnabledValues[I] != 0;
        }
        return bOn;
    }
    uint32 Id = 0;                          // Particle emitter ID
    uint32 Flags = 0;                       // Emitter flags
    FVector Position = FVector::ZeroVector; // Position relative to bone
    int16 Bone = -1;                        // Bone index this emitter is attached to
    uint16 Texture = 0;                     // Texture index for particles
    FString BlendingType;                   // Legacy string form of BlendMode
    uint8 BlendMode = 2;                    // 0 opaque, 1 alpha-key, 2 blend, 3 add, 4 add-alpha, 5 mod, 6 mod2x
    uint8 EmitterType = 0;                  // 1=plane, 2=sphere, 3=spline
    uint8 ParticleType = 0;                 // 0=regular, 1=chunky, 2=both
    uint8 HeadorTail = 0;                   // 0=head, 1=tail, 2=both
    uint16 TextureRows = 1;                 // Particle texture atlas rows
    uint16 TextureColumns = 1;              // Particle texture atlas columns
    float TailLength = 0.0f;                // Length of particle tail
    float MiddleTime = 0.5f;                // Normalized time of the middle lifetime key
    uint32 ColorStart[3] = {255, 255, 255}; // RGB start color
    uint32 ColorMiddle[3] = {255, 255, 255}; // RGB middle color
    uint32 ColorEnd[3] = {255, 255, 255};   // RGB end color
    uint8 AlphaStart = 255;                 // Start alpha
    uint8 AlphaMiddle = 255;                // Middle alpha
    uint8 AlphaEnd = 255;                   // End alpha
    float ScaleStart = 1.0f;                // Start scale
    float ScaleMiddle = 1.0f;               // Middle scale
    float ScaleEnd = 1.0f;                  // End scale
    float ScaleVariation = 0.0f;            // Scale randomization
    uint16 HeadCellStart = 0;                // First SubUV frame over particle lifetime
    uint16 HeadCellMiddle = 0;               // Middle SubUV frame over particle lifetime
    uint16 HeadCellEnd = 0;                  // Last SubUV frame over particle lifetime
    float EmissionSpeed = 0.0f;              // Model units per second
    float SpeedVariation = 0.0f;             // Random +/- fraction of EmissionSpeed
    float VerticalRange = 0.0f;              // Radians around authored launch direction
    float HorizontalRange = 0.0f;            // Radians around authored launch direction
    float Lifespan = 1.0f;                   // Seconds
    float LifespanVariation = 0.0f;          // Random +/- seconds
    float HeadLifeStart = 1000.0f;          // Head lifetime start (ms)
    float HeadLifeEnd = 1000.0f;            // Head lifetime end (ms)
    float HeadLifeRepeat = 0.0f;            // Head lifetime repeat rate
    float HeadDecay = 0.0f;                 // Head decay rate
    float TailLifeStart = 1000.0f;          // Tail lifetime start (ms)
    float TailLifeEnd = 1000.0f;            // Tail lifetime end (ms)
    float TailLifeRepeat = 0.0f;            // Tail lifetime repeat rate
    float TailDecay = 0.0f;                 // Tail decay rate
    float EmissionRate = 10.0f;             // Particles per second
    float EmissionRateVariation = 0.0f;
    float EmissionAreaLength = 0.0f;        // Length of emission area
    float EmissionAreaWidth = 0.0f;         // Width of emission area
    float ZSource = 0.0f;
    float Gravity = -9.8f;                  // Gravity affecting particles

    // Helper functions
    bool IsFireEmitter() const;             // Check if this is a fire emitter
    bool IsSmokeEmitter() const;            // Check if this is a smoke emitter
    bool IsSparkleEmitter() const;          // Check if this is an ambient sparkle emitter
};

struct WOWDATA_API FM2Submesh
{
    uint16 Id = 0;           // Geoset ID (used for equipment geoset swaps)
    uint16 StartVertex = 0;
    uint16 NumVertices = 0;
    uint32 StartTriangle = 0; // index offset; HD skins carry bits 16+ in the submesh Level field
    uint16 NumTriangles = 0;
    FVector CenterMass = FVector::ZeroVector;
    float Radius = 0.0f;
};

struct WOWDATA_API FM2Data
{
    TArray<FM2Vertex> Vertices;
    TArray<uint16> Indices;
    TArray<FString> TexturePaths;
    TArray<uint32> TextureTypes;  // M2 texture type per index (0=filename, 1=skin, 6=hair, etc.)
    TArray<uint32> TextureFlags;  // M2 texture flags: 0x1 wrap U, 0x2 wrap V (unset = clamp, as the original client)
    TArray<FM2RenderPass> RenderPasses;
    FBox BoundingBox = FBox(ForceInit);
    float BoundingSphereRadius = 0.0f;
    // Authored M2 collision geometry (the header calls these "bounding"
    // triangles/vertices).  This is deliberately kept separate from the
    // render mesh: foliage cards and branches are visual geometry and must
    // not become a large invisible collision hull around a tree.
    TArray<FVector> CollisionVertices;
    TArray<uint16> CollisionIndices;
    FBox CollisionBox = FBox(ForceInit);
    float CollisionSphereRadius = 0.0f;
    TArray<FM2Bone> Bones;
    TArray<FM2AnimSequence> Animations;
    TArray<FM2AnimationData> AnimationTracks; // parsed keyframe data per animation
    TArray<FM2Attachment> Attachments;
    TArray<FM2ParticleEmitter> ParticleEmitters; // particle emitter data
    TArray<FM2RibbonEmitter> RibbonEmitters;     // animated trail/ribbon data
    TArray<FM2Submesh> Submeshes;          // geoset info from skin file
    TArray<int16> AttachmentLookup;        // attachment ID → index in Attachments
    uint32 NumBones = 0;
    bool bIsValid = false;

    bool HasBones() const { return Bones.Num() > 0; }
    bool HasAnimations() const { return Animations.Num() > 0; }
    bool HasAnimationData() const { return AnimationTracks.Num() > 0; }
    /** The sequence the client loops on an idle model: Stand (id 0), else the first one.
     *  Spell state models list a one-shot first (e.g. Lightning Shield's fade-out). */
    int32 StandSequence() const
    {
        for (int32 I = 0; I < Animations.Num(); ++I) if (Animations[I].AnimationId == 0) return I;
        return 0;
    }

    /** AnimationTracks clip a persistent spell state loops: Hold (158) when the model has one,
     *  else Stand (r251). Water Shield's Stand orbits three quarters of a turn and then holds
     *  still for 300 ms each second; its Hold sequence is the seamless full orbit. */
    int32 StateLoopClip() const
    {
        // r449 (Stefan: Lightning Shield's three orbs circle but "never become one"): Lightning
        // Shield's Hold carries no fades at all (every pass evaluates to 0 there), while its Stand
        // fades the single orb in. A Hold with no fade keys on any pass is not the state's loop;
        // Stand is. (Water Shield keeps Hold: its orbs have Hold keys.)
        bool bHoldHasFades = false, bStandHasFades = false;
        for (const FM2RenderPass& Pass : RenderPasses)
        {
            // r456: the parser stores "no keys in Hold" as a single 0 key (hidden), so only a Hold
            // value above zero counts as a real Hold fade (Water Shield's orbs).
            for (const float Value : Pass.HoldTextureWeight.Values) bHoldHasFades |= Value > 0.004f;
            bStandHasFades |= !Pass.TextureWeight.Values.IsEmpty() || !Pass.ColorAlpha.Values.IsEmpty();
        }
        const bool bSkipHold = !bHoldHasFades && bStandHasFades && !RenderPasses.IsEmpty();
        for (int32 I = 0; I < AnimationTracks.Num() && !bSkipHold; ++I) if (AnimationTracks[I].AnimationId == 158) return I;
        for (int32 I = 0; I < AnimationTracks.Num(); ++I) if (AnimationTracks[I].AnimationId == 0) return I;
        return StandSequence();
    }
};
