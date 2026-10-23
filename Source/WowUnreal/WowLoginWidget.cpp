#include "WowLoginWidget.h"
#include "WowLoginTheme.h"
#include "WowFontManager.h"
#include "Mpq/MpqManager.h"
#include "Formats/BlpParser.h"
#include "WowTextureFactory.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformProcess.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "WowClientPreferences.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "MediaTexture.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "WowBackdropWidget.h"

// r388 (Stefan 16.10: "new text boxes, thinner and rounder, and better text fonts, more like the OG
// screen"): AccountLoginAccountEdit / PasswordEdit are 200 x 37 with the Glue-Tooltip-Border backdrop
// (edge 16, UI-Tooltip-Background tiled at 16, insets 10/5/4/9) and GlueEditBoxFont (ARIALN 16, white);
// the labels are GlueFontNormal (FRIZQT 15, outline, gold). Our canvas is 1280 x 720, the glue UI
// 768 high, so WoW units x 0.9375; Slate font sizes are points (x 0.75 of the pixel height).
namespace WowLoginGlue
{
    constexpr float Unit = 720.f / 768.f;
    inline int32 FontPoints(float WowPixels) { return FMath::Max(6, FMath::RoundToInt(WowPixels * Unit * .75f)); }
    TArray<TWeakPtr<SWowBackdrop>> FieldBackdrops; // filled with the art once the MPQs are open

    /** r389 (Stefan 16.10: "hide the inner border of the actual textbox"): the edit box draws no frame
     *  or background of its own - the glue backdrop behind it is the whole box. */
    const FEditableTextBoxStyle& BareTextBoxStyle()
    {
        static FEditableTextBoxStyle Style = []
        {
            FEditableTextBoxStyle Bare = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
            const FSlateNoResource None;
            Bare.SetBackgroundImageNormal(None).SetBackgroundImageHovered(None).SetBackgroundImageFocused(None)
                .SetBackgroundImageReadOnly(None).SetPadding(FMargin(0.f));
            return Bare;
        }();
        return Style;
    }
}

// r280 (Stefan 13.10, Diablo 3 login patch): the login labels and button captions take their colour
// and shadow from GlueFontNormal in Interface\GlueXML\GlueFontStyles.xml, like the client's glue
// text (the patch makes it 0.94/0.92/0.88 with a dark red shadow). Gold with a black shadow when the
// file gives none.
static FLinearColor GWowGlueTextColor(1.f, .82f, 0.f);
static FLinearColor GWowGlueShadowColor = FLinearColor::Black;
static FVector2D GWowGlueShadowOffset(1.f, 1.f);

// r286 (Stefan 13.10, Diablo 3 login patch handoff): a login theme may place the account box with
// Interface\D3Login\UELayout.json (patch-enGB-T): named rectangles x, y, w, h on a reference canvas
// (1280 x 720, the canvas this screen is laid out on). Rectangles it does not name keep the native
// positions; without the file everything is native. While a theme layout is active the corner logo is
// hidden (the theme's logo is in its glue model, centred).
static TMap<FString, FMargin> GWowLoginLayout;
static bool bRememberAccountPref = false;
static FString RememberedAccountPref;

static void ReadLoginLayout(FMpqManager* Mpq)
{
    TArray<uint8> Raw;
    // r300 (Stefan 13.10: the offline login showed the stock gold text and layout): a later call with
    // data that cannot be read keeps what an earlier call found instead of falling back.
    if (!Mpq || !Mpq->IsInitialized() || !Mpq->ReadFile(TEXT("Interface\\D3Login\\UELayout.json"), Raw) || Raw.IsEmpty())
    {
        UE_LOG(LogTemp, Log, TEXT("Login layout: no UELayout.json in this data (%d rectangles kept)"), GWowLoginLayout.Num());
        return;
    }
    GWowLoginLayout.Empty();
    FString Json;
    FFileHelper::BufferToString(Json, Raw.GetData(), Raw.Num());
    float ScaleX = 1.f, ScaleY = 1.f;
    {
        const FRegexPattern CanvasPattern(TEXT("\"reference_canvas\"\\s*:\\s*\\[\\s*([0-9.]+)\\s*,\\s*([0-9.]+)\\s*\\]"));
        FRegexMatcher Canvas(CanvasPattern, Json);
        if (Canvas.FindNext())
        {
            const float W = FCString::Atof(*Canvas.GetCaptureGroup(1)), H = FCString::Atof(*Canvas.GetCaptureGroup(2));
            if (W > 1.f && H > 1.f) { ScaleX = 1280.f / W; ScaleY = 720.f / H; }
        }
    }
    const int32 Positions = Json.Find(TEXT("\"positions\""));
    if (Positions == INDEX_NONE) return;
    const int32 Open = Json.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Positions);
    const int32 Close = Open == INDEX_NONE ? INDEX_NONE : Json.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
    if (Close == INDEX_NONE) return;
    const FString Block = Json.Mid(Open, Close - Open);
    const FRegexPattern RectPattern(TEXT("\"([A-Za-z_]+)\"\\s*:\\s*\\[\\s*(-?[0-9.]+)\\s*,\\s*(-?[0-9.]+)\\s*,\\s*([0-9.]+)\\s*,\\s*([0-9.]+)\\s*\\]"));
    FRegexMatcher Rect(RectPattern, Block);
    while (Rect.FindNext())
    {
        const float W = FCString::Atof(*Rect.GetCaptureGroup(4)), H = FCString::Atof(*Rect.GetCaptureGroup(5));
        if (W <= 0.f || H <= 0.f) continue;
        GWowLoginLayout.Add(Rect.GetCaptureGroup(1), FMargin(FCString::Atof(*Rect.GetCaptureGroup(2)) * ScaleX,
            FCString::Atof(*Rect.GetCaptureGroup(3)) * ScaleY, W * ScaleX, H * ScaleY));
    }
    FString Names;
    for (const auto& Pair : GWowLoginLayout) Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + Pair.Key;
    UE_LOG(LogTemp, Log, TEXT("Login layout from UELayout.json: %d rectangles (%s)"), GWowLoginLayout.Num(), *Names);
}

static FMargin LoginRect(const TCHAR* Name, const FMargin& Native)
{
    const FMargin* Theme = GWowLoginLayout.Find(Name);
    return Theme ? *Theme : Native;
}

