#include "SWowQuestLog.h"
#include "WowColor.h"
#include "WowTextCentering.h"
#include "WowWindowStyle.h"
#include "WowUiSurfaces.h"
#include "WowNativeWidgetStyle.h"
#include "WowItemPresentation.h"
#include "WowInventoryManager.h"
#include "Brushes/SlateColorBrush.h"
#include "WowRetainedSlateBrush.h"
#include "WowUIManager.h"
#include "WowClientPreferences.h"
#include "WowFrameManager.h"
#include "WowWotlkStyle.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "WowUpdateFields.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/SpellDbc.h"
#include "Formats/Dbc/SpellIconDbc.h"
#include "Engine/Texture2D.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/SCanvas.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SToolTip.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "WowQuestLog"

namespace
{
// Colours from the 3.3.5 FontStyles (QuestTitleFont / QuestFont on parchment).
// Blizzard values are sRGB; FLinearColor literals would wash them out.
FLinearColor Srgb(uint8 R, uint8 G, uint8 B) { return FLinearColor::FromSRGBColor(FColor(R, G, B)); }
FLinearColor ParchmentTitle()  { return Srgb(0, 0, 0); }
FLinearColor ParchmentText()   { return Srgb(46, 31, 15); }   // 0.18, 0.12, 0.06
FLinearColor ParchmentDone()   { return Srgb(98, 82, 62); }   // completed objective line

constexpr uint16 CMSG_QUESTLOG_REMOVE_QUEST = 0x194;
constexpr uint16 CMSG_PUSHQUESTTOPARTY = 0x19D;

const TCHAR* RaceName(uint8 Race)
{
    switch (Race)
    {
    case 1: return TEXT("Human"); case 2: return TEXT("Orc"); case 3: return TEXT("Dwarf");
    case 4: return TEXT("Night Elf"); case 5: return TEXT("Undead"); case 6: return TEXT("Tauren");
    case 7: return TEXT("Gnome"); case 8: return TEXT("Troll"); case 10: return TEXT("Blood Elf");
    case 11: return TEXT("Draenei"); default: return TEXT("adventurer");
    }
}
const TCHAR* ClassName(uint8 Class)
{
    switch (Class)
    {
    case 1: return TEXT("Warrior"); case 2: return TEXT("Paladin"); case 3: return TEXT("Hunter");
    case 4: return TEXT("Rogue"); case 5: return TEXT("Priest"); case 6: return TEXT("Death Knight");
    case 7: return TEXT("Shaman"); case 8: return TEXT("Mage"); case 9: return TEXT("Warlock");
    case 11: return TEXT("Druid"); default: return TEXT("adventurer");
    }
}

TSharedRef<SWidget> SectionHeading(const FText& Text)
{
    return SNew(STextBlock).Text(Text).Font(FWowNativeWidgetStyle::Heading(18)).ColorAndOpacity(ParchmentTitle());
}

TSharedRef<SWidget> BodyText(const FString& Text, const FLinearColor& Color = ParchmentText())
{
    return SNew(STextBlock).Text(FText::FromString(Text)).Font(FWowWindowStyle::TitleFont(13))
        .ColorAndOpacity(Color).AutoWrapText(true);
}

}

FString SWowQuestLog::ExpandQuestText(const FString& Text, const FString& PlayerName, const FString& RaceNameText,
    const FString& ClassNameText, bool bFemale)
{
    FString Out;
    Out.Reserve(Text.Len() + 16);
    for (int32 I = 0; I < Text.Len(); ++I)
    {
        const TCHAR C = Text[I];
        if (C != TEXT('$') || I + 1 >= Text.Len()) { Out.AppendChar(C); continue; }
        const TCHAR Token = Text[I + 1];
        switch (Token)
        {
        case TEXT('N'): case TEXT('n'): Out += PlayerName; ++I; break;
        case TEXT('R'): Out += RaceNameText; ++I; break;
        case TEXT('r'): Out += RaceNameText.ToLower(); ++I; break;
        case TEXT('C'): Out += ClassNameText; ++I; break;
        case TEXT('c'): Out += ClassNameText.ToLower(); ++I; break;
        case TEXT('B'): case TEXT('b'): Out.AppendChar(TEXT('\n')); ++I; break;
        case TEXT('G'): case TEXT('g'):
        {
            // $Gmale:female;
            const int32 Colon = Text.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I + 2);
            const int32 Semi = Colon == INDEX_NONE ? INDEX_NONE : Text.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Colon + 1);
            if (Colon == INDEX_NONE || Semi == INDEX_NONE) { Out.AppendChar(C); break; }
            Out += bFemale ? Text.Mid(Colon + 1, Semi - Colon - 1) : Text.Mid(I + 2, Colon - I - 2).TrimStart();
            I = Semi;
            break;
        }
        default: Out.AppendChar(C); break;
        }
    }
    return Out;
}

void SWowQuestLog::Construct(const FArguments& InArgs, FWowPacketHandler* InPacketHandler)
{
    PacketHandler = InPacketHandler;
    OnTrackedQuestsChanged = InArgs._OnTrackedQuestsChanged;
    OnShowMap = InArgs._OnShowMap;
    UIManager = InArgs._UIManager;
    if (UIManager.IsValid() && UIManager->GetFrameManager() && UIManager->GetFrameManager()->GetUIScale() > 0.f)
        Scale = UIManager->GetFrameManager()->GetUIScale();

    ChildSlot
    [
        SAssignNew(Root, SBox)
        .WidthOverride_Lambda([this]() { return FrameW * Scale; })
        .HeightOverride_Lambda([this]() { return FrameH * Scale; })
    ];
    Rebuild();
    SetVisibility(CurrentVisibility);
    UpdateQuestList();
}

void SWowQuestLog::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    if (UIManager.IsValid() && UIManager->GetFrameManager())
    {
        const float NewScale = UIManager->GetFrameManager()->GetUIScale();
        if (NewScale > 0.f && !FMath::IsNearlyEqual(NewScale, Scale))
        {
            Scale = NewScale;
            Rebuild();
            UpdateQuestList();
        }
    }
}

FSlateFontInfo SWowQuestLog::Fnt(float WowPixels) const
{
    // WoW font heights are pixels; Slate sizes are points (x 0.75).
    return FWowWindowStyle::TitleFont(FMath::Max(6, FMath::RoundToInt(WowPixels * Scale * .75f)));
}

