#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateTypes.h"

class UWowConnectionManager;
class UWowUIManager;
class SCanvas;
class SBox;
class SEditableText;
class FWowRetainedSlateBrush;

/**
 * Native replica of the 3.3.5 FriendsFrame (social window): bottom tabs Friends / Who /
 * Guild / Chat / Raid, the Friends tab with its Friends / Ignore header tabs, the WhoFrame
 * list with its edit box, and small Add Friend / Ignore / Join Channel input dialogs.
 * Positions are FriendsFrame.xml values in WoW units x UI scale. Opened from the social
 * micro button, the chat friends button, ToggleFriendsFrame() and the O key.
 */
class WOWUNREAL_API SWowFriendsFrame : public SCompoundWidget
{
public:
    static constexpr float FrameW = 384.f;
    static constexpr float FrameH = 512.f;

    enum class ETab : uint8 { Friends, Who, Guild, Chat, Raid };

    SLATE_BEGIN_ARGS(SWowFriendsFrame) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UWowUIManager>, UIManager)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager);
    virtual ~SWowFriendsFrame();

    void Toggle();
    void Open(TOptional<ETab> InTab = TOptional<ETab>());
    void Close();
    bool IsOpen() const { return bVisible; }
    FVector2D GetPanelSize() const { return FVector2D(FrameW, FrameH) * Scale; }

    /** Chat edit box with "/w Name " (Send Message, Whisper). */
    TFunction<void(const FString&)> OnWhisper;
    /** The full guild window (Guild tab button). */
    TFunction<void()> OnOpenGuildWindow;

    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

private:
    enum class EPopup : uint8 { None, AddFriend, AddIgnore, JoinChannel, GuildInvite };

    void Rebuild();
    uint32 StateStamp() const;
    void BuildArt(SCanvas& Canvas) const;
    void BuildTabs(SCanvas& Canvas);
    void BuildFriends(SCanvas& Canvas);
    void BuildIgnore(SCanvas& Canvas);
    void BuildWho(SCanvas& Canvas);
    void BuildGuild(SCanvas& Canvas);
    void BuildChat(SCanvas& Canvas);
    void BuildRaid(SCanvas& Canvas);
    void BuildPopup(SCanvas& Canvas);
    void BuildContextMenu(SCanvas& Canvas);

    // Actions
    void SetTab(ETab NewTab);
    void RequestContacts();
    void SendWho(const FString& Filter);
    void AddFriend(const FString& Name);
    void RemoveFriend(uint64 Guid);
    void AddIgnore(const FString& Name);
    void RemoveIgnore(uint64 Guid);
    void JoinChannel(const FString& Name);
    void LeaveChannel(const FString& Name);
    void InvitePlayer(const FString& Name);
    void Whisper(const FString& Name);
    void OpenPopup(EPopup Popup);
    void AcceptPopup();
    void OpenContext(const FString& Name, uint64 Guid, bool bFriend, FVector2D LocalPos);
    void PlayUiSound(const TCHAR* Sound) const;

    // Data
    FString ContactName(uint64 Guid, const FString& Fallback) const;
    static FString ClassName(uint32 ClassId);
    static FLinearColor ClassColor(uint32 ClassId);
    static FString ZoneName(uint32 AreaId);

    // Widget helpers (WoW units)
    void Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const;
    /** A list in its scroll frame (X,Y,W,H) with the client's scroll bar in the frame's scroll column
     *  (6 units right of the list, like FauxScrollFrameTemplate). Keeps the scroll position over
     *  rebuilds (every click rebuilds the window). */
    /** BarX/BarY/BarH: the scroll bar column as FriendsFrame.xml places it (UIPanelScrollBarTemplate:
     *  up arrow at the top, down arrow at the bottom); negative = right of the list, its height. */
    void PlaceScrolled(SCanvas& Canvas, int32 ListKey, float X, float Y, float W, float H, TSharedRef<SWidget> List,
        float BarX = -1.f, float BarY = -1.f, float BarH = -1.f);
    TMap<int32, TWeakPtr<class SScrollBox>> ListBoxes;
    FSlateFontInfo Fnt(float WowPixels) const;
    float TextWidth(const FString& Text, const FSlateFontInfo& Font) const;
    const FSlateBrush* LoadBrush(const FString& TexturePath, FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1))) const;
    const FButtonStyle* TextureButtonStyle(const FString& Up, const FString& Down) const;
    TSharedRef<SWidget> Img(const FString& Path, FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1)), FLinearColor Tint = FLinearColor::White) const;
    TSharedRef<SWidget> Label(const FString& Text, float WowPixels, FLinearColor Color, ETextJustify::Type Justify = ETextJustify::Left) const;
    TSharedRef<SWidget> PanelButton(const FString& Text, float W, float H, TFunction<void()> OnClick, bool bEnabled = true) const;
    TSharedRef<SWidget> InputBorder() const;
    TSharedRef<SWidget> Row(TFunction<void(const FPointerEvent&, FVector2D)> OnClick, bool bSelected, TSharedRef<SWidget> Content) const;

    TWeakObjectPtr<UWowConnectionManager> ConnectionManager;
    TWeakObjectPtr<UWowUIManager> UIManager;
    TSharedPtr<SBox> Root;
    TSharedPtr<SEditableText> FocusField;
    FDelegateHandle FriendListHandle, WhoListHandle;
    bool bVisible = false;
    bool bFocusPending = false;
    ETab Tab = ETab::Friends;
    bool bIgnoreList = false;
    float Scale = 1.f;
    uint32 LastStamp = 0;

    uint64 SelectedFriend = 0, SelectedIgnore = 0;
    int32 SelectedWho = INDEX_NONE, SelectedChannel = INDEX_NONE, SelectedGuildMember = INDEX_NONE;
    FString WhoText;
    bool bGuildShowOffline = false;

    EPopup Popup = EPopup::None;
    FString PopupText;

    bool bContextOpen = false;
    bool bContextFriend = false;
    FString ContextName;
    uint64 ContextGuid = 0;
    FVector2D ContextPos = FVector2D::ZeroVector;

    mutable TMap<FString, TSharedPtr<FWowRetainedSlateBrush>> TextureBrushes;
    mutable TMap<FString, double> FailedTextureTimes;
    mutable TMap<FString, TSharedPtr<FButtonStyle>> ButtonStyles;
};
