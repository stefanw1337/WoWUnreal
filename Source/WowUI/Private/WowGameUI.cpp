#include "WowGameUI.h"
#include "WowUiSurfaces.h"
#include "WowFrameManager.h"
#include "WowFrameLayout.h"
#include "WowPanelLayout.h"
#include "Coord/WowCoordinate.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWowLootWindow.h"
#include "Widgets/SWowTradeWindow.h"
#include "Widgets/SWowVendorWindow.h"
#include "Widgets/SWowQuestDialog.h"
#include "WowConnectionManager.h"
#include "WowUIManager.h"
#include "WowEventSystem.h"
#include "WowClientPreferences.h"
#include "WowNpcDialogState.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Components/Widget.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowGameUI, Log, All);

UWowGameUI::UWowGameUI()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UWowGameUI::BeginPlay()
{
    Super::BeginPlay();
    CreateWidgets();
}

void UWowGameUI::SetConnectionManager(UWowConnectionManager* InConnectionManager)
{
    ConnectionManager = InConnectionManager;
    if(QuestDialog.IsValid())QuestDialog->SetConnectionManager(ConnectionManager);
}

// The Auto Loot Key (Options > Interface > Combat: Shift, Ctrl, Alt or None) is held.
static bool IsAutoLootKeyDown()
{
    const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
    switch (FWowClientPreferences::GetAutoLootKey())
    {
    case 0: return Mods.IsShiftDown();
    case 1: return Mods.IsControlDown();
    case 2: return Mods.IsAltDown();
    default: return false;
    }
}

void UWowGameUI::PrepareLootRequest(uint64 LootGuid)
{
    PreparedLootGuid = LootGuid;
    bPreparedAutoLoot = FWowClientPreferences::GetWorldOption(TEXT("autolootdefault")) != IsAutoLootKeyDown();
    PreparedLootCursor = FSlateApplication::Get().GetCursorPos();
}

bool UWowGameUI::CloseLootIfEmpty()
{
    if (!CurrentLootGuid || CurrentLootMoney || CurrentLootItems.ContainsByPredicate(
        [](const FWowLootItem& Item) { return !Item.bLooted; })) return false;
    OnCloseLoot(); // Release the server's loot session once, after acknowledgement.
    HideLootWindow();
    return true;
}

void UWowGameUI::ShowLootWindow(uint64 LootGuid, uint8 LootType, uint32 Gold, const TArray<FWowLootItem>& Items)
{
    CreateWidgets();
    if (LootWindow.IsValid())
    {
        if (PreparedLootGuid != LootGuid) PrepareLootRequest(LootGuid);
        // The Auto Loot Key inverts auto loot (AUTOLOOTTOGGLE). Holding it when the loot arrives counts too:
        // gathering/chests open loot after a cast, long after the click.
        const bool AutoLoot = bPreparedAutoLoot && !(FWowClientPreferences::GetWorldOption(TEXT("autolootdefault"))
            && IsAutoLootKeyDown());
        PreparedLootGuid = 0;
        CurrentLootGuid = LootGuid;
        CurrentLootType = LootType;
        CurrentLootMoney = Gold;
        CurrentLootItems = Items;
        if (CloseLootIfEmpty()) return;
        UWowUIManager* UI = GetWorld() && GetWorld()->GetGameInstance()
            ? GetWorld()->GetGameInstance()->GetSubsystem<UWowUIManager>() : nullptr;
        LootWindow->SetContext(ConnectionManager, UI);
        LootWindow->UpdateLoot(LootGuid, LootType, Gold, Items);
        bLootAtMouse = FWowClientPreferences::GetWorldOption(TEXT("lootundermouse"));
        if (bLootAtMouse && GEngine && GEngine->GameViewport)
        {
            if (auto Viewport = GEngine->GameViewport->GetGameViewportWidget())
            {
                LootWindow->SlatePrepass();
                const FGeometry& Geometry = Viewport->GetCachedGeometry();
                const FVector2D Size = Geometry.GetLocalSize();
                const FVector2D Desired = LootWindow->GetDesiredSize();
                const FVector2D Mouse = Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
                const FVector2D Anchor = LootWindow->GetFirstLootAnchor();
                LootWindowPosition = FVector2D(
                    FMath::Clamp(Mouse.X - Anchor.X, 0.0f, FMath::Max(0.0f, Size.X - Desired.X)),
                    FMath::Clamp(Mouse.Y - Anchor.Y, 0.0f, FMath::Max(0.0f, Size.Y - Desired.Y)));
                LootOpenCursor = FSlateApplication::Get().GetCursorPos();
                LootAnchorFixFrames = 3;
            }
        }
        if (AutoLoot) OnLootAll();
        UE_LOG(LogWowGameUI, Log, TEXT("Showing loot window"));
    }
}

void UWowGameUI::HideLootWindow()
{
    CurrentLootGuid = 0;
    CurrentLootMoney = 0;
    CurrentLootItems.Reset();
    if (LootWindow.IsValid())
    {
        LootWindow->CloseLoot();
        UE_LOG(LogWowGameUI, Log, TEXT("Hiding loot window"));
    }
}

void UWowGameUI::ShowVendorWindow(uint64 VendorGuid, const TArray<FWowVendorItem>& Items)
{
    if(!IsNpcInteractionInRange(VendorGuid)) {HideQuestDialog();HideVendorWindow();return;}
    CreateWidgets();
    if (VendorWindow.IsValid())
    {
        HideQuestDialog();
        CurrentVendorGuid=VendorGuid;
        UWowUIManager* UI = GetWorld() && GetWorld()->GetGameInstance()
            ? GetWorld()->GetGameInstance()->GetSubsystem<UWowUIManager>() : nullptr;
        VendorWindow->SetContext(ConnectionManager, UI);
        VendorWindow->UpdateVendor(VendorGuid, Items);
        // MerchantFrame is 384x512 at the current UI scale.
        if (FManagedPanel* Panel = ManagedPanels.Find(TEXT("NativeVendor"))) Panel->Size = VendorWindow->GetPanelSize();
        UE_LOG(LogWowGameUI, Log, TEXT("Showing vendor window"));
    }
}

