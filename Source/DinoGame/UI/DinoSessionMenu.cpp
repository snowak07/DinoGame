#include "UI/DinoSessionMenu.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "DinoGameState.h"
#include "DinoPlayerController.h"
#include "DinoPlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Online/DinoSessionSubsystem.h"
#include "TimerManager.h"

namespace
{
	constexpr float RefreshInterval = 0.25f;
	constexpr float ButtonWidth = 280.0f;
}

// --- Layout ----------------------------------------------------------------------------------

void UDinoSessionMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	BuildLayout();
}

void UDinoSessionMenu::BuildLayout()
{
	// Built once. A native widget gets an empty tree in Initialize; a second build would stack
	// duplicate buttons on top of the first.
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	// The backdrop fills the screen, dims the game behind it, and centres everything.
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.65f));
	Backdrop->SetHorizontalAlignment(HAlign_Center);
	Backdrop->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Backdrop;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Backdrop->SetContent(Column);

	TitleText = AddText(Column, 34, FLinearColor::White);
	InfoText = AddText(Column, 18, FLinearColor(0.85f, 0.85f, 0.85f));
	PlayersText = AddText(Column, 16, FLinearColor(0.7f, 0.9f, 0.7f));

	StartButton = AddButton(Column, TEXT("Start Round"));
	RestartButton = AddButton(Column, TEXT("Restart Round"));
	LobbyButton = AddButton(Column, TEXT("Back to Lobby"));
	ResumeButton = AddButton(Column, TEXT("Resume"));
	LeaveButton = AddButton(Column, TEXT("Leave Session"));

	StartButton->OnClicked.AddDynamic(this, &UDinoSessionMenu::HandleStartClicked);
	RestartButton->OnClicked.AddDynamic(this, &UDinoSessionMenu::HandleRestartClicked);
	LobbyButton->OnClicked.AddDynamic(this, &UDinoSessionMenu::HandleLobbyClicked);
	ResumeButton->OnClicked.AddDynamic(this, &UDinoSessionMenu::HandleResumeClicked);
	LeaveButton->OnClicked.AddDynamic(this, &UDinoSessionMenu::HandleLeaveClicked);
}

UTextBlock* UDinoSessionMenu::AddText(UPanelWidget* Parent, int32 FontSize, const FLinearColor& Colour)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());

	FSlateFontInfo Font = Text->GetFont();
	Font.Size = FontSize;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Colour));
	Text->SetJustification(ETextJustify::Center);

	if (UVerticalBoxSlot* BoxSlot = Cast<UVerticalBoxSlot>(Parent->AddChild(Text)))
	{
		BoxSlot->SetHorizontalAlignment(HAlign_Center);
		BoxSlot->SetPadding(FMargin(0.0f, 6.0f));
	}

	return Text;
}

UButton* UDinoSessionMenu::AddButton(UPanelWidget* Parent, const FString& Label)
{
	// A fixed width so the column reads as one list rather than buttons sized to their words.
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Size->SetWidthOverride(ButtonWidth);

	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Size->SetContent(Button);

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Label));
	Text->SetJustification(ETextJustify::Center);

	// Dark on the default light button. The engine default is white text, which is close to
	// unreadable on the default button style.
	Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.05f, 0.05f, 0.05f)));
	Button->SetContent(Text);

	if (UVerticalBoxSlot* BoxSlot = Cast<UVerticalBoxSlot>(Parent->AddChild(Size)))
	{
		BoxSlot->SetHorizontalAlignment(HAlign_Center);
		BoxSlot->SetPadding(FMargin(0.0f, 4.0f));
	}

	return Button;
}

// --- Lifetime --------------------------------------------------------------------------------

void UDinoSessionMenu::NativeConstruct()
{
	Super::NativeConstruct();

	// Without this the widget never receives key events, so Escape and Tab would do nothing.
	SetIsFocusable(true);

	Refresh();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer,
			FTimerDelegate::CreateUObject(this, &UDinoSessionMenu::Refresh), RefreshInterval, true);
	}
}

void UDinoSessionMenu::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}

	Super::NativeDestruct();
}

void UDinoSessionMenu::Open()
{
	if (!IsInViewport())
	{
		AddToViewport(10);
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		// Without a cursor and UI input the buttons render but cannot be clicked, which reads as
		// a broken menu rather than a missing input mode.
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(true);
	}
}

void UDinoSessionMenu::Close()
{
	RemoveFromParent();

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
}

