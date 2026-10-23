#include "SWowChatWindow.h"
#include "Formats/Dbc/DbcStore.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "WowNetwork/Public/WowConnectionManager.h"
#include "Styling/CoreStyle.h"

void SWowChatWindow::Construct(const FArguments& InArgs)
{
    ConnectionManager = InArgs._ConnectionManager;

    ChildSlot
    [
        SNew(SBox)
        .WidthOverride(450.0f)
        .HeightOverride(250.0f)
        [
            SNew(SBorder)
            .BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f)) // Semi-transparent dark background
            .BorderImage(FCoreStyle::Get().GetBrush("Border"))
            .Padding(FMargin(5.0f))
            [
                SNew(SVerticalBox)

                // Tab buttons
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 0.0f, 0.0f, 5.0f))
                [
                    SAssignNew(TabContainer, SHorizontalBox)

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(FMargin(0.0f, 0.0f, 5.0f, 0.0f))
                    [
                        SAssignNew(GeneralTabButton, SButton)
                        .Text(FText::FromString(TEXT("General")))
                        .OnClicked(this, &SWowChatWindow::OnTabClicked, EChatTab::General)
                    ]

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(FMargin(0.0f, 0.0f, 5.0f, 0.0f))
                    [
                        SAssignNew(CombatTabButton, SButton)
                        .Text(FText::FromString(TEXT("Combat Log")))
                        .OnClicked(this, &SWowChatWindow::OnTabClicked, EChatTab::Combat)
                    ]

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SAssignNew(TradeTabButton, SButton)
                        .Text(FText::FromString(TEXT("Trade")))
                        .OnClicked(this, &SWowChatWindow::OnTabClicked, EChatTab::Trade)
                    ]
                ]

                // Message area
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                [
                    SAssignNew(ScrollBox, SScrollBox)
                    .Orientation(EOrientation::Orient_Vertical)
                    .ScrollBarAlwaysVisible(false)
                    .AllowOverscroll(EAllowOverscroll::No)
                    + SScrollBox::Slot()
                    [
                        SAssignNew(MessageContainer, SVerticalBox)
                    ]
                ]

                // Input box
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 5.0f, 0.0f, 0.0f))
                [
                    SAssignNew(InputTextBox, SEditableTextBox)
                    .HintText(FText::FromString(TEXT("Press Enter to chat...")))
                    .OnTextCommitted(this, &SWowChatWindow::OnInputTextCommitted)
                    .OnTextChanged(this, &SWowChatWindow::OnInputTextChanged)
                ]
            ]
        ]
    ];

    // Set initial tab styling
    UpdateTabStyles();
}

void SWowChatWindow::AddChatMessage(uint8 Type, uint32 Language, uint64 SenderGuid, const FString& SenderName,
                                   const FString& Message, const FString& Channel)
{
    FLinearColor Color = GetColorForChatType(Type);
    FString FormattedSenderName = SenderName.IsEmpty() ? TEXT("Unknown") : SenderName;

    FWowChatMessage ChatMsg(Message, Color, Type, FormattedSenderName, Channel);

    // Determine which tab to add to
    EChatTab TargetTab = EChatTab::General;

    if (Type == 17 && Channel == TEXT("Trade")) // Channel message for Trade
    {
        TargetTab = EChatTab::Trade;
    }
    else if (Type >= 1 && Type <= 17) // Chat messages go to General
    {
        TargetTab = EChatTab::General;
    }

    // Add to appropriate message array
    TArray<FWowChatMessage>* TargetMessages = nullptr;
    switch (TargetTab)
    {
        case EChatTab::General:
            TargetMessages = &GeneralMessages;
            break;
        case EChatTab::Combat:
            TargetMessages = &CombatMessages;
            break;
        case EChatTab::Trade:
            TargetMessages = &TradeMessages;
            break;
    }

    if (TargetMessages)
    {
        TargetMessages->Add(ChatMsg);

        // Limit message count
        if (TargetMessages->Num() > MaxMessages)
        {
            TargetMessages->RemoveAt(0);
        }

        // Refresh display if this is the active tab
        if (TargetTab == ActiveTab)
        {
            RebuildMessageDisplay();
        }
    }
}

