#pragma once
#include "CoreMinimal.h"

/** Anchor point names */
UENUM()
enum class EWowAnchorPoint : uint8
{
	TOPLEFT, TOP, TOPRIGHT,
	LEFT, CENTER, RIGHT,
	BOTTOMLEFT, BOTTOM, BOTTOMRIGHT
};

/** Frame strata levels (z-order) */
UENUM()
enum class EWowFrameStrata : uint8
{
	BACKGROUND = 0,
	LOW,
	MEDIUM,
	HIGH,
	DIALOG,
	FULLSCREEN,
	FULLSCREEN_DIALOG,
	TOOLTIP
};

/** Draw layers within a frame */
UENUM()
enum class EWowDrawLayer : uint8
{
	BACKGROUND = 0,
	BORDER,
	ARTWORK,
	OVERLAY,
	HIGHLIGHT
};

/** An anchor definition */
struct FWowAnchor
{
	EWowAnchorPoint Point = EWowAnchorPoint::CENTER;
	FString RelativeTo;
	EWowAnchorPoint RelativePoint = EWowAnchorPoint::CENTER;
	float OffsetX = 0.f;
	float OffsetY = 0.f;
};

/** A texture element within a layer */
struct FWowTextureElement
{
	FString Name;
	FString ParentKey;
	FString File;
	float Width = 0.f;
	float Height = 0.f;
	TArray<FWowAnchor> Anchors;
	FLinearColor VertexColor = FLinearColor::White;
	bool bSetAllPoints = false;
	bool bHidden = false;
	// Tex coords
	float Left = 0.f, Right = 1.f, Top = 0.f, Bottom = 1.f;
};

/** A font string element within a layer */
struct FWowFontStringElement
{
	FString Name;
	FString ParentKey;
	FString Inherits;
	FString Text;
	float Width = 0.f;
	float Height = 0.f;
	TArray<FWowAnchor> Anchors;
	FString JustifyH = TEXT("CENTER");
	FString JustifyV = TEXT("MIDDLE");
	FLinearColor Color = FLinearColor::White;
	float FontHeight = 12.f;
	FString FontFlags; // OUTLINE, THICKOUTLINE, MONOCHROME
	// Given on the fontstring itself; otherwise its font object (inherits) decides.
	bool bHasColor = false;
	bool bHasFontHeight = false;
};

/** A layer containing textures and font strings */
struct FWowLayer
{
	EWowDrawLayer Level = EWowDrawLayer::ARTWORK;
	TArray<FWowTextureElement> Textures;
	TArray<FWowFontStringElement> FontStrings;
};

/** Script handler (event name -> Lua code) */
struct FWowScriptHandler
{
	FString Event; // OnLoad, OnEvent, OnClick, OnUpdate, etc.
	FString Code;  // Inline Lua code
	FString File;  // External Lua file reference
};

/** Backdrop definition (9-slice background) */
struct FWowBackdrop
{
	FString BgFile;
	FString EdgeFile;
	int32 EdgeSize = 0;
	int32 TileSize = 0;
	bool Tile = false;
	float InsetLeft = 0, InsetRight = 0, InsetTop = 0, InsetBottom = 0;
};

/** Frame types */
UENUM()
enum class EWowFrameType : uint8
{
	Frame,
	Button,
	CheckButton,
	EditBox,
	ScrollFrame,
	ScrollingMessageFrame,
	MessageFrame,
	SimpleHTML,
	Slider,
	StatusBar,
	Cooldown,
	ColorSelect,
	GameTooltip,
	Minimap,
	Model,
	PlayerModel,
	DressUpModel,
	TabardModel,
	WorldFrame
};

