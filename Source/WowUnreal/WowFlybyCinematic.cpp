#include "WowFlybyCinematic.h"
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowOpcodes.h"
#include "WowWorldManager.h"
#include "WowAudioManager.h"
#include "WowTrailerPlayer.h"
#include "WowClientPreferences.h"
#include "WowLoginController.h"
#include "SWowLoadingScreen.h"
#include "HAL/PlatformTime.h"
#include "Misc/ConfigCacheIni.h"
#include "Mpq/MpqManager.h"
#include "Formats/DbcParser.h"
#include "Coord/WowCoordinate.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "RHI.h"
#include "RenderCore.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowCinematicFlyby, Log, All);

namespace
{
bool LoadDbc(FMpqManager* Mpq, const TCHAR* Path, FDbcParser& Out)
{
    TArray<uint8> Raw;
    return Mpq && Mpq->ReadFile(Path, Raw) && Out.Parse(Raw) && Out.IsValid();
}
template<typename T> bool ReadAt(const TArray<uint8>& D, int64 Offset, T& Out)
{
    if (Offset < 0 || Offset + int64(sizeof(T)) > D.Num()) return false;
    FMemory::Memcpy(&Out, D.GetData() + Offset, sizeof(T));
    return true;
}
// One M2 camera track (WotLK layout): keys of the first sequence; spline keys carry value + two tangents.
struct FTrackKey { float Time = 0.f; FVector Value, InTan, OutTan; uint8 Interp = 0; };
bool ReadTrack(const TArray<uint8>& D, int64 Offset, TArray<FTrackKey>& Out)
{
    int16 Interp = 0, Global = 0; uint32 NT = 0, OT = 0, NV = 0, OV = 0;
    if (!ReadAt(D, Offset, Interp) || !ReadAt(D, Offset + 2, Global) || !ReadAt(D, Offset + 4, NT) || !ReadAt(D, Offset + 8, OT)
        || !ReadAt(D, Offset + 12, NV) || !ReadAt(D, Offset + 16, OV) || NT == 0 || NV == 0) return false;
    uint32 KT = 0, KTO = 0, KV = 0, KVO = 0;
    if (!ReadAt(D, OT, KT) || !ReadAt(D, OT + 4, KTO) || !ReadAt(D, OV, KV) || !ReadAt(D, OV + 4, KVO)) return false;
    const int32 Stride = Interp >= 2 ? 36 : 12;
    const uint32 Count = FMath::Min(KT, KV);
    if (Count == 0 || Count > 4096) return false;
    for (uint32 K = 0; K < Count; ++K)
    {
        uint32 Time = 0; float V[9] = {};
        if (!ReadAt(D, KTO + 4ll * K, Time)) return false;
        for (int32 I = 0; I < (Stride == 36 ? 9 : 3); ++I)
            if (!ReadAt(D, KVO + int64(Stride) * K + 4ll * I, V[I])) return false;
        FTrackKey Key;
        Key.Time = float(Time);
        Key.Value = FVector(V[0], V[1], V[2]);
        Key.InTan = FVector(V[3], V[4], V[5]);
        Key.OutTan = FVector(V[6], V[7], V[8]);
        Key.Interp = uint8(FMath::Clamp<int16>(Interp, 0, 3));
        Out.Add(Key);
    }
    return true;
}
}

AWowFlybyCinematic::AWowFlybyCinematic()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FlybyCamera"));
    SetRootComponent(Camera);
}

uint32 AWowFlybyCinematic::SequenceForCharacter(FMpqManager* Mpq, uint8 Race, uint8 Class)
{
    FDbcParser Parser;
    // ChrClasses field 58 / ChrRaces field 12 (3.3.5): CinematicSequenceID (r380: was field 5, a name slot).
    if (Class && LoadDbc(Mpq, TEXT("DBFilesClient\\ChrClasses.dbc"), Parser) && Parser.GetFieldCount() > 58)
        for (uint32 R = 0; R < Parser.GetRecordCount(); ++R)
            if (Parser.GetUInt(R, 0) == Class && Parser.GetUInt(R, 58) > 0 && Parser.GetUInt(R, 58) < 1000)
                return Parser.GetUInt(R, 58);
    if (LoadDbc(Mpq, TEXT("DBFilesClient\\ChrRaces.dbc"), Parser))
        for (uint32 R = 0; R < Parser.GetRecordCount(); ++R)
            if (Parser.GetUInt(R, 0) == Race && Parser.GetFieldCount() > 12) return Parser.GetUInt(R, 12);
    // Same values as the 3.3.5 data, should the tables be missing.
    static const uint32 Fallback[] = {0, 81, 21, 41, 61, 2, 141, 101, 121, 0, 162, 163};
    if (Class == 6) return 165;
    return Race < UE_ARRAY_COUNT(Fallback) ? Fallback[Race] : 0;
}

