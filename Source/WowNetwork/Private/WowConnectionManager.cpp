#include "WowConnectionManager.h"
#include "WowRealmHandshake.h"
#include "WowItemDestroyPolicy.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowUpdateFields.h"
#include "Net/WowAuthSocket.h"
#include "Net/WowWorldSocket.h"
#include "WowOpcodes.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowNet, Log, All);

namespace
{
    constexpr uint8 ACTION_BUTTON_TYPE_SPELL = 0x00;
    constexpr uint8 ACTION_BUTTON_TYPE_ITEM = 0x80;
    constexpr uint32 AUTO_ATTACK_SPELL_ID = 6603;

    template <typename T>
    void AppendPacketValue(TArray<uint8>& Data, const T& Value)
    {
        Data.Append(reinterpret_cast<const uint8*>(&Value), sizeof(T));
    }

    void AppendPacketString(TArray<uint8>& Data, const FString& Value)
    {
        FTCHARToUTF8 Utf8(*Value);
        Data.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
        Data.Add(0);
    }
}

void UWowConnectionManager::SetState(EWowSessionState S)
{
    State = S;
    OnStateChanged.Broadcast(S);
}

void UWowConnectionManager::Login(const FString& U, const FString& P, const FString& S, int32 Port)
{
    UE_LOG(LogWowNet, Log, TEXT("Login: %s@%s:%d"), *U, *S, Port);

    // Disconnect any existing session
    Disconnect();

    CachedAccountName = U;
    BindingServerIdentity = S.ToLower()+FString::Printf(TEXT(":%d"),Port);
    SetState(EWowSessionState::AuthConnecting);

    AuthSocket = MakeShared<FWowAuthSocket>();
    AuthSocket->OnAuthResult.BindUObject(this, &UWowConnectionManager::OnAuthResultReceived);
    AuthSocket->OnRealmList.BindUObject(this, &UWowConnectionManager::OnRealmListReceived);

    if (!AuthSocket->Connect(S, Port, U, P))
    {
        UE_LOG(LogWowNet, Error, TEXT("Failed to connect to auth server"));
        SetState(EWowSessionState::Error);
        OnError.Broadcast(TEXT("Failed to connect to auth server"));
        AuthSocket.Reset();
        return;
    }

    SetState(EWowSessionState::AuthChallengeSent);
}

void UWowConnectionManager::OnAuthResultReceived(bool bSuccess)
{
    if (bSuccess)
    {
        UE_LOG(LogWowNet, Log, TEXT("Authentication successful"));
        SessionKey = AuthSocket->GetSessionKey();
        SetState(EWowSessionState::AuthProofSent);

        // Automatically request realm list
        SetState(EWowSessionState::AuthRequestingRealmList);
        AuthSocket->RequestRealmList();
    }
    else
    {
        UE_LOG(LogWowNet, Error, TEXT("Authentication failed"));
        SetState(EWowSessionState::Error);
        OnError.Broadcast(TEXT("Authentication failed"));
        AuthSocket.Reset();
    }
}

void UWowConnectionManager::OnRealmListReceived(const TArray<FWowRealmInfo>& Realms)
{
    UE_LOG(LogWowNet, Log, TEXT("Received %d realms"), Realms.Num());
    CachedRealms = Realms;
    SetState(EWowSessionState::AuthHaveRealmList);
    OnRealmList.Broadcast(Realms);
}

void UWowConnectionManager::SelectRealm(int32 I)
{
    UE_LOG(LogWowNet, Log, TEXT("Realm: %d"), I);

    if (!CachedRealms.IsValidIndex(I))
    {
        UE_LOG(LogWowNet, Error, TEXT("Invalid realm index: %d"), I);
        OnError.Broadcast(TEXT("Invalid realm index"));
        return;
    }

    const FWowRealmInfo& Realm = CachedRealms[I];
    UE_LOG(LogWowNet, Log, TEXT("Selected realm: %s id=%u (%s:%d)"), *Realm.Name, Realm.RealmId, *Realm.Address, Realm.Port);

    if (SessionKey.Num() == 0)
    {
        UE_LOG(LogWowNet, Error, TEXT("Cannot connect to world server: no session key (auth not completed)"));
        OnError.Broadcast(TEXT("No session key — authenticate first"));
        return;
    }

    SetState(EWowSessionState::WorldConnecting);

    // Disconnect auth socket -- no longer needed
    if (AuthSocket.IsValid())
    {
        AuthSocket->Disconnect();
        AuthSocket.Reset();
    }

    // Change Realm closes the old world socket without discarding the auth session.
    if (WorldSocket.IsValid())
    {
        WorldSocket->Disconnect();
        WorldSocket.Reset();
    }
    CachedCharacters.Empty();
    PacketHandler.EntityManager.Clear();
    // Create and connect world socket
    WorldSocket = MakeShared<FWowWorldSocket>();
    WorldSocket->OnDisconnected.BindUObject(this, &UWowConnectionManager::HandleWorldDisconnected);
    WorldSocket->OnAuthResult.BindUObject(this, &UWowConnectionManager::OnWorldAuthResult);
    WorldSocket->OnCharacterList.BindUObject(this, &UWowConnectionManager::OnWorldCharacterList);

    // Route unhandled server packets through the packet handler (fires on game thread)
    WorldSocket->OnPacket.BindLambda([this](uint16 Opcode, const TArray<uint8>& Data)
    {
        // Intercept char create/delete results
        if (Opcode == WowOpcode::SMSG_CHAR_CREATE || Opcode == WowOpcode::SMSG_CHAR_DELETE)
        {
            uint8 Result = Data.Num() > 0 ? Data[0] : 0xFF;
            OnCharCreateResult.Broadcast(Result);
            // Auto-refresh character list on success (0x2F = CHAR_CREATE_SUCCESS, 0x47 = CHAR_DELETE_SUCCESS
            // in 3.3.5; it said 0x3A, which is a create error, so a deleted character stayed listed - r276).
            if (Result == 0x2F || Result == 0x47)
            {
                RequestCharacterList();
            }
            return;
        }
		// r398 (Stefan 16.10: Sinister Strike rank 1 stayed on the bar after rank 2 was learned and could
		// not be used): SMSG_SUPERCEDED_SPELL (0x12C: u32 old spell, u32 new spell) - the server made the
		// old rank inactive. Like the client, every action slot holding the old rank now holds the new
		// one (also saved on the server), and the old rank leaves the known spells.
		if (Opcode == 0x012C && Data.Num() >= 8)
		{
			uint32 OldSpell = 0, NewSpell = 0;
			FMemory::Memcpy(&OldSpell, Data.GetData(), 4);
			FMemory::Memcpy(&NewSpell, Data.GetData() + 4, 4);
			int32 Replaced = 0;
			for (int32 Slot = 0; Slot < PacketHandler.ActionButtons.Num(); ++Slot)
			{
				const uint32 Packed = PacketHandler.ActionButtons[Slot];
				if (Packed == 0 || (Packed >> 24) != ACTION_BUTTON_TYPE_SPELL || (Packed & 0x00FFFFFFu) != OldSpell) continue;
				PacketHandler.ActionButtons[Slot] = (NewSpell & 0x00FFFFFFu) | (Packed & 0xFF000000u);
				SendSetActionButton(Slot, NewSpell, ACTION_BUTTON_TYPE_SPELL);
				++Replaced;
			}
			PacketHandler.KnownSpells.Add(NewSpell);
			PacketHandler.KnownSpells.Remove(OldSpell);
			UE_LOG(LogWowNet, Log, TEXT("SUPERCEDED_SPELL: %u -> %u (%d action slots updated)"), OldSpell, NewSpell, Replaced);
			PacketHandler.OnKnownSpellsChanged.Broadcast(true);
			if (Replaced > 0) PacketHandler.OnActionButtonsUpdated.Broadcast();
			return;
		}
		if (Opcode == WowOpcode::SMSG_LOGOUT_COMPLETE)
		{
			UE_LOG(LogWowNet, Log, TEXT("Server completed logout; returning to character selection"));
			PacketHandler.HandlePacket(Opcode, Data);
			PacketHandler.EntityManager.Clear();
			SelectedCharacterGuid = 0;
			OnLogoutComplete.Broadcast();
			RequestCharacterList();
			return;
		}
        PacketHandler.HandlePacket(Opcode, Data);
    });

    // Allow packet handler to send responses back to the server (e.g. TIME_SYNC_RESP)
    PacketHandler.OnSendPacket.BindLambda([this](uint32 Opcode, const TArray<uint8>& Data)
    {
        if (WorldSocket.IsValid())
        {
            WorldSocket->SendPacket(Opcode, Data);
        }
    });

    BindingRealmId=Realm.RealmId;
    if (!WorldSocket->Connect(Realm.Address, Realm.Port, CachedAccountName, SessionKey, Realm.RealmId))
    {
        UE_LOG(LogWowNet, Error, TEXT("Failed to connect to world server %s:%d"), *Realm.Address, Realm.Port);
        SetState(EWowSessionState::Error);
        OnError.Broadcast(TEXT("Failed to connect to world server"));
        WorldSocket.Reset();
        return;
    }

    UE_LOG(LogWowNet, Log, TEXT("Connecting to world server %s:%d ..."), *Realm.Address, Realm.Port);
}

void UWowConnectionManager::OnWorldAuthResult(bool bSuccess)
{
    if (bSuccess)
    {
        UE_LOG(LogWowNet, Log, TEXT("World server authenticated"));
        SetState(EWowSessionState::WorldAuthenticated);

        // Initialize Warden anti-cheat system with session key
        PacketHandler.InitializeWarden(SessionKey);
    }
    else
    {
        UE_LOG(LogWowNet, Error, TEXT("World server authentication failed"));
        SetState(EWowSessionState::Error);
        OnError.Broadcast(TEXT("World server authentication failed"));
        WorldSocket.Reset();
    }
}

void UWowConnectionManager::OnWorldCharacterList(const TArray<FWowCharacterInfo>& Characters)
{
    UE_LOG(LogWowNet, Log, TEXT("Received %d characters from world server"), Characters.Num());
    CachedCharacters = Characters;
    SetState(EWowSessionState::WorldHaveCharList);
    OnCharacterList.Broadcast(Characters);
}

void UWowConnectionManager::RequestCharacterList()
{
    UE_LOG(LogWowNet, Log, TEXT("Requesting character list"));

    if (!WorldSocket.IsValid())
    {
        UE_LOG(LogWowNet, Error, TEXT("No world socket connection"));
        OnError.Broadcast(TEXT("Not connected to world server"));
        return;
    }

    SetState(EWowSessionState::WorldRequestingCharList);
    WorldSocket->SendCharEnum();
}

void UWowConnectionManager::RequestLogout()
{
	if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame)
	{
		UE_LOG(LogWowNet, Warning, TEXT("Cannot request logout without an active world session"));
		return;
	}

	WorldSocket->SendPacket(WowOpcode::CMSG_LOGOUT_REQUEST, {});
	UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_LOGOUT_REQUEST"));
}

void UWowConnectionManager::CancelLogout()
{
	if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame)
	{
		return;
	}

	WorldSocket->SendPacket(WowOpcode::CMSG_LOGOUT_CANCEL, {});
	UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_LOGOUT_CANCEL"));
}

void UWowConnectionManager::EnterWorld(int64 G)
{
    UE_LOG(LogWowNet, Log, TEXT("Enter world: GUID %llu"), G);
    SelectedCharacterGuid = G;

    if (!WorldSocket.IsValid())
    {
        UE_LOG(LogWowNet, Error, TEXT("No world socket connection"));
        OnError.Broadcast(TEXT("Not connected to world server"));
        return;
    }

    PacketHandler.EntityManager.LocalPlayerGuid = static_cast<uint64>(G);
    PendingTargetSpell=0;
    PendingEnchantItemGuid=0;
    PacketHandler.TrainerGuid=0;PacketHandler.AuctionGuid=0;PacketHandler.TrainerOffers.Reset();PacketHandler.AuctionOffers.Reset();
    PacketHandler.QuestLog.Reset();
    PacketHandler.PendingQuestTemplateQueries.Reset();
    PacketHandler.QuestPOIs.Reset();
    PacketHandler.QuestPOIRequested.Reset();
    PacketHandler.SuperTrackedQuestId = 0;

    // Listen for LOGIN_VERIFY_WORLD to transition to InGame state
    PacketHandler.OnLoginVerifyWorld.Remove(WorldEntryStateHandle);
    WorldEntryStateHandle = PacketHandler.OnLoginVerifyWorld.AddLambda([this](uint32 MapId, float X, float Y, float Z, float Orientation)
    {
        UE_LOG(LogWowNet, Log, TEXT("Entered world: map=%d pos=(%.1f, %.1f, %.1f) orient=%.2f"), MapId, X, Y, Z, Orientation);
        SetState(EWowSessionState::WorldInGame);
        // Ask whether the realm runs mod-wowunreal (r259); realms without it never answer.
        FWowRealmHandshake::Reset();
        FWowRealmHandshake::SendHello(*this);
    });
    static FDelegateHandle RealmHandshakeChatHandle;
    PacketHandler.OnChatMessage.Remove(RealmHandshakeChatHandle);
    RealmHandshakeChatHandle = PacketHandler.OnChatMessage.AddLambda(
        [this](uint8, uint32 Language, uint64, const FString&, const FString& Message, const FString&)
        {
            if (Language == 0xFFFFFFFFu) FWowRealmHandshake::HandleAddonMessage(*this, Message); // LANG_ADDON
        });

    SetState(EWowSessionState::WorldEnteringWorld);
    WorldSocket->SendPlayerLogin(G);
}

bool UWowConnectionManager::GetSelectedCharacterInfo(FWowCharacterInfo& OutCharacter) const
{
    const int64 Guid = SelectedCharacterGuid != 0
        ? SelectedCharacterGuid
        : static_cast<int64>(PacketHandler.EntityManager.LocalPlayerGuid);

    if (Guid == 0)
    {
        return false;
    }

    for (const FWowCharacterInfo& Character : CachedCharacters)
    {
        if (Character.Guid == Guid)
        {
            OutCharacter = Character;
            return true;
        }
    }

    return false;
}

void UWowConnectionManager::ConfigureOfflineSession(
    const TArray<FWowCharacterInfo>& Characters,
    int64 InSelectedCharacterGuid)
{
    PacketHandler.RealmClock = FWowRealmClock();
    CachedCharacters = Characters;
    SelectedCharacterGuid = InSelectedCharacterGuid;
    if (InSelectedCharacterGuid != 0)
    {
        PacketHandler.EntityManager.LocalPlayerGuid = static_cast<uint64>(InSelectedCharacterGuid);
    }
}

void UWowConnectionManager::CreateCharacter(const FString& Name, uint8 Race, uint8 Class, uint8 Gender,
    uint8 Skin, uint8 Face, uint8 HairStyle, uint8 HairColor, uint8 FacialHair)
{
    if (!WorldSocket.IsValid())
    {
        OnError.Broadcast(TEXT("Not connected to world server"));
        return;
    }

    WorldSocket->SendCharCreate(Name, Race, Class, Gender, Skin, Face, HairStyle, HairColor, FacialHair);
}

void UWowConnectionManager::DeleteCharacter(int64 Guid)
{
    if (!WorldSocket.IsValid())
    {
        OnError.Broadcast(TEXT("Not connected to world server"));
        return;
    }

    WorldSocket->SendCharDelete(static_cast<uint64>(Guid));
}

void UWowConnectionManager::SendMovement(int32 Opcode, const FVector& Position, float Orientation, int32 MoveFlags)
{
    // Legacy entry point (position + flags only). Route through the shared
    // writer so flags that require extra blocks still produce a full packet.
    FWowOutgoingMovementInfo Info;
    Info.Flags = static_cast<uint32>(MoveFlags);
    Info.Position = Position;
    Info.Orientation = Orientation;
    SendMovementInfo(static_cast<uint16>(Opcode), Info);
}

// r315: the position the server last heard from us (WoW yards), for the gossip range diagnostic.
static FVector GWowLastSentMovePosition = FVector::ZeroVector;
static double GWowLastSentMoveSeconds = 0.0;

void UWowConnectionManager::SendMovementInfo(uint16 Opcode, FWowOutgoingMovementInfo Info)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    GWowLastSentMovePosition = Info.Position;
    GWowLastSentMoveSeconds = FPlatformTime::Seconds();
    Info.Guid = PacketHandler.EntityManager.LocalPlayerGuid;
    if (Info.TimeMs == 0) Info.TimeMs = WowMovementPacket::ClientTimeMs();
    const TArray<uint8> Data = WowMovementPacket::BuildMovePayload(Info);
    UE_LOG(LogWowNet, VeryVerbose, TEXT("Movement 0x%03X flags=0x%08X len=%d"), Opcode, Info.Flags, Data.Num());
    WorldSocket->SendPacket(static_cast<uint32>(Opcode), Data);
}

