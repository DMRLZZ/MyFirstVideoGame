// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRCockpitHeadModel.h"

void FLRCockpitHeadModel::Configure(const FLRHeadModelParams& InParams)
{
	Params = InParams;
	Params.NaturalFrequencyHz = FMath::Clamp(Params.NaturalFrequencyHz, 0.2f, 10.f);
	Params.DampingRatio = FMath::Clamp(Params.DampingRatio, 0.3f, 3.f);
	Params.MaxOffsetCm = FMath::Max(Params.MaxOffsetCm, 0.f);
	Params.MaxRollDeg = FMath::Max(Params.MaxRollDeg, 0.f);
	Params.MaxPitchDeg = FMath::Max(Params.MaxPitchDeg, 0.f);
	Params.MaxInputG = FMath::Max(Params.MaxInputG, 0.1f);
	Params.Intensity = FMath::Clamp(Params.Intensity, 0.f, 3.f);
}

void FLRCockpitHeadModel::Reset()
{
	OffsetCm = FVector::ZeroVector;
	OffsetVelocityCms = FVector::ZeroVector;
	AnglesDeg = FVector2D::ZeroVector;
	AngularVelocityDegS = FVector2D::ZeroVector;
}

FLRCockpitHeadModel::FTargets FLRCockpitHeadModel::ComputeTargets(const FLRHeadModelParams& InParams, const FVector& LocalAccelerationMs2)
{
	const double MaxG = FMath::Max(static_cast<double>(InParams.MaxInputG), 0.1);
	const double GLong = FMath::Clamp(LocalAccelerationMs2.X / GravityMs2, -MaxG, MaxG);
	const double GLat = FMath::Clamp(LocalAccelerationMs2.Y / GravityMs2, -MaxG, MaxG);
	const double GVert = FMath::Clamp(LocalAccelerationMs2.Z / GravityMs2, -MaxG, MaxG);
	const double Intensity = FMath::Max(static_cast<double>(InParams.Intensity), 0.0);
	const double MaxOffset = FMath::Max(static_cast<double>(InParams.MaxOffsetCm), 0.0);

	FTargets Targets;
	// The head is thrown opposite to the acceleration of the car.
	Targets.OffsetCm.X = FMath::Clamp(-GLong * InParams.LongitudinalCmPerG * Intensity, -MaxOffset, MaxOffset);
	Targets.OffsetCm.Y = FMath::Clamp(-GLat * InParams.LateralCmPerG * Intensity, -MaxOffset, MaxOffset);
	Targets.OffsetCm.Z = FMath::Clamp(-GVert * InParams.VerticalCmPerG * Intensity, -MaxOffset, MaxOffset);

	// Leaning out of the turn (right turn = acceleration to the right = head rolls left, right side up).
	const double MaxRoll = FMath::Max(static_cast<double>(InParams.MaxRollDeg), 0.0);
	Targets.RollDeg = static_cast<float>(FMath::Clamp(-GLat * InParams.RollDegPerG * Intensity, -MaxRoll, MaxRoll));

	// Accelerating tilts the head back (up), braking nods it forward (down).
	const double MaxPitch = FMath::Max(static_cast<double>(InParams.MaxPitchDeg), 0.0);
	Targets.PitchDeg = static_cast<float>(FMath::Clamp(GLong * InParams.PitchDegPerG * Intensity, -MaxPitch, MaxPitch));
	return Targets;
}

void FLRCockpitHeadModel::Update(const FVector& LocalAccelerationMs2, float DeltaSeconds)
{
	if (DeltaSeconds <= 0.f)
	{
		return;
	}

	const FTargets Targets = ComputeTargets(Params, LocalAccelerationMs2);
	const float Omega = UE_TWO_PI * Params.NaturalFrequencyHz;
	const float DampingCoeff = 2.f * Params.DampingRatio * Omega;

	float Remaining = FMath::Min(DeltaSeconds, MaxFrameSeconds);
	while (Remaining > UE_KINDA_SMALL_NUMBER)
	{
		const float H = FMath::Min(Remaining, MaxSubstepSeconds);
		Substep(Targets, Omega, DampingCoeff, H);
		Remaining -= H;
	}
}

void FLRCockpitHeadModel::Substep(const FTargets& Targets, float Omega, float DampingCoeff, float H)
{
	const double OmegaSq = static_cast<double>(Omega) * Omega;

	// Semi-implicit Euler: velocity first, then position with the new velocity (stable for Omega * H << 2).
	OffsetVelocityCms += (OmegaSq * (Targets.OffsetCm - OffsetCm) - static_cast<double>(DampingCoeff) * OffsetVelocityCms) * H;
	OffsetCm += OffsetVelocityCms * H;

	const FVector2D AngleTargets(Targets.RollDeg, Targets.PitchDeg);
	AngularVelocityDegS += (OmegaSq * (AngleTargets - AnglesDeg) - static_cast<double>(DampingCoeff) * AngularVelocityDegS) * H;
	AnglesDeg += AngularVelocityDegS * H;

	const double MaxOffset = Params.MaxOffsetCm;
	ClampChannel(OffsetCm.X, OffsetVelocityCms.X, MaxOffset);
	ClampChannel(OffsetCm.Y, OffsetVelocityCms.Y, MaxOffset);
	ClampChannel(OffsetCm.Z, OffsetVelocityCms.Z, MaxOffset);
	ClampChannel(AnglesDeg.X, AngularVelocityDegS.X, Params.MaxRollDeg);
	ClampChannel(AnglesDeg.Y, AngularVelocityDegS.Y, Params.MaxPitchDeg);
}

void FLRCockpitHeadModel::ClampChannel(double& Value, double& Velocity, double Limit)
{
	if (Value > Limit)
	{
		Value = Limit;
		Velocity = FMath::Min(Velocity, 0.0);
	}
	else if (Value < -Limit)
	{
		Value = -Limit;
		Velocity = FMath::Max(Velocity, 0.0);
	}
}
