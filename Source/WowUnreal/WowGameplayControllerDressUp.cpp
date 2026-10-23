// Dressing room preview (r300, Claude-Perf): DressUpFrame's model, gear try-on and - behind
// Options > Experimental "Mount preview" - mounts. Claude-UI wires DressUpModel's Lua methods,
// the frame's render target and Ctrl-click (DressUpItemLink) to the public API below.
#include "WowGameplayController.h"
#include "WowCharacterPreview.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "WowUpdateFields.h"
#include "WowSessionState.h"
#include "WowWorldManager.h"
#include "WowClientPreferences.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowDressUp, Log, All);

namespace
{
// Slot group an inventory type replaces on the model (robe and chest share a group).
int32 DressUpSlotGroup(uint32 InventoryType)
{
    switch (InventoryType)
    {
    case 1: return 1;            // head
    case 3: return 3;            // shoulders
    case 4: return 4;            // shirt
    case 5: case 20: return 5;   // chest / robe
    case 6: return 6;            // waist
    case 7: return 7;            // legs
    case 8: return 8;            // feet
    case 9: return 9;            // wrists
    case 10: return 10;          // hands
    case 16: return 16;          // back
    case 19: return 19;          // tabard
    case 13: case 17: case 21: case 15: case 25: case 26: return 100; // main hand / ranged in hand
    case 14: case 22: case 23: return 101;                            // off hand
    default: return 0;           // rings, trinkets, neck, bags, ammo: nothing to show
    }
}

// Creature entry a mount item summons: one of its spells applies aura 78 (MOD_MOUNTED),
// whose misc value is the creature entry (3.3.5 mount items teach through 55884 + the spell).
uint32 MountCreatureEntry(const FWowItemTemplateInfo& Item)
{
    for (const uint32 SpellId : Item.SpellIds)
        if (const FSpellDbcEntry* Spell = SpellId ? FDbcStore::Get().Spells().GetById(SpellId) : nullptr)
            for (int32 Effect = 0; Effect < 3; ++Effect)
                if (Spell->EffectApplyAuraName[Effect] == 78 && Spell->EffectMiscValue[Effect] > 0)
                    return uint32(Spell->EffectMiscValue[Effect]);
    return 0;
}
}

bool AWowGameplayController::BuildLiveCharacterInfo(FWowCharacterInfo& Info) const
{
    if (!ConnectionManager) return false;
    const FWowPlayerEntity* Me = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
    if (!Me) return false;
    ConnectionManager->GetSelectedCharacterInfo(Info);
    Info.Race = Me->GetRaceId();
    Info.Gender = Me->GetGenderId();
    Info.Skin = Me->GetFieldByte(PlayerField::BYTES, 0);
    Info.Face = Me->GetFieldByte(PlayerField::BYTES, 1);
    Info.HairStyle = Me->GetFieldByte(PlayerField::BYTES, 2);
    Info.HairColor = Me->GetFieldByte(PlayerField::BYTES, 3);
    Info.FacialHair = Me->GetFieldByte(PlayerField::BYTES_2, 0);
    Info.Equipment.Reset();
    for (uint8 Slot = 0; Slot < 19; ++Slot)
    {
        if (Slot == PlayerField::EQUIPMENT_SLOT_RANGED) continue; // the hands hold the melee weapons
        const uint32 Entry = Me->GetField(PlayerField::VISIBLE_ITEM_BASE + Slot * 2);
        if (!Entry) continue;
        uint32 DisplayId = 0, InventoryType = 0;
        if (const FWowItemTemplateInfo* Template = ConnectionManager->PacketHandler.ItemTemplates.Find(Entry))
        {
            DisplayId = Template->DisplayId;
            InventoryType = Template->InventoryType;
        }
        else if (const FItemDbcEntry* Item = FDbcStore::Get().Items().GetById(Entry))
        {
            DisplayId = Item->DisplayInfoID;
            InventoryType = uint32(Item->InventoryType);
        }
        if (!DisplayId) continue;
        if (Slot == PlayerField::EQUIPMENT_SLOT_OFFHAND && (InventoryType == 13 || InventoryType == 21))
            InventoryType = 22; // a one-hander in the off-hand slot is held in the left hand
        FWowCharacterEquipment& Equipment = Info.Equipment.AddDefaulted_GetRef();
        Equipment.DisplayId = int32(DisplayId);
        Equipment.InventoryType = uint8(InventoryType);
    }
    return true;
}

