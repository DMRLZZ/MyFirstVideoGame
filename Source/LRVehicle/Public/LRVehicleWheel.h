// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChaosVehicleWheel.h"

#include "LRVehicleWheel.generated.h"

/**
 * Front wheel class of every LEFTRIGHT player car (steered, not affected by the handbrake).
 *
 * Chaos builds the physics wheel and suspension from the class default object, so these defaults are a generic sports
 * car. Per-car values (radius, grip, cornering stiffness, brakes, steering lock, suspension) are written into this
 * default object by ULRVehicleMovementComponent::ApplyDefinition right before the physics vehicle is rebuilt.
 */
UCLASS()
class LRVEHICLE_API ULRWheelFront : public UChaosVehicleWheel
{
	GENERATED_BODY()

public:
	ULRWheelFront(const FObjectInitializer& ObjectInitializer);
};

/**
 * Rear wheel class of every LEFTRIGHT player car (not steered, handbrake enabled). See ULRWheelFront.
 */
UCLASS()
class LRVEHICLE_API ULRWheelRear : public UChaosVehicleWheel
{
	GENERATED_BODY()

public:
	ULRWheelRear(const FObjectInitializer& ObjectInitializer);
};
