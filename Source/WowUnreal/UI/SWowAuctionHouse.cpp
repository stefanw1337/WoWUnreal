#include "SWowAuctionHouse.h"
#include "WowColor.h"
#include "WowTextCentering.h"
#include "WowWindowStyle.h"
#include "WowItemPresentation.h"
#include "WowRetainedSlateBrush.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "Materials/MaterialInterface.h"
#include "WowAudioManager.h"
#include "WowNetwork/Public/WowConnectionManager.h"
#include "WowNetwork/Public/WowPacketHandler.h"
#include "WowNetwork/Public/WowNpcServices.h"
#include "WowUpdateFields.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WowAuctionHouse"

namespace
{
constexpr uint32 AuctionsPerPage = 50;   // NUM_AUCTION_ITEMS_PER_PAGE
constexpr float RowH = 37.f;             // BrowseButton height

FLinearColor NormalFontColor()    { return WowColor::FromWow(1.f, .82f, 0.f); }
FLinearColor HighlightFontColor() { return FLinearColor(1.f, 1.f, 1.f); }
FLinearColor GrayFontColor()      { return WowColor::FromWow(.5f, .5f, .5f); }
FLinearColor RedFontColor()       { return WowColor::FromWow(1.f, .1f, .1f); }
FLinearColor GreenFontColor()     { return WowColor::FromWow(.1f, 1.f, .1f); }

FBox2f UV4(float U0, float U1, float V0, float V1) { return FBox2f(FVector2f(U0, V0), FVector2f(U1, V1)); }

uint32 ParseDigits(const FString& Text)
{
    uint64 Value = 0;
    for (TCHAR C : Text) if (FChar::IsDigit(C)) Value = FMath::Min<uint64>(Value * 10 + (C - TEXT('0')), 0xFFFFFFFFull);
    return static_cast<uint32>(Value);
}

uint32 MoneyFrom(const FString& Gold, const FString& Silver, const FString& Copper)
{
    const uint64 Value = uint64(ParseDigits(Gold)) * 10000 + uint64(FMath::Min(ParseDigits(Silver), 99u)) * 100 + FMath::Min(ParseDigits(Copper), 99u);
    return static_cast<uint32>(FMath::Min<uint64>(Value, 0xFFFFFFFFull));
}

void MoneyTo(uint32 Copper, FString& Gold, FString& Silver, FString& OutCopper)
{
    Gold = Copper >= 10000 ? FString::FromInt(Copper / 10000) : FString();
    Silver = Copper >= 100 ? FString::FromInt(Copper / 100 % 100) : FString();
    OutCopper = FString::FromInt(Copper % 100);
}

// GetAuctionItemClasses / GetAuctionItemSubClasses (3.3.5 order): item class and subclass ids.
struct FAuctionSub { const TCHAR* Name; uint32 Id; };
struct FAuctionClass { const TCHAR* Name; uint32 Id; TArray<FAuctionSub> Subs; };
const TArray<FAuctionClass>& Categories()
{
    static const TArray<FAuctionClass> List = {
        { TEXT("Weapon"), 2, { {TEXT("One-Handed Axes"),0},{TEXT("Two-Handed Axes"),1},{TEXT("Bows"),2},{TEXT("Guns"),3},
            {TEXT("One-Handed Maces"),4},{TEXT("Two-Handed Maces"),5},{TEXT("Polearms"),6},{TEXT("One-Handed Swords"),7},
            {TEXT("Two-Handed Swords"),8},{TEXT("Staves"),10},{TEXT("Fist Weapons"),13},{TEXT("Miscellaneous"),14},
            {TEXT("Daggers"),15},{TEXT("Thrown"),16},{TEXT("Crossbows"),18},{TEXT("Wands"),19},{TEXT("Fishing Poles"),20} } },
        { TEXT("Armor"), 4, { {TEXT("Miscellaneous"),0},{TEXT("Cloth"),1},{TEXT("Leather"),2},{TEXT("Mail"),3},{TEXT("Plate"),4},
            {TEXT("Shields"),6},{TEXT("Librams"),7},{TEXT("Idols"),8},{TEXT("Totems"),9},{TEXT("Sigils"),10} } },
        { TEXT("Container"), 1, { {TEXT("Bag"),0},{TEXT("Soul Bag"),1},{TEXT("Herb Bag"),2},{TEXT("Enchanting Bag"),3},
            {TEXT("Engineering Bag"),4},{TEXT("Gem Bag"),5},{TEXT("Mining Bag"),6},{TEXT("Leatherworking Bag"),7},{TEXT("Inscription Bag"),8} } },
        { TEXT("Consumable"), 0, { {TEXT("Food & Drink"),5},{TEXT("Potion"),1},{TEXT("Elixir"),2},{TEXT("Flask"),3},
            {TEXT("Bandage"),7},{TEXT("Item Enhancement"),6},{TEXT("Scroll"),4},{TEXT("Other"),8} } },
        { TEXT("Glyph"), 16, {} },
        { TEXT("Trade Goods"), 7, { {TEXT("Elemental"),10},{TEXT("Cloth"),5},{TEXT("Leather"),6},{TEXT("Metal & Stone"),7},
            {TEXT("Meat"),8},{TEXT("Herb"),9},{TEXT("Enchanting"),12},{TEXT("Jewelcrafting"),4},{TEXT("Parts"),1},
            {TEXT("Devices"),3},{TEXT("Explosives"),2},{TEXT("Materials"),13},{TEXT("Other"),11},
            {TEXT("Armor Enchantment"),14},{TEXT("Weapon Enchantment"),15} } },
        { TEXT("Projectile"), 6, { {TEXT("Arrow"),2},{TEXT("Bullet"),3} } },
        { TEXT("Quiver"), 11, { {TEXT("Quiver"),2},{TEXT("Ammo Pouch"),3} } },
        { TEXT("Recipe"), 9, { {TEXT("Book"),0},{TEXT("Leatherworking"),1},{TEXT("Tailoring"),2},{TEXT("Engineering"),3},
            {TEXT("Blacksmithing"),4},{TEXT("Cooking"),5},{TEXT("Alchemy"),6},{TEXT("First Aid"),7},{TEXT("Enchanting"),8},
            {TEXT("Fishing"),9},{TEXT("Jewelcrafting"),10} } },
        { TEXT("Gem"), 3, { {TEXT("Red"),0},{TEXT("Blue"),1},{TEXT("Yellow"),2},{TEXT("Purple"),3},{TEXT("Green"),4},
            {TEXT("Orange"),5},{TEXT("Meta"),6},{TEXT("Simple"),7},{TEXT("Prismatic"),8} } },
        { TEXT("Miscellaneous"), 15, { {TEXT("Junk"),0},{TEXT("Reagent"),1},{TEXT("Pet"),2},{TEXT("Holiday"),3},{TEXT("Other"),4},{TEXT("Mount"),5} } },
        { TEXT("Quest"), 12, {} },
    };
    return List;
}

const TCHAR* QualityName(int32 Quality)
{
    static const TCHAR* Names[] = { TEXT("Poor"), TEXT("Common"), TEXT("Uncommon"), TEXT("Rare"), TEXT("Epic"), TEXT("Legendary") };
    return Quality >= 0 && Quality < 6 ? Names[Quality] : TEXT("All");
}

FLinearColor QualityColorOf(int32 Quality)
{
    // ITEM_QUALITY_COLORS
    switch (Quality)
    {
    case 0: return WowColor::FromWow(.62f, .62f, .62f);
    case 2: return WowColor::FromWow(.12f, 1.f, 0.f);
    case 3: return WowColor::FromWow(0.f, .44f, .87f);
    case 4: return WowColor::FromWow(.64f, .21f, .93f);
    case 5: return WowColor::FromWow(1.f, .5f, 0.f);
    default: return FLinearColor::White;
    }
}
}

SWowAuctionHouse::~SWowAuctionHouse()
{
    if (ConnectionManager.IsValid())
        ConnectionManager->ContainerItemUseOverride = PreviousItemUseOverride;
}

void SWowAuctionHouse::Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager)
{
    ConnectionManager = InConnectionManager;
    UIManager = InArgs._UIManager;
    if (UIManager.IsValid() && UIManager->GetFrameManager() && UIManager->GetFrameManager()->GetUIScale() > 0.f)
        Scale = UIManager->GetFrameManager()->GetUIScale();

    if (ConnectionManager.IsValid())
    {
        // Right-clicking a bag item while the Auctions tab is shown puts it in the auction slot;
        // anything else goes on to the previous handler (the mailbox's Send Mail tab).
        PreviousItemUseOverride = ConnectionManager->ContainerItemUseOverride;
        TWeakPtr<SWowAuctionHouse> Weak = SharedThis(this);
        TFunction<bool(uint64)> Previous = PreviousItemUseOverride;
        ConnectionManager->ContainerItemUseOverride = [Weak, Previous](uint64 ItemGuid)
        {
            const TSharedPtr<SWowAuctionHouse> Self = Weak.Pin();
            if (Self.IsValid() && Self->bVisible && Self->Tab == ETab::Auctions) { Self->PutSellItem(ItemGuid); return true; }
            return Previous ? Previous(ItemGuid) : false;
        };
    }

    ChildSlot
    [
        SAssignNew(Root, SBox)
        .WidthOverride_Lambda([this]() { return FrameW * Scale; })
        .HeightOverride_Lambda([this]() { return 512.f * Scale; })
        [ SAssignNew(Layers, SOverlay) ]
    ];
    SetVisibility(EVisibility::Collapsed);
}