void UWowGameUI::UpdateVendorPlayerInventory(const TArray<FWowItem>& Items)
{
    if (VendorWindow.IsValid())
    {
        VendorWindow->UpdatePlayerInventory(Items);
        UE_LOG(LogWowGameUI, Log, TEXT("Updated vendor player inventory"));
    }
}

void UWowGameUI::HideVendorWindow()
{
    CurrentVendorGuid=0;
    if (VendorWindow.IsValid())
    {
        VendorWindow->CloseVendor();
        UE_LOG(LogWowGameUI, Log, TEXT("Hiding vendor window"));
    }
}

void UWowGameUI::ShowQuestDialog(const FWowQuestDetails& QuestDetails)
{
    if(!IsNpcInteractionInRange(QuestDetails.QuestGiverGuid)) {HideQuestDialog();HideVendorWindow();return;}
    CreateWidgets();
    if (QuestDialog.IsValid())
    {
        if (ConnectionManager) QuestDialog->SetPlayerName(ConnectionManager->GetCharacterName());
        HideVendorWindow();
        CurrentQuestNpcGuid=QuestDetails.QuestGiverGuid;
        QuestDialog->SetConnectionManager(ConnectionManager);
        QuestDialog->ShowQuestDetails(QuestDetails);
        PublishNpcDialog(uint8(FWowNpcDialogState::EMode::Detail), nullptr, &QuestDetails);
        UE_LOG(LogWowGameUI, Log, TEXT("Showing quest dialog"));
    }
}

void UWowGameUI::ShowQuestList(const FWowGossipMenu& Menu)
{
    if(!IsNpcInteractionInRange(Menu.NpcGuid)) {HideQuestDialog();HideVendorWindow();return;}
    CreateWidgets();
    if (QuestDialog.IsValid())
    {
        if (ConnectionManager) QuestDialog->SetPlayerName(ConnectionManager->GetCharacterName());
        HideVendorWindow();
        CurrentQuestNpcGuid=Menu.NpcGuid;
        QuestDialog->SetConnectionManager(ConnectionManager);
        QuestDialog->ShowQuestList(Menu);
        // A gossip menu (options or a gossip menu id) or the quest giver's greeting list.
        PublishNpcDialog(uint8(Menu.Options.Num() || Menu.MenuId ? FWowNpcDialogState::EMode::Gossip
            : FWowNpcDialogState::EMode::Greeting), &Menu, nullptr);
        UE_LOG(LogWowGameUI, Log, TEXT("Showing NPC quest list (%d quests)"), Menu.Quests.Num());
    }
}

void UWowGameUI::ShowQuestRewardDialog(const FWowQuestDetails& QuestDetails)
{
    if(!IsNpcInteractionInRange(QuestDetails.QuestGiverGuid)) {HideQuestDialog();HideVendorWindow();return;}
    CreateWidgets();
    if (QuestDialog.IsValid())
    {
        if (ConnectionManager) QuestDialog->SetPlayerName(ConnectionManager->GetCharacterName());
        HideVendorWindow();
        CurrentQuestNpcGuid=QuestDetails.QuestGiverGuid;
        QuestDialog->SetConnectionManager(ConnectionManager);
        QuestDialog->ShowQuestReward(QuestDetails);
        PublishNpcDialog(uint8(FWowNpcDialogState::EMode::Reward), nullptr, &QuestDetails);
        UE_LOG(LogWowGameUI, Log, TEXT("Showing quest reward dialog"));
    }
}

void UWowGameUI::ShowQuestProgressDialog(const FWowQuestDetails& QuestDetails)
{
    if(!IsNpcInteractionInRange(QuestDetails.QuestGiverGuid)) {HideQuestDialog();HideVendorWindow();return;}
    CreateWidgets();
    if (QuestDialog.IsValid())
    {
        if (ConnectionManager) QuestDialog->SetPlayerName(ConnectionManager->GetCharacterName());
        HideVendorWindow();
        CurrentQuestNpcGuid=QuestDetails.QuestGiverGuid;
        QuestDialog->SetConnectionManager(ConnectionManager);
        QuestDialog->ShowQuestProgress(QuestDetails);
        PublishNpcDialog(uint8(FWowNpcDialogState::EMode::Progress), nullptr, &QuestDetails);
        UE_LOG(LogWowGameUI, Log, TEXT("Showing quest progress dialog"));
    }
}

void UWowGameUI::HideQuestDialog()
{
    if (FWowNpcDialogState::Get().Mode != FWowNpcDialogState::EMode::None)
        PublishNpcDialog(uint8(FWowNpcDialogState::EMode::None), nullptr, nullptr);
    CurrentQuestNpcGuid=0;
    if (QuestDialog.IsValid())
    {
        QuestDialog->CloseDialog();
        UE_LOG(LogWowGameUI, Log, TEXT("Hiding quest dialog"));
    }
}

bool UWowGameUI::IsQuestDialogVisible() const
{
    return QuestDialog.IsValid() && QuestDialog->GetVisibility().IsVisible();
}

bool UWowGameUI::IsPointerOverWindow(const FVector2D& ScreenPosition) const
{
    const auto Contains = [&ScreenPosition](const TSharedPtr<SWidget>& Widget, const FMargin& Insets = FMargin(0))
    {
        if (!Widget.IsValid() || !Widget->GetCachedGeometry().IsUnderLocation(ScreenPosition)) return false;
        // The art's transparent margin is not part of the window (Stefan 10.10: the chat friends
        // button below the open Friends window could not close it, the window's empty bottom
        // margin lay over the button and took the click).
        if (Insets.Left > 0.f || Insets.Top > 0.f || Insets.Right > 0.f || Insets.Bottom > 0.f)
        {
            const FGeometry& Geometry = Widget->GetCachedGeometry();
            const FVector2D Local = Geometry.AbsoluteToLocal(ScreenPosition);
            const FVector2D LocalSize = Geometry.GetLocalSize();
            if (Local.X < LocalSize.X * Insets.Left || Local.Y < LocalSize.Y * Insets.Top
                || Local.X > LocalSize.X * (1.f - Insets.Right) || Local.Y > LocalSize.Y * (1.f - Insets.Bottom)) return false;
        }
        // Shown only if it and every parent are visible: a closed panel whose wrapper was
        // collapsed keeps its own visibility and its last geometry, and swallowed clicks there.
        for (TSharedPtr<SWidget> Walk = Widget; Walk.IsValid(); Walk = Walk->GetParentWidget())
            if (!Walk->GetVisibility().IsVisible()) return false;
        return true;
    };
    for(const auto& Pair:ManagedPanels)if(Contains(Pair.Value.Widget.Pin(),Pair.Value.HitInsets))return true;
    const FManagedPanel* QuestPanel=ManagedPanels.Find(TEXT("NativeQuest"));
    return Contains(QuestDialog,QuestPanel?QuestPanel->HitInsets:FMargin(0)) || Contains(VendorWindow) || Contains(LootWindow) || Contains(TradeWindow);
}

