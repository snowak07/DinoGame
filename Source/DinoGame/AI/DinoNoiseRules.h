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
	 * Whether one speaker's voice report is drawn by the noise debug: only if it is louder than
	 * the drawing still on screen for them, or that drawing has expired. A talker reports four
	 * times a second and each drawing lasts seconds, so drawing all of them stacked a dozen
	 * overlapping rings; this keeps the loudest, which is the one that matters for "how far did
	 * my voice reach".
	 *
	 * @param LastDrawTime      when this speaker's current drawing was made (very negative if never)
	 * @param LastDrawLoudness  its loudness
	 */
	inline bool ShouldDrawVoice(double Now, double LastDrawTime, float LastDrawLoudness, float Loudness, double DrawSeconds)
	{
		return Now - LastDrawTime >= DrawSeconds || Loudness > LastDrawLoudness;
	}
}
