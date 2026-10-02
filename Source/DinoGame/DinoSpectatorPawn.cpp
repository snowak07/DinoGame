#include "DinoSpectatorPawn.h"

#include "AI/DinoCreature.h"
#include "DinoCharacter.h"
#include "DinoGame.h"
#include "DinoGameState.h"
#include "EngineUtils.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
	/** Tag a designer puts on an actor (normally a Camera Actor) to set the overhead shot. */
	const FName OverheadTag(TEXT("DinoOverhead"));

	/**
	 * Above the template's default contexts, so Space and the mouse buttons reach these actions
	 * rather than Jump and friends. The actions consume their keys, so the lower-priority
	 * mappings for the same keys never fire while spectating.
	 */
	constexpr int32 SpectateInputPriority = 10;

	/** Radius of the probe that stops the orbit camera passing through walls. */
	constexpr float CameraProbeRadius = 12.0f;
}

ADinoSpectatorPawn::ADinoSpectatorPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// The engine's defaults are legacy axis bindings, which do nothing under Enhanced Input.
	// Replaced in SetupPlayerInputComponent.
	bAddDefaultMovementBindings = false;
}

void ADinoSpectatorPawn::BeginPlay()
{
	Super::BeginPlay();

	// Follow when there is someone alive to follow and a round is on; the lobby and an empty
	// round get the overview.
	const ADinoGameState* DinoState = GetWorld() ? GetWorld()->GetGameState<ADinoGameState>() : nullptr;
	const bool bRoundOn = DinoState && DinoState->IsRoundInProgress();
	SetMode(bRoundOn && HasAnyTarget() ? EDinoSpectateMode::Follow : EDinoSpectateMode::Overhead);
}

void ADinoSpectatorPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Left in place, the context would keep eating Space and the mouse buttons after the next
	// round hands this player a character.
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = InputSubsystem.Get())
	{
		Subsystem->RemoveMappingContext(InputContext);
	}

	Super::EndPlay(EndPlayReason);
}

// --- Input -----------------------------------------------------------------------------------

void ADinoSpectatorPawn::CreateInput()
{
	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		return Action;
	};

	LookAction = MakeAction(TEXT("SpectateLook"), EInputActionValueType::Axis2D);
	MoveForwardAction = MakeAction(TEXT("SpectateMoveForward"), EInputActionValueType::Axis1D);
	MoveRightAction = MakeAction(TEXT("SpectateMoveRight"), EInputActionValueType::Axis1D);
	RiseAction = MakeAction(TEXT("SpectateRise"), EInputActionValueType::Axis1D);
	NextTargetAction = MakeAction(TEXT("SpectateNext"), EInputActionValueType::Boolean);
	PreviousTargetAction = MakeAction(TEXT("SpectatePrevious"), EInputActionValueType::Boolean);
	CycleModeAction = MakeAction(TEXT("SpectateMode"), EInputActionValueType::Boolean);

	InputContext = NewObject<UInputMappingContext>(this, TEXT("SpectateContext"));

	auto MapNegated = [this](UInputAction* Action, const FKey& Key)
	{
		FEnhancedActionKeyMapping& Mapping = InputContext->MapKey(Action, Key);
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(InputContext));
	};

	InputContext->MapKey(LookAction, EKeys::Mouse2D);

	InputContext->MapKey(MoveForwardAction, EKeys::W);
	MapNegated(MoveForwardAction, EKeys::S);
	InputContext->MapKey(MoveRightAction, EKeys::D);
	MapNegated(MoveRightAction, EKeys::A);
	InputContext->MapKey(RiseAction, EKeys::E);
	MapNegated(RiseAction, EKeys::Q);

	InputContext->MapKey(NextTargetAction, EKeys::LeftMouseButton);
	InputContext->MapKey(PreviousTargetAction, EKeys::RightMouseButton);
	InputContext->MapKey(CycleModeAction, EKeys::SpaceBar);
}

void ADinoSpectatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogDinoGame, Warning, TEXT("DinoSpectatorPawn: input component is not Enhanced Input; spectator controls disabled."));
		return;
	}

	CreateInput();

	const APlayerController* PC = Cast<APlayerController>(GetController());
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr)
	{
		Subsystem->AddMappingContext(InputContext, SpectateInputPriority);
		InputSubsystem = Subsystem;
	}

	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADinoSpectatorPawn::HandleLook);
	Input->BindAction(MoveForwardAction, ETriggerEvent::Triggered, this, &ADinoSpectatorPawn::HandleMoveForward);
	Input->BindAction(MoveRightAction, ETriggerEvent::Triggered, this, &ADinoSpectatorPawn::HandleMoveRight);
	Input->BindAction(RiseAction, ETriggerEvent::Triggered, this, &ADinoSpectatorPawn::HandleRise);
	Input->BindAction(NextTargetAction, ETriggerEvent::Started, this, &ADinoSpectatorPawn::HandleNextTarget);
	Input->BindAction(PreviousTargetAction, ETriggerEvent::Started, this, &ADinoSpectatorPawn::HandlePreviousTarget);
	Input->BindAction(CycleModeAction, ETriggerEvent::Started, this, &ADinoSpectatorPawn::HandleCycleMode);
}

