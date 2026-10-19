#include "UI/DinoSessionMenu.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "DinoGameState.h"
#include "DinoPlayerController.h"
#include "DinoPlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Online/DinoSessionSubsystem.h"
#include "Online/DinoVoiceSubsystem.h"
#include "TimerManager.h"
#include "UI/DinoVoiceRow.h"

namespace
{
	constexpr float RefreshInterval = 0.25f;
	constexpr float ButtonWidth = 280.0f;
	constexpr float VoiceColumnWidth = 520.0f;

	/** The mic picker's first entry. Picking it follows whatever Windows calls the default input. */
	const TCHAR* const WindowsDefaultMic = TEXT("Windows default");
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

	// Session on the left, voice on the right.
	UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Columns"));
	Backdrop->SetContent(Columns);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	if (UHorizontalBoxSlot* ColumnSlot = Cast<UHorizontalBoxSlot>(Columns->AddChild(Column)))
	{
		ColumnSlot->SetVerticalAlignment(VAlign_Center);
	}

	USpacer* Gap = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	Gap->SetSize(FVector2D(60.0f, 1.0f));
	Columns->AddChild(Gap);

	USizeBox* VoiceSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("VoiceColumnSize"));
	VoiceSize->SetWidthOverride(VoiceColumnWidth);
	if (UHorizontalBoxSlot* VoiceSlot = Cast<UHorizontalBoxSlot>(Columns->AddChild(VoiceSize)))
	{
		VoiceSlot->SetVerticalAlignment(VAlign_Center);
	}

	UVerticalBox* VoiceColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VoiceColumn"));
	VoiceSize->SetContent(VoiceColumn);
	BuildVoiceColumn(VoiceColumn);

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

void UDinoSessionMenu::BuildVoiceColumn(UPanelWidget* Parent)
{
	UTextBlock* Header = AddText(Parent, 24, FLinearColor::White);
	Header->SetText(FText::FromString(TEXT("Voice")));

	VoiceRowBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VoiceRows"));
	if (UVerticalBoxSlot* RowsSlot = Cast<UVerticalBoxSlot>(Parent->AddChild(VoiceRowBox)))
	{
		RowsSlot->SetHorizontalAlignment(HAlign_Center);
	}

	NoOtherPlayersText = AddText(Parent, 14, FLinearColor(0.6f, 0.6f, 0.6f));
	NoOtherPlayersText->SetText(FText::FromString(TEXT("Nobody else is here yet.")));

	UTextBlock* MicHeader = AddText(Parent, 16, FLinearColor(0.85f, 0.85f, 0.85f));
	MicHeader->SetText(FText::FromString(TEXT("Microphone")));

	USizeBox* PickerSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	PickerSize->SetWidthOverride(ButtonWidth + 80.0f);
	MicPicker = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("MicPicker"));
	PickerSize->SetContent(MicPicker);
	if (UVerticalBoxSlot* PickerSlot = Cast<UVerticalBoxSlot>(Parent->AddChild(PickerSize)))
	{
		PickerSlot->SetHorizontalAlignment(HAlign_Center);
		PickerSlot->SetPadding(FMargin(0.0f, 4.0f));
	}
	MicPicker->OnSelectionChanged.AddDynamic(this, &UDinoSessionMenu::HandleMicPicked);

	UTextBlock* SensitivityHeader = AddText(Parent, 14, FLinearColor(0.85f, 0.85f, 0.85f));
	SensitivityHeader->SetText(FText::FromString(TEXT("Mic sensitivity  (quiet mic or cut-off words: raise it)")));

	USizeBox* SliderSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	SliderSize->SetWidthOverride(ButtonWidth + 80.0f);
	SensitivitySlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("MicSensitivity"));
	SensitivitySlider->SetMinValue(0.0f);
	SensitivitySlider->SetMaxValue(1.0f);
	SensitivitySlider->SetStepSize(0.02f);
	SliderSize->SetContent(SensitivitySlider);
	if (UVerticalBoxSlot* SliderSlot = Cast<UVerticalBoxSlot>(Parent->AddChild(SliderSize)))
	{
		SliderSlot->SetHorizontalAlignment(HAlign_Center);
		SliderSlot->SetPadding(FMargin(0.0f, 4.0f));
	}
	SensitivitySlider->OnValueChanged.AddDynamic(this, &UDinoSessionMenu::HandleSensitivityChanged);

	MicLevelText = AddText(Parent, 14, FLinearColor(0.7f, 0.7f, 0.7f));
	MicNoticeText = AddText(Parent, 14, FLinearColor(0.95f, 0.7f, 0.3f));
	MicNoticeText->SetAutoWrapText(true);

	ReconnectMicButton = AddButton(Parent, TEXT("Reconnect mic"));
	ReconnectMicButton->OnClicked.AddDynamic(this, &UDinoSessionMenu::HandleReconnectMicClicked);
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

	FillMicPicker();
	if (const UDinoVoiceSubsystem* Voice = GetVoiceSubsystem())
	{
		// Set here rather than every refresh, which would fight a slider being dragged.
		SensitivitySlider->SetValue(Voice->GetMicSensitivity());
	}

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
	RefreshVoice();

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

		// From the player status, so this list can never disagree with spectating or the round
		// end. Lobby during a round is someone who joined after it started: watching, not dead.
		const TCHAR* Status = TEXT("");
		if (!bLobby)
		{
			switch (DinoPlayer->GetStatus())
			{
			case EDinoPlayerStatus::Alive:  Status = TEXT("  - alive"); break;
			case EDinoPlayerStatus::Downed: Status = TEXT("  - downed"); break;
			case EDinoPlayerStatus::Dead:   Status = TEXT("  - dead"); break;
			default:                        Status = TEXT("  - spectating"); break;
			}
		}
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

