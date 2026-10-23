#include "SWowWorldMap.h"
#include "WowColor.h"
#include "Mpq/MpqManager.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowEntityManager.h"
#include "WowEntity.h"
#include "WowNetwork/Public/WowPacketHandler.h"
#include "WowWindowStyle.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowSatelliteTiles.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SCanvas.h"
#include "Styling/CoreStyle.h"

// r255 (Stefan 12.10: "show WHERE I died so I can walk back - a gravestone on the world map and minimap"):
// a small grey headstone with a dark cross, drawn as triangles (outline first).
static void PaintWorldMapGravestone(FSlateWindowElementList& Elements, int32 Layer, const FSlateRenderTransform& RT,
    const FVector2D& C, float H)
{
    auto Tri = [&](const FVector2D& A, const FVector2D& B, const FVector2D& D, const FLinearColor& Color, int32 L)
    {
        const FColor Col = Color.ToFColor(true);
        TArray<FSlateVertex> V = {FSlateVertex::Make(RT, FVector2f(A), FVector2f::ZeroVector, Col),
            FSlateVertex::Make(RT, FVector2f(B), FVector2f::ZeroVector, Col), FSlateVertex::Make(RT, FVector2f(D), FVector2f::ZeroVector, Col)};
        TArray<SlateIndex> I = {0, 1, 2};
        FSlateDrawElement::MakeCustomVerts(Elements, L, FSlateResourceHandle(), V, I, nullptr, 0, 0, ESlateDrawEffect::None);
    };
    auto Stone = [&](float Grow, const FLinearColor& Color, int32 L)
    {
        const float W = H * .36f + Grow, Bottom = C.Y + H * .5f + Grow, Shoulder = C.Y - H * .14f;
        const FVector2D BL(C.X - W, Bottom), BR(C.X + W, Bottom), SL(C.X - W, Shoulder), SR(C.X + W, Shoulder);
        Tri(BL, BR, SR, Color, L); Tri(BL, SR, SL, Color, L);
        const FVector2D Arc(C.X, Shoulder);
        constexpr int32 Steps = 8;
        for (int32 S = 0; S < Steps; ++S)
        {
            const float A0 = PI * S / Steps, A1 = PI * (S + 1) / Steps;
            Tri(Arc, Arc + FVector2D(-FMath::Cos(A0) * W, -FMath::Sin(A0) * W), Arc + FVector2D(-FMath::Cos(A1) * W, -FMath::Sin(A1) * W), Color, L);
        }
    };
    Stone(1.6f, FLinearColor(0.f, 0.f, 0.f, .9f), Layer);
    Stone(0.f, FLinearColor(.62f, .62f, .6f, 1.f), Layer + 1);
    const FLinearColor Cross(.18f, .18f, .18f, 1.f);
    const float T = FMath::Max(1.f, H * .08f);
    const FVector2D V0(C.X - T, C.Y - H * .3f), V1(C.X + T, C.Y + H * .25f);
    Tri(V0, FVector2D(V1.X, V0.Y), V1, Cross, Layer + 2); Tri(V0, V1, FVector2D(V0.X, V1.Y), Cross, Layer + 2);
    const FVector2D H0(C.X - H * .2f, C.Y - H * .13f - T), H1(C.X + H * .2f, C.Y - H * .13f + T);
    Tri(H0, FVector2D(H1.X, H0.Y), H1, Cross, Layer + 2); Tri(H0, H1, FVector2D(H0.X, H1.Y), Cross, Layer + 2);
}



namespace
{
// Map surface: draws through a callback and forwards mouse input to the map.
class SWowMapSurface:public SLeafWidget
{
public:
 SLATE_BEGIN_ARGS(SWowMapSurface){} SLATE_END_ARGS()
 TFunction<int32(const FGeometry&,FSlateWindowElementList&,int32)> Draw;
 TFunction<void(FVector2D,FVector2D,float)> Wheel;
 TFunction<void(FVector2D,FVector2D)> Click;
 TFunction<void(FVector2D,FVector2D)> Drag;
 TFunction<void(FVector2D,FVector2D)> Hover;
 TFunction<void()> RightClick;
 TFunction<void()> Leave;
 void Construct(const FArguments&){}
 virtual void OnMouseLeave(const FPointerEvent& E)override{SLeafWidget::OnMouseLeave(E);if(Leave)Leave();}
 virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(802,535);}
 virtual int32 OnPaint(const FPaintArgs&,const FGeometry&G,const FSlateRect&,FSlateWindowElementList&E,int32 L,const FWidgetStyle&,bool)const override
 {E.PushClip(FSlateClippingZone(G));const int32 R=Draw?Draw(G,E,L):L;E.PopClip();return R;}
 virtual FReply OnMouseWheel(const FGeometry& G,const FPointerEvent& E)override
 {if(Wheel)Wheel(G.AbsoluteToLocal(E.GetScreenSpacePosition()),G.GetLocalSize(),E.GetWheelDelta());return FReply::Handled();}
 virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)override
 {
  if(E.GetEffectingButton()==EKeys::LeftMouseButton)
  {bLeftDown=true;bDragged=false;Last=Press=G.AbsoluteToLocal(E.GetScreenSpacePosition());return FReply::Handled().CaptureMouse(SharedThis(this));}
  if(E.GetEffectingButton()==EKeys::RightMouseButton)return FReply::Handled();
  return FReply::Unhandled();
 }
 virtual FReply OnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)override
 {
  if(E.GetEffectingButton()==EKeys::RightMouseButton){if(RightClick)RightClick();return FReply::Handled();}
  if(E.GetEffectingButton()!=EKeys::LeftMouseButton||!bLeftDown)return FReply::Unhandled();
  bLeftDown=false;
  if(!bDragged&&Click)Click(G.AbsoluteToLocal(E.GetScreenSpacePosition()),G.GetLocalSize());
  return FReply::Handled().ReleaseMouseCapture();
 }
 virtual FReply OnMouseMove(const FGeometry& G,const FPointerEvent& E)override
 {
  const FVector2D Local=G.AbsoluteToLocal(E.GetScreenSpacePosition());
  if(bLeftDown&&HasMouseCapture())
  {
   if(!bDragged&&FVector2D::Distance(Local,Press)>4.f)bDragged=true;
   if(bDragged&&Drag)Drag(Local-Last,G.GetLocalSize());
   Last=Local;return FReply::Handled();
  }
  if(Hover)Hover(Local,G.GetLocalSize());
  return FReply::Unhandled();
 }
 virtual void OnMouseCaptureLost(const FCaptureLostEvent&)override{bLeftDown=false;}
 virtual FCursorReply OnCursorQuery(const FGeometry&,const FPointerEvent&)const override
 // r345 (Stefan 14.10: "this mouse pointer for dragging the map is a no"): dragging keeps the normal
 // WoW gauntlet. GrabHandClosed has no WoW cursor (WowCursorManager maps Default/Hand/TextEditBeam),
 // so the system's white hand showed.
 {return FCursorReply::Cursor(EMouseCursor::Default);}
private:
 bool bLeftDown=false,bDragged=false;FVector2D Press=FVector2D::ZeroVector,Last=FVector2D::ZeroVector;
};

constexpr uint32 MapEasternKingdoms=0,MapKalimdor=1,MapOutland=530,MapNorthrend=571;

// Convex hull (Andrew's monotone chain) of a POI's points, counter-clockwise.
TArray<FVector2D> PoiHull(TArray<FVector2D> P)
{
 P.Sort([](const FVector2D& A,const FVector2D& B){return A.X<B.X||(A.X==B.X&&A.Y<B.Y);});
 if(P.Num()<3)return P;
 auto Cross=[](const FVector2D& O,const FVector2D& A,const FVector2D& B){return (A.X-O.X)*(B.Y-O.Y)-(A.Y-O.Y)*(B.X-O.X);};
 TArray<FVector2D> H;H.SetNum(P.Num()*2);int32 K=0;
 for(int32 I=0;I<P.Num();++I){while(K>=2&&Cross(H[K-2],H[K-1],P[I])<=0)--K;H[K++]=P[I];}
 for(int32 I=P.Num()-2,T=K+1;I>=0;--I){while(K>=T&&Cross(H[K-2],H[K-1],P[I])<=0)--K;H[K++]=P[I];}
 H.SetNum(FMath::Max(0,K-1));return H;
}
FVector2D PoiCentroid(const TArray<FVector2D>& P)
{
 FVector2D Sum=FVector2D::ZeroVector;for(const FVector2D& V:P)Sum+=V;
 return P.Num()?Sum/P.Num():Sum;
}
constexpr float PoiButtonSize=32.f; // QuestPOITemplate
// r336: the map position under the mouse (0..1 of the shown map; negative = not over the map).
FVector2D GWowMapCursor(-1.0,-1.0);
// r371 (Stefan 16.10: "a mouse over to tell us the objective, for the button and the blue areas"): the
// quest under the mouse (its numbered POI button, or one of the selected quest's objective areas) and
// where the mouse is; the map shows WorldMapTooltip-style lines for it.
uint32 GWowMapTipQuest=0;
FVector2D GWowMapTipPos(0.0,0.0);
// The quest's title and its objectives ("Duskbat Wing: 2/6"; finished ones flagged), or its
// completion line for a quest that is ready to turn in.
bool WowMapQuestTip(const FWowPacketHandler* Quests,uint32 QuestId,FString& OutTitle,TArray<TPair<FString,bool>>& OutLines)
{
 OutLines.Reset();
 if(!Quests||!QuestId)return false;
 const FWowQuestLogEntry* Quest=Quests->QuestLog.FindByPredicate([QuestId](const FWowQuestLogEntry& Q){return Q.QuestId==QuestId;});
 if(!Quest)return false;
 OutTitle=Quest->Title.IsEmpty()?FString::Printf(TEXT("Quest %u"),QuestId):Quest->Title;
 if(Quest->State==1)
 {
  OutLines.Add(TPair<FString,bool>(Quest->CompletionText.IsEmpty()?FString(TEXT("Ready to turn in")):Quest->CompletionText,false));
  return true;
 }
 for(const FWowQuestObjective& Objective:Quest->Objectives)
 {
  if(!Objective.Required&&Objective.Text.IsEmpty())continue;
  FString Name=Objective.Text;
  if(Name.IsEmpty()&&Objective.ItemId)
   if(const FWowItemTemplateInfo* Item=Quests->ItemTemplates.Find(Objective.ItemId))Name=Item->Name;
  if(Name.IsEmpty()&&Objective.CreatureOrGOId&&!(Objective.CreatureOrGOId&0x80000000u))
   if(const FString* Creature=Quests->CreatureNameCache.Find(Objective.CreatureOrGOId))Name=*Creature+TEXT(" slain");
  if(Name.IsEmpty())continue;
  const uint32 Count=FMath::Min(Objective.Count,Objective.Required);
  OutLines.Add(TPair<FString,bool>(Objective.Required>0?FString::Printf(TEXT("%s: %u/%u"),*Name,Count,Objective.Required):Name,
   Objective.Required>0&&Objective.Count>=Objective.Required));
 }
 if(OutLines.IsEmpty()&&!Quest->ObjectiveText.IsEmpty())OutLines.Add(TPair<FString,bool>(Quest->ObjectiveText,false));
 return true;
}
// One objective line of that tooltip (empty lines collapse).
TSharedRef<SWidget> WowMapQuestTipLine(TFunction<const FWowPacketHandler*()> GetQuests,int32 Index)
{
 return SNew(STextBlock).Font(FWowWindowStyle::TooltipFont(false))
  .Visibility_Lambda([GetQuests,Index](){FString T;TArray<TPair<FString,bool>> Lines;WowMapQuestTip(GetQuests(),GWowMapTipQuest,T,Lines);
   return Lines.IsValidIndex(Index)?EVisibility::Visible:EVisibility::Collapsed;})
  .ColorAndOpacity_Lambda([GetQuests,Index](){FString T;TArray<TPair<FString,bool>> Lines;WowMapQuestTip(GetQuests(),GWowMapTipQuest,T,Lines);
   return FSlateColor(Lines.IsValidIndex(Index)&&Lines[Index].Value?WowColor::FromWow(.5f,.5f,.5f):FLinearColor::White);})
  .Text_Lambda([GetQuests,Index](){FString T;TArray<TPair<FString,bool>> Lines;WowMapQuestTip(GetQuests(),GWowMapTipQuest,T,Lines);
   return FText::FromString(Lines.IsValidIndex(Index)?Lines[Index].Key:FString());});
}
bool WowPointInConvex(const TArray<FVector2D>& Pts,FVector2D P)
{
 if(Pts.Num()<3)return false;
 int32 Sign=0;
 for(int32 I=0;I<Pts.Num();++I)
 {
  const FVector2D A=Pts[I],B=Pts[(I+1)%Pts.Num()];
  const double Cross=(B.X-A.X)*(P.Y-A.Y)-(B.Y-A.Y)*(P.X-A.X);
  const int32 S=Cross>0?1:(Cross<0?-1:0);
  if(S==0)continue;
  if(Sign==0)Sign=S;else if(S!=Sign)return false;
 }
 return true;
}
}

