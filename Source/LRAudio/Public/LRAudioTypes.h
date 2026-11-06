// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LRAudioTypes.generated.h"

/**
 * One partial of the procedural engine tone.
 *
 * Order is relative to the FIRING frequency (rpm / 60 * cylinders / 2), in steps of 0.5:
 * 0.5 = half-order rumble (the uneven "lope" of a cross-plane V8), 1 = firing order, 2 = first overtone...
 * The gain blends from OffLoadGain (throttle closed, overrun) to OnLoadGain (full throttle).
 */
USTRUCT(BlueprintType)
struct LRAUDIO_API FLREngineHarmonic
{
	GENERATED_BODY()

	/** Multiple of the firing frequency (rounded to the nearest 0.5 at runtime), 0.5..16. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.5", ClampMax = "16.0"))
	float Order = 1.f;

	/** Linear amplitude of this partial with the throttle closed, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OffLoadGain = 0.3f;

	/** Linear amplitude of this partial at full throttle, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OnLoadGain = 0.5f;
};

/**
 * Complete tuning of the procedural vehicle sound (engine, wind, tyres, near-miss whoosh, crash, cabin mix).
 *
 * Loaded from Data/Audio/<VehicleDefinition.AudioProfile>.json (type "EngineAudioProfile"), with
 * Data/Audio/EngineAudio_Default.json as fallback. Owned by value by ULRVehicleAudioComponent; the audio render thread
 * receives its own copy through FLRVehicleSynthBridge. Every gain is linear; frequencies are in Hz; times in seconds;
 * speeds in km/h.
 */
USTRUCT(BlueprintType)
struct LRAUDIO_API FLREngineAudioProfile
{
	GENERATED_BODY()

	/** Maximum number of partials used by the synth; extra entries in Harmonics are ignored. */
	static constexpr int32 MaxHarmonics = 12;

