#include "DinoCharacter.h"
#include "DinoDamageTypes.h"
#include "DinoHealthRules.h"
#include "DinoTestCharacter.h"
#include "DinoTestWorld.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DinoTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	using namespace DinoHealthRules;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoHealthDamageTest, "DinoGame.Health.DownedThenDead", DinoTestFlags)
bool FDinoHealthDamageTest::RunTest(const FString& Parameters)
{
	FVitals V;
	TestTrue(TEXT("a hit that leaves health: hurt"), ApplyDamage(V, 30.0f, true) == EChange::Hurt);
	TestEqual(TEXT("...health down"), V.Health, 70.0f);

	TestTrue(TEXT("a downing hit past zero: down, not dead"), ApplyDamage(V, 80.0f, true) == EChange::Downed);
	TestTrue(TEXT("...downed"), V.bDowned);
	TestEqual(TEXT("...with a full bleed-out meter"), V.BleedOut, BleedOutFull);
	TestFalse(TEXT("...not dead"), IsDead(V));
	TestFalse(TEXT("...not standing"), IsStanding(V));

	TestTrue(TEXT("a raptor still biting a downed player drains bleed-out"), ApplyDamage(V, 30.0f, true) == EChange::Bleeding);
	TestEqual(TEXT("...point for point"), V.BleedOut, 70.0f);

	TestTrue(TEXT("...until it runs out: dead"), ApplyDamage(V, 80.0f, true) == EChange::Died);
	TestTrue(TEXT("...dead"), IsDead(V));
	TestTrue(TEXT("nothing hurts the dead"), ApplyDamage(V, 10.0f, true) == EChange::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoHealthKillTest, "DinoGame.Health.KillingBlowsSkipDowned", DinoTestFlags)
bool FDinoHealthKillTest::RunTest(const FString& Parameters)
{
	// A T-Rex's bite and a fall kill outright: no down, standing or downed.
	FVitals Standing;
	TestTrue(TEXT("a killing hit from full: dead, never down"), ApplyDamage(Standing, 101.0f, false) == EChange::Died);
	TestFalse(TEXT("...never downed"), Standing.bDowned);

	FVitals Downed{ 0.0f, true, 90.0f };
	TestTrue(TEXT("a killing hit on a downed player: dead, whatever bleed-out is left"), ApplyDamage(Downed, 1.0f, false) == EChange::Died);
	TestTrue(TEXT("...dead"), IsDead(Downed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoHealthBleedTest, "DinoGame.Health.BleedOutAndRevive", DinoTestFlags)
bool FDinoHealthBleedTest::RunTest(const FString& Parameters)
{
	FVitals V{ 0.0f, true, BleedOutFull };
	TestTrue(TEXT("bleeding"), Bleed(V, 22.5f, 45.0f) == EChange::Bleeding);
	TestEqual(TEXT("half the time: half the meter"), V.BleedOut, 50.0f, 0.01f);
	TestTrue(TEXT("the rest of the time: dead"), Bleed(V, 22.5f, 45.0f) == EChange::Died);
	TestTrue(TEXT("...dead"), IsDead(V));

	FVitals Up;
	TestTrue(TEXT("standing players do not bleed"), Bleed(Up, 10.0f, 45.0f) == EChange::None);

	FVitals Down{ 0.0f, true, 10.0f };
	TestTrue(TEXT("a downed player can be revived"), Revive(Down, 100.0f, 0.2f));
	TestEqual(TEXT("...at 20% health"), Down.Health, 20.0f);
	TestTrue(TEXT("...standing"), IsStanding(Down));
	TestFalse(TEXT("a standing player cannot be revived"), Revive(Down, 100.0f, 0.2f));

	FVitals Dead{ 0.0f, false, 0.0f };
	TestFalse(TEXT("the dead cannot be revived"), Revive(Dead, 100.0f, 0.2f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoReviveRulesTest, "DinoGame.Health.WhoCanRevive", DinoTestFlags)
bool FDinoReviveRulesTest::RunTest(const FString& Parameters)
{
	// Reviver standing, reviver held, target down, target held, distance, max, facing, min facing.
	TestNull(TEXT("standing, free, close, facing: yes"), WhyCannotRevive(true, false, true, false, 100.0f, 200.0f, 0.9f, 0.5f));
	TestNotNull(TEXT("reviver down"), WhyCannotRevive(false, false, true, false, 100.0f, 200.0f, 0.9f, 0.5f));
	TestNotNull(TEXT("reviver pinned"), WhyCannotRevive(true, true, true, false, 100.0f, 200.0f, 0.9f, 0.5f));
	TestNotNull(TEXT("target not down"), WhyCannotRevive(true, false, false, false, 100.0f, 200.0f, 0.9f, 0.5f));
	TestNotNull(TEXT("target in a raptor's grip"), WhyCannotRevive(true, false, true, true, 100.0f, 200.0f, 0.9f, 0.5f));
	TestNotNull(TEXT("too far"), WhyCannotRevive(true, false, true, false, 250.0f, 200.0f, 0.9f, 0.5f));
	TestNotNull(TEXT("looking away"), WhyCannotRevive(true, false, true, false, 100.0f, 200.0f, 0.1f, 0.5f));

	TestTrue(TEXT("nobody standing, someone down: everyone bleeds out"), ShouldBleedOutEveryone(0, 2));
	TestFalse(TEXT("someone standing: they can still revive"), ShouldBleedOutEveryone(1, 2));
	TestFalse(TEXT("nobody down: nothing to do"), ShouldBleedOutEveryone(0, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoCharacterDownedTest, "DinoGame.Health.CharacterGoesDownFromABite", DinoTestFlags)
bool FDinoCharacterDownedTest::RunTest(const FString& Parameters)
{
	// The rules wired into a real character, through the real damage path a raptor uses.
	FDinoTestWorld World;
	ADinoCharacter* Player = World.Spawn<ADinoTestCharacter>();
	if (!TestNotNull(TEXT("character spawned"), Player))
	{
		return false;
	}

	TestTrue(TEXT("starts standing"), Player->IsStanding());

	UGameplayStatics::ApplyDamage(Player, 500.0f, nullptr, nullptr, UDinoDamageType_Bite::StaticClass());
	TestTrue(TEXT("a raptor's bite downs"), Player->IsDowned());
	TestTrue(TEXT("...still alive"), Player->IsAlive());
	TestEqual(TEXT("...full bleed-out"), Player->GetBleedOut(), BleedOutFull);

	UGameplayStatics::ApplyDamage(Player, 30.0f, nullptr, nullptr, UDinoDamageType_Bite::StaticClass());
	TestEqual(TEXT("further bites drain bleed-out"), Player->GetBleedOut(), 70.0f);

	Player->Revive(0.2f);
	TestTrue(TEXT("revived: standing"), Player->IsStanding());
	TestEqual(TEXT("...at 20% health"), Player->GetCurrentHealth(), Player->GetMaxHealth() * 0.2f);

	UGameplayStatics::ApplyDamage(Player, 500.0f, nullptr, nullptr, UDinoDamageType_Devour::StaticClass());
	TestFalse(TEXT("a T-Rex's bite kills outright"), Player->IsAlive());
	TestFalse(TEXT("...without going down"), Player->IsDowned());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoDownedLiesStillTest, "DinoGame.Health.DownedLiesStillWhenLetGo", DinoTestFlags)
bool FDinoDownedLiesStillTest::RunTest(const FString& Parameters)
{
	// The bug: a raptor downs the player it has pinned, is shot off (or lets go), and the end of
	// the pin stood the downed player up and gave them movement back - they could run around.
	FDinoTestWorld World;
	ADinoCharacter* Player = World.Spawn<ADinoTestCharacter>();
	if (!TestNotNull(TEXT("character spawned"), Player))
	{
		return false;
	}
	const UCharacterMovementComponent* Movement = Player->GetCharacterMovement();

	Player->BeginRestraint(nullptr, EDinoRestraint::Pinned);
	TestTrue(TEXT("pinned: lying down"), Player->IsLyingDown());

	UGameplayStatics::ApplyDamage(Player, 500.0f, nullptr, nullptr, UDinoDamageType_Bite::StaticClass());
	TestTrue(TEXT("bitten down while pinned"), Player->IsDowned());

	Player->EndRestraint();
	TestTrue(TEXT("let go while down: still down"), Player->IsDowned());
	TestTrue(TEXT("...still lying down"), Player->IsLyingDown());
	TestEqual(TEXT("...still unable to move"), Movement->MovementMode.GetValue(), MOVE_None);

	Player->Revive(0.2f);
	TestFalse(TEXT("revived: up off the ground"), Player->IsLyingDown());
	TestNotEqual(TEXT("...able to move"), Movement->MovementMode.GetValue(), MOVE_None);

	// Down without ever being pinned (DinoDown, or a bite with no pin): the same pose.
	ADinoCharacter* Other = World.Spawn<ADinoTestCharacter>();
	if (!TestNotNull(TEXT("second character spawned"), Other))
	{
		return false;
	}
	UGameplayStatics::ApplyDamage(Other, 500.0f, nullptr, nullptr, UDinoDamageType_Bite::StaticClass());
	TestTrue(TEXT("downed unpinned: lying down"), Other->IsLyingDown());
	TestEqual(TEXT("...unable to move"), Other->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	return true;
}

#endif
