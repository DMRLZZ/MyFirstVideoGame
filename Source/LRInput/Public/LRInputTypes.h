// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LRInputTypes.generated.h"

/**
 * Family of physical device the player used last. Drives which button prompts the HUD shows.
 * Mouse counts as KeyboardMouse.
 */
UENUM(BlueprintType)
enum class ELRInputDevice : uint8
{
	/** Keyboard (and mouse). */
	KeyboardMouse,
	/** Any gamepad key recognized by FKey::IsGamepadKey(). */
	Gamepad
};

/**
 * Tunable input parameters, loaded from Data/Input/Input.json (envelope type "InputTuning").
 *
 * All values are unitless normalized axis magnitudes in [0, 1] unless stated otherwise.
 * Missing JSON fields keep the C++ defaults below. Owned by value by whoever loads it
 * (typically the player controller) and copied into ULRInputActions / FLRLaneChangeDetector.
 */
USTRUCT(BlueprintType)
struct LRINPUT_API FLRInputTuning
{
	GENERATED_BODY()

	/** Inner dead zone of the analog sticks (normalized). Values below are ignored, the rest is remapped to 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float StickDeadZone = 0.15f;

	/** Inner dead zone of the analog triggers (normalized). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float TriggerDeadZone = 0.05f;

	/** Exponent of the stick steering response curve: out = sign(x) * |x|^Exponent. 1 = linear, > 1 = finer control near center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.2", ClampMax = "5.0"))
	float SteeringExponent = 1.5f;

	/** Arcade scheme: |Steer| must reach this value to request a lane change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float LaneChangeThreshold = 0.55f;

	/** Arcade scheme: |Steer| must drop below this value to re-arm the lane change detector (hysteresis). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LaneChangeReleaseThreshold = 0.3f;

	/** Arcade scheme: while the stick is held beyond the threshold, a new lane change is requested every N seconds. 0 disables repeat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.0", ClampMax = "5.0", Units = "Seconds"))
	float LaneChangeRepeatDelaySeconds = 0.35f;

	/** Inverts the analog stick steering axis (keyboard keys are never inverted). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bInvertSteering = false;

	/**
	 * Returns a copy with every field clamped to a safe range
	 * (dead zones in [0, 0.95], exponent in [0.2, 5], release threshold <= lane change threshold, delay in [0, 5] s).
	 */
	FLRInputTuning Sanitized() const;
};
