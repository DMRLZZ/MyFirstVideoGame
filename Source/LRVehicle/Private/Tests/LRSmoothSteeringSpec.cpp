// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Assist/LRSmoothSteering.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRSmoothSteeringSpec, "LeftRight.Vehicle.SmoothSteering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
	FLRSmoothSteeringParams Params;
	FLRSmoothSteering::FLimits Limits;
	FLRSmoothSteering Steering;
	float D = 0.f;
	static constexpr float Dt = 1.f / 60.f;
	static constexpr float ForwardSpeed = 50.f;

	/** Simulates a car that takes exactly the lateral speed it is asked for. Returns the last speed. */
	float Run(float Input, float Seconds, float LaneCenter = 100.f)
	{
		float Speed = 0.f;
		const int32 Steps = FMath::RoundToInt32(Seconds / Dt);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Speed = Steering.Update(Params, Input, 1.f, Dt, D, ForwardSpeed, Speed, LaneCenter, Limits);
			D += Speed * Dt;
		}
		return Speed;
	}
END_DEFINE_SPEC(FLRSmoothSteeringSpec)

void FLRSmoothSteeringSpec::Define()
{
	BeforeEach([this]()
	{
		Params = FLRSmoothSteeringParams();
		Limits.MinD = -100.f;
		Limits.MaxD = 100.f;
		D = 0.f;
		Steering.Reset(D);
	});

	It("does not jump on the first frame of a key press", [this]()
	{
		const float First = Steering.Update(Params, 1.f, 1.f, Dt, D, ForwardSpeed, 0.f, 100.f, Limits);
		TestTrue(TEXT("Small first step"), First < 0.1f * Params.MaxLateralSpeedMs);
	});

	It("keeps moving for as long as the key is held", [this]()
	{
		Run(1.f, 1.f);
		const float AfterOneSecond = D;
		const float Speed = Run(1.f, 1.f);
		TestTrue(TEXT("Went on moving"), D > AfterOneSecond + 0.5f * Params.MaxLateralSpeedMs);
		TestEqual(TEXT("At full speed"), Speed, Params.MaxLateralSpeedMs, 0.01f);
	});

	It("gives a tap much less movement than a hold", [this]()
	{
		Run(1.f, 0.1f);
		Run(0.f, 1.f);
		const float Tap = FMath::Abs(D);

		D = 0.f;
		Steering.Reset(D);
		Run(1.f, 1.f);
		Run(0.f, 1.f);
		TestTrue(TEXT("Proportional"), Tap < 0.25f * FMath::Abs(D));
	});

	It("stops moving soon after the key is released and stays there", [this]()
	{
		Run(1.f, 1.f);
		Run(0.f, 1.f);
		const float Settled = D;
		const float Speed = Run(0.f, 3.f);
		TestEqual(TEXT("No speed left"), Speed, 0.f, 0.01f);
		TestEqual(TEXT("No drift"), D, Settled, 0.05f);
	});

	It("crosses to the other side without a jump", [this]()
	{
		float Previous = Run(1.f, 1.f);
		for (int32 Step = 0; Step < 120; ++Step)
		{
			const float Speed = Steering.Update(Params, -1.f, 1.f, Dt, D, ForwardSpeed, Previous, 100.f, Limits);
			D += Speed * Dt;
			if (!TestTrue(TEXT("Continuous"), FMath::Abs(Speed - Previous) <= Params.MaxLateralSpeedMs * Params.InputReversePerS * Dt * 1.5f))
			{
				return;
			}
			Previous = Speed;
		}
	});

	It("never goes past the edge of the road, however long the key is held", [this]()
	{
		Limits.MaxD = 5.f;
		Run(1.f, 20.f);
		TestTrue(TEXT("Inside"), D <= Limits.MaxD + 0.01f);
		TestTrue(TEXT("Close to the edge"), D > Limits.MaxD - 0.5f);

		Limits.MinD = -5.f;
		Run(-1.f, 20.f);
		TestTrue(TEXT("Inside on the other side"), D >= Limits.MinD - 0.01f);
	});

	It("comes back if it starts beyond the edge", [this]()
	{
		Limits.MaxD = 5.f;
		D = 6.f;
		Steering.Reset(D);
		Run(0.f, 5.f);
		TestTrue(TEXT("Back inside"), D <= 5.05f);
	});

	It("settles on a lane center that is close", [this]()
	{
		D = 0.3f;
		Steering.Reset(D);
		Run(0.f, 8.f, 0.f);
		TestEqual(TEXT("Centered"), D, 0.f, 0.05f);
	});

	It("leaves the car alone when the lane center is far", [this]()
	{
		D = 1.5f;
		Steering.Reset(D);
		Run(0.f, 8.f, 0.f);
		TestEqual(TEXT("Not pulled"), D, 1.5f, 0.05f);
	});

	It("does not pull the car back while it is still stopping after a release", [this]()
	{
		// A car with inertia: it keeps 5 m/s for half a second after the release, whatever is asked for.
		Run(1.f, 1.f);
		float Reached = D;
		for (int32 Step = 0; Step < 30; ++Step)
		{
			Steering.Update(Params, 0.f, 1.f, Dt, D, ForwardSpeed, 5.f, 100.f, Limits);
			D += 5.f * Dt;
		}
		Reached = D;
		Run(0.f, 3.f);
		TestEqual(TEXT("Stays where it stopped"), D, Reached, 0.2f);
	});

	It("goes to a position asked for with no input", [this]()
	{
		Steering.SetHoldTarget(3.7f);
		Run(0.f, 5.f, 3.7f);
		TestEqual(TEXT("Reached"), D, 3.7f, 0.05f);
	});

	It("scales with the sensitivity and with low speeds", [this]()
	{
		float Speed = 0.f;
		for (int32 Step = 0; Step < 120; ++Step)
		{
			Speed = Steering.Update(Params, 1.f, 0.5f, Dt, 0.f, ForwardSpeed, Speed, 100.f, Limits);
		}
		TestEqual(TEXT("Half sensitivity"), Speed, 0.5f * Params.MaxLateralSpeedMs, 0.01f);

		Speed = Steering.Update(Params, 1.f, 1.f, Dt, 0.f, 5.f, Speed, 100.f, Limits);
		TestEqual(TEXT("Limited at low speed"), Speed, 5.f * Params.LateralSpeedPerForwardSpeed, 0.01f);
	});
}

#endif
