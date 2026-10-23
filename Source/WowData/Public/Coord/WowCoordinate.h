#pragma once
#include "CoreMinimal.h"

/**
 * Coordinate conversion between WoW/WoW ADT space and UE space.
 *
 * WoW ADT coordinate system (used by ADT data):
 *   X = east (increases going east), range: 0 to 64*TILESIZE
 *   Y = up (height)
 *   Z = south (increases going south), range: 0 to 64*TILESIZE
 *   Tile (0,0) is at the northwest corner
 *
 * UE coordinate system:
 *   X = north (we map -WoW ADT.Z to UE.X so north is positive)
 *   Y = east (we map WoW ADT.X to UE.Y)
 *   Z = up (we map WoW ADT.Y to UE.Z)
 *   Scale: 1 WoW unit ~ 1 yard ~ 91.44cm, we use SCALE=100 for simplicity
 */
struct WOWDATA_API FWowCoordinate
{
    static constexpr float TILE_SIZE = 533.33333f;
    static constexpr float CHUNK_SIZE = TILE_SIZE / 16.0f;
    static constexpr float UNIT_SIZE = CHUNK_SIZE / 8.0f;
    static constexpr float MAP_ORIGIN = 32.0f * TILE_SIZE;
    static constexpr float SCALE = 100.0f;

    /** ADT center offset: tile (32,32) center in ADT coords, so UE (0,0) = map center.
     *  This keeps all coordinates within ~17km of origin for float precision. */
    static constexpr float ADT_CENTER_X = 32.0f * TILE_SIZE + TILE_SIZE * 0.5f;
    static constexpr float ADT_CENTER_Z = 32.0f * TILE_SIZE + TILE_SIZE * 0.5f;

    /** Convert WoW ADT coords (X=east, Y=up, Z=south) to UE coords (centered on map) */
    static FVector AdtToUE(float NgX, float NgY, float NgZ)
    {
        return FVector(-(NgZ - ADT_CENTER_Z) * SCALE, (NgX - ADT_CENTER_X) * SCALE, NgY * SCALE);
    }

    /** Get UE world position for the center of a tile (for placing the tile actor) */
    static FVector TileToWorld(int32 TileX, int32 TileY)
    {
        float NgX = TileX * TILE_SIZE + TILE_SIZE * 0.5f;  // east-west center
        float NgZ = TileY * TILE_SIZE + TILE_SIZE * 0.5f;  // north-south center
        return AdtToUE(NgX, 0.0f, NgZ);
    }

    /** Get the tile indices for a given UE world position */
    static FIntPoint WorldToTile(const FVector& UEPos)
    {
        // Reverse of centered AdtToUE
        float NgX = UEPos.Y / SCALE + ADT_CENTER_X;
        float NgZ = -UEPos.X / SCALE + ADT_CENTER_Z;
        int32 TX = FMath::FloorToInt32(NgX / TILE_SIZE);
        int32 TY = FMath::FloorToInt32(NgZ / TILE_SIZE);
        return FIntPoint(TX, TY);
    }

    /** Convert WoW world coords (centered map space used by movement/login packets) to UE coords */
    static FVector WowToUE(float X, float Y, float Z)
    {
        const float NgX = MAP_ORIGIN - Y;
        const float NgZ = MAP_ORIGIN - X;
        return AdtToUE(NgX, Z, NgZ);
    }
    static FVector WowToUE(const FVector& P) { return WowToUE(P.X, P.Y, P.Z); }
    static FVector UEToWow(const FVector& P)
    {
        const float NgX = P.Y / SCALE + ADT_CENTER_X;
        const float NgY = P.Z / SCALE;
        const float NgZ = -P.X / SCALE + ADT_CENTER_Z;
        return FVector(MAP_ORIGIN - NgZ, MAP_ORIGIN - NgX, NgY);
    }

    /** Convert a WoW world-facing angle (radians) to an Unreal yaw (degrees). */
    static float WowOrientationToUEYaw(float OrientationRadians)
    {
        return FRotator::NormalizeAxis(-FMath::RadiansToDegrees(OrientationRadians));
    }

    /** Convert an Unreal yaw (degrees) to a normalized WoW world-facing angle (radians). */
    static float UEYawToWowOrientation(float YawDegrees)
    {
        return FMath::UnwindRadians(-FMath::DegreesToRadians(YawDegrees));
    }

    /** Return the Unreal yaw matching a direction expressed in WoW world coordinates. */
    static float WowDirectionToUEYaw(const FVector& WowDirection)
    {
        const FVector UEDirection(WowDirection.X, -WowDirection.Y, 0.0f);
        return UEDirection.IsNearlyZero() ? 0.0f : UEDirection.Rotation().Yaw;
    }

    /**
     * Convert WoW MODF/MDDF Euler rotation to UE quaternion.
     *
     * Pipeline: build rotation in ADT space using noggit3's from_model_rotation
     * (-RZ, RY-90, RX) applied in YZX order, then convert from ADT to UE axes.
     * Noggit uses clockwise X/Z and counterclockwise Y axis matrices. In a
     * standard Hamilton quaternion that means angles (RZ, RY-90, -RX).
     *
     * ADT axes → UE axes: X(east)→Y, Y(up)→Z, Z(south)→-X
     * Quaternion conversion: Q_ue = (-Q_adt.Z, Q_adt.X, Q_adt.Y, -Q_adt.W)
     *
     * Verified via R_ue = A * R_adt * M_adt * M^(-1) matrix derivation
     * where A=ADT→UE, M_adt=model→ADT vertex, M=model→UE vertex transforms.
     */
    static FQuat WowRotationToUEQuat(float RX, float RY, float RZ)
    {
        // Step 1: Build rotation quaternion in ADT space (right-handed, X=east, Y=up, Z=south)
        // Convert the source clockwise pitch/roll to Hamilton angles, YZX order.
        // Yaw-only objects were correct; tilted waterfalls/rocks leaned backwards.
        const float HalfYaw   = FMath::DegreesToRadians(RY - 90.0f) * 0.5f;  // around ADT Y (up)
        const float HalfPitch = FMath::DegreesToRadians(-RX) * 0.5f;           // around ADT Z (south)
        const float HalfRoll  = FMath::DegreesToRadians(RZ) * 0.5f;          // around ADT X (east)

        // Individual quaternions in ADT space: Qaxis(angle) = (sin*nx, sin*ny, sin*nz, cos)
        const FQuat Qy(0, FMath::Sin(HalfYaw), 0, FMath::Cos(HalfYaw));
        const FQuat Qz(0, 0, FMath::Sin(HalfPitch), FMath::Cos(HalfPitch));
        const FQuat Qx(FMath::Sin(HalfRoll), 0, 0, FMath::Cos(HalfRoll));

        const FQuat Q = Qy * Qz * Qx;  // YZX order

        // Step 2: Convert ADT quaternion to UE quaternion
        // ADT X(east)→UE Y, ADT Y(up)→UE Z, ADT Z(south)→-UE X, plus handedness flip
        return FQuat(-Q.Z, Q.X, Q.Y, -Q.W);
    }

    /** Convenience wrapper returning FRotator (for APIs that need it). */
    static FRotator WowRotationToUE(float RX, float RY, float RZ)
    {
        return WowRotationToUEQuat(RX, RY, RZ).Rotator();
    }
};
