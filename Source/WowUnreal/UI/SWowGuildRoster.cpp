#include "SWowGuildRoster.h"
#include "WowWindowStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWowPanelScrollBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Engine/Engine.h"
#include "WowNetwork/Public/WowPacketHandler.h"
#include "WowNetwork/Public/WowEntity.h"
#include "WowNetwork/Public/WowOpcodes.h"

void SWowGuildRoster::Construct(const FArguments& InArgs, FWowPacketHandler* InPacketHandler)
{
    PacketHandler = InPacketHandler;

    // Create the main window structure
    ChildSlot
    [
        SNew(SBox)
        .WidthOverride(400.0f)
        .HeightOverride(500.0f)
        .Visibility_Lambda([this]() { return CurrentVisibility; })
        [
            SNew(SWowWindowFrame)
            .Padding(8.0f)
            [
                SNew(SVerticalBox)

                // Title bar with guild name and close button
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 0, 0, 8)
                [
                    SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() { return GetGuildTitle(); })
                        .Font(FWowWindowStyle::TitleFont())
                        .ColorAndOpacity(FLinearColor::Yellow)
                    ]

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center)
                        .Text(NSLOCTEXT("WowUI", "Close", "Close"))
                        .OnClicked(this, &SWowGuildRoster::OnCloseButtonClicked)
                    ]
                ]

                // Column headers
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 0, 0, 4)
                [
                    SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                    .FillWidth(0.25f)
                    [
                        SNew(STextBlock)
                        .Text(NSLOCTEXT("WowUI", "Name", "Name"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                        .ColorAndOpacity(FLinearColor::White)
                    ]

                    + SHorizontalBox::Slot()
                    .FillWidth(0.15f)
                    [
                        SNew(STextBlock)
                        .Text(NSLOCTEXT("WowUI", "Level", "Level"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                        .ColorAndOpacity(FLinearColor::White)
                    ]

                    + SHorizontalBox::Slot()
                    .FillWidth(0.20f)
                    [
                        SNew(STextBlock)
                        .Text(NSLOCTEXT("WowUI", "Class", "Class"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                        .ColorAndOpacity(FLinearColor::White)
                    ]

                    + SHorizontalBox::Slot()
                    .FillWidth(0.25f)
                    [
                        SNew(STextBlock)
                        .Text(NSLOCTEXT("WowUI", "Zone", "Zone"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                        .ColorAndOpacity(FLinearColor::White)
                    ]

                    + SHorizontalBox::Slot()
                    .FillWidth(0.15f)
                    [
                        SNew(STextBlock)
                        .Text(NSLOCTEXT("WowUI", "Status", "Status"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                        .ColorAndOpacity(FLinearColor::White)
                    ]
                ]

                // Member list
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                [
                    SWowPanelScrollBar::Wrap(SAssignNew(MemberListScrollBox, SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always)
                    .Orientation(Orient_Vertical))
                ]
            ]
        ]
    ];

    // Initial roster update
    UpdateGuildRoster();
}

void SWowGuildRoster::UpdateGuildRoster()
{
    if (!MemberListScrollBox.IsValid() || !PacketHandler)
        return;

    MemberListScrollBox->ClearChildren();

    // Add each guild member to the list
    for (const FWowGuildMember& Member : PacketHandler->GuildRoster)
    {
        MemberListScrollBox->AddSlot()
        .Padding(0, 1)
        [
            CreateGuildMemberEntry(Member)
        ];
    }
}

TSharedRef<SWidget> SWowGuildRoster::CreateGuildMemberEntry(const FWowGuildMember& Member) const
{
    return SNew(SHorizontalBox)

        // Name
        + SHorizontalBox::Slot()
        .FillWidth(0.25f)
        .Padding(2, 0)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Member.Name))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .ColorAndOpacity(GetOnlineStatusColor(Member.Status))
        ]

        // Level
        + SHorizontalBox::Slot()
        .FillWidth(0.15f)
        .Padding(2, 0)
        [
            SNew(STextBlock)
            .Text(FText::FromString(FString::Printf(TEXT("%d"), Member.Level)))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .ColorAndOpacity(GetOnlineStatusColor(Member.Status))
        ]

        // Class
        + SHorizontalBox::Slot()
        .FillWidth(0.20f)
        .Padding(2, 0)
        [
            SNew(STextBlock)
            .Text(FText::FromString(GetClassName(Member.Class)))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .ColorAndOpacity(GetOnlineStatusColor(Member.Status))
        ]

        // Zone
        + SHorizontalBox::Slot()
        .FillWidth(0.25f)
        .Padding(2, 0)
        [
            SNew(STextBlock)
            .Text(FText::FromString(GetZoneName(Member.ZoneId)))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .ColorAndOpacity(GetOnlineStatusColor(Member.Status))
        ]

        // Status
        + SHorizontalBox::Slot()
        .FillWidth(0.15f)
        .Padding(2, 0)
        [
            SNew(STextBlock)
            .Text(GetOnlineStatusText(Member.Status))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .ColorAndOpacity(GetOnlineStatusColor(Member.Status))
        ];
}

FString SWowGuildRoster::GetClassName(uint8 ClassId) const
{
    static const TMap<uint8, FString> ClassNames = {
        {1, TEXT("Warrior")},
        {2, TEXT("Paladin")},
        {3, TEXT("Hunter")},
        {4, TEXT("Rogue")},
        {5, TEXT("Priest")},
        {6, TEXT("Death Knight")},
        {7, TEXT("Shaman")},
        {8, TEXT("Mage")},
        {9, TEXT("Warlock")},
        {11, TEXT("Druid")}
    };

    if (const FString* ClassName = ClassNames.Find(ClassId))
    {
        return *ClassName;
    }
    return FString::Printf(TEXT("Class %d"), ClassId);
}

FString SWowGuildRoster::GetZoneName(uint32 ZoneId) const
{
    // For now, return zone ID. In a full implementation, this would look up zone names
    if (ZoneId == 0)
        return TEXT("Unknown");
    return FString::Printf(TEXT("Zone %d"), ZoneId);
}

FText SWowGuildRoster::GetOnlineStatusText(uint8 Status) const
{
    switch (Status)
    {
        case 0: return NSLOCTEXT("WowUI", "Offline", "Offline");
        case 1: return NSLOCTEXT("WowUI", "Online", "Online");
        case 2: return NSLOCTEXT("WowUI", "AFK", "AFK");
        case 3: return NSLOCTEXT("WowUI", "DND", "DND");
        default: return NSLOCTEXT("WowUI", "Unknown", "Unknown");
    }
}

FSlateColor SWowGuildRoster::GetOnlineStatusColor(uint8 Status) const
{
    switch (Status)
    {
        case 0: return FLinearColor::Gray;      // Offline
        case 1: return FLinearColor::White;     // Online
        case 2: return FLinearColor::Yellow;    // AFK
        case 3: return FLinearColor::Red;       // DND
        default: return FLinearColor::Gray;
    }
}

FText SWowGuildRoster::GetGuildTitle() const
{
    if (PacketHandler && !PacketHandler->GuildName.IsEmpty())
    {
        return FText::FromString(FString::Printf(TEXT("Guild Roster - %s"), *PacketHandler->GuildName));
    }
    return NSLOCTEXT("WowUI", "GuildRoster", "Guild Roster");
}

void SWowGuildRoster::ToggleVisibility()
{
    if (CurrentVisibility == EVisibility::Visible)
    {
        CurrentVisibility = EVisibility::Hidden;
    }
    else
    {
        CurrentVisibility = EVisibility::Visible;
        RequestGuildRoster();
        UpdateGuildRoster();
    }
}

bool SWowGuildRoster::IsVisible() const
{
    return CurrentVisibility == EVisibility::Visible;
}

FReply SWowGuildRoster::OnCloseButtonClicked()
{
    CurrentVisibility = EVisibility::Hidden;
    return FReply::Handled();
}

void SWowGuildRoster::RequestGuildRoster()
{
    if (PacketHandler && PacketHandler->OnSendPacket.IsBound())
    {
        TArray<uint8> EmptyData;
        PacketHandler->OnSendPacket.ExecuteIfBound(WowOpcode::CMSG_GUILD_ROSTER, EmptyData);
    }
}
