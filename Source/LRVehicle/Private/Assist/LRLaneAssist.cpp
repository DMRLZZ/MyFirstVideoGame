// Copyright LEFTRIGHT. All Rights Reserved.

#include "Assist/LRLaneAssist.h"

float FLRLaneAssist::ComputeSteeringAuthority(const FLRLaneAssistParams& Params, float SpeedMs)
{
	const float Speed = FMath::Max(SpeedMs, FMath::Max(Params.MinSpeedMs, 0.1f));
	const float Kinematic = Speed * Speed * Params.KinematicCurvaturePerSteering;
	return FMath::Max(FMath::Min(Params.SteeringToLateralAccelMs2, Kinematic), 0.1f);
}

float FLRLaneAssist::ComputeSteering(const FLRLaneAssistParams& Params, float CurrentD, float TargetD, float HeadingErrorRad,
	float YawRateRadS, float SpeedMs, float RoadCurvaturePerM)
{
	if (!Params.bEnabled)
	{
		return 0.f;
	}

	const float MaxLateralAccel = FMath::Max(Params.MaxLateralAccelMs2, 0.1f);

	// Outer loop: lateral error -> desired lateral speed.
	// The square-root profile keeps the lateral speed low enough to stop at the target with the braking budget.
	const float LateralError = TargetD - CurrentD;
	const float AbsError = FMath::Abs(LateralError);
	const float StoppingSpeed = FMath::Sqrt(2.f * FMath::Clamp(Params.LateralDecelFraction, 0.01f, 1.f) * MaxLateralAccel * AbsError);
	const float DesiredLateralSpeedAbs = FMath::Min3(Params.LateralGain * AbsError, StoppingSpeed, FMath::Max(Params.MaxLateralSpeedMs, 0.f));
	const float DesiredLateralSpeed = LateralError >= 0.f ? DesiredLateralSpeedAbs : -DesiredLateralSpeedAbs;

	return ComputeSteeringForLateralSpeed(Params, DesiredLateralSpeed, MaxLateralAccel, HeadingErrorRad, YawRateRadS, SpeedMs, RoadCurvaturePerM);
}

float FLRLaneAssist::ComputeSteeringForLateralSpeed(const FLRLaneAssistParams& Params, float DesiredLateralSpeedMs, float MaxLateralAccelMs2,
	float HeadingErrorRad, float YawRateRadS, float SpeedMs, float RoadCurvaturePerM)
{
	const float Speed = FMath::Max(SpeedMs, FMath::Max(Params.MinSpeedMs, 0.1f));
	const float MaxLateralAccel = FMath::Max(MaxLateralAccelMs2, 0.1f);

	// Inner loop: lateral speed error -> lateral acceleration, damped by the yaw rate relative to the road
	// (subtracting v * k so that following a curve is not "damped away").
	const float LateralSpeed = Speed * FMath::Sin(FMath::UnwindRadians(HeadingErrorRad));
	const float RelativeYawRate = YawRateRadS - SpeedMs * RoadCurvaturePerM;
	const float FeedbackAccel = FMath::Clamp(
		Params.LateralSpeedGain * (DesiredLateralSpeedMs - LateralSpeed) - Params.YawRateDamping * Speed * RelativeYawRate,
		-MaxLateralAccel, MaxLateralAccel);

	// Feed-forward: the centripetal acceleration the road curve demands.
	const float CurveAccel = SpeedMs * SpeedMs * RoadCurvaturePerM;

	const float Limit = FMath::Clamp(Params.MaxSteering, 0.f, 1.f);
	return FMath::Clamp((FeedbackAccel + CurveAccel) / ComputeSteeringAuthority(Params, SpeedMs), -Limit, Limit);
}