static void ReadGlueFontNormal(FMpqManager* Mpq)
{
    TArray<uint8> Raw;
    if (!Mpq || !Mpq->IsInitialized() || !Mpq->ReadFile(TEXT("Interface\\GlueXML\\GlueFontStyles.xml"), Raw) || Raw.IsEmpty())
    {
        UE_LOG(LogTemp, Log, TEXT("Login text colour: no GlueFontStyles.xml in this data (kept %s)"), *GWowGlueTextColor.ToString());
        return; // r300: keep what an earlier read found
    }
    GWowGlueTextColor = FLinearColor(1.f, .82f, 0.f);
    GWowGlueShadowColor = FLinearColor::Black;
    GWowGlueShadowOffset = FVector2D(1.f, 1.f);
    FString Xml;
    FFileHelper::BufferToString(Xml, Raw.GetData(), Raw.Num());
    const int32 Start = Xml.Find(TEXT("name=\"GlueFontNormal\""));
    if (Start == INDEX_NONE) return;
    const int32 TagEnd = Xml.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
    if (TagEnd == INDEX_NONE || Xml[TagEnd - 1] == TEXT('/')) return; // <Font .../>: nothing of its own
    const int32 End = Xml.Find(TEXT("</Font>"), ESearchCase::IgnoreCase, ESearchDir::FromStart, TagEnd);
    if (End == INDEX_NONE) return;
    FString Block = Xml.Mid(TagEnd + 1, End - TagEnd - 1);
    auto ReadColor = [](const FString& Text, FLinearColor& Out)
    {
        const FRegexPattern Pattern(TEXT("<Color\\s+r=\"([0-9.]+)\"\\s+g=\"([0-9.]+)\"\\s+b=\"([0-9.]+)\"(?:\\s+a=\"([0-9.]+)\")?"));
        FRegexMatcher Match(Pattern, Text);
        if (!Match.FindNext()) return false;
        const FString A = Match.GetCaptureGroup(4);
        Out = FLinearColor(FCString::Atof(*Match.GetCaptureGroup(1)), FCString::Atof(*Match.GetCaptureGroup(2)),
            FCString::Atof(*Match.GetCaptureGroup(3)), A.IsEmpty() ? 1.f : FCString::Atof(*A));
        return true;
    };
    // The shadow block has its own Color; the font colour is the one outside it.
    const int32 ShadowStart = Block.Find(TEXT("<Shadow"));
    const int32 ShadowEnd = ShadowStart == INDEX_NONE ? INDEX_NONE : Block.Find(TEXT("</Shadow>"), ESearchCase::IgnoreCase, ESearchDir::FromStart, ShadowStart);
    if (ShadowStart != INDEX_NONE && ShadowEnd != INDEX_NONE)
    {
        const FString Shadow = Block.Mid(ShadowStart, ShadowEnd - ShadowStart);
        ReadColor(Shadow, GWowGlueShadowColor);
        const FRegexPattern OffsetPattern(TEXT("x=\"(-?[0-9.]+)\"\\s+y=\"(-?[0-9.]+)\""));
        FRegexMatcher Offset(OffsetPattern, Shadow);
        if (Offset.FindNext())
            GWowGlueShadowOffset = FVector2D(FCString::Atof(*Offset.GetCaptureGroup(1)), -FCString::Atof(*Offset.GetCaptureGroup(2)));
        Block = Block.Left(ShadowStart) + Block.Mid(ShadowEnd + 9);
    }
    ReadColor(Block, GWowGlueTextColor);
    UE_LOG(LogTemp, Log, TEXT("Login text colour from GlueFontNormal: %s shadow %s offset %s"),
        *GWowGlueTextColor.ToString(), *GWowGlueShadowColor.ToString(), *GWowGlueShadowOffset.ToString());
}

// WoW stores top/bottom border strips vertically in its eight-piece atlas.
class SLoginServerFrame : public SBorder
{
public:
    void Construct(const SBorder::FArguments& Args, const TArray<TSharedPtr<FSlateBrush>>* InEdges)
    { Edges=InEdges; SBorder::Construct(Args); }
    int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,
        FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override
    {
        const int32 Top=SBorder::OnPaint(Args,G,Clip,Out,Layer,Style,Enabled);
        if(!Edges || Edges->Num()!=8)return Top;
        const FVector2D Size=G.GetLocalSize();const float E=16.f;
        auto Box=[&](int32 I,FVector2D Pos,FVector2D Extent,bool Rotate=false)
        {
            if(Extent.X<=0 || Extent.Y<=0)return;
            const auto Geo=G.ToPaintGeometry(Extent,FSlateLayoutTransform(Pos));
            if(Rotate)FSlateDrawElement::MakeRotatedBox(Out,Top+1,Geo,(*Edges)[I].Get(),ESlateDrawEffect::None,
                HALF_PI,FVector2f::ZeroVector,FSlateDrawElement::RelativeToElement,Style.GetColorAndOpacityTint());
            else FSlateDrawElement::MakeBox(Out,Top+1,Geo,(*Edges)[I].Get(),ESlateDrawEffect::None,Style.GetColorAndOpacityTint());
        };
        Box(0,{0,E},{E,Size.Y-2*E});Box(1,{Size.X-E,E},{E,Size.Y-2*E});
        Box(2,{Size.X-E,0},{E,Size.X-2*E},true);Box(3,{Size.X-E,Size.Y-E},{E,Size.X-2*E},true);
        Box(4,{0,0},{E,E});Box(5,{Size.X-E,0},{E,E});Box(6,{0,Size.Y-E},{E,E});Box(7,{Size.X-E,Size.Y-E},{E,E});
        return Top+1;
    }
private:
    const TArray<TSharedPtr<FSlateBrush>>* Edges=nullptr;
};

// â”€â”€â”€ Expansion-specific theming â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

FLinearColor SWowLoginWidget::GetBackgroundColor() const
{
    switch (CurrentExpansion)
    {
    case EWowExpansion::Classic:
        return FLinearColor(0.050f, 0.030f, 0.012f, 0.95f);
    case EWowExpansion::BurningCrusade:
        return FLinearColor(0.010f, 0.040f, 0.018f, 0.95f);
    case EWowExpansion::WrathOfTheLichKing:
    default:
        return FLinearColor(0.018f, 0.033f, 0.060f, 0.95f);
    }
}

FLinearColor SWowLoginWidget::GetAccentColor() const
{
    switch (CurrentExpansion)
    {
    case EWowExpansion::Classic:
        return FLinearColor(1.0f, 0.84f, 0.0f); // Classic gold
    case EWowExpansion::BurningCrusade:
        return FLinearColor(0.4f, 1.0f, 0.2f); // Fel green
    case EWowExpansion::WrathOfTheLichKing:
    default:
        return FLinearColor(0.6f, 0.85f, 1.0f); // Icy blue
    }
}

FLinearColor SWowLoginWidget::GetFallbackSideColor(bool bRightSide) const
{
    switch (CurrentExpansion)
    {
    case EWowExpansion::Classic:
        return bRightSide
            ? FLinearColor(0.16f, 0.09f, 0.030f, 0.78f)
            : FLinearColor(0.05f, 0.11f, 0.035f, 0.78f);
    case EWowExpansion::BurningCrusade:
        return bRightSide
            ? FLinearColor(0.03f, 0.18f, 0.045f, 0.78f)
            : FLinearColor(0.12f, 0.035f, 0.08f, 0.78f);
    case EWowExpansion::WrathOfTheLichKing:
    default:
        return bRightSide
            ? FLinearColor(0.020f, 0.075f, 0.120f, 0.78f)
            : FLinearColor(0.040f, 0.055f, 0.110f, 0.78f);
    }
}

FLinearColor SWowLoginWidget::GetFallbackCenterColor() const
{
    switch (CurrentExpansion)
    {
    case EWowExpansion::Classic:
        return FLinearColor(0.23f, 0.145f, 0.050f, 0.38f);
    case EWowExpansion::BurningCrusade:
        return FLinearColor(0.050f, 0.220f, 0.060f, 0.34f);
    case EWowExpansion::WrathOfTheLichKing:
    default:
        return FLinearColor(0.030f, 0.120f, 0.180f, 0.34f);
    }
}

FLinearColor SWowLoginWidget::GetFallbackBandColor() const
{
    switch (CurrentExpansion)
    {
    case EWowExpansion::Classic:
        return FLinearColor(0.26f, 0.12f, 0.025f, 0.50f);
    case EWowExpansion::BurningCrusade:
        return FLinearColor(0.12f, 0.28f, 0.035f, 0.48f);
    case EWowExpansion::WrathOfTheLichKing:
    default:
        return FLinearColor(0.040f, 0.145f, 0.230f, 0.48f);
    }
}

