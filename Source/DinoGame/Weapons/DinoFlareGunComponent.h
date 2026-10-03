#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DinoFlareGunComponent.generated.h"

class ADinoFlareProjectile;
class APlayerController;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;

/**
 * The player's flare gun. LMB (or right trigger) fires, at most once per FireInterval.
 *
 * The owning client asks, the server decides: the client sends where it is aiming, and the
 * server checks the cooldown and that the shooter is alive and free before spawning a
 * replicated flare. The client also checks the cooldown first, but only so a held-down button
 * does not flood the server with requests it will refuse.
 *
 * The flare therefore appears on the shooter's screen one round trip after the click. Fine at a
 * flare gun's fire rate; a rapid-fire weapon would want client-side prediction.
 *
 * Input is created in code at runtime, like the spectator controls, so there is no input asset
 * to make or assign.
 */
UCLASS(ClassGroup = (Dino), meta = (BlueprintSpawnableComponent))
class DINOGAME_API UDinoFlareGunComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDinoFlareGunComponent();

	/** Called by the owning character when it is set up for local control. */
	void BindInput(UEnhancedInputComponent* Input, APlayerController* PlayerController);

	/**
	 * Removes the fire mapping, so the key is free for whatever comes next - the spectator
	 * camera uses LMB too. Called on death; EndPlay covers everything else.
	 */
	void UnbindInput();

	/** Alive, not held by a creature, and off cooldown. */
	bool CanFire() const;

	float GetCooldownRemaining() const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Flare Gun")
	TSubclassOf<ADinoFlareProjectile> ProjectileClass;

	/** Minimum seconds between shots. Deliberately slow - every shot should count. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Flare Gun", meta = (ClampMin = "0.1"))
	float FireInterval = 1.5f;

	/**
	 * Damage per flare. Needs to clear a creature's stagger threshold to knock it off a pinned
	 * player - the raptor's is 10 - so this is the number to check when tuning either.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Flare Gun", meta = (ClampMin = "0.0"))
	float Damage = 25.0f;

	/** How far in front of the eyes the flare spawns, clear of the shooter's own capsule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Flare Gun", meta = (ClampMin = "0.0"))
	float MuzzleDistance = 80.0f;

private:
	void HandleFirePressed();

	UFUNCTION(Server, Reliable)
	void ServerFire(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction);

	double GetTimeSeconds() const;

	/** Server: when the last flare actually fired. Client: when the last request was sent. */
	double LastFireTime = -1000.0;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> FireContext;

	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
};
