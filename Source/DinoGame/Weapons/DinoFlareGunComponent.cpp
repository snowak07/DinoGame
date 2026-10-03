#include "Weapons/DinoFlareGunComponent.h"

#include "DinoCharacter.h"
#include "DinoGame.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Weapons/DinoFlareProjectile.h"

namespace
{
	/**
	 * Above the template's default contexts, below the spectator camera's. The two never coexist
	 * in practice - firing needs a living character, spectating needs a dead one - but the order
	 * is defined anyway in case a mapping is ever left behind.
	 */
	constexpr int32 FireInputPriority = 5;

	/**
	 * How far the client's claimed muzzle may be from the shooter's eyes on the server. Generous,
	 * since the two disagree by however far the shooter moved in a round trip; it exists only
	 * to stop a client spawning flares somewhere else entirely.
	 */
	constexpr float MaxMuzzleDiscrepancy = 400.0f;
}

UDinoFlareGunComponent::UDinoFlareGunComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Needed for the server RPC: an actor component can only send one if it replicates and its
	// owner belongs to the calling client, which a possessed player character does.
	SetIsReplicatedByDefault(true);

	ProjectileClass = ADinoFlareProjectile::StaticClass();
}

void UDinoFlareGunComponent::BindInput(UEnhancedInputComponent* Input, APlayerController* PlayerController)
{
	if (!Input || !PlayerController)
	{
		return;
	}

	FireAction = NewObject<UInputAction>(this, TEXT("FlareFire"));
	FireAction->ValueType = EInputActionValueType::Boolean;

	FireContext = NewObject<UInputMappingContext>(this, TEXT("FlareContext"));
	FireContext->MapKey(FireAction, EKeys::LeftMouseButton);
	FireContext->MapKey(FireAction, EKeys::Gamepad_RightTrigger);

	if (const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->AddMappingContext(FireContext, FireInputPriority);
			InputSubsystem = Subsystem;
		}
	}

	// Started, not Triggered: one flare per click. Holding the button does not auto-fire.
	Input->BindAction(FireAction, ETriggerEvent::Started, this, &UDinoFlareGunComponent::HandleFirePressed);
}

void UDinoFlareGunComponent::UnbindInput()
{
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = InputSubsystem.Get())
	{
		Subsystem->RemoveMappingContext(FireContext);
	}
	InputSubsystem.Reset();
}

void UDinoFlareGunComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindInput();

	Super::EndPlay(EndPlayReason);
}

bool UDinoFlareGunComponent::CanFire() const
{
	const ADinoCharacter* Shooter = Cast<ADinoCharacter>(GetOwner());
	return Shooter
		&& Shooter->IsAlive()
		&& !Shooter->IsRestrained()
		&& GetCooldownRemaining() <= 0.0f;
}

float UDinoFlareGunComponent::GetCooldownRemaining() const
{
	return FMath::Max(0.0f, static_cast<float>(LastFireTime + FireInterval - GetTimeSeconds()));
}

void UDinoFlareGunComponent::HandleFirePressed()
{
	if (!CanFire())
	{
		return;
	}

	const APawn* Shooter = Cast<APawn>(GetOwner());
	const APlayerController* PC = Shooter ? Cast<APlayerController>(Shooter->GetController()) : nullptr;
	if (!PC)
	{
		return;
	}

	// The camera's view, not the actor's: the flare goes where the crosshair points, which in
	// first person is the only aim the player can see.
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector Direction = ViewRotation.Vector();
	ServerFire(ViewLocation + Direction * MuzzleDistance, Direction);

	// Stamped after the request, and only on a remote client. On the listen-server host this
	// component *is* the server's: ServerFire has just run here and stamped it already, and
	// stamping it first would make the server see a cooldown and refuse the host's every shot.
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		LastFireTime = GetTimeSeconds();
	}
}

void UDinoFlareGunComponent::ServerFire_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction)
{
	// Re-checked here whatever the client thought. The client's own check is only courtesy.
	if (!CanFire())
	{
		return;
	}

	APawn* Shooter = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	if (!Shooter || !World || !ProjectileClass)
	{
		return;
	}

	// A client claiming to fire from across the map gets its flare moved back to its own eyes.
	FVector SpawnLocation = Origin;
	const FVector Eyes = Shooter->GetPawnViewLocation();
	if (FVector::Dist(SpawnLocation, Eyes) > MaxMuzzleDiscrepancy)
	{
		SpawnLocation = Eyes + Direction * MuzzleDistance;
	}

	LastFireTime = GetTimeSeconds();

	// Deferred so the damage is set before the flare can touch anything - including a creature
	// close enough to be overlapped on the very frame it spawns.
	const FTransform SpawnTransform(Direction.Rotation(), SpawnLocation);
	ADinoFlareProjectile* Flare = World->SpawnActorDeferred<ADinoFlareProjectile>(
		ProjectileClass, SpawnTransform, Shooter, Shooter, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Flare)
	{
		Flare->SetDamage(Damage);
		Flare->FinishSpawning(SpawnTransform);
	}
}

double UDinoFlareGunComponent::GetTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}
