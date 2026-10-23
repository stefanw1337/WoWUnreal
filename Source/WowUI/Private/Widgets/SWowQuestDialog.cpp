#include "Widgets/SWowQuestDialog.h"
#include "WowColor.h"
#include "WowWindowStyle.h"
#include "Widgets/SWowProgressiveQuestText.h"
#include "WowClientPreferences.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Styling/CoreStyle.h"
#include "Engine/Engine.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "WowNativeWidgetStyle.h"
#include "WowItemPresentation.h"
#include "WowConnectionManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowRetainedSlateBrush.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SNullWidget.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "WowFontManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowQuestDialog, Log, All);

void SWowQuestDialog::Construct(const FArguments& InArgs)
{
    RichTextStyles = MakeShared<FSlateStyleSet>(TEXT("WowQuestDialogText"));
    SetClipping(EWidgetClipping::ClipToBounds);
    OnQuestAccept = InArgs._OnQuestAccept;
    OnQuestSelect = InArgs._OnQuestSelect;
    OnQuestProgress = InArgs._OnQuestProgress;
    OnQuestContinue = InArgs._OnQuestContinue;
    OnQuestComplete = InArgs._OnQuestComplete;
    OnGossipSelect = InArgs._OnGossipSelect;
    OnQuestDecline = InArgs._OnQuestDecline;

    // QuestFrame / GossipFrame (384 x 512): portrait at 7,6, NPC name on the title bar, the text in
    // the parchment page (QuestDetailScrollFrame 20,74, 300 x 334), rewards under it and the red
    // buttons in the bottom corners (BOTTOMLEFT 20,54 / BOTTOMRIGHT -37,54), close at TOPRIGHT -30,-8.
    auto Art = [this](const TCHAR* Part)
    {
        // r243 (Stefan 12.10: on the reward page the name missed the title bar, the X missed its socket
        // and the bottom bar had three sockets): every QuestFrame panel uses UI-QuestGreeting
        // (QuestFramePanelTemplate); the UI-QuestDetails art is not the QuestFrame's.
        return SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this, Part]()
        {
            return ArtBrush(FString::Printf(TEXT("Interface\\QuestFrame\\UI-QuestGreeting-%s"), Part));
        });
    };
    // Knob only: no track/background (the frame art draws the column). Brushes are filled in once
    // the textures load (RefreshPageScrollStyle in Tick would also work; the style is read per paint).
    PageScrollStyle = FCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>("ScrollBar");
    {
        FSlateBrush None; None.DrawAs = ESlateBrushDrawType::NoDrawType;
        PageScrollStyle.SetVerticalBackgroundImage(None).SetVerticalTopSlotImage(None).SetVerticalBottomSlotImage(None)
            .SetHorizontalBackgroundImage(None).SetHorizontalTopSlotImage(None).SetHorizontalBottomSlotImage(None);
    }
    EnsurePageScrollKnob();
    PageScrollBar = SNew(SScrollBar)
        .Orientation(Orient_Vertical)
        .AlwaysShowScrollbar(true)
        .AlwaysShowScrollbarTrack(false)
        .Thickness(FVector2D(16.f, 16.f))
        .Padding(0)
        .Style(&PageScrollStyle);
    auto At = [this](float X, float Y) { return TAttribute<FVector2D>::CreateLambda([this, X, Y]() { return FVector2D(X, Y) * GetScale(); }); };
    auto Size = [this](float W, float H) { return TAttribute<FVector2D>::CreateLambda([this, W, H]() { return FVector2D(W, H) * GetScale(); }); };
    ChildSlot
    [
        SNew(SBox)
        .WidthOverride_Lambda([this]() { return 384.f * GetScale(); })
        .HeightOverride_Lambda([this]() { return 512.f * GetScale(); })
        [
            SNew(SCanvas)
            + SCanvas::Slot().Position(At(7, 6)).Size(Size(60, 60))
            [ SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this]() { return PortraitImage(); }) ]
            + SCanvas::Slot().Position(At(0, 0)).Size(Size(256, 256))[ Art(TEXT("TopLeft")) ]
            + SCanvas::Slot().Position(At(256, 0)).Size(Size(128, 256))[ Art(TEXT("TopRight")) ]
            + SCanvas::Slot().Position(At(0, 256)).Size(Size(256, 256))[ Art(TEXT("BotLeft")) ]
            + SCanvas::Slot().Position(At(256, 256)).Size(Size(128, 256))[ Art(TEXT("BotRight")) ]
            // r235: Gossip/QuestFrameGreetingPanel cover the empty left button socket of the bottom bar
            // with UI-Quest-BotLeftPatch (128 x 64, BOTTOMLEFT 22,68) - only Goodbye is shown there.
            + SCanvas::Slot().Position(At(22, 380)).Size(Size(128, 64))
            [
                SNew(SImage).Visibility_Lambda([this]() { return bIsQuestList ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                .Image_Lambda([this]() { return ArtBrush(TEXT("Interface\\QuestFrame\\UI-Quest-BotLeftPatch")); })
            ]
            // The frame swallows clicks inside its art (not the transparent margin right and below).
            + SCanvas::Slot().Position(At(10, 10)).Size(Size(330, 430))
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
                .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
            ]
            // QuestNpcNameFrame: TOP 0,-23, 300 x 14 with QuestFrameNpcNameText (GameFontHighlight, white,
            // 235 x 20) centred on it. Stefan 11.10: long NPC names ran past the title bar; a name that is
            // wider than the 235 units is drawn smaller until it fits.
            // r235: 2 units lower than the XML box so the letters sit in the middle of the title bar
            // (the line box includes the descender space; Stefan 12.10: "text is a little high").
            + SCanvas::Slot().Position(At(74.5f, 22)).Size(Size(235, 20))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() { return FText::FromString(TitleName()); })
                .Font_Lambda([this]() { return TitleFontFor(TitleName()); })
                .ColorAndOpacity(FLinearColor::White)
                .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
                .Justification(ETextJustify::Center)
            ]
            // QuestFrameCloseButton (UIPanelCloseButton 32 x 32): CENTER on the frame's TOPRIGHT -42,-31.
            // r235: back on the XML spot (top-left 326,15). Measured on Stefan's 12.10 screenshot the
            // X glyph sat 4 right and 6 down of the socket's centre at 330,21; the earlier shift was wrong.
            + SCanvas::Slot().Position(At(326, 15)).Size(Size(32, 32))
            [
                SNew(SButton).ContentPadding(0).ClickMethod(EButtonClickMethod::MouseDown)
                .ButtonStyle(FCoreStyle::Get(), "NoBorder")
                .OnClicked(this, &SWowQuestDialog::OnDeclineClicked)
                [ SNew(SImage).Image_Lambda([this]() { return ArtBrush(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up")); }) ]
            ]
            // GossipGreetingScrollFrame TOPLEFT 23,-81 (300 x 334), its text 10,-10 in (Stefan 10.10: the
            // greeting sat on the top edge); quest pages keep QuestDetailScrollFrame's 22,76.
            // Every page uses the 23,-81 / 300 x 334 scroll frame (Quest*ScrollFrame): the detail page ran
            // down into the button bar (Stefan 10.10).
            + SCanvas::Slot()
            .Position(At(23, 81))
            .Size(Size(300, 334))
            [
                SNew(SBox).Padding_Lambda([this]() { return FMargin(10, 10, 20, 0) * GetScale(); })
                [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().FillHeight(1.0f)
                [
                    SAssignNew(ContentArea, SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always)
                    .Orientation(Orient_Vertical)
                    .ExternalScrollbar(PageScrollBar)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
                [ SAssignNew(RewardArea, SHorizontalBox) ]
                ]
            ]
            // The page's scroll bar in the frame's scroll column (UIPanelScrollBarTemplate: TOPLEFT of the
            // scroll frame's TOPRIGHT 6,-16, up/down buttons above and below it), always shown like
            // GossipGreetingScrollFrame's; the arrows grey out when there is nothing to scroll
            // (Stefan 10.10: the column was empty).
            + SCanvas::Slot().Position(At(329, 81)).Size(Size(16, 334))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SBox).HeightOverride_Lambda([this]() { return 16.f * GetScale(); })
                    [
                        SNew(SButton).ContentPadding(0).ButtonStyle(FCoreStyle::Get(), "NoBorder")
                        .OnClicked_Lambda([this]() { ScrollPage(-1.f); return FReply::Handled(); })
                        [
                            SNew(SImage).Image_Lambda([this]()
                            {
                                const bool bCanScroll = ContentArea.IsValid() && ContentArea->GetScrollOffset() > 0.5f;
                                return ArtBrushUV(bCanScroll ? TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Up")
                                    : TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Disabled"), FBox2f(FVector2f(.2f, .25f), FVector2f(.8f, .75f)));
                            })
                        ]
                    ]
                ]
                + SVerticalBox::Slot().FillHeight(1.f)
                [
                    PageScrollBar.ToSharedRef()
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SBox).HeightOverride_Lambda([this]() { return 16.f * GetScale(); })
                    [
                        SNew(SButton).ContentPadding(0).ButtonStyle(FCoreStyle::Get(), "NoBorder")
                        .OnClicked_Lambda([this]() { ScrollPage(1.f); return FReply::Handled(); })
                        [
                            SNew(SImage).Image_Lambda([this]()
                            {
                                const bool bCanScroll = ContentArea.IsValid() && ContentArea->GetScrollOffsetOfEnd() - ContentArea->GetScrollOffset() > 0.5f;
                                return ArtBrushUV(bCanScroll ? TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Up")
                                    : TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Disabled"), FBox2f(FVector2f(.2f, .25f), FVector2f(.8f, .75f)));
                            })
                        ]
                    ]
                ]
            ]
            // The buttons sit in the frame's bottom bar: BOTTOMLEFT 23,72 and BOTTOMRIGHT -39,72 of the
            // 384 x 512 frame, i.e. y 418, x 23 .. 345 (they hung below it, Stefan 10.10).
            + SCanvas::Slot().Position(At(23, 418)).Size(Size(322, 22))
            [ SAssignNew(ButtonArea, SHorizontalBox) ]
        ]
    ];

    SetVisibility(EVisibility::Collapsed);
}

