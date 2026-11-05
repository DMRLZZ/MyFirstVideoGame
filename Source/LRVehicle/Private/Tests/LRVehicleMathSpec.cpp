// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRRoadTypes.h"
#include "LRVehicleMath.h"
#include "LRVehicleTypes.h"
#include "Misc/AutomationTest.h"

namespace LRVehicleMathSpecPrivate
{
	/** Builds lanes 3.7 m wide: NumOncoming lanes with Direction -1 on the left, NumPlayer lanes with +1 on the right. */
	TArray<FLRLaneDesc> MakeLanes(int32 NumOncoming, int32 NumPlayer)
	{
		TArray<FLRLaneDesc> Lanes;
		const int32 Total = NumOncoming + NumPlayer;
		for (int32 Index = 0; Index < Total; ++Index)
		{
			FLRLaneDesc& Lane = Lanes.AddDefaulted_GetRef();
			Lane.WidthMeters = 3.7f;
			Lane.CenterD = (static_cast<float>(Index) - (Total - 1) * 0.5f) * 3.7f;
			Lane.Direction = Index < NumOncoming ? -1 : 1;
		}
		return Lanes;
	}
}

BEGIN_DEFINE_SPEC(FLRVehicleMathSpec, "LeftRight.Vehicle.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRVehicleMathSpec)

void FLRVehicleMathSpec::Define()
{
	using namespace LRVehicleMathSpecPrivate;

	Describe("Lanes", [this]()
	{
		It("finds the nearest lane and applies hysteresis to the current lane", [this]()
		{
			const TArray<FLRLaneDesc> Lanes = MakeLanes(0, 4); // Centers -5.55, -1.85, 1.85, 5.55.
			TestEqual(TEXT("Nearest to 0.1"), LRVehicleMath::FindNearestLane(Lanes, 0.1f), 2);
			TestEqual(TEXT("Nearest to -9"), LRVehicleMath::FindNearestLane(Lanes, -9.f), 0);
			TestEqual(TEXT("No lanes"), LRVehicleMath::FindNearestLane(TArray<FLRLaneDesc>(), 0.f), static_cast<int32>(INDEX_NONE));

			// Just past the boundary (D = 0) the current lane is kept, well past it the lane switches.
			TestEqual(TEXT("Hysteresis keeps lane 1"), LRVehicleMath::UpdateCurrentLane(Lanes, 1, 0.05f, 0.2f), 1);
			TestEqual(TEXT("Switches to lane 2"), LRVehicleMath::UpdateCurrentLane(Lanes, 1, 0.3f, 0.2f), 2);
			TestEqual(TEXT("Invalid current -> nearest"), LRVehicleMath::UpdateCurrentLane(Lanes, INDEX_NONE, 0.05f, 0.2f), 2);
		});

		It("steps the target lane within the player's lanes on a one-way road", [this]()
		{
			const TArray<FLRLaneDesc> Lanes = MakeLanes(0, 4);
			TestEqual(TEXT("Right from 1"), LRVehicleMath::StepTargetLane(Lanes, 1, 1, false), 2);
			TestEqual(TEXT("Left from 1"), LRVehicleMath::StepTargetLane(Lanes, 1, -1, false), 0);
			TestEqual(TEXT("Clamped at the left edge"), LRVehicleMath::StepTargetLane(Lanes, 0, -1, false), 0);
			TestEqual(TEXT("Clamped at the right edge"), LRVehicleMath::StepTargetLane(Lanes, 3, 1, false), 3);
			TestEqual(TEXT("Direction magnitude is ignored"), LRVehicleMath::StepTargetLane(Lanes, 1, 5, false), 2);
		});

		It("only enters oncoming lanes when allowed (two-way overtaking)", [this]()
		{
			const TArray<FLRLaneDesc> Lanes = MakeLanes(2, 2); // 0,1 oncoming; 2,3 player.
			TestEqual(TEXT("Blocked without permission"), LRVehicleMath::StepTargetLane(Lanes, 2, -1, false), 2);
			TestEqual(TEXT("Allowed into oncoming"), LRVehicleMath::StepTargetLane(Lanes, 2, -1, true), 1);
			TestEqual(TEXT("Back to the player side"), LRVehicleMath::StepTargetLane(Lanes, 1, 1, false), 2);
		});
	});

	Describe("Drivetrain", [this]()
	{
		It("fades the throttle only near the top speed", [this]()
		{
			TestEqual(TEXT("Well below"), LRVehicleMath::ComputeTopSpeedThrottleScale(200.f, 250.f, 0.03f), 1.f);
			TestEqual(TEXT("At the limit"), LRVehicleMath::ComputeTopSpeedThrottleScale(250.f, 250.f, 0.03f), 0.f);
			const float Mid = LRVehicleMath::ComputeTopSpeedThrottleScale(246.25f, 250.f, 0.03f);
			TestNearlyEqual(TEXT("Halfway through the fade band"), Mid, 0.5f, 1.e-3f);
			TestEqual(TEXT("Reverse never limited"), LRVehicleMath::ComputeTopSpeedThrottleScale(-30.f, 250.f, 0.03f), 1.f);
		});

		It("computes engine rpm and picks a sensible starting gear", [this]()
		{
			FLRTransmissionParams Transmission;
			Transmission.FinalDriveRatio = 3.5f;
			Transmission.ForwardGearRatios = { 3.f, 2.f, 1.5f, 1.2f, 1.f, 0.8f };
			Transmission.ChangeUpRpm = 6500.f;

			// 30 m/s on a 0.33 m wheel in 6th: 30 / 0.33 * 0.8 * 3.5 rad/s.
			const float ExpectedRpm = 30.f / 0.33f * 0.8f * 3.5f * 60.f / UE_TWO_PI;
			TestNearlyEqual(TEXT("Rpm in 6th"), LRVehicleMath::ComputeEngineRpm(Transmission, 33.f, 30.f, 6), ExpectedRpm, 1.f);

			TestEqual(TEXT("Standstill -> 1st"), LRVehicleMath::SelectGearForSpeed(Transmission, 33.f, 0.f), 1);
			const int32 Gear = LRVehicleMath::SelectGearForSpeed(Transmission, 33.f, 40.f);
			TestTrue(TEXT("Selected gear keeps the engine below ChangeUpRpm"),
				LRVehicleMath::ComputeEngineRpm(Transmission, 33.f, 40.f, Gear) < Transmission.ChangeUpRpm);
			TestTrue(TEXT("Selected gear is the lowest valid"),
				Gear == 1 || LRVehicleMath::ComputeEngineRpm(Transmission, 33.f, 40.f, Gear - 1) >= Transmission.ChangeUpRpm);
		});
	});

	Describe("Crash", [this]()
	{
		It("projects the relative velocity on the contact normal", [this]()
		{
			// 10 m/s (1000 cm/s) head-on: 36 km/h.
			TestNearlyEqual(TEXT("Head-on"), LRVehicleMath::ComputeImpactSpeedKmh(FVector(1000.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0)), 36.f, 1.e-3f);
			// A pure side-swipe along the barrier has no normal component.
			TestNearlyEqual(TEXT("Grazing"), LRVehicleMath::ComputeImpactSpeedKmh(FVector(5000.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0)), 0.f, 1.e-3f);
			// Unnormalized normal is handled.
			TestNearlyEqual(TEXT("Unnormalized"), LRVehicleMath::ComputeImpactSpeedKmh(FVector(0.0, 1000.0, 0.0), FVector(0.0, 5.0, 0.0)), 36.f, 1.e-3f);
		});
	});

	Describe("Smoothing", [this]()
	{
		It("computes an exponential smoothing factor", [this]()
		{
			TestEqual(TEXT("No smoothing"), LRVehicleMath::ComputeSmoothingAlpha(0.016f, 0.f), 1.f);
			TestNearlyEqual(TEXT("One time constant"), LRVehicleMath::ComputeSmoothingAlpha(0.1f, 0.1f), 1.f - FMath::Exp(-1.f), 1.e-5f);
			TestEqual(TEXT("Zero dt"), LRVehicleMath::ComputeSmoothingAlpha(0.f, 0.1f), 0.f);
		});

		It("wraps angles to [-pi, pi]", [this]()
		{
			TestNearlyEqual(TEXT("3 pi -> pi"), FMath::Abs(LRVehicleMath::WrapAngleRad(3.f * UE_PI)), UE_PI, 1.e-4f);
			TestNearlyEqual(TEXT("-0.5 stays"), LRVehicleMath::WrapAngleRad(-0.5f), -0.5f, 1.e-6f);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
