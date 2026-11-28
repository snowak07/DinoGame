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

	/**
	 * Two bodies this close, capsule edge to edge and flat, are touching: one bumped into the other.
	 */
	inline bool IsTouching(float EdgeDistance, float TouchDistance)
	{
		return EdgeDistance <= TouchDistance;
	}

	/**
	 * Whether a creature finds a player hiding in a bush. From outside, a bush blocks sight, the
	 * close sense and half of any noise; these are the ways past it - bumping into them, walking
	 * into the bush they are in, or stopping to check that bush (a search step). Always found,
	 * never a chance: the counterplay is not letting it get that close.
	 */
	inline bool FindsHiddenPlayer(bool bTouching, bool bInSameBush, bool bCheckedTheirBush)
	{
		return bTouching || bInSameBush || bCheckedTheirBush;
	}

	/**
	 * Whether a searching creature's next leg checks a bush instead of walking to its next search
	 * point.
	 *
	 * A spot inside a bush is always checked from its edge: walking in would find whoever is there
	 * with no warning. Otherwise a bush in the search area is checked every other leg, so a search
	 * still covers the open ground between bushes - and never in place of a freshly investigated
	 * spot, which comes first.
	 */
	inline bool ShouldCheckBushNext(bool bDestinationInUncheckedBush, bool bInvestigationPending,
		bool bUncheckedBushInRange, bool bLastLegCheckedBush)
	{
		if (bDestinationInUncheckedBush)
		{
			return true;
		}
		return !bInvestigationPending && bUncheckedBushInRange && !bLastLegCheckedBush;
	}

	/**
	 * Whether a noise lets a creature carry on checking a bush (or walking to check it) rather
	 * than redirecting its search. A noise from that same bush is what it is already doing. Left
	 * to redirect, a player talking in their bush cancelled the very check that would have found
	 * them. A forced investigation (a flare hit) always redirects.
	 */
	inline bool NoiseKeepsBushCheck(bool bBusyWithBush, bool bForced, bool bNoiseFromThatBush)
	{
		return bBusyWithBush && !bForced && bNoiseFromThatBush;
	}

	/**
	 * Whether a bush counts as checked. Only a check that ran to the end, or one the creature could
	 * not get to (so it does not keep trying): an interrupted check saw nothing.
	 */
	inline bool CountsAsChecked(bool bCheckFinished, bool bCouldNotReach)
	{
		return bCheckFinished || bCouldNotReach;
	}

	/**
	 * Whether a noise from a bush already checked this search makes it worth checking again. It
	 * was empty then; a player may have crept in since. Not when the creature already found the
	 * one making the noise there - it knows, and is coming.
	 */
	inline bool NoiseReopensBush(bool bBushChecked, bool bFoundThemThere)
	{
		return bBushChecked && !bFoundThemThere;
	}
}
