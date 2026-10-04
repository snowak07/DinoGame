#include "AI/DinoCreature.h"

#include "AI/DinoAIControllerBase.h"
#include "AI/DinoAttackComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DinoGame.h"
#include "DrawDebugHelpers.h"
#include "NavigationInvokerComponent.h"
#include "Net/UnrealNetwork.h"

static TAutoConsoleVariable<int32> CVarDinoAIDebugDraw(
	TEXT("DinoAI.DebugDraw"),
	0,
	TEXT("Draw a state-coloured capsule and label over every Dino creature. 0 off, 1 on."),
	ECVF_Cheat);

namespace
{
	// Lifetime is slightly longer than the interval so shapes never flicker between redraws.
	constexpr float DebugDrawInterval = 0.1f;
	constexpr float DebugDrawLifetime = 0.15f;
}

ADinoCreature::ADinoCreature()
{
	PrimaryActorTick.bCanEverTick = false;

	NavigationInvoker = CreateDefaultSubobject<UNavigationInvokerComponent>(TEXT("NavigationInvoker"));

	// Only the server runs AI, so only the server needs navmesh generated around creatures.
	// Generating it on clients too would cost every player CPU for data nothing reads.
	NavigationInvoker->SetAutoActivate(false);

	// ACharacter defaults these to following the controller's control rotation, which an AI
	// controller never aims at where it is walking. The creature then slides around facing
	// whatever direction it spawned in - and, worse, its sight cone points there too, so it
	// fails to see things directly ahead of its movement.
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = true;
		Movement->bUseControllerDesiredRotation = false;
		Movement->RotationRate = FRotator(0.0f, TurnRateDegreesPerSecond, 0.0f);
	}
}

void ADinoCreature::BeginPlay()
{
	Super::BeginPlay();

	// Deferred from the constructor so it activates only where AI actually runs. Doing
	// this in the constructor would enable it on every client as well.
	if (HasAuthority() && NavigationInvoker)
	{
		NavigationInvoker->Activate();
	}

	// Re-applied here so a Blueprint child's override is honoured; the constructor only ever
	// sees the C++ default.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->RotationRate = FRotator(0.0f, TurnRateDegreesPerSecond, 0.0f);
	}

	AttackComponent = FindComponentByClass<UDinoAttackComponent>();

	if (HasAuthority())
	{
		CurrentHealth = MaxHealth;
	}

	// Runs on every machine, not just the server: Awareness is replicated, so clients can
	// draw the same colours without any AI existing on them. Cheap while the CVar is off -
	// one comparison every 100ms, and no per-frame tick on the actor.
	GetWorldTimerManager().SetTimer(
		DebugDrawTimer, this, &ADinoCreature::DrawStateDebug, DebugDrawInterval, true);
}

FVector ADinoCreature::GetPawnViewLocation() const
{
	// Zero means "not configured", so the engine behaviour stands rather than putting the
	// eyes on the floor - an unset value should degrade to the old behaviour, not to a bug.
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (EyeHeightAboveFeet <= 0.0f || !Capsule)
	{
		return Super::GetPawnViewLocation();
	}

	return GetActorLocation() +
		FVector(0.0f, 0.0f, EyeHeightAboveFeet - Capsule->GetScaledCapsuleHalfHeight());
}

float ADinoCreature::GetEQSContextHeightOffset() const
{
	// Derived from the same two numbers GetPawnViewLocation uses, so the query and the eyes
	// cannot drift apart.
	return GetPawnViewLocation().Z - GetActorLocation().Z;
}

float ADinoCreature::GetEyeHeightAboveFeet() const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	return Capsule
		? GetPawnViewLocation().Z - (GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight())
		: 0.0f;
}

void ADinoCreature::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADinoCreature, Awareness);
	DOREPLIFETIME(ADinoCreature, CurrentHealth);
}

