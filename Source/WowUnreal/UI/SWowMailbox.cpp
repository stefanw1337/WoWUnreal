#include "SWowMailbox.h"
#include "WowColor.h"
#include "WowTextCentering.h"
#include "WowWindowStyle.h"
#include "WowItemPresentation.h"
#include "WowInventoryManager.h"
#include "WowRetainedSlateBrush.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowAudioManager.h"
#include "WowNetwork/Public/WowConnectionManager.h"
#include "WowNetwork/Public/WowPacketHandler.h"
#include "WowUpdateFields.h"
#include "WowWorldManager.h"
#include "Mpq/MpqManager.h"
#include "Formats/DbcParser.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WowMailbox"

namespace
{
// MailFrame.lua constants.
constexpr int32 InboxItemsToDisplay = 7;
constexpr int32 AttachmentsMaxSend = 12;
constexpr int32 AttachmentsPerRow = 7;
constexpr uint32 MaxCodGold = 10000;

FLinearColor Srgb(uint8 R, uint8 G, uint8 B, uint8 A = 255) { return FLinearColor::FromSRGBColor(FColor(R, G, B, A)); }
FLinearColor NormalFontColor()    { return WowColor::FromWow(1.f, .82f, 0.f); }   // NORMAL_FONT_COLOR
FLinearColor HighlightFontColor() { return FLinearColor(1.f, 1.f, 1.f); }
FLinearColor GrayFontColor()      { return WowColor::FromWow(.5f, .5f, .5f); }
FLinearColor ReadFontColor()      { return WowColor::FromWow(.75f, .75f, .75f); }
FLinearColor GreenFontColor()     { return WowColor::FromWow(.1f, 1.f, .1f); }
FLinearColor RedFontColor()       { return WowColor::FromWow(1.f, .1f, .1f); }
FLinearColor MailTextColor()      { return Srgb(46, 31, 15); }               // MailTextFontNormal 0.18, 0.12, 0.06

FBox2f UV4(float U0, float U1, float V0, float V1) { return FBox2f(FVector2f(U0, V0), FVector2f(U1, V1)); }

// MailTemplate.dbc (3.3.5): ID, Subject[17] (localized), Body[17] (localized). Server mails
// with a template id carry no subject/body of their own; the client fills them from here.
struct FMailTemplateText { FString Subject, Body; };
const FMailTemplateText* FindMailTemplate(uint32 TemplateId)
{
    static TMap<uint32, FMailTemplateText> Templates;
    static bool bLoaded = false;
    if (!bLoaded)
        for (AWowWorldManager* WorldManager : TObjectRange<AWowWorldManager>())
            if (FMpqManager* Mpq = IsValid(WorldManager) && !WorldManager->IsTemplate() ? WorldManager->GetMpqManager() : nullptr)
            {
                bLoaded = true;
                TArray<uint8> Bytes;
                FDbcParser P;
                if (Mpq->ReadFile(TEXT("DBFilesClient\\MailTemplate.dbc"), Bytes) && P.Parse(Bytes) && P.GetFieldCount() >= 35)
                    for (uint32 I = 0; I < P.GetRecordCount(); ++I)
                        Templates.Add(P.GetUInt(I, 0), { P.GetLocalizedString(I, 1), P.GetLocalizedString(I, 18) });
                UE_LOG(LogTemp, Log, TEXT("Mail templates loaded: %d"), Templates.Num());
                break;
            }
    return Templates.Find(TemplateId);
}

FString FormatExpire(float DaysLeft, FLinearColor& OutColor)
{
    if (!FMath::IsFinite(DaysLeft)) DaysLeft = 0.f;
    if (DaysLeft >= 1.f)
    {
        OutColor = GreenFontColor();
        const int32 Days = FMath::FloorToInt(DaysLeft);
        return FString::Printf(TEXT("%d %s"), Days, Days == 1 ? TEXT("Day") : TEXT("Days"));
    }
    OutColor = RedFontColor();
    const int32 Minutes = FMath::Max(0, FMath::FloorToInt(DaysLeft * 24.f * 60.f));
    return Minutes >= 60 ? FString::Printf(TEXT("%d Hr %d Min"), Minutes / 60, Minutes % 60)
                         : FString::Printf(TEXT("%d Min"), Minutes);
}

FString CoinIcon(uint32 Copper)
{
    // GetCoinIcon (MoneyFrame.lua).
    if (Copper >= 10000) return TEXT("Interface\\Icons\\INV_Misc_Coin_01");
    if (Copper >= 100) return TEXT("Interface\\Icons\\INV_Misc_Coin_03");
    return TEXT("Interface\\Icons\\INV_Misc_Coin_05");
}

uint32 ParseDigits(const FString& Text)
{
    uint64 Value = 0;
    for (TCHAR C : Text) if (FChar::IsDigit(C)) Value = FMath::Min<uint64>(Value * 10 + (C - TEXT('0')), 0xFFFFFFFFull);
    return static_cast<uint32>(Value);
}
}

SWowMailbox::~SWowMailbox()
{
    if (ConnectionManager.IsValid())
    {
        ConnectionManager->PacketHandler.OnMailListReceived.Remove(MailListHandle);
        ConnectionManager->PacketHandler.OnMailResult.Remove(MailResultHandle);
        ConnectionManager->ContainerItemUseOverride = nullptr;
    }
}

void SWowMailbox::Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager)
{
    ConnectionManager = InConnectionManager;
    UIManager = InArgs._UIManager;
    SendAttachments.Init(0, AttachmentsMaxSend);
    if (UIManager.IsValid() && UIManager->GetFrameManager() && UIManager->GetFrameManager()->GetUIScale() > 0.f)
        Scale = UIManager->GetFrameManager()->GetUIScale();

    if (ConnectionManager.IsValid())
    {
        MailListHandle = ConnectionManager->PacketHandler.OnMailListReceived.AddSP(this, &SWowMailbox::OnMailListReceived);
        MailResultHandle = ConnectionManager->PacketHandler.OnMailResult.AddSP(this, &SWowMailbox::OnMailResult);
        // Right-clicking a bag item while Send Mail is shown attaches it.
        TWeakPtr<SWowMailbox> Weak = SharedThis(this);
        ConnectionManager->ContainerItemUseOverride = [Weak](uint64 ItemGuid)
        {
            const TSharedPtr<SWowMailbox> Self = Weak.Pin();
            return Self.IsValid() && Self->bVisible && Self->Tab == ETab::Send && Self->AttachItem(ItemGuid);
        };
    }

    ChildSlot
    [
        SAssignNew(Root, SBox)
        .WidthOverride_Lambda([this]() { return FrameW * Scale; })
        .HeightOverride_Lambda([this]() { return FrameH * Scale; })
    ];
    SAssignNew(OpenMailRoot, SBox)
        .WidthOverride_Lambda([this]() { return FrameW * Scale; })
        .HeightOverride_Lambda([this]() { return FrameH * Scale; });

    SetVisibility(EVisibility::Collapsed);
    OpenMailRoot->SetVisibility(EVisibility::Collapsed);
    Rebuild();
}

// ── Visibility ────────────────────────────────────────────────────────────

void SWowMailbox::Show(uint64 MailboxGuid)
{
    const bool bWasVisible = bVisible;
    CurrentMailboxGuid = MailboxGuid;
    bVisible = true;
    if (!bWasVisible)
    {
        Tab = ETab::Inbox;
        PageNum = 0;
        OpenMailId = 0;
        PlayUiSound(TEXT("igSpellBookOpen"));
    }
    SetVisibility(EVisibility::SelfHitTestInvisible);
    bWaitingForMailList = true;
    RequestMailList();
    Rebuild();
    RebuildOpenMail();
}

void SWowMailbox::Close()
{
    if (!bVisible) return;
    bVisible = false;
    OpenMailId = 0;
    PendingCodAttachment = INDEX_NONE;
    bPendingDeleteConfirm = bPendingSendConfirm = false;
    CurrentMailboxGuid = 0;
    SetVisibility(EVisibility::Collapsed);
    OpenMailRoot->SetVisibility(EVisibility::Collapsed);
    // Unsent attachments go back to the bags (they never left them).
    SendAttachments.Init(0, AttachmentsMaxSend);
    PlayUiSound(TEXT("igSpellBookClose"));
}

void SWowMailbox::ToggleVisibility()
{
    if (bVisible) Close();
    else if (CurrentMailboxGuid) Show(CurrentMailboxGuid);
}

bool SWowMailbox::IsVisible() const
{
    return bVisible;
}

void SWowMailbox::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    if (!bVisible) return;
    bool bDirty = false;
    if (UIManager.IsValid() && UIManager->GetFrameManager())
    {
        const float NewScale = UIManager->GetFrameManager()->GetUIScale();
        if (NewScale > 0.f && !FMath::IsNearlyEqual(NewScale, Scale)) { Scale = NewScale; bDirty = true; }
    }
    // Money, item names/icons arriving and attachments leaving the bags change the layout.
    const uint32 Stamp = ComputeStateStamp();
    if (Stamp != LastStateStamp) { LastStateStamp = Stamp; bDirty = true; }
    if (bDirty) { Rebuild(); RebuildOpenMail(); }
}