void UWowConnectionManager::SendMovementAck(uint16 AckOpcode, uint32 Counter, FWowOutgoingMovementInfo Info,
    bool bAppendFloat, float FloatValue, bool bAppendU32, uint32 U32Value)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    Info.Guid = PacketHandler.EntityManager.LocalPlayerGuid;
    if (Info.TimeMs == 0) Info.TimeMs = WowMovementPacket::ClientTimeMs();
    TArray<uint8> Data = WowMovementPacket::BuildAckPayload(Info, Counter);
    if (bAppendFloat) WowMovementPacket::AppendFloat(Data, FloatValue);
    if (bAppendU32) WowMovementPacket::AppendU32(Data, U32Value);
    UE_LOG(LogWowNet, Log, TEXT("Movement ack 0x%03X counter=%u flags=0x%08X"), AckOpcode, Counter, Info.Flags);
    WorldSocket->SendPacket(static_cast<uint32>(AckOpcode), Data);
}

uint32 UWowConnectionManager::GetDefaultChatLanguage()
{
    uint8 Race = 0;
    if (const FWowPlayerEntity* Player = PacketHandler.EntityManager.GetLocalPlayer()) Race = Player->GetRaceId();
    switch (Race)
    {
    case 2: case 5: case 6: case 8: case 10: return 1; // Orc, Undead, Tauren, Troll, Blood Elf -> Orcish
    default: return 7;                                // Common
    }
}

void UWowConnectionManager::SendAddonMessage(const FString& Prefix, const FString& Text, int32 Type, const FString& Target)
{
    // The original client caps prefix+text at 254 bytes and never sends an empty prefix.
    if (Prefix.IsEmpty() || Prefix.Contains(TEXT("\t"))) return;
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    // Prefix and text arrive byte for byte from Lua (one TCHAR per byte) and are sent
    // unchanged: re-encoding as UTF-8 would corrupt binary addon payloads.
    TArray<uint8> Data;
    Data.Reserve(Prefix.Len() + Text.Len() + Target.Len() + 16);
    const uint32 ChatType = static_cast<uint32>(Type);
    Data.Append(reinterpret_cast<const uint8*>(&ChatType), 4);
    const uint32 Lang = 0xFFFFFFFFu; // LANG_ADDON
    Data.Append(reinterpret_cast<const uint8*>(&Lang), 4);
    if (Type == 7 || Type == 17)
    {
        FTCHARToUTF8 TargetUtf8(*Target);
        Data.Append(reinterpret_cast<const uint8*>(TargetUtf8.Get()), TargetUtf8.Length());
        Data.Add(0);
    }
    const FString Body = Prefix + TEXT("\t") + Text;
    for (int32 Index = 0; Index < Body.Len() && Index < 255; ++Index)
    {
        const uint8 Byte = static_cast<uint8>(Body[Index] & 0xFF);
        if (Byte == 0) break;
        Data.Add(Byte);
    }
    Data.Add(0);
    WorldSocket->SendPacket(WowOpcode::CMSG_MESSAGECHAT, Data);
    static int32 LoggedAddonSends = 0;
    if (LoggedAddonSends++ < 20)
        UE_LOG(LogWowNet, Log, TEXT("Sent addon message (type=%d, prefix=%s, target=%s, %d bytes)"), Type, *Prefix, *Target, Body.Len());
}

void UWowConnectionManager::SendChatMessage(const FString& Message, int32 Type, const FString& Target, int32 Language)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    // Common/Orcish are faction languages: send the one this race can speak,
    // otherwise the server answers "You don't know that language".
    if (Language == 7 || Language == 1) Language = static_cast<int32>(GetDefaultChatLanguage());

    // WoW 3.3.5a chat messages are limited to 255 bytes (UTF-8)
    FString TruncatedMessage = Message.Left(255);

    TArray<uint8> Data;
    Data.Reserve(TruncatedMessage.Len() + 16);

    // ChatMsg enum used by WoW 3.3.5a (SAY=1, PARTY=2, GUILD=4,
    // YELL=6, WHISPER=7, CHANNEL=17).
    uint32 ChatType = static_cast<uint32>(Type);
    Data.Append(reinterpret_cast<const uint8*>(&ChatType), 4);

    // uint32 language (Common=7)
    uint32 Lang = static_cast<uint32>(Language);
    Data.Append(reinterpret_cast<const uint8*>(&Lang), 4);

    // Whispers and numbered channels carry a null-terminated target/channel
    // name before the message body.
    if (Type == 7 || Type == 17)
    {
        FTCHARToUTF8 TargetUtf8(*Target);
        Data.Append(reinterpret_cast<const uint8*>(TargetUtf8.Get()), TargetUtf8.Length());
        Data.Add(0);
    }

    // Null-terminated message string (UTF-8)
    FTCHARToUTF8 MsgUtf8(*TruncatedMessage);
    int32 MsgLen = FMath::Min(MsgUtf8.Length(), 255);
    Data.Append(reinterpret_cast<const uint8*>(MsgUtf8.Get()), MsgLen);
    Data.Add(0);

    WorldSocket->SendPacket(WowOpcode::CMSG_MESSAGECHAT, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent chat (type=%d): %s"), Type, *Message);
}

void UWowConnectionManager::SendSetSelection(int64 InTargetGuid)
{
    // Selection is client state first: FrameXML, offline testing, spell routing,
    // and focus/target-of-target queries all need it even without a world socket.
    TargetGuid = InTargetGuid;

    if (!WorldSocket.IsValid()) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 G = static_cast<uint64>(InTargetGuid);
    FMemory::Memcpy(Data.GetData(), &G, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_SET_SELECTION, Data);
    UE_LOG(LogWowNet, Log, TEXT("Set target: GUID %lld"), InTargetGuid);
}

void UWowConnectionManager::SendCastSpell(int32 SpellId, int64 InTargetGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    // r290 (Stefan 13.10: one click on Healing Wave said "Another action is in progress"): a click on
    // a Bartender button cast twice in the same frame - the native secure action dispatch and the
    // button's own SecureActionButton OnClick (UseAction) - and the server refused the second.
    // One cast of a spell per frame, like one click in the client.
    {
        static uint64 LastCastFrame = 0;
        static int32 LastCastSpell = 0;
        if (LastCastFrame == GFrameCounter && LastCastSpell == SpellId)
        {
            UE_LOG(LogWowNet, Verbose, TEXT("SendCastSpell: spell %d already sent this frame"), SpellId);
            return;
        }
        LastCastFrame = GFrameCounter;
        LastCastSpell = SpellId;
    }

    // r254 (Stefan 12.10): dead or a ghost, the client refuses spells that are not castable while dead
    // itself, like the original ("Can't cast as ghost"); the server's answer read "Can't do that while charmed".
    if (const auto* Me = PacketHandler.EntityManager.GetLocalPlayer())
    {
        const bool bGhost = PacketHandler.DeathState.bGhost || (Me->GetField(PlayerField::FLAGS) & 0x10u);
        const bool bDead = !bGhost && Me->GetHealth() <= 0;
        const auto* DeadSpell = FDbcStore::Get().Spells().GetById(uint32(SpellId));
        if ((bGhost || bDead) && SpellId != 6603 && !(DeadSpell && (DeadSpell->Attributes & 0x00800000u)))
        {
            PacketHandler.OnCastFailed.Broadcast(uint32(SpellId), bGhost ? 73 : 23); // NOT_WHILE_GHOST / CASTER_DEAD (r277: 22 is CASTER_AURASTATE)
            return;
        }
    }

    // Most UI callers pass 0 to mean "use the current selection".
    const auto* ExplicitObject = PacketHandler.EntityManager.Find(static_cast<uint64>(InTargetGuid));
    const bool bObjectTarget = ExplicitObject && ExplicitObject->IsGameObject();
    const int64 ResolvedTargetGuid = bObjectTarget ? InTargetGuid : ResolveHelpfulSpellTarget(SpellId,(InTargetGuid != 0) ? InTargetGuid : TargetGuid);
    if (ResolvedTargetGuid==-1) { PendingTargetSpell=SpellId; return; }
    PendingTargetSpell=0;
    // A spell that needs an enemy (TARGET_UNIT_TARGET_ENEMY) with nothing targeted: the client says
    // "No target" itself and sends nothing (Stefan 11.10: Lightning Bolt without a target was sent
    // anyway and the server's refusal read "Target can't be charmed").
    if (ResolvedTargetGuid == 0)
        if (const auto* CastSpell = FDbcStore::Get().Spells().GetById(uint32(SpellId)))
        {
            bool bNeedsEnemy = false;
            for (int32 E = 0; E < 3; ++E)
                bNeedsEnemy |= CastSpell->EffectImplicitTargetA[E] == 6 || CastSpell->EffectImplicitTargetB[E] == 6;
            if (bNeedsEnemy && SpellId != 6603)
            {
                PacketHandler.OnCastFailed.Broadcast(uint32(SpellId), 11); // SPELL_FAILED_BAD_IMPLICIT_TARGETS "No target" (r277: 10 is AUTOTRACK_INTERRUPTED)
                return;
            }
        }
    const auto* ResolvedObject=PacketHandler.EntityManager.Find(static_cast<uint64>(ResolvedTargetGuid));
    const bool bItemTarget=ResolvedObject && ResolvedObject->IsItem();

    // Auto Attack (spell 6603) is not cast via CMSG_CAST_SPELL in 3.3.5.
    // Route it through the melee swing opcode so Blizzard action buttons can start combat.
    if (SpellId == 6603)
    {
        if (ResolvedTargetGuid == 0)
        {
            UE_LOG(LogWowNet, Warning, TEXT("SendCastSpell: ignoring auto-attack without a valid target"));
            return;
        }

        SendAttackSwing(ResolvedTargetGuid);
        UE_LOG(LogWowNet, Log, TEXT("SendCastSpell: routed auto-attack spell %d to AttackSwing on target %lld"),
            SpellId, ResolvedTargetGuid);
        return;
    }

    // On-next-swing abilities (Heroic Strike, Cleave, Maul, Raptor Strike) stay queued
    // until the swing lands. The original client does not send them again while queued;
    // every resend made the server interrupt the queued one, so it never went off.
    {
        static TMap<uint32, double> QueuedNextSwing;
        static TWeakObjectPtr<UWowConnectionManager> HookedManager;
        if (HookedManager.Get() != this)
        {
            HookedManager = this;
            QueuedNextSwing.Reset();
            PacketHandler.OnSpellGo.AddWeakLambda(this, [this](uint64 Caster, uint64, uint32 Id, uint32)
                { if (Caster == PacketHandler.EntityManager.LocalPlayerGuid) QueuedNextSwing.Remove(Id); });
            PacketHandler.OnSpellFailure.AddWeakLambda(this, [this](uint64 Caster, uint32 Id, uint8)
                { if (Caster == PacketHandler.EntityManager.LocalPlayerGuid) QueuedNextSwing.Remove(Id); });
        }
        const auto* SpellEntry = FDbcStore::Get().Spells().GetById(static_cast<uint32>(SpellId));
        if (SpellEntry && (SpellEntry->Attributes & (0x00000004 | 0x00000400)) != 0)
        {
            const double Now = FPlatformTime::Seconds();
            if (const double* Since = QueuedNextSwing.Find(static_cast<uint32>(SpellId)); Since && Now - *Since < 6.0)
            {
                UE_LOG(LogWowNet, Log, TEXT("SendCastSpell: spell %d already queued for the next swing"), SpellId);
                return;
            }
            QueuedNextSwing.Add(static_cast<uint32>(SpellId), Now);
        }
    }

    TArray<uint8> Data;
    Data.Reserve(32);

    // castCount (uint8) — increments per cast, wraps at 255
    static uint8 CastCounter = 0;
    Data.Add(CastCounter++);

    UE_LOG(LogWowNet, Log, TEXT("SendCastSpell: spell=%d target=%lld requestedTarget=%lld castCount=%d"),
        SpellId, ResolvedTargetGuid, InTargetGuid, CastCounter - 1);

    // spellId (uint32)
    uint32 Spell = static_cast<uint32>(SpellId);
    Data.Append(reinterpret_cast<const uint8*>(&Spell), 4);

    // castFlags (uint8) — 0 for normal cast
    uint8 CastFlags = 0;
    Data.Add(CastFlags);

    // targetMask (uint32) — TARGET_FLAG_UNIT (0x02) if we have a target, else TARGET_FLAG_NONE (0x00)
    uint64 TGuid = static_cast<uint64>(ResolvedTargetGuid);
    uint32 TargetMask = (TGuid != 0) ? (bItemTarget ? 0x0010 : bObjectTarget ? 0x0800 : 0x0002) : 0x0000;
    Data.Append(reinterpret_cast<const uint8*>(&TargetMask), 4);

    // If targeting a unit, write packed GUID
    if (TGuid != 0)
    {
        uint8 GuidMask = 0;
        uint8 GuidBytes[8];
        int32 GuidByteCount = 0;
        for (int32 i = 0; i < 8; ++i)
        {
            uint8 Byte = static_cast<uint8>((TGuid >> (i * 8)) & 0xFF);
            if (Byte != 0)
            {
                GuidMask |= (1 << i);
                GuidBytes[GuidByteCount++] = Byte;
            }
        }
        Data.Add(GuidMask);
        Data.Append(GuidBytes, GuidByteCount);
    }

    WorldSocket->SendPacket(WowOpcode::CMSG_CAST_SPELL, Data);
    UE_LOG(LogWowNet, Log, TEXT("Cast spell %d on target %lld"), SpellId, ResolvedTargetGuid);
}

void UWowConnectionManager::SendCancelCast(uint32 SpellId, bool bChanneling)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    if (bChanneling && SpellId == 0) return;

    // 3.3.5: cancel-cast carries its own counter then uint32 spell ID;
    // cancel-channel carries only uint32 spell ID (no counter).
    TArray<uint8> Data;
    Data.Reserve(5);
    if (!bChanneling) Data.Add(CancelCastCounter++);
    Data.Append(reinterpret_cast<const uint8*>(&SpellId), sizeof(SpellId));
    WorldSocket->SendPacket(bChanneling ? WowOpcode::CMSG_CANCEL_CHANNELLING
        : WowOpcode::CMSG_CANCEL_CAST, Data);
}

void UWowConnectionManager::SendSetActionButton(int32 SlotIndex, uint32 ActionId, uint8 ActionType)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    if (SlotIndex < 0 || SlotIndex >= 144) return;

    TArray<uint8> Data;
    Data.Reserve(5);

    const uint8 Button = static_cast<uint8>(SlotIndex);
    const uint32 PackedAction = (ActionId & 0x00FFFFFFu) | (uint32(ActionType) << 24);

    Data.Add(Button);
    Data.Append(reinterpret_cast<const uint8*>(&PackedAction), sizeof(PackedAction));

    WorldSocket->SendPacket(WowOpcode::CMSG_SET_ACTION_BUTTON, Data);
    UE_LOG(LogWowNet, Log, TEXT("Set action button %d -> action=%u type=%u"), SlotIndex, ActionId, ActionType);
}

void UWowConnectionManager::SendClearActionButton(int32 SlotIndex)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    if (SlotIndex < 0 || SlotIndex >= 144) return;

    TArray<uint8> Data;
    Data.Reserve(5);

    const uint8 Button = static_cast<uint8>(SlotIndex);
    const uint32 PackedAction = 0;

    Data.Add(Button);
    Data.Append(reinterpret_cast<const uint8*>(&PackedAction), sizeof(PackedAction));

    WorldSocket->SendPacket(WowOpcode::CMSG_SET_ACTION_BUTTON, Data);
    UE_LOG(LogWowNet, Log, TEXT("Cleared action button %d"), SlotIndex);
}

void UWowConnectionManager::PickupSpellCursor(int32 SpellId, const FString& BookType)
{
    CursorPayload.Type = ECursorPayloadType::Spell;
    CursorPayload.PrimaryId = SpellId;
    CursorPayload.Detail = BookType;
    CursorPayload.ActionType = ACTION_BUTTON_TYPE_SPELL;
    CursorPayload.SourceActionSlot = -1;
}

void UWowConnectionManager::PickupActionCursor(int32 SlotIndex)
{
    if (SlotIndex < 0 || SlotIndex >= 144 || !PacketHandler.ActionButtons.IsValidIndex(SlotIndex))
    {
        return;
    }

    const uint32 PackedAction = PacketHandler.ActionButtons[SlotIndex];
    if (PackedAction == 0)
    {
        return;
    }

    CursorPayload.Type = ECursorPayloadType::Action;
    CursorPayload.PrimaryId = static_cast<int32>(PackedAction & 0x00FFFFFFu);
    CursorPayload.Detail.Empty();
    CursorPayload.ActionType = static_cast<uint8>((PackedAction >> 24) & 0xFF);
    CursorPayload.SourceActionSlot = SlotIndex;
}

void UWowConnectionManager::PickupMacroCursor(int32 MacroIndex)
{
    if (MacroIndex <= 0) return;
    ClearCursorPayload();
    CursorPayload.Type = ECursorPayloadType::Action;
    CursorPayload.PrimaryId = MacroIndex;
    CursorPayload.Detail.Empty();
    CursorPayload.ActionType = 64; // ACTION_BUTTON_MACRO
    CursorPayload.SourceActionSlot = -1;
}

