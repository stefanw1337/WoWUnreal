#include "SWowNpcServices.h"
#include "WowColor.h"
#include "WowConnectionManager.h"
#include "WowPacketHandler.h"
#include "WowWindowStyle.h"
#include "Formats/Dbc/DbcStore.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWowPanelScrollBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SNullWidget.h"
#include "WowEntity.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowSpellPresentation.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
namespace{FString Coins(uint32 C){return FString::Printf(TEXT("%ug %us %uc"),C/10000,C/100%100,C%100);}}
namespace
{
constexpr float TrainerScale = 600.f / 512.f; // the quest window's size for the same 384 x 512 art
FVector2D TP(float X, float Y) { return FVector2D(X, Y) * TrainerScale; }
// r335 (Stefan 14.10: "smaller fonts"): a FrameXML font's height (GameFontNormal 12, ...Small 10)
// in Slate points at the window's scale - Slate sizes are points (x 4/3 in pixels), like the fontstrings.
int32 NpcServicesFont(float WowHeight) { return FMath::Max(1, FMath::RoundToInt(WowHeight * TrainerScale * 0.75f)); }
}
void SWowNpcServices::Construct(const FArguments&,UWowConnectionManager* C)
{
 Connection=C;
 auto IsAuction=[this](){return Connection.IsValid()&&Connection->PacketHandler.AuctionOpen;};
 ChildSlot[SNew(SOverlay)
 +SOverlay::Slot()[SNew(SBox).WidthOverride(650).HeightOverride(510).Visibility_Lambda([IsAuction](){return IsAuction()?EVisibility::Visible:EVisibility::Collapsed;})
 [SNew(SWowWindowFrame).Padding(10)[SNew(SVerticalBox)
 +SVerticalBox::Slot().AutoHeight().Padding(3,3,3,10)[SNew(SHorizontalBox)
 +SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Center)[SNew(STextBlock).Font(FWowWindowStyle::TitleFont()).ColorAndOpacity(FWowWindowStyle::Gold())
 .Text_Lambda([this](){return FText::FromString(Connection.IsValid()&&Connection->PacketHandler.AuctionOpen?TEXT("Auction House"):TEXT("Trainer"));})]
 +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT("X")))
 .OnClicked_Lambda([this](){Close();return FReply::Handled();})]]
 +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(SHorizontalBox)
 .Visibility_Lambda([this](){return Connection.IsValid()&&Connection->PacketHandler.AuctionOpen?EVisibility::Visible:EVisibility::Collapsed;})
 +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(Query,SEditableTextBox).HintText(FText::FromString(TEXT("Item name")))]
 +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT("Search"))).OnClicked_Lambda([this](){Page=0;return Search();})]
 +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT("My auctions"))).OnClicked_Lambda([this](){Page=0;return Search(true);})]
 +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT("<"))).OnClicked_Lambda([this](){if(Page)Page-=50;return Search(Own);})]
 +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT(">"))).OnClicked_Lambda([this](){if(Connection.IsValid()&&Page+50<Connection->PacketHandler.AuctionTotal)Page+=50;return Search(Own);})]]
 +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(SHorizontalBox)
 .Visibility_Lambda([this](){return Connection.IsValid()&&Connection->PacketHandler.AuctionOpen?EVisibility::Visible:EVisibility::Collapsed;})
 +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(StartPrice,SEditableTextBox).HintText(FText::FromString(TEXT("Starting bid (copper)")))]
 +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(BuyoutPrice,SEditableTextBox).HintText(FText::FromString(TEXT("Buyout (copper, 0 = none)")))]
 +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FWowWindowStyle::Button())
 .Text_Lambda([this](){return FText::FromString(SellConfirmation.IsEmpty()?TEXT("Sell cursor item (12h)"):TEXT("Confirm auction"));})
 .ToolTipText(FText::FromString(TEXT("Pick up an item from your bags. The full stack is listed; the server charges the auction deposit.")))
 .OnClicked_Lambda([this](){
 if(!Connection.IsValid())return FReply::Handled();
 const FString B=StartPrice->GetText().ToString(),O=BuyoutPrice->GetText().ToString();
 const uint64 Bid=FCString::Strtoui64(*B,nullptr,10),Buyout=O.IsEmpty()?0:FCString::Strtoui64(*O,nullptr,10),Guid=Connection->GetAuctionCursorGuid();
 if(!B.IsNumeric()||(!O.IsEmpty()&&!O.IsNumeric())||!Guid||!Bid||Bid>2147483647||Buyout>2147483647||(Buyout&&Buyout<Bid))
 {Connection->PacketHandler.ServiceMessage=TEXT("Choose a bag item and valid prices in copper.");SellConfirmation.Empty();return FReply::Handled();}
 const FString Key=FString::Printf(TEXT("%llu:%llu:%llu"),Guid,Bid,Buyout);
 if(SellConfirmation==Key){Connection->SellAuctionCursor(uint32(Bid),uint32(Buyout));SellConfirmation.Empty();}
 else{SellConfirmation=Key;Connection->PacketHandler.ServiceMessage=FString::Printf(TEXT("List the cursor stack for 12 hours: bid %s, buyout %s, plus deposit. Click Confirm auction."),*Coins(uint32(Bid)),*Coins(uint32(Buyout)));}
 return FReply::Handled();})]]
 +SVerticalBox::Slot().FillHeight(1)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0,0,0,.35f)).Padding(2)
 [SWowPanelScrollBar::Wrap(SAssignNew(Rows,SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always))]]
 +SVerticalBox::Slot().AutoHeight().Padding(4,10)[SNew(STextBlock).AutoWrapText(true).Font(FWowWindowStyle::TitleFont(12))
 .Text_Lambda([this](){return FText::FromString(ConfirmId?(ConfirmPrice?FString::Printf(TEXT("Click the same purchase again to confirm %s."),*Coins(ConfirmPrice)):FString(TEXT("Click Cancel auction again to confirm cancellation."))):Connection.IsValid()?Connection->PacketHandler.ServiceMessage:FString());})]]]]
 +SOverlay::Slot()[SNew(SBox).Visibility_Lambda([IsAuction](){return IsAuction()?EVisibility::Collapsed:EVisibility::SelfHitTestInvisible;})[BuildTrainer()]]];
 SetVisibility(EVisibility::Hidden);
}
void SWowNpcServices::Open(){ConfirmId=0;Page=0;SelectedSpell=0;SetVisibility(EVisibility::Visible);Refresh();}
void SWowNpcServices::SetUIManager(UWowUIManager* InUIManager){UIManager=InUIManager;}
void SWowNpcServices::Close()
{
 SetVisibility(EVisibility::Hidden);ConfirmId=0;bFilterMenuOpen=false;
 // A Slate tooltip opened over the window stayed on screen after it closed (Stefan 10.10).
 if(FSlateApplication::IsInitialized())FSlateApplication::Get().CloseToolTip();
}
const FSlateBrush* SWowNpcServices::SpellIcon(uint32 Spell)
{
 if(const TSharedPtr<FWowRetainedSlateBrush>* Found=IconBrushes.Find(Spell))return Found->Get();
 const auto*S=FDbcStore::Get().Spells().GetById(Spell);
 const auto*I=S?FDbcStore::Get().SpellIcons().GetById(S->SpellIconID):nullptr;
 UTexture2D* Texture=I&&UIManager.IsValid()&&UIManager->GetFrameManager()?UIManager->GetFrameManager()->LoadUITexture(I->TexturePath):nullptr;
 if(!Texture)return nullptr;
 TSharedPtr<FWowRetainedSlateBrush> Brush=MakeShared<FWowRetainedSlateBrush>();
 Brush->SetResourceObject(Texture);Brush->ImageSize=FVector2D(36,36);Brush->DrawAs=ESlateBrushDrawType::Image;
 IconBrushes.Add(Spell,Brush);return Brush.Get();
}
void SWowNpcServices::Tick(const FGeometry&G,double T,float D)
{
 SCompoundWidget::Tick(G,T,D);if(!GetVisibility().IsVisible()||!Connection.IsValid()||T<NextRefresh)return;
 NextRefresh=T+.5;
 if(Connection->GetState()!=EWowSessionState::WorldInGame){Close();return;}
 {
  // Walking away closes it, as in the client (the NPC's interaction range, ~10 yards).
  auto&H=Connection->PacketHandler;
  const FWowEntity* Npc=H.EntityManager.Find(H.AuctionOpen?H.AuctionGuid:H.TrainerGuid);
  const FWowEntity* Me=H.EntityManager.GetLocalPlayer();
  if(Npc&&Me&&FVector::Dist(Npc->Movement.Position,Me->Movement.Position)>10.f){Close();return;}
 }
 if(Revision!=Connection->PacketHandler.ServiceRevision||ItemCount!=Connection->PacketHandler.ItemTemplates.Num())Refresh();
}
FReply SWowNpcServices::Search(bool InOwn){if(Connection.IsValid()){Own=InOwn;ConfirmId=0;Connection->SearchAuctions(Query->GetText().ToString(),Page,Own);}return FReply::Handled();}
FReply SWowNpcServices::Purchase(uint32 Id,uint32 Price)
{
 if(!Connection.IsValid())return FReply::Handled();
 if(ConfirmId!=Id||ConfirmPrice!=Price){ConfirmId=Id;ConfirmPrice=Price;return FReply::Handled();}
 Connection->BidAuction(Id,Price);ConfirmId=0;return FReply::Handled();
}
void SWowNpcServices::Refresh()
{
 if(!Connection.IsValid())return;auto&H=Connection->PacketHandler;Revision=H.ServiceRevision;ItemCount=H.ItemTemplates.Num();Rows->ClearChildren();
 if(H.AuctionOpen)
 {
  for(const auto&O:H.AuctionOffers)
  {
   const auto* Item=H.ItemTemplates.Find(O.Item);const FString Name=Item?Item->Name:FString::Printf(TEXT("Loading item %u..."),O.Item);
   TSharedRef<SHorizontalBox> Row=SNew(SHorizontalBox);
   Row->AddSlot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s x%u\nBid %s | Buyout %s"),*Name,O.Count,*Coins(O.Bid?O.Bid:O.Minimum),*Coins(O.Buyout))))
   .Font(FWowWindowStyle::TitleFont(12)).AutoWrapText(true).ColorAndOpacity(FWowWindowStyle::Gold())];
   if(O.Owner!=H.EntityManager.LocalPlayerGuid)
   {
    const uint32 Bid=O.Bid?O.Bid+O.Increment:O.Minimum;
    Row->AddSlot().AutoWidth()[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT("Bid"))).OnClicked_Lambda([this,Id=O.Id,Bid](){return Purchase(Id,Bid);})];
    if(O.Buyout)Row->AddSlot().AutoWidth()[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT("Buyout"))).OnClicked_Lambda([this,Id=O.Id,Price=O.Buyout](){return Purchase(Id,Price);})];
   }
   else Row->AddSlot().AutoWidth()[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT("Cancel auction")))
    .OnClicked_Lambda([this,Id=O.Id](){if(ConfirmId==Id&&ConfirmPrice==0){Connection->CancelAuction(Id);ConfirmId=0;}else{ConfirmId=Id;ConfirmPrice=0;}return FReply::Handled();})];
   Rows->AddSlot().Padding(4,7)[Row];
  }
 }
 else RefreshTrainerList();
}
// r401 (Stefan 16.10: "Rogue trainer still gave me nothing of use, I can't see what skills I will get
// when"): a server that teaches class spells by itself as you level (TheraWoW) sends a class trainer
// list with only the spells already known. Then the class's later spells (Spell.dbc /
// SkillLineAbility.dbc: the class skill lines, not passive, not talents) are listed as unavailable
// with the level they come at. Lists that already have unavailable entries are left as the server sent them.
static TMap<uint32,uint32> GWowTrainerPreviewLevels; // spell -> level, for the detail panel
static void WowBuildTrainerPreview(FWowPacketHandler& H,TArray<FWowTrainerOffer>& Out)
{
 GWowTrainerPreviewLevels.Reset();
 const FWowPlayerEntity* Me=H.EntityManager.GetLocalPlayer();
 if(!Me||H.TrainerOffers.Num()==0)return;
 for(const auto&O:H.TrainerOffers)if(O.State==1)return;
 const int32 MyLevel=Me->GetLevel();
 const uint32 ClassBit=Me->GetClassId()>0?1u<<(Me->GetClassId()-1):0u, RaceBit=Me->GetRaceId()>0?1u<<(Me->GetRaceId()-1):0u;
 if(!ClassBit)return;
 const auto& Lines=FDbcStore::Get().SkillLines().GetLines();
 auto IsClassSpell=[&](uint32 Spell)
 {
  if(const TArray<FSkillAbilityInfo>* Abilities=FDbcStore::Get().SkillLines().ForSpell(Spell))
   for(const FSkillAbilityInfo& A:*Abilities)
    if(const FSkillLineInfo* Line=Lines.Find(A.Skill); Line&&Line->Category==7
     &&(A.ClassMask&ClassBit)&&(A.RaceMask==0||(A.RaceMask&RaceBit)))return true;
  return false;
 };
 bool bClassTrainer=false;
 for(const auto&O:H.TrainerOffers)if(IsClassSpell(O.Spell)){bClassTrainer=true;break;}
 if(!bClassTrainer)return;
 static TSet<uint32> TalentSpells;
 if(TalentSpells.Num()==0)for(const auto&T:FDbcStore::Get().Talents().GetAll())for(uint32 R:T.RankID)if(R)TalentSpells.Add(R);
 TSet<uint32> Offered;for(const auto&O:H.TrainerOffers)Offered.Add(O.Spell);
 TSet<FString> Seen;
 for(const FSpellDbcEntry& S:FDbcStore::Get().Spells().GetAll())
 {
  if(S.SpellLevel<=uint32(FMath::Max(0,MyLevel))||S.SpellLevel>80||(S.Attributes&0x40)||S.SpellName.IsEmpty())continue;
  if(TalentSpells.Contains(S.ID)||Offered.Contains(S.ID)||H.KnownSpells.Contains(S.ID)||!IsClassSpell(S.ID))continue;
  bool bDuplicate=false;Seen.Add(S.SpellName+TEXT("|")+S.Rank,&bDuplicate);if(bDuplicate)continue;
  FWowTrainerOffer Preview;Preview.Spell=S.ID;Preview.State=1;Preview.Level=uint8(S.SpellLevel);
  Out.Add(Preview);GWowTrainerPreviewLevels.Add(S.ID,S.SpellLevel);
 }
}
void SWowNpcServices::RefreshTrainerDetail()
{
 if(!TrainerDetail.IsValid()||!Connection.IsValid())return;
 auto&H=Connection->PacketHandler;
 const auto*O=H.TrainerOffers.FindByPredicate([this](const auto&V){return V.Spell==SelectedSpell;});
 FWowTrainerOffer PreviewOffer; // r401: a later spell the server will teach by itself
 const uint32* PreviewLevel=O?nullptr:GWowTrainerPreviewLevels.Find(SelectedSpell);
 if(PreviewLevel){PreviewOffer.Spell=SelectedSpell;PreviewOffer.State=1;PreviewOffer.Level=uint8(*PreviewLevel);O=&PreviewOffer;}
 if(!O){TrainerDetail->SetContent(SNullWidget::NullWidget);return;}
 const auto*S=FDbcStore::Get().Spells().GetById(O->Spell);
 const auto*P=H.EntityManager.GetLocalPlayer();
 const bool bMoney=P&&P->GetCoinage()>=O->Cost, bLevel=!P||P->GetLevel()>=int32(O->Level);
 const FSlateBrush* Icon=SpellIcon(O->Spell);
 const FLinearColor Red=WowColor::FromWow(1.f,.1f,.1f), Grey=WowColor::FromWow(.5f,.5f,.5f);
 const int32 SmallFont=NpcServicesFont(10);
 // Description with its numbers filled in ($s1, $d ...), like the spell's tooltip.
 const FString Description=FWowSpellPresentation::Description(O->Spell);
 auto Line=[](const FString& Text,const FLinearColor& Color,int32 FontSize)->TSharedRef<STextBlock>
 {
  return SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(true).Font(FWowWindowStyle::TitleFont(FontSize))
   .ColorAndOpacity(Color).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black);
 };
 // As ClassTrainerDetailScrollChildFrame / TradeSkillDetail: DetailHeaderLeft/Right art (0,3), the
 // icon (8,-3, 37 x 37), name (50,-5, GameFontNormal) with the rank under it; from 5,-50 the
 // requirements (GameFontHighlightSmall, red when not met), "Cost:" (GameFontNormalSmall) with the
 // money (red when you cannot pay) and the description (GameFontHighlightSmall).
 TSharedRef<SCanvas> Header=SNew(SCanvas)
  +SCanvas::Slot().Position(TP(0,-3)).Size(TP(256,64))[ArtImage(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-DetailHeaderLeft"),0,0,1,1)]
  +SCanvas::Slot().Position(TP(256,-3)).Size(TP(64,64))[ArtImage(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-DetailHeaderRight"),0,0,1,1)]
  +SCanvas::Slot().Position(TP(8,3)).Size(TP(37,37))[SNew(SImage).Image(Icon?Icon:FCoreStyle::Get().GetBrush("NoBrush"))]
  +SCanvas::Slot().Position(TP(50,5)).Size(TP(244,16))[Line(S?S->SpellName:FString(),FWowWindowStyle::Gold(),NpcServicesFont(12))]
  +SCanvas::Slot().Position(TP(50,21)).Size(TP(244,14))[Line(S?S->Rank:FString(),Grey,SmallFont)];
 TSharedRef<SScrollBox> Scroll=SNew(SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always)
 +SScrollBox::Slot()[SNew(SBox).HeightOverride(50*TrainerScale)[Header]]
 +SScrollBox::Slot().Padding(5*TrainerScale,0,0,0)[Line(O->State==2?FString(TEXT("Already known"))
  :PreviewLevel?FString::Printf(TEXT("Learned at level %u"),O->Level):FString::Printf(TEXT("Requires: Level %u"),O->Level),
  O->State==2?Grey:bLevel?FLinearColor::White:Red,SmallFont)]
 +SScrollBox::Slot().Padding(5*TrainerScale,2*TrainerScale,0,0)[SNew(SHorizontalBox)
  +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)[Line(TEXT("Cost:"),FWowWindowStyle::Gold(),SmallFont)]
  +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MoneyText(O->Cost,FSlateColor(bMoney?FLinearColor::White:Red),SmallFont)]]
 +SScrollBox::Slot().Padding(5*TrainerScale,6*TrainerScale,0,4*TrainerScale)[Line(Description,FLinearColor::White,SmallFont)];
 TrainerDetail->SetContent(SWowPanelScrollBar::Wrap(Scroll,TrainerScale));
}

// ---------------------------------------------------------------------------------------------
// Trainer: ClassTrainerFrame on its stock art, 384 x 512 design units drawn at TrainerScale like the
// quest/gossip window. Positions follow the 3.3.5 templates the trainer shares with TradeSkillFrame
// (Blizzard_TradeSkillUI.xml: title TOP 0,-17; expand tab 15,-71; filter TOPRIGHT -25,-66; rows
// 293 x 16 from 22,-96; list scroll TOPRIGHT -67,-96; close TOPRIGHT -29,-8; buttons 224/305,-422).
// Stefan 11.10: "Training window needs to be in line with WoW WOTLK windows."
// ---------------------------------------------------------------------------------------------
const FSlateBrush* SWowNpcServices::Art(const FString& Path, float U0, float V0, float U1, float V1)
{
 const FString Key=FString::Printf(TEXT("%s|%.4f|%.4f|%.4f|%.4f"),*Path,U0,V0,U1,V1);
 if(const TSharedPtr<FWowRetainedSlateBrush>* Found=ArtBrushes.Find(Key))return Found->Get();
 UTexture2D* Texture=UIManager.IsValid()&&UIManager->GetFrameManager()?UIManager->GetFrameManager()->LoadUITexture(Path):nullptr;
 if(!Texture)return nullptr; // retried on the next paint
 TSharedPtr<FWowRetainedSlateBrush> Brush=MakeShared<FWowRetainedSlateBrush>();
 Brush->SetResourceObject(Texture);Brush->DrawAs=ESlateBrushDrawType::Image;
 Brush->ImageSize=FVector2D(Texture->GetSizeX()*(U1-U0),Texture->GetSizeY()*(V1-V0));
 Brush->SetUVRegion(FBox2f(FVector2f(U0,V0),FVector2f(U1,V1)));
 ArtBrushes.Add(Key,Brush);return Brush.Get();
}
TSharedRef<SWidget> SWowNpcServices::ArtImage(const FString& Path,float U0,float V0,float U1,float V1)
{
 return SNew(SImage).Visibility(EVisibility::HitTestInvisible)
  .Image_Lambda([this,Path,U0,V0,U1,V1](){const FSlateBrush* B=Art(Path,U0,V0,U1,V1);return B?B:FCoreStyle::Get().GetBrush("NoBrush");});
}
TSharedRef<SWidget> SWowNpcServices::MoneyText(TAttribute<uint32> Copper, TAttribute<FSlateColor> Color, int32 FontSize)
{
 // MoneyFrame: amounts with the coin icons; gold and silver only when they are not zero.
 auto Coin=[this,Copper,Color,FontSize](int32 Kind)->TSharedRef<SWidget>
 {
  const FString IconPath=Kind==0?TEXT("Interface\\MoneyFrame\\UI-GoldIcon"):Kind==1?TEXT("Interface\\MoneyFrame\\UI-SilverIcon"):TEXT("Interface\\MoneyFrame\\UI-CopperIcon");
  return SNew(SHorizontalBox)
   .Visibility_Lambda([Copper,Kind](){const uint32 C=Copper.Get();return Kind==0?(C>=10000?EVisibility::Visible:EVisibility::Collapsed):Kind==1?(C>=100?EVisibility::Visible:EVisibility::Collapsed):EVisibility::Visible;})
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Font(FWowWindowStyle::TitleFont(FontSize)).ColorAndOpacity(Color)
    .ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)
    .Text_Lambda([Copper,Kind](){const uint32 C=Copper.Get();return FText::AsNumber(Kind==0?C/10000:Kind==1?C/100%100:C%100);})]
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2,0,6,0)[SNew(SBox).WidthOverride(13*TrainerScale).HeightOverride(13*TrainerScale)
    [ArtImage(IconPath,0.f,0.f,.8f,.8f)]];
 };
 return SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[Coin(0)]+SHorizontalBox::Slot().AutoWidth()[Coin(1)]+SHorizontalBox::Slot().AutoWidth()[Coin(2)];
}
TSharedRef<SWidget> SWowNpcServices::BuildTrainer()
{
 auto PanelButtonText=[](const TCHAR* Label)->TSharedRef<STextBlock>
 {
  return SNew(STextBlock).Text(FText::FromString(Label)).Font(FWowWindowStyle::TitleFont(NpcServicesFont(12)))
   .ColorAndOpacity(FWowWindowStyle::Gold()).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black);
 };
 auto Small=[](const FString& Text,const FLinearColor& Color)->TSharedRef<STextBlock>
 {
  return SNew(STextBlock).Text(FText::FromString(Text)).Font(FWowWindowStyle::TitleFont(NpcServicesFont(10)))
   .ColorAndOpacity(Color).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black);
 };
 const FString LabelFrame(TEXT("Interface\\Glues\\CharacterCreate\\CharacterCreate-LabelFrame"));
 TSharedRef<SCanvas> Canvas=SNew(SCanvas)
 // ClassTrainerFramePortrait (60 x 60 at 7,-6), under the art: the trainer's face in the ring.
 +SCanvas::Slot().Position(TP(7,6)).Size(TP(60,60))
 [SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this](){return TrainerPortrait();})]
 // Art: UI-ClassTrainer-TopLeft/TopRight/BotLeft/BotRight.
 +SCanvas::Slot().Position(TP(0,0)).Size(TP(256,256))[ArtImage(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-TopLeft"),0,0,1,1)]
 +SCanvas::Slot().Position(TP(256,0)).Size(TP(128,256))[ArtImage(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-TopRight"),0,0,1,1)]
 +SCanvas::Slot().Position(TP(0,256)).Size(TP(256,256))[ArtImage(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-BotLeft"),0,0,1,1)]
 +SCanvas::Slot().Position(TP(256,256)).Size(TP(128,256))[ArtImage(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-BotRight"),0,0,1,1)]
 // ClassTrainerNameText: GameFontNormal, TOP 0,-17, 256 x 16.
 +SCanvas::Slot().Position(TP(64,17)).Size(TP(256,16))
 [SNew(STextBlock).Justification(ETextJustify::Center).Font(FWowWindowStyle::TitleFont(NpcServicesFont(12)))
  .ColorAndOpacity(FWowWindowStyle::Gold()).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)
  .Text_Lambda([this](){return FText::FromString(TrainerTitle());})]
 // ClassTrainerGreetingText: GameFontHighlight, TOPLEFT 76,-38, 260 x 30, left and top justified.
 +SCanvas::Slot().Position(TP(76,38)).Size(TP(260,30))
 [SNew(STextBlock).Justification(ETextJustify::Left).AutoWrapText(true).Font(FWowWindowStyle::TitleFont(NpcServicesFont(12)))
  .ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)
  .Text_Lambda([this](){return FText::FromString(Connection.IsValid()?Connection->PacketHandler.ServiceMessage:FString());})]
 // ClassTrainerExpandButtonFrame (15,-71): tab art and the "All" collapse button.
 +SCanvas::Slot().Position(TP(15,71)).Size(TP(8,32))[ArtImage(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-ExpandTab-Left"),0,0,1,1)]
 +SCanvas::Slot().Position(TP(23,65)).Size(TP(38,32))[ArtImage(TEXT("Interface\\QuestFrame\\UI-QuestLogSortTab-Middle"),0,0,1,1)]
 +SCanvas::Slot().Position(TP(61,65)).Size(TP(8,32))[ArtImage(TEXT("Interface\\QuestFrame\\UI-QuestLogSortTab-Right"),0,0,1,1)]
 +SCanvas::Slot().Position(TP(23,73)).Size(TP(40,22))
 [SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0).OnClicked_Lambda([this](){ToggleAllHeaders();return FReply::Handled();})
  [SNew(SHorizontalBox)
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3*TrainerScale,0,2*TrainerScale,0)[SNew(SBox).WidthOverride(16*TrainerScale).HeightOverride(16*TrainerScale)
    [SNew(SImage).Image_Lambda([this](){const FSlateBrush* B=Art(AllHeadersCollapsed()?TEXT("Interface\\Buttons\\UI-PlusButton-Up"):TEXT("Interface\\Buttons\\UI-MinusButton-Up"),0,0,1,1);return B?B:FCoreStyle::Get().GetBrush("NoBrush");})]]
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Small(TEXT("All"),FWowWindowStyle::Gold())]]]
 // ClassTrainerFrameFilterDropDown (UIDropDownMenuTemplate, TOPRIGHT -25,-66, width 130): "Filter".
 +SCanvas::Slot().Position(TP(204,49)).Size(TP(25,64))[ArtImage(LabelFrame,0,0,.1953125f,1)]
 +SCanvas::Slot().Position(TP(229,49)).Size(TP(105,64))[ArtImage(LabelFrame,.1953125f,0,.8046875f,1)]
 +SCanvas::Slot().Position(TP(334,49)).Size(TP(25,64))[ArtImage(LabelFrame,.8046875f,0,1,1)]
 +SCanvas::Slot().Position(TP(222,73)).Size(TP(92,16))[SNew(STextBlock).Justification(ETextJustify::Right).Text(FText::FromString(TEXT("Filter")))
  .Font(FWowWindowStyle::TitleFont(NpcServicesFont(10))).ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)]
 +SCanvas::Slot().Position(TP(204,66)).Size(TP(155,32))
 [SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0).OnClicked_Lambda([this](){bFilterMenuOpen=!bFilterMenuOpen;return FReply::Handled();})
  [SNew(SCanvas)+SCanvas::Slot().Position(TP(115,1)).Size(TP(24,24))
   [SNew(SImage).Image_Lambda([this](){const FSlateBrush* B=Art(bFilterMenuOpen?TEXT("Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Down"):TEXT("Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Up"),0,0,1,1);return B?B:FCoreStyle::Get().GetBrush("NoBrush");})]]]
 // Close button (UIPanelCloseButton, TOPRIGHT -29,-8).
 +SCanvas::Slot().Position(TP(323,8)).Size(TP(32,32))
 [SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0).OnClicked_Lambda([this](){Close();return FReply::Handled();})
  [SNew(SImage).Image_Lambda([this](){const FSlateBrush* B=Art(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"),0,0,1,1);return B?B:FCoreStyle::Get().GetBrush("NoBrush");})]]
 // Skill list: rows 293 x 16 from 22,-96; the scroll bar in the art's track at the right.
 +SCanvas::Slot().Position(TP(22,96)).Size(TP(318,184))
 [SWowPanelScrollBar::Wrap(SAssignNew(TrainerRows,SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always),TrainerScale)]
 // The bar between the list and the details (UI-ClassTrainer-HorizontalBar, 256 + 75 units).
 +SCanvas::Slot().Position(TP(15,282)).Size(TP(256,16))[ArtImage(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-HorizontalBar"),0,0,1,.25f)]
 +SCanvas::Slot().Position(TP(271,282)).Size(TP(75,16))[ArtImage(TEXT("Interface\\ClassTrainerFrame\\UI-ClassTrainer-HorizontalBar"),0,.25f,.29296875f,.5f)]
 // Details of the selected skill (ClassTrainerDetailScrollFrame).
 +SCanvas::Slot().Position(TP(20,298)).Size(TP(320,106))[SAssignNew(TrainerDetail,SBox)]
 // Your money: ClassTrainerMoneyFrame (SmallMoneyFrameTemplate, 13 high), its BOTTOMRIGHT at the
 // frame's BOTTOMLEFT +180,+86 - right-aligned, ending at x 180, bottom 426 (Stefan 14.10: placement).
 +SCanvas::Slot().Position(TP(20,411)).Size(TP(160,15))
 [SNew(SBox).HAlign(HAlign_Right).VAlign(VAlign_Center)[MoneyText(TAttribute<uint32>::CreateLambda([this](){const auto*P=Connection.IsValid()?Connection->PacketHandler.EntityManager.GetLocalPlayer():nullptr;return P?uint32(P->GetCoinage()):0u;}),
  FSlateColor(FLinearColor::White),NpcServicesFont(10))]]
 // Train and Exit (UIPanelButtonTemplate 80 x 22, centres 224,-420 and 305,-420).
 +SCanvas::Slot().Position(TP(184,409)).Size(TP(80,22))
 [SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(0)
  .IsEnabled_Lambda([this](){
   if(!Connection.IsValid()||!SelectedSpell)return false;auto&H=Connection->PacketHandler;
   const auto*O=H.TrainerOffers.FindByPredicate([this](const auto&V){return V.Spell==SelectedSpell;});
   const auto*P=H.EntityManager.GetLocalPlayer();return O&&O->State==0&&P&&P->GetCoinage()>=O->Cost;})
  .OnClicked_Lambda([this](){if(Connection.IsValid()&&SelectedSpell){Connection->BuyTrainerSpell(SelectedSpell);Connection->RequestTrainer(Connection->PacketHandler.TrainerGuid);}return FReply::Handled();})
  [PanelButtonText(TEXT("Train"))]]
 +SCanvas::Slot().Position(TP(265,409)).Size(TP(80,22))
 [SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(0)
  .OnClicked_Lambda([this](){Close();return FReply::Handled();})[PanelButtonText(TEXT("Exit"))]]
 // The filter menu (DropDownList1) under the dropdown: Available / Unavailable / Used with ticks.
 +SCanvas::Slot().Position(TP(214,92)).Size(TP(130,76))
 [SNew(SBox).Visibility_Lambda([this](){return bFilterMenuOpen?EVisibility::Visible:EVisibility::Collapsed;})
  [SNew(SWowWindowFrame).Padding(FMargin(12))[SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[FilterRow(TEXT("Available"),&bFilterAvailable)]
   +SVerticalBox::Slot().AutoHeight()[FilterRow(TEXT("Unavailable"),&bFilterUnavailable)]
   +SVerticalBox::Slot().AutoHeight()[FilterRow(TEXT("Used"),&bFilterUsed)]]]];
 return SNew(SBox).WidthOverride(384*TrainerScale).HeightOverride(512*TrainerScale)[Canvas];
}
TSharedRef<SWidget> SWowNpcServices::FilterRow(const TCHAR* Label,bool* Flag)
{
 const FString Text(Label);
 return SNew(SBox).HeightOverride(16*TrainerScale)
 [SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0)
  .OnClicked_Lambda([this,Flag](){*Flag=!*Flag;RefreshTrainerList();return FReply::Handled();})
  [SNew(SHorizontalBox)
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)[SNew(SBox).WidthOverride(16*TrainerScale).HeightOverride(16*TrainerScale)
    [SNew(SImage).Visibility_Lambda([Flag](){return *Flag?EVisibility::HitTestInvisible:EVisibility::Hidden;})
     .Image_Lambda([this](){const FSlateBrush* B=Art(TEXT("Interface\\Buttons\\UI-CheckBox-Check"),0,0,1,1);return B?B:FCoreStyle::Get().GetBrush("NoBrush");})]]
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Text))
    .Font(FWowWindowStyle::TitleFont(NpcServicesFont(10))).ColorAndOpacity(FLinearColor::White)
    .ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)]]];
}
const FSlateBrush* SWowNpcServices::TrainerPortrait()
{
 if(!Connection.IsValid()||!UIManager.IsValid()||!UIManager->GetFrameManager()||!UIManager->GetFrameManager()->UnitPortraitProvider)return FCoreStyle::Get().GetBrush("NoBrush");
 UMaterialInterface* Portrait=UIManager->GetFrameManager()->UnitPortraitProvider(Connection->PacketHandler.TrainerGuid);
 if(!Portrait)return FCoreStyle::Get().GetBrush("NoBrush");
 if(!PortraitBrush.IsValid())PortraitBrush=MakeShared<FWowRetainedSlateBrush>();
 if(PortraitBrush->GetResourceObject()!=Portrait)
 {
  PortraitBrush->SetResourceObject(Portrait);PortraitBrush->DrawAs=ESlateBrushDrawType::Image;
  PortraitBrush->ImageSize=FVector2D(60,60)*TrainerScale;
 }
 return PortraitBrush.Get();
}
FString SWowNpcServices::TrainerTitle()
{
 if(!Connection.IsValid())return TEXT("Trainer");
 auto&H=Connection->PacketHandler;
 if(const FWowEntity* Npc=H.EntityManager.Find(H.TrainerGuid))
  if(const FString* Name=H.CreatureNameCache.Find(Npc->Entry))if(!Name->IsEmpty())return *Name;
 return TEXT("Trainer");
}
FString SWowNpcServices::TrainerCategory(uint32 Spell) const
{
 // The list's headers are the spells' skill lines (Arcane, Frost, Weapon Skills ...).
 if(const TArray<FSkillAbilityInfo>* Abilities=FDbcStore::Get().SkillLines().ForSpell(Spell))
  for(const FSkillAbilityInfo& Ability:*Abilities)
   if(const FSkillLineInfo* Line=FDbcStore::Get().SkillLines().GetLines().Find(Ability.Skill))
    if(!Line->Name.IsEmpty())return Line->Name;
 return FString();
}
bool SWowNpcServices::AllHeadersCollapsed() const
{
 return HeaderNames.Num()>0&&CollapsedHeaders.Num()>=HeaderNames.Num();
}
void SWowNpcServices::ToggleAllHeaders()
{
 if(AllHeadersCollapsed())CollapsedHeaders.Reset();
 else for(const FString& Header:HeaderNames)CollapsedHeaders.Add(Header);
 RefreshTrainerList();
}
void SWowNpcServices::RefreshTrainerList()
{
 if(!TrainerRows.IsValid()||!Connection.IsValid())return;
 auto&H=Connection->PacketHandler;TrainerRows->ClearChildren();
 // The client's filter (Available and Unavailable on, Used off by default), then by level and name.
 auto Shown=[this](uint8 State){return State==0?bFilterAvailable:State==2?bFilterUsed:bFilterUnavailable;};
 TArray<FWowTrainerOffer> PreviewOffers;WowBuildTrainerPreview(H,PreviewOffers); // r401
 TArray<const FWowTrainerOffer*> Sorted;for(const auto&O:H.TrainerOffers)if(Shown(O.State))Sorted.Add(&O);
 for(const auto&O:PreviewOffers)if(Shown(O.State))Sorted.Add(&O);
 Sorted.Sort([](const FWowTrainerOffer&A,const FWowTrainerOffer&B){if(A.Level!=B.Level)return A.Level<B.Level;return A.Spell<B.Spell;});
 // Group under skill-line headers; a list where every group would hold one skill stays flat
 // (a weapon master's skills are each their own skill line).
 TMap<FString,TArray<const FWowTrainerOffer*>> Groups;TArray<FString> Order;
 for(const FWowTrainerOffer* O:Sorted){const FString Cat=TrainerCategory(O->Spell);if(!Groups.Contains(Cat))Order.Add(Cat);Groups.FindOrAdd(Cat).Add(O);}
 bool bHeaders=Order.Num()>1;
 if(bHeaders){bool bAllSingle=true;for(const auto&Pair:Groups)if(Pair.Value.Num()>1)bAllSingle=false;if(bAllSingle)bHeaders=false;}
 HeaderNames=bHeaders?Order:TArray<FString>();
 if(!SelectedSpell||!Sorted.ContainsByPredicate([this](const FWowTrainerOffer* V){return V->Spell==SelectedSpell;}))
  SelectedSpell=Sorted.Num()?Sorted[0]->Spell:0;
 const int32 RowFont=NpcServicesFont(12);
 auto AddSkill=[&](const FWowTrainerOffer& O)
 {
  const auto*S=FDbcStore::Get().Spells().GetById(O.Spell);
  const FString Name=S?S->SpellName:FString::Printf(TEXT("Spell %u"),O.Spell);
  const FString Rank=S&&!S->Rank.IsEmpty()?FString::Printf(TEXT(" (%s)"),*S->Rank):FString();
  // ClassTrainer colours: available green, unavailable red, used grey.
  const FLinearColor Color=O.State==0?WowColor::FromWow(.25f,.75f,.25f):O.State==2?WowColor::FromWow(.5f,.5f,.5f):WowColor::FromWow(.9f,0.f,0.f);
  const uint32 Id=O.Spell;
  TrainerRows->AddSlot()[SNew(SBox).HeightOverride(16*TrainerScale)
  [SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0)
   .OnClicked_Lambda([this,Id](){SelectedSpell=Id;RefreshTrainerDetail();return FReply::Handled();})
   [SNew(SOverlay)
    // ClassTrainerSkillHighlight: UI-Listbox-Highlight2 in the row's colour.
    +SOverlay::Slot()[SNew(SImage).ColorAndOpacity(FSlateColor(FLinearColor(Color.R,Color.G,Color.B,1.f)))
     .Visibility_Lambda([this,Id](){return SelectedSpell==Id?EVisibility::HitTestInvisible:EVisibility::Hidden;})
     .Image_Lambda([this](){const FSlateBrush* B=Art(TEXT("Interface\\Buttons\\UI-Listbox-Highlight2"),0,0,1,1);return B?B:FCoreStyle::Get().GetBrush("NoBrush");})]
    +SOverlay::Slot().Padding(FMargin(20*TrainerScale,0,0,0)).VAlign(VAlign_Center)
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(Name)).Font(FWowWindowStyle::TitleFont(RowFont))
      .ColorAndOpacity(Color).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)]
     +SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(Rank)).Font(FWowWindowStyle::TitleFont(RowFont))
      .ColorAndOpacity(WowColor::FromWow(.5f,.5f,.5f)).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)]]]]];
 };
 for(const FString& Cat:Order)
 {
  if(bHeaders)
  {
   const FString Header=Cat.IsEmpty()?FString(TEXT("Other")):Cat;
   const bool bCollapsed=CollapsedHeaders.Contains(Cat);
   TrainerRows->AddSlot()[SNew(SBox).HeightOverride(16*TrainerScale)
   [SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0)
    .OnClicked_Lambda([this,Cat](){if(CollapsedHeaders.Contains(Cat))CollapsedHeaders.Remove(Cat);else CollapsedHeaders.Add(Cat);RefreshTrainerList();return FReply::Handled();})
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3*TrainerScale,0,1*TrainerScale,0)[SNew(SBox).WidthOverride(16*TrainerScale).HeightOverride(16*TrainerScale)
      [SNew(SImage).Image_Lambda([this,bCollapsed](){const FSlateBrush* B=Art(bCollapsed?TEXT("Interface\\Buttons\\UI-PlusButton-Up"):TEXT("Interface\\Buttons\\UI-MinusButton-Up"),0,0,1,1);return B?B:FCoreStyle::Get().GetBrush("NoBrush");})]]
     +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Header)).Font(FWowWindowStyle::TitleFont(RowFont))
      .ColorAndOpacity(FWowWindowStyle::Gold()).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)]]]];
   if(bCollapsed)continue;
  }
  for(const FWowTrainerOffer* O:Groups[Cat])AddSkill(*O);
 }
 RefreshTrainerDetail();
}
