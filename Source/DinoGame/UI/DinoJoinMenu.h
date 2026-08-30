#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DinoJoinMenu.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UDinoSessionSubsystem;

/**
 * Host / join-by-code menu.
 *
 * All behaviour lives here; the Blueprint child supplies only layout. Every BindWidget member
 * below must exist in that Blueprint with a matching name, or the Blueprint fails to compile -
 * which is the point: a renamed button is caught in the editor rather than as a dead button
 * halfway through a playtest.
 */
UCLASS(Abstract)
class DINOGAME_API UDinoJoinMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Shows the menu and switches to UI input, so the player can actually click it. */
	UFUNCTION(BlueprintCallable, Category = "Dino|UI")
	void ShowMenu();

	/** Hides the menu and returns input to the game. */
	UFUNCTION(BlueprintCallable, Category = "Dino|UI")
	void HideMenu();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/**
	 * Closes the menu on Escape.
	 *
	 * The menu sets UI-only input while open, so a game input action cannot close it - the key
	 * never reaches the PlayerController. Handling it here is what stops the menu becoming a
	 * trap you can open but not leave.
	 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	// --- Widgets the Blueprint child must contain, by name --------------------------------

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> HostButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> JoinButton;

	/** Where the joiner types the code. Filtered as they type. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> CodeInput;

	/** Shows this player's own code once hosting. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CodeDisplay;

	/** One line of feedback: searching, failed, joined. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatusText;

	/** Optional - leave it out of the Blueprint and everything else still works. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> VersionText;

private:
	UFUNCTION()
	void HandleHostClicked();

	UFUNCTION()
	void HandleJoinClicked();

	UFUNCTION()
	void HandleCodeTextChanged(const FText& Text);

	UFUNCTION()
	void HandleHostComplete(bool bWasSuccessful);

	UFUNCTION()
	void HandleJoinComplete(bool bWasSuccessful);

	UDinoSessionSubsystem* GetSessionSubsystem() const;

	/** Greys out both buttons while an async host or join is in flight. */
	void SetBusy(bool bInBusy);

	void SetStatus(const FString& Message);

	/** True between a button press and its completion delegate. */
	bool bBusy = false;
};