void UWowConnectionManager::PickupBagItemCursor(uint8 SourceBag, uint8 SourceSlot, int32 ItemId, const FString& ItemLink, uint32 SplitCount)
{
    ClearCursorPayload();
    CursorPayload.ItemGuid = GetItemGuidAtPosition(NetworkBag(SourceBag), NetworkSlot(SourceBag, SourceSlot));
    if (!CursorPayload.ItemGuid) return;
    CursorPayload.Type = ECursorPayloadType::Item;
    CursorPayload.PrimaryId = ItemId;
    CursorPayload.Detail = ItemLink;
    CursorPayload.SourceBag = SourceBag;
    CursorPayload.SourceSlot = SourceSlot;
    CursorPayload.SourceInventorySlot = 0;
    CursorPayload.bFromInventorySlot = false;
    CursorPayload.ActionType = 0;
    CursorPayload.SourceActionSlot = -1;
    CursorPayload.SplitCount = SplitCount;
}

void UWowConnectionManager::PickupInventoryItemCursor(uint8 InventorySlot, int32 ItemId, const FString& ItemLink)
{
    ClearCursorPayload();
    CursorPayload.ItemGuid = GetItemGuidAtPosition(255, InventorySlot);
    if (!CursorPayload.ItemGuid) return;
    CursorPayload.Type = ECursorPayloadType::Item;
    CursorPayload.PrimaryId = ItemId;
    CursorPayload.Detail = ItemLink;
    CursorPayload.SourceBag = 255;
    CursorPayload.SourceSlot = InventorySlot;
    CursorPayload.SourceInventorySlot = InventorySlot;
    CursorPayload.bFromInventorySlot = true;
    CursorPayload.ActionType = 0;
    CursorPayload.SourceActionSlot = -1;
    CursorPayload.SplitCount = 0;
}

void UWowConnectionManager::ClearCursorPayload()
{
    CursorPayload = FCursorPayloadState();
    DeleteConfirmationGuid = 0;
}

bool UWowConnectionManager::DropCursorActionOutside()
{
    if (CursorPayload.Type != ECursorPayloadType::Action) return false;
    const int32 Source = CursorPayload.SourceActionSlot;
    if (Source >= 0 && Source < 144 && PacketHandler.ActionButtons.IsValidIndex(Source))
    {
        SendClearActionButton(Source);
        PacketHandler.ActionButtons[Source] = 0;
        BroadcastActionButtonsChanged();
    }
    ClearCursorPayload();
    return true;
}

bool UWowConnectionManager::HasCursorPayload() const
{
    return CursorPayload.Type != ECursorPayloadType::None;
}

bool UWowConnectionManager::HasCursorSpellPayload() const
{
    return CursorPayload.Type == ECursorPayloadType::Spell;
}

bool UWowConnectionManager::HasCursorItemPayload() const
{
    return CursorPayload.Type == ECursorPayloadType::Item;
}

bool UWowConnectionManager::GetCursorInfo(FString& OutType, int32& OutId, FString& OutDetail) const
{
    if (CursorPayload.Type == ECursorPayloadType::Spell)
    {
        OutType = TEXT("spell");
        OutId = CursorPayload.PrimaryId;
        OutDetail = CursorPayload.Detail;
        return true;
    }

    if (CursorPayload.Type == ECursorPayloadType::Action && CursorPayload.SourceActionSlot < 0 && CursorPayload.ActionType == 64)
    {
        OutType = TEXT("macro"); // straight from the macro window
        OutId = CursorPayload.PrimaryId;
        OutDetail.Empty();
        return true;
    }

    if (CursorPayload.Type == ECursorPayloadType::Action)
    {
        OutType = TEXT("action");
        OutId = CursorPayload.SourceActionSlot + 1;
        OutDetail.Empty();
        return true;
    }

    if (CursorPayload.Type == ECursorPayloadType::Item)
    {
        OutType = TEXT("item");
        OutId = CursorPayload.PrimaryId;
        OutDetail = CursorPayload.Detail;
        return true;
    }

    OutType.Empty();
    OutId = 0;
    OutDetail.Empty();
    return false;
}

bool UWowConnectionManager::PlaceCursorIntoActionSlot(int32 SlotIndex)
{
    // Bag item -> action button (type 0x80, item entry). The item itself never moves:
    // the server never saw the pickup, so clearing the cursor returns it to its slot.
    if (SlotIndex >= 0 && SlotIndex < 144 && CursorPayload.Type == ECursorPayloadType::Item)
    {
        const uint32 Entry = static_cast<uint32>(CursorPayload.PrimaryId);
        if (!IsItemActionable(Entry))
        {
            UE_LOG(LogWowNet, Log, TEXT("Item %u cannot go on the action bar (no use effect)"), Entry);
            ClearCursorPayload();
            return true; // consumed: never fall through to a bag/sell/delete handler
        }
        EnsureActionButtonCapacity(SlotIndex);
        SendSetActionButton(SlotIndex, Entry, ACTION_BUTTON_TYPE_ITEM);
        PacketHandler.ActionButtons[SlotIndex] = (Entry & 0x00FFFFFFu) | (uint32(ACTION_BUTTON_TYPE_ITEM) << 24);
        BroadcastActionButtonsChanged();
        ClearCursorPayload();
        return true;
    }
    if (SlotIndex < 0 || SlotIndex >= 144 ||
        (CursorPayload.Type != ECursorPayloadType::Spell && CursorPayload.Type != ECursorPayloadType::Action))
    {
        return false;
    }

    EnsureActionButtonCapacity(SlotIndex);

    if (CursorPayload.Type == ECursorPayloadType::Spell)
    {
        SendSetActionButton(SlotIndex, static_cast<uint32>(CursorPayload.PrimaryId), ACTION_BUTTON_TYPE_SPELL);
        PacketHandler.ActionButtons[SlotIndex] = (static_cast<uint32>(CursorPayload.PrimaryId) & 0x00FFFFFFu);
    }
    else if (CursorPayload.Type == ECursorPayloadType::Action)
    {
        if (CursorPayload.SourceActionSlot == SlotIndex)
        {
            ClearCursorPayload();
            return true;
        }

        const uint32 DisplacedAction = PacketHandler.ActionButtons[SlotIndex];
        const uint32 PackedAction = (static_cast<uint32>(CursorPayload.PrimaryId) & 0x00FFFFFFu)
            | (uint32(CursorPayload.ActionType) << 24);
        SendSetActionButton(SlotIndex, static_cast<uint32>(CursorPayload.PrimaryId), CursorPayload.ActionType);
        PacketHandler.ActionButtons[SlotIndex] = PackedAction;

        if (CursorPayload.SourceActionSlot >= 0 && CursorPayload.SourceActionSlot < 144)
        {
            EnsureActionButtonCapacity(CursorPayload.SourceActionSlot);
            if (DisplacedAction)
                SendSetActionButton(CursorPayload.SourceActionSlot, DisplacedAction & 0x00ffffffu,
                    static_cast<uint8>(DisplacedAction >> 24));
            else SendClearActionButton(CursorPayload.SourceActionSlot);
            PacketHandler.ActionButtons[CursorPayload.SourceActionSlot] = DisplacedAction;
        }
    }

    BroadcastActionButtonsChanged();
    ClearCursorPayload();
    return true;
}

uint64 UWowConnectionManager::GetItemGuidAtPosition(uint8 Bag, uint8 Slot)
{
    const auto* Player = PacketHandler.EntityManager.GetLocalPlayer();
    if (!Player) return 0;
    // r282: player slots 39-66 are the bank, 67-73 the bank bags (contiguous after the backpack).
    if (Bag == 255)
        return Slot < 74 ? Player->GetField64(PlayerField::INV_SLOT_HEAD + Slot * 2) : 0;
    uint64 BagGuid = 0;
    if (Bag >= 19 && Bag <= 22) BagGuid = Player->GetBagGuid(Bag - 18);
    else if (Bag >= 67 && Bag <= 73) BagGuid = Player->GetField64(PlayerField::INV_SLOT_HEAD + Bag * 2);
    else return 0;
    const auto* Container = PacketHandler.EntityManager.FindContainer(BagGuid);
    return Container && Slot < Container->GetNumSlots() ? Container->GetItemGuidAtSlot(Slot) : 0;
}

void UWowConnectionManager::SendAutoBankItem(uint8 Bag, uint8 Slot)
{
    SendRawPacket(0x283, {Bag, Slot}); // CMSG_AUTOBANK_ITEM: uint8 bag, uint8 slot
}

void UWowConnectionManager::SendAutoStoreBankItem(uint8 Bag, uint8 Slot)
{
    SendRawPacket(0x282, {Bag, Slot}); // CMSG_AUTOSTORE_BANK_ITEM: uint8 bag, uint8 slot
}

// ── r285: guild bank ───────────────────────────────────────────────────────
namespace
{
void AppendU8(TArray<uint8>& Data, uint8 V) { Data.Add(V); }
void AppendU32(TArray<uint8>& Data, uint32 V) { for (int32 Byte = 0; Byte < 4; ++Byte) Data.Add(uint8(V >> (8 * Byte))); }
void AppendU64(TArray<uint8>& Data, uint64 V) { for (int32 Byte = 0; Byte < 8; ++Byte) Data.Add(uint8(V >> (8 * Byte))); }
void AppendString(TArray<uint8>& Data, const FString& Text)
{
    const FTCHARToUTF8 Utf8(*Text);
    Data.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    Data.Add(0);
}
}

void UWowConnectionManager::OpenGuildBank(uint64 VaultGuid)
{
    ActiveGuildBankGuid = VaultGuid;
    SendGuildPermissionsQuery();
    SendGuildBankMoneyWithdrawnQuery();
    TArray<uint8> Data;
    AppendU64(Data, VaultGuid);
    AppendU8(Data, 1); // all slots + tab names
    SendRawPacket(0x3E6, Data); // CMSG_GUILD_BANKER_ACTIVATE
}

void UWowConnectionManager::SendGuildBankQueryTab(uint8 Tab, bool bFull)
{
    if (!ActiveGuildBankGuid) return;
    TArray<uint8> Data;
    AppendU64(Data, ActiveGuildBankGuid);
    AppendU8(Data, Tab);
    AppendU8(Data, bFull ? 1 : 0);
    SendRawPacket(0x3E7, Data); // CMSG_GUILD_BANK_QUERY_TAB
}

void UWowConnectionManager::SendGuildBankSwapBankOnly(uint8 DestTab, uint8 DestSlot, uint8 SourceTab, uint8 SourceSlot, uint32 SourceEntry, int32 SplitCount)
{
    if (!ActiveGuildBankGuid) return;
    TArray<uint8> Data;
    AppendU64(Data, ActiveGuildBankGuid);
    AppendU8(Data, 1); // bank only
    AppendU8(Data, DestTab); AppendU8(Data, DestSlot); AppendU32(Data, 0);
    AppendU8(Data, SourceTab); AppendU8(Data, SourceSlot); AppendU32(Data, SourceEntry);
    AppendU8(Data, 0); // auto store
    AppendU32(Data, static_cast<uint32>(FMath::Max(0, SplitCount)));
    SendRawPacket(0x3E9, Data); // CMSG_GUILD_BANK_SWAP_ITEMS
}

void UWowConnectionManager::SendGuildBankSwapWithInventory(bool bToCharacter, uint8 Tab, uint8 Slot, uint32 Entry, uint8 Bag, uint8 BagSlot, int32 SplitCount, bool bAutoStore)
{
    if (!ActiveGuildBankGuid) return;
    TArray<uint8> Data;
    AppendU64(Data, ActiveGuildBankGuid);
    AppendU8(Data, 0); // with the character's inventory
    AppendU8(Data, Tab); AppendU8(Data, Slot); AppendU32(Data, Entry);
    AppendU8(Data, bAutoStore ? 1 : 0);
    if (bAutoStore)
    {
        AppendU32(Data, 0); AppendU8(Data, 0); AppendU32(Data, 0);
    }
    else
    {
        AppendU8(Data, Bag); AppendU8(Data, BagSlot); AppendU8(Data, bToCharacter ? 1 : 0);
        AppendU32(Data, static_cast<uint32>(FMath::Max(0, SplitCount)));
    }
    SendRawPacket(0x3E9, Data); // CMSG_GUILD_BANK_SWAP_ITEMS
}

void UWowConnectionManager::SendGuildBankBuyTab(uint8 Tab)
{
    if (!ActiveGuildBankGuid) return;
    TArray<uint8> Data; AppendU64(Data, ActiveGuildBankGuid); AppendU8(Data, Tab);
    SendRawPacket(0x3EA, Data); // CMSG_GUILD_BANK_BUY_TAB
}

void UWowConnectionManager::SendGuildBankUpdateTab(uint8 Tab, const FString& Name, const FString& Icon)
{
    if (!ActiveGuildBankGuid) return;
    TArray<uint8> Data; AppendU64(Data, ActiveGuildBankGuid); AppendU8(Data, Tab); AppendString(Data, Name); AppendString(Data, Icon);
    SendRawPacket(0x3EB, Data); // CMSG_GUILD_BANK_UPDATE_TAB
}

void UWowConnectionManager::SendGuildBankDepositMoney(uint32 Copper)
{
    if (!ActiveGuildBankGuid || !Copper) return;
    TArray<uint8> Data; AppendU64(Data, ActiveGuildBankGuid); AppendU32(Data, Copper);
    SendRawPacket(0x3EC, Data); // CMSG_GUILD_BANK_DEPOSIT_MONEY
}

void UWowConnectionManager::SendGuildBankWithdrawMoney(uint32 Copper)
{
    if (!ActiveGuildBankGuid || !Copper) return;
    TArray<uint8> Data; AppendU64(Data, ActiveGuildBankGuid); AppendU32(Data, Copper);
    SendRawPacket(0x3ED, Data); // CMSG_GUILD_BANK_WITHDRAW_MONEY
}

void UWowConnectionManager::SendGuildBankLogQuery(uint8 Tab)
{
    TArray<uint8> Data; AppendU8(Data, Tab);
    SendRawPacket(0x3EE, Data); // MSG_GUILD_BANK_LOG_QUERY (tab 6 = money log)
}

void UWowConnectionManager::SendGuildBankTextQuery(uint8 Tab)
{
    TArray<uint8> Data; AppendU8(Data, Tab);
    SendRawPacket(0x40A, Data); // MSG_QUERY_GUILD_BANK_TEXT
}

void UWowConnectionManager::SendGuildBankSetText(uint8 Tab, const FString& Text)
{
    TArray<uint8> Data; AppendU8(Data, Tab); AppendString(Data, Text.Left(500));
    SendRawPacket(0x40B, Data); // CMSG_SET_GUILD_BANK_TEXT
}

void UWowConnectionManager::SendGuildPermissionsQuery()
{
    SendRawPacket(0x3FC); // MSG_GUILD_PERMISSIONS
}

void UWowConnectionManager::SendGuildBankMoneyWithdrawnQuery()
{
    SendRawPacket(0x3FE); // MSG_GUILD_BANK_MONEY_WITHDRAWN
}

void UWowConnectionManager::PickupGuildBankItemCursor(uint8 Tab, uint8 Slot, uint32 Entry, const FString& ItemLink, uint32 SplitCount)
{
    ClearCursorPayload();
    if (!Entry) return;
    CursorPayload.Type = ECursorPayloadType::Item;
    CursorPayload.PrimaryId = static_cast<int32>(Entry);
    CursorPayload.Detail = ItemLink;
    CursorPayload.SourceBag = 253;
    CursorPayload.SourceSlot = Slot;
    CursorPayload.GuildBankTab = Tab;
    CursorPayload.SplitCount = SplitCount;
}

bool UWowConnectionManager::PlaceCursorIntoGuildBankSlot(uint8 Tab, uint8 Slot)
{
    if (CursorPayload.Type != ECursorPayloadType::Item) return false;
    if (CursorPayload.SourceBag == 253)
    {
        SendGuildBankSwapBankOnly(Tab, Slot, CursorPayload.GuildBankTab, CursorPayload.SourceSlot,
            static_cast<uint32>(CursorPayload.PrimaryId), static_cast<int32>(CursorPayload.SplitCount));
        ClearCursorPayload();
        return true;
    }
    if (!CursorSourceIsValid()) { ClearCursorPayload(); return false; }
    const uint8 SourceBag = CursorPayload.bFromInventorySlot ? 255 : NetworkBag(CursorPayload.SourceBag);
    const uint8 SourceSlot = CursorPayload.bFromInventorySlot ? CursorPayload.SourceInventorySlot : NetworkSlot(CursorPayload.SourceBag, CursorPayload.SourceSlot);
    SendGuildBankSwapWithInventory(false, Tab, Slot, 0, SourceBag, SourceSlot, static_cast<int32>(CursorPayload.SplitCount), false);
    ClearCursorPayload();
    return true;
}

void UWowConnectionManager::SendBuyBankSlot()
{
    if (!ActiveBankerGuid) return;
    TArray<uint8> Data;
    for (int32 Byte = 0; Byte < 8; ++Byte) Data.Add(uint8(ActiveBankerGuid >> (8 * Byte)));
    SendRawPacket(0x1B9, Data); // CMSG_BUY_BANK_SLOT: uint64 banker
}

bool UWowConnectionManager::CursorSourceIsValid()
{
    const uint8 Bag = CursorPayload.bFromInventorySlot ? 255 : NetworkBag(CursorPayload.SourceBag);
    const uint8 Slot = CursorPayload.bFromInventorySlot ? CursorPayload.SourceInventorySlot : NetworkSlot(CursorPayload.SourceBag, CursorPayload.SourceSlot);
    return HasCursorItemPayload() && CursorPayload.ItemGuid &&
        GetItemGuidAtPosition(Bag, Slot) == CursorPayload.ItemGuid;
}