uint32 SWowMailbox::ComputeStateStamp() const
{
    uint32 Stamp = GetPlayerMoney();
    UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Connection) return Stamp;
    for (uint64 Guid : SendAttachments)
    {
        Stamp = HashCombine(Stamp, GetTypeHash(Guid));
        if (Guid)
            if (const FWowItemEntity* Item = Connection->PacketHandler.EntityManager.FindItem(Guid))
            {
                Stamp = HashCombine(Stamp, static_cast<uint32>(Item->GetStackCount()));
                Stamp = HashCombine(Stamp, Connection->PacketHandler.ItemTemplates.Contains(Item->Entry) ? 1u : 0u);
            }
    }
    for (const FWowMailMessage& Mail : CurrentMail)
        for (const FWowMailAttachment& Attachment : Mail.Attachments)
            Stamp = HashCombine(Stamp, Connection->PacketHandler.ItemTemplates.Contains(Attachment.ItemEntry) ? 3u : 5u);
    return Stamp;
}

// ── Network ───────────────────────────────────────────────────────────────

void SWowMailbox::RequestMailList()
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Connection || CurrentMailboxGuid == 0) { bWaitingForMailList = false; return; }
    Connection->SendGetMailList(static_cast<int64>(CurrentMailboxGuid));
}

void SWowMailbox::OnMailListReceived(const TArray<FWowMailMessage>& InMail)
{
    CurrentMail = InMail;
    TotalMailCount = ConnectionManager.IsValid() ? ConnectionManager->PacketHandler.MailInboxTotalCount : InMail.Num();
    // Template mails (server rewards, system mail) arrive without subject and body (Stefan 10.10:
    // an empty "Open Mail"). Fill them from MailTemplate.dbc like the client does.
    for (FWowMailMessage& Mail : CurrentMail)
    {
        if (Mail.MailTemplateId && (Mail.Subject.IsEmpty() || Mail.Body.IsEmpty()))
            if (const FMailTemplateText* Template = FindMailTemplate(Mail.MailTemplateId))
            {
                if (Mail.Subject.IsEmpty()) Mail.Subject = Template->Subject;
                if (Mail.Body.IsEmpty()) Mail.Body = Template->Body;
            }
        UE_LOG(LogTemp, Log, TEXT("Mail %u: type %u sender %llu/%u template %u days %.3f subject %d body %d items %d money %u cod %u"),
            Mail.MessageId, uint32(Mail.MessageType), Mail.SenderGuid, Mail.SenderEntry, Mail.MailTemplateId,
            double(Mail.DaysLeft), Mail.Subject.Len(), Mail.Body.Len(), Mail.Attachments.Num(), Mail.Money, Mail.COD);
    }
    bWaitingForMailList = false;
    if (UWowConnectionManager* Connection = ConnectionManager.Get())
        for (const FWowMailMessage& Mail : CurrentMail)
        {
            for (const FWowMailAttachment& Attachment : Mail.Attachments)
                if (!Connection->PacketHandler.ItemTemplates.Contains(Attachment.ItemEntry))
                    Connection->PacketHandler.RequestItemTemplate(Attachment.ItemEntry);
            if (Mail.MessageType == WowMailMessageType::NORMAL && Mail.SenderGuid
                && !Connection->PacketHandler.PlayerNameCache.Contains(Mail.SenderGuid))
                Connection->SendNameQuery(static_cast<int64>(Mail.SenderGuid));
            if (Mail.MessageType == WowMailMessageType::CREATURE && Mail.SenderEntry
                && !Connection->PacketHandler.CreatureNameCache.Contains(Mail.SenderEntry))
                Connection->SendCreatureQuery(static_cast<int32>(Mail.SenderEntry), 0);
        }
    const int32 MaxPage = FMath::Max(0, (CurrentMail.Num() - 1) / InboxItemsToDisplay);
    PageNum = FMath::Clamp(PageNum, 0, MaxPage);
    if (OpenMailId && !FindMail(OpenMailId)) OpenMailId = 0;
    Rebuild();
    RebuildOpenMail();
}

void SWowMailbox::OnMailResult(uint32 MailId, uint32 Action, uint32 Error, uint32 EquipError)
{
    if (!bVisible) return;
    if (Action == 0) // MAIL_SEND
    {
        bSending = false;
        if (Error == 0) { ResetSendForm(); PlayUiSound(TEXT("igSpellBookClose")); }
        Rebuild();
        return;
    }
    if (Error == 0)
    {
        if (FWowMailMessage* Mail = CurrentMail.FindByPredicate([MailId](const FWowMailMessage& M) { return M.MessageId == MailId; }))
        {
            if (Action == 1) Mail->Money = 0;                                  // MAIL_MONEY_TAKEN
            if (Action == 2) { Mail->COD = 0; }                                // MAIL_ITEM_TAKEN (list refresh removes it)
        }
        if (Action == 3 || Action == 4)                                        // returned / deleted
        {
            CurrentMail.RemoveAll([MailId](const FWowMailMessage& M) { return M.MessageId == MailId; });
            if (OpenMailId == MailId) OpenMailId = 0;
        }
    }
    // The server owns the attachment list; refresh after every change.
    RequestMailList();
    Rebuild();
    RebuildOpenMail();
}

// ── Data helpers ──────────────────────────────────────────────────────────

const FWowMailMessage* SWowMailbox::FindMail(uint32 MailId) const
{
    return CurrentMail.FindByPredicate([MailId](const FWowMailMessage& M) { return M.MessageId == MailId; });
}

FString SWowMailbox::GetSenderName(const FWowMailMessage& Mail) const
{
    const UWowConnectionManager* Connection = ConnectionManager.Get();
    switch (Mail.MessageType)
    {
    case WowMailMessageType::NORMAL:
        if (Connection)
            if (const FString* Name = Connection->PacketHandler.PlayerNameCache.Find(Mail.SenderGuid); Name && !Name->IsEmpty())
                return *Name;
        break;
    case WowMailMessageType::CREATURE:
        if (Connection)
            if (const FString* Name = Connection->PacketHandler.CreatureNameCache.Find(Mail.SenderEntry); Name && !Name->IsEmpty())
                return *Name;
        break;
    case WowMailMessageType::AUCTION:
        return TEXT("Auction House");
    default:
        break;
    }
    return TEXT("Unknown");
}

FString SWowMailbox::GetStationeryTexture(const FWowMailMessage& Mail) const
{
    // Stationery.dbc texture names for the stationery ids servers send.
    switch (Mail.Stationery)
    {
    case 61: case 67: return TEXT("Stationery_ill");
    case 62: return TEXT("AuctionHouse");
    case 64: return TEXT("Stationery_Val");
    case 65: return TEXT("Stationery_Chr");
    default: return TEXT("StationeryTest");
    }
}

FString SWowMailbox::GetPackageIcon(const FWowMailMessage& Mail) const
{
    if (!Mail.Attachments.IsEmpty())
    {
        const FString Path = FWowItemPresentation::IconPath(Mail.Attachments[0].ItemEntry);
        if (!Path.IsEmpty()) return Path;
    }
    return Mail.Stationery == 61 || Mail.Stationery == 67 ? TEXT("Interface\\Icons\\INV_Misc_Note_03")
                                                          : TEXT("Interface\\Icons\\INV_Misc_Note_01");
}

bool SWowMailbox::CanDelete(const FWowMailMessage& Mail) const
{
    // InboxItemCanDelete: player mail that still carries items or money goes back to the sender.
    const bool bCarries = Mail.Money > 0 || !Mail.Attachments.IsEmpty();
    return !(Mail.MessageType == WowMailMessageType::NORMAL && bCarries && !(Mail.Checked & WowMailCheckMask::RETURNED));
}

uint32 SWowMailbox::GetPlayerMoney() const
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    const FWowPlayerEntity* Me = Connection ? Connection->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
    return Me ? Me->GetField(PlayerField::COINAGE) : 0;
}

uint32 SWowMailbox::GetSendMoneyCopper() const
{
    const uint64 Copper = uint64(ParseDigits(MoneyGold)) * 10000 + uint64(FMath::Min(ParseDigits(MoneySilver), 99u)) * 100
        + FMath::Min(ParseDigits(MoneyCopper), 99u);
    return static_cast<uint32>(FMath::Min<uint64>(Copper, 0xFFFFFFFFull));
}

int32 SWowMailbox::CountSendAttachments() const
{
    int32 Count = 0;
    for (uint64 Guid : SendAttachments) if (Guid) ++Count;
    return Count;
}

uint32 SWowMailbox::GetPostage() const
{
    // GetSendMailPrice: 30 copper per item, at least one stamp.
    return 30u * static_cast<uint32>(FMath::Max(1, CountSendAttachments()));
}

void SWowMailbox::PlayUiSound(const TCHAR* Sound) const
{
    UWorld* World = UIManager.IsValid() ? UIManager->GetWorld() : nullptr;
    if (AWowAudioManager* Audio = World ? Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(World, AWowAudioManager::StaticClass())) : nullptr)
        Audio->PlayUiSound(Sound);
}

// ── Actions ───────────────────────────────────────────────────────────────

void SWowMailbox::SetTab(ETab NewTab)
{
    if (Tab == NewTab) return;
    Tab = NewTab;
    if (Tab == ETab::Send) OpenMailId = 0; // MailFrameTab_OnClick hides OpenMailFrame
    PlayUiSound(TEXT("igMainMenuOptionCheckBoxOn"));
    Rebuild();
    RebuildOpenMail();
}

void SWowMailbox::OpenMail(uint32 MailId)
{
    if (OpenMailId == MailId) { CloseOpenMail(); return; }
    OpenMailId = MailId;
    PendingCodAttachment = INDEX_NONE;
    bPendingDeleteConfirm = false;
    if (FWowMailMessage* Mail = CurrentMail.FindByPredicate([MailId](const FWowMailMessage& M) { return M.MessageId == MailId; }))
        if (!(Mail->Checked & WowMailCheckMask::READ))
        {
            Mail->Checked |= WowMailCheckMask::READ;
            if (ConnectionManager.IsValid()) ConnectionManager->SendMailMarkAsRead(CurrentMailboxGuid, MailId);
        }
    PlayUiSound(TEXT("igSpellBookOpen"));
    Rebuild();
    RebuildOpenMail();
}

