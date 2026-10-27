#include "DinoSelfCheck.h"

#include "AI/DinoAIControllerBase.h"
#include "AI/DinoAttackComponent.h"
#include "AI/DinoCreature.h"
#include "Components/AudioComponent.h"
#include "DinoCharacter.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "VoipListenerSynthComponent.h"
#include "DinoGame.h"
#include "DinoGameState.h"
#include "DinoPlayerController.h"
#include "DinoPlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerMuteList.h"
#include "Online/DinoVoiceRules.h"
#include "Online/DinoVoiceSubsystem.h"
#include "Online/DinoVoiceTalker.h"

namespace
{
	using FResult = DinoSelfCheck::FResult;
	using EOutcome = DinoSelfCheck::FResult::EOutcome;

	void Add(TArray<FResult>& Out, bool bPassed, const FString& Name, const FString& Detail)
	{
		Out.Add({ bPassed ? EOutcome::Pass : EOutcome::Fail, Name, Detail });
	}

	void Skip(TArray<FResult>& Out, const FString& Name, const FString& Detail)
	{
		Out.Add({ EOutcome::Skip, Name, Detail });
	}

	const TCHAR* StatusName(const ADinoPlayerState* Player)
	{
		switch (Player->GetStatus())
		{
		case EDinoPlayerStatus::Lobby:  return TEXT("Lobby");
		case EDinoPlayerStatus::Alive:  return TEXT("Alive");
		case EDinoPlayerStatus::Downed: return TEXT("Downed");
		case EDinoPlayerStatus::Dead:   return TEXT("Dead");
		default:                        return TEXT("?");
		}
	}

	/** The engine's block reasons for a speaker in a controller's mute list, as text. */
	FString DescribeReasons(const APlayerController* Listener, const FUniqueNetIdRepl& SpeakerId, EVoiceBlockReasons& OutReasons)
	{
		OutReasons = EVoiceBlockReasons::None;
		const FUniqueNetIdPtr Id = SpeakerId.GetUniqueNetId();
		if (const EVoiceBlockReasons* Reasons = Id.IsValid() ? Listener->MuteList.VoicePacketFilterMap.Find(Id.ToSharedRef()) : nullptr)
		{
			OutReasons = *Reasons;
		}

		TArray<FString> Names;
		if (EnumHasAnyFlags(OutReasons, EVoiceBlockReasons::Muted))    { Names.Add(TEXT("Muted")); }
		if (EnumHasAnyFlags(OutReasons, EVoiceBlockReasons::Gameplay)) { Names.Add(TEXT("Gameplay")); }
		if (EnumHasAnyFlags(OutReasons, EVoiceBlockReasons::Blocked))  { Names.Add(TEXT("Blocked")); }
		return Names.IsEmpty() ? TEXT("none") : FString::Join(Names, TEXT("|"));
	}

	bool HasLivingBody(const ADinoPlayerState* Player)
	{
		const ADinoCharacter* Body = Player->GetVoiceBody();
		return IsValid(Body) && Body->IsAlive();
	}

	/** Whether a controller hears the living while spectating: the server's copy on the host, the sent copy on a client. */
	bool HearsLiving(const APlayerController* Controller)
	{
		const ADinoPlayerController* DinoController = Cast<ADinoPlayerController>(Controller);
		if (!DinoController)
		{
			return true;
		}
		return Controller->HasAuthority() ? DinoController->HearsLivingWhileSpectating() : DinoController->LocalHearsLivingWhileSpectating();
	}