bool UWowConnectionManager::PlaceCursorIntoContainerSlot(uint8 DestinationBag, uint8 DestinationSlot)
{
    // r285: a guild bank item goes into this bag slot (the server moves or swaps it).
    if (CursorPayload.Type == ECursorPayloadType::Item && CursorPayload.SourceBag == 253)
    {
        SendGuildBankSwapWithInventory(true, CursorPayload.GuildBankTab, CursorPayload.SourceSlot, static_cast<uint32>(CursorPayload.PrimaryId),
            NetworkBag(DestinationBag), NetworkSlot(DestinationBag, DestinationSlot), static_cast<int32>(CursorPayload.SplitCount), false);
        ClearCursorPayload();
        return true;
    }
    if (!CursorSourceIsValid()) { ClearCursorPayload(); return false; }
    const uint8 SourceBag = CursorPayload.bFromInventorySlot ? 255 : NetworkBag(CursorPayload.SourceBag);
    const uint8 SourceSlot = CursorPayload.bFromInventorySlot ? CursorPayload.SourceInventorySlot : NetworkSlot(CursorPayload.SourceBag, CursorPayload.SourceSlot);
    if (CursorPayload.SplitCount > 0 && !CursorPayload.bFromInventorySlot)
        SendSplitItem(SourceBag, SourceSlot, NetworkBag(DestinationBag), NetworkSlot(DestinationBag, DestinationSlot), CursorPayload.SplitCount);
    else
        SendSwapItem(SourceBag, SourceSlot, NetworkBag(DestinationBag), NetworkSlot(DestinationBag, DestinationSlot));
    ClearCursorPayload();
    return true;
}

bool UWowConnectionManager::PlaceCursorIntoFirstFreeContainerSlot(uint8 DestinationBag)
{
    if (CursorPayload.Type != ECursorPayloadType::Item)
    {
        return false;
    }

    const FWowPlayerEntity* Player = PacketHandler.EntityManager.GetLocalPlayer();
    if (!Player)
    {
        return false;
    }

    if (DestinationBag == 255)
    {
        for (uint8 SlotIndex = 0; SlotIndex < 16; ++SlotIndex)
        {
            if (Player->GetBackpackItemGuid(SlotIndex) == 0)
            {
                return PlaceCursorIntoContainerSlot(255, SlotIndex);
            }
        }
        return false;
    }

    // r282: the bank's own 28 slots (254) and the bank bags (5-11).
    if (DestinationBag == 254)
    {
        for (uint8 SlotIndex = 0; SlotIndex < 28; ++SlotIndex)
            if (GetItemGuidAtPosition(255, static_cast<uint8>(39 + SlotIndex)) == 0)
                return PlaceCursorIntoContainerSlot(254, SlotIndex);
        return false;
    }
    const uint64 BagGuid = DestinationBag >= 5 && DestinationBag <= 11
        ? Player->GetField64(PlayerField::INV_SLOT_HEAD + (62 + DestinationBag) * 2)
        : Player->GetBagGuid(DestinationBag);
    if (BagGuid == 0)
    {
        return false;
    }

    const FWowContainerEntity* Container = PacketHandler.EntityManager.FindContainer(BagGuid);
    if (!Container)
    {
        return false;
    }

    for (int32 SlotIndex = 0; SlotIndex < Container->GetNumSlots(); ++SlotIndex)
    {
        if (Container->GetItemGuidAtSlot(SlotIndex) == 0)
        {
            return PlaceCursorIntoContainerSlot(DestinationBag, static_cast<uint8>(SlotIndex));
        }
    }

    return false;
}

bool UWowConnectionManager::AutoEquipCursorItem()
{
    if (!CursorSourceIsValid() || CursorPayload.SplitCount) return false;
    const uint8 Bag = CursorPayload.bFromInventorySlot ? 255 : NetworkBag(CursorPayload.SourceBag);
    const uint8 Slot = CursorPayload.bFromInventorySlot ? CursorPayload.SourceInventorySlot : NetworkSlot(CursorPayload.SourceBag, CursorPayload.SourceSlot);
    return RequestEquipItem(Bag, Slot);
}

bool UWowConnectionManager::EquipCursorIntoSlot(uint8 EquipmentSlot)
{
    if (EquipmentSlot >= 19 || !CursorSourceIsValid() || CursorPayload.SplitCount) return false;
    const uint8 Bag = CursorPayload.bFromInventorySlot ? 255 : NetworkBag(CursorPayload.SourceBag);
    const uint8 Slot = CursorPayload.bFromInventorySlot ? CursorPayload.SourceInventorySlot : NetworkSlot(CursorPayload.SourceBag, CursorPayload.SourceSlot);
    return RequestEquipItem(Bag, Slot, EquipmentSlot);
}

bool UWowConnectionManager::RequestEquipItem(uint8 Bag, uint8 Slot, int32 Destination)
{
    if (Destination < -1 || Destination >= 19) return false;
    // A newer equip request replaces one still waiting for the bind confirmation (a popup
    // that never showed must not block equipping for the rest of the session).
    PendingEquipGuid = 0;
    const uint64 Guid = GetItemGuidAtPosition(Bag, Slot);
    const auto* Item = PacketHandler.EntityManager.FindItem(Guid);
    if (!Item) return false;
    const auto* Info = PacketHandler.ItemTemplates.Find(Item->Entry);
    if (!Info) { RequestItemInfo(Item->Entry); return false; }
    if (!Info->InventoryType) return false;
    if (Info->Bonding == 2 && !(Item->GetItemFlags() & 1u))
    {
        PendingEquipGuid = Guid; PendingEquipBag = Bag; PendingEquipSlot = Slot;
        PendingEquipDestination = Destination;
        ++PendingEquipToken;
        OnItemConfirmation.Broadcast(Destination < 0 ? TEXT("AUTOEQUIP_BIND_CONFIRM") : TEXT("EQUIP_BIND_CONFIRM"),
            {FString::Printf(TEXT("%u"), PendingEquipToken)});
        return true;
    }
    if (Destination < 0) SendAutoEquipItem(Bag, Slot);
    else SendSwapItem(Bag, Slot, 255, static_cast<uint8>(Destination));
    ClearCursorPayload();
    return true;
}

void UWowConnectionManager::ConfirmPendingEquip(uint32 Token)
{
    if (!PendingEquipGuid || Token != PendingEquipToken) return;
    if (GetItemGuidAtPosition(PendingEquipBag, PendingEquipSlot) == PendingEquipGuid)
    {
        if (PendingEquipDestination < 0) SendAutoEquipItem(PendingEquipBag, PendingEquipSlot);
        else SendSwapItem(PendingEquipBag, PendingEquipSlot, 255, static_cast<uint8>(PendingEquipDestination));
    }
    PendingEquipGuid = 0;
    ClearCursorPayload();
}

void UWowConnectionManager::CancelPendingEquip(uint32 Token)
{
    if (Token == PendingEquipToken) { PendingEquipGuid = 0; ClearCursorPayload(); }
}

bool UWowConnectionManager::AutoStoreCursorItem(uint8 DestinationBag)
{
    return PlaceCursorIntoFirstFreeContainerSlot(DestinationBag);
}

bool UWowConnectionManager::RequestDeleteCursorItem()
{
    if (!CursorSourceIsValid()) { ClearCursorPayload(); return false; }
    const auto* Item = PacketHandler.EntityManager.FindItem(CursorPayload.ItemGuid);
    const auto* Info = Item ? PacketHandler.ItemTemplates.Find(Item->Entry) : nullptr;
    if (!Info) { if (Item) RequestItemInfo(Item->Entry); return false; }
    DeleteConfirmationGuid = CursorPayload.ItemGuid;
    // Grey/white go immediately; green+ asks, BoE and epic+ need "DELETE" typed.
    const EWowDestroyConfirm Mode = FWowItemDestroyPolicy::Get(Info->Quality, Info->Bonding, Item->GetItemFlags());
    if (Mode == EWowDestroyConfirm::None) return DeleteCursorItem();
    const int32 Count = CursorPayload.SplitCount ? static_cast<int32>(CursorPayload.SplitCount) : FMath::Max(1, Item->GetStackCount());
    // Args: name, quality, confirmation mode (1 = Yes/No, 2 = type DELETE), count, item entry.
    OnItemConfirmation.Broadcast(TEXT("DELETE_ITEM_CONFIRM"), {Info->Name, FString::FromInt(Info->Quality),
        FString::FromInt(static_cast<int32>(Mode)), FString::FromInt(Count), FString::FromInt(static_cast<int32>(Item->Entry))});
    return true;
}

bool UWowConnectionManager::DeleteCursorItem()
{
    if (!CursorSourceIsValid() || !DeleteConfirmationGuid || DeleteConfirmationGuid != CursorPayload.ItemGuid)
        return false;
    const uint8 Bag = CursorPayload.bFromInventorySlot ? 255 : NetworkBag(CursorPayload.SourceBag);
    const uint8 Slot = CursorPayload.bFromInventorySlot ? CursorPayload.SourceInventorySlot : NetworkSlot(CursorPayload.SourceBag, CursorPayload.SourceSlot);
    const uint8 Count = CursorPayload.SplitCount ? static_cast<uint8>(FMath::Min(CursorPayload.SplitCount, 255u)) : 0;
    SendDestroyItem(Bag, Slot, Count);
    ClearCursorPayload();
    return true;
}

void UWowConnectionManager::SendSwapItem(uint8 SourceBag, uint8 SourceSlot, uint8 DestinationBag, uint8 DestinationSlot)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    // 3.3.5 CMSG_SWAP_ITEM puts destination first, unlike CMSG_SPLIT_ITEM.
    TArray<uint8> Data = {DestinationBag, DestinationSlot, SourceBag, SourceSlot};
    WorldSocket->SendPacket(WowOpcode::CMSG_SWAP_ITEM, Data);
}

FWowActionInvocation UWowConnectionManager::ResolveActionInvocation(uint32 PackedAction, int64 InTargetGuid)
{
    FWowActionInvocation Invocation;
    Invocation.ActionId = PackedAction & 0x00FFFFFFu;
    Invocation.TargetGuid = InTargetGuid;

    if (Invocation.ActionId == 0)
    {
        return Invocation;
    }

    const uint8 ActionType = static_cast<uint8>((PackedAction >> 24) & 0xFF);
    switch (ActionType)
    {
    case ACTION_BUTTON_TYPE_SPELL:
        Invocation.Kind = (Invocation.ActionId == AUTO_ATTACK_SPELL_ID)
            ? EWowActionInvocationKind::AutoAttack
            : EWowActionInvocationKind::SpellCast;
        break;
    case 64:
        Invocation.Kind = EWowActionInvocationKind::Macro;
        break;
    case 128:
        Invocation.Kind = EWowActionInvocationKind::ItemUse;
        break;
    default:
        Invocation.Kind = EWowActionInvocationKind::None;
        break;
    }

    return Invocation;
}

bool UWowConnectionManager::UseActionSlot(int32 SlotIndex)
{
    if (SlotIndex < 0 || SlotIndex >= 144 || !PacketHandler.ActionButtons.IsValidIndex(SlotIndex))
    {
        return false;
    }

    const FWowActionInvocation Invocation =
        ResolveActionInvocation(PacketHandler.ActionButtons[SlotIndex], GetTargetGuid());
    if (!Invocation.IsValid())
    {
        return false;
    }

    switch (Invocation.Kind)
    {
    case EWowActionInvocationKind::SpellCast:
        SendCastSpell(static_cast<int32>(Invocation.ActionId), Invocation.TargetGuid);
        return true;

    case EWowActionInvocationKind::AutoAttack:
        if (Invocation.TargetGuid == 0)
        {
            UE_LOG(LogWowNet, Log, TEXT("UseActionSlot: slot %d auto-attack ignored with no target"), SlotIndex);
            return false;
        }

        SendAttackSwing(Invocation.TargetGuid);
        return true;

    case EWowActionInvocationKind::ItemUse:
        return UseItemByEntry(Invocation.ActionId);

    case EWowActionInvocationKind::Macro:
        UE_LOG(LogWowNet, Log, TEXT("UseActionSlot: macro use not implemented (slot=%d macro=%u)"),
            SlotIndex, Invocation.ActionId);
        return false;

    case EWowActionInvocationKind::None:
    default:
        return false;
    }
}

void UWowConnectionManager::BroadcastActionButtonsChanged()
{
    PacketHandler.OnActionButtonsUpdated.Broadcast();
}

void UWowConnectionManager::EnsureActionButtonCapacity(int32 SlotIndex)
{
    if (SlotIndex < 0)
    {
        return;
    }

    if (PacketHandler.ActionButtons.Num() <= SlotIndex)
    {
        PacketHandler.ActionButtons.SetNumZeroed(SlotIndex + 1);
    }
}

void UWowConnectionManager::SendAttackSwing(int64 InTargetGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 G = static_cast<uint64>(InTargetGuid);
    FMemory::Memcpy(Data.GetData(), &G, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_ATTACKSWING, Data);
    UE_LOG(LogWowNet, Log, TEXT("Attack swing on target %lld"), InTargetGuid);
}

void UWowConnectionManager::SendAttackStop()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::CMSG_ATTACKSTOP);
    UE_LOG(LogWowNet, Log, TEXT("Attack stop"));
}

void UWowConnectionManager::SendStandStateChange(uint32 StandState)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    AppendPacketValue(Data, StandState);
    WorldSocket->SendPacket(WowOpcode::CMSG_STANDSTATECHANGE, Data);
}

void UWowConnectionManager::SendSetSheathed(uint32 SheathState)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    AppendPacketValue(Data, SheathState);
    WorldSocket->SendPacket(WowOpcode::CMSG_SET_SHEATHED, Data);
}

void UWowConnectionManager::SendKeepAlive()
{
    if (!WorldSocket.IsValid()) return;
    WorldSocket->SendPacket(WowOpcode::CMSG_KEEP_ALIVE);
    // Revisit unresolved quest metadata even while the player stands still.
    // RequestItemInfo deduplicates and rate-limits; no full UI rebuild here.
    for(const auto& Quest : PacketHandler.QuestLog)
        for(const auto& Objective : Quest.Objectives)
            if(Objective.ItemId) RequestItemInfo(Objective.ItemId);
}

void UWowConnectionManager::SendLoot(int64 CorpseGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 Guid = static_cast<uint64>(CorpseGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_LOOT, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_LOOT for corpse %llu"), Guid);
}

void UWowConnectionManager::SendLootRelease(int64 CorpseGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 Guid = static_cast<uint64>(CorpseGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_LOOT_RELEASE, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_LOOT_RELEASE for corpse %llu"), Guid);
}

void UWowConnectionManager::SendLootItem(int32 LootSlot)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Add(static_cast<uint8>(LootSlot));

    WorldSocket->SendPacket(WowOpcode::CMSG_AUTOSTORE_LOOT_ITEM, Data);
    UE_LOG(LogWowNet, Log, TEXT("Loot item slot %d"), LootSlot);
}

void UWowConnectionManager::RequestItemInfo(uint32 ItemId)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    PacketHandler.RequestItemTemplate(ItemId);
}

void UWowConnectionManager::SendBuyItem(int64 VendorGuid, int32 ItemId, int32 Count, int32 VendorSlot)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || Count <= 0
        || PacketHandler.ActiveVendorGuid != static_cast<uint64>(VendorGuid)) return;
    const FWowVendorItem* Item = PacketHandler.VendorItems.FindByPredicate(
        [ItemId, VendorSlot](const FWowVendorItem& Row)
        { return Row.ItemId == static_cast<uint32>(ItemId) && (VendorSlot == 0 || Row.Slot == VendorSlot); });
    if (!Item || Item->MaxCount == 0) return;
    TArray<uint8> Data; Data.SetNumZeroed(21);
    const uint64 Guid = static_cast<uint64>(VendorGuid);
    const uint32 Id = Item->ItemId, Slot = Item->Slot, Quantity = static_cast<uint32>(Count);
    FMemory::Memcpy(Data.GetData(), &Guid, 8); FMemory::Memcpy(Data.GetData() + 8, &Id, 4);
    FMemory::Memcpy(Data.GetData() + 12, &Slot, 4); FMemory::Memcpy(Data.GetData() + 16, &Quantity, 4);
    WorldSocket->SendPacket(WowOpcode::CMSG_BUY_ITEM, Data);
}

void UWowConnectionManager::SendListInventory(int64 VendorGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    const uint64 Guid = static_cast<uint64>(VendorGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, sizeof(Guid));
    WorldSocket->SendPacket(WowOpcode::CMSG_LIST_INVENTORY, Data);
    UE_LOG(LogWowNet, Log, TEXT("Requested inventory from vendor %lld"), VendorGuid);
}

void UWowConnectionManager::SendSellItem(int64 VendorGuid, int64 ItemGuid, uint8 Count)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Reserve(17);

    // uint64 vendor_guid, uint64 item_guid, uint8 count
    uint64 VGuid = static_cast<uint64>(VendorGuid);
    Data.SetNumUninitialized(8);
    FMemory::Memcpy(Data.GetData(), &VGuid, 8);

    uint64 IGuid = static_cast<uint64>(ItemGuid);
    Data.SetNumUninitialized(Data.Num() + 8);
    FMemory::Memcpy(Data.GetData() + 8, &IGuid, 8);

    const uint32 Quantity = Count;
    Data.Append(reinterpret_cast<const uint8*>(&Quantity), sizeof(Quantity));

    WorldSocket->SendPacket(WowOpcode::CMSG_SELL_ITEM, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sell item %lld (count=%d) to vendor %lld"), ItemGuid, Count, VendorGuid);
}

