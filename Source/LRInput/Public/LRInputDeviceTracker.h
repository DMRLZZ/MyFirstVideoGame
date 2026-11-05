// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "LRInputTypes.h"

/**
 * Remembers which device family (keyboard/mouse or gamepad) the player used last, so the HUD can show matching prompts.
 * Pure logic: feed it every key event (e.g. from APlayerController::InputKey or an input preprocessor).
 * Analog keys only count once their magnitude reaches AnalogActivationThreshold, so stick drift does not flip prompts.
 */
struct LRINPUT_API FLRInputDeviceTracker
{
	/** Minimum |value| for an analog key (stick, trigger, mouse axis) to count as device usage. */
	float AnalogActivationThreshold = 0.25f;

	/** Classifies a key: gamepad keys (FKey::IsGamepadKey) -> Gamepad, everything else -> KeyboardMouse. */
	static ELRInputDevice ClassifyKey(const FKey& Key);

	/**
	 * Registers a key event.
	 * @param Key          The key that changed (invalid keys are ignored).
	 * @param AnalogValue  Axis value for analog keys; ignored for digital keys.
	 * @return true when the last used device changed because of this event.
	 */
	bool NotifyKey(const FKey& Key, float AnalogValue = 1.f);

	/** Last device family used. */
	ELRInputDevice GetLastDevice() const { return LastDevice; }

	/** Convenience: true when the last device used is a gamepad. */
	bool IsUsingGamepad() const { return LastDevice == ELRInputDevice::Gamepad; }

	/** Number of device switches since construction or Reset (lets polling widgets detect changes cheaply). */
	uint32 GetChangeCount() const { return ChangeCount; }

	/** Forces the current device and clears the change counter. */
	void Reset(ELRInputDevice InitialDevice = ELRInputDevice::KeyboardMouse);

private:
	/** Device family of the last accepted key event. */
	ELRInputDevice LastDevice = ELRInputDevice::KeyboardMouse;

	/** Device switches since construction or Reset. */
	uint32 ChangeCount = 0;
};