bool AWowFlybyCinematic::ParseCamera(const TArray<uint8>& M2, FVector& OutPositionBase, TArray<FKey>& OutPositions,
    FVector& OutTargetBase, TArray<FKey>& OutTargets, float& OutFov, float& OutDurationMs)
{
    uint32 Magic = 0, Version = 0, NCameras = 0, OCameras = 0;
    if (!ReadAt(M2, 0, Magic) || Magic != 0x3032444D /*MD20*/ || !ReadAt(M2, 4, Version) || Version < 264) return false;
    if (!ReadAt(M2, 272, NCameras) || !ReadAt(M2, 276, OCameras) || NCameras == 0) return false;
    const int64 C = OCameras;
    float Fov = 0.f;
    if (!ReadAt(M2, C + 4, Fov)) return false;
    OutFov = Fov > 0.05f && Fov < 3.0f ? Fov : 0.785f;
    TArray<FTrackKey> Pos, Target;
    float PX = 0, PY = 0, PZ = 0, TX = 0, TY = 0, TZ = 0;
    if (!ReadTrack(M2, C + 16, Pos) || !ReadAt(M2, C + 36, PX) || !ReadAt(M2, C + 40, PY) || !ReadAt(M2, C + 44, PZ)
        || !ReadTrack(M2, C + 48, Target) || !ReadAt(M2, C + 68, TX) || !ReadAt(M2, C + 72, TY) || !ReadAt(M2, C + 76, TZ)) return false;
    OutPositionBase = FVector(PX, PY, PZ);
    OutTargetBase = FVector(TX, TY, TZ);
    OutPositions.Reset(); OutTargets.Reset();
    for (const FTrackKey& K : Pos) OutPositions.Add({K.Time, K.Value, K.InTan, K.OutTan, K.Interp});
    for (const FTrackKey& K : Target) OutTargets.Add({K.Time, K.Value, K.InTan, K.OutTan, K.Interp});
    OutDurationMs = FMath::Max(OutPositions.Last().TimeMs, OutTargets.Last().TimeMs);
    return OutDurationMs > 0.f;
}

bool AWowFlybyCinematic::Load(FMpqManager* Mpq, uint32 SequenceId)
{
    FDbcParser Sequences, Cameras;
    if (!LoadDbc(Mpq, TEXT("DBFilesClient\\CinematicSequences.dbc"), Sequences)
        || !LoadDbc(Mpq, TEXT("DBFilesClient\\CinematicCamera.dbc"), Cameras)) return false;
    uint32 CameraId = 0;
    for (uint32 R = 0; R < Sequences.GetRecordCount() && !CameraId; ++R)
        if (Sequences.GetUInt(R, 0) == SequenceId)
            for (uint32 F = 2; F < FMath::Min<uint32>(Sequences.GetFieldCount(), 10) && !CameraId; ++F)
                CameraId = Sequences.GetUInt(R, F);
    if (!CameraId) return false;
    for (uint32 R = 0; R < Cameras.GetRecordCount(); ++R)
    {
        if (Cameras.GetUInt(R, 0) != CameraId) continue;
        FString ModelPath = Cameras.GetString(R, 1);
        if (ModelPath.EndsWith(TEXT(".mdx"), ESearchCase::IgnoreCase) || ModelPath.EndsWith(TEXT(".mdl"), ESearchCase::IgnoreCase))
            ModelPath = ModelPath.LeftChop(4) + TEXT(".m2");
        NarrationSound = Cameras.GetUInt(R, 2);
        Origin = FVector(Cameras.GetFloat(R, 3), Cameras.GetFloat(R, 4), Cameras.GetFloat(R, 5));
        Facing = Cameras.GetFloat(R, 6);
        TArray<uint8> Raw;
        if (!Mpq->ReadFile(ModelPath, Raw))
        {
            UE_LOG(LogWowCinematicFlyby, Warning, TEXT("Flyby camera %s not found"), *ModelPath);
            return false;
        }
        if (!ParseCamera(Raw, PositionBase, PositionKeys, TargetBase, TargetKeys, FovRadians, DurationMs))
        {
            UE_LOG(LogWowCinematicFlyby, Warning, TEXT("Flyby camera %s could not be read"), *ModelPath);
            return false;
        }
        UE_LOG(LogWowCinematicFlyby, Log, TEXT("Flyby sequence %u: camera %u %s, %.1f s, %d/%d keys, origin %s facing %.3f, narration %u"),
            SequenceId, CameraId, *ModelPath, DurationMs / 1000.f, PositionKeys.Num(), TargetKeys.Num(),
            *Origin.ToCompactString(), Facing, NarrationSound);
        return true;
    }
    return false;
}