UTextureRenderTarget2D* AWowGameplayController::EnsureDressUpPreview()
{
    if (!GetWorld()) return nullptr;
    if (!DressUpPreview)
    {
        AWowWorldManager* WorldManager = Cast<AWowWorldManager>(
            UGameplayStatics::GetActorOfClass(GetWorld(), AWowWorldManager::StaticClass()));
        if (!WorldManager || !WorldManager->GetMpqManager() || !WorldManager->GetAssetCache()) return nullptr;
        FActorSpawnParameters Params;
        Params.Name = FName(TEXT("WowDressUpPreview"));
        DressUpPreview = GetWorld()->SpawnActor<AWowCharacterPreview>(AWowCharacterPreview::StaticClass(),
            FVector::ZeroVector, FRotator::ZeroRotator, Params);
        if (!DressUpPreview) return nullptr;
        // Its own spot and light channel: the character sheet's rig must not light this model.
        DressUpPreview->SetRig(FVector(40000.0f, 0.0f, -500000.0f), 2);
        // DressUpModel is about 230 x 320 in the stock layout.
        DressUpPreview->Setup(WorldManager, 512, 712, EWowCharacterPreviewMode::PaperDoll);
        DressUpReset();
    }
    DressUpPreview->SetPreviewActive(true);
    return DressUpPreview->GetRenderTarget();
}

void AWowGameplayController::CloseDressUpPreview()
{
    if (DressUpPreview) DressUpPreview->SetPreviewActive(false);
    DressUpPendingItem = 0;
    DressUpPendingCreature = 0;
}

void AWowGameplayController::DressUpReset()
{
    if (!DressUpPreview) { EnsureDressUpPreview(); return; } // EnsureDressUpPreview resets
    DressUpPendingItem = DressUpPendingCreature = 0;
    if (!DressUpInfo.IsValid()) DressUpInfo = MakeShared<FWowCharacterInfo>();
    if (!BuildLiveCharacterInfo(*DressUpInfo)) { DressUpInfo->Race = 1; DressUpInfo->Gender = 0; }
    bDressUpShowingCreature = false;
    DressUpPreview->ShowCharacter(*DressUpInfo);
}

void AWowGameplayController::DressUpUndress()
{
    if (!EnsureDressUpPreview()) return;
    if (!DressUpInfo.IsValid() || bDressUpShowingCreature) DressUpReset();
    DressUpInfo->Equipment.Reset();
    DressUpPreview->ShowCharacter(*DressUpInfo);
}

void AWowGameplayController::DressUpRotate(float YawDegrees)
{
    if (DressUpPreview) DressUpPreview->RotateCharacter(YawDegrees);
}

void AWowGameplayController::HandleUiWheel(float Delta)
{
    // r262 (Stefan 12.10: the scroll wheel should zoom the dressing room model).
    FWowFrameManager* Frames = UIManager ? UIManager->GetFrameManager() : nullptr;
    if (!Frames || !DressUpPreview) return;
    const int64 Model = Frames->FindFrame(TEXT("DressUpModel"));
    if (Model >= 0 && Frames->IsFrameActuallyVisible(Model) && Frames->IsMouseOverFrame(Model))
        DressUpPreview->AddZoom(Delta);
}

