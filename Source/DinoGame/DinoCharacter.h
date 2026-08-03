#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DinoCharacter.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDinoHealthChanged, float, NewHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDinoDied);

/**
 * Networked base for player characters. Intentionally declares no mesh or camera components:
 * BP_FirstPersonCharacter already owns those, so reparenting onto this class is non-destructive.
 *
 * Aim pitch replication is not implemented here because APawn already replicates it via
 * RemoteViewPitch16 and exposes it through GetBaseAimRotation().
 */
UCLASS()
class DINOGAME_API ADinoCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADinoCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintPure, Category = "Dino|Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "Dino|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Dino|Health")
	bool IsAlive() const { return CurrentHealth > 0.0f; }

	/** Server only. Clamped to [0, MaxHealth]. */
	UFUNCTION(BlueprintCallable, Category = "Dino|Health")
	void Heal(float Amount);

	/** Fires on every machine, including the server. */
	UPROPERTY(BlueprintAssignable, Category = "Dino|Health")
	FDinoHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Dino|Health")
	FDinoDied OnDied;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentHealth, BlueprintReadOnly, Category = "Dino|Health")
	float CurrentHealth = 100.0f;

	UFUNCTION()
	void OnRep_CurrentHealth(float OldHealth);

private:
	void ApplyHealthChange(float OldHealth);
};