FString SWowLoginWidget::GetExpansionTitle() const
{
    switch (CurrentExpansion)
    {
    case EWowExpansion::Classic:
        return TEXT("World of Warcraft");
    case EWowExpansion::BurningCrusade:
        return TEXT("The Burning Crusade");
    case EWowExpansion::WrathOfTheLichKing:
    default:
        return TEXT("Wrath of the Lich King");
    }
}

FString SWowLoginWidget::GetExpansionSubtitle() const
{
    switch (CurrentExpansion)
    {
    case EWowExpansion::Classic:
        return TEXT("");
    case EWowExpansion::BurningCrusade:
        return TEXT("World of Warcraft");
    case EWowExpansion::WrathOfTheLichKing:
    default:
        return TEXT("World of Warcraft");
    }
}

// â”€â”€â”€ UI Construction â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void SWowLoginWidget::Construct(const FArguments& InArgs)
{
    LoginButtonStyle=FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
    ServerRowStyle=LoginButtonStyle;
    // r416 (Stefan 17.10, build 141155: captions "STILL not centered vertically"): SButton takes its
    // padding from the style when it is built; InitializeArtwork set it only afterwards, so r407/r410
    // never reached the buttons and they kept the default 2 px all round (caption centred on the whole
    // texture, 3 units below the art's face). Set here, before any button exists: the face is art rows
    // 8-33 of the 48-row region, centre at row 20.5, so 5 units more below than above.
    // r417 (Stefan 17.10, build 161551: now "Too high", about 4 px): measured on his screenshots,
    // 2 px default padding left the letters 3.5 px low and 0/5 put them 4.5 px high, so the middle
    // is about 2.2 units more below than above.
    LoginButtonStyle.SetNormalPadding(FMargin(4,0,4,2.2f)).SetPressedPadding(FMargin(4,1,4,1.2f));
    ServerRowStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.025f,.035f,.045f,.85f),2.f,FLinearColor(.25f,.27f,.29f),1.f));
    ServerRowStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.10f,.15f,.19f,.95f),2.f,FLinearColor(.65f,.55f,.25f),1.f));
    ServerRowStyle.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.015f,.025f,.035f),2.f,FLinearColor(.85f,.70f,.3f),1.f));
    ServerRowStyle.SetNormalPadding(FMargin(8,5)).SetPressedPadding(FMargin(8,6,8,4));
    ThemeAvailable.Init(false,3);
    if(GConfig)GConfig->GetInt(TEXT("WowUnreal.ClientPreferences"),TEXT("LoginTheme"),SavedTheme,GGameUserSettingsIni);
    if(GConfig)
    {
        GConfig->GetBool(TEXT("WowUnreal.ClientPreferences"),TEXT("LoginUseMods"),bModdedArtwork,GGameUserSettingsIni);
        GConfig->GetString(TEXT("WowUnreal.ClientPreferences"),TEXT("ProjectWebsite"),ProjectWebsite,GGameUserSettingsIni);
    }
    if(SavedTheme>=0 && SavedTheme<3)CurrentExpansion=static_cast<EWowExpansion>(SavedTheme);
    auto Label=[this](const TCHAR* Text,int32 Size=14)
    {
        // r388: the field captions (Size 14 here) are GlueFontNormal, FRIZQT 15 in the glue UI.
        return SNew(STextBlock).Text(FText::FromString(Text)).Font_Lambda([this,Size]{return LoginFont(Size==14?WowLoginGlue::FontPoints(15.f):Size);})
            .ColorAndOpacity_Lambda([]{return FSlateColor(GWowGlueTextColor);})
            .ShadowOffset_Lambda([]{return GWowGlueShadowOffset;})
            .ShadowColorAndOpacity_Lambda([]{return GWowGlueShadowColor;}); // r280: GlueFontNormal
    };
    // r405: the glue button caption (GlueFontNormal size, black outline), also on the link buttons.
    auto GlueCaption=[this](const TCHAR* Text)->TSharedRef<SWidget>
    {
        return SNew(STextBlock).Text(FText::FromString(Text))
            .Font_Lambda([this]{FSlateFontInfo Font=LoginFont(WowLoginGlue::FontPoints(15.f));
                Font.OutlineSettings.OutlineSize=1;Font.OutlineSettings.OutlineColor=FLinearColor(0.f,0.f,0.f,.9f);return Font;})
            .ColorAndOpacity_Lambda([]{return FSlateColor(GWowGlueTextColor);})
            .ShadowOffset_Lambda([]{return GWowGlueShadowOffset;})
            .ShadowColorAndOpacity_Lambda([]{return GWowGlueShadowColor;});
    };
    auto Button=[this](const TCHAR* Text,FOnClicked Action)
    {
        // r396 (Stefan 16.10, our Login button next to the client's): the caption is the glue
        // button font, FRIZQT 15 like GlueFontNormal (it was 10 points, smaller than the field
        // captions), with the client's thin black outline around the gold letters.
        return SNew(SButton).ButtonStyle(&LoginButtonStyle).HAlign(HAlign_Center).VAlign(VAlign_Center)
            .OnClicked(Action)
            [SNew(STextBlock).Text(FText::FromString(Text))
                .Font_Lambda([this]{FSlateFontInfo Font=LoginFont(WowLoginGlue::FontPoints(15.f));
                    Font.OutlineSettings.OutlineSize=1;Font.OutlineSettings.OutlineColor=FLinearColor(0.f,0.f,0.f,.9f);return Font;})
                .ColorAndOpacity_Lambda([]{return FSlateColor(GWowGlueTextColor);})
                .ShadowOffset_Lambda([]{return GWowGlueShadowOffset;})
                .ShadowColorAndOpacity_Lambda([]{return GWowGlueShadowColor;})];
    };
    auto Field=[this](TSharedPtr<SEditableTextBox>& Out,bool Password)
    {
        // r388: the glue edit box - rounded grey tooltip border on the dark tooltip background, the
        // text in ARIALN inset 12 / 5 like the XML's TextInsets.
        TSharedRef<SWowBackdrop> Backdrop=SNew(SWowBackdrop);
        WowLoginGlue::FieldBackdrops.Add(Backdrop);
        ApplyFieldArt(Backdrop);
        const float U=WowLoginGlue::Unit;
        return SNew(SOverlay)
            +SOverlay::Slot()[Backdrop]
            // r389: centred in the box's height (Stefan: "it needs to go a little down").
            +SOverlay::Slot().VAlign(VAlign_Center).Padding(FMargin(12.f*U,0.f,5.f*U,0.f))
            [SAssignNew(Out,SEditableTextBox).Style(&WowLoginGlue::BareTextBoxStyle()).IsPassword(Password)
                .Font_Lambda([this]{auto Font=Fonts.IsInitialized()?Fonts.GetFont(TEXT("Fonts\\ARIALN.TTF"),WowLoginGlue::FontPoints(16.f)):LoginFont(12);Font.OutlineSettings.OutlineSize=0;return Font;})
                .Padding(FMargin(0)).BackgroundColor(FLinearColor::Transparent).ForegroundColor(FLinearColor::White)
                .OnTextCommitted_Lambda([this](const FText&,ETextCommit::Type Type){if(Type==ETextCommit::OnEnter)OnLoginClicked();})];
    };
    auto Canvas=SNew(SConstraintCanvas);
    auto Place=[&](float X,float Y,float W,float H,TSharedRef<SWidget> Widget)
    { Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(X,Y,W,H))[Widget]; };
    // r405 (Stefan 16.10, our Login button against the client's again): the button art is now drawn
    // whole - Glue-Panel-Button-Up-Blue's 148 x 25 art with its ornamented end caps (x 0-148, y 8-33);
    // the old crop (x 8-138, y 7-37) cut both end caps off. r407 (Stefan 17.10: "still look cut at the
    // bottom and also text is cut"): the button is the client's whole texture area, 148 x 48 (TexCoords
    // 0-0.578 x 0-0.75, the art in its upper part), so the rectangle is as tall as GlueButtonTemplate's
    // and the caption has room; a rectangle laid out for the old crop keeps the blue face in place.
    auto PlaceGlue=[&](float X,float Y,float W,float H,TSharedRef<SWidget> Widget)
    { Place(X-W*8.f/130.f,Y-H*7.f/30.f,W*148.f/130.f,H*48.f/30.f,Widget); };
    // r286: the account box follows the theme's UELayout.json rectangles when it has them.
    auto PlaceThemed=[&](const TCHAR* Name,float X,float Y,float W,float H,TSharedRef<SWidget> Widget)
    {
        const FString Key(Name); const FMargin Native(X,Y,W,H);
        Canvas->AddSlot().Alignment(FVector2D::ZeroVector)
            .Offset(TAttribute<FMargin>::CreateLambda([Key,Native]{return LoginRect(*Key,Native);}))[Widget];
    };
    // r385 (Stefan 16.10, against the HD pack's own client at 1920x1080: the logo was about 5 % small):
    // AccountLoginLogo is 256 x 128 at TOPLEFT 3,-7 in the 768-high UI, i.e. x 0.9375 on this 720 canvas.
    Place(2.8f,6.6f,240,120,SNew(SImage).Image_Lambda([this]() -> const FSlateBrush*
    { const int32 I=static_cast<int32>(CurrentExpansion);return LogoBrushes.IsValidIndex(I)&&LogoBrushes[I].IsValid()?LogoBrushes[I].Get():FCoreStyle::Get().GetBrush("NoBrush"); })
        .Visibility_Lambda([]{return GWowLoginLayout.Num()?EVisibility::Collapsed:EVisibility::HitTestInvisible;}));
    // r295 (Stefan 13.10: "ACCOUNT" with a stray ":" after it - the cut-off N of "ACCOUNT NAME" in the
    // D3 patch's wide capitals): the labels shrink to fit their rectangle instead of being clipped.
    auto Fit=[](TSharedRef<SWidget> Content)
    { return SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[Content]; };
    PlaceThemed(TEXT("account_label"),550,336,180,22,SNew(SBox).HAlign(HAlign_Center)[Fit(Label(TEXT("Account Name")))]);
    // r388: 200 x 37 glue units (187.5 x 34.7 here), centred where the old 180 x 27 boxes were.
    PlaceThemed(TEXT("account_field"),546.25f,354.2f,187.5f,34.7f,Field(UsernameBox,false));
    PlaceThemed(TEXT("password_label"),550,403,180,22,SNew(SBox).HAlign(HAlign_Center)[Fit(Label(TEXT("Password")))]);
    PlaceThemed(TEXT("password_field"),546.25f,421.2f,187.5f,34.7f,Field(PasswordBox,true));
    // r389 (Stefan 16.10, our Login button next to the client's): AccountLoginLoginButton is 220 x 45 at
    // BOTTOM 0,170 with the whole 148 x 48 texture area; the visible button (the crop the style draws)
    // is 193 x 28 of it, i.e. x 549.4-730.5, y 524.6-550.9 here - a bit narrower, lower and flatter.
    PlaceThemed(TEXT("login_button"),538.3f,518.5f,206.2f,42.1f, // r407: AccountLoginLoginButton 220 x 45 (x 0.9375), the face where r389 had it
        Button(TEXT("Login"),FOnClicked::CreateSP(this,&SWowLoginWidget::OnLoginClicked)));
    // r286: "Remember Account Name" between the password and Login (after Login in the widget order, so
    // Tab still goes account -> password -> Login). Only the account name is kept, never the password.
    if(GConfig)
    {
        GConfig->GetBool(TEXT("WowUnreal.ClientPreferences"),TEXT("RememberAccountName"),bRememberAccountPref,GGameUserSettingsIni);
        GConfig->GetString(TEXT("WowUnreal.ClientPreferences"),TEXT("RememberedAccountName"),RememberedAccountPref,GGameUserSettingsIni);
    }
    PlaceThemed(TEXT("remember_account"),550,470,180,20,SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [SNew(SCheckBox).IsChecked_Lambda([]{return bRememberAccountPref?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([](ECheckBoxState State)
                {
                    bRememberAccountPref=State==ECheckBoxState::Checked;
                    if(!bRememberAccountPref)RememberedAccountPref.Empty();
                    if(GConfig)
                    {
                        GConfig->SetBool(TEXT("WowUnreal.ClientPreferences"),TEXT("RememberAccountName"),bRememberAccountPref,GGameUserSettingsIni);
                        GConfig->SetString(TEXT("WowUnreal.ClientPreferences"),TEXT("RememberedAccountName"),*RememberedAccountPref,GGameUserSettingsIni);
                        GConfig->Flush(false,GGameUserSettingsIni);
                    }
                })]
        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).HAlign(HAlign_Left).Padding(4,0,0,0)[Fit(Label(TEXT("Remember Account Name"),10))]);
    if(bRememberAccountPref && !RememberedAccountPref.IsEmpty() && UsernameBox.IsValid() && UsernameBox->GetText().IsEmpty())
        UsernameBox->SetText(FText::FromString(RememberedAccountPref));
    Place(520,307,240,20,SAssignNew(ModeLabel,STextBlock).Visibility(EVisibility::Collapsed).Justification(ETextJustify::Center)
        .Font_Lambda([this]{return LoginFont(12);}).ColorAndOpacity(FLinearColor::White));
    PlaceGlue(1140,446,126,26,Button(TEXT("Options"),FOnClicked::CreateLambda([this]{OnOptionsRequested.ExecuteIfBound();return FReply::Handled();})));
    PlaceGlue(1140,665,126,26,Button(TEXT("Quit"),FOnClicked::CreateLambda([this]{OnExitRequested.ExecuteIfBound();return FReply::Handled();})));
    // 133 layout units (~200 px at 1080p) below the logo, leaving room for taller custom logos.
    PlaceGlue(16,265,128,25,SNew(SBox).Visibility_Lambda([this]{return bOfflineMode?EVisibility::Collapsed:EVisibility::Visible;})
        [Button(TEXT("Saved Servers"),FOnClicked::CreateSP(this,&SWowLoginWidget::ToggleServerDrawer))]);
    Place(16,295,252,300,SAssignNew(BookmarkDrawer,SBorder).Visibility(EVisibility::Collapsed).Padding(0)
        .BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
        [BuildServerPanelFrame(SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[Label(TEXT("Saved Servers"),12)]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(22).HeightOverride(20)
                    [SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0).HAlign(HAlign_Center).VAlign(VAlign_Center).OnClicked(this,&SWowLoginWidget::ToggleServerDrawer)[Label(TEXT("X"),10)]]]]
            +SVerticalBox::Slot().FillHeight(1)
                [SNew(SScrollBox).ScrollBarThickness(FVector2D(6,6))
                    +SScrollBox::Slot()[SAssignNew(BookmarkList,SVerticalBox)]]
            +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
                [SNew(SBox).HeightOverride(37)[Button(TEXT("Edit server address"),FOnClicked::CreateLambda([this]
                    {bServerEditorOpen=!bServerEditorOpen;return FReply::Handled();}))]]
            +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                [SNew(SVerticalBox).Visibility_Lambda([this]{return bServerEditorOpen?EVisibility::Visible:EVisibility::Collapsed;})
                    +SVerticalBox::Slot().AutoHeight()[Field(ServerBox,false)]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                        [SNew(SBox).HeightOverride(37)[Button(TEXT("Save Server"),FOnClicked::CreateSP(this,&SWowLoginWidget::SaveServer))]]])]);
    ServerBox->SetFont(LoginFont(10));
    // Version/background selection lives in Options alongside HD artwork.
    auto Link=[&](const TCHAR* Name,float Y,TFunction<FString()> Url)
    {
        PlaceGlue(1140,Y,126,26,SNew(SButton).ButtonStyle(&LoginButtonStyle)
            .HAlign(HAlign_Center).VAlign(VAlign_Center).IsEnabled_Lambda([Url]{return IsWebUrl(Url());})
            .OnClicked_Lambda([Url]{const FString Value=Url();if(IsWebUrl(Value))FPlatformProcess::LaunchURL(*Value,nullptr,nullptr);return FReply::Handled();})
            [GlueCaption(Name)]);
    };
    PlaceGlue(1140,480,126,26,Button(TEXT("Credits"),FOnClicked::CreateLambda([this]{CreditsPanel->SetVisibility(EVisibility::Visible);return FReply::Handled();})));
    Link(TEXT("Terms of Use"),514,[this]{return GetTermsUrl();});
    Link(TEXT("Official Site"),548,[this]{return GetWebsiteUrl();});
    Link(TEXT("Server Account"),582,[this]{return GetAccountUrl();});
    Link(TEXT("Project Website"),616,[this]{return ProjectWebsite;});
    Place(380,140,520,360,SAssignNew(CreditsPanel,SBorder).Visibility(EVisibility::Collapsed)
        .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(.02f,.02f,.03f,.98f)).Padding(20)
        [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Label(TEXT("Credits"),22)]
            +SVerticalBox::Slot().FillHeight(1).Padding(0,16)
                [SNew(SScrollBox)+SScrollBox::Slot()
                [SNew(STextBlock).AutoWrapText(true).Font_Lambda([this]{return LoginFont(10);})
                .Text(FText::FromString(TEXT(
                    "WoWUnreal contributors\n\n"
                    "World of Warcraft artwork and original game data: Blizzard Entertainment.\n"
                    "Engine: Unreal Engine / Epic Games.\n"
                    "MPQ reading: StormLib contributors.\n"
                    "Lua 5.1: Lua.org, PUC-Rio.\n"
                    "XML parsing: pugixml (Arseny Kapoulkine).\n"
                    "Upscaling / frame generation: AMD FidelityFX.\n\n"
                    "Ideas and inspiration\n"
                    "Loot beams (Options > Experimental): inspired by Loot Beam by manatactical, "
                    "github.com/manatactical/wxl-loot-beam - including support for its server module mod-loot-beam. "
                    "Our implementation is written independently.\n\n"
                    "An independent project; not an official Blizzard client.")))]]
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [SNew(SBox).WidthOverride(130).HeightOverride(27)[Button(TEXT("Close"),FOnClicked::CreateLambda([this]{CreditsPanel->SetVisibility(EVisibility::Collapsed);return FReply::Handled();}))]]]);
    Place(16,695,430,20,Label(TEXT("WoWUnreal  |  3.3.5a (12340)"),11));
    Place(870,696,394,18,SNew(STextBlock).Justification(ETextJustify::Right)
        .Text(FText::FromString(TEXT("Login artwork comes from your local data")))
        .Font_Lambda([this]{return LoginFont(10);}).ColorAndOpacity(FLinearColor(.7f,.7f,.7f)));
    ChildSlot[SNew(SOverlay)
        +SOverlay::Slot()[SAssignNew(BackgroundBorder,SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
            .BorderBackgroundColor(GetBackgroundColor()).Visibility(EVisibility::HitTestInvisible)]
        +SOverlay::Slot()[SAssignNew(BackgroundImage,SImage).Visibility(EVisibility::Collapsed)]
        +SOverlay::Slot()[SAssignNew(CinematicImage,SImage).Visibility(EVisibility::Collapsed)]
        +SOverlay::Slot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
            [SNew(SBox).WidthOverride(1280).HeightOverride(720)[Canvas]]]];
}