void SWowMailbox::CloseOpenMail()
{
    if (!OpenMailId) return;
    // OpenMailFrame_OnHide: a letter whose text copy was made and that carries nothing is deleted.
    if (const FWowMailMessage* Mail = FindMail(OpenMailId))
        if (Mail->Money == 0 && Mail->Attachments.IsEmpty() && (Mail->Checked & WowMailCheckMask::COPIED) && ConnectionManager.IsValid())
            ConnectionManager->SendMailDelete(CurrentMailboxGuid, Mail->MessageId, Mail->MailTemplateId);
    OpenMailId = 0;
    PendingCodAttachment = INDEX_NONE;
    bPendingDeleteConfirm = false;
    PlayUiSound(TEXT("igSpellBookClose"));
    Rebuild();
    RebuildOpenMail();
}

void SWowMailbox::TakeAttachment(int32 AttachmentIndex)
{
    const FWowMailMessage* Mail = FindMail(OpenMailId);
    UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Mail || !Connection || !Mail->Attachments.IsValidIndex(AttachmentIndex)) return;
    if (Mail->COD > 0 && PendingCodAttachment != AttachmentIndex)
    {
        if (Mail->COD > GetPlayerMoney())
        {
            Connection->PacketHandler.OnUiErrorMessage.Broadcast(TEXT("You do not have enough money to pay the C.O.D. charges."));
            return;
        }
        PendingCodAttachment = AttachmentIndex; // COD_CONFIRMATION
        RebuildOpenMail();
        return;
    }
    PendingCodAttachment = INDEX_NONE;
    Connection->SendMailTakeItem(CurrentMailboxGuid, Mail->MessageId, Mail->Attachments[AttachmentIndex].ItemGuidLow);
    PlayUiSound(TEXT("igMainMenuOptionCheckBoxOn"));
    RebuildOpenMail();
}

void SWowMailbox::TakeMoney()
{
    const FWowMailMessage* Mail = FindMail(OpenMailId);
    if (!Mail || !ConnectionManager.IsValid() || Mail->Money == 0) return;
    ConnectionManager->SendMailTakeMoney(CurrentMailboxGuid, Mail->MessageId);
    PlayUiSound(TEXT("LOOTWINDOWCOINSOUND"));
}

void SWowMailbox::DeleteOrReturnOpenMail(bool bConfirmed)
{
    const FWowMailMessage* Mail = FindMail(OpenMailId);
    UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Mail || !Connection) return;
    if (CanDelete(*Mail))
    {
        if (!bConfirmed && (Mail->Money > 0 || !Mail->Attachments.IsEmpty()))
        {
            bPendingDeleteConfirm = true; // DELETE_MAIL / DELETE_MONEY
            RebuildOpenMail();
            return;
        }
        Connection->SendMailDelete(CurrentMailboxGuid, Mail->MessageId, Mail->MailTemplateId);
    }
    else
    {
        Connection->SendMailReturnToSender(CurrentMailboxGuid, Mail->MessageId, Mail->SenderGuid);
    }
    bPendingDeleteConfirm = false;
    OpenMailId = 0;
    Rebuild();
    RebuildOpenMail();
}

void SWowMailbox::ReplyToOpenMail()
{
    const FWowMailMessage* Mail = FindMail(OpenMailId);
    if (!Mail || Mail->MessageType != WowMailMessageType::NORMAL) return;
    SendTo = GetSenderName(*Mail);
    SendSubject = Mail->Subject.StartsWith(TEXT("RE: ")) ? Mail->Subject : TEXT("RE: ") + Mail->Subject;
    OpenMailId = 0;
    Tab = ETab::Send;
    Rebuild();
    RebuildOpenMail();
}

bool SWowMailbox::AttachItem(uint64 ItemGuid, int32 Slot)
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Connection || !ItemGuid || SendAttachments.Contains(ItemGuid)) return Connection && ItemGuid != 0;
    if (!Connection->PacketHandler.EntityManager.FindItem(ItemGuid)) return false;
    if (!SendAttachments.IsValidIndex(Slot) || SendAttachments[Slot] != 0)
    {
        Slot = SendAttachments.IndexOfByKey(0ull);
        if (Slot == INDEX_NONE)
        {
            Connection->PacketHandler.OnUiErrorMessage.Broadcast(TEXT("You cannot attach more than 12 items to mail."));
            return true;
        }
    }
    SendAttachments[Slot] = ItemGuid;
    UpdateAutoSubject();
    PlayUiSound(TEXT("igMainMenuOptionCheckBoxOn"));
    Rebuild();
    return true;
}

void SWowMailbox::ClickSendAttachment(int32 Slot)
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Connection || !SendAttachments.IsValidIndex(Slot)) return;
    if (const uint64 CursorItem = Connection->GetCursorItemGuid())
    {
        // Dropping the held item: it stays in its bag slot and is attached by reference.
        Connection->ClearCursorPayload();
        AttachItem(CursorItem, Slot);
        return;
    }
    if (SendAttachments[Slot])
    {
        SendAttachments[Slot] = 0;
        UpdateAutoSubject();
        Rebuild();
    }
}

void SWowMailbox::UpdateAutoSubject()
{
    // SendMailFrame_Update: the subject follows the first attachment until the player types one.
    UWowConnectionManager* Connection = ConnectionManager.Get();
    FString Title;
    if (Connection)
        for (uint64 Guid : SendAttachments)
            if (const FWowItemEntity* Item = Guid ? Connection->PacketHandler.EntityManager.FindItem(Guid) : nullptr)
            {
                const FString Name = FWowItemPresentation::Name(Connection, Item->Entry);
                Title = Item->GetStackCount() > 1 ? FString::Printf(TEXT("%s (%d)"), *Name, Item->GetStackCount()) : Name;
                break;
            }
    if (SendSubject.IsEmpty() || SendSubject == AutoSubject) SendSubject = Title;
    AutoSubject = Title;
    if (CountSendAttachments() == 0) bSendCod = false;
}

void SWowMailbox::SendMail(bool bConfirmed)
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Connection || bSending) return;
    const FString To = SendTo.TrimStartAndEnd();
    if (To.IsEmpty()) return;
    const uint32 Copper = GetSendMoneyCopper();
    if (bSendCod && Copper > MaxCodGold * 10000)
    {
        Connection->PacketHandler.OnUiErrorMessage.Broadcast(FString::Printf(TEXT("C.O.D. must be <= %u"), MaxCodGold));
        return;
    }
    if (!bSendCod && Copper > 0 && !bConfirmed)
    {
        bPendingSendConfirm = true; // SEND_MONEY
        Rebuild();
        return;
    }
    bPendingSendConfirm = false;
    TArray<uint64> Items;
    for (uint64 Guid : SendAttachments) if (Guid && Connection->PacketHandler.EntityManager.FindItem(Guid)) Items.Add(Guid);
    FString Subject = SendSubject;
    if (Subject.IsEmpty()) Subject = AutoSubject;
    bSending = true;
    Connection->SendMail(CurrentMailboxGuid, To, Subject, SendBody, Items, bSendCod ? 0 : Copper, bSendCod ? Copper : 0);
    Rebuild();
}

void SWowMailbox::ResetSendForm()
{
    SendTo.Reset(); SendSubject.Reset(); SendBody.Reset(); AutoSubject.Reset();
    MoneyGold.Reset(); MoneySilver.Reset(); MoneyCopper.Reset();
    bSendCod = false;
    bPendingSendConfirm = false;
    SendAttachments.Init(0, AttachmentsMaxSend);
}

// ── Widget helpers ────────────────────────────────────────────────────────

FSlateFontInfo SWowMailbox::Fnt(float WowPixels) const
{
    // WoW font heights are pixels; Slate sizes are points (x 0.75).
    return FWowWindowStyle::TitleFont(FMath::Max(6, FMath::RoundToInt(WowPixels * Scale * .75f)));
}

float SWowMailbox::TextWidth(const FString& Text, const FSlateFontInfo& Font) const
{
    if (!FSlateApplication::IsInitialized()) return Text.Len() * Font.Size * .6f;
    return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font).X;
}

void SWowMailbox::Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const
{
    Canvas.AddSlot().Position(FVector2D(X, Y) * Scale).Size(FVector2D(W, H) * Scale)[Widget];
}

const FSlateBrush* SWowMailbox::LoadBrush(const FString& TexturePath, FBox2f UV) const
{
    if (TexturePath.IsEmpty()) return nullptr;
    const FString Key = FString::Printf(TEXT("%s|%.4f,%.4f,%.4f,%.4f"), *TexturePath, UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y);
    if (const TSharedPtr<FWowRetainedSlateBrush>* Existing = TextureBrushes.Find(Key)) return Existing->Get();
    const double Now = FPlatformTime::Seconds();
    if (const double* Failed = FailedTextureTimes.Find(Key); Failed && Now - *Failed < 2.0) return nullptr;
    UTexture2D* Texture = UIManager.IsValid() && UIManager->GetFrameManager()
        ? UIManager->GetFrameManager()->LoadUITexture(TexturePath) : nullptr;
    if (!Texture) { FailedTextureTimes.Add(Key, Now); return nullptr; }
    FailedTextureTimes.Remove(Key);
    TSharedPtr<FWowRetainedSlateBrush> Brush = MakeShared<FWowRetainedSlateBrush>();
    Brush->SetResourceObject(Texture);
    Brush->DrawAs = ESlateBrushDrawType::Image;
    Brush->SetUVRegion(UV);
    Brush->ImageSize = FVector2D(Texture->GetSizeX() * (UV.Max.X - UV.Min.X), Texture->GetSizeY() * (UV.Max.Y - UV.Min.Y));
    TextureBrushes.Add(Key, Brush);
    return Brush.Get();
}

