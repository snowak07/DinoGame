#pragma once

#include "CoreMinimal.h"

class APlayerController;

/**
 * Checks this machine's live game state against the rules it should follow, and logs a line
 * per check: "DinoSelfCheck[trigger]: PASS|FAIL|SKIP <check> | <detail>", then a summary. The
 * " | " is what Tools/check_logs.py splits on: check names themselves contain colons.
 *
 * Written for the bugs that only show on a real two-machine packaged build, after a restart -
 * the kind PIE and unit tests cannot reach. Every one of the October voice bugs was a silent
 * state mismatch (a talker unregistered, a mute never lifted) that this would have named in the
 * first round it happened, from either machine's log.
 *
 * Runs on every machine, about what that machine can see:
 * - every machine: each other player's voice is registered with the engine and positioned as
 *   the rules say; any voice playing right now really plays through those settings, and none
 *   plays without a talker; this player receives exactly the voices the rules allow; this
 *   player is either in a living body or watching through a spectator camera.
 * - the host also: what the server sends to every other player, every player's status against
 *   the match phase, push-to-talk being off, every creature's target passing the AI's own
 *   target rule, and both halves of every pin or devour agreeing.
 *
 * Runs on demand (DinoSelfCheck) and by itself a few seconds after every match phase change,
 * status change and join or leave (ADinoPlayerController::ScheduleSelfCheck). Failures are
 * logged as warnings, so Tools/check_logs.py counts them.
 *
 * The rules compared against are DinoVoiceRules - the same functions the game applies - so a
 * failure means the engine's state disagrees with what the game decided, not that two copies
 * of a rule drifted apart.
 */
namespace DinoSelfCheck
{
	struct FResult
	{
		enum class EOutcome : uint8 { Pass, Fail, Skip };

		EOutcome Outcome = EOutcome::Pass;
		FString Name;
		FString Detail;
	};

	/** Runs every check that applies on this machine, logs each one, and returns them. */
	TArray<FResult> Run(APlayerController* LocalController, const FString& Trigger);

	int32 CountFailures(const TArray<FResult>& Results);
}
