#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DinoNoiseSubsystem.generated.h"

/**
 * Noises creatures can hear: players' voices, footsteps and landings. Server only.
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

	/**
	 * Any noise at Location from Source, carrying OpenRange in the open (less through things).
	 * Kind groups one source's noises for the log and debug draw ("voice", "footsteps"); Label
	 * describes this one for both; Reason is what a creature that investigates logs.
	 */
	void ReportNoise(const FVector& Location, float OpenRange, AActor* Source, FName Kind, const FString& Label, const TCHAR* Reason);

	/** How far a voice of this loudness carries in the open, from the dino.Noise.* settings. */
	static float GetOpenRange(float Loudness);

	/** The loudest voice range: the furthest any creature can hear anyone. */
	static float GetShoutRange();

	/** The mic level that counts as a shout (loudness 1), for DinoNoiseRules::VoiceLoudness. */
	static float GetShoutMicLevel();

	/** dino.Noise.Debug, or the general DinoAI.DebugDraw. */
	static bool IsDebugDrawOn();

	/**
	 * The two rings for a noise from Source: how far it carries in the open, and through
	 * something. Flat on the floor under it, where they can be read; drawn at head height they
	 * cut through the speaker's own body. Drawn by the host, and by a speaker's own machine for
	 * their own voice - a client sees no host debug draw, but can work this much out for itself.
	 */
	static void DrawRangeRings(const UWorld* World, const AActor* Source, const FVector& NoiseLocation, float OpenRange, const FString& Label);

	/** Seconds a noise drawing stays up, for the speaker-side throttle (DinoNoiseRules::ShouldDrawNoise). */
	static double GetDebugDrawSeconds();

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	using FSourceKind = TTuple<TWeakObjectPtr<AActor>, FName>;

	/** Per source and kind, for a compact log line rather than one per report. */
	TMap<FSourceKind, double> LastLoggedReport;

	/** Per source and kind, the drawing on screen now: when, and how far it carried. See ShouldDrawNoise. */
	struct FDrawnNoise { double Time = -1.0e9; float Range = 0.0f; };
	TMap<FSourceKind, FDrawnNoise> LastDrawnNoise;
};
