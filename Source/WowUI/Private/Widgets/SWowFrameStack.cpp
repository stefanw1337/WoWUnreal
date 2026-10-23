#include "Widgets/SWowFrameStack.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformApplicationMisc.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowFrameStack,Log,All);

namespace
{
const TCHAR* StrataName(EWowFrameStrata S)
{
 switch(S)
 {
 case EWowFrameStrata::BACKGROUND:return TEXT("BACKGROUND");
 case EWowFrameStrata::LOW:return TEXT("LOW");
 case EWowFrameStrata::MEDIUM:return TEXT("MEDIUM");
 case EWowFrameStrata::HIGH:return TEXT("HIGH");
 case EWowFrameStrata::DIALOG:return TEXT("DIALOG");
 case EWowFrameStrata::FULLSCREEN:return TEXT("FULLSCREEN");
 case EWowFrameStrata::FULLSCREEN_DIALOG:return TEXT("FULLSCREEN_DIALOG");
 case EWowFrameStrata::TOOLTIP:return TEXT("TOOLTIP");
 }
 return TEXT("?");
}
const FLinearColor Gold=FLinearColor::FromSRGBColor(FColor(255,209,0));
const FLinearColor Grey=FLinearColor::FromSRGBColor(FColor(150,150,150));
const FLinearColor Green=FLinearColor::FromSRGBColor(FColor(64,255,64));
const FLinearColor Cyan=FLinearColor::FromSRGBColor(FColor(110,200,255));
constexpr int32 MaxFrameLines=28;
}

void SWowFrameStack::Construct(const FArguments& Args)
{
 Manager=Args._Manager;ManagerAlive=Args._ManagerAlive;
 SetVisibility(EVisibility::HitTestInvisible); // never part of the stack it shows
 SetCanTick(true);
}

void SWowFrameStack::Tick(const FGeometry& Geometry,double CurrentTime,float DeltaTime)
{
 SLeafWidget::Tick(Geometry,CurrentTime,DeltaTime);
 if(!Manager||!ManagerAlive.IsValid()||!FSlateApplication::IsInitialized())return;
 FSlateApplication& App=FSlateApplication::Get();
 const FModifierKeysState Mods=App.GetModifierKeys();
 bFrozen=Mods.IsAltDown();
 if(!bFrozen)
 {
  Cursor=App.GetCursorPos();
  Stack=Manager->GetFrameStackAt(Cursor);
  ClickTarget=-1;
  for(const auto& E:Stack)if(E.bMouseEnabled){ClickTarget=E.Handle;break;}
  // Native Slate widgets under the cursor (our C++ windows such as the quest log or world map).
  NativeWidgets.Reset();
  const FWidgetPath Path=App.LocateWindowUnderMouse(Cursor,App.GetInteractiveTopLevelWindows(),true);
  for(int32 I=Path.Widgets.Num()-1;I>=0&&NativeWidgets.Num()<6;--I)
  {
   const FString Type=Path.Widgets[I].Widget->GetTypeAsString();
   if(Type.StartsWith(TEXT("SWow"))&&Type!=TEXT("SWowFrameStack")&&!NativeWidgets.Contains(Type))NativeWidgets.Add(Type);
  }
  Rebuild();
 }
 // Ctrl+C: copy (edge triggered).
 bool bC=false;
 if(GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport)bC=GEngine->GameViewport->Viewport->KeyState(EKeys::C);
 const bool bCopy=Mods.IsControlDown()&&bC;
 if(bCopy&&!bCopyDown)
 {
  FString Text;for(const auto& L:Lines)Text+=L.Text+TEXT("\n");
  FPlatformApplicationMisc::ClipboardCopy(*Text);
  UE_LOG(LogWowFrameStack,Log,TEXT("Frame stack at (%.0f, %.0f):\n%s"),Cursor.X,Cursor.Y,*Text);
 }
 bCopyDown=bCopy;
}

