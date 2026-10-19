#include "DinoDamageTypes.h"
#include "DinoPlayerState.h"
#include "DinoTestWorld.h"
#include "Misc/AutomationTest.h"
#include "Online/DinoSessionSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DinoTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

// --- Player status ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoPlayerStatusTest, "DinoGame.PlayerStatus.Transitions", DinoTestFlags)
bool FDinoPlayerStatusTest::RunTest(const FString& Parameters)
{
	FDinoTestWorld World;
	ADinoPlayerState* Player = World.Spawn<ADinoPlayerState>();
	if (!TestNotNull(TEXT("PlayerState spawned"), Player))
	{
		return false;
	}

	// Starting Alive would count lobby players and late joiners, and a round could never end.
	TestEqual(TEXT("starts in the lobby"), Player->GetStatus(), EDinoPlayerStatus::Lobby);
	TestFalse(TEXT("lobby is not alive"), Player->IsAlive());

	Player->SetStatus(EDinoPlayerStatus::Alive);
	TestTrue(TEXT("alive"), Player->IsAlive());

	Player->SetStatus(EDinoPlayerStatus::Downed);
	TestTrue(TEXT("downed still counts as alive"), Player->IsAlive());

	Player->SetStatus(EDinoPlayerStatus::Dead, UDinoDamageType_Bite::StaticClass());
	TestFalse(TEXT("dead is not alive"), Player->IsAlive());
	TestTrue(TEXT("dead keeps its cause"), Player->GetDeathCause() == UDinoDamageType_Bite::StaticClass());

	Player->SetStatus(EDinoPlayerStatus::Alive, UDinoDamageType_Bite::StaticClass());
	TestNull(TEXT("a cause is only kept while dead"), Player->GetDeathCause().Get());

	Player->SetStatus(EDinoPlayerStatus::Lobby);
	TestFalse(TEXT("back to the lobby is not alive"), Player->IsAlive());
	return true;
}

// --- Damage types ----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoDamageTypeTest, "DinoGame.Death.CausesAreNamed", DinoTestFlags)
bool FDinoDamageTypeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("bite"), UDinoDamageType::NameOf(UDinoDamageType_Bite::StaticClass()), FString(TEXT("Bite")));
	TestEqual(TEXT("devour"), UDinoDamageType::NameOf(UDinoDamageType_Devour::StaticClass()), FString(TEXT("Devour")));
	TestEqual(TEXT("fall"), UDinoDamageType::NameOf(UDinoDamageType_Fall::StaticClass()), FString(TEXT("Fall")));
	TestEqual(TEXT("no cause"), UDinoDamageType::NameOf(nullptr), FString(TEXT("Other")));

	// The death message ("<name> was eaten") reads these; each cause says something different.
	const FString Bite = UDinoDamageType::DescribeDeath(UDinoDamageType_Bite::StaticClass());
	const FString Devour = UDinoDamageType::DescribeDeath(UDinoDamageType_Devour::StaticClass());
	const FString Fall = UDinoDamageType::DescribeDeath(UDinoDamageType_Fall::StaticClass());
	TestFalse(TEXT("bite has a description"), Bite.IsEmpty());
	TestFalse(TEXT("unknown cause still has a description"), UDinoDamageType::DescribeDeath(nullptr).IsEmpty());
	TestTrue(TEXT("descriptions differ"), Bite != Devour && Devour != Fall && Bite != Fall);
	return true;
}

// --- Join codes ------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoJoinCodeTest, "DinoGame.Session.JoinCodeNormalising", DinoTestFlags)
bool FDinoJoinCodeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("lowercase, a dash and a trailing space"), UDinoSessionSubsystem::NormaliseJoinCode(TEXT("k7m-2pq ")), FString(TEXT("K7M2PQ")));
	TestEqual(TEXT("already clean"), UDinoSessionSubsystem::NormaliseJoinCode(TEXT("ABC234")), FString(TEXT("ABC234")));
	TestEqual(TEXT("spaces inside"), UDinoSessionSubsystem::NormaliseJoinCode(TEXT("abc def")), FString(TEXT("ABCDEF")));

	// Never issued, because they are easy to misread or mishear (0/O, 1/I/L).
	TestEqual(TEXT("ambiguous characters dropped"), UDinoSessionSubsystem::NormaliseJoinCode(TEXT("0O1IL")), FString());
	return true;
}

#endif