// ── Visibility ────────────────────────────────────────────────────────────

void SWowAuctionHouse::Open()
{
    const bool bWasVisible = bVisible;
    bVisible = true;
    if (!bWasVisible)
    {
        Tab = ETab::Browse;
        Page = 0;
        SelectedAuction = PendingConfirmAuction = 0;
        bPendingCancel = false;
        PlayUiSound(TEXT("AuctionWindowOpen"));
    }
    if (!bWasVisible) { ListScroll = FilterScroll = 0.f; }
    if (UWowConnectionManager* Connection = ConnectionManager.Get())
    {
        Connection->ListOwnerAuctions();
        Connection->ListBidderAuctions();
    }
    // Build first, then show: showing first drew the previous visit's content for a frame.
    if (!bWasVisible)
    {
        if (CurrentLayer.IsValid()) RetiredLayers.Add(CurrentLayer);
        CurrentLayer.Reset();
        DropRetiredLayers();
    }
    Rebuild();
    SetVisibility(EVisibility::SelfHitTestInvisible);
}

void SWowAuctionHouse::Close()
{
    if (!bVisible) return;
    bVisible = false;
    DropRetiredLayers();
    SelectedAuction = PendingConfirmAuction = 0;
    bPendingCancel = false;
    SellItemGuid = 0;
    SetVisibility(EVisibility::Collapsed);
    if (UWowConnectionManager* Connection = ConnectionManager.Get())
    {
        Connection->PacketHandler.AuctionOpen = false;
        Connection->PacketHandler.AuctionGuid = 0;
    }
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().CloseToolTip();
    PlayUiSound(TEXT("AuctionWindowClose"));
}

void SWowAuctionHouse::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    if (!bVisible) return;
    UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Connection || Connection->GetState() != EWowSessionState::WorldInGame) { Close(); return; }
    if (CurrentTime >= NextRangeCheck)
    {
        NextRangeCheck = CurrentTime + .5;
        // Walking away from the auctioneer closes the window, as in the client.
        auto& Handler = Connection->PacketHandler;
        const FWowEntity* Npc = Handler.EntityManager.Find(Handler.AuctionGuid);
        const FWowEntity* Me = Handler.EntityManager.GetLocalPlayer();
        if (!Handler.AuctionGuid || (Npc && Me && FVector::Dist(Npc->Movement.Position, Me->Movement.Position) > 10.f)) { Close(); return; }
    }
    // A finished sell / cancel / bid refreshes the Bids and Auctions lists.
    if (Connection->PacketHandler.AuctionResultCount != SeenResultCount)
    {
        SeenResultCount = Connection->PacketHandler.AuctionResultCount;
        Connection->ListOwnerAuctions();
        Connection->ListBidderAuctions();
        if (Connection->PacketHandler.LastAuctionError == 0)
        {
            if (Connection->PacketHandler.LastAuctionAction == 0) { SellItemGuid = 0; PlayUiSound(TEXT("LOOTWINDOWCOINSOUND")); }
            if (Connection->PacketHandler.LastAuctionAction == 2 && Tab == ETab::Browse) Search(Page);
        }
    }
    bool bDirty = false;
    if (UIManager.IsValid() && UIManager->GetFrameManager())
    {
        const float NewScale = UIManager->GetFrameManager()->GetUIScale();
        if (NewScale > 0.f && !FMath::IsNearlyEqual(NewScale, Scale)) { Scale = NewScale; bDirty = true; }
    }
    if (ComputeStateStamp() != LastStateStamp) bDataRebuildPending = true;
    // Not while typing in the name / level / money boxes: the rebuild would take the caret away.
    if (bDataRebuildPending && CurrentTime - LastRebuildTime >= .25 && !HasFocusedDescendants()) bDirty = true;
    if (bDirty) Rebuild();
    if (RetiredLayers.Num() && GFrameCounter >= RetireAfterFrame) DropRetiredLayers();
}

void SWowAuctionHouse::DropRetiredLayers()
{
    if (Layers.IsValid())
        for (const TSharedPtr<SWidget>& Old : RetiredLayers)
            if (Old.IsValid()) Layers->RemoveSlot(Old.ToSharedRef());
    RetiredLayers.Reset();
}

uint32 SWowAuctionHouse::ComputeStateStamp() const
{
    uint32 Stamp = GetPlayerMoney();
    if (UWowConnectionManager* Connection = ConnectionManager.Get())
    {
        auto& Handler = Connection->PacketHandler;
        Stamp = HashCombine(Stamp, Handler.ServiceRevision);
        Stamp = HashCombine(Stamp, uint32(Handler.ItemTemplates.Num()));
        Stamp = HashCombine(Stamp, uint32(Handler.PlayerNameCache.Num()));
        if (SellItemGuid)
            if (const FWowItemEntity* Item = Handler.EntityManager.FindItem(SellItemGuid))
                Stamp = HashCombine(Stamp, uint32(Item->GetStackCount()));
    }
    return Stamp;
}

// ── Data ──────────────────────────────────────────────────────────────────

const FWowAuctionOffer* SWowAuctionHouse::FindOffer(uint32 AuctionId) const
{
    const UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Connection || !AuctionId) return nullptr;
    const auto& H = Connection->PacketHandler;
    auto Match = [AuctionId](const FWowAuctionOffer& O) { return O.Id == AuctionId; };
    const TArray<FWowAuctionOffer>& List = Tab == ETab::Browse ? H.AuctionOffers : Tab == ETab::Bids ? H.AuctionBidOffers : H.AuctionOwnOffers;
    return List.FindByPredicate(Match);
}

uint32 SWowAuctionHouse::NextBid(const FWowAuctionOffer& Offer) const
{
    // The next bid is the current bid plus the minimum increment, or the starting price.
    const uint32 Next = Offer.Bid ? Offer.Bid + Offer.Increment : Offer.Minimum;
    return Offer.Buyout ? FMath::Min(Next, Offer.Buyout) : Next;
}

uint32 SWowAuctionHouse::GetPlayerMoney() const
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    const FWowPlayerEntity* Me = Connection ? Connection->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
    return Me ? Me->GetField(PlayerField::COINAGE) : 0;
}

uint32 SWowAuctionHouse::SellCount() const
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    const FWowItemEntity* Item = Connection && SellItemGuid ? Connection->PacketHandler.EntityManager.FindItem(SellItemGuid) : nullptr;
    return Item ? uint32(FMath::Max(1, Item->GetStackCount())) : 0;
}

uint32 SWowAuctionHouse::Deposit() const
{
    // CalculateAuctionDeposit: vendor sell price x count x 15 % (faction house) per 12 hours.
    UWowConnectionManager* Connection = ConnectionManager.Get();
    const FWowItemEntity* Item = Connection && SellItemGuid ? Connection->PacketHandler.EntityManager.FindItem(SellItemGuid) : nullptr;
    const FWowItemTemplateInfo* Info = Item ? FWowItemPresentation::Info(Connection, Item->Entry) : nullptr;
    if (!Info) return 0;
    const uint32 Hours = Duration == 2 ? 48 : Duration == 1 ? 24 : 12;
    return FMath::Max(1u, uint32(double(Info->SellPrice) * SellCount() * .15 * (Hours / 12)));
}

FString SWowAuctionHouse::SellerName(uint64 Guid) const
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Connection || !Guid) return FString();
    if (const FString* Name = Connection->PacketHandler.PlayerNameCache.Find(Guid)) return *Name;
    if (!RequestedNames.Contains(Guid)) { RequestedNames.Add(Guid); Connection->SendNameQuery(static_cast<int64>(Guid)); }
    return TEXT("...");
}

FString SWowAuctionHouse::TimeLeftText(uint32 Milliseconds, FLinearColor& OutColor) const
{
    // AUCTION_TIME_LEFT1-4: under 30 minutes, 30 minutes - 2 hours, 2 - 12 hours, over 12 hours.
    const uint32 Minutes = Milliseconds / 60000;
    if (Minutes < 30) { OutColor = RedFontColor(); return TEXT("Short"); }
    OutColor = GrayFontColor();
    if (Minutes < 120) return TEXT("Medium");
    if (Minutes < 720) return TEXT("Long");
    return TEXT("Very Long");
}

TArray<FWowAuctionOffer> SWowAuctionHouse::Sorted(const TArray<FWowAuctionOffer>& Offers) const
{
    TArray<FWowAuctionOffer> Result = Offers;
    if (SortKey == 0) return Result;
    UWowConnectionManager* Connection = ConnectionManager.Get();
    auto Key = [this, Connection](const FWowAuctionOffer& O) -> FString
    {
        const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(Connection, O.Item);
        switch (SortKey)
        {
        case 1: return FString::Printf(TEXT("%d%s"), 9 - int32(Info ? Info->Quality : 1), Info ? *Info->Name : TEXT(""));
        case 2: return FString::Printf(TEXT("%03u"), Info ? Info->RequiredLevel : 0u);
        case 3: return FString::Printf(TEXT("%010u"), O.Time);
        case 4: return SellerName(O.Owner);
        default: return FString::Printf(TEXT("%010u"), O.Bid ? O.Bid : O.Minimum);
        }
    };
    Result.StableSort([&Key, this](const FWowAuctionOffer& A, const FWowAuctionOffer& B)
    {
        const int32 Cmp = Key(A).Compare(Key(B), ESearchCase::IgnoreCase);
        return bSortReverse ? Cmp > 0 : Cmp < 0;
    });
    return Result;
}

