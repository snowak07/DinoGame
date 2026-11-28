#include "World/DinoBush.h"

#include "AI/DinoNavAreas.h"
#include "Components/StaticMeshComponent.h"
#include "DinoGame.h"
#include "EngineUtils.h"

const FName ADinoBush::FoliageProfile(TEXT("Foliage"));

UDinoBushNavShape::UDinoBushNavShape()
{
	// Exported as an area modifier, not collision (UShapeComponent::GetNavigationData). Touches
	// nothing: it only has collision at all because navigation export reads the shape from it.
	bDynamicObstacle = true;
	bUseSystemDefaultObstacleAreaClass = false;
	AreaClassOverride = UDinoNavArea_Foliage::StaticClass();
	bCanEverAffectNavigation = true;
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetGenerateOverlapEvents(false);
	SetHiddenInGame(true);
}

ADinoBush::ADinoBush()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ADinoBush::BeginPlay()
{
	Super::BeginPlay();

	// Set here as well as in the Blueprint, so a bush can only be wrong by missing its leaves.
	// Not navigation geometry: leaves cut holes in the navmesh, and a player in a gap between
	// bushes narrower than a raptor could not be reached.
	TArray<UStaticMeshComponent*> Leaves;
	GetLeaves(Leaves);
	for (UStaticMeshComponent* Leaf : Leaves)
	{
		Leaf->SetCollisionProfileName(FoliageProfile);
		Leaf->SetCanEverAffectNavigation(false);
	}

	// Only the server has creatures, so only it paths. Added at runtime rather than as a default
	// component: a native root would take over from BP_Bush's own and move every placed bush.
	if (HasAuthority() && !Leaves.IsEmpty())
	{
		NavShape = NewObject<UDinoBushNavShape>(this, TEXT("FoliageNavArea"));
		NavShape->SetSphereRadius(GetRadius() + NavMargin, false);
		NavShape->SetWorldLocation(GetCentre());
		// This only re-marks the navmesh saved with the map (a modifier rebuild reuses its saved
		// layers); it cannot fill a hole built into it. If the self-check "Navmesh under bushes"
		// fails, the fix is Build Paths in the editor and save, not code.
		NavShape->RegisterComponent();
	}

	const FString Problem = DescribeSetupProblem();
	if (Problem.IsEmpty())
	{
		UE_LOG(LogDinoGame, Log, TEXT("Bush %s: %.1f m across at %s, %d leaf mesh(es)%s."), *GetName(), GetRadius() * 2.0f / 100.0f,
			*GetCentre().ToCompactString(), Leaves.Num(),
			NavShape ? *FString::Printf(TEXT(", foliage nav area %.1f m across"), NavShape->GetScaledSphereRadius() * 2.0f / 100.0f) : TEXT(""));
	}
	else
	{
		UE_LOG(LogDinoGame, Warning, TEXT("Bush %s is not set up right: %s."), *GetName(), *Problem);
	}
}

void ADinoBush::GetLeaves(TArray<UStaticMeshComponent*>& OutLeaves) const
{
	GetComponents<UStaticMeshComponent>(OutLeaves);
}

FBox ADinoBush::GetLeavesBox() const
{
	TArray<UStaticMeshComponent*> Leaves;
	GetLeaves(Leaves);

	FBox Box(ForceInit);
	for (const UStaticMeshComponent* Leaf : Leaves)
	{
		Box += Leaf->Bounds.GetBox();
	}
	return Box.IsValid ? Box : FBox::BuildAABB(GetActorLocation(), FVector(50.0f));
}

FVector ADinoBush::GetCentre() const
{
	return GetLeavesBox().GetCenter();
}

float ADinoBush::GetRadius() const
{
	const FVector Extent = GetLeavesBox().GetExtent();
	return FMath::Max(Extent.X, Extent.Y);
}

bool ADinoBush::Contains(const FVector& Point) const
{
	if (!GetLeavesBox().IsInside(Point))
	{
		return false;
	}

	TArray<UStaticMeshComponent*> Leaves;
	GetLeaves(Leaves);
	for (const UStaticMeshComponent* Leaf : Leaves)
	{
		// 0 means inside the collision. Below 0 the leaf has no simple collision to ask, and its
		// bounds stand in.
		FVector Closest;
		const float Distance = Leaf->IsCollisionEnabled() ? Leaf->GetDistanceToCollision(Point, Closest) : -1.0f;
		if (Distance == 0.0f || (Distance < 0.0f && Leaf->Bounds.GetBox().IsInside(Point)))
		{
			return true;
		}
	}
	return false;
}

bool ADinoBush::CoversFlat(const FVector& Point) const
{
	return FVector::Dist2D(Point, GetCentre()) <= GetRadius();
}

FVector ADinoBush::GetCheckSpot(const FVector& From, float CreatureRadius) const
{
	const FBox Box = GetLeavesBox();
	const FVector Centre = Box.GetCenter();

	FVector Away = (From - Centre).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = FVector::ForwardVector;
	}

	// A little clear of the leaves, so standing there is not standing in the bush.
	const FVector Spot = Centre + Away * (GetRadius() + CreatureRadius + 30.0f);
	return FVector(Spot.X, Spot.Y, Box.Min.Z);
}

ADinoBush* ADinoBush::FindContaining(const UWorld* World, const FVector& Point)
{
	for (TActorIterator<ADinoBush> It(const_cast<UWorld*>(World)); World && It; ++It)
	{
		if (It->Contains(Point))
		{
			return *It;
		}
	}
	return nullptr;
}

ADinoBush* ADinoBush::FindCovering(const UWorld* World, const FVector& Point)
{
	for (TActorIterator<ADinoBush> It(const_cast<UWorld*>(World)); World && It; ++It)
	{
		if (It->CoversFlat(Point))
		{
			return *It;
		}
	}
	return nullptr;
}

FString ADinoBush::DescribeSetupProblem() const
{
	TArray<UStaticMeshComponent*> Leaves;
	GetLeaves(Leaves);
	if (Leaves.IsEmpty())
	{
		return TEXT("no static mesh component for the leaves");
	}

	TArray<FString> Problems;
	for (const UStaticMeshComponent* Leaf : Leaves)
	{
		if (!Leaf->GetStaticMesh())
		{
			Problems.Add(FString::Printf(TEXT("%s has no mesh"), *Leaf->GetName()));
		}
		if (Leaf->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block)
		{
			Problems.Add(FString::Printf(TEXT("%s does not block Visibility, so it hides nobody (is the '%s' collision preset missing?)"),
				*Leaf->GetName(), *FoliageProfile.ToString()));
		}
		if (Leaf->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
		{
			Problems.Add(FString::Printf(TEXT("%s blocks pawns"), *Leaf->GetName()));
		}
		if (Leaf->CanEverAffectNavigation())
		{
			Problems.Add(FString::Printf(TEXT("%s cuts the navmesh"), *Leaf->GetName()));
		}
	}
	return FString::Join(Problems, TEXT("; "));
}
