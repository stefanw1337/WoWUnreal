#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WowFlybyCinematic.generated.h"

class UCameraComponent;
class UAudioComponent;
class FMpqManager;
class AWowGameplayController;

/**
 * r377 (Stefan: the race intro after character creation was missing): the server sends
 * SMSG_TRIGGER_CINEMATIC on a character's first login. The original client then flies a camera
 * along the race's path (Cameras\FlyBy<Race>.m2, placed in the world by CinematicCamera.dbc)
 * while the narration plays, and answers CMSG_COMPLETE_CINEMATIC when it ends or is skipped (Esc).
 * The flight shows our own live world, so the player's HD options apply to everything in it.
 */
UCLASS()
class AWowFlybyCinematic : public AActor
{
    GENERATED_BODY()
public:
    AWowFlybyCinematic();
    /** Starts the sequence; nullptr when its data is missing (the completion is then sent at once). */
    static AWowFlybyCinematic* Start(AWowGameplayController* Controller, FMpqManager* Mpq, uint32 SequenceId);
    /** ChrClasses/ChrRaces CinematicSequenceID (death knights use the class one). */
    static uint32 SequenceForCharacter(FMpqManager* Mpq, uint8 Race, uint8 Class);
    /** r380: SMSG_TRIGGER_CINEMATIC (a character's first login). Plays the server's class intro
     *  movie once per character when its data has one (FWowTrailerPlayer rules), replacing the race
     *  flyby or before it; otherwise the race flyby. Input and UI come back afterwards and the
     *  server gets CMSG_COMPLETE_CINEMATIC either way. ActiveFlyby is the controller's flyby slot. */
    static void HandleTrigger(AWowGameplayController* Controller, FMpqManager* Mpq, uint8 Race, uint8 Class, uint8 Gender,
        uint64 CharacterGuid, const FString& RealmName, TWeakObjectPtr<AWowFlybyCinematic>* ActiveFlyby);
    void Skip();
    bool IsPlaying() const { return bPlaying; }
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    /** r603: a camera key with the authored tangents (M2 spline keys: value, in, out) and the track's
     *  interpolation (2 Bezier: the tangents are control points; 3 Hermite: they are tangents). */
    struct FKey { float TimeMs = 0.f; FVector Value = FVector::ZeroVector; FVector InTan = FVector::ZeroVector, OutTan = FVector::ZeroVector; uint8 Interp = 0; };
    bool Load(FMpqManager* Mpq, uint32 SequenceId);
    static bool ParseCamera(const TArray<uint8>& M2, FVector& OutPositionBase, TArray<FKey>& OutPositions,
        FVector& OutTargetBase, TArray<FKey>& OutTargets, float& OutFov, float& OutDurationMs);
    static FVector Sample(const TArray<FKey>& Keys, float TimeMs);
    FVector ToWorld(const FVector& Base, const FVector& Offset) const;
    void Finish(bool bSkipped);
    /** r467: show the flight (view, narration) once its first stretch has streamed in. */
    void BeginPlayback();
    void RemovePrepareScreen();

    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<UAudioComponent> Narration;
    TWeakObjectPtr<AWowGameplayController> Controller;
    TArray<FKey> PositionKeys, TargetKeys;
    FVector PositionBase = FVector::ZeroVector, TargetBase = FVector::ZeroVector, Origin = FVector::ZeroVector;
    float Facing = 0.f, FovRadians = 0.785f, DurationMs = 0.f, TimeMs = 0.f;
    uint32 NarrationSound = 0;
    bool bPlaying = false;
    bool bFinished = false;
    bool bTookControl = false;
    /** r467 (Stefan: the night elf flyby showed trees over an empty sea, then his character came
     *  back while the narrator spoke): SMSG_TRIGGER_CINEMATIC arrives during world entry, whose
     *  end turned the view back to the pawn. The flyby now waits for the world entry, then holds a
     *  loading screen while the terrain and objects at its start load, and only then flies. */
    enum class EPhase : uint8 { WaitWorld, Prepare, Play };
    EPhase Phase = EPhase::WaitWorld;
    double PhaseSince = 0.0, StableSince = 0.0;
    uint64 StableRevision = 0;
    TSharedPtr<class SWowLoadingScreen> PrepareScreen;
    /** r603: performance during the flight, logged every 5 s (frames, worst frame, thread/GPU times). */
    double PerfSince = 0.0; int32 PerfFrames = 0; float PerfWorstMs = 0.f;
};
