#include "Widgets/SWowDestroyItemDialog.h"
#include "WowColor.h"
#include "WowWindowStyle.h"
#include "WowItemPresentation.h"
#include "WowInventoryManager.h"
#include "WowRetainedSlateBrush.h"
#include "WowConnectionManager.h"
#include "WowItemDestroyPolicy.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowDestroyItem, Log, All);

namespace
{
UTexture2D* LoadItemIcon(const TWeakObjectPtr<UWowUIManager>& UI, uint32 Entry, FString& InOutLoadedPath)
{
    if (!Entry || !UI.IsValid() || !UI->GetFrameManager()) return nullptr;
    const FString Path = FWowItemPresentation::IconPath(Entry);
    if (Path == InOutLoadedPath) return nullptr;
    UTexture2D* Texture = UI->GetFrameManager()->LoadUITexture(Path);
    if (Texture) InOutLoadedPath = Path;
    return Texture;
}

// The flat dialog look shared with the FrameXML StaticPopups (WowUIManager "FlatDialogs"): a dark
// rectangle with a thin gold line, flat red buttons (Stefan 11.10: "more flat, more rectangle shaped").
[[maybe_unused]] const FSlateBrush* FlatDialogBrush()
{
    static const FSlateRoundedBoxBrush Brush(WowColor::FromWow(.035f, .035f, .04f, .94f), 0.f,
        WowColor::FromWow(.62f, .52f, .32f), 1.f);
    return &Brush;
}

// r276 (Stefan 13.10: every dialog box like the exit game box): the client's UIPanelButtonTemplate
// red button, as in SWowSessionCountdown.
const FButtonStyle* FlatDialogButtonStyle()
{
    return FWowWindowStyle::Button();
}

[[maybe_unused]] const FButtonStyle* OldFlatDialogButtonStyle()
{
    static FButtonStyle Style = []()
    {
        FButtonStyle S = *FWowWindowStyle::Button();
        const FLinearColor Line = WowColor::FromWow(.62f, .52f, .32f);
        S.SetNormal(FSlateRoundedBoxBrush(WowColor::FromWow(.30f, .035f, .012f), 0.f, Line, 1.f));
        S.SetHovered(FSlateRoundedBoxBrush(WowColor::FromWow(.40f, .06f, .03f), 0.f, WowColor::FromWow(.9f, .75f, .45f), 1.f));
        S.SetPressed(FSlateRoundedBoxBrush(WowColor::FromWow(.17f, .02f, .008f), 0.f, Line, 1.f));
        S.SetDisabled(FSlateRoundedBoxBrush(WowColor::FromWow(.10f, .10f, .10f), 0.f, WowColor::FromWow(.3f, .3f, .3f), 1.f));
        S.SetNormalPadding(FMargin(8, 3));
        S.SetPressedPadding(FMargin(8, 4, 8, 2));
        return S;
    }();
    return &Style;
}
}

void SWowDestroyItemDialog::Construct(const FArguments& InArgs)
{
    ItemName = InArgs._ItemName.IsEmpty() ? TEXT("this item") : InArgs._ItemName;
    ItemEntry = InArgs._ItemEntry;
    Quality = InArgs._Quality;
    Count = FMath::Max(1, InArgs._Count);
    bRequireTypedDelete = InArgs._bRequireTypedDelete;
    ConnectionManager = InArgs._ConnectionManager;
    UIManager = InArgs._UIManager;
    IsStillValid = InArgs._IsStillValid;
    OnConfirmed = InArgs._OnConfirmed;
    OnCancelled = InArgs._OnCancelled;

    IconBrush = MakeShared<FWowRetainedSlateBrush>();
    IconBrush->DrawAs = ESlateBrushDrawType::Image;
    IconBrush->ImageSize = FVector2D(36.0f);

    const FLinearColor QualityColor = FWowInventoryManager::GetQualityColor(static_cast<uint8>(Quality));
    const FString CountSuffix = Count > 1 ? FString::Printf(TEXT(" x%d"), Count) : FString();

    TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 8)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Do you want to destroy")))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
            .ColorAndOpacity(WowColor::FromWow(1.0f, 0.82f, 0.1f))
        ]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(QualityColor)
                .Padding(1)
                [
                    SNew(SBox).WidthOverride(36).HeightOverride(36)
                    [ SNew(SImage).Image(IconBrush.Get()) ]
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(FString::Printf(TEXT("[%s]%s?"), *ItemName, *CountSuffix)))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                .ColorAndOpacity(QualityColor)
            ]
        ];

    if (bRequireTypedDelete)
    {
        Body->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 6)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Type \"DELETE\" into the field to confirm.")))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
            .ColorAndOpacity(WowColor::FromWow(1.0f, 0.82f, 0.1f))
        ];
        Body->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)
        [
            SNew(SBox).WidthOverride(170)
            [
                SAssignNew(DeleteTextBox, SEditableTextBox)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                .HintText(FText::FromString(TEXT("DELETE")))
                .SelectAllTextWhenFocused(true)
                .ClearKeyboardFocusOnCommit(false)
                .OnTextChanged_Lambda([this](const FText& Text) { TypedText = Text.ToString(); })
                .OnTextCommitted(this, &SWowDestroyItemDialog::OnTextCommitted)
            ]
        ];
    }

    Body->AddSlot().AutoHeight().HAlign(HAlign_Center)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
        [
            SNew(SBox).WidthOverride(110)
            [
                SNew(SButton).ButtonStyle(FlatDialogButtonStyle()).HAlign(HAlign_Center)
                .Text(FText::FromString(TEXT("Yes")))
                .IsEnabled_Lambda([this]() { return CanConfirm(); })
                .OnClicked(this, &SWowDestroyItemDialog::OnYes)
            ]
        ]
        + SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
        [
            SNew(SBox).WidthOverride(110)
            [
                SNew(SButton).ButtonStyle(FlatDialogButtonStyle()).HAlign(HAlign_Center)
                .Text(FText::FromString(TEXT("No")))
                .OnClicked(this, &SWowDestroyItemDialog::OnNo)
            ]
        ]
    ];

    ChildSlot
    [
        SNew(SBox).MinDesiredWidth(340)
        [
            // r276: the dialog frame (UI-DialogBox background and border) like the exit/logout box.
            SNew(SWowWindowFrame)
            .Padding(FMargin(20, 14))
            [ Body ]
        ]
    ];
    UpdateIcon();
}

