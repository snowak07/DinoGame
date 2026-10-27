#include "AI/DinoNoiseRules.h"
#include "AI/DinoTargetRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DinoTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr double Cooldown = 2.0;
	constexpr float OutOfSight = DinoTargetRules::NotInSight;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoTargetSwitchTest, "DinoGame.AI.Target.ClosestVisibleWins", DinoTestFlags)
bool FDinoTargetSwitchTest::RunTest(const FString& Parameters)
{
	// Columns: current target's distance (or out of sight), nearest other visible player's,
	// seconds since the last change of target, attacking, expected switch.
	struct FCase { float Current, Candidate; double Since; bool bAttacking, bSwitch; const TCHAR* Why; };
	const FCase Cases[] = {
		{ 1000.0f,  999.0f, 5.0, false, true,  TEXT("closer by any amount wins - no margin") },
		{ 1000.0f, 1000.0f, 5.0, false, false, TEXT("equally close: stay") },
		{ 1000.0f, 1500.0f, 5.0, false, false, TEXT("farther: stay") },
		{ 1000.0f,  200.0f, 1.9, false, false, TEXT("closer, but inside the 2 s commitment") },
		{ 1000.0f,  200.0f, 2.0, false, true,  TEXT("closer, commitment just over") },
		{ OutOfSight,   3000.0f, 5.0, false, true,  TEXT("current out of sight: the visible one wins, however far") },
		{ OutOfSight,   3000.0f, 1.0, false, false, TEXT("current out of sight, but inside the commitment") },
		{ 1000.0f,  100.0f, 5.0, true,  false, TEXT("never mid-attack: a raptor finishes who it pinned") },
		{ OutOfSight,    100.0f, 5.0, true,  false, TEXT("never mid-attack, even with its victim out of sight") },
	};

	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("current=%.0f candidate=%.0f since=%.1f attacking=%d (%s)"),
			Case.Current, Case.Candidate, Case.Since, Case.bAttacking, Case.Why),
			DinoTargetRules::ShouldSwitchTarget(Case.Current, Case.Candidate, Case.Since, Cooldown, Case.bAttacking), Case.bSwitch);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoInvestigateTest, "DinoGame.AI.Target.SoundNeverRedirectsAHunt", DinoTestFlags)