void SWowChatWindow::AddCombatMessage(const FString& Message, const FLinearColor& Color)
{
    FWowChatMessage ChatMsg(Message, Color, 0, TEXT(""), TEXT(""));
    CombatMessages.Add(ChatMsg);

    // Limit message count
    if (CombatMessages.Num() > MaxMessages)
    {
        CombatMessages.RemoveAt(0);
    }

    // Refresh display if combat tab is active
    if (ActiveTab == EChatTab::Combat)
    {
        RebuildMessageDisplay();
    }
}

void SWowChatWindow::ClearMessages()
{
    GetCurrentMessages().Empty();
    RebuildMessageDisplay();
}

void SWowChatWindow::ToggleInputFocus()
{
    if (!InputTextBox.IsValid())
    {
        return;
    }

    if (HasInputFocus())
    {
        // Lose focus - switch to GameOnly input mode
        FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
        bInputHasFocus = false;
        OnInputModeChanged.ExecuteIfBound(false);
    }
    else
    {
        // Gain focus - switch to GameAndUI input mode
        FSlateApplication::Get().SetKeyboardFocus(InputTextBox);
        bInputHasFocus = true;
        OnInputModeChanged.ExecuteIfBound(true);
    }
}

bool SWowChatWindow::HasInputFocus() const
{
    if (!InputTextBox.IsValid())
    {
        return false;
    }
    return FSlateApplication::Get().GetKeyboardFocusedWidget() == InputTextBox;
}

FLinearColor SWowChatWindow::GetColorForChatType(uint8 Type)
{
    switch (Type)
    {
        case 0: // System
            return FLinearColor::Red;
        case 1: // SAY
            return FLinearColor::White;
        case 3: // GUILD
            return FLinearColor::Green;
        case 4: // PARTY
            return FLinearColor(0.667f, 0.667f, 1.0f, 1.0f); // Light blue
        case 5: // YELL
            return FLinearColor::Yellow;
        case 6: // WHISPER
            return FLinearColor(1.0f, 0.5f, 1.0f, 1.0f); // Pink
        case 17: // CHANNEL
            return FLinearColor(1.0f, 0.647f, 0.0f, 1.0f); // Orange
        default:
            return FLinearColor::White;
    }
}

FString SWowChatWindow::FormatChatMessage(const FWowChatMessage& ChatMsg) const
{
    FString FormattedMessage = ChatMsg.Timestamp + TEXT(" ");

    if (ChatMsg.Type == 17 && !ChatMsg.Channel.IsEmpty()) // Channel message
    {
        FormattedMessage += FString::Printf(TEXT("[%s] %s: %s"), *ChatMsg.Channel, *ChatMsg.SenderName, *ChatMsg.Message);
    }
    else if (ChatMsg.Type == 6) // Whisper
    {
        FormattedMessage += FString::Printf(TEXT("%s whispers: %s"), *ChatMsg.SenderName, *ChatMsg.Message);
    }
    else if (ChatMsg.Type >= 1 && ChatMsg.Type <= 5) // SAY, PARTY, GUILD, YELL
    {
        const TCHAR* ChannelName = TEXT("");
        switch (ChatMsg.Type)
        {
            case 1: ChannelName = TEXT("Say"); break;
            case 3: ChannelName = TEXT("Guild"); break;
            case 4: ChannelName = TEXT("Party"); break;
            case 5: ChannelName = TEXT("Yell"); break;
        }

        if (ChatMsg.SenderName.IsEmpty())
        {
            FormattedMessage += ChatMsg.Message; // System message or combat log
        }
        else
        {
            FormattedMessage += FString::Printf(TEXT("[%s] %s: %s"), ChannelName, *ChatMsg.SenderName, *ChatMsg.Message);
        }
    }
    else
    {
        // Default formatting for combat log or unknown types
        FormattedMessage += ChatMsg.Message;
    }

    return FormattedMessage;
}

