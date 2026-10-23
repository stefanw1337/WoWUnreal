#include "SWowSpellbook.h"
#include "WowColor.h"
#include "WowWindowStyle.h"
#include "WowSpellPresentation.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/SpellDbc.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

void SWowSpellbook::Construct(const FArguments& InArgs)
{
	ConnectionManager = InArgs._ConnectionManager;

	// Create the main spellbook content
	SpellbookContent =
		SNew(SWowWindowFrame)
		.Padding(5)
		[
			SNew(SBox)
			.WidthOverride(300)
			.HeightOverride(400)
			[
				SNew(SVerticalBox)

				// Header with close button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 5)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Known Spells")))
						.Font(FWowWindowStyle::TitleFont())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center)
						.Text(FText::FromString(TEXT("X")))
						.OnClicked(this, &SWowSpellbook::OnCloseButtonClicked)
					]
				]

				// Spell list
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(SpellListScrollBox, SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always)
					.Orientation(Orient_Vertical)
					.ScrollBarAlwaysVisible(true)
				]
			]
		];

	// Set widget content - initially hidden
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SpellbookContent.ToSharedRef()
		]
	];

	// Initially hidden
	SetVisibility(EVisibility::Collapsed);
	bIsVisible = false;
}

void SWowSpellbook::SetSpellbookVisible(bool bVisible)
{
	bIsVisible = bVisible;

	if (bVisible)
	{
		RefreshSpellList();
		SetVisibility(EVisibility::Visible);
	}
	else
	{
		SetVisibility(EVisibility::Collapsed);
	}
}

void SWowSpellbook::RefreshSpellList()
{
	if (!SpellListScrollBox.IsValid() || !ConnectionManager.IsValid())
	{
		return;
	}

	// Clear existing content
	SpellListScrollBox->ClearChildren();

	// Get known spells from packet handler
	const TSet<uint32>& KnownSpells = ConnectionManager->PacketHandler.KnownSpells;

	// Sort spells by ID for consistent ordering
	TArray<uint32> SortedSpells = KnownSpells.Array();
	SortedSpells.Sort();

	// Add each spell to the list
	for (uint32 SpellId : SortedSpells)
	{
		SpellListScrollBox->AddSlot()
		.Padding(2)
		[
			CreateSpellRowWidget(SpellId)
		];
	}
}

TSharedRef<SWidget> SWowSpellbook::CreateSpellRowWidget(uint32 SpellId)
{
	const FSpellDbcEntry* SpellEntry = GetSpellData(SpellId);

	FString SpellName = SpellEntry ? SpellEntry->SpellName : FString::Printf(TEXT("Unknown Spell %u"), SpellId);
	FString SpellRank = SpellEntry ? FormatSpellRank(SpellEntry) : TEXT("");
	int32 SpellLevel = SpellEntry ? static_cast<int32>(SpellEntry->SpellLevel) : 0;

	FLinearColor SchoolColor = SpellEntry ? GetSpellSchoolColor(SpellEntry->SchoolMask) : FLinearColor::White;

	return SNew(SButton)
		.ButtonStyle(FCoreStyle::Get(), "NoBorder")
		.OnClicked(this, &SWowSpellbook::OnSpellRowClicked, SpellId)
		.ToolTipText(FText::FromString(SpellEntry ? FWowSpellPresentation::Tooltip(nullptr,SpellEntry->ID) : SpellName))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(5)
			[
				SNew(SHorizontalBox)

				// School color indicator
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 5, 0)
				[
					SNew(SBox)
					.WidthOverride(4)
					.HeightOverride(20)
					[
						SNew(SImage)
						.ColorAndOpacity(SchoolColor)
						.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
					]
				]

				// Spell name and rank
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(SpellName))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(SpellRank))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(WowColor::FromWow(0.7f, 0.7f, 0.7f, 1.0f))
						.Visibility(SpellRank.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					]
				]

				// Spell level
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("Lv %d"), SpellLevel)))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(WowColor::FromWow(0.8f, 0.8f, 0.8f, 1.0f))
					.Visibility(SpellLevel > 0 ? EVisibility::Visible : EVisibility::Collapsed)
				]
			]
		];
}

FReply SWowSpellbook::OnSpellRowClicked(uint32 SpellId)
{
	if (ConnectionManager.IsValid())
	{
		// Send spell cast packet directly through connection manager
		ConnectionManager->SendCastSpell(SpellId, 0); // SpellId, TargetGuid (0 for self/current target)
	}

	return FReply::Handled();
}

FReply SWowSpellbook::OnCloseButtonClicked()
{
	SetSpellbookVisible(false);
	return FReply::Handled();
}

const FSpellDbcEntry* SWowSpellbook::GetSpellData(uint32 SpellId) const
{
	// Access the global DBC store
	return FDbcStore::Get().Spells().GetById(SpellId);
}

FLinearColor SWowSpellbook::GetSpellSchoolColor(uint32 SchoolMask) const
{
	// WoW spell school colors
	if (SchoolMask & 0x02) return FLinearColor(1.0f, 1.0f, 0.0f, 1.0f); // Holy (yellow)
	if (SchoolMask & 0x04) return WowColor::FromWow(1.0f, 0.5f, 0.0f, 1.0f); // Fire (orange)
	if (SchoolMask & 0x08) return WowColor::FromWow(0.5f, 1.0f, 0.5f, 1.0f); // Nature (green)
	if (SchoolMask & 0x10) return WowColor::FromWow(0.5f, 0.5f, 1.0f, 1.0f); // Frost (blue)
	if (SchoolMask & 0x20) return WowColor::FromWow(0.5f, 0.0f, 1.0f, 1.0f); // Shadow (purple)
	if (SchoolMask & 0x40) return FLinearColor(1.0f, 0.0f, 1.0f, 1.0f); // Arcane (pink)

	return WowColor::FromWow(0.8f, 0.8f, 0.8f, 1.0f); // Physical/Unknown (gray)
}

FString SWowSpellbook::FormatSpellRank(const FSpellDbcEntry* SpellEntry) const
{
	if (!SpellEntry || SpellEntry->Rank.IsEmpty() || SpellEntry->Rank == TEXT(""))
	{
		return TEXT("");
	}

	return FString::Printf(TEXT("(%s)"), *SpellEntry->Rank);
}
