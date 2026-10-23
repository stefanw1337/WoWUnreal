#pragma once
#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "WowFrameTypes.h"
class UWowBackdropWidget;

class UCanvasPanel;
class UWidget;
class UUserWidget;
class UImage;
class UButton;
class UEditableTextBox;
class UProgressBar;
class USlider;
class UTextBlock;
class FWowEventSystem;

/** Button texture regions that are only shown in one state (SetHighlightTexture(texture) etc.). */
enum class EWowButtonStateRegion : uint8 { Highlight, Pushed, Disabled, Count };
class FWowFontManager;
class FMpqManager;
class FWowAssetCache;
class UTexture2D;
class UTextureRenderTarget2D;
struct FWowPlayerEntity;

DECLARE_DELEGATE_OneParam(FOnSecureActionDispatch, int32 /*ActionSlot (0-based)*/);
DECLARE_DELEGATE_OneParam(FOnSecureSpellDispatch, int32 /*SpellId*/);
DECLARE_DELEGATE_OneParam(FOnMinimapZoomRequest, bool /*bZoomIn*/);
DECLARE_DELEGATE_OneParam(FOnMinimapTrackingChanged, int32 /*TrackingMode*/);

/** Tooltip state structures */
struct FTooltipLine
{
	FString Text;
	FLinearColor Color = FLinearColor::White;
	bool bWrapText = false;
	FString RightText;   // AddDoubleLine right column (restored: lost from the header in the 00:06 commit)
	FLinearColor RightColor = FLinearColor::White;
};

struct FTooltipDoubleLine
{
	FString LeftText;
	FString RightText;
	FLinearColor LeftColor = FLinearColor::White;
	FLinearColor RightColor = FLinearColor::White;
};

struct FTooltipState
{
	TArray<FTooltipLine> Lines;
	TArray<FTooltipDoubleLine> DoubleLines;
	int64 OwnerHandle = -1;
	FString AnchorType = TEXT("ANCHOR_NONE");
	FString ItemLink;
    uint32 ItemEntry = 0;
    uint64 ItemGuid = 0; // item object (random suffix stats); 0 for links and vendor items
    int32 ItemStack = 1, ItemDurability = 0, ItemMaxDurability = 0;
    FString ItemContext;
	int32 SpellId = 0;
	FString UnitToken;
	float MinimumWidth = 0.f;
	/** Line drawn in the header font (ShoppingTooltip: line 2, after "Currently Equipped"). */
	int32 HeaderLine = 0;
	/** The item comes from an equipped slot (tooltip:IsEquippedItem()). */
	bool bEquippedItem = false;

	void Clear()
	{
		Lines.Empty();
		DoubleLines.Empty();
		OwnerHandle = -1;
		AnchorType = TEXT("ANCHOR_NONE");
		ItemLink.Empty();
        ItemEntry = 0;
        ItemGuid = 0;
		SpellId = 0;
		UnitToken.Empty();
		MinimumWidth = 0.f;
		HeaderLine = 0;
		bEquippedItem = false;
	}

	int32 GetTotalLines() const { return Lines.Num() + DoubleLines.Num(); }
};

struct FWowMirrorTimerState
{
	FString Name = TEXT("UNKNOWN");
	FString Label;
	float ValueMs = 0.0f;
	float MaxValueMs = 0.0f;
	float Scale = 0.0f;
	uint32 SpellId = 0;
	bool bActive = false;
	bool bPaused = false;
};

/**
 * Manages WoW UI frames, mapping them to UMG widgets.
 */
class WOWUI_API FWowFrameManager
{
public:
    void SetItemConnection(class UWowConnectionManager* Connection);
	/** Fired when a SecureActionButton with type="action" is clicked. Slot is 0-based. */
	FOnSecureActionDispatch OnSecureActionDispatch;
	/** Fired when a SecureActionButton with type="spell" is clicked. */
	FOnSecureSpellDispatch OnSecureSpellDispatch;
	/** Fired by the stock plus/minus controls around the minimap. */
	FOnMinimapZoomRequest OnMinimapZoomRequest;
	/** Fired after choosing a row in the minimap tracking menu. */
	FOnMinimapTrackingChanged OnMinimapTrackingChanged;
	/** Fired by the Wrath LFG micro button and active queue eye. */
	FSimpleDelegate OnDungeonFinderToggle;
    FSimpleDelegate OnSkillsToggle;
	FWowFrameManager();
    int64 GetMovableFrameAtScreenPosition(float X,float Y) const;
    void UpdateUserFrameLayouts();

	/** Initialize with a root canvas panel to add widgets to */
	void Initialize(UCanvasPanel* RootCanvas);
	UCanvasPanel* GetRootCanvas() const { return RootCanvas.Get(); }
    void SetMinimapFrostOverlay(TSharedPtr<SWidget> Widget);
    void SetRootVisible(bool bVisible);

	/** Set the event system for script compilation on frame creation */
	void SetEventSystem(FWowEventSystem* InEventSystem) { EventSystem = InEventSystem; }

	/** Set the font manager for font rendering */
	void SetFontManager(FWowFontManager* InFontManager) { FontManager = InFontManager; }

	/** Set MPQ manager for loading texture files */
	void SetMpqManager(FMpqManager* InMpqManager) { MpqManager = InMpqManager; }

	/** Set asset cache for texture caching */
	void SetAssetCache(FWowAssetCache* InAssetCache) { AssetCache = InAssetCache; }

	/** Create a frame from a parsed definition */
	int64 CreateFrame(const FWowFrameDef& Def);

	/** Register a template for inheritance */
	void RegisterTemplate(const FString& Name, const FWowFrameDef& Def);

