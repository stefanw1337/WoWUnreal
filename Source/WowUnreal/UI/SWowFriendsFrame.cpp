#include "SWowFriendsFrame.h"
#include "WowColor.h"
#include "WowTextCentering.h"
#include "WowWindowStyle.h"
#include "WowRetainedSlateBrush.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowAudioManager.h"
#include "WowNetwork/Public/WowConnectionManager.h"
#include "WowNetwork/Public/WowPacketHandler.h"
#include "Formats/Dbc/DbcStore.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWowPanelScrollBar.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
// 3.3.5a opcodes used by the social window.
constexpr uint32 CMSG_WHO = 0x062;
constexpr uint32 CMSG_FRIEND_LIST = 0x066;
constexpr uint32 CMSG_ADD_FRIEND = 0x069;
constexpr uint32 CMSG_DEL_FRIEND = 0x06A;
constexpr uint32 CMSG_ADD_IGNORE = 0x06C;
constexpr uint32 CMSG_DEL_IGNORE = 0x06D;
constexpr uint32 CMSG_GUILD_INVITE = 0x082;
constexpr uint32 CMSG_JOIN_CHANNEL = 0x097;
constexpr uint32 CMSG_LEAVE_CHANNEL = 0x098;
constexpr uint32 CMSG_GROUP_RAID_CONVERT = 0x28E;

FLinearColor NormalFontColor()    { return WowColor::FromWow(1.f, .82f, 0.f); }
FLinearColor HighlightFontColor() { return FLinearColor(1.f, 1.f, 1.f); }
FLinearColor GrayFontColor()      { return WowColor::FromWow(.5f, .5f, .5f); }
FLinearColor GreenFontColor()     { return WowColor::FromWow(.1f, 1.f, .1f); }
FBox2f UV4(float U0, float U1, float V0, float V1) { return FBox2f(FVector2f(U0, V0), FVector2f(U1, V1)); }

void U32(TArray<uint8>& Data, uint32 Value) { Data.Append(reinterpret_cast<const uint8*>(&Value), 4); }
void U64(TArray<uint8>& Data, uint64 Value) { Data.Append(reinterpret_cast<const uint8*>(&Value), 8); }
void Str(TArray<uint8>& Data, const FString& Value)
{
    FTCHARToUTF8 Utf8(*Value);
    Data.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    Data.Add(0);
}
bool IsOnline(const FWowFriendInfo& F) { return (F.Status & 0x1) != 0; }
}

SWowFriendsFrame::~SWowFriendsFrame()
{
    if (ConnectionManager.IsValid())
    {
        ConnectionManager->PacketHandler.OnFriendListUpdated.Remove(FriendListHandle);
        ConnectionManager->PacketHandler.OnWhoListUpdated.Remove(WhoListHandle);
    }
}

void SWowFriendsFrame::Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager)
{
    ConnectionManager = InConnectionManager;
    UIManager = InArgs._UIManager;
    if (UIManager.IsValid() && UIManager->GetFrameManager() && UIManager->GetFrameManager()->GetUIScale() > 0.f)
        Scale = UIManager->GetFrameManager()->GetUIScale();
    if (ConnectionManager.IsValid())
    {
        FriendListHandle = ConnectionManager->PacketHandler.OnFriendListUpdated.AddSP(this, &SWowFriendsFrame::Rebuild);
        WhoListHandle = ConnectionManager->PacketHandler.OnWhoListUpdated.AddSP(this, &SWowFriendsFrame::Rebuild);
    }
    ChildSlot
    [
        SAssignNew(Root, SBox)
        .WidthOverride_Lambda([this]() { return FrameW * Scale; })
        .HeightOverride_Lambda([this]() { return FrameH * Scale; })
    ];
    SetVisibility(EVisibility::Collapsed);
}

// ── Visibility ────────────────────────────────────────────────────────────

void SWowFriendsFrame::Toggle()
{
    if (bVisible) Close();
    else Open();
}

void SWowFriendsFrame::Open(TOptional<ETab> InTab)
{
    if (InTab.IsSet()) Tab = InTab.GetValue();
    if (!bVisible) PlayUiSound(TEXT("igCharacterInfoOpen"));
    bVisible = true;
    Popup = EPopup::None;
    bContextOpen = false;
    SetVisibility(EVisibility::SelfHitTestInvisible);
    RequestContacts();
    Rebuild();
}

void SWowFriendsFrame::Close()
{
    if (!bVisible) return;
    bVisible = false;
    Popup = EPopup::None;
    bContextOpen = false;
    SetVisibility(EVisibility::Collapsed);
    PlayUiSound(TEXT("igCharacterInfoClose"));
}

void SWowFriendsFrame::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    if (!bVisible) return;
    bool bDirty = false;
    if (UIManager.IsValid() && UIManager->GetFrameManager())
    {
        const float NewScale = UIManager->GetFrameManager()->GetUIScale();
        if (NewScale > 0.f && !FMath::IsNearlyEqual(NewScale, Scale)) { Scale = NewScale; bDirty = true; }
    }
    const uint32 Stamp = StateStamp();
    if (Stamp != LastStamp) { LastStamp = Stamp; bDirty = true; }
    if (bDirty) Rebuild();
    if (bFocusPending && FocusField.IsValid())
    {
        bFocusPending = false;
        FSlateApplication::Get().SetKeyboardFocus(FocusField, EFocusCause::SetDirectly);
    }
}

uint32 SWowFriendsFrame::StateStamp() const
{
    const UWowConnectionManager* C = ConnectionManager.Get();
    if (!C) return 0;
    const auto& H = C->PacketHandler;
    uint32 Stamp = GetTypeHash(H.FriendsList.Num());
    for (const FWowFriendInfo& F : H.FriendsList)
        Stamp = HashCombine(Stamp, HashCombine(GetTypeHash(F.Guid), GetTypeHash(F.Status * 1000003u + F.AreaId * 31u + F.Level + F.Flags * 7u)));
    for (const FWowFriendInfo& F : H.FriendsList)
        if (F.Name.IsEmpty()) Stamp = HashCombine(Stamp, GetTypeHash(ContactName(F.Guid, FString())));
    Stamp = HashCombine(Stamp, GetTypeHash(H.GuildRoster.Num()));
    for (const FWowGuildMember& M : H.GuildRoster) Stamp = HashCombine(Stamp, GetTypeHash(M.Status * 131u + M.ZoneId + M.Level * 7u));
    Stamp = HashCombine(Stamp, GetTypeHash(H.JoinedChannels.Num()));
    for (const FString& Channel : H.JoinedChannels) Stamp = HashCombine(Stamp, GetTypeHash(Channel));
    Stamp = HashCombine(Stamp, GetTypeHash(H.GroupInfo.Members.Num() * 3 + H.GroupInfo.GroupType));
    Stamp = HashCombine(Stamp, GetTypeHash(H.GroupInfo.LeaderGuid));
    return Stamp;
}

void SWowFriendsFrame::PlayUiSound(const TCHAR* Sound) const
{
    UWorld* World = UIManager.IsValid() ? UIManager->GetWorld() : nullptr;
    if (AWowAudioManager* Audio = World ? Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(World, AWowAudioManager::StaticClass())) : nullptr)
        Audio->PlayUiSound(Sound);
}

// ── Data ──────────────────────────────────────────────────────────────────

FString SWowFriendsFrame::ContactName(uint64 Guid, const FString& Fallback) const
{
    if (!Fallback.IsEmpty()) return Fallback;
    if (const UWowConnectionManager* C = ConnectionManager.Get())
        if (const FString* Cached = C->PacketHandler.PlayerNameCache.Find(Guid)) return *Cached;
    return TEXT("Unknown");
}

