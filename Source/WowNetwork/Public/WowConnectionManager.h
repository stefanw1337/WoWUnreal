#pragma once
#include "CoreMinimal.h"
#include "WowSessionState.h"
#include "WowPacketHandler.h"
#include "WowMovementPacket.h"
#include "WowConnectionManager.generated.h"

class FWowAuthSocket;
class FWowWorldSocket;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSessionStateChanged, EWowSessionState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRealmList, const TArray<FWowRealmInfo>&, Realms);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharacterList, const TArray<FWowCharacterInfo>&, Characters);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWowError, const FString&, Msg);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharCreateResult, uint8, ResultCode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLogoutComplete);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnItemConfirmation, const FString&, const TArray<FString>&);

enum class EWowActionInvocationKind : uint8
{
    None,
    SpellCast,
    AutoAttack,
    ItemUse,
    Macro,
};

struct FWowActionInvocation
{
    EWowActionInvocationKind Kind = EWowActionInvocationKind::None;
    uint32 ActionId = 0;
    int64 TargetGuid = 0;

    bool IsValid() const
    {
        return Kind != EWowActionInvocationKind::None && ActionId != 0;
    }
};

UCLASS(BlueprintType)
class WOWNETWORK_API UWowConnectionManager : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable) void Login(const FString& User, const FString& Pass, const FString& Server, int32 Port = 3724);
    UFUNCTION(BlueprintCallable) void SelectRealm(int32 Index);
    FString GetBindingCharacterIdentity() const;
    UFUNCTION(BlueprintCallable) void RequestCharacterList();
    UFUNCTION(BlueprintCallable) void EnterWorld(int64 Guid);
    UFUNCTION(BlueprintCallable) void CreateCharacter(const FString& Name, uint8 Race, uint8 Class, uint8 Gender, uint8 Skin = 0, uint8 Face = 0, uint8 HairStyle = 0, uint8 HairColor = 0, uint8 FacialHair = 0);
    UFUNCTION(BlueprintCallable) void DeleteCharacter(int64 Guid);
    UFUNCTION(BlueprintCallable) void RequestLogout();
    UFUNCTION(BlueprintCallable) void CancelLogout();
    UFUNCTION(BlueprintCallable) void Disconnect();
    UFUNCTION(BlueprintCallable) EWowSessionState GetState() const { return State; }

    /** Send a movement packet to the server */
    UFUNCTION(BlueprintCallable) void SendMovement(int32 Opcode, const FVector& Position, float Orientation, int32 MoveFlags);

    /** Send a complete MovementInfo; optional blocks are written exactly as the flags demand. */
    void SendMovementInfo(uint16 Opcode, FWowOutgoingMovementInfo Info);

    /** Acknowledge a server-forced movement change (speed, root, knockback, hover...). */
    void SendMovementAck(uint16 AckOpcode, uint32 Counter, FWowOutgoingMovementInfo Info,
        bool bAppendFloat = false, float FloatValue = 0.0f, bool bAppendU32 = false, uint32 U32Value = 0);

    /** Send a chat message */
    UFUNCTION(BlueprintCallable) void SendChatMessage(const FString& Message, int32 Type = 1 /*SAY*/, const FString& Target = TEXT(""), int32 Language = 7 /*Common*/);

    /** Racial default chat language: Common (7) for Alliance, Orcish (1) for Horde. */
    uint32 GetDefaultChatLanguage();

    /** Addon channel (LANG_ADDON): "prefix<TAB>text" on PARTY/RAID/GUILD/BATTLEGROUND/WHISPER. */
    void SendAddonMessage(const FString& Prefix, const FString& Text, int32 Type, const FString& Target = TEXT(""));

    /** Send keep-alive heartbeat */
    UFUNCTION(BlueprintCallable) void SendKeepAlive();

    /** Set target selection */
    UFUNCTION(BlueprintCallable) void SendSetSelection(int64 TargetGuid);

    /** Cast a spell on the current target (or self if no target) */
    void RequestTrainer(uint64 Guid);
    void BuyTrainerSpell(uint32 Spell);
    void SearchAuctions(const FString& Name,uint32 Offset=0,bool Own=false);
    void BidAuction(uint32 Id,uint32 Price);
    uint64 GetAuctionCursorGuid() const {return CursorPayload.ItemGuid;}
    bool SellAuctionCursor(uint32 Bid,uint32 Buyout);
    void CancelAuction(uint32 Id);
    /** CMSG_AUCTION_LIST_ITEMS with the Browse filters (0 levels / 0xFFFFFFFF ids = any). */
    void SearchAuctionsFiltered(const FString& Name,uint32 Offset,uint8 MinLevel,uint8 MaxLevel,uint32 InventoryType,uint32 ItemClass,uint32 SubClass,uint32 Quality,bool bUsable);
    void ListOwnerAuctions(uint32 Offset=0);
    void ListBidderAuctions(uint32 Offset=0);
    /** CMSG_AUCTION_SELL_ITEM for a bag item: the whole stack, duration in minutes (720/1440/2880). */
    bool SellAuctionItem(uint64 ItemGuid,uint32 Bid,uint32 Buyout,uint32 DurationMinutes);
    UFUNCTION(BlueprintCallable) void SendCastSpell(int32 SpellId, int64 TargetGuid = 0);
    uint32 PendingTargetSpell = 0;
    int64 ResolveHelpfulSpellTarget(uint32 SpellId,int64 SelectedGuid);
    bool CompleteSpellTarget(uint64 Guid);
    void CancelSpellTarget() { PendingTargetSpell=0; PendingEnchantItemGuid=0; }
    bool UseWeaponEnchantItem(uint8 Bag,uint8 Slot);
    /** Right-click "Use" of a bag item: on-use/learn spell (CMSG_USE_ITEM) or open a lootable
     *  container (CMSG_OPEN_ITEM). Returns false when the item has nothing to use. */
    bool UseBagItem(uint8 NetworkBag, uint8 NetworkSlot);
    /** True when the item template can go into an equipment or bag slot. */
    bool IsBagItemEquippable(uint8 NetworkBag, uint8 NetworkSlot);
    /** Item actions (action bar type 0x80): lookups by item entry across equipment, backpack and bags. */
    bool FindPlayerItemByEntry(uint32 Entry, uint8& OutNetworkBag, uint8& OutNetworkSlot, bool bIncludeEquipped = true);
    int32 CountPlayerItems(uint32 Entry);
    bool IsItemEntryEquipped(uint32 Entry);
    /** First on-use/learn spell of an item template, 0 if none or not loaded. */
    uint32 GetItemUseSpell(uint32 Entry);
    /** True when an item can go on the action bar (use spell, openable or equippable). */
    bool IsItemActionable(uint32 Entry);
    /** Use (or equip) the first carried copy of an item; equipped copies are used first. */
    bool UseItemByEntry(uint32 Entry);
    uint64 PendingEnchantItemGuid=0;
    uint8 PendingEnchantBag=255,PendingEnchantSlot=0;

    /** Request interruption; server events remain authoritative for cast state. */
    void SendCancelCast(uint32 SpellId, bool bChanneling);

    /** Assign or clear an action bar slot */
    void SendSetActionButton(int32 SlotIndex, uint32 ActionId, uint8 ActionType = 0);
    void SendClearActionButton(int32 SlotIndex);

    /** Cursor payload helpers for spell/action pickup flows */
    void PickupSpellCursor(int32 SpellId, const FString& BookType = TEXT("spell"));
    void PickupActionCursor(int32 SlotIndex);
    /** A macro (MacroFrame) on the cursor: placed on an action button as type 64 with the macro index. */
    void PickupMacroCursor(int32 MacroIndex);
    void PickupBagItemCursor(uint8 SourceBag, uint8 SourceSlot, int32 ItemId, const FString& ItemLink = TEXT(""), uint32 SplitCount = 0);
    void PickupInventoryItemCursor(uint8 InventorySlot, int32 ItemId, const FString& ItemLink = TEXT(""));
    void ClearCursorPayload();
    /** An action picked up from a bar and dropped outside any slot: removes it from its bar (no confirm, like the client). */
    bool DropCursorActionOutside();
    bool HasCursorPayload() const;
    bool HasCursorSpellPayload() const;
    bool HasCursorItemPayload() const;
    bool GetCursorInfo(FString& OutType, int32& OutId, FString& OutDetail) const;
    bool PlaceCursorIntoActionSlot(int32 SlotIndex);
    bool PlaceCursorIntoContainerSlot(uint8 DestinationBag, uint8 DestinationSlot);
    bool PlaceCursorIntoFirstFreeContainerSlot(uint8 DestinationBag);
    bool AutoEquipCursorItem();
    bool AutoStoreCursorItem(uint8 DestinationBag = 255);
    bool DeleteCursorItem();
    bool RequestDeleteCursorItem();
    bool EquipCursorIntoSlot(uint8 EquipmentSlot);
    bool RequestEquipItem(uint8 NetworkBag, uint8 NetworkSlot, int32 EquipmentSlot = -1);
    void ConfirmPendingEquip(uint32 Token);
    void CancelPendingEquip(uint32 Token);
    FOnItemConfirmation OnItemConfirmation;
    void SendBuybackItem(uint64 VendorGuid, uint32 Slot, uint64 ExpectedItemGuid);
    /** CMSG_REPAIR_ITEM: one item, or every item when ItemGuid is 0 (merchant Repair All). */
    void SendRepairItem(uint64 VendorGuid, uint64 ItemGuid, bool bGuildBank = false);
    uint64 GetItemGuidAtPosition(uint8 NetworkBag, uint8 NetworkSlot);
    // Container ids as the cursor code keeps them: 255 = backpack, 1-4 = bags, r282: 254 = the bank's
    // 28 slots (Lua BANK_CONTAINER -1, player slots 39-66), 5-11 = bank bags (player slots 67-73).
    static uint8 NetworkBag(uint8 Bag) { return Bag >= 254 ? 255 : (Bag >= 5 && Bag <= 11 ? 62 + Bag : 18 + Bag); }
    static uint8 NetworkSlot(uint8 Bag, uint8 Slot) { return Bag == 255 ? 23 + Slot : (Bag == 254 ? 39 + Slot : Slot); }
    /** r282: the banker whose bank is open (0 = closed); set by BANKFRAME_OPENED, cleared by CloseBankFrame. */
    uint64 ActiveBankerGuid = 0;
    /** r282: CMSG_AUTOBANK_ITEM (bag item -> bank) / CMSG_AUTOSTORE_BANK_ITEM (bank item -> bags). */
    void SendAutoBankItem(uint8 NetworkBag, uint8 NetworkSlot);
    void SendAutoStoreBankItem(uint8 NetworkBag, uint8 NetworkSlot);
    /** r282: CMSG_BUY_BANK_SLOT at the open banker. */
    void SendBuyBankSlot();
    /** r285: the guild vault (gameobject type 34) whose bank is open, 0 = closed; tab shown in Lua (1-based). */
    uint64 ActiveGuildBankGuid = 0;
    int32 GuildBankCurrentTab = 1;
    /** r285: permissions + withdraw money first (MSG_GUILD_PERMISSIONS unsubscribes from updates), then CMSG_GUILD_BANKER_ACTIVATE. */
    void OpenGuildBank(uint64 VaultGuid);
    void SendGuildBankQueryTab(uint8 Tab, bool bFull);
    void SendGuildBankSwapBankOnly(uint8 DestTab, uint8 DestSlot, uint8 SourceTab, uint8 SourceSlot, uint32 SourceEntry, int32 SplitCount);
    void SendGuildBankSwapWithInventory(bool bToCharacter, uint8 Tab, uint8 Slot, uint32 Entry, uint8 NetworkBag, uint8 NetworkSlot, int32 SplitCount, bool bAutoStore);
    void SendGuildBankBuyTab(uint8 Tab);
    void SendGuildBankUpdateTab(uint8 Tab, const FString& Name, const FString& Icon);
    void SendGuildBankDepositMoney(uint32 Copper);
    void SendGuildBankWithdrawMoney(uint32 Copper);
    void SendGuildBankLogQuery(uint8 Tab);
    void SendGuildBankTextQuery(uint8 Tab);
    void SendGuildBankSetText(uint8 Tab, const FString& Text);
    void SendGuildPermissionsQuery();
    void SendGuildBankMoneyWithdrawnQuery();
    /** r285: an item of the guild bank on the cursor (container id 253; dropped on a bag slot or another bank slot). */
    void PickupGuildBankItemCursor(uint8 Tab, uint8 Slot, uint32 Entry, const FString& ItemLink, uint32 SplitCount);
    bool PlaceCursorIntoGuildBankSlot(uint8 Tab, uint8 Slot);
    void SendSwapItem(uint8 SourceBag, uint8 SourceSlot, uint8 DestinationBag, uint8 DestinationSlot);

    /** Resolve the action assigned to a slot into a concrete gameplay invocation. */
    static FWowActionInvocation ResolveActionInvocation(uint32 PackedAction, int64 TargetGuid);

    /** Use the action assigned to a slot, matching Blizzard action-button behavior. */
    bool UseActionSlot(int32 SlotIndex);

    /** Start melee attack on target */
    UFUNCTION(BlueprintCallable) void SendAttackSwing(int64 TargetGuid);

    /** Stop melee attack */
    UFUNCTION(BlueprintCallable) void SendAttackStop();

    /** Change the local player's server-authoritative stand state. */
    void SendStandStateChange(uint32 StandState);

    /** Change the local player's server-authoritative weapon sheath state. */
    void SendSetSheathed(uint32 SheathState);

    /** Send loot request (right-click corpse to open loot window) */
    UFUNCTION(BlueprintCallable) void SendLoot(int64 CorpseGuid);

    /** Send loot release (close loot window) */
    UFUNCTION(BlueprintCallable) void SendLootRelease(int64 CorpseGuid);

    /** Send loot item command */
    UFUNCTION(BlueprintCallable) void SendLootItem(int32 LootSlot);

    /** Send buy item from vendor command */
    UFUNCTION(BlueprintCallable) void SendBuyItem(int64 VendorGuid, int32 ItemId, int32 Count = 1, int32 VendorSlot = 0);

    /** Request the inventory sold by a vendor NPC. */
    UFUNCTION(BlueprintCallable) void SendListInventory(int64 VendorGuid);
    void RequestItemInfo(uint32 ItemId);

    /** Send sell item to vendor command */
    UFUNCTION(BlueprintCallable) void SendSellItem(int64 VendorGuid, int64 ItemGuid, uint8 Count = 1);

    /** Auto-equip an inventory item from the specified bag/slot. */
    void SendAutoEquipItem(uint8 SourceBag, uint8 SourceSlot);

    /** Auto-store an equipped or bag item into the requested destination bag. */
    void SendAutoStoreBagItem(uint8 SourceBag, uint8 SourceSlot, uint8 DestinationBag = 255);

    /** Split an item stack into a specific destination slot. */
    void SendSplitItem(uint8 SourceBag, uint8 SourceSlot, uint8 DestinationBag, uint8 DestinationSlot, uint32 Count);

    /** Destroy an item or reduce a stack count. */
    void SendDestroyItem(uint8 Bag, uint8 Slot, uint8 Count = 0);

    /** Start a player trade with the target player */
    UFUNCTION(BlueprintCallable) void SendInitiateTrade(int64 TargetGuid);

    /** Accept an incoming trade request and open the trade window */
    UFUNCTION(BlueprintCallable) void SendBeginTrade();

    /** Accept the current trade contents */
    UFUNCTION(BlueprintCallable) void SendAcceptTrade();

    /** Revoke local trade acceptance */
    UFUNCTION(BlueprintCallable) void SendUnacceptTrade();

    /** Cancel the current trade */
    UFUNCTION(BlueprintCallable) void SendCancelTrade();

    /** Set the amount of gold offered in the current trade */
    UFUNCTION(BlueprintCallable) void SendSetTradeGold(int32 CopperAmount);

    /** Accept the currently pending duel request */
    void SendAcceptDuel(int64 ArbiterGuid);

    /** Decline a pending duel request or forfeit an active duel */
    void SendCancelDuel(int64 ArbiterGuid);

    /** Request the current pet action bar state from the server */
    void SendRequestPetInfo();

    /** Trigger the pet action currently assigned to a pet action bar slot */
    void SendPetActionBarSlot(int32 SlotIndex, int64 TargetGuid = 0);

    /** Toggle pet spell autocast for the action currently assigned to a slot */
    void SendPetSpellAutocast(int32 SlotIndex, bool bEnabled);

    /** Request the current raid target icon assignments */
    void SendRequestRaidTargetIcons();

    /** Assign a raid target icon to the specified guid (or clear by passing 0) */
    void SendSetRaidTargetIcon(int32 IconIndex, int64 InTargetGuid);

    /** Start a ready check if the local player can issue one */
    void SendStartReadyCheck();

    /** Send a ready-check response */
    void SendReadyCheckConfirm(bool bReady);

    /** Finish the active ready check */
    void SendFinishReadyCheck();

    /** Send gossip/questgiver hello to initiate NPC interaction */
    UFUNCTION(BlueprintCallable) void SendGossipHello(int64 NpcGuid);

    /** Select an entry from the active NPC gossip menu. */
    UFUNCTION(BlueprintCallable) void SendGossipSelectOption(int64 NpcGuid, int32 MenuId, int32 OptionId, const FString& Code = TEXT(""));

    /** Query the marker state for one visible quest giver. */
    UFUNCTION(BlueprintCallable) void SendQuestStatusQuery(int64 QuestGiverGuid);

    /** Refresh marker states for every visible quest giver. */
    UFUNCTION(BlueprintCallable) void SendQuestStatusMultipleQuery();

    /** Use an interactable gameobject such as a mailbox */
    uint64 PendingObjectUse = 0;
    double PendingObjectUseAt = 0;
    UFUNCTION(BlueprintCallable) void SendGameObjectUse(int64 GameObjectGuid);

    /** Send banker activate to open the bank window */
    UFUNCTION(BlueprintCallable) void SendBankerActivate(int64 BankerGuid);

    /** Request mailbox contents from a mailbox gameobject or mailbox NPC */
    UFUNCTION(BlueprintCallable) void SendGetMailList(int64 MailboxGuid);
    /** Mail actions (TrinityCore 3.3.5 MailHandler formats). */
    void SendMailTakeMoney(uint64 MailboxGuid, uint32 MailId);
    void SendMailTakeItem(uint64 MailboxGuid, uint32 MailId, uint32 ItemGuidLow);
    void SendMailMarkAsRead(uint64 MailboxGuid, uint32 MailId);
    void SendMailReturnToSender(uint64 MailboxGuid, uint32 MailId, uint64 SenderGuid);
    void SendMailDelete(uint64 MailboxGuid, uint32 MailId, uint32 MailTemplateId);
    void SendMail(uint64 MailboxGuid, const FString& Receiver, const FString& Subject, const FString& Body,
        const TArray<uint64>& ItemGuids, uint32 Money, uint32 COD);
    /** Item GUID held on the cursor, 0 when the cursor holds no item. */
    uint64 GetCursorItemGuid() const;
    /** Right-click on a bag item is offered here first (Send Mail attaches it); return true to consume. */
    TFunction<bool(uint64 /*ItemGuid*/)> ContainerItemUseOverride;

    /** Send quest accept command */
    UFUNCTION(BlueprintCallable) void SendQuestAccept(int64 QuestGiverGuid, int32 QuestId);

    /** Request the detail/progress page for a quest selected in an NPC menu */
    UFUNCTION(BlueprintCallable) void SendQuestQuery(int64 QuestGiverGuid, int32 QuestId);

    /** Request static quest log data such as title, level, zone, and objectives. */
    UFUNCTION(BlueprintCallable) void SendQuestTemplateQuery(int32 QuestId);

    /** Open the progress page for an accepted quest. */
    UFUNCTION(BlueprintCallable) void SendQuestComplete(int64 QuestGiverGuid, int32 QuestId);

    /** Ask the quest giver to advance a completed quest to its reward page */
    UFUNCTION(BlueprintCallable) void SendQuestRequestReward(int64 QuestGiverGuid, int32 QuestId);

    /** Send quest complete/choose reward command */
    UFUNCTION(BlueprintCallable) void SendQuestChooseReward(int64 QuestGiverGuid, int32 QuestId, int32 RewardChoice = 0);

    /** Get current target GUID */
    UFUNCTION(BlueprintCallable) int64 GetTargetGuid() const { return TargetGuid; }

    /** Account name typed at login (empty offline). */
    FString GetAccountName() const { return CachedAccountName; }
    /** Name of the realm this session entered (empty offline). */
    FString GetSelectedRealmName() const
    {
        for (const FWowRealmInfo& Realm : CachedRealms)
            if (Realm.RealmId == BindingRealmId) return Realm.Name;
        return CachedRealms.Num() == 1 ? CachedRealms[0].Name : FString();
    }

    /** Get the character name of the local player (from cached character list) */
    UFUNCTION(BlueprintCallable) FString GetCharacterName() const
    {
        uint64 Guid = PacketHandler.EntityManager.LocalPlayerGuid;
        for (const FWowCharacterInfo& C : CachedCharacters)
        {
            if (static_cast<uint64>(C.Guid) == Guid) return C.Name;
        }
        return TEXT("");
    }

    /** Send a raw packet (for advanced use) */
    void SendRawPacket(uint32 Opcode, const TArray<uint8>& Data = {});

    /** Send name query for a player */
    UFUNCTION(BlueprintCallable) void SendNameQuery(int64 Guid);
    /** Ask how a mirror-image unit (dressed NPC) looks: SMSG_MIRRORIMAGE_DATA answers (r257). */
    void SendGetMirrorImageData(uint64 Guid);

    /** Send creature query for an NPC */
    UFUNCTION(BlueprintCallable) void SendCreatureQuery(int32 Entry, int64 Guid);

    /** Send text emote command */
    UFUNCTION(BlueprintCallable) void SendTextEmote(int32 EmoteTextId, int64 TargetGuid = 0);

    /** Send learn talent command */
    UFUNCTION(BlueprintCallable) void SendLearnTalent(int32 TalentId, int32 RequestedRank);

    /** CMSG_LEARN_PREVIEW_TALENTS: X = talent id, Y = final 0-based rank index; lower tiers first. */
    void SendLearnPreviewTalents(const TArray<FIntPoint>& TalentRanks);

    /** Join the Wrath Dungeon Finder or raid-browser listing with packed LFGDungeons entries. */
    void SendLfgJoin(uint8 Roles, const TArray<uint32>& DungeonEntries, const FString& Comment = FString());
    void SendLfgLeave();
    void SendLfgSetRoles(uint8 Roles);
    void SendLfgSetComment(const FString& Comment);
    void SendLfgProposalResult(uint32 ProposalId, bool bAccept);
    void SendLfgTeleport(bool bOut);
    void SendLfgStatusRequest();
    void SendLfgPlayerLockInfoRequest();
    void SendLfgPartyLockInfoRequest();
    void SendRaidBrowserSearch(uint32 RaidEntry);
    void LeaveRaidBrowserSearch(uint32 RaidEntry);
    void SendGroupInviteByName(const FString& PlayerName);

    /** Get the cached character list (valid after WorldHaveCharList state) */
    UFUNCTION(BlueprintCallable) TArray<FWowCharacterInfo> GetCachedCharacters() const { return CachedCharacters; }

    UFUNCTION(BlueprintCallable) int64 GetSelectedCharacterGuid() const { return SelectedCharacterGuid; }
    UFUNCTION(BlueprintCallable) bool GetSelectedCharacterInfo(FWowCharacterInfo& OutCharacter) const;

    /** Configure the local roster and selection without opening network sockets. */
    void ConfigureOfflineSession(const TArray<FWowCharacterInfo>& Characters, int64 InSelectedCharacterGuid = 0);

    /** Get the cached realm list (valid after AuthHaveRealmList state) */
    UFUNCTION(BlueprintCallable) TArray<FWowRealmInfo> GetCachedRealms() const { return CachedRealms; }

    /** Packet handler — dispatches SMSG opcodes and tracks entities */
    FWowPacketHandler PacketHandler;

    UPROPERTY(BlueprintAssignable) FOnSessionStateChanged OnStateChanged;
    UPROPERTY(BlueprintAssignable) FOnRealmList OnRealmList;
    UPROPERTY(BlueprintAssignable) FOnCharacterList OnCharacterList;
    UPROPERTY(BlueprintAssignable) FOnWowError OnError;
    UPROPERTY(BlueprintAssignable) FOnCharCreateResult OnCharCreateResult;
    UPROPERTY(BlueprintAssignable) FOnLogoutComplete OnLogoutComplete;
    UPROPERTY(BlueprintAssignable) FOnWowError OnWorldDisconnected;
    int32 GetWorldLatencyMs() const;
    int32 GetUnitReaction(const FWowEntity* From, const FWowEntity* To);
