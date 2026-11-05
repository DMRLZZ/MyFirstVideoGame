// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "InputCoreTypes.h"
#include "LRInputDeviceTracker.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRInputDeviceTrackerSpec, "LeftRight.Input.DeviceTracker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRInputDeviceTrackerSpec)

void FLRInputDeviceTrackerSpec::Define()
{
	It("starts on keyboard and mouse", [this]()
	{
		const FLRInputDeviceTracker Tracker;
		TestEqual(TEXT("Initial device"), Tracker.GetLastDevice(), ELRInputDevice::KeyboardMouse);
		TestFalse(TEXT("Not gamepad"), Tracker.IsUsingGamepad());
	});

	It("classifies keys with FKey::IsGamepadKey", [this]()
	{
		TestEqual(TEXT("W"), FLRInputDeviceTracker::ClassifyKey(EKeys::W), ELRInputDevice::KeyboardMouse);
		TestEqual(TEXT("Mouse"), FLRInputDeviceTracker::ClassifyKey(EKeys::LeftMouseButton), ELRInputDevice::KeyboardMouse);
		TestEqual(TEXT("FaceButton"), FLRInputDeviceTracker::ClassifyKey(EKeys::Gamepad_FaceButton_Bottom), ELRInputDevice::Gamepad);
		TestEqual(TEXT("Stick"), FLRInputDeviceTracker::ClassifyKey(EKeys::Gamepad_LeftX), ELRInputDevice::Gamepad);
	});

	It("switches device and counts changes", [this]()
	{
		FLRInputDeviceTracker Tracker;
		TestFalse(TEXT("Same device"), Tracker.NotifyKey(EKeys::W));
		TestTrue(TEXT("To gamepad"), Tracker.NotifyKey(EKeys::Gamepad_FaceButton_Bottom));
		TestTrue(TEXT("Is gamepad"), Tracker.IsUsingGamepad());
		TestFalse(TEXT("Gamepad again"), Tracker.NotifyKey(EKeys::Gamepad_DPad_Left));
		TestTrue(TEXT("Back to keyboard"), Tracker.NotifyKey(EKeys::SpaceBar));
		TestEqual(TEXT("Change count"), Tracker.GetChangeCount(), 2u);
	});

	It("ignores small analog values and invalid keys", [this]()
	{
		FLRInputDeviceTracker Tracker;
		TestFalse(TEXT("Stick drift"), Tracker.NotifyKey(EKeys::Gamepad_LeftX, 0.1f));
		TestFalse(TEXT("Invalid key"), Tracker.NotifyKey(EKeys::Invalid));
		TestEqual(TEXT("Still keyboard"), Tracker.GetLastDevice(), ELRInputDevice::KeyboardMouse);
		TestTrue(TEXT("Real stick input"), Tracker.NotifyKey(EKeys::Gamepad_LeftX, -0.8f));
	});

	It("Reset forces the device and clears the counter", [this]()
	{
		FLRInputDeviceTracker Tracker;
		Tracker.NotifyKey(EKeys::Gamepad_FaceButton_Top);
		Tracker.Reset(ELRInputDevice::Gamepad);
		TestTrue(TEXT("Gamepad"), Tracker.IsUsingGamepad());
		TestEqual(TEXT("Counter cleared"), Tracker.GetChangeCount(), 0u);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