// ── Actions ───────────────────────────────────────────────────────────────

void SWowAuctionHouse::PlayUiSound(const TCHAR* Sound) const
{
    UWorld* World = UIManager.IsValid() ? UIManager->GetWorld() : nullptr;
    if (AWowAudioManager* Audio = World ? Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(World, AWowAudioManager::StaticClass())) : nullptr)
        Audio->PlayUiSound(Sound);
}

void SWowAuctionHouse::SetTab(ETab NewTab)
{
    if (Tab == NewTab) return;
    Tab = NewTab;
    SelectedAuction = PendingConfirmAuction = 0;
    bPendingCancel = false;
    if (UWowConnectionManager* Connection = ConnectionManager.Get())
    {
        if (Tab == ETab::Bids) Connection->ListBidderAuctions();
        if (Tab == ETab::Auctions) Connection->ListOwnerAuctions();
    }
    PlayUiSound(TEXT("igCharacterInfoTab"));
    Rebuild();
}

void SWowAuctionHouse::Search(uint32 NewPage)
{
    ListBox.Reset(); ListScroll = 0.f; // a new result page starts at the top
    bRarityOpen = false;
    UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Connection) return;
    Page = NewPage;
    SelectedAuction = PendingConfirmAuction = 0;
    const TArray<FAuctionClass>& Cats = Categories();
    const uint32 ClassId = Cats.IsValidIndex(SelectedClass) ? Cats[SelectedClass].Id : 0xFFFFFFFFu;
    const uint32 SubId = Cats.IsValidIndex(SelectedClass) && Cats[SelectedClass].Subs.IsValidIndex(SelectedSubClass)
        ? Cats[SelectedClass].Subs[SelectedSubClass].Id : 0xFFFFFFFFu;
    Connection->SearchAuctionsFiltered(SearchName, Page * AuctionsPerPage,
        uint8(FMath::Min(ParseDigits(MinLevel), 255u)), uint8(FMath::Min(ParseDigits(MaxLevel), 255u)),
        0xFFFFFFFFu, ClassId, SubId, Quality < 0 ? 0xFFFFFFFFu : uint32(Quality), bUsableOnly);
    Rebuild();
}

void SWowAuctionHouse::SelectAuction(uint32 AuctionId)
{
    SelectedAuction = AuctionId;
    PendingConfirmAuction = 0;
    bPendingCancel = false;
    if (const FWowAuctionOffer* Offer = FindOffer(AuctionId))
        MoneyTo(NextBid(*Offer), BidGold, BidSilver, BidCopper);
    Rebuild();
}

void SWowAuctionHouse::PlaceBid(bool bBuyout)
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    const FWowAuctionOffer* Offer = FindOffer(SelectedAuction);
    if (!Connection || !Offer) return;
    const uint32 Price = bBuyout ? Offer->Buyout : MoneyFrom(BidGold, BidSilver, BidCopper);
    if (!Price) return;
    // BUYOUT_AUCTION / BID_AUCTION confirmation: the second click places it.
    if (PendingConfirmAuction != Offer->Id || bPendingConfirmBuyout != bBuyout)
    {
        PendingConfirmAuction = Offer->Id;
        bPendingConfirmBuyout = bBuyout;
        Rebuild();
        return;
    }
    Connection->BidAuction(Offer->Id, Price);
    PendingConfirmAuction = 0;
    Rebuild();
}

void SWowAuctionHouse::PutSellItem(uint64 ItemGuid)
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Connection) return;
    SellItemGuid = ItemGuid;
    // Start at the vendor price of the stack, like an empty starting price would be prompted for.
    if (const FWowItemEntity* Item = ItemGuid ? Connection->PacketHandler.EntityManager.FindItem(ItemGuid) : nullptr)
        if (const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(Connection, Item->Entry))
        {
            MoneyTo(FMath::Max(1u, Info->SellPrice * SellCount()), StartGold, StartSilver, StartCopper);
            BuyoutGold.Reset(); BuyoutSilver.Reset(); BuyoutCopper.Reset();
        }
    PlayUiSound(TEXT("igAbilityIconDrop"));
    Rebuild();
}

void SWowAuctionHouse::CreateAuction()
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    if (!Connection || !SellItemGuid) return;
    const uint32 Start = MoneyFrom(StartGold, StartSilver, StartCopper);
    const uint32 Buyout = MoneyFrom(BuyoutGold, BuyoutSilver, BuyoutCopper);
    const uint32 Minutes = Duration == 2 ? 2880 : Duration == 1 ? 1440 : 720;
    if (Connection->SellAuctionItem(SellItemGuid, Start, Buyout, Minutes))
        Connection->PacketHandler.ServiceMessage = TEXT("Creating auction...");
    Rebuild();
}

void SWowAuctionHouse::CancelSelectedAuction()
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    const FWowAuctionOffer* Offer = FindOffer(SelectedAuction);
    if (!Connection || !Offer) return;
    if (!bPendingCancel) { bPendingCancel = true; Rebuild(); return; }
    Connection->CancelAuction(Offer->Id);
    bPendingCancel = false;
    SelectedAuction = 0;
    Rebuild();
}

// ── Widget helpers ────────────────────────────────────────────────────────

FSlateFontInfo SWowAuctionHouse::Fnt(float WowPixels) const
{
    return FWowWindowStyle::TitleFont(FMath::Max(6, FMath::RoundToInt(WowPixels * Scale * .75f)));
}

float SWowAuctionHouse::TextWidth(const FString& Text, const FSlateFontInfo& Font) const
{
    if (!FSlateApplication::IsInitialized()) return Text.Len() * Font.Size * .6f;
    return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font).X;
}

void SWowAuctionHouse::Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const
{
    Canvas.AddSlot().Position(FVector2D(X, Y) * Scale).Size(FVector2D(W, H) * Scale)[Widget];
}

const FSlateBrush* SWowAuctionHouse::LoadBrush(const FString& TexturePath, FBox2f UV) const
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

const FButtonStyle* SWowAuctionHouse::TextureButtonStyle(const FString& Up, const FString& Down, const FString& Disabled, FBox2f UV) const
{
    const FString Key = Up + TEXT("|") + Down + TEXT("|") + Disabled + FString::Printf(TEXT("|%.3f%.3f%.3f%.3f"), UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y);
    if (const TSharedPtr<FButtonStyle>* Found = ButtonStyles.Find(Key)) return Found->Get();
    const FSlateBrush* Normal = LoadBrush(Up, UV);
    if (!Normal) return nullptr;
    const FSlateBrush* Pressed = Down.IsEmpty() ? nullptr : LoadBrush(Down, UV);
    const FSlateBrush* Off = Disabled.IsEmpty() ? nullptr : LoadBrush(Disabled, UV);
    TSharedPtr<FButtonStyle> Style = MakeShared<FButtonStyle>(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"));
    Style->SetNormal(*Normal);
    FSlateBrush Hovered = *Normal;
    Hovered.TintColor = WowColor::FromWow(1.2f, 1.2f, 1.2f);
    Style->SetHovered(Hovered);
    Style->SetPressed(Pressed ? *Pressed : *Normal);
    Style->SetDisabled(Off ? *Off : *Normal);
    Style->SetNormalPadding(FMargin(0));
    Style->SetPressedPadding(FMargin(0));
    ButtonStyles.Add(Key, Style);
    return Style.Get();
}

TSharedRef<SWidget> SWowAuctionHouse::Img(const FString& Path, FBox2f UV, FLinearColor Tint) const
{
    return SNew(SImage).Visibility(EVisibility::HitTestInvisible).ColorAndOpacity(Tint)
        .Image_Lambda([this, Path, UV]() { return LoadBrush(Path, UV); });
}

TSharedRef<SWidget> SWowAuctionHouse::Label(const FText& Text, float WowPixels, FLinearColor Color, ETextJustify::Type Justify) const
{
    return SNew(STextBlock).Text(Text).Font(Fnt(WowPixels)).ColorAndOpacity(Color).Justification(Justify)
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis).Visibility(EVisibility::HitTestInvisible)
        .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black);
}

TSharedRef<SWidget> SWowAuctionHouse::PanelButton(const FText& Text, float W, float H, FOnClicked OnClicked, TAttribute<bool> Enabled) const
{
    return SNew(SBox).WidthOverride(W * Scale).HeightOverride(H * Scale)
        [
            SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center)
            .ContentPadding(FMargin(2 * Scale, 0)).IsEnabled(Enabled).OnClicked(OnClicked)
            [ SNew(STextBlock).Text(Text).Font(Fnt(12)).OverflowPolicy(ETextOverflowPolicy::Clip).RenderTransform(WowCapCenterTransform(Fnt(12)))
              .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
        ];
}

TSharedRef<SWidget> SWowAuctionHouse::CloseButton(FOnClicked OnClicked) const
{
    const FButtonStyle* Style = TextureButtonStyle(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"),
        TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Down"));
    if (!Style) Style = &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder");
    return SNew(SButton).ButtonStyle(Style).ContentPadding(0).ClickMethod(EButtonClickMethod::MouseDown).OnClicked(OnClicked);
}

TSharedRef<SWidget> SWowAuctionHouse::InputBorder() const
{
    return SNew(SHorizontalBox).Visibility(EVisibility::HitTestInvisible)
        + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(8 * Scale)
            [ Img(TEXT("Interface\\Common\\Common-Input-Border"), UV4(0, .0625f, 0, .625f)) ] ]
        + SHorizontalBox::Slot().FillWidth(1)[ Img(TEXT("Interface\\Common\\Common-Input-Border"), UV4(.0625f, .9375f, 0, .625f)) ]
        + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(8 * Scale)
            [ Img(TEXT("Interface\\Common\\Common-Input-Border"), UV4(.9375f, 1, 0, .625f)) ] ];
}

