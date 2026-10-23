#pragma once
#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

class FMpqManager;

/**
 * Manages WoW font loading from MPQ archives.
 * Extracts TTF files from MPQ to a temp directory and creates FSlateFontInfo objects.
 * Maps WoW font object names (like "GameFontNormal") to appropriate TTF files.
 */
class WOWUI_API FWowFontManager
{
public:
    static FSlateFontInfo GetScrollFont(int32 Size);
	FWowFontManager();
	~FWowFontManager();

	/** Initialize the font manager and extract fonts from MPQ */
	bool Initialize(FMpqManager* Mpq);

	/** Shutdown and cleanup */
	void Shutdown();

	/** Get a font info for a WoW font name and size */
	FSlateFontInfo GetFont(const FString& WowFontName, int32 Size = 12) const;

	/** Get the default UI font */
	FSlateFontInfo GetDefaultFont(int32 Size = 12) const;

	/** Register a WoW font name mapping from FrameXML */
	/** r289: FontFile is the Font element's font= attribute (Fonts\\ARIALN.TTF); it wins over inheritance. */
	void RegisterFontMapping(const FString& WowFontName, const FString& InheritsFrom, int32 DefaultSize, const FString& FontFile = FString());

	/** Size and colour of a FrameXML font object (Fonts.xml / FontStyles.xml <Font>), inherited like
	 *  the client does: GameFontNormalSmall = 10 pt gold, GameFontDisableSmall = 10 pt grey, ... */
	struct FFontStyle { float Height = 0.f; FLinearColor Color = FLinearColor::White; bool bHasHeight = false; bool bHasColor = false; };
	void RegisterFontStyle(const FString& WowFontName, const FString& InheritsFrom, bool bHasHeight, float Height, const FLinearColor* Color);
	const FFontStyle* FindFontStyle(const FString& WowFontName) const { return FontStyles.Find(WowFontName); }

	/** Check if fonts are available */
	bool IsInitialized() const { return bInitialized; }

private:
	/** Extract a TTF font file from MPQ to the temp directory */
	bool ExtractFont(FMpqManager* Mpq, const FString& MpqPath, const FString& LocalPath);

	/** Create a font info from a local TTF file path */
	FSlateFontInfo CreateFontFromFile(const FString& FilePath, int32 Size) const;

	/** Set up the default WoW font mappings */
	void SetupFontMappings();

	// Font mappings from WoW font names to TTF file names
	TMap<FString, FString> FontMappings;
	TMap<FString, FFontStyle> FontStyles;

	// Directory where fonts are extracted
	FString FontsDirectory;

	// Available font paths
	TMap<FString, FString> AvailableFonts;

	bool bInitialized = false;
};