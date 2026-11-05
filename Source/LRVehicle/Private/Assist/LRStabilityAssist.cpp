// Copyright LEFTRIGHT. All Rights Reserved.

#include "Assist/LRStabilityAssist.h"

#include "LRUnits.h"
#include "LRVehicleMath.h"

float FLRStabilityAssist::EvaluateSteeringScale(const FLRStabilityAssistParams& Params, float SpeedKmh)
{
	if (!Params.bSteeringScaleEnabled)
	{
		return 1.f;
	}
	const float Scale = LRVehicleMath::EvaluateCurve(Params.SteeringScaleCurve, FMath::Abs(SpeedKmh), 1.f);
	return FMath::Clamp(Scale, 0.01f, 1.f);
}

float FLRStabilityAssist::ComputeDesiredYawRate(float SteeringAngleRad, float SpeedMs, float WheelbaseMeters, float MaxLateralAccelMs2)
{
	if (WheelbaseMeters <= UE_SMALL_NUMBER)
	{
		return 0.f;
	}
	const float KinematicYawRate = SpeedMs / WheelbaseMeters * FMath::Tan(FMath::Clamp(SteeringAngleRad, -1.2f, 1.2f));
	const float AbsSpeed = FMath::Abs(SpeedMs);
	if (AbsSpeed < 1.f || MaxLateralAccelMs2 <= 0.f)
	{
		return KinematicYawRate;
	}
	const float GripLimit = MaxLateralAccelMs2 / AbsSpeed;
	return FMath::Clamp(KinematicYawRate, -GripLimit, GripLimit);
}

float FLRStabilityAssist::ComputeCounterSteer(const FLRStabilityAssistParams& Params, float YawRateRadS, float DesiredYawRateRadS,
	float SideSlipRad, float SpeedMs)
{
	if (!Params.bYawDampingEnabled || SpeedMs < Params.MinSpeedMs)
	{
		return 0.f;
	}

	float Correction = 0.f;

	// Yaw damping: only the oversteer part (rotating faster than asked, or against the request) is corrected.
	const float AbsYaw = FMath::Abs(YawRateRadS);
	const bool bAgainstRequest = YawRateRadS * DesiredYawRateRadS < 0.f && AbsYaw > Params.YawRateDeadbandRadS;
	const bool bTooFast = AbsYaw > FMath::Abs(DesiredYawRateRadS) + Params.YawRateDeadbandRadS;
	if (bAgainstRequest || bTooFast)
	{
		const float Excess = YawRateRadS - DesiredYawRateRadS;
		const float Deadband = FMath::Sign(Excess) * Params.YawRateDeadbandRadS;
		Correction -= Params.YawDampingGain * (Excess - Deadband);
	}

	// Counter-steer into the slide: steer towards the velocity direction.
	const float AbsSlip = FMath::Abs(SideSlipRad);
	if (AbsSlip > Params.SideSlipThresholdRad)
	{
		Correction += Params.CounterSteerGain * FMath::Sign(SideSlipRad) * (AbsSlip - Params.SideSlipThresholdRad);
	}

	return FMath::Clamp(Correction, -Params.MaxCorrection, Params.MaxCorrection);
}

float FLRStabilityAssist::ComputeSteering(const FLRStabilityAssistParams& Params, float RawSteering, float SpeedMs, float YawRateRadS,
	float SideSlipRad, float WheelbaseMeters, float MaxSteerAngleDeg)
{
	const float Scale = EvaluateSteeringScale(Params, static_cast<float>(LRUnits::MsToKmh(SpeedMs)));
	const float ScaledSteering = FMath::Clamp(RawSteering, -1.f, 1.f) * Scale;

	const float WheelAngleRad = FMath::DegreesToRadians(ScaledSteering * MaxSteerAngleDeg);
	const float DesiredYawRate = ComputeDesiredYawRate(WheelAngleRad, SpeedMs, WheelbaseMeters, Params.MaxLateralAccelMs2);
	const float Correction = ComputeCounterSteer(Params, YawRateRadS, DesiredYawRate, SideSlipRad, SpeedMs);

	return FMath::Clamp(ScaledSteering + Correction, -1.f, 1.f);
}
