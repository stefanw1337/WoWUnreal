#include "Misc/AutomationTest.h"
#include "../UI/SWowMinimap.h"
#include "Coord/WowCoordinate.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowMinimapProjectionTest,"WowUnreal.Minimap.ZoomAndProjection",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowMinimapProjectionTest::RunTest(const FString&)
{
    auto Map=SNew(SWowMinimap).EntityManager(nullptr).WorldManager(nullptr);
    const FVector Player=FWowCoordinate::WowToUE(FVector(-6100,380,400));
    const FVector2D Center(92,92);
    const float Width=Map->GetCaptureWidth();
    TestEqual(TEXT("Capture diameter matches 100-yard radius"),Width,20000.f);
    const auto East=Map->WorldToMinimapCoords(Player+FVector(0,5000,0),Player,Center,90,0);
    TestTrue(TEXT("50 yards east lies halfway to east edge"),East.Equals(Center+FVector2D(45,0),.01));
    const auto North=Map->WorldToMinimapCoords(Player+FVector(5000,0,0),Player,Center,90,2);
    TestTrue(TEXT("North stays up regardless of player yaw"),North.Equals(Center+FVector2D(0,-45),.01));
    Map->ZoomIn();TestTrue(TEXT("Zoom changes actual capture width"),Map->GetCaptureWidth()<Width);
    const auto Zoomed=Map->WorldToMinimapCoords(Player+FVector(0,5000,0),Player,Center,90,0);
    TestTrue(TEXT("Markers follow the same zoom"),Zoomed.X>East.X);
    Map->ZoomOut();TestEqual(TEXT("Plus/minus reverses zoom"),Map->GetCaptureWidth(),Width);
    for(int I=0;I<30;++I)Map->ZoomIn();TestEqual(TEXT("Minimum zoom bounded"),Map->GetCaptureWidth(),10000.f);
    for(int I=0;I<30;++I)Map->ZoomOut();TestEqual(TEXT("Maximum zoom bounded"),Map->GetCaptureWidth(),100000.f);
    return true;
}
#endif
