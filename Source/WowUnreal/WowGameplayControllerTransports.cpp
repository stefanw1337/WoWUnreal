// Transports (r599): boats and zeppelins (game object type 15, MO_TRANSPORT) and elevators (type 11).
//
// The 3.3.5 client moves transports itself; the server only sends where a transport is on its path
// (the UPDATEFLAG_TRANSPORT path timer in the create block, kept in FWowMovementInfo since r598).
//  - Boats/zeppelins follow their taxi path (TaxiPathNode.dbc, template data0) with the same keyframe
//    generation the server uses (AzerothCore/TrinityCore TransportMgr::GeneratePath: Catmull-Rom
//    segments, acceleration data2 up to speed data1, waits at stop nodes, map changes) and the same
//    position calculation (Transport::Update / CalculateSegmentPos).
//  - Elevators follow TransportAnimation.dbc for their entry, as offsets from the spawn point.
// The computed position is written back into the entity (so streaming and other code see the real
// place) and the actor is moved here; UpdateEntityNetworkMovement leaves these actors alone.
//
// r600, riding:
//  - Local player: while the pawn stands on (or jumps from) a transport, every movement packet carries
//    ONTRANSPORT with the offset and facing in the transport's frame (ApplyLocalTransport, from
//    BuildLocalMovementInfo); boarding and leaving send a heartbeat at once so the server adds or drops
//    the passenger. The deck carries the pawn as a moving base (all transport parts are Movable).
//  - Other players and NPCs with a transport guid are placed at transport position + rotated offset.
//  - Continent change on board: SMSG_TRANSFER_PENDING names the transport, and SMSG_NEW_WORLD then holds
//    the deck offset, not a world position (ResolveTransportTransfer turns it into the world position on
//    the new map from our own path). Until the ship itself has arrived on the new map the pawn is held on
//    the computed deck spot, so it never drops into the sea.

#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowOpcodes.h"
#include "Mpq/MpqManager.h"
#include "Formats/DbcParser.h"
#include "Coord/WowCoordinate.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "WowMovementPacket.h"
#include "Components/SceneComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowTransport, Log, All);

// WowPacketHandler.cpp (r598): game object template (type, data0..23) by entry.
WOWNETWORK_API const TArray<uint32>* WowFindGameObjectTemplate(uint32 Entry);
// WowPacketHandler.cpp (r600): transport entry of the last SMSG_TRANSFER_PENDING (0 if none), cleared on read.
WOWNETWORK_API uint32 WowTakePendingTransportTransfer();

namespace WowTransport
{
    struct FNode
    {
        uint32 Map = 0;
        FVector P = FVector::ZeroVector;
        uint32 Flags = 0, Delay = 0, ArrivalEvent = 0, DepartureEvent = 0;
    };

    // Catmull-Rom spline as the server's Movement::Spline (non-cyclic, length by 3 steps per segment).
    struct FSpline
    {
        TArray<FVector> Points;
        TArray<double> Lengths;
        int32 Lo = 1, Hi = 0;

        static void Weights(float T, bool bDerivative, float Out[4])
        {
            static const float M[4][4] = { {-0.5f, 1.5f, -1.5f, 0.5f}, {1.f, -2.5f, 2.f, -0.5f}, {-0.5f, 0.f, 0.5f, 0.f}, {0.f, 1.f, 0.f, 0.f} };
            const float Tv[4] = { bDerivative ? 3.f * T * T : T * T * T, bDerivative ? 2.f * T : T * T, bDerivative ? 1.f : T, bDerivative ? 0.f : 1.f };
            for (int32 J = 0; J < 4; ++J) { Out[J] = 0.f; for (int32 K = 0; K < 4; ++K) Out[J] += Tv[K] * M[K][J]; }
        }
        FVector Eval(int32 Index, float T, bool bDerivative = false) const
        {
            // The last control point is the end of the last segment.
            if (Index >= Hi && Hi > Lo) { Index = Hi - 1; T = 1.f; }
            Index = FMath::Clamp(Index, Lo, FMath::Max(Lo, Hi - 1));
            float W[4]; Weights(T, bDerivative, W);
            FVector R = FVector::ZeroVector;
            for (int32 J = 0; J < 4; ++J) R += Points[FMath::Clamp(Index - 1 + J, 0, Points.Num() - 1)] * W[J];
            return R;
        }
        void Init(const TArray<FVector>& Controls)
        {
            Points.Reset();
            Points.Add(Controls[0]);
            Points.Append(Controls);
            Points.Add(Controls.Last());
            Hi = Controls.Num();
            Lengths.Init(0.0, Hi + 1);
            double Acc = 0.0;
            for (int32 I = Lo; I < Hi; ++I)
            {
                FVector Cur = Points[I];
                double L = 0.0;
                for (int32 S = 1; S <= 3; ++S)
                {
                    const FVector Next = Eval(I, float(S) / 3.f);
                    L += FVector::Dist(Next, Cur);
                    Cur = Next;
                }
                Acc += L;
                Lengths[I + 1] = Acc;
            }
        }
        double Length(int32 A, int32 B) const
        {
            return Lengths.IsValidIndex(A) && Lengths.IsValidIndex(B) ? Lengths[B] - Lengths[A] : 0.0;
        }
    };

