// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/LRSynthDSP.h"
#include "LRAudioTypes.h"

/**
 * Snapshot of the vehicle state the synth follows. Written by the game thread (through the atomics of
 * FLRVehicleSynthBridge) and consumed once per audio buffer. SI units.
 */
struct FLRVehicleSynthParams
{
	/** Engine speed, rpm. */
	float Rpm = 0.f;

	/** Rev limiter of the engine, rpm (<= 0 uses the profile FallbackMaxRpm). */
	float MaxRpm = 0.f;

	/** Throttle applied to the engine, 0..1 (engine load). */
	float Throttle = 0.f;

	/** Signed forward speed, m/s. */
	float SpeedMs = 0.f;

	/** Distance along the road, meters (drives the expansion joint thumps). */
	double RoadS = 0.0;

	/** Distance between the axles, meters (second thump of each joint). */
	float WheelbaseMeters = 2.7f;

	/** Camera inside the cabin: apply the interior mix. */
	bool bInterior = false;

	/** Engine running (false after a severe crash or while wrecked). */
	bool bEngineOn = true;

	/** False until the vehicle is known (no road thumps from a stale S). */
	bool bHasRoadS = false;
};

/** One-shot sound requested by the game thread (crossing threads through a lock-free SPSC queue). */
struct FLRVehicleSynthEvent
{
	/** Kind of one-shot. */
	enum class EType : uint8
	{
		Whoosh,
		Crash,
		/** Silences every one-shot and resets the joint tracker (new run). */
		Reset
	};

	/** Kind of one-shot. */
	EType Type = EType::Whoosh;

	/** Strength, 0..1. */
	float Intensity = 0.f;

	/** Stereo position, -1 (left) .. +1 (right). */
	float Pan = 0.f;
};

/**
 * Complete procedural vehicle sound: engine (additive partials + intake / exhaust noise + jitter + soft clip),
 * wind (band-passed noise ~ v^2), tyre rumble with expansion-joint thumps from S, near-miss whooshes, crash burst
 * and the exterior / cabin mix. Pure C++: owned by FLRVehicleSynthBridge and driven from the audio render thread, or
 * directly by unit tests. Render, SetParams, SwapProfile and the Trigger functions never allocate or free memory.
 *
 * Threading: every method must be called from ONE thread at a time (the audio render thread in game).
 */
class LRAUDIO_API FLRVehicleSynth
{
public:
	/** Samples processed between control-rate updates (filter coefficients, partial gains). */
	static constexpr int32 ControlBlockFrames = 32;

	/** Simultaneous whoosh voices (the oldest is stolen). */
	static constexpr int32 MaxWhooshVoices = 3;

	FLRVehicleSynth();

	/** Sets the output format and resets every state. NumChannels: 1 = mono, 2+ = stereo on the first two channels. */
	void Prepare(float InSampleRate, int32 InNumChannels);

	/**
	 * Applies a profile that is already sanitized by exchanging it with the current one, so InOutProfile receives the
	 * previous profile. Neither allocates nor frees: this is the only profile entry point safe on the render thread.
	 */
	void SwapProfile(FLREngineAudioProfile& InOutProfile);

	/** Sanitizes and copies a profile. Allocates: game-thread setup and tests only. */
	void ApplyProfile(const FLREngineAudioProfile& InProfile);

	/** Sets the state to follow (smoothed internally). Non-finite values are replaced by safe ones. */
	void SetParams(const FLRVehicleSynthParams& InParams);

	/** Starts a near-miss whoosh. Intensity 0..1, Pan -1..1 toward the passed side. */
	void TriggerWhoosh(float Intensity, float Pan);

	/** Starts a crash burst. Intensity 0..1. */
	void TriggerCrash(float Intensity);

	/** Applies a one-shot event from the queue. */
	void HandleEvent(const FLRVehicleSynthEvent& Event);

	/** Silences one-shots and clears every filter / smoother state. */
	void Reset();

	/**
	 * Renders NumFrames frames of interleaved audio into Out (NumFrames * NumChannels floats), overwriting it.
	 * Every sample is finite and inside [-1, 1].
	 */
	void Render(float* Out, int32 NumFrames);

	/** Profile in use. */
	const FLREngineAudioProfile& GetProfile() const { return Profile; }

	/** Output sample rate, Hz. */
	float GetSampleRate() const { return SampleRate; }

	/** Number of output channels. */
	int32 GetNumChannels() const { return NumChannels; }

	/** Number of expansion joints crossed since the last Reset (tests, debug). */
	int32 GetJointCount() const { return JointCount; }

	/** Number of whoosh voices currently sounding. */
	int32 GetActiveWhooshCount() const;

