#pragma once
#include "CoreMinimal.h"

class ACharacter;
class UWowAnimationController;
class FMpqManager;
class FWowAssetCache;

/**
 * r432 (Stefan: "flying to another place doesn't spawn the gryphon"): the local player's mount.
 * Remote players were drawn on their mounts, but the local player never was - neither the taxi
 * gryphon nor a mount spell. While UNIT_FIELD_MOUNTDISPLAYID is set, the mount creature stands
 * under the player's capsule (facing the same way), the player's body sits on its saddle socket
 * playing the Mount pose, and the mount runs, flies or stands with the player's motion.
 */
class FWowLocalMount
{
public:
    /** Every tick from the player animation update. Returns true while mounted (the rider pose is set). */
    static bool Update(ACharacter* Rider, UWowAnimationController* RiderAnimation, uint32 MountDisplayId,
        FMpqManager* Mpq, FWowAssetCache* Cache, float DeltaTime);
};
