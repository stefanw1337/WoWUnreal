#include "SWowCharacterPanel.h"
#include "WowFontManager.h"
#include "WowWindowStyle.h"
#include "WowItemPresentation.h"
#include "WowInventoryManager.h"
#include "WowConnectionManager.h"
#include "WowEntity.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SBoxPanel.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "HAL/UnrealMemory.h"

void SWowEquipmentSlot::Construct(const FArguments& InArgs, TSharedPtr<FWowInventoryManager> InInventoryManager)
{
    InventoryManager = InInventoryManager;
    SlotIndex = InArgs._SlotIndex;
    SlotName = InArgs._SlotName;

    ChildSlot
    [
        SAssignNew(SlotBorder, SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .BorderBackgroundColor(this, &SWowEquipmentSlot::GetSlotBorderColor)
        .Padding(2.0f)
        [
            SNew(SBox)
            .WidthOverride(40.0f)
            .HeightOverride(40.0f)
            [
                SNew(SOverlay)

                // Item background (colored by quality)
                + SOverlay::Slot()
                [
                    SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(this, &SWowEquipmentSlot::GetItemQualityColor)
                    .Padding(0)
                ]

                // Slot label
                + SOverlay::Slot()
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(this, &SWowEquipmentSlot::GetSlotText)
                    .ColorAndOpacity(FLinearColor::White)
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .Justification(ETextJustify::Center)
                ]
            ]
        ]
    ];

    UpdateSlotDisplay();
}

FReply SWowEquipmentSlot::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && InventoryManager.IsValid())
    {
        const FWowItemSlot* ItemSlot = InventoryManager->GetEquippedItem(SlotIndex);
        if (ItemSlot && !ItemSlot->IsEmpty())
        {
            InventoryManager->AutoStoreItem(ItemSlot->Bag, ItemSlot->Slot, 255);
            return FReply::Handled();
        }
    }

    return FReply::Unhandled();
}

void SWowEquipmentSlot::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    bIsHovered = true;

    if (!InventoryManager.IsValid())
    {
        return;
    }

    const FWowItemSlot* ItemSlot = InventoryManager->GetEquippedItem(SlotIndex);
    if (!ItemSlot || ItemSlot->IsEmpty())
    {
        return;
    }

    // Equipped items have nothing to compare against.
    SetToolTip(FWowItemPresentation::Tooltip(InventoryManager->GetConnectionManager(), ItemSlot->ItemEntry, false));
}

void SWowEquipmentSlot::OnMouseLeave(const FPointerEvent& MouseEvent)
{
    bIsHovered = false;
    SetToolTip(nullptr);
}

void SWowEquipmentSlot::UpdateSlotDisplay()
{
    // Force refresh
    Invalidate(EInvalidateWidget::Layout);
}

FSlateColor SWowEquipmentSlot::GetSlotBorderColor() const
{
    if (bIsHovered)
        return FLinearColor::Yellow;

    return FLinearColor(1.0f, 0.84f, 0.0f); // Gold border
}

FSlateColor SWowEquipmentSlot::GetItemQualityColor() const
{
    if (!InventoryManager.IsValid())
        return FLinearColor::Gray;

    const FWowItemSlot* ItemSlot = InventoryManager->GetEquippedItem(SlotIndex);
    if (!ItemSlot || ItemSlot->IsEmpty())
        return FLinearColor(0.2f, 0.2f, 0.2f); // Dark background for empty slot

    return FWowInventoryManager::GetQualityColor(ItemSlot->Quality);
}

FText SWowEquipmentSlot::GetSlotText() const
{
    if (!InventoryManager.IsValid())
        return FText::FromString(SlotName);

    const FWowItemSlot* ItemSlot = InventoryManager->GetEquippedItem(SlotIndex);
    if (!ItemSlot || ItemSlot->IsEmpty())
        return FText::FromString(SlotName); // Show slot name when empty

    // Show item entry ID when equipped (placeholder)
    return FText::AsNumber(ItemSlot->ItemEntry);
}

