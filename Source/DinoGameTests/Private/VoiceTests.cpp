#include "DinoGameMode.h"
#include "DinoPlayerState.h"
#include "DinoTestWorld.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerMuteList.h"
#include "Misc/AutomationTest.h"
#include "Net/VoiceConfig.h"
#include "Online/DinoVoiceRules.h"
#include "Online/DinoVoiceSubsystem.h"
#include "Online/DinoVoiceTalker.h"
#include "VoipListenerSynthComponent.h"
#include "VoiceEngineImpl.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DinoTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	EVoiceBlockReasons ReasonsFor(const APlayerController* Controller, const FUniqueNetIdRepl& Id)
	{
		const EVoiceBlockReasons* Reasons = Controller->MuteList.VoicePacketFilterMap.Find(Id.GetUniqueNetId().ToSharedRef());
		return Reasons ? *Reasons : EVoiceBlockReasons::None;
	}
}

// --- Rules -----------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoVoiceRulesMuteTest, "DinoGame.Voice.Rules.WhoHearsWhom", DinoTestFlags)
bool FDinoVoiceRulesMuteTest::RunTest(const FString& Parameters)
{
	// Written out case by case, rather than recomputed, so a change to the rule has to change
	// this table on purpose. Columns: round running, listener alive, speaker alive, listener's
	// spectator camera hears the living, expected mute.
	struct FCase { bool bRound, bListenerAlive, bSpeakerAlive, bHearsLiving, bMute; const TCHAR* Why; };
	const FCase Cases[] = {
		{ false, false, false, false, false, TEXT("lobby/round over: everyone hears everyone") },
		{ false, false, true,  false, false, TEXT("lobby/round over: everyone hears everyone") },
		{ false, true,  false, false, false, TEXT("lobby/round over: everyone hears everyone") },
		{ false, true,  true,  false, false, TEXT("lobby/round over: everyone hears everyone") },
		{ false, false, true,  true,  false, TEXT("lobby/round over: everyone hears everyone") },
		{ true,  true,  true,  false, false, TEXT("living hear the living") },
		{ true,  true,  true,  true,  false, TEXT("living hear the living") },
		{ true,  true,  false, false, true,  TEXT("living never hear the dead") },
		{ true,  true,  false, true,  true,  TEXT("living never hear the dead") },
		{ true,  false, false, false, false, TEXT("dead hear the dead") },
		{ true,  false, false, true,  false, TEXT("dead hear the dead") },
		{ true,  false, true,  true,  false, TEXT("follow/free spectator hears the living") },
		{ true,  false, true,  false, true,  TEXT("overhead spectator does not hear the living") },
	};

	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("round=%d listenerAlive=%d speakerAlive=%d hearsLiving=%d (%s)"),
			Case.bRound, Case.bListenerAlive, Case.bSpeakerAlive, Case.bHearsLiving, Case.Why),
			DinoVoiceRules::ShouldMute(Case.bRound, Case.bListenerAlive, Case.bSpeakerAlive, Case.bHearsLiving), Case.bMute);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoVoiceRulesPlacementTest, "DinoGame.Voice.Rules.Placement", DinoTestFlags)
bool FDinoVoiceRulesPlacementTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("alive with a living body: from the body"), DinoVoiceRules::ShouldBePositioned(true, true));
	TestFalse(TEXT("alive, body not here yet: unpositioned"), DinoVoiceRules::ShouldBePositioned(true, false));
	TestFalse(TEXT("dead, body still lying there: unpositioned"), DinoVoiceRules::ShouldBePositioned(false, true));
	TestFalse(TEXT("dead, no body: unpositioned"), DinoVoiceRules::ShouldBePositioned(false, false));
	return true;
}

