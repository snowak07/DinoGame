#include "AI/DinoAIControllerBase.h"
#include "AI/DinoNavAreas.h"
#include "DinoTestCharacter.h"
#include "AI/DinoTargetRules.h"
#include "Components/StaticMeshComponent.h"
#include "DinoTestWorld.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "World/DinoBush.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DinoTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** A bush as BP_Bush makes one: the engine sphere as leaves, at Scale x its 50 cm radius, centred on Centre. */
	ADinoBush* SpawnBush(FDinoTestWorld& World, const FVector& Centre, float Scale)
	{
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		ADinoBush* Bush = World.Spawn<ADinoBush>();
		if (!Bush || !Sphere)
		{
			return nullptr;
		}

		// Placed on the component itself: an actor spawned with no root has no transform of its
		// own to give it.
		UStaticMeshComponent* Leaves = NewObject<UStaticMeshComponent>(Bush, TEXT("Leaves"));
		Leaves->SetStaticMesh(Sphere);
		Leaves->SetWorldLocationAndRotation(Centre, FQuat::Identity);
		Leaves->SetWorldScale3D(FVector(Scale));
		Bush->SetRootComponent(Leaves);
		Leaves->RegisterComponent();

		// The bare test world never begins play (it has no game mode to start it), so BeginPlay -
		// where the bush sets its leaves up - is run by hand, after the leaves exist, as in BP_Bush.
		Bush->DispatchBeginPlay();
		return Bush;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoBushFindRulesTest, "DinoGame.Bush.Rules.WhoFindsAHiddenPlayer", DinoTestFlags)