void SWowCharacterPanel::Construct(const FArguments& InArgs, TSharedPtr<FWowInventoryManager> InInventoryManager, UWowConnectionManager* InConnectionManager)
{
    InventoryManager = InInventoryManager;
    ConnectionManager = InConnectionManager;

    // Equipment slot layout (simplified)
    TSharedPtr<SVerticalBox> MainLayout;

    ChildSlot
    [
        SNew(SWowWindowFrame)
        .Padding(8.0f)
        [
            SAssignNew(MainLayout, SVerticalBox)

            // Character name and level
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 5)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .HAlign(HAlign_Center)
                [
                    SAssignNew(CharacterNameText, STextBlock)
                    .Text(NSLOCTEXT("WowUI", "CharacterName", "Character"))
                    .Font(FWowFontManager::GetScrollFont(12))
                    .ColorAndOpacity(FLinearColor::White)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(5, 0, 0, 0)
                [
                    SAssignNew(CharacterLevelText, STextBlock)
                    .Text(NSLOCTEXT("WowUI", "Level", "Level 1"))
                    .Font(FAppStyle::GetFontStyle("SmallFont"))
                    .ColorAndOpacity(FLinearColor::Yellow)
                ]
            ]
        ]
    ];

    // Create equipment slots in a simplified layout
    struct FSlotInfo
    {
        uint8 SlotIndex;
        const TCHAR* SlotName;
    };

    const FSlotInfo SlotInfos[] = {
        { FWowInventoryManager::EQUIP_SLOT_HEAD, TEXT("Head") },
        { FWowInventoryManager::EQUIP_SLOT_SHOULDERS, TEXT("Shoulders") },
        { FWowInventoryManager::EQUIP_SLOT_CHEST, TEXT("Chest") },
        { FWowInventoryManager::EQUIP_SLOT_LEGS, TEXT("Legs") },
        { FWowInventoryManager::EQUIP_SLOT_FEET, TEXT("Feet") },
        { FWowInventoryManager::EQUIP_SLOT_HANDS, TEXT("Hands") },
        { FWowInventoryManager::EQUIP_SLOT_WRISTS, TEXT("Wrists") },
        { FWowInventoryManager::EQUIP_SLOT_WAIST, TEXT("Waist") },
        { FWowInventoryManager::EQUIP_SLOT_BACK, TEXT("Back") },
        { FWowInventoryManager::EQUIP_SLOT_MAINHAND, TEXT("Main Hand") },
        { FWowInventoryManager::EQUIP_SLOT_OFFHAND, TEXT("Off Hand") },
        { FWowInventoryManager::EQUIP_SLOT_RANGED, TEXT("Ranged") }
    };

    EquipmentSlots.Empty();
    EquipmentSlots.Reserve(UE_ARRAY_COUNT(SlotInfos));

    // Create a grid layout for equipment slots (3 columns)
    TSharedPtr<SGridPanel> EquipmentGrid;
    MainLayout->AddSlot()
    .AutoHeight()
    [
        SAssignNew(EquipmentGrid, SGridPanel)
    ];

    int32 SlotCount = UE_ARRAY_COUNT(SlotInfos);
    for (int32 i = 0; i < SlotCount; ++i)
    {
        const FSlotInfo& Info = SlotInfos[i];
        int32 Row = i / 3;
        int32 Col = i % 3;

        TSharedPtr<SWowEquipmentSlot> EquipmentSlot;
        EquipmentGrid->AddSlot(Col, Row)
        .Padding(2.0f)
        [
            SAssignNew(EquipmentSlot, SWowEquipmentSlot, InventoryManager)
            .SlotIndex(Info.SlotIndex)
            .SlotName(Info.SlotName)
        ];

        EquipmentSlots.Add(EquipmentSlot);
    }

    // Add stats section
    MainLayout->AddSlot()
    .AutoHeight()
    .Padding(0, 10, 0, 0)
    [
        SNew(SHorizontalBox)

        // Left column: Basic stats
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .Padding(0, 0, 5, 0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("WowUI", "BasicStats", "Basic Stats"))
                .Font(FAppStyle::GetFontStyle("SmallFont"))
                .ColorAndOpacity(FLinearColor::Yellow)
                .Justification(ETextJustify::Center)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 2, 0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WowUI", "Strength", "Strength: "))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(StrengthText, STextBlock)
                    .Text(FText::AsNumber(0))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                    .Justification(ETextJustify::Right)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 1, 0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WowUI", "Agility", "Agility: "))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(AgilityText, STextBlock)
                    .Text(FText::AsNumber(0))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                    .Justification(ETextJustify::Right)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 1, 0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WowUI", "Stamina", "Stamina: "))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(StaminaText, STextBlock)
                    .Text(FText::AsNumber(0))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                    .Justification(ETextJustify::Right)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 1, 0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WowUI", "Intellect", "Intellect: "))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(IntellectText, STextBlock)
                    .Text(FText::AsNumber(0))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                    .Justification(ETextJustify::Right)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 1, 0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WowUI", "Spirit", "Spirit: "))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(SpiritText, STextBlock)
                    .Text(FText::AsNumber(0))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                    .Justification(ETextJustify::Right)
                ]
            ]
        ]

        // Right column: Resistances
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .Padding(5, 0, 0, 0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("WowUI", "Resistances", "Resistances"))
                .Font(FAppStyle::GetFontStyle("SmallFont"))
                .ColorAndOpacity(FLinearColor::Yellow)
                .Justification(ETextJustify::Center)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 2, 0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WowUI", "Armor", "Armor: "))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::Gray)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(ArmorText, STextBlock)
                    .Text(FText::AsNumber(0))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                    .Justification(ETextJustify::Right)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 1, 0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WowUI", "Holy", "Holy: "))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::Yellow)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(HolyResText, STextBlock)
                    .Text(FText::AsNumber(0))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                    .Justification(ETextJustify::Right)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 1, 0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WowUI", "Fire", "Fire: "))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::Red)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(FireResText, STextBlock)
                    .Text(FText::AsNumber(0))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                    .Justification(ETextJustify::Right)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 1, 0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WowUI", "Nature", "Nature: "))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::Green)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(NatureResText, STextBlock)
                    .Text(FText::AsNumber(0))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                    .Justification(ETextJustify::Right)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 1, 0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WowUI", "Frost", "Frost: "))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor(0.0f, 1.0f, 1.0f, 1.0f)) // Cyan
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(FrostResText, STextBlock)
                    .Text(FText::AsNumber(0))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                    .Justification(ETextJustify::Right)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 1, 0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WowUI", "Shadow", "Shadow: "))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor(0.5f, 0.0f, 1.0f))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(ShadowResText, STextBlock)
                    .Text(FText::AsNumber(0))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                    .Justification(ETextJustify::Right)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 1, 0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WowUI", "Arcane", "Arcane: "))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor(1.0f, 0.0f, 1.0f))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(ArcaneResText, STextBlock)
                    .Text(FText::AsNumber(0))
                    .Font(FAppStyle::GetFontStyle("TinyFont"))
                    .ColorAndOpacity(FLinearColor::White)
                    .Justification(ETextJustify::Right)
                ]
            ]
        ]
    ];

    // Add attack power section
    MainLayout->AddSlot()
    .AutoHeight()
    .Padding(0, 5, 0, 0)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("WowUI", "AttackPower", "Attack Power: "))
                .Font(FAppStyle::GetFontStyle("TinyFont"))
                .ColorAndOpacity(FLinearColor::White)
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SAssignNew(AttackPowerText, STextBlock)
                .Text(FText::AsNumber(0))
                .Font(FAppStyle::GetFontStyle("TinyFont"))
                .ColorAndOpacity(FLinearColor::White)
                .Justification(ETextJustify::Right)
            ]
        ]
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .Padding(5, 0, 0, 0)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("WowUI", "RangedAttackPower", "Ranged AP: "))
                .Font(FAppStyle::GetFontStyle("TinyFont"))
                .ColorAndOpacity(FLinearColor::White)
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SAssignNew(RangedAttackPowerText, STextBlock)
                .Text(FText::AsNumber(0))
                .Font(FAppStyle::GetFontStyle("TinyFont"))
                .ColorAndOpacity(FLinearColor::White)
                .Justification(ETextJustify::Right)
            ]
        ]
    ];

    // Bind to inventory changes
    if (InventoryManager.IsValid())
    {
        InventoryManager->OnInventoryChanged.AddSP(this, &SWowCharacterPanel::OnInventoryChanged);
    }

    SetPanelVisibility(EVisibility::Hidden);
}

