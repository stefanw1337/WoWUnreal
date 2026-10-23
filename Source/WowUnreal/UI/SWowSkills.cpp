#include "SWowSkills.h"
#include "WowColor.h"
#include "WowPacketHandler.h"
#include "WowEntity.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowWindowStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWowPanelScrollBar.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
void SWowSkills::Construct(const FArguments&, FWowPacketHandler* InHandler)
{
 Handler=InHandler;
 ChildSlot[SNew(SBox).WidthOverride(384).HeightOverride(512)
 [SNew(SWowWindowFrame).Padding(8)
 [SNew(SVerticalBox)
 +SVerticalBox::Slot().AutoHeight().Padding(4,2,4,12)
 [SNew(SHorizontalBox)
 +SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Center)
 [SNew(STextBlock).Text(FText::FromString(TEXT("Skills"))).Font(FWowWindowStyle::TitleFont()).ColorAndOpacity(FWowWindowStyle::Gold())]
 +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT("X")))
 .OnClicked_Lambda([this](){SetVisibility(EVisibility::Hidden);return FReply::Handled();})]]
 +SVerticalBox::Slot().FillHeight(1)[SWowPanelScrollBar::Wrap(SAssignNew(List,SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always))]
 +SVerticalBox::Slot().AutoHeight().Padding(6,10)[SNew(STextBlock).Text(FText::FromString(TEXT("Skills improve through training and use.")))
 .Font(FWowWindowStyle::TitleFont(11)).ColorAndOpacity(WowColor::FromWow(.7f, .7f, .7f))]]]];
 SetVisibility(EVisibility::Hidden);
}
void SWowSkills::Toggle(){const bool Open=!GetVisibility().IsVisible();SetVisibility(Open?EVisibility::Visible:EVisibility::Hidden);if(Open)Refresh();}
void SWowSkills::Tick(const FGeometry& G,double T,float D)
{
 SCompoundWidget::Tick(G,T,D);
 if(!GetVisibility().IsVisible() || T<NextUpdate || !Handler)return;
 NextUpdate=T+.25;
 const auto* P=Handler->EntityManager.GetLocalPlayer();if(!P)return;
 uint32 H=FDbcStore::Get().SkillLines().Revision;
 for(uint32 I=0;I<384;++I)H=HashCombine(H,GetTypeHash(P->GetField(PlayerField::BASE+0x1E8+I)));
 if(H!=Signature){Signature=H;Refresh();}
}
void SWowSkills::Refresh()
{
 List->ClearChildren();
 const auto* P=Handler?Handler->EntityManager.GetLocalPlayer():nullptr;if(!P)return;
 struct FRow{uint32 Id,Category,Rank,Max;int32 Bonus;FString Name;};TArray<FRow> Rows;
 for(uint32 I=0;I<128;++I)
 {
  const uint32 Base=PlayerField::BASE+0x1E8+I*3,Id=P->GetField(Base)&0xffff;
  if(!Id)continue;
  const auto* Info=FDbcStore::Get().SkillLines().GetLines().Find(Id);
  const uint32 Values=P->GetField(Base+1),Bonus=P->GetField(Base+2);
  Rows.Add({Id,Info?Info->Category:0,Values&0xffff,Values>>16,int16(Bonus&0xffff)+int16(Bonus>>16),Info?Info->Name:FString::Printf(TEXT("Skill %u"),Id)});
 }
 Rows.Sort([](const FRow&A,const FRow&B){return A.Category==B.Category?A.Name<B.Name:A.Category<B.Category;});
 uint32 Category=MAX_uint32;
 for(const FRow& R:Rows)
 {
  if(Category!=R.Category)
  {
   Category=R.Category;const uint32 C=Category;
   const TCHAR* Label=C==6?TEXT("Weapon Skills"):C==7?TEXT("Class Skills"):C==8?TEXT("Armor Proficiencies"):C==9?TEXT("Secondary Skills"):C==10?TEXT("Languages"):C==11?TEXT("Professions"):TEXT("Other Skills");
   List->AddSlot().Padding(0,8,0,4)[SNew(SButton).ButtonStyle(FWowWindowStyle::Button())
    .Text(FText::FromString(FString(Collapsed.Contains(C)?TEXT("+ "):TEXT("- "))+Label))
    .OnClicked_Lambda([this,C](){if(Collapsed.Contains(C))Collapsed.Remove(C);else Collapsed.Add(C);Refresh();return FReply::Handled();})];
  }
  if(Collapsed.Contains(Category))continue;
  const FString Value=R.Max>1?FString::Printf(TEXT("%u / %u"),R.Rank,R.Max):TEXT("");
  List->AddSlot().Padding(8,3)[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(R.Name)).Font(FWowWindowStyle::TitleFont(12)).ColorAndOpacity(FWowWindowStyle::Gold())]
   +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(126).HeightOverride(18)
    .Visibility(R.Max>1?EVisibility::Visible:EVisibility::Hidden)
    [SNew(SOverlay)+SOverlay::Slot()[SNew(SProgressBar).Percent(R.Max?FMath::Clamp(float(R.Rank)/R.Max,0.f,1.f):0.f).FillColorAndOpacity(WowColor::FromWow(.15f, .35f, .75f))]
     +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Value)).Font(FWowWindowStyle::TitleFont(10))
      .ToolTipText(FText::FromString(FString::Printf(TEXT("Bonus: %+d"),R.Bonus)))]]]];
 }
 if(Rows.IsEmpty())List->AddSlot()[SNew(STextBlock).Text(FText::FromString(TEXT("Waiting for character skills...")))];
}
