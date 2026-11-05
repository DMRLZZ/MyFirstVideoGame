// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRReplayBuffer.h"
#include "LRReplayTypes.h"
#include "Misc/AutomationTest.h"

namespace LRReplayBufferSpecPrivate
{
	/** Sampling period used by the tests, seconds (30 Hz). */
	constexpr float Period = 1.f / 30.f;

	/** Builds a traffic snapshot with the given id and road position. */
	FLRTrafficVehicleSnapshot MakeVehicle(int32 Id, double S, float D, float SpeedMs = 30.f, uint8 Lights = 0)
	{
		FLRTrafficVehicleSnapshot Vehicle;
		Vehicle.Id = Id;
		Vehicle.S = S;
		Vehicle.D = D;
		Vehicle.SpeedMs = SpeedMs;
		Vehicle.LightFlags = Lights;
		return Vehicle;
	}

	/** Builds a frame at Time with the player at X = Time * 1000 cm and yaw = Time * 10 degrees. */
	FLRReplayFrame MakeFrame(float Time)
	{
		FLRReplayFrame Frame;
		Frame.Time = Time;
		Frame.PlayerTransform = FTransform(FRotator(0.f, Time * 10.f, 0.f), FVector(Time * 1000.f, 0.f, 0.f));
		Frame.PlayerRoadCoord = FLRRoadCoord(Time * 10.0, 0.f);
		Frame.PlayerSpeedKmh = 100.f + Time;
		Frame.EngineRpm = 3000.f + Time * 100.f;
		Frame.SteeringInput = 0.f;
		return Frame;
	}
}