void SWowQuestLog::Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const
{
    Canvas.AddSlot().Position(FVector2D(X, Y) * Scale).Size(FVector2D(W, H) * Scale)[Widget];
}

TSharedRef<SWidget> SWowQuestLog::PanelButton(const FText& Label, float W, float H, FOnClicked OnClicked, TAttribute<bool> Enabled) const
{
    // UIPanelButtonTemplate: red button art, GameFontNormal (gold, grey when disabled).
    return SNew(SBox).WidthOverride(W * Scale).HeightOverride(H * Scale)
        [
            SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center)
            .ContentPadding(FMargin(2 * Scale, 0)).IsEnabled(Enabled).OnClicked(OnClicked)
            [ SNew(STextBlock).Text(Label).Font(Fnt(12)).OverflowPolicy(ETextOverflowPolicy::Clip).RenderTransform(WowCapCenterTransform(Fnt(12)))
              .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
        ];
}

void SWowQuestLog::PlaceScrollBar(SCanvas& Canvas, float X, float Top, float Bottom, TSharedRef<SScrollBar> Bar,
    TSharedPtr<SScrollBox> Box) const
{
    // UIPanelScrollUpButtonTemplate / DownButton (16x16, TexCoords .20-.80, .25-.75) outside
    // the slider, the knob (UI-ScrollBar-Knob) inside; the trough is part of the frame art.
    auto Arrow = [this, Box](const TCHAR* Name, float Step)
    {
        const FString Base = FString::Printf(TEXT("Interface\\Buttons\\UI-ScrollBar-Scroll%sButton-"), Name);
        const FBox2f UV(FVector2f(.20f, .25f), FVector2f(.80f, .75f));
        TSharedRef<FButtonStyle> Style = MakeShared<FButtonStyle>();
        if (const FSlateBrush* Up = LoadBrush(Base + TEXT("Up"), UV)) { Style->SetNormal(*Up); Style->SetHovered(*Up); }
        if (const FSlateBrush* Down = LoadBrush(Base + TEXT("Down"), UV)) Style->SetPressed(*Down);
        if (const FSlateBrush* Off = LoadBrush(Base + TEXT("Disabled"), UV)) Style->SetDisabled(*Off);
        Style->SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        TWeakPtr<SScrollBox> WeakBox = Box;
        return SNew(SButton).ButtonStyle(&Style.Get()).ContentPadding(0).ClickMethod(EButtonClickMethod::MouseDown)
            .IsEnabled_Lambda([WeakBox, Step]()
            {
                const TSharedPtr<SScrollBox> B = WeakBox.Pin();
                if (!B.IsValid()) return false;
                return Step < 0 ? B->GetScrollOffset() > 0.5f : B->GetScrollOffset() < B->GetScrollOffsetOfEnd() - 0.5f;
            })
            .OnClicked_Lambda([WeakBox, Step, Style]()
            {
                if (const TSharedPtr<SScrollBox> B = WeakBox.Pin())
                    B->SetScrollOffset(FMath::Clamp(B->GetScrollOffset() + Step, 0.f, B->GetScrollOffsetOfEnd()));
                return FReply::Handled();
            });
    };
    Place(Canvas, X, Top - 16, 16, 16, Arrow(TEXT("Up"), -16.f * Scale));
    Place(Canvas, X, Top, 16, Bottom - Top, Bar);
    Place(Canvas, X, Bottom, 16, 16, Arrow(TEXT("Down"), 16.f * Scale));
}