FVector AWowFlybyCinematic::Sample(const TArray<FKey>& Keys, float Time)
{
    if (Keys.IsEmpty()) return FVector::ZeroVector;
    if (Time <= Keys[0].TimeMs) return Keys[0].Value;
    if (Time >= Keys.Last().TimeMs) return Keys.Last().Value;
    int32 I = 0;
    while (I + 1 < Keys.Num() && Keys[I + 1].TimeMs < Time) ++I;
    const FKey& A = Keys[I];
    const FKey& B = Keys[FMath::Min(I + 1, Keys.Num() - 1)];
    const FVector P0 = Keys[FMath::Max(I - 1, 0)].Value, P3 = Keys[FMath::Min(I + 2, Keys.Num() - 1)].Value;
    const float T = FMath::Clamp((Time - A.TimeMs) / FMath::Max(B.TimeMs - A.TimeMs, 1.f), 0.f, 1.f);
    // r603 (Stefan: the flyby is "a bit jagged at places"): the camera paths are authored Bezier splines.
    // Their keys are unevenly spaced (some only 33 ms apart, where the flight holds or turns), and a
    // Catmull-Rom through the bare values sped up and braked at every key. Use the authored control
    // points like the original client: Bezier (interp 2) A, A.out, B.in, B; Hermite (3) with tangents.
    if (A.Interp == 2)
    {
        const float U = 1.f - T;
        return A.Value * (U * U * U) + A.OutTan * (3.f * U * U * T) + B.InTan * (3.f * U * T * T) + B.Value * (T * T * T);
    }
    if (A.Interp == 3)
    {
        const float T2h = T * T, T3h = T2h * T;
        return A.Value * (2.f * T3h - 3.f * T2h + 1.f) + A.OutTan * (T3h - 2.f * T2h + T)
            + B.Value * (-2.f * T3h + 3.f * T2h) + B.InTan * (T3h - T2h);
    }
    if (A.Interp == 1) return FMath::Lerp(A.Value, B.Value, T);
    // Catmull-Rom through the authored keys (smooth, passes every key).
    const float T2 = T * T, T3 = T2 * T;
    return 0.5f * ((2.f * A.Value) + (-P0 + B.Value) * T + (2.f * P0 - 5.f * A.Value + 4.f * B.Value - P3) * T2
        + (-P0 + 3.f * A.Value - 3.f * B.Value + P3) * T3);
}

FVector AWowFlybyCinematic::ToWorld(const FVector& Base, const FVector& Offset) const
{
    // The camera model's space is turned by the record's facing about the origin (as the
    // server's cinematic manager places it): distance and bearing in the XY plane, Z added.
    const FVector L = Base + Offset;
    const float Distance = FMath::Sqrt(L.X * L.X + L.Y * L.Y);
    const float Angle = FMath::Atan2(L.X, L.Y) - Facing;
    const FVector Wow(Origin.X + Distance * FMath::Sin(Angle), Origin.Y + Distance * FMath::Cos(Angle), Origin.Z + L.Z);
    return FWowCoordinate::WowToUE(Wow);
}

