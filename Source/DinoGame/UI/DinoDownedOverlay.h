#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DinoDownedOverlay.generated.h"

class UProgressBar;
class UTextBlock;

/**
 * Going down and reviving, on screen. For a downed player: that they are down, how long they
 * have (the bleed-out bar), and a revive bar while a teammate works on them. For a standing
 * player near a downed teammate: "Hold E to revive", that teammate's bleed-out, and the revive
 * bar while they hold it. Empty otherwise.
 *
 * Reads only replicated state (ADinoCharacter's vitals and revive state), so it is right on
 * every machine. Laid out in C++ like UDinoSpectatorOverlay; hit-test invisible.
 */
UCLASS()
class DINOGAME_API UDinoDownedOverlay : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	void Refresh();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DetailText;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> BleedBar;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> ReviveBar;

	FTimerHandle RefreshTimer;
};