void SWowQuestLog::Rebuild()
{
    if (!Root.IsValid()) return;
    TSharedRef<SCanvas> Canvas = SNew(SCanvas);

    // Scroll bars: knob only (UI-ScrollBar-Knob, TexCoords .20-.80 / .125-.875).
    ScrollStyle = MakeShared<FScrollBarStyle>(FCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>("ScrollBar"));
    static const FSlateNoResource NoTrack;
    ScrollStyle->SetVerticalBackgroundImage(NoTrack).SetVerticalTopSlotImage(NoTrack).SetVerticalBottomSlotImage(NoTrack)
        .SetHorizontalBackgroundImage(NoTrack).SetHorizontalTopSlotImage(NoTrack).SetHorizontalBottomSlotImage(NoTrack);
    if (const FSlateBrush* Knob = LoadBrush(TEXT("Interface\\Buttons\\UI-ScrollBar-Knob"),
        FBox2f(FVector2f(.20f, .125f), FVector2f(.80f, .875f))))
        ScrollStyle->SetNormalThumbImage(*Knob).SetHoveredThumbImage(*Knob).SetDraggedThumbImage(*Knob);
    TSharedRef<SScrollBar> ListBar = SNew(SScrollBar).Orientation(Orient_Vertical).Style(ScrollStyle.Get())
        .Thickness(FVector2D(16 * Scale, 16 * Scale)).Padding(0).AlwaysShowScrollbar(true);
    TSharedRef<SScrollBar> DetailBar = SNew(SScrollBar).Orientation(Orient_Vertical).Style(ScrollStyle.Get())
        .Thickness(FVector2D(16 * Scale, 16 * Scale)).Padding(0).AlwaysShowScrollbar(true);

    // Frame art: book icon (BACKGROUND) under the dual-pane pages (BORDER).
    auto Img = [this](const TCHAR* Path, FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1)))
    {
        const FString TexturePath = Path;
        return SNew(SImage).Visibility(EVisibility::HitTestInvisible)
            .Image_Lambda([this, TexturePath, UV]() { return LoadBrush(TexturePath, UV); });
    };
    Place(*Canvas, 3, 4, 64, 64, Img(TEXT("Interface\\QuestFrame\\UI-QuestLog-BookIcon")));
    Place(*Canvas, 0, 0, 512, 445, Img(TEXT("Interface\\QuestFrame\\UI-QuestLogDualPane-Left"),
        FBox2f(FVector2f(0.f, 0.f), FVector2f(1.f, .86914f))));
    Place(*Canvas, 512, 0, 170, 445, Img(TEXT("Interface\\QuestFrame\\UI-QuestLogDualPane-RIGHT"),
        FBox2f(FVector2f(0.f, 0.f), FVector2f(.6640625f, .86914f))));

    // QuestLogTitleText: GameFontNormal, TOP 0,-15, 300 wide.
    Place(*Canvas, 191, 15, 300, 14, WowTitleBox(SNew(STextBlock).Text(LOCTEXT("QuestLogTitle", "Quest Log")).Font(Fnt(12))
        .ColorAndOpacity(FWowWindowStyle::Gold()).Justification(ETextJustify::Center)
        .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black), Fnt(12)));

    // QuestLogFrameCloseButton (UIPanelCloseButton): TOPRIGHT +2,-8.
    {
        TSharedRef<FButtonStyle> Style = MakeShared<FButtonStyle>();
        if (const FSlateBrush* Up = LoadBrush(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"))) { Style->SetNormal(*Up); Style->SetHovered(*Up); }
        if (const FSlateBrush* Down = LoadBrush(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Down"))) Style->SetPressed(*Down);
        Style->SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        Place(*Canvas, 652, 8, 32, 32, SNew(SButton).ButtonStyle(&Style.Get()).ContentPadding(0)
            .OnClicked_Lambda([this, Style]() { return OnCloseButtonClicked(); }));
    }

    // QuestLogCount (TOPLEFT 80,-38): Common-Input-Border box, GameFontNormalSmall, width = text + 15.
    {
        const FSlateBrush* Border = LoadBrush(TEXT("Interface\\Common\\Common-Input-Border"),
            FBox2f(FVector2f(0.f, 0.f), FVector2f(1.f, .625f)));
        static FSlateBrush CountBrush;
        if (Border) { CountBrush = *Border; CountBrush.DrawAs = ESlateBrushDrawType::Box; CountBrush.Margin = FMargin(.0625f, .4f, .0625f, .4f); }
        Place(*Canvas, 80, 38, 200, 20, SNew(SBox).HAlign(HAlign_Left)
            [
                SAssignNew(CountBox, SBox).WidthOverride(120 * Scale).HeightOverride(20 * Scale)
                [
                    SNew(SBorder).BorderImage(&CountBrush).Padding(FMargin(7 * Scale, 0)).HAlign(HAlign_Right).VAlign(VAlign_Center)
                    [ SAssignNew(QuestCountText, STextBlock).Font(Fnt(10)).ColorAndOpacity(FWowWindowStyle::Gold()) ]
                ]
            ]);
    }

    // QuestLogFrameShowMapButton (TOPRIGHT -25,-38, 48x32) with "Show Map" to its left.
    Place(*Canvas, 480, 38, 177, 32, SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(0)
        .OnClicked_Lambda([this]() { OnShowMap.ExecuteIfBound(); return FReply::Handled(); })
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(LOCTEXT("ShowMap", "Show Map")).Font(Fnt(12)).ColorAndOpacity(FWowWindowStyle::Gold())
              .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(48 * Scale).HeightOverride(32 * Scale)
              [ Img(TEXT("Interface\\QuestFrame\\UI-QuestMap_Button"), FBox2f(FVector2f(.125f, 0.f), FVector2f(.875f, .5f))) ] ]
        ]);

    // QuestLogScrollFrame (TOPLEFT 19,-75, 305x335) and its HybridScrollBar (0,-13 / 0,14).
    SAssignNew(QuestListScrollBox, SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always)
        .ExternalScrollbar(ListBar).ScrollBarVisibility(EVisibility::Collapsed);
    Place(*Canvas, 19, 75, 305, 335, QuestListScrollBox.ToSharedRef());
    PlaceScrollBar(*Canvas, 324, 88, 396, ListBar, QuestListScrollBox);

    // QuestLogDetailScrollFrame in the log: TOPRIGHT -32,-77, 298x334; bar at TOPRIGHT +6,-13.
    SAssignNew(QuestDetailScrollBox, SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always)
        .ExternalScrollbar(DetailBar).ScrollBarVisibility(EVisibility::Collapsed)
        + SScrollBox::Slot().Padding(FMargin(4 * Scale, 2 * Scale, 10 * Scale, 8 * Scale))[ SAssignNew(QuestDetailBox, SVerticalBox) ];
    Place(*Canvas, 352, 77, 298, 334, QuestDetailScrollBox.ToSharedRef());
    PlaceScrollBar(*Canvas, 656, 90, 395, DetailBar, QuestDetailScrollBox);

    // QuestLogControlPanel (BOTTOMLEFT 18,11, 307x26): Abandon / Share / Track.
    Place(*Canvas, 18, 410, 307, 26, SAssignNew(BottomBarBox, SBox).VAlign(VAlign_Center));
    // QuestLogFrameCancelButton: BOTTOMRIGHT -7,14, 80x22.
    Place(*Canvas, 595, 411, 80, 22, PanelButton(LOCTEXT("Close", "Close"), 80, 22,
        FOnClicked::CreateSP(this, &SWowQuestLog::OnCloseButtonClicked)));

    Root->SetContent(Canvas);
}

const FSlateBrush* SWowQuestLog::LoadBrush(const FString& TexturePath, FBox2f UV, FSlateColor Tint) const
{
    const FString Key = FString::Printf(TEXT("%s|%.3f,%.3f,%.3f,%.3f"), *TexturePath, UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y);
    if (const TSharedPtr<FWowRetainedSlateBrush>* Existing = TextureBrushes.Find(Key))
    {
        (*Existing)->TintColor = Tint;
        return Existing->Get();
    }
    const double Now = FPlatformTime::Seconds();
    if (const double* Failed = FailedTextureTimes.Find(Key); Failed && Now - *Failed < 2.0) return nullptr;
    UTexture2D* Texture = UIManager.IsValid() && UIManager->GetFrameManager()
        ? UIManager->GetFrameManager()->LoadUITexture(TexturePath) : nullptr;
    if (!Texture) { FailedTextureTimes.Add(Key, Now); return nullptr; }
    FailedTextureTimes.Remove(Key);
    TSharedPtr<FWowRetainedSlateBrush> Brush = MakeShared<FWowRetainedSlateBrush>();
    Brush->SetResourceObject(Texture);
    Brush->DrawAs = ESlateBrushDrawType::Image;
    Brush->SetUVRegion(UV);
    Brush->TintColor = Tint;
    Brush->ImageSize = FVector2D(Texture->GetSizeX() * (UV.Max.X - UV.Min.X), Texture->GetSizeY() * (UV.Max.Y - UV.Min.Y));
    TextureBrushes.Add(Key, Brush);
    return Brush.Get();
}

int32 SWowQuestLog::GetPlayerLevel() const
{
    const FWowPlayerEntity* Player = PacketHandler ? PacketHandler->EntityManager.GetLocalPlayer() : nullptr;
    return Player ? FMath::Max(1, Player->GetLevel()) : 1;
}

FLinearColor SWowQuestLog::GetQuestDifficultyColor(int32 QuestLevel) const
{
    return DifficultyColor(QuestLevel, GetPlayerLevel());
}

FString SWowQuestLog::TitleWithLevel(const FString& Title, int32 QuestLevel)
{
    if (QuestLevel <= 0 || !FWowClientPreferences::GetWorldOption(TEXT("showquestlevel"))) return Title;
    return FString::Printf(TEXT("[%d] %s"), QuestLevel, *Title);
}

FLinearColor SWowQuestLog::DifficultyColor(int32 QuestLevel, int32 PlayerLevel)
{
    // GetQuestDifficultyColor() from the 3.3.5 UI.
    PlayerLevel = FMath::Max(1, PlayerLevel);
    const int32 Diff = QuestLevel - PlayerLevel;
    if (QuestLevel <= 0) return WowColor::FromWow(1.f, .82f, 0.f);
    if (Diff >= 5) return WowColor::FromWow(1.f, .1f, .1f);
    if (Diff >= 3) return WowColor::FromWow(1.f, .5f, .25f);
    if (Diff >= -2) return WowColor::FromWow(1.f, .82f, 0.f);
    const int32 GreenRange = PlayerLevel <= 5 ? 5 : PlayerLevel <= 39 ? 5 + PlayerLevel / 10 : PlayerLevel <= 59 ? 9 : 10;
    if (-Diff <= GreenRange) return WowColor::FromWow(.25f, .75f, .25f);
    return WowColor::FromWow(.5f, .5f, .5f);
}

FString SWowQuestLog::ExpandForPlayer(const FString& Text) const
{
    FString Name = TEXT("adventurer");
    uint8 Race = 0, Class = 0, Gender = 0;
    if (PacketHandler)
    {
        const uint64 Guid = PacketHandler->EntityManager.LocalPlayerGuid;
        if (const FString* Cached = PacketHandler->PlayerNameCache.Find(Guid); Cached && !Cached->IsEmpty()) Name = *Cached;
        if (const FWowPlayerEntity* Player = PacketHandler->EntityManager.GetLocalPlayer())
        { Race = Player->GetRaceId(); Class = Player->GetClassId(); Gender = Player->GetGenderId(); }
    }
    return ExpandQuestText(Text, Name, RaceName(Race), ClassName(Class), Gender == 1);
}

const FWowQuestLogEntry* SWowQuestLog::GetSelectedQuest() const
{
    return PacketHandler && PacketHandler->QuestLog.IsValidIndex(SelectedQuestIndex)
        ? &PacketHandler->QuestLog[SelectedQuestIndex] : nullptr;
}

int32 SWowQuestLog::FindQuestLogSlot(uint32 QuestId) const
{
    const FWowPlayerEntity* Player = PacketHandler ? PacketHandler->EntityManager.GetLocalPlayer() : nullptr;
    if (!Player || !QuestId) return INDEX_NONE;
    for (uint16 Slot = 0; Slot < PlayerField::QUEST_LOG_SLOT_COUNT; ++Slot)
        if (Player->GetField(PlayerField::QUEST_LOG_START + Slot * PlayerField::QUEST_LOG_FIELDS_PER_SLOT) == QuestId)
            return Slot;
    return INDEX_NONE;
}

void SWowQuestLog::UpdateQuestList()
{
    if (!QuestListScrollBox.IsValid() || !PacketHandler)
    {
        return;
    }

    QuestListScrollBox->ClearChildren();


    if (SelectedQuestIndex == INDEX_NONE && !PacketHandler->QuestLog.IsEmpty())
    {
        SelectedQuestIndex = 0;
    }
    if (SelectedQuestIndex >= PacketHandler->QuestLog.Num())
    {
        SelectedQuestIndex = PacketHandler->QuestLog.IsEmpty() ? INDEX_NONE : 0;
    }
    // The abandon popup belongs to its quest (SetAbandonQuest), not to the selection; it closes
    // when that quest leaves the log.
    if (PendingAbandonQuestId && !PacketHandler->QuestLog.ContainsByPredicate(
        [this](const FWowQuestLogEntry& Entry) { return Entry.QuestId == PendingAbandonQuestId; }))
    {
        HideAbandonPopup();
    }

    TArray<FString> ZoneOrder;
    TMap<FString, TArray<int32>> QuestsByZone;
    for (int32 QuestIndex = 0; QuestIndex < PacketHandler->QuestLog.Num(); ++QuestIndex)
    {
        const FString ZoneName = GetQuestZoneName(PacketHandler->QuestLog[QuestIndex]);
        if (!QuestsByZone.Contains(ZoneName))
        {
            ZoneOrder.Add(ZoneName);
        }
        QuestsByZone.FindOrAdd(ZoneName).Add(QuestIndex);
    }

    for (const FString& ZoneName : ZoneOrder)
    {
        const TArray<int32>& ZoneQuests = QuestsByZone.FindChecked(ZoneName);
        QuestListScrollBox->AddSlot().Padding(0, 3, 0, 1)[ CreateZoneHeader(ZoneName, ZoneQuests.Num()) ];
        if (CollapsedZones.Contains(ZoneName)) continue;
        for (const int32 QuestIndex : ZoneQuests)
        {
            QuestListScrollBox->AddSlot().Padding(8, 0, 0, 0)[ CreateQuestEntry(PacketHandler->QuestLog[QuestIndex], QuestIndex) ];
        }
    }

    if (QuestCountText.IsValid())
    {
        // QUEST_LOG_COUNT_TEMPLATE "Quests: %d/%d"; QuestLog_UpdateQuestCount sizes the box to the text + 15.
        const FString CountText = FString::Printf(TEXT("Quests: %d/25"), FMath::Min(PacketHandler->QuestLog.Num(), 25));
        QuestCountText->SetText(FText::FromString(CountText));
        if (CountBox.IsValid() && FSlateApplication::IsInitialized())
        {
            const FVector2D Size = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(CountText, Fnt(10));
            CountBox->SetWidthOverride(Size.X + 15.f * Scale);
        }
    }

    if (PacketHandler->QuestLog.IsEmpty())
    {
        QuestListScrollBox->AddSlot().Padding(0, 20, 0, 20)
        [
            SNew(STextBlock).Text(LOCTEXT("NoQuests", "No active quests"))
            .ColorAndOpacity(FWowWotlkStyle::TextLight()).Font(FWowWindowStyle::TitleFont(12))
            .Justification(ETextJustify::Center)
        ];
    }

    if (QuestDetailBox.IsValid())
    {
        QuestDetailBox->ClearChildren();
        QuestDetailBox->AddSlot().AutoHeight()[ CreateQuestDetailPanel() ];
    }
    if (BottomBarBox.IsValid())
    {
        BottomBarBox->SetContent(CreateBottomBar());
    }
}

TSharedRef<SWidget> SWowQuestLog::CreateQuestEntry(const FWowQuestLogEntry& QuestEntry, int32 QuestIndex) const
{
    const bool bSelected = QuestIndex == SelectedQuestIndex;
    const FLinearColor Color = GetQuestDifficultyColor(QuestEntry.Level);
    const FString StateTag = QuestEntry.State == 1 ? TEXT("(Complete)") : QuestEntry.State == 2 ? TEXT("(Failed)") : TEXT("");
    const FLinearColor TextColor = bSelected ? FLinearColor::White : Color;
    // Selected row: the original difficulty-tinted UI-QuestLogTitleHighlight bar.
    const FSlateBrush* Highlight = bSelected ? LoadBrush(TEXT("Interface\\QuestFrame\\UI-QuestLogTitleHighlight"),
        FBox2f(FVector2f(0, 0), FVector2f(1, 1)), FSlateColor(FLinearColor(Color.R, Color.G, Color.B, .85f))) : nullptr;
    static const FSlateColorBrush SelectionFallback(FLinearColor::White);

    return SNew(SButton)
        .ButtonStyle(FCoreStyle::Get(), "NoBorder")
        .ContentPadding(0.0f)
        // r366 (Stefan 16.10: a "Level 1" tooltip stuck to the mouse after the quest log closed, through
        // every other window): no Slate tooltip on the rows. The client's quest log shows none either;
        // the level is in the title's colour (and "[1]" when "Show quest level" is on).
        .OnClicked(const_cast<SWowQuestLog*>(this), &SWowQuestLog::OnQuestSelected, QuestIndex)
        [
            SNew(SBorder)
            .BorderImage(bSelected ? (Highlight ? Highlight : &SelectionFallback) : FWowWotlkStyle::TransparentBrush())
            .BorderBackgroundColor(bSelected && !Highlight ? FLinearColor(Color.R, Color.G, Color.B, .45f) : FLinearColor::White)
            .Padding(FMargin(4.0f, 1.0f, 6.0f, 1.0f))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    // Tracked quests carry a check mark in front of the title.
                    SNew(SBox).WidthOverride(14)
                    [ SNew(STextBlock).Text(FText::FromString(QuestEntry.bIsTracked ? TEXT("\u2713") : TEXT("")))
                      .ColorAndOpacity(WowColor::FromWow(1.f, .82f, 0.f)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10)) ]
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(FText::FromString(TitleWithLevel(GetQuestName(QuestEntry), QuestEntry.Level)))
                    .ColorAndOpacity(TextColor).Font(FWowWindowStyle::TitleFont(13))
                    .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, .8f))
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
                [
                    SNew(STextBlock).Text(FText::FromString(StateTag))
                    .ColorAndOpacity(TextColor).Font(FWowWindowStyle::TitleFont(13))
                    .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, .8f))
                ]
            ]
        ];
}

