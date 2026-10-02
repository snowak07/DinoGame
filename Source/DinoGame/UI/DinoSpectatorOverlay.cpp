#include "UI/DinoSpectatorOverlay.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "DinoSpectatorPawn.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

namespace
{
	UTextBlock* MakeLine(UWidgetTree& Tree, UVerticalBox& Column, int32 FontSize, const FLinearColor& Colour)
	{
		UTextBlock* Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());

		FSlateFontInfo Font = Text->GetFont();
		Font.Size = FontSize;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Colour));
		Text->SetJustification(ETextJustify::Center);
		Text->SetShadowOffset(FVector2D(1.0f, 1.0f));

		if (UVerticalBoxSlot* Slot = Column.AddChildToVerticalBox(Text))
		{
			Slot->SetHorizontalAlignment(HAlign_Center);
		}

		return Text;
	}
}

void UDinoSpectatorOverlay::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	// Transparent and full-screen, purely to anchor the text to the bottom centre.
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Frame"));
	Frame->SetBrushColor(FLinearColor::Transparent);
	Frame->SetHorizontalAlignment(HAlign_Center);
	Frame->SetVerticalAlignment(VAlign_Bottom);
	Frame->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 60.0f));
	WidgetTree->RootWidget = Frame;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Frame->SetContent(Column);

	ViewText = MakeLine(*WidgetTree, *Column, 22, FLinearColor::White);
	HintText = MakeLine(*WidgetTree, *Column, 13, FLinearColor(0.75f, 0.75f, 0.75f));

	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UDinoSpectatorOverlay::NativeConstruct()
{
	Super::NativeConstruct();

	Refresh();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer,
			FTimerDelegate::CreateUObject(this, &UDinoSpectatorOverlay::Refresh), 0.2f, true);
	}
}

void UDinoSpectatorOverlay::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}

	Super::NativeDestruct();
}

void UDinoSpectatorOverlay::Refresh()
{
	const APlayerController* PC = GetOwningPlayer();
	const ADinoSpectatorPawn* Spectator = PC ? Cast<ADinoSpectatorPawn>(PC->GetSpectatorPawn()) : nullptr;
	if (!Spectator)
	{
		ViewText->SetText(FText::GetEmpty());
		HintText->SetText(FText::GetEmpty());
		return;
	}

	ViewText->SetText(FText::FromString(Spectator->DescribeView()));

	const bool bFree = Spectator->GetMode() == EDinoSpectateMode::Free;
	HintText->SetText(FText::FromString(bFree
		? TEXT("WASD / Q E  fly      LMB / RMB  watch a player      Space  camera mode      Tab  menu")
		: TEXT("LMB / RMB  switch player      Space  camera mode      Tab  menu")));
}
