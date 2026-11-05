// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"
#include "LRTrafficTypes.h"

#include "LRReplayTypes.generated.h"

/**
 * Tunables of the replay recorder.
 *
 * Optionally loaded from Data/Replay/Replay.json (type "ReplaySettings"); when the file does not exist the
 * C++ defaults below are used (60 s at 30 Hz, traffic within +-250 m, at most 64 vehicles per frame).
 */
USTRUCT(BlueprintType)
struct LRREPLAY_API FLRReplaySettings
{
	GENERATED_BODY()

	/** Length of the ring buffer, seconds of gameplay kept (older frames are overwritten). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay", meta = (ClampMin = "1.0", ClampMax = "600.0"))
	float DurationSeconds = 60.f;

	/** Fixed sampling rate of the recorder, frames per second (Hz). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	float SampleRateHz = 30.f;

	/** Traffic vehicles are recorded only when |S - PlayerS| <= this radius, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay", meta = (ClampMin = "10.0", ClampMax = "2000.0"))
	float TrafficRadiusMeters = 250.f;

	/** Maximum traffic vehicles stored per frame (the nearest ones along S are kept). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay", meta = (ClampMin = "0", ClampMax = "256"))
	int32 MaxTrafficVehicles = 64;

	/** Number of frames the ring buffer must hold: ceil(DurationSeconds * SampleRateHz), at least 2. */
	int32 GetFrameCapacity() const
	{
		const float SafeDuration = FMath::Max(DurationSeconds, 0.f);
		const float SafeRate = FMath::Max(SampleRateHz, 0.f);
		return FMath::Max(2, FMath::CeilToInt32(SafeDuration * SafeRate));
	}

	/** Sampling period, seconds (1 / SampleRateHz, guarded against zero). */
	float GetSamplePeriodSeconds() const
	{
		return 1.f / FMath::Max(SampleRateHz, 1.f);
	}
};

/**
 * One recorded instant of a run: the player car and the nearby traffic.
 *
 * Frames live inside FLRReplayBuffer, which owns them and reuses their Traffic arrays (no allocation after warm-up).
 * Traffic is stored sorted by ascending Id so two frames can be matched with a linear merge.
 */
USTRUCT(BlueprintType)
struct LRREPLAY_API FLRReplayFrame
{
	GENERATED_BODY()

	/** Recording clock of this frame, seconds since the recording started. Strictly increasing inside a buffer. */
	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	float Time = 0.f;

	/** World transform of the player vehicle (location in cm, rotation, scale). */
	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	FTransform PlayerTransform = FTransform::Identity;

	/** Road coordinate of the player (S, D in meters). */
	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	FLRRoadCoord PlayerRoadCoord;

	/** Player speed, km/h. */
	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	float PlayerSpeedKmh = 0.f;

	/** Engine speed, revolutions per minute. */
	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	float EngineRpm = 0.f;

	/** Steering applied to the wheels after assists, -1..1 (drives the steering wheel / front wheel visuals). */
	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	float SteeringInput = 0.f;

	/** Throttle applied, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	float ThrottleInput = 0.f;

	/** Brake input, 0..1 (drives the brake lights). */
	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	float BrakeInput = 0.f;

	/** Engaged gear (0 = neutral, negative = reverse). */
	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	int32 Gear = 0;

	/** Nearby traffic (within the configured radius, at most MaxTrafficVehicles), sorted by ascending Id. */
	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	TArray<FLRTrafficVehicleSnapshot> Traffic;
};