TSharedRef<SWidget> SWowQuestLog::CreateZoneHeader(const FString& ZoneName, int32 QuestCount) const
{
    const bool bCollapsed = CollapsedZones.Contains(ZoneName);
    const FString Button = bCollapsed ? TEXT("Interface\\Buttons\\UI-PlusButton-Up") : TEXT("Interface\\Buttons\\UI-MinusButton-Up");
    return SNew(SButton)
        .ButtonStyle(FCoreStyle::Get(), "NoBorder")
        .ContentPadding(0.0f)
        .OnClicked(const_cast<SWowQuestLog*>(this), &SWowQuestLog::OnZoneHeaderClicked, ZoneName)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 1, 5, 1)
            [
                SNew(SBox).WidthOverride(16).HeightOverride(16)
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()[ SNew(SImage).Image_Lambda([this, Button]() { return LoadBrush(Button); }) ]
                    // Text fallback until the original button art is available.
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text(FText::FromString(bCollapsed ? TEXT("+") : TEXT("-")))
                      .Visibility_Lambda([this, Button]() { return LoadBrush(Button) ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
                      .ColorAndOpacity(WowColor::FromWow(1.f, .82f, 0.f)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)) ]
                ]
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(ZoneName))
              .ColorAndOpacity(WowColor::FromWow(.80f, .80f, .80f)).Font(FWowWindowStyle::TitleFont(13))
              .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, .8f)) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
            [ SNew(STextBlock).Text(bCollapsed ? FText::AsNumber(QuestCount) : FText::GetEmpty())
              .ColorAndOpacity(WowColor::FromWow(.65f, .65f, .65f)).Font(FWowWindowStyle::TitleFont(11)) ]
        ];
}