    struct FFrame
    {
        FNode Node;
        bool bTeleport = false;
        int32 Index = 0;
        float Dist = -1.f, NextDist = 0.f, Since = -1.f, Until = -1.f, TimeFrom = 0.f, TimeTo = 0.f;
        uint32 Arrive = 0, Depart = 0, NextArrive = 0;
        TSharedPtr<FSpline> Spline;
        bool IsStop() const { return (Node.Flags & 2u) != 0; }
    };

    struct FPath
    {
        TArray<FFrame> Frames;
        uint32 Period = 0;
        float Speed = 0.f, Accel = 0.f, AccelTime = 0.f, AccelDist = 0.f;
        bool bValid = false;
    };

    struct FAnimKey { uint32 Time = 0; FVector P = FVector::ZeroVector; };

    struct FState
    {
        uint32 Entry = 0;
        uint8 Type = 0;
        FVector BasePosition = FVector::ZeroVector; // spawn (elevators)
        float CreateOrientation = 0.f;
        bool bHasYawOffset = false;
        float YawOffset = 0.f; // actor yaw - WowOrientationToUEYaw(create orientation) at spawn
        double LoggedAt = -1000.0;
        bool bHidden = false;
        int32 MovableComponents = -1; // component count when they were last made movable
    };

    bool GTaxiLoaded = false, GAnimLoaded = false;
    TMap<uint32, TArray<FNode>> GTaxiNodes;            // path id -> nodes in order
    TMap<uint32, TArray<FAnimKey>> GAnimations;        // elevator entry -> keys
    TMap<uint32, TSharedPtr<FPath>> GPaths;            // transport entry -> generated path
    TMap<uint64, FState> GStates;                      // guid -> state
    TSet<uint32> GQueried;
    // r600: last known path timer per transport entry (survives the entity being removed on a map change).
    struct FTimerBase { uint32 Timer = 0; double At = 0.0; };
    TMap<uint32, FTimerBase> GEntryTimers;
    TMap<uint64, double> GProgress;      // guid -> path progress (ms) this frame, for the transport time field
    uint64 GLastSentTransport = 0;       // transport guid of the last movement packet we sent
    uint64 GLastBaseTransport = 0;       // transport the pawn stood on last frame
    struct FBoarding { uint32 Entry = 0; FVector Offset = FVector::ZeroVector; float Orientation = 0.f; double Until = 0.0; };
    FBoarding GBoarding;                 // held on the deck after a continent change on board

    FVector OffsetToWorld(const FVector& TransportPos, float TransportOrientation, const FVector& Offset)
    {
        const float C = FMath::Cos(TransportOrientation), S = FMath::Sin(TransportOrientation);
        return TransportPos + FVector(Offset.X * C - Offset.Y * S, Offset.X * S + Offset.Y * C, Offset.Z);
    }
    FVector WorldToOffset(const FVector& TransportPos, float TransportOrientation, const FVector& World)
    {
        const FVector D = World - TransportPos;
        const float C = FMath::Cos(TransportOrientation), S = FMath::Sin(TransportOrientation);
        return FVector(D.X * C + D.Y * S, -D.X * S + D.Y * C, D.Z);
    }

