// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Parameters of the Intelligent Driver Model (Treiber, 2000) for one driver. Pure data, SI units.
 *
 *   a = a_max * [1 - (v / v0)^delta - (s* / gap)^2]
 *   s* = s0 + max(0, v*T + v*dv / (2*sqrt(a_max*b)))
 */
struct FLRIdmParams
{
	/** Desired (free road) speed v0, m/s. */
	float DesiredSpeedMs = 33.f;

	/** Safe time headway T, seconds. */
	float TimeHeadwayS = 1.5f;

	/** Jam distance s0 (minimum bumper-to-bumper gap), meters. */
	float MinGapM = 2.f;

	/** Maximum acceleration a_max, m/s^2. */
	float MaxAccelMs2 = 1.5f;

	/** Comfortable deceleration b (positive), m/s^2. */
	float ComfortDecelMs2 = 2.f;

	/** Free-road acceleration exponent delta. */
	float Delta = 4.f;
};

namespace LRIdm
{
	/** Strongest deceleration the model may return, m/s^2 (negative). */
	inline constexpr float MaxDecelMs2 = -9.f;

	/** Gaps are floored at this value before entering the interaction term, meters. */
	inline constexpr float MinGapFloorM = 0.1f;
}

/**
 * IDM acceleration of a vehicle, m/s^2, clamped to [LRIdm::MaxDecelMs2, Params.MaxAccelMs2].
 *
 * @param SpeedMs         Own speed (>= 0), m/s.
 * @param GapM            Bumper-to-bumper distance to the leader, meters (floored at 0.1 m). Ignored without leader.
 * @param ClosingSpeedMs  Own speed minus leader speed (positive when approaching), m/s.
 * @param bHasLeader      False on a free road: only the free-road term applies.
 */
LRTRAFFIC_API float IdmAcceleration(const FLRIdmParams& Params, float SpeedMs, float GapM, float ClosingSpeedMs, bool bHasLeader);
