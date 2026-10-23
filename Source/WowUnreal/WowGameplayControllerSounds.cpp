// Unit and server-driven sounds, wired like the original 3.3.5a client:
// creature aggro/attack/wound/death (CreatureSoundData), NPC greetings (NPCSounds),
// weapon swing + impact (WeaponSwingSounds2 / WeaponImpactSounds), and scripted
// boss/NPC lines and music the server sends (SMSG_PLAY_SOUND/OBJECT_SOUND/MUSIC).
// Kept out of WowGameplayController.cpp on purpose.
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowAudioManager.h"
#include "WowUnitSounds.h"
#include "WowUpdateFields.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "WowEntity.h"
#include "WowWaterMaterial.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowUnitAudio, Log, All);

namespace
{
// UNIT_VIRTUAL_ITEM_SLOT_ID (3.3.5a): a creature's three displayed weapons (item entries).
constexpr uint16 UnitVirtualItemSlot = UnitField::BASE + 0x0032;
constexpr float UnitSoundRange = 6500.0f;   // beyond this the 45 m falloff makes them inaudible anyway
constexpr double WoundCooldown = 0.30;
constexpr double AggroCooldown = 12.0;
// Other players' attack and wound grunts (r244): only the local player had a voice in combat.
// Per player, like the local voice budget, so a group fight is not a wall of grunts.
constexpr double RemoteVoiceCooldown = 2.2;
TMap<uint64, double> GRemoteVoiceTimes;

void PlayRemotePlayerVoice(AWowAudioManager* Audio, const FWowEntity* Player, const TCHAR* Cue, const FVector& Location)
{
    if (!Audio || !Player) return;
    const double Now = FPlatformTime::Seconds();
    if (const double* Last = GRemoteVoiceTimes.Find(Player->Guid); Last && Now - *Last < RemoteVoiceCooldown) return;
    if (FMath::FRand() > 0.6f) return; // not on every swing, as the original client
    const uint32 Id = AWowAudioManager::ResolveCharacterCue(Player->GetFieldByte(UnitField::BYTES_0, 0),
        Player->GetFieldByte(UnitField::BYTES_0, 2), Cue);
    if (!Id) return;
    if (GRemoteVoiceTimes.Num() > 256) GRemoteVoiceTimes.Reset();
    GRemoteVoiceTimes.Add(Player->Guid, Now);
    Audio->PlayWorldSoundEffect(Id, Location, true);
}

AWowAudioManager* FindAudio(const UObject* Context)
{
    return Context ? Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(Context, AWowAudioManager::StaticClass())) : nullptr;
}
}

void AWowGameplayController::BindUnitSounds()
{
    if (!ConnectionManager) return;
    ConnectionManager->PacketHandler.OnPlayServerSound.AddUObject(this, &AWowGameplayController::OnServerSound);
    ConnectionManager->PacketHandler.OnPlayServerMusic.AddUObject(this, &AWowGameplayController::OnServerMusic);
    ConnectionManager->PacketHandler.OnAIReaction.AddUObject(this, &AWowGameplayController::OnAIReaction);
    // A fish bites (r253, Stefan): the bobber splashes and dips, as in the original client.
    ConnectionManager->PacketHandler.OnGameObjectCustomAnim.AddWeakLambda(this, [this](uint64 Guid, uint32 Anim)
    {
        const TObjectPtr<AActor>* Found = SpawnedEntityActors.Find(Guid);
        AActor* Bobber = Found ? Found->Get() : nullptr;
        if (!Bobber || !ConnectionManager) return;
        const FWowEntity* Object = ConnectionManager->PacketHandler.EntityManager.Find(Guid);
        if (!Object || !Object->IsGameObject()) return;
        if (static_cast<const FWowGameObjectEntity*>(Object)->GetGameObjectType() != 17) return; // FISHINGNODE
        if (AWowAudioManager* Audio = FindAudio(this)) Audio->PlayWorldSoundEffect(3355, Bobber->GetActorLocation()); // Fishing Hooked
        // Splash and a hard shake for about 1.3 s (r265, Stefan: the bite was barely visible; the
        // original plays the bobber's custom sequence: it jerks under and rocks).
        const FVector Rest = Bobber->GetActorLocation();
        const FRotator RestRotation = Bobber->GetActorRotation();
        if (GetWorld())
        {
            FWowWaterMaterial::AddSurfaceRipple(Rest, FVector::UpVector, true, GetWorld()->GetTimeSeconds());
            FWowWaterMaterial::AddSurfaceRipple(Rest + FVector(6, -4, 0), FVector::UpVector, true, GetWorld()->GetTimeSeconds());
        }
        TWeakObjectPtr<AActor> WeakBobber(Bobber);
        TSharedRef<int32> Step = MakeShared<int32>(0);
        TSharedRef<FTimerHandle> Shake = MakeShared<FTimerHandle>();
        GetWorldTimerManager().SetTimer(*Shake, FTimerDelegate::CreateWeakLambda(this, [this, WeakBobber, Rest, RestRotation, Step, Shake]()
        {
            AActor* Actor = WeakBobber.Get();
            const int32 Frame = (*Step)++;
            constexpr int32 Frames = 26; // 26 x 50 ms
            if (!Actor || Frame >= Frames)
            {
                if (Actor) Actor->SetActorLocationAndRotation(Rest, RestRotation);
                GetWorldTimerManager().ClearTimer(*Shake);
                return;
            }
            const float T = float(Frame) / Frames;
            const float Decay = 1.0f - T;
            const float Sink = (Frame < 3 ? 10.0f * (Frame + 1) / 3.0f : 10.0f * Decay * FMath::Abs(FMath::Sin(T * 18.0f)));
            const float Tilt = 14.0f * Decay * FMath::Sin(T * 23.0f);
            Actor->SetActorLocationAndRotation(Rest - FVector(0, 0, Sink),
                RestRotation + FRotator(Tilt, 0.0f, Tilt * 0.6f));
        }), 0.05f, true);
    });
}