	void CheckVoiceOfOthers(TArray<FResult>& Out, const ADinoGameState& GameState, const APlayerController& Local, bool bPIE)
	{
		for (const APlayerState* State : GameState.PlayerArray)
		{
			const ADinoPlayerState* Speaker = Cast<ADinoPlayerState>(State);
			if (!Speaker || Speaker == Local.PlayerState)
			{
				continue;
			}

			const FString Who = Speaker->GetPlayerName();
			const UDinoVoiceTalker* Talker = Speaker->GetVoiceTalker();
			if (!Speaker->GetUniqueId().IsValid() || !Talker)
			{
				Skip(Out, FString::Printf(TEXT("Voice of %s"), *Who), TEXT("no net ID yet"));
				continue;
			}

			// The engine's talker lookup is one map for the whole process, so PIE windows overwrite
			// each other's entries and this would fail for reasons that never happen packaged.
			if (bPIE)
			{
				Skip(Out, FString::Printf(TEXT("Voice registered: %s"), *Who), TEXT("not meaningful in PIE: the talker lookup is shared by every PIE window"));
			}
			else
			{
				Add(Out, Talker->IsEngineTalker(), FString::Printf(TEXT("Voice registered: %s"), *Who),
					Talker->IsEngineTalker() ? TEXT("engine plays their voice through our talker")
						: TEXT("engine has no talker for them - their voice plays at full volume from anywhere"));
			}

			const bool bExpected = DinoVoiceRules::ShouldBePositioned(Speaker->IsAlive(), HasLivingBody(Speaker));
			const bool bActual = Talker->IsPositioned();
			Add(Out, bExpected == bActual, FString::Printf(TEXT("Voice placement: %s"), *Who),
				FString::Printf(TEXT("status %s, living body %s; expected %s, talker %s"), StatusName(Speaker),
					HasLivingBody(Speaker) ? TEXT("yes") : TEXT("no"),
					bExpected ? TEXT("from body") : TEXT("unpositioned"),
					bActual ? TEXT("from body") : TEXT("unpositioned")));
		}
	}

	/**
	 * The sound actually playing for each voice, against its talker's settings - and any voice
	 * the engine is playing with no talker of ours behind it, which plays at full volume from
	 * anywhere whatever our settings say. Only voices playing at the moment can be checked.
	 */
	void CheckVoicePlayback(TArray<FResult>& Out, const UWorld& World, const ADinoGameState& GameState, const APlayerController& Local)
	{
		TSet<const UAudioComponent*> OurSounds;
		for (const APlayerState* State : GameState.PlayerArray)
		{
			const ADinoPlayerState* Speaker = Cast<ADinoPlayerState>(State);
			const UDinoVoiceTalker* Talker = Speaker && Speaker != Local.PlayerState ? Speaker->GetVoiceTalker() : nullptr;
			if (!Talker || !Talker->GetCurrentAudio())
			{
				continue;
			}

			OurSounds.Add(Talker->GetCurrentAudio());
			FString Detail;
			Add(Out, Talker->CheckPlayback(Detail), FString::Printf(TEXT("Voice playing: %s"), *Speaker->GetPlayerName()), Detail);
		}

		for (TObjectIterator<UVoipListenerSynthComponent> It; It; ++It)
		{
			UVoipListenerSynthComponent* Voice = *It;
			const UAudioComponent* Sound = IsValid(Voice) && Voice->GetWorld() == &World ? Voice->GetAudioComponent() : nullptr;
			if (Sound && Sound->IsPlaying() && !OurSounds.Contains(Sound))
			{
				Add(Out, false, TEXT("Voice playing without a talker"),
					FString::Printf(TEXT("%s is playing a voice the engine found no talker for: full volume, from anywhere"), *Voice->GetName()));
			}
		}
	}

