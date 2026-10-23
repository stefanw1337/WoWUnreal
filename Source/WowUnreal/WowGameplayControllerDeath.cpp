// Death, ghost and resurrection flow. The original FrameXML handles the dialogs (UIParent.lua
// shows StaticPopup DEATH / RESURRECT / XP_LOSS / RECOVER_CORPSE / AREA_SPIRIT_HEAL on these
// events); this keeps FWowPacketHandler::DeathState current and fires the events the stock
// client fires: PLAYER_DEAD, PLAYER_ALIVE (released or resurrected), PLAYER_UNGHOST,
// RESURRECT_REQUEST, CONFIRM_XP_LOSS, CORPSE_IN_RANGE / CORPSE_OUT_OF_RANGE and
// AREA_SPIRIT_HEALER_IN_RANGE / _OUT_OF_RANGE. It also releases the corpse after the 6 minute
// DEATH countdown, as the original client does.
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "WowUpdateFields.h"
#include "WowUIManager.h"
#include "WowEventSystem.h"

namespace
{
constexpr uint32 PLAYER_FLAGS_GHOST = 0x10;
constexpr uint32 UNIT_NPC_FLAG_SPIRITGUIDE = 0x8000;
constexpr uint32 CMSG_REPOP_REQUEST = 0x015A;
constexpr uint32 MSG_CORPSE_QUERY = 0x0216;
constexpr uint32 CMSG_AREA_SPIRIT_HEALER_QUERY = 0x02E2;
constexpr double AutoReleaseSeconds = 360.0;
constexpr float CorpseReclaimRadius = 39.f;   // CORPSE_RECLAIM_RADIUS (yards)
constexpr float SpiritGuideRadius = 20.f;
}

void AWowGameplayController::UpdateDeathState(float DeltaTime)
{
    if (!ConnectionManager || !UIManager) return;
    FWowEventSystem* Events = UIManager->GetEventSystem();
    if (!Events) return;
    FWowPacketHandler& Handler = ConnectionManager->PacketHandler;
    FWowDeathState& Death = Handler.DeathState;

    if (!bDeathHooksBound)
    {
        bDeathHooksBound = true;
        Handler.OnResurrectRequest.AddWeakLambda(this, [this](const FString& Name)
        {
            if (UIManager && UIManager->GetEventSystem())
                UIManager->GetEventSystem()->FireEvent(TEXT("RESURRECT_REQUEST"), { Name });
        });
        Handler.OnSpiritHealerConfirm.AddWeakLambda(this, [this](uint64)
        {
            if (UIManager && UIManager->GetEventSystem())
                UIManager->GetEventSystem()->FireEvent(TEXT("CONFIRM_XP_LOSS"));
        });
    }

    FWowEntity* Me = Handler.EntityManager.Find(Handler.EntityManager.LocalPlayerGuid);
    if (!Me || !Me->IsUnit()) return;
    const bool bGhost = (Me->GetField(PlayerField::FLAGS) & PLAYER_FLAGS_GHOST) != 0;
    const bool bDead = !bGhost && Me->GetHealth() <= 0;
    const double Now = FPlatformTime::Seconds();

    if (bDead && !Death.bDead)
    {
        // The corpse lies where the player died; MSG_CORPSE_QUERY refines it after release.
        Death.DeathTime = Now;
        Death.bReleaseSent = false;
        Death.bHasCorpse = true;
        Death.CorpseMap = Death.CurrentMap;
        Death.CorpsePos = Me->Movement.Position;
        Death.bCorpseInRange = false;
        Events->FireEvent(TEXT("PLAYER_DEAD"));
    }
    if (bGhost && !Death.bGhost)
    {
        Events->FireEvent(TEXT("PLAYER_ALIVE")); // the stock client fires it on release as well
        ConnectionManager->SendRawPacket(MSG_CORPSE_QUERY);
        Death.bCorpseInRange = false;
    }
    if (!bDead && !bGhost && (Death.bDead || Death.bGhost))
    {
        if (Death.bCorpseInRange) Events->FireEvent(TEXT("CORPSE_OUT_OF_RANGE"));
        if (Death.bAreaSpiritHealerInRange) Events->FireEvent(TEXT("AREA_SPIRIT_HEALER_OUT_OF_RANGE"));
        Events->FireEvent(Death.bGhost ? TEXT("PLAYER_UNGHOST") : TEXT("PLAYER_ALIVE"));
        const uint32 Map = Death.CurrentMap;
        Death = FWowDeathState();
        Death.CurrentMap = Map;
        return;
    }
    Death.bDead = bDead;
    Death.bGhost = bGhost;
    if (bGhost) EnsureQuestDiamondLayer(); // r255: the corpse skull in the world

    // DEATH dialog countdown ran out: release like the original client.
    if (bDead && !Death.bReleaseSent && Now - Death.DeathTime >= AutoReleaseSeconds)
    {
        Death.bReleaseSent = true;
        ConnectionManager->SendRawPacket(CMSG_REPOP_REQUEST, TArray<uint8>{0});
    }

    if (!bGhost || Now < NextDeathRangeCheck) return;
    NextDeathRangeCheck = Now + 0.25;

    // Back at the corpse: RECOVER_CORPSE ("Resurrect now?") with the reclaim delay countdown.
    const bool bInRange = Death.bHasCorpse && Death.CorpseMap == Death.CurrentMap
        && FVector::Dist(Me->Movement.Position, Death.CorpsePos) <= CorpseReclaimRadius;
    if (bInRange != Death.bCorpseInRange)
    {
        Death.bCorpseInRange = bInRange;
        Events->FireEvent(bInRange ? TEXT("CORPSE_IN_RANGE") : TEXT("CORPSE_OUT_OF_RANGE"));
    }

    // Battleground spirit guide: ask for the wave timer, open AREA_SPIRIT_HEAL once it is known.
    uint64 NearGuide = 0;
    for (const auto& Pair : Handler.EntityManager.GetAll())
    {
        const FWowEntity* Entity = Pair.Value.Get();
        if (!Entity || !Entity->IsUnit() || !(Entity->GetField(UnitField::NPC_FLAGS) & UNIT_NPC_FLAG_SPIRITGUIDE)) continue;
        if (FVector::Dist(Me->Movement.Position, Entity->Movement.Position) <= SpiritGuideRadius) { NearGuide = Pair.Key; break; }
    }
    if (NearGuide && !Death.bAreaSpiritHealerInRange)
    {
        if (Death.AreaSpiritHealerGuid != NearGuide || Death.AreaSpiritHealerReadyTime <= Now)
        {
            if (Death.AreaSpiritHealerGuid != NearGuide)
            {
                Death.AreaSpiritHealerGuid = NearGuide;
                Death.AreaSpiritHealerReadyTime = 0.0;
                TArray<uint8> Data;
                Data.Append(reinterpret_cast<const uint8*>(&NearGuide), 8);
                ConnectionManager->SendRawPacket(CMSG_AREA_SPIRIT_HEALER_QUERY, Data);
            }
        }
        else
        {
            Death.bAreaSpiritHealerInRange = true;
            Events->FireEvent(TEXT("AREA_SPIRIT_HEALER_IN_RANGE"));
        }
    }
    else if (!NearGuide && Death.bAreaSpiritHealerInRange)
    {
        Death.bAreaSpiritHealerInRange = false;
        Death.AreaSpiritHealerGuid = 0;
        Events->FireEvent(TEXT("AREA_SPIRIT_HEALER_OUT_OF_RANGE"));
    }
}
