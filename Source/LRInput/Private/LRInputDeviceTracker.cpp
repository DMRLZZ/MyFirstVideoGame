// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRInputDeviceTracker.h"

ELRInputDevice FLRInputDeviceTracker::ClassifyKey(const FKey& Key)
{
	return Key.IsGamepadKey() ? ELRInputDevice::Gamepad : ELRInputDevice::KeyboardMouse;
}

bool FLRInputDeviceTracker::NotifyKey(const FKey& Key, float AnalogValue)
{
	if (!Key.IsValid())
	{
		return false;
	}
	if (Key.IsAnalog() && FMath::Abs(AnalogValue) < AnalogActivationThreshold)
	{
		return false;
	}

	const ELRInputDevice Device = ClassifyKey(Key);
	if (Device == LastDevice)
	{
		return false;
	}
	LastDevice = Device;
	++ChangeCount;
	return true;
}

void FLRInputDeviceTracker::Reset(ELRInputDevice InitialDevice)
{
	LastDevice = InitialDevice;
	ChangeCount = 0;
}
