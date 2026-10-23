#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
class FWowPacketHandler;
class SScrollBox;
class SWowSkills : public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SWowSkills){} SLATE_END_ARGS()
 void Construct(const FArguments&, FWowPacketHandler*);
 void Toggle();
 virtual void Tick(const FGeometry&, double, float) override;
private:
 void Refresh();
 FWowPacketHandler* Handler=nullptr;
 TSharedPtr<SScrollBox> List;
 TSet<uint32> Collapsed;
 uint32 Signature=0;
 double NextUpdate=0;
};