void SWowQuestDialog::ShowQuestDetails(const FWowQuestDetails& QuestDetails)
{
    EnsurePageScrollKnob();
    CurrentQuest = QuestDetails;
    bIsQuestList = false;
    bIsProgressDialog = false;
    bIsRewardDialog = false;
    SelectedRewardChoice = INDEX_NONE;

    bActionPending = false;
    PendingGossipOption.Reset();
    RefreshContent();
    ContentArea->ScrollToStart();
    SetVisibility(EVisibility::Visible);

    UE_LOG(LogWowQuestDialog, Log, TEXT("Showing quest details: %s"), *QuestDetails.Title);
}

void SWowQuestDialog::ShowQuestList(const FWowGossipMenu& Menu)
{
    EnsurePageScrollKnob();
    CurrentMenu = Menu;
    CurrentQuest = FWowQuestDetails();
    bIsQuestList = true;
    bIsProgressDialog = false;
    bIsRewardDialog = false;
    bActionPending = false;
    PendingGossipOption.Reset();
    RefreshContent();
    ContentArea->ScrollToStart();
    SetVisibility(EVisibility::Visible);
    UE_LOG(LogWowQuestDialog, Log, TEXT("Showing NPC quest list: guid=%llu quests=%d"),
        Menu.NpcGuid, Menu.Quests.Num());
}

void SWowQuestDialog::ShowQuestReward(const FWowQuestDetails& QuestDetails)
{
    EnsurePageScrollKnob();
    CurrentQuest = QuestDetails;
    bIsQuestList = false;
    bIsProgressDialog = false;
    bIsRewardDialog = true;
    SelectedRewardChoice = INDEX_NONE;

    bActionPending = false;
    PendingGossipOption.Reset();
    RefreshContent();
    ContentArea->ScrollToStart();
    SetVisibility(EVisibility::Visible);

    UE_LOG(LogWowQuestDialog, Log, TEXT("Showing quest reward: %s"), *QuestDetails.Title);
}

void SWowQuestDialog::ShowQuestProgress(const FWowQuestDetails& QuestDetails)
{
    EnsurePageScrollKnob();
    CurrentQuest = QuestDetails;
    bIsQuestList = false;
    bIsProgressDialog = true;
    bIsRewardDialog = false;
    SelectedRewardChoice = INDEX_NONE;
    bActionPending = false;
    PendingGossipOption.Reset();
    RefreshContent();
    ContentArea->ScrollToStart();
    SetVisibility(EVisibility::Visible);

    UE_LOG(LogWowQuestDialog, Log, TEXT("Showing quest progress: %s (canComplete=%d)"),
        *QuestDetails.Title, QuestDetails.bCanComplete);
}

