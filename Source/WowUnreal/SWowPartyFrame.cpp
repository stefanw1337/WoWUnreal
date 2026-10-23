#include "SWowPartyFrame.h"

#include "Algo/Sort.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowPartyFrame, Log, All);

void SWowPartyFrame::Construct(const FArguments& InArgs)
{
    ConnectionManager = InArgs._ConnectionManager;

    ChildSlot
    [
        SNew(SBorder)
        .Visibility(this, &SWowPartyFrame::GetMemberVisibility)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f))
        .Padding(8.0f)
        [
            SAssignNew(MembersContainer, SVerticalBox)
        ]
    ];
}

void SWowPartyFrame::UpdatePartyInfo(const FWowGroupInfo& GroupInfo)
{
    CurrentGroupInfo = GroupInfo;
    MemberWidgets.Empty();
    RaidMarkButtons.Reset();

    if (!MembersContainer.IsValid())
    {
        return;
    }

    MembersContainer->ClearChildren();

    if (GroupInfo.IsEmpty())
    {
        return;
    }

    MembersContainer->AddSlot()
        .AutoHeight()
        .Padding(0, 0, 0, 6)
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
            .BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.9f))
            .Padding(6.0f)
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
                        .Text(this, &SWowPartyFrame::GetFrameTitleText)
                        .ColorAndOpacity(FLinearColor(0.95f, 0.82f, 0.45f))
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(STextBlock)
                        .Text(this, &SWowPartyFrame::GetFrameStatusText)
                        .ColorAndOpacity(FLinearColor(0.82f, 0.82f, 0.82f))
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 6, 0, 0)
                [
                    SNew(SBorder)
                    .Visibility(this, &SWowPartyFrame::GetRaidControlsVisibility)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
                    .BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.08f, 0.92f))
                    .Padding(6.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            .Padding(0, 0, 4, 0)
                            [
                                SNew(SButton)
                                .Text(FText::FromString(TEXT("Ready Check")))
                                .OnClicked(this, &SWowPartyFrame::OnStartReadyCheckClicked)
                                .Visibility(this, &SWowPartyFrame::GetReadyCheckStartVisibility)
                                .IsEnabled(this, &SWowPartyFrame::CanLocalPlayerControlRaid)
                            ]
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            .Padding(0, 0, 4, 0)
                            [
                                SNew(SButton)
                                .Text(FText::FromString(TEXT("Ready")))
                                .OnClicked(this, &SWowPartyFrame::OnReadyClicked)
                                .Visibility(this, &SWowPartyFrame::GetReadyCheckResponseVisibility)
                            ]
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            .Padding(0, 0, 6, 0)
                            [
                                SNew(SButton)
                                .Text(FText::FromString(TEXT("Not Ready")))
                                .OnClicked(this, &SWowPartyFrame::OnNotReadyClicked)
                                .Visibility(this, &SWowPartyFrame::GetReadyCheckResponseVisibility)
                            ]
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            [
                                SNew(SButton)
                                .Text(FText::FromString(TEXT("Refresh Marks")))
                                .OnClicked(this, &SWowPartyFrame::OnRefreshRaidMarksClicked)
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0, 6, 0, 0)
                        [
                            SAssignNew(RaidMarkButtons, SWrapBox)
                        ]
                    ]
                ]
            ]
        ];

    if (RaidMarkButtons.IsValid())
    {
        for (int32 IconIndex = 0; IconIndex < 8; ++IconIndex)
        {
            RaidMarkButtons->AddSlot()
                .Padding(2.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(GetRaidMarkLabel(IconIndex)))
                    .OnClicked(this, &SWowPartyFrame::OnRaidMarkClicked, IconIndex)
                    .IsEnabled(this, &SWowPartyFrame::CanLocalPlayerControlRaid)
                ];
        }
    }

    TArray<FWowGroupMember> DisplayMembers = GroupInfo.Members;
    if (GroupInfo.IsRaidGroup())
    {
        const FWowGroupMember LocalMember = BuildLocalMember();
        if (LocalMember.Guid != 0)
        {
            DisplayMembers.Add(LocalMember);
        }

        const uint64 LocalPlayerGuid = ConnectionManager.IsValid()
            ? ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid
            : 0;
        Algo::Sort(DisplayMembers, [LocalPlayerGuid](const FWowGroupMember& A, const FWowGroupMember& B)
        {
            if (A.Group != B.Group)
            {
                return A.Group < B.Group;
            }

            const bool bALocal = A.Guid == LocalPlayerGuid;
            const bool bBLocal = B.Guid == LocalPlayerGuid;
            if (bALocal != bBLocal)
            {
                return bALocal;
            }

            if (A.Name != B.Name)
            {
                return A.Name < B.Name;
            }

            return A.Guid < B.Guid;
        });
    }

    const uint64 LocalPlayerGuid = ConnectionManager.IsValid()
        ? ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid
        : 0;

    TSharedPtr<SWrapBox> RaidMemberContainer;
    if (GroupInfo.IsRaidGroup())
    {
        MembersContainer->AddSlot()
            .AutoHeight()
            [
                SAssignNew(RaidMemberContainer, SWrapBox)
            ];
    }

    for (const FWowGroupMember& Member : DisplayMembers)
    {
        const bool bIsLocalPlayer = LocalPlayerGuid != 0 && LocalPlayerGuid == Member.Guid;
        if (!GroupInfo.IsRaidGroup() && bIsLocalPlayer)
        {
            continue;
        }

        FPartyMemberWidget MemberWidget;
        MemberWidget.Guid = Member.Guid;
        MemberWidget.Name = Member.Name;
        MemberWidget.Level = Member.Level;
        MemberWidget.Class = Member.Class;
        MemberWidget.Subgroup = Member.Group;
        MemberWidget.Flags = Member.Flags;
        MemberWidget.Roles = Member.Roles;
        MemberWidget.Status = Member.Status;
        MemberWidget.bIsLocalPlayer = bIsLocalPlayer;
        MemberWidget.bIsLeader = Member.Guid == GroupInfo.LeaderGuid;

        if (ConnectionManager.IsValid())
        {
            if (const FWowEntity* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Member.Guid))
            {
                if (Entity->IsUnit())
                {
                    const FWowUnitEntity& UnitEntity = static_cast<const FWowUnitEntity&>(*Entity);
                    MemberWidget.CurrentHealth = FMath::Max(0, Entity->GetHealth());
                    MemberWidget.MaxHealth = FMath::Max(1, Entity->GetMaxHealth());
                    MemberWidget.CurrentPower = FMath::Max(0, UnitEntity.GetPower(0));
                    MemberWidget.MaxPower = FMath::Max(1, UnitEntity.GetMaxPower(0));
                    MemberWidget.bInRange = true;
                }
            }
        }

        const int32 MemberIndex = MemberWidgets.Num();
        MemberWidgets.Add(MoveTemp(MemberWidget));
        TSharedRef<SWidget> MemberWidgetRef = CreateMemberWidget(Member);

        if (RaidMemberContainer.IsValid())
        {
            RaidMemberContainer->AddSlot()
                .Padding(2.0f)
                [
                    SNew(SBox)
                    .WidthOverride(220.0f)
                    [
                        MemberWidgetRef
                    ]
                ];
        }
        else
        {
            MembersContainer->AddSlot()
                .AutoHeight()
                .Padding(0, 2)
                [
                    MemberWidgetRef
                ];
        }
    }

    if (ConnectionManager.IsValid())
    {
        for (const auto& EntityPair : ConnectionManager->PacketHandler.EntityManager.GetAll())
        {
            UpdateMemberFromEntity(*EntityPair.Value);
        }
    }

    UE_LOG(LogWowPartyFrame, Log, TEXT("Updated %s frame with %d visible members"),
        CurrentGroupInfo.IsRaidGroup() ? TEXT("raid") : TEXT("party"),
        MemberWidgets.Num());
}

