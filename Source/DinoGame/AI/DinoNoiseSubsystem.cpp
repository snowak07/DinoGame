#include "AI/DinoNoiseSubsystem.h"

#include "AI/DinoAIControllerBase.h"
#include "AI/DinoCreature.h"
#include "AI/DinoNoiseRules.h"
#include "DinoGame.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<float> CVarWhisperRange(TEXT("dino.Noise.WhisperRange"), 500.0f,
		TEXT("How far (cm) the quietest voice that is sent at all carries to a creature. Default 500 (5 m)."));

	TAutoConsoleVariable<float> CVarShoutRange(TEXT("dino.Noise.ShoutRange"), 2500.0f,
		TEXT("How far (cm) a shout carries to a creature. Default 2500 (25 m)."));

	TAutoConsoleVariable<float> CVarShoutMicLevel(TEXT("dino.Noise.ShoutMicLevel"), 0.3f,
		TEXT("The mic level (0-1, as DinoVoiceStatus shows it) that counts as a shout - full range. Read on the speaker's machine. Default 0.3."));

	TAutoConsoleVariable<float> CVarWallFactor(TEXT("dino.Noise.WallFactor"), 0.5f,
		TEXT("How much of a voice's range is left when geometry is between it and a creature. Default 0.5."));

	TAutoConsoleVariable<int32> CVarNoiseDebug(TEXT("dino.Noise.Debug"), 0,
		TEXT("1 draws voices' hearing on the host: the range rings around the speaker, and a line to every creature in earshot, ")
		TEXT("cut where something blocks it, with what. Also on with DinoAI.DebugDraw. Your own range ring draws on your machine."));

	/**
	 * Long enough to walk over and read the labels after a single DinoNoise. While someone keeps
	 * talking, reports come 4 times a second, so several draws overlap - fine for reading where
	 * a voice reached.
	 */
	constexpr float DebugDrawSeconds = 4.5f;

	/** How often one speaker's voice may set off investigations. Reports arrive up to 4 times a second. */
	constexpr double VoiceRetriggerSeconds = 1.0;

	/** How often a still-talking speaker gets a "heard by" log line. */
	constexpr double LogIntervalSeconds = 2.0;
}

bool UDinoNoiseSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

float UDinoNoiseSubsystem::GetOpenRange(float Loudness)
{
	return DinoNoiseRules::HeardRange(Loudness, false, CVarWhisperRange.GetValueOnGameThread(),
		CVarShoutRange.GetValueOnGameThread(), CVarWallFactor.GetValueOnGameThread());
}

float UDinoNoiseSubsystem::GetShoutRange()
{
	return CVarShoutRange.GetValueOnGameThread();
}

float UDinoNoiseSubsystem::GetShoutMicLevel()
{
	return CVarShoutMicLevel.GetValueOnGameThread();
}

bool UDinoNoiseSubsystem::IsDebugDrawOn()
{
	static const IConsoleVariable* AIDebugDraw = IConsoleManager::Get().FindConsoleVariable(TEXT("DinoAI.DebugDraw"));
	return CVarNoiseDebug.GetValueOnGameThread() != 0 || (AIDebugDraw && AIDebugDraw->GetInt() != 0);
}

double UDinoNoiseSubsystem::GetDebugDrawSeconds()
{
	return DebugDrawSeconds;
}

void UDinoNoiseSubsystem::DrawRangeRings(const UWorld* World, const AActor* Speaker, const FVector& MouthLocation, float Loudness)
{
#if ENABLE_DRAW_DEBUG
	if (!World)
	{
		return;
	}

	// The floor under the speaker, a little above it so the rings are not buried in it.
	FVector Location = MouthLocation;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DinoNoiseDebugFloor), false, Speaker);
	FHitResult Floor;
	if (World->LineTraceSingleByChannel(Floor, MouthLocation, MouthLocation - FVector(0.0f, 0.0f, 1000.0f), ECC_Visibility, Params))
	{
		Location = Floor.ImpactPoint + FVector(0.0f, 0.0f, 5.0f);
	}

	const float Whisper = CVarWhisperRange.GetValueOnGameThread();
	const float Shout = CVarShoutRange.GetValueOnGameThread();
	const float WallFactor = CVarWallFactor.GetValueOnGameThread();
	const float Open = DinoNoiseRules::HeardRange(Loudness, false, Whisper, Shout, WallFactor);
	const float Walled = DinoNoiseRules::HeardRange(Loudness, true, Whisper, Shout, WallFactor);

	// Flat rings at the speaker's height: yellow is how far this voice carries in the open,
	// orange how far through something.
	DrawDebugCircle(World, Location, Open, 64, FColor::Yellow, false, DebugDrawSeconds, 0, 4.0f, FVector::ForwardVector, FVector::RightVector, false);
	DrawDebugCircle(World, Location, Walled, 48, FColor::Orange, false, DebugDrawSeconds, 0, 2.0f, FVector::ForwardVector, FVector::RightVector, false);
	DrawDebugString(World, Location + FVector(0.0f, 0.0f, 40.0f),
		FString::Printf(TEXT("voice %.2f: %.0f m open, %.0f m blocked"), Loudness, Open / 100.0f, Walled / 100.0f),
		nullptr, FColor::Yellow, DebugDrawSeconds, true);
