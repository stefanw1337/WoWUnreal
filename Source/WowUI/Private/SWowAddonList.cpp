#include "SWowAddonList.h"
#include "WowUiSurfaces.h"
#include "WowUIManager.h"
#include "WowWindowStyle.h"
#include "WowAddonErrorLog.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

// AddOns list in the style of the 3.3.5 one: dialog-box frame, addons that need another
// addon listed under it (folder style, collapsible), a status column, "Load out of date
// AddOns", Enable All / Disable All, Okay / Cancel (and Reload UI / Copy Errors in game).

namespace
{
	TSharedPtr<SWidget> GViewportContainer;
	TSharedPtr<SWowAddonList> GViewportList;

	const FSlateBrush* DimBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor(0.f, 0.f, 0.f, 0.55f));
		return &Brush;
	}
	const FSlateBrush* ListBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor(0.f, 0.f, 0.f, 0.45f));
		return &Brush;
	}
	const FSlateBrush* RowBrush(bool bOdd)
	{
		static const FSlateColorBrush Even(FLinearColor(1.f, 1.f, 1.f, 0.025f));
		static const FSlateColorBrush Odd(FLinearColor(1.f, 1.f, 1.f, 0.055f));
		return bOdd ? &Odd : &Even;
	}
	const FSlateBrush* LineBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor(0.55f, 0.45f, 0.25f, 0.6f));
		return &Brush;
	}
	const FLinearColor Gold(1.f, 0.82f, 0.f);
	const FLinearColor White(0.95f, 0.95f, 0.95f);
	const FLinearColor Grey(0.5f, 0.5f, 0.5f);
	const FLinearColor Red(1.f, 0.25f, 0.2f);
	const FLinearColor Green(0.25f, 1.f, 0.25f);

	TSharedRef<SWidget> MakeButton(const FString& Label, FOnClicked OnClicked, float Width = 120.f)
	{
		TSharedRef<SButton> Button = SNew(SButton).ClickMethod(EButtonClickMethod::MouseDown)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			// No vertical content padding: the label's descenders were clipped in the 24-unit button.
			.ContentPadding(FMargin(6.f, 0.f))
			.OnClicked(OnClicked)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.Font(FWowWindowStyle::TitleFont(10))
				.ColorAndOpacity(Gold)
				.ShadowOffset(FVector2D(1.f, 1.f))
			];
		if (const FButtonStyle* Style = FWowWindowStyle::Button())
		{
			Button->SetButtonStyle(Style);
		}
		return SNew(SBox).WidthOverride(Width).HeightOverride(26.f)[Button];
	}

	/** In game: what happened to the addon in this UI session. */
	FString RuntimeLine(const FWowAddonListEntry& Entry)
	{
		FWowAddonRuntimeStatus S;
		if (!FWowAddonErrorLog::Get(Entry.Name, S))
			return Entry.bLoadOnDemand ? TEXT("Not loaded yet (load on demand)") : TEXT("Not loaded this session");
		if (S.ErrorCount > 0)
			return FString::Printf(TEXT("%s, %d error%s: %s"), S.bLoaded ? TEXT("Loaded") : TEXT("Failed"), S.ErrorCount,
				S.ErrorCount == 1 ? TEXT("") : TEXT("s"), S.Errors.Num() ? *S.Errors[0] : TEXT(""));
		if (S.bLoaded) return TEXT("Loaded, no errors");
		return S.NotLoadedReason.IsEmpty() ? TEXT("Not loaded") : TEXT("Not loaded: ") + S.NotLoadedReason;
	}
	FLinearColor RuntimeColor(const FWowAddonListEntry& Entry)
	{
		FWowAddonRuntimeStatus S;
		if (!FWowAddonErrorLog::Get(Entry.Name, S)) return Grey;
		if (S.ErrorCount > 0) return Red;
		return S.bLoaded ? Green : Grey;
	}
}

