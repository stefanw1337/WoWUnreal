#include "WowNameplateWidget.h"
#include "Styling/CoreStyle.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Blueprint/WidgetTree.h"
#include "WowEntity.h"
#include "WowFontManager.h"
#include "WowVRSupport.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Fonts/CompositeFont.h"

// r381 (Stefan 16.10: "the ? and ! on top of NPCs should be bigger and in 3D"): the marker is a stack
// of the same glyph - darker copies stepped down-right behind a bright face with a pale rim at its
// top-left - so it reads as a solid, bevelled gold sign like the client's, not a flat letter.
// r387 (Stefan 16.10: "now make them rounder"): every layer gets an outline in its own colour, which
// Slate draws with round joins, so the glyph turns plump with rounded ends and corners; the back
// layer keeps a dark edge around the whole sign.
namespace WowQuestMarkerLook
{
    constexpr int32 DepthLayers = 5;
    constexpr int32 DesktopSize = 40, VrSize = 64;

    // r402 (Stefan 16.10: "another font on those ! and ?'s - they're too square"): the marks are drawn in
    // MORPHEUS (WoW's round, curly heading font, extracted to Saved/Fonts by the font manager) instead of
    // the engine's square bold sans. Until the font is there, the bold sans is used.
    FSlateFontInfo MarkerFace(int32 Size)
    {
        static TSharedPtr<const FCompositeFont> Morpheus;
        if (!Morpheus.IsValid())
        {
            const FString Path = FPaths::ProjectSavedDir() / TEXT("Fonts/MORPHEUS.TTF");
            if (IFileManager::Get().FileExists(*Path))
                Morpheus = MakeShared<FCompositeFont>(FName(TEXT("QuestMarker")), Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
        }
        return Morpheus.IsValid() ? FSlateFontInfo(Morpheus, Size) : FCoreStyle::GetDefaultFontStyle("Bold", Size);
    }

    void Apply(UTextBlock* Face, const TCHAR* Glyph, const FLinearColor& FaceColor, bool bShow)
    {
        if (!Face) return;
        UPanelWidget* Stack = Face->GetParent();
        const int32 Want = FWowVRSupport::IsActive() ? VrSize : DesktopSize;
        const int32 Count = Stack ? Stack->GetChildrenCount() : 0;
        for (int32 Index = 0; Index < FMath::Max(Count, 1); ++Index)
        {
            UTextBlock* Layer = Stack ? Cast<UTextBlock>(Stack->GetChildAt(Index)) : Face;
            if (!Layer) continue;
            Layer->SetText(FText::FromString(Glyph));
            FLinearColor Color = FaceColor;
            if (Layer != Face)
            {
                // Children 0..DepthLayers-1 are the side, back to front; the last one before the face is the rim.
                const bool bRim = Index == Count - 2;
                const float Shade = bRim ? 1.f : .28f + .3f * Index / FMath::Max(1, DepthLayers - 1);
                Color = bRim ? FLinearColor(FMath::Min(1.f, FaceColor.R * 1.1f + .25f), FMath::Min(1.f, FaceColor.G * 1.1f + .25f), FMath::Min(1.f, FaceColor.B + .35f), 1.f)
                    : FLinearColor(FaceColor.R * Shade, FaceColor.G * Shade, FaceColor.B * Shade, 1.f);
            }
            // Rounding: an outline about 4 % of the glyph size in the layer's own colour (the back
            // layer one step wider and dark, for the silhouette). r390 (Stefan 16.10: "a bit too fat
            // - a thin black outline should look better and separate the dot of the ?"): 8 % made the
            // glyph fat and closed the gap under the hook; the face now has a thin black outline.
            const bool bBackLayer = Stack && Index == 0;
            const bool bFaceLayer = Layer == Face;
            const int32 Round = FMath::Max(1, FMath::RoundToInt(Want * .04f));
            FSlateFontInfo LayerFont = Layer->GetFont();
            const FSlateFontInfo Face = MarkerFace(Want);
            const bool bFaceChanged = LayerFont.FontObject != Face.FontObject || LayerFont.CompositeFont != Face.CompositeFont;
            if (bFaceChanged) { const FFontOutlineSettings KeptOutline = LayerFont.OutlineSettings; LayerFont = Face; LayerFont.OutlineSettings = KeptOutline; LayerFont.Size = -1; }
            const FLinearColor EdgeColor = bBackLayer ? FLinearColor(.06f, .04f, 0.f, .9f)
                : bFaceLayer ? FLinearColor(0.f, 0.f, 0.f, .9f) : Color;
            const int32 EdgeSize = bBackLayer ? Round + 1 : bFaceLayer ? FMath::Max(1, FMath::RoundToInt(Want * .04f)) : Round;
            if (LayerFont.Size != Want || LayerFont.OutlineSettings.OutlineSize != EdgeSize
                || !LayerFont.OutlineSettings.OutlineColor.Equals(EdgeColor))
            {
                LayerFont.Size = Want;
                LayerFont.OutlineSettings.OutlineSize = EdgeSize;
                LayerFont.OutlineSettings.OutlineColor = EdgeColor;
                Layer->SetFont(LayerFont);
            }
            Layer->SetColorAndOpacity(Color);
            Layer->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        }
        if (Stack) Stack->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
}

TSharedRef<SWidget> UWowNameplateWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree=NewObject<UWidgetTree>(this);

    // This class is instantiated directly by the world actor component, so it
    // needs a native widget tree when no optional Blueprint bindings exist.
    if (!WidgetTree->RootWidget)
    {
        UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NameplateRoot"));
        WidgetTree->RootWidget = Root;

        // The component's pivot is the bottom centre of the draw area: keep the stack at the
        // bottom so the name sits right above the head and does not jump when the quest
        // marker, subtitle or health bar appear.
        USpacer* TopFill = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("NameplateTopFill"));
        if (UVerticalBoxSlot* FillSlot = Root->AddChildToVerticalBox(TopFill))
            FillSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

        // r381: the marker's 3D stack (WowQuestMarkerLook): side layers, rim, then the face.
        UOverlay* MarkerStack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("QuestMarkerStack"));
        FSlateFontInfo MarkerFont = FCoreStyle::GetDefaultFontStyle("Bold", WowQuestMarkerLook::DesktopSize);
        auto AddMarkerLayer = [&](const FName Name, const FVector2D& Offset, bool bOutline)
        {
            UTextBlock* Layer = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
            FSlateFontInfo LayerFont = MarkerFont;
            LayerFont.OutlineSettings.OutlineSize = bOutline ? 2 : 0;
            LayerFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, .85f);
            Layer->SetFont(LayerFont);
            Layer->SetJustification(ETextJustify::Center);
            Layer->SetRenderTranslation(Offset);
            Layer->SetVisibility(ESlateVisibility::Collapsed);
            if (UOverlaySlot* LayerSlot = MarkerStack->AddChildToOverlay(Layer))
            {
                LayerSlot->SetHorizontalAlignment(HAlign_Center);
                LayerSlot->SetVerticalAlignment(VAlign_Bottom);
            }
            return Layer;
        };
        for (int32 Depth = WowQuestMarkerLook::DepthLayers; Depth >= 1; --Depth)
            AddMarkerLayer(*FString::Printf(TEXT("QuestMarkerSide%d"), Depth), FVector2D(.8f, 1.2f) * Depth, Depth == WowQuestMarkerLook::DepthLayers);
        AddMarkerLayer(TEXT("QuestMarkerRim"), FVector2D(-.8f, -.8f), false);
        QuestMarkerText = AddMarkerLayer(TEXT("QuestMarkerText"), FVector2D::ZeroVector, false);
        MarkerStack->SetVisibility(ESlateVisibility::Collapsed);
        if (UVerticalBoxSlot* MarkerSlot = Root->AddChildToVerticalBox(MarkerStack))
            MarkerSlot->SetHorizontalAlignment(HAlign_Center);

        NameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NameText"));
        FSlateFontInfo NameFont = FCoreStyle::GetDefaultFontStyle("Regular",13);
        NameFont.Size = 13;
        NameText->SetFont(NameFont);
        NameText->SetJustification(ETextJustify::Center);
        NameText->SetShadowOffset(FVector2D(1.0f, 1.0f));
        NameText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.95f));
        // Name row: optional quest-objective mark, then the name.
        UHorizontalBox* NameRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("NameRow"));
        ObjectiveIcon=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ObjectiveIcon"));
        FSlateFontInfo IconFont=FCoreStyle::GetDefaultFontStyle("Bold",14);
        IconFont.OutlineSettings.OutlineSize=1; IconFont.OutlineSettings.OutlineColor=FLinearColor::Black;
        ObjectiveIcon->SetFont(IconFont);
        ObjectiveIcon->SetText(FText::FromString(TEXT("?")));
        ObjectiveIcon->SetColorAndOpacity(FLinearColor(1.f,.82f,0.f));
        ObjectiveIcon->SetVisibility(ESlateVisibility::Collapsed);
        if(UHorizontalBoxSlot* S=NameRow->AddChildToHorizontalBox(ObjectiveIcon)){S->SetPadding(FMargin(0,0,4,0));S->SetVerticalAlignment(VAlign_Center);}
        if(UHorizontalBoxSlot* S=NameRow->AddChildToHorizontalBox(NameText)) S->SetVerticalAlignment(VAlign_Center);
        if(UVerticalBoxSlot* S=Root->AddChildToVerticalBox(NameRow)) S->SetHorizontalAlignment(HAlign_Center);
        SubtitleText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("SubtitleText"));
        SubtitleText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",11));
        SubtitleText->SetJustification(ETextJustify::Center);
        SubtitleText->SetShadowOffset(FVector2D(1,1)); SubtitleText->SetShadowColorAndOpacity(FLinearColor::Black);
        Root->AddChildToVerticalBox(SubtitleText); UpdateSubtitle(CachedSubtitle);

        // Health row (WoW: Forever style): dark bar with a thin gold frame, the current health
        // right-aligned in it, and the level in its own small gold-framed box beside it.
        const FLinearColor Gold(.80f,.64f,.18f,1.f);
        auto Frame=[this,&Gold](const TCHAR* Name)
        {
            UBorder* B=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),Name);
            B->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.f,0.f,0.f,.72f),2.f,Gold,1.f));
            B->SetPadding(FMargin(1.f));
            return B;
        };
        auto PlateFont=[](int32 Size)
        {
            FSlateFontInfo F=FCoreStyle::GetDefaultFontStyle("Bold",Size);
            F.OutlineSettings.OutlineSize=1; F.OutlineSettings.OutlineColor=FLinearColor::Black;
            return F;
        };
        auto FlatBar=[this](const TCHAR* Name)
        {
            UProgressBar* Bar=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),Name);
            FProgressBarStyle Style=Bar->GetWidgetStyle();
            Style.SetBackgroundImage(FSlateNoResource());
            Style.SetFillImage(FSlateColorBrush(FLinearColor::White));
            Style.SetMarqueeImage(FSlateNoResource());
            Bar->SetWidgetStyle(Style);
            return Bar;
        };

        HealthBox=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HealthRow"));
        HealthBox->SetHeightOverride(14.f);
        UHorizontalBox* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("HealthRowBox"));
        HealthBox->AddChild(Row);
        USizeBox* BarSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HealthBarSize"));
        BarSize->SetWidthOverride(112.f);
        UBorder* BarFrame=Frame(TEXT("HealthBarFrame"));
        BarSize->AddChild(BarFrame);
        UOverlay* BarOverlay=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("HealthBarOverlay"));
        BarFrame->SetContent(BarOverlay);
        HealthBar=FlatBar(TEXT("HealthBar"));
        if(UOverlaySlot* S=BarOverlay->AddChildToOverlay(HealthBar)){S->SetHorizontalAlignment(HAlign_Fill);S->SetVerticalAlignment(VAlign_Fill);}
        HealthValueText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HealthValueText"));
        HealthValueText->SetFont(PlateFont(9));
        HealthValueText->SetColorAndOpacity(FLinearColor::White);
        if(UOverlaySlot* S=BarOverlay->AddChildToOverlay(HealthValueText)){S->SetHorizontalAlignment(HAlign_Right);S->SetVerticalAlignment(VAlign_Center);S->SetPadding(FMargin(0,0,3,0));}
        if(UHorizontalBoxSlot* S=Row->AddChildToHorizontalBox(BarSize)) S->SetVerticalAlignment(VAlign_Fill);

        USizeBox* LevelSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("LevelSize"));
        LevelSize->SetMinDesiredWidth(18.f);
        UBorder* LevelFrame=Frame(TEXT("LevelFrame"));
        LevelFrame->SetPadding(FMargin(3.f,0.f));
        LevelFrame->SetHorizontalAlignment(HAlign_Center); LevelFrame->SetVerticalAlignment(VAlign_Center);
        LevelSize->AddChild(LevelFrame);
        LevelText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("LevelText"));
        LevelText->SetFont(PlateFont(9));
        LevelText->SetJustification(ETextJustify::Center);
        LevelFrame->SetContent(LevelText);
        if(UHorizontalBoxSlot* S=Row->AddChildToHorizontalBox(LevelSize)){S->SetPadding(FMargin(4,0,0,0));S->SetVerticalAlignment(VAlign_Fill);}
        if (UVerticalBoxSlot* HealthSlot = Root->AddChildToVerticalBox(HealthBox))
        {
            HealthSlot->SetHorizontalAlignment(HAlign_Center);
            HealthSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
        }

        // Cast bar under it: spell icon, then a gold-framed bar with the spell name.
        CastBox=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("CastRow"));
        CastBox->SetWidthOverride(134.f); CastBox->SetHeightOverride(13.f);
        UBorder* CastFrame=Frame(TEXT("CastFrame"));
        CastBox->AddChild(CastFrame);
        UHorizontalBox* CastRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("CastRowBox"));
        CastFrame->SetContent(CastRow);
        CastIcon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("CastIcon"));
        USizeBox* IconSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("CastIconSize"));
        IconSize->SetWidthOverride(11.f); IconSize->AddChild(CastIcon);
        if(UHorizontalBoxSlot* S=CastRow->AddChildToHorizontalBox(IconSize)){S->SetPadding(FMargin(0,0,1,0));}
        UOverlay* CastOverlay=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("CastOverlay"));
        if(UHorizontalBoxSlot* S=CastRow->AddChildToHorizontalBox(CastOverlay)) S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        CastBar=FlatBar(TEXT("CastBar"));
        if(UOverlaySlot* S=CastOverlay->AddChildToOverlay(CastBar)){S->SetHorizontalAlignment(HAlign_Fill);S->SetVerticalAlignment(VAlign_Fill);}
        CastText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CastText"));
        CastText->SetFont(PlateFont(8));
        CastText->SetColorAndOpacity(FLinearColor::White);
        if(UOverlaySlot* S=CastOverlay->AddChildToOverlay(CastText)){S->SetHorizontalAlignment(HAlign_Left);S->SetVerticalAlignment(VAlign_Center);S->SetPadding(FMargin(3,0,0,0));}
        CastBox->SetVisibility(ESlateVisibility::Collapsed);
        if(UVerticalBoxSlot* S=Root->AddChildToVerticalBox(CastBox)){S->SetHorizontalAlignment(HAlign_Center);S->SetPadding(FMargin(0,2,0,0));}
    }
    UpdateName(CachedName); UpdateHealth(CachedHealth,CachedMaxHealth);
    UpdateQuestStatus(CachedQuestStatus); SetDisplayOptions(bShowName,bShowBar);
    if(CachedLevel>=0) UpdateLevel(CachedLevel,CachedPlayerLevel);
    return Super::RebuildWidget();
}