bool FDinoBushFindRulesTest::RunTest(const FString& Parameters)
{
	using namespace DinoTargetRules;

	TestTrue(TEXT("bodies 10 cm apart, within 15: touching"), IsTouching(10.0f, 15.0f));
	TestFalse(TEXT("bodies 40 cm apart: not touching"), IsTouching(40.0f, 15.0f));
	TestTrue(TEXT("overlapping bodies (negative gap): touching"), IsTouching(-5.0f, 15.0f));

	// Touching, same bush, checked their bush.
	TestFalse(TEXT("none of the three: still hidden"), FindsHiddenPlayer(false, false, false));
	TestTrue(TEXT("bumped into them: found"), FindsHiddenPlayer(true, false, false));
	TestTrue(TEXT("walked into their bush: found"), FindsHiddenPlayer(false, true, false));
	TestTrue(TEXT("checked their bush: always found, no chance to stay hidden"), FindsHiddenPlayer(false, false, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoBushCheckRulesTest, "DinoGame.Bush.Rules.WhenASearchChecksABush", DinoTestFlags)
bool FDinoBushCheckRulesTest::RunTest(const FString& Parameters)
{
	using namespace DinoTargetRules;

	// Destination in an unchecked bush, investigation pending, unchecked bush in range, last leg checked one.
	TestTrue(TEXT("search spot inside a bush: checked from its edge, even a freshly investigated one"), ShouldCheckBushNext(true, true, false, true));
	TestTrue(TEXT("a bush in the search area after an open-ground leg: checked"), ShouldCheckBushNext(false, false, true, false));
	TestFalse(TEXT("...but not twice in a row: open ground in between"), ShouldCheckBushNext(false, false, true, true));
	TestFalse(TEXT("never in place of a freshly investigated spot"), ShouldCheckBushNext(false, true, true, false));
	TestFalse(TEXT("no unchecked bush nearby: an ordinary leg"), ShouldCheckBushNext(false, false, false, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoBushNoiseDuringCheckTest, "DinoGame.Bush.Rules.TalkingDuringACheck", DinoTestFlags)
bool FDinoBushNoiseDuringCheckTest::RunTest(const FString& Parameters)
{
	using namespace DinoTargetRules;

	// The bug: a raptor began checking a bush with the player crouched inside; the player's own
	// voice, from that bush, redirected its search, which cancelled the check - and the bush had
	// already been counted as checked, so every later voice from it was walked past.

	// Busy with a bush, forced, noise from that bush.
	TestTrue(TEXT("their voice from the bush being checked: the check carries on"), NoiseKeepsBushCheck(true, false, true));
	TestFalse(TEXT("a noise from somewhere else: redirected"), NoiseKeepsBushCheck(true, false, false));
	TestFalse(TEXT("a flare hit: always redirected"), NoiseKeepsBushCheck(true, true, true));
	TestFalse(TEXT("not busy with a bush: an ordinary redirect"), NoiseKeepsBushCheck(false, false, true));

	// Finished, could not reach.
	TestFalse(TEXT("a check cut short saw nothing: not counted as checked"), CountsAsChecked(false, false));
	TestTrue(TEXT("a check run to the end: counted"), CountsAsChecked(true, false));
	TestTrue(TEXT("a bush it could not get to: counted, so it stops trying"), CountsAsChecked(false, true));

	// Checked already, found the noise-maker there.
	TestTrue(TEXT("a voice from a bush checked empty earlier: checked again"), NoiseReopensBush(true, false));
	TestFalse(TEXT("...not when it already found them there"), NoiseReopensBush(true, true));
	TestFalse(TEXT("an unchecked bush has nothing to reopen"), NoiseReopensBush(false, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoBushShapeTest, "DinoGame.Bush.ShapeAndSetup", DinoTestFlags)
bool FDinoBushShapeTest::RunTest(const FString& Parameters)
{
	FDinoTestWorld World;
	const FVector Centre(0.0f, 0.0f, 100.0f);
	ADinoBush* Bush = SpawnBush(World, Centre, 2.0f);
	if (!TestNotNull(TEXT("bush spawned with leaves"), Bush))
	{
		return false;
	}

	// The collision preset is the project's (Config/DefaultEngine.ini), so this also pins that.
	TestEqual(TEXT("set up right: hides, lets pawns through, cuts no navmesh"), Bush->DescribeSetupProblem(), FString());
	TestEqual(TEXT("1 m radius"), Bush->GetRadius(), 100.0f, 2.0f);

	TestTrue(TEXT("its middle is inside"), Bush->Contains(Centre));
	TestTrue(TEXT("a point 60 cm in is inside"), Bush->Contains(Centre + FVector(60.0f, 0.0f, 0.0f)));
	TestFalse(TEXT("a point 1.5 m out is not"), Bush->Contains(Centre + FVector(150.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("the ground under it is covered"), Bush->CoversFlat(FVector(50.0f, 50.0f, 0.0f)));

	TestTrue(TEXT("found by a point in it"), ADinoBush::FindContaining(World.Get(), Centre) == Bush);
	TestNull(TEXT("nothing found in the open"), ADinoBush::FindContaining(World.Get(), Centre + FVector(500.0f, 0.0f, 0.0f)));

	// Checked from just outside, on the side the creature comes from - not from inside it.
	const FVector Spot = Bush->GetCheckSpot(FVector(1000.0f, 0.0f, 0.0f), 60.0f);
	TestTrue(TEXT("check spot on the creature's side"), Spot.X > 0.0f);
	TestTrue(TEXT("check spot clear of the leaves, body and all"), FVector::Dist2D(Spot, Centre) >= Bush->GetRadius() + 60.0f);
	TestFalse(TEXT("standing at the check spot is not being in the bush"), Bush->Contains(FVector(Spot.X, Spot.Y, Centre.Z)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoBushFoundLineTest, "DinoGame.Bush.FoundPlayerCanBeReached", DinoTestFlags)
bool FDinoBushFoundLineTest::RunTest(const FString& Parameters)
{
	// The bug: a raptor found a player in a bush and hunted them - sight ignored the bush - but
	// its attack's line check still hit the leaves, so it stood beside them and never pounced.
	FDinoTestWorld World;
	const FVector Centre(0.0f, 0.0f, 100.0f);
	ADinoBush* Bush = SpawnBush(World, Centre, 2.0f);
	ADinoCharacter* Player = World.Spawn<ADinoTestCharacter>();
	ADinoAIControllerBase* Raptor = World.Spawn<ADinoAIControllerBase>();
	if (!TestNotNull(TEXT("bush"), Bush) || !TestNotNull(TEXT("player"), Player) || !TestNotNull(TEXT("creature brain"), Raptor))
	{
		return false;
	}
	Player->SetActorLocation(Centre);
	TestTrue(TEXT("player is hiding in the bush"), Bush->Contains(Player->GetActorLocation()));

	const FVector Outside = Centre + FVector(400.0f, 0.0f, 0.0f);
	TestFalse(TEXT("hidden: the leaves block the line to them"), Raptor->HasClearLineTo(Outside, Player));

	Raptor->RevealPlayer(Player, Bush, TEXT("a test"));
	TestTrue(TEXT("found: the line passes through that bush, so it can attack"), Raptor->HasClearLineTo(Outside, Player));

	// Only the bush it found them in: another one in the way still blocks.
	ADinoBush* Other = SpawnBush(World, Centre + FVector(250.0f, 0.0f, 0.0f), 1.0f);
	TestFalse(TEXT("another bush in between still hides them"), Other && Raptor->HasClearLineTo(Outside, Player));

	Player->SetActorLocation(Centre + FVector(-800.0f, 0.0f, 0.0f));
	TestNull(TEXT("out of the bush: no longer revealed by it"), Raptor->GetBushRevealedIn(Player));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoBushPathingTest, "DinoGame.Bush.CreaturesGoAroundButChaseThrough", DinoTestFlags)
bool FDinoBushPathingTest::RunTest(const FString& Parameters)
{
	const UDinoNavArea_Foliage* Foliage = GetDefault<UDinoNavArea_Foliage>();
	TestTrue(TEXT("ground under a bush costs more than open ground"), Foliage->DefaultCost > 1.0f);

	const UDinoNavFilter_Chase* Chase = GetDefault<UDinoNavFilter_Chase>();
	const FNavigationFilterArea* Override = Chase->Areas.FindByPredicate([](const FNavigationFilterArea& Area)
	{
		return Area.AreaClass == UDinoNavArea_Foliage::StaticClass();
	});
	TestTrue(TEXT("a chase costs bushes like open ground"), Override && Override->bOverrideTravelCost && Override->TravelCostOverride == 1.0f);
	TestFalse(TEXT("...and does not exclude them"), Override && Override->bIsExcluded);
	return true;
}

#endif
