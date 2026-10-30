#pragma once

#include "CoreMinimal.h"

/**
 * Down, bleed out, revive: the rules as plain functions with no engine state. Applied by
 * ADinoCharacter and ADinoGameMode, compared against by DinoSelfCheck, pinned by the unit tests
 * (DinoGameTests) - one copy, so the three cannot drift apart.
 *
 * A player has health, and once it runs out a second meter: bleed-out. Damage that downs (a
 * raptor's bite) takes health to zero and the player goes down with a full bleed-out meter.
 * That meter drains on its own and from further damage - a raptor finishing what it started -
 * and at zero they are dead. Damage that does not down (a T-Rex's bite, a fall) kills outright,
 * down or not. A teammate's revive stands them back up with a fraction of their health.
 */
namespace DinoHealthRules
{
	constexpr float BleedOutFull = 100.0f;

	struct FVitals
	{
		float Health = 100.0f;
		bool bDowned = false;
		/** 0 - BleedOutFull while downed; 0 otherwise. */
		float BleedOut = 0.0f;
	};

	enum class EChange : uint8
	{
		None,
		Hurt,		// lost health, still standing
		Downed,		// health ran out to damage that downs
		Bleeding,	// downed, and the bleed-out meter fell
		Died,
	};

	inline bool IsDead(const FVitals& Vitals)     { return Vitals.Health <= 0.0f && !Vitals.bDowned; }
	inline bool IsStanding(const FVitals& Vitals) { return Vitals.Health > 0.0f && !Vitals.bDowned; }

	/**
	 * Damage of Amount, which downs if bCanDown and otherwise kills. Downed, damage drains the
	 * bleed-out meter point for point instead of health.
	 */
	inline EChange ApplyDamage(FVitals& Vitals, float Amount, bool bCanDown)
	{
		if (Amount <= 0.0f || IsDead(Vitals))
		{
			return EChange::None;
		}

		if (Vitals.bDowned)
		{
			if (!bCanDown)
			{
				Vitals = FVitals{ 0.0f, false, 0.0f };
				return EChange::Died;
			}
			Vitals.BleedOut = FMath::Max(0.0f, Vitals.BleedOut - Amount);
			if (Vitals.BleedOut <= 0.0f)
			{
				Vitals = FVitals{ 0.0f, false, 0.0f };
				return EChange::Died;
			}
			return EChange::Bleeding;
		}

		Vitals.Health = FMath::Max(0.0f, Vitals.Health - Amount);
		if (Vitals.Health > 0.0f)
		{
			return EChange::Hurt;
		}
		if (bCanDown)
		{
			Vitals.bDowned = true;
			Vitals.BleedOut = BleedOutFull;
			return EChange::Downed;
		}
		return EChange::Died;
	}

	/** DeltaSeconds of bleeding out, the full meter lasting FullSeconds. Nothing while standing. */
	inline EChange Bleed(FVitals& Vitals, float DeltaSeconds, float FullSeconds)
	{
		if (!Vitals.bDowned || DeltaSeconds <= 0.0f)
		{
			return EChange::None;
		}
		Vitals.BleedOut = FMath::Max(0.0f, Vitals.BleedOut - BleedOutFull * DeltaSeconds / FMath::Max(FullSeconds, 0.1f));
		if (Vitals.BleedOut <= 0.0f)
		{
			Vitals = FVitals{ 0.0f, false, 0.0f };
			return EChange::Died;
		}
		return EChange::Bleeding;
	}

	/** Back on their feet with HealthFraction of MaxHealth (at least 1). Only from downed. */
	inline bool Revive(FVitals& Vitals, float MaxHealth, float HealthFraction)
	{
		if (!Vitals.bDowned)
		{
			return false;
		}
		Vitals = FVitals{ FMath::Max(1.0f, MaxHealth * FMath::Clamp(HealthFraction, 0.0f, 1.0f)), false, 0.0f };
		return true;
	}

	/** Why a revive cannot go on, or nullptr when it can. A reason, because a stopped revive is logged. */
	inline const TCHAR* WhyCannotRevive(bool bReviverStanding, bool bReviverHeld, bool bTargetDowned, bool bTargetHeld,
		float Distance, float MaxDistance, float FacingDot, float MinFacingDot)
	{
		if (!bReviverStanding)          { return TEXT("the reviver is not on their feet"); }
		if (bReviverHeld)               { return TEXT("the reviver is pinned"); }
		if (!bTargetDowned)             { return TEXT("they are no longer down"); }
		if (bTargetHeld)                { return TEXT("a creature is holding them"); }
		if (Distance > MaxDistance)     { return TEXT("too far away"); }
		if (FacingDot < MinFacingDot)   { return TEXT("not facing them"); }
		return nullptr;
	}

	/**
	 * With nobody left standing, nobody can revive anyone: everyone still down dies then, rather
	 * than the round waiting out their bleed-out.
	 */
	inline bool ShouldBleedOutEveryone(int32 Standing, int32 Downed)
	{
		return Standing == 0 && Downed > 0;
	}
}