FReply UDinoSessionMenu::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::Tab)
	{
		Close();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// --- Content ---------------------------------------------------------------------------------

void UDinoSessionMenu::Refresh()
{
	const UWorld* World = GetWorld();
	const ADinoGameState* DinoState = World ? World->GetGameState<ADinoGameState>() : nullptr;
	const bool bHost = IsHost();

	auto Show = [](UWidget* Widget, bool bVisible)
	{
		if (Widget)
		{
			// The button's parent size box is what takes up space, so collapse that.
			UWidget* Target = Widget->GetParent() ? static_cast<UWidget*>(Widget->GetParent()) : Widget;
			Target->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	};

	if (!DinoState)
	{
		// A client in the moment between arriving and the game state replicating.
		TitleText->SetText(FText::FromString(TEXT("Connecting...")));
		InfoText->SetText(FText::GetEmpty());
		PlayersText->SetText(FText::GetEmpty());
		Show(StartButton, false);
		Show(RestartButton, false);
		Show(LobbyButton, false);
		Show(ResumeButton, true);
		Show(LeaveButton, true);
		return;
	}

	const bool bLobby = DinoState->IsInLobby();
	const bool bRound = DinoState->IsRoundInProgress();
	const bool bOver = DinoState->IsRoundOver();

	FString Title;
	FString Info;
	if (bLobby)
	{
		Title = TEXT("Lobby");
		Info = bHost ? TEXT("Start the round when everyone is in.") : TEXT("Waiting for the host to start the round.");
	}
	else if (bRound)
	{
		Title = TEXT("Round in progress");
		Info = FString::Printf(TEXT("%d of %d alive"), DinoState->GetAlivePlayerCount(), DinoState->PlayerArray.Num());
	}
	else if (bOver)
	{
		Title = TEXT("Everyone is dead");
		Info = bHost ? TEXT("Restart for another round, or go back to the lobby.") : TEXT("Waiting for the host.");
	}
	else
	{
		Title = TEXT("Loading...");
	}

	const FString Code = DinoState->GetJoinCode();
	Info += Code.IsEmpty() ? TEXT("\nSolo - no session") : FString::Printf(TEXT("\nJoin code: %s"), *Code);

	FString Players;
	for (const APlayerState* State : DinoState->PlayerArray)
	{
		const ADinoPlayerState* DinoPlayer = Cast<ADinoPlayerState>(State);
		if (!DinoPlayer)
		{
			continue;
		}

		// "out" rather than "dead": a player who joined mid-round is not alive either, and has
		// not died. Both are simply out of this round.
		const TCHAR* Status = bLobby ? TEXT("") : (DinoPlayer->IsAlive() ? TEXT("  - alive") : TEXT("  - out"));
		Players += FString::Printf(TEXT("%s%s%s\n"),
			*DinoPlayer->GetPlayerName(), DinoPlayer->IsHost() ? TEXT(" (host)") : TEXT(""), Status);
	}

	TitleText->SetText(FText::FromString(Title));
	InfoText->SetText(FText::FromString(Info));
	PlayersText->SetText(FText::FromString(Players.TrimEnd()));

	Show(StartButton, bHost && bLobby);
	Show(RestartButton, bHost && (bRound || bOver));
	Show(LobbyButton, bHost && (bRound || bOver));
	Show(ResumeButton, true);
	Show(LeaveButton, true);
}

// --- Buttons ---------------------------------------------------------------------------------

void UDinoSessionMenu::HandleStartClicked()
{
	if (ADinoPlayerController* PC = GetDinoController())
	{
		PC->ServerStartRound();
	}
}

void UDinoSessionMenu::HandleRestartClicked()
{
	if (ADinoPlayerController* PC = GetDinoController())
	{
		PC->ServerRestartRound();
	}
}

void UDinoSessionMenu::HandleLobbyClicked()
{
	if (ADinoPlayerController* PC = GetDinoController())
	{
		PC->ServerReturnToLobby();
	}
}

void UDinoSessionMenu::HandleResumeClicked()
{
	Close();
}

void UDinoSessionMenu::HandleLeaveClicked()
{
	const UGameInstance* GameInstance = GetGameInstance();
	if (UDinoSessionSubsystem* Sessions = GameInstance ? GameInstance->GetSubsystem<UDinoSessionSubsystem>() : nullptr)
	{
		Sessions->ReturnToMainMenu();
	}
}

ADinoPlayerController* UDinoSessionMenu::GetDinoController() const
{
	return Cast<ADinoPlayerController>(GetOwningPlayer());
}

bool UDinoSessionMenu::IsHost() const
{
	const APlayerController* PC = GetOwningPlayer();
	return PC && PC->HasAuthority();
}