TSharedRef<SWidget> SWowAuctionHouse::TextField(FString& Value, int32 MaxLetters, float WowPixels, bool bNumeric)
{
    FString* Target = &Value;
    return SNew(SOverlay)
        + SOverlay::Slot()[ InputBorder() ]
        + SOverlay::Slot().VAlign(VAlign_Center).Padding(FMargin(6 * Scale, 0, 4 * Scale, 0))
        [
            SNew(SEditableText)
            .Text_Lambda([Target]() { return FText::FromString(*Target); })
            .Font(Fnt(WowPixels))
            .ColorAndOpacity(HighlightFontColor())
            .SelectAllTextWhenFocused(bNumeric)
            .ClearKeyboardFocusOnCommit(true)
            .OnTextChanged_Lambda([Target, MaxLetters, bNumeric](const FText& NewText)
            {
                FString Text = NewText.ToString();
                if (bNumeric) { FString Digits; for (TCHAR C : Text) if (FChar::IsDigit(C)) Digits.AppendChar(C); Text = Digits; }
                *Target = Text.Left(MaxLetters);
            })
            .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit)
            {
                if (Commit == ETextCommit::OnEnter && Tab == ETab::Browse) Search(0);
            })
        ];
}

TSharedRef<SWidget> SWowAuctionHouse::MoneyInput(FString& Gold, FString& Silver, FString& Copper)
{
    // MoneyInputFrameTemplate: gold (5 digits), silver and copper (2 digits) with coin icons.
    auto Coin = [this](const TCHAR* Icon)
    {
        return SNew(SBox).WidthOverride(13 * Scale).HeightOverride(13 * Scale)[ Img(Icon) ];
    };
    // r268: MoneyInputFrame.xml sizes - gold 58 wide, silver and copper 30, each coin 2 right of its
    // box and the next box 26 right of the previous one.
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(58 * Scale).HeightOverride(20 * Scale)[ TextField(Gold, 6, 12, true) ] ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2 * Scale, 0, 11 * Scale, 0))[ Coin(TEXT("Interface\\MoneyFrame\\UI-GoldIcon")) ]
        + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(30 * Scale).HeightOverride(20 * Scale)[ TextField(Silver, 2, 12, true) ] ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2 * Scale, 0, 11 * Scale, 0))[ Coin(TEXT("Interface\\MoneyFrame\\UI-SilverIcon")) ]
        + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(30 * Scale).HeightOverride(20 * Scale)[ TextField(Copper, 2, 12, true) ] ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2 * Scale, 0, 0, 0))[ Coin(TEXT("Interface\\MoneyFrame\\UI-CopperIcon")) ];
}

TSharedRef<SWidget> SWowAuctionHouse::MoneyText(uint32 Copper, float WowPixels, FLinearColor Color) const
{
    TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox).Visibility(EVisibility::HitTestInvisible);
    auto Add = [this, &Row, WowPixels, Color](uint32 Value, const TCHAR* Icon)
    {
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(4 * Scale, 0, 1 * Scale, 0))
            [ SNew(STextBlock).Text(FText::AsNumber(Value)).Font(Fnt(WowPixels)).ColorAndOpacity(Color)
              .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ];
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(13 * Scale).HeightOverride(13 * Scale)[ Img(Icon) ] ];
    };
    if (Copper >= 10000) Add(Copper / 10000, TEXT("Interface\\MoneyFrame\\UI-GoldIcon"));
    if (Copper >= 100) Add(Copper / 100 % 100, TEXT("Interface\\MoneyFrame\\UI-SilverIcon"));
    if (Copper % 100 || Copper < 100) Add(Copper % 100, TEXT("Interface\\MoneyFrame\\UI-CopperIcon"));
    return Row;
}

TSharedRef<SWidget> SWowAuctionHouse::CheckBox(bool& bValue, const FText& Text)
{
    // OptionsCheckButtonTemplate (UI-CheckBox-Up / -Check), 24x24, label to the right.
    bool* Target = &bValue;
    return SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(0)
        .OnClicked_Lambda([this, Target]() { *Target = !*Target; PlayUiSound(TEXT("igMainMenuOptionCheckBoxOn")); Rebuild(); return FReply::Handled(); })
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride(24 * Scale).HeightOverride(24 * Scale)
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()[ Img(TEXT("Interface\\Buttons\\UI-CheckBox-Up")) ]
                    + SOverlay::Slot()[ SNew(SBox).Visibility(*Target ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
                        [ Img(TEXT("Interface\\Buttons\\UI-CheckBox-Check")) ] ]
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2 * Scale, 0, 0, 0))
            [ Label(Text, 10, HighlightFontColor()) ]
        ];
}

// ── Layout ────────────────────────────────────────────────────────────────

void SWowAuctionHouse::BuildFrameArt(SCanvas& Canvas) const
{
    // AuctionFrame*: six pieces per tab (UI-AuctionFrame-Browse/-Bid/-Auction-TopLeft ... -BotRight).
    const TCHAR* Set = Tab == ETab::Browse ? TEXT("Browse") : Tab == ETab::Bids ? TEXT("Bid") : TEXT("Auction");
    auto Piece = [&](const TCHAR* Part, float X, float Y, float W)
    {
        Place(Canvas, X, Y, W, 256, Img(FString::Printf(TEXT("Interface\\AuctionFrame\\UI-AuctionFrame-%s-%s"), Set, Part)));
    };
    Piece(TEXT("TopLeft"), 0, 0, 256);
    Piece(TEXT("Top"), 256, 0, 320);
    Piece(TEXT("TopRight"), 576, 0, 256);
    Piece(TEXT("BotLeft"), 0, 256, 256);
    Piece(TEXT("Bot"), 256, 256, 320);
    Piece(TEXT("BotRight"), 576, 256, 256);
}

void SWowAuctionHouse::Rebuild()
{
    if (!Root.IsValid() || !Layers.IsValid()) return;
    LastStateStamp = ComputeStateStamp();
    LastRebuildTime = FPlatformTime::Seconds();
    bDataRebuildPending = false;
    if (ListBox.IsValid()) ListScroll = ListBox->GetScrollOffset();
    if (FilterBox.IsValid()) FilterScroll = FilterBox->GetScrollOffset();
    ListBox.Reset(); FilterBox.Reset();
    TSharedRef<SCanvas> Canvas = SNew(SCanvas);
    // Auctioneer portrait (BACKGROUND, under the frame art's ring), like SetPortraitTexture(AuctionPortraitTexture, "npc").
    if (ConnectionManager.IsValid() && UIManager.IsValid() && UIManager->GetFrameManager() && UIManager->GetFrameManager()->UnitPortraitProvider)
        if (UMaterialInterface* Portrait = UIManager->GetFrameManager()->UnitPortraitProvider(ConnectionManager->PacketHandler.AuctionGuid))
        {
            if (!PortraitBrush.IsValid()) PortraitBrush = MakeShared<FWowRetainedSlateBrush>();
            PortraitBrush->SetResourceObject(Portrait);
            PortraitBrush->DrawAs = ESlateBrushDrawType::Image;
            PortraitBrush->ImageSize = FVector2D(64, 64);
            Place(*Canvas, 7, 6, 60, 60, SNew(SImage).Image(PortraitBrush.Get()).Visibility(EVisibility::HitTestInvisible));
        }
    BuildFrameArt(*Canvas);
    // AuctionFrame title: "Auction House" centred on the top bar.
    Place(*Canvas, 75, 18, 608, 14, WowTitleBox(Label(LOCTEXT("Title", "Auction House"), 12, NormalFontColor(), ETextJustify::Center), Fnt(12)));
    if (Tab == ETab::Browse) BuildBrowse(*Canvas);
    else if (Tab == ETab::Bids) BuildBids(*Canvas);
    else BuildAuctions(*Canvas);
    BuildTabs(*Canvas);
    if (Tab == ETab::Browse && bRarityOpen) BuildRarityList(*Canvas); // on top of everything
    Place(*Canvas, 803, 8, 32, 32, CloseButton(FOnClicked::CreateLambda([this]() { Close(); return FReply::Handled(); })));

    // Money in the bottom left (AuctionFrameMoneyFrame).
    // Left-aligned, vertically centred in its black box (Stefan 10.10).
    // Stefan 11.10: a few pixels higher (it sat on the box's lower edge).
    // r261 (Stefan 12.10, measured on his screenshot): the digits sat 2 units above the middle of the
    // gold-edged box (box interior 16-35 px, digits 17-28 px at 1.39 px per unit): 2 units lower.
    // r268 (Stefan 13.10, screenshot of the original): right-aligned like AuctionFrameMoneyFrame
    // (SmallMoneyFrame, BOTTOMRIGHT at 181 from the frame's left edge).
    Place(*Canvas, 22, 410, 160, 22, SNew(SBox).HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(FMargin(0, 0, 3 * Scale, 0))[ MoneyText(GetPlayerMoney(), 12) ]);

    // Server feedback (ERR_AUCTION_* / "Auction created.") above the bottom bar.
    const FString Message = ConnectionManager.IsValid() ? ConnectionManager->PacketHandler.ServiceMessage : FString();
    Place(*Canvas, 190, 392, 380, 16, Label(FText::FromString(Message), 10, NormalFontColor()));

    TSharedRef<SWidget> NewLayer =
        SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SBox).Padding(FMargin(0, 0, 0, (512.f - FrameH) * Scale))
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
                .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
            ]
        ]
        + SOverlay::Slot()[ Canvas ];
    // The old canvas stays drawn (but not clickable) under the new one for one frame.
    if (CurrentLayer.IsValid())
    {
        CurrentLayer->SetVisibility(EVisibility::HitTestInvisible);
        RetiredLayers.Add(CurrentLayer);
    }
    if (Layers.IsValid()) Layers->AddSlot()[ NewLayer ];
    CurrentLayer = NewLayer;
    RetireAfterFrame = GFrameCounter + 1;
}

