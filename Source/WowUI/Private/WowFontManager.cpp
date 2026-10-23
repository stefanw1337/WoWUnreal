#include "WowFontManager.h"
#include "Mpq/MpqManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Fonts/CompositeFont.h"
#include "HAL/FileManager.h"
#include "Styling/CoreStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowFont, Log, All);

FWowFontManager::FWowFontManager()
{
	// Set up the fonts directory in the project's Saved folder
	FontsDirectory = FPaths::ProjectSavedDir() / TEXT("Fonts");
}

FWowFontManager::~FWowFontManager()
{
	Shutdown();
}

bool FWowFontManager::Initialize(FMpqManager* Mpq)
{
	if (!Mpq || !Mpq->IsInitialized())
	{
		UE_LOG(LogWowFont, Error, TEXT("Cannot initialize font manager: Invalid MPQ manager"));
		return false;
	}

	if (bInitialized)
	{
		UE_LOG(LogWowFont, Warning, TEXT("Font manager already initialized"));
		return true;
	}

	UE_LOG(LogWowFont, Log, TEXT("Initializing WoW font manager..."));

	// Create the fonts directory
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*FontsDirectory))
	{
		if (!PlatformFile.CreateDirectoryTree(*FontsDirectory))
		{
			UE_LOG(LogWowFont, Error, TEXT("Failed to create fonts directory: %s"), *FontsDirectory);
			return false;
		}
	}

	// List of WoW 3.3.5 fonts to extract from MPQ
	TArray<TPair<FString, FString>> FontsToExtract = {
		{TEXT("Fonts/FRIZQT__.TTF"), TEXT("FRIZQT__.TTF")},		// Main UI font
		{TEXT("Fonts/ARIALN.TTF"), TEXT("ARIALN.TTF")},			// Smaller text
		{TEXT("Fonts/MORPHEUS.TTF"), TEXT("MORPHEUS.TTF")},		// Headers/titles
		{TEXT("Fonts/SKURRI.TTF"), TEXT("SKURRI.TTF")}			// Combat text
	};

	// Extract fonts from MPQ
	int32 ExtractedCount = 0;
	for (const auto& FontPair : FontsToExtract)
	{
		FString LocalPath = FontsDirectory / FontPair.Value;

		// r280 (Stefan 13.10, Diablo 3 login patch): a font extracted once was kept for good, so a
		// patch MPQ replacing Fonts\FRIZQT__.TTF (Exocet) never showed. The extracted copy is
		// refreshed whenever the MPQs hold a different file; it is kept when they hold none.
		if (PlatformFile.FileExists(*LocalPath))
		{
			TArray<uint8> MpqData, LocalData;
			if (Mpq->ReadFile(FontPair.Key, MpqData) && !MpqData.IsEmpty()
				&& (!FFileHelper::LoadFileToArray(LocalData, *LocalPath) || LocalData != MpqData))
			{
				if (FFileHelper::SaveArrayToFile(MpqData, *LocalPath))
				{
					UE_LOG(LogWowFont, Log, TEXT("Font changed in the MPQs, re-extracted: %s (%d bytes)"), *FontPair.Key, MpqData.Num());
				}
				else
				{
					UE_LOG(LogWowFont, Warning, TEXT("Font changed in the MPQs but could not be rewritten: %s"), *LocalPath);
				}
			}
			AvailableFonts.Add(FontPair.Value, LocalPath);
			ExtractedCount++;
			continue;
		}

		if (ExtractFont(Mpq, FontPair.Key, LocalPath))
		{
			AvailableFonts.Add(FontPair.Value, LocalPath);
			ExtractedCount++;
			UE_LOG(LogWowFont, Log, TEXT("Extracted font: %s -> %s"), *FontPair.Key, *LocalPath);
		}
		else
		{
			UE_LOG(LogWowFont, Warning, TEXT("Failed to extract font: %s"), *FontPair.Key);
		}
	}

    // Register locally supplied fonts as well as the four original MPQ fonts.
    TArray<FString> LocalFonts;
    IFileManager::Get().FindFiles(LocalFonts, *(FontsDirectory / TEXT("*.ttf")), true, false);
    for (const FString& Filename : LocalFonts)
        AvailableFonts.Add(Filename.ToUpper(), FontsDirectory / Filename);

	if (AvailableFonts.IsEmpty())
	{
		UE_LOG(LogWowFont, Error, TEXT("No fonts could be extracted"));
		return false;
	}

	// Set up the font mappings
	SetupFontMappings();

	bInitialized = true;
	UE_LOG(LogWowFont, Log, TEXT("WoW font manager initialized successfully (%d fonts available)"), ExtractedCount);
    UE_LOG(LogWowFont, Log, TEXT("Registered %d local font files"), AvailableFonts.Num());
	return true;
}

