#include "Misc/AutomationTest.h"
#include "WowWeatherPolicy.h"
#include "WowPacketHandler.h"
#include "WowConnectionManager.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherStateTest,"WowUnreal.Weather.ServerStates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FWeatherStateTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("State 1 is fog, not rain"),WowWeather::DecodeState(1)==EWowWeatherKind::Fog);
    TestTrue(TEXT("State 3 is light rain, not storm"),WowWeather::DecodeState(3)==EWowWeatherKind::Rain);
    for(uint32 Id:{6u,7u,8u}) TestTrue(TEXT("All snow intensities decode"),WowWeather::DecodeState(Id)==EWowWeatherKind::Snow);
    for(uint32 Id:{22u,41u,42u}) TestTrue(TEXT("All sand intensities decode"),WowWeather::DecodeState(Id)==EWowWeatherKind::Dust);
    TestTrue(TEXT("Unknown does not spawn arbitrary rain"),WowWeather::DecodeState(999)==EWowWeatherKind::Clear);
    UWowConnectionManager* Connection=NewObject<UWowConnectionManager>();
    int Calls=0; uint32 Received=0; uint8 Abrupt=0;
    Connection->PacketHandler.OnWeatherUpdate.AddLambda([&](uint32 State,float Grade,uint8 Flag){ ++Calls; Received=State; Abrupt=Flag; });
    const auto Send=[&](float Grade) { const uint32 State=7; TArray<uint8> Packet; Packet.Append(reinterpret_cast<const uint8*>(&State),4); Packet.Append(reinterpret_cast<const uint8*>(&Grade),4); Packet.Add(1); Connection->PacketHandler.HandlePacket(WowOpcode::SMSG_WEATHER,Packet); };
    Send(0.6f); TestEqual(TEXT("Weather packet dispatched"),Calls,1); TestEqual(TEXT("Preserves snow state"),Received,7u); TestEqual(TEXT("Final byte is abrupt flag"),Abrupt,uint8(1));
    Send(std::numeric_limits<float>::quiet_NaN()); TestEqual(TEXT("Invalid grade ignored"),Calls,1);
    Connection->PacketHandler.HandlePacket(WowOpcode::SMSG_WEATHER,{1,2,3}); TestEqual(TEXT("Truncated packet ignored"),Calls,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherClimateTest,"WowUnreal.Weather.Climates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FWeatherClimateTest::RunTest(const FString& Parameters)
{
    FWowWeatherSample Rain; Rain.Kind=EWowWeatherKind::Rain; Rain.Intensity=0.6f; Rain.Wind=12;
    TestTrue(TEXT("Dun Morogh receives snow"),WowWeather::Adapt(Rain,WowWeather::Climate(TEXT("Dun Morogh"))).Kind==EWowWeatherKind::Snow);
    TestTrue(TEXT("Elwynn receives rain"),WowWeather::Adapt(Rain,WowWeather::Climate(TEXT("Elwynn Forest"))).Kind==EWowWeatherKind::Rain);
    TestTrue(TEXT("Tanaris receives dust"),WowWeather::Adapt(Rain,WowWeather::Climate(TEXT("Tanaris"))).Kind==EWowWeatherKind::Dust);
    TestTrue(TEXT("Tropical showers intensify"),WowWeather::Adapt(Rain,WowWeather::Climate(TEXT("Stranglethorn Vale"))).Intensity>Rain.Intensity);
    TestTrue(TEXT("Volcanic zones receive ash"),WowWeather::Adapt(Rain,WowWeather::Climate(TEXT("Burning Steppes"))).Kind==EWowWeatherKind::BlackSnow);
    // r723: mountain zones take rain or snow from the temperature; strong wind or a storm makes a blizzard.
    const EWowClimate Alterac=WowWeather::Climate(TEXT("Alterac Mountains"),36);
    TestTrue(TEXT("Alterac is a mountain zone"),Alterac==EWowClimate::Mountain);
    FWowWeatherSample WarmRain=Rain; WarmRain.Wind=3; WarmRain.TemperatureC=15; WarmRain.bHasTemperature=true;
    FWowWeatherSample ColdRain=WarmRain; ColdRain.TemperatureC=4;
    TestTrue(TEXT("Warm rain stays rain in Alterac"),WowWeather::Adapt(WarmRain,Alterac).Kind==EWowWeatherKind::Rain);
    TestTrue(TEXT("Cold rain falls as snow in Alterac"),WowWeather::Adapt(ColdRain,Alterac).Kind==EWowWeatherKind::Snow);
    TestTrue(TEXT("Realm rain without a temperature is kept"),WowWeather::Adapt(Rain,Alterac).Kind==EWowWeatherKind::Rain);
    FWowWeatherSample Snow=WarmRain; Snow.Kind=EWowWeatherKind::Snow; Snow.Intensity=0.4f;
    TestTrue(TEXT("Snow at home is snow in Alterac"),WowWeather::Adapt(Snow,Alterac).Kind==EWowWeatherKind::Snow);
    TestTrue(TEXT("Snow at home is rain in Elwynn"),WowWeather::Adapt(Snow,WowWeather::Climate(TEXT("Elwynn Forest"))).Kind==EWowWeatherKind::Rain);
    FWowWeatherSample WindySnow=Snow; WindySnow.Wind=14;
    const FWowWeatherSample Blizzard=WowWeather::Adapt(WindySnow,Alterac);
    TestTrue(TEXT("Windy snow becomes a blizzard"),Blizzard.Intensity>=0.85f && Blizzard.Wind>18.f);
    FWowWeatherSample Storm=ColdRain; Storm.Kind=EWowWeatherKind::Storm;
    TestTrue(TEXT("A cold storm is a blizzard in Dun Morogh"),WowWeather::Adapt(Storm,WowWeather::Climate(TEXT("Dun Morogh"))).Wind>=44.f);
    FWowWeatherSample HomeFog; HomeFog.Kind=EWowWeatherKind::Fog; HomeFog.Intensity=0.7f; HomeFog.Wind=2;
    TestTrue(TEXT("Fog at home is no sandstorm in Tanaris"),WowWeather::Adapt(HomeFog,WowWeather::Climate(TEXT("Tanaris"))).Kind==EWowWeatherKind::Clear);
    TestFalse(TEXT("Reject invalid latitude"),WowWeather::CoordinatesValid(91,10));
    TestTrue(TEXT("Explicit zero coordinates are valid"),WowWeather::CoordinatesValid(0,0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherProviderTest,"WowUnreal.Weather.ProviderValidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FWeatherProviderTest::RunTest(const FString& Parameters)
{
    const int64 Now=1800000000;
    const FString Valid=TEXT("{\"current\":{\"time\":1800000000,\"weather_code\":73,\"cloud_cover\":90,\"wind_speed_10m\":8,\"wind_direction_10m\":270}}");
    FWowWeatherSample Sample; int64 Stamp=0;
    TestTrue(TEXT("Parses current API response"),WowWeather::ParseCurrent(Valid,Now,Sample,Stamp));
    TestTrue(TEXT("73 means moderate snow"),Sample.Kind==EWowWeatherKind::Snow);
    TestEqual(TEXT("Uses metres per second"),Sample.Wind,8.f);
    TestFalse(TEXT("Temperature is optional"),Sample.bHasTemperature);
    FWowWeatherSample Warm; int64 WarmStamp=0;
    TestTrue(TEXT("Parses the temperature"),WowWeather::ParseCurrent(Valid.Replace(TEXT("\"wind_direction_10m\":270"),TEXT("\"wind_direction_10m\":270,\"temperature_2m\":-3.5")),Now,Warm,WarmStamp)
        && Warm.bHasTemperature && FMath::IsNearlyEqual(Warm.TemperatureC,-3.5f));
    TestFalse(TEXT("Expired conditions rejected"),WowWeather::ParseCurrent(Valid,Now+7201,Sample,Stamp));
    TestFalse(TEXT("Future conditions rejected"),WowWeather::ParseCurrent(Valid,Now-901,Sample,Stamp));
    TestFalse(TEXT("Missing fields rejected"),WowWeather::ParseCurrent(TEXT("{\"current\":{\"weather_code\":63}}"),Now,Sample,Stamp));
    TestFalse(TEXT("HTML/error rejected"),WowWeather::ParseCurrent(TEXT("Service unavailable"),Now,Sample,Stamp));
    TestFalse(TEXT("Unknown weather code rejected"),WowWeather::ParseCurrent(Valid.Replace(TEXT(":73"),TEXT(":999")),Now,Sample,Stamp));
    TestTrue(TEXT("Failures preserve last valid sample"),Sample.Kind==EWowWeatherKind::Snow);
    TestEqual(TEXT("Failures preserve timestamp"),Stamp,Now);
    return true;
}