	/**
	 * Host only. Every creature's target passes the AI's own target rule: a living player
	 * character not held by another creature. Mid-attack targets are judged when the attack
	 * ends (the AI does the same), so they count as fine here.
	 */
	void CheckCreatureTargets(TArray<FResult>& Out, UWorld& World)
	{
		for (TActorIterator<ADinoCreature> It(&World); It; ++It)
		{
			const ADinoCreature* Creature = *It;
			const ADinoAIControllerBase* Brain = Cast<ADinoAIControllerBase>(Creature->GetController());
			const AActor* Target = Brain ? Brain->GetCurrentTarget() : nullptr;
			if (!Target)
			{
				continue;
			}

			const FString Name = FString::Printf(TEXT("Creature target: %s"), *Creature->GetName());
			if (Brain->IsInVisionTest())
			{
				Skip(Out, Name, TEXT("in vision test mode"));
				continue;
			}

			const ADinoCharacter* Player = Cast<ADinoCharacter>(Target);
			const APlayerState* TargetState = Player ? Player->GetPlayerState() : nullptr;
			const FString TargetText = FString::Printf(TEXT("%s (%s%s%s)"), *Target->GetName(),
				Player ? (Player->IsAlive() ? TEXT("alive") : TEXT("DEAD")) : TEXT("NOT a player character"),
				TargetState ? *FString::Printf(TEXT(", %s"), *TargetState->GetPlayerName()) : TEXT(""),
				Player && Player->IsRestrained() ? *FString::Printf(TEXT(", held by %s"),
					Player->GetRestrainingCreature() ? *Player->GetRestrainingCreature()->GetName() : TEXT("?")) : TEXT(""));

			if (Brain->IsEngagedInAttack())
			{
				Add(Out, true, Name, FString::Printf(TEXT("mid-attack on %s; judged when the attack ends"), *TargetText));
				continue;
			}

			Add(Out, Brain->IsAcceptableTarget(Target), Name, FString::Printf(TEXT("targeting %s"), *TargetText));

			// Closest visible wins, once the commitment after its last switch has run out (with
			// half a second for the combat tick to act). Only reported when someone else is in
			// sight - otherwise there was no choice to get wrong.
			if (Creature->GetAwareness() == EDinoAwareness::Hunting
				&& Brain->GetSecondsSinceTargetChange() > Brain->GetTargetSwitchCooldown() + 0.5)
			{
				float NearestDistance = 0.0f;
				const AActor* Nearest = Brain->FindNearestVisibleTarget(Target, &NearestDistance);
				if (Nearest)
				{
					const bool bTargetInSight = Brain->IsInSight(Target);
					const float TargetDistance = FVector::Dist(Creature->GetActorLocation(), Target->GetActorLocation());
					const bool bChoseClosest = bTargetInSight && TargetDistance <= NearestDistance;
					Add(Out, bChoseClosest, FString::Printf(TEXT("Creature hunts the closest: %s"), *Creature->GetName()),
						FString::Printf(TEXT("hunting %s (%s), while %s is in sight at %.1f m; %.1f s since its last switch"),
							*Target->GetName(),
							bTargetInSight ? *FString::Printf(TEXT("%.1f m"), TargetDistance / 100.0f) : TEXT("out of sight"),
							*Nearest->GetName(), NearestDistance / 100.0f, Brain->GetSecondsSinceTargetChange()));
				}
			}
		}
	}

	/**
	 * Host only. Both halves of every pin or devour agree: a held player names the creature, the
	 * creature's attack holds that player, and the player is alive (a death ends the hold).
	 */
	void CheckRestraints(TArray<FResult>& Out, UWorld& World)
	{
		for (TActorIterator<ADinoCharacter> It(&World); It; ++It)
		{
			const ADinoCharacter* Player = *It;
			if (!Player->IsRestrained())
			{
				continue;
			}

			const ADinoCreature* Captor = Player->GetRestrainingCreature();
			const UDinoAttackComponent* Attack = Captor ? Captor->GetAttackComponent() : nullptr;
			const bool bCaptorHolds = Attack && Attack->GetHeldVictim() == Player;
			const APlayerState* State = Player->GetPlayerState();
			const FString Who = State ? State->GetPlayerName() : Player->GetName();

			Add(Out, Captor && bCaptorHolds && Player->IsAlive(), FString::Printf(TEXT("Restraint: %s"), *Who),
				FString::Printf(TEXT("held by %s, which %s; player %s"),
					Captor ? *Captor->GetName() : TEXT("NOBODY"),
					!Captor ? TEXT("-") : (bCaptorHolds ? TEXT("holds them") : TEXT("does NOT hold them")),
					Player->IsAlive() ? TEXT("alive") : TEXT("DEAD but still held")));
		}

		for (TActorIterator<ADinoCreature> It(&World); It; ++It)
		{
			const ADinoCreature* Creature = *It;
			const UDinoAttackComponent* Attack = Creature->GetAttackComponent();
			const ADinoCharacter* Victim = Attack ? Attack->GetHeldVictim() : nullptr;
			if (!Victim)
			{
				continue;
			}

			Add(Out, Victim->IsRestrained() && Victim->GetRestrainingCreature() == Creature,
				FString::Printf(TEXT("Hold: %s"), *Creature->GetName()),
				FString::Printf(TEXT("holds %s, who is %s"), *Victim->GetName(),
					!Victim->IsRestrained() ? TEXT("NOT restrained")
						: (Victim->GetRestrainingCreature() == Creature ? TEXT("held by it") : TEXT("held by ANOTHER creature"))));
		}
	}

