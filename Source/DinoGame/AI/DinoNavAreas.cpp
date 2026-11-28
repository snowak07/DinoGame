#include "AI/DinoNavAreas.h"

UDinoNavArea_Foliage::UDinoNavArea_Foliage()
{
	DefaultCost = FoliageCost;
	DrawColor = FColor(60, 150, 60);
}

UDinoNavFilter_Chase::UDinoNavFilter_Chase()
{
	FNavigationFilterArea ThroughBushes;
	ThroughBushes.AreaClass = UDinoNavArea_Foliage::StaticClass();
	ThroughBushes.bOverrideTravelCost = true;
	ThroughBushes.TravelCostOverride = 1.0f;
	Areas.Add(ThroughBushes);
}