void UWowConnectionManager::SendAutoEquipItem(uint8 SourceBag, uint8 SourceSlot)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame)
    {
        return;
    }

    TArray<uint8> Data;
    Data.Reserve(2);
    Data.Add(SourceBag);
    Data.Add(SourceSlot);

    WorldSocket->SendPacket(WowOpcode::CMSG_AUTOEQUIP_ITEM, Data);
    UE_LOG(LogWowNet, Log, TEXT("Auto equip item from bag=%u slot=%u"), SourceBag, SourceSlot);
}

void UWowConnectionManager::SendAutoStoreBagItem(uint8 SourceBag, uint8 SourceSlot, uint8 DestinationBag)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame)
    {
        return;
    }

    TArray<uint8> Data;
    Data.Reserve(3);
    Data.Add(SourceBag);
    Data.Add(SourceSlot);
    Data.Add(DestinationBag);

    WorldSocket->SendPacket(WowOpcode::CMSG_AUTOSTORE_BAG_ITEM, Data);
    UE_LOG(LogWowNet, Log, TEXT("Auto store item from bag=%u slot=%u into bag=%u"),
        SourceBag, SourceSlot, DestinationBag);
}

void UWowConnectionManager::SendSplitItem(uint8 SourceBag, uint8 SourceSlot, uint8 DestinationBag, uint8 DestinationSlot, uint32 Count)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame)
    {
        return;
    }

    TArray<uint8> Data;
    Data.Reserve(8);
    Data.Add(SourceBag);
    Data.Add(SourceSlot);
    Data.Add(DestinationBag);
    Data.Add(DestinationSlot);
    Data.Append(reinterpret_cast<const uint8*>(&Count), sizeof(Count));

    WorldSocket->SendPacket(WowOpcode::CMSG_SPLIT_ITEM, Data);
    UE_LOG(LogWowNet, Log, TEXT("Split item from bag=%u slot=%u into bag=%u slot=%u count=%u"),
        SourceBag, SourceSlot, DestinationBag, DestinationSlot, Count);
}

void UWowConnectionManager::SendDestroyItem(uint8 Bag, uint8 Slot, uint8 Count)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame)
    {
        return;
    }

    TArray<uint8> Data;
    Data.Reserve(6);
    Data.Add(Bag);
    Data.Add(Slot);
    Data.Add(Count);
    Data.Add(0);
    Data.Add(0);
    Data.Add(0);

    WorldSocket->SendPacket(WowOpcode::CMSG_DESTROYITEM, Data);
    UE_LOG(LogWowNet, Log, TEXT("Destroy item bag=%u slot=%u count=%u"), Bag, Slot, Count);
}

void UWowConnectionManager::SendInitiateTrade(int64 TargetGuidToTrade)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    const uint64 Guid = static_cast<uint64>(TargetGuidToTrade);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_INITIATE_TRADE, Data);
    PacketHandler.CurrentTrade = FWowTradeState{};
    PacketHandler.CurrentTrade.TraderGuid = Guid;
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_INITIATE_TRADE for player %llu"), Guid);
}

void UWowConnectionManager::SendBeginTrade()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::CMSG_BEGIN_TRADE, {});
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_BEGIN_TRADE"));
}

void UWowConnectionManager::SendAcceptTrade()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::CMSG_ACCEPT_TRADE, {});
    PacketHandler.CurrentTrade.bLocalAccepted = true;
    PacketHandler.OnTradeUpdated.Broadcast(PacketHandler.CurrentTrade);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_ACCEPT_TRADE"));
}

void UWowConnectionManager::SendUnacceptTrade()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::CMSG_UNACCEPT_TRADE, {});
    PacketHandler.CurrentTrade.bLocalAccepted = false;
    PacketHandler.OnTradeUpdated.Broadcast(PacketHandler.CurrentTrade);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_UNACCEPT_TRADE"));
}

void UWowConnectionManager::SendCancelTrade()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::CMSG_CANCEL_TRADE, {});
    PacketHandler.CurrentTrade.Status = WowTradeStatus::TRADE_CANCELED;
    PacketHandler.CurrentTrade.bTradeOpen = false;
    PacketHandler.CurrentTrade.bLocalAccepted = false;
    PacketHandler.CurrentTrade.bTargetAccepted = false;
    PacketHandler.CurrentTrade.PlayerMoney = 0;
    PacketHandler.CurrentTrade.TargetMoney = 0;
    PacketHandler.CurrentTrade.PlayerSpell = 0;
    PacketHandler.CurrentTrade.TargetSpell = 0;
    PacketHandler.CurrentTrade.PlayerItems.Reset();
    PacketHandler.CurrentTrade.TargetItems.Reset();
    PacketHandler.OnTradeUpdated.Broadcast(PacketHandler.CurrentTrade);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_CANCEL_TRADE"));
}

void UWowConnectionManager::SendSetTradeGold(int32 CopperAmount)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(4);
    const uint32 Gold = static_cast<uint32>(FMath::Max(0, CopperAmount));
    FMemory::Memcpy(Data.GetData(), &Gold, 4);

    WorldSocket->SendPacket(WowOpcode::CMSG_SET_TRADE_GOLD, Data);
    PacketHandler.CurrentTrade.PlayerMoney = Gold;
    PacketHandler.CurrentTrade.bLocalAccepted = false;
    PacketHandler.CurrentTrade.bTargetAccepted = false;
    PacketHandler.OnTradeUpdated.Broadcast(PacketHandler.CurrentTrade);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_SET_TRADE_GOLD amount=%u"), Gold);
}

void UWowConnectionManager::SendAcceptDuel(int64 ArbiterGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || ArbiterGuid == 0) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(sizeof(uint64));
    const uint64 Guid = static_cast<uint64>(ArbiterGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, sizeof(Guid));

    WorldSocket->SendPacket(WowOpcode::CMSG_DUEL_ACCEPTED, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_DUEL_ACCEPTED arbiter=%llu"), Guid);
}

void UWowConnectionManager::SendCancelDuel(int64 ArbiterGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || ArbiterGuid == 0) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(sizeof(uint64));
    const uint64 Guid = static_cast<uint64>(ArbiterGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, sizeof(Guid));

    WorldSocket->SendPacket(WowOpcode::CMSG_DUEL_CANCELLED, Data);

    if (PacketHandler.CurrentDuel.IsRequestPending())
    {
        PacketHandler.CurrentDuel.MarkInterrupted();
        PacketHandler.OnDuelUpdated.Broadcast(PacketHandler.CurrentDuel);
    }

    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_DUEL_CANCELLED arbiter=%llu"), Guid);
}

void UWowConnectionManager::SendRequestPetInfo()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::CMSG_REQUEST_PET_INFO, {});
    UE_LOG(LogWowNet, Verbose, TEXT("Sent CMSG_REQUEST_PET_INFO"));
}

void UWowConnectionManager::SendPetActionBarSlot(int32 SlotIndex, int64 InTargetGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    const FWowPetActionSlot* Slot = PacketHandler.PetActionBar.GetSlot(SlotIndex);
    if (!Slot || !Slot->IsUsable() || PacketHandler.PetActionBar.PetGuid == 0)
    {
        return;
    }

    uint64 Target = static_cast<uint64>(InTargetGuid != 0 ? InTargetGuid : TargetGuid);
    if (!Slot->IsSpell() && (!Slot->IsCommand() || Slot->ActionId != WowPetCommand::ATTACK))
    {
        Target = 0;
    }

    TArray<uint8> Data;
    Data.Reserve(sizeof(uint64) + sizeof(uint32) + sizeof(uint64));

    const uint64 PetGuid = PacketHandler.PetActionBar.PetGuid;
    const uint32 PackedData = Slot->PackedData;
    Data.Append(reinterpret_cast<const uint8*>(&PetGuid), sizeof(PetGuid));
    Data.Append(reinterpret_cast<const uint8*>(&PackedData), sizeof(PackedData));
    Data.Append(reinterpret_cast<const uint8*>(&Target), sizeof(Target));

    WorldSocket->SendPacket(WowOpcode::CMSG_PET_ACTION, Data);

    if (Slot->IsReaction())
    {
        PacketHandler.PetActionBar.ReactState = static_cast<uint8>(Slot->ActionId);
        PacketHandler.OnPetBarUpdated.Broadcast(PacketHandler.PetActionBar);
    }
    else if (Slot->IsCommand())
    {
        if (Slot->ActionId == WowPetCommand::FOLLOW || Slot->ActionId == WowPetCommand::STAY)
        {
            PacketHandler.PetActionBar.CommandState = static_cast<uint8>(Slot->ActionId);
            PacketHandler.OnPetBarUpdated.Broadcast(PacketHandler.PetActionBar);
        }
    }

    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_PET_ACTION slot=%d pet=%llu packed=0x%08X target=%llu"),
        SlotIndex, PetGuid, PackedData, Target);
}

void UWowConnectionManager::SendPetSpellAutocast(int32 SlotIndex, bool bEnabled)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    const FWowPetActionSlot* Slot = PacketHandler.PetActionBar.GetSlot(SlotIndex);
    if (!Slot || !Slot->IsAutocastCapable() || PacketHandler.PetActionBar.PetGuid == 0 || Slot->ActionId == 0)
    {
        return;
    }

    TArray<uint8> Data;
    Data.Reserve(sizeof(uint64) + sizeof(uint32) + sizeof(uint8));

    const uint64 PetGuid = PacketHandler.PetActionBar.PetGuid;
    Data.Append(reinterpret_cast<const uint8*>(&PetGuid), sizeof(PetGuid));
    Data.Append(reinterpret_cast<const uint8*>(&Slot->ActionId), sizeof(Slot->ActionId));
    Data.Add(bEnabled ? 1u : 0u);

    WorldSocket->SendPacket(WowOpcode::CMSG_PET_SPELL_AUTOCAST, Data);

    if (FWowPetActionSlot* MutableSlot = PacketHandler.PetActionBar.GetMutableSlot(SlotIndex))
    {
        MutableSlot->ActionType = bEnabled ? WowPetActionType::ENABLED : WowPetActionType::DISABLED;
        MutableSlot->PackedData = (MutableSlot->PackedData & 0x00FFFFFF) | (static_cast<uint32>(MutableSlot->ActionType) << 24);
        PacketHandler.OnPetBarUpdated.Broadcast(PacketHandler.PetActionBar);
    }

    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_PET_SPELL_AUTOCAST slot=%d spell=%u enabled=%d"),
        SlotIndex, Slot->ActionId, bEnabled ? 1 : 0);
}

void UWowConnectionManager::SendRequestRaidTargetIcons()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Add(0xFF);
    WorldSocket->SendPacket(WowOpcode::MSG_RAID_TARGET_UPDATE, Data);
    UE_LOG(LogWowNet, Verbose, TEXT("Requested current raid target icon list"));
}

void UWowConnectionManager::SendSetRaidTargetIcon(int32 IconIndex, int64 InTargetGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    if (IconIndex < 0 || IconIndex > 7) return;

    TArray<uint8> Data;
    Data.Reserve(9);

    const uint8 Icon = static_cast<uint8>(IconIndex);
    const uint64 Guid = static_cast<uint64>(InTargetGuid);
    Data.Add(Icon);
    Data.Append(reinterpret_cast<const uint8*>(&Guid), sizeof(Guid));

    WorldSocket->SendPacket(WowOpcode::MSG_RAID_TARGET_UPDATE, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent MSG_RAID_TARGET_UPDATE icon=%d guid=%llu"), IconIndex, Guid);
}

void UWowConnectionManager::SendStartReadyCheck()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::MSG_RAID_READY_CHECK, {});
    UE_LOG(LogWowNet, Log, TEXT("Sent MSG_RAID_READY_CHECK request"));
}

void UWowConnectionManager::SendReadyCheckConfirm(bool bReady)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Add(bReady ? WowReadyCheckResponse::READY : WowReadyCheckResponse::NOT_READY);

    WorldSocket->SendPacket(WowOpcode::MSG_RAID_READY_CHECK, Data);

    const uint64 LocalPlayerGuid = PacketHandler.EntityManager.LocalPlayerGuid;
    if (PacketHandler.ReadyCheck.bActive && LocalPlayerGuid != 0)
    {
        PacketHandler.ReadyCheck.SetResponse(LocalPlayerGuid, Data[0]);
        PacketHandler.OnReadyCheckUpdated.Broadcast(PacketHandler.ReadyCheck);
    }

    UE_LOG(LogWowNet, Log, TEXT("Sent MSG_RAID_READY_CHECK response ready=%d"), bReady ? 1 : 0);
}

void UWowConnectionManager::SendFinishReadyCheck()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::MSG_RAID_READY_CHECK_FINISHED, {});
    PacketHandler.ReadyCheck.Clear();
    PacketHandler.OnReadyCheckUpdated.Broadcast(PacketHandler.ReadyCheck);
    UE_LOG(LogWowNet, Log, TEXT("Sent MSG_RAID_READY_CHECK_FINISHED"));
}

void UWowConnectionManager::SendGossipHello(int64 NpcGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 Guid = static_cast<uint64>(NpcGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    if(const auto* Npc=PacketHandler.EntityManager.FindUnit(Guid))
    {
        const uint32 Flags=Npc->GetNpcFlags();
        if(!(Flags & 1))
        {
            if(Flags & WowNpcFlags::AUCTIONEER){WorldSocket->SendPacket(0x255,Data);return;}
            if(Flags & WowNpcFlags::TRAINER){RequestTrainer(Guid);return;}
        }
    }
    // Use CMSG_GOSSIP_HELLO — the server routes to questgiver/vendor/trainer
    // based on the NPC's flags automatically
    WorldSocket->SendPacket(WowOpcode::CMSG_GOSSIP_HELLO, Data);
    // r315 (Stefan 14.10: Gornek ignored many right-clicks before answering): the server only answers
    // within INTERACTION_DISTANCE of where it thinks we and the NPC are; log those distances.
    const auto* GossipNpc = PacketHandler.EntityManager.FindUnit(Guid);
    const FVector NpcPos = GossipNpc ? GossipNpc->Movement.Position : FVector::ZeroVector;
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_GOSSIP_HELLO for NPC %llu (to our last sent position %.1f yd, 2D %.1f, dz %.1f; sent %.1f s ago)"), Guid,
        GossipNpc ? FVector::Dist(NpcPos, GWowLastSentMovePosition) : -1.f,
        GossipNpc ? FVector::Dist2D(NpcPos, GWowLastSentMovePosition) : -1.f,
        GossipNpc ? NpcPos.Z - GWowLastSentMovePosition.Z : 0.f,
        GWowLastSentMoveSeconds > 0.0 ? FPlatformTime::Seconds() - GWowLastSentMoveSeconds : -1.0);
}

void UWowConnectionManager::SendGossipSelectOption(int64 NpcGuid, int32 MenuId, int32 OptionId, const FString& Code)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(16);
    const uint64 Guid = static_cast<uint64>(NpcGuid);
    const uint32 Menu = static_cast<uint32>(MenuId);
    const uint32 Option = static_cast<uint32>(OptionId);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);
    FMemory::Memcpy(Data.GetData() + 8, &Menu, 4);
    FMemory::Memcpy(Data.GetData() + 12, &Option, 4);
    if (!Code.IsEmpty())
    {
        const FTCHARToUTF8 Encoded(*Code);
        Data.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
        Data.Add(0);
    }
    WorldSocket->SendPacket(WowOpcode::CMSG_GOSSIP_SELECT_OPTION, Data);
    UE_LOG(LogWowNet, Log, TEXT("Selected gossip option %u from menu %u on NPC %llu"), Option, Menu, Guid);
}

void UWowConnectionManager::SendQuestStatusQuery(int64 QuestGiverGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    const uint64 Guid = static_cast<uint64>(QuestGiverGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);
    WorldSocket->SendPacket(WowOpcode::CMSG_QUESTGIVER_STATUS_QUERY, Data);
}

void UWowConnectionManager::SendQuestStatusMultipleQuery()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    WorldSocket->SendPacket(WowOpcode::CMSG_QUESTGIVER_STATUS_MULTIPLE_QUERY, {});
    UE_LOG(LogWowNet, Verbose, TEXT("Requested all visible quest giver statuses"));
}