    void LoadTaxiNodes(FMpqManager* Mpq)
    {
        if (GTaxiLoaded || !Mpq) return;
        GTaxiLoaded = true;
        TArray<uint8> Bytes; FDbcParser P;
        // TaxiPathNode.dbc (3.3.5): ID, PathID, NodeIndex, MapID, X, Y, Z, Flags, Delay, ArrivalEventID, DepartureEventID.
        if (!Mpq->ReadFile(TEXT("DBFilesClient\\TaxiPathNode.dbc"), Bytes) || !P.Parse(Bytes) || P.GetFieldCount() < 11) return;
        TMap<uint32, TArray<TPair<uint32, FNode>>> ByPath;
        for (uint32 I = 0; I < P.GetRecordCount(); ++I)
        {
            FNode N;
            N.Map = P.GetUInt(I, 3);
            N.P = FVector(P.GetFloat(I, 4), P.GetFloat(I, 5), P.GetFloat(I, 6));
            N.Flags = P.GetUInt(I, 7); N.Delay = P.GetUInt(I, 8);
            N.ArrivalEvent = P.GetUInt(I, 9); N.DepartureEvent = P.GetUInt(I, 10);
            ByPath.FindOrAdd(P.GetUInt(I, 1)).Add({ P.GetUInt(I, 2), N });
        }
        for (auto& Pair : ByPath)
        {
            Pair.Value.Sort([](const TPair<uint32, FNode>& A, const TPair<uint32, FNode>& B) { return A.Key < B.Key; });
            TArray<FNode>& Out = GTaxiNodes.Add(Pair.Key);
            for (const auto& Entry : Pair.Value) Out.Add(Entry.Value);
        }
        UE_LOG(LogWowTransport, Log, TEXT("Transports: %d taxi paths loaded"), GTaxiNodes.Num());
    }

    void LoadAnimations(FMpqManager* Mpq)
    {
        if (GAnimLoaded || !Mpq) return;
        GAnimLoaded = true;
        TArray<uint8> Bytes; FDbcParser P;
        // TransportAnimation.dbc (3.3.5): ID, TransportID, TimeIndex, X, Y, Z, SequenceID.
        if (!Mpq->ReadFile(TEXT("DBFilesClient\\TransportAnimation.dbc"), Bytes) || !P.Parse(Bytes) || P.GetFieldCount() < 6) return;
        for (uint32 I = 0; I < P.GetRecordCount(); ++I)
        {
            FAnimKey K; K.Time = P.GetUInt(I, 2); K.P = FVector(P.GetFloat(I, 3), P.GetFloat(I, 4), P.GetFloat(I, 5));
            GAnimations.FindOrAdd(P.GetUInt(I, 1)).Add(K);
        }
        for (auto& Pair : GAnimations) Pair.Value.Sort([](const FAnimKey& A, const FAnimKey& B) { return A.Time < B.Time; });
        UE_LOG(LogWowTransport, Log, TEXT("Transports: %d elevator animations loaded"), GAnimations.Num());
    }