void SWowAuctionHouse::BuildRarityList(SCanvas& Canvas)
{
    // DropDownList1: dark dialog background with the tooltip border, one 16-unit line per choice,
    // the current one checked, coloured like the item quality.
    const int32 Count = 7; // All, Poor .. Legendary
    const float X = 346.f, Y = 72.f, W = 130.f, H = Count * 16.f + 22.f;
    Place(Canvas, X, Y, W, H, SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(WowColor::FromWow(.55f, .55f, .55f, 1.f)).Padding(FMargin(1.5f * Scale))
        [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0, 0, 0, .92f)) ]);
    for (int32 i = 0; i < Count; ++i)
    {
        const int32 Value = i - 1;
        const float RowY = Y + 11.f + i * 16.f;
        if (Value == Quality)
            Place(Canvas, X + 8, RowY, 16, 16, Img(TEXT("Interface\\Buttons\\UI-CheckBox-Check")));
        TSharedRef<SBorder> Row = SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0);
        TWeakPtr<SBorder> WeakRow = Row;
        Place(Canvas, X + 6, RowY, W - 12, 16, SNew(SOverlay)
            + SOverlay::Slot()[ SNew(SImage).Visibility(EVisibility::HitTestInvisible)
                .Image_Lambda([this]() { return LoadBrush(TEXT("Interface\\QuestFrame\\UI-QuestTitleHighlight")); })
                .ColorAndOpacity_Lambda([WeakRow]() { const TSharedPtr<SBorder> R = WeakRow.Pin();
                    return FSlateColor(FLinearColor(1, 1, 1, R.IsValid() && R->IsHovered() ? .6f : 0.f)); }) ]
            + SOverlay::Slot()[ Row ]);
        Place(Canvas, X + 26, RowY, W - 32, 16, SNew(SBox).VAlign(VAlign_Center)
            [ SNew(STextBlock).Visibility(EVisibility::HitTestInvisible).Text(FText::FromString(QualityName(Value))).Font(Fnt(10))
              .ColorAndOpacity(Value < 0 ? HighlightFontColor() : QualityColorOf(Value)) ]);
        Row->SetOnMouseButtonDown(FPointerEventHandler::CreateLambda([this, Value](const FGeometry&, const FPointerEvent&)
        {
            Quality = Value; bRarityOpen = false; PlayUiSound(TEXT("UChatScrollButton")); Rebuild();
            return FReply::Handled();
        }));
    }
}

void SWowAuctionHouse::BuildTabs(SCanvas& Canvas)
{
    // AuctionFrameTab1-3 (CharacterFrameTabButtonTemplate) under the bottom edge, 15 units in.
    const FSlateFontInfo TabFont = Fnt(10);
    const FText Labels[3] = { LOCTEXT("BrowseTab", "Browse"), LOCTEXT("BidsTab", "Bids"), LOCTEXT("AuctionsTab", "Auctions") };
    float X = 15.f;
    const float Y = FrameH - 10.f;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const ETab Target = static_cast<ETab>(Index);
        const bool bSelected = Tab == Target;
        const float W = TextWidth(Labels[Index].ToString(), TabFont) / Scale + 40.f;
        const FString Art = bSelected ? TEXT("Interface\\PaperDollInfoFrame\\UI-Character-ActiveTab")
                                      : TEXT("Interface\\PaperDollInfoFrame\\UI-Character-InActiveTab");
        const float ArtY = bSelected ? Y - 5.f : Y;
        Place(Canvas, X, ArtY, 20, 32, Img(Art, UV4(0, .15625f, 0, 1)));
        Place(Canvas, X + 20, ArtY, W - 40, 32, Img(Art, UV4(.15625f, .84375f, 0, 1)));
        Place(Canvas, X + W - 20, ArtY, 20, 32, Img(Art, UV4(.84375f, 1, 0, 1)));
        Place(Canvas, X, Y + 6.f, W, 16, Label(Labels[Index], 10, bSelected ? HighlightFontColor() : NormalFontColor(), ETextJustify::Center));
        Place(Canvas, X, Y, W, 32, SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
            .OnMouseButtonDown_Lambda([this, Target](const FGeometry&, const FPointerEvent& Event)
            {
                if (Event.GetEffectingButton() == EKeys::LeftMouseButton) SetTab(Target);
                return FReply::Handled();
            }));
        X += W - 15.f;
    }
}

void SWowAuctionHouse::BuildColumnHeader(SCanvas& Canvas, float X, float Y, float W, const FText& Text, int32 InSortKey)
{
    // AuctionSortButtonTemplate: WhoFrame-ColumnTabs left / middle / right, 19 high.
    const FString Art = TEXT("Interface\\FriendsFrame\\WhoFrame-ColumnTabs");
    Place(Canvas, X, Y, 5, 19, Img(Art, UV4(0, .078125f, 0, .59375f)));
    Place(Canvas, X + 5, Y, W - 9, 19, Img(Art, UV4(.078125f, .90625f, 0, .59375f)));
    Place(Canvas, X + W - 4, Y, 4, 19, Img(Art, UV4(.90625f, .96875f, 0, .59375f)));
    Place(Canvas, X + 8, Y + 3, W - 12, 14, Label(Text, 10, HighlightFontColor()));
    if (InSortKey <= 0) return;
    Place(Canvas, X, Y, W, 19, SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
        .OnMouseButtonDown_Lambda([this, InSortKey](const FGeometry&, const FPointerEvent&)
        {
            bSortReverse = SortKey == InSortKey ? !bSortReverse : false;
            SortKey = InSortKey;
            Rebuild();
            return FReply::Handled();
        }));
}

TSharedRef<SWidget> SWowAuctionHouse::BuildResultRow(const FWowAuctionOffer& Offer, ETab ForTab)
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(Connection, Offer.Item);
    const FString Name = FWowItemPresentation::Name(Connection, Offer.Item);
    const FString Icon = FWowItemPresentation::IconPath(Offer.Item, Info ? Info->DisplayId : 0);
    const bool bSelected = Offer.Id == SelectedAuction;
    const uint64 Me = Connection ? Connection->PacketHandler.EntityManager.LocalPlayerGuid : 0;
    FLinearColor TimeColor;
    const FString TimeLeft = TimeLeftText(Offer.Time, TimeColor);
    const uint32 Level = Info ? Info->RequiredLevel : 0;
    const uint32 Shown = Offer.Bid ? Offer.Bid : Offer.Minimum;

    // Columns (BrowseButton): icon 32, name 190, level 40, time left 90, seller 110, bid 140.
    TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2 * Scale, 0, 4 * Scale, 0))
        [
            SNew(SBox).WidthOverride(32 * Scale).HeightOverride(32 * Scale)
            [
                SNew(SOverlay)
                + SOverlay::Slot()[ Img(Icon) ]
                + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(FMargin(0, 0, 2 * Scale, 0))
                [ SNew(STextBlock).Text(Offer.Count > 1 ? FText::AsNumber(Offer.Count) : FText::GetEmpty()).Font(Fnt(12))
                  .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
            ]
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(SBox).WidthOverride(190 * Scale)[ Label(FText::FromString(Name), 12, QualityColorOf(Info ? int32(Info->Quality) : 1)) ] ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(SBox).WidthOverride(40 * Scale)[ Label(Level ? FText::AsNumber(Level) : FText::GetEmpty(), 12, HighlightFontColor(), ETextJustify::Center) ] ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(SBox).WidthOverride(90 * Scale)[ Label(FText::FromString(TimeLeft), 12, TimeColor, ETextJustify::Center) ] ];

    // Fourth column: seller (Browse), bid status (Bids), high bidder (Auctions).
    FString Fourth;
    FLinearColor FourthColor = HighlightFontColor();
    if (ForTab == ETab::Browse) Fourth = SellerName(Offer.Owner);
    else if (ForTab == ETab::Bids)
    {
        const bool bHigh = Offer.Bidder == Me;
        Fourth = bHigh ? TEXT("High Bidder") : TEXT("Outbid");
        FourthColor = bHigh ? GreenFontColor() : RedFontColor();
    }
    else Fourth = Offer.Bidder ? SellerName(Offer.Bidder) : TEXT("No Bids");
    Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(SBox).WidthOverride(110 * Scale)[ Label(FText::FromString(Fourth), 12, FourthColor, ETextJustify::Center) ] ];

    // Current bid (red when you cannot afford the next bid) with the buyout beneath it.
    const bool bCanAfford = GetPlayerMoney() >= NextBid(Offer);
    TSharedRef<SVerticalBox> Prices = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[ MoneyText(Shown, 12, ForTab == ETab::Browse && !bCanAfford ? RedFontColor() : HighlightFontColor()) ];
    if (Offer.Buyout)
        Prices->AddSlot().AutoHeight().HAlign(HAlign_Right)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Label(LOCTEXT("BuyoutShort", "Buyout"), 9, GrayFontColor()) ]
            + SHorizontalBox::Slot().AutoWidth()[ MoneyText(Offer.Buyout, 9, GrayFontColor()) ]
        ];
    Row->AddSlot().FillWidth(1).VAlign(VAlign_Center).Padding(FMargin(0, 0, 6 * Scale, 0))[ Prices ];

    const uint32 Id = Offer.Id;
    const uint32 OfferItemEntry = Offer.Item;
    return SNew(SBox).HeightOverride(RowH * Scale)
        [
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
            // The item tooltip over the whole row (Stefan 10.10: none showed; it sat on the 32 px
            // icon box only, which is not hit-testable).
            .ToolTip(TSharedPtr<SToolTip>(FWowItemPresentation::Tooltip(Connection, Offer.Item)))
            .OnMouseButtonDown_Lambda([this, Id, OfferItemEntry](const FGeometry&, const FPointerEvent& Event)
            {
                // Ctrl-click: the dressing room (DressUpItemLink; mounts with the Experimental mount preview).
                if (Event.GetEffectingButton() == EKeys::LeftMouseButton && Event.IsControlDown() && OfferItemEntry
                    && UIManager.IsValid() && UIManager->GetEventSystem())
                {
                    UIManager->GetEventSystem()->ExecuteLuaString(FString::Printf(
                        TEXT("if DressUpItemLink then DressUpItemLink(\"item:%u\") end"), OfferItemEntry), TEXT("=AuctionDressUp"));
                    return FReply::Handled();
                }
                if (Event.GetEffectingButton() == EKeys::LeftMouseButton) { SelectAuction(Id); PlayUiSound(TEXT("igMainMenuOptionCheckBoxOn")); }
                return FReply::Handled();
            })
            [
                SNew(SOverlay)
                // HelpFrameButton-Highlight: gold bar under the selected row.
                + SOverlay::Slot()[ SNew(SBox).Visibility(bSelected ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
                    [ Img(TEXT("Interface\\HelpFrame\\HelpFrameButton-Highlight"), UV4(0, 1, 0, .578125f)) ] ]
                + SOverlay::Slot()[ Row ]
            ]
        ];
}

