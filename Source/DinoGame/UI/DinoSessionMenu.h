#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DinoSessionMenu.generated.h"

class ADinoPlayerController;
class UButton;
class UPanelWidget;
class UTextBlock;

/**
 * The in-session menu, on Tab: the lobby, a mid-round menu, and the round-over screen.
 *
 * Content follows the replicated match state rather than being told what to show, so every
 * machine agrees on it and nothing can leave it showing the wrong phase. It is refreshed a few
 * times a second from the game state - cheap, and immune to the ordering problems of
 * subscribing to replication events that may already have fired.
 *
 * Laid out entirely in C++ (WidgetTree->ConstructWidget), so there is no Blueprint to build or
 * keep in step. It looks plain on purpose. When a designed layout is wanted, the same handlers
 * can be driven from BindWidget members on a Blueprint child, as UDinoJoinMenu does, without the
 * behaviour here changing.
 *
 * Start Round, Restart Round and Back to Lobby show only for the host. The server checks again
 * regardless (see ADinoPlayerController's server RPCs): a hidden button is a convenience, not
 * the rule.
 */
UCLASS()
class DINOGAME_API UDinoSessionMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Adds the menu and switches to UI input, so its buttons can be clicked. */
	void Open();

	/** Removes the menu and returns input to the game. */
	void Close();

	bool IsOpen() const { return IsInViewport(); }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/**
	 * Escape and Tab both close it. The menu takes UI-only input while open, so the Tab key
	 * never reaches the PlayerController's menu action - without handling it here, Tab could
	 * open the menu but not close it.
	 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void BuildLayout();
	UTextBlock* AddText(UPanelWidget* Parent, int32 FontSize, const FLinearColor& Colour);
	UButton* AddButton(UPanelWidget* Parent, const FString& Label);

	void Refresh();

	UFUNCTION()
	void HandleStartClicked();

	UFUNCTION()
	void HandleRestartClicked();

	UFUNCTION()
	void HandleLobbyClicked();

	UFUNCTION()
	void HandleResumeClicked();

	UFUNCTION()
	void HandleLeaveClicked();

	ADinoPlayerController* GetDinoController() const;

	/** Standalone and the listen-server host both have authority over their own controller. */
	bool IsHost() const;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> InfoText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PlayersText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> StartButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RestartButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> LobbyButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ResumeButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> LeaveButton;

	FTimerHandle RefreshTimer;
};
