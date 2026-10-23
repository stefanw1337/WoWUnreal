#include "SWowBankWindow.h"
#include "WowWindowStyle.h"

#include "WowInventoryManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FWowItemSlot* ResolveBankItemSlot(TSharedPtr<FWowInventoryManager> InventoryManager, uint8 SlotIndex, bool bIsBagSlot)
{
    if (!InventoryManager.IsValid())
    {
        return nullptr;
    }

    return bIsBagSlot
        ? InventoryManager->GetBankBagSlot(SlotIndex)
        : InventoryManager->GetBankItem(SlotIndex);
}
}

void SWowBankItemSlot::Construct(const FArguments& InArgs, TSharedPtr<FWowInventoryManager> InInventoryManager)
{
    InventoryManager = InInventoryManager;
    SlotIndex = InArgs._SlotIndex;
    bIsBagSlot = InArgs._bIsBagSlot;

    ChildSlot
    [
        SAssignNew(SlotBorder, SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .BorderBackgroundColor(this, &SWowBankItemSlot::GetSlotBorderColor)
        .Padding(2.0f)
        [
            SNew(SBox)
            .WidthOverride(bIsBagSlot ? 34.0f : 40.0f)
            .HeightOverride(bIsBagSlot ? 34.0f : 40.0f)
            [
                SNew(SOverlay)
                + SOverlay::Slot()
                [
                    SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(this, &SWowBankItemSlot::GetItemQualityColor)
                    .Padding(0)
                ]
                + SOverlay::Slot()
                .HAlign(HAlign_Right)
                .VAlign(VAlign_Bottom)
                [
                    SAssignNew(StackCountText, STextBlock)
                    .Text(this, &SWowBankItemSlot::GetStackCountText)
                    .ColorAndOpacity(FLinearColor::White)
                    .Font(FAppStyle::GetFontStyle("SmallFont"))
                ]
            ]
        ]
    ];

    UpdateSlotDisplay();
}

void SWowBankItemSlot::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    bIsHovered = true;

    if (const FWowItemSlot* ItemSlot = GetItemSlot())
    {
        if (!ItemSlot->IsEmpty())
        {
            SetToolTip(CreateItemTooltip(ItemSlot));
        }
    }
}

void SWowBankItemSlot::OnMouseLeave(const FPointerEvent& MouseEvent)
{
    bIsHovered = false;
    SetToolTip(nullptr);
}

void SWowBankItemSlot::UpdateSlotDisplay()
{
    Invalidate(EInvalidateWidget::Layout);
}

const FWowItemSlot* SWowBankItemSlot::GetItemSlot() const
{
    return ResolveBankItemSlot(InventoryManager, SlotIndex, bIsBagSlot);
}

FSlateColor SWowBankItemSlot::GetSlotBorderColor() const
{
    return bIsHovered ? FLinearColor::Yellow : FLinearColor(1.0f, 0.84f, 0.0f);
}

FSlateColor SWowBankItemSlot::GetItemQualityColor() const
{
    const FWowItemSlot* ItemSlot = GetItemSlot();
    if (!ItemSlot || ItemSlot->IsEmpty())
    {
        return bIsBagSlot ? FLinearColor(0.10f, 0.10f, 0.10f) : FLinearColor(0.16f, 0.16f, 0.16f);
    }

    return FWowInventoryManager::GetQualityColor(ItemSlot->Quality);
}

FText SWowBankItemSlot::GetStackCountText() const
{
    const FWowItemSlot* ItemSlot = GetItemSlot();
    if (!ItemSlot || ItemSlot->IsEmpty() || ItemSlot->StackCount <= 1)
    {
        return FText::GetEmpty();
    }

    return FText::AsNumber(ItemSlot->StackCount);
}

TSharedPtr<SToolTip> SWowBankItemSlot::CreateItemTooltip(const FWowItemSlot* ItemSlot) const
{
    if (!ItemSlot || ItemSlot->IsEmpty())
    {
        return nullptr;
    }

    const FString ItemName = FString::Printf(TEXT("Item #%u"), ItemSlot->ItemEntry);
    const FLinearColor QualityColor = FWowInventoryManager::GetQualityColor(ItemSlot->Quality);

    return SNew(SToolTip)
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
        .BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.95f))
        .Padding(8.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(ItemName))
                .ColorAndOpacity(QualityColor)
                .Font(FAppStyle::GetFontStyle("NormalFont"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 2, 0, 0)
            [
                SNew(STextBlock)
                .Text(FText::FromString(FString::Printf(TEXT("Item ID: %u"), ItemSlot->ItemEntry)))
                .ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f, 1.0f))
                .Font(FAppStyle::GetFontStyle("SmallFont"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 2, 0, 0)
            [
                SNew(STextBlock)
                .Text(ItemSlot->StackCount > 1
                    ? FText::FromString(FString::Printf(TEXT("Count: %u"), ItemSlot->StackCount))
                    : FText::GetEmpty())
                .ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f, 1.0f))
                .Font(FAppStyle::GetFontStyle("SmallFont"))
                .Visibility(ItemSlot->StackCount > 1 ? EVisibility::Visible : EVisibility::Collapsed)
            ]
        ]
    ];
}

