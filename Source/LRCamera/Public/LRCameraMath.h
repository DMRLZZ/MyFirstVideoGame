// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCameraTypes.h"
#include "LRCoreTypes.h"

struct FPostProcessSettings;

/** One sample of the procedural camera shake, applied in camera space. */
struct LRCAMERA_API FLRCameraShakeSample
{
	/** Location offset in camera space (X forward, Y right, Z up), centimeters. */
	FVector LocationCm = FVector::ZeroVector;

	/** Rotation offset, degrees. */
	FRotator Rotation = FRotator::ZeroRotator;
};

/**
 * Pure camera helpers (no UObject, no world): FOV curves, deterministic shake, look-ahead, horizon stabilization,
 * mode cycling and the Viral post-process mapping. Everything here is unit tested (Private/Tests).
 */
struct LRCAMERA_API FLRCameraMath
{
	/** Period of FMath::PerlinNoise1D (permutation table size); noise phases are wrapped to it to keep float precision. */
	static constexpr double PerlinPeriod = 256.0;

	/**
	 * Normalized speed factor: clamp((SpeedKmh - StartKmh) / (FullKmh - StartKmh), 0, 1)^Exponent.
	 * Monotonic non-decreasing in SpeedKmh; returns a step at StartKmh when FullKmh <= StartKmh.
	 */
	static float EvaluateSpeedAlpha(float SpeedKmh, float StartKmh, float FullKmh, float Exponent);

	/** Horizontal FOV for a speed, degrees (monotonic non-decreasing in speed, within [MinFovDeg, max(Min, Max)]). */
	static float EvaluateSpeedFov(const FLRSpeedFovCurve& Curve, float SpeedKmh);

	/** Cockpit FOV: FovDeg + SpeedFovExtraDeg * alpha(speed), clamped to [MinFovDeg, MaxFovDeg], degrees. */
	static float EvaluateCockpitFov(const FLRCockpitCameraParams& Params, float SpeedKmh);

	/**
	 * Amplitude multiplier of a shake profile: clamp(IdleScale + (|Speed| / ReferenceSpeedKmh)^SpeedExponent, 0, MaxScale)
	 * * Roughness * GlobalScale.
	 */
	static float ComputeShakeScale(const FLRCameraShakeParams& Params, float SpeedKmh, float Roughness, float GlobalScale);

	/**
	 * Deterministic shake: the same (Params, TimeSeconds, Scale) always returns the same sample (Perlin noise of time,
	 * one decorrelated phase per channel, two frequency bands). Amplitudes are multiplied by Scale.
	 * @param TimeSeconds  Shake clock, seconds (accumulated by the caller; may grow without precision loss).
	 */
	static FLRCameraShakeSample EvaluateShake(const FLRCameraShakeParams& Params, double TimeSeconds, float Scale);

	/**
	 * Look-ahead yaw toward the target lane and into the curve, degrees (+ = right).
	 * Distance L = max(SpeedMs * LookAheadSeconds, MinDistanceMeters);
	 * yaw = atan2(LateralErrorMeters, L) + CurvatureScale * atan(CurvaturePerM * L / 2), clamped to +-MaxYawDeg.
	 * @param LateralErrorMeters  Target lane center D minus the car D, meters (+ = target on the right).
	 * @param CurvaturePerM       Road curvature at the car, 1/m (+ = turning right).
	 */
	static float ComputeLookAheadYawDeg(const FLRLookAheadParams& Params, float LateralErrorMeters, float SpeedMs, float CurvaturePerM);

	/** Removes a fraction (0..1) of the pitch and roll of a body rotation; yaw is kept. */
	static FRotator StabilizeHorizon(const FRotator& BodyRotation, float Stabilization);

	/** Next mode in the cycle ChaseFar -> ChaseNear -> Hood -> Cockpit -> Bumper -> Viral -> ChaseFar. */
	static ELRCameraMode GetNextCameraMode(ELRCameraMode Mode);

	/** True for cameras inside the cabin: Cockpit, and Viral when the active preset is an interior one. */
	static bool IsInteriorMode(ELRCameraMode Mode, bool bViralPresetInterior);

	/** Parses "GoPro", "Dashcam" or "Phone" (case-insensitive). Returns false for anything else. */
	static bool ParseViralPreset(const FString& Text, ELRViralPreset& OutPreset);

	/** Name of a preset ("GoPro", "Dashcam", "Phone"). */
	static const TCHAR* GetViralPresetName(ELRViralPreset Preset);

	/**
	 * Writes the Viral overrides into PostProcessSettings and sets the matching bOverride_ flags.
	 * Fields with a negative value are skipped (the engine / volume value is kept).
	 */
	static void ApplyViralPostProcess(const FLRViralPostProcessParams& Params, FPostProcessSettings& InOutSettings);
};
