// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Assist/LRStabilityAssist.h"
#include "LRVehicleTypes.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRStabilityAssistSpec, "LeftRight.Vehicle.StabilityAssist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRStabilityAssistSpec)

void FLRStabilityAssistSpec::Define()
{
	Describe("EvaluateSteeringScale", [this]()
	{
		It("is monotonically non-increasing with speed and stays in (0, 1]", [this]()
		{
			const FLRStabilityAssistParams Params;
			float Previous = 1.f;
			for (float SpeedKmh = 0.f; SpeedKmh <= 450.f; SpeedKmh += 5.f)
			{
				const float Scale = FLRStabilityAssist::EvaluateSteeringScale(Params, SpeedKmh);
				TestTrue(FString::Printf(TEXT("Non-increasing at %.0f km/h"), SpeedKmh), Scale <= Previous + KINDA_SMALL_NUMBER);
				TestTrue(FString::Printf(TEXT("In (0, 1] at %.0f km/h"), SpeedKmh), Scale > 0.f && Scale <= 1.f);
				Previous = Scale;
			}
		});

		It("is symmetric for reverse speeds", [this]()
		{
			const FLRStabilityAssistParams Params;
			TestNearlyEqual(TEXT("Scale(-120) == Scale(120)"), FLRStabilityAssist::EvaluateSteeringScale(Params, -120.f),
				FLRStabilityAssist::EvaluateSteeringScale(Params, 120.f), 1.e-6f);
		});

		It("interpolates linearly between curve points", [this]()
		{
			FLRStabilityAssistParams Params;
			Params.SteeringScaleCurve = { FVector2D(0.0, 1.0), FVector2D(100.0, 0.5) };
			TestNearlyEqual(TEXT("Midpoint"), FLRStabilityAssist::EvaluateSteeringScale(Params, 50.f), 0.75f, 1.e-5f);
			TestNearlyEqual(TEXT("Clamped past the last point"), FLRStabilityAssist::EvaluateSteeringScale(Params, 300.f), 0.5f, 1.e-5f);
		});

		It("returns 1 when disabled or when the curve is empty", [this]()
		{
			FLRStabilityAssistParams Params;
			Params.bSteeringScaleEnabled = false;
			TestEqual(TEXT("Disabled"), FLRStabilityAssist::EvaluateSteeringScale(Params, 250.f), 1.f);
			Params.bSteeringScaleEnabled = true;
			Params.SteeringScaleCurve.Reset();
			TestEqual(TEXT("Empty curve"), FLRStabilityAssist::EvaluateSteeringScale(Params, 250.f), 1.f);
		});
	});

	Describe("ComputeDesiredYawRate", [this]()
	{
		It("follows the kinematic bicycle model and is capped by grip", [this]()
		{
			const float SteerRad = FMath::DegreesToRadians(2.f);
			const float Kinematic = 20.f / 2.7f * FMath::Tan(SteerRad);
			TestNearlyEqual(TEXT("Kinematic at 20 m/s"), FLRStabilityAssist::ComputeDesiredYawRate(SteerRad, 20.f, 2.7f, 100.f), Kinematic, 1.e-5f);

			const float Capped = FLRStabilityAssist::ComputeDesiredYawRate(FMath::DegreesToRadians(20.f), 60.f, 2.7f, 9.f);
			TestNearlyEqual(TEXT("Grip cap a / v"), Capped, 9.f / 60.f, 1.e-5f);
		});
	});

	Describe("ComputeCounterSteer", [this]()
	{
		It("does nothing while the car follows the request", [this]()
		{
			const FLRStabilityAssistParams Params;
			TestEqual(TEXT("No correction"), FLRStabilityAssist::ComputeCounterSteer(Params, 0.1f, 0.1f, 0.f, 40.f), 0.f);
		});

		It("steers against excess yaw (oversteer to the right -> steer left)", [this]()
		{
			const FLRStabilityAssistParams Params;
			TestTrue(TEXT("Negative correction"), FLRStabilityAssist::ComputeCounterSteer(Params, 0.6f, 0.1f, 0.f, 40.f) < 0.f);
			TestTrue(TEXT("Positive correction (mirror)"), FLRStabilityAssist::ComputeCounterSteer(Params, -0.6f, -0.1f, 0.f, 40.f) > 0.f);
		});

		It("counter-steers into a slide and respects MaxCorrection", [this]()
		{
			const FLRStabilityAssistParams Params;
			const float Correction = FLRStabilityAssist::ComputeCounterSteer(Params, 0.f, 0.f, 0.5f, 40.f);
			TestTrue(TEXT("Steers towards the slide"), Correction > 0.f);
			TestTrue(TEXT("Clamped"), FMath::Abs(Correction) <= Params.MaxCorrection + KINDA_SMALL_NUMBER);
		});

		It("is inactive below MinSpeedMs or when disabled", [this]()
		{
			FLRStabilityAssistParams Params;
			TestEqual(TEXT("Too slow"), FLRStabilityAssist::ComputeCounterSteer(Params, 1.f, 0.f, 0.5f, Params.MinSpeedMs * 0.5f), 0.f);
			Params.bYawDampingEnabled = false;
			TestEqual(TEXT("Disabled"), FLRStabilityAssist::ComputeCounterSteer(Params, 1.f, 0.f, 0.5f, 40.f), 0.f);
		});
	});

	Describe("ComputeSteering", [this]()
	{
		It("reduces the effective steering at high speed", [this]()
		{
			const FLRStabilityAssistParams Params;
			const float Slow = FLRStabilityAssist::ComputeSteering(Params, 1.f, 5.f, 0.f, 0.f, 2.7f, 38.f);
			const float Fast = FLRStabilityAssist::ComputeSteering(Params, 1.f, 80.f, 0.f, 0.f, 2.7f, 38.f);
			TestTrue(FString::Printf(TEXT("Fast %.3f < slow %.3f"), Fast, Slow), Fast < Slow);
			TestTrue(TEXT("Output clamped"), FMath::Abs(Slow) <= 1.f && FMath::Abs(Fast) <= 1.f);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
