// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FLRInputTuning;

/**
 * Pure, stateless helpers to shape raw analog axis values (no UObject, no world; unit-testable).
 *
 * They mirror the modifiers that ULRInputActions bakes into the gamepad mappings, so any code path
 * that reads raw hardware values (steering wheels, raw input replays, UI previews) gets the same feel.
 * Do NOT apply them again to the values delivered by the Steer/Throttle/Brake input actions: those are already shaped.
 */
struct LRINPUT_API FLRInputCurves
{
	/**
	 * Dead zone remap, identical to UInputModifierDeadZone (Axial):
	 * |Value| <= LowerThreshold -> 0, |Value| >= UpperThreshold -> 1, linear in between, sign preserved.
	 * Returns Value clamped to [-1, 1] when UpperThreshold <= LowerThreshold.
	 */
	static float RemapDeadZone(float Value, float LowerThreshold, float UpperThreshold = 1.f);

	/**
	 * Symmetric power response curve: sign(Value) * |Value|^Exponent, identical to UInputModifierResponseCurveExponential.
	 * Exponent <= 0 is treated as 1 (linear). Input is expected in [-1, 1].
	 */
	static float ApplyResponseCurve(float Value, float Exponent);

	/** Full stick steering chain: dead zone (StickDeadZone), response curve (SteeringExponent), optional invert. Output in [-1, 1]. */
	static float ShapeStickSteering(float RawAxis, const FLRInputTuning& Tuning);

	/** Full trigger chain: dead zone (TriggerDeadZone), clamped to [0, 1]. */
	static float ShapeTrigger(float RawAxis, const FLRInputTuning& Tuning);

	/**
	 * Clamps the Steer action value to [-1, 1]. Steer accumulates all its mappings (A + D gives 0),
	 * so a key plus the stick may exceed 1 before clamping.
	 */
	static float ClampSteer(float SteerActionValue) { return FMath::Clamp(SteerActionValue, -1.f, 1.f); }
};
