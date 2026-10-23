#include "SWowChatBubbles.h"
#include "WowBackdropWidget.h"
#include "WowWindowStyle.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "Engine/Texture2D.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
// Bubbles stay up longer for longer lines, like the original client.
double BubbleSeconds(const FString& Text) { return FMath::Clamp(4.0 + Text.Len() * 0.06, 5.0, 12.0); }
}

void SWowChatBubbles::Construct(const FArguments& InArgs, FAnchorFunction InAnchor)
{
    Anchor = MoveTemp(InAnchor);
    UIManager = InArgs._UIManager;
    SetVisibility(EVisibility::HitTestInvisible);
    ChildSlot[ SAssignNew(Canvas, SConstraintCanvas) ];
}

float SWowChatBubbles::UIScale() const
{
    const float Scale = UIManager.IsValid() && UIManager->GetFrameManager() ? UIManager->GetFrameManager()->GetUIScale() : 1.f;
    return Scale > 0.f ? Scale : 1.f;
}

UTexture2D* SWowChatBubbles::Texture(const TCHAR* Path) const
{
    if (const TStrongObjectPtr<UTexture2D>* Held = HeldTextures.Find(Path); Held && Held->IsValid()) return Held->Get();
    UTexture2D* Loaded = UIManager.IsValid() && UIManager->GetFrameManager() ? UIManager->GetFrameManager()->LoadUITexture(Path) : nullptr;
    if (Loaded) HeldTextures.Add(Path, TStrongObjectPtr<UTexture2D>(Loaded));
    return Loaded;
}

TSharedRef<SWidget> SWowChatBubbles::BuildBubble(const FString& Text, const FLinearColor& Color, uint64 Guid)
{
    const float Scale = UIScale();
    // ChatBubble backdrop: stretched background inside a 16 px ChatBubble-Backdrop border.
    TSharedRef<SWowBackdrop> Backdrop = SNew(SWowBackdrop);
    Backdrop->SetBackdrop(Texture(TEXT("Interface\\Tooltips\\ChatBubble-Background")),
        Texture(TEXT("Interface\\Tooltips\\ChatBubble-Backdrop")), 16.f * Scale, false, 0.f, FMargin(6.f * Scale));
    if (UTexture2D* Tail = Texture(TEXT("Interface\\Tooltips\\ChatBubble-Tail")))
    {
        TailBrush.SetResourceObject(Tail);
        TailBrush.DrawAs = ESlateBrushDrawType::Image;
        TailBrush.ImageSize = FVector2D(16.f, 16.f) * Scale;
    }

    FSlateFontInfo Font = FWowWindowStyle::TitleFont(FMath::Max(7, FMath::RoundToInt(13.f * Scale * .75f))); // ChatBubbleFont
    return SNew(SBox)
        .Visibility_Lambda([this, Guid]()
        {
            const FBubble* Bubble = Bubbles.Find(Guid);
            return Bubble && Bubble->bOnScreen ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
        })
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SOverlay)
                + SOverlay::Slot()[ Backdrop ]
                + SOverlay::Slot().Padding(FMargin(13.f * Scale, 11.f * Scale))
                [
                    SNew(STextBlock).Text(FText::FromString(Text)).Font(Font).ColorAndOpacity(Color)
                    .WrapTextAt(270.f * Scale).Justification(ETextJustify::Center)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0, -4.f * Scale, 0, 0))
            [
                SNew(SBox).WidthOverride(16.f * Scale).HeightOverride(16.f * Scale)
                [ SNew(SImage).Image(&TailBrush) ]
            ]
        ];
}

void SWowChatBubbles::AddBubble(uint64 Guid, const FString& InText, const FLinearColor& Color)
{
    if (!Canvas.IsValid() || Guid == 0) return;
    // Links and colour escapes show as plain text: |cffxxxxxx..|r, |Hitem:..|h[Name]|h.
    FString Text;
    Text.Reserve(InText.Len());
    for (int32 I = 0; I < InText.Len(); ++I)
    {
        if (InText[I] == TEXT('|') && I + 1 < InText.Len())
        {
            const TCHAR Code = InText[I + 1];
            if (Code == TEXT('c') && I + 9 < InText.Len()) { I += 9; continue; }
            if (Code == TEXT('r') || Code == TEXT('h')) { I += 1; continue; }
            if (Code == TEXT('H')) { const int32 End = InText.Find(TEXT("|h"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I + 2); if (End != INDEX_NONE) { I = End + 1; continue; } }
        }
        Text.AppendChar(InText[I]);
    }
    Text.TrimStartAndEndInline();
    if (Text.IsEmpty()) return;

    if (TSharedPtr<SWidget>* Old = BubbleWidgets.Find(Guid); Old && Old->IsValid())
        Canvas->RemoveSlot(Old->ToSharedRef());
    FBubble& Bubble = Bubbles.FindOrAdd(Guid);
    Bubble.Guid = Guid;
    Bubble.ExpireAt = FPlatformTime::Seconds() + BubbleSeconds(Text);
    Bubble.bOnScreen = bWorldMode || (Anchor && Anchor(Guid, Bubble.Position));

    TSharedRef<SWidget> Widget = BuildBubble(Text, Color, Guid);
    if (bWorldMode) { BubbleWidgets.Add(Guid, Widget); return; } // shown on a VR world card
    Canvas->AddSlot()
        .AutoSize(true)
        .Alignment(FVector2D(.5f, 1.f))
        .Offset_Lambda([this, Guid]()
        {
            const FBubble* B = Bubbles.Find(Guid);
            return B ? FMargin(B->Position.X, B->Position.Y, 0, 0) : FMargin(0);
        })
        [ Widget ];
    BubbleWidgets.Add(Guid, Widget);
}

void SWowChatBubbles::ClearAll()
{
    if (Canvas.IsValid()) Canvas->ClearChildren();
    Bubbles.Reset();
    BubbleWidgets.Reset();
}

void SWowChatBubbles::SetWorldMode(bool bInWorldMode)
{
    if (bWorldMode == bInWorldMode) return;
    // Bubbles in flight are dropped on the switch; a widget lives on one layer only.
    ClearAll();
    bWorldMode = bInWorldMode;
}

void SWowChatBubbles::GetWorldBubbles(TArray<TPair<uint64, TSharedRef<SWidget>>>& Out) const
{
    Out.Reset();
    if (!bWorldMode) return;
    for (const auto& Pair : BubbleWidgets)
        if (Pair.Value.IsValid()) Out.Emplace(Pair.Key, Pair.Value.ToSharedRef());
}

void SWowChatBubbles::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    const double Now = FPlatformTime::Seconds();
    TArray<uint64> Expired;
    for (auto& Pair : Bubbles)
    {
        FBubble& Bubble = Pair.Value;
        if (Now >= Bubble.ExpireAt) { Expired.Add(Pair.Key); continue; }
        Bubble.bOnScreen = bWorldMode || (Anchor && Anchor(Pair.Key, Bubble.Position));
    }
    for (uint64 Guid : Expired)
    {
        if (TSharedPtr<SWidget>* Widget = BubbleWidgets.Find(Guid); Widget && Widget->IsValid() && Canvas.IsValid() && !bWorldMode)
            Canvas->RemoveSlot(Widget->ToSharedRef());
        BubbleWidgets.Remove(Guid);
        Bubbles.Remove(Guid);
    }
}