void UWowGameUI::ShowTradeWindow(const FWowTradeState& TradeState)
{
    CreateWidgets();
    if (TradeWindow.IsValid())
    {
        TradeWindow->UpdateTrade(TradeState);
        UE_LOG(LogWowGameUI, Log, TEXT("Showing trade window"));
    }
}

void UWowGameUI::HideTradeWindow()
{
    if (TradeWindow.IsValid())
    {
        TradeWindow->CloseTrade();
        UE_LOG(LogWowGameUI, Log, TEXT("Hiding trade window"));
    }
}

void UWowGameUI::CreateWidgets()
{
    if (bWidgetsCreated) return;

    // Create loot window
    LootWindow = SNew(SWowLootWindow)
        .OnLootItem_UObject(this, &UWowGameUI::OnLootItem)
        .OnLootAll_UObject(this, &UWowGameUI::OnLootAll)
        .OnCloseLoot_UObject(this, &UWowGameUI::OnCloseLoot);

    // Create vendor window
    VendorWindow = SNew(SWowVendorWindow)
        .OnBuyItem_UObject(this, &UWowGameUI::OnBuyItem)
        .OnSellItem_UObject(this, &UWowGameUI::OnSellItem)
        .OnCloseVendor_UObject(this, &UWowGameUI::OnCloseVendor);

    // Create quest dialog
    QuestDialog = SNew(SWowQuestDialog)
        .OnQuestAccept_UObject(this, &UWowGameUI::OnQuestAccept)
        .OnQuestSelect_UObject(this, &UWowGameUI::OnQuestSelect)
        .OnQuestProgress_UObject(this, &UWowGameUI::OnQuestProgress)
        .OnQuestContinue_UObject(this, &UWowGameUI::OnQuestContinue)
        .OnQuestComplete_UObject(this, &UWowGameUI::OnQuestComplete)
        .OnGossipSelect_UObject(this, &UWowGameUI::OnGossipSelect)
        .OnQuestDecline_UObject(this, &UWowGameUI::OnQuestDecline);

    TradeWindow = SNew(SWowTradeWindow)
        .OnTradeAccept_UObject(this, &UWowGameUI::OnAcceptTrade)
        .OnTradeCancel_UObject(this, &UWowGameUI::OnCancelTrade);

    if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
        QuestDialog->SetUIManager(GI->GetSubsystem<UWowUIManager>());
    AddWidgetsToViewport();
    bWidgetsCreated = true;
    RegisterFrameMoveInput();

    UE_LOG(LogWowGameUI, Log, TEXT("Created WoW game UI widgets"));
}

void UWowGameUI::AddWidgetsToViewport()
{
    if (!GEngine || !GEngine->GameViewport) return;

    // Add widgets to viewport with proper positioning
    auto LootContainer = SNew(SConstraintCanvas)
        .Visibility(EVisibility::SelfHitTestInvisible)
        + SConstraintCanvas::Slot()
        .Alignment(FVector2D::ZeroVector)
        .Anchors_Lambda([this]() { return bLootAtMouse || FWowFrameLayout::Positions().Contains(TEXT("NativeLoot")) ? FAnchors(0.0f,0.0f) : FAnchors(0.5f,0.3f); })
        .AutoSize(true)
        .Offset_Lambda([this]() {
            if(bLootAtMouse)return FMargin(LootWindowPosition.X,LootWindowPosition.Y,0,0);
            if(const auto* Saved=FWowFrameLayout::Positions().Find(TEXT("NativeLoot")))
                if(GEngine&&GEngine->GameViewport)
                    if(auto V=GEngine->GameViewport->GetGameViewportWidget())
                    {const auto P=FWowFrameLayout::ClampPosition(*Saved,LootWindow->GetDesiredSize(),V->GetCachedGeometry().GetLocalSize());return FMargin(P.X,P.Y,0,0);}
            return FMargin(-126,-70,0,0);
        })
        [
            LootWindow.ToSharedRef()
        ];
    ViewportContainers.Add(LootContainer);
    // Above the FrameXML UI (z 210 panels): at the default z the UI root took the clicks.
    FWowUiSurfaces::Add(LootContainer, EWowUiZone::Windows, 215, TEXT("NativeLoot"));

    AddManagedPanel(TEXT("NativeVendor"),VendorWindow.ToSharedRef(),VendorWindow->GetPanelSize(),[this](){HideVendorWindow();});
    AddManagedPanel(TEXT("NativeQuest"),QuestDialog.ToSharedRef(),FVector2D(430,600),[this](){HideQuestDialog();});
    // QuestFrame/GossipFrame art ends at about 350 x 440 of 384 x 512.
    SetPanelHitInsets(TEXT("NativeQuest"),FMargin(0.f,0.f,34.f/384.f,72.f/512.f));
    AddManagedPanel(TEXT("NativeTrade"),TradeWindow.ToSharedRef(),FVector2D(480,340));
}


static FVector2D LivePanelSize(const TWeakPtr<SWidget>& WeakWidget,const FVector2D& Fallback)
{
    if(const TSharedPtr<SWidget> Widget=WeakWidget.Pin())
    {
        const FVector2D Desired=Widget->GetDesiredSize();
        if(Desired.X>1.f&&Desired.Y>1.f)return Desired;
    }
    return Fallback;
}

