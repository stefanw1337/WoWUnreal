#include "SWowBagWindow.h"
#include "WowWindowStyle.h"
#include "WowItemPresentation.h"
#include "WowInventoryManager.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "InputCoreTypes.h"
#include "Formats/Dbc/DbcStore.h"

void SWowItemSlot::Construct(const FArguments& InArgs, TSharedPtr<FWowInventoryManager> InInventoryManager)
{
    InventoryManager = InInventoryManager;
    SlotIndex = InArgs._SlotIndex;

    ChildSlot
    [
        SAssignNew(SlotBorder, SBorder)
        .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
        .BorderBackgroundColor(this, &SWowItemSlot::GetSlotBorderColor)
        .Padding(1.0f)
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.015f, 0.014f, 0.012f, 1.0f))
            .Padding(2.0f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor(this, &SWowItemSlot::GetItemQualityColor)
                .Padding(0.0f)
                [
                    SNew(SBox)
                    .WidthOverride(34.0f)
                    .HeightOverride(34.0f)
                    [
                        SNew(SOverlay)

                        // Empty/item plate. This intentionally stays dark rather than
                        // using a red "unable" overlay, which is reserved for action range.
                        + SOverlay::Slot()
                        [
                            SNew(SBorder)
                            .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                            .BorderBackgroundColor(FLinearColor(0.020f, 0.019f, 0.017f, 0.96f))
                            .Padding(0.0f)
                        ]

                        // Stack count text
                        + SOverlay::Slot()
                        .HAlign(HAlign_Right)
                        .VAlign(VAlign_Bottom)
                        .Padding(0.0f, 0.0f, 2.0f, 0.0f)
                        [
                            SAssignNew(StackCountText, STextBlock)
                            .Text(this, &SWowItemSlot::GetStackCountText)
                            .ColorAndOpacity(FLinearColor(1.0f, 0.93f, 0.68f, 1.0f))
                            .ShadowOffset(FVector2D(1.0f))
                            .ShadowColorAndOpacity(FLinearColor::Black)
                            .Font(FAppStyle::GetFontStyle("SmallFont"))
                        ]
                    ]
                ]
            ]
        ]
    ];

    UpdateSlotDisplay();
}

FReply SWowItemSlot::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
    {
        // Right click to auto-equip
        if (InventoryManager.IsValid())
        {
            const FWowItemSlot* ItemSlot = InventoryManager->GetBackpackItem(SlotIndex);
            if (ItemSlot && !ItemSlot->IsEmpty())
            {
                InventoryManager->AutoEquipItem(ItemSlot->Bag, ItemSlot->Slot);
                return FReply::Handled();
            }
        }
    }

    return FReply::Unhandled();
}

void SWowItemSlot::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    bIsHovered = true;

    // Show item tooltip if slot has an item
    if (InventoryManager.IsValid())
    {
        const FWowItemSlot* ItemSlot = InventoryManager->GetBackpackItem(SlotIndex);
        if (ItemSlot && !ItemSlot->IsEmpty())
        {
            TSharedPtr<SToolTip> ItemTooltip = CreateItemTooltip(ItemSlot);
            SetToolTip(ItemTooltip);
        }
    }
}

void SWowItemSlot::OnMouseLeave(const FPointerEvent& MouseEvent)
{
    bIsHovered = false;

    // Clear tooltip
    SetToolTip(nullptr);
}

void SWowItemSlot::UpdateSlotDisplay()
{
    // Force refresh
    Invalidate(EInvalidateWidget::Layout);
}

FSlateColor SWowItemSlot::GetSlotBorderColor() const
{
    if (bIsHovered)
    {
        return FLinearColor(0.95f, 0.74f, 0.30f, 1.0f);
    }

    return FLinearColor(0.22f, 0.22f, 0.20f, 0.95f);
}

