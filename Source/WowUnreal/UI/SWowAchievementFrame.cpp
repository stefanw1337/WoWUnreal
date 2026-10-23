#include "SWowAchievementFrame.h"
#include "WowColor.h"
#include "WowWindowStyle.h"
#include "WowRetainedSlateBrush.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowNetwork/Public/WowConnectionManager.h"
#include "WowNetwork/Public/WowPacketHandler.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/SpellIconDbc.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWowPanelScrollBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace
{
constexpr uint32 StatisticsCategory = 1;   // Achievement_Category "Statistics" (counters, not achievements)
constexpr uint32 FeatsOfStrength = 81;     // shown only once earned
constexpr uint32 FlagCounter = 0x1;        // ACHIEVEMENT_FLAG_COUNTER (statistic)
constexpr uint32 FlagHidden = 0x2;         // ACHIEVEMENT_FLAG_HIDDEN

FLinearColor Gold() { return WowColor::FromWow(1.f, .82f, 0.f); }
FLinearColor Grey() { return WowColor::FromWow(.55f, .55f, .55f); }
}

void SWowAchievementFrame::Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager)
{
    ConnectionManager = InConnectionManager;
    UIManager = InArgs._UIManager;
    if (UIManager.IsValid() && UIManager->GetFrameManager() && UIManager->GetFrameManager()->GetUIScale() > 0.f)
        Scale = UIManager->GetFrameManager()->GetUIScale();
    ChildSlot
    [
        SAssignNew(Root, SBox)
        .WidthOverride_Lambda([this]() { return FrameW * Scale; })
        .HeightOverride_Lambda([this]() { return FrameH * Scale; })
    ];
    SetVisibility(EVisibility::Collapsed);
}

void SWowAchievementFrame::Toggle()
{
    if (bVisible) { Close(); return; }
    bVisible = true;
    SeenRevision = 0xFFFFFFFFu;
    SetVisibility(EVisibility::SelfHitTestInvisible);
    Rebuild();
}

void SWowAchievementFrame::Close()
{
    if (!bVisible) return;
    bVisible = false;
    SetVisibility(EVisibility::Collapsed);
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().CloseToolTip();
}

void SWowAchievementFrame::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    if (!bVisible) return;
    bool bDirty = false;
    if (UIManager.IsValid() && UIManager->GetFrameManager())
    {
        const float NewScale = UIManager->GetFrameManager()->GetUIScale();
        if (NewScale > 0.f && !FMath::IsNearlyEqual(NewScale, Scale)) { Scale = NewScale; bDirty = true; }
    }
    if (ConnectionManager.IsValid() && ConnectionManager->PacketHandler.AchievementRevision != SeenRevision) bDirty = true;
    if (bDirty) Rebuild();
}

FSlateFontInfo SWowAchievementFrame::Fnt(float WowPixels) const
{
    return FWowWindowStyle::TitleFont(FMath::Max(6, FMath::RoundToInt(WowPixels * Scale * .75f)));
}

const FSlateBrush* SWowAchievementFrame::TextureBrush(const FString& Path) const
{
    if (Path.IsEmpty()) return nullptr;
    if (const TSharedPtr<FWowRetainedSlateBrush>* Found = Brushes.Find(Path)) return Found->Get();
    UTexture2D* Texture = UIManager.IsValid() && UIManager->GetFrameManager() ? UIManager->GetFrameManager()->LoadUITexture(Path) : nullptr;
    if (!Texture) return nullptr;
    TSharedPtr<FWowRetainedSlateBrush> Brush = MakeShared<FWowRetainedSlateBrush>();
    Brush->SetResourceObject(Texture);
    Brush->DrawAs = ESlateBrushDrawType::Image;
    Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
    Brushes.Add(Path, Brush);
    return Brush.Get();
}

const FSlateBrush* SWowAchievementFrame::IconBrush(uint32 SpellIconId) const
{
    const FSpellIconDbcEntry* Icon = FDbcStore::Get().SpellIcons().GetById(SpellIconId);
    return TextureBrush(Icon && !Icon->TexturePath.IsEmpty() ? Icon->TexturePath : FString(TEXT("Interface\\Icons\\INV_Misc_QuestionMark")));
}

bool SWowAchievementFrame::IsStatisticsCategory(uint32 CategoryId) const
{
    const auto& Categories = FDbcStore::Get().AchievementCategories();
    for (int32 Depth = 0; Depth < 8 && CategoryId; ++Depth)
    {
        if (CategoryId == StatisticsCategory) return true;
        const FDbcStore::FAchievementCategory* Category = Categories.Find(CategoryId);
        if (!Category || Category->Parent < 0) return false;
        CategoryId = uint32(Category->Parent);
    }
    return false;
}

