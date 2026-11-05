// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Sim/LRMobil.h"

namespace LRMobilSpecPrivate
{
	FLRMobilParams MakeParams(float Politeness = 0.3f)
	{
		FLRMobilParams Params;
		Params.Politeness = Politeness;
		Params.ThresholdMs2 = 0.2f;
		Params.KeepRightBiasMs2 = 0.15f;
		Params.SafeDecelMs2 = 4.f;
		Params.MinGapM = 2.f;
		return Params;
	}

	/** A slow leader in the current lane and a free target lane: strong selfish incentive, no followers. */
	FLRMobilInput MakeAttractiveInput()
	{
		FLRMobilInput Input;
		Input.AccelSelfOld = -1.5f;
		Input.AccelSelfNew = 0.8f;
		Input.bHasNewLeader = true;
		Input.GapToNewLeaderM = 80.f;
		Input.bTowardRight = false;
		return Input;
	}
}

BEGIN_DEFINE_SPEC(FLRMobilSpec, "LeftRight.Traffic.Mobil",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRMobilSpec)

void FLRMobilSpec::Define()
{
	using namespace LRMobilSpecPrivate;

	Describe("EvaluateMobil", [this]()
	{
		It("changes lane when the incentive exceeds the threshold", [this]()
		{
			const FLRMobilResult Result = EvaluateMobil(MakeParams(), MakeAttractiveInput());
			TestTrue(TEXT("Safe"), Result.bSafe);
			TestTrue(TEXT("Change"), Result.bChange);
			// Incentive 2.3 minus (threshold 0.2 + bias 0.15 against moving left).
			TestNearlyEqual(TEXT("Utility"), Result.Utility, 2.3f - 0.35f, 1.0e-4f);
		});

		It("does not change lane without incentive", [this]()
		{
			FLRMobilInput Input = MakeAttractiveInput();
			Input.AccelSelfNew = Input.AccelSelfOld + 0.1f;
			const FLRMobilResult Result = EvaluateMobil(MakeParams(), Input);
			TestFalse(TEXT("Change"), Result.bChange);
			TestTrue(TEXT("Negative utility"), Result.Utility < 0.f);
		});

		It("rejects an unsafe change (new follower would brake too hard)", [this]()
		{
			FLRMobilInput Input = MakeAttractiveInput();
			Input.bHasNewFollower = true;
			Input.GapToNewFollowerM = 15.f;
			Input.AccelNewFollowerOld = 0.f;
			Input.AccelNewFollowerNew = -6.f;
			const FLRMobilResult Result = EvaluateMobil(MakeParams(0.f), Input);
			TestFalse(TEXT("Safe"), Result.bSafe);
			TestFalse(TEXT("Change"), Result.bChange);
		});

		It("rejects a change into a gap shorter than the minimum gap", [this]()
		{
			FLRMobilInput Front = MakeAttractiveInput();
			Front.GapToNewLeaderM = 1.f;
			TestFalse(TEXT("Front gap too short"), EvaluateMobil(MakeParams(), Front).bChange);

			FLRMobilInput Rear = MakeAttractiveInput();
			Rear.bHasNewFollower = true;
			Rear.GapToNewFollowerM = 1.f;
			Rear.AccelNewFollowerNew = 0.f;
			TestFalse(TEXT("Rear gap too short"), EvaluateMobil(MakeParams(), Rear).bChange);
		});

		It("politeness reduces lane changes that hurt the followers", [this]()
		{
			FLRMobilInput Input = MakeAttractiveInput();
			Input.AccelSelfNew = Input.AccelSelfOld + 1.0f;
			Input.bHasNewFollower = true;
			Input.GapToNewFollowerM = 30.f;
			Input.AccelNewFollowerOld = 0.5f;
			Input.AccelNewFollowerNew = -2.f;

			const FLRMobilResult Selfish = EvaluateMobil(MakeParams(0.f), Input);
			const FLRMobilResult Polite = EvaluateMobil(MakeParams(0.5f), Input);
			TestTrue(TEXT("Selfish driver changes"), Selfish.bChange);
			TestFalse(TEXT("Polite driver stays"), Polite.bChange);
			TestTrue(TEXT("Politeness lowers the utility"), Polite.Utility < Selfish.Utility);
		});

		It("counts the gain of the old follower", [this]()
		{
			FLRMobilInput Input = MakeAttractiveInput();
			Input.AccelSelfNew = Input.AccelSelfOld + 0.3f;
			Input.bHasOldFollower = true;
			Input.AccelOldFollowerOld = -1.f;
			Input.AccelOldFollowerNew = 1.f;
			TestFalse(TEXT("Selfish: not enough incentive"), EvaluateMobil(MakeParams(0.f), Input).bChange);
			TestTrue(TEXT("Polite: freeing the follower is worth it"), EvaluateMobil(MakeParams(0.5f), Input).bChange);
		});

		It("keep-right bias favours moving right", [this]()
		{
			FLRMobilInput Input = MakeAttractiveInput();
			Input.AccelSelfNew = Input.AccelSelfOld + 0.25f;

			Input.bTowardRight = false;
			const FLRMobilResult Left = EvaluateMobil(MakeParams(), Input);
			Input.bTowardRight = true;
			const FLRMobilResult Right = EvaluateMobil(MakeParams(), Input);

			TestFalse(TEXT("Same gain toward the left is not enough"), Left.bChange);
			TestTrue(TEXT("Same gain toward the right is enough"), Right.bChange);
			TestNearlyEqual(TEXT("Bias applied twice between both sides"), Right.Utility - Left.Utility, 0.3f, 1.0e-4f);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