FString SWowFriendsFrame::ClassName(uint32 ClassId)
{
    static const TCHAR* Classes[] = {TEXT(""), TEXT("Warrior"), TEXT("Paladin"), TEXT("Hunter"), TEXT("Rogue"),
        TEXT("Priest"), TEXT("Death Knight"), TEXT("Shaman"), TEXT("Mage"), TEXT("Warlock"), TEXT(""), TEXT("Druid")};
    return ClassId < UE_ARRAY_COUNT(Classes) ? Classes[ClassId] : TEXT("");
}

FLinearColor SWowFriendsFrame::ClassColor(uint32 ClassId)
{
    // RAID_CLASS_COLORS
    switch (ClassId)
    {
    case 1: return WowColor::FromWow(.78f, .61f, .43f);
    case 2: return WowColor::FromWow(.96f, .55f, .73f);
    case 3: return WowColor::FromWow(.67f, .83f, .45f);
    case 4: return WowColor::FromWow(1.f, .96f, .41f);
    case 5: return FLinearColor(1.f, 1.f, 1.f);
    case 6: return WowColor::FromWow(.77f, .12f, .23f);
    case 7: return WowColor::FromWow(0.f, .44f, .87f);
    case 8: return WowColor::FromWow(.41f, .8f, .94f);
    case 9: return WowColor::FromWow(.58f, .51f, .79f);
    case 11: return WowColor::FromWow(1.f, .49f, .04f);
    default: return HighlightFontColor();
    }
}

FString SWowFriendsFrame::ZoneName(uint32 AreaId)
{
    const auto* Area = AreaId ? FDbcStore::Get().AreaTable().GetById(AreaId) : nullptr;
    return Area ? Area->Name : FString();
}

// ── Actions ───────────────────────────────────────────────────────────────

void SWowFriendsFrame::SetTab(ETab NewTab)
{
    if (Tab == NewTab) return;
    Tab = NewTab;
    Popup = EPopup::None;
    bContextOpen = false;
    PlayUiSound(TEXT("igCharacterInfoTab"));
    Rebuild();
}

void SWowFriendsFrame::RequestContacts()
{
    if (UWowConnectionManager* C = ConnectionManager.Get())
    {
        TArray<uint8> Data; U32(Data, 0x1 | 0x2); // friends + ignored
        C->SendRawPacket(CMSG_FRIEND_LIST, Data);
    }
}

void SWowFriendsFrame::SendWho(const FString& Filter)
{
    UWowConnectionManager* C = ConnectionManager.Get();
    if (!C) return;
    // Same parsing as SendWho(): "10-20" is a level range, a number one level, the rest a name.
    uint32 LevelMin = 0, LevelMax = 100;
    FString Name;
    TArray<FString> Words;
    Filter.ParseIntoArrayWS(Words);
    for (const FString& Word : Words)
    {
        FString A, B;
        if (Word.Split(TEXT("-"), &A, &B) && A.IsNumeric() && B.IsNumeric()) { LevelMin = FCString::Atoi(*A); LevelMax = FCString::Atoi(*B); }
        else if (Word.IsNumeric()) { LevelMin = LevelMax = FCString::Atoi(*Word); }
        else if (Name.IsEmpty()) { Name = Word.Replace(TEXT("n-"), TEXT("")).Replace(TEXT("\""), TEXT("")); }
    }
    TArray<uint8> Data;
    U32(Data, LevelMin); U32(Data, LevelMax);
    Str(Data, Name); Str(Data, FString());
    U32(Data, 0xFFFFFFFFu); U32(Data, 0xFFFFFFFFu);
    U32(Data, 0); U32(Data, 0);
    C->PacketHandler.bWhoForFrame = true;
    C->SendRawPacket(CMSG_WHO, Data);
    SelectedWho = INDEX_NONE;
}

void SWowFriendsFrame::AddFriend(const FString& Name)
{
    if (UWowConnectionManager* C = ConnectionManager.Get(); C && !Name.IsEmpty())
    {
        TArray<uint8> Data; Str(Data, Name); Str(Data, FString());
        C->SendRawPacket(CMSG_ADD_FRIEND, Data);
    }
}

void SWowFriendsFrame::RemoveFriend(uint64 Guid)
{
    if (UWowConnectionManager* C = ConnectionManager.Get(); C && Guid)
    {
        TArray<uint8> Data; U64(Data, Guid);
        C->SendRawPacket(CMSG_DEL_FRIEND, Data);
    }
}

void SWowFriendsFrame::AddIgnore(const FString& Name)
{
    if (UWowConnectionManager* C = ConnectionManager.Get(); C && !Name.IsEmpty())
    {
        TArray<uint8> Data; Str(Data, Name);
        C->SendRawPacket(CMSG_ADD_IGNORE, Data);
    }
}

void SWowFriendsFrame::RemoveIgnore(uint64 Guid)
{
    if (UWowConnectionManager* C = ConnectionManager.Get(); C && Guid)
    {
        TArray<uint8> Data; U64(Data, Guid);
        C->SendRawPacket(CMSG_DEL_IGNORE, Data);
    }
}

void SWowFriendsFrame::JoinChannel(const FString& Name)
{
    if (UWowConnectionManager* C = ConnectionManager.Get(); C && !Name.IsEmpty())
    {
        TArray<uint8> Data; U32(Data, 0); Data.Add(0); Data.Add(0); Str(Data, Name); Str(Data, FString());
        C->SendRawPacket(CMSG_JOIN_CHANNEL, Data);
    }
}

void SWowFriendsFrame::LeaveChannel(const FString& Name)
{
    if (UWowConnectionManager* C = ConnectionManager.Get(); C && !Name.IsEmpty())
    {
        TArray<uint8> Data; U32(Data, 0); Str(Data, Name);
        C->SendRawPacket(CMSG_LEAVE_CHANNEL, Data);
    }
}

void SWowFriendsFrame::InvitePlayer(const FString& Name)
{
    if (UWowConnectionManager* C = ConnectionManager.Get(); C && !Name.IsEmpty()) C->SendGroupInviteByName(Name);
}

void SWowFriendsFrame::Whisper(const FString& Name)
{
    UE_LOG(LogTemp, Log, TEXT("[Friends] whisper %s (handler %d)"), *Name, OnWhisper ? 1 : 0);
    if (!Name.IsEmpty() && OnWhisper) OnWhisper(Name);
}

void SWowFriendsFrame::OpenPopup(EPopup InPopup)
{
    Popup = InPopup;
    PopupText.Reset();
    bContextOpen = false;
    bFocusPending = true;
    Rebuild();
}

void SWowFriendsFrame::AcceptPopup()
{
    const FString Text = PopupText.TrimStartAndEnd();
    switch (Popup)
    {
    case EPopup::AddFriend: AddFriend(Text); break;
    case EPopup::AddIgnore: AddIgnore(Text); break;
    case EPopup::JoinChannel: JoinChannel(Text); break;
    case EPopup::GuildInvite:
        if (UWowConnectionManager* C = ConnectionManager.Get(); C && !Text.IsEmpty())
        { TArray<uint8> Data; Str(Data, Text); C->SendRawPacket(CMSG_GUILD_INVITE, Data); }
        break;
    default: break;
    }
    Popup = EPopup::None;
    FSlateApplication::Get().SetAllUserFocusToGameViewport();
    Rebuild();
}

void SWowFriendsFrame::OpenContext(const FString& Name, uint64 Guid, bool bFriend, FVector2D LocalPos)
{
    ContextName = Name;
    ContextGuid = Guid;
    bContextFriend = bFriend;
    ContextPos = LocalPos / FMath::Max(Scale, .01f);
    bContextOpen = !Name.IsEmpty();
    Rebuild();
}

// ── Widget helpers ────────────────────────────────────────────────────────

