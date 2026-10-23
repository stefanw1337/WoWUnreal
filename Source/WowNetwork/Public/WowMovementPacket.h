#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"

/**
 * Client -> server MovementInfo for build 12340 (3.3.5a).
 *
 * The server (AzerothCore/TrinityCore WorldSession::ReadMovementInfo) decides
 * which optional blocks follow the fixed header purely from the flags, so the
 * writer below derives every optional block from the same flags it writes.
 * A packet can therefore never claim data it does not contain, which is what
 * made TormentWoW reject our MSG_MOVE_HEARTBEAT (0x0EE) as "too short".
 *
 * Layout:
 *   packed guid, u32 flags, u16 flags2, u32 time, f32 x, y, z, o
 *   [ONTRANSPORT]  packed guid, f32 x, y, z, o, u32 time, u8 seat
 *                  [flags2 INTERPOLATED_MOVEMENT] u32 time2
 *   [SWIMMING|FLYING or flags2 ALWAYS_ALLOW_PITCHING]  f32 pitch
 *   u32 fallTime
 *   [FALLING]      f32 zspeed, f32 sin, f32 cos, f32 xyspeed
 *   [SPLINE_ELEVATION] f32 elevation
 */
namespace WowMoveFlag
{
    static constexpr uint32 FORWARD          = 0x00000001;
    static constexpr uint32 BACKWARD         = 0x00000002;
    static constexpr uint32 STRAFE_LEFT      = 0x00000004;
    static constexpr uint32 STRAFE_RIGHT     = 0x00000008;
    static constexpr uint32 TURN_LEFT        = 0x00000010;
    static constexpr uint32 TURN_RIGHT       = 0x00000020;
    static constexpr uint32 PITCH_UP         = 0x00000040;
    static constexpr uint32 PITCH_DOWN       = 0x00000080;
    static constexpr uint32 WALKING          = 0x00000100;
    static constexpr uint32 ONTRANSPORT      = 0x00000200;
    static constexpr uint32 DISABLE_GRAVITY  = 0x00000400;
    static constexpr uint32 ROOT             = 0x00000800;
    static constexpr uint32 FALLING          = 0x00001000; // jump or fall; carries jump block
    static constexpr uint32 FALLING_FAR      = 0x00002000;
    static constexpr uint32 PENDING_STOP     = 0x00004000;
    static constexpr uint32 SWIMMING         = 0x00200000;
    static constexpr uint32 ASCENDING        = 0x00400000;
    static constexpr uint32 DESCENDING       = 0x00800000;
    static constexpr uint32 CAN_FLY          = 0x01000000;
    static constexpr uint32 FLYING           = 0x02000000;
    static constexpr uint32 SPLINE_ELEVATION = 0x04000000;
    static constexpr uint32 SPLINE_ENABLED   = 0x08000000;
    static constexpr uint32 WATERWALKING     = 0x10000000;
    static constexpr uint32 FALLING_SLOW     = 0x20000000;
    static constexpr uint32 HOVER            = 0x40000000;

    static constexpr uint32 MOVING_MASK = FORWARD | BACKWARD | STRAFE_LEFT | STRAFE_RIGHT;
    static constexpr uint32 TURNING_MASK = TURN_LEFT | TURN_RIGHT;

    static constexpr uint16 FLAGS2_ALWAYS_ALLOW_PITCHING = 0x0020;
    static constexpr uint16 FLAGS2_INTERPOLATED_MOVEMENT = 0x0400;
}

struct FWowOutgoingMovementInfo
{
    uint64 Guid = 0;
    uint32 Flags = 0;
    uint16 Flags2 = 0;
    uint32 TimeMs = 0;
    FVector Position = FVector::ZeroVector; // WoW world coordinates (yards)
    float Orientation = 0.0f;

    uint64 TransportGuid = 0;
    FVector TransportOffset = FVector::ZeroVector;
    float TransportOrientation = 0.0f;
    uint32 TransportTimeMs = 0;
    int8 TransportSeat = -1;
    uint32 TransportTime2Ms = 0;

    float Pitch = 0.0f;
    uint32 FallTimeMs = 0;
    float JumpZSpeed = 0.0f;   // WoW convention: negative while rising
    float JumpSinAngle = 0.0f;
    float JumpCosAngle = 1.0f;
    float JumpXYSpeed = 0.0f;
    float SplineElevation = 0.0f;
};

namespace WowMovementPacket
{
    /** Millisecond client clock shared by movement packets and CMSG_TIME_SYNC_RESP. */
    inline uint32 ClientTimeMs()
    {
        return static_cast<uint32>(static_cast<uint64>(FPlatformTime::Seconds() * 1000.0));
    }

    inline void AppendU8(TArray<uint8>& Out, uint8 V) { Out.Add(V); }
    inline void AppendU16(TArray<uint8>& Out, uint16 V) { Out.Append(reinterpret_cast<const uint8*>(&V), 2); }
    inline void AppendU32(TArray<uint8>& Out, uint32 V) { Out.Append(reinterpret_cast<const uint8*>(&V), 4); }
    inline void AppendFloat(TArray<uint8>& Out, float V)
    {
        if (!FMath::IsFinite(V)) V = 0.0f;
        Out.Append(reinterpret_cast<const uint8*>(&V), 4);
    }