void SWowChatWindow::SendChatMessage()
{
    if (!ConnectionManager || !InputTextBox.IsValid())
    {
        return;
    }

    FString InputText = InputTextBox->GetText().ToString().TrimStartAndEnd();
    if (InputText.IsEmpty())
    {
        return;
    }

    FChatCommand Command = ParseChatCommand(InputText);

    // Clear input
    InputTextBox->SetText(FText::GetEmpty());

    // Handle emotes separately
    if (Command.Type == -1)
    {
        if (Command.Message == TEXT("stand"))
        {
            ConnectionManager->SendStandStateChange(0);
            return;
        }
        // This is an emote command
        int32 EmoteTextId = GetEmoteTextIdForCommand(Command.Message);
        if (EmoteTextId > 0)
        {
            // Send emote to server
            ConnectionManager->SendTextEmote(EmoteTextId, ConnectionManager->GetTargetGuid());

            // Show local feedback
            FString PlayerName = ConnectionManager->GetCharacterName();
            if (PlayerName.IsEmpty())
            {
                PlayerName = TEXT("You");
            }

            FString EmoteText = FString::Printf(TEXT("%s %ss."), *PlayerName, *Command.Message);
            AddChatMessage(0, 0, 0, TEXT(""), EmoteText, TEXT(""));
        }
        else
        {
            // Unknown emote command
            FString ErrorMsg = FString::Printf(TEXT("Unknown emote: /%s"), *Command.Message);
            AddChatMessage(0, 0, 0, TEXT("System"), ErrorMsg, TEXT(""));
        }
    }
    else
    {
        // Regular chat message
        FString Target = Command.Target.IsEmpty() ? Command.Channel : Command.Target;
        ConnectionManager->SendChatMessage(Command.Message, Command.Type, Target, GetDefaultLanguage());

        // Echo to local chat (server will send it back, but for immediate feedback)
        FString PlayerName = ConnectionManager->GetCharacterName();
        if (PlayerName.IsEmpty())
        {
            PlayerName = TEXT("You");
        }

        AddChatMessage(Command.Type, GetDefaultLanguage(), 0, PlayerName, Command.Message, Command.Channel);
    }
}

SWowChatWindow::FChatCommand SWowChatWindow::ParseChatCommand(const FString& Input) const
{
    FChatCommand Command;
    Command.Message = Input;

    if (!Input.StartsWith(TEXT("/")))
    {
        // Default to SAY
        Command.Type = 1;
        return Command;
    }

    // Parse slash commands
    FString TrimmedInput = Input.Mid(1); // Remove leading slash
    FString CommandPart;
    FString MessagePart;

    int32 SpaceIndex;
    if (TrimmedInput.FindChar(' ', SpaceIndex))
    {
        CommandPart = TrimmedInput.Left(SpaceIndex).ToLower();
        MessagePart = TrimmedInput.Mid(SpaceIndex + 1).TrimStartAndEnd();
    }
    else
    {
        CommandPart = TrimmedInput.ToLower();
        MessagePart = TEXT("");
    }

    Command.Message = MessagePart;

    if (CommandPart == TEXT("s") || CommandPart == TEXT("say"))
    {
        Command.Type = 1; // SAY
    }
    else if (CommandPart == TEXT("p") || CommandPart == TEXT("party"))
    {
        Command.Type = 4; // PARTY
    }
    else if (CommandPart == TEXT("g") || CommandPart == TEXT("guild"))
    {
        Command.Type = 3; // GUILD
    }
    else if (CommandPart == TEXT("y") || CommandPart == TEXT("yell"))
    {
        Command.Type = 5; // YELL
    }
    else if (CommandPart == TEXT("w") || CommandPart == TEXT("whisper"))
    {
        // Parse whisper target
        int32 TargetSpaceIndex;
        if (MessagePart.FindChar(' ', TargetSpaceIndex))
        {
            Command.Target = MessagePart.Left(TargetSpaceIndex);
            Command.Message = MessagePart.Mid(TargetSpaceIndex + 1).TrimStartAndEnd();
        }
        Command.Type = 6; // WHISPER
    }
    else if (CommandPart == TEXT("1"))
    {
        Command.Type = 17; // CHANNEL
        Command.Channel = TEXT("General");
    }
    else if (CommandPart == TEXT("2"))
    {
        Command.Type = 17; // CHANNEL
        Command.Channel = TEXT("Trade");
    }
    // Handle emote commands
    else if (IsEmoteCommand(CommandPart))
    {
        // Don't send as chat, handle as emote instead
        Command.Type = -1; // Special flag for emotes
        Command.Message = CommandPart; // Store the emote command
    }
    else
    {
        // Unknown command, treat as SAY with original message
        Command.Type = 1;
        Command.Message = Input;
    }

    return Command;
}

