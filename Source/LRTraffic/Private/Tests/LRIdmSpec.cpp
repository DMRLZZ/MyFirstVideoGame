// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Sim/LRIdm.h"

namespace LRIdmSpecPrivate
{
	/** Fixed step used by the integration tests, seconds (same rate as the traffic simulation). */
	constexpr float StepSeconds = 1.f / 60.f;

	FLRIdmParams MakeParams()
	{
		FLRIdmParams Params;
		Params.DesiredSpeedMs = 30.f;
		Params.TimeHeadwayS = 1.5f;
		Params.MinGapM = 2.f;
		Params.MaxAccelMs2 = 1.5f;
		Params.ComfortDecelMs2 = 2.f;
		return Params;
	}
}

BEGIN_DEFINE_SPEC(FLRIdmSpec, "LeftRight.Traffic.Idm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRIdmSpec)

void FLRIdmSpec::Define()
{
	using namespace LRIdmSpecPrivate;

	Describe("IdmAcceleration", [this]()
	{
		It("returns the maximum acceleration when standing on a free road", [this]()
		{
			const FLRIdmParams Params = MakeParams();
			TestNearlyEqual(TEXT("a(0) on a free road"), IdmAcceleration(Params, 0.f, 0.f, 0.f, false), Params.MaxAccelMs2, 1.0e-4f);
		});

		It("returns zero at the desired speed on a free road", [this]()
		{
			const FLRIdmParams Params = MakeParams();
			TestNearlyEqual(TEXT("a(v0) on a free road"), IdmAcceleration(Params, Params.DesiredSpeedMs, 0.f, 0.f, false), 0.f, 1.0e-4f);
		});

		It("clamps the deceleration to -9 m/s2 and floors the gap", [this]()
		{
			const FLRIdmParams Params = MakeParams();
			const float Accel = IdmAcceleration(Params, 40.f, 0.f, 40.f, true);
			TestEqual(TEXT("Clamped deceleration"), Accel, LRIdm::MaxDecelMs2);
			const float Negative = IdmAcceleration(Params, 10.f, -5.f, 0.f, true);
			TestTrue(TEXT("Negative gap gives a finite value"), FMath::IsFinite(Negative));
			TestTrue(TEXT("Negative gap brakes"), Negative < 0.f);
		});

		It("never exceeds the maximum acceleration", [this]()
		{
			const FLRIdmParams Params = MakeParams();
			TestTrue(TEXT("Leader far away and faster"), IdmAcceleration(Params, 5.f, 1000.f, -20.f, true) <= Params.MaxAccelMs2 + 1.0e-5f);
		});

		It("converges to the desired speed on a free road", [this]()
		{
			const FLRIdmParams Params = MakeParams();
			float Speed = 0.f;
			for (int32 Step = 0; Step < 120 * 60; ++Step)
			{
				Speed = FMath::Max(0.f, Speed + IdmAcceleration(Params, Speed, 0.f, 0.f, false) * StepSeconds);
				if (Speed > Params.DesiredSpeedMs + 1.0e-3f)
				{
					AddError(FString::Printf(TEXT("Overshoot: %f m/s"), Speed));
					return;
				}
			}
			TestNearlyEqual(TEXT("Speed after 120 s"), Speed, Params.DesiredSpeedMs, 0.05f);
		});

		It("stops behind a stopped obstacle without overlap", [this]()
		{
			const FLRIdmParams Params = MakeParams();
			const double ObstacleRear = 300.0;
			double Front = 0.0;
			float Speed = 30.f;
			double MinGap = TNumericLimits<double>::Max();

			for (int32 Step = 0; Step < 180 * 60; ++Step)
			{
				const double Gap = ObstacleRear - Front;
				const float Accel = IdmAcceleration(Params, Speed, static_cast<float>(Gap), Speed, true);
				Speed = FMath::Max(0.f, Speed + Accel * StepSeconds);
				Front += static_cast<double>(Speed) * StepSeconds;
				MinGap = FMath::Min(MinGap, ObstacleRear - Front);
			}

			const double FinalGap = ObstacleRear - Front;
			TestTrue(FString::Printf(TEXT("Never overlaps (min gap %.3f m)"), MinGap), MinGap > 0.5);
			TestTrue(FString::Printf(TEXT("Stopped (%.4f m/s)"), Speed), Speed < 0.2f);
			TestTrue(FString::Printf(TEXT("Stops near the jam distance (gap %.3f m)"), FinalGap), FinalGap >= 1.0 && FinalGap <= Params.MinGapM + 4.0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