void UDinoSessionMenu::FillMicPicker()
{
	const UDinoVoiceSubsystem* Voice = GetVoiceSubsystem();
	if (!Voice || !MicPicker)
	{
		return;
	}

	if (MicPicker->GetOptionCount() == 0)
	{
		MicPicker->AddOption(WindowsDefaultMic);
		for (const FString& Mic : Voice->GetSelectableMics())
		{
			MicPicker->AddOption(Mic);
		}
	}

	const FString Wanted = Voice->GetPickedMic().IsEmpty() ? FString(WindowsDefaultMic) : Voice->GetPickedMic();
	if (MicPicker->GetSelectedOption() != Wanted)
	{
		MicPicker->SetSelectedOption(Wanted);
	}
}

void UDinoSessionMenu::RefreshVoice()
{
	UDinoVoiceSubsystem* Voice = GetVoiceSubsystem();
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const APlayerController* PC = GetOwningPlayer();
	if (!Voice || !VoiceRowBox)
	{
		return;
	}

	TArray<ADinoPlayerState*> Others;
	if (GameState)
	{
		for (APlayerState* State : GameState->PlayerArray)
		{
			ADinoPlayerState* DinoPlayer = Cast<ADinoPlayerState>(State);
			if (DinoPlayer && (!PC || DinoPlayer != PC->PlayerState))
			{
				Others.Add(DinoPlayer);
			}
		}
	}

	bool bSamePlayers = Others.Num() == VoiceRows.Num();
	for (int32 Index = 0; bSamePlayers && Index < Others.Num(); ++Index)
	{
		bSamePlayers = VoiceRows[Index] && VoiceRows[Index]->GetPlayer() == Others[Index];
	}

	if (!bSamePlayers)
	{
		VoiceRowBox->ClearChildren();
		VoiceRows.Reset();
		for (ADinoPlayerState* Other : Others)
		{
			UDinoVoiceRow* Row = CreateWidget<UDinoVoiceRow>(GetOwningPlayer(), UDinoVoiceRow::StaticClass());
			if (Row)
			{
				VoiceRowBox->AddChild(Row);
				Row->SetPlayer(Other);
				VoiceRows.Add(Row);
			}
		}
	}
	else
	{
		for (UDinoVoiceRow* Row : VoiceRows)
		{
			Row->Refresh();
		}
	}

	NoOtherPlayersText->SetVisibility(Others.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	// The level is the loudest of the last few seconds, so a word just spoken stays readable.
	const float Peak = Voice->GetRecentMicPeak();
	const float Threshold = Voice->GetMicThreshold();
	const bool bHeard = Peak > 0.0f && Peak >= Threshold;
	FString Level;
	if (Voice->GetActiveMic().IsEmpty())
	{
		Level = TEXT("Voice starts once you host or join.");
	}
	else
	{
		Level = FString::Printf(TEXT("Using %s\nLevel %.3f  -  sent above %.3f  -  %s"),
			*Voice->GetActiveMic(), Peak, Threshold,
			bHeard ? TEXT("others hear you") : TEXT("too quiet to send"));
	}
	MicLevelText->SetText(FText::FromString(Level));
	MicLevelText->SetColorAndOpacity(FSlateColor(bHeard ? FLinearColor(0.5f, 0.9f, 0.5f) : FLinearColor(0.7f, 0.7f, 0.7f)));

	const FString Notice = Voice->GetMicNotice();
	MicNoticeText->SetText(FText::FromString(Notice));
	MicNoticeText->SetVisibility(Notice.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UDinoSessionMenu::HandleMicPicked(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// Direct is FillMicPicker showing the saved choice, not the player picking.
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}

	if (UDinoVoiceSubsystem* Voice = GetVoiceSubsystem())
	{
		Voice->PickMic(SelectedItem == WindowsDefaultMic ? FString() : SelectedItem);
	}
}

void UDinoSessionMenu::HandleSensitivityChanged(float Value)
{
	if (UDinoVoiceSubsystem* Voice = GetVoiceSubsystem())
	{
		Voice->SetMicSensitivity(Value);
	}
}

void UDinoSessionMenu::HandleReconnectMicClicked()
{
	if (UDinoVoiceSubsystem* Voice = GetVoiceSubsystem())
	{
		Voice->RefreshMic();
	}
}

UDinoVoiceSubsystem* UDinoSessionMenu::GetVoiceSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UDinoVoiceSubsystem>() : nullptr;
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