bool AWowGameplayController::GetUnitSoundLocation(uint64 Guid, FVector& OutLocation) const
{
    if (!ConnectionManager || Guid == 0) return false;
    if (Guid == ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid && GetPawn())
    {
        OutLocation = GetPawn()->GetActorLocation();
        return true;
    }
    const TObjectPtr<AActor>* Actor = SpawnedEntityActors.Find(Guid);
    if (!Actor || !*Actor) return false;
    OutLocation = (*Actor)->GetActorLocation();
    if (const APawn* Listener = GetPawn())
        if (FVector::Dist(Listener->GetActorLocation(), OutLocation) > UnitSoundRange) return false;
    return true;
}

void AWowGameplayController::OnServerSound(uint32 SoundId, uint64 SourceGuid)
{
    AWowAudioManager* Audio = FindAudio(this);
    if (!Audio || SoundId == 0) return;
    FVector Location;
    // Positional when the source is known and near (boss yells carry far: play them 2D otherwise).
    if (SourceGuid && GetUnitSoundLocation(SourceGuid, Location)) Audio->PlayWorldSoundEffect(SoundId, Location);
    else Audio->PlaySoundEffect(SoundId);
}

void AWowGameplayController::OnServerMusic(uint32 SoundId)
{
    if (AWowAudioManager* Audio = FindAudio(this)) if (SoundId) Audio->PlaySoundEffect(SoundId);
}

void AWowGameplayController::OnAIReaction(uint64 Guid, uint32 Reaction)
{
    if (Reaction != 2 || !ConnectionManager) return; // AI_REACTION_HOSTILE
    const FWowEntity* Unit = ConnectionManager->PacketHandler.EntityManager.Find(Guid);
    FVector Location;
    if (!Unit || Unit->IsPlayer() || !GetUnitSoundLocation(Guid, Location)) return;
    const double Now = FPlatformTime::Seconds();
    if (const double* Last = UnitAggroSoundTimes.Find(Guid); Last && Now - *Last < AggroCooldown) return;
    UnitAggroSoundTimes.Add(Guid, Now);
    if (AWowAudioManager* Audio = FindAudio(this)) Audio->PlayCreatureSound(Unit->GetDisplayId(), EWowCreatureSound::Aggro, Location);
}

void AWowGameplayController::PlayNpcClickSound(uint64 NpcGuid)
{
    if (!ConnectionManager) return;
    const FWowEntity* Npc = ConnectionManager->PacketHandler.EntityManager.Find(NpcGuid);
    FVector Location;
    if (!Npc || Npc->IsPlayer() || !GetUnitSoundLocation(NpcGuid, Location)) return;
    // Greeting on the first click; clicking the same NPC over and over annoys it.
    const double Now = FPlatformTime::Seconds();
    TPair<double, int32>& Clicks = NpcClickCounts.FindOrAdd(NpcGuid);
    Clicks.Value = Now - Clicks.Key < 8.0 ? Clicks.Value + 1 : 1;
    Clicks.Key = Now;
    if (Clicks.Value > 1 && Clicks.Value < 5) return; // don't repeat the greeting on every click
    if (AWowAudioManager* Audio = FindAudio(this))
        Audio->PlayNpcSound(Npc->GetDisplayId(), Clicks.Value >= 5 ? EWowNpcSound::Pissed : EWowNpcSound::Greeting, Location);
    if (Clicks.Value >= 5) Clicks.Value = 1;
}

