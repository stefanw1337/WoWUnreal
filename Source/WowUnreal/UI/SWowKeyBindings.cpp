#include "SWowKeyBindings.h"
#include "WowColor.h"
#include "WowWindowStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"

// While a field is listening, the scroll box must not consume a wheel binding.
class FWowBindingCaptureInput : public IInputProcessor
{
public:
    TWeakPtr<SWowKeyBindings> Owner;
    explicit FWowBindingCaptureInput(TSharedRef<SWowKeyBindings> Widget):Owner(Widget){}
    virtual void Tick(float,FSlateApplication&,TSharedRef<ICursor>) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& Event) override
    {auto W=Owner.Pin();return W && !W->bCommitted && !W->CaptureAction.IsEmpty() && W->OnKeyDown(FGeometry(),Event).IsEventHandled();}
    virtual bool HandleMouseButtonDownEvent(FSlateApplication&,const FPointerEvent& Event) override
    {
        if(Event.GetEffectingButton()==EKeys::LeftMouseButton || Event.GetEffectingButton()==EKeys::RightMouseButton)return false;
        auto W=Owner.Pin();return W && !W->bCommitted && !W->CaptureAction.IsEmpty() && W->OnMouseButtonDown(FGeometry(),Event).IsEventHandled();
    }
    virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication&,const FPointerEvent& Event,const FPointerEvent*) override
    {auto W=Owner.Pin();return W && !W->bCommitted && !W->CaptureAction.IsEmpty() && W->OnMouseWheel(FGeometry(),Event).IsEventHandled();}
};