	/** Find a frame by name */
	int64 FindFrame(const FString& Name) const;

	/** Get frame name by handle */
	FString GetFrameName(int64 Handle) const;

	/** Keep the minimap time-of-day icon and tooltip synchronized with the world sky clock. */
	void UpdateMinimapTime(float TimeOfDayMinutes);
	/** Show the stock green LFG eye only while queued, role-checking, or in a proposal. */
	void SetLfgQueueVisible(bool bVisible);
	bool bLfgQueueEyeShown = false; // r263: the minimap queue eye is shown (queued)
	bool IsMinimapTrackingMenuOpen() const { return bMinimapTrackingMenuOpen; }
	int32 GetMinimapTrackingMode() const { return MinimapTrackingMode; }
	int32 GetMinimapDayNightState() const { return LastMinimapDayNightState; }

	/** Backing state for the stock FrameXML Breath/Fatigue/Fire mirror bars. */
	static FString GetMirrorTimerName(int32 TimerId);
	void StartMirrorTimer(int32 TimerId, int32 ValueMs, int32 MaxValueMs, int32 Scale,
		bool bPaused, uint32 SpellId = 0);
	void PauseMirrorTimer(int32 TimerId, bool bPaused);
	void StopMirrorTimer(int32 TimerId);
	void TickMirrorTimers(float DeltaTime);
	void RefreshBagSlotBorders();
	bool GetMirrorTimerInfo(int32 OneBasedIndex, FWowMirrorTimerState& OutState) const;
	bool GetMirrorTimerState(int32 TimerId, FWowMirrorTimerState& OutState) const;
	float GetMirrorTimerProgress(const FString& TimerName) const;

	/** Get the frame definition for a handle (nullptr if not found) */
	const FWowFrameDef* GetFrameDef(int64 Handle) const;

	/** Get mutable frame definition for a handle (nullptr if not found) */
	FWowFrameDef* GetMutableFrameDef(int64 Handle);

	/** Show/hide a frame */
	void SetFrameVisible(int64 Handle, bool bVisible);

	/** Check if a frame is currently visible */
	bool IsFrameVisible(int64 Handle) const;

	/** Check whether the frame's UMG widget is actually being rendered */
	bool IsFrameActuallyVisible(int64 Handle) const;

	/** Set frame size and update widget */
	void SetFrameSize(int64 Handle, float W, float H);

	/** Set frame anchors and reposition widget */
	void SetFrameAnchors(int64 Handle, const TArray<FWowAnchor>& NewAnchors);

	/** r228 (Stefan 11.10: /bt4 options window empty and scattered): the original client re-lays out
	 *  everything anchored to a frame when that frame moves or resizes. Lua-created frames anchored
	 *  to a changed frame (its children, or frames whose points name it) are laid out again once per
	 *  tick, and so is anything anchored to them in turn. Called from TickWidgetEvents. */
	void FlushDependentLayout();
	/** r228: a Lua region (texture/font string) with a point on a frame other than its parent.
	 *  Anchors name that frame in RelativeTo (empty = the parent); W/H 0 = no explicit size. The
	 *  region is placed from the live frame rects and placed again whenever they change. */
	void SetDynamicRegionAnchors(UWidget* Region, int64 ParentHandle, const TArray<FWowAnchor>& Anchors, float W, float H);
	void ClearDynamicRegionAnchors(UWidget* Region);
	/** SetWidth/SetHeight of a dynamic region; false when the region is not one. */
	bool UpdateDynamicRegionSize(UWidget* Region, float W, float H);
	/** Live placement of a frame in UIParent design units (top-left origin) and UIParent's height;
	 *  used for GetPoint on natively placed frames that have no anchors (the chat frames). */
	bool GetFrameLivePlacement(int64 Handle, float& X, float& Y, float& W, float& H, float& ParentHeight) const;
	/** Absolute rect of a frame in WoW UI units, bottom-left origin (frame:GetLeft/GetBottom/
	 *  GetRight/GetTop). False when the frame has no laid-out rect yet. */
	bool GetFrameScreenRect(int64 Handle, float& Left, float& Bottom, float& Right, float& Top);
	/** frame:SetBackdrop(table|nil), SetBackdropColor / SetBackdropBorderColor. */
	void SetFrameBackdrop(int64 Handle, const struct FWowBackdrop* Backdrop);
	void SetFrameBackdropColor(int64 Handle, const FLinearColor& Color, bool bBorder);
	/** frame:SetParent(parent|nil): moves the widget into the new parent's canvas (AceGUI recycles widgets this way). */
	void SetFrameParent(int64 Handle, int64 NewParentHandle);
	/** Explicitly protected (inherits a Secure* template), as frame:IsProtected() reports it. */
	bool IsFrameProtected(int64 Handle) const { return ProtectedFrames.Contains(Handle); }
    void PlaceDockPanel(int64 Handle, FVector2D Position, float Scale);
    bool IsDockPanelChild(int64 Handle) const;
    void SetChatPointerBlocked(bool bBlocked) { bChatPointerBlocked=bBlocked; }

	/** Clear all anchors on a frame */
	void ClearFrameAnchors(int64 Handle);

	/** Set frame alpha */
	void SetFrameAlpha(int64 Handle, float Alpha);

	/** Set frame strata and level */
	void SetFrameStrata(int64 Handle, EWowFrameStrata Strata);
	void SetFrameLevel(int64 Handle, int32 Level);

	/** Get the parent handle of a frame (-1 if none) */
	int64 GetParentHandle(int64 Handle) const;