const FScrollBarStyle* SWowAuctionHouse::GetScrollStyle()
{
    // Stefan 10.10 ("Feil scrollbar"): the grey Slate bar; WoW lists use the gold knob of UIPanelScrollBar.
    if (!ScrollStyle.IsValid())
    {
        ScrollStyle = MakeShared<FScrollBarStyle>(FCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>("ScrollBar"));
        static const FSlateNoResource NoTrack;
        ScrollStyle->SetVerticalBackgroundImage(NoTrack).SetVerticalTopSlotImage(NoTrack).SetVerticalBottomSlotImage(NoTrack)
            .SetHorizontalBackgroundImage(NoTrack).SetHorizontalTopSlotImage(NoTrack).SetHorizontalBottomSlotImage(NoTrack);
        if (const FSlateBrush* Knob = LoadBrush(TEXT("Interface\\Buttons\\UI-ScrollBar-Knob"),
            FBox2f(FVector2f(.20f, .125f), FVector2f(.80f, .875f))))
            ScrollStyle->SetNormalThumbImage(*Knob).SetHoveredThumbImage(*Knob).SetDraggedThumbImage(*Knob);
    }
    return ScrollStyle.Get();
}

void SWowAuctionHouse::PlaceList(SCanvas& Canvas, float X, float Y, float W, float H, const TArray<FWowAuctionOffer>& Offers, ETab ForTab)
{
    TSharedRef<SScrollBox> Box = SNew(SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always) /* at the end of the list the wheel zoomed the camera */
        .ScrollBarStyle(GetScrollStyle()).ScrollBarThickness(FVector2D(16 * Scale, 16 * Scale)).ScrollBarPadding(FMargin(0, 16 * Scale));
    for (const FWowAuctionOffer& Offer : Sorted(Offers))
        Box->AddSlot()[ BuildResultRow(Offer, ForTab) ];
    ListBox = Box;
    Box->SetScrollOffset(ListScroll);
    if (Offers.Num() == 0)
        Box->AddSlot().Padding(FMargin(10 * Scale, 20 * Scale))
        [ Label(ForTab == ETab::Browse ? LOCTEXT("NoResults", "No items found") : ForTab == ETab::Bids
            ? LOCTEXT("NoBids", "You have no active bids") : LOCTEXT("NoAuctions", "You have no active auctions"), 12, NormalFontColor(), ETextJustify::Center) ];
    // The list reaches the scroll bar column of BrowseScrollFrame (x 803-819) with its arrows.
    Place(Canvas, X, Y, W, H, Box);
    PlaceScrollArrows(Canvas, X + W - 16, Y, H, Box, 37);
}

void SWowAuctionHouse::PlaceScrollArrows(SCanvas& Canvas, float X, float Y, float H, TSharedRef<SScrollBox> Box, float Step)
{
    // UIPanelScrollUpButtonTemplate / DownButton (TexCoords .20-.80, .25-.75) at the ends of the bar.
    TWeakPtr<SScrollBox> WeakBox = Box;
    auto Arrow = [this, WeakBox, Step](const TCHAR* Dir, float Sign) -> TSharedRef<SWidget>
    {
        const FString Base = FString::Printf(TEXT("Interface\\Buttons\\UI-ScrollBar-Scroll%sButton-"), Dir);
        const FBox2f UV = UV4(.20f, .80f, .25f, .75f);
        // FauxScrollFrame hides its bar when everything fits (Stefan 11.10: lone gold arrows on
        // the category column looked like stray textures).
        return SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(0)
            .ClickMethod(EButtonClickMethod::MouseDown)
            .Visibility_Lambda([WeakBox]()
            {
                const TSharedPtr<SScrollBox> List = WeakBox.Pin();
                return List.IsValid() && List->GetScrollOffsetOfEnd() > .5f ? EVisibility::Visible : EVisibility::Collapsed;
            })
            .OnClicked_Lambda([this, WeakBox, Step, Sign]()
            {
                if (const TSharedPtr<SScrollBox> List = WeakBox.Pin())
                    List->SetScrollOffset(FMath::Max(0.f, List->GetScrollOffset() + Sign * Step * Scale));
                PlayUiSound(TEXT("UChatScrollButton"));
                return FReply::Handled();
            })
            [ Img(Base + TEXT("Up"), UV) ];
    };
    // UIPanelScrollUpButtonTemplate is 18 x 16, centred on the 16-unit bar.
    Place(Canvas, X - 1, Y, 18, 16, Arrow(TEXT("Up"), -1.f));
    Place(Canvas, X - 1, Y + H - 16, 18, 16, Arrow(TEXT("Down"), 1.f));
}

void SWowAuctionHouse::BuildFilters(SCanvas& Canvas)
{
    // AuctionFilterButton1-15: classes, the selected class unfolds its subclasses (indented).
    TSharedRef<SScrollBox> Box = SNew(SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always) /* at the end of the list the wheel zoomed the camera */
        .ScrollBarStyle(GetScrollStyle()).ScrollBarThickness(FVector2D(16 * Scale, 16 * Scale)).ScrollBarPadding(FMargin(0, 16 * Scale));
    const TArray<FAuctionClass>& Cats = Categories();
    auto AddButton = [this, &Box](const FString& Text, bool bSelected, bool bSub, TFunction<void()> OnClick)
    {
        Box->AddSlot()
        [
            SNew(SBox).HeightOverride(20 * Scale)
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
                .OnMouseButtonDown_Lambda([OnClick](const FGeometry&, const FPointerEvent& Event)
                {
                    if (Event.GetEffectingButton() == EKeys::LeftMouseButton) OnClick();
                    return FReply::Handled();
                })
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()[ SNew(SBox).Visibility(bSelected ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
                        [ Img(TEXT("Interface\\HelpFrame\\HelpFrameButton-Highlight"), UV4(0, 1, 0, .578125f)) ] ]
                    + SOverlay::Slot().VAlign(VAlign_Center).Padding(FMargin((bSub ? 16 : 6) * Scale, 0, 2 * Scale, 0))
                    [ Label(FText::FromString(Text), bSub ? 10 : 12, bSub ? HighlightFontColor() : NormalFontColor()) ]
                ]
            ]
        ];
    };
    for (int32 ClassIndex = 0; ClassIndex < Cats.Num(); ++ClassIndex)
    {
        AddButton(Cats[ClassIndex].Name, SelectedClass == ClassIndex && SelectedSubClass < 0, false, [this, ClassIndex]()
        {
            SelectedClass = SelectedClass == ClassIndex && SelectedSubClass < 0 ? -1 : ClassIndex;
            SelectedSubClass = -1;
            Rebuild();
        });
        if (SelectedClass != ClassIndex) continue;
        for (int32 SubIndex = 0; SubIndex < Cats[ClassIndex].Subs.Num(); ++SubIndex)
            AddButton(Cats[ClassIndex].Subs[SubIndex].Name, SelectedSubClass == SubIndex, true, [this, SubIndex]()
            {
                SelectedSubClass = SelectedSubClass == SubIndex ? -1 : SubIndex;
                Rebuild();
            });
    }
    FilterBox = Box;
    Box->SetScrollOffset(FilterScroll);
    // BrowseFilterScrollFrame: buttons from 23,-105; its scroll bar at x 164-180 (Stefan 10.10: the
    // bar sat too far left and had no arrows).
    Place(Canvas, 23, 105, 157, 305, Box);
    PlaceScrollArrows(Canvas, 164, 105, 305, Box, 20);
}