// --- Talker registration ---------------------------------------------------------------------
// The engine's talker lookup is one map per process; every test removes what it added.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoVoiceTalkerLateIdTest, "DinoGame.Voice.Talker.RegistersWhenIdArrivesAfterBeginPlay", DinoTestFlags)
bool FDinoVoiceTalkerLateIdTest::RunTest(const FString& Parameters)
{
	// The server sets a joining player's ID at login, after their PlayerState began play. A
	// talker registered only from BeginPlay or OnRep_UniqueId stayed unregistered on the host.
	FDinoTestWorld World;
	ADinoPlayerState* Player = World.Spawn<ADinoPlayerState>();
	if (!TestNotNull(TEXT("PlayerState spawned"), Player))
	{
		return false;
	}

	TestFalse(TEXT("no ID yet, so not registered"), Player->GetVoiceTalker()->IsEngineTalker());

	const FUniqueNetIdRepl Id = FDinoTestWorld::MakeNetId(TEXT("Late"));
	Player->SetUniqueId(Id);
	TestTrue(TEXT("registered once the ID is set"), Player->GetVoiceTalker()->IsEngineTalker());

	UVOIPStatics::ResetPlayerVoiceTalker(Id);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoVoiceTalkerReplacedTest, "DinoGame.Voice.Talker.NewerTalkerSurvivesOldOneLeaving", DinoTestFlags)
bool FDinoVoiceTalkerReplacedTest::RunTest(const FString& Parameters)
{
	// The order of a seamless restart on the server (AGameMode::HandleSeamlessTravelPlayer): a
	// new PlayerState gets the player's ID, then the old one is destroyed. The old talker used to
	// delete the new one's registration, and every client was heard from anywhere.
	FDinoTestWorld World;
	const FUniqueNetIdRepl Id = FDinoTestWorld::MakeNetId(TEXT("Restart"));

	ADinoPlayerState* OldPlayer = World.Spawn<ADinoPlayerState>();
	OldPlayer->SetUniqueId(Id);
	TestTrue(TEXT("old talker registered"), OldPlayer->GetVoiceTalker()->IsEngineTalker());

	ADinoPlayerState* NewPlayer = World.Spawn<ADinoPlayerState>();
	NewPlayer->SetUniqueId(Id);
	UDinoVoiceTalker* NewTalker = NewPlayer->GetVoiceTalker();
	TestTrue(TEXT("new talker took over the ID"), NewTalker->IsEngineTalker());

	OldPlayer->Destroy();
	TestTrue(TEXT("new talker still registered after the old PlayerState is destroyed"), NewTalker->IsEngineTalker());
	TestTrue(TEXT("engine lookup finds the new talker"), UVOIPStatics::GetVOIPTalkerForPlayer(Id) == NewTalker);

	UVOIPStatics::ResetPlayerVoiceTalker(Id);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoVoiceTalkerLeavesTest, "DinoGame.Voice.Talker.UnregistersWhenItLeaves", DinoTestFlags)
bool FDinoVoiceTalkerLeavesTest::RunTest(const FString& Parameters)
{
	// The other half: keeping a newer talker must not leave a destroyed one in the lookup, where
	// the engine would read a dangling pointer for that player's next word.
	FDinoTestWorld World;
	const FUniqueNetIdRepl Id = FDinoTestWorld::MakeNetId(TEXT("Leave"));

	ADinoPlayerState* Player = World.Spawn<ADinoPlayerState>();
	Player->SetUniqueId(Id);
	Player->Destroy();

	TestNull(TEXT("engine lookup is empty once the only talker is gone"), UVOIPStatics::GetVOIPTalkerForPlayer(Id));

	UVOIPStatics::ResetPlayerVoiceTalker(Id);
	return true;
}

