#include "Widgets/SWowVendorWindow.h"
#include "WowColor.h"
#include "WowWindowStyle.h"
#include "WowItemPresentation.h"
#include "WowInventoryManager.h"
#include "WowRetainedSlateBrush.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowClientPreferences.h"
#include "WowEntityManager.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SToolTip.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"

// Layout numbers are the 3.3.5 originals (MerchantFrame.xml / MerchantFrame.lua) in WoW UI
// units from the frame's top-left corner.
namespace
{
    constexpr float MerchantW = 384.f, MerchantH = 512.f;
    constexpr int32 MerchantItemsPerPage = 10;   // MERCHANT_ITEMS_PER_PAGE
    constexpr int32 BuybackItemsPerPage = 12;    // BUYBACK_ITEMS_PER_PAGE
    constexpr float ItemX = 24.f, ItemY = 80.f, ItemW = 153.f, ItemH = 44.f, ItemGapX = 12.f;
    constexpr uint32 NpcFlagRepair = 0x00001000; // UNIT_NPC_FLAG_REPAIR

    const FLinearColor NormalFontColor = WowColor::FromWow(1.f, .82f, 0.f);
    const FLinearColor HighlightFontColor(1.f, 1.f, 1.f);
}

void SWowVendorWindow::SetContext(UWowConnectionManager* InConnection, UWowUIManager* UI)
{
    Connection = InConnection;
    UIManager = UI;
    if (UI && UI->GetFrameManager() && UI->GetFrameManager()->GetUIScale() > 0.f)
        Scale = UI->GetFrameManager()->GetUIScale();
}

void SWowVendorWindow::Construct(const FArguments& InArgs)
{
    OnBuyItem = InArgs._OnBuyItem;
    OnSellItem = InArgs._OnSellItem;
    OnCloseVendor = InArgs._OnCloseVendor;
    ChildSlot
    [
        SAssignNew(Root, SBox)
        .WidthOverride_Lambda([this]() { return MerchantW * Scale; })
        .HeightOverride_Lambda([this]() { return MerchantH * Scale; })
    ];
    SetVisibility(EVisibility::Collapsed);
}

FVector2D SWowVendorWindow::GetPanelSize() const
{
    return FVector2D(MerchantW, MerchantH) * Scale;
}

// r415 (Stefan 17.10: the first time a vendor opens, its items "flicker" - only the first time):
// the item names, icons and prices arrive one by one after the window opened, and each answer
// rebuilt the page, so the rows changed several times in the first frames. The window now stays
// invisible until every listed item is known (at most 0.6 s), then appears complete once.
static TMap<const SWowVendorWindow*, double> GWowVendorRevealAt;
static bool WowVendorItemsKnown(const TWeakObjectPtr<UWowConnectionManager>& Connection, const TArray<FWowVendorItem>& Items)
{
    if (!Connection.IsValid()) return true;
    for (const FWowVendorItem& Item : Items)
        if (Item.ItemId && !Connection->PacketHandler.ItemTemplates.Contains(Item.ItemId)) return false;
    return true;
}

void SWowVendorWindow::UpdateVendor(uint64 Guid, const TArray<FWowVendorItem>& Items)
{
    if (Guid != CurrentVendorGuid) { Page = 0; Tab = 0; }
    CurrentVendorGuid = Guid;
    CurrentItems = Items;
    for (const auto& Item : Items)
        if (Connection.IsValid()) Connection->RequestItemInfo(Item.ItemId);
    PortraitBrush.Reset();
    if (WowVendorItemsKnown(Connection, Items))
    {
        GWowVendorRevealAt.Remove(this);
        SetRenderOpacity(1.f);
    }
    else
    {
        GWowVendorRevealAt.Add(this, FPlatformTime::Seconds() + 0.6);
        SetRenderOpacity(0.f);
    }
    SetVisibility(EVisibility::Visible);
    Rebuild();
    RunAutoActions();
}

void SWowVendorWindow::CloseVendor()
{
    if (Connection.IsValid() && Connection->PacketHandler.ActiveVendorGuid == CurrentVendorGuid)
        Connection->PacketHandler.ActiveVendorGuid = 0;
    CurrentVendorGuid = 0;
    CurrentItems.Empty();
    PortraitBrush.Reset();
    AutoVendorGuid = 0;
    AutoSellUntil = 0.0;
    GWowVendorRevealAt.Remove(this);
    SetRenderOpacity(1.f);
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().CloseToolTip();
    SetVisibility(EVisibility::Collapsed);
}

void SWowVendorWindow::ForEachBagItem(TFunctionRef<void(uint64, const FWowItemEntity&)> Visit) const
{
    if (!Connection.IsValid()) return;
    auto& Entities = Connection->PacketHandler.EntityManager;
    const FWowPlayerEntity* Player = Entities.GetLocalPlayer();
    if (!Player) return;
    auto VisitGuid = [&](uint64 Guid)
    {
        if (!Guid) return;
        if (const FWowItemEntity* Item = Entities.FindItem(Guid)) Visit(Guid, *Item);
    };
    for (uint8 Slot = 0; Slot < 16; ++Slot) VisitGuid(Player->GetBackpackItemGuid(Slot));
    for (uint8 Bag = 1; Bag <= 4; ++Bag)
        if (const FWowContainerEntity* Container = Entities.FindContainer(Player->GetBagGuid(Bag)))
            for (int32 Slot = 0; Slot < Container->GetNumSlots(); ++Slot) VisitGuid(Container->GetItemGuidAtSlot(Slot));
}

// When each grey item's sale was sent (a bought-back item keeps its GUID).
static TMap<uint64, double> GVendorSoldAt;

