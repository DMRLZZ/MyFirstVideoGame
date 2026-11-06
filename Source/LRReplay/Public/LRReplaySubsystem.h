// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LRReplayBuffer.h"
#include "LRReplayTypes.h"

#include "LRReplaySubsystem.generated.h"

struct FLRRunResetEvent;
struct FLRRunStateChangedEvent;

/**
 * Records the last seconds of the run into a ring buffer (one per game world).
 *
 * Automatic behavior (driven by the LRCore event bus):
 * - OnRunReset: clears the recording; sampling restarts at once if the run state is still Running (a restart
 *   does not change the state, so no state event would follow), otherwise it stops until the state becomes Running.
 * - Run state becomes Running: recording starts (or resumes on the same buffer).
 * - Run state becomes Crashed (or any other non-Running state): recording stops; the buffer is kept so the
 *   Replay state can play it back through EvaluateAt.
 *
 * Sampling uses a fixed 30 Hz accumulator (FLRReplaySettings::SampleRateHz). Each sample stores the player transform,
 * speed, rpm, inputs and the nearby traffic (+-TrafficRadiusMeters along S, nearest MaxTrafficVehicles).
 * After the first full buffer no memory is allocated (budget: < 0.1 ms per tick).
 *
 * Playback rendering (ghost actors, cinematic cameras, photo mode) is NOT implemented here yet; consumers read frames
 * through EvaluateAt / GetBuffer.
 */
UCLASS()
class LRREPLAY_API ULRReplaySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Returns the replay subsystem of the world that owns WorldContext, or nullptr (no world / unsupported world type). */
	static ULRReplaySubsystem* Get(const UObject* WorldContext);

	/** Clears the buffer and starts a new recording (recording clock restarts at 0). */
	void StartRecording();

	/** Stops sampling; recorded frames are kept until the next StartRecording / ClearRecording / run reset. */
	void StopRecording();

	/** Stops sampling and discards every recorded frame (keeps the allocations). */
	void ClearRecording();

	/** True while frames are being sampled. */
	bool IsRecording() const { return bRecording; }

	/** Number of frames in the buffer (0..capacity). */
	int32 GetNumFrames() const { return Buffer.Num(); }

	/** Seconds covered by the recorded frames. */
	float GetRecordedDuration() const { return Buffer.Duration(); }

	/**
	 * Interpolated state TimeFromStart seconds after the oldest recorded frame (clamped to the recorded range).
	 * Reuse OutFrame between calls to avoid allocations. Returns false if nothing is recorded.
	 */
	bool EvaluateAt(float TimeFromStart, FLRReplayFrame& OutFrame) const;

	/** Read-only access to the raw ring buffer (frames by chronological index). */
	const FLRReplayBuffer& GetBuffer() const { return Buffer; }

	/** Active recorder settings. */
	const FLRReplaySettings& GetSettings() const { return Settings; }

	/** Data file of the optional recorder settings, relative to Data/. */
	static const TCHAR* SettingsFile;

	//~ UTickableWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Bus callback: a new run starts. */
	void HandleRunReset(const FLRRunResetEvent& Event);

	/** Bus callback: the run state changed. */
	void HandleRunStateChanged(const FLRRunStateChangedEvent& Event);

	/** Starts sampling without clearing (used when the run enters Running with frames already recorded). */
	void ResumeRecording();

	/** Loads Data/Replay/Replay.json when it exists and (re)allocates the buffer. */
	void LoadSettingsAndAllocate();

	/** Fills ScratchFrame from the player pawn and traffic and appends it to the buffer. */
	void CaptureFrame(float Time);

	/** Recorder tunables. */
	FLRReplaySettings Settings;

	/** Recorded frames. */
	FLRReplayBuffer Buffer;

	/** Fixed-rate sampling clock. */
	FLRReplaySampleClock SampleClock;

	/** Frame being assembled this sample (preallocated, reused). */
	FLRReplayFrame ScratchFrame;

	/** Traffic snapshot scratch (reused; grows only up to the peak traffic count in the radius). */
	TArray<FLRTrafficVehicleSnapshot> TrafficScratch;

	/** True while sampling. */
	bool bRecording = false;

	/** Bus binding handles. */
	FDelegateHandle RunResetHandle;
	FDelegateHandle RunStateChangedHandle;
};