	/** What this machine's own player receives, from its controller's mute list. */
	void CheckWhatIHear(TArray<FResult>& Out, const ADinoGameState& GameState, const APlayerController& Local, const UDinoVoiceSubsystem* VoiceSettings)
	{
		const ADinoPlayerState* Me = Local.GetPlayerState<ADinoPlayerState>();
		if (!Me)
		{
			return;
		}

		const bool bRound = GameState.IsRoundInProgress();
		for (const APlayerState* State : GameState.PlayerArray)
		{
			const ADinoPlayerState* Speaker = Cast<ADinoPlayerState>(State);
			if (!Speaker || Speaker == Me || !Speaker->GetUniqueId().IsValid())
			{
				continue;
			}

			const bool bGameplayMute = DinoVoiceRules::ShouldMute(bRound, Me->IsAlive(), Speaker->IsAlive(), HearsLiving(&Local));
			const bool bUserMute = VoiceSettings && VoiceSettings->IsPlayerMuted(Speaker->GetUniqueId());

			// Both kinds of mute reach this side as the "Muted" reason (ClientMutePlayer), and the
			// host also holds its own "Gameplay" reasons. Either one means silence here.
			EVoiceBlockReasons Reasons;
			const FString ReasonText = DescribeReasons(&Local, Speaker->GetUniqueId(), Reasons);
			const bool bActuallyMuted = EnumHasAnyFlags(Reasons, EVoiceBlockReasons::Muted | EVoiceBlockReasons::Gameplay);
			const bool bExpectMuted = bGameplayMute || bUserMute;

			Add(Out, bExpectMuted == bActuallyMuted, FString::Printf(TEXT("I hear %s"), *Speaker->GetPlayerName()),
				FString::Printf(TEXT("me %s, them %s, hears living %s, my Tab mute %s; expected %s, engine reasons %s"),
					StatusName(Me), StatusName(Speaker), HearsLiving(&Local) ? TEXT("yes") : TEXT("no"),
					bUserMute ? TEXT("yes") : TEXT("no"), bExpectMuted ? TEXT("silent") : TEXT("heard"), *ReasonText));
		}
	}

	/** Host only: what the server sends every remote player. */
	void CheckServerSends(TArray<FResult>& Out, const UWorld& World, const ADinoGameState& GameState, const APlayerController& Local)
	{
		const bool bRound = GameState.IsRoundInProgress();
		for (FConstPlayerControllerIterator It = World.GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Listener = It->Get();
			const ADinoPlayerState* ListenerState = Listener ? Listener->GetPlayerState<ADinoPlayerState>() : nullptr;
			if (!ListenerState || Listener == &Local)
			{
				continue;
			}

			for (const APlayerState* State : GameState.PlayerArray)
			{
				const ADinoPlayerState* Speaker = Cast<ADinoPlayerState>(State);
				if (!Speaker || Speaker == ListenerState || !Speaker->GetUniqueId().IsValid())
				{
					continue;
				}

				// Only the gameplay reason is ours; a "Muted" reason is that player's own Tab mute.
				const bool bExpected = DinoVoiceRules::ShouldMute(bRound, ListenerState->IsAlive(), Speaker->IsAlive(), HearsLiving(Listener));
				EVoiceBlockReasons Reasons;
				const FString ReasonText = DescribeReasons(Listener, Speaker->GetUniqueId(), Reasons);
				const bool bActual = EnumHasAnyFlags(Reasons, EVoiceBlockReasons::Gameplay);

				Add(Out, bExpected == bActual,
					FString::Printf(TEXT("Server sends %s to %s"), *Speaker->GetPlayerName(), *ListenerState->GetPlayerName()),
					FString::Printf(TEXT("listener %s, speaker %s, hears living %s; expected %s, reasons %s"),
						StatusName(ListenerState), StatusName(Speaker), HearsLiving(Listener) ? TEXT("yes") : TEXT("no"),
						bExpected ? TEXT("withheld") : TEXT("sent"), *ReasonText));
			}
		}
	}