void SWowAddonList::Construct(const FArguments& InArgs)
{
	bInGame = InArgs._bInGame;
	OnClosed = InArgs._OnClosed;
	FWowAddonSettings::Reload();
	Entries = FWowAddonSettings::GetUserAddons(nullptr);
	for (const FWowAddonListEntry& Entry : Entries)
	{
		Pending.Add(Entry.Name, Entry.bEnabled);
	}
	bLoadOutOfDate = FWowAddonSettings::GetLoadOutOfDate();

	// Folder structure: an addon goes under the first addon of this list it requires.
	TMap<FString, int32> IndexByName;
	for (int32 I = 0; I < Entries.Num(); ++I) IndexByName.Add(Entries[I].Name.ToLower(), I);
	for (int32 I = 0; I < Entries.Num(); ++I)
	{
		for (const FString& Dep : Entries[I].RequiredDeps)
		{
			const int32* Parent = IndexByName.Find(Dep.ToLower());
			if (Parent && *Parent != I)
			{
				Children.FindOrAdd(Entries[*Parent].Name.ToLower()).Add(I);
				break;
			}
		}
	}

	const FString Hint = bInGame
		? TEXT("Changes take effect after Reload UI.")
		: TEXT("Changes take effect when you enter the world.");

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(DimBrush())
		.Padding(0.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		// The dimmed backdrop is modal: clicks and the wheel must not reach the world
		// (targeting NPCs, camera) or frames behind the list.
		.OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
		.OnMouseButtonUp_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
		.OnMouseDoubleClick_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
		[
			SNew(SWowWindowFrame)
			.Padding(FMargin(20.f, 14.f))
			[
				SNew(SBox)
				.WidthOverride(620.f)
				.HeightOverride(620.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("AddOns")))
						.Font(FWowWindowStyle::TitleFont(20))
						.ColorAndOpacity(Gold)
						.ShadowOffset(FVector2D(1.f, 1.f))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 8.f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Hint))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						.ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 4.f, 2.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).Padding(30.f, 0.f, 0.f, 0.f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("AddOn"))).Font(FWowWindowStyle::TitleFont(10)).ColorAndOpacity(Gold)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox).WidthOverride(190.f)
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("Status"))).Font(FWowWindowStyle::TitleFont(10)).ColorAndOpacity(Gold)
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(1.f)[SNew(SBorder).BorderImage(LineBrush())]]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(SBorder)
						.BorderImage(ListBrush())
						.Padding(2.f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SAssignNew(RowBox, SVerticalBox)
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(1.f)[SNew(SBorder).BorderImage(LineBrush())]]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
				// Toggle on press: the gameplay controller's mouse handling can drop the
				// pointer capture before the release, so the default press+release never fired.
				.ClickMethod(EButtonClickMethod::MouseDown)
							.IsChecked_Lambda([this]() { return bLoadOutOfDate ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bLoadOutOfDate = State == ECheckBoxState::Checked; })
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Load out of date AddOns")))
							.Font(FWowWindowStyle::TitleFont(11))
							.ColorAndOpacity(Gold)
							.ToolTipText(FText::FromString(FString::Printf(
								TEXT("Also load addons whose .toc says an older game version (## Interface below %d)."),
								FWowAddonSettings::ClientInterface)))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)[SNullWidget::NullWidget]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							bInGame
								? MakeButton(TEXT("Copy Errors"), FOnClicked::CreateLambda([this]()
									{
										// Every addon's load result and errors, ready to paste into a bug report.
										FString Text;
										for (const FWowAddonListEntry& Entry : Entries)
										{
											const FString Line = FWowAddonErrorLog::Describe(Entry.Name);
											Text += (Line.IsEmpty() ? Entry.Name + TEXT(": not loaded") : Line) + TEXT("\n");
										}
										FPlatformApplicationMisc::ClipboardCopy(*Text);
										UE_LOG(LogTemp, Log, TEXT("[AddOns status]\n%s"), *Text);
										return FReply::Handled();
									}), 110.f)
								: SNullWidget::NullWidget
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
						[
							MakeButton(TEXT("Enable All"), FOnClicked::CreateLambda([this]() { SetAll(true); return FReply::Handled(); }), 110.f)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							MakeButton(TEXT("Disable All"), FOnClicked::CreateLambda([this]() { SetAll(false); return FReply::Handled(); }), 110.f)
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)[SNullWidget::NullWidget]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
						[
							bInGame
								? MakeButton(TEXT("Reload UI"), FOnClicked::CreateLambda([this]()
									{
										Apply();
										UWowUIManager::RequestReloadUI();
										Close();
										return FReply::Handled();
									}), 100.f)
								: SNullWidget::NullWidget
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
						[
							MakeButton(TEXT("Okay"), FOnClicked::CreateLambda([this]() { Apply(); Close(); return FReply::Handled(); }), 90.f)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							MakeButton(TEXT("Cancel"), FOnClicked::CreateLambda([this]() { Close(); return FReply::Handled(); }), 90.f)
						]
					]
				]
			]
		]
	];
	RebuildRows();
}