bool FDinoInvestigateTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("idle or searching: investigates"), DinoTargetRules::ShouldInvestigate(false, false));
	TestFalse(TEXT("hunting: ignores it"), DinoTargetRules::ShouldInvestigate(true, false));
	TestFalse(TEXT("attacking: ignores it"), DinoTargetRules::ShouldInvestigate(false, true));
	TestFalse(TEXT("both: ignores it"), DinoTargetRules::ShouldInvestigate(true, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoCloseSenseTest, "DinoGame.AI.Target.CloseSense", DinoTestFlags)
bool FDinoCloseSenseTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("just inside 3 m, clear line"), DinoTargetRules::IsWithinCloseSense(299.0f, 300.0f, false));
	TestTrue(TEXT("exactly 3 m"), DinoTargetRules::IsWithinCloseSense(300.0f, 300.0f, false));
	TestFalse(TEXT("just outside"), DinoTargetRules::IsWithinCloseSense(301.0f, 300.0f, false));
	TestFalse(TEXT("inside, but through a wall"), DinoTargetRules::IsWithinCloseSense(50.0f, 300.0f, true));
	TestTrue(TEXT("touching"), DinoTargetRules::IsWithinCloseSense(-10.0f, 300.0f, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoVoiceNoiseTest, "DinoGame.AI.Noise.VoiceCarries", DinoTestFlags)
bool FDinoVoiceNoiseTest::RunTest(const FString& Parameters)
{
	// Loudness, judged against the speaker's own threshold.
	TestEqual(TEXT("at the send threshold: quietest"), DinoNoiseRules::VoiceLoudness(0.03f, 0.03f, 0.3f), 0.0f);
	TestEqual(TEXT("at the shout level: loudest"), DinoNoiseRules::VoiceLoudness(0.3f, 0.03f, 0.3f), 1.0f);
	TestEqual(TEXT("halfway"), DinoNoiseRules::VoiceLoudness(0.165f, 0.03f, 0.3f), 0.5f, 0.001f);
	TestEqual(TEXT("past a shout clamps"), DinoNoiseRules::VoiceLoudness(0.9f, 0.03f, 0.3f), 1.0f);
	TestEqual(TEXT("below the threshold clamps"), DinoNoiseRules::VoiceLoudness(0.0f, 0.03f, 0.3f), 0.0f);
	TestEqual(TEXT("a quiet mic with a low threshold reads like a loud one with a high threshold"),
		DinoNoiseRules::VoiceLoudness(0.02f, 0.01f, 0.03f), DinoNoiseRules::VoiceLoudness(0.2f, 0.1f, 0.3f), 0.001f);
	TestEqual(TEXT("shout level at or below the threshold: any speech is a shout"), DinoNoiseRules::VoiceLoudness(0.05f, 0.05f, 0.05f), 1.0f);

	// Range.
	TestEqual(TEXT("whisper in the open"), DinoNoiseRules::HeardRange(0.0f, false, 500.0f, 2500.0f, 0.5f), 500.0f);
	TestEqual(TEXT("shout in the open"), DinoNoiseRules::HeardRange(1.0f, false, 500.0f, 2500.0f, 0.5f), 2500.0f);
	TestEqual(TEXT("halfway in the open"), DinoNoiseRules::HeardRange(0.5f, false, 500.0f, 2500.0f, 0.5f), 1500.0f);
	TestEqual(TEXT("shout through a wall is halved"), DinoNoiseRules::HeardRange(1.0f, true, 500.0f, 2500.0f, 0.5f), 1250.0f);
	TestEqual(TEXT("wall factor clamps to 0-1"), DinoNoiseRules::HeardRange(1.0f, true, 500.0f, 2500.0f, 2.0f), 2500.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoNoiseDrawTest, "DinoGame.AI.Noise.DebugDrawKeepsTheLoudest", DinoTestFlags)
bool FDinoNoiseDrawTest::RunTest(const FString& Parameters)
{
	constexpr double Window = 4.5;
	TestTrue(TEXT("nothing drawn yet: draw"), DinoNoiseRules::ShouldDrawNoise(10.0, -1.0e9, 0.0f, 0.2f, Window));
	TestFalse(TEXT("shorter, while the last is still up: skip"), DinoNoiseRules::ShouldDrawNoise(11.0, 10.0, 0.5f, 0.3f, Window));
	TestFalse(TEXT("as far, while the last is still up: skip"), DinoNoiseRules::ShouldDrawNoise(11.0, 10.0, 0.5f, 0.5f, Window));
	TestTrue(TEXT("further, while the last is still up: draw"), DinoNoiseRules::ShouldDrawNoise(11.0, 10.0, 0.5f, 0.6f, Window));
	TestTrue(TEXT("shorter, but the last has expired: draw"), DinoNoiseRules::ShouldDrawNoise(14.5, 10.0, 0.9f, 0.1f, Window));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoFootstepTest, "DinoGame.AI.Noise.Footsteps", DinoTestFlags)
bool FDinoFootstepTest::RunTest(const FString& Parameters)
{
	// Speed, max speed, crouched, on the ground, expected range (walk range 600).
	TestEqual(TEXT("full walking speed: full range"), DinoNoiseRules::FootstepRange(500.0f, 500.0f, false, true, 600.0f), 600.0f);
	TestEqual(TEXT("half speed: half range"), DinoNoiseRules::FootstepRange(250.0f, 500.0f, false, true, 600.0f), 300.0f);
	TestEqual(TEXT("crouch-walking is silent"), DinoNoiseRules::FootstepRange(300.0f, 300.0f, true, true, 600.0f), 0.0f);
	TestEqual(TEXT("in the air is silent (landing is its own noise)"), DinoNoiseRules::FootstepRange(500.0f, 500.0f, false, false, 600.0f), 0.0f);
	TestEqual(TEXT("barely moving is silent"), DinoNoiseRules::FootstepRange(30.0f, 500.0f, false, true, 600.0f), 0.0f);
	TestEqual(TEXT("faster than max clamps"), DinoNoiseRules::FootstepRange(900.0f, 500.0f, false, true, 600.0f), 600.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoCrouchStealthTest, "DinoGame.AI.Target.CrouchingHides", DinoTestFlags)
bool FDinoCrouchStealthTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("standing: full range"), DinoTargetRules::StealthRange(4000.0f, false, 0.6f), 4000.0f);
	TestEqual(TEXT("crouched: the factor of it"), DinoTargetRules::StealthRange(4000.0f, true, 0.6f), 2400.0f);
	TestEqual(TEXT("close sense crouched: 3 m to 1.5 m"), DinoTargetRules::StealthRange(300.0f, true, 0.5f), 150.0f);
	TestEqual(TEXT("a factor above 1 never reveals more"), DinoTargetRules::StealthRange(4000.0f, true, 2.0f), 4000.0f);
	TestEqual(TEXT("a negative factor clamps to hidden"), DinoTargetRules::StealthRange(4000.0f, true, -1.0f), 0.0f);
	return true;
}

#endif