FSlateFontInfo SWowFriendsFrame::Fnt(float WowPixels) const
{
    return FWowWindowStyle::TitleFont(FMath::Max(6, FMath::RoundToInt(WowPixels * Scale * .75f)));
}

float SWowFriendsFrame::TextWidth(const FString& Text, const FSlateFontInfo& Font) const
{
    if (!FSlateApplication::IsInitialized()) return Text.Len() * Font.Size * .6f;
    return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font).X;
}

void SWowFriendsFrame::Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const
{
    Canvas.AddSlot().Position(FVector2D(X, Y) * Scale).Size(FVector2D(W, H) * Scale)[Widget];
}

const FSlateBrush* SWowFriendsFrame::LoadBrush(const FString& TexturePath, FBox2f UV) const
{
    if (TexturePath.IsEmpty()) return nullptr;
    const FString Key = FString::Printf(TEXT("%s|%.4f,%.4f,%.4f,%.4f"), *TexturePath, UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y);
    if (const TSharedPtr<FWowRetainedSlateBrush>* Existing = TextureBrushes.Find(Key)) return Existing->Get();
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
    Brush->ImageSize = FVector2D(Texture->GetSizeX() * (UV.Max.X - UV.Min.X), Texture->GetSizeY() * (UV.Max.Y - UV.Min.Y));
    TextureBrushes.Add(Key, Brush);
    return Brush.Get();
}

const FButtonStyle* SWowFriendsFrame::TextureButtonStyle(const FString& Up, const FString& Down) const
{
    const FString Key = Up + TEXT("|") + Down;
    if (const TSharedPtr<FButtonStyle>* Found = ButtonStyles.Find(Key)) return Found->Get();
    const FSlateBrush* Normal = LoadBrush(Up);
    if (!Normal) return nullptr;
    const FSlateBrush* Pressed = Down.IsEmpty() ? nullptr : LoadBrush(Down);
    TSharedPtr<FButtonStyle> Style = MakeShared<FButtonStyle>(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"));
    Style->SetNormal(*Normal);
    FSlateBrush Hovered = *Normal;
    Hovered.TintColor = WowColor::FromWow(1.2f, 1.2f, 1.2f);
    Style->SetHovered(Hovered);
    Style->SetPressed(Pressed ? *Pressed : *Normal);
    Style->SetDisabled(*Normal);
    Style->SetNormalPadding(FMargin(0));
    Style->SetPressedPadding(FMargin(0));
    ButtonStyles.Add(Key, Style);
    return Style.Get();
}

TSharedRef<SWidget> SWowFriendsFrame::Img(const FString& Path, FBox2f UV, FLinearColor Tint) const
{
    return SNew(SImage).Visibility(EVisibility::HitTestInvisible).ColorAndOpacity(Tint)
        .Image_Lambda([this, Path, UV]() { return LoadBrush(Path, UV); });
}

TSharedRef<SWidget> SWowFriendsFrame::Label(const FString& Text, float WowPixels, FLinearColor Color, ETextJustify::Type Justify) const
{
    return SNew(STextBlock).Text(FText::FromString(Text)).Font(Fnt(WowPixels)).ColorAndOpacity(Color).Justification(Justify)
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis).Visibility(EVisibility::HitTestInvisible)
        .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black);
}

TSharedRef<SWidget> SWowFriendsFrame::PanelButton(const FString& Text, float W, float H, TFunction<void()> OnClick, bool bEnabled) const
{
    // UIPanelButtonTemplate: red button art, GameFontNormal (grey when disabled).
    return SNew(SBox).WidthOverride(W * Scale).HeightOverride(H * Scale)
        [
            SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center)
            .ContentPadding(FMargin(2 * Scale, 0)).IsEnabled(bEnabled).ClickMethod(EButtonClickMethod::MouseDown)
            .OnClicked_Lambda([OnClick]() { if (OnClick) OnClick(); return FReply::Handled(); })
            [ SNew(STextBlock).Text(FText::FromString(Text)).Font(Fnt(12)).OverflowPolicy(ETextOverflowPolicy::Clip).RenderTransform(WowCapCenterTransform(Fnt(12)))
              .ColorAndOpacity(bEnabled ? NormalFontColor() : GrayFontColor())
              .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
        ];
}

TSharedRef<SWidget> SWowFriendsFrame::InputBorder() const
{
    // Common-Input-Border: 8px caps (TexCoords 0-.0625 / .9375-1, height .625).
    return SNew(SHorizontalBox).Visibility(EVisibility::HitTestInvisible)
        + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(8 * Scale)
            [ Img(TEXT("Interface\\Common\\Common-Input-Border"), UV4(0, .0625f, 0, .625f)) ] ]
        + SHorizontalBox::Slot().FillWidth(1)[ Img(TEXT("Interface\\Common\\Common-Input-Border"), UV4(.0625f, .9375f, 0, .625f)) ]
        + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(8 * Scale)
            [ Img(TEXT("Interface\\Common\\Common-Input-Border"), UV4(.9375f, 1, 0, .625f)) ] ];
}