void SWowQuestDialog::CloseDialog()
{
    CurrentQuest = FWowQuestDetails();
    CurrentMenu = FWowGossipMenu();
    bIsQuestList = false;
    bIsProgressDialog = false;
    bIsRewardDialog = false;
    PendingGossipOption.Reset();
    bActionPending = false;
    SetVisibility(EVisibility::Collapsed);

    UE_LOG(LogWowQuestDialog, Log, TEXT("Closed quest dialog"));
}

void SWowQuestDialog::RefreshContent()
{
    NextTextRevealDelay = 0;
    // Clear existing content
    ContentArea->ClearChildren();
    RewardArea->ClearChildren();
    ButtonArea->ClearChildren();

    if (bIsQuestList)
    {
        ContentArea->AddSlot()
        [
            MakeRichText(CurrentMenu.Greeting, FLinearColor::Black, true)
        ];

        // GossipTitleButtonN: 20 under the greeting, 10 units left of it, 3 apart; the option's type
        // icon (GossipFrame\<Type>GossipIcon, 16x16 at 3,1) and the text 20 in.
        bool bFirstOption = true;
        for (const FWowGossipOption& Option : CurrentMenu.Options)
        {
            const uint32 OptionId = Option.OptionId;
            static const TCHAR* const IconTypes[] = { TEXT("Gossip"), TEXT("Vendor"), TEXT("Taxi"), TEXT("Trainer"), TEXT("Healer"),
                TEXT("Binder"), TEXT("Banker"), TEXT("Petition"), TEXT("Tabard"), TEXT("BattleMaster"), TEXT("Auctioneer") };
            const FString IconPath = FString::Printf(TEXT("Interface\\GossipFrame\\%sGossipIcon"),
                Option.Icon < UE_ARRAY_COUNT(IconTypes) ? IconTypes[Option.Icon] : TEXT("Gossip"));
            const float S = GetScale();
            ContentArea->AddSlot()
            .Padding(-10.0f * S, (bFirstOption ? 20.0f : 3.0f) * S, 0.0f, 0.0f)
            [
                SNew(SButton)
                .ButtonStyle(FWowNativeWidgetStyle::TextChoice())
                .HAlign(HAlign_Left)
                .ContentPadding(0)
                .OnClicked_Lambda([this, OptionId]() { return OnGossipOptionClicked(OptionId); })
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(3.0f * S, 1.0f * S, 1.0f * S, 0.0f)
                    [
                        SNew(SBox).WidthOverride(16.0f * S).HeightOverride(16.0f * S)
                        [ SNew(SImage).Image_Lambda([this, IconPath]()
                            { const FSlateBrush* B = ArtBrush(IconPath); return B ? B : ArtBrush(TEXT("Interface\\QuestFrame\\UI-Quest-BulletPoint")); }) ]
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                    [ MakeRichText(Option.Text, FLinearColor::Black) ]
                ]
            ];
            bFirstOption = false;
        }

        for (const FWowQuestMenuItem& Quest : CurrentMenu.Quests)
        {
            const uint32 QuestId = Quest.QuestId;
            const uint32 QuestIcon = Quest.Icon;
            // r381 (Stefan 16.10): the client's gossip quest icons instead of "!" / "?" characters -
            // GossipFrame\AvailableQuestIcon (yellow !) for a quest you can take, ActiveQuestIcon
            // (yellow ?) for one you can hand in, and that ? greyed while the quest is unfinished.
            bool bQuestDone = true;
            if (QuestIcon == 4 && Connection.IsValid())
                for (const FWowQuestLogEntry& Entry : Connection->PacketHandler.QuestLog)
                    if (Entry.QuestId == QuestId) { bQuestDone = Entry.State == 1; break; }
            const FString QuestIconPath = QuestIcon == 4 ? TEXT("Interface\\GossipFrame\\ActiveQuestIcon") : TEXT("Interface\\GossipFrame\\AvailableQuestIcon");
            // Difficulty colour of the 3.3.5 quest log (red/orange/yellow/green/grey), in darker
            // shades that read on the parchment.
            const TCHAR* Shade = TEXT("");
            if (Quest.Level > 0)
            {
                int32 PlayerLevel = 1;
                if (Connection.IsValid())
                    if (const FWowPlayerEntity* Player = Connection->PacketHandler.EntityManager.GetLocalPlayer())
                        PlayerLevel = FMath::Max(1, Player->GetLevel());
                const int32 Diff = Quest.Level - PlayerLevel;
                const int32 GreenRange = PlayerLevel <= 5 ? 5 : PlayerLevel <= 39 ? 5 + PlayerLevel / 10 : PlayerLevel <= 59 ? 9 : 10;
                Shade = Diff >= 5 ? TEXT("|cffb80d0d") : Diff >= 3 ? TEXT("|cffc2520a") : Diff >= -2 ? TEXT("|cff7a5a00")
                    : -Diff <= GreenRange ? TEXT("|cff0d6e0d") : TEXT("|cff5a5a5a");
            }
            const FString Label = Quest.Level > 0
                ? FString::Printf(TEXT("%s[%d] %s|r"), Shade, Quest.Level, *Quest.Title)
                : Quest.Title;
            // Same rows as the gossip options above (GossipTitleButtonN, 3 apart, icon 16x16 at 3,1).
            const float S = GetScale();
            ContentArea->AddSlot()
            .Padding(-10.0f * S, (bFirstOption ? 20.0f : 3.0f) * S, 0.0f, 0.0f)
            [
                SNew(SButton)
                .ButtonStyle(FWowNativeWidgetStyle::TextChoice())
                .HAlign(HAlign_Left)
                .ContentPadding(0)
                .OnClicked_Lambda([this, QuestId, QuestIcon]() { return OnQuestListItemClicked(QuestId, QuestIcon); })
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(3.0f * S, 1.0f * S, 1.0f * S, 0.0f)
                    [
                        SNew(SBox).WidthOverride(16.0f * S).HeightOverride(16.0f * S)
                        [ SNew(SImage).Visibility(EVisibility::HitTestInvisible)
                            .IsEnabled(bQuestDone) // Slate's disabled effect greys it, like SetDesaturated(1)
                            .Image_Lambda([this, QuestIconPath]() { return ArtBrush(QuestIconPath); }) ]
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                    [ MakeRichText(Label, FLinearColor::Black) ]
                ]
            ];
            bFirstOption = false;
        }

        ButtonArea->AddSlot()
        .AutoWidth()
        [
            SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(0).TextStyle(PanelButtonTextStyle())
            .Text(FText::FromString(TEXT("Goodbye")))
            .OnClicked(this, &SWowQuestDialog::OnDeclineClicked)
        ];
        ArrangeButtons(true);
        return;
    }


    if (bIsProgressDialog && CurrentQuest.RequiredItems.Num() > 0)
    {
        ContentArea->AddSlot()
        .Padding(0.0f, 8.0f, 0.0f, 2.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Required items:")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
            .ColorAndOpacity(WowColor::FromWow(.012f, .006f, .002f))
        ];

        for (const FWowQuestRewardItem& Item : CurrentQuest.RequiredItems)
        {
            ContentArea->AddSlot()
            [
                FWowItemPresentation::ItemCard(Connection.Get(),UIManager.Get(),Item.ItemId,Item.Count,Item.DisplayId)
            ];
        }
    }

    // Quest title
    ContentArea->AddSlot()
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
        .Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
        [
            SNew(STextBlock)
            .Text(FText::FromString(CurrentQuest.Title))
            .Font(FWowNativeWidgetStyle::Heading(23))
            .ColorAndOpacity(WowColor::FromWow(.012f, .006f, .002f))
            .AutoWrapText(true)
        ]
    ];

    // Quest details text
    if (!CurrentQuest.Details.IsEmpty())
    {
        ContentArea->AddSlot()
        [
            MakeRichText(CurrentQuest.Details, FLinearColor::Black, true)
        ];
    }

    // QuestInfo layout: section headers in the quest title font with a gap above them, so the page
    // is not one block of text (Stefan 10.10).
    const FLinearColor Ink = WowColor::FromWow(.012f, .006f, .002f);
    auto SectionHeader = [this, Ink](const TCHAR* Text)
    {
        ContentArea->AddSlot().Padding(0.0f, 14.0f, 0.0f, 4.0f)
        [
            SNew(STextBlock).Text(FText::FromString(Text))
            .Font(FWowNativeWidgetStyle::Heading(18)).ColorAndOpacity(Ink)
        ];
    };

    // Quest objectives
    if (!CurrentQuest.Objectives.IsEmpty())
    {
        SectionHeader(TEXT("Quest Objectives"));
        ContentArea->AddSlot()
        [
            MakeRichText(CurrentQuest.Objectives, FLinearColor::Black, true)
        ];
    }

    // Rewards section
    if (CurrentQuest.RewardMoney > 0 || CurrentQuest.RewardXP > 0 || CurrentQuest.RewardItems.Num() > 0 || CurrentQuest.ChoiceRewards.Num() > 0)
    {
        SectionHeader(TEXT("Rewards"));

        // "Experience:" in the page's ink; the amount as on screen when you gain it (SWowXPGain: the
        // scroll font in the XP bar's pink with a black outline). Stefan 11.10: only the number and
        // "XP" are pink, not the whole line.
        if (CurrentQuest.RewardXP > 0)
        {
            FSlateFontInfo XpFont = FWowFontManager::GetScrollFont(FMath::RoundToInt(14.f * GetScale()));
            XpFont.OutlineSettings.OutlineSize = 1;
            XpFont.OutlineSettings.OutlineColor = FLinearColor::Black;
            ContentArea->AddSlot().Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Experience:")))
                    .Font(FWowNativeWidgetStyle::Heading(14)).ColorAndOpacity(Ink)
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%u XP"), CurrentQuest.RewardXP)))
                    .Font(XpFont).ColorAndOpacity(WowColor::FromWow(.58f, 0.0f, .55f))
                ]
            ];
        }

        // Money as a MoneyFrame: amounts with the gold / silver / copper coins.
        if (CurrentQuest.RewardMoney > 0)
        {
            const uint32 Money = CurrentQuest.RewardMoney;
            TSharedRef<SHorizontalBox> Coins = SNew(SHorizontalBox);
            auto AddCoin = [this, &Coins](uint32 Value, const TCHAR* Icon)
            {
                const FString IconPath = Icon;
                Coins->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 2.0f, 0.0f)
                [
                    SNew(STextBlock).Text(FText::AsNumber(Value))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11)).ColorAndOpacity(FLinearColor::White)
                    .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
                ];
                Coins->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(SBox).WidthOverride(13.0f).HeightOverride(13.0f)
                    [ SNew(SImage).Image_Lambda([this, IconPath]() { return ArtBrush(IconPath); }) ]
                ];
            };
            if (Money >= 10000) AddCoin(Money / 10000, TEXT("Interface\\MoneyFrame\\UI-GoldIcon"));
            if (Money >= 100) AddCoin(Money / 100 % 100, TEXT("Interface\\MoneyFrame\\UI-SilverIcon"));
            if (Money % 100 || Money < 100) AddCoin(Money % 100, TEXT("Interface\\MoneyFrame\\UI-CopperIcon"));
            ContentArea->AddSlot().Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("You will receive:")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).ColorAndOpacity(Ink)
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Coins ]
            ];
        }
    }

    // r399 (Stefan 16.10): each reward shows what it sells for at the right of its row, with the gold /
    // silver / copper coins like a MoneyFrame (the item's SellPrice; hidden when it cannot be sold, and
    // filled in once the item's data arrives).
    auto RewardSellPrice = [this](uint32 ItemId) -> TSharedRef<SWidget>
    {
        const TWeakObjectPtr<UWowConnectionManager> WeakConnection = Connection;
        auto SellPriceOf = [WeakConnection, ItemId]() -> uint32
        {
            const UWowConnectionManager* Live = WeakConnection.Get();
            const auto* Template = Live ? Live->PacketHandler.ItemTemplates.Find(ItemId) : nullptr;
            return Template ? Template->SellPrice : 0u;
        };
        TSharedRef<SHorizontalBox> Coins = SNew(SHorizontalBox)
            .Visibility_Lambda([SellPriceOf]() { return SellPriceOf() > 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; });
        auto AddRewardCoin = [this, &Coins, SellPriceOf](int32 Kind, const TCHAR* Icon)
        {
            const FString IconPath = Icon;
            auto Amount = [SellPriceOf, Kind]() -> uint32
            {
                const uint32 Price = SellPriceOf();
                return Kind == 0 ? Price / 10000 : Kind == 1 ? Price / 100 % 100 : Price % 100;
            };
            auto Shown = [SellPriceOf, Kind]() -> EVisibility
            {
                const uint32 Price = SellPriceOf();
                const bool bShow = Kind == 0 ? Price >= 10000 : Kind == 1 ? Price >= 100 : (Price % 100 != 0 || Price < 100);
                return bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
            };
            Coins->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 2.0f, 0.0f)
            [
                SNew(STextBlock).Visibility_Lambda(Shown).Text_Lambda([Amount]() { return FText::AsNumber(Amount()); })
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10)).ColorAndOpacity(FLinearColor::White)
                .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
            ];
            Coins->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 5.0f, 0.0f)
            [
                SNew(SBox).Visibility_Lambda(Shown).WidthOverride(12.0f).HeightOverride(12.0f)
                [ SNew(SImage).Image_Lambda([this, IconPath]() { return ArtBrush(IconPath); }) ]
            ];
        };
        AddRewardCoin(0, TEXT("Interface\\MoneyFrame\\UI-GoldIcon"));
        AddRewardCoin(1, TEXT("Interface\\MoneyFrame\\UI-SilverIcon"));
        AddRewardCoin(2, TEXT("Interface\\MoneyFrame\\UI-CopperIcon"));
        return Coins;
    };
    auto RewardRow = [this, &RewardSellPrice](const FWowQuestRewardItem& Reward) -> TSharedRef<SWidget>
    {
        return SNew(SOverlay)
            + SOverlay::Slot()[ FWowItemPresentation::ItemCard(Connection.Get(),UIManager.Get(),Reward.ItemId,Reward.Count,Reward.DisplayId) ]
            + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[ RewardSellPrice(Reward.ItemId) ];
    };
    for (const FWowQuestRewardItem& Item : CurrentQuest.RewardItems)
    {
        ContentArea->AddSlot().Padding(0.0f, 3.0f)
        [ RewardRow(Item) ];
    }
    if (bIsProgressDialog && CurrentQuest.RequiredMoney > 0)
    {
        const uint32 Money = CurrentQuest.RequiredMoney;
        ContentArea->AddSlot().Padding(0.0f, 6.0f)
        [ MakeRichText(FString::Printf(TEXT("Required money: %ug %us %uc"),
            Money / 10000, (Money / 100) % 100, Money % 100),
            FLinearColor::Black) ];
    }

    // Choice rewards (for reward dialog)
    if (CurrentQuest.ChoiceRewards.Num() > 0)
    {
        ContentArea->AddSlot()
        [
            SNew(STextBlock)
            .Text(FText::FromString(bIsRewardDialog ? TEXT("Choose your reward:") : TEXT("You will be able to choose one reward:")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
            .ColorAndOpacity(WowColor::FromWow(.012f, .006f, .002f))
        ];

        for (int32 i = 0; i < CurrentQuest.ChoiceRewards.Num(); ++i)
        {
            const FWowQuestRewardItem& RewardItem = CurrentQuest.ChoiceRewards[i];

            ContentArea->AddSlot().Padding(0,3)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).Padding(2)
                .BorderBackgroundColor_Lambda([this,i](){return SelectedRewardChoice==i?WowColor::FromWow(1, .82f, 0):WowColor::FromWow(.22f, .17f, .10f);})
                [SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0)
                    .OnClicked_Lambda([this,i](){return OnRewardItemClicked(i);})
                    [RewardRow(RewardItem)]]];
        }
    }

    // Buttons
    if (bIsRewardDialog)
    {
        // Complete Quest button
        ButtonArea->AddSlot()
        .AutoWidth()
        [
            SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(0).TextStyle(PanelButtonTextStyle())
            .Text(FText::FromString(TEXT("Complete Quest")))
            .IsEnabled_Lambda([this]() { return !bActionPending &&
                (CurrentQuest.ChoiceRewards.IsEmpty() || CurrentQuest.ChoiceRewards.IsValidIndex(SelectedRewardChoice)); })
            .OnClicked(this, &SWowQuestDialog::OnCompleteClicked)
        ];
        // r243: QuestFrameCancelButton (78 x 22, BOTTOMRIGHT -39,73) on the reward page too.
        ButtonArea->AddSlot()
        .AutoWidth()
        [
            SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(0).TextStyle(PanelButtonTextStyle())
            .Text(FText::FromString(TEXT("Cancel")))
            .OnClicked(this, &SWowQuestDialog::OnDeclineClicked)
        ];
    }
    else if (bIsProgressDialog)
    {
        // r266 (Stefan 13.10: an empty socket at the bottom left while items were still missing):
        // QuestFrameProgressPanel always shows QuestFrameCompleteButton ("Continue") and disables it
        // until the quest can be completed (QuestFrameProgressPanel_OnShow, IsQuestCompletable).
        ButtonArea->AddSlot()
        .AutoWidth()
        [
            SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(0).TextStyle(PanelButtonTextStyle())
            .Text(FText::FromString(TEXT("Continue")))
            .IsEnabled_Lambda([this]() { return CurrentQuest.bCanComplete && !bActionPending; })
            .OnClicked(this, &SWowQuestDialog::OnContinueClicked)
        ];

        ButtonArea->AddSlot()
        .AutoWidth()
        .Padding(8.0f, 0.0f, 0.0f, 0.0f)
        [
            SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(0).TextStyle(PanelButtonTextStyle())
            .Text(FText::FromString(TEXT("Cancel")))
            .OnClicked(this, &SWowQuestDialog::OnDeclineClicked)
        ];
    }
    else
    {
        // Accept/Decline buttons
        ButtonArea->AddSlot()
        .AutoWidth()
        [
            SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(0).TextStyle(PanelButtonTextStyle())
            .Text(FText::FromString(TEXT("Accept")))
            .OnClicked(this, &SWowQuestDialog::OnAcceptClicked)
        ];

        ButtonArea->AddSlot()
        .AutoWidth()
        .Padding(8.0f, 0.0f, 0.0f, 0.0f)
        [
            SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(0).TextStyle(PanelButtonTextStyle())
            .Text(FText::FromString(TEXT("Decline")))
            .OnClicked(this, &SWowQuestDialog::OnDeclineClicked)
        ];
    }
    // Complete Quest alone stays on the left; Goodbye alone (progress without Continue) goes right.
    ArrangeButtons(bIsProgressDialog);
}

