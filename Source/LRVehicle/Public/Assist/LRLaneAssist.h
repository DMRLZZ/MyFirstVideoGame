// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRVehicleTypes.h"

/**
 * Lane-keeping / lane-change steering controller of the Arcade scheme (pure logic, no world).
 *
 * The car keeps real Chaos physics: this controller only produces the steering input a skilled driver would apply.
 * Control law (angles in radians, + = right; v = forward speed, k = road curvature):
 *
 *   e        = TargetD - CurrentD
 *   vLatDes  = sign(e) * min(LateralGain * |e|, sqrt(2 * LateralDecelFraction * MaxLateralAccelMs2 * |e|), MaxLateralSpeedMs)
 *   vLat     = v * sin(HeadingError)
 *   aFb      = clamp(LateralSpeedGain * (vLatDes - vLat) - YawRateDamping * v * (YawRate - v * k), +-MaxLateralAccelMs2)
 *   steer    = clamp((aFb + v^2 * k) / authority(v), +-MaxSteering)
 *   authority(v) = min(SteeringToLateralAccelMs2, v^2 * KinematicCurvaturePerSteering)
 *
 * The law is built on the plant measured in the running game: at road speeds Chaos produces a lateral acceleration
 * proportional to the steering input and independent of speed (tire-limited understeer, about 15 m/s^2 per unit of
 * input), so working in lateral acceleration gives the same response at 100, 200 and 300 km/h. Only at very low speed
 * does the kinematic limit v^2 * k_max take over. The square-root profile on vLatDes keeps the lateral motion stoppable
 * at the target within the grip budget, so lane changes do not overshoot.
 */
struct LRVEHICLE_API FLRLaneAssist
{
	/**
	 * Lateral acceleration produced by one unit of steering input at this speed, m/s^2.
	 * @param SpeedMs  Forward speed, m/s.
	 */
	static float ComputeSteeringAuthority(const FLRLaneAssistParams& Params, float SpeedMs);

	/**
	 * Steering input that drives the car towards TargetD and aligns it with the road.
	 *
	 * @param CurrentD           Lateral position of the car, meters (+ = right of the route centerline).
	 * @param TargetD            Desired lateral position (center of the target lane), meters.
	 * @param HeadingErrorRad    Vehicle yaw minus road yaw, radians (+ = pointing to the right of the road).
	 * @param YawRateRadS        Vehicle yaw rate, rad/s (+ = turning right).
	 * @param SpeedMs            Forward speed, m/s.
	 * @param RoadCurvaturePerM  Road curvature at the car, 1/m (+ = road turns right); 0 on straights.
	 * @return steering input in [-MaxSteering, MaxSteering] (+ = steer right). 0 when the assist is disabled.
	 */
	static float ComputeSteering(const FLRLaneAssistParams& Params, float CurrentD, float TargetD, float HeadingErrorRad,
		float YawRateRadS, float SpeedMs, float RoadCurvaturePerM = 0.f);

	/**
	 * Inner loop on its own: steering input that reaches a lateral speed relative to the road.
	 *
	 * @param DesiredLateralSpeedMs  Lateral speed to reach, m/s (+ = right).
	 * @param MaxLateralAccelMs2     Grip budget for this move, m/s^2.
	 * @return steering input in [-MaxSteering, MaxSteering] (+ = steer right).
	 */
	static float ComputeSteeringForLateralSpeed(const FLRLaneAssistParams& Params, float DesiredLateralSpeedMs, float MaxLateralAccelMs2,
		float HeadingErrorRad, float YawRateRadS, float SpeedMs, float RoadCurvaturePerM = 0.f);
};
