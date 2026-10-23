#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "Styling/SlateTypes.h"
#include "WowSessionState.h"

DECLARE_DELEGATE_OneParam(FOnRealmSelected, int32 /*Index*/);

class SCanvas;
class SBox;
class STextBlock;
class UTexture2D;

/**
 * Native replica of the 3.3.5 GlueXML RealmList frame (RealmList.xml / RealmList.lua):
 * 0.75 black screen, 640x512 HelpFrame panel with the "Realm Selection" header, sortable
 * Realm Name / Type / Characters / Population columns, 18 rows of 16 px (20 px pitch),
 * UI-QuestLogTitleHighlight selection, category tabs, Okay / Cancel and the close button.
 * Click selects, double-click / Okay / Enter connects. Positions are glue units (768 high).
 */
class SWowRealmSelectWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowRealmSelectWidget) {}
        /** Loads an MPQ texture by path without extension (the user's own data). */
        SLATE_ARGUMENT(TFunction<UTexture2D*(const FString&)>, TextureLoader)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void PopulateRealms(const TArray<FWowRealmInfo>& Realms, int32 ActiveIndex = INDEX_NONE);
    void SetStatusText(const FString& Text);

    FOnRealmSelected OnRealmSelected;
    FSimpleDelegate OnCancel;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
    enum class ESort : uint8 { None, Name, Type, Characters, Load };

    void Rebuild();
    void BuildPanelArt(SCanvas& Canvas);
    void BuildSortButtons(SCanvas& Canvas);
    void BuildRows(SCanvas& Canvas);
    void BuildScrollBar(SCanvas& Canvas, int32 NumRealms);
    void BuildTabs(SCanvas& Canvas);
    void BuildButtons(SCanvas& Canvas);

    void SelectRow(int32 RealmIndex);
    void Accept();
    void Cancel();
    void SetSort(ESort NewSort);
    void Scroll(int32 Delta);
    TArray<int32> VisibleRealms() const;
    bool IsSelectable(int32 RealmIndex) const;

    // Helpers (glue units)
    void Place(SCanvas& Canvas, float X, float Y, float W, float H, TSharedRef<SWidget> Widget) const;
    const FSlateBrush* Brush(const FString& Path, FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1))) const;
    TSharedRef<SWidget> Img(const FString& Path, FBox2f UV = FBox2f(FVector2f(0, 0), FVector2f(1, 1))) const;
    TSharedRef<SWidget> AddImg(const FString& Path, FBox2f UV, TAttribute<FLinearColor> Tint) const;
    static FSlateFontInfo GlueFont(float WowPixels);

    TFunction<UTexture2D*(const FString&)> TextureLoader;
    mutable TMap<FString, TStrongObjectPtr<UTexture2D>> Textures;
    mutable TMap<FString, TSharedPtr<FSlateBrush>> Brushes;
    mutable TSet<FString> MissingTextures;
    TArray<TSharedPtr<FButtonStyle>> ButtonStyles;

    TSharedPtr<SBox> PanelRoot;
    TArray<FWowRealmInfo> CachedRealms;
    TArray<uint8> Categories;
    uint8 SelectedCategory = 0;
    int32 CurrentRealmIndex = INDEX_NONE;
    int32 SelectedRealm = INDEX_NONE;
    int32 Offset = 0;
    ESort Sort = ESort::None;
    bool bSortDescending = false;
    FString StatusText;
};