AWowFlybyCinematic* AWowFlybyCinematic::Start(AWowGameplayController* Controller, FMpqManager* Mpq, uint32 SequenceId)
{
    if (!Controller || !Controller->GetWorld() || !Mpq) return nullptr;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AWowFlybyCinematic* Flyby = Controller->GetWorld()->SpawnActor<AWowFlybyCinematic>(Params);
    if (!Flyby) return nullptr;
    Flyby->Controller = Controller;
    if (!Flyby->Load(Mpq, SequenceId))
    {
        UE_LOG(LogWowCinematicFlyby, Warning, TEXT("Cinematic sequence %u has no playable camera; completing it at once"), SequenceId);
        Flyby->Finish(true);
        return nullptr;
    }
    // r467: nothing is shown yet; Tick waits for the world entry and the flight's first area.
    Flyby->bPlaying = true;
    Flyby->Phase = EPhase::WaitWorld;
    Flyby->PhaseSince = FPlatformTime::Seconds();
    return Flyby;
}

void AWowFlybyCinematic::RemovePrepareScreen()
{
    if (!PrepareScreen.IsValid()) return;
    if (GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(PrepareScreen.ToSharedRef());
    PrepareScreen.Reset();
}

void AWowFlybyCinematic::BeginPlayback()
{
    AWowGameplayController* PC = Controller.Get();
    Phase = EPhase::Play;
    TimeMs = 0.f;
    SetActorLocation(ToWorld(PositionBase, Sample(PositionKeys, 0.f)));
    const FVector Target = ToWorld(TargetBase, Sample(TargetKeys, 0.f));
    if (!(Target - GetActorLocation()).IsNearlyZero()) SetActorRotation((Target - GetActorLocation()).Rotation());
    if (PC) PC->SetViewTargetWithBlend(this, 0.f);
    RemovePrepareScreen();
    if (AWowAudioManager* Audio = Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(this, AWowAudioManager::StaticClass())))
        Narration = Audio->PlayNarration(NarrationSound);
    UE_LOG(LogWowCinematicFlyby, Log, TEXT("Flyby starts after %.1f s of loading"), FPlatformTime::Seconds() - PhaseSince);
}

