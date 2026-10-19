#pragma once

#include "CoreMinimal.h"

/**
 * Who hears whom, and from where, as plain functions with no engine state. The game applies
 * them (ADinoGameMode::RefreshVoiceRules, ADinoPlayerState::RefreshVoicePlacement), the
 * self-check compares the engine's actual state against them (DinoSelfCheck), and the unit
 * tests pin them (DinoGameTests). One copy, so the three cannot drift apart.
 */
namespace DinoVoiceRules
{
	/**
	 * Whether the server should stop sending Speaker's voice to Listener.
	 *
	 * Only a round separates the living from the dead: in the lobby nobody has a body, and at
	 * round over everyone is dead, so both are free talk. During a round the living never hear
	 * the dead, and a spectator hears the living only in the follow and free cameras.
	 */
	inline bool ShouldMute(bool bRoundRunning, bool bListenerAlive, bool bSpeakerAlive, bool bListenerHearsLivingWhileSpectating)
	{
		if (!bRoundRunning)
		{
			return false;
		}
		if (bListenerAlive)
		{
			return !bSpeakerAlive;
		}
		return bSpeakerAlive && !bListenerHearsLivingWhileSpectating;
	}

	/**
	 * Whether a speaker's voice should come from their body (with distance falloff and
	 * occlusion) rather than play unpositioned. Only while they are alive and their living body
	 * exists on this machine; a dead player's voice is unpositioned, so the dead hear each
	 * other at full volume.
	 */
	inline bool ShouldBePositioned(bool bSpeakerAlive, bool bHasLivingBody)
	{
		return bSpeakerAlive && bHasLivingBody;
	}
}
