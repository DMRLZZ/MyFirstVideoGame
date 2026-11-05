// Copyright LEFTRIGHT. All Rights Reserved.

#include "Sim/LRIdm.h"

float IdmAcceleration(const FLRIdmParams& Params, float SpeedMs, float GapM, float ClosingSpeedMs, bool bHasLeader)
{
	const float MaxAccel = FMath::Max(Params.MaxAccelMs2, 0.01f);
	const float DesiredSpeed = FMath::Max(Params.DesiredSpeedMs, 0.1f);
	const float Speed = FMath::Max(SpeedMs, 0.f);

	// Free-road term; delta = 4 is the common case and avoids a pow().
	const float Ratio = Speed / DesiredSpeed;
	float FreeTerm;
	if (Params.Delta == 4.f)
	{
		const float Ratio2 = Ratio * Ratio;
		FreeTerm = Ratio2 * Ratio2;
	}
	else
	{
		FreeTerm = FMath::Pow(Ratio, FMath::Max(Params.Delta, 0.1f));
	}

	float Accel = MaxAccel * (1.f - FreeTerm);

	if (bHasLeader)
	{
		const float Gap = FMath::Max(GapM, LRIdm::MinGapFloorM);
		const float Decel = FMath::Max(Params.ComfortDecelMs2, 0.01f);
		const float Dynamic = Speed * Params.TimeHeadwayS + (Speed * ClosingSpeedMs) / (2.f * FMath::Sqrt(MaxAccel * Decel));
		const float DesiredGap = FMath::Max(Params.MinGapM, 0.f) + FMath::Max(0.f, Dynamic);
		const float Interaction = DesiredGap / Gap;
		Accel -= MaxAccel * Interaction * Interaction;
	}

	return FMath::Clamp(Accel, LRIdm::MaxDecelMs2, MaxAccel);
}
