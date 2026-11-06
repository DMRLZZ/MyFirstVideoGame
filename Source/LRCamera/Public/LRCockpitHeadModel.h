// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCameraTypes.h"

/**
 * Driver head model for the cockpit camera (pure logic, no UObject).
 *
 * A mass-normalized spring-damper per channel (offset X/Y/Z, roll, pitch) is driven by the smoothed local acceleration
 * of the car: x'' = w^2 (target - x) - 2 zeta w x', w = 2 pi NaturalFrequencyHz. The targets are computed by
 * ComputeTargets() (head thrown opposite to the acceleration, scaled by Intensity and clamped).
 *
 * Frame-rate independence: the step is integrated with fixed semi-implicit Euler sub-steps of at most
 * MaxSubstepSeconds, and a frame is never simulated for more than MaxFrameSeconds (hitches do not explode the head).
 * The state is clamped to the configured limits (velocity cancelled against the clamp), so it cannot wind up.
 * The camera horizon stays stable because only the small head roll / pitch is added; the body roll and pitch are
 * partly removed by the camera manager (FLRCockpitCameraParams::HorizonStabilization).
 */
struct LRCAMERA_API FLRCockpitHeadModel
{
	/** Largest integration sub-step, seconds. */
	static constexpr float MaxSubstepSeconds = 1.f / 240.f;

	/** Longest frame simulated in one Update, seconds. */
	static constexpr float MaxFrameSeconds = 0.25f;

	/** Standard gravity used to convert m/s^2 to G. */
	static constexpr float GravityMs2 = 9.80665f;

	/** Head pose targets for one acceleration sample. */
	struct FTargets
	{
		/** Head offset in vehicle space (X forward, Y right, Z up), centimeters. */
		FVector OffsetCm = FVector::ZeroVector;

		/** Head roll, degrees (+ = right side down). */
		float RollDeg = 0.f;

		/** Head pitch, degrees (+ = up). */
		float PitchDeg = 0.f;
	};

	/** Stores the parameters (sanitized copy) without touching the state. */
	void Configure(const FLRHeadModelParams& InParams);

	/** Returns the head to rest (zero offset, zero velocity). */
	void Reset();

	/**
	 * Advances the model.
	 * @param LocalAccelerationMs2  Smoothed acceleration of the car in vehicle space (X forward, Y right, Z up), m/s^2.
	 * @param DeltaSeconds          Frame time, seconds (<= 0 leaves the state untouched).
	 */
	void Update(const FVector& LocalAccelerationMs2, float DeltaSeconds);

	/** Head offset in vehicle space, centimeters. */
	const FVector& GetOffsetCm() const { return OffsetCm; }

	/** Head roll, degrees. */
	float GetRollDeg() const { return static_cast<float>(AnglesDeg.X); }

	/** Head pitch, degrees. */
	float GetPitchDeg() const { return static_cast<float>(AnglesDeg.Y); }

	/** Current parameters. */
	const FLRHeadModelParams& GetParams() const { return Params; }

	/** Equilibrium pose for a constant acceleration (what the springs converge to). */
	static FTargets ComputeTargets(const FLRHeadModelParams& InParams, const FVector& LocalAccelerationMs2);

private:
	/** One semi-implicit Euler step of every channel. */
	void Substep(const FTargets& Targets, float Omega, float DampingCoeff, float H);

	/** Clamps one channel to +-Limit and cancels the velocity pushing it further out. */
	static void ClampChannel(double& Value, double& Velocity, double Limit);

	FLRHeadModelParams Params;

	/** Head offset, centimeters, and its velocity, cm/s. */
	FVector OffsetCm = FVector::ZeroVector;
	FVector OffsetVelocityCms = FVector::ZeroVector;

	/** X = roll, Y = pitch, degrees and degrees per second. */
	FVector2D AnglesDeg = FVector2D::ZeroVector;
	FVector2D AngularVelocityDegS = FVector2D::ZeroVector;
};
