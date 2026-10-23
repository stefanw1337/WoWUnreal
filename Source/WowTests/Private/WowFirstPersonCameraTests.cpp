#include "Misc/AutomationTest.h"
#include "WowFirstPersonCameraPolicy.h"
#include "WowCharacterInspectionCameraPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWowFirstPersonRaceCameraTest,
	"WowUnreal.Camera.FirstPerson.RaceProfileContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWowFirstPersonRaceCameraTest::RunTest(const FString& Parameters)
{
	const FWowFirstPersonCameraProfile Human = FWowFirstPersonCameraPolicy::Resolve(1, 0);
	const FWowFirstPersonCameraProfile Dwarf = FWowFirstPersonCameraPolicy::Resolve(3, 0);
	const FWowFirstPersonCameraProfile FemaleDwarf = FWowFirstPersonCameraPolicy::Resolve(3, 1);
	const FWowFirstPersonLookDownOffset LevelLook =
		FWowFirstPersonCameraPolicy::ResolveLookDownOffset(0.0f);
	const FWowFirstPersonLookDownOffset ModerateLook =
		FWowFirstPersonCameraPolicy::ResolveLookDownOffset(-51.0f);
	const FWowFirstPersonLookDownOffset SteepLook =
		FWowFirstPersonCameraPolicy::ResolveLookDownOffset(-78.0f);

	TestTrue(TEXT("Dwarf eye stays behind its forward two-handed weapon arc"),
		Dwarf.ForwardOffset <= 4.0f && Dwarf.ForwardOffset < Human.ForwardOffset);
	TestTrue(TEXT("Dwarf keeps a valid full-body eye height"),
		Dwarf.FallbackEyeHeight > 80.0f);
	TestTrue(TEXT("Default race profile remains compact"),
		Human.ForwardOffset >= 15.0f && Human.ForwardOffset <= 30.0f);
	TestEqual(TEXT("Female anatomy adjustment preserves race profile"),
		FemaleDwarf.ForwardOffset, Dwarf.ForwardOffset - 2.0f);
	TestTrue(TEXT("Female anatomy adjustment lowers the fallback eye"),
		FemaleDwarf.FallbackEyeHeight < Dwarf.FallbackEyeHeight);
	TestTrue(TEXT("Head equipment is hidden only in the owner's first-person view"),
		FWowFirstPersonCameraPolicy::ShouldHideHeadEquipmentFromOwner(true, false));
	TestFalse(TEXT("Optional visor view keeps head equipment visible"),
		FWowFirstPersonCameraPolicy::ShouldHideHeadEquipmentFromOwner(true, true));
	TestFalse(TEXT("Head equipment returns immediately in third person"),
		FWowFirstPersonCameraPolicy::ShouldHideHeadEquipmentFromOwner(false, false));
	TestEqual(TEXT("Level running retains the approved weapon-safe eye point"),
		LevelLook.Forward, 0.0f);
	TestTrue(TEXT("Look-down clearance begins progressively"),
		ModerateLook.Forward > 0.0f && ModerateLook.Forward < SteepLook.Forward);
	TestTrue(TEXT("Steep look moves above and ahead of head/spine geometry"),
		SteepLook.Forward >= 17.0f && SteepLook.Up >= 6.5f);
	TestTrue(TEXT("First-person near clip is positive and tighter than the gameplay default"),
		FWowFirstPersonCameraPolicy::PerspectiveNearClipPlane > 0.0f &&
		FWowFirstPersonCameraPolicy::PerspectiveNearClipPlane <= 2.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWowCharacterInspectionCameraTest,
    "WowUnreal.Camera.Inspection.SmallCharacterFramingAndFocus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWowCharacterInspectionCameraTest::RunTest(const FString&)
{
    using Policy = FWowCharacterInspectionCameraPolicy;
    // Dwarf height from the reported client's runtime log; the capsule origin is above its feet.
    const float DwarfBody = Policy::BodyFocusHeight(-88.0f, 155.8f);
    const float GnomeBody = Policy::BodyFocusHeight(-88.0f, 110.0f);
    const float HumanBody = Policy::BodyFocusHeight(-88.0f, 200.0f);
    TestTrue(TEXT("Short character focus is based on its mesh, not the capsule"), GnomeBody < DwarfBody && DwarfBody < HumanBody);
    TestEqual(TEXT("Normal gameplay orbit remains unchanged"), Policy::PivotHeight(760, 180, DwarfBody, 57.2f), 180.0f);
    for (const float Height : {110.0f, 155.8f, 200.0f})
    {
        const float Body = Policy::BodyFocusHeight(-88.0f, Height);
        const float Pivot = Policy::PivotHeight(400, 180, Body, 57.2f);
        TestTrue(TEXT("Near orbit targets the visible upper body"), Pivot > -88.0f + Height * .5f && Pivot < -88.0f + Height);
        TestTrue(TEXT("Head stays near screen center at level close zoom"),
            FMath::RadiansToDegrees(FMath::Atan2((-88.0f + Height) - Pivot, 400.0f)) < 10.0f);
    }
    TestEqual(TEXT("Orbit stays at body height before first-person transition"), Policy::PivotHeight(150, 180, DwarfBody, 57.2f), DwarfBody);
    TestEqual(TEXT("First-person eye remains unchanged"), Policy::PivotHeight(20, 180, DwarfBody, 57.2f), 57.2f);
    TestEqual(TEXT("No early camera push through the body"), Policy::EyeBlend(150), 0.0f);
    for (float Zoom = 21.0f; Zoom < 800.0f; Zoom += 1.0f)
        TestTrue(TEXT("Zoom transitions are continuous"), FMath::Abs(Policy::PivotHeight(Zoom, 180, DwarfBody, 57.2f)
            - Policy::PivotHeight(Zoom - 1, 180, DwarfBody, 57.2f)) < 2.0f);
    TestFalse(TEXT("Normal third-person zoom keeps aim focus"), Policy::ShouldFocusCharacter(600));
    TestFalse(TEXT("Medium zoom keeps aim focus"), Policy::ShouldFocusCharacter(350));
    TestTrue(TEXT("Deliberate close zoom inspects the character"), Policy::ShouldFocusCharacter(200));
    TestTrue(TEXT("Near zoom boundary inspects the character"), Policy::ShouldFocusCharacter(300));
    TestFalse(TEXT("Beyond near zoom follows the scene"), Policy::ShouldFocusCharacter(301));
    TestFalse(TEXT("First-person scene still uses autofocus"), Policy::ShouldFocusCharacter(20));
    TestFalse(TEXT("Normal zoom shortened by a wall must not force character focus"), Policy::ShouldFocusCharacter(760));
    TestEqual(TEXT("Entering close zoom does not wait for distant focus"), Policy::FocusDistance(50000, 450, 1.f/60, true), 450.0f);
    TestEqual(TEXT("Tracked body stays focused while scrolling"), Policy::FocusDistance(600, 200, 1.f/60, true), 200.0f);
    TestTrue(TEXT("Scene autofocus retains smoothing"), Policy::FocusDistance(450, 50000, 1.f/60, false) < 50000);
    return true;
}

#endif
