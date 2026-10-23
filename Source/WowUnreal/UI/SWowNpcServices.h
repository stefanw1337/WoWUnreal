#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "WowRetainedSlateBrush.h"
class UWowConnectionManager;class SScrollBox;class SEditableTextBox;
class SWowNpcServices:public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SWowNpcServices){} SLATE_END_ARGS()
 void Construct(const FArguments&,UWowConnectionManager*);
 void Open();
 /** For spell icons (textures come from the frame manager's MPQ loader). */
 void SetUIManager(class UWowUIManager* InUIManager);
 void Close();
 virtual void Tick(const FGeometry&,double,float)override;
private:
 void Refresh();FReply Search(bool Own=false);FReply Purchase(uint32,uint32);
 TWeakObjectPtr<UWowConnectionManager> Connection;
 TSharedPtr<SScrollBox> Rows;TSharedPtr<SEditableTextBox> Query,StartPrice,BuyoutPrice;
 FString SellConfirmation;
 int32 ItemCount=-1;
 uint32 Revision=0,Page=0,ConfirmId=0,ConfirmPrice=0;bool Own=false;
 double NextRefresh=0;
 // Trainer window (ClassTrainerFrame-like): list, details of the selected spell, Train/Exit.
 TWeakObjectPtr<class UWowUIManager> UIManager;
 uint32 SelectedSpell=0;
 TSharedPtr<class SBox> TrainerDetail;
 TMap<uint32,TSharedPtr<FWowRetainedSlateBrush>> IconBrushes;
 const struct FSlateBrush* SpellIcon(uint32 Spell);
 void RefreshTrainerDetail();
 // ClassTrainerFrame look (Stefan 11.10): stock art, list, details, money.
 TSharedPtr<SScrollBox> TrainerRows;
 TMap<FString,TSharedPtr<FWowRetainedSlateBrush>> ArtBrushes;
 const struct FSlateBrush* Art(const FString& Path,float U0,float V0,float U1,float V1);
 TSharedRef<class SWidget> MoneyText(TAttribute<uint32> Copper,TAttribute<struct FSlateColor> Color,int32 FontSize);
 TSharedRef<class SWidget> BuildTrainer();
 FString TrainerTitle();
 void RefreshTrainerList();
 TSharedRef<class SWidget> ArtImage(const FString& Path,float U0,float V0,float U1,float V1);
 TSharedRef<class SWidget> FilterRow(const TCHAR* Label,bool* Flag);
 const struct FSlateBrush* TrainerPortrait();
 FString TrainerCategory(uint32 Spell) const;
 bool AllHeadersCollapsed() const;
 void ToggleAllHeaders();
 // The client's trainer filter (Available, Unavailable on; Used off) and collapsed skill-line headers.
 bool bFilterAvailable=true,bFilterUnavailable=true,bFilterUsed=false,bFilterMenuOpen=false;
 TSet<FString> CollapsedHeaders;
 TArray<FString> HeaderNames;
 TSharedPtr<FWowRetainedSlateBrush> PortraitBrush;
};