	/** Get child handles of a frame */
	TArray<int64> GetChildHandles(int64 Handle) const;
	/** r272: widgets the experimental immersive interface has faded out; frames inside them take no mouse (no tooltips, clicks). */
	void SetMouseBlockedWidgets(const TSet<const UWidget*>& Widgets) { MouseBlockedWidgets = Widgets; }
	TSet<const UWidget*> MouseBlockedWidgets;

	/** Get frame count */
	int32 GetFrameCount() const { return Frames.Num(); }

	/** Get the UWidget for a frame handle (nullptr if not found or invalid) */
	UWidget* GetWidgetForHandle(int64 Handle) const;

	/** r380 (Stefan 16.10: "Move to Inactive" sat on top of the "At War" label): true when an XML frame
	 *  is anchored to this named texture or fontstring. Its SetText / SetWidth from Lua then moves
	 *  that frame, as in the client (ReputationDetailInactiveCheckBox follows AtWarCheckBoxText). */
	bool IsFrameAnchorRegion(const FString& RegionName) const { return FrameAnchorRegions.Contains(RegionName); }
	TSet<FString> FrameAnchorRegions;

	/** Resolve a stock action-button frame to its zero-based server slot. */
	int32 GetActionSlotForFrame(int64 Handle) const;
	/** Apply server action assignments directly to all stock action-button icons. */
	void RefreshActionButtonIcons(const TArray<uint32>& PackedActions);
	void RefreshMainActionBarControls();
	void SetActionBarPage(int32 Page);

	/** Load a BLP texture from MPQ and cache it */
	UTexture2D* LoadUITexture(const FString& TexturePath);

	/** Refresh the stock paper-doll header, portrait, stat rows and slot placeholders. */
	void UpdateCharacterSheet(const FWowPlayerEntity& Player, const FString& CharacterName);
	/** Connect the live paper-doll render target to the stock CharacterModelFrame. */
	void SetCharacterSheetRenderTarget(UTextureRenderTarget2D* RenderTarget);
	/** Dressing room: draws the preview capture inside DressUpModel (DressUpFrame). */
	void SetDressUpRenderTarget(UTextureRenderTarget2D* RenderTarget);
	/** Dressing room actions from Lua (DressUpModel methods, IsDressableItem), answered by the
	 *  gameplay controller: "reset", "undress", "tryon" (ItemEntry), "rotate" (Value = degrees),
	 *  "dressable" (ItemEntry). Returns false when the action does not apply. */
	TFunction<bool(const FString& Action, uint32 ItemEntry, float Value)> DressUpHandler;
	/** Select one of the supported 3.3.5 character-info pages. */
	void SetActiveCharacterSheetTab(int32 TabIndex);

	/** Get the UButton child widget for a frame (nullptr if not found or invalid) */
	UButton* GetButtonWidget(int64 Handle) const;

	/** Get the UEditableTextBox child widget for a frame (nullptr if not found or invalid) */
	UEditableTextBox* GetEditBoxWidget(int64 Handle) const;

	/** Get the USlider child widget for a frame (nullptr if not found or invalid) */
	USlider* GetSliderWidget(int64 Handle) const;

	/** Get the primary text widget associated with a frame (button label, etc.) */
	UTextBlock* GetPrimaryTextWidget(int64 Handle) const;

	/** Get a named FontString's UTextBlock widget */
	UTextBlock* GetNamedFontString(const FString& Name) const;

	/** Append a coloured line to a FrameXML ScrollingMessageFrame. */
	void AddScrollingMessage(int64 Handle, const FString& Message, const FLinearColor& Color = FLinearColor::White);
    void AddTransientMessage(int64 Handle,const FString& Text,const FLinearColor& Color);
    void SetMessageTiming(int64 Handle,double Seconds,bool bFade);
    void TickTransientMessages(double Now);
    TArray<int64> GetFramesAtScreenPosition(float X,float Y) const;
    void ToggleFrameStack();
    /** CheckButton state (frame:SetChecked/GetChecked) and its CheckedTexture overlay. */
    void SetButtonChecked(int64 Handle, bool bChecked);
    bool IsButtonChecked(int64 Handle) const { return CheckedButtons.Contains(Handle); }
    void SetCheckedTexture(int64 Handle, const FString& TexturePath);
    bool IsFrameStackVisible() const {return bFrameStackVisible;}
    /** One frame under the cursor for /fstack (absolute Slate coordinates). */
    struct FFrameStackEntry
    {
        int64 Handle=-1;FString Name,Parent;EWowFrameStrata Strata=EWowFrameStrata::MEDIUM;int32 Level=0,Depth=0;
        FVector2D AbsMin=FVector2D::ZeroVector,AbsMax=FVector2D::ZeroVector,Size=FVector2D::ZeroVector;bool bMouseEnabled=false;
    };
    /** Shown frames whose on-screen rectangle contains the point, top-most first (strata, level, depth). */
    TArray<FFrameStackEntry> GetFrameStackAt(FVector2D AbsolutePosition) const;
    int64 GetHoveredFrame() const {return HoverFrameHandle;}
	void ClearScrollingMessages(int64 Handle);
	void ScrollMessages(int64 Handle, int32 LineDelta);
	void ScrollMessagesToTop(int64 Handle);
	void ScrollMessagesToBottom(int64 Handle);
	int32 GetScrollingMessageCount(int64 Handle) const;
	bool AreMessagesAtTop(int64 Handle) const;
	bool AreMessagesAtBottom(int64 Handle) const;

