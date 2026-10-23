#pragma once
#include "CoreMinimal.h"
#include "WowNetwork/Public/WowEntity.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateTypes.h"

class UWowConnectionManager;
class UWowUIManager;
class SCanvas;
class SBox;
class SToolTip;
class FWowRetainedSlateBrush;
struct FSlateBrush;
struct FButtonStyle;

/**
 * Native replica of the 3.3.5 MailFrame: Inbox tab (7 mails per page, Prev/Next),
 * Send Mail tab (To/Subject/body, 12 attachment slots, Send Money / C.O.D.) and the
 * separate OpenMailFrame panel (GetOpenMailWidget) that opens beside the inbox.
 * Positions are MailFrame.xml / MailFrame.lua values in WoW units x UI scale.
 */
class WOWUNREAL_API SWowMailbox : public SCompoundWidget
{
public:
    static constexpr float FrameW = 384.f;
    static constexpr float FrameH = 512.f;

    SLATE_BEGIN_ARGS(SWowMailbox) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UWowUIManager>, UIManager)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager);
    virtual ~SWowMailbox();

    /** Open the mailbox at a mailbox gameobject / NPC and request the inbox. */
    void Show(uint64 MailboxGuid);
    /** MAIL_CLOSED: hide both panels (walked away, Escape, close button). */
    void Close();
    void ToggleVisibility();
    bool IsVisible() const;
    bool IsOpenMailVisible() const { return bVisible && OpenMailId != 0; }
    uint64 GetMailboxGuid() const { return CurrentMailboxGuid; }

    /** OpenMailFrame, registered by the controller as its own UI panel. */
    TSharedRef<SWidget> GetOpenMailWidget() const { return OpenMailRoot.ToSharedRef(); }
    FVector2D GetPanelSize() const { return FVector2D(FrameW, FrameH) * Scale; }

    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

private:
    enum class ETab : uint8 { Inbox, Send };

    // Layout
    void Rebuild();
    void RebuildOpenMail();
    void BuildFrameArt(SCanvas& Canvas, bool bOpenMail) const;
    /** Letter scroll frame (21,-97, 296 wide) with the UIPanelScrollFrameTemplate bar and trough art. */
    void PlaceLetterScroll(SCanvas& Canvas, float Height, float TextX, float TextW, TSharedRef<SWidget> Content);
    void BuildInbox(SCanvas& Canvas);
    void BuildSend(SCanvas& Canvas);
    void BuildTabs(SCanvas& Canvas);
    TSharedRef<SWidget> BuildMailItem(int32 MailIndex);
    TSharedRef<SWidget> ItemButton(TAttribute<const FSlateBrush*> Icon, uint32 Count, TSharedPtr<SToolTip> Tip,
        FOnClicked OnClicked, FLinearColor IconTint = FLinearColor::White) const;
    TSharedRef<SWidget> MoneyText(uint32 Copper, float WowPixels, FLinearColor Color = FLinearColor::White) const;
    TSharedRef<SWidget> InputBorder() const;
    TSharedRef<SWidget> PanelButton(const FText& Label, float W, float H, FOnClicked OnClicked, TAttribute<bool> Enabled = true) const;
    TSharedRef<SWidget> CloseButton(FOnClicked OnClicked) const;
    TSharedRef<SWidget> Img(const FString& Path, FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1)), FLinearColor Tint = FLinearColor::White) const;
    TSharedRef<SWidget> Label(const FText& Text, float WowPixels, FLinearColor Color, ETextJustify::Type Justify = ETextJustify::Left) const;
    TSharedRef<SWidget> TextField(FString& Value, int32 MaxLetters, float WowPixels, bool bNumeric = false);
    void Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const;
    FSlateFontInfo Fnt(float WowPixels) const;
    float TextWidth(const FString& Text, const FSlateFontInfo& Font) const;
    const FSlateBrush* LoadBrush(const FString& TexturePath, FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1))) const;
    const FButtonStyle* TextureButtonStyle(const FString& Up, const FString& Down, const FString& Disabled = FString(),
        FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1))) const;

    // Data helpers
    const FWowMailMessage* FindMail(uint32 MailId) const;
    FString GetSenderName(const FWowMailMessage& Mail) const;
    FString GetStationeryTexture(const FWowMailMessage& Mail) const;
    FString GetPackageIcon(const FWowMailMessage& Mail) const;
    bool CanDelete(const FWowMailMessage& Mail) const;
    uint32 GetPlayerMoney() const;
    uint32 GetSendMoneyCopper() const;
    uint32 GetPostage() const;
    int32 CountSendAttachments() const;
    uint32 ComputeStateStamp() const;

    // Network events
    void OnMailListReceived(const TArray<FWowMailMessage>& InMail);
    void OnMailResult(uint32 MailId, uint32 Action, uint32 Error, uint32 EquipError);
    void RequestMailList();

    // Actions
    void OpenMail(uint32 MailId);
    void CloseOpenMail();
    void TakeAttachment(int32 AttachmentIndex);
    void TakeMoney();
    void DeleteOrReturnOpenMail(bool bConfirmed);
    void ReplyToOpenMail();
    bool AttachItem(uint64 ItemGuid, int32 Slot = INDEX_NONE);
    void ClickSendAttachment(int32 Slot);
    void SendMail(bool bConfirmed);
    void ResetSendForm();
    void UpdateAutoSubject();
    void SetTab(ETab NewTab);
    void PlayUiSound(const TCHAR* Sound) const;

    TWeakObjectPtr<UWowConnectionManager> ConnectionManager;
    TWeakObjectPtr<UWowUIManager> UIManager;
    TArray<FWowMailMessage> CurrentMail;
    uint32 TotalMailCount = 0;
    uint64 CurrentMailboxGuid = 0;
    bool bWaitingForMailList = false;
    bool bVisible = false;
    ETab Tab = ETab::Inbox;
    int32 PageNum = 0;
    uint32 OpenMailId = 0;
    /** Confirmations shown in place of the StaticPopups (COD_CONFIRMATION, DELETE_MAIL, SEND_MONEY). */
    int32 PendingCodAttachment = INDEX_NONE;
    bool bPendingDeleteConfirm = false;
    bool bPendingSendConfirm = false;
    bool bSending = false;
    float Scale = 1.f;
    uint32 LastStateStamp = 0;

    // Send Mail form
    FString SendTo, SendSubject, SendBody, AutoSubject;
    FString MoneyGold, MoneySilver, MoneyCopper;
    bool bSendCod = false;
    TArray<uint64> SendAttachments; // ATTACHMENTS_MAX_SEND = 12, 0 = empty slot

    FTextBlockStyle MailBodyStyle;
    TSharedPtr<FScrollBarStyle> LetterScrollStyle;
    TSharedPtr<SBox> Root;
    TSharedPtr<SBox> OpenMailRoot;
    FDelegateHandle MailListHandle, MailResultHandle;
    mutable TMap<FString, TSharedPtr<FWowRetainedSlateBrush>> TextureBrushes;
    mutable TMap<FString, double> FailedTextureTimes;
    mutable TMap<FString, TSharedPtr<FButtonStyle>> ButtonStyles;
};
