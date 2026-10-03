#include "Weapons/DinoFlareProjectile.h"

#include "AI/DinoCreature.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DinoGame.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ADinoFlareProjectile::ADinoFlareProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(true);

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(10.0f);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);

	// Overlap rather than block pawns, so it can pass through players (see the class comment)
	// and still report the creature it passes into.
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// Invisible to sight and camera traces. A flare in flight must not briefly break a
	// creature's line of sight or bump the spectator camera.
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Collision->SetGenerateOverlapEvents(true);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeScale3D(FVector(0.15f));

	// Engine content, referenced from C++ so it is cooked with the game - no asset to make or
	// assign. The sphere is 100 units across, hence the scale above.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
	}
	if (ShapeMaterial.Succeeded())
	{
		Mesh->SetMaterial(0, ShapeMaterial.Object);
	}

	// The light does most of the work of reading as a flare, and is what makes one visible in
	// the dark. No shadows: several flares in the air at once should stay cheap.
	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Collision);
	Glow->SetIntensity(6000.0f);
	Glow->SetAttenuationRadius(700.0f);
	Glow->SetCastShadows(false);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->InitialSpeed = 3500.0f;
	Movement->MaxSpeed = 3500.0f;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;

	// A little drop, so it reads as a fired flare rather than a laser, without making a
	// raptor at rescue distance hard to hit.
	Movement->ProjectileGravityScale = 0.25f;
}

void ADinoFlareProjectile::BeginPlay()
{
	Super::BeginPlay();

	SetLifeSpan(Lifetime);

	// Never the shooter, which it spawns just in front of.
	if (APawn* Shooter = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(Shooter, true);
	}

	// Colour applied per instance: BasicShapeMaterial exposes "Color", and a dynamic instance is
	// the only way to change it without an asset.
	if (UMaterialInstanceDynamic* Tint = Mesh->CreateDynamicMaterialInstance(0))
	{
		Tint->SetVectorParameterValue(TEXT("Color"), FlareColour);
	}
	Glow->SetLightColor(FlareColour);

	Collision->OnComponentBeginOverlap.AddDynamic(this, &ADinoFlareProjectile::HandleOverlap);
	Movement->OnProjectileStop.AddDynamic(this, &ADinoFlareProjectile::HandleStop);
}

void ADinoFlareProjectile::HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority() || bSpent)
	{
		return;
	}

	ADinoCreature* Creature = Cast<ADinoCreature>(OtherActor);
	if (!Creature)
	{
		// A player, or any other pawn. Pass through.
		return;
	}

	bSpent = true;

	// Through the ordinary damage path, so the creature's own vulnerability decides what a hit
	// means: a raptor staggers and drops whoever it is pinning, a T-Rex shrugs it off.
	const FVector Direction = Movement->Velocity.GetSafeNormal();
	UGameplayStatics::ApplyPointDamage(Creature, Damage, Direction, SweepResult,
		GetInstigatorController(), this, UDamageType::StaticClass());

	UE_LOG(LogDinoGame, Log, TEXT("Flare from %s hit %s for %.0f."),
		*GetNameSafe(GetInstigator()), *Creature->GetName(), Damage);

	Destroy();
}

void ADinoFlareProjectile::HandleStop(const FHitResult& ImpactResult)
{
	// Clients wait for the server's destroy to replicate, so the flare cannot vanish on one
	// machine and carry on flying on another.
	if (HasAuthority())
	{
		Destroy();
	}
}
