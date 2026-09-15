#include "AI/DinoAITypes.h"

namespace DinoAwarenessTags
{
	UE_DEFINE_GAMEPLAY_TAG(Unaware,    "Dino.Awareness.Unaware");
	UE_DEFINE_GAMEPLAY_TAG(Suspicious, "Dino.Awareness.Suspicious");
	UE_DEFINE_GAMEPLAY_TAG(Alerted,    "Dino.Awareness.Alerted");
	UE_DEFINE_GAMEPLAY_TAG(Hunting,    "Dino.Awareness.Hunting");
	UE_DEFINE_GAMEPLAY_TAG(Searching,  "Dino.Awareness.Searching");

	FGameplayTag FromAwareness(EDinoAwareness Awareness)
	{
		switch (Awareness)
		{
		case EDinoAwareness::Unaware:    return Unaware.GetTag();
		case EDinoAwareness::Suspicious: return Suspicious.GetTag();
		case EDinoAwareness::Alerted:    return Alerted.GetTag();
		case EDinoAwareness::Hunting:    return Hunting.GetTag();
		case EDinoAwareness::Searching:  return Searching.GetTag();
		default:                         return FGameplayTag();
		}
	}
}

FColor DinoAwarenessColor(EDinoAwareness Awareness)
{
	// Deliberately a threat gradient rather than arbitrary colours: calm to alarming, so the
	// meaning of a creature across the map reads without consulting a key.
	switch (Awareness)
	{
	case EDinoAwareness::Unaware:    return FColor(140, 140, 140); // grey, ignore me
	case EDinoAwareness::Suspicious: return FColor(230, 200, 60);  // yellow, heard something
	case EDinoAwareness::Alerted:    return FColor(240, 150, 40);  // orange, knows
	case EDinoAwareness::Hunting:    return FColor(220, 50, 50);   // red, coming for you
	case EDinoAwareness::Searching:  return FColor(80, 150, 235);  // blue, looking
	default:                         return FColor::Magenta;       // unmapped, obviously wrong
	}
}

FString DinoAwarenessName(EDinoAwareness Awareness)
{
	return UEnum::GetDisplayValueAsText(Awareness).ToString();
}

bool DinoAwarenessFromString(const FString& Text, EDinoAwareness& OutAwareness)
{
	const FString Wanted = Text.TrimStartAndEnd();
	if (Wanted.IsEmpty())
	{
		return false;
	}

	const UEnum* EnumType = StaticEnum<EDinoAwareness>();
	if (!EnumType)
	{
		return false;
	}

	// NumEnums() includes the generated _MAX entry, hence the -1.
	for (int32 Index = 0; Index < EnumType->NumEnums() - 1; ++Index)
	{
		const FString Name = EnumType->GetDisplayNameTextByIndex(Index).ToString();
		if (Name.StartsWith(Wanted, ESearchCase::IgnoreCase))
		{
			OutAwareness = static_cast<EDinoAwareness>(EnumType->GetValueByIndex(Index));
			return true;
		}
	}

	return false;
}