void UWowConnectionManager::SendGameObjectUse(int64 GameObjectGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    const uint64 ObjectGuid = static_cast<uint64>(GameObjectGuid);
    const auto* Object = PacketHandler.EntityManager.Find(ObjectGuid);
    if (!Object || !Object->IsGameObject()) return;
    // Quest objects (GO_FLAG_INTERACT_COND) are usable only while the server activates them for us.
    if (!static_cast<const FWowGameObjectEntity*>(Object)->CanInteractNow()) return;
    const uint8 Type = Object->GetFieldByte(GameObjectField::BYTES_1,1);
    const uint32 Entry = Object->GetField(ObjectField::ENTRY);
    if (Type==3 || Type==10)
    {
        const uint32* Lock = PacketHandler.GameObjectLocks.Find(Entry);
        if (!Lock)
        {
            if (PendingObjectUse==ObjectGuid && FPlatformTime::Seconds()-PendingObjectUseAt<1.0) return;
            PendingObjectUse = ObjectGuid; PendingObjectUseAt = FPlatformTime::Seconds();
            TWeakObjectPtr<UWowConnectionManager> WeakThis(this);
            PacketHandler.OnGameObjectInfo = [WeakThis](uint32 ReceivedEntry)
            {
                if (!WeakThis.IsValid()) return;
                auto* Self = WeakThis.Get();
                const auto* Pending = Self->PacketHandler.EntityManager.Find(Self->PendingObjectUse);
                if (!Pending || Pending->GetField(ObjectField::ENTRY)!=ReceivedEntry) return;
                const uint64 Guid = Self->PendingObjectUse;
                Self->PendingObjectUse=0;
                if (FPlatformTime::Seconds()-Self->PendingObjectUseAt<5.0) Self->SendGameObjectUse(static_cast<int64>(Guid));
            };
            TArray<uint8> Query;
            AppendPacketValue(Query,Entry); AppendPacketValue(Query,ObjectGuid);
            WorldSocket->SendPacket(WowOpcode::CMSG_GAMEOBJECT_QUERY,Query);
            return;
        }
        PendingObjectUse=0;
        if (*Lock)
        {
            const auto& Spells=FDbcStore::Get().Spells();
            const auto* Cases=Spells.Locks.Find(*Lock);
            uint32 OpeningSpell=0;
            uint32 MissingSkillLock=0;
            if (Cases) for (const auto& Case : *Cases)
            {
                if (Case.Key==3) { OpeningSpell=Case.Value; break; }
                if (Case.Key!=2) continue;
                for (const auto& Spell : Spells.GetAll())
                {
                    if (!PacketHandler.KnownSpells.Contains(Spell.ID) && Spell.ID!=3365) continue;
                    for (int I=0; I<3; ++I)
                        if (Spell.Effect[I]==33 && Spell.EffectMiscValue[I]==int32(Case.Value))
                            { OpeningSpell=Spell.ID; break; }
                    if (OpeningSpell) break;
                }
                if (OpeningSpell) break;
                if (!MissingSkillLock) MissingSkillLock=Case.Value;
            }
            // Profession nodes without the profession: the client refuses with the red
            // "Requires Mining" / "Requires Herbalism" error instead of casting anything.
            const TCHAR* MissingSkill = MissingSkillLock==2 ? TEXT("Herbalism") : MissingSkillLock==3 ? TEXT("Mining")
                : MissingSkillLock==20 ? TEXT("Inscription") : MissingSkillLock==1 ? TEXT("Lockpicking") : nullptr;
            if (!OpeningSpell && MissingSkill)
            {
                PacketHandler.OnUiErrorMessage.Broadcast(FString::Printf(TEXT("Requires %s"), MissingSkill));
                return;
            }
            // Opening also handles item-key locks; the server validates ownership/skill.
            if (!OpeningSpell) OpeningSpell=3365;
            UE_LOG(LogWowNet,Log,TEXT("Open object %llu lock=%u spell=%u"),ObjectGuid,*Lock,OpeningSpell);
            SendCastSpell(OpeningSpell,GameObjectGuid);
            return;
        }
        if (Type==3) { SendLoot(GameObjectGuid); return; }
    }

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    const uint64 Guid = static_cast<uint64>(GameObjectGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_GAMEOBJ_USE, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_GAMEOBJ_USE for gameobject %llu"), Guid);
    // r427 (Stefan: right-clicking a plaque - Daelin Proudmoore's - does nothing): AzerothCore never
    // answers a readable object (GAMEOBJECT_TYPE_TEXT, 9) with SMSG_GAMEOBJECT_PAGETEXT; like the
    // original client we open it ourselves: the same path as that packet (first page from the
    // object's template, CMSG_PAGE_TEXT_QUERY, ItemTextFrame via ITEM_TEXT_BEGIN).
    if (Type == 9) PacketHandler.HandlePacket(WowOpcode::SMSG_GAMEOBJECT_PAGETEXT, Data);
}

void UWowConnectionManager::SendBankerActivate(int64 BankerGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 Guid = static_cast<uint64>(BankerGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_BANKER_ACTIVATE, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_BANKER_ACTIVATE for NPC %llu"), Guid);
}

void UWowConnectionManager::SendGetMailList(int64 MailboxGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    const uint64 Guid = static_cast<uint64>(MailboxGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_GET_MAIL_LIST, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_GET_MAIL_LIST for mailbox %llu"), Guid);
}

namespace
{
void MailAppendU8(TArray<uint8>& D, uint8 V) { D.Add(V); }
void MailAppendU32(TArray<uint8>& D, uint32 V) { D.Append(reinterpret_cast<const uint8*>(&V), 4); }
void MailAppendU64(TArray<uint8>& D, uint64 V) { D.Append(reinterpret_cast<const uint8*>(&V), 8); }
void MailAppendCString(TArray<uint8>& D, const FString& S)
{
    const FTCHARToUTF8 Utf8(*S);
    D.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    D.Add(0);
}
}

void UWowConnectionManager::SendMailTakeMoney(uint64 MailboxGuid, uint32 MailId)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    TArray<uint8> D; MailAppendU64(D, MailboxGuid); MailAppendU32(D, MailId);
    WorldSocket->SendPacket(WowOpcode::CMSG_MAIL_TAKE_MONEY, D);
}

void UWowConnectionManager::SendMailTakeItem(uint64 MailboxGuid, uint32 MailId, uint32 ItemGuidLow)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    TArray<uint8> D; MailAppendU64(D, MailboxGuid); MailAppendU32(D, MailId); MailAppendU32(D, ItemGuidLow);
    WorldSocket->SendPacket(WowOpcode::CMSG_MAIL_TAKE_ITEM, D);
}

void UWowConnectionManager::SendMailMarkAsRead(uint64 MailboxGuid, uint32 MailId)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    TArray<uint8> D; MailAppendU64(D, MailboxGuid); MailAppendU32(D, MailId);
    WorldSocket->SendPacket(WowOpcode::CMSG_MAIL_MARK_AS_READ, D);
}

void UWowConnectionManager::SendMailReturnToSender(uint64 MailboxGuid, uint32 MailId, uint64 SenderGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    TArray<uint8> D; MailAppendU64(D, MailboxGuid); MailAppendU32(D, MailId); MailAppendU64(D, SenderGuid);
    WorldSocket->SendPacket(WowOpcode::CMSG_MAIL_RETURN_TO_SENDER, D);
}

void UWowConnectionManager::SendMailDelete(uint64 MailboxGuid, uint32 MailId, uint32 MailTemplateId)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    TArray<uint8> D; MailAppendU64(D, MailboxGuid); MailAppendU32(D, MailId); MailAppendU32(D, MailTemplateId);
    WorldSocket->SendPacket(WowOpcode::CMSG_MAIL_DELETE, D);
}

void UWowConnectionManager::SendMail(uint64 MailboxGuid, const FString& Receiver, const FString& Subject, const FString& Body,
    const TArray<uint64>& ItemGuids, uint32 Money, uint32 COD)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    // u64 mailbox, cstr receiver, cstr subject, cstr body, u32 stationery (41 = default),
    // u32 unk (0), u8 itemCount, {u8 slot, u64 itemGuid}..., u32 money, u32 COD, u64 0, u8 0.
    TArray<uint8> D;
    MailAppendU64(D, MailboxGuid);
    MailAppendCString(D, Receiver);
    MailAppendCString(D, Subject);
    MailAppendCString(D, Body);
    MailAppendU32(D, 41);
    MailAppendU32(D, 0);
    const int32 Count = FMath::Min(ItemGuids.Num(), 12);
    MailAppendU8(D, static_cast<uint8>(Count));
    for (int32 Index = 0; Index < Count; ++Index) { MailAppendU8(D, static_cast<uint8>(Index)); MailAppendU64(D, ItemGuids[Index]); }
    MailAppendU32(D, Money);
    MailAppendU32(D, COD);
    MailAppendU64(D, 0);
    MailAppendU8(D, 0);
    WorldSocket->SendPacket(WowOpcode::CMSG_SEND_MAIL, D);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_SEND_MAIL to %s: %d items, money %u, COD %u"), *Receiver, Count, Money, COD);
}

uint64 UWowConnectionManager::GetCursorItemGuid() const
{
    return CursorPayload.Type == ECursorPayloadType::Item ? CursorPayload.ItemGuid : 0;
}

void UWowConnectionManager::SendQuestAccept(int64 QuestGiverGuid, int32 QuestId)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Reserve(16);

    // uint64 questgiver_guid, uint32 quest_id, uint32 unk1
    uint64 QGuid = static_cast<uint64>(QuestGiverGuid);
    Data.SetNumZeroed(16);
    FMemory::Memcpy(Data.GetData(), &QGuid, 8);

    uint32 QId = static_cast<uint32>(QuestId);
    FMemory::Memcpy(Data.GetData() + 8, &QId, 4);

    WorldSocket->SendPacket(WowOpcode::CMSG_QUESTGIVER_ACCEPT_QUEST, Data);
    UE_LOG(LogWowNet, Log, TEXT("Accept quest %d from questgiver %lld"), QuestId, QuestGiverGuid);
}

void UWowConnectionManager::SendQuestQuery(int64 QuestGiverGuid, int32 QuestId)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumZeroed(13);
    const uint64 Guid = static_cast<uint64>(QuestGiverGuid);
    const uint32 Id = static_cast<uint32>(QuestId);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);
    FMemory::Memcpy(Data.GetData() + 8, &Id, 4);
    Data[12] = 0; // unk1 in the 3.3.5a client packet
    WorldSocket->SendPacket(WowOpcode::CMSG_QUESTGIVER_QUERY_QUEST, Data);
    UE_LOG(LogWowNet, Log, TEXT("Query quest %d from questgiver %lld"), QuestId, QuestGiverGuid);
}

void UWowConnectionManager::SendQuestTemplateQuery(int32 QuestId)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || QuestId <= 0)
    {
        return;
    }

    const uint32 Id = static_cast<uint32>(QuestId);
    TArray<uint8> Data;
    Data.Append(reinterpret_cast<const uint8*>(&Id), sizeof(Id));
    PacketHandler.PendingQuestTemplateQueries.Add(Id);
    WorldSocket->SendPacket(WowOpcode::CMSG_QUEST_QUERY, Data);
    UE_LOG(LogWowNet, Verbose, TEXT("Requested quest template %u"), Id);
}

void UWowConnectionManager::SendQuestComplete(int64 QuestGiverGuid, int32 QuestId)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(12);
    const uint64 Guid = static_cast<uint64>(QuestGiverGuid);
    const uint32 Id = static_cast<uint32>(QuestId);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);
    FMemory::Memcpy(Data.GetData() + 8, &Id, 4);
    WorldSocket->SendPacket(WowOpcode::CMSG_QUESTGIVER_COMPLETE_QUEST, Data);
    UE_LOG(LogWowNet, Log, TEXT("Opened quest progress %d on questgiver %lld"), QuestId, QuestGiverGuid);
}

void UWowConnectionManager::SendQuestRequestReward(int64 QuestGiverGuid, int32 QuestId)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(12);
    const uint64 Guid = static_cast<uint64>(QuestGiverGuid);
    const uint32 Id = static_cast<uint32>(QuestId);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);
    FMemory::Memcpy(Data.GetData() + 8, &Id, 4);
    WorldSocket->SendPacket(WowOpcode::CMSG_QUESTGIVER_REQUEST_REWARD, Data);
    UE_LOG(LogWowNet, Log, TEXT("Request reward page for quest %d from questgiver %lld"), QuestId, QuestGiverGuid);
}

void UWowConnectionManager::SendQuestChooseReward(int64 QuestGiverGuid, int32 QuestId, int32 RewardChoice)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Reserve(16);

    // uint64 questgiver_guid, uint32 quest_id, uint32 reward_choice
    uint64 QGuid = static_cast<uint64>(QuestGiverGuid);
    Data.SetNumUninitialized(8);
    FMemory::Memcpy(Data.GetData(), &QGuid, 8);

    uint32 QId = static_cast<uint32>(QuestId);
    Data.SetNumUninitialized(Data.Num() + 4);
    FMemory::Memcpy(Data.GetData() + 8, &QId, 4);

    uint32 RChoice = static_cast<uint32>(RewardChoice);
    Data.SetNumUninitialized(Data.Num() + 4);
    FMemory::Memcpy(Data.GetData() + 12, &RChoice, 4);

    WorldSocket->SendPacket(WowOpcode::CMSG_QUESTGIVER_CHOOSE_REWARD, Data);
    UE_LOG(LogWowNet, Log, TEXT("Choose quest reward %d for quest %d from questgiver %lld"), RewardChoice, QuestId, QuestGiverGuid);
}

void UWowConnectionManager::SendRawPacket(uint32 Opcode, const TArray<uint8>& Data)
{
    if (!WorldSocket.IsValid()) return;
    WorldSocket->SendPacket(Opcode, Data);
}

void UWowConnectionManager::SendNameQuery(int64 Guid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 G = static_cast<uint64>(Guid);
    FMemory::Memcpy(Data.GetData(), &G, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_NAME_QUERY, Data);
    UE_LOG(LogWowNet, Verbose, TEXT("Sent name query for GUID %lld"), Guid);
}

void UWowConnectionManager::SendGetMirrorImageData(uint64 Guid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);
    WorldSocket->SendPacket(WowOpcode::CMSG_GET_MIRRORIMAGE_DATA, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent mirror image data request for GUID %llu"), Guid);
}

void UWowConnectionManager::SendCreatureQuery(int32 Entry, int64 Guid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Reserve(12);

    // Entry (uint32)
    uint32 E = static_cast<uint32>(Entry);
    Data.Append(reinterpret_cast<const uint8*>(&E), 4);

    // GUID (uint64)
    uint64 G = static_cast<uint64>(Guid);
    Data.Append(reinterpret_cast<const uint8*>(&G), 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_CREATURE_QUERY, Data);
    UE_LOG(LogWowNet, Verbose, TEXT("Sent creature query for Entry %d, GUID %lld"), Entry, Guid);
}

void UWowConnectionManager::SendTextEmote(int32 EmoteTextId, int64 InTargetGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Reserve(16);

    // EmoteTextId (uint32) — from EmotesText.dbc
    uint32 TextEmoteId = static_cast<uint32>(EmoteTextId);
    Data.Append(reinterpret_cast<const uint8*>(&TextEmoteId), 4);

    // EmoteNum (uint32) — always 0 for client-sent emotes
    uint32 EmoteNum = 0;
    Data.Append(reinterpret_cast<const uint8*>(&EmoteNum), 4);

    // TargetGuid (uint64) — 0 for untargeted emotes
    uint64 EmoteTargetGuid = static_cast<uint64>(InTargetGuid);
    Data.Append(reinterpret_cast<const uint8*>(&EmoteTargetGuid), 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_TEXT_EMOTE, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent text emote: EmoteTextId=%d, Target=%lld"), EmoteTextId, InTargetGuid);
}

void UWowConnectionManager::SendLearnTalent(int32 TalentId, int32 RequestedRank)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Reserve(8);

    // TalentId (uint32)
    uint32 TId = static_cast<uint32>(TalentId);
    Data.Append(reinterpret_cast<const uint8*>(&TId), 4);

    // RequestedRank (uint32)
    uint32 Rank = static_cast<uint32>(RequestedRank);
    Data.Append(reinterpret_cast<const uint8*>(&Rank), 4);

    WorldSocket->SendPacket(WowOpcode::CMSG_LEARN_TALENT, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent learn talent: TalentId=%d, Rank=%d"), TalentId, RequestedRank);
}

void UWowConnectionManager::SendLearnPreviewTalents(const TArray<FIntPoint>& TalentRanks)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || TalentRanks.IsEmpty()) return;
    TArray<uint8> Data;
    Data.Reserve(4 + TalentRanks.Num() * 8);
    auto WriteU32 = [&Data](uint32 Value) { Data.Append(reinterpret_cast<const uint8*>(&Value), 4); };
    WriteU32(static_cast<uint32>(TalentRanks.Num()));
    for (const FIntPoint& Pair : TalentRanks)
    {
        WriteU32(static_cast<uint32>(Pair.X));
        WriteU32(static_cast<uint32>(Pair.Y));
    }
    // 0x04C1 is CMSG_LEARN_PREVIEW_TALENTS in 3.3.5 (named LEARN_TALENTS_MULTIPLE here).
    WorldSocket->SendPacket(WowOpcode::CMSG_LEARN_TALENTS_MULTIPLE, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent learn preview talents: %d entries"), TalentRanks.Num());
}

