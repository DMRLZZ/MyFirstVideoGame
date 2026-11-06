// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "Containers/CircularQueue.h"
#include "CoreMinimal.h"
#include "DSP/LRVehicleSynth.h"
#include "LRAudioTypes.h"

#include <atomic>

/**
 * Lock-free crossing between the game thread and the audio render thread for one FLRVehicleSynth.
 *
 * - Continuous values travel through relaxed std::atomic fields; the synth smooths them per sample.
 * - One-shots travel through a fixed-size single-producer / single-consumer ring (TCircularQueue).
 * - Profiles are heap objects handed over through two atomic pointers: the game thread allocates, the render thread
 *   exchanges contents with its current profile and hands the old one back, and the game thread frees it. The render
 *   thread never allocates, frees or blocks.
 *
 * Pure C++ with no UObject, so its lifetime is not tied to the component's garbage collection: it is shared by
 * ULRVehicleAudioComponent and the sound generator that the audio mixer may keep alive slightly longer.
 *
 * Threading: "game thread" functions must be called from one producer thread, Render from one consumer thread at a time.
 */
class LRAUDIO_API FLRVehicleSynthBridge
{
public:
	/** Size of the one-shot ring (rounded by the queue to a power of two; it holds one element less). */
	static constexpr uint32 EventQueueCapacity = 32;

	FLRVehicleSynthBridge();

	/** Frees the profiles still in flight. Only runs once no generator can render any more (shared ownership). */
	~FLRVehicleSynthBridge();

	FLRVehicleSynthBridge(const FLRVehicleSynthBridge&) = delete;
	FLRVehicleSynthBridge& operator=(const FLRVehicleSynthBridge&) = delete;

	// --- Game thread ---------------------------------------------------------------------------

	/** Publishes the state the synth must follow. No allocation, no lock. */
	void SetParams(const FLRVehicleSynthParams& Params);

	/**
	 * Queues a one-shot for the next audio buffer.
	 * @return false when the ring is full (the event is dropped: harmless for a sound effect).
	 */
	bool PushEvent(const FLRVehicleSynthEvent& Event);

	/**
	 * Sanitizes and copies a profile and publishes it. Allocates, so never call it per tick. A profile the render thread
	 * has not picked up yet is replaced and freed here.
	 */
	void SubmitProfile(const FLREngineAudioProfile& Profile);

	/** Frees the profile the render thread handed back, if any. Call it regularly (cheap when there is nothing to do). */
	void ReclaimRetiredProfile();

	/** True when a replaced profile is waiting for ReclaimRetiredProfile (tests). */
	bool HasRetiredProfile() const;

	// --- Audio render thread -------------------------------------------------------------------

	/**
	 * Applies pending parameters, profile and one-shots, then renders one buffer. Never allocates, frees or blocks.
	 * The synth is prepared on the first call and whenever the format changes.
	 * @param OutAudio     Interleaved output, NumSamples floats, overwritten.
	 * @param NumSamples   Total samples in the buffer (frames * channels).
	 * @param SampleRate   Output sample rate, Hz.
	 * @param NumChannels  Output channels (at least 1).
	 * @return NumSamples. The buffer is silent if another thread is still inside Render (generator restart).
	 */
	int32 Render(float* OutAudio, int32 NumSamples, float SampleRate, int32 NumChannels);

	/** Read access to the synth. Only valid from the render thread, or from tests when nothing is rendering. */
	const FLRVehicleSynth& GetSynth() const { return Synth; }

private:
	/** Rendering state. Only touched inside Render, after the constructor. */
	FLRVehicleSynth Synth;

	/** Output format the synth was prepared for (0 = not prepared yet). Only touched inside Render. */
	float PreparedSampleRate = 0.f;
	int32 PreparedNumChannels = 0;

	/** One-shots, game thread -> render thread. */
	TCircularQueue<FLRVehicleSynthEvent> Events;

	/** Next profile, game thread -> render thread. Owned by the bridge while here. */
	std::atomic<FLREngineAudioProfile*> PendingProfile{ nullptr };

	/** Replaced profile, render thread -> game thread. While it is set, the render thread takes no new profile. */
	std::atomic<FLREngineAudioProfile*> RetiredProfile{ nullptr };

	/** Guards against two generators rendering at once during a restart. */
	std::atomic<bool> bRendering{ false };

	// Published vehicle state (see FLRVehicleSynthParams for meaning and units).
	std::atomic<float> Rpm{ 0.f };
	std::atomic<float> MaxRpm{ 0.f };
	std::atomic<float> Throttle{ 0.f };
	std::atomic<float> SpeedMs{ 0.f };
	std::atomic<double> RoadS{ 0.0 };
	std::atomic<float> WheelbaseMeters{ 2.7f };
	std::atomic<bool> bInterior{ false };
	std::atomic<bool> bEngineOn{ true };
	std::atomic<bool> bHasRoadS{ false };
};
