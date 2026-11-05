// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRInputCurves.h"
#include "LRInputTypes.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRInputCurvesSpec, "LeftRight.Input.Curves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRInputCurvesSpec)

void FLRInputCurvesSpec::Define()
{
	Describe("RemapDeadZone", [this]()
	{
		It("zeroes values inside the dead zone", [this]()
		{
			TestEqual(TEXT("0.1 in 0.15"), FLRInputCurves::RemapDeadZone(0.1f, 0.15f), 0.f);
			TestEqual(TEXT("-0.15 in 0.15"), FLRInputCurves::RemapDeadZone(-0.15f, 0.15f), 0.f);
		});

		It("remaps the rest linearly to 0..1 keeping the sign", [this]()
		{
			TestEqual(TEXT("Full right"), FLRInputCurves::RemapDeadZone(1.f, 0.2f), 1.f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Mid right"), FLRInputCurves::RemapDeadZone(0.6f, 0.2f), 0.5f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Mid left"), FLRInputCurves::RemapDeadZone(-0.6f, 0.2f), -0.5f, KINDA_SMALL_NUMBER);
		});

		It("clamps beyond the upper threshold", [this]()
		{
			TestEqual(TEXT("Over upper"), FLRInputCurves::RemapDeadZone(0.95f, 0.1f, 0.9f), 1.f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Over 1"), FLRInputCurves::RemapDeadZone(-2.f, 0.1f), -1.f, KINDA_SMALL_NUMBER);
		});

		It("passes values through when thresholds are degenerate", [this]()
		{
			TestEqual(TEXT("Upper <= lower"), FLRInputCurves::RemapDeadZone(0.4f, 0.5f, 0.5f), 0.4f, KINDA_SMALL_NUMBER);
		});
	});

	Describe("ApplyResponseCurve", [this]()
	{
		It("is linear with exponent 1 or invalid exponents", [this]()
		{
			TestEqual(TEXT("Exp 1"), FLRInputCurves::ApplyResponseCurve(0.3f, 1.f), 0.3f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Exp 0"), FLRInputCurves::ApplyResponseCurve(0.3f, 0.f), 0.3f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Exp -1"), FLRInputCurves::ApplyResponseCurve(-0.3f, -1.f), -0.3f, KINDA_SMALL_NUMBER);
		});

		It("is symmetric and keeps endpoints", [this]()
		{
			TestEqual(TEXT("0.5^2"), FLRInputCurves::ApplyResponseCurve(0.5f, 2.f), 0.25f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("-0.5^2"), FLRInputCurves::ApplyResponseCurve(-0.5f, 2.f), -0.25f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("1^1.5"), FLRInputCurves::ApplyResponseCurve(1.f, 1.5f), 1.f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("0^1.5"), FLRInputCurves::ApplyResponseCurve(0.f, 1.5f), 0.f, KINDA_SMALL_NUMBER);
		});
	});

	Describe("ShapeStickSteering", [this]()
	{
		It("applies dead zone then curve", [this]()
		{
			FLRInputTuning Tuning;
			Tuning.StickDeadZone = 0.2f;
			Tuning.SteeringExponent = 2.f;
			// 0.6 -> dead zone 0.5 -> curve 0.25.
			TestEqual(TEXT("Shaped"), FLRInputCurves::ShapeStickSteering(0.6f, Tuning), 0.25f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Inside dead zone"), FLRInputCurves::ShapeStickSteering(0.1f, Tuning), 0.f);
		});

		It("inverts when requested", [this]()
		{
			FLRInputTuning Tuning;
			Tuning.StickDeadZone = 0.f;
			Tuning.SteeringExponent = 1.f;
			Tuning.bInvertSteering = true;
			TestEqual(TEXT("Inverted"), FLRInputCurves::ShapeStickSteering(0.7f, Tuning), -0.7f, KINDA_SMALL_NUMBER);
		});
	});

	Describe("ShapeTrigger", [this]()
	{
		It("never returns negative values", [this]()
		{
			FLRInputTuning Tuning;
			TestEqual(TEXT("Negative raw"), FLRInputCurves::ShapeTrigger(-0.5f, Tuning), 0.f);
			TestEqual(TEXT("Full"), FLRInputCurves::ShapeTrigger(1.f, Tuning), 1.f, KINDA_SMALL_NUMBER);
			TestEqual(TEXT("Inside dead zone"), FLRInputCurves::ShapeTrigger(0.04f, Tuning), 0.f);
		});
	});

	Describe("ClampSteer", [this]()
	{
		It("clamps cumulative steer values", [this]()
		{
			TestEqual(TEXT("Over"), FLRInputCurves::ClampSteer(1.8f), 1.f);
			TestEqual(TEXT("Under"), FLRInputCurves::ClampSteer(-2.f), -1.f);
			TestEqual(TEXT("Inside"), FLRInputCurves::ClampSteer(0.3f), 0.3f);
		});
	});

	Describe("TuningSanitized", [this]()
	{
		It("clamps every field to its safe range", [this]()
		{
			FLRInputTuning Tuning;
			Tuning.StickDeadZone = 2.f;
			Tuning.TriggerDeadZone = -1.f;
			Tuning.SteeringExponent = 50.f;
			Tuning.LaneChangeThreshold = 0.4f;
			Tuning.LaneChangeReleaseThreshold = 0.8f;
			Tuning.LaneChangeRepeatDelaySeconds = -3.f;
			const FLRInputTuning Safe = Tuning.Sanitized();
			TestEqual(TEXT("StickDeadZone"), Safe.StickDeadZone, 0.95f);
			TestEqual(TEXT("TriggerDeadZone"), Safe.TriggerDeadZone, 0.f);
			TestEqual(TEXT("SteeringExponent"), Safe.SteeringExponent, 5.f);
			TestEqual(TEXT("Release <= threshold"), Safe.LaneChangeReleaseThreshold, 0.4f);
			TestEqual(TEXT("Delay"), Safe.LaneChangeRepeatDelaySeconds, 0.f);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