void ADinoCreature::SetAwareness(EDinoAwareness NewAwareness)
{
	if (!HasAuthority() || NewAwareness == Awareness)
	{
		return;
	}

	const EDinoAwareness OldAwareness = Awareness;
	Awareness = NewAwareness;

	// A RepNotify never fires on the machine that wrote the value, so the server has to
	// broadcast for itself. On a listen server the host is a player too, and would
	// otherwise miss every cue its own clients get.
	BroadcastAwarenessChange(OldAwareness);
}

void ADinoCreature::OnRep_Awareness(EDinoAwareness OldAwareness)
{
	BroadcastAwarenessChange(OldAwareness);
}

UDinoAttackComponent* ADinoCreature::GetAttackComponent() const
{
	// Falls back to a search for callers that run before BeginPlay has cached it.
	return AttackComponent ? AttackComponent.Get() : FindComponentByClass<UDinoAttackComponent>();
}

float ADinoCreature::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
	if (!HasAuthority() || ActualDamage <= 0.0f || IsDead())
	{
		return 0.0f;
	}

	OnCreatureDamaged.Broadcast(ActualDamage, DamageCauser);

	// Health first, so a killing blow does not also start a stagger on a corpse.
	if (bCanBeKilled)
	{
		const float OldHealth = CurrentHealth;
		CurrentHealth = FMath::Max(0.0f, CurrentHealth - ActualDamage);
		ApplyHealthChange(OldHealth);
	}

	FString Reaction = TEXT("no effect");
	if (IsDead())
	{
		Reaction = TEXT("killed");
	}
	else if (bCanBeStaggered && ActualDamage >= StaggerDamageThreshold)
	{
		// A staggerable creature with no attack component has nothing to be knocked off, so the
		// stagger has nowhere to live. Logged rather than silent: it is a setup mistake.
		UDinoAttackComponent* Attack = GetAttackComponent();
		if (!Attack)
		{
			Reaction = TEXT("stagger skipped - no attack component");
		}
		else
		{
			Reaction = Attack->BeginStagger(StaggerDuration) ? TEXT("staggered") : TEXT("stagger refused");
		}
	}
	else if (bCanBeStaggered)
	{
		Reaction = FString::Printf(TEXT("below stagger threshold %.0f"), StaggerDamageThreshold);
	}

	UE_LOG(LogDinoGame, Log, TEXT("%s took %.0f from %s: %s."), *GetName(), ActualDamage,
		*GetNameSafe(DamageCauser), *Reaction);

	return bCanBeKilled ? ActualDamage : 0.0f;
}

FString ADinoCreature::DescribeVulnerability() const
{
	if (!bCanBeStaggered && !bCanBeKilled)
	{
		return TEXT("invulnerable, cannot be staggered");
	}

	const FString Stagger = bCanBeStaggered
		? FString::Printf(TEXT("stagger >= %.0f for %.1fs"), StaggerDamageThreshold, StaggerDuration)
		: FString(TEXT("no stagger"));

	const FString Health = bCanBeKilled
		? FString::Printf(TEXT("killable %.0f/%.0f"), CurrentHealth, MaxHealth)
		: FString(TEXT("unkillable"));

	return Stagger + TEXT(", ") + Health;
}

void ADinoCreature::OnRep_CurrentHealth(float OldHealth)
{
	ApplyHealthChange(OldHealth);
}

void ADinoCreature::ApplyHealthChange(float OldHealth)
{
	if (bCanBeKilled && CurrentHealth <= 0.0f && OldHealth > 0.0f)
	{
		HandleDeath();
	}
}

