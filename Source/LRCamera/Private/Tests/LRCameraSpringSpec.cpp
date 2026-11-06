// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRCameraSpring.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRCameraSpringSpec, "LeftRight.Camera.Spring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRCameraSpringSpec)

void FLRCameraSpringSpec::Define()
{
	Describe("Float spring", [this]()
	{
		It("converges to a fixed target", [this]()
		{
			FLRCriticallyDampedSpring Spring;
			Spring.Reset(0.f);
			for (int32 Step = 0; Step < 300; ++Step)
			{
				Spring.Update(10.f, 0.2f, 1.f / 60.f);
			}
			TestEqual(TEXT("Value"), Spring.Value, 10.f, 1.e-3f);
			TestEqual(TEXT("Velocity"), Spring.Velocity, 0.f, 1.e-3f);
		});

		It("never overshoots a fixed target", [this]()
		{
			FLRCriticallyDampedSpring Spring;
			Spring.Reset(0.f);
			float Previous = 0.f;
			bool bMonotonic = true;
			bool bOvershoot = false;
			for (int32 Step = 0; Step < 600; ++Step)
			{
				const float Value = Spring.Update(10.f, 0.5f, 1.f / 30.f);
				bMonotonic &= Value >= Previous - 1.e-5f;
				bOvershoot |= Value > 10.f + 1.e-4f;
				Previous = Value;
			}
			TestTrue(TEXT("Monotonic"), bMonotonic);
			TestFalse(TEXT("Overshoot"), bOvershoot);
		});

		It("is frame-rate independent", [this]()
		{
			FLRCriticallyDampedSpring OneStep;
			FLRCriticallyDampedSpring ManySteps;
			OneStep.Reset(0.f);
			ManySteps.Reset(0.f);

			OneStep.Update(100.f, 0.4f, 0.5f);
			for (int32 Step = 0; Step < 120; ++Step)
			{
				ManySteps.Update(100.f, 0.4f, 0.5f / 120.f);
			}
			TestEqual(TEXT("Value"), ManySteps.Value, OneStep.Value, 0.01f);
			TestEqual(TEXT("Velocity"), ManySteps.Velocity, OneStep.Velocity, 0.05f);
		});

		It("stays stable with a huge step", [this]()
		{
			FLRCriticallyDampedSpring Spring;
			Spring.Reset(0.f);
			Spring.Update(5.f, 0.01f, 100.f);
			TestEqual(TEXT("Value"), Spring.Value, 5.f, 1.e-3f);
		});

		It("snaps with a zero smoothing time and ignores a zero step", [this]()
		{
			FLRCriticallyDampedSpring Spring;
			Spring.Reset(1.f);
			Spring.Update(5.f, 0.2f, 0.f);
			TestEqual(TEXT("Zero step"), Spring.Value, 1.f);
			Spring.Update(5.f, 0.f, 1.f / 60.f);
			TestEqual(TEXT("Snap"), Spring.Value, 5.f);
			TestEqual(TEXT("Snap velocity"), Spring.Velocity, 0.f);
		});

		It("follows angles along the shortest arc", [this]()
		{
			FLRCriticallyDampedSpring Spring;
			Spring.Reset(170.f);
			bool bShortArc = true;
			for (int32 Step = 0; Step < 300; ++Step)
			{
				const float Value = Spring.UpdateAngleDegrees(-170.f, 0.2f, 1.f / 60.f);
				bShortArc &= FMath::Abs(Value) >= 169.9f;
			}
			TestTrue(TEXT("Crosses 180, not 0"), bShortArc);
			TestEqual(TEXT("Reaches the target"), FMath::FindDeltaAngleDegrees(Spring.Value, -170.f), 0.f, 0.01f);
		});
	});

	Describe("Vector spring", [this]()
	{
		It("converges on every axis and is frame-rate independent", [this]()
		{
			const FVector Target(100.0, -50.0, 25.0);

			FLRCriticallyDampedSpringVector OneStep;
			FLRCriticallyDampedSpringVector ManySteps;
			OneStep.Reset(FVector::ZeroVector);
			ManySteps.Reset(FVector::ZeroVector);

			OneStep.Update(Target, 0.3f, 0.25f);
			for (int32 Step = 0; Step < 60; ++Step)
			{
				ManySteps.Update(Target, 0.3f, 0.25f / 60.f);
			}
			TestTrue(TEXT("Same result"), OneStep.Value.Equals(ManySteps.Value, 1.e-3));

			OneStep.Update(Target, 0.3f, 10.f);
			TestTrue(TEXT("Converged"), OneStep.Value.Equals(Target, 1.e-3));
		});
	});

	Describe("Double spring", [this]()
	{
		It("keeps precision at large world coordinates", [this]()
		{
			FLRCriticallyDampedSpringDouble Spring;
			Spring.Reset(1.0e9);
			Spring.Update(1.0e9 + 100.0, 0.1f, 5.f);
			TestEqual(TEXT("Value"), Spring.Value, 1.0e9 + 100.0, 1.e-3);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