void UWowGameUI::AddManagedPanel(FName Name,TSharedRef<SWidget> Widget,FVector2D Size,TFunction<void()> Close)
{
    if(!GEngine||!GEngine->GameViewport||ManagedPanels.Contains(Name))return;
    FManagedPanel Panel;Panel.Widget=Widget;Panel.Size=Size;Panel.Close=MoveTemp(Close);ManagedPanels.Add(Name,Panel);
    Widget->SetRenderTransformPivot(FVector2D::ZeroVector);
    auto Container=SNew(SConstraintCanvas).Visibility(EVisibility::SelfHitTestInvisible)
        +SConstraintCanvas::Slot().Alignment(FVector2D::ZeroVector)
        // The live size (the window's own desired size follows the UI scale; the size given at
        // registration did not, which cut the gossip window off on the right - Stefan 10.10).
        .Offset_Lambda([this,Name](){const auto& P=ManagedPanels.FindChecked(Name);const FVector2D S=LivePanelSize(P.Widget,P.Size);return FMargin(P.Position.X,P.Position.Y,S.X,S.Y);})
        [Widget];
    ViewportContainers.Add(Container);
    FWowUiSurfaces::Add(Container, EWowUiZone::Windows, 210, Name);
}

void UWowGameUI::SetPanelHitInsets(FName Name,const FMargin& FractionOfSize)
{
    if(FManagedPanel* Panel=ManagedPanels.Find(Name))Panel->HitInsets=FractionOfSize;
}

void UWowGameUI::ReflowPanels()
{
    if(!GEngine||!GEngine->GameViewport)return;
    auto Viewport=GEngine->GameViewport->GetGameViewportWidget();if(!Viewport.IsValid())return;
    const FVector2D ViewSize=Viewport->GetCachedGeometry().GetLocalSize();if(ViewSize.X<100||ViewSize.Y<100)return;
    UWowUIManager* UI=GetWorld()&&GetWorld()->GetGameInstance()?GetWorld()->GetGameInstance()->GetSubsystem<UWowUIManager>():nullptr;
    auto* Frames=UI?UI->GetFrameManager():nullptr;
    float WowToSlate=1;
    TMap<FName,FVector2D> Sizes;
    // r265 (Stefan 12.10, r03t40: C then O overlapped, O then C left a wide gap, C/N the same): a
    // FrameXML window's slot was its XML width times ViewSize/UIParent width, but it is drawn at the
    // frame manager's own scale (DPI and interface scale), so its slot and its picture differed.
    // Each window now uses the size it is really laid out at, and its anchor offset is converted
    // with the same measured scale, so the row's gaps are what Arrange computed.
    TMap<FName,float> SlatePerFrameUnit;
    const float ViewportAbsScale=Viewport->GetCachedGeometry().Scale;
    if(Frames)
    {
        if(const auto* Parent=Frames->GetFrameDef(Frames->FindFrame(TEXT("UIParent"))))
            if(Parent->Width>0)WowToSlate=ViewSize.X/Parent->Width;
        for(const TCHAR* Name:{TEXT("CharacterFrame"),TEXT("FriendsFrame"),TEXT("SpellBookFrame"),TEXT("TalentFrame"),TEXT("PlayerTalentFrame"),TEXT("GuildFrame"),TEXT("MailFrame"),TEXT("BankFrame"),TEXT("QuestLogFrame"),TEXT("DressUpFrame")})
        {
            // The native friends window replaces FrameXML's FriendsFrame; a FriendsFrame left
            // "shown" behind it took a slot of its own and pushed the character sheet a whole
            // window width to the right (Stefan 11.10: O then C).
            if(FCString::Strcmp(Name,TEXT("FriendsFrame"))==0&&ManagedPanels.Contains(FName(TEXT("NativeFriends"))))continue;
            const int64 Handle=Frames->FindFrame(Name);
            if(Frames->IsFrameVisible(Handle))
                if(const auto* Def=Frames->GetFrameDef(Handle))
                {
                    FVector2D Size=FVector2D(FMath::Max(Def->Width,320.0f),FMath::Max(Def->Height,480.0f))*WowToSlate;
                    if(const UWidget* FrameWidget=Frames->GetWidgetForHandle(Handle);FrameWidget&&ViewportAbsScale>0.f&&Def->Width>1.f)
                    {
                        // Layout geometry: the row's render scale is not part of it.
                        const FVector2D Measured=FrameWidget->GetCachedGeometry().GetAbsoluteSize()/ViewportAbsScale;
                        if(Measured.X>8.f&&Measured.Y>8.f)
                        {
                            Size=Measured;
                            SlatePerFrameUnit.Add(Name,Measured.X/Def->Width);
                        }
                    }
                    Sizes.Add(Name,Size);
                }
        }
    }
    for(const auto& Pair:ManagedPanels)
        if(auto Widget=Pair.Value.Widget.Pin())
            if(Widget->GetVisibility().IsVisible())Sizes.Add(Pair.Key,LivePanelSize(Pair.Value.Widget,Pair.Value.Size));
    PanelOrder.RemoveAll([&](FName Name){return !Sizes.Contains(Name);});
    // Stable order: existing windows keep their slot, newly visible windows append.
    TArray<FName> Added;for(const auto& Pair:Sizes)if(!PanelOrder.Contains(Pair.Key))Added.Add(Pair.Key);
    Added.Sort(FNameLexicalLess());PanelOrder.Append(Added);
    // The dressing room always sits right of the window it previews from (auction house, bags...).
    if(PanelOrder.Remove(FName(TEXT("DressUpFrame"))))PanelOrder.Add(FName(TEXT("DressUpFrame")));
    // At most two windows side by side, like UIParent's left and center slots; a doublewide window
    // (wider than tall: quest log, auction house, achievements, group finder) takes both. The oldest
    // closable window closes when a newer one needs the room (Stefan 10.10: three windows were open
    // and the row was shrunk to fit, which looked like a zoom).
    {
        // The dressing room rides along next to whatever it previews (Stefan 11.10: it opened behind
        // the auction house); it never closes another window, the row shrinks to fit instead.
        auto Weight=[&Sizes](FName Name){if(Name==FName(TEXT("DressUpFrame")))return 0;const FVector2D S=Sizes.FindChecked(Name);return S.X>S.Y*1.15f?2:1;};
        int32 Total=0;for(FName Name:PanelOrder)Total+=Weight(Name);
        static TMap<FName,double> RecentlyClosed;
        const double Now=FPlatformTime::Seconds();
        for(int32 I=0;Total>2&&I<PanelOrder.Num()-1;)
        {
            const FName Name=PanelOrder[I];
            if(Weight(Name)==0){++I;continue;}
            const double* Last=RecentlyClosed.Find(Name);
            bool bClosed=false;
            if(!Last||Now-*Last>1.0)
            {
                if(FManagedPanel* Native=ManagedPanels.Find(Name))
                {
                    if(Native->Close){Native->Close();bClosed=true;}
                }
                else if(UI&&UI->GetEventSystem())
                {
                    UI->GetEventSystem()->ExecuteLuaString(FString::Printf(
                        TEXT("if %s and HideUIPanel then HideUIPanel(%s) end"),*Name.ToString(),*Name.ToString()),TEXT("=ClosePanelForRoom"));
                    bClosed=true;
                }
            }
            if(bClosed)
            {
                UE_LOG(LogWowGameUI,Log,TEXT("Closing %s: a newer window needs its place"),*Name.ToString());
                RecentlyClosed.Add(Name,Now);Total-=Weight(Name);Sizes.Remove(Name);PanelOrder.RemoveAt(I);
            }
            else ++I;
        }
    }
    TArray<FVector2D> OrderedSizes;for(FName Name:PanelOrder)OrderedSizes.Add(Sizes.FindChecked(Name));
    auto Placements=FWowPanelLayout::Arrange(OrderedSizes,ViewSize);
    // r260 (Stefan 12.10: the dressing room opened from the auction house lay off the right edge of
    // the screen): whatever the sizes in the row added up to, every window stays inside the screen -
    // one that would reach past the right edge moves left (over the window before it, like the
    // original's side dressing room next to a wide window).
    for(int32 I=0;I<Placements.Num()&&I<OrderedSizes.Num();++I)
    {
        auto& Place=Placements[I];
        const FVector2D Scaled=OrderedSizes[I]*Place.Scale;
        Place.Position.X=FMath::Max(16.0,FMath::Min((double)Place.Position.X,(double)ViewSize.X-16.0-Scaled.X));
        Place.Position.Y=FMath::Max(0.0,FMath::Min((double)Place.Position.Y,(double)ViewSize.Y-Scaled.Y));
        if(Place.Position.X+Scaled.X>ViewSize.X-8.0||I+1==Placements.Num())
            UE_LOG(LogWowGameUI,Verbose,TEXT("Panel %s at (%.0f,%.0f) size %.0fx%.0f scale %.2f"),*PanelOrder[I].ToString(),
                Place.Position.X,Place.Position.Y,OrderedSizes[I].X,OrderedSizes[I].Y,Place.Scale);
    }
    {
        // Diagnostics: the row's windows and their sizes, once per change.
        static FString LastRow;
        FString Row;
        for(int32 I=0;I<PanelOrder.Num()&&I<Placements.Num();++I)
            Row+=FString::Printf(TEXT(" %s@(%.0f,%.0f %.0fx%.0f x%.2f)"),*PanelOrder[I].ToString(),Placements[I].Position.X,Placements[I].Position.Y,
                OrderedSizes[I].X,OrderedSizes[I].Y,Placements[I].Scale);
        if(Row!=LastRow){LastRow=Row;UE_LOG(LogWowGameUI,Log,TEXT("Window row (view %.0fx%.0f):%s"),ViewSize.X,ViewSize.Y,*Row);}
    }
    for(int32 I=0;I<PanelOrder.Num();++I)
    {
        const auto& Place=Placements[I];
        if(auto* Native=ManagedPanels.Find(PanelOrder[I]))
        {
            Native->Position=Place.Position;Native->Scale=Place.Scale;
            if(const auto* Saved=FWowFrameLayout::Positions().Find(PanelOrder[I].ToString()))
                Native->Position=FWowFrameLayout::ClampPosition(*Saved,Native->Size*Place.Scale,ViewSize);
            if(auto Widget=Native->Widget.Pin())Widget->SetRenderTransform(FSlateRenderTransform(Place.Scale));
        }
        else if(Frames)
        {
            FVector2D Position=Place.Position;
            if(const auto* Saved=FWowFrameLayout::Positions().Find(PanelOrder[I].ToString()))
                Position=FWowFrameLayout::ClampPosition(*Saved,OrderedSizes[I]*Place.Scale,ViewSize);
            const float* Measured=SlatePerFrameUnit.Find(PanelOrder[I]);
            Frames->PlaceDockPanel(Frames->FindFrame(PanelOrder[I].ToString()),Position/(Measured&&*Measured>0.f?*Measured:WowToSlate),Place.Scale);
        }
    }
}