TSharedRef<SWidget> SWowFriendsFrame::Row(TFunction<void(const FPointerEvent&, FVector2D)> OnClick, bool bSelected, TSharedRef<SWidget> Content) const
{
    // List button: UI-FriendsFrame-HighlightBar (ADD) while selected or hovered.
    TSharedRef<TWeakPtr<SBorder>> Holder = MakeShared<TWeakPtr<SBorder>>();
    TSharedPtr<SBorder> Border;
    SAssignNew(Border, SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
        .OnMouseButtonDown_Lambda([this, OnClick](const FGeometry& Geometry, const FPointerEvent& Event)
        {
            if (OnClick)
            {
                const TSharedPtr<SWidget> RootWidget = Root;
                const FVector2D Local = RootWidget.IsValid()
                    ? RootWidget->GetCachedGeometry().AbsoluteToLocal(Event.GetScreenSpacePosition()) : FVector2D::ZeroVector;
                OnClick(Event, Local);
            }
            return FReply::Handled();
        })
        [
            SNew(SOverlay)
            + SOverlay::Slot()
            [
                SNew(SWowAddImage)
                .Image_Lambda([this]() { return LoadBrush(TEXT("Interface\\FriendsFrame\\UI-FriendsFrame-HighlightBar")); })
                .ColorAndOpacity_Lambda([Holder, bSelected]()
                {
                    const TSharedPtr<SBorder> B = Holder->Pin();
                    return FLinearColor(1, 1, 1, bSelected ? 1.f : B.IsValid() && B->IsHovered() ? .5f : 0.f);
                })
            ]
            + SOverlay::Slot()[ Content ]
        ];
    *Holder = Border;
    return Border.ToSharedRef();
}

// ── Layout ────────────────────────────────────────────────────────────────

void SWowFriendsFrame::Rebuild()
{
    if (!Root.IsValid()) return;
    FocusField.Reset();
    TSharedRef<SCanvas> Canvas = SNew(SCanvas);
    BuildArt(*Canvas);
    switch (Tab)
    {
    case ETab::Friends: if (bIgnoreList) BuildIgnore(*Canvas); else BuildFriends(*Canvas); break;
    case ETab::Who: BuildWho(*Canvas); break;
    case ETab::Guild: BuildGuild(*Canvas); break;
    case ETab::Chat: BuildChat(*Canvas); break;
    case ETab::Raid: BuildRaid(*Canvas); break;
    }
    BuildTabs(*Canvas);
    // UIPanelCloseButton at TOPRIGHT -30,-8.
    const FButtonStyle* CloseStyle = TextureButtonStyle(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"),
        TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Down"));
    Place(*Canvas, FrameW - 30 - 32, 8, 32, 32,
        SNew(SButton).ButtonStyle(CloseStyle ? CloseStyle : &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
        .ContentPadding(0).ClickMethod(EButtonClickMethod::MouseDown)
        .OnClicked_Lambda([this]() { Close(); return FReply::Handled(); }));
    if (Popup != EPopup::None) BuildPopup(*Canvas);
    if (bContextOpen) BuildContextMenu(*Canvas);
    Root->SetContent(Canvas);
    Root->SlatePrepass(GetCachedGeometry().Scale > 0.f ? GetCachedGeometry().Scale : 1.f);
}

void SWowFriendsFrame::BuildArt(SCanvas& Canvas) const
{
    // FriendsFrame_Update swaps the four quadrants per tab.
    FString TL, TR, BL, BR;
    switch (Tab)
    {
    case ETab::Friends:
        TL = TEXT("Interface\\FriendsFrame\\UI-FriendsFrame-TopLeft-bnet");
        TR = TEXT("Interface\\FriendsFrame\\UI-FriendsFrame-TopRight-bnet");
        BL = bIgnoreList ? TEXT("Interface\\FriendsFrame\\UI-FriendsFrameMute-BotLeft-bnet") : TEXT("Interface\\FriendsFrame\\UI-FriendsFrame-BotLeft-bnet");
        BR = bIgnoreList ? TEXT("Interface\\FriendsFrame\\UI-FriendsFrameMute-BotRight-bnet") : TEXT("Interface\\FriendsFrame\\UI-FriendsFrame-BotRight-bnet");
        break;
    case ETab::Who:
        TL = TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-TopLeft");
        TR = TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-TopRight");
        BL = TEXT("Interface\\FriendsFrame\\WhoFrame-BotLeft");
        BR = TEXT("Interface\\FriendsFrame\\WhoFrame-BotRight");
        break;
    case ETab::Guild:
        if (!ConnectionManager.IsValid() || ConnectionManager->PacketHandler.GuildName.IsEmpty())
        {
            // Not in a guild: plain panel art, without the roster columns and the two button
            // recesses of GuildFrame (Stefan: "why can I see their rooms when I'm not in a guild").
            TL = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-TopLeft");
            TR = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-TopRight");
            BL = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-BottomLeft");
            BR = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-BottomRight");
            break;
        }
        TL = TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-TopLeft");
        TR = TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-TopRight");
        BL = TEXT("Interface\\FriendsFrame\\GuildFrame-BotLeft");
        BR = TEXT("Interface\\FriendsFrame\\GuildFrame-BotRight");
        break;
    case ETab::Chat:
        TL = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-TopLeft");
        TR = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-TopRight");
        BL = TEXT("Interface\\FriendsFrame\\UI-ChannelFrame-BotLeft");
        BR = TEXT("Interface\\FriendsFrame\\UI-ChannelFrame-BotRight");
        break;
    case ETab::Raid:
        TL = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-TopLeft");
        TR = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-TopRight");
        BL = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-BottomLeft");
        BR = TEXT("Interface\\PaperDollInfoFrame\\UI-Character-General-BottomRight");
        break;
    }
    // Pre-Battle.net art when a "-bnet" quadrant is missing from the client data.
    auto Art = [this](const FString& Path) -> FString
    {
        if (LoadBrush(Path) || !Path.EndsWith(TEXT("-bnet"))) return Path;
        return Path.LeftChop(5);
    };
    Place(Canvas, 7, 6, 60, 60, Img(TEXT("Interface\\FriendsFrame\\FriendsFrameScrollIcon")));
    Place(Canvas, 0, 0, 256, 256, Img(Art(TL)));
    Place(Canvas, 256, 0, 128, 256, Img(Art(TR)));
    Place(Canvas, 0, 256, 256, 256, Img(Art(BL)));
    Place(Canvas, 256, 256, 128, 256, Img(Art(BR)));
    static const TCHAR* Titles[] = {TEXT("Friends List"), TEXT("Who List"), TEXT("Guild"), TEXT("Chat Channels"), TEXT("Raid")};
    FString Title = Titles[uint8(Tab)];
    if (Tab == ETab::Friends && bIgnoreList) Title = TEXT("Ignore List");
    if (Tab == ETab::Guild && ConnectionManager.IsValid() && !ConnectionManager->PacketHandler.GuildName.IsEmpty())
        Title = ConnectionManager->PacketHandler.GuildName;
    Place(Canvas, 70, 16, 250, 16, WowTitleBox(Label(Title, 12, NormalFontColor(), ETextJustify::Center), Fnt(12)));
}

void SWowFriendsFrame::BuildTabs(SCanvas& Canvas)
{
    // FriendsFrameTab1..5 (CharacterFrameTabButtonTemplate): BOTTOMLEFT 11,46, each -15 over the previous.
    static const TCHAR* Names[] = {TEXT("Friends"), TEXT("Who"), TEXT("Guild"), TEXT("Chat"), TEXT("Raid")};
    float X = 11.f;
    const float Y = FrameH - 46.f - 32.f;
    const FSlateFontInfo TabFont = Fnt(10);
    for (int32 Index = 0; Index < 5; ++Index)
    {
        const bool bSelected = uint8(Tab) == Index;
        const float W = FMath::Max(64.f, TextWidth(Names[Index], TabFont) / Scale + 40.f);
        const FString Art = bSelected ? TEXT("Interface\\PaperDollInfoFrame\\UI-Character-ActiveTab") : TEXT("Interface\\PaperDollInfoFrame\\UI-Character-InActiveTab");
        // CharacterFrameTabButtonTemplate: the active art sits 5 higher; the text stays put.
        const float ArtY = bSelected ? Y - 5.f : Y;
        Place(Canvas, X, ArtY, 20, 32, Img(Art, UV4(0.f, .15625f, 0.f, 1.f)));
        Place(Canvas, X + 20, ArtY, W - 40, 32, Img(Art, UV4(.15625f, .84375f, 0.f, 1.f)));
        Place(Canvas, X + W - 20, ArtY, 20, 32, Img(Art, UV4(.84375f, 1.f, 0.f, 1.f)));
        Place(Canvas, X, Y + 6, W, 16, Label(Names[Index], 10, bSelected ? HighlightFontColor() : NormalFontColor(), ETextJustify::Center));
        const ETab TabValue = ETab(Index);
        Place(Canvas, X + 6, Y, W - 12, 32, SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
            .OnMouseButtonDown_Lambda([this, TabValue](const FGeometry&, const FPointerEvent& Event)
            {
                if (Event.GetEffectingButton() == EKeys::LeftMouseButton) SetTab(TabValue);
                return FReply::Handled();
            }));
        X += W - 15.f;
    }
}

void SWowFriendsFrame::BuildFriends(SCanvas& Canvas)
{
    const UWowConnectionManager* C = ConnectionManager.Get();
    TArray<const FWowFriendInfo*> Friends;
    if (C) for (const FWowFriendInfo& F : C->PacketHandler.FriendsList) if (F.Flags & 0x1) Friends.Add(&F);
    // Online first, then by name (FriendsList_Update sorts online friends to the top).
    Friends.StableSort([this](const FWowFriendInfo& A, const FWowFriendInfo& B)
    {
        if (IsOnline(A) != IsOnline(B)) return IsOnline(A);
        return ContactName(A.Guid, A.Name) < ContactName(B.Guid, B.Name);
    });
    int32 Online = 0;
    for (const FWowFriendInfo* F : Friends) Online += IsOnline(*F) ? 1 : 0;
    Place(Canvas, 80, 44, 260, 18, Label(FString::Printf(TEXT("Online  •  Friends %d / %d"), Online, Friends.Num()), 12, GreenFontColor()));

    // FriendsTabHeaderTab1/2 (TabButtonTemplate) at TOPLEFT 30,-64.
    auto HeaderTab = [this, &Canvas](float X, const TCHAR* Text, bool bActive, bool bIgnore)
    {
        const FString Art = bActive ? TEXT("Interface\\HelpFrame\\HelpFrameTab-Active") : TEXT("Interface\\HelpFrame\\HelpFrameTab-Inactive");
        const float ArtY = bActive ? 67.f : 64.f; // TabButtonTemplate: active art 3 lower
        Place(Canvas, X, ArtY, 16, 32, Img(Art, UV4(0.f, .25f, 0.f, 1.f)));
        Place(Canvas, X + 16, ArtY, 58, 32, Img(Art, UV4(.25f, .75f, 0.f, 1.f)));
        Place(Canvas, X + 74, ArtY, 16, 32, Img(Art, UV4(.75f, 1.f, 0.f, 1.f)));
        Place(Canvas, X, 77, 90, 16, Label(Text, 10, bActive ? HighlightFontColor() : NormalFontColor(), ETextJustify::Center));
        Place(Canvas, X + 4, 66, 82, 28, SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
            .OnMouseButtonDown_Lambda([this, bIgnore](const FGeometry&, const FPointerEvent&)
            {
                if (bIgnoreList != bIgnore) { bIgnoreList = bIgnore; bContextOpen = false; PlayUiSound(TEXT("igMainMenuOptionCheckBoxOn")); Rebuild(); }
                return FReply::Handled();
            }));
    };
    HeaderTab(30, TEXT("Friends"), true, false);
    HeaderTab(120, TEXT("Ignore"), false, true);

    // FriendsFrameFriendsScrollFrame TOPLEFT 21,-100, 302x307; FRIENDS_BUTTON_NORMAL_HEIGHT 34.
    TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    if (Friends.IsEmpty())
        List->AddSlot().AutoHeight().Padding(FMargin(14 * Scale, 8 * Scale, 0, 0))[ Label(TEXT("You have no friends listed."), 12, HighlightFontColor()) ];
    for (const FWowFriendInfo* F : Friends)
    {
        const FString Name = ContactName(F->Guid, F->Name);
        const bool bOnline = IsOnline(*F);
        FString NameLine = Name;
        if (bOnline && (F->Status & 0x2)) NameLine += TEXT(" <AFK>");
        if (bOnline && (F->Status & 0x4)) NameLine += TEXT(" <DND>");
        const FString Info = bOnline
            ? FString::Printf(TEXT("Level %d %s"), F->Level, *ClassName(F->Class))
            : TEXT("Offline");
        const FString Zone = bOnline ? ZoneName(F->AreaId) : FString();
        const uint64 Guid = F->Guid;
        TSharedRef<SWidget> Content = SNew(SBox).HeightOverride(34 * Scale)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(10 * Scale, 3 * Scale, 6 * Scale, 0))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()[ Label(NameLine, 12, bOnline ? NormalFontColor() : GrayFontColor()) ]
                    + SHorizontalBox::Slot().FillWidth(1).Padding(FMargin(8 * Scale, 0, 0, 0))
                    [ Label(Zone, 12, HighlightFontColor(), ETextJustify::Right) ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(10 * Scale, 0, 0, 0))
                [ Label(F->Note.IsEmpty() ? Info : Info + TEXT("  ") + F->Note, 10, bOnline ? HighlightFontColor() : GrayFontColor()) ]
            ];
        List->AddSlot().AutoHeight()
        [
            Row([this, Guid, Name](const FPointerEvent& Event, FVector2D Local)
            {
                SelectedFriend = Guid;
                if (Event.GetEffectingButton() == EKeys::RightMouseButton) OpenContext(Name, Guid, true, Local);
                else { bContextOpen = false; Rebuild(); }
            }, SelectedFriend == Guid, Content)
        ];
    }
    // FriendsFrameFriendsScrollFrame 21,-100; its bar TOPRIGHT -47,-115 to 6,15 above the frame bottom.
    PlaceScrolled(Canvas, 0, 21, 100, 294, 307, List, 321.f, 99.f, 309.f);

    const FWowFriendInfo* const* Selected = Friends.FindByPredicate([this](const FWowFriendInfo* F) { return F->Guid == SelectedFriend; });
    const FString SelectedName = Selected ? ContactName((*Selected)->Guid, (*Selected)->Name) : FString();
    const bool bSelectedOnline = Selected && IsOnline(**Selected);
    Place(Canvas, 15, FrameH - 81 - 21, 131, 21, PanelButton(TEXT("Add Friend"), 131, 21, [this]() { OpenPopup(EPopup::AddFriend); }));
    Place(Canvas, FrameW - 41 - 131, FrameH - 81 - 21, 131, 21,
        PanelButton(TEXT("Send Message"), 131, 21, [this, SelectedName]() { Whisper(SelectedName); }, bSelectedOnline));
}

void SWowFriendsFrame::BuildIgnore(SCanvas& Canvas)
{
    const UWowConnectionManager* C = ConnectionManager.Get();
    TArray<const FWowFriendInfo*> Ignored;
    if (C) for (const FWowFriendInfo& F : C->PacketHandler.FriendsList) if (F.Flags & 0x2) Ignored.Add(&F);
    auto HeaderTab = [this, &Canvas](float X, const TCHAR* Text, bool bActive, bool bIgnore)
    {
        const FString Art = bActive ? TEXT("Interface\\HelpFrame\\HelpFrameTab-Active") : TEXT("Interface\\HelpFrame\\HelpFrameTab-Inactive");
        const float ArtY = bActive ? 67.f : 64.f; // TabButtonTemplate: active art 3 lower
        Place(Canvas, X, ArtY, 16, 32, Img(Art, UV4(0.f, .25f, 0.f, 1.f)));
        Place(Canvas, X + 16, ArtY, 58, 32, Img(Art, UV4(.25f, .75f, 0.f, 1.f)));
        Place(Canvas, X + 74, ArtY, 16, 32, Img(Art, UV4(.75f, 1.f, 0.f, 1.f)));
        Place(Canvas, X, 77, 90, 16, Label(Text, 10, bActive ? HighlightFontColor() : NormalFontColor(), ETextJustify::Center));
        Place(Canvas, X + 4, 66, 82, 28, SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
            .OnMouseButtonDown_Lambda([this, bIgnore](const FGeometry&, const FPointerEvent&)
            {
                if (bIgnoreList != bIgnore) { bIgnoreList = bIgnore; bContextOpen = false; PlayUiSound(TEXT("igMainMenuOptionCheckBoxOn")); Rebuild(); }
                return FReply::Handled();
            }));
    };
    HeaderTab(30, TEXT("Friends"), false, false);
    HeaderTab(120, TEXT("Ignore"), true, true);

    // FriendsFrameIgnoreButton1.. at TOPLEFT 21,-102, 298x16.
    TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    if (Ignored.IsEmpty())
        List->AddSlot().AutoHeight().Padding(FMargin(14 * Scale, 6 * Scale, 0, 0))[ Label(TEXT("You are not ignoring anyone."), 12, HighlightFontColor()) ];
    for (const FWowFriendInfo* F : Ignored)
    {
        const uint64 Guid = F->Guid;
        List->AddSlot().AutoHeight()
        [
            Row([this, Guid](const FPointerEvent&, FVector2D) { SelectedIgnore = Guid; Rebuild(); }, SelectedIgnore == Guid,
                SNew(SBox).HeightOverride(16 * Scale).Padding(FMargin(10 * Scale, 0, 0, 0)).VAlign(VAlign_Center)
                [ Label(ContactName(F->Guid, F->Name), 12, NormalFontColor()) ])
        ];
    }
    // FriendsFrameIgnoreScrollFrame TOPRIGHT -65,-99, 298 x 309.
    PlaceScrolled(Canvas, 1, 21, 99, 298, 309, List, 325.f, 99.f, 309.f);
    Place(Canvas, 15, FrameH - 81 - 21, 131, 21, PanelButton(TEXT("Ignore Player"), 131, 21, [this]() { OpenPopup(EPopup::AddIgnore); }));
    const uint64 Selected = SelectedIgnore;
    const bool bValid = Ignored.ContainsByPredicate([Selected](const FWowFriendInfo* F) { return F->Guid == Selected; });
    Place(Canvas, FrameW - 38 - 134, FrameH - 81 - 21, 134, 21,
        PanelButton(TEXT("Remove Player"), 134, 21, [this, Selected]() { RemoveIgnore(Selected); SelectedIgnore = 0; }, bValid));
}

void SWowFriendsFrame::BuildWho(SCanvas& Canvas)
{
    const UWowConnectionManager* C = ConnectionManager.Get();
    // WhoFrameColumnHeader1..4 (WhoFrame-ColumnTabs) at TOPLEFT 20,-70, each -2 over the previous.
    struct FColumn { const TCHAR* Text; float W; };
    static const FColumn Columns[] = {{TEXT("Name"), 83}, {TEXT("Zone"), 95}, {TEXT("Level"), 40}, {TEXT("Class"), 86}};
    float X = 20.f;
    for (const FColumn& Column : Columns)
    {
        const FString Art = TEXT("Interface\\FriendsFrame\\WhoFrame-ColumnTabs");
        Place(Canvas, X, 70, 5, 24, Img(Art, UV4(0.f, .078125f, 0.f, .75f)));
        Place(Canvas, X + 5, 70, Column.W - 9, 24, Img(Art, UV4(.078125f, .90625f, 0.f, .75f)));
        Place(Canvas, X + Column.W - 4, 70, 4, 24, Img(Art, UV4(.90625f, .96875f, 0.f, .75f)));
        Place(Canvas, X + 8, 75, Column.W - 12, 14, Label(Column.Text, 10, HighlightFontColor()));
        X += Column.W - 2.f;
    }
    const TArray<FWowWhoEntry> Results = C ? C->PacketHandler.WhoResults : TArray<FWowWhoEntry>();
    TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    for (int32 Index = 0; Index < Results.Num(); ++Index)
    {
        const FWowWhoEntry& W = Results[Index];
        const FString Name = W.Name;
        TSharedRef<SWidget> Content = SNew(SBox).HeightOverride(16 * Scale)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(81 * Scale).Padding(FMargin(4 * Scale, 0, 0, 0)).VAlign(VAlign_Center)[ Label(W.Name, 12, ClassColor(W.Class)) ] ]
                + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(93 * Scale).VAlign(VAlign_Center)[ Label(W.Guild.IsEmpty() ? ZoneName(W.Zone) : ZoneName(W.Zone), 10, HighlightFontColor()) ] ]
                + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(38 * Scale).VAlign(VAlign_Center)[ Label(FString::FromInt(W.Level), 10, HighlightFontColor(), ETextJustify::Center) ] ]
                + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(84 * Scale).VAlign(VAlign_Center)[ Label(ClassName(W.Class), 10, HighlightFontColor()) ] ]
            ];
        List->AddSlot().AutoHeight()
        [
            Row([this, Index, Name](const FPointerEvent& Event, FVector2D Local)
            {
                SelectedWho = Index;
                if (Event.GetEffectingButton() == EKeys::RightMouseButton) OpenContext(Name, 0, false, Local);
                else { bContextOpen = false; Rebuild(); }
            }, SelectedWho == Index, Content)
        ];
    }
    // WhoListScrollFrame TOPRIGHT -67,-96, 296x287 (17 rows of 16).
    PlaceScrolled(Canvas, 2, 21, 96, 296, 272, List, 323.f, 96.f, 287.f);
    if (C)
    {
        const uint32 Total = C->PacketHandler.WhoTotalCount;
        FString Totals = FString::Printf(TEXT("%u %s total"), Total, Total == 1 ? TEXT("player") : TEXT("players"));
        if (Total > uint32(Results.Num())) Totals += FString::Printf(TEXT(" (%d shown)"), Results.Num());
        Place(Canvas, 19, FrameH - 127 - 16, 298, 16, Label(Totals, 12, NormalFontColor(), ETextJustify::Center));
    }
    // WhoFrameEditBox: typing filters like /who; Enter sends. It fills the frame art's own input
    // slot (Stefan 10.10: the box stopped short of the slot's right end and its border sat inside
    // the art's), so no border of our own.
    TSharedPtr<SEditableText> Field;
    Place(Canvas, 25, FrameH - 100 - 28, 322, 24,
        SNew(SOverlay)
        + SOverlay::Slot().Padding(FMargin(8 * Scale, 0)).VAlign(VAlign_Center)
        [
            SAssignNew(Field, SEditableText)
            .Text_Lambda([this]() { return FText::FromString(WhoText); })
            .Font(Fnt(12)).ColorAndOpacity(HighlightFontColor())
            .ClearKeyboardFocusOnCommit(true)
            .OnTextChanged_Lambda([this](const FText& Text) { WhoText = Text.ToString().Left(128); })
            // Stefan 11.10: with text in the box Esc did not close the window. Esc in the box
            // leaves it and closes the window in one press.
            .OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& Key)
            {
                if (Key.GetKey() != EKeys::Escape) return FReply::Unhandled();
                FSlateApplication::Get().SetAllUserFocusToGameViewport();
                Close();
                return FReply::Handled();
            })
            .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Commit)
            {
                WhoText = Text.ToString().Left(128);
                if (Commit == ETextCommit::OnEnter) SendWho(WhoText);
                // Esc/Enter leave the box: hand the keyboard back to the game, or the next Esc
                // went nowhere and the window could not be closed (Stefan 10.10).
                if (Commit == ETextCommit::OnEnter || Commit == ETextCommit::OnCleared)
                    FSlateApplication::Get().SetAllUserFocusToGameViewport();
            })
        ]);
    // WhoFrameWhoButton / AddFriend / GroupInvite at BOTTOMRIGHT -40,82, right to left.
    const FString SelectedName = Results.IsValidIndex(SelectedWho) ? Results[SelectedWho].Name : FString();
    const float By = FrameH - 82 - 22;
    Place(Canvas, FrameW - 40 - 120 - 120 - 85, By, 85, 22, PanelButton(TEXT("Refresh"), 85, 22, [this]() { SendWho(WhoText); }));
    Place(Canvas, FrameW - 40 - 120 - 120, By, 120, 22, PanelButton(TEXT("Add Friend"), 120, 22,
        [this, SelectedName]() { AddFriend(SelectedName); }, !SelectedName.IsEmpty()));
    Place(Canvas, FrameW - 40 - 120, By, 120, 22, PanelButton(TEXT("Group Invite"), 120, 22,
        [this, SelectedName]() { InvitePlayer(SelectedName); }, !SelectedName.IsEmpty()));
}

