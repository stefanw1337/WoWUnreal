#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SVerticalBox;

/** One line of a unit menu: a gold title, a disabled grey line, a clickable command or a submenu. */
struct FWowUnitMenuEntry
{
    FText Label;
    TFunction<void()> Action;
    bool bTitle = false;
    bool bEnabled = true;
    /** Radio choice (difficulty, raid icon): a gold dot marks the current one. */
    bool bCheckable = false;
    bool bChecked = false;
    /** UnitPopup "nested" entries (Raid Target Icon, Dungeon Difficulty ...): an arrow, the
     *  choices open in a second box to the right when the line is hovered or clicked. */
    TSharedPtr<TArray<FWowUnitMenuEntry>> Children;
};

/**
 * Right-click menu for the unit frames (UnitPopup: Whisper, Inspect, Invite, Trade, Duel ...),
 * drawn like the DropDownList: dark box with a thin border, title line in gold, commands in
 * white. Opens at the mouse; a click anywhere else or Escape closes it.
 */
class WOWUNREAL_API SWowUnitMenu : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowUnitMenu) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    /** AbsolutePosition: Slate desktop coordinates of the menu's top-left corner. */
    void Open(const TArray<FWowUnitMenuEntry>& Entries, const FVector2D& AbsolutePosition, float InScale);
    void Close();
    bool IsOpen() const { return bOpen; }

private:
    void FillLines(SVerticalBox& Box, const TArray<FWowUnitMenuEntry>& Entries, bool bSubmenu);
    void OpenSubmenu(const TSharedPtr<TArray<FWowUnitMenuEntry>>& Children, const FVector2D& AbsoluteTopLeft);
    FVector2D MenuPosition() const;
    FVector2D SubmenuPosition() const;
    TSharedPtr<SVerticalBox> Lines;
    TSharedPtr<SVerticalBox> SubLines;
    TSharedPtr<SWidget> MenuBox;
    TSharedPtr<SWidget> SubBox;
    TSharedPtr<TArray<FWowUnitMenuEntry>> OpenChildren;
    FVector2D AbsoluteAnchor = FVector2D::ZeroVector;
    FVector2D SubAnchor = FVector2D::ZeroVector;
    float Scale = 1.f;
    bool bOpen = false;
    bool bSubOpen = false;
};