void SWowBankWindow::Construct(const FArguments& InArgs, TSharedPtr<FWowInventoryManager> InInventoryManager)
{
    InventoryManager = InInventoryManager;

    TSharedPtr<SUniformGridPanel> BankGrid;
    TSharedPtr<SUniformGridPanel> BankBagGrid;

    SAssignNew(BankGrid, SUniformGridPanel).SlotPadding(FMargin(2.0f));
    SAssignNew(BankBagGrid, SUniformGridPanel).SlotPadding(FMargin(2.0f));

    BankSlots.Reserve(28);
    for (int32 SlotIndex = 0; SlotIndex < 28; ++SlotIndex)
    {
        TSharedPtr<SWowBankItemSlot> SlotWidget;
        BankGrid->AddSlot(SlotIndex % 7, SlotIndex / 7)
        [
            SAssignNew(SlotWidget, SWowBankItemSlot, InventoryManager)
            .SlotIndex(static_cast<uint8>(SlotIndex))
            .bIsBagSlot(false)
        ];
        BankSlots.Add(SlotWidget);
    }

    BankBagSlots.Reserve(7);
    for (int32 SlotIndex = 0; SlotIndex < 7; ++SlotIndex)
    {
        TSharedPtr<SWowBankItemSlot> SlotWidget;
        BankBagGrid->AddSlot(SlotIndex, 0)
        [
            SAssignNew(SlotWidget, SWowBankItemSlot, InventoryManager)
            .SlotIndex(static_cast<uint8>(SlotIndex))
            .bIsBagSlot(true)
        ];
        BankBagSlots.Add(SlotWidget);
    }

    ChildSlot
    [
        SNew(SWowWindowFrame)
        .Padding(10.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 8)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WowUI", "BankTitle", "Bank"))
                    .Font(FAppStyle::GetFontStyle("NormalFont"))
                    .ColorAndOpacity(FLinearColor::White)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center)
                    .Text(NSLOCTEXT("WowUI", "CloseBank", "X"))
                    .OnClicked(this, &SWowBankWindow::OnCloseClicked)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 8)
            [
                BankGrid.ToSharedRef()
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 4, 0, 4)
            [
                SNew(STextBlock)
                .Text(this, &SWowBankWindow::GetBankBagStatusText)
                .Font(FAppStyle::GetFontStyle("SmallFont"))
                .ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                BankBagGrid.ToSharedRef()
            ]
        ]
    ];

    if (InventoryManager.IsValid())
    {
        InventoryManager->OnInventoryChanged.AddSP(this, &SWowBankWindow::OnInventoryChanged);
    }

    Hide();
}

void SWowBankWindow::Show()
{
    CurrentVisibility = EVisibility::SelfHitTestInvisible;
    SWidget::SetVisibility(CurrentVisibility);
}

void SWowBankWindow::Hide()
{
    CurrentVisibility = EVisibility::Hidden;
    SWidget::SetVisibility(CurrentVisibility);
}

bool SWowBankWindow::IsBankVisible() const
{
    return CurrentVisibility != EVisibility::Hidden;
}

void SWowBankWindow::OnInventoryChanged()
{
    for (const TSharedPtr<SWowBankItemSlot>& Slot : BankSlots)
    {
        if (Slot.IsValid())
        {
            Slot->UpdateSlotDisplay();
        }
    }

    for (const TSharedPtr<SWowBankItemSlot>& Slot : BankBagSlots)
    {
        if (Slot.IsValid())
        {
            Slot->UpdateSlotDisplay();
        }
    }
}

FReply SWowBankWindow::OnCloseClicked()
{
    Hide();
    return FReply::Handled();
}

FText SWowBankWindow::GetBankBagStatusText() const
{
    const int32 PurchasedSlots = InventoryManager.IsValid() ? InventoryManager->GetPurchasedBankBagSlots() : 0;
    return FText::FromString(FString::Printf(TEXT("Purchased bank bag slots: %d / 7"), PurchasedSlots));
}
