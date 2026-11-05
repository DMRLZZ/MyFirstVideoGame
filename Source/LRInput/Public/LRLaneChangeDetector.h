// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FLRInputTuning;

/**
 * Turns an analog steering axis into discrete lane change requests for the Arcade control scheme.
 * Pure logic (no UObject, no allocations); one instance per local player, owned by the player controller.
 *
 * Behavior:
 * - Fires once (returns -1 left / +1 right) when |Axis| crosses LaneChangeThreshold.
 * - Hysteresis: it re-arms only after |Axis| drops below LaneChangeReleaseThreshold (or the axis flips side).
 * - While held beyond the threshold in the same direction, it fires again every LaneChangeRepeatDelaySeconds
 *   (the repeat timer only runs while beyond the threshold; a delay of 0 disables repeat).
 * - At most one request per Update().
 */
struct LRINPUT_API FLRLaneChangeDetector
{
	/** Copies the thresholds from the (sanitized) tuning and resets the state. */
	void Configure(const FLRInputTuning& Tuning);

	/**
	 * Feeds the current steering axis value.
	 * @param SteerAxis     Steering in [-1, 1], + = right (already dead-zoned).
	 * @param DeltaSeconds  Frame time in seconds (negative values are treated as 0).
	 * @return -1 to request a lane change to the left, +1 to the right, 0 otherwise.
	 */
	int32 Update(float SteerAxis, float DeltaSeconds);

	/** Forgets any held direction and re-arms (e.g. on run reset or when the control scheme changes). */
	void Reset();

	/** Direction held since the last fire (-1/+1), or 0 when armed. */
	int32 GetHeldDirection() const { return HeldDirection; }

	/** True when the next threshold crossing will fire immediately. */
	bool IsArmed() const { return HeldDirection == 0; }

private:
	/** Normalized |axis| that fires a request. */
	float Threshold = 0.55f;

	/** Normalized |axis| under which the detector re-arms. */
	float ReleaseThreshold = 0.3f;

	/** Seconds between repeated requests while held; 0 disables repeat. */
	float RepeatDelaySeconds = 0.35f;

	/** Direction of the last fire while not re-armed, 0 when armed. */
	int32 HeldDirection = 0;

	/** Seconds left before the next repeat fire. */
	float RepeatTimer = 0.f;
};