int32 SWowVendorWindow::SellGreyItems()
{
    if (!Connection.IsValid() || !CurrentVendorGuid) return 0;
    int32 Sent = 0;
    ForEachBagItem([&](uint64 Guid, const FWowItemEntity& Item)
    {
        if (SoldGuids.Contains(Guid) && FPlatformTime::Seconds() - GVendorSoldAt.FindRef(Guid) < 3.0) return;
        const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(Connection.Get(), Item.Entry);
        if (!Info) { Connection->RequestItemInfo(Item.Entry); return; }
        if (Info->Quality != 0 || Info->SellPrice == 0) return;
        const int32 Count = FMath::Clamp(Item.GetStackCount(), 1, 255);
        SoldGuids.Add(Guid);
        GVendorSoldAt.Add(Guid, FPlatformTime::Seconds());
        OnSellItem.ExecuteIfBound(CurrentVendorGuid, Guid, static_cast<uint8>(Count));
        SoldCopper += static_cast<uint64>(Info->SellPrice) * Count;
        ++SoldCount;
        ++Sent;
    });
    if (Sent > 0) bSoldMessagePending = true;
    return Sent;
}

void SWowVendorWindow::RepairIfDamaged()
{
    if (Connection.IsValid() && CanRepair() && IsAnythingDamaged())
        Connection->SendRepairItem(CurrentVendorGuid, 0, false);
}

bool SWowVendorWindow::IsAnythingDamaged() const
{
    if (!Connection.IsValid()) return false;
    auto& Entities = Connection->PacketHandler.EntityManager;
    const FWowPlayerEntity* Player = Entities.GetLocalPlayer();
    if (!Player) return false;
    bool bDamaged = false;
    auto Check = [&](const FWowItemEntity& Item)
    {
        if (Item.GetMaxDurability() > 0 && Item.GetDurability() < Item.GetMaxDurability()) bDamaged = true;
    };
    for (uint8 Slot = 0; Slot < 19; ++Slot)
        if (const uint64 Guid = Player->GetEquipmentItemGuid(Slot))
            if (const FWowItemEntity* Item = Entities.FindItem(Guid)) Check(*Item);
    ForEachBagItem([&](uint64, const FWowItemEntity& Item) { Check(Item); });
    return bDamaged;
}

void SWowVendorWindow::RunAutoActions()
{
    if (!CurrentVendorGuid || AutoVendorGuid == CurrentVendorGuid) return;
    AutoVendorGuid = CurrentVendorGuid;
    SoldGuids.Reset();
    SoldCount = 0;
    SoldCopper = 0;
    // Repair first so the money from selling isn't needed to judge it (as the addons do).
    if (FWowClientPreferences::GetWorldOption(TEXT("autorepair"))) RepairIfDamaged();
    if (FWowClientPreferences::GetWorldOption(TEXT("autosellgrey")))
    {
        SellGreyItems();
        // Item info for items not seen this session arrives a moment later.
        AutoSellUntil = FPlatformTime::Seconds() + 3.0;
    }
}

int32 SWowVendorWindow::NumPages() const
{
    return FMath::Max(1, FMath::DivideAndRoundUp(CurrentItems.Num(), MerchantItemsPerPage));
}

bool SWowVendorWindow::CanRepair() const
{
    if (!Connection.IsValid()) return false;
    const FWowEntity* Entity = Connection->PacketHandler.EntityManager.Find(CurrentVendorGuid);
    return Entity && Entity->IsUnit() && (static_cast<const FWowUnitEntity*>(Entity)->GetNpcFlags() & NpcFlagRepair) != 0;
}

uint64 SWowVendorWindow::StateStamp() const
{
    uint64 Stamp = static_cast<uint64>(Page) * 31ull + Tab;
    Stamp = Stamp * 1000003ull + CurrentVendorGuid;
    Stamp = Stamp * 131ull + static_cast<uint64>(FMath::RoundToInt(Scale * 100.f));
    if (!Connection.IsValid()) return Stamp;
    auto& Packets = Connection->PacketHandler;
    Stamp = Stamp * 1000003ull + Packets.VendorRevision;
    Stamp = Stamp * 1000003ull + static_cast<uint64>(Packets.ItemTemplates.Num());
    if (const auto* Player = Packets.EntityManager.GetLocalPlayer())
    {
        Stamp = Stamp * 1000003ull + Player->GetCoinage();
        Stamp = Stamp * 131ull + Player->GetLevel();
        for (int32 Index = 0; Index < BuybackItemsPerPage; ++Index)
            Stamp = Stamp * 1000003ull + Player->GetField64(PlayerField::BUYBACK_SLOT_START + Index * 2);
    }
    // Repair buttons grey out once nothing is damaged.
    if (Tab == 0 && CanRepair()) Stamp = Stamp * 3ull + (IsAnythingDamaged() ? 1ull : 2ull);
    return Stamp;
}

void SWowVendorWindow::Tick(const FGeometry& Geometry, double Time, float Delta)
{
    SCompoundWidget::Tick(Geometry, Time, Delta);
    if (!CurrentVendorGuid) return;
    if (UIManager.IsValid() && UIManager->GetFrameManager())
    {
        const float NewScale = UIManager->GetFrameManager()->GetUIScale();
        if (NewScale > 0.f && !FMath::IsNearlyEqual(NewScale, Scale)) Scale = NewScale;
    }
    if (Connection.IsValid() && Connection->PacketHandler.VendorItems.Num() && Connection->PacketHandler.ActiveVendorGuid == CurrentVendorGuid)
        CurrentItems = Connection->PacketHandler.VendorItems;
    if (AutoSellUntil > 0.0)
    {
        if (FPlatformTime::Seconds() < AutoSellUntil) SellGreyItems();
        else AutoSellUntil = 0.0;
    }
    if (bSoldMessagePending && AutoSellUntil == 0.0)
    {
        bSoldMessagePending = false;
        if (SoldCount > 0 && UIManager.IsValid() && UIManager->GetEventSystem())
        {
            const uint64 Gold = SoldCopper / 10000, Silver = SoldCopper / 100 % 100, Copper = SoldCopper % 100;
            FString Money;
            if (Gold) Money += FString::Printf(TEXT("%llu|cffffd700g|r "), Gold);
            if (Gold || Silver) Money += FString::Printf(TEXT("%llu|cffc7c7cfs|r "), Silver);
            Money += FString::Printf(TEXT("%llu|cffeda55fc|r"), Copper);
            UIManager->GetEventSystem()->ExecuteLuaString(FString::Printf(
                TEXT("DEFAULT_CHAT_FRAME:AddMessage(\"Sold %d grey item%s for %s.\", 1, 1, 0)"),
                SoldCount, SoldCount == 1 ? TEXT("") : TEXT("s"), *Money), TEXT("=VendorSellGrey"));
        }
        SoldCount = 0;
        SoldCopper = 0;
    }
    if (StateStamp() != BuiltStamp) Rebuild();
    if (const double* RevealAt = GWowVendorRevealAt.Find(this))
    {
        if (FPlatformTime::Seconds() >= *RevealAt || WowVendorItemsKnown(Connection, CurrentItems))
        {
            GWowVendorRevealAt.Remove(this);
            SetRenderOpacity(1.f);
        }
    }
}