// --- What actually plays ---------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoVoicePlaybackCheckTest, "DinoGame.Voice.Talker.PlaybackCheckReadsTheRealSound", DinoTestFlags)
bool FDinoVoicePlaybackCheckTest::RunTest(const FString& Parameters)
{
	// The engine's voice sound, built the way the engine builds it (CreateVoiceSynthComponent,
	// then ApplyVoiceSettings' CreateAudioComponent): the synth attaches its inner sound to itself.
	FDinoTestWorld World;
	auto MakeVoiceSound = [&World]()
	{
		UVoipListenerSynthComponent* Synth = NewObject<UVoipListenerSynthComponent>();
		Synth->RegisterComponentWithWorld(World.Get());
		Synth->CreateAudioComponent();
		return Synth;
	};

	// Unpositioned settings, unpositioned sound: the lobby and the dead. This read as a mismatch
	// once, because the sound hangs off its own synth rather than nothing.
	UDinoVoiceTalker* Unpositioned = NewObject<UDinoVoiceTalker>();
	UVoipListenerSynthComponent* LobbyVoice = MakeVoiceSound();
	if (!TestNotNull(TEXT("the engine's voice sound exists"), LobbyVoice->GetAudioComponent()))
	{
		return false;
	}
	Unpositioned->OnTalkingBegin(LobbyVoice->GetAudioComponent());
	FString Detail;
	TestTrue(TEXT("an unpositioned voice on its own synth matches unpositioned settings"), Unpositioned->CheckPlayback(Detail));

	// Positioned settings, but a sound the engine never attached: the "heard from anywhere" bug.
	AActor* Body = World.Spawn<AActor>();
	USceneComponent* BodyRoot = NewObject<USceneComponent>(Body);
	Body->SetRootComponent(BodyRoot);
	BodyRoot->RegisterComponent();

	UDinoVoiceTalker* Positioned = NewObject<UDinoVoiceTalker>();
	Positioned->SetPlacement(BodyRoot, nullptr);
	UVoipListenerSynthComponent* StrayVoice = MakeVoiceSound();
	Positioned->OnTalkingBegin(StrayVoice->GetAudioComponent());
	TestFalse(TEXT("a body-placed voice playing unattached is caught"), Positioned->CheckPlayback(Detail));

	// And once the engine does attach it, it matches.
	StrayVoice->GetAudioComponent()->AttachToComponent(BodyRoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	TestTrue(TEXT("a body-placed voice attached to the body matches"), Positioned->CheckPlayback(Detail));

	LobbyVoice->DestroyComponent();
	StrayVoice->DestroyComponent();
	return true;
}

// --- Gameplay mute on the host ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoHostMuteQuirkTest, "DinoGame.Voice.HostMute.EngineQuirkStillPresent", DinoTestFlags)
bool FDinoHostMuteQuirkTest::RunTest(const FString& Parameters)
{
	// Documents why ADinoGameMode::UnmuteForGameplay exists. A controller whose server and client
	// halves share one mute list (the listen-server host) keeps a "Muted" reason after a plain
	// gameplay unmute. If this starts failing, the engine has fixed it and the workaround can go.
	FDinoTestWorld World;
	APlayerController* Host = World.Spawn<APlayerController>();
	const FUniqueNetIdRepl Speaker = FDinoTestWorld::MakeNetId(TEXT("Quirk"));

	Host->GameplayMutePlayer(Speaker);
	Host->GameplayUnmutePlayer(Speaker);

	TestTrue(TEXT("engine leaves the host muted after a plain gameplay unmute"),
		EnumHasAnyFlags(ReasonsFor(Host, Speaker), EVoiceBlockReasons::Muted));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoHostMuteWorkaroundTest, "DinoGame.Voice.HostMute.UnmuteLiftsFully", DinoTestFlags)
bool FDinoHostMuteWorkaroundTest::RunTest(const FString& Parameters)
{
	FDinoTestWorld World;
	APlayerController* Host = World.Spawn<APlayerController>();
	const FUniqueNetIdRepl Speaker = FDinoTestWorld::MakeNetId(TEXT("Lift"));

	Host->GameplayMutePlayer(Speaker);
	TestTrue(TEXT("muted while the rules say so"), ReasonsFor(Host, Speaker) != EVoiceBlockReasons::None);

	ADinoGameMode::UnmuteForGameplay(Host, Speaker, /*bListenerIsHost*/ true, /*bListenerMutedThem*/ false);
	TestTrue(TEXT("no reason left: the host hears them again"), ReasonsFor(Host, Speaker) == EVoiceBlockReasons::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoHostMuteKeepsTabMuteTest, "DinoGame.Voice.HostMute.KeepsTheHostsOwnMute", DinoTestFlags)
bool FDinoHostMuteKeepsTabMuteTest::RunTest(const FString& Parameters)
{
	FDinoTestWorld World;
	APlayerController* Host = World.Spawn<APlayerController>();
	const FUniqueNetIdRepl Speaker = FDinoTestWorld::MakeNetId(TEXT("TabMute"));

	Host->ServerMutePlayer(Speaker);      // the host's own Tab-menu mute
	Host->GameplayMutePlayer(Speaker);    // then they die, say
	ADinoGameMode::UnmuteForGameplay(Host, Speaker, true, /*bListenerMutedThem*/ true);

	TestFalse(TEXT("gameplay reason removed"), EnumHasAnyFlags(ReasonsFor(Host, Speaker), EVoiceBlockReasons::Gameplay));
	TestTrue(TEXT("the host's own mute stays"), EnumHasAnyFlags(ReasonsFor(Host, Speaker), EVoiceBlockReasons::Muted));
	return true;
}

// --- Am I talking? ---------------------------------------------------------------------------

namespace
{
	/** The engine's voice engine, reporting that player 0 has voice data ready - bit 0. */
	class FPlayerZeroHasVoice : public FVoiceEngineImpl
	{
	public:
		FPlayerZeroHasVoice() : FVoiceEngineImpl(nullptr) {}
		virtual uint32 GetVoiceDataReadyFlags() const override { return 1u << 0; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoTalkingQuirkTest, "DinoGame.Voice.Talking.EngineQuirkStillPresent", DinoTestFlags)
bool FDinoTalkingQuirkTest::RunTest(const FString& Parameters)
{
	// Documents why UDinoVoiceSubsystem::IsLocallyTalking exists. FVoiceEngineImpl::IsLocalPlayerTalking
	// tests (LocalUserNum << 1) - for player 0, no bit at all - so it says false even with voice
	// ready. Asking it is why creatures never heard a real voice. If this starts failing, the
	// engine has fixed it.
	FPlayerZeroHasVoice Engine;
	TestFalse(TEXT("engine says player 0 is not talking even with their voice data ready"), Engine.IsLocalPlayerTalking(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoTalkingStateTest, "DinoGame.Voice.Talking.FollowsOnlyThisPlayer", DinoTestFlags)
bool FDinoTalkingStateTest::RunTest(const FString& Parameters)
{
	const FUniqueNetIdRepl Me = FDinoTestWorld::MakeNetId(TEXT("Me"));
	const FUniqueNetIdRepl Them = FDinoTestWorld::MakeNetId(TEXT("Them"));
	const FUniqueNetId* Local = Me.GetUniqueNetId().Get();

	bool bTalking = false;
	bTalking = UDinoVoiceSubsystem::NextLocallyTalking(bTalking, *Them, true, Local);
	TestFalse(TEXT("someone else talking is not me talking"), bTalking);

	bTalking = UDinoVoiceSubsystem::NextLocallyTalking(bTalking, *Me, true, Local);
	TestTrue(TEXT("I started talking"), bTalking);

	bTalking = UDinoVoiceSubsystem::NextLocallyTalking(bTalking, *Them, false, Local);
	TestTrue(TEXT("someone else stopping does not stop me"), bTalking);

	bTalking = UDinoVoiceSubsystem::NextLocallyTalking(bTalking, *Me, false, Local);
	TestFalse(TEXT("I stopped talking"), bTalking);

	TestFalse(TEXT("no local identity yet: nothing counts"), UDinoVoiceSubsystem::NextLocallyTalking(false, *Me, true, nullptr));
	return true;
}

// --- Mic sensitivity -------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDinoMicSensitivityTest, "DinoGame.Voice.MicSensitivity", DinoTestFlags)
bool FDinoMicSensitivityTest::RunTest(const FString& Parameters)
{
	const float AtZero = UDinoVoiceSubsystem::SensitivityToThreshold(0.0f);
	const float AtOne = UDinoVoiceSubsystem::SensitivityToThreshold(1.0f);
	TestTrue(TEXT("more sensitive means a lower threshold"), AtOne < AtZero);
	TestTrue(TEXT("full sensitivity still needs some signal"), AtOne > 0.0f);
	TestEqual(TEXT("clamped below 0"), UDinoVoiceSubsystem::SensitivityToThreshold(-1.0f), AtZero);
	TestEqual(TEXT("clamped above 1"), UDinoVoiceSubsystem::SensitivityToThreshold(2.0f), AtOne);

	float Previous = AtZero + 1.0f;
	for (float Sensitivity = 0.0f; Sensitivity <= 1.0f; Sensitivity += 0.1f)
	{
		const float Threshold = UDinoVoiceSubsystem::SensitivityToThreshold(Sensitivity);
		TestTrue(FString::Printf(TEXT("threshold falls steadily at %.1f"), Sensitivity), Threshold < Previous);
		TestEqual(FString::Printf(TEXT("slider round-trips at %.1f"), Sensitivity),
			UDinoVoiceSubsystem::ThresholdToSensitivity(Threshold), Sensitivity, 0.001f);
		Previous = Threshold;
	}
	return true;
}

#endif