void SWowFriendsFrame::BuildGuild(SCanvas& Canvas)
{
    const UWowConnectionManager* C = ConnectionManager.Get();
    if (!C || C->PacketHandler.GuildName.IsEmpty())
    {
        Place(Canvas, 24, 120, 300, 40, Label(TEXT("You are not in a guild."), 12, NormalFontColor(), ETextJustify::Center));
        return;
    }
    const auto& H = C->PacketHandler;
    TArray<const FWowGuildMember*> Members;
    int32 OnlineCount = 0;
    for (const FWowGuildMember& M : H.GuildRoster)
    {
        if (M.Status) ++OnlineCount;
        if (M.Status || bGuildShowOffline) Members.Add(&M);
    }
    Members.StableSort([](const FWowGuildMember& A, const FWowGuildMember& B)
    {
        if ((A.Status != 0) != (B.Status != 0)) return A.Status != 0;
        return A.Name < B.Name;
    });
    Place(Canvas, 80, 44, 260, 16, Label(FString::Printf(TEXT("%d Guild Members (%d Online)"), H.GuildRoster.Num(), OnlineCount), 10, HighlightFontColor()));
    // Show Offline Members (GuildFrameLFGButton position, top right).
    Place(Canvas, 200, 58, 20, 20,
        SNew(SCheckBox).IsChecked_Lambda([this]() { return bGuildShowOffline ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bGuildShowOffline = State == ECheckBoxState::Checked; Rebuild(); }));
    Place(Canvas, 222, 60, 120, 16, Label(TEXT("Show Offline"), 10, NormalFontColor()));

    struct FColumn { const TCHAR* Text; float W; };
    static const FColumn Columns[] = {{TEXT("Name"), 83}, {TEXT("Zone"), 95}, {TEXT("Level"), 40}, {TEXT("Class"), 86}};
    float X = 20.f;
    for (const FColumn& Column : Columns)
    {
        const FString Art = TEXT("Interface\\FriendsFrame\\WhoFrame-ColumnTabs");
        Place(Canvas, X, 78, 5, 24, Img(Art, UV4(0.f, .078125f, 0.f, .75f)));
        Place(Canvas, X + 5, 78, Column.W - 9, 24, Img(Art, UV4(.078125f, .90625f, 0.f, .75f)));
        Place(Canvas, X + Column.W - 4, 78, 4, 24, Img(Art, UV4(.90625f, .96875f, 0.f, .75f)));
        Place(Canvas, X + 8, 83, Column.W - 12, 14, Label(Column.Text, 10, HighlightFontColor()));
        X += Column.W - 2.f;
    }
    TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    for (int32 Index = 0; Index < Members.Num(); ++Index)
    {
        const FWowGuildMember& M = *Members[Index];
        const bool bOnline = M.Status != 0;
        const FLinearColor Text = bOnline ? HighlightFontColor() : GrayFontColor();
        const FString Name = M.Name;
        TSharedRef<SWidget> Content = SNew(SBox).HeightOverride(16 * Scale)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(81 * Scale).Padding(FMargin(4 * Scale, 0, 0, 0)).VAlign(VAlign_Center)[ Label(M.Name, 12, bOnline ? ClassColor(M.Class) : GrayFontColor()) ] ]
                + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(93 * Scale).VAlign(VAlign_Center)[ Label(bOnline ? ZoneName(M.ZoneId) : TEXT("Offline"), 10, Text) ] ]
                + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(38 * Scale).VAlign(VAlign_Center)[ Label(FString::FromInt(M.Level), 10, Text, ETextJustify::Center) ] ]
                + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(84 * Scale).VAlign(VAlign_Center)[ Label(ClassName(M.Class), 10, Text) ] ]
            ];
        List->AddSlot().AutoHeight()
        [
            Row([this, Index, Name, bOnline](const FPointerEvent& Event, FVector2D Local)
            {
                SelectedGuildMember = Index;
                if (Event.GetEffectingButton() == EKeys::RightMouseButton && bOnline) OpenContext(Name, 0, false, Local);
                else { bContextOpen = false; Rebuild(); }
            }, SelectedGuildMember == Index, Content)
        ];
    }
    // GuildListScrollFrame TOPRIGHT -67,-98, 296 x 237.
    PlaceScrolled(Canvas, 3, 21, 98, 296, 237, List, 323.f, 98.f, 237.f);
    if (!H.GuildMotd.IsEmpty())
        Place(Canvas, 24, 350, 296, 40, SNew(STextBlock).Text(FText::FromString(TEXT("Message of the Day: ") + H.GuildMotd)).Font(Fnt(10))
            .ColorAndOpacity(HighlightFontColor()).AutoWrapText(true).ShadowOffset(FVector2D(1, 1)));
    const float By = FrameH - 82 - 22;
    Place(Canvas, 19, By, 120, 22, PanelButton(TEXT("Add Member"), 120, 22, [this]() { OpenPopup(EPopup::GuildInvite); }));
    Place(Canvas, FrameW - 40 - 120, By, 120, 22, PanelButton(TEXT("Guild Window"), 120, 22, [this]() { if (OnOpenGuildWindow) OnOpenGuildWindow(); }));
}