    inline void AppendPackedGuid(TArray<uint8>& Out, uint64 Guid)
    {
        const int32 MaskIndex = Out.Add(0);
        for (int32 I = 0; I < 8; ++I)
        {
            const uint8 Byte = static_cast<uint8>((Guid >> (I * 8)) & 0xFF);
            if (Byte != 0)
            {
                Out[MaskIndex] |= static_cast<uint8>(1u << I);
                Out.Add(Byte);
            }
        }
    }

    inline bool HasPitch(uint32 Flags, uint16 Flags2)
    {
        return (Flags & (WowMoveFlag::SWIMMING | WowMoveFlag::FLYING)) != 0
            || (Flags2 & WowMoveFlag::FLAGS2_ALWAYS_ALLOW_PITCHING) != 0;
    }

    /** Size of the MovementInfo body (without the packed guid) the server will expect for these flags. */
    inline int32 ExpectedBodySize(uint32 Flags, uint16 Flags2, uint64 TransportGuid = 0)
    {
        int32 Size = 4 + 2 + 4 + 16;
        if (Flags & WowMoveFlag::ONTRANSPORT)
        {
            int32 TransportGuidBytes = 1;
            for (int32 I = 0; I < 8; ++I) if ((TransportGuid >> (I * 8)) & 0xFF) ++TransportGuidBytes;
            Size += TransportGuidBytes + 16 + 4 + 1;
            if (Flags2 & WowMoveFlag::FLAGS2_INTERPOLATED_MOVEMENT) Size += 4;
        }
        if (HasPitch(Flags, Flags2)) Size += 4;
        Size += 4;
        if (Flags & WowMoveFlag::FALLING) Size += 16;
        if (Flags & WowMoveFlag::SPLINE_ELEVATION) Size += 4;
        return Size;
    }

    /** Append MovementInfo (without guid). Optional blocks follow the flags exactly. */
    inline void AppendMovementBody(TArray<uint8>& Out, const FWowOutgoingMovementInfo& Info)
    {
        uint32 Flags = Info.Flags;
        // A transport block without a transport guid is meaningless; never claim it.
        if (Info.TransportGuid == 0) Flags &= ~WowMoveFlag::ONTRANSPORT;

        AppendU32(Out, Flags);
        AppendU16(Out, Info.Flags2);
        AppendU32(Out, Info.TimeMs);
        AppendFloat(Out, Info.Position.X);
        AppendFloat(Out, Info.Position.Y);
        AppendFloat(Out, Info.Position.Z);
        AppendFloat(Out, Info.Orientation);

        if (Flags & WowMoveFlag::ONTRANSPORT)
        {
            AppendPackedGuid(Out, Info.TransportGuid);
            AppendFloat(Out, Info.TransportOffset.X);
            AppendFloat(Out, Info.TransportOffset.Y);
            AppendFloat(Out, Info.TransportOffset.Z);
            AppendFloat(Out, Info.TransportOrientation);
            AppendU32(Out, Info.TransportTimeMs);
            AppendU8(Out, static_cast<uint8>(Info.TransportSeat));
            if (Info.Flags2 & WowMoveFlag::FLAGS2_INTERPOLATED_MOVEMENT) AppendU32(Out, Info.TransportTime2Ms);
        }
        if (HasPitch(Flags, Info.Flags2)) AppendFloat(Out, Info.Pitch);
        AppendU32(Out, Info.FallTimeMs);
        if (Flags & WowMoveFlag::FALLING)
        {
            AppendFloat(Out, Info.JumpZSpeed);
            AppendFloat(Out, Info.JumpSinAngle);
            AppendFloat(Out, Info.JumpCosAngle);
            AppendFloat(Out, Info.JumpXYSpeed);
        }
        if (Flags & WowMoveFlag::SPLINE_ELEVATION) AppendFloat(Out, Info.SplineElevation);
    }

    /** Full MSG_MOVE_* payload: packed guid + MovementInfo. */
    inline TArray<uint8> BuildMovePayload(const FWowOutgoingMovementInfo& Info)
    {
        TArray<uint8> Out;
        Out.Reserve(80);
        AppendPackedGuid(Out, Info.Guid);
        AppendMovementBody(Out, Info);
        return Out;
    }

    /** CMSG_*_ACK payload: packed guid, u32 counter, MovementInfo, then optional trailing value. */
    inline TArray<uint8> BuildAckPayload(const FWowOutgoingMovementInfo& Info, uint32 Counter)
    {
        TArray<uint8> Out;
        Out.Reserve(88);
        AppendPackedGuid(Out, Info.Guid);
        AppendU32(Out, Counter);
        AppendMovementBody(Out, Info);
        return Out;
    }
}
