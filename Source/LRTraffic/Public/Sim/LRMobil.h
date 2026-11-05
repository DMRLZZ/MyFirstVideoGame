// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Parameters of the MOBIL lane change model (Kesting, Treiber, Helbing 2007) for one driver. SI units. */
struct FLRMobilParams
{
	/** Politeness factor p (0 = selfish). */
	float Politeness = 0.3f;

	/** Switching threshold, m/s^2. */
	float ThresholdMs2 = 0.2f;

	/** Keep-right bias, m/s^2 (> 0 favours moving right). */
	float KeepRightBiasMs2 = 0.15f;

	/** Maximum deceleration imposed on the new follower, m/s^2 (positive). */
	float SafeDecelMs2 = 4.f;

	/** Minimum bumper-to-bumper gaps to the new leader and the new follower, meters. */
	float MinGapM = 2.f;
};

/**
 * Accelerations (m/s^2) and gaps (m) needed by one MOBIL evaluation. "c" = the candidate vehicle,
 * "n" = its new follower in the target lane, "o" = its old follower in the current lane.
 * "Old" = without the change, "New" = after the change. Missing vehicles: set the bHas* flag to false.
 */
struct FLRMobilInput
{
	float AccelSelfOld = 0.f;
	float AccelSelfNew = 0.f;
	float AccelNewFollowerOld = 0.f;
	float AccelNewFollowerNew = 0.f;
	float AccelOldFollowerOld = 0.f;
	float AccelOldFollowerNew = 0.f;

	/** Gap between the candidate's front and the new leader's rear, meters. */
	float GapToNewLeaderM = TNumericLimits<float>::Max();

	/** Gap between the new follower's front and the candidate's rear, meters. */
	float GapToNewFollowerM = TNumericLimits<float>::Max();

	bool bHasNewLeader = false;
	bool bHasNewFollower = false;
	bool bHasOldFollower = false;

	/** True when the change is toward the driver's right (keep-right bias favours it). */
	bool bTowardRight = false;
};

/** Outcome of a MOBIL evaluation. */
struct FLRMobilResult
{
	/** Safe and the utility is positive. */
	bool bChange = false;

	/** Safety criterion (new follower deceleration and both gaps) holds. */
	bool bSafe = false;

	/** Incentive minus (threshold +/- bias), m/s^2: > 0 means the change is attractive. */
	float Utility = 0.f;
};

/**
 * Evaluates MOBIL:
 *   incentive = (a_c_new - a_c_old) + p * ((a_n_new - a_n_old) + (a_o_new - a_o_old))
 *   change if incentive > threshold - bias (toward right) or threshold + bias (toward left)
 *   safe if a_n_new >= -SafeDecel and both gaps >= MinGap.
 */
LRTRAFFIC_API FLRMobilResult EvaluateMobil(const FLRMobilParams& Params, const FLRMobilInput& Input);