BEGIN_DEFINE_SPEC(FLRReplayBufferSpec, "LeftRight.Replay.Buffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRReplayBufferSpec)

void FLRReplayBufferSpec::Define()
{
	using namespace LRReplayBufferSpecPrivate;

	Describe("Settings", [this]()
	{
		It("defaults to 60 s at 30 Hz with 64 vehicles per frame", [this]()
		{
			const FLRReplaySettings Settings;
			TestEqual(TEXT("Capacity"), Settings.GetFrameCapacity(), 1800);
			TestEqual(TEXT("Max traffic"), Settings.MaxTrafficVehicles, 64);
			TestNearlyEqual(TEXT("Radius"), Settings.TrafficRadiusMeters, 250.f);
		});
	});

	Describe("WrapAround", [this]()
	{
		It("keeps the newest frames in chronological order after overwriting", [this]()
		{
			FLRReplayBuffer Buffer;
			Buffer.Configure(5, 4);
			for (int32 Index = 0; Index < 12; ++Index)
			{
				TestTrue(FString::Printf(TEXT("Add %d"), Index), Buffer.Add(MakeFrame(Index * Period)));
			}

			TestEqual(TEXT("Num is clamped to capacity"), Buffer.Num(), 5);
			for (int32 Index = 0; Index < Buffer.Num(); ++Index)
			{
				const float Expected = (7 + Index) * Period;
				TestNearlyEqual(FString::Printf(TEXT("Frame %d time"), Index), Buffer.GetFrame(Index).Time, Expected, 1.e-5f);
			}
			TestNearlyEqual(TEXT("Oldest"), Buffer.GetOldestTime(), 7 * Period, 1.e-5f);
			TestNearlyEqual(TEXT("Newest"), Buffer.GetNewestTime(), 11 * Period, 1.e-5f);
		});

		It("rejects frames whose time does not increase", [this]()
		{
			FLRReplayBuffer Buffer;
			Buffer.Configure(4, 0);
			TestTrue(TEXT("First"), Buffer.Add(MakeFrame(1.f)));
			TestFalse(TEXT("Same time"), Buffer.Add(MakeFrame(1.f)));
			TestFalse(TEXT("Older time"), Buffer.Add(MakeFrame(0.5f)));
			TestEqual(TEXT("Num"), Buffer.Num(), 1);
		});

		It("clears without losing capacity", [this]()
		{
			FLRReplayBuffer Buffer;
			Buffer.Configure(4, 2);
			Buffer.Add(MakeFrame(0.f));
			Buffer.Add(MakeFrame(Period));
			Buffer.Clear();
			TestTrue(TEXT("Empty"), Buffer.IsEmpty());
			TestEqual(TEXT("Capacity"), Buffer.GetCapacity(), 4);
			TestTrue(TEXT("Accepts frames again from time 0"), Buffer.Add(MakeFrame(0.f)));
		});
	});

	Describe("Duration", [this]()
	{
		It("is zero with fewer than two frames", [this]()
		{
			FLRReplayBuffer Buffer;
			Buffer.Configure(10, 0);
			TestNearlyEqual(TEXT("Empty"), Buffer.Duration(), 0.f);
			Buffer.Add(MakeFrame(3.f));
			TestNearlyEqual(TEXT("One frame"), Buffer.Duration(), 0.f);
		});

		It("covers the oldest to the newest frame and saturates at capacity", [this]()
		{
			FLRReplayBuffer Buffer;
			Buffer.Configure(31, 0);
			for (int32 Index = 0; Index < 31; ++Index)
			{
				Buffer.Add(MakeFrame(Index * Period));
			}
			TestNearlyEqual(TEXT("Full buffer covers one second"), Buffer.Duration(), 1.f, 1.e-4f);

			for (int32 Index = 31; Index < 100; ++Index)
			{
				Buffer.Add(MakeFrame(Index * Period));
			}
			TestNearlyEqual(TEXT("Still one second after wrapping"), Buffer.Duration(), 1.f, 1.e-4f);
		});
	});

	Describe("Interpolation", [this]()
	{
		It("returns midpoints between two frames", [this]()
		{
			FLRReplayBuffer Buffer;
			Buffer.Configure(8, 4);
			FLRReplayFrame A = MakeFrame(0.f);
			FLRReplayFrame B = MakeFrame(1.f);
			A.SteeringInput = -1.f;
			B.SteeringInput = 1.f;
			A.PlayerTransform.SetRotation(FRotator(0.f, 0.f, 0.f).Quaternion());
			B.PlayerTransform.SetRotation(FRotator(0.f, 90.f, 0.f).Quaternion());
			Buffer.Add(A);
			Buffer.Add(B);

			FLRReplayFrame Out;
			TestTrue(TEXT("EvaluateAt succeeds"), Buffer.EvaluateAt(0.5f, Out));
			TestNearlyEqual(TEXT("Time"), Out.Time, 0.5f, 1.e-5f);
			TestNearlyEqual(TEXT("Location X"), Out.PlayerTransform.GetLocation().X, 500.0, 1.e-3);
			TestNearlyEqual(TEXT("Yaw slerp"), static_cast<double>(Out.PlayerTransform.Rotator().Yaw), 45.0, 1.e-3);
			TestNearlyEqual(TEXT("Speed"), Out.PlayerSpeedKmh, 100.5f, 1.e-4f);
			TestNearlyEqual(TEXT("Rpm"), Out.EngineRpm, 3050.f, 1.e-2f);
			TestNearlyEqual(TEXT("Steering"), Out.SteeringInput, 0.f, 1.e-5f);
			TestNearlyEqual(TEXT("Road S"), Out.PlayerRoadCoord.S, 5.0, 1.e-4);
		});

		It("takes the shortest rotation path across the +-180 degree seam", [this]()
		{
			FLRReplayBuffer Buffer;
			Buffer.Configure(4, 0);
			FLRReplayFrame A = MakeFrame(0.f);
			FLRReplayFrame B = MakeFrame(1.f);
			A.PlayerTransform.SetRotation(FRotator(0.f, 170.f, 0.f).Quaternion());
			B.PlayerTransform.SetRotation(FRotator(0.f, -170.f, 0.f).Quaternion());
			Buffer.Add(A);
			Buffer.Add(B);

			FLRReplayFrame Out;
			Buffer.EvaluateAt(0.5f, Out);
			const double Yaw = FRotator::NormalizeAxis(Out.PlayerTransform.Rotator().Yaw);
			TestNearlyEqual(TEXT("Yaw is 180 (not 0)"), FMath::Abs(Yaw), 180.0, 1.e-2);
		});

		It("clamps outside the recorded range and hits frames exactly", [this]()
		{
			FLRReplayBuffer Buffer;
			Buffer.Configure(8, 0);
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Buffer.Add(MakeFrame(10.f + Index));
			}

			FLRReplayFrame Out;
			Buffer.EvaluateAt(-5.f, Out);
			TestNearlyEqual(TEXT("Before start -> oldest"), Out.Time, 10.f, 1.e-5f);
			Buffer.EvaluateAt(100.f, Out);
			TestNearlyEqual(TEXT("After end -> newest"), Out.Time, 13.f, 1.e-5f);
			TestNearlyEqual(TEXT("Newest location"), Out.PlayerTransform.GetLocation().X, 13000.0, 1.e-2);
			Buffer.EvaluateAt(2.f, Out);
			TestNearlyEqual(TEXT("Exact frame location"), Out.PlayerTransform.GetLocation().X, 12000.0, 1.e-2);
			Buffer.EvaluateAt(2.25f, Out);
			TestNearlyEqual(TEXT("Quarter location"), Out.PlayerTransform.GetLocation().X, 12250.0, 1.e-1);
		});

		It("fails on an empty buffer and copies a single frame", [this]()
		{
			FLRReplayBuffer Buffer;
			Buffer.Configure(4, 2);
			FLRReplayFrame Out;
			TestFalse(TEXT("Empty"), Buffer.EvaluateAt(0.f, Out));

			FLRReplayFrame Single = MakeFrame(2.f);
			Single.Traffic.Add(MakeVehicle(7, 30.0, 1.f));
			Buffer.Add(Single);
			TestTrue(TEXT("Single"), Buffer.EvaluateAt(1.f, Out));
			TestNearlyEqual(TEXT("Single time"), Out.Time, 2.f, 1.e-5f);
			TestEqual(TEXT("Single traffic"), Out.Traffic.Num(), 1);
		});
	});

	Describe("TrafficMatching", [this]()
	{
		It("interpolates vehicles by id regardless of their order", [this]()
		{
			FLRReplayBuffer Buffer;
			Buffer.Configure(4, 8);

			FLRReplayFrame A = MakeFrame(0.f);
			A.Traffic.Add(MakeVehicle(5, 100.0, 1.85f, 20.f));
			A.Traffic.Add(MakeVehicle(2, 50.0, -1.85f, 30.f));
			FLRReplayFrame B = MakeFrame(1.f);
			B.Traffic.Add(MakeVehicle(2, 80.0, 1.85f, 32.f, 1));
			B.Traffic.Add(MakeVehicle(5, 120.0, 1.85f, 20.f));
			Buffer.Add(A);
			Buffer.Add(B);

			TestEqual(TEXT("Stored sorted by id (first)"), Buffer.GetFrame(0).Traffic[0].Id, 2);
			TestEqual(TEXT("Stored sorted by id (second)"), Buffer.GetFrame(0).Traffic[1].Id, 5);

			FLRReplayFrame Out;
			Buffer.EvaluateAt(0.5f, Out);
			TestEqual(TEXT("Two vehicles"), Out.Traffic.Num(), 2);
			if (Out.Traffic.Num() == 2)
			{
				const FLRTrafficVehicleSnapshot& V2 = Out.Traffic[0];
				const FLRTrafficVehicleSnapshot& V5 = Out.Traffic[1];
				TestEqual(TEXT("Id 2 first"), V2.Id, 2);
				TestNearlyEqual(TEXT("Id 2 S"), V2.S, 65.0, 1.e-4);
				TestNearlyEqual(TEXT("Id 2 D"), V2.D, 0.f, 1.e-4f);
				TestNearlyEqual(TEXT("Id 2 speed"), V2.SpeedMs, 31.f, 1.e-4f);
				TestEqual(TEXT("Id 2 lights from nearest (B at 0.5)"), static_cast<int32>(V2.LightFlags), 1);
				TestEqual(TEXT("Id 5 second"), V5.Id, 5);
				TestNearlyEqual(TEXT("Id 5 S"), V5.S, 110.0, 1.e-4);
			}
		});

		It("keeps vehicles present in one frame only while that frame is nearest", [this]()
		{
			FLRReplayBuffer Buffer;
			Buffer.Configure(4, 8);

			FLRReplayFrame A = MakeFrame(0.f);
			A.Traffic.Add(MakeVehicle(1, 10.0, 0.f));
			A.Traffic.Add(MakeVehicle(3, 30.0, 0.f));
			FLRReplayFrame B = MakeFrame(1.f);
			B.Traffic.Add(MakeVehicle(1, 12.0, 0.f));
			B.Traffic.Add(MakeVehicle(4, 40.0, 0.f));
			Buffer.Add(A);
			Buffer.Add(B);

			FLRReplayFrame Out;
			Buffer.EvaluateAt(0.25f, Out);
			TestEqual(TEXT("Near A: ids 1 and 3"), Out.Traffic.Num(), 2);
			if (Out.Traffic.Num() == 2)
			{
				TestEqual(TEXT("Near A first id"), Out.Traffic[0].Id, 1);
				TestEqual(TEXT("Near A second id"), Out.Traffic[1].Id, 3);
			}

			Buffer.EvaluateAt(0.75f, Out);
			TestEqual(TEXT("Near B: ids 1 and 4"), Out.Traffic.Num(), 2);
			if (Out.Traffic.Num() == 2)
			{
				TestEqual(TEXT("Near B first id"), Out.Traffic[0].Id, 1);
				TestEqual(TEXT("Near B second id"), Out.Traffic[1].Id, 4);
				TestNearlyEqual(TEXT("Id 1 interpolated S"), Out.Traffic[0].S, 11.5, 1.e-4);
			}
		});

		It("drops traffic beyond the per-frame limit", [this]()
		{
			FLRReplayBuffer Buffer;
			Buffer.Configure(4, 3);
			FLRReplayFrame Frame = MakeFrame(0.f);
			for (int32 Id = 0; Id < 10; ++Id)
			{
				Frame.Traffic.Add(MakeVehicle(Id, Id * 10.0, 0.f));
			}
			Buffer.Add(Frame);
			TestEqual(TEXT("Clamped to 3"), Buffer.GetFrame(0).Traffic.Num(), 3);
		});
	});

	Describe("Memory", [this]()
	{
		It("does not grow after warm-up", [this]()
		{
			constexpr int32 MaxTraffic = 64;
			FLRReplayBuffer Buffer;
			Buffer.Configure(60, MaxTraffic);

			FLRReplayFrame Frame;
			Frame.Traffic.Reserve(MaxTraffic * 2);
			auto FillFrame = [&Frame](int32 Step, int32 NumVehicles)
			{
				Frame.Time = Step * Period;
				Frame.PlayerTransform.SetLocation(FVector(Step * 100.0, 0.0, 0.0));
				Frame.Traffic.Reset();
				for (int32 Index = 0; Index < NumVehicles; ++Index)
				{
					Frame.Traffic.Add(MakeVehicle(NumVehicles - Index + Step, Index * 5.0, 0.f));
				}
			};

			const SIZE_T AllocatedAfterConfigure = Buffer.GetAllocatedSize();

			// Warm-up: fill the ring once, with more vehicles than the per-frame limit.
			int32 Step = 0;
			for (; Step < 60; ++Step)
			{
				FillFrame(Step, MaxTraffic + 20);
				Buffer.Add(Frame);
			}
			const SIZE_T AllocatedAfterWarmUp = Buffer.GetAllocatedSize();
			TestEqual(TEXT("Preallocation already covered the warm-up"), AllocatedAfterWarmUp, AllocatedAfterConfigure);

			const FLRTrafficVehicleSnapshot* FirstTrafficData = Buffer.GetFrame(0).Traffic.GetData();

			// Several more laps with varying traffic counts.
			for (; Step < 400; ++Step)
			{
				FillFrame(Step, Step % (MaxTraffic + 30));
				Buffer.Add(Frame);
			}
			TestEqual(TEXT("No growth after many wraps"), Buffer.GetAllocatedSize(), AllocatedAfterWarmUp);
			TestEqual(TEXT("Capacity unchanged"), Buffer.GetCapacity(), 60);

			bool bFoundSameStorage = false;
			for (int32 Index = 0; Index < Buffer.Num(); ++Index)
			{
				bFoundSameStorage |= Buffer.GetFrame(Index).Traffic.GetData() == FirstTrafficData;
			}
			TestTrue(TEXT("Traffic arrays are reused in place"), bFoundSameStorage);
		});
	});

	Describe("SampleClock", [this]()
	{
		It("samples immediately, then at the fixed rate, without bursts after a hitch", [this]()
		{
			FLRReplaySampleClock Clock;
			Clock.Configure(30.f);
			TestTrue(TEXT("First advance samples"), Clock.Advance(0.016f));

			int32 Samples = 0;
			for (int32 Index = 0; Index < 600; ++Index)
			{
				Samples += Clock.Advance(1.f / 120.f) ? 1 : 0;
			}
			TestTrue(TEXT("About 150 samples in 5 s at 30 Hz"), FMath::Abs(Samples - 150) <= 2);

			TestTrue(TEXT("Hitch produces one sample"), Clock.Advance(0.5f));
			TestFalse(TEXT("No burst right after the hitch"), Clock.Advance(0.001f));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