FReply SWowVendorWindow::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
    // MerchantFrame OnMouseWheel: wheel down = next page, up = previous page.
    if (Tab == 0)
    {
        const int32 NewPage = FMath::Clamp(Page + (Event.GetWheelDelta() < 0.f ? 1 : -1), 0, NumPages() - 1);
        if (NewPage != Page) { Page = NewPage; Rebuild(); }
    }
    return FReply::Handled();
}

void SWowVendorWindow::Rebuild()
{
    Page = FMath::Clamp(Page, 0, NumPages() - 1);
    BuiltStamp = StateStamp();
    // Replacing the content destroys the hovered widget; Slate would leave its tooltip
    // window behind, following the cursor (e.g. "Repair All Items" after repairing).
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().CloseToolTip();
    if (Root.IsValid()) Root->SetContent(BuildFrame());
}

const FSlateBrush* SWowVendorWindow::Brush(const FString& Path, float U0, float U1, float V0, float V1) const
{
    if (Path.IsEmpty()) return nullptr;
    const FString Key = FString::Printf(TEXT("%s|%.5f,%.5f,%.5f,%.5f"), *Path, U0, U1, V0, V1);
    if (const TSharedPtr<FWowRetainedSlateBrush>* Found = Brushes.Find(Key)) return Found->Get();
    const double Now = FPlatformTime::Seconds();
    if (const double* Failed = FailedBrushes.Find(Key); Failed && Now - *Failed < 5.0) return nullptr;
    UTexture2D* Texture = UIManager.IsValid() && UIManager->GetFrameManager()
        ? UIManager->GetFrameManager()->LoadUITexture(Path) : nullptr;
    if (!Texture) { FailedBrushes.Add(Key, Now); return nullptr; }
    TSharedPtr<FWowRetainedSlateBrush> New = MakeShared<FWowRetainedSlateBrush>();
    New->SetResourceObject(Texture);
    New->DrawAs = ESlateBrushDrawType::Image;
    New->SetUVRegion(FBox2f(FVector2f(U0, V0), FVector2f(U1, V1)));
    New->ImageSize = FVector2D(Texture->GetSizeX() * FMath::Abs(U1 - U0), Texture->GetSizeY() * FMath::Abs(V1 - V0));
    Brushes.Add(Key, New);
    return New.Get();
}

const FButtonStyle* SWowVendorWindow::TextureButtonStyle(const FString& Up, const FString& Down, const FString& Disabled,
    float U0, float U1, float V0, float V1) const
{
    const FString Key = Up + TEXT("|") + Down + TEXT("|") + Disabled + FString::Printf(TEXT("|%.3f%.3f%.3f%.3f"), U0, U1, V0, V1);
    if (const TSharedPtr<FButtonStyle>* Found = ButtonStyles.Find(Key)) return Found->Get();
    const FSlateBrush* Normal = Brush(Up, U0, U1, V0, V1);
    if (!Normal) return nullptr;
    const FSlateBrush* Pressed = Brush(Down, U0, U1, V0, V1);
    const FSlateBrush* Off = Brush(Disabled, U0, U1, V0, V1);
    TSharedPtr<FButtonStyle> Style = MakeShared<FButtonStyle>(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"));
    Style->SetNormal(*Normal);
    FSlateBrush Hovered = *Normal;
    Hovered.TintColor = WowColor::FromWow(1.25f, 1.25f, 1.25f);
    Style->SetHovered(Hovered);
    Style->SetPressed(Pressed ? *Pressed : *Normal);
    Style->SetDisabled(Off ? *Off : *Normal);
    Style->SetNormalPadding(FMargin(0));
    Style->SetPressedPadding(FMargin(0));
    ButtonStyles.Add(Key, Style);
    return Style.Get();
}

FSlateFontInfo SWowVendorWindow::Font(float WowPixels, bool bOutline) const
{
    // WoW font heights are pixels; Slate sizes are points (x 0.75).
    FSlateFontInfo Info = FWowWindowStyle::TitleFont(FMath::Max(6, FMath::RoundToInt(WowPixels * Scale * .75f)));
    if (bOutline)
    {
        Info.OutlineSettings.OutlineSize = 1;
        Info.OutlineSettings.OutlineColor = FLinearColor::Black;
    }
    return Info;
}

float SWowVendorWindow::TextWidth(const FString& Text, const FSlateFontInfo& FontInfo) const
{
    if (!FSlateApplication::IsInitialized()) return Text.Len() * FontInfo.Size * .6f;
    return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, FontInfo).X;
}

void SWowVendorWindow::Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const
{
    Canvas.AddSlot().Position(FVector2D(X, Y) * Scale).Size(FVector2D(W, H) * Scale)[Widget];
}

void SWowVendorWindow::PlaceImage(SCanvas& Canvas, float X, float Y, float W, float H, const FSlateBrush* Image,
    const FLinearColor& Tint) const
{
    if (!Image) return;
    Place(Canvas, X, Y, W, H, SNew(SImage).Image(Image).ColorAndOpacity(Tint).Visibility(EVisibility::HitTestInvisible));
}

