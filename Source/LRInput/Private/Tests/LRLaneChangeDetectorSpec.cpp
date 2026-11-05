// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRInputTypes.h"
#include "LRLaneChangeDetector.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRLaneChangeDetectorSpec, "LeftRight.Input.LaneChangeDetector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

	FLRLaneChangeDetector Detector;

	/** Tuning with binary-exact times so repeat timing is deterministic in float. */
	static FLRInputTuning MakeTuning()
	{
		FLRInputTuning Tuning;
		Tuning.LaneChangeThreshold = 0.5f;
		Tuning.LaneChangeReleaseThreshold = 0.25f;
		Tuning.LaneChangeRepeatDelaySeconds = 0.5f;
		return Tuning;
	}

END_DEFINE_SPEC(FLRLaneChangeDetectorSpec)

void FLRLaneChangeDetectorSpec::Define()
{
	BeforeEach([this]()
	{
		Detector = FLRLaneChangeDetector();
		Detector.Configure(MakeTuning());
	});

	Describe("SingleFire", [this]()
	{
		It("does not fire below the threshold", [this]()
		{
			TestEqual(TEXT("0.0"), Detector.Update(0.f, 0.016f), 0);
			TestEqual(TEXT("0.4 right"), Detector.Update(0.4f, 0.016f), 0);
			TestEqual(TEXT("0.49 left"), Detector.Update(-0.49f, 0.016f), 0);
			TestTrue(TEXT("Still armed"), Detector.IsArmed());
		});

		It("fires once to the right when crossing the threshold", [this]()
		{
			TestEqual(TEXT("Cross right"), Detector.Update(0.8f, 0.016f), 1);
			TestEqual(TEXT("Held next frame"), Detector.Update(0.8f, 0.016f), 0);
			TestEqual(TEXT("Held direction"), Detector.GetHeldDirection(), 1);
		});

		It("fires once to the left when crossing the negative threshold", [this]()
		{
			TestEqual(TEXT("Cross left"), Detector.Update(-1.f, 0.016f), -1);
			TestEqual(TEXT("Held next frame"), Detector.Update(-1.f, 0.016f), 0);
		});

		It("fires at exactly the threshold", [this]()
		{
			TestEqual(TEXT("Exactly 0.5"), Detector.Update(0.5f, 0.016f), 1);
		});
	});

	Describe("Hysteresis", [this]()
	{
		It("does not re-fire while inside the hysteresis band", [this]()
		{
			TestEqual(TEXT("Fire"), Detector.Update(0.9f, 0.016f), 1);
			TestEqual(TEXT("Into band"), Detector.Update(0.3f, 0.016f), 0);
			TestEqual(TEXT("Back beyond threshold"), Detector.Update(0.9f, 0.016f), 0);
			TestFalse(TEXT("Not armed"), Detector.IsArmed());
		});

		It("re-arms under the release threshold and fires again", [this]()
		{
			TestEqual(TEXT("Fire"), Detector.Update(0.9f, 0.016f), 1);
			TestEqual(TEXT("Release"), Detector.Update(0.1f, 0.016f), 0);
			TestTrue(TEXT("Armed"), Detector.IsArmed());
			TestEqual(TEXT("Fire again"), Detector.Update(0.9f, 0.016f), 1);
		});

		It("fires immediately in the opposite direction on a flick", [this]()
		{
			TestEqual(TEXT("Right"), Detector.Update(0.9f, 0.016f), 1);
			TestEqual(TEXT("Flick left"), Detector.Update(-0.9f, 0.016f), -1);
		});

		It("Reset re-arms", [this]()
		{
			TestEqual(TEXT("Fire"), Detector.Update(0.9f, 0.016f), 1);
			Detector.Reset();
			TestEqual(TEXT("Fire after reset"), Detector.Update(0.9f, 0.016f), 1);
		});
	});

	Describe("Repeat", [this]()
	{
		It("repeats every delay while held beyond the threshold", [this]()
		{
			TestEqual(TEXT("t=0 fire"), Detector.Update(1.f, 0.f), 1);
			TestEqual(TEXT("t=0.25"), Detector.Update(1.f, 0.25f), 0);
			TestEqual(TEXT("t=0.5 repeat"), Detector.Update(1.f, 0.25f), 1);
			TestEqual(TEXT("t=0.75"), Detector.Update(1.f, 0.25f), 0);
			TestEqual(TEXT("t=1.0 repeat"), Detector.Update(1.f, 0.25f), 1);
		});

		It("fires at most once per update on a long frame", [this]()
		{
			TestEqual(TEXT("Fire"), Detector.Update(-1.f, 0.f), -1);
			TestEqual(TEXT("Long frame"), Detector.Update(-1.f, 3.f), -1);
			TestEqual(TEXT("Next short frame"), Detector.Update(-1.f, 0.125f), 0);
		});

		It("does not advance the repeat timer inside the hysteresis band", [this]()
		{
			TestEqual(TEXT("Fire"), Detector.Update(1.f, 0.f), 1);
			TestEqual(TEXT("Band for a long time"), Detector.Update(0.3f, 2.f), 0);
			TestEqual(TEXT("Beyond again, timer intact"), Detector.Update(1.f, 0.25f), 0);
			TestEqual(TEXT("Delay reached"), Detector.Update(1.f, 0.25f), 1);
		});

		It("never repeats when the delay is zero", [this]()
		{
			FLRInputTuning Tuning = MakeTuning();
			Tuning.LaneChangeRepeatDelaySeconds = 0.f;
			Detector.Configure(Tuning);
			TestEqual(TEXT("Fire"), Detector.Update(1.f, 0.f), 1);
			for (int32 Frame = 0; Frame < 100; ++Frame)
			{
				if (Detector.Update(1.f, 0.1f) != 0)
				{
					AddError(TEXT("Repeated with zero delay"));
					break;
				}
			}
		});
	});

	Describe("Configure", [this]()
	{
		It("clamps a release threshold above the fire threshold", [this]()
		{
			FLRInputTuning Tuning = MakeTuning();
			Tuning.LaneChangeReleaseThreshold = 0.9f;
			Detector.Configure(Tuning);
			TestEqual(TEXT("Fire"), Detector.Update(0.6f, 0.016f), 1);
			TestEqual(TEXT("Below threshold re-arms"), Detector.Update(0.45f, 0.016f), 0);
			TestEqual(TEXT("Fire again"), Detector.Update(0.6f, 0.016f), 1);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
