#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateTypes.h"

class UTexture2D;
class UWowConnectionManager;
class SBox;
class SMultiLineEditableTextBox;
class SEditableTextBox;
class FWowRetainedSlateBrush;
struct FSlateBrush;

/**
 * Native replica of the 3.3.5 macro window (Blizzard_MacroUI: MacroFrame + MacroPopupFrame):
 * "General Macros" (36, shared by the account) and "<character> Specific Macros" (18), the
 * selected macro's icon and name with Change Name/Icon, the command text (255 letters) with its
 * counter, Delete / New / Exit. Drag a macro onto an action button to place it. Data: FWowMacros.
 */
class WOWUI_API SWowMacroFrame : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowMacroFrame) {}
        SLATE_ARGUMENT(TFunction<UTexture2D*(const FString&)>, LoadTexture)
        SLATE_ARGUMENT(TFunction<float()>, GetScale)
        SLATE_ARGUMENT(TFunction<FString()>, GetCharacterName)
        SLATE_ARGUMENT(UWowConnectionManager*, Connection)
        SLATE_ARGUMENT(TFunction<void(const TCHAR*)>, UiSound)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void Open();
    void Close();
    void Toggle() { if (bOpen) Close(); else Open(); }
    bool IsOpen() const { return bOpen; }
    FVector2D GetPanelSize() const;

    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;
    virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent& Event) override;
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Key) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }

private:
    void Rebuild();
    TSharedRef<SWidget> BuildSlot(int32 MacroIndex);
    TSharedRef<SWidget> BuildPopup();
    TSharedRef<SWidget> Icon(const FString& Path, float Size) const;
    const FSlateBrush* Brush(const FString& Path) const;
    TSharedRef<SWidget> Button(const FString& Label, float Width, TFunction<void()> OnClick, TAttribute<bool> Enabled = true) const;
    FSlateFontInfo Font(int32 Size) const;
    // r238: the original art and controls (Blizzard_MacroUI.xml positions).
    const FSlateBrush* BrushUV(const FString& Path, float L, float R, float T, float B) const;
    TSharedRef<SWidget> Art(const FString& Path) const;
    TSharedRef<SWidget> ArtUV(const FString& Path, float L, float R, float T, float B) const;
    TSharedRef<SWidget> AddArt(const FString& Path, float Alpha = 1.f) const;
    TSharedRef<SWidget> CloseButton(TFunction<void()> OnClick) const;
    TSharedRef<SWidget> TextLabel(const FString& Text, int32 Size, const FLinearColor& Color) const;
    TSharedRef<SWidget> BuildTab(const FString& Text, bool bCharacter, float& InOutX);
    const FTextBlockStyle* ButtonText() const;
    float TextWidth(const FString& Text, int32 Size) const;
    float S() const { return Scale; }

    int32 FirstIndex() const;
    int32 SlotCount() const;
    void Select(int32 Index);
    void SaveBody(const FString& Body);
    void OpenPopup(bool bNew);
    void ClosePopup(bool bAccept);
    void Sound(const TCHAR* Name) const { if (PlaySoundFn) PlaySoundFn(Name); }

    TFunction<UTexture2D*(const FString&)> LoadTextureFn;
    TFunction<float()> GetScaleFn;
    TFunction<FString()> GetCharacterNameFn;
    TFunction<void(const TCHAR*)> PlaySoundFn;
    TWeakObjectPtr<UWowConnectionManager> Connection;

    TSharedPtr<SBox> Root;
    TSharedPtr<SMultiLineEditableTextBox> BodyBox;
    TSharedPtr<SEditableTextBox> PopupNameBox;
    mutable TMap<FString, TSharedPtr<FWowRetainedSlateBrush>> Brushes;

    bool bOpen = false;
    bool bCharacterTab = false;
    int32 SelectedIndex = 0;
    uint32 SeenRevision = 0;
    int32 BodyLength = 0;
    float Scale = 1.f;

    // Drag start on a slot (picked up once the pointer moves a few pixels with the button held).
    int32 PressedIndex = 0;
    FVector2D PressedAt = FVector2D::ZeroVector;

    // MacroPopupFrame: name and icon for a new macro, or Change Name/Icon of the selected one.
    bool bPopupOpen = false;
    bool bPopupNew = false;
    FString PopupName;
    int32 PopupIcon = 0;      // index into FWowMacros::Icons()
    int32 PopupFirstRow = 0;  // icon grid scroll (5 per row, 4 rows shown)
    int32 GridFirstRow = 0;   // r238: macro grid scroll (6 per row, 3 rows shown like the XML's 18 buttons)
    mutable FTextBlockStyle ButtonTextStyle;
};