    // TransportMgr::GeneratePath (AzerothCore/TrinityCore 3.3.5).
    TSharedPtr<FPath> Generate(const TArray<FNode>& Nodes, float Speed, float Accel)
    {
        TSharedPtr<FPath> Path = MakeShared<FPath>();
        if (Nodes.Num() < 2 || Speed <= 0.f || Accel <= 0.f) return Path;
        TArray<FFrame>& K = Path->Frames;
        TArray<FVector> SplinePath;
        bool bMapChange = false;
        for (int32 I = 0; I < Nodes.Num(); ++I)
        {
            if (!bMapChange)
            {
                const FNode& N = Nodes[I];
                if (I != Nodes.Num() - 1 && ((N.Flags & 1u) || N.Map != Nodes[I + 1].Map))
                {
                    if (K.Num() > 0) K.Last().bTeleport = true;
                    bMapChange = true;
                }
                else
                {
                    FFrame F; F.Node = N;
                    K.Add(F);
                    SplinePath.Add(N.P);
                }
            }
            else bMapChange = false;
        }
        if (SplinePath.Num() >= 2)
        {
            if (!K[0].IsStop() && !K[0].Node.ArrivalEvent && !K[0].Node.DepartureEvent) { SplinePath.RemoveAt(0); K.RemoveAt(0); }
            if (!K.Last().IsStop() && !K.Last().Node.ArrivalEvent && !K.Last().Node.DepartureEvent) { SplinePath.Pop(); K.Pop(); }
        }
        if (K.Num() < 2) return Path;
        K.Last().bTeleport = true;

        const float AccelDist = 0.5f * Speed * Speed / Accel;
        Path->Speed = Speed; Path->Accel = Accel; Path->AccelTime = Speed / Accel; Path->AccelDist = AccelDist;
        int32 FirstStop = -1, LastStop = -1;
        K[0].Dist = 0.f; K[0].Index = 1;
        if (K[0].IsStop()) { FirstStop = 0; LastStop = 0; }
        int32 Start = 0;
        for (int32 I = 1; I < K.Num(); ++I)
        {
            if (K[I - 1].bTeleport || I + 1 == K.Num())
            {
                const int32 Extra = !K[I - 1].bTeleport ? 1 : 0;
                TArray<FVector> Controls;
                for (int32 J = Start; J < I + Extra && J < SplinePath.Num(); ++J) Controls.Add(SplinePath[J]);
                TSharedPtr<FSpline> Spline = MakeShared<FSpline>();
                if (Controls.Num() >= 1) Spline->Init(Controls);
                for (int32 J = Start; J < I + Extra; ++J)
                {
                    K[J].Index = J - Start + 1;
                    K[J].Dist = float(Spline->Length(J - Start, J + 1 - Start));
                    if (J > 0) K[J - 1].NextDist = K[J].Dist;
                    K[J].Spline = Spline;
                }
                if (K[I - 1].bTeleport)
                {
                    K[I].Index = I - Start + 1;
                    K[I].Dist = 0.f;
                    K[I - 1].NextDist = 0.f;
                    K[I].Spline = Spline;
                }
                Start = I;
            }
            if (K[I].IsStop())
            {
                if (FirstStop == -1) FirstStop = I;
                LastStop = I;
            }
        }
        K.Last().NextDist = K[0].Dist;
        if (FirstStop == -1 || LastStop == -1) FirstStop = LastStop = 0;
        const int32 N = K.Num();
        float Tmp = 0.f;
        for (int32 I = 0; I < N; ++I)
        {
            const int32 J = (I + LastStop) % N;
            if (K[J].IsStop() || J == LastStop) Tmp = 0.f; else Tmp += K[J].Dist;
            K[J].Since = Tmp;
        }
        Tmp = 0.f;
        for (int32 I = N - 1; I >= 0; --I)
        {
            const int32 J = (I + FirstStop) % N;
            Tmp += K[(J + 1) % N].Dist;
            K[J].Until = Tmp;
            if (K[J].IsStop() || J == FirstStop) Tmp = 0.f;
        }
        for (FFrame& F : K)
        {
            const float Total = F.Since + F.Until;
            if (Total < 2.f * AccelDist)
            {
                if (F.Since < F.Until) F.TimeTo = 2.f * FMath::Sqrt(Total / Accel) - FMath::Sqrt(2.f * F.Since / Accel);
                else F.TimeTo = FMath::Sqrt(2.f * F.Until / Accel);
            }
            else if (F.Since < AccelDist) F.TimeTo = Total / Speed + Speed / Accel - FMath::Sqrt(2.f * F.Since / Accel);
            else if (F.Until < AccelDist) F.TimeTo = FMath::Sqrt(2.f * F.Until / Accel);
            else F.TimeTo = F.Until / Speed + 0.5f * Speed / Accel;
        }
        float Segment = 0.f;
        for (int32 I = 0; I < N; ++I)
        {
            const int32 J = (I + LastStop) % N;
            if (K[J].IsStop() || J == LastStop) Segment = K[J].TimeTo;
            K[J].TimeFrom = Segment - K[J].TimeTo;
        }
        K[0].Arrive = 0;
        float Cur = 0.f;
        if (K[0].IsStop()) { Cur = float(K[0].Node.Delay); K[0].Depart = uint32(Cur * 1000.f); }
        for (int32 I = 1; I < N; ++I)
        {
            Cur += K[I - 1].TimeTo;
            if (K[I].IsStop())
            {
                K[I].Arrive = uint32(Cur * 1000.f);
                K[I - 1].NextArrive = K[I].Arrive;
                Cur += float(K[I].Node.Delay);
                K[I].Depart = uint32(Cur * 1000.f);
            }
            else
            {
                Cur -= K[I].TimeTo;
                K[I].Arrive = uint32(Cur * 1000.f);
                K[I - 1].NextArrive = K[I].Arrive;
                K[I].Depart = K[I].Arrive;
            }
        }
        K.Last().NextArrive = K.Last().Depart;
        Path->Period = K.Last().Depart;
        Path->bValid = Path->Period > 0;
        return Path;
    }