bool AWowGameplayController::DressUpItem(uint32 ItemEntry)
{
    if (!ItemEntry || !ConnectionManager || !EnsureDressUpPreview()) return false;
    FWowPacketHandler& Packets = ConnectionManager->PacketHandler;
    const FWowItemTemplateInfo* Item = Packets.ItemTemplates.Find(ItemEntry);
    if (!Item)
    {
        // Ask the server and finish in UpdateDressUpPreview when the template arrives.
        Packets.RequestItemTemplate(ItemEntry);
        DressUpPendingItem = ItemEntry;
        DressUpPendingUntil = FPlatformTime::Seconds() + 5.0;
        return true;
    }
    DressUpPendingItem = 0;
    if (Item->Class == 15 && Item->SubClass == 5)
    {
        // Mounts are not in the 3.3.5 dressing room: Experimental option (Stefan).
        if (!FWowClientPreferences::GetWorldOption(TEXT("mountpreview"))) return false;
        const uint32 Creature = MountCreatureEntry(*Item);
        if (!Creature) return false;
        const auto* Template = Packets.CreatureTemplateCache.Find(Creature);
        uint32 Display = 0;
        if (Template) for (const uint32 Id : Template->DisplayIds) if (Id) { Display = Id; break; }
        if (!Display)
        {
            ConnectionManager->SendCreatureQuery(int32(Creature), 0);
            DressUpPendingCreature = Creature;
            DressUpPendingUntil = FPlatformTime::Seconds() + 5.0;
            return true;
        }
        DressUpPendingCreature = 0;
        bDressUpShowingCreature = true;
        DressUpPreview->ShowCreatureDisplay(Display);
        return true;
    }
    const int32 Group = DressUpSlotGroup(Item->InventoryType);
    if (!Group || !Item->DisplayId) return false;
    if (!DressUpInfo.IsValid() || bDressUpShowingCreature) DressUpReset();
    TArray<FWowCharacterEquipment>& Equipment = DressUpInfo->Equipment;
    const bool bTwoHand = Item->InventoryType == 17;
    Equipment.RemoveAll([Group, bTwoHand](const FWowCharacterEquipment& Worn)
    {
        const int32 WornGroup = DressUpSlotGroup(Worn.InventoryType);
        // A two-hander frees the off hand; an off-hand item clears a worn two-hander.
        return WornGroup == Group || (bTwoHand && WornGroup == 101)
            || (Group == 101 && Worn.InventoryType == 17);
    });
    FWowCharacterEquipment& New = Equipment.AddDefaulted_GetRef();
    New.DisplayId = int32(Item->DisplayId);
    New.InventoryType = uint8(Item->InventoryType);
    DressUpPreview->ShowCharacter(*DressUpInfo);
    return true;
}

void AWowGameplayController::UpdateDressUpPreview()
{
    if (!DressUpPendingItem && !DressUpPendingCreature) return;
    if (FPlatformTime::Seconds() > DressUpPendingUntil || !ConnectionManager)
    {
        UE_LOG(LogWowDressUp, Warning, TEXT("Dressing room: no answer for item %u / creature %u"), DressUpPendingItem, DressUpPendingCreature);
        DressUpPendingItem = DressUpPendingCreature = 0;
        return;
    }
    const FWowPacketHandler& Packets = ConnectionManager->PacketHandler;
    if (DressUpPendingItem && Packets.ItemTemplates.Contains(DressUpPendingItem))
    {
        const uint32 Item = DressUpPendingItem;
        DressUpPendingItem = 0;
        DressUpItem(Item);
    }
    else if (DressUpPendingCreature)
        if (const auto* Template = Packets.CreatureTemplateCache.Find(DressUpPendingCreature))
            for (const uint32 Id : Template->DisplayIds)
                if (Id)
                {
                    DressUpPendingCreature = 0;
                    bDressUpShowingCreature = true;
                    if (DressUpPreview) DressUpPreview->ShowCreatureDisplay(Id);
                    break;
                }
}