void SWowVendorWindow::PlaceText(SCanvas& Canvas, float X, float Y, float W, float H, const FString& Text,
    const FLinearColor& Color, float WowPixels, EHorizontalAlignment HAlign, bool bOutline, bool bWrap) const
{
    if (Text.IsEmpty()) return;
    // r381 (Stefan 16.10: the vendor window's text and icons did a little hop when it refreshed):
    // a rebuilt AutoWrapText block is laid out unwrapped for its first frame and wraps on the next,
    // re-centring in its box. Wrap at the box width from the start instead.
    Place(Canvas, X, Y, W, H, SNew(SBox).HAlign(HAlign).VAlign(VAlign_Center).Visibility(EVisibility::HitTestInvisible)
        [ SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(WowPixels, bOutline)).ColorAndOpacity(Color)
            .WrapTextAt(bWrap ? W * Scale : 0.f).Justification(HAlign == HAlign_Right ? ETextJustify::Right : HAlign == HAlign_Center ? ETextJustify::Center : ETextJustify::Left)
            .ShadowOffset(bOutline ? FVector2D::ZeroVector : FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 1)) ]);
}

void SWowVendorWindow::PlaceMoney(SCanvas& Canvas, float X, float Y, uint32 Copper, bool bRight) const
{
    // SmallMoneyFrameTemplate: "n[gold] n[silver] n[copper]", leading zero denominations hidden.
    struct FCoin { uint32 Value; const TCHAR* Icon; };
    TArray<FCoin> Coins;
    const uint32 Gold = Copper / 10000, Silver = Copper / 100 % 100, Cu = Copper % 100;
    if (Gold) Coins.Add({Gold, TEXT("Interface\\MoneyFrame\\UI-GoldIcon")});
    if (Gold || Silver) Coins.Add({Silver, TEXT("Interface\\MoneyFrame\\UI-SilverIcon")});
    Coins.Add({Cu, TEXT("Interface\\MoneyFrame\\UI-CopperIcon")});
    const FSlateFontInfo MoneyFont = Font(12);
    constexpr float IconSize = 13.f, IconGap = 1.f, CoinGap = 4.f;
    float Total = 0.f;
    TArray<float> Widths;
    for (const FCoin& Coin : Coins)
    {
        Widths.Add(TextWidth(FString::FromInt(Coin.Value), MoneyFont) / Scale);
        Total += Widths.Last() + IconGap + IconSize;
    }
    Total += CoinGap * (Coins.Num() - 1);
    float Cursor = bRight ? X - Total : X;
    for (int32 Index = 0; Index < Coins.Num(); ++Index)
    {
        PlaceText(Canvas, Cursor, Y, Widths[Index] + 2.f, IconSize, FString::FromInt(Coins[Index].Value), HighlightFontColor, 12);
        Cursor += Widths[Index] + IconGap;
        PlaceImage(Canvas, Cursor, Y, IconSize, IconSize, Brush(Coins[Index].Icon));
        Cursor += IconSize + CoinGap;
    }
}