void SWowLoginWidget::ApplyFieldArt(const TSharedRef<SWowBackdrop>& Backdrop) const
{
    const float U=WowLoginGlue::Unit;
    Backdrop->SetBackdrop(FieldBackground,FieldBorder,16.f*U,true,16.f*U,FMargin(10.f*U,4.f*U,5.f*U,9.f*U));
    // AccountLogin.lua: DEFAULT_TOOLTIP_COLOR = {0.8, 0.8, 0.8, 0.09, 0.09, 0.09} (border, background).
    Backdrop->SetBorderColor(FLinearColor(FColor(204,204,204)));
    Backdrop->SetBackgroundColor(FLinearColor(FColor(23,23,23)));
}

FSlateFontInfo SWowLoginWidget::LoginFont(int32 Size) const
{
    auto Font=Fonts.IsInitialized()?Fonts.GetDefaultFont(Size):FCoreStyle::GetDefaultFontStyle("Regular",Size);
    Font.OutlineSettings.OutlineSize=1;
    return Font;
}

void SWowLoginWidget::InitializeArtwork(FMpqManager* Mpq)
{
    if(!Mpq || !Mpq->IsInitialized())return;
    if(!Fonts.IsInitialized())Fonts.Initialize(Mpq);
    ReadGlueFontNormal(Mpq); // r280
    ReadLoginLayout(Mpq); // r286
    auto Load=[&](const TCHAR* Path) -> TSharedPtr<FSlateBrush>
    {
        TArray<uint8> Data;if(!Mpq->ReadFile(Path,Data))return nullptr;
        auto Blp=FBlpParser::Parse(Data);if(!Blp.bIsValid)return nullptr;
        UTexture2D* Texture=FWowTextureFactory::CreateTexture(Blp,Path);if(!Texture)return nullptr;
        ArtTextures.Emplace(Texture);
        auto Brush=MakeShared<FSlateBrush>();Brush->SetResourceObject(Texture);
        Brush->ImageSize=FVector2D(Texture->GetSizeX(),Texture->GetSizeY());
        Brush->DrawAs=ESlateBrushDrawType::Image;return Brush;
    };
    LogoBrushes.Empty();PanelEdges.Empty();ArtTextures.Empty();
    FieldBackground=nullptr;FieldBorder=nullptr; // r388: released with ArtTextures; set again below
    for(int32 I=0;I<3;++I)
    {
        const auto Theme=static_cast<EWowExpansion>(I);
        ThemeAvailable[I]=Mpq->FileExists(WowLoginTheme::Model(Theme));
        LogoBrushes.Add(Load(WowLoginTheme::Logo(Theme)));
    }
    if(auto Border=Load(TEXT("Interface\\DialogFrame\\UI-DialogBox-Border.blp"));Border.IsValid())
        for(int32 I=0;I<8;++I)
        {
            auto Edge=MakeShared<FSlateBrush>(*Border);
            Edge->SetUVRegion(FBox2f(FVector2f(I/8.f,0),FVector2f((I+1)/8.f,1)));
            PanelEdges.Add(Edge);
        }
    // r388: the edit boxes' glue backdrop art.
    if(auto FieldBg=Load(TEXT("Interface\\Tooltips\\UI-Tooltip-Background.blp"));FieldBg.IsValid())
        FieldBackground=Cast<UTexture2D>(FieldBg->GetResourceObject());
    if(auto FieldEdge=Load(TEXT("Interface\\Glues\\Common\\Glue-Tooltip-Border.blp"));FieldEdge.IsValid())
        FieldBorder=Cast<UTexture2D>(FieldEdge->GetResourceObject());
    for(int32 I=WowLoginGlue::FieldBackdrops.Num()-1;I>=0;--I)
    {
        const TSharedPtr<SWowBackdrop> Backdrop=WowLoginGlue::FieldBackdrops[I].Pin();
        if(Backdrop.IsValid())ApplyFieldArt(Backdrop.ToSharedRef());else WowLoginGlue::FieldBackdrops.RemoveAt(I);
    }
    auto Normal=Load(TEXT("Interface\\Glues\\Common\\Glue-Panel-Button-Up-Blue.blp"));
    auto Pressed=Load(TEXT("Interface\\Glues\\Common\\Glue-Panel-Button-Down-Blue.blp"));
    // Crop the transparent atlas padding evenly: Slate centers content in this rectangle.
    for (auto Brush : {Normal, Pressed})
        if (Brush.IsValid()) Brush->SetUVRegion(FBox2f(FVector2f(0.f,0.f),FVector2f(148.f/256.f,48.f/64.f))); // r405/r407: the client's TexCoords, art and caps whole
    // r389: hover brightens like the client's additive Glue-Panel-Button-Highlight-Blue.
    if(Normal.IsValid())
    {
        FSlateBrush Hover=*Normal;Hover.TintColor=FLinearColor(1.22f,1.22f,1.3f);
        LoginButtonStyle.SetNormal(*Normal).SetHovered(Hover).SetDisabled(*Normal);
    }
    if(Pressed.IsValid())LoginButtonStyle.SetPressed(*Pressed);
    // r407: the art fills the upper 25 of the 48 texture rows (centre at row 20.5), so the caption is
    // centred there: 6 units more padding below than above.
    // r410 (Stefan 17.10, build 010741: captions sat about 4 px below the middle of the blue face):
    // the line box was centred on the face (art rows 8-33 of 48), but the font's ascender room above
    // the capitals put the visible letters low. 7 more units of bottom padding lift them by half that.
    LoginButtonStyle.SetNormalPadding(FMargin(4,0,4,2.2f)).SetPressedPadding(FMargin(4,1,4,1.2f)); // r416/r417: same as in Construct
    // One version selector controls local artwork and the saved-server list.
    int32 Choice = SavedTheme;
    FString Override;
    if (FParse::Value(FCommandLine::Get(), TEXT("-loginexpansion="), Override))
    {
        Override.ToLowerInline();
        if (Override == TEXT("classic") || Override == TEXT("vanilla")) Choice = 0;
        else if (Override == TEXT("tbc") || Override == TEXT("burningcrusade")) Choice = 1;
        else if (Override == TEXT("wotlk") || Override == TEXT("wrath")) Choice = 2;
    }
    SetExpansion(WowLoginTheme::Resolve(Choice,ThemeAvailable));
    SetServerVersion(static_cast<int32>(CurrentExpansion));
}