/** Complete parsed frame definition */
struct WOWUI_API FWowFrameDef
{
	EWowFrameType Type = EWowFrameType::Frame;
	FString Name;
	FString ParentKey;
	FString Parent;
	FString Inherits; // Comma-separated template names
	bool bVirtual = false;
	bool bHidden = false;
	bool bSetAllPoints = false;
	bool bEnableMouse = false;
	bool bEnableKeyboard = false;
	/** clampedToScreen / SetClampedToScreen: kept inside UIParent when placed (tooltips near the right edge). */
	bool bClampedToScreen = false;
	/** Created by Lua CreateFrame: original sizing rules (no size = 0, no anchor = not drawn). */
	bool bCreatedByLua = false;

	float Width = 0.f;
	float Height = 0.f;

	EWowFrameStrata Strata = EWowFrameStrata::MEDIUM;
	int32 FrameLevel = 0;
	int32 FrameID = 0; // WoW XML "id" attribute

	TArray<FWowAnchor> Anchors;
	TArray<FWowLayer> Layers;
	TArray<FWowScriptHandler> Scripts;
	TArray<FString> NamedObjectGlobals;
	/** r338: <Attributes><Attribute name="" type="number|string|boolean|nil" value=""/></Attributes> - the
	 *  frame's initial SetAttribute values (UIParent's panel offsets, SecureTemplates' showParty ...). */
	struct FXmlAttribute { FString Name, Type, Value; };
	TArray<FXmlAttribute> XmlAttributes;
	TArray<FWowFrameDef> Children;

	TOptional<FWowBackdrop> Backdrop;
	TOptional<FMargin> HitRectInsets; // WoW units; positive insets shrink the mouse region.

	// Type-specific properties
	FString ButtonText;
	FString NormalTexture, PushedTexture, HighlightTexture, DisabledTexture;
    // left, right, top, bottom: state textures can be regions of a shared atlas.
    FVector4 NormalTextureUV = FVector4(0, 1, 0, 1);
    // r379: the NormalTexture's own <Size> and first anchor (0 x 0 = fill the button). A +/- toggle
    // (UI-MinusButton-Up, 16 x 16 at LEFT 3,0 on a 300-wide header) was stretched over the whole row.
    float NormalTextureW = 0.f, NormalTextureH = 0.f;
    EWowAnchorPoint NormalTexturePoint = EWowAnchorPoint::CENTER;
    EWowAnchorPoint NormalTextureRelativePoint = EWowAnchorPoint::CENTER;
    FVector2D NormalTextureOffset = FVector2D::ZeroVector;
    FVector4 PushedTextureUV = FVector4(0, 1, 0, 1);
    FVector4 HighlightTextureUV = FVector4(0, 1, 0, 1);
    FVector4 DisabledTextureUV = FVector4(0, 1, 0, 1);
	// CheckButton: shown while checked (frame:SetChecked / GetChecked).
	FString CheckedTexture;
	FVector4 CheckedTextureUV = FVector4(0, 1, 0, 1);
	FString NormalTextureName, PushedTextureName, HighlightTextureName, DisabledTextureName;
	FString NormalTextureParentKey, PushedTextureParentKey, HighlightTextureParentKey, DisabledTextureParentKey;
	float MinValue = 0.f, MaxValue = 100.f, DefaultValue = 0.f;
	FString Orientation; // HORIZONTAL, VERTICAL (for Slider/StatusBar)
	FString BarTexture; // Authored StatusBar fill texture
};

/** Directive from XML parsing */
struct FWowXmlDirective
{
	enum class EType { Frame, Include, Script, Font };
	EType Type;

	// For Frame
	FWowFrameDef FrameDef;

	// For Include/Script
	FString FilePath;

	// For Font
	FString FontName;
	FString FontInherits;
	float FontHeight = 12.f;
	FString FontFlags;
	FString FontFile;            // font="Fonts\\FRIZQT__.TTF"
	bool bFontHasHeight = false; // height given here (otherwise inherited)
	bool bFontHasColor = false;  // <Color> child (GameFontNormal gold, GameFontHighlight white...)
	FLinearColor FontColor = FLinearColor::White;
};
