#include "Widgets/SWowLootWindow.h"
#include "WowColor.h"
#include "WowWindowStyle.h"
#include "WowItemPresentation.h"
#include "WowRetainedSlateBrush.h"
#include "WowNativeWidgetStyle.h"
#include "WowConnectionManager.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SToolTip.h"
#include "Styling/CoreStyle.h"

namespace
{
// Loot slot as in the 3.3.5 LootFrame: 37px item button and the name beside it
// on a dark plate. Hover lights the whole slot like the original highlight.
const FButtonStyle& LootSlotStyle()
{
    static const FButtonStyle Style = []()
    {
        FButtonStyle Value = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder");
        Value.SetNormal(FSlateNoResource());
        Value.SetHovered(FSlateColorBrush(WowColor::FromWow(1.f, .82f, .2f, .16f)));
        Value.SetPressed(FSlateColorBrush(WowColor::FromWow(1.f, .82f, .2f, .28f)));
        Value.SetDisabled(FSlateNoResource());
        Value.SetNormalPadding(FMargin(0));
        Value.SetPressedPadding(FMargin(0));
        return Value;
    }();
    return Style;
}

FLinearColor QualityColor(uint32 Quality)
{
    // ITEM_QUALITY_COLORS (sRGB) from the original client.
    static const FColor Colors[] = { FColor(157,157,157), FColor(255,255,255), FColor(30,255,0),
        FColor(0,112,221), FColor(163,53,238), FColor(255,128,0), FColor(230,204,128), FColor(230,204,128) };
    return FLinearColor::FromSRGBColor(Colors[FMath::Min<uint32>(Quality, UE_ARRAY_COUNT(Colors) - 1)]);
}

FString MoneyName(uint32 Copper)
{
    // The original money slot reads "1 Gold 2 Silver 5 Copper", leaving out zero parts.
    TArray<FString> Parts;
    if (Copper / 10000) Parts.Add(FString::Printf(TEXT("%u Gold"), Copper / 10000));
    if (Copper / 100 % 100) Parts.Add(FString::Printf(TEXT("%u Silver"), Copper / 100 % 100));
    if (Copper % 100 || Parts.IsEmpty()) Parts.Add(FString::Printf(TEXT("%u Copper"), Copper % 100));
    return FString::Join(Parts, TEXT(" "));
}

const TCHAR* MoneyIcon(uint32 Copper)
{
    // Coin pile icon by size of the amount (gold / silver / copper coins).
    if (Copper >= 10000) return TEXT("Interface\\Icons\\INV_Misc_Coin_01");
    if (Copper >= 100) return TEXT("Interface\\Icons\\INV_Misc_Coin_03");
    return TEXT("Interface\\Icons\\INV_Misc_Coin_05");
}
}

void SWowLootWindow::SetContext(UWowConnectionManager* InConnection, UWowUIManager* UI)
{ Connection = InConnection; UIManager = UI; }

void SWowLootWindow::Construct(const FArguments& InArgs)
{
    OnLootItem = InArgs._OnLootItem; OnLootAll = InArgs._OnLootAll; OnCloseLoot = InArgs._OnCloseLoot;
    ChildSlot
    [ SNew(SBox).WidthOverride(236).MaxDesiredHeight(420)
      [ SNew(SWowWindowFrame)
        .Padding(3)
        [ SNew(SBorder).BorderImage(FWowWindowStyle::Background()).Padding(FMargin(8, 5, 8, 8))
          [ SNew(SVerticalBox)
            // Title bar: "Items" in gold like the original LootFrame, close button right.
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [ SNew(SOverlay)
              + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
              [ SNew(STextBlock).Text(FText::FromString(TEXT("Items")))
                .Font(FWowWindowStyle::TitleFont(14)).ColorAndOpacity(FWowWindowStyle::Gold())
                .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
              + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center)
              [ SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).ContentPadding(FMargin(6, 0))
                .ClickMethod(EButtonClickMethod::MouseDown)
                .Text(FText::FromString(TEXT("X"))).OnClicked(this, &SWowLootWindow::OnCloseClicked) ] ]
            + SVerticalBox::Slot().AutoHeight()
            [ SAssignNew(ItemScrollBox, SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always)
              + SScrollBox::Slot()[ SAssignNew(ItemList, SVerticalBox) ] ] ] ] ] ];
    SetVisibility(EVisibility::Collapsed);
}

