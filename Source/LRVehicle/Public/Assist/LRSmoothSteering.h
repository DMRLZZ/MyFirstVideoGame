// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRVehicleTypes.h"

/**
 * Steering of the Smooth scheme: the car moves sideways for as long as the key is held and goes straight on release.
 *
 * A key is all or nothing, so the input is ramped first: a tap gives a small correction and holding gives a full
 * move, with no jump in either case. The result is a lateral speed, which FLRLaneAssist::ComputeSteeringForLateralSpeed
 * turns into steering. Near the edges of the road the outward speed fades out, so holding a key never ends in a wall.
 */
struct LRVEHICLE_API FLRSmoothSteering
{
	/** Position of the road the car has to respect, meters. */
	struct FLimits
	{
		float MinD = -TNumericLimits<float>::Max();
		float MaxD = TNumericLimits<float>::Max();
	};

	/** Clears the ramp and keeps the car where it is. */
	void Reset(float CurrentD);

	/** Moves towards a position with no input held: what a lane change key or the autopilot ask for. */
	void SetHoldTarget(float TargetD);

	/** True when SetHoldTarget asked for a position and the car has not arrived yet. */
	bool HasHoldTarget() const { return bHasHoldTarget; }

	/**
	 * Advances one frame.
	 *
	 * @param Input              Raw steering, -1 to 1 (+ = right).
	 * @param Sensitivity        Player setting that scales the top lateral speed. 1 = as tuned.
	 * @param CurrentD           Lateral position of the car, meters.
	 * @param ForwardSpeedMs     Forward speed, m/s.
	 * @param LateralSpeedMs     Sideways speed the car has right now, m/s. Until it fades the car is not pulled back.
	 * @param NearestLaneCenterD Center of the closest lane, meters. A car that is almost centered settles on it.
	 * @return desired lateral speed, m/s (+ = right).
	 */
	float Update(const FLRSmoothSteeringParams& Params, float Input, float Sensitivity, float DeltaSeconds, float CurrentD,
		float ForwardSpeedMs, float LateralSpeedMs, float NearestLaneCenterD, const FLimits& Limits);

	/** Input after the ramp, -1 to 1. */
	float GetSmoothedInput() const { return SmoothedInput; }

	/** Position the car returns to with no input, meters. */
	float GetHoldD() const { return HoldD; }

private:
	float SmoothedInput = 0.f;
	float HoldD = 0.f;
	bool bHasHoldTarget = false;
};