bool SWowAchievementFrame::IsShown(uint32 AchievementId) const
{
    const FDbcStore::FAchievement* Achievement = FDbcStore::Get().Achievements().Find(AchievementId);
    if (!Achievement || (Achievement->Flags & FlagCounter) || IsStatisticsCategory(Achievement->Category)) return false;
    const bool bDone = ConnectionManager.IsValid() && ConnectionManager->PacketHandler.CompletedAchievements.Contains(AchievementId);
    if (!bDone && ((Achievement->Flags & FlagHidden) || Achievement->Category == FeatsOfStrength)) return false;
    if (Achievement->Faction >= 0 && ConnectionManager.IsValid())
        if (const FWowPlayerEntity* Me = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer())
        {
            const uint8 Race = Me->GetRaceId();
            const bool bAlliance = Race == 1 || Race == 3 || Race == 4 || Race == 7 || Race == 11;
            if ((Achievement->Faction == 1) != bAlliance) return false;
        }
    return true;
}

uint32 SWowAchievementFrame::TotalPoints() const
{
    uint32 Points = 0;
    if (ConnectionManager.IsValid())
        for (const TPair<uint32, uint32>& Done : ConnectionManager->PacketHandler.CompletedAchievements)
            if (const FDbcStore::FAchievement* Achievement = FDbcStore::Get().Achievements().Find(Done.Key))
                if (!(Achievement->Flags & FlagCounter)) Points += Achievement->Points;
    return Points;
}

FString SWowAchievementFrame::DateText(uint32 Packed) const
{
    // Packed time: minute 0-5, hour 6-10, weekday 11-13, day-1 14-19, month-1 20-23, year-2000 24-28.
    const uint32 Day = ((Packed >> 14) & 0x3F) + 1;
    const uint32 Month = ((Packed >> 20) & 0xF) + 1;
    const uint32 Year = (Packed >> 24) & 0x1F;
    return FString::Printf(TEXT("%u/%u/%02u"), Month, Day, Year);
}

TSharedRef<SWidget> SWowAchievementFrame::AchievementRow(uint32 AchievementId) const
{
    const FDbcStore::FAchievement* Achievement = FDbcStore::Get().Achievements().Find(AchievementId);
    const uint32* Date = ConnectionManager.IsValid() ? ConnectionManager->PacketHandler.CompletedAchievements.Find(AchievementId) : nullptr;
    const bool bDone = Date != nullptr;
    static const FSlateRoundedBoxBrush DoneBack(WowColor::FromWow(.30f, .22f, .05f, .85f), 4.f, WowColor::FromWow(.75f, .6f, .2f, 1.f), 1.f);
    static const FSlateRoundedBoxBrush OpenBack(WowColor::FromWow(.08f, .08f, .08f, .85f), 4.f, WowColor::FromWow(.3f, .3f, .3f, 1.f), 1.f);
    static const FSlateRoundedBoxBrush Shield(WowColor::FromWow(.45f, .33f, .08f, 1.f), 6.f, WowColor::FromWow(.9f, .75f, .3f, 1.f), 1.f);
    const uint32 Icon = Achievement ? Achievement->Icon : 0;
    return SNew(SBorder).BorderImage(bDone ? &DoneBack : &OpenBack).Padding(FMargin(6 * Scale, 4 * Scale))
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(40 * Scale).HeightOverride(40 * Scale)
                [ SNew(SImage).Image(IconBrush(Icon)).ColorAndOpacity(bDone ? FLinearColor::White : WowColor::FromWow(.45f, .45f, .45f)) ]
            ]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(FMargin(8 * Scale, 0))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(STextBlock).Text(FText::FromString(Achievement ? Achievement->Name : FString())).Font(Fnt(14))
                  .ColorAndOpacity(bDone ? FLinearColor::White : Grey()).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2 * Scale, 0, 0)
                [ SNew(STextBlock).Text(FText::FromString(Achievement ? Achievement->Description : FString())).Font(Fnt(11)).AutoWrapText(true)
                  .ColorAndOpacity(bDone ? WowColor::FromWow(.95f, .92f, .8f) : Grey()) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2 * Scale, 0, 0)
                [ SNew(STextBlock).Text(FText::FromString(Achievement && !Achievement->Reward.IsEmpty() ? Achievement->Reward : FString())).Font(Fnt(10))
                  .Visibility(Achievement && !Achievement->Reward.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
                  .ColorAndOpacity(WowColor::FromWow(.1f, 1.f, .1f)) ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [
                    SNew(SBox).WidthOverride(34 * Scale).HeightOverride(30 * Scale).Visibility(Achievement && Achievement->Points ? EVisibility::Visible : EVisibility::Hidden)
                    [
                        SNew(SBorder).BorderImage(&Shield).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0)
                        [ SNew(STextBlock).Text(FText::AsNumber(Achievement ? Achievement->Points : 0)).Font(Fnt(13)).ColorAndOpacity(FLinearColor::White)
                          .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2 * Scale, 0, 0)
                [ SNew(STextBlock).Text(FText::FromString(bDone ? DateText(*Date) : FString())).Font(Fnt(10)).ColorAndOpacity(Gold()) ]
            ]
        ];
}