	/** Focus/read/close a FrameXML EditBox for native keybind bridges such as chat. */
	bool FocusEditBox(int64 Handle, const FString& InitialText = FString(), bool bTakeKeyboardFocus = true);
	bool GetFocusedEditBox(int64& OutHandle, FString& OutText) const;
	void CloseEditBox(int64 Handle, bool bClearText = true);

	/** Select an enabled chat frame. */
	void SetActiveChatTab(int32 TabIndex);
	/** Close transient chat menus when the player clicks back into the world. */
	void DismissChatMenus();
	/** Closes the native speech-bubble chat menu; true if it was open (Esc). */
	bool CloseNativeChatMenu();
	/** Consume text entered after the context-menu Rename Window action. */
	bool CommitPendingChatTabRename(const FString& NewLabel);
	/** Update the native Social-frame bridge from network/offline state. */
	void SetSocialFriendEntries(const TArray<FString>& Entries, int32 OnlineCount);
	void SetSocialContactEntries(const TArray<FString>& Friends, const TArray<FString>& Ignored, int32 OnlineCount);
	void SetRecentWhisperContact(const FString& Sender, const FString& Message);
	/** Enable/rename a chat tab. Addons and server-neutral UI code can opt into slots 3-10. */
	void ConfigureChatTab(int32 TabIndex, const FString& Label, bool bEnabled = true);

	/** Runtime input flags used by the UI hit-test and controller bridge */
	void SetFrameMouseEnabled(int64 Handle, bool bEnabled);
	void SetFrameHitRectInsets(int64 Handle, const FMargin& Insets);
	void SetFrameKeyboardEnabled(int64 Handle, bool bEnabled);
	void SetFrameMouseWheelEnabled(int64 Handle, bool bEnabled);
	void SetFrameRegisteredClicks(int64 Handle, const TArray<FString>& ClickTypes);
	bool IsFrameMouseEnabled(int64 Handle) const;
	bool IsFrameKeyboardEnabled(int64 Handle) const;
	bool IsFrameMouseWheelEnabled(int64 Handle) const;
	bool IsFrameClickRegistered(int64 Handle, const FString& Button, bool bMouseDown) const;

	/** Create a simple test frame for debugging */
	int64 CreateDebugFrame(const FString& Name, float Width, float Height, float X = 0.f, float Y = 0.f);

	/** Get the current UI scale factor */
	float GetUIScale() const { return UIScale; }
	/** Pointer in WoW UI units with the origin at UIParent's bottom-left (GetCursorPosition). */
	bool GetLuaCursorPosition(float& OutX, float& OutY);
	/** Combat Log tab line. The Self / Everything / What happened to me? filters use whether the
	 *  player is the source and/or the target; switching filter replays the kept history. */
	void AddCombatLogMessage(const FString& Message, const FLinearColor& Color, bool bPlayerIsSource, bool bPlayerIsTarget);
	TFunction<class UMaterialInterface*(uint64)> UnitPortraitProvider;

	/** Debug: dump key frame layout info to log */
	void DebugDumpLayout() const;

	/** Post-load pass: hide children of hidden parents (catches frames made visible
	 *  before their parent's OnLoad script hid the parent) */
	void SyncChildVisibility();

	/** Keep placeholder container windows hidden when only the backpack has live data. */
	void SanitizeBackpackOnlyContainers();
	/** Centre bag names on the container title plate and shrink long names to fit. */
	void FitContainerTitles();
	bool TogglePlayerBag(int32 BagId, bool bOpenOnly = false);
	void ToggleAllPlayerBags();
	/** Refresh the stock backpack's compact gold/silver/copper display. */
	void UpdateBackpackMoney(uint32 TotalCopper);

	/** Recursively invalidate cached FrameRects for a frame and all descendants */
	void InvalidateFrameRectsRecursive(int64 Handle);

	/** Find the topmost interactive frame under a screen position (pixels).
	 *  Returns frame handle or -1 if no interactive frame found. */
	int64 HitTestFrames(float ScreenX, float ScreenY) const;
	/** Strata*1000 + level with the client's inheritance (strata from parents, child >= parent + 1). */
	int32 GetEffectiveHitZOrder(int64 Handle) const;
	/** Request item templates for all equipped/bag items once a second (prefetch for tooltips). */
	void PrefetchInventoryItemInfo();
	bool IsMouseOverFrame(int64 Handle, float TopOffset = 0.f, float BottomOffset = 0.f, float LeftOffset = 0.f, float RightOffset = 0.f) const;

	/** Dispatch a mouse click event to a frame's Lua scripts.
	 *  Button: "LeftButton", "RightButton", "MiddleButton" */
	void DispatchMouseDown(int64 Handle, const FString& Button);
	/** Attach a texture region to a button state; it is hidden until that state is active. */
	void SetButtonStateRegion(int64 Handle, EWowButtonStateRegion State, UWidget* Region);
	void SetButtonStateRegionShown(int64 Handle, EWowButtonStateRegion State, bool bShown);
	/** r237: regions in the HIGHLIGHT draw layer (XML <Layer level="HIGHLIGHT">, CreateTexture(nil,
	 *  "HIGHLIGHT")) show only while the mouse is over their frame and the script has not hidden them. */
	void RegisterHighlightLayerRegion(int64 Handle, UWidget* Region, bool bScriptShown = true);
	void UnregisterHighlightLayerRegion(UWidget* Region);
	bool IsHighlightLayerRegion(const UWidget* Region) const;
	void SetHighlightLayerRegionScriptShown(UWidget* Region, bool bShown);
	/** r239: a script called UnregisterAllEvents on this frame. On a stock action button
	 *  (MultiBar*ButtonN) that means an action bar addon took the bar over (Bartender4). */
	void NoteScriptReleasedFrame(int64 Handle);
	/** r243: a named region was re-anchored by a script: refresh its rect and re-anchor the frames
	 *  placed against it (UIDropDownMenuTemplate's Button on $parentRight). */
	void NoteRegionMoved(const FString& RegionName, UWidget* Region, int64 OwnerHandle);
	/** EnumerateFrames(): next non-virtual frame handle after After (-1 = first), or -1. */
	int64 GetNextFrameHandle(int64 After) const;
	void DispatchMouseUp(int64 Handle, const FString& Button);
	void DispatchClick(int64 Handle, const FString& Button, bool bMouseDown);
    void SetFrameRegisteredDragButtons(int64 Handle, const TArray<FString>& Buttons);
    bool IsFrameDragRegistered(int64 Handle, const FString& Button) const;
    bool DispatchDragStart(int64 Handle, const FString& Button);
    void DispatchDragStop(int64 Handle, const FString& Button);
	bool DispatchReceiveDrag(int64 Handle);

