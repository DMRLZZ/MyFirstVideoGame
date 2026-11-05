// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRReplayTypes.h"

/**
 * Fixed-rate sampling clock of the replay recorder (pure logic, no world).
 *
 * Accumulates variable frame deltas and reports when a sample is due. At most ONE sample is reported per call:
 * after a hitch the excess time is dropped instead of producing a burst of identical frames (frame timestamps
 * carry the real recording clock, so interpolation stays correct with irregular spacing).
 */
struct LRREPLAY_API FLRReplaySampleClock
{
	/** Sets the sampling rate (Hz, clamped to >= 1) and resets the clock. */
	void Configure(float SampleRateHz);

	/** Restarts the clock at Time = 0; the next Advance call reports a sample immediately. */
	void Reset();

	/**
	 * Advances the clock by DeltaSeconds (negative values are ignored).
	 * @return true if a sample must be recorded now (its timestamp is GetTime()).
	 */
	bool Advance(float DeltaSeconds);

	/** Seconds elapsed since the last Reset. */
	float GetTime() const { return Time; }

	/** Sampling period, seconds. */
	float GetPeriod() const { return Period; }

private:
	/** Sampling period, seconds. */
	float Period = 1.f / 30.f;

	/** Recording clock, seconds. */
	float Time = 0.f;

	/** Time accumulated since the last reported sample, seconds. */
	float Accumulator = 0.f;

	/** True until the first sample has been reported after a Reset. */
	bool bFirstSample = true;
};

/**
 * Ring buffer of replay frames (pure logic, no UObject, no world; owned by ULRReplaySubsystem).
 *
 * - Capacity is fixed by Configure (default 60 s x 30 Hz = 1800 frames). All frames and their Traffic arrays are
 *   preallocated there, so Add never allocates afterwards (frames are overwritten in place, oldest first).
 * - Frames must be added with strictly increasing Time.
 * - EvaluateAt interpolates between the two frames that bracket a time: player transform with lerp (location,
 *   scale) and slerp (rotation), scalars with lerp, and traffic vehicles matched by Id.
 *
 * Memory: Capacity * MaxTrafficPerFrame * sizeof(FLRTrafficVehicleSnapshot) (~5.5 MB with the defaults).
 */
class LRREPLAY_API FLRReplayBuffer
{
public:
	FLRReplayBuffer() = default;

	/**
	 * Allocates the storage: Capacity frames (clamped to >= 2), each with room for MaxTrafficPerFrame vehicles.
	 * Discards any recorded frame. This is the only function that allocates.
	 */
	void Configure(int32 Capacity, int32 MaxTrafficPerFrame);

	/** Convenience overload using FLRReplaySettings::GetFrameCapacity and MaxTrafficVehicles. */
	void Configure(const FLRReplaySettings& Settings);

	/**
	 * Copies Frame into the ring (overwriting the oldest frame when full). Traffic beyond MaxTrafficPerFrame is
	 * dropped (the caller should pass the most relevant vehicles first) and the stored copy is sorted by Id.
	 * @return false (frame ignored) if the buffer is not configured or Frame.Time is not greater than the newest frame time.
	 */
	bool Add(const FLRReplayFrame& Frame);

	/** Forgets every frame but keeps the allocations. */
	void Clear();

	/** Number of recorded frames (0..Capacity). */
	int32 Num() const { return NumFrames; }

	/** True when no frame is recorded. */
	bool IsEmpty() const { return NumFrames == 0; }

	/** Maximum number of frames kept. */
	int32 GetCapacity() const { return Frames.Num(); }

	/** Maximum traffic vehicles stored per frame. */
	int32 GetMaxTrafficPerFrame() const { return MaxTraffic; }

	/** Seconds covered by the recorded frames (newest Time - oldest Time); 0 with fewer than two frames. */
	float Duration() const;

	/** Recording-clock time of the oldest frame, seconds (0 when empty). */
	float GetOldestTime() const;

	/** Recording-clock time of the newest frame, seconds (0 when empty). */
	float GetNewestTime() const;

	/** Frame by chronological index: 0 = oldest, Num() - 1 = newest. Index must be valid. */
	const FLRReplayFrame& GetFrame(int32 ChronologicalIndex) const;

	/**
	 * Samples the recording at TimeFromStart seconds after the OLDEST frame (clamped to [0, Duration()]).
	 * Out.Time receives the recording-clock time of the sampled instant. Reuse Out between calls to avoid
	 * allocations (its Traffic array is reset, not freed).
	 * @return false if the buffer is empty.
	 */
	bool EvaluateAt(float TimeFromStart, FLRReplayFrame& OutFrame) const;

	/**
	 * Interpolates two frames (A at Alpha = 0, B at Alpha = 1). Both Traffic arrays must be sorted by Id.
	 * Vehicles present in both frames are interpolated; a vehicle present in only one frame is kept while that
	 * frame is the nearest (Alpha < 0.5 for A, Alpha >= 0.5 for B). Discrete fields (lights, direction, archetype,
	 * size, gear) come from the nearest frame. OutFrame must not alias A or B.
	 */
	static void InterpolateFrames(const FLRReplayFrame& A, const FLRReplayFrame& B, float Alpha, FLRReplayFrame& OutFrame);

	/** Interpolates one traffic vehicle (same Id expected), see InterpolateFrames. */
	static void InterpolateVehicle(const FLRTrafficVehicleSnapshot& A, const FLRTrafficVehicleSnapshot& B, float Alpha, FLRTrafficVehicleSnapshot& OutVehicle);

	/** Bytes currently allocated by the buffer (frame array + every Traffic array). Used to verify there is no growth. */
	SIZE_T GetAllocatedSize() const;

private:
	/** Storage index of a chronological index. */
	int32 ToStorageIndex(int32 ChronologicalIndex) const;

	/** Preallocated frames (size == capacity). */
	TArray<FLRReplayFrame> Frames;

	/** Storage index where the next frame is written. */
	int32 Head = 0;

	/** Number of valid frames. */
	int32 NumFrames = 0;

	/** Maximum vehicles stored per frame. */
	int32 MaxTraffic = 64;
};
