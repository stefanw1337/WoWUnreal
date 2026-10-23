#include "WowGameUI.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowFrameLayout.h"
#include "Widgets/SWowLootWindow.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "UnrealClient.h"

class FWowFrameMoveInput : public IInputProcessor
{
    TWeakObjectPtr<UWowGameUI> Owner;
public:
    explicit FWowFrameMoveInput(UWowGameUI* InOwner):Owner(InOwner){}
    virtual void Tick(float,FSlateApplication& App,TSharedRef<ICursor>) override
    {
        if(Owner.IsValid()&&Owner->IsMovingFrame()&&(!App.IsActive()||!App.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton)))
            Owner->EndFrameMove();
    }
    // A Ctrl press only becomes a frame move once the mouse travels a few pixels; a plain
    // Ctrl-click goes through to the UI (the objectives tracker's Ctrl-click diamond was swallowed
    // because the tracker is movable - Stefan, quest diamonds never appeared).
    bool bPendingMove=false;
    FVector2D PendingAt=FVector2D::ZeroVector;
    virtual bool HandleMouseButtonDownEvent(FSlateApplication&,const FPointerEvent& E) override
    {
        if(Owner.IsValid()&&E.GetEffectingButton()==EKeys::LeftMouseButton&&E.IsControlDown()&&!E.IsMouseButtonDown(EKeys::RightMouseButton))
        {bPendingMove=true;PendingAt=E.GetScreenSpacePosition();}
        return false;
    }
    virtual bool HandleMouseMoveEvent(FSlateApplication&,const FPointerEvent& E) override
    {
        if(Owner.IsValid()&&bPendingMove&&!Owner->IsMovingFrame())
        {
            if(!E.IsMouseButtonDown(EKeys::LeftMouseButton)||!E.IsControlDown()){bPendingMove=false;return false;}
            if(FVector2D::Distance(E.GetScreenSpacePosition(),PendingAt)<5.0)return false;
            bPendingMove=false;
            if(Owner->BeginFrameMove(PendingAt)){Owner->UpdateFrameMove(E.GetScreenSpacePosition());return true;}
            return false;
        }
        if(!Owner.IsValid()||!Owner->IsMovingFrame())return false;
        // Releasing Ctrl locks the position immediately; swallow the remaining
        // left-button gesture so it cannot turn into an item click/camera drag.
        if(E.IsControlDown())Owner->UpdateFrameMove(E.GetScreenSpacePosition());
        return true;
    }
    virtual bool HandleMouseButtonUpEvent(FSlateApplication&,const FPointerEvent& E) override
    {
        if(E.GetEffectingButton()==EKeys::LeftMouseButton)bPendingMove=false;
        if(!Owner.IsValid()||!Owner->IsMovingFrame()||E.GetEffectingButton()!=EKeys::LeftMouseButton)return false;
        Owner->EndFrameMove();return true;
    }
};

FWowFrameManager* UWowGameUI::GetFrameManager() const
{
    auto* GI=GetWorld()?GetWorld()->GetGameInstance():nullptr;
    auto* UI=GI?GI->GetSubsystem<UWowUIManager>():nullptr;
    return UI?UI->GetFrameManager():nullptr;
}
void UWowGameUI::RegisterFrameMoveInput()
{
    if(!FrameMoveInput.IsValid()&&FSlateApplication::IsInitialized())
    {FrameMoveInput=MakeShared<FWowFrameMoveInput>(this);FSlateApplication::Get().RegisterInputPreProcessor(FrameMoveInput);}
}
void UWowGameUI::UnregisterFrameMoveInput()
{
    EndFrameMove();
    if(FrameMoveInput.IsValid()&&FSlateApplication::IsInitialized())FSlateApplication::Get().UnregisterInputPreProcessor(FrameMoveInput);
    FrameMoveInput.Reset();
}
bool UWowGameUI::BeginFrameMove(const FVector2D& Screen)
{
    if(bFrameMoveOwned||!CanMoveFrames||!CanMoveFrames()||!GEngine||!GEngine->GameViewport||!GEngine->GameViewport->Viewport)
    {
        // Diagnostic for "Ctrl-drag moves nothing" (test log 08.10).
        UE_LOG(LogTemp,Log,TEXT("Ctrl-drag: not allowed now (owned=%d canMove=%d)"),bFrameMoveOwned?1:0,CanMoveFrames&&CanMoveFrames()?1:0);
        return false;
    }
    auto V=GEngine->GameViewport->GetGameViewportWidget();if(!V.IsValid())return false;
    const FGeometry& VG=V->GetCachedGeometry();const FVector2D View=VG.GetLocalSize();
    if(View.X<=0||View.Y<=0||!VG.IsUnderLocation(Screen))return false;
    FGeometry Geometry;
    // Reverse layout order follows the visible native panels' front-to-back order.
    for(int32 I=PanelOrder.Num()-1;I>=0;--I)
        if(auto* Panel=ManagedPanels.Find(PanelOrder[I]))
            if(auto W=Panel->Widget.Pin())
                if(W->GetVisibility().IsVisible()&&W->GetCachedGeometry().IsUnderLocation(Screen))
                {MovingFrameName=PanelOrder[I].ToString();Geometry=W->GetCachedGeometry();break;}
    if(MovingFrameName.IsEmpty()&&LootWindow.IsValid()&&LootWindow->GetVisibility().IsVisible()&&LootWindow->GetCachedGeometry().IsUnderLocation(Screen))
    {MovingFrameName=TEXT("NativeLoot");Geometry=LootWindow->GetCachedGeometry();}
    bMovingNativeWidget=false;
    if(MovingFrameName.IsEmpty())
        for(const auto& Pair:MovableWidgets)
            if(auto W=Pair.Value.Pin())
                if(W->GetVisibility().IsVisible()&&W->GetCachedGeometry().IsUnderLocation(Screen))
                {
                    MovingFrameName=Pair.Key.ToString();Geometry=W->GetCachedGeometry();bMovingNativeWidget=true;
                    MoveStartTranslation=W->GetRenderTransform().IsSet()?FVector2D(W->GetRenderTransform()->GetTranslation()):FVector2D::ZeroVector;
                    break;
                }
    if(MovingFrameName.IsEmpty())
    {
        auto* Frames=GetFrameManager();if(!Frames)return false;
        const FVector2D Local=VG.AbsoluteToLocal(Screen);const auto Pixels=GEngine->GameViewport->Viewport->GetSizeXY();
        const int64 H=Frames->GetMovableFrameAtScreenPosition(Local.X/View.X*Pixels.X,Local.Y/View.Y*Pixels.Y);
        auto* Widget=Frames->GetWidgetForHandle(H);
        if(!Widget)
        {
            const auto Stack=Frames->GetFramesAtScreenPosition(Local.X/View.X*Pixels.X,Local.Y/View.Y*Pixels.Y);
            UE_LOG(LogTemp,Log,TEXT("Ctrl-drag: no movable frame at (%.0f,%.0f); %d frames there, top '%s'"),
                Local.X,Local.Y,Stack.Num(),Stack.Num()?*Frames->GetFrameName(Stack[0]):TEXT("-"));
            return false;
        }
        MovingFrameName=Frames->GetFrameName(H);Geometry=Widget->GetCachedGeometry();
    }
    if(Geometry.GetLocalSize().X<=0||Geometry.GetLocalSize().Y<=0){MovingFrameName.Reset();return false;}
    MoveStartCursor=VG.AbsoluteToLocal(Screen);
    MoveStartPosition=VG.AbsoluteToLocal(Geometry.LocalToAbsolute(FVector2D::ZeroVector));
    MoveSize=VG.AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize()))-MoveStartPosition;
    bFrameMoveOwned=true;
    UE_LOG(LogTemp,Log,TEXT("Ctrl-drag: moving '%s'"),*MovingFrameName);
    return true;
}
void UWowGameUI::UpdateFrameMove(const FVector2D& Screen)
{
    if(!bFrameMoveOwned||!GEngine||!GEngine->GameViewport)return;
    if(!CanMoveFrames||!CanMoveFrames()){EndFrameMove();return;}
    auto V=GEngine->GameViewport->GetGameViewportWidget();if(!V.IsValid())return;
    const auto& G=V->GetCachedGeometry();const FVector2D View=G.GetLocalSize();if(View.X<=0||View.Y<=0)return;
    if(bMovingNativeWidget)
    {
        // Native element: offset from its own layout place, stored in [0,1] as 0.5 + offset/view/2.
        const FVector2D Offset=MoveStartTranslation+G.AbsoluteToLocal(Screen)-MoveStartCursor;
        FWowFrameLayout::Set(MovingFrameName,FVector2D(.5,.5)+Offset/View*.5);
        ApplyMovableWidgets();
        return;
    }
    const FVector2D Desired=MoveStartPosition+G.AbsoluteToLocal(Screen)-MoveStartCursor;
    const FVector2D Position=FWowFrameLayout::ClampPosition(Desired/View,MoveSize,View);
    if(MovingFrameName==TEXT("NativeLoot")&&bLootAtMouse) LootWindowPosition=Position;
    else FWowFrameLayout::Set(MovingFrameName,Position/View);
    ReflowPanels();if(auto* Frames=GetFrameManager())Frames->UpdateUserFrameLayouts();
}
void UWowGameUI::AddMovableWidget(FName Name,TSharedRef<SWidget> Widget)
{
    MovableWidgets.Add(Name,Widget);
    ApplyMovableWidgets();
}
void UWowGameUI::ApplyMovableWidgets()
{
    if(!GEngine||!GEngine->GameViewport)return;
    auto V=GEngine->GameViewport->GetGameViewportWidget();if(!V.IsValid())return;
    const FVector2D View=V->GetCachedGeometry().GetLocalSize();if(View.X<=0||View.Y<=0)return;
    const auto& Saved=FWowFrameLayout::Positions();
    for(const auto& Pair:MovableWidgets)
        if(auto W=Pair.Value.Pin())
        {
            const FVector2D* Stored=Saved.Find(Pair.Key.ToString());
            const FVector2D Offset=Stored?(*Stored-FVector2D(.5,.5))*2.*View:FVector2D::ZeroVector;
            W->SetRenderTransform(FSlateRenderTransform(FVector2f(Offset)));
        }
}
void UWowGameUI::EndFrameMove()
{
    if(bFrameMoveOwned)FWowFrameLayout::Save();
    bFrameMoveOwned=false;MovingFrameName.Reset();
}