void AWowFlybyCinematic::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bPlaying) return;
    AWowGameplayController* PC = Controller.Get();
    if (!PC) { Finish(true); return; }
    AWowWorldManager* WorldManager = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this, AWowWorldManager::StaticClass()));
    const double Now = FPlatformTime::Seconds();
    const FVector StartPosition = ToWorld(PositionBase, Sample(PositionKeys, 0.f));
    if (Phase == EPhase::WaitWorld)
    {
        // The login's world entry reveals the pawn when it is done (and then hands the view to it).
        const APawn* Pawn = PC->GetPawn();
        if ((!Pawn || Pawn->IsHidden()) && Now - PhaseSince < 60.0) return;
        bTookControl = true;
        PC->SetUserInterfaceHidden(true);
        PC->SetIgnoreMoveInput(true);
        PC->SetIgnoreLookInput(true);
        if (WorldManager) WorldManager->SetCinematicStreamingFocus(true, StartPosition);
        auto* Login = Cast<AWowLoginController>(UGameplayStatics::GetActorOfClass(this, AWowLoginController::StaticClass()));
        const uint32 Map = PC->ConnectionManager ? PC->ConnectionManager->PacketHandler.DeathState.CurrentMap : 0;
        PrepareScreen = Login ? TSharedPtr<SWowLoadingScreen>(Login->CreateTransferLoadingScreen(Map)) : TSharedPtr<SWowLoadingScreen>(SNew(SWowLoadingScreen));
        if (PrepareScreen.IsValid() && GEngine && GEngine->GameViewport)
            GEngine->GameViewport->AddViewportWidgetContent(PrepareScreen.ToSharedRef(), 1000);
        Phase = EPhase::Prepare;
        PhaseSince = StableSince = Now;
        StableRevision = WorldManager ? WorldManager->GetObjectStreamingRevision() : 0;
        UE_LOG(LogWowCinematicFlyby, Log, TEXT("Flyby waits for its first area at %s"), *StartPosition.ToCompactString());
        return;
    }
    if (Phase == EPhase::Prepare)
    {
        bool bReady = true;
        if (WorldManager)
        {
            WorldManager->SetCinematicStreamingFocus(true, StartPosition);
            const uint64 Revision = WorldManager->GetObjectStreamingRevision();
            const bool bTerrain = WorldManager->IsTerrainReadyAtPosition(StartPosition) && WorldManager->GetPendingTileCount() == 0;
            if (Revision != StableRevision || !bTerrain) { StableRevision = Revision; StableSince = Now; }
            bReady = bTerrain && Now - StableSince >= 1.0;
        }
        if (PrepareScreen.IsValid())
            PrepareScreen->SetProgress(FMath::Clamp(float((Now - PhaseSince) / 10.0), 0.05f, 0.95f), TEXT("Loading..."));
        if (!bReady && Now - PhaseSince < 25.0) return;
        BeginPlayback();
        return;
    }
    // Something else (a loading screen ending, a teleport) may hand the view back to the pawn.
    if (PC->GetViewTarget() != this) PC->SetViewTargetWithBlend(this, 0.f);
    TimeMs += DeltaSeconds * 1000.f;
    const FVector Position = ToWorld(PositionBase, Sample(PositionKeys, TimeMs));
    const FVector Target = ToWorld(TargetBase, Sample(TargetKeys, TimeMs));
    SetActorLocation(Position);
    if (!(Target - Position).IsNearlyZero()) SetActorRotation((Target - Position).Rotation());
    // The authored field of view is diagonal; UE's is horizontal.
    FVector2D ViewSize(16.f, 9.f);
    if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(ViewSize);
    const float Aspect = ViewSize.Y > 0.f ? ViewSize.X / ViewSize.Y : 16.f / 9.f;
    Camera->SetFieldOfView(FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FovRadians * 0.5f) * Aspect / FMath::Sqrt(1.f + Aspect * Aspect))));
    // Stream a little ahead of the camera, so the next stretch is there when it arrives.
    if (WorldManager)
        WorldManager->SetCinematicStreamingFocus(true, ToWorld(PositionBase, Sample(PositionKeys, FMath::Min(TimeMs + 2500.f, DurationMs))));
    // r603 (Stefan: "the performance in the flyby is still pretty horrible"): what the flight costs, so the
    // next round can aim at the right thread (game / render / GPU) and at streaming.
    {
        const double PerfNow = FPlatformTime::Seconds();
        if (PerfSince <= 0.0) PerfSince = PerfNow;
        ++PerfFrames;
        PerfWorstMs = FMath::Max(PerfWorstMs, DeltaSeconds * 1000.f);
        if (PerfNow - PerfSince >= 5.0)
        {
            UE_LOG(LogWowCinematicFlyby, Log, TEXT("Flyby perf at %.0f s: %.1f fps, worst frame %.0f ms | game %.1f ms, render %.1f, RHI %.1f, GPU %.1f | pending tiles %d | camera %s"),
                TimeMs / 1000.f, PerfFrames / (PerfNow - PerfSince), PerfWorstMs,
                FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime),
                FPlatformTime::ToMilliseconds(GRHIThreadTime), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()),
                WorldManager ? WorldManager->GetPendingTileCount() : -1, *Position.ToCompactString());
            PerfSince = PerfNow; PerfFrames = 0; PerfWorstMs = 0.f;
        }
    }
    if (TimeMs >= DurationMs) Finish(false);
}

void AWowFlybyCinematic::Skip()
{
    if (bPlaying) Finish(true);
}

void AWowFlybyCinematic::Finish(bool bSkipped)
{
    if (bFinished) return;
    bFinished = true;
    bPlaying = false;
    RemovePrepareScreen();
    if (Narration && bSkipped) Narration->Stop();
    if (AWowWorldManager* World = Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(this, AWowWorldManager::StaticClass())))
        World->SetCinematicStreamingFocus(false);
    if (AWowGameplayController* PC = Controller.Get())
    {
        if (PC->ConnectionManager) PC->ConnectionManager->SendRawPacket(WowOpcode::CMSG_COMPLETE_CINEMATIC);
    }
    if (AWowGameplayController* PC = bTookControl ? Controller.Get() : nullptr)
    {
        PC->SetIgnoreMoveInput(false);
        PC->SetIgnoreLookInput(false);
        PC->SetUserInterfaceHidden(false);
        if (APawn* Pawn = PC->GetPawn()) PC->SetViewTargetWithBlend(Pawn, bSkipped ? 0.f : 1.0f);
    }
    UE_LOG(LogWowCinematicFlyby, Log, TEXT("Flyby %s after %.1f s"), bSkipped ? TEXT("skipped") : TEXT("finished"), TimeMs / 1000.f);
    SetLifeSpan(1.5f);
}

