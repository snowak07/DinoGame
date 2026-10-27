#pragma once

#include "CoreMinimal.h"

/**
 * Which player a hunting creature goes after, as plain functions with no engine state. Applied
 * by ADinoAIControllerBase::ReconsiderTarget, compared against by DinoSelfCheck, pinned by the
 * unit tests (DinoGameTests) - one copy, so the three cannot drift apart.
 */
namespace DinoTargetRules
{
	/** Passed as CurrentDistance when the current target is not in sight right now. */
	constexpr float NotInSight = -1.0f;

	/**
	 * Whether a hunting creature should switch from its current target to the nearest player it
	 * can see.
	 *
	 * Closest wins, with no margin: a player who is nearer by any amount takes over. What stops
	 * two players at similar distances making it flick between them every frame is the cooldown -
	 * after any change of target it commits for CooldownSeconds.
	 *
	 * Never mid-attack. A raptor that has pinned someone finishes them, whoever walks up.
	 *
	 * @param CurrentDistance    to the current target, or NotInSight
	 * @param CandidateDistance  to the nearest visible valid player other than the current target
	 */
	inline bool ShouldSwitchTarget(float CurrentDistance, float CandidateDistance,
		double SecondsSinceLastChange, double CooldownSeconds, bool bAttackInProgress)
	{
		if (bAttackInProgress || SecondsSinceLastChange < CooldownSeconds)
		{
			return false;
		}
		if (CurrentDistance < 0.0f)
		{
			// The current target is out of sight and someone else is in it: the one it can see wins.
			return true;
		}
		return CandidateDistance < CurrentDistance;
	}

	/**
	 * Whether a creature notices a player by being right next to them, in any direction - the
	 * answer to a narrow sight cone making the space behind a creature perfectly safe. Walls
	 * still block it: "it heard you breathing", not "it knows where you are".
	 *
	 * @param EdgeDistance  between the two bodies' capsule edges, flat (ignoring height)
	 */
	inline bool IsWithinCloseSense(float EdgeDistance, float CloseSenseRadius, bool bLineBlocked)
	{
		return !bLineBlocked && EdgeDistance <= CloseSenseRadius;
	}

	/**
	 * A range that crouching shortens - sight, and the close-range sense. Factor is the fraction
	 * left while crouched (dino.Stealth.*), clamped to 0-1 so tuning can only ever hide, not reveal.
	 */
	inline float StealthRange(float Range, bool bCrouched, float CrouchFactor)
	{
		return bCrouched ? Range * FMath::Clamp(CrouchFactor, 0.0f, 1.0f) : Range;
	}

	/**
	 * Whether something heard or felt makes a creature investigate it. Only when it is not
	 * already committed: a hunting or attacking creature ignores it, so a sound never pulls it
	 * off the player it is chasing.
	 */
	inline bool ShouldInvestigate(bool bHunting, bool bAttacking)
	{
		return !bHunting && !bAttacking;
	}
}
