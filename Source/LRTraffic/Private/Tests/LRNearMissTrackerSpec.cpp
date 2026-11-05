// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Sim/LRNearMissTracker.h"

namespace LRNearMissTrackerSpecPrivate
{
	constexpr double StepSeconds = 1.0 / 60.0;
	constexpr float PlayerWidthM = 1.9f;
	constexpr float VehicleWidthM = 1.85f;
	constexpr float HalfWidths = 0.5f * (PlayerWidthM + VehicleWidthM);

	/** One traffic vehicle on a straight scripted path: S(t) = StartS + SpeedMs * t at a fixed D. */
	struct FScriptedVehicle
	{
		int32 Id = 1;
		double StartS = 30.0;
		float D = 3.f;
		float SpeedMs = 20.f;
		bool bOncoming = false;
		/** Optional lateral bump: D changes to ContactD between these times (contact tests). */
		double ContactFrom = -1.0;
		double ContactTo = -1.0;
		float ContactD = 0.f;
	};

	/**
	 * Runs the tracker for Seconds with a player at D = 0 driving at PlayerSpeedMs from S = 0,
	 * returns every event produced. Optionally reports a contact with ContactNotifyId at ContactNotifyTime.
	 */
	TArray<FLRNearMissEvent> RunScenario(FLRNearMissTracker& Tracker, float PlayerSpeedMs, TConstArrayView<FScriptedVehicle> Script, double Seconds,
		int32 ContactNotifyId = INDEX_NONE, double ContactNotifyTime = 0.0)
	{
		TArray<FLRNearMissEvent> Events;
		TArray<FLRNearMissVehicleState> States;
		const int32 NumSteps = FMath::CeilToInt(Seconds / StepSeconds);
		const int32 ContactStep = FMath::RoundToInt(ContactNotifyTime / StepSeconds);
		for (int32 Step = 0; Step <= NumSteps; ++Step)
		{
			const double Time = Step * StepSeconds;

			FLRNearMissPlayerState Player;
			Player.S = PlayerSpeedMs * Time;
			Player.D = 0.f;
			Player.LengthM = 4.5f;
			Player.WidthM = PlayerWidthM;
			Player.SpeedMs = PlayerSpeedMs;

			States.Reset();
			for (const FScriptedVehicle& Vehicle : Script)
			{
				FLRNearMissVehicleState& State = States.AddDefaulted_GetRef();
				State.Id = Vehicle.Id;
				State.S = Vehicle.StartS + Vehicle.SpeedMs * Time;
				State.D = (Time >= Vehicle.ContactFrom && Time <= Vehicle.ContactTo) ? Vehicle.ContactD : Vehicle.D;
				State.LengthM = 4.6f;
				State.WidthM = VehicleWidthM;
				State.SpeedMs = Vehicle.SpeedMs;
				State.Archetype = TEXT("sedan");
				State.bOncoming = Vehicle.bOncoming;
			}

			if (ContactNotifyId != INDEX_NONE && Step == ContactStep)
			{
				Tracker.NotifyContact(ContactNotifyId);
			}
			Tracker.Update(Time, Player, States, Events);
		}
		return Events;
	}

	FLRNearMissTracker MakeTracker()
	{
		FLRNearMissTracker Tracker;
		Tracker.Configure(FLRNearMissParams());
		return Tracker;
	}

	/** Lateral offset of a vehicle center giving the requested clearance on the right of the player. */
	float OffsetForClearance(float ClearanceM)
	{
		return HalfWidths + ClearanceM;
	}
}