bool SWowAddonList::WillLoad(const FWowAddonListEntry& Entry) const
{
	const bool* State = Pending.Find(Entry.Name);
	if (!State || !*State) return false;
	if (FWowAddonSettings::IsOutOfDate(Entry.Interface) && !bLoadOutOfDate) return false;
	return GetStatusText(Entry).ToString().Find(TEXT("ependency")) == INDEX_NONE;
}

FText SWowAddonList::GetStatusText(const FWowAddonListEntry& Entry) const
{
	TArray<FString> Parts;
	for (const FString& Dep : Entry.RequiredDeps)
	{
		if (Dep.StartsWith(TEXT("Blizzard_"), ESearchCase::IgnoreCase)) continue;
		const bool* DepState = nullptr;
		for (const auto& Pair : Pending)
		{
			if (Pair.Key.Equals(Dep, ESearchCase::IgnoreCase)) { DepState = &Pair.Value; break; }
		}
		if (!DepState) Parts.Add(FString::Printf(TEXT("Missing dependency: %s"), *Dep));
		else if (!*DepState) Parts.Add(FString::Printf(TEXT("Dependency disabled: %s"), *Dep));
	}
	if (FWowAddonSettings::IsOutOfDate(Entry.Interface)) Parts.Add(TEXT("Out of date"));
	if (Entry.bLoadOnDemand) Parts.Add(TEXT("Load on demand"));
	return FText::FromString(FString::Join(Parts, TEXT(", ")));
}

void SWowAddonList::SetAddonEnabled(const FString& Name, bool bEnabled)
{
	Pending.Add(Name, bEnabled);
	if (!bEnabled) return;
	// Enabling an addon also enables what it requires (DBM-GUI -> DBM-Core).
	for (const FWowAddonListEntry& Entry : Entries)
	{
		if (!Entry.Name.Equals(Name)) continue;
		for (const FString& Dep : Entry.RequiredDeps)
			for (const FWowAddonListEntry& Other : Entries)
				if (Other.Name.Equals(Dep, ESearchCase::IgnoreCase) && !Pending.FindRef(Other.Name))
					SetAddonEnabled(Other.Name, true);
	}
}

