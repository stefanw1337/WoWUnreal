#pragma once

#include "CoreMinimal.h"

struct FWowFirstPersonCameraProfile
{
	float TopClearanceFraction;
	float ForwardOffset;
	float FallbackEyeHeight;
};

struct FWowFirstPersonLookDownOffset
{
	float Forward = 0.0f;
	float Up = 0.0f;
};

/** Race-aware eye placement for the full-body first-person presentation. */
struct FWowFirstPersonCameraPolicy
{
	static constexpr float PerspectiveNearClipPlane = 2.0f;
    static FORCEINLINE float ClampHeadYaw(float ViewYaw, float BodyYaw)
    {
        const float Relative=FMath::FindDeltaAngleDegrees(BodyYaw,ViewYaw);
        return FRotator::NormalizeAxis(BodyYaw+FMath::Clamp(Relative,-85.0f,85.0f));
    }

	static FORCEINLINE FWowFirstPersonLookDownOffset ResolveLookDownOffset(float ViewPitchDegrees)
	{
		if (!FMath::IsFinite(ViewPitchDegrees))
		{
			return {};
		}

		// Keep the proven weapon-safe eye point for level running. Only as the
		// player looks steeply down do we slide out of the head/upper-spine volume.
		const float NormalizedPitch = FRotator::NormalizeAxis(ViewPitchDegrees);
		const float DownDegrees = FMath::Clamp(-NormalizedPitch, 0.0f, 90.0f);
		const float LinearAlpha = FMath::Clamp((DownDegrees - 30.0f) / 42.0f, 0.0f, 1.0f);
		const float SmoothAlpha = LinearAlpha * LinearAlpha * (3.0f - 2.0f * LinearAlpha);
		return {18.0f * SmoothAlpha, 7.0f * SmoothAlpha};
	}

	static FORCEINLINE bool ShouldHideHeadEquipmentFromOwner(
		bool bFirstPerson,
		bool bHelmetViewEnabled)
	{
		// OwnerNoSee is a local render flag. It does not remove the component or
		// alter the character representation rendered by another client.
		return bFirstPerson && !bHelmetViewEnabled;
	}

	static FORCEINLINE FWowFirstPersonCameraProfile Resolve(uint8 RaceId, uint8 GenderId)
	{
		// Imported model bounds provide the main height. These race values account
		// for anatomy that bounds alone cannot distinguish, especially horns and
		// the proportionally larger heads of dwarves and gnomes.
		FWowFirstPersonCameraProfile Profile{0.070f, 20.0f, 120.0f};
		switch (RaceId)
		{
		case 2:  Profile = {0.080f, 25.0f, 125.0f}; break; // Orc
		// A dwarf's broad two-handed run cycle carries large weapon heads in
		// front of the face. Keep the locally head-hidden lens near the animated
		// head pivot so the complete weapon arc stays in front of the camera.
		case 3:  Profile = {0.105f, 4.0f, 92.0f}; break;   // Dwarf
		case 4:  Profile = {0.065f, 21.0f, 142.0f}; break; // Night elf
		case 5:  Profile = {0.085f, 19.0f, 112.0f}; break; // Undead
		case 6:  Profile = {0.185f, 31.0f, 145.0f}; break; // Tauren horns
		case 7:  Profile = {0.120f, 13.0f, 72.0f}; break;  // Gnome
		case 8:  Profile = {0.080f, 25.0f, 137.0f}; break; // Troll
		case 10: Profile = {0.070f, 19.0f, 125.0f}; break; // Blood elf
		case 11: Profile = {0.095f, 23.0f, 137.0f}; break; // Draenei
		default: break;                                    // Human / safe fallback
		}

		if (GenderId == 1)
		{
			Profile.ForwardOffset = FMath::Max(2.0f, Profile.ForwardOffset - 2.0f);
			Profile.FallbackEyeHeight *= 0.94f;
		}
		return Profile;
	}
};
