#include "WowFrameManager.h"
#include "WowFrameLayout.h"
#include "WowColor.h"
#include "WowUIManager.h"
#include "WowUiVrHost.h"
#include "WowCooldownWidget.h"
#include "WowClientPreferences.h"
#include "Components/NativeWidgetHost.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WowWindowStyle.h"
#include "Components/Border.h"
#include "WowChatFade.h"
#include "Widgets/SViewport.h"
#include "WowViewportCoordinates.h"
#include "WowSpellPresentation.h"
#include "WowKeyBindings.h"
#include "WowItemPresentation.h"
#include "WowConnectionManager.h"
#include "WowMacros.h"
#include "WowClientPreferences.h"
#include "WowEventSystem.h"
#include "WowBackdropWidget.h"
#include "WowFontManager.h"
#include "WowBlendImage.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/SlateBrushAsset.h"
#include "Mpq/MpqManager.h"
#include "WowAssetCache.h"
#include "Formats/BlpParser.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/SpellDbc.h"
#include "Formats/Dbc/SpellIconDbc.h"
#include "WowTextureFactory.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Slate/WidgetTransform.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Engine/UserInterfaceSettings.h"
#include "Misc/DateTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "WowEntity.h"

// r278 (Stefan 13.10, calendar): a texture/fontstring rect in FrameRects is absolute, computed from
// its frame's rect at the time. When the frame later moved (the window row places CalendarFrame),
// the region kept its old position, so frames anchored to it (the day buttons hang off
// CalendarWeekday1Background) landed 20 units too high, over the weekday row. Each region rect
// remembers the frame position it was computed from and follows the frame in GetFrameRect.
struct FWowRegionOwnerAt { FString Owner; float X = 0.f; float Y = 0.f; };
static TMap<FString, FWowRegionOwnerAt> GWowRegionOwners;
static void NoteRegionOwnerAt(const FString& Region, const FString& Owner, float OwnerX, float OwnerY)
{
	if (Region.IsEmpty() || Owner.IsEmpty() || Region == Owner) return;
	FWowRegionOwnerAt& At = GWowRegionOwners.FindOrAdd(Region);
	At.Owner = Owner; At.X = OwnerX; At.Y = OwnerY;
}

// The pointer in the absolute space of the UI widgets: the virtual window while the VR host is
// active, the desktop otherwise.
static FVector2D WowUiCursorAbsolute()
{
	FVector2D WindowPos;
	if (FWowUiVrHost::CursorToWindow(WindowPos)) return WindowPos;
	return FSlateApplication::IsInitialized() ? FVector2D(FSlateApplication::Get().GetCursorPos()) : FVector2D::ZeroVector;
}

static int32 MinimapOverlayZOrder(const FWowFrameManager& Manager); // defined near SetMinimapFrostOverlay

bool FWowFrameManager::GetLuaCursorPosition(float& OutX, float& OutY)
{
	// From the Slate pointer and the UI root's geometry, like hover tests. The viewport's own mouse
	// position stays stale (0,0) while the pointer is over the UI, so "cursor" dropdowns (chat tab
	// right-click) opened in the top-left corner (Stefan 10.10).
	if (!RootCanvas.IsValid() || UIScale <= 0.f || !FSlateApplication::IsInitialized()) return false;
	const FVector2D Local = RootCanvas->GetCachedGeometry().AbsoluteToLocal(WowUiCursorAbsolute()) / UIScale;
	const FFrameRect Screen = GetFrameRect(TEXT("UIParent"));
	if (Screen.H <= 0.f) return false;
	OutX = float(Local.X);
	OutY = Screen.H - float(Local.Y);
	return true;
}

// Combat Log filters (Stefan 10.10: the three buttons did nothing). Self: the player is the
// source or the target. Everything: all lines. What happened to me?: the player is the target.
bool FWowFrameManager::CombatLogLinePasses(const FCombatLogHistoryLine& Line) const
{
	if (ActiveCombatLogFilterIndex == 1) return Line.bPlayerSource || Line.bPlayerTarget;
	if (ActiveCombatLogFilterIndex == 3) return Line.bPlayerTarget;
	return true;
}

void FWowFrameManager::AddCombatLogMessage(const FString& Message, const FLinearColor& Color, bool bPlayerIsSource, bool bPlayerIsTarget)
{
	FCombatLogHistoryLine& Line = CombatLogHistory.AddDefaulted_GetRef();
	Line.Message = Message; Line.Color = Color; Line.bPlayerSource = bPlayerIsSource; Line.bPlayerTarget = bPlayerIsTarget;
	if (CombatLogHistory.Num() > 500) CombatLogHistory.RemoveAt(0, CombatLogHistory.Num() - 500);
	if (CombatLogLinePasses(Line)) AddScrollingMessage(FindFrame(TEXT("ChatFrame2")), Message, Color);
}

void FWowFrameManager::ReplayCombatLog()
{
	const int64 CombatLogFrame = FindFrame(TEXT("ChatFrame2"));
	if (CombatLogFrame < 0) return;
	ClearScrollingMessages(CombatLogFrame);
	TArray<const FCombatLogHistoryLine*> Shown;
	for (int32 Index = CombatLogHistory.Num() - 1; Index >= 0 && Shown.Num() < 200; --Index)
		if (CombatLogLinePasses(CombatLogHistory[Index])) Shown.Add(&CombatLogHistory[Index]);
	for (int32 Index = Shown.Num() - 1; Index >= 0; --Index)
		AddScrollingMessage(CombatLogFrame, Shown[Index]->Message, Shown[Index]->Color);
}
// Like the real client, a frame with no points and no size has no rectangle and is not
// drawn (for example an addon's Interface Options panel parented to UIParent until the
// options window places it). Its Lua shown state is unchanged.
/** Lua-created frames use the original sizing rules; tooltips are placed natively by SetOwner. */
static bool UsesOriginalSizing(const FWowFrameDef& Def)
{
	return Def.bCreatedByLua && Def.Type != EWowFrameType::GameTooltip;
}

// Chat row height from the font's real line height: Slate font sizes are points (x 4/3 in
// pixels), so the old "size + 3" rows overlapped and cut the text at larger chat font sizes
// (Stefan 10.10). Never below the old 16-unit row.
static float WowChatRowHeight(const FSlateFontInfo& Font, float Scale)
{
	float Measured = static_cast<float>(Font.Size) * 1.4f;
	if (FSlateApplication::IsInitialized())
		Measured = static_cast<float>(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->GetMaxCharacterHeight(Font));
	return FMath::Max(16.0f * Scale, FMath::Max(static_cast<float>(Font.Size) + 3.0f * Scale, Measured + 1.0f * Scale));
}

static bool IsUnplacedFrame(const FWowFrameDef& Def)
{
	// A frame without any anchor has no position and is not drawn. Lua-created frames follow
	// that exactly (AceGUI dropdown pullouts are sized but unanchored until opened); XML frames
	// keep the older rule (only unsized ones) because native layout places some of them.
	return !Def.bVirtual && Def.Anchors.IsEmpty() && !Def.bSetAllPoints
		&& (UsesOriginalSizing(Def) || Def.Width <= 0.f || Def.Height <= 0.f) && Def.Name != TEXT("UIParent");
}
DEFINE_LOG_CATEGORY_STATIC(LogWowFrame, Log, All);

static void CentreUnitFrameText(UTextBlock* Label, float BoxTop, float BoxH, int32 Lines, float InUIScale)
{
    // The middle of the capitals (~0.70 em above the baseline) on the middle of a box BoxH units
    // high, BoxTop units below the top of the label's slot (the text block is top-aligned).
    if (!Label || !FSlateApplication::IsInitialized()) return;
    const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const FSlateFontInfo LabelFont = Label->GetFont();
    const float LineH = static_cast<float>(Measure->GetMaxCharacterHeight(LabelFont));
    const float Descender = -static_cast<float>(Measure->GetBaseline(LabelFont));
    const float CapH = 0.70f * LabelFont.Size * (96.f / 72.f);
    const float CapCentreFromTop = (LineH - Descender) - CapH * .5f + (Lines - 1) * LineH * .5f;
    Label->SetRenderTranslation(FVector2D(0.f, FMath::RoundToFloat((BoxTop + BoxH * .5f) * InUIScale - CapCentreFromTop)));
}

namespace
{
struct FWowChatTextSegment
{
	FString Text;
	FLinearColor Color = FLinearColor::White;
};

int32 WowHexDigit(TCHAR Character)
{
	if (Character >= TEXT('0') && Character <= TEXT('9')) return Character - TEXT('0');
	if (Character >= TEXT('a') && Character <= TEXT('f')) return Character - TEXT('a') + 10;
	if (Character >= TEXT('A') && Character <= TEXT('F')) return Character - TEXT('A') + 10;
	return INDEX_NONE;
}

FString StripWowHyperlinkMarkup(FString Message)
{
	int32 LinkStart = Message.Find(TEXT("|H"));
	while (LinkStart != INDEX_NONE)
	{
		const int32 FirstLabel = Message.Find(TEXT("|h"), ESearchCase::CaseSensitive,
			ESearchDir::FromStart, LinkStart + 2);
		const int32 SecondLabel = FirstLabel == INDEX_NONE ? INDEX_NONE : Message.Find(
			TEXT("|h"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FirstLabel + 2);
		if (FirstLabel == INDEX_NONE || SecondLabel == INDEX_NONE) break;

		const FString VisibleText = Message.Mid(FirstLabel + 2, SecondLabel - FirstLabel - 2);
		Message = Message.Left(LinkStart) + VisibleText + Message.Mid(SecondLabel + 2);
		LinkStart = Message.Find(TEXT("|H"));
	}
	return Message;
}

void ParseWowChatText(const FString& SourceMessage, const FLinearColor& DefaultColor,
	FString& OutPlainText, TArray<FWowChatTextSegment>& OutSegments)
{
	OutPlainText.Reset();
	OutSegments.Reset();
	const FString Message = StripWowHyperlinkMarkup(SourceMessage);
	FLinearColor CurrentColor = DefaultColor;
	FString CurrentText;

	auto FlushSegment = [&OutPlainText, &OutSegments, &CurrentText, &CurrentColor]()
	{
		if (CurrentText.IsEmpty()) return;
		FWowChatTextSegment& Segment = OutSegments.AddDefaulted_GetRef();
		Segment.Text = MoveTemp(CurrentText);
		Segment.Color = CurrentColor;
		OutPlainText += Segment.Text;
		CurrentText.Reset();
	};

	for (int32 Index = 0; Index < Message.Len();)
	{
		if (Message[Index] == TEXT('|') && Index + 1 < Message.Len())
		{
			if (Message[Index + 1] == TEXT('|'))
			{
				CurrentText.AppendChar(TEXT('|'));
				Index += 2;
				continue;
			}
			if (Message[Index + 1] == TEXT('r'))
			{
				FlushSegment();
				CurrentColor = DefaultColor;
				Index += 2;
				continue;
			}
			if (Message[Index + 1] == TEXT('c') && Index + 9 < Message.Len())
			{
				uint32 PackedColor = 0;
				bool bValidColor = true;
				for (int32 DigitIndex = 0; DigitIndex < 8; ++DigitIndex)
				{
					const int32 Digit = WowHexDigit(Message[Index + 2 + DigitIndex]);
					if (Digit == INDEX_NONE)
					{
						bValidColor = false;
						break;
					}
					PackedColor = (PackedColor << 4) | static_cast<uint32>(Digit);
				}
				if (bValidColor)
				{
					FlushSegment();
					const float Alpha = static_cast<float>((PackedColor >> 24) & 0xff) / 255.0f;
					const float Red = static_cast<float>((PackedColor >> 16) & 0xff) / 255.0f;
					const float Green = static_cast<float>((PackedColor >> 8) & 0xff) / 255.0f;
					const float Blue = static_cast<float>(PackedColor & 0xff) / 255.0f;
					CurrentColor = WowColor::FromWow(Red, Green, Blue, Alpha); // |cAARRGGBB is sRGB
					Index += 10;
					continue;
				}
			}
		}

		CurrentText.AppendChar(Message[Index]);
		++Index;
	}
	FlushSegment();

	OutPlainText.TrimStartAndEndInline();
	while (!OutSegments.IsEmpty() && OutSegments[0].Text.TrimStart().IsEmpty())
	{
		OutSegments.RemoveAt(0);
	}
	while (!OutSegments.IsEmpty() && OutSegments.Last().Text.TrimEnd().IsEmpty())
	{
		OutSegments.Pop();
	}
	if (!OutSegments.IsEmpty())
	{
		OutSegments[0].Text.TrimStartInline();
		OutSegments.Last().Text.TrimEndInline();
	}
}

// Tooltip lines are single-colour text blocks: |c/|r markup (addon tooltips such as
// LibDBIcon's "|cffffff00Click|r to ...") is removed instead of shown. A line that is
// one colour run takes that colour; mixed runs keep the line colour.
static void ResolveTooltipLineMarkup(const FString& Source, const FLinearColor& DefaultColor,
	FString& OutText, FLinearColor& OutColor)
{
	OutColor = DefaultColor;
	if (!Source.Contains(TEXT("|")))
	{
		OutText = Source;
		return;
	}
	TArray<FWowChatTextSegment> Segments;
	ParseWowChatText(Source, DefaultColor, OutText, Segments);
	if (Segments.Num() == 1) OutColor = Segments[0].Color;
}

void AppendPersistentChatLog(const FString& PlainText)
{
	const FString LogDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"));
	IFileManager::Get().MakeDirectory(*LogDirectory, true);
	const FDateTime Now = FDateTime::Now();
	const FString LogLine = FString::Printf(TEXT("[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n"),
		Now.GetYear(), Now.GetMonth(), Now.GetDay(), Now.GetHour(), Now.GetMinute(), Now.GetSecond(),
		*PlainText);
	FFileHelper::SaveStringToFile(LogLine,
		*FPaths::Combine(LogDirectory, TEXT("ChatLog.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append | FILEWRITE_AllowRead);
}

FString BuildParentFrameAliasName(const FString& ParentName, const FString& ChildName)
{
	if (!ParentName.EndsWith(TEXT("Frame")) || ChildName.StartsWith(ParentName) || ParentName.Len() <= 5)
	{
		return FString();
	}

	const FString ParentBase = ParentName.LeftChop(5);
	if (ParentBase.IsEmpty() || !ChildName.StartsWith(ParentBase))
	{
		return FString();
	}

	const FString Suffix = ChildName.Mid(ParentBase.Len());
	if (Suffix.IsEmpty())
	{
		return FString();
	}

	return ParentName + Suffix;
}

FString NormalizeClickRegistrationToken(FString Token)
{
	Token.TrimStartAndEndInline();
	Token.ReplaceInline(TEXT(" "), TEXT(""));
	Token = Token.ToUpper();
	return Token;
}

FString BuildClickRegistrationKey(const FString& Button, bool bMouseDown)
{
	return NormalizeClickRegistrationToken(Button + (bMouseDown ? TEXT("Down") : TEXT("Up")));
}

bool HasRegisteredClickToken(const TSet<FString>& RegisteredClicks, const FString& Button, bool bMouseDown)
{
	const FString SpecificKey = BuildClickRegistrationKey(Button, bMouseDown);
	const FString AnyKey = bMouseDown ? TEXT("ANYDOWN") : TEXT("ANYUP");
	return RegisteredClicks.Contains(SpecificKey) || RegisteredClicks.Contains(AnyKey);
}

bool UsesDefaultButtonClickRegistration(const FWowFrameDef& Def, const FString& Button, bool bMouseDown)
{
	return !bMouseDown
		&& Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase)
		&& (Def.Type == EWowFrameType::Button || Def.Type == EWowFrameType::CheckButton);
}

int32 ResolveActionSlotFromFrameName(const FString& Name)
{
	int32 ButtonNum = 0;
	int32 BaseSlot = -1;
	int32 NumberOffset = 0;
	if (Name.StartsWith(TEXT("ActionButton")) && Name.Len() <= 14)
	{
		BaseSlot = 12*(FWowKeyBindings::ActionPage()-1);
		NumberOffset = 12;
	}
	else if (Name.StartsWith(TEXT("MultiBarBottomLeftButton")))
	{
		BaseSlot = 60;
		NumberOffset = 24;
	}
	else if (Name.StartsWith(TEXT("MultiBarBottomRightButton")))
	{
		BaseSlot = 48;
		NumberOffset = 25;
	}
	else if (Name.StartsWith(TEXT("MultiBarRightButton")))
	{
		BaseSlot = 24;
		NumberOffset = 19;
	}
	else if (Name.StartsWith(TEXT("MultiBarLeftButton")))
	{
		BaseSlot = 36;
		NumberOffset = 18;
	}
	else if (Name.StartsWith(TEXT("BonusActionButton")))
	{
		BaseSlot = 72;
		NumberOffset = 17;
	}

	if (BaseSlot < 0)
	{
		return -1;
	}
	ButtonNum = FCString::Atoi(*Name.Mid(NumberOffset));
	return (ButtonNum >= 1 && ButtonNum <= 12) ? BaseSlot + ButtonNum - 1 : -1;
}
}

FWowFrameManager::FWowFrameManager()
{
	for (int32 TimerId = 0; TimerId < MirrorTimers.Num(); ++TimerId)
	{
		MirrorTimers[TimerId].Name = GetMirrorTimerName(TimerId);
	}
}

FString FWowFrameManager::GetMirrorTimerName(int32 TimerId)
{
	switch (TimerId)
	{
	case 0: return TEXT("FATIGUE");
	case 1: return TEXT("BREATH");
	case 2: return TEXT("FIRE");
	default: return TEXT("UNKNOWN");
	}
}

void FWowFrameManager::StartMirrorTimer(int32 TimerId, int32 ValueMs, int32 MaxValueMs,
	int32 Scale, bool bPaused, uint32 SpellId)
{
	if (TimerId < 0 || TimerId >= MirrorTimers.Num())
	{
		UE_LOG(LogWowFrame, Warning, TEXT("Ignoring unknown mirror timer id %d"), TimerId);
		return;
	}

	FWowMirrorTimerState& Timer = MirrorTimers[TimerId];
	Timer.Name = GetMirrorTimerName(TimerId);
	Timer.Label = TimerId == 0 ? TEXT("Fatigue") : TimerId == 1 ? TEXT("Breath") : TEXT("Fire");
	Timer.MaxValueMs = FMath::Max(0, MaxValueMs);
	Timer.ValueMs = FMath::Clamp(static_cast<float>(ValueMs), 0.0f, Timer.MaxValueMs);
	Timer.Scale = static_cast<float>(Scale);
	Timer.SpellId = SpellId;
	Timer.bPaused = bPaused;
	Timer.bActive = Timer.MaxValueMs > 0.0f;
	RefreshMirrorTimerPresentation();
}

void FWowFrameManager::PauseMirrorTimer(int32 TimerId, bool bPaused)
{
	if (TimerId >= 0 && TimerId < MirrorTimers.Num() && MirrorTimers[TimerId].bActive)
	{
		MirrorTimers[TimerId].bPaused = bPaused;
		RefreshMirrorTimerPresentation();
	}
}

void FWowFrameManager::StopMirrorTimer(int32 TimerId)
{
	if (TimerId < 0 || TimerId >= MirrorTimers.Num())
	{
		return;
	}

	FWowMirrorTimerState ResetState;
	ResetState.Name = GetMirrorTimerName(TimerId);
	MirrorTimers[TimerId] = MoveTemp(ResetState);
	RefreshMirrorTimerPresentation();
}

void FWowFrameManager::RefreshBagSlotBorders()
{
	// Equipped bags (and the backpack) get a full gold outline around their
	// icon; an empty bag slot shows only its stock empty-slot art.
	static const TCHAR* BagNames[] = {
		TEXT("MainMenuBarBackpackButton"),
		TEXT("CharacterBag0Slot"),
		TEXT("CharacterBag1Slot"),
		TEXT("CharacterBag2Slot"),
		TEXT("CharacterBag3Slot"),
	};
	for (const TCHAR* BagName : BagNames)
	{
		const FString IconName = FString(BagName) + TEXT("IconTexture");
		TWeakObjectPtr<UImage>* IconImage = NamedTextureWidgets.Find(IconName);
		if (!IconImage || !IconImage->IsValid())
		{
			continue;
		}
		UImage* Icon = IconImage->Get();
		UCanvasPanel* Canvas = Cast<UCanvasPanel>(Icon->GetParent());
		const UCanvasPanelSlot* IconSlot = Cast<UCanvasPanelSlot>(Icon->Slot);
		if (!Canvas || !IconSlot)
		{
			continue;
		}
		// r284 (Stefan 13.10, immersive interface: five yellow squares stayed): the outline follows the
		// icon's whole opacity chain (bar fades and Lua SetAlpha on any parent), not the icon's own.
		float ChainOpacity = 1.0f;
		for (const UWidget* Link = Icon; Link; Link = Link->GetParent()) ChainOpacity *= Link->GetRenderOpacity();
		bool bHasBag = Icon->IsVisible() && ChainOpacity > 0.01f;
		// r284 diagnostic: where the bag button's widgets sit while the immersive interface fades.
		static double NextBagChainLog = 0.0;
		if (FCString::Strcmp(BagName, TEXT("CharacterBag0Slot")) == 0 && FPlatformTime::Seconds() >= NextBagChainLog)
		{
			NextBagChainLog = FPlatformTime::Seconds() + 30.0;
			FString Chain;
			for (const UWidget* Link = Icon; Link; Link = Link->GetParent())
				Chain += FString::Printf(TEXT(" > %s[o%.2f v%d]"), *Link->GetName(), Link->GetRenderOpacity(), int32(Link->GetVisibility()));
			UE_LOG(LogWowFrame, Log, TEXT("Bag button chain: icon opacity chain %.2f%s"), ChainOpacity, *Chain);
		}
		if (bHasBag && FCString::Strcmp(BagName, TEXT("MainMenuBarBackpackButton")) != 0)
		{
			const UObject* Resource = Icon->GetBrush().GetResourceObject();
			bHasBag = Resource && !Resource->GetName().Contains(TEXT("Slot-Bag"), ESearchCase::IgnoreCase)
				&& !Resource->GetName().Contains(TEXT("UI-Quickslot"), ESearchCase::IgnoreCase);
		}
		const FString BorderName = FString(BagName) + TEXT("GoldBorder");
		UImage* Border = nullptr;
		if (TWeakObjectPtr<UImage>* Existing = NamedTextureWidgets.Find(BorderName);
			Existing && Existing->IsValid())
		{
			Border = Existing->Get();
		}
		else if (bHasBag)
		{
			Border = NewObject<UImage>(Canvas);
			Border->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, 2.0f * UIScale,
				WowColor::FromWow(1.0f, 0.78f, 0.12f, 1.0f), 1.5f * UIScale));
			Canvas->AddChildToCanvas(Border);
			NamedTextureWidgets.Add(BorderName, Border);
		}
		if (!Border)
		{
			continue;
		}
		Border->SetVisibility(bHasBag ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		Border->SetRenderOpacity(Icon->GetRenderOpacity()); // r284: the parents' opacity is inherited already
		if (UCanvasPanelSlot* BorderSlot = Cast<UCanvasPanelSlot>(Border->Slot))
		{
			BorderSlot->SetAutoSize(false);
			BorderSlot->SetPosition(IconSlot->GetPosition() - FVector2D(1.0f * UIScale));
			BorderSlot->SetSize(IconSlot->GetSize() + FVector2D(2.0f * UIScale));
			BorderSlot->SetZOrder(IconSlot->GetZOrder() + 50);
		}
	}
}

void FWowFrameManager::TickMirrorTimers(float DeltaTime)
{
	RefreshBagSlotBorders();
	for (FWowMirrorTimerState& Timer : MirrorTimers)
	{
		if (!Timer.bActive || Timer.bPaused || FMath::IsNearlyZero(Timer.Scale))
		{
			continue;
		}

		Timer.ValueMs = FMath::Clamp(
			Timer.ValueMs + Timer.Scale * FMath::Max(0.0f, DeltaTime) * 1000.0f,
			0.0f,
			Timer.MaxValueMs);
	}
	RefreshMirrorTimerPresentation();
}

void FWowFrameManager::RefreshMirrorTimerPresentation()
{
	TArray<const FWowMirrorTimerState*> ActiveTimers;
	for (const FWowMirrorTimerState& Timer : MirrorTimers)
	{
		if (Timer.bActive)
		{
			ActiveTimers.Add(&Timer);
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < MirrorTimers.Num(); ++SlotIndex)
	{
		const FString FrameName = FString::Printf(TEXT("MirrorTimer%d"), SlotIndex + 1);
		const int64 FrameHandle = FindFrame(FrameName);
		if (FrameHandle < 0)
		{
			continue;
		}

		const bool bShouldShow = ActiveTimers.IsValidIndex(SlotIndex);
		if (IsFrameVisible(FrameHandle) != bShouldShow)
		{
			SetFrameVisible(FrameHandle, bShouldShow);
		}
		if (!bShouldShow)
		{
			continue;
		}

		const FWowMirrorTimerState& Timer = *ActiveTimers[SlotIndex];
		const int64 StatusHandle = FindFrame(FrameName + TEXT("StatusBar"));
		const FWowFrameDef* OuterDef = GetFrameDef(FrameHandle);
		const FWowFrameDef* StatusDef = GetFrameDef(StatusHandle);
		const float OuterWidth = (OuterDef && OuterDef->Width > 0.0f) ? OuterDef->Width : 206.0f;
		const float OuterHeight = (OuterDef && OuterDef->Height > 0.0f) ? OuterDef->Height : 26.0f;
		const float StatusWidth = (StatusDef && StatusDef->Width > 0.0f) ? StatusDef->Width : 195.0f;
		const float StatusHeight = (StatusDef && StatusDef->Height > 0.0f) ? StatusDef->Height : 13.0f;
		static constexpr float MirrorTimerContentVerticalOffset = -4.0f;
		const FVector2D StatusPosition(
			FMath::Max(0.0f, (OuterWidth - StatusWidth) * 0.5f) * UIScale,
			(FMath::Max(0.0f, (OuterHeight - StatusHeight) * 0.5f) +
				MirrorTimerContentVerticalOffset) * UIScale);
		const FVector2D StatusSize(StatusWidth * UIScale, StatusHeight * UIScale);
		if (UProgressBar* Bar = GetStatusBarWidget(StatusHandle))
		{
			// MirrorTimer's trough and border are artwork siblings of the nested
			// StatusBar.  Generic layer creation places ARTWORK at Z=200, while
			// the child status frame inherited Z=0.  Put the smaller, inset fill
			// immediately above that artwork; it cannot cover the surrounding
			// border and no longer disappears behind the opaque trough.
			if (UWidget* StatusWidget = GetWidgetForHandle(StatusHandle))
			{
				if (UCanvasPanelSlot* StatusSlot = Cast<UCanvasPanelSlot>(StatusWidget->Slot))
				{
					StatusSlot->SetAutoSize(false);
					StatusSlot->SetAnchors(FAnchors(0.0f, 0.0f));
					StatusSlot->SetAlignment(FVector2D::ZeroVector);
					StatusSlot->SetPosition(StatusPosition);
					StatusSlot->SetSize(StatusSize);
					StatusSlot->SetZOrder(210);
				}
				StatusWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				StatusWidget->SetRenderOpacity(1.0f);
			}
			if (UCanvasPanelSlot* BarSlot = Cast<UCanvasPanelSlot>(Bar->Slot))
			{
				BarSlot->SetAutoSize(false);
				BarSlot->SetAnchors(FAnchors(0.0f, 0.0f));
				BarSlot->SetAlignment(FVector2D::ZeroVector);
				BarSlot->SetPosition(FVector2D::ZeroVector);
				BarSlot->SetSize(StatusSize);
				BarSlot->SetZOrder(1);
			}
			Bar->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			Bar->SetRenderOpacity(1.0f);
			const float Percent = Timer.MaxValueMs > 0.0f
				? FMath::Clamp(Timer.ValueMs / Timer.MaxValueMs, 0.0f, 1.0f)
				: 0.0f;
			Bar->SetPercent(Percent);
			Bar->SetFillColorAndOpacity(
				Timer.Name == TEXT("BREATH") ? WowColor::FromWow(0.08f, 0.42f, 0.92f, 1.0f) :
				Timer.Name == TEXT("FATIGUE") ? WowColor::FromWow(0.82f, 0.62f, 0.08f, 1.0f) :
				WowColor::FromWow(0.92f, 0.23f, 0.04f, 1.0f));
		}

        // Use the native timer value for the visible fill. The stock Lua frame can
        // still change its StatusBar during OnUpdate, after native timer updates.
        // A separate brush also avoids a missing/cooked status-bar texture making
        // a valid timer appear empty. It belongs to the outer frame, under the label.
        if (UCanvasPanel* Outer = Cast<UCanvasPanel>(GetWidgetForHandle(FrameHandle)))
        {
            UImage* Fill = MirrorTimerFills.FindRef(FrameHandle).Get();
            if (!Fill)
            {
                Fill = NewObject<UImage>(Outer);
                Fill->SetBrush(FSlateColorBrush(FLinearColor::White));
                Fill->SetVisibility(ESlateVisibility::HitTestInvisible);
                Outer->AddChildToCanvas(Fill);
                MirrorTimerFills.Add(FrameHandle, Fill);
            }
            const float Percent = Timer.MaxValueMs > 0 ? FMath::Clamp(Timer.ValueMs / Timer.MaxValueMs, 0.0f, 1.0f) : 0;
            Fill->SetColorAndOpacity(Timer.Name == TEXT("BREATH") ? WowColor::FromWow(.08f, .42f, .92f, 1) :
                Timer.Name == TEXT("FATIGUE") ? WowColor::FromWow(.82f, .62f, .08f, 1) : WowColor::FromWow(.92f, .23f, .04f, 1));
            auto* Slot = CastChecked<UCanvasPanelSlot>(Fill->Slot);
            Slot->SetAutoSize(false); Slot->SetAnchors(FAnchors(0,0)); Slot->SetAlignment(FVector2D::ZeroVector);
            Slot->SetPosition(StatusPosition); Slot->SetSize(FVector2D(StatusSize.X*Percent,StatusSize.Y));
            Slot->SetZOrder(215);
            if (UProgressBar* Bar = GetStatusBarWidget(StatusHandle)) Bar->SetRenderOpacity(0);
        }

		const FString NativeLabelName = FrameName + TEXT("NativeText");
		// Prefer the label already supplied by the Blizzard FrameXML template.
		// Creating a fallback unconditionally draws "Breath" twice.
		UTextBlock* Label = GetPrimaryTextWidget(FrameHandle);
		if (!Label)
		{
			Label = GetNamedFontString(NativeLabelName);
		}
		if (!Label)
		{
			// Add the label to the StatusBar canvas itself. A sibling on the outer
			// mirror frame can still be covered by the nested StatusBar widget even
			// with a higher local Z-order.
			if (UCanvasPanel* StatusCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(StatusHandle)))
			{
				Label = NewObject<UTextBlock>(StatusCanvas);
				Label->SetJustification(ETextJustify::Center);
				Label->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				FSlateFontInfo LabelFont = FontManager && FontManager->IsInitialized()
					? FontManager->GetFont(TEXT("GameFontHighlightSmall"),
						FMath::Max(7, FMath::RoundToInt(8.0f * UIScale)))
					: FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),
						FMath::Max(7, FMath::RoundToInt(8.0f * UIScale)));
				LabelFont.OutlineSettings.OutlineSize = 1;
				LabelFont.OutlineSettings.OutlineColor = FLinearColor::Black;
				Label->SetFont(LabelFont);
				if (UCanvasPanelSlot* LabelSlot = StatusCanvas->AddChildToCanvas(Label))
				{
					LabelSlot->SetAutoSize(false);
					LabelSlot->SetAnchors(FAnchors(0.0f, 0.0f));
					LabelSlot->SetAlignment(FVector2D::ZeroVector);
					LabelSlot->SetPosition(FVector2D::ZeroVector);
					LabelSlot->SetSize(StatusSize);
					LabelSlot->SetZOrder(50);
				}
				NamedFontStringWidgets.Add(NativeLabelName, Label);
			}
		}
		if (Label)
		{
            if (UCanvasPanel* Outer = Cast<UCanvasPanel>(GetWidgetForHandle(FrameHandle)))
                if (Label->GetParent() != Outer) { Label->RemoveFromParent(); Outer->AddChildToCanvas(Label); }
			Label->SetText(FText::FromString(Timer.Label));
			Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			FSlateFontInfo LabelFont = FontManager && FontManager->IsInitialized()
				? FontManager->GetFont(TEXT("GameFontHighlightSmall"),
					FMath::Max(7, FMath::RoundToInt(8.0f * UIScale)))
				: FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),
					FMath::Max(7, FMath::RoundToInt(8.0f * UIScale)));
			LabelFont.OutlineSettings.OutlineSize = 1;
			LabelFont.OutlineSettings.OutlineColor = FLinearColor::Black;
			Label->SetFont(LabelFont);
			Label->SetJustification(ETextJustify::Center);
			// UTextBlock aligns its glyph baseline low inside this unusually short
			// (13 px) stock bar. Lift only the glyphs so the word is optically
			// centered without moving the correctly aligned fill.
			Label->SetRenderTranslation(FVector2D(0.0f, -3.0f * UIScale));
			Label->SetShadowOffset(FVector2D(1.0f, 1.0f));
			Label->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.95f));
			if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(Label->Slot))
			{
				LabelSlot->SetAutoSize(false);
				LabelSlot->SetAnchors(FAnchors(0.0f, 0.0f));
				LabelSlot->SetAlignment(FVector2D::ZeroVector);
				// The stock label belongs to the outer frame, while the native
				// fallback belongs to the inset StatusBar. Normalize both to the
				// exact fill rectangle so the text is geometrically centered.
				const bool bInsideStatusBar = Label->GetParent() == GetWidgetForHandle(StatusHandle);
				LabelSlot->SetPosition(bInsideStatusBar ? FVector2D::ZeroVector : StatusPosition);
				LabelSlot->SetSize(StatusSize);
				LabelSlot->SetZOrder(bInsideStatusBar ? 50 : 220);
			}
		}
	}
}

bool FWowFrameManager::GetMirrorTimerInfo(int32 OneBasedIndex, FWowMirrorTimerState& OutState) const
{
	const int32 TimerId = OneBasedIndex - 1;
	if (TimerId < 0 || TimerId >= MirrorTimers.Num() || !MirrorTimers[TimerId].bActive)
	{
		OutState = FWowMirrorTimerState();
		return false;
	}

	OutState = MirrorTimers[TimerId];
	return true;
}

bool FWowFrameManager::GetMirrorTimerState(int32 TimerId, FWowMirrorTimerState& OutState) const
{
	if (TimerId < 0 || TimerId >= MirrorTimers.Num() || !MirrorTimers[TimerId].bActive)
	{
		OutState = FWowMirrorTimerState();
		OutState.Name = GetMirrorTimerName(TimerId);
		return false;
	}

	OutState = MirrorTimers[TimerId];
	return true;
}

float FWowFrameManager::GetMirrorTimerProgress(const FString& TimerName) const
{
	for (const FWowMirrorTimerState& Timer : MirrorTimers)
	{
		if (Timer.bActive && Timer.Name.Equals(TimerName, ESearchCase::IgnoreCase))
		{
			return Timer.ValueMs;
		}
	}

	return 0.0f;
}

void FWowFrameManager::Initialize(UCanvasPanel* InRootCanvas)
{
	RootCanvas = InRootCanvas;

	if (InRootCanvas)
	{
		// WoW's base UI height (the reference for scaling)
		const float WowBaseHeight = 768.0f;

		// Get actual viewport size
		FVector2D ViewportSize = FVector2D(1024.0f, 768.0f); // Default fallback

		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->GetViewportSize(ViewportSize);
		}

		// Guard against viewport not ready (returns 0,0 early in startup)
		if (ViewportSize.X < 100.f || ViewportSize.Y < 100.f)
		{
			ViewportSize = FVector2D(1920.0f, 1080.0f); // Sensible default
			UE_LOG(LogWowFrame, Warning, TEXT("Viewport size not available yet, defaulting to %.0fx%.0f"), ViewportSize.X, ViewportSize.Y);
		}

		// UMG applies its own DPI scaling curve (Project Settings → Engine → User Interface).
		// At 720p the default curve yields ~0.67, so a widget at pixel 670 renders at
		// 670×0.67 ≈ 450 — nowhere near the bottom. We must divide our scale by the
		// UMG viewport scale so positions come out correct after DPI scaling.
		// Get the effective DPI scale that UMG applies to viewport widgets.
		// UGameViewportClient::GetDPIScale() may return 1.0 even when UMG
		// is applying a scale internally via the DPI curve.
		float ViewportDPIScale = 1.0f;
		if (GEngine && GEngine->GameViewport)
		{
			// Try multiple APIs to find the actual scale
			float Method1 = GEngine->GameViewport->GetDPIScale();

			// The viewport overlay uses SDPIScaler whose scale comes from
			// UUserInterfaceSettings::GetDPIScaleBasedOnSize
			FIntPoint ViewSize((int32)ViewportSize.X, (int32)ViewportSize.Y);
			float Method2 = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(ViewSize);

			ViewportDPIScale = Method2; // This is what actually gets applied
			if (ViewportDPIScale <= 0.f) ViewportDPIScale = 1.0f;

			UE_LOG(LogWowFrame, Warning, TEXT("DPI detection: GetDPIScale=%.3f, UISettings.GetDPIScaleBasedOnSize=%.3f, using=%.3f"),
				Method1, Method2, ViewportDPIScale);
		}

		// WoW scales by screen HEIGHT only — UIParent stretches to fill the full
		// viewport width.  This means on a 1920×1080 screen the UI scale is
		// 1080/768 = 1.40625 and UIParent is 1365×768 in WoW-coordinate units.
		// Divide by DPI scale so that WoW-coord → pixel conversion accounts for
		// the additional DPI transform UMG applies.
		RawViewportScale = ViewportSize.Y / WowBaseHeight;
		UIScale = RawViewportScale / ViewportDPIScale;

		// UIParent dimensions in WoW coordinate units (covers the full viewport)
		// UIScale already accounts for DPI, so ViewportSize / UIScale gives correct WoW coords.
		float UIParentW = ViewportSize.X / (ViewportSize.Y / WowBaseHeight); // Use raw viewport ratio
		float UIParentH = WowBaseHeight;
        UserInterfaceScale = FWowClientPreferences::GetUIScale();
        LaidOutInterfaceScale = UserInterfaceScale;
        RawViewportScale *= UserInterfaceScale;
        // The design-pixel scale grows with it too: only UIParent shrank, so at 120 % every frame
        // kept its size and everything anchored right or bottom ended at 1600 px of 1920 (Stefan:
        // minimap map left behind at the edge, tracker over the minimap, chat and bars too high).
        UIScale *= UserInterfaceScale;
        UIParentW /= UserInterfaceScale;
        UIParentH /= UserInterfaceScale;

		UE_LOG(LogWowFrame, Log, TEXT("UI Scale: Viewport (%.0fx%.0f) -> UIParent %.1fx%.1f, scale %.3f"),
			ViewportSize.X, ViewportSize.Y, UIParentW, UIParentH, UIScale);

		// Initialize UIParent as a virtual frame that represents the screen
		// UIParent doesn't get a widget - it's just a coordinate reference point
		FWowFrameDef UIParentDef;
		UIParentDef.Name = TEXT("UIParent");
		UIParentDef.Type = EWowFrameType::Frame;
		UIParentDef.Width = UIParentW;   // Full viewport width in WoW units
		UIParentDef.Height = UIParentH;  // Always 768 WoW units tall
		UIParentDef.bVirtual = true; // Don't create a widget for this
		UIParentDef.Strata = EWowFrameStrata::MEDIUM;
		UIParentDef.FrameLevel = 0;

		// Register UIParent as a virtual frame and store its rect
		RegisterTemplate(TEXT("UIParent"), UIParentDef);

		// Store the rect so anchor lookups use the correct full-screen dimensions
		FrameRects.Add(TEXT("UIParent"), FFrameRect(0.0f, 0.0f, UIParentW, UIParentH));

		// Create a handle for UIParent but without a widget
		int64 UIParentHandle = NextHandle++;
		FFrameEntry UIParentEntry;
		UIParentEntry.Def = UIParentDef;
		UIParentEntry.Widget = nullptr; // No widget for virtual frames
		UIParentEntry.ParentHandle = -1;

		Frames.Add(UIParentHandle, MoveTemp(UIParentEntry));
		NameToHandle.Add(TEXT("UIParent"), UIParentHandle);

		// Check DPI / application scale — on Retina displays, Slate operates
		// in physical pixels while GetViewportSize returns logical pixels.
		float DPIScale = 1.0f;
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWindow())
		{
			DPIScale = GEngine->GameViewport->GetWindow()->GetNativeWindow()->GetDPIScaleFactor();
		}

		UE_LOG(LogWowFrame, Warning, TEXT("Initialized UIParent virtual frame (%.1fx%.1f WoW coordinates, viewport %.0fx%.0f, UIScale %.3f, DPIScale=%.3f, viewportDPI=%.3f)"),
			UIParentW, UIParentH, ViewportSize.X, ViewportSize.Y, UIScale, DPIScale, ViewportDPIScale);
	}

	// Register built-in virtual templates that WoW defines in Lua but not in XML.
	// These templates are referenced by inherits="..." but their Frame definitions
	// aren't in the MPQ's XML files (they're defined implicitly by FrameXML Lua code).
	{
		// FadingFrame template — used by ZoneTextFrame, SubZoneTextFrame etc.
		// Frames inheriting this should start hidden; OnUpdate runs FadingFrame_OnUpdate.
		FWowFrameDef FadingFrameDef;
		FadingFrameDef.Name = TEXT("FadingFrame");
		FadingFrameDef.Type = EWowFrameType::Frame;
		FadingFrameDef.bVirtual = true;
		FadingFrameDef.bHidden = true;
		FadingFrameDef.Width = 128.0f;
		FadingFrameDef.Height = 128.0f;
		FWowScriptHandler FadingOnLoad;
		FadingOnLoad.Event = TEXT("OnLoad");
		FadingOnLoad.Code = TEXT("FadingFrame_OnLoad(self);");
		FadingFrameDef.Scripts.Add(FadingOnLoad);
		FWowScriptHandler FadingOnUpdate;
		FadingOnUpdate.Event = TEXT("OnUpdate");
		FadingOnUpdate.Code = TEXT("FadingFrame_OnUpdate(self, elapsed);");
		FadingFrameDef.Scripts.Add(FadingOnUpdate);
		RegisterTemplate(TEXT("FadingFrame"), FadingFrameDef);
	}

	UE_LOG(LogWowFrame, Warning, TEXT("Frame manager initialized - WoW anchor positioning system ready"));
}

void FWowFrameManager::RegisterTemplate(const FString& Name, const FWowFrameDef& Def)
{
	Templates.Add(Name, Def);
	UE_LOG(LogWowFrame, Verbose, TEXT("Registered template: %s"), *Name);
}

int64 FWowFrameManager::FindFrame(const FString& Name) const
{
	const int64* Handle = NameToHandle.Find(Name);
	return Handle ? *Handle : -1;
}

FString FWowFrameManager::GetFrameName(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? Entry->Def.Name : FString();
}

const FWowFrameDef* FWowFrameManager::GetFrameDef(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? &Entry->Def : nullptr;
}

void FWowFrameManager::SetFrameVisible(int64 Handle, bool bVisible)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;
	if (!bVisible) HideShoppingTooltips(Handle);
	static const TCHAR* StandardPanels[] = {
		TEXT("CharacterFrame"), TEXT("FriendsFrame"), TEXT("QuestLogFrame"),
		TEXT("SpellBookFrame"), TEXT("TalentFrame"), TEXT("PlayerTalentFrame"), TEXT("GuildFrame"),
		TEXT("MailFrame"), TEXT("MerchantFrame"), TEXT("BankFrame"),
		TEXT("TradeFrame"), TEXT("InterfaceOptionsFrame"), TEXT("VideoOptionsFrame"),
		TEXT("AudioOptionsFrame"), TEXT("KeyBindingFrame"), TEXT("MacroFrame")
	};
	static const TCHAR* DialoguePanels[] = {
		TEXT("GossipFrame"), TEXT("QuestFrame"), TEXT("LootFrame")
	};

	if (bVisible)
	{
		auto IsNamedPanel = [&Entry](const TCHAR* const* Names, int32 Count)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				if (Entry->Def.Name.Equals(Names[Index], ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
			return false;
		};
		const bool bStandardPanel = IsNamedPanel(StandardPanels, UE_ARRAY_COUNT(StandardPanels));
		const bool bDialoguePanel = IsNamedPanel(DialoguePanels, UE_ARRAY_COUNT(DialoguePanels));
		const bool bWorldMap = Entry->Def.Name.Equals(TEXT("WorldMapFrame"), ESearchCase::IgnoreCase);
		if (bStandardPanel || bDialoguePanel || bWorldMap)
		{
			auto HideOtherPanels = [this, Handle](const TCHAR* const* Names, int32 Count)
			{
				for (int32 Index = 0; Index < Count; ++Index)
				{
					const int64 OtherHandle = FindFrame(Names[Index]);
					if (OtherHandle >= 0 && OtherHandle != Handle && IsFrameVisible(OtherHandle))
					{
						SetFrameVisible(OtherHandle, false);
					}
				}
			};
			if (bWorldMap) HideOtherPanels(StandardPanels, UE_ARRAY_COUNT(StandardPanels));
			if (bWorldMap) HideOtherPanels(DialoguePanels, UE_ARRAY_COUNT(DialoguePanels));
			const int64 WorldMapHandle = FindFrame(TEXT("WorldMapFrame"));
			if (WorldMapHandle >= 0 && WorldMapHandle != Handle && IsFrameVisible(WorldMapHandle))
			{
				SetFrameVisible(WorldMapHandle, false);
			}

			if (bStandardPanel)
			{
				SetFrameStrata(Handle, EWowFrameStrata::HIGH);
			}
			else if (bDialoguePanel)
			{
				SetFrameStrata(Handle, EWowFrameStrata::DIALOG);
			}
			else
			{
				SetFrameStrata(Handle, EWowFrameStrata::FULLSCREEN);
			}
		}
	}

    if (bVisible && Entry->Def.Name.StartsWith(TEXT("ContainerFrame")))
        SetFrameStrata(Handle, EWowFrameStrata::DIALOG);
	Entry->Def.bHidden = !bVisible;
	// Frames can share a flat canvas: showing a child must still respect every ancestor.
	bVisible = IsFrameEffectivelyShown(Handle) && !IsUnplacedFrame(Entry->Def);

	if (Entry->Widget.IsValid())
	{
		if (bVisible)
		{
			Entry->Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			Entry->Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (Entry->Def.Name.Equals(TEXT("CharacterFrame"), ESearchCase::IgnoreCase))
	{
		for (auto& Pair : NamedFontStringWidgets)
		{
			if (Pair.Key.StartsWith(TEXT("CharacterSheetOverlay")) && Pair.Value.IsValid())
			{
				Pair.Value->SetVisibility(bVisible
					? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
		for (auto& Pair : NamedTextureWidgets)
		{
			if (Pair.Key.StartsWith(TEXT("CharacterSheetOverlay")) && Pair.Value.IsValid())
			{
				Pair.Value->SetVisibility(bVisible
					? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
	}
	for (const TCHAR* UnitFramePrefix : {TEXT("TargetFrame"), TEXT("FocusFrame")})
	{
		if (!Entry->Def.Name.Equals(UnitFramePrefix, ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FString Prefix(UnitFramePrefix);
		for (const FString& TextureName : {
			Prefix + TEXT("Background"), Prefix + TEXT("NameBackground"),
			Prefix + TEXT("Portrait"), Prefix + TEXT("TextureFrameTexture")})
		{
			if (UImage* Texture = GetNamedTexture(TextureName))
			{
				Texture->SetRenderOpacity(bVisible ? 1.0f : 0.0f);
				Texture->SetVisibility(bVisible
					? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
		for (const FString& FontName : {
			Prefix + TEXT("TextureFrameName"), Prefix + TEXT("TextureFrameLevelText"),
			Prefix + TEXT("TextureFrameHealthBarText"),
			Prefix + TEXT("TextureFrameManaBarText")})
		{
			if (UTextBlock* Label = GetNamedFontString(FontName))
			{
				Label->SetVisibility(bVisible
					? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
	}

	// Preserve each descendant's own Show/Hide state while applying the hierarchy.
	TArray<int64> ToPropagate;
	TSet<int64> Visited;
	ToPropagate.Add(Handle);
	while (!ToPropagate.IsEmpty())
	{
		const int64 Current = ToPropagate.Pop();
		if (Visited.Contains(Current)) continue;
		Visited.Add(Current);
		for (auto& Pair : Frames)
		{
			if (Pair.Value.ParentHandle != Current) continue;
			if (Pair.Value.Widget.IsValid())
			{
				Pair.Value.Widget->SetVisibility(IsFrameEffectivelyShown(Pair.Key)
					? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			}
			ToPropagate.Add(Pair.Key);
		}
	}

	bool bPanelOccludesChatChrome = false;
	auto AnyNamedPanelVisible = [this](const TCHAR* const* Names, int32 Count)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int64 PanelHandle = FindFrame(Names[Index]);
			if (PanelHandle >= 0 && IsFrameVisible(PanelHandle))
			{
				return true;
			}
		}
		return false;
	};
	bPanelOccludesChatChrome =
		AnyNamedPanelVisible(StandardPanels, UE_ARRAY_COUNT(StandardPanels)) ||
		AnyNamedPanelVisible(DialoguePanels, UE_ARRAY_COUNT(DialoguePanels));
	const int64 WorldMapHandle = FindFrame(TEXT("WorldMapFrame"));
	bPanelOccludesChatChrome |= WorldMapHandle >= 0 && IsFrameVisible(WorldMapHandle);

	auto SetChatChromeWidgetVisible = [this, bPanelOccludesChatChrome](const FString& FrameName,
		bool bNormallyVisible)
	{
		if (UWidget* Widget = GetWidgetForHandle(FindFrame(FrameName)))
		{
			// Hidden chat (the drawer arrow) keeps its tabs and buttons hidden too: this refresh
			// put ChatFrame1Tab back as a bare grey box at the screen edge.
			Widget->SetVisibility(!bPanelOccludesChatChrome && bNormallyVisible && !bChatWindowHidden
				? ESlateVisibility::SelfHitTestInvisible
				: ESlateVisibility::Collapsed);
		}
	};
	for (int32 ChatIndex = 1; ChatIndex <= 10; ++ChatIndex)
	{
		// Tabs the dock layout hid (overflow, undocked) stay hidden: showing them here left
		// an unplaced tab as a grey box at the screen edge that opened the tab menu.
		const FString TabName = FString::Printf(TEXT("ChatFrame%dTab"), ChatIndex);
		// (Checking IsFrameVisible here also caught tabs that FrameXML's chat fade hides, and
		// General / Combat Log disappeared - with them the friends button placed above the tab.)
		SetChatChromeWidgetVisible(TabName, EnabledChatTabs.Contains(ChatIndex) && !LayoutHiddenChatTabs.Contains(ChatIndex));
	}
	// r270 (Stefan 13.10, chat tabs missing; r269 log: ChatFrame1Tab/2Tab shown and laid out, but their
	// parent GeneralDockManager collapsed): the dock that holds the tabs follows the tabs' rules.
	SetChatChromeWidgetVisible(TEXT("GeneralDockManager"), true);
	SetChatChromeWidgetVisible(TEXT("FriendsMicroButton"), true);
	SetChatChromeWidgetVisible(TEXT("ChatFrameMenuButton"), true);
	SetChatChromeWidgetVisible(TEXT("ChatFrame1ButtonFrame"), true);
}

static bool GChatHiddenDiagLogged=false;

void FWowFrameManager::SetChatWindowHidden(bool bHidden)
{
    if(bChatWindowHidden==bHidden) return;
    GChatHiddenDiagLogged=false;
    bChatWindowHidden=bHidden;
    if(bHidden)
    {
        bChatTabContextMenuOpen=false;
        bCombatLogFilterMenuOpen=false;
        bChatResizeActive=false;
        CloseEditBox(FindFrame(TEXT("ChatFrame1EditBox")),false);
        ApplyChatWindowHidden();
    }
    else
    {
        for(auto& Pair:HiddenChatWidgets)
            if(Pair.Key.IsValid()) Pair.Key->SetVisibility(Pair.Value);
        HiddenChatWidgets.Empty();
        SetActiveChatTab(ActiveChatTabIndex);
    }
}

void FWowFrameManager::ApplyChatWindowHidden()
{
    if(!bChatWindowHidden) return;
    auto IsChat=[](const FString& Name) {
        return Name.StartsWith(TEXT("Chat")) || Name.StartsWith(TEXT("GeneralDock")) ||
            Name.StartsWith(TEXT("CombatLogFilter")) || Name==TEXT("FriendsMicroButton");
    };
    auto Hide=[this](UWidget* Widget) {
        if(!Widget) return;
        const TWeakObjectPtr<UWidget> Key(Widget);
        if(!HiddenChatWidgets.Contains(Key)) HiddenChatWidgets.Add(Key,Widget->GetVisibility());
        Widget->SetVisibility(ESlateVisibility::Collapsed);
    };
    for(auto& Pair:Frames) if(IsChat(Pair.Value.Def.Name)) Hide(Pair.Value.Widget.Get());
    for(auto& Pair:NamedTextureWidgets) if(IsChat(Pair.Key)) Hide(Pair.Value.Get());
    // Diagnostic for the box left at the screen edge under the hide-chat arrow: log what is
    // still drawn in the bottom-left strip (once per hide; this runs every tick).
    if(GChatHiddenDiagLogged) return;
    GChatHiddenDiagLogged=true;
    for(auto& Pair:NamedTextureWidgets)
    {
        UWidget* Widget=Pair.Value.Get();
        if(!Widget||!Widget->IsVisible()||Widget->GetRenderOpacity()<=0.01f) continue;
        const FGeometry& Geo=Widget->GetCachedGeometry();
        const FVector2D Pos=Geo.GetAbsolutePosition(), Size=Geo.GetAbsoluteSize();
        if(Size.X<=0||Pos.X>160.f||Size.X>600.f) continue;
        UE_LOG(LogWowFrame,Log,TEXT("Chat hidden: texture near the left edge: '%s' at (%.0f,%.0f) size %.0fx%.0f"),
            *Pair.Key,Pos.X,Pos.Y,Size.X,Size.Y);
    }
    for(auto& Pair:Frames)
    {
        UWidget* Widget=Pair.Value.Widget.Get();
        if(!Widget||!Widget->IsVisible()||IsChat(Pair.Value.Def.Name)) continue;
        const FGeometry& Geo=Widget->GetCachedGeometry();
        const FVector2D Pos=Geo.GetAbsolutePosition(), Size=Geo.GetAbsoluteSize();
        if(Size.X<=0||Pos.X>120.f||Pos.X+Size.X<0.f||Size.X>600.f) continue;
        UE_LOG(LogWowFrame,Log,TEXT("Chat hidden: still drawn near the left edge: '%s' at (%.0f,%.0f) size %.0fx%.0f"),
            *Pair.Value.Def.Name,Pos.X,Pos.Y,Size.X,Size.Y);
    }
}

bool FWowFrameManager::IsFrameEffectivelyShown(int64 Handle) const
{
    const FFrameEntry* Entry = Frames.Find(Handle);
    if (!Entry) return false;
    if(bChatWindowHidden && (Entry->Def.Name.StartsWith(TEXT("Chat")) ||
        Entry->Def.Name.StartsWith(TEXT("GeneralDock")) || Entry->Def.Name.StartsWith(TEXT("CombatLogFilter")) ||
        Entry->Def.Name==TEXT("FriendsMicroButton"))) return false;
    int32 Remaining = Frames.Num() + 1;
    while (Entry && --Remaining > 0)
    {
        if (Entry->Def.bHidden) return false;
        Entry = Frames.Find(Entry->ParentHandle);
    }
    return Remaining > 0; // Malformed parent cycles must not expose hidden UI.
}

bool FWowFrameManager::IsFrameVisible(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? !Entry->Def.bHidden : false;
}

bool FWowFrameManager::IsFrameActuallyVisible(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry || !Entry->Widget.IsValid())
	{
		return false;
	}
	const ESlateVisibility Visibility = Entry->Widget->GetVisibility();
	return Visibility != ESlateVisibility::Collapsed && Visibility != ESlateVisibility::Hidden;
}

void FWowFrameManager::SanitizeBackpackOnlyContainers()
{
	// The current runtime only has reliable data for the backpack container.
	// FrameXML may still create/show the reusable extra container frames; hide
	// them so the player does not get stacked, empty, cut-off bag windows.
	int32 HiddenCount = 0;
	// A player can have the fixed backpack plus four equipped bags. Frames above
	// that range are reusable stock templates and must never appear by themselves.
	for (int32 ContainerIndex = 6; ContainerIndex <= 13; ++ContainerIndex)
	{
		const int64 ContainerHandle = FindFrame(
			FString::Printf(TEXT("ContainerFrame%d"), ContainerIndex));
		if (ContainerHandle >= 0 &&
			(IsFrameVisible(ContainerHandle) || IsFrameActuallyVisible(ContainerHandle)))
		{
			SetFrameVisible(ContainerHandle, false);
			++HiddenCount;
		}
	}

	// The backpack is the fixed, non-replaceable container. Keep it attached to
	// the lower-right HUD corner above the action bar, matching the stock client.
	// Reapplying this after FrameXML events also prevents reusable container
	// frames from leaving it stranded halfway up the screen.
	const int64 BackpackHandle = FindFrame(TEXT("ContainerFrame1"));
	if (BackpackHandle >= 0)
	{
		SetFrameSize(BackpackHandle, 192.0f, 256.0f);
		if (UWidget* BackpackWidget = GetWidgetForHandle(BackpackHandle))
		{
			BackpackWidget->SetRenderTransformPivot(FVector2D(1.0f, 1.0f));
			BackpackWidget->SetRenderScale(FVector2D(0.75f));
		}
		// ContainerFrame1 is authored as the largest reusable container. The stock
		// Lua generator hides the unused atlas sections and item buttons for the
		// fixed 16-slot backpack, but those template widgets can remain rendered in
		// UMG after the frame is resized. Collapse them explicitly so descendants
		// outside the 256-unit backpack do not draw below the action bar.
		for (const TCHAR* TextureName : {
			TEXT("ContainerFrame1BackgroundMiddle1"),
			TEXT("ContainerFrame1BackgroundMiddle2"),
			TEXT("ContainerFrame1BackgroundBottom") })
		{
			if (UImage* Texture = GetNamedTexture(TextureName))
			{
				Texture->SetVisibility(ESlateVisibility::Collapsed);
				Texture->SetRenderOpacity(0.0f);
			}
		}

		for (int32 ItemIndex = 17; ItemIndex <= 36; ++ItemIndex)
		{
			const int64 ItemHandle = FindFrame(
				FString::Printf(TEXT("ContainerFrame1Item%d"), ItemIndex));
			if (ItemHandle >= 0)
			{
				SetFrameVisible(ItemHandle, false);
			}
		}

		// Some inherited template regions are materialized as direct images without
		// retaining the same named-region identity used by Lua. Remove any direct
		// image that starts below the fixed backpack artwork as a final guard against
		// the reusable middle/bottom atlas leaking outside the resized frame.
		if (UCanvasPanel* BackpackCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(BackpackHandle)))
		{
			const float BackpackHeight = 256.0f;
			for (int32 ChildIndex = 0; ChildIndex < BackpackCanvas->GetChildrenCount(); ++ChildIndex)
			{
				UImage* DirectImage = Cast<UImage>(BackpackCanvas->GetChildAt(ChildIndex));
				UCanvasPanelSlot* ImageSlot = DirectImage
					? Cast<UCanvasPanelSlot>(DirectImage->Slot)
					: nullptr;
				const UObject* BrushResource = DirectImage
					? DirectImage->GetBrush().GetResourceObject()
					: nullptr;
				const FString ResourceName = BrushResource ? BrushResource->GetName() : FString();
				const bool bReusableBagAtlas =
					ResourceName.Contains(TEXT("UI_Bag_Components"), ESearchCase::IgnoreCase) ||
					ResourceName.Contains(TEXT("UI-Bag-Components"), ESearchCase::IgnoreCase);
				const bool bBackpackAtlas =
					ResourceName.Contains(TEXT("UI_BackpackBackground"), ESearchCase::IgnoreCase) ||
					ResourceName.Contains(TEXT("UI-BackpackBackground"), ESearchCase::IgnoreCase);
				const bool bSingleSlotAtlas =
					ResourceName.Contains(TEXT("UI_Bag_1Slot"), ESearchCase::IgnoreCase) ||
					ResourceName.Contains(TEXT("UI-Bag-1Slot"), ESearchCase::IgnoreCase);
				const bool bStartsBelowBackpack =
					ImageSlot && ImageSlot->GetPosition().Y / UIScale >= BackpackHeight - 1.0f;
				if (DirectImage && bBackpackAtlas && ImageSlot)
				{
					ImageSlot->SetAutoSize(false);
					ImageSlot->SetSize(FVector2D(256.0f * UIScale, 256.0f * UIScale));
					DirectImage->SetRenderOpacity(1.0f);
					DirectImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				}
				else if (DirectImage && (bReusableBagAtlas || bSingleSlotAtlas || bStartsBelowBackpack))
				{
					DirectImage->SetVisibility(ESlateVisibility::Collapsed);
					DirectImage->SetRenderOpacity(0.0f);
				}
			}
		}

		UpdateBackpackMoney(BackpackMoneyCopper);
	}

	// Stock container windows fill a vertical column from the action bar upward.
	// When the next equipped bag no longer fits, continue in a new column to the
	// left. ContainerFrame1 is always the backpack and therefore owns the base.
	float AvailableHeight = 768.0f;
	if (const FFrameEntry* UIParent = Frames.Find(FindFrame(TEXT("UIParent")));
		UIParent && UIParent->Def.Height > 0.0f)
	{
		AvailableHeight = UIParent->Def.Height;
	}
	constexpr float ContainerScale = 0.75f;
	constexpr float ContainerGap = 4.0f;
	constexpr float BaseBottomOffset = 88.0f;
	constexpr float ScreenMargin = 16.0f;
	// r322 (Stefan 14.10: closing one bag moved the others to the left): FrameXML's
	// updateContainerFrameAnchors keeps the bags left of the right-side action bars
	// (CONTAINER_OFFSET_X: 2*45+3 with both bars, 45+3 with one) while this layout used a fixed 16,
	// so the bags jumped between the two whenever the other one ran last. Use the same offset.
	// This layout is the only one (TickWidgetEvents turns FrameXML's own off), so it also takes the
	// VR headset offset the old updateContainerFrameAnchors hook added (a quarter of the screen).
	const float SideBarOffset = IsFrameActuallyVisible(FindFrame(TEXT("MultiBarLeft"))) ? 93.0f
		: IsFrameActuallyVisible(FindFrame(TEXT("MultiBarRight"))) ? 48.0f : 0.0f;
	float VrOffset = 0.0f;
	if (FWowUiVrHost::IsActive())
		if (const FFrameEntry* Root = Frames.Find(FindFrame(TEXT("UIParent"))); Root && Root->Def.Width > 0.0f)
			VrOffset = FMath::FloorToFloat(Root->Def.Width * 0.27f);
	float ColumnRightOffset = -(FMath::Max(ScreenMargin, SideBarOffset) + VrOffset);
	float ColumnBottomOffset = BaseBottomOffset;
	float ColumnWidth = 0.0f;
	for (int32 ContainerIndex = 1; ContainerIndex <= 5; ++ContainerIndex)
	{
		const int64 ContainerHandle = FindFrame(
			FString::Printf(TEXT("ContainerFrame%d"), ContainerIndex));
		FFrameEntry* ContainerEntry = Frames.Find(ContainerHandle);
		if (!ContainerEntry || !IsFrameActuallyVisible(ContainerHandle))
		{
			continue;
		}

		// The stock container template is a tall reusable atlas. WoW clips it to
		// the generated frame height, while UMG otherwise lets those atlas pieces
		// draw far outside the bag. Recreate the generated four-column layout for
		// equipped bags and clip the backing art to the resulting window.
		if (UWidget* ContainerWidget = GetWidgetForHandle(ContainerHandle))
		{
			ContainerWidget->SetClipping(EWidgetClipping::ClipToBoundsAlways);
		}
		if (ContainerIndex > 1)
		{
			int32 VisibleSlotCount = 0;
			for (int32 ItemIndex = 1; ItemIndex <= 36; ++ItemIndex)
			{
				const int64 ItemHandle = FindFrame(FString::Printf(
					TEXT("ContainerFrame%dItem%d"), ContainerIndex, ItemIndex));
				if (ItemHandle >= 0 &&
					(IsFrameVisible(ItemHandle) || IsFrameActuallyVisible(ItemHandle)))
				{
					VisibleSlotCount = ItemIndex;
				}
			}

			if (VisibleSlotCount > 0)
			{
				const int32 RowCount = FMath::DivideAndRoundUp(VisibleSlotCount, 4);
				const bool bHasTwoSlotTopRow = VisibleSlotCount % 4 == 2;
				const float TopHeight = bHasTwoSlotTopRow ? 72.0f : (RowCount == 1 ? 86.0f : 94.0f);
				float MiddleHeights[2] = {0.0f, 0.0f};
				int32 RemainingRows = FMath::Max(0, RowCount - 1);
				for (int32 MiddleIndex = 0; MiddleIndex < UE_ARRAY_COUNT(MiddleHeights) && RemainingRows > 0; ++MiddleIndex)
				{
					constexpr int32 RowsPerMiddleTexture = 6;
					if (RemainingRows > RowsPerMiddleTexture)
					{
						MiddleHeights[MiddleIndex] =
							static_cast<float>(RowsPerMiddleTexture) * 41.0f + 0.353515625f;
						RemainingRows -= RowsPerMiddleTexture;
					}
					else
					{
						MiddleHeights[MiddleIndex] = static_cast<float>(RemainingRows) * 41.0f - 9.0f;
						RemainingRows = 0;
					}
				}
				const float MiddleHeight = MiddleHeights[0] + MiddleHeights[1];
				const float GeneratedHeight = TopHeight + MiddleHeight + 10.0f;
				SetFrameSize(ContainerHandle, 192.0f, GeneratedHeight);
				ContainerEntry = Frames.Find(ContainerHandle);

				for (int32 ItemIndex = 1; ItemIndex <= VisibleSlotCount; ++ItemIndex)
				{
					const int64 ItemHandle = FindFrame(FString::Printf(
						TEXT("ContainerFrame%dItem%d"), ContainerIndex, ItemIndex));
					if (ItemHandle < 0)
					{
						continue;
					}

					const int32 GridIndex = ItemIndex - 1;
					const int32 RowFromBottom = GridIndex / 4;
					const int32 Column = GridIndex % 4;
					FWowAnchor SlotAnchor;
					SlotAnchor.Point = EWowAnchorPoint::BOTTOMRIGHT;
					SlotAnchor.RelativeTo = ContainerEntry->Def.Name;
					SlotAnchor.RelativePoint = EWowAnchorPoint::BOTTOMRIGHT;
					SlotAnchor.OffsetX = -(12.0f + static_cast<float>(Column) * 41.0f);
					SlotAnchor.OffsetY = 9.0f + static_cast<float>(RowFromBottom) * 41.0f;
					TArray<FWowAnchor> SlotAnchors;
					SlotAnchors.Add(SlotAnchor);
					SetFrameAnchors(ItemHandle, SlotAnchors);
				}

				if (UCanvasPanel* ContainerCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(ContainerHandle)))
				{
					TArray<TPair<UImage*, UCanvasPanelSlot*>> BagAtlasImages;
					for (int32 ChildIndex = 0; ChildIndex < ContainerCanvas->GetChildrenCount(); ++ChildIndex)
					{
						UImage* Image = Cast<UImage>(ContainerCanvas->GetChildAt(ChildIndex));
						UCanvasPanelSlot* ImageSlot = Image ? Cast<UCanvasPanelSlot>(Image->Slot) : nullptr;
						const UObject* Resource = Image ? Image->GetBrush().GetResourceObject() : nullptr;
						const FString ResourceName = Resource ? Resource->GetName() : FString();
						if (Image && ImageSlot &&
							(ResourceName.Contains(TEXT("UI_Bag_Components"), ESearchCase::IgnoreCase) ||
							 ResourceName.Contains(TEXT("UI-Bag-Components"), ESearchCase::IgnoreCase)))
						{
							BagAtlasImages.Emplace(Image, ImageSlot);
						}
						else if (Image &&
							(ResourceName.Contains(TEXT("UI_Bag_1Slot"), ESearchCase::IgnoreCase) ||
							 ResourceName.Contains(TEXT("UI-Bag-1Slot"), ESearchCase::IgnoreCase)))
						{
							Image->SetVisibility(ESlateVisibility::Collapsed);
							Image->SetRenderOpacity(0.0f);
						}
					}

					for (int32 AtlasIndex = 0; AtlasIndex < BagAtlasImages.Num(); ++AtlasIndex)
					{
						UImage* Image = BagAtlasImages[AtlasIndex].Key;
						UCanvasPanelSlot* ImageSlot = BagAtlasImages[AtlasIndex].Value;
						const bool bTop = AtlasIndex == 0;
						const bool bMiddle = AtlasIndex == 1 || AtlasIndex == 2;
						const bool bBottom = AtlasIndex == 3;
						const int32 MiddleIndex = AtlasIndex - 1;
						const float ThisMiddleHeight = bMiddle ? MiddleHeights[MiddleIndex] : 0.0f;
						if ((!bTop && !bMiddle && !bBottom) || (bMiddle && ThisMiddleHeight <= 0.0f))
						{
							Image->SetVisibility(ESlateVisibility::Collapsed);
							Image->SetRenderOpacity(0.0f);
							continue;
						}

						float TextureTop = 0.0f;
						float TextureBottom = 1.0f;
						float ImageHeight = TopHeight;
						float ImageTop = 0.0f;
						if (bTop)
						{
							TextureTop = bHasTwoSlotTopRow ? 0.189453125f : 0.00390625f;
							TextureBottom = bHasTwoSlotTopRow
								? 0.330078125f
								: (RowCount == 1 ? 0.16796875f : 0.18359375f);
						}
						else if (bMiddle)
						{
							TextureTop = 0.353515625f;
							TextureBottom = TextureTop + ThisMiddleHeight / 512.0f;
							ImageHeight = ThisMiddleHeight;
							ImageTop = TopHeight + (MiddleIndex > 0 ? MiddleHeights[0] : 0.0f);
						}
						else
						{
							TextureTop = 0.330078125f;
							TextureBottom = 0.349609375f;
							ImageHeight = 10.0f;
							ImageTop = TopHeight + MiddleHeight;
						}

						FSlateBrush Brush = Image->GetBrush();
						Brush.SetUVRegion(FBox2D(
							FVector2D(0.0f, TextureTop),
							FVector2D(1.0f, TextureBottom)));
						Brush.ImageSize = FVector2D(256.0f, ImageHeight);
						Image->SetBrush(Brush);

						ImageSlot->SetAnchors(FAnchors(0.0f, 0.0f));
						ImageSlot->SetAlignment(FVector2D::ZeroVector);
						ImageSlot->SetAutoSize(false);
						ImageSlot->SetPosition(FVector2D(
							-64.0f * UIScale,
							ImageTop * UIScale));
						ImageSlot->SetSize(FVector2D(
							256.0f * UIScale,
							ImageHeight * UIScale));
						Image->SetRenderOpacity(1.0f);
						Image->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
					}
				}
			}
		}

		const float RenderedWidth = FMath::Max(ContainerEntry->Def.Width, 192.0f) * ContainerScale;
		const float RenderedHeight = FMath::Max(ContainerEntry->Def.Height, 64.0f) * ContainerScale;
		if (ColumnBottomOffset > BaseBottomOffset &&
			ColumnBottomOffset + RenderedHeight > AvailableHeight - ScreenMargin)
		{
			ColumnRightOffset -= ColumnWidth + ContainerGap;
			ColumnBottomOffset = BaseBottomOffset;
			ColumnWidth = 0.0f;
		}

		if (UWidget* ContainerWidget = GetWidgetForHandle(ContainerHandle))
		{
			ContainerWidget->SetRenderTransformPivot(FVector2D(1.0f, 1.0f));
			ContainerWidget->SetRenderScale(FVector2D(ContainerScale));
		}
		FWowAnchor ContainerAnchor;
		ContainerAnchor.Point = EWowAnchorPoint::BOTTOMRIGHT;
		ContainerAnchor.RelativeTo = TEXT("UIParent");
		ContainerAnchor.RelativePoint = EWowAnchorPoint::BOTTOMRIGHT;
		ContainerAnchor.OffsetX = ColumnRightOffset;
		ContainerAnchor.OffsetY = ColumnBottomOffset;
		TArray<FWowAnchor> ContainerAnchors;
		ContainerAnchors.Add(ContainerAnchor);
		SetFrameAnchors(ContainerHandle, ContainerAnchors);

		ColumnBottomOffset += RenderedHeight + ContainerGap;
		ColumnWidth = FMath::Max(ColumnWidth, RenderedWidth);
	}

	FitContainerTitles();

	if (HiddenCount > 0)
	{
		UE_LOG(LogWowFrame, Log, TEXT("SanitizeBackpackOnlyContainers: hid %d inactive container frames"), HiddenCount);
	}
}

void FWowFrameManager::UpdateClockTooltip()
{
	// TimeManagerClockButton tooltip: "Time Info", realm time and local time.
	const UImage* Clock = GetNamedTexture(TEXT("MinimapNativeClockBackground"));
	const bool bOver = Clock && Clock->IsVisible() && FSlateApplication::IsInitialized()
		&& Clock->GetCachedGeometry().IsUnderLocation(WowUiCursorAbsolute());
	const int64 Tooltip = FindFrame(TEXT("GameTooltip"));
	if (Tooltip < 0) return;
	if (!bOver)
	{
		if (bClockTooltipShown)
		{
			bClockTooltipShown = false;
			const FTooltipState* State = GetTooltipState(Tooltip);
			if (State && State->OwnerHandle == FindFrame(TEXT("MinimapCluster"))) SetFrameVisible(Tooltip, false);
		}
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (bClockTooltipShown && Now < NextClockTooltipRefresh) return;
	NextClockTooltipRefresh = Now + 1.0;
	bClockTooltipShown = true;
	auto Global = [this](const TCHAR* Name, const TCHAR* Fallback)
	{
		const FString Value = EventSystem ? EventSystem->GetGlobalString(Name) : FString();
		return Value.IsEmpty() ? FString(Fallback) : Value;
	};
	const int32 RealmMinutes = FMath::FloorToInt(MinimapTimeOfDayMinutes) % 1440;
	const FDateTime Local = FDateTime::Now();
	TooltipSetOwner(Tooltip, FindFrame(TEXT("MinimapCluster")), TEXT("ANCHOR_BOTTOMLEFT"));
	TooltipAddLine(Tooltip, Global(TEXT("TIMEMANAGER_TOOLTIP_TITLE"), TEXT("Time Info")), 1.f, 1.f, 1.f, false);
	TooltipAddDoubleLine(Tooltip, Global(TEXT("TIMEMANAGER_TOOLTIP_REALMTIME"), TEXT("Realm time:")),
		FString::Printf(TEXT("%02d:%02d"), RealmMinutes / 60, RealmMinutes % 60), 1.f, .82f, 0.f, 1.f, 1.f, 1.f);
	TooltipAddDoubleLine(Tooltip, Global(TEXT("TIMEMANAGER_TOOLTIP_LOCALTIME"), TEXT("Local time:")),
		FString::Printf(TEXT("%02d:%02d"), Local.GetHour(), Local.GetMinute()), 1.f, .82f, 0.f, 1.f, 1.f, 1.f);
	// Sun and moon times in realm time. The day is the same everywhere in the world, as the sky
	// draws it (AWowSkyManager: WowDaySunriseMinutes 330 / WowDaySunsetMinutes 1110). With the
	// experimental "realisticmoon" the moon follows the sun's path delayed by phase x 24 h.
	{
		constexpr float SunriseMinutes = 330.f, SunsetMinutes = 1110.f;
		auto Hm = [](float Minutes)
		{
			const int32 M = FMath::RoundToInt(FMath::Fmod(Minutes + 2880.f, 1440.f)) % 1440;
			return FString::Printf(TEXT("%02d:%02d"), M / 60, M % 60);
		};
		TooltipAddDoubleLine(Tooltip, TEXT("Sunrise:"), Hm(SunriseMinutes), 1.f, .82f, 0.f, 1.f, 1.f, 1.f);
		TooltipAddDoubleLine(Tooltip, TEXT("Sunset:"), Hm(SunsetMinutes), 1.f, .82f, 0.f, 1.f, 1.f, 1.f);
		if (FWowClientPreferences::GetWorldOption(TEXT("realisticmoon")))
		{
			// Same mean synodic cycle as WowLunarPhase::Cycle (new moon 2000-01-06 18:14 UTC).
			double Phase = FMath::Fmod((FDateTime::UtcNow() - FDateTime(2000, 1, 6, 18, 14)).GetTotalDays() / 29.530588853, 1.0);
			if (Phase < 0.0) Phase += 1.0;
			const float Delay = static_cast<float>(Phase) * 1440.f;
			TooltipAddDoubleLine(Tooltip, TEXT("Moonrise:"), Hm(SunriseMinutes + Delay), 1.f, .82f, 0.f, 1.f, 1.f, 1.f);
			TooltipAddDoubleLine(Tooltip, TEXT("Moonset:"), Hm(SunsetMinutes + Delay), 1.f, .82f, 0.f, 1.f, 1.f, 1.f);
		}
	}
	SetFrameStrata(Tooltip, EWowFrameStrata::TOOLTIP);
	SetFrameVisible(Tooltip, true);
}

FString FWowFrameManager::ResolveXmlText(const FString& Text) const
{
	// XML text="ASSEMBLING_GROUP" names a GlobalStrings.lua entry, as in the client.
	if (Text.IsEmpty() || !EventSystem) return Text;
	for (const TCHAR C : Text)
		if (!(FChar::IsUpper(C) || FChar::IsDigit(C) || C == TEXT('_'))) return Text;
	const FString Value = EventSystem->GetGlobalString(Text);
	return Value.IsEmpty() ? Text : Value;
}

void FWowFrameManager::RefreshPendingBagTitles()
{
	// GetBagName answers "Bag" until the bag's item template has arrived, and
	// ContainerFrame only sets the title when the bag opens. Retry those titles.
	const double Now = FPlatformTime::Seconds();
	if (!EventSystem || Now < NextBagTitleRefresh) return;
	NextBagTitleRefresh = Now + 0.5;
	bool bPending = false;
	for (int32 ContainerIndex = 1; ContainerIndex <= 5 && !bPending; ++ContainerIndex)
	{
		const UTextBlock* Title = GetNamedFontString(FString::Printf(TEXT("ContainerFrame%dName"), ContainerIndex));
		bPending = Title && Title->IsVisible() && Title->GetText().ToString() == TEXT("Bag");
	}
	if (!bPending) return;
	EventSystem->ExecuteLuaString(TEXT(
		"for i = 1, (NUM_CONTAINER_FRAMES or 13) do "
		"local f, t = _G['ContainerFrame'..i], _G['ContainerFrame'..i..'Name'] "
		"if f and t and f:IsShown() and t:GetText() == 'Bag' then "
		"local n = GetBagName(f:GetID()) if n and n ~= 'Bag' then t:SetText(n) end end end"),
		TEXT("=BagTitleRefresh"));
	FitContainerTitles();
}

void FWowFrameManager::FitContainerTitles()
{
	// Bag titles ("Small Brown Pouch", "Backpack") sit on the title plate between the
	// portrait and the close button: stock FontString $parentName is 112x12 at
	// TOPLEFT(47,-10). Centre the name there, nudge it up so it stays inside the
	// plate, and shrink the font for long bag names instead of running under the X.
	static TMap<FString, int32> BaseFontSizes;
	TSharedPtr<FSlateFontMeasure> FontMeasure;
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
	{
		FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	}
	// Slate's line height for a size-N font is taller than WoW's N-unit FontString, so the
	// plate box is 16 units tall and the name uses ~85% of the template size; otherwise the
	// descenders ("g", "y") were cut off and the name looked too big for the plate.
	// The dark title plate itself runs from the portrait's edge to the close button and from
	// the frame's top rim down: centre the name on that, both ways (Stefan 09.10).
	// (r125: 4 units right and 1 down - the name ran onto the portrait ring and the top rim.)
	constexpr float PlateLeft = 44.0f;
	constexpr float PlateTop = 6.0f; // (Stefan 10.10: a few pixels lower)
	constexpr float PlateWidth = 112.0f;
	constexpr float PlateHeight = 20.0f;
	for (int32 ContainerIndex = 1; ContainerIndex <= 5; ++ContainerIndex)
	{
		const FString TitleName = FString::Printf(TEXT("ContainerFrame%dName"), ContainerIndex);
		UTextBlock* Title = GetNamedFontString(TitleName);
		UCanvasPanelSlot* TitleSlot = Title ? Cast<UCanvasPanelSlot>(Title->Slot) : nullptr;
		if (!TitleSlot)
		{
			continue;
		}
		TitleSlot->SetAnchors(FAnchors(0.0f, 0.0f));
		TitleSlot->SetAlignment(FVector2D::ZeroVector);
		TitleSlot->SetAutoSize(false);
		TitleSlot->SetPosition(FVector2D(PlateLeft, PlateTop) * UIScale);
		TitleSlot->SetSize(FVector2D(PlateWidth, PlateHeight) * UIScale);
		Title->SetJustification(ETextJustify::Center);
		Title->SetClipping(EWidgetClipping::ClipToBounds);

		FSlateFontInfo Font = Title->GetFont();
		int32& BaseSize = BaseFontSizes.FindOrAdd(TitleName);
		if (BaseSize <= 0)
		{
			BaseSize = FMath::Max(1, FMath::RoundToInt(Font.Size * 0.85f));
		}
		Font.Size = BaseSize;
		if (FontMeasure.IsValid())
		{
			const FString Text = Title->GetText().ToString();
			const float MaxWidth = (PlateWidth - 4.0f) * UIScale;
			const int32 MinSize = FMath::Max(6, FMath::RoundToInt(BaseSize * 0.6f));
			while (Font.Size > MinSize && FontMeasure->Measure(Text, Font).X > MaxWidth)
			{
				--Font.Size;
			}
		}
		if (Font.Size != Title->GetFont().Size)
		{
			Title->SetFont(Font);
		}
		// Vertical centring: the text block is top-aligned in its box, so move it down by half
		// the free height, then put the middle of the capitals (~0.70 em) on the box's middle.
		if (FontMeasure.IsValid())
		{
			const float LineH = static_cast<float>(FontMeasure->GetMaxCharacterHeight(Font));
			const float Descender = -static_cast<float>(FontMeasure->GetBaseline(Font));
			const float CapH = 0.70f * Font.Size * (96.f / 72.f);
			const float BoxH = PlateHeight * UIScale;
			const float CapCentreFromTop = (LineH - Descender) - CapH * .5f;
			Title->SetRenderTranslation(FVector2D(0.f, FMath::RoundToFloat(BoxH * .5f - CapCentreFromTop)));
		}
	}
}

void FWowFrameManager::UpdateBackpackMoney(uint32 TotalCopper)
{
	BackpackMoneyCopper = TotalCopper;
	const int64 BackpackHandle = FindFrame(TEXT("ContainerFrame1"));
	UCanvasPanel* BackpackCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(BackpackHandle));
	if (!BackpackCanvas)
	{
		return;
	}

	// MoneyFrame.lua normally builds the three denominations dynamically. Its
	// template children are incomplete in the standalone runtime, so suppress
	// them and provide the same compact numeric/icon row directly on the bag.
	for (auto& Pair : Frames)
	{
		if (Pair.Value.Def.Name.StartsWith(TEXT("ContainerFrame1MoneyFrame")))
		{
			Pair.Value.Def.bHidden = true;
			if (Pair.Value.Widget.IsValid())
			{
				Pair.Value.Widget->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	const uint32 Gold = TotalCopper / 10000;
	const uint32 Silver = (TotalCopper / 100) % 100;
	const uint32 Copper = TotalCopper % 100;
	const uint32 Values[] = {Gold, Silver, Copper};
	static const TCHAR* Labels[] = {
		TEXT("BackpackMoneyGoldText"),
		TEXT("BackpackMoneySilverText"),
		TEXT("BackpackMoneyCopperText")
	};
	static const TCHAR* Icons[] = {
		TEXT("BackpackMoneyGoldIcon"),
		TEXT("BackpackMoneySilverIcon"),
		TEXT("BackpackMoneyCopperIcon")
	};
	static const TCHAR* IconPaths[] = {
		TEXT("Interface\\MoneyFrame\\UI-GoldIcon"),
		TEXT("Interface\\MoneyFrame\\UI-SilverIcon"),
		TEXT("Interface\\MoneyFrame\\UI-CopperIcon")
	};

	// The copper icon ended past the money bar's right edge (Stefan 10.10): the row ends at ~166.
	const float GroupWidth = 42.0f;
	const float RowStartX = 43.0f;
	const float RowY = 215.0f; // (Stefan 10.10: a few pixels higher)
	for (int32 Denomination = 0; Denomination < 3; ++Denomination)
	{
		const bool bShowDenomination = Denomination == 2 || Values[Denomination] > 0;
		UTextBlock* ValueText = GetNamedFontString(Labels[Denomination]);
		if (!ValueText)
		{
			ValueText = NewObject<UTextBlock>(BackpackCanvas);
			BackpackCanvas->AddChildToCanvas(ValueText);
			NamedFontStringWidgets.Add(Labels[Denomination], ValueText);
		}
		if (UCanvasPanelSlot* TextSlot = Cast<UCanvasPanelSlot>(ValueText->Slot))
		{
			TextSlot->SetAnchors(FAnchors(0.0f, 0.0f));
			TextSlot->SetAlignment(FVector2D::ZeroVector);
			TextSlot->SetAutoSize(false);
			TextSlot->SetPosition(FVector2D(RowStartX + GroupWidth * Denomination, RowY) * UIScale);
			TextSlot->SetSize(FVector2D(25.0f, 16.0f) * UIScale);
			TextSlot->SetZOrder(32000);
		}
		ValueText->SetText(FText::AsNumber(Values[Denomination]));
		ValueText->SetJustification(ETextJustify::Right);
		ValueText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ValueText->SetShadowOffset(FVector2D(1.0f));
		ValueText->SetShadowColorAndOpacity(FLinearColor::Black);
		ValueText->SetVisibility(bShowDenomination
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed);
		FSlateFontInfo Font = ValueText->GetFont();
		Font.Size = FMath::Max(9, FMath::RoundToInt(9.0f * UIScale));
		ValueText->SetFont(Font);

		UImage* CoinIcon = GetNamedTexture(Icons[Denomination]);
		if (!CoinIcon)
		{
			CoinIcon = NewObject<UImage>(BackpackCanvas);
			BackpackCanvas->AddChildToCanvas(CoinIcon);
			NamedTextureWidgets.Add(Icons[Denomination], CoinIcon);
		}
		if (UCanvasPanelSlot* IconSlot = Cast<UCanvasPanelSlot>(CoinIcon->Slot))
		{
			IconSlot->SetAnchors(FAnchors(0.0f, 0.0f));
			IconSlot->SetAlignment(FVector2D::ZeroVector);
			IconSlot->SetAutoSize(false);
			IconSlot->SetPosition(FVector2D(RowStartX + 26.0f + GroupWidth * Denomination, RowY + 1.0f) * UIScale);
			IconSlot->SetSize(FVector2D(13.0f, 13.0f) * UIScale);
			IconSlot->SetZOrder(32001);
		}
		if (UTexture2D* Texture = LoadUITexture(IconPaths[Denomination]))
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = FVector2D(13.0f, 13.0f) * UIScale;
			Brush.DrawAs = ESlateBrushDrawType::Image;
			CoinIcon->SetBrush(Brush);
		}
		CoinIcon->SetColorAndOpacity(FLinearColor::White);
		CoinIcon->SetVisibility(bShowDenomination
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed);
	}
}

FWowFrameDef* FWowFrameManager::GetMutableFrameDef(int64 Handle)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? &Entry->Def : nullptr;
}

static void SyncCombatChatFrame(FWowFrameManager& Manager); // defined below
// r314 (Stefan 14.10: opening the chat from the speech-bubble menu or "Whisper" on a unit frame
// moved the chat window up ~50 units while its tabs and input line stayed, so the tabs sat inside
// the text): once the native chat layout has placed ChatFrame1 (tabs, input line, size grabber are
// native and follow that placement), FrameXML/addon SetPoint/SetSize on it is ignored. A recreated
// ChatFrame1 (UI reload) is a new widget and gets its XML placement again.
static TWeakObjectPtr<UWidget> GNativeChatFrameWidget;
static bool IsNativelyPlacedChatFrame(const FWowFrameDef& Def, const TWeakObjectPtr<UWidget>& Widget)
{
	return Def.Name == TEXT("ChatFrame1") && Widget.IsValid() && GNativeChatFrameWidget.Get() == Widget.Get();
}

void FWowFrameManager::SetFrameSize(int64 Handle, float W, float H)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;

	if (Entry->Def.Name.Contains(TEXT("MainMenu")))
	{
		UE_LOG(LogWowFrame, Warning, TEXT("SetFrameSize %s: %.0fx%.0f -> %.0fx%.0f (anchors=%d)"),
			*Entry->Def.Name, Entry->Def.Width, Entry->Def.Height, W, H, Entry->Def.Anchors.Num());
	}

	if (Entry->Def.Name == TEXT("ChatFrame2"))
	{
		Entry->Def.Width = W;
		Entry->Def.Height = H;
		SyncCombatChatFrame(*this);
		RefreshScrollingMessageLayout(Handle);
		return;
	}
	if (IsNativelyPlacedChatFrame(Entry->Def, Entry->Widget))
	{
		static int32 Logged = 0;
		if (Logged++ < 10)
			UE_LOG(LogWowFrame, Log, TEXT("ChatFrame1 SetSize %.0fx%.0f from Lua ignored (natively placed)"), W, H);
		return;
	}

	const bool bWasUnplaced = IsUnplacedFrame(Entry->Def);
	if (!FMath::IsNearlyEqual(Entry->Def.Width, W) || !FMath::IsNearlyEqual(Entry->Def.Height, H))
		PendingSizeChanged.Add(Handle); // r228
	Entry->Def.Width = W;
	Entry->Def.Height = H;
	if (bWasUnplaced != IsUnplacedFrame(Entry->Def) && Entry->Widget.IsValid())
		Entry->Widget->SetVisibility(!bWasUnplaced || !IsFrameEffectivelyShown(Handle)
			? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);

	// Clear cached frame rect for this frame and ALL descendants.
	// When a parent moves/resizes, all child absolute positions become stale.
	InvalidateFrameRectsRecursive(Handle);

	if (Entry->Widget.IsValid())
	{
		// Re-apply anchors to recalculate position with new size
		ApplyAnchors(Entry->Widget.Get(), Entry->Def);
		QueueDependentLayout(Handle); // r228

		// ContainerFrame_GenerateFrame resizes the reusable 256-wide template to
		// the 192-wide bag frame at runtime. WoW immediately reflows the frame's
		// layer regions and anchored children. UMG does not do that automatically,
		// so the 256-wide backpack atlas otherwise remains 64 units too far right
		// and gets cut off at the viewport edge.
		const FString ContainerSuffix = Entry->Def.Name.StartsWith(TEXT("ContainerFrame"))
			? Entry->Def.Name.Mid(14)
			: FString();
		if (!ContainerSuffix.IsEmpty() && ContainerSuffix.IsNumeric())
		{
			if (UCanvasPanel* ContainerCanvas = Cast<UCanvasPanel>(Entry->Widget.Get()))
			{
				const FFrameRect OwnerRect = GetFrameRect(Entry->Def.Name);
				for (const FWowLayer& Layer : Entry->Def.Layers)
				{
					for (const FWowTextureElement& Texture : Layer.Textures)
					{
						if (Texture.Name.IsEmpty())
						{
							continue;
						}

						TWeakObjectPtr<UImage>* ImagePtr = NamedTextureWidgets.Find(Texture.Name);
						UImage* Image = ImagePtr && ImagePtr->IsValid() ? ImagePtr->Get() : nullptr;
						UCanvasPanelSlot* ImageSlot = Image ? Cast<UCanvasPanelSlot>(Image->Slot) : nullptr;
						if (!Image || !ImageSlot)
						{
							continue;
						}

						// Preserve dynamic SetWidth/SetHeight changes made by FrameXML while
						// recalculating only the region's anchor position.
						const FVector2D CurrentPixelSize = ImageSlot->GetSize();
						const float RegionW = CurrentPixelSize.X > 0.0f
							? CurrentPixelSize.X / UIScale
							: Texture.Width;
						const float RegionH = CurrentPixelSize.Y > 0.0f
							? CurrentPixelSize.Y / UIScale
							: Texture.Height;
						ApplyElementAnchors(Image, ContainerCanvas, Texture.Anchors,
							RegionW, RegionH, Texture.bSetAllPoints,
							Entry->Def.Width, Entry->Def.Height, OwnerRect.X, OwnerRect.Y);

						const FVector2D RegionPosition = ImageSlot->GetPosition() / UIScale;
						const FVector2D RegionSize = ImageSlot->GetSize() / UIScale;
						FrameRects.Add(Texture.Name, FFrameRect(
							OwnerRect.X + RegionPosition.X,
							OwnerRect.Y + RegionPosition.Y,
							RegionSize.X, RegionSize.Y));
						NoteRegionOwnerAt(Texture.Name, Entry->Def.Name, OwnerRect.X, OwnerRect.Y);
					}
				}
			}

			TArray<int64> DirectChildren;
			for (const TPair<int64, FFrameEntry>& Pair : Frames)
			{
				if (Pair.Value.ParentHandle == Handle && Pair.Value.Widget.IsValid())
				{
					DirectChildren.Add(Pair.Key);
				}
			}
			for (const int64 ChildHandle : DirectChildren)
			{
				if (FFrameEntry* Child = Frames.Find(ChildHandle); Child && Child->Widget.IsValid())
				{
					ApplyAnchors(Child->Widget.Get(), Child->Def);
				}
			}
		}
		// r302 (Stefan 13.10, /bt4: "Reset Profile" and other AceGUI buttons were a small dark box):
		// a Lua frame from a template is laid out at the template's size (UIPanelButtonTemplate2:
		// 40x22) and resized afterwards; its stretched layer textures (Middle, Right: two anchors)
		// follow the new size, like the client.
		else if (Entry->Def.bCreatedByLua)
		{
			if (UCanvasPanel* FrameCanvas = Cast<UCanvasPanel>(Entry->Widget.Get()))
			{
				const FFrameRect OwnerRect = GetFrameRect(Entry->Def.Name);
				for (const FWowLayer& Layer : Entry->Def.Layers)
					for (const FWowTextureElement& Texture : Layer.Textures)
					{
						if (Texture.Name.IsEmpty() || (Texture.Anchors.Num() < 2 && !Texture.bSetAllPoints)) continue;
						TWeakObjectPtr<UImage>* ImagePtr = NamedTextureWidgets.Find(Texture.Name);
						UImage* Image = ImagePtr && ImagePtr->IsValid() ? ImagePtr->Get() : nullptr;
						if (!Image || !Cast<UCanvasPanelSlot>(Image->Slot)) continue;
						ApplyElementAnchors(Image, FrameCanvas, Texture.Anchors, Texture.Width, Texture.Height,
							Texture.bSetAllPoints, Entry->Def.Width, Entry->Def.Height, OwnerRect.X, OwnerRect.Y);
						// r350: the next region anchored to this one (UIPanelButtonTemplate2's Middle between
						// Left and Right) reads its new rect, not the one from the template size.
						if (const UCanvasPanelSlot* LaidOut = Cast<UCanvasPanelSlot>(Image->Slot); LaidOut && UIScale > 0.f)
						{
							const FVector2D At = LaidOut->GetPosition() / UIScale;
							const FVector2D Sz = LaidOut->GetSize() / UIScale;
							FrameRects.Add(Texture.Name, FFrameRect(OwnerRect.X + At.X, OwnerRect.Y + At.Y, Sz.X, Sz.Y));
							NoteRegionOwnerAt(Texture.Name, Entry->Def.Name, OwnerRect.X, OwnerRect.Y);
						}
					}
			}
		}
	}
}

// The Combat Log (ChatFrame2) shares the General chat box natively. Blizzard_CombatLog and
// FCF code re-anchor/resize it from Lua, which pulled it out of the box (huge background,
// lines below it); keep it glued to ChatFrame1 after every such change.
static void SyncCombatChatFrame(FWowFrameManager& Manager)
{
	UWidget* General = Manager.GetWidgetForHandle(Manager.FindFrame(TEXT("ChatFrame1")));
	UWidget* Combat = Manager.GetWidgetForHandle(Manager.FindFrame(TEXT("ChatFrame2")));
	UCanvasPanelSlot* GeneralSlot = General ? Cast<UCanvasPanelSlot>(General->Slot) : nullptr;
	UCanvasPanelSlot* CombatSlot = Combat ? Cast<UCanvasPanelSlot>(Combat->Slot) : nullptr;
	if (!GeneralSlot || !CombatSlot) return;
	CombatSlot->SetAnchors(GeneralSlot->GetAnchors());
	CombatSlot->SetAlignment(GeneralSlot->GetAlignment());
	CombatSlot->SetPosition(GeneralSlot->GetPosition());
	CombatSlot->SetSize(GeneralSlot->GetSize());
}

bool FWowFrameManager::GetFrameLivePlacement(int64 Handle, float& X, float& Y, float& W, float& H, float& ParentHeight) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	const UCanvasPanelSlot* Slot = Entry && Entry->Widget.IsValid() ? Cast<UCanvasPanelSlot>(Entry->Widget->Slot) : nullptr;
	if (!Slot || UIScale <= 0.f) return false;
	X = Slot->GetPosition().X / UIScale;
	Y = Slot->GetPosition().Y / UIScale;
	W = Slot->GetSize().X / UIScale;
	H = Slot->GetSize().Y / UIScale;
	const FWowFrameDef* Parent = GetFrameDef(FindFrame(TEXT("UIParent")));
	ParentHeight = Parent && Parent->Height > 0.f ? Parent->Height : 768.f;
	return true;
}

bool FWowFrameManager::GetFrameScreenRect(int64 Handle, float& Left, float& Bottom, float& Right, float& Top)
{
	const FWowFrameDef* Def = GetFrameDef(Handle);
	if (!Def || Def->Name.IsEmpty()) return false;
	const FFrameRect Rect = GetFrameRect(Def->Name);
	if (Rect.W <= 0.f && Rect.H <= 0.f && Rect.X == 0.f && Rect.Y == 0.f) return false;
	const float ScreenHeight = GetFrameRect(TEXT("UIParent")).H > 0.f ? GetFrameRect(TEXT("UIParent")).H : 768.f;
	Left = Rect.X;
	Right = Rect.X + Rect.W;
	Top = ScreenHeight - Rect.Y; // FrameRects are top-down
	Bottom = Top - Rect.H;
	return true;
}

// r358 (Stefan 15.10: /bt4 froze the game for 4 s): who follows whom - children by parent, Lua frames
// by the frames they anchor to - kept between flushes. AceGUI reads GetWidth/GetHeight after nearly
// every SetPoint, each read flushes the layout, and each flush walked all ~6000 frames to build these
// maps again (1100 flushes while the /bt4 window was built). Entries are only added between full
// rebuilds; a stale one only lays a frame out once more from its current anchors.
namespace WowDependentLayoutCache
{
	const void* Owner = nullptr;
	bool bBuilt = false;
	int32 Pairs = 0;
	int32 FrameCount = 0; // r361: fewer frames than last time = a new UI (reload), even at the same address
	TMultiMap<int64, int64> ChildrenOf;
	TMultiMap<FString, int64> AnchoredTo;
	// r361 (Stefan 15.10: low FPS while /bt4 is open): the size OnSizeChanged last reported per
	// frame. A re-anchor drops the cached rects of the whole subtree, so every relaid descendant
	// looked resized and got OnSizeChanged again; AceGUI's handlers re-anchor, which kept the window
	// busy every tick. The client only fires it on a real change.
	TMap<int64, FVector2f> LastFiredSize;

	bool Live(const void* Manager) { return bBuilt && Owner == Manager; }
	void NoteParent(const void* Manager, int64 Handle, int64 ParentHandle)
	{
		if (!Live(Manager) || ParentHandle < 0) return;
		ChildrenOf.AddUnique(ParentHandle, Handle);
		++Pairs;
	}
	void NoteAnchors(const void* Manager, int64 Handle, const FString& ParentName, const TArray<FWowAnchor>& Anchors)
	{
		if (!Live(Manager)) return;
		for (const FWowAnchor& Anchor : Anchors)
			if (!Anchor.RelativeTo.IsEmpty() && Anchor.RelativeTo != ParentName) { AnchoredTo.AddUnique(Anchor.RelativeTo, Handle); ++Pairs; }
	}
}

void FWowFrameManager::SetFrameAnchors(int64 Handle, const TArray<FWowAnchor>& NewAnchors)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;
	if (Entry->Def.Name == TEXT("ChatFrame2"))
	{
		// Natively placed; see SyncCombatChatFrame.
		Entry->Def.Anchors = NewAnchors;
		SyncCombatChatFrame(*this);
		RefreshScrollingMessageLayout(Handle);
		return;
	}
	if (IsNativelyPlacedChatFrame(Entry->Def, Entry->Widget))
	{
		static int32 Logged = 0;
		if (Logged++ < 10)
			UE_LOG(LogWowFrame, Log, TEXT("ChatFrame1 SetPoint (%d anchors, first to '%s' %.0f,%.0f) from Lua ignored (natively placed)"),
				NewAnchors.Num(), NewAnchors.Num() ? *NewAnchors[0].RelativeTo : TEXT(""),
				NewAnchors.Num() ? NewAnchors[0].OffsetX : 0.f, NewAnchors.Num() ? NewAnchors[0].OffsetY : 0.f);
		return;
	}

	const bool bWasUnplaced = IsUnplacedFrame(Entry->Def);
	Entry->Def.Anchors = NewAnchors;
	if (Entry->Def.bCreatedByLua && Entry->Widget.IsValid()) WowDependentLayoutCache::NoteAnchors(this, Handle, Entry->Def.Parent, NewAnchors); // r358
	// frame:SetAllPoints() sets bSetAllPoints and passes no anchors; keep it.
	if (NewAnchors.Num() > 0) Entry->Def.bSetAllPoints = false;

	// Clear cached frame rect for this frame and ALL descendants.
	InvalidateFrameRectsRecursive(Handle);

	if (Entry->Widget.IsValid())
	{
		ApplyAnchors(Entry->Widget.Get(), Entry->Def);
		QueueDependentLayout(Handle); // r228
		if (bWasUnplaced != IsUnplacedFrame(Entry->Def))
			Entry->Widget->SetVisibility(!bWasUnplaced || !IsFrameEffectivelyShown(Handle)
				? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
}

bool FWowFrameManager::IsDockPanelChild(int64 Handle) const
{
    for(int32 Guard=0;Guard<64 && Handle>=0;++Guard)
    {
        if(DockPanels.Contains(Handle))return true;
        const FFrameEntry* Entry=Frames.Find(Handle);
        if(!Entry)return false;
        Handle=FindFrame(Entry->Def.Parent);
    }
    return false;
}

void FWowFrameManager::PlaceDockPanel(int64 Handle,FVector2D Position,float Scale)
{
    auto* Entry=Frames.Find(Handle);if(!Entry)return;
    DockPanels.Add(Handle);
    FWowAnchor Anchor;Anchor.Point=EWowAnchorPoint::TOPLEFT;
    Anchor.RelativePoint=EWowAnchorPoint::TOPLEFT;Anchor.RelativeTo=TEXT("UIParent");
    Anchor.OffsetX=Position.X;Anchor.OffsetY=-Position.Y;
    const bool bMoved=Entry->Def.Anchors.Num()!=1 || Entry->Def.Anchors[0].RelativeTo!=Anchor.RelativeTo
        || !FMath::IsNearlyEqual(Entry->Def.Anchors[0].OffsetX,Anchor.OffsetX)
        || !FMath::IsNearlyEqual(Entry->Def.Anchors[0].OffsetY,Anchor.OffsetY);
    if(bMoved)SetFrameAnchors(Handle,{Anchor});
    if(Entry->Widget.IsValid())
    {
        Entry->Widget->SetRenderTransformPivot(FVector2D::ZeroVector);
        Entry->Widget->SetRenderScale(FVector2D(Scale));
        if(auto* Slot=Cast<UCanvasPanelSlot>(Entry->Widget->Slot))Slot->SetZOrder(Entry->Def.Name.StartsWith(TEXT("ContainerFrame")) ? 4500 : 3500);
    }
}

void FWowFrameManager::ClearFrameAnchors(int64 Handle)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;

	Entry->Def.Anchors.Empty();
	Entry->Def.bSetAllPoints = false;
}

void FWowFrameManager::SetFrameAlpha(int64 Handle, float Alpha)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;

	// Chat tabs: FrameXML's chat fade sets them to .2 (others) / .4 (selected), and with the native
	// tab plate and label fading too they could not be seen at all, only clicked (Stefan 10.10).
	// Their look follows the chat hover in the tab update instead.
	if (Entry->Def.Name.StartsWith(TEXT("ChatFrame")) && Entry->Def.Name.EndsWith(TEXT("Tab")))
	{
		Alpha = 1.0f;
	}
	if (Entry->Widget.IsValid())
	{
		Entry->Widget->SetRenderOpacity(Alpha);
	}
}

void FWowFrameManager::SetFrameStrata(int64 Handle, EWowFrameStrata Strata)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;

	Entry->Def.Strata = Strata;

	if (Entry->Widget.IsValid())
	{
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Entry->Widget->Slot))
		{
			Slot->SetZOrder(static_cast<int32>(Strata) * 1000 + Entry->Def.FrameLevel);
		}
	}
}

void FWowFrameManager::SetFrameLevel(int64 Handle, int32 Level)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;

	Entry->Def.FrameLevel = Level;

	if (Entry->Widget.IsValid())
	{
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Entry->Widget->Slot))
		{
			Slot->SetZOrder(static_cast<int32>(Entry->Def.Strata) * 1000 + Level);
		}
	}
}

int64 FWowFrameManager::GetParentHandle(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? Entry->ParentHandle : -1;
}

TArray<int64> FWowFrameManager::GetChildHandles(int64 Handle) const
{
	TArray<int64> Children;
	for (const auto& Pair : Frames)
	{
		if (Pair.Value.ParentHandle == Handle)
		{
			Children.Add(Pair.Key);
		}
	}
	return Children;
}

UWidget* FWowFrameManager::GetWidgetForHandle(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry && Entry->Widget.IsValid() ? Entry->Widget.Get() : nullptr;
}

UButton* FWowFrameManager::GetButtonWidget(int64 Handle) const
{
	const TWeakObjectPtr<UButton>* Found = ButtonWidgets.Find(Handle);
	return (Found && Found->IsValid()) ? Found->Get() : nullptr;
}

UEditableTextBox* FWowFrameManager::GetEditBoxWidget(int64 Handle) const
{
	const TWeakObjectPtr<UEditableTextBox>* Found = EditBoxWidgets.Find(Handle);
	return (Found && Found->IsValid()) ? Found->Get() : nullptr;
}

USlider* FWowFrameManager::GetSliderWidget(int64 Handle) const
{
	const TWeakObjectPtr<USlider>* Found = SliderWidgets.Find(Handle);
	return (Found && Found->IsValid()) ? Found->Get() : nullptr;
}

UTextBlock* FWowFrameManager::GetPrimaryTextWidget(int64 Handle) const
{
	const TWeakObjectPtr<UTextBlock>* Found = PrimaryTextWidgets.Find(Handle);
	return (Found && Found->IsValid()) ? Found->Get() : nullptr;
}

UTextBlock* FWowFrameManager::GetNamedFontString(const FString& Name) const
{
	const TWeakObjectPtr<UTextBlock>* Found = NamedFontStringWidgets.Find(Name);
	return (Found && Found->IsValid()) ? Found->Get() : nullptr;
}

void FWowFrameManager::AddScrollingMessage(int64 Handle, const FString& Message, const FLinearColor& Color)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	UCanvasPanel* MessageCanvas = Entry ? Cast<UCanvasPanel>(Entry->Widget.Get()) : nullptr;
	if (!Entry || !MessageCanvas ||
		(Entry->Def.Type != EWowFrameType::ScrollingMessageFrame && Entry->Def.Type != EWowFrameType::MessageFrame))
	{
		return;
	}

    if(Entry->Def.Type==EWowFrameType::MessageFrame)
    {
        AddTransientMessage(Handle,Message,Color);
        return;
    }

	// Preserve WoW's |cAARRGGBB...|r colour runs while still reducing hyperlink
	// control syntax to its readable label. Server MOTDs commonly use several
	// colours in one line, so a single UTextBlock tint is not sufficient.
	FString DisplayMessage;
	TArray<FWowChatTextSegment> TextSegments;
	ParseWowChatText(Message, Color, DisplayMessage, TextSegments);

	bool bHasReadableContent = false;
	for (const TCHAR Character : DisplayMessage)
	{
		if (FChar::IsAlnum(Character))
		{
			bHasReadableContent = true;
			break;
		}
	}
	if (!bHasReadableContent) return;

	TArray<FScrollingMessageEntry>& Lines = ScrollingMessageWidgets.FindOrAdd(Handle);
	Lines.RemoveAll([](const FScrollingMessageEntry& Line) { return !Line.Widget.IsValid(); });
	if (!Lines.IsEmpty() && Lines.Last().Widget.IsValid() &&
		Lines.Last().PlainText == DisplayMessage)
	{
		return;
	}

	constexpr int32 MaxHistoryLines = 128;
	while (Lines.Num() >= MaxHistoryLines)
	{
		if (Lines[0].Widget.IsValid())
		{
			Lines[0].Widget->RemoveFromParent();
		}
		Lines.RemoveAt(0);
	}

	FScrollingMessageEntry& NewLine = Lines.AddDefaulted_GetRef();
	NewLine.SourceText = Message;
	NewLine.PlainText = DisplayMessage;
	NewLine.DefaultColor = Color;
	NewLine.AddedAtSeconds = FPlatformTime::Seconds();
    ChatLastActivity.Add(Handle,NewLine.AddedAtSeconds);
	RebuildScrollingMessageWidget(Handle, NewLine);
	ScrollingMessageOffsets.Add(Handle, 0);
	RefreshScrollingMessageLayout(Handle);
	if (Entry->Def.Name == TEXT("ChatFrame1"))
	{
		AppendPersistentChatLog(DisplayMessage);
	}

	UE_LOG(LogWowFrame, Log, TEXT("Chat line [%s]: %s"), *Entry->Def.Name, *DisplayMessage);
}

void FWowFrameManager::RebuildScrollingMessageWidget(int64 Handle,
	FScrollingMessageEntry& MessageEntry)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	UCanvasPanel* MessageCanvas = Entry ? Cast<UCanvasPanel>(Entry->Widget.Get()) : nullptr;
	if (!Entry || !MessageCanvas) return;

	if (MessageEntry.Widget.IsValid())
	{
		MessageEntry.Widget->RemoveFromParent();
	}

	FString DisplayMessage;
	TArray<FWowChatTextSegment> TextSegments;
	ParseWowChatText(MessageEntry.SourceText, MessageEntry.DefaultColor, DisplayMessage, TextSegments);
	MessageEntry.PlainText = DisplayMessage;

	UCanvasPanel* Line = NewObject<UCanvasPanel>(MessageCanvas);
	Line->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Line->SetClipping(EWidgetClipping::ClipToBounds);
	FSlateFontInfo FontInfo;
	if (FontManager && FontManager->IsInitialized())
	{
		FontInfo = FontManager->GetFont(TEXT("ChatFontNormal"),
			FMath::Max(8, FMath::RoundToInt(static_cast<float>(ChatFontSize) * UIScale)));
	}
	else
	{
		FontInfo.Size = FMath::Max(8,
			FMath::RoundToInt(static_cast<float>(ChatFontSize) * UIScale));
	}

	const float FrameWidth = FMath::Max(Entry->Def.Width * UIScale, 320.0f * UIScale);
	const float AvailableWidth = FMath::Max(1.0f, FrameWidth - 10.0f * UIScale);
	TSharedPtr<FSlateFontMeasure> FontMeasure;
	if (FSlateApplication::IsInitialized())
	{
		FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	}
	const float LineHeight = WowChatRowHeight(FontInfo, UIScale);
	float CursorX = 0.0f;
	int32 RowIndex = 0;

	auto BeginNextRow = [&CursorX, &RowIndex]()
	{
		CursorX = 0.0f;
		++RowIndex;
	};

	auto AddToken = [&](FString Token, const FLinearColor& TokenColor)
	{
		if (Token.IsEmpty()) return;
		const bool bOnlyWhitespace = Token.TrimStartAndEnd().IsEmpty();
		if (CursorX <= 0.0f && bOnlyWhitespace) return;

		float TokenWidth = FontMeasure.IsValid()
			? FontMeasure->Measure(Token, FontInfo).X
			: static_cast<float>(Token.Len() * FontInfo.Size) * 0.55f;
		if (CursorX > 0.0f && CursorX + TokenWidth > AvailableWidth)
		{
			BeginNextRow();
			Token.TrimStartInline();
			if (Token.IsEmpty()) return;
			TokenWidth = FontMeasure.IsValid()
				? FontMeasure->Measure(Token, FontInfo).X
				: static_cast<float>(Token.Len() * FontInfo.Size) * 0.55f;
		}

		UTextBlock* Run = NewObject<UTextBlock>(Line);
		Run->SetText(FText::FromString(Token));
		Run->SetColorAndOpacity(FSlateColor(TokenColor));
		Run->SetFont(FontInfo);
		Run->SetJustification(ETextJustify::Left);
		Run->SetShadowOffset(FVector2D(1.0f, 1.0f));
		Run->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.95f));
		Run->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UCanvasPanelSlot* RunSlot = Line->AddChildToCanvas(Run))
		{
			RunSlot->SetAutoSize(true);
			RunSlot->SetPosition(FVector2D(CursorX, RowIndex * LineHeight));
		}
		CursorX += TokenWidth;
	};

	for (const FWowChatTextSegment& Segment : TextSegments)
	{
		FString Token;
		for (const TCHAR Character : Segment.Text)
		{
			if (Character == TEXT('\r')) continue;
			if (Character == TEXT('\n'))
			{
				AddToken(MoveTemp(Token), Segment.Color);
				Token.Reset();
				BeginNextRow();
				continue;
			}

			Token.AppendChar(Character);
			if (FChar::IsWhitespace(Character))
			{
				AddToken(MoveTemp(Token), Segment.Color);
				Token.Reset();
			}
		}
		AddToken(MoveTemp(Token), Segment.Color);
	}

	if (UCanvasPanelSlot* LineSlot = MessageCanvas->AddChildToCanvas(Line))
	{
		LineSlot->SetAutoSize(false);
		LineSlot->SetSize(FVector2D(AvailableWidth, (RowIndex + 1) * LineHeight));
		LineSlot->SetZOrder(10000);
	}
	MessageEntry.Widget = Line;
	MessageEntry.VisualLineCount = FMath::Max(1, RowIndex + 1);
}

void FWowFrameManager::RefreshScrollingMessageLayout(int64 Handle)
{
	// The same row height the lines were built with (WowChatRowHeight).
	FSlateFontInfo ChatRowFont;
	if (FontManager && FontManager->IsInitialized())
		ChatRowFont = FontManager->GetFont(TEXT("ChatFontNormal"), FMath::Max(8, FMath::RoundToInt(static_cast<float>(ChatFontSize) * UIScale)));
	else
		ChatRowFont.Size = FMath::Max(8, FMath::RoundToInt(static_cast<float>(ChatFontSize) * UIScale));
	const float ChatRowHeight = WowChatRowHeight(ChatRowFont, UIScale);
	TArray<FScrollingMessageEntry>* LinesPtr = ScrollingMessageWidgets.Find(Handle);
	const FFrameEntry* Entry = Frames.Find(Handle);
	if (!LinesPtr || !Entry) return;

	TArray<FScrollingMessageEntry>& Lines = *LinesPtr;
    const float TopInset=Entry->Def.Name==TEXT("ChatFrame2")?27.f*UIScale:0.f;
    const auto* LiveSlot=Entry->Widget.IsValid()?Cast<UCanvasPanelSlot>(Entry->Widget->Slot):nullptr;
    const float ActualHeight=LiveSlot?LiveSlot->GetSize().Y:Entry->Def.Height*UIScale;
    const float RowHeight=ChatRowHeight;
    const int32 MaxVisibleLines=FMath::Clamp(FMath::FloorToInt((ActualHeight-TopInset-5.f*UIScale)/RowHeight),1,30);
	int32& OffsetFromBottom = ScrollingMessageOffsets.FindOrAdd(Handle);
	// r258 (Stefan 12.10: the chat scroll buttons did nothing): the limit counted messages against
	// the visible ROWS, so a few long wrapped messages (server broadcasts) could never be scrolled.
	// Scroll until the oldest message is on screen: the page that starts with message 0.
	int32 TopPageLast = 0;
	for (int32 Rows = 0, Index = 0; Index < Lines.Num(); ++Index)
	{
		Rows += FMath::Max(1, Lines[Index].VisualLineCount);
		if (Rows > MaxVisibleLines && Index > 0) break;
		TopPageLast = Index;
	}
	const int32 MaxOffset = FMath::Max(0, Lines.Num() - 1 - TopPageLast);
	OffsetFromBottom = FMath::Clamp(OffsetFromBottom, 0, MaxOffset);

	const int32 LastVisibleIndex = Lines.Num() - 1 - OffsetFromBottom;
	int32 FirstVisibleIndex = LastVisibleIndex;
	int32 VisibleRowCount = 0;
	for (int32 Index = LastVisibleIndex; Index >= 0; --Index)
	{
		const int32 MessageRows = FMath::Max(1, Lines[Index].VisualLineCount);
		if (VisibleRowCount > 0 && VisibleRowCount + MessageRows > MaxVisibleLines) break;
		VisibleRowCount += MessageRows;
		FirstVisibleIndex = Index;
		if (VisibleRowCount >= MaxVisibleLines) break;
	}
	const float LineHeight = ChatRowHeight;
	const float BottomPadding = 5.0f * UIScale;
	const float FrameHeight = FMath::Max(ActualHeight, TopInset+LineHeight+BottomPadding);
	const float FrameWidth = FMath::Max(Entry->Def.Width * UIScale, 320.0f * UIScale);

	float CurrentBottom = FrameHeight - BottomPadding;
	for (int32 Index = Lines.Num() - 1; Index >= 0; --Index)
	{
		if (!Lines[Index].Widget.IsValid()) continue;
		const bool bVisible = Index >= FirstVisibleIndex && Index <= LastVisibleIndex;
		Lines[Index].Widget->SetVisibility(bVisible
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (!bVisible) continue;

		if (UCanvasPanelSlot* ExistingSlot = Cast<UCanvasPanelSlot>(Lines[Index].Widget->Slot))
		{
			const float MessageHeight = FMath::Max(1, Lines[Index].VisualLineCount) * LineHeight;
			CurrentBottom -= MessageHeight;
            if (CurrentBottom<TopInset) { Lines[Index].Widget->SetVisibility(ESlateVisibility::Collapsed); continue; }
			ExistingSlot->SetAutoSize(false);
			ExistingSlot->SetPosition(FVector2D(5.0f * UIScale, CurrentBottom));
			ExistingSlot->SetSize(FVector2D(FrameWidth - 10.0f * UIScale, MessageHeight));
		}
	}
}

void FWowFrameManager::ClearScrollingMessages(int64 Handle)
{
    if(auto* State=TransientMessages.Find(Handle))
    {
        for(auto& Line:State->Lines)if(Line.Widget.IsValid())Line.Widget->RemoveFromParent();
        State->Lines.Reset();
    }
	if (TArray<FScrollingMessageEntry>* Lines = ScrollingMessageWidgets.Find(Handle))
	{
		for (FScrollingMessageEntry& Line : *Lines)
		{
			if (Line.Widget.IsValid()) Line.Widget->RemoveFromParent();
		}
		Lines->Reset();
	}
	ScrollingMessageOffsets.Add(Handle, 0);
}

void FWowFrameManager::ScrollMessages(int64 Handle, int32 LineDelta)
{
	const int32 LineCount = GetScrollingMessageCount(Handle);
	int32& Offset = ScrollingMessageOffsets.FindOrAdd(Handle);
	const int32 Before = Offset;
	Offset = FMath::Clamp(Offset + LineDelta, 0, FMath::Max(0, LineCount - 1)); // r258: the layout clamps to the oldest page
	RefreshScrollingMessageLayout(Handle);
	UE_LOG(LogWowFrame, Log, TEXT("Chat scroll: %s by %d - %d messages, offset %d -> %d"),
		*GetFrameName(Handle), LineDelta, LineCount, Before, ScrollingMessageOffsets.FindRef(Handle));
}

void FWowFrameManager::ScrollMessagesToTop(int64 Handle)
{
	ScrollingMessageOffsets.Add(Handle, FMath::Max(0, GetScrollingMessageCount(Handle) - 1)); // r258: clamped by the layout
	RefreshScrollingMessageLayout(Handle);
}

void FWowFrameManager::ScrollMessagesToBottom(int64 Handle)
{
	ScrollingMessageOffsets.Add(Handle, 0);
	RefreshScrollingMessageLayout(Handle);
}

int32 FWowFrameManager::GetScrollingMessageCount(int64 Handle) const
{
    if(const auto* State=TransientMessages.Find(Handle))return State->Lines.Num();
	const TArray<FScrollingMessageEntry>* Lines = ScrollingMessageWidgets.Find(Handle);
	return Lines ? Lines->Num() : 0;
}

bool FWowFrameManager::AreMessagesAtTop(int64 Handle) const
{
	constexpr int32 MaxVisibleLines = 7;
	const int32 MaxOffset = FMath::Max(0, GetScrollingMessageCount(Handle) - MaxVisibleLines);
	const int32* Offset = ScrollingMessageOffsets.Find(Handle);
	return (Offset ? *Offset : 0) >= MaxOffset;
}

bool FWowFrameManager::AreMessagesAtBottom(int64 Handle) const
{
	const int32* Offset = ScrollingMessageOffsets.Find(Handle);
	return !Offset || *Offset == 0;
}

bool FWowFrameManager::FocusEditBox(int64 Handle, const FString& InitialText, bool bTakeKeyboardFocus)
{
	UEditableTextBox* EditBox = GetEditBoxWidget(Handle);
	if (!EditBox) return false;

	SetFrameVisible(Handle, true);
	if (const FFrameEntry* Entry = Frames.Find(Handle); Entry && Entry->Def.Name.StartsWith(TEXT("ChatFrame")))
	{
		constexpr float ChatInputWidth = 430.0f;
		constexpr float ChatInputHeight = 24.0f;
		constexpr float ChatInputCapWidth = 24.0f;

		if (UWidget* Container = GetWidgetForHandle(Handle))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Container->Slot))
			{
				Slot->SetAutoSize(false);
				// 112 units below the top of the owning chat frame, expressed in whatever canvas
				// holds the edit box now (its chat frame's, or the root after a SetParent/strata move).
				FVector2D Position(0.0f, 112.0f);
				const FString ChatFrameName = Entry->Def.Name.LeftChop(FCString::Strlen(TEXT("EditBox")));
				if (Entry->Def.Name.EndsWith(TEXT("EditBox")) && FindFrame(ChatFrameName) >= 0)
				{
					const FFrameRect ChatRect = GetFrameRect(ChatFrameName);
					FVector2D CanvasOrigin = FVector2D::ZeroVector;
					for (int64 Owner = Entry->ParentHandle; Owner >= 0; Owner = GetParentHandle(Owner))
					{
						UWidget* OwnerWidget = GetWidgetForHandle(Owner);
						if (OwnerWidget && OwnerWidget == Container->GetParent())
						{
							const FFrameRect OwnerRect = GetFrameRect(GetFrameName(Owner));
							CanvasOrigin = FVector2D(OwnerRect.X, OwnerRect.Y);
							break;
						}
					}
					Position = FVector2D(ChatRect.X, ChatRect.Y + 112.0f) - CanvasOrigin;
				}
				Slot->SetPosition(Position * UIScale);
				Slot->SetSize(FVector2D(ChatInputWidth * UIScale, ChatInputHeight * UIScale));
				Slot->SetZOrder(25000);
			}
		}

		// ChatFrameEditBoxTemplate is authored with a width that Lua expands to
		// match the selected chat window. Until SetWidth has run, FrameXML leaves
		// it only five units wide, which makes the middle texture negative-width
		// and piles both end caps at the left edge. Reflow the original Blizzard
		// three-piece artwork after applying our known chat-window width.
		auto LayoutBorderPiece = [this, ChatInputWidth, ChatInputHeight, ChatInputCapWidth](
			const FString& Name, float X, float Width, int32 ZOrder, bool bVisible)
		{
			if (UImage* Piece = GetNamedTexture(Name))
			{
				if (UCanvasPanelSlot* PieceSlot = Cast<UCanvasPanelSlot>(Piece->Slot))
				{
					PieceSlot->SetAnchors(FAnchors(0.0f, 0.0f));
					PieceSlot->SetAlignment(FVector2D::ZeroVector);
					PieceSlot->SetAutoSize(false);
					PieceSlot->SetPosition(FVector2D(X * UIScale, 0.0f));
					PieceSlot->SetSize(FVector2D(Width * UIScale, ChatInputHeight * UIScale));
					PieceSlot->SetZOrder(ZOrder);
				}
				Piece->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			}
		};

		const FString Prefix = Entry->Def.Name;
		const float MiddleWidth = ChatInputWidth - (2.0f * ChatInputCapWidth);
		LayoutBorderPiece(Prefix + TEXT("Left"), 0.0f, ChatInputCapWidth, 10, true);
		LayoutBorderPiece(Prefix + TEXT("Mid"), ChatInputCapWidth, MiddleWidth, 10, true);
		LayoutBorderPiece(Prefix + TEXT("Right"), ChatInputWidth - ChatInputCapWidth, ChatInputCapWidth, 10, true);

		// The focus atlas is a highlight overlay. Keep it hidden until proper
		// focus-state texture switching is implemented instead of doubling the
		// normal border and making it look like detached blocks.
		LayoutBorderPiece(Prefix + TEXT("FocusLeft"), 0.0f, ChatInputCapWidth, 11, false);
		LayoutBorderPiece(Prefix + TEXT("FocusMid"), ChatInputCapWidth, MiddleWidth, 11, false);
		LayoutBorderPiece(Prefix + TEXT("FocusRight"), ChatInputWidth - ChatInputCapWidth, ChatInputCapWidth, 11, false);

		// Supply the subtle translucent input tint as a separate owned widget.
		// Changing a live SEditableTextBox style while it receives first focus can
		// invalidate Slate during the same traversal; keeping the edit box itself
		// transparent avoids that lifetime/invalidation hazard.
		if (UCanvasPanel* ChatCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(Handle)))
		{
			const FString FillName = Prefix + TEXT("Fill");
			UImage* Fill = GetNamedTexture(FillName);
			if (!Fill)
			{
				Fill = NewObject<UImage>(ChatCanvas);
				Fill->SetBrush(FSlateColorBrush(FLinearColor::White));
				ChatCanvas->AddChildToCanvas(Fill);
				NamedTextureWidgets.Add(FillName, Fill);
			}
			if (UCanvasPanelSlot* FillSlot = Cast<UCanvasPanelSlot>(Fill->Slot))
			{
				FillSlot->SetAnchors(FAnchors(0.0f, 0.0f));
				FillSlot->SetAlignment(FVector2D::ZeroVector);
				FillSlot->SetAutoSize(false);
				FillSlot->SetPosition(FVector2D(2.0f * UIScale, 4.0f * UIScale));
				FillSlot->SetSize(FVector2D((ChatInputWidth - 4.0f) * UIScale,
					(ChatInputHeight - 8.0f) * UIScale));
				FillSlot->SetZOrder(5);
			}
			Fill->SetColorAndOpacity(WowColor::FromWow(0.62f, 0.64f, 0.68f, 0.13f));
			Fill->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}

		// Keep the channel header ("Say:") and typed text on one line: the header uses the
		// input's font size and is centred vertically like the text; LayoutChatEditHeaders
		// places the suffix and sets the text inset after the header (ChatEdit_UpdateHeader).
		for (const TCHAR* Suffix : { TEXT("Header"), TEXT("HeaderSuffix") })
		{
			UTextBlock* Header = GetNamedFontString(Prefix + Suffix);
			if (!Header) continue;
			if (UCanvasPanelSlot* HeaderSlot = Cast<UCanvasPanelSlot>(Header->Slot))
			{
				HeaderSlot->SetAnchors(FAnchors(0.0f, 0.5f));
				HeaderSlot->SetAlignment(FVector2D(0.0f, 0.5f));
				HeaderSlot->SetAutoSize(true);
				HeaderSlot->SetPosition(FVector2D(ChatEditHeaderX * UIScale, 0.0f));
				HeaderSlot->SetZOrder(60);
			}
			FSlateFontInfo HeaderFont = Header->GetFont();
			// One point smaller than before (Stefan 10.10: "Say:" too big).
			HeaderFont.Size = FMath::RoundToInt(9.0f * UIScale);
			Header->SetFont(HeaderFont);
			Header->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		ChatEditInsetApplied.Remove(Handle);

	}
	EditBox->SetVisibility(ESlateVisibility::Visible);
	EditBox->SetText(FText::FromString(InitialText));
	EditBoxLastText.Add(Handle, InitialText);
	if (bTakeKeyboardFocus)
	{
		EditBox->SetKeyboardFocus();
	}
	return true;
}

bool FWowFrameManager::GetFocusedEditBox(int64& OutHandle, FString& OutText) const
{
	OutHandle = -1;
	OutText.Empty();
	// The box that really holds keyboard focus wins. With addons loaded several edit
	// boxes can report user focus (e.g. after an addon's EditBox:SetFocus), and the
	// first match used to be some other box: Enter then neither sent nor closed chat.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (const auto& Pair : EditBoxWidgets)
		{
			if (!Pair.Value.IsValid() || !Pair.Value->IsVisible()) continue;
			const bool bFocused = Pass == 0
				? (Pair.Value->HasKeyboardFocus() || Pair.Value->HasFocusedDescendants())
				: Pair.Value->HasAnyUserFocus();
			if (bFocused)
			{
				OutHandle = Pair.Key;
				OutText = Pair.Value->GetText().ToString();
				return true;
			}
		}
	}
	return false;
}

void FWowFrameManager::LayoutChatEditHeaders()
{
	if (!FSlateApplication::IsInitialized()) return;
	for (const auto& Pair : EditBoxWidgets)
	{
		UEditableTextBox* EditBox = Pair.Value.Get();
		if (!EditBox || !EditBox->IsVisible()) continue;
		const FFrameEntry* Entry = Frames.Find(Pair.Key);
		if (!Entry || !Entry->Def.Name.StartsWith(TEXT("ChatFrame"))) continue;
		const FString& Prefix = Entry->Def.Name;

		// Header ("Yell:") and typed text use the chat type's colour (ChatEdit_UpdateHeader:
		// header:SetTextColor / editBox:SetTextColor). Re-read only when the header changes.
		{
			UTextBlock* ColorHeader = GetNamedFontString(Prefix + TEXT("Header"));
			const FString Key = ColorHeader ? ColorHeader->GetText().ToString() : FString();
			const FString* LastKey = ChatEditColorKey.Find(Pair.Key);
			if (EventSystem && (!LastKey || *LastKey != Key))
			{
				ChatEditColorKey.Add(Pair.Key, Key);
				EventSystem->ExecuteLuaString(FString::Printf(TEXT(R"LUA(
					__WowEditColor = ""
					local eb = _G["%s"]
					local t = eb and eb.GetAttribute and eb:GetAttribute("chatType")
					local i = t and ChatTypeInfo and ChatTypeInfo[t]
					if t == "CHANNEL" and eb then
						local c = eb:GetAttribute("channelTarget")
						if c and ChatTypeInfo["CHANNEL"..c] then i = ChatTypeInfo["CHANNEL"..c] end
					end
					if i and i.r then __WowEditColor = string.format("%%.3f %%.3f %%.3f", i.r, i.g, i.b) end
				)LUA"), *Prefix), TEXT("=ChatEditColor"));
				TArray<FString> Parts;
				EventSystem->GetGlobalString(TEXT("__WowEditColor")).ParseIntoArrayWS(Parts);
				// The chat type attribute is not always readable here: fall back to the header text
				// with the stock ChatTypeInfo colours (Stefan 10.10: "Yell:" and the typed text
				// stayed white).
				FLinearColor TypeColor = FLinearColor::White;
				bool bHaveColor = false;
				if (Parts.Num() == 3)
				{
					TypeColor = WowColor::FromWow(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]), 1.0f);
					bHaveColor = true;
				}
				else
				{
					struct FHeaderColor { const TCHAR* Prefix; FLinearColor Color; };
					static const FHeaderColor HeaderColors[] = {
						{TEXT("Raid Warning"), WowColor::FromWow(1.0f, 0.282f, 0.0f)}, {TEXT("Raid"), WowColor::FromWow(1.0f, 0.498f, 0.0f)},
						{TEXT("Yell"), WowColor::FromWow(1.0f, 0.251f, 0.251f)}, {TEXT("Party"), WowColor::FromWow(0.667f, 0.667f, 1.0f)},
						{TEXT("Officer"), WowColor::FromWow(0.251f, 0.753f, 0.251f)}, {TEXT("Guild"), WowColor::FromWow(0.251f, 1.0f, 0.251f)},
						{TEXT("Tell"), WowColor::FromWow(1.0f, 0.502f, 1.0f)}, {TEXT("Whisper"), WowColor::FromWow(1.0f, 0.502f, 1.0f)},
						{TEXT("Battleground"), WowColor::FromWow(1.0f, 0.498f, 0.0f)}, {TEXT("Emote"), WowColor::FromWow(1.0f, 0.502f, 0.251f)},
						{TEXT("["), WowColor::FromWow(1.0f, 0.753f, 0.753f)}, {TEXT("Say"), FLinearColor(1.0f, 1.0f, 1.0f)}};
					const FString HeaderText = Key.TrimStart();
					for (const FHeaderColor& HeaderEntry : HeaderColors)
						if (HeaderText.StartsWith(HeaderEntry.Prefix)) { TypeColor = HeaderEntry.Color; bHaveColor = true; break; }
				}
				if (bHaveColor)
				{
					if (ColorHeader) ColorHeader->SetColorAndOpacity(FSlateColor(TypeColor));
					if (UTextBlock* SuffixText = GetNamedFontString(Prefix + TEXT("HeaderSuffix")))
						SuffixText->SetColorAndOpacity(FSlateColor(TypeColor));
					// The typed text too, focused or not (the style's focused colour was white). Set as
					// the live box's colour overrides, never by restyling it: SetWidgetStyle on a live
					// edit box left its text style dangling, and typing /dance (header -> emote) crashed
					// in SEditableText::SynchronizeTextStyle (20:26, via Claude-Perf).
					if (const TSharedPtr<SWidget> BoxWidget = EditBox->GetCachedWidget();
						BoxWidget.IsValid() && BoxWidget->GetType() == FName(TEXT("SEditableTextBox")))
					{
						const TSharedPtr<SEditableTextBox> LiveBox = StaticCastSharedPtr<SEditableTextBox>(BoxWidget);
						LiveBox->SetForegroundColor(FSlateColor(TypeColor));
						LiveBox->SetFocusedForegroundColor(FSlateColor(TypeColor));
					}
				}
			}
		}

		// Header and suffix sit side by side; typed text starts after them (original
		// SetTextInsets(15 + header width + suffix width, 13, 0, 0)).
		float X = ChatEditHeaderX * UIScale;
		UTextBlock* Header = GetNamedFontString(Prefix + TEXT("Header"));
		if (Header && Header->IsVisible() && !Header->GetText().IsEmpty())
			X += Header->GetDesiredSize().X;
		if (UTextBlock* Suffix = GetNamedFontString(Prefix + TEXT("HeaderSuffix")))
		{
			if (UCanvasPanelSlot* SuffixSlot = Cast<UCanvasPanelSlot>(Suffix->Slot))
				SuffixSlot->SetPosition(FVector2D(X, 0.0f));
			if (Suffix->IsVisible() && !Suffix->GetText().IsEmpty())
				X += Suffix->GetDesiredSize().X;
		}
		const float Left = X + 4.0f * UIScale;

		const float* Applied = ChatEditInsetApplied.Find(Pair.Key);
		if (Applied && FMath::Abs(*Applied - Left) < 0.5f) continue;
		ChatEditInsetApplied.Add(Pair.Key, Left);
		// Move the text box itself (layout only); its style is set once at creation.
		if (UCanvasPanelSlot* BoxSlot = Cast<UCanvasPanelSlot>(EditBox->Slot))
			BoxSlot->SetOffsets(FMargin(Left, 0.0f, 8.0f * UIScale, 0.0f));
	}
}

void FWowFrameManager::CloseEditBox(int64 Handle, bool bClearText)
{
	if (UEditableTextBox* EditBox = GetEditBoxWidget(Handle))
	{
		if (bClearText)
		{
			EditBox->SetText(FText::GetEmpty());
			EditBoxLastText.Add(Handle, FString());
		}
		EditBox->SetUserFocus(nullptr);
	}
	SetFrameVisible(Handle, false);
}

void FWowFrameManager::SetActiveChatTab(int32 TabIndex)
{
	TabIndex = FMath::Clamp(TabIndex, 1, 10);
	if (!EnabledChatTabs.Contains(TabIndex))
	{
		return;
	}
	ActiveChatTabIndex = TabIndex;
	if (ActiveChatTabIndex != 2)
	{
		bCombatLogFilterMenuOpen = false;
	}
	for (int32 ChatIndex = 1; ChatIndex <= 10; ++ChatIndex)
	{
		const FString FrameName = FString::Printf(TEXT("ChatFrame%d"), ChatIndex);
		const FString TabName = FrameName + TEXT("Tab");
		SetFrameVisible(FindFrame(FrameName), EnabledChatTabs.Contains(ChatIndex) && ChatIndex == TabIndex);
		if (UTextBlock* Label = GetPrimaryTextWidget(FindFrame(TabName)))
		{
			Label->SetColorAndOpacity(FSlateColor(ChatIndex == TabIndex
				? WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f)
				: WowColor::FromWow(0.55f, 0.50f, 0.38f, 1.0f)));
		}
	}
	SetFrameVisible(FindFrame(TEXT("GeneralDockManagerOverflowButtonList")), false);
}

namespace
{
// Speech-bubble chat menu: label, slash command (ChatMenu's UIMenu_AddButton rows).
// r314 (Stefan 14.10: the rows should have their colours): each row in its chat type's colour,
// the client's ChatTypeInfo defaults (Reply as Whisper).
struct FNativeChatMenuRow { const TCHAR* Label; const TCHAR* Slash; float R, G, B; };
const FNativeChatMenuRow GNativeChatMenuRows[] = {
	{TEXT("Say"), TEXT("/s"), 1.f, 1.f, 1.f}, {TEXT("Party"), TEXT("/p"), .667f, .667f, 1.f},
	{TEXT("Raid"), TEXT("/raid"), 1.f, .498f, 0.f}, {TEXT("Battleground"), TEXT("/bg"), 1.f, .498f, 0.f},
	{TEXT("Guild"), TEXT("/g"), .251f, 1.f, .251f}, {TEXT("Officer"), TEXT("/o"), .251f, .753f, .251f},
	{TEXT("Yell"), TEXT("/y"), 1.f, .251f, .251f}, {TEXT("Whisper"), TEXT("/w"), 1.f, .502f, 1.f},
	{TEXT("Emote"), TEXT("/e"), 1.f, .502f, .251f}, {TEXT("Reply"), TEXT("/r"), 1.f, .502f, 1.f},
};
}

bool FWowFrameManager::CloseNativeChatMenu()
{
	if (!bNativeChatMenuOpen) return false;
	bNativeChatMenuOpen = false;
	RefreshNativeChatMenu();
	return true;
}

void FWowFrameManager::RefreshNativeChatMenu()
{
	if (!RootCanvas.IsValid()) return;
	constexpr int32 RowCount = UE_ARRAY_COUNT(GNativeChatMenuRows);
	constexpr float MenuWidth = 150.0f;
	constexpr float RowHeight = 18.0f;
	constexpr float MenuHeight = RowCount * RowHeight + 10.0f;
	const int32 BaseZ = static_cast<int32>(EWowFrameStrata::TOOLTIP) * 1000 + 140;

	// Created on first use: rows are native buttons (clicks come back through DispatchClick),
	// the slash command is a second, right-aligned text on the row like UIMenu's ShortcutText.
	for (int32 RowIndex = 1; RowIndex <= RowCount; ++RowIndex)
	{
		const FString RowName = FString::Printf(TEXT("ChatNativeMenuRow%d"), RowIndex);
		if (FindFrame(RowName) < 0)
		{
			FWowFrameDef RowDef;
			RowDef.Name = RowName;
			RowDef.Type = EWowFrameType::Button;
			RowDef.Width = MenuWidth - 6.0f;
			RowDef.Height = RowHeight;
			RowDef.Strata = EWowFrameStrata::TOOLTIP;
			RowDef.FrameLevel = 140 + RowIndex;
			RowDef.ButtonText = GNativeChatMenuRows[RowIndex - 1].Label;
			CreateFrame(RowDef);
			const int64 Created = FindFrame(RowName);
			SetFrameMouseEnabled(Created, true);
			if (UTextBlock* Label = GetPrimaryTextWidget(Created))
			{
				FSlateFontInfo Font = Label->GetFont();
				Font.Size = FMath::Max(9, FMath::RoundToInt(10.0f * UIScale));
				Label->SetFont(Font);
				Label->SetJustification(ETextJustify::Left);
				Label->SetShadowOffset(FVector2D(1.0f));
				Label->SetShadowColorAndOpacity(FLinearColor::Black);
				const FNativeChatMenuRow& Row = GNativeChatMenuRows[RowIndex - 1];
				Label->SetColorAndOpacity(FSlateColor(WowColor::FromWow(Row.R, Row.G, Row.B, 1.0f)));
			}
			const FString ShortcutName = RowName + TEXT("Shortcut");
			UTextBlock* Shortcut = NewObject<UTextBlock>(RootCanvas.Get());
			RootCanvas->AddChildToCanvas(Shortcut);
			NamedFontStringWidgets.Add(ShortcutName, Shortcut);
			Shortcut->SetText(FText::FromString(GNativeChatMenuRows[RowIndex - 1].Slash));
			FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", FMath::Max(8, FMath::RoundToInt(9.0f * UIScale)));
			Shortcut->SetFont(Font);
			Shortcut->SetJustification(ETextJustify::Right);
			Shortcut->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Shortcut->SetShadowOffset(FVector2D(1.0f));
			Shortcut->SetShadowColorAndOpacity(FLinearColor::Black);
		}
	}
	UImage* Background = GetNamedTexture(TEXT("ChatNativeMenuBackground"));
	if (!Background)
	{
		Background = NewObject<UImage>(RootCanvas.Get());
		RootCanvas->AddChildToCanvas(Background);
		NamedTextureWidgets.Add(TEXT("ChatNativeMenuBackground"), Background);
		Background->SetBrush(FSlateRoundedBoxBrush(FWowWindowStyle::ChatBackgroundColor(), 3.0f * UIScale,
			WowColor::FromWow(0.62f, 0.62f, 0.67f, 0.98f), 1.0f * UIScale));
	}

	// ChatMenu: BOTTOMLEFT at the speech bubble's TOPRIGHT, kept on screen. The bubble's laid-out
	// rect is stale once the chat chrome is placed on the root canvas, so its drawn geometry is
	// used (Stefan 10.10: the menu opened far up and to the right of its button).
	const FFrameRect UIParentRect = GetFrameRect(TEXT("UIParent"));
	FFrameRect BubbleRect = GetFrameRect(TEXT("ChatFrameMenuButton"));
	if (UWidget* Bubble = GetWidgetForHandle(FindFrame(TEXT("ChatFrameMenuButton"))))
	{
		const FGeometry& BubbleGeometry = Bubble->GetCachedGeometry();
		const FGeometry& RootGeometry = RootCanvas->GetCachedGeometry();
		if (UIScale > 0.f && BubbleGeometry.GetLocalSize().X > 0.f && RootGeometry.GetLocalSize().X > 0.f)
		{
			const FVector2D TopLeft = RootGeometry.AbsoluteToLocal(BubbleGeometry.GetAbsolutePosition());
			const FVector2D BottomRight = RootGeometry.AbsoluteToLocal(BubbleGeometry.GetAbsolutePosition() + BubbleGeometry.GetAbsoluteSize());
			BubbleRect.X = TopLeft.X / UIScale; BubbleRect.Y = TopLeft.Y / UIScale;
			BubbleRect.W = (BottomRight.X - TopLeft.X) / UIScale; BubbleRect.H = (BottomRight.Y - TopLeft.Y) / UIScale;
		}
	}
	const float MenuX = FMath::Clamp(BubbleRect.X + BubbleRect.W, 4.0f, FMath::Max(4.0f, UIParentRect.W - MenuWidth - 4.0f));
	const float MenuY = FMath::Clamp(BubbleRect.Y - MenuHeight, 4.0f, FMath::Max(4.0f, UIParentRect.H - MenuHeight - 4.0f));
	const ESlateVisibility Shown = bNativeChatMenuOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;

	Background->SetVisibility(Shown);
	if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Background->Slot))
	{
		Slot->SetAutoSize(false);
		Slot->SetPosition(FVector2D(MenuX, MenuY) * UIScale);
		Slot->SetSize(FVector2D(MenuWidth, MenuHeight) * UIScale);
		Slot->SetZOrder(BaseZ);
	}
	for (int32 RowIndex = 1; RowIndex <= RowCount; ++RowIndex)
	{
		const FString RowName = FString::Printf(TEXT("ChatNativeMenuRow%d"), RowIndex);
		const int64 RowHandle = FindFrame(RowName);
		const FVector2D RowPos = FVector2D(MenuX + 8.0f, MenuY + 5.0f + (RowIndex - 1) * RowHeight) * UIScale;
		const FVector2D RowSize = FVector2D(MenuWidth - 16.0f, RowHeight) * UIScale;
		SetFrameVisible(RowHandle, bNativeChatMenuOpen);
		if (UWidget* RowWidget = GetWidgetForHandle(RowHandle))
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(RowWidget->Slot))
			{
				Slot->SetAutoSize(false);
				Slot->SetPosition(RowPos);
				Slot->SetSize(RowSize);
				Slot->SetZOrder(BaseZ + RowIndex * 2);
			}
		if (TWeakObjectPtr<UTextBlock>* Shortcut = NamedFontStringWidgets.Find(RowName + TEXT("Shortcut")); Shortcut && Shortcut->IsValid())
		{
			(*Shortcut)->SetVisibility(bNativeChatMenuOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>((*Shortcut)->Slot))
			{
				Slot->SetAutoSize(false);
				Slot->SetPosition(RowPos + FVector2D(0.0f, 2.0f * UIScale));
				Slot->SetSize(RowSize);
				Slot->SetZOrder(BaseZ + RowIndex * 2 + 1);
			}
		}
	}
	// FrameXML's own ChatMenu never shows alongside.
	SetFrameVisible(FindFrame(TEXT("ChatMenu")), false);
}

void FWowFrameManager::DismissChatMenus()
{
	CloseNativeChatMenu();
	bChatTabContextMenuOpen = false;
	bCombatLogFilterMenuOpen = false;
	SetFrameVisible(FindFrame(TEXT("GeneralDockManagerOverflowButtonList")), false);
}

bool FWowFrameManager::CommitPendingChatTabRename(const FString& NewLabel)
{
	if (!bChatTabRenamePending)
	{
		return false;
	}
	bChatTabRenamePending = false;
	FString SanitizedLabel = NewLabel.TrimStartAndEnd();
	if (SanitizedLabel.IsEmpty())
	{
		return true;
	}
	SanitizedLabel.LeftInline(24);
	ChatTabLabels.Add(ChatContextTabIndex, SanitizedLabel);
	if (UTextBlock* Label = GetPrimaryTextWidget(FindFrame(
		*FString::Printf(TEXT("ChatFrame%dTab"), ChatContextTabIndex))))
	{
		Label->SetText(FText::FromString(SanitizedLabel));
	}
	return true;
}

void FWowFrameManager::ConfigureChatTab(int32 TabIndex, const FString& Label, bool bEnabled)
{
	if (TabIndex < 1 || TabIndex > 10)
	{
		return;
	}
	if (bEnabled)
	{
		EnabledChatTabs.Add(TabIndex);
		ChatTabLabels.Add(TabIndex, Label.IsEmpty()
			? FString::Printf(TEXT("Chat %d"), TabIndex) : Label);
	}
	else
	{
		EnabledChatTabs.Remove(TabIndex);
		if (ActiveChatTabIndex == TabIndex)
		{
			ActiveChatTabIndex = EnabledChatTabs.Contains(1) ? 1 : 2;
		}
	}
}

void FWowFrameManager::SetSocialFriendEntries(const TArray<FString>& Entries, int32 OnlineCount)
{
	SetSocialContactEntries(Entries, TArray<FString>(), OnlineCount);
}

void FWowFrameManager::SetSocialContactEntries(const TArray<FString>& Friends,
	const TArray<FString>& Ignored, int32 OnlineCount)
{
	SocialFriendEntries = Friends;
	SocialIgnoreEntries = Ignored;
	SocialOnlineFriendCount = FMath::Clamp(OnlineCount, 0, Friends.Num());
}

void FWowFrameManager::SetRecentWhisperContact(const FString& Sender, const FString& Message)
{
	RecentWhisperSender = Sender;
	RecentWhisperMessage = Message;
}

void FWowFrameManager::SetFrameHitRectInsets(int64 Handle, const FMargin& Insets)
{
    if (auto* Entry = Frames.Find(Handle)) Entry->Def.HitRectInsets = Insets;
}

void FWowFrameManager::SetFrameMouseEnabled(int64 Handle, bool bEnabled)
{
	if (FFrameEntry* Entry = Frames.Find(Handle))
	{
		Entry->bMouseEnabled = bEnabled;
	}
}

void FWowFrameManager::SetFrameKeyboardEnabled(int64 Handle, bool bEnabled)
{
	if (FFrameEntry* Entry = Frames.Find(Handle))
	{
		Entry->bKeyboardEnabled = bEnabled;
	}
}

void FWowFrameManager::SetFrameMouseWheelEnabled(int64 Handle, bool bEnabled)
{
	if (FFrameEntry* Entry = Frames.Find(Handle))
	{
		Entry->bMouseWheelEnabled = bEnabled;
	}
}

void FWowFrameManager::SetFrameRegisteredClicks(int64 Handle, const TArray<FString>& ClickTypes)
{
	if (FFrameEntry* Entry = Frames.Find(Handle))
	{
		Entry->bHasExplicitClickRegistration = true;
		Entry->RegisteredClicks.Reset();

		for (const FString& ClickType : ClickTypes)
		{
			const FString Normalized = NormalizeClickRegistrationToken(ClickType);
			if (!Normalized.IsEmpty())
			{
				Entry->RegisteredClicks.Add(Normalized);
			}
		}
	}
}

bool FWowFrameManager::IsFrameMouseEnabled(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? Entry->bMouseEnabled : false;
}

bool FWowFrameManager::IsFrameKeyboardEnabled(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? Entry->bKeyboardEnabled : false;
}

bool FWowFrameManager::IsFrameMouseWheelEnabled(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? Entry->bMouseWheelEnabled : false;
}

bool FWowFrameManager::IsFrameClickRegistered(int64 Handle, const FString& Button, bool bMouseDown) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry)
	{
		return false;
	}

	if (Entry->bHasExplicitClickRegistration)
	{
		return HasRegisteredClickToken(Entry->RegisteredClicks, Button, bMouseDown);
	}

	return UsesDefaultButtonClickRegistration(Entry->Def, Button, bMouseDown);
}

// ── Template Inheritance ──────────────────────────────────────────────────────

void FWowFrameManager::MergeTemplate(FWowFrameDef& Target, const FWowFrameDef& Template)
{
	// Type — template sets the base type if target doesn't override
	// (typically the inheriting frame specifies its own type, so only fallback)

	// Size — use template size if target doesn't specify
	if (Target.Width == 0.f && Template.Width > 0.f) Target.Width = Template.Width;
	if (Target.Height == 0.f && Template.Height > 0.f) Target.Height = Template.Height;

	// Strata/level — use template if target is at default
	if (Target.Strata == EWowFrameStrata::MEDIUM && Template.Strata != EWowFrameStrata::MEDIUM)
		Target.Strata = Template.Strata;
	if (Target.FrameLevel == 0 && Template.FrameLevel != 0)
		Target.FrameLevel = Template.FrameLevel;

	// Hidden — template hidden overrides unless target explicitly sets visible
	if (Template.bHidden && !Target.bHidden) Target.bHidden = Template.bHidden;
	if (Target.ParentKey.IsEmpty()) Target.ParentKey = Template.ParentKey;

    if (!Target.HitRectInsets.IsSet()) Target.HitRectInsets = Template.HitRectInsets;
	// EnableMouse / EnableKeyboard — inherit from template
	if (Template.bEnableMouse && !Target.bEnableMouse) Target.bEnableMouse = true;
	if (Template.bEnableKeyboard && !Target.bEnableKeyboard) Target.bEnableKeyboard = true;
	if (Template.bClampedToScreen && !Target.bClampedToScreen) Target.bClampedToScreen = true;

	// SetAllPoints
	if (Template.bSetAllPoints && !Target.bSetAllPoints) Target.bSetAllPoints = Template.bSetAllPoints;

	// Anchors — template anchors are used if target has none
	if (Target.Anchors.IsEmpty() && !Template.Anchors.IsEmpty())
	{
		Target.Anchors = Template.Anchors;
	}

	// Layers — merge: template layers first, then target layers (target overrides by draw layer level)
	for (const FWowLayer& TplLayer : Template.Layers)
	{
		bool bFound = false;
		for (FWowLayer& TargetLayer : Target.Layers)
		{
			if (TargetLayer.Level == TplLayer.Level)
			{
				// Target already has this layer — prepend template textures/fontstrings
				TArray<FWowTextureElement> Merged = TplLayer.Textures;
				Merged.Append(TargetLayer.Textures);
				TargetLayer.Textures = MoveTemp(Merged);

				TArray<FWowFontStringElement> MergedFS = TplLayer.FontStrings;
				MergedFS.Append(TargetLayer.FontStrings);
				TargetLayer.FontStrings = MoveTemp(MergedFS);
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			Target.Layers.Add(TplLayer);
		}
	}

	// Scripts — concatenate: template scripts run first
	TArray<FWowScriptHandler> MergedScripts = Template.Scripts;
	MergedScripts.Append(Target.Scripts);
	Target.Scripts = MoveTemp(MergedScripts);

	for (const FString& NamedObject : Template.NamedObjectGlobals)
	{
		Target.NamedObjectGlobals.AddUnique(NamedObject);
	}
	// r338: XML attributes - the frame's own win over its template's.
	for (const FWowFrameDef::FXmlAttribute& Attr : Template.XmlAttributes)
	{
		if (!Target.XmlAttributes.ContainsByPredicate([&Attr](const FWowFrameDef::FXmlAttribute& Own) { return Own.Name == Attr.Name; }))
			Target.XmlAttributes.Add(Attr);
	}

	// Children — template children are prepended
	TArray<FWowFrameDef> MergedChildren = Template.Children;
	MergedChildren.Append(Target.Children);
	Target.Children = MoveTemp(MergedChildren);

	// Backdrop — use template if target doesn't have one
	if (!Target.Backdrop.IsSet() && Template.Backdrop.IsSet())
	{
		Target.Backdrop = Template.Backdrop;
	}

	// Type-specific properties — use template values if target is empty
	if (Target.ButtonText.IsEmpty()) Target.ButtonText = Template.ButtonText;
	if (Target.NormalTexture.IsEmpty()) { Target.NormalTexture = Template.NormalTexture; Target.NormalTextureName = Template.NormalTextureName; Target.NormalTextureParentKey = Template.NormalTextureParentKey; Target.NormalTextureUV = Template.NormalTextureUV;
		Target.NormalTextureW = Template.NormalTextureW; Target.NormalTextureH = Template.NormalTextureH; Target.NormalTexturePoint = Template.NormalTexturePoint;
		Target.NormalTextureRelativePoint = Template.NormalTextureRelativePoint; Target.NormalTextureOffset = Template.NormalTextureOffset; } // r379
	if (Target.PushedTexture.IsEmpty()) { Target.PushedTexture = Template.PushedTexture; Target.PushedTextureName = Template.PushedTextureName; Target.PushedTextureParentKey = Template.PushedTextureParentKey; Target.PushedTextureUV = Template.PushedTextureUV; }
	if (Target.CheckedTexture.IsEmpty()) { Target.CheckedTexture = Template.CheckedTexture; Target.CheckedTextureUV = Template.CheckedTextureUV; }
	if (Target.HighlightTexture.IsEmpty()) { Target.HighlightTexture = Template.HighlightTexture; Target.HighlightTextureName = Template.HighlightTextureName; Target.HighlightTextureParentKey = Template.HighlightTextureParentKey; Target.HighlightTextureUV = Template.HighlightTextureUV; }
	if (Target.DisabledTexture.IsEmpty()) { Target.DisabledTexture = Template.DisabledTexture; Target.DisabledTextureName = Template.DisabledTextureName; Target.DisabledTextureParentKey = Template.DisabledTextureParentKey; Target.DisabledTextureUV = Template.DisabledTextureUV; }
	if (Target.Orientation.IsEmpty()) Target.Orientation = Template.Orientation;
	if (Target.BarTexture.IsEmpty()) Target.BarTexture = Template.BarTexture;
}

bool FWowFrameManager::InheritsSecureTemplate(const FString& Inherits) const
{
	TArray<FString> Pending;
	Inherits.ParseIntoArray(Pending, TEXT(","), true);
	TSet<FString> Seen;
	while (Pending.Num() > 0)
	{
		FString Name = Pending.Pop().TrimStartAndEnd();
		if (Name.IsEmpty() || Seen.Contains(Name)) continue;
		Seen.Add(Name);
		// SecureFrameTemplate and everything built on it (Secure*Template) is protected="true".
		if (Name.StartsWith(TEXT("Secure"))) return true;
		if (const FWowFrameDef* Tpl = Templates.Find(Name))
		{
			TArray<FString> More;
			Tpl->Inherits.ParseIntoArray(More, TEXT(","), true);
			Pending.Append(More);
		}
	}
	return false;
}

FWowFrameDef FWowFrameManager::ResolveInherits(const FWowFrameDef& Def) const
{
	if (Def.Inherits.IsEmpty()) return Def;

	FWowFrameDef Resolved = Def;

	// Parse comma-separated template names
	TArray<FString> TemplateNames;
	Def.Inherits.ParseIntoArray(TemplateNames, TEXT(","), true);

	// Apply templates left-to-right (later templates override earlier ones)
	// Build a merged template first, then merge into the frame def
	for (FString& TplName : TemplateNames)
	{
		TplName.TrimStartAndEndInline();
		const FWowFrameDef* Tpl = Templates.Find(TplName);
		if (Tpl)
		{
			// Recursively resolve the template's own inheritance
			FWowFrameDef ResolvedTpl = ResolveInherits(*Tpl);
			MergeTemplate(Resolved, ResolvedTpl);
			UE_LOG(LogWowFrame, Verbose, TEXT("Applied template '%s' to frame '%s'"), *TplName, *Def.Name);
		}
		else
		{
			UE_LOG(LogWowFrame, Warning, TEXT("Template '%s' not found for frame '%s'"), *TplName, *Def.Name);
		}
	}

	// Clear the inherits field so we don't re-resolve
	Resolved.Inherits.Empty();

	return Resolved;
}

// ── Anchor Positioning ────────────────────────────────────────────────────────

/** Get the screen position offset of an anchor point on a frame */
FVector2D FWowFrameManager::GetAnchorPointOffset(EWowAnchorPoint Point, float Width, float Height)
{
	switch (Point)
	{
	case EWowAnchorPoint::TOPLEFT:     return FVector2D(0.0f, 0.0f);
	case EWowAnchorPoint::TOP:         return FVector2D(Width * 0.5f, 0.0f);
	case EWowAnchorPoint::TOPRIGHT:    return FVector2D(Width, 0.0f);
	case EWowAnchorPoint::LEFT:        return FVector2D(0.0f, Height * 0.5f);
	case EWowAnchorPoint::CENTER:      return FVector2D(Width * 0.5f, Height * 0.5f);
	case EWowAnchorPoint::RIGHT:       return FVector2D(Width, Height * 0.5f);
	case EWowAnchorPoint::BOTTOMLEFT:  return FVector2D(0.0f, Height);
	case EWowAnchorPoint::BOTTOM:      return FVector2D(Width * 0.5f, Height);
	case EWowAnchorPoint::BOTTOMRIGHT: return FVector2D(Width, Height);
	default: return FVector2D(Width * 0.5f, Height * 0.5f);
	}
}

/** Recursively invalidate cached FrameRects for a frame and all its children */
void FWowFrameManager::InvalidateFrameRectsRecursive(int64 Handle)
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;

	if (!Entry->Def.Name.IsEmpty())
	{
		FrameRects.Remove(Entry->Def.Name);
	}

	// r361 (Stefan 15.10: low FPS while /bt4 is open): one pass over the frames per call. The
	// recursion walked all ~6000 frames again for every descendant, and AceGUI re-anchors its
	// containers (each with a few hundred descendants) many times a frame.
	TMultiMap<int64, int64> ChildrenByParent;
	for (const auto& Pair : Frames)
	{
		if (Pair.Value.ParentHandle >= 0) ChildrenByParent.Add(Pair.Value.ParentHandle, Pair.Key);
	}
	TArray<int64> Pending;
	ChildrenByParent.MultiFind(Handle, Pending);
	TSet<int64> Seen;
	Seen.Add(Handle);
	for (int32 Index = 0; Index < Pending.Num(); ++Index)
	{
		const int64 Child = Pending[Index];
		bool bSeen = false;
		Seen.Add(Child, &bSeen);
		if (bSeen) continue;
		if (const FFrameEntry* ChildEntry = Frames.Find(Child))
		{
			if (!ChildEntry->Def.Name.IsEmpty()) FrameRects.Remove(ChildEntry->Def.Name);
		}
		ChildrenByParent.MultiFind(Child, Pending);
	}
}

// ── r228: dependent layout ───────────────────────────────────────────────────

void FWowFrameManager::QueueDependentLayout(int64 Handle)
{
	if (!bInDependentLayout) DependentLayoutChanged.Add(Handle);
}

void FWowFrameManager::FlushDependentLayout()
{
	if ((DependentLayoutChanged.IsEmpty() && PendingSizeChanged.IsEmpty()) || bInDependentLayout) return;
	bInDependentLayout = true;

	// Who depends on whom: children by parent handle, anchored frames by the name they point at.
	// Only Lua-created frames follow automatically: FrameXML frames keep the layout code they have
	// (several are placed natively after their anchors), which this must not undo.
	TMultiMap<int64, int64>& ChildrenOf = WowDependentLayoutCache::ChildrenOf;
	TMultiMap<FString, int64>& AnchoredTo = WowDependentLayoutCache::AnchoredTo;
	const bool bNewUi = WowDependentLayoutCache::Owner != this || Frames.Num() < WowDependentLayoutCache::FrameCount;
	WowDependentLayoutCache::FrameCount = Frames.Num();
	if (bNewUi) WowDependentLayoutCache::LastFiredSize.Reset();
	if (bNewUi || !WowDependentLayoutCache::Live(this) || WowDependentLayoutCache::Pairs > 8 * FMath::Max(1, Frames.Num()))
	{
		ChildrenOf.Reset();
		AnchoredTo.Reset();
		int32 Pairs = 0;
		for (const TPair<int64, FFrameEntry>& Pair : Frames)
		{
			const FWowFrameDef& Def = Pair.Value.Def;
			if (!Def.bCreatedByLua || !Pair.Value.Widget.IsValid()) continue;
			if (Pair.Value.ParentHandle >= 0) { ChildrenOf.Add(Pair.Value.ParentHandle, Pair.Key); ++Pairs; }
			for (const FWowAnchor& Anchor : Def.Anchors)
				if (!Anchor.RelativeTo.IsEmpty() && Anchor.RelativeTo != Def.Parent) { AnchoredTo.AddUnique(Anchor.RelativeTo, Pair.Key); ++Pairs; }
		}
		WowDependentLayoutCache::Pairs = Pairs;
		WowDependentLayoutCache::Owner = this;
		WowDependentLayoutCache::bBuilt = true;
	}

	TSet<FString> ChangedNames;
	TArray<int64> Queue;
	// r357 (Stefan 15.10, /bt4: sliders with no width, the tree list 0 high): a frame already waiting
	// in the queue is not queued again - it is laid out once, from the latest rects. AceGUI's flow
	// layout chains every widget to the one before it, and the repeated entries used up the cycle
	// guard below, so widgets far down a page (and their children) kept a stale, empty layout.
	TSet<int64> Waiting;
	auto PushDependents = [&](int64 Handle)
	{
		const FFrameEntry* Entry = Frames.Find(Handle);
		if (!Entry) return;
		ChangedNames.Add(Entry->Def.Name);
		TArray<int64> Dependents;
		ChildrenOf.MultiFind(Handle, Dependents);
		if (!Entry->Def.Name.IsEmpty()) AnchoredTo.MultiFind(Entry->Def.Name, Dependents);
		for (const int64 Dependent : Dependents)
		{
			bool bAlreadyWaiting = false;
			Waiting.Add(Dependent, &bAlreadyWaiting);
			if (!bAlreadyWaiting) Queue.Add(Dependent);
		}
	};
	for (const int64 Handle : DependentLayoutChanged) PushDependents(Handle);
	DependentLayoutChanged.Reset();

	TMap<int64, int32> Visits;
	for (int32 Index = 0; Index < Queue.Num() && Index < 20000; ++Index)
	{
		const int64 Handle = Queue[Index];
		Waiting.Remove(Handle);
		int32& Count = Visits.FindOrAdd(Handle);
		if (++Count > 8) continue; // anchor cycles: give up after a few passes, like the client's "anchor family" guard
		FFrameEntry* Entry = Frames.Find(Handle);
		if (!Entry || !Entry->Widget.IsValid() || Entry->Def.Name.IsEmpty()) continue;
		const FFrameRect* OldRect = FrameRects.Find(Entry->Def.Name);
		const bool bHadRect = OldRect != nullptr;
		const FFrameRect Old = bHadRect ? *OldRect : FFrameRect();
		FrameRects.Remove(Entry->Def.Name);
		ApplyAnchors(Entry->Widget.Get(), Entry->Def);
		const FFrameRect* NewRect = FrameRects.Find(Entry->Def.Name);
		const bool bChanged = !bHadRect || !NewRect || !FMath::IsNearlyEqual(Old.X, NewRect->X, .01f)
			|| !FMath::IsNearlyEqual(Old.Y, NewRect->Y, .01f) || !FMath::IsNearlyEqual(Old.W, NewRect->W, .01f)
			|| !FMath::IsNearlyEqual(Old.H, NewRect->H, .01f);
		if (bChanged) PushDependents(Handle);
		if (NewRect && (!bHadRect || !FMath::IsNearlyEqual(Old.W, NewRect->W, .01f) || !FMath::IsNearlyEqual(Old.H, NewRect->H, .01f)))
			PendingSizeChanged.Add(Handle);
	}

	// Lua regions anchored to another frame follow their frames (and their parent).
	DynamicRegions.RemoveAll([](const FDynamicRegion& Region) { return !Region.Widget.IsValid(); });
	for (const FDynamicRegion& Region : DynamicRegions)
	{
		const FFrameEntry* Parent = Frames.Find(Region.ParentHandle);
		bool bAffected = Parent && ChangedNames.Contains(Parent->Def.Name);
		for (const FWowAnchor& Anchor : Region.Anchors)
			bAffected |= !Anchor.RelativeTo.IsEmpty() && ChangedNames.Contains(Anchor.RelativeTo);
		if (bAffected) ApplyDynamicRegion(Region);
	}
	bInDependentLayout = false;

	// OnSizeChanged(self, width, height) for frames whose size changed (AceGUI's scroll frames and
	// tree groups re-flow in it). A handler that moves frames again is laid out on the next tick.
	static int32 GSizeChangedDepth = 0; // a handler's own resize waits for the next tick
	if (!PendingSizeChanged.IsEmpty() && EventSystem && GSizeChangedDepth == 0)
	{
		TGuardValue<int32> Depth(GSizeChangedDepth, 1);
		const TArray<int64> Resized = PendingSizeChanged.Array();
		PendingSizeChanged.Reset();
		for (const int64 Handle : Resized)
		{
			float Left = 0.f, Bottom = 0.f, Right = 0.f, Top = 0.f;
			if (!GetFrameScreenRect(Handle, Left, Bottom, Right, Top)) continue;
			const FVector2f NewSize(Right - Left, Top - Bottom);
			if (const FVector2f* Fired = WowDependentLayoutCache::LastFiredSize.Find(Handle))
			{
				if (FMath::IsNearlyEqual(Fired->X, NewSize.X, .01f) && FMath::IsNearlyEqual(Fired->Y, NewSize.Y, .01f)) continue; // r361
			}
			WowDependentLayoutCache::LastFiredSize.Add(Handle, NewSize);
			EventSystem->RunFrameScript(Handle, TEXT("OnSizeChanged"),
				{ FString::SanitizeFloat(Right - Left), FString::SanitizeFloat(Top - Bottom) });
		}
	}
}

void FWowFrameManager::ApplyDynamicRegion(const FDynamicRegion& Region)
{
	UWidget* Widget = Region.Widget.Get();
	const FFrameEntry* Parent = Frames.Find(Region.ParentHandle);
	if (!Widget || !Parent || !Cast<UCanvasPanelSlot>(Widget->Slot)) return;
	// Make sure every frame named by an anchor has a rect (ApplyElementAnchors reads FrameRects).
	for (const FWowAnchor& Anchor : Region.Anchors)
		if (!Anchor.RelativeTo.IsEmpty()) GetFrameRect(Anchor.RelativeTo);
	const FFrameRect ParentRect = GetFrameRect(Parent->Def.Name);
	ApplyElementAnchors(Widget, Cast<UCanvasPanel>(Parent->Widget.Get()), Region.Anchors, Region.W, Region.H, false,
		ParentRect.W, ParentRect.H, ParentRect.X, ParentRect.Y);
}

void FWowFrameManager::SetDynamicRegionAnchors(UWidget* Region, int64 ParentHandle, const TArray<FWowAnchor>& Anchors, float W, float H)
{
	if (!Region) return;
	FDynamicRegion* Entry = DynamicRegions.FindByPredicate([Region](const FDynamicRegion& R) { return R.Widget.Get() == Region; });
	if (!Entry) { Entry = &DynamicRegions.AddDefaulted_GetRef(); Entry->Widget = Region; }
	Entry->ParentHandle = ParentHandle;
	Entry->Anchors = Anchors;
	Entry->W = W;
	Entry->H = H;
	ApplyDynamicRegion(*Entry);
}

void FWowFrameManager::ClearDynamicRegionAnchors(UWidget* Region)
{
	DynamicRegions.RemoveAll([Region](const FDynamicRegion& R) { return !R.Widget.IsValid() || R.Widget.Get() == Region; });
}

bool FWowFrameManager::UpdateDynamicRegionSize(UWidget* Region, float W, float H)
{
	FDynamicRegion* Entry = DynamicRegions.FindByPredicate([Region](const FDynamicRegion& R) { return R.Widget.Get() == Region; });
	if (!Entry) return false;
	Entry->W = W;
	Entry->H = H;
	ApplyDynamicRegion(*Entry);
	return true;
}

/** Get the frame rect for a named frame, or UIParent default */
FWowFrameManager::FFrameRect FWowFrameManager::GetFrameRect(const FString& FrameName)
{
	if (FrameName.IsEmpty() || FrameName == TEXT("UIParent"))
	{
		// UIParent covers the entire screen in WoW coordinates
		if (FrameRects.Contains(TEXT("UIParent")))
		{
			return FrameRects[TEXT("UIParent")];
		}

		// Initialize UIParent rect if not set — use actual viewport-based dimensions
		float FallbackW = 1024.0f;
		float FallbackH = 768.0f;
		if (UIScale > 0.f && GEngine && GEngine->GameViewport)
		{
			FVector2D VP;
			GEngine->GameViewport->GetViewportSize(VP);
			FallbackW = VP.X / UIScale;
			FallbackH = 768.0f;
		}
		FFrameRect UIParentRect(0.0f, 0.0f, FallbackW, FallbackH);
		FrameRects.Add(TEXT("UIParent"), UIParentRect);
		return UIParentRect;
	}

	if (FFrameRect* Cached = FrameRects.Find(FrameName))
	{
		// r278: a texture/fontstring follows its frame (see GWowRegionOwners).
		if (FWowRegionOwnerAt* Region = GWowRegionOwners.Find(FrameName))
		{
			if (const FFrameRect* OwnerRect = FrameRects.Find(Region->Owner))
			{
				const float DX = OwnerRect->X - Region->X;
				const float DY = OwnerRect->Y - Region->Y;
				if (DX != 0.f || DY != 0.f)
				{
					Cached->X += DX; Cached->Y += DY;
					Region->X = OwnerRect->X; Region->Y = OwnerRect->Y;
				}
			}
		}
		return *Cached;
	}

	// Try to look up the frame and calculate its rect
	int64 Handle = FindFrame(FrameName);
	if (Handle != -1)
	{
		const FWowFrameDef* Def = GetFrameDef(Handle);
		if (Def)
		{
			if (Def->bSetAllPoints)
            {
                const FFrameRect Rect = GetFrameRect(Def->Parent);
                FrameRects.Add(FrameName, Rect);
                return Rect;
            }
            // Calculate rect based on frame's definition
			FFrameRect Rect(0.0f, 0.0f, Def->Width, Def->Height);

			if (!Def->Anchors.IsEmpty())
			{
				const FWowAnchor& Anchor = Def->Anchors[0];
				FFrameRect ParentRect = GetFrameRect(Anchor.RelativeTo.IsEmpty() ? Def->Parent : Anchor.RelativeTo);
				FVector2D ParentAnchorPos = FVector2D(ParentRect.X, ParentRect.Y) +
					GetAnchorPointOffset(Anchor.RelativePoint, ParentRect.W, ParentRect.H);
				FVector2D FrameAnchorOffset = GetAnchorPointOffset(Anchor.Point, Rect.W, Rect.H);

				Rect.X = ParentAnchorPos.X + Anchor.OffsetX - FrameAnchorOffset.X;
				// WoW convention: positive Y = UP, but our rect system Y increases downward
				Rect.Y = ParentAnchorPos.Y - Anchor.OffsetY - FrameAnchorOffset.Y;
			}

			// Cache the calculated rect
			FrameRects.Add(FrameName, Rect);
			return Rect;
		}
	}

	// Fallback to center of screen
	UE_LOG(LogWowFrame, Warning, TEXT("Frame '%s' not found, using default rect"), *FrameName);
	FFrameRect DefaultRect(400.0f, 300.0f, 200.0f, 100.0f);
	FrameRects.Add(FrameName, DefaultRect);
	return DefaultRect;
}

void FWowFrameManager::ApplyAnchors(UWidget* Widget, const FWowFrameDef& Def)
{
	if (!Widget) return;
	// r380: remember the named regions frames are anchored to (see IsFrameAnchorRegion).
	for (const FWowAnchor& RegionAnchor : Def.Anchors)
	{
		FString RegionRel = RegionAnchor.RelativeTo;
		if (RegionRel.Contains(TEXT("$parent"))) RegionRel.ReplaceInline(TEXT("$parent"), *Def.Parent);
		if (!RegionRel.IsEmpty() && (NamedFontStringWidgets.Contains(RegionRel) || NamedTextureWidgets.Contains(RegionRel)))
			FrameAnchorRegions.Add(RegionRel);
	}

	UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot);
	if (!Slot)
	{
		UE_LOG(LogWowFrame, Error, TEXT("  ApplyAnchors: No canvas slot for %s"), *Def.Name);
		return;
	}

	if (Def.bSetAllPoints)
	{
		// Fill parent completely
		Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		Slot->SetOffsets(FMargin(0));

		// Store frame rect for children to reference
		FFrameRect ParentRect = GetFrameRect(Def.Parent);
		FrameRects.Add(Def.Name, ParentRect);

		UE_LOG(LogWowFrame, Verbose, TEXT("  %s: SetAllPoints (fill parent %.1fx%.1f)"), *Def.Name, ParentRect.W, ParentRect.H); // r358: Verbose (hundreds a second in AceGUI)
		return;
	}

	float FrameWidth = Def.Width;
	float FrameHeight = Def.Height;

	// Get the canvas owner's absolute position — we need to convert absolute WoW coords
	// to relative coords within the parent canvas. In WoW, all positions are absolute
	// screen coords, but UMG canvas slots use parent-relative positioning.
	FFrameRect CanvasOwnerRect = GetFrameRect(Def.Parent);
	float CanvasOffX = CanvasOwnerRect.X;
	float CanvasOffY = CanvasOwnerRect.Y;

	if (Def.Anchors.IsEmpty())
	{
		// Default positioning - center of parent with explicit size
		float PosX = (CanvasOwnerRect.W * 0.5f) - (FrameWidth * 0.5f);
		float PosY = (CanvasOwnerRect.H * 0.5f) - (FrameHeight * 0.5f);

		// Store ABSOLUTE rect for child lookups
		float AbsX = CanvasOffX + PosX;
		float AbsY = CanvasOffY + PosY;

		Slot->SetAnchors(FAnchors(0.0f, 0.0f));
		Slot->SetAlignment(FVector2D(0.0f, 0.0f));

		// A size given in only one dimension (SetWidth(1) before SetHeight) is still an
		// explicit size; only a frame with no size at all borrows its parent's.
		if (FrameWidth > 0.f || FrameHeight > 0.f)
		{
			Slot->SetPosition(FVector2D(PosX * UIScale, PosY * UIScale));
			Slot->SetSize(FVector2D(FrameWidth * UIScale, FrameHeight * UIScale));
			FrameRects.Add(Def.Name, FFrameRect(AbsX, AbsY, FrameWidth, FrameHeight));
		}
		else if (CanvasOwnerRect.W > 0.f && CanvasOwnerRect.H > 0.f && !UsesOriginalSizing(Def))
		{
			// No explicit size but parent has size - use parent size to avoid 100x30 default
			Slot->SetPosition(FVector2D(0.0f, 0.0f));
			Slot->SetSize(FVector2D(CanvasOwnerRect.W * UIScale, CanvasOwnerRect.H * UIScale));
			// The slot sits at the parent's origin, so the absolute rect does too
			// (it used to be offset by half the parent: GetLeft/GetRight were wrong).
			FrameRects.Add(Def.Name, FFrameRect(CanvasOffX, CanvasOffY, CanvasOwnerRect.W, CanvasOwnerRect.H));
			UE_LOG(LogWowFrame, Log, TEXT("  %s: No explicit size, using parent size %.1fx%.1f"), *Def.Name, CanvasOwnerRect.W, CanvasOwnerRect.H);
		}
		else
		{
			Slot->SetAutoSize(true);
		}
		return;
	}

	// Handle single anchor positioning
	const FWowAnchor& Anchor = Def.Anchors[0];

	// Get anchor reference frame rect (absolute coords)
	FString AnchorParentName = Anchor.RelativeTo.IsEmpty() ? Def.Parent : Anchor.RelativeTo;
	FFrameRect AnchorParentRect = GetFrameRect(AnchorParentName);

	// Calculate anchor position in absolute WoW coordinates
	FVector2D ParentAnchorPos = FVector2D(AnchorParentRect.X, AnchorParentRect.Y) +
		GetAnchorPointOffset(Anchor.RelativePoint, AnchorParentRect.W, AnchorParentRect.H);

	FVector2D FrameAnchorOffset = GetAnchorPointOffset(Anchor.Point, FrameWidth, FrameHeight);

	// Calculate frame position in absolute WoW coordinates
	// WoW convention: positive OffsetY = UP (toward smaller Y values)
	float AbsX = ParentAnchorPos.X + Anchor.OffsetX - FrameAnchorOffset.X;
	float AbsY = ParentAnchorPos.Y - Anchor.OffsetY - FrameAnchorOffset.Y;

	// clampedToScreen (every GameTooltip, dropdown lists, chat frames): the client keeps the frame
	// inside the screen, so a tooltip of a button at the right edge (minimap zoom, calendar) moves
	// left instead of running off it (Stefan 10.10). Single-anchor sized frames only; not in VR,
	// where frames live in the presenter's zones.
	if ((Def.bClampedToScreen || Def.Type == EWowFrameType::GameTooltip) && Def.Anchors.Num() == 1
		&& FrameWidth > 0.f && FrameHeight > 0.f && !FWowUiVrHost::IsActive())
	{
		const FFrameRect ScreenRect = GetFrameRect(TEXT("UIParent"));
		if (ScreenRect.W > FrameWidth) AbsX = FMath::Clamp(AbsX, ScreenRect.X, ScreenRect.X + ScreenRect.W - FrameWidth);
		if (ScreenRect.H > FrameHeight) AbsY = FMath::Clamp(AbsY, ScreenRect.Y, ScreenRect.Y + ScreenRect.H - FrameHeight);
	}

	// Convert absolute position to relative (within parent canvas)
	float RelX = AbsX - CanvasOffX;
	float RelY = AbsY - CanvasOffY;

	// Sanity check: ensure calculated position is reasonable for the viewport
	// r358: the first three per frame, then one in 500 (AceGUI lays a page out hundreds of times).
	static TMap<FString, int32> GUnusualYCount;
	const bool bUnusualY = RelY > 1500.0f || RelY < -200.0f;
	int32* UnusualCount = bUnusualY ? &GUnusualYCount.FindOrAdd(Def.Name) : nullptr;
	if (UnusualCount && (++*UnusualCount <= 3 || *UnusualCount % 500 == 0))
	{
		UE_LOG(LogWowFrame, Warning, TEXT("Frame %s calculated unusual Y position %.1f (AbsY=%.1f, CanvasOffY=%.1f, ParentAnchor=%.1f, FrameAnchor=%.1f)"),
			*Def.Name, RelY, AbsY, CanvasOffY, ParentAnchorPos.Y, FrameAnchorOffset.Y);
	}

	// Debug log for key frames
	if (Def.Name.Contains(TEXT("Party")) || Def.Name.Contains(TEXT("Target")) || Def.Name.Contains(TEXT("Pet"))
		|| Def.Name.Contains(TEXT("Player")) || Def.Name.Contains(TEXT("MainMenu")) || Def.Name.Contains(TEXT("Minimap")))
	{
		UE_LOG(LogWowFrame, VeryVerbose, TEXT("ANCHOR %s: point=%d relPoint=%d relTo='%s' offset=(%.1f,%.1f) -> abs=(%.1f,%.1f) rel=(%.1f,%.1f) frameSize=(%.1f,%.1f) parentRect=(%.1f,%.1f,%.1f,%.1f)"),
			*Def.Name, (int)Anchor.Point, (int)Anchor.RelativePoint, *AnchorParentName,
			Anchor.OffsetX, Anchor.OffsetY, AbsX, AbsY, RelX, RelY,
			FrameWidth, FrameHeight, AnchorParentRect.X, AnchorParentRect.Y, AnchorParentRect.W, AnchorParentRect.H);
	}

	// Handle two-anchor stretching
	if (Def.Anchors.Num() >= 2)
	{
		const FWowAnchor& Anchor2 = Def.Anchors[1];
		FFrameRect Parent2Rect = GetFrameRect(Anchor2.RelativeTo.IsEmpty() ? Def.Parent : Anchor2.RelativeTo);

		FVector2D Parent2AnchorPos = FVector2D(Parent2Rect.X, Parent2Rect.Y) +
			GetAnchorPointOffset(Anchor2.RelativePoint, Parent2Rect.W, Parent2Rect.H);
		FVector2D Frame2AnchorOffset = GetAnchorPointOffset(Anchor2.Point, FrameWidth, FrameHeight);

		float Abs2X = Parent2AnchorPos.X + Anchor2.OffsetX - Frame2AnchorOffset.X;
		float Abs2Y = Parent2AnchorPos.Y - Anchor2.OffsetY - Frame2AnchorOffset.Y;

		float StretchWidth = Abs2X - AbsX + Frame2AnchorOffset.X - FrameAnchorOffset.X;
		float StretchHeight = Abs2Y - AbsY + Frame2AnchorOffset.Y - FrameAnchorOffset.Y;
		// r228: two points on the same edge (TOPLEFT + TOPRIGHT) only stretch along that edge; the
		// other size is the frame's own (AceGUI title bars, headings, tab rows were 0 high).
		{
			const FVector2D Frac1 = GetAnchorPointOffset(Anchor.Point, 1.0f, 1.0f);
			const FVector2D Frac2 = GetAnchorPointOffset(Anchor2.Point, 1.0f, 1.0f);
			if (FMath::IsNearlyEqual(Frac1.X, Frac2.X)) StretchWidth = FrameWidth;
			if (FMath::IsNearlyEqual(Frac1.Y, Frac2.Y)) StretchHeight = FrameHeight;
		}
		// r281 (Stefan 13.10, /bt4): every anchor counts, like the client - each point fixes the edge
		// or centre it names on each axis, and the size comes from two edges (or an edge and the
		// centre) where there are, else from the frame's own size. Only the first two were used, so
		// AceGUI's TOPLEFT + RIGHT + BOTTOM tree group (and a RIGHT + BOTTOM pair) came out wrong.
		{
			TOptional<float> Lo[2], Hi[2], Mid[2];
			for (const FWowAnchor& Each : Def.Anchors)
			{
				const FFrameRect EachRect = GetFrameRect(Each.RelativeTo.IsEmpty() ? Def.Parent : Each.RelativeTo);
				const FVector2D P = FVector2D(EachRect.X, EachRect.Y)
					+ GetAnchorPointOffset(Each.RelativePoint, EachRect.W, EachRect.H)
					+ FVector2D(Each.OffsetX, -Each.OffsetY);
				const FVector2D F = GetAnchorPointOffset(Each.Point, 1.0f, 1.0f);
				for (int32 Axis = 0; Axis < 2; ++Axis)
				{
					const float Fr = static_cast<float>(Axis == 0 ? F.X : F.Y);
					const float V = static_cast<float>(Axis == 0 ? P.X : P.Y);
					(Fr < .25f ? Lo[Axis] : (Fr > .75f ? Hi[Axis] : Mid[Axis])) = V;
				}
			}
			float Pos[2] = {AbsX, AbsY};
			float Size[2] = {FrameWidth, FrameHeight};
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				const float Own = Axis == 0 ? FrameWidth : FrameHeight;
				if (Lo[Axis] && Hi[Axis]) { Pos[Axis] = *Lo[Axis]; Size[Axis] = *Hi[Axis] - *Lo[Axis]; }
				// r294 (Stefan 13.10, /bt4: "Use Blizzard Vehicle UI" drawn over "Right-click Self-Cast"):
				// an edge plus a centre point (AceGUI's full-width widgets: TOPLEFT + RIGHT) keeps the
				// frame's own size on that axis - the centre only sizes a frame that has none. The r281
				// rule made a 24-high check box 415 high with its label in the middle of the page.
				else if (Lo[Axis] && Mid[Axis] && Own <= 0.f) { Pos[Axis] = *Lo[Axis]; Size[Axis] = 2.f * (*Mid[Axis] - *Lo[Axis]); }
				else if (Hi[Axis] && Mid[Axis] && Own <= 0.f) { Size[Axis] = 2.f * (*Hi[Axis] - *Mid[Axis]); Pos[Axis] = *Hi[Axis] - Size[Axis]; }
				else if (Lo[Axis]) { Pos[Axis] = *Lo[Axis]; Size[Axis] = Own; }
				else if (Hi[Axis]) { Size[Axis] = Own; Pos[Axis] = *Hi[Axis] - Own; }
				else if (Mid[Axis]) { Size[Axis] = Own; Pos[Axis] = *Mid[Axis] - Own * .5f; }
			}
			AbsX = Pos[0]; AbsY = Pos[1];
			StretchWidth = FMath::Max(0.f, Size[0]);
			StretchHeight = FMath::Max(0.f, Size[1]);
			RelX = AbsX - CanvasOffX;
			RelY = AbsY - CanvasOffY;
		}

		Slot->SetAnchors(FAnchors(0.0f, 0.0f));
		Slot->SetAlignment(FVector2D(0.0f, 0.0f));
		FVector2D FinalPosition(RelX * UIScale, RelY * UIScale);
		Slot->SetPosition(FinalPosition);
		Slot->SetSize(FVector2D(StretchWidth * UIScale, StretchHeight * UIScale));

		// Store ABSOLUTE rect for other frames to anchor to
		FrameRects.Add(Def.Name, FFrameRect(AbsX, AbsY, StretchWidth, StretchHeight));
	}
	else
	{
		// Single anchor positioning
		Slot->SetAnchors(FAnchors(0.0f, 0.0f));
		Slot->SetAlignment(FVector2D(0.0f, 0.0f));
		FVector2D FinalPosition(RelX * UIScale, RelY * UIScale);
		Slot->SetPosition(FinalPosition);

		// One explicit dimension counts as sized (Bartender: SetWidth(1) then SetHeight(1)).
		// Borrowing the parent's 1365-unit width for that moment made LibWindow save the
		// bar 683 units off the right edge of the screen.
		if (FrameWidth > 0.f || FrameHeight > 0.f)
		{
			Slot->SetSize(FVector2D(FrameWidth * UIScale, FrameHeight * UIScale));
			// Store ABSOLUTE rect for other frames to anchor to
			FrameRects.Add(Def.Name, FFrameRect(AbsX, AbsY, FrameWidth, FrameHeight));
		}
		else if (UsesOriginalSizing(Def))
		{
			// Original rule: one anchor and no size is a 0x0 frame (AceGUI panes, LibWindow bars).
			// Borrowing the parent's size made panes cover the screen and bars save off-screen.
			Slot->SetSize(FVector2D::ZeroVector);
			FrameRects.Add(Def.Name, FFrameRect(AbsX, AbsY, 0.f, 0.f));
		}
		else if (CanvasOwnerRect.W > 0.f && CanvasOwnerRect.H > 0.f)
		{
			// No explicit size but parent has size - use parent size to avoid 100x30 default
			Slot->SetSize(FVector2D(CanvasOwnerRect.W * UIScale, CanvasOwnerRect.H * UIScale));
			// Store ABSOLUTE rect for other frames to anchor to
			FrameRects.Add(Def.Name, FFrameRect(AbsX, AbsY, CanvasOwnerRect.W, CanvasOwnerRect.H));
			UE_LOG(LogWowFrame, Log, TEXT("  %s: Single anchor, no explicit size, using parent size %.1fx%.1f"), *Def.Name, CanvasOwnerRect.W, CanvasOwnerRect.H);
		}
		else
		{
			Slot->SetAutoSize(true);
			// Store ABSOLUTE rect for other frames to anchor to (use zero size for auto-sized frames)
			FrameRects.Add(Def.Name, FFrameRect(AbsX, AbsY, 0.f, 0.f));
		}
	}
}

// ── Layer Content (Textures & FontStrings) ───────────────────────────────────

void FWowFrameManager::ApplyElementAnchors(UWidget* Widget, UCanvasPanel* Parent,
	const TArray<FWowAnchor>& Anchors, float Width, float Height, bool bSetAllPoints,
	float ParentFrameW, float ParentFrameH, float ParentFrameX, float ParentFrameY)
{
	UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot);
	if (!Slot) return;

	// Use passed-in parent dimensions (WoW coords) — GetDesiredSize() returns 0 at creation time
	float ParentW = ParentFrameW;
	float ParentH = ParentFrameH;

	if (bSetAllPoints)
	{
		// SetAllPoints=true: explicitly fill parent
		Slot->SetAnchors(FAnchors(0.0f, 0.0f));
		Slot->SetAlignment(FVector2D(0.0f, 0.0f));
		Slot->SetPosition(FVector2D(0.0f, 0.0f));
		if (ParentW > 0.f && ParentH > 0.f)
		{
			Slot->SetSize(FVector2D(ParentW * UIScale, ParentH * UIScale));
		}
		else
		{
			Slot->SetAutoSize(true);
		}
		return;
	}

	if (Anchors.IsEmpty())
	{
		// No anchors and no SetAllPoints: position at (0,0), use explicit size
		// or fall back to the widget's native image dimensions.
		// Do NOT fill parent — that causes small decorative textures to cover the screen.
		Slot->SetAnchors(FAnchors(0.0f, 0.0f));
		Slot->SetAlignment(FVector2D(0.0f, 0.0f));
		Slot->SetPosition(FVector2D(0.0f, 0.0f));

		if (Width > 0.f && Height > 0.f)
		{
			Slot->SetSize(FVector2D(Width * UIScale, Height * UIScale));
		}
		else
		{
			// Use native image size if available
			UImage* ImgWidget = Cast<UImage>(Widget);
			if (ImgWidget)
			{
				FSlateBrush Brush = ImgWidget->GetBrush();
				if (Brush.ImageSize.X > 0.f && Brush.ImageSize.Y > 0.f)
				{
					Slot->SetSize(FVector2D(Brush.ImageSize.X * UIScale, Brush.ImageSize.Y * UIScale));
				}
				else
				{
					Slot->SetAutoSize(true);
				}
			}
			else
			{
				Slot->SetAutoSize(true);
			}
		}
		return;
	}

	// Auto-sized text must align its measured bottom/right edge to the anchor.
	// A fixed top-left at BOTTOMRIGHT puts aura stacks below the next icon.
	// Native canvas anchors also follow Lua's later aura-size changes.
	// The same for text anchored to another region (a label's RIGHT, a header's BOTTOM): the text's
	// own anchor point goes on that point. With a zero size its top-left went there, so the PvP
	// window's "0" values hung below and right of their labels (Stefan 10.10).
	if (Cast<UTextBlock>(Widget) && Anchors.Num() == 1 && !Anchors[0].RelativeTo.IsEmpty()
		&& Width <= 0.0f && Height <= 0.0f && FrameRects.Contains(Anchors[0].RelativeTo))
	{
		const FWowAnchor& TextAnchor = Anchors[0];
		const FFrameRect& ReferenceRect = FrameRects[TextAnchor.RelativeTo];
		const FVector2D ReferencePoint = FVector2D(ReferenceRect.X - ParentFrameX, ReferenceRect.Y - ParentFrameY)
			+ GetAnchorPointOffset(TextAnchor.RelativePoint, ReferenceRect.W, ReferenceRect.H);
		Slot->SetAnchors(FAnchors(0.0f, 0.0f));
		Slot->SetAlignment(GetAnchorPointOffset(TextAnchor.Point, 1.0f, 1.0f));
		Slot->SetPosition((ReferencePoint + FVector2D(TextAnchor.OffsetX, -TextAnchor.OffsetY)) * UIScale);
		Slot->SetAutoSize(true);
		return;
	}
	if (Cast<UTextBlock>(Widget) && Anchors.Num() == 1 && Anchors[0].RelativeTo.IsEmpty()
		&& Width <= 0.0f && Height <= 0.0f)
	{
		const FWowAnchor& TextAnchor = Anchors[0];
		const FVector2D ParentPoint = GetAnchorPointOffset(TextAnchor.RelativePoint, 1.0f, 1.0f);
		Slot->SetAnchors(FAnchors(ParentPoint.X, ParentPoint.Y));
		Slot->SetAlignment(GetAnchorPointOffset(TextAnchor.Point, 1.0f, 1.0f));
		Slot->SetPosition(FVector2D(TextAnchor.OffsetX, -TextAnchor.OffsetY) * UIScale);
		Slot->SetAutoSize(true);
		return;
	}

	// For layer elements, positioning is relative to the parent frame (which is a canvas)
	const FWowAnchor& Anchor = Anchors[0];

	// Calculate the reference anchor in the owning canvas's local coordinates.
	// Layer regions may anchor to a sibling texture/fontstring or another named
	// frame. FrameRects stores absolute WoW coordinates, so subtract the owner
	// frame origin before assigning the UCanvasPanel slot.
	auto ResolveReferenceAnchor = [this, ParentW, ParentH, ParentFrameX, ParentFrameY](const FWowAnchor& InAnchor)
	{
		if (!InAnchor.RelativeTo.IsEmpty())
		{
			if (FrameRects.Contains(InAnchor.RelativeTo))
			{
				const FFrameRect ReferenceRect = GetFrameRect(InAnchor.RelativeTo); // r278: follows its frame
				return FVector2D(ReferenceRect.X - ParentFrameX, ReferenceRect.Y - ParentFrameY)
					+ GetAnchorPointOffset(InAnchor.RelativePoint, ReferenceRect.W, ReferenceRect.H);
			}
		}
		return GetAnchorPointOffset(InAnchor.RelativePoint, ParentW, ParentH);
	};

	FVector2D ParentAnchorPos = ResolveReferenceAnchor(Anchor);

	// r263 (Stefan 12.10: "Resurrect now?" sat at the left of the dialog): a fontstring with a width
	// but no height (StaticPopup's 290 x 0 text at TOP) is auto-sized to its text, so placing the
	// 290-wide box's top-left put short text at the box's left edge. Place the text by its
	// justification inside that box instead, following the parent's anchor point (the dialog
	// widens to 320/420 after creation).
	if (UTextBlock* SizedText = Cast<UTextBlock>(Widget); SizedText && Anchors.Num() == 1 && Width > 0.f && Height <= 0.f)
	{
		const FVector2D Frac = GetAnchorPointOffset(Anchor.Point, 1.0f, 1.0f);
		// r424: UE 5.8's UTextBlock has no GetJustification() (the property is protected); read it by reflection.
		ETextJustify::Type Justify = ETextJustify::Left;
		if (const FProperty* JustifyProp = UTextLayoutWidget::StaticClass()->FindPropertyByName(TEXT("Justification")))
			Justify = static_cast<ETextJustify::Type>(*JustifyProp->ContainerPtrToValuePtr<uint8>(SizedText));
		const float J = Justify == ETextJustify::Left ? 0.0f : Justify == ETextJustify::Right ? 1.0f : 0.5f;
		const float ShiftX = (J - float(Frac.X)) * Width;
		if (Anchor.RelativeTo.IsEmpty())
		{
			const FVector2D ParentFrac = GetAnchorPointOffset(Anchor.RelativePoint, 1.0f, 1.0f);
			Slot->SetAnchors(FAnchors(ParentFrac.X, ParentFrac.Y));
			Slot->SetPosition(FVector2D(Anchor.OffsetX + ShiftX, -Anchor.OffsetY) * UIScale);
		}
		else
		{
			Slot->SetAnchors(FAnchors(0.0f, 0.0f));
			Slot->SetPosition(FVector2D(ParentAnchorPos.X + Anchor.OffsetX + ShiftX, ParentAnchorPos.Y - Anchor.OffsetY) * UIScale);
		}
		Slot->SetAlignment(FVector2D(J, Frac.Y));
		Slot->SetAutoSize(true);
		return;
	}

	// Calculate element anchor offset
	FVector2D ElementAnchorOffset = GetAnchorPointOffset(Anchor.Point, Width, Height);

	// Calculate final element position: parent_anchor + wow_offset - element_anchor_offset
	// WoW convention: positive Y = UP, but UMG canvas Y increases downward, so negate OffsetY
	float FinalX = ParentAnchorPos.X + Anchor.OffsetX - ElementAnchorOffset.X;
	float FinalY = ParentAnchorPos.Y - Anchor.OffsetY - ElementAnchorOffset.Y;

	// Apply positioning
	Slot->SetAnchors(FAnchors(0.0f, 0.0f));
	Slot->SetAlignment(FVector2D(0.0f, 0.0f));
	Slot->SetPosition(FVector2D(FinalX * UIScale, FinalY * UIScale));

	if (Width > 0.f && Height > 0.f)
	{
		FVector2D NewSize(Width * UIScale, Height * UIScale);
		Slot->SetSize(NewSize);
		UE_LOG(LogWowFrame, Verbose, TEXT("    ApplyAnchors: pos=(%.0f,%.0f) size=(%.0f,%.0f) [from Width=%.0f Height=%.0f scale=%.3f]"),
			FinalX * UIScale, FinalY * UIScale, NewSize.X, NewSize.Y, Width, Height, UIScale);
	}
	else
	{
		// No explicit size — use the widget's brush texture dimensions if available,
		// otherwise auto-size. Auto-size doesn't always work at creation time in UMG.
		UImage* ImgCheck = Cast<UImage>(Widget);
		if (ImgCheck)
		{
			FSlateBrush Brush = ImgCheck->GetBrush();
			if (Brush.ImageSize.X > 0.f && Brush.ImageSize.Y > 0.f)
			{
				Slot->SetSize(FVector2D(Brush.ImageSize.X * UIScale, Brush.ImageSize.Y * UIScale));
			}
			else
			{
				Slot->SetAutoSize(true);
			}
		}
		else
		{
			Slot->SetAutoSize(true);
		}
	}

	// Handle two-anchor stretching for layer elements
	if (Anchors.Num() >= 2)
	{
		const FWowAnchor& Anchor2 = Anchors[1];

		// Calculate second anchor positions
		FVector2D Parent2AnchorPos = ResolveReferenceAnchor(Anchor2);
		FVector2D Element2AnchorOffset = GetAnchorPointOffset(Anchor2.Point, Width, Height);

		float Final2X = Parent2AnchorPos.X + Anchor2.OffsetX - Element2AnchorOffset.X;
		float Final2Y = Parent2AnchorPos.Y - Anchor2.OffsetY - Element2AnchorOffset.Y;

		// Element stretches between the two positions
		float StretchWidth = Final2X - FinalX + Element2AnchorOffset.X - ElementAnchorOffset.X;
		float StretchHeight = Final2Y - FinalY + Element2AnchorOffset.Y - ElementAnchorOffset.Y;
		// r228: same-edge points stretch one axis only (see ApplyAnchors).
		{
			const FVector2D Frac1 = GetAnchorPointOffset(Anchor.Point, 1.0f, 1.0f);
			const FVector2D Frac2 = GetAnchorPointOffset(Anchor2.Point, 1.0f, 1.0f);
			if (FMath::IsNearlyEqual(Frac1.X, Frac2.X) && Width > 0.f) StretchWidth = Width;
			if (FMath::IsNearlyEqual(Frac1.Y, Frac2.Y) && Height > 0.f) StretchHeight = Height;
		}

		Slot->SetPosition(FVector2D(FinalX * UIScale, FinalY * UIScale));
		Slot->SetSize(FVector2D(StretchWidth * UIScale, StretchHeight * UIScale));
	}
}

void FWowFrameManager::CreateLayerContent(UCanvasPanel* Container, const FWowFrameDef& Def, int64 OwnerHandle)
{
	if (!Container) return;

	// Resolve effective parent dimensions for element anchoring.
	// Frames with SetAllPoints=true or no explicit size have Width/Height=0 in the def,
	// but their actual size is computed during ApplyAnchors and cached in FrameRects.
	float EffectiveParentW = Def.Width;
	float EffectiveParentH = Def.Height;
	float EffectiveParentX = 0.f;
	float EffectiveParentY = 0.f;
	if (const FFrameRect* OwnerRect = FrameRects.Find(Def.Name))
	{
		EffectiveParentX = OwnerRect->X;
		EffectiveParentY = OwnerRect->Y;
	}
	if (EffectiveParentW <= 0.f || EffectiveParentH <= 0.f)
	{
		if (FrameRects.Contains(Def.Name))
		{
			const FFrameRect& Rect = FrameRects[Def.Name];
			if (Rect.W > 0.f) EffectiveParentW = Rect.W;
			if (Rect.H > 0.f) EffectiveParentH = Rect.H;
		}
		if (EffectiveParentW <= 0.f || EffectiveParentH <= 0.f)
		{
			UE_LOG(LogWowFrame, Warning, TEXT("CreateLayerContent: %s has zero effective size (def=%.0fx%.0f rect=%.0fx%.0f) — element anchors will collapse"),
				*Def.Name, Def.Width, Def.Height, EffectiveParentW, EffectiveParentH);
		}
	}

	int32 TotalTextures = 0, TotalFontStrings = 0, NamedTextures = 0;
	for (const FWowLayer& L : Def.Layers) { TotalTextures += L.Textures.Num(); TotalFontStrings += L.FontStrings.Num(); for (const auto& T : L.Textures) { if (!T.Name.IsEmpty()) NamedTextures++; } }
	if (TotalTextures > 0 || TotalFontStrings > 0)
	{
		UE_LOG(LogWowFrame, Log, TEXT("CreateLayerContent: %s — %d layers, %d textures (%d named), %d fontstrings, effectiveSize=%.0fx%.0f"),
			*Def.Name, Def.Layers.Num(), TotalTextures, NamedTextures, TotalFontStrings, EffectiveParentW, EffectiveParentH);
	}

	UObject* Outer = Container->GetOuter();
	if (!Outer) Outer = GetTransientPackage();

	// Layer draw order: BACKGROUND(0), BORDER(1), ARTWORK(2), OVERLAY(3), HIGHLIGHT(4)
	int32 LayerZBase = 0;

	for (const FWowLayer& Layer : Def.Layers)
	{
		int32 ZOrder = static_cast<int32>(Layer.Level) * 100;

		// Create UImage widgets for textures
		for (const FWowTextureElement& Tex : Layer.Textures)
		{
			const bool bRuntimeEquipmentManagerOverlay =
				Tex.File.Contains(TEXT("UI-GearManager-LeaveItem-Transparent"), ESearchCase::IgnoreCase);
			UImage* ImgWidget = NewObject<UImage>(Outer);
			// Start with transparent brush — prevents default UE white/grey square
			// from showing before the BLP texture is loaded (or for empty textures).
			FSlateBrush TransparentBrush;
			TransparentBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
			ImgWidget->SetBrush(TransparentBrush);
			ImgWidget->SetColorAndOpacity(WowColor::FromWow(Tex.VertexColor));
			if (Tex.bHidden || bRuntimeEquipmentManagerOverlay)
			{
				ImgWidget->SetVisibility(ESlateVisibility::Collapsed);
				ImgWidget->SetRenderOpacity(0.0f);
			}

			// Register named texture regions for Lua access + create Lua global
			if (!Tex.Name.IsEmpty())
			{
				// Named FrameXML regions are Lua globals and must be unique.  Some
				// inherited templates materialise a region twice; keep the newest
				// definition and collapse the stale widget instead of leaving detached
				// portraits, rings, or labels on the root canvas.
				if (TWeakObjectPtr<UImage>* Previous = NamedTextureWidgets.Find(Tex.Name);
					Previous && Previous->IsValid() && Previous->Get() != ImgWidget)
				{
					Previous->Get()->SetVisibility(ESlateVisibility::Collapsed);
				}
				NamedTextureWidgets.Add(Tex.Name, ImgWidget);

				// Create a Lua global table for this texture region so scripts can call
				// MainMenuBarLeftEndCap:SetTexture("Interface\\...")
				if (EventSystem)
				{
					EventSystem->CreateTextureRegionGlobal(Tex.Name, OwnerHandle, TEXT("Texture"));
					UE_LOG(LogWowFrame, Verbose, TEXT("    Registered texture region global: %s"), *Tex.Name);
				}
				else
				{
					UE_LOG(LogWowFrame, Warning, TEXT("    NO EventSystem for texture region: %s"), *Tex.Name);
				}
			}

			// If no file specified, hide until Lua sets a texture via SetTexture()
			if (Tex.File.IsEmpty())
			{
				ImgWidget->SetRenderOpacity(0.0f);
			}

			// Load the BLP texture if specified
			if (!Tex.File.IsEmpty())
			{
				UTexture2D* LoadedTexture = LoadUITexture(Tex.File);
				if (LoadedTexture)
				{
					// Create slate brush with texture and UV coordinates
					FSlateBrush SlateBrush;
					SlateBrush.SetResourceObject(LoadedTexture);
					SlateBrush.DrawAs = ESlateBrushDrawType::Image;
					float FullW = (float)LoadedTexture->GetSizeX();
					float FullH = (float)LoadedTexture->GetSizeY();
					SlateBrush.Tiling = ESlateBrushTileType::NoTile;

					// Apply texture coordinates if specified (default is 0,0,1,1 for full texture)
					bool bHasSubRegion = (Tex.Left != 0.0f || Tex.Right != 1.0f || Tex.Top != 0.0f || Tex.Bottom != 1.0f);
					if (bHasSubRegion)
					{
						// WoW uses Left,Right,Top,Bottom UV coordinates.
						// FBox2D expects Min < Max, but WoW uses Left > Right for horizontal flip
						// and Top > Bottom for vertical flip.  Normalize the UVs and apply
						// a RenderTransform to handle flipping.
						float UVLeft  = FMath::Min(Tex.Left, Tex.Right);
						float UVRight = FMath::Max(Tex.Left, Tex.Right);
						float UVTop   = FMath::Min(Tex.Top, Tex.Bottom);
						float UVBot   = FMath::Max(Tex.Top, Tex.Bottom);

						FBox2D UVRegion(FVector2D(UVLeft, UVTop), FVector2D(UVRight, UVBot));
						SlateBrush.SetUVRegion(UVRegion);

						// ImageSize = visible sub-region size (not full atlas)
						// so fallback sizing uses correct dimensions
						SlateBrush.ImageSize = FVector2D(
							FullW * (UVRight - UVLeft),
							FullH * (UVBot - UVTop));

						// Apply render transform for flipped texcoords
						bool bFlipH = Tex.Left > Tex.Right;
						bool bFlipV = Tex.Top > Tex.Bottom;
						if (bFlipH || bFlipV)
						{
							FVector2D Scale(bFlipH ? -1.0f : 1.0f, bFlipV ? -1.0f : 1.0f);
							ImgWidget->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
							ImgWidget->SetRenderTransform(FWidgetTransform(FVector2D::ZeroVector, Scale, FVector2D::ZeroVector, 0.0f));
						}
					}
					else
					{
						SlateBrush.ImageSize = FVector2D(FullW, FullH);
					}

					ImgWidget->SetBrush(SlateBrush);
					if (Tex.Left > Tex.Right || Tex.Top > Tex.Bottom)
					{
						UE_LOG(LogWowFrame, Warning, TEXT("  FLIPPED texture: %s in frame %s (UV: L=%.3f R=%.3f T=%.3f B=%.3f) flipH=%d flipV=%d"),
							*Tex.File, *Def.Name, Tex.Left, Tex.Right, Tex.Top, Tex.Bottom,
							Tex.Left > Tex.Right ? 1 : 0, Tex.Top > Tex.Bottom ? 1 : 0);
					}
					UE_LOG(LogWowFrame, Verbose, TEXT("  Applied texture: %s (UV: %.3f,%.3f to %.3f,%.3f)"),
						*Tex.File, Tex.Left, Tex.Top, Tex.Right, Tex.Bottom);
				}
				else
				{
					UE_LOG(LogWowFrame, Warning, TEXT("  Failed to load texture: %s for frame %s"), *Tex.File, *Def.Name);
				}
				// Log texture with position info
				UCanvasPanelSlot* DbgSlot = Cast<UCanvasPanelSlot>(ImgWidget->Slot);
				FVector2D DbgPos = DbgSlot ? DbgSlot->GetPosition() : FVector2D::ZeroVector;
				FVector2D DbgSize = DbgSlot ? DbgSlot->GetSize() : FVector2D::ZeroVector;
				UE_LOG(LogWowFrame, Log, TEXT("  TEX: %s -> %s (%s) xmlSize=%.0fx%.0f slot=(%.0f,%.0f) slotSz=(%.0f,%.0f) anchors=%d setAll=%d"),
					*Tex.File, LoadedTexture ? TEXT("OK") : TEXT("FAIL"), *Def.Name, Tex.Width, Tex.Height,
					DbgPos.X, DbgPos.Y, DbgSize.X, DbgSize.Y, Tex.Anchors.Num(), Tex.bSetAllPoints ? 1 : 0);
			}

			UCanvasPanelSlot* Slot = Container->AddChildToCanvas(ImgWidget);
			if (Slot)
			{
				Slot->SetZOrder(ZOrder++);
			}
			// r237: the HIGHLIGHT layer only shows while the mouse is over the frame.
			if (Layer.Level == EWowDrawLayer::HIGHLIGHT && OwnerHandle >= 0)
			{
				RegisterHighlightLayerRegion(OwnerHandle, ImgWidget, !Tex.bHidden);
			}

			// A texture with neither size nor anchors fills its parent in the client
			// (ActionButtonTemplate's $parentIcon, MultiCastSlotButtonTemplate's background);
			// sizing it to the image showed whole atlases (UI-TotemBar) at native size.
			const bool bFillParent = Tex.bSetAllPoints
				|| (Tex.Width <= 0.f && Tex.Height <= 0.f && Tex.Anchors.Num() == 0);
			ApplyElementAnchors(ImgWidget, Container, Tex.Anchors, Tex.Width, Tex.Height, bFillParent,
				EffectiveParentW, EffectiveParentH, EffectiveParentX, EffectiveParentY);

			// For textures with anchors but no explicit size that auto-sized,
			// override with texture native size × UIScale (prevents oversized rendering)
			if (Tex.Width <= 0.f && Tex.Height <= 0.f && !Tex.bSetAllPoints && Tex.Anchors.Num() > 0)
			{
				UCanvasPanelSlot* TexSlot = Cast<UCanvasPanelSlot>(ImgWidget->Slot);
				if (TexSlot)
				{
					FSlateBrush CurrentBrush = ImgWidget->GetBrush();
					FVector2D ImgSize = CurrentBrush.ImageSize;
					if (ImgSize.X > 0.f && ImgSize.Y > 0.f)
					{
						TexSlot->SetAutoSize(false);
						TexSlot->SetSize(FVector2D(ImgSize.X * UIScale, ImgSize.Y * UIScale));
					}
				}
			}

			// Register named texture elements so other frames can anchor to them
			if (!Tex.Name.IsEmpty())
			{
				// Calculate and store absolute rect for this texture element.
				// WoW XML allows frames to anchor relativeTo a named texture/fontstring.
				UCanvasPanelSlot* TexSlot2 = Cast<UCanvasPanelSlot>(ImgWidget->Slot);
				if (TexSlot2)
				{
					// Get the parent frame's absolute position
					FFrameRect ParentAbsRect = FrameRects.Contains(Def.Name)
						? FrameRects[Def.Name]
						: FFrameRect(0.f, 0.f, EffectiveParentW, EffectiveParentH);

					// Element position within parent canvas (in WoW coords, pre-scale)
					FVector2D SlotPos = TexSlot2->GetPosition();
					FVector2D SlotSize = TexSlot2->GetSize();
					float ElemX = ParentAbsRect.X + SlotPos.X / UIScale;
					float ElemY = ParentAbsRect.Y + SlotPos.Y / UIScale;
					float ElemW = SlotSize.X / UIScale;
					float ElemH = SlotSize.Y / UIScale;

					FrameRects.Add(Tex.Name, FFrameRect(ElemX, ElemY, ElemW, ElemH));
					NoteRegionOwnerAt(Tex.Name, Def.Name, ParentAbsRect.X, ParentAbsRect.Y);
					UE_LOG(LogWowFrame, Log, TEXT("  Registered texture rect: %s abs=(%.1f,%.1f) sz=(%.1f,%.1f)"),
						*Tex.Name, ElemX, ElemY, ElemW, ElemH);
				}
			}
		}

		// Create UTextBlock widgets for fontstrings
		for (const FWowFontStringElement& FS : Layer.FontStrings)
		{
            // Arrow artwork already communicates previous/next page.
            if (Def.Name==TEXT("SpellBookPrevPageButton") || Def.Name==TEXT("SpellBookNextPageButton")) continue;
			UTextBlock* TextWidget = NewObject<UTextBlock>(Outer);

			if (!FS.Text.IsEmpty())
			{
				TextWidget->SetText(FText::FromString(ResolveXmlText(FS.Text)));
			}

			// The fontstring's font object (inherits="GameFontNormalSmall"...) gives its size and colour
			// unless the fontstring sets them itself, as in the client. Every inherited fontstring was
			// drawn 12 pt white: the PvP window's grey 10 pt labels came out large and white and ran
			// into each other (Stefan 10.10).
			const FString FontObjectName = FS.Inherits.IsEmpty() ? TEXT("GameFontNormal") : FS.Inherits;
			const FWowFontManager::FFontStyle* FontStyle = FontManager ? FontManager->FindFontStyle(FontObjectName) : nullptr;
			const float FontHeight = !FS.bHasFontHeight && FontStyle && FontStyle->bHasHeight ? FontStyle->Height : FS.FontHeight;
			const FLinearColor FontColor = !FS.bHasColor && FontStyle && FontStyle->bHasColor ? FontStyle->Color : FS.Color;

			// Apply color
			TextWidget->SetColorAndOpacity(FSlateColor(WowColor::FromWow(FontColor)));

			// Apply font from font manager or fallback to default
			FSlateFontInfo FontInfo;
			if (FontManager && FontManager->IsInitialized())
			{
				// r270 (Stefan 13.10: the dressing room's title, its instructions and the button captions
				// were too large): a font object's height is its em size in WoW units, while a Slate font
				// size is in points (96 DPI: 1 pt = 4/3 px) - 0.75 x, as the native windows already do.
				int32 ScaledFontSize = FMath::Max(1, FMath::RoundToInt(FontHeight * UIScale * 0.75f));
				FontInfo = FontManager->GetFont(FontObjectName, ScaledFontSize);
			}
			else
			{
				// Fallback to default UE font (also scale the size)
				FontInfo = TextWidget->GetFont();
				FontInfo.Size = FMath::Max(1, FMath::RoundToInt(FontHeight * UIScale * 0.75f));
			}
			TextWidget->SetFont(FontInfo);

			// Apply justification. Without justifyH the font object decides: the ...Left / ...Right
			// fonts (GameFontNormalLeft, GameFontHighlightSmallLeft, NumberFontNormalRight ...) are
			// LEFT / RIGHT, every other font CENTER, as in the client.
			FString Justify = FS.JustifyH.ToUpper();
			if (Justify.IsEmpty())
				Justify = FontObjectName.EndsWith(TEXT("Left")) ? TEXT("LEFT")
					: FontObjectName.EndsWith(TEXT("Right")) ? TEXT("RIGHT") : TEXT("CENTER");
			if (Justify == TEXT("LEFT"))
				TextWidget->SetJustification(ETextJustify::Left);
			else if (Justify == TEXT("RIGHT"))
				TextWidget->SetJustification(ETextJustify::Right);
			else
				TextWidget->SetJustification(ETextJustify::Center);
			// A box with room for more than one line wraps its text inside the width, as the client
			// does (the Dressing Room's "CTRL-Left Click additional items ..." ran past the window).
			if (FS.Width > 0.0f && FS.Height >= FontHeight * 1.8f)
			{
				TextWidget->SetAutoWrapText(true);
			}
			else if (FS.Width > 0.0f && FS.Height <= 0.0f)
			{
				// r380 (Stefan 16.10: a faction's description in ReputationDetailFrame ran far past the
				// window): a fontstring with a width and no height wraps at that width and grows down,
				// as in the client (ReputationDetailFactionDescription is 170 x 0). It is auto-sized,
				// so wrap at the width itself rather than at the slot.
				TextWidget->SetWrapTextAt(FS.Width * UIScale);
			}

			UCanvasPanelSlot* Slot = Container->AddChildToCanvas(TextWidget);
			if (Slot)
			{
				Slot->SetZOrder(ZOrder++);
			}

			ApplyElementAnchors(TextWidget, Container, FS.Anchors, FS.Width, FS.Height, false,
				EffectiveParentW, EffectiveParentH, EffectiveParentX, EffectiveParentY);

			if (!FS.Name.IsEmpty())
			{
				// Create a Lua global for this fontstring so scripts can access it
				if (EventSystem)
				{
					EventSystem->CreateTextureRegionGlobal(FS.Name, OwnerHandle, TEXT("FontString"));
				}
				if (TWeakObjectPtr<UTextBlock>* Previous = NamedFontStringWidgets.Find(FS.Name);
					Previous && Previous->IsValid() && Previous->Get() != TextWidget)
				{
					Previous->Get()->SetVisibility(ESlateVisibility::Collapsed);
				}
				NamedFontStringWidgets.Add(FS.Name, TextWidget);
				// Warn if fontstring name still contains unresolved $parent
				if (FS.Name.Contains(TEXT("$parent")))
				{
					UE_LOG(LogWowFrame, Warning, TEXT("  UNRESOLVED $parent in fontstring: %s (frame: %s)"), *FS.Name, *Def.Name);
				}

				if (!Def.Name.IsEmpty() && FS.Name == (Def.Name + TEXT("Text")))
				{
					PrimaryTextWidgets.Add(OwnerHandle, TextWidget);
				}

				// Register fontstring rect so other frames can anchor to it
				UCanvasPanelSlot* FSSlot = Cast<UCanvasPanelSlot>(TextWidget->Slot);
				if (FSSlot)
				{
					FFrameRect ParentAbsRect = FrameRects.Contains(Def.Name)
						? FrameRects[Def.Name]
						: FFrameRect(0.f, 0.f, EffectiveParentW, EffectiveParentH);
					FVector2D SlotPos = FSSlot->GetPosition();
					FVector2D SlotSize = FSSlot->GetSize();
					float ElemX = ParentAbsRect.X + SlotPos.X / UIScale;
					float ElemY = ParentAbsRect.Y + SlotPos.Y / UIScale;
					float ElemW = SlotSize.X / UIScale;
					float ElemH = SlotSize.Y / UIScale;
					// Auto-sized text: its rect is the measured text placed by the slot's anchor and
					// alignment, so regions anchored to its RIGHT/BOTTOM land after the text.
					if (FSSlot->GetAutoSize() && FSlateApplication::IsInitialized() && UIScale > 0.f)
					{
						const FVector2D Measured = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(
							TextWidget->GetText().ToString(), TextWidget->GetFont()) / UIScale;
						const FAnchors SlotAnchors = FSSlot->GetAnchors();
						const FVector2D AnchorBase(SlotAnchors.Minimum.X * EffectiveParentW, SlotAnchors.Minimum.Y * EffectiveParentH);
						const FVector2D TopLeft = AnchorBase + SlotPos / UIScale - FSSlot->GetAlignment() * Measured;
						ElemX = ParentAbsRect.X + TopLeft.X;
						ElemY = ParentAbsRect.Y + TopLeft.Y;
						ElemW = Measured.X;
						ElemH = Measured.Y;
					}
					FrameRects.Add(FS.Name, FFrameRect(ElemX, ElemY, ElemW, ElemH));
					NoteRegionOwnerAt(FS.Name, Def.Name, ParentAbsRect.X, ParentAbsRect.Y);
				}
			}
		}
	}
}

// ── Backdrop Creation ────────────────────────────────────────────────────────

UWowBackdropWidget* FWowFrameManager::CreateBackdrop(UCanvasPanel* Container, const FWowBackdrop& Backdrop, int64 Handle)
{
	if (!Container) return nullptr;
	UWowBackdropWidget* Widget = nullptr;
	if (const TWeakObjectPtr<UWowBackdropWidget>* Found = BackdropWidgets.Find(Handle); Found && Found->IsValid())
	{
		Widget = Found->Get();
	}
	if (!Widget)
	{
		Widget = NewObject<UWowBackdropWidget>(Container);
		if (UCanvasPanelSlot* Slot = Container->AddChildToCanvas(Widget))
		{
			Slot->SetZOrder(-1000); // behind every layer
			Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			Slot->SetOffsets(FMargin(0.0f));
		}
		if (Handle >= 0) BackdropWidgets.Add(Handle, Widget);
	}
	// Background inside the insets (stretched or tiled), 8-piece edge on the frame bounds.
	UTexture2D* Background = Backdrop.BgFile.IsEmpty() ? nullptr : LoadUITexture(Backdrop.BgFile);
	// Drop-down menus (DropDownList1/2...) get a solid dark background: the dialog texture under the
	// tooltip tint was too see-through to read (Stefan 10.10: "make it darker, as a standard").
	const bool bDropDownMenu = Handle >= 0 && GetFrameName(Handle).StartsWith(TEXT("DropDownList"));
	// Tooltips too: UI-Tooltip-Background's own alpha under the 95 % tint still let the character
	// sheet's stats read through (Stefan 10.10), so the tint alone sets how opaque they are.
	if (bDropDownMenu || Backdrop.BgFile.Contains(TEXT("UI-Tooltip-Background")))
		if (UTexture2D* Solid = LoadUITexture(TEXT("Interface\\Buttons\\WHITE8X8"))) Background = Solid;
	UTexture2D* Edge = Backdrop.EdgeFile.IsEmpty() ? nullptr : LoadUITexture(Backdrop.EdgeFile);
	Widget->SetBackdrop(Background, Edge, Backdrop.EdgeSize * UIScale, Backdrop.Tile, Backdrop.TileSize * UIScale,
		FMargin(Backdrop.InsetLeft * UIScale, Backdrop.InsetTop * UIScale, Backdrop.InsetRight * UIScale, Backdrop.InsetBottom * UIScale));
	Widget->SetVisibility(Background || Edge ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	// Tooltips (GameTooltip, item/compare tooltips, addon tooltips on UI-Tooltip-Background) all
	// get the chat window's black, half-transparent background.
	if (Handle >= 0)
	{
		if (Backdrop.BgFile.Contains(TEXT("UI-Tooltip-Background")) || GetFrameName(Handle).StartsWith(TEXT("DropDownList")))
			TooltipBackdrops.Add(Handle);
		else TooltipBackdrops.Remove(Handle);
		if (TooltipBackdrops.Contains(Handle)) Widget->SetBackgroundColor(bDropDownMenu
			? WowColor::FromWow(0.02f, 0.02f, 0.03f, 0.98f) : FWowWindowStyle::TooltipBackgroundColor());
	}
	return Widget;
}

void FWowFrameManager::SetFrameBackdrop(int64 Handle, const FWowBackdrop* Backdrop)
{
	if (!Backdrop)
	{
		if (const TWeakObjectPtr<UWowBackdropWidget>* Found = BackdropWidgets.Find(Handle); Found && Found->IsValid())
			(*Found)->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	CreateBackdrop(Cast<UCanvasPanel>(GetWidgetForHandle(Handle)), *Backdrop, Handle);
}

void FWowFrameManager::SetFrameParent(int64 Handle, int64 NewParentHandle)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry || NewParentHandle == Handle || Entry->ParentHandle == NewParentHandle) return;
	// Refuse cycles (a frame can't become its own ancestor).
	for (int64 Ancestor = NewParentHandle, Guard = 0; Ancestor >= 0 && Guard < 128; Ancestor = GetParentHandle(Ancestor), ++Guard)
		if (Ancestor == Handle) return;

	Entry->ParentHandle = NewParentHandle;
	Entry->Def.Parent = NewParentHandle >= 0 ? GetFrameName(NewParentHandle) : FString();
	if (Entry->Def.bCreatedByLua && Entry->Widget.IsValid()) // r358
	{
		WowDependentLayoutCache::NoteParent(this, Handle, NewParentHandle);
		WowDependentLayoutCache::NoteAnchors(this, Handle, Entry->Def.Parent, Entry->Def.Anchors);
	}

	if (UWidget* Widget = Entry->Widget.Get())
	{
		// Virtual parents (UIParent) and frames without a canvas draw on the root canvas.
		UCanvasPanel* Target = RootCanvas.Get();
		if (NewParentHandle >= 0)
			if (UCanvasPanel* ParentCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(NewParentHandle))) Target = ParentCanvas;
		if (Target && Widget->GetParent() != Target)
		{
			Widget->RemoveFromParent();
			if (UCanvasPanelSlot* Slot = Target->AddChildToCanvas(Widget))
				Slot->SetZOrder(static_cast<int32>(Entry->Def.Strata) * 1000 + Entry->Def.FrameLevel);
		}
		// Anchors are relative to the new parent now.
		InvalidateFrameRectsRecursive(Handle);
		ApplyAnchors(Widget, Entry->Def);
		Widget->SetVisibility(!Entry->Def.bHidden && !IsUnplacedFrame(Entry->Def)
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	else
	{
		InvalidateFrameRectsRecursive(Handle);
	}
}

void FWowFrameManager::SetFrameBackdropColor(int64 Handle, const FLinearColor& Color, bool bBorder)
{
	if (const TWeakObjectPtr<UWowBackdropWidget>* Found = BackdropWidgets.Find(Handle); Found && Found->IsValid())
	{
		if (bBorder) (*Found)->SetBorderColor(Color);
		// UIDropDownMenu's own SetBackdropColor turned the near-opaque menu into the half-see-through
		// tooltip colour: the stat text underneath showed through the Melee/Ranged menu (Stefan 10.10).
		else if (TooltipBackdrops.Contains(Handle)) (*Found)->SetBackgroundColor(GetFrameName(Handle).StartsWith(TEXT("DropDownList"))
			? WowColor::FromWow(0.02f, 0.02f, 0.03f, 0.98f) : FWowWindowStyle::TooltipBackgroundColor());
		else (*Found)->SetBackgroundColor(Color);
	}
}

// ── Widget Creation ──────────────────────────────────────────────────────────

UWidget* FWowFrameManager::CreateWidgetForFrame(const FWowFrameDef& Def, int64 Handle, int64 ParentHandle)
{
	UCanvasPanel* Canvas = RootCanvas.Get();
	if (!Canvas)
	{
		UE_LOG(LogWowFrame, Error, TEXT("No root canvas for frame creation"));
		return nullptr;
	}

	bool bForceHidden = false; // Set to true for orphaned frames whose parent doesn't exist

	// Determine parent canvas: use parent frame's canvas if it exists, otherwise root canvas
	UCanvasPanel* ParentCanvas = Canvas; // Default to root canvas

	if (ParentHandle != -1)
	{
		UWidget* ParentWidget = GetWidgetForHandle(ParentHandle);
		if (ParentWidget)
		{
			if (UCanvasPanel* ParentCanvasPanel = Cast<UCanvasPanel>(ParentWidget))
			{
				ParentCanvas = ParentCanvasPanel;
				UE_LOG(LogWowFrame, Verbose, TEXT("  %s: Using parent canvas from frame %lld"), *Def.Name, ParentHandle);
			}
			else
			{
				UE_LOG(LogWowFrame, Warning, TEXT("  %s: Parent frame %lld is not a canvas, using root canvas"), *Def.Name, ParentHandle);
			}
		}
		else
		{
			// Parent frame exists but has no widget (like UIParent or other virtual frames)
			// Use root canvas
			const FFrameEntry* ParentEntry = Frames.Find(ParentHandle);
			if (ParentEntry)
			{
				UE_LOG(LogWowFrame, Verbose, TEXT("  %s: Parent frame %lld (%s) is virtual, using root canvas"),
					*Def.Name, ParentHandle, *ParentEntry->Def.Name);
			}
		}
	}
	else if (!Def.Parent.IsEmpty())
	{
		UE_LOG(LogWowFrame, Warning, TEXT("  %s: Parent '%s' not found, using root canvas (hidden)"), *Def.Name, *Def.Parent);
		// Force-hide orphaned frames — they belong inside a container that doesn't exist yet
		bForceHidden = true;
	}

	UE_LOG(LogWowFrame, Log, TEXT("CreateFrame: %s type=%d canvas=%p parent=%p hidden=%d"),
		*Def.Name, (int32)Def.Type, Canvas, ParentCanvas, Def.bHidden);

	UObject* Outer = ParentCanvas->GetOuter();
	if (!Outer) Outer = GetTransientPackage();

	UWidget* Widget = nullptr;

	// WorldFrame is the 3D world viewport — skip widget creation (it's handled by UE5's viewport)
	if (Def.Type == EWowFrameType::WorldFrame)
	{
		UE_LOG(LogWowFrame, Log, TEXT("  %s: WorldFrame type — skipping widget (3D viewport)"), *Def.Name);
		return nullptr;
	}

	// ALL frame types use UCanvasPanel so they can contain children (WoW allows children on any frame type).
	// Button/EditBox/StatusBar/Slider behavior is handled through Lua scripts and event routing,
	// not UMG widget types. Using non-canvas widgets (UButton etc.) breaks the parent-child hierarchy
	// because UButton can't contain arbitrary positioned children.
	{
		UCanvasPanel* Container = NewObject<UCanvasPanel>(Outer);
		// WoW frames do NOT clip their layer content by default.
		// Layer textures (portraits, borders, decorations) regularly extend
		// beyond the frame bounds. Only clip if XML explicitly sets clipsChildren.
		// ScrollFrames are the exception - they must clip to provide scrolling behavior.
		if (Def.Type == EWowFrameType::ScrollFrame)
		{
			Container->SetClipping(EWidgetClipping::ClipToBounds);
		}
		else
		{
			Container->SetClipping(EWidgetClipping::Inherit);
		}
		Widget = Container;
	}

	if (!Widget)
	{
		return nullptr;
	}

	UCanvasPanel* ContainerWidget = Cast<UCanvasPanel>(Widget);
	if (ContainerWidget)
	{
		auto AddFillChild = [this, ContainerWidget](UWidget* Child, int32 ZOrder, ESlateVisibility Visibility)
		{
			if (!Child) return;
			Child->SetVisibility(Visibility);
			if (UCanvasPanelSlot* ChildSlot = ContainerWidget->AddChildToCanvas(Child))
			{
				ChildSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
				ChildSlot->SetOffsets(FMargin(0.f));
				ChildSlot->SetZOrder(ZOrder);
			}
		};

		if (Def.Type == EWowFrameType::Button || Def.Type == EWowFrameType::CheckButton)
		{
			UButton* Button = NewObject<UButton>(ContainerWidget);
			// Make button transparent — WoW buttons have no default background.
			// Visual appearance comes from NormalTexture/PushedTexture/HighlightTexture.
			FButtonStyle TransparentStyle;
			FSlateBrush EmptyBrush;
			EmptyBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
			TransparentStyle.SetNormal(EmptyBrush);
			TransparentStyle.SetHovered(EmptyBrush);
			TransparentStyle.SetPressed(EmptyBrush);
			TransparentStyle.SetDisabled(EmptyBrush);
			TransparentStyle.SetNormalPadding(FMargin(0));
			TransparentStyle.SetPressedPadding(FMargin(0));

			// XML button-state textures are properties of the button rather than
			// ordinary layer elements.  Initialise the Slate style directly so
			// inherited micro-menu buttons render even before their Lua OnLoad runs.
			auto LoadButtonBrush = [this](const FString& TexturePath, const FVector4& UV, FSlateBrush& Brush) -> bool
			{
				if (TexturePath.IsEmpty()) return false;
				if (UTexture2D* Texture = LoadUITexture(TexturePath))
				{
					Brush.SetResourceObject(Texture);
					Brush.DrawAs = ESlateBrushDrawType::Image;
                    Brush.SetUVRegion(FBox2D(FVector2D(FMath::Min(UV.X, UV.Y), FMath::Min(UV.Z, UV.W)),
                        FVector2D(FMath::Max(UV.X, UV.Y), FMath::Max(UV.Z, UV.W))));
                    Brush.Mirroring = UV.X > UV.Y
                        ? (UV.Z > UV.W ? ESlateBrushMirrorType::Both : ESlateBrushMirrorType::Horizontal)
                        : (UV.Z > UV.W ? ESlateBrushMirrorType::Vertical : ESlateBrushMirrorType::NoMirror);
					return true;
				}
				return false;
			};
			FString NormalTexturePath = Def.NormalTexture;
			if (Def.Name.Contains(TEXT("ChatFrame")))
			{
				if (Def.Name.EndsWith(TEXT("ButtonFrameUpButton")))
				{
					NormalTexturePath = TEXT("Interface\\ChatFrame\\UI-ChatIcon-ScrollUp-Up");
				}
				else if (Def.Name.EndsWith(TEXT("ButtonFrameDownButton")))
				{
					NormalTexturePath = TEXT("Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Up");
				}
				else if (Def.Name.EndsWith(TEXT("ButtonFrameBottomButton")))
				{
					NormalTexturePath = TEXT("Interface\\ChatFrame\\UI-ChatIcon-ScrollEnd-Up");
				}
				if (Def.Name.EndsWith(TEXT("ButtonFrameUpButton")) ||
					Def.Name.EndsWith(TEXT("ButtonFrameDownButton")) ||
					Def.Name.EndsWith(TEXT("ButtonFrameBottomButton")))
				{
					UE_LOG(LogWowFrame, Log, TEXT("Chat scroll texture: %s -> %s"),
						*Def.Name, *NormalTexturePath);
				}
			}
			if (NormalTexturePath.IsEmpty() && Def.Name.EndsWith(TEXT("MicroButton")))
			{
				static const TMap<FString, FString> MicroButtonFallbacks = {
					// r297 (Stefan 13.10: the character button showed only the portrait, no button):
					// the client's file is UI-MicroButtonCharacter-Up (CharacterMicroButton_OnLoad);
					// "UI-MicroButton-Character-Up" does not exist, so it had no art.
					{TEXT("CharacterMicroButton"), TEXT("Interface\\Buttons\\UI-MicroButtonCharacter-Up")},
					{TEXT("SpellbookMicroButton"), TEXT("Interface\\Buttons\\UI-MicroButton-Spellbook-Up")},
					{TEXT("TalentMicroButton"), TEXT("Interface\\Buttons\\UI-MicroButton-Talents-Up")},
					{TEXT("AchievementMicroButton"), TEXT("Interface\\Buttons\\UI-MicroButton-Achievement-Up")},
					{TEXT("QuestLogMicroButton"), TEXT("Interface\\Buttons\\UI-MicroButton-Quest-Up")},
					{TEXT("SocialsMicroButton"), TEXT("Interface\\Buttons\\UI-MicroButton-Socials-Up")},
					{TEXT("LFDMicroButton"), TEXT("Interface\\Buttons\\UI-MicroButton-LFG-Up")},
					{TEXT("MainMenuMicroButton"), TEXT("Interface\\Buttons\\UI-MicroButton-MainMenu-Up")},
					{TEXT("HelpMicroButton"), TEXT("Interface\\Buttons\\UI-MicroButton-Help-Up")},
				};
				if (const FString* Fallback = MicroButtonFallbacks.Find(Def.Name))
				{
					NormalTexturePath = *Fallback;
				}
			}
			if (Def.Name.EndsWith(TEXT("MicroButton")))
			{
				UE_LOG(LogWowFrame, Log, TEXT("Micro-menu texture: %s XML='%s' resolved='%s'"),
					*Def.Name, *Def.NormalTexture, *NormalTexturePath);
			}

			FSlateBrush NormalBrush;
			const bool bHasNormalTexture = LoadButtonBrush(NormalTexturePath, Def.NormalTextureUV, NormalBrush);
			if (bHasNormalTexture)
			{
				TransparentStyle.SetNormal(NormalBrush);
				TransparentStyle.SetHovered(NormalBrush);
				TransparentStyle.SetPressed(NormalBrush);
				TransparentStyle.SetDisabled(NormalBrush);
			}
			FSlateBrush PushedBrush;
			if (LoadButtonBrush(Def.PushedTexture, Def.PushedTextureUV, PushedBrush))
			{
				TransparentStyle.SetPressed(PushedBrush);
			}
			FSlateBrush HighlightBrush;
			if (LoadButtonBrush(Def.HighlightTexture, Def.HighlightTextureUV, HighlightBrush))
			{
				TransparentStyle.SetHovered(HighlightBrush);
			}
			FSlateBrush DisabledBrush;
			if (LoadButtonBrush(Def.DisabledTexture, Def.DisabledTextureUV, DisabledBrush))
			{
				TransparentStyle.SetDisabled(DisabledBrush);
			}
			if (Def.Type == EWowFrameType::CheckButton)
			{
				// Check mark overlay above the button art, shown while checked.
				// r370 (Stefan 16.10: the chosen spellbook tab showed a black square instead of its icon):
				// highlight-style checked art (CheckButtonHilight - alphaMode="ADD" in the XML, a glow on
				// black) is drawn additively like the client; blended, its black covered the icon. Check
				// marks (UI-CheckBox-Check) keep normal blending.
				const bool bAdditiveCheck = Def.CheckedTexture.Contains(TEXT("Hilight")) || Def.CheckedTexture.Contains(TEXT("Highlight"))
					|| Def.CheckedTexture.Contains(TEXT("Glow"));
				UWowBlendImage* Check = NewObject<UWowBlendImage>(ContainerWidget);
				Check->SetAdditive(bAdditiveCheck);
				FSlateBrush CheckBrush;
				if (!LoadButtonBrush(Def.CheckedTexture, Def.CheckedTextureUV, CheckBrush))
				{
					CheckBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
				}
				Check->SetBrush(CheckBrush);
				AddFillChild(Check, -50, ESlateVisibility::Collapsed);
				CheckedTextureWidgets.Add(Handle, Check);
			}
			Button->SetStyle(TransparentStyle);
			// BackgroundColor tints every state brush.  Transparent is correct for
			// textureless hit areas, but would erase a loaded micro-menu image.
			Button->SetBackgroundColor(bHasNormalTexture ? FLinearColor::White : FLinearColor::Transparent);
			// HitTestInvisible — clicks are routed via our custom HitTestFrames(),
			// not UMG's Slate hit-testing.
			AddFillChild(Button, -100, ESlateVisibility::HitTestInvisible);
			// r379 (Stefan 16.10: skill headers and "All" had a stretched red bar behind them): a
			// NormalTexture with its own size smaller than the button (the +/- toggle of SkillFrame,
			// QuestLog and the collapse-all button: 16 x 16 at LEFT 3,0) draws at that size and anchor,
			// not stretched over the button. The button's state brushes share that rect.
			if (Def.NormalTextureW > 0.f && Def.NormalTextureH > 0.f && Def.Width > 0.f && Def.Height > 0.f
				&& (Def.NormalTextureW < Def.Width - .5f || Def.NormalTextureH < Def.Height - .5f))
				if (UCanvasPanelSlot* ArtSlot = Cast<UCanvasPanelSlot>(Button->Slot))
				{
					const FVector2D Self = GetAnchorPointOffset(Def.NormalTexturePoint, 1.f, 1.f);
					const FVector2D Rel = GetAnchorPointOffset(Def.NormalTextureRelativePoint, 1.f, 1.f);
					ArtSlot->SetAnchors(FAnchors(Rel.X, Rel.Y));
					ArtSlot->SetAlignment(Self);
					ArtSlot->SetOffsets(FMargin(Def.NormalTextureOffset.X * UIScale, -Def.NormalTextureOffset.Y * UIScale,
						Def.NormalTextureW * UIScale, Def.NormalTextureH * UIScale));
				}
			ButtonWidgets.Add(Handle, Button);

			if (!Def.ButtonText.IsEmpty() || Def.Name == TEXT("GameTimeFrame"))
			{
				UTextBlock* ButtonText = NewObject<UTextBlock>(ContainerWidget);
				ButtonText->SetText(FText::FromString(ResolveXmlText(Def.ButtonText)));
				ButtonText->SetJustification(ETextJustify::Center);
				FSlateFontInfo FontInfo = ButtonText->GetFont();
				FontInfo.Size = FMath::RoundToInt(12.f * UIScale * 0.75f); // r270: points, see the fontstrings
				// r262 (Stefan 12.10: the dressing room's Reset / Close had a plain white sans font, not
				// WotLK's): button captions use GameFontNormal (Friz Quadrata, gold) like
				// UIPanelButtonTemplate's NormalFont, instead of the engine default font.
				if (FontManager && FontManager->IsInitialized())
					FontInfo = FontManager->GetFont(TEXT("GameFontNormal"), FMath::RoundToInt(12.f * UIScale * 0.75f));
				ButtonText->SetFont(FontInfo);
				ButtonText->SetColorAndOpacity(FSlateColor(WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f)));
				ButtonText->SetShadowOffset(FVector2D(1.0f, 1.0f));
				ButtonText->SetShadowColorAndOpacity(FLinearColor::Black);
				AddFillChild(ButtonText, 250, ESlateVisibility::HitTestInvisible);
				// r392 (Stefan 16.10: Okay/Cancel in Bartender's key binding dialog had their text too
				// high): a <ButtonText> without anchors is centred on the button in the client; the
				// filling text block drew its line at the button's top edge. Full width, centred in
				// the height at the text's own height. (Captions placed by code later keep their place.)
				if (UCanvasPanelSlot* CaptionSlot = Cast<UCanvasPanelSlot>(ButtonText->Slot))
				{
					CaptionSlot->SetAnchors(FAnchors(0.f, .5f, 1.f, .5f));
					CaptionSlot->SetAlignment(FVector2D(0.f, .5f));
					CaptionSlot->SetOffsets(FMargin(0.f));
					CaptionSlot->SetAutoSize(true);
				}
				PrimaryTextWidgets.Add(Handle, ButtonText);

				if (!Def.Name.IsEmpty())
				{
					const FString ButtonTextName = Def.Name + TEXT("Text");
					NamedFontStringWidgets.Add(ButtonTextName, ButtonText);
					if (EventSystem)
					{
						EventSystem->CreateTextureRegionGlobal(ButtonTextName, Handle, TEXT("FontString"));
					}
				}
			}
		}
		else if (Def.Type == EWowFrameType::EditBox)
		{
			UEditableTextBox* EditBox = NewObject<UEditableTextBox>(ContainerWidget);
			// Make edit box background transparent — WoW edit boxes use XML textures for appearance
			FEditableTextBoxStyle BoxStyle = EditBox->GetWidgetStyle();
			FSlateBrush EmptyBrush;
			EmptyBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
			BoxStyle.SetBackgroundImageNormal(EmptyBrush);
			BoxStyle.SetBackgroundImageHovered(EmptyBrush);
			BoxStyle.SetBackgroundImageFocused(EmptyBrush);
			BoxStyle.SetBackgroundImageReadOnly(EmptyBrush);
			// Chat boxes: equal top/bottom padding centres the line in the 24-unit strip so the
			// text shares the "Say:" header's baseline; the left inset after the header is the
			// slot offset set by LayoutChatEditHeaders (never restyle a live SEditableTextBox -
			// that left it with a dangling style and crashed on Enter).
			float ChatVertical = 3.0f * UIScale;
			if (Def.Name.StartsWith(TEXT("ChatFrame")) && FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
			{
				FSlateFontInfo InputFont = BoxStyle.TextStyle.Font;
				InputFont.Size = FMath::RoundToInt(10.0f * UIScale);
				const float LineHeight = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->GetMaxCharacterHeight(InputFont);
				ChatVertical = FMath::Max(0.0f, (24.0f * UIScale - LineHeight) * 0.5f);
			}
			BoxStyle.SetPadding(Def.Name.StartsWith(TEXT("ChatFrame"))
				? FMargin(2.0f * UIScale, ChatVertical, 2.0f * UIScale, ChatVertical)
				: FMargin(2.0f));
			BoxStyle.SetForegroundColor(FSlateColor(FLinearColor::White));
			BoxStyle.SetFocusedForegroundColor(FSlateColor(FLinearColor::White));
			if (Def.Name.StartsWith(TEXT("ChatFrame")))
			{
				FSlateFontInfo InputFont = BoxStyle.TextStyle.Font;
				InputFont.Size = FMath::RoundToInt(10.0f * UIScale);
				BoxStyle.SetFont(InputFont);
			}
			EditBox->SetWidgetStyle(BoxStyle);
			AddFillChild(EditBox, 50, ESlateVisibility::Visible);
			EditBoxWidgets.Add(Handle, EditBox);
		}
		else if (Def.Type == EWowFrameType::Slider)
		{
			USlider* Slider = NewObject<USlider>(ContainerWidget);
			if (Def.Orientation.Equals(TEXT("VERTICAL"), ESearchCase::IgnoreCase))
			{
				Slider->SetOrientation(EOrientation::Orient_Vertical);
			}
			// Use transparent bar — WoW sliders use XML textures
			Slider->SetSliderBarColor(FLinearColor::Transparent);
			AddFillChild(Slider, 50, ESlateVisibility::Visible);
			SliderWidgets.Add(Handle, Slider);
		}
	}

	// Walk the full parent chain to check if ANY ancestor is hidden
	bool bParentHidden = false;
	{
		int64 CheckHandle = ParentHandle;
		int32 MaxDepth = 20; // prevent infinite loops
		while (CheckHandle >= 0 && MaxDepth-- > 0)
		{
			const FFrameEntry* AncestorEntry = Frames.Find(CheckHandle);
			if (!AncestorEntry) break;
			if (AncestorEntry->Def.bHidden || (AncestorEntry->Widget.IsValid() &&
				AncestorEntry->Widget->GetVisibility() == ESlateVisibility::Collapsed))
			{
				bParentHidden = true;
				break;
			}
			CheckHandle = AncestorEntry->ParentHandle;
		}
	}

	// Frames that WoW's Lua hides at startup but aren't marked hidden="true" in XML.
	// These are typically voice chat, quest info sub-frames, and other panels that
	// are shown conditionally by scripts we haven't fully implemented.
	static const TSet<FString> LuaHiddenFrames = {
		TEXT("LoopbackVUMeter"),
		TEXT("BasicScriptErrors"),
		TEXT("BasicScriptErrorsButton"),
		TEXT("QuestInfoRequiredMoneyFrame"),
		TEXT("QuestInfoRequiredMoneyDisplay"),
		// r367 (Stefan 16.10: no objective tracker): WatchFrame is no longer listed. It is shown in
		// the XML (hidden="false") and the stock WatchFrame.lua never calls Show on it, so hiding it
		// here kept the tracker invisible since it replaced the native one (r339).
	};
	bool bLuaHidden = LuaHiddenFrames.Contains(Def.Name);

	// Frames that WoW's Lua SHOWS at startup (via OnLoad scripts) but our FrameXML
	// scripts fail to execute. Force these visible so the UI is usable.
	// ActionButton1-12, their icons, and micro buttons are key examples.
	bool bForceVisible = false;
	if (Def.bHidden && !bForceHidden && !bParentHidden)
	{
		// ActionButton1-12, MultiBarBottomLeftButton1-12, etc.
		if (Def.Name.StartsWith(TEXT("ActionButton")) ||
			Def.Name.StartsWith(TEXT("MultiBarBottomLeftButton")) ||
			Def.Name.StartsWith(TEXT("MultiBarBottomRightButton")) ||
			Def.Name.StartsWith(TEXT("MultiBarRightButton")) ||
			Def.Name.StartsWith(TEXT("MultiBarLeftButton")))
		{
			bForceVisible = true;
		}
	}

	if ((Def.bHidden && !bForceVisible) || bForceHidden || bParentHidden || bLuaHidden || IsUnplacedFrame(Def))
	{
		Widget->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		// All frame containers use SelfHitTestInvisible — clicks pass through to the
		// 3D world and are routed to Lua via our custom HitTestFrames() in the
		// PlayerController.  EditBox and Slider children that need UMG interaction
		// set their own visibility to Visible.
		Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	// Add to parent canvas (may be root canvas or a parent frame's canvas)
	UCanvasPanelSlot* Slot = ParentCanvas->AddChildToCanvas(Widget);

	// Apply strata z-ordering (1000 per strata level + frame level offset)
	if (Slot)
	{
		int32 ZOrder = static_cast<int32>(Def.Strata) * 1000 + Def.FrameLevel;
		Slot->SetZOrder(ZOrder);
		UE_LOG(LogWowFrame, Verbose, TEXT("  Added to canvas: slot=%p ZOrder=%d"), Slot, ZOrder);
	}
	else
	{
		UE_LOG(LogWowFrame, Error, TEXT("  Failed to add widget to canvas!"));
	}

	// Apply anchor positioning
	ApplyAnchors(Widget, Def);

	// Ensure frame has minimum size if not auto-sizing
	if (Slot && Def.Width > 0.f && Def.Height > 0.f)
	{
		// Double-check that size was set (in case ApplyAnchors didn't handle it)
		FVector2D CurrentSize = Slot->GetSize();
		if (CurrentSize.X == 0.f && CurrentSize.Y == 0.f)
		{
			Slot->SetSize(FVector2D(Def.Width * UIScale, Def.Height * UIScale));
			UE_LOG(LogWowFrame, Warning, TEXT("  %s: Fallback size set to %.1fx%.1f (scaled)"), *Def.Name, Def.Width * UIScale, Def.Height * UIScale);
		}
	}

	// Create UProgressBar child for StatusBar frames
	if (Def.Type == EWowFrameType::StatusBar)
	{
		UProgressBar* Bar = NewObject<UProgressBar>(Widget);
		// Transparent style by default — Lua will set fill color via :SetStatusBarColor()
		// Remove the default UE grey background/border
		FSlateBrush EmptyBrush;
		EmptyBrush.DrawAs = ESlateBrushDrawType::NoDrawType;

		// UProgressBar cannot render a plain FSlateBrush whose resource is null.
		// Use UE's built-in white texel as the drawable surface, then tint it via
		// SetStatusBarColor/SetFillColorAndOpacity.  This keeps XML status bars
		// texture-independent while still producing a visible fill.
		FSlateBrush FillBrush;
		FillBrush.DrawAs = ESlateBrushDrawType::Image;
		FillBrush.ImageSize = FVector2D(1.0f, 1.0f);
		FillBrush.TintColor = FSlateColor(FLinearColor::White);
		FillBrush.SetResourceObject(LoadObject<UTexture2D>(
			nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")));

		FProgressBarStyle BarStyle;
		BarStyle.SetBackgroundImage(EmptyBrush);
		// Use the FrameXML texture so unit bars retain the stock bevel/shading.
		if (!Def.BarTexture.IsEmpty())
		{
			if (UTexture2D* Texture = LoadUITexture(Def.BarTexture))
			{
				FillBrush.SetResourceObject(Texture);
				FillBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			}
		}
		BarStyle.SetFillImage(FillBrush);
		Bar->SetWidgetStyle(BarStyle);
		Bar->SetBarFillType(EProgressBarFillType::LeftToRight);
		Bar->SetBarFillStyle(EProgressBarFillStyle::Scale);
		Bar->SetBorderPadding(FVector2D::ZeroVector);
		Bar->SetIsMarquee(false);
		Bar->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Bar->SetFillColorAndOpacity(FLinearColor::Green); // default, Lua will override
		Bar->SetPercent(0.0f);
		UCanvasPanel* CanvasPanel = Cast<UCanvasPanel>(Widget);
		UCanvasPanelSlot* BarSlot = CanvasPanel ? CanvasPanel->AddChildToCanvas(Bar) : nullptr;
		if (BarSlot)
		{
			BarSlot->SetAnchors(FAnchors(0, 0));
			BarSlot->SetPosition(FVector2D(0, 0));
			BarSlot->SetSize(FVector2D(Def.Width * UIScale, Def.Height * UIScale));
			BarSlot->SetZOrder(1);
		}
		StatusBarWidgets.Add(Handle, Bar);
	}

	// Create backdrop (9-slice background) if specified
	if (UCanvasPanel* BackdropCanvas = Cast<UCanvasPanel>(Widget); Def.Backdrop.IsSet() && BackdropCanvas)
	{
		CreateBackdrop(BackdropCanvas, Def.Backdrop.GetValue(), Handle);
	}

	// Create layer content (textures and fontstrings) inside the frame
	if (Def.Layers.Num() > 0)
	{
		UCanvasPanel* Container = Cast<UCanvasPanel>(Widget);
		// Only create layers for canvas-type frames; skip for buttons/statusbars/etc.
		// to avoid dumping textures onto the root canvas where they'd fill the screen
		if (Container)
		{
			CreateLayerContent(Container, Def, Handle);
		}
	}

	return Widget;
}

// ── $parent Resolution ───────────────────────────────────────────────────────

void FWowFrameManager::ResolveParentReferences(FWowFrameDef& Def, const FString& ParentName)
{
	// Resolve $parent in the frame name itself
	if (Def.Name.StartsWith(TEXT("$parent")) && !ParentName.IsEmpty())
	{
		FString ResolvedName = Def.Name;
		ResolvedName.ReplaceInline(TEXT("$parent"), *ParentName);
		Def.Name = ResolvedName;

		UE_LOG(LogWowFrame, Verbose, TEXT("Resolved frame name: $parent -> %s"), *Def.Name);
	}

	// Resolve $parent in anchor RelativeTo fields
	for (FWowAnchor& Anchor : Def.Anchors)
	{
		if (Anchor.RelativeTo.StartsWith(TEXT("$parent")) && !ParentName.IsEmpty())
		{
			FString ResolvedRelativeTo = Anchor.RelativeTo;
			ResolvedRelativeTo.ReplaceInline(TEXT("$parent"), *ParentName);
			Anchor.RelativeTo = ResolvedRelativeTo;

			UE_LOG(LogWowFrame, Verbose, TEXT("Resolved anchor RelativeTo: $parent -> %s"), *Anchor.RelativeTo);
		}
	}

	// Resolve $parent in layer content (textures and fontstrings)
	// $parent in layer content refers to the FRAME itself. For anonymous frames
	// (no name), fall back to using ParentName (the frame's parent).
	const FString& FrameName = Def.Name.IsEmpty() ? ParentName : Def.Name;
	for (FWowLayer& Layer : Def.Layers)
	{
		// Resolve in texture elements
		for (FWowTextureElement& Texture : Layer.Textures)
		{
			// Resolve texture name: $parentBorder → ActionButton1Border
			if (Texture.Name.Contains(TEXT("$parent")) && !FrameName.IsEmpty())
			{
				Texture.Name.ReplaceInline(TEXT("$parent"), *FrameName);
			}

			// Resolve texture anchor RelativeTo fields
			for (FWowAnchor& TexAnchor : Texture.Anchors)
			{
				if (TexAnchor.RelativeTo.Contains(TEXT("$parent")) && !FrameName.IsEmpty())
				{
					TexAnchor.RelativeTo.ReplaceInline(TEXT("$parent"), *FrameName);
				}
			}
		}

		// Resolve in fontstring elements
		for (FWowFontStringElement& FontString : Layer.FontStrings)
		{
			// Resolve fontstring name: $parentName → PlayerFrameName
			if (FontString.Name.Contains(TEXT("$parent")) && !FrameName.IsEmpty())
			{
				FontString.Name.ReplaceInline(TEXT("$parent"), *FrameName);
			}

			// Resolve fontstring anchor RelativeTo fields
			for (FWowAnchor& FSAnchor : FontString.Anchors)
			{
				if (FSAnchor.RelativeTo.Contains(TEXT("$parent")) && !FrameName.IsEmpty())
				{
					FSAnchor.RelativeTo.ReplaceInline(TEXT("$parent"), *FrameName);
				}
			}
		}
	}

	// Resolve $parent in button texture names (NormalTexture, PushedTexture, etc.)
	if (!FrameName.IsEmpty())
	{
		if (Def.NormalTextureName.Contains(TEXT("$parent")))
			Def.NormalTextureName.ReplaceInline(TEXT("$parent"), *FrameName);
		if (Def.PushedTextureName.Contains(TEXT("$parent")))
			Def.PushedTextureName.ReplaceInline(TEXT("$parent"), *FrameName);
		if (Def.HighlightTextureName.Contains(TEXT("$parent")))
			Def.HighlightTextureName.ReplaceInline(TEXT("$parent"), *FrameName);
		if (Def.DisabledTextureName.Contains(TEXT("$parent")))
			Def.DisabledTextureName.ReplaceInline(TEXT("$parent"), *FrameName);
	}

	for (FString& NamedObject : Def.NamedObjectGlobals)
	{
		if (NamedObject.Contains(TEXT("$parent")) && !FrameName.IsEmpty())
		{
			NamedObject.ReplaceInline(TEXT("$parent"), *FrameName);
		}
	}

	// Recursively resolve $parent in child frames.
	// For anonymous frames (empty name), pass the effective name (ParentName)
	// so grandchildren can still resolve $parent.
	const FString& EffectiveName = Def.Name.IsEmpty() ? ParentName : Def.Name;
	for (FWowFrameDef& Child : Def.Children)
	{
		ResolveParentReferences(Child, EffectiveName);
	}
}

// ── Frame Creation ───────────────────────────────────────────────────────────

int64 FWowFrameManager::CreateFrame(const FWowFrameDef& Def)
{
	return CreateFrameInternal(Def, -1);
}

int64 FWowFrameManager::CreateFrameInternal(const FWowFrameDef& Def, int64 ExplicitParentHandle)
{
	// Resolve template inheritance first
	FWowFrameDef Resolved = ResolveInherits(Def);
	if (Resolved.Name == TEXT("TargetFrameSpellBar") ||
		Resolved.Name == TEXT("FocusFrameSpellBar"))
	{
		Resolved.Type = EWowFrameType::StatusBar;
	}

	// Resolve $parent references in frame names and anchors.
	// SKIP for virtual frames — they are templates whose children should keep
	// $parent unresolved so it gets resolved during instantiation.
	if (!Resolved.bVirtual)
	{
		// Always resolve, even if Parent is empty — layer content ($parent in
		// texture/fontstring names) uses the frame's OWN name, not the parent.
		FString ParentForResolution = Resolved.Parent.IsEmpty() ? Resolved.Name : Resolved.Parent;
		ResolveParentReferences(Resolved, ParentForResolution);
	}

	int64 Handle = NextHandle++;
	if (InheritsSecureTemplate(Def.Inherits)) ProtectedFrames.Add(Handle);
	FFrameEntry Entry;
	Entry.Def = Resolved;
	Entry.bMouseEnabled = Resolved.bEnableMouse ||
		Resolved.Type == EWowFrameType::Button ||
		Resolved.Type == EWowFrameType::CheckButton ||
		Resolved.Type == EWowFrameType::EditBox ||
		Resolved.Type == EWowFrameType::Slider;
	Entry.bKeyboardEnabled = Resolved.bEnableKeyboard ||
		Resolved.Type == EWowFrameType::EditBox;
	Entry.bMouseWheelEnabled = false;

	// Resolve parent handle
	if (ExplicitParentHandle >= 0)
	{
		Entry.ParentHandle = ExplicitParentHandle;
	}
	else if (!Resolved.Parent.IsEmpty())
	{
		Entry.ParentHandle = FindFrame(Resolved.Parent);
		if (Entry.ParentHandle == -1)
		{
			UE_LOG(LogWowFrame, Warning, TEXT("Parent frame '%s' not found for frame '%s'"), *Resolved.Parent, *Resolved.Name);
		}
	}

	// Handle virtual frames (like templates and UIParent)
	if (Resolved.bVirtual)
	{
		// Virtual frames don't get widgets but are stored for reference/inheritance
		Entry.Widget = nullptr;
		Frames.Add(Handle, MoveTemp(Entry));

		if (!Resolved.Name.IsEmpty())
		{
			NameToHandle.Add(Resolved.Name, Handle);
			RegisterTemplate(Resolved.Name, Resolved);
		}

		UE_LOG(LogWowFrame, Log, TEXT("Created virtual frame [%lld] %s"), Handle, *Resolved.Name);
		return Handle;
	}

	// Create the widget for non-virtual frames
	Entry.Widget = CreateWidgetForFrame(Resolved, Handle, Entry.ParentHandle);
	if (Resolved.bCreatedByLua && Entry.Widget.IsValid()) // r358
	{
		WowDependentLayoutCache::NoteParent(this, Handle, Entry.ParentHandle);
		WowDependentLayoutCache::NoteAnchors(this, Handle, Resolved.Parent, Resolved.Anchors);
	}

	Frames.Add(Handle, MoveTemp(Entry));

	if (!Resolved.Name.IsEmpty())
	{
		NameToHandle.Add(Resolved.Name, Handle);
	}

	// Register frame as Lua global FIRST (so children and scripts can find it)
	if (EventSystem && !Resolved.Name.IsEmpty())
	{
		EventSystem->CreateFrameObject(Handle, Resolved.Name, Resolved.FrameID);

		if (Entry.ParentHandle != -1)
		{
			const FString ParentName = GetFrameName(Entry.ParentHandle);
			const FString AliasName = BuildParentFrameAliasName(ParentName, Resolved.Name);
			if (!AliasName.IsEmpty())
			{
				EventSystem->AliasGlobalObject(Resolved.Name, AliasName);
			}
		}

		// Create Lua globals for button texture names (e.g., ActionButton1NormalTexture)
		// These are needed by GetNormalTexture() etc.
		if (!Resolved.NormalTextureName.IsEmpty())
			EventSystem->CreateTextureRegionGlobal(Resolved.NormalTextureName, Handle, TEXT("Texture"));
		if (!Resolved.PushedTextureName.IsEmpty())
			EventSystem->CreateTextureRegionGlobal(Resolved.PushedTextureName, Handle, TEXT("Texture"));
		if (!Resolved.HighlightTextureName.IsEmpty())
			EventSystem->CreateTextureRegionGlobal(Resolved.HighlightTextureName, Handle, TEXT("Texture"));
		if (!Resolved.DisabledTextureName.IsEmpty())
			EventSystem->CreateTextureRegionGlobal(Resolved.DisabledTextureName, Handle, TEXT("Texture"));

		for (const FString& NamedObject : Resolved.NamedObjectGlobals)
		{
			if (!NamedObject.IsEmpty())
			{
				EventSystem->CreateTextureRegionGlobal(NamedObject, Handle, TEXT("Animation"));
			}
		}
	}

	// Create child frames BEFORE firing OnLoad (WoW creates children first,
	// then fires parent OnLoad — scripts like ActionButton_OnLoad expect
	// _G["ActionButton1Cooldown"] to exist when OnLoad runs)
	for (const FWowFrameDef& ChildDef : Resolved.Children)
	{
		FWowFrameDef ChildWithParent = ChildDef;
		const FString LogicalParentName = Resolved.Name.IsEmpty() ? Resolved.Parent : Resolved.Name;
		if (ChildWithParent.Parent.IsEmpty() && !LogicalParentName.IsEmpty())
		{
			ChildWithParent.Parent = LogicalParentName;
		}
		CreateFrameInternal(ChildWithParent, Handle);
	}

	// NOW compile and fire OnLoad — all children exist
	// Always call CompileFrameScripts so child-field mapping and self.name are set,
	// even for frames without scripts (they still need fields for parent scripts to access)
	if (EventSystem)
	{
		EventSystem->CompileFrameScripts(Handle, Resolved);
	}

	UE_LOG(LogWowFrame, Log, TEXT("Created frame [%lld] %s (type %d, %d layers, %d children, widget=%p)"),
		Handle, *Resolved.Name, (int32)Resolved.Type, Resolved.Layers.Num(), Resolved.Children.Num(), Entry.Widget.Get());
	return Handle;
}

// ── Texture Loading ──────────────────────────────────────────────────────

// r425 (Stefan 23.10, TormentWoW's ElvUI: NormTex2, Melli, ArrowUp, Close ... "Failed to read UI
// texture"): addons ship .tga files, which the client reads like .blp. Truecolour and greyscale
// TGA, raw or RLE, 8/24/32 bit, either row order, decoded to RGBA8 (one mip).
static bool WowDecodeTga(const TArray<uint8>& Data, FBlpTexture& Out)
{
    if (Data.Num() < 18) return false;
    const uint8* H = Data.GetData();
    const int32 IdLength = H[0], ColorMapType = H[1], ImageType = H[2];
    const int32 Width = H[12] | (H[13] << 8), Height = H[14] | (H[15] << 8), Bpp = H[16];
    const bool bTopDown = (H[17] & 0x20) != 0;
    const bool bRle = ImageType == 10 || ImageType == 11;
    const bool bGrey = ImageType == 3 || ImageType == 11;
    if (ColorMapType != 0 || Width <= 0 || Height <= 0 || Width > 4096 || Height > 4096) return false;
    if (!(ImageType == 2 || ImageType == 3 || ImageType == 10 || ImageType == 11)) return false;
    if (bGrey ? Bpp != 8 : (Bpp != 24 && Bpp != 32)) return false;
    const int32 PixelBytes = Bpp / 8;
    int32 Pos = 18 + IdLength;
    TArray<uint8> Rgba;
    Rgba.SetNumZeroed(Width * Height * 4);
    auto Put = [&](int32 PixelIndex, const uint8* Src)
    {
        const int32 X = PixelIndex % Width, Row = PixelIndex / Width;
        const int32 Y = bTopDown ? Row : (Height - 1 - Row);
        uint8* D = Rgba.GetData() + (Y * Width + X) * 4;
        if (bGrey) { D[0] = D[1] = D[2] = Src[0]; D[3] = 255; }
        else { D[0] = Src[2]; D[1] = Src[1]; D[2] = Src[0]; D[3] = PixelBytes == 4 ? Src[3] : 255; }
    };
    const int32 Total = Width * Height;
    int32 Index = 0;
    while (Index < Total)
    {
        if (!bRle)
        {
            if (Pos + PixelBytes > Data.Num()) return false;
            Put(Index++, H + Pos); Pos += PixelBytes;
            continue;
        }
        if (Pos >= Data.Num()) return false;
        const uint8 Packet = H[Pos++];
        const int32 Count = (Packet & 0x7F) + 1;
        if (Packet & 0x80)
        {
            if (Pos + PixelBytes > Data.Num()) return false;
            for (int32 I = 0; I < Count && Index < Total; ++I) Put(Index++, H + Pos);
            Pos += PixelBytes;
        }
        else
        {
            for (int32 I = 0; I < Count && Index < Total; ++I)
            {
                if (Pos + PixelBytes > Data.Num()) return false;
                Put(Index++, H + Pos); Pos += PixelBytes;
            }
        }
    }
    Out = FBlpTexture();
    Out.Width = Width; Out.Height = Height;
    Out.PixelFormat = EBlpPixelFormat::RGBA8;
    FBlpMipLevel& Mip = Out.MipLevels.AddDefaulted_GetRef();
    Mip.Width = Width; Mip.Height = Height; Mip.Data = MoveTemp(Rgba);
    Out.bIsValid = true;
    return true;
}

UTexture2D* FWowFrameManager::LoadUITexture(const FString& TexturePath)
{
	if (TexturePath.IsEmpty() || !MpqManager || !AssetCache)
	{
		return nullptr;
	}

	// Check cache first
	if (TWeakObjectPtr<UTexture2D>* CachedPtr = TextureCache.Find(TexturePath))
	{
		if (CachedPtr->IsValid())
		{
			return CachedPtr->Get();
		}
		else
		{
			// Remove stale entry
			TextureCache.Remove(TexturePath);
		}
	}

	// Check asset cache
	UTexture2D* ExistingTexture = AssetCache->FindTexture(TexturePath);
	if (ExistingTexture)
	{
		TextureCache.Add(TexturePath, ExistingTexture);
		return ExistingTexture;
	}

	// Convert interface path to BLP file path
	FString BlpPath = TexturePath;
	// Replace known extensions with .blp
	if (BlpPath.EndsWith(TEXT(".tga"), ESearchCase::IgnoreCase))
	{
		BlpPath = BlpPath.Left(BlpPath.Len() - 4) + TEXT(".blp");
	}
	else if (BlpPath.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
	{
		BlpPath = BlpPath.Left(BlpPath.Len() - 4) + TEXT(".blp");
	}
	else if (!BlpPath.EndsWith(TEXT(".blp"), ESearchCase::IgnoreCase))
	{
		BlpPath += TEXT(".blp");
	}

	// Read BLP file from MPQ
	TArray<uint8> BlpData;
	FBlpTexture BlpTexture;
	if (!MpqManager->ReadFile(BlpPath, BlpData))
	{
		// r425: a loose .tga (addon media) instead of the .blp.
		const FString TgaPath = BlpPath.Left(BlpPath.Len() - 4) + TEXT(".tga");
		TArray<uint8> TgaData;
		if (!MpqManager->ReadFile(TgaPath, TgaData) || !WowDecodeTga(TgaData, BlpTexture))
		{
			UE_LOG(LogWowFrame, Warning, TEXT("Failed to read UI texture: %s"), *BlpPath);
			return nullptr;
		}
	}
	else
	{
		// Parse BLP
		BlpTexture = FBlpParser::Parse(BlpData);
	}
	if (!BlpTexture.bIsValid)
	{
		UE_LOG(LogWowFrame, Warning, TEXT("Failed to parse UI texture: %s"), *BlpPath);
		return nullptr;
	}

	// Create UTexture2D
	UTexture2D* Texture = FWowTextureFactory::CreateTexture(BlpTexture, TexturePath);
	if (!Texture)
	{
		UE_LOG(LogWowFrame, Warning, TEXT("Failed to create UI texture: %s"), *BlpPath);
		return nullptr;
	}

	// Cache the texture
	AssetCache->CacheTexture(TexturePath, Texture);
	TextureCache.Add(TexturePath, Texture);

	UE_LOG(LogWowFrame, Verbose, TEXT("Loaded UI texture: %s (%dx%d)"),
		*TexturePath, Texture->GetSizeX(), Texture->GetSizeY());

	return Texture;
}

void FWowFrameManager::UpdateCharacterSheet(const FWowPlayerEntity& Player, const FString& CharacterName)
{
	UE_LOG(LogWowFrame, Log, TEXT("UpdateCharacterSheet: name='%s' level=%d race=%u class=%u"),
		*CharacterName, Player.GetLevel(), Player.GetRaceId(), Player.GetClassId());
	auto SetFrameText = [this](const TCHAR* FrameName, const FString& Value, ETextJustify::Type Justification)
	{
		if (UTextBlock* Text = GetPrimaryTextWidget(FindFrame(FrameName)))
		{
			Text->SetText(FText::FromString(Value));
			Text->SetJustification(Justification);
			Text->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.95f, 0.88f, 0.68f, 1.0f)));
			Text->SetShadowOffset(FVector2D(1.0f));
			Text->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.95f));
			Text->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			Text->SetRenderOpacity(1.0f);
			FSlateFontInfo Font = Text->GetFont();
			Font.Size = FMath::Max(8, FMath::RoundToInt(8.0f * UIScale));
			Text->SetFont(Font);
		}
	};

	static const TCHAR* ClassNames[] = {
		TEXT("Adventurer"), TEXT("Warrior"), TEXT("Paladin"), TEXT("Hunter"), TEXT("Rogue"),
		TEXT("Priest"), TEXT("Death Knight"), TEXT("Shaman"), TEXT("Mage"), TEXT("Warlock"),
		TEXT("Adventurer"), TEXT("Druid")
	};
	const uint8 ClassId = Player.GetClassId();
	const FString ClassName = ClassId < UE_ARRAY_COUNT(ClassNames)
		? ClassNames[ClassId]
		: FString::Printf(TEXT("Class %u"), ClassId);

	FString RaceName = TEXT("Human");
	if (const FChrRacesDbcEntry* Race = FDbcStore::Get().ChrRaces().GetById(Player.GetRaceId());
		Race && !Race->Name.IsEmpty())
	{
		RaceName = Race->Name;
	}

	const FString DisplayName = CharacterName.IsEmpty() ? TEXT("Offline Adventurer") : CharacterName;
	const FString PlayerUnitFrameName = CharacterName.IsEmpty() ? TEXT("Player") : CharacterName;
	const bool bCharacterFrameVisible = IsFrameVisible(FindFrame(TEXT("CharacterFrame")));
	const FFrameRect CharacterFrameRect = GetFrameRect(TEXT("CharacterFrame"));
	const FVector2D CharacterFrameOrigin(
		CharacterFrameRect.X * UIScale, CharacterFrameRect.Y * UIScale);
	// PlayerTitleFrame is the title selector, not the character-name field.
	// Keep a neutral entry until title data is supplied by the connected realm.
	SetFrameText(TEXT("PlayerTitleFrame"), TEXT("No Title"), ETextJustify::Center);
	const FString LevelRaceClass = FString::Printf(
		TEXT("Level %d %s %s"), Player.GetLevel(), *RaceName, *ClassName);
	if (UTextBlock* SharedHeader = GetPrimaryTextWidget(FindFrame(TEXT("CharacterNameFrame"))))
	{
		SharedHeader->SetText(FText::GetEmpty());
		SharedHeader->SetVisibility(ESlateVisibility::Collapsed);
		if (UPanelWidget* Parent = SharedHeader->GetParent())
		{
			Parent->RemoveChild(SharedHeader);
		}
	}
	for (auto& FontPair : NamedFontStringWidgets)
	{
		if (FontPair.Key.StartsWith(TEXT("PlayerTitleFrame")) && FontPair.Value.IsValid())
		{
			UTextBlock* TitleText = FontPair.Value.Get();
			TitleText->SetText(FText::FromString(TEXT("No Title")));
			TitleText->SetJustification(ETextJustify::Center);
			FSlateFontInfo Font = TitleText->GetFont();
			Font.Size = FMath::Max(8, FMath::RoundToInt(8.0f * UIScale));
			TitleText->SetFont(Font);
		}
	}
	if (UCanvasPanel* TitleCanvas = Cast<UCanvasPanel>(
		GetWidgetForHandle(FindFrame(TEXT("PlayerTitleFrame")))))
	{
		if (UCanvasPanelSlot* TitleSlot = Cast<UCanvasPanelSlot>(TitleCanvas->Slot))
		{
			const FVector2D LocalTitlePosition(94.0f, 36.0f);
			const bool bRootOwned = TitleCanvas->GetParent() == RootCanvas.Get();
			TitleSlot->SetAutoSize(false);
			TitleSlot->SetPosition((bRootOwned ? CharacterFrameOrigin : FVector2D::ZeroVector)
				+ LocalTitlePosition * UIScale);
			TitleSlot->SetSize(FVector2D(190.0f, 24.0f) * UIScale);
		}
		UTextBlock* ClientTitle = GetNamedFontString(TEXT("CharacterTitleClientText"));
		for (int32 ChildIndex = TitleCanvas->GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
		{
			if (UTextBlock* AuthoredText = Cast<UTextBlock>(TitleCanvas->GetChildAt(ChildIndex));
				AuthoredText && AuthoredText != ClientTitle)
			{
				TitleCanvas->RemoveChild(AuthoredText);
			}
		}
		if (!ClientTitle)
		{
			ClientTitle = NewObject<UTextBlock>(TitleCanvas);
			TitleCanvas->AddChildToCanvas(ClientTitle);
			NamedFontStringWidgets.Add(TEXT("CharacterTitleClientText"), ClientTitle);
		}
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ClientTitle->Slot))
		{
			Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			Slot->SetOffsets(FMargin(0.0f));
			Slot->SetZOrder(1000);
		}
		ClientTitle->SetText(FText::FromString(TEXT("No Title")));
		ClientTitle->SetJustification(ETextJustify::Center);
		ClientTitle->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.95f, 0.88f, 0.68f, 1.0f)));
		ClientTitle->SetShadowOffset(FVector2D(1.0f));
		ClientTitle->SetShadowColorAndOpacity(FLinearColor::Black);
		ClientTitle->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		FSlateFontInfo Font = ClientTitle->GetFont();
		Font.Size = FMath::Max(8, FMath::RoundToInt(8.0f * UIScale));
		ClientTitle->SetFont(Font);
	}
	if (UTextBlock* LegacyTitleButtonText = GetPrimaryTextWidget(
		FindFrame(TEXT("PlayerTitleFrameButton"))))
	{
		LegacyTitleButtonText->SetText(FText::GetEmpty());
		LegacyTitleButtonText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (UTextBlock* LevelText = GetNamedFontString(TEXT("CharacterLevelText")))
	{
		LevelText->SetText(FText::GetEmpty());
		LevelText->SetVisibility(ESlateVisibility::Collapsed);
	}
	// PaperDollFrame authors three legacy captions directly on its canvas. With
	// the native model bridge unfinished they resolve to fragments such as "N"
	// and "player". Our header/title labels live on CharacterFrame and
	// PlayerTitleFrame, so every direct paper-doll caption is safe to suppress.
	if (UCanvasPanel* PaperDollCanvas = Cast<UCanvasPanel>(
		GetWidgetForHandle(FindFrame(TEXT("PaperDollFrame")))))
	{
		for (int32 ChildIndex = PaperDollCanvas->GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
		{
			if (UTextBlock* LegacyCaption = Cast<UTextBlock>(PaperDollCanvas->GetChildAt(ChildIndex)))
			{
				LegacyCaption->SetText(FText::GetEmpty());
				LegacyCaption->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
	if (UCanvasPanel* CharacterCanvas = Cast<UCanvasPanel>(
		GetWidgetForHandle(FindFrame(TEXT("CharacterFrame")))))
	{
		auto EnsureHeaderLine = [this, CharacterCanvas](const FString& WidgetName,
			const FString& TextValue, const FVector2D& Position, const FVector2D& Size,
			int32 FontSize, const FLinearColor& Color)
		{
			UTextBlock* Text = GetNamedFontString(WidgetName);
			if (!Text)
			{
				Text = NewObject<UTextBlock>(CharacterCanvas);
				CharacterCanvas->AddChildToCanvas(Text);
				NamedFontStringWidgets.Add(WidgetName, Text);
			}
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Text->Slot))
			{
				Slot->SetAnchors(FAnchors(0.0f, 0.0f));
				Slot->SetAlignment(FVector2D::ZeroVector);
				Slot->SetAutoSize(false);
				Slot->SetPosition(Position * UIScale);
				Slot->SetSize(Size * UIScale);
				Slot->SetZOrder(31000);
			}
			Text->SetText(FText::FromString(TextValue));
			Text->SetJustification(ETextJustify::Center);
			Text->SetColorAndOpacity(FSlateColor(Color));
			Text->SetShadowOffset(FVector2D(1.0f));
			Text->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.95f));
			Text->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			FSlateFontInfo Font = WidgetName == TEXT("CharacterSheetClientName")
                ? FCoreStyle::GetDefaultFontStyle("Regular", FontSize) : Text->GetFont();
			Font.Size = FMath::Max(FontSize, FMath::RoundToInt(FontSize * UIScale));
			Text->SetFont(Font);
		};
		EnsureHeaderLine(TEXT("CharacterSheetClientName"), DisplayName,
			FVector2D(82.0f, 24.0f), FVector2D(220.0f, 14.0f), 7,
			WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f));
		EnsureHeaderLine(TEXT("CharacterSheetClientLevel"), LevelRaceClass,
			FVector2D(82.0f, 38.0f), FVector2D(220.0f, 12.0f), 6,
			WowColor::FromWow(0.95f, 0.88f, 0.68f, 1.0f));
	}
	if (UWidget* ModelWidget = GetWidgetForHandle(FindFrame(TEXT("CharacterModelFrame"))))
	{
		TFunction<void(UWidget*)> HideModelPlaceholderText = [&HideModelPlaceholderText](UWidget* Widget)
		{
			if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
			{
				for (int32 ChildIndex = Panel->GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
				{
					UWidget* Child = Panel->GetChildAt(ChildIndex);
					if (UTextBlock* Text = Cast<UTextBlock>(Child))
					{
						Text->SetText(FText::GetEmpty());
						Panel->RemoveChild(Text);
					}
					else
					{
						HideModelPlaceholderText(Child);
					}
				}
			}
		};
		HideModelPlaceholderText(ModelWidget);
	}

	struct FStatRow
	{
		const TCHAR* Frame;
		const TCHAR* ValueFrame;
		const TCHAR* Label;
		int32 Value;
	};
	static const TCHAR* CharacterRightPageNames[] = {
		TEXT("Base Stats"), TEXT("Melee"), TEXT("Ranged"), TEXT("Spell"), TEXT("Defense")
	};
	CharacterRightStatPage = FMath::Clamp(
		CharacterRightStatPage, 0, UE_ARRAY_COUNT(CharacterRightPageNames) - 1);
	CharacterLeftStatPage = FMath::Clamp(
		CharacterLeftStatPage, 0, UE_ARRAY_COUNT(CharacterRightPageNames) - 1);
	// Page 0 is Base Stats; both columns pick any page (the arrays are shared by both sides).
	CharacterRightStatLabels[0] = {
		TEXT("Strength"), TEXT("Agility"), TEXT("Stamina"),
		TEXT("Intellect"), TEXT("Spirit"), TEXT("Armor")
	};
	CharacterRightStatValues[0] = {
		Player.GetStrength(), Player.GetAgility(), Player.GetStamina(),
		Player.GetIntellect(), Player.GetSpirit(), Player.GetArmor()
	};
	CharacterRightStatLabels[1] = {
		TEXT("Attack Power"), TEXT("Weapon Skill"), TEXT("Hit Rating"),
		TEXT("Crit Rating"), TEXT("Haste Rating"), TEXT("Expertise")
	};
	CharacterRightStatValues[1] = {
		Player.GetAttackPower(), Player.GetCombatRating(0), Player.GetCombatRating(5),
		Player.GetCombatRating(8), Player.GetCombatRating(17), Player.GetCombatRating(24)
	};
	CharacterRightStatLabels[2] = {
		TEXT("Ranged Power"), TEXT("Weapon Skill"), TEXT("Hit Rating"),
		TEXT("Crit Rating"), TEXT("Haste Rating"), TEXT("Ammo Bonus")
	};
	CharacterRightStatValues[2] = {
		Player.GetRangedAttackPower(), Player.GetCombatRating(0), Player.GetCombatRating(6),
		Player.GetCombatRating(9), Player.GetCombatRating(18), 0
	};
	CharacterRightStatLabels[3] = {
		TEXT("Spell Power"), TEXT("Healing"), TEXT("Hit Rating"),
		TEXT("Crit Rating"), TEXT("Haste Rating"), TEXT("Mana Regen")
	};
	CharacterRightStatValues[3] = {
		Player.GetIntellect(), Player.GetIntellect() + Player.GetSpirit() / 2,
		Player.GetCombatRating(7), Player.GetCombatRating(10), Player.GetCombatRating(19),
		Player.GetSpirit() / 4 + Player.GetLevel() / 2
	};
	CharacterRightStatLabels[4] = {
		TEXT("Armor"), TEXT("Defense Rating"), TEXT("Dodge Rating"),
		TEXT("Parry Rating"), TEXT("Block Rating"), TEXT("Resilience")
	};
	CharacterRightStatValues[4] = {
		Player.GetArmor(), Player.GetCombatRating(1), Player.GetCombatRating(2),
		Player.GetCombatRating(3), Player.GetCombatRating(4), Player.GetCombatRating(22)
	};
	SetFrameText(TEXT("PlayerStatFrameLeftDropDown"),
		CharacterRightPageNames[CharacterLeftStatPage], ETextJustify::Center);
	SetFrameText(TEXT("PlayerStatFrameRightDropDown"),
		CharacterRightPageNames[CharacterRightStatPage], ETextJustify::Center);
	auto LayoutStatDropDown = [this](const TCHAR* FrameName, const FString& ClientLabelName,
		const FString& LabelText, float LabelX)
	{
		const int64 Handle = FindFrame(FrameName);
		if (UWidget* DropDown = GetWidgetForHandle(Handle))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(DropDown->Slot))
			{
				Slot->SetAutoSize(false);
				Slot->SetSize(FVector2D(165.0f, 32.0f) * UIScale);
			}
		}
		if (UTextBlock* Label = GetPrimaryTextWidget(Handle))
		{
			Label->SetText(FText::GetEmpty());
			Label->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (UCanvasPanel* CharacterCanvas = Cast<UCanvasPanel>(
			GetWidgetForHandle(FindFrame(TEXT("CharacterFrame")))))
		{
			UTextBlock* Label = GetNamedFontString(ClientLabelName);
			if (Label && Label->GetParent() != CharacterCanvas)
			{
				if (UPanelWidget* OldParent = Label->GetParent())
				{
					OldParent->RemoveChild(Label);
				}
				CharacterCanvas->AddChildToCanvas(Label);
			}
			if (!Label)
			{
				Label = NewObject<UTextBlock>(CharacterCanvas);
				CharacterCanvas->AddChildToCanvas(Label);
				NamedFontStringWidgets.Add(ClientLabelName, Label);
			}
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Label->Slot))
			{
				Slot->SetAnchors(FAnchors(0.0f, 0.0f));
				Slot->SetAlignment(FVector2D::ZeroVector);
				Slot->SetAutoSize(false);
				Slot->SetPosition(FVector2D(67.0f + LabelX, 270.0f) * UIScale);
				Slot->SetSize(FVector2D(115.0f, 16.0f) * UIScale);
				Slot->SetZOrder(31000);
			}
			Label->SetText(FText::FromString(LabelText));
			Label->SetJustification(ETextJustify::Center);
			Label->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.95f, 0.88f, 0.68f, 1.0f)));
			Label->SetShadowOffset(FVector2D(1.0f));
			Label->SetShadowColorAndOpacity(FLinearColor::Black);
			Label->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = FMath::Max(8, FMath::RoundToInt(8.0f * UIScale));
			Label->SetFont(Font);
		}
	};
	LayoutStatDropDown(TEXT("PlayerStatFrameLeftDropDown"),
		TEXT("CharacterStatLeftCategoryClient"), CharacterRightPageNames[CharacterLeftStatPage], 0.0f);
	LayoutStatDropDown(TEXT("PlayerStatFrameRightDropDown"),
		TEXT("CharacterStatRightCategoryClient"),
		CharacterRightPageNames[CharacterRightStatPage], 115.0f);
	const FStatRow StatRows[] = {
		{TEXT("PlayerStatFrameLeft1"), TEXT("PlayerStatFrameLeft1Stat"), TEXT("Strength"), Player.GetStrength()},
		{TEXT("PlayerStatFrameLeft2"), TEXT("PlayerStatFrameLeft2Stat"), TEXT("Agility"), Player.GetAgility()},
		{TEXT("PlayerStatFrameLeft3"), TEXT("PlayerStatFrameLeft3Stat"), TEXT("Stamina"), Player.GetStamina()},
		{TEXT("PlayerStatFrameLeft4"), TEXT("PlayerStatFrameLeft4Stat"), TEXT("Intellect"), Player.GetIntellect()},
		{TEXT("PlayerStatFrameLeft5"), TEXT("PlayerStatFrameLeft5Stat"), TEXT("Spirit"), Player.GetSpirit()},
		{TEXT("PlayerStatFrameLeft6"), TEXT("PlayerStatFrameLeft6Stat"), TEXT("Armor"), Player.GetArmor()},
		{TEXT("PlayerStatFrameRight1"), TEXT("PlayerStatFrameRight1Stat"), TEXT("Attack Power"), Player.GetAttackPower()},
		{TEXT("PlayerStatFrameRight2"), TEXT("PlayerStatFrameRight2Stat"), TEXT("Weapon Skill"), Player.GetCombatRating(0)},
		{TEXT("PlayerStatFrameRight3"), TEXT("PlayerStatFrameRight3Stat"), TEXT("Hit Rating"), Player.GetCombatRating(5)},
		{TEXT("PlayerStatFrameRight4"), TEXT("PlayerStatFrameRight4Stat"), TEXT("Crit Rating"), Player.GetCombatRating(8)},
		{TEXT("PlayerStatFrameRight5"), TEXT("PlayerStatFrameRight5Stat"), TEXT("Haste Rating"), Player.GetCombatRating(17)},
		{TEXT("PlayerStatFrameRight6"), TEXT("PlayerStatFrameRight6Stat"), TEXT("Expertise"), Player.GetCombatRating(24)},
	};
	for (const FStatRow& Row : StatRows)
	{
		SetFrameText(Row.Frame, Row.Label, ETextJustify::Left);
		SetFrameText(Row.ValueFrame, FString::FromInt(Row.Value), ETextJustify::Right);
		if (UTextBlock* Label = GetPrimaryTextWidget(FindFrame(Row.Frame)))
		{
			Label->SetRenderTranslation(FVector2D(-4.0f, -5.0f) * UIScale);
		}
		if (UTextBlock* Value = GetPrimaryTextWidget(FindFrame(Row.ValueFrame)))
		{
			Value->SetRenderTranslation(FVector2D(-4.0f, -5.0f) * UIScale);
		}
		if (UTextBlock* Label = GetNamedFontString(FString(Row.Frame) + TEXT("Label")))
		{
			Label->SetText(FText::FromString(Row.Label));
			Label->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			Label->SetRenderOpacity(1.0f);
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = FMath::Max(8, FMath::RoundToInt(8.0f * UIScale));
			Label->SetFont(Font);
			Label->SetRenderTranslation(FVector2D(-4.0f, -5.0f) * UIScale);
		}
		if (UTextBlock* Value = GetNamedFontString(FString(Row.Frame) + TEXT("StatText")))
		{
			Value->SetText(FText::AsNumber(Row.Value));
			Value->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			Value->SetRenderOpacity(1.0f);
			FSlateFontInfo Font = Value->GetFont();
			Font.Size = FMath::Max(8, FMath::RoundToInt(8.0f * UIScale));
			Value->SetFont(Font);
			Value->SetRenderTranslation(FVector2D(-4.0f, -5.0f) * UIScale);
		}
	}

	static const TCHAR* RaceTokens[] = {
		TEXT("Human"), TEXT("Orc"), TEXT("Dwarf"), TEXT("NightElf"), TEXT("Scourge"),
		TEXT("Tauren"), TEXT("Gnome"), TEXT("Troll"), TEXT("BloodElf"), TEXT("Draenei")
	};
	static const uint8 RaceIds[] = {1, 2, 3, 4, 5, 6, 7, 8, 10, 11};
	FString RaceToken = TEXT("Human");
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(RaceIds); ++Index)
	{
		if (Player.GetRaceId() == RaceIds[Index])
		{
			RaceToken = RaceTokens[Index];
			break;
		}
	}
	const FString GenderToken = Player.GetGenderId() == 1 ? TEXT("Female") : TEXT("Male");
	UTexture2D* PlayerPortraitTexture = LoadUITexture(FString::Printf(
		TEXT("Interface\\CharacterFrame\\TEMPORARYPORTRAIT-%s-%s"),
		*GenderToken, *RaceToken));
	if (!PlayerPortraitTexture)
	{
		PlayerPortraitTexture = LoadUITexture(TEXT("Interface\\Icons\\INV_Misc_QuestionMark"));
	}
	auto ApplyPlayerPortrait = [this, PlayerPortraitTexture](UImage* Image)
	{
		if (!Image || !PlayerPortraitTexture)
		{
			return;
		}
		// With the live 3D portrait available, keep the slot empty until SetPortraitTexture
		// fills it: the generic TEMPORARYPORTRAIT art flashed a different-looking character.
		if (UnitPortraitProvider && Image != GetNamedTexture(TEXT("MicroButtonPortrait")))
		{
			Image->SetRenderOpacity(0.0f);
			Image->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			return;
		}

		FSlateBrush Brush;
		Brush.SetResourceObject(PlayerPortraitTexture);
		Brush.ImageSize = FVector2D(
			PlayerPortraitTexture->GetSizeX(), PlayerPortraitTexture->GetSizeY());
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Tiling = ESlateBrushTileType::NoTile;
		const FBox2f KeptUV = Image->GetBrush().GetUVRegion(); // keep the crop its button set (micro-menu portrait)
		Brush.SetUVRegion(KeptUV);
		Image->SetBrush(Brush);
		Image->SetColorAndOpacity(FLinearColor::White);
		Image->SetRenderOpacity(1.0f);
		Image->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	};

	UImage* AuthoredPlayerPortrait = GetNamedTexture(TEXT("PlayerPortrait"));
	UImage* DuplicatePlayerPortrait = GetNamedTexture(TEXT("PlayerFramePortrait"));
	ApplyPlayerPortrait(AuthoredPlayerPortrait);
	ApplyPlayerPortrait(GetNamedTexture(TEXT("MicroButtonPortrait")));
	if (DuplicatePlayerPortrait && DuplicatePlayerPortrait != AuthoredPlayerPortrait)
	{
		DuplicatePlayerPortrait->SetVisibility(ESlateVisibility::Collapsed);
	}
	const int64 CharacterMicroButtonHandle = FindFrame(TEXT("CharacterMicroButton"));
	if (CharacterMicroButtonHandle >= 0)
	{
		SetFrameVisible(CharacterMicroButtonHandle, true);
		// r394: a block here loaded "UI-MicroButton-Character-Up", which does not exist (42 "Failed to read UI
		// texture" warnings and a failed archive search per call in the 16.10 log), so it never did anything.
		// The button keeps its own art (SetNormalTexture / the r297 fallback UI-MicroButtonCharacter-Up)
		// with its pushed state; the dead block is removed.
	}
	if (UImage* Portrait = GetNamedTexture(TEXT("CharacterFramePortrait")))
	{
		Portrait->SetVisibility(ESlateVisibility::Collapsed);
		Portrait->SetRenderOpacity(0.0f);
	}
	if (UImage* LegacyClientPortrait = GetNamedTexture(TEXT("CharacterFrameClientPortrait")))
	{
		LegacyClientPortrait->SetVisibility(ESlateVisibility::Collapsed);
		LegacyClientPortrait->SetRenderOpacity(0.0f);
	}

	// Keep the native character data overlay above the authored HIGH-strata panel
	// while tying its visibility explicitly to CharacterFrame.
	if (RootCanvas.IsValid())
	{
		constexpr int32 OverlayZ = 3990; // Above HIGH panels, below DIALOG strata.

		auto EnsureRootText = [this, &CharacterFrameOrigin, bCharacterFrameVisible](const FString& WidgetName,
			const FString& TextValue, const FVector2D& LocalPosition, const FVector2D& Size,
			int32 FontSize, ETextJustify::Type Justification, const FLinearColor& Color)
		{
			UTextBlock* Text = GetNamedFontString(WidgetName);
			if (Text && Text->GetParent() != RootCanvas.Get())
			{
				if (UPanelWidget* PreviousParent = Text->GetParent())
				{
					PreviousParent->RemoveChild(Text);
				}
				RootCanvas->AddChildToCanvas(Text);
			}
			if (!Text)
			{
				Text = NewObject<UTextBlock>(RootCanvas.Get());
				RootCanvas->AddChildToCanvas(Text);
				NamedFontStringWidgets.Add(WidgetName, Text);
			}
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Text->Slot))
			{
				Slot->SetAnchors(FAnchors(0.0f, 0.0f));
				Slot->SetAlignment(FVector2D::ZeroVector);
				Slot->SetAutoSize(false);
				Slot->SetPosition(CharacterFrameOrigin + LocalPosition * UIScale);
				Slot->SetSize(Size * UIScale);
				Slot->SetZOrder(OverlayZ);
			}
			Text->SetText(FText::FromString(TextValue));
			Text->SetJustification(Justification);
			Text->SetColorAndOpacity(FSlateColor(Color));
			Text->SetShadowOffset(FVector2D(1.0f));
			Text->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.95f));
			Text->SetVisibility(bCharacterFrameVisible
				? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			FSlateFontInfo Font = Text->GetFont();
			Font.Size = FMath::Max(FontSize, FMath::RoundToInt(FontSize * UIScale));
			Text->SetFont(Font);
		};

		EnsureRootText(TEXT("CharacterSheetOverlayName"), DisplayName,
			FVector2D(84.0f, 17.0f), FVector2D(214.0f, 13.0f), 8,
			ETextJustify::Center, WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f));
		EnsureRootText(TEXT("CharacterSheetOverlayLevel"), LevelRaceClass,
			FVector2D(84.0f, 40.0f), FVector2D(214.0f, 14.0f), 7,
			ETextJustify::Center, WowColor::FromWow(0.95f, 0.88f, 0.68f, 1.0f));
		const FFrameRect LeftStatBackground = GetFrameRect(TEXT("PlayerStatLeftTop"));
		const FFrameRect RightStatBackground = GetFrameRect(TEXT("PlayerStatRightTop"));
		const FVector2D LeftCategoryPosition = LeftStatBackground.W > 0.0f
			? FVector2D(LeftStatBackground.X - CharacterFrameRect.X,
				LeftStatBackground.Y - CharacterFrameRect.Y - 14.0f)
			: FVector2D(35.0f, 260.0f);
		const FVector2D RightCategoryPosition = RightStatBackground.W > 0.0f
			? FVector2D(RightStatBackground.X - CharacterFrameRect.X,
				RightStatBackground.Y - CharacterFrameRect.Y - 14.0f)
			: FVector2D(140.0f, 260.0f);
		const FVector2D LeftCategorySize = LeftStatBackground.W > 0.0f
			? FVector2D(LeftStatBackground.W, 13.0f) : FVector2D(100.0f, 13.0f);
		const FVector2D RightCategorySize = RightStatBackground.W > 0.0f
			? FVector2D(RightStatBackground.W, 13.0f) : FVector2D(100.0f, 13.0f);
		EnsureRootText(TEXT("CharacterSheetOverlayLeftCategory"), CharacterRightPageNames[CharacterLeftStatPage],
			LeftCategoryPosition, LeftCategorySize, 6,
			ETextJustify::Center, WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f));
		EnsureRootText(TEXT("CharacterSheetOverlayRightCategory"),
			CharacterRightPageNames[CharacterRightStatPage],
			RightCategoryPosition, RightCategorySize, 6,
			ETextJustify::Center, WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f));

		for (int32 RowIndex = 0; RowIndex < UE_ARRAY_COUNT(StatRows); ++RowIndex)
		{
			const bool bRightColumn = RowIndex >= 6;
			const int32 ColumnRow = bRightColumn ? RowIndex - 6 : RowIndex;
			const int32 ColumnPage = bRightColumn ? CharacterRightStatPage : CharacterLeftStatPage;
			const FString RowLabel = CharacterRightStatLabels[ColumnPage].IsValidIndex(ColumnRow)
				? CharacterRightStatLabels[ColumnPage][ColumnRow]
				: FString(StatRows[RowIndex].Label);
			const int32 RowValue = CharacterRightStatValues[ColumnPage].IsValidIndex(ColumnRow)
				? CharacterRightStatValues[ColumnPage][ColumnRow]
				: StatRows[RowIndex].Value;
			const FFrameRect LabelRect = GetFrameRect(StatRows[RowIndex].Frame);
			const FFrameRect ValueRect = GetFrameRect(StatRows[RowIndex].ValueFrame);
			const bool bHasAuthoredRow = LabelRect.W > 0.0f && LabelRect.H > 0.0f &&
				ValueRect.W > 0.0f && ValueRect.H > 0.0f;
			const float ColumnX = bRightColumn ? 140.0f : 35.0f;
			const float RowY = 279.0f + static_cast<float>(ColumnRow) * 16.0f;
			const FVector2D LabelPosition = bHasAuthoredRow
				? FVector2D(LabelRect.X - CharacterFrameRect.X,
					LabelRect.Y - CharacterFrameRect.Y)
				: FVector2D(ColumnX, RowY);
			const FVector2D ValuePosition = bHasAuthoredRow
				? FVector2D(ValueRect.X - CharacterFrameRect.X,
					ValueRect.Y - CharacterFrameRect.Y)
				: FVector2D(ColumnX + 69.0f, RowY);
			const FVector2D LabelSize = bHasAuthoredRow
				? FVector2D(FMath::Max(1.0f, ValueRect.X - LabelRect.X), LabelRect.H)
				: FVector2D(69.0f, 12.0f);
			const FVector2D ValueSize = bHasAuthoredRow
				? FVector2D(ValueRect.W, ValueRect.H) : FVector2D(28.0f, 12.0f);
			// Stefan 10.10: the text sat against the left edge and the top of each row box. Inset the
			// label 4 units from the left and the value 4 from the right, and drop both 2 units so
			// the line is centred in the 12-13 unit row.
			// Stefan 10.10 (again): better inside the panel. Like the 3.3.5 sheet: "Strength:" in the gold
			// GameFontNormalSmall, the value in white, 6 units in from both panel edges.
			const FVector2D StatTextNudge(0.0f, 2.0f);
			EnsureRootText(FString::Printf(TEXT("CharacterSheetOverlayStatLabel%d"), RowIndex),
				RowLabel + TEXT(":"), LabelPosition + StatTextNudge + FVector2D(6.0f, 0.0f), LabelSize - FVector2D(6.0f, 0.0f), 6,
				ETextJustify::Left, WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f));
			EnsureRootText(FString::Printf(TEXT("CharacterSheetOverlayStatValue%d"), RowIndex),
				FString::FromInt(RowValue), ValuePosition + StatTextNudge,
				ValueSize - FVector2D(6.0f, 0.0f), 6, ETextJustify::Right,
				FLinearColor::White);
		}

		UImage* OverlayPortrait = GetNamedTexture(TEXT("CharacterSheetOverlayPortrait"));
		if (!OverlayPortrait)
		{
			OverlayPortrait = NewObject<UImage>(RootCanvas.Get());
			RootCanvas->AddChildToCanvas(OverlayPortrait);
			NamedTextureWidgets.Add(TEXT("CharacterSheetOverlayPortrait"), OverlayPortrait);
		}
		if (OverlayPortrait)
		{
			if (UCanvasPanelSlot* PortraitSlot = Cast<UCanvasPanelSlot>(OverlayPortrait->Slot))
			{
				const FFrameRect AuthoredPortraitRect = GetFrameRect(TEXT("CharacterFramePortrait"));
				const bool bHasAuthoredPortraitRect =
					AuthoredPortraitRect.W > 0.0f && AuthoredPortraitRect.H > 0.0f;
				const FVector2D PortraitPosition = bHasAuthoredPortraitRect
					? FVector2D(AuthoredPortraitRect.X + 6.0f, AuthoredPortraitRect.Y + 6.0f)
					: CharacterFrameOrigin / UIScale + FVector2D(14.0f, 12.0f);
				const FVector2D PortraitSize = bHasAuthoredPortraitRect
					? FVector2D(AuthoredPortraitRect.W - 12.0f, AuthoredPortraitRect.H - 12.0f)
					: FVector2D(48.0f, 48.0f);
				PortraitSlot->SetAnchors(FAnchors(0.0f, 0.0f));
				PortraitSlot->SetAlignment(FVector2D::ZeroVector);
				PortraitSlot->SetAutoSize(false);
				PortraitSlot->SetPosition(PortraitPosition * UIScale);
				PortraitSlot->SetSize(PortraitSize * UIScale);
				PortraitSlot->SetZOrder(OverlayZ);
			}
			UTexture2D* PortraitTexture = PlayerPortraitTexture;
			if (PortraitTexture)
			{
				FSlateBrush Brush;
				Brush.SetResourceObject(PortraitTexture);
				Brush.ImageSize = FVector2D(PortraitTexture->GetSizeX(), PortraitTexture->GetSizeY());
				Brush.DrawAs = ESlateBrushDrawType::Image;
				OverlayPortrait->SetBrush(Brush);
				OverlayPortrait->SetColorAndOpacity(FLinearColor::White);
				OverlayPortrait->SetRenderOpacity(1.0f);
				OverlayPortrait->SetVisibility(bCharacterFrameVisible
					? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			}
		}

		// The native overlay above is authoritative. Suppress the incomplete XML
		// labels that otherwise duplicate it at slightly different coordinates.
		for (const TCHAR* LegacyName : {
			TEXT("CharacterSheetClientName"), TEXT("CharacterSheetClientLevel"),
			TEXT("CharacterStatLeftCategoryClient"), TEXT("CharacterStatRightCategoryClient") })
		{
			if (UTextBlock* LegacyText = GetNamedFontString(LegacyName))
			{
				LegacyText->SetText(FText::GetEmpty());
				LegacyText->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		for (const FStatRow& Row : StatRows)
		{
			for (const TCHAR* FrameName : {Row.Frame, Row.ValueFrame})
			{
				if (UTextBlock* LegacyText = GetPrimaryTextWidget(FindFrame(FrameName)))
				{
					LegacyText->SetText(FText::GetEmpty());
					LegacyText->SetVisibility(ESlateVisibility::Collapsed);
				}
			}
			for (const FString& FontStringName : {
				FString(Row.Frame) + TEXT("Label"), FString(Row.Frame) + TEXT("StatText") })
			{
				if (UTextBlock* LegacyText = GetNamedFontString(FontStringName))
				{
					LegacyText->SetText(FText::GetEmpty());
					LegacyText->SetVisibility(ESlateVisibility::Collapsed);
				}
			}
		}
	}
	EnsureNativeCharacterStatControls();
	RefreshNativeCharacterStatControls();
	EnsureNativeCharacterTitleControl();
	RefreshNativeCharacterTitleControl();
	EnsureNativeCharacterSheetTabs();
	RefreshNativeCharacterSheetTabs();

	// Equipment-manager mode uses a large red "leave item" overlay. It is not
	// the normal paper-doll empty state and must never become the slot icon.
	struct FSlotPlaceholder
	{
		const TCHAR* FrameName;
		const TCHAR* IconName;
		const TCHAR* TexturePath;
		const TCHAR* EquippedTexturePath;
		int32 EquipmentSlot;
	};
	const FSlotPlaceholder Placeholders[] = {
		{TEXT("CharacterHeadSlot"), TEXT("CharacterHeadSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Head"), TEXT("Interface\\Icons\\INV_Helmet_02"), 0},
		{TEXT("CharacterNeckSlot"), TEXT("CharacterNeckSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Neck"), TEXT("Interface\\Icons\\INV_Jewelry_Necklace_03"), 1},
		{TEXT("CharacterShoulderSlot"), TEXT("CharacterShoulderSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Shoulder"), TEXT("Interface\\Icons\\INV_Shoulder_05"), 2},
		{TEXT("CharacterBackSlot"), TEXT("CharacterBackSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Chest"), TEXT("Interface\\Icons\\INV_Misc_Cape_01"), 14},
		{TEXT("CharacterChestSlot"), TEXT("CharacterChestSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Chest"), TEXT("Interface\\Icons\\INV_Chest_Leather_01"), 4},
		{TEXT("CharacterShirtSlot"), TEXT("CharacterShirtSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Shirt"), TEXT("Interface\\Icons\\INV_Shirt_02"), 3},
		{TEXT("CharacterTabardSlot"), TEXT("CharacterTabardSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Tabard"), TEXT("Interface\\Icons\\INV_Misc_Tabard_01"), 18},
		{TEXT("CharacterWristSlot"), TEXT("CharacterWristSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Wrists"), TEXT("Interface\\Icons\\INV_Bracer_03"), 8},
		{TEXT("CharacterHandsSlot"), TEXT("CharacterHandsSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Hands"), TEXT("Interface\\Icons\\INV_Gauntlets_05"), 9},
		{TEXT("CharacterWaistSlot"), TEXT("CharacterWaistSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Waist"), TEXT("Interface\\Icons\\INV_Belt_05"), 5},
		{TEXT("CharacterLegsSlot"), TEXT("CharacterLegsSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Legs"), TEXT("Interface\\Icons\\INV_Pants_03"), 6},
		{TEXT("CharacterFeetSlot"), TEXT("CharacterFeetSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Feet"), TEXT("Interface\\Icons\\INV_Boots_08"), 7},
		{TEXT("CharacterFinger0Slot"), TEXT("CharacterFinger0SlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Finger"), TEXT("Interface\\Icons\\INV_Jewelry_Ring_03"), 10},
		{TEXT("CharacterFinger1Slot"), TEXT("CharacterFinger1SlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Finger"), TEXT("Interface\\Icons\\INV_Jewelry_Ring_02"), 11},
		{TEXT("CharacterTrinket0Slot"), TEXT("CharacterTrinket0SlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Trinket"), TEXT("Interface\\Icons\\INV_Jewelry_Talisman_01"), 12},
		{TEXT("CharacterTrinket1Slot"), TEXT("CharacterTrinket1SlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Trinket"), TEXT("Interface\\Icons\\INV_Jewelry_Talisman_02"), 13},
		{TEXT("CharacterMainHandSlot"), TEXT("CharacterMainHandSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-MainHand"), TEXT("Interface\\Icons\\INV_Sword_04"), 15},
		{TEXT("CharacterSecondaryHandSlot"), TEXT("CharacterSecondaryHandSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-SecondaryHand"), TEXT("Interface\\Icons\\INV_Shield_05"), 16},
		{TEXT("CharacterRangedSlot"), TEXT("CharacterRangedSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Ranged"), TEXT("Interface\\Icons\\INV_Weapon_Bow_01"), 17},
		{TEXT("CharacterAmmoSlot"), TEXT("CharacterAmmoSlotIconTexture"), TEXT("Interface\\PaperDoll\\UI-PaperDoll-Slot-Ammo"), TEXT("Interface\\Icons\\INV_Ammo_Arrow_02"), INDEX_NONE},
	};
	auto ResolveEquippedItemIcon = [this](uint32 ItemEntry) -> FString
	{
		if (ItemEntry == 0)
		{
			return FString();
		}

		// Item.dbc lacks server-side items; the queried template's display id is the
		// fallback (Journeyman's Stave kept the generic shield icon in the off-hand slot).
		// The server's item template wins over the client Item.dbc: private servers change
		// display ids (Stefan 10.10: his staff showed a skull icon in the main-hand slot).
		const FDbcStore& DbcStore = FDbcStore::Get();
		uint32 DisplayId = 0;
		if (ItemConnection.IsValid())
		{
			if (const FWowItemTemplateInfo* Info = ItemConnection->PacketHandler.ItemTemplates.Find(ItemEntry))
				DisplayId = Info->DisplayId;
			else
				ItemConnection->RequestItemInfo(ItemEntry);
		}
		if (!DisplayId)
			if (const FItemDbcEntry* Item = DbcStore.Items().GetById(ItemEntry))
				DisplayId = Item->DisplayInfoID;
		const FItemDisplayInfoDbcEntry* Display = DisplayId ? DbcStore.ItemDisplayInfo().GetById(DisplayId) : nullptr;
		if ((!Display || Display->Icon1.IsEmpty()) && ItemConnection.IsValid())
			if (const FWowItemTemplateInfo* Info = ItemConnection->PacketHandler.ItemTemplates.Find(ItemEntry))
				Display = DbcStore.ItemDisplayInfo().GetById(Info->DisplayId);
		if (!Display || Display->Icon1.IsEmpty())
		{
			return FString();
		}

		FString IconPath = Display->Icon1;
		IconPath.ReplaceInline(TEXT("/"), TEXT("\\"));
		if (!IconPath.Contains(TEXT("\\")))
		{
			IconPath = TEXT("Interface\\Icons\\") + IconPath;
		}
		return IconPath;
	};
	FSlateBrush QuickSlotBrush;
	if (UTexture2D* QuickSlotTexture = LoadUITexture(TEXT("Interface\\Buttons\\UI-Quickslot2")))
	{
		QuickSlotBrush.SetResourceObject(QuickSlotTexture);
		QuickSlotBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	FSlateBrush PressedSlotBrush = QuickSlotBrush;
	if (UTexture2D* PressedTexture = LoadUITexture(TEXT("Interface\\Buttons\\UI-Quickslot-Depress")))
	{
		PressedSlotBrush.SetResourceObject(PressedTexture);
	}
	auto IsForbiddenSlotOverlay = [](const UImage* Image) -> bool
	{
		if (!Image)
		{
			return false;
		}

		const UObject* Resource = Image->GetBrush().GetResourceObject();
		const FString ResourceName = Resource ? Resource->GetName().ToLower() : FString();
		const FString WidgetName = Image->GetName().ToLower();
		const FLinearColor Tint = Image->GetColorAndOpacity();
		return ResourceName.Contains(TEXT("gearmanager")) ||
			ResourceName.Contains(TEXT("leaveitem")) ||
			ResourceName.Contains(TEXT("grouploot-pass")) ||
			ResourceName.Contains(TEXT("unable")) ||
			ResourceName.Contains(TEXT("forbidden")) ||
			WidgetName.Contains(TEXT("gearmanager")) ||
			WidgetName.Contains(TEXT("leaveitem")) ||
			WidgetName.Contains(TEXT("forbidden")) ||
			(Tint.R > 0.55f && Tint.G < 0.20f && Tint.B < 0.20f && Tint.A > 0.20f);
	};
	auto HideForbiddenSlotOverlay = [&IsForbiddenSlotOverlay](UImage* Image)
	{
		if (IsForbiddenSlotOverlay(Image))
		{
			Image->SetColorAndOpacity(FLinearColor::Transparent);
			Image->SetRenderOpacity(0.0f);
			Image->SetVisibility(ESlateVisibility::Collapsed);
			return true;
		}
		return false;
	};
	for (const FSlotPlaceholder& Placeholder : Placeholders)
	{
		if (UWidget* SlotWidget = GetWidgetForHandle(FindFrame(Placeholder.FrameName)))
		{
			SlotWidget->SetRenderTranslation(FVector2D(4.0f, 3.0f) * UIScale);
			SlotWidget->SetVisibility(ESlateVisibility::Visible);
		}
		if (UCanvasPanel* SlotCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(FindFrame(Placeholder.FrameName))))
		{
			for (int32 ChildIndex = SlotCanvas->GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
			{
				UImage* LayerImage = Cast<UImage>(SlotCanvas->GetChildAt(ChildIndex));
				if (HideForbiddenSlotOverlay(LayerImage))
				{
					SlotCanvas->RemoveChild(LayerImage);
				}
			}
		}
		if (UButton* Button = GetButtonWidget(FindFrame(Placeholder.FrameName)))
		{
			FButtonStyle Style;
			Style.SetNormal(QuickSlotBrush);
			Style.SetHovered(QuickSlotBrush);
			Style.SetPressed(PressedSlotBrush);
			Style.SetDisabled(QuickSlotBrush);
			Style.SetNormalPadding(FMargin(0.0f));
			Style.SetPressedPadding(FMargin(0.0f));
			Button->SetStyle(Style);
			Button->SetBackgroundColor(FLinearColor::White);
			Button->SetIsEnabled(true);
		}
		UImage* AuthoredIcon = GetNamedTexture(Placeholder.IconName);
		// The equipped item object is authoritative; the player's VISIBLE_ITEM fields can
		// arrive in a later update than the inventory change that triggered this refresh,
		// which left the previous weapon's icon in the slot after equipping.
		uint32 ItemEntry = 0;
		if (Placeholder.EquipmentSlot != INDEX_NONE)
		{
			const uint64 EquippedGuid = Player.GetEquipmentItemGuid(static_cast<uint8>(Placeholder.EquipmentSlot));
			if (EquippedGuid != 0 && ItemConnection.IsValid())
			{
				if (const FWowItemEntity* Equipped = ItemConnection->PacketHandler.EntityManager.FindItem(EquippedGuid))
				{
					ItemEntry = Equipped->Entry;
				}
			}
			if (ItemEntry == 0 && EquippedGuid != 0)
			{
				ItemEntry = Player.GetField(PlayerField::VISIBLE_ITEM_BASE + Placeholder.EquipmentSlot * 2);
			}
		}
		const bool bEmptySlot = Placeholder.EquipmentSlot == INDEX_NONE ||
			(ItemEntry == 0 &&
			 Player.GetEquipmentItemGuid(static_cast<uint8>(Placeholder.EquipmentSlot)) == 0);
		// The authored icon is removed on the first refresh, so later refreshes find only our
		// "Client" icon. Skipping the slot when the authored one was gone froze every slot at
		// the first-open state (Stefan 10.10: new staff and gloves never shown).
		const FString ClientIconName = FString(Placeholder.IconName) + TEXT("Client");
		UImage* Icon = GetNamedTexture(ClientIconName);
		if (!AuthoredIcon && !Icon)
		{
			continue;
		}
		if (!Icon)
		{
			if (UCanvasPanel* ParentCanvas = Cast<UCanvasPanel>(AuthoredIcon->GetParent()))
			{
				Icon = NewObject<UImage>(ParentCanvas);
				if (UCanvasPanelSlot* NewSlot = ParentCanvas->AddChildToCanvas(Icon))
				{
					if (const UCanvasPanelSlot* OldSlot = Cast<UCanvasPanelSlot>(AuthoredIcon->Slot))
					{
						NewSlot->SetAnchors(OldSlot->GetAnchors());
						NewSlot->SetPosition(OldSlot->GetPosition());
						NewSlot->SetSize(OldSlot->GetSize());
						NewSlot->SetAlignment(OldSlot->GetAlignment());
						NewSlot->SetZOrder(FMath::Max(OldSlot->GetZOrder() + 1, 32000));
					}
				}
				NamedTextureWidgets.Add(ClientIconName, Icon);
			}
		}
		if (AuthoredIcon)
		{
			AuthoredIcon->SetVisibility(ESlateVisibility::Collapsed);
			if (UPanelWidget* Parent = AuthoredIcon->GetParent())
			{
				Parent->RemoveChild(AuthoredIcon);
			}
		}
		if (!Icon)
		{
			continue;
		}
		FString ChosenTexture = bEmptySlot ? Placeholder.TexturePath : ResolveEquippedItemIcon(ItemEntry);
		if (!bEmptySlot && ChosenTexture.IsEmpty())
		{
			ChosenTexture = Placeholder.EquippedTexturePath;
		}
		if (UTexture2D* Texture = LoadUITexture(ChosenTexture))
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = FVector2D(32.0f * UIScale);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Icon->SetBrush(Brush);
			Icon->SetColorAndOpacity(FLinearColor::White);
			Icon->SetRenderOpacity(1.0f);
			Icon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* IconSlot = Cast<UCanvasPanelSlot>(Icon->Slot))
			{
				IconSlot->SetAutoSize(false);
				IconSlot->SetPosition(FVector2D::ZeroVector);
				IconSlot->SetSize(FVector2D(32.0f * UIScale));
				IconSlot->SetZOrder(32000);
			}
		}
	}

	SetFrameVisible(FindFrame(TEXT("PaperDollFrameItemFlyoutButtons")), false);
	for (auto& TexturePair : NamedTextureWidgets)
	{
		const bool bPaperDollSlotTexture =
			TexturePair.Key.StartsWith(TEXT("Character")) &&
			TexturePair.Key.Contains(TEXT("Slot"));
		if (TexturePair.Value.IsValid() &&
			(TexturePair.Key.Contains(TEXT("ItemFlyout")) ||
			 TexturePair.Key.Contains(TEXT("EquipmentManager")) ||
			 (bPaperDollSlotTexture && IsForbiddenSlotOverlay(TexturePair.Value.Get()))))
		{
			HideForbiddenSlotOverlay(TexturePair.Value.Get());
		}
	}

	// The stock XML's unimplemented model widget leaves orientation/debug labels
	// behind. Keep the live header and stats, but suppress those model-only labels.
	if (UWidget* CharacterWidget = GetWidgetForHandle(FindFrame(TEXT("CharacterFrame"))))
	{
		TFunction<void(UWidget*)> HideLegacyModelLabels = [&HideLegacyModelLabels](UWidget* Widget)
		{
			if (UTextBlock* Text = Cast<UTextBlock>(Widget))
			{
				const FString Value = Text->GetText().ToString().TrimStartAndEnd();
				if (Value.Equals(TEXT("player"), ESearchCase::IgnoreCase) ||
					Value.Equals(TEXT("N"), ESearchCase::CaseSensitive) ||
					Value.Equals(TEXT("X"), ESearchCase::CaseSensitive))
				{
					Text->SetText(FText::GetEmpty());
					Text->SetVisibility(ESlateVisibility::Collapsed);
				}
				return;
			}
			if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
			{
				for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
				{
					HideLegacyModelLabels(Panel->GetChildAt(ChildIndex));
				}
			}
		};
		HideLegacyModelLabels(CharacterWidget);
	}
	if (RootCanvas.IsValid())
	{
		TFunction<void(UWidget*)> HideDetachedPlayerLabel = [&HideDetachedPlayerLabel](UWidget* Widget)
		{
			if (UTextBlock* Text = Cast<UTextBlock>(Widget))
			{
				const FString Value = Text->GetText().ToString().TrimStartAndEnd();
				if (Value.Equals(TEXT("player"), ESearchCase::IgnoreCase) ||
					Value.Equals(TEXT("N"), ESearchCase::CaseSensitive) ||
					Value.Equals(TEXT("X"), ESearchCase::CaseSensitive))
				{
					Text->SetText(FText::GetEmpty());
					Text->SetVisibility(ESlateVisibility::Collapsed);
				}
				return;
			}
			if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
			{
				for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
				{
					HideDetachedPlayerLabel(Panel->GetChildAt(ChildIndex));
				}
			}
		};
		HideDetachedPlayerLabel(RootCanvas.Get());
	}
}

int64 FWowFrameManager::CreateDebugFrame(const FString& Name, float Width, float Height, float X, float Y)
{
	// Create a simple debug frame with a background and text
	FWowFrameDef DebugDef;
	DebugDef.Name = Name;
	DebugDef.Type = EWowFrameType::Frame;
	DebugDef.Width = Width;
	DebugDef.Height = Height;
	DebugDef.Strata = EWowFrameStrata::DIALOG;
	DebugDef.FrameLevel = 10;

	// Add anchor to position it
	FWowAnchor Anchor;
	Anchor.Point = EWowAnchorPoint::TOPLEFT;
	Anchor.RelativePoint = EWowAnchorPoint::TOPLEFT;
	Anchor.OffsetX = X;
	Anchor.OffsetY = Y;
	DebugDef.Anchors.Add(Anchor);

	// Add a backdrop for visual feedback
	FWowBackdrop Backdrop;
	// For now, create a debug frame without backdrop since we need texture files
	// A real implementation would load Interface/DialogFrame/UI-DialogBox-Background.blp

	// Add a text layer to show the frame name
	FWowLayer TextLayer;
	TextLayer.Level = EWowDrawLayer::OVERLAY;

	FWowFontStringElement DebugText;
	DebugText.Name = Name + TEXT("_Text");
	DebugText.Text = Name + FString::Printf(TEXT(" (%.0fx%.0f)"), Width, Height);
	DebugText.FontHeight = 12.f;
	DebugText.Color = FLinearColor::Yellow;
	DebugText.JustifyH = TEXT("CENTER");

	// Center the text in the frame
	FWowAnchor TextAnchor;
	TextAnchor.Point = EWowAnchorPoint::CENTER;
	TextAnchor.RelativePoint = EWowAnchorPoint::CENTER;
	DebugText.Anchors.Add(TextAnchor);

	TextLayer.FontStrings.Add(DebugText);
	DebugDef.Layers.Add(TextLayer);

	int64 Handle = CreateFrame(DebugDef);
	UE_LOG(LogWowFrame, Warning, TEXT("Created debug frame '%s' [%lld] at (%.0f,%.0f) size %.0fx%.0f"),
		*Name, Handle, X, Y, Width, Height);

	return Handle;
}

void FWowFrameManager::SetCharacterSheetRenderTarget(UTextureRenderTarget2D* RenderTarget)
{
	if (!RootCanvas.IsValid() || !RenderTarget)
	{
		return;
	}

	UImage* ModelImage = GetNamedTexture(TEXT("CharacterSheetOverlayModel"));
	if (!ModelImage)
	{
		ModelImage = NewObject<UImage>(RootCanvas.Get());
		RootCanvas->AddChildToCanvas(ModelImage);
		NamedTextureWidgets.Add(TEXT("CharacterSheetOverlayModel"), ModelImage);
	}

    // Authored character-local viewport, clear of equipment and resistance icons.
    FFrameRect ModelRect(65, 78, 210, 195);
    if (auto* Canvas = Cast<UCanvasPanel>(GetWidgetForHandle(FindFrame(TEXT("CharacterFrame")))))
        if (ModelImage->GetParent() != Canvas)
        { ModelImage->RemoveFromParent(); Canvas->AddChildToCanvas(ModelImage); }
    if (auto* Backdrop = GetNamedTexture(TEXT("CharacterSheetOverlayModelBackdrop")))
        Backdrop->SetVisibility(ESlateVisibility::Collapsed);

	if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ModelImage->Slot))
	{
		Slot->SetAnchors(FAnchors(0.0f, 0.0f));
		Slot->SetAlignment(FVector2D::ZeroVector);
		Slot->SetAutoSize(false);
        // Fit the captured image uniformly; filling a differently shaped XML
        // model frame squeezed tall races sideways regardless of camera fit.
        const FVector2D CaptureSize(RenderTarget->SizeX, RenderTarget->SizeY);
        const float Fit = FMath::Min(ModelRect.W / FMath::Max(1.0f, static_cast<float>(CaptureSize.X)),
            ModelRect.H / FMath::Max(1.0f, static_cast<float>(CaptureSize.Y)));
        const FVector2D DisplaySize = CaptureSize * Fit;
        const FVector2D DisplayPosition = FVector2D(ModelRect.X, ModelRect.Y)
            + (FVector2D(ModelRect.W, ModelRect.H) - DisplaySize) * 0.5f;
        Slot->SetPosition(DisplayPosition * UIScale);
        Slot->SetSize(DisplaySize * UIScale);
		Slot->SetZOrder(3975);
	}

	FSlateBrush Brush;
	auto* Material = Cast<UMaterialInstanceDynamic>(ModelImage->GetBrush().GetResourceObject());
    if (!Material)
        if (auto* Base = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_WowPaperDollUI.M_WowPaperDollUI")))
            Material = UMaterialInstanceDynamic::Create(Base, ModelImage);
    if (Material) Material->SetTextureParameterValue(TEXT("PortraitTexture"), RenderTarget);
    Brush.SetResourceObject(Material ? static_cast<UObject*>(Material) : static_cast<UObject*>(RenderTarget));
	Brush.ImageSize = FVector2D(RenderTarget->SizeX, RenderTarget->SizeY);
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.Tiling = ESlateBrushTileType::NoTile;
	ModelImage->SetBrush(Brush);
	ModelImage->SetColorAndOpacity(FLinearColor::White);
	ModelImage->SetRenderOpacity(1.0f);
	ModelImage->SetVisibility(IsFrameVisible(FindFrame(TEXT("CharacterFrame"))) && ActiveCharacterSheetTabIndex == 1
		? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

void FWowFrameManager::SetDressUpRenderTarget(UTextureRenderTarget2D* RenderTarget)
{
	const int64 DressUpModelHandle = FindFrame(TEXT("DressUpModel"));
	UCanvasPanel* DressUpCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(DressUpModelHandle));
	if (!RenderTarget || !DressUpCanvas) return;
	UImage* DressUpImage = GetNamedTexture(TEXT("DressUpModelPreview"));
	if (!DressUpImage)
	{
		DressUpImage = NewObject<UImage>(DressUpCanvas);
		DressUpCanvas->AddChildToCanvas(DressUpImage);
		NamedTextureWidgets.Add(TEXT("DressUpModelPreview"), DressUpImage);
		DressUpImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else if (DressUpImage->GetParent() != DressUpCanvas)
	{
		DressUpImage->RemoveFromParent();
		DressUpCanvas->AddChildToCanvas(DressUpImage);
	}
	// Uniform fit inside DressUpModel (316 x 331), centred, under the rotate buttons.
	const FFrameRect DressUpRect = GetFrameRect(TEXT("DressUpModel"));
	const float DressUpW = DressUpRect.W > 0.f ? DressUpRect.W : 316.f;
	const float DressUpH = DressUpRect.H > 0.f ? DressUpRect.H : 331.f;
	const FVector2D DressUpCapture(RenderTarget->SizeX, RenderTarget->SizeY);
	const float DressUpFit = FMath::Min(DressUpW / FMath::Max(1.0f, float(DressUpCapture.X)), DressUpH / FMath::Max(1.0f, float(DressUpCapture.Y)));
	const FVector2D DressUpSize = DressUpCapture * DressUpFit;
	if (UCanvasPanelSlot* DressUpSlot = Cast<UCanvasPanelSlot>(DressUpImage->Slot))
	{
		DressUpSlot->SetAnchors(FAnchors(0.0f, 0.0f));
		DressUpSlot->SetAlignment(FVector2D::ZeroVector);
		DressUpSlot->SetAutoSize(false);
		DressUpSlot->SetPosition((FVector2D(DressUpW, DressUpH) - DressUpSize) * 0.5f * UIScale);
		DressUpSlot->SetSize(DressUpSize * UIScale);
		DressUpSlot->SetZOrder(-10);
	}
	// The paper-doll UI material, like the character sheet (alpha from the capture).
	auto* DressUpMaterial = Cast<UMaterialInstanceDynamic>(DressUpImage->GetBrush().GetResourceObject());
	if (!DressUpMaterial)
		if (auto* DressUpBase = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_WowPaperDollUI.M_WowPaperDollUI")))
			DressUpMaterial = UMaterialInstanceDynamic::Create(DressUpBase, DressUpImage);
	if (DressUpMaterial) DressUpMaterial->SetTextureParameterValue(TEXT("PortraitTexture"), RenderTarget);
	FSlateBrush DressUpBrush;
	DressUpBrush.SetResourceObject(DressUpMaterial ? static_cast<UObject*>(DressUpMaterial) : static_cast<UObject*>(RenderTarget));
	DressUpBrush.ImageSize = DressUpCapture;
	DressUpBrush.DrawAs = ESlateBrushDrawType::Image;
	DressUpBrush.Tiling = ESlateBrushTileType::NoTile;
	DressUpImage->SetBrush(DressUpBrush);
	DressUpImage->SetColorAndOpacity(FLinearColor::White);
	DressUpImage->SetRenderOpacity(1.0f);
}

void FWowFrameManager::EnsureNativeCharacterSheetTabs()
{
	if (!RootCanvas.IsValid())
	{
		return;
	}

	const int64 CharacterFrameHandle = FindFrame(TEXT("CharacterFrame"));
	const FFrameRect CharacterRect = GetFrameRect(TEXT("CharacterFrame"));
	if (CharacterFrameHandle < 0 || CharacterRect.W <= 0.0f || CharacterRect.H <= 0.0f)
	{
		return;
	}

	// FrameXML creates these at width 10 and relies on
	// PanelTemplates_TabResize() in Lua to expand the three texture slices around
	// the localized caption. Complete that stock layout step here instead of
	// replacing the original 3.3.5 artwork.
	static const TCHAR* TabLabels[] = {TEXT("Character"), TEXT("Reputation"), TEXT("Skills")};
	static const float TabWidths[] = {104.0f, 112.0f, 82.0f};
	const float LocalTabY = CharacterRect.H - 83.0f;
	float LocalX = 10.0f;
	for (int32 TabIndex = 1; TabIndex <= UE_ARRAY_COUNT(TabLabels); ++TabIndex)
	{
		const FString TabName = FString::Printf(TEXT("CharacterFrameTab%d"), TabIndex);
		const int64 TabHandle = FindFrame(TabName);
		FFrameEntry* TabEntry = Frames.Find(TabHandle);
		UCanvasPanel* TabCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(TabHandle));
		if (!TabEntry || !TabCanvas)
		{
			LocalX += TabWidths[TabIndex - 1] - 6.0f;
			continue;
		}

		const float TabWidth = TabWidths[TabIndex - 1];
		TabEntry->Def.HitRectInsets = FMargin(0);
        TabEntry->Def.FrameLevel = 130;
        TabEntry->Def.Width = TabWidth;
		TabEntry->Def.Height = 32.0f;
		if (UCanvasPanelSlot* TabSlot = Cast<UCanvasPanelSlot>(TabCanvas->Slot))
		{
			const bool bRootOwned = TabCanvas->GetParent() == RootCanvas.Get();
			TabSlot->SetAnchors(FAnchors(0.0f, 0.0f));
			TabSlot->SetAlignment(FVector2D::ZeroVector);
			TabSlot->SetAutoSize(false);
			TabSlot->SetPosition((bRootOwned
				? FVector2D(CharacterRect.X + LocalX, CharacterRect.Y + LocalTabY)
				: FVector2D(LocalX, LocalTabY)) * UIScale);
			TabSlot->SetSize(FVector2D(TabWidth, 32.0f) * UIScale);
			TabSlot->SetZOrder(3998 + TabIndex);
		}
		FrameRects.Add(TabName, FFrameRect(
			CharacterRect.X + LocalX, CharacterRect.Y + LocalTabY,
			TabWidth, 32.0f));

		auto LayoutSlice = [this](const FString& TextureName,
			float X, float Y, float Width)
		{
			if (UImage* Slice = GetNamedTexture(TextureName))
			{
				if (UCanvasPanelSlot* SliceSlot = Cast<UCanvasPanelSlot>(Slice->Slot))
				{
					SliceSlot->SetAnchors(FAnchors(0.0f, 0.0f));
					SliceSlot->SetAlignment(FVector2D::ZeroVector);
					SliceSlot->SetAutoSize(false);
					SliceSlot->SetPosition(FVector2D(X, Y) * UIScale);
					SliceSlot->SetSize(FVector2D(Width, 32.0f) * UIScale);
					SliceSlot->SetZOrder(20);
				}
			}
		};
		LayoutSlice(TabName + TEXT("LeftDisabled"), 0.0f, 0.0f, 20.0f);
		LayoutSlice(TabName + TEXT("MiddleDisabled"), 20.0f, 0.0f,
			FMath::Max(1.0f, TabWidth - 40.0f));
		LayoutSlice(TabName + TEXT("RightDisabled"), TabWidth - 20.0f, 0.0f, 20.0f);
		LayoutSlice(TabName + TEXT("Left"), 0.0f, 5.0f, 20.0f);
		LayoutSlice(TabName + TEXT("Middle"), 20.0f, 5.0f,
			FMath::Max(1.0f, TabWidth - 40.0f));
		LayoutSlice(TabName + TEXT("Right"), TabWidth - 20.0f, 5.0f, 20.0f);

		const FString LabelName = FString::Printf(TEXT("CharacterSheetTabLabel%d"), TabIndex);
		UTextBlock* Label = GetNamedFontString(LabelName);
		if (Label && Label->GetParent() != TabCanvas)
		{
			if (UPanelWidget* PreviousParent = Label->GetParent())
			{
				PreviousParent->RemoveChild(Label);
			}
			TabCanvas->AddChildToCanvas(Label);
		}
		if (!Label)
		{
			Label = NewObject<UTextBlock>(TabCanvas);
			TabCanvas->AddChildToCanvas(Label);
			NamedFontStringWidgets.Add(LabelName, Label);
		}
		if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(Label->Slot))
		{
			LabelSlot->SetAnchors(FAnchors(0.0f, 0.0f));
			LabelSlot->SetAlignment(FVector2D::ZeroVector);
			LabelSlot->SetAutoSize(false);
			LabelSlot->SetPosition(FVector2D(4.0f, 5.0f) * UIScale);
			LabelSlot->SetSize(FVector2D(TabWidth - 8.0f, 18.0f) * UIScale);
			LabelSlot->SetZOrder(250);
		}
		Label->SetText(FText::FromString(TabLabels[TabIndex - 1]));
		Label->SetJustification(ETextJustify::Center);
		Label->SetShadowOffset(FVector2D(1.0f));
		Label->SetShadowColorAndOpacity(FLinearColor::Black);
		Label->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FMath::Max(9, FMath::RoundToInt(10.0f * UIScale));
		Label->SetFont(Font);

		SetFrameMouseEnabled(TabHandle, true);
		SetFrameRegisteredClicks(TabHandle, {TEXT("LeftButtonUp")});
		LocalX += TabWidth - 6.0f;
	}

	// Currency and pet are separate unfinished features. Keep only the requested
	// Character/Reputation/Skills set visible for now.
	for (int32 TabIndex = 4; TabIndex <= 8; ++TabIndex)
	{
		SetFrameVisible(FindFrame(FString::Printf(TEXT("CharacterFrameTab%d"), TabIndex)), false);
	}
}

void FWowFrameManager::RefreshNativeCharacterSheetTabs()
{
	const bool bCharacterVisible = IsFrameVisible(FindFrame(TEXT("CharacterFrame")));
	ActiveCharacterSheetTabIndex = FMath::Clamp(ActiveCharacterSheetTabIndex, 1, 3);

	for (int32 TabIndex = 1; TabIndex <= 3; ++TabIndex)
	{
		const FString TabName = FString::Printf(TEXT("CharacterFrameTab%d"), TabIndex);
		const int64 TabHandle = FindFrame(TabName);
		if (FFrameEntry* TabEntry = Frames.Find(TabHandle))
		{
			TabEntry->Def.bHidden = !bCharacterVisible;
			if (TabEntry->Widget.IsValid())
			{
				TabEntry->Widget->SetVisibility(bCharacterVisible
					? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			}
		}

		const bool bSelected = TabIndex == ActiveCharacterSheetTabIndex;
		for (const TCHAR* Suffix : {TEXT("LeftDisabled"), TEXT("MiddleDisabled"), TEXT("RightDisabled")})
		{
			if (UImage* Slice = GetNamedTexture(TabName + Suffix))
			{
				Slice->SetVisibility(bSelected && bCharacterVisible
					? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
		for (const TCHAR* Suffix : {TEXT("Left"), TEXT("Middle"), TEXT("Right")})
		{
			if (UImage* Slice = GetNamedTexture(TabName + Suffix))
			{
				Slice->SetVisibility(!bSelected && bCharacterVisible
					? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
		if (UTextBlock* Label = GetNamedFontString(
			FString::Printf(TEXT("CharacterSheetTabLabel%d"), TabIndex)))
		{
			Label->SetColorAndOpacity(FSlateColor(bSelected
				? WowColor::FromWow(1.0f, 0.95f, 0.78f, 1.0f)
				: WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f)));
			// The text stays put; only the art changes (active art is drawn 5 higher).
			Label->SetRenderTranslation(FVector2D(0.0f, 5.0f) * UIScale);
			Label->SetVisibility(bCharacterVisible
				? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
		// r246 (Stefan 12.10: "CHARACTER", "PETS", "REPUTATION" in a large font under the panel):
		// the tabs' own XML captions (Tab2 is Pets in FrameXML) stayed visible next to the native
		// labels above, which carry the Character/Reputation/Skills captions. Keep them hidden.
		const UTextBlock* NativeLabel = GetNamedFontString(FString::Printf(TEXT("CharacterSheetTabLabel%d"), TabIndex));
		for (UTextBlock* OwnCaption : {GetPrimaryTextWidget(TabHandle), GetNamedFontString(TabName + TEXT("Text"))})
		{
			if (OwnCaption && OwnCaption != NativeLabel && OwnCaption->GetVisibility() != ESlateVisibility::Collapsed)
			{
				OwnCaption->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	const bool bPaperDollPage = bCharacterVisible && ActiveCharacterSheetTabIndex == 1;
	for (auto& Pair : NamedFontStringWidgets)
	{
		const bool bPaperDollOverlay =
			Pair.Key.StartsWith(TEXT("CharacterSheetOverlayStat")) ||
			Pair.Key.Equals(TEXT("CharacterSheetOverlayLeftCategory")) ||
			Pair.Key.Equals(TEXT("CharacterSheetOverlayRightCategory"));
		if (bPaperDollOverlay && Pair.Value.IsValid())
		{
			Pair.Value->SetVisibility(bPaperDollPage
				? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
	for (const TCHAR* Name : {TEXT("CharacterSheetOverlayModel")})
	if (UImage* Model = GetNamedTexture(Name))
	{
		Model->SetVisibility(bPaperDollPage
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	RefreshNativeCharacterStatControls();
	RefreshNativeCharacterTitleControl();
    LayoutCharacterSheetRegions();
}

void FWowFrameManager::LayoutCharacterSheetRegions()
{
    auto* Canvas = Cast<UCanvasPanel>(GetWidgetForHandle(FindFrame(TEXT("CharacterFrame"))));
    if (!Canvas) return;
    const bool bOpen = IsFrameVisible(FindFrame(TEXT("CharacterFrame")));
    const bool bDoll = bOpen && ActiveCharacterSheetTabIndex == 1;
    auto Place = [this, Canvas](UWidget* Widget, FVector2D Position, FVector2D Size, int32 Z, bool Show)
    {
        if (!Widget) return;
        if (Widget->GetParent() != Canvas)
        { Widget->RemoveFromParent(); Canvas->AddChildToCanvas(Widget); }
        Widget->SetRenderTranslation(FVector2D::ZeroVector);
        if (auto* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
        {
            Slot->SetAnchors(FAnchors(0,0)); Slot->SetAlignment(FVector2D::ZeroVector);
            Slot->SetAutoSize(false); Slot->SetPosition(Position * UIScale);
            Slot->SetSize(Size * UIScale); Slot->SetZOrder(Z);
        }
        Widget->SetVisibility(Show ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    };
    Place(GetNamedFontString(TEXT("CharacterSheetOverlayName")), {84,17}, {214,13}, 32000, bOpen);
    Place(GetNamedFontString(TEXT("CharacterSheetOverlayLevel")), {84,40}, {214,14}, 32000, bOpen);
    Place(GetNamedTexture(TEXT("CharacterSheetOverlayPortrait")), {7,6}, {60,60}, 1, bOpen);
    for (const TCHAR* Name : {TEXT("CharacterNameText"), TEXT("CharacterNameFrameText"), TEXT("CharacterLevelText")})
        if (auto* Text = GetNamedFontString(Name)) { Text->SetText(FText::GetEmpty()); Text->SetVisibility(ESlateVisibility::Collapsed); }
    // Stefan 11.10: centre the stat text in the panels. Each panel is 115 x 85 (Top 291-307,
    // Middle 307-360, Bottom 360-376): six 13-unit rows centred vertically (3.5 above and below),
    // the label 7 in from the left edge and the value right-aligned 7 in from the right edge.
    // Inside each row the line is centred on its measured height.
    constexpr float StatPanelX = 67.f, StatPanelY = 291.f, StatPanelW = 115.f, StatPanelH = 85.f;
    constexpr float StatRowH = 13.f, StatInset = 7.f, StatValueW = 30.f;
    const float StatTop = StatPanelY + (StatPanelH - 6.f * StatRowH) * .5f;
    for (int32 Row=0; Row<12; ++Row)
    {
        const float PanelX = StatPanelX + (Row<6?0.f:StatPanelW);
        float LineOffset = 0.f;
        UTextBlock* StatLabel = GetNamedFontString(FString::Printf(TEXT("CharacterSheetOverlayStatLabel%d"),Row));
        UTextBlock* StatValue = GetNamedFontString(FString::Printf(TEXT("CharacterSheetOverlayStatValue%d"),Row));
        if (StatLabel && FSlateApplication::IsInitialized() && UIScale > 0.f)
        {
            const float LineH = static_cast<float>(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()
                ->GetMaxCharacterHeight(StatLabel->GetFont())) / UIScale;
            LineOffset = FMath::Max(0.f, (StatRowH - LineH) * .5f);
        }
        const float Y = StatTop + (Row%6)*StatRowH + LineOffset;
        const float LabelW = StatPanelW - 2.f*StatInset - StatValueW;
        Place(StatLabel, {PanelX+StatInset,Y}, {LabelW,StatRowH}, 32000,bDoll);
        Place(StatValue, {PanelX+StatInset+LabelW,Y}, {StatValueW,StatRowH},32000,bDoll);
        if (StatLabel) StatLabel->SetJustification(ETextJustify::Left);
        if (StatValue) StatValue->SetJustification(ETextJustify::Right);
    }
    for (int32 Column=0;Column<2;++Column)
    {
        const FString Prefix=Column==0?TEXT("PlayerStatLeft"):TEXT("PlayerStatRight");
        const float X=67.f+Column*115.f;
        Place(GetNamedTexture(Prefix+TEXT("Top")),{X,291},{115,16},31900,bDoll);
        Place(GetNamedTexture(Prefix+TEXT("Middle")),{X,307},{115,53},31900,bDoll);
        Place(GetNamedTexture(Prefix+TEXT("Bottom")),{X,360},{115,16},31900,bDoll);
    }
    // The button and popup share the window's transform and lifetime.
    auto PlaceControl = [&](const FString& Name, FVector2D P, FVector2D Size, int32 Z, bool Show)
    {
        const int64 H=FindFrame(Name);
        if (auto* Entry=Frames.Find(H)) { Entry->Def.Parent=TEXT("CharacterFrame"); Entry->ParentHandle=FindFrame(TEXT("CharacterFrame")); Entry->Def.HitRectInsets=FMargin(0); }
        Place(GetWidgetForHandle(H),P,Size,Z,Show);
    };
    // Both panels: the same dropdown over the panel, its five-page menu dropping over the stats.
    for (int32 Column=0;Column<2;++Column)
    {
        const TCHAR* Side=Column==0?TEXT("Left"):TEXT("Right");
        const float DropX=68.f+Column*115.f;
        const bool bMenu=bDoll&&(Column==0?bCharacterLeftStatMenuOpen:bCharacterRightStatMenuOpen);
        const FString DropName=FString::Printf(TEXT("CharacterNative%sStatDropDown"),Side);
        PlaceControl(DropName,{DropX,271},{112,20},32001,bDoll);
        if (auto* Label=GetPrimaryTextWidget(FindFrame(DropName)))
        { auto Font=Label->GetFont(); Font.Size=FMath::Max(8,FMath::RoundToInt(8*UIScale)); Label->SetFont(Font); }
        Place(GetNamedTexture(FString::Printf(TEXT("CharacterNative%sStatBackground"),Side)),{DropX,292},{112,5*17.f+6.f},33000,bMenu);
        for(int32 I=0;I<5;++I)
            PlaceControl(FString::Printf(TEXT("CharacterNative%sStatRow%d"),Side,I+1),{DropX+3,295.f+I*17},{106,17},33001+I,bMenu);
    }
    // Suppress the legacy dropdown artwork; the native button owns the caption.
    SetFrameVisible(FindFrame(TEXT("PlayerStatFrameLeftDropDown")),false);
    SetFrameVisible(FindFrame(TEXT("PlayerStatFrameRightDropDown")),false);
    if (auto* Backdrop=GetNamedTexture(TEXT("CharacterSheetOverlayModelBackdrop"))) Backdrop->SetVisibility(ESlateVisibility::Collapsed);
}

void FWowFrameManager::SetActiveCharacterSheetTab(int32 TabIndex)
{
    // r341 (Stefan 14.10: the skills window belongs in the character sheet's tabs): tab 3 shows the
    // stock SkillFrame (LuaSkillApi.cpp) instead of opening the separate native skills window.
	ActiveCharacterSheetTabIndex = FMath::Clamp(TabIndex, 1, 3);
	bCharacterRightStatMenuOpen = false;
	bCharacterLeftStatMenuOpen = false;
	bCharacterTitleMenuOpen = false;

	static const TCHAR* PageNames[] = {
		TEXT("PaperDollFrame"), TEXT("ReputationFrame"), TEXT("SkillFrame")
	};
	const bool bCharacterVisible = IsFrameVisible(FindFrame(TEXT("CharacterFrame")));
	for (int32 PageIndex = 0; PageIndex < UE_ARRAY_COUNT(PageNames); ++PageIndex)
	{
		const int64 PageHandle = FindFrame(PageNames[PageIndex]);
		if (PageHandle < 0)
		{
			continue;
		}
		if (bCharacterVisible)
		{
			// r346 (Stefan 14.10: the Reputation tab showed 15 empty rows, all "Revered" - the XML's
			// sample text): the native page switch never ran the page's OnShow, so
			// ReputationFrame_Update / SkillFrame_UpdateSkills never filled the rows. Run OnShow /
			// OnHide like frame:Show() / frame:Hide() do. The paper doll (page 1) keeps its native
			// refresh (UpdateCharacterSheet); its stock OnShow would overwrite those texts.
			const bool bShowPage = PageIndex + 1 == ActiveCharacterSheetTabIndex;
			const bool bWasShown = IsFrameVisible(PageHandle);
			SetFrameVisible(PageHandle, bShowPage);
			if (PageIndex == 0)
			{
			}
			else if (EventSystem && bShowPage != bWasShown)
			{
				EventSystem->RunFrameScript(PageHandle, bShowPage ? TEXT("OnShow") : TEXT("OnHide"));
			}
			else if (EventSystem && bShowPage)
			{
				EventSystem->RunFrameScript(PageHandle, TEXT("OnShow")); // reopened on the same tab: refresh
			}
		}
		else if (FFrameEntry* PageEntry = Frames.Find(PageHandle))
		{
			PageEntry->Def.bHidden = PageIndex + 1 != ActiveCharacterSheetTabIndex;
			if (PageEntry->Widget.IsValid())
			{
				PageEntry->Widget->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	RefreshNativeCharacterSheetTabs();
	UE_LOG(LogWowFrame, Log, TEXT("Character sheet tab selected: %d"), ActiveCharacterSheetTabIndex);
}

void FWowFrameManager::EnsureNativeCharacterStatControls()
{
	if (!RootCanvas.IsValid())
	{
		return;
	}

	// Stefan 11.10: the left and right stat panels get the same dropdown with the same five pages.
	static const TCHAR* PageNames[] = {
		TEXT("Base Stats"), TEXT("Melee"), TEXT("Ranged"), TEXT("Spell"), TEXT("Defense")
	};
	for (const TCHAR* Side : {TEXT("Left"), TEXT("Right")})
	{
		const bool bLeftSide = FCString::Strcmp(Side, TEXT("Left")) == 0;
		for (int32 PageIndex = 0; PageIndex < UE_ARRAY_COUNT(PageNames); ++PageIndex)
		{
			const FString RowName = FString::Printf(
				TEXT("CharacterNative%sStatRow%d"), Side, PageIndex + 1);
			if (FindFrame(RowName) < 0)
			{
				FWowFrameDef RowDef;
				RowDef.Name = RowName;
				RowDef.Type = EWowFrameType::Button;
				RowDef.Width = 104.0f;
				RowDef.Height = 17.0f;
				RowDef.Strata = EWowFrameStrata::DIALOG;
				RowDef.FrameLevel = 160 + PageIndex;
				RowDef.ButtonText = PageNames[PageIndex];
				CreateFrame(RowDef);
			}

			const int64 RowHandle = FindFrame(RowName);
			SetFrameVisible(RowHandle, false);
			SetFrameMouseEnabled(RowHandle, true);
			SetFrameRegisteredClicks(RowHandle, {TEXT("LeftButtonUp")});
			if (UTextBlock* Label = GetPrimaryTextWidget(RowHandle))
			{
				FSlateFontInfo Font = Label->GetFont();
				Font.Size = FMath::Max(8, FMath::RoundToInt(8.0f * UIScale));
				Label->SetFont(Font);
				Label->SetJustification(ETextJustify::Left);
				Label->SetShadowOffset(FVector2D(1.0f));
				Label->SetShadowColorAndOpacity(FLinearColor::Black);
			}
		}

		const FString BackgroundName = FString::Printf(TEXT("CharacterNative%sStatBackground"), Side);
		UImage* MenuBackground = GetNamedTexture(BackgroundName);
		if (!MenuBackground)
		{
			MenuBackground = NewObject<UImage>(RootCanvas.Get());
			RootCanvas->AddChildToCanvas(MenuBackground);
			NamedTextureWidgets.Add(BackgroundName, MenuBackground);
		}
		MenuBackground->SetBrush(FSlateRoundedBoxBrush(
			FWowWindowStyle::ChatBackgroundColor(), 2.0f * UIScale,
			WowColor::FromWow(0.48f, 0.48f, 0.52f, 0.98f), 1.0f * UIScale));
		MenuBackground->SetVisibility(ESlateVisibility::Collapsed);

		const FString NativeDropDownName = FString::Printf(TEXT("CharacterNative%sStatDropDown"), Side);
		if (FindFrame(NativeDropDownName) < 0)
		{
			FWowFrameDef ButtonDef;
			ButtonDef.Name = NativeDropDownName;
			ButtonDef.Type = EWowFrameType::Button;
			ButtonDef.Width = 100.0f;
			ButtonDef.Height = 22.0f;
			ButtonDef.Strata = EWowFrameStrata::DIALOG;
			ButtonDef.FrameLevel = 149;
			ButtonDef.ButtonText = bLeftSide ? TEXT("Base Stats") : TEXT("Melee");
			CreateFrame(ButtonDef);
		}
		const int64 NativeDropDownHandle = FindFrame(NativeDropDownName);
		SetFrameMouseEnabled(NativeDropDownHandle, true);
		if (UButton* Button = GetButtonWidget(NativeDropDownHandle))
		{
			// Drawn like UIDropDownMenuTemplate (label frame + arrow button), not a red button.
			FButtonStyle Style = Button->GetStyle();
			FSlateBrush None; None.DrawAs = ESlateBrushDrawType::NoDrawType;
			Style.Normal = None; Style.Hovered = None; Style.Pressed = None; Style.Disabled = None;
			Style.NormalPadding = FMargin(0); Style.PressedPadding = FMargin(0);
			Button->SetStyle(Style);
			Button->SetBackgroundColor(FLinearColor::White);
		}
		if (UCanvasPanel* DropCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(NativeDropDownHandle)))
		{
			// UIDropDownMenuTemplate on a 112x20 control: CharacterCreate-LabelFrame caps (25x64,
			// drawn 22 above the control) and the UI-ChatIcon-ScrollDown arrow at the right.
			struct FDropPiece { const TCHAR* Suffix; const TCHAR* Path; float X, Y, W, H, U0, U1; };
			const FDropPiece Pieces[] = {
				{TEXT("Left"), TEXT("Interface\\Glues\\CharacterCreate\\CharacterCreate-LabelFrame"), -8.f, -22.f, 25.f, 64.f, 0.f, .1953125f},
				{TEXT("Middle"), TEXT("Interface\\Glues\\CharacterCreate\\CharacterCreate-LabelFrame"), 17.f, -22.f, 78.f, 64.f, .1953125f, .8046875f},
				{TEXT("Right"), TEXT("Interface\\Glues\\CharacterCreate\\CharacterCreate-LabelFrame"), 95.f, -22.f, 25.f, 64.f, .8046875f, 1.f},
				{TEXT("Arrow"), TEXT("Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Up"), 81.f, -2.f, 24.f, 24.f, 0.f, 1.f},
			};
			for (const FDropPiece& Piece : Pieces)
			{
				// The right side keeps its original piece names.
				const FString PieceName = bLeftSide
					? FString::Printf(TEXT("CharacterNativeLeftStatDrop%s"), Piece.Suffix)
					: FString::Printf(TEXT("CharacterNativeStatDrop%s"), Piece.Suffix);
				UImage* Image = GetNamedTexture(PieceName);
				if (!Image)
				{
					Image = NewObject<UImage>(DropCanvas);
					DropCanvas->AddChildToCanvas(Image);
					NamedTextureWidgets.Add(PieceName, Image);
					Image->SetVisibility(ESlateVisibility::HitTestInvisible);
				}
				FSlateBrush Brush;
				if (UTexture2D* Texture = LoadUITexture(Piece.Path)) Brush.SetResourceObject(Texture);
				else Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
				Brush.SetUVRegion(FBox2f(FVector2f(Piece.U0, 0.f), FVector2f(Piece.U1, 1.f)));
				Brush.ImageSize = FVector2D(Piece.W, Piece.H) * UIScale;
				Image->SetBrush(Brush);
				if (UCanvasPanelSlot* PieceSlot = Cast<UCanvasPanelSlot>(Image->Slot))
				{
					PieceSlot->SetAnchors(FAnchors(0.f, 0.f));
					PieceSlot->SetAlignment(FVector2D::ZeroVector);
					PieceSlot->SetAutoSize(false);
					PieceSlot->SetPosition(FVector2D(Piece.X, Piece.Y) * UIScale);
					PieceSlot->SetSize(FVector2D(Piece.W, Piece.H) * UIScale);
					PieceSlot->SetZOrder(Piece.H < 30.f ? 12 : 1); // arrow above the frame caps
				}
			}
		}
		SetFrameRegisteredClicks(NativeDropDownHandle, {TEXT("LeftButtonUp")});
		if (UTextBlock* Label = GetPrimaryTextWidget(NativeDropDownHandle))
		{
			// GameFontHighlightSmall, right-justified next to the arrow (RIGHT of the right cap -43,2).
			Label->SetJustification(ETextJustify::Right);
			if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(Label->Slot))
			{
				LabelSlot->SetAnchors(FAnchors(0.f, 0.f));
				LabelSlot->SetAlignment(FVector2D::ZeroVector);
				LabelSlot->SetAutoSize(false);
				LabelSlot->SetPosition(FVector2D(10.f, 2.f) * UIScale);
				LabelSlot->SetSize(FVector2D(67.f, 16.f) * UIScale);
				LabelSlot->SetZOrder(11);
			}
			Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Label->SetShadowOffset(FVector2D(1.0f));
			Label->SetShadowColorAndOpacity(FLinearColor::Black);
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = FMath::Max(9, FMath::RoundToInt(10.0f * UIScale));
			Label->SetFont(Font);
		}

		const int64 DropDownHandle = FindFrame(FString::Printf(TEXT("PlayerStatFrame%sDropDown"), Side));
		if (DropDownHandle >= 0)
		{
			SetFrameVisible(DropDownHandle, false);
		}
	}
	RefreshNativeCharacterStatControls();
}

void FWowFrameManager::EnsureNativeCharacterTitleControl()
{
	if (!RootCanvas.IsValid())
	{
		return;
	}

	const FString RowName = TEXT("CharacterNativeTitleNoneRow");
	if (FindFrame(RowName) < 0)
	{
		FWowFrameDef RowDef;
		RowDef.Name = RowName;
		RowDef.Type = EWowFrameType::Button;
		RowDef.Width = 184.0f;
		RowDef.Height = 18.0f;
		RowDef.Strata = EWowFrameStrata::DIALOG;
		RowDef.FrameLevel = 180;
		RowDef.ButtonText = TEXT("No Title");
		CreateFrame(RowDef);
	}

	const int64 RowHandle = FindFrame(RowName);
	SetFrameVisible(RowHandle, false);
	SetFrameMouseEnabled(RowHandle, true);
	SetFrameRegisteredClicks(RowHandle, {TEXT("LeftButtonUp")});
	if (UTextBlock* Label = GetPrimaryTextWidget(RowHandle))
	{
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FMath::Max(7, FMath::RoundToInt(7.0f * UIScale));
		Label->SetFont(Font);
		Label->SetJustification(ETextJustify::Left);
		Label->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.95f, 0.88f, 0.68f, 1.0f)));
		Label->SetShadowOffset(FVector2D(1.0f));
		Label->SetShadowColorAndOpacity(FLinearColor::Black);
	}

	UImage* MenuBackground = GetNamedTexture(TEXT("CharacterNativeTitleBackground"));
	if (!MenuBackground)
	{
		MenuBackground = NewObject<UImage>(RootCanvas.Get());
		RootCanvas->AddChildToCanvas(MenuBackground);
		NamedTextureWidgets.Add(TEXT("CharacterNativeTitleBackground"), MenuBackground);
	}
	MenuBackground->SetBrush(FSlateRoundedBoxBrush(
		FWowWindowStyle::ChatBackgroundColor(), 2.0f * UIScale,
		WowColor::FromWow(0.48f, 0.48f, 0.52f, 0.98f), 1.0f * UIScale));
	MenuBackground->SetVisibility(ESlateVisibility::Collapsed);

	for (const TCHAR* FrameName : {TEXT("PlayerTitleFrame"), TEXT("PlayerTitleFrameButton")})
	{
		const int64 Handle = FindFrame(FrameName);
		if (Handle < 0)
		{
			continue;
		}
		TArray<int64> PendingHandles = {Handle};
		while (!PendingHandles.IsEmpty())
		{
			const int64 PendingHandle = PendingHandles.Pop(EAllowShrinking::No);
			SetFrameMouseEnabled(PendingHandle, true);
			SetFrameRegisteredClicks(PendingHandle, {TEXT("LeftButtonUp")});
			PendingHandles.Append(GetChildHandles(PendingHandle));
		}
	}
	RefreshNativeCharacterTitleControl();
}

void FWowFrameManager::RefreshNativeCharacterTitleControl()
{
	if (!RootCanvas.IsValid())
	{
		return;
	}

	// The 3.3.5 title selector is absent until the character owns a title.
	// Title data is not exposed by the current entity snapshot, so keep both the
	// selector and its native fallback menu hidden instead of showing a fake row.
	bCharacterTitleMenuOpen = false;
	SetFrameVisible(FindFrame(TEXT("PlayerTitleFrame")), false);
	SetFrameVisible(FindFrame(TEXT("PlayerTitleFrameButton")), false);
	const bool bShowMenu = false;
	const FFrameRect CharacterRect = GetFrameRect(TEXT("CharacterFrame"));
	const FVector2D MenuPosition(CharacterRect.X + 94.0f, CharacterRect.Y + 58.0f);
	constexpr float MenuWidth = 190.0f;
	constexpr float MenuHeight = 24.0f;
	constexpr float MenuPadding = 3.0f;

	if (UImage* Background = GetNamedTexture(TEXT("CharacterNativeTitleBackground")))
	{
		Background->SetVisibility(bShowMenu
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Background->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition(MenuPosition * UIScale);
			Slot->SetSize(FVector2D(MenuWidth, MenuHeight) * UIScale);
			Slot->SetZOrder(static_cast<int32>(EWowFrameStrata::DIALOG) * 1000 + 180);
		}
	}

	const int64 RowHandle = FindFrame(TEXT("CharacterNativeTitleNoneRow"));
	SetFrameVisible(RowHandle, bShowMenu);
	if (UWidget* RowWidget = GetWidgetForHandle(RowHandle))
	{
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(RowWidget->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition((MenuPosition + FVector2D(MenuPadding, MenuPadding)) * UIScale);
			Slot->SetSize(FVector2D(MenuWidth - MenuPadding * 2.0f,
				MenuHeight - MenuPadding * 2.0f) * UIScale);
			Slot->SetZOrder(static_cast<int32>(EWowFrameStrata::DIALOG) * 1000 + 181);
		}
	}
}

void FWowFrameManager::RefreshNativeCharacterStatControls()
{
	if (!RootCanvas.IsValid())
	{
		return;
	}

	static const TCHAR* PageNames[] = {
		TEXT("Base Stats"), TEXT("Melee"), TEXT("Ranged"), TEXT("Spell"), TEXT("Defense")
	};
	CharacterLeftStatPage = FMath::Clamp(CharacterLeftStatPage, 0, UE_ARRAY_COUNT(PageNames) - 1);
	CharacterRightStatPage = FMath::Clamp(CharacterRightStatPage, 0, UE_ARRAY_COUNT(PageNames) - 1);
	const bool bCharacterVisible = IsFrameVisible(FindFrame(TEXT("CharacterFrame")));
	const bool bPaperDollPage = bCharacterVisible && ActiveCharacterSheetTabIndex == 1;
	// Positions come from LayoutCharacterSheetRegions; this sets captions, visibility and the rows.
	for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		const TCHAR* Side = SideIndex == 0 ? TEXT("Left") : TEXT("Right");
		const int32 Page = SideIndex == 0 ? CharacterLeftStatPage : CharacterRightStatPage;
		const bool bShowMenu = bPaperDollPage &&
			(SideIndex == 0 ? bCharacterLeftStatMenuOpen : bCharacterRightStatMenuOpen);
		const int64 NativeDropDownHandle = FindFrame(FString::Printf(TEXT("CharacterNative%sStatDropDown"), Side));
		SetFrameVisible(NativeDropDownHandle, bPaperDollPage);
		if (UTextBlock* Label = GetPrimaryTextWidget(NativeDropDownHandle))
		{
			Label->SetText(FText::FromString(FString(PageNames[Page])));
		}
		if (UImage* Background = GetNamedTexture(FString::Printf(TEXT("CharacterNative%sStatBackground"), Side)))
		{
			Background->SetVisibility(bShowMenu
				? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
		for (int32 PageIndex = 0; PageIndex < UE_ARRAY_COUNT(PageNames); ++PageIndex)
		{
			const int64 RowHandle = FindFrame(FString::Printf(
				TEXT("CharacterNative%sStatRow%d"), Side, PageIndex + 1));
			if (RowHandle < 0)
			{
				continue;
			}
			SetFrameVisible(RowHandle, bShowMenu);
			if (UTextBlock* Label = GetPrimaryTextWidget(RowHandle))
			{
				Label->SetText(FText::FromString(FString::Printf(TEXT("%s%s"),
					PageIndex == Page ? TEXT(">  ") : TEXT("   "), PageNames[PageIndex])));
				Label->SetColorAndOpacity(FSlateColor(PageIndex == Page
					? WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f) : FLinearColor::White));
			}
		}
		if (UTextBlock* Category = GetNamedFontString(FString::Printf(TEXT("CharacterSheetOverlay%sCategory"), Side)))
		{
			Category->SetVisibility(ESlateVisibility::Collapsed); // Caption is inside the dropdown button.
		}
		for (int32 RowIndex = 0; RowIndex < 6; ++RowIndex)
		{
			const FString StatName = CharacterRightStatLabels[Page].IsValidIndex(RowIndex)
				? CharacterRightStatLabels[Page][RowIndex] : FString();
			const int32 StatNumber = CharacterRightStatValues[Page].IsValidIndex(RowIndex)
				? CharacterRightStatValues[Page][RowIndex] : 0;
			if (UTextBlock* StatLabel = GetNamedFontString(FString::Printf(
				TEXT("CharacterSheetOverlayStatLabel%d"), RowIndex + SideIndex * 6)))
			{
				StatLabel->SetText(FText::FromString(StatName.IsEmpty() ? FString() : StatName + TEXT(":")));
			}
			if (UTextBlock* StatValue = GetNamedFontString(FString::Printf(
				TEXT("CharacterSheetOverlayStatValue%d"), RowIndex + SideIndex * 6)))
			{
				StatValue->SetText(StatName.IsEmpty() ? FText::GetEmpty() : FText::AsNumber(StatNumber));
			}
		}
	}
	LayoutCharacterSheetRegions();
}

void FWowFrameManager::EnsureNativeMinimapControls()
{
	if (!RootCanvas.IsValid())
	{
		return;
	}

	static const TCHAR* TrackingLabels[] = {
		TEXT("Repair"), TEXT("Food & Drink"), TEXT("Reagents"), TEXT("Innkeeper"),
		TEXT("Flight Master"), TEXT("Class Trainer"), TEXT("Profession Trainers"),
		TEXT("Auctioneer"), TEXT("Banker"), TEXT("Mailbox"),
		TEXT("Low Level Quests"), TEXT("None")
	};

	for (int32 RowIndex = 1; RowIndex <= UE_ARRAY_COUNT(TrackingLabels); ++RowIndex)
	{
		const FString RowName = FString::Printf(TEXT("MinimapNativeTrackingRow%d"), RowIndex);
		if (FindFrame(RowName) < 0)
		{
			FWowFrameDef RowDef;
			RowDef.Name = RowName;
			RowDef.Type = EWowFrameType::Button;
			RowDef.Width = 190.0f;
			RowDef.Height = 18.0f;
			RowDef.Strata = EWowFrameStrata::TOOLTIP;
			RowDef.FrameLevel = 120 + RowIndex;
			RowDef.ButtonText = TrackingLabels[RowIndex - 1];
			CreateFrame(RowDef);
		}

		const int64 RowHandle = FindFrame(RowName);
		SetFrameVisible(RowHandle, false);
		SetFrameMouseEnabled(RowHandle, true);
		if (UTextBlock* Label = GetPrimaryTextWidget(RowHandle))
		{
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = FMath::Max(9, FMath::RoundToInt(10.0f * UIScale));
			Label->SetFont(Font);
			Label->SetJustification(ETextJustify::Left);
			Label->SetShadowOffset(FVector2D(1.0f));
			Label->SetShadowColorAndOpacity(FLinearColor::Black);
			Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		}
	}

	UImage* MenuBackground = GetNamedTexture(TEXT("MinimapNativeTrackingBackground"));
	if (!MenuBackground)
	{
		MenuBackground = NewObject<UImage>(RootCanvas.Get());
		RootCanvas->AddChildToCanvas(MenuBackground);
		NamedTextureWidgets.Add(TEXT("MinimapNativeTrackingBackground"), MenuBackground);
	}
	MenuBackground->SetBrush(FSlateRoundedBoxBrush(
		FWowWindowStyle::ChatBackgroundColor(), 3.0f * UIScale,
		WowColor::FromWow(0.62f, 0.62f, 0.67f, 0.98f), 1.0f * UIScale));
	MenuBackground->SetVisibility(ESlateVisibility::Collapsed);

	auto SetSquareFrame = [this](const TCHAR* Name, float Size)
	{
		const int64 Handle = FindFrame(Name);
		if (Handle < 0) return;
		SetFrameSize(Handle, Size, Size);
		if (UWidget* Widget = GetWidgetForHandle(Handle))
		{
			Widget->SetRenderScale(FVector2D(1.0f));
		}
	};
	SetSquareFrame(TEXT("MiniMapTracking"), 32.0f);
	SetSquareFrame(TEXT("MiniMapTrackingButton"), 32.0f);
	SetSquareFrame(TEXT("MinimapZoomIn"), 32.0f);
	SetSquareFrame(TEXT("MinimapZoomOut"), 32.0f);
	SetSquareFrame(TEXT("MiniMapWorldMapButton"), 33.0f);
	SetSquareFrame(TEXT("GameTimeFrame"), 40.0f);
	SetSquareFrame(TEXT("MiniMapMailFrame"), 33.0f);
	SetSquareFrame(TEXT("MiniMapLFGFrame"), 33.0f);
	SetSquareFrame(TEXT("MiniMapLFGFrameIcon"), 30.0f);

	auto SetTextureSquare = [this](const TCHAR* Name, const TCHAR* OwnerName,
		float Size, const FVector2D& LocalPosition)
	{
		if (UImage* Image = GetNamedTexture(Name))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Image->Slot))
			{
				FVector2D Position = LocalPosition;
				if (Image->GetParent() == RootCanvas.Get())
				{
					const FFrameRect OwnerRect = GetFrameRect(OwnerName);
					Position += FVector2D(OwnerRect.X, OwnerRect.Y);
				}
                // Old two-anchor regions must not stretch our fixed square.
                Slot->SetAnchors(FAnchors(0.0f));
                Slot->SetAlignment(FVector2D::ZeroVector);
				Slot->SetAutoSize(false);
				Slot->SetPosition(Position * UIScale);
				Slot->SetSize(FVector2D(Size * UIScale));
			}
			Image->SetRenderTransform(FWidgetTransform());
		}
	};
	// TrackingBorder is a 64x64 image whose visible circle occupies only the
    // top-left 40x40 pixels. Keep the authored 52/54 extent and origin; forcing
    // it to 40 shrank and shifted the ring away from the icon and click target.
    SetTextureSquare(TEXT("MiniMapTrackingBackground"), TEXT("MiniMapTracking"), 25, FVector2D(2,4));
    SetTextureSquare(TEXT("MiniMapTrackingIcon"), TEXT("MiniMapTracking"), 20, FVector2D(6,6));
    SetTextureSquare(TEXT("MiniMapTrackingIconOverlay"), TEXT("MiniMapTracking"), 20, FVector2D(6,6));
    SetTextureSquare(TEXT("MiniMapTrackingButtonBorder"), TEXT("MiniMapTrackingButton"), 54, FVector2D::ZeroVector);
    SetTextureSquare(TEXT("MiniMapWorldMapButtonIcon"), TEXT("MiniMapWorldMapButton"), 20, FVector2D(4.5,4.5));
    SetTextureSquare(TEXT("MiniMapWorldBorder"), TEXT("MiniMapWorldMapButton"), 52, FVector2D::ZeroVector);
    SetTextureSquare(TEXT("MiniMapMailIcon"), TEXT("MiniMapMailFrame"), 18, FVector2D(7,6));
    SetTextureSquare(TEXT("MiniMapMailBorder"), TEXT("MiniMapMailFrame"), 52, FVector2D::ZeroVector);
    SetTextureSquare(TEXT("MiniMapLFGFrameBorder"), TEXT("MiniMapLFGFrame"), 52, FVector2D(1,1));
    SetTextureSquare(TEXT("MiniMapLFGFrameIconTexture"), TEXT("MiniMapLFGFrameIcon"), 30, FVector2D::ZeroVector);
    // Wrath uses the calendar button, not the obsolete day/night atlas behind it.
    if (UImage* OldTime = GetNamedTexture(TEXT("GameTimeTexture"))) OldTime->SetVisibility(ESlateVisibility::Collapsed);

	// Calendar invite/alarm art is authored without an explicit size and is
	// normally hidden by Blizzard_Calendar until a real notification exists.
	// Leaving it visible stretches its source atlas across the minimap controls.
	for (const TCHAR* CalendarAlertTexture : {
		TEXT("GameTimeCalendarInvitesTexture"),
		TEXT("GameTimeCalendarInvitesGlow"),
		TEXT("GameTimeCalendarEventAlarmTexture") })
	{
		if (UImage* AlertImage = GetNamedTexture(CalendarAlertTexture))
		{
			AlertImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	if (UImage* TrackingIcon = GetNamedTexture(TEXT("MiniMapTrackingIcon")))
	{
		UTexture2D* TrackingTexture = LoadUITexture(TEXT("Interface\\Minimap\\Tracking\\None"));
		if (!TrackingTexture)
		{
			TrackingTexture = LoadUITexture(TEXT("Interface\\Icons\\Ability_Tracking"));
		}
		if (!TrackingTexture)
		{
			TrackingTexture = LoadUITexture(TEXT("Interface\\Icons\\INV_Misc_Spyglass_03"));
		}
		if (TrackingTexture)
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(TrackingTexture);
			Brush.ImageSize = FVector2D(TrackingTexture->GetSizeX(), TrackingTexture->GetSizeY());
			Brush.DrawAs = ESlateBrushDrawType::Image;
			TrackingIcon->SetBrush(Brush);
			TrackingIcon->SetRenderOpacity(1.0f);
			TrackingIcon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}

	for (const TCHAR* LfgFrameName : { TEXT("MiniMapLFGFrame"), TEXT("MiniMapLFGFrameIcon") })
	{
		const int64 LfgHandle = FindFrame(LfgFrameName);
		if (LfgHandle >= 0)
		{
			SetFrameVisible(LfgHandle, false);
			SetFrameMouseEnabled(LfgHandle, false); // r263: no hover/tooltip until queued
		}
	}
	// r270 (Stefan 13.10: a dark box with "Leave Queue" under the minimap): MiniMapLFGFrameDropDown is
	// a MENU-mode UIDropDownMenu - only the anchor of DropDownList1, never drawn itself.
	if (UWidget* LfgDropDown = GetWidgetForHandle(FindFrame(TEXT("MiniMapLFGFrameDropDown"))))
		LfgDropDown->SetVisibility(ESlateVisibility::Collapsed);
	for (const TCHAR* LfgTextureName : { TEXT("MiniMapLFGFrameBorder"), TEXT("MiniMapLFGFrameIconTexture") })
	{
		if (UImage* LfgImage = GetNamedTexture(LfgTextureName))
		{
			LfgImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// The clock lives inside MinimapCluster's canvas. Frame Z orders are only
	// compared between siblings, so a root-canvas clock with the cluster's Z drew
	// underneath all of UIParent (minimap ring included). As the cluster's last
	// child it draws over the minimap art and still under MEDIUM+ windows (bags,
	// dialogs), which are later siblings of the cluster in UIParent.
	const int64 ClockClusterHandle = FindFrame(TEXT("MinimapCluster"));
	UCanvasPanel* ClockParent = Cast<UCanvasPanel>(GetWidgetForHandle(ClockClusterHandle));
	const bool bClockInCluster = ClockParent != nullptr;
	if (!ClockParent) ClockParent = RootCanvas.Get();
	auto AdoptClockWidget = [ClockParent](UWidget* Widget)
	{
		if (Widget && Widget->GetParent() != ClockParent)
		{
			Widget->RemoveFromParent();
			ClockParent->AddChildToCanvas(Widget);
		}
	};
	UImage* ClockBackground = GetNamedTexture(TEXT("MinimapNativeClockBackground"));
	if (!ClockBackground)
	{
		ClockBackground = NewObject<UImage>(RootCanvas.Get());
		NamedTextureWidgets.Add(TEXT("MinimapNativeClockBackground"), ClockBackground);
	}
	AdoptClockWidget(ClockBackground);
	ClockBackground->SetBrush(FSlateRoundedBoxBrush(
		FWowWindowStyle::ChatBackgroundColor(), 1.5f * UIScale,
		WowColor::FromWow(0.58f, 0.58f, 0.62f, 0.96f), 1.0f * UIScale));
	ClockBackground->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UTextBlock* ClockText = GetNamedFontString(TEXT("MinimapNativeClockText"));
	if (!ClockText)
	{
		ClockText = NewObject<UTextBlock>(RootCanvas.Get());
		NamedFontStringWidgets.Add(TEXT("MinimapNativeClockText"), ClockText);
	}
	AdoptClockWidget(ClockText);
	if (ClockText)
	{
		const FFrameRect MinimapRect = GetFrameRect(TEXT("Minimap"));
		constexpr float ClockWidth = 44.0f;
		constexpr float ClockHeight = 15.0f;
		FVector2D ClockPosition(
			MinimapRect.X + (MinimapRect.W - ClockWidth) * 0.5f,
			MinimapRect.Y + MinimapRect.H - 9.0f);
		if (bClockInCluster)
		{
			// Frame rects are absolute design units; children are placed relative to their parent.
			const FFrameRect ClusterRect = GetFrameRect(TEXT("MinimapCluster"));
			ClockPosition -= FVector2D(ClusterRect.X, ClusterRect.Y);
		}
		const int32 ClockZ = bClockInCluster ? 9000 : MinimapOverlayZOrder(*this) + 2;
		if (UCanvasPanelSlot* BackgroundSlot = Cast<UCanvasPanelSlot>(ClockBackground->Slot))
		{
			BackgroundSlot->SetAutoSize(false);
			BackgroundSlot->SetPosition(ClockPosition * UIScale);
			BackgroundSlot->SetSize(FVector2D(ClockWidth, ClockHeight) * UIScale);
			// Above every other MinimapCluster child (ring, zoom/tracking buttons).
			BackgroundSlot->SetZOrder(ClockZ);
		}
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ClockText->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition((ClockPosition + FVector2D(2.0f, 1.0f)) * UIScale);
			Slot->SetSize(FVector2D(ClockWidth - 4.0f, ClockHeight - 2.0f) * UIScale);
			Slot->SetZOrder(ClockZ + 1);
		}
		if (MinimapFrostHost.IsValid())
			if (UCanvasPanelSlot* FrostSlot = Cast<UCanvasPanelSlot>(MinimapFrostHost->Slot))
				FrostSlot->SetZOrder(MinimapOverlayZOrder(*this) + 1);
		ClockText->SetJustification(ETextJustify::Center);
		ClockText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ClockText->SetShadowOffset(FVector2D(1.0f));
		ClockText->SetShadowColorAndOpacity(FLinearColor::Black);
		ClockText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		FSlateFontInfo Font = FontManager && FontManager->IsInitialized()
			? FontManager->GetFont(TEXT("GameFontHighlightSmall"),
				FMath::Max(8, FMath::RoundToInt(8.0f * UIScale)))
			: ClockText->GetFont();
		if (!FontManager || !FontManager->IsInitialized())
		{
			Font.Size = FMath::Max(8, FMath::RoundToInt(8.0f * UIScale));
		}
		ClockText->SetFont(Font);
	}

	UpdateMinimapTime(MinimapTimeOfDayMinutes);
	RefreshNativeMinimapControls();
}

void FWowFrameManager::SetLfgQueueVisible(bool bVisible)
{
	for (const TCHAR* FrameName : { TEXT("MiniMapLFGFrame"), TEXT("MiniMapLFGFrameIcon") })
	{
		const int64 Handle = FindFrame(FrameName);
		if (Handle >= 0)
		{
			SetFrameVisible(Handle, bVisible);
			// r263 (Stefan 12.10: a "Dungeon Finder" tooltip over the minimap while not queued): the
			// Lua side can Show() the frame again with its art hidden; an invisible button must not
			// take the mouse.
			SetFrameMouseEnabled(Handle, bVisible);
		}
	}
	bLfgQueueEyeShown = bVisible;
	if (UWidget* LfgDropDown = GetWidgetForHandle(FindFrame(TEXT("MiniMapLFGFrameDropDown"))))
		LfgDropDown->SetVisibility(ESlateVisibility::Collapsed); // r270: the menu anchor is never drawn

	for (const TCHAR* TextureName : { TEXT("MiniMapLFGFrameBorder"), TEXT("MiniMapLFGFrameIconTexture") })
	{
		if (UImage* Image = GetNamedTexture(TextureName))
		{
			Image->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
}

void FWowFrameManager::UpdateMinimapTime(float TimeOfDayMinutes)
{
	MinimapTimeOfDayMinutes = FMath::Fmod(TimeOfDayMinutes, 1440.0f);
	if (MinimapTimeOfDayMinutes < 0.0f)
	{
		MinimapTimeOfDayMinutes += 1440.0f;
	}

	const bool bNight = MinimapTimeOfDayMinutes < 360.0f || MinimapTimeOfDayMinutes >= 1080.0f;
	LastMinimapDayNightState = bNight ? 1 : 0;
    const int64 CalendarHandle = FindFrame(TEXT("GameTimeFrame"));
    if (CalendarHandle >= 0)
    {
        if (UTextBlock* DateLabel = GetPrimaryTextWidget(CalendarHandle))
        {
            DateLabel->SetText(FText::FromString(FString::FromInt(FDateTime::Now().GetDay())));
            DateLabel->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
            FSlateFontInfo Font = DateLabel->GetFont();
            Font.Size = FMath::Max(9, FMath::RoundToInt(12 * UIScale));
            DateLabel->SetFont(Font);
            // Centre the day number on the calendar page (stock ButtonText anchor: CENTER -1,-1)
            // instead of the fill slot, which leaves a UTextBlock at the top of the button.
            if (UCanvasPanelSlot* DateSlot = Cast<UCanvasPanelSlot>(DateLabel->Slot))
            {
                DateSlot->SetAnchors(FAnchors(0.5f, 0.5f));
                DateSlot->SetAlignment(FVector2D(0.5f, 0.5f));
                DateSlot->SetAutoSize(true);
                DateSlot->SetPosition(FVector2D(-1.0f, 1.0f) * UIScale);
            }
            DateLabel->SetJustification(ETextJustify::Center);
        }
    }

	if (UTextBlock* ClockText = GetNamedFontString(TEXT("MinimapNativeClockText")))
	{
		const int32 RealmMinutes = FMath::FloorToInt(MinimapTimeOfDayMinutes) % 1440;
		ClockText->SetText(FText::FromString(FString::Printf(
			TEXT("%02d:%02d"), RealmMinutes / 60, RealmMinutes % 60)));
		ClockText->SetRenderOpacity(1.0f);
		// The clock is ordered in strata LOW (see MinimapOverlayZOrder), so bags and
		// dialogs simply draw over it; no need to hide it any more.
	}
}

void FWowFrameManager::RefreshNativeMinimapControls()
{
	if (!RootCanvas.IsValid())
	{
		return;
	}

	constexpr float MenuWidth = 196.0f;
	constexpr float RowHeight = 18.0f;
	constexpr int32 RowCount = 12;
	constexpr float MenuHeight = RowCount * RowHeight + 10.0f;
	FFrameRect UIParentRect = GetFrameRect(TEXT("UIParent"));
	FFrameRect TrackingRect = GetFrameRect(TEXT("MiniMapTrackingButton"));
	const float MenuX = FMath::Clamp(TrackingRect.X - MenuWidth + 40.0f,
		8.0f, FMath::Max(8.0f, UIParentRect.W - MenuWidth - 8.0f));
	const float MenuY = FMath::Clamp(TrackingRect.Y + 42.0f,
		8.0f, FMath::Max(8.0f, UIParentRect.H - MenuHeight - 8.0f));

	if (UImage* Background = GetNamedTexture(TEXT("MinimapNativeTrackingBackground")))
	{
		Background->SetVisibility(bMinimapTrackingMenuOpen
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Background->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition(FVector2D(MenuX, MenuY) * UIScale);
			Slot->SetSize(FVector2D(MenuWidth, MenuHeight) * UIScale);
			Slot->SetZOrder(static_cast<int32>(EWowFrameStrata::TOOLTIP) * 1000 + 110);
		}
	}

	for (int32 RowIndex = 1; RowIndex <= RowCount; ++RowIndex)
	{
		const int64 RowHandle = FindFrame(FString::Printf(TEXT("MinimapNativeTrackingRow%d"), RowIndex));
		if (RowHandle < 0) continue;
		SetFrameVisible(RowHandle, bMinimapTrackingMenuOpen);
		if (UWidget* RowWidget = GetWidgetForHandle(RowHandle))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(RowWidget->Slot))
			{
				Slot->SetAutoSize(false);
				Slot->SetPosition(FVector2D(MenuX + 3.0f,
					MenuY + 5.0f + (RowIndex - 1) * RowHeight) * UIScale);
				Slot->SetSize(FVector2D(MenuWidth - 6.0f, RowHeight) * UIScale);
				Slot->SetZOrder(static_cast<int32>(EWowFrameStrata::TOOLTIP) * 1000 + 120 + RowIndex);
			}
		}

		const int32 RowMode = RowIndex == RowCount ? 0 : RowIndex;
		if (UTextBlock* Label = GetPrimaryTextWidget(RowHandle))
		{
			static const TCHAR* TrackingLabels[] = {
				TEXT("Repair"), TEXT("Food & Drink"), TEXT("Reagents"), TEXT("Innkeeper"),
				TEXT("Flight Master"), TEXT("Class Trainer"), TEXT("Profession Trainers"),
				TEXT("Auctioneer"), TEXT("Banker"), TEXT("Mailbox"),
				TEXT("Low Level Quests"), TEXT("None")
			};
			Label->SetText(FText::FromString(FString::Printf(TEXT("%s%s"),
				RowMode == MinimapTrackingMode ? TEXT(">  ") : TEXT("   "),
				TrackingLabels[RowIndex - 1])));
			Label->SetColorAndOpacity(FSlateColor(RowMode == MinimapTrackingMode
				? WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f) : FLinearColor::White));
		}
	}
}

void FWowFrameManager::HideNativeTooltip()
{
	if (NativeTooltipOwnerHandle < 0)
	{
		return;
	}
	NativeTooltipOwnerHandle = -1;
	const int64 TooltipHandle = FindFrame(TEXT("GameTooltip"));
	if (TooltipHandle >= 0)
	{
		SetFrameVisible(TooltipHandle, false);
	}
}

void FWowFrameManager::ShowNativeTooltipForFrame(int64 OwnerHandle)
{
	const int64 TooltipHandle = FindFrame(TEXT("GameTooltip"));
	if (TooltipHandle < 0)
	{
		return;
	}

    if (ItemConnection.IsValid())
    {
        const int32 ActionSlot = GetActionSlotForFrame(OwnerHandle);
        const auto& Actions = ItemConnection->PacketHandler.ActionButtons;
        if (Actions.IsValidIndex(ActionSlot) && Actions[ActionSlot])
        {
            const uint32 Id = Actions[ActionSlot] & 0x00ffffffu;
            const uint32 Type = Actions[ActionSlot] >> 24;
            if (Type == 0 || Type == 128)
            {
                NativeTooltipOwnerHandle = OwnerHandle;
                TooltipSetOwner(TooltipHandle, OwnerHandle, TEXT("ANCHOR_DEFAULT")); // default corner, like GameTooltip_SetDefaultAnchor
                if (Type == 0) TooltipSetSpell(TooltipHandle, Id);
                else TooltipSetItemDetails(TooltipHandle, Id, FString(), 1, FString());
                SetFrameStrata(TooltipHandle, EWowFrameStrata::TOOLTIP);
                SetFrameVisible(TooltipHandle, true);
                return;
            }
        }
        static const TCHAR* Slots[]={TEXT("Head"),TEXT("Neck"),TEXT("Shoulder"),TEXT("Shirt"),TEXT("Chest"),TEXT("Waist"),TEXT("Legs"),TEXT("Feet"),TEXT("Wrist"),TEXT("Hands"),TEXT("Finger0"),TEXT("Finger1"),TEXT("Trinket0"),TEXT("Trinket1"),TEXT("Back"),TEXT("MainHand"),TEXT("SecondaryHand"),TEXT("Ranged"),TEXT("Tabard")};
        for (int64 H=OwnerHandle, Guard=0;H>=0 && Guard<20;++Guard)
        {
            const auto* OwnerEntry=Frames.Find(H); if(!OwnerEntry) break;
            for(int32 I=0;I<UE_ARRAY_COUNT(Slots);++I)
                if(OwnerEntry->Def.Name==FString(TEXT("Character"))+Slots[I]+TEXT("Slot"))
                {
                    auto& Entities=ItemConnection->PacketHandler.EntityManager;
                    if(auto* Player=Entities.GetLocalPlayer())
                    {
                        auto* Item=Entities.FindItem(Player->GetEquipmentItemGuid(uint8(I)));
                        const uint32 Id=Item?Item->GetField(ObjectField::ENTRY):Player->GetField(PlayerField::VISIBLE_ITEM_BASE+I*2);
                        if(Id)
                        {
                            NativeTooltipOwnerHandle=H;
                            if (const auto* Existing=GetTooltipState(TooltipHandle);
                                Existing && Existing->ItemEntry==Id && Existing->Lines.Num()>1 && IsFrameVisible(TooltipHandle))
                                return; // Preserve enchantment/context lines already supplied by Lua.
                            TooltipSetOwner(TooltipHandle,H,TEXT("ANCHOR_DEFAULT"));
                            // The item object too: its bound state ("Soulbound") and random suffix.
                            TooltipSetItemDetails(TooltipHandle,Id,FString::Printf(TEXT("item:%u"),Id),1,FString(),Item?Item->GetDurability():0,Item?Item->GetMaxDurability():0,
                                true,Item?Item->Guid:0);
                            SetFrameStrata(TooltipHandle,EWowFrameStrata::TOOLTIP);
                            SetFrameVisible(TooltipHandle,true);
                            return;
                        }
                    }
                }
            H=OwnerEntry->ParentHandle;
        }
    }
	// Hit testing can land on an icon or texture owned by an interactive frame.
	// Resolve the first supported ancestor so nested micro buttons receive the
	// same native tooltip as their top-level counterpart.
	int64 TooltipOwnerHandle = OwnerHandle;
	const FFrameEntry* Owner = Frames.Find(TooltipOwnerHandle);
	int32 RemainingDepth = 20;
	auto SupportsNativeTooltip = [](const FString& FrameName)
	{
		return FrameName == TEXT("MiniMapTrackingButton") ||
			FrameName == TEXT("MiniMapTracking") ||
			FrameName == TEXT("MinimapZoomIn") ||
			FrameName == TEXT("MinimapZoomOut") ||
			FrameName == TEXT("GameTimeFrame") ||
			FrameName == TEXT("MiniMapMailFrame") ||
			FrameName == TEXT("MiniMapLFGFrame") ||
			FrameName == TEXT("LFDMicroButton") ||
			FrameName == TEXT("MinimapZoneTextButton") ||
			FrameName == TEXT("MiniMapWorldMapButton");
	};
	while (Owner && !SupportsNativeTooltip(Owner->Def.Name) && RemainingDepth-- > 0)
	{
		TooltipOwnerHandle = Owner->ParentHandle;
		Owner = Frames.Find(TooltipOwnerHandle);
	}
	if (!Owner || !SupportsNativeTooltip(Owner->Def.Name))
	{
		return;
	}
	if (Owner->Def.Name == TEXT("MiniMapLFGFrame") && !bLfgQueueEyeShown) return; // r263: hidden queue eye

	FString Title;
	TArray<FString> DetailLines;
	const FString& Name = Owner->Def.Name;
	if (Name == TEXT("MiniMapTrackingButton") || Name == TEXT("MiniMapTracking"))
	{
		Title = TEXT("Tracking");
		DetailLines.Add(TEXT("Click to choose tracking type."));
	}
	else if (Name == TEXT("MinimapZoomIn"))
	{
		Title = TEXT("Zoom In");
		DetailLines.Add(TEXT("Show a smaller area in greater detail."));
	}
	else if (Name == TEXT("MinimapZoomOut"))
	{
		Title = TEXT("Zoom Out");
		DetailLines.Add(TEXT("Show a larger area around your character."));
	}
	else if (Name == TEXT("GameTimeFrame"))
	{
		// Calendar button: there is no calendar yet, so it shows today's date only
		// (the clock below the minimap has the Time Info tooltip).
		static const TCHAR* WeekDays[] = {TEXT("Monday"), TEXT("Tuesday"), TEXT("Wednesday"), TEXT("Thursday"),
			TEXT("Friday"), TEXT("Saturday"), TEXT("Sunday")};
		static const TCHAR* Months[] = {TEXT("January"), TEXT("February"), TEXT("March"), TEXT("April"), TEXT("May"),
			TEXT("June"), TEXT("July"), TEXT("August"), TEXT("September"), TEXT("October"), TEXT("November"), TEXT("December")};
		const FDateTime Today = FDateTime::Now();
		Title = FString::Printf(TEXT("%s, %s %d"), WeekDays[static_cast<int32>(Today.GetDayOfWeek())],
			Months[Today.GetMonth() - 1], Today.GetDay());
	}
	else if (Name == TEXT("MiniMapMailFrame"))
	{
		Title = TEXT("Unread Mail");
		DetailLines.Add(TEXT("You have unread mail waiting at a mailbox."));
	}
	else if (Name == TEXT("MiniMapLFGFrame") || Name == TEXT("LFDMicroButton"))
	{
		Title = TEXT("Dungeon Finder");
		DetailLines.Add(TEXT("Click to choose a dungeon and group role."));
	}
	else if (Name == TEXT("MinimapZoneTextButton") || Name == TEXT("MiniMapWorldMapButton"))
	{
		Title = TEXT("World Map");
		DetailLines.Add(TEXT("Click to open the world map."));
	}
	else
	{
		return;
	}

	NativeTooltipOwnerHandle = TooltipOwnerHandle;
	TooltipClearLines(TooltipHandle);
	// Minimap buttons sit at the right screen edge: open the tooltip down-left of them
	// (ANCHOR_DEFAULT put it off screen to the right).
	TooltipSetOwner(TooltipHandle, TooltipOwnerHandle, TEXT("ANCHOR_BOTTOMLEFT"));
	TooltipAddLine(TooltipHandle, Title, 1.0f, 0.82f, 0.0f, false);
	for (const FString& Line : DetailLines)
	{
		TooltipAddLine(TooltipHandle, Line, 1.0f, 1.0f, 1.0f, false);
	}
	SetFrameStrata(TooltipHandle, EWowFrameStrata::TOOLTIP);
	SetFrameVisible(TooltipHandle, true);
}

static UTexture2D* GetUnitCastSparkTexture()
{
    static TWeakObjectPtr<UTexture2D> Cached;
    if (Cached.IsValid()) return Cached.Get();
    UTexture2D* Texture = UTexture2D::CreateTransient(32, 32, PF_B8G8R8A8);
    if (!Texture) return nullptr;
    FColor* Pixels = static_cast<FColor*>(Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
    for (int32 Y = 0; Y < 32; ++Y)
        for (int32 X = 0; X < 32; ++X)
        {
            const float U = (X - 15.5f) / 15.5f, V = (Y - 15.5f) / 15.5f;
            const float Alpha = FMath::Exp(-10.0f * U * U - 3.5f * V * V);
            Pixels[Y * 32 + X] = FColor(255, 239, 150, FMath::RoundToInt(Alpha * 255.0f));
        }
    Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
    Texture->SRGB = true;
    Texture->Filter = TF_Bilinear;
    Texture->UpdateResource();
    Cached = Texture;
    return Texture;
}

namespace
{
// Stock unit frame bar colours (HealthBar_OnValueChanged: 0,1,0; PowerBarColor["MANA"]: 0,0,1).
// A bar still at the creation default (FLinearColor::Green, see CreateFrame) was never coloured by Lua.
void ApplyStockUnitBarColorsTo(UProgressBar* HealthBar, UProgressBar* ManaBar)
{
	if (HealthBar) HealthBar->SetFillColorAndOpacity(WowColor::FromWow(0.0f, 1.0f, 0.0f, 1.0f));
	if (ManaBar)
	{
		const FLinearColor Current = ManaBar->GetFillColorAndOpacity();
		const bool bUncoloured = Current.Equals(FLinearColor::Green, 1e-3f)
			|| Current.Equals(WowColor::FromWow(0.01f, 0.055f, 0.32f, 1.0f), 1e-3f); // the old forced blue
		if (bUncoloured) ManaBar->SetFillColorAndOpacity(WowColor::FromWow(0.0f, 0.0f, 1.0f, 1.0f));
	}
}
}

void FWowFrameManager::SyncChildVisibility()
{
	// After all OnLoad scripts fire, some parents got hidden but their children
	// were already created visible. Walk the tree and hide children of hidden ancestors.
	int32 HiddenCount = 0;
	for (auto& Pair : Frames)
	{
		if (!Pair.Value.Widget.IsValid()) continue;
		if (Pair.Value.Def.bHidden) continue; // Already hidden by itself

		// Walk parent chain
		int64 CheckHandle = Pair.Value.ParentHandle;
		int32 MaxDepth = 20;
		bool bAncestorHidden = false;
		while (CheckHandle >= 0 && MaxDepth-- > 0)
		{
			const FFrameEntry* Ancestor = Frames.Find(CheckHandle);
			if (!Ancestor) break;
			if (Ancestor->Def.bHidden || (Ancestor->Widget.IsValid() &&
				Ancestor->Widget->GetVisibility() == ESlateVisibility::Collapsed))
			{
				bAncestorHidden = true;
				break;
			}
			CheckHandle = Ancestor->ParentHandle;
		}

		if (bAncestorHidden && Pair.Value.Widget->GetVisibility() != ESlateVisibility::Collapsed)
		{
			Pair.Value.Widget->SetVisibility(ESlateVisibility::Collapsed);
			HiddenCount++;
		}
	}

	if (HiddenCount > 0)
	{
		UE_LOG(LogWowFrame, Log, TEXT("SyncChildVisibility: hid %d orphaned visible children of hidden parents"), HiddenCount);
	}

	// Force-show frames that WoW's Lua would show at startup but our FrameXML
	// scripts can't execute (missing API stubs cause OnLoad to fail/hide them).
	int32 ForceShownCount = 0;
	for (auto& Pair : Frames)
	{
		if (!Pair.Value.Widget.IsValid()) continue;
		const FString& Name = Pair.Value.Def.Name;

		bool bShouldForceShow = false;

		// ActionButton1-12 (main action bar buttons)
		if (Name.StartsWith(TEXT("ActionButton")) && Name.Len() <= 14)
			bShouldForceShow = true;
		// BonusActionButton1-12
		if (Name.StartsWith(TEXT("BonusActionButton")))
			bShouldForceShow = true;

		if (bShouldForceShow && (Pair.Value.Def.bHidden || Pair.Value.Widget->GetVisibility() == ESlateVisibility::Collapsed))
		{
			// Check parent is actually visible first
			bool bParentVis = true;
			int64 PH = Pair.Value.ParentHandle;
			if (PH >= 0)
			{
				const FFrameEntry* PE = Frames.Find(PH);
				if (PE && PE->Widget.IsValid() && PE->Widget->GetVisibility() == ESlateVisibility::Collapsed)
					bParentVis = false;
			}
			if (bParentVis)
			{
                SetFrameVisible(Pair.Key, true);
				ForceShownCount++;
			}
		}
	}
	if (ForceShownCount > 0)
	{
		UE_LOG(LogWowFrame, Log, TEXT("SyncChildVisibility: force-showed %d action bar frames"), ForceShownCount);
	}

    UpdateOptionalActionBars();

	// The stock anchors leave six design pixels between 36-pixel buttons. At our
	// current viewport scale that becomes a conspicuous eight-pixel gap. Classic
	// action bars retain only a one-unit gutter, enough to distinguish each thin
	// bevel without returning to the original wide empty strips.
	auto CompactActionBar = [this](const TCHAR* BarName, bool bVertical)
	{
		// All four optional bars share one look and size (Stefan 10.10: "make all action bar
		// slots alike"); the vertical columns only keep a tighter step.
		const float ButtonDesignSize = 36.0f;
		const float CompactStep = (bVertical ? 37.0f : 39.0f) * UIScale;
		const FVector2D DesiredButtonSize(ButtonDesignSize * UIScale);
		UCanvasPanelSlot* BarSlot = nullptr;
		if (UWidget* Bar = GetWidgetForHandle(FindFrame(BarName)))
		{
			BarSlot = Cast<UCanvasPanelSlot>(Bar->Slot);
		}
		FVector2D ButtonSize = DesiredButtonSize;
		for (int32 ButtonIndex = 1; ButtonIndex <= 12; ++ButtonIndex)
		{
			const FString ButtonName = FString::Printf(TEXT("%sButton%d"), BarName, ButtonIndex);
            if (auto* BackgroundButton = ButtonWidgets.FindRef(FindFrame(ButtonName)).Get())
                BackgroundButton->SetBackgroundColor(FLinearColor(1,1,1,.18f));
			if (UWidget* Button = GetWidgetForHandle(FindFrame(ButtonName)))
			{
				if (UCanvasPanelSlot* ButtonSlot = Cast<UCanvasPanelSlot>(Button->Slot))
				{
					ButtonSlot->SetSize(DesiredButtonSize);
					ButtonSize = DesiredButtonSize;
					ButtonSlot->SetPosition(bVertical
						? FVector2D(2.0f * UIScale, (ButtonIndex - 1) * CompactStep)
						: FVector2D((ButtonIndex - 1) * CompactStep, 2.0f * UIScale));
				}

				// The optional bars inherited only a small native-size placeholder,
				// which made 36-unit frames look widely separated. Stretch the stock
				// quick-slot face over the complete frame, just like the main bar.
				if (UCanvasPanel* ButtonCanvas = Cast<UCanvasPanel>(Button))
				{
					const FString FaceName = ButtonName + TEXT("NativeSlotFace");
					UImage* Face = GetNamedTexture(FaceName);
					if (!Face)
					{
						Face = NewObject<UImage>(ButtonCanvas);
						Face->SetBrush(FSlateColorBrush(WowColor::FromWow(0.16f, 0.15f, 0.13f, .96f)));
						Face->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
						if (UCanvasPanelSlot* FaceSlot = ButtonCanvas->AddChildToCanvas(Face))
						{
							FaceSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
							FaceSlot->SetOffsets(FMargin(0.0f));
							FaceSlot->SetZOrder(40);
						}
						NamedTextureWidgets.Add(FaceName, Face);
					}

					const FString InnerName = ButtonName + TEXT("NativeSlotInner");
					UImage* Inner = GetNamedTexture(InnerName);
					if (!Inner)
					{
						Inner = NewObject<UImage>(ButtonCanvas);
						Inner->SetBrush(FSlateColorBrush(
							WowColor::FromWow(0.0020f, 0.0025f, 0.0030f, 1.f)));
						Inner->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
						if (UCanvasPanelSlot* InnerSlot = ButtonCanvas->AddChildToCanvas(Inner))
						{
							InnerSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
							// Bartender-style face: only a thin bevel remains visible.
							InnerSlot->SetOffsets(FMargin(1.25f * UIScale));
							InnerSlot->SetZOrder(41);
						}
						NamedTextureWidgets.Add(InnerName, Inner);
					}

					// Four narrow edge strips create an inset 3D frame: light catches
					// the top/left edges while the bottom/right edges fall into shadow.
					auto EnsureBevelEdge = [this, ButtonCanvas, &ButtonName](
						const TCHAR* Suffix, const FVector2D& Position, const FVector2D& Size,
						const FLinearColor& Color)
					{
						const FString EdgeName = ButtonName + Suffix;
						UImage* Edge = GetNamedTexture(EdgeName);
						if (!Edge)
						{
							Edge = NewObject<UImage>(ButtonCanvas);
							Edge->SetBrush(FSlateColorBrush(Color));
							Edge->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
							ButtonCanvas->AddChildToCanvas(Edge);
							NamedTextureWidgets.Add(EdgeName, Edge);
						}
						if (UCanvasPanelSlot* EdgeSlot = Cast<UCanvasPanelSlot>(Edge->Slot))
						{
							EdgeSlot->SetAutoSize(false);
							EdgeSlot->SetPosition(Position);
							EdgeSlot->SetSize(Size);
							EdgeSlot->SetZOrder(42);
						}
					};
					const float EdgeWidth = 1.0f * UIScale;
					const FLinearColor LitEdge = WowColor::FromWow(0.31f, 0.29f, 0.24f, .96f);
					const FLinearColor ShadowEdge = WowColor::FromWow(0.025f, 0.022f, 0.018f, 1.f);
					EnsureBevelEdge(TEXT("NativeBevelTop"), FVector2D::ZeroVector,
						FVector2D(ButtonSize.X, EdgeWidth), LitEdge);
					EnsureBevelEdge(TEXT("NativeBevelLeft"), FVector2D::ZeroVector,
						FVector2D(EdgeWidth, ButtonSize.Y), LitEdge);
					EnsureBevelEdge(TEXT("NativeBevelBottom"), FVector2D(0.0f, ButtonSize.Y - EdgeWidth),
						FVector2D(ButtonSize.X, EdgeWidth), ShadowEdge);
					EnsureBevelEdge(TEXT("NativeBevelRight"), FVector2D(ButtonSize.X - EdgeWidth, 0.0f),
						FVector2D(EdgeWidth, ButtonSize.Y), ShadowEdge);
				}
			}
		}
		if (BarSlot)
		{
			const float LongSide = 11.0f * CompactStep +
				(bVertical ? ButtonSize.Y : ButtonSize.X) + 2.0f * UIScale;
			BarSlot->SetSize(bVertical
				? FVector2D(ButtonSize.X + 4.0f * UIScale, LongSide)
				: FVector2D(LongSide, ButtonSize.Y + 4.0f * UIScale));
		}
	};
	CompactActionBar(TEXT("MultiBarBottomLeft"), false);
	CompactActionBar(TEXT("MultiBarBottomRight"), false);
	CompactActionBar(TEXT("MultiBarRight"), true);
	CompactActionBar(TEXT("MultiBarLeft"), true);

	// Reserve the complete XP rail plus a gutter below both extra horizontal bars.
	if (UWidget* MainBar = GetWidgetForHandle(FindFrame(TEXT("MainMenuBar"))))
	{
		if (const UCanvasPanelSlot* MainBarSlot = Cast<UCanvasPanelSlot>(MainBar->Slot))
		{
			const float UpperBarY = MainBarSlot->GetPosition().Y - 45.0f * UIScale;
			for (const TCHAR* UpperBarName : {TEXT("MultiBarBottomLeft"), TEXT("MultiBarBottomRight")})
			{
				if (UWidget* UpperBar = GetWidgetForHandle(FindFrame(UpperBarName)))
				{
					if (UCanvasPanelSlot* UpperBarSlot = Cast<UCanvasPanelSlot>(UpperBar->Slot))
					{
						FVector2D Position = UpperBarSlot->GetPosition();
						Position.Y = UpperBarY;
						if (FCString::Strcmp(UpperBarName, TEXT("MultiBarBottomLeft")) == 0)
						{
							// The 24-slot upper strip (this bar + MultiBarBottomRight, 3 units apart) is
							// centred over the main bar; a fixed X ran off the right edge above 100 % UI
							// scale and sat left of it below (Stefan 10.10).
							const float UpperStripWidth = UpperBarSlot->GetSize().X * 2.0f + 3.0f * UIScale;
							Position.X = MainBarSlot->GetPosition().X + MainBarSlot->GetSize().X * 0.5f - UpperStripWidth * 0.5f;
						}
						UpperBarSlot->SetPosition(Position);
					}
				}
			}
		}
	}

	// Join the two lower rows and the two right columns after compacting their
	// children; otherwise the old parent anchors preserve a large empty seam.
	if (UCanvasPanelSlot* LeftSlot = Cast<UCanvasPanelSlot>(
		GetWidgetForHandle(FindFrame(TEXT("MultiBarBottomLeft")))->Slot))
	{
		if (UCanvasPanelSlot* RightSlot = Cast<UCanvasPanelSlot>(
			GetWidgetForHandle(FindFrame(TEXT("MultiBarBottomRight")))->Slot))
		{
			RightSlot->SetPosition(LeftSlot->GetPosition() +
				FVector2D(LeftSlot->GetSize().X + 3.0f * UIScale, 0.0f));
		}
	}
	if (UCanvasPanelSlot* RightColumnSlot = Cast<UCanvasPanelSlot>(
		GetWidgetForHandle(FindFrame(TEXT("MultiBarRight")))->Slot))
	{
		FVector2D RightColumnPosition = RightColumnSlot->GetPosition();
		RightColumnPosition.Y = 225.0f * UIScale;
		RightColumnSlot->SetPosition(RightColumnPosition);
		if (UCanvasPanelSlot* LeftColumnSlot = Cast<UCanvasPanelSlot>(
			GetWidgetForHandle(FindFrame(TEXT("MultiBarLeft")))->Slot))
		{
			LeftColumnSlot->SetPosition(FVector2D(
				RightColumnSlot->GetPosition().X - LeftColumnSlot->GetSize().X - UIScale,
				RightColumnSlot->GetPosition().Y));
		}
	}

	// Force-hide frames that WoW expects to start hidden but our XML/template
	// system didn't mark as hidden (e.g., FadingFrame templates not in MPQ XML).
	static const TCHAR* ForceHideFrames[] = {
		TEXT("ZoneTextFrame"),
		TEXT("SubZoneTextFrame"),
		TEXT("AutoFollowStatusText"),
		TEXT("PVPInfoTextString"),
		TEXT("ReadyCheckListenerFrame"),
		TEXT("ReadyCheckFrame"),
		TEXT("GMChatFrame"),
		TEXT("GMChatFrameEditBox"),
		// Context menus and token/currency displays are shown by Lua only when
		// their owning feature is active.  Their OnLoad handlers currently run
		// before all of the supporting APIs exist, leaving them at UIParent's
		// centre and visibly stacked over the world.
		TEXT("ChatChannelDropDown"),
		TEXT("ChatBNPlayerDropDown"),
		TEXT("CombatLogDropDown"),
		TEXT("CombatLogDefaultButton"),
		TEXT("CombatLogQuickButtonFrame"),
		TEXT("CombatLogQuickButtonFrame_Custom"),
		TEXT("CombatLogQuickButtonFrame_CustomAdditionalFilterButton"),
		TEXT("CombatLogQuickButtonFrameButton1"),
		TEXT("CombatLogQuickButtonFrameButton2"),
		TEXT("CombatLogQuickButtonFrameButton3"),
		TEXT("CombatLogUpdateFrame"),
		TEXT("ChatFrame2ButtonFrame"),
		TEXT("ChatFrame2ButtonFrameBottomButton"),
		TEXT("ChatFrame2ButtonFrameDownButton"),
		TEXT("ChatFrame2ButtonFrameUpButton"),
		TEXT("ChatFrame2ButtonFrameMinimizeButton"),
		TEXT("BackpackTokenFrame"),
		// Unit subframes are conditional in the stock UI.  Keep them hidden
		// until the corresponding pet/alternate-power state is implemented.
		TEXT("PetFrame"),
		TEXT("PetFrameTexture"),
		TEXT("PetFrameHealthBar"),
		TEXT("PetFrameManaBar"),
		TEXT("PetFrameHappiness"),
		TEXT("PetFrameFlash"),
		TEXT("PetFrameDropDown"),
		TEXT("PlayerFrameAlternateManaBar"),
		TEXT("PlayerFrameDropDown"),
		TEXT("PlayerFrameGroupIndicator"),
		TEXT("PlayerFrameReadyCheck"),
		// Unit frames are conditional.  Target/focus (and their target-of-target
		// descendants) are revealed by the corresponding unit-change events.
		TEXT("TargetFrame"),
		TEXT("TargetFrameToT"),
		TEXT("TargetFrameSpellBar"),
		TEXT("TargetFrameBuffs"),
		TEXT("TargetFrameDebuffs"),
		TEXT("TargetFrameNumericalThreat"),
		TEXT("TargetFrameDropDown"),
		TEXT("FocusFrame"),
		TEXT("FocusFrameToT"),
		TEXT("FocusFrameSpellBar"),
		TEXT("FocusFrameBuffs"),
		TEXT("FocusFrameDebuffs"),
		TEXT("FocusFrameNumericalThreat"),
		TEXT("FocusFrameDropDown"),
		// Party frames must not occupy the player-frame area while solo/offline.
		// They are shown later from real group state instead of XML defaults.
		TEXT("PartyMemberFrame1"),
		TEXT("PartyMemberFrame1PetFrame"),
		TEXT("PartyMemberFrame1NotPresentIcon"),
		TEXT("PartyMemberFrame1Disconnect"),
		TEXT("PartyMemberFrame1ReadyCheck"),
		TEXT("PartyMemberFrame1LeaderIcon"),
		TEXT("PartyMemberFrame1PVPIcon"),
		TEXT("PartyMemberFrame2"),
		TEXT("PartyMemberFrame2PetFrame"),
		TEXT("PartyMemberFrame2NotPresentIcon"),
		TEXT("PartyMemberFrame2Disconnect"),
		TEXT("PartyMemberFrame2ReadyCheck"),
		TEXT("PartyMemberFrame2LeaderIcon"),
		TEXT("PartyMemberFrame2PVPIcon"),
		TEXT("PartyMemberFrame3"),
		TEXT("PartyMemberFrame3PetFrame"),
		TEXT("PartyMemberFrame3NotPresentIcon"),
		TEXT("PartyMemberFrame3Disconnect"),
		TEXT("PartyMemberFrame3ReadyCheck"),
		TEXT("PartyMemberFrame3LeaderIcon"),
		TEXT("PartyMemberFrame3PVPIcon"),
		TEXT("PartyMemberFrame4"),
		TEXT("PartyMemberFrame4PetFrame"),
		TEXT("PartyMemberFrame4NotPresentIcon"),
		TEXT("PartyMemberFrame4Disconnect"),
		TEXT("PartyMemberFrame4ReadyCheck"),
		TEXT("PartyMemberFrame4LeaderIcon"),
		TEXT("PartyMemberFrame4PVPIcon"),
		// The stock chat UI fades resize/menu controls in on mouse-over. The two
		// requested chat tabs remain visible and are laid out below the messages.
		TEXT("ChatFrame1ResizeButton"),
		// Shown later by UPDATE_PENDING_MAIL when the player actually has mail.
		TEXT("MiniMapMailFrame"),
	};
	int32 ForceHiddenCount = 0;
	for (const TCHAR* FrameName : ForceHideFrames)
	{
		int64 H = FindFrame(FrameName);
		if (H >= 0)
		{
			SetFrameVisible(H, false);
			ForceHiddenCount++;
		}
	}
	if (ForceHiddenCount > 0)
	{
		UE_LOG(LogWowFrame, Log, TEXT("SyncChildVisibility: force-hid %d frames (FadingFrame pattern)"), ForceHiddenCount);
	}
	SanitizeBackpackOnlyContainers();

	// Keep persistent HUD overlays behind interactive panels. Stock FrameXML uses
	// strata for this relationship, but several inherited root frames otherwise
	// retain the MEDIUM fallback and can draw chat text over the character sheet.
	for (const auto& Pair : Frames)
	{
		const FString& FrameName = Pair.Value.Def.Name;
		if (FrameName.StartsWith(TEXT("ChatFrame")) ||
			FrameName.Equals(TEXT("GeneralDockManager")) ||
			FrameName.Equals(TEXT("WatchFrame")))
		{
			SetFrameStrata(Pair.Key, EWowFrameStrata::LOW);
		}
	}

	static const TCHAR* HighPanels[] = {
		TEXT("CharacterFrame"), TEXT("FriendsFrame"), TEXT("QuestLogFrame"),
		TEXT("SpellBookFrame"), TEXT("TalentFrame"), TEXT("PlayerTalentFrame"), TEXT("GuildFrame"),
		TEXT("MailFrame"), TEXT("MerchantFrame"), TEXT("BankFrame"),
		TEXT("TradeFrame"), TEXT("InterfaceOptionsFrame"), TEXT("VideoOptionsFrame"),
		TEXT("AudioOptionsFrame"), TEXT("KeyBindingFrame"), TEXT("MacroFrame")
	};
	for (const TCHAR* FrameName : HighPanels)
	{
		const int64 FrameHandle = FindFrame(FrameName);
		if (FrameHandle >= 0)
		{
			SetFrameStrata(FrameHandle, EWowFrameStrata::HIGH);
		}
	}
	for (int32 ContainerIndex = 1; ContainerIndex <= 13; ++ContainerIndex)
	{
		const int64 ContainerHandle = FindFrame(
			FString::Printf(TEXT("ContainerFrame%d"), ContainerIndex));
		if (ContainerHandle >= 0)
		{
			SetFrameStrata(ContainerHandle, EWowFrameStrata::DIALOG);
		}
	}
	for (const TCHAR* FrameName : {TEXT("GossipFrame"), TEXT("QuestFrame"), TEXT("LootFrame")})
	{
		const int64 FrameHandle = FindFrame(FrameName);
		if (FrameHandle >= 0)
		{
			SetFrameStrata(FrameHandle, EWowFrameStrata::DIALOG);
		}
	}
	if (const int64 WorldMapHandle = FindFrame(TEXT("WorldMapFrame")); WorldMapHandle >= 0)
	{
		SetFrameStrata(WorldMapHandle, EWowFrameStrata::FULLSCREEN);
	}
	for (int32 PopupIndex = 1; PopupIndex <= 4; ++PopupIndex)
	{
		const int64 PopupHandle = FindFrame(FString::Printf(TEXT("StaticPopup%d"), PopupIndex));
		if (PopupHandle >= 0)
		{
			SetFrameStrata(PopupHandle, EWowFrameStrata::FULLSCREEN_DIALOG);
		}
	}
	for (const TCHAR* FrameName : {TEXT("GameTooltip"), TEXT("ItemRefTooltip"), TEXT("ShoppingTooltip1"), TEXT("ShoppingTooltip2")})
	{
		const int64 TooltipHandle = FindFrame(FrameName);
		if (TooltipHandle >= 0)
		{
			SetFrameStrata(TooltipHandle, EWowFrameStrata::TOOLTIP);
		}
	}

	// Hide Flash textures on action buttons — these start visible but should be hidden
	// until the FrameXML combat flash animation activates them
	int32 FlashHiddenCount = 0;
	for (auto& TexPair : NamedTextureWidgets)
	{
		if (TexPair.Key.Contains(TEXT("Flash")) && TexPair.Value.IsValid())
		{
			TexPair.Value->SetVisibility(ESlateVisibility::Collapsed);
			FlashHiddenCount++;
		}
	}
	if (FlashHiddenCount > 0)
	{
		UE_LOG(LogWowFrame, Log, TEXT("SyncChildVisibility: hid %d Flash textures"), FlashHiddenCount);
	}

	// ChatFrame1 itself stays available for message text, but its border and
	// background textures are normally transparent until mouse-over.  The Lua
	// fade controller is not complete yet, so initialise just those decorations
	// to transparent; a later Texture:SetAlpha call can still reveal them.
	static const TCHAR* ChatDecorationNames[] = {
		TEXT("ChatFrame1Background"),
		TEXT("ChatFrame1TopLeftTexture"),
		TEXT("ChatFrame1BottomLeftTexture"),
		TEXT("ChatFrame1TopRightTexture"),
		TEXT("ChatFrame1BottomRightTexture"),
		TEXT("ChatFrame1LeftTexture"),
		TEXT("ChatFrame1RightTexture"),
		TEXT("ChatFrame1BottomTexture"),
		TEXT("ChatFrame1TopTexture"),
	};
	for (const TCHAR* TextureName : ChatDecorationNames)
	{
		if (TWeakObjectPtr<UImage>* Image = NamedTextureWidgets.Find(TextureName);
			Image && Image->IsValid())
		{
			Image->Get()->SetRenderOpacity(0.0f);
		}
	}

	// Start with two useful chat tabs. The remaining FrameXML slots stay available
	// for future custom/addon chat windows and are handled by the responsive dock.
	if (EnabledChatTabs.IsEmpty())
	{
		EnabledChatTabs.Add(1);
		EnabledChatTabs.Add(2);
		ChatTabLabels.Add(1, TEXT("General"));
		ChatTabLabels.Add(2, TEXT("Combat Log"));
	}
	const int64 GeneralFrameHandle = FindFrame(TEXT("ChatFrame1"));
	const int64 CombatFrameHandle = FindFrame(TEXT("ChatFrame2"));
	const int64 GeneralTabHandle = FindFrame(TEXT("ChatFrame1Tab"));
	const int64 CombatTabHandle = FindFrame(TEXT("ChatFrame2Tab"));
	UWidget* GeneralFrameWidget = GetWidgetForHandle(GeneralFrameHandle);
	UWidget* CombatFrameWidget = GetWidgetForHandle(CombatFrameHandle);
	UWidget* GeneralTabWidget = GetWidgetForHandle(GeneralTabHandle);
	UWidget* CombatTabWidget = GetWidgetForHandle(CombatTabHandle);
	if (GeneralFrameWidget && CombatFrameWidget)
	{
		if (UCanvasPanelSlot* GeneralSlot = Cast<UCanvasPanelSlot>(GeneralFrameWidget->Slot))
		{
			if (UCanvasPanelSlot* CombatSlot = Cast<UCanvasPanelSlot>(CombatFrameWidget->Slot))
			{
				CombatSlot->SetPosition(GeneralSlot->GetPosition());
				CombatSlot->SetSize(GeneralSlot->GetSize());
			}

			const FVector2D ChatPosition = GeneralSlot->GetPosition();
			const FVector2D ChatSize = GeneralSlot->GetSize();
			if (ChatSize.X > 0.0f && ChatSize.Y > 0.0f) GNativeChatFrameWidget = GeneralFrameWidget; // r314
			const FVector2D TabPosition(ChatPosition.X, ChatPosition.Y - 22.0f * UIScale);
			if (UCanvasPanelSlot* TabSlot = GeneralTabWidget ? Cast<UCanvasPanelSlot>(GeneralTabWidget->Slot) : nullptr)
			{
				TabSlot->SetPosition(TabPosition);
				TabSlot->SetSize(FVector2D(72.0f * UIScale, 28.0f * UIScale));
				TabSlot->SetZOrder(20000);
			}
			if (UCanvasPanelSlot* TabSlot = CombatTabWidget ? Cast<UCanvasPanelSlot>(CombatTabWidget->Slot) : nullptr)
			{
				TabSlot->SetPosition(TabPosition + FVector2D(74.0f * UIScale, 0.0f));
				TabSlot->SetSize(FVector2D(94.0f * UIScale, 28.0f * UIScale));
				TabSlot->SetZOrder(20000);
			}

			// Restore the authored friends/chat/scroll controls as a compact column
			// beside the message surface. Their inherited anchors resolve against a
			// temporary 430x120 parent and otherwise strand them inside the frame.
			auto PlaceRootControl = [this](const TCHAR* Name, const FVector2D& Position,
				const FVector2D& Size, int32 ZOrder)
			{
				const int64 ControlHandle = FindFrame(Name);
				SetFrameVisible(ControlHandle, true);
				if (UWidget* Control = GetWidgetForHandle(ControlHandle))
				{
					if (UCanvasPanelSlot* ControlSlot = Cast<UCanvasPanelSlot>(Control->Slot))
					{
						ControlSlot->SetAutoSize(false);
						ControlSlot->SetPosition(Position);
						ControlSlot->SetSize(Size);
						ControlSlot->SetZOrder(ZOrder);
					}
				}
			};

			const float ControlSize = 32.0f * UIScale;
			const float ControlGap = 2.0f * UIScale;
			const float ControlX = ChatPosition.X - 30.0f * UIScale;
			PlaceRootControl(TEXT("FriendsMicroButton"),
				FVector2D(ControlX, ChatPosition.Y),
				FVector2D(ControlSize), 21010);
			auto PlaceFriendsOnlineCount = [this, ControlSize](UTextBlock* FriendsOnlineCount)
			{
				if (!FriendsOnlineCount) return;
				if (UCanvasPanelSlot* CountSlot = Cast<UCanvasPanelSlot>(FriendsOnlineCount->Slot))
				{
					CountSlot->SetAnchors(FAnchors(0.0f, 0.0f));
					CountSlot->SetAlignment(FVector2D::ZeroVector);
					CountSlot->SetAutoSize(false);
					CountSlot->SetPosition(FVector2D(0.0f, 18.0f * UIScale));
					CountSlot->SetSize(FVector2D(ControlSize, 12.0f * UIScale));
					CountSlot->SetZOrder(21030);
				}
				FriendsOnlineCount->SetJustification(ETextJustify::Center);
				FriendsOnlineCount->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			};
			bool bPlacedNamedFriendsCount = false;
			for (auto& FontPair : NamedFontStringWidgets)
			{
				if (FontPair.Key.StartsWith(TEXT("FriendsMicroButton")) && FontPair.Value.IsValid())
				{
					PlaceFriendsOnlineCount(FontPair.Value.Get());
					bPlacedNamedFriendsCount = true;
				}
			}
			if (!bPlacedNamedFriendsCount)
			{
				PlaceFriendsOnlineCount(GetPrimaryTextWidget(FindFrame(TEXT("FriendsMicroButton"))));
			}
			PlaceRootControl(TEXT("ChatFrameMenuButton"),
				FVector2D(ControlX, ChatPosition.Y + 34.0f * UIScale),
				FVector2D(ControlSize), 21010);

			// The stock chat menu allocates 32 reusable rows and relies on Lua to
			// populate/hide them. Provide the five useful chat modes natively so the
			// speech-bubble control never reveals a full-screen stack of empty rows.
			const FVector2D ChatMenuPosition(ChatPosition.X,
				ChatPosition.Y - 92.0f * UIScale);
			const int64 ChatMenuHandle = FindFrame(TEXT("ChatMenu"));
			if (UWidget* ChatMenuWidget = GetWidgetForHandle(ChatMenuHandle))
			{
				if (UCanvasPanelSlot* MenuSlot = Cast<UCanvasPanelSlot>(ChatMenuWidget->Slot))
				{
					MenuSlot->SetAutoSize(false);
					MenuSlot->SetPosition(ChatMenuPosition);
					MenuSlot->SetSize(FVector2D(128.0f * UIScale, 90.0f * UIScale));
					MenuSlot->SetZOrder(22000);
				}
				if (UCanvasPanel* MenuCanvas = Cast<UCanvasPanel>(ChatMenuWidget))
				{
					UImage* MenuBackground = GetNamedTexture(TEXT("ChatMenuNativeBackground"));
					if (!MenuBackground)
					{
						MenuBackground = NewObject<UImage>(MenuCanvas);
						MenuBackground->SetBrush(FSlateColorBrush(FLinearColor::White));
						MenuCanvas->AddChildToCanvas(MenuBackground);
						NamedTextureWidgets.Add(TEXT("ChatMenuNativeBackground"), MenuBackground);
					}
					MenuBackground->SetColorAndOpacity(FWowWindowStyle::ChatBackgroundColor());
					MenuBackground->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
					if (UCanvasPanelSlot* BackgroundSlot = Cast<UCanvasPanelSlot>(MenuBackground->Slot))
					{
						BackgroundSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
						BackgroundSlot->SetOffsets(FMargin(0.0f));
						BackgroundSlot->SetZOrder(-10);
					}
				}
			}

			static const TCHAR* NativeChatMenuLabels[] = {
				TEXT("Say"), TEXT("Party"), TEXT("Guild"), TEXT("Yell"), TEXT("Whisper")
			};
			for (int32 MenuIndex = 1; MenuIndex <= 32; ++MenuIndex)
			{
				const int64 MenuButtonHandle = FindFrame(
					*FString::Printf(TEXT("ChatMenuButton%d"), MenuIndex));
				const bool bUsedRow = MenuIndex <= UE_ARRAY_COUNT(NativeChatMenuLabels);
				if (FWowFrameDef* MenuButtonDef = GetMutableFrameDef(MenuButtonHandle))
				{
					MenuButtonDef->bHidden = !bUsedRow;
				}
				if (UWidget* MenuButton = GetWidgetForHandle(MenuButtonHandle))
				{
					MenuButton->SetVisibility(ESlateVisibility::Collapsed);
					if (UCanvasPanelSlot* MenuButtonSlot = Cast<UCanvasPanelSlot>(MenuButton->Slot))
					{
						MenuButtonSlot->SetAutoSize(false);
						MenuButtonSlot->SetPosition(ChatMenuPosition + FVector2D(0.0f,
							(MenuIndex - 1) * 18.0f * UIScale));
						MenuButtonSlot->SetSize(FVector2D(128.0f * UIScale, 18.0f * UIScale));
						MenuButtonSlot->SetZOrder(22010 + MenuIndex);
					}
				}
				if (bUsedRow)
				{
					if (UTextBlock* MenuLabel = GetPrimaryTextWidget(MenuButtonHandle))
					{
						MenuLabel->SetText(FText::FromString(NativeChatMenuLabels[MenuIndex - 1]));
						MenuLabel->SetJustification(ETextJustify::Left);
						MenuLabel->SetColorAndOpacity(FSlateColor(WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f)));
					}
				}
			}
			PlaceRootControl(TEXT("ChatFrame1ButtonFrame"),
				FVector2D(-30.0f * UIScale, 20.0f * UIScale),
				FVector2D(ControlSize, ControlSize * 3.0f + ControlGap * 2.0f), 21000);

			auto PlaceScrollButton = [this, ControlSize, ControlGap](const TCHAR* Name, int32 Row)
			{
				const int64 ButtonHandle = FindFrame(Name);
				SetFrameVisible(ButtonHandle, true);
				if (UWidget* Button = GetWidgetForHandle(ButtonHandle))
				{
					if (UCanvasPanelSlot* ButtonSlot = Cast<UCanvasPanelSlot>(Button->Slot))
					{
						ButtonSlot->SetAutoSize(false);
						ButtonSlot->SetPosition(FVector2D(0.0f, Row * (ControlSize + ControlGap)));
						ButtonSlot->SetSize(FVector2D(ControlSize));
						ButtonSlot->SetZOrder(21020 + Row);
					}
				}
			};
			PlaceScrollButton(TEXT("ChatFrame1ButtonFrameUpButton"), 0);
			PlaceScrollButton(TEXT("ChatFrame1ButtonFrameDownButton"), 1);
			PlaceScrollButton(TEXT("ChatFrame1ButtonFrameBottomButton"), 2);
			SetFrameVisible(FindFrame(TEXT("ChatFrame1ButtonFrameMinimizeButton")), false);

			static const TCHAR* ButtonFrameDecorationNames[] = {
				TEXT("ChatFrame1ButtonFrameBackground"),
				TEXT("ChatFrame1ButtonFrameTopLeftTexture"), TEXT("ChatFrame1ButtonFrameBottomLeftTexture"),
				TEXT("ChatFrame1ButtonFrameTopRightTexture"), TEXT("ChatFrame1ButtonFrameBottomRightTexture"),
				TEXT("ChatFrame1ButtonFrameLeftTexture"), TEXT("ChatFrame1ButtonFrameRightTexture"),
				TEXT("ChatFrame1ButtonFrameBottomTexture"), TEXT("ChatFrame1ButtonFrameTopTexture"),
			};
			for (const TCHAR* TextureName : ButtonFrameDecorationNames)
			{
				if (UImage* Decoration = GetNamedTexture(TextureName))
				{
					Decoration->SetRenderOpacity(0.0f);
				}
			}

			auto EnsureChatSurfacePlate = [this](int64 FrameHandle, const FString& Prefix)
			{
				UCanvasPanel* ChatCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(FrameHandle));
				if (!ChatCanvas) return;
				auto EnsurePlate = [this, ChatCanvas](const FString& Name, const FLinearColor& Color,
					const FMargin& Insets, int32 ZOrder)
				{
					UImage* Plate = GetNamedTexture(Name);
					if (!Plate)
					{
						Plate = NewObject<UImage>(ChatCanvas);
						if (UCanvasPanelSlot* PlateSlot = ChatCanvas->AddChildToCanvas(Plate))
						{
							PlateSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
							PlateSlot->SetOffsets(Insets);
							PlateSlot->SetZOrder(ZOrder);
						}
						NamedTextureWidgets.Add(Name, Plate);
					}
					Plate->SetBrush(FSlateRoundedBoxBrush(Color, 6.0f * UIScale));
					Plate->SetColorAndOpacity(FLinearColor::White);
					Plate->SetRenderOpacity(0.0f);
					Plate->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				};
				EnsurePlate(Prefix + TEXT("HoverBorder"), WowColor::FromWow(0.08f, 0.085f, 0.10f, 1.0f),
					FMargin(0.0f), -301);
				EnsurePlate(Prefix + TEXT("HoverBackground"), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f),
					FMargin(2.0f * UIScale), -300);
			};
			EnsureChatSurfacePlate(GeneralFrameHandle, TEXT("ChatFrame1"));
			EnsureChatSurfacePlate(CombatFrameHandle, TEXT("ChatFrame2"));
		}
	}

	auto EnsureChatTabLabel = [this](int64 TabHandle, const FString& LabelText)
	{
		UCanvasPanel* TabCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(TabHandle));
		if (!TabCanvas) return;
		const FString FrameName = GetFrameName(TabHandle);
		const FString ClientLabelName = FrameName + TEXT("ClientLabel");
		if (UButton* TabButton = GetButtonWidget(TabHandle))
		{
			FButtonStyle TransparentStyle;
			FSlateBrush EmptyBrush;
			EmptyBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
			TransparentStyle.SetNormal(EmptyBrush);
			TransparentStyle.SetHovered(EmptyBrush);
			TransparentStyle.SetPressed(EmptyBrush);
			TransparentStyle.SetDisabled(EmptyBrush);
			TransparentStyle.SetNormalPadding(FMargin(0.0f));
			TransparentStyle.SetPressedPadding(FMargin(0.0f));
			TabButton->SetStyle(TransparentStyle);
			TabButton->SetBackgroundColor(FLinearColor::Transparent);
		}

		auto EnsurePlate = [this, TabCanvas](const FString& PlateName, const FLinearColor& PlateColor,
			const FMargin& Insets, int32 ZOrder)
		{
			UImage* Plate = GetNamedTexture(PlateName);
			if (!Plate)
			{
				Plate = NewObject<UImage>(TabCanvas);
				if (UCanvasPanelSlot* PlateSlot = TabCanvas->AddChildToCanvas(Plate))
				{
					PlateSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
					PlateSlot->SetOffsets(Insets);
					PlateSlot->SetZOrder(ZOrder);
				}
				NamedTextureWidgets.Add(PlateName, Plate);
			}
			Plate->SetBrush(FSlateColorBrush(PlateColor));
			Plate->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		};

		// The inherited atlas fragments do not stretch reliably with the corrected
		// widths. Use one owned plate whose active fill visually continues into the
		// translucent message surface.
		// r289: black like the chat background; UpdateTabOpacity gives it the background's alpha.
		EnsurePlate(FrameName + TEXT("PlateOuter"), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f),
			FMargin(0.0f), 29000);
		EnsurePlate(FrameName + TEXT("PlateInner"), FLinearColor::Transparent,
			FMargin(2.0f * UIScale), 29001);
		if (UImage* OuterPlate = GetNamedTexture(FrameName + TEXT("PlateOuter")))
		{
			OuterPlate->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		if (UImage* InnerPlate = GetNamedTexture(FrameName + TEXT("PlateInner")))
		{
			InnerPlate->SetVisibility(ESlateVisibility::Collapsed);
		}

		// Do not stack the stock normal/selected/highlight atlas fragments behind
		// the owned plate. Their flattened FrameXML anchors were the detached black
		// rectangle seen beside the caption.
		static const TCHAR* AuthoredTabTextureSuffixes[] = {
			TEXT("Left"), TEXT("Middle"), TEXT("Right"),
			TEXT("SelectedLeft"), TEXT("SelectedMiddle"), TEXT("SelectedRight"),
			TEXT("HighlightLeft"), TEXT("HighlightMiddle"), TEXT("HighlightRight")
		};
		for (const TCHAR* Suffix : AuthoredTabTextureSuffixes)
		{
			if (UImage* Texture = GetNamedTexture(FrameName + Suffix))
			{
				Texture->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		if (UImage* Glow = GetNamedTexture(FrameName + TEXT("Glow")))
		{
			Glow->SetVisibility(ESlateVisibility::Collapsed);
		}

		// FrameXML can leave both a button caption and a separate FontString alive.
		// Own one caption and remove every inherited text descendant so repeated UI
		// refreshes can never build a visible stack of labels over the same tab.
		UTextBlock* Label = GetNamedFontString(ClientLabelName);
		TFunction<void(UPanelWidget*)> RemoveInheritedCaptions =
			[&RemoveInheritedCaptions, Label](UPanelWidget* Panel)
		{
			for (int32 ChildIndex = Panel->GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
			{
				UWidget* Child = Panel->GetChildAt(ChildIndex);
				if (UTextBlock* Text = Cast<UTextBlock>(Child))
				{
					if (Text != Label)
					{
						Text->SetText(FText::GetEmpty());
						Panel->RemoveChild(Text);
					}
				}
				else if (UPanelWidget* ChildPanel = Cast<UPanelWidget>(Child))
				{
					RemoveInheritedCaptions(ChildPanel);
				}
			}
		};
		RemoveInheritedCaptions(TabCanvas);
		if (!Label)
		{
			Label = NewObject<UTextBlock>(TabCanvas);
			if (UCanvasPanelSlot* LabelSlot = TabCanvas->AddChildToCanvas(Label))
			{
				LabelSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
				LabelSlot->SetOffsets(FMargin(0.0f));
				LabelSlot->SetZOrder(30000);
			}
			NamedFontStringWidgets.Add(ClientLabelName, Label);
		}
		PrimaryTextWidgets.Add(TabHandle, Label);

		Label->SetText(FText::FromString(LabelText));
		if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(Label->Slot))
		{
			LabelSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			LabelSlot->SetOffsets(FMargin(0.0f));
			LabelSlot->SetZOrder(30000);
		}
		Label->SetJustification(ETextJustify::Center);
		Label->SetColorAndOpacity(FSlateColor(WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f)));
		Label->SetShadowOffset(FVector2D(1.0f));
		Label->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.95f));
		Label->SetRenderTranslation(FVector2D::ZeroVector);
		Label->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		// r289: the caption in the tab's FrameXML font (GameFontNormalSmall), not the engine's Roboto.
		FSlateFontInfo LabelFont = FontManager && FontManager->IsInitialized()
			? FontManager->GetFont(TEXT("GameFontNormalSmall"), FMath::Max(9, FMath::RoundToInt(10.0f * UIScale)))
			: Label->GetFont();
		LabelFont.Size = FMath::Max(9, FMath::RoundToInt(10.0f * UIScale));
		Label->SetFont(LabelFont);
	};
	for (int32 ChatIndex = 1; ChatIndex <= 10; ++ChatIndex)
	{
		const int64 TabHandle = FindFrame(*FString::Printf(TEXT("ChatFrame%dTab"), ChatIndex));
		const FString Label = ChatTabLabels.Contains(ChatIndex)
			? ChatTabLabels[ChatIndex] : FString::Printf(TEXT("Chat %d"), ChatIndex);
		EnsureChatTabLabel(TabHandle, Label);
        SetFrameMouseEnabled(TabHandle,true);
        SetFrameRegisteredClicks(TabHandle,{TEXT("LeftButtonUp"),TEXT("RightButtonUp")});
		SetFrameVisible(TabHandle, EnabledChatTabs.Contains(ChatIndex));
	}

	// Reuse the stock dock overflow controls, correcting their inherited full-screen
	// geometry. Menu rows are native buttons so hidden tabs remain selectable.
	const int64 OverflowButtonHandle = FindFrame(TEXT("GeneralDockManagerOverflowButton"));
	const int64 OverflowListHandle = FindFrame(TEXT("GeneralDockManagerOverflowButtonList"));
	SetFrameVisible(OverflowButtonHandle, false);
	SetFrameVisible(OverflowListHandle, false);
	if (FindFrame(TEXT("ChatTabOverflowNativeButton")) < 0)
	{
		FWowFrameDef ButtonDef;
		ButtonDef.Name = TEXT("ChatTabOverflowNativeButton");
		ButtonDef.Type = EWowFrameType::Button;
		ButtonDef.Width = 20.0f;
		ButtonDef.Height = 22.0f;
		ButtonDef.Strata = EWowFrameStrata::DIALOG;
		ButtonDef.FrameLevel = 49;
		ButtonDef.ButtonText = TEXT("v");
		CreateFrame(ButtonDef);
	}
	SetFrameVisible(FindFrame(TEXT("ChatTabOverflowNativeButton")), false);
	SetFrameMouseEnabled(FindFrame(TEXT("ChatTabOverflowNativeButton")), true);
	if (UTextBlock* NativeOverflowLabel = GetPrimaryTextWidget(
		FindFrame(TEXT("ChatTabOverflowNativeButton"))))
	{
		NativeOverflowLabel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (UCanvasPanel* NativeButtonCanvas = Cast<UCanvasPanel>(
		GetWidgetForHandle(FindFrame(TEXT("ChatTabOverflowNativeButton")))))
	{
		UImage* ArrowImage = GetNamedTexture(TEXT("ChatTabOverflowNativeArrow"));
		if (!ArrowImage)
		{
			ArrowImage = NewObject<UImage>(NativeButtonCanvas);
			NativeButtonCanvas->AddChildToCanvas(ArrowImage);
			NamedTextureWidgets.Add(TEXT("ChatTabOverflowNativeArrow"), ArrowImage);
		}
		FSlateBrush ArrowBrush;
		ArrowBrush.SetResourceObject(LoadUITexture(
			TEXT("Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Up")));
		ArrowBrush.ImageSize = FVector2D(20.0f * UIScale, 22.0f * UIScale);
		ArrowImage->SetBrush(ArrowBrush);
		ArrowImage->SetColorAndOpacity(WowColor::FromWow(0.62f, 0.62f, 0.62f, 0.78f));
		ArrowImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ArrowImage->Slot))
		{
			Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			Slot->SetOffsets(FMargin(0.0f));
			Slot->SetZOrder(10);
		}
	}
	UTextBlock* OverflowLabel = GetPrimaryTextWidget(OverflowButtonHandle);
	if (!OverflowLabel)
	{
		if (UCanvasPanel* OverflowCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(OverflowButtonHandle)))
		{
			OverflowLabel = NewObject<UTextBlock>(OverflowCanvas);
			if (UCanvasPanelSlot* Slot = OverflowCanvas->AddChildToCanvas(OverflowLabel))
			{
				Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
				Slot->SetOffsets(FMargin(0.0f));
				Slot->SetZOrder(10);
			}
			PrimaryTextWidgets.Add(OverflowButtonHandle, OverflowLabel);
		}
	}
	if (OverflowLabel)
	{
		OverflowLabel->SetText(FText::FromString(TEXT("v")));
		OverflowLabel->SetJustification(ETextJustify::Center);
		OverflowLabel->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.8f, 0.7f, 0.35f, 0.9f)));
		OverflowLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (UCanvasPanel* OverflowCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(OverflowButtonHandle)))
	{
		UImage* ButtonBackground = GetNamedTexture(TEXT("ChatTabOverflowButtonBackground"));
		if (!ButtonBackground)
		{
			ButtonBackground = NewObject<UImage>(OverflowCanvas);
			ButtonBackground->SetBrush(FSlateColorBrush(FLinearColor::White));
			OverflowCanvas->AddChildToCanvas(ButtonBackground);
			NamedTextureWidgets.Add(TEXT("ChatTabOverflowButtonBackground"), ButtonBackground);
		}
		ButtonBackground->SetColorAndOpacity(FWowWindowStyle::ChatBackgroundColor());
		ButtonBackground->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ButtonBackground->Slot))
		{
			Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			Slot->SetOffsets(FMargin(1.0f * UIScale));
			Slot->SetZOrder(0);
		}
	}
	if (RootCanvas.IsValid())
	{
		UImage* Background = GetNamedTexture(TEXT("ChatTabOverflowRootBackground"));
		if (!Background)
		{
			Background = NewObject<UImage>(RootCanvas.Get());
			RootCanvas->AddChildToCanvas(Background);
			NamedTextureWidgets.Add(TEXT("ChatTabOverflowRootBackground"), Background);
		}
		const FSlateRoundedBoxBrush MenuBrush(
			FWowWindowStyle::ChatBackgroundColor(),
			5.0f * UIScale,
			WowColor::FromWow(0.42f, 0.42f, 0.45f, 0.92f),
			1.0f * UIScale);
		Background->SetBrush(MenuBrush);
		Background->SetColorAndOpacity(FLinearColor::White);
		Background->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (FindFrame(TEXT("ChatTabOverflowHeader")) < 0)
	{
		FWowFrameDef HeaderDef;
		HeaderDef.Name = TEXT("ChatTabOverflowHeader");
		HeaderDef.Type = EWowFrameType::Button;
		HeaderDef.Width = 150.0f;
		HeaderDef.Height = 22.0f;
		HeaderDef.Strata = EWowFrameStrata::DIALOG;
		HeaderDef.FrameLevel = 50;
		HeaderDef.ButtonText = TEXT("Chat Windows");
		CreateFrame(HeaderDef);
	}
	SetFrameVisible(FindFrame(TEXT("ChatTabOverflowHeader")), false);
	SetFrameMouseEnabled(FindFrame(TEXT("ChatTabOverflowHeader")), false);
	for (int32 ChatIndex = 1; ChatIndex <= 10; ++ChatIndex)
	{
		const FString RowName = FString::Printf(TEXT("ChatTabOverflowRow%d"), ChatIndex);
		if (FindFrame(RowName) < 0)
		{
			FWowFrameDef RowDef;
			RowDef.Name = RowName;
			RowDef.Type = EWowFrameType::Button;
			RowDef.Width = 150.0f;
			RowDef.Height = 18.0f;
			RowDef.Strata = EWowFrameStrata::DIALOG;
			RowDef.FrameLevel = 51 + ChatIndex;
			RowDef.ButtonText = FString::Printf(TEXT("Chat %d"), ChatIndex);
			CreateFrame(RowDef);
		}
		SetFrameVisible(FindFrame(RowName), false);
		SetFrameMouseEnabled(FindFrame(RowName), true);
		if (UTextBlock* RowLabel = GetPrimaryTextWidget(FindFrame(RowName)))
		{
			FSlateFontInfo RowFont = RowLabel->GetFont();
			RowFont.Size = FMath::Max(9, FMath::RoundToInt(9.0f * UIScale));
			RowLabel->SetFont(RowFont);
			RowLabel->SetShadowOffset(FVector2D(1.0f));
			RowLabel->SetShadowColorAndOpacity(FLinearColor::Black);
		}
	}

	// General has the full chat-window menu; Combat Log and custom tabs use the
	// shorter variant. The row labels are populated dynamically when opened.
	if (RootCanvas.IsValid())
	{
		UImage* ContextBackground = GetNamedTexture(TEXT("ChatTabContextBackground"));
		if (!ContextBackground)
		{
			ContextBackground = NewObject<UImage>(RootCanvas.Get());
			RootCanvas->AddChildToCanvas(ContextBackground);
			NamedTextureWidgets.Add(TEXT("ChatTabContextBackground"), ContextBackground);
		}
		const FSlateRoundedBoxBrush ContextBrush(
			FWowWindowStyle::ChatBackgroundColor(), 4.0f * UIScale,
			WowColor::FromWow(0.48f, 0.48f, 0.50f, 0.95f), 1.0f * UIScale);
		ContextBackground->SetBrush(ContextBrush);
		ContextBackground->SetColorAndOpacity(FLinearColor::White);
		ContextBackground->SetVisibility(ESlateVisibility::Collapsed);
	}
	for (int32 RowIndex = 1; RowIndex <= 10; ++RowIndex)
	{
		const FString RowName = FString::Printf(TEXT("ChatTabContextRow%d"), RowIndex);
		if (FindFrame(RowName) < 0)
		{
			FWowFrameDef RowDef;
			RowDef.Name = RowName;
			RowDef.Type = EWowFrameType::Button;
			RowDef.Width = 132.0f;
			RowDef.Height = 15.0f;
			RowDef.Strata = EWowFrameStrata::DIALOG;
			RowDef.FrameLevel = 81 + RowIndex;
			RowDef.ButtonText = TEXT("Chat option");
			CreateFrame(RowDef);
		}
		SetFrameVisible(FindFrame(RowName), false);
		SetFrameMouseEnabled(FindFrame(RowName), true);
		if (UTextBlock* Label = GetPrimaryTextWidget(FindFrame(RowName)))
		{
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = FMath::Max(8, FMath::RoundToInt(8.0f * UIScale));
			Label->SetFont(Font);
			Label->SetJustification(ETextJustify::Left);
			Label->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.92f, 0.88f, 0.75f, 1.0f)));
			Label->SetShadowOffset(FVector2D(1.0f));
			Label->SetShadowColorAndOpacity(FLinearColor::Black);
		}
	}

	// Combat Log owns a second, compact filter strip.  Keep it native for now:
	// the 3.3.5 FrameXML dropdown depends on combat-log glue that is not yet
	// complete, and exposing that half-built menu makes the client unstable.
	static const TCHAR* CombatFilterNames[] = {
		TEXT("CombatLogFilterSelf"), TEXT("CombatLogFilterEverything"),
		TEXT("CombatLogFilterWhatHappened"), TEXT("CombatLogFilterDropDownButton")
	};
	static const TCHAR* CombatFilterLabels[] = {
		TEXT("Self"), TEXT("Everything"), TEXT("What happened to me?"), TEXT("")
	};
	for (int32 FilterIndex = 0; FilterIndex < UE_ARRAY_COUNT(CombatFilterNames); ++FilterIndex)
	{
		if (FindFrame(CombatFilterNames[FilterIndex]) < 0)
		{
			FWowFrameDef FilterDef;
			FilterDef.Name = CombatFilterNames[FilterIndex];
			FilterDef.Type = EWowFrameType::Button;
			FilterDef.Width = FilterIndex == 2 ? 148.0f : (FilterIndex == 3 ? 18.0f : 78.0f);
			FilterDef.Height = 22.0f;
			FilterDef.Strata = EWowFrameStrata::DIALOG;
			FilterDef.FrameLevel = 75 + FilterIndex;
			FilterDef.ButtonText = CombatFilterLabels[FilterIndex];
			CreateFrame(FilterDef);
		}
		SetFrameVisible(FindFrame(CombatFilterNames[FilterIndex]), false);
		SetFrameMouseEnabled(FindFrame(CombatFilterNames[FilterIndex]), true);
		if (UTextBlock* Label = GetPrimaryTextWidget(FindFrame(CombatFilterNames[FilterIndex])))
		{
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = FMath::Max(8, FMath::RoundToInt((FilterIndex == 3 ? 9.0f : 10.0f) * UIScale));
			Label->SetFont(Font);
			Label->SetJustification(ETextJustify::Center);
			Label->SetShadowOffset(FVector2D(1.0f));
			Label->SetShadowColorAndOpacity(FLinearColor::Black);
		}
	}
	if (UCanvasPanel* DropDownCanvas = Cast<UCanvasPanel>(
		GetWidgetForHandle(FindFrame(TEXT("CombatLogFilterDropDownButton")))))
	{
		UImage* ArrowImage = GetNamedTexture(TEXT("CombatLogFilterDropDownArrowImage"));
		if (!ArrowImage)
		{
			ArrowImage = NewObject<UImage>(DropDownCanvas);
			DropDownCanvas->AddChildToCanvas(ArrowImage);
			NamedTextureWidgets.Add(TEXT("CombatLogFilterDropDownArrowImage"), ArrowImage);
		}
		FSlateBrush ArrowBrush;
		ArrowBrush.SetResourceObject(LoadUITexture(
			TEXT("Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Up")));
		ArrowBrush.ImageSize = FVector2D(18.0f * UIScale);
		ArrowImage->SetBrush(ArrowBrush);
		ArrowImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ArrowImage->Slot))
		{
			Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			Slot->SetOffsets(FMargin(0.0f));
			Slot->SetZOrder(10);
		}
	}
	if (RootCanvas.IsValid())
	{
		UImage* FilterStrip = GetNamedTexture(TEXT("CombatLogFilterStripBackground"));
		if (!FilterStrip)
		{
			FilterStrip = NewObject<UImage>(RootCanvas.Get());
			RootCanvas->AddChildToCanvas(FilterStrip);
			NamedTextureWidgets.Add(TEXT("CombatLogFilterStripBackground"), FilterStrip);
		}
		FilterStrip->SetBrush(FSlateRoundedBoxBrush(
			WowColor::FromWow(0.008f, 0.009f, 0.012f, 0.68f), 2.0f * UIScale,
			WowColor::FromWow(0.13f, 0.13f, 0.15f, 0.55f), 1.0f * UIScale));
		FilterStrip->SetVisibility(ESlateVisibility::Collapsed);

		UImage* DropDownPlate = GetNamedTexture(TEXT("CombatLogFilterDropDownBackground"));
		if (!DropDownPlate)
		{
			DropDownPlate = NewObject<UImage>(RootCanvas.Get());
			RootCanvas->AddChildToCanvas(DropDownPlate);
			NamedTextureWidgets.Add(TEXT("CombatLogFilterDropDownBackground"), DropDownPlate);
		}
		DropDownPlate->SetBrush(FSlateRoundedBoxBrush(
			FWowWindowStyle::ChatBackgroundColor(), 2.0f * UIScale,
			WowColor::FromWow(0.38f, 0.34f, 0.22f, 0.90f), 1.0f * UIScale));
		DropDownPlate->SetVisibility(ESlateVisibility::Collapsed);

		UImage* FilterMenu = GetNamedTexture(TEXT("CombatLogFilterMenuBackground"));
		if (!FilterMenu)
		{
			FilterMenu = NewObject<UImage>(RootCanvas.Get());
			RootCanvas->AddChildToCanvas(FilterMenu);
			NamedTextureWidgets.Add(TEXT("CombatLogFilterMenuBackground"), FilterMenu);
		}
		FilterMenu->SetBrush(FSlateRoundedBoxBrush(
			FWowWindowStyle::ChatBackgroundColor(), 4.0f * UIScale,
			WowColor::FromWow(0.48f, 0.48f, 0.50f, 0.95f), 1.0f * UIScale));
		FilterMenu->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (FindFrame(TEXT("CombatLogFilterKills")) < 0)
	{
		FWowFrameDef KillsDef;
		KillsDef.Name = TEXT("CombatLogFilterKills");
		KillsDef.Type = EWowFrameType::Button;
		KillsDef.Width = 72.0f;
		KillsDef.Height = 24.0f;
		KillsDef.Strata = EWowFrameStrata::DIALOG;
		KillsDef.FrameLevel = 90;
		KillsDef.ButtonText = TEXT("   Kills");
		CreateFrame(KillsDef);
	}
	SetFrameVisible(FindFrame(TEXT("CombatLogFilterKills")), false);
	SetFrameMouseEnabled(FindFrame(TEXT("CombatLogFilterKills")), true);
	if (UTextBlock* Label = GetPrimaryTextWidget(FindFrame(TEXT("CombatLogFilterKills"))))
	{
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FMath::Max(8, FMath::RoundToInt(9.0f * UIScale));
		Label->SetFont(Font);
		Label->SetJustification(ETextJustify::Left);
		Label->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.95f, 0.90f, 0.68f, 1.0f)));
		Label->SetShadowOffset(FVector2D(1.0f));
		Label->SetShadowColorAndOpacity(FLinearColor::Black);
	}
	if (UCanvasPanel* KillsCanvas = Cast<UCanvasPanel>(
		GetWidgetForHandle(FindFrame(TEXT("CombatLogFilterKills")))))
	{
		UImage* CheckImage = GetNamedTexture(TEXT("CombatLogFilterKillsCheckImage"));
		if (!CheckImage)
		{
			CheckImage = NewObject<UImage>(KillsCanvas);
			KillsCanvas->AddChildToCanvas(CheckImage);
			NamedTextureWidgets.Add(TEXT("CombatLogFilterKillsCheckImage"), CheckImage);
		}
		FSlateBrush CheckBrush;
		CheckBrush.SetResourceObject(LoadUITexture(TEXT("Interface\\Buttons\\UI-CheckBox-Check")));
		CheckBrush.ImageSize = FVector2D(12.0f * UIScale);
		CheckImage->SetBrush(CheckBrush);
		CheckImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(CheckImage->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition(FVector2D(1.0f, 5.0f) * UIScale);
			Slot->SetSize(FVector2D(12.0f * UIScale));
			Slot->SetZOrder(10);
		}
	}

	// The stock FriendsFrame artwork imports correctly, but its list and tab
	// labels depend on Lua layout calls that are not reliable yet. Add a native
	// data layer over that authored frame; all artwork still comes from MPQ.
	auto EnsureSocialTextButton = [this](const FString& Name, const FString& Text, bool bMouseEnabled)
	{
		if (FindFrame(Name) < 0)
		{
			FWowFrameDef Def;
			Def.Name = Name;
			Def.Type = EWowFrameType::Button;
			Def.Width = 200.0f;
			Def.Height = 20.0f;
			Def.Strata = EWowFrameStrata::DIALOG;
			Def.FrameLevel = 120;
			Def.ButtonText = Text;
			CreateFrame(Def);
		}
		SetFrameVisible(FindFrame(Name), false);
		SetFrameMouseEnabled(FindFrame(Name), bMouseEnabled);
		if (UTextBlock* Label = GetPrimaryTextWidget(FindFrame(Name)))
		{
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = FMath::Max(9, FMath::RoundToInt(10.0f * UIScale));
			Label->SetFont(Font);
			Label->SetJustification(ETextJustify::Left);
			Label->SetShadowOffset(FVector2D(1.0f));
			Label->SetShadowColorAndOpacity(FLinearColor::Black);
			Label->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.95f, 0.82f, 0.35f, 1.0f)));
		}
	};
	EnsureSocialTextButton(TEXT("SocialNativeTitle"), TEXT("Friends List"), false);
	EnsureSocialTextButton(TEXT("SocialNativeSummary"), TEXT("Online"), false);
	EnsureSocialTextButton(TEXT("SocialNativeRecent"), TEXT("No recent whispers"), false);
	EnsureSocialTextButton(TEXT("SocialNativeAddFriendButton"), TEXT("Add Friend"), true);
	EnsureSocialTextButton(TEXT("SocialNativeReplyButton"), TEXT("Send Message"), true);
	EnsureSocialTextButton(TEXT("SocialNativeEmpty"), TEXT("You have no friends listed."), false);
	EnsureSocialTextButton(TEXT("SocialNativeListTab1"), TEXT("Friends"), true);
	EnsureSocialTextButton(TEXT("SocialNativeListTab2"), TEXT("Ignore"), true);
	static const TCHAR* SocialTabs[] = {TEXT("Friends"), TEXT("Who"), TEXT("Guild"), TEXT("Chat"), TEXT("Raid")};
	for (int32 Index = 1; Index <= UE_ARRAY_COUNT(SocialTabs); ++Index)
	{
		EnsureSocialTextButton(FString::Printf(TEXT("SocialNativeTab%d"), Index), SocialTabs[Index - 1], true);
	}
	for (int32 Index = 1; Index <= 8; ++Index)
	{
		EnsureSocialTextButton(FString::Printf(TEXT("SocialNativeFriendRow%d"), Index), TEXT("Friend"), true);
	}
	if (RootCanvas.IsValid())
	{
		static const TCHAR* SocialPlateNames[] = {
			TEXT("SocialNativeAddFriendPlate"), TEXT("SocialNativeReplyPlate"),
			TEXT("SocialNativeListTab1Plate"), TEXT("SocialNativeListTab2Plate"),
			TEXT("SocialNativeTab1Plate"), TEXT("SocialNativeTab2Plate"),
			TEXT("SocialNativeTab3Plate"), TEXT("SocialNativeTab4Plate"),
			TEXT("SocialNativeTab5Plate"), TEXT("SocialNativeFriendContextPlate"),
			TEXT("SocialNativeFriendRow1Plate"), TEXT("SocialNativeFriendRow2Plate"),
			TEXT("SocialNativeFriendRow3Plate"), TEXT("SocialNativeFriendRow4Plate"),
			TEXT("SocialNativeFriendRow5Plate"), TEXT("SocialNativeFriendRow6Plate"),
			TEXT("SocialNativeFriendRow7Plate"), TEXT("SocialNativeFriendRow8Plate")
		};
		for (const TCHAR* PlateName : SocialPlateNames)
		{
			UImage* Plate = GetNamedTexture(PlateName);
			if (!Plate)
			{
				Plate = NewObject<UImage>(RootCanvas.Get());
				RootCanvas->AddChildToCanvas(Plate);
				NamedTextureWidgets.Add(PlateName, Plate);
			}
			Plate->SetBrush(FSlateRoundedBoxBrush(
				FWowWindowStyle::ChatBackgroundColor(), 3.0f * UIScale,
				WowColor::FromWow(0.43f, 0.43f, 0.46f, 0.92f), 1.0f * UIScale));
			Plate->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	static const TCHAR* FriendContextLabels[] = {
		TEXT("Whisper"), TEXT("Invite to Group"), TEXT("Remove Friend"), TEXT("Close")
	};
	for (int32 Index = 1; Index <= UE_ARRAY_COUNT(FriendContextLabels); ++Index)
	{
		EnsureSocialTextButton(FString::Printf(TEXT("SocialNativeFriendContextRow%d"), Index),
			FriendContextLabels[Index - 1], true);
	}

	// Use a root-level grabber so it remains available while any chat tab is
	// selected. It is hidden until the tab menu explicitly unlocks the window.
	if (FindFrame(TEXT("ChatFrameNativeResizeButton")) < 0)
	{
		FWowFrameDef ResizeDef;
		ResizeDef.Name = TEXT("ChatFrameNativeResizeButton");
		ResizeDef.Type = EWowFrameType::Button;
		ResizeDef.Width = 16.0f;
		ResizeDef.Height = 16.0f;
		ResizeDef.Strata = EWowFrameStrata::DIALOG;
		ResizeDef.FrameLevel = 78;
		CreateFrame(ResizeDef);
	}
	const int64 ResizeHandle = FindFrame(TEXT("ChatFrameNativeResizeButton"));
	// r292: General starts unlocked like the client's default chat window (right-click the tab > Lock Window).
	{
		static bool bChatLockDefaulted = false;
		if (!bChatLockDefaulted) { bChatLockDefaulted = true; bChatFrameUnlocked = true; }
	}
	SetFrameVisible(ResizeHandle, false);
	SetFrameMouseEnabled(ResizeHandle, true);
	if (UCanvasPanel* ResizeCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(ResizeHandle)))
	{
		UImage* ResizeImage = GetNamedTexture(TEXT("ChatFrameNativeResizeImage"));
		if (!ResizeImage)
		{
			ResizeImage = NewObject<UImage>(ResizeCanvas);
			ResizeCanvas->AddChildToCanvas(ResizeImage);
			NamedTextureWidgets.Add(TEXT("ChatFrameNativeResizeImage"), ResizeImage);
		}
		FSlateBrush ResizeBrush;
		ResizeBrush.SetResourceObject(LoadUITexture(
			TEXT("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Up")));
		ResizeBrush.ImageSize = FVector2D(16.0f * UIScale);
		ResizeImage->SetBrush(ResizeBrush);
		ResizeImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ResizeImage->Slot))
		{
			Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			Slot->SetOffsets(FMargin(0.0f));
			Slot->SetZOrder(10);
		}
	}
	SetActiveChatTab(1);

	// Calendar invite/alarm overlays are conditional decorations on the minimap
	// time button. They must stay hidden until the corresponding calendar event
	// explicitly reveals them; otherwise their large atlas regions appear as
	// extra narrow controls beside the minimap.
	static const TCHAR* CalendarNotificationTextures[] = {
		TEXT("GameTimeCalendarInvitesTexture"),
		TEXT("GameTimeCalendarInvitesGlow"),
		TEXT("GameTimeCalendarEventAlarmTexture"),
	};
	for (const TCHAR* TextureName : CalendarNotificationTextures)
	{
		if (TWeakObjectPtr<UImage>* Image = NamedTextureWidgets.Find(TextureName);
			Image && Image->IsValid())
		{
			Image->Get()->SetRenderOpacity(0.0f);
			Image->Get()->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Experience text is an on-hover detail in the stock HUD.  Its initial
	// numeric value otherwise appears as a detached "0" while the hover/fade
	// scripts are still incomplete.
	static const TCHAR* HoverOnlyFontStrings[] = {
		TEXT("MainMenuExpBarOverlayFrameTextLeft"),
		TEXT("MainMenuExpBarOverlayFrameTextRight"),
	};
	for (const TCHAR* FontStringName : HoverOnlyFontStrings)
	{
		if (TWeakObjectPtr<UTextBlock>* Label = NamedFontStringWidgets.Find(FontStringName);
			Label && Label->IsValid())
		{
			Label->Get()->SetRenderOpacity(0.0f);
		}
	}

	// Keep the central XP readout visible like the 3.3.5 action-bar reference;
	// only its detailed left/right values remain hover-only.
	UTextBlock* XPLabel = GetNamedFontString(TEXT("MainMenuExpBarOverlayFrameText"));
	if (!XPLabel)
	{
		const int64 XPBarHandle = FindFrame(TEXT("MainMenuExpBar"));
		if (UCanvasPanel* XPBarCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(XPBarHandle)))
		{
			XPLabel = NewObject<UTextBlock>(XPBarCanvas);
			if (UCanvasPanelSlot* XPSlot = XPBarCanvas->AddChildToCanvas(XPLabel))
			{
				XPSlot->SetPosition(FVector2D::ZeroVector);
				XPSlot->SetSize(FVector2D(1024.0f * UIScale, 13.0f * UIScale));
				XPSlot->SetZOrder(500);
			}
			XPLabel->SetJustification(ETextJustify::Center);
			NamedFontStringWidgets.Add(TEXT("MainMenuExpBarOverlayFrameText"), XPLabel);
			if (EventSystem)
			{
				EventSystem->CreateTextureRegionGlobal(TEXT("MainMenuExpBarOverlayFrameText"), XPBarHandle, TEXT("FontString"));
			}
		}
	}
	if (XPLabel)
	{
		FSlateFontInfo XPFont = XPLabel->GetFont();
		XPFont.Size = FMath::RoundToInt(6.0f * UIScale);
		XPLabel->SetFont(XPFont);
		XPLabel->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.82f, 0.82f, 0.82f, 1.0f)));
		XPLabel->SetShadowOffset(FVector2D(1.0f));
		XPLabel->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		XPLabel->SetRenderOpacity(1.0f);
	}

	// ExhaustionTick marks the end of rested XP; it is not the current-XP
	// cursor. GetXPExhaustion currently reports zero until the server update
	// field is wired, so the stock frame must remain hidden instead of falling
	// back to the centre of the bar and obscuring its progress text.
	if (UWidget* ExhaustionTickWidget = GetWidgetForHandle(FindFrame(TEXT("ExhaustionTick"))))
	{
		ExhaustionTickWidget->SetVisibility(ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* ExhaustionSlot = Cast<UCanvasPanelSlot>(ExhaustionTickWidget->Slot))
		{
			// When rested XP support enables the marker later, keep the XP bar and
			// its centred label above the marker's wide tooltip hit region.
			ExhaustionSlot->SetZOrder(-1);
		}
	}
	if (TWeakObjectPtr<UImage>* ExhaustionFill = NamedTextureWidgets.Find(TEXT("ExhaustionLevelFillBar"));
		ExhaustionFill && ExhaustionFill->IsValid())
	{
		ExhaustionFill->Get()->SetVisibility(ESlateVisibility::Collapsed);
		ExhaustionFill->Get()->SetRenderOpacity(0.0f);
	}

	RefreshMainActionBarControls();

	// The parsed button row lands eight WoW units below the recesses painted in
	// UI-MainMenuBar-Dwarf.  Use scale-relative design coordinates so the button
	// faces remain registered with that artwork at every global UI scale.
	for (int32 ButtonIndex = 1; ButtonIndex <= 12; ++ButtonIndex)
	{
		const int64 ActionButtonHandle = FindFrame(FString::Printf(TEXT("ActionButton%d"), ButtonIndex));
		UWidget* ActionButtonWidget = GetWidgetForHandle(ActionButtonHandle);
		if (ActionButtonWidget)
		{
			if (UCanvasPanelSlot* ActionSlot = Cast<UCanvasPanelSlot>(ActionButtonWidget->Slot))
			{
				FVector2D Position = ActionSlot->GetPosition();
				Position.Y = 14.0f * UIScale;
				ActionSlot->SetPosition(Position);
			}
		}

		// Fill almost the complete interior recess, retaining a narrow rim of the
		// authored artwork for its bevel. A populated spell/item icon naturally
		// covers this neutral backing when the slot receives an action.
		const FString BackingName = FString::Printf(TEXT("ActionButton%dEmptyBacking"), ButtonIndex);
		UImage* EmptyBacking = nullptr;
		if (TWeakObjectPtr<UImage>* ExistingBacking = NamedTextureWidgets.Find(BackingName);
			ExistingBacking && ExistingBacking->IsValid())
		{
			EmptyBacking = ExistingBacking->Get();
		}
		else if (UCanvasPanel* ActionButtonCanvas = Cast<UCanvasPanel>(ActionButtonWidget))
		{
			EmptyBacking = NewObject<UImage>(ActionButtonCanvas);
			EmptyBacking->SetBrush(FSlateColorBrush(WowColor::FromWow(0.0020f, 0.0025f, 0.0030f, 1.0f)));
			if (UCanvasPanelSlot* BackingSlot = ActionButtonCanvas->AddChildToCanvas(EmptyBacking))
			{
				BackingSlot->SetZOrder(-100);
			}
			NamedTextureWidgets.Add(BackingName, EmptyBacking);
		}
		if (EmptyBacking)
		{
			EmptyBacking->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* BackingSlot = Cast<UCanvasPanelSlot>(EmptyBacking->Slot))
			{
				BackingSlot->SetAutoSize(false);
				BackingSlot->SetPosition(FVector2D(1.5f * UIScale));
				BackingSlot->SetSize(FVector2D(33.0f * UIScale));
				BackingSlot->SetZOrder(-100);
			}
		}

		// Action icons are authored at 32 units inside a 36-unit button. At the
		// scaled 1080p bar that leaves an exaggerated empty rim, so enlarge only
		// the icon face while preserving the button frame and hit rectangle.
		const FString IconName = FString::Printf(TEXT("ActionButton%dIcon"), ButtonIndex);
		if (TWeakObjectPtr<UImage>* IconImage = NamedTextureWidgets.Find(IconName);
			IconImage && IconImage->IsValid())
		{
			// The cropped backing replaces only the padded stock empty texture.
			// Lua's SetTexture restores opacity when a real spell/item icon arrives.
			if (const UTexture2D* IconTexture = Cast<UTexture2D>(IconImage->Get()->GetBrush().GetResourceObject());
				IconTexture && IconTexture->GetName().Contains(TEXT("UI-Quickslot"), ESearchCase::IgnoreCase))
			{
				IconImage->Get()->SetRenderOpacity(0.0f);
			}
			if (UCanvasPanelSlot* IconSlot = Cast<UCanvasPanelSlot>(IconImage->Get()->Slot))
			{
				IconSlot->SetAutoSize(false);
				IconSlot->SetPosition(FVector2D::ZeroVector);
				IconSlot->SetSize(FVector2D(36.0f * UIScale));
			}
		}
	}

	static const TCHAR* BagIconNames[] = {
		TEXT("CharacterBag3SlotIconTexture"),
		TEXT("CharacterBag2SlotIconTexture"),
		TEXT("CharacterBag1SlotIconTexture"),
		TEXT("CharacterBag0SlotIconTexture"),
		TEXT("MainMenuBarBackpackButtonIconTexture"),
	};

	// (The bag buttons keep their FrameXML places, which match the recesses in the bar art;
	// packing Bags 1-3 toward Bag0 moved them off those recesses - Stefan 09.10.)

	for (const TCHAR* BagIconName : BagIconNames)
	{
		if (TWeakObjectPtr<UImage>* IconImage = NamedTextureWidgets.Find(BagIconName);
			IconImage && IconImage->IsValid())
		{
			// 29 units at +4 overflowed the recesses in the bar art (Stefan 10.10: "too big").
			// Centre a smaller icon in the button's own rect.
			FString ButtonName(BagIconName);
			ButtonName.RemoveFromEnd(TEXT("IconTexture"));
			const FFrameRect* ButtonRect = FrameRects.Find(ButtonName);
			const float ButtonSize = ButtonRect && ButtonRect->W > 1.0f
				? FMath::Min(ButtonRect->W, ButtonRect->H) : 37.0f;
			const float IconSize = FMath::Min(26.0f, ButtonSize * 0.72f);
			if (UCanvasPanelSlot* IconSlot = Cast<UCanvasPanelSlot>(IconImage->Get()->Slot))
			{
				IconSlot->SetAutoSize(false);
				IconSlot->SetPosition(FVector2D((ButtonSize - IconSize) * 0.5f * UIScale));
				IconSlot->SetSize(FVector2D(IconSize * UIScale));
			}
			static bool bLoggedBagRect = false;
			if (!bLoggedBagRect)
			{
				bLoggedBagRect = true;
				UE_LOG(LogWowFrame, Log, TEXT("Bag bar button %s: rect %.1fx%.1f, icon %.1f"),
					*ButtonName, ButtonRect ? double(ButtonRect->W) : -1.0,
					ButtonRect ? double(ButtonRect->H) : -1.0, double(IconSize));
			}
		}
	}

	// A mild reduction matches the original fixed-aspect bar proportions at
	// 1080p while retaining readable action slots and micro-menu buttons.
	if (const int64 MainMenuBarHandle = FindFrame(TEXT("MainMenuBar")); MainMenuBarHandle >= 0)
	{
		if (UWidget* MainMenuBarWidget = GetWidgetForHandle(MainMenuBarHandle))
		{
			// Outside VR, ApplyElementScales draws the whole bar group (this 0.9 times the
			// Action Bar Size option) centred on the screen.
			if (FWowUiVrHost::IsActive())
			{
				MainMenuBarWidget->SetRenderTransformPivot(FVector2D(0.5f, 1.0f));
				MainMenuBarWidget->SetRenderScale(FVector2D(ActionBarBaseScale));
			}
		}
	}

	// The stock minimap art is flush with the original fixed-aspect screen edge,
	// but its clock/mail controls extend several units beyond the 192-unit
	// cluster. Give the entire cluster a small design-space margin so those
	// circular controls are not clipped into narrow ovals at modern resolutions.
	if (const int64 MinimapClusterHandle = FindFrame(TEXT("MinimapCluster")); MinimapClusterHandle >= 0)
	{
		if (UWidget* MinimapClusterWidget = GetWidgetForHandle(MinimapClusterHandle))
		{
			if (UCanvasPanelSlot* MinimapSlot = Cast<UCanvasPanelSlot>(MinimapClusterWidget->Slot))
			{
				FVector2D Position = MinimapSlot->GetPosition();
				if (const FFrameRect* ClusterRect = FrameRects.Find(TEXT("MinimapCluster")))
				{
					Position.X = (ClusterRect->X - 8.0f) * UIScale;
				}
				MinimapSlot->SetPosition(Position);
			}
		}
	}

	// UI-TOD-Indicator is a 128x64 day/night atlas. Keep its selected square
	// synchronized with the world sky clock and preserve its authored aspect.
	UpdateMinimapTime(MinimapTimeOfDayMinutes);

	// The 3.3.5 PlayerFrame XML intentionally uses a small negative left
	// offset for the original fixed-aspect viewport.  At modern 16:9 sizes it
	// clips the portrait and shifts both status bars into the screen edge.
	// Keep the authored vertical position but clamp the frame fully onscreen.
	// Stefan 10.10: bigger unit frames as a base (was 0.70); the Unit Frame Size option scales on top.
	constexpr float PlayerFrameVisualScale = UnitFrameBaseScale;
	float PlayerFrameTopY = -1.f;
	if (const int64 PlayerFrameHandle = FindFrame(TEXT("PlayerFrame")); PlayerFrameHandle >= 0)
	{
		if (UWidget* PlayerFrameWidget = GetWidgetForHandle(PlayerFrameHandle))
		{
			if (UCanvasPanelSlot* PlayerSlot = Cast<UCanvasPanelSlot>(PlayerFrameWidget->Slot))
			{
				FVector2D Position = PlayerSlot->GetPosition();
				Position.X = FMath::Max(0.0f, Position.X);
				PlayerSlot->SetPosition(Position);
				PlayerFrameTopY = Position.Y;
			}
			// The stock frame is authored for a much smaller 3.3.5 viewport and
			// becomes oversized after our global 1080p UI scaling.  Scale this
			// unit-frame subtree independently from its top-left corner.
			// Outside VR ApplyElementScales sets the transform (base times the option, live).
			if (FWowUiVrHost::IsActive())
			{
				PlayerFrameWidget->SetRenderTransformPivot(FVector2D::ZeroVector);
				PlayerFrameWidget->SetRenderScale(FVector2D(PlayerFrameVisualScale));
			}
		}
	}
	// Target and focus use the same 232x100 stock artwork as PlayerFrame. Keep
	// all three unit frames at one visual scale, but preserve the classic layout:
	// target immediately to the right of the player and focus beneath it. Their
	// target-of-target, aura, portrait, and status-bar children inherit the same
	// transform so the artwork remains registered as a single unit.
	auto LayoutConditionalUnitFrame = [this, PlayerFrameTopY](const TCHAR* FrameName, const FVector2D& DesignPosition, bool bPlayerRow = false)
	{
		if (const int64 Handle = FindFrame(FrameName); Handle >= 0)
		{
			if (UWidget* Widget = GetWidgetForHandle(Handle))
			{
				if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
				{
					FVector2D Position = DesignPosition * UIScale * PlayerFrameVisualScale;
					// Same top edge as the player frame (its Y is the authored one, not scaled):
					// the target frame sat ~2 px higher (Stefan 09.10).
					if (bPlayerRow && PlayerFrameTopY >= 0.f) Position.Y = PlayerFrameTopY;
					Slot->SetPosition(Position);
				}
				if (FWowUiVrHost::IsActive())
				{
					Widget->SetRenderTransformPivot(FVector2D::ZeroVector);
					Widget->SetRenderScale(FVector2D(PlayerFrameVisualScale));
				}
			}
		}
	};
	LayoutConditionalUnitFrame(TEXT("TargetFrame"), FVector2D(250.0f, 4.0f), true);
	LayoutConditionalUnitFrame(TEXT("FocusFrame"), FVector2D(250.0f, 240.0f));
	auto LayoutTargetOfTargetFrame = [this](const TCHAR* FrameName)
	{
		if (UWidget* Widget = GetWidgetForHandle(FindFrame(FrameName)))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				Slot->SetAutoSize(false);
				Slot->SetPosition(FVector2D(104.0f, 65.0f) * UIScale);
				Slot->SetSize(FVector2D(93.0f, 45.0f) * UIScale);
				Slot->SetZOrder(5000);
			}
		}
	};
	LayoutTargetOfTargetFrame(TEXT("TargetFrameToT"));
	LayoutTargetOfTargetFrame(TEXT("FocusFrameToT"));
	for (const TCHAR* Name : {TEXT("TargetFrameToTTextureFrameName"), TEXT("FocusFrameToTTextureFrameName")})
	{
		if (UTextBlock* Label = GetNamedFontString(Name))
		{
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = FMath::RoundToInt(6.0f * UIScale / PlayerFrameVisualScale);
			Label->SetFont(Font);
			Label->SetColorAndOpacity(FSlateColor(WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f)));
			Label->SetShadowOffset(FVector2D(1.0f / PlayerFrameVisualScale));
			Label->SetShadowColorAndOpacity(FLinearColor::Black);
		}
	}
	auto LayoutUnitCastBar = [this](const TCHAR* FrameName)
	{
		if (UWidget* Widget = GetWidgetForHandle(FindFrame(FrameName)))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				Slot->SetAutoSize(false);
				Slot->SetPosition(FVector2D(36.0f, 112.0f) * UIScale);
				Slot->SetSize(FVector2D(150.0f, 10.0f) * UIScale);
				Slot->SetZOrder(6000);
			}
		}
	};
	LayoutUnitCastBar(TEXT("TargetFrameSpellBar"));
	LayoutUnitCastBar(TEXT("FocusFrameSpellBar"));
	for (const TCHAR* TextName : {TEXT("TargetFrameSpellBarText"), TEXT("FocusFrameSpellBarText")})
	{
		if (UTextBlock* CastText = GetNamedFontString(TextName))
		{
			CastText->SetJustification(ETextJustify::Center);
		}
	}
	// Player/pet cast bar: the stock FontString is anchored TOP(0,5) and sits low in
	// the fill here. Centre the spell name both ways inside the bar itself.
	for (const TCHAR* BarName : {TEXT("CastingBarFrame"), TEXT("PetCastingBarFrame")})
	{
		UTextBlock* CastText = GetNamedFontString(FString(BarName) + TEXT("Text"));
		UCanvasPanelSlot* TextSlot = CastText ? Cast<UCanvasPanelSlot>(CastText->Slot) : nullptr;
		const int64 BarHandle = FindFrame(BarName);
		const FWowFrameDef* BarDef = GetFrameDef(BarHandle);
		if (!TextSlot || !BarDef)
		{
			continue;
		}
		const FVector2D BarSize = FVector2D(FMath::Max(BarDef->Width, 1.0f), FMath::Max(BarDef->Height, 1.0f)) * UIScale;
		float LineHeight = static_cast<float>(CastText->GetFont().Size) * 1.35f;
		if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
		{
			LineHeight = static_cast<float>(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()
				->GetMaxCharacterHeight(CastText->GetFont()));
		}
		TextSlot->SetAnchors(FAnchors(0.0f, 0.0f));
		TextSlot->SetAlignment(FVector2D::ZeroVector);
		TextSlot->SetAutoSize(false);
		// -2 UI units: the drop shadow makes the text look lower than its box (r390, Stefan 16.10:
		// "Hearthstone" still sat a little low with -1).
		// r421 (Stefan 18.10: "Wrath" still not centred; his screenshot: letters 2 px below the middle of
		// the fill): 2 more px up.
		TextSlot->SetPosition(FVector2D(0.0f, (BarSize.Y - LineHeight) * 0.5f - 2.0f * UIScale - 2.0f));
		TextSlot->SetSize(FVector2D(BarSize.X, LineHeight));
		CastText->SetJustification(ETextJustify::Center);
		// r420 (Stefan 18.10: "Healing Touch" broke onto two lines and was cut by the bar): the name
		// uses the whole bar on one line, centred both ways; it never wraps.
		CastText->SetAutoWrapText(false);
		CastText->SetWrapTextAt(0.0f);
		CastText->SetClipping(EWidgetClipping::Inherit);
	}
	for (const TCHAR* SparkName : {TEXT("TargetFrameSpellBarSpark"), TEXT("FocusFrameSpellBarSpark"),
        TEXT("CastingBarFrameSpark"), TEXT("PetCastingBarFrameSpark")})
	{
		if (UImage* Spark = GetNamedTexture(SparkName))
		{
			FSlateBrush SparkBrush;
			SparkBrush.DrawAs = ESlateBrushDrawType::Image;
			SparkBrush.ImageSize = FVector2D(3.0f, 14.0f);
			SparkBrush.SetResourceObject(GetUnitCastSparkTexture());
			Spark->SetBrush(SparkBrush);
			Spark->SetColorAndOpacity(FLinearColor::White);
			Spark->SetRenderOpacity(1.0f);
			Spark->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}
	for (const TCHAR* Prefix : {TEXT("TargetFrameToT"), TEXT("FocusFrameToT")})
	{
		if (UWidget* PlateFrame = GetWidgetForHandle(FindFrame(FString(Prefix) + TEXT("TextureFrame"))))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(PlateFrame->Slot))
			{
				Slot->SetZOrder(500);
			}
		}
		if (UWidget* HealthBar = GetWidgetForHandle(FindFrame(FString(Prefix) + TEXT("HealthBar"))))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(HealthBar->Slot))
			{
				Slot->SetZOrder(80);
			}
		}
		if (UWidget* ManaBar = GetWidgetForHandle(FindFrame(FString(Prefix) + TEXT("ManaBar"))))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ManaBar->Slot))
			{
				Slot->SetZOrder(110);
			}
		}
		if (UImage* Portrait = GetNamedTexture(FString(Prefix) + TEXT("Portrait")))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Portrait->Slot))
			{
				Slot->SetZOrder(200);
			}
		}
	}

	// The TargetFrame XML creates its main plate as a named texture whose
	// default materialized opacity can remain zero until Texture:SetTexture is
	// called. It is authored artwork, not a conditional overlay, so make it
	// drawable from the outset. Its hidden parent controls whether it appears,
	// so the plate must not be permanently collapsed during initial UI sync.
	for (const TCHAR* TextureName : {
		TEXT("TargetFrameTextureFrameTexture"), TEXT("FocusFrameTextureFrameTexture"),
		TEXT("TargetFrameToTTextureFrameTexture"), TEXT("FocusFrameToTTextureFrameTexture")})
	{
		if (UImage* UnitFramePlate = GetNamedTexture(TextureName))
		{
			UnitFramePlate->SetColorAndOpacity(FLinearColor::White);
			UnitFramePlate->SetRenderOpacity(1.0f);
			UnitFramePlate->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}

	auto FormatConditionalUnitFrame = [this](const TCHAR* Prefix)
	{
		auto CentreUnitTextCaps = [this](UTextBlock* Label, float BoxTop, float BoxH) { CentreUnitFrameText(Label, BoxTop, BoxH, 1, UIScale); };
		const FString FramePrefix(Prefix);
		auto LayoutTexture = [this](const FString& Name, const FVector2D& Position,
			const FVector2D& Size, int32 ZOrder)
		{
			if (UImage* Texture = GetNamedTexture(Name))
			{
				if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Texture->Slot))
				{
					Slot->SetAutoSize(false);
					Slot->SetPosition(Position * UIScale);
					Slot->SetSize(Size * UIScale);
					Slot->SetZOrder(ZOrder);
				}
			}
		};
		auto LayoutLabel = [this](const FString& Name, const FVector2D& Position,
			const FVector2D& Size, ETextJustify::Type Justification)
		{
			if (UTextBlock* Label = GetNamedFontString(Name))
			{
				if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Label->Slot))
				{
                    Slot->SetAnchors(FAnchors(0.0f,0.0f));
                    Slot->SetAlignment(FVector2D::ZeroVector);
					Slot->SetAutoSize(false);
					Slot->SetPosition(Position * UIScale);
					Slot->SetSize(Size * UIScale);
				}
				Label->SetJustification(Justification);
			}
		};
		auto FormatLabel = [this](const FString& Name, float FontSize, bool bGold)
		{
			if (UTextBlock* Label = GetNamedFontString(Name))
			{
				FSlateFontInfo Font = Label->GetFont();
				Font.Size = FMath::RoundToInt(FontSize * UIScale / PlayerFrameVisualScale);
				// Stefan 11.10: a black outline on all unit frame text, like the client's outlined fonts.
				Font.OutlineSettings.OutlineSize = 1;
				Font.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
				Label->SetFont(Font);
				Label->SetColorAndOpacity(FSlateColor(bGold
					? WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f)
					: FLinearColor::White));
				Label->SetShadowOffset(FVector2D(1.0f / PlayerFrameVisualScale));
				Label->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
			}
		};

		FormatLabel(FramePrefix + TEXT("TextureFrameName"), 5.25f, true); // Stefan 11.10: a size smaller
		FormatLabel(FramePrefix + TEXT("TextureFrameLevelText"), 7.5f, true); // the player's level size (Stefan 11.10)
		FormatLabel(FramePrefix + TEXT("TextureFrameHealthBarText"), 5.0f, false);
		FormatLabel(FramePrefix + TEXT("TextureFrameManaBarText"), 5.0f, false);
		LayoutLabel(FramePrefix + TEXT("TextureFrameName"),
			FVector2D(7.0f, 27.0f), FVector2D(119.0f, 14.0f), ETextJustify::Center);
		// Long names are fitted every tick by FitUnitNameLabels (the name changes with the target).
		FittedUnitNames.Remove(FramePrefix);
		LayoutLabel(FramePrefix + TEXT("TextureFrameHealthBarText"),
			FVector2D(7.0f, 41.0f), FVector2D(119.0f, 12.0f), ETextJustify::Center);
		LayoutLabel(FramePrefix + TEXT("TextureFrameManaBarText"),
			FVector2D(7.0f, 52.0f), FVector2D(119.0f, 12.0f), ETextJustify::Center);
		// Centred both ways (Stefan 11.10): the middle of the capitals on the middle of each bar.
		CentreUnitTextCaps(GetNamedFontString(FramePrefix + TEXT("TextureFrameHealthBarText")), 0.0f, 11.0f);
		CentreUnitTextCaps(GetNamedFontString(FramePrefix + TEXT("TextureFrameManaBarText")), 0.0f, 11.0f);
		LayoutLabel(FramePrefix + TEXT("TextureFrameDeadText"),
			FVector2D(7.0f, 41.0f), FVector2D(119.0f, 23.0f), ETextJustify::Center);
		// The big "Dead" label is not wanted at all (the empty health bar already
		// says it). Fully transparent, so a FrameXML Show() cannot flash it either.
		if (UTextBlock* DeadLabel = GetNamedFontString(FramePrefix + TEXT("TextureFrameDeadText")))
		{
			DeadLabel->SetRenderOpacity(0.0f);
			DeadLabel->SetVisibility(ESlateVisibility::Collapsed);
		}
		LayoutLabel(FramePrefix + TEXT("TextureFrameLevelText"),
			// Stefan (09.10): the target's level sat left of its ring's centre; (10.10) two digits were
			// cut off by the 18-unit box and it then sat right of it: wider box, centre 3 units left.
			FVector2D(166.0f, 59.0f), FVector2D(26.0f, 14.0f), ETextJustify::Center);
		CentreUnitTextCaps(GetNamedFontString(FramePrefix + TEXT("TextureFrameLevelText")), 0.0f, 14.0f);

		auto LayoutStatusBar = [this](int64 Handle, const FVector2D& Position)
		{
			if (Handle < 0)
			{
				return;
			}
			const FVector2D BarSize(119.0f * UIScale, 12.0f * UIScale);
			if (UWidget* Widget = GetWidgetForHandle(Handle))
			{
				if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
				{
					Slot->SetAutoSize(false);
					Slot->SetPosition(Position * UIScale);
					Slot->SetSize(BarSize);
					Slot->SetZOrder(80);
				}
			}
			if (UProgressBar* Fill = GetStatusBarWidget(Handle))
			{
				if (UCanvasPanelSlot* FillSlot = Cast<UCanvasPanelSlot>(Fill->Slot))
				{
					FillSlot->SetAutoSize(false);
					FillSlot->SetPosition(FVector2D::ZeroVector);
					FillSlot->SetSize(BarSize);
				}
			}
		};
		LayoutTexture(FramePrefix + TEXT("Background"),
			FVector2D(7.0f, 22.0f), FVector2D(119.0f, 41.0f), 0);
		LayoutTexture(FramePrefix + TEXT("NameBackground"),
			FVector2D(7.0f, 22.0f), FVector2D(119.0f, 19.0f), 50);
		LayoutTexture(FramePrefix + TEXT("Portrait"),
			FVector2D(126.0f, 12.0f), FVector2D(64.0f, 64.0f), 200);
		if (UWidget* PlateFrame = GetWidgetForHandle(FindFrame(FramePrefix + TEXT("TextureFrame"))))
		{
			if (UCanvasPanelSlot* PlateSlot = Cast<UCanvasPanelSlot>(PlateFrame->Slot))
			{
				PlateSlot->SetZOrder(500);
			}
		}

		const int64 HealthBarHandle = FindFrame(FramePrefix + TEXT("HealthBar"));
		const int64 ManaBarHandle = FindFrame(FramePrefix + TEXT("ManaBar"));
		LayoutStatusBar(HealthBarHandle, FVector2D(7.0f, 41.0f));
		LayoutStatusBar(ManaBarHandle, FVector2D(7.0f, 52.0f));

		// Stefan 11.10: the bars were very dark - forced to 0.34 green / 0.32 blue every tick, then
		// darkened again by the sRGB conversion. Health is the client's pure green; the power bar keeps
		// the colour Lua gives it for the unit's power type (rage red, energy yellow ...), and only a
		// bar Lua never coloured (still the creation default) gets the mana blue.
		ApplyStockUnitBarColorsTo(GetStatusBarWidget(HealthBarHandle), GetStatusBarWidget(ManaBarHandle));

		static const TCHAR* ConditionalSuffixes[] = {
			TEXT("Flash"), TEXT("TextureFramePVPIcon"), TEXT("TextureFrameLeaderIcon"),
			TEXT("TextureFrameRaidTargetIcon"), TEXT("NumericalThreat"),
		};
		for (const TCHAR* Suffix : ConditionalSuffixes)
		{
			const FString ConditionalName = FramePrefix + Suffix;
			if (const int64 Handle = FindFrame(ConditionalName); Handle >= 0)
			{
				SetFrameVisible(Handle, false);
			}
			if (TWeakObjectPtr<UImage>* Texture = NamedTextureWidgets.Find(ConditionalName);
				Texture && Texture->IsValid())
			{
				Texture->Get()->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	};
	FormatConditionalUnitFrame(TEXT("TargetFrame"));
	FormatConditionalUnitFrame(TEXT("FocusFrame"));

	// Vertical centring of a top-aligned text block: the middle of its capitals (~0.70 em, measured
	// from the baseline) goes on the middle of a box BoxH units high starting BoxTop units below the
	// slot's top. The label is inside PlayerFrame's scaled canvas, so slot units are UIScale.
	auto CentreUnitFrameCaps = [this](UTextBlock* Label, float BoxTop, float BoxH)
	{
		if (!Label || !FSlateApplication::IsInitialized()) return;
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FSlateFontInfo LabelFont = Label->GetFont();
		const float LineH = static_cast<float>(Measure->GetMaxCharacterHeight(LabelFont));
		const float Descender = -static_cast<float>(Measure->GetBaseline(LabelFont));
		const float CapH = 0.70f * LabelFont.Size * (96.f / 72.f);
		const float CapCentreFromTop = (LineH - Descender) - CapH * .5f;
		Label->SetRenderTranslation(FVector2D(0.f, FMath::RoundToFloat((BoxTop + BoxH * .5f) * UIScale - CapCentreFromTop)));
	};

	// PlayerName is populated by Lua, but its inherited anchor resolves before
	// the status-bar region exists in this load order.  Place it explicitly over
	// the health bar using the same authored dimensions as that region.
	UTextBlock* PlayerNameLabel = GetNamedFontString(TEXT("PlayerName"));
	if (!PlayerNameLabel)
	{
		UCanvasPanel* PlayerFrameCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(FindFrame(TEXT("PlayerFrame"))));
		if (!PlayerFrameCanvas)
		{
			if (UImage* PlayerFrameTexture = GetNamedTexture(TEXT("PlayerFrameTexture")))
			{
				PlayerFrameCanvas = Cast<UCanvasPanel>(PlayerFrameTexture->GetParent());
			}
		}
		if (PlayerFrameCanvas)
		{
			PlayerNameLabel = NewObject<UTextBlock>(PlayerFrameCanvas);
			PlayerFrameCanvas->AddChildToCanvas(PlayerNameLabel);
			NamedFontStringWidgets.Add(TEXT("PlayerName"), PlayerNameLabel);
		}
	}
	if (PlayerNameLabel)
	{
		if (UCanvasPanelSlot* NameSlot = Cast<UCanvasPanelSlot>(PlayerNameLabel->Slot))
		{
			NameSlot->SetAutoSize(false);
			NameSlot->SetPosition(FVector2D(106.0f * UIScale, 28.0f * UIScale));
			NameSlot->SetSize(FVector2D(119.0f * UIScale, 14.0f * UIScale));
			NameSlot->SetZOrder(32000);
			FSlateFontInfo NameFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
			// Counter the frame-only render scale so the already-approved compact
			// label remains legible while the surrounding art shrinks.
			NameFont.Size = FMath::RoundToInt(7.0f * UIScale / PlayerFrameVisualScale); // Stefan 11.10: a size smaller
			NameFont.OutlineSettings.OutlineSize = 1;
			NameFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
			PlayerNameLabel->SetFont(NameFont);
			PlayerNameLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			PlayerNameLabel->SetJustification(ETextJustify::Center);
		}
		FString PlayerNameValue = PlayerNameLabel->GetText().ToString();
		if (PlayerNameValue.TrimStartAndEnd().IsEmpty())
		{
			PlayerNameValue = TEXT("Player");
		}
		PlayerNameLabel->SetText(FText::FromString(PlayerNameValue));
		// Stefan 11.10: the name sat high in its dark field (25-42). Put the middle of the capitals on
		// the field's middle (the text block is top-aligned in its 28-42 slot).
		CentreUnitFrameCaps(PlayerNameLabel, -3.0f, 17.0f);
		FittedUnitNames.Remove(TEXT("PlayerFrame")); // long names: refitted by FitUnitNameLabels
		PlayerNameLabel->SetShadowOffset(FVector2D(1.0f / PlayerFrameVisualScale));
		PlayerNameLabel->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.95f));
		PlayerNameLabel->SetRenderOpacity(1.0f);
		PlayerNameLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	// Centre the zone label inside the full minimap title plate.  Its inherited
	// font-string slot otherwise keeps the source text's left-biased auto size.
	if (UTextBlock* ZoneLabel = GetNamedFontString(TEXT("MinimapZoneText")))
	{
		if (UCanvasPanelSlot* ZoneSlot = Cast<UCanvasPanelSlot>(ZoneLabel->Slot))
		{
			ZoneSlot->SetAutoSize(false);
			ZoneSlot->SetPosition(FVector2D::ZeroVector);
			ZoneSlot->SetSize(FVector2D(150.0f * UIScale, 12.0f * UIScale));
			FSlateFontInfo ZoneFont = ZoneLabel->GetFont();
			ZoneFont.Size = FMath::RoundToInt(7.0f * UIScale);
			ZoneLabel->SetFont(ZoneFont);
			ZoneLabel->SetColorAndOpacity(FSlateColor(WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f)));
			ZoneLabel->SetJustification(ETextJustify::Center);
		}
	}

	// The original fade scripts normally supply these dark label fields.  Keep
	// them local to the label containers so their existing border art remains.
	auto EnsureLabelBackground = [this](const FString& LabelName, const FLinearColor& Color)
	{
		UTextBlock* Label = GetNamedFontString(LabelName);
		UCanvasPanel* ParentCanvas = Label ? Cast<UCanvasPanel>(Label->GetParent()) : nullptr;
		UCanvasPanelSlot* LabelSlot = Label ? Cast<UCanvasPanelSlot>(Label->Slot) : nullptr;
		if (!ParentCanvas || !LabelSlot) return;

		UImage* Background = nullptr;
		if (TWeakObjectPtr<UImage>* Existing = LabelBackgroundWidgets.Find(LabelName);
			Existing && Existing->IsValid())
		{
			Background = Existing->Get();
		}
		else
		{
			Background = NewObject<UImage>(ParentCanvas);
			FSlateColorBrush BackgroundBrush(Color);
			Background->SetBrush(BackgroundBrush);
			Background->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* BackgroundSlot = ParentCanvas->AddChildToCanvas(Background))
			{
				BackgroundSlot->SetZOrder(LabelSlot->GetZOrder() - 1);
			}
			LabelBackgroundWidgets.Add(LabelName, Background);
		}

		if (UCanvasPanelSlot* BackgroundSlot = Background ? Cast<UCanvasPanelSlot>(Background->Slot) : nullptr)
		{
			const float PadX = 2.0f * UIScale;
			const bool bPlayerName = LabelName == TEXT("PlayerName");
			const float TopPad = bPlayerName ? 3.0f * UIScale : 0.0f;
			const float BottomPad = 0.0f; // ends where the health bar starts, like TargetFrameNameBackground
			BackgroundSlot->SetAutoSize(false);
			BackgroundSlot->SetPosition(LabelSlot->GetPosition() - FVector2D(PadX, TopPad));
			BackgroundSlot->SetSize(LabelSlot->GetSize() + FVector2D(PadX * 2.0f, TopPad + BottomPad));
			BackgroundSlot->SetZOrder(LabelSlot->GetZOrder() - 1);
		}
	};

	EnsureLabelBackground(TEXT("PlayerName"), WowColor::FromWow(0.015f, 0.015f, 0.015f, 0.72f));
	EnsureLabelBackground(TEXT("MinimapZoneText"), WowColor::FromWow(0.015f, 0.015f, 0.015f, 0.78f));

	auto FormatPlayerValueLabel = [this, &CentreUnitFrameCaps](const TCHAR* Name, const FVector2D& Position)
	{
		if (UTextBlock* Label = GetNamedFontString(Name))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Label->Slot))
			{
                Slot->SetAnchors(FAnchors(0.0f,0.0f));
                Slot->SetAlignment(FVector2D::ZeroVector);
				Slot->SetAutoSize(false);
				// These inherited FontStrings are materialized on PlayerFrame's
				// canvas rather than inside the StatusBar widget.  Mirror the bar
				// anchors instead of leaving the live values at the canvas origin.
				Slot->SetPosition(Position * UIScale);
				Slot->SetSize(FVector2D(119.0f * UIScale, 12.0f * UIScale));
				Slot->SetZOrder(1000);
			}
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = FMath::RoundToInt(5.0f * UIScale / PlayerFrameVisualScale);
			Font.OutlineSettings.OutlineSize = 1;
			Font.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
			Label->SetFont(Font);
			Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Label->SetShadowOffset(FVector2D(1.0f / PlayerFrameVisualScale));
			Label->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
			Label->SetJustification(ETextJustify::Center);
			// r244 (Stefan 12.10: the health and mana numbers sat 2-3 px high on their bars, measured on
			// his screenshot): the cap-height estimate centres them about 1.75 units too high.
			CentreUnitFrameCaps(Label, 1.75f, 11.0f); // on its bar
		}
	};
	FormatPlayerValueLabel(TEXT("PlayerFrameHealthBarText"), FVector2D(106.0f, 41.0f));
	FormatPlayerValueLabel(TEXT("PlayerFrameManaBarText"), FVector2D(106.0f, 51.0f));

	auto LayoutPlayerStatusBar = [this](const TCHAR* Name, const FVector2D& Position)
	{
		const int64 Handle = FindFrame(Name);
		if (Handle < 0) return;

		const FVector2D BarSize(119.0f * UIScale, 12.0f * UIScale);
		if (UWidget* Widget = GetWidgetForHandle(Handle))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				Slot->SetAutoSize(false);
				Slot->SetPosition(Position * UIScale);
				Slot->SetSize(BarSize);
				Slot->SetZOrder(80);
			}
		}

		if (UProgressBar* Fill = GetStatusBarWidget(Handle))
		{
			if (UCanvasPanelSlot* FillSlot = Cast<UCanvasPanelSlot>(Fill->Slot))
			{
				FillSlot->SetAutoSize(false);
				FillSlot->SetPosition(FVector2D::ZeroVector);
				FillSlot->SetSize(BarSize);
			}
		}
	};
	LayoutPlayerStatusBar(TEXT("PlayerFrameHealthBar"), FVector2D(106.0f, 41.0f));
	LayoutPlayerStatusBar(TEXT("PlayerFrameManaBar"), FVector2D(106.0f, 51.0f)); // level with TargetFrameManaBar

	if (UImage* Plate = GetNamedTexture(TEXT("PlayerFrameTexture")))
	{
		UWidget* PlayerRoot = GetWidgetForHandle(FindFrame(TEXT("PlayerFrame")));
		UWidget* ArtBranch = Plate;
		while (ArtBranch->GetParent() && ArtBranch->GetParent() != PlayerRoot)
		{
			ArtBranch = ArtBranch->GetParent();
		}
		if (ArtBranch->GetParent() == PlayerRoot)
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ArtBranch->Slot))
			{
				Slot->SetZOrder(500);
			}
		}
	}

	ApplyStockUnitBarColorsTo(GetStatusBarWidget(FindFrame(TEXT("PlayerFrameHealthBar"))), GetStatusBarWidget(FindFrame(TEXT("PlayerFrameManaBar"))));

	if (UTextBlock* LevelLabel = GetNamedFontString(TEXT("PlayerLevelText")))
	{
		if (UCanvasPanelSlot* LevelSlot = Cast<UCanvasPanelSlot>(LevelLabel->Slot))
		{
            LevelSlot->SetAnchors(FAnchors(0.0f,0.0f));
            LevelSlot->SetAlignment(FVector2D::ZeroVector);
			LevelSlot->SetAutoSize(false);
			// Center the value in the stock level ring at the portrait's lower-left (centre 53.5, 64.5).
			// Stefan 11.10: two digits overflowed the 18-unit box and sat off centre; a wider box
			// around the same centre, and the capitals centred vertically below.
			// r401 (Stefan 16.10: "the 9 should go down 2 pixels"): 2 screen pixels lower in the ring.
			LevelSlot->SetPosition(FVector2D(40.5f * UIScale, 57.5f * UIScale + 2.0f));
			LevelSlot->SetSize(FVector2D(26.0f * UIScale, 14.0f * UIScale));
		}
		FSlateFontInfo LevelFont = LevelLabel->GetFont();
		LevelFont.Size = FMath::RoundToInt(7.5f * UIScale / PlayerFrameVisualScale);
		LevelFont.OutlineSettings.OutlineSize = 1;
		LevelFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
		LevelLabel->SetFont(LevelFont);
		LevelLabel->SetColorAndOpacity(FSlateColor(WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f)));
		LevelLabel->SetJustification(ETextJustify::Center);
		CentreUnitFrameText(LevelLabel, 0.0f, 14.0f, 1, UIScale);
	}

	static const TCHAR* InactivePlayerStatusTextures[] = {
		TEXT("PlayerFrameFlash"),
		TEXT("PlayerFrameVehicleTexture"),
		TEXT("PlayerPVPIcon"),
		TEXT("PlayerStatusTexture"), TEXT("PlayerRestIcon"), TEXT("PlayerRestGlow"),
		TEXT("PlayerAttackBackground"), TEXT("PlayerAttackIcon"), TEXT("PlayerAttackGlow"),
	};
	for (const TCHAR* TextureName : InactivePlayerStatusTextures)
	{
		if (TWeakObjectPtr<UImage>* Texture = NamedTextureWidgets.Find(TextureName);
			Texture && Texture->IsValid())
		{
			Texture->Get()->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Apply the persistent HUD strata after all manual layout work. Several chat
	// controls need large local Z values while they are arranged, but those values
	// must never lift the chat above full UI panels such as the character sheet.
	for (const auto& Pair : Frames)
	{
		const FString& FrameName = Pair.Value.Def.Name;
		const bool bPersistentChatFrame =
			FrameName.StartsWith(TEXT("ChatFrame")) ||
			FrameName.Equals(TEXT("GeneralDockManager")) ||
			FrameName.Equals(TEXT("FriendsMicroButton"));
		if (bPersistentChatFrame)
		{
			SetFrameStrata(Pair.Key, EWowFrameStrata::LOW);
		}
	}

	for (const TCHAR* FrameName : HighPanels)
	{
		const int64 FrameHandle = FindFrame(FrameName);
		if (FrameHandle >= 0)
		{
			SetFrameStrata(FrameHandle, EWowFrameStrata::HIGH);
		}
	}

	EnsureNativeMinimapControls();

	// Dump all visible frames with their positions for debugging
	UE_LOG(LogWowFrame, Warning, TEXT("=== VISIBLE FRAMES (post-sync) ==="));
	for (const auto& Pair : Frames)
	{
		if (!Pair.Value.Widget.IsValid()) continue;
		if (Pair.Value.Widget->GetVisibility() == ESlateVisibility::Collapsed) continue;

		UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Pair.Value.Widget->Slot);
		if (!Slot) continue;
		FVector2D Pos = Slot->GetPosition();
		FVector2D Sz = Slot->GetSize();
		// Only log frames with significant area (skip zero-size and tiny frames)
		if (Sz.X * Sz.Y > 100.f)
		{
			UE_LOG(LogWowFrame, Warning, TEXT("  VISIBLE: %-40s pos=(%.0f,%.0f) sz=(%.0f,%.0f) parent='%s'"),
				*Pair.Value.Def.Name, Pos.X, Pos.Y, Sz.X, Sz.Y, *Pair.Value.Def.Parent);
		}
	}
	// Same frame as the layout above: no flash of the unscaled bars/unit frames.
	ApplyElementScales();
}

void FWowFrameManager::SetCooldown(int64 Handle,double StartTime,float Duration)
{
    // A cooldown frame laid out with no size drew its countdown as a small extra number at the
    // corner above the button (Stefan 10.10: "0.4" over two buttons next to the real "0.8").
    // Such a frame shares the overlay of the first ancestor that has a real size.
    // r298 (Stefan 13.10: on Bartender's buttons the "0.9" sat on the top-left corner and no sweep
    // showed): a Cooldown frame covers its button (ActionButtonTemplate's SetAllPoints), but its own
    // widget can be laid out 0x0, so the countdown drew around (0,0). Its button hosts the overlay.
    if (const FFrameEntry* CooldownEntry = Frames.Find(Handle); CooldownEntry && CooldownEntry->Def.Type == EWowFrameType::Cooldown)
    {
        const int64 Owner = GetParentHandle(Handle);
        float L = 0, B = 0, R = 0, T = 0;
        if (Owner >= 0 && Owner != FindFrame(TEXT("UIParent")) && GetFrameScreenRect(Owner, L, B, R, T) && R - L >= 4.f && T - B >= 4.f
            && Cast<UCanvasPanel>(GetWidgetForHandle(Owner)))
            Handle = Owner;
    }
    for (int32 Guard = 0; Guard < 4 && Handle >= 0; ++Guard)
    {
        float L = 0, B = 0, R = 0, T = 0;
        if (GetFrameScreenRect(Handle, L, B, R, T) && R - L >= 4.f && T - B >= 4.f) break;
        const int64 Parent = GetParentHandle(Handle);
        if (Parent < 0 || Parent == FindFrame(TEXT("UIParent"))) break;
        Handle = Parent;
    }
    if (Duration<=0 || StartTime+Duration<=FPlatformTime::Seconds())
    {
        CooldownStates.Remove(Handle);
        // Stefan 11.10 (again): "0.6" / "0.5" stayed on every button after the global cooldown. A
        // cooldown cleared here (CooldownFrame_SetTimer(0, 0)) kept its last countdown text and sweep,
        // and showed them again whenever the button's children were made visible. Clear them.
        if (auto* P=CooldownOverlayWidgets.Find(Handle);P && P->IsValid())
        {
            (*P)->Fraction=0.f;
            (*P)->Countdown.Reset();
            (*P)->SetVisibility(ESlateVisibility::Collapsed);
            if (auto Slate=(*P)->GetCachedWidget()) Slate->Invalidate(EInvalidateWidgetReason::Paint);
        }
        return;
    }
    CooldownStates.FindOrAdd(Handle)={StartTime,Duration};
    auto& Overlay=CooldownOverlayWidgets.FindOrAdd(Handle);
    if (!Overlay.IsValid())
    {
        auto* Container=Cast<UCanvasPanel>(GetWidgetForHandle(Handle));
        if (!Container) return;
        auto* Widget=NewObject<UWowCooldownWidget>(Container);
        if (auto* Slot=Container->AddChildToCanvas(Widget))
        {
            Slot->SetAnchors(FAnchors(0,0,1,1));Slot->SetOffsets(FMargin(0));Slot->SetZOrder(300); // r298: over the icon (ARTWORK 260), under OVERLAY text (360)
        }
        Overlay=Widget;
    }
    Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void FWowFrameManager::TickCooldowns()
{
    const double AnimationNow=FPlatformTime::Seconds();
    const float Dt=LastActionAnimation>0?FMath::Clamp(float(AnimationNow-LastActionAnimation),0.f,.1f):.016f;
    LastActionAnimation=AnimationNow;
    const float Blend=1.f-FMath::Exp(-14.f*Dt);
    for (auto It=ActionIconTargets.CreateIterator();It;++It)
    {
        auto* Icon=It.Key().Get();
        if (!Icon) { It.RemoveCurrent(); continue; }
        const auto Color=FMath::Lerp(Icon->GetColorAndOpacity(),It.Value(),Blend);
        Icon->SetColorAndOpacity(Color);
        const float Scale=.87f+.13f*FMath::Clamp(FMath::Max3(Color.R,Color.G,Color.B),0.f,1.f);
        Icon->SetRenderTransformPivot(FVector2D(.5f));
        Icon->SetRenderScale(FVector2D(Scale));
    }
    const double Now=FPlatformTime::Seconds();
    const bool ShowTime=FWowClientPreferences::GetWorldOption(TEXT("showcooldowntime"));
    for(auto It=CooldownStates.CreateIterator();It;++It)
    {
        const float Remaining=FMath::Max(0.f,float(It.Value().StartTime+It.Value().Duration-Now));
        if(auto* P=CooldownOverlayWidgets.Find(It.Key());P && P->IsValid())
        {
            auto* W=P->Get();W->Fraction=Remaining/It.Value().Duration;
            W->Countdown=ShowTime && Remaining>0 ? (Remaining<10.f?FString::Printf(TEXT("%.1f"),Remaining):FString::FromInt(FMath::CeilToInt(Remaining))) : FString();
            W->SetVisibility(Remaining>0?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
            if (auto Slate=W->GetCachedWidget()) Slate->Invalidate(EInvalidateWidgetReason::Paint);
        }
        if (Remaining<=0) It.RemoveCurrent();
    }
}

void FWowFrameManager::DebugDumpLayout() const
{
	// Dump key WoW UI frames with expected vs actual sizes
	static const TCHAR* KeyFrames[] = {
		TEXT("UIParent"), TEXT("PlayerFrame"), TEXT("TargetFrame"),
		TEXT("TargetFrameToT"), TEXT("TargetFrameToTTextureFrame"),
		TEXT("TargetFrameToTHealthBar"), TEXT("TargetFrameToTManaBar"),
		TEXT("TargetFrameSpellBar"), TEXT("FocusFrame"), TEXT("FocusFrameToT"),
		TEXT("FocusFrameToTTextureFrame"), TEXT("FocusFrameToTHealthBar"),
		TEXT("FocusFrameToTManaBar"), TEXT("FocusFrameSpellBar"),
		TEXT("MainMenuBar"), TEXT("MainMenuBarArtFrame"), TEXT("MinimapCluster"),
		TEXT("Minimap"), TEXT("ActionButton1"), TEXT("MultiBarBottomLeft"),
		TEXT("ChatFrame1"), TEXT("BuffFrame"), TEXT("CastingBarFrame"),
		TEXT("ContainerFrame1"), TEXT("ContainerFrame2"), TEXT("ContainerFrame3"),
		TEXT("ContainerFrame4"), TEXT("ContainerFrame5"),
	};

	UE_LOG(LogWowFrame, Warning, TEXT("═══════════ LAYOUT DEBUG DUMP ═══════════"));
	UE_LOG(LogWowFrame, Warning, TEXT("UIScale=%.3f  RootCanvas=%p  TotalFrames=%d"),
		UIScale, RootCanvas.Get(), Frames.Num());

	for (const TCHAR* FrameName : KeyFrames)
	{
		const int64* HandlePtr = NameToHandle.Find(FrameName);
		if (!HandlePtr) {
			UE_LOG(LogWowFrame, Warning, TEXT("  %-25s  NOT FOUND"), FrameName);
			continue;
		}

		const FFrameEntry* Entry = Frames.Find(*HandlePtr);
		if (!Entry) continue;

		const FWowFrameDef& Def = Entry->Def;
		UWidget* W = Entry->Widget.IsValid() ? Entry->Widget.Get() : nullptr;

		// Get stored rect
		const FFrameRect* Rect = FrameRects.Find(FrameName);
		float RX = Rect ? Rect->X : -1, RY = Rect ? Rect->Y : -1;
		float RW = Rect ? Rect->W : -1, RH = Rect ? Rect->H : -1;

		// Get actual slot info
		FString SlotInfo = TEXT("no widget");
		if (W)
		{
			UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(W->Slot);
			if (Slot)
			{
				FVector2D Pos = Slot->GetPosition();
				FVector2D Size = Slot->GetSize();
				FAnchors Anch = Slot->GetAnchors();
				SlotInfo = FString::Printf(TEXT("slot=(%.0f,%.0f) sz=(%.0f,%.0f) anch=(%.1f,%.1f,%.1f,%.1f) vis=%d clip=%d opacity=%.2f z=%d"),
					Pos.X, Pos.Y, Size.X, Size.Y,
					Anch.Minimum.X, Anch.Minimum.Y, Anch.Maximum.X, Anch.Maximum.Y,
					(int32)W->GetVisibility(), (int32)W->GetClipping(), W->GetRenderOpacity(), Slot->GetZOrder());
			}
			else
			{
				SlotInfo = TEXT("no slot");
			}
		}

		UE_LOG(LogWowFrame, Warning, TEXT("  %-25s  rect=(%.0f,%.0f,%.0f,%.0f) hidden=%d setAll=%d parent='%s' %s"),
			FrameName, RX, RY, RW, RH, Def.bHidden, Def.bSetAllPoints, *Def.Parent, *SlotInfo);
	}

	if (RootCanvas.IsValid())
	{
		const FVector2D RootAbsolute = RootCanvas->GetCachedGeometry().GetAbsolutePosition();
		TFunction<void(UWidget*)> LogTopLeftWidget =
			[this, &LogTopLeftWidget, RootAbsolute](UWidget* Widget)
		{
			if (!Widget || Widget->GetVisibility() == ESlateVisibility::Collapsed ||
				Widget->GetVisibility() == ESlateVisibility::Hidden)
			{
				return;
			}
			const FGeometry& Geometry = Widget->GetCachedGeometry();
			const FVector2D Position = Geometry.GetAbsolutePosition() - RootAbsolute;
			const FVector2D Size = Geometry.GetLocalSize();
			if (Position.X <= 40.0f && Position.Y <= 40.0f &&
				Size.X >= 24.0f && Size.X <= 40.0f && Size.Y >= 24.0f && Size.Y <= 40.0f)
			{
				FString FrameName;
				for (const TPair<int64, FFrameEntry>& Pair : Frames)
				{
					if (Pair.Value.Widget.Get() == Widget)
					{
						FrameName = Pair.Value.Def.Name;
						break;
					}
				}
				FString ResourceName;
				if (const UImage* Image = Cast<UImage>(Widget))
				{
					if (const UObject* Resource = Image->GetBrush().GetResourceObject())
					{
						ResourceName = Resource->GetName();
					}
				}
				UE_LOG(LogWowFrame, Warning,
					TEXT("TOPLEFT_WIDGET class=%s frame='%s' pos=(%.0f,%.0f) size=(%.0f,%.0f) resource='%s'"),
					*Widget->GetClass()->GetName(), *FrameName, Position.X, Position.Y,
					Size.X, Size.Y, *ResourceName);
			}
			if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
			{
				for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
				{
					LogTopLeftWidget(Panel->GetChildAt(ChildIndex));
				}
			}
		};
		LogTopLeftWidget(RootCanvas.Get());
	}

	for (const TCHAR* TextureName : {
		TEXT("TargetFramePortrait"), TEXT("TargetFrameTextureFrameTexture"),
		TEXT("TargetFrameToTPortrait"), TEXT("TargetFrameToTTextureFrameTexture"),
		TEXT("FocusFramePortrait"), TEXT("FocusFrameTextureFrameTexture"),
		TEXT("FocusFrameToTPortrait"), TEXT("FocusFrameToTTextureFrameTexture")})
	{
		if (const UImage* Texture = GetNamedTexture(TextureName))
		{
			const FGeometry& Geometry = Texture->GetCachedGeometry();
			const FVector2D AbsolutePosition = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
			const FVector2D LocalSize = Geometry.GetLocalSize();
			const FLinearColor Tint = Texture->GetColorAndOpacity();
			UE_LOG(LogWowFrame, Warning,
				TEXT("  TEXGEOM %-38s abs=(%.0f,%.0f) size=(%.0f,%.0f) vis=%d opacity=%.2f tintA=%.2f resource=%s"),
				TextureName, AbsolutePosition.X, AbsolutePosition.Y, LocalSize.X, LocalSize.Y,
				static_cast<int32>(Texture->GetVisibility()), Texture->GetRenderOpacity(), Tint.A,
				Texture->GetBrush().GetResourceObject()
					? *Texture->GetBrush().GetResourceObject()->GetName() : TEXT("none"));
		}
	}

	UE_LOG(LogWowFrame, Warning, TEXT("═══════════ END LAYOUT DUMP ═══════════"));
}

void FWowFrameManager::UpdateStatusBarSpark(int64 Handle, float NormalizedValue)
{
	const FString FrameName = GetFrameName(Handle);
	if (FrameName != TEXT("TargetFrameSpellBar") &&
		FrameName != TEXT("FocusFrameSpellBar") &&
        FrameName != TEXT("CastingBarFrame") && FrameName != TEXT("PetCastingBarFrame"))
	{
		return;
	}

	UImage* Spark = GetNamedTexture(FrameName + TEXT("Spark"));
	const FFrameEntry* Entry = Frames.Find(Handle);
	UCanvasPanelSlot* Slot = Spark ? Cast<UCanvasPanelSlot>(Spark->Slot) : nullptr;
	if (!Spark || !Entry || !Slot)
	{
		return;
	}

	const UProgressBar* Fill = GetStatusBarWidget(Handle);
	const UCanvasPanelSlot* FillSlot = Fill ? Cast<UCanvasPanelSlot>(Fill->Slot) : nullptr;
	const FVector2D FillPosition = FillSlot ? FillSlot->GetPosition() : FVector2D::ZeroVector;
	const float BarWidth = FillSlot ? FillSlot->GetSize().X : Entry->Def.Width * UIScale;
	const float SparkX = FillPosition.X + FMath::Clamp(NormalizedValue, 0.0f, 1.0f) * BarWidth - 6.0f * UIScale;
	Slot->SetAutoSize(false);
	Slot->SetAnchors(FAnchors(0.0f, 0.0f));
	Slot->SetAlignment(FVector2D::ZeroVector);
	Slot->SetPosition(FVector2D(SparkX, FillPosition.Y - 6.0f * UIScale));
	const float FillHeight = FillSlot ? FillSlot->GetSize().Y : Entry->Def.Height * UIScale;
	Slot->SetSize(FVector2D(12.0f * UIScale, FillHeight + 12.0f * UIScale));
	Slot->SetZOrder(1000);
	Spark->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

// ── Mouse Hit-Testing & Click Dispatch ───────────────────────────────────────

// A frame with no size and fewer than two points has an empty rect in the 3.3.5
// client and never receives the mouse. The layout falls back to the parent's size
// for such frames (so their children still draw), which let invisible addon frames
// swallow world clicks and left/right mouse camera drags.
static bool HasEmptyWowRect(const FWowFrameDef& Def)
{
	return !Def.bSetAllPoints && Def.Anchors.Num() < 2 && (Def.Width <= 0.f || Def.Height <= 0.f);
}

void FWowFrameManager::PrefetchInventoryItemInfo()
{
	// Ask the server for item templates of everything carried/equipped ahead of time, so the
	// first hover over a bag item doesn't flash "Retrieving item information".
	static double NextScan = 0.0;
	const double Now = FPlatformTime::Seconds();
	if (Now < NextScan || !ItemConnection.IsValid()) return;
	NextScan = Now + 1.0;
	auto& Packets = ItemConnection->PacketHandler;
	auto& Entities = Packets.EntityManager;
	const FWowPlayerEntity* Player = Entities.GetLocalPlayer();
	if (!Player) return;
	auto Want = [&](uint64 Guid)
	{
		if (!Guid) return;
		if (const FWowItemEntity* Item = Entities.FindItem(Guid))
			if (Item->Entry && !Packets.ItemTemplates.Contains(Item->Entry)) ItemConnection->RequestItemInfo(Item->Entry);
	};
	for (uint8 Slot = 0; Slot < 19; ++Slot) Want(Player->GetEquipmentItemGuid(Slot));
	for (uint8 Slot = 0; Slot < 16; ++Slot) Want(Player->GetBackpackItemGuid(Slot));
	for (uint8 Bag = 1; Bag <= 4; ++Bag)
	{
		const uint64 BagGuid = Player->GetBagGuid(Bag);
		Want(BagGuid);
		if (const FWowContainerEntity* Container = Entities.FindContainer(BagGuid))
			for (int32 Slot = 0; Slot < Container->GetNumSlots(); ++Slot) Want(Container->GetItemGuidAtSlot(Slot));
	}
}

int32 FWowFrameManager::GetEffectiveHitZOrder(int64 Handle) const
{
	// Client rules: a frame without its own strata uses its parent's, and a child is at
	// least one level above its parent. StaticPopup1 is DIALOG but its Okay/Cancel buttons
	// were MEDIUM here, so the dialog itself won every click on them.
	TArray<int64, TInlineAllocator<16>> Chain;
	for (int64 H = Handle; H >= 0 && Chain.Num() < 32; H = GetParentHandle(H)) Chain.Add(H);
	int32 Strata = 0, Level = -1;
	for (int32 I = Chain.Num() - 1; I >= 0; --I)
	{
		const FFrameEntry* E = Frames.Find(Chain[I]);
		if (!E) continue;
		if (E->Def.Name == TEXT("UIParent") || E->Def.Name == TEXT("WorldFrame")) continue;
		Strata = FMath::Max(Strata, static_cast<int32>(E->Def.Strata));
		Level = FMath::Max(E->Def.FrameLevel, Level + 1);
	}
	return Strata * 1000 + FMath::Max(Level, 0);
}


int64 FWowFrameManager::HitTestFrames(float ScreenX, float ScreenY) const
{
    if(RootCanvas.IsValid() && !RootCanvas->IsVisible()) return -1;
	// Convert screen position to absolute Slate coordinates for hit testing.
	// GetMousePosition() returns viewport coordinates. We need to convert to the same
	// coordinate space that Slate uses for widget geometry.
	// Use the viewport's local-to-absolute transform if available.
	FVector2D DesignPoint(ScreenX / RawViewportScale * UIScale, ScreenY / RawViewportScale * UIScale);
	if (RootCanvas.IsValid() && GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const TSharedPtr<SViewport> ViewportWidget = GEngine->GameViewport->GetGameViewportWidget();
		const FGeometry& CanvasGeometry = RootCanvas->GetCachedGeometry();
		if (ViewportWidget.IsValid() && CanvasGeometry.GetLocalSize().X > 0)
			DesignPoint = WowViewportCoordinates::PixelToWidget(FVector2D(ScreenX, ScreenY),
				GEngine->GameViewport->Viewport->GetSizeXY(), ViewportWidget->GetCachedGeometry(), CanvasGeometry);
	}
	// VR: the UI lives in the host's virtual window, not in the game viewport, so the viewport
	// mapping above lands somewhere else (every FrameXML click missed in the headset: the close
	// button, buffs, micro menu). Map the pointer into the window and from there into the canvas.
	// Not through the canvas's cached geometry: the canvas is painted into the headset window and
	// can also hold the flat viewport's geometry, so at the moment of a click the mapping was often
	// off and the press found no frame (Stefan 09.10: action bar, buffs and close buttons did not
	// react to the mouse in VR while hover worked). Map proportionally onto UIParent instead.
	if (FVector2D WindowPos; RootCanvas.IsValid() && FWowUiVrHost::ViewportPixelToWindow(FVector2D(ScreenX, ScreenY), WindowPos))
	{
		DesignPoint = RootCanvas->GetCachedGeometry().AbsoluteToLocal(WindowPos);
		float PL = 0, PB = 0, PR = 0, PT = 0;
		const int64 ParentHandle = FindFrame(TEXT("UIParent"));
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport && ParentHandle >= 0
			&& const_cast<FWowFrameManager*>(this)->GetFrameScreenRect(ParentHandle, PL, PB, PR, PT) && PR - PL > 1.f && PT - PB > 1.f)
		{
			const FIntPoint Pixels = GEngine->GameViewport->Viewport->GetSizeXY();
			if (Pixels.X > 0 && Pixels.Y > 0)
				DesignPoint = FVector2D(ScreenX / Pixels.X * (PR - PL), ScreenY / Pixels.Y * (PT - PB)) * UIScale;
		}
	}
	const float DesignX = DesignPoint.X;
	const float DesignY = DesignPoint.Y;
	const float WowX = DesignX / UIScale;
	const float WowY = DesignY / UIScale;

	TSet<int64> TestedLiveFrames;
	int64 BestHandle = -1;
	int32 BestZOrder = -999999;
	int32 BestDepth = -1;

	for (const auto& Pair : Frames)
	{
		const FFrameEntry& Entry = Pair.Value;

		// Debug: check why ActionButton1 might be skipped
		if (Entry.Def.Name == TEXT("ActionButton1"))
		{
			static bool bLoggedSkip = false;
			if (!bLoggedSkip)
			{
				bLoggedSkip = true;
				UE_LOG(LogWowFrame, Warning,
					TEXT("HitTest AB1 check: widget=%d vis=%d mouseEnabled=%d slot=%d"),
					Entry.Widget.IsValid() ? 1 : 0,
					Entry.Widget.IsValid() ? (int)Entry.Widget->GetVisibility() : -1,
					Entry.bMouseEnabled ? 1 : 0,
					Entry.Widget.IsValid() ? (Cast<UCanvasPanelSlot>(Entry.Widget->Slot) != nullptr ? 1 : 0) : -1);
			}
		}

		if (!Entry.Widget.IsValid()) continue;
		if (Entry.Widget->GetVisibility() == ESlateVisibility::Collapsed) continue;
		if (!Entry.bMouseEnabled || !IsFrameEffectivelyShown(Pair.Key)) continue;
        if (Entry.Widget->GetVisibility() == ESlateVisibility::Hidden) continue;
		if (HasEmptyWowRect(Entry.Def)) continue;
		// r272 (Stefan 13.10: tooltips from action bars the immersive interface had faded out).
		if (!MouseBlockedWidgets.IsEmpty())
		{
			bool bBlocked = false;
			for (const UWidget* Link = Entry.Widget.Get(); Link && !bBlocked; Link = Link->GetParent())
				bBlocked = MouseBlockedWidgets.Contains(Link);
			if (bBlocked) continue;
		}

		// Calculate absolute UMG design-pixel position by walking up the parent slot chain
		float AbsX = 0, AbsY = 0, W = 0, H = 0;

		// Walk the UMG widget → slot → parent chain to get absolute position
		UWidget* Current = Entry.Widget.Get();
		float AccumX = 0, AccumY = 0;
		bool bValidChain = false;

		// Get this widget's own slot size
		UCanvasPanelSlot* MySlot = Cast<UCanvasPanelSlot>(Current->Slot);
		if (!MySlot) continue;
		FVector2D MySize = MySlot->GetSize();
		if (MySize.X <= 0 || MySize.Y <= 0) continue;

		W = MySize.X;
		H = MySize.Y;

		// Accumulate positions up the parent chain
		UWidget* Walker = Current;
		int32 Depth = 0;
		while (Walker && Depth < 20)
		{
			AccumX += Walker->GetRenderTransform().Translation.X;
			AccumY += Walker->GetRenderTransform().Translation.Y;
			UCanvasPanelSlot* WSlot = Cast<UCanvasPanelSlot>(Walker->Slot);
			if (WSlot)
			{
				FVector2D WPos = WSlot->GetPosition();
				AccumX += WPos.X;
				AccumY += WPos.Y;
			}

			// Walk to parent: the slot's owning panel → its slot → etc.
			UWidget* ParentWidget = Walker->GetParent();
			if (!ParentWidget) break;

			// If the parent is the root canvas, stop
			if (ParentWidget == RootCanvas.Get())
			{
				bValidChain = true;
				break;
			}
			Walker = ParentWidget;
			Depth++;
		}

		if (!bValidChain)
		{
			// Debug: log why the chain failed for action buttons
			if (Entry.Def.Name.StartsWith(TEXT("ActionButton1")) && Entry.Def.Name.Len() <= 14)
			{
				UE_LOG(LogWowFrame, VeryVerbose, TEXT("HitTest: %s parent chain FAILED at depth %d, walker=%s"),
					*Entry.Def.Name, Depth, Walker ? *Walker->GetName() : TEXT("null"));
			}
			continue;
		}

        TestedLiveFrames.Add(Pair.Key);
		AbsX = AccumX;
		AbsY = AccumY;

		// One-time log for ActionButton1 to debug coordinates
		if (Entry.Def.Name == TEXT("ActionButton1"))
		{
			static bool bLoggedAB1 = false;
			if (!bLoggedAB1)
			{
				bLoggedAB1 = true;
				UE_LOG(LogWowFrame, Warning,
					TEXT("HitTest AB1 computed: absDesign=(%.0f,%.0f) sz=(%.0f,%.0f) depth=%d valid=%d UIScale=%.3f RawScale=%.3f"),
					AbsX, AbsY, W, H, Depth, bValidChain, UIScale, RawViewportScale);
			}
		}

        // Test in the widget's local space so authored transparent borders and
        // render-scaled/docked windows agree with what is actually drawn.
        const FMargin Insets = Entry.Def.HitRectInsets.Get(FMargin(0));
        FVector2D LocalPoint(DesignX-AbsX,DesignY-AbsY), LocalSize(W,H);
        if (RootCanvas.IsValid() && Entry.Widget->GetCachedGeometry().GetLocalSize().X>0)
        {
            const FGeometry& Geometry = Entry.Widget->GetCachedGeometry();
            LocalPoint = Geometry.AbsoluteToLocal(RootCanvas->GetCachedGeometry().LocalToAbsolute(DesignPoint));
            LocalSize = Geometry.GetLocalSize();
        }
        const bool bUnderPointer = LocalPoint.X>=Insets.Left*UIScale && LocalPoint.Y>=Insets.Top*UIScale
            && LocalPoint.X<=LocalSize.X-Insets.Right*UIScale && LocalPoint.Y<=LocalSize.Y-Insets.Bottom*UIScale;
        if(bUnderPointer)
		{
			int32 ZOrder = GetEffectiveHitZOrder(Pair.Key);
			// Native popup rows are placed directly on the root canvas with an
			// explicit high Z-order. Comparing only their authored FrameXML level
			// lets unrelated full-screen frames steal the click even though the menu
			// is visibly on top.
			if (Entry.Widget->GetParent() == RootCanvas.Get())
			{
				ZOrder = FMath::Max(ZOrder, MySlot->GetZOrder());
			}
            if(IsDockPanelChild(Pair.Key))
            {
                // Children inherit the actual dock root's layer, regardless of their own name.
                int32 DockZ = 3500;
                for(int64 Ancestor = Pair.Key; Ancestor >= 0; Ancestor = GetParentHandle(Ancestor))
                {
                    const auto* Def = GetFrameDef(Ancestor);
                    if(Def && Def->Name.StartsWith(TEXT("ContainerFrame"))) { DockZ = 4500; break; }
                    if(Def && Def->Name == TEXT("UIParent")) break;
                }
                ZOrder=DockZ+FMath::Min(Entry.Def.FrameLevel,99);
            }
			if (ZOrder > BestZOrder || (ZOrder == BestZOrder && Depth > BestDepth))
			{
                BestDepth = Depth;
				BestZOrder = ZOrder;
				BestHandle = Pair.Key;
			}
		}
	}

	// Fallback: if design-pixel walk missed, try cached FrameRects in WoW coordinates.
	// This handles frames whose parent chain walk fails but have valid cached rects.
	if (BestHandle < 0)
	{
		for (const auto& Pair : Frames)
		{
			const FFrameEntry& Entry = Pair.Value;
			if (!Entry.Widget.IsValid()) continue;
			if (Entry.Widget->GetVisibility() == ESlateVisibility::Collapsed) continue;
			if (!Entry.bMouseEnabled || !IsFrameEffectivelyShown(Pair.Key)) continue;
        if (Entry.Widget->GetVisibility() == ESlateVisibility::Hidden) continue;
			if (HasEmptyWowRect(Entry.Def)) continue;

            // A miss against the live slot/geometry is final. Authored rectangles
            // predate chat docking and must not create a second invisible target.
            if(TestedLiveFrames.Contains(Pair.Key) || IsDockPanelChild(Pair.Key))continue;
            bool bMovedAncestor=false;
            for(UWidget* W=Entry.Widget.Get();W&&W!=RootCanvas.Get();W=W->GetParent())
                if(!W->GetRenderTransform().Translation.IsNearlyZero()){bMovedAncestor=true;break;}
            if(bMovedAncestor)continue; // Old authored rect must not leave an invisible click target.
			const FFrameRect* Rect = FrameRects.Find(Entry.Def.Name);
			if (!Rect || Rect->W <= 0 || Rect->H <= 0) continue;

            const FMargin Insets = Entry.Def.HitRectInsets.Get(FMargin(0));
			if (WowX >= Rect->X+Insets.Left && WowX <= Rect->X+Rect->W-Insets.Right &&
				WowY >= Rect->Y+Insets.Top && WowY <= Rect->Y+Rect->H-Insets.Bottom)
			{
				int32 ZOrder = GetEffectiveHitZOrder(Pair.Key);
				if (ZOrder > BestZOrder)
				{
					BestZOrder = ZOrder;
					BestHandle = Pair.Key;
				}
			}
		}
	}

	if (BestHandle >= 0)
	{
		const FFrameEntry* HitEntry = Frames.Find(BestHandle);
		UE_LOG(LogWowFrame, VeryVerbose, TEXT("HitTest: screen=(%.0f,%.0f) design=(%.1f,%.1f) wow=(%.1f,%.1f) -> %s [%lld] z=%d"),
			ScreenX, ScreenY, DesignX, DesignY, WowX, WowY,
			HitEntry ? *HitEntry->Def.Name : TEXT("?"), BestHandle, BestZOrder);
	}
	else
	{
		UE_LOG(LogWowFrame, VeryVerbose, TEXT("HitTest MISS: screen=(%.0f,%.0f) design=(%.1f,%.1f)"),
			ScreenX, ScreenY, DesignX, DesignY);
	}

	return BestHandle;
}

bool FWowFrameManager::IsMouseOverFrame(int64 Handle, float TopOffset, float BottomOffset, float LeftOffset, float RightOffset) const
{
	if (bMouseInteractionSuppressed)
	{
		return false;
	}

	const FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry || !Entry->Widget.IsValid())
	{
		return false;
	}

	if (Entry->Widget->GetVisibility() == ESlateVisibility::Collapsed)
	{
		return false;
	}

	// r299 (Stefan 13.10: the character and main menu micro buttons do nothing on Bartender's micro
	// bar): their OnMouseUp only acts when MouseIsOver(self). The frame our hit test has under the
	// cursor (or a child of it) is over the mouse, whatever its cached rect still says.
	if (FMath::IsNearlyZero(TopOffset) && FMath::IsNearlyZero(BottomOffset) && FMath::IsNearlyZero(LeftOffset)
		&& FMath::IsNearlyZero(RightOffset))
	{
		for (int64 Link = HoverFrameHandle, Guard = 0; Link >= 0 && Guard < 32; Link = GetParentHandle(Link), ++Guard)
			if (Link == Handle) return true;
	}

	const FFrameRect* Rect = FrameRects.Find(Entry->Def.Name);
	if (!Rect || Rect->W <= 0.f || Rect->H <= 0.f || RawViewportScale <= 0.f)
	{
		return false;
	}

	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}

	if (!RootCanvas.IsValid() || UIScale <= 0.f) return false;
	const FVector2D CursorPos = RootCanvas->GetCachedGeometry().AbsoluteToLocal(WowUiCursorAbsolute());
	const FGeometry& Geometry=Entry->Widget->GetCachedGeometry();
	if(Geometry.GetLocalSize().X>0)
	{
		const FVector2D Local=Geometry.AbsoluteToLocal(WowUiCursorAbsolute());
		const FVector2D Size=Geometry.GetLocalSize();
		return Local.X>=LeftOffset*UIScale&&Local.X<=Size.X+RightOffset*UIScale
			&&Local.Y>=-TopOffset*UIScale&&Local.Y<=Size.Y-BottomOffset*UIScale;
	}
	const float WowX = CursorPos.X / UIScale;
	const float WowY = CursorPos.Y / UIScale;

	const float Left = Rect->X + LeftOffset;
	const float Right = Rect->X + Rect->W + RightOffset;
	const float Top = Rect->Y - TopOffset;
	const float Bottom = Rect->Y + Rect->H - BottomOffset;
	return WowX >= Left && WowX <= Right && WowY >= Top && WowY <= Bottom;
}

void FWowFrameManager::LayoutSpellBookText()
{
    if (!IsFrameVisible(FindFrame(TEXT("SpellBookFrame")))) return;
    auto Place=[this](UTextBlock* Text, float X, float Y, float W, float H)
    {
        if (!Text) return;
        Text->SetJustification(ETextJustify::Left);
        Text->SetClipping(EWidgetClipping::ClipToBounds);
        if (auto* Slot=Cast<UCanvasPanelSlot>(Text->Slot))
        {
            Slot->SetAnchors(FAnchors(0.f,0.f)); Slot->SetAlignment(FVector2D::ZeroVector);
            Slot->SetAutoSize(false); Slot->SetPosition(FVector2D(X,Y)*UIScale); Slot->SetSize(FVector2D(W,H)*UIScale);
        }
    };
    struct FCachedTextLayout { FString Text; float Scale=0.f, Height=0.f; };
    static TMap<TWeakObjectPtr<UTextBlock>,FCachedTextLayout> CachedLayouts;
    if (CachedLayouts.Num()>32) CachedLayouts.Empty();
    for (int32 I=1; I<=12; ++I)
    {
        const FString Prefix=FString::Printf(TEXT("SpellButton%d"),I);
        UTextBlock* Name=GetNamedFontString(Prefix+TEXT("SpellName"));
        UTextBlock* Rank=GetNamedFontString(Prefix+TEXT("SubSpellName"));
        if (!Name) continue;
        // Each 157-unit column owns a 32-unit icon and a bounded text block.
        auto& Cached=CachedLayouts.FindOrAdd(Name);
        const FString Value=Name->GetText().ToString();
        if (Cached.Text!=Value || Cached.Scale!=UIScale)
        {
            Cached.Text=Value; Cached.Scale=UIScale;
            Name->SetAutoWrapText(false); Name->SetWrapTextAt(103.f*UIScale);
            // r271 (Stefan 13.10: the book "doesn't look like it should"): SpellName is GameFontNormal
            // (Friz Quadrata 12, gold) in the original, not the engine's sans font. 12 WoW units =
            // 9 Slate points; smaller only when a long name needs more than its three lines.
            auto SpellNameFont=[this](float Points)
            {
                return FontManager && FontManager->IsInitialized()
                    ? FontManager->GetFont(TEXT("GameFontNormal"),FMath::Max(1,FMath::RoundToInt(Points*UIScale)))
                    : FCoreStyle::GetDefaultFontStyle("Regular",FMath::Max(1,FMath::RoundToInt(Points*UIScale)));
            };
            Name->SetFont(SpellNameFont(9.f));
            Name->ForceLayoutPrepass();
            Cached.Height=Name->GetDesiredSize().Y/UIScale;
            for (float Size=8.f; Cached.Height>36.f && Size>=6.f; Size-=1.f)
            {
                Name->SetFont(SpellNameFont(Size));
                Name->ForceLayoutPrepass(); Cached.Height=Name->GetDesiredSize().Y/UIScale;
            }
        }
        // The "checked" overlay (current spell, e.g. Attack) is CheckButtonHilight, an additive
        // glow drawn here as a black square with a blue rim bigger than the icon (and left behind
        // on an empty slot after a page change): a blue outline on the icon instead, never on an
        // empty slot.
        if (const TWeakObjectPtr<UImage>* Check = CheckedTextureWidgets.Find(FindFrame(Prefix)); Check && Check->IsValid())
        {
            UImage* CheckImage = Check->Get();
            if (Value.IsEmpty()) CheckImage->SetVisibility(ESlateVisibility::Collapsed);
            const UObject* Resource = CheckImage->GetBrush().GetResourceObject();
            if (Resource || CheckImage->GetBrush().DrawAs != ESlateBrushDrawType::RoundedBox)
                CheckImage->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, 3.f * UIScale,
                    WowColor::FromWow(.35f, .65f, 1.f, 1.f), 2.f * UIScale));
            if (auto* CheckSlot = Cast<UCanvasPanelSlot>(CheckImage->Slot))
            {
                CheckSlot->SetAnchors(FAnchors(0.f, 0.f)); CheckSlot->SetAlignment(FVector2D::ZeroVector);
                CheckSlot->SetAutoSize(false); CheckSlot->SetPosition(FVector2D::ZeroVector); CheckSlot->SetSize(FVector2D(36.f) * UIScale);
                CheckSlot->SetZOrder(60);
            }
        }
        // The slot background art (64x64 from the icon's top-left) showed as a dark square with a
        // blue rim behind spell icons (Stefan: Auto Attack, Blood Fury, Summon Friend): only an
        // empty slot shows it.
        // A slot counts as filled when either its name or its icon shows: some buttons' name
        // lookup returned an empty duplicate, which left the dark 64x64 block behind six spells
        // (Stefan 10.10: Auto Attack, Block, Dodge, Fishing, Summon Friend ...).
        bool bHasIcon = false;
        if (const UImage* Icon = GetNamedTexture(Prefix + TEXT("IconTexture")))
            bHasIcon = Icon->GetBrush().GetResourceObject() != nullptr && Icon->GetRenderOpacity() > 0.f
                && Icon->GetVisibility() != ESlateVisibility::Collapsed && Icon->GetVisibility() != ESlateVisibility::Hidden;
        // r390 (Stefan 16.10: on Subtlety, slots 3-11 showed a small square and only slot 12 the
        // real empty slot): a hidden name keeps the text of the tab shown before (General has 11
        // spells), so the slot counts as filled only while its name or icon is shown. An empty slot
        // shows only its 64x64 slot art, like the client; the button frame (UI-Quickslot2, drawn
        // squeezed into the 37-unit button here) is hidden on it.
        const bool bNameShown = !Value.IsEmpty() && Name->GetVisibility() != ESlateVisibility::Collapsed
            && Name->GetVisibility() != ESlateVisibility::Hidden;
        const bool bEmptySlot = !bNameShown && !bHasIcon;
        if (UImage* Background = GetNamedTexture(Prefix + TEXT("Background")))
            Background->SetRenderOpacity(bEmptySlot ? 1.f : 0.f);
        if (UButton* SlotButton = GetButtonWidget(FindFrame(Prefix)))
            SlotButton->SetRenderOpacity(bEmptySlot ? 0.f : 1.f);
        const float Height=FMath::Clamp(Cached.Height,12.f,36.f);
        const bool HasRank=Rank && !Rank->GetText().IsEmpty();
        // Name and rank centred on the 37-unit icon, 4 units right of it (SpellName LEFT +4,0).
        const float Top=(37.f-Height-(HasRank ? 12.f : 0.f))*0.5f;
        Place(Name,41.f,Top,103.f,Height);
        if (Rank)
        {
            // SubSpellFont: Friz Quadrata 10 (7.5 points), the brown "Passive" / "Rank N" line.
            Rank->SetFont(FontManager && FontManager->IsInitialized()
                ? FontManager->GetFont(TEXT("GameFontNormalSmall"),FMath::Max(1,FMath::RoundToInt(7.5f*UIScale)))
                : FCoreStyle::GetDefaultFontStyle("Regular",FMath::Max(1,FMath::RoundToInt(7.5f*UIScale))));
            Rank->SetAutoWrapText(false); Rank->SetWrapTextAt(103.f*UIScale);
            Place(Rank,41.f,Top+Height,103.f,12.f);
        }
    }
    if (auto* Text=GetNamedFontString(TEXT("ShowAllSpellRanksCheckBoxText")))
    {
        Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",FMath::Max(1,FMath::RoundToInt(9.f*UIScale))));
        Place(Text,24.f,5.f,195.f,16.f);
    }
    // r331 (Stefan 14.10: "the text at the bottom tab isn't in the right place"): the bottom tabs'
    // ButtonText is anchored CENTER, 0, +3 on the 128 x 64 tab (SpellBookFrameTabButtonTemplate);
    // the caption filled the button from its top edge, so it sat above the tab art.
    for (int32 I=1; I<=3; ++I)
    {
        if (UTextBlock* Caption=GetPrimaryTextWidget(FindFrame(FString::Printf(TEXT("SpellBookFrameTabButton%d"),I))))
        {
            Place(Caption,0.f,32.f-3.f-7.f,128.f,14.f);
            Caption->SetJustification(ETextJustify::Center);
        }
    }
}

void FWowFrameManager::TickWidgetEvents()
{
    FlushDependentLayout(); // r228
    // r322 (Stefan 14.10: closing one bag moved the others left, with gaps): two layouts placed the
    // bags - SanitizeBackpackOnlyContainers (render scale .75, side-bar offset) and FrameXML's
    // updateContainerFrameAnchors (scale 1, run from OnShow/OnHide) - and the last one won. FrameXML's
    // is turned into a no-op (once per Lua state) and the native one runs whenever the set of open
    // bags or side bars changes.
    {
        static FString GLastBagLayoutKey;
        FString BagLayoutKey = FString::Printf(TEXT("%lld:"), FindFrame(TEXT("ContainerFrame1")));
        for (int32 BagIndex = 1; BagIndex <= 5; ++BagIndex)
            BagLayoutKey.AppendChar(IsFrameActuallyVisible(FindFrame(FString::Printf(TEXT("ContainerFrame%d"), BagIndex))) ? TEXT('1') : TEXT('0'));
        BagLayoutKey.AppendChar(IsFrameActuallyVisible(FindFrame(TEXT("MultiBarLeft"))) ? TEXT('L') : TEXT('-'));
        BagLayoutKey.AppendChar(IsFrameActuallyVisible(FindFrame(TEXT("MultiBarRight"))) ? TEXT('R') : TEXT('-'));
        BagLayoutKey.AppendChar(FWowUiVrHost::IsActive() ? TEXT('V') : TEXT('-'));
        if (BagLayoutKey != GLastBagLayoutKey && FindFrame(TEXT("ContainerFrame1")) >= 0)
        {
            GLastBagLayoutKey = BagLayoutKey;
            if (EventSystem)
                EventSystem->ExecuteLuaString(TEXT("if updateContainerFrameAnchors and not __WowNativeBagLayout then ")
                    TEXT("__WowNativeBagLayout = true local stock = updateContainerFrameAnchors ")
                    // The bank's bags (ContainerFrame6+) still use the stock column while the bank is open.
                    TEXT("updateContainerFrameAnchors = function(...) if BankFrame and BankFrame:IsShown() then return stock(...) end end end"),
                    TEXT("=NativeBagLayout"));
            SanitizeBackpackOnlyContainers();
        }
    }
    LayoutSpellBookText();
    // CharacterMicroButton_SetPushed / _SetNormal: the face on the button is dimmed (alpha .5, the
    // pushed crop) while the character sheet is open and bright otherwise. Driven here because
    // SetPortraitTexture and the sheet refresh reset it to full alpha, which left it bright while
    // open and dim after closing (Stefan 10.10).
    if (UImage* MicroPortrait = GetNamedTexture(TEXT("MicroButtonPortrait")))
    {
        const bool bSheetOpen = IsFrameVisible(FindFrame(TEXT("CharacterFrame")));
        const FBox2f WantUV = bSheetOpen ? FBox2f(FVector2f(.2666f, 0.f), FVector2f(.8666f, .8333f))
            : FBox2f(FVector2f(.2f, .0666f), FVector2f(.8f, .9f));
        FSlateBrush Brush = MicroPortrait->GetBrush();
        if (!(Brush.GetUVRegion() == WantUV)) { Brush.SetUVRegion(WantUV); MicroPortrait->SetBrush(Brush); }
        const float WantAlpha = bSheetOpen ? .5f : 1.f;
        if (!FMath::IsNearlyEqual(MicroPortrait->GetRenderOpacity(), WantAlpha)) MicroPortrait->SetRenderOpacity(WantAlpha);
    }
    // PVPMicroButtonTexture: TOP +6,-30 of the 28 x 58 micro button, 36 x 36 (the faction emblem fills
    // the top-left of UI-PVP-<faction>, so it lands centred on the button face). The anchor came out
    // elsewhere and the emblem sat up and to the left of the button (Stefan 10.10). Pin the slot.
    if (UImage* PvpEmblem = GetNamedTexture(TEXT("PVPMicroButtonTexture")))
    {
        if (UCanvasPanelSlot* EmblemSlot = Cast<UCanvasPanelSlot>(PvpEmblem->Slot))
        {
            const FVector2D EmblemPosition = FVector2D(2.0f, 30.0f) * UIScale;
            const FVector2D EmblemSize = FVector2D(36.0f) * UIScale;
            if (!EmblemSlot->GetPosition().Equals(EmblemPosition, 0.01f) || !EmblemSlot->GetSize().Equals(EmblemSize, 0.01f))
            {
                EmblemSlot->SetAnchors(FAnchors(0.0f, 0.0f));
                EmblemSlot->SetAlignment(FVector2D::ZeroVector);
                EmblemSlot->SetAutoSize(false);
                EmblemSlot->SetPosition(EmblemPosition);
                EmblemSlot->SetSize(EmblemSize);
            }
        }
    }
    UpdateOptionalActionBars();
    RefreshInterfaceScale();
    ApplyElementScales();
    FitUnitNameLabels();
    // PVPParentFrameTab1/2: BOTTOMLEFT 11,46 and LEFT of the previous tab's RIGHT -15.
    LayoutTemplateTabs(TEXT("PVPParentFrame"), 2, 11.0f, 46.0f);
    // DressUpFrame's OnLoad (SetDressUpBackground) runs before the player exists, so UnitRace fell
    // back to Human for everyone. Pick the race background again when the window first opens.
    {
        static TWeakObjectPtr<UWidget> DressUpBackgroundDoneFor;
        const int64 DressUpHandle = FindFrame(TEXT("DressUpFrame"));
        UWidget* DressUpWidget = GetWidgetForHandle(DressUpHandle);
        if (EventSystem && DressUpWidget && DressUpBackgroundDoneFor.Get() != DressUpWidget && IsFrameVisible(DressUpHandle))
        {
            DressUpBackgroundDoneFor = DressUpWidget;
            EventSystem->ExecuteLuaString(TEXT("if SetDressUpBackground then SetDressUpBackground() end"), TEXT("=DressUpBackground"));
        }
    }
    TickTransientMessages(FPlatformTime::Seconds());
    TickFrameStack();
    LayoutChatEditHeaders();
    RefreshPendingBagTitles();
    UpdateClockTooltip();
    PrefetchInventoryItemInfo();
    {
        // Diagnostic (Stefan 10.10: opening all bags drew stretched grey strips, the backpack behind):
        // when the set of open bags changes, log each open bag's rect and its background pieces.
        static FString LastOpenBags;
        FString OpenBags;
        for (int32 Index = 1; Index <= 13; ++Index)
            if (const int64 Bag = FindFrame(FString::Printf(TEXT("ContainerFrame%d"), Index)); Bag >= 0 && IsFrameVisible(Bag))
                OpenBags += FString::Printf(TEXT("%d "), Index);
        if (OpenBags != LastOpenBags)
        {
            LastOpenBags = OpenBags;
            for (int32 Index = 1; Index <= 13; ++Index)
            {
                const FString Name = FString::Printf(TEXT("ContainerFrame%d"), Index);
                const int64 Bag = FindFrame(Name);
                if (Bag < 0 || !IsFrameVisible(Bag)) continue;
                float L = 0, B = 0, R = 0, T = 0;
                GetFrameScreenRect(Bag, L, B, R, T);
                FString Pieces;
                for (const TCHAR* Piece : { TEXT("BackgroundTop"), TEXT("BackgroundMiddle1"), TEXT("BackgroundMiddle2"), TEXT("BackgroundBottom"), TEXT("Portrait") })
                    if (UImage* Image = GetNamedTexture(Name + Piece))
                    {
                        const UCanvasPanelSlot* PieceSlot = Cast<UCanvasPanelSlot>(Image->Slot);
                        const UObject* Res = Image->GetBrush().GetResourceObject();
                        const FString ResName = Res ? Res->GetName() : FString(TEXT("none"));
                        const FBox2f UV = Image->GetBrush().GetUVRegion();
                        const FVector2D PiecePos = PieceSlot ? PieceSlot->GetPosition() : FVector2D(-1.0);
                        const FVector2D PieceSize = PieceSlot ? PieceSlot->GetSize() : FVector2D(-1.0);
                        Pieces += FString::Printf(TEXT(" %s[%s vis=%d pos=%.0f,%.0f size=%.0fx%.0f uv=%.2f,%.2f-%.2f,%.2f]"), Piece, *ResName,
                            Image->IsVisible() ? 1 : 0, PiecePos.X, PiecePos.Y, PieceSize.X, PieceSize.Y,
                            double(UV.Min.X), double(UV.Min.Y), double(UV.Max.X), double(UV.Max.Y));
                    }
                UE_LOG(LogWowFrame, Log, TEXT("Open bag %s rect=(%.0f,%.0f)-(%.0f,%.0f)%s"), *Name, L, B, R, T, *Pieces);
            }
        }
    }
    if (FWowClientPreferences::GetWorldOption(TEXT("tooltipatcursor")))
    {
        const int64 Tooltip = FindFrame(TEXT("GameTooltip"));
        if (Tooltip >= 0 && IsFrameActuallyVisible(Tooltip)) PositionTooltipAtScreenCorner(Tooltip);
    }
    if (ItemConnection.IsValid() && LastItemRevision != ItemConnection->PacketHandler.VendorRevision)
    {
        LastItemRevision = ItemConnection->PacketHandler.VendorRevision;
        for (auto& Pair : TooltipStates)
        {
            const FTooltipState Snapshot = Pair.Value;
            if (Snapshot.ItemEntry && IsFrameVisible(Pair.Key))
                TooltipSetItemDetails(Pair.Key, Snapshot.ItemEntry, Snapshot.ItemLink, Snapshot.ItemStack,
                    Snapshot.ItemContext, Snapshot.ItemDurability, Snapshot.ItemMaxDurability, Snapshot.bEquippedItem, Snapshot.ItemGuid);
        }
    }
	const bool bCharacterFrameVisible = IsFrameVisible(FindFrame(TEXT("CharacterFrame")));
	if (!bCharacterFrameVisible)
	{
		for (const TPair<FString, TWeakObjectPtr<UTextBlock>>& Pair : NamedFontStringWidgets)
		{
			if (Pair.Key.StartsWith(TEXT("CharacterSheetOverlay")) && Pair.Value.IsValid())
			{
				Pair.Value->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		for (const TCHAR* TextureName : {
			TEXT("CharacterSheetOverlayPortrait"), TEXT("CharacterSheetOverlayModel"), TEXT("CharacterSheetOverlayModelBackdrop")})
		{
			if (UImage* Image = GetNamedTexture(TextureName))
			{
				Image->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
	if (!bCharacterFrameVisible)
	{
		bCharacterRightStatMenuOpen = false;
		bCharacterLeftStatMenuOpen = false;
		RefreshNativeCharacterStatControls();
	}
	if (!bCharacterFrameVisible && bCharacterTitleMenuOpen)
	{
		bCharacterTitleMenuOpen = false;
		RefreshNativeCharacterTitleControl();
	}
	if (bCharacterFrameVisible)
	{
		RefreshNativeCharacterSheetTabs();
		if (UCanvasPanel* PaperDollCanvas = Cast<UCanvasPanel>(
			GetWidgetForHandle(FindFrame(TEXT("PaperDollFrame")))))
		{
			for (int32 ChildIndex = 0; ChildIndex < PaperDollCanvas->GetChildrenCount(); ++ChildIndex)
			{
				if (UTextBlock* LegacyCaption = Cast<UTextBlock>(PaperDollCanvas->GetChildAt(ChildIndex)))
				{
					LegacyCaption->SetText(FText::GetEmpty());
					LegacyCaption->SetVisibility(ESlateVisibility::Collapsed);
				}
			}
		}
		auto HidePlayerPlaceholder = [](UTextBlock* Text)
		{
			if (!Text)
			{
				return;
			}
			const FString Value = Text->GetText().ToString().TrimStartAndEnd();
			if (Value.Equals(TEXT("player"), ESearchCase::IgnoreCase) ||
				Value.Equals(TEXT("N"), ESearchCase::CaseSensitive) ||
				Value.Equals(TEXT("X"), ESearchCase::CaseSensitive) ||
				Value.Equals(TEXT("?"), ESearchCase::CaseSensitive))
			{
				Text->SetText(FText::GetEmpty());
				Text->SetVisibility(ESlateVisibility::Collapsed);
			}
		};
		if (UWidget* CharacterWidget = GetWidgetForHandle(FindFrame(TEXT("CharacterFrame"))))
		{
			TFunction<void(UWidget*)> HideLegacyCharacterText =
				[&HideLegacyCharacterText, &HidePlayerPlaceholder](UWidget* Widget)
			{
				if (UTextBlock* Text = Cast<UTextBlock>(Widget))
				{
					HidePlayerPlaceholder(Text);
					return;
				}
				if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
				{
					for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
					{
						HideLegacyCharacterText(Panel->GetChildAt(ChildIndex));
					}
				}
			};
			HideLegacyCharacterText(CharacterWidget);
		}
		auto IsCharacterSheetPlaceholderOwner = [](const FString& Name)
		{
			return Name.Contains(TEXT("CharacterModel")) ||
				Name.Contains(TEXT("PaperDoll")) ||
				Name.Contains(TEXT("CharacterNameFrame"));
		};
		for (auto& Pair : PrimaryTextWidgets)
		{
			if (const FFrameEntry* Entry = Frames.Find(Pair.Key);
				Entry && IsCharacterSheetPlaceholderOwner(Entry->Def.Name))
			{
				HidePlayerPlaceholder(Pair.Value.Get());
			}
		}
		for (auto& Pair : NamedFontStringWidgets)
		{
			if (Pair.Key.Equals(TEXT("PlayerName"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (IsCharacterSheetPlaceholderOwner(Pair.Key))
			{
				HidePlayerPlaceholder(Pair.Value.Get());
			}
		}
		for (auto& Pair : NamedFontStringWidgets)
		{
			const bool bLegacyClientLabel =
				Pair.Key.StartsWith(TEXT("CharacterSheetClient")) ||
				Pair.Key.StartsWith(TEXT("CharacterStat"));
			const bool bLegacyStatLabel =
				Pair.Key.StartsWith(TEXT("PlayerStatFrameLeft")) ||
				Pair.Key.StartsWith(TEXT("PlayerStatFrameRight"));
			if ((bLegacyClientLabel || bLegacyStatLabel) && Pair.Value.IsValid())
			{
				Pair.Value->SetText(FText::GetEmpty());
				Pair.Value->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		for (auto& Pair : PrimaryTextWidgets)
		{
			if (const FFrameEntry* Entry = Frames.Find(Pair.Key);
				Entry && (Entry->Def.Name.StartsWith(TEXT("PlayerStatFrameLeft")) ||
					Entry->Def.Name.StartsWith(TEXT("PlayerStatFrameRight"))) &&
				Pair.Value.IsValid())
			{
				Pair.Value->SetText(FText::GetEmpty());
				Pair.Value->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	// Poll EditBox widgets for text changes
	for (auto& Pair : EditBoxWidgets)
	{
		if (!Pair.Value.IsValid()) continue;
		if (const FFrameEntry* Entry = Frames.Find(Pair.Key);
			Entry && Entry->Def.Name.StartsWith(TEXT("ChatFrame")))
		{
			if (UImage* Fill = GetNamedTexture(Entry->Def.Name + TEXT("Fill")))
			{
				Fill->SetColorAndOpacity(!bMouseInteractionSuppressed && Pair.Value->IsHovered()
					? WowColor::FromWow(0.72f, 0.74f, 0.78f, 0.19f)
					: WowColor::FromWow(0.62f, 0.64f, 0.68f, 0.13f));
			}
		}
		FString CurrentText = Pair.Value->GetText().ToString();
		FString* LastText = EditBoxLastText.Find(Pair.Key);
		if (!LastText)
		{
			EditBoxLastText.Add(Pair.Key, CurrentText);
		}
		else if (*LastText != CurrentText)
		{
			*LastText = CurrentText;
			if (EventSystem)
			{
				EventSystem->RunFrameScript(Pair.Key, TEXT("OnTextChanged"));
			}
		}
	}

	// Native size-grabber drag. Slot dimensions are already in rendered design
	// pixels, matching Slate cursor deltas in the game viewport.
	if (bChatResizeActive && FSlateApplication::IsInitialized())
	{
		const FVector2D Cursor = FSlateApplication::Get().GetCursorPos();
		const FVector2D Delta = Cursor - ChatResizeStartCursor;
		const FVector2D NewSize(
			FMath::Clamp(ChatResizeStartSize.X + Delta.X, 296.0f * UIScale, 608.0f * UIScale),
			FMath::Clamp(ChatResizeStartSize.Y + Delta.Y, 120.0f * UIScale, 400.0f * UIScale));
		for (int32 ChatIndex = 1; ChatIndex <= 10; ++ChatIndex)
		{
			if (UWidget* Frame = GetWidgetForHandle(FindFrame(
				*FString::Printf(TEXT("ChatFrame%d"), ChatIndex))))
			{
				if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Frame->Slot))
				{
					Slot->SetSize(NewSize);
				}
			}
		}
	}

	// Blizzard's chat chrome is deliberately subdued until the pointer enters
	// the chat area. Reveal all tabs and controls together, with the exact tab
	// or button under the pointer receiving the strongest highlight.
    const bool bChatOccluded=bChatPointerBlocked||IsDockPanelChild(HoverFrameHandle);
    auto IsNamedFrameHovered = [this,bChatOccluded](const TCHAR* Name)
	{
		if (bMouseInteractionSuppressed || bChatOccluded)
		{
			return false;
		}
		// r258 (Stefan 12.10: the chat scroll buttons gave no feedback on hover): the frame widgets
		// do not take Slate hover, so Widget->IsHovered() never became true; our own hover frame plus
		// the pointer inside its rect decides.
		const int64 Handle = FindFrame(Name);
		if (UWidget* Widget = GetWidgetForHandle(Handle))
		{
			return HoverFrameHandle == Handle && (Widget->IsHovered() || IsMouseOverFrame(Handle, 0.0f, 0.0f, 0.0f, 0.0f));
		}
		return false;
	};
	const bool bForceChatHover = !bMouseInteractionSuppressed && !bChatOccluded &&
		FParse::Param(FCommandLine::Get(), TEXT("testchathover"));
	const bool bFriendsHovered = IsNamedFrameHovered(TEXT("FriendsMicroButton"));
	const bool bMenuHovered = IsNamedFrameHovered(TEXT("ChatFrameMenuButton"));
	const bool bUpHovered = IsNamedFrameHovered(TEXT("ChatFrame1ButtonFrameUpButton"));
	const bool bDownHovered = IsNamedFrameHovered(TEXT("ChatFrame1ButtonFrameDownButton"));
	const bool bBottomHovered = IsNamedFrameHovered(TEXT("ChatFrame1ButtonFrameBottomButton"));
	const bool bOverflowHovered = IsNamedFrameHovered(TEXT("ChatTabOverflowNativeButton"));
	const int64 ActiveFrameHandle = FindFrame(*FString::Printf(TEXT("ChatFrame%d"), ActiveChatTabIndex));
	const bool bChatSurfaceHovered = !bChatOccluded && (IsMouseOverFrame(ActiveFrameHandle, 24.0f, 8.0f, -35.0f, 4.0f) || bForceChatHover);
	bool bAnyChatTabHovered = false;
	for (const int32 ChatIndex : EnabledChatTabs)
	{
		bAnyChatTabHovered |= IsNamedFrameHovered(
			*FString::Printf(TEXT("ChatFrame%dTab"), ChatIndex));
	}
	const bool bChatChromeHovered = bChatSurfaceHovered || bAnyChatTabHovered || bOverflowHovered ||
		bChatTabContextMenuOpen || bFriendsHovered ||
		bMenuHovered || bUpHovered || bDownHovered || bBottomHovered;

	// Chat background like 3.3.5: black and see-through while hovered or shortly after a
	// new line, then fading out completely (alpha 0) when nothing has happened for a while.
	// The tabs use the same alpha so they read as part of the window.
	static double LastChatFadeTick = 0.0;
	static float ChatBackgroundAlpha = 0.0f;
	{
		const double FadeNow = FPlatformTime::Seconds();
		const float FadeDelta = LastChatFadeTick > 0.0 ? float(FMath::Min(FadeNow - LastChatFadeTick, 0.25)) : 0.0f;
		LastChatFadeTick = FadeNow;
		const double LastLine = ChatLastActivity.FindRef(ActiveFrameHandle);
		const float Target = bChatChromeHovered ? 0.50f : (LastLine > 0.0 && FadeNow - LastLine < 10.0 ? 0.30f : 0.0f);
		ChatBackgroundAlpha = FMath::FInterpConstantTo(ChatBackgroundAlpha, Target, FadeDelta, Target > ChatBackgroundAlpha ? 3.0f : 0.6f);
	}

	// Size each authored tab around its current label. This also supports custom
	// tab names from addons without leaving the next tab at a fixed offset.
	auto MeasureChatTabWidth = [this](const TCHAR* Name, float FallbackWidth)
	{
		UTextBlock* Label = GetPrimaryTextWidget(FindFrame(Name));
		if (!Label) return FallbackWidth * UIScale;

		float TextWidth = Label->GetText().ToString().Len() * 6.0f * UIScale;
		if (FSlateApplication::IsInitialized())
		{
			const TSharedRef<FSlateFontMeasure> FontMeasure =
				FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			TextWidth = FontMeasure->Measure(Label->GetText(), Label->GetFont()).X;
		}
		return FMath::Clamp(TextWidth + 24.0f * UIScale, 56.0f * UIScale, 220.0f * UIScale);
	};

	auto LayoutAuthoredTabPieces = [this](const TCHAR* Name, float TabWidth)
	{
		const float CapWidth = 16.0f * UIScale;
		const float TextureHeight = 32.0f * UIScale;
		static const TCHAR* States[] = {TEXT(""), TEXT("Selected"), TEXT("Highlight")};
		for (const TCHAR* State : States)
		{
			const FString Prefix = FString(Name) + State;
			auto PlacePiece = [this, TextureHeight](const FString& TextureName, float X, float Width)
			{
				if (UImage* Texture = GetNamedTexture(TextureName))
				{
					if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Texture->Slot))
					{
						Slot->SetAutoSize(false);
						Slot->SetPosition(FVector2D(X, -6.0f * UIScale));
						Slot->SetSize(FVector2D(Width, TextureHeight));
					}
				}
			};
			PlacePiece(Prefix + TEXT("Left"), 0.0f, CapWidth);
			PlacePiece(Prefix + TEXT("Middle"), CapWidth, FMath::Max(1.0f, TabWidth - 2.0f * CapWidth));
			PlacePiece(Prefix + TEXT("Right"), TabWidth - CapWidth, CapWidth);
		}
	};

	TArray<int32> ChatIndices = EnabledChatTabs.Array();
	ChatIndices.Sort();
	TMap<int32, float> TabWidths;
	float NaturalTotal = FMath::Max(0, ChatIndices.Num() - 1) * 2.0f * UIScale;
	for (const int32 ChatIndex : ChatIndices)
	{
		const FString TabName = FString::Printf(TEXT("ChatFrame%dTab"), ChatIndex);
		if (UTextBlock* Label = GetPrimaryTextWidget(FindFrame(TabName)))
		{
			if (const FString* ConfiguredLabel = ChatTabLabels.Find(ChatIndex))
			{
				Label->SetText(FText::FromString(*ConfiguredLabel));
			}
		}
		const float Width = MeasureChatTabWidth(*TabName, ChatIndex == 2 ? 94.0f : 72.0f);
		TabWidths.Add(ChatIndex, Width);
		NaturalTotal += Width;
	}

	UWidget* GeneralTabWidget = GetWidgetForHandle(FindFrame(TEXT("ChatFrame1Tab")));
	UWidget* GeneralFrameWidget = GetWidgetForHandle(FindFrame(TEXT("ChatFrame1")));
	UCanvasPanelSlot* GeneralTabSlot = GeneralTabWidget
		? Cast<UCanvasPanelSlot>(GeneralTabWidget->Slot) : nullptr;
	UCanvasPanelSlot* GeneralFrameSlot = GeneralFrameWidget
		? Cast<UCanvasPanelSlot>(GeneralFrameWidget->Slot) : nullptr;
	// r314: the tab row sits on the chat window's top edge (22 units above it), so it follows the
	// window instead of staying where it was first put.
	const FVector2D TabOrigin = GeneralFrameSlot ? GeneralFrameSlot->GetPosition() - FVector2D(0.0f, 22.0f * UIScale)
		: GeneralTabSlot ? GeneralTabSlot->GetPosition() : FVector2D::ZeroVector;
	if (GeneralFrameSlot && FParse::Param(FCommandLine::Get(), TEXT("testchatten")))
	{
		GeneralFrameSlot->SetSize(FVector2D(580.0f * UIScale, GeneralFrameSlot->GetSize().Y));
	}
	// Stock 3.3.5 keeps a small dock margin instead of stretching tabs across the
	// full message surface. Base this on the live frame width so user resizing
	// naturally changes how many tabs fit (five at the default width). Seven tabs
	// is the largest visible dock; additional windows return to overflow.
	const float FrameWidth = GeneralFrameSlot ? GeneralFrameSlot->GetSize().X : 430.0f * UIScale;
    const FString HideName=TEXT("WowChatVisibilityButton");
    if(RootCanvas.IsValid() && GeneralFrameWidget && GeneralTabSlot && FindFrame(HideName)<0)
    {
        FWowFrameDef Def;Def.Name=HideName;Def.Type=EWowFrameType::Button;
        Def.Width=32;Def.Height=32;Def.Strata=EWowFrameStrata::MEDIUM;
        Def.FrameLevel=30;Def.ButtonText=FString();
        CreateFrame(Def);SetFrameMouseEnabled(FindFrame(HideName),true);
    }
    // Hide/show chat: a 32x32 chat-control button under the speech bubble (ChatFrameMenuButton),
    // a left arrow that slides the chat away like a drawer and a right arrow to pull it back.
    // It stays visible while the chat is hidden. The arrow is an image child of the frame canvas
    // (like the tab overflow arrow); the UButton itself only provides the click and draws nothing.
    {
        const int64 HideHandle=FindFrame(HideName);
        static TWeakObjectPtr<UButton> GStyledChatToggle;
        if(UButton* Button=GetButtonWidget(HideHandle); Button && GStyledChatToggle.Get()!=Button)
        {
            GStyledChatToggle=Button;
            FButtonStyle Style=Button->GetStyle();
            FSlateBrush None; None.DrawAs=ESlateBrushDrawType::NoDrawType;
            Style.Normal=None; Style.Hovered=None; Style.Pressed=None; Style.Disabled=None;
            Style.NormalPadding=FMargin(0); Style.PressedPadding=FMargin(0);
            Button->SetStyle(Style);
        }
        if(UButton* Button=GetButtonWidget(HideHandle))
            Button->SetToolTipText(FText::FromString(bChatWindowHidden?TEXT("Show chat"):TEXT("Hide chat")));
        if(UCanvasPanel* HideCanvas=Cast<UCanvasPanel>(GetWidgetForHandle(HideHandle)))
        {
            UImage* Icon=GetNamedTexture(TEXT("WowChatVisibilityIcon"));
            if(!Icon)
            {
                Icon=NewObject<UImage>(HideCanvas);
                HideCanvas->AddChildToCanvas(Icon);
                NamedTextureWidgets.Add(TEXT("WowChatVisibilityIcon"),Icon);
                if(UCanvasPanelSlot* IconSlot=Cast<UCanvasPanelSlot>(Icon->Slot))
                { IconSlot->SetAnchors(FAnchors(0.f,0.f,1.f,1.f)); IconSlot->SetOffsets(FMargin(0.f)); IconSlot->SetZOrder(10); }
                Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
            }
            const UButton* Button=GetButtonWidget(HideHandle);
            const bool bDown=Button&&Button->IsPressed();
            const int32 State=int32(bChatWindowHidden)*2+int32(bDown);
            static TWeakObjectPtr<UImage> GChatToggleIcon; static int32 GChatToggleIconState=-1;
            if(GChatToggleIcon.Get()!=Icon||GChatToggleIconState!=State)
            {
                GChatToggleIcon=Icon; GChatToggleIconState=State;
                FSlateBrush Brush;
                if(UTexture2D* Texture=LoadUITexture(FString::Printf(TEXT("Interface\\Buttons\\UI-SpellbookIcon-%sPage-%s"),
                    bChatWindowHidden?TEXT("Next"):TEXT("Prev"),bDown?TEXT("Down"):TEXT("Up"))))
                { Brush.SetResourceObject(Texture); Brush.DrawAs=ESlateBrushDrawType::Image; }
                else UE_LOG(LogWowFrame,Warning,TEXT("Chat hide button: arrow texture missing"));
                Brush.ImageSize=FVector2D(32.f,32.f)*UIScale;
                Icon->SetBrush(Brush);
            }
            Icon->SetColorAndOpacity(Button&&Button->IsHovered()?FLinearColor(1.f,1.f,1.f):WowColor::FromWow(.85f, .85f, .85f));
        }
    }
    if(UTextBlock* Label=GetPrimaryTextWidget(FindFrame(HideName)))
        Label->SetText(FText::GetEmpty());
    // UIMenuTemplate menus (speech bubble ChatMenu and its submenus) are sized by UIMenu_Initialize /
    // UIMenu_AutoSize from Lua. If that size never arrived, the frame borrowed UIParent's size and
    // showed as a screen-wide grey box: size it like UIMenu does (16 per button + 12 border each side).
    for(const TCHAR* MenuName:{TEXT("ChatMenu"),TEXT("EmoteMenu"),TEXT("LanguageMenu"),TEXT("VoiceMacroMenu")})
    {
        const int64 MenuHandle=FindFrame(MenuName);
        const FWowFrameDef* MenuDef=MenuHandle>=0?GetFrameDef(MenuHandle):nullptr;
        if(!MenuDef||!IsFrameVisible(MenuHandle)||(MenuDef->Width>0.f&&MenuDef->Width<400.f&&MenuDef->Height>0.f&&MenuDef->Height<700.f))
            continue;
        int32 Shown=0;float ButtonWidth=0.f;
        for(int32 I=1;I<=32;++I)
        {
            const int64 ButtonHandle=FindFrame(FString::Printf(TEXT("%sButton%d"),MenuName,I));
            if(ButtonHandle<0||!IsFrameVisible(ButtonHandle))continue;
            ++Shown;
            if(const FWowFrameDef* ButtonDef=GetFrameDef(ButtonHandle))ButtonWidth=FMath::Max(ButtonWidth,ButtonDef->Width);
        }
        if(ButtonWidth<=0.f||ButtonWidth>300.f)ButtonWidth=104.f;
        SetFrameSize(MenuHandle,ButtonWidth+24.f,FMath::Max(1,Shown)*16.f+24.f);
    }
	const float MaximumFrameWidth = 608.0f * UIScale;
	const float MaximumTabStripWidth = MaximumFrameWidth - 70.0f * UIScale;
	const float ClampedFrameWidth = FMath::Min(FrameWidth, MaximumFrameWidth);
	const float AvailableWidth = FMath::Min(MaximumTabStripWidth,
		FMath::Max(200.0f * UIScale, ClampedFrameWidth - 70.0f * UIScale));
	if (GeneralFrameSlot && FrameWidth > MaximumFrameWidth)
	{
		GeneralFrameSlot->SetSize(FVector2D(MaximumFrameWidth, GeneralFrameSlot->GetSize().Y));
		if (UWidget* CombatFrame = GetWidgetForHandle(FindFrame(TEXT("ChatFrame2"))))
		{
			if (UCanvasPanelSlot* CombatSlot = Cast<UCanvasPanelSlot>(CombatFrame->Slot))
			{
				CombatSlot->SetSize(FVector2D(MaximumFrameWidth, CombatSlot->GetSize().Y));
			}
		}
	}
	const FVector2D LiveChatPosition = GeneralFrameSlot
		? GeneralFrameSlot->GetPosition() : FVector2D::ZeroVector;
	const FVector2D LiveChatSize = GeneralFrameSlot
		? GeneralFrameSlot->GetSize() : FVector2D(430.0f, 120.0f) * UIScale;

	// Keep the social control near the tabs, while the speech/scroll controls
	// remain stacked upward from the live bottom edge as the box is resized.
	auto MoveRootWidget = [this](const TCHAR* Name, const FVector2D& Position)
	{
		if (UWidget* Widget = GetWidgetForHandle(FindFrame(Name)))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				Slot->SetPosition(Position);
			}
		}
	};
	// The column (friends, speech bubble, hide-chat arrow, scroll buttons) hangs from the chat
	// window's top edge to its left (Stefan 10.10: it sat above the chat, up at the tabs).
	const FVector2D ChatControlBase(
		LiveChatPosition.X - 30.0f * UIScale,
		LiveChatPosition.Y + 34.0f * UIScale);
	MoveRootWidget(TEXT("FriendsMicroButton"),
		FVector2D(LiveChatPosition.X - 30.0f * UIScale, LiveChatPosition.Y));
	MoveRootWidget(TEXT("ChatFrameMenuButton"), ChatControlBase);
	// These controls are authored beneath ChatFrame1ButtonFrame, but our flat
	// FrameXML widget construction gives each one a root canvas slot. Position
	// them explicitly or they retain unrelated XML coordinates in the world.
	// Hide/show chat sits right under the speech bubble; the scroll buttons follow.
	// Placed from the speech bubble's live slot, so it always sits directly under it.
	if (UWidget* Toggle = GetWidgetForHandle(FindFrame(HideName)))
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Toggle->Slot))
		{
			FVector2D BubblePos = ChatControlBase;
			FVector2D BubbleSize = FVector2D(32.0f, 32.0f) * UIScale;
			if (UWidget* Bubble = GetWidgetForHandle(FindFrame(TEXT("ChatFrameMenuButton"))))
				if (UCanvasPanelSlot* BubbleSlot = Cast<UCanvasPanelSlot>(Bubble->Slot))
				{
					BubblePos = BubbleSlot->GetPosition();
					if (BubbleSlot->GetSize().X > 1.0f) BubbleSize = BubbleSlot->GetSize();
				}
			Slot->SetAnchors(FAnchors(0.0f, 0.0f));
			Slot->SetAlignment(FVector2D(0.0f, 0.0f));
			Slot->SetAutoSize(false);
			Slot->SetPosition(BubblePos + FVector2D(0.0f, BubbleSize.Y + 2.0f * UIScale));
			Slot->SetSize(BubbleSize);
		}
	// (The scroll buttons are placed with their ButtonFrame below.)
	// Diagnostic (once a minute): visible frames in the control column left of the chat,
	// to identify a stray light square under the hide-chat arrow.
	{
		static double NextColumnDump = 0.0;
		const double DumpNow = FPlatformTime::Seconds();
		if (DumpNow >= NextColumnDump && RootCanvas.IsValid())
		{
			NextColumnDump = DumpNow + 60.0;
			const FGeometry& RootGeometry = RootCanvas->GetCachedGeometry();
			for (const auto& Pair : Frames)
			{
				const UWidget* W = Pair.Value.Widget.Get();
				if (!W || !W->IsVisible() || W->GetRenderOpacity() <= 0.01f) continue;
				const FGeometry& G = W->GetCachedGeometry();
				const FVector2D TopLeft = RootGeometry.AbsoluteToLocal(G.GetAbsolutePosition());
				const FVector2D Size = G.GetAbsoluteSize() / FMath::Max(0.01f, RootGeometry.Scale);
				if (Size.X <= 0.f || Size.Y <= 0.f) continue;
				if (TopLeft.X < LiveChatPosition.X && TopLeft.X + Size.X > 0.f &&
					TopLeft.Y + Size.Y > ChatControlBase.Y - 80.0f * UIScale && TopLeft.Y < LiveChatPosition.Y + LiveChatSize.Y + 10.f)
					UE_LOG(LogWowFrame, Log, TEXT("ChatColumn: %s at (%.0f,%.0f) size (%.0f,%.0f)"),
						*Pair.Value.Def.Name, TopLeft.X, TopLeft.Y, Size.X, Size.Y);
			}
			for (const auto& Pair : NamedTextureWidgets)
			{
				const UImage* W = Pair.Value.Get();
				if (!W || !W->IsVisible() || W->GetRenderOpacity() <= 0.01f) continue;
				const FGeometry& G = W->GetCachedGeometry();
				const FVector2D TopLeft = RootGeometry.AbsoluteToLocal(G.GetAbsolutePosition());
				const FVector2D Size = G.GetAbsoluteSize() / FMath::Max(0.01f, RootGeometry.Scale);
				if (Size.X <= 0.f || Size.Y <= 0.f) continue;
				if (TopLeft.X < LiveChatPosition.X && TopLeft.X + Size.X > 0.f &&
					TopLeft.Y + Size.Y > ChatControlBase.Y - 80.0f * UIScale && TopLeft.Y < LiveChatPosition.Y + LiveChatSize.Y + 10.f)
					UE_LOG(LogWowFrame, Log, TEXT("ChatColumn texture: %s at (%.0f,%.0f) size (%.0f,%.0f)"),
						*Pair.Key, TopLeft.X, TopLeft.Y, Size.X, Size.Y);
			}
			// r269 diagnostic (Stefan 13.10: the General / Combat Log tabs are not drawn even with the
			// chat hovered; the VR zone log shows ChatFrame1Tab with no painted geometry): each tab's
			// state and its widget chain up to the root (visibility as a number, render opacity).
			for (int32 TabIndex = 1; TabIndex <= 2; ++TabIndex)
			{
				const int64 TabHandle = FindFrame(*FString::Printf(TEXT("ChatFrame%dTab"), TabIndex));
				const UWidget* TabWidget = GetWidgetForHandle(TabHandle);
				if (!TabWidget) continue;
				FString Chain;
				for (const UWidget* Link = TabWidget; Link && Chain.Len() < 700; Link = Link->GetParent())
				{
					FString LinkName = Link->GetName();
					for (const auto& FramePair : Frames)
						if (FramePair.Value.Widget.Get() == Link) { LinkName = FramePair.Value.Def.Name; break; }
					Chain += FString::Printf(TEXT(" > %s[v%d a%.2f]"), *LinkName, int32(Link->GetVisibility()), Link->GetRenderOpacity());
				}
				const FGeometry& TabGeometry = TabWidget->GetCachedGeometry();
				const FVector2D TabTopLeft = RootGeometry.AbsoluteToLocal(TabGeometry.GetAbsolutePosition());
				const FVector2D TabSize = TabGeometry.GetAbsoluteSize() / FMath::Max(0.01f, RootGeometry.Scale);
				UE_LOG(LogWowFrame, Log, TEXT("Chat tab %d: logical=%d enabled=%d layoutHidden=%d chatHidden=%d at (%.0f,%.0f) size (%.0f,%.0f) chat at (%.0f,%.0f)%s"),
					TabIndex, IsFrameVisible(TabHandle) ? 1 : 0, EnabledChatTabs.Contains(TabIndex) ? 1 : 0,
					LayoutHiddenChatTabs.Contains(TabIndex) ? 1 : 0, bChatWindowHidden ? 1 : 0,
					TabTopLeft.X, TabTopLeft.Y, TabSize.X, TabSize.Y, LiveChatPosition.X, LiveChatPosition.Y, *Chain);
			}
		}
	}
	// Scroll up / down / to the bottom: a strip on the chat's right edge like a scroll bar - up at
	// the top, down and bottom at the bottom (Stefan 10.10: in the left column they lay over the
	// speech bubble, social and hide buttons, so clicking those scrolled the chat). The frame is
	// a child of the chat frame, so these positions are chat-relative.
	{
		const float ScrollButtonSize = 24.0f * UIScale;
		const float StripHeight = FMath::Max(LiveChatSize.Y, 3.0f * ScrollButtonSize);
		if (UWidget* Strip = GetWidgetForHandle(FindFrame(TEXT("ChatFrame1ButtonFrame"))))
			if (UCanvasPanelSlot* StripSlot = Cast<UCanvasPanelSlot>(Strip->Slot))
			{
				StripSlot->SetAutoSize(false);
				StripSlot->SetPosition(FVector2D(LiveChatSize.X + 2.0f * UIScale, 0.0f));
				StripSlot->SetSize(FVector2D(ScrollButtonSize, StripHeight));
			}
		auto PlaceStripButton = [this, ScrollButtonSize](const TCHAR* StripButtonName, float StripButtonY)
		{
			if (UWidget* StripButton = GetWidgetForHandle(FindFrame(StripButtonName)))
				if (UCanvasPanelSlot* StripButtonSlot = Cast<UCanvasPanelSlot>(StripButton->Slot))
				{
					StripButtonSlot->SetAutoSize(false);
					StripButtonSlot->SetPosition(FVector2D(0.0f, StripButtonY));
					StripButtonSlot->SetSize(FVector2D(ScrollButtonSize));
				}
		};
		PlaceStripButton(TEXT("ChatFrame1ButtonFrameUpButton"), 0.0f);
		PlaceStripButton(TEXT("ChatFrame1ButtonFrameDownButton"), StripHeight - 2.0f * ScrollButtonSize);
		PlaceStripButton(TEXT("ChatFrame1ButtonFrameBottomButton"), StripHeight - ScrollButtonSize);
	}

	// Repair the data layer of the authored Social window. The portrait, frame,
	// close button and list surface remain stock; only labels/list data are native.
	const int64 FriendsFrameHandle = FindFrame(TEXT("FriendsFrame"));
	const bool bShowSocial = IsFrameVisible(FriendsFrameHandle);
	FVector2D SocialPosition(18.0f, 165.0f);
	FVector2D SocialSize(470.0f, 590.0f);
	if (UWidget* FriendsWidget = GetWidgetForHandle(FriendsFrameHandle))
	{
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(FriendsWidget->Slot))
		{
			SocialPosition = Slot->GetPosition();
			SocialSize = Slot->GetSize();
		}
	}
	// These Battle.net-era controls are not part of the 3.3.5-style view shown
	// here and currently retain invalid flattened anchors.
	static const TCHAR* BrokenSocialControls[] = {
		TEXT("FriendsFrameBattlenetStatus"), TEXT("FriendsFrameStatusDropDown"),
		TEXT("FriendsFrameBroadcastInput"), TEXT("FriendsTabHeader"),
		TEXT("FriendsFriendsFrameDropDown"),
		TEXT("FriendsFrameFriendsScrollFrameScrollBar"),
		TEXT("FriendsFrameFriendsScrollFrameScrollBarScrollUpButton"),
		TEXT("FriendsFrameFriendsScrollFrameScrollBarScrollDownButton")
	};
	for (const TCHAR* ControlName : BrokenSocialControls)
	{
		SetFrameVisible(FindFrame(ControlName), false);
	}
	for (const TCHAR* ChatTabName : {TEXT("ChatFrame1Tab"), TEXT("ChatFrame2Tab")})
	{
		if (UWidget* ChatTab = GetWidgetForHandle(FindFrame(ChatTabName)))
		{
			ChatTab->SetRenderOpacity(bShowSocial ? 0.0f : 1.0f);
		}
	}
	auto LayoutSocialWidget = [this, bShowSocial](const TCHAR* Name,
		const FVector2D& Position, const FVector2D& Size, int32 ZOrder)
	{
		const int64 Handle = FindFrame(Name);
		SetFrameVisible(Handle, bShowSocial);
		if (UWidget* Widget = GetWidgetForHandle(Handle))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				Slot->SetAutoSize(false);
				Slot->SetPosition(Position);
				Slot->SetSize(Size);
				Slot->SetZOrder(ZOrder);
			}
		}
	};
	LayoutSocialWidget(TEXT("SocialNativeTitle"), SocialPosition + FVector2D(82.0f, 13.0f) * UIScale,
		FVector2D(270.0f, 24.0f) * UIScale, 32000);
	LayoutSocialWidget(TEXT("SocialNativeSummary"), SocialPosition + FVector2D(78.0f, 43.0f) * UIScale,
		FVector2D(150.0f, 22.0f) * UIScale, 32000);
	LayoutSocialWidget(TEXT("SocialNativeListTab1"), SocialPosition + FVector2D(35.0f, 78.0f) * UIScale,
		FVector2D(78.0f, 24.0f) * UIScale, 32000);
	LayoutSocialWidget(TEXT("SocialNativeListTab2"), SocialPosition + FVector2D(118.0f, 78.0f) * UIScale,
		FVector2D(78.0f, 24.0f) * UIScale, 32000);
	if (UTextBlock* Title = GetPrimaryTextWidget(FindFrame(TEXT("SocialNativeTitle"))))
	{
		static const TCHAR* SocialTitles[] = {
			TEXT("Friends List"), TEXT("Who List"), TEXT("Guild"), TEXT("Chat Channels"), TEXT("Raid")
		};
		Title->SetText(FText::FromString(SocialTitles[FMath::Clamp(ActiveSocialTabIndex - 1, 0, 4)]));
		Title->SetJustification(ETextJustify::Center);
		Title->SetColorAndOpacity(FSlateColor(WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f)));
	}
	if (UTextBlock* Summary = GetPrimaryTextWidget(FindFrame(TEXT("SocialNativeSummary"))))
	{
		Summary->SetText(FText::FromString(FString::Printf(TEXT("Online  •  Friends %d / %d"),
			SocialOnlineFriendCount, SocialFriendEntries.Num())));
		Summary->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.2f, 1.0f, 0.2f, 1.0f)));
	}
	if (UTextBlock* FriendsTab = GetPrimaryTextWidget(FindFrame(TEXT("SocialNativeListTab1"))))
	{
		FriendsTab->SetColorAndOpacity(FSlateColor(!bSocialIgnoreSelected
			? WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f) : WowColor::FromWow(0.65f, 0.60f, 0.48f, 1.0f)));
	}
	if (UTextBlock* IgnoreTab = GetPrimaryTextWidget(FindFrame(TEXT("SocialNativeListTab2"))))
	{
		IgnoreTab->SetColorAndOpacity(FSlateColor(bSocialIgnoreSelected
			? WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f) : WowColor::FromWow(0.65f, 0.60f, 0.48f, 1.0f)));
	}

	const bool bFriendsPanelActive = ActiveSocialTabIndex == 1;
	TArray<FString> VisibleSocialEntries = bSocialIgnoreSelected
		? SocialIgnoreEntries : SocialFriendEntries;
	if (!bSocialIgnoreSelected && !RecentWhisperSender.IsEmpty()
		&& !VisibleSocialEntries.Contains(RecentWhisperSender))
	{
		VisibleSocialEntries.Insert(RecentWhisperSender, 0);
	}
	for (int32 RowIndex = 1; RowIndex <= 8; ++RowIndex)
	{
		const bool bHasEntry = bFriendsPanelActive &&
			VisibleSocialEntries.IsValidIndex(RowIndex - 1);
		const FString RowName = FString::Printf(TEXT("SocialNativeFriendRow%d"), RowIndex);
		LayoutSocialWidget(*RowName,
			SocialPosition + FVector2D(42.0f, 118.0f + (RowIndex - 1) * 34.0f) * UIScale,
			FVector2D(285.0f, 32.0f) * UIScale, 32000);
		SetFrameVisible(FindFrame(RowName), bShowSocial && bHasEntry);
		if (bHasEntry)
		{
			if (UTextBlock* RowLabel = GetPrimaryTextWidget(FindFrame(RowName)))
			{
				const FString& EntryText = VisibleSocialEntries[RowIndex - 1];
				const bool bRecent = EntryText == RecentWhisperSender;
				RowLabel->SetText(FText::FromString(bRecent
					? FString::Printf(TEXT("● %s\n   Recent whisper"), *EntryText)
					: FString::Printf(TEXT("● %s"), *EntryText)));
				RowLabel->SetColorAndOpacity(FSlateColor(bRecent
					? WowColor::FromWow(1.0f, 0.55f, 1.0f, 1.0f)
					: WowColor::FromWow(0.72f, 0.82f, 1.0f, 1.0f)));
			}
		}
	}
	const bool bShowSocialEmpty = !bFriendsPanelActive || VisibleSocialEntries.IsEmpty();
	LayoutSocialWidget(TEXT("SocialNativeEmpty"), SocialPosition + FVector2D(42.0f, 120.0f) * UIScale,
		FVector2D(285.0f, 24.0f) * UIScale, 32000);
	SetFrameVisible(FindFrame(TEXT("SocialNativeEmpty")), bShowSocial && bShowSocialEmpty);
	if (UTextBlock* EmptyLabel = GetPrimaryTextWidget(FindFrame(TEXT("SocialNativeEmpty"))))
	{
		static const TCHAR* SocialPanelEmptyText[] = {
			TEXT("You have no friends listed."), TEXT("No player search results."),
			TEXT("No guild roster is available."), TEXT("No joined chat channels."),
			TEXT("You are not in a raid group.")
		};
		EmptyLabel->SetText(FText::FromString(!bFriendsPanelActive
			? SocialPanelEmptyText[FMath::Clamp(ActiveSocialTabIndex - 1, 0, 4)]
			: (bSocialIgnoreSelected ? TEXT("You are not ignoring anyone.")
				: TEXT("You have no friends listed."))));
		EmptyLabel->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.55f, 0.55f, 0.55f, 1.0f)));
	}
	LayoutSocialWidget(TEXT("SocialNativeAddFriendButton"),
		SocialPosition + FVector2D(18.0f, SocialSize.Y / UIScale - 120.0f) * UIScale,
		FVector2D(132.0f, 24.0f) * UIScale, 32000);
	LayoutSocialWidget(TEXT("SocialNativeReplyButton"),
		SocialPosition + FVector2D(205.0f, SocialSize.Y / UIScale - 120.0f) * UIScale,
		FVector2D(145.0f, 24.0f) * UIScale, 32000);
	SetFrameVisible(FindFrame(TEXT("SocialNativeAddFriendButton")), bShowSocial && bFriendsPanelActive);
	SetFrameVisible(FindFrame(TEXT("SocialNativeReplyButton")),
		bShowSocial && bFriendsPanelActive && !RecentWhisperSender.IsEmpty());
	SetFrameVisible(FindFrame(TEXT("SocialNativeListTab1")), bShowSocial && bFriendsPanelActive);
	SetFrameVisible(FindFrame(TEXT("SocialNativeListTab2")), bShowSocial && bFriendsPanelActive);
	static const TCHAR* BottomSocialTabs[] = {TEXT("Friends"), TEXT("Who"), TEXT("Guild"), TEXT("Chat"), TEXT("Raid")};
	for (int32 TabIndex = 1; TabIndex <= UE_ARRAY_COUNT(BottomSocialTabs); ++TabIndex)
	{
		const FString TabName = FString::Printf(TEXT("SocialNativeTab%d"), TabIndex);
		LayoutSocialWidget(*TabName,
			SocialPosition + FVector2D(10.0f + (TabIndex - 1) * 58.0f,
				SocialSize.Y / UIScale - 78.0f) * UIScale,
			FVector2D(55.0f, 26.0f) * UIScale, 32010);
		if (UTextBlock* TabLabel = GetPrimaryTextWidget(FindFrame(TabName)))
		{
			TabLabel->SetJustification(ETextJustify::Center);
			TabLabel->SetColorAndOpacity(FSlateColor(ActiveSocialTabIndex == TabIndex
				? WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f)
				: WowColor::FromWow(0.55f, 0.52f, 0.45f, 1.0f)));
		}
	}
	auto LayoutSocialPlate = [this, bShowSocial](const TCHAR* Name,
		const FVector2D& Position, const FVector2D& Size, bool bVisible)
	{
		if (UImage* Plate = GetNamedTexture(Name))
		{
			Plate->SetVisibility(bShowSocial && bVisible
				? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Plate->Slot))
			{
				Slot->SetAutoSize(false);
				Slot->SetPosition(Position);
				Slot->SetSize(Size);
				Slot->SetZOrder(31990);
			}
		}
	};
	LayoutSocialPlate(TEXT("SocialNativeListTab1Plate"),
		SocialPosition + FVector2D(33.0f, 77.0f) * UIScale, FVector2D(80.0f, 26.0f) * UIScale,
		bFriendsPanelActive);
	LayoutSocialPlate(TEXT("SocialNativeListTab2Plate"),
		SocialPosition + FVector2D(116.0f, 77.0f) * UIScale, FVector2D(80.0f, 26.0f) * UIScale,
		bFriendsPanelActive);
	const FVector2D ActionButtonY(0.0f, SocialSize.Y / UIScale - 121.0f);
	LayoutSocialPlate(TEXT("SocialNativeAddFriendPlate"),
		SocialPosition + (FVector2D(16.0f, 0.0f) + ActionButtonY) * UIScale,
		FVector2D(136.0f, 26.0f) * UIScale, bFriendsPanelActive);
	LayoutSocialPlate(TEXT("SocialNativeReplyPlate"),
		SocialPosition + (FVector2D(203.0f, 0.0f) + ActionButtonY) * UIScale,
		FVector2D(149.0f, 26.0f) * UIScale, bFriendsPanelActive && !RecentWhisperSender.IsEmpty());
	for (int32 TabIndex = 1; TabIndex <= 5; ++TabIndex)
	{
		LayoutSocialPlate(*FString::Printf(TEXT("SocialNativeTab%dPlate"), TabIndex),
			SocialPosition + FVector2D(9.0f + (TabIndex - 1) * 58.0f,
				SocialSize.Y / UIScale - 79.0f) * UIScale,
			FVector2D(57.0f, 28.0f) * UIScale, true);
	}
	for (int32 RowIndex = 1; RowIndex <= 8; ++RowIndex)
	{
		const bool bHasEntry = bFriendsPanelActive &&
			VisibleSocialEntries.IsValidIndex(RowIndex - 1);
		const FString PlateName = FString::Printf(TEXT("SocialNativeFriendRow%dPlate"), RowIndex);
		LayoutSocialPlate(*PlateName,
			SocialPosition + FVector2D(34.0f, 115.0f + (RowIndex - 1) * 34.0f) * UIScale,
			FVector2D(306.0f, 34.0f) * UIScale, bHasEntry);
		if (UImage* Plate = GetNamedTexture(PlateName))
		{
			const bool bSelected = SocialContextFriendRow == RowIndex ||
				(RowIndex == 1 && !RecentWhisperSender.IsEmpty());
			Plate->SetBrush(FSlateRoundedBoxBrush(bSelected
				? WowColor::FromWow(0.07f, 0.24f, 0.46f, 0.88f)
				: WowColor::FromWow(0.025f, 0.035f, 0.055f, 0.45f), 1.0f * UIScale));
		}
	}
	const FVector2D FriendMenuPosition = SocialPosition + FVector2D(215.0f,
		115.0f + FMath::Max(0, SocialContextFriendRow - 1) * 34.0f) * UIScale;
	LayoutSocialPlate(TEXT("SocialNativeFriendContextPlate"), FriendMenuPosition,
		FVector2D(132.0f, 86.0f) * UIScale, bSocialFriendContextMenuOpen);
	if (UImage* ContextPlate = GetNamedTexture(TEXT("SocialNativeFriendContextPlate")))
	{
		ContextPlate->SetBrush(FSlateRoundedBoxBrush(
			WowColor::FromWow(0.006f, 0.006f, 0.010f, 0.98f), 3.0f * UIScale,
			WowColor::FromWow(0.50f, 0.50f, 0.52f, 0.95f), 1.0f * UIScale));
	}
	for (int32 ContextIndex = 1; ContextIndex <= 4; ++ContextIndex)
	{
		const FString RowName = FString::Printf(TEXT("SocialNativeFriendContextRow%d"), ContextIndex);
		LayoutSocialWidget(*RowName,
			FriendMenuPosition + FVector2D(6.0f, 5.0f + (ContextIndex - 1) * 19.0f) * UIScale,
			FVector2D(120.0f, 18.0f) * UIScale, 32100 + ContextIndex);
		SetFrameVisible(FindFrame(RowName), bShowSocial && bSocialFriendContextMenuOpen);
		if (UTextBlock* ContextLabel = GetPrimaryTextWidget(FindFrame(RowName)))
		{
			ContextLabel->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.95f, 0.88f, 0.68f, 1.0f)));
		}
	}

	// r292 (Stefan 13.10: "there should be a resize triangle down right"): like the client, the size
	// grabber shows while the mouse is over an unlocked chat window (or while resizing).
	const int64 ResizeHandle = FindFrame(TEXT("ChatFrameNativeResizeButton"));
	SetFrameVisible(ResizeHandle, bChatFrameUnlocked && (bChatChromeHovered || bChatResizeActive
		|| IsNamedFrameHovered(TEXT("ChatFrameNativeResizeButton"))));
	if (UWidget* ResizeWidget = GetWidgetForHandle(ResizeHandle))
	{
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ResizeWidget->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition(LiveChatPosition + LiveChatSize - FVector2D(16.0f * UIScale));
			Slot->SetSize(FVector2D(16.0f * UIScale));
			Slot->SetZOrder(24000);
		}
	}

	// The Combat Log has its own display-only category strip. It occupies the
	// top of the message surface and is only present while that tab is active.
	const bool bShowCombatFilters = ActiveChatTabIndex == 2;
	// Stock CombatLog.lua can re-show these after the initial visibility pass.
	// They are not usable until the full quick-button system exists and currently
	// surface as an untextured white square beside our native dropdown.
	static const TCHAR* IncompleteStockCombatControls[] = {
		TEXT("CombatLogDefaultButton"), TEXT("CombatLogQuickButtonFrame"),
		TEXT("CombatLogQuickButtonFrame_Custom"),
		TEXT("CombatLogQuickButtonFrame_CustomAdditionalFilterButton"),
		TEXT("CombatLogQuickButtonFrameButton1"), TEXT("CombatLogQuickButtonFrameButton2"),
		TEXT("CombatLogQuickButtonFrameButton3"), TEXT("CombatLogUpdateFrame"),
		TEXT("ChatFrame2ButtonFrame"), TEXT("ChatFrame2ButtonFrameBottomButton"),
		TEXT("ChatFrame2ButtonFrameDownButton"), TEXT("ChatFrame2ButtonFrameUpButton"),
		TEXT("ChatFrame2ButtonFrameMinimizeButton")
	};
	for (const TCHAR* ControlName : IncompleteStockCombatControls)
	{
		SetFrameVisible(FindFrame(ControlName), false);
	}
	static const TCHAR* IncompleteStockCombatTextures[] = {
		TEXT("ChatFrame2ButtonFrameBackground"), TEXT("ChatFrame2ButtonFrameTopLeftTexture"),
		TEXT("ChatFrame2ButtonFrameBottomLeftTexture"), TEXT("ChatFrame2ButtonFrameTopRightTexture"),
		TEXT("ChatFrame2ButtonFrameBottomRightTexture"), TEXT("ChatFrame2ButtonFrameLeftTexture"),
		TEXT("ChatFrame2ButtonFrameRightTexture"), TEXT("ChatFrame2ButtonFrameBottomTexture"),
		TEXT("ChatFrame2ButtonFrameTopTexture"), TEXT("ChatFrame2ButtonFrameBottomButtonFlash")
	};
	for (const TCHAR* TextureName : IncompleteStockCombatTextures)
	{
		if (UImage* Texture = GetNamedTexture(TextureName))
		{
			Texture->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	const FVector2D FilterStripPosition = LiveChatPosition + FVector2D(0.0f, 2.0f * UIScale);
	const FVector2D FilterStripSize(LiveChatSize.X, 23.0f * UIScale);
	if (UImage* FilterStrip = GetNamedTexture(TEXT("CombatLogFilterStripBackground")))
	{
		FilterStrip->SetVisibility(bShowCombatFilters
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(FilterStrip->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition(FilterStripPosition);
			Slot->SetSize(FilterStripSize);
			Slot->SetZOrder(23000);
		}
	}
	static const TCHAR* CombatFilterNames[] = {
		TEXT("CombatLogFilterSelf"), TEXT("CombatLogFilterEverything"),
		TEXT("CombatLogFilterWhatHappened"), TEXT("CombatLogFilterDropDownButton")
	};
	const float FilterWidths[] = {55.0f, 88.0f, 148.0f, 18.0f};
	float FilterX = FilterStripPosition.X + 5.0f * UIScale;
	for (int32 FilterIndex = 0; FilterIndex < UE_ARRAY_COUNT(CombatFilterNames); ++FilterIndex)
	{
		const int64 FilterHandle = FindFrame(CombatFilterNames[FilterIndex]);
		SetFrameVisible(FilterHandle, bShowCombatFilters);
		const float Width = FilterWidths[FilterIndex] * UIScale;
		const float X = FilterIndex == 3
			? FilterStripPosition.X + FilterStripSize.X - Width - 5.0f * UIScale : FilterX;
		if (UWidget* Widget = GetWidgetForHandle(FilterHandle))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				Slot->SetAutoSize(false);
				Slot->SetPosition(FVector2D(X, FilterStripPosition.Y + 2.0f * UIScale));
				Slot->SetSize(FVector2D(Width, 22.0f * UIScale));
				Slot->SetZOrder(23010 + FilterIndex);
			}
		}
		if (UTextBlock* Label = GetPrimaryTextWidget(FilterHandle))
		{
			const bool bSelected = FilterIndex < 3 && ActiveCombatLogFilterIndex == FilterIndex + 1;
			Label->SetColorAndOpacity(FSlateColor(bSelected
				? WowColor::FromWow(0.96f, 0.93f, 0.84f, 1.0f)
				: WowColor::FromWow(0.48f, 0.48f, 0.47f, 0.92f)));
		}
		if (FilterIndex < 3) FilterX += Width;
	}
	const FVector2D DropDownPosition(FilterStripPosition.X + FilterStripSize.X - 23.0f * UIScale,
		FilterStripPosition.Y + 3.0f * UIScale);
	if (UImage* DropDownPlate = GetNamedTexture(TEXT("CombatLogFilterDropDownBackground")))
	{
		DropDownPlate->SetVisibility(bShowCombatFilters
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(DropDownPlate->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition(DropDownPosition);
			Slot->SetSize(FVector2D(18.0f * UIScale, 20.0f * UIScale));
			Slot->SetZOrder(23005);
		}
	}
	const bool bShowCombatFilterMenu = bShowCombatFilters && bCombatLogFilterMenuOpen;
	const FVector2D FilterMenuPosition(
		FilterStripPosition.X + FilterStripSize.X + 5.0f * UIScale,
		FilterStripPosition.Y + 18.0f * UIScale);
	if (UImage* FilterMenu = GetNamedTexture(TEXT("CombatLogFilterMenuBackground")))
	{
		FilterMenu->SetVisibility(bShowCombatFilterMenu
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(FilterMenu->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition(FilterMenuPosition);
			Slot->SetSize(FVector2D(78.0f, 30.0f) * UIScale);
			Slot->SetZOrder(24020);
		}
	}
	const int64 KillsHandle = FindFrame(TEXT("CombatLogFilterKills"));
	SetFrameVisible(KillsHandle, bShowCombatFilterMenu);
	if (UWidget* KillsWidget = GetWidgetForHandle(KillsHandle))
	{
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(KillsWidget->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition(FilterMenuPosition + FVector2D(5.0f, 3.0f) * UIScale);
			Slot->SetSize(FVector2D(68.0f, 24.0f) * UIScale);
			Slot->SetZOrder(24025);
		}
	}
	if (UTextBlock* KillsLabel = GetPrimaryTextWidget(KillsHandle))
	{
		KillsLabel->SetText(FText::FromString(TEXT("   Kills")));
	}
	if (UImage* CheckImage = GetNamedTexture(TEXT("CombatLogFilterKillsCheckImage")))
	{
		CheckImage->SetVisibility(bShowCombatFilterMenu && bCombatLogKillsEnabled
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	static const TCHAR* GeneralContextLabels[] = {
		TEXT("Unlock Window"), TEXT("Make Noninteractive"), TEXT("Rename Window"),
		TEXT("Create New Window"), TEXT("Reset Chat Windows"), TEXT("Display        >"),
		TEXT("Font Size      >"), TEXT("Background     []"), TEXT("Filters"), TEXT("Settings")
	};
	static const TCHAR* OtherContextLabels[] = {
		TEXT("Unlock Window"), TEXT("Make Noninteractive"), TEXT("Rename Window"),
		TEXT("Display        >"), TEXT("Font Size      >"), TEXT("Background     []"),
		TEXT("Filters"), TEXT("Settings")
	};
	const bool bGeneralContext = ChatContextTabIndex == 1;
	const int32 ContextRowCount = bGeneralContext
		? UE_ARRAY_COUNT(GeneralContextLabels) : UE_ARRAY_COUNT(OtherContextLabels);
	FVector2D ContextPosition = TabOrigin + FVector2D(0.0f, 28.0f * UIScale);
	// The tab's screen rectangle: its slot sits in GeneralDockManager's canvas, so the slot
	// position put the menu in the top-left corner of the screen (Stefan 10.10).
	const FFrameRect ContextTabRect = GetFrameRect(FString::Printf(TEXT("ChatFrame%dTab"), ChatContextTabIndex));
	if (ContextTabRect.W > 0.0f)
	{
		ContextPosition = FVector2D(ContextTabRect.X, ContextTabRect.Y + 28.0f) * UIScale;
	}
	else if (UWidget* ContextTab = GetWidgetForHandle(FindFrame(
		*FString::Printf(TEXT("ChatFrame%dTab"), ChatContextTabIndex))))
	{
		if (UCanvasPanelSlot* ContextTabSlot = Cast<UCanvasPanelSlot>(ContextTab->Slot))
		{
			ContextPosition = ContextTabSlot->GetPosition() + FVector2D(0.0f, 28.0f * UIScale);
		}
	}
	const FVector2D ContextSize(132.0f * UIScale,
		(ContextRowCount * 15.0f + 8.0f) * UIScale);
	if (UImage* ContextBackground = GetNamedTexture(TEXT("ChatTabContextBackground")))
	{
		ContextBackground->SetVisibility(bChatTabContextMenuOpen
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ContextBackground->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition(ContextPosition);
			Slot->SetSize(ContextSize);
			Slot->SetZOrder(23980);
		}
	}
	for (int32 RowIndex = 1; RowIndex <= 10; ++RowIndex)
	{
		const int64 RowHandle = FindFrame(*FString::Printf(TEXT("ChatTabContextRow%d"), RowIndex));
		const bool bUsed = RowIndex <= ContextRowCount;
		SetFrameVisible(RowHandle, bChatTabContextMenuOpen && bUsed);
		if (!bUsed) continue;
		if (UWidget* RowWidget = GetWidgetForHandle(RowHandle))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(RowWidget->Slot))
			{
				Slot->SetAutoSize(false);
				Slot->SetPosition(ContextPosition + FVector2D(4.0f * UIScale,
					(4.0f + (RowIndex - 1) * 15.0f) * UIScale));
				Slot->SetSize(FVector2D(124.0f * UIScale, 15.0f * UIScale));
				Slot->SetZOrder(23990 + RowIndex);
			}
		}
		if (UTextBlock* Label = GetPrimaryTextWidget(RowHandle))
		{
			FString RowLabel = bGeneralContext
				? GeneralContextLabels[RowIndex - 1] : OtherContextLabels[RowIndex - 1];
			if (RowIndex == 1 && bChatFrameUnlocked)
			{
				RowLabel = TEXT("Lock Window");
			}
			else if (RowIndex == 2 && bChatFrameNonInteractive)
			{
				RowLabel = TEXT("Make Interactive");
			}
			const int32 FontRow = bGeneralContext ? 7 : 5;
			const int32 BackgroundRow = bGeneralContext ? 8 : 6;
			if (RowIndex == FontRow)
			{
				RowLabel = FString::Printf(TEXT("Font Size      %d"), ChatFontSize);
			}
			else if (RowIndex == BackgroundRow)
			{
				RowLabel = FString::Printf(TEXT("Background     [%s]"),
					bChatBackgroundEnabled ? TEXT("x") : TEXT(" "));
			}
			Label->SetText(FText::FromString(RowLabel));
			Label->SetColorAndOpacity(FSlateColor(HoverFrameHandle == RowHandle
				? WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f)
				: WowColor::FromWow(0.92f, 0.88f, 0.75f, 1.0f)));
		}
	}
	const float OverflowButtonWidth = 20.0f * UIScale;
	const float Gap = 2.0f * UIScale;
	OverflowChatTabIndices.Reset();

	// First reduce generous text padding. Only after that do tabs move into overflow.
	if (NaturalTotal > AvailableWidth && !ChatIndices.IsEmpty())
	{
		const float ReductionPerTab = FMath::Min(6.0f * UIScale,
			(NaturalTotal - AvailableWidth) / ChatIndices.Num());
		NaturalTotal = FMath::Max(0, ChatIndices.Num() - 1) * Gap;
		for (const int32 ChatIndex : ChatIndices)
		{
			float& Width = TabWidths.FindChecked(ChatIndex);
			Width = FMath::Max(42.0f * UIScale, Width - ReductionPerTab);
			NaturalTotal += Width;
		}
	}

	const float AvailableWidthInUiUnits = UIScale > KINDA_SMALL_NUMBER
		? AvailableWidth / UIScale : AvailableWidth;
	const int32 VisibleCapacity = FMath::Clamp(5 + FMath::FloorToInt(
		(AvailableWidthInUiUnits - 360.0f) / 75.0f), 1, 7);
	const bool bNeedsOverflow = ChatIndices.Num() > VisibleCapacity || NaturalTotal > AvailableWidth;
	const float RowLimit = AvailableWidth - (bNeedsOverflow ? OverflowButtonWidth + Gap : 0.0f);
	TArray<int32> VisibleChatTabIndices;
	float VisibleNaturalWidth = 0.0f;
	for (const int32 ChatIndex : ChatIndices)
	{
		const float Width = TabWidths.FindChecked(ChatIndex);
		const float CandidateWidth = VisibleNaturalWidth +
			(VisibleChatTabIndices.IsEmpty() ? 0.0f : Gap) + Width;
		const bool bFits = VisibleChatTabIndices.Num() < VisibleCapacity &&
			(CandidateWidth <= RowLimit || VisibleChatTabIndices.IsEmpty());
		if (!bFits)
		{
			OverflowChatTabIndices.Add(ChatIndex);
			continue;
		}
		VisibleNaturalWidth = CandidateWidth;
		VisibleChatTabIndices.Add(ChatIndex);
	}

	// The stock dock distributes spare room across its visible tabs, leaving no
	// empty gap before the overflow control.
	const float ExtraWidthPerTab = !bNeedsOverflow || VisibleChatTabIndices.IsEmpty() ? 0.0f
		: FMath::Max(0.0f, RowLimit - VisibleNaturalWidth) / VisibleChatTabIndices.Num();
	float CursorX = 0.0f;
	for (const int32 ChatIndex : ChatIndices)
	{
		const FString TabName = FString::Printf(TEXT("ChatFrame%dTab"), ChatIndex);
		const int64 TabHandle = FindFrame(TabName);
		const bool bVisible = VisibleChatTabIndices.Contains(ChatIndex);
		if (bVisible) LayoutHiddenChatTabs.Remove(ChatIndex); else LayoutHiddenChatTabs.Add(ChatIndex);
        SetFrameMouseEnabled(TabHandle,true);
        SetFrameRegisteredClicks(TabHandle,{TEXT("LeftButtonUp"),TEXT("RightButtonUp")});
		SetFrameVisible(TabHandle, bVisible);
		if (!bVisible)
		{
			continue;
		}
		UWidget* TabWidget = GetWidgetForHandle(TabHandle);
		UCanvasPanelSlot* TabSlot = TabWidget ? Cast<UCanvasPanelSlot>(TabWidget->Slot) : nullptr;
		const float Width = TabWidths.FindChecked(ChatIndex) + ExtraWidthPerTab;
		if (TabSlot)
		{
			TabSlot->SetAutoSize(false);
			TabSlot->SetPosition(TabOrigin + FVector2D(CursorX, 0.0f));
			TabSlot->SetSize(FVector2D(Width, 28.0f * UIScale));
			TabSlot->SetZOrder(20000);
		}
		LayoutAuthoredTabPieces(*TabName, Width);
		CursorX += Width + Gap;
	}

	const int64 OverflowButtonHandle = FindFrame(TEXT("ChatTabOverflowNativeButton"));
	const int64 OverflowListHandle = FindFrame(TEXT("GeneralDockManagerOverflowButtonList"));
	const float OverflowButtonX = FMath::Max(0.0f, AvailableWidth - OverflowButtonWidth);
	SetFrameVisible(OverflowButtonHandle, !OverflowChatTabIndices.IsEmpty());
	if (UWidget* OverflowButton = GetWidgetForHandle(OverflowButtonHandle))
	{
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(OverflowButton->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition(TabOrigin + FVector2D(OverflowButtonX, 1.0f * UIScale));
			Slot->SetSize(FVector2D(OverflowButtonWidth, 22.0f * UIScale));
			Slot->SetZOrder(20500);
		}
	}
	if (UImage* Arrow = GetNamedTexture(TEXT("ChatTabOverflowNativeArrow")))
	{
		Arrow->SetColorAndOpacity(bOverflowHovered
			? WowColor::FromWow(1.0f, 0.82f, 0.24f, 1.0f)
			: WowColor::FromWow(0.48f, 0.48f, 0.48f, 0.72f));
	}
	if (OverflowChatTabIndices.IsEmpty())
	{
		SetFrameVisible(OverflowListHandle, false);
	}
	const bool bOverflowMenuVisible = IsFrameVisible(OverflowListHandle);
	const float MenuWidth = 150.0f * UIScale;
	const float MenuHeight = (22.0f + 18.0f * ChatIndices.Num()) * UIScale;
	// Open outward from the right edge of the overflow button, matching the stock
	// client instead of covering the visible tab row.
	const FVector2D MenuPosition = TabOrigin + FVector2D(
		AvailableWidth + 2.0f * UIScale, -MenuHeight);
	if (UWidget* OverflowList = GetWidgetForHandle(OverflowListHandle))
	{
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(OverflowList->Slot))
		{
			Slot->SetAutoSize(false);
			// The stock list is a child of the overflow button, unlike the root-level
			// tab and menu-row widgets.
			Slot->SetPosition(MenuPosition - (TabOrigin + FVector2D(OverflowButtonX, 1.0f * UIScale)));
			Slot->SetSize(FVector2D(MenuWidth, MenuHeight));
			Slot->SetZOrder(23000);
		}
		if (UTextBlock* Header = GetPrimaryTextWidget(OverflowListHandle))
		{
			Header->SetText(FText::FromString(FString::Printf(TEXT("%d Chat Windows"), ChatIndices.Num())));
			Header->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.78f, 0.72f, 0.58f, 1.0f)));
		}
	}
	if (UImage* Background = GetNamedTexture(TEXT("ChatTabOverflowRootBackground")))
	{
		Background->SetVisibility(bOverflowMenuVisible
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Background->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition(MenuPosition);
			Slot->SetSize(FVector2D(MenuWidth, MenuHeight));
			Slot->SetZOrder(22990);
		}
	}
	const int64 HeaderHandle = FindFrame(TEXT("ChatTabOverflowHeader"));
	SetFrameVisible(HeaderHandle, bOverflowMenuVisible);
	if (UWidget* HeaderWidget = GetWidgetForHandle(HeaderHandle))
	{
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(HeaderWidget->Slot))
		{
			Slot->SetAutoSize(false);
			Slot->SetPosition(MenuPosition);
			Slot->SetSize(FVector2D(MenuWidth, 22.0f * UIScale));
			Slot->SetZOrder(23000);
		}
	}
	if (UTextBlock* HeaderLabel = GetPrimaryTextWidget(HeaderHandle))
	{
		HeaderLabel->SetText(FText::FromString(FString::Printf(TEXT("%d Chat Windows"), ChatIndices.Num())));
		HeaderLabel->SetJustification(ETextJustify::Left);
		HeaderLabel->SetColorAndOpacity(FSlateColor(WowColor::FromWow(0.78f, 0.72f, 0.58f, 1.0f)));
		FSlateFontInfo HeaderFont = HeaderLabel->GetFont();
		HeaderFont.Size = FMath::Max(9, FMath::RoundToInt(9.0f * UIScale));
		HeaderLabel->SetFont(HeaderFont);
	}
	for (int32 Row = 0; Row < ChatIndices.Num(); ++Row)
	{
		const int32 ChatIndex = ChatIndices[Row];
		const int64 RowHandle = FindFrame(*FString::Printf(TEXT("ChatTabOverflowRow%d"), ChatIndex));
		SetFrameVisible(RowHandle, bOverflowMenuVisible);
		if (UWidget* RowWidget = GetWidgetForHandle(RowHandle))
		{
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(RowWidget->Slot))
			{
				Slot->SetAutoSize(false);
				Slot->SetPosition(MenuPosition + FVector2D(0.0f, (22.0f + Row * 18.0f) * UIScale));
				Slot->SetSize(FVector2D(MenuWidth, 18.0f * UIScale));
				Slot->SetZOrder(23010 + Row);
			}
		}
		if (UTextBlock* RowLabel = GetPrimaryTextWidget(RowHandle))
		{
			RowLabel->SetText(FText::FromString(ChatTabLabels.FindRef(ChatIndex)));
			RowLabel->SetJustification(ETextJustify::Left);
			RowLabel->SetColorAndOpacity(FSlateColor(ChatIndex == ActiveChatTabIndex
				? WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f) : WowColor::FromWow(0.9f, 0.76f, 0.25f, 1.0f)));
		}
	}

	auto UpdateTabOpacity = [this, bChatChromeHovered](const TCHAR* Name, bool bActive, bool bHovered)
	{
		if (UWidget* Tab = GetWidgetForHandle(FindFrame(Name)))
		{
			Tab->SetRenderOpacity(1.0f);
		}
		if (UTextBlock* Label = GetPrimaryTextWidget(FindFrame(Name)))
		{
			Label->SetColorAndOpacity(FSlateColor(bActive
				? WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f)
				: WowColor::FromWow(0.58f, 0.58f, 0.56f, 1.0f)));
			Label->SetRenderOpacity(bHovered ? 1.0f
				: bChatChromeHovered ? (bActive ? 0.96f : 0.82f)
				: bActive ? 0.85f : 0.6f);
		}

		auto SetTextureGroupOpacity = [this, Name](const TCHAR* State, float Opacity)
		{
			static const TCHAR* Pieces[] = {TEXT("Left"), TEXT("Middle"), TEXT("Right")};
			for (const TCHAR* Piece : Pieces)
			{
				if (UImage* Texture = GetNamedTexture(FString(Name) + State + Piece))
				{
					Texture->SetRenderOpacity(Opacity);
					// r283: the stock pieces are shown again (they were collapsed behind the black plate).
					const ESlateVisibility Wanted = Opacity > 0.0f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
					if (Texture->GetVisibility() != Wanted) Texture->SetVisibility(Wanted);
				}
			}
		};
		// r289 (Stefan 13.10: "General" sat on a dark box with a stray orange piece of the stock art;
		// "still needs to be like the chat background"): the stock pieces do not fit our flattened
		// anchors, so they stay hidden; the tab is a plate in the chat background's black at the
		// same alpha (the selected tab), a bit fainter for the others, fading with it.
		SetTextureGroupOpacity(TEXT(""), 0.0f);
		SetTextureGroupOpacity(TEXT("Selected"), 0.0f);
		SetTextureGroupOpacity(TEXT("Highlight"), 0.0f);
		if (UImage* Plate = GetNamedTexture(FString(Name) + TEXT("PlateOuter")))
		{
			const float PlateAlpha = !bChatBackgroundEnabled ? 0.0f
				: bActive ? ChatBackgroundAlpha : ChatBackgroundAlpha * (bHovered ? 0.8f : 0.5f);
			Plate->SetRenderOpacity(PlateAlpha);
			const ESlateVisibility Wanted = PlateAlpha > 0.005f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
			if (Plate->GetVisibility() != Wanted) Plate->SetVisibility(Wanted);
			// r301 (Stefan 13.10: the tab's dark plate overlapped the chat background - a darker
			// band): the plate stops where the chat background starts.
			UImage* ChatBackground = GetNamedTexture(FString::Printf(TEXT("ChatFrame%dHoverBackground"),
				ActiveChatTabIndex == 2 ? 2 : 1));
			UCanvasPanelSlot* PlateSlot = Cast<UCanvasPanelSlot>(Plate->Slot);
			UWidget* TabWidget = GetWidgetForHandle(FindFrame(Name));
			if (ChatBackground && PlateSlot && TabWidget)
			{
				const FGeometry& TabGeometry = TabWidget->GetCachedGeometry();
				const FGeometry& BackgroundGeometry = ChatBackground->GetCachedGeometry();
				float Overlap = 0.f;
				if (TabGeometry.GetLocalSize().Y > 0.f && BackgroundGeometry.GetLocalSize().Y > 0.f)
				{
					const FVector2D BackgroundTop = TabGeometry.AbsoluteToLocal(BackgroundGeometry.GetAbsolutePosition());
					Overlap = FMath::Clamp(static_cast<float>(TabGeometry.GetLocalSize().Y - BackgroundTop.Y), 0.f,
						static_cast<float>(TabGeometry.GetLocalSize().Y));
				}
				const FMargin Want(0.f, 0.f, 0.f, Overlap);
				if (!(PlateSlot->GetOffsets() == Want)) PlateSlot->SetOffsets(Want);
			}
		}
	};
	for (const int32 ChatIndex : ChatIndices)
	{
		const FString TabName = FString::Printf(TEXT("ChatFrame%dTab"), ChatIndex);
		const bool bHovered = IsNamedFrameHovered(*TabName) ||
			(bForceChatHover && ChatIndex == ActiveChatTabIndex);
		UpdateTabOpacity(*TabName, ActiveChatTabIndex == ChatIndex, bHovered);
	}

	auto SetDecorationOpacity = [this](const FString& Name, float Opacity)
	{
		if (UImage* Decoration = GetNamedTexture(Name))
		{
			Decoration->SetRenderOpacity(Opacity);
		}
	};
	for (int32 ChatIndex = 1; ChatIndex <= 2; ++ChatIndex)
	{
		const FString Prefix = FString::Printf(TEXT("ChatFrame%d"), ChatIndex);
		const bool bSelected = ActiveChatTabIndex == ChatIndex;
		SetDecorationOpacity(Prefix + TEXT("HoverBackground"), bSelected && bChatBackgroundEnabled
			? ChatBackgroundAlpha : 0.0f);
		SetDecorationOpacity(Prefix + TEXT("HoverBorder"), 0.0f); // no frame/border line around the chat
		// The inherited four-anchor pieces collapsed to 16x16 in our FrameXML
		// parser; keep them hidden behind the correctly stretched owned plate.
		SetDecorationOpacity(Prefix + TEXT("Background"), 0.0f);
		SetDecorationOpacity(Prefix + TEXT("TopLeftTexture"), 0.0f);
		SetDecorationOpacity(Prefix + TEXT("BottomLeftTexture"), 0.0f);
		SetDecorationOpacity(Prefix + TEXT("TopRightTexture"), 0.0f);
		SetDecorationOpacity(Prefix + TEXT("BottomRightTexture"), 0.0f);
		SetDecorationOpacity(Prefix + TEXT("LeftTexture"), 0.0f);
		SetDecorationOpacity(Prefix + TEXT("RightTexture"), 0.0f);
		SetDecorationOpacity(Prefix + TEXT("BottomTexture"), 0.0f);
		SetDecorationOpacity(Prefix + TEXT("TopTexture"), 0.0f);
	}

	auto UpdateControlOpacity = [this, bChatChromeHovered](const TCHAR* Name, bool bHovered)
	{
		if (UWidget* Control = GetWidgetForHandle(FindFrame(Name)))
		{
			Control->SetRenderOpacity(bHovered ? 1.0f : (bChatChromeHovered ? 0.86f : 0.58f));
		}
		// r292 (Stefan 13.10: the chat buttons do not light up on mouseover): the buttons'
		// HighlightTexture (Interface\Buttons\UI-Common-MouseHilight) over the button while hovered.
		// r314 (Stefan 14.10: "I don't want the square over the button"): the highlight texture was
		// drawn as a plain blended square (the client adds it); the button lights up by opacity only.
		const FString HighlightName = FString(Name) + TEXT("NativeHilight");
		UImage* Highlight = GetNamedTexture(HighlightName);
		if (false && !Highlight && bHovered)
			if (UCanvasPanel* ButtonCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(FindFrame(Name))))
			{
				Highlight = NewObject<UImage>(ButtonCanvas);
				if (UCanvasPanelSlot* HighlightSlot = ButtonCanvas->AddChildToCanvas(Highlight))
				{
					HighlightSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
					HighlightSlot->SetOffsets(FMargin(0.0f));
					HighlightSlot->SetZOrder(500);
				}
				FSlateBrush HighlightBrush;
				HighlightBrush.SetResourceObject(LoadUITexture(TEXT("Interface\\Buttons\\UI-Common-MouseHilight")));
				Highlight->SetBrush(HighlightBrush);
				Highlight->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.55f));
				NamedTextureWidgets.Add(HighlightName, Highlight);
			}
		if (Highlight)
		{
			const ESlateVisibility Wanted = ESlateVisibility::Collapsed;
			if (Highlight->GetVisibility() != Wanted) Highlight->SetVisibility(Wanted);
		}
	};
	UpdateControlOpacity(TEXT("FriendsMicroButton"), bFriendsHovered);
	UpdateControlOpacity(TEXT("ChatFrameMenuButton"), bMenuHovered);
	UpdateControlOpacity(TEXT("ChatFrame1ButtonFrameUpButton"), bUpHovered);
	UpdateControlOpacity(TEXT("ChatFrame1ButtonFrameDownButton"), bDownHovered);
	UpdateControlOpacity(TEXT("ChatFrame1ButtonFrameBottomButton"), bBottomHovered);
	// r383/r384 (Stefan 16.10: with a watched faction its text ran over the bottom of the upper action
	// bars): like UIParent_ManageFramePositions' "reputation" offset, the two upper bars sit 9 higher
	// while the reputation bar shows above the XP bar. r384 (Stefan: "it bounces up and down now"):
	// moving their slots fought whoever else placed them, so the lift is now a render offset, and
	// none when the bar is already 9 higher (placed by the stock code) or moved by hand / an addon.
	{
		static double NextReputationLift = 0.0;
		const double LiftNow = FPlatformTime::Seconds();
		if (LiftNow >= NextReputationLift)
		{
			NextReputationLift = LiftNow + .25;
			const UWidget* LiftMainBar = GetWidgetForHandle(FindFrame(TEXT("MainMenuBar")));
			const UCanvasPanelSlot* LiftMainSlot = LiftMainBar ? Cast<UCanvasPanelSlot>(LiftMainBar->Slot) : nullptr;
			if (LiftMainSlot && UIScale > 0.f)
			{
				const int64 RepHandle = FindFrame(TEXT("ReputationWatchBar"));
				const int64 ExpHandle = FindFrame(TEXT("MainMenuExpBar"));
				const UWidget* RepWidget = GetWidgetForHandle(RepHandle);
				const UWidget* ExpWidget = GetWidgetForHandle(ExpHandle);
				const bool bLift = RepWidget && ExpWidget && IsFrameEffectivelyShown(RepHandle) && IsFrameEffectivelyShown(ExpHandle)
					&& RepWidget->IsVisible() && ExpWidget->IsVisible() && RepWidget->GetRenderOpacity() > .05f;
				const float BaseY = static_cast<float>(LiftMainSlot->GetPosition().Y) - 45.0f * UIScale;
				int32 LiftBarIndex = 0;
				for (const TCHAR* LiftBarName : {TEXT("MultiBarBottomLeft"), TEXT("MultiBarBottomRight")})
				{
					const int32 ReleasedBit = 1 << LiftBarIndex++;
					UWidget* LiftBar = GetWidgetForHandle(FindFrame(LiftBarName));
					const UCanvasPanelSlot* LiftSlot = LiftBar ? Cast<UCanvasPanelSlot>(LiftBar->Slot) : nullptr;
					if (!LiftSlot) continue;
					const bool bAtBase = FMath::Abs(static_cast<float>(LiftSlot->GetPosition().Y) - BaseY) < 1.f;
					const float Want = bLift && bAtBase && !(ScriptReleasedActionBars & ReleasedBit) ? -9.0f * UIScale : 0.f;
					if (FMath::Abs(static_cast<float>(LiftBar->GetRenderTransform().Translation.Y) - Want) >= .5f)
						LiftBar->SetRenderTranslation(FVector2D(0.f, Want));
				}
			}
		}
	}
	// XP bar (Stefan 10.10: the action bar shows no XP bar at all). Every 10 s: log its state once
	// per change, and if Lua shows it but the widget is collapsed or transparent, restore it.
	{
		static double NextXPBarCheck = 0.0;
		static FString LastXPBarState;
		static FString LastXPBarHiddenBy;
		const double XPBarNow = FPlatformTime::Seconds();
		if (XPBarNow >= NextXPBarCheck)
		{
			NextXPBarCheck = XPBarNow + 10.0;
			const int64 XPBarCheckHandle = FindFrame(TEXT("MainMenuExpBar"));
			if (UWidget* XPBarCheckWidget = GetWidgetForHandle(XPBarCheckHandle))
			{
				const bool bXPBarShown = IsFrameEffectivelyShown(XPBarCheckHandle);
				const UCanvasPanelSlot* XPBarCheckSlot = Cast<UCanvasPanelSlot>(XPBarCheckWidget->Slot);
				const FFrameRect XPBarCheckRect = GetFrameRect(TEXT("MainMenuExpBar"));
				const FString XPBarState = FString::Printf(
					TEXT("shown=%d vis=%d opacity=%.2f slot=(%.0f,%.0f %.0fx%.0f) z=%d rect=(%.0f,%.0f %.0fx%.0f) desired=(%.0fx%.0f)"),
					bXPBarShown ? 1 : 0, int32(XPBarCheckWidget->GetVisibility()), XPBarCheckWidget->GetRenderOpacity(),
					XPBarCheckSlot ? XPBarCheckSlot->GetPosition().X : -1.f, XPBarCheckSlot ? XPBarCheckSlot->GetPosition().Y : -1.f,
					XPBarCheckSlot ? XPBarCheckSlot->GetSize().X : -1.f, XPBarCheckSlot ? XPBarCheckSlot->GetSize().Y : -1.f,
					XPBarCheckSlot ? XPBarCheckSlot->GetZOrder() : -1,
					XPBarCheckRect.X, XPBarCheckRect.Y, XPBarCheckRect.W, XPBarCheckRect.H,
					XPBarCheckWidget->GetDesiredSize().X, XPBarCheckWidget->GetDesiredSize().Y);
				if (XPBarState != LastXPBarState)
				{
					LastXPBarState = XPBarState;
					UE_LOG(LogWowFrame, Log, TEXT("XP bar: %s"), *XPBarState);
				}
				if (bXPBarShown && (!XPBarCheckWidget->IsVisible() || XPBarCheckWidget->GetRenderOpacity() < 0.05f))
				{
					UE_LOG(LogWowFrame, Warning, TEXT("XP bar: shown in Lua but hidden as a widget - restoring it"));
					XPBarCheckWidget->SetRenderOpacity(1.0f);
					XPBarCheckWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				}
				// The 20:53 log (level 17): "shown=0" - the bar itself or a parent was hidden although
				// ReputationWatchBar_Update shows it below the level cap. Name the hidden one, and when
				// it is the bar, show it again the way that function does.
				if (!bXPBarShown && EventSystem)
				{
					FString HiddenName;
					for (const FFrameEntry* Walk = Frames.Find(XPBarCheckHandle); Walk; Walk = Frames.Find(Walk->ParentHandle))
						if (Walk->Def.bHidden) { HiddenName = Walk->Def.Name; break; }
					if (XPBarState + HiddenName != LastXPBarHiddenBy)
					{
						LastXPBarHiddenBy = XPBarState + HiddenName;
						UE_LOG(LogWowFrame, Warning, TEXT("XP bar: hidden by '%s'"), *HiddenName);
					}
					if (HiddenName == TEXT("MainMenuExpBar"))
						EventSystem->ExecuteLuaString(TEXT(R"LUA(
							-- r303 (Stefan 13.10: still no XP bar at level 1 with Bartender off): MAX_PLAYER_LEVEL
							-- may not be set yet (UIParent sets it from the account's expansion), which skipped this.
							local level = UnitLevel and UnitLevel("player") or 0
							local maxLevel = MAX_PLAYER_LEVEL
								or (MAX_PLAYER_LEVEL_TABLE and GetAccountExpansionLevel and MAX_PLAYER_LEVEL_TABLE[GetAccountExpansionLevel()])
								or 80
							local why
							if not MainMenuExpBar then why = "no MainMenuExpBar"
							elseif not (MainMenuBar and MainMenuBar:IsShown()) then why = "MainMenuBar hidden (an action bar addon)"
							elseif level <= 0 or level >= maxLevel then why = "level " .. level .. " / " .. maxLevel
							elseif IsXPUserDisabled and IsXPUserDisabled() then why = "XP disabled"
							elseif UnitHasVehicleUI and UnitHasVehicleUI("player") then why = "vehicle"
							end
							if why then
								if __WowXPBarWhy ~= why and __WowLog then __WowLog("XP bar not restored: " .. why) end
								__WowXPBarWhy = why
							else
								__WowXPBarWhy = nil
								MainMenuExpBar:Show()
								MainMenuExpBar.pauseUpdates = nil
								if MainMenuExpBar_Update then MainMenuExpBar_Update() end
							end
						)LUA"), TEXT("=XPBarRestore"));
				}
			}
		}
	}
	// Chat tab name accepted in the naming dialog (Rename Window).
	if (ChatTabRenameIndex > 0 && EventSystem)
	{
		const FString AcceptedTabName = EventSystem->GetGlobalString(TEXT("__WowChatTabRename"));
		if (!AcceptedTabName.IsEmpty())
		{
			EventSystem->ExecuteLuaString(TEXT("__WowChatTabRename = nil"), TEXT("=ChatTabRename"));
			const int32 MenuTabIndex = ChatContextTabIndex;
			ChatContextTabIndex = ChatTabRenameIndex;
			bChatTabRenamePending = true;
			CommitPendingChatTabRename(AcceptedTabName);
			ChatContextTabIndex = MenuTabIndex;
			ChatTabRenameIndex = 0;
		}
	}
	// The hide/show chat arrow under the bubble fades with the other chat controls (Stefan 10.10:
	// the bubble and social button faded, the hide button stayed fully opaque). While the chat is
	// hidden it is the only way back, so it stays at full opacity then.
	if (UWidget* ChatToggleWidget = GetWidgetForHandle(FindFrame(TEXT("WowChatVisibilityButton"))))
	{
		const bool bChatToggleHovered = ChatToggleWidget->IsHovered();
		ChatToggleWidget->SetRenderOpacity(bChatWindowHidden || bChatToggleHovered ? 1.0f : (bChatChromeHovered ? 0.86f : 0.58f));
	}

	// Poll Slider widgets for value changes
	for (auto& Pair : SliderWidgets)
	{
		if (!Pair.Value.IsValid()) continue;
		float CurrentValue = Pair.Value->GetValue();
		float* LastValue = SliderLastValue.Find(Pair.Key);
		if (!LastValue)
		{
			SliderLastValue.Add(Pair.Key, CurrentValue);
		}
		else if (!FMath::IsNearlyEqual(*LastValue, CurrentValue))
		{
			*LastValue = CurrentValue;
			if (EventSystem)
			{
				EventSystem->RunFrameScript(Pair.Key, TEXT("OnValueChanged"),
					{FString::SanitizeFloat(CurrentValue)});
			}
		}
	}

	// Stock chat keeps recent lines readable, then fades them individually.
	// New messages occupy the bottom row and push older lines upward.
	const double NowSeconds = FPlatformTime::Seconds();
	for (auto& Pair : ScrollingMessageWidgets)
	{
		TArray<FScrollingMessageEntry>& Lines = Pair.Value;
		bool bRemovedInvalidLine = false;
		const int32* OffsetPtr = ScrollingMessageOffsets.Find(Pair.Key);
		const bool bBrowsingHistory = OffsetPtr && *OffsetPtr > 0;
		for (int32 Index = Lines.Num() - 1; Index >= 0; --Index)
		{
			FScrollingMessageEntry& Line = Lines[Index];
			if (!Line.Widget.IsValid())
			{
				Lines.RemoveAt(Index);
				bRemovedInvalidLine = true;
				continue;
			}

            const bool bReveal=bChatChromeHovered||bBrowsingHistory;
            if(bReveal)ChatLastActivity.Add(Pair.Key,NowSeconds);
            const float Opacity=WowChatFade::Opacity(NowSeconds,Line.AddedAtSeconds,ChatLastActivity.FindRef(Pair.Key),bReveal);
			Line.Widget->SetRenderOpacity(FMath::Clamp(Opacity, 0.0f, 1.0f));
		}
		if (bRemovedInvalidLine)
		{
			RefreshScrollingMessageLayout(Pair.Key);
		}
	}
    UpdateUserFrameLayouts();
    // The button column's stock art (ChatFrameButtonFrame Background + border pieces) is a white
    // texture FrameXML tints/fades from Lua; we draw our own plates, so keep it invisible always
    // (it was only cleared in one layout path and could show as a white box under the icons).
    for (int32 ChatIndex = 1; ChatIndex <= 10; ++ChatIndex)
    {
        static const TCHAR* Suffixes[] = {
            TEXT("Background"), TEXT("TopLeftTexture"), TEXT("BottomLeftTexture"), TEXT("TopRightTexture"),
            TEXT("BottomRightTexture"), TEXT("LeftTexture"), TEXT("RightTexture"), TEXT("BottomTexture"), TEXT("TopTexture") };
        for (const TCHAR* Suffix : Suffixes)
            if (UImage* Decoration = GetNamedTexture(FString::Printf(TEXT("ChatFrame%dButtonFrame%s"), ChatIndex, Suffix)))
                if (Decoration->GetRenderOpacity() != 0.0f) Decoration->SetRenderOpacity(0.0f);
    }
    ApplyChatWindowHidden();
}

bool FWowFrameManager::DispatchEditBoxEnterPressed()
{
	for (const auto& Pair : EditBoxWidgets)
	{
		if (!Pair.Value.IsValid()) continue;
		if (Pair.Value->HasKeyboardFocus() || Pair.Value->HasAnyUserFocus() || Pair.Value->HasFocusedDescendants())
		{
			if (EventSystem)
			{
				EventSystem->RunFrameScript(Pair.Key, TEXT("OnEnterPressed"));
			}
			return true;
		}
	}
	return false;
}

bool FWowFrameManager::DispatchEditBoxEscapePressed()
{
	for (const auto& Pair : EditBoxWidgets)
	{
		if (!Pair.Value.IsValid()) continue;
		if (Pair.Value->HasKeyboardFocus() || Pair.Value->HasAnyUserFocus() || Pair.Value->HasFocusedDescendants())
		{
			if (EventSystem)
			{
				EventSystem->RunFrameScript(Pair.Key, TEXT("OnEscapePressed"));
			}
			return true;
		}
	}
	return false;
}

int64 FWowFrameManager::GetNextFrameHandle(int64 After) const
{
	// Handles are allocated sequentially, so walk forward instead of scanning the map.
	for (int64 Handle = FMath::Max<int64>(After + 1, 0); Handle < NextHandle; ++Handle)
	{
		const FFrameEntry* Entry = Frames.Find(Handle);
		if (Entry && Entry->Widget.IsValid()) return Handle;
	}
	return -1;
}

void FWowFrameManager::SetButtonStateRegion(int64 Handle, EWowButtonStateRegion State, UWidget* Region)
{
	if (Handle < 0 || State == EWowButtonStateRegion::Count) return;
	auto& Map = ButtonStateRegions[static_cast<int32>(State)];
	if (!Region)
	{
		Map.Remove(Handle);
		return;
	}
	Map.Add(Handle, Region);
	bool bActive = false;
	if (State == EWowButtonStateRegion::Highlight) bActive = Handle == HoverFrameHandle;
	SetButtonStateRegionShown(Handle, State, bActive);
}

void FWowFrameManager::SetButtonStateRegionShown(int64 Handle, EWowButtonStateRegion State, bool bShown)
{
	if (Handle < 0 || State == EWowButtonStateRegion::Count) return;
	if (const TWeakObjectPtr<UWidget>* Region = ButtonStateRegions[static_cast<int32>(State)].Find(Handle))
	{
		if (Region->IsValid())
		{
			(*Region)->SetVisibility(bShown ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
}

void FWowFrameManager::RegisterHighlightLayerRegion(int64 Handle, UWidget* Region, bool bScriptShown)
{
	if (!Region || Handle < 0) return;
	UnregisterHighlightLayerRegion(Region);
	const TWeakObjectPtr<UWidget> Key(Region);
	FHighlightLayerRegion Entry;
	Entry.Handle = Handle;
	Entry.bScriptShown = bScriptShown;
	HighlightLayerRegions.Add(Key, Entry);
	HighlightLayerRegionsByFrame.Add(Handle, Key);
	ApplyHighlightLayer(Handle, Handle == HoverFrameHandle);
}

void FWowFrameManager::UnregisterHighlightLayerRegion(UWidget* Region)
{
	if (!Region) return;
	const TWeakObjectPtr<UWidget> Key(Region);
	if (const FHighlightLayerRegion* Entry = HighlightLayerRegions.Find(Key))
	{
		HighlightLayerRegionsByFrame.RemoveSingle(Entry->Handle, Key);
		HighlightLayerRegions.Remove(Key);
	}
}

bool FWowFrameManager::IsHighlightLayerRegion(const UWidget* Region) const
{
	return Region && HighlightLayerRegions.Contains(TWeakObjectPtr<UWidget>(const_cast<UWidget*>(Region)));
}

void FWowFrameManager::SetHighlightLayerRegionScriptShown(UWidget* Region, bool bShown)
{
	if (FHighlightLayerRegion* Entry = HighlightLayerRegions.Find(TWeakObjectPtr<UWidget>(Region)))
	{
		Entry->bScriptShown = bShown;
		const bool bHovered = Entry->Handle == HoverFrameHandle && !bMouseInteractionSuppressed;
		Region->SetVisibility(bShown && bHovered ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void FWowFrameManager::NoteRegionMoved(const FString& RegionName, UWidget* Region, int64 OwnerHandle)
{
	if (RegionName.IsEmpty() || !Region || OwnerHandle < 0 || UIScale <= 0.f) return;
	const FFrameEntry* Owner = Frames.Find(OwnerHandle);
	UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Region->Slot);
	const FFrameRect* OwnerRect = Owner ? FrameRects.Find(Owner->Def.Name) : nullptr;
	if (!OwnerRect || !Slot) return;
	const float S = UIScale;
	const FAnchors A = Slot->GetAnchors();
	const FMargin O = Slot->GetOffsets();
	const FVector2D Align = Slot->GetAlignment();
	const FVector2D Desired = Region->GetDesiredSize();
	const float ParentPx[2] = { OwnerRect->W * S, OwnerRect->H * S };
	float Pos[2], Size[2];
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const float Min = static_cast<float>(Axis == 0 ? A.Minimum.X : A.Minimum.Y);
		const float Max = static_cast<float>(Axis == 0 ? A.Maximum.X : A.Maximum.Y);
		const float Lead = Axis == 0 ? O.Left : O.Top;
		const float Trail = Axis == 0 ? O.Right : O.Bottom;
		if (Min == Max)
		{
			Size[Axis] = Slot->GetAutoSize() ? static_cast<float>(Desired[Axis]) : Trail;
			Pos[Axis] = Min * ParentPx[Axis] + Lead - static_cast<float>(Align[Axis]) * Size[Axis];
		}
		else
		{
			Pos[Axis] = Min * ParentPx[Axis] + Lead;
			Size[Axis] = Max * ParentPx[Axis] - Trail - Pos[Axis];
		}
	}
	FrameRects.Add(RegionName, FFrameRect(OwnerRect->X + Pos[0] / S, OwnerRect->Y + Pos[1] / S, Size[0] / S, Size[1] / S));
	NoteRegionOwnerAt(RegionName, Owner->Def.Name, OwnerRect->X, OwnerRect->Y);

	// r350 (Stefan 15.10, /bt4: tab art did not fit its text, the "Key Bindings" button was a small dark
	// box with its caps far apart): an XML texture anchored to another region of the same frame was
	// placed once. PanelTemplates_TabResize widens a tab's Middle from Lua, and the Right cap,
	// anchored to Middle's RIGHT in OptionsFrameTabButtonTemplate, stayed where it was. Like the
	// client, the owner's layer regions anchored to this region follow it (and those anchored to them).
	if (UCanvasPanel* OwnerCanvas = Cast<UCanvasPanel>(Owner->Widget.Get()))
	{
		const FFrameRect OwnerNow = *OwnerRect;
		auto AnchorsOn = [&](const TArray<FWowAnchor>& Anchors, const FString& Name)
		{
			for (const FWowAnchor& Each : Anchors)
			{
				FString Rel = Each.RelativeTo;
				if (Rel.Contains(TEXT("$parent"))) Rel.ReplaceInline(TEXT("$parent"), *Owner->Def.Name);
				if (Rel.Equals(Name, ESearchCase::IgnoreCase)) return true;
			}
			return false;
		};
		auto NoteLaidOut = [&](const FString& Name, UWidget* Widget)
		{
			const UCanvasPanelSlot* LaidOut = Cast<UCanvasPanelSlot>(Widget->Slot);
			if (!LaidOut) return;
			const FVector2D At = LaidOut->GetPosition() / S;
			const FVector2D Sz = LaidOut->GetAutoSize() ? Widget->GetDesiredSize() / S : LaidOut->GetSize() / S;
			FrameRects.Add(Name, FFrameRect(OwnerNow.X + At.X, OwnerNow.Y + At.Y, Sz.X, Sz.Y));
			NoteRegionOwnerAt(Name, Owner->Def.Name, OwnerNow.X, OwnerNow.Y);
		};
		TArray<FString> Moved = { RegionName };
		TSet<FString> Done = { RegionName };
		for (int32 Step = 0; Step < Moved.Num() && Step < 48; ++Step)
		{
			const FString MovedName = Moved[Step];
			for (const FWowLayer& Layer : Owner->Def.Layers)
			{
				for (const FWowTextureElement& Texture : Layer.Textures)
				{
					if (Texture.Name.IsEmpty() || Done.Contains(Texture.Name) || !AnchorsOn(Texture.Anchors, MovedName)) continue;
					const TWeakObjectPtr<UImage>* ImagePtr = NamedTextureWidgets.Find(Texture.Name);
					UImage* Image = ImagePtr && ImagePtr->IsValid() ? ImagePtr->Get() : nullptr;
					const UCanvasPanelSlot* ImageSlot = Image ? Cast<UCanvasPanelSlot>(Image->Slot) : nullptr;
					if (!ImageSlot) continue;
					// Keep a size Lua gave it (SetWidth) for the axes the anchors do not stretch.
					const FVector2D Current = ImageSlot->GetSize() / S;
					const float RegionW = Current.X > 0.f ? static_cast<float>(Current.X) : Texture.Width;
					const float RegionH = Current.Y > 0.f ? static_cast<float>(Current.Y) : Texture.Height;
					ApplyElementAnchors(Image, OwnerCanvas, Texture.Anchors, RegionW, RegionH, Texture.bSetAllPoints,
						OwnerNow.W, OwnerNow.H, OwnerNow.X, OwnerNow.Y);
					NoteLaidOut(Texture.Name, Image);
					Done.Add(Texture.Name);
					Moved.Add(Texture.Name);
				}
				for (const FWowFontStringElement& Text : Layer.FontStrings)
				{
					if (Text.Name.IsEmpty() || Done.Contains(Text.Name) || !AnchorsOn(Text.Anchors, MovedName)) continue;
					const TWeakObjectPtr<UTextBlock>* TextPtr = NamedFontStringWidgets.Find(Text.Name);
					UTextBlock* TextWidget = TextPtr && TextPtr->IsValid() ? TextPtr->Get() : nullptr;
					if (!TextWidget || !Cast<UCanvasPanelSlot>(TextWidget->Slot)) continue;
					ApplyElementAnchors(TextWidget, OwnerCanvas, Text.Anchors, Text.Width, Text.Height, false,
						OwnerNow.W, OwnerNow.H, OwnerNow.X, OwnerNow.Y);
					NoteLaidOut(Text.Name, TextWidget);
					Done.Add(Text.Name);
					Moved.Add(Text.Name);
				}
			}
		}
	}
	for (TPair<int64, FFrameEntry>& Pair : Frames)
	{
		FFrameEntry& Entry = Pair.Value;
		if (!Entry.Widget.IsValid() || Entry.Def.Anchors.Num() == 0) continue;
		bool bAnchored = false;
		for (const FWowAnchor& Anchor : Entry.Def.Anchors)
		{
			FString Rel = Anchor.RelativeTo;
			if (Rel.Contains(TEXT("$parent"))) Rel.ReplaceInline(TEXT("$parent"), *Entry.Def.Parent);
			if (Rel.Equals(RegionName, ESearchCase::IgnoreCase)) { bAnchored = true; break; }
		}
		if (!bAnchored) continue;
		FrameRects.Remove(Entry.Def.Name);
		ApplyAnchors(Entry.Widget.Get(), Entry.Def);
		QueueDependentLayout(Pair.Key);
	}
}

void FWowFrameManager::NoteScriptReleasedFrame(int64 Handle)
{
	if (Handle < 0) return;
	const FString Name = GetFrameName(Handle);
	static const TCHAR* const ButtonPrefixes[] = {
		TEXT("MultiBarBottomLeftButton"), TEXT("MultiBarBottomRightButton"), TEXT("MultiBarRightButton"), TEXT("MultiBarLeftButton") };
	for (int32 I = 0; I < UE_ARRAY_COUNT(ButtonPrefixes); ++I)
	{
		const int32 PrefixLen = FCString::Strlen(ButtonPrefixes[I]);
		if (Name.StartsWith(ButtonPrefixes[I], ESearchCase::CaseSensitive) && Name.Len() > PrefixLen && Name.Mid(PrefixLen).IsNumeric())
		{
			if (!(ScriptReleasedActionBars & (1 << I)))
			{
				UE_LOG(LogWowFrame, Log, TEXT("Action bar %s taken over by an addon (UnregisterAllEvents on %s): no longer shown or laid out natively"),
					ButtonPrefixes[I], *Name);
			}
			ScriptReleasedActionBars |= 1 << I;
		}
	}
}

void FWowFrameManager::ApplyHighlightLayer(int64 Handle, bool bHovered)
{
	if (Handle < 0) return;
	TArray<TWeakObjectPtr<UWidget>> Regions;
	HighlightLayerRegionsByFrame.MultiFind(Handle, Regions);
	for (const TWeakObjectPtr<UWidget>& Weak : Regions)
	{
		UWidget* Region = Weak.Get();
		if (!Region)
		{
			HighlightLayerRegionsByFrame.RemoveSingle(Handle, Weak);
			HighlightLayerRegions.Remove(Weak);
			continue;
		}
		const FHighlightLayerRegion* Entry = HighlightLayerRegions.Find(Weak);
		const bool bShow = bHovered && (!Entry || Entry->bScriptShown);
		Region->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void FWowFrameManager::UpdateMouseHover(float ScreenX, float ScreenY)
{
	int64 NewHover = bMouseInteractionSuppressed ? -1 : HitTestFrames(ScreenX, ScreenY);

	if (NewHover != HoverFrameHandle)
	{
		// Dispatch OnLeave for the old frame
		SetButtonStateRegionShown(HoverFrameHandle, EWowButtonStateRegion::Highlight, false);
		SetButtonStateRegionShown(HoverFrameHandle, EWowButtonStateRegion::Pushed, false);
		ApplyHighlightLayer(HoverFrameHandle, false);
		if (HoverFrameHandle >= 0 && EventSystem)
		{
			EventSystem->RunFrameScript(HoverFrameHandle, TEXT("OnLeave"), {TEXT("0")});
		}
		HideNativeTooltip();

		HoverFrameHandle = NewHover;
		SetButtonStateRegionShown(HoverFrameHandle, EWowButtonStateRegion::Highlight, true);
		ApplyHighlightLayer(HoverFrameHandle, true);

		// Dispatch OnEnter for the new frame
		if (HoverFrameHandle >= 0 && EventSystem)
		{
			EventSystem->RunFrameScript(HoverFrameHandle, TEXT("OnEnter"), {TEXT("0")});
		}
		if (HoverFrameHandle >= 0)
		{
			ShowNativeTooltipForFrame(HoverFrameHandle);
		}
	}
}

void FWowFrameManager::SetMouseInteractionSuppressed(bool bSuppressed)
{
	if (bMouseInteractionSuppressed == bSuppressed)
	{
		return;
	}

    bMouseInteractionSuppressed = bSuppressed;
    if (RootCanvas.IsValid())
    {
        if (bSuppressed)
        {
            MouseLookPreviousVisibility = RootCanvas->GetVisibility();
            if (RootCanvas->IsVisible()) RootCanvas->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        else if (RootCanvas->GetVisibility() == ESlateVisibility::HitTestInvisible)
        {
            RootCanvas->SetVisibility(MouseLookPreviousVisibility);
        }
    }
	if (bSuppressed)
	{
		SetButtonStateRegionShown(HoverFrameHandle, EWowButtonStateRegion::Highlight, false);
		ApplyHighlightLayer(HoverFrameHandle, false);
		if (HoverFrameHandle >= 0 && EventSystem)
		{
			EventSystem->RunFrameScript(HoverFrameHandle, TEXT("OnLeave"), {TEXT("0")});
		}
		HoverFrameHandle = -1;
		HideNativeTooltip();
	}
}

void FWowFrameManager::DispatchMouseDown(int64 Handle, const FString& Button)
{
	if (!EventSystem || Handle < 0) return;
	if (const FFrameEntry* Entry = Frames.Find(Handle))
	{
		if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) && bNativeChatMenuOpen &&
			Entry->Def.Name != TEXT("ChatFrameMenuButton") && !Entry->Def.Name.StartsWith(TEXT("ChatNativeMenuRow")))
		{
			CloseNativeChatMenu();
		}
		if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) &&
			bMinimapTrackingMenuOpen &&
			Entry->Def.Name != TEXT("MiniMapTrackingButton") &&
			Entry->Def.Name != TEXT("MiniMapTracking") &&
			!Entry->Def.Name.StartsWith(TEXT("MinimapNativeTrackingRow")))
		{
			bMinimapTrackingMenuOpen = false;
			RefreshNativeMinimapControls();
		}
		if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) &&
			(bCharacterRightStatMenuOpen || bCharacterLeftStatMenuOpen) &&
			!Entry->Def.Name.StartsWith(TEXT("CharacterNativeRightStat")) &&
			!Entry->Def.Name.StartsWith(TEXT("CharacterNativeLeftStat")) &&
			!Entry->Def.Name.StartsWith(TEXT("PlayerStatFrameRightDropDown")) &&
			!Entry->Def.Name.StartsWith(TEXT("PlayerStatFrameLeftDropDown")))
		{
			bCharacterRightStatMenuOpen = false;
			bCharacterLeftStatMenuOpen = false;
			RefreshNativeCharacterStatControls();
		}
		if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) &&
			bCharacterTitleMenuOpen &&
			Entry->Def.Name != TEXT("PlayerTitleFrame") &&
			!Entry->Def.Name.StartsWith(TEXT("PlayerTitleFrameButton")) &&
			Entry->Def.Name != TEXT("CharacterNativeTitleNoneRow"))
		{
			bCharacterTitleMenuOpen = false;
			RefreshNativeCharacterTitleControl();
		}
		if (Button.Equals(TEXT("RightButton"), ESearchCase::IgnoreCase) &&
			Entry->Def.Name.StartsWith(TEXT("ChatFrame")) && Entry->Def.Name.EndsWith(TEXT("Tab")))
		{
			const FString IndexText = Entry->Def.Name.Mid(9, Entry->Def.Name.Len() - 12);
			const int32 RequestedTabIndex = FMath::Clamp(FCString::Atoi(*IndexText), 1, 10);
			const bool bToggleClosed = bChatTabContextMenuOpen && ChatContextTabIndex == RequestedTabIndex;
			ChatContextTabIndex = RequestedTabIndex;
			bChatTabContextMenuOpen = !bToggleClosed;
			SetFrameVisible(FindFrame(TEXT("GeneralDockManagerOverflowButtonList")), false);
		}
		else if (Button.Equals(TEXT("RightButton"), ESearchCase::IgnoreCase) &&
			Entry->Def.Name.StartsWith(TEXT("SocialNativeFriendRow")))
		{
			SocialContextFriendRow = FMath::Clamp(
				FCString::Atoi(*Entry->Def.Name.Mid(21)), 1, 8);
			TArray<FString> ContextEntries = bSocialIgnoreSelected
				? SocialIgnoreEntries : SocialFriendEntries;
			if (!bSocialIgnoreSelected && !RecentWhisperSender.IsEmpty()
				&& !ContextEntries.Contains(RecentWhisperSender))
			{
				ContextEntries.Insert(RecentWhisperSender, 0);
			}
			if (ContextEntries.IsValidIndex(SocialContextFriendRow - 1))
			{
				SocialContextFriendName = ContextEntries[SocialContextFriendRow - 1];
				int32 DetailSeparator = INDEX_NONE;
				if (SocialContextFriendName.FindChar(TEXT('•'), DetailSeparator))
				{
					SocialContextFriendName = SocialContextFriendName.Left(DetailSeparator).TrimEnd();
				}
				bSocialFriendContextMenuOpen = true;
			}
		}
		else if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) &&
			Entry->Def.Name == TEXT("ChatFrameNativeResizeButton") && bChatFrameUnlocked &&
			FSlateApplication::IsInitialized())
		{
			if (UWidget* ChatFrame = GetWidgetForHandle(FindFrame(TEXT("ChatFrame1"))))
			{
				if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ChatFrame->Slot))
				{
					bChatResizeActive = true;
					ChatResizeStartCursor = FSlateApplication::Get().GetCursorPos();
					ChatResizeStartSize = Slot->GetSize();
				}
			}
		}
		else if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) &&
			!Entry->Def.Name.StartsWith(TEXT("ChatTabContextRow")))
		{
			bChatTabContextMenuOpen = false;
			if (Entry->Def.Name != TEXT("CombatLogFilterDropDownButton") &&
				Entry->Def.Name != TEXT("CombatLogFilterKills"))
			{
				bCombatLogFilterMenuOpen = false;
			}
			if (!Entry->Def.Name.StartsWith(TEXT("SocialNativeFriendContextRow")))
			{
				bSocialFriendContextMenuOpen = false;
			}
		}
	}
	SetButtonStateRegionShown(Handle, EWowButtonStateRegion::Pushed, true);
	if (const FFrameEntry* DownEntry = Frames.Find(Handle); DownEntry && DownEntry->Def.Name.EndsWith(TEXT("MicroButton")))
		UE_LOG(LogWowFrame, Log, TEXT("Micro button press: %s"), *DownEntry->Def.Name); // r299 diagnostic
	EventSystem->RunFrameScript(Handle, TEXT("OnMouseDown"), {Button});
}

void FWowFrameManager::DispatchMouseUp(int64 Handle, const FString& Button)
{
	if (!EventSystem || Handle < 0) return;
	if (Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase) && bChatResizeActive)
	{
		bChatResizeActive = false;
	}
	SetButtonStateRegionShown(Handle, EWowButtonStateRegion::Pushed, false);
	// r299 diagnostic: micro buttons (OnMouseDown/OnMouseUp scripts) - did the release reach them.
	if (const FFrameEntry* UpEntry = Frames.Find(Handle); UpEntry && UpEntry->Def.Name.EndsWith(TEXT("MicroButton")))
		UE_LOG(LogWowFrame, Log, TEXT("Micro button release: %s hovered=%lld mouseOver=%d suppressed=%d"), *UpEntry->Def.Name,
			HoverFrameHandle, IsMouseOverFrame(Handle, 0.f, 0.f, 0.f, 0.f) ? 1 : 0, bMouseInteractionSuppressed ? 1 : 0);
	EventSystem->RunFrameScript(Handle, TEXT("OnMouseUp"), {Button});
}

void FWowFrameManager::DispatchClick(int64 Handle, const FString& Button, bool bMouseDown)
{
	if (!EventSystem || Handle < 0) return;

	const FFrameEntry* Entry = Frames.Find(Handle);
	bool bHandledNativeChatControl = false;
	if (Entry && !bMouseDown && Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase))
	{
		const FString& FrameName = Entry->Def.Name;
		if (FrameName == TEXT("MainMenuBarBackpackButton") ||
            FrameName == TEXT("CharacterBag0Slot") || FrameName == TEXT("CharacterBag1Slot") ||
            FrameName == TEXT("CharacterBag2Slot") || FrameName == TEXT("CharacterBag3Slot"))
        {
            TogglePlayerBag(FrameName == TEXT("MainMenuBarBackpackButton") ? 0 : FCString::Atoi(*FrameName.Mid(12,1))+1);
            bHandledNativeChatControl = true;
        }
        else if (FrameName == TEXT("ActionBarUpButton") || FrameName == TEXT("ActionBarDownButton"))
        {
            // ActionBar_PageUp/PageDown: the arrows step through pages 1-6 and wrap
            // (they only alternated 1 and 2 - Stefan 10.10).
            // Pages already on screen as extra bars are skipped (Stefan 10.10): page 3 = right bar
            // (option 5), 4 = right bar 2 (option 6), 5 = bottom right (option 4), 6 = bottom left (option 3).
            auto Shown = [](int32 P)
            {
                const bool B5 = FWowClientPreferences::GetWorldOption(TEXT("actionbar5"));
                return (P == 3 && B5) || (P == 4 && B5 && FWowClientPreferences::GetWorldOption(TEXT("actionbar6")))
                    || (P == 5 && FWowClientPreferences::GetWorldOption(TEXT("actionbar4")))
                    || (P == 6 && FWowClientPreferences::GetWorldOption(TEXT("actionbar3")));
            };
            // r373: page 1 without the bonus bar is part of the cycle while one is active (stealth).
            SetActionBarPage(FWowKeyBindings::StepActionPage(FrameName == TEXT("ActionBarUpButton"), Shown));
            bHandledNativeChatControl = true;
        }
        else if (FrameName.StartsWith(TEXT("CharacterFrameTab")))
		{
			const int32 TabIndex = FCString::Atoi(*FrameName.Mid(17));
			if (TabIndex >= 1 && TabIndex <= 3)
			{
				SetActiveCharacterSheetTab(TabIndex);
			}
			bHandledNativeChatControl = true;
		}
		else if (FrameName == TEXT("PlayerTitleFrame") ||
			FrameName.StartsWith(TEXT("PlayerTitleFrameButton")))
		{
			bCharacterTitleMenuOpen = !bCharacterTitleMenuOpen;
			RefreshNativeCharacterTitleControl();
			bHandledNativeChatControl = true;
		}
		else if (FrameName == TEXT("CharacterNativeTitleNoneRow"))
		{
			bCharacterTitleMenuOpen = false;
			if (UTextBlock* TitleText = GetNamedFontString(TEXT("CharacterTitleClientText")))
			{
				TitleText->SetText(FText::FromString(TEXT("No Title")));
			}
			RefreshNativeCharacterTitleControl();
			bHandledNativeChatControl = true;
		}
		else if (FrameName.StartsWith(TEXT("PlayerStatFrameRightDropDown")) ||
			FrameName == TEXT("CharacterNativeRightStatDropDown"))
		{
			bCharacterRightStatMenuOpen = !bCharacterRightStatMenuOpen;
			bCharacterLeftStatMenuOpen = false;
			RefreshNativeCharacterStatControls();
			bHandledNativeChatControl = true;
		}
		else if (FrameName.StartsWith(TEXT("PlayerStatFrameLeftDropDown")) ||
			FrameName == TEXT("CharacterNativeLeftStatDropDown"))
		{
			bCharacterLeftStatMenuOpen = !bCharacterLeftStatMenuOpen;
			bCharacterRightStatMenuOpen = false;
			RefreshNativeCharacterStatControls();
			bHandledNativeChatControl = true;
		}
		else if (FrameName.StartsWith(TEXT("CharacterNativeRightStatRow")) ||
			FrameName.StartsWith(TEXT("CharacterNativeLeftStatRow")))
		{
			const bool bLeftRow = FrameName.StartsWith(TEXT("CharacterNativeLeftStatRow"));
			const FString Prefix = bLeftRow ? TEXT("CharacterNativeLeftStatRow") : TEXT("CharacterNativeRightStatRow");
			const int32 PickedPage = FMath::Clamp(FCString::Atoi(*FrameName.Mid(Prefix.Len())) - 1, 0, 4);
			(bLeftRow ? CharacterLeftStatPage : CharacterRightStatPage) = PickedPage;
			bCharacterRightStatMenuOpen = false;
			bCharacterLeftStatMenuOpen = false;
			RefreshNativeCharacterStatControls();
			bHandledNativeChatControl = true;
		}
		else if (FrameName == TEXT("ChatFrameMenuButton"))
		{
			bNativeChatMenuOpen = !bNativeChatMenuOpen;
			RefreshNativeChatMenu();
			bHandledNativeChatControl = true;
		}
		else if (FrameName.StartsWith(TEXT("ChatNativeMenuRow")))
		{
			const int32 RowIndex = FCString::Atoi(*FrameName.Mid(17));
			if (RowIndex >= 1 && RowIndex <= UE_ARRAY_COUNT(GNativeChatMenuRows))
				EventSystem->ExecuteLuaString(FString::Printf(
					TEXT("ChatFrame_OpenChat(\"%s \", DEFAULT_CHAT_FRAME)"), GNativeChatMenuRows[RowIndex - 1].Slash),
					TEXT("=NativeChatMenu"));
			CloseNativeChatMenu();
			bHandledNativeChatControl = true;
		}
		else if (FrameName == TEXT("MiniMapTrackingButton") || FrameName == TEXT("MiniMapTracking"))
		{
			bMinimapTrackingMenuOpen = !bMinimapTrackingMenuOpen;
			RefreshNativeMinimapControls();
			bHandledNativeChatControl = true;
		}
		else if (FrameName.StartsWith(TEXT("MinimapNativeTrackingRow")))
		{
			const FString Prefix = TEXT("MinimapNativeTrackingRow");
			const int32 RowIndex = FCString::Atoi(*FrameName.Mid(Prefix.Len()));
			MinimapTrackingMode = RowIndex == 12 ? 0 : FMath::Clamp(RowIndex, 1, 11);
			bMinimapTrackingMenuOpen = false;
			RefreshNativeMinimapControls();
			OnMinimapTrackingChanged.ExecuteIfBound(MinimapTrackingMode);
			bHandledNativeChatControl = true;
		}
		else if (FrameName == TEXT("MinimapZoomIn") || FrameName == TEXT("MinimapZoomOut"))
		{
			OnMinimapZoomRequest.ExecuteIfBound(FrameName == TEXT("MinimapZoomIn"));
			bHandledNativeChatControl = true;
		}
		else if (FrameName == TEXT("GameTimeFrame"))
		{
			EventSystem->ExecuteLuaString(TEXT(R"LUA(
				if not IsAddOnLoaded("Blizzard_Calendar") then
					LoadAddOn("Blizzard_Calendar")
				end
				if Calendar_Toggle then
					Calendar_Toggle()
				elseif CalendarFrame then
					if CalendarFrame:IsShown() then CalendarFrame:Hide() else CalendarFrame:Show() end
				end
			)LUA"), TEXT("=NativeGameTimeFrame"));
			bHandledNativeChatControl = true;
		}
		else if (FrameName == TEXT("LFDMicroButton") || FrameName == TEXT("MiniMapLFGFrame"))
		{
			if (OnDungeonFinderToggle.IsBound())
			{
				OnDungeonFinderToggle.Execute();
			}
			else
			{
				const int64 LfdHandle = FindFrame(TEXT("LFDParentFrame"));
				if (LfdHandle >= 0)
				{
					SetFrameVisible(LfdHandle, !IsFrameVisible(LfdHandle));
					SetFrameStrata(LfdHandle, EWowFrameStrata::HIGH);
				}
			}
			bHandledNativeChatControl = true;
		}
		else if (FrameName == TEXT("MiniMapWorldMapButton") ||
			FrameName == TEXT("MinimapZoneTextButton"))
		{
			const int64 WorldMapHandle = FindFrame(TEXT("WorldMapFrame"));
			if (WorldMapHandle >= 0)
			{
				SetFrameVisible(WorldMapHandle, !IsFrameVisible(WorldMapHandle));
			}
			bHandledNativeChatControl = true;
		}
        else if(FrameName==TEXT("WowChatVisibilityButton"))
        {
            SetChatWindowHidden(!bChatWindowHidden);
            bHandledNativeChatControl=true;
        }
		else if (FrameName.StartsWith(TEXT("ChatFrame")) && FrameName.EndsWith(TEXT("Tab")))
		{
			const FString IndexText = FrameName.Mid(9, FrameName.Len() - 12);
			SetActiveChatTab(FCString::Atoi(*IndexText));
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("ChatTabOverflowNativeButton"))
		{
			const int64 ListHandle = FindFrame(TEXT("GeneralDockManagerOverflowButtonList"));
			SetFrameVisible(ListHandle, !IsFrameVisible(ListHandle));
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name.StartsWith(TEXT("ChatTabOverflowRow")))
		{
			SetActiveChatTab(FCString::Atoi(*Entry->Def.Name.Mid(18)));
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name.StartsWith(TEXT("ChatTabContextRow")))
		{
			const int32 RowIndex = FCString::Atoi(*Entry->Def.Name.Mid(17));
			const bool bGeneralContext = ChatContextTabIndex == 1;
			const int32 FontRow = bGeneralContext ? 7 : 5;
			const int32 BackgroundRow = bGeneralContext ? 8 : 6;
			bChatTabContextMenuOpen = false;

			if (RowIndex == 1)
			{
				bChatFrameUnlocked = !bChatFrameUnlocked;
				bChatResizeActive = false;
				SetFrameVisible(FindFrame(TEXT("ChatFrameNativeResizeButton")), bChatFrameUnlocked);
			}
			else if (RowIndex == 2)
			{
				bChatFrameNonInteractive = !bChatFrameNonInteractive;
			}
			else if (RowIndex == 3)
			{
				// The client's naming dialog (StaticPopup with an edit box), not the chat input
				// (Stefan 10.10: the tab name appeared in the "Say:" box). The accepted name comes
				// back through __WowChatTabRename (polled with the chat controls).
				FString OldTabName = ChatTabLabels.FindRef(ChatContextTabIndex);
				OldTabName.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
				OldTabName.ReplaceInline(TEXT("\""), TEXT("\\\""));
				ChatTabRenameIndex = ChatContextTabIndex;
				if (!EventSystem || !EventSystem->ExecuteLuaString(FString::Printf(TEXT(R"LUA(
					__WowChatTabRenameOld = "%s"
					__WowChatTabRename = nil
					if StaticPopupDialogs and StaticPopup_Show then
						StaticPopupDialogs["WOW_NAME_CHAT_TAB"] = {
							text = NAME_CHAT_WINDOW or "Name this chat window:",
							button1 = ACCEPT or "Accept", button2 = CANCEL or "Cancel",
							hasEditBox = 1, maxLetters = 24, whileDead = 1, timeout = 0, hideOnEscape = 1,
							OnShow = function(self)
								local box = _G[self:GetName().."EditBox"]
								if box then box:SetText(__WowChatTabRenameOld or ""); box:SetFocus(); box:HighlightText() end
							end,
							OnAccept = function(self)
								local box = _G[self:GetName().."EditBox"]
								if box then __WowChatTabRename = box:GetText() end
							end,
							EditBoxOnEnterPressed = function(self)
								__WowChatTabRename = self:GetText()
								self:GetParent():Hide()
							end,
							EditBoxOnEscapePressed = function(self) self:GetParent():Hide() end,
						}
						StaticPopup_Show("WOW_NAME_CHAT_TAB")
						__WowChatTabRenameShown = "1"
					end
				)LUA"), *OldTabName), TEXT("=ChatTabRename"))
					|| EventSystem->GetGlobalString(TEXT("__WowChatTabRenameShown")) != TEXT("1"))
				{
					// No StaticPopup: the old way through the chat input.
					bChatTabRenamePending = true;
					FocusEditBox(FindFrame(TEXT("ChatFrame1EditBox")),
						ChatTabLabels.FindRef(ChatContextTabIndex), true);
				}
			}
			else if (bGeneralContext && RowIndex == 4)
			{
				int32 NewTabIndex = INDEX_NONE;
				for (int32 Candidate = 3; Candidate <= 10; ++Candidate)
				{
					if (!EnabledChatTabs.Contains(Candidate))
					{
						NewTabIndex = Candidate;
						break;
					}
				}
				if (NewTabIndex != INDEX_NONE)
				{
					ConfigureChatTab(NewTabIndex,
						FString::Printf(TEXT("Chat %d"), NewTabIndex), true);
					SetActiveChatTab(NewTabIndex);
				}
			}
			else if (bGeneralContext && RowIndex == 5)
			{
				for (int32 ChatIndex = 3; ChatIndex <= 10; ++ChatIndex)
				{
					ConfigureChatTab(ChatIndex, FString(), false);
				}
				ChatTabLabels.Add(1, TEXT("General"));
				ChatTabLabels.Add(2, TEXT("Combat Log"));
				bChatFrameUnlocked = false;
				bChatFrameNonInteractive = false;
				bChatBackgroundEnabled = true;
				SetActiveChatTab(1);
				for (int32 ChatIndex = 1; ChatIndex <= 2; ++ChatIndex)
				{
					if (UWidget* Frame = GetWidgetForHandle(FindFrame(
						*FString::Printf(TEXT("ChatFrame%d"), ChatIndex))))
					{
						if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Frame->Slot))
						{
							Slot->SetSize(FVector2D(430.0f, 120.0f) * UIScale);
						}
					}
				}
			}
			else if (RowIndex == FontRow)
			{
				ChatFontSize = ChatFontSize >= 14 ? 10 : ChatFontSize + 2;
				for (auto& MessagePair : ScrollingMessageWidgets)
				{
					for (FScrollingMessageEntry& MessageLine : MessagePair.Value)
					{
						RebuildScrollingMessageWidget(MessagePair.Key, MessageLine);
					}
					RefreshScrollingMessageLayout(MessagePair.Key);
				}
			}
			else if (RowIndex == BackgroundRow)
			{
				bChatBackgroundEnabled = !bChatBackgroundEnabled;
			}
			else
			{
				AddScrollingMessage(FindFrame(TEXT("ChatFrame1")),
					TEXT("That chat-window option is not available yet."),
					WowColor::FromWow(1.0f, 0.82f, 0.0f, 1.0f));
			}
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("CombatLogFilterSelf"))
		{
			if (ActiveCombatLogFilterIndex != 1) { ActiveCombatLogFilterIndex = 1; ReplayCombatLog(); }
			bCombatLogFilterMenuOpen = false;
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("CombatLogFilterEverything"))
		{
			if (ActiveCombatLogFilterIndex != 2) { ActiveCombatLogFilterIndex = 2; ReplayCombatLog(); }
			bCombatLogFilterMenuOpen = false;
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("CombatLogFilterWhatHappened"))
		{
			if (ActiveCombatLogFilterIndex != 3) { ActiveCombatLogFilterIndex = 3; ReplayCombatLog(); }
			bCombatLogFilterMenuOpen = false;
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("CombatLogFilterDropDownButton"))
		{
			bCombatLogFilterMenuOpen = !bCombatLogFilterMenuOpen;
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("CombatLogFilterKills"))
		{
			bCombatLogKillsEnabled = !bCombatLogKillsEnabled;
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("ChatFrame1ButtonFrameUpButton"))
		{
			ScrollMessages(FindFrame(ActiveChatTabIndex == 2 ? TEXT("ChatFrame2") : TEXT("ChatFrame1")), 1);
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("ChatFrame1ButtonFrameDownButton"))
		{
			ScrollMessages(FindFrame(ActiveChatTabIndex == 2 ? TEXT("ChatFrame2") : TEXT("ChatFrame1")), -1);
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("ChatFrame1ButtonFrameBottomButton"))
		{
			ScrollMessagesToBottom(FindFrame(ActiveChatTabIndex == 2 ? TEXT("ChatFrame2") : TEXT("ChatFrame1")));
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name.StartsWith(TEXT("FriendsFrameCloseButton")))
		{
			SetFrameVisible(FindFrame(TEXT("FriendsFrame")), false);
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("SocialNativeListTab1"))
		{
			bSocialIgnoreSelected = false;
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("SocialNativeListTab2"))
		{
			bSocialIgnoreSelected = true;
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name.StartsWith(TEXT("SocialNativeTab")))
		{
			ActiveSocialTabIndex = FMath::Clamp(
				FCString::Atoi(*Entry->Def.Name.Mid(15)), 1, 5);
			if (ActiveSocialTabIndex == 4)
			{
				SetFrameVisible(FindFrame(TEXT("FriendsFrame")), false);
				SetActiveChatTab(1);
			}
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("SocialNativeReplyButton"))
		{
			if (!RecentWhisperSender.IsEmpty())
			{
				SetFrameVisible(FindFrame(TEXT("FriendsFrame")), false);
				SetActiveChatTab(1);
				FocusEditBox(FindFrame(TEXT("ChatFrame1EditBox")),
					FString::Printf(TEXT("/whisper %s "), *RecentWhisperSender));
			}
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("SocialNativeAddFriendButton"))
		{
			SetFrameVisible(FindFrame(TEXT("FriendsFrame")), false);
			SetActiveChatTab(1);
			FocusEditBox(FindFrame(TEXT("ChatFrame1EditBox")), TEXT("/friend "));
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name.StartsWith(TEXT("SocialNativeFriendContextRow")))
		{
			const int32 ContextAction = FCString::Atoi(*Entry->Def.Name.Mid(28));
			if (ContextAction >= 1 && ContextAction <= 3 && !SocialContextFriendName.IsEmpty())
			{
				static const TCHAR* SocialCommands[] = {TEXT("/whisper "), TEXT("/invite "), TEXT("/removefriend ")};
				SetFrameVisible(FindFrame(TEXT("FriendsFrame")), false);
				SetActiveChatTab(1);
				FocusEditBox(FindFrame(TEXT("ChatFrame1EditBox")),
					FString::Printf(TEXT("%s%s "), SocialCommands[ContextAction - 1], *SocialContextFriendName));
			}
			bSocialFriendContextMenuOpen = false;
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("FriendsMicroButton"))
		{
			const int64 FriendsHandle = FindFrame(TEXT("FriendsFrame"));
			if (FriendsHandle >= 0) SetFrameVisible(FriendsHandle, !IsFrameVisible(FriendsHandle));
			bHandledNativeChatControl = true;
		}
		else if (Entry->Def.Name == TEXT("ChatFrameMenuButton"))
		{
			// The stock UIMenu (ChatMenu_OnLoad fills Say/Party/Raid/... with their slash
			// shortcuts). If its OnLoad did not complete the menu was an empty grey square:
			// run it again here and log why it failed.
			EventSystem->ExecuteLuaString(TEXT(R"LUA(
				__WowChatMenuError = ""
				local ok, err = pcall(function()
					if ChatMenu and (not ChatMenu.numButtons or ChatMenu.numButtons == 0) and ChatMenu_OnLoad then
						ChatMenu_OnLoad(ChatMenu)
					end
					if ChatFrameMenu_UpdateAnchorPoint then ChatFrameMenu_UpdateAnchorPoint() end
					if ChatMenu:IsShown() then ChatMenu:Hide() else ChatMenu:Show() end
				end)
				if not ok then __WowChatMenuError = tostring(err) end
			)LUA"), TEXT("=ChatMenuToggle"));
			const FString MenuError = EventSystem->GetGlobalString(TEXT("__WowChatMenuError"));
			if (!MenuError.IsEmpty())
			{
				UE_LOG(LogWowFrame, Warning, TEXT("ChatMenu toggle failed: %s"), *MenuError);
				const int64 MenuHandle = FindFrame(TEXT("ChatMenu"));
				if (MenuHandle >= 0) SetFrameVisible(MenuHandle, !IsFrameVisible(MenuHandle));
			}
			bHandledNativeChatControl = true;
		}
	}

	if (!bHandledNativeChatControl)
	{
		EventSystem->RunFrameClickScript(Handle, TEXT("PreClick"), Button, bMouseDown);
	}

	// Native secure action dispatch — emulates WoW's C++ SecureActionButton behavior.
	// The FrameXML OnLoad for SecureActionButtonTemplate sets type="action" via Lua,
	// but this often fails in our environment. So we also detect action buttons by
	// name pattern (ActionButton1-12, MultiBarBottomLeftButton1-12, etc.) and by
	// checking the Lua attribute as a fallback.
	if (Entry && (Entry->Def.Type == EWowFrameType::Button || Entry->Def.Type == EWowFrameType::CheckButton))
	{
		int32 ActionSlot = -1;

		// Method 1: Detect action buttons by name pattern
		// ActionButton1-12 → slots 1-12 (page 1)
		// MultiBarBottomLeftButton1-12 → slots 61-72
		// MultiBarBottomRightButton1-12 → slots 49-60
		// MultiBarRightButton1-12 → slots 25-36
		// MultiBarLeftButton1-12 → slots 37-48
		// BonusActionButton1-12 → slots 73-84
		const FString& Name = Entry->Def.Name;
		const int32 ZeroBasedActionSlot = ResolveActionSlotFromFrameName(Name);
		if (ZeroBasedActionSlot >= 0)
		{
			ActionSlot = ZeroBasedActionSlot + 1;
		}

		// Method 2: Check Lua attribute (if OnLoad managed to set it)
		if (ActionSlot < 0)
		{
			FString ActionType = EventSystem->GetFrameAttribute(Handle, TEXT("type"));
			if (ActionType.Equals(TEXT("action"), ESearchCase::IgnoreCase))
			{
				FString ActionAttr = EventSystem->GetFrameAttribute(Handle, TEXT("action"));
				if (!ActionAttr.IsEmpty())
				{
					ActionSlot = FCString::Atoi(*ActionAttr);
				}
				else
				{
					int32 FrameID = EventSystem->GetFrameID(Handle);
					if (FrameID > 0) ActionSlot = FrameID;
				}
			}
			else if (ActionType.Equals(TEXT("spell"), ESearchCase::IgnoreCase))
			{
				FString SpellAttr = EventSystem->GetFrameAttribute(Handle, TEXT("spell"));
				if (!SpellAttr.IsEmpty())
				{
					int32 SpellId = FCString::Atoi(*SpellAttr);
					if (SpellId > 0 && OnSecureSpellDispatch.IsBound())
					{
						OnSecureSpellDispatch.Execute(SpellId);
					}
				}
			}
		}

		if (ActionSlot > 0 && OnSecureActionDispatch.IsBound())
		{
			OnSecureActionDispatch.Execute(ActionSlot - 1); // Convert to 0-based
			UE_LOG(LogWowFrame, Log, TEXT("Secure action dispatch: slot %d (frame %s)"),
				ActionSlot, *Entry->Def.Name);
		}
	}

	if (!bHandledNativeChatControl)
	{
		// A CheckButton flips its state before OnClick runs, like the client does.
		if (Entry && Entry->Def.Type == EWowFrameType::CheckButton && IsFrameClickRegistered(Handle, Button, bMouseDown))
		{
			SetButtonChecked(Handle, !IsButtonChecked(Handle));
		}
		EventSystem->RunFrameClickScript(Handle, TEXT("OnClick"), Button, bMouseDown);
		// PostClick runs after the click like in the client. Action buttons use it to reset the
		// checked (yellow) border to IsCurrentAction() after the CheckButton flipped it.
		const bool bRanPostClick = EventSystem->RunFrameClickScript(Handle, TEXT("PostClick"), Button, bMouseDown);
		// Unit frames: right-click opens the unit menu (SecureUnitButton "*type2" = "menu").
		// When the secure template did not open it, call the frame's menu function ourselves.
		if (Entry && !bMouseDown && Button.Equals(TEXT("RightButton"), ESearchCase::IgnoreCase)
			&& (Entry->Def.Name == TEXT("PlayerFrame") || Entry->Def.Name == TEXT("TargetFrame") || Entry->Def.Name == TEXT("FocusFrame")
				|| Entry->Def.Name.StartsWith(TEXT("PartyMemberFrame"))))
		{
			EventSystem->ExecuteLuaString(FString::Printf(TEXT(R"LUA(
				if not (DropDownList1 and DropDownList1:IsShown()) then
					local f = _G["%s"]
					if f and type(f.menu) == "function" then f.menu(f)
					elseif f and f:GetName() == "PlayerFrame" and PlayerFrameDropDown and ToggleDropDownMenu then
						ToggleDropDownMenu(1, nil, PlayerFrameDropDown, "PlayerFrame", 106, 27)
					end
				end
			)LUA"), *Entry->Def.Name), TEXT("=UnitFrameMenu"));
		}
		// Action button without a PostClick script (addon bars): do not leave the flipped
		// checked border on; IsCurrentAction() decides it again on the next update.
		if (!bRanPostClick && Entry && Entry->Def.Type == EWowFrameType::CheckButton
			&& ResolveActionSlotFromFrameName(Entry->Def.Name) >= 0)
		{
			SetButtonChecked(Handle, false);
		}
	}
}

void FWowFrameManager::SetButtonChecked(int64 Handle, bool bChecked)
{
	if (bChecked) CheckedButtons.Add(Handle); else CheckedButtons.Remove(Handle);
	if (const TWeakObjectPtr<UImage>* Check = CheckedTextureWidgets.Find(Handle); Check && Check->IsValid())
	{
		(*Check)->SetVisibility(bChecked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void FWowFrameManager::SetCheckedTexture(int64 Handle, const FString& TexturePath)
{
	UImage* Check = nullptr;
	if (const TWeakObjectPtr<UImage>* Existing = CheckedTextureWidgets.Find(Handle); Existing && Existing->IsValid())
	{
		Check = Existing->Get();
	}
	else if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(GetWidgetForHandle(Handle)))
	{
		Check = NewObject<UImage>(Canvas);
		if (UCanvasPanelSlot* CheckSlot = Canvas->AddChildToCanvas(Check))
		{
			CheckSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			CheckSlot->SetOffsets(FMargin(0.f));
			CheckSlot->SetZOrder(-50);
		}
		CheckedTextureWidgets.Add(Handle, Check);
	}
	if (!Check) return;
	FSlateBrush Brush;
	if (UTexture2D* Texture = TexturePath.IsEmpty() ? nullptr : LoadUITexture(TexturePath))
	{
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		Brush.DrawAs = ESlateBrushDrawType::Image;
	}
	else
	{
		Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
	}
	Check->SetBrush(Brush);
	Check->SetVisibility(IsButtonChecked(Handle) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

int32 FWowFrameManager::GetActionSlotForFrame(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? ResolveActionSlotFromFrameName(Entry->Def.Name) : -1;
}

void FWowFrameManager::RefreshActionButtonIcons(const TArray<uint32>& PackedActions)
{
	auto FrameNameForSlot = [](int32 SlotIndex) -> FString
	{
		if (SlotIndex >= 0 && SlotIndex < 12)
			return FString::Printf(TEXT("ActionButton%d"), SlotIndex + 1);
		if (SlotIndex >= 24 && SlotIndex < 36)
			return FString::Printf(TEXT("MultiBarRightButton%d"), SlotIndex - 23);
		if (SlotIndex >= 36 && SlotIndex < 48)
			return FString::Printf(TEXT("MultiBarLeftButton%d"), SlotIndex - 35);
		if (SlotIndex >= 48 && SlotIndex < 60)
			return FString::Printf(TEXT("MultiBarBottomRightButton%d"), SlotIndex - 47);
		if (SlotIndex >= 60 && SlotIndex < 72)
			return FString::Printf(TEXT("MultiBarBottomLeftButton%d"), SlotIndex - 59);
		return FString();
	};

	for (int32 SlotIndex = 0; SlotIndex < 72; ++SlotIndex)
	{
		const FString FrameName = FrameNameForSlot(SlotIndex);
		if (FrameName.IsEmpty()) continue;
		const int64 ButtonHandle = FindFrame(FrameName);
		UCanvasPanel* ButtonCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(ButtonHandle));
		if (!ButtonCanvas) continue;
		if (UImage* AuthoredIcon = GetNamedTexture(FrameName + TEXT("Icon")))
		{
			// FrameXML's icon region starts life as a deliberately non-drawing brush.
			AuthoredIcon->SetRenderOpacity(0.0f);
		}
		const FString NativeIconName = FrameName + TEXT("NativeActionIcon");
		UImage* Icon = GetNamedTexture(NativeIconName);
		if (!Icon)
		{
			Icon = NewObject<UImage>(ButtonCanvas);
			Icon->SetVisibility(ESlateVisibility::Collapsed);
			if (UCanvasPanelSlot* NativeSlot = ButtonCanvas->AddChildToCanvas(Icon))
			{
				NativeSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
				NativeSlot->SetOffsets(FMargin(1.5f * UIScale));
				NativeSlot->SetZOrder(50);
			}
			NamedTextureWidgets.Add(NativeIconName, Icon);
		}

        // One label for the saved primary binding on every actionbar.
        {
			const FString HotKeyName = FrameName + TEXT("NativeHotKey");
			UTextBlock* HotKey = GetNamedFontString(HotKeyName);
			if (!HotKey)
			{
				HotKey = NewObject<UTextBlock>(ButtonCanvas);
				if (UCanvasPanelSlot* HotKeySlot = ButtonCanvas->AddChildToCanvas(HotKey))
				{
					HotKeySlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
					HotKeySlot->SetOffsets(FMargin(2.0f * UIScale, 1.0f * UIScale,
						2.0f * UIScale, 1.0f * UIScale));
					HotKeySlot->SetZOrder(90);
				}
				NamedFontStringWidgets.Add(HotKeyName, HotKey);
			}
			FString KeyLabel;
            for(const auto& Action:FWowKeyBindings::Actions()) if(Action.ActionSlot==SlotIndex)
            { const auto Keys=FWowKeyBindings::Keys(Action.Command); for(const auto& Key:Keys)if(!Key.IsEmpty()){KeyLabel=Key;break;} break; }
            HotKey->SetText(FText::FromString(KeyLabel));
			HotKey->SetJustification(ETextJustify::Right);
			HotKey->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			HotKey->SetShadowOffset(FVector2D(1.0f));
			HotKey->SetShadowColorAndOpacity(FLinearColor::Black);
			FSlateFontInfo HotKeyFont = HotKey->GetFont();
			HotKeyFont.Size = FMath::Max(8, FMath::RoundToInt(8.0f * UIScale));
			HotKey->SetFont(HotKeyFont);
			HotKey->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
        const int32 DataSlot=SlotIndex<12?SlotIndex+12*(FWowKeyBindings::ActionPage()-1):SlotIndex;
		const uint32 PackedAction = PackedActions.IsValidIndex(DataSlot) ? PackedActions[DataSlot] : 0;
		if (PackedAction == 0)
		{
			Icon->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}

		const uint8 ActionType = static_cast<uint8>((PackedAction >> 24) & 0xFF);
		const uint32 ActionId = PackedAction & 0x00FFFFFFu;
		FString TexturePath = TEXT("Interface\\Icons\\INV_Misc_QuestionMark");
		if (ActionType == 0)
		{
			if (const FSpellDbcEntry* Spell = FDbcStore::Get().Spells().GetById(ActionId))
			{
				if (const FSpellIconDbcEntry* SpellIcon = FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID);
					SpellIcon && !SpellIcon->TexturePath.IsEmpty())
				{
					TexturePath = SpellIcon->TexturePath;
				}
			}
		}
		else if (ActionType == 0x80)
		{
			// Item action (potions, quest items, hearthstone): the item's own icon.
			uint32 DisplayId = 0;
			if (ItemConnection.IsValid())
			{
				if (const FWowItemTemplateInfo* Info = ItemConnection->PacketHandler.ItemTemplates.Find(ActionId))
					DisplayId = Info->DisplayId;
				else
					ItemConnection->PacketHandler.RequestItemTemplate(ActionId);
			}
			TexturePath = FWowItemPresentation::IconPath(ActionId, DisplayId);
		}
		else if (ActionType == 64)
		{
			// r311 (Stefan 13.10: a Smite macro on the stock bar showed the question mark): a macro
			// shows its icon, and a "?" macro the spell of its #showtooltip / first /cast line.
			if (const FWowMacro* Macro = FWowMacros::Get(static_cast<int32>(ActionId)))
				TexturePath = FWowMacros::DisplayIcon(*Macro, ItemConnection.Get());
		}

		UTexture2D* ActionTexture = LoadUITexture(TexturePath);
		// SetBrushFromTexture creates a fresh image brush and synchronizes the live
		// Slate resource. Reusing the FrameXML brush retained its empty draw state
		// (and inherited atlas UVs), so successfully loaded spell textures remained
		// invisible even though the UTexture2D was valid.
		Icon->SetBrushFromTexture(ActionTexture, true);
		Icon->SetColorAndOpacity(FLinearColor::White);
		Icon->SetRenderOpacity(1.0f);
		Icon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		UE_LOG(LogWowFrame, Log, TEXT("Action icon slot=%d id=%u path=%s texture=%s"),
			SlotIndex + 1, ActionId, *TexturePath,
			ActionTexture ? *ActionTexture->GetName() : TEXT("NONE"));
	}
}

void FWowFrameManager::SetFrameRegisteredDragButtons(int64 Handle, const TArray<FString>& Buttons)
{
    if (auto* Entry = Frames.Find(Handle))
    {
        Entry->RegisteredDragButtons.Reset();
        for (const auto& Button : Buttons) Entry->RegisteredDragButtons.Add(Button.ToLower());
    }
}

bool FWowFrameManager::IsFrameDragRegistered(int64 Handle, const FString& Button) const
{
    const auto* Entry = Frames.Find(Handle);
    return Entry && Entry->RegisteredDragButtons.Contains(Button.ToLower());
}

bool FWowFrameManager::DispatchDragStart(int64 Handle, const FString& Button)
{
    // Chat tabs: FCF's OnDragStart undocks the window and starts moving it, but the dock,
    // tabs and chat frames are laid out natively here, so an undocked frame lost its tabs.
    // A slightly moving click on Combat Log / General must stay a tab click.
    if (const FFrameEntry* Entry = Frames.Find(Handle);
        Entry && Entry->Def.Name.StartsWith(TEXT("ChatFrame")) && Entry->Def.Name.EndsWith(TEXT("Tab")))
        return false;
    return EventSystem && IsFrameDragRegistered(Handle, Button)
        && EventSystem->RunFrameScript(Handle, TEXT("OnDragStart"), {Button});
}

void FWowFrameManager::DispatchDragStop(int64 Handle, const FString& Button)
{
    if (EventSystem && Handle >= 0) EventSystem->RunFrameScript(Handle, TEXT("OnDragStop"), {Button});
}

bool FWowFrameManager::DispatchReceiveDrag(int64 Handle)
{
	if (!EventSystem || Handle < 0)
	{
		return false;
	}

	return EventSystem->RunFrameScript(Handle, TEXT("OnReceiveDrag"));
}

// ── ScrollFrame Implementation ──────────────────────────────────────────────

void FWowFrameManager::SetScrollChild(int64 ScrollFrameHandle, int64 ChildHandle)
{
	if (ScrollFrameHandle < 0 || ChildHandle < 0)
		return;

	const FFrameEntry* ScrollFrameEntry = Frames.Find(ScrollFrameHandle);
	const FFrameEntry* ChildEntry = Frames.Find(ChildHandle);

	if (!ScrollFrameEntry || !ChildEntry || ScrollFrameEntry->Def.Type != EWowFrameType::ScrollFrame)
		return;

	// Store the child relationship
	ScrollChildHandles.Add(ScrollFrameHandle, ChildHandle);

	// Ensure child is parented to the scroll frame (reparent if necessary)
	UWidget* ScrollWidget = ScrollFrameEntry->Widget.Get();
	UWidget* ChildWidget = ChildEntry->Widget.Get();

	if (ScrollWidget && ChildWidget)
	{
		UCanvasPanel* ScrollCanvas = Cast<UCanvasPanel>(ScrollWidget);
		if (ScrollCanvas && ChildWidget->GetParent() != ScrollCanvas)
		{
			// Remove from old parent if needed
			if (UPanelWidget* OldParent = ChildWidget->GetParent())
			{
				OldParent->RemoveChild(ChildWidget);
			}

			// Add to scroll frame canvas
			UCanvasPanelSlot* Slot = ScrollCanvas->AddChildToCanvas(ChildWidget);
			if (Slot)
			{
				// Apply current scroll offset
				float VerticalOffset = ScrollVerticalOffsets.FindRef(ScrollFrameHandle);
				float HorizontalOffset = ScrollHorizontalOffsets.FindRef(ScrollFrameHandle);

				FVector2D CurrentPos = Slot->GetPosition();
				Slot->SetPosition(FVector2D(CurrentPos.X - HorizontalOffset * UIScale,
											CurrentPos.Y - VerticalOffset * UIScale));
			}
		}
	}

	UE_LOG(LogWowFrame, Log, TEXT("SetScrollChild: frame %lld child %lld"), ScrollFrameHandle, ChildHandle);
}

int64 FWowFrameManager::GetScrollChild(int64 ScrollFrameHandle) const
{
	if (ScrollFrameHandle < 0)
		return -1;

	return ScrollChildHandles.FindRef(ScrollFrameHandle);
}

void FWowFrameManager::SetVerticalScroll(int64 ScrollFrameHandle, float Offset)
{
	if (ScrollFrameHandle < 0)
		return;

	const FFrameEntry* ScrollFrameEntry = Frames.Find(ScrollFrameHandle);
	if (!ScrollFrameEntry || ScrollFrameEntry->Def.Type != EWowFrameType::ScrollFrame)
		return;

	// Clamp offset to valid range
	Offset = FMath::Max(0.0f, Offset);

	float MaxScroll = GetVerticalScrollRange(ScrollFrameHandle);
	if (MaxScroll > 0.0f)
	{
		Offset = FMath::Min(Offset, MaxScroll);
	}

	float OldOffset = ScrollVerticalOffsets.FindRef(ScrollFrameHandle);
	ScrollVerticalOffsets.Add(ScrollFrameHandle, Offset);

	// Update child position if child exists
	int64 ChildHandle = GetScrollChild(ScrollFrameHandle);
	if (ChildHandle >= 0)
	{
		const FFrameEntry* ChildEntry = Frames.Find(ChildHandle);
		if (ChildEntry && ChildEntry->Widget.IsValid())
		{
			UWidget* ChildWidget = ChildEntry->Widget.Get();
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ChildWidget->Slot))
			{
				FVector2D CurrentPos = Slot->GetPosition();
				// Adjust Y position by the change in scroll offset
				float DeltaScroll = Offset - OldOffset;
				Slot->SetPosition(FVector2D(CurrentPos.X, CurrentPos.Y - DeltaScroll * UIScale));
			}
		}
	}

	UE_LOG(LogWowFrame, Verbose, TEXT("SetVerticalScroll: frame %lld offset %.2f (was %.2f)"),
		ScrollFrameHandle, Offset, OldOffset);
}

float FWowFrameManager::GetVerticalScroll(int64 ScrollFrameHandle) const
{
	if (ScrollFrameHandle < 0)
		return 0.0f;

	return ScrollVerticalOffsets.FindRef(ScrollFrameHandle);
}

void FWowFrameManager::SetHorizontalScroll(int64 ScrollFrameHandle, float Offset)
{
	if (ScrollFrameHandle < 0)
		return;

	const FFrameEntry* ScrollFrameEntry = Frames.Find(ScrollFrameHandle);
	if (!ScrollFrameEntry || ScrollFrameEntry->Def.Type != EWowFrameType::ScrollFrame)
		return;

	// Clamp offset to valid range
	Offset = FMath::Max(0.0f, Offset);

	float MaxScroll = GetHorizontalScrollRange(ScrollFrameHandle);
	if (MaxScroll > 0.0f)
	{
		Offset = FMath::Min(Offset, MaxScroll);
	}

	float OldOffset = ScrollHorizontalOffsets.FindRef(ScrollFrameHandle);
	ScrollHorizontalOffsets.Add(ScrollFrameHandle, Offset);

	// Update child position if child exists
	int64 ChildHandle = GetScrollChild(ScrollFrameHandle);
	if (ChildHandle >= 0)
	{
		const FFrameEntry* ChildEntry = Frames.Find(ChildHandle);
		if (ChildEntry && ChildEntry->Widget.IsValid())
		{
			UWidget* ChildWidget = ChildEntry->Widget.Get();
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(ChildWidget->Slot))
			{
				FVector2D CurrentPos = Slot->GetPosition();
				// Adjust X position by the change in scroll offset
				float DeltaScroll = Offset - OldOffset;
				Slot->SetPosition(FVector2D(CurrentPos.X - DeltaScroll * UIScale, CurrentPos.Y));
			}
		}
	}

	UE_LOG(LogWowFrame, Verbose, TEXT("SetHorizontalScroll: frame %lld offset %.2f (was %.2f)"),
		ScrollFrameHandle, Offset, OldOffset);
}

float FWowFrameManager::GetHorizontalScroll(int64 ScrollFrameHandle) const
{
	if (ScrollFrameHandle < 0)
		return 0.0f;

	return ScrollHorizontalOffsets.FindRef(ScrollFrameHandle);
}

float FWowFrameManager::GetVerticalScrollRange(int64 ScrollFrameHandle) const
{
	if (ScrollFrameHandle < 0)
		return 0.0f;

	const FFrameEntry* ScrollFrameEntry = Frames.Find(ScrollFrameHandle);
	if (!ScrollFrameEntry || ScrollFrameEntry->Def.Type != EWowFrameType::ScrollFrame)
		return 0.0f;

	int64 ChildHandle = GetScrollChild(ScrollFrameHandle);
	if (ChildHandle < 0)
		return 0.0f;

	const FFrameEntry* ChildEntry = Frames.Find(ChildHandle);
	if (!ChildEntry || !ChildEntry->Widget.IsValid() || !ScrollFrameEntry->Widget.IsValid())
		return 0.0f;

	// Get frame sizes in WoW coordinates (pre-scaling)
	float ScrollFrameHeight = ScrollFrameEntry->Def.Height;
	float ChildHeight = ChildEntry->Def.Height;

	// If frame height is 0 (auto-sized), get actual widget size
	if (ScrollFrameHeight <= 0.0f)
	{
		if (UCanvasPanelSlot* ScrollSlot = Cast<UCanvasPanelSlot>(ScrollFrameEntry->Widget->Slot))
		{
			ScrollFrameHeight = ScrollSlot->GetSize().Y / UIScale;
		}
	}

	if (ChildHeight <= 0.0f)
	{
		if (UCanvasPanelSlot* ChildSlot = Cast<UCanvasPanelSlot>(ChildEntry->Widget->Slot))
		{
			ChildHeight = ChildSlot->GetSize().Y / UIScale;
		}
	}

	// Maximum scroll is the difference between child height and frame height
	return FMath::Max(0.0f, ChildHeight - ScrollFrameHeight);
}

float FWowFrameManager::GetHorizontalScrollRange(int64 ScrollFrameHandle) const
{
	if (ScrollFrameHandle < 0)
		return 0.0f;

	const FFrameEntry* ScrollFrameEntry = Frames.Find(ScrollFrameHandle);
	if (!ScrollFrameEntry || ScrollFrameEntry->Def.Type != EWowFrameType::ScrollFrame)
		return 0.0f;

	int64 ChildHandle = GetScrollChild(ScrollFrameHandle);
	if (ChildHandle < 0)
		return 0.0f;

	const FFrameEntry* ChildEntry = Frames.Find(ChildHandle);
	if (!ChildEntry || !ChildEntry->Widget.IsValid() || !ScrollFrameEntry->Widget.IsValid())
		return 0.0f;

	// Get frame sizes in WoW coordinates (pre-scaling)
	float ScrollFrameWidth = ScrollFrameEntry->Def.Width;
	float ChildWidth = ChildEntry->Def.Width;

	// If frame width is 0 (auto-sized), get actual widget size
	if (ScrollFrameWidth <= 0.0f)
	{
		if (UCanvasPanelSlot* ScrollSlot = Cast<UCanvasPanelSlot>(ScrollFrameEntry->Widget->Slot))
		{
			ScrollFrameWidth = ScrollSlot->GetSize().X / UIScale;
		}
	}

	if (ChildWidth <= 0.0f)
	{
		if (UCanvasPanelSlot* ChildSlot = Cast<UCanvasPanelSlot>(ChildEntry->Widget->Slot))
		{
			ChildWidth = ChildSlot->GetSize().X / UIScale;
		}
	}

	// Maximum scroll is the difference between child width and frame width
	return FMath::Max(0.0f, ChildWidth - ScrollFrameWidth);
}

// ── Tooltip Implementation ──────────────────────────────────────────────────

void FWowFrameManager::TooltipSetOwner(int64 TooltipHandle, int64 OwnerHandle, const FString& AnchorType)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	// SetOwner starts a new tooltip (the client clears it): without this, hovering
	// a LibDBIcon button appended its lines to the previous tooltip's, twice.
	TooltipClearLines(TooltipHandle);
	State = GetTooltipState(TooltipHandle);
	if (!State) return;
	State->SpellId = 0; // a new tooltip is not a spell tooltip until SetSpell (header size)
	State->OwnerHandle = OwnerHandle;
	State->AnchorType = AnchorType;
	const FWowFrameDef* TooltipDef = GetFrameDef(TooltipHandle);
	if (TooltipDef && (TooltipDef->Name == TEXT("GameTooltip") || TooltipDef->Type == EWowFrameType::GameTooltip))
	{
		// ANCHOR_NONE: the caller places it with SetPoint next (GameTooltip_SetDefaultAnchor,
		// LibDBIcon next to its button). ANCHOR_<side>: next to the owner. Otherwise the
		// default bottom-right corner (or the "follow mouse" option).
		if (AnchorType == TEXT("ANCHOR_NONE"))
		{
			// SetOwner also clears the tooltip's points in the client.
			ClearFrameAnchors(TooltipHandle);
			return;
		}
		const bool bOwnerSide = AnchorType == TEXT("ANCHOR_LEFT") || AnchorType == TEXT("ANCHOR_RIGHT")
			|| AnchorType == TEXT("ANCHOR_TOP") || AnchorType == TEXT("ANCHOR_BOTTOM")
			|| AnchorType == TEXT("ANCHOR_TOPLEFT") || AnchorType == TEXT("ANCHOR_TOPRIGHT")
			|| AnchorType == TEXT("ANCHOR_BOTTOMLEFT") || AnchorType == TEXT("ANCHOR_BOTTOMRIGHT");
		const FWowFrameDef* OwnerDef = OwnerHandle > 0 ? GetFrameDef(OwnerHandle) : nullptr;
		if (!bOwnerSide || !OwnerDef || OwnerDef->Name.IsEmpty() || OwnerDef->Name == TEXT("UIParent"))
		{
			PositionTooltipAtScreenCorner(TooltipHandle);
			return;
		}
	}

	// Position the tooltip relative to owner frame if valid
	if (OwnerHandle > 0)
	{
		const FWowFrameDef* OwnerDef = GetFrameDef(OwnerHandle);
		if (OwnerDef)
		{
			// Create anchor based on anchor type
			TArray<FWowAnchor> NewAnchors;
			FWowAnchor TooltipAnchor;

			// As in the client: ANCHOR_LEFT/RIGHT put the tooltip above the owner's
			// top-left/top-right corner, growing outwards.
			if (AnchorType == TEXT("ANCHOR_LEFT"))
			{
				TooltipAnchor.Point = EWowAnchorPoint::BOTTOMRIGHT;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::TOPLEFT;
			}
			else if (AnchorType == TEXT("ANCHOR_RIGHT"))
			{
				TooltipAnchor.Point = EWowAnchorPoint::BOTTOMLEFT;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::TOPRIGHT;
			}
			else if (AnchorType == TEXT("ANCHOR_TOPLEFT"))
			{
				TooltipAnchor.Point = EWowAnchorPoint::BOTTOMLEFT;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::TOPLEFT;
			}
			else if (AnchorType == TEXT("ANCHOR_TOPRIGHT"))
			{
				TooltipAnchor.Point = EWowAnchorPoint::BOTTOMRIGHT;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::TOPRIGHT;
			}
			else if (AnchorType == TEXT("ANCHOR_BOTTOMLEFT"))
			{
				TooltipAnchor.Point = EWowAnchorPoint::TOPLEFT;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::BOTTOMLEFT;
			}
			else if (AnchorType == TEXT("ANCHOR_BOTTOMRIGHT"))
			{
				TooltipAnchor.Point = EWowAnchorPoint::TOPRIGHT;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::BOTTOMRIGHT;
			}
			else if (AnchorType == TEXT("ANCHOR_TOP"))
			{
				TooltipAnchor.Point = EWowAnchorPoint::BOTTOM;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::TOP;
			}
			else if (AnchorType == TEXT("ANCHOR_BOTTOM"))
			{
				TooltipAnchor.Point = EWowAnchorPoint::TOP;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::BOTTOM;
			}
			else // ANCHOR_CURSOR or default
			{
				TooltipAnchor.Point = EWowAnchorPoint::BOTTOMLEFT;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::BOTTOMLEFT;
				TooltipAnchor.OffsetX = 5.f;
				TooltipAnchor.OffsetY = 5.f;
			}

			TooltipAnchor.RelativeTo = OwnerDef->Name;
			NewAnchors.Add(TooltipAnchor);
			SetFrameAnchors(TooltipHandle, NewAnchors);
		}
	}
}

void FWowFrameManager::PositionTooltipAtScreenCorner(int64 TooltipHandle)
{
	const FFrameEntry* TooltipEntry = Frames.Find(TooltipHandle);
	if (!TooltipEntry)
	{
		return;
	}

	// VR: GameTooltip always at one fixed place in the UI window, left of the middle where no
	// other panel crops the window (Stefan 09.10: the action-bar tooltip was cut by the action
	// bar panel; he wants it at a fixed spot instead of following the pointer). The presenter
	// shows the Tooltip zone as its own panel beside the minimap. Shopping tooltips anchor to it.
	if (FWowUiVrHost::IsActive() && TooltipEntry->Def.Name == TEXT("GameTooltip"))
	{
		const FFrameRect Parent = GetFrameRect(TEXT("UIParent"));
		FWowAnchor Fixed;
		Fixed.Point = EWowAnchorPoint::TOPLEFT;
		Fixed.RelativeTo = TEXT("UIParent");
		Fixed.RelativePoint = EWowAnchorPoint::TOPLEFT;
		Fixed.OffsetX = FMath::Max(4.f, Parent.W * .15f);
		Fixed.OffsetY = -FMath::Max(4.f, Parent.H * .12f);
		SetFrameAnchors(TooltipHandle, { Fixed });
		SetFrameStrata(TooltipHandle, EWowFrameStrata::TOOLTIP);
		SetFrameLevel(TooltipHandle, 900);
		return;
	}
	// Placed next to a frame (SetOwner ANCHOR_<side>, or ANCHOR_NONE + SetPoint to a
	// button): keep that placement when the tooltip resizes.
	if (!TooltipEntry->Def.Anchors.IsEmpty() && !TooltipEntry->Def.Anchors[0].RelativeTo.IsEmpty()
		&& TooltipEntry->Def.Anchors[0].RelativeTo != TEXT("UIParent"))
	{
		return;
	}

	const FFrameRect UIParentRect = GetFrameRect(TEXT("UIParent"));
	// Interface option "Tooltip follows mouse pointer": top-left corner just below-right
	// of the pointer, flipped to the other side near the screen edges.
	if (FWowClientPreferences::GetWorldOption(TEXT("tooltipatcursor")) && RootCanvas.IsValid() && UIScale > 0.f
		&& FSlateApplication::IsInitialized())
	{
		const FVector2D Cursor = RootCanvas->GetCachedGeometry().AbsoluteToLocal(WowUiCursorAbsolute()) / UIScale;
		const float W = TooltipEntry->Def.Width, H = TooltipEntry->Def.Height;
		float X = Cursor.X + 18.f, Y = Cursor.Y + 22.f;
		if (X + W > UIParentRect.W - 4.f) X = Cursor.X - W - 8.f;
		if (Y + H > UIParentRect.H - 4.f) Y = Cursor.Y - H - 8.f;
		FWowAnchor CursorAnchor;
		CursorAnchor.Point = EWowAnchorPoint::TOPLEFT;
		CursorAnchor.RelativeTo = TEXT("UIParent");
		CursorAnchor.RelativePoint = EWowAnchorPoint::TOPLEFT;
		CursorAnchor.OffsetX = FMath::Max(4.f, X);
		CursorAnchor.OffsetY = -FMath::Max(4.f, Y);
		SetFrameAnchors(TooltipHandle, { CursorAnchor });
		SetFrameStrata(TooltipHandle, EWowFrameStrata::TOOLTIP);
		SetFrameLevel(TooltipHandle, 900);
		return;
	}
	float RightInset = 14.0f;
	for (int32 ContainerIndex = 1; ContainerIndex <= 13; ++ContainerIndex)
	{
		const FString ContainerName = FString::Printf(TEXT("ContainerFrame%d"), ContainerIndex);
		const int64 ContainerHandle = FindFrame(ContainerName);
		if (ContainerHandle < 0 || !IsFrameActuallyVisible(ContainerHandle))
		{
			continue;
		}

		const FFrameRect ContainerRect = GetFrameRect(ContainerName);
		if (ContainerRect.X + ContainerRect.W >= UIParentRect.W * 0.55f)
		{
			RightInset = FMath::Max(RightInset, UIParentRect.W - ContainerRect.X + 10.0f);
		}
	}

	FWowAnchor Anchor;
	Anchor.Point = EWowAnchorPoint::BOTTOMRIGHT;
	Anchor.RelativeTo = TEXT("UIParent");
	Anchor.RelativePoint = EWowAnchorPoint::BOTTOMRIGHT;
    // Keep the complete measured tooltip inside the viewport when moved bags
    // push it left or a long description makes it taller than the usual inset.
    const float MaxRightInset = FMath::Max(8.f, UIParentRect.W - TooltipEntry->Def.Width - 8.f);
    const float MaxBottomInset = FMath::Max(8.f, UIParentRect.H - TooltipEntry->Def.Height - 8.f);
    Anchor.OffsetX = -FMath::Clamp(RightInset, 8.f, MaxRightInset);
    Anchor.OffsetY = FMath::Clamp(102.f, 8.f, MaxBottomInset);
	TArray<FWowAnchor> Anchors;
	Anchors.Add(Anchor);
	SetFrameAnchors(TooltipHandle, Anchors);
	SetFrameStrata(TooltipHandle, EWowFrameStrata::TOOLTIP);
	SetFrameLevel(TooltipHandle, 900);
}

void FWowFrameManager::EnsureTooltipPresentation(int64 TooltipHandle)
{
	const FWowFrameDef* TooltipDef = GetFrameDef(TooltipHandle);
	UCanvasPanel* TooltipCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(TooltipHandle));
	if (!TooltipDef || !TooltipCanvas)
	{
		return;
	}

	const FString BackdropName = TooltipDef->Name + TEXT("NativeBackdrop");
	UImage* NativeBackdrop = GetNamedTexture(BackdropName);
	if (!NativeBackdrop)
	{
		NativeBackdrop = NewObject<UImage>(TooltipCanvas);
		NativeBackdrop->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		TooltipCanvas->AddChildToCanvas(NativeBackdrop);
		NamedTextureWidgets.Add(BackdropName, NativeBackdrop);
	}

	// The chat window's black, half-transparent background (Stefan: no grey anywhere; the
	// rounded box with its grey outline drew as a raw grey plate, worst on the VR panels).
	NativeBackdrop->SetBrush(FSlateColorBrush(FWowWindowStyle::TooltipBackgroundColor()));
	if (UCanvasPanelSlot* BackdropSlot = Cast<UCanvasPanelSlot>(NativeBackdrop->Slot))
	{
		BackdropSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		BackdropSlot->SetOffsets(FMargin(-2.0f * UIScale));
		BackdropSlot->SetZOrder(-50);
	}

	// Replace the gray FrameXML backdrop while retaining all text and item content.
	for (int32 ChildIndex = 0; ChildIndex < TooltipCanvas->GetChildrenCount(); ++ChildIndex)
	{
        if(auto* Border=Cast<UBorder>(TooltipCanvas->GetChildAt(ChildIndex))) Border->SetRenderOpacity(0);
        if(auto* Backdrop=Cast<UWowBackdropWidget>(TooltipCanvas->GetChildAt(ChildIndex))) Backdrop->SetRenderOpacity(0);
		UImage* Image = Cast<UImage>(TooltipCanvas->GetChildAt(ChildIndex));
		UCanvasPanelSlot* ImageSlot = Image ? Cast<UCanvasPanelSlot>(Image->Slot) : nullptr;
		if (Image && Image != NativeBackdrop && ImageSlot && ImageSlot->GetZOrder() <= 0)
		{
			Image->SetRenderOpacity(0.0f);
		}
	}

	PositionTooltipAtScreenCorner(TooltipHandle);
}

void FWowFrameManager::TooltipAddLine(int64 TooltipHandle, const FString& Text, float R, float G, float B, bool bWrapText)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	FTooltipLine NewLine;
	NewLine.Text = Text;
	NewLine.Color = WowColor::FromWow(R, G, B, 1.0f); // WoW colours are sRGB
	NewLine.bWrapText = bWrapText;
	State->Lines.Add(NewLine);

	TooltipUpdateDisplay(TooltipHandle);
}

void FWowFrameManager::TooltipAddDoubleLine(int64 TooltipHandle, const FString& LeftText, const FString& RightText,
											 float LR, float LG, float LB, float RR, float RG, float RB)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	// Stored in Lines so the double line keeps its position (3.3.5 shares one line list).
	FTooltipLine NewLine;
	NewLine.Text = LeftText;
	NewLine.Color = WowColor::FromWow(LR, LG, LB, 1.0f); // WoW colours are sRGB
	NewLine.RightText = RightText;
	NewLine.RightColor = WowColor::FromWow(RR, RG, RB, 1.0f);
	State->Lines.Add(NewLine);

	TooltipUpdateDisplay(TooltipHandle);
}

void FWowFrameManager::TooltipClearLines(int64 TooltipHandle)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	State->ItemEntry = 0;
	State->bEquippedItem = false;
	State->HeaderLine = 0;
	State->Lines.Empty();
	State->DoubleLines.Empty();
	TooltipUpdateDisplay(TooltipHandle);
	HideShoppingTooltips(TooltipHandle);
}

int32 FWowFrameManager::TooltipNumLines(int64 TooltipHandle) const
{
	const FTooltipState* State = GetTooltipState(TooltipHandle);
	return State ? State->GetTotalLines() : 0;
}

void FWowFrameManager::TooltipSetText(int64 TooltipHandle, const FString& Text)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	// SetText replaces line 1 or creates it
	State->ItemEntry = 0;
	State->Lines.Empty();
	State->DoubleLines.Empty();

	FTooltipLine NewLine;
	NewLine.Text = Text;
	NewLine.Color = FLinearColor::White;
	State->Lines.Add(NewLine);

	TooltipUpdateDisplay(TooltipHandle);
}

void FWowFrameManager::TooltipAppendText(int64 TooltipHandle, const FString& Text)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	if (State->Lines.Num() > 0)
	{
		// r406: GameTooltip:AppendText adds to the FIRST line, as in the client - a bag button's
		// key "(F11)" ended up behind "Sell Price".
		State->Lines[0].Text += Text;
	}
	else
	{
		// Create new line if none exist
		FTooltipLine NewLine;
		NewLine.Text = Text;
		NewLine.Color = FLinearColor::White;
		State->Lines.Add(NewLine);
	}

	TooltipUpdateDisplay(TooltipHandle);
}

// UTextBlock has one colour. A tooltip line with differently coloured runs inside it
// ("Level 13 Draenei |cffabd473Hunter|r (Player)") is drawn as one sibling text block per run,
// each in its colour at its measured offset. r412 (Stefan 17.10: "Death Knight" in red had white
// edges): the runs used to be drawn on top of the whole white line, whose glyph edges showed
// around the coloured letters; the base line now stays for layout only and is drawn transparent.
// Empty Source hides the runs and makes the base line visible again.
static void UpdateTooltipColorRun(UTextBlock* Base, const FString& Source, const FLinearColor& LineColor)
{
	static TMap<TWeakObjectPtr<UTextBlock>, TArray<TWeakObjectPtr<UTextBlock>>> Runs;
	TArray<TWeakObjectPtr<UTextBlock>>& Blocks = Runs.FindOrAdd(Base);
	TArray<FWowChatTextSegment> Segments;
	FString Plain;
	if (!Source.IsEmpty() && Source.Contains(TEXT("|c"))) ParseWowChatText(Source, LineColor, Plain, Segments);
	bool bColoured = false;
	if (Segments.Num() > 1)
		for (const FWowChatTextSegment& Segment : Segments)
			if (!Segment.Color.Equals(LineColor, 0.01f)) { bColoured = true; break; }
	UCanvasPanel* Canvas = Cast<UCanvasPanel>(Base->GetParent());
	UCanvasPanelSlot* BaseSlot = Cast<UCanvasPanelSlot>(Base->Slot);
	auto HideFrom = [&Blocks](int32 First)
	{
		for (int32 I = First; I < Blocks.Num(); ++I)
			if (UTextBlock* Block = Blocks[I].Get()) Block->SetVisibility(ESlateVisibility::Collapsed);
	};
	if (!bColoured || !Canvas || !BaseSlot || !FSlateApplication::IsInitialized())
	{
		HideFrom(0);
		const FLinearColor Current = Base->GetColorAndOpacity().GetSpecifiedColor();
		if (Current.A <= 0.f) Base->SetColorAndOpacity(FSlateColor(FLinearColor(Current.R, Current.G, Current.B, 1.f)));
		return;
	}
	const FSlateFontInfo Font = Base->GetFont();
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float BaseWidth = Measure->Measure(Plain, Font).X;
	const FVector2D Pos = BaseSlot->GetPosition();
	const float Left = Pos.X - BaseSlot->GetAlignment().X * BaseWidth;
	FString Prefix;
	int32 Used = 0;
	for (const FWowChatTextSegment& Segment : Segments)
	{
		if (Segment.Text.IsEmpty()) continue;
		const float Offset = Measure->Measure(Prefix, Font).X;
		Prefix += Segment.Text;
		if (!Blocks.IsValidIndex(Used) || !Blocks[Used].IsValid())
		{
			UTextBlock* Created = NewObject<UTextBlock>(Canvas);
			Canvas->AddChildToCanvas(Created);
			if (Blocks.IsValidIndex(Used)) Blocks[Used] = Created; else Blocks.Add(Created);
		}
		UTextBlock* Run = Blocks[Used].Get();
		++Used;
		Run->SetFont(Font);
		Run->SetText(FText::FromString(Segment.Text));
		Run->SetColorAndOpacity(FSlateColor(Segment.Color));
		Run->SetShadowOffset(FVector2D(1.0f));
		Run->SetShadowColorAndOpacity(FLinearColor::Black);
		if (UCanvasPanelSlot* RunSlot = Cast<UCanvasPanelSlot>(Run->Slot))
		{
			RunSlot->SetAnchors(BaseSlot->GetAnchors());
			RunSlot->SetAlignment(FVector2D(0.f, BaseSlot->GetAlignment().Y));
			RunSlot->SetAutoSize(true);
			RunSlot->SetZOrder(BaseSlot->GetZOrder() + 1);
			RunSlot->SetPosition(FVector2D(Left + Offset, Pos.Y));
		}
		Run->SetVisibility(Base->GetVisibility() == ESlateVisibility::Collapsed ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	HideFrom(Used);
	// The base line keeps its text for the tooltip's size and wrapping, but draws nothing.
	const FLinearColor BaseColor = Base->GetColorAndOpacity().GetSpecifiedColor();
	Base->SetColorAndOpacity(FSlateColor(FLinearColor(BaseColor.R, BaseColor.G, BaseColor.B, 0.f)));
	Base->SetShadowColorAndOpacity(FLinearColor::Transparent);
}

void FWowFrameManager::TooltipUpdateDisplay(int64 TooltipHandle)
{
	const FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	const FWowFrameDef* TooltipDef = GetFrameDef(TooltipHandle);
	if (!TooltipDef) return;

	EnsureTooltipPresentation(TooltipHandle);

	// Line font strings are named after the tooltip (GameTooltipTextLeft1,
	// ShoppingTooltip1TextLeft1, ...). The templates only author the first few; more are
	// created on demand like GameTooltip:AddLine does in the client.
	const FString TextPrefix = TooltipDef->Name + TEXT("Text");
	for (int32 I = 1; I <= State->GetTotalLines(); ++I)
	{
		EnsureTooltipText(TooltipHandle, TextPrefix, false, I);
		EnsureTooltipText(TooltipHandle, TextPrefix, true, I);
	}

	// Hide coloured runs left from the previous contents; regular lines below re-show theirs.
	for (int32 I = 1; I <= 40; ++I)
		if (UTextBlock* Base = GetNamedFontString(FString::Printf(TEXT("%sLeft%d"), *TextPrefix, I)))
			UpdateTooltipColorRun(Base, FString(), FLinearColor::White);

	// Update FontString widgets with tooltip lines
	int32 LineIndex = 0;

	// Process regular lines
	for (const FTooltipLine& Line : State->Lines)
	{
		LineIndex++;

		// Find the FontString widget for this line
		FString LeftFontStringName = FString::Printf(TEXT("%sLeft%d"), *TextPrefix, LineIndex);
		UTextBlock* LeftTextBlock = GetNamedFontString(LeftFontStringName);

		if (LeftTextBlock)
		{
			FString LeftText; FLinearColor LeftColor;
			ResolveTooltipLineMarkup(Line.Text, Line.Color, LeftText, LeftColor);
			LeftTextBlock->SetText(FText::FromString(LeftText));
			LeftTextBlock->SetColorAndOpacity(FSlateColor(LeftColor));
			LeftTextBlock->SetAutoWrapText(Line.bWrapText);
			LeftTextBlock->SetWrapTextAt(Line.bWrapText ? 330.0f * UIScale : 0.0f);
			LeftTextBlock->SetShadowOffset(FVector2D(1.0f));
			LeftTextBlock->SetShadowColorAndOpacity(FLinearColor::Black);
			LeftTextBlock->SetVisibility(ESlateVisibility::Visible);
			UpdateTooltipColorRun(LeftTextBlock, Line.bWrapText ? FString() : Line.Text, Line.Color);
		}
		if (UTextBlock* RightTextBlock = GetNamedFontString(FString::Printf(TEXT("%sRight%d"), *TextPrefix, LineIndex)))
		{
			if (Line.RightText.IsEmpty())
			{
				RightTextBlock->SetVisibility(ESlateVisibility::Collapsed);
			}
			else
			{
				FString RightText; FLinearColor RightColor;
				ResolveTooltipLineMarkup(Line.RightText, Line.RightColor, RightText, RightColor);
				RightTextBlock->SetText(FText::FromString(RightText));
				RightTextBlock->SetColorAndOpacity(FSlateColor(RightColor));
				RightTextBlock->SetShadowOffset(FVector2D(1.0f));
				RightTextBlock->SetShadowColorAndOpacity(FLinearColor::Black);
				RightTextBlock->SetVisibility(ESlateVisibility::Visible);
			}
		}
	}

	// Process double lines
	for (const FTooltipDoubleLine& DoubleLine : State->DoubleLines)
	{
		LineIndex++;

		// Left side
		FString LeftFontStringName = FString::Printf(TEXT("%sLeft%d"), *TextPrefix, LineIndex);
		UTextBlock* LeftTextBlock = GetNamedFontString(LeftFontStringName);
		if (LeftTextBlock)
		{
			LeftTextBlock->SetText(FText::FromString(DoubleLine.LeftText));
			LeftTextBlock->SetColorAndOpacity(FSlateColor(DoubleLine.LeftColor));
			LeftTextBlock->SetVisibility(ESlateVisibility::Visible);
		}

		// Right side
		FString RightFontStringName = FString::Printf(TEXT("%sRight%d"), *TextPrefix, LineIndex);
		UTextBlock* RightTextBlock = GetNamedFontString(RightFontStringName);
		if (RightTextBlock)
		{
			RightTextBlock->SetText(FText::FromString(DoubleLine.RightText));
			RightTextBlock->SetColorAndOpacity(FSlateColor(DoubleLine.RightColor));
			RightTextBlock->SetVisibility(ESlateVisibility::Visible);
		}
	}

	// Hide unused FontString widgets
	for (int32 HideIndex = LineIndex + 1; HideIndex <= 30; HideIndex++) // WoW usually has up to 30 tooltip lines
	{
		FString LeftFontStringName = FString::Printf(TEXT("%sLeft%d"), *TextPrefix, HideIndex);
		UTextBlock* LeftTextBlock = GetNamedFontString(LeftFontStringName);
		if (LeftTextBlock)
		{
			LeftTextBlock->SetVisibility(ESlateVisibility::Collapsed);
		}

		FString RightFontStringName = FString::Printf(TEXT("%sRight%d"), *TextPrefix, HideIndex);
		UTextBlock* RightTextBlock = GetNamedFontString(RightFontStringName);
		if (RightTextBlock)
		{
			RightTextBlock->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

    // Original GameTooltip layout: 10 px padding, 2 px line spacing, header line in
    // GameTooltipHeaderText (14), the rest in GameTooltipText (12), and the frame
    // shrinks to the widest line. Long lines wrap at a readable width.
    // Slate font sizes are points at 96 DPI; WoW font heights are pixels (x 0.75).
    const float S = UIScale * FWowClientPreferences::GetTooltipScale();
    const float Pad = 10.f * S, Gap = 2.f * S, DoubleGap = 20.f * S;
    const float MaxText = 300.f * S;
    auto Prepare = [&](UTextBlock* Text, int32 LineIndex)->FVector2D
    {
        if(!Text) return FVector2D::ZeroVector;
        FSlateFontInfo Font = Text->GetFont();
        // Spell tooltips: the spell name 2 sizes above the header font (Stefan 10.10: the name did not
        // stand out from the cost/range lines).
        const float HeaderSize = State->SpellId > 0 ? 16.f : 14.f;
        Font.Size = FMath::Max(6, FMath::RoundToInt((LineIndex == State->HeaderLine ? HeaderSize : (LineIndex < State->HeaderLine ? 10.f : 12.f)) * 0.75f * S));
        Text->SetFont(Font);
        Text->SetShadowOffset(FVector2D(FMath::Max(1.f, S)));
        Text->SetAutoWrapText(false); Text->SetWrapTextAt(0.f);
        auto Slate=Text->TakeWidget(); Slate->SlatePrepass();
        return Slate->GetDesiredSize();
    };
    auto Place = [&](UTextBlock* Text, float X, float LineY, float W)->float
    {
        if(!Text) return 0.f;
        // Wrap only a line that is really longer than the tooltip, and give the slot a little room:
        // the widest line was wrapped at exactly its own measured width, and drawn at the VR
        // window's pixel scale it came out a hair wider, wrapped and overlapped the next line
        // (Stefan 09.10: "Pygmy Surf / Crawler" over "Level 5 Beast").
        Text->SetWrapTextAt(0.f);
        auto Slate=Text->TakeWidget(); Slate->SlatePrepass();
        const bool bWrap = Slate->GetDesiredSize().X > W + .5f;
        if (bWrap) { Text->SetWrapTextAt(W); Slate->SlatePrepass(); }
        const FVector2D Desired=Slate->GetDesiredSize();
        const float H=float(Desired.Y);
        if(auto* Slot=Cast<UCanvasPanelSlot>(Text->Slot))
        {
            Slot->SetAnchors(FAnchors(0,0)); Slot->SetAlignment(FVector2D::ZeroVector);
            Slot->SetAutoSize(false); Slot->SetPosition(FVector2D(X,LineY)); Slot->SetSize(FVector2D(FMath::Max(W, float(Desired.X)) + 2.f * S, H));
        }
        return H;
    };
    const int32 TotalLines = State->GetTotalLines();
    float ContentWidth = FMath::Max(0.f, State->MinimumWidth * S - 2.f * Pad);
    for(int32 I=0; I<TotalLines; ++I)
    {
        auto* Left=GetNamedFontString(FString::Printf(TEXT("%sLeft%d"),*TextPrefix,I+1));
        auto* Right=GetNamedFontString(FString::Printf(TEXT("%sRight%d"),*TextPrefix,I+1));
        if(I<State->Lines.Num() && State->Lines[I].RightText.IsEmpty())
        {
            if(Right) Right->SetVisibility(ESlateVisibility::Collapsed);
            ContentWidth=FMath::Max(ContentWidth, FMath::Min(MaxText, Prepare(Left,I).X));
        }
        else
        {
            if(Right) Right->SetJustification(ETextJustify::Right);
            ContentWidth=FMath::Max(ContentWidth, Prepare(Left,I).X + DoubleGap + Prepare(Right,I).X);
        }
    }
    ContentWidth = FMath::Clamp(ContentWidth, 40.f * S, 2.f * MaxText);
    float Y = Pad;
    for(int32 I=0; I<TotalLines; ++I)
    {
        auto* Left=GetNamedFontString(FString::Printf(TEXT("%sLeft%d"),*TextPrefix,I+1));
        auto* Right=GetNamedFontString(FString::Printf(TEXT("%sRight%d"),*TextPrefix,I+1));
        float Height=0;
        if(I<State->Lines.Num() && State->Lines[I].RightText.IsEmpty())
        {
            Height=Place(Left,Pad,Y,ContentWidth);
        }
        else
        {
            const float RightW = Right ? Right->TakeWidget()->GetDesiredSize().X : 0.f;
            const float LeftW = FMath::Max(10.f, ContentWidth - RightW - DoubleGap);
            Height=FMath::Max(Place(Left,Pad,Y,LeftW), Place(Right,Pad+ContentWidth-RightW,Y,RightW));
        }
        Y+=Height+(I+1<TotalLines?Gap:0.f);
    }
    const float Width = (ContentWidth + 2.f * Pad + 2.f * S) / UIScale; // + the slack Place gives each line
    // A few pixels of slack under the last line: descenders and the text shadow were cut by the
    // frame edge (Stefan: "Humanoid" cut off at the bottom).
    SetFrameSize(TooltipHandle,FMath::CeilToFloat(Width),FMath::CeilToFloat((Y+Pad+3.f*S)/UIScale));

	PositionTooltipAtScreenCorner(TooltipHandle);
}

void FWowFrameManager::TooltipSetItem(int64 TooltipHandle, const FString& ItemLink)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	State->ItemLink = ItemLink;
	State->SpellId = 0; // Clear other contexts
	State->UnitToken.Empty();

	uint32 ItemEntry = 0;
	int32 ItemMarker = ItemLink.Find(TEXT("item:"), ESearchCase::IgnoreCase);
	if (ItemMarker != INDEX_NONE)
	{
		ItemEntry = FCString::Strtoui64(*ItemLink.Mid(ItemMarker + 5), nullptr, 10);
	}
	else if (ItemLink.IsNumeric())
	{
		ItemEntry = FCString::Strtoui64(*ItemLink, nullptr, 10);
	}
	TooltipSetItemDetails(TooltipHandle, ItemEntry, ItemLink, 1, FString());
}

void FWowFrameManager::TooltipSetItemDetails(
	int64 TooltipHandle,
	uint32 ItemEntry,
	const FString& ItemLink,
	int32 StackCount,
	const FString& ContextText,
	int32 Durability,
	int32 MaxDurability,
	bool bEquipped,
	uint64 ItemGuid)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	State->ItemLink = ItemLink;
	State->SpellId = 0;
	State->UnitToken.Empty();
	State->Lines.Empty();
	State->DoubleLines.Empty();
	State->HeaderLine = 0;
	State->bEquippedItem = bEquipped;

    State->ItemEntry = ItemEntry; State->ItemStack = StackCount; State->ItemGuid = ItemGuid;
    State->ItemDurability = Durability; State->ItemMaxDurability = MaxDurability; State->ItemContext = ContextText;
    const FWowItemEntity* Instance = ItemGuid && ItemConnection.IsValid() ? ItemConnection->PacketHandler.EntityManager.FindItem(ItemGuid) : nullptr;
    for (const FWowItemTooltipLine& ItemLine : FWowItemPresentation::Lines(ItemConnection.Get(), ItemEntry,
        MaxDurability > 0 ? Durability : -1, MaxDurability > 0 ? MaxDurability : -1, Instance))
    {
        FTooltipLine Line; Line.Text = ItemLine.Text; Line.Color = ItemLine.Color; Line.bWrapText = ItemLine.Right.IsEmpty();
        Line.RightText = ItemLine.Right; Line.RightColor = ItemLine.RightColor;
        State->Lines.Add(Line);
    }

	if (!ContextText.IsEmpty())
	{
		FTooltipLine ContextLine;
		ContextLine.Text = ContextText;
		ContextLine.Color = WowColor::FromWow(0.72f, 0.72f, 0.72f, 1.0f);
		State->Lines.Add(ContextLine);
	}
	// Durability is part of FWowItemPresentation::Lines (in its 3.3.5 position).

	TooltipUpdateDisplay(TooltipHandle);

	// The client fires OnTooltipSetItem once the item is on the tooltip; GameTooltip's
	// handler shows the ShoppingTooltip comparison (IsModifiedClick("COMPAREITEMS") or
	// the alwaysCompareItems cvar).
	static bool bInTooltipSetItem = false;
	if (EventSystem && !bInTooltipSetItem && ItemEntry)
	{
		TGuardValue<bool> Guard(bInTooltipSetItem, true);
		EventSystem->RunFrameScript(TooltipHandle, TEXT("OnTooltipSetItem"));
	}
}

void FWowFrameManager::TooltipSetSpell(int64 TooltipHandle, int32 SpellId)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	State->SpellId = SpellId;
	State->ItemLink.Empty(); // Clear other contexts
	State->UnitToken.Empty();

    TooltipClearLines(TooltipHandle);
    // 3.3.5 colours (Stefan 10.10: the whole tooltip was gold): name white with the rank in grey
    // on the right, cost | range and cast time | cooldown white, the description gold, red problems.
    const FWowSpellTooltipParts Parts = FWowSpellPresentation::TooltipParts(ItemConnection.Get(), SpellId);
    if (!Parts.bValid) { TooltipAddLine(TooltipHandle, Parts.Name, 1.0f, 1.0f, 1.0f, false); return; }
    if (Parts.Rank.IsEmpty()) TooltipAddLine(TooltipHandle, Parts.Name, 1.0f, 1.0f, 1.0f, false);
    else TooltipAddDoubleLine(TooltipHandle, Parts.Name, Parts.Rank, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f);
    if (!Parts.Cost.IsEmpty() || !Parts.Range.IsEmpty())
    {
        if (Parts.Range.IsEmpty()) TooltipAddLine(TooltipHandle, Parts.Cost, 1.0f, 1.0f, 1.0f, false);
        else TooltipAddDoubleLine(TooltipHandle, Parts.Cost, Parts.Range, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    }
    if (Parts.Cooldown.IsEmpty()) TooltipAddLine(TooltipHandle, Parts.Cast, 1.0f, 1.0f, 1.0f, false);
    else TooltipAddDoubleLine(TooltipHandle, Parts.Cast, Parts.Cooldown, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    if (!Parts.Description.IsEmpty()) TooltipAddLine(TooltipHandle, Parts.Description, 1.0f, 0.82f, 0.0f, true);
    if (!Parts.Reason.IsEmpty()) TooltipAddLine(TooltipHandle, Parts.Reason, 1.0f, 0.125f, 0.125f, true);
}

void FWowFrameManager::TooltipSetUnit(int64 TooltipHandle, const FString& UnitToken)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	State->UnitToken = UnitToken;
	State->ItemLink.Empty(); // Clear other contexts
	State->SpellId = 0;

	// TODO: Lookup unit data and populate tooltip
	// For now, just add a placeholder line
	TooltipClearLines(TooltipHandle);
	TooltipAddLine(TooltipHandle, FString::Printf(TEXT("Unit: %s"), *UnitToken), 1.0f, 1.0f, 1.0f, false);
}

FTooltipState* FWowFrameManager::GetTooltipState(int64 TooltipHandle)
{
	// Ensure the tooltip state exists
	if (!TooltipStates.Contains(TooltipHandle))
	{
		TooltipStates.Add(TooltipHandle, FTooltipState());
	}
	return &TooltipStates[TooltipHandle];
}

const FTooltipState* FWowFrameManager::GetTooltipState(int64 TooltipHandle) const
{
	return TooltipStates.Find(TooltipHandle);
}

void FWowFrameManager::HideShoppingTooltips(int64 TooltipHandle)
{
	// GameTooltip OnHide / a new owner: its comparison tooltips go with it.
	const FFrameEntry* Entry = Frames.Find(TooltipHandle);
	if (!Entry || Entry->Def.Name != TEXT("GameTooltip")) return;
	for (const TCHAR* Name : { TEXT("ShoppingTooltip1"), TEXT("ShoppingTooltip2"), TEXT("ShoppingTooltip3") })
	{
		const int64 Shopping = FindFrame(Name);
		if (Shopping >= 0 && IsFrameVisible(Shopping)) SetFrameVisible(Shopping, false);
	}
}

UTextBlock* FWowFrameManager::EnsureTooltipText(int64 TooltipHandle, const FString& Prefix, bool bRight, int32 Index)
{
	const FString Name = FString::Printf(TEXT("%s%s%d"), *Prefix, bRight ? TEXT("Right") : TEXT("Left"), Index);
	if (UTextBlock* Found = GetNamedFontString(Name)) return Found;
	UCanvasPanel* Canvas = Cast<UCanvasPanel>(GetWidgetForHandle(TooltipHandle));
	if (!Canvas) return nullptr;
	// Copy the body line's font (TextLeft2 is GameTooltipText in both templates).
	UTextBlock* Model = GetNamedFontString(Prefix + (bRight ? TEXT("Right2") : TEXT("Left2")));
	if (!Model) Model = GetNamedFontString(Prefix + TEXT("Left1"));
	UTextBlock* Text = NewObject<UTextBlock>(Canvas);
	if (Model)
	{
		Text->SetFont(Model->GetFont());
		Text->SetJustification(bRight ? ETextJustify::Right : ETextJustify::Left); // UTextBlock has no GetJustification()
	}
	Text->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Text))
	{
		Slot->SetAnchors(FAnchors(0.f, 0.f));
		Slot->SetAutoSize(true);
		Slot->SetZOrder(260); // ARTWORK, like the authored lines
	}
	NamedFontStringWidgets.Add(Name, Text);
	return Text;
}

namespace
{
	uint32 ParseTooltipItemEntry(const FString& ItemLink)
	{
		const int32 Marker = ItemLink.Find(TEXT("item:"), ESearchCase::IgnoreCase);
		if (Marker != INDEX_NONE) return static_cast<uint32>(FCString::Strtoui64(*ItemLink.Mid(Marker + 5), nullptr, 10));
		return ItemLink.IsNumeric() ? static_cast<uint32>(FCString::Strtoui64(*ItemLink, nullptr, 10)) : 0u;
	}
}

TArray<uint32> FWowFrameManager::GetEquippedForCompare(uint32 ItemEntry) const
{
	TArray<uint32> Result;
	if (!ItemConnection.IsValid() || !ItemEntry) return Result;
	const FWowItemTemplateInfo* Info = FWowItemPresentation::Info(ItemConnection.Get(), ItemEntry);
	auto& Entities = ItemConnection->PacketHandler.EntityManager;
	const FWowPlayerEntity* Player = Entities.GetLocalPlayer();
	if (!Info || !Player) return Result;
	for (uint8 Slot : FWowItemPresentation::CompareSlots(Info->InventoryType))
		if (const uint64 Guid = Player->GetEquipmentItemGuid(Slot))
			if (const FWowItemEntity* Item = Entities.FindItem(Guid))
				if (Item->Entry) Result.Add(Item->Entry);
	return Result;
}

bool FWowFrameManager::TooltipSetCompareItem(int64 TooltipHandle, const FString& ItemLink, int32 Index)
{
	// ShoppingTooltipN:SetHyperlinkCompareItem(link, N): "Currently Equipped", the
	// equipped item in the hovered item's slot(s), then the stat changes.
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return false;
	const uint32 NewEntry = ParseTooltipItemEntry(ItemLink);
	const TArray<uint32> Equipped = GetEquippedForCompare(NewEntry);
	if (!Equipped.IsValidIndex(Index - 1)) return false;
	const uint32 EquippedEntry = Equipped[Index - 1];
	State->Lines.Empty();
	State->DoubleLines.Empty();
	State->SpellId = 0;
	State->UnitToken.Empty();
	State->ItemEntry = EquippedEntry;
	State->ItemLink = FString::Printf(TEXT("item:%u:0:0:0:0:0:0:0:0"), EquippedEntry);
	State->HeaderLine = 1;
	for (const FWowItemTooltipLine& ItemLine : FWowItemPresentation::CompareLines(ItemConnection.Get(), NewEntry, EquippedEntry))
	{
		FTooltipLine Line; Line.Text = ItemLine.Text; Line.Color = ItemLine.Color; Line.bWrapText = ItemLine.Right.IsEmpty();
		Line.RightText = ItemLine.Right; Line.RightColor = ItemLine.RightColor;
		State->Lines.Add(Line);
	}
	TooltipUpdateDisplay(TooltipHandle);
	return true;
}

bool FWowFrameManager::IsTooltipItemEquipped(int64 TooltipHandle) const
{
	const FTooltipState* State = GetTooltipState(TooltipHandle);
	return State && State->ItemEntry && State->bEquippedItem;
}

void FWowFrameManager::RefreshInterfaceScale()
{
    // A live re-anchor only moved the root frames by their anchor delta; everything laid out by
    // Lua or by our hand layout against the old UIParent size stayed put (Stefan 09.10: buffs in
    // a column mid-screen, chat too high, after a UI Scale change). Rebuild the interface at the
    // new scale instead, once the slider has rested for a moment - like a /reload.
    static double ScaleChangedAt = 0.0;
    static float PendingScale = 0.f;
    const float NewScale = FWowClientPreferences::GetUIScale();
    static double ReloadRequestedAt = 0.0;
    // Live preview while the slider moves (Stefan 09.10: only the quest tracker changed live):
    // every frame placed on UIParent is drawn scaled about the UIParent point it is anchored to,
    // as the real rescale will place it; the reload then lays everything out exactly.
    {
        static TMap<TWeakObjectPtr<UWidget>, FWidgetTransform> PreviewOriginal;
        static float PreviewFactor = 1.f;
        const float Factor = RootCanvas.IsValid() && UserInterfaceScale > 0.f ? NewScale / UserInterfaceScale : 1.f;
        // Keep the preview up until the reload has rebuilt the widgets (no flash of the old size).
        const bool bReloading = FPlatformTime::Seconds() - ReloadRequestedAt < 3.0;
        if (!bReloading && !FMath::IsNearlyEqual(Factor, PreviewFactor, 1e-3f))
        {
            PreviewFactor = Factor;
            for (auto& Pair : PreviewOriginal)
                if (UWidget* W = Pair.Key.Get()) W->SetRenderTransform(Pair.Value);
            PreviewOriginal.Reset();
            float PL = 0, PB = 0, PR = 0, PT = 0;
            const int64 ParentHandle = FindFrame(TEXT("UIParent"));
            if (!FMath::IsNearlyEqual(Factor, 1.f, 1e-3f) && ParentHandle >= 0
                && GetFrameScreenRect(ParentHandle, PL, PB, PR, PT) && PR - PL > 1.f && PT - PB > 1.f)
            {
                const FVector2D ParentSize(PR - PL, PT - PB); // UIParent units
                auto PointOf = [&ParentSize](EWowAnchorPoint P)
                {
                    const int32 I = static_cast<int32>(P);
                    return FVector2D(ParentSize.X * (I % 3) * .5f, ParentSize.Y * (I / 3) * .5f); // from the top-left, y down
                };
                for (const auto& Pair : Frames)
                {
                    const FFrameEntry& Entry = Pair.Value;
                    UWidget* Widget = Entry.Widget.Get();
                    if (!Widget || Pair.Key == ParentHandle || (Entry.ParentHandle >= 0 && Entry.ParentHandle != ParentHandle)) continue;
                    // UIParent's own children (most of the HUD) count too, not only root-canvas frames.
                    const UWidget* PreviewParentWidget = GetWidgetForHandle(ParentHandle);
                    if ((Widget->GetParent() != RootCanvas.Get() && Widget->GetParent() != PreviewParentWidget) || !Widget->IsVisible()) continue;
                    // Bars, unit frames and minimap: ApplyElementScales folds this factor in itself.
                    if (ElementScaledWidgets.Contains(Widget)) continue;
                    float L = 0, B = 0, R = 0, T = 0;
                    if (!GetFrameScreenRect(Pair.Key, L, B, R, T) || R - L < 1.f || T - B < 1.f) continue;
                    const FVector2D Pos(L - PL, PT - T), Size(R - L, T - B); // UIParent units, y down
                    // The UIParent point it hangs from: its anchor's, or for frames anchored to other
                    // frames the nearest of the nine UIParent points to its own centre.
                    FVector2D C;
                    const FWowAnchor* Anchor = Entry.Def.Anchors.IsEmpty() ? nullptr : &Entry.Def.Anchors[0];
                    if (Anchor && (Anchor->RelativeTo.IsEmpty() || Anchor->RelativeTo == TEXT("UIParent")))
                        C = PointOf(Anchor->RelativePoint);
                    else
                    {
                        const FVector2D Centre = Pos + Size * .5f;
                        const int32 Col = FMath::Clamp(FMath::RoundToInt(Centre.X / ParentSize.X * 2.f), 0, 2);
                        const int32 Row = FMath::Clamp(FMath::RoundToInt(Centre.Y / ParentSize.Y * 2.f), 0, 2);
                        C = PointOf(static_cast<EWowAnchorPoint>(Row * 3 + Col));
                    }
                    // Keep the widget's own render transform (main bar 0.9 about its bottom centre ...)
                    // and add the preview scale about C: same pivot, scale * Factor, and a translation
                    // that moves the pivot point as scaling about C would (canvas units).
                    const FWidgetTransform Original = Widget->GetRenderTransform();
                    PreviewOriginal.Add(Widget, Original);
                    const FVector2D Q = Pos + Size * Widget->GetRenderTransformPivot();
                    FWidgetTransform Preview = Original;
                    Preview.Scale = Original.Scale * Factor;
                    Preview.Translation = Original.Translation * Factor + (Q - C) * (Factor - 1.f) * UIScale;
                    Widget->SetRenderTransform(Preview);
                }
            }
        }
    }
    if (FMath::IsNearlyEqual(NewScale, UserInterfaceScale) || !RootCanvas.IsValid()) { ScaleChangedAt = 0.0; return; }
    const double Now = FPlatformTime::Seconds();
    if (ScaleChangedAt <= 0.0 || !FMath::IsNearlyEqual(NewScale, PendingScale)) { ScaleChangedAt = Now; PendingScale = NewScale; return; }
    if (Now - ScaleChangedAt < 0.8) return;
    if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPressedMouseButtons().Num() > 0) return; // still dragging
    UE_LOG(LogWowFrame, Log, TEXT("UI Scale %.2f -> %.2f: reloading the interface"), UserInterfaceScale, NewScale);
    UserInterfaceScale = NewScale; // asked once; the reloaded frame manager reads the new scale
    ScaleChangedAt = 0.0;
    ReloadRequestedAt = FPlatformTime::Seconds();
    UWowUIManager::RequestReloadUI();
}

void FWowFrameManager::ApplyElementScales()
{
    // Stefan 10.10: "huge player and target frames, but smaller bars and the rest of the UI,
    // maybe a bigger minimap". Each group is drawn with a render transform about its screen
    // anchor, so its layout (slots, Lua anchors, hit tests through cached geometry) is untouched
    // and the options apply live. VR keeps the presenter's own placement.
    if (!RootCanvas.IsValid() || UIScale <= 0.f || FWowUiVrHost::IsActive()) return;
    const FFrameRect ScreenRect = GetFrameRect(TEXT("UIParent"));
    const FVector2D Root(ScreenRect.W * UIScale, ScreenRect.H * UIScale);
    if (Root.X < 1.f || Root.Y < 1.f) return;
    // UI Scale slider preview until the reload lays everything out at the new scale.
    const float LiveInterfaceScale = FWowClientPreferences::GetUIScale();
    const float Preview = LaidOutInterfaceScale > 0.f && FMath::IsFinite(LiveInterfaceScale) ? LiveInterfaceScale / LaidOutInterfaceScale : 1.f;
    ElementScaledWidgets.Reset();
    // Stefan 11.10: Action Bar Size, Minimap Size and Unit Frame Size on the player frame did
    // nothing. PlayerFrame, MainMenuBar, MinimapCluster, the bars and the party frames are children
    // of UIParent's canvas, not of the root canvas (only TargetFrame and FocusFrame are), and were
    // skipped. Both parents are accepted; UIParent's canvas is unscaled, so positions convert to
    // root units by adding its slot position.
    UWidget* UIParentWidget = GetWidgetForHandle(FindFrame(TEXT("UIParent")));
    FVector2D UIParentOrigin = FVector2D::ZeroVector;
    if (UIParentWidget)
        if (const UCanvasPanelSlot* ParentSlot = Cast<UCanvasPanelSlot>(UIParentWidget->Slot))
            UIParentOrigin = ParentSlot->GetPosition();
    auto RootSlotOf = [this, UIParentWidget](const TCHAR* Name, UWidget*& OutWidget) -> UCanvasPanelSlot*
    {
        OutWidget = GetWidgetForHandle(FindFrame(Name));
        if (!OutWidget) return nullptr;
        const UPanelWidget* Parent = OutWidget->GetParent();
        if (Parent != RootCanvas.Get() && (!UIParentWidget || Parent != UIParentWidget)) return nullptr;
        return Cast<UCanvasPanelSlot>(OutWidget->Slot);
    };
    // Draw the widget at Scale, its top-left moved as scaling the layout by PositionScale about the
    // canvas point From (which lands on To) would move it. The unit frames' slots already carry their
    // base size, so only the option factor moves them; the bar group's slots are unscaled.
    // Unit frames and the minimap cluster can be Ctrl-dragged (WowFrameMovement.cpp, same list).
    auto IsDragPlacedElement = [](const TCHAR* ElementName)
    {
        static const TSet<FString> DragPlaced = { TEXT("PlayerFrame"), TEXT("TargetFrame"), TEXT("FocusFrame"),
            TEXT("PartyMemberFrame1"), TEXT("PartyMemberFrame2"), TEXT("PartyMemberFrame3"), TEXT("PartyMemberFrame4"),
            TEXT("MinimapCluster") };
        return DragPlaced.Contains(ElementName);
    };
    auto Place = [this, &RootSlotOf, &Root, &IsDragPlacedElement, UIParentWidget, UIParentOrigin](const TCHAR* Name, float Scale, float PositionScale, const FVector2D& From, const FVector2D& To)
    {
        UWidget* Widget = nullptr;
        UCanvasPanelSlot* Slot = RootSlotOf(Name, Widget);
        if (!Slot) return;
        ElementScaledWidgets.Add(Widget);
        const FVector2D Position = Slot->GetPosition() + (Widget->GetParent() == UIParentWidget ? UIParentOrigin : FVector2D::ZeroVector);
        FVector2D Translation = To + (Position - From) * PositionScale - Position;
        // Ctrl-dragged (WowFrameMovement): its top-left goes where it was dropped, at any size.
        if (IsDragPlacedElement(Name))
            if (const FVector2D* Dropped = FWowFrameLayout::Positions().Find(Name))
                Translation = FWowFrameLayout::ClampPosition(*Dropped, Slot->GetSize() * Scale, Root) - Position;
        if (!Widget->GetRenderTransformPivot().Equals(FVector2D::ZeroVector, 1e-4f))
            Widget->SetRenderTransformPivot(FVector2D::ZeroVector);
        const FWidgetTransform& Current = Widget->GetRenderTransform();
        if (!Current.Translation.Equals(Translation, 0.05f) || !Current.Scale.Equals(FVector2D(Scale), 1e-4f)
            || !Current.Shear.IsNearlyZero() || !FMath::IsNearlyZero(Current.Angle))
            Widget->SetRenderTransform(FWidgetTransform(Translation, FVector2D(Scale), FVector2D::ZeroVector, 0.f));
    };

    // Unit frames grow from the top-left corner of the screen.
    const float UnitScale = FWowClientPreferences::GetUnitFrameScale() * Preview;
    for (const TCHAR* Name : {TEXT("PlayerFrame"), TEXT("TargetFrame"), TEXT("FocusFrame")})
        Place(Name, UnitFrameBaseScale * UnitScale, UnitScale, FVector2D::ZeroVector, FVector2D::ZeroVector);
    for (const TCHAR* Name : {TEXT("PartyMemberFrame1"), TEXT("PartyMemberFrame2"), TEXT("PartyMemberFrame3"), TEXT("PartyMemberFrame4")})
        Place(Name, UnitScale, UnitScale, FVector2D::ZeroVector, FVector2D::ZeroVector);

    // Minimap from the top-right corner.
    const FVector2D TopRight(Root.X, 0.f);
    const float MinimapScale = FWowClientPreferences::GetMinimapScale() * Preview;
    Place(TEXT("MinimapCluster"), MinimapScale, MinimapScale, TopRight, TopRight);

    // Bars: one group about the main bar's bottom centre, which is put on the screen's centre
    // line (the bars were off centre at every scale), shrunk if the main bar with its gryphons
    // would not fit the width (Stefan: slots off the right edge at 120 %).
    UWidget* MainWidget = nullptr;
    if (UCanvasPanelSlot* MainSlot = RootSlotOf(TEXT("MainMenuBar"), MainWidget))
    {
        const FVector2D MainPosition = MainSlot->GetPosition() + (MainWidget->GetParent() == UIParentWidget ? UIParentOrigin : FVector2D::ZeroVector);
        const FVector2D MainSize = MainSlot->GetSize();
        float BarScale = ActionBarBaseScale * FWowClientPreferences::GetActionBarScale() * Preview;
        if (MainSize.X > 1.f) BarScale = FMath::Min(BarScale, Root.X * 0.98f / (MainSize.X * 1.12f));
        const FVector2D From(MainPosition.X + MainSize.X * 0.5f, MainPosition.Y + MainSize.Y);
        const FVector2D To(Root.X * 0.5f, Root.Y + (From.Y - Root.Y) * Preview);
        for (const TCHAR* Name : {TEXT("MainMenuBar"), TEXT("MultiBarBottomLeft"), TEXT("MultiBarBottomRight"),
            TEXT("ShapeshiftBarFrame"), TEXT("PetActionBarFrame"), TEXT("PossessBarFrame"),
            TEXT("MultiCastActionBarFrame"), TEXT("MainMenuBarVehicleLeaveButton"), TEXT("CastingBarFrame")})
            Place(Name, BarScale, BarScale, From, To);
        // The two right columns from the bottom-right corner, same size as the bars.
        const FVector2D BottomRight(Root.X, Root.Y);
        Place(TEXT("MultiBarRight"), BarScale, BarScale, BottomRight, BottomRight);
        Place(TEXT("MultiBarLeft"), BarScale, BarScale, BottomRight, BottomRight);
    }
}

void FWowFrameManager::LayoutTemplateTabs(const TCHAR* ParentName, int32 NumTabs, float FirstX, float BottomOffset)
{
    // Stefan 10.10, PvP window: no tab captions, and the Battlegrounds tab lay under the PvP tab.
    // Like PanelTemplates_TabResize(tab, 0): width = caption + both 20-unit end caps; tab N+1 starts
    // 15 units into tab N (CharacterFrameTabButtonTemplate anchors).
    const int64 ParentHandle = FindFrame(ParentName);
    if (ParentHandle < 0 || !IsFrameVisible(ParentHandle) || TemplateTabsDone.Contains(ParentName) || UIScale <= 0.f) return;
    const FFrameRect* ParentRect = FrameRects.Find(ParentName);
    if (!ParentRect || ParentRect->W <= 0.f || !FSlateApplication::IsInitialized()) return;
    TemplateTabsDone.Add(ParentName);
    const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    float LocalX = FirstX;
    const float LocalY = ParentRect->H - BottomOffset - 32.0f;
    for (int32 TabIndex = 1; TabIndex <= NumTabs; ++TabIndex)
    {
        const FString TabName = FString::Printf(TEXT("%sTab%d"), ParentName, TabIndex);
        const int64 TabHandle = FindFrame(TabName);
        FFrameEntry* TabEntry = Frames.Find(TabHandle);
        UCanvasPanel* TabCanvas = Cast<UCanvasPanel>(GetWidgetForHandle(TabHandle));
        if (!TabEntry || !TabCanvas) continue;
        const FString Caption = ResolveXmlText(TabEntry->Def.ButtonText);
        FSlateFontInfo CaptionFont = FontManager && FontManager->IsInitialized()
            ? FontManager->GetFont(TEXT("GameFontNormalSmall"), FMath::RoundToInt(10.0f * UIScale)) : FCoreStyle::GetDefaultFontStyle("Regular", FMath::RoundToInt(10.0f * UIScale));
        const float CaptionWidth = float(Measure->Measure(Caption, CaptionFont).X) / UIScale;
        const float TabWidth = FMath::Max(64.0f, CaptionWidth + 40.0f);
        TabEntry->Def.HitRectInsets = FMargin(0);
        TabEntry->Def.Width = TabWidth;
        TabEntry->Def.Height = 32.0f;
        if (UCanvasPanelSlot* TabSlot = Cast<UCanvasPanelSlot>(TabCanvas->Slot))
        {
            const bool bRootOwned = TabCanvas->GetParent() == RootCanvas.Get();
            TabSlot->SetAnchors(FAnchors(0.0f, 0.0f));
            TabSlot->SetAlignment(FVector2D::ZeroVector);
            TabSlot->SetAutoSize(false);
            TabSlot->SetPosition((bRootOwned ? FVector2D(ParentRect->X + LocalX, ParentRect->Y + LocalY) : FVector2D(LocalX, LocalY)) * UIScale);
            TabSlot->SetSize(FVector2D(TabWidth, 32.0f) * UIScale);
        }
        FrameRects.Add(TabName, FFrameRect(ParentRect->X + LocalX, ParentRect->Y + LocalY, TabWidth, 32.0f));
        auto LayoutSlice = [this](const FString& TextureName, float X, float Y, float Width)
        {
            if (UImage* Slice = GetNamedTexture(TextureName))
                if (UCanvasPanelSlot* SliceSlot = Cast<UCanvasPanelSlot>(Slice->Slot))
                {
                    SliceSlot->SetAnchors(FAnchors(0.0f, 0.0f));
                    SliceSlot->SetAlignment(FVector2D::ZeroVector);
                    SliceSlot->SetAutoSize(false);
                    SliceSlot->SetPosition(FVector2D(X, Y) * UIScale);
                    SliceSlot->SetSize(FVector2D(Width, 32.0f) * UIScale);
                }
        };
        LayoutSlice(TabName + TEXT("LeftDisabled"), 0.0f, 0.0f, 20.0f);
        LayoutSlice(TabName + TEXT("MiddleDisabled"), 20.0f, 0.0f, FMath::Max(1.0f, TabWidth - 40.0f));
        LayoutSlice(TabName + TEXT("RightDisabled"), TabWidth - 20.0f, 0.0f, 20.0f);
        LayoutSlice(TabName + TEXT("Left"), 0.0f, 5.0f, 20.0f);
        LayoutSlice(TabName + TEXT("Middle"), 20.0f, 5.0f, FMath::Max(1.0f, TabWidth - 40.0f));
        LayoutSlice(TabName + TEXT("Right"), TabWidth - 20.0f, 5.0f, 20.0f);
        // Caption: the tab's own ButtonText, centred over the tab (ButtonText at 0,2 in the template).
        UTextBlock* CaptionLabel = GetPrimaryTextWidget(TabHandle);
        if (!CaptionLabel) CaptionLabel = GetNamedFontString(TabName + TEXT("Text"));
        if (CaptionLabel)
        {
            if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(CaptionLabel->Slot))
            {
                LabelSlot->SetAnchors(FAnchors(0.0f, 0.0f));
                LabelSlot->SetAlignment(FVector2D::ZeroVector);
                LabelSlot->SetAutoSize(false);
                LabelSlot->SetPosition(FVector2D(4.0f, 8.0f) * UIScale);
                LabelSlot->SetSize(FVector2D(TabWidth - 8.0f, 18.0f) * UIScale);
                LabelSlot->SetZOrder(250);
            }
            if (CaptionLabel->GetText().IsEmpty()) CaptionLabel->SetText(FText::FromString(Caption));
            CaptionLabel->SetFont(CaptionFont);
            CaptionLabel->SetJustification(ETextJustify::Center);
            CaptionLabel->SetColorAndOpacity(FSlateColor(WowColor::FromWow(1.0f, 0.82f, 0.0f)));
            CaptionLabel->SetShadowOffset(FVector2D(1.0f));
            CaptionLabel->SetShadowColorAndOpacity(FLinearColor::Black);
            CaptionLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        }
        SetFrameMouseEnabled(TabHandle, true);
        LocalX += TabWidth - 15.0f;
    }
    UE_LOG(LogWowFrame, Log, TEXT("Template tabs laid out for %s (%d)"), ParentName, NumTabs);
}

void FWowFrameManager::FitUnitNameLabels()
{
    // Stefan 10.10: long names ("Eye of the Storm Emissary") ran far past the name plate. A name
    // wider than the plate wraps to two lines at a slightly smaller size inside a taller box;
    // checked every tick because the text changes with the target.
    if (!FSlateApplication::IsInitialized() || UIScale <= 0.f) return;
    const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    // Stefan 11.10: the player's name too; names a size smaller with a black outline; centred both
    // ways in the dark name field. A name that does not fit wraps the rest onto a second line in a
    // smaller size.
    struct FNamePlate { const TCHAR* Key; const TCHAR* Label; float X, FieldTop, FieldH, Size; };
    static const FNamePlate Plates[] = {
        {TEXT("PlayerFrame"), TEXT("PlayerName"), 106.0f, 25.0f, 17.0f, 7.0f},
        {TEXT("TargetFrame"), TEXT("TargetFrameTextureFrameName"), 7.0f, 22.0f, 19.0f, 5.25f},
        {TEXT("FocusFrame"), TEXT("FocusFrameTextureFrameName"), 7.0f, 22.0f, 19.0f, 5.25f},
    };
    for (const FNamePlate& Plate : Plates)
    {
        UTextBlock* NameLabel = GetNamedFontString(Plate.Label);
        if (!NameLabel) continue;
        const FString NameText = NameLabel->GetText().ToString();
        if (const FString* Done = FittedUnitNames.Find(Plate.Key); Done && *Done == NameText) continue;
        FittedUnitNames.Add(Plate.Key, NameText);
        FSlateFontInfo NameFont = NameLabel->GetFont();
        NameFont.Size = FMath::RoundToInt(Plate.Size * UIScale / UnitFrameBaseScale);
        NameFont.OutlineSettings.OutlineSize = 1;
        NameFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
        const float PlateW = 115.0f * UIScale;
        const bool bWrap = float(Measure->Measure(NameText, NameFont).X) > PlateW;
        if (bWrap) NameFont.Size = FMath::Max(1, FMath::RoundToInt(NameFont.Size * 0.8f));
        NameLabel->SetFont(NameFont);
        NameLabel->SetAutoWrapText(bWrap);
        NameLabel->SetWrapTextAt(bWrap ? PlateW : 0.f);
        NameLabel->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        NameLabel->SetJustification(ETextJustify::Center);
        if (UCanvasPanelSlot* NameSlot = Cast<UCanvasPanelSlot>(NameLabel->Slot))
        {
            // The slot covers the whole name field; the text is moved down to its middle below.
            NameSlot->SetAutoSize(false);
            NameSlot->SetPosition(FVector2D(Plate.X, Plate.FieldTop) * UIScale);
            NameSlot->SetSize(FVector2D(119.0f, Plate.FieldH) * UIScale);
        }
        if (bWrap)
        {
            // Two lines: the block's middle on the field's middle.
            const float LineH = float(Measure->GetMaxCharacterHeight(NameFont));
            NameLabel->SetRenderTranslation(FVector2D(0.f, FMath::RoundToFloat((Plate.FieldH * UIScale - 2.f * LineH) * .5f)));
        }
        else CentreUnitFrameText(NameLabel, 0.0f, Plate.FieldH, 1, UIScale);
    }
}


void FWowFrameManager::UpdateOptionalActionBars()
{
    RefreshMainActionBarControls();
    RefreshActionButtonState();
    const TCHAR* Bars[]={TEXT("MultiBarBottomLeft"),TEXT("MultiBarBottomRight"),TEXT("MultiBarRight"),TEXT("MultiBarLeft")};
    int32 Mask=0;
    for(int32 I=0;I<4;++I) if(FWowClientPreferences::GetWorldOption(FString::Printf(TEXT("actionbar%d"),I+3))) Mask|=1<<I;
    // Match stock FrameXML: Right 2 requires Right, without discarding its saved choice.
    if (!(Mask & (1 << 2))) Mask &= ~(1 << 3);
    // r229 (Stefan 11.10, Bartender4): an action bar addon hides the stock bars by moving them under
    // a hidden frame of its own (Bartender4, Dominos, ElvUI). Such a bar is the addon's: no showing
    // it again, no compact layout - our empty slots stayed on screen beside Bartender's bars.
    const int64 UIParentHandle = FindFrame(TEXT("UIParent"));
    int32 AddonOwned = 0;
    for (int32 I = 0; I < 4; ++I)
    {
        const int64 Bar = FindFrame(Bars[I]);
        if (Bar >= 0 && UIParentHandle >= 0 && GetParentHandle(Bar) != UIParentHandle) AddonOwned |= 1 << I;
    }
    // r239 (Stefan 12.10, Bartender4 4.4.2): it keeps the stock bars under UIParent and only hides
    // their buttons and unregisters their events (Bartender4:HideBlizzard). Our reconcile below
    // showed those buttons again as empty black slots on the right and under Bartender's bars.
    AddonOwned |= ScriptReleasedActionBars;
    Mask &= ~AddonOwned;
    // Temporary drop targets while dragging something that can go on a bar: spells,
    // macros and usable items. A held weapon or armour piece does not open the bars.
    bool bDraggingActionable = false;
    if (ItemConnection.IsValid() && ItemConnection->HasCursorPayload())
    {
        bDraggingActionable = true;
        if (ItemConnection->HasCursorItemPayload())
        {
            FString CursorType, CursorDetail;
            int32 CursorId = 0;
            bDraggingActionable = false;
            if (ItemConnection->GetCursorInfo(CursorType, CursorId, CursorDetail) && CursorId > 0)
            {
                // Usable (on-use spell) or openable items only.
                const uint32 Entry = static_cast<uint32>(CursorId);
                const FWowItemTemplateInfo* Info = ItemConnection->PacketHandler.ItemTemplates.Find(Entry);
                bDraggingActionable = ItemConnection->GetItemUseSpell(Entry) != 0 || (Info && (Info->Flags & 0x4u) != 0);
            }
        }
    }
    const bool bShowEmpty = FWowClientPreferences::GetWorldOption(TEXT("alwaysshowactionbars")) || bDraggingActionable;
    const int32 FirstSlots[] = {60, 48, 24, 36}; // Zero-based server action slots.
    auto Reconcile = [this](int64 Handle, bool bShown)
    {
        if (Handle < 0) return;
        const bool bOwnStateChanged = IsFrameVisible(Handle) != bShown;
        if (bOwnStateChanged || IsFrameActuallyVisible(Handle) != IsFrameEffectivelyShown(Handle))
            SetFrameVisible(Handle, bShown);
    };
    // Reconcile actual state, not only preference changes: Lua/load callbacks can hide bars later.
    for (int32 I = 0; I < UE_ARRAY_COUNT(Bars); ++I)
    {
        if (AddonOwned & (1 << I)) continue; // r229
        const bool bEnabled = (Mask & (1 << I)) != 0;
        Reconcile(FindFrame(Bars[I]), bEnabled);
        for (int32 Button = 1; Button <= 12; ++Button)
        {
            const int32 Slot = FirstSlots[I] + Button - 1;
            const bool bHasAction = ItemConnection.IsValid()
                && ItemConnection->PacketHandler.ActionButtons.IsValidIndex(Slot)
                && ItemConnection->PacketHandler.ActionButtons[Slot] != 0;
            Reconcile(FindFrame(FString::Printf(TEXT("%sButton%d"), Bars[I], Button)),
                bEnabled && (bShowEmpty || bHasAction));
        }
    }
    auto SlotFor=[this](const TCHAR* Name)->UCanvasPanelSlot*
    { UWidget* Widget=GetWidgetForHandle(FindFrame(Name)); return Widget?Cast<UCanvasPanelSlot>(Widget->Slot):nullptr; };
    auto* Main=SlotFor(TEXT("MainMenuBar")); auto* CastBar=SlotFor(TEXT("CastingBarFrame"));
    if(Main && CastBar)
    {
        float Top=Main->GetPosition().Y;
        for(int32 I=0;I<2;++I) if(Mask&(1<<I)) if(auto* Slot=SlotFor(Bars[I])) Top=FMath::Min(Top,Slot->GetPosition().Y);
        FVector2D Position=CastBar->GetPosition(); Position.Y=Top-CastBar->GetSize().Y-30.0f*UIScale;
        CastBar->SetPosition(Position);
    }
    // Stock Lua (MultiActionBar_Update / UIParent_ManageFramePositions) re-anchors the bars after
    // our one-time compact layout: the two right columns drifted apart and bar 4's buttons lost
    // their size (Stefan 10.10). Re-assert the compact layout; slots are written only on change.
    {
        const float U=UIScale;
        auto Put=[](UCanvasPanelSlot* Slot,const FVector2D& Position,const FVector2D& Size)
        {
            if(!Slot) return;
            if(!Slot->GetPosition().Equals(Position,0.01f)) Slot->SetPosition(Position);
            if(!Slot->GetSize().Equals(Size,0.01f)) { Slot->SetAutoSize(false); Slot->SetSize(Size); }
        };
        for(int32 I=0;I<4;++I)
        {
            if(AddonOwned&(1<<I)) continue; // r229
            const bool bVertical=I>=2;
            for(int32 Button=1;Button<=12;++Button)
                Put(SlotFor(*FString::Printf(TEXT("%sButton%d"),Bars[I],Button)),
                    bVertical?FVector2D(2.f*U,(Button-1)*37.f*U):FVector2D((Button-1)*39.f*U,2.f*U),FVector2D(36.f*U));
        }
        // The 24-slot upper strip stays centred over the main bar, 45 units above it.
        if(auto* BottomLeft=SlotFor(Bars[0]); BottomLeft && Main)
        {
            const FVector2D StripSize=BottomLeft->GetSize();
            const FVector2D StripPosition(Main->GetPosition().X+Main->GetSize().X*.5f-(StripSize.X*2.f+3.f*U)*.5f,Main->GetPosition().Y-45.f*U);
            Put(BottomLeft,StripPosition,StripSize);
            if(auto* BottomRight=SlotFor(Bars[1])) Put(BottomRight,StripPosition+FVector2D(StripSize.X+3.f*U,0.f),BottomRight->GetSize());
        }
        auto* Right=SlotFor(TEXT("MultiBarRight")); auto* Left=SlotFor(TEXT("MultiBarLeft"));
        if(Right)
        {
            const FVector2D ColumnSize(40.f*U,(11.f*37.f+36.f+2.f)*U);
            // Bottom just above the two upper horizontal bars (which sit 45 units over the main bar).
            const float Bottom=Main?Main->GetPosition().Y-51.f*U:Right->GetPosition().Y+ColumnSize.Y;
            // Keep the column's right edge where the stock anchor put it while its width changes.
            const float RightEdge=Right->GetPosition().X+Right->GetSize().X;
            const FVector2D RightPosition(RightEdge-ColumnSize.X,FMath::Max(8.f*U,Bottom-ColumnSize.Y));
            Put(Right,RightPosition,ColumnSize);
            Put(Left,FVector2D(RightPosition.X-ColumnSize.X-U,RightPosition.Y),ColumnSize);
        }
    }
}

void FWowFrameManager::SetItemConnection(UWowConnectionManager* Connection) { ItemConnection=Connection; }

void FWowFrameManager::RefreshActionButtonState()
{
    if(!ItemConnection.IsValid() || FPlatformTime::Seconds()-LastActionStateUpdate<0.2) return;
    LastActionStateUpdate=FPlatformTime::Seconds();
    const auto& Packed=ItemConnection->PacketHandler.ActionButtons;
    for(const auto& Action:FWowKeyBindings::Actions())
    {
        const int32 Slot=Action.ActionSlot; if(Slot<0 || (Slot>=12 && Slot<24)) continue;
        FString Frame;
        if(Slot<12) Frame=FString::Printf(TEXT("ActionButton%d"),Slot+1);
        else if(Slot<36) Frame=FString::Printf(TEXT("MultiBarRightButton%d"),Slot-23);
        else if(Slot<48) Frame=FString::Printf(TEXT("MultiBarLeftButton%d"),Slot-35);
        else if(Slot<60) Frame=FString::Printf(TEXT("MultiBarBottomRightButton%d"),Slot-47);
        else Frame=FString::Printf(TEXT("MultiBarBottomLeftButton%d"),Slot-59);
        const int32 DataSlot=Slot<12?Slot+12*(FWowKeyBindings::ActionPage()-1):Slot;
        if(auto* Icon=GetNamedTexture(Frame+TEXT("NativeActionIcon")); Icon && Packed.IsValidIndex(DataSlot))
        {
            const uint32 Data=Packed[DataSlot];
            auto State=(Data>>24)==0 ? FWowSpellPresentation::Usability(ItemConnection.Get(),Data&0xffffff):FWowSpellUsability{true,false,FString()};
            // r265 (Stefan 12.10, r05t07): an item with none left greys out, like IsUsableAction
            // (the native icon tinted every non-spell action as usable).
            if((Data>>24)==128)
                State.Usable=ItemConnection->CountPlayerItems(Data&0xffffff)>0 || ItemConnection->IsItemEntryEquipped(Data&0xffffff);
            const bool OutOfRange=(Data>>24)==0 && FWowSpellPresentation::InRange(ItemConnection.Get(),Data&0xffffff)==0;
            ActionIconTargets.Add(Icon,ItemConnection->PendingTargetSpell==(Data&0xffffff) && ItemConnection->PendingTargetSpell ? WowColor::FromWow(.25f, .25f, .25f):OutOfRange?WowColor::FromWow(.24f, .08f, .08f):State.Usable?FLinearColor::White:State.NoResource?WowColor::FromWow(.35f, .4f, .9f):WowColor::FromWow(.35f, .35f, .35f));
        }
        const uint32 PackedAction=Packed.IsValidIndex(DataSlot)?Packed[DataSlot]:0;
        const auto Cooldown=(PackedAction && (PackedAction>>24)==0)
            ? ItemConnection->PacketHandler.GetVisibleSpellCooldown(PackedAction&0xffffff)
            : FWowPacketHandler::FCooldownWindow{};
        SetCooldown(FindFrame(Frame),Cooldown.Start,Cooldown.Duration);
        if(auto* Text=GetNamedFontString(Frame+TEXT("NativeHotKey")))
        {
            const auto Keys=FWowKeyBindings::Keys(Action.Command);
            FString Key;for(const auto& Candidate:Keys)if(!Candidate.IsEmpty()){Key=Candidate;break;}
            Key.ReplaceInline(TEXT("CTRL-"),TEXT("C-")); Key.ReplaceInline(TEXT("SHIFT-"),TEXT("S-")); Key.ReplaceInline(TEXT("ALT-"),TEXT("A-"));
            Text->SetText(FText::FromString(Key));
        }
        if(auto* Original=GetNamedFontString(Frame+TEXT("HotKey"))) Original->SetVisibility(ESlateVisibility::Collapsed);
        // r377 (Stefan 16.10: the macro name showed twice on the action bar): ActionButton_Update now
        // fills the stock Name/Count strings too (GetActionText, GetActionCount), so they are hidden
        // like HotKey; the button's own labels below draw them.
        if(auto* Original=GetNamedFontString(Frame+TEXT("Name"))) Original->SetVisibility(ESlateVisibility::Collapsed);
        if(auto* Original=GetNamedFontString(Frame+TEXT("Count"))) Original->SetVisibility(ESlateVisibility::Collapsed);
        // Item counts and macro names (Stefan 11.10: a stack of potions on the bar showed no count).
        // The stock Count/Name strings sit under the native icon, so the button draws its own, like
        // ActionButton_UpdateCount (consumable or stackable items) and the macro name under it.
        if(UCanvasPanel* ButtonCanvas=Cast<UCanvasPanel>(GetWidgetForHandle(FindFrame(Frame))))
        {
            auto NativeLabel=[&](const TCHAR* Suffix, bool bCount)->UTextBlock*
            {
                const FString LabelName=Frame+Suffix;
                UTextBlock* Label=GetNamedFontString(LabelName);
                if(!Label)
                {
                    Label=NewObject<UTextBlock>(ButtonCanvas);
                    if(UCanvasPanelSlot* LabelSlot=ButtonCanvas->AddChildToCanvas(Label))
                    {
                        // Along the bottom edge: stretched across, 14 units high, 1 unit up from the bottom.
                        LabelSlot->SetAnchors(FAnchors(0.f,1.f,1.f,1.f));
                        LabelSlot->SetAlignment(FVector2D(0.f,1.f));
                        LabelSlot->SetOffsets(FMargin(2.f*UIScale,-1.f*UIScale,2.f*UIScale,14.f*UIScale));
                        LabelSlot->SetZOrder(91);
                    }
                    FSlateFontInfo LabelFont=Label->GetFont();
                    LabelFont.Size=FMath::Max(7,FMath::RoundToInt((bCount?9.f:7.f)*UIScale));
                    LabelFont.OutlineSettings.OutlineSize=1;
                    LabelFont.OutlineSettings.OutlineColor=FLinearColor::Black;
                    Label->SetFont(LabelFont);
                    Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
                    Label->SetJustification(bCount?ETextJustify::Right:ETextJustify::Center);
                    Label->SetVisibility(ESlateVisibility::HitTestInvisible);
                    NamedFontStringWidgets.Add(LabelName,Label);
                }
                return Label;
            };
            FString CountText, NameText;
            const uint8 ActionType=uint8(PackedAction>>24);
            const uint32 ActionId=PackedAction&0x00FFFFFFu;
            if(PackedAction && ActionType==128)
            {
                const FWowItemTemplateInfo* Info=ItemConnection->PacketHandler.ItemTemplates.Find(ActionId);
                if(Info && (Info->Class==0 || Info->MaxStack>1))
                {
                    const int32 Count=ItemConnection->CountPlayerItems(ActionId);
                    CountText=Count>9999?FString(TEXT("*")):FString::FromInt(Count);
                }
            }
            else if(PackedAction && ActionType==64)
            {
                if(const FWowMacro* Macro=FWowMacros::Get(int32(ActionId))) NameText=Macro->Name;
            }
            if(UTextBlock* CountLabel=NativeLabel(TEXT("NativeCount"),true))
            {
                CountLabel->SetText(FText::FromString(CountText));
            }
            if(UTextBlock* NameLabel=NativeLabel(TEXT("NativeMacroName"),false))
            {
                NameLabel->SetText(FText::FromString(NameText));
            }
        }
    }
}


void FWowFrameManager::RefreshMainActionBarControls()
{
    // Main slots remain available even when empty (drop targets). Keep native
    // recovery consistent with the logical visibility used by click routing.
    for (int32 I=1; I<=12; ++I)
    {
        const int64 Handle = FindFrame(FString::Printf(TEXT("ActionButton%d"),I));
        if (const auto* Entry = Frames.Find(Handle); Entry && Entry->Def.bHidden)
            SetFrameVisible(Handle,true);
    }
    for (const TCHAR* Name : {TEXT("ActionBarUpButton"),TEXT("ActionBarDownButton")})
    {
        const int64 Handle = FindFrame(Name);
        if (const auto* Entry=Frames.Find(Handle); Entry && Entry->Def.bHidden) SetFrameVisible(Handle,true);
        SetFrameMouseEnabled(Handle,true);
        SetFrameRegisteredClicks(Handle,{TEXT("LeftButtonUp")});
    }
    const int64 ArtHandle = FindFrame(TEXT("MainMenuBarArtFrame"));
    auto* Art = Cast<UCanvasPanel>(GetWidgetForHandle(ArtHandle));
    if (!Art) return;
    UTextBlock* Label = GetNamedFontString(TEXT("MainMenuBarPageNumber"));
    if (!Label)
    {
        Label = NewObject<UTextBlock>(Art);
        NamedFontStringWidgets.Add(TEXT("MainMenuBarPageNumber"),Label);
        if (EventSystem) EventSystem->CreateTextureRegionGlobal(TEXT("MainMenuBarPageNumber"),ArtHandle,TEXT("FontString"));
    }
    if (Label->GetParent()!=Art)
    {
        Label->RemoveFromParent();
        Art->AddChildToCanvas(Label);
    }
    if (auto* Slot=Cast<UCanvasPanelSlot>(Label->Slot))
    {
        // The XML label used CENTER anchors. A new position without clearing
        // those anchors adds half the bar width and puts the number by the gryphon.
        Slot->SetAnchors(FAnchors(0,0));
        Slot->SetAlignment(FVector2D::ZeroVector);
        Slot->SetAutoSize(false);
        // MainMenuBar.xml: CENTER of MainMenuBarArtFrame +30,-5, centred in the page-number window.
        const FFrameRect ArtRect=GetFrameRect(TEXT("MainMenuBarArtFrame"));
        const FVector2D ArtSize=(ArtRect.W>1&&ArtRect.H>1?FVector2D(ArtRect.W,ArtRect.H):FVector2D(1024,53))*UIScale;
        const FVector2D LabelSize(24*UIScale,14*UIScale);
        Slot->SetPosition(ArtSize*.5f+FVector2D(30*UIScale,5*UIScale)-LabelSize*.5f);
        Slot->SetSize(LabelSize);
        Slot->SetZOrder(2000000);
    }
    Label->SetText(FText::AsNumber(FWowKeyBindings::ActionPage()));
    Label->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Label->SetRenderOpacity(1);
    auto Font=Label->GetFont();Font.Size=FMath::RoundToInt(10*UIScale);Label->SetFont(Font);
    Label->SetJustification(ETextJustify::Center);
    Label->SetColorAndOpacity(FSlateColor(WowColor::FromWow(1, .82f, 0, 1)));
    Label->SetShadowOffset(FVector2D(1));
    Label->SetShadowColorAndOpacity(FLinearColor(0,0,0,.95f));
}

void FWowFrameManager::SetActionBarPage(int32 Page)
{
    FWowKeyBindings::SetActionPage(Page);
    if (EventSystem)
    {
        // r383 (Stefan 16.10: in stealth the page read 7 but bar 1's icons stayed): the stock buttons
        // work out their action from GetActionBarPage() and do not know our bonus paging, so every
        // stock ActionButton_Update (UPDATE_BONUS_ACTIONBAR, slot changes) drew page 1 again over the
        // native icons. Their "actionpage" attribute now carries the page actually shown.
        EventSystem->ExecuteLuaString(FString::Printf(TEXT("CURRENT_ACTIONBAR_PAGE=%d for i=1,12 do local b=_G[\"ActionButton\"..i] if b and b.SetAttribute then b:SetAttribute(\"actionpage\",%d) end end"),
            FWowKeyBindings::ManualActionPage(), FWowKeyBindings::ActionPage()));
        EventSystem->FireEvent(TEXT("ACTIONBAR_PAGE_CHANGED"));
    }
    RefreshMainActionBarControls();
    if (ItemConnection.IsValid()) RefreshActionButtonIcons(ItemConnection->PacketHandler.ActionButtons);
    LastActionStateUpdate=0;
    RefreshActionButtonState();
}

bool FWowFrameManager::TogglePlayerBag(int32 BagId, bool bOpenOnly)
{
    if (!EventSystem || BagId < 0 || BagId > 4) return false;
    const int64 Handle = FindFrame(FString::Printf(TEXT("ContainerFrame%d"), BagId+1));
    if (Handle < 0) return false;
    // Native layout reserves frame 1 for the backpack. Stock ToggleBag chooses
    // the first unused frame, which can assign an equipped bag to that frame.
    // Use the same mapping for buttons and keyboard, while keeping stock slot
    // generation, item IDs, update events and OnShow/OnHide bookkeeping intact.
    const bool bOk = EventSystem->ExecuteLuaString(FString::Printf(TEXT(R"LUA(
        local bag, openOnly = %d, %s
        local frame = _G["ContainerFrame"..(bag+1)]
        local root = ContainerFrame1
        if not frame or not root then return end
        -- Recover from older native close paths that bypassed OnHide.
        root.bags, root.bagsShown = {}, 0
        for i=1,13 do
            local f = _G["ContainerFrame"..i]
            if f and f:IsShown() then
                root.bagsShown = root.bagsShown+1
                root.bags[root.bagsShown] = f:GetName()
            end
        end
        if frame:IsShown() then
            if not openOnly then frame:Hide() end
            return
        end
        local size = GetContainerNumSlots(bag)
        if not size or size <= 0 then return end
        assert(type(ContainerFrame_GenerateFrame)=="function", "Container frame generator unavailable")
        ContainerFrame_GenerateFrame(frame, size, bag)
    )LUA"), BagId, bOpenOnly ? TEXT("true") : TEXT("false")), TEXT("=NativePlayerBag"));
    SanitizeBackpackOnlyContainers();
    return bOk;
}

void FWowFrameManager::ToggleAllPlayerBags()
{
    bool bAnyShown = false;
    for (int32 BagId=0; BagId<=4; ++BagId)
        bAnyShown |= IsFrameVisible(FindFrame(FString::Printf(TEXT("ContainerFrame%d"), BagId+1)));
    for (int32 BagId=0; BagId<=4; ++BagId)
    {
        const int64 Handle=FindFrame(FString::Printf(TEXT("ContainerFrame%d"),BagId+1));
        if (!bAnyShown || IsFrameVisible(Handle)) TogglePlayerBag(BagId, !bAnyShown);
    }
}

void FWowFrameManager::SetRootVisible(bool bVisible)
{
    if (RootCanvas.IsValid()) RootCanvas->SetVisibility(bVisible
        ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

// Z order just above MinimapCluster (strata LOW) for the native minimap overlays
// (frost, clock): above the minimap itself, below every MEDIUM+ window such as bags,
// the character sheet or dialogs - like the real TimeManagerClockButton.
static int32 MinimapOverlayZOrder(const FWowFrameManager& Manager)
{
    const int64 Cluster = Manager.FindFrame(TEXT("MinimapCluster"));
    if (UWidget* Widget = Cluster >= 0 ? Manager.GetWidgetForHandle(Cluster) : nullptr)
        if (const UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
            return Slot->GetZOrder();
    return static_cast<int32>(EWowFrameStrata::LOW) * 1000 + 1;
}

void FWowFrameManager::SetMinimapFrostOverlay(TSharedPtr<SWidget> Widget)
{
    if(!RootCanvas.IsValid()) return;
    if(!Widget.IsValid()) { if(MinimapFrostHost.IsValid()) MinimapFrostHost->RemoveFromParent(); MinimapFrostHost.Reset(); return; }
    if(!MinimapFrostHost.IsValid())
    {
        auto* Host=NewObject<UNativeWidgetHost>(RootCanvas.Get());
        auto* Slot=RootCanvas->AddChildToCanvas(Host);
        Slot->SetAnchors(FAnchors(0,0,1,1)); Slot->SetOffsets(FMargin(0)); Slot->SetZOrder(MinimapOverlayZOrder(*this) + 1);
        Host->SetVisibility(ESlateVisibility::HitTestInvisible); MinimapFrostHost=Host;
    }
    MinimapFrostHost->SetContent(Widget.ToSharedRef());
}
