#include "SWowDuelInvite.h"
#include "WowWindowStyle.h"

#include "WowConnectionManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowDuelInvite, Log, All);

void SWowDuelInvite::Construct(const FArguments& InArgs)
{
    ConnectionManager = InArgs._ConnectionManager;
    ChallengerName = InArgs._ChallengerName;
    ArbiterGuid = InArgs._ArbiterGuid;
    OnClosed = InArgs._OnClosed;

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
                .Text(this, &SWowDuelInvite::GetInviteMessageText)
                .ColorAndOpacity(FLinearColor::White)
                .Font(FAppStyle::GetFontStyle("NormalFont"))
                .Justification(ETextJustify::Center)
                .WrapTextAt(320.0f)
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
                    .OnClicked(this, &SWowDuelInvite::OnAcceptClicked)
                    .ButtonColorAndOpacity(FLinearColor(0.0f, 0.55f, 0.0f))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .Padding(4, 0, 0, 0)
                [
                    SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center)
                    .Text(FText::FromString(TEXT("Decline")))
                    .OnClicked(this, &SWowDuelInvite::OnDeclineClicked)
                    .ButtonColorAndOpacity(FLinearColor(0.55f, 0.0f, 0.0f))
                ]
            ]
        ]
    ];
}

void SWowDuelInvite::CloseDialog()
{
    if (OnClosed.IsBound())
    {
        OnClosed.Execute();
    }
}

FReply SWowDuelInvite::OnAcceptClicked()
{
    UE_LOG(LogWowDuelInvite, Log, TEXT("Accepting duel invite from %s"), *ChallengerName);

    if (ConnectionManager.IsValid())
    {
        ConnectionManager->SendAcceptDuel(ArbiterGuid);
    }

    CloseDialog();
    return FReply::Handled();
}

FReply SWowDuelInvite::OnDeclineClicked()
{
    UE_LOG(LogWowDuelInvite, Log, TEXT("Declining duel invite from %s"), *ChallengerName);

    if (ConnectionManager.IsValid())
    {
        ConnectionManager->SendCancelDuel(ArbiterGuid);
    }

    CloseDialog();
    return FReply::Handled();
}

FText SWowDuelInvite::GetInviteMessageText() const
{
    return FText::FromString(FString::Printf(TEXT("%s has challenged you to a duel."), *ChallengerName));
}
