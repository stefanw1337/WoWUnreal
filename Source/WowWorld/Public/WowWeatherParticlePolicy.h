#pragma once
#include "CoreMinimal.h"

namespace WowWeatherParticles
{
inline int32 SurfaceSlot(const FVector& Position, float CellSize, int32 Grid)
{
    const int32 X = FMath::FloorToInt(Position.X / CellSize);
    const int32 Y = FMath::FloorToInt(Position.Y / CellSize);
    // World-keyed ring buffer: overlapping cells survive camera movement.
    return ((Y % Grid + Grid) % Grid) * Grid + (X % Grid + Grid) % Grid;
}
inline float GroundOpacity(bool bFresh, bool bValid, float HeightAboveGround)
{
    // Unknown is not underground. Depth testing still hides flakes behind terrain;
    // shelter exposure separately hides indoor precipitation.
    return bFresh && bValid ? FMath::Clamp((HeightAboveGround - 8) / 40, 0.f, 1.f) : 1.f;
}
}
