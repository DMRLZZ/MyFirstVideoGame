// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Assist/LRLaneAssist.h"
#include "LRUnits.h"
#include "LRVehicleTypes.h"
#include "Misc/AutomationTest.h"

namespace LRLaneAssistSpecPrivate
{
	/**
	 * Plant measured in the running game: lateral acceleration proportional to the steering input, independent of speed,
	 * with a kinematic limit at low speed, a steering input slew and a first-order tire lag.
	 */
	struct FBicycleModel
	{
		/** Lateral acceleration per unit of steering input, m/s^2. */
		float AccelPerSteering = 15.f;

		/** Path curvature per unit of steering input, 1/m. */
		float CurvaturePerSteering = 0.25f;

		/** Steering input slew rate, 1/s (Chaos SteeringInputRate). 0 = instantaneous. */
		float SteeringRatePerS = 0.f;

		/** First-order lag of the lateral acceleration, seconds (tire relaxation). */
		float LagSeconds = 0.15f;
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
		float Accel = SpeedMs * YawRate;

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

			const float Authority = FMath::Min(Car.AccelPerSteering, SpeedMs * SpeedMs * Car.CurvaturePerSteering);
			Accel += (Authority * Steering - Accel) * (1.f - FMath::Exp(-DeltaSeconds / Car.LagSeconds));
			YawRate = Accel / SpeedMs;

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
			TestNearlyEqual(TEXT("Feed-forward only"), Steering, SpeedMs * SpeedMs * Curvature / Params.SteeringToLateralAccelMs2, 1.e-5f);
		});
	});

	Describe("ComputeSteeringAuthority", [this]()
	{
		It("is limited by the kinematic curvature at low speed and constant at road speed", [this]()
		{
			const FLRLaneAssistParams Params;
			TestNearlyEqual(TEXT("Kinematic at low speed"), FLRLaneAssist::ComputeSteeringAuthority(Params, 5.f), 25.f * Params.KinematicCurvaturePerSteering, 1.e-3f);
			TestNearlyEqual(TEXT("Measured value at 100 km/h"), FLRLaneAssist::ComputeSteeringAuthority(Params, 27.8f), Params.SteeringToLateralAccelMs2, 1.e-3f);
			TestNearlyEqual(TEXT("Same at 300 km/h"), FLRLaneAssist::ComputeSteeringAuthority(Params, 83.3f), Params.SteeringToLateralAccelMs2, 1.e-3f);
		});
	});

	Describe("Closed loop on the measured plant", [this]()
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
				Car.LagSeconds = 0.3f;
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

		It("settles in the same time at 100, 200 and 300 km/h (speed independent)", [this]()
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
