#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateTypes.h"

class UWowConnectionManager;
class UWowUIManager;
class SCanvas;
class SBox;
class FWowRetainedSlateBrush;
struct FSlateBrush;
struct FButtonStyle;
struct FWowAuctionOffer;

/**
 * Native replica of the 3.3.5 AuctionFrame (Blizzard_AuctionUI): Browse (name, level range,
 * rarity, usable, category list, 50 results per page, bid/buyout), Bids (auctions you bid on)
 * and Auctions (create an auction from a bag item: starting price, buyout, 12/24/48 hours,
 * deposit; your auctions with Cancel). Positions are AuctionFrame.xml units x UI scale.
 */
class WOWUNREAL_API SWowAuctionHouse : public SCompoundWidget
{
public:
    static constexpr float FrameW = 832.f;
    static constexpr float FrameH = 447.f;

    SLATE_BEGIN_ARGS(SWowAuctionHouse) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UWowUIManager>, UIManager)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager);
    virtual ~SWowAuctionHouse();

    /** AUCTION_HOUSE_SHOW (MSG_AUCTION_HELLO from an auctioneer). */
    void Open();
    /** AUCTION_HOUSE_CLOSED: close button, Escape, walking away. */
    void Close();
    bool IsOpen() const { return bVisible; }
    FVector2D GetPanelSize() const { return FVector2D(FrameW, 512.f) * Scale; }

    /** The wheel over the window never reaches the camera (it zoomed out at the end of a list). */
    virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

