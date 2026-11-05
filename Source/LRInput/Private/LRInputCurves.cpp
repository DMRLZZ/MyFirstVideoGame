// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRInputCurves.h"

#include "LRInputTypes.h"

float FLRInputCurves::RemapDeadZone(float Value, float LowerThreshold, float UpperThreshold)
{
	const float Lower = FMath::Max(0.f, LowerThreshold);
	if (UpperThreshold <= Lower)
	{
		return FMath::Clamp(Value, -1.f, 1.f);
	}
	const float Magnitude = FMath::Min(1.f, FMath::Max(0.f, FMath::Abs(Value) - Lower) / (UpperThreshold - Lower));
	return Magnitude * FMath::Sign(Value);
}

float FLRInputCurves::ApplyResponseCurve(float Value, float Exponent)
{
	if (Exponent <= 0.f || Exponent == 1.f)
	{
		return Value;
	}
	return FMath::Sign(Value) * FMath::Pow(FMath::Abs(Value), Exponent);
}

float FLRInputCurves::ShapeStickSteering(float RawAxis, const FLRInputTuning& Tuning)
{
	float Value = RemapDeadZone(RawAxis, Tuning.StickDeadZone);
	Value = ApplyResponseCurve(Value, Tuning.SteeringExponent);
	if (Tuning.bInvertSteering)
	{
		Value = -Value;
	}
	return FMath::Clamp(Value, -1.f, 1.f);
}

float FLRInputCurves::ShapeTrigger(float RawAxis, const FLRInputTuning& Tuning)
{
	return FMath::Clamp(RemapDeadZone(RawAxis, Tuning.TriggerDeadZone), 0.f, 1.f);
}