BEGIN_DEFINE_SPEC(FLRNearMissTrackerSpec, "LeftRight.Traffic.NearMiss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRNearMissTrackerSpec)

void FLRNearMissTrackerSpec::Define()
{
	using namespace LRNearMissTrackerSpecPrivate;

	Describe("ClassifyClearance", [this]()
	{
		It("maps clearances to the default tiers", [this]()
		{
			const FLRNearMissTracker Tracker = MakeTracker();
			TestEqual(TEXT("1.2 m"), Tracker.ClassifyClearance(1.2f), ELRNearMissTier::None);
			TestEqual(TEXT("0.8 m"), Tracker.ClassifyClearance(0.8f), ELRNearMissTier::Close);
			TestEqual(TEXT("0.4 m"), Tracker.ClassifyClearance(0.4f), ELRNearMissTier::VeryClose);
			TestEqual(TEXT("0.1 m"), Tracker.ClassifyClearance(0.1f), ELRNearMissTier::Razor);
		});
	});

	Describe("Update", [this]()
	{
		It("emits one event with the right tier when an overtake completes", [this]()
		{
			struct FCase
			{
				float Clearance;
				ELRNearMissTier Tier;
			};
			const FCase Cases[] = { { 0.8f, ELRNearMissTier::Close }, { 0.4f, ELRNearMissTier::VeryClose }, { 0.15f, ELRNearMissTier::Razor } };
			for (const FCase& Case : Cases)
			{
				FLRNearMissTracker Tracker = MakeTracker();
				FScriptedVehicle Vehicle;
				Vehicle.D = OffsetForClearance(Case.Clearance);
				const TArray<FLRNearMissEvent> Events = RunScenario(Tracker, 40.f, MakeArrayView(&Vehicle, 1), 5.0);
				if (!TestEqual(FString::Printf(TEXT("Events for %.2f m"), Case.Clearance), Events.Num(), 1))
				{
					continue;
				}
				TestEqual(TEXT("Tier"), Events[0].Tier, Case.Tier);
				TestNearlyEqual(TEXT("Clearance"), Events[0].ClearanceMeters, Case.Clearance, 1.0e-3f);
				TestNearlyEqual(TEXT("Relative speed"), Events[0].RelativeSpeedKmh, 72.f, 0.1f);
				TestFalse(TEXT("Not oncoming"), Events[0].bOncoming);
				TestFalse(TEXT("No thread the needle"), Events[0].bThreadTheNeedle);
				TestEqual(TEXT("Vehicle id"), Events[0].TrafficVehicleId, Vehicle.Id);
				TestEqual(TEXT("Archetype"), Events[0].Archetype, FName(TEXT("sedan")));
			}
		});

		It("ignores wide passes", [this]()
		{
			FLRNearMissTracker Tracker = MakeTracker();
			FScriptedVehicle Vehicle;
			Vehicle.D = OffsetForClearance(1.3f);
			TestEqual(TEXT("Events"), RunScenario(Tracker, 40.f, MakeArrayView(&Vehicle, 1), 5.0).Num(), 0);
		});

		It("ignores passes below the minimum relative speed", [this]()
		{
			FLRNearMissTracker Tracker = MakeTracker();
			FScriptedVehicle Vehicle;
			Vehicle.D = OffsetForClearance(0.3f);
			Vehicle.StartS = 8.0;
			Vehicle.SpeedMs = 36.f; // 4 m/s = 14.4 km/h slower than the player
			TestEqual(TEXT("Events"), RunScenario(Tracker, 40.f, MakeArrayView(&Vehicle, 1), 8.0).Num(), 0);
		});

		It("flags oncoming vehicles", [this]()
		{
			FLRNearMissTracker Tracker = MakeTracker();
			FScriptedVehicle Vehicle;
			Vehicle.StartS = 120.0;
			Vehicle.SpeedMs = -30.f;
			Vehicle.D = -OffsetForClearance(0.6f);
			Vehicle.bOncoming = true;
			const TArray<FLRNearMissEvent> Events = RunScenario(Tracker, 40.f, MakeArrayView(&Vehicle, 1), 4.0);
			if (TestEqual(TEXT("Events"), Events.Num(), 1))
			{
				TestTrue(TEXT("Oncoming"), Events[0].bOncoming);
				TestEqual(TEXT("Tier"), Events[0].Tier, ELRNearMissTier::Close);
				TestNearlyEqual(TEXT("Relative speed"), Events[0].RelativeSpeedKmh, 252.f, 0.1f);
			}
		});

		It("detects thread the needle when both sides are passed together", [this]()
		{
			FLRNearMissTracker Tracker = MakeTracker();
			FScriptedVehicle Script[2];
			Script[0].Id = 1;
			Script[0].D = -OffsetForClearance(0.5f);
			Script[0].StartS = 30.0;
			Script[1].Id = 2;
			Script[1].D = OffsetForClearance(0.7f);
			Script[1].StartS = 31.0;
			const TArray<FLRNearMissEvent> Events = RunScenario(Tracker, 40.f, Script, 5.0);
			if (TestEqual(TEXT("Events"), Events.Num(), 2))
			{
				TestFalse(TEXT("First pass alone"), Events[0].bThreadTheNeedle);
				TestTrue(TEXT("Second pass threads the needle"), Events[1].bThreadTheNeedle);
			}
		});

		It("does not thread the needle with two passes on the same side", [this]()
		{
			FLRNearMissTracker Tracker = MakeTracker();
			FScriptedVehicle Script[2];
			Script[0].Id = 1;
			Script[0].D = OffsetForClearance(0.5f);
			Script[0].StartS = 30.0;
			Script[1].Id = 2;
			Script[1].D = OffsetForClearance(0.6f);
			Script[1].StartS = 37.0;
			const TArray<FLRNearMissEvent> Events = RunScenario(Tracker, 40.f, Script, 5.0);
			if (TestEqual(TEXT("Events"), Events.Num(), 2))
			{
				TestFalse(TEXT("Same side"), Events[1].bThreadTheNeedle);
			}
		});

		It("does not thread the needle when the passes are far apart in time", [this]()
		{
			FLRNearMissTracker Tracker = MakeTracker();
			FScriptedVehicle Script[2];
			Script[0].Id = 1;
			Script[0].D = -OffsetForClearance(0.5f);
			Script[0].StartS = 30.0;
			Script[1].Id = 2;
			Script[1].D = OffsetForClearance(0.5f);
			Script[1].StartS = 60.0; // 1.5 s later at 20 m/s closing speed
			const TArray<FLRNearMissEvent> Events = RunScenario(Tracker, 40.f, Script, 8.0);
			if (TestEqual(TEXT("Events"), Events.Num(), 2))
			{
				TestFalse(TEXT("Outside the window"), Events[1].bThreadTheNeedle);
			}
		});

		It("produces no event after a geometric contact", [this]()
		{
			FLRNearMissTracker Tracker = MakeTracker();
			FScriptedVehicle Vehicle;
			Vehicle.D = OffsetForClearance(0.3f);
			// While overlapping (t around 1.5 s) the vehicle touches the player for a moment.
			Vehicle.ContactFrom = 1.45;
			Vehicle.ContactTo = 1.5;
			Vehicle.ContactD = OffsetForClearance(-0.05f);
			TestEqual(TEXT("Events"), RunScenario(Tracker, 40.f, MakeArrayView(&Vehicle, 1), 5.0).Num(), 0);
		});

		It("produces no event after NotifyContact during the overlap", [this]()
		{
			FLRNearMissTracker Tracker = MakeTracker();
			FScriptedVehicle Vehicle;
			Vehicle.D = OffsetForClearance(0.3f);
			// The overlap lasts from about 1.27 s to 1.73 s.
			TestEqual(TEXT("Events"), RunScenario(Tracker, 40.f, MakeArrayView(&Vehicle, 1), 5.0, Vehicle.Id, 1.5).Num(), 0);
		});

		It("produces no event after NotifyContact right before the overlap", [this]()
		{
			FLRNearMissTracker Tracker = MakeTracker();
			FScriptedVehicle Vehicle;
			Vehicle.D = OffsetForClearance(0.3f);
			TestEqual(TEXT("Events"), RunScenario(Tracker, 40.f, MakeArrayView(&Vehicle, 1), 5.0, Vehicle.Id, 1.2).Num(), 0);
		});

		It("drops passes of vehicles that disappear while overlapping", [this]()
		{
			FLRNearMissTracker Tracker = MakeTracker();
			TArray<FLRNearMissEvent> Events;
			FLRNearMissPlayerState Player;
			Player.SpeedMs = 40.f;
			FLRNearMissVehicleState Vehicle;
			Vehicle.Id = 7;
			Vehicle.S = 1.0;
			Vehicle.D = OffsetForClearance(0.3f);
			Vehicle.WidthM = VehicleWidthM;
			Player.WidthM = PlayerWidthM;
			Tracker.Update(0.0, Player, MakeArrayView(&Vehicle, 1), Events);
			TestEqual(TEXT("Tracked"), Tracker.GetNumActivePasses(), 1);
			Tracker.Update(StepSeconds, Player, TConstArrayView<FLRNearMissVehicleState>(), Events);
			TestEqual(TEXT("Dropped"), Tracker.GetNumActivePasses(), 0);
			TestEqual(TEXT("No event"), Events.Num(), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