void UWowNameplateWidget::SetDisplayOptions(bool ShowName,bool ShowBar)
{
    bShowName=ShowName; bShowBar=ShowBar;
    if(SubtitleText) SubtitleText->SetVisibility(ShowName && !CachedSubtitle.IsEmpty()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(NameText) NameText->SetVisibility(ShowName ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if(HealthBox) HealthBox->SetVisibility(ShowBar && CachedMaxHealth>0 && CachedHealth>0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if(CastBox && !(ShowBar && bCasting)) CastBox->SetVisibility(ESlateVisibility::Collapsed);
    UpdateName(CachedName);
}

void UWowNameplateWidget::UpdateQuestStatus(uint8 QuestStatus)
{
    CachedQuestStatus=QuestStatus;
    if (!QuestMarkerText) return;
    // r256 (Stefan 12.10: too small in the headset): larger in VR - sizes in WowQuestMarkerLook.
    const FLinearColor Gold(1.0f, 0.82f, 0.0f, 1.0f), Grey(.55f, .55f, .55f, 1.0f);
    if(QuestStatus==WowQuestGiverStatus::UNAVAILABLE || QuestStatus==WowQuestGiverStatus::LOW_LEVEL_AVAILABLE || QuestStatus==WowQuestGiverStatus::LOW_LEVEL_AVAILABLE_REP)
        WowQuestMarkerLook::Apply(QuestMarkerText, TEXT("!"), QuestStatus==WowQuestGiverStatus::UNAVAILABLE ? Grey : FLinearColor(.65f,.48f,.08f), true);
    else if (WowQuestGiverStatus::IsAvailable(QuestStatus))
        WowQuestMarkerLook::Apply(QuestMarkerText, TEXT("!"), Gold, true);
    else if (WowQuestGiverStatus::IsReadyForTurnIn(QuestStatus))
        WowQuestMarkerLook::Apply(QuestMarkerText, TEXT("?"), Gold, true);
    else if (QuestStatus == WowQuestGiverStatus::INCOMPLETE)
        WowQuestMarkerLook::Apply(QuestMarkerText, TEXT("?"), Grey, true);
    else
        WowQuestMarkerLook::Apply(QuestMarkerText, TEXT(""), Gold, false);
}

void UWowNameplateWidget::SetupNameplate(const FString& EntityName, int32 Health, int32 MaxHealth,
    bool bIsHostile, bool bIsPlayer)
{
    bIsHostileEntity = bIsHostile;
    bIsPlayerEntity = bIsPlayer;

    UpdateName(EntityName);
    UpdateHealth(Health, MaxHealth);
}

void UWowNameplateWidget::UpdateName(const FString& EntityName)
{
    CachedName=EntityName;
    if (!NameText) return;

    NameText->SetText(FText::FromString(EntityName));
    // With the health bar shown the bar carries the reaction colour and the name is white
    // (3.3.5 / WoW: Forever nameplates); a name alone keeps the reaction colour.
    const bool bBarShown = bShowBar && CachedMaxHealth>0 && CachedHealth>0;
    const FLinearColor NameColor = CachedMaxHealth>0 && CachedHealth<=0
        ? FLinearColor(0.55f,0.55f,0.55f) : bBarShown ? FLinearColor(1.f,1.f,1.f) : ReactionColor;
    NameText->SetColorAndOpacity(NameColor);
}

void UWowNameplateWidget::UpdateHealth(int32 Health, int32 MaxHealth)
{
    CachedHealth=Health; CachedMaxHealth=MaxHealth;
    SetDisplayOptions(bShowName,bShowBar);
    UpdateName(CachedName);
    if (HealthBar && MaxHealth<=0) HealthBar->SetPercent(0.0f);
    if (HealthBar && MaxHealth > 0)
    {
        float HealthPercent = FMath::Clamp(static_cast<float>(Health) / static_cast<float>(MaxHealth), 0.0f, 1.0f);
        HealthBar->SetPercent(HealthPercent);

        // Bar in the unit's reaction colour (hostile red, neutral yellow, friendly green).
        const FLinearColor HealthColor = ReactionColor;
        HealthBar->SetFillColorAndOpacity(HealthColor);
    }
    if (HealthValueText) HealthValueText->SetText(FText::AsNumber(FMath::Max(0,Health)));
}

void UWowNameplateWidget::SetQuestObjective(bool bObjective)
{
    bQuestObjective=bObjective;
    if(ObjectiveIcon) ObjectiveIcon->SetVisibility(bObjective && bShowName ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UWowNameplateWidget::UpdateLevel(int32 Level, int32 PlayerLevel)
{
    CachedLevel=Level; CachedPlayerLevel=PlayerLevel;
    if(!LevelText) return;
    if(Level<=0){ LevelText->SetText(FText::FromString(TEXT("??"))); LevelText->SetColorAndOpacity(FLinearColor(1.f,.1f,.1f)); return; }
    LevelText->SetText(FText::AsNumber(Level));
    // GetQuestDifficultyColor / GetQuestGreenRange (3.3.5).
    const int32 Diff=Level-PlayerLevel;
    const int32 GrayLevel=PlayerLevel<=5?0:PlayerLevel<=39?PlayerLevel-5-PlayerLevel/10:PlayerLevel<=59?PlayerLevel-1-PlayerLevel/5:PlayerLevel-9;
    FLinearColor Color(1.f,.82f,0.f);
    if(Diff>=5) Color=FLinearColor(1.f,.1f,.1f);
    else if(Diff>=3) Color=FLinearColor(1.f,.5f,.25f);
    else if(Diff>=-2) Color=FLinearColor(1.f,.82f,0.f);
    else if(Level>GrayLevel) Color=FLinearColor(.25f,.75f,.25f);
    else Color=FLinearColor(.5f,.5f,.5f);
    LevelText->SetColorAndOpacity(Color);
}

void UWowNameplateWidget::UpdateCast(bool bInCasting, const FString& SpellName, UTexture2D* Icon, double StartMs, double EndMs, bool bChanneling)
{
    bCasting=bInCasting && EndMs>StartMs;
    if(!bCasting){ if(CastBox) CastBox->SetVisibility(ESlateVisibility::Collapsed); return; }
    if(CastSpellName!=SpellName && CastText) CastText->SetText(FText::FromString(SpellName));
    CastSpellName=SpellName; CastStartMs=StartMs; CastEndMs=EndMs; bCastChanneling=bChanneling;
    if(CastIcon && Icon!=CastIconTexture){ CastIconTexture=Icon; if(Icon) CastIcon->SetBrushFromTexture(Icon,false); CastIcon->SetVisibility(Icon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden); }
    if(CastBar) CastBar->SetFillColorAndOpacity(bChanneling?FLinearColor(0.f,1.f,0.f):FLinearColor(1.f,.7f,0.f));
    if(CastBox) CastBox->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UWowNameplateWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if(!bCasting || !CastBar) return;
    const double Now=FPlatformTime::Seconds()*1000.0;
    if(Now>=CastEndMs){ bCasting=false; if(CastBox) CastBox->SetVisibility(ESlateVisibility::Collapsed); return; }
    const float Done=float((Now-CastStartMs)/(CastEndMs-CastStartMs));
    CastBar->SetPercent(FMath::Clamp(bCastChanneling?1.f-Done:Done,0.f,1.f));
}

void UWowNameplateWidget::UpdateSubtitle(const FString& Text)
{
    CachedSubtitle=Text;
    if(SubtitleText) {
        SubtitleText->SetText(FText::FromString(Text.IsEmpty()?Text:TEXT("<")+Text+TEXT(">")));
        SubtitleText->SetColorAndOpacity(bIsPlayerEntity?FLinearColor(.55f,.85f,1.f):FLinearColor(.3f,1.f,.3f));
    }
    SetDisplayOptions(bShowName,bShowBar);
}

void UWowNameplateWidget::SetReactionColor(const FLinearColor& Color)
{
    ReactionColor=Color;
    UpdateName(CachedName);
    if(HealthBar) HealthBar->SetFillColorAndOpacity(Color);
}