void SWowQuestDialog::ArrangeButtons(bool bSingleOnRight)
{
    if (!ButtonArea.IsValid()) return;
    // QuestFrame.xml sizes: Accept 77, Complete Quest / Continue 120 on the left; Decline / Cancel /
    // Goodbye 78 on the right.
    TArray<TSharedRef<SWidget>> Buttons;
    FChildren* Children = ButtonArea->GetChildren();
    for (int32 Index = 0; Index < Children->Num(); ++Index) Buttons.Add(Children->GetChildAt(Index));
    ButtonArea->ClearChildren();
    if (Buttons.IsEmpty()) return;
    const bool bDetailPage = !bIsQuestList && !bIsProgressDialog && !bIsRewardDialog;
    auto Sized = [this](const TSharedRef<SWidget>& Button, float Width) -> TSharedRef<SWidget>
    {
        // UIPanelButtonTemplate is 22 high: the buttons were as tall as their padded text and hung
        // over the frame's bottom bar (Stefan 11.10).
        return SNew(SBox).WidthOverride_Lambda([this, Width]() { return Width * GetScale(); })
            .HeightOverride_Lambda([this]() { return 22.f * GetScale(); })[ Button ];
    };
    const bool bHasLeft = Buttons.Num() >= 2 || !bSingleOnRight;
    const bool bHasRight = Buttons.Num() >= 2 || bSingleOnRight;
    if (bHasLeft) ButtonArea->AddSlot().AutoWidth()[ Sized(Buttons[0], bDetailPage ? 77.f : 120.f) ];
    ButtonArea->AddSlot().FillWidth(1.0f)[ SNullWidget::NullWidget ];
    if (bHasRight) ButtonArea->AddSlot().AutoWidth()[ Sized(Buttons.Last(), 78.f) ];
}

