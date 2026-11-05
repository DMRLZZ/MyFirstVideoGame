// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRInputTypes.h"

FLRInputTuning FLRInputTuning::Sanitized() const
{
	FLRInputTuning Out = *this;
	Out.StickDeadZone = FMath::Clamp(StickDeadZone, 0.f, 0.95f);
	Out.TriggerDeadZone = FMath::Clamp(TriggerDeadZone, 0.f, 0.95f);
	Out.SteeringExponent = FMath::Clamp(SteeringExponent, 0.2f, 5.f);
	Out.LaneChangeThreshold = FMath::Clamp(LaneChangeThreshold, 0.05f, 1.f);
	Out.LaneChangeReleaseThreshold = FMath::Clamp(LaneChangeReleaseThreshold, 0.f, Out.LaneChangeThreshold);
	Out.LaneChangeRepeatDelaySeconds = FMath::Clamp(LaneChangeRepeatDelaySeconds, 0.f, 5.f);
	return Out;
}