	/** Update mouse hover state — call once per frame from Tick.
	 *  Dispatches OnEnter/OnLeave events to Lua scripts. */
	void UpdateMouseHover(float ScreenX, float ScreenY);
	/** Suspend hover/hit presentation while gameplay owns a captured mouse-look drag. */
	void SetMouseInteractionSuppressed(bool bSuppressed);

	/** Poll EditBox/Slider widgets for changes and dispatch Lua events.
	 *  Call once per frame from Tick. */
	void TickWidgetEvents();
    void LayoutSpellBookText();

	/** Dispatch OnEnterPressed to the currently focused EditBox (if any).
	 *  Returns true if an EditBox was focused and received the event. */
	bool DispatchEditBoxEnterPressed();

	/** Dispatch OnEscapePressed to the currently focused EditBox (if any).
	 *  Returns true if an EditBox was focused and received the event. */
	bool DispatchEditBoxEscapePressed();

	/** ScrollFrame API */
	void SetScrollChild(int64 ScrollFrameHandle, int64 ChildHandle);
	int64 GetScrollChild(int64 ScrollFrameHandle) const;
	void SetVerticalScroll(int64 ScrollFrameHandle, float Offset);
	float GetVerticalScroll(int64 ScrollFrameHandle) const;
	void SetHorizontalScroll(int64 ScrollFrameHandle, float Offset);
	float GetHorizontalScroll(int64 ScrollFrameHandle) const;
	float GetVerticalScrollRange(int64 ScrollFrameHandle) const;
	float GetHorizontalScrollRange(int64 ScrollFrameHandle) const;

	/** Cooldown API */
	void SetCooldown(int64 Handle, double StartTime, float Duration);
	void TickCooldowns();

	/** Set the UI scale factor (typically calculated from viewport size vs WoW's base resolution) */
	void SetUIScale(float InScale) { UIScale = InScale; }

private:
    TSet<int64> UserPositionedFrames;
    TMap<int64, TWeakObjectPtr<UWidget>> ButtonStateRegions[static_cast<int32>(EWowButtonStateRegion::Count)];
    struct FHighlightLayerRegion { int64 Handle = -1; bool bScriptShown = true; };
    TMap<TWeakObjectPtr<UWidget>, FHighlightLayerRegion> HighlightLayerRegions;
    TMultiMap<int64, TWeakObjectPtr<UWidget>> HighlightLayerRegionsByFrame;
    void ApplyHighlightLayer(int64 Handle, bool bHovered);
    int32 ScriptReleasedActionBars = 0; // r239: bit per MultiBar (BottomLeft, BottomRight, Right, Left)
    TArray<TWeakObjectPtr<UWidget>> UserPositionedRegions;
    int64 GetFrameLayoutRoot(int64 Handle) const;
    friend class FWowMinimapControlsTest;
	struct FFrameEntry
	{
		FWowFrameDef Def;
		TWeakObjectPtr<UWidget> Widget;
		int64 ParentHandle = -1;
		bool bMouseEnabled = false;
		bool bKeyboardEnabled = false;
		bool bMouseWheelEnabled = false;
		bool bHasExplicitClickRegistration = false;
		TSet<FString> RegisteredClicks;
        TSet<FString> RegisteredDragButtons;
	};

	/** Handle of the frame currently under the mouse cursor (-1 if none) */
	int64 HoverFrameHandle = -1;
	bool bMouseInteractionSuppressed = false;
    ESlateVisibility MouseLookPreviousVisibility = ESlateVisibility::SelfHitTestInvisible;

	/** Last-known text for each EditBox, used to detect changes for OnTextChanged dispatch */
	TMap<int64, FString> EditBoxLastText;

	/** Chat edit boxes: x of the "Say:" header in WoW units, and the applied left text inset. */
	static constexpr float ChatEditHeaderX = 12.0f; // was 8: "Say:" sat against the left end of the box (Stefan 10.10)
	TMap<int64, float> ChatEditInsetApplied;
	// Last chat-type header text per edit box; the colour is re-read from Lua when it changes.
	TMap<int64, FString> ChatEditColorKey;
	void LayoutChatEditHeaders();
	double NextBagTitleRefresh = 0.0;
	void RefreshPendingBagTitles();
	FString ResolveXmlText(const FString& Text) const;
	bool bClockTooltipShown = false;
	double NextClockTooltipRefresh = 0.0;
	void UpdateClockTooltip();

	/** Last-known value for each Slider, used to detect changes for OnValueChanged dispatch */
	TMap<int64, float> SliderLastValue;

