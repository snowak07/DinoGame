#pragma once

#include "CoreMinimal.h"

/**
 * How far a noise carries, as plain functions with no engine state. Applied by
 * UDinoNoiseSubsystem, pinned by the unit tests (DinoGameTests).
 */
namespace DinoNoiseRules
{
	/**
	 * A voice's loudness, 0 (just above the level that is sent at all) to 1 (shouting), from the
	 * speaker's own mic. Worked out on the speaker's machine against their own threshold, so a
	 * quiet mic with high sensitivity and a loud one with low sensitivity both read the same.
	 */
	inline float VoiceLoudness(float MicLevel, float SendThreshold, float ShoutLevel)
	{
		if (ShoutLevel <= SendThreshold)
		{
			return MicLevel >= SendThreshold ? 1.0f : 0.0f;
		}
		return FMath::Clamp((MicLevel - SendThreshold) / (ShoutLevel - SendThreshold), 0.0f, 1.0f);
	}

	/**
	 * How far a noise of this loudness carries to one listener: WhisperRange at 0, ShoutRange at
	 * 1, cut by WallFactor when geometry is in the way. The engine's own hearing ignores walls,
	 * which is why noise does not go through it.
	 */
	inline float HeardRange(float Loudness, bool bBlocked, float WhisperRange, float ShoutRange, float WallFactor)
	{
		const float Range = FMath::Lerp(WhisperRange, ShoutRange, FMath::Clamp(Loudness, 0.0f, 1.0f));
		return bBlocked ? Range * FMath::Clamp(WallFactor, 0.0f, 1.0f) : Range;
	}

	/**
	 * Whether a noise is drawn by the noise debug: only if it carries further than the drawing of
	 * the same kind still on screen for its source, or that drawing has expired. A talker reports
	 * four times a second and each drawing lasts seconds, so drawing all of them stacked a dozen
	 * overlapping rings; this keeps the loudest, which is the one that matters for "how far did
	 * that reach".
	 *
	 * @param LastDrawTime   when the current drawing was made (very negative if never)
	 * @param LastDrawRange  how far it carried
	 */
	inline bool ShouldDrawNoise(double Now, double LastDrawTime, float LastDrawRange, float Range, double DrawSeconds)
	{
		return Now - LastDrawTime >= DrawSeconds || Range > LastDrawRange;
	}

	/**
	 * How far a player's footsteps carry: none while crouched or in the air (landing is its own
	 * noise), none when barely moving, otherwise WalkRange scaled by speed - so a gamepad stick
	 * pushed halfway is half as loud.
	 *
	 * @param MinSpeed  below this (cm/s) a player is shuffling or turning on the spot: silent
	 */
	inline float FootstepRange(float Speed, float MaxSpeed, bool bCrouched, bool bOnGround, float WalkRange, float MinSpeed = 50.0f)
	{
		if (bCrouched || !bOnGround || Speed < MinSpeed || MaxSpeed <= 0.0f)
		{
			return 0.0f;
		}
		return WalkRange * FMath::Clamp(Speed / MaxSpeed, 0.0f, 1.0f);
	}
}
