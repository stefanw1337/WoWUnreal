#pragma once
#include "CoreMinimal.h"

class AWowGameplayController;
class APawn;

/**
 * r419 (Stefan: "head turning on NPCs as well - if I target them, they look at me; of course only
 * I can see that"): the NPC you have targeted turns its head (and a little of the neck) towards your
 * face - your eyes in first person / VR, your character's head otherwise - using the same head look
 * as the player model (UWowFootIKInstance). Only while it is alive, within 25 m and you are in front
 * of it (the head look ignores points behind it). Purely local: the server and other players see
 * nothing. Called from the player's CalcCamera.
 */
class FWowNpcHeadLook
{
public:
    static void Update(AWowGameplayController* Controller, APawn* Player, const FVector& CameraLocation);
};
