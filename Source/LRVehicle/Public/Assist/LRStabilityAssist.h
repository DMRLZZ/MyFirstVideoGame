// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRVehicleTypes.h"

/**
 * Simcade steering aids (pure logic, no world): speed-sensitive steering and ESP-like counter-steer.
 * Sign convention everywhere: + = right (steering, yaw rate, side slip).
 */
struct LRVEHICLE_API FLRStabilityAssist
{
	/**
	 * Steering sensitivity at a given speed: SteeringScaleCurve evaluated at |SpeedKmh| (1 when disabled or empty).
	 * Data curves are validated as non-increasing, so the scale never grows with speed.
	 */
	static float EvaluateSteeringScale(const FLRStabilityAssistParams& Params, float SpeedKmh);

	/**
	 * Yaw rate the driver asks for with a road-wheel angle, from the kinematic bicycle model, capped by tire grip.
	 * @param SteeringAngleRad    Front road-wheel angle, radians.
	 * @param SpeedMs             Forward speed, m/s.
	 * @param WheelbaseMeters     Axle distance, meters.
	 * @param MaxLateralAccelMs2  Grip limit: |yaw rate| <= MaxLateralAccel / v.
	 * @return desired yaw rate, rad/s.
	 */
	static float ComputeDesiredYawRate(float SteeringAngleRad, float SpeedMs, float WheelbaseMeters, float MaxLateralAccelMs2);

	/**
	 * Steering correction against oversteer: damps yaw rate in excess of the desired one and counter-steers into the
	 * slide when the body side slip exceeds the threshold. Returns 0 below MinSpeedMs or when yaw damping is disabled.
	 * @param YawRateRadS         Measured yaw rate, rad/s.
	 * @param DesiredYawRateRadS  Yaw rate requested by the driver (ComputeDesiredYawRate), rad/s.
	 * @param SideSlipRad         atan2(local lateral velocity, local forward velocity), radians (+ = sliding right).
	 * @param SpeedMs             Forward speed, m/s.
	 * @return steering delta in [-MaxCorrection, MaxCorrection].
	 */
	static float ComputeCounterSteer(const FLRStabilityAssistParams& Params, float YawRateRadS, float DesiredYawRateRadS,
		float SideSlipRad, float SpeedMs);

	/**
	 * Full Simcade steering pipeline: RawSteering * EvaluateSteeringScale + ComputeCounterSteer, clamped to [-1, 1].
	 * @param RawSteering       Driver steering input, -1..1.
	 * @param MaxSteerAngleDeg  Front wheel angle at full input, degrees.
	 */
	static float ComputeSteering(const FLRStabilityAssistParams& Params, float RawSteering, float SpeedMs, float YawRateRadS,
		float SideSlipRad, float WheelbaseMeters, float MaxSteerAngleDeg);
};