void FWowFontManager::Shutdown()
{
	if (bInitialized)
	{
		FontMappings.Empty();
		AvailableFonts.Empty();
		bInitialized = false;
		UE_LOG(LogWowFont, Log, TEXT("WoW font manager shutdown"));
	}
}

FSlateFontInfo FWowFontManager::GetFont(const FString& WowFontName, int32 Size) const
{
	if (!bInitialized)
	{
		UE_LOG(LogWowFont, Warning, TEXT("Font manager not initialized, returning default font"));
		return FSlateFontInfo();
	}

    FString FileKey = WowFontName.Replace(TEXT("\\"), TEXT("/"));
    FileKey = FPaths::GetCleanFilename(FileKey).ToUpper();
    if (!FileKey.EndsWith(TEXT(".TTF"))) FileKey += TEXT(".TTF");
    if (FileKey == TEXT("SCROLL.TTF")) return GetScrollFont(Size);
    if (const FString* LocalFile = AvailableFonts.Find(FileKey)) return CreateFontFromFile(*LocalFile, Size);

	// Look up the TTF file for this WoW font name
	const FString* TtfFileName = FontMappings.Find(WowFontName);
	if (!TtfFileName)
	{
		UE_LOG(LogWowFont, Verbose, TEXT("Unknown WoW font name '%s', using default"), *WowFontName);
		return GetDefaultFont(Size);
	}

    if (*TtfFileName == TEXT("SCROLL.TTF")) return GetScrollFont(Size);

	// Find the actual file path
	const FString* FilePath = AvailableFonts.Find(*TtfFileName);
	if (!FilePath)
	{
		UE_LOG(LogWowFont, Warning, TEXT("TTF file '%s' not available, using default"), **TtfFileName);
		return GetDefaultFont(Size);
	}

	return CreateFontFromFile(*FilePath, Size);
}

FSlateFontInfo FWowFontManager::GetDefaultFont(int32 Size) const
{
	if (!bInitialized)
	{
		return FSlateFontInfo();
	}

	// Try to use FRIZQT__ as the default font (main WoW UI font)
	const FString* FrizqtPath = AvailableFonts.Find(TEXT("FRIZQT__.TTF"));
	if (FrizqtPath)
	{
		return CreateFontFromFile(*FrizqtPath, Size);
	}

	// Fallback to any available font
	for (const auto& FontPair : AvailableFonts)
	{
		return CreateFontFromFile(FontPair.Value, Size);
	}

	// Last resort: engine default font
	return FSlateFontInfo();
}

bool FWowFontManager::ExtractFont(FMpqManager* Mpq, const FString& MpqPath, const FString& LocalPath)
{
	TArray<uint8> FontData;
	if (!Mpq->ReadFile(MpqPath, FontData))
	{
		return false;
	}

	if (FontData.IsEmpty())
	{
		UE_LOG(LogWowFont, Warning, TEXT("Font file is empty: %s"), *MpqPath);
		return false;
	}

	// Write the TTF data to the local file
	if (!FFileHelper::SaveArrayToFile(FontData, *LocalPath))
	{
		UE_LOG(LogWowFont, Error, TEXT("Failed to write font file: %s"), *LocalPath);
		return false;
	}

	return true;
}