bool UWowGameUI::IsNpcInteractionInRange(uint64 Guid) const
{
    if(!Guid||!ConnectionManager||!GetWorld()) return false;
    const APawn* Pawn=UGameplayStatics::GetPlayerPawn(this,0);
    const auto* Npc=ConnectionManager->PacketHandler.EntityManager.Find(Guid);
    if(!Pawn||!Npc) return false;
    FVector Feet=Pawn->GetActorLocation();
    if(const auto* Capsule=Cast<UCapsuleComponent>(Pawn->GetRootComponent())) Feet.Z-=Capsule->GetScaledCapsuleHalfHeight();
    const FVector Target=FWowCoordinate::WowToUE(Npc->Movement.Position);
    // INTERACTION_DISTANCE (5.5 yards) is measured between the two bodies' edges (combat reach),
    // about 7.5 m centre to centre (Stefan 10.10: a vendor across his counter was out of reach).
    if(FVector::DistSquared(Feet,Target)>FMath::Square(750.f)) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(NpcInteractionVisibility),true,Pawn);
    FHitResult Hit;
    // Gameobjects (corpses, chests, wanted posters) are WorldStatic themselves: the trace
    // hits their own mesh and the dialog closed at once. Only geometry well before the
    // target blocks the interaction.
    // Head height, so a counter or a table between you and the NPC does not block the talk.
    const FVector From=Feet+FVector(0,0,160), To=Target+FVector(0,0,160);
    if(!GetWorld()->LineTraceSingleByObjectType(Hit,From,To,FCollisionObjectQueryParams(ECC_WorldStatic),Query)) return true;
    return FVector::Dist(Hit.ImpactPoint,To)<=250.f;
}

void UWowGameUI::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    // Saved Ctrl-drag offsets of native elements (cheap; also undone by "Reset frames").
    if(!MovableWidgets.IsEmpty())ApplyMovableWidgets();
    if(ConnectionManager)
    {
        if(CurrentVendorGuid&&!IsNpcInteractionInRange(CurrentVendorGuid))HideVendorWindow();
        if(CurrentQuestNpcGuid&&!IsNpcInteractionInRange(CurrentQuestNpcGuid))HideQuestDialog();
        // LOOT_CLOSED: walking out of loot range of a corpse or gameobject releases the loot
        // (bag items being opened have no world position and stay open).
        if(CurrentLootGuid&&LootWindow.IsValid()&&LootWindow->GetVisibility().IsVisible())
        {
            const auto* Source=ConnectionManager->PacketHandler.EntityManager.Find(CurrentLootGuid);
            const APawn* Pawn=UGameplayStatics::GetPlayerPawn(this,0);
            if(Pawn&&(!Source||Source->IsUnit()||Source->IsGameObject()))
            {
                bool bInRange=false;
                if(Source&&!Source->Movement.Position.IsNearlyZero())
                {
                    FVector Feet=Pawn->GetActorLocation();
                    if(const auto* Capsule=Cast<UCapsuleComponent>(Pawn->GetRootComponent()))Feet.Z-=Capsule->GetScaledCapsuleHalfHeight();
                    const float CmPerYard=(FWowCoordinate::WowToUE(FVector(1,0,0))-FWowCoordinate::WowToUE(FVector::ZeroVector)).Size();
                    bInRange=FVector::DistSquared(Feet,FWowCoordinate::WowToUE(Source->Movement.Position))<=FMath::Square(8.f*CmPerYard);
                }
                else if(Source)bInRange=true; // no position known yet: never close on bad data
                if(!bInRange){UE_LOG(LogWowGameUI,Log,TEXT("Loot source %llu out of range: closing loot"),CurrentLootGuid);OnCloseLoot();}
            }
        }
    }
    // Loot under the mouse: once the window is arranged, put its first slot exactly under the
    // cursor where it was opened (the pre-layout estimate was off by the title/padding scale).
    if (LootAnchorFixFrames > 0 && LootWindow.IsValid() && LootWindow->GetVisibility().IsVisible() && GEngine && GEngine->GameViewport)
    {
        FVector2D Slot;
        if (LootWindow->GetFirstLootAbsoluteAnchor(Slot))
            if (auto Viewport = GEngine->GameViewport->GetGameViewportWidget())
            {
                const FGeometry& Geometry = Viewport->GetCachedGeometry();
                const FVector2D Delta = Geometry.AbsoluteToLocal(LootOpenCursor) - Geometry.AbsoluteToLocal(Slot);
                const FVector2D Size = Geometry.GetLocalSize();
                const FVector2D Desired = LootWindow->GetDesiredSize();
                LootWindowPosition = FVector2D(
                    FMath::Clamp(LootWindowPosition.X + Delta.X, 0.0f, FMath::Max(0.0f, Size.X - Desired.X)),
                    FMath::Clamp(LootWindowPosition.Y + Delta.Y, 0.0f, FMath::Max(0.0f, Size.Y - Desired.Y)));
                if (Delta.SizeSquared() < 1.f) LootAnchorFixFrames = 0; else --LootAnchorFixFrames;
            }
    }
    ReflowPanels();
    if(GetWorld()&&GetWorld()->GetGameInstance())
        if(auto* UI=GetWorld()->GetGameInstance()->GetSubsystem<UWowUIManager>())
            if(auto* Frames=UI->GetFrameManager())Frames->SetChatPointerBlocked(IsPointerOverWindow(FSlateApplication::Get().GetCursorPos()));
}
bool UWowGameUI::CloseNpcWindows()
{
    const bool LootVisible = LootWindow.IsValid() && LootWindow->GetVisibility().IsVisible();
    const bool Visible=LootVisible||(VendorWindow.IsValid()&&VendorWindow->GetVisibility().IsVisible())||IsQuestDialogVisible();
    if (LootVisible) OnCloseLoot();
    if(Visible){HideVendorWindow();HideQuestDialog();}return Visible;
}
void UWowGameUI::EndPlay(const EEndPlayReason::Type Reason)
{
    UnregisterFrameMoveInput();
    if(GEngine&&GEngine->GameViewport)
        for(const auto& Container:ViewportContainers)if(Container.IsValid())FWowUiSurfaces::Remove(Container.ToSharedRef());
    ViewportContainers.Reset();ManagedPanels.Reset();PanelOrder.Reset();
    RemoveWidgetsFromViewport();Super::EndPlay(Reason);
}

void UWowGameUI::RemoveWidgetsFromViewport()
{
    if (GEngine && GEngine->GameViewport)
    {
        if (LootWindow.IsValid())
        {
            FWowUiSurfaces::Remove(LootWindow.ToSharedRef());
        }
        if (VendorWindow.IsValid())
        {
            FWowUiSurfaces::Remove(VendorWindow.ToSharedRef());
        }
        if (QuestDialog.IsValid())
        {
            FWowUiSurfaces::Remove(QuestDialog.ToSharedRef());
        }
        if (TradeWindow.IsValid())
        {
            FWowUiSurfaces::Remove(TradeWindow.ToSharedRef());
        }
    }
}

void UWowGameUI::OnLootItem(int32 LootSlot)
{
    if (ConnectionManager)
    {
        const FWowLootItem* Item = CurrentLootItems.FindByPredicate(
            [LootSlot](const FWowLootItem& Entry) { return Entry.Index == LootSlot; });
        if (!Item || Item->bLooted || (Item->SlotType != 0 && Item->SlotType != 4))
        {
            // LOOT_SLOT_TYPE: 1 roll in progress, 2 master looter decides, 3 locked (not yours).
            UE_LOG(LogWowGameUI, Warning, TEXT("Loot slot %d not lootable: %s"), LootSlot,
                !Item ? TEXT("not in loot list") : Item->bLooted ? TEXT("already looted") :
                Item->SlotType == 1 ? TEXT("roll in progress") : Item->SlotType == 2 ? TEXT("master looter") : TEXT("locked"));
            return;
        }
        ConnectionManager->SendLootItem(LootSlot);
        UE_LOG(LogWowGameUI, Log, TEXT("Looting item slot %d"), LootSlot);
    }
}