void SWowWorldMap::Construct(const FArguments&,UWowUIManager* InUI,FMpqManager* InMpq,FWowEntityManager* EM,TFunction<uint32()> GetZone)
{
 UI=InUI;Entities=EM;Zone=MoveTemp(GetZone);Mpq=InMpq;
 TArray<uint8> Bytes;
 if(Mpq)
 {
  FDbcParser P;if(Mpq->ReadFile(TEXT("DBFilesClient\\WorldMapArea.dbc"),Bytes)&&P.Parse(Bytes))Data.LoadAreas(P);
  if(Mpq->ReadFile(TEXT("DBFilesClient\\WorldMapOverlay.dbc"),Bytes)&&P.Parse(Bytes))Data.LoadOverlays(P);
  if(Mpq->ReadFile(TEXT("DBFilesClient\\DungeonMap.dbc"),Bytes)&&P.Parse(Bytes))Data.LoadFloors(P);
  if(Mpq->ReadFile(TEXT("Textures\\Minimap\\md5translate.trs"),Bytes)){FString Text;FFileHelper::BufferToString(Text,Bytes.GetData(),Bytes.Num());Data.LoadSatellite(Text);}
  AddInstances(*Mpq);
 }
 for(int32 I=0;I<Data.Areas.Num();++I)if(HasMapArt(I))Indices.Add(I);
 Indices.Sort([this](int32 A,int32 B){return ViewName(A)<ViewName(B);});
 for(int32 I:Indices)Options.Add(MakeShared<FString>(ViewName(I)));
 TSharedPtr<SWowMapSurface> Surface;
 TSharedRef<SHorizontalBox> MapControls=SNew(SHorizontalBox)
 +SHorizontalBox::Slot().FillWidth(1)[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Options)
 .OnGenerateWidget_Lambda([](TSharedPtr<FString> S){return SNew(STextBlock).Text(FText::FromString(*S));})
 .OnSelectionChanged_Lambda([this](TSharedPtr<FString>S,ESelectInfo::Type){const int32 I=Options.IndexOfByKey(S);if(Indices.IsValidIndex(I))Select(Indices[I]);})
 [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(ViewName(Selected));})]]
 // Floor selector (dungeons/raids with several levels).
 +SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0)[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT("Up")))
 .ToolTipText(FText::FromString(TEXT("Previous floor")))
 .Visibility_Lambda([this](){return FloorCount()>1?EVisibility::Visible:EVisibility::Collapsed;})
 .IsEnabled_Lambda([this](){return FloorSlot()>0;})
 .OnClicked_Lambda([this](){StepFloor(-1);return FReply::Handled();})]
 +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6,0)[SNew(STextBlock).Font(FWowWindowStyle::TitleFont(12)).ColorAndOpacity(FWowWindowStyle::Gold())
 .Visibility_Lambda([this](){return FloorCount()>1?EVisibility::Visible:EVisibility::Collapsed;})
 .Text_Lambda([this](){return FText::FromString(FString::Printf(TEXT("Floor %d/%d"),FloorSlot()+1,FloorCount()));})]
 +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT("Down")))
 .ToolTipText(FText::FromString(TEXT("Next floor")))
 .Visibility_Lambda([this](){return FloorCount()>1?EVisibility::Visible:EVisibility::Collapsed;})
 .IsEnabled_Lambda([this](){return FloorSlot()+1<FloorCount();})
 .OnClicked_Lambda([this](){StepFloor(1);return FReply::Handled();})]
 // Route / pack overlay toggle (Keystone.guru data is connected in a later round).
 +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(SButton).ButtonStyle(FWowWindowStyle::Button())
 .ToolTipText(FText::FromString(TEXT("Show routes, packs and markers on this map")))
 .Visibility_Lambda([this](){return Data.Areas.IsValidIndex(Selected)&&Data.Areas[Selected].bDungeon?EVisibility::Visible:EVisibility::Collapsed;})
 .Text_Lambda([this](){return FText::FromString(bRouteOverlay?TEXT("Routes: On"):TEXT("Routes: Off"));})
 .OnClicked_Lambda([this](){bRouteOverlay=!bRouteOverlay;return FReply::Handled();})]
 +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT("Zoom Out")))
 .IsEnabled_Lambda([this](){return ParentOf(Selected)!=INDEX_NONE||ViewZoom>1.001f;})
 .OnClicked_Lambda([this](){GoUp();return FReply::Handled();})]
 +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).Text(FText::FromString(TEXT("Current zone")))
 .OnClicked_Lambda([this](){Current();return FReply::Handled();})]
 // Map style button: shows the style on screen, like "Routes: On" (Stefan 11.10: it named the next
 // style, so on Satellite v2 it read "Classic map"); the tooltip says what a click switches to.
 // Satellite v2 = our own top-down renders recorded while playing (r295), Blizzard's satellite
 // tiles where none is recorded yet.
 +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(SButton).ButtonStyle(FWowWindowStyle::Button())
 .ToolTipText_Lambda([this](){return FText::FromString(!Satellite?TEXT("Click for Blizzard's satellite map")
  :!bSatelliteV2?TEXT("Click for Satellite v2: our own renders of the places you have been, Blizzard's satellite elsewhere")
  :TEXT("Click for the classic drawn map"));})
 .Text_Lambda([this](){return FText::FromString(!Satellite?TEXT("Map: Classic"):!bSatelliteV2?TEXT("Map: Satellite"):TEXT("Map: Satellite v2"));})
 .IsEnabled_Lambda([this](){return !(Data.Areas.IsValidIndex(Selected)&&Data.Areas[Selected].bDungeon)||(HasSatellite(Selected)&&Data.FloorsFor(Data.Areas[Selected]).Num()>0);})
 .OnClicked_Lambda([this]()
 {
  if(!Satellite){Satellite=true;bSatelliteV2=false;}
  else if(!bSatelliteV2)bSatelliteV2=true;
  else{Satellite=false;bSatelliteV2=false;}
  Select(Selected,CurrentFloor);return FReply::Handled();
 })]