void SWowAchievementFrame::Rebuild()
{
    if (!Root.IsValid()) return;
    SeenRevision = ConnectionManager.IsValid() ? ConnectionManager->PacketHandler.AchievementRevision : 0;
    const auto& Categories = FDbcStore::Get().AchievementCategories();
    const auto& Achievements = FDbcStore::Get().Achievements();

    // Shown achievements per category, and totals per top-level category (Summary progress).
    TMap<uint32, TArray<uint32>> ByCategory;
    for (const TPair<uint32, FDbcStore::FAchievement>& Pair : Achievements)
        if (IsShown(Pair.Key)) ByCategory.FindOrAdd(Pair.Value.Category).Add(Pair.Key);
    auto TopOf = [&Categories](uint32 CategoryId)
    {
        for (int32 Depth = 0; Depth < 8; ++Depth)
        {
            const FDbcStore::FAchievementCategory* Category = Categories.Find(CategoryId);
            if (!Category || Category->Parent < 0) return CategoryId;
            CategoryId = uint32(Category->Parent);
        }
        return CategoryId;
    };

    // Left: category tree (top level sorted by order; the selected branch unfolds).
    TArray<const FDbcStore::FAchievementCategory*> Top;
    for (const TPair<uint32, FDbcStore::FAchievementCategory>& Pair : Categories)
        if (Pair.Value.Parent < 0 && !IsStatisticsCategory(Pair.Key)) Top.Add(&Pair.Value);
    Top.Sort([](const FDbcStore::FAchievementCategory& A, const FDbcStore::FAchievementCategory& B) { return A.Order < B.Order; });
    const uint32 SelectedTop = SelectedCategory ? TopOf(SelectedCategory) : 0;

    static const FSlateRoundedBoxBrush Selected(WowColor::FromWow(.45f, .33f, .08f, .9f), 3.f);
    TSharedRef<SScrollBox> CategoryList = SNew(SScrollBox);
    auto AddCategory = [this, &CategoryList](uint32 Id, const FString& Name, bool bChild)
    {
        const bool bSelected = SelectedCategory == Id;
        CategoryList->AddSlot()
        [
            SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(0)
            .OnClicked_Lambda([this, Id]() { SelectedCategory = Id; Rebuild(); return FReply::Handled(); })
            [
                SNew(SBorder).BorderImage(bSelected ? static_cast<const FSlateBrush*>(&Selected) : FCoreStyle::Get().GetBrush("NoBorder"))
                .Padding(FMargin((bChild ? 18 : 6) * Scale, 3 * Scale))
                [ SNew(STextBlock).Text(FText::FromString(Name)).Font(Fnt(bChild ? 11 : 13))
                  .ColorAndOpacity(bChild ? FLinearColor::White : Gold()).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
            ]
        ];
    };
    AddCategory(0, TEXT("Summary"), false);
    for (const FDbcStore::FAchievementCategory* Category : Top)
    {
        AddCategory(Category->Id, Category->Name, false);
        if (Category->Id != SelectedTop) continue;
        TArray<const FDbcStore::FAchievementCategory*> Children;
        for (const TPair<uint32, FDbcStore::FAchievementCategory>& Pair : Categories)
            if (Pair.Value.Parent == int32(Category->Id)) Children.Add(&Pair.Value);
        Children.Sort([](const FDbcStore::FAchievementCategory& A, const FDbcStore::FAchievementCategory& B) { return A.Order < B.Order; });
        for (const FDbcStore::FAchievementCategory* Child : Children) AddCategory(Child->Id, Child->Name, true);
    }

    // Right: the selected category's achievements, or the summary.
    TSharedRef<SScrollBox> List = SNew(SScrollBox);
    const auto* Completed = ConnectionManager.IsValid() ? &ConnectionManager->PacketHandler.CompletedAchievements : nullptr;
    if (SelectedCategory == 0)
    {
        List->AddSlot().Padding(FMargin(4 * Scale, 2 * Scale))
        [ SNew(STextBlock).Text(FText::FromString(TEXT("Recent Achievements"))).Font(Fnt(14)).ColorAndOpacity(Gold()) ];
        TArray<TPair<uint32, uint32>> Recent;
        if (Completed)
            for (const TPair<uint32, uint32>& Done : *Completed)
                if (IsShown(Done.Key)) Recent.Add(TPair<uint32, uint32>(Done.Key, Done.Value));
        Recent.Sort([](const TPair<uint32, uint32>& A, const TPair<uint32, uint32>& B) { return A.Value > B.Value; });
        for (int32 Index = 0; Index < FMath::Min(5, Recent.Num()); ++Index)
            List->AddSlot().Padding(FMargin(2 * Scale))[ AchievementRow(Recent[Index].Key) ];
        if (Recent.Num() == 0)
            List->AddSlot().Padding(FMargin(8 * Scale))
            [ SNew(STextBlock).Text(FText::FromString(Completed && Completed->Num() ? TEXT("") : TEXT("No achievements earned yet."))).Font(Fnt(12)).ColorAndOpacity(Grey()) ];
        List->AddSlot().Padding(FMargin(4 * Scale, 10 * Scale, 4 * Scale, 2 * Scale))
        [ SNew(STextBlock).Text(FText::FromString(TEXT("Progress Overview"))).Font(Fnt(14)).ColorAndOpacity(Gold()) ];
        for (const FDbcStore::FAchievementCategory* Category : Top)
        {
            int32 Total = 0, Done = 0;
            for (const TPair<uint32, TArray<uint32>>& Pair : ByCategory)
                if (TopOf(Pair.Key) == Category->Id)
                    for (uint32 Id : Pair.Value) { ++Total; if (Completed && Completed->Contains(Id)) ++Done; }
            if (!Total) continue;
            List->AddSlot().Padding(FMargin(8 * Scale, 2 * Scale))
            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s:  %d / %d"), *Category->Name, Done, Total))).Font(Fnt(12)).ColorAndOpacity(FLinearColor::White) ];
        }
    }
    else
    {
        TArray<uint32> Ids = ByCategory.FindRef(SelectedCategory);
        Ids.Sort([&Achievements, Completed](uint32 A, uint32 B)
        {
            // Unearned first in their order, like the client lists the next goals on top.
            const bool bDoneA = Completed && Completed->Contains(A), bDoneB = Completed && Completed->Contains(B);
            if (bDoneA != bDoneB) return !bDoneA;
            const FDbcStore::FAchievement* EA = Achievements.Find(A);
            const FDbcStore::FAchievement* EB = Achievements.Find(B);
            return (EA ? EA->Order : 0) < (EB ? EB->Order : 0);
        });
        for (uint32 Id : Ids) List->AddSlot().Padding(FMargin(2 * Scale))[ AchievementRow(Id) ];
        if (Ids.Num() == 0)
            List->AddSlot().Padding(FMargin(8 * Scale))
            [ SNew(STextBlock).Text(FText::FromString(TEXT("Choose a sub-category."))).Font(Fnt(12)).ColorAndOpacity(Grey()) ];
    }

    Root->SetContent(
        SNew(SWowWindowFrame).Padding(FMargin(8 * Scale))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(4 * Scale, 0, 0, 6 * Scale))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("Achievements"))).Font(Fnt(18)).ColorAndOpacity(Gold())
                  .ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 12 * Scale, 0))
                [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Achievement Points: %u"), TotalPoints()))).Font(Fnt(14))
                  .ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor::Black) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SBox).WidthOverride(28 * Scale).HeightOverride(28 * Scale)
                    [
                        SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(0)
                        .OnClicked_Lambda([this]() { Close(); return FReply::Handled(); })
                        [ SNew(SImage).Image(TextureBrush(TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"))) ]
                    ]
                ]
            ]
            + SVerticalBox::Slot().FillHeight(1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SBox).WidthOverride(190 * Scale)[ SWowPanelScrollBar::Wrap(CategoryList, Scale) ] ]
                + SHorizontalBox::Slot().FillWidth(1).Padding(FMargin(8 * Scale, 0, 0, 0))
                [ SWowPanelScrollBar::Wrap(List, Scale) ]
            ]
        ]);
}
