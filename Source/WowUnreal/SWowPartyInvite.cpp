#include "SWowPartyInvite.h"
#include "WowWindowStyle.h"
#include "WowConnectionManager.h"
#include "WowOpcodes.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowPartyInvite, Log, All);

void SWowPartyInvite::Construct(const FArguments& InArgs)
{
    ConnectionManager = InArgs._ConnectionManager;
    InviterName = InArgs._InviterName;

    ChildSlot
    [
        SNew(SWowWindowFrame)
        .Padding(16.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 12)
            [
                SNew(STextBlock)
                .Text(this, &SWowPartyInvite::GetInviteMessageText)
                .ColorAndOpacity(FLinearColor::White)
                .Font(FAppStyle::GetFontStyle("NormalFont"))
                .Justification(ETextJustify::Center)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .Padding(0, 0, 4, 0)
                [
                    SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center)
                    .Text(FText::FromString(TEXT("Accept")))
                    // Fire on press: a world click handler could otherwise take the mouse
                    // capture between press and release, and the click never arrived.
                    .ClickMethod(EButtonClickMethod::MouseDown)
                    .OnClicked(this, &SWowPartyInvite::OnAcceptClicked)
                    .ButtonColorAndOpacity(FLinearColor(0.0f, 0.6f, 0.0f))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .Padding(4, 0, 0, 0)
                [
                    SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center)
                    .Text(FText::FromString(TEXT("Decline")))
                    .ClickMethod(EButtonClickMethod::MouseDown)
                    .OnClicked(this, &SWowPartyInvite::OnDeclineClicked)
                    .ButtonColorAndOpacity(FLinearColor(0.6f, 0.0f, 0.0f))
                ]
            ]
        ]
    ];
}

void SWowPartyInvite::CloseDialog()
{
    // Remove this widget from the parent by invalidating itself
    SetVisibility(EVisibility::Collapsed);
}

FReply SWowPartyInvite::OnAcceptClicked()
{
    UE_LOG(LogWowPartyInvite, Log, TEXT("Accepting party invite from %s"), *InviterName);

    if (ConnectionManager.IsValid())
    {
        // CMSG_GROUP_ACCEPT carries a uint32 (0) in 3.3.5; an empty packet is rejected by
        // the server, so accepting silently did nothing.
        TArray<uint8> Data;
        Data.AddZeroed(4);
        ConnectionManager->SendRawPacket(WowOpcode::CMSG_GROUP_ACCEPT, Data);
    }

    CloseDialog();
    return FReply::Handled();
}

FReply SWowPartyInvite::OnDeclineClicked()
{
    UE_LOG(LogWowPartyInvite, Log, TEXT("Declining party invite from %s"), *InviterName);

    if (ConnectionManager.IsValid())
    {
        // Send CMSG_GROUP_DECLINE with no payload
        TArray<uint8> Data;
        ConnectionManager->SendRawPacket(WowOpcode::CMSG_GROUP_DECLINE, Data);
    }

    CloseDialog();
    return FReply::Handled();
}

FText SWowPartyInvite::GetInviteMessageText() const
{
    return FText::FromString(FString::Printf(TEXT("%s has invited you to a group."), *InviterName));
}
