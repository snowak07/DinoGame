#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpectatorPawn.h"
#include "DinoSpectatorPawn.generated.h"

class ADinoCharacter;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

UENUM(BlueprintType)
enum class EDinoSpectateMode : uint8
{
	/** Orbits a living teammate. */
	Follow		UMETA(DisplayName = "Follow"),

	/** Flies freely. */
	Free		UMETA(DisplayName = "Free"),

	/** Fixed view looking down over the action. */
	Overhead	UMETA(DisplayName = "Overhead")
};

/**
 * The camera a dead, lobby, or late-joining player watches through.
 *
 * Spawned by the engine's spectating state on the owning client only, never replicated (see
 * APlayerController::SpawnSpectatorPawn). Everything here is local presentation: the server
 * neither knows nor cares where a spectator is looking.
 *
 * Controls are built in code at runtime - input actions and a mapping context created here -
 * so spectating needs no input assets. The engine's own spectator pawn binds legacy input axes,
 * which Enhanced Input ignores, so its fly controls are switched off and replaced.
 *
 * LMB / RMB  next / previous teammate (Follow)
 * Space      cycle Follow -> Free -> Overhead
 * WASD, Q/E  fly (Free)
 * Mouse      orbit (Follow) or look (Free)
 */
UCLASS()
class DINOGAME_API ADinoSpectatorPawn : public ASpectatorPawn
{
	GENERATED_BODY()

public:
	ADinoSpectatorPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UFUNCTION(BlueprintPure, Category = "Dino|Spectate")
	EDinoSpectateMode GetMode() const { return Mode; }

	UFUNCTION(BlueprintCallable, Category = "Dino|Spectate")
	void SetMode(EDinoSpectateMode NewMode);

	/** "Spectating Sam" / "Free camera" / "Overhead view", for the overlay. */
	FString DescribeView() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Distance from the followed player's head to the camera. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Spectate", meta = (ClampMin = "50.0"))
	float OrbitDistance = 450.0f;

	/** Height above the followed player's centre that the camera orbits around. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Spectate")
	float OrbitPivotHeight = 70.0f;

	/** Fallback overhead height when the level has no actor tagged DinoOverhead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Spectate", meta = (ClampMin = "100.0"))
	float OverheadHeight = 3500.0f;

	/** Fallback overhead pitch. -90 is straight down. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Spectate", meta = (ClampMin = "-90.0", ClampMax = "-10.0"))
	float OverheadPitch = -75.0f;

private:
	void CreateInput();

	void HandleLook(const FInputActionValue& Value);
	void HandleMoveForward(const FInputActionValue& Value);
	void HandleMoveRight(const FInputActionValue& Value);
	void HandleRise(const FInputActionValue& Value);
	void HandleNextTarget();
	void HandlePreviousTarget();
	void HandleCycleMode();

	void CycleTarget(int32 Direction);
	void GatherTargets(TArray<ADinoCharacter*>& OutTargets) const;
	bool HasAnyTarget() const;

	void UpdateFollow();
	void UpdateOverhead();

	/**
	 * Where the overhead view sits. An actor tagged DinoOverhead wins - a designer placing a
	 * Camera Actor is the way to get a deliberate shot. Otherwise high above the middle of every
	 * player and creature, which works on any map with no setup.
	 */
	bool ResolveOverhead(FVector& OutLocation, FRotator& OutRotation) const;

	EDinoSpectateMode Mode = EDinoSpectateMode::Overhead;
	TWeakObjectPtr<ADinoCharacter> FollowTarget;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> InputContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveForwardAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveRightAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RiseAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> NextTargetAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PreviousTargetAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CycleModeAction;

	/** Kept so EndPlay removes the context even if the controller is already gone by then. */
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;

	mutable TWeakObjectPtr<AActor> OverheadActor;
	mutable bool bSearchedForOverheadActor = false;
};
