// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Assist/LRLaneAssist.h"
#include "LRUnits.h"
#include "LRVehicleTypes.h"
#include "Misc/AutomationTest.h"

namespace LRLaneAssistSpecPrivate
{
	/** Kinematic bicycle model of a generic sports car (matches the default lane assist tuning). */
	struct FBicycleModel
	{
		/** Axle distance, meters. */
		float WheelbaseM = 2.7f;

		/** Front road-wheel angle at full steering input, radians (38 degrees). */
		float MaxSteerAngleRad = FMath::DegreesToRadians(38.f);

		/** Steering input slew rate, 1/s (Chaos SteeringInputRate). 0 = instantaneous. */
		float SteeringRatePerS = 0.f;

		/** First-order lag of the yaw rate behind the kinematic value, seconds (tire relaxation). 0 = none. */
		float YawLagSeconds = 0.f;
	};

	/** Result of one closed-loop simulation. */
	struct FSimResult
	{
		/** Largest excursion past the target in the direction of travel, meters (<= 0 means no overshoot). */
		float OvershootM = 0.f;

		/** Lateral error at the end of the simulation, meters. */
		float FinalErrorM = 0.f;

		/** Heading error at the end of the simulation, radians. */
		float FinalHeadingRad = 0.f;

		/** Time after which the car stays within 0.25 m of the target, seconds (-1 if never). */
		float SettleTimeS = -1.f;

		/** Largest absolute steering input produced by the assist. */
		float MaxSteering = 0.f;
	};

	/**
	 * Simulates a lane change on a road of constant curvature, in road coordinates.
	 * Heading error integrates (yaw rate - v * k), lateral position integrates v * sin(heading error).
	 */
	FSimResult SimulateLaneChange(const FLRLaneAssistParams& Params, const FBicycleModel& Car, float SpeedKmh, float StartD,
		float TargetD, float RoadCurvaturePerM = 0.f, float DurationS = 6.f, float DeltaSeconds = 1.f / 60.f)
	{
		const float SpeedMs = static_cast<float>(LRUnits::KmhToMs(SpeedKmh));
		const float Direction = FMath::Sign(TargetD - StartD);

		float D = StartD;
		float Heading = 0.f;
		float YawRate = SpeedMs * RoadCurvaturePerM;
		float Steering = 0.f;

		FSimResult Result;
		Result.OvershootM = -TNumericLimits<float>::Max();
		const int32 NumSteps = FMath::CeilToInt(DurationS / DeltaSeconds);
		for (int32 Step = 0; Step < NumSteps; ++Step)
		{
			const float Command = FLRLaneAssist::ComputeSteering(Params, D, TargetD, Heading, YawRate, SpeedMs, RoadCurvaturePerM);
			Result.MaxSteering = FMath::Max(Result.MaxSteering, FMath::Abs(Command));

			if (Car.SteeringRatePerS > 0.f)
			{
				const float MaxDelta = Car.SteeringRatePerS * DeltaSeconds;
				Steering += FMath::Clamp(Command - Steering, -MaxDelta, MaxDelta);
			}
			else
			{
				Steering = Command;
			}

			const float KinematicYawRate = SpeedMs / Car.WheelbaseM * FMath::Tan(Steering * Car.MaxSteerAngleRad);
			YawRate = Car.YawLagSeconds > 0.f
				? YawRate + (KinematicYawRate - YawRate) * (1.f - FMath::Exp(-DeltaSeconds / Car.YawLagSeconds))
				: KinematicYawRate;

			Heading += (YawRate - SpeedMs * RoadCurvaturePerM) * DeltaSeconds;
			D += SpeedMs * FMath::Sin(Heading) * DeltaSeconds;

			const float Excursion = (D - TargetD) * Direction;
			Result.OvershootM = FMath::Max(Result.OvershootM, Excursion);

			const float Time = static_cast<float>(Step + 1) * DeltaSeconds;
			if (FMath::Abs(D - TargetD) < 0.25f)
			{
				if (Result.SettleTimeS < 0.f)
				{
					Result.SettleTimeS = Time;
				}
			}
			else
			{
				Result.SettleTimeS = -1.f;
			}
		}

		Result.FinalErrorM = D - TargetD;
		Result.FinalHeadingRad = Heading;
		return Result;
	}

	/** Speeds required by the design: 100, 200 and 300 km/h. */
	const float TestSpeedsKmh[] = { 100.f, 200.f, 300.f };

	/** One lane, US standard width, meters. */
	constexpr float LaneWidthM = 3.7f;

	/** Maximum overshoot accepted by the design, meters. */
	constexpr float MaxOvershootM = 0.3f;
}

