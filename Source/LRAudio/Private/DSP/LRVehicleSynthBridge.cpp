// Copyright LEFTRIGHT. All Rights Reserved.

#include "DSP/LRVehicleSynthBridge.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"

FLRVehicleSynthBridge::FLRVehicleSynthBridge()
	: Events(EventQueueCapacity)
{
}

FLRVehicleSynthBridge::~FLRVehicleSynthBridge()
{
	delete PendingProfile.exchange(nullptr, std::memory_order_acq_rel);
	delete RetiredProfile.exchange(nullptr, std::memory_order_acq_rel);
}

void FLRVehicleSynthBridge::SetParams(const FLRVehicleSynthParams& Params)
{
	Rpm.store(Params.Rpm, std::memory_order_relaxed);
	MaxRpm.store(Params.MaxRpm, std::memory_order_relaxed);
	Throttle.store(Params.Throttle, std::memory_order_relaxed);
	SpeedMs.store(Params.SpeedMs, std::memory_order_relaxed);
	RoadS.store(Params.RoadS, std::memory_order_relaxed);
	WheelbaseMeters.store(Params.WheelbaseMeters, std::memory_order_relaxed);
	bInterior.store(Params.bInterior, std::memory_order_relaxed);
	bEngineOn.store(Params.bEngineOn, std::memory_order_relaxed);
	bHasRoadS.store(Params.bHasRoadS, std::memory_order_relaxed);
}

bool FLRVehicleSynthBridge::PushEvent(const FLRVehicleSynthEvent& Event)
{
	return Events.Enqueue(Event);
}

void FLRVehicleSynthBridge::SubmitProfile(const FLREngineAudioProfile& Profile)
{
	ReclaimRetiredProfile();

	FLREngineAudioProfile* NewProfile = new FLREngineAudioProfile(Profile.Sanitized());
	delete PendingProfile.exchange(NewProfile, std::memory_order_acq_rel);
}

void FLRVehicleSynthBridge::ReclaimRetiredProfile()
{
	delete RetiredProfile.exchange(nullptr, std::memory_order_acq_rel);
}

bool FLRVehicleSynthBridge::HasRetiredProfile() const
{
	return RetiredProfile.load(std::memory_order_acquire) != nullptr;
}

int32 FLRVehicleSynthBridge::Render(float* OutAudio, int32 NumSamples, float SampleRate, int32 NumChannels)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLRVehicleSynthBridge::Render);

	if (!OutAudio || NumSamples <= 0)
	{
		return 0;
	}

	if (bRendering.exchange(true, std::memory_order_acquire))
	{
		FMemory::Memzero(OutAudio, static_cast<SIZE_T>(NumSamples) * sizeof(float));
		return NumSamples;
	}

	const int32 SafeNumChannels = FMath::Max(NumChannels, 1);

	// The render thread only takes a new profile once the game thread has freed the previous one, so it never has to
	// free anything itself.
	if (RetiredProfile.load(std::memory_order_acquire) == nullptr)
	{
		if (FLREngineAudioProfile* NewProfile = PendingProfile.exchange(nullptr, std::memory_order_acq_rel))
		{
			Synth.SwapProfile(*NewProfile);
			RetiredProfile.store(NewProfile, std::memory_order_release);
		}
	}

	FLRVehicleSynthParams Params;
	Params.Rpm = Rpm.load(std::memory_order_relaxed);
	Params.MaxRpm = MaxRpm.load(std::memory_order_relaxed);
	Params.Throttle = Throttle.load(std::memory_order_relaxed);
	Params.SpeedMs = SpeedMs.load(std::memory_order_relaxed);
	Params.RoadS = RoadS.load(std::memory_order_relaxed);
	Params.WheelbaseMeters = WheelbaseMeters.load(std::memory_order_relaxed);
	Params.bInterior = bInterior.load(std::memory_order_relaxed);
	Params.bEngineOn = bEngineOn.load(std::memory_order_relaxed);
	Params.bHasRoadS = bHasRoadS.load(std::memory_order_relaxed);
	Synth.SetParams(Params);

	// Prepared after the parameters so the smoothers start on the real values instead of ramping up from zero.
	if (PreparedSampleRate != SampleRate || PreparedNumChannels != SafeNumChannels)
	{
		Synth.Prepare(SampleRate, SafeNumChannels);
		PreparedSampleRate = SampleRate;
		PreparedNumChannels = SafeNumChannels;
		Synth.SetParams(Params);
	}

	FLRVehicleSynthEvent Event;
	while (Events.Dequeue(Event))
	{
		Synth.HandleEvent(Event);
	}

	const int32 NumFrames = NumSamples / SafeNumChannels;
	Synth.Render(OutAudio, NumFrames);

	// A buffer that is not a whole number of frames would leave stale samples at its end.
	for (int32 Index = NumFrames * SafeNumChannels; Index < NumSamples; ++Index)
	{
		OutAudio[Index] = 0.f;
	}

	bRendering.store(false, std::memory_order_release);
	return NumSamples;
}
