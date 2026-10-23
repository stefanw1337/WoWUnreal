#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class UWowConnectionManager;
class UWowUIManager;
class FWowRetainedSlateBrush;

// Destroy confirmation for an item dropped from the bags into the world.
// bRequireTypedDelete: the Yes button stays disabled until "DELETE" (any case) is typed.
class WOWUI_API SWowDestroyItemDialog : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowDestroyItemDialog)
        : _ItemEntry(0), _Quality(0), _Count(1), _bRequireTypedDelete(false)
    {}
        SLATE_ARGUMENT(FString, ItemName)
        SLATE_ARGUMENT(uint32, ItemEntry)
        SLATE_ARGUMENT(uint32, Quality)
        SLATE_ARGUMENT(int32, Count)
        SLATE_ARGUMENT(bool, bRequireTypedDelete)
        SLATE_ARGUMENT(TWeakObjectPtr<UWowConnectionManager>, ConnectionManager)
        SLATE_ARGUMENT(TWeakObjectPtr<UWowUIManager>, UIManager)
        // Closes the dialog when it returns false (item left the cursor/slot).
        SLATE_EVENT(TDelegate<bool()>, IsStillValid)
        SLATE_EVENT(FSimpleDelegate, OnConfirmed)
        SLATE_EVENT(FSimpleDelegate, OnCancelled)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;
    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }

    // Gives the edit box (typed mode) or the dialog itself keyboard focus.
    void FocusInput();
    void Cancel();
    bool RequiresTypedDelete() const { return bRequireTypedDelete; }

private:
    FReply OnYes();
    FReply OnNo();
    void Confirm();
    void Finish(bool bConfirmed);
    bool CanConfirm() const;
    void OnTextCommitted(const FText& Text, ETextCommit::Type CommitType);
    void UpdateIcon();

    FString ItemName;
    uint32 ItemEntry = 0;
    uint32 Quality = 0;
    int32 Count = 1;
    bool bRequireTypedDelete = false;
    bool bFinished = false;
    FString TypedText;
    FString LoadedIconPath;
    TWeakObjectPtr<UWowConnectionManager> ConnectionManager;
    TWeakObjectPtr<UWowUIManager> UIManager;
    TDelegate<bool()> IsStillValid;
    FSimpleDelegate OnConfirmed;
    FSimpleDelegate OnCancelled;
    TSharedPtr<SEditableTextBox> DeleteTextBox;
    TSharedPtr<FWowRetainedSlateBrush> IconBrush;
};

// Item icon that follows the mouse while an item is held on the cursor.
// Hit-test invisible; positions itself from its own geometry every tick.
class WOWUI_API SWowCursorItemIcon : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowCursorItemIcon) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UWowConnectionManager>, ConnectionManager)
        SLATE_ARGUMENT(TWeakObjectPtr<UWowUIManager>, UIManager)
        // Item entry to show (0 hides the icon).
        SLATE_EVENT(TDelegate<uint32()>, GetItemEntry)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;
    /** True while the item icon is actually drawn at the pointer (the normal pointer may then hide). */
    bool IsShowingIcon() const;

private:
    TWeakObjectPtr<UWowConnectionManager> ConnectionManager;
    TWeakObjectPtr<UWowUIManager> UIManager;
    TDelegate<uint32()> GetItemEntry;
    TSharedPtr<class SImage> Icon;
    TSharedPtr<FWowRetainedSlateBrush> Brush;
    uint32 ShownEntry = 0;
    FString LoadedIconPath;
};
