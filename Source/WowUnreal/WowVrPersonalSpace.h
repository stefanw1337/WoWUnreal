#pragma once
#include "CoreMinimal.h"

class AWowGameplayController;
class APawn;

/**
 * r415 (Stefan: "some mobs are right up in our face in VR - could they stand further away?"):
 * in the headset a mob in melee range stands with its face (or chest) a few centimetres from the
 * lens. The server decides where it stands, so only the drawn body moves: the mesh of a unit that
 * comes closer than a small personal space is slid back along the line from the player, smoothly,
 * as far as needed (at most 1.6 m). The actor, its collision and its nameplate stay where the
 * server put them. Only in VR first person; everything goes back when VR is off.
 *
 * Called from the player's CalcCamera, which runs after every actor and component has ticked,
 * so the offset is the last word on the mesh this frame.
 */
class FWowVrPersonalSpace
{
public:
    static void Update(AWowGameplayController* Controller, APawn* Player, float DeltaTime, bool bActive);
};
