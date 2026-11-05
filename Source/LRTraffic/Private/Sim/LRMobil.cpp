// Copyright LEFTRIGHT. All Rights Reserved.

#include "Sim/LRMobil.h"

FLRMobilResult EvaluateMobil(const FLRMobilParams& Params, const FLRMobilInput& Input)
{
	FLRMobilResult Result;

	bool bSafe = true;
	if (Input.bHasNewFollower)
	{
		bSafe &= Input.AccelNewFollowerNew >= -Params.SafeDecelMs2;
		bSafe &= Input.GapToNewFollowerM >= Params.MinGapM;
	}
	if (Input.bHasNewLeader)
	{
		bSafe &= Input.GapToNewLeaderM >= Params.MinGapM;
	}
	Result.bSafe = bSafe;

	const float SelfGain = Input.AccelSelfNew - Input.AccelSelfOld;
	const float NewFollowerGain = Input.bHasNewFollower ? (Input.AccelNewFollowerNew - Input.AccelNewFollowerOld) : 0.f;
	const float OldFollowerGain = Input.bHasOldFollower ? (Input.AccelOldFollowerNew - Input.AccelOldFollowerOld) : 0.f;
	const float Incentive = SelfGain + Params.Politeness * (NewFollowerGain + OldFollowerGain);

	const float Required = Params.ThresholdMs2 + (Input.bTowardRight ? -Params.KeepRightBiasMs2 : Params.KeepRightBiasMs2);
	Result.Utility = Incentive - Required;
	Result.bChange = bSafe && Result.Utility > 0.f;
	return Result;
}
