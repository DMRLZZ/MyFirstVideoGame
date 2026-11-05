// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRLaneChangeDetector.h"

#include "LRInputTypes.h"

void FLRLaneChangeDetector::Configure(const FLRInputTuning& Tuning)
{
	const FLRInputTuning Safe = Tuning.Sanitized();
	Threshold = Safe.LaneChangeThreshold;
	ReleaseThreshold = Safe.LaneChangeReleaseThreshold;
	RepeatDelaySeconds = Safe.LaneChangeRepeatDelaySeconds;
	Reset();
}

int32 FLRLaneChangeDetector::Update(float SteerAxis, float DeltaSeconds)
{
	const float Magnitude = FMath::Abs(SteerAxis);
	const int32 Direction = SteerAxis > 0.f ? 1 : (SteerAxis < 0.f ? -1 : 0);
	const float Dt = FMath::Max(0.f, DeltaSeconds);

	// Re-arm when released below the hysteresis band or when the stick flips to the other side.
	if (HeldDirection != 0 && (Magnitude < ReleaseThreshold || Direction != HeldDirection))
	{
		HeldDirection = 0;
		RepeatTimer = 0.f;
	}

	const bool bBeyondThreshold = Direction != 0 && Magnitude >= Threshold;

	if (HeldDirection == 0)
	{
		if (bBeyondThreshold)
		{
			HeldDirection = Direction;
			RepeatTimer = RepeatDelaySeconds;
			return Direction;
		}
		return 0;
	}

	// Held in the same direction: auto-repeat only while beyond the threshold.
	if (bBeyondThreshold && RepeatDelaySeconds > 0.f)
	{
		RepeatTimer -= Dt;
		if (RepeatTimer <= 0.f)
		{
			RepeatTimer += RepeatDelaySeconds;
			if (RepeatTimer <= 0.f)
			{
				// Very long frame: never fire more than once per update nor queue a burst.
				RepeatTimer = RepeatDelaySeconds;
			}
			return HeldDirection;
		}
	}
	return 0;
}

void FLRLaneChangeDetector::Reset()
{
	HeldDirection = 0;
	RepeatTimer = 0.f;
}