void SWowAuctionHouse::BuildBrowse(SCanvas& Canvas)
{
    // Search row: Name, Level Range, Rarity, Usable Items, Search.
    // Stefan 11.10: the name box ran into the level boxes. AuctionFrame.xml: BrowseName 140 wide
    // under "Name"; BrowseMinLevel / BrowseMaxLevel 25 wide 12 units right of it, the hyphen between;
    // every box the same height on one line (InputBoxTemplate's border reaches 5 units left).
    Place(Canvas, 80, 34, 140, 14, Label(LOCTEXT("Name", "Name"), 10, NormalFontColor()));
    Place(Canvas, 75, 49, 150, 20, TextField(SearchName, 63, 12));
    Place(Canvas, 231, 34, 90, 14, Label(LOCTEXT("LevelRange", "Level Range"), 10, NormalFontColor()));
    Place(Canvas, 231, 49, 32, 20, TextField(MinLevel, 2, 12, true));
    Place(Canvas, 263, 49, 10, 20, SNew(SBox).VAlign(VAlign_Center)[ Label(FText::FromString(TEXT("-")), 12, HighlightFontColor(), ETextJustify::Center) ]);
    Place(Canvas, 273, 49, 32, 20, TextField(MaxLevel, 2, 12, true));
    Place(Canvas, 332, 34, 90, 14, Label(LOCTEXT("Rarity", "Rarity"), 10, NormalFontColor()));
    // BrowseDropDown (UIDropDownMenuTemplate): CharacterCreate-LabelFrame left/middle/right, the
    // ScrollDown arrow at the right, the choice in white; clicking opens the list (Stefan 10.10).
    {
        const TCHAR* Frame = TEXT("Interface\\Glues\\CharacterCreate\\CharacterCreate-LabelFrame");
        const float X0 = 308.f, Y0 = 27.f;
        Place(Canvas, X0, Y0, 25, 64, Img(Frame, UV4(0, .1953125f, 0, 1)));
        Place(Canvas, X0 + 25, Y0, 95, 64, Img(Frame, UV4(.1953125f, .8046875f, 0, 1)));
        Place(Canvas, X0 + 120, Y0, 25, 64, Img(Frame, UV4(.8046875f, 1, 0, 1)));
        // The choice is right-aligned next to the arrow like UIDropDownMenu's text (RIGHT -43,2).
        Place(Canvas, X0 + 22, Y0 + 18, 80, 26, SNew(SBox).HAlign(HAlign_Right).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(QualityName(Quality))).Font(Fnt(10))
              .ColorAndOpacity(Quality < 0 ? HighlightFontColor() : QualityColorOf(Quality)).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]);
        Place(Canvas, X0 + 105, Y0 + 19, 24, 24, Img(bRarityOpen ? TEXT("Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Down")
            : TEXT("Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Up")));
        Place(Canvas, X0 + 16, Y0 + 18, 116, 26, SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(0)
            .OnClicked_Lambda([this]() { bRarityOpen = !bRarityOpen; PlayUiSound(TEXT("igMainMenuOptionCheckBoxOn")); Rebuild(); return FReply::Handled(); }));
    }
    Place(Canvas, 462, 47, 120, 24, CheckBox(bUsableOnly, LOCTEXT("Usable", "Usable Items")));
    Place(Canvas, 668, 46, 80, 22, PanelButton(LOCTEXT("Search", "Search"), 80, 22, FOnClicked::CreateLambda([this]() { Search(0); return FReply::Handled(); })));

    // Prev / next page arrows (BrowsePrevPageButton / BrowseNextPageButton) and the result range.
    const uint32 Total = ConnectionManager.IsValid() ? ConnectionManager->PacketHandler.AuctionTotal : 0;
    const uint32 Shown = ConnectionManager.IsValid() ? uint32(ConnectionManager->PacketHandler.AuctionOffers.Num()) : 0;
    auto Arrow = [this](const TCHAR* Name, bool bEnabled, TFunction<void()> OnClick)
    {
        const FString Base = FString::Printf(TEXT("Interface\\Buttons\\UI-SpellbookIcon-%sPage-"), Name);
        const FButtonStyle* Style = TextureButtonStyle(Base + TEXT("Up"), Base + TEXT("Down"), Base + TEXT("Disabled"));
        if (!Style) Style = &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder");
        return SNew(SButton).ButtonStyle(Style).ContentPadding(0).IsEnabled(bEnabled)
            .OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); });
    };
    Place(Canvas, 752, 42, 28, 28, Arrow(TEXT("Prev"), Page > 0, [this]() { Search(Page - 1); }));
    Place(Canvas, 780, 42, 28, 28, Arrow(TEXT("Next"), (Page + 1) * AuctionsPerPage < Total, [this]() { Search(Page + 1); }));
    if (Total)
        Place(Canvas, 560, 69, 240, 14, Label(FText::FromString(FString::Printf(TEXT("Items %u - %u (%u)"),
            Page * AuctionsPerPage + 1, Page * AuctionsPerPage + Shown, Total)), 10, HighlightFontColor(), ETextJustify::Right));

    BuildFilters(Canvas);

    // Column headers and results.
    BuildColumnHeader(Canvas, 188, 82, 228, LOCTEXT("ColRarity", "Rarity"), 1);
    BuildColumnHeader(Canvas, 414, 82, 42, LOCTEXT("ColLvl", "Lvl"), 2);
    BuildColumnHeader(Canvas, 454, 82, 92, LOCTEXT("ColTime", "Time Left"), 3);
    BuildColumnHeader(Canvas, 544, 82, 112, LOCTEXT("ColSeller", "Seller"), 4);
    BuildColumnHeader(Canvas, 654, 82, 150, LOCTEXT("ColBid", "Current Bid"), 5);
    if (ConnectionManager.IsValid())
        PlaceList(Canvas, 188, 105, 629, 306, ConnectionManager->PacketHandler.AuctionOffers, ETab::Browse);

    // Bottom bar: bid amount, Bid, Buyout, Close.
    const FWowAuctionOffer* Selected = FindOffer(SelectedAuction);
    const uint64 Me = ConnectionManager.IsValid() ? ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid : 0;
    const bool bOwn = Selected && Selected->Owner == Me;
    const uint32 BidValue = MoneyFrom(BidGold, BidSilver, BidCopper);
    const bool bCanBid = Selected && !bOwn && Selected->Bidder != Me && BidValue >= NextBid(*Selected) && BidValue <= GetPlayerMoney();
    const bool bCanBuyout = Selected && !bOwn && Selected->Buyout && Selected->Buyout <= GetPlayerMoney();
    // r268 (Stefan 13.10: a line where two textures met beside the bid box): BrowseBidPrice is
    // MoneyInputFrameTemplate (176 x 18) at BOTTOM +25,18 of the 758-wide tab frame, BrowseBidText
    // ("Bid", GameFontHighlightSmall) right-aligned at BOTTOM -75,21. The box sat 10 units left of the
    // art's inset and had no label.
    Place(Canvas, 200, 413, 104, 14, Label(LOCTEXT("BidPriceLabel", "Bid"), 10, FLinearColor::White, ETextJustify::Right));
    Place(Canvas, 316, 411, 196, 20, MoneyInput(BidGold, BidSilver, BidCopper));
    const bool bConfirmBid = Selected && PendingConfirmAuction == Selected->Id && !bPendingConfirmBuyout;
    const bool bConfirmBuyout = Selected && PendingConfirmAuction == Selected->Id && bPendingConfirmBuyout;
    Place(Canvas, 584, 411, 80, 22, PanelButton(bConfirmBid ? LOCTEXT("ConfirmBid", "Confirm") : LOCTEXT("Bid", "Bid"), 80, 22,
        FOnClicked::CreateLambda([this]() { PlaceBid(false); return FReply::Handled(); }), bCanBid));
    Place(Canvas, 664, 411, 80, 22, PanelButton(bConfirmBuyout ? LOCTEXT("ConfirmBuyout", "Confirm") : LOCTEXT("Buyout", "Buyout"), 80, 22,
        FOnClicked::CreateLambda([this]() { PlaceBid(true); return FReply::Handled(); }), bCanBuyout));
    Place(Canvas, 744, 411, 80, 22, PanelButton(LOCTEXT("Close", "Close"), 80, 22, FOnClicked::CreateLambda([this]() { Close(); return FReply::Handled(); })));
}

