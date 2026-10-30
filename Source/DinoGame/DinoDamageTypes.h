#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "DinoDamageTypes.generated.h"

/**
 * What killed a player, carried the way Unreal already carries it: as the damage type class
 * passed to ApplyDamage, through TakeDamage, and into the player's status
 * (FDinoPlayerStatusState::Cause). No parallel list of causes to keep in step.
 *
 * Add a subclass per cause that should read differently in game; anything else is reported
 * as a plain death.
 */
UCLASS(Abstract)
class DINOGAME_API UDinoDamageType : public UDamageType
{
	GENERATED_BODY()

public:
	/** Completes "<player name> ..." in death messages, e.g. "was eaten". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Damage")
	FString DeathDescription = TEXT("died");

	/**
	 * When this damage takes a player's health to zero: down (they can be revived, and bleed
	 * out otherwise) rather than dead. Downed, further damage of this kind drains their bleed-out;
	 * damage that does not down kills them outright, down or not. See DinoHealthRules.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dino|Damage")
	bool bDownsInsteadOfKilling = false;

	/** Whether Cause downs rather than kills. False for anything that is not a Dino damage type. */
	static bool Downs(TSubclassOf<UDamageType> Cause);

	/** "<description>" for Cause, or "died" for a cause that is not a Dino damage type. */
	static FString DescribeDeath(TSubclassOf<UDamageType> Cause);

	/** A short name for logs: "Bite", "Devour", "Fall", or "Other". */
	static FString NameOf(TSubclassOf<UDamageType> Cause);
};

/**
 * A raptor's lunge bite, and the damage it keeps doing while the player is pinned. Downs rather
 * than kills - and a raptor that keeps biting a downed player finishes them.
 */
UCLASS()
class DINOGAME_API UDinoDamageType_Bite : public UDinoDamageType
{
	GENERATED_BODY()

public:
	UDinoDamageType_Bite();
};

/** Swallowed by a T-Rex. */
UCLASS()
class DINOGAME_API UDinoDamageType_Devour : public UDinoDamageType
{
	GENERATED_BODY()

public:
	UDinoDamageType_Devour();
};

/** Ran out of bleed-out while down, untended - or left down with nobody standing to help. */
UCLASS()
class DINOGAME_API UDinoDamageType_BleedOut : public UDinoDamageType
{
	GENERATED_BODY()

public:
	UDinoDamageType_BleedOut();
};

/** Fell below the level's Kill Z height (World Settings). See ADinoCharacter::FellOutOfWorld. */
UCLASS()
class DINOGAME_API UDinoDamageType_Fall : public UDinoDamageType
{
	GENERATED_BODY()

public:
	UDinoDamageType_Fall();
};