	/** Unique id, equal to the file name without extension (e.g. "engine_i6_turbo"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName Id;

	// --- Engine ----------------------------------------------------------------------------------

	/** Number of cylinders of the four-stroke engine (firing frequency = rpm / 60 * cylinders / 2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "1", ClampMax = "16"))
	int32 CylinderCount = 6;

	/** Partials of the engine tone (at most MaxHarmonics are used). Empty = built-in default set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine")
	TArray<FLREngineHarmonic> Harmonics;

	/** Overall engine level, 0..2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float EngineGain = 0.5f;

	/** Engine level multiplier with the throttle closed (overrun is quieter than full load), 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OffLoadLevel = 0.55f;

	/** Engine level multiplier at idle rpm relative to the rev limiter, 0..1 (level rises linearly with rpm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IdleLevel = 0.45f;

	/** Low-pass cutoff of the engine tone with the throttle closed, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "100.0", ClampMax = "16000.0"))
	float OffLoadCutoffHz = 1200.f;

	/** Low-pass cutoff of the engine tone at full throttle, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "100.0", ClampMax = "16000.0"))
	float OnLoadCutoffHz = 4500.f;

	/** How much the tone cutoff follows rpm (0 = fixed, 1 = proportional to rpm / max rpm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CutoffRpmTracking = 0.5f;

	/** Pre-gain of the soft clipper that adds grit at high load, 0.5..8 (1 = almost clean). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float Drive = 1.6f;

	/** Random frequency deviation applied per engine cycle, fraction of the firing frequency, 0..0.05. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "0.05"))
	float CycleJitter = 0.004f;

	/** Random amplitude deviation applied per engine cycle, 0..0.5 (combustion roughness). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float AmplitudeJitter = 0.06f;

	/** Level of the intake roar (band-passed noise), 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IntakeNoiseGain = 0.12f;

	/** Center of the intake noise band, as a multiple of the firing frequency, 0.5..16. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.5", ClampMax = "16.0"))
	float IntakeNoiseOrder = 3.f;

	/** Quality factor of the intake noise band-pass, 0.3..10. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.3", ClampMax = "10.0"))
	float IntakeNoiseQ = 1.2f;

	/** Level of the exhaust pulse noise (low-passed noise gated by every firing), 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExhaustNoiseGain = 0.08f;

	/** Low-pass cutoff of the exhaust pulse noise, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "50.0", ClampMax = "8000.0"))
	float ExhaustNoiseCutoffHz = 500.f;

	/** Probability per firing of an overrun crackle (throttle closed, high rpm), 0..0.5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float OverrunCrackleProbability = 0.02f;

	/** Level of the overrun crackles, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OverrunCrackleGain = 0.3f;

	/** Crackles only above this fraction of the max rpm, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OverrunMinRpmFraction = 0.45f;

	/** The rev limiter stutter starts at this fraction of the max rpm, 0.8..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.8", ClampMax = "1.0"))
	float LimiterRpmFraction = 0.985f;

	/** Rate of the rev limiter fuel cut, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "2.0", ClampMax = "60.0"))
	float LimiterCutHz = 18.f;

	/** Time constant of the rpm smoothing inside the audio thread, seconds (keep short so gear shifts snap). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RpmSmoothingSeconds = 0.025f;

	/** Time constant of the throttle / load smoothing, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LoadSmoothingSeconds = 0.06f;

	/** Fade time of the engine when it is cut (crash) or restarted, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float EngineCutFadeSeconds = 0.15f;

	/** Max rpm used when the vehicle reports none, rpm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "1000.0", ClampMax = "20000.0"))
	float FallbackMaxRpm = 7000.f;

	// --- Wind ------------------------------------------------------------------------------------

	/** Wind level at WindReferenceSpeedKmh, 0..2 (scales with speed squared). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wind", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float WindGain = 0.28f;

	/** Speed at which the wind reaches WindGain, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wind", meta = (ClampMin = "20.0", ClampMax = "500.0"))
	float WindReferenceSpeedKmh = 250.f;

	/** Upper bound of the wind level scale (v / vref)^2, 0..4. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wind", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float WindMaxScale = 1.6f;

	/** Center of the wind noise band at standstill, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wind", meta = (ClampMin = "50.0", ClampMax = "8000.0"))
	float WindLowHz = 300.f;

	/** Center of the wind noise band at the reference speed, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wind", meta = (ClampMin = "50.0", ClampMax = "12000.0"))
	float WindHighHz = 1800.f;

	/** Quality factor of the wind band-pass, 0.3..10. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wind", meta = (ClampMin = "0.3", ClampMax = "10.0"))
	float WindQ = 0.7f;

	/** Depth of the slow gust modulation, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wind", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WindGustDepth = 0.25f;

	/** Rate of the gust modulation, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wind", meta = (ClampMin = "0.01", ClampMax = "5.0"))
	float WindGustRateHz = 0.3f;

	// --- Tyres and road --------------------------------------------------------------------------

	/** Tyre / road rumble level at TyreReferenceSpeedKmh, 0..2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tyres", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float TyreGain = 0.16f;

	/** Speed at which the rumble reaches TyreGain, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tyres", meta = (ClampMin = "10.0", ClampMax = "500.0"))
	float TyreReferenceSpeedKmh = 200.f;

	/** Exponent of the rumble level vs speed curve, 0.3..3. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tyres", meta = (ClampMin = "0.3", ClampMax = "3.0"))
	float TyreSpeedExponent = 1.f;

	/** Low-pass cutoff of the rumble noise, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tyres", meta = (ClampMin = "30.0", ClampMax = "4000.0"))
	float TyreCutoffHz = 180.f;

	/** Distance between expansion joints along S, meters (0 disables the thumps). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tyres", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float JointSpacingMeters = 12.f;

	/** Level of each expansion joint thump at the tyre reference speed, 0..2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tyres", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float JointGain = 0.45f;

	/** Pitch of the joint thump, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tyres", meta = (ClampMin = "20.0", ClampMax = "400.0"))
	float JointToneHz = 55.f;

	/** Decay time constant of each joint thump, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tyres", meta = (ClampMin = "0.005", ClampMax = "0.5"))
	float JointDecaySeconds = 0.05f;

	// --- Near-miss whoosh ------------------------------------------------------------------------

	/** Peak level of a full-intensity whoosh, 0..2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whoosh", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float WhooshGain = 0.8f;

	/** Duration of a whoosh, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whoosh", meta = (ClampMin = "0.05", ClampMax = "3.0"))
	float WhooshDurationSeconds = 0.45f;

	/** Band center at the start of the sweep, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whoosh", meta = (ClampMin = "100.0", ClampMax = "12000.0"))
	float WhooshStartHz = 2400.f;

	/** Band center at the end of the sweep, Hz (lower than the start: Doppler-like fall). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whoosh", meta = (ClampMin = "50.0", ClampMax = "12000.0"))
	float WhooshEndHz = 450.f;

	/** Quality factor of the whoosh band-pass, 0.3..10. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whoosh", meta = (ClampMin = "0.3", ClampMax = "10.0"))
	float WhooshQ = 1.4f;

	/** Relative speed that produces a full-intensity whoosh, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whoosh", meta = (ClampMin = "10.0", ClampMax = "500.0"))
	float WhooshReferenceRelativeKmh = 120.f;

	/** Intensity falls as exp(-clearance / falloff), meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whoosh", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float WhooshClearanceFalloffMeters = 1.5f;

	/** Maximum stereo pan of the whoosh toward the passed side, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whoosh", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WhooshMaxPan = 0.8f;

	/** Distance along the road, ahead and behind the player, searched for the passed vehicle to find its side, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whoosh", meta = (ClampMin = "1.0", ClampMax = "50.0"))
	float WhooshSideProbeRadiusMeters = 25.f;

	// --- Crash -----------------------------------------------------------------------------------

	/** Peak level of the crash burst, 0..2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float CrashGain = 1.f;

	/** Decay time of the crash burst, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash", meta = (ClampMin = "0.05", ClampMax = "5.0"))
	float CrashDurationSeconds = 1.2f;

	/** Low-pass cutoff of the crash noise, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash", meta = (ClampMin = "100.0", ClampMax = "16000.0"))
	float CrashCutoffHz = 2500.f;

	/** Pitch of the low body thump of the crash, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash", meta = (ClampMin = "20.0", ClampMax = "300.0"))
	float CrashThumpHz = 45.f;

	/** Impact speed that produces a full-intensity crash, km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crash", meta = (ClampMin = "5.0", ClampMax = "400.0"))
	float CrashReferenceKmh = 80.f;

	// --- Mix -------------------------------------------------------------------------------------

	/** Output level before the final soft clipper, 0..2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MasterGain = 0.8f;

	/** Cabin camera: low-pass cutoff applied to the engine, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix", meta = (ClampMin = "100.0", ClampMax = "20000.0"))
	float InteriorEngineCutoffHz = 1400.f;

	/** Cabin camera: engine level multiplier, 0..2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float InteriorEngineGain = 0.9f;

	/** Cabin camera: wind level multiplier, 0..2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float InteriorWindGain = 0.35f;

	/** Cabin camera: tyre and joint level multiplier (structure-borne boom), 0..2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float InteriorTyreGain = 1.2f;

	/** Cabin camera: whoosh level multiplier, 0..2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float InteriorWhooshGain = 0.6f;

	/** Crossfade time between the exterior and cabin mixes, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MixSmoothingSeconds = 0.25f;

	/** Returns a copy with every field clamped to its documented range (and default harmonics if empty). */
	FLREngineAudioProfile Sanitized() const;

	/** Built-in partial set used when Harmonics is empty (generic inline-six-like spectrum). */
	static TArray<FLREngineHarmonic> MakeDefaultHarmonics();

	/**
	 * Loads Data/Audio/<ProfileId>.json, falling back to Data/Audio/EngineAudio_Default.json and then to the C++
	 * defaults. Out always receives a sanitized, usable profile. Reads a file: call it from the game thread, not per tick.
	 * @return true only if the requested file itself was loaded.
	 */
	static bool LoadById(FName ProfileId, FLREngineAudioProfile& Out);

	/** Id of the fallback profile (Data/Audio/EngineAudio_Default.json). */
	static FName GetDefaultProfileId();
};