void ADinoSpectatorPawn::HandleLook(const FInputActionValue& Value)
{
	// The overhead shot is fixed on purpose: a static view is the point of it.
	if (Mode == EDinoSpectateMode::Overhead)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);

	// Negated to match the template character's own look, so mouse-up means look-up here too.
	AddControllerPitchInput(-Axis.Y);
}

void ADinoSpectatorPawn::HandleMoveForward(const FInputActionValue& Value)
{
	if (Mode == EDinoSpectateMode::Free && Controller)
	{
		AddMovementInput(Controller->GetControlRotation().Vector(), Value.Get<float>());
	}
}

void ADinoSpectatorPawn::HandleMoveRight(const FInputActionValue& Value)
{
	if (Mode == EDinoSpectateMode::Free && Controller)
	{
		AddMovementInput(FRotationMatrix(Controller->GetControlRotation()).GetScaledAxis(EAxis::Y), Value.Get<float>());
	}
}

void ADinoSpectatorPawn::HandleRise(const FInputActionValue& Value)
{
	if (Mode == EDinoSpectateMode::Free)
	{
		AddMovementInput(FVector::UpVector, Value.Get<float>());
	}
}

void ADinoSpectatorPawn::HandleNextTarget()
{
	CycleTarget(+1);
}

void ADinoSpectatorPawn::HandlePreviousTarget()
{
	CycleTarget(-1);
}

void ADinoSpectatorPawn::HandleCycleMode()
{
	switch (Mode)
	{
	case EDinoSpectateMode::Follow:   SetMode(EDinoSpectateMode::Free); break;
	case EDinoSpectateMode::Free:     SetMode(EDinoSpectateMode::Overhead); break;
	case EDinoSpectateMode::Overhead: SetMode(HasAnyTarget() ? EDinoSpectateMode::Follow : EDinoSpectateMode::Free); break;
	}
}

// --- Modes -----------------------------------------------------------------------------------

void ADinoSpectatorPawn::SetMode(EDinoSpectateMode NewMode)
{
	if (NewMode == EDinoSpectateMode::Follow && !FollowTarget.IsValid())
	{
		CycleTarget(+1);
		if (!FollowTarget.IsValid())
		{
			NewMode = EDinoSpectateMode::Overhead;
		}
	}

	Mode = NewMode;

	// Free flight starts from wherever the camera already is, so switching to it never jumps.
	// The overhead shot points the controller at its own rotation once, here, because look input
	// is ignored in that mode and nothing else would.
	if (Mode == EDinoSpectateMode::Overhead)
	{
		FVector Location;
		FRotator Rotation;
		if (ResolveOverhead(Location, Rotation))
		{
			SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			if (Controller)
			{
				Controller->SetControlRotation(Rotation);
			}
		}
	}
}

void ADinoSpectatorPawn::GatherTargets(TArray<ADinoCharacter*>& OutTargets) const
{
	OutTargets.Reset();

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ADinoCharacter> It(World); It; ++It)
	{
		if (It->IsAlive())
		{
			OutTargets.Add(*It);
		}
	}

	// A stable order, so LMB and RMB walk the same list in opposite directions rather than
	// jumping about as actors are iterated in whatever order the world holds them.
	OutTargets.Sort([](const ADinoCharacter& A, const ADinoCharacter& B)
	{
		const APlayerState* StateA = A.GetPlayerState();
		const APlayerState* StateB = B.GetPlayerState();
		return (StateA ? StateA->GetPlayerName() : A.GetName()) < (StateB ? StateB->GetPlayerName() : B.GetName());
	});
}

bool ADinoSpectatorPawn::HasAnyTarget() const
{
	TArray<ADinoCharacter*> Targets;
	GatherTargets(Targets);
	return Targets.Num() > 0;
}