void UWowGameUI::OnLootAll()
{
    if (!ConnectionManager || CurrentLootGuid == 0) return;
    if (CurrentLootMoney > 0) ConnectionManager->SendRawPacket(WowOpcode::CMSG_LOOT_MONEY);
    for (const FWowLootItem& Item : CurrentLootItems)
    {
        if (!Item.bLooted && (Item.SlotType == 0 || Item.SlotType == 4))
            ConnectionManager->SendLootItem(Item.Index);
    }
}

void UWowGameUI::OnCloseLoot()
{
    UE_LOG(LogWowGameUI, Log, TEXT("Loot window closed by user"));
    if (ConnectionManager && CurrentLootGuid != 0)
        ConnectionManager->SendLootRelease(CurrentLootGuid);
    CurrentLootGuid = 0;
    CurrentLootMoney = 0;
    CurrentLootItems.Reset();
    HideLootWindow();
}

void UWowGameUI::OnBuyItem(uint64 VendorGuid, uint32 ItemId, int32 Count, int32 VendorSlot)
{
    if (ConnectionManager)
    {
        ConnectionManager->SendBuyItem(VendorGuid, ItemId, Count, VendorSlot);
        UE_LOG(LogWowGameUI, Log, TEXT("Buying item %u (count=%d) from vendor %llu"), ItemId, Count, VendorGuid);
    }
}

void UWowGameUI::OnAcceptTrade(bool bBeginTrade)
{
    if (!ConnectionManager)
    {
        return;
    }

    if (bBeginTrade)
    {
        ConnectionManager->SendBeginTrade();
        UE_LOG(LogWowGameUI, Log, TEXT("Accepting incoming trade request"));
    }
    else
    {
        ConnectionManager->SendAcceptTrade();
        UE_LOG(LogWowGameUI, Log, TEXT("Accepting current trade contents"));
    }
}

void UWowGameUI::OnCancelTrade()
{
    if (ConnectionManager)
    {
        ConnectionManager->SendCancelTrade();
        UE_LOG(LogWowGameUI, Log, TEXT("Canceling trade"));
    }
}

void UWowGameUI::OnSellItem(uint64 VendorGuid, uint64 ItemGuid, uint8 Count)
{
    if (ConnectionManager)
    {
        ConnectionManager->SendSellItem(VendorGuid, ItemGuid, Count);
        UE_LOG(LogWowGameUI, Log, TEXT("Selling item %llu (count=%d) to vendor %llu"), ItemGuid, Count, VendorGuid);
    }
}

void UWowGameUI::OnCloseVendor()
{
    UE_LOG(LogWowGameUI, Log, TEXT("Vendor window closed by user"));
    // The vendor window is already hidden by the widget itself
}

bool UWowGameUI::OnQuestAccept(uint64 QuestGiverGuid, uint32 QuestId)
{
    if(!IsNpcInteractionInRange(QuestGiverGuid)) {HideQuestDialog();return false;}
    if (ConnectionManager && ConnectionManager->GetState() == EWowSessionState::WorldInGame)
    {
        FWowPacketHandler& PacketHandler = ConnectionManager->PacketHandler;
        if (PacketHandler.QuestLog.Num() >= 25 && !PacketHandler.QuestLog.ContainsByPredicate(
            [QuestId](const FWowQuestLogEntry& Entry) { return Entry.QuestId == QuestId; }))
        {
            UE_LOG(LogWowGameUI, Warning, TEXT("Cannot accept quest %u: quest log is full (25/25)"), QuestId);
            return false;
        }

        ConnectionManager->SendQuestAccept(QuestGiverGuid, QuestId);
        // UPDATE_OBJECT owns the quest log. A rejected accept must not leave
        // a phantom quest locally or emit QUEST_ACCEPTED before confirmation.
        ConnectionManager->SendQuestTemplateQuery(static_cast<int32>(QuestId));
        ConnectionManager->SendQuestStatusQuery(QuestGiverGuid);
        UE_LOG(LogWowGameUI, Log, TEXT("Accepting quest %u from questgiver %llu"), QuestId, QuestGiverGuid);
        return true;
    }
    return false;
}

void UWowGameUI::OnQuestSelect(uint64 QuestGiverGuid, uint32 QuestId)
{
    if(!IsNpcInteractionInRange(QuestGiverGuid)) {HideQuestDialog();HideVendorWindow();return;}
    if (ConnectionManager)
    {
        ConnectionManager->SendQuestQuery(QuestGiverGuid, QuestId);
        UE_LOG(LogWowGameUI, Log, TEXT("Selected quest %u from questgiver %llu"), QuestId, QuestGiverGuid);
    }
}

void UWowGameUI::OnQuestProgress(uint64 QuestGiverGuid, uint32 QuestId)
{
    if(!IsNpcInteractionInRange(QuestGiverGuid)) {HideQuestDialog();HideVendorWindow();return;}
    if (ConnectionManager)
    {
        ConnectionManager->SendQuestComplete(QuestGiverGuid, QuestId);
        UE_LOG(LogWowGameUI, Log, TEXT("Opening quest %u progress at questgiver %llu"), QuestId, QuestGiverGuid);
    }
}

void UWowGameUI::OnQuestContinue(uint64 QuestGiverGuid, uint32 QuestId)
{
    if(!IsNpcInteractionInRange(QuestGiverGuid)) {HideQuestDialog();HideVendorWindow();return;}
    if (ConnectionManager)
    {
        ConnectionManager->SendQuestRequestReward(QuestGiverGuid, QuestId);
        UE_LOG(LogWowGameUI, Log, TEXT("Continuing quest %u to rewards at questgiver %llu"), QuestId, QuestGiverGuid);
    }
}

void UWowGameUI::OnQuestComplete(uint64 QuestGiverGuid, uint32 QuestId, int32 RewardChoice)
{
    if(!IsNpcInteractionInRange(QuestGiverGuid)) {HideQuestDialog();HideVendorWindow();return;}
    if (ConnectionManager)
    {
        ConnectionManager->SendQuestChooseReward(QuestGiverGuid, QuestId, RewardChoice);
        ConnectionManager->SendQuestStatusQuery(QuestGiverGuid);
        UE_LOG(LogWowGameUI, Log, TEXT("Completing quest %u with reward choice %d from questgiver %llu"), QuestId, RewardChoice, QuestGiverGuid);
    }
}

