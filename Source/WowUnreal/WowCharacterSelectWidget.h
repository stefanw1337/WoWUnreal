#pragma once
#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "WowSessionState.h"

DECLARE_DELEGATE_OneParam(FOnCharacterSelected, int64 /*Guid*/);
DECLARE_DELEGATE(FOnCreateCharacterRequest);
DECLARE_DELEGATE_OneParam(FOnDeleteCharacterRequest, int64 /*Guid*/);
DECLARE_DELEGATE(FOnBackFromCharacterSelect);
DECLARE_DELEGATE_OneParam(FOnCharacterHighlighted, const FWowCharacterInfo& /*Character*/);
DECLARE_DELEGATE_OneParam(FOnCharacterRotated, float /*YawDeltaDegrees*/);

class UTextureRenderTarget2D;
class SBox;
struct FSlateBrush;

class SWowCharacterSelectWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowCharacterSelectWidget) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWowCharacterSelectWidget();

    /** Enter / keypad Enter on the character list: enter the world (GlueXML CharacterSelect_OnKeyDown). */
    bool HandleEnterKey();
    void PopulateCharacters(const TArray<FWowCharacterInfo>& Characters);
    void SetStatusText(const FString& Text);

    /** Set the render target for 3D character preview */
    void SetPreviewRenderTarget(UTextureRenderTarget2D* InRenderTarget);

    /** Apply original 3.3.5 glue textures loaded from MPQ at runtime. */
    void SetGlueTextures(UTexture2D* InLogo, UTexture2D* InCharacterHighlight);

    /** r232: the realm's name at the top of the character list (CharSelectRealmName). */
    void SetRealmName(const FString& RealmName);
    /** r232: a server's own news, shown in a scroll panel on the left (empty = no panel). Servers put
     *  it in Interface\Glues\ServerNews.txt in their data (loose or MPQ); |cAARRGGBB..|r colours and
     *  |n line breaks are understood. */
    void SetServerNews(const FString& NewsText);

    /** Select an entry without pointer input for deterministic local visual tests. */
    void SelectCharacterForTesting(int32 Index);

    FOnCharacterSelected OnCharacterSelected;
    FOnCreateCharacterRequest OnCreateCharacterRequest;
    FOnDeleteCharacterRequest OnDeleteCharacterRequest;
    FOnBackFromCharacterSelect OnBackRequest;
    FOnBackFromCharacterSelect OnChangeRealmRequest;
    FOnCharacterHighlighted OnCharacterHighlighted;
    FOnCharacterRotated OnCharacterRotated;

    virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
    void SelectCharacter(int32 Index);
    void EnterSelectedCharacter();
    void DeleteSelectedCharacter();

    TSharedPtr<SVerticalBox> CharListBox;
    TSharedPtr<STextBlock> StatusLabel;
    TSharedPtr<STextBlock> SelectedNameLabel;
    TSharedPtr<STextBlock> SelectedDetailLabel;
    TSharedPtr<STextBlock> RealmNameLabel;            // r232
    TSharedPtr<class SVerticalBox> ServerNewsLines;   // r232
    TSharedPtr<SWidget> ServerNewsPanel;              // r232
    TSharedPtr<SImage> PreviewImage;
    TSharedPtr<FSlateBrush> PreviewBrush;
    TSharedPtr<STextBlock> PreviewPlaceholder;
    TSharedPtr<SImage> LogoImage;
    TSharedPtr<SBox> LogoFallback;
    TSharedPtr<FSlateBrush> LogoBrush;
    TSharedPtr<FSlateBrush> CharacterHighlightBrush;
    TStrongObjectPtr<UTexture2D> LogoTexture;
    TStrongObjectPtr<UMaterialInterface> CharacterHighlightMaterial;
    TStrongObjectPtr<UTexture2D> CharacterHighlightTexture;
    TArray<FWowCharacterInfo> CachedCharacters;
    int32 SelectedIndex = INDEX_NONE;
    int32 HoveredIndex = INDEX_NONE;
    int64 SelectedGuid = 0;
    bool bRotatingCharacter = false;
    // Delete asks first; holds the character being deleted while the dialog is open.
    int64 PendingDeleteGuid = 0;
    FString PendingDeleteName;
    /** The confirmation needs "DELETE" typed (CharacterDeleteDialog). */
    FString DeleteTypedText;
    TSharedPtr<class SEditableText> DeleteTextBox;
    /** Double-clicking a character row enters the world. */
    int32 LastClickedIndex = INDEX_NONE;
    double LastClickTime = 0.0;
    TSharedPtr<class IInputProcessor> EnterKeyProcessor;
};
