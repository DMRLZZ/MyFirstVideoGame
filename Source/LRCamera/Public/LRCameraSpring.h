// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Critically damped spring (pure logic, no UObject), used to smooth every camera quantity.
 *
 * Uses the closed-form solution of x'' = -w^2 (x - target) - 2 w x' with w = 2 / SmoothTime, so the result is exact
 * for a target that is constant during the step: N small steps give the same result as one big step
 * (frame-rate independent, unconditionally stable, never overshoots a fixed target).
 * SmoothTime is roughly the time to cover most of the distance (about 0.6 of it after SmoothTime, 99 % after 3.3x).
 * A SmoothTime <= 0 snaps to the target.
 *
 * ValueType must support +, -, and multiplication by double (float, double, FVector).
 * Owned by value by its user; Reset() before the first Update() to avoid a jump from zero.
 */
template <typename ValueType>
struct TLRCriticallyDampedSpring
{
	/** Current (smoothed) value. */
	ValueType Value = ValueType(0);

	/** Current rate of change, units per second. */
	ValueType Velocity = ValueType(0);

	/** Teleports the spring to InValue with zero velocity. */
	void Reset(const ValueType& InValue)
	{
		Value = InValue;
		Velocity = ValueType(0);
	}

	/**
	 * Advances the spring toward Target.
	 * @param SmoothTime    Smoothing time, seconds (<= 0 snaps).
	 * @param DeltaSeconds  Step, seconds (<= 0 leaves the state untouched).
	 * @return The new value.
	 */
	const ValueType& Update(const ValueType& Target, float SmoothTime, float DeltaSeconds)
	{
		Step(Value, Velocity, Target, SmoothTime, DeltaSeconds);
		return Value;
	}

	/** Stateless form of Update(). */
	static void Step(ValueType& InOutValue, ValueType& InOutVelocity, const ValueType& Target, float SmoothTime, float DeltaSeconds)
	{
		if (SmoothTime <= UE_KINDA_SMALL_NUMBER)
		{
			InOutValue = Target;
			InOutVelocity = ValueType(0);
			return;
		}
		if (DeltaSeconds <= 0.f)
		{
			return;
		}

		const double Omega = 2.0 / static_cast<double>(SmoothTime);
		const double Dt = static_cast<double>(DeltaSeconds);
		const double Decay = FMath::Exp(-Omega * Dt);

		const ValueType Delta = static_cast<ValueType>(InOutValue - Target);
		const ValueType Temp = static_cast<ValueType>((InOutVelocity + Delta * Omega) * Dt);
		InOutVelocity = static_cast<ValueType>((InOutVelocity - Temp * Omega) * Decay);
		InOutValue = static_cast<ValueType>(Target + (Delta + Temp) * Decay);
	}
};

/** Float critically damped spring with an angle-aware update (degrees, shortest path). */
struct FLRCriticallyDampedSpring : public TLRCriticallyDampedSpring<float>
{
	/**
	 * Smooths an angle in degrees toward TargetDeg along the shortest arc. Value stays normalized to (-180, 180].
	 * @return The new angle, degrees.
	 */
	float UpdateAngleDegrees(float TargetDeg, float SmoothTime, float DeltaSeconds)
	{
		const float UnwoundTarget = Value + FMath::FindDeltaAngleDegrees(Value, TargetDeg);
		Update(UnwoundTarget, SmoothTime, DeltaSeconds);
		Value = FMath::UnwindDegrees(Value);
		return Value;
	}
};

/** Double critically damped spring (world heights in centimeters). */
using FLRCriticallyDampedSpringDouble = TLRCriticallyDampedSpring<double>;

/** Vector critically damped spring (component-wise, same smoothing time on every axis). */
using FLRCriticallyDampedSpringVector = TLRCriticallyDampedSpring<FVector>;