TSharedRef<SWidget> SWowLoginWidget::BuildExpansionTabs()
{
    auto Tabs=SNew(SHorizontalBox);
    const TCHAR* Names[]={TEXT("Auto"),TEXT("Vanilla"),TEXT("Burning Crusade"),TEXT("Wrath")};
    for(int32 Choice=-1;Choice<3;++Choice)
        Tabs->AddSlot().FillWidth(Choice==1?1.6f:1.f).Padding(2)
        [SNew(SButton).ButtonStyle(&LoginButtonStyle).HAlign(HAlign_Center).VAlign(VAlign_Center)
            .IsEnabled_Lambda([this,Choice]{return Choice<0 || (ThemeAvailable.IsValidIndex(Choice)&&ThemeAvailable[Choice]);})
            .OnClicked_Lambda([this,Choice]
            {
                SavedTheme=Choice;
                if(GConfig){GConfig->SetInt(TEXT("WowUnreal.ClientPreferences"),TEXT("LoginTheme"),Choice,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);}
                SetExpansion(WowLoginTheme::Resolve(Choice,ThemeAvailable));
    SetServerVersion(static_cast<int32>(CurrentExpansion));
                return FReply::Handled();
            })[SNew(STextBlock).Text(FText::FromString(Names[Choice+1])).Font_Lambda([this]{return LoginFont(10);})
                .ColorAndOpacity_Lambda([this,Choice]{return SavedTheme==Choice?FLinearColor::White:FLinearColor(1.f,.82f,0.f);})]];
    return SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Game version / background"))).Font_Lambda([this]{return LoginFont(11);})]
        +SVerticalBox::Slot().FillHeight(1)[Tabs];
}