float SWowQuestDialog::GetScale() const
{
    const float Scale = UIManager.IsValid() && UIManager->GetFrameManager() ? UIManager->GetFrameManager()->GetUIScale() : 1.f;
    return Scale > 0.f ? Scale : 1.f;
}

const FSlateBrush* SWowQuestDialog::ArtBrush(const FString& Path) const
{
    if (const TSharedPtr<FWowRetainedSlateBrush>* Found = ArtBrushes.Find(Path)) return Found->Get();
    UTexture2D* Texture = UIManager.IsValid() && UIManager->GetFrameManager() ? UIManager->GetFrameManager()->LoadUITexture(Path) : nullptr;
    if (!Texture) return nullptr;
    TSharedPtr<FWowRetainedSlateBrush> Brush = MakeShared<FWowRetainedSlateBrush>();
    Brush->SetResourceObject(Texture);
    Brush->DrawAs = ESlateBrushDrawType::Image;
    Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
    ArtBrushes.Add(Path, Brush);
    return Brush.Get();
}

const FSlateBrush* SWowQuestDialog::PortraitImage() const
{
    // SetPortraitTexture(QuestFramePortrait, "npc"): the live 3D portrait of the quest giver.
    // QuestFrame_SetPortrait: a giver that is no unit (a book, a wanted poster, a quest item) shows
    // UI-QuestLog-BookIcon instead (Stefan 11.10: the ring was empty on a library book).
    const uint64 Guid = bIsQuestList ? CurrentMenu.NpcGuid : CurrentQuest.QuestGiverGuid;
    const uint32 HighGuid = uint32(Guid >> 48);
    const bool bUnitGiver = Guid && (HighGuid == 0 || HighGuid == 0xF130 || HighGuid == 0xF140 || HighGuid == 0xF150);
    const TCHAR* BookIcon = TEXT("Interface\\QuestFrame\\UI-QuestLog-BookIcon");
    if (!bUnitGiver) return ArtBrush(BookIcon);
    if (!UIManager.IsValid() || !UIManager->GetFrameManager() || !UIManager->GetFrameManager()->UnitPortraitProvider) return ArtBrush(BookIcon);
    UMaterialInterface* Portrait = UIManager->GetFrameManager()->UnitPortraitProvider(Guid);
    if (!Portrait) return ArtBrush(BookIcon);
    if (!PortraitBrush.IsValid()) PortraitBrush = MakeShared<FWowRetainedSlateBrush>();
    if (PortraitBrush->GetResourceObject() != Portrait)
    {
        PortraitBrush->SetResourceObject(Portrait);
        PortraitBrush->DrawAs = ESlateBrushDrawType::Image;
        PortraitBrush->ImageSize = FVector2D(64, 64);
    }
    return PortraitBrush.Get();
}