    // Transport::Update + CalculateSegmentPos: position, orientation (WoW radians) and map at path time Ms.
    bool Evaluate(const FPath& Path, uint32 Ms, FVector& OutPos, float& OutOrientation, uint32& OutMap, bool& bOutMoving)
    {
        const TArray<FFrame>& K = Path.Frames;
        if (!Path.bValid || K.Num() < 2) return false;
        int32 I = K.Num() - 1;
        for (int32 J = 0; J < K.Num(); ++J)
        {
            if (Ms >= K[J].Arrive && Ms < FMath::Max(K[J].NextArrive, K[J].Depart)) { I = J; break; }
        }
        const FFrame& F = K[I];
        OutMap = F.Node.Map;
        if (!F.Spline.IsValid() || F.Spline->Points.Num() < 2) return false;
        if (Ms < F.Depart || F.NextDist <= 0.f || F.bTeleport)
        {
            // Waiting at a stop (or on the last point before a map change).
            bOutMoving = false;
            OutPos = F.Spline->Eval(F.Index, 0.f);
            const FVector Dir = F.Spline->Eval(F.Index, 0.f, true);
            OutOrientation = FMath::Atan2(float(Dir.Y), float(Dir.X)) + PI;
            return true;
        }
        bOutMoving = true;
        const float Now = float(Ms) * 0.001f;
        const float Since = F.TimeFrom + (Now - 0.001f * float(F.Depart));
        const float Until = F.TimeTo - (Now - 0.001f * float(F.Depart));
        float Dist, SegPos;
        if (Since < Until)
        {
            Dist = Since < Path.AccelTime ? 0.5f * Path.Accel * Since * Since : Path.AccelDist + (Since - Path.AccelTime) * Path.Speed;
            SegPos = Dist - F.Since;
        }
        else
        {
            Dist = Until < Path.AccelTime ? 0.5f * Path.Accel * Until * Until : Path.AccelDist + (Until - Path.AccelTime) * Path.Speed;
            SegPos = F.Until - Dist;
        }
        const float T = FMath::Clamp(SegPos / F.NextDist, 0.f, 1.f);
        OutPos = F.Spline->Eval(F.Index, T);
        const FVector Dir = F.Spline->Eval(F.Index, T, true);
        OutOrientation = FMath::Atan2(float(Dir.Y), float(Dir.X)) + PI;
        return true;
    }

    FVector EvaluateAnimation(const TArray<FAnimKey>& Keys, uint32 Ms)
    {
        if (Keys.Num() == 0) return FVector::ZeroVector;
        if (Keys.Num() == 1 || Ms <= Keys[0].Time) return Keys[0].P;
        for (int32 I = 1; I < Keys.Num(); ++I)
        {
            if (Ms <= Keys[I].Time)
            {
                const float Span = float(FMath::Max<uint32>(Keys[I].Time - Keys[I - 1].Time, 1u));
                return FMath::Lerp(Keys[I - 1].P, Keys[I].P, float(Ms - Keys[I - 1].Time) / Span);
            }
        }
        return Keys.Last().P;
    }
}

bool AWowGameplayController::IsClientDrivenTransport(uint64 Guid) const
{
    return WowTransport::GStates.Contains(Guid);
}