TSharedRef<SWidget> SWowQuestLog::CreateRewardCard(uint32 ItemEntry, uint32 Count) const
{
    // Original reward button: 37px item icon + brown name plate with white text.
    UWowConnectionManager* Connection = UIManager.IsValid() ? UIManager->GetConnectionManager() : nullptr;
    FWowPacketHandler* Handler = PacketHandler;
    if (Handler && !Handler->ItemTemplates.Contains(ItemEntry)) Handler->RequestItemTemplate(ItemEntry);
    const FString IconPath = FWowItemPresentation::IconPath(ItemEntry);
    return SNew(SBox).WidthOverride(182).HeightOverride(44)
        .ToolTip(FWowItemPresentation::Tooltip(Connection, ItemEntry))
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).Padding(1)
                .BorderBackgroundColor_Lambda([Handler, ItemEntry]()
                {
                    const FWowItemTemplateInfo* Info = Handler ? Handler->ItemTemplates.Find(ItemEntry) : nullptr;
                    return FSlateColor(Info && Info->Quality >= 2 ? FWowInventoryManager::GetQualityColor(Info->Quality) : WowColor::FromWow(.1f, .1f, .1f));
                })
                [
                    SNew(SBox).WidthOverride(40).HeightOverride(40)
                    [
                        SNew(SOverlay)
                        + SOverlay::Slot()[ SNew(SImage).Image_Lambda([this, IconPath]() { return LoadBrush(IconPath); }) ]
                        + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0, 0, 2, 0)
                        [ SNew(STextBlock).Text(Count > 1 ? FText::AsNumber(Count) : FText::GetEmpty())
                          .Font(FWowWindowStyle::TitleFont(12)).ColorAndOpacity(FLinearColor::White)
                          .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
                    ]
                ]
            ]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [
                SNew(SBox).HeightOverride(40)
                [
                    SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).VAlign(VAlign_Center)
                    .BorderBackgroundColor(FLinearColor::FromSRGBColor(FColor(92, 66, 40, 235))).Padding(FMargin(7, 2))
                    [
                        SNew(STextBlock).AutoWrapText(true).Font(FWowWindowStyle::TitleFont(12))
                        .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
                        .ColorAndOpacity(FLinearColor::White)
                        .Text_Lambda([Handler, ItemEntry]()
                        {
                            const FWowItemTemplateInfo* Info = Handler ? Handler->ItemTemplates.Find(ItemEntry) : nullptr;
                            return FText::FromString(Info && !Info->Name.IsEmpty() ? Info->Name : FString(TEXT("Loading...")));
                        })
                    ]
                ]
            ]
        ];
}

