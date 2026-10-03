#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DinoFlareProjectile.generated.h"

class UPointLightComponent;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * A fired flare: a small red light on a shallow arc. Hurts the first creature it touches and
 * ends on anything solid.
 *
 * Built from engine content - the basic sphere and its material, tinted - so it needs no assets
 * of its own. Spawned on the server and replicated; only the server applies damage.
 *
 * Passes through players rather than stopping on them. The flare exists to knock a raptor off a
 * pinned teammate, and the teammate is lying directly under the raptor: a flare that stopped on
 * the first body it met would hit the person being rescued as often as the thing pinning them.
 */
UCLASS()
class DINOGAME_API ADinoFlareProjectile : public AActor
{
	GENERATED_BODY()

public:
	ADinoFlareProjectile();

	/** Set by the flare gun before the projectile finishes spawning. */
	void SetDamage(float NewDamage) { Damage = NewDamage; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dino|Flare")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dino|Flare")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dino|Flare")
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dino|Flare")
	TObjectPtr<UProjectileMovementComponent> Movement;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Flare")
	FLinearColor FlareColour = FLinearColor(1.0f, 0.08f, 0.04f);

	/** How long an unobstructed flare flies before burning out. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Flare", meta = (ClampMin = "0.1"))
	float Lifetime = 5.0f;

private:
	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** The projectile stopped on something solid. */
	UFUNCTION()
	void HandleStop(const FHitResult& ImpactResult);

	float Damage = 25.0f;

	/** Set on the first creature hit, so a flare passing through a capsule and a mesh hits once. */
	bool bSpent = false;
};
