#pragma once
#include "WowWindowStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

// The live dialog and offscreen UI preview use the same compact layout.
class SWowSessionCountdown : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowSessionCountdown) : _ShowExit(false) {}
        SLATE_ARGUMENT(TSharedPtr<STextBlock>, Message)
        SLATE_ATTRIBUTE(bool, ShowExit)
        SLATE_EVENT(FOnClicked, OnExit)
        SLATE_EVENT(FOnClicked, OnCancel)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        ChildSlot
        [ SNew(SBox).WidthOverride(360)
          [ SNew(SWowWindowFrame).Padding(18)
            [ SNew(SVerticalBox)
              + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,14).HAlign(HAlign_Center)
              [ Args._Message.ToSharedRef() ]
              + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
              [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SBox).Padding(0,0,10,0)
                  .Visibility_Lambda([Show=Args._ShowExit](){return Show.Get()?EVisibility::Visible:EVisibility::Collapsed;})
                  [ Button(TEXT("Exit Game"),Args._OnExit) ] ]
                + SHorizontalBox::Slot().AutoWidth()
                [ Button(TEXT("Cancel"),Args._OnCancel) ] ] ] ] ];
    }
private:
    static TSharedRef<SWidget> Button(const TCHAR* Label, FOnClicked Click)
    {
        return SNew(SBox).WidthOverride(140).HeightOverride(30)
            [ SNew(SButton).ButtonStyle(FWowWindowStyle::Button()).ContentPadding(0).HAlign(HAlign_Center).VAlign(VAlign_Center)
              .ClickMethod(EButtonClickMethod::MouseDown) // a press must not be lost to camera/world input
              .OnClicked(Click)
              [ SNew(STextBlock).Text(FText::FromString(Label)).Font(FWowWindowStyle::TitleFont(12))
                .ColorAndOpacity(FWowWindowStyle::Gold()) ] ];
    }
};

