#pragma once
#include "WowEntity.h"

// The server duration describes the whole path, not an equal time per point.
// Return the current segment tangent as well as position; looking one point
// ahead makes a creature turn sideways before it has reached a corner.
inline FVector SampleWowLinearSpline(const FWowMovementInfo& Move, float Progress, FVector& Tangent)
{
    Tangent=FVector::ZeroVector;
    double Length=0;
    FVector Previous=Move.SplineStartPosition;
    for(const auto& Point:Move.SplineWaypoints)
    {
        Length+=FVector::Distance(Previous,Point.Position);
        Previous=Point.Position;
    }
    if(!FMath::IsFinite(Length) || Length<=UE_SMALL_NUMBER) return Move.SplineStartPosition;
    double Remaining=FMath::Clamp(Progress,0.f,1.f)*Length;
    Previous=Move.SplineStartPosition;
    for(const auto& Point:Move.SplineWaypoints)
    {
        const FVector Delta=Point.Position-Previous;
        const double SegmentLength=Delta.Size();
        if(SegmentLength>UE_SMALL_NUMBER)
        {
            Tangent=Delta;
            if(Remaining<=SegmentLength) return Previous+Delta*(Remaining/SegmentLength);
            Remaining-=SegmentLength;
        }
        Previous=Point.Position;
    }
    return Previous;
}