const FButtonStyle* SWowMailbox::TextureButtonStyle(const FString& Up, const FString& Down, const FString& Disabled, FBox2f UV) const
{
    const FString Key = Up + TEXT("|") + Down + TEXT("|") + Disabled + FString::Printf(TEXT("|%.3f%.3f%.3f%.3f"), UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y);
    if (const TSharedPtr<FButtonStyle>* Found = ButtonStyles.Find(Key)) return Found->Get();
    const FSlateBrush* Normal = LoadBrush(Up, UV);
    if (!Normal) return nullptr;
    const FSlateBrush* Pressed = Down.IsEmpty() ? nullptr : LoadBrush(Down, UV);
    const FSlateBrush* Off = Disabled.IsEmpty() ? nullptr : LoadBrush(Disabled, UV);
    TSharedPtr<FButtonStyle> Style = MakeShared<FButtonStyle>(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"));
    Style->SetNormal(*Normal);
    FSlateBrush Hovered = *Normal;
    Hovered.TintColor = WowColor::FromWow(1.2f, 1.2f, 1.2f);
    Style->SetHovered(Hovered);
    Style->SetPressed(Pressed ? *Pressed : *Normal);
    Style->SetDisabled(Off ? *Off : *Normal);
    Style->SetNormalPadding(FMargin(0));
    Style->SetPressedPadding(FMargin(0));
    ButtonStyles.Add(Key, Style);
    return Style.Get();
}

TSharedRef<SWidget> SWowMailbox::Img(const FString& Path, FBox2f UV, FLinearColor Tint) const
{
    return SNew(SImage).Visibility(EVisibility::HitTestInvisible).ColorAndOpacity(Tint)
        .Image_Lambda([this, Path, UV]() { return LoadBrush(Path, UV); });
}

TSharedRef<SWidget> SWowMailbox::Label(const FText& Text, float WowPixels, FLinearColor Color, ETextJustify::Type Justify) const
{
    return SNew(STextBlock).Text(Text).Font(Fnt(WowPixels)).ColorAndOpacity(Color).Justification(Justify)
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis).Visibility(EVisibility::HitTestInvisible)
        .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black);
}

TSharedRef<SWidget> SWowMailbox::PanelButton(const FText& Text, float W, float H, FOnClicked OnClicked, TAttribute<bool> Enabled) const
{
    // UIPanelButtonTemplate: red button art, GameFontNormal (gold, grey when disabled).
    return SNew(SBox).WidthOverride(W * Scale).HeightOverride(H * Scale)
        [
            SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center)
            .ContentPadding(FMargin(2 * Scale, 0)).IsEnabled(Enabled).OnClicked(OnClicked)
            [ SNew(STextBlock).Text(Text).Font(Fnt(12)).OverflowPolicy(ETextOverflowPolicy::Clip).RenderTransform(WowCapCenterTransform(Fnt(12)))
              .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
        ];
}

TSharedRef<SWidget> SWowMailbox::CloseButton(FOnClicked OnClicked) const
{
    const FButtonStyle* Style = TextureButtonStyle(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"),
        TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Down"));
    if (!Style) Style = &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder");
    return SNew(SButton).ButtonStyle(Style).ContentPadding(0).ClickMethod(EButtonClickMethod::MouseDown).OnClicked(OnClicked);
}

TSharedRef<SWidget> SWowMailbox::InputBorder() const
{
    // Common-Input-Border: 8px caps (TexCoords 0-.0625 / .9375-1, height .625).
    return SNew(SHorizontalBox).Visibility(EVisibility::HitTestInvisible)
        + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(8 * Scale)
            [ Img(TEXT("Interface\\Common\\Common-Input-Border"), UV4(0, .0625f, 0, .625f)) ] ]
        + SHorizontalBox::Slot().FillWidth(1)[ Img(TEXT("Interface\\Common\\Common-Input-Border"), UV4(.0625f, .9375f, 0, .625f)) ]
        + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(8 * Scale)
            [ Img(TEXT("Interface\\Common\\Common-Input-Border"), UV4(.9375f, 1, 0, .625f)) ] ];
}

TSharedRef<SWidget> SWowMailbox::TextField(FString& Value, int32 MaxLetters, float WowPixels, bool bNumeric)
{
    FString* Target = &Value;
    return SNew(SEditableText)
        .Text_Lambda([Target]() { return FText::FromString(*Target); })
        .Font(Fnt(WowPixels))
        .ColorAndOpacity(HighlightFontColor())
        .SelectAllTextWhenFocused(bNumeric)
        .ClearKeyboardFocusOnCommit(true)
        .OnTextChanged_Lambda([this, Target, MaxLetters, bNumeric](const FText& NewText)
        {
            FString Text = NewText.ToString();
            if (bNumeric) { FString Digits; for (TCHAR C : Text) if (FChar::IsDigit(C)) Digits.AppendChar(C); Text = Digits; }
            *Target = Text.Left(MaxLetters);
        });
}

TSharedRef<SWidget> SWowMailbox::MoneyText(uint32 Copper, float WowPixels, FLinearColor Color) const
{
    // SmallMoneyFrameTemplate: amounts with 13px coin icons; zero shows "0" copper.
    TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox).Visibility(EVisibility::HitTestInvisible);
    auto Add = [this, &Row, WowPixels, Color](uint32 Value, const TCHAR* Icon)
    {
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(4 * Scale, 0, 1 * Scale, 0))
            [ SNew(STextBlock).Text(FText::AsNumber(Value)).Font(Fnt(WowPixels)).ColorAndOpacity(Color)
              .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ];
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(13 * Scale).HeightOverride(13 * Scale)[ Img(Icon) ] ];
    };
    if (Copper >= 10000) Add(Copper / 10000, TEXT("Interface\\MoneyFrame\\UI-GoldIcon"));
    if (Copper >= 100) Add(Copper / 100 % 100, TEXT("Interface\\MoneyFrame\\UI-SilverIcon"));
    if (Copper % 100 || Copper < 100) Add(Copper % 100, TEXT("Interface\\MoneyFrame\\UI-CopperIcon"));
    return Row;
}