	/** ScrollFrame state tracking */
	TMap<int64, float> ScrollVerticalOffsets;
	TMap<int64, float> ScrollHorizontalOffsets;
	TMap<int64, int64> ScrollChildHandles;

	/** Cooldown state per frame */
	struct FCooldownState
	{
		double StartTime = 0.0;
		float Duration = 0.0f;
	};
	TMap<int64, FCooldownState> CooldownStates;
	TMap<int64, TWeakObjectPtr<class UWowCooldownWidget>> CooldownOverlayWidgets;

	TMap<int64, FFrameEntry> Frames;
	TMap<FString, int64> NameToHandle;
	TMap<FString, FWowFrameDef> Templates;
	/** Frames created from a Secure* template chain (SecureFrameTemplate is protected="true"). */
	TSet<int64> ProtectedFrames;
	bool InheritsSecureTemplate(const FString& Inherits) const;

	/** Tooltip state storage */
	TMap<int64, FTooltipState> TooltipStates;

	FWowEventSystem* EventSystem = nullptr;
	FWowFontManager* FontManager = nullptr;
	FMpqManager* MpqManager = nullptr;
	FWowAssetCache* AssetCache = nullptr;
	TWeakObjectPtr<UCanvasPanel> RootCanvas;
    TWeakObjectPtr<class UNativeWidgetHost> MinimapFrostHost;
	int64 NextHandle = 1;

	/** UI scale factor to convert WoW coordinates to UMG design pixels (includes DPI adjustment) */
	float UIScale = 1.0f;
    TSet<int64> DockPanels;
    bool bChatPointerBlocked=false;
    TMap<int64,double> ChatLastActivity;

	/** Raw viewport-to-WoW scale: viewportHeight / 768. Used for converting
	 *  viewport pixel positions (e.g. mouse cursor) to WoW coordinates. */
	float RawViewportScale = 1.0f;
	float UserInterfaceScale = 1.0f;
	void RefreshInterfaceScale();
	/** UI Scale the current widgets were laid out with; the slider preview is GetUIScale() / this. */
	float LaidOutInterfaceScale = 1.0f;
	/** Unit frames (player/target/focus) are drawn at this size before the Unit Frame Size option. */
	static constexpr float UnitFrameBaseScale = 0.85f;
	/** Main bar and extra bars are drawn at this size before the Action Bar Size option. */
	static constexpr float ActionBarBaseScale = 0.90f;
	/** Per-tick: unit frames, action bars and minimap drawn at their own option sizes (render
	 *  transforms about their screen anchor; the bar group centred and fitted to the width). */
	void ApplyElementScales();
	TArray<TWeakObjectPtr<UWidget>> ElementScaledWidgets;
	/** Per-tick: target/focus names wrap to two lines instead of running past the name plate. */
	void FitUnitNameLabels();
	/** PanelTemplates_TabResize for CharacterFrameTabButtonTemplate tabs (<Parent>Tab1..N): the tabs
	 *  stay 10 units wide otherwise, overlap and lose their caption. Runs once per shown parent. */
	void LayoutTemplateTabs(const TCHAR* ParentName, int32 NumTabs, float FirstX, float BottomOffset);
	TSet<FString> TemplateTabsDone;
	TMap<FString, FString> FittedUnitNames;
    TWeakObjectPtr<class UWowConnectionManager> ItemConnection;
    uint32 LastItemRevision = 0;

	/** Selected chat surface; used by the native fade/hover bridge. */
    bool bChatWindowHidden = false;
    TMap<TWeakObjectPtr<UWidget>, ESlateVisibility> HiddenChatWidgets;
    void ApplyChatWindowHidden();
public:
    void SetChatWindowHidden(bool bHidden);
private:
	int32 ActiveChatTabIndex = 1;
	/** Enabled chat windows and their display names. Only General and Combat Log are enabled by default. */
	TSet<int32> EnabledChatTabs;
	/** Tabs the dock layout itself hid (overflow / undocked); Lua hiding a tab does not count. */
	TSet<int32> LayoutHiddenChatTabs;
	TMap<int32, FString> ChatTabLabels;
	/** Tabs currently moved into the overflow menu by the responsive dock layout. */
	TArray<int32> OverflowChatTabIndices;
	/** Native chat-window lock and active size-grabber drag state. */
	bool bChatFrameUnlocked = false;
	bool bChatResizeActive = false;
	bool bChatTabContextMenuOpen = false;
	bool bChatBackgroundEnabled = true;
	bool bChatFrameNonInteractive = false;
	bool bChatTabRenamePending = false;
	int32 ChatFontSize = 10;
	bool bCombatLogFilterMenuOpen = false;
	bool bCombatLogKillsEnabled = true;
	int32 ActiveCombatLogFilterIndex = 2;
	int32 ChatTabRenameIndex = 0; // tab whose naming dialog is open (Rename Window)
	struct FCombatLogHistoryLine { FString Message; FLinearColor Color; bool bPlayerSource = false; bool bPlayerTarget = false; };
	TArray<FCombatLogHistoryLine> CombatLogHistory;
	bool CombatLogLinePasses(const FCombatLogHistoryLine& Line) const;
	void ReplayCombatLog();
	int32 ChatContextTabIndex = 1;
	FVector2D ChatResizeStartCursor = FVector2D::ZeroVector;
	FVector2D ChatResizeStartSize = FVector2D::ZeroVector;
	TArray<FString> SocialFriendEntries;
	TArray<FString> SocialIgnoreEntries;
	int32 SocialOnlineFriendCount = 0;
	int32 ActiveSocialTabIndex = 1;
	bool bSocialIgnoreSelected = false;
	bool bSocialFriendContextMenuOpen = false;
	int32 SocialContextFriendRow = 0;
	FString SocialContextFriendName;
	FString RecentWhisperSender;
	FString RecentWhisperMessage;
	bool bCharacterRightStatMenuOpen = false;
	bool bCharacterTitleMenuOpen = false;
	int32 ActiveCharacterSheetTabIndex = 1;
	/** Stefan 11.10: both stat panels have the same dropdown (Base Stats, Melee, Ranged, Spell, Defense);
	 *  the left one starts on Base Stats, the right one on Melee. */
	bool bCharacterLeftStatMenuOpen = false;
	int32 CharacterLeftStatPage = 0;
	int32 CharacterRightStatPage = 1;
	TArray<FString> CharacterRightStatLabels[5];
	TArray<int32> CharacterRightStatValues[5];
	bool bMinimapTrackingMenuOpen = false;
	/** Native speech-bubble chat menu (replaces FrameXML's ChatMenu, whose labels did not draw). */
	bool bNativeChatMenuOpen = false;
	void RefreshNativeChatMenu();
	int32 MinimapTrackingMode = 0;
	float MinimapTimeOfDayMinutes = 720.0f;
	int32 LastMinimapDayNightState = INDEX_NONE;
	TStaticArray<FWowMirrorTimerState, 3> MirrorTimers;
    TMap<int64, TWeakObjectPtr<UImage>> MirrorTimerFills;
	void RefreshMirrorTimerPresentation();
	int64 NativeTooltipOwnerHandle = -1;