void ADinoSpectatorPawn::CycleTarget(int32 Direction)
{
	TArray<ADinoCharacter*> Targets;
	GatherTargets(Targets);

	if (Targets.Num() == 0)
	{
		FollowTarget.Reset();
		return;
	}

	const int32 Current = Targets.IndexOfByKey(FollowTarget.Get());
	const int32 Next = Current == INDEX_NONE
		? 0
		: (Current + Direction + Targets.Num()) % Targets.Num();

	FollowTarget = Targets[Next];

	// Clicking a teammate is a request to watch them, whatever mode the camera was in.
	if (Mode != EDinoSpectateMode::Follow)
	{
		Mode = EDinoSpectateMode::Follow;
	}
}

void ADinoSpectatorPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Mode == EDinoSpectateMode::Follow)
	{
		UpdateFollow();
	}
	else if (Mode == EDinoSpectateMode::Overhead)
	{
		UpdateOverhead();
	}
}

void ADinoSpectatorPawn::UpdateFollow()
{
	ADinoCharacter* Target = FollowTarget.Get();
	if (!Target || !Target->IsAlive())
	{
		// The player being watched died. Move on to the next, or to the overview if nobody is
		// left - which is also what the round-over screen will be showing behind its menu.
		FollowTarget.Reset();
		CycleTarget(+1);
		if (!FollowTarget.IsValid())
		{
			SetMode(EDinoSpectateMode::Overhead);
		}
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || !Controller)
	{
		return;
	}

	const FRotator View = Controller->GetControlRotation();
	const FVector Pivot = Target->GetActorLocation() + FVector(0.0f, 0.0f, OrbitPivotHeight);
	FVector CameraLocation = Pivot - View.Vector() * OrbitDistance;

	// Swept from the player outwards, so a wall behind them pulls the camera in rather than
	// letting it see through.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DinoSpectateCamera), false, this);
	Params.AddIgnoredActor(Target);

	FHitResult Hit;
	if (World->SweepSingleByChannel(Hit, Pivot, CameraLocation, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(CameraProbeRadius), Params))
	{
		CameraLocation = Hit.Location;
	}

	// The view comes from this pawn's location plus the control rotation (BaseEyeHeight is 0
	// on a spectator pawn), so placing the pawn is placing the camera.
	SetActorLocation(CameraLocation, false, nullptr, ETeleportType::TeleportPhysics);
}

void ADinoSpectatorPawn::UpdateOverhead()
{
	// Re-applied every frame: the fallback shot tracks the action as it moves, and a placed
	// camera costs nothing to re-read once it has been found.
	FVector Location;
	FRotator Rotation;
	if (ResolveOverhead(Location, Rotation))
	{
		SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		if (Controller)
		{
			Controller->SetControlRotation(Rotation);
		}
	}
}

bool ADinoSpectatorPawn::ResolveOverhead(FVector& OutLocation, FRotator& OutRotation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Searched once: walking every actor in the world each frame to find one tag would be the
	// most expensive thing a spectator does.
	if (!bSearchedForOverheadActor)
	{
		bSearchedForOverheadActor = true;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(OverheadTag))
			{
				OverheadActor = *It;
				break;
			}
		}
	}

	if (const AActor* Placed = OverheadActor.Get())
	{
		OutLocation = Placed->GetActorLocation();
		OutRotation = Placed->GetActorRotation();
		return true;
	}

	// No placed camera: centre over every player and creature, so the shot frames whatever is
	// happening rather than the world origin.
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (TActorIterator<ADinoCharacter> It(World); It; ++It)
	{
		Sum += It->GetActorLocation();
		++Count;
	}
	for (TActorIterator<ADinoCreature> It(World); It; ++It)
	{
		Sum += It->GetActorLocation();
		++Count;
	}

	if (Count == 0)
	{
		return false;
	}

	const FRotator Down(OverheadPitch, 0.0f, 0.0f);
	const FVector Centre = Sum / Count;

	// Pulled back along the view direction so a tilted shot still looks at the centre rather
	// than past it.
	OutLocation = Centre - Down.Vector() * (OverheadHeight / FMath::Max(0.2f, FMath::Abs(FMath::Sin(FMath::DegreesToRadians(OverheadPitch)))));
	OutRotation = Down;
	return true;
}

FString ADinoSpectatorPawn::DescribeView() const
{
	switch (Mode)
	{
	case EDinoSpectateMode::Follow:
	{
		const ADinoCharacter* Target = FollowTarget.Get();
		const APlayerState* State = Target ? Target->GetPlayerState() : nullptr;
		return FString::Printf(TEXT("Spectating %s"), State ? *State->GetPlayerName() : TEXT("..."));
	}
	case EDinoSpectateMode::Free:
		return TEXT("Free camera");
	case EDinoSpectateMode::Overhead:
	default:
		return TEXT("Overhead view");
	}
}
