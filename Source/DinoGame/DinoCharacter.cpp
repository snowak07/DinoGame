#include "DinoCharacter.h"

#include "Components/SkeletalMeshComponent.h"
#include "DinoGame.h"
#include "Net/UnrealNetwork.h"

ADinoCharacter::ADinoCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	// The inherited mesh is the third-person body other players see. The owner sees only the
	// first-person arms, which live on the Blueprint.
	GetMesh()->SetOwnerNoSee(true);
}

void ADinoCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADinoCharacter, CurrentHealth);
}

void ADinoCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		CurrentHealth = MaxHealth;
	}
}

float ADinoCharacter::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	if (!HasAuthority() || ActualDamage <= 0.0f || !IsAlive())
	{
		return 0.0f;
	}

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Max(0.0f, CurrentHealth - ActualDamage);
	ApplyHealthChange(OldHealth);

	return ActualDamage;
}

void ADinoCharacter::Heal(float Amount)
{
	if (!HasAuthority() || Amount <= 0.0f || !IsAlive())
	{
		return;
	}

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Min(MaxHealth, CurrentHealth + Amount);
	ApplyHealthChange(OldHealth);
}

void ADinoCharacter::OnRep_CurrentHealth(float OldHealth)
{
	ApplyHealthChange(OldHealth);
}

void ADinoCharacter::ApplyHealthChange(float OldHealth)
{
	// Reached from OnRep on simulated proxies and called directly on the authority, since a
	// RepNotify never fires on the machine that wrote the value.
	OnHealthChanged.Broadcast(CurrentHealth, CurrentHealth - OldHealth);

	if (CurrentHealth <= 0.0f && OldHealth > 0.0f)
	{
		UE_LOG(LogDinoGame, Log, TEXT("%s died."), *GetName());
		OnDied.Broadcast();
	}
}