void SWowAddonList::AddRow(const FWowAddonListEntry& Entry, int32 Depth)
{
	const FString Name = Entry.Name;
	const FString Key = Name.ToLower();
	const TArray<int32>* Kids = Children.Find(Key);
	const bool bOdd = RowBox->NumSlots() % 2 == 1;
	TSharedRef<SWidget> Toggle = SNullWidget::NullWidget;
	if (Kids && Kids->Num())
	{
		Toggle = SNew(SButton).ClickMethod(EButtonClickMethod::MouseDown)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.ContentPadding(0.f)
			.OnClicked_Lambda([this, Key]()
			{
				if (Collapsed.Contains(Key)) Collapsed.Remove(Key); else Collapsed.Add(Key);
				RebuildRows();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(Collapsed.Contains(Key) ? TEXT("[+]") : TEXT("[-]")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(Gold)
			];
	}
	RowBox->AddSlot().AutoHeight()
	[
		SNew(SBorder)
		.BorderImage(RowBrush(bOdd))
		.Padding(FMargin(4.f + Depth * 22.f, 3.f, 6.f, 3.f))
		.ToolTipText(FText::FromString(Entry.Notes.IsEmpty() ? Entry.Name : Entry.Notes + TEXT("\n(") + Entry.Name + TEXT(")")))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(26.f).HAlign(HAlign_Center)[Toggle]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SCheckBox)
				// Toggle on press: the gameplay controller's mouse handling can drop the
				// pointer capture before the release, so the default press+release never fired.
				.ClickMethod(EButtonClickMethod::MouseDown)
				.IsChecked_Lambda([this, Name]()
				{
					return Pending.FindRef(Name) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, Name](ECheckBoxState NewState)
				{
					SetAddonEnabled(Name, NewState == ECheckBoxState::Checked);
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Entry.Title + (Entry.Version.IsEmpty() ? FString() : TEXT("  ") + Entry.Version)))
					.Font(FWowWindowStyle::TitleFont(11))
					.ColorAndOpacity_Lambda([this, Entry]()
					{
						if (!Pending.FindRef(Entry.Name)) return FSlateColor(Grey);
						return FSlateColor(WillLoad(Entry) ? Gold : Red);
					})
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Visibility(bInGame ? EVisibility::Visible : EVisibility::Collapsed)
					.Text_Lambda([Entry]() { return FText::FromString(RuntimeLine(Entry)); })
					.ToolTipText_Lambda([Name]() { return FText::FromString(FWowAddonErrorLog::Describe(Name)); })
					.AutoWrapText(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity_Lambda([Entry]() { return FSlateColor(RuntimeColor(Entry)); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(190.f)
				[
					SNew(STextBlock)
					.Text_Lambda([this, Entry]() { return GetStatusText(Entry); })
					.AutoWrapText(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity_Lambda([this, Entry]()
					{
						const FString Status = GetStatusText(Entry).ToString();
						const bool bBad = Status.Contains(TEXT("ependency")) || (Status.Contains(TEXT("Out of date")) && !bLoadOutOfDate);
						return FSlateColor(bBad ? Red : FLinearColor(0.75f, 0.75f, 0.75f));
					})
				]
			]
		]
	];
	if (Kids && !Collapsed.Contains(Key) && Depth < 6)
	{
		for (const int32 Child : *Kids)
		{
			AddRow(Entries[Child], Depth + 1);
		}
	}
}

void SWowAddonList::RebuildRows()
{
	if (!RowBox.IsValid()) return;
	RowBox->ClearChildren();
	if (Entries.Num() == 0)
	{
		RowBox->AddSlot().AutoHeight().Padding(8.f, 20.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No addons found in Interface\\AddOns.")))
			.ColorAndOpacity(Grey)
		];
		return;
	}
	TSet<int32> IsChild;
	for (const auto& Pair : Children) for (const int32 Child : Pair.Value) IsChild.Add(Child);
	for (int32 I = 0; I < Entries.Num(); ++I)
	{
		if (!IsChild.Contains(I)) AddRow(Entries[I], 0);
	}
}

void SWowAddonList::SetAll(bool bEnabled)
{
	for (auto& Pair : Pending)
	{
		Pair.Value = bEnabled;
	}
}

void SWowAddonList::Apply()
{
	bool bChanged = bLoadOutOfDate != FWowAddonSettings::GetLoadOutOfDate();
	for (const auto& Pair : Pending)
	{
		bChanged |= FWowAddonSettings::IsEnabled(Pair.Key) != Pair.Value;
		FWowAddonSettings::SetEnabled(Pair.Key, Pair.Value);
	}
	FWowAddonSettings::SetLoadOutOfDate(bLoadOutOfDate);
	FWowAddonSettings::Save();
	// At character select the loaded UI survives logout, so reload it when the player
	// enters the world again (a first login loads the new list anyway).
	// In game, Okay applies the change right away like Reload UI (Stefan's request);
	// at character select the new list loads when entering the world.
	if (bChanged)
	{
		UWowUIManager::RequestReloadUI();
	}
}

void SWowAddonList::Close()
{
	OnClosed.ExecuteIfBound();
	if (GViewportList.Get() == this)
	{
		CloseViewportInstance();
	}
}

FReply SWowAddonList::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Close();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SWowAddonList::OpenInViewport(bool bInGame)
{
	if (IsOpen() || !GEngine || !GEngine->GameViewport) return;
	GViewportList = SNew(SWowAddonList).bInGame(bInGame);
	GViewportContainer = GViewportList;
	FWowUiSurfaces::Add(GViewportContainer.ToSharedRef(), EWowUiZone::Windows, 400);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(GViewportList, EFocusCause::SetDirectly);
	}
}

void SWowAddonList::CloseViewportInstance()
{
	if (GViewportContainer.IsValid() && GEngine && GEngine->GameViewport)
	{
		FWowUiSurfaces::Remove(GViewportContainer.ToSharedRef());
	}
	GViewportContainer.Reset();
	GViewportList.Reset();
}

bool SWowAddonList::IsOpen()
{
	return GViewportList.IsValid();
}
