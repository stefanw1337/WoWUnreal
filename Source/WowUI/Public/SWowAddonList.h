#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "WowAddonSettings.h"

class SVerticalBox;

/**
 * The AddOns list (character select "AddOns" button and in-game Game Menu):
 * check/uncheck addons, Enable All / Disable All, Okay / Cancel and, in game,
 * Reload UI to apply the change immediately. States are saved to Saved/WTF/AddOns.txt.
 */
class WOWUI_API SWowAddonList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWowAddonList) : _bInGame(false) {}
		SLATE_ARGUMENT(bool, bInGame)
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Open the list on top of everything in the game viewport (one instance at a time). */
	static void OpenInViewport(bool bInGame);
	static void CloseViewportInstance();
	static bool IsOpen();

private:
	void RebuildRows();
	void AddRow(const FWowAddonListEntry& Entry, int32 Depth);
	void SetAddonEnabled(const FString& Name, bool bEnabled);
	bool WillLoad(const FWowAddonListEntry& Entry) const;
	void SetAll(bool bEnabled);
	void Apply();
	void Close();
	FText GetStatusText(const FWowAddonListEntry& Entry) const;

	bool bInGame = false;
	FSimpleDelegate OnClosed;
	TArray<FWowAddonListEntry> Entries;
	TMap<FString, bool> Pending;
	bool bLoadOutOfDate = true;
	TMap<FString, TArray<int32>> Children;   // parent addon (lower case) -> child entry indices
	TSet<FString> Collapsed;                 // lower-case names of collapsed parents
	TSharedPtr<SVerticalBox> RowBox;
};