	/**
	 * Whoosh intensity for a near miss: relative-speed factor (relative / reference, capped at 1.5) times
	 * exp(-clearance / falloff), clamped to [0, 1].
	 */
	static float ComputeWhooshIntensity(const FLREngineAudioProfile& Profile, float RelativeSpeedKmh, float ClearanceMeters);

	/** Crash intensity: impact / reference, clamped to [0, 1]. */
	static float ComputeCrashIntensity(const FLREngineAudioProfile& Profile, float ImpactSpeedKmh);

	/**
	 * Stereo position of a whoosh from road coordinates: +1 when the passed vehicle is to the right of the player
	 * (greater D, meters), -1 to the left, 0 when exactly aligned. The profile WhooshMaxPan limits it when triggering.
	 */
	static float ComputeWhooshPan(float PlayerD, float VehicleD);

private:
	/** One near-miss whoosh voice. */
	struct FWhooshVoice
	{
		bool bActive = false;
		float Age = 0.f;
		float Intensity = 0.f;
		float GainLeft = 0.f;
		float GainRight = 0.f;
		float Makeup = 1.f;
		FLRBiquad BandPass;
	};

	/** Derives the partial tables, smoother times and fixed coefficients from Profile and SampleRate. */
	void ConfigureFromProfile();

	/** Recomputes control-rate values (coefficients, gains) for the next block. */
	void UpdateControl();

	/** Engine layer for one sample (mono). */
	float RenderEngineSample();

	/** Advances the joint tracker one sample and returns the thump signal. */
	float RenderJointSample(float SpeedMs);

	FLREngineAudioProfile Profile;
	FLRVehicleSynthParams Params;

	float SampleRate = 48000.f;
	int32 NumChannels = 2;
	float InvSampleRate = 1.f / 48000.f;

	// Noise sources (independent streams).
	FLRNoise EngineNoise;
	FLRNoise WindNoiseLeft;
	FLRNoise WindNoiseRight;
	FLRNoise TyreNoise;
	FLRNoise OneShotNoise;

	// Smoothers.
	FLRParamSmoother RpmSmoother;
	FLRParamSmoother LoadSmoother;
	FLRParamSmoother SpeedSmoother;
	FLRParamSmoother EngineOnSmoother;
	FLRParamSmoother InteriorSmoother;

	// Engine.
	FLROscillatorBank Bank;
	float OffLoadGains[FLROscillatorBank::MaxPartials] = {};
	float OnLoadGains[FLROscillatorBank::MaxPartials] = {};
	float BankNormalization = 1.f;
	FLRBiquad EngineTone;
	FLRBiquad IntakeBand;
	float IntakeMakeup = 1.f;
	FLROnePoleLowPass ExhaustLowPass;
	float ExhaustMakeup = 1.f;
	float DriveNormalization = 1.f;
	FLROnePoleHighPass EngineDcBlock;
	FLROnePoleLowPass InteriorLowPass;
	float CycleFrequencyJitter = 0.f;
	float CycleAmplitude = 1.f;
	float LastFiringPhase = 0.f;
	float CrackleEnvelope = 0.f;
	float CrackleDecay = 0.f;
	float LimiterPhase = 0.f;
	float LimiterGate = 1.f;
	float LimiterGateCoefficient = 1.f;
	float EngineLevel = 0.f;
	float RpmNormalized = 0.f;
	float Load = 0.f;
	float FiringHz = 0.f;

	// Wind.
	FLRBiquad WindBandLeft;
	FLRBiquad WindBandRight;
	float WindMakeup = 1.f;
	float WindLevel = 0.f;
	float GustPhase = 0.f;
	float GustPhase2 = 0.f;

	// Tyres and joints.
	FLRBiquad TyreLowPass;
	float TyreMakeup = 1.f;
	float TyreLevel = 0.f;
	float JointSpeedScale = 0.f;
	double AudioRoadS = 0.0;
	int64 LastJointIndex = 0;
	bool bJointTrackerValid = false;
	float JointEnvelope = 0.f;
	float JointDecay = 0.f;
	float JointPhase = 0.f;
	float JointAmplitude = 0.f;
	float RearAxleDelaySamples = -1.f;
	FLROnePoleLowPass JointNoiseLowPass;
	int32 JointCount = 0;

	// One-shots.
	FWhooshVoice WhooshVoices[MaxWhooshVoices];
	bool bCrashActive = false;
	float CrashAge = 0.f;
	float CrashIntensity = 0.f;
	float CrashEnvelope = 0.f;
	float CrashDecay = 0.f;
	float CrashThumpPhase = 0.f;
	float CrashMakeup = 1.f;
	FLROnePoleLowPass CrashLowPass;

	// Mix (control rate).
	float EngineMix = 1.f;
	float WindMix = 1.f;
	float TyreMix = 1.f;
	float WhooshMix = 1.f;
};
