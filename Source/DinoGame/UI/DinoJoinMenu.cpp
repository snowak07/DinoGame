#include "UI/DinoJoinMenu.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "DinoBuildInfo.h"
#include "DinoGame.h"
#include "Engine/GameInstance.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Online/DinoSessionSubsystem.h"

void UDinoJoinMenu::NativeConstruct()
{
	Super::NativeConstruct();

	if (HostButton)
	{
		HostButton->OnClicked.AddDynamic(this, &UDinoJoinMenu::HandleHostClicked);
	}

	if (JoinButton)
	{
		JoinButton->OnClicked.AddDynamic(this, &UDinoJoinMenu::HandleJoinClicked);
	}

	if (QuitButton)
	{
		QuitButton->OnClicked.AddDynamic(this, &UDinoJoinMenu::HandleQuitClicked);
	}

	if (CodeInput)
	{
		CodeInput->OnTextChanged.AddDynamic(this, &UDinoJoinMenu::HandleCodeTextChanged);
	}

	if (UDinoSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		Subsystem->OnHostComplete.AddDynamic(this, &UDinoJoinMenu::HandleHostComplete);
		Subsystem->OnJoinComplete.AddDynamic(this, &UDinoJoinMenu::HandleJoinComplete);
	}

	if (VersionText)
	{
		VersionText->SetText(FText::FromString(FString::Printf(TEXT("Build %s"), *UDinoBuildInfo::GetBuildVersion())));
	}

	// Reopening the menu mid-session should show the code you are already hosting with, not a
	// blank field.
	if (CodeDisplay)
	{
		const UDinoSessionSubsystem* Subsystem = GetSessionSubsystem();
		const FString Existing = Subsystem ? Subsystem->GetCurrentJoinCode() : FString();
		CodeDisplay->SetText(FText::FromString(Existing));
	}

	// Without this the widget never receives key events, so Escape would do nothing even with
	// SetWidgetToFocus pointing at it.
	SetIsFocusable(true);

	SetStatus(TEXT(""));
	SetBusy(false);
}

FReply UDinoJoinMenu::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && !bMainMenuMode)
	{
		HideMenu();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UDinoJoinMenu::NativeDestruct()
{
	// The subsystem outlives this widget, so a binding left behind would fire into freed memory
	// the next time anyone hosts.
	if (UDinoSessionSubsystem* Subsystem = GetSessionSubsystem())
	{
		Subsystem->OnHostComplete.RemoveDynamic(this, &UDinoJoinMenu::HandleHostComplete);
		Subsystem->OnJoinComplete.RemoveDynamic(this, &UDinoJoinMenu::HandleJoinComplete);
	}

	Super::NativeDestruct();
}

UDinoSessionSubsystem* UDinoJoinMenu::GetSessionSubsystem() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UDinoSessionSubsystem>() : nullptr;
}

void UDinoJoinMenu::ShowMenu()
{
	AddToViewport();
	SetVisibility(ESlateVisibility::Visible);

	if (APlayerController* PC = GetOwningPlayer())
	{
		// Without a cursor and UI input mode the buttons render but cannot be clicked, which
		// reads as a broken menu rather than a missing input mode.
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(true);
	}
}

void UDinoJoinMenu::HideMenu()
{
	SetVisibility(ESlateVisibility::Collapsed);
	RemoveFromParent();

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
}

void UDinoJoinMenu::SetBusy(bool bInBusy)
{
	bBusy = bInBusy;

	if (HostButton)
	{
		HostButton->SetIsEnabled(!bInBusy);
	}

	if (JoinButton)
	{
		// Also gated on a complete code, so Join cannot be pressed against a half-typed one.
		const bool bCodeReady = CodeInput && UDinoSessionSubsystem::NormaliseJoinCode(CodeInput->GetText().ToString()).Len() == 6;
		JoinButton->SetIsEnabled(!bInBusy && bCodeReady);
	}
}

void UDinoJoinMenu::SetStatus(const FString& Message)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Message));
	}

	if (!Message.IsEmpty())
	{
		UE_LOG(LogDinoNet, Log, TEXT("Join menu: %s"), *Message);
	}
}

void UDinoJoinMenu::HandleCodeTextChanged(const FText& Text)
{
	if (!CodeInput)
	{
		return;
	}

	// Normalise in place so the field always shows exactly what will be searched for. Only
	// write back when it actually differs, since SetText re-enters this handler.
	const FString Raw = Text.ToString();
	const FString Clean = UDinoSessionSubsystem::NormaliseJoinCode(Raw).Left(6);

	if (Clean != Raw)
	{
		CodeInput->SetText(FText::FromString(Clean));
	}

	SetBusy(bBusy);
}

void UDinoJoinMenu::HandleHostClicked()
{
	UDinoSessionSubsystem* Subsystem = GetSessionSubsystem();
	if (!Subsystem)
	{
		SetStatus(TEXT("Session system unavailable."));
		return;
	}

	SetBusy(true);
	SetStatus(TEXT("Creating session..."));

	// Empty map name means the configured gameplay map; HostSession resolves it.
	Subsystem->HostSession(FString(), 4, false);
}

void UDinoJoinMenu::HandleQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UDinoJoinMenu::HandleJoinClicked()
{
	UDinoSessionSubsystem* Subsystem = GetSessionSubsystem();
	if (!Subsystem || !CodeInput)
	{
		SetStatus(TEXT("Session system unavailable."));
		return;
	}

	const FString Code = UDinoSessionSubsystem::NormaliseJoinCode(CodeInput->GetText().ToString());

	SetBusy(true);
	SetStatus(FString::Printf(TEXT("Looking for %s..."), *Code));

	Subsystem->JoinByCode(Code);
}

void UDinoJoinMenu::HandleHostComplete(bool bWasSuccessful)
{
	SetBusy(false);

	if (!bWasSuccessful)
	{
		SetStatus(TEXT("Could not create a session. Is Steam running?"));
		return;
	}

	const UDinoSessionSubsystem* Subsystem = GetSessionSubsystem();
	const FString Code = Subsystem ? Subsystem->GetCurrentJoinCode() : FString();

	if (CodeDisplay)
	{
		CodeDisplay->SetText(FText::FromString(Code));
	}

	SetStatus(TEXT("Hosting. Share the code above."));

	// The host travels to the listen-server map immediately after this, which tears the menu
	// down anyway - but hide explicitly so input returns to the game either way.
	HideMenu();
}

void UDinoJoinMenu::HandleJoinComplete(bool bWasSuccessful)
{
	SetBusy(false);

	if (!bWasSuccessful)
	{
		// Deliberately vague about which of the two it was: the subsystem already put the
		// specific reason on screen, and the common case is simply a mistyped code.
		SetStatus(TEXT("No session found with that code."));
		return;
	}

	SetStatus(TEXT("Joining..."));
	HideMenu();
}