TSharedRef<SWidget> SWowQuestLog::CreateMoneyWidget(int32 Copper) const
{
    TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
    auto Add = [this, &Row](int32 Value, const TCHAR* Icon, const TCHAR* Suffix)
    {
        const FString IconPath = Icon;
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 2, 0)
        [ SNew(STextBlock).Text(FText::AsNumber(Value)).Font(FWowWindowStyle::TitleFont(13))
          .ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ];
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
        [
            SNew(SBox).WidthOverride(14).HeightOverride(14)
            [
                SNew(SOverlay)
                + SOverlay::Slot()[ SNew(SImage).Image_Lambda([this, IconPath]() { return LoadBrush(IconPath); }) ]
                + SOverlay::Slot()
                [ SNew(STextBlock).Text(FText::FromString(Suffix)).Font(FWowWindowStyle::TitleFont(11))
                  .ColorAndOpacity(ParchmentText())
                  .Visibility_Lambda([this, IconPath]() { return LoadBrush(IconPath) ? EVisibility::Collapsed : EVisibility::HitTestInvisible; }) ]
            ]
        ];
    };
    if (Copper >= 10000) Add(Copper / 10000, TEXT("Interface\\MoneyFrame\\UI-GoldIcon"), TEXT("g"));
    if (Copper >= 100) Add(Copper / 100 % 100, TEXT("Interface\\MoneyFrame\\UI-SilverIcon"), TEXT("s"));
    if (Copper % 100 || Copper < 100) Add(Copper % 100, TEXT("Interface\\MoneyFrame\\UI-CopperIcon"), TEXT("c"));
    return Row;
}

TSharedRef<SWidget> SWowQuestLog::CreateQuestDetailPanel()
{
    const FWowQuestLogEntry* Selected = GetSelectedQuest();
    if (!Selected)
    {
        return SNew(STextBlock).Text(LOCTEXT("NoQuestSelected", "No quest selected"))
            .ColorAndOpacity(ParchmentText()).Font(FWowWindowStyle::TitleFont(13)).Justification(ETextJustify::Center);
    }
    const FWowQuestLogEntry& QuestEntry = *Selected;
    const FString Objectives = ExpandForPlayer(QuestEntry.ObjectiveText);
    const FString Description = ExpandForPlayer(QuestEntry.Details);

    TSharedRef<SVerticalBox> Page = SNew(SVerticalBox);
    // Title (QuestTitleFont), then the objective summary and objective lines,
    // "Description" and the story text, then "Rewards" - the 3.3.5 page order.
    Page->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
    [
        SNew(STextBlock).Text(FText::FromString(GetQuestName(QuestEntry)))
        .Font(FWowNativeWidgetStyle::Heading(20)).ColorAndOpacity(ParchmentTitle()).AutoWrapText(true)
    ];
    if (QuestEntry.State != 0)
    {
        Page->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
        [ BodyText(QuestEntry.State == 1 ? TEXT("- Complete -") : TEXT("- Failed -"),
            QuestEntry.State == 1 ? ParchmentText() : Srgb(140, 0, 0)) ];
    }
    if (!Objectives.IsEmpty())
    {
        Page->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ BodyText(Objectives) ];
    }
    Page->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[ CreateObjectivesWidget(QuestEntry) ];
    if (!Description.IsEmpty())
    {
        Page->AddSlot().AutoHeight().Padding(0, 4, 0, 4)[ SectionHeading(LOCTEXT("Description", "Description")) ];
        Page->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[ BodyText(Description) ];
    }
    Page->AddSlot().AutoHeight()[ CreateRewardsWidget(QuestEntry) ];
    return Page;
}

TSharedRef<SWidget> SWowQuestLog::CreateObjectivesWidget(const FWowQuestLogEntry& QuestEntry) const
{
    TSharedRef<SVerticalBox> ObjectivesBox = SNew(SVerticalBox);
    for (int32 i = 0; i < QuestEntry.Objectives.Num(); ++i)
    {
        const FWowQuestObjective& Objective = QuestEntry.Objectives[i];
        if (Objective.Required == 0) continue;

        FString ObjectiveLabel = Objective.Text;
        if (ObjectiveLabel.IsEmpty())
        {
            const auto* Item = PacketHandler->ItemTemplates.Find(Objective.ItemId);
            ObjectiveLabel = Item ? Item->Name : Objective.ItemId ? TEXT("Loading item...")
                : FString::Printf(TEXT("Objective %d"), i + 1);
        }
        const bool bDone = Objective.Count >= Objective.Required;
        const FString Line = FString::Printf(TEXT("- %s: %u/%u%s"), *ObjectiveLabel,
            FMath::Min(Objective.Count, Objective.Required), Objective.Required, bDone ? TEXT(" (Complete)") : TEXT(""));
        ObjectivesBox->AddSlot().AutoHeight().Padding(6, 1, 0, 1)[ BodyText(Line, bDone ? ParchmentDone() : ParchmentText()) ];
    }
    return ObjectivesBox;
}