FReply SWowQuestDialog::OnAcceptClicked()
{
    if (OnQuestAccept.IsBound() &&
        OnQuestAccept.Execute(CurrentQuest.QuestGiverGuid, CurrentQuest.QuestId))
    {
        CloseDialog();
    }
    return FReply::Handled();
}

FReply SWowQuestDialog::OnDeclineClicked()
{
    if (OnQuestDecline.IsBound())
    {
        OnQuestDecline.Execute();
    }
    CloseDialog();
    return FReply::Handled();
}

FReply SWowQuestDialog::OnCompleteClicked()
{
    if (bActionPending || (!CurrentQuest.ChoiceRewards.IsEmpty() &&
        !CurrentQuest.ChoiceRewards.IsValidIndex(SelectedRewardChoice))) return FReply::Handled();
    if (OnQuestComplete.IsBound())
    {
        bActionPending = true;
        OnQuestComplete.Execute(CurrentQuest.QuestGiverGuid, CurrentQuest.QuestId,
            CurrentQuest.ChoiceRewards.IsEmpty() ? 0 : SelectedRewardChoice);
    }
    CloseDialog();
    return FReply::Handled();
}

FReply SWowQuestDialog::OnContinueClicked()
{
    if (!CurrentQuest.bCanComplete || bActionPending) return FReply::Handled();
    if (OnQuestContinue.IsBound())
    {
        bActionPending = true;
        OnQuestContinue.Execute(CurrentQuest.QuestGiverGuid, CurrentQuest.QuestId);
    }
    return FReply::Handled();
}

FReply SWowQuestDialog::OnRewardItemClicked(int32 RewardIndex)
{
    if (bIsRewardDialog && CurrentQuest.ChoiceRewards.IsValidIndex(RewardIndex))
        SelectedRewardChoice = RewardIndex;
    return FReply::Handled();
}

FReply SWowQuestDialog::OnQuestListItemClicked(uint32 QuestId, uint32 QuestIcon)
{
    if (bActionPending) return FReply::Handled();
    bActionPending = true;
    if (QuestIcon == 4 && OnQuestProgress.IsBound())
    {
        OnQuestProgress.Execute(CurrentMenu.NpcGuid, QuestId);
    }
    else if (OnQuestSelect.IsBound())
    {
        OnQuestSelect.Execute(CurrentMenu.NpcGuid, QuestId);
    }
    return FReply::Handled();
}