void SWowLoginWidget::SetCinematicTexture(UMediaTexture* InTexture)
{
    if (!CinematicImage.IsValid()) return;

    if (InTexture)
    {
        // Slate brushes do not keep transient UObjects alive. Hold a strong
        // reference for as long as the login widget can paint this texture.
        CinematicTexture.Reset(InTexture);
        CinematicBrush = MakeShared<FSlateBrush>();
        CinematicBrush->SetResourceObject(CinematicTexture.Get());
        CinematicBrush->ImageSize = FVector2D(1920, 1080);
        CinematicBrush->DrawAs = ESlateBrushDrawType::Image;
        CinematicBrush->Tiling = ESlateBrushTileType::NoTile;

        CinematicImage->SetImage(CinematicBrush.Get());
        CinematicImage->SetVisibility(EVisibility::SelfHitTestInvisible);
        RefreshFallbackVisuals();

        // Make the color overlay semi-transparent so cinematic shows through
        if (BackgroundBorder.IsValid())
        {
            FLinearColor BgColor = GetBackgroundColor();
            BgColor.A = 0.3f; // Let video show through
            BackgroundBorder->SetBorderBackgroundColor(BgColor);
        }
    }
    else
    {
        CinematicImage->SetVisibility(EVisibility::Collapsed);
        CinematicImage->SetImage(FCoreStyle::Get().GetBrush("NoBrush"));
        CinematicBrush.Reset();
        CinematicTexture.Reset();
        RefreshFallbackVisuals();

        // Restore opaque background
        if (BackgroundBorder.IsValid())
        {
            BackgroundBorder->SetBorderBackgroundColor(GetBackgroundColor());
        }
    }
}

