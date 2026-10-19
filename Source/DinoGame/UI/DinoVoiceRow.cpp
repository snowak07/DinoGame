#include "UI/DinoVoiceRow.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "DinoPlayerState.h"
#include "Engine/GameInstance.h"
#include "Online/DinoVoiceSubsystem.h"

namespace
{
	constexpr float NameWidth = 170.0f;
	constexpr float MuteWidth = 90.0f;
	constexpr float SliderWidth = 150.0f;
	constexpr float MaxVolume = 2.0f;

	UTextBlock* MakeText(UWidgetTree* Tree, int32 FontSize, const FLinearColor& Colour)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = FontSize;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Colour));
		return Text;
	}

	/** Fixed widths, so every row's controls line up in columns. */
	void AddSized(UWidgetTree* Tree, UHorizontalBox* Row, UWidget* Content, float Width)
	{
		USizeBox* Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetWidthOverride(Width);
		Size->SetContent(Content);
		if (UHorizontalBoxSlot* BoxSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Size)))
		{
			BoxSlot->SetVerticalAlignment(VAlign_Center);
			BoxSlot->SetPadding(FMargin(4.0f, 2.0f));
		}
	}
}

void UDinoVoiceRow::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
	WidgetTree->RootWidget = Row;

	NameText = MakeText(WidgetTree, 15, FLinearColor::White);
	AddSized(WidgetTree, Row, NameText, NameWidth);

	MuteButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	MuteLabel = MakeText(WidgetTree, 13, FLinearColor(0.05f, 0.05f, 0.05f));
	MuteLabel->SetJustification(ETextJustify::Center);
	MuteButton->SetContent(MuteLabel);
	MuteButton->OnClicked.AddDynamic(this, &UDinoVoiceRow::HandleMuteClicked);
	AddSized(WidgetTree, Row, MuteButton, MuteWidth);

	VolumeSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
	VolumeSlider->SetMinValue(0.0f);
	VolumeSlider->SetMaxValue(MaxVolume);
	VolumeSlider->SetStepSize(0.05f);
	VolumeSlider->SetValue(1.0f);
	VolumeSlider->OnValueChanged.AddDynamic(this, &UDinoVoiceRow::HandleVolumeChanged);
	AddSized(WidgetTree, Row, VolumeSlider, SliderWidth);

	VolumeText = MakeText(WidgetTree, 13, FLinearColor(0.8f, 0.8f, 0.8f));
	AddSized(WidgetTree, Row, VolumeText, 50.0f);
}

void UDinoVoiceRow::SetPlayer(ADinoPlayerState* InPlayer)
{
	Player = InPlayer;

	const UGameInstance* GameInstance = GetGameInstance();
	const UDinoVoiceSubsystem* Voice = GameInstance ? GameInstance->GetSubsystem<UDinoVoiceSubsystem>() : nullptr;
	if (VolumeSlider && InPlayer && Voice)
	{
		VolumeSlider->SetValue(Voice->GetPlayerVolume(InPlayer->GetUniqueId()));
	}

	Refresh();
}

void UDinoVoiceRow::Refresh()
{
	const ADinoPlayerState* Target = Player.Get();
	const UGameInstance* GameInstance = GetGameInstance();
	const UDinoVoiceSubsystem* Voice = GameInstance ? GameInstance->GetSubsystem<UDinoVoiceSubsystem>() : nullptr;
	if (!Target || !Voice || !NameText)
	{
		return;
	}

	const bool bMuted = Voice->IsPlayerMuted(Target->GetUniqueId());

	// A level above zero only while their voice is actually playing here, so a muted player,
	// or a dead one the living cannot hear, never shows as talking.
	const bool bTalking = !bMuted && Target->GetVoiceLevel() > 0.0f;

	NameText->SetText(FText::FromString(FString::Printf(TEXT("%s%s"), *Target->GetPlayerName(), bTalking ? TEXT("  (talking)") : TEXT(""))));
	NameText->SetColorAndOpacity(FSlateColor(bMuted ? FLinearColor(0.55f, 0.55f, 0.55f) : FLinearColor::White));
	MuteLabel->SetText(FText::FromString(bMuted ? TEXT("Unmute") : TEXT("Mute")));
	VolumeText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(VolumeSlider->GetValue() * 100.0f))));
}

void UDinoVoiceRow::HandleMuteClicked()
{
	ADinoPlayerState* Target = Player.Get();
	const UGameInstance* GameInstance = GetGameInstance();
	UDinoVoiceSubsystem* Voice = GameInstance ? GameInstance->GetSubsystem<UDinoVoiceSubsystem>() : nullptr;
	if (Target && Voice)
	{
		Voice->SetPlayerMuted(GetOwningPlayer(), Target, !Voice->IsPlayerMuted(Target->GetUniqueId()));
		Refresh();
	}
}

void UDinoVoiceRow::HandleVolumeChanged(float Value)
{
	const UGameInstance* GameInstance = GetGameInstance();
	if (UDinoVoiceSubsystem* Voice = GameInstance ? GameInstance->GetSubsystem<UDinoVoiceSubsystem>() : nullptr)
	{
		Voice->SetPlayerVolume(Player.Get(), Value);
	}
	Refresh();
}