void UWowConnectionManager::SendLfgJoin(uint8 Roles, const TArray<uint32>& DungeonEntries, const FString& Comment)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || DungeonEntries.IsEmpty()) return;

    TArray<uint8> Data;
    Data.Reserve(16 + DungeonEntries.Num() * 4 + Comment.Len());
    const uint32 PackedRoles = Roles;
    AppendPacketValue(Data, PackedRoles);
    Data.Add(0); // do not clear partial saved progress
    Data.Add(0); // achievement-based matching is unused by the 3.3.5 client
    const uint8 DungeonCount = static_cast<uint8>(FMath::Min(DungeonEntries.Num(), 255));
    Data.Add(DungeonCount);
    for (uint8 Index = 0; Index < DungeonCount; ++Index)
    {
        AppendPacketValue(Data, DungeonEntries[Index]);
    }
    Data.Add(3); // client need-preference slots
    Data.Add(0);
    Data.Add(0);
    Data.Add(0);
    AppendPacketString(Data, Comment.Left(255));

    PacketHandler.LfgState.Roles = Roles;
    PacketHandler.LfgState.SelectedDungeonEntries = DungeonEntries;
    PacketHandler.LfgState.Comment = Comment.Left(255);
    WorldSocket->SendPacket(WowOpcode::CMSG_LFG_JOIN, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_LFG_JOIN: roles=0x%02X entries=%d"), Roles, DungeonCount);
}

void UWowConnectionManager::SendLfgLeave()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    WorldSocket->SendPacket(WowOpcode::CMSG_LFG_LEAVE, {});
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_LFG_LEAVE"));
}

void UWowConnectionManager::SendLfgSetRoles(uint8 Roles)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    PacketHandler.LfgState.Roles = Roles;
    WorldSocket->SendPacket(WowOpcode::CMSG_LFG_SET_ROLES, { Roles });
}

void UWowConnectionManager::SendLfgSetComment(const FString& Comment)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    TArray<uint8> Data;
    AppendPacketString(Data, Comment.Left(255));
    PacketHandler.LfgState.Comment = Comment.Left(255);
    WorldSocket->SendPacket(WowOpcode::CMSG_SET_LFG_COMMENT, Data);
}

void UWowConnectionManager::SendLfgProposalResult(uint32 ProposalId, bool bAccept)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || ProposalId == 0) return;
    TArray<uint8> Data;
    AppendPacketValue(Data, ProposalId);
    Data.Add(bAccept ? 1 : 0);
    WorldSocket->SendPacket(WowOpcode::CMSG_LFG_PROPOSAL_RESULT, Data);
}

void UWowConnectionManager::SendLfgTeleport(bool bOut)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    WorldSocket->SendPacket(WowOpcode::CMSG_LFG_TELEPORT, { static_cast<uint8>(bOut ? 1 : 0) });
}

void UWowConnectionManager::SendLfgStatusRequest()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    WorldSocket->SendPacket(WowOpcode::CMSG_LFG_GET_STATUS, {});
}

void UWowConnectionManager::SendLfgPlayerLockInfoRequest()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    WorldSocket->SendPacket(WowOpcode::CMSG_LFD_PLAYER_LOCK_INFO_REQUEST, {});
}

void UWowConnectionManager::SendLfgPartyLockInfoRequest()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    WorldSocket->SendPacket(WowOpcode::CMSG_LFD_PARTY_LOCK_INFO_REQUEST, {});
}

void UWowConnectionManager::SendRaidBrowserSearch(uint32 RaidEntry)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || RaidEntry == 0) return;
    TArray<uint8> Data;
    AppendPacketValue(Data, RaidEntry);
    PacketHandler.LfgState.RaidSearchEntry = RaidEntry;
    PacketHandler.LfgState.Mode = EWowLfgMode::RaidSearch;
    WorldSocket->SendPacket(WowOpcode::CMSG_SEARCH_LFG_JOIN, Data);
}

void UWowConnectionManager::LeaveRaidBrowserSearch(uint32 RaidEntry)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || RaidEntry == 0) return;
    TArray<uint8> Data;
    AppendPacketValue(Data, RaidEntry);
    WorldSocket->SendPacket(WowOpcode::CMSG_SEARCH_LFG_LEAVE, Data);
    PacketHandler.LfgState.RaidSearchEntry = 0;
    PacketHandler.LfgState.RaidResults.Reset();
}

void UWowConnectionManager::SendGroupInviteByName(const FString& PlayerName)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || PlayerName.IsEmpty()) return;
    TArray<uint8> Data;
    AppendPacketString(Data, PlayerName.Left(48));
    AppendPacketValue(Data, uint32(0)); // 3.3.5 CMSG_GROUP_INVITE: name + uint32 (server reads both)
    WorldSocket->SendPacket(WowOpcode::CMSG_GROUP_INVITE, Data);
}

int32 UWowConnectionManager::GetWorldLatencyMs() const
{
    return WorldSocket.IsValid() ? WorldSocket->GetLatencyMs() : 0;
}

void UWowConnectionManager::HandleWorldDisconnected(const FString& Reason)
{
    if (!WorldSocket.IsValid()) return;
    Disconnect(); // joins worker and invalidates every queued callback from this session
    OnWorldDisconnected.Broadcast(Reason);
}

void UWowConnectionManager::Disconnect()
{
    PacketHandler.RealmClock = FWowRealmClock();
    if (WorldSocket.IsValid())
    {
        WorldSocket->Disconnect();
        WorldSocket.Reset();
    }

    if (AuthSocket.IsValid())
    {
        AuthSocket->Disconnect();
        AuthSocket.Reset();
    }

    PacketHandler.EntityManager.Clear();
    PacketHandler.GuildNames.Empty();
    PacketHandler.GuildQueryTimes.Empty();
    PacketHandler.NextGuildNameQuery = 0;
    PacketHandler.GuildRoster.Empty();
    PacketHandler.GuildName.Empty();
    PacketHandler.GuildMotd.Empty();
    PacketHandler.PlayerNameCache.Empty();
    PacketHandler.PlayerGenderCache.Empty();
    PacketHandler.PlayerClassCache.Empty();
    PacketHandler.CreatureNameCache.Empty();
    PacketHandler.GameObjectLocks.Empty();
    PacketHandler.OnGameObjectInfo = nullptr;
    PendingObjectUse = 0;
    PacketHandler.CreatureTitleCache.Empty();
    PacketHandler.QuestTitleCache.Empty();
    PacketHandler.QuestTemplateCache.Empty();
    PacketHandler.QuestObjectiveTextCache.Empty();
    PacketHandler.PendingQuestTemplateQueries.Empty();
    PacketHandler.QuestPOIs.Empty();
    PacketHandler.QuestPOIRequested.Empty();
    PacketHandler.SuperTrackedQuestId = 0;
    PacketHandler.EntityManager.LocalPlayerGuid = 0;
    SelectedCharacterGuid = 0;
    TargetGuid = 0;
    CursorPayload = FCursorPayloadState();
    PacketHandler.KnownSpells.Empty();
    PacketHandler.ActionButtons.Empty();
    PacketHandler.SpellCooldowns.Empty();
    PacketHandler.SpellCooldownWindows.Empty();
    PacketHandler.GlobalCooldownWindows.Empty();
    PacketHandler.LastGlobalCast=-100;
    PacketHandler.QuestLog.Empty();
    PacketHandler.ResetInventoryTracking();
    PendingEquipGuid = 0;
    DeleteConfirmationGuid = 0;
    PacketHandler.QuestGiverStatuses.Empty();
    PacketHandler.Talents.Empty();
    PacketHandler.TalentGroups.Empty();
    PacketHandler.TalentGlyphs.Empty();
    PacketHandler.ActiveTalentGroup = 0;
    PacketHandler.UnspentTalentPoints = 0;
    PacketHandler.bTalentInfoReceived = false;
    PacketHandler.PetTalents.Empty();
    ++PacketHandler.TalentRevision;
    PacketHandler.GroupInfo.Clear();
    PacketHandler.RaidTargets.Clear();
    PacketHandler.ReadyCheck.Clear();
    PacketHandler.PetActionBar.Clear();
    PacketHandler.CurrentDuel = FWowDuelState();
    PacketHandler.CurrentTrade = FWowTradeState();
    PacketHandler.LfgState = FWowLfgState();
    PacketHandler.OnSendPacket.Unbind();
    // Item IDs can have different names on different custom servers.
    PacketHandler.PendingItemQueries.Empty();
    PacketHandler.UnavailableItemQueries.Empty();
    PacketHandler.ItemTemplates.Empty();

    SessionKey.Empty();
    CachedRealms.Empty();
    CachedCharacters.Empty();
    CachedAccountName.Empty();

    SetState(EWowSessionState::Disconnected);
}

FString UWowConnectionManager::GetBindingCharacterIdentity() const
{
    const uint64 Guid=PacketHandler.EntityManager.LocalPlayerGuid;
    return Guid ? FString::Printf(TEXT("%s|%s|%u|%llu"),*BindingServerIdentity,*CachedAccountName,BindingRealmId,Guid) : FString();
}

int32 UWowConnectionManager::GetUnitReaction(const FWowEntity* From, const FWowEntity* To)
{
    if(!From || !To) return 4;
    const auto Controller=[](const FWowEntity* Entity)->uint64
    {
        if(Entity->IsUnit())
        {
            for(uint16 Field : {UnitField::CHARMEDBY,UnitField::SUMMONEDBY,UnitField::CREATEDBY})
                if(uint64 Guid=Entity->GetField64(Field)) return Guid;
        }
        return Entity->Guid;
    };
    const uint64 A=Controller(From), B=Controller(To);
    if(A && A==B) return 8;
    const auto InGroup=[this](uint64 Guid)
    {
        return Guid && (Guid==PacketHandler.EntityManager.LocalPlayerGuid || PacketHandler.GroupInfo.FindMember(Guid));
    };
    if(InGroup(A) && InGroup(B)) return 8;
    const auto Team=[this](uint64 Guid)
    {
        const auto* Entity=PacketHandler.EntityManager.Find(Guid);
        if(!Entity || !Entity->IsPlayer()) return 0;
        switch(static_cast<const FWowUnitEntity*>(Entity)->GetRaceId())
        {
            case 1: case 3: case 4: case 7: case 11: return 1;
            case 2: case 5: case 6: case 8: case 10: return 2;
            default: return 0;
        }
    };
    const int32 TeamA=Team(A),TeamB=Team(B);
    if(TeamA && TeamB) return TeamA==TeamB?8:2;
    if(!From->IsUnit() || !To->IsUnit()) return 4;
    // Direction matters: target -> player is the target frame's UnitReaction policy.
    return FDbcStore::Get().FactionTemplates().Reaction(
        static_cast<const FWowUnitEntity*>(From)->GetFactionTemplate(),
        static_cast<const FWowUnitEntity*>(To)->GetFactionTemplate());
}

void UWowConnectionManager::SendBuybackItem(uint64 VendorGuid, uint32 Slot, uint64 ExpectedItemGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || Slot < 74 || Slot >= 86 ||
        VendorGuid != PacketHandler.ActiveVendorGuid) return;
    const auto* Player = PacketHandler.EntityManager.GetLocalPlayer();
    if (!Player || !ExpectedItemGuid || Player->GetField64(PlayerField::BUYBACK_SLOT_START + (Slot - 74) * 2) != ExpectedItemGuid) return;
    TArray<uint8> Data;
    Data.Append(reinterpret_cast<const uint8*>(&VendorGuid), sizeof(VendorGuid));
    Data.Append(reinterpret_cast<const uint8*>(&Slot), sizeof(Slot));
    WorldSocket->SendPacket(WowOpcode::CMSG_BUYBACK_ITEM, Data);
}

void UWowConnectionManager::SendRepairItem(uint64 VendorGuid, uint64 ItemGuid, bool bGuildBank)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || !VendorGuid ||
        VendorGuid != PacketHandler.ActiveVendorGuid) return;
    // uint64 npc guid, uint64 item guid (0 = all), uint8 use guild bank
    TArray<uint8> Data;
    Data.Append(reinterpret_cast<const uint8*>(&VendorGuid), sizeof(VendorGuid));
    Data.Append(reinterpret_cast<const uint8*>(&ItemGuid), sizeof(ItemGuid));
    Data.Add(bGuildBank ? 1 : 0);
    WorldSocket->SendPacket(WowOpcode::CMSG_REPAIR_ITEM, Data);
}

int64 UWowConnectionManager::ResolveHelpfulSpellTarget(uint32 SpellId,int64 SelectedGuid)
{
    const auto* Spell=FDbcStore::Get().Spells().GetById(SpellId);
    if (!Spell) return SelectedGuid;
    bool ItemTarget=false;
    for(int I=0;I<3;++I)ItemTarget|=Spell->Effect[I]==53 || Spell->Effect[I]==54;
    if(ItemTarget)
    {
        if(const auto* Item=PacketHandler.EntityManager.FindItem(uint64(SelectedGuid)))
            if(Item->GetField64(ItemField::OWNER)==PacketHandler.EntityManager.LocalPlayerGuid)return SelectedGuid;
        // A single equipped weapon is unambiguous; dual wield requires choosing a hand.
        const uint64 Main=GetItemGuidAtPosition(255,15),Off=GetItemGuidAtPosition(255,16);
        bool OffWeapon=false;
        if(const auto* Item=PacketHandler.EntityManager.FindItem(Off))
            if(const auto* Info=PacketHandler.ItemTemplates.Find(Item->Entry))OffWeapon=Info->Class==2;
        return Main && !OffWeapon ? int64(Main) : -1;
    }
    bool Enemy=false,Ally=false,Self=false,Resurrect=false;
    for (int I=0;I<3;++I)
    {
        Resurrect|=Spell->Effect[I]==18 || Spell->Effect[I]==113;
        // Heals (HEAL 10, HEAL_MAX_HEALTH 67, HEAL_PCT 136, periodic-heal aura 8) are helpful
        // even when their target type is "any unit" (25), party (35) or raid (57).
        const bool bHealEffect=Spell->Effect[I]==10 || Spell->Effect[I]==67 || Spell->Effect[I]==136
            || ((Spell->Effect[I]==6 || Spell->Effect[I]==35) && Spell->EffectApplyAuraName[I]==8);
        for (uint32 Target : {Spell->EffectImplicitTargetA[I],Spell->EffectImplicitTargetB[I]})
        {
            Enemy|=Target==6;
            Ally|=Target==21 || Target==35 || Target==57 || (Target==25 && bHealEffect);
            Self|=Target==1;
        }
    }
    // r264 (Stefan 12.10: Healing Wave with a mob or a corpse targeted failed - "Target can't be
    // charmed" - while it worked with nothing targeted; the log shows it was sent at the mob): a spell
    // that heals is helpful whatever its target type says, unless it can only go on an enemy.
    bool bHeals=false,bHarms=false;
    for (int I=0;I<3;++I)
    {
        bHeals|=Spell->Effect[I]==10 || Spell->Effect[I]==67 || Spell->Effect[I]==136
            || ((Spell->Effect[I]==6 || Spell->Effect[I]==35) && Spell->EffectApplyAuraName[I]==8);
        bHarms|=Spell->Effect[I]==2 || Spell->Effect[I]==9 || ((Spell->Effect[I]==6) && Spell->EffectApplyAuraName[I]==3); // damage, leech, periodic damage
    }
    if (bHeals && !bHarms) Ally=true;
    {
        static TSet<uint32> Logged;
        if (!Logged.Contains(SpellId) && Logged.Num() < 200)
        {
            Logged.Add(SpellId);
            UE_LOG(LogWowNet, Log, TEXT("Spell %u target type: effects %u/%u/%u, targets A %u/%u/%u B %u/%u/%u -> enemy=%d ally=%d self=%d heals=%d"),
                SpellId, Spell->Effect[0], Spell->Effect[1], Spell->Effect[2],
                Spell->EffectImplicitTargetA[0], Spell->EffectImplicitTargetA[1], Spell->EffectImplicitTargetA[2],
                Spell->EffectImplicitTargetB[0], Spell->EffectImplicitTargetB[1], Spell->EffectImplicitTargetB[2],
                Enemy ? 1 : 0, Ally ? 1 : 0, Self ? 1 : 0, bHeals ? 1 : 0);
        }
    }
    // Like autoSelfCast: a helpful spell with an enemy, a corpse or nothing targeted lands on you.
    if (Ally) Enemy=false;
    auto& Entities=PacketHandler.EntityManager;
    const auto* Player=Entities.GetLocalPlayer();
    const auto* Target=Entities.Find(SelectedGuid);
    const bool FriendlyUnit=Player && Target && Target->IsUnit() && GetUnitReaction(Target,Player)>4;
    if (Resurrect)
        return FriendlyUnit && Target->IsPlayer() && Target->GetHealth()<=0 ? SelectedGuid : -1;
    if (Enemy) return SelectedGuid;
    if (Ally) return FriendlyUnit && Target->GetHealth()>0 ? SelectedGuid : int64(Entities.LocalPlayerGuid);
    if (Self) return int64(Entities.LocalPlayerGuid);
    return SelectedGuid;
}
bool UWowConnectionManager::CompleteSpellTarget(uint64 Guid)
{
    if (!PendingTargetSpell || !Guid) return false;
    const uint32 Spell=PendingTargetSpell;
    const int64 Resolved=ResolveHelpfulSpellTarget(Spell,Guid);
    if (Resolved<0) return false;
    if(PendingEnchantItemGuid)
    {
        if(GetItemGuidAtPosition(PendingEnchantBag,PendingEnchantSlot)!=PendingEnchantItemGuid){CancelSpellTarget();return false;}
        TArray<uint8>D;auto U32=[&](uint32 V){D.Append(reinterpret_cast<const uint8*>(&V),4);};
        D.Add(PendingEnchantBag);D.Add(PendingEnchantSlot);D.Add(0);U32(Spell);
        D.Append(reinterpret_cast<const uint8*>(&PendingEnchantItemGuid),8);U32(0);D.Add(0);U32(0x10);
        const uint64 ItemGuid=uint64(Resolved);uint8 Mask=0;TArray<uint8> Packed;
        for(int I=0;I<8;++I){uint8 B=uint8(ItemGuid>>(8*I));if(B){Mask|=1<<I;Packed.Add(B);}}
        D.Add(Mask);D.Append(Packed);
        if(!WorldSocket.IsValid()||State!=EWowSessionState::WorldInGame)return false;
        WorldSocket->SendPacket(0x0AB,D);CancelSpellTarget();return true;
    }
    SendCastSpell(Spell,Resolved);
    return PendingTargetSpell==0;
}