FSlateColor SWowItemSlot::GetItemQualityColor() const
{
    if (!InventoryManager.IsValid())
    {
        return FLinearColor(0.025f, 0.024f, 0.022f, 0.96f);
    }

    const FWowItemSlot* ItemSlot = InventoryManager->GetBackpackItem(SlotIndex);
    if (!ItemSlot || ItemSlot->IsEmpty())
    {
        return FLinearColor(0.025f, 0.024f, 0.022f, 0.96f);
    }

    FLinearColor QualityColor = FWowInventoryManager::GetQualityColor(ItemSlot->Quality);
    QualityColor.A = 1.0f;
    return QualityColor * 0.32f + FLinearColor(0.025f, 0.024f, 0.022f, 1.0f) * 0.68f;
}

FText SWowItemSlot::GetStackCountText() const
{
    if (!InventoryManager.IsValid())
        return FText::GetEmpty();

    const FWowItemSlot* ItemSlot = InventoryManager->GetBackpackItem(SlotIndex);
    if (!ItemSlot || ItemSlot->IsEmpty() || ItemSlot->StackCount <= 1)
        return FText::GetEmpty();

    return FText::AsNumber(ItemSlot->StackCount);
}

TSharedPtr<SToolTip> SWowItemSlot::CreateItemTooltip(const FWowItemSlot* ItemSlot) const
{
    if (!ItemSlot || ItemSlot->IsEmpty())
    {
        return nullptr;
    }

    return FWowItemPresentation::Tooltip(InventoryManager->GetConnectionManager(), ItemSlot->ItemEntry);
}

void SWowBagWindow::Construct(const FArguments& InArgs, TSharedPtr<FWowInventoryManager> InInventoryManager)
{
    InventoryManager = InInventoryManager;

    // Create 4x4 grid of item slots
    SAssignNew(ItemGrid, SGridPanel);

    ItemSlots.Empty();
    ItemSlots.Reserve(16);

    for (int32 Row = 0; Row < 4; ++Row)
    {
        for (int32 Col = 0; Col < 4; ++Col)
        {
            uint8 SlotIndex = Row * 4 + Col;

            TSharedPtr<SWowItemSlot> ItemSlot;
            ItemGrid->AddSlot(Col, Row)
            .Padding(FMargin(1.0f))
            [
                SAssignNew(ItemSlot, SWowItemSlot, InventoryManager)
                .SlotIndex(SlotIndex)
            ];

            ItemSlots.Add(ItemSlot);
        }
    }

    ChildSlot
    [
        SNew(SWowWindowFrame)
        .Padding(6.0f)
        [
            SNew(SVerticalBox)

            // Title bar
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 4)
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("WowUI", "BackpackTitle", "Backpack"))
                .Font(FAppStyle::GetFontStyle("SmallFont"))
                .ColorAndOpacity(FLinearColor(1.0f, 0.82f, 0.0f, 1.0f))
                .ShadowOffset(FVector2D(1.0f))
                .ShadowColorAndOpacity(FLinearColor::Black)
                .Justification(ETextJustify::Center)
            ]

            // Item grid
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                ItemGrid.ToSharedRef()
            ]
        ]
    ];

    // Bind to inventory changes
    if (InventoryManager.IsValid())
    {
        InventoryManager->OnInventoryChanged.AddSP(this, &SWowBagWindow::OnInventoryChanged);
    }

    SetBagVisibility(EVisibility::Hidden);
}

void SWowBagWindow::SetBagVisibility(EVisibility InVisibility)
{
    CurrentVisibility = InVisibility;
    SWidget::SetVisibility(InVisibility);
}

void SWowBagWindow::ToggleVisibility()
{
    if (CurrentVisibility == EVisibility::Hidden)
    {
        SetBagVisibility(EVisibility::Visible);
    }
    else
    {
        SetBagVisibility(EVisibility::Hidden);
    }
}

bool SWowBagWindow::IsBagVisible() const
{
    return CurrentVisibility != EVisibility::Hidden;
}

void SWowBagWindow::OnInventoryChanged()
{
    // Update all item slots
    for (auto& ItemSlot : ItemSlots)
    {
        if (ItemSlot.IsValid())
        {
            ItemSlot->UpdateSlotDisplay();
        }
    }
}