private:
    enum class ECursorPayloadType : uint8
    {
        None,
        Spell,
        Action,
        Item,
    };

    struct FCursorPayloadState
    {
        ECursorPayloadType Type = ECursorPayloadType::None;
        int32 PrimaryId = 0;
        FString Detail;
        uint8 ActionType = 0;
        int32 SourceActionSlot = -1;
        uint8 SourceBag = 255;
        uint8 SourceSlot = 0;
        uint8 SourceInventorySlot = 0;
        bool bFromInventorySlot = false;
        uint32 SplitCount = 0;
        uint64 ItemGuid = 0;
        uint8 GuildBankTab = 0; // r285: with SourceBag 253 (guild bank)
    };

    EWowSessionState State = EWowSessionState::Disconnected;
    void SetState(EWowSessionState S);
    void BroadcastActionButtonsChanged();
    void EnsureActionButtonCapacity(int32 SlotIndex);

    void OnAuthResultReceived(bool bSuccess);
    void OnRealmListReceived(const TArray<FWowRealmInfo>& Realms);
    void OnWorldAuthResult(bool bSuccess);
    void HandleWorldDisconnected(const FString& Reason);
    FDelegateHandle WorldEntryStateHandle;
    void OnWorldCharacterList(const TArray<FWowCharacterInfo>& Characters);

    TSharedPtr<FWowAuthSocket> AuthSocket;
    TSharedPtr<FWowWorldSocket> WorldSocket;
    FString CachedAccountName;
    FString BindingServerIdentity;
    uint32 BindingRealmId=0;
    TArray<uint8> SessionKey;
    TArray<FWowRealmInfo> CachedRealms;
    TArray<FWowCharacterInfo> CachedCharacters;
    int64 SelectedCharacterGuid = 0;
    int64 TargetGuid = 0;
    uint8 CancelCastCounter = 0;
    FCursorPayloadState CursorPayload;
    uint64 DeleteConfirmationGuid = 0;
    uint64 PendingEquipGuid = 0;
    uint8 PendingEquipBag = 255, PendingEquipSlot = 0;
    int32 PendingEquipDestination = -1;
    uint32 PendingEquipToken = 0;
    bool CursorSourceIsValid();
};
