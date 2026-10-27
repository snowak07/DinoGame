#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DinoNoiseSubsystem.generated.h"

/**
 * Noises creatures can hear - today, players' voices. Server only.
 *
 * Not the engine's hearing sense (UAISense_Hearing): that ignores walls entirely, so a whisper
 * behind a rock would carry exactly as far as one in the open. This checks each creature in
 * range with one line trace and shortens the range when something is in the way
 * (DinoNoiseRules::HeardRange), then has it investigate (ADinoAIControllerBase::Investigate).
 *
 * Voices reach it from ADinoPlayerController::ServerReportSpeech: each player's own machine
 * knows when it is sending voice and how loud, which the host cannot reliably know - it does not
 * hear players it has muted, and a dedicated server would hear nobody.
 *
 * Tuning: the dino.Noise.* console variables (docs/CONSOLE.md).
 */
UCLASS()
class DINOGAME_API UDinoNoiseSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * A voice at Location, Loudness 0-1 (DinoNoiseRules::VoiceLoudness), from Speaker. Every
	 * creature that hears it investigates, unless it is already hunting or attacking.
	 */
	void ReportVoice(const FVector& Location, float Loudness, AActor* Speaker);

	/** How far a voice of this loudness carries in the open, from the dino.Noise.* settings. */
	static float GetOpenRange(float Loudness);

	/** The loudest voice range: the furthest any creature can hear anyone. */
	static float GetShoutRange();

	/** The mic level that counts as a shout (loudness 1), for DinoNoiseRules::VoiceLoudness. */
	static float GetShoutMicLevel();

	/** dino.Noise.Debug, or the general DinoAI.DebugDraw. */
	static bool IsDebugDrawOn();

	/**
	 * The two rings for a voice from Speaker: how far it carries in the open, and through
	 * something. Flat on the floor under them, where they can be read; drawn at head height they
	 * cut through the speaker's own body. Drawn by the host, and by a speaker's own machine for
	 * their own voice - a client sees no host debug draw, but can work this much out for itself.
	 */
	static void DrawRangeRings(const UWorld* World, const AActor* Speaker, const FVector& MouthLocation, float Loudness);

	/** Seconds a noise drawing stays up, for the speaker-side throttle (DinoNoiseRules::ShouldDrawVoice). */
	static double GetDebugDrawSeconds();

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Per speaker, for a compact log line rather than one per report. */
	TMap<TWeakObjectPtr<AActor>, double> LastLoggedReport;

	/** Per speaker, the drawing on screen now: when it was made and how loud. See ShouldDrawVoice. */
	struct FDrawnVoice { double Time = -1.0e9; float Loudness = 0.0f; };
	TMap<TWeakObjectPtr<AActor>, FDrawnVoice> LastDrawnVoice;
};