void SWowAuctionHouse::BuildBids(SCanvas& Canvas)
{
    BuildColumnHeader(Canvas, 64, 82, 228, LOCTEXT("ColRarity", "Rarity"), 1);
    BuildColumnHeader(Canvas, 290, 82, 42, LOCTEXT("ColLvl", "Lvl"), 2);
    BuildColumnHeader(Canvas, 330, 82, 92, LOCTEXT("ColTime", "Time Left"), 3);
    BuildColumnHeader(Canvas, 420, 82, 112, LOCTEXT("ColStatus", "Status"), 0);
    BuildColumnHeader(Canvas, 530, 82, 150, LOCTEXT("ColBid", "Current Bid"), 5);
    if (ConnectionManager.IsValid())
        PlaceList(Canvas, 64, 105, 753, 306, ConnectionManager->PacketHandler.AuctionBidOffers, ETab::Bids);

    const FWowAuctionOffer* Selected = FindOffer(SelectedAuction);
    const uint64 Me = ConnectionManager.IsValid() ? ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid : 0;
    const uint32 BidValue = MoneyFrom(BidGold, BidSilver, BidCopper);
    const bool bCanBid = Selected && Selected->Bidder != Me && BidValue >= NextBid(*Selected) && BidValue <= GetPlayerMoney();
    const bool bCanBuyout = Selected && Selected->Buyout && Selected->Buyout <= GetPlayerMoney();
    // r268: the Bids tab's art has its inset 40 units further left: BidBidPrice at BOTTOM -15,18,
    // BidBidText at BOTTOM -115,21 (the Browse spot left the inset's edge showing as a line).
    Place(Canvas, 160, 413, 104, 14, Label(LOCTEXT("BidPriceLabel", "Bid"), 10, FLinearColor::White, ETextJustify::Right));
    Place(Canvas, 276, 411, 196, 20, MoneyInput(BidGold, BidSilver, BidCopper));
    const bool bConfirmBid = Selected && PendingConfirmAuction == Selected->Id && !bPendingConfirmBuyout;
    const bool bConfirmBuyout = Selected && PendingConfirmAuction == Selected->Id && bPendingConfirmBuyout;
    Place(Canvas, 584, 411, 80, 22, PanelButton(bConfirmBid ? LOCTEXT("ConfirmBid", "Confirm") : LOCTEXT("Bid", "Bid"), 80, 22,
        FOnClicked::CreateLambda([this]() { PlaceBid(false); return FReply::Handled(); }), bCanBid));
    Place(Canvas, 664, 411, 80, 22, PanelButton(bConfirmBuyout ? LOCTEXT("ConfirmBuyout", "Confirm") : LOCTEXT("Buyout", "Buyout"), 80, 22,
        FOnClicked::CreateLambda([this]() { PlaceBid(true); return FReply::Handled(); }), bCanBuyout));
    Place(Canvas, 744, 411, 80, 22, PanelButton(LOCTEXT("Close", "Close"), 80, 22, FOnClicked::CreateLambda([this]() { Close(); return FReply::Handled(); })));
}

void SWowAuctionHouse::BuildAuctions(SCanvas& Canvas)
{
    UWowConnectionManager* Connection = ConnectionManager.Get();
    // Left panel: Create Auction.
    Place(Canvas, 28, 74, 160, 16, Label(LOCTEXT("CreateAuction", "Create Auction"), 12, NormalFontColor()));
    Place(Canvas, 28, 94, 160, 14, Label(LOCTEXT("AuctionItem", "Auction Item"), 10, HighlightFontColor()));
    // AuctionsItemButton: drop an item from the cursor; right-click a bag item; right-click to clear.
    const FWowItemEntity* SellItem = Connection && SellItemGuid ? Connection->PacketHandler.EntityManager.FindItem(SellItemGuid) : nullptr;
    if (!SellItem) SellItemGuid = 0;
    const FWowItemTemplateInfo* SellInfo = SellItem ? FWowItemPresentation::Info(Connection, SellItem->Entry) : nullptr;
    const FString SellIcon = SellItem ? FWowItemPresentation::IconPath(SellItem->Entry, SellInfo ? SellInfo->DisplayId : 0) : FString();
    TSharedRef<SWidget> ItemSlot = SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder")).Padding(0)
        .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent& Event)
        {
            UWowConnectionManager* C = ConnectionManager.Get();
            if (!C) return FReply::Handled();
            if (Event.GetEffectingButton() == EKeys::RightMouseButton) { SellItemGuid = 0; Rebuild(); }
            else if (C->HasCursorItemPayload()) { const uint64 Guid = C->GetCursorItemGuid(); C->ClearCursorPayload(); PutSellItem(Guid); }
            return FReply::Handled();
        })
        [
            SNew(SOverlay)
            + SOverlay::Slot()[ Img(TEXT("Interface\\Buttons\\UI-EmptySlot-Disabled"), UV4(.140625f, .859375f, .140625f, .859375f)) ]
            + SOverlay::Slot()[ SNew(SBox).Visibility(SellItem ? EVisibility::HitTestInvisible : EVisibility::Collapsed)[ Img(SellIcon) ] ]
            + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(FMargin(0, 0, 3 * Scale, 1 * Scale))
            [ SNew(STextBlock).Text(SellCount() > 1 ? FText::AsNumber(SellCount()) : FText::GetEmpty()).Font(Fnt(13))
              .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
        ];
    if (SellItem) ItemSlot->SetToolTip(TSharedPtr<IToolTip>(FWowItemPresentation::Tooltip(Connection, SellItem->Entry)));
    Place(Canvas, 28, 112, 37, 37, ItemSlot);
    Place(Canvas, 70, 120, 120, 24, Label(SellItem ? FText::FromString(FWowItemPresentation::Name(Connection, SellItem->Entry))
        : LOCTEXT("DropItem", "Drag an item here"), 10, SellInfo ? QualityColorOf(int32(SellInfo->Quality)) : GrayFontColor()));

    Place(Canvas, 28, 160, 160, 14, Label(LOCTEXT("StartingPrice", "Starting Price"), 10, NormalFontColor()));
    Place(Canvas, 28, 176, 196, 20, MoneyInput(StartGold, StartSilver, StartCopper));
    Place(Canvas, 28, 206, 160, 14, Label(LOCTEXT("BuyoutPrice", "Buyout Price"), 10, NormalFontColor()));
    Place(Canvas, 28, 222, 196, 20, MoneyInput(BuyoutGold, BuyoutSilver, BuyoutCopper));
    Place(Canvas, 166, 206, 60, 14, Label(LOCTEXT("Optional", "(optional)"), 9, GrayFontColor()));

    // Auction Duration: 12 / 24 / 48 hours (radio buttons).
    Place(Canvas, 28, 254, 160, 14, Label(LOCTEXT("Duration", "Auction Duration"), 10, NormalFontColor()));
    const FText Durations[3] = { LOCTEXT("H12", "12 Hours"), LOCTEXT("H24", "24 Hours"), LOCTEXT("H48", "48 Hours") };
    for (int32 Index = 0; Index < 3; ++Index)
        Place(Canvas, 28, 270 + Index * 20, 120, 20, SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(0)
            .OnClicked_Lambda([this, Index]() { Duration = Index; Rebuild(); return FReply::Handled(); })
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SBox).WidthOverride(16 * Scale).HeightOverride(16 * Scale)
                    [ Img(TEXT("Interface\\Buttons\\UI-RadioButton"), Duration == Index ? UV4(.25f, .5f, 0, 1) : UV4(0, .25f, 0, 1)) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(4 * Scale, 0, 0, 0))
                [ Label(Durations[Index], 10, HighlightFontColor()) ]
            ]);

    Place(Canvas, 28, 336, 60, 16, Label(LOCTEXT("Deposit", "Deposit:"), 10, NormalFontColor()));
    Place(Canvas, 84, 336, 120, 16, MoneyText(Deposit(), 10));

    const uint32 Start = MoneyFrom(StartGold, StartSilver, StartCopper);
    const uint32 Buyout = MoneyFrom(BuyoutGold, BuyoutSilver, BuyoutCopper);
    const bool bCanCreate = SellItemGuid && Start > 0 && (Buyout == 0 || Buyout >= Start) && Deposit() <= GetPlayerMoney();
    Place(Canvas, 26, 360, 160, 22, PanelButton(LOCTEXT("CreateButton", "Create Auction"), 160, 22,
        FOnClicked::CreateLambda([this]() { CreateAuction(); return FReply::Handled(); }), bCanCreate));

    // Right: your auctions.
    BuildColumnHeader(Canvas, 216, 82, 228, LOCTEXT("ColRarity", "Rarity"), 1);
    BuildColumnHeader(Canvas, 442, 82, 42, LOCTEXT("ColLvl", "Lvl"), 2);
    BuildColumnHeader(Canvas, 482, 82, 92, LOCTEXT("ColTime", "Time Left"), 3);
    BuildColumnHeader(Canvas, 572, 82, 112, LOCTEXT("ColHigh", "High Bidder"), 0);
    BuildColumnHeader(Canvas, 682, 82, 122, LOCTEXT("ColBid", "Current Bid"), 5);
    if (Connection)
        PlaceList(Canvas, 216, 105, 601, 306, Connection->PacketHandler.AuctionOwnOffers, ETab::Auctions);

    const bool bCanCancel = FindOffer(SelectedAuction) != nullptr;
    Place(Canvas, 622, 411, 122, 22, PanelButton(bPendingCancel ? LOCTEXT("ConfirmCancel", "Confirm Cancel") : LOCTEXT("CancelAuction", "Cancel Auction"), 122, 22,
        FOnClicked::CreateLambda([this]() { CancelSelectedAuction(); return FReply::Handled(); }), bCanCancel));
    Place(Canvas, 744, 411, 80, 22, PanelButton(LOCTEXT("Close", "Close"), 80, 22, FOnClicked::CreateLambda([this]() { Close(); return FReply::Handled(); })));
}

#undef LOCTEXT_NAMESPACE