TSharedRef<SWidget> SWowQuestLog::CreateRewardsWidget(const FWowQuestLogEntry& QuestEntry)
{
    const bool bAny = !QuestEntry.RewardItems.IsEmpty() || !QuestEntry.RewardChoiceItems.IsEmpty()
        || QuestEntry.RewardMoney > 0 || QuestEntry.RewardSpell != 0;
    if (!bAny) return SNullWidget::NullWidget;

    auto ItemGrid = [this](const TArray<TPair<uint32, uint32>>& Items)
    {
        TSharedRef<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4));
        for (const auto& Reward : Items) Grid->AddSlot()[ CreateRewardCard(Reward.Key, Reward.Value) ];
        return Grid;
    };

    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
    Box->AddSlot().AutoHeight().Padding(0, 4, 0, 4)[ SectionHeading(LOCTEXT("Rewards", "Rewards")) ];
    if (!QuestEntry.RewardChoiceItems.IsEmpty())
    {
        Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
        [ BodyText(TEXT("You will be able to choose one of these rewards:")) ];
        Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ ItemGrid(QuestEntry.RewardChoiceItems) ];
    }
    if (!QuestEntry.RewardItems.IsEmpty())
    {
        Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
        [ BodyText(QuestEntry.RewardChoiceItems.IsEmpty() ? TEXT("You will receive:") : TEXT("You will also receive:")) ];
        Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ ItemGrid(QuestEntry.RewardItems) ];
    }
    if (QuestEntry.RewardSpell)
    {
        const FSpellDbcEntry* Spell = FDbcStore::Get().Spells().GetById(QuestEntry.RewardSpell);
        const FSpellIconDbcEntry* Icon = Spell ? FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID) : nullptr;
        const FSlateBrush* IconBrush = Icon && !Icon->TexturePath.IsEmpty() ? LoadBrush(Icon->TexturePath) : nullptr;
        Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[ BodyText(TEXT("You will learn:")) ];
        Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [ SNew(SBox).WidthOverride(36).HeightOverride(36).Visibility(IconBrush ? EVisibility::Visible : EVisibility::Collapsed)
              [ SNew(SImage).Image(IconBrush) ] ]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [ BodyText(Spell ? Spell->SpellName : FString::Printf(TEXT("Spell %u"), QuestEntry.RewardSpell), ParchmentTitle()) ]
        ];
    }
    if (QuestEntry.RewardMoney > 0)
    {
        Box->AddSlot().AutoHeight().Padding(0, 2, 0, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
            [ BodyText(QuestEntry.RewardItems.IsEmpty() && QuestEntry.RewardChoiceItems.IsEmpty()
                ? TEXT("You will receive:") : TEXT("You will also receive:")) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ CreateMoneyWidget(QuestEntry.RewardMoney) ]
        ];
    }
    return Box;
}

TSharedRef<SWidget> SWowQuestLog::CreateBottomBar()
{
    const FWowQuestLogEntry* Selected = GetSelectedQuest();

    // Abandon 110 at LEFT 0,1; Track 100 at RIGHT -3,1; Share fills the gap (QuestLogFrame.xml).
    const bool bHasQuest = Selected != nullptr;
    const bool bTracked = Selected && Selected->bIsTracked;
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ PanelButton(LOCTEXT("Abandon", "Abandon"), 110, 21, FOnClicked::CreateSP(this, &SWowQuestLog::OnAbandonClicked), bHasQuest) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ PanelButton(LOCTEXT("Share", "Share"), 94, 21, FOnClicked::CreateSP(this, &SWowQuestLog::OnShareClicked), bHasQuest) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3 * Scale, 0)
        [ PanelButton(bTracked ? LOCTEXT("Untrack", "Untrack") : LOCTEXT("Track", "Track"), 100, 21,
            FOnClicked::CreateSP(this, &SWowQuestLog::OnTrackClicked), bHasQuest) ];
}

FString SWowQuestLog::GetQuestName(const FWowQuestLogEntry& QuestEntry) const
{
    if (!QuestEntry.Title.IsEmpty())
    {
        return QuestEntry.Title;
    }
    if (PacketHandler)
    {
        const FString CachedName = PacketHandler->QuestTitleCache.FindRef(QuestEntry.QuestId);
        if (!CachedName.IsEmpty())
        {
            return CachedName;
        }
    }
    return FString::Printf(TEXT("Quest %u"), QuestEntry.QuestId);
}

FString SWowQuestLog::GetQuestZoneName(const FWowQuestLogEntry& QuestEntry) const
{
    return QuestEntry.ZoneName.IsEmpty() ? TEXT("Miscellaneous") : QuestEntry.ZoneName;
}

void SWowQuestLog::ToggleVisibility()
{
    if (CurrentVisibility == EVisibility::Hidden)
    {
        CurrentVisibility = EVisibility::Visible;
        SetVisibility(CurrentVisibility);
        UpdateQuestList();
    }
    else
    {
        CurrentVisibility = EVisibility::Hidden;
        SetVisibility(CurrentVisibility);
    }
}

bool SWowQuestLog::IsVisible() const
{
    return CurrentVisibility != EVisibility::Hidden;
}

uint32 SWowQuestLog::GetSelectedQuestId() const
{
    const FWowQuestLogEntry* Quest = GetSelectedQuest();
    return Quest ? Quest->QuestId : 0;
}

void SWowQuestLog::ShowQuest(uint32 QuestId)
{
    if (!PacketHandler) return;
    const int32 Index = PacketHandler->QuestLog.IndexOfByPredicate([QuestId](const FWowQuestLogEntry& Q) { return Q.QuestId == QuestId; });
    if (Index == INDEX_NONE) return;
    const FWowQuestLogEntry& Quest = PacketHandler->QuestLog[Index];
    CollapsedZones.Remove(GetQuestZoneName(Quest));
    SelectedQuestIndex = Index;
    if (!IsVisible()) ToggleVisibility();
    UpdateQuestList();
}

