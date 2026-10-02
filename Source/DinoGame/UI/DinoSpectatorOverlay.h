#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DinoSpectatorOverlay.generated.h"

class UTextBlock;

/**
 * What a spectator is looking at, and how to change it. Shown along the bottom of the screen
 * while spectating a round.
 *
 * Laid out in C++ for the same reason as UDinoSessionMenu. Hit-test invisible, so it never
 * eats a click meant for the camera or a menu.
 */
UCLASS()
class DINOGAME_API UDinoSpectatorOverlay : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	void Refresh();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ViewText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HintText;

	FTimerHandle RefreshTimer;
};
