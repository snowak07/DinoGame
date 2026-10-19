#include "DinoDamageTypes.h"

UDinoDamageType_Bite::UDinoDamageType_Bite()
{
	DeathDescription = TEXT("was mauled");
}

UDinoDamageType_Devour::UDinoDamageType_Devour()
{
	DeathDescription = TEXT("was eaten");
}

UDinoDamageType_Fall::UDinoDamageType_Fall()
{
	DeathDescription = TEXT("fell to their death");
}

FString UDinoDamageType::DescribeDeath(TSubclassOf<UDamageType> Cause)
{
	const UDinoDamageType* Dino = Cause ? Cast<UDinoDamageType>(Cause->GetDefaultObject()) : nullptr;
	return Dino ? Dino->DeathDescription : FString(TEXT("died"));
}

FString UDinoDamageType::NameOf(TSubclassOf<UDamageType> Cause)
{
	if (!Cause || !Cause->IsChildOf(UDinoDamageType::StaticClass()))
	{
		return TEXT("Other");
	}

	FString Name = Cause->GetName();
	Name.RemoveFromStart(TEXT("DinoDamageType_"));
	return Name;
}
