#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "DinoPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDinoAliveStateChanged, bool, bIsAlive);

UCLASS()
class DINOGAME_API ADinoPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Dino|Multiplayer")
	bool IsAlive() const { return bIsAlive; }

	/** Server only. */
	UFUNCTION(BlueprintCallable, Category = "Dino|Multiplayer")
	void SetIsAlive(bool bNewIsAlive);

	/** Fires on every machine, including the server, whenever the alive state changes. */
	UPROPERTY(BlueprintAssignable, Category = "Dino|Multiplayer")
	FDinoAliveStateChanged OnAliveStateChanged;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_IsAlive, BlueprintReadOnly, Category = "Dino|Multiplayer")
	bool bIsAlive = true;

	UFUNCTION()
	void OnRep_IsAlive();
};