	/** Host only: every player's status against the match phase, and their body or camera. */
	void CheckRoundState(TArray<FResult>& Out, const UWorld& World, const ADinoGameState& GameState)
	{
		const bool bLobby = GameState.IsInLobby();
		const bool bRound = GameState.IsRoundInProgress();
		const bool bOver = GameState.IsRoundOver();

		for (const APlayerState* State : GameState.PlayerArray)
		{
			const ADinoPlayerState* Player = Cast<ADinoPlayerState>(State);
			if (!Player)
			{
				continue;
			}

			const FString Who = Player->GetPlayerName();
			const AController* Controller = Player->GetOwningController();
			const ADinoCharacter* Body = Controller ? Cast<ADinoCharacter>(Controller->GetPawn()) : nullptr;

			if (bLobby)
			{
				Add(Out, Player->GetStatus() == EDinoPlayerStatus::Lobby, FString::Printf(TEXT("Lobby status: %s"), *Who),
					FString::Printf(TEXT("status %s; everyone should be Lobby before a round"), StatusName(Player)));
			}
			else if (bOver)
			{
				Add(Out, !Player->IsAlive(), FString::Printf(TEXT("Round-over status: %s"), *Who),
					FString::Printf(TEXT("status %s; nobody is alive once the round is over"), StatusName(Player)));
			}
			else if (bRound)
			{
				if (Player->IsAlive())
				{
					Add(Out, IsValid(Body) && Body->IsAlive(), FString::Printf(TEXT("Alive player has a body: %s"), *Who),
						Body ? (Body->IsAlive() ? TEXT("possessing a living character") : TEXT("possessing a DEAD character"))
							: TEXT("status Alive but possessing no character"));
				}
				else
				{
					// A player who just died is still in the death beat (ADinoGameMode's spectate delay);
					// the automatic runs wait it out.
					const bool bSpectating = Controller && Controller->IsInState(NAME_Spectating);
					Add(Out, bSpectating, FString::Printf(TEXT("Non-living player spectates: %s"), *Who),
						FString::Printf(TEXT("status %s, controller %s"), StatusName(Player),
							bSpectating ? TEXT("spectating") : TEXT("NOT spectating (fine only within a few seconds of dying)")));
				}
			}
		}

		// Push-to-talk on silently switches every mic off at each restart (CLAUDE.md, Voice chat).
		const AGameModeBase* GameMode = World.GetAuthGameMode();
		if (const AGameSession* Session = GameMode ? GameMode->GameSession.Get() : nullptr)
		{
			Add(Out, !Session->bRequiresPushToTalk, TEXT("Push-to-talk off"),
				Session->bRequiresPushToTalk ? TEXT("bRequiresPushToTalk is on: every mic will switch off at the next restart")
					: TEXT("bRequiresPushToTalk is off"));
		}
	}

	/** This player is either in a living body or watching through a spectator camera. */
	void CheckMyView(TArray<FResult>& Out, const ADinoGameState& GameState, const APlayerController& Local)
	{
		const ADinoPlayerState* Me = Local.GetPlayerState<ADinoPlayerState>();
		if (!Me || !(GameState.IsInLobby() || GameState.IsRoundInProgress() || GameState.IsRoundOver()))
		{
			return;
		}

		if (Me->IsAlive() && GameState.IsRoundInProgress())
		{
			const ADinoCharacter* Body = Cast<ADinoCharacter>(Local.GetPawn());
			Add(Out, IsValid(Body) && Body->IsAlive(), TEXT("My view: in my body"),
				Body ? TEXT("possessing my living character") : TEXT("alive but possessing no character"));
		}
		else
		{
			const bool bSpectating = Local.IsInState(NAME_Spectating);
			const bool bHasCamera = Local.GetSpectatorPawn() != nullptr;
			Add(Out, bSpectating && bHasCamera, TEXT("My view: spectator camera"),
				FString::Printf(TEXT("status %s, controller %s, camera %s"), StatusName(Me),
					bSpectating ? TEXT("spectating") : TEXT("NOT spectating"),
					bHasCamera ? TEXT("exists") : TEXT("MISSING (frozen view)")));
		}
	}
}