void UWowGameUI::OnGossipSelect(uint64 NpcGuid, uint32 MenuId, uint32 OptionId, const FString& Code)
{
    if(!IsNpcInteractionInRange(NpcGuid)) {HideQuestDialog();HideVendorWindow();return;}
    if (ConnectionManager)
    {
        ConnectionManager->SendGossipSelectOption(NpcGuid, MenuId, OptionId, Code);
    }
}

void UWowGameUI::OnQuestDecline()
{
    UE_LOG(LogWowGameUI, Log, TEXT("Quest declined by user"));
    // No packet needs to be sent for quest decline - just close the dialog
}

void UWowGameUI::UpdateGossipGreeting(uint32 TextId, const FString& Greeting)
{
    if (QuestDialog.IsValid()) QuestDialog->UpdateGreeting(TextId, Greeting);
    FWowNpcDialogState::Get().Menu.Greeting = Greeting;
}

void UWowGameUI::PublishNpcDialog(uint8 InMode, const FWowGossipMenu* Menu, const FWowQuestDetails* Quest)
{
    using EMode = FWowNpcDialogState::EMode;
    FWowNpcDialogState& State = FWowNpcDialogState::Get();
    const EMode Mode = static_cast<EMode>(InMode);
    const EMode Previous = State.Mode;
    State.Mode = Mode;
    if (Menu) State.Menu = *Menu;
    if (Quest) State.Quest = *Quest;
    // The same code as the dialog's own buttons (SWowQuestDialog -> these handlers). Bound only when
    // a panel opens: closing must not replace an action while it runs (AcceptQuest -> HideQuestDialog).
    TWeakObjectPtr<UWowGameUI> Weak(this);
    if (Mode != EMode::None)
    {
    State.SelectQuest = [Weak](uint32 QuestId, bool bActive)
    {
        UWowGameUI* GameUI = Weak.Get(); if (!GameUI) return;
        const uint64 Npc = FWowNpcDialogState::Get().Menu.NpcGuid;
        if (bActive) GameUI->OnQuestProgress(Npc, QuestId); else GameUI->OnQuestSelect(Npc, QuestId);
    };
    State.SelectOption = [Weak](int32 OptionIndex)
    {
        UWowGameUI* GameUI = Weak.Get(); const FWowGossipMenu& Shown = FWowNpcDialogState::Get().Menu;
        if (GameUI && Shown.Options.IsValidIndex(OptionIndex))
            GameUI->OnGossipSelect(Shown.NpcGuid, Shown.MenuId, Shown.Options[OptionIndex].OptionId, FString());
    };
    State.Accept = [Weak]()
    {
        UWowGameUI* GameUI = Weak.Get(); const FWowQuestDetails& Shown = FWowNpcDialogState::Get().Quest;
        if (GameUI && GameUI->OnQuestAccept(Shown.QuestGiverGuid, Shown.QuestId)) GameUI->HideQuestDialog();
    };
    State.Decline = [Weak]() { if (UWowGameUI* GameUI = Weak.Get()) { GameUI->OnQuestDecline(); GameUI->HideQuestDialog(); } };
    State.Continue = [Weak]()
    {
        UWowGameUI* GameUI = Weak.Get(); const FWowQuestDetails& Shown = FWowNpcDialogState::Get().Quest;
        if (GameUI && Shown.bCanComplete) GameUI->OnQuestContinue(Shown.QuestGiverGuid, Shown.QuestId);
    };
    State.Complete = [Weak](int32 RewardChoice)
    {
        UWowGameUI* GameUI = Weak.Get(); const FWowQuestDetails Shown = FWowNpcDialogState::Get().Quest;
        if (!GameUI) return;
        GameUI->OnQuestComplete(Shown.QuestGiverGuid, Shown.QuestId, RewardChoice);
        GameUI->HideQuestDialog();
    };
    State.Close = [Weak]() { if (UWowGameUI* GameUI = Weak.Get()) GameUI->HideQuestDialog(); };
    State.IsOpen = [Weak]() { const UWowGameUI* GameUI = Weak.Get(); return GameUI && GameUI->IsQuestDialogVisible(); };
    }

    const TCHAR* EventName = nullptr;
    switch (Mode)
    {
    case EMode::Gossip: EventName = TEXT("GOSSIP_SHOW"); break;
    case EMode::Greeting: EventName = TEXT("QUEST_GREETING"); break;
    case EMode::Detail: EventName = TEXT("QUEST_DETAIL"); break;
    case EMode::Progress: EventName = TEXT("QUEST_PROGRESS"); break;
    case EMode::Reward: EventName = TEXT("QUEST_COMPLETE"); break;
    default: break;
    }
    UWowUIManager* UIManagerForEvents = GetWorld() && GetWorld()->GetGameInstance()
        ? GetWorld()->GetGameInstance()->GetSubsystem<UWowUIManager>() : nullptr;
    FWowEventSystem* Events = UIManagerForEvents ? UIManagerForEvents->GetEventSystem() : nullptr;
    if (!Events) return;
    // Leaving a gossip menu (closed, or on to a quest) closes it; leaving a quest panel finishes it.
    if (Previous == EMode::Gossip && Mode != EMode::Gossip) Events->FireEvent(TEXT("GOSSIP_CLOSED"), {});
    else if (Mode == EMode::None && Previous != EMode::None) Events->FireEvent(TEXT("QUEST_FINISHED"), {});
    if (EventName) Events->FireEvent(EventName, {});
}

void UWowGameUI::RemoveLootSlot(uint8 Slot)
{
    CurrentLootItems.RemoveAll([Slot](const FWowLootItem& Item) { return Item.Index == Slot; });
    if (CloseLootIfEmpty()) return;
    if (CurrentLootGuid != 0 && LootWindow.IsValid() && LootWindow->GetVisibility().IsVisible())
        LootWindow->UpdateLoot(CurrentLootGuid, CurrentLootType, CurrentLootMoney, CurrentLootItems);
}

void UWowGameUI::ClearLootMoney()
{
    CurrentLootMoney = 0;
    if (CloseLootIfEmpty()) return;
    if (CurrentLootGuid != 0 && LootWindow.IsValid() && LootWindow->GetVisibility().IsVisible())
        LootWindow->UpdateLoot(CurrentLootGuid, CurrentLootType, 0, CurrentLootItems);
}