bool SWowChatWindow::IsEmoteCommand(const FString& Command) const
{
    return Command == TEXT("stand") || FDbcStore::Get().EmotesText().GetByCommand(Command) != nullptr;
}

int32 SWowChatWindow::GetEmoteTextIdForCommand(const FString& Command) const
{
    const auto* Entry = FDbcStore::Get().EmotesText().GetByCommand(Command);
    return Entry ? int32(Entry->ID) : -1;
}

FReply SWowChatWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Enter)
    {
        if (HasInputFocus())
        {
            // Send message and lose focus
            SendChatMessage();
            ToggleInputFocus();
            return FReply::Handled();
        }
        else
        {
            // Gain focus
            ToggleInputFocus();
            return FReply::Handled();
        }
    }
    else if (InKeyEvent.GetKey() == EKeys::Escape && HasInputFocus())
    {
        // Cancel input
        InputTextBox->SetText(FText::GetEmpty());
        ToggleInputFocus();
        return FReply::Handled();
    }

    return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SWowChatWindow::OnTabClicked(EChatTab Tab)
{
    ActiveTab = Tab;
    UpdateTabStyles();
    RebuildMessageDisplay();
    return FReply::Handled();
}

void SWowChatWindow::OnInputTextCommitted(const FText& Text, ETextCommit::Type CommitType)
{
    if (CommitType == ETextCommit::OnEnter)
    {
        SendChatMessage();
        ToggleInputFocus();
    }
}

void SWowChatWindow::OnInputTextChanged(const FText& Text)
{
    // Update focus state
    bInputHasFocus = HasInputFocus();
}

TArray<FWowChatMessage>& SWowChatWindow::GetCurrentMessages()
{
    switch (ActiveTab)
    {
        case EChatTab::General:
            return GeneralMessages;
        case EChatTab::Combat:
            return CombatMessages;
        case EChatTab::Trade:
            return TradeMessages;
        default:
            return GeneralMessages;
    }
}

const TArray<FWowChatMessage>& SWowChatWindow::GetCurrentMessages() const
{
    return const_cast<SWowChatWindow*>(this)->GetCurrentMessages();
}

void SWowChatWindow::RebuildMessageDisplay()
{
    if (!MessageContainer.IsValid())
    {
        return;
    }

    MessageContainer->ClearChildren();

    const TArray<FWowChatMessage>& Messages = GetCurrentMessages();
    for (const FWowChatMessage& ChatMsg : Messages)
    {
        FString FormattedMessage = FormatChatMessage(ChatMsg);

        TSharedRef<STextBlock> MessageText = SNew(STextBlock)
            .Text(FText::FromString(FormattedMessage))
            .ColorAndOpacity(ChatMsg.Color)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .AutoWrapText(true);

        MessageContainer->AddSlot()
            .AutoHeight()
            .Padding(FMargin(2.0f, 1.0f))
            [
                MessageText
            ];
    }

    // Auto-scroll to bottom
    ScrollToBottom();
}

void SWowChatWindow::ScrollToBottom()
{
    if (ScrollBox.IsValid())
    {
        ScrollBox->ScrollToEnd();
    }
}

void SWowChatWindow::UpdateTabStyles()
{
    // Update button styles based on active tab
    // For simplicity, we'll just ensure the buttons are visible
    // In a full implementation, you'd want to style active/inactive states differently
}