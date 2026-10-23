#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "WowKeyBindings.h"
class SWowKeyBindings : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowKeyBindings){} SLATE_EVENT(FSimpleDelegate,OnClose) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void OpenSession();
    FReply Cancel(){return Close(false);}
    virtual ~SWowKeyBindings() override;
    virtual bool SupportsKeyboardFocus() const override {return true;}
    virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent&) override;
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&) override;
    virtual FReply OnMouseWheel(const FGeometry&,const FPointerEvent&) override;
private:
    friend class FWowBindingCaptureInput;
    TSharedPtr<class IInputProcessor> CaptureInput;
    TSharedPtr<class SScrollBox> List;
    FString Filter, CaptureAction, Message, PendingKey;
    int32 CaptureSlot=0;
    bool bCommitted=false;
    FWowBindingSnapshot Original;
    FSimpleDelegate OnClose;
    void Refresh();
    FReply Close(bool Save);
    FReply ApplyKey(const FString& Key);
    FReply Capture(const FKey& Key,bool Ctrl,bool Shift,bool Alt);
};