void SWowLoginWidget::SetBackgroundTexture(UTexture2D* InTexture)
{
    if (!BackgroundImage.IsValid()) return;

    if (InTexture)
    {
        // Runtime MPQ textures are transient. The brush alone is not a GC
        // reference, so retain the texture until this screen is removed.
        BackgroundTexture.Reset(InTexture);
        BackgroundTextureBrush = MakeShared<FSlateBrush>();
        BackgroundTextureBrush->SetResourceObject(BackgroundTexture.Get());
        BackgroundTextureBrush->ImageSize = FVector2D(1920, 1080);
        BackgroundTextureBrush->DrawAs = ESlateBrushDrawType::Image;
        BackgroundTextureBrush->Tiling = ESlateBrushTileType::NoTile;

        BackgroundImage->SetImage(BackgroundTextureBrush.Get());
        BackgroundImage->SetVisibility(EVisibility::SelfHitTestInvisible);
        // The fallback plate fills the viewport without a one-sided crop.
        BackgroundImage->SetRenderTransform(FSlateRenderTransform(
            FScale2D(1.f), FVector2D::ZeroVector));
        RefreshFallbackVisuals();

        if (BackgroundBorder.IsValid())
        {
            FLinearColor BgColor = GetBackgroundColor();
            BgColor.A = 0.18f;
            BackgroundBorder->SetBorderBackgroundColor(BgColor);
        }
        if (SubtitleText.IsValid())
        {
            SubtitleText->SetVisibility(EVisibility::Collapsed);
        }
        if (TitleText.IsValid())
        {
            TitleText->SetVisibility(EVisibility::Collapsed);
        }
    }
    else
    {
        BackgroundImage->SetVisibility(EVisibility::Collapsed);
        BackgroundImage->SetRenderTransform(FSlateRenderTransform());
        BackgroundImage->SetImage(FCoreStyle::Get().GetBrush("NoBrush"));
        BackgroundTextureBrush.Reset();
        BackgroundTexture.Reset();
        RefreshFallbackVisuals();

        if (BackgroundBorder.IsValid())
        {
            BackgroundBorder->SetBorderBackgroundColor(GetBackgroundColor());
        }
        if (SubtitleText.IsValid())
        {
            SubtitleText->SetVisibility(EVisibility::SelfHitTestInvisible);
        }
        if (TitleText.IsValid())
        {
            TitleText->SetVisibility(EVisibility::SelfHitTestInvisible);
        }
    }
}

void SWowLoginWidget::SetWorldSceneBackgroundActive(bool bActive)
{
    bWorldSceneBackgroundActive = bActive;
    if (bActive)
    {
        if (BackgroundImage.IsValid())
        {
            BackgroundImage->SetVisibility(EVisibility::Collapsed);
            BackgroundImage->SetRenderTransform(FSlateRenderTransform());
        }
        if (CinematicImage.IsValid())
        {
            CinematicImage->SetVisibility(EVisibility::Collapsed);
        }
        if (BackgroundBorder.IsValid())
        {
            FLinearColor BgColor = GetBackgroundColor();
            BgColor.A = 0.06f;
            BackgroundBorder->SetBorderBackgroundColor(BgColor);
        }
        if (SubtitleText.IsValid())
        {
            SubtitleText->SetVisibility(EVisibility::Collapsed);
        }
        if (TitleText.IsValid())
        {
            TitleText->SetVisibility(EVisibility::Collapsed);
        }
        RefreshFallbackVisuals();
    }
    else if (BackgroundBorder.IsValid())
    {
        BackgroundBorder->SetBorderBackgroundColor(GetBackgroundColor());
        RefreshFallbackVisuals();
    }
}

void SWowLoginWidget::SetExpansion(EWowExpansion Expansion)
{
    if(static_cast<uint8>(Expansion)>=3)return;
    CurrentExpansion = Expansion;

    // Update visuals
    if (BackgroundBorder.IsValid())
    {
        // If cinematic is playing, keep semi-transparent; otherwise opaque
        FLinearColor BgColor = GetBackgroundColor();
        if ((CinematicImage.IsValid() && CinematicImage->GetVisibility() != EVisibility::Collapsed)
            || (BackgroundImage.IsValid() && BackgroundImage->GetVisibility() != EVisibility::Collapsed))
        {
            BgColor.A = 0.18f;
        }
        BackgroundBorder->SetBorderBackgroundColor(BgColor);
    }
    if (TitleText.IsValid())
    {
        TitleText->SetText(FText::FromString(GetExpansionTitle()));
        TitleText->SetColorAndOpacity(GetAccentColor());
    }
    if (SubtitleText.IsValid())
    {
        SubtitleText->SetText(FText::FromString(GetExpansionSubtitle()));
    }
    RefreshFallbackVisuals();

    // Notify controller to switch cinematic
    OnExpansionChanged.ExecuteIfBound(static_cast<uint8>(Expansion));
}

bool SWowLoginWidget::HasExternalBackground() const
{
    return bWorldSceneBackgroundActive
        || (BackgroundImage.IsValid() && BackgroundImage->GetVisibility() != EVisibility::Collapsed)
        || (CinematicImage.IsValid() && CinematicImage->GetVisibility() != EVisibility::Collapsed);
}

void SWowLoginWidget::RefreshFallbackVisuals()
{
    const EVisibility FallbackVisibility = HasExternalBackground()
        ? EVisibility::Collapsed
        : EVisibility::SelfHitTestInvisible;

    if (FallbackLeftPanel.IsValid())
    {
        FallbackLeftPanel->SetVisibility(FallbackVisibility);
        FallbackLeftPanel->SetBorderBackgroundColor(GetFallbackSideColor(false));
    }
    if (FallbackRightPanel.IsValid())
    {
        FallbackRightPanel->SetVisibility(FallbackVisibility);
        FallbackRightPanel->SetBorderBackgroundColor(GetFallbackSideColor(true));
    }
    if (FallbackCenterPanel.IsValid())
    {
        FallbackCenterPanel->SetVisibility(FallbackVisibility);
        FallbackCenterPanel->SetBorderBackgroundColor(GetFallbackCenterColor());
    }
    if (FallbackTopBand.IsValid())
    {
        FallbackTopBand->SetVisibility(FallbackVisibility);
        FallbackTopBand->SetBorderBackgroundColor(GetFallbackBandColor());
    }
    if (FallbackBottomBand.IsValid())
    {
        FallbackBottomBand->SetVisibility(FallbackVisibility);
        FallbackBottomBand->SetBorderBackgroundColor(GetFallbackBandColor());
    }
}

// â”€â”€â”€ Login logic â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