FReply SWowQuestLog::OnQuestSelected(int32 QuestIndex)
{
    if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
    {
        ToggleQuestWatch(QuestIndex);
    }
    SelectedQuestIndex = QuestIndex;
    UpdateQuestList();
    return FReply::Handled();
}

FReply SWowQuestLog::OnZoneHeaderClicked(FString ZoneName)
{
    if (CollapsedZones.Contains(ZoneName)) CollapsedZones.Remove(ZoneName);
    else CollapsedZones.Add(MoveTemp(ZoneName));
    UpdateQuestList();
    return FReply::Handled();
}

void SWowQuestLog::ToggleQuestWatch(int32 QuestIndex)
{
    if (!PacketHandler || !PacketHandler->QuestLog.IsValidIndex(QuestIndex))
    {
        return;
    }

    FWowQuestLogEntry& Quest = PacketHandler->QuestLog[QuestIndex];
    if (!Quest.bIsTracked)
    {
        int32 WatchedCount = 0;
        for (const FWowQuestLogEntry& Entry : PacketHandler->QuestLog)
        {
            WatchedCount += Entry.bIsTracked ? 1 : 0;
        }
        if (WatchedCount >= 25)
        {
            return;
        }
    }

    Quest.bIsTracked = !Quest.bIsTracked;
    PacketHandler->OnQuestWatchChanged.Broadcast();
    OnTrackedQuestsChanged.ExecuteIfBound();
}

FReply SWowQuestLog::OnCloseButtonClicked()
{
    CurrentVisibility = EVisibility::Hidden;
    SetVisibility(CurrentVisibility);
    PendingAbandonQuestId = 0;
    return FReply::Handled();
}

FReply SWowQuestLog::OnAbandonClicked()
{
    if (const FWowQuestLogEntry* Selected = GetSelectedQuest())
    {
        ShowAbandonPopup(Selected->QuestId);
    }
    return FReply::Handled();
}

void SWowQuestLog::ShowAbandonPopup(uint32 QuestId)
{
    // StaticPopupDialogs["ABANDON_QUEST"]: its own dialog at StaticPopup1's place (TOP 0,-135),
    // 320 wide, the question in white, Yes / No (Stefan 10.10: it was squeezed into the log's
    // bottom bar and did not fit).
    HideAbandonPopup();
    const FWowQuestLogEntry* Quest = PacketHandler ? PacketHandler->QuestLog.FindByPredicate(
        [QuestId](const FWowQuestLogEntry& Entry) { return Entry.QuestId == QuestId; }) : nullptr;
    if (!Quest) return;
    PendingAbandonQuestId = QuestId;
    AbandonPopupFrame = SNew(SBox).WidthOverride(320 * Scale)
        [
            SNew(SWowWindowFrame).Padding(FMargin(16 * Scale, 16 * Scale, 16 * Scale, 14 * Scale))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12 * Scale)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FString::Printf(TEXT("Abandon \"%s\"?"), *GetQuestName(*Quest))))
                    .Font(Fnt(12)).ColorAndOpacity(FLinearColor::White).AutoWrapText(true)
                    .Justification(ETextJustify::Center)
                    .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
                ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6 * Scale, 0)
                    [ PanelButton(LOCTEXT("Yes", "Yes"), 128, 21, FOnClicked::CreateSP(this, &SWowQuestLog::OnAbandonConfirmed)) ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [ PanelButton(LOCTEXT("No", "No"), 128, 21, FOnClicked::CreateLambda([this]()
                        { HideAbandonPopup(); return FReply::Handled(); })) ]
                ]
            ]
        ];
    AbandonPopup = SNew(SBox).Visibility(EVisibility::SelfHitTestInvisible)
        .HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0, 135 * Scale, 0, 0))
        [ AbandonPopupFrame.ToSharedRef() ];
    FWowUiSurfaces::Add(AbandonPopup.ToSharedRef(), EWowUiZone::Popups, 220, TEXT("NativeAbandonQuest"));
}

void SWowQuestLog::HideAbandonPopup()
{
    PendingAbandonQuestId = 0;
    if (AbandonPopup.IsValid())
    {
        const TSharedRef<SWidget> Popup = AbandonPopup.ToSharedRef();
        AbandonPopup.Reset();
        FWowUiSurfaces::Remove(Popup);
    }
}

bool SWowQuestLog::CancelAbandonPopup()
{
    if (!AbandonPopup.IsValid()) return false;
    HideAbandonPopup();
    return true;
}

FReply SWowQuestLog::OnAbandonConfirmed()
{
    const uint32 QuestId = PendingAbandonQuestId;
    HideAbandonPopup();
    if (!PacketHandler || !QuestId) return FReply::Handled();

    // The server owns the quest log: CMSG_QUESTLOG_REMOVE_QUEST takes the player-field
    // slot. The following field update rebuilds the list; remove locally for instant feedback.
    const int32 Slot = FindQuestLogSlot(QuestId);
    if (Slot != INDEX_NONE && PacketHandler->OnSendPacket.IsBound())
    {
        TArray<uint8> Data = {static_cast<uint8>(Slot)};
        PacketHandler->OnSendPacket.Execute(CMSG_QUESTLOG_REMOVE_QUEST, Data);
    }
    PacketHandler->QuestLog.RemoveAll([QuestId](const FWowQuestLogEntry& Entry) { return Entry.QuestId == QuestId; });
    OnTrackedQuestsChanged.ExecuteIfBound();
    PacketHandler->OnQuestLogUpdated.Broadcast();
    UpdateQuestList();
    return FReply::Handled();
}

FReply SWowQuestLog::OnShareClicked()
{
    const FWowQuestLogEntry* Selected = GetSelectedQuest();
    if (Selected && PacketHandler && PacketHandler->OnSendPacket.IsBound())
    {
        TArray<uint8> Data;
        Data.Append(reinterpret_cast<const uint8*>(&Selected->QuestId), 4);
        PacketHandler->OnSendPacket.Execute(CMSG_PUSHQUESTTOPARTY, Data);
    }
    return FReply::Handled();
}

FReply SWowQuestLog::OnTrackClicked()
{
    ToggleQuestWatch(SelectedQuestIndex);
    UpdateQuestList();
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
