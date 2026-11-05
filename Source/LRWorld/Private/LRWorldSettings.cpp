// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRWorldSettings.h"

ULRWorldSettings::ULRWorldSettings()
	: OneWayRouteFile(TEXT("World/Route_GreyboxFreeway.json"))
	, TwoWayRouteFile(TEXT("World/Route_GreyboxTwoWay.json"))
	, bSpawnRoadStreamer(true)
	, bSpawnEnvironment(true)
	, FallbackSeed(1)
{
}

const ULRWorldSettings* ULRWorldSettings::Get()
{
	return GetDefault<ULRWorldSettings>();
}

FName ULRWorldSettings::GetCategoryName() const
{
	return TEXT("Game");
}