FSlateFontInfo FWowFontManager::CreateFontFromFile(const FString& FilePath, int32 Size) const
{
	// Create a font info using UE5.7's new API
	FSlateFontInfo FontInfo;

	// Set the size first
	FontInfo.Size = Size;

	// Create a composite font with the TTF file
	TSharedPtr<FCompositeFont> CompositeFont = MakeShared<FCompositeFont>();

	FTypefaceEntry TypefaceEntry;
	FString FontName = FPaths::GetBaseFilename(FilePath);
	TypefaceEntry.Name = FName(*FontName);

	// Create font data from the file path with default hinting and loading policy
	TypefaceEntry.Font = FFontData(FilePath, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);

	CompositeFont->DefaultTypeface.Fonts.Add(TypefaceEntry);

	FontInfo.CompositeFont = CompositeFont;
	FontInfo.TypefaceFontName = FName(*FontName);

	UE_LOG(LogWowFont, Verbose, TEXT("Created font info: %s (size %d) from file: %s"),
		*FontName, Size, *FilePath);

	return FontInfo;
}

void FWowFontManager::SetupFontMappings()
{
	// Map common WoW font object names to TTF files
	// These are the names used in FrameXML and Lua code

	// FRIZQT__ is the main UI font used for most interface elements
	FontMappings.Add(TEXT("GameFontNormal"), TEXT("FRIZQT__.TTF"));
	FontMappings.Add(TEXT("GameFontNormalSmall"), TEXT("FRIZQT__.TTF"));
	FontMappings.Add(TEXT("GameFontNormalLarge"), TEXT("FRIZQT__.TTF"));
	FontMappings.Add(TEXT("GameFontHighlight"), TEXT("FRIZQT__.TTF"));
	FontMappings.Add(TEXT("GameFontHighlightSmall"), TEXT("FRIZQT__.TTF"));
	FontMappings.Add(TEXT("GameFontHighlightLarge"), TEXT("FRIZQT__.TTF"));
	FontMappings.Add(TEXT("GameFontDisable"), TEXT("FRIZQT__.TTF"));
	FontMappings.Add(TEXT("GameFontDisableSmall"), TEXT("FRIZQT__.TTF"));
	FontMappings.Add(TEXT("GameFontWhite"), TEXT("FRIZQT__.TTF"));
	FontMappings.Add(TEXT("GameFontRed"), TEXT("FRIZQT__.TTF"));
	FontMappings.Add(TEXT("GameFontGreen"), TEXT("FRIZQT__.TTF"));
	FontMappings.Add(TEXT("SystemFont"), TEXT("FRIZQT__.TTF"));

	// ARIALN for smaller text and tooltips
	FontMappings.Add(TEXT("GameTooltipText"), TEXT("ARIALN.TTF"));
	// The 3.3.5 chat font (ChatFontNormal/ChatFontSmall) is Arial Narrow.
	FontMappings.Add(TEXT("ChatFontNormal"), TEXT("ARIALN.TTF"));
	FontMappings.Add(TEXT("ChatFontSmall"), TEXT("ARIALN.TTF"));
	FontMappings.Add(TEXT("GameTooltipTextSmall"), TEXT("ARIALN.TTF"));
	FontMappings.Add(TEXT("TooltipFont"), TEXT("ARIALN.TTF"));
	FontMappings.Add(TEXT("QuestFont"), TEXT("ARIALN.TTF"));
	FontMappings.Add(TEXT("QuestFontNormalSmall"), TEXT("ARIALN.TTF"));

	// MORPHEUS for headers and titles
	FontMappings.Add(TEXT("GameFontNormalHuge"), TEXT("SCROLL.TTF"));
	FontMappings.Add(TEXT("GameFontHighlightHuge"), TEXT("SCROLL.TTF"));
	FontMappings.Add(TEXT("ErrorFont"), TEXT("MORPHEUS.TTF"));
	FontMappings.Add(TEXT("ZoneTextFont"), TEXT("MORPHEUS.TTF"));
	FontMappings.Add(TEXT("SubZoneTextFont"), TEXT("MORPHEUS.TTF"));

	// SKURRI for combat text and special effects
	FontMappings.Add(TEXT("CombatTextFont"), TEXT("SKURRI.TTF"));
	FontMappings.Add(TEXT("ChatFont"), TEXT("SKURRI.TTF"));

	UE_LOG(LogWowFont, Log, TEXT("Set up %d font mappings"), FontMappings.Num());
}