void SWowVendorWindow::PlaceItem(SCanvas& Canvas, float X, float Y, uint32 Entry, const FString& Name,
    const FLinearColor& NameColor, uint32 Price, uint32 Count, int32 Stock, bool bRedTint, bool bGreyTint, bool bBuyback,
    TFunction<void(const FPointerEvent&)> OnClick) const
{
    const FLinearColor SlotTint = bRedTint ? FLinearColor(1.f, 0.f, 0.f) : bGreyTint ? WowColor::FromWow(.5f, .5f, .5f) : FLinearColor::White;
    const FLinearColor PlateTint = bRedTint ? FLinearColor(1.f, 0.f, 0.f) : WowColor::FromWow(.5f, .5f, .5f);
    const FLinearColor IconTint = bRedTint ? WowColor::FromWow(.9f, 0.f, 0.f) : bGreyTint ? WowColor::FromWow(.5f, .5f, .5f) : FLinearColor::White;

    // $parentSlotTexture (UI-EmptySlot 64x64 at -13,-13) and $parentNameFrame (UI-Merchant-LabelSlots).
    PlaceImage(Canvas, X - 13, Y - 13, 64, 64, Brush(TEXT("Interface\\Buttons\\UI-EmptySlot")), Entry ? SlotTint : WowColor::FromWow(.4f, .4f, .4f));
    if (bBuyback)
        PlaceImage(Canvas, X + 42, Y - 3, 128, 64, Brush(TEXT("Interface\\MerchantFrame\\UI-Merchant-LabelSlots")), PlateTint);
    else
        PlaceImage(Canvas, X + 42, Y - 2, 128, 78, Brush(TEXT("Interface\\MerchantFrame\\UI-Merchant-LabelSlots")), PlateTint);
    if (!Entry) return;

    // Item name (GameFontNormalSmall, wraps in 100x30) and price (SmallMoneyFrame).
    PlaceText(Canvas, X + 46, bBuyback ? Y - 6 : Y - 3, bBuyback ? 90 : 100, 30, Name, NameColor, 10, HAlign_Left, false, true);
    if (Price) PlaceMoney(Canvas, bBuyback ? X + 42 : X + 44, bBuyback ? Y + 23 : Y + 32, Price, false);

    // ItemButtonTemplate: icon, UI-Quickslot2 frame, count bottom-right, stock top-left.
    const FString IconPath = FWowItemPresentation::IconPath(Entry);
    PlaceImage(Canvas, X, Y, 37, 37, Brush(IconPath), IconTint);
    PlaceImage(Canvas, X - 13.5f, Y - 12.5f, 64, 64, Brush(TEXT("Interface\\Buttons\\UI-Quickslot2")), IconTint);
    if (Count > 1) PlaceText(Canvas, X, Y + 21, 32, 14, FString::FromInt(Count), HighlightFontColor, 14, HAlign_Right, true);
    if (Stock >= 0) PlaceText(Canvas, X + 1, Y + 1, 36, 14, FString::FromInt(Stock), NormalFontColor, 14, HAlign_Left, true);

    // Hit area with highlight and the item tooltip.
    TSharedRef<TWeakPtr<SBorder>> Holder = MakeShared<TWeakPtr<SBorder>>();
    TSharedPtr<SBorder> Hit;
    Place(Canvas, X, Y, 37, 37, SAssignNew(Hit, SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
        .ToolTip(FWowItemPresentation::Tooltip(Connection.Get(), Entry))
        .OnMouseButtonDown_Lambda([OnClick](const FGeometry&, const FPointerEvent& Event)
        {
            if (OnClick) OnClick(Event);
            return FReply::Handled();
        })
        [
            SNew(SWowAddImage).Image(Brush(TEXT("Interface\\Buttons\\ButtonHilight-Square"))).ColorAndOpacity(FLinearColor(1, 1, 1, .5f))
            .Visibility_Lambda([Holder]()
            {
                const TSharedPtr<SBorder> Border = Holder->Pin();
                return Border.IsValid() && Border->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Hidden;
            })
        ]);
    *Holder = Hit;
}

void SWowVendorWindow::PlaceCheckBox(SCanvas& Canvas, float X, float Y, const FString& Label, const FString& Option) const
{
    // UICheckButtonTemplate look, 20x20 with the label to the right (GameFontNormalSmall).
    const bool bChecked = FWowClientPreferences::GetWorldOption(Option);
    PlaceImage(Canvas, X, Y, 20, 20, Brush(TEXT("Interface\\Buttons\\UI-CheckBox-Up")));
    if (bChecked) PlaceImage(Canvas, X, Y, 20, 20, Brush(TEXT("Interface\\Buttons\\UI-CheckBox-Check")));
    PlaceText(Canvas, X + 20, Y + 4, 140, 12, Label, NormalFontColor, 10, HAlign_Left);
    TSharedRef<TWeakPtr<SBorder>> Holder = MakeShared<TWeakPtr<SBorder>>();
    TSharedPtr<SBorder> Hit;
    SWowVendorWindow* Self = const_cast<SWowVendorWindow*>(this);
    Place(Canvas, X, Y, 20 + TextWidth(Label, Font(10)) / Scale + 4, 20, SAssignNew(Hit, SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0).HAlign(HAlign_Left)
        .OnMouseButtonDown_Lambda([Self, Option](const FGeometry&, const FPointerEvent& Event)
        {
            if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Handled();
            // One toggle per physical click (the press can reach this handler twice).
            static double LastToggle = 0.0;
            const double Now = FPlatformTime::Seconds();
            if (Now - LastToggle < 0.3) return FReply::Handled();
            LastToggle = Now;
            const bool bNow = !FWowClientPreferences::GetWorldOption(Option);
            FWowClientPreferences::SetWorldOption(Option, bNow);
            UE_LOG(LogTemp, Log, TEXT("Vendor option %s -> %d (stored %d)"), *Option, bNow ? 1 : 0,
                FWowClientPreferences::GetWorldOption(Option) ? 1 : 0);
            // Turning an option on while the window is open applies it right away.
            if (bNow && Option == TEXT("autosellgrey")) Self->SellGreyItems();
            if (bNow && Option == TEXT("autorepair")) Self->RepairIfDamaged();
            Self->Rebuild();
            return FReply::Handled();
        })
        [
            SNew(SBox).WidthOverride(20 * Scale).HeightOverride(20 * Scale)
            [
                SNew(SWowAddImage).Image(Brush(TEXT("Interface\\Buttons\\UI-CheckBox-Highlight")))
                .Visibility_Lambda([Holder]()
                {
                    const TSharedPtr<SBorder> Border = Holder->Pin();
                    return Border.IsValid() && Border->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Hidden;
                })
            ]
        ]);
    *Holder = Hit;
}

TSharedRef<SWidget> SWowVendorWindow::BuildFrame()
{
    // Button tooltips drawn inside the window (GameTooltip look) instead of Slate tool-tip windows,
    // which stayed stuck on the cursor until the client was closed (Stefan 10.10).
    // Placed last (below), so they draw over the frame art (Stefan 10.10: the tip lay behind the
    // bottom border).
    TArray<TFunction<void()>> DeferredTips;
    auto HoverTip = [this, &DeferredTips](SCanvas& Canvas, TSharedPtr<SWidget> Target, float X, float Y, const FString& Text)
    {
        if (!Target.IsValid()) return;
        TWeakPtr<SWidget> Weak = Target;
        SCanvas* CanvasPtr = &Canvas;
        DeferredTips.Add([this, CanvasPtr, Weak, X, Y, Text]()
        {
        Place(*CanvasPtr, X, Y, 230, 44, SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Top)
            .Visibility_Lambda([Weak]() { const TSharedPtr<SWidget> W = Weak.Pin(); return W.IsValid() && W->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
            [ SNew(SWowTooltipFrame).Padding(FMargin(8 * Scale, 6 * Scale))
              [ SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(10)).ColorAndOpacity(FLinearColor::White).AutoWrapText(true) ] ]);
        });
    };
    TSharedRef<SCanvas> Canvas = SNew(SCanvas);
    UWowConnectionManager* C = Connection.Get();
    const FWowPlayerEntity* Player = C ? C->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
    const bool bBuybackTab = Tab == 1;

    // Portrait (BACKGROUND): the NPC portrait; the buyback tab shows UI-BuyBack-Icon.
    if (bBuybackTab)
    {
        PlaceImage(*Canvas, 7, 6, 60, 60, Brush(TEXT("Interface\\MerchantFrame\\UI-BuyBack-Icon")));
    }
    else
    {
        if (!PortraitBrush.IsValid() && UIManager.IsValid() && UIManager->GetFrameManager()
            && UIManager->GetFrameManager()->UnitPortraitProvider)
        {
            if (UMaterialInterface* Material = UIManager->GetFrameManager()->UnitPortraitProvider(CurrentVendorGuid))
            {
                PortraitBrush = MakeShared<FWowRetainedSlateBrush>();
                PortraitBrush->SetResourceObject(Material);
                PortraitBrush->DrawAs = ESlateBrushDrawType::Image;
                PortraitBrush->ImageSize = FVector2D(60, 60);
            }
        }
        if (PortraitBrush.IsValid()) PlaceImage(*Canvas, 7, 6, 60, 60, PortraitBrush.Get());
    }

    // Frame art (BORDER).
    PlaceImage(*Canvas, 0, 0, 256, 256, Brush(TEXT("Interface\\MerchantFrame\\UI-Merchant-TopLeft")));
    PlaceImage(*Canvas, 256, 0, 128, 256, Brush(TEXT("Interface\\MerchantFrame\\UI-Merchant-TopRight")));
    PlaceImage(*Canvas, 0, 256, 256, 256, Brush(TEXT("Interface\\MerchantFrame\\UI-Merchant-BotLeft")));
    PlaceImage(*Canvas, 256, 256, 128, 256, Brush(TEXT("Interface\\MerchantFrame\\UI-Merchant-BotRight")));

    // Buyback art (ARTWORK), only on the Buyback tab.
    if (bBuybackTab)
    {
        PlaceImage(*Canvas, 19, 71, 256, 256, Brush(TEXT("Interface\\MerchantFrame\\UI-BuyBack-TopLeft")));
        PlaceImage(*Canvas, 275, 71, 64, 256, Brush(TEXT("Interface\\MerchantFrame\\UI-BuyBack-TopRight")));
        PlaceImage(*Canvas, 19, 327, 256, 128, Brush(TEXT("Interface\\MerchantFrame\\UI-BuyBack-BotLeft")));
        PlaceImage(*Canvas, 275, 327, 64, 128, Brush(TEXT("Interface\\MerchantFrame\\UI-BuyBack-BotRight")));
    }

    // Title: NPC name, or "Buyback".
    FString Title = TEXT("Buyback");
    if (!bBuybackTab)
    {
        Title = TEXT("Merchant");
        if (C)
            if (const FWowEntity* Entity = C->PacketHandler.EntityManager.Find(CurrentVendorGuid))
                if (const FString* Cached = C->PacketHandler.CreatureNameCache.Find(Entity->Entry)) Title = *Cached;
    }
    // MerchantNameText: GameFontNormal, TOP of the frame centred, 17 down (inside the title bar).
    PlaceText(*Canvas, 72, 17, 240, 14, Title, NormalFontColor, 12, HAlign_Center);

    // Grey-item selling and repair automation (not in the original frame; uses its free
    // strip between the title and the item grid).
    if (!bBuybackTab)
    {
        PlaceCheckBox(*Canvas, 74, 38, TEXT("Auto-sell grey items"), TEXT("autosellgrey"));
        if (CanRepair()) PlaceCheckBox(*Canvas, 214, 38, TEXT("Auto-repair"), TEXT("autorepair"));
        // UIPanelButtonTemplate.
        bool bHasGrey = false;
        ForEachBagItem([&](uint64 Guid, const FWowItemEntity& Item)
        {
            const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(C, Item.Entry);
            // A grey item bought back carries its old GUID: only a sale still in flight counts as sold
            // (Stefan 10.10: after buying a grey item back the button stayed greyed out).
            const double* SoldAt = GVendorSoldAt.Find(Guid);
            const bool bSaleInFlight = SoldGuids.Contains(Guid) && SoldAt && FPlatformTime::Seconds() - *SoldAt < 3.0;
            if (Info && Info->Quality == 0 && Info->SellPrice > 0 && !bSaleInFlight) bHasGrey = true;
        });
        if (const FButtonStyle* SellStyle = TextureButtonStyle(TEXT("Interface\\Buttons\\UI-Panel-Button-Up"),
            TEXT("Interface\\Buttons\\UI-Panel-Button-Down"), TEXT("Interface\\Buttons\\UI-Panel-Button-Disabled"),
            0.f, .625f, 0.f, .6875f))
        {
            // Bottom right, beside the Merchant/Buyback tabs.
            TSharedPtr<SButton> SellButton;
            Place(*Canvas, 232, 452, 106, 22, SAssignNew(SellButton, SButton).ButtonStyle(SellStyle).ClickMethod(EButtonClickMethod::MouseDown)
                .IsEnabled(bHasGrey).ContentPadding(0).HAlign(HAlign_Center).VAlign(VAlign_Center)
                .OnClicked_Lambda([this]() { SellGreyItems(); Rebuild(); return FReply::Handled(); })
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Sell Grey Items"))).Font(Font(10))
                    .ColorAndOpacity(bHasGrey ? NormalFontColor : WowColor::FromWow(.5f, .5f, .5f))
                    .ShadowOffset(FVector2D(1, -1) * Scale).ShadowColorAndOpacity(FLinearColor::Black)
                ]);
            HoverTip(*Canvas, SellButton, 232, 476, TEXT("Sell all poor-quality (grey) items in your bags."));
        }
    }

    auto Buy = [this](const FWowVendorItem& Item)
    {
        return [this, Item](const FPointerEvent& Event)
        {
            if (Event.GetEffectingButton() == EKeys::LeftMouseButton || Event.GetEffectingButton() == EKeys::RightMouseButton)
                OnBuyItem.ExecuteIfBound(CurrentVendorGuid, Item.ItemId, 1, Item.Slot);
        };
    };

    if (!bBuybackTab)
    {
        // Merchant items, 10 per page in two columns.
        for (int32 Index = 0; Index < MerchantItemsPerPage; ++Index)
        {
            const float X = ItemX + (Index % 2) * (ItemW + ItemGapX);
            const float Y = ItemY + (Index / 2) * (ItemH + 8.f);
            const int32 ItemIndex = Page * MerchantItemsPerPage + Index;
            if (!CurrentItems.IsValidIndex(ItemIndex))
            {
                PlaceItem(*Canvas, X, Y, 0, FString(), FLinearColor::White, 0, 0, -1, false, false, false, nullptr);
                continue;
            }
            const FWowVendorItem& Item = CurrentItems[ItemIndex];
            const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(C, Item.ItemId);
            const FString Name = Info ? Info->Name : TEXT("Retrieving item information");
            const FLinearColor NameColor = Info ? FWowInventoryManager::GetQualityColor(static_cast<uint8>(Info->Quality)) : NormalFontColor;
            const bool bUsable = !Info || !Player || static_cast<int64>(Info->RequiredLevel) <= static_cast<int64>(Player->GetLevel());
            const int32 Stock = Item.MaxCount == MAX_uint32 ? -1 : static_cast<int32>(Item.MaxCount);
            PlaceItem(*Canvas, X, Y, Item.ItemId, Name, NameColor, Item.Price, FMath::Max(1u, Item.BuyCount), Stock,
                !bUsable && Stock != 0, Stock == 0, false, Buy(Item));
        }

        // Paging (only with more than one page).
        if (CurrentItems.Num() > MerchantItemsPerPage)
        {
            PlaceText(*Canvas, 126, 348, 104, 14, FString::Printf(TEXT("Page %d of %d"), Page + 1, NumPages()), NormalFontColor, 12, HAlign_Center);
            PlaceImage(*Canvas, 21, 339, 32, 32, Brush(TEXT("Interface\\Buttons\\UI-PageButton-Background")));
            PlaceImage(*Canvas, 308, 339, 32, 32, Brush(TEXT("Interface\\Buttons\\UI-PageButton-Background")));
            PlaceText(*Canvas, 53, 348, 60, 14, TEXT("Prev"), NormalFontColor, 12, HAlign_Left);
            PlaceText(*Canvas, 245, 348, 60, 14, TEXT("Next"), NormalFontColor, 12, HAlign_Right);
            if (const FButtonStyle* PrevStyle = TextureButtonStyle(TEXT("Interface\\Buttons\\UI-SpellbookIcon-PrevPage-Up"),
                TEXT("Interface\\Buttons\\UI-SpellbookIcon-PrevPage-Down"), TEXT("Interface\\Buttons\\UI-SpellbookIcon-PrevPage-Disabled")))
                Place(*Canvas, 21, 340, 32, 32, SNew(SButton).ButtonStyle(PrevStyle).ClickMethod(EButtonClickMethod::MouseDown)
                    .IsEnabled(Page > 0).OnClicked_Lambda([this]() { Page = FMath::Max(0, Page - 1); Rebuild(); return FReply::Handled(); }));
            if (const FButtonStyle* NextStyle = TextureButtonStyle(TEXT("Interface\\Buttons\\UI-SpellbookIcon-NextPage-Up"),
                TEXT("Interface\\Buttons\\UI-SpellbookIcon-NextPage-Down"), TEXT("Interface\\Buttons\\UI-SpellbookIcon-NextPage-Disabled")))
                Place(*Canvas, 308, 340, 32, 32, SNew(SButton).ButtonStyle(NextStyle).ClickMethod(EButtonClickMethod::MouseDown)
                    .IsEnabled(Page + 1 < NumPages()).OnClicked_Lambda([this]() { Page = FMath::Min(NumPages() - 1, Page + 1); Rebuild(); return FReply::Handled(); }));
        }

        // Bottom border strip (OVERLAY).
        PlaceImage(*Canvas, 14, 366, 256, 61, Brush(TEXT("Interface\\MerchantFrame\\UI-Merchant-BottomBorder"), 0.f, 1.f, 0.f, .4765625f));
        PlaceImage(*Canvas, 270, 366, 76, 61, Brush(TEXT("Interface\\MerchantFrame\\UI-Merchant-BottomBorder"), 0.f, .296875f, .4765625f, .953125f));

        // Repair buttons (armorers only).
        if (CanRepair())
        {
            PlaceText(*Canvas, 26, 395, 120, 14, TEXT("Repair Items"), HighlightFontColor, 10, HAlign_Left);
            // MerchantFrame_UpdateRepairButtons: with no repair cost the buttons are
            // desaturated and do nothing (no highlight either).
            const bool bCanRepairNow = IsAnythingDamaged();
            auto RepairButton = [this, &Canvas, bCanRepairNow, &HoverTip](float X, float U0, float U1, const FString& Tip)
            {
                PlaceImage(*Canvas, X, 385, 36, 36, Brush(TEXT("Interface\\MerchantFrame\\UI-Merchant-RepairIcons"), U0, U1, 0.f, .5625f),
                    bCanRepairNow ? FLinearColor::White : WowColor::FromWow(.4f, .4f, .4f));
                TSharedRef<TWeakPtr<SBorder>> Holder = MakeShared<TWeakPtr<SBorder>>();
                TSharedPtr<SBorder> Hit;
                Place(*Canvas, X, 385, 36, 36, SAssignNew(Hit, SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
                    .OnMouseButtonDown_Lambda([this, bCanRepairNow](const FGeometry&, const FPointerEvent&)
                    {
                        // The client's single-item repair cursor is not implemented yet; both
                        // buttons repair everything (CMSG_REPAIR_ITEM with item guid 0).
                        if (bCanRepairNow && Connection.IsValid()) Connection->SendRepairItem(CurrentVendorGuid, 0, false);
                        return FReply::Handled();
                    })
                    [
                        SNew(SWowAddImage).Image(Brush(TEXT("Interface\\Buttons\\ButtonHilight-Square"))).ColorAndOpacity(FLinearColor(1, 1, 1, .5f))
                        .Visibility_Lambda([Holder, bCanRepairNow]()
                        {
                            const TSharedPtr<SBorder> Border = Holder->Pin();
                            return bCanRepairNow && Border.IsValid() && Border->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Hidden;
                        })
                    ]);
                *Holder = Hit;
                HoverTip(*Canvas, Hit, X + 38, 385, Tip);
            };
            RepairButton(98, 0.f, .28125f, TEXT("Repair an Item"));
            RepairButton(136, .28125f, .5625f, TEXT("Repair All Items"));
        }

        // Last sold item (MerchantBuyBackItem), bought back with a click.
        if (Player && C)
        {
            int32 Latest = INDEX_NONE;
            for (int32 Index = 0; Index < BuybackItemsPerPage; ++Index)
                if (Player->GetField64(PlayerField::BUYBACK_SLOT_START + Index * 2) &&
                    (Latest == INDEX_NONE || Player->GetField(PlayerField::BUYBACK_TIME_START + Index) > Player->GetField(PlayerField::BUYBACK_TIME_START + Latest)))
                    Latest = Index;
            const uint64 Guid = Latest != INDEX_NONE ? Player->GetField64(PlayerField::BUYBACK_SLOT_START + Latest * 2) : 0;
            const auto* Item = Guid ? C->PacketHandler.EntityManager.FindItem(Guid) : nullptr;
            if (Item)
            {
                C->RequestItemInfo(Item->Entry);
                const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(C, Item->Entry);
                PlaceItem(*Canvas, 189, 385, Item->Entry, Info ? Info->Name : FString(),
                    Info ? FWowInventoryManager::GetQualityColor(static_cast<uint8>(Info->Quality)) : NormalFontColor,
                    Player->GetField(PlayerField::BUYBACK_PRICE_START + Latest), Item->GetStackCount(), -1, false, false, true,
                    [this, Latest, Guid](const FPointerEvent&) { if (Connection.IsValid()) Connection->SendBuybackItem(CurrentVendorGuid, 74 + Latest, Guid); });
            }
            else
            {
                PlaceItem(*Canvas, 189, 385, 0, FString(), FLinearColor::White, 0, 0, -1, false, false, true, nullptr);
            }
        }
    }
    else if (Player && C)
    {
        // Buyback: 12 slots, rows 15 apart.
        for (int32 Index = 0; Index < BuybackItemsPerPage; ++Index)
        {
            const float X = ItemX + (Index % 2) * (ItemW + ItemGapX);
            const float Y = ItemY + (Index / 2) * (ItemH + 15.f);
            const uint64 Guid = Player->GetField64(PlayerField::BUYBACK_SLOT_START + Index * 2);
            const auto* Item = Guid ? C->PacketHandler.EntityManager.FindItem(Guid) : nullptr;
            if (!Item)
            {
                PlaceItem(*Canvas, X, Y, 0, FString(), FLinearColor::White, 0, 0, -1, false, false, false, nullptr);
                continue;
            }
            C->RequestItemInfo(Item->Entry);
            const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(C, Item->Entry);
            PlaceItem(*Canvas, X, Y, Item->Entry, Info ? Info->Name : FString(),
                Info ? FWowInventoryManager::GetQualityColor(static_cast<uint8>(Info->Quality)) : NormalFontColor,
                Player->GetField(PlayerField::BUYBACK_PRICE_START + Index), Item->GetStackCount(), -1, false, false, false,
                [this, Index, Guid](const FPointerEvent&) { if (Connection.IsValid()) Connection->SendBuybackItem(CurrentVendorGuid, 74 + Index, Guid); });
        }
    }

    // Player money (MerchantMoneyFrame, BOTTOMRIGHT -40, 67).
    PlaceMoney(*Canvas, 344, 432, Player ? Player->GetCoinage() : 0, true);

    // Close button (UIPanelCloseButton, TOPRIGHT -30, -8).
    if (const FButtonStyle* CloseStyle = TextureButtonStyle(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"),
        TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Down")))
        Place(*Canvas, 322, 8, 32, 32, SNew(SButton).ButtonStyle(CloseStyle).ClickMethod(EButtonClickMethod::MouseDown)
            .OnClicked_Lambda([this]() { CloseVendor(); OnCloseVendor.ExecuteIfBound(); return FReply::Handled(); }));

    // Merchant / Buyback tabs (CharacterFrameTabButtonTemplate), Tab1 centred at BOTTOMLEFT (60, 46).
    {
        const FSlateFontInfo TabFont = Font(10);
        const FString Labels[2] = { TEXT("Merchant"), TEXT("Buyback") };
        const float W0 = TextWidth(Labels[0], TabFont) / Scale + 40.f;
        float X = 60.f - W0 * .5f;
        const float Y = MerchantH - 46.f - 16.f;
        for (int32 Index = 0; Index < 2; ++Index)
        {
            const bool bSelected = Index == Tab;
            const float W = TextWidth(Labels[Index], TabFont) / Scale + 40.f;
            const FString Art = bSelected ? TEXT("Interface\\PaperDollInfoFrame\\UI-Character-ActiveTab") : TEXT("Interface\\PaperDollInfoFrame\\UI-Character-InActiveTab");
            const float ArtY = bSelected ? Y - 5.f : Y;
            PlaceImage(*Canvas, X, ArtY, 20, 32, Brush(Art, 0.f, .15625f, 0.f, 1.f));
            PlaceImage(*Canvas, X + 20, ArtY, W - 40, 32, Brush(Art, .15625f, .84375f, 0.f, 1.f));
            PlaceImage(*Canvas, X + W - 20, ArtY, 20, 32, Brush(Art, .84375f, 1.f, 0.f, 1.f));
            PlaceText(*Canvas, X, Y + 6, W, 16, Labels[Index], bSelected ? HighlightFontColor : NormalFontColor, 10, HAlign_Center);
            TSharedRef<TWeakPtr<SBorder>> Holder = MakeShared<TWeakPtr<SBorder>>();
            TSharedPtr<SBorder> Hit;
            Place(*Canvas, X, Y, W, 32, SAssignNew(Hit, SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(FMargin(10 * Scale, 0))
                .OnMouseButtonDown_Lambda([this, Index](const FGeometry&, const FPointerEvent& Event)
                {
                    if (Event.GetEffectingButton() == EKeys::LeftMouseButton && Tab != Index) { Tab = Index; Rebuild(); }
                    return FReply::Handled();
                })
                [
                    SNew(SWowAddImage).Image(Brush(TEXT("Interface\\PaperDollInfoFrame\\UI-Character-Tab-Highlight"))).ColorAndOpacity(FLinearColor(1, 1, 1, .6f))
                    .Visibility_Lambda([Holder, bSelected]()
                    {
                        const TSharedPtr<SBorder> Border = Holder->Pin();
                        return !bSelected && Border.IsValid() && Border->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Hidden;
                    })
                ]);
            *Holder = Hit;
            X += W - 16.f;
        }
    }

    for (const TFunction<void()>& PlaceTip : DeferredTips) PlaceTip();
    // The frame swallows clicks inside its art (HitRectInsets right 35, bottom 61).
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
        .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
        [ Canvas ];
}