	/** Cache for loaded UI textures to avoid loading the same BLP twice */
	TMap<FString, TWeakObjectPtr<UTexture2D>> TextureCache;

	/** Named texture regions (from XML Texture name="Foo") → UImage widget */
	TMap<FString, TWeakObjectPtr<UImage>> NamedTextureWidgets;

	/** Named fontstrings (from XML FontString name="Foo") → UTextBlock widget */
	TMap<FString, TWeakObjectPtr<UTextBlock>> NamedFontStringWidgets;
	TMap<FString, TWeakObjectPtr<UImage>> LabelBackgroundWidgets;

	/** StatusBar frames → UProgressBar child widgets */
	TMap<int64, TWeakObjectPtr<UProgressBar>> StatusBarWidgets;

	/** Interactive control widgets owned by frame containers */
	TMap<int64, TWeakObjectPtr<UButton>> ButtonWidgets;
	TMap<int64, TWeakObjectPtr<UEditableTextBox>> EditBoxWidgets;
	TMap<int64, TWeakObjectPtr<USlider>> SliderWidgets;
	TMap<int64, TWeakObjectPtr<UTextBlock>> PrimaryTextWidgets;
	uint32 BackpackMoneyCopper = 0;
	struct FScrollingMessageEntry
	{
		TWeakObjectPtr<UWidget> Widget;
		FString SourceText;
		FString PlainText;
		FLinearColor DefaultColor = FLinearColor::White;
		int32 VisualLineCount = 1;
		double AddedAtSeconds = 0.0;
	};
    struct FTransientLine {FString Text;TWeakObjectPtr<UTextBlock> Widget;double AddedAt=0;};
    struct FTransientState {TArray<FTransientLine> Lines;double VisibleSeconds=3,FadeSeconds=1;};
    TMap<int64,FTransientState> TransientMessages;
    bool bFrameStackVisible=false;
    void TickFrameStack();
    TSharedPtr<class SWidget> FrameStackOverlay;
    TSet<int64> CheckedButtons;
    TMap<int64, TWeakObjectPtr<UImage>> CheckedTextureWidgets;
    TSharedPtr<int32> FrameStackLifetime=MakeShared<int32>(0); // overlay holds a weak ref: no access after the manager is gone
	TMap<int64, TArray<FScrollingMessageEntry>> ScrollingMessageWidgets;
	/** Number of lines the view is offset from the newest message (zero = bottom). */
	TMap<int64, int32> ScrollingMessageOffsets;
	void RebuildScrollingMessageWidget(int64 Handle, FScrollingMessageEntry& MessageEntry);
	void RefreshScrollingMessageLayout(int64 Handle);
	void EnsureNativeCharacterStatControls();
	void RefreshNativeCharacterStatControls();
    void LayoutCharacterSheetRegions();
	void EnsureNativeCharacterTitleControl();
	void RefreshNativeCharacterTitleControl();
	void EnsureNativeCharacterSheetTabs();
	void RefreshNativeCharacterSheetTabs();
	void EnsureNativeMinimapControls();
	void RefreshNativeMinimapControls();
	void ShowNativeTooltipForFrame(int64 OwnerHandle);
	void HideNativeTooltip();

public:
	/** Get named texture region's UImage widget */
	UImage* GetNamedTexture(const FString& Name) const
	{
		const TWeakObjectPtr<UImage>* Found = NamedTextureWidgets.Find(Name);
		return (Found && Found->IsValid()) ? Found->Get() : nullptr;
	}
	void UpdateStatusBarSpark(int64 Handle, float NormalizedValue);
    void UpdateOptionalActionBars();
    bool IsFrameEffectivelyShown(int64 Handle) const;
    TMap<TWeakObjectPtr<UImage>,FLinearColor> ActionIconTargets;
    double LastActionAnimation = -1;
    double LastActionStateUpdate = -1;
    void RefreshActionButtonState();

	/** Get the UProgressBar widget for a StatusBar frame (nullptr if not found or invalid) */
	UProgressBar* GetStatusBarWidget(int64 Handle) const
	{
		const TWeakObjectPtr<UProgressBar>* Found = StatusBarWidgets.Find(Handle);
		return (Found && Found->IsValid()) ? Found->Get() : nullptr;
	}

