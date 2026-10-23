#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "WowSessionState.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"

DECLARE_DELEGATE_NineParams(FOnCharacterCreated,
    const FString& /*Name*/, uint8 /*Race*/, uint8 /*Class*/, uint8 /*Gender*/,
    uint8 /*Skin*/, uint8 /*Face*/, uint8 /*HairStyle*/, uint8 /*HairColor*/, uint8 /*FacialHair*/);
DECLARE_DELEGATE(FOnBackToCharSelect);
DECLARE_DELEGATE_TwoParams(FOnCharacterAppearanceChanged, uint8 /*Race*/, uint8 /*Gender*/);

class SEditableTextBox;
class UTextureRenderTarget2D;
struct FSlateBrush;

class SWowCharacterCreateWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowCharacterCreateWidget) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetStatusText(const FString& Text);
    /** A red error across the top like the game's UIErrorsFrame ("Inventory is full."); fades out. */
    void ShowError(const FString& Text);
    void SetPreviewRenderTarget(UTextureRenderTarget2D* InRenderTarget);
    void SetPreviewSelection(uint8 RaceId, uint8 GenderId);

    void SetGlueTexture(const FString& Key, UTexture2D* Texture);
    /** Loads Interface textures by path (no extension) from the user's MPQs. */
    void SetTextureLoader(TFunction<UTexture2D*(const FString&)> InLoader);
    /** GlueStrings.lua from the user's locale (race/class texts, labels). */
    void SetGlueStrings(TMap<FString, FString> InStrings);
    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;
    FWowCharacterInfo GetPreviewCharacter() const;
    void CycleAppearance(int32 Field, int32 Direction);
    DECLARE_DELEGATE_OneParam(FOnPreviewRotated, float);
    FOnPreviewRotated OnPreviewRotated;
    /** true while the player is changing face/hair/facial features (preview camera should zoom
     *  to the face), false again on race, gender or class changes. Bound by the login controller. */
    DECLARE_DELEGATE_OneParam(FOnPreviewFaceFocus, bool);
    FOnPreviewFaceFocus OnPreviewFaceFocus;
    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
    virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent&) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override;

    FOnCharacterCreated OnCharacterCreated;
    FOnBackToCharSelect OnBackToCharSelect;
    FOnCharacterAppearanceChanged OnAppearanceChanged;

private:
    const FSlateBrush* GlueBrush(const FString& Key) const;
    void SubmitCharacter();
    TArray<int32> AppearanceOptions(int32 Field) const;
    void NormalizeAppearance();
    int32& AppearanceValue(int32 Field);
    mutable TMap<FString, TSharedPtr<FSlateBrush>> GlueBrushes;
    const FSlateBrush* TexBrush(const FString& Path, FBox2f UV) const;
    TSharedRef<SWidget> TexImage(const FString& Path, FBox2f UV, TAttribute<FSlateColor> Tint = FSlateColor(FLinearColor::White)) const;
    TSharedRef<SWidget> TexAdd(const FString& Path, FBox2f UV, TAttribute<FLinearColor> Tint) const;
    FString Glue(const FString& Token, const TCHAR* Fallback = nullptr) const;
    FString Flavor(const FString& Token) const;
    FString AbilityText(bool bRace) const;
    FString CustomizationLabel(int32 Field) const;
    FLinearColor BackdropColor() const;
    TSharedRef<SWidget> IconButton(const FString& Tooltip, TFunction<const FSlateBrush*()> Image,
        TFunction<bool()> Checked, TFunction<bool()> Enabled, TFunction<void()> Click);
    TSharedRef<SWidget> GlueButton(const FString& Text, float FontPixels, TFunction<void()> Click);
    TSharedRef<SWidget> ArrowButton(bool bRight, TFunction<void()> Click);
    TSharedRef<SWidget> InfoBox(bool bRace);
    TFunction<UTexture2D*(const FString&)> TextureLoader;
    mutable TMap<FString, TStrongObjectPtr<UTexture2D>> PathTextures;
    mutable TSet<FString> MissingPaths;
    TArray<TSharedPtr<FButtonStyle>> ButtonStyles;
    TMap<FString, FString> GlueStrings;
    int32 RotateDirection = 0;
    TSharedPtr<class SScrollBox> RaceScroll, ClassScroll;
    TArray<TStrongObjectPtr<UTexture2D>> GlueTextures;
    bool bRotatingPreview = false;
    void SetSelectedRaceIndex(int32 NewIndex);
    void SetSelectedClassIndex(int32 NewIndex);
    void CycleRace(int32 Direction);
    void CycleClass(int32 Direction);
    void ToggleGender();
    void RandomizeAppearance();
    void RefreshSelectionText();
    void RefreshDescriptionText();
    void NotifyAppearanceChanged();
    bool IsClassAllowedForRace(uint8 RaceId, uint8 ClassId) const;
    void EnsureValidClassForRace();
    /** Remembers race/class/gender/looks/name for the next visit to character create. */
    void SaveDraft() const;
    void RestoreDraft();
    /** Looks (skin, face, hair, hair colour, facial hair) are remembered per race and gender. */
    void StoreLooks() const;
    void LoadLooks();
    /** "Go back?" confirmation shown by the Back button. */
    bool bConfirmBack = false;

    TSharedPtr<SEditableTextBox> NameBox;
    TSharedPtr<STextBlock> StatusLabel;
    FString ErrorText;
    double ErrorShownAt = -100.0;
    TSharedPtr<STextBlock> FactionText;
    TSharedPtr<STextBlock> RaceText;
    TSharedPtr<STextBlock> ClassText;
    TSharedPtr<STextBlock> GenderText;
    TSharedPtr<STextBlock> SkinText;
    TSharedPtr<STextBlock> FaceText;
    TSharedPtr<STextBlock> HairStyleText;
    TSharedPtr<STextBlock> HairColorText;
    TSharedPtr<STextBlock> FacialHairText;
    TSharedPtr<STextBlock> RaceTitleText;
    TSharedPtr<STextBlock> RaceBodyText;
    TSharedPtr<STextBlock> ClassTitleText;
    TSharedPtr<STextBlock> ClassBodyText;
    TSharedPtr<SImage> PreviewImage;
    TSharedPtr<FSlateBrush> PreviewBrush;
    TSharedPtr<STextBlock> PreviewPlaceholder;

    int32 SelectedRaceIndex = 0;
    int32 SelectedClassIndex = 0;
    int32 SelectedGender = 0;
    int32 SelectedSkin = 0;
    int32 SelectedFace = 0;
    int32 SelectedHairStyle = 0;
    int32 SelectedHairColor = 0;
    int32 SelectedFacialHair = 0;
};
