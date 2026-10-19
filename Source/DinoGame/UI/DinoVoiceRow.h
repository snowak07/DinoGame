#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DinoVoiceRow.generated.h"

class ADinoPlayerState;
class UButton;
class USlider;
class UTextBlock;

/**
 * One other player's line in the Tab menu's voice section: name, a mute button, and a volume
 * slider. Everything it changes is local to this machine - see UDinoVoiceSubsystem.
 *
 * Laid out in C++, like UDinoSessionMenu, which creates one per other player.
 */
UCLASS()
class DINOGAME_API UDinoVoiceRow : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPlayer(ADinoPlayerState* InPlayer);
	ADinoPlayerState* GetPlayer() const { return Player.Get(); }

	/** Name, talking marker and mute label, from current state. Called by the menu's refresh. */
	void Refresh();

protected:
	virtual void NativeOnInitialized() override;

private:
	UFUNCTION()
	void HandleMuteClicked();

	UFUNCTION()
	void HandleVolumeChanged(float Value);

	TWeakObjectPtr<ADinoPlayerState> Player;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> MuteButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MuteLabel;

	UPROPERTY(Transient)
	TObjectPtr<USlider> VolumeSlider;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> VolumeText;
};
