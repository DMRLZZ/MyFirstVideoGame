// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRVehicleTypes.h"

/**
 * Lane-keeping / lane-change steering controller of the Arcade scheme (pure logic, no world).
 *
 * The car keeps real Chaos physics: this controller only produces the steering input a skilled driver would apply.
 * Control law (all angles in radians, + = right):
 *
 *   e        = TargetD - CurrentD
 *   vLatDes  = sign(e) * min(LateralGain * |e|, sqrt(2 * LateralDecelFraction * MaxLateralAccelMs2 * |e|), MaxLateralSpeedMs)
 *   psiDes   = atan2(vLatDes, max(v, MinSpeedMs))
 *   g(v)     = clamp((ReferenceSpeedMs / max(v, MinSpeedMs))^SpeedGainExponent, MinGainScale, MaxGainScale)
 *   fb       = g(v) * (HeadingGain * (psiDes - HeadingError) - YawRateDamping * (YawRate - v * k))
 *   fb       = clamp(fb, +-CurvatureFeedForward * MaxLateralAccelMs2 / v^2)          (tire grip limit)
 *   steer    = clamp(fb + CurvatureFeedForward * k, +-MaxSteering)
 *
 * The kinematic yaw gain of a car grows linearly with speed, so g(v) ~ 1/v makes the heading loop speed-independent
 * and the lateral response identical at 100, 200 or 300 km/h. The braking-distance profile on vLatDes guarantees the
 * lateral motion can always be stopped at the target within the grip budget, so lane changes do not overshoot.
 */
struct LRVEHICLE_API FLRLaneAssist
{
	/**
	 * Speed scheduling factor g(v) applied to the heading gains.
	 * @param SpeedMs  Forward speed, m/s.
	 */
	static float ComputeGainScale(const FLRLaneAssistParams& Params, float SpeedMs);

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
};
