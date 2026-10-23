#include "Misc/AutomationTest.h"
#include "WowAnimationController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementAnimationAirborneContract,
	"WowUnreal.Movement.Animation.AirborneContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMovementAnimationAirborneContract::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A player jump stays Jumping while rising"),
		FWowMovementAnimationPolicy::ResolveAirborneState(true, true, 0.1, EWowAnimState::Running),
		EWowAnimState::Jumping);
	TestEqual(TEXT("A player jump stays Jumping throughout descent"),
		FWowMovementAnimationPolicy::ResolveAirborneState(true, false, 5.0, EWowAnimState::Jumping),
		EWowAnimState::Jumping);
	TestEqual(TEXT("A tiny uninitiated curb drop preserves locomotion"),
		FWowMovementAnimationPolicy::ResolveAirborneState(false, false, 0.19, EWowAnimState::Running),
		EWowAnimState::Running);
	TestEqual(TEXT("A genuine ledge drop selects Fall"),
		FWowMovementAnimationPolicy::ResolveAirborneState(false, false, 0.20, EWowAnimState::Running),
		EWowAnimState::Falling);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementAnimationApexContract,
	"WowUnreal.Movement.Animation.ApexContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMovementAnimationApexContract::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("The authored JumpStart pose freezes once at physical descent"),
		FWowMovementAnimationPolicy::ShouldFreezeJumpStartAtApex(true, true, true, false));
	TestFalse(TEXT("A genuine fall never freezes a jump pose"),
		FWowMovementAnimationPolicy::ShouldFreezeJumpStartAtApex(true, false, true, false));
	TestFalse(TEXT("The apex hold is not restarted every frame"),
		FWowMovementAnimationPolicy::ShouldFreezeJumpStartAtApex(true, true, true, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementAnimationLandingContract,
	"WowUnreal.Movement.Animation.LandingContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMovementAnimationLandingContract::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("A stationary landing may play JumpEnd"),
		FWowMovementAnimationPolicy::ShouldPlayStationaryLanding(false, 0.0f, true));
	TestFalse(TEXT("Movement intent skips the planted landing"),
		FWowMovementAnimationPolicy::ShouldPlayStationaryLanding(true, 0.0f, true));
	TestFalse(TEXT("Existing horizontal movement skips the planted landing"),
		FWowMovementAnimationPolicy::ShouldPlayStationaryLanding(false, 35.1f, true));
	TestFalse(TEXT("A missing JumpEnd clip cannot lock locomotion"),
		FWowMovementAnimationPolicy::ShouldPlayStationaryLanding(false, 0.0f, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementAnimationSwimIdleContract,
	"WowUnreal.Movement.Animation.SwimIdleContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMovementAnimationSwimIdleContract::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A settled swimmer uses the authored SwimIdle loop"),
		FWowMovementAnimationPolicy::ResolveSwimmingState(0.0f),
		EWowAnimState::SwimmingIdle);
	TestEqual(TEXT("Residual motion below the threshold remains SwimIdle"),
		FWowMovementAnimationPolicy::ResolveSwimmingState(10.0f),
		EWowAnimState::SwimmingIdle);
	TestEqual(TEXT("Horizontal or vertical travel uses the Swim loop"),
		FWowMovementAnimationPolicy::ResolveSwimmingState(10.1f),
		EWowAnimState::Swimming);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEmoteMovementOwnership335,
    "WowUnreal.Animation.EmoteMovementOwnership335", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FEmoteMovementOwnership335::RunTest(const FString& Parameters)
{
    UWowAnimationController* Controller = NewObject<UWowAnimationController>();
    Controller->bPlayingEmote = true;
    Controller->bEmoteLoops = true;
    FWowMovementInfo Movement;
    Controller->UpdateAnimationState(Movement);
    TestTrue(TEXT("Idle preserves dance ownership"),Controller->bPlayingEmote);
    Movement.MoveFlags = WowMovementFlags::BACKWARD;
    Controller->UpdateAnimationState(Movement);
    TestFalse(TEXT("Backpedal releases dance"),Controller->bPlayingEmote);
    Controller->bPlayingEmote = true;
    Controller->NotifyLocalMovementIntent();
    TestFalse(TEXT("Fresh local input immediately releases dance"),Controller->bPlayingEmote);
    return true;
}
