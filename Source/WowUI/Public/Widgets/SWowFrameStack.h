#pragma once
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "WowFrameManager.h"

/**
 * /fstack overlay (like the 3.3.5 frame stack tooltip): lists every FrameXML frame
 * under the cursor grouped by strata, highest first, with frame level, parent, size
 * and whether it takes mouse input. The frame that receives clicks is marked ">".
 * Also lists the native Slate widgets under the cursor (our C++ windows).
 *  - Hold Alt to freeze the list so the cursor can move away.
 *  - Ctrl+C copies the list to the clipboard and writes it to the log.
 */
class SWowFrameStack:public SLeafWidget
{
public:
 SLATE_BEGIN_ARGS(SWowFrameStack){}
  SLATE_ARGUMENT(FWowFrameManager*,Manager)
  SLATE_ARGUMENT(TWeakPtr<int32>,ManagerAlive)
 SLATE_END_ARGS()
 void Construct(const FArguments& Args);
 virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D::ZeroVector;}
 virtual void Tick(const FGeometry& Geometry,double CurrentTime,float DeltaTime)override;
 virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool)const override;
private:
 struct FLine{FString Text;FLinearColor Color;};
 void Rebuild();
 FWowFrameManager* Manager=nullptr;TWeakPtr<int32> ManagerAlive;
 FVector2D Cursor=FVector2D::ZeroVector;
 TArray<FWowFrameManager::FFrameStackEntry> Stack;int64 ClickTarget=-1;
 TArray<FString> NativeWidgets;TArray<FLine> Lines;
 bool bFrozen=false,bCopyDown=false;
};