void SWowFrameStack::Rebuild()
{
 Lines.Reset();
 Lines.Add({TEXT("Frame Stack   (Alt: freeze   Ctrl+C: copy   /fstack: close)"),Gold});
 if(Stack.IsEmpty())Lines.Add({TEXT("  No FrameXML frame under the cursor (world)."),Grey});
 int32 Shown=0;bool bFirst=true;EWowFrameStrata Current=EWowFrameStrata::BACKGROUND;
 for(const auto& E:Stack)
 {
  if(Shown>=MaxFrameLines){Lines.Add({FString::Printf(TEXT("  ... %d more"),Stack.Num()-Shown),Grey});break;}
  if(bFirst||E.Strata!=Current){Current=E.Strata;bFirst=false;Lines.Add({FString::Printf(TEXT("<%s>"),StrataName(E.Strata)),Gold});}
  const bool bTarget=E.Handle==ClickTarget;
  FString Line=FString::Printf(TEXT("%s%3d  %s"),bTarget?TEXT("> "):TEXT("  "),E.Level,*E.Name);
  if(!E.Parent.IsEmpty())Line+=FString::Printf(TEXT("   (parent %s)"),*E.Parent);
  Line+=FString::Printf(TEXT("   %.0fx%.0f"),E.Size.X,E.Size.Y);
  if(E.bMouseEnabled)Line+=TEXT("   mouse");
  Lines.Add({Line,bTarget?Green:(E.bMouseEnabled?FLinearColor::White:Grey)});
  ++Shown;
 }
 if(NativeWidgets.Num())
 {
  Lines.Add({TEXT("<Native windows>"),Gold});
  for(const FString& N:NativeWidgets)Lines.Add({TEXT("      ")+N,Cyan});
 }
}

int32 SWowFrameStack::OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool)const
{
 if(!ManagerAlive.IsValid()||!FSlateApplication::IsInitialized())return Layer;
 const FSlateBrush* White=FCoreStyle::Get().GetBrush("WhiteBrush");
 auto Outline=[&](FVector2D Min,FVector2D Max,const FLinearColor& Color,float Thickness,int32 L)
 {
  TArray<FVector2f> P={FVector2f(Min),FVector2f(Max.X,Min.Y),FVector2f(Max),FVector2f(Min.X,Max.Y),FVector2f(Min)};
  FSlateDrawElement::MakeLines(Out,L,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Color,true,Thickness);
 };
 // Frame bounds: the click target bright green, the frame on top yellow, the rest faint.
 for(int32 I=Stack.Num()-1;I>=0;--I)
 {
  const auto& E=Stack[I];
  const FVector2D Min=G.AbsoluteToLocal(E.AbsMin),Max=G.AbsoluteToLocal(E.AbsMax);
  const bool bTarget=E.Handle==ClickTarget;
  Outline(Min,Max,bTarget?Green:(I==0?Gold:FLinearColor(.4f,.6f,1.f,.35f)),bTarget||I==0?2.f:1.f,Layer+1);
 }
 // Text panel next to the cursor, kept on screen.
 const FSlateFontInfo Font=FCoreStyle::GetDefaultFontStyle("Mono",9);
 const TSharedRef<FSlateFontMeasure> Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
 float Width=0,LineH=0;
 for(const auto& L:Lines){const FVector2D S=Measure->Measure(L.Text,Font);Width=FMath::Max(Width,float(S.X));LineH=FMath::Max(LineH,float(S.Y));}
 LineH=FMath::Max(LineH,12.f);
 const FVector2D Pad(8,6),Panel(Width+Pad.X*2,Lines.Num()*LineH+Pad.Y*2),Area=G.GetLocalSize();
 FVector2D Pos=G.AbsoluteToLocal(Cursor)+FVector2D(24,24);
 if(Pos.X+Panel.X>Area.X)Pos.X=FMath::Max(0.,G.AbsoluteToLocal(Cursor).X-24-Panel.X);
 if(Pos.Y+Panel.Y>Area.Y)Pos.Y=FMath::Max(0.,Area.Y-Panel.Y);
 FSlateDrawElement::MakeBox(Out,Layer+2,G.ToPaintGeometry(FVector2f(Panel),FSlateLayoutTransform(FVector2f(Pos))),White,ESlateDrawEffect::None,FLinearColor(0,0,0,.85f));
 Outline(Pos,Pos+Panel,bFrozen?Cyan:FLinearColor(.6f,.5f,.2f),1.f,Layer+3);
 for(int32 I=0;I<Lines.Num();++I)
 {
  const FVector2D P=Pos+Pad+FVector2D(0,I*LineH);
  FSlateDrawElement::MakeText(Out,Layer+3,G.ToPaintGeometry(FVector2f(Width,LineH),FSlateLayoutTransform(FVector2f(P))),Lines[I].Text,Font,ESlateDrawEffect::None,Lines[I].Color);
 }
 return Layer+3;
}