void AWowGameplayController::UpdateTransports(float DeltaTime)
{
    using namespace WowTransport;
    if (!ConnectionManager) return;
    FWowPacketHandler& Handler = ConnectionManager->PacketHandler;
    const uint32 CurrentMap = Handler.DeathState.CurrentMap;
    const double Now = FPlatformTime::Seconds();

    for (const auto& Pair : Handler.EntityManager.GetAll())
    {
        FWowEntity* Entity = Pair.Value.Get();
        if (!Entity || !Entity->IsGameObject()) continue;
        const FWowGameObjectEntity* Object = static_cast<const FWowGameObjectEntity*>(Entity);
        const uint8 Type = Object->GetGameObjectType();
        if (Type != 11 && Type != 15) continue;

        FState* State = GStates.Find(Pair.Key);
        if (!State)
        {
            State = &GStates.Add(Pair.Key);
            State->Entry = Entity->Entry;
            State->Type = Type;
            State->BasePosition = Entity->Movement.Position;
            State->CreateOrientation = Entity->Movement.Orientation;
        }

        FVector WowPos = State->BasePosition;
        float Orientation = State->CreateOrientation;
        uint32 Map = CurrentMap;
        bool bMoving = false, bHaveRotation = false;
        const double Elapsed = Entity->Movement.bHasTransportTimer ? (Now - Entity->Movement.TransportTimerAt) * 1000.0 : 0.0;
        const double Progress = double(Entity->Movement.TransportTimer) + Elapsed;
        if (Entity->Movement.bHasTransportTimer)
            GEntryTimers.Add(Entity->Entry, { Entity->Movement.TransportTimer, Entity->Movement.TransportTimerAt });
        GProgress.Add(Pair.Key, Progress);

        if (Type == 15)
        {
            const TArray<uint32>* Template = WowFindGameObjectTemplate(Entity->Entry);
            if (!Template || Template->Num() < 4)
            {
                if (!GQueried.Contains(Entity->Entry))
                {
                    GQueried.Add(Entity->Entry);
                    TArray<uint8> Query;
                    const uint32 Entry = Entity->Entry; const uint64 Guid = Entity->Guid;
                    Query.Append(reinterpret_cast<const uint8*>(&Entry), 4);
                    Query.Append(reinterpret_cast<const uint8*>(&Guid), 8);
                    ConnectionManager->SendRawPacket(WowOpcode::CMSG_GAMEOBJECT_QUERY, Query);
                    UE_LOG(LogWowTransport, Log, TEXT("Transport %u: querying its template"), Entity->Entry);
                }
                continue;
            }
            TSharedPtr<FPath>* Found = GPaths.Find(Entity->Entry);
            if (!Found)
            {
                LoadTaxiNodes(CachedMpq);
                const uint32 PathId = (*Template)[1];
                const float Speed = float((*Template)[2]);
                const float Accel = float((*Template)[3]);
                const TArray<FNode>* Nodes = GTaxiNodes.Find(PathId);
                TSharedPtr<FPath> Generated = Nodes ? Generate(*Nodes, Speed, Accel) : MakeShared<FPath>();
                Found = &GPaths.Add(Entity->Entry, Generated);
                UE_LOG(LogWowTransport, Log, TEXT("Transport %u: taxi path %u (%d nodes), speed %.0f, accel %.2f -> %d keyframes, period %.1f s (server LEVEL field %d)"),
                    Entity->Entry, PathId, Nodes ? Nodes->Num() : 0, Speed, Accel, Generated->Frames.Num(),
                    Generated->Period * 0.001f, Object->GetGameObjectLevel());
            }
            const FPath& Path = **Found;
            if (!Path.bValid) continue;
            const uint32 Ms = uint32(FMath::Fmod(FMath::Max(Progress, 0.0), double(Path.Period)));
            if (!Evaluate(Path, Ms, WowPos, Orientation, Map, bMoving)) continue;
            bHaveRotation = true;
        }
        else
        {
            LoadAnimations(CachedMpq);
            const TArray<FAnimKey>* Keys = GAnimations.Find(Entity->Entry);
            if (!Keys || Keys->Num() == 0) continue;
            uint32 Period = Keys->Last().Time;
            // r602: the animation's own loop is the period (Deeprun Tram cars 143.3 s); the LEVEL field is only
            // trusted when it agrees with it (its meaning for type 11 differs between server cores).
            const int32 Level = Object->GetGameObjectLevel();
            if (Level > 0 && FMath::Abs(float(Level) - float(Period)) < 0.05f * float(Period)) Period = uint32(Level);
            if (Period == 0) continue;
            const uint32 Ms = uint32(FMath::Fmod(FMath::Max(Progress, 0.0), double(Period)));
            const FVector Local = EvaluateAnimation(*Keys, Ms);
            const float C = FMath::Cos(State->CreateOrientation), S = FMath::Sin(State->CreateOrientation);
            WowPos = State->BasePosition + FVector(Local.X * C - Local.Y * S, Local.X * S + Local.Y * C, Local.Z);
            bMoving = true;
        }

        Entity->Movement.Position = WowPos;
        if (bHaveRotation) Entity->Movement.Orientation = Orientation;

        AActor* Actor = SpawnedEntityActors.FindRef(Pair.Key);
        if (!Actor || !IsValid(Actor)) continue;
        // A transport is a moving base: every part of it must be movable (WMO groups load later, so
        // check again whenever the component count changes). The character movement then carries
        // a player standing on the deck along with it.
        {
            TInlineComponentArray<USceneComponent*> Parts(Actor);
            if (Parts.Num() != State->MovableComponents)
            {
                for (int32 Index = Parts.Num() - 1; Index >= 0; --Index) // children before the root
                    if (Parts[Index] && Parts[Index]->Mobility != EComponentMobility::Movable) Parts[Index]->SetMobility(EComponentMobility::Movable);
                State->MovableComponents = Parts.Num();
            }
        }
        if (!State->bHasYawOffset)
        {
            // The spawn turned the model by its own convention (WMO as is, M2 -90); keep that offset.
            State->YawOffset = FRotator::NormalizeAxis(Actor->GetActorRotation().Yaw - FWowCoordinate::WowOrientationToUEYaw(State->CreateOrientation));
            State->bHasYawOffset = true;
        }
        const bool bHide = Map != CurrentMap;
        if (bHide != State->bHidden)
        {
            State->bHidden = bHide;
            Actor->SetActorHiddenInGame(bHide);
            Actor->SetActorEnableCollision(!bHide);
        }
        const FVector UEPos = FWowCoordinate::WowToUE(WowPos);
        if (UEPos.ContainsNaN()) continue;
        if (bHaveRotation)
        {
            FRotator Rot = Actor->GetActorRotation();
            Rot.Yaw = FRotator::NormalizeAxis(FWowCoordinate::WowOrientationToUEYaw(Orientation) + State->YawOffset);
            Actor->SetActorLocationAndRotation(UEPos, Rot, false, nullptr, ETeleportType::None);
        }
        else Actor->SetActorLocation(UEPos, false, nullptr, ETeleportType::None);

        if (Now - State->LoggedAt > 30.0)
        {
            State->LoggedAt = Now;
            UE_LOG(LogWowTransport, Log, TEXT("Transport %u (type %u): map %u%s, pos (%.1f, %.1f, %.1f), facing %.2f, %s, timer %.0f ms"),
                State->Entry, Type, Map, bHide ? TEXT(" (other map: hidden)") : TEXT(""), WowPos.X, WowPos.Y, WowPos.Z, Orientation,
                bMoving ? TEXT("moving") : TEXT("waiting"), Progress);
        }
    }

    // Forget transports that are gone.
    for (auto It = GStates.CreateIterator(); It; ++It)
        if (!Handler.EntityManager.Find(It.Key())) { GProgress.Remove(It.Key()); It.RemoveCurrent(); }

    // r600: other players and NPCs on board ride at the transport's position plus their rotated offset
    // (UpdateEntityNetworkMovement then places them without smoothing lag).
    for (const auto& Pair : Handler.EntityManager.GetAll())
    {
        FWowEntity* Entity = Pair.Value.Get();
        if (!Entity || Pair.Key == Handler.EntityManager.LocalPlayerGuid || !Entity->IsUnit()) continue;
        const uint64 TransportGuid = Entity->Movement.TransportGuid;
        if (TransportGuid == 0 || !GStates.Contains(TransportGuid)) continue;
        const FWowEntity* Transport = Handler.EntityManager.Find(TransportGuid);
        if (!Transport) continue;
        Entity->Movement.Position = OffsetToWorld(Transport->Movement.Position, Transport->Movement.Orientation, Entity->Movement.TransportOffset);
        Entity->Movement.Orientation = FMath::UnwindRadians(Transport->Movement.Orientation + Entity->Movement.TransportOrientation);
    }

    ACharacter* Char = Cast<ACharacter>(GetPawn());
    if (!Char) return;

    // r600: held on the deck after a continent change on board, until the transport is here and drawn.
    if (GBoarding.Entry != 0)
    {
        FVector TransportPos = FVector::ZeroVector;
        float TransportO = 0.f;
        bool bArrived = false, bKnown = false;
        for (const auto& StatePair : GStates)
        {
            if (StatePair.Value.Entry != GBoarding.Entry || StatePair.Value.bHidden) continue;
            const FWowEntity* Transport = Handler.EntityManager.Find(StatePair.Key);
            AActor* TransportActor = SpawnedEntityActors.FindRef(StatePair.Key);
            if (!Transport) continue;
            TransportPos = Transport->Movement.Position; TransportO = Transport->Movement.Orientation; bKnown = true;
            bArrived = TransportActor && IsValid(TransportActor) && !TransportActor->IsHidden();
            break;
        }
        if (!bKnown)
        {
            // Not created on this map yet: follow our own path for it.
            const TSharedPtr<FPath>* Path = GPaths.Find(GBoarding.Entry);
            const FTimerBase* Base = GEntryTimers.Find(GBoarding.Entry);
            uint32 Map = 0; bool bMoving = false;
            if (Path && Path->IsValid() && (*Path)->bValid && Base)
            {
                const uint32 Ms = uint32(FMath::Fmod(double(Base->Timer) + (Now - Base->At) * 1000.0, double((*Path)->Period)));
                bKnown = Evaluate(**Path, Ms, TransportPos, TransportO, Map, bMoving);
            }
        }
        if (bKnown)
        {
            const FVector Feet = OffsetToWorld(TransportPos, TransportO, GBoarding.Offset);
            FVector PawnPos = FWowCoordinate::WowToUE(Feet);
            if (const UCapsuleComponent* Capsule = Char->GetCapsuleComponent()) PawnPos.Z += Capsule->GetScaledCapsuleHalfHeight() + 4.0f;
            Char->SetActorLocation(PawnPos, false, nullptr, ETeleportType::TeleportPhysics);
            if (UCharacterMovementComponent* Move = Char->GetCharacterMovement()) Move->Velocity = FVector::ZeroVector;
        }
        if (bArrived || Now > GBoarding.Until)
        {
            UE_LOG(LogWowTransport, Log, TEXT("Transport %u: continent change on board %s"), GBoarding.Entry,
                bArrived ? TEXT("done, standing on the deck") : TEXT("timed out (transport not seen on the new map)"));
            GBoarding = FBoarding();
        }
        return;
    }

    // r600: boarding or leaving a transport is told to the server at once.
    const uint64 BaseTransport = LocalTransportGuid(Char);
    if (BaseTransport != GLastSentTransport && BaseTransport != GLastBaseTransport)
    {
        UE_LOG(LogWowTransport, Log, TEXT("Local player %s transport %llu"), BaseTransport ? TEXT("boarded") : TEXT("left"),
            BaseTransport ? BaseTransport : GLastSentTransport);
        ConnectionManager->SendMovementInfo(WowOpcode::MSG_MOVE_HEARTBEAT, BuildLocalMovementInfo(Char, ComputeLocalMovementFlags(Char)));
    }
    GLastBaseTransport = BaseTransport;
}

