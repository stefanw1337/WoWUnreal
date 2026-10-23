#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../WowCharacterAppearance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowGnomeHairChoicesTest,
    "WowUnreal.CharacterCreation.GnomeHairChoices",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowGnomeHairChoicesTest::RunTest(const FString&)
{
    for (uint32 Gender : {0u, 1u})
    {
        TArray<FCharSectionsDbcEntry> Sections;
        TArray<FCharHairGeosetsDbcEntry> Geosets;
        // Gnome styles 0-6 use flag 17; styles 7-11 use flag 18.
        for (uint32 Style = 0; Style < 12; ++Style)
        {
            FCharHairGeosetsDbcEntry Hair;
            Hair.RaceID = 7; Hair.SexID = Gender; Hair.VariationID = Style;
            Geosets.Add(Hair);
            for (uint32 Color = 0; Color < 9; ++Color)
            {
                FCharSectionsDbcEntry Row;
                Row.RaceID = 7; Row.SexID = Gender; Row.Type = 3;
                Row.Variation = Style; Row.Color = Color; Row.Flags = Style < 7 ? 17 : 18;
                Sections.Add(Row);
            }
        }
        TestEqual(TEXT("All twelve player styles remain available"),
            WowCharacterAppearance::HairChoices(Sections, Geosets, 7, Gender).Num(), 12);
        for (int32 Style = 0; Style < 12; ++Style)
            TestEqual(TEXT("Every offered style retains all nine standard colors"),
                WowCharacterAppearance::HairChoices(Sections, Geosets, 7, Gender, Style).Num(), 9);
        TestTrue(TEXT("Opposite gender cannot borrow these options"),
            WowCharacterAppearance::HairChoices(Sections, Geosets, 7, 1-Gender).IsEmpty());
        Sections.RemoveAll([](const auto& Row) { return Row.Variation == 11; });
        TestFalse(TEXT("Geoset without color data is not offered"),
            WowCharacterAppearance::HairChoices(Sections, Geosets, 7, Gender).Contains(11));
        for (auto& Row : Sections) if (Row.Variation == 10) Row.Flags = 8;
        TestFalse(TEXT("NPC-only color rows are excluded"),
            WowCharacterAppearance::HairChoices(Sections, Geosets, 7, Gender).Contains(10));
    }
    return true;
}
#endif
