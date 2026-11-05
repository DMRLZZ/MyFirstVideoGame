// Copyright LEFTRIGHT. All Rights Reserved.

#include "Assist/LRLaneAssist.h"

float FLRLaneAssist::ComputeGainScale(const FLRLaneAssistParams& Params, float SpeedMs)
{
	const float Speed = FMath::Max(SpeedMs, FMath::Max(Params.MinSpeedMs, 0.1f));
	const float Ratio = Params.ReferenceSpeedMs / Speed;
	const float Scale = FMath::Pow(Ratio, Params.SpeedGainExponent);
	return FMath::Clamp(Scale, Params.MinGainScale, FMath::Max(Params.MaxGainScale, Params.MinGainScale));
}

float FLRLaneAssist::ComputeSteering(const FLRLaneAssistParams& Params, float CurrentD, float TargetD, float HeadingErrorRad,
	float YawRateRadS, float SpeedMs, float RoadCurvaturePerM)
{
	if (!Params.bEnabled)
	{
		return 0.f;
	}

	const float Speed = FMath::Max(SpeedMs, FMath::Max(Params.MinSpeedMs, 0.1f));
	const float MaxLateralAccel = FMath::Max(Params.MaxLateralAccelMs2, 0.1f);

	// Outer loop: lateral error -> desired lateral speed -> desired heading relative to the road.
	// The square-root profile keeps the lateral speed low enough to stop at the target with the braking budget.
	const float LateralError = TargetD - CurrentD;
	const float AbsError = FMath::Abs(LateralError);
	const float StoppingSpeed = FMath::Sqrt(2.f * FMath::Clamp(Params.LateralDecelFraction, 0.01f, 1.f) * MaxLateralAccel * AbsError);
	const float DesiredLateralSpeedAbs = FMath::Min3(Params.LateralGain * AbsError, StoppingSpeed, FMath::Max(Params.MaxLateralSpeedMs, 0.f));
	const float DesiredLateralSpeed = LateralError >= 0.f ? DesiredLateralSpeedAbs : -DesiredLateralSpeedAbs;
	const float DesiredHeading = FMath::Atan2(DesiredLateralSpeed, Speed);

	// Inner loop: heading PD with damping on the yaw rate relative to the road (so curves are not "damped away").
	const float HeadingError = FMath::UnwindRadians(HeadingErrorRad);
	const float RelativeYawRate = YawRateRadS - SpeedMs * RoadCurvaturePerM;
	const float GainScale = ComputeGainScale(Params, SpeedMs);
	float Feedback = GainScale * (Params.HeadingGain * (DesiredHeading - HeadingError) - Params.YawRateDamping * RelativeYawRate);

	// Grip limit: path curvature a / v^2 converted to steering with the same factor as the curvature feed-forward.
	if (Params.CurvatureFeedForward > 0.f)
	{
		const float FeedbackLimit = Params.CurvatureFeedForward * MaxLateralAccel / (Speed * Speed);
		Feedback = FMath::Clamp(Feedback, -FeedbackLimit, FeedbackLimit);
	}

	const float FeedForward = Params.CurvatureFeedForward * RoadCurvaturePerM;
	const float Limit = FMath::Clamp(Params.MaxSteering, 0.f, 1.f);
	return FMath::Clamp(Feedback + FeedForward, -Limit, Limit);
}