void SWowCharacterPanel::SetPanelVisibility(EVisibility InVisibility)
{
    CurrentVisibility = InVisibility;
    SWidget::SetVisibility(InVisibility);
}

void SWowCharacterPanel::ToggleVisibility()
{
    if (CurrentVisibility == EVisibility::Hidden)
    {
        SetPanelVisibility(EVisibility::SelfHitTestInvisible);
        UpdateStats(); // Refresh stats when panel is shown
    }
    else
    {
        SetPanelVisibility(EVisibility::Hidden);
    }
}

bool SWowCharacterPanel::IsPanelVisible() const
{
    return CurrentVisibility != EVisibility::Hidden;
}

void SWowCharacterPanel::OnInventoryChanged()
{
    // Update all equipment slots
    for (auto& EquipmentSlot : EquipmentSlots)
    {
        if (EquipmentSlot.IsValid())
        {
            EquipmentSlot->UpdateSlotDisplay();
        }
    }
}

void SWowCharacterPanel::UpdateStats()
{
    if (!ConnectionManager)
    {
        return;
    }

    const FWowPlayerEntity* LocalPlayer = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
    if (!LocalPlayer)
    {
        return;
    }

    // Update character name and level
    if (CharacterNameText.IsValid())
    {
        // Try to get player name from cache, fallback to "Player"
        FString PlayerName = TEXT("Player");
        if (const FString* FoundName = ConnectionManager->PacketHandler.PlayerNameCache.Find(LocalPlayer->Guid))
        {
            PlayerName = *FoundName;
        }
        CharacterNameText->SetText(FText::FromString(PlayerName));
    }

    if (CharacterLevelText.IsValid())
    {
        CharacterLevelText->SetText(FText::Format(NSLOCTEXT("WowUI", "LevelFormat", "Level {0}"), FText::AsNumber(LocalPlayer->GetLevel())));
    }

    // Update basic stats
    if (StrengthText.IsValid())
    {
        StrengthText->SetText(FText::AsNumber(LocalPlayer->GetStrength()));
    }

    if (AgilityText.IsValid())
    {
        AgilityText->SetText(FText::AsNumber(LocalPlayer->GetAgility()));
    }

    if (StaminaText.IsValid())
    {
        StaminaText->SetText(FText::AsNumber(LocalPlayer->GetStamina()));
    }

    if (IntellectText.IsValid())
    {
        IntellectText->SetText(FText::AsNumber(LocalPlayer->GetIntellect()));
    }

    if (SpiritText.IsValid())
    {
        SpiritText->SetText(FText::AsNumber(LocalPlayer->GetSpirit()));
    }

    // Update resistances
    if (ArmorText.IsValid())
    {
        ArmorText->SetText(FText::AsNumber(LocalPlayer->GetArmor()));
    }

    if (HolyResText.IsValid())
    {
        HolyResText->SetText(FText::AsNumber(LocalPlayer->GetResistance(1))); // Holy = index 1
    }

    if (FireResText.IsValid())
    {
        FireResText->SetText(FText::AsNumber(LocalPlayer->GetResistance(2))); // Fire = index 2
    }

    if (NatureResText.IsValid())
    {
        NatureResText->SetText(FText::AsNumber(LocalPlayer->GetResistance(3))); // Nature = index 3
    }

    if (FrostResText.IsValid())
    {
        FrostResText->SetText(FText::AsNumber(LocalPlayer->GetResistance(4))); // Frost = index 4
    }

    if (ShadowResText.IsValid())
    {
        ShadowResText->SetText(FText::AsNumber(LocalPlayer->GetResistance(5))); // Shadow = index 5
    }

    if (ArcaneResText.IsValid())
    {
        ArcaneResText->SetText(FText::AsNumber(LocalPlayer->GetResistance(6))); // Arcane = index 6
    }

    // Update attack power
    if (AttackPowerText.IsValid())
    {
        AttackPowerText->SetText(FText::AsNumber(LocalPlayer->GetAttackPower()));
    }

    if (RangedAttackPowerText.IsValid())
    {
        RangedAttackPowerText->SetText(FText::AsNumber(LocalPlayer->GetRangedAttackPower()));
    }
}

void SWowCharacterPanel::SetConnectionManager(UWowConnectionManager* InConnectionManager)
{
    ConnectionManager = InConnectionManager;
}