void ADinoCreature::HandleDeath()
{
	UE_LOG(LogDinoGame, Log, TEXT("%s died."), *GetName());

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	if (HasAuthority())
	{
		// Cancel before stopping the brain, so a pinned victim is released by the attack's own
		// cleanup rather than left held by a corpse.
		if (UDinoAttackComponent* Attack = GetAttackComponent())
		{
			Attack->CancelAttack();
		}

		if (ADinoAIControllerBase* AI = Cast<ADinoAIControllerBase>(GetController()))
		{
			AI->HandleCreatureDied();
		}

		if (NavigationInvoker)
		{
			NavigationInvoker->Deactivate();
		}
	}

	OnCreatureDied.Broadcast();
}

void ADinoCreature::BroadcastAwarenessChange(EDinoAwareness OldAwareness)
{
	UE_LOG(LogDinoGame, Verbose, TEXT("%s awareness: %s -> %s"),
		*GetName(),
		*UEnum::GetDisplayValueAsText(OldAwareness).ToString(),
		*UEnum::GetDisplayValueAsText(Awareness).ToString());

	OnAwarenessChanged.Broadcast(Awareness, OldAwareness);
}

void ADinoCreature::DrawStateDebug()
{
	// A vision test draws whether or not debug draw is on: without the lines it shows nothing.
	const ADinoAIControllerBase* VisionTest = Cast<ADinoAIControllerBase>(GetController());
	if (VisionTest && VisionTest->IsInVisionTest())
	{
		VisionTest->DrawVisionTest(GetWorld(), DebugDrawLifetime);
	}
	else
	{
		VisionTest = nullptr;
	}

	if (CVarDinoAIDebugDraw.GetValueOnGameThread() == 0 && !VisionTest)
	{
		return;
	}

	const UWorld* World = GetWorld();
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!World || !Capsule)
	{
		return;
	}

	const FColor Colour = DinoAwarenessColor(Awareness);
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float Radius = Capsule->GetScaledCapsuleRadius();

	DrawDebugCapsule(World, GetActorLocation(), HalfHeight, Radius, FQuat::Identity,
		Colour, false, DebugDrawLifetime, 0, 4.0f);

	// Facing matters as much as state: "why is it not reacting to me" is usually "it is not
	// looking at you", and that is invisible without this.
	DrawDebugDirectionalArrow(World,
		GetActorLocation(),
		GetActorLocation() + GetActorForwardVector() * (Radius + 250.0f),
		120.0f, Colour, false, DebugDrawLifetime, 0, 6.0f);

	// Where the creature actually sees from, drawn against the model so eye height can be
	// judged by eye rather than computed. A sight cone starting at the wrong height is the
	// kind of error that reads as "the AI is cheating" or "the AI is blind" and never as a
	// number being wrong.
	const FVector EyeLocation = GetPawnViewLocation();
	DrawDebugSphere(World, EyeLocation, 30.0f, 8, FColor::Cyan, false, DebugDrawLifetime, 0, 2.0f);

	// View rotation, not actor forward: the sight cone is centred on the controller's control
	// rotation, which tracks the focus target while hunting and only falls back to the body's
	// facing when there is no focus. Drawing the body direction here would show the two as
	// identical and hide the very thing worth seeing - that the eyes and the body diverge.
	DrawDebugLine(World, EyeLocation, EyeLocation + GetViewRotation().Vector() * 400.0f,
		FColor::Cyan, false, DebugDrawLifetime, 0, 1.5f);

	FString Label = FString::Printf(TEXT("%s : %s%s"),
		SpeciesName.IsNone() ? *GetName() : *SpeciesName.ToString(),
		IsDead() ? TEXT("DEAD") : *DinoAwarenessName(Awareness),
		bCanBeKilled && !IsDead() ? *FString::Printf(TEXT("  (%.0f hp)"), CurrentHealth) : TEXT(""));

	// Attack shapes and phase live in the component, which knows its own style. Phase and held
	// victim replicate, so this draws the same on clients as on the host.
	if (const UDinoAttackComponent* Attack = GetAttackComponent())
	{
		Attack->DrawDebug(World, DebugDrawLifetime);

		const FString AttackText = Attack->DescribeAttack();
		if (!AttackText.IsEmpty())
		{
			Label += TEXT("\n") + AttackText;
		}
	}

	// Only on the host, where the controller exists.
	if (const ADinoAIControllerBase* Diag = Cast<ADinoAIControllerBase>(GetController()))
	{
		if (Awareness == EDinoAwareness::Searching)
		{
			Label += TEXT("\n") + Diag->DescribeSearchDiagnostics();
		}
		else if (Awareness == EDinoAwareness::Hunting)
		{
			Label += TEXT("\n") + Diag->DescribeChase();
		}
	}

	DrawDebugString(World,
		FVector(0.0f, 0.0f, HalfHeight + 120.0f),
		Label, this, Colour, DebugDrawLifetime, true);

	// Where it is actually trying to go. Only draws on the host, since AI controllers do not
	// replicate - but that is where the decision is made, so that is where it matters.
	//
	// Worth drawing permanently rather than as a one-off diagnostic: a destination that is
	// wrong looks identical to movement that is broken, and this tells the two apart at a
	// glance instead of by reading logs.
	if (const ADinoAIControllerBase* AI = Cast<ADinoAIControllerBase>(GetController()))
	{
		// Same sanity bound the controller uses, so a sentinel destination draws as nothing
		// rather than as a line to the horizon.
		auto IsDrawable = [](const FVector& P)
		{
			return !P.ContainsNaN() && P.GetAbsMax() < 1.0e9f && !P.IsNearlyZero();
		};

		// Where it is going: solid sphere, plus a line so the direction is unambiguous.
		const FVector Active = AI->GetActiveSearchDestination();
		if (IsDrawable(Active))
		{
			DrawDebugSphere(World, Active, 80.0f, 12, Colour, false, DebugDrawLifetime, 0, 3.0f);
			DrawDebugLine(World, GetActorLocation(), Active, Colour, false, DebugDrawLifetime, 0, 2.0f);
		}

		// Where it will go next: smaller and grey, so the prefetch is visible as a queued
		// candidate rather than mistaken for the current destination.
		const FVector Next = AI->GetNextSearchPoint();
		if (IsDrawable(Next) && !Next.Equals(Active, 1.0f))
		{
			DrawDebugSphere(World, Next, 40.0f, 8, FColor(110, 110, 110), false, DebugDrawLifetime, 0, 1.5f);
		}

		// What the search is centred on, and how wide it currently is.
		//
		// Drawn because "it keeps going back to the same area" is ambiguous without it: the
		// centre being stuck and the centre being correct-but-narrow look identical from the
		// destinations alone.
		if (Awareness == EDinoAwareness::Searching)
		{
			const FVector Centre = AI->GetLastKnownLocation();
			if (IsDrawable(Centre))
			{
				DrawDebugSphere(World, Centre, 60.0f, 12, FColor::Magenta, false, DebugDrawLifetime, 0, 3.0f);
				DrawDebugCircleArc(World, Centre, AI->GetSearchRadiusForDebug(), FVector::ForwardVector,
					PI, 32, FColor::Magenta, false, DebugDrawLifetime, 0, 2.0f);
				DrawDebugCircleArc(World, Centre, AI->GetSearchRadiusForDebug(), FVector::BackwardVector,
					PI, 32, FColor::Magenta, false, DebugDrawLifetime, 0, 2.0f);
			}

			// Where it thinks you went - the thing the EQS scoring pulls toward.
			const FVector Predicted = AI->GetPredictedTargetLocation();
			if (IsDrawable(Predicted) && !Predicted.Equals(Centre, 1.0f))
			{
				DrawDebugSphere(World, Predicted, 50.0f, 8, FColor::Yellow, false, DebugDrawLifetime, 0, 2.0f);
				DrawDebugLine(World, Centre, Predicted, FColor::Yellow, false, DebugDrawLifetime, 0, 1.5f);
			}
		}
	}
}