;
 TSharedRef<SOverlay> MapArea=SNew(SOverlay)
  +SOverlay::Slot()[SAssignNew(Surface,SWowMapSurface)]
  // Stefan 11.10: the flight point under the mouse gets the normal GameTooltip, as TaxiFrame's
  // TaxiNodeOnButtonEnter builds it: the name, then the price as coins (SetTooltipMoney), "You are
  // here" in green, or that no known route leads there.
  +SOverlay::Slot()[SNew(SConstraintCanvas).Visibility(EVisibility::HitTestInvisible)
   +SConstraintCanvas::Slot().AutoSize(true).Alignment(FVector2D(0,0))
   .Offset_Lambda([this](){return FMargin(TaxiTooltipAnchor().X,TaxiTooltipAnchor().Y,0,0);})
   [SNew(SBox).Visibility_Lambda([this](){return bTaxiMode&&TaxiPoints.IsValidIndex(HoverTaxi)?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
    [SNew(SWowTooltipFrame)[SNew(SVerticalBox)
     +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(FWowWindowStyle::TooltipFont(true)).ColorAndOpacity(FLinearColor::White)
      .Text_Lambda([this](){return FText::FromString(TaxiPoints.IsValidIndex(HoverTaxi)?TaxiPoints[HoverTaxi].Name:FString());})]
     +SVerticalBox::Slot().AutoHeight().Padding(0,2,0,0)[SNew(STextBlock).Font(FWowWindowStyle::TooltipFont(false))
      .Visibility_Lambda([this](){return TaxiPoints.IsValidIndex(HoverTaxi)&&(TaxiPoints[HoverTaxi].bCurrent||!TaxiPoints[HoverTaxi].bReachable)?EVisibility::Visible:EVisibility::Collapsed;})
      .ColorAndOpacity_Lambda([this](){return FSlateColor(TaxiPoints.IsValidIndex(HoverTaxi)&&TaxiPoints[HoverTaxi].bCurrent?WowColor::FromWow(.5f,1.f,.5f):WowColor::FromWow(.5f,.5f,.5f));})
      .Text_Lambda([this](){return FText::FromString(TaxiPoints.IsValidIndex(HoverTaxi)&&TaxiPoints[HoverTaxi].bCurrent?TEXT("You are here"):TEXT("No known route"));})]
     +SVerticalBox::Slot().AutoHeight().Padding(0,2,0,0)[SNew(SHorizontalBox)
      .Visibility_Lambda([this](){return TaxiPoints.IsValidIndex(HoverTaxi)&&!TaxiPoints[HoverTaxi].bCurrent&&TaxiPoints[HoverTaxi].bReachable?EVisibility::Visible:EVisibility::Collapsed;})
      +SHorizontalBox::Slot().AutoWidth()[TaxiCoin(0)]+SHorizontalBox::Slot().AutoWidth()[TaxiCoin(1)]+SHorizontalBox::Slot().AutoWidth()[TaxiCoin(2)]]]]]]
  // r371: the hovered quest's objectives (WorldMapQuestPOI_OnEnter / the blob tooltip): title in gold,
  // open objectives white, finished ones grey.
  +SOverlay::Slot()[SNew(SConstraintCanvas).Visibility(EVisibility::HitTestInvisible)
   +SConstraintCanvas::Slot().AutoSize(true).Alignment(FVector2D(0,0))
   .Offset_Lambda([](){return FMargin(GWowMapTipPos.X+16.,GWowMapTipPos.Y-12.,0,0);})
   [SNew(SBox).Visibility_Lambda([this](){FString T;TArray<TPair<FString,bool>> Lines;
     return !bTaxiMode&&WowMapQuestTip(Quests,GWowMapTipQuest,T,Lines)?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
    [SNew(SWowTooltipFrame)[SNew(SVerticalBox)
     +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(FWowWindowStyle::TooltipFont(true)).ColorAndOpacity(FWowWindowStyle::Gold())
      .Text_Lambda([this](){FString T;TArray<TPair<FString,bool>> Lines;WowMapQuestTip(Quests,GWowMapTipQuest,T,Lines);return FText::FromString(T);})]
     +SVerticalBox::Slot().AutoHeight().Padding(0,2,0,0)[WowMapQuestTipLine([this](){return static_cast<const FWowPacketHandler*>(Quests);},0)]
     +SVerticalBox::Slot().AutoHeight()[WowMapQuestTipLine([this](){return static_cast<const FWowPacketHandler*>(Quests);},1)]
     +SVerticalBox::Slot().AutoHeight()[WowMapQuestTipLine([this](){return static_cast<const FWowPacketHandler*>(Quests);},2)]
     +SVerticalBox::Slot().AutoHeight()[WowMapQuestTipLine([this](){return static_cast<const FWowPacketHandler*>(Quests);},3)]
     +SVerticalBox::Slot().AutoHeight()[WowMapQuestTipLine([this](){return static_cast<const FWowPacketHandler*>(Quests);},4)]
     +SVerticalBox::Slot().AutoHeight()[WowMapQuestTipLine([this](){return static_cast<const FWowPacketHandler*>(Quests);},5)]]]]];
 TSharedRef<STextBlock> MapStatus=SNew(STextBlock).Font(FWowWindowStyle::TitleFont(11)).ColorAndOpacity(FWowWindowStyle::Gold())
 .Text_Lambda([this]()
 {
  if(NextTile<Tiles.Num())return FText::FromString(TEXT("Loading map..."));
  if(bTaxiMode)
  {
   if(TaxiPoints.IsValidIndex(HoverTaxi))
   {
    const FTaxiPoint& Point=TaxiPoints[HoverTaxi];
    if(Point.bCurrent)return FText::FromString(Point.Name+TEXT("   (you are here)"));
    if(!Point.bReachable)return FText::FromString(Point.Name+TEXT("   (no known route)"));
    const uint32 Gold=Point.Cost/10000,Silver=(Point.Cost/100)%100,Copper=Point.Cost%100;
    FString Price;
    if(Gold)Price+=FString::Printf(TEXT("%ug "),Gold);
    if(Gold||Silver)Price+=FString::Printf(TEXT("%us "),Silver);
    Price+=FString::Printf(TEXT("%uc"),Copper);
    return FText::FromString(FString::Printf(TEXT("%s   %s   (click to fly)"),*Point.Name,*Price));
   }
   return FText::FromString(TEXT("Flight Master: choose a destination. Hover a flight point for its route and price."));
  }
  if(Tiles.IsEmpty())return FText::FromString(TEXT("No map artwork available for this area."));
  if(bRouteOverlay&&Data.Areas.IsValidIndex(Selected)&&Data.Areas[Selected].bDungeon)
   return FText::FromString(TEXT("Routes: no route data connected yet (Keystone.guru overlay comes next)."));
  if(HoverTarget!=INDEX_NONE)return FText::FromString(ViewName(HoverTarget)+TEXT("   (click to open)"));
  return FText::FromString(TEXT("Wheel: zoom   Drag: move   Left-click: open area   Right-click: zoom out"));
 });
 // r336 (Stefan 14.10: "coordinates for player and mouse on the world map"): zone coordinates
 // (0-100, as map addons and /way show them) of you and of the point under the mouse.
 TSharedRef<STextBlock> MapCoords=SNew(STextBlock).Font(FWowWindowStyle::TitleFont(11)).ColorAndOpacity(FLinearColor::White)
  .ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)
  .Text_Lambda([this]()
  {
   FString Text;FVector2D Pos;float Facing=0.f;
   if(!bTaxiMode&&PlayerOnView(Selected,Pos,Facing))Text=FString::Printf(TEXT("Player: %.1f, %.1f"),Pos.X*100.0,Pos.Y*100.0);
   if(GWowMapCursor.X>=0.0&&GWowMapCursor.X<=1.0&&GWowMapCursor.Y>=0.0&&GWowMapCursor.Y<=1.0)
   {
    if(!Text.IsEmpty())Text+=TEXT("      ");
    Text+=FString::Printf(TEXT("Cursor: %.1f, %.1f"),GWowMapCursor.X*100.0,GWowMapCursor.Y*100.0);
   }
   return FText::FromString(Text);
  });
 // r336 (Stefan 14.10: "make the world map window frame like the WotLK frame"): WorldMapFrame's
 // full-size art - UI-WorldMap-Top/Middle/Bottom 1-4, 4 x 3 tiles of 256 on its 1024 x 768
 // positioning guide - with the title (CENTER 0,+372), the map where WorldMapDetailFrame sits
 // (1002 x 668 at 10,69), the close button at the top right (+4,+4), the controls where the
 // dropdowns are (y -35) and the status and coordinates in the bottom strip.
 constexpr float MapFrameScale=830.f/1024.f;
 auto MP=[](float X,float Y){return FVector2D(X,Y)*MapFrameScale;};
 TSharedRef<SCanvas> MapFrame=SNew(SCanvas);
 static const TCHAR* const ArtRows[3]={TEXT("Top"),TEXT("Middle"),TEXT("Bottom")};
 for(int32 ArtRow=0;ArtRow<3;++ArtRow)for(int32 ArtCol=0;ArtCol<4;++ArtCol)
 {
  const FString ArtPath=FString::Printf(TEXT("Interface\\WorldMap\\UI-WorldMap-%s%d"),ArtRows[ArtRow],ArtCol+1);
  MapFrame->AddSlot().Position(MP(ArtCol*256.f,ArtRow*256.f)).Size(MP(256.f,256.f))
  [SNew(SImage).Visibility(EVisibility::HitTestInvisible).Image_Lambda([this,ArtPath]()
   {const FSlateBrush* B=PoiBrush(*ArtPath,FBox2f(FVector2f(0.f,0.f),FVector2f(1.f,1.f)));return B?B:FCoreStyle::Get().GetBrush("NoBrush");})];
 }
 MapFrame->AddSlot().Position(MP(10.f,69.f)).Size(MP(1002.f,668.f))[MapArea];
 MapFrame->AddSlot().Position(MP(0.f,3.f)).Size(MP(1024.f,18.f))
 [SNew(STextBlock).Justification(ETextJustify::Center).Font(FWowWindowStyle::TitleFont(11)).ColorAndOpacity(FWowWindowStyle::Gold())
  .ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black).Text(FText::FromString(TEXT("World Map")))];
 MapFrame->AddSlot().Position(MP(18.f,34.f)).Size(FVector2D(988.f*MapFrameScale,26.f))[MapControls];
 MapFrame->AddSlot().Position(MP(996.f,-4.f)).Size(MP(32.f,32.f))
 [SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0)
  .OnClicked_Lambda([this](){SetVisibility(EVisibility::Hidden);CloseTaxi();return FReply::Handled();})
  [SNew(SImage).Image_Lambda([this](){const FSlateBrush* B=PoiBrush(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"),FBox2f(FVector2f(0.f,0.f),FVector2f(1.f,1.f)));return B?B:FCoreStyle::Get().GetBrush("NoBrush");})]];
 MapFrame->AddSlot().Position(MP(18.f,741.f)).Size(FVector2D(988.f*MapFrameScale,20.f))
 [SNew(SHorizontalBox)
  +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[MapStatus]
  +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0,0,0)[MapCoords]];
 ChildSlot[SNew(SBox).WidthOverride(1024.f*MapFrameScale).HeightOverride(768.f*MapFrameScale)[MapFrame]];
 Surface->Draw=[this](const FGeometry&G,FSlateWindowElementList&E,int32 L){return PaintMap(G,E,L);};
 Surface->Wheel=[this](FVector2D P,FVector2D S,float D){OnWheel(P,S,D);};
 Surface->Click=[this](FVector2D P,FVector2D S){OnClick(P,S);};
 Surface->Drag=[this](FVector2D D,FVector2D S){OnDrag(D,S);};
 Surface->Hover=[this](FVector2D P,FVector2D S){OnHover(P,S);};
 Surface->RightClick=[this](){if(!bTaxiMode)GoUp();};
 Surface->Leave=[](){GWowMapCursor=FVector2D(-1.0,-1.0);GWowMapTipQuest=0;};
 SetVisibility(EVisibility::Hidden);
}

FString SWowWorldMap::ViewName(int32 View)const
{
 if(View==WorldView)return TEXT("World");
 if(View==CosmicView)return TEXT("Azeroth / Outland");
 if(!Data.Areas.IsValidIndex(View))return TEXT("World Map");
 const auto& A=Data.Areas[View];
 if(A.bSynthetic)return A.Name;
 if(A.Area==0)
 {
  if(A.Map==MapEasternKingdoms)return TEXT("Eastern Kingdoms");
  if(A.Map==MapKalimdor)return TEXT("Kalimdor");
  if(A.Map==MapOutland)return TEXT("Outland");
  if(A.Map==MapNorthrend)return TEXT("Northrend");
 }
 const auto* Area=FDbcStore::Get().AreaTable().GetById(A.Area);
 return Area?Area->Name:A.Folder;
}

void SWowWorldMap::Toggle(){bool Open=!GetVisibility().IsVisible();SetVisibility(Open?EVisibility::Visible:EVisibility::Hidden);if(Open)Current();}

void SWowWorldMap::ShowQuest(uint32 QuestId)
{
 SetVisibility(EVisibility::Visible);
 Current();
 if(!Quests)return;
 Quests->SuperTrackedQuestId=QuestId;
 const FWowQuestLogEntry* Quest=Quests->QuestLog.FindByPredicate([QuestId](const FWowQuestLogEntry& Q){return Q.QuestId==QuestId;});
 const TArray<FWowQuestPOI>* Pois=Quests->QuestPOIs.Find(QuestId);
 if(!Quest||!Pois)return;
 const bool bComplete=Quest->State==1;
 // Already on a zone that shows the quest: stay there.
 if(Data.Areas.IsValidIndex(Selected))
  for(const FWowQuestPOI& Poi:*Pois)if(Poi.WorldMapAreaId==Data.Areas[Selected].Id)return;
 for(int32 Pass=0;Pass<2;++Pass)
  for(const FWowQuestPOI& Poi:*Pois)
  {
   if(Pass==0&&(bComplete?Poi.ObjectiveIndex>=0:Poi.ObjectiveIndex<0))continue; // current step first
   for(int32 I=0;I<Data.Areas.Num();++I)
    if(Data.Areas[I].Id==Poi.WorldMapAreaId&&!Data.Areas[I].bSynthetic&&HasMapArt(I))
    {Select(I,Data.Areas[I].bDungeon?int32(Poi.Floor):0);return;}
  }
}

void SWowWorldMap::Current()
{
 // The UI zone can be a sub-zone (Valley of Trials): climb to the zone that has a map.
 uint32 Id=Zone?Zone():0;
 for(int32 Guard=0;Id&&Guard<4;++Guard)
 {
  for(int32 I=0;I<Data.Areas.Num();++I)if(Data.Areas[I].Area==Id&&!Data.Areas[I].bSynthetic&&HasMapArt(I))
  {Select(I,Data.Areas[I].bDungeon?PlayerFloor(I):0);return;}
  const auto* Area=FDbcStore::Get().AreaTable().GetById(Id);
  Id=Area?Area->ParentAreaID:0;
 }
 const uint32 Map=PlayerMap();
 // Inside an instance whose area id did not match: any dungeon map of that instance.
 for(int32 I=0;I<Data.Areas.Num();++I)if(Data.Areas[I].bDungeon&&Data.Areas[I].Map==Map&&HasMapArt(I))
 {Select(I,PlayerFloor(I));return;}
 const int32 Continent=ContinentForMap(Map);
 if(Continent!=INDEX_NONE){Select(Continent);return;}
 if(Selected==INDEX_NONE&&Data.Areas.Num())Select(0);
}

int32 SWowWorldMap::ContinentForMap(uint32 Map)const
{
 for(int32 I=0;I<Data.Areas.Num();++I)if(Data.Areas[I].Area==0&&!Data.Areas[I].bDungeon&&Data.Areas[I].Map==Map)return I;
 return INDEX_NONE;
}

int32 SWowWorldMap::ParentOf(int32 View)const
{
 if(View==CosmicView)return INDEX_NONE;
 if(View==WorldView)return CosmicView;
 if(!Data.Areas.IsValidIndex(View))return INDEX_NONE;
 const auto& A=Data.Areas[View];
 if(A.bDungeon)return WorldView; // instance maps have no parent zone in the 3.3.5 data
 if(A.Area!=0)
 {
  const int32 Continent=ContinentForMap(A.Map);
  if(Continent!=INDEX_NONE&&Continent!=View)return Continent;
 }
 // Continents (and zones without a continent map, e.g. instances) go to World / Cosmic.
 return A.Map==MapOutland?CosmicView:(A.Map==MapEasternKingdoms||A.Map==MapKalimdor||A.Map==MapNorthrend?WorldView:INDEX_NONE);
}

void SWowWorldMap::GoUp()
{
 if(ViewZoom>1.001f&&ParentOf(Selected)==INDEX_NONE){ViewZoom=1.f;ViewCenter=FVector2D(.5f,.5f);return;}
 const int32 Parent=ParentOf(Selected);
 if(Parent!=INDEX_NONE)Select(Parent);
}

bool SWowWorldMap::HotspotForMap(int32 View,uint32 Map,FBox2D& OutRect)
{
 // Where each continent sits on the World / Cosmic artwork (normalised 1002x668 page).
 if(View==WorldView)
 {
  if(Map==MapKalimdor){OutRect=FBox2D(FVector2D(.05,.20),FVector2D(.40,.95));return true;}
  if(Map==MapEasternKingdoms){OutRect=FBox2D(FVector2D(.62,.16),FVector2D(.95,.92));return true;}
  if(Map==MapNorthrend){OutRect=FBox2D(FVector2D(.36,.02),FVector2D(.66,.32));return true;}
 }
 if(View==CosmicView)
 {
  // Cosmic artwork: Outland is the planet top left, Azeroth the globe bottom right.
  if(Map==MapOutland){OutRect=FBox2D(FVector2D(.17,.07),FVector2D(.47,.58));return true;}
  if(Map==MapEasternKingdoms||Map==MapKalimdor||Map==MapNorthrend){OutRect=FBox2D(FVector2D(.60,.40),FVector2D(.95,.94));return true;}
 }
 return false;
}

int32 SWowWorldMap::PickTarget(FVector2D P)const
{
 if(Selected==CosmicView)
 {
  FBox2D R;
  if(HotspotForMap(CosmicView,MapOutland,R)&&R.IsInside(P))return ContinentForMap(MapOutland);
  if(HotspotForMap(CosmicView,MapKalimdor,R)&&R.IsInside(P))return WorldView;
  return INDEX_NONE;
 }
 if(Selected==WorldView)
 {
  for(uint32 Map:{MapNorthrend,MapKalimdor,MapEasternKingdoms})
  {FBox2D R;if(HotspotForMap(WorldView,Map,R)&&R.IsInside(P))return ContinentForMap(Map);}
  return INDEX_NONE;
 }
 if(!Data.Areas.IsValidIndex(Selected)||Data.Areas[Selected].bDungeon)return INDEX_NONE;
 const auto& View=Data.Areas[Selected];
 // Map position -> world coordinates (inverse of FWowMapArea::Project).
 const double WorldY=View.Left+P.X*(View.Right-View.Left);
 const double WorldX=View.Top+P.Y*(View.Bottom-View.Top);
 // Zones overlap as rectangles: pick the one the point is most central in.
 int32 Best=INDEX_NONE;double BestScore=TNumericLimits<double>::Max();
 for(int32 I=0;I<Data.Areas.Num();++I)
 {
  const auto& A=Data.Areas[I];
  if(A.Map!=View.Map||A.Area==0||A.bDungeon)continue;
  const FVector2D Local=A.Project(WorldX,WorldY);
  if(Local.X<0||Local.X>1||Local.Y<0||Local.Y>1)continue;
  const double Score=FMath::Max(FMath::Abs(Local.X-.5),FMath::Abs(Local.Y-.5));
  if(Score<BestScore){BestScore=Score;Best=I;}
 }
 return Best==Selected?INDEX_NONE:Best;
}

void SWowWorldMap::Select(int32 Index,int32 Floor)
{
 if(!Data.Areas.IsValidIndex(Index)&&Index!=WorldView&&Index!=CosmicView)return;
 Selected=Index;CurrentFloor=0;Tiles.Reset();NextTile=0;ViewZoom=1.f;ViewCenter=FVector2D(.5f,.5f);HoverTarget=INDEX_NONE;
 auto Add=[this](FString Path,FVector2D P,FVector2D S,FVector2D UV=FVector2D(1,1)){FTile T;T.Path=MoveTemp(Path);T.Pos=P;T.Size=S;T.UV=UV;Tiles.Add(MoveTemp(T));};
 auto AddPage=[&Add](const FString& Folder)
 {
  const FString Base=TEXT("Interface\\WorldMap\\")+Folder+TEXT("\\");
  for(int32 Y=0;Y<3;++Y)for(int32 X=0;X<4;++X){FVector2D Size(FMath::Min(256,1002-X*256),FMath::Min(256,668-Y*256));Add(Base+Folder+FString::FromInt(Y*4+X+1)+TEXT(".blp"),FVector2D(X*256/1002.,Y*256/668.),FVector2D(Size.X/1002.,Size.Y/668.),Size/256.);}
 };
 // Terrain minimap tiles (Textures\Minimap\<map>\mapX_Y.blp) covering a view.
 auto AddAdt=[this,&Add](const FWowMapArea& V)
 {
  const auto*M=FDbcStore::Get().Maps().GetById(V.Map);
  if(!M)return;
  constexpr double Step=533.333333333;
  // One extra tile around the zone so the map edges are filled too.
  int32 X0=FMath::Clamp(FMath::FloorToInt(32-V.Left/Step)-1,0,63),X1=FMath::Clamp(FMath::FloorToInt(32-V.Right/Step)+1,0,63);
  int32 Y0=FMath::Clamp(FMath::FloorToInt(32-V.Top/Step)-1,0,63),Y1=FMath::Clamp(FMath::FloorToInt(32-V.Bottom/Step)+1,0,63);
  const FVector2D TileSize(Step/(V.Left-V.Right),Step/(V.Top-V.Bottom));
  // Satellite v2 resolution: the tile's share of a ~1000 px map, x2 for zooming in (64 on a
  // continent, 256-512 on a zone).
  const int32 V2Pixels=FMath::Clamp(int32(FMath::RoundUpToPowerOfTwo(uint32(FMath::Max(1.0,FMath::Abs(TileSize.X)*2000.0)))),64,512);
  for(int32 Y=Y0;Y<=Y1;++Y)for(int32 X=X0;X<=X1;++X)
  {
   const FString Key=FString::Printf(TEXT("%s\\map%d_%d.blp"),*M->InternalName,X,Y).ToLower();
   const FString* Path=Data.Satellite.Find(Key);
   // Same tile numbers as md5translate's mapX_Y, so a recorded tile takes the BLP tile's place.
   if(bSatelliteV2&&FWowSatelliteTiles::HasTile(M->InternalName,X,Y))
   {
    Add(Path?TEXT("Textures\\Minimap\\")+*Path:FString(),V.Project((32-Y)*Step,(32-X)*Step),TileSize);
    FTile& V2=Tiles.Last();V2.V2Map=M->InternalName;V2.V2Tile=FIntPoint(X,Y);V2.V2Pixels=V2Pixels;
   }
   else if(Path)Add(TEXT("Textures\\Minimap\\")+*Path,V.Project((32-Y)*Step,(32-X)*Step),TileSize);
  }
 };
 if(Index==WorldView){AddPage(TEXT("World"));return;}
 if(Index==CosmicView){AddPage(TEXT("Cosmic"));return;}
 const auto&A=Data.Areas[Index];
 if(A.bDungeon)
 {
  // Dungeon/raid floor: Interface\WorldMap\<Folder>\<Folder><floor>_<tile>.blp.
  EnsureWmo(A.Map);
  const auto Floors=Data.FloorsFor(A);
  if(!Floors.IsEmpty())
  {
   const FWowDungeonFloor* Chosen=Floors[0];
   for(const auto* F:Floors)if(F->Floor==(Floor>0?Floor:A.DefaultFloor))Chosen=F;
   CurrentFloor=Chosen->Floor;
  }
  // Minimap style: the instance's own minimap tiles (also the only map of most vanilla/TBC instances).
  if(Floors.IsEmpty()||Satellite)
  {
   const FWowMapArea V=ViewArea(Index);
   const FWowWmoMinimap* W=Data.Wmo.Find(A.Map);
   if(!W||!W->IsValid()){if(V.Left!=V.Right&&V.Top!=V.Bottom)AddAdt(V);return;}
   for(const auto& T:W->Tiles)
   {
    // Overlapping floors: a group belongs to the smallest floor that contains its centre.
    if(Floors.Num()>1)
    {
     const FVector2D C=W->GroupWorld(T.Group).GetCenter();const FWowDungeonFloor* Best=nullptr;
     for(const auto* F:Floors)if(F->Contains(C.X,C.Y)&&(!Best||F->AreaSize()<Best->AreaSize()))Best=F;
     if(Best&&Best->Floor!=CurrentFloor)continue;
    }
    const FBox2D R=W->TileWorld(T);
    const FVector2D P0=V.Project(R.Max.X,R.Max.Y),P1=V.Project(R.Min.X,R.Min.Y);
    if(P1.X<-.25||P0.X>1.25||P1.Y<-.25||P0.Y>1.25)continue;
    Add(TEXT("Textures\\Minimap\\")+T.Path,P0,P1-P0);
   }
   return;
  }
  const FString Base=TEXT("Interface\\WorldMap\\")+A.Folder+TEXT("\\");
  for(int32 Y=0;Y<3;++Y)for(int32 X=0;X<4;++X)
  {
   FVector2D Size(FMath::Min(256,1002-X*256),FMath::Min(256,668-Y*256));
   Add(FString::Printf(TEXT("%s%s%d_%d.blp"),*Base,*A.Folder,CurrentFloor,Y*4+X+1),FVector2D(X*256/1002.,Y*256/668.),FVector2D(Size.X/1002.,Size.Y/668.),Size/256.);
  }
  return;
 }
 if(!Satellite)
 {
  AddPage(A.Folder);
  const FString Base=TEXT("Interface\\WorldMap\\")+A.Folder+TEXT("\\");
  for(const auto&O:Data.Overlays)if(O.Area==A.Id&&!O.Name.IsEmpty()&&O.Width>0&&O.Height>0&&O.Width<2048&&O.Height<2048)
  {int32 N=1;for(int32 Y=0;Y<O.Height;Y+=256)for(int32 X=0;X<O.Width;X+=256){int32 W=FMath::Min(256,O.Width-X),H=FMath::Min(256,O.Height-Y);Add(Base+O.Name+FString::FromInt(N++)+TEXT(".blp"),FVector2D((O.X+X)/1002.,(O.Y+Y)/668.),FVector2D(W/1002.,H/668.),FVector2D(double(W)/FMath::RoundUpToPowerOfTwo(W),double(H)/FMath::RoundUpToPowerOfTwo(H)));}}
 }
 else AddAdt(A);
}

FVector2D SWowWorldMap::ToMap(FVector2D Local,FVector2D Size)const
{
 return ViewCenter+(Local/FVector2D(FMath::Max(1.0,Size.X),FMath::Max(1.0,Size.Y))-FVector2D(.5,.5))/ViewZoom;
}
FVector2D SWowWorldMap::ToLocal(FVector2D Map,FVector2D Size)const
{
 return ((Map-ViewCenter)*ViewZoom+FVector2D(.5,.5))*Size;
}
void SWowWorldMap::ClampView()
{
 ViewZoom=FMath::Clamp(ViewZoom,1.f,6.f);
 const float Half=.5f/ViewZoom;
 ViewCenter.X=FMath::Clamp(ViewCenter.X,(double)Half,1.0-Half);
 ViewCenter.Y=FMath::Clamp(ViewCenter.Y,(double)Half,1.0-Half);
}

void SWowWorldMap::OnWheel(FVector2D Local,FVector2D Size,float Delta)
{
 // The wheel only zooms this map; going up a level is right-click or "Zoom Out".
 if(Delta<0&&ViewZoom<=1.001f)return;
 const FVector2D Anchor=ToMap(Local,Size);
 ViewZoom*=FMath::Pow(1.25f,Delta);
 ViewZoom=FMath::Clamp(ViewZoom,1.f,6.f);
 // Keep the point under the cursor fixed while zooming.
 ViewCenter=Anchor-(Local/Size-FVector2D(.5,.5))/ViewZoom;
 ClampView();
}
void SWowWorldMap::OnDrag(FVector2D DeltaPixels,FVector2D Size)
{
 ViewCenter-=DeltaPixels/Size/ViewZoom;
 ClampView();
}
void SWowWorldMap::OnClick(FVector2D Local,FVector2D Size)
{
 if(bTaxiMode)
 {
  const int32 Index=TaxiPointAt(Local,Size);
  if(TaxiPoints.IsValidIndex(Index)&&TaxiPoints[Index].bReachable&&!TaxiPoints[Index].bCurrent&&TaxiFly)
  {
   const TArray<uint32> Route=TaxiPoints[Index].Route;
   TaxiFly(Route);
  }
  return;
 }
 // WorldMapQuestPOI_OnClick: select the quest; its objective areas show and the minimap tracks it.
 uint32 PoiQuest=0;
 if(Quests&&QuestPoiAt(Local,Size,PoiQuest)){Quests->SuperTrackedQuestId=PoiQuest;return;}
 const int32 Target=PickTarget(ToMap(Local,Size));
 if(Target!=INDEX_NONE)Select(Target);
}
void SWowWorldMap::OnHover(FVector2D Local,FVector2D Size)
{
 GWowMapCursor=ToMap(Local,Size);
 GWowMapTipQuest=0;
 if(bTaxiMode){HoverTaxi=TaxiPointAt(Local,Size);HoverTarget=INDEX_NONE;HoverQuest=0;return;}
 HoverQuest=0;
 if(QuestPoiAt(Local,Size,HoverQuest)){HoverTarget=INDEX_NONE;GWowMapTipQuest=HoverQuest;GWowMapTipPos=Local;return;}
 // r371: over one of the selected quest's objective areas (drawn as in PaintQuestPois).
 if(Quests&&Data.Areas.IsValidIndex(Selected))
 {
  const uint32 TrackedQuest=Quests->SuperTrackedQuestId;
  const FWowQuestLogEntry* Tracked=Quests->QuestLog.FindByPredicate([TrackedQuest](const FWowQuestLogEntry& Q){return Q.QuestId==TrackedQuest;});
  const TArray<FWowQuestPOI>* AreaList=Tracked&&Tracked->State!=1?Quests->QuestPOIs.Find(TrackedQuest):nullptr;
  if(AreaList)
  {
   const FWowMapArea& ShownArea=Data.Areas[Selected];
   const FWowMapArea ShownView=ViewArea(Selected);
   for(const FWowQuestPOI& Poi:*AreaList)
   {
    if(Poi.ObjectiveIndex<0||Poi.WorldMapAreaId!=ShownArea.Id||Poi.Points.Num()<3)continue;
    if(ShownArea.bDungeon&&Poi.Floor!=0&&int32(Poi.Floor)!=CurrentFloor)continue;
    TArray<FVector2D> LocalHull;
    for(const FVector2D& W:PoiHull(Poi.Points))LocalHull.Add(ToLocal(ShownView.Project(W.X,W.Y),Size));
    if(WowPointInConvex(LocalHull,Local)){GWowMapTipQuest=TrackedQuest;GWowMapTipPos=Local;break;}
   }
  }
 }
 HoverTarget=PickTarget(ToMap(Local,Size));
}

uint32 SWowWorldMap::PlayerMap()const
{
 const auto* Area=Zone?FDbcStore::Get().AreaTable().GetById(Zone()):nullptr;
 return Area?Area->MapID:MAX_uint32;
}

bool SWowWorldMap::PlayerOnView(int32 View,FVector2D& OutPos,float& OutFacing)const
{
 const auto* P=Entities?Entities->GetLocalPlayer():nullptr;
 const uint32 Map=PlayerMap();
 if(!P||Map==MAX_uint32)return false;
 OutFacing=P->Movement.Orientation;
 auto Inside=[](const FVector2D& V){return V.X>=0&&V.X<=1&&V.Y>=0&&V.Y<=1;};
 if(Data.Areas.IsValidIndex(View))
 {
  const FWowMapArea A=ViewArea(View);
  if(A.Map!=Map)return false;
  // Overlapping floors: only show the arrow on the floor the player is on.
  if(A.bDungeon&&PlayerFloor(View)!=CurrentFloor)return false;
  OutPos=A.Project(P->Movement.Position.X,P->Movement.Position.Y);
  return Inside(OutPos);
 }
 // World / Cosmic: place the continent position inside that continent's hotspot.
 const int32 Continent=ContinentForMap(Map);
 FBox2D Rect;
 if(Continent==INDEX_NONE||!HotspotForMap(View,Map,Rect))return false;
 const FVector2D OnContinent=Data.Areas[Continent].Project(P->Movement.Position.X,P->Movement.Position.Y);
 if(!Inside(OnContinent))return false;
 OutPos=Rect.Min+OnContinent*(Rect.Max-Rect.Min);
 // Azeroth on the cosmic map: go through the World map layout (continent inside the
 // world page), then scale the world page into the Azeroth globe.
 FBox2D WorldRect;
 if(View==CosmicView&&Map!=MapOutland&&HotspotForMap(WorldView,Map,WorldRect))
 {
  const FVector2D OnWorld=WorldRect.Min+OnContinent*(WorldRect.Max-WorldRect.Min);
  OutPos=Rect.Min+OnWorld*(Rect.Max-Rect.Min);
 }
 return true;
}

void SWowWorldMap::Tick(const FGeometry&G,double T,float D)
{
 SCompoundWidget::Tick(G,T,D);
 if(!GetVisibility().IsVisible()&&bTaxiMode)CloseTaxi();
 if(!GetVisibility().IsVisible()||!UI.IsValid()||!UI->GetFrameManager())return;
 if(!ArrowTexture.IsValid()&&T>=NextArrowTry)
 {
  NextArrowTry=T+2.0;
  if(UTexture2D* Arrow=UI->GetFrameManager()->LoadUITexture(TEXT("Interface\\Minimap\\MinimapArrow")))
  {ArrowTexture.Reset(Arrow);ArrowBrush.SetResourceObject(Arrow);ArrowBrush.ImageSize=FVector2D(32,32);ArrowBrush.DrawAs=ESlateBrushDrawType::Image;}
 }
 // Like WorldMapFrame_UpdateQuests: with nothing selected, the first quest on the map is selected.
 if(Quests&&Quests->SuperTrackedQuestId==0)
 {
  TArray<FQuestPoiIcon> Icons;GatherQuestPois(Icons);
  if(Icons.Num())Quests->SuperTrackedQuestId=Icons[0].QuestId;
 }
 for(int32 Budget=0;Budget<4&&NextTile<Tiles.Num();++Budget)
 {
  auto& Tile=Tiles[NextTile++];
  if(Tile.V2Tile.X>=0)
   if(UTexture2D* V2Texture=FWowSatelliteTiles::LoadTile(Tile.V2Map,Tile.V2Tile.X,Tile.V2Tile.Y,Tile.V2Pixels))
   {
    Tile.V2Texture=V2Texture;Tile.Brush.SetResourceObject(V2Texture);Tile.Brush.ImageSize=FVector2D(V2Texture->GetSizeX(),V2Texture->GetSizeY());
    Tile.Brush.DrawAs=ESlateBrushDrawType::Image;Tile.Brush.SetUVRegion(FBox2f(FVector2f(0,0),FVector2f(1,1)));continue;
   }
  if(Tile.Path.IsEmpty())continue;
  if(auto* Texture=UI->GetFrameManager()->LoadUITexture(Tile.Path))
  {Tile.Texture.Reset(Texture);Tile.Brush.SetResourceObject(Texture);Tile.Brush.ImageSize=FVector2D(Texture->GetSizeX(),Texture->GetSizeY());Tile.Brush.DrawAs=ESlateBrushDrawType::Image;Tile.Brush.SetUVRegion(FBox2f(FVector2f(0,0),FVector2f(Tile.UV)));}
 }
}

// r357 (build fix): the map surface is a Construct() local; TaxiTooltipAnchor reads its size from here.
static FVector2D GWowMapSurfaceSize=FVector2D::ZeroVector;
int32 SWowWorldMap::PaintMap(const FGeometry&G,FSlateWindowElementList&E,int32 L)const
{
 GWowMapSurfaceSize=FVector2D(G.GetLocalSize());
 // Satellite: tiles without terrain are open sea, so the backdrop is minimap water.
 const bool bDungeonView=Data.Areas.IsValidIndex(Selected)&&Data.Areas[Selected].bDungeon;
 const FLinearColor Backdrop=bDungeonView&&(Satellite||Data.FloorsFor(Data.Areas[Selected]).IsEmpty())?FLinearColor(0,0,0)
  :Satellite&&Data.Areas.IsValidIndex(Selected)?FLinearColor::FromSRGBColor(FColor(17,42,56)):WowColor::FromWow(.035f, .03f, .02f);
 FSlateDrawElement::MakeBox(E,L,G.ToPaintGeometry(),FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,Backdrop);
 const FVector2D Size=G.GetLocalSize();
 if(Satellite&&!(Data.Areas.IsValidIndex(Selected)&&Data.Areas[Selected].bDungeon))
 {
  // Fill pass: every minimap tile drawn enlarged underneath the real tiles, so
  // single missing tiles and seams are covered by the neighbouring terrain.
  for(const auto&T:Tiles)if(T.IsReady())
  {
   const FVector2D Grow=T.Size*.35;
   const FVector2D TopLeft=ToLocal(T.Pos-Grow,Size),BottomRight=ToLocal(T.Pos+T.Size+Grow,Size);
   FSlateDrawElement::MakeBox(E,L+1,G.ToPaintGeometry(FVector2f(BottomRight-TopLeft),FSlateLayoutTransform(FVector2f(TopLeft))),&T.Brush,
    ESlateDrawEffect::None,WowColor::FromWow(.92f, .92f, .92f));
  }
 }
 for(const auto&T:Tiles)if(T.IsReady())
 {
  const FVector2D TopLeft=ToLocal(T.Pos,Size),BottomRight=ToLocal(T.Pos+T.Size,Size);
  FSlateDrawElement::MakeBox(E,L+2,G.ToPaintGeometry(FVector2f(BottomRight-TopLeft),FSlateLayoutTransform(FVector2f(TopLeft))),&T.Brush);
 }
 // Highlight the area a click would open (continent hotspots / neighbouring zones).
 if(HoverTarget!=INDEX_NONE)
 {
  FBox2D Box(ForceInit);
  // Neighbouring zones on a zone map get no box: their rectangles overlap most of the map,
  // so it lit up the whole view. The caption at the bottom names the zone instead.
  if(Data.Areas.IsValidIndex(HoverTarget)&&Data.Areas.IsValidIndex(Selected)&&Data.Areas[HoverTarget].Map==Data.Areas[Selected].Map)
  {
  }
  // The planets on the cosmic page are round: no square tint there (the caption names them).
  else if(Selected==CosmicView){}
  else if(Data.Areas.IsValidIndex(HoverTarget))HotspotForMap(Selected,Data.Areas[HoverTarget].Map,Box);
  else if(HoverTarget==WorldView)HotspotForMap(Selected,MapKalimdor,Box);
  if(Box.bIsValid)
  {
   const FVector2D A=ToLocal(Box.Min,Size),B=ToLocal(Box.Max,Size);
   FSlateDrawElement::MakeBox(E,L+3,G.ToPaintGeometry(FVector2f(B-A),FSlateLayoutTransform(FVector2f(A))),
    FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,WowColor::FromWow(1.f, .82f, .2f, .12f));
  }
 }
 if(bTaxiMode)PaintTaxi(G,E,L+3,Size);
 else PaintQuestPois(G,E,L+3,Size);
 // r255: the corpse while you are a ghost (WorldMapCorpse in the original): a gravestone where you died.
 if(Quests&&Data.Areas.IsValidIndex(Selected))
 {
  const FWowDeathState& Death=Quests->DeathState;
  const FWowMapArea A=ViewArea(Selected);
  if(Death.bGhost&&Death.bHasCorpse&&A.Map==Death.CorpseMap)
  {
   const FVector2D CorpseUV=A.Project(Death.CorpsePos.X,Death.CorpsePos.Y);
   if(CorpseUV.X>=0&&CorpseUV.X<=1&&CorpseUV.Y>=0&&CorpseUV.Y<=1)
    PaintWorldMapGravestone(E,L+7,G.GetAccumulatedRenderTransform(),ToLocal(CorpseUV,Size),20.f);
  }
 }
 // Player arrow: position and facing (orientation 0 = north, counter-clockwise).
 FVector2D PlayerPos;float Facing=0.f;
 if(PlayerOnView(Selected,PlayerPos,Facing))
 {
  const FVector2D Center=ToLocal(PlayerPos,Size);
  const float ArrowSize=ArrowTexture.IsValid()?28.f:12.f;
  const FVector2D Half(ArrowSize*.5f,ArrowSize*.5f);
  const FSlateBrush* Brush=ArrowTexture.IsValid()?&ArrowBrush:FCoreStyle::Get().GetBrush("WhiteBrush");
  FSlateDrawElement::MakeRotatedBox(E,L+9,G.ToPaintGeometry(FVector2f(ArrowSize,ArrowSize),FSlateLayoutTransform(FVector2f(Center-Half))),
   Brush,ESlateDrawEffect::None,-Facing,{},FSlateDrawElement::RelativeToElement,
   ArrowTexture.IsValid()?FLinearColor::White:FLinearColor::Yellow);
 }
 return L+9;
}


const FSlateBrush* SWowWorldMap::PoiBrush(const TCHAR* Path,FBox2f UV)const
{
 const FString Key=FString::Printf(TEXT("%s|%.3f,%.3f,%.3f,%.3f"),Path,UV.Min.X,UV.Min.Y,UV.Max.X,UV.Max.Y);
 if(const TSharedPtr<FSlateBrush>* Found=PoiBrushes.Find(Key))return Found->Get();
 UTexture2D* Texture=UI.IsValid()&&UI->GetFrameManager()?UI->GetFrameManager()->LoadUITexture(Path):nullptr;
 if(!Texture)return nullptr; // retried next paint
 PoiTextures.Emplace(Texture);
 TSharedPtr<FSlateBrush> Brush=MakeShared<FSlateBrush>();
 Brush->SetResourceObject(Texture);Brush->DrawAs=ESlateBrushDrawType::Image;
 Brush->ImageSize=FVector2D(Texture->GetSizeX(),Texture->GetSizeY());Brush->SetUVRegion(UV);
 PoiBrushes.Add(Key,Brush);return Brush.Get();
}

void SWowWorldMap::GatherQuestPois(TArray<FQuestPoiIcon>& Out)const
{
 Out.Reset();
 if(!Quests||!Data.Areas.IsValidIndex(Selected))return;
 const FWowMapArea& Area=Data.Areas[Selected];
 const FWowMapArea View=ViewArea(Selected);
 int32 Numeric=0;
 for(const FWowQuestLogEntry& Quest:Quests->QuestLog)
 {
  const TArray<FWowQuestPOI>* List=Quests->QuestPOIs.Find(Quest.QuestId);
  if(!List)continue;
  const bool bComplete=Quest.State==1;
  const FWowQuestPOI* Icon=nullptr;
  for(const FWowQuestPOI& Poi:*List)
  {
   if(Poi.WorldMapAreaId!=Area.Id||Poi.Points.IsEmpty())continue;
   if(Area.bDungeon&&Poi.Floor!=0&&int32(Poi.Floor)!=CurrentFloor)continue;
   // Unfinished quests point at their first objective area, finished ones at the turn-in.
   if(bComplete?Poi.ObjectiveIndex<0:Poi.ObjectiveIndex>=0){Icon=&Poi;break;}
  }
  if(!Icon)continue;
  FQuestPoiIcon Entry;Entry.QuestId=Quest.QuestId;Entry.bComplete=bComplete;
  Entry.Number=bComplete?0:++Numeric;
  const FVector2D World=PoiCentroid(PoiHull(Icon->Points));
  Entry.MapPos=View.Project(World.X,World.Y);
  if(Entry.MapPos.X<-.05||Entry.MapPos.X>1.05||Entry.MapPos.Y<-.05||Entry.MapPos.Y>1.05)continue;
  Entry.MapPos.X=FMath::Clamp(Entry.MapPos.X,.01,.99);Entry.MapPos.Y=FMath::Clamp(Entry.MapPos.Y,.01,.99);
  Out.Add(Entry);
 }
}

bool SWowWorldMap::QuestPoiAt(FVector2D Local,FVector2D Size,uint32& OutQuest)const
{
 TArray<FQuestPoiIcon> Icons;GatherQuestPois(Icons);
 // Topmost first (drawn last); HitRectInsets of 8 leave a 16x16 hit area.
 for(int32 I=Icons.Num()-1;I>=0;--I)
 {
  const FVector2D Center=ToLocal(Icons[I].MapPos,Size);
  if(FVector2D::DistSquared(Local,Center)<=13.f*13.f){OutQuest=Icons[I].QuestId;return true;} // the visible circle
 }
 return false;
}

void SWowWorldMap::PaintQuestPois(const FGeometry& G,FSlateWindowElementList& E,int32 L,FVector2D Size)const
{
 if(!Quests||!Data.Areas.IsValidIndex(Selected))return;
 TArray<FQuestPoiIcon> Icons;GatherQuestPois(Icons);
 const uint32 SelectedQuest=Quests->SuperTrackedQuestId;
 const FWowMapArea& Area=Data.Areas[Selected];
 const FWowMapArea View=ViewArea(Selected);

 // Selected unfinished quest: its objective areas as blobs (WorldMapBlobFrame:DrawQuestBlob).
 const FWowQuestLogEntry* Selection=Quests->QuestLog.FindByPredicate([SelectedQuest](const FWowQuestLogEntry& Q){return Q.QuestId==SelectedQuest;});
 if(Selection&&Selection->State!=1)
  if(const TArray<FWowQuestPOI>* List=Quests->QuestPOIs.Find(SelectedQuest))
   for(const FWowQuestPOI& Poi:*List)
   {
    if(Poi.ObjectiveIndex<0||Poi.WorldMapAreaId!=Area.Id||Poi.Points.Num()<3)continue;
    if(Area.bDungeon&&Poi.Floor!=0&&int32(Poi.Floor)!=CurrentFloor)continue;
    const TArray<FVector2D> Hull=PoiHull(Poi.Points);
    if(Hull.Num()<3)continue;
    TArray<FVector2D> Pts;for(const FVector2D& W:Hull)Pts.Add(ToLocal(View.Project(W.X,W.Y),Size));
    const FSlateRenderTransform& RT=G.GetAccumulatedRenderTransform();
    const FColor Fill=WowColor::FromWow(.24f, .55f, 1.f, .30f).ToFColor(true);
    TArray<FSlateVertex> Verts;TArray<SlateIndex> Idx;
    const FVector2D Mid=PoiCentroid(Pts);
    Verts.Add(FSlateVertex::Make(RT,FVector2f(Mid),FVector2f::ZeroVector,Fill));
    for(const FVector2D& P:Pts)Verts.Add(FSlateVertex::Make(RT,FVector2f(P),FVector2f::ZeroVector,Fill));
    for(int32 I=0;I<Pts.Num();++I){Idx.Add(0);Idx.Add(SlateIndex(1+I));Idx.Add(SlateIndex(1+(I+1)%Pts.Num()));}
    FSlateDrawElement::MakeCustomVerts(E,L,FSlateResourceHandle(),Verts,Idx,nullptr,0,0,ESlateDrawEffect::None);
    TArray<FVector2D> Outline=Pts;Outline.Add(Pts[0]);
    FSlateDrawElement::MakeLines(E,L+1,G.ToPaintGeometry(),Outline,ESlateDrawEffect::None,WowColor::FromWow(.45f, .75f, 1.f, .85f),true,2.f);
   }

 // bAdd: alphaMode="ADD" textures (glow, highlight). Premultiplied with zero alpha adds the
 // colour instead of covering what is under it, so their black background disappears.
 auto Box=[&](int32 Layer,FVector2D Center,float Px,const FSlateBrush* Brush,FLinearColor Tint=FLinearColor::White,bool bAdd=false)
 {
  if(!Brush)return;
  if(bAdd)Tint=FLinearColor(Tint.R*Tint.A,Tint.G*Tint.A,Tint.B*Tint.A,0.f);
  FSlateDrawElement::MakeBox(E,Layer,G.ToPaintGeometry(FVector2f(Px,Px),FSlateLayoutTransform(FVector2f(Center-FVector2D(Px,Px)*.5))),Brush,
   bAdd?ESlateDrawEffect::PreMultipliedAlpha|ESlateDrawEffect::IgnoreTextureAlpha:ESlateDrawEffect::None,Tint);
 };
 const TCHAR* Numbers=TEXT("Interface\\WorldMap\\UI-QuestPoi-NumberIcons");
 const TCHAR* TurnIn=TEXT("Interface\\WorldMap\\UI-WorldMap-QuestIcon");
 const TCHAR* Glow=TEXT("Interface\\WorldMap\\UI-QuestPoi-IconGlow");
 // Selected icon last so it draws on top (WORLDMAP_POI_FRAMELEVEL + 1).
 Icons.StableSort([SelectedQuest](const FQuestPoiIcon& A,const FQuestPoiIcon& B){return A.QuestId!=SelectedQuest&&B.QuestId==SelectedQuest;});
 for(const FQuestPoiIcon& Icon:Icons)
 {
  const FVector2D C=ToLocal(Icon.MapPos,Size);
  const bool bSel=Icon.QuestId==SelectedQuest,bHover=Icon.QuestId==HoverQuest;
  if(bSel)Box(L+2,C,50.f,PoiBrush(Glow,FBox2f(FVector2f(0,0),FVector2f(1,1))),FLinearColor::White,true);
  if(Icon.bComplete&&!bSel)
  {
   // QuestPOICompletedTemplate: the "?" alone; highlight is the right half of the texture.
   Box(L+3,C,PoiButtonSize,PoiBrush(TurnIn,FBox2f(FVector2f(0,0),FVector2f(.5f,.5f))));
   if(bHover)Box(L+4,C,PoiButtonSize,PoiBrush(TurnIn,FBox2f(FVector2f(.5f,0),FVector2f(1,.5f))));
   continue;
  }
  // Circle: normal (0.875,1,0.875,1), selected (0.5,0.625,0.375,0.5); ADD highlight on hover.
  Box(L+3,C,PoiButtonSize,PoiBrush(Numbers,bSel?FBox2f(FVector2f(.5f,.375f),FVector2f(.625f,.5f)):FBox2f(FVector2f(.875f,.875f),FVector2f(1,1))));
  if(Icon.bComplete)
   Box(L+4,C+FVector2D(-1,0),24.f,PoiBrush(TurnIn,FBox2f(FVector2f(0,0),FVector2f(.5f,.5f))));
  else
  {
   const int32 Index=FMath::Clamp(Icon.Number-1,0,31);
   const float X=(Index%8)*.125f,Y=(bSel?0.f:.5f)+(Index/8)*.125f; // black when selected, yellow otherwise
   Box(L+4,C,PoiButtonSize,PoiBrush(Numbers,FBox2f(FVector2f(X,Y),FVector2f(X+.125f,Y+.125f))));
  }
  if(bHover)Box(L+5,C,PoiButtonSize,PoiBrush(Numbers,bSel?FBox2f(FVector2f(.625f,.375f),FVector2f(.75f,.5f)):FBox2f(FVector2f(.625f,.875f),FVector2f(.75f,1))),FLinearColor::White,true);
 }
}

FWowMapArea SWowWorldMap::ViewArea(int32 View)const
{
 FWowMapArea A=Data.Areas[View];
 if(A.bDungeon)
 {
  const auto Floors=Data.FloorsFor(A);
  const int32 Want=View==Selected?CurrentFloor:A.DefaultFloor;
  for(const auto* F:Floors)if(F->Floor==Want)return FWowWorldMapData::WithFloor(A,*F);
  if(Floors.Num())return FWowWorldMapData::WithFloor(A,*Floors[0]); // never project with zero bounds
  if(const FWowWmoMinimap* W=Data.Wmo.Find(A.Map);W&&W->IsValid())return FWowWorldMapData::WithWmo(A,*W);
 }
 return A;
}

int32 SWowWorldMap::PlayerFloor(int32 View)const
{
 if(!Data.Areas.IsValidIndex(View))return 0;
 const auto* P=Entities?Entities->GetLocalPlayer():nullptr;
 const auto Floors=Data.FloorsFor(Data.Areas[View]);
 if(Floors.IsEmpty())return 0;
 if(!P)return Floors[0]->Floor;
 // Floors overlap in X/Y; the smallest floor that contains the player is the most specific.
 const FWowDungeonFloor* Best=nullptr;
 for(const auto* F:Floors)
  if(F->Contains(P->Movement.Position.X,P->Movement.Position.Y)&&(!Best||F->AreaSize()<Best->AreaSize()))Best=F;
 return Best?Best->Floor:Floors[0]->Floor;
}

int32 SWowWorldMap::FloorCount()const
{
 return Data.Areas.IsValidIndex(Selected)&&Data.Areas[Selected].bDungeon?Data.FloorsFor(Data.Areas[Selected]).Num():0;
}

int32 SWowWorldMap::FloorSlot()const
{
 if(!Data.Areas.IsValidIndex(Selected))return 0;
 const auto Floors=Data.FloorsFor(Data.Areas[Selected]);
 for(int32 I=0;I<Floors.Num();++I)if(Floors[I]->Floor==CurrentFloor)return I;
 return 0;
}

void SWowWorldMap::StepFloor(int32 Direction)
{
 if(!Data.Areas.IsValidIndex(Selected))return;
 const auto Floors=Data.FloorsFor(Data.Areas[Selected]);
 const int32 Next=FloorSlot()+Direction;
 if(Floors.IsValidIndex(Next))Select(Selected,Floors[Next]->Floor);
}

bool SWowWorldMap::HasSatellite(int32 View)const
{
 if(!Data.Areas.IsValidIndex(View))return false;
 const auto& A=Data.Areas[View];
 return A.bDungeon&&(Data.Wmo.Contains(A.Map)||(A.bSynthetic&&A.Left!=A.Right&&A.Top!=A.Bottom));
}

bool SWowWorldMap::HasMapArt(int32 View)const
{
 if(!Data.Areas.IsValidIndex(View))return false;
 const auto& A=Data.Areas[View];
 return !A.bDungeon||Data.FloorsFor(A).Num()>0||HasSatellite(View);
}

void SWowWorldMap::EnsureWmo(uint32 Map)
{
 FWowWmoMinimap* W=Data.Wmo.Find(Map);
 if(!W||W->bLoaded||!Mpq)return;
 W->bLoaded=true;
 TArray<uint8> Bytes;
 if(Mpq->ReadFile(W->WmoPath,Bytes))W->Groups=FWowWmoMinimap::ReadGroupBounds(Bytes);
 W->Build(Data.Satellite);
}

void SWowWorldMap::AddInstances(FMpqManager& InMpq)
{
 // Split "<name>_<a>_<b>_<c>" from the right; returns false unless a, b, c are numbers.
 auto SplitTile=[](const FString& Name,FString& OutBase,FString& OutA,FString& OutB,FString& OutC)
 {
  FString Rest1,Rest2;
  if(!Name.Split(TEXT("_"),&Rest1,&OutC,ESearchCase::CaseSensitive,ESearchDir::FromEnd))return false;
  if(!Rest1.Split(TEXT("_"),&Rest2,&OutB,ESearchCase::CaseSensitive,ESearchDir::FromEnd))return false;
  if(!Rest2.Split(TEXT("_"),&OutBase,&OutA,ESearchCase::CaseSensitive,ESearchDir::FromEnd))return false;
  return OutA.IsNumeric()&&OutB.IsNumeric()&&OutC.IsNumeric()&&!OutBase.IsEmpty();
 };
 // Which WMOs have minimap tiles, and which maps have terrain minimap tiles (index range).
 TSet<FString> WmoBases;TMap<FString,FIntRect> AdtTiles;
 for(const auto& KV:Data.Satellite)
 {
  const FString File=FPaths::GetBaseFilename(KV.Key);
  FString Base,A,B,C;
  if(SplitTile(File,Base,A,B,C)){WmoBases.Add(Base);continue;}
  FString Dir=FPaths::GetPath(KV.Key),X,Y;
  if(File.StartsWith(TEXT("map"))&&File.Mid(3).Split(TEXT("_"),&X,&Y)&&X.IsNumeric()&&Y.IsNumeric())
  {
   const FIntPoint P(FCString::Atoi(*X),FCString::Atoi(*Y));
   if(FIntRect* R=AdtTiles.Find(Dir)){R->Min=R->Min.ComponentMin(P);R->Max=R->Max.ComponentMax(P);}
   else AdtTiles.Add(Dir,FIntRect(P,P));
  }
 }
 int32 Added=0;
 for(const FMapDbcEntry& M:FDbcStore::Get().Maps().GetAll())
 {
  if(M.MapType!=1&&M.MapType!=2)continue; // dungeons and raids
  TArray<uint8> Bytes;FWowWmoMinimap W;
  const FString Wdt=FString::Printf(TEXT("World\\Maps\\%s\\%s.wdt"),*M.InternalName,*M.InternalName);
  if(InMpq.ReadFile(Wdt,Bytes)&&FWowWmoMinimap::ReadWdt(Bytes,W.WmoPath,W.Position,W.Yaw)
   &&WmoBases.Contains(FPaths::GetBaseFilename(W.WmoPath).ToLower()))
   Data.Wmo.Add(M.ID,MoveTemp(W));
  if(Data.Areas.ContainsByPredicate([&M](const FWowMapArea& A){return A.Map==M.ID;}))continue;
  FWowMapArea A;A.Map=M.ID;A.Folder=M.InternalName;A.bDungeon=A.bSynthetic=true;A.Name=M.Name.IsEmpty()?M.InternalName:M.Name;
  if(!Data.Wmo.Contains(M.ID))
  {
   // Terrain instance (e.g. Zul'Farrak): frame its terrain minimap tiles.
   const FIntRect* R=AdtTiles.Find(M.InternalName.ToLower());
   if(!R)continue;
   constexpr double Step=533.333333333;
   FWowWmoMinimap Frame;
   Frame.MaxY=(32-R->Min.X)*Step;Frame.MinY=(32-R->Max.X-1)*Step;Frame.MaxX=(32-R->Min.Y)*Step;Frame.MinX=(32-R->Max.Y-1)*Step;
   A=FWowWorldMapData::WithWmo(A,Frame);
  }
  Data.Areas.Add(MoveTemp(A));++Added;
 }
 UE_LOG(LogTemp,Log,TEXT("[WorldMap] %d instance minimaps (WMO), %d instances without WorldMapArea added"),Data.Wmo.Num(),Added);
}

// ── Flight master (3.3.5 TaxiFrame) ─────────────────────────────────────────
// Stefan 10.10: the old taxi overlay was a pile of grey/green boxes over the world (every node
// of map 0, debug islands included). Now it is this map, opened on the flight master's continent:
// only discovered flight points, the client's taxi icons (green = here, white = reachable,
// yellow = known but no known route), the route drawn on hover, a click flies it.
void SWowWorldMap::ShowTaxi(const FWowTaxiData& Taxi,TFunction<void(const TArray<uint32>&)> Fly)
{
 TaxiFly=MoveTemp(Fly);
 TaxiPoints.Reset();HoverTaxi=INDEX_NONE;HoverTarget=INDEX_NONE;HoverQuest=0;
 if(!bTaxiEdgesLoaded&&Mpq)
 {
  // TaxiPath.dbc (3.3.5): ID, FromTaxiNode, ToTaxiNode, Cost.
  bTaxiEdgesLoaded=true;TArray<uint8> Bytes;FDbcParser P;
  if(Mpq->ReadFile(TEXT("DBFilesClient\\TaxiPath.dbc"),Bytes)&&P.Parse(Bytes)&&P.GetFieldCount()>=4)
   for(uint32 I=0;I<P.GetRecordCount();++I)TaxiEdges.Add({P.GetUInt(I,1),P.GetUInt(I,2),P.GetUInt(I,3)});
  UE_LOG(LogTemp,Log,TEXT("[Taxi] %d flight paths"),TaxiEdges.Num());
 }
 const FWowTaxiNode* Here=Taxi.FindNode(Taxi.CurrentNodeId);
 const int32 Continent=Here?ContinentForMap(Here->MapId):INDEX_NONE;
 // Cheapest route over discovered nodes (the server only flies through known flight points).
 TMap<uint32,uint32> Cost,Previous;TSet<uint32> Settled;
 Cost.Add(Taxi.CurrentNodeId,0);
 for(;;)
 {
  uint32 Best=0;uint32 BestCost=MAX_uint32;
  for(const auto& Pair:Cost)if(!Settled.Contains(Pair.Key)&&Pair.Value<BestCost){Best=Pair.Key;BestCost=Pair.Value;}
  if(BestCost==MAX_uint32)break;
  Settled.Add(Best);
  for(const FTaxiEdge& Edge:TaxiEdges)
  {
   if(Edge.From!=Best||!Taxi.IsNodeKnown(Edge.To)||Settled.Contains(Edge.To))continue;
   const uint32 Candidate=BestCost+Edge.Cost;
   const uint32* Old=Cost.Find(Edge.To);
   if(!Old||Candidate<*Old){Cost.Add(Edge.To,Candidate);Previous.Add(Edge.To,Best);}
  }
 }
 if(Here&&Continent!=INDEX_NONE)
 {
  const FWowMapArea& Area=Data.Areas[Continent];
  for(const FWowTaxiNode& Node:Taxi.AllNodes)
  {
   if(Node.MapId!=Here->MapId||(!Taxi.IsNodeKnown(Node.Id)&&Node.Id!=Taxi.CurrentNodeId))continue;
   const FVector2D MapPos=Area.Project(Node.X,Node.Y);
   if(MapPos.X<0||MapPos.X>1||MapPos.Y<0||MapPos.Y>1)continue;
   FTaxiPoint Point;Point.Id=Node.Id;Point.Name=Node.Name;Point.MapPos=MapPos;
   Point.bCurrent=Node.Id==Taxi.CurrentNodeId;
   if(const uint32* Found=Cost.Find(Node.Id))
   {
    Point.bReachable=true;Point.Cost=*Found;
    for(uint32 Step=Node.Id,Guard=0;Guard<64;++Guard)
    {
     Point.Route.Insert(Step,0);
     const uint32* Back=Previous.Find(Step);
     if(!Back)break;
     Step=*Back;
    }
   }
   TaxiPoints.Add(MoveTemp(Point));
  }
 }
 UE_LOG(LogTemp,Log,TEXT("[Taxi] node %u on map %d: %d flight points shown"),Taxi.CurrentNodeId,Here?int32(Here->MapId):-1,TaxiPoints.Num());
 bTaxiMode=true;
 SetVisibility(EVisibility::Visible);
 if(Continent!=INDEX_NONE)Select(Continent);
 ViewZoom=1.f;ViewCenter=FVector2D(.5f,.5f);
}

void SWowWorldMap::CloseTaxi()
{
 bTaxiMode=false;TaxiPoints.Reset();HoverTaxi=INDEX_NONE;TaxiFly=nullptr;
}

int32 SWowWorldMap::TaxiPointAt(FVector2D Local,FVector2D Size)const
{
 int32 Best=INDEX_NONE;double BestDistance=FMath::Square(12.0);
 for(int32 I=0;I<TaxiPoints.Num();++I)
 {
  const double Distance=FVector2D::DistSquared(ToLocal(TaxiPoints[I].MapPos,Size),Local);
  if(Distance<BestDistance){BestDistance=Distance;Best=I;}
 }
 return Best;
}

void SWowWorldMap::PaintTaxi(const FGeometry& G,FSlateWindowElementList& E,int32 L,FVector2D Size)const
{
 // Route of the hovered point (TaxiFrame draws UI-Taxi-Line segments hop by hop).
 if(TaxiPoints.IsValidIndex(HoverTaxi)&&TaxiPoints[HoverTaxi].Route.Num()>1)
 {
  TArray<FVector2D> Line;
  for(uint32 Step:TaxiPoints[HoverTaxi].Route)
   if(const FTaxiPoint* Point=TaxiPoints.FindByPredicate([Step](const FTaxiPoint& P){return P.Id==Step;}))
    Line.Add(ToLocal(Point->MapPos,Size));
  if(Line.Num()>1)
  {
   FSlateDrawElement::MakeLines(E,L,G.ToPaintGeometry(),Line,ESlateDrawEffect::None,FLinearColor(0,0,0,.7f),true,5.f);
   FSlateDrawElement::MakeLines(E,L+1,G.ToPaintGeometry(),Line,ESlateDrawEffect::None,WowColor::FromWow(1.f,.82f,.1f),true,2.5f);
  }
 }
 const FBox2f Full(FVector2f(0,0),FVector2f(1,1));
 for(int32 I=0;I<TaxiPoints.Num();++I)
 {
  const FTaxiPoint& Point=TaxiPoints[I];
  const TCHAR* Icon=Point.bCurrent?TEXT("Interface\\TaxiFrame\\UI-Taxi-Icon-Green")
   :Point.bReachable?TEXT("Interface\\TaxiFrame\\UI-Taxi-Icon-White"):TEXT("Interface\\TaxiFrame\\UI-Taxi-Icon-Yellow");
  const float IconSize=I==HoverTaxi?24.f:18.f;
  const FVector2D Center=ToLocal(Point.MapPos,Size);
  if(const FSlateBrush* Brush=PoiBrush(Icon,Full))
   FSlateDrawElement::MakeBox(E,L+2,G.ToPaintGeometry(FVector2f(IconSize,IconSize),FSlateLayoutTransform(FVector2f(Center-FVector2D(IconSize*.5f)))),Brush);
  else
   FSlateDrawElement::MakeBox(E,L+2,G.ToPaintGeometry(FVector2f(8.f,8.f),FSlateLayoutTransform(FVector2f(Center-FVector2D(4.f)))),
    FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,Point.bCurrent?FLinearColor::Green:FLinearColor::White);
  if(I==HoverTaxi)
  {
   if(const FSlateBrush* Glow=PoiBrush(TEXT("Interface\\TaxiFrame\\UI-Taxi-Icon-Highlight"),Full))
    FSlateDrawElement::MakeBox(E,L+3,G.ToPaintGeometry(FVector2f(40.f,40.f),FSlateLayoutTransform(FVector2f(Center-FVector2D(20.f)))),Glow,ESlateDrawEffect::None,FLinearColor(1,1,1,.8f));
   const FSlateFontInfo Font=FWowWindowStyle::TitleFont(12);
   FSlateDrawElement::MakeText(E,L+4,G.ToPaintGeometry(FVector2f(400.f,20.f),FSlateLayoutTransform(FVector2f(Center+FVector2D(15.f,-21.f)))),Point.Name,Font,ESlateDrawEffect::None,FLinearColor::Black);
   FSlateDrawElement::MakeText(E,L+5,G.ToPaintGeometry(FVector2f(400.f,20.f),FSlateLayoutTransform(FVector2f(Center+FVector2D(14.f,-22.f)))),Point.Name,Font,ESlateDrawEffect::None,FLinearColor::White);
  }
 }
}

FVector2D SWowWorldMap::TaxiTooltipAnchor()const
{
 // GameTooltip_SetDefaultAnchor-like: just right of and above the hovered flight point.
 if(GWowMapSurfaceSize.IsZero()||!TaxiPoints.IsValidIndex(HoverTaxi))return FVector2D::ZeroVector;
 const FVector2D Size=GWowMapSurfaceSize;
 return ToLocal(TaxiPoints[HoverTaxi].MapPos,Size)+FVector2D(16.,-12.);
}
TSharedRef<SWidget> SWowWorldMap::TaxiCoin(int32 Kind)
{
 // SetTooltipMoney: gold and silver only when not zero, white numbers with the coin icons.
 const TCHAR* Icon=Kind==0?TEXT("Interface\\MoneyFrame\\UI-GoldIcon"):Kind==1?TEXT("Interface\\MoneyFrame\\UI-SilverIcon"):TEXT("Interface\\MoneyFrame\\UI-CopperIcon");
 auto Cost=[this](){return TaxiPoints.IsValidIndex(HoverTaxi)?TaxiPoints[HoverTaxi].Cost:0u;};
 return SNew(SHorizontalBox)
  .Visibility_Lambda([Cost,Kind](){const uint32 C=Cost();return Kind==0?(C>=10000?EVisibility::Visible:EVisibility::Collapsed):Kind==1?(C>=100?EVisibility::Visible:EVisibility::Collapsed):EVisibility::Visible;})
  +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Font(FWowWindowStyle::TooltipFont(false)).ColorAndOpacity(FLinearColor::White)
   .Text_Lambda([Cost,Kind](){const uint32 C=Cost();return FText::AsNumber(Kind==0?C/10000:Kind==1?C/100%100:C%100);})]
  +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2,0,6,0)[SNew(SBox).WidthOverride(13).HeightOverride(13)
   [SNew(SImage).Image_Lambda([this,Icon](){const FSlateBrush* B=PoiBrush(Icon,FBox2f(FVector2f(0,0),FVector2f(.8f,.8f)));return B?B:FCoreStyle::Get().GetBrush("NoBrush");})]];
}
