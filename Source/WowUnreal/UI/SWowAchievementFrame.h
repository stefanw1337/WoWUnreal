#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UWowConnectionManager;
class UWowUIManager;
class SBox;
class FWowRetainedSlateBrush;
struct FSlateBrush;

/**
 * Native Achievement window (3.3.5 AchievementFrame, Y key / micro button): achievement points,
 * the category list on the left (Summary, General, Quests, Exploration ... with sub-categories),
 * the achievements of the selected category on the right with icon, name, description, points
 * and the date earned. Data: Achievement.dbc / Achievement_Category.dbc and SMSG_ALL_ACHIEVEMENT_DATA.
 */
class WOWUNREAL_API SWowAchievementFrame : public SCompoundWidget
{
public:
    static constexpr float FrameW = 768.f;
    static constexpr float FrameH = 500.f;

    SLATE_BEGIN_ARGS(SWowAchievementFrame) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UWowUIManager>, UIManager)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UWowConnectionManager* InConnectionManager);
    void Toggle();
    void Close();
    bool IsOpen() const { return bVisible; }
    FVector2D GetPanelSize() const { return FVector2D(FrameW, FrameH) * Scale; }
    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

private:
    void Rebuild();
    bool IsShown(uint32 AchievementId) const;
    bool IsStatisticsCategory(uint32 CategoryId) const;
    uint32 TotalPoints() const;
    FString DateText(uint32 PackedDate) const;
    const FSlateBrush* IconBrush(uint32 SpellIconId) const;
    const FSlateBrush* TextureBrush(const FString& Path) const;
    FSlateFontInfo Fnt(float WowPixels) const;
    TSharedRef<SWidget> AchievementRow(uint32 AchievementId) const;

    TWeakObjectPtr<UWowConnectionManager> ConnectionManager;
    TWeakObjectPtr<UWowUIManager> UIManager;
    bool bVisible = false;
    float Scale = 1.f;
    uint32 SelectedCategory = 0; // 0 = Summary
    uint32 SeenRevision = 0xFFFFFFFFu;
    TSharedPtr<SBox> Root;
    mutable TMap<FString, TSharedPtr<FWowRetainedSlateBrush>> Brushes;
};
