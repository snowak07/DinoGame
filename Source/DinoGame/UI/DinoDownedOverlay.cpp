#include "UI/DinoDownedOverlay.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "DinoCharacter.h"
#include "DinoHealthRules.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
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
			Slot->SetPadding(FMargin(0.0f, 2.0f));
		}
		return Text;
	}

	UProgressBar* MakeBar(UWidgetTree& Tree, UVerticalBox& Column, const FLinearColor& Colour)
	{
		USizeBox* Size = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetWidthOverride(320.0f);
		Size->SetHeightOverride(14.0f);

		UProgressBar* Bar = Tree.ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		Bar->SetFillColorAndOpacity(Colour);
		Size->SetContent(Bar);

		if (UVerticalBoxSlot* Slot = Column.AddChildToVerticalBox(Size))
		{
			Slot->SetHorizontalAlignment(HAlign_Center);
			Slot->SetPadding(FMargin(0.0f, 4.0f));
		}
		return Bar;
	}

	FString NameOf(const ADinoCharacter* Character)
	{
		const APlayerState* State = Character ? Character->GetPlayerState() : nullptr;
		return State ? State->GetPlayerName() : TEXT("them");
	}

	/** The bar's parent size box is what takes up room, so that is what is shown or collapsed. */
	void ShowBar(UProgressBar* Bar, bool bShow, float Percent)
	{
		if (UWidget* Holder = Bar ? Bar->GetParent() : nullptr)
		{
			Holder->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
		if (Bar && bShow)
		{
			Bar->SetPercent(FMath::Clamp(Percent, 0.0f, 1.0f));
		}
	}
}

void UDinoDownedOverlay::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	// Full-screen and transparent, to place the column a little below the centre of the view.
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Frame"));
	Frame->SetBrushColor(FLinearColor::Transparent);
	Frame->SetHorizontalAlignment(HAlign_Center);
	Frame->SetVerticalAlignment(VAlign_Center);
	Frame->SetPadding(FMargin(0.0f, 260.0f, 0.0f, 0.0f));
	WidgetTree->RootWidget = Frame;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Frame->SetContent(Column);

	TitleText = MakeLine(*WidgetTree, *Column, 26, FLinearColor(0.95f, 0.3f, 0.25f));
	DetailText = MakeLine(*WidgetTree, *Column, 15, FLinearColor::White);
	BleedBar = MakeBar(*WidgetTree, *Column, FLinearColor(0.85f, 0.15f, 0.1f));
	ReviveBar = MakeBar(*WidgetTree, *Column, FLinearColor(0.3f, 0.85f, 0.35f));

	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UDinoDownedOverlay::NativeConstruct()
{
	Super::NativeConstruct();

	Refresh();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer,
			FTimerDelegate::CreateUObject(this, &UDinoDownedOverlay::Refresh), 0.1f, true);
	}
}

void UDinoDownedOverlay::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	Super::NativeDestruct();
}

void UDinoDownedOverlay::Refresh()
{
	const APlayerController* PC = GetOwningPlayer();
	ADinoCharacter* Me = PC ? Cast<ADinoCharacter>(PC->GetPawn()) : nullptr;

	auto Hide = [this]()
	{
		SetVisibility(ESlateVisibility::Collapsed);
	};

	if (!Me || !Me->IsAlive())
	{
		Hide();
		return;
	}

	if (Me->IsDowned())
	{
		const FDinoReviveState& Revive = Me->GetReviveState();
		TitleText->SetText(FText::FromString(TEXT("YOU'RE DOWN")));
		DetailText->SetText(FText::FromString(Revive.Reviver
			? FString::Printf(TEXT("%s is reviving you"), *NameOf(Revive.Reviver))
			: FString(TEXT("Bleeding out - a teammate can revive you"))));
		ShowBar(BleedBar, true, Me->GetBleedOut() / DinoHealthRules::BleedOutFull);
		ShowBar(ReviveBar, Revive.Reviver != nullptr, Revive.Progress);
		SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}

	// Standing: the teammate in reach to revive, if any.
	const ADinoCharacter* Target = Me->FindReviveTarget();
	if (!Target)
	{
		Hide();
		return;
	}

	const FDinoReviveState& Revive = Target->GetReviveState();
	const bool bMeReviving = Revive.Reviver == Me;
	TitleText->SetText(FText::GetEmpty());
	DetailText->SetText(FText::FromString(bMeReviving
		? FString::Printf(TEXT("Reviving %s - keep holding E"), *NameOf(Target))
		: Revive.Reviver
			? FString::Printf(TEXT("%s is reviving %s"), *NameOf(Revive.Reviver), *NameOf(Target))
			: FString::Printf(TEXT("Hold E to revive %s"), *NameOf(Target))));
	ShowBar(BleedBar, true, Target->GetBleedOut() / DinoHealthRules::BleedOutFull);
	ShowBar(ReviveBar, Revive.Reviver != nullptr, Revive.Progress);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}