#endif
}

void UDinoNoiseSubsystem::ReportVoice(const FVector& Location, float Loudness, AActor* Speaker)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !Speaker)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const float Whisper = CVarWhisperRange.GetValueOnGameThread();
	const float Shout = CVarShoutRange.GetValueOnGameThread();
	const float WallFactor = CVarWallFactor.GetValueOnGameThread();

	// Drawn only when louder than this speaker's drawing still on screen, or once it expires.
	bool bDraw = false;
	if (IsDebugDrawOn())
	{
		FDrawnVoice& Drawn = LastDrawnVoice.FindOrAdd(Speaker);
		if (DinoNoiseRules::ShouldDrawVoice(Now, Drawn.Time, Drawn.Loudness, Loudness, DebugDrawSeconds))
		{
			Drawn.Time = Now;
			Drawn.Loudness = Loudness;
			bDraw = true;
			DrawRangeRings(World, Speaker, Location, Loudness);
		}
	}

	TArray<FString> Heard;
	TArray<FString> TooFar;
	for (TActorIterator<ADinoCreature> It(World); It; ++It)
	{
		ADinoCreature* Creature = *It;
		ADinoAIControllerBase* Brain = Cast<ADinoAIControllerBase>(Creature->GetController());
		if (!Brain || Creature->IsDead())
		{
			continue;
		}

		const float Distance = FVector::Dist(Location, Creature->GetActorLocation());
		if (Distance > Shout)
		{
			continue; // beyond any voice; not worth a trace or a log entry
		}

		FVector Eyes;
		FRotator EyeRotation;
		Creature->GetActorEyesViewPoint(Eyes, EyeRotation);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DinoVoiceNoise), false, Creature);
		Params.AddIgnoredActor(Speaker);
		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByChannel(Hit, Location, Eyes, ECC_Visibility, Params);

		// What cut the line, and how far along: the question when voices seem to carry too little.
		// A tree trunk, a bush, a rock and a rise in the ground all call for different fixes.
		const FString Blocker = bBlocked
			? FString::Printf(TEXT("%s/%s at %.1f m"), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
				FVector::Dist(Location, Hit.ImpactPoint) / 100.0f)
			: FString();

		const float Range = DinoNoiseRules::HeardRange(Loudness, bBlocked, Whisper, Shout, WallFactor);
		const bool bHeard = Distance <= Range;
		const FString Entry = FString::Printf(TEXT("%s %.1f/%.1f m%s"), *Creature->GetName(),
			Distance / 100.0f, Range / 100.0f, bBlocked ? *FString::Printf(TEXT(" muffled by %s"), *Blocker) : TEXT(""));

#if ENABLE_DRAW_DEBUG
		if (bDraw)
		{
			// Green: heard. Red: out of reach. A blocked line is orange up to what blocked it -
			// marked and named - then grey on to the creature's eyes.
			const FColor Verdict = bHeard ? FColor::Green : FColor::Red;
			if (bBlocked)
			{
				DrawDebugLine(World, Location, Hit.ImpactPoint, FColor::Orange, false, DebugDrawSeconds, 0, 3.0f);
				DrawDebugLine(World, Hit.ImpactPoint, Eyes, FColor(90, 90, 90), false, DebugDrawSeconds, 0, 1.5f);
				DrawDebugSphere(World, Hit.ImpactPoint, 15.0f, 8, FColor::Orange, false, DebugDrawSeconds);
				DrawDebugString(World, Hit.ImpactPoint + FVector(0.0f, 0.0f, 25.0f),
					FString::Printf(TEXT("blocked by %s"), *GetNameSafe(Hit.GetActor())), nullptr, FColor::Orange, DebugDrawSeconds, true);
			}
			else
			{
				DrawDebugLine(World, Location, Eyes, Verdict, false, DebugDrawSeconds, 0, 3.0f);
			}
			DrawDebugString(World, Eyes + FVector(0.0f, 0.0f, 40.0f),
				FString::Printf(TEXT("%s %.1f / %.1f m"), bHeard ? TEXT("HEARD") : TEXT("too far"), Distance / 100.0f, Range / 100.0f),
				nullptr, Verdict, DebugDrawSeconds, true);
		}
#endif

		if (!bHeard)
		{
			TooFar.Add(Entry);
			continue;
		}

		Heard.Add(Entry);
		if (Brain->CanReactToVoice(Speaker, Now, VoiceRetriggerSeconds))
		{
			Brain->Investigate(Location, Speaker, TEXT("a voice"));
		}
	}

	// One line per speaker every couple of seconds while they talk: who heard, who was in reach
	// of a shout but not of this voice, and the numbers behind each - distance / heard range.
	double& LastLog = LastLoggedReport.FindOrAdd(Speaker, -1.0e9);
	if ((!Heard.IsEmpty() || !TooFar.IsEmpty()) && Now - LastLog >= LogIntervalSeconds)
	{
		LastLog = Now;
		UE_LOG(LogDinoGame, Log, TEXT("Noise: %s's voice (loudness %.2f) heard by [%s]; out of reach [%s]."),
			*Speaker->GetName(), Loudness, *FString::Join(Heard, TEXT(", ")), *FString::Join(TooFar, TEXT(", ")));
	}
}
