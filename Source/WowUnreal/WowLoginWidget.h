#pragma once
#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "MediaTexture.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "WowExpansionTypes.h"
#include "WowCredentialStore.h"
#include "WowFontManager.h"
#include "Styling/SlateTypes.h"

DECLARE_DELEGATE_FourParams(FOnLoginSubmit, const FString& /*Server*/, int32 /*Port*/, const FString& /*User*/, const FString& /*Pass*/);
DECLARE_DELEGATE_OneParam(FOnExpansionChanged, uint8 /*ExpansionIndex*/);
DECLARE_DELEGATE(FOnLoginOptionsRequested);
DECLARE_DELEGATE(FOnLoginExitRequested);
DECLARE_DELEGATE_OneParam(FOnLoginNotice, const FString&);

struct FSlateBrush;

/**
 * WoW-themed login screen with expansion switcher.
 * Mimics the look of the original client login screens.
 * Supports cinematic video background via media texture.
 */
class SWowLoginWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowLoginWidget) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void InitializeArtwork(FMpqManager* Mpq);
    void SetStatusText(const FString& Text);

    /** Prefill the login fields */
    void SetCredentials(const FString& Server, const FString& Username, const FString& Password);

    /** Configure login for a local session that never opens a network connection. */
    void SetOfflineMode(bool bEnabled);

    /** Populate the optional left-side saved server/account drawer. */
    void SetSavedProfiles(const TArray<FWowCredential>& Profiles);

    /** Programmatically click the login button */
    void AutoSubmit();

    /** Set the media texture for cinematic background */
    void SetCinematicTexture(UMediaTexture* InTexture);

    /** Set a runtime MPQ/static texture for the login background */
    void SetBackgroundTexture(UTexture2D* InTexture);

    /** Keep the login UI visible while allowing a real 3D scene behind it. */
    void SetWorldSceneBackgroundActive(bool bActive);

    FOnLoginSubmit OnLoginSubmit;
    FOnLoginNotice OnNotice;
    FOnExpansionChanged OnExpansionChanged;
    FOnLoginOptionsRequested OnOptionsRequested;
    FOnLoginExitRequested OnExitRequested;
    FSimpleDelegate OnArtworkSourceChanged;
    FOnLoginSubmit OnSaveServer;
    TSharedRef<SWidget> CreateArtworkOptions() { return BuildExpansionTabs(); }
    bool UsesModdedArtwork() const { return bModdedArtwork; }
    void SetModdedArtwork(bool bEnabled) { bModdedArtwork = bEnabled; OnArtworkSourceChanged.ExecuteIfBound(); }
    FString GetWebsiteUrl() const;
    FString GetAccountUrl() const;
    FString GetTermsUrl() const;
    static bool IsWebUrl(const FString& Url);
    int32 GetServerVersion() const { return ServerVersion; }

    EWowExpansion GetCurrentExpansion() const { return CurrentExpansion; }

public:
    void SetExpansion(EWowExpansion Expansion);

private:
    friend class FWowLoginThemeTest;
    FSlateFontInfo LoginFont(int32 Size) const;
    /** r388: the glue edit-box backdrop (Glue-Tooltip-Border on UI-Tooltip-Background); the textures are kept alive by ArtTextures. */
    void ApplyFieldArt(const TSharedRef<class SWowBackdrop>& Backdrop) const;
    UTexture2D* FieldBackground = nullptr;
    UTexture2D* FieldBorder = nullptr;
    FWowFontManager Fonts;
    FButtonStyle LoginButtonStyle;
    TArray<TStrongObjectPtr<UTexture2D>> ArtTextures;
    TArray<TSharedPtr<FSlateBrush>> LogoBrushes;
    TArray<bool> ThemeAvailable;
    int32 SavedTheme=-1;
    bool bRealmDrawerOpen=false;
    bool bServerEditorOpen=false;
    FReply ToggleServerDrawer();
    FButtonStyle ServerRowStyle;
    bool bModdedArtwork=false;
    int32 ServerVersion=2;
    TArray<FWowCredential> SavedProfiles;
    void RefreshSavedProfiles();
    void SetServerVersion(int32 Version);
    FReply SaveServer();
    TArray<TSharedPtr<FSlateBrush>> PanelEdges;
    TSharedRef<SWidget> BuildServerPanelFrame(TSharedRef<SWidget> Content);
    FReply OnLoginClicked();

    /** Build the expansion tab bar at the bottom */
    TSharedRef<SWidget> BuildExpansionTabs();

    /** Get colors for the current expansion */
    FLinearColor GetBackgroundColor() const;
    FLinearColor GetAccentColor() const;
    FLinearColor GetFallbackSideColor(bool bRightSide) const;
    FLinearColor GetFallbackCenterColor() const;
    FLinearColor GetFallbackBandColor() const;
    FString GetExpansionTitle() const;
    FString GetExpansionSubtitle() const;
    void RefreshFallbackVisuals();
    bool HasExternalBackground() const;

    EWowExpansion CurrentExpansion = EWowExpansion::WrathOfTheLichKing;
    bool bWorldSceneBackgroundActive = false;

    TSharedPtr<SEditableTextBox> ServerBox;
    FString WebsiteUrl, AccountUrl, TermsUrl;
    TSharedPtr<SBorder> CreditsPanel;
    FString ProjectWebsite;
    TSharedPtr<SEditableTextBox> UsernameBox;
    TSharedPtr<SEditableTextBox> PasswordBox;
    TSharedPtr<STextBlock> StatusLabel;
	TSharedPtr<STextBlock> ModeLabel;
	TSharedPtr<SBorder> BookmarkDrawer;
	TSharedPtr<SVerticalBox> BookmarkList;
    TSharedPtr<STextBlock> TitleText;
    TSharedPtr<STextBlock> SubtitleText;
    TSharedPtr<SBorder> BackgroundBorder;
    TSharedPtr<SBorder> FallbackLeftPanel;
    TSharedPtr<SBorder> FallbackRightPanel;
    TSharedPtr<SBorder> FallbackCenterPanel;
    TSharedPtr<SBorder> FallbackTopBand;
    TSharedPtr<SBorder> FallbackBottomBand;

    // Cinematic background
    TSharedPtr<SImage> BackgroundImage;
    TStrongObjectPtr<UTexture2D> BackgroundTexture;
    TSharedPtr<FSlateBrush> BackgroundTextureBrush;
    TSharedPtr<SImage> CinematicImage;
    TStrongObjectPtr<UMediaTexture> CinematicTexture;
    TSharedPtr<FSlateBrush> CinematicBrush;

    // Expansion tab buttons for styling updates
    TSharedPtr<SBorder> ClassicTab;
    TSharedPtr<SBorder> BCTab;
    TSharedPtr<SBorder> WotLKTab;
	bool bOfflineMode = false;
};
