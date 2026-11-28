#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Actor.h"
#include "DinoBush.generated.h"

class UStaticMeshComponent;

/**
 * The invisible shape that marks the ground under a bush as foliage for creature pathing
 * (UDinoNavArea_Foliage). A shape set up as a dynamic obstacle is exported to the navmesh as an
 * area rather than as collision, so it changes what a path costs without cutting a hole.
 */
UCLASS(NotBlueprintable)
class DINOGAME_API UDinoBushNavShape : public USphereComponent
{
	GENERATED_BODY()

public:
	UDinoBushNavShape();
};

/**
 * A bush players can hide in. BP_Bush derives from this and supplies the leaves: any static mesh
 * components it has. Everything else is here.
 *
 * - **Hides.** The leaves block the Visibility channel, which is what creature sight, the close
 *   sense and the noise system trace: from outside, a player in a bush is not seen or felt, and
 *   is heard at half range.
 * - **Lets everyone through.** Pawns pass through the leaves, and the leaves are not navigation
 *   geometry - they cut holes creatures could not reach players in.
 * - **Costs creatures a detour.** The ground under it is a foliage nav area, so a wandering or
 *   searching creature goes around (a chase goes straight through: UDinoNavFilter_Chase).
 *
 * How hidden players are found is ADinoAIControllerBase's: bumping into them, walking into their
 * bush, or stopping to check it (DinoTargetRules::FindsHiddenPlayer).
 */
UCLASS()
class DINOGAME_API ADinoBush : public AActor
{
	GENERATED_BODY()

public:
	ADinoBush();

	/** The collision preset the leaves use (Config/DefaultEngine.ini): blocks Visibility, ignores Pawn, Camera and Projectile. */
	static const FName FoliageProfile;

	/** Whether a point is inside the leaves' collision. A body is in the bush when its centre is. */
	bool Contains(const FVector& Point) const;

	/** Whether a point is within the bush's footprint, ignoring height. For spots on the ground. */
	bool CoversFlat(const FVector& Point) const;

	/** Middle of the leaves. */
	FVector GetCentre() const;

	/** Flat radius of the leaves. */
	float GetRadius() const;

	/** Bounds of all the leaves. */
	FBox GetLeavesBox() const;

	/** Where a creature of this radius stands to check the bush: just outside it, on the side facing From. */
	FVector GetCheckSpot(const FVector& From, float CreatureRadius) const;

	/** The bush whose leaves contain Point, or null. */
	static ADinoBush* FindContaining(const UWorld* World, const FVector& Point);

	/** The bush whose footprint covers Point, or null. */
	static ADinoBush* FindCovering(const UWorld* World, const FVector& Point);

	/** What is wrong with this bush's setup, or empty when it is right. For DinoSelfCheck. */
	FString DescribeSetupProblem() const;

protected:
	virtual void BeginPlay() override;

	/** Added to the leaves' radius for the foliage nav area, so a creature walking round passes the leaves rather than brushing through them. */
	UPROPERTY(EditDefaultsOnly, Category = "Dino|Bush", meta = (ClampMin = "0.0"))
	float NavMargin = 50.0f;

private:
	void GetLeaves(TArray<UStaticMeshComponent*>& OutLeaves) const;

	UPROPERTY(Transient)
	TObjectPtr<UDinoBushNavShape> NavShape;
};