TSharedRef<SWidget> SWowMailbox::ItemButton(TAttribute<const FSlateBrush*> Icon, uint32 Count, TSharedPtr<SToolTip> Tip,
    FOnClicked OnClicked, FLinearColor IconTint) const
{
    // ItemButtonTemplate: 37x37 icon, NumberFontNormal count bottom right, square highlight.
    TSharedRef<SButton> Button = SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(0)
        .ClickMethod(EButtonClickMethod::MouseDown).OnClicked(OnClicked)
        [
            SNew(SOverlay)
            + SOverlay::Slot()[ SNew(SImage).Image(Icon).ColorAndOpacity(IconTint) ]
            + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(FMargin(0, 0, 4 * Scale, 2 * Scale))
            [ SNew(STextBlock).Text(Count > 1 ? FText::AsNumber(Count) : FText::GetEmpty()).Font(Fnt(14))
              .ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
        ];
    TWeakPtr<SButton> WeakButton = Button;
    return SNew(SOverlay).ToolTip(Tip)
        + SOverlay::Slot()[ Button ]
        + SOverlay::Slot()
        [
            SNew(SWowAddImage).Visibility(EVisibility::HitTestInvisible)
            .Image_Lambda([this]() { return LoadBrush(TEXT("Interface\\Buttons\\ButtonHilight-Square")); })
            .ColorAndOpacity_Lambda([WeakButton]()
            {
                const TSharedPtr<SButton> B = WeakButton.Pin();
                return FLinearColor(1, 1, 1, B.IsValid() && B->IsHovered() ? .8f : 0.f);
            })
        ];
}

// ── Layout ────────────────────────────────────────────────────────────────

void SWowMailbox::BuildFrameArt(SCanvas& Canvas, bool bOpenMail) const
{
    // MailFrame: Mail-Icon portrait under UI-ItemText / Spellbook pages.
    // OpenMailFrame: ClassTrainer top pages, UI-OpenMail-BotLeft, ClassTrainer bottom right.
    if (bOpenMail)
    {
        Place(Canvas, 9, 6, 60, 60, Img(TEXT("Interface\\MailFrame\\Mail-Icon")));
        Place(Canvas, 0, 0, 256, 256, Img(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-TopLeft")));
        Place(Canvas, 256, 0, 128, 256, Img(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-TopRight")));
        Place(Canvas, 0, 256, 256, 256, Img(TEXT("Interface\\MailFrame\\UI-OpenMail-BotLeft")));
        Place(Canvas, 256, 256, 128, 256, Img(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-BotRight")));
    }
    else if (Tab == ETab::Send)
    {
        // MailFrameTab_OnClick(2): ClassTrainer quadrants, MailFrameTopLeft at TOPLEFT 2,-1.
        Place(Canvas, 10, 8, 58, 58, Img(TEXT("Interface\\MailFrame\\Mail-Icon")));
        Place(Canvas, 2, 1, 256, 256, Img(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-TopLeft")));
        Place(Canvas, 258, 1, 128, 256, Img(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-TopRight")));
        Place(Canvas, 2, 257, 256, 256, Img(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-BotLeft")));
        Place(Canvas, 258, 257, 128, 256, Img(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-BotRight")));
    }
    else
    {
        Place(Canvas, 10, 8, 58, 58, Img(TEXT("Interface\\MailFrame\\Mail-Icon")));
        Place(Canvas, 0, 0, 256, 256, Img(TEXT("Interface\\ItemTextFrame\\UI-ItemText-TopLeft")));
        Place(Canvas, 256, 0, 128, 256, Img(TEXT("Interface\\Spellbook\\UI-SpellbookPanel-TopRight")));
        Place(Canvas, 0, 256, 256, 256, Img(TEXT("Interface\\ItemTextFrame\\UI-ItemText-BotLeft")));
        Place(Canvas, 256, 256, 128, 256, Img(TEXT("Interface\\Spellbook\\UI-SpellbookPanel-BotRight")));
    }
}

void SWowMailbox::PlaceLetterScroll(SCanvas& Canvas, float Height, float TextX, float TextW, TSharedRef<SWidget> Content)
{
    // UIPanelScrollFrameTemplate at TOPLEFT 21,-97 (296 wide): the bar sits 6 px right of the
    // frame between its up/down buttons; UI-Character-ScrollBar art is the trough behind it.
    const float X = 21.f, Y = 97.f, W = 296.f;
    const float TopH = FMath::Min(Height, 256.f);
    Place(Canvas, X + W - 2, Y - 5, 31, TopH, Img(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar"), UV4(0, .484375f, 0, TopH / 256.f)));
    Place(Canvas, X + W - 2, Y + Height + 2 - 106, 31, 106, Img(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar"), UV4(.515625f, 1, 0, .4140625f)));

    if (!LetterScrollStyle.IsValid())
    {
        LetterScrollStyle = MakeShared<FScrollBarStyle>(FCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>("ScrollBar"));
        static const FSlateNoResource NoTrack;
        LetterScrollStyle->SetVerticalBackgroundImage(NoTrack).SetVerticalTopSlotImage(NoTrack).SetVerticalBottomSlotImage(NoTrack)
            .SetHorizontalBackgroundImage(NoTrack).SetHorizontalTopSlotImage(NoTrack).SetHorizontalBottomSlotImage(NoTrack);
    }
    if (const FSlateBrush* Knob = LoadBrush(TEXT("Interface\\Buttons\\UI-ScrollBar-Knob"), UV4(.2f, .8f, .125f, .875f)))
        LetterScrollStyle->SetNormalThumbImage(*Knob).SetHoveredThumbImage(*Knob).SetDraggedThumbImage(*Knob);
    TSharedRef<SScrollBar> Bar = SNew(SScrollBar).Orientation(Orient_Vertical).Style(LetterScrollStyle.Get())
        .Thickness(FVector2D(16 * Scale, 16 * Scale)).Padding(0).AlwaysShowScrollbar(true);
    TSharedRef<SScrollBox> Box = SNew(SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible)
        .ExternalScrollbar(Bar).ScrollBarVisibility(EVisibility::Collapsed)
        + SScrollBox::Slot().Padding(FMargin(TextX * Scale, 10 * Scale, 0, 10 * Scale))
        [ SNew(SBox).WidthOverride(TextW * Scale)[ Content ] ];
    Place(Canvas, X, Y, W, Height, Box);

    TWeakPtr<SScrollBox> WeakBox = Box;
    auto Arrow = [this, WeakBox](const TCHAR* Name, float Step)
    {
        const FString Base = FString::Printf(TEXT("Interface\\Buttons\\UI-ScrollBar-Scroll%sButton-"), Name);
        const FButtonStyle* Style = TextureButtonStyle(Base + TEXT("Up"), Base + TEXT("Down"), Base + TEXT("Disabled"), UV4(.2f, .8f, .25f, .75f));
        if (!Style) Style = &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder");
        return SNew(SButton).ButtonStyle(Style).ContentPadding(0).ClickMethod(EButtonClickMethod::MouseDown)
            .IsEnabled_Lambda([WeakBox, Step]()
            {
                const TSharedPtr<SScrollBox> B = WeakBox.Pin();
                if (!B.IsValid()) return false;
                return Step < 0 ? B->GetScrollOffset() > .5f : B->GetScrollOffset() < B->GetScrollOffsetOfEnd() - .5f;
            })
            .OnClicked_Lambda([WeakBox, Step]()
            {
                if (const TSharedPtr<SScrollBox> B = WeakBox.Pin())
                    B->SetScrollOffset(FMath::Clamp(B->GetScrollOffset() + Step, 0.f, B->GetScrollOffsetOfEnd()));
                return FReply::Handled();
            });
    };
    const float BarX = X + W + 6.f;
    Place(Canvas, BarX, Y, 16, 16, Arrow(TEXT("Up"), -20.f * Scale));
    Place(Canvas, BarX, Y + 16, 16, FMath::Max(1.f, Height - 32), Bar);
    Place(Canvas, BarX, Y + Height - 16, 16, 16, Arrow(TEXT("Down"), 20.f * Scale));
}

void SWowMailbox::Rebuild()
{
    if (!Root.IsValid()) return;
    LastStateStamp = ComputeStateStamp();
    TSharedRef<SCanvas> Canvas = SNew(SCanvas);
    BuildFrameArt(*Canvas, false);
    if (Tab == ETab::Inbox) BuildInbox(*Canvas);
    else BuildSend(*Canvas);
    BuildTabs(*Canvas);
    // InboxCloseButton: CENTER at TOPRIGHT -45,-25.
    Place(*Canvas, 323, 9, 32, 32, CloseButton(FOnClicked::CreateLambda([this]() { Close(); return FReply::Handled(); })));

    // The frame swallows clicks inside its art (HitRectInsets right 35, bottom 75).
    Root->SetContent(
        SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SBox).Padding(FMargin(0, 0, 35 * Scale, 75 * Scale))
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
                .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
            ]
        ]
        + SOverlay::Slot()[ Canvas ]);
}

void SWowMailbox::BuildTabs(SCanvas& Canvas)
{
    // MailFrameTab1/2 (FriendsFrameTabTemplate): Tab1 BOTTOMLEFT 24,44; Tab2 8px overlapped.
    const FSlateFontInfo TabFont = Fnt(10);
    const FText Labels[2] = { LOCTEXT("InboxTab", "Inbox"), LOCTEXT("SendMailTab", "Send Mail") };
    float X = 24.f;
    const float Y = FrameH - 44.f - 32.f;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const bool bSelected = (Index == 0) == (Tab == ETab::Inbox);
        const float W = TextWidth(Labels[Index].ToString(), TabFont) / Scale + 40.f;
        const FString Art = bSelected ? TEXT("Interface\\PaperDollInfoFrame\\UI-Character-ActiveTab")
                                      : TEXT("Interface\\PaperDollInfoFrame\\UI-Character-InActiveTab");
        const float ArtY = bSelected ? Y - 5.f : Y; // CharacterFrameTabButtonTemplate
        Place(Canvas, X, ArtY, 20, 32, Img(Art, UV4(0, .15625f, 0, 1)));
        Place(Canvas, X + 20, ArtY, W - 40, 32, Img(Art, UV4(.15625f, .84375f, 0, 1)));
        Place(Canvas, X + W - 20, ArtY, 20, 32, Img(Art, UV4(.84375f, 1, 0, 1)));
        Place(Canvas, X, Y + 6.f, W, 16, Label(Labels[Index], 10,
            bSelected ? HighlightFontColor() : NormalFontColor(), ETextJustify::Center));
        const ETab Target = Index == 0 ? ETab::Inbox : ETab::Send;
        TSharedRef<TWeakPtr<SBorder>> Holder = MakeShared<TWeakPtr<SBorder>>();
        TSharedPtr<SBorder> Hit;
        Place(Canvas, X, Y, W, 32, SAssignNew(Hit, SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(FMargin(10 * Scale, 0))
            .OnMouseButtonDown_Lambda([this, Target](const FGeometry&, const FPointerEvent& Event)
            {
                if (Event.GetEffectingButton() == EKeys::LeftMouseButton) SetTab(Target);
                return FReply::Handled();
            })
            [
                SNew(SWowAddImage).Image_Lambda([this]() { return LoadBrush(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-Tab-Highlight")); })
                .ColorAndOpacity(FLinearColor(1, 1, 1, .6f))
                .Visibility_Lambda([Holder, bSelected]()
                {
                    const TSharedPtr<SBorder> Border = Holder->Pin();
                    return !bSelected && Border.IsValid() && Border->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Hidden;
                })
            ]);
        *Holder = Hit;
        X += W - 8.f;
    }
}

void SWowMailbox::BuildInbox(SCanvas& Canvas)
{
    // InboxTitleText: CENTER 6,232, 224x14.
    Place(Canvas, 86, 17, 224, 14, WowTitleBox(Label(LOCTEXT("InboxTitle", "Inbox"), 12, NormalFontColor(), ETextJustify::Center), Fnt(12)));

    // InboxTooMuchMail (TOP 0,-38): alert icon + "Your inbox is full."
    if (TotalMailCount > static_cast<uint32>(CurrentMail.Num()))
    {
        const FText Full = LOCTEXT("InboxFull", "Your inbox is full.");
        const float W = TextWidth(Full.ToString(), Fnt(12)) / Scale;
        const float TextX = 192.f + 16.f - W * .5f;
        Place(Canvas, TextX - 32, 40, 32, 32, Img(TEXT("Interface\\DialogFrame\\UI-Dialog-Icon-AlertNew")));
        Place(Canvas, TextX, 47, W + 4, 16, Label(Full, 12, NormalFontColor()));
    }

    if (bWaitingForMailList && CurrentMail.IsEmpty())
    {
        Place(Canvas, 28, 180, 305, 16, Label(LOCTEXT("Retrieving", "Retrieving mail..."), 12, GrayFontColor(), ETextJustify::Center));
    }

    // MailItem1..7: TOPLEFT 28,-80, 305x45 each.
    const int32 First = PageNum * InboxItemsToDisplay;
    for (int32 Row = 0; Row < InboxItemsToDisplay; ++Row)
        Place(Canvas, 28, 80 + 45.f * Row, 305, 45, BuildMailItem(First + Row));

    // InboxPrevPageButton CENTER at BOTTOMLEFT 50,104; InboxNextPageButton at 314,104.
    const bool bPrev = PageNum > 0;
    const bool bNext = (PageNum + 1) * InboxItemsToDisplay < CurrentMail.Num();
    auto PageButton = [this](const TCHAR* Name, bool bEnabled, int32 Delta)
    {
        const FString Base = FString::Printf(TEXT("Interface\\Buttons\\UI-SpellbookIcon-%sPage-"), Name);
        const FButtonStyle* Style = TextureButtonStyle(Base + TEXT("Up"), Base + TEXT("Down"), Base + TEXT("Disabled"));
        if (!Style) Style = &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder");
        return SNew(SButton).ButtonStyle(Style).ContentPadding(0).IsEnabled(bEnabled)
            .OnClicked_Lambda([this, Delta]()
            {
                PageNum = FMath::Max(0, PageNum + Delta);
                PlayUiSound(TEXT("igMainMenuOptionCheckBoxOn"));
                Rebuild();
                return FReply::Handled();
            });
    };
    Place(Canvas, 34, 392, 32, 32, PageButton(TEXT("Prev"), bPrev, -1));
    Place(Canvas, 68, 400, 80, 16, Label(LOCTEXT("Prev", "Prev"), 12, bPrev ? NormalFontColor() : GrayFontColor()));
    Place(Canvas, 298, 392, 32, 32, PageButton(TEXT("Next"), bNext, 1));
    Place(Canvas, 214, 400, 82, 16, Label(LOCTEXT("Next", "Next"), 12, bNext ? NormalFontColor() : GrayFontColor(), ETextJustify::Right));
}

TSharedRef<SWidget> SWowMailbox::BuildMailItem(int32 MailIndex)
{
    TSharedRef<SCanvas> Canvas = SNew(SCanvas);
    // MailItemBorder: 42x48 left cap and 263x48 body (TexCoords 0-.164 / .164-1, height .75).
    Place(*Canvas, 0, 0, 42, 48, Img(TEXT("Interface\\MailFrame\\MailItemBorder"), UV4(0, .1640625f, 0, .75f)));
    Place(*Canvas, 42, 0, 263, 48, Img(TEXT("Interface\\MailFrame\\MailItemBorder"), UV4(.1640625f, 1, 0, .75f)));
    if (!CurrentMail.IsValidIndex(MailIndex)) return Canvas;

    const FWowMailMessage& Mail = CurrentMail[MailIndex];
    const bool bRead = (Mail.Checked & WowMailCheckMask::READ) != 0;
    const uint32 MailId = Mail.MessageId;
    UWowConnectionManager* Connection = ConnectionManager.Get();

    // $parentButton (37x37 at 4,-3) over UI-EmptySlot-White (64x64 centred), gold when unread.
    Place(*Canvas, 4 + 18.5f - 32, 3 + 18.5f - 32, 64, 64, Img(TEXT("Interface\\Buttons\\UI-EmptySlot-White"), UV4(0, 1, 0, 1),
        bRead ? WowColor::FromWow(.5f, .5f, .5f) : WowColor::FromWow(1.f, .82f, 0.f)));
    const FString Icon = GetPackageIcon(Mail);
    const uint32 FirstCount = Mail.Attachments.IsEmpty() ? 0 : Mail.Attachments[0].Count;
    TSharedPtr<SToolTip> Tip;
    if (!Mail.Attachments.IsEmpty() && Connection)
    {
        if (Mail.Attachments.Num() == 1) Tip = FWowItemPresentation::Tooltip(Connection, Mail.Attachments[0].ItemEntry, false);
        else { TSharedRef<SToolTip> Multi = SNew(SToolTip).Text(LOCTEXT("MultipleItems", "Multiple items")); Tip = Multi; }
    }
    Place(*Canvas, 4, 3, 37, 37, ItemButton(TAttribute<const FSlateBrush*>::CreateLambda([this, Icon]() { return LoadBrush(Icon); }),
        FirstCount, Tip, FOnClicked::CreateLambda([this, MailId]() { OpenMail(MailId); return FReply::Handled(); }),
        bRead ? WowColor::FromWow(.55f, .55f, .55f) : FLinearColor::White));
    if (OpenMailId == MailId)
        Place(*Canvas, 4, 3, 37, 37, SNew(SWowAddImage).Visibility(EVisibility::HitTestInvisible)
            .Image_Lambda([this]() { return LoadBrush(TEXT("Interface\\Buttons\\CheckButtonHilight")); }).ColorAndOpacity(FLinearColor(1, 1, 1, .9f)));
    if (Mail.COD > 0)
    {
        // $parentCODBackground (34x12 at 2,-2) + "C.O.D." GameFontHighlightSmall.
        Place(*Canvas, 6, 5, 34, 12, SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush")).ColorAndOpacity(FLinearColor(0, 0, 0, .7f)));
        Place(*Canvas, 4, 4, 37, 12, Label(LOCTEXT("CODShort", "C.O.D."), 10, HighlightFontColor(), ETextJustify::Center));
    }

    // Sender (GameFontNormal at 47,-4) and subject (GameFontHighlightSmall below).
    Place(*Canvas, 47, 4, 200, 16, Label(FText::FromString(GetSenderName(Mail)), 12, bRead ? ReadFontColor() : NormalFontColor()));
    Place(*Canvas, 47, 20, 248, 18, Label(FText::FromString(Mail.Subject), 10, bRead ? ReadFontColor() : HighlightFontColor()));

    // $parentExpireTime (TOPRIGHT -4,-4): days in green, hours in red; tooltip says deleted or returned.
    FLinearColor ExpireColor;
    const FString Expire = FormatExpire(Mail.DaysLeft, ExpireColor);
    const FText ExpireTip = CanDelete(Mail) ? LOCTEXT("TimeUntilDeleted", "Time until message is deleted")
                                            : LOCTEXT("TimeUntilReturned", "Time until message is returned");
    Place(*Canvas, 201, 4, 100, 16, SNew(SBox).ToolTip(SNew(SToolTip).Text(ExpireTip))
        [ SNew(STextBlock).Text(FText::FromString(Expire)).Font(Fnt(10)).ColorAndOpacity(ExpireColor)
          .Justification(ETextJustify::Right).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]);

    // Clicking the row (not only the icon) opens the letter.
    return SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
            .OnMouseButtonDown_Lambda([this, MailId](const FGeometry&, const FPointerEvent& Event)
            {
                if (Event.GetEffectingButton() == EKeys::LeftMouseButton) OpenMail(MailId);
                return FReply::Handled();
            })
        ]
        + SOverlay::Slot()[ Canvas ];
}

void SWowMailbox::BuildSend(SCanvas& Canvas)
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    // SendMailTitleText: CENTER 6,230.
    Place(Canvas, 86, 19, 224, 14, WowTitleBox(Label(LOCTEXT("SendMailTitle", "Send Mail"), 12, NormalFontColor(), ETextJustify::Center), Fnt(12)));

    // SendMailNameEditBox (105,-46, 109x20) with "To:" and SendMailCostMoneyFrame (TOPRIGHT -36,-48).
    Place(Canvas, 20, 48, 73, 16, Label(LOCTEXT("MailTo", "To:"), 12, NormalFontColor(), ETextJustify::Right));
    Place(Canvas, 97, 46, 117, 20, InputBorder());
    Place(Canvas, 103, 49, 107, 16, TextField(SendTo, 12, 12));
    {
        const uint32 Postage = GetPostage();
        const FLinearColor Color = Postage > GetPlayerMoney() ? RedFontColor() : HighlightFontColor();
        Place(Canvas, 220, 48, 128, 16, SNew(SHorizontalBox).Visibility(EVisibility::HitTestInvisible)
            + SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(LOCTEXT("Postage", "Postage:")).Font(Fnt(12)).ColorAndOpacity(NormalFontColor())
              .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MoneyText(Postage, 10, Color) ]);
    }

    // SendMailSubjectEditBox (below To, 220x20) with "Subject:".
    Place(Canvas, 20, 71, 73, 16, Label(LOCTEXT("MailSubject", "Subject:"), 12, NormalFontColor(), ETextJustify::Right));
    Place(Canvas, 97, 69, 237, 20, InputBorder());
    Place(Canvas, 103, 72, 225, 16, TextField(SendSubject, 64, 12));

    // Attachment rows decide the letter height (SendMailFrame_Update).
    const int32 Attached = CountSendAttachments();
    int32 LastUsed = 0;
    for (int32 Index = 0; Index < SendAttachments.Num(); ++Index) if (SendAttachments[Index]) LastUsed = Index + 1;
    const int32 Rows = LastUsed >= AttachmentsPerRow ? 2 : 1;
    const float AreaY = 12.f + 5.f * (Rows - 1) + 39.f * Rows;
    const float ScrollHeight = 249.f - AreaY;

    // SendMailScrollFrame (21,-97, 296 wide) with the stationery and the body edit box.
    const float VClip = FMath::Min(ScrollHeight, 256.f) / 256.f;
    Place(Canvas, 21, 97, 252, ScrollHeight, Img(TEXT("Interface\\Stationery\\StationeryTest1"), UV4(0, 1, 0, VClip)));
    Place(Canvas, 273, 97, 64, ScrollHeight, Img(TEXT("Interface\\Stationery\\StationeryTest2"), UV4(0, 1, 0, VClip)));
    {
        MailBodyStyle = FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
            .SetFont(Fnt(15)).SetColorAndOpacity(MailTextColor());
        FString* Body = &SendBody;
        PlaceLetterScroll(Canvas, ScrollHeight, 20.f, 270.f,
            SNew(SMultiLineEditableText)
            .Text_Lambda([Body]() { return FText::FromString(*Body); })
            .TextStyle(&MailBodyStyle)
            .AutoWrapText(true)
            .OnTextChanged_Lambda([Body](const FText& NewText) { *Body = NewText.ToString().Left(500); }));
    }

    // Horizontal bars: SendMailHorizontalBarLeft2 at BOTTOMLEFT 15,170+areaY; SendMailHorizontalBarLeft at 15,-350.
    auto Bar = [this, &Canvas](float Y)
    {
        Place(Canvas, 15, Y, 256, 16, Img(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-HorizontalBar"), UV4(0, 1, 0, .25f)));
        Place(Canvas, 271, Y, 75, 16, Img(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-HorizontalBar"), UV4(0, .29296875f, .25f, .5f)));
    };
    Bar(FrameH - 170.f - AreaY);
    Bar(350.f);

    // SendMailAttachment1..12: 37x37, 7 per row, margins 31/46 (tab 44 x 44).
    const float IndentX = 33.f;
    for (int32 Slot = 0; Slot < FMath::Min(AttachmentsMaxSend, Rows * AttachmentsPerRow); ++Slot)
    {
        const int32 Row = Slot / AttachmentsPerRow;
        const float X = IndentX + 44.f * (Slot % AttachmentsPerRow);
        const float Y = FrameH - (201.f + 44.f * (Rows - 1 - Row));
        Place(Canvas, X - 2, Y - 2, 39, 39, Img(TEXT("Interface\\Buttons\\UI-Slot-Background"), UV4(0, .640625f, 0, .640625f)));
        const uint64 Guid = SendAttachments[Slot];
        const FWowItemEntity* Item = Guid && Connection ? Connection->PacketHandler.EntityManager.FindItem(Guid) : nullptr;
        const uint32 Entry = Item ? Item->Entry : 0;
        const FString IconPath = Entry ? FWowItemPresentation::IconPath(Entry) : FString();
        TSharedPtr<SToolTip> Tip;
        if (Entry && Connection) Tip = FWowItemPresentation::Tooltip(Connection, Entry, false);
        else { TSharedRef<SToolTip> Hint = SNew(SToolTip).Text(LOCTEXT("AttachmentHint", "Drag an item here to include it with your mail")); Tip = Hint; }
        TSharedRef<SWidget> Button = ItemButton(TAttribute<const FSlateBrush*>::CreateLambda([this, IconPath]() { return LoadBrush(IconPath); }),
            Item ? static_cast<uint32>(Item->GetStackCount()) : 0, Tip,
            FOnClicked::CreateLambda([this, Slot]() { ClickSendAttachment(Slot); return FReply::Handled(); }));
        // Releasing a dragged bag item over the slot drops it here (OnReceiveDrag).
        Place(Canvas, X, Y, 37, 37, SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
            .OnMouseButtonUp_Lambda([this, Slot](const FGeometry&, const FPointerEvent& Event)
            {
                UWowConnectionManager* C = ConnectionManager.Get();
                if (C && Event.GetEffectingButton() == EKeys::LeftMouseButton && C->GetCursorItemGuid())
                { ClickSendAttachment(Slot); return FReply::Handled(); }
                return FReply::Unhandled();
            })
            [ Button ]);
    }

    // SendMailMoneyButton area (BOTTOMLEFT 30,110): "Send Money" / "C.O.D." label and the
    // MoneyInputFrame (gold / silver / copper boxes) with the Send Money / C.O.D. radio buttons.
    const bool bHasItems = Attached > 0;
    if (!bHasItems) bSendCod = false;
    Place(Canvas, 30, 370, 180, 12, Label(bSendCod ? LOCTEXT("CODAmount", "C.O.D.") : LOCTEXT("SendMoney", "Send Money"), 10, NormalFontColor()));
    auto CoinBox = [this, &Canvas](float X, float W, FString& Value, int32 Letters, const TCHAR* Icon)
    {
        Place(Canvas, X - 5, 385, W + 10, 20, InputBorder());
        Place(Canvas, X, 388, W - 12, 16, TextField(Value, Letters, 12, true));
        Place(Canvas, X + W - 12, 389, 13, 13, Img(Icon));
    };
    CoinBox(35, 58, MoneyGold, 6, TEXT("Interface\\MoneyFrame\\UI-GoldIcon"));
    CoinBox(106, 34, MoneySilver, 2, TEXT("Interface\\MoneyFrame\\UI-SilverIcon"));
    CoinBox(153, 34, MoneyCopper, 2, TEXT("Interface\\MoneyFrame\\UI-CopperIcon"));
    auto Radio = [this, &Canvas](float Y, const FText& Text, bool bChecked, bool bEnabled, bool bCod)
    {
        // UIRadioButtonTemplate: 16x16 from UI-RadioButton (unchecked .0-.25, checked .25-.5).
        const FBox2f UV = bChecked ? UV4(.25f, .5f, 0, 1) : UV4(0, .25f, 0, 1);
        Place(Canvas, 207, Y, 16, 16, SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(0).IsEnabled(bEnabled)
            .OnClicked_Lambda([this, bCod]() { bSendCod = bCod; Rebuild(); return FReply::Handled(); })
            [ Img(TEXT("Interface\\Buttons\\UI-RadioButton"), UV, bEnabled ? FLinearColor::White : WowColor::FromWow(.5f, .5f, .5f)) ]);
        Place(Canvas, 225, Y + 1, 110, 14, Label(Text, 10, bEnabled ? NormalFontColor() : GrayFontColor()));
    };
    Radio(373, LOCTEXT("SendMoneyRadio", "Send Money"), !bSendCod, true, false);
    Radio(390, LOCTEXT("CODRadio", "C.O.D."), bSendCod, bHasItems, true);

    // SendMailMoneyFrame: the player's money, BOTTOMRIGHT at BOTTOMLEFT 183,84.
    Place(Canvas, 20, 414, 163, 14, SNew(SBox).HAlign(HAlign_Right)[ MoneyText(GetPlayerMoney(), 10) ]);

    // SendMailCancelButton (BOTTOMRIGHT -39,80) and SendMailMailButton to its left.
    const bool bCanSend = !bSending && !SendTo.TrimStartAndEnd().IsEmpty() && (!SendSubject.IsEmpty() || bHasItems);
    Place(Canvas, 185, 410, 80, 22, PanelButton(LOCTEXT("Send", "Send"), 80, 22,
        FOnClicked::CreateLambda([this]() { SendMail(false); return FReply::Handled(); }),
        TAttribute<bool>::CreateLambda([this, bHasItems]() { return !bSending && !SendTo.TrimStartAndEnd().IsEmpty() && (!SendSubject.IsEmpty() || bHasItems); })));
    (void)bCanSend;
    Place(Canvas, 265, 410, 80, 22, PanelButton(LOCTEXT("Cancel", "Cancel"), 80, 22,
        FOnClicked::CreateLambda([this]() { Close(); return FReply::Handled(); })));

    // SEND_MONEY confirmation ("Really send %s the following amount?").
    if (bPendingSendConfirm)
    {
        Place(Canvas, 32, 180, 300, 110, SNew(SWowWindowFrame).Padding(FMargin(14 * Scale, 12 * Scale))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Really send %s the following amount?"), *SendTo.TrimStartAndEnd())))
                  .Font(Fnt(12)).ColorAndOpacity(HighlightFontColor()).AutoWrapText(true).Justification(ETextJustify::Center) ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6 * Scale)[ MoneyText(GetSendMoneyCopper(), 12) ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(4 * Scale, 0)[ PanelButton(LOCTEXT("Accept", "Accept"), 110, 22,
                        FOnClicked::CreateLambda([this]() { SendMail(true); return FReply::Handled(); })) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(4 * Scale, 0)[ PanelButton(LOCTEXT("CancelSend", "Cancel"), 110, 22,
                        FOnClicked::CreateLambda([this]() { bPendingSendConfirm = false; Rebuild(); return FReply::Handled(); })) ]
                ]
            ]);
    }
}

void SWowMailbox::RebuildOpenMail()
{
    if (!OpenMailRoot.IsValid()) return;
    const FWowMailMessage* Mail = bVisible ? FindMail(OpenMailId) : nullptr;
    OpenMailRoot->SetVisibility(Mail ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
    if (!Mail) { OpenMailRoot->SetContent(SNullWidget::NullWidget); return; }

    UWowConnectionManager* Connection = ConnectionManager.Get();
    const uint32 MailId = Mail->MessageId;
    TSharedRef<SCanvas> Canvas = SNew(SCanvas);
    BuildFrameArt(*Canvas, true);

    // OpenMailTitleText (CENTER 6,232), From / Subject labels at x 114.
    // Same title box as the inbox, so both titles sit on the bar (it was drawn 5 units high).
    Place(*Canvas, 86, 17, 224, 14, WowTitleBox(Label(LOCTEXT("OpenMailTitle", "Open Mail"), 12, NormalFontColor(), ETextJustify::Center), Fnt(12)));
    Place(*Canvas, 14, 45, 100, 16, Label(LOCTEXT("From", "From:"), 12, HighlightFontColor(), ETextJustify::Right));
    Place(*Canvas, 119, 45, 225, 16, Label(FText::FromString(GetSenderName(*Mail)), 12, NormalFontColor()));
    Place(*Canvas, 14, 65, 100, 16, Label(LOCTEXT("Subject", "Subject:"), 12, HighlightFontColor(), ETextJustify::Right));
    Place(*Canvas, 119, 69, 225, 16, Label(FText::FromString(Mail->Subject), 10, NormalFontColor()));

    // Attachment buttons: money first, then items, 7 per row, last row centred (OpenMail_Update).
    struct FButtonDef { int32 Attachment; };
    TArray<FButtonDef> Buttons;
    if (Mail->Money > 0) Buttons.Add({ INDEX_NONE });
    for (int32 Index = 0; Index < Mail->Attachments.Num(); ++Index) Buttons.Add({ Index });
    const int32 Rows = Buttons.IsEmpty() ? 0 : (Buttons.Num() + AttachmentsPerRow - 1) / AttachmentsPerRow;
    const float TextH = 10.f;
    float AreaY = 3.f + TextH + 3.f + 39.f * Rows + 3.f * FMath::Max(0, Rows - 1) + 3.f;
    float ScrollHeight = 305.f - AreaY;
    if (ScrollHeight > 256.f) { ScrollHeight = 256.f; AreaY = 305.f - ScrollHeight; }

    // OpenMailScrollFrame (21,-97, 296 wide) with the letter's stationery and MailTextFontNormal body.
    const FString Stationery = GetStationeryTexture(*Mail);
    const float VClip = FMath::Min(ScrollHeight, 256.f) / 256.f;
    auto Paper = [this, Stationery, VClip](const TCHAR* Suffix)
    {
        const FString Wanted = TEXT("Interface\\Stationery\\") + Stationery + Suffix;
        const FString Fallback = FString(TEXT("Interface\\Stationery\\StationeryTest")) + Suffix;
        const FBox2f UV = UV4(0, 1, 0, VClip);
        return SNew(SImage).Visibility(EVisibility::HitTestInvisible)
            .Image_Lambda([this, Wanted, Fallback, UV]() { const FSlateBrush* B = LoadBrush(Wanted, UV); return B ? B : LoadBrush(Fallback, UV); });
    };
    Place(*Canvas, 21, 97, 252, ScrollHeight, Paper(TEXT("1")));
    Place(*Canvas, 273, 97, 64, ScrollHeight, Paper(TEXT("2")));
    PlaceLetterScroll(*Canvas, ScrollHeight, 10.f, 276.f,
        SNew(STextBlock).Text(FText::FromString(Mail->Body)).Font(Fnt(15)).ColorAndOpacity(MailTextColor()).AutoWrapText(true));

    // OpenMailHorizontalBarLeft at BOTTOMLEFT 15, 114+areaY.
    const float BarY = FrameH - 114.f - AreaY;
    Place(*Canvas, 15, BarY, 256, 16, Img(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-HorizontalBar"), UV4(0, 1, 0, .25f)));
    Place(*Canvas, 271, BarY, 75, 16, Img(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-HorizontalBar"), UV4(0, .29296875f, .25f, .5f)));

    // Margins 27/47, icons 39 apart + 6 gap; buttons rise from BOTTOMLEFT 106+39.
    const float IndentX = 27.f;
    const float TabX = 45.f, TabY = 42.f;
    if (Buttons.IsEmpty())
    {
        Place(*Canvas, 27, FrameH - 106.f - AreaY * .5f - TextH, 310, 14, Label(LOCTEXT("NoAttachments", "No Attachments"), 10, GrayFontColor(), ETextJustify::Center));
    }
    else
    {
        Place(*Canvas, IndentX, FrameH - (106.f + 39.f * Rows + 3.f * (Rows - 1) + 3.f + TextH), 300, 14,
            Label(LOCTEXT("TakeAttachments", "Take Attachments:"), 10, HighlightFontColor()));
        for (int32 Index = 0; Index < Buttons.Num(); ++Index)
        {
            const int32 Row = Index / AttachmentsPerRow;
            const int32 InRow = FMath::Min(AttachmentsPerRow, Buttons.Num() - Row * AttachmentsPerRow);
            const float XStart = Row == Rows - 1 ? (AttachmentsPerRow - InRow) * .5f : 0.f;
            const float X = IndentX + TabX * (XStart + Index % AttachmentsPerRow);
            const float Y = FrameH - (106.f + 39.f + TabY * (Rows - 1 - Row));
            if (Buttons[Index].Attachment == INDEX_NONE)
            {
                const FString Coin = CoinIcon(Mail->Money);
                TSharedRef<SToolTip> Tip = SNew(SToolTip)[ SNew(SWowTooltipFrame).Padding(8)[ MoneyText(Mail->Money, 12) ] ];
                Place(*Canvas, X, Y, 37, 37, ItemButton(TAttribute<const FSlateBrush*>::CreateLambda([this, Coin]() { return LoadBrush(Coin); }),
                    0, Tip, FOnClicked::CreateLambda([this]() { TakeMoney(); return FReply::Handled(); })));
            }
            else
            {
                const FWowMailAttachment& Attachment = Mail->Attachments[Buttons[Index].Attachment];
                const int32 AttachmentIndex = Buttons[Index].Attachment;
                const uint32 Entry = Attachment.ItemEntry;
                TSharedPtr<SToolTip> Tip;
                if (Connection) Tip = FWowItemPresentation::Tooltip(Connection, Entry, false);
                Place(*Canvas, X, Y, 37, 37, ItemButton(TAttribute<const FSlateBrush*>::CreateLambda([this, Entry]()
                    { return LoadBrush(FWowItemPresentation::IconPath(Entry)); }),
                    Attachment.Count, Tip, FOnClicked::CreateLambda([this, AttachmentIndex]() { TakeAttachment(AttachmentIndex); return FReply::Handled(); })));
            }
        }
    }

    // OpenMailCancelButton (BOTTOMRIGHT -39,80), Delete/Return and Reply to its left.
    const bool bReply = Mail->MessageType == WowMailMessageType::NORMAL && Mail->SenderGuid != 0;
    Place(*Canvas, 101, 410, 82, 22, PanelButton(LOCTEXT("Reply", "Reply"), 82, 22,
        FOnClicked::CreateLambda([this]() { ReplyToOpenMail(); return FReply::Handled(); }), bReply));
    Place(*Canvas, 183, 410, 82, 22, PanelButton(CanDelete(*Mail) ? LOCTEXT("Delete", "Delete") : LOCTEXT("Return", "Return"), 82, 22,
        FOnClicked::CreateLambda([this]() { DeleteOrReturnOpenMail(false); return FReply::Handled(); })));
    Place(*Canvas, 265, 410, 80, 22, PanelButton(LOCTEXT("Close", "Close"), 80, 22,
        FOnClicked::CreateLambda([this]() { CloseOpenMail(); return FReply::Handled(); })));
    // OpenMailCloseButton: CENTER at TOPRIGHT -47,-25.
    Place(*Canvas, 321, 9, 32, 32, CloseButton(FOnClicked::CreateLambda([this]() { CloseOpenMail(); return FReply::Handled(); })));

    // COD_CONFIRMATION / DELETE_MAIL / DELETE_MONEY in place of the StaticPopups.
    if (PendingCodAttachment != INDEX_NONE || bPendingDeleteConfirm)
    {
        FText Prompt;
        uint32 Amount = 0;
        if (PendingCodAttachment != INDEX_NONE) { Prompt = LOCTEXT("CODConfirm", "Accepting this item will cost:"); Amount = Mail->COD; }
        else if (!Mail->Attachments.IsEmpty() && Connection)
            Prompt = FText::FromString(FString::Printf(TEXT("Deleting this mail will also destroy %s"),
                *FWowItemPresentation::Name(Connection, Mail->Attachments[0].ItemEntry)));
        else { Prompt = LOCTEXT("DeleteMoney", "Deleting this mail will also destroy:"); Amount = Mail->Money; }
        const bool bCod = PendingCodAttachment != INDEX_NONE;
        const int32 CodIndex = PendingCodAttachment;
        Place(*Canvas, 32, 170, 300, 110, SNew(SWowWindowFrame).Padding(FMargin(14 * Scale, 12 * Scale))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [ SNew(STextBlock).Text(Prompt).Font(Fnt(12)).ColorAndOpacity(HighlightFontColor()).AutoWrapText(true).Justification(ETextJustify::Center) ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6 * Scale)
                [ Amount ? MoneyText(Amount, 12) : SNullWidget::NullWidget ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(4 * Scale, 0)[ PanelButton(bCod ? LOCTEXT("AcceptCOD", "Accept") : LOCTEXT("Okay", "Okay"), 110, 22,
                        FOnClicked::CreateLambda([this, bCod, CodIndex]()
                        {
                            if (bCod) TakeAttachment(CodIndex); else DeleteOrReturnOpenMail(true);
                            return FReply::Handled();
                        })) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(4 * Scale, 0)[ PanelButton(LOCTEXT("CancelConfirm", "Cancel"), 110, 22,
                        FOnClicked::CreateLambda([this]()
                        {
                            PendingCodAttachment = INDEX_NONE; bPendingDeleteConfirm = false; RebuildOpenMail();
                            return FReply::Handled();
                        })) ]
                ]
            ]);
    }
    (void)MailId;

    OpenMailRoot->SetContent(
        SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SBox).Padding(FMargin(0, 0, 35 * Scale, 75 * Scale))
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
                .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
            ]
        ]
        + SOverlay::Slot()[ Canvas ]);
}

#undef LOCTEXT_NAMESPACE
