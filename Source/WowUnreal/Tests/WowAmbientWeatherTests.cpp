#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "WowWeatherPolicy.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowAmbientWeatherTest,"WowUnreal.Weather.AmbientBaseline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowAmbientWeatherTest::RunTest(const FString&)
{
    TestEqual(TEXT("No weather in loading/selection"),WowWeather::AmbientStrength(0,100,2),0.f);
    for(uint32 Zone : {1u,12u,14u,33u,46u})
        for(int32 Seconds=0; Seconds<86400; Seconds+=37)
            for(float Wind : {0.f,2.f,20.f,56.f})
            {
                const float A=WowWeather::AmbientStrength(Zone,Seconds,Wind);
                if(A<.10f || A>.22001f) { AddError(TEXT("Ambient baseline exceeded bounds")); return false; }
                if(FMath::Abs(A-WowWeather::AmbientStrength(Zone,Seconds+1,Wind))>.002f)
                { AddError(TEXT("Atmosphere changed abruptly")); return false; }
            }
    TestFalse(TEXT("Zones do not breathe in lockstep"),FMath::IsNearlyEqual(
        WowWeather::AmbientStrength(12,100,2),WowWeather::AmbientStrength(14,100,2)));
    FWowWeatherSample Clear;
    TestEqual(TEXT("Dry atmospheric baseline does not require rain"),Clear.Intensity,0.f);
    return true;
}
#endif