const FSlateBrush* SWowLootWindow::IconByPath(const FString& TexturePath)
{
    if (const auto* Brush = IconBrushes.Find(TexturePath)) return Brush->Get();
    if (!UIManager.IsValid() || !UIManager->GetFrameManager()) return FCoreStyle::Get().GetBrush("NoBrush");
    UTexture2D* Texture = UIManager->GetFrameManager()->LoadUITexture(TexturePath);
    auto Brush = MakeShared<FWowRetainedSlateBrush>(); Brush->SetResourceObject(Texture);
    Brush->DrawAs = ESlateBrushDrawType::Image; Brush->ImageSize = FVector2D(37, 37);
    IconBrushes.Add(TexturePath, Brush); return &Brush.Get();
}

const FSlateBrush* SWowLootWindow::Icon(uint32 Entry, uint32 DisplayId)
{
    return IconByPath(FWowItemPresentation::IconPath(Entry, DisplayId));
}

void SWowLootWindow::Tick(const FGeometry& Geometry, double Time, float Delta)
{
    SCompoundWidget::Tick(Geometry,Time,Delta);
    if (CurrentLootGuid && Connection.IsValid() && LastRevision != Connection->PacketHandler.VendorRevision)
    {
        LastRevision = Connection->PacketHandler.VendorRevision;
        RefreshItems();
    }
}

TSharedRef<SWidget> SWowLootWindow::MakeSlot(const FSlateBrush* IconBrush, const FText& Name, const FLinearColor& NameColor,
    uint32 Count, bool bEnabled, TSharedPtr<SButton>& OutButton, FOnClicked OnClicked, TSharedPtr<IToolTip> ToolTip)
{
    return SAssignNew(OutButton, SButton).ButtonStyle(&LootSlotStyle()).ContentPadding(FMargin(2))
        .IsEnabled(bEnabled).ToolTip(ToolTip).OnClicked(OnClicked)
        // Loot on press like the original client; a release-click could be lost to the world handler.
        .ClickMethod(EButtonClickMethod::MouseDown)
        [ SNew(SHorizontalBox)
          // Item button: icon framed by a dark slot edge, quality tint on the edge.
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
          [ SNew(SBox).WidthOverride(39).HeightOverride(39)
            [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).Padding(1)
              .BorderBackgroundColor(FLinearColor(NameColor.R * .55f, NameColor.G * .55f, NameColor.B * .55f, 1.f))
              [ SNew(SOverlay)
                + SOverlay::Slot()[ SNew(SImage).Image(IconBrush)
                    .ColorAndOpacity(bEnabled ? FLinearColor::White : WowColor::FromWow(.45f, .45f, .45f)) ]
                + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0, 0, 2, 0)
                [ SNew(STextBlock).Font(FWowWindowStyle::TitleFont(12)).ShadowOffset(FVector2D(1, 1))
                  .ShadowColorAndOpacity(FLinearColor::Black).ColorAndOpacity(FLinearColor::White)
                  .Text(FText::AsNumber(Count)).Visibility(Count > 1 ? EVisibility::HitTestInvisible : EVisibility::Collapsed) ] ] ] ]
          // Name plate.
          + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(3, 0, 0, 0)
          [ SNew(SBox).HeightOverride(35)
            [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).VAlign(VAlign_Center)
              .BorderBackgroundColor(FLinearColor::FromSRGBColor(FColor(28, 22, 14, 230))).Padding(FMargin(6, 1))
              // Stefan 11.10: "Forest Mushroom Cap" was cut on its second line. A smaller size with
              // tighter lines: two lines fit the 35-unit plate (GameFontNormalSmall-like).
              [ SNew(STextBlock).Font(FWowWindowStyle::TitleFont(10)).AutoWrapText(true).LineHeightPercentage(0.9f)
                .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black)
                .ColorAndOpacity(bEnabled ? NameColor : WowColor::FromWow(.5f, .5f, .5f)).Text(Name) ] ] ] ];
}