void SWowFriendsFrame::BuildChat(SCanvas& Canvas)
{
    const UWowConnectionManager* C = ConnectionManager.Get();
    const TArray<FString> Channels = C ? C->PacketHandler.JoinedChannels : TArray<FString>();
    Place(Canvas, 24, 74, 300, 16, Label(TEXT("Channels"), 12, NormalFontColor()));
    TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    if (Channels.IsEmpty())
        List->AddSlot().AutoHeight().Padding(FMargin(10 * Scale, 4 * Scale, 0, 0))[ Label(TEXT("You are not in any channels."), 12, HighlightFontColor()) ];
    for (int32 Index = 0; Index < Channels.Num(); ++Index)
    {
        const FString Text = FString::Printf(TEXT("%d. %s"), Index + 1, *Channels[Index]);
        List->AddSlot().AutoHeight()
        [
            Row([this, Index](const FPointerEvent&, FVector2D) { SelectedChannel = Index; Rebuild(); }, SelectedChannel == Index,
                SNew(SBox).HeightOverride(18 * Scale).Padding(FMargin(10 * Scale, 0, 0, 0)).VAlign(VAlign_Center)
                [ Label(Text, 12, HighlightFontColor()) ])
        ];
    }
    PlaceScrolled(Canvas, 4, 21, 94, 296, 300, List);
    const FString Selected = Channels.IsValidIndex(SelectedChannel) ? Channels[SelectedChannel] : FString();
    const float By = FrameH - 82 - 22;
    Place(Canvas, 19, By, 120, 22, PanelButton(TEXT("Join Channel"), 120, 22, [this]() { OpenPopup(EPopup::JoinChannel); }));
    Place(Canvas, FrameW - 40 - 120, By, 120, 22, PanelButton(TEXT("Leave Channel"), 120, 22,
        [this, Selected]() { LeaveChannel(Selected); SelectedChannel = INDEX_NONE; }, !Selected.IsEmpty()));
}

