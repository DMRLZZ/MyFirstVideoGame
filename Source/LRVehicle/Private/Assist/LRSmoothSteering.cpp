// Copyright LEFTRIGHT. All Rights Reserved.

#include "Assist/LRSmoothSteering.h"

namespace LRSmoothSteeringPrivate
{
	/** Below this ramp value the player counts as not steering. */
	constexpr float ActiveInput = 0.02f;

	/** While the car moves sideways faster than this, its stop is still in progress, m/s. */
	constexpr float SettledLateralSpeedMs = 0.35f;

	/** Closer than this to a requested position counts as arrived, meters. */
	constexpr float ArrivedMeters = 0.1f;
}

FLRSmoothSteering::FLimits FLRSmoothSteering::ComputeLimits(float LanesLeftD, float LanesRightD, float LeftBarrierD, float RightBarrierD,
	float HalfWidthMeters, float BarrierMarginMeters, float ShoulderAllowanceMeters)
{
	FLimits Limits;
	Limits.MinD = LeftBarrierD + HalfWidthMeters + BarrierMarginMeters;
	Limits.MaxD = RightBarrierD - HalfWidthMeters - BarrierMarginMeters;
	if (ShoulderAllowanceMeters >= 0.f)
	{
		Limits.MinD = FMath::Max(Limits.MinD, LanesLeftD + HalfWidthMeters - ShoulderAllowanceMeters);
		Limits.MaxD = FMath::Min(Limits.MaxD, LanesRightD - HalfWidthMeters + ShoulderAllowanceMeters);
	}
	if (Limits.MinD > Limits.MaxD)
	{
		Limits.MinD = Limits.MaxD = 0.5f * (Limits.MinD + Limits.MaxD);
	}
	return Limits;
}

void FLRSmoothSteering::Reset(float CurrentD)
{
	SmoothedInput = 0.f;
	HoldD = CurrentD;
	bHasHoldTarget = false;
}

void FLRSmoothSteering::SetHoldTarget(float TargetD)
{
	HoldD = TargetD;
	bHasHoldTarget = true;
}

float FLRSmoothSteering::Update(const FLRSmoothSteeringParams& Params, float Input, float Sensitivity, float DeltaSeconds, float CurrentD,
	float ForwardSpeedMs, float LateralSpeedMs, float NearestLaneCenterD, const FLimits& Limits)
{
	using namespace LRSmoothSteeringPrivate;

	const float Dt = FMath::Max(0.f, DeltaSeconds);
	const float Target = FMath::Clamp(Input, -1.f, 1.f);

	// Ramp: separate rates for pressing, releasing and crossing to the other side.
	const bool bReversing = Target * SmoothedInput < 0.f;
	const bool bReleasing = FMath::Abs(Target) < FMath::Abs(SmoothedInput);
	const float Rate = bReversing ? Params.InputReversePerS : (bReleasing ? Params.InputFallPerS : Params.InputRisePerS);
	SmoothedInput = FMath::FInterpConstantTo(SmoothedInput, Target, Dt, FMath::Max(Rate, 0.01f));

	// Slower sideways at low speed: the same lateral speed would mean a much sharper angle.
	const float TopSpeed = FMath::Min(
		Params.MaxLateralSpeedMs * FMath::Max(Sensitivity, 0.f),
		Params.LateralSpeedPerForwardSpeed * FMath::Abs(ForwardSpeedMs));

	const float Magnitude = FMath::Abs(SmoothedInput);
	bHasHoldTarget = bHasHoldTarget && Magnitude <= ActiveInput && FMath::Abs(HoldD - CurrentD) > ArrivedMeters;
	if (Magnitude > ActiveInput || (!bHasHoldTarget && FMath::Abs(LateralSpeedMs) > SettledLateralSpeedMs))
	{
		// Steering, or still stopping after a release: the position to keep is wherever the car ends up, not behind it.
		HoldD = CurrentD;
	}
	else if (!bHasHoldTarget && FMath::Abs(NearestLaneCenterD - HoldD) <= Params.LaneMagnetRadiusMeters)
	{
		HoldD = FMath::FInterpConstantTo(HoldD, NearestLaneCenterD, Dt, Params.LaneMagnetSpeedMs);
	}
	HoldD = FMath::Clamp(HoldD, Limits.MinD, Limits.MaxD);

	// What the player asks for, mixed with holding the position as the ramp fades out.
	const float HoldSpeed = FMath::Clamp((HoldD - CurrentD) * Params.HoldGain, -TopSpeed, TopSpeed);
	float LateralSpeed = SmoothedInput * TopSpeed + (1.f - Magnitude) * HoldSpeed;

	// Edges: the speed towards the outside fades with the room that is left, and turns back beyond the limit.
	const float Slowdown = FMath::Max(Params.EdgeSlowdownMeters, 0.1f);
	const float RoomRight = Limits.MaxD - CurrentD;
	const float RoomLeft = CurrentD - Limits.MinD;
	const float MaxRight = RoomRight >= 0.f ? TopSpeed * FMath::Min(RoomRight / Slowdown, 1.f) : RoomRight * Params.HoldGain;
	const float MaxLeft = RoomLeft >= 0.f ? TopSpeed * FMath::Min(RoomLeft / Slowdown, 1.f) : RoomLeft * Params.HoldGain;
	return FMath::Clamp(LateralSpeed, -MaxLeft, MaxRight);
}
