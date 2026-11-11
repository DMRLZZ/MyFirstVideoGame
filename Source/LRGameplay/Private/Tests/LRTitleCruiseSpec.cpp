// Copyright LEFTRIGHT. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "LRTitleCruise.h"
#include "LRTrafficTypes.h"

BEGIN_DEFINE_SPEC(FLRTitleCruiseSpec, "LeftRight.Gameplay.TitleCruise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRTitleCruiseSpec)

namespace LRTitleCruiseSpecPrivate
{
	FLRTrafficVehicleSnapshot MakeVehicle(double S, float D, float SpeedKmh, int32 Direction = 1)
	{
		FLRTrafficVehicleSnapshot Vehicle;
		Vehicle.S = S;
		Vehicle.D = D;
		Vehicle.SpeedMs = SpeedKmh / 3.6f;
		Vehicle.Direction = Direction;
		Vehicle.LengthMeters = 4.f;
		return Vehicle;
	}
}

void FLRTitleCruiseSpec::Define()
{
	using namespace LRTitleCruiseSpecPrivate;

	Describe("FindLeader", [this]()
	{
		It("picks the nearest vehicle ahead in the lane", [this]()
		{
			const TArray<FLRTrafficVehicleSnapshot> Traffic = {
				MakeVehicle(180.0, 1.75f, 80.f),
				MakeVehicle(140.0, 1.9f, 70.f),
				MakeVehicle(90.0, 1.75f, 60.f), // behind
				MakeVehicle(120.0, 5.25f, 50.f), // next lane
				MakeVehicle(110.0, 1.75f, 90.f, -1), // oncoming
			};
			float Gap = 0.f;
			float Speed = 0.f;
			TestTrue(TEXT("found"), FLRTitleCruise::FindLeader(Traffic, 100.0, 1.75f, 1.6f, Gap, Speed));
			TestEqual(TEXT("gap"), Gap, 36.f, 0.01f);
			TestEqual(TEXT("speed"), Speed, 70.f, 0.01f);
		});

		It("finds nothing on an empty lane", [this]()
		{
			const TArray<FLRTrafficVehicleSnapshot> Traffic = { MakeVehicle(150.0, 5.25f, 80.f) };
			float Gap = 0.f;
			float Speed = 0.f;
			TestFalse(TEXT("found"), FLRTitleCruise::FindLeader(Traffic, 100.0, 1.75f, 1.6f, Gap, Speed));
		});
	});

	Describe("ComputeTargetSpeedKmh", [this]()
	{
		It("cruises without a leader or with a far one", [this]()
		{
			TestEqual(TEXT("no leader"), FLRTitleCruise::ComputeTargetSpeedKmh(100.f, 45.f, -1.f, 0.f), 100.f);
			TestEqual(TEXT("far leader"), FLRTitleCruise::ComputeTargetSpeedKmh(100.f, 45.f, 95.f, 20.f), 100.f);
		});

		It("matches the leader at the follow gap", [this]()
		{
			TestEqual(TEXT("at gap"), FLRTitleCruise::ComputeTargetSpeedKmh(100.f, 45.f, 45.f, 70.f), 70.f);
		});

		It("slows below the leader when too close and never goes negative", [this]()
		{
			TestTrue(TEXT("below leader"), FLRTitleCruise::ComputeTargetSpeedKmh(100.f, 45.f, 30.f, 70.f) < 70.f);
			TestEqual(TEXT("very close"), FLRTitleCruise::ComputeTargetSpeedKmh(100.f, 45.f, 2.f, 10.f), 0.f);
		});

		It("never exceeds the cruise speed behind a fast leader", [this]()
		{
			TestEqual(TEXT("capped"), FLRTitleCruise::ComputeTargetSpeedKmh(100.f, 45.f, 60.f, 140.f), 100.f);
		});
	});

	Describe("ComputePedals", [this]()
	{
		It("accelerates below the target", [this]()
		{
			const FLRTitleCruisePedals Pedals = FLRTitleCruise::ComputePedals(60.f, 100.f);
			TestEqual(TEXT("full throttle"), Pedals.Throttle, 1.f);
			TestEqual(TEXT("no brake"), Pedals.Brake, 0.f);
		});

		It("holds a light throttle at the target", [this]()
		{
			const FLRTitleCruisePedals Pedals = FLRTitleCruise::ComputePedals(100.f, 100.f);
			TestTrue(TEXT("some throttle"), Pedals.Throttle > 0.f && Pedals.Throttle < 1.f);
			TestEqual(TEXT("no brake"), Pedals.Brake, 0.f);
		});

		It("brakes well above the target", [this]()
		{
			const FLRTitleCruisePedals Pedals = FLRTitleCruise::ComputePedals(100.f, 60.f);
			TestEqual(TEXT("no throttle"), Pedals.Throttle, 0.f);
			TestTrue(TEXT("brakes"), Pedals.Brake > 0.f);
		});

		It("does not push against a stop target", [this]()
		{
			const FLRTitleCruisePedals Pedals = FLRTitleCruise::ComputePedals(1.f, 0.f);
			TestEqual(TEXT("no throttle"), Pedals.Throttle, 0.f);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
