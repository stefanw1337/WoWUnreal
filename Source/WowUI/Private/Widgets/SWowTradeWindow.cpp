#include "Widgets/SWowTradeWindow.h"
#include "WowColor.h"
#include "WowWindowStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowTradeWindow, Log, All);

void SWowTradeWindow::Construct(const FArguments& InArgs)
{
    OnTradeAccept = InArgs._OnTradeAccept;
    OnTradeCancel = InArgs._OnTradeCancel;

    ChildSlot
    [
        SNew(SWowWindowFrame)
        .Padding(8.0f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Trade")))
                    .Font(FWowWindowStyle::TitleFont())
                    .ColorAndOpacity(FLinearColor::Yellow)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center)
                    .Text(FText::FromString(TEXT("Cancel")))
                    .OnClicked(this, &SWowTradeWindow::OnCancelClicked)
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 6.0f)
            [
                SAssignNew(StatusText, STextBlock)
                .Text(this, &SWowTradeWindow::GetStatusText)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                .ColorAndOpacity(FLinearColor::White)
            ]

            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(0.0f, 8.0f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(SBorder)
                    .BorderBackgroundColor(WowColor::FromWow(0.15f, 0.12f, 0.10f, 0.9f))
                    .Padding(6.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Your Offer")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                            .ColorAndOpacity(FLinearColor::Yellow)
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 4.0f)
                        [
                            SAssignNew(PlayerMoneyText, STextBlock)
                            .Text(FText::FromString(TEXT("0g 0s 0c")))
                            .ColorAndOpacity(FLinearColor::Yellow)
                        ]
                        + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                        [
                            SAssignNew(PlayerItemsBox, SVerticalBox)
                        ]
                    ]
                ]

                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SBorder)
                    .BorderBackgroundColor(WowColor::FromWow(0.15f, 0.12f, 0.10f, 0.9f))
                    .Padding(6.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Their Offer")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                            .ColorAndOpacity(FLinearColor::Yellow)
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 4.0f)
                        [
                            SAssignNew(TargetMoneyText, STextBlock)
                            .Text(FText::FromString(TEXT("0g 0s 0c")))
                            .ColorAndOpacity(FLinearColor::Yellow)
                        ]
                        + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                        [
                            SAssignNew(TargetItemsBox, SVerticalBox)
                        ]
                    ]
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 8.0f, 0.0f, 0.0f)
            .HAlign(HAlign_Right)
            [
                SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center)
                .Text(this, &SWowTradeWindow::GetAcceptButtonText)
                .OnClicked(this, &SWowTradeWindow::OnAcceptClicked)
            ]
        ]
    ];

    SetVisibility(EVisibility::Collapsed);
    RefreshTradeLists();
}

void SWowTradeWindow::UpdateTrade(const FWowTradeState& TradeState)
{
    CurrentTrade = TradeState;
    RefreshTradeLists();

    const bool bShowWindow = CurrentTrade.bTradeOpen
        || CurrentTrade.Status == WowTradeStatus::BEGIN_TRADE
        || CurrentTrade.Status == WowTradeStatus::BACK_TO_TRADE
        || CurrentTrade.Status == WowTradeStatus::TRADE_ACCEPT;

    SetVisibility(bShowWindow ? EVisibility::Visible : EVisibility::Collapsed);
    UE_LOG(LogWowTradeWindow, Log, TEXT("Trade window updated: status=%u open=%d"), CurrentTrade.Status, CurrentTrade.bTradeOpen ? 1 : 0);
}

void SWowTradeWindow::CloseTrade()
{
    CurrentTrade = FWowTradeState{};
    RefreshTradeLists();
    SetVisibility(EVisibility::Collapsed);
}

FReply SWowTradeWindow::OnAcceptClicked()
{
    const bool bBeginTrade = (CurrentTrade.Status == WowTradeStatus::BEGIN_TRADE && !CurrentTrade.bTradeOpen);
    if (!bBeginTrade && CurrentTrade.bLocalAccepted)
    {
        return FReply::Handled();
    }

    if (!bBeginTrade)
    {
        CurrentTrade.bLocalAccepted = true;
    }

    if (OnTradeAccept.IsBound())
    {
        OnTradeAccept.Execute(bBeginTrade);
    }
    return FReply::Handled();
}

FReply SWowTradeWindow::OnCancelClicked()
{
    if (OnTradeCancel.IsBound())
    {
        OnTradeCancel.Execute();
    }

    CloseTrade();
    return FReply::Handled();
}