	/** Tooltip methods for Lua API */
	void TooltipSetOwner(int64 TooltipHandle, int64 OwnerHandle, const FString& AnchorType);
	void TooltipAddLine(int64 TooltipHandle, const FString& Text, float R, float G, float B, bool bWrapText);
	void TooltipAddDoubleLine(int64 TooltipHandle, const FString& LeftText, const FString& RightText,
							  float LR, float LG, float LB, float RR, float RG, float RB);
	void TooltipClearLines(int64 TooltipHandle);
	int32 TooltipNumLines(int64 TooltipHandle) const;
	void TooltipSetText(int64 TooltipHandle, const FString& Text);
	void TooltipAppendText(int64 TooltipHandle, const FString& Text);
	void TooltipSetItem(int64 TooltipHandle, const FString& ItemLink);
	void TooltipSetItemDetails(int64 TooltipHandle, uint32 ItemEntry, const FString& ItemLink,
		int32 StackCount, const FString& ContextText, int32 Durability = 0, int32 MaxDurability = 0, bool bEquipped = false,
		uint64 ItemGuid = 0);
	/** ShoppingTooltipN:SetHyperlinkCompareItem(link, N); false when nothing is equipped there. */
	bool TooltipSetCompareItem(int64 TooltipHandle, const FString& ItemLink, int32 Index);
	bool IsTooltipItemEquipped(int64 TooltipHandle) const;
	void TooltipSetSpell(int64 TooltipHandle, int32 SpellId);
	void TooltipSetUnit(int64 TooltipHandle, const FString& UnitToken);
	void TooltipUpdateDisplay(int64 TooltipHandle);
	FTooltipState* GetTooltipState(int64 TooltipHandle);
	const FTooltipState* GetTooltipState(int64 TooltipHandle) const;

private:
	void EnsureTooltipPresentation(int64 TooltipHandle);
	void PositionTooltipAtScreenCorner(int64 TooltipHandle);
	UTextBlock* EnsureTooltipText(int64 TooltipHandle, const FString& Prefix, bool bRight, int32 Index);
	void HideShoppingTooltips(int64 TooltipHandle);
	TArray<uint32> GetEquippedForCompare(uint32 ItemEntry) const;

	/** Frame position/size tracking for anchor resolution */
	struct FFrameRect
	{
		float X, Y, W, H;
		FFrameRect(float InX = 0.f, float InY = 0.f, float InW = 0.f, float InH = 0.f)
			: X(InX), Y(InY), W(InW), H(InH) {}
	};
	TMap<FString, FFrameRect> FrameRects;

	// r228: dependent layout (see FlushDependentLayout).
	struct FDynamicRegion
	{
		TWeakObjectPtr<UWidget> Widget;
		int64 ParentHandle = -1;
		TArray<FWowAnchor> Anchors;
		float W = 0.f, H = 0.f;
	};
	TArray<FDynamicRegion> DynamicRegions;
	TSet<int64> DependentLayoutChanged;
	TSet<int64> PendingSizeChanged; // OnSizeChanged, fired after the layout pass like the original
	bool bInDependentLayout = false;
	void QueueDependentLayout(int64 Handle);
	void ApplyDynamicRegion(const FDynamicRegion& Region);

	int64 CreateFrameInternal(const FWowFrameDef& Def, int64 ExplicitParentHandle);
	UWidget* CreateWidgetForFrame(const FWowFrameDef& Def, int64 Handle, int64 ParentHandle = -1);

	/** Create layer content (textures and fontstrings) inside a frame's canvas */
	void CreateLayerContent(UCanvasPanel* Container, const FWowFrameDef& Def, int64 OwnerHandle);

	/** Resolve template inheritance for a frame definition */
	FWowFrameDef ResolveInherits(const FWowFrameDef& Def) const;

	/** Resolve $parent references in frame names and anchor RelativeTo fields */
	void ResolveParentReferences(FWowFrameDef& Def, const FString& ParentName);

	/** Merge template attributes into a target definition (template first, then target overrides) */
	static void MergeTemplate(FWowFrameDef& Target, const FWowFrameDef& Template);

	/** Apply anchor positioning to a widget's canvas slot */
	void ApplyAnchors(UWidget* Widget, const FWowFrameDef& Def);

	/** Apply anchor positioning to a widget within a parent canvas.
	 *  ParentFrameW/H are the parent frame's dimensions in WoW coords (needed because GetDesiredSize returns 0 at creation time). */
	void ApplyElementAnchors(UWidget* Widget, UCanvasPanel* Parent, const TArray<FWowAnchor>& Anchors, float Width, float Height, bool bSetAllPoints = false, float ParentFrameW = 0.f, float ParentFrameH = 0.f, float ParentFrameX = 0.f, float ParentFrameY = 0.f);

	/** Get the screen position offset of an anchor point on a frame */
	static FVector2D GetAnchorPointOffset(EWowAnchorPoint Point, float Width, float Height);

	/** Get the frame rect for a named frame, or UIParent default */
	FFrameRect GetFrameRect(const FString& FrameName);

	/** Create backdrop (9-slice background) for a frame */
	class UWowBackdropWidget* CreateBackdrop(UCanvasPanel* Container, const FWowBackdrop& Backdrop, int64 Handle);
	TMap<int64, TWeakObjectPtr<class UWowBackdropWidget>> BackdropWidgets;
	/** Backdrops using the tooltip background texture: always FWowWindowStyle::TooltipBackgroundColor(). */
	TSet<int64> TooltipBackdrops;
};