FReply SWowLoginWidget::OnLoginClicked()
{
    FString ServerStr = ServerBox.IsValid() ? ServerBox->GetText().ToString().TrimStartAndEnd() : TEXT("127.0.0.1:3724");
    FString User = UsernameBox.IsValid() ? UsernameBox->GetText().ToString().TrimStartAndEnd() : TEXT("");
    FString Pass = PasswordBox.IsValid() ? PasswordBox->GetText().ToString() : TEXT("");

    FString Host = ServerStr;
    int32 Port = 3724;
    int32 ColonIdx;
    if (ServerStr.FindChar(TEXT(':'), ColonIdx))
    {
        Host = ServerStr.Left(ColonIdx);
        Port = FCString::Atoi(*ServerStr.Mid(ColonIdx + 1));
    }

    if (User.IsEmpty())
    {
        OnNotice.ExecuteIfBound(TEXT("Please enter your account name"));
        return FReply::Handled();
    }


    // r286: Remember Account Name keeps only the account name.
    if (bRememberAccountPref && GConfig && User != TEXT("Offline"))
    {
        RememberedAccountPref = User;
        GConfig->SetString(TEXT("WowUnreal.ClientPreferences"), TEXT("RememberedAccountName"), *RememberedAccountPref, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
    OnLoginSubmit.ExecuteIfBound(Host, Port, User, Pass);
    return FReply::Handled();
}

void SWowLoginWidget::SetCredentials(const FString& Server, const FString& Username, const FString& Password)
{
    WebsiteUrl.Empty();AccountUrl.Empty();TermsUrl.Empty();
    if (ServerBox.IsValid()) ServerBox->SetText(FText::FromString(Server));
    // r286: an empty account name falls back to the remembered one.
    const FString Account = Username.IsEmpty() && bRememberAccountPref ? RememberedAccountPref : Username;
    if (UsernameBox.IsValid()) UsernameBox->SetText(FText::FromString(Account));
    if (PasswordBox.IsValid()) PasswordBox->SetText(FText::FromString(Password));
    for(const auto& Profile:SavedProfiles)
        if(Profile.GameVersion==ServerVersion && Profile.Username==Username &&
            FString::Printf(TEXT("%s:%d"),*Profile.ServerAddress,Profile.AuthPort)==Server)
        {
            WebsiteUrl=Profile.WebsiteUrl;
            AccountUrl=Profile.AccountUrl;
            TermsUrl=Profile.TermsUrl;
            break;
        }
}

void SWowLoginWidget::SetOfflineMode(bool bEnabled)
{
	bOfflineMode = bEnabled;
	if (ModeLabel.IsValid())
	{
		ModeLabel->SetText(FText::FromString(bOfflineMode ? TEXT("Offline") : TEXT("")));
	}
	if (bOfflineMode)
	{
		SetCredentials(TEXT("Offline"), TEXT("Offline"), FString());
	}
	if (BookmarkDrawer.IsValid())
	{
		BookmarkDrawer->SetVisibility(!bOfflineMode && bRealmDrawerOpen
			? EVisibility::Visible
			: EVisibility::Collapsed);
	}
}

void SWowLoginWidget::SetSavedProfiles(const TArray<FWowCredential>& Profiles)
{
    SavedProfiles=Profiles;
    RefreshSavedProfiles();
}

void SWowLoginWidget::RefreshSavedProfiles()
{
	if (!BookmarkList.IsValid())
	{
		return;
	}

	BookmarkList->ClearChildren();
	for (const FWowCredential& Profile : SavedProfiles)
	{
		if(Profile.GameVersion!=ServerVersion)continue;
		const FString Server = FString::Printf(TEXT("%s:%d"), *Profile.ServerAddress, Profile.AuthPort);
		const FString Label = Profile.Alias.IsEmpty() ? Profile.ServerAddress : Profile.Alias;
		BookmarkList->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
		[
			SNew(SButton).ButtonStyle(&ServerRowStyle).HAlign(HAlign_Fill)
			.OnClicked_Lambda([this, Server, Profile]() -> FReply
			{
				SetCredentials(Server, Profile.Username, UWowCredentialStore::ResolvePassword(Profile));
                WebsiteUrl=Profile.WebsiteUrl;
                AccountUrl=Profile.AccountUrl;
                TermsUrl=Profile.TermsUrl;
				return FReply::Handled();
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Label)).OverflowPolicy(ETextOverflowPolicy::Ellipsis).ToolTipText(FText::FromString(Server))
					.Font_Lambda([this]{return LoginFont(10);})
					.ColorAndOpacity(FLinearColor(1.0f, 0.84f, 0.0f))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Profile.Username)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Font_Lambda([this]{return LoginFont(8);})
					.ColorAndOpacity(FLinearColor(0.72f, 0.72f, 0.72f))
				]
			]
		];
	}

	if (BookmarkDrawer.IsValid())
	{
		BookmarkDrawer->SetVisibility(!bOfflineMode && bRealmDrawerOpen
			? EVisibility::Visible
			: EVisibility::Collapsed);
	}
}

void SWowLoginWidget::AutoSubmit()
{
    OnLoginClicked();
}

void SWowLoginWidget::SetStatusText(const FString& Text)
{
    if (StatusLabel.IsValid())
    {
        StatusLabel->SetText(FText::FromString(Text));
    }
}


TSharedRef<SWidget> SWowLoginWidget::BuildServerPanelFrame(TSharedRef<SWidget> Content)
{
    return SNew(SLoginServerFrame,&PanelEdges)
        .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
        .BorderBackgroundColor(FLinearColor(.025f,.028f,.032f,.88f)).Padding(16)[Content];
}

FReply SWowLoginWidget::ToggleServerDrawer()
{
    bRealmDrawerOpen=!bRealmDrawerOpen;
    RefreshSavedProfiles();
    return FReply::Handled();
}

void SWowLoginWidget::SetServerVersion(int32 Version)
{
    if(Version<0 || Version>2 || Version==ServerVersion)return;
    ServerVersion=Version;
    SetCredentials(TEXT(""),TEXT(""),TEXT(""));
    WebsiteUrl.Empty();
    AccountUrl.Empty();
    TermsUrl.Empty();
    RefreshSavedProfiles();
    SetStatusText(Version==2?TEXT(""):TEXT("Server bookmarks only; this client connects to WotLK 3.3.5a."));
}

FString SWowLoginWidget::GetWebsiteUrl() const {return WebsiteUrl.TrimStartAndEnd();}
FString SWowLoginWidget::GetAccountUrl() const {return AccountUrl.TrimStartAndEnd();}
FString SWowLoginWidget::GetTermsUrl() const {return TermsUrl.TrimStartAndEnd();}

bool SWowLoginWidget::IsWebUrl(const FString& Url)
{
    int32 Start=Url.StartsWith(TEXT("https://"),ESearchCase::IgnoreCase)?8:
        (Url.StartsWith(TEXT("http://"),ESearchCase::IgnoreCase)?7:0);
    if(Start==0 || Url.Len()<=Start)return false;
    for(TCHAR C:Url)if(FChar::IsWhitespace(C)||C<TEXT(' ')||C==TEXT('\\')||C==TEXT('"'))return false;
    FString Authority=Url.Mid(Start);
    int32 End=0;
    for(int32 I=0;I<Authority.Len();++I)if(Authority[I]==TEXT('/')||Authority[I]==TEXT('?')||Authority[I]==TEXT('#')){End=I;break;}
    if(End>0)Authority=Authority.Left(End);
    return !Authority.IsEmpty() && Authority[0]!=TEXT('/') && Authority[0]!=TEXT('?') && Authority[0]!=TEXT('#') && !Authority.Contains(TEXT("@"));
}

FReply SWowLoginWidget::SaveServer()
{
    const FString Server=ServerBox->GetText().ToString().TrimStartAndEnd();
    const FString User=UsernameBox->GetText().ToString().TrimStartAndEnd();
    if(Server.IsEmpty()||User.IsEmpty())
    {
        OnNotice.ExecuteIfBound(TEXT("Enter the server address and account name before saving."));
        return FReply::Handled();
    }
    for(const FString& Url:{GetWebsiteUrl(),GetAccountUrl(),GetTermsUrl()})
        if(!Url.IsEmpty()&&!IsWebUrl(Url))
        {
            OnNotice.ExecuteIfBound(TEXT("Optional links must be full http:// or https:// website addresses."));
            return FReply::Handled();
        }
    FString Host=Server;int32 Port=3724,Colon;
    if(Server.FindChar(TEXT(':'),Colon))
    {
        Host=Server.Left(Colon);const FString PortText=Server.Mid(Colon+1);
        if(!PortText.IsNumeric()){OnNotice.ExecuteIfBound(TEXT("Enter a valid server port."));return FReply::Handled();}
        Port=FCString::Atoi(*PortText);
    }
    if(Host.IsEmpty()||Port<1||Port>65535){OnNotice.ExecuteIfBound(TEXT("Enter a valid server address and port."));return FReply::Handled();}
    OnSaveServer.ExecuteIfBound(Host,Port,User,PasswordBox->GetText().ToString());
    return FReply::Handled();
}
