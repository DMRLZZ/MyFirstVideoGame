// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRRoadTypes.h"

FLRRoadsideProp FLRRoadsideProp::Sanitized() const
{
	FLRRoadsideProp Safe = *this;

	Safe.CountPer100M = FMath::Clamp(CountPer100M, 0.f, 100.f);
	Safe.SpacingM = FMath::Max(SpacingM, 10.f);
	Safe.PhaseM = FMath::Max(PhaseM, 0.f);

	Safe.MinDistanceM = FMath::Max(MinDistanceM, 0.f);
	Safe.MaxDistanceM = FMath::Max(MaxDistanceM, Safe.MinDistanceM);
	Safe.DistancePower = FMath::Clamp(DistancePower, 0.1f, 5.f);

	Safe.MinScale = FMath::Clamp(MinScale, 0.05f, 20.f);
	Safe.MaxScale = FMath::Clamp(MaxScale, Safe.MinScale, 20.f);
	Safe.SinkM = FMath::Clamp(SinkM, -5.f, 5.f);
	Safe.CullDistanceM = FMath::Max(CullDistanceM, 0.f);

	Safe.KeepClearAlongM = FMath::Max(KeepClearAlongM, 0.f);
	Safe.KeepClearReachM = FMath::Max(KeepClearReachM, 0.f);
	return Safe;
}
