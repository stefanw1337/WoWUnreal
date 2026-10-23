#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"

class SScrollBox;
class UWowUIManager;
class UTexture2D;
struct FSlateBrush;

/**
 * The 3.3.5 scroll bar (UIPanelScrollBarTemplate / FauxScrollFrameTemplate): a 16-unit column with
 * the up arrow on top, the down arrow at the bottom and the 18 x 24 knob between them. The arrows
 * grey out at the ends and the knob hides when there is nothing to scroll.
 *
 * Usage: create it, give the scroll box ScrollBarVisibility(Collapsed) (its own Slate bar off) and
 * call SetScrollBox; place this widget in the window's scroll column (the frame art's track).
 */
class WOWUI_API SWowPanelScrollBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWowPanelScrollBar) : _Scale(1.f), _UIManager(nullptr) {}
		/** Window scale (design units -> Slate units). */
		SLATE_ATTRIBUTE(float, Scale)
		SLATE_ARGUMENT(UWowUIManager*, UIManager)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SWowPanelScrollBar() override;
	void SetScrollBox(const TSharedPtr<SScrollBox>& InBox) { Box = InBox; }

	/** A scroll box with this bar in a column at its right (its own Slate bar turned off). */
	static TSharedRef<SWidget> Wrap(const TSharedRef<SScrollBox>& InBox, TAttribute<float> InScale = 1.f, UWowUIManager* InUIManager = nullptr);

	/** Width of the column in design units. */
	static constexpr float ColumnWidth = 16.f;

	// Used by the track.
	float GetFraction() const;
	bool HasRange() const;
	void SetFraction(float Fraction);
	void Step(float Direction);
	const FSlateBrush* Brush(const FString& Path, float U0, float V0, float U1, float V1) const;
	float GetScale() const { return FMath::Max(0.01f, Scale.Get(1.f)); }

private:
	bool CanScroll(float Direction) const;
	TSharedRef<SWidget> ArrowButton(bool bUp);

	TAttribute<float> Scale;
	TWeakObjectPtr<UWowUIManager> UI;
	TWeakPtr<SScrollBox> Box;
	mutable TMap<FString, TSharedPtr<FSlateBrush>> Brushes;
	mutable TArray<TStrongObjectPtr<UTexture2D>> Textures;
};