void SWowTradeWindow::RefreshTradeLists()
{
    if (!PlayerItemsBox.IsValid() || !TargetItemsBox.IsValid())
    {
        return;
    }

    PlayerItemsBox->ClearChildren();
    TargetItemsBox->ClearChildren();

    const int32 SlotCount = FMath::Max(7, FMath::Max(CurrentTrade.PlayerItems.Num(), CurrentTrade.TargetItems.Num()));
    for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
    {
        const FWowTradeItem* PlayerItem = CurrentTrade.PlayerItems.IsValidIndex(SlotIndex) ? &CurrentTrade.PlayerItems[SlotIndex] : nullptr;
        const FWowTradeItem* TargetItem = CurrentTrade.TargetItems.IsValidIndex(SlotIndex) ? &CurrentTrade.TargetItems[SlotIndex] : nullptr;

        const FString PlayerLabel = (PlayerItem && PlayerItem->HasItem())
            ? FString::Printf(TEXT("Slot %d: Item %u x%u"), SlotIndex + 1, PlayerItem->ItemId, PlayerItem->Count)
            : FString::Printf(TEXT("Slot %d: (empty)"), SlotIndex + 1);
        const FString TargetLabel = (TargetItem && TargetItem->HasItem())
            ? FString::Printf(TEXT("Slot %d: Item %u x%u"), SlotIndex + 1, TargetItem->ItemId, TargetItem->Count)
            : FString::Printf(TEXT("Slot %d: (empty)"), SlotIndex + 1);

        PlayerItemsBox->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 2.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(PlayerLabel))
            .ColorAndOpacity((PlayerItem && PlayerItem->HasItem()) ? FLinearColor::White : WowColor::FromWow(0.6f, 0.6f, 0.6f))
        ];

        TargetItemsBox->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 2.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TargetLabel))
            .ColorAndOpacity((TargetItem && TargetItem->HasItem()) ? FLinearColor::White : WowColor::FromWow(0.6f, 0.6f, 0.6f))
        ];
    }

    if (PlayerMoneyText.IsValid())
    {
        PlayerMoneyText->SetText(GetMoneyText(CurrentTrade.PlayerMoney));
    }
    if (TargetMoneyText.IsValid())
    {
        TargetMoneyText->SetText(GetMoneyText(CurrentTrade.TargetMoney));
    }
}

FText SWowTradeWindow::GetStatusText() const
{
    if (CurrentTrade.bTradeOpen)
    {
        if (CurrentTrade.bLocalAccepted && CurrentTrade.bTargetAccepted)
        {
            return FText::FromString(TEXT("Both players accepted the current trade."));
        }

        if (CurrentTrade.bLocalAccepted)
        {
            return FText::FromString(TEXT("You accepted the current trade."));
        }

        if (CurrentTrade.bTargetAccepted)
        {
            return FText::FromString(TEXT("The other player accepted the current trade."));
        }
    }

    switch (CurrentTrade.Status)
    {
    case WowTradeStatus::BEGIN_TRADE:
        return FText::FromString(FString::Printf(TEXT("%s wants to trade."), *GetTraderLabel()));
    case WowTradeStatus::OPEN_WINDOW:
        return FText::FromString(TEXT("Trade window open."));
    case WowTradeStatus::TRADE_ACCEPT:
        return FText::FromString(TEXT("The other player accepted the current trade."));
    case WowTradeStatus::BACK_TO_TRADE:
        return FText::FromString(TEXT("Trade changed. Acceptance reset."));
    case WowTradeStatus::TRADE_COMPLETE:
        return FText::FromString(TEXT("Trade complete."));
    case WowTradeStatus::TRADE_CANCELED:
        return FText::FromString(TEXT("Trade canceled."));
    case WowTradeStatus::CLOSE_WINDOW:
        return FText::FromString(TEXT("Trade closed."));
    case WowTradeStatus::BUSY:
    case WowTradeStatus::BUSY_2:
        return FText::FromString(TEXT("Target is busy."));
    case WowTradeStatus::TARGET_TO_FAR:
        return FText::FromString(TEXT("Target is too far away."));
    case WowTradeStatus::WRONG_FACTION:
        return FText::FromString(TEXT("You cannot trade across factions."));
    case WowTradeStatus::NO_TARGET:
        return FText::FromString(TEXT("No trade target."));
    default:
        return FText::FromString(TEXT("Trade"));
    }
}

FText SWowTradeWindow::GetAcceptButtonText() const
{
    if (CurrentTrade.Status == WowTradeStatus::BEGIN_TRADE && !CurrentTrade.bTradeOpen)
    {
        return FText::FromString(TEXT("Accept Request"));
    }

    if (CurrentTrade.bLocalAccepted)
    {
        return FText::FromString(TEXT("Accepted"));
    }

    return FText::FromString(TEXT("Accept"));
}

FText SWowTradeWindow::GetMoneyText(uint32 Copper) const
{
    const uint32 Gold = Copper / 10000;
    const uint32 Silver = (Copper / 100) % 100;
    const uint32 Bronze = Copper % 100;
    return FText::FromString(FString::Printf(TEXT("%ug %us %uc"), Gold, Silver, Bronze));
}

FString SWowTradeWindow::GetTraderLabel() const
{
    return CurrentTrade.TraderGuid != 0
        ? FString::Printf(TEXT("Player %llu"), CurrentTrade.TraderGuid)
        : FString(TEXT("The other player"));
}
