#include "Misc/AutomationTest.h"
#include "WowAudioManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnderwaterAudioPolicyContract,
	"WowUnreal.Audio.Underwater.PolicyContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FUnderwaterAudioPolicyContract::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Dry camera and dry head leave the normal mix active"),
		FWowUnderwaterAudioPolicy::ShouldEnable(false, false));
	TestTrue(TEXT("A submerged third-person camera enables the underwater mix"),
		FWowUnderwaterAudioPolicy::ShouldEnable(true, false));
	TestFalse(TEXT("The listener is the camera: a submerged head under a dry camera keeps the normal mix"),
		FWowUnderwaterAudioPolicy::ShouldEnable(false, true));

	TestEqual(TEXT("Fully submerged music receives an obvious low-pass"),
		FWowUnderwaterAudioPolicy::ResolveLowPassFrequency(1.0f), 300.0f);
	TestEqual(TEXT("Fully submerged music is attenuated"),
		FWowUnderwaterAudioPolicy::ResolveMusicVolumeScale(1.0f), 0.50f);
	TestEqual(TEXT("Fully submerged ambience is attenuated below music"),
		FWowUnderwaterAudioPolicy::ResolveAmbienceVolumeScale(1.0f), 0.28f);
	TestEqual(TEXT("Leaving water restores the unfiltered cutoff"),
		FWowUnderwaterAudioPolicy::ResolveLowPassFrequency(0.0f), 20000.0f);
	return true;
}
