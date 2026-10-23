#pragma once
#include "CoreMinimal.h"

class FMpqManager;

/** CreatureSoundData.dbc slots used by the client (3.3.5a column order). */
enum class EWowCreatureSound : uint8
{
    Exertion,          // swing / attack grunt
    ExertionCritical,
    Injury,            // wound
    InjuryCritical,
    InjuryCrushing,
    Death,
    Stun,
    Stand,
    Footstep,
    Aggro,
    Alert,
    Fidget,            // random idle sound (5 variants)
    CustomAttack,      // creature-specific attack (4 variants)
    Birth,
    SpellCastDirected,
};

/** NPCSounds.dbc: greeting when talked to, farewell when the window closes, pissed when clicked repeatedly. */
enum class EWowNpcSound : uint8 { Greeting, Farewell, Pissed };

/**
 * Lazy read-only tables for unit sounds: CreatureSoundData, NPCSounds,
 * WeaponSwingSounds2 and WeaponImpactSounds. Every lookup returns a
 * SoundEntries.dbc id (0 = none), exactly what the original client plays.
 */
class WOWWORLD_API FWowUnitSounds
{
public:
    static void EnsureLoaded(FMpqManager* Mpq);
    static void Reset();
    /** Creature display -> CreatureSoundData row (CreatureDisplayInfo.SoundID, else CreatureModelData.SoundDataID). */
    static uint32 GetCreatureSound(uint32 DisplayId, EWowCreatureSound Kind, int32 Variant = -1);
    /** CreatureSoundData.CreatureImpactType (0-9): the material a weapon hits on this unit. */
    static int32 GetImpactType(uint32 DisplayId);
    static uint32 GetNpcSound(uint32 DisplayId, EWowNpcSound Kind);
    /** SwingType: 0 light (dagger/fist/unarmed), 1 medium (1H), 2 heavy (2H). */
    static uint32 GetWeaponSwing(int32 SwingType, bool bCrit);
    static uint32 GetWeaponImpact(int32 WeaponSubclass, int32 ImpactType, bool bCrit);
    /** Item.dbc subclass helpers for a weapon item entry (-1 = unarmed / unknown). */
    static int32 GetWeaponSubclass(uint32 ItemEntry);
    static int32 GetSwingType(int32 WeaponSubclass);
};
