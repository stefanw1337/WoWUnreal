#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Formats/M2Parser.h"
#include "WowFixturePolicy.h"
#include "WowLampFixtures.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowHearthModelsTest,"WowUnreal.Fixtures.HearthSources",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWowHearthModelsTest::RunTest(const FString&)
{
    TestFalse(TEXT("A directory named fire must not classify smoke as flame"),FWowFixturePolicy::IsFlameTexture(TEXT("BONFIRE/SMOKEWISPY03.BLP")));
    TestFalse(TEXT("An undead campfire is not an extinguished campfire"),FWowFixturePolicy::IsUnlit(TEXT("UNDEADCAMPFIRE.M2")));
    FString Directory;
    if(!FParse::Value(FCommandLine::Get(),TEXT("-hearthfixture="),Directory))
    {AddWarning(TEXT("Original hearth fixtures not supplied"));return true;}
    for(const TCHAR* Name:{TEXT("ELWYNNCAMPFIRE"),TEXT("ELWYNNCAMPFIRE_BLUE"),TEXT("ORCBONFIRE"),TEXT("ORCBONFIREOFF"),
        TEXT("FIREWOODPILE01"),TEXT("FIREWOODPILE04"),TEXT("UNDEADCAMPFIRE"),TEXT("LARGEFIREPIT01"),TEXT("SMALLFIREPIT01"),TEXT("OUTLANDDEADCAMPFIRE"),TEXT("SUMMERFEST_BONFIRE_OFF")})
    {
        TArray<uint8> Raw,Skin;
        if(!FFileHelper::LoadFileToArray(Raw,*(Directory/(FString(Name)+TEXT(".m2")))) ||
            !FFileHelper::LoadFileToArray(Skin,*(Directory/(FString(Name)+TEXT("00.skin")))))
        {AddError(TEXT("Missing original hearth: ")+FString(Name));continue;}
        const auto Data=FM2Parser::Parse(Raw,Skin);
        TestTrue(TEXT("Original hearth parses"),Data.bIsValid);
        FVector Position;FLinearColor Color;TArray<int32> Slots;
        if(FWowFixturePolicy::IsUnlit(Name))
        {
            TestFalse(TEXT("Extinguished model has no replacement flame"),FWowFixturePolicy::HasReplacementFlame(Data,Name));
            TestFalse(TEXT("Extinguished model has no managed light"),FWowLampFixtures::IsSupported(Name));
            continue;
        }
        TestTrue(TEXT("Lit campfire has active authored flame"),FWowFixturePolicy::HasReplacementFlame(Data,Name));
        TestTrue(TEXT("One fixture light source resolved"),FWowLampFixtures::FindM2Source(Data,Name,Position,Color,Slots));
        TestTrue(TEXT("Light is above fire base"),Position.Z>30);
        TestEqual(TEXT("Wood and stone are not made glowing glass"),Slots.Num(),0);
        int32 Kept=0;
        for(const auto& Pass:Data.RenderPasses) if(!FWowFixturePolicy::SkipLegacyPass(Data,Name,Pass)) ++Kept;
        TestTrue(TEXT("Solid firepit geometry survives halo filtering"),Kept>0);
        AddInfo(FString::Printf(TEXT("%s light=%s passes kept=%d/%d"),Name,*Position.ToString(),Kept,Data.RenderPasses.Num()));
    }
    return true;
}
#endif