uint64 AWowGameplayController::LocalTransportGuid(ACharacter* Char) const
{
    using namespace WowTransport;
    if (!Char) return 0;
    const UCharacterMovementComponent* Move = Char->GetCharacterMovement();
    const UPrimitiveComponent* Base = Char->GetMovementBase();
    const AActor* BaseActor = Base ? Base->GetOwner() : nullptr;
    if (BaseActor)
    {
        for (const auto& Pair : SpawnedEntityActors)
            if (Pair.Value.Get() == BaseActor && GStates.Contains(Pair.Key)) return Pair.Key;
        return 0;
    }
    // In the air (jumping on deck): still on board, as in the original client.
    if (Move && Move->IsFalling() && GLastSentTransport != 0 && GStates.Contains(GLastSentTransport)) return GLastSentTransport;
    return 0;
}

void AWowGameplayController::ApplyLocalTransport(ACharacter* Char, FWowOutgoingMovementInfo& Info) const
{
    using namespace WowTransport;
    const uint64 TransportGuid = LocalTransportGuid(Char);
    const FWowEntity* Transport = TransportGuid && ConnectionManager ? ConnectionManager->PacketHandler.EntityManager.Find(TransportGuid) : nullptr;
    if (!Transport)
    {
        GLastSentTransport = 0;
        return;
    }
    Info.Flags |= WowMoveFlag::ONTRANSPORT;
    Info.TransportGuid = TransportGuid;
    Info.TransportOffset = WorldToOffset(Transport->Movement.Position, Transport->Movement.Orientation, Info.Position);
    Info.TransportOrientation = FMath::UnwindRadians(Info.Orientation - Transport->Movement.Orientation);
    Info.TransportTimeMs = uint32(GProgress.FindRef(TransportGuid));
    Info.TransportSeat = -1;
    GLastSentTransport = TransportGuid;
}