void FWowFontManager::RegisterFontMapping(const FString& WowFontName, const FString& InheritsFrom, int32 DefaultSize, const FString& FontFile)
{
	if (!bInitialized)
	{
		UE_LOG(LogWowFont, Warning, TEXT("Cannot register font mapping: font manager not initialized"));
		return;
	}

	// r289 (Stefan 13.10: the chat used the login screen's font): the font= file was ignored, so
	// NumberFont_Shadow_Med (Fonts\ARIALN.TTF, no inherits) became Friz Quadrata and ChatFontNormal,
	// which inherits it, too - with the Diablo 3 patch's Friz replacement the chat turned Exocet. A
	// declared file now sets the mapping, as in the client.
	if (!FontFile.IsEmpty())
	{
		const FString FileKey = FPaths::GetCleanFilename(FontFile.Replace(TEXT("\\"), TEXT("/"))).ToUpper();
		if (AvailableFonts.Contains(FileKey))
		{
			FontMappings.Add(WowFontName, FileKey);
			return;
		}
	}
	// If this font inherits from another, copy its mapping
	if (!InheritsFrom.IsEmpty())
	{
		const FString* ParentTtf = FontMappings.Find(InheritsFrom);
		if (ParentTtf)
		{
			FontMappings.Add(WowFontName, *ParentTtf);
			UE_LOG(LogWowFont, Verbose, TEXT("Registered font mapping: %s -> %s (inherited from %s)"),
				*WowFontName, **ParentTtf, *InheritsFrom);
		}
		else
		{
			// Parent not found, use default font
			FontMappings.Add(WowFontName, TEXT("FRIZQT__.TTF"));
			UE_LOG(LogWowFont, Warning, TEXT("Font %s inherits from unknown font %s, using default"),
				*WowFontName, *InheritsFrom);
		}
	}
	else
	{
		// No inheritance: keep a known mapping (e.g. ChatFontNormal -> ARIALN, as in
		// Fonts.xml) instead of forcing Friz Quadrata; unknown fonts use the default.
		if (!FontMappings.Contains(WowFontName))
		{
			FontMappings.Add(WowFontName, TEXT("FRIZQT__.TTF"));
		}
		UE_LOG(LogWowFont, Verbose, TEXT("Registered font mapping: %s -> FRIZQT__.TTF (no inheritance)"), *WowFontName);
	}
}
FSlateFontInfo FWowFontManager::GetScrollFont(int32 Size)
{
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Fonts");
    const FString Scroll=Directory/TEXT("SCROLL.ttf");
    if (!IFileManager::Get().FileExists(*Scroll))
    {
        const FString Original=Directory/TEXT("MORPHEUS.TTF");
        if (!IFileManager::Get().FileExists(*Original)) return FCoreStyle::GetDefaultFontStyle("Bold",Size);
        static TSharedPtr<const FCompositeFont> OriginalFont=MakeShared<FCompositeFont>(
            FName(TEXT("Heading")),Original,EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
        return FSlateFontInfo(OriginalFont,Size);
    }
    static TSharedPtr<const FCompositeFont> Font=[Directory,Scroll]() {
        auto Composite=MakeShared<FCompositeFont>(FName(TEXT("Scroll")),Scroll,EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
        // Scroll has 70 glyphs: use Friz for +, accents and other missing characters.
        const FString Fallback=Directory/TEXT("FRIZQT__.TTF");
        if (IFileManager::Get().FileExists(*Fallback))
            Composite->FallbackTypeface.Typeface.Fonts.Emplace(FName(TEXT("Scroll")),Fallback,EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
        return Composite;
    }();
    return FSlateFontInfo(Font,Size,FName(TEXT("Scroll")));
}

void FWowFontManager::RegisterFontStyle(const FString& WowFontName, const FString& InheritsFrom, bool bHasHeight, float Height, const FLinearColor* Color)
{
	if (WowFontName.IsEmpty()) return;
	FFontStyle Style;
	if (!InheritsFrom.IsEmpty())
		if (const FFontStyle* Parent = FontStyles.Find(InheritsFrom)) Style = *Parent;
	if (bHasHeight && Height > 0.f) { Style.Height = Height; Style.bHasHeight = true; }
	if (Color) { Style.Color = *Color; Style.bHasColor = true; }
	FontStyles.Add(WowFontName, Style);
}