FReply SWowQuestDialog::OnGossipOptionClicked(uint32 OptionId)
{
    const FWowGossipOption* Option = CurrentMenu.Options.FindByPredicate(
        [OptionId](const FWowGossipOption& Item) { return Item.OptionId == OptionId; });
    if (!Option || bActionPending) return FReply::Handled();
    if (Option->bCoded || Option->BoxMoney > 0 || !Option->BoxText.IsEmpty())
    {
        PendingGossipOption = *Option;
        ContentArea->ClearChildren();
        ContentArea->AddSlot()
        [ MakeRichText(Option->BoxText.IsEmpty() ? Option->Text : Option->BoxText,
            FLinearColor::Black) ];
        if (Option->BoxMoney > 0)
        {
            ContentArea->AddSlot().Padding(0, 10)
            [ MakeRichText(FString::Printf(TEXT("Cost: %ug %us %uc"),
                Option->BoxMoney / 10000, (Option->BoxMoney / 100) % 100, Option->BoxMoney % 100),
                FLinearColor::Black) ];
        }
        GossipCode.Reset();
        if (Option->bCoded)
        {
            ContentArea->AddSlot().Padding(0, 10)
            [ SAssignNew(GossipCode, SEditableTextBox) ];
        }
        ButtonArea->ClearChildren();
        ButtonArea->AddSlot().AutoWidth()
        [ SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(0).TextStyle(PanelButtonTextStyle()).Text(FText::FromString(TEXT("Confirm")))
            .OnClicked(this, &SWowQuestDialog::OnConfirmGossip) ];
        ButtonArea->AddSlot().AutoWidth().Padding(8, 0)
        [ SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(0).TextStyle(PanelButtonTextStyle()).Text(FText::FromString(TEXT("Cancel")))
            .OnClicked(this, &SWowQuestDialog::OnCancelGossip) ];
        ContentArea->ScrollToStart();
    }
    else if (OnGossipSelect.IsBound())
    {
        bActionPending = true;
        OnGossipSelect.Execute(CurrentMenu.NpcGuid, CurrentMenu.MenuId, OptionId, FString());
    }
    return FReply::Handled();
}


FReply SWowQuestDialog::OnConfirmGossip()
{
    if (PendingGossipOption.IsSet() && !bActionPending && OnGossipSelect.IsBound())
    {
        const FString Code = GossipCode.IsValid() ? GossipCode->GetText().ToString() : FString();
        if (PendingGossipOption->bCoded && Code.IsEmpty()) return FReply::Handled();
        bActionPending = true;
        OnGossipSelect.Execute(CurrentMenu.NpcGuid, CurrentMenu.MenuId, PendingGossipOption->OptionId, Code);
    }
    return FReply::Handled();
}

FReply SWowQuestDialog::OnCancelGossip()
{
    PendingGossipOption.Reset();
    GossipCode.Reset();
    bActionPending = false;
    RefreshContent();
    return FReply::Handled();
}

TSharedRef<SWidget> SWowQuestDialog::MakeRichText(const FString& Input, const FLinearColor& Color, bool bAnimate)
{
    FString Source = Input.Replace(TEXT("$B"), TEXT("\n"), ESearchCase::IgnoreCase);
    if (!PlayerName.IsEmpty()) Source.ReplaceInline(TEXT("$N"), *PlayerName, ESearchCase::IgnoreCase);
    FWowCharacterInfo Character;
    if (Connection.IsValid() && Connection->GetSelectedCharacterInfo(Character))
    {
        static const TCHAR* Classes[] = {TEXT(""),TEXT("Warrior"),TEXT("Paladin"),TEXT("Hunter"),
            TEXT("Rogue"),TEXT("Priest"),TEXT("Death Knight"),TEXT("Shaman"),TEXT("Mage"),
            TEXT("Warlock"),TEXT(""),TEXT("Druid")};
        if (Character.Class < UE_ARRAY_COUNT(Classes) && *Classes[Character.Class])
            Source.ReplaceInline(TEXT("$C"), Classes[Character.Class], ESearchCase::IgnoreCase);
        if (const auto* Race = FDbcStore::Get().ChrRaces().GetById(Character.Race))
            Source.ReplaceInline(TEXT("$R"), *Race->Name, ESearchCase::IgnoreCase);
        int32 Start = Source.Find(TEXT("$G"), ESearchCase::IgnoreCase);
        while (Start != INDEX_NONE)
        {
            const int32 End = Source.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 2);
            const int32 Split = Source.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 2);
            if (End == INDEX_NONE || Split == INDEX_NONE || Split > End) break;
            const FString Choice = Character.Gender == 1 ? Source.Mid(Split+1,End-Split-1) : Source.Mid(Start+2,Split-Start-2);
            Source = Source.Left(Start) + Choice + Source.Mid(End+1);
            Start = Source.Find(TEXT("$G"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Start+Choice.Len());
        }
    }
    auto RegisterStyle = [this](const FLinearColor& Value)
    {
        const FName Name(*FString::Printf(TEXT("c%s"), *Value.ToFColor(false).ToHex()));
        if (!RegisteredTextStyles.Contains(Name))
        {
            RichTextStyles->Set(Name, FTextBlockStyle()
                .SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                .SetColorAndOpacity(Value)
                .SetShadowOffset(FVector2D::ZeroVector)
                .SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f)));
            RegisteredTextStyles.Add(Name);
        }
        return Name;
    };
    const FName DefaultStyle = RegisterStyle(Color);
    FString Markup;
    bool bOpenColor = false;
    for (int32 I = 0; I < Source.Len();)
    {
        if (Source[I] == TEXT('|') && I + 1 < Source.Len())
        {
            const TCHAR Code = Source[I + 1];
            if (Code == TEXT('|')) { Markup += TEXT("|"); I += 2; continue; }
            if (Code == TEXT('r'))
            {
                if (bOpenColor) Markup += TEXT("</>");
                bOpenColor = false; I += 2; continue;
            }
            if (Code == TEXT('n')) { Markup += TEXT("\n"); I += 2; continue; }
            if (Code == TEXT('c') && I + 10 <= Source.Len())
            {
                const FString Hex = Source.Mid(I + 2, 8);
                bool bHex = true;
                for (TCHAR C : Hex) bHex &= FChar::IsHexDigit(C);
                if (bHex)
                {
                    const uint32 Packed = FCString::Strtoui64(*Hex, nullptr, 16);
                    // |cAARRGGBB is an sRGB colour: read as linear it came out far too light
                    // (the dark green gossip quest level was pale on the parchment).
                    const FLinearColor InlineColor = FLinearColor::FromSRGBColor(FColor(
                        uint8((Packed >> 16) & 255), uint8((Packed >> 8) & 255), uint8(Packed & 255), uint8((Packed >> 24) & 255)));
                    if (bOpenColor) Markup += TEXT("</>");
                    Markup += FString::Printf(TEXT("<%s>"), *RegisterStyle(InlineColor).ToString());
                    bOpenColor = true; I += 10; continue;
                }
            }
            if (Code == TEXT('H') || Code == TEXT('T'))
            {
                const int32 End = Source.Find(Code == TEXT('H') ? TEXT("|h") : TEXT("|t"),
                    ESearchCase::CaseSensitive, ESearchDir::FromStart, I + 2);
                if (End != INDEX_NONE) { I = End + 2; continue; }
            }
            if (Code == TEXT('h')) { I += 2; continue; }
        }
        const TCHAR C = Source[I++];
        if (C == TEXT('&')) Markup += TEXT("&amp;");
        else if (C == TEXT('<')) Markup += TEXT("&lt;");
        else if (C == TEXT('>')) Markup += TEXT("&gt;");
        else Markup.AppendChar(C);
    }
    if (bOpenColor) Markup += TEXT("</>");
    if (bAnimate)
    {
        const double Delay = NextTextRevealDelay;
        NextTextRevealDelay += FWowQuestTextReveal(Markup).Duration();
        return SNew(SWowProgressiveQuestText, Markup, RichTextStyles.ToSharedRef(), DefaultStyle)
            .Delay(Delay).Instant_Lambda([](){return FWowClientPreferences::GetWorldOption(TEXT("instantquesttext"));});
    }
    return SNew(SRichTextBlock)
        .Text(FText::FromString(Markup))
        .TextStyle(&RichTextStyles->GetWidgetStyle<FTextBlockStyle>(DefaultStyle))
        .DecoratorStyleSet(RichTextStyles.Get())
        .AutoWrapText(true);
}