void SWowFriendsFrame::BuildRaid(SCanvas& Canvas)
{
    const UWowConnectionManager* C = ConnectionManager.Get();
    if (!C) return;
    const FWowGroupInfo& Group = C->PacketHandler.GroupInfo;
    const uint64 Me = C->PacketHandler.EntityManager.LocalPlayerGuid;
    if (Group.GroupType != 1)
    {
        // RaidFrameNotInRaid: RAID_DESCRIPTION.
        Place(Canvas, 30, 90, 290, 120, SNew(STextBlock)
            .Text(FText::FromString(TEXT("A raid is a group of more than five people. Raids are needed for some of the hardest dungeons. Party leaders can convert their party into a raid.")))
            .Font(Fnt(12)).ColorAndOpacity(NormalFontColor()).AutoWrapText(true).Justification(ETextJustify::Center).ShadowOffset(FVector2D(1, 1)));
        const bool bLeader = Group.Members.Num() > 0 && Group.LeaderGuid == Me;
        Place(Canvas, (FrameW - 30 - 140) * .5f, 230, 140, 22, PanelButton(TEXT("Convert to Raid"), 140, 22,
            [this]() { if (UWowConnectionManager* Conn = ConnectionManager.Get()) Conn->SendRawPacket(CMSG_GROUP_RAID_CONVERT, {}); }, bLeader));
        return;
    }
    // Raid groups 1-8 in two columns (RaidGroup1..8).
    for (int32 Sub = 0; Sub < 8; ++Sub)
    {
        const float X = 24 + (Sub % 2) * 150, Y = 76 + (Sub / 2) * 84;
        Place(Canvas, X, Y, 140, 14, Label(FString::Printf(TEXT("Group %d"), Sub + 1), 10, NormalFontColor()));
        int32 Slot = 0;
        for (const FWowGroupMember& M : Group.Members)
        {
            if (M.Group != Sub || Slot >= 5) continue;
            const FString Name = M.Name;
            Place(Canvas, X, Y + 14 + Slot * 13, 140, 13, Label(M.Name + (M.Guid == Group.LeaderGuid ? TEXT(" (L)") : TEXT("")), 10,
                M.Status ? ClassColor(M.Class) : GrayFontColor()));
            ++Slot;
        }
    }
}