void SWowKeyBindings::Construct(const FArguments& Args)
{
    OnClose=Args._OnClose;Original=FWowKeyBindings::Snapshot();
    CaptureInput=MakeShared<FWowBindingCaptureInput>(SharedThis(this));
    FSlateApplication::Get().RegisterInputPreProcessor(CaptureInput,0);
    ChildSlot[SNew(SBox).WidthOverride(780).HeightOverride(680)
    [SNew(SWowWindowFrame)
      .Padding(18)
      [SNew(SVerticalBox)
       + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
       [SNew(STextBlock).Text(FText::FromString(TEXT("Key Bindings"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",22)).ColorAndOpacity(WowColor::FromWow(1, .82f, .1f))]
       + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
       [SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
        [SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).Text_Lambda([](){return FText::FromString(FWowKeyBindings::CurrentPreset()==1?TEXT("[Preset 1 - Standard]"):TEXT("Preset 1 - Standard"));})
          .OnClicked_Lambda([this](){CaptureAction.Empty();PendingKey.Empty();FWowKeyBindings::UsePreset(1);Refresh();return FReply::Handled();})]
        + SHorizontalBox::Slot().AutoWidth()
        [SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).Text_Lambda([](){return FText::FromString(FWowKeyBindings::CurrentPreset()==2?TEXT("[Preset 2 - My bindings]"):TEXT("Preset 2 - My bindings"));})
          .OnClicked_Lambda([this](){CaptureAction.Empty();PendingKey.Empty();FWowKeyBindings::UsePreset(2);Refresh();return FReply::Handled();})]
        + SHorizontalBox::Slot().FillWidth(1)
        + SHorizontalBox::Slot().AutoWidth()
        [SNew(SCheckBox).IsEnabled_Lambda([](){return FWowKeyBindings::CanUseCharacterProfile();})
          .IsChecked_Lambda([](){return FWowKeyBindings::IsCharacterSpecific()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
          .OnCheckStateChanged_Lambda([this](ECheckBoxState S){CaptureAction.Empty();PendingKey.Empty();FWowKeyBindings::SetCharacterSpecific(S==ECheckBoxState::Checked);Refresh();})
          [SNew(STextBlock).Text(FText::FromString(TEXT("Character specific keybindings")))]]]
       + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
       [SNew(SSearchBox).HintText(FText::FromString(TEXT("Find an action..."))).OnTextChanged_Lambda([this](const FText& Text){Filter=Text.ToString();Refresh();})]
       + SVerticalBox::Slot().AutoHeight()
       [SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(TEXT("Command")))]
        + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(161)[SNew(STextBlock).Justification(ETextJustify::Center).Text(FText::FromString(TEXT("Key 1")))]]
        + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(161)[SNew(STextBlock).Justification(ETextJustify::Center).Text(FText::FromString(TEXT("Key 2")))]]]
       + SVerticalBox::Slot().FillHeight(1)[SAssignNew(List,SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::Always)]
       + SVerticalBox::Slot().AutoHeight().Padding(0,10)
       [SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){return FText::FromString(!Message.IsEmpty()?Message:CaptureAction.IsEmpty()?TEXT("Select a field to change its key. Not Bound is allowed. Okay saves both presets; Cancel discards changes."):TEXT("Press a key or mouse button (3-5), or scroll the wheel. Escape cancels capture."));})]
       + SVerticalBox::Slot().AutoHeight()
       [SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).Text(FText::FromString(TEXT("Reset preset"))).ToolTipText(FText::FromString(TEXT("Restore only the selected preset: standard keys for preset 1, original screenshot keys for preset 2.")))
          .OnClicked_Lambda([this](){CaptureAction.Empty();PendingKey.Empty();FWowKeyBindings::ResetPreset();Message=TEXT("Selected preset reset. Press Okay to save or Cancel to undo.");Refresh();return FReply::Handled();})]
        + SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).Text(FText::FromString(TEXT("Unbind key"))).IsEnabled_Lambda([this](){return !CaptureAction.IsEmpty();})
          .OnClicked_Lambda([this](){FWowKeyBindings::Unbind(CaptureAction,CaptureSlot);CaptureAction.Empty();PendingKey.Empty();Message.Empty();return FReply::Handled();})]
        + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).Text(FText::FromString(TEXT("Reassign key"))).Visibility_Lambda([this](){return PendingKey.IsEmpty()?EVisibility::Collapsed:EVisibility::Visible;})
          .OnClicked_Lambda([this](){FWowKeyBindings::Bind(PendingKey,CaptureAction,CaptureSlot);PendingKey.Empty();CaptureAction.Empty();Message.Empty();return FReply::Handled();})]
        + SHorizontalBox::Slot().FillWidth(1)
        + SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).Text(FText::FromString(TEXT("Okay"))).OnClicked_Lambda([this](){return Close(true);})]
        + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).Text(FText::FromString(TEXT("Cancel"))).OnClicked_Lambda([this](){return Close(false);})]
       ]]]];
    Refresh();
}
void SWowKeyBindings::OpenSession(){Original=FWowKeyBindings::Snapshot();bCommitted=false;CaptureAction.Empty();PendingKey.Empty();Message.Empty();Refresh();}
SWowKeyBindings::~SWowKeyBindings(){if(FSlateApplication::IsInitialized())FSlateApplication::Get().UnregisterInputPreProcessor(CaptureInput);if(!bCommitted)FWowKeyBindings::Restore(Original);}
FReply SWowKeyBindings::Close(bool Save)
{
    if(Save)FWowKeyBindings::Save();else FWowKeyBindings::Restore(Original);
    bCommitted=true;CaptureAction.Empty();PendingKey.Empty();OnClose.ExecuteIfBound();return FReply::Handled();
}
void SWowKeyBindings::Refresh()
{
    if(!List)return;const float Offset=List->GetScrollOffset();List->ClearChildren();FString Category;
    // r385 (Claude-Perf r620: the isometric camera commands are added after the catalog and showed as a
    // second "Camera Functions" list at the bottom): each category once, in the order it first appears.
    TArray<FString> CategoryOrder;
    for(const auto& Ordered:FWowKeyBindings::Actions())CategoryOrder.AddUnique(Ordered.Category);
    for(const FString& WantedCategory:CategoryOrder)for(const auto& Action:FWowKeyBindings::Actions())
    {
        if(Action.Category!=WantedCategory)continue;
        if(!Filter.IsEmpty() && !Action.Label.Contains(Filter) && !Action.Category.Contains(Filter))continue;
        if(Category!=Action.Category)
        {Category=Action.Category;List->AddSlot().Padding(0,12,0,4)[SNew(STextBlock).Text(FText::FromString(Category)).ColorAndOpacity(WowColor::FromWow(1, .82f, .1f))];}
        auto Row=SNew(SHorizontalBox);
        Row->AddSlot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Action.Label))];
        for(int32 Slot=0;Slot<2;++Slot)
        {
            const FString Command=Action.Command;
            Row->AddSlot().AutoWidth().Padding(3,0)
            [SNew(SBox).WidthOverride(155)[SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).HAlign(HAlign_Center).HAlign(HAlign_Center)
             .Text_Lambda([this,Command,Slot](){const auto Keys=FWowKeyBindings::Keys(Command);return FText::FromString(CaptureAction==Command && CaptureSlot==Slot?TEXT("Press a key..."):Keys.IsValidIndex(Slot) && !Keys[Slot].IsEmpty()?Keys[Slot]:TEXT("Not Bound"));})
             .OnClicked_Lambda([this,Command,Slot](){CaptureAction=Command;CaptureSlot=Slot;PendingKey.Empty();Message.Empty();return FReply::Handled().SetUserFocus(SharedThis(this));})]];
        }
        List->AddSlot().Padding(0,3)[Row];
    }
    List->SetScrollOffset(Offset);
}
FReply SWowKeyBindings::ApplyKey(const FString& Key)
{
    const FString Owner=FWowKeyBindings::Command(Key);
    if(!Owner.IsEmpty() && Owner!=CaptureAction)
    {
        FString Label=Owner;
        for(const auto& A:FWowKeyBindings::Actions())if(A.Command==Owner){Label=A.Label;break;}
        PendingKey=Key;Message=FString::Printf(TEXT("%s is used by %s. Choose Reassign key to move it, or press another key."),*Key,*Label);return FReply::Handled();
    }
    FWowKeyBindings::Bind(Key,CaptureAction,CaptureSlot);CaptureAction.Empty();PendingKey.Empty();Message.Empty();return FReply::Handled();
}
FReply SWowKeyBindings::Capture(const FKey& Key,bool Ctrl,bool Shift,bool Alt)
{
    if(CaptureAction.IsEmpty())return FReply::Unhandled();
    if(Key==EKeys::Escape){CaptureAction.Empty();PendingKey.Empty();Message.Empty();return FReply::Handled();}
    if(Key.IsModifierKey())return FReply::Handled();
    if(Key==EKeys::LeftMouseButton || Key==EKeys::RightMouseButton)
    {Message=TEXT("Left and right mouse buttons are used for selection and camera control. Use another key.");return FReply::Handled();}
    return ApplyKey(FWowKeyBindings::KeyName(Key.GetFName().ToString(),Ctrl,Shift,Alt));
}
FReply SWowKeyBindings::OnKeyDown(const FGeometry&,const FKeyEvent& Event)
{
    if(Event.IsRepeat())return FReply::Handled();
    if(CaptureAction.IsEmpty() && Event.GetKey()==EKeys::Escape)return Close(false);
    return Capture(Event.GetKey(),Event.IsControlDown(),Event.IsShiftDown(),Event.IsAltDown());
}
FReply SWowKeyBindings::OnMouseButtonDown(const FGeometry&,const FPointerEvent& Event)
{return Capture(Event.GetEffectingButton(),Event.IsControlDown(),Event.IsShiftDown(),Event.IsAltDown());}
FReply SWowKeyBindings::OnMouseWheel(const FGeometry&,const FPointerEvent& Event)
{
    if(CaptureAction.IsEmpty())return FReply::Unhandled();
    return ApplyKey(FWowKeyBindings::KeyName(Event.GetWheelDelta()>0?TEXT("MOUSEWHEELUP"):TEXT("MOUSEWHEELDOWN"),Event.IsControlDown(),Event.IsShiftDown(),Event.IsAltDown()));
}