void SWowQuestDialog::UpdateGreeting(uint32 TextId, const FString& Greeting)
{
    // A late NPC_TEXT response may update only its existing visible menu.
    // It must never reopen a dismissed menu or replace quest details.
    if (!bIsQuestList || !GetVisibility().IsVisible() || CurrentMenu.TextId != TextId) return;
    CurrentMenu.Greeting = Greeting;
    if (!PendingGossipOption.IsSet() && !bActionPending)
    {
        const float Scroll = ContentArea->GetScrollOffset();
        RefreshContent();
        ContentArea->SetScrollOffset(Scroll);
    }
}

void SWowQuestDialog::SetUIManager(UWowUIManager* UI) { UIManager=UI; EnsurePageScrollKnob(); }

void SWowQuestDialog::EnsurePageScrollKnob()
{
    if (PageScrollStyle.NormalThumbImage.GetResourceObject()) return;
    const FBox2f KnobUV(FVector2f(.2f, .125f), FVector2f(.8f, .875f));
    if (const FSlateBrush* Knob = ArtBrushUV(TEXT("Interface\\Buttons\\UI-ScrollBar-Knob"), KnobUV))
        PageScrollStyle.SetNormalThumbImage(*Knob).SetHoveredThumbImage(*Knob).SetDraggedThumbImage(*Knob);
}

void SWowQuestDialog::SetConnectionManager(UWowConnectionManager* InConnection) { Connection=InConnection; }

const FSlateBrush* SWowQuestDialog::ArtBrushUV(const FString& Path, const FBox2f& UV) const
{
    const FString Key = FString::Printf(TEXT("%s|%.3f,%.3f,%.3f,%.3f"), *Path, UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y);
    if (const TSharedPtr<FWowRetainedSlateBrush>* Found = ArtBrushes.Find(Key)) return Found->Get();
    UTexture2D* Texture = UIManager.IsValid() && UIManager->GetFrameManager() ? UIManager->GetFrameManager()->LoadUITexture(Path) : nullptr;
    if (!Texture) return nullptr;
    TSharedPtr<FWowRetainedSlateBrush> Brush = MakeShared<FWowRetainedSlateBrush>();
    Brush->SetResourceObject(Texture);
    Brush->DrawAs = ESlateBrushDrawType::Image;
    Brush->ImageSize = FVector2D(Texture->GetSizeX() * (UV.Max.X - UV.Min.X), Texture->GetSizeY() * (UV.Max.Y - UV.Min.Y));
    Brush->SetUVRegion(UV);
    ArtBrushes.Add(Key, Brush);
    return Brush.Get();
}

void SWowQuestDialog::ScrollPage(float Direction)
{
    // UIPanelScrollFrame buttons step by half the frame height.
    if (!ContentArea.IsValid()) return;
    const float Step = ContentArea->GetCachedGeometry().GetLocalSize().Y * 0.5f;
    ContentArea->SetScrollOffset(FMath::Clamp(ContentArea->GetScrollOffset() + Direction * FMath::Max(16.f, Step), 0.f, ContentArea->GetScrollOffsetOfEnd()));
}

FString SWowQuestDialog::TitleName() const
{
    const FString Name = bIsQuestList ? CurrentMenu.NpcName : CurrentQuest.QuestGiverName;
    return Name.IsEmpty() ? FString(TEXT("Quest")) : Name;
}

FSlateFontInfo SWowQuestDialog::TitleFontFor(const FString& Text) const
{
    // GameFontHighlight (12) at the window scale, shrunk until the name fits QuestFrameNpcNameText's 235.
    const float S = GetScale();
    FSlateFontInfo Font = FWowWindowStyle::TitleFont(FMath::Max(7, FMath::RoundToInt(12.f * S * .75f)));
    if (FSlateApplication::IsInitialized())
    {
        const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        while (Font.Size > 6 && Measure->Measure(Text, Font).X > 235.f * S) --Font.Size;
    }
    return Font;
}

const FTextBlockStyle* SWowQuestDialog::PanelButtonTextStyle() const
{
    // UIPanelButtonTemplate text: GameFontNormal in the button's foreground (gold, white on hover,
    // grey disabled), at the window scale.
    const int32 Size = FMath::Max(7, FMath::RoundToInt(12.f * GetScale() * .75f));
    if (ButtonTextStyle.Font.Size != Size || !ButtonTextStyle.Font.HasValidFont())
    {
        ButtonTextStyle = FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"));
        ButtonTextStyle.SetFont(FWowWindowStyle::TitleFont(Size));
        ButtonTextStyle.SetColorAndOpacity(FSlateColor::UseForeground());
        ButtonTextStyle.SetShadowOffset(FVector2D(1, 1));
        ButtonTextStyle.SetShadowColorAndOpacity(FLinearColor::Black);
    }
    return &ButtonTextStyle;
}
