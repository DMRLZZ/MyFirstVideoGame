// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRCockpitHeadModel.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(FLRCockpitHeadModelSpec, "LeftRight.Camera.HeadModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRCockpitHeadModelSpec)

void FLRCockpitHeadModelSpec::Define()
{
	static constexpr float G = FLRCockpitHeadModel::GravityMs2;

	Describe("Step input", [this]()
	{
		It("converges to the equilibrium pose", [this]()
		{
			FLRHeadModelParams Params;
			FLRCockpitHeadModel Model;
			Model.Configure(Params);

			const FVector Acceleration(-0.5 * G, 0.5 * G, 0.0);
			for (int32 Step = 0; Step < 600; ++Step)
			{
				Model.Update(Acceleration, 1.f / 60.f);
			}

			const FLRCockpitHeadModel::FTargets Targets = FLRCockpitHeadModel::ComputeTargets(Params, Acceleration);
			TestTrue(TEXT("Offset"), Model.GetOffsetCm().Equals(Targets.OffsetCm, 1.e-3));
			TestEqual(TEXT("Roll"), Model.GetRollDeg(), Targets.RollDeg, 1.e-3f);
			TestEqual(TEXT("Pitch"), Model.GetPitchDeg(), Targets.PitchDeg, 1.e-3f);
		});

		It("never grows an oscillation, even with the lowest damping", [this]()
		{
			FLRHeadModelParams Params;
			Params.DampingRatio = 0.3f;
			Params.NaturalFrequencyHz = 3.f;

			FLRCockpitHeadModel Model;
			Model.Configure(Params);

			const FVector Acceleration(0.0, 0.5 * G, 0.0);
			const double Target = FLRCockpitHeadModel::ComputeTargets(Params, Acceleration).OffsetCm.Y;

			double LastPeak = TNumericLimits<double>::Max();
			double Error2 = FMath::Abs(Target);
			double Error1 = Error2;
			int32 NumPeaks = 0;
			bool bGrew = false;
			for (int32 Step = 0; Step < 600; ++Step)
			{
				Model.Update(Acceleration, 1.f / 60.f);
				const double Error0 = FMath::Abs(Model.GetOffsetCm().Y - Target);
				// The threshold ignores the rounding noise left once the head has settled.
				if (Error1 > Error2 && Error1 >= Error0 && Error1 > 1.e-6)
				{
					bGrew |= Error1 > LastPeak;
					LastPeak = Error1;
					++NumPeaks;
				}
				Error2 = Error1;
				Error1 = Error0;
			}

			TestTrue(TEXT("It does overshoot with damping 0.3 (the test sees peaks)"), NumPeaks >= 2);
			TestFalse(TEXT("Peaks never grow"), bGrew);
			TestTrue(TEXT("Settled"), Error1 < 1.e-3);
		});

		It("barely depends on the frame rate", [this]()
		{
			const auto Simulate = [](float DeltaSeconds)
			{
				FLRCockpitHeadModel Model;
				Model.Configure(FLRHeadModelParams());
				const FVector Acceleration(0.0, G, 0.0);
				const int32 Steps = FMath::RoundToInt(0.3f / DeltaSeconds);
				for (int32 Step = 0; Step < Steps; ++Step)
				{
					Model.Update(Acceleration, DeltaSeconds);
				}
				return Model.GetOffsetCm().Y;
			};

			const double At30 = Simulate(1.f / 30.f);
			const double At60 = Simulate(1.f / 60.f);
			const double At144 = Simulate(1.f / 144.f);
			TestEqual(TEXT("30 vs 60 fps"), At30, At60, 0.05);
			TestEqual(TEXT("144 vs 60 fps"), At144, At60, 0.05);
		});
	});

	Describe("Limits", [this]()
	{
		It("clamps the pose under absurd accelerations", [this]()
		{
			FLRHeadModelParams Params;
			FLRCockpitHeadModel Model;
			Model.Configure(Params);

			bool bInside = true;
			for (int32 Step = 0; Step < 300; ++Step)
			{
				// 50 G crash pulses in alternating directions.
				const double Sign = (Step / 10) % 2 == 0 ? 1.0 : -1.0;
				Model.Update(FVector(Sign * 50.0 * G, -Sign * 50.0 * G, Sign * 50.0 * G), 1.f / 60.f);

				const FVector& Offset = Model.GetOffsetCm();
				bInside &= Offset.GetAbsMax() <= Params.MaxOffsetCm + UE_KINDA_SMALL_NUMBER;
				bInside &= FMath::Abs(Model.GetRollDeg()) <= Params.MaxRollDeg + UE_KINDA_SMALL_NUMBER;
				bInside &= FMath::Abs(Model.GetPitchDeg()) <= Params.MaxPitchDeg + UE_KINDA_SMALL_NUMBER;
			}
			TestTrue(TEXT("Inside the limits"), bInside);
		});

		It("stays stable after a hitch", [this]()
		{
			FLRHeadModelParams Params;
			FLRCockpitHeadModel Model;
			Model.Configure(Params);
			Model.Update(FVector(G, G, 0.0), 30.f);
			TestFalse(TEXT("Finite"), Model.GetOffsetCm().ContainsNaN());
			TestTrue(TEXT("Bounded"), Model.GetOffsetCm().GetAbsMax() <= Params.MaxOffsetCm + UE_KINDA_SMALL_NUMBER);
		});

		It("is rigid with zero intensity", [this]()
		{
			FLRHeadModelParams Params;
			Params.Intensity = 0.f;
			FLRCockpitHeadModel Model;
			Model.Configure(Params);
			for (int32 Step = 0; Step < 120; ++Step)
			{
				Model.Update(FVector(G, G, G), 1.f / 60.f);
			}
			TestTrue(TEXT("No offset"), Model.GetOffsetCm().IsNearlyZero());
			TestEqual(TEXT("No roll"), Model.GetRollDeg(), 0.f);
			TestEqual(TEXT("No pitch"), Model.GetPitchDeg(), 0.f);
		});

		It("scales with the intensity", [this]()
		{
			FLRHeadModelParams Full;
			FLRHeadModelParams Half;
			Half.Intensity = 0.5f;
			const FVector Acceleration(0.0, 0.5 * G, 0.0);
			const FLRCockpitHeadModel::FTargets FullTargets = FLRCockpitHeadModel::ComputeTargets(Full, Acceleration);
			const FLRCockpitHeadModel::FTargets HalfTargets = FLRCockpitHeadModel::ComputeTargets(Half, Acceleration);
			TestEqual(TEXT("Offset"), HalfTargets.OffsetCm.Y, FullTargets.OffsetCm.Y * 0.5, 1.e-3);
			TestEqual(TEXT("Roll"), HalfTargets.RollDeg, FullTargets.RollDeg * 0.5f, 1.e-3f);
		});
	});

	Describe("Direction", [this]()
	{
		It("throws the head opposite to the acceleration", [this]()
		{
			FLRHeadModelParams Params;

			const FLRCockpitHeadModel::FTargets Braking = FLRCockpitHeadModel::ComputeTargets(Params, FVector(-G, 0.0, 0.0));
			TestTrue(TEXT("Braking moves the head forward"), Braking.OffsetCm.X > 0.0);
			TestTrue(TEXT("Braking nods the head down"), Braking.PitchDeg < 0.f);

			const FLRCockpitHeadModel::FTargets RightTurn = FLRCockpitHeadModel::ComputeTargets(Params, FVector(0.0, G, 0.0));
			TestTrue(TEXT("A right turn moves the head left"), RightTurn.OffsetCm.Y < 0.0);
			TestTrue(TEXT("A right turn rolls the head left"), RightTurn.RollDeg < 0.f);
		});
	});

	Describe("Reset", [this]()
	{
		It("returns to rest", [this]()
		{
			FLRCockpitHeadModel Model;
			Model.Configure(FLRHeadModelParams());
			Model.Update(FVector(G, G, G), 0.2f);
			TestFalse(TEXT("Moved"), Model.GetOffsetCm().IsNearlyZero());

			Model.Reset();
			TestTrue(TEXT("Offset at rest"), Model.GetOffsetCm().IsNearlyZero());
			TestEqual(TEXT("Roll at rest"), Model.GetRollDeg(), 0.f);

			// No stored velocity: without acceleration it stays at rest.
			Model.Update(FVector::ZeroVector, 0.1f);
			TestTrue(TEXT("Still at rest"), Model.GetOffsetCm().IsNearlyZero());
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