void SWowLootWindow::RefreshItems()
{
    ItemList->ClearChildren();
    FirstLootButton.Reset();
    GoldButton.Reset();

    // Money is its own slot, first in the list, exactly like the original window.
    if (CurrentGold)
    {
        ItemList->AddSlot().AutoHeight().Padding(0, 2)
        [ MakeSlot(IconByPath(MoneyIcon(CurrentGold)), FText::FromString(MoneyName(CurrentGold)), FLinearColor::White,
            1, true, GoldButton, FOnClicked::CreateLambda([this]()
            {
                if (FSlateApplication::Get().GetModifierKeys().IsShiftDown()) return OnLootAllClicked();
                if (Connection.IsValid() && CurrentLootGuid && CurrentGold)
                    Connection->SendRawPacket(WowOpcode::CMSG_LOOT_MONEY);
                return FReply::Handled();
            }), nullptr) ];
    }

    for (const FWowLootItem& Item : CurrentItems)
    {
        if (Item.bLooted) continue;
        const auto* Info = FWowItemPresentation::Info(Connection.Get(),Item.ItemId);
        const uint32 Quality = Info ? Info->Quality : Item.Quality;
        const bool bLootable = Item.SlotType == 0 || Item.SlotType == 4;
        TSharedPtr<SButton> Row;
        ItemList->AddSlot().AutoHeight().Padding(0, 2)
        [ MakeSlot(Icon(Item.ItemId, Item.DisplayId), FText::FromString(FWowItemPresentation::Name(Connection.Get(), Item.ItemId)),
            QualityColor(Quality), Item.Count, bLootable, Row,
            FOnClicked::CreateLambda([this, Item]() { return OnLootItemClicked(Item.Index); }),
            FWowItemPresentation::Tooltip(Connection.Get(), Item.ItemId)) ];
        if (!FirstLootButton && bLootable) FirstLootButton = Row;
    }
}

void SWowLootWindow::UpdateLoot(uint64 Guid, uint8 Type, uint32 Gold, const TArray<FWowLootItem>& Items)
{
    if (CurrentLootGuid != Guid && ItemScrollBox) ItemScrollBox->ScrollToStart();
    CurrentLootGuid = Guid; CurrentGold = Gold; CurrentItems = Items;
    for (const auto& Item : Items) if (Connection.IsValid()) Connection->RequestItemInfo(Item.ItemId);
    RefreshItems(); SetVisibility(EVisibility::Visible);
}
void SWowLootWindow::CloseLoot()
{
    CurrentLootGuid = 0; CurrentGold = 0; CurrentItems.Reset();
    ItemList->ClearChildren(); FirstLootButton.Reset(); GoldButton.Reset(); SetVisibility(EVisibility::Collapsed);
}
FReply SWowLootWindow::OnLootAllClicked()
{
    if (OnLootAll.IsBound()) OnLootAll.Execute();
    return FReply::Handled();
}
FReply SWowLootWindow::OnCloseClicked()
{
    CloseLoot(); if (OnCloseLoot.IsBound()) OnCloseLoot.Execute(); return FReply::Handled();
}
FReply SWowLootWindow::OnLootItemClicked(int32 Slot)
{
    if (FSlateApplication::Get().GetModifierKeys().IsShiftDown()) return OnLootAllClicked();
    if (OnLootItem.IsBound()) OnLootItem.Execute(Slot);
    // Keep the row until the server confirms success (inventory may be full).
    return FReply::Handled();
}
FText SWowLootWindow::GetGoldText(uint32 Gold) const
{ return FText::FromString(MoneyName(Gold)); }

bool SWowLootWindow::GetFirstLootAbsoluteAnchor(FVector2D& OutAbsolute) const
{
    const TSharedPtr<SButton> Button = GoldButton ? GoldButton : FirstLootButton;
    if (!Button) return false;
    const FGeometry& Geometry = Button->GetCachedGeometry();
    const FVector2D Size = Geometry.GetLocalSize();
    if (Size.X <= 1.f || Size.Y <= 1.f) return false;
    // Over the item icon (left part of the row), vertically centred.
    OutAbsolute = Geometry.LocalToAbsolute(FVector2D(FMath::Min(20.0f, Size.X * 0.5f), Size.Y * 0.5f));
    return true;
}

FVector2D SWowLootWindow::GetFirstLootAnchor() const
{
    // The first slot in the window (money when present, else the first lootable item)
    // opens under the cursor.
    const TSharedPtr<SButton> Button = GoldButton ? GoldButton : FirstLootButton;
    if (!Button) return FVector2D(20, 20);
    // Arrange from measured Slate sizes, not cached geometry from the previous
    // corpse. Title, money row, font metrics and UI scaling all affect this point.
    const FGeometry Root = FGeometry::MakeRoot(GetDesiredSize(), FSlateLayoutTransform());
    const FGeometry Row = FindChildGeometry(Root, Button.ToSharedRef());
    return Root.AbsoluteToLocal(Row.LocalToAbsolute(FVector2D(
        FMath::Min(20.0f, Row.GetLocalSize().X * 0.5f), Row.GetLocalSize().Y * 0.5f)));
}
