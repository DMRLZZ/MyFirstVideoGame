// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FLRVehicleDefinition;
struct FLRTransmissionParams;
struct FLRLaneDesc;

/**
 * Pure helpers of the player vehicle (no UObject, no world), shared by ALRVehicle and the unit tests.
 * All inputs are SI unless the parameter name says otherwise.
 */
namespace LRVehicleMath
{
	/**
	 * Evaluates a piecewise-linear curve given as points sorted by X (clamped at both ends).
	 * @return DefaultValue when Points is empty.
	 */
	LRVEHICLE_API float EvaluateCurve(TConstArrayView<FVector2D> Points, float X, float DefaultValue = 1.f);

	/** Wraps an angle to [-PI, PI], radians. */
	LRVEHICLE_API float WrapAngleRad(float AngleRad);

	/**
	 * Throttle multiplier of the electronic speed limiter: 1 below (1 - FadeFraction) * TopSpeedKmh, fading linearly to
	 * 0 at TopSpeedKmh. Negative speeds (reversing) are never limited.
	 */
	LRVEHICLE_API float ComputeTopSpeedThrottleScale(float SpeedKmh, float TopSpeedKmh, float FadeFraction);

	/**
	 * Engine speed for a given road speed and gear, rpm.
	 * @param Gear  1-based forward gear (values outside the ratio table are clamped).
	 */
	LRVEHICLE_API float ComputeEngineRpm(const FLRTransmissionParams& Transmission, float WheelRadiusCm, float SpeedMs, int32 Gear);

	/**
	 * Lowest forward gear whose engine speed at SpeedMs stays below ChangeUpRpm (top gear if none does).
	 * Used to start a run with a rolling start in a sensible gear. Returns 1 for speeds near zero.
	 */
	LRVEHICLE_API int32 SelectGearForSpeed(const FLRTransmissionParams& Transmission, float WheelRadiusCm, float SpeedMs);

	/**
	 * Impact speed of a contact, km/h: absolute relative velocity projected on the contact normal.
	 * @param RelativeVelocityCms  Own velocity minus the other body velocity, cm/s (world).
	 * @param Normal               Contact normal (any orientation, normalized internally).
	 */
	LRVEHICLE_API float ComputeImpactSpeedKmh(const FVector& RelativeVelocityCms, const FVector& Normal);

	/**
	 * True when a velocity change between two frames is a physical impact rather than braking or a frame hitch.
	 *
	 * @param DeltaVelocityKmh  Size of the horizontal velocity change, km/h.
	 * @param DeltaSeconds      Frame time, seconds.
	 * @param MinDeltaKmh       Smallest change considered an impact, km/h.
	 * @param MinAccelMs2       Smallest deceleration considered an impact, m/s^2.
	 */
	LRVEHICLE_API bool IsImpactSpike(float DeltaVelocityKmh, float DeltaSeconds, float MinDeltaKmh, float MinAccelMs2);

	/**
	 * True when the vehicle left the drivable corridor: fell below the surface or went past a barrier.
	 *
	 * @param D                 Lateral road coordinate, meters.
	 * @param HeightAboveRoadM  Vehicle height relative to the road surface, meters (negative = below).
	 * @param LeftBarrierD      Lateral position of the left barrier, meters.
	 * @param RightBarrierD     Lateral position of the right barrier, meters.
	 * @param BelowLimitM       Allowed distance below the surface, meters (positive).
	 * @param LateralMarginM    Allowed distance beyond a barrier, meters.
	 */
	LRVEHICLE_API bool IsOffRoad(float D, double HeightAboveRoadM, float LeftBarrierD, float RightBarrierD, float BelowLimitM, float LateralMarginM);

	/** Exponential smoothing factor for a time constant: 1 - exp(-Dt / Tau) (1 when Tau <= 0). */
	LRVEHICLE_API float ComputeSmoothingAlpha(float DeltaSeconds, float TimeConstantSeconds);

	/**
	 * Lane reported as "current" with hysteresis: switches from CurrentLane to the nearest lane only when the nearest
	 * lane center is closer to D than the current lane center by more than HysteresisMeters.
	 * @return the nearest lane when CurrentLane is invalid, INDEX_NONE when Lanes is empty.
	 */
	LRVEHICLE_API int32 UpdateCurrentLane(TConstArrayView<FLRLaneDesc> Lanes, int32 CurrentLane, float D, float HysteresisMeters);

	/** Nearest lane to D by center distance (INDEX_NONE when Lanes is empty). */
	LRVEHICLE_API int32 FindNearestLane(TConstArrayView<FLRLaneDesc> Lanes, float D);

	/**
	 * Target lane after a discrete lane change request (Arcade scheme).
	 * @param Direction        -1 left, +1 right (other values are clamped to the sign).
	 * @param bAllowOncoming   Two-way roads: oncoming lanes (Direction -1) are valid targets (overtaking).
	 * @return the new target, or TargetLane unchanged when the move would leave the allowed lanes.
	 */
	LRVEHICLE_API int32 StepTargetLane(TConstArrayView<FLRLaneDesc> Lanes, int32 TargetLane, int32 Direction, bool bAllowOncoming);

	/**
	 * Checks that a definition is physically plausible (masses, sizes, gear table, curves, wheel bones).
	 * @return true when no problem was found; every problem is appended to OutProblems in plain English.
	 */
	LRVEHICLE_API bool ValidateDefinition(const FLRVehicleDefinition& Definition, TArray<FString>& OutProblems);

	/** Theoretical rev-limited top speed in the highest gear, km/h. */
	LRVEHICLE_API float ComputeGearingTopSpeedKmh(const FLRVehicleDefinition& Definition);
}