bool AWowGameplayController::ResolveTransportTransfer(uint32 MapId, float& X, float& Y, float& Z, float& Orientation)
{
    using namespace WowTransport;
    const uint32 Entry = WowTakePendingTransportTransfer();
    if (Entry == 0) return false;
    GLastSentTransport = 0; GLastBaseTransport = 0;
    const FVector Offset(X, Y, Z);
    const float OffsetO = Orientation;
    const TSharedPtr<FPath>* Path = GPaths.Find(Entry);
    const FTimerBase* Base = GEntryTimers.Find(Entry);
    if (!Path || !Path->IsValid() || !(*Path)->bValid || !Base)
    {
        UE_LOG(LogWowTransport, Warning, TEXT("Transport %u: continent change on board to map %u, but its path is unknown; using the server position as is"), Entry, MapId);
        return false;
    }
    const double Now = FPlatformTime::Seconds();
    const uint32 Ms = uint32(FMath::Fmod(double(Base->Timer) + (Now - Base->At) * 1000.0, double((*Path)->Period)));
    FVector TransportPos; float TransportO = 0.f; uint32 Map = 0; bool bMoving = false;
    if (!Evaluate(**Path, Ms, TransportPos, TransportO, Map, bMoving)) return false;
    const FVector Feet = OffsetToWorld(TransportPos, TransportO, Offset);
    UE_LOG(LogWowTransport, Log, TEXT("Transport %u: continent change on board to map %u (path says map %u): deck offset (%.1f, %.1f, %.1f) -> world (%.1f, %.1f, %.1f)"),
        Entry, MapId, Map, Offset.X, Offset.Y, Offset.Z, Feet.X, Feet.Y, Feet.Z);
    X = Feet.X; Y = Feet.Y; Z = Feet.Z;
    Orientation = FMath::UnwindRadians(TransportO + OffsetO);
    GBoarding.Entry = Entry;
    GBoarding.Offset = Offset;
    GBoarding.Orientation = OffsetO;
    GBoarding.Until = Now + 20.0;
    return true;
}