void UWowConnectionManager::RequestTrainer(uint64 Guid)
{
 if(!WorldSocket.IsValid()||State!=EWowSessionState::WorldInGame||!Guid)return;
 TArray<uint8>D;D.Append(reinterpret_cast<const uint8*>(&Guid),8);WorldSocket->SendPacket(0x1B0,D);
}
void UWowConnectionManager::BuyTrainerSpell(uint32 Spell)
{
 if(!WorldSocket.IsValid()||State!=EWowSessionState::WorldInGame||!PacketHandler.TrainerGuid)return;
 const auto* O=PacketHandler.TrainerOffers.FindByPredicate([Spell](const auto&V){return V.Spell==Spell&&V.State==0;});if(!O)return;
 TArray<uint8>D;D.Append(reinterpret_cast<const uint8*>(&PacketHandler.TrainerGuid),8);D.Append(reinterpret_cast<const uint8*>(&Spell),4);WorldSocket->SendPacket(0x1B2,D);
}
void UWowConnectionManager::SearchAuctions(const FString& Name,uint32 Offset,bool Own)
{
 if(!WorldSocket.IsValid()||State!=EWowSessionState::WorldInGame||!PacketHandler.AuctionGuid)return;
 TArray<uint8>D;auto U32=[&](uint32 V){D.Append(reinterpret_cast<const uint8*>(&V),4);};D.Append(reinterpret_cast<const uint8*>(&PacketHandler.AuctionGuid),8);U32(Offset);
 if(!Own){FTCHARToUTF8 Utf(*Name.Left(63));D.Append(reinterpret_cast<const uint8*>(Utf.Get()),Utf.Length());D.Add(0);D.Add(0);D.Add(0);for(int I=0;I<4;++I)U32(0xffffffff);D.Add(0);D.Add(0);D.Add(0);}
 WorldSocket->SendPacket(Own?0x259:0x258,D);
}
void UWowConnectionManager::BidAuction(uint32 Id,uint32 Price)
{
 if(!WorldSocket.IsValid()||State!=EWowSessionState::WorldInGame||!PacketHandler.AuctionGuid||!Price)return;
 const auto* O=PacketHandler.AuctionOffers.FindByPredicate([Id](const auto&V){return V.Id==Id;});
 if(!O)O=PacketHandler.AuctionBidOffers.FindByPredicate([Id](const auto&V){return V.Id==Id;});
 if(!O||O->Owner==PacketHandler.EntityManager.LocalPlayerGuid)return;
 TArray<uint8>D;D.Append(reinterpret_cast<const uint8*>(&PacketHandler.AuctionGuid),8);D.Append(reinterpret_cast<const uint8*>(&Id),4);D.Append(reinterpret_cast<const uint8*>(&Price),4);WorldSocket->SendPacket(0x25A,D);
}
void UWowConnectionManager::SearchAuctionsFiltered(const FString& Name,uint32 Offset,uint8 MinLevel,uint8 MaxLevel,uint32 InventoryType,uint32 ItemClass,uint32 SubClass,uint32 Quality,bool bUsable)
{
 if(!WorldSocket.IsValid()||State!=EWowSessionState::WorldInGame||!PacketHandler.AuctionGuid)return;
 TArray<uint8>D;auto U32=[&](uint32 V){D.Append(reinterpret_cast<const uint8*>(&V),4);};D.Append(reinterpret_cast<const uint8*>(&PacketHandler.AuctionGuid),8);U32(Offset);
 FTCHARToUTF8 Utf(*Name.Left(63));D.Append(reinterpret_cast<const uint8*>(Utf.Get()),Utf.Length());D.Add(0);
 D.Add(MinLevel);D.Add(MaxLevel);U32(InventoryType);U32(ItemClass);U32(SubClass);U32(Quality);D.Add(bUsable?1:0);D.Add(0);D.Add(0);
 WorldSocket->SendPacket(0x258,D);
}
void UWowConnectionManager::ListOwnerAuctions(uint32 Offset)
{
 if(!WorldSocket.IsValid()||State!=EWowSessionState::WorldInGame||!PacketHandler.AuctionGuid)return;
 TArray<uint8>D;D.Append(reinterpret_cast<const uint8*>(&PacketHandler.AuctionGuid),8);D.Append(reinterpret_cast<const uint8*>(&Offset),4);WorldSocket->SendPacket(0x259,D);
}
void UWowConnectionManager::ListBidderAuctions(uint32 Offset)
{
 if(!WorldSocket.IsValid()||State!=EWowSessionState::WorldInGame||!PacketHandler.AuctionGuid)return;
 // CMSG_AUCTION_LIST_BIDDER_ITEMS: guid, list from, outbid count (0 = no id list follows).
 const uint32 Zero=0;TArray<uint8>D;D.Append(reinterpret_cast<const uint8*>(&PacketHandler.AuctionGuid),8);D.Append(reinterpret_cast<const uint8*>(&Offset),4);D.Append(reinterpret_cast<const uint8*>(&Zero),4);WorldSocket->SendPacket(0x264,D);
}
bool UWowConnectionManager::SellAuctionItem(uint64 ItemGuid,uint32 Bid,uint32 Buyout,uint32 DurationMinutes)
{
 if(!WorldSocket.IsValid()||State!=EWowSessionState::WorldInGame||!PacketHandler.AuctionGuid||!ItemGuid||!Bid||(Buyout&&Buyout<Bid))return false;
 const auto* Item=PacketHandler.EntityManager.FindItem(ItemGuid);if(!Item)return false;
 TArray<uint8>D;auto U32=[&](uint32 V){D.Append(reinterpret_cast<const uint8*>(&V),4);};
 D.Append(reinterpret_cast<const uint8*>(&PacketHandler.AuctionGuid),8);U32(1);D.Append(reinterpret_cast<const uint8*>(&ItemGuid),8);U32(FMath::Max(1,int32(Item->GetStackCount())));U32(Bid);U32(Buyout);U32(DurationMinutes);
 WorldSocket->SendPacket(0x256,D);return true;
}

bool UWowConnectionManager::IsBagItemEquippable(uint8 Bag, uint8 Slot)
{
    const auto* Item = PacketHandler.EntityManager.FindItem(GetItemGuidAtPosition(Bag, Slot));
    const auto* Info = Item ? PacketHandler.ItemTemplates.Find(Item->Entry) : nullptr;
    // 0 = not equippable; 24 = ammo goes to the ammo slot via the paper doll only.
    return Info && Info->InventoryType != 0 && Info->InventoryType != 24;
}

bool UWowConnectionManager::FindPlayerItemByEntry(uint32 Entry, uint8& OutBag, uint8& OutSlot, bool bIncludeEquipped)
{
    if (!Entry) return false;
    auto Matches = [this, Entry](uint8 Bag, uint8 Slot)
    {
        const auto* Item = PacketHandler.EntityManager.FindItem(GetItemGuidAtPosition(Bag, Slot));
        return Item && Item->Entry == Entry;
    };
    if (bIncludeEquipped)
        for (uint8 Slot = 0; Slot < 19; ++Slot)
            if (Matches(255, Slot)) { OutBag = 255; OutSlot = Slot; return true; }
    for (uint8 Slot = 23; Slot < 39; ++Slot)
        if (Matches(255, Slot)) { OutBag = 255; OutSlot = Slot; return true; }
    for (uint8 Bag = 19; Bag <= 22; ++Bag)
        for (uint8 Slot = 0; Slot < 36; ++Slot)
            if (Matches(Bag, Slot)) { OutBag = Bag; OutSlot = Slot; return true; }
    return false;
}

int32 UWowConnectionManager::CountPlayerItems(uint32 Entry)
{
    int32 Total = 0;
    auto Add = [this, Entry, &Total](uint8 Bag, uint8 Slot)
    {
        if (const auto* Item = PacketHandler.EntityManager.FindItem(GetItemGuidAtPosition(Bag, Slot)))
            if (Item->Entry == Entry) Total += FMath::Max(1, Item->GetStackCount());
    };
    for (uint8 Slot = 23; Slot < 39; ++Slot) Add(255, Slot);
    for (uint8 Bag = 19; Bag <= 22; ++Bag)
        for (uint8 Slot = 0; Slot < 36; ++Slot) Add(Bag, Slot);
    return Total;
}

bool UWowConnectionManager::IsItemEntryEquipped(uint32 Entry)
{
    for (uint8 Slot = 0; Slot < 19; ++Slot)
        if (const auto* Item = PacketHandler.EntityManager.FindItem(GetItemGuidAtPosition(255, Slot)))
            if (Item->Entry == Entry) return true;
    return false;
}

uint32 UWowConnectionManager::GetItemUseSpell(uint32 Entry)
{
    const auto* Info = PacketHandler.ItemTemplates.Find(Entry);
    if (!Info) { if (Entry) PacketHandler.RequestItemTemplate(Entry); return 0; }
    // Spell triggers: 0 = on use, 5 = on use without delay, 6 = learn (recipes/patterns).
    for (int32 I = 0; I < 5; ++I)
        if (Info->SpellIds[I] && (Info->SpellTriggers[I] == 0 || Info->SpellTriggers[I] == 5 || Info->SpellTriggers[I] == 6))
            return Info->SpellIds[I];
    return 0;
}

bool UWowConnectionManager::IsItemActionable(uint32 Entry)
{
    const auto* Info = PacketHandler.ItemTemplates.Find(Entry);
    if (!Info) return false;
    constexpr uint32 ItemFlagHasLoot = 0x4;
    return GetItemUseSpell(Entry) != 0 || (Info->Flags & ItemFlagHasLoot) != 0
        || (Info->InventoryType != 0 && Info->InventoryType != 24);
}

bool UWowConnectionManager::UseItemByEntry(uint32 Entry)
{
    uint8 Bag = 0, Slot = 0;
    if (!FindPlayerItemByEntry(Entry, Bag, Slot, true))
    {
        UE_LOG(LogWowNet, Log, TEXT("Use item %u from action bar: none carried"), Entry);
        return false;
    }
    if (Bag != 255 || Slot >= 19)
    {
        if (UseWeaponEnchantItem(Bag, Slot)) return true;
        if (UseBagItem(Bag, Slot)) return true;
        // Gear on the action bar equips it, like the original client.
        if (IsBagItemEquippable(Bag, Slot)) return RequestEquipItem(Bag, Slot);
        return false;
    }
    return UseBagItem(Bag, Slot); // equipped on-use item (trinket, engineering gear)
}

bool UWowConnectionManager::UseBagItem(uint8 Bag, uint8 Slot)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return false;
    const uint64 ItemGuid = GetItemGuidAtPosition(Bag, Slot);
    const auto* Item = PacketHandler.EntityManager.FindItem(ItemGuid);
    if (!Item) return false;
    const auto* Info = PacketHandler.ItemTemplates.Find(Item->Entry);
    if (!Info) { PacketHandler.RequestItemTemplate(Item->Entry); return false; }

    const uint32 UseSpell = GetItemUseSpell(Item->Entry);

    if (!UseSpell)
    {
        constexpr uint32 ItemFlagHasLoot = 0x4; // clams, lockboxes, satchels
        if ((Info->Flags & ItemFlagHasLoot) == 0) return false;
        TArray<uint8> Open = {Bag, Slot};
        WorldSocket->SendPacket(0x0AC, Open); // CMSG_OPEN_ITEM
        UE_LOG(LogWowNet, Log, TEXT("Open item %u bag=%u slot=%u"), Item->Entry, Bag, Slot);
        return true;
    }

    // Unit-targeted item spells (quest items, bandages on others) use the current
    // target; everything else is cast on self (empty target mask). Like the client's
    // auto self-cast, a helpful item (scrolls, bandages, potions on others) goes on you
    // when the target is not friendly (Stefan 11.10: Scroll of Protection with a hostile
    // troll targeted failed instead of buffing him). Harmful items (enemy target 6) keep
    // the target.
    uint64 UnitTarget = 0;
    if (const auto* Spell = FDbcStore::Get().Spells().GetById(UseSpell))
    {
        bool bNeedsUnit = false, bOffensive = false;
        for (int32 I = 0; I < 3; ++I)
            for (uint32 Implicit : {Spell->EffectImplicitTargetA[I], Spell->EffectImplicitTargetB[I]})
            {
                bNeedsUnit |= Implicit == 6 || Implicit == 21 || Implicit == 25 || Implicit == 57;
                bOffensive |= Implicit == 6;
            }
        const auto* Target = PacketHandler.EntityManager.Find(static_cast<uint64>(TargetGuid));
        if (bNeedsUnit && Target && Target->IsUnit())
        {
            const bool bFriendlyTarget = GetUnitReaction(PacketHandler.EntityManager.GetLocalPlayer(), Target) >= 5;
            if (bOffensive || bFriendlyTarget) UnitTarget = static_cast<uint64>(TargetGuid);
        }
    }

    static uint8 ItemCastCounter = 0;
    TArray<uint8> D;
    auto U32 = [&D](uint32 V) { D.Append(reinterpret_cast<const uint8*>(&V), 4); };
    D.Add(Bag); D.Add(Slot); D.Add(ItemCastCounter++);
    U32(UseSpell);
    D.Append(reinterpret_cast<const uint8*>(&ItemGuid), 8);
    U32(0);   // glyph index
    D.Add(0); // cast flags
    if (UnitTarget)
    {
        U32(0x2); // TARGET_FLAG_UNIT
        uint8 Mask = 0; TArray<uint8> Packed;
        for (int32 I = 0; I < 8; ++I) { const uint8 B = uint8(UnitTarget >> (8 * I)); if (B) { Mask |= 1 << I; Packed.Add(B); } }
        D.Add(Mask); D.Append(Packed);
    }
    else U32(0); // self
    WorldSocket->SendPacket(0x0AB, D); // CMSG_USE_ITEM
    UE_LOG(LogWowNet, Log, TEXT("Use item %u spell %u bag=%u slot=%u target=%llu"), Item->Entry, UseSpell, Bag, Slot, UnitTarget);
    return true;
}

bool UWowConnectionManager::UseWeaponEnchantItem(uint8 Bag,uint8 Slot)
{
 const uint64 Guid=GetItemGuidAtPosition(Bag,Slot);const auto* Item=PacketHandler.EntityManager.FindItem(Guid);if(!Item)return false;
 const auto* Info=PacketHandler.ItemTemplates.Find(Item->Entry);if(!Info){PacketHandler.RequestItemTemplate(Item->Entry);return true;}
 for(int I=0;I<5;++I)if(Info->SpellIds[I]&&Info->SpellTriggers[I]==0)
 if(const auto*S=FDbcStore::Get().Spells().GetById(Info->SpellIds[I]))
 for(int J=0;J<3;++J)if(S->Effect[J]==54)
 {PendingTargetSpell=S->ID;PendingEnchantItemGuid=Guid;PendingEnchantBag=Bag;PendingEnchantSlot=Slot;return true;}
 return false;
}

bool UWowConnectionManager::SellAuctionCursor(uint32 Bid,uint32 Buyout)
{
 if(!WorldSocket.IsValid()||State!=EWowSessionState::WorldInGame||!PacketHandler.AuctionGuid||!Bid||(Buyout&&Buyout<Bid)||!CursorSourceIsValid())return false;
 const auto* Item=PacketHandler.EntityManager.FindItem(CursorPayload.ItemGuid);if(!Item)return false;
 TArray<uint8>D;auto U32=[&](uint32 V){D.Append(reinterpret_cast<const uint8*>(&V),4);};
 D.Append(reinterpret_cast<const uint8*>(&PacketHandler.AuctionGuid),8);U32(1);D.Append(reinterpret_cast<const uint8*>(&CursorPayload.ItemGuid),8);U32(Item->GetStackCount());U32(Bid);U32(Buyout);U32(720);
 WorldSocket->SendPacket(0x256,D);ClearCursorPayload();return true;
}
void UWowConnectionManager::CancelAuction(uint32 Id)
{
 if(!WorldSocket.IsValid()||State!=EWowSessionState::WorldInGame||!PacketHandler.AuctionGuid)return;
 const auto* O=PacketHandler.AuctionOwnOffers.FindByPredicate([Id](const auto&V){return V.Id==Id;});
 if(!O)O=PacketHandler.AuctionOffers.FindByPredicate([Id](const auto&V){return V.Id==Id;});
 if(!O||O->Owner!=PacketHandler.EntityManager.LocalPlayerGuid)return;
 TArray<uint8>D;D.Append(reinterpret_cast<const uint8*>(&PacketHandler.AuctionGuid),8);D.Append(reinterpret_cast<const uint8*>(&Id),4);WorldSocket->SendPacket(0x257,D);
}