void AWowGameplayController::PlayCombatLogSounds(const FWowCombatLogEntry& Entry)
{
    if (!ConnectionManager) return;
    const bool bSwing = Entry.Event == TEXT("SWING_DAMAGE");
    const bool bDirectHit = bSwing || Entry.Event == TEXT("SPELL_DAMAGE") || Entry.Event == TEXT("RANGE_DAMAGE");
    const bool bDied = Entry.Event == TEXT("UNIT_DIED");
    if (!bDirectHit && !bDied) return;
    AWowAudioManager* Audio = FindAudio(this);
    if (!Audio) return;
    auto& Entities = ConnectionManager->PacketHandler.EntityManager;
    const FWowEntity* Victim = Entities.Find(Entry.DestGuid);
    FVector VictimLocation;
    const bool bVictimAudible = Victim && GetUnitSoundLocation(Entry.DestGuid, VictimLocation);
    const bool bVictimCreature = Victim && Victim->IsUnit() && !Victim->IsPlayer();

    if (bDied)
    {
        if (bVictimAudible && bVictimCreature)
            Audio->PlayCreatureSound(Victim->GetDisplayId(), EWowCreatureSound::Death, VictimLocation);
        UnitSoundCooldowns.Remove(Entry.DestGuid);
        UnitAggroSoundTimes.Remove(Entry.DestGuid);
        return;
    }
    if (Entry.Amount <= 0) return;
    const double Now = FPlatformTime::Seconds();

    if (bSwing)
    {
        const FWowEntity* Attacker = Entities.Find(Entry.SourceGuid);
        FVector AttackerLocation;
        if (Attacker && GetUnitSoundLocation(Entry.SourceGuid, AttackerLocation))
        {
            const uint64 LocalGuid = Entities.LocalPlayerGuid;
            if (Attacker->IsPlayer() && Entry.SourceGuid != LocalGuid)
                PlayRemotePlayerVoice(Audio, Attacker, TEXT("PlayerAttack"), AttackerLocation);
            if (bVictimAudible && Victim->IsPlayer() && Entry.DestGuid != LocalGuid)
                PlayRemotePlayerVoice(Audio, Victim, Entry.bCritical ? TEXT("PlayerWoundCrit") : TEXT("PlayerWound"), VictimLocation);
            int32 WeaponSubclass = -1;
            if (Attacker->IsPlayer())
                WeaponSubclass = FWowUnitSounds::GetWeaponSubclass(
                    Attacker->GetField(PlayerField::VISIBLE_ITEM_BASE + PlayerField::EQUIPMENT_SLOT_MAINHAND * 2));
            else
            {
                WeaponSubclass = FWowUnitSounds::GetWeaponSubclass(Attacker->GetField(UnitVirtualItemSlot));
                // Creature attack grunt (exertion); a creature that hasn't growled yet does so now.
                if (!UnitAggroSoundTimes.Contains(Entry.SourceGuid))
                {
                    UnitAggroSoundTimes.Add(Entry.SourceGuid, Now);
                    Audio->PlayCreatureSound(Attacker->GetDisplayId(), EWowCreatureSound::Aggro, AttackerLocation);
                }
                else if (!Audio->PlayCreatureSound(Attacker->GetDisplayId(),
                    Entry.bCritical ? EWowCreatureSound::ExertionCritical : EWowCreatureSound::Exertion, AttackerLocation))
                    Audio->PlayCreatureSound(Attacker->GetDisplayId(), EWowCreatureSound::CustomAttack, AttackerLocation);
            }
            if (bVictimAudible)
            {
                const int32 ImpactType = bVictimCreature ? FWowUnitSounds::GetImpactType(Victim->GetDisplayId()) : 0;
                // Armed creatures and players get the weapon swing + impact; beasts only the impact.
                Audio->PlayWeaponHit(WeaponSubclass, ImpactType, Entry.bCritical,
                    Attacker->IsPlayer() || WeaponSubclass >= 0, VictimLocation);
            }
        }
    }

    // Wounded creatures cry out (players use the race voice cues elsewhere).
    if (bVictimAudible && bVictimCreature)
    {
        if (const double* Last = UnitSoundCooldowns.Find(Entry.DestGuid); Last && Now - *Last < WoundCooldown) return;
        UnitSoundCooldowns.Add(Entry.DestGuid, Now);
        const EWowCreatureSound Kind = Entry.bCrushing ? EWowCreatureSound::InjuryCrushing
            : Entry.bCritical ? EWowCreatureSound::InjuryCritical : EWowCreatureSound::Injury;
        Audio->PlayCreatureSound(Victim->GetDisplayId(), Kind, VictimLocation);
    }
}