void SWowFriendsFrame::BuildPopup(SCanvas& Canvas)
{
    // StaticPopup style dialog (ADD_FRIEND / ADD_IGNORE / CHANNEL_JOIN / ADD_GUILDMEMBER) over the frame.
    FString Prompt;
    switch (Popup)
    {
    case EPopup::AddFriend: Prompt = TEXT("Enter the name of the friend you wish to add:"); break;
    case EPopup::AddIgnore: Prompt = TEXT("Enter the name of the player you wish to ignore:"); break;
    case EPopup::JoinChannel: Prompt = TEXT("Enter the name of the channel you wish to join:"); break;
    case EPopup::GuildInvite: Prompt = TEXT("Add who to your guild?"); break;
    default: return;
    }
    const float W = 320, H = 118, X = (FrameW - 30 - W) * .5f, Y = 150;
    Place(Canvas, X, Y, W, H, SNew(SWowWindowFrame).Padding(0));
    Place(Canvas, X + 16, Y + 16, W - 32, 32, SNew(STextBlock).Text(FText::FromString(Prompt)).Font(Fnt(12))
        .ColorAndOpacity(HighlightFontColor()).AutoWrapText(true).Justification(ETextJustify::Center).ShadowOffset(FVector2D(1, 1)));
    Place(Canvas, X + 40, Y + 50, W - 80, 24,
        SNew(SOverlay)
        + SOverlay::Slot()[ InputBorder() ]
        + SOverlay::Slot().Padding(FMargin(8 * Scale, 0)).VAlign(VAlign_Center)
        [
            SAssignNew(FocusField, SEditableText)
            .Text_Lambda([this]() { return FText::FromString(PopupText); })
            .Font(Fnt(12)).ColorAndOpacity(HighlightFontColor())
            .OnTextChanged_Lambda([this](const FText& Text) { PopupText = Text.ToString().Left(48); })
            .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Commit)
            {
                PopupText = Text.ToString().Left(48);
                if (Commit == ETextCommit::OnEnter) AcceptPopup();
                else if (Commit == ETextCommit::OnCleared) { Popup = EPopup::None; Rebuild(); }
                if (Commit == ETextCommit::OnEnter || Commit == ETextCommit::OnCleared)
                    FSlateApplication::Get().SetAllUserFocusToGameViewport();
            })
        ]);
    Place(Canvas, X + W * .5f - 128, Y + H - 34, 120, 22, PanelButton(TEXT("Accept"), 120, 22, [this]() { AcceptPopup(); }));
    Place(Canvas, X + W * .5f + 8, Y + H - 34, 120, 22, PanelButton(TEXT("Cancel"), 120, 22,
        [this]() { Popup = EPopup::None; FSlateApplication::Get().SetAllUserFocusToGameViewport(); Rebuild(); }));
}

void SWowFriendsFrame::BuildContextMenu(SCanvas& Canvas)
{
    // FriendsDropDown (UnitPopup FRIEND menu): name title, Whisper, Invite, Remove Friend, Cancel.
    struct FEntry { FString Text; TFunction<void()> Action; };
    TArray<FEntry> Entries;
    const FString Name = ContextName;
    const uint64 Guid = ContextGuid;
    Entries.Add({TEXT("Whisper"), [this, Name]() { Whisper(Name); }});
    Entries.Add({TEXT("Invite"), [this, Name]() { InvitePlayer(Name); }});
    if (bContextFriend) Entries.Add({TEXT("Remove Friend"), [this, Guid]() { RemoveFriend(Guid); SelectedFriend = 0; }});
    else Entries.Add({TEXT("Add Friend"), [this, Name]() { AddFriend(Name); }});
    Entries.Add({TEXT("Cancel"), []() {}});
    const float W = 130, LineH = 16, H = 24 + LineH * (Entries.Num() + 1);
    const float X = FMath::Clamp(float(ContextPos.X), 0.f, FrameW - 30 - W), Y = FMath::Clamp(float(ContextPos.Y), 0.f, FrameH - 45 - H);
    Place(Canvas, X, Y, W, H, SNew(SWowTooltipFrame).Padding(0));
    Place(Canvas, X + 12, Y + 10, W - 24, LineH, Label(Name, 12, NormalFontColor()));
    for (int32 Index = 0; Index < Entries.Num(); ++Index)
    {
        TFunction<void()> Action = Entries[Index].Action;
        const FString EntryText = Entries[Index].Text;
        // Stefan 10.10: "Friend menu doesn't work". Each choice is logged so the next build shows
        // whether the click arrives. The action runs after the menu is rebuilt (the row's own
        // widget, which holds this lambda, is replaced by Rebuild).
        Place(Canvas, X + 8, Y + 10 + LineH * (Index + 1), W - 16, LineH,
            Row([this, Action, EntryText, Name](const FPointerEvent&, FVector2D)
            {
                UE_LOG(LogTemp, Log, TEXT("[Friends] menu '%s' for %s"), *EntryText, *Name);
                const TFunction<void()> RunAction = Action;
                bContextOpen = false;
                Rebuild();
                if (RunAction) RunAction();
            }, false,
                SNew(SBox).Padding(FMargin(6 * Scale, 0, 0, 0)).VAlign(VAlign_Center)[ Label(Entries[Index].Text, 12, HighlightFontColor()) ]));
    }
}

void SWowFriendsFrame::PlaceScrolled(SCanvas& Canvas, int32 ListKey, float X, float Y, float W, float H, TSharedRef<SWidget> List,
    float BarX, float BarY, float BarH)
{
    // Stefan 10.10: the lists had Slate's thin grey bar inside the list, not the client's bar with
    // arrows in the frame's scroll column on the right.
    float KeepOffset = 0.f;
    if (const TWeakPtr<SScrollBox>* Old = ListBoxes.Find(ListKey))
        if (const TSharedPtr<SScrollBox> OldBox = Old->Pin()) KeepOffset = OldBox->GetScrollOffset();
    TSharedRef<SWowPanelScrollBar> Bar = SNew(SWowPanelScrollBar)
        .Scale_Lambda([this]() { return Scale; })
        .UIManager(UIManager.Get());
    TSharedRef<SScrollBox> Box = SNew(SScrollBox)
        .ScrollBarVisibility(EVisibility::Collapsed)
        .ConsumeMouseWheel(EConsumeMouseWheel::Always)
        + SScrollBox::Slot()[ List ];
    Box->SetScrollOffset(KeepOffset);
    Bar->SetScrollBox(Box);
    ListBoxes.Add(ListKey, Box);
    Place(Canvas, X, Y, W, H, Box);
    // Stefan 11.10: the arrows did not sit in the art's scroll column. FauxScrollFrame's bar is
    // TOPLEFT of the frame's TOPRIGHT 6,-16 to BOTTOMLEFT of its BOTTOMRIGHT 6,16 with the arrows
    // above and below it, i.e. the column x = right + 6 over the frame's full height.
    Place(Canvas, BarX >= 0.f ? BarX : X + W + 6.f, BarY >= 0.f ? BarY : Y, SWowPanelScrollBar::ColumnWidth, BarH > 0.f ? BarH : H, Bar);
}
