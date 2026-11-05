// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "LRRoadParticipant.generated.h"

UINTERFACE(MinimalAPI, NotBlueprintable)
class ULRRoadParticipant : public UInterface
{
	GENERATED_BODY()
};

/**
 * Minimal view of a vehicle that other systems need without depending on LRVehicle.
 *
 * Implemented by ALRVehicle. LRTraffic uses it to treat the player as an obstacle (IDM/MOBIL) and to
 * compute near-miss clearances; LRReplay and LRAudio use it to read speed.
 */
class LRCORE_API ILRRoadParticipant
{
	GENERATED_BODY()

public:
	/** Footprint of the vehicle body in meters: X = length, Y = width. */
	virtual FVector2D GetFootprintSizeMeters() const = 0;

	/** Signed speed along the vehicle's forward axis, m/s (negative when reversing). */
	virtual float GetForwardSpeedMs() const = 0;

	/** True once the vehicle crashed and should be ignored by traffic logic. */
	virtual bool IsWrecked() const { return false; }
};