BEGIN_DEFINE_SPEC(FLRLaneAssistSpec, "LeftRight.Vehicle.LaneAssist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRLaneAssistSpec)

void FLRLaneAssistSpec::Define()
{
	using namespace LRLaneAssistSpecPrivate;

	Describe("ComputeSteering", [this]()
	{
		It("returns zero when disabled", [this]()
		{
			FLRLaneAssistParams Params;
			Params.bEnabled = false;
			TestEqual(TEXT("Disabled output"), FLRLaneAssist::ComputeSteering(Params, 0.f, LaneWidthM, 0.f, 0.f, 50.f), 0.f);
		});

		It("steers towards the target lane (+ right, - left)", [this]()
		{
			const FLRLaneAssistParams Params;
			TestTrue(TEXT("Target on the right -> steer right"), FLRLaneAssist::ComputeSteering(Params, 0.f, LaneWidthM, 0.f, 0.f, 50.f) > 0.f);
			TestTrue(TEXT("Target on the left -> steer left"), FLRLaneAssist::ComputeSteering(Params, 0.f, -LaneWidthM, 0.f, 0.f, 50.f) < 0.f);
			TestEqual(TEXT("Centered and aligned -> no steering"), FLRLaneAssist::ComputeSteering(Params, 1.85f, 1.85f, 0.f, 0.f, 50.f), 0.f);
		});

		It("counter-steers a heading error towards the road direction", [this]()
		{
			const FLRLaneAssistParams Params;
			TestTrue(TEXT("Pointing right -> steer left"), FLRLaneAssist::ComputeSteering(Params, 0.f, 0.f, 0.05f, 0.f, 50.f) < 0.f);
			TestTrue(TEXT("Pointing left -> steer right"), FLRLaneAssist::ComputeSteering(Params, 0.f, 0.f, -0.05f, 0.f, 50.f) > 0.f);
		});

		It("stays within the output clamp", [this]()
		{
			FLRLaneAssistParams Params;
			Params.MaxSteering = 0.4f;
			for (const float Speed : { 1.f, 5.f, 20.f, 60.f, 110.f })
			{
				for (const float HeadingError : { -1.f, -0.2f, 0.f, 0.2f, 1.f })
				{
					const float Steering = FLRLaneAssist::ComputeSteering(Params, -10.f, 10.f, HeadingError, 0.f, Speed);
					TestTrue(FString::Printf(TEXT("|steer| <= 0.4 at v=%.0f psi=%.1f"), Speed, HeadingError), FMath::Abs(Steering) <= 0.4f + KINDA_SMALL_NUMBER);
				}
			}
		});

		It("adds only the road curvature feed-forward when centered on a curve", [this]()
		{
			const FLRLaneAssistParams Params;
			const float Curvature = 1.f / 1500.f;
			const float SpeedMs = 60.f;
			const float Steering = FLRLaneAssist::ComputeSteering(Params, 0.f, 0.f, 0.f, SpeedMs * Curvature, SpeedMs, Curvature);
			TestNearlyEqual(TEXT("Feed-forward only"), Steering, Params.CurvatureFeedForward * Curvature, 1.e-5f);
		});
	});

	Describe("ComputeGainScale", [this]()
	{
		It("is non-increasing with speed and clamped", [this]()
		{
			const FLRLaneAssistParams Params;
			float Previous = TNumericLimits<float>::Max();
			for (float Speed = 0.f; Speed <= 120.f; Speed += 2.5f)
			{
				const float Scale = FLRLaneAssist::ComputeGainScale(Params, Speed);
				TestTrue(FString::Printf(TEXT("Monotonic at %.1f m/s"), Speed), Scale <= Previous + KINDA_SMALL_NUMBER);
				TestTrue(TEXT("Within clamps"), Scale >= Params.MinGainScale - KINDA_SMALL_NUMBER && Scale <= Params.MaxGainScale + KINDA_SMALL_NUMBER);
				Previous = Scale;
			}
			TestNearlyEqual(TEXT("Unscaled at the reference speed"), FLRLaneAssist::ComputeGainScale(Params, Params.ReferenceSpeedMs), 1.f, 1.e-5f);
		});
	});

	Describe("Closed loop on a kinematic bicycle model", [this]()
	{
		for (const float SpeedKmh : TestSpeedsKmh)
		{
			It(FString::Printf(TEXT("changes one lane to the right at %.0f km/h without overshoot"), SpeedKmh), [this, SpeedKmh]()
			{
				const FSimResult Result = SimulateLaneChange(FLRLaneAssistParams(), FBicycleModel(), SpeedKmh, 0.f, LaneWidthM);
				TestTrue(FString::Printf(TEXT("Overshoot %.3f m < %.2f m"), Result.OvershootM, MaxOvershootM), Result.OvershootM < MaxOvershootM);
				TestTrue(FString::Printf(TEXT("Final error %.3f m"), Result.FinalErrorM), FMath::Abs(Result.FinalErrorM) < 0.05f);
				TestTrue(FString::Printf(TEXT("Final heading %.4f rad"), Result.FinalHeadingRad), FMath::Abs(Result.FinalHeadingRad) < 0.01f);
				TestTrue(FString::Printf(TEXT("Settles within 2 s (%.2f s)"), Result.SettleTimeS), Result.SettleTimeS > 0.f && Result.SettleTimeS < 2.f);
			});

			It(FString::Printf(TEXT("changes one lane to the left at %.0f km/h without overshoot"), SpeedKmh), [this, SpeedKmh]()
			{
				const FSimResult Result = SimulateLaneChange(FLRLaneAssistParams(), FBicycleModel(), SpeedKmh, LaneWidthM, 0.f);
				TestTrue(FString::Printf(TEXT("Overshoot %.3f m < %.2f m"), Result.OvershootM, MaxOvershootM), Result.OvershootM < MaxOvershootM);
				TestTrue(FString::Printf(TEXT("Final error %.3f m"), Result.FinalErrorM), FMath::Abs(Result.FinalErrorM) < 0.05f);
			});

			It(FString::Printf(TEXT("changes two lanes at %.0f km/h without overshoot"), SpeedKmh), [this, SpeedKmh]()
			{
				const FSimResult Result = SimulateLaneChange(FLRLaneAssistParams(), FBicycleModel(), SpeedKmh, -LaneWidthM, LaneWidthM);
				TestTrue(FString::Printf(TEXT("Overshoot %.3f m < %.2f m"), Result.OvershootM, MaxOvershootM), Result.OvershootM < MaxOvershootM);
				TestTrue(FString::Printf(TEXT("Final error %.3f m"), Result.FinalErrorM), FMath::Abs(Result.FinalErrorM) < 0.05f);
			});

			It(FString::Printf(TEXT("tolerates steering slew and tire lag at %.0f km/h"), SpeedKmh), [this, SpeedKmh]()
			{
				FBicycleModel Car;
				Car.SteeringRatePerS = 8.f;
				Car.YawLagSeconds = 0.08f;
				const FSimResult Result = SimulateLaneChange(FLRLaneAssistParams(), Car, SpeedKmh, 0.f, LaneWidthM);
				TestTrue(FString::Printf(TEXT("Overshoot %.3f m < %.2f m"), Result.OvershootM, MaxOvershootM), Result.OvershootM < MaxOvershootM);
				TestTrue(FString::Printf(TEXT("Final error %.3f m"), Result.FinalErrorM), FMath::Abs(Result.FinalErrorM) < 0.05f);
			});

			It(FString::Printf(TEXT("changes lane inside a curve at %.0f km/h"), SpeedKmh), [this, SpeedKmh]()
			{
				for (const float Curvature : { 1.f / 1500.f, -1.f / 1000.f })
				{
					const FSimResult Result = SimulateLaneChange(FLRLaneAssistParams(), FBicycleModel(), SpeedKmh, 0.f, LaneWidthM, Curvature);
					TestTrue(FString::Printf(TEXT("Overshoot %.3f m (k=%.5f)"), Result.OvershootM, Curvature), Result.OvershootM < MaxOvershootM);
					TestTrue(FString::Printf(TEXT("Final error %.3f m (k=%.5f)"), Result.FinalErrorM, Curvature), FMath::Abs(Result.FinalErrorM) < 0.1f);
				}
			});
		}

		It("settles in the same time at 100, 200 and 300 km/h (speed scheduling)", [this]()
		{
			float MinTime = TNumericLimits<float>::Max();
			float MaxTime = 0.f;
			for (const float SpeedKmh : TestSpeedsKmh)
			{
				const FSimResult Result = SimulateLaneChange(FLRLaneAssistParams(), FBicycleModel(), SpeedKmh, 0.f, LaneWidthM);
				MinTime = FMath::Min(MinTime, Result.SettleTimeS);
				MaxTime = FMath::Max(MaxTime, Result.SettleTimeS);
			}
			TestTrue(FString::Printf(TEXT("Settle time spread %.3f s"), MaxTime - MinTime), MinTime > 0.f && MaxTime - MinTime < 0.2f);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
