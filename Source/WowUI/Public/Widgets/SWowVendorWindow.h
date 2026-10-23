#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "WowEntity.h"

class SBox;
class SCanvas;
class FWowRetainedSlateBrush;
struct FButtonStyle;

DECLARE_DELEGATE_FourParams(FOnBuyItem, uint64 /*VendorGuid*/, uint32 /*ItemId*/, int32 /*Count*/, int32 /*VendorSlot*/);
DECLARE_DELEGATE_ThreeParams(FOnSellItem, uint64 /*VendorGuid*/, uint64 /*ItemGuid*/, uint8 /*Count*/);
DECLARE_DELEGATE(FOnCloseVendor);

/**
 * Native replica of the 3.3.5 MerchantFrame (MerchantFrame.xml/.lua): 384x512 frame art with the
 * NPC portrait and name, 10 items per page in two columns (slot, name plate, price), Prev/Next
 * and "Page n of m", repair buttons for armorers, the last sold item, money, and the Merchant /
 * Buyback tabs (12 buyback slots on the buyback art). Selling is right-click on a bag item.
 */
class WOWUI_API SWowVendorWindow : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowVendorWindow) {}
        SLATE_EVENT(FOnBuyItem, OnBuyItem)
        SLATE_EVENT(FOnSellItem, OnSellItem)
        SLATE_EVENT(FOnCloseVendor, OnCloseVendor)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetContext(class UWowConnectionManager* Connection, class UWowUIManager* UI);
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override;
    virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;

    /** Update the vendor window with new data */
    void UpdateVendor(uint64 VendorGuid, const TArray<FWowVendorItem>& Items);

    /** Kept for callers; selling happens from the bags (right-click). */
    void UpdatePlayerInventory(const TArray<FWowItem>& Items) {}

    /** Hide the vendor window */
    void CloseVendor();

    /** Frame size in Slate units (384x512 at the UI scale). */
    FVector2D GetPanelSize() const;

private:
    FOnBuyItem OnBuyItem;
    FOnSellItem OnSellItem;
    FOnCloseVendor OnCloseVendor;

    TWeakObjectPtr<class UWowConnectionManager> Connection;
    TWeakObjectPtr<class UWowUIManager> UIManager;
    uint64 CurrentVendorGuid = 0;
    TArray<FWowVendorItem> CurrentItems;
    float Scale = 1.25f;
    int32 Page = 0;          // 0-based
    int32 Tab = 0;           // 0 = Merchant, 1 = Buyback
    uint64 BuiltStamp = 0;
    TSharedPtr<SBox> Root;

    mutable TMap<FString, TSharedPtr<FWowRetainedSlateBrush>> Brushes;
    mutable TMap<FString, double> FailedBrushes;
    mutable TMap<FString, TSharedPtr<FButtonStyle>> ButtonStyles;
    TSharedPtr<FWowRetainedSlateBrush> PortraitBrush;

    uint64 StateStamp() const;
    void Rebuild();
    int32 NumPages() const;
    bool CanRepair() const;

    const FSlateBrush* Brush(const FString& Path, float U0 = 0.f, float U1 = 1.f, float V0 = 0.f, float V1 = 1.f) const;
    const FButtonStyle* TextureButtonStyle(const FString& Up, const FString& Down, const FString& Disabled = FString(),
        float U0 = 0.f, float U1 = 1.f, float V0 = 0.f, float V1 = 1.f) const;
    FSlateFontInfo Font(float WowPixels, bool bOutline = false) const;
    float TextWidth(const FString& Text, const FSlateFontInfo& FontInfo) const;
    void Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const;
    void PlaceImage(SCanvas& Canvas, float X, float Y, float W, float H, const FSlateBrush* Image,
        const FLinearColor& Tint = FLinearColor::White) const;
    void PlaceText(SCanvas& Canvas, float X, float Y, float W, float H, const FString& Text, const FLinearColor& Color,
        float WowPixels, EHorizontalAlignment HAlign = HAlign_Left, bool bOutline = false, bool bWrap = false) const;
    /** SmallMoneyFrameTemplate: gold/silver/copper with coin icons; X is the left edge, or the right edge if bRight. */
    void PlaceMoney(SCanvas& Canvas, float X, float Y, uint32 Copper, bool bRight) const;
    /** MerchantItemTemplate / MerchantBuyBackItem at (X, Y). */
    void PlaceItem(SCanvas& Canvas, float X, float Y, uint32 Entry, const FString& Name, const FLinearColor& NameColor,
        uint32 Price, uint32 Count, int32 Stock, bool bRedTint, bool bGreyTint, bool bBuyback, TFunction<void(const FPointerEvent&)> OnClick) const;

    TSharedRef<SWidget> BuildFrame();

    /** Bag items (backpack + bags) with their guid, for selling and repair checks. */
    void ForEachBagItem(TFunctionRef<void(uint64 Guid, const struct FWowItemEntity& Item)> Visit) const;
    /** Sell every grey (poor quality, sellable) bag item; returns how many were sent. */
    int32 SellGreyItems();
    /** Repair everything when something equipped or in the bags is damaged. */
    void RepairIfDamaged();
    /** Anything equipped or in the bags below max durability (Repair All has a cost). */
    bool IsAnythingDamaged() const;
    /** Auto-sell/auto-repair once per vendor visit; selling retries while item info arrives. */
    void RunAutoActions();
    void PlaceCheckBox(SCanvas& Canvas, float X, float Y, const FString& Label, const FString& Option) const;

    uint64 AutoVendorGuid = 0;
    double AutoSellUntil = 0.0;
    TSet<uint64> SoldGuids;
    int32 SoldCount = 0;
    uint64 SoldCopper = 0;
    bool bSoldMessagePending = false;
};