void AWowFlybyCinematic::EndPlay(const EEndPlayReason::Type Reason)
{
    if (!bFinished && bPlaying) Finish(true);
    Super::EndPlay(Reason);
}

void AWowFlybyCinematic::HandleTrigger(AWowGameplayController* Controller, FMpqManager* Mpq, uint8 Race, uint8 Class, uint8 Gender,
    uint64 CharacterGuid, const FString& RealmName, TWeakObjectPtr<AWowFlybyCinematic>* ActiveFlyby)
{
    if (!Controller) return;
    const uint32 Sequence = SequenceForCharacter(Mpq, Race, Class);
    TWeakObjectPtr<AWowGameplayController> WeakController = Controller;
    auto StartFlyby = [WeakController, Mpq, Sequence, ActiveFlyby]()
    {
        AWowGameplayController* PC = WeakController.Get();
        if (!PC || !ActiveFlyby) return;
        if (ActiveFlyby->IsValid()) (*ActiveFlyby)->Skip();
        *ActiveFlyby = Start(PC, Mpq, Sequence);
    };

    // Class intro movie: once per character (data folder + realm + character), never twice.
    FString Movie;
    const FString DataPath = Mpq ? Mpq->GetDataPath() : FString();
    const FString SeenKey = FString::Printf(TEXT("ClassIntro_%08x_%08x_%016llx"), GetTypeHash(DataPath.ToLower()), GetTypeHash(RealmName.ToLower()), CharacterGuid);
    bool bSeen = false;
    if (GConfig) GConfig->GetBool(TEXT("WowUnreal.ClassIntros"), *SeenKey, bSeen, GGameUserSettingsIni);
    if (!bSeen && !FWowClientPreferences::GetWorldOption(TEXT("skipintromovies")))
        Movie = FWowTrailerPlayer::FindClassIntro(Mpq, Race, Class, Gender);
    const bool bFlybyAfter = FWowTrailerPlayer::GetServerRule(Mpq, TEXT("ClassIntroFlyby"), TEXT("replace")).Equals(TEXT("after"), ESearchCase::IgnoreCase);
    UE_LOG(LogWowCinematicFlyby, Log, TEXT("Cinematic triggered: race %u class %u gender %u -> sequence %u, class intro %s%s"),
        Race, Class, Gender, Sequence, Movie.IsEmpty() ? TEXT("none") : *Movie, bSeen ? TEXT(" (already seen)") : TEXT(""));
    if (Movie.IsEmpty())
    {
        StartFlyby();
        return;
    }
    if (GConfig)
    {
        // Marked before playing: a movie that crashes or cannot decode must not come back.
        GConfig->SetBool(TEXT("WowUnreal.ClassIntros"), *SeenKey, true, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
    Controller->SetUserInterfaceHidden(true);
    Controller->SetIgnoreMoveInput(true);
    Controller->SetIgnoreLookInput(true);
    FWowMovieOptions Options;
    Options.bInWorld = true;
    Options.Reason = TEXT("class intro");
    const bool bStarted = FWowTrailerPlayer::PlayMovie(Mpq, Movie, Options, [WeakController, StartFlyby, bFlybyAfter](bool bPlayed)
    {
        AWowGameplayController* PC = WeakController.Get();
        if (!PC) return;
        PC->SetIgnoreMoveInput(false);
        PC->SetIgnoreLookInput(false);
        PC->SetUserInterfaceHidden(false);
        // A movie that could not be decoded falls back to the flyby, like a missing one.
        if (bFlybyAfter || !bPlayed) StartFlyby();
        else if (PC->ConnectionManager) PC->ConnectionManager->SendRawPacket(WowOpcode::CMSG_COMPLETE_CINEMATIC);
    });
    if (!bStarted)
    {
        Controller->SetIgnoreMoveInput(false);
        Controller->SetIgnoreLookInput(false);
        Controller->SetUserInterfaceHidden(false);
        StartFlyby();
    }
}