private:
    enum class ETab : uint8 { Browse, Bids, Auctions };

    // Layout
    void Rebuild();
    void BuildFrameArt(SCanvas& Canvas) const;
    void BuildTabs(SCanvas& Canvas);
    void BuildBrowse(SCanvas& Canvas);
    void BuildBids(SCanvas& Canvas);
    void BuildAuctions(SCanvas& Canvas);
    void BuildFilters(SCanvas& Canvas);
    void BuildColumnHeader(SCanvas& Canvas, float X, float Y, float W, const FText& Text, int32 SortKey);
    TSharedRef<SWidget> BuildResultRow(const FWowAuctionOffer& Offer, ETab ForTab);
    void PlaceList(SCanvas& Canvas, float X, float Y, float W, float H, const TArray<FWowAuctionOffer>& Offers, ETab ForTab);
    /** Up / down arrow buttons at the ends of a list's scroll bar column (X = the column's left edge). */
    void PlaceScrollArrows(SCanvas& Canvas, float X, float Y, float H, TSharedRef<class SScrollBox> Box, float Step);
    TSharedRef<SWidget> MoneyInput(FString& Gold, FString& Silver, FString& Copper);
    TSharedRef<SWidget> MoneyText(uint32 Copper, float WowPixels, FLinearColor Color = FLinearColor::White) const;
    TSharedRef<SWidget> InputBorder() const;
    TSharedRef<SWidget> TextField(FString& Value, int32 MaxLetters, float WowPixels, bool bNumeric = false);
    TSharedRef<SWidget> PanelButton(const FText& Label, float W, float H, FOnClicked OnClicked, TAttribute<bool> Enabled = true) const;
    TSharedRef<SWidget> CloseButton(FOnClicked OnClicked) const;
    TSharedRef<SWidget> CheckBox(bool& bValue, const FText& Text);
    TSharedRef<SWidget> Img(const FString& Path, FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1)), FLinearColor Tint = FLinearColor::White) const;
    TSharedRef<SWidget> Label(const FText& Text, float WowPixels, FLinearColor Color, ETextJustify::Type Justify = ETextJustify::Left) const;
    void Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const;
    FSlateFontInfo Fnt(float WowPixels) const;
    float TextWidth(const FString& Text, const FSlateFontInfo& Font) const;
    const FSlateBrush* LoadBrush(const FString& TexturePath, FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1))) const;
    const FButtonStyle* TextureButtonStyle(const FString& Up, const FString& Down, const FString& Disabled = FString(),
        FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1))) const;

    // Data helpers
    const FWowAuctionOffer* FindOffer(uint32 AuctionId) const;
    uint32 NextBid(const FWowAuctionOffer& Offer) const;
    uint32 GetPlayerMoney() const;
    uint32 SellCount() const;
    uint32 Deposit() const;
    FString SellerName(uint64 Guid) const;
    FString TimeLeftText(uint32 Milliseconds, FLinearColor& OutColor) const;
    uint32 ComputeStateStamp() const;
    TArray<FWowAuctionOffer> Sorted(const TArray<FWowAuctionOffer>& Offers) const;

    // Actions
    void SetTab(ETab NewTab);
    void Search(uint32 NewPage);
    void SelectAuction(uint32 AuctionId);
    void PlaceBid(bool bBuyout);
    void PutSellItem(uint64 ItemGuid);
    void CreateAuction();
    void CancelSelectedAuction();
    void PlayUiSound(const TCHAR* Sound) const;

    TWeakObjectPtr<UWowConnectionManager> ConnectionManager;
    TWeakObjectPtr<UWowUIManager> UIManager;
    TFunction<bool(uint64)> PreviousItemUseOverride;
    bool bVisible = false;
    ETab Tab = ETab::Browse;
    float Scale = 1.f;
    uint32 LastStateStamp = 0;
    double NextRangeCheck = 0.0;
    /** Data-driven rebuilds (item names/icons arriving one by one after a search) are coalesced:
     *  a full rebuild per arriving item made the window flicker (Stefan 10.10). */
    double LastRebuildTime = 0.0;
    bool bDataRebuildPending = false;
    /** Scroll positions kept across rebuilds (the lists are recreated). */
    TSharedPtr<class SScrollBox> ListBox, FilterBox;
    float ListScroll = 0.f, FilterScroll = 0.f;
    /** The auctioneer's portrait in the top-left ring (AuctionPortraitTexture). */
    TSharedPtr<FWowRetainedSlateBrush> PortraitBrush;
    uint32 SeenResultCount = 0;

    // Browse
    FString SearchName, MinLevel, MaxLevel;
    int32 Quality = -1;          // -1 = all, 0..5 = Poor..Legendary
    bool bRarityOpen = false;    // BrowseDropDown list shown
    void BuildRarityList(SCanvas& Canvas);
    bool bUsableOnly = false;
    int32 SelectedClass = -1;    // index into the category table
    int32 SelectedSubClass = -1;
    uint32 Page = 0;
    int32 SortKey = 0;           // 0 none, 1 rarity/name, 2 level, 3 time left, 4 seller, 5 bid
    bool bSortReverse = false;

    // Selected row (any tab) and the bid entry under the list
    uint32 SelectedAuction = 0;
    FString BidGold, BidSilver, BidCopper;
    uint32 PendingConfirmAuction = 0;
    bool bPendingConfirmBuyout = false;
    bool bPendingCancel = false;

    // Auctions tab
    uint64 SellItemGuid = 0;
    FString StartGold, StartSilver, StartCopper, BuyoutGold, BuyoutSilver, BuyoutCopper;
    int32 Duration = 0;          // 0 = 12, 1 = 24, 2 = 48 hours

    TSharedPtr<SBox> Root;
    /** Double-buffered content (Stefan 11.10: the window blinked on open and on every search).
     *  A rebuild adds the new canvas on top and drops the previous one a frame later, so
     *  there is never a frame with an empty window or with nothing under the mouse. */
    TSharedPtr<class SOverlay> Layers;
    TSharedPtr<SWidget> CurrentLayer;
    TArray<TSharedPtr<SWidget>> RetiredLayers;
    uint64 RetireAfterFrame = 0;
    void DropRetiredLayers();
    mutable TSet<uint64> RequestedNames;
    mutable TMap<FString, TSharedPtr<FWowRetainedSlateBrush>> TextureBrushes;
    /** UIPanelScrollBar look: the UI-ScrollBar-Knob thumb, no Slate track (built once, outlives the lists). */
    TSharedPtr<struct FScrollBarStyle> ScrollStyle;
    const struct FScrollBarStyle* GetScrollStyle();
    mutable TMap<FString, double> FailedTextureTimes;
    mutable TMap<FString, TSharedPtr<FButtonStyle>> ButtonStyles;
};