TArray<DinoSelfCheck::FResult> DinoSelfCheck::Run(APlayerController* LocalController, const FString& Trigger)
{
	TArray<FResult> Results;
	UWorld* World = LocalController ? LocalController->GetWorld() : nullptr;
	const ADinoGameState* GameState = World ? World->GetGameState<ADinoGameState>() : nullptr;
	if (!World || !GameState)
	{
		UE_LOG(LogDinoGame, Log, TEXT("DinoSelfCheck[%s]: skipped - not in a match (main menu, or still loading)."), *Trigger);
		return Results;
	}

	const bool bHost = LocalController->HasAuthority();
	const bool bPIE = World->WorldType == EWorldType::PIE;
	const UGameInstance* GameInstance = World->GetGameInstance();
	const UDinoVoiceSubsystem* VoiceSettings = GameInstance ? GameInstance->GetSubsystem<UDinoVoiceSubsystem>() : nullptr;

	CheckVoiceOfOthers(Results, *GameState, *LocalController, bPIE);
	CheckVoicePlayback(Results, *World, *GameState, *LocalController);
	CheckWhatIHear(Results, *GameState, *LocalController, VoiceSettings);
	CheckMyView(Results, *GameState, *LocalController);
	if (bHost)
	{
		CheckServerSends(Results, *World, *GameState, *LocalController);
		CheckRoundState(Results, *World, *GameState);
		CheckCreatureTargets(Results, *World);
		CheckRestraints(Results, *World);
	}

	// A missing or lost mic is the machine, not the code - noted, not failed.
	if (VoiceSettings && !VoiceSettings->GetMicNotice().IsEmpty())
	{
		Skip(Results, TEXT("Microphone"), VoiceSettings->GetMicNotice());
	}

	int32 Passed = 0, Failed = 0, Skipped = 0;
	for (const FResult& Result : Results)
	{
		switch (Result.Outcome)
		{
		case FResult::EOutcome::Pass:
			++Passed;
			UE_LOG(LogDinoGame, Log, TEXT("DinoSelfCheck[%s]: PASS %s | %s"), *Trigger, *Result.Name, *Result.Detail);
			break;
		case FResult::EOutcome::Fail:
			++Failed;
			UE_LOG(LogDinoGame, Warning, TEXT("DinoSelfCheck[%s]: FAIL %s | %s"), *Trigger, *Result.Name, *Result.Detail);
			break;
		case FResult::EOutcome::Skip:
			++Skipped;
			UE_LOG(LogDinoGame, Log, TEXT("DinoSelfCheck[%s]: SKIP %s | %s"), *Trigger, *Result.Name, *Result.Detail);
			break;
		}
	}

	const ADinoPlayerState* Me = LocalController->GetPlayerState<ADinoPlayerState>();
	UE_LOG(LogDinoGame, Log, TEXT("DinoSelfCheck[%s]: %d passed, %d failed, %d skipped - %s as %s, match %s."),
		*Trigger, Passed, Failed, Skipped, bHost ? TEXT("host") : TEXT("client"),
		Me ? *Me->GetPlayerName() : TEXT("?"), *GameState->GetMatchState().ToString());

	return Results;
}

int32 DinoSelfCheck::CountFailures(const TArray<FResult>& Results)
{
	int32 Failed = 0;
	for (const FResult& Result : Results)
	{
		Failed += Result.Outcome == FResult::EOutcome::Fail ? 1 : 0;
	}
	return Failed;
}