void SWowDestroyItemDialog::FocusInput()
{
    if (!FSlateApplication::IsInitialized()) return;
    if (DeleteTextBox.IsValid())
        FSlateApplication::Get().SetAllUserFocus(DeleteTextBox, EFocusCause::SetDirectly);
    else
        FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::SetDirectly);
}

void SWowDestroyItemDialog::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    if (bFinished) return;
    if (IsStillValid.IsBound() && !IsStillValid.Execute())
    {
        UE_LOG(LogWowDestroyItem, Log, TEXT("Destroy dialog for %s closed: item no longer on cursor"), *ItemName);
        Finish(false);
        return;
    }
    if (LoadedIconPath.IsEmpty()) UpdateIcon(); // icon data may arrive after the dialog opens
}

FReply SWowDestroyItemDialog::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
    if (KeyEvent.GetKey() == EKeys::Escape) { Cancel(); return FReply::Handled(); }
    if (KeyEvent.GetKey() == EKeys::Enter && bRequireTypedDelete)
    {
        if (CanConfirm()) Confirm();
        return FReply::Handled();
    }
    // Swallow everything else so gameplay bindings never fire while the dialog is up.
    return FReply::Handled();
}

bool SWowDestroyItemDialog::CanConfirm() const
{
    return !bFinished && (!bRequireTypedDelete || FWowItemDestroyPolicy::IsConfirmText(TypedText));
}

void SWowDestroyItemDialog::OnTextCommitted(const FText& Text, ETextCommit::Type CommitType)
{
    TypedText = Text.ToString();
    if (CommitType == ETextCommit::OnEnter && CanConfirm()) Confirm();
}

FReply SWowDestroyItemDialog::OnYes()
{
    if (CanConfirm()) Confirm();
    return FReply::Handled();
}

FReply SWowDestroyItemDialog::OnNo()
{
    Cancel();
    return FReply::Handled();
}

void SWowDestroyItemDialog::Confirm()
{
    UE_LOG(LogWowDestroyItem, Log, TEXT("Destroy confirmed: %s x%d (quality %u, typed=%d)"),
        *ItemName, Count, Quality, bRequireTypedDelete ? 1 : 0);
    Finish(true);
}

void SWowDestroyItemDialog::Cancel()
{
    Finish(false);
}

void SWowDestroyItemDialog::Finish(bool bConfirmed)
{
    if (bFinished) return;
    bFinished = true;
    // Copy: the owner usually removes (and releases) this widget from the callback.
    const FSimpleDelegate Callback = bConfirmed ? OnConfirmed : OnCancelled;
    Callback.ExecuteIfBound();
}

void SWowDestroyItemDialog::UpdateIcon()
{
    if (UTexture2D* Texture = LoadItemIcon(UIManager, ItemEntry, LoadedIconPath))
        IconBrush->SetResourceObject(Texture);
}

// ---------------------------------------------------------------------------

void SWowCursorItemIcon::Construct(const FArguments& InArgs)
{
    ConnectionManager = InArgs._ConnectionManager;
    UIManager = InArgs._UIManager;
    GetItemEntry = InArgs._GetItemEntry;
    Brush = MakeShared<FWowRetainedSlateBrush>();
    Brush->DrawAs = ESlateBrushDrawType::Image;
    Brush->ImageSize = FVector2D(30.0f);
    SetVisibility(EVisibility::HitTestInvisible);
    ChildSlot
    .HAlign(HAlign_Left)
    .VAlign(VAlign_Top)
    [
        SAssignNew(Icon, SImage)
        .Image(Brush.Get())
        .Visibility(EVisibility::Collapsed)
        .RenderTransformPivot(FVector2D::ZeroVector)
    ];
}

bool SWowCursorItemIcon::IsShowingIcon() const
{
    return Icon.IsValid() && Icon->GetVisibility() == EVisibility::HitTestInvisible;
}

void SWowCursorItemIcon::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    const uint32 Entry = GetItemEntry.IsBound() ? GetItemEntry.Execute() : 0;
    if (Entry != ShownEntry) { ShownEntry = Entry; LoadedIconPath.Reset(); }
    if (Entry && LoadedIconPath.IsEmpty())
        if (UTexture2D* Texture = LoadItemIcon(UIManager, Entry, LoadedIconPath))
            Brush->SetResourceObject(Texture);
    const bool bShow = Entry != 0 && !LoadedIconPath.IsEmpty() && FSlateApplication::IsInitialized();
    if (!bShow)
    {
        if (Icon->GetVisibility() != EVisibility::Collapsed) Icon->SetVisibility(EVisibility::Collapsed);
        return;
    }
    // The held item is the pointer (the controller hides the normal cursor): its
    // top-left corner sits on the hotspot, like the original client.
    const FVector2D Local = Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
    Icon->SetRenderTransform(FSlateRenderTransform(Local - FVector2D(2.0f, 2.0f)));
    if (Icon->GetVisibility() != EVisibility::HitTestInvisible) Icon->SetVisibility(EVisibility::HitTestInvisible);
}
