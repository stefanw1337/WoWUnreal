#pragma once
#include "WowEntity.h"

// Predict only grounded player translation. Splines, jumps and transports retain
// their own authoritative paths. Never advance simulation/network coordinates.
inline FVector PredictWowRemoteGroundPosition(const FWowMovementInfo& Move, double Now)
{
    constexpr uint32 Unsupported = 0x00000200u | 0x00000400u | 0x00001000u |
        0x00002000u | 0x00200000u | 0x02000000u;
    if (Move.bHasActiveSpline || Move.TransportGuid != 0 || (Move.MoveFlags & Unsupported) ||
        Move.MovementReceivedAtSeconds <= 0.0 || !FMath::IsFinite(Now) ||
        !FMath::IsFinite(Move.Orientation)) return Move.Position;

    const double Age = FMath::Clamp(Now - Move.MovementReceivedAtSeconds, 0.0, 1.0);
    const float Forward = ((Move.MoveFlags & 1u) ? 1.f : 0.f) - ((Move.MoveFlags & 2u) ? 1.f : 0.f);
    const float Left = ((Move.MoveFlags & 4u) ? 1.f : 0.f) - ((Move.MoveFlags & 8u) ? 1.f : 0.f);
    FVector Direction(Forward, Left, 0);
    if (!Direction.Normalize()) return Move.Position;
    const float Speed = (Move.MoveFlags & 0x100u) ? Move.WalkSpeed :
        (Forward < 0.f ? Move.RunBackSpeed : Move.RunSpeed);
    if (!FMath::IsFinite(Speed) || Speed <= 0.f) return Move.Position;

    const double C = FMath::Cos(Move.Orientation), S = FMath::Sin(Move.Orientation);
    const FVector Velocity((Direction.X*C - Direction.Y*S)*Speed,
        (Direction.X*S + Direction.Y*C)*Speed, 0);
    return Move.Position + Velocity*Age;
}