void SWowPartyFrame::UpdateMemberFromEntity(const FWowEntity& Entity)
{
    if (!Entity.IsUnit())
    {
        return;
    }

    for (FPartyMemberWidget& Widget : MemberWidgets)
    {
        if (Widget.Guid == Entity.Guid)
        {
            const FWowUnitEntity& UnitEntity = static_cast<const FWowUnitEntity&>(Entity);
            Widget.CurrentHealth = Entity.GetHealth();
            Widget.MaxHealth = Entity.GetMaxHealth();
            Widget.CurrentPower = UnitEntity.GetPower(0);
            Widget.MaxPower = UnitEntity.GetMaxPower(0);
            Widget.bInRange = true;
            UpdateMemberWidget(Widget);
            break;
        }
    }
}

void SWowPartyFrame::ClearParty()
{
    CurrentGroupInfo.Clear();
    MemberWidgets.Empty();
    RaidMarkButtons.Reset();
    if (MembersContainer.IsValid())
    {
        MembersContainer->ClearChildren();
    }
}

TSharedRef<SWidget> SWowPartyFrame::CreateMemberWidget(const FWowGroupMember& Member)
{
    const int32 MemberIndex = MemberWidgets.Num() - 1;
    FPartyMemberWidget& Widget = MemberWidgets[MemberIndex];

    return SAssignNew(Widget.MemberBorder, SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
        .BorderBackgroundColor_Lambda([this, MemberIndex]() { return GetMemberTint(MemberIndex); })
        .Padding(CurrentGroupInfo.IsRaidGroup() ? 3.0f : 4.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(Widget.NameText, STextBlock)
                    .Text(this, &SWowPartyFrame::GetMemberNameText, MemberIndex)
                    .ColorAndOpacity(GetClassColor(Member.Class))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(6, 0, 0, 0)
                [
                    SNew(STextBlock)
                    .Text(this, &SWowPartyFrame::GetMemberMetaText, MemberIndex)
                    .ColorAndOpacity(FLinearColor(0.82f, 0.82f, 0.82f))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(6, 0, 0, 0)
                [
                    SAssignNew(Widget.LevelText, STextBlock)
                    .Text(this, &SWowPartyFrame::GetMemberLevelText, MemberIndex)
                    .ColorAndOpacity(FLinearColor::White)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 2)
            [
                SAssignNew(Widget.HealthBar, SProgressBar)
                .Percent(this, &SWowPartyFrame::GetHealthPercent, MemberIndex)
                .FillColorAndOpacity(FLinearColor::Green)
                .BackgroundImage(FAppStyle::GetBrush("ProgressBar.Background"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 1, 0, 0)
            [
                SAssignNew(Widget.PowerBar, SProgressBar)
                .Percent(this, &SWowPartyFrame::GetPowerPercent, MemberIndex)
                .FillColorAndOpacity(GetPowerColor(Member.Class))
                .BackgroundImage(FAppStyle::GetBrush("ProgressBar.Background"))
            ]
        ];
}

void SWowPartyFrame::UpdateMemberWidget(FPartyMemberWidget& Widget)
{
    static_cast<void>(Widget);
    // Slate attribute bindings keep this widget current.
}

FWowGroupMember SWowPartyFrame::BuildLocalMember() const
{
    FWowGroupMember LocalMember;
    if (!ConnectionManager.IsValid())
    {
        return LocalMember;
    }

    LocalMember.Guid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
    LocalMember.Name = ConnectionManager->GetCharacterName();
    LocalMember.Group = CurrentGroupInfo.SelfSubgroup;
    LocalMember.Flags = CurrentGroupInfo.SelfFlags;
    LocalMember.Roles = CurrentGroupInfo.SelfRoles;
    LocalMember.Status = WowGroupMemberStatus::ONLINE;

    if (FWowPlayerEntity* LocalPlayer = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer())
    {
        LocalMember.Level = static_cast<uint8>(LocalPlayer->GetLevel());
        LocalMember.Class = LocalPlayer->GetClassId();
    }

    if (LocalMember.Name.IsEmpty())
    {
        LocalMember.Name = TEXT("Player");
    }

    return LocalMember;
}

FString SWowPartyFrame::GetRaidMarkLabel(int32 IconIndex) const
{
    return FString::Printf(TEXT("M%d"), IconIndex + 1);
}

FString SWowPartyFrame::GetReadyStateLabel(const FPartyMemberWidget& Widget) const
{
    if (!ConnectionManager.IsValid())
    {
        return FString();
    }

    const FWowReadyCheckState& ReadyCheck = ConnectionManager->PacketHandler.ReadyCheck;
    if (const uint8* Response = ReadyCheck.FindResponse(Widget.Guid))
    {
        const bool bOnline = (Widget.Status & WowGroupMemberStatus::ONLINE) != 0;
        if (*Response == WowReadyCheckResponse::READY)
        {
            return TEXT("RDY");
        }

        return bOnline ? TEXT("NR") : TEXT("OFF");
    }

    if (ReadyCheck.bActive)
    {
        return TEXT("WAIT");
    }

    return FString();
}

FString SWowPartyFrame::GetMemberMetaLabel(const FPartyMemberWidget& Widget) const
{
    TArray<FString> Labels;
    if (CurrentGroupInfo.IsRaidGroup())
    {
        Labels.Add(FString::Printf(TEXT("G%d"), Widget.Subgroup + 1));
    }

    if (Widget.bIsLeader)
    {
        Labels.Add(TEXT("L"));
    }
    else if ((Widget.Flags & WowGroupMemberFlags::ASSISTANT) != 0)
    {
        Labels.Add(TEXT("A"));
    }

    if (ConnectionManager.IsValid())
    {
        const int32 IconIndex = ConnectionManager->PacketHandler.RaidTargets.GetIconIndex(Widget.Guid);
        if (IconIndex != INDEX_NONE)
        {
            Labels.Add(GetRaidMarkLabel(IconIndex));
        }
    }

    const FString ReadyState = GetReadyStateLabel(Widget);
    if (!ReadyState.IsEmpty())
    {
        Labels.Add(ReadyState);
    }

    return FString::Join(Labels, TEXT(" "));
}

bool SWowPartyFrame::CanLocalPlayerControlRaid() const
{
    if (!ConnectionManager.IsValid() || !CurrentGroupInfo.IsRaidGroup())
    {
        return false;
    }

    const uint64 LocalPlayerGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
    return CurrentGroupInfo.LocalPlayerIsLeader(LocalPlayerGuid) || CurrentGroupInfo.LocalPlayerIsAssistant();
}

bool SWowPartyFrame::HasPendingReadyCheckResponse() const
{
    if (!ConnectionManager.IsValid())
    {
        return false;
    }

    const FWowReadyCheckState& ReadyCheck = ConnectionManager->PacketHandler.ReadyCheck;
    if (!ReadyCheck.bActive)
    {
        return false;
    }

    const uint64 LocalPlayerGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
    return LocalPlayerGuid != 0 && ReadyCheck.FindResponse(LocalPlayerGuid) == nullptr;
}

FReply SWowPartyFrame::OnStartReadyCheckClicked()
{
    if (ConnectionManager.IsValid())
    {
        ConnectionManager->SendStartReadyCheck();
    }

    return FReply::Handled();
}

FReply SWowPartyFrame::OnReadyClicked()
{
    if (ConnectionManager.IsValid())
    {
        ConnectionManager->SendReadyCheckConfirm(true);
    }

    return FReply::Handled();
}

FReply SWowPartyFrame::OnNotReadyClicked()
{
    if (ConnectionManager.IsValid())
    {
        ConnectionManager->SendReadyCheckConfirm(false);
    }

    return FReply::Handled();
}

FReply SWowPartyFrame::OnRefreshRaidMarksClicked()
{
    if (ConnectionManager.IsValid())
    {
        ConnectionManager->SendRequestRaidTargetIcons();
    }

    return FReply::Handled();
}

FReply SWowPartyFrame::OnRaidMarkClicked(int32 IconIndex)
{
    if (ConnectionManager.IsValid())
    {
        const int64 TargetGuid = ConnectionManager->GetTargetGuid();
        if (TargetGuid != 0)
        {
            ConnectionManager->SendSetRaidTargetIcon(IconIndex, TargetGuid);
        }
    }

    return FReply::Handled();
}

FLinearColor SWowPartyFrame::GetClassColor(uint8 ClassId) const
{
    switch (ClassId)
    {
    case 1: return FLinearColor(0.78f, 0.61f, 0.43f);
    case 2: return FLinearColor(0.96f, 0.55f, 0.73f);
    case 3: return FLinearColor(0.67f, 0.83f, 0.45f);
    case 4: return FLinearColor(1.0f, 0.96f, 0.41f);
    case 5: return FLinearColor(1.0f, 1.0f, 1.0f);
    case 6: return FLinearColor(0.77f, 0.12f, 0.23f);
    case 7: return FLinearColor(0.0f, 0.44f, 0.87f);
    case 8: return FLinearColor(0.41f, 0.8f, 0.94f);
    case 9: return FLinearColor(0.58f, 0.51f, 0.79f);
    case 11: return FLinearColor(1.0f, 0.49f, 0.04f);
    default: return FLinearColor::White;
    }
}

FLinearColor SWowPartyFrame::GetPowerColor(uint8 ClassId) const
{
    switch (ClassId)
    {
    case 1:
    case 6:
        return FLinearColor(1.0f, 0.0f, 0.0f);
    case 3:
    case 4:
        return FLinearColor(1.0f, 1.0f, 0.0f);
    default:
        return FLinearColor(0.0f, 0.0f, 1.0f);
    }
}

FLinearColor SWowPartyFrame::GetMemberTint(int32 MemberIndex) const
{
    if (!MemberWidgets.IsValidIndex(MemberIndex))
    {
        return FLinearColor(0.1f, 0.1f, 0.1f, 0.9f);
    }

    const FPartyMemberWidget& Widget = MemberWidgets[MemberIndex];
    if ((Widget.Status & WowGroupMemberStatus::ONLINE) == 0)
    {
        return FLinearColor(0.08f, 0.08f, 0.08f, 0.92f);
    }

    const FString ReadyState = GetReadyStateLabel(Widget);
    if (ReadyState == TEXT("RDY"))
    {
        return FLinearColor(0.08f, 0.24f, 0.1f, 0.92f);
    }
    if (ReadyState == TEXT("NR") || ReadyState == TEXT("OFF"))
    {
        return FLinearColor(0.28f, 0.08f, 0.08f, 0.92f);
    }
    if (ReadyState == TEXT("WAIT"))
    {
        return FLinearColor(0.24f, 0.18f, 0.06f, 0.92f);
    }
    if (Widget.bIsLocalPlayer)
    {
        return FLinearColor(0.08f, 0.13f, 0.24f, 0.92f);
    }
    if (Widget.bIsLeader)
    {
        return FLinearColor(0.22f, 0.19f, 0.08f, 0.92f);
    }

    return FLinearColor(0.1f, 0.1f, 0.1f, 0.9f);
}

EVisibility SWowPartyFrame::GetMemberVisibility() const
{
    return (!CurrentGroupInfo.IsEmpty() && MemberWidgets.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SWowPartyFrame::GetRaidControlsVisibility() const
{
    // Raid controls (marks, ready check) are collapsed by default to keep the frame compact.
    // FrameXML handles raid utility panel display.
    return EVisibility::Collapsed;
}

EVisibility SWowPartyFrame::GetReadyCheckStartVisibility() const
{
    return (CanLocalPlayerControlRaid()
            && ConnectionManager.IsValid()
            && !ConnectionManager->PacketHandler.ReadyCheck.bActive)
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

EVisibility SWowPartyFrame::GetReadyCheckResponseVisibility() const
{
    return HasPendingReadyCheckResponse() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SWowPartyFrame::GetFrameTitleText() const
{
    if (CurrentGroupInfo.IsRaidGroup())
    {
        return FText::FromString(FString::Printf(TEXT("Raid (%d)"), CurrentGroupInfo.MemberCount));
    }

    return FText::FromString(FString::Printf(TEXT("Party (%d)"), FMath::Max(0, CurrentGroupInfo.MemberCount - 1)));
}

FText SWowPartyFrame::GetFrameStatusText() const
{
    if (!ConnectionManager.IsValid())
    {
        return FText::GetEmpty();
    }

    const FWowReadyCheckState& ReadyCheck = ConnectionManager->PacketHandler.ReadyCheck;
    if (ReadyCheck.bActive)
    {
        FString InitiatorName;
        const uint64 LocalPlayerGuid = ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid;
        if (ReadyCheck.InitiatorGuid == LocalPlayerGuid)
        {
            InitiatorName = ConnectionManager->GetCharacterName();
        }
        else if (const FString* CachedName = ConnectionManager->PacketHandler.PlayerNameCache.Find(ReadyCheck.InitiatorGuid))
        {
            InitiatorName = *CachedName;
        }

        if (InitiatorName.IsEmpty())
        {
            InitiatorName = TEXT("raid");
        }

        return FText::FromString(FString::Printf(
            TEXT("Ready check by %s (%ds)"),
            *InitiatorName,
            FMath::CeilToInt(ReadyCheck.GetTimeLeftSeconds())));
    }

    if (CurrentGroupInfo.IsRaidGroup())
    {
        return FText::GetEmpty(); // Don't show any status for raids
    }

    return FText::GetEmpty();
}

TOptional<float> SWowPartyFrame::GetHealthPercent(int32 MemberIndex) const
{
    if (!MemberWidgets.IsValidIndex(MemberIndex))
    {
        return 0.0f;
    }

    const FPartyMemberWidget& Widget = MemberWidgets[MemberIndex];
    if (Widget.MaxHealth <= 0)
    {
        return 0.0f;
    }

    return static_cast<float>(Widget.CurrentHealth) / static_cast<float>(Widget.MaxHealth);
}

TOptional<float> SWowPartyFrame::GetPowerPercent(int32 MemberIndex) const
{
    if (!MemberWidgets.IsValidIndex(MemberIndex))
    {
        return 0.0f;
    }

    const FPartyMemberWidget& Widget = MemberWidgets[MemberIndex];
    if (Widget.MaxPower <= 0)
    {
        return 0.0f;
    }

    return static_cast<float>(Widget.CurrentPower) / static_cast<float>(Widget.MaxPower);
}

FText SWowPartyFrame::GetMemberNameText(int32 MemberIndex) const
{
    if (!MemberWidgets.IsValidIndex(MemberIndex))
    {
        return FText::GetEmpty();
    }

    const FPartyMemberWidget& Widget = MemberWidgets[MemberIndex];
    return FText::FromString(Widget.bIsLocalPlayer
        ? FString::Printf(TEXT("%s (You)"), *Widget.Name)
        : Widget.Name);
}

FText SWowPartyFrame::GetMemberLevelText(int32 MemberIndex) const
{
    if (!MemberWidgets.IsValidIndex(MemberIndex))
    {
        return FText::GetEmpty();
    }

    const FPartyMemberWidget& Widget = MemberWidgets[MemberIndex];
    return Widget.Level > 0
        ? FText::FromString(FString::Printf(TEXT("%d"), Widget.Level))
        : FText::GetEmpty();
}

FText SWowPartyFrame::GetMemberMetaText(int32 MemberIndex) const
{
    if (!MemberWidgets.IsValidIndex(MemberIndex))
    {
        return FText::GetEmpty();
    }

    return FText::FromString(GetMemberMetaLabel(MemberWidgets[MemberIndex]));
}
