// Copyright LEFTRIGHT. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "LRDataLoader.h"
#include "LRRoadPath.h"
#include "LRUnits.h"
#include "Misc/AutomationTest.h"
#include "Sim/LRTrafficSim.h"

namespace LRTrafficSimSpecPrivate
{
	constexpr float StepSeconds = 1.f / 60.f;
	constexpr int32 StepsPerMinute = 60 * 60;
	const float SpeedLimitMs = static_cast<float>(LRUnits::KmhToMs(130.0));

	/** Traffic profile close to Data/Traffic/TrafficProfile_Default.json, built in code so the tests need no file. */
	FLRTrafficProfile MakeProfile()
	{
		FLRTrafficProfile Profile;

		FLRTrafficArchetype& Sedan = Profile.Archetypes.AddDefaulted_GetRef();
		Sedan.Name = TEXT("sedan");
		Sedan.LengthMeters = 4.8f;
		Sedan.WidthMeters = 1.85f;
		Sedan.Weight = 5.f;
		Sedan.SpeedFactorMin = 0.9f;
		Sedan.SpeedFactorMax = 1.05f;

		FLRTrafficArchetype& Hatch = Profile.Archetypes.AddDefaulted_GetRef();
		Hatch.Name = TEXT("hatchback");
		Hatch.LengthMeters = 4.1f;
		Hatch.WidthMeters = 1.75f;
		Hatch.Weight = 3.f;

		FLRTrafficArchetype& Truck = Profile.Archetypes.AddDefaulted_GetRef();
		Truck.Name = TEXT("truck");
		Truck.LengthMeters = 16.5f;
		Truck.WidthMeters = 2.55f;
		Truck.HeightMeters = 4.f;
		Truck.Weight = 1.5f;
		Truck.SpeedFactorMin = 0.68f;
		Truck.SpeedFactorMax = 0.78f;
		Truck.bPreferRightLanes = true;
		Truck.DriverProfile = TEXT("truck");

		FLRTrafficDriverProfile& Normal = Profile.DriverProfiles.AddDefaulted_GetRef();
		Normal.Name = TEXT("normal");
		Normal.Weight = 6.f;

		FLRTrafficDriverProfile& Hurry = Profile.DriverProfiles.AddDefaulted_GetRef();
		Hurry.Name = TEXT("hurry");
		Hurry.Weight = 2.f;
		Hurry.DesiredSpeedFactorMin = 1.1f;
		Hurry.DesiredSpeedFactorMax = 1.25f;
		Hurry.TimeHeadwayS = 0.9f;
		Hurry.Politeness = 0.1f;
		Hurry.LaneChangeThresholdMs2 = 0.1f;
		Hurry.AggressiveEventChance = 0.1f;

		FLRTrafficDriverProfile& Sunday = Profile.DriverProfiles.AddDefaulted_GetRef();
		Sunday.Name = TEXT("sunday");
		Sunday.Weight = 1.f;
		Sunday.DesiredSpeedFactorMin = 0.75f;
		Sunday.DesiredSpeedFactorMax = 0.85f;
		Sunday.KeepRightBiasMs2 = -0.2f;
		Sunday.bSpawnInLeftLanes = true;

		FLRTrafficDriverProfile& TruckDriver = Profile.DriverProfiles.AddDefaulted_GetRef();
		TruckDriver.Name = TEXT("truck");
		TruckDriver.Weight = 0.f;
		TruckDriver.DesiredSpeedFactorMin = 0.97f;
		TruckDriver.DesiredSpeedFactorMax = 1.f;
		TruckDriver.TimeHeadwayS = 1.8f;
		TruckDriver.MaxAccelMs2 = 0.8f;

		Profile.PlayerDriverProfile = TEXT("normal");
		Profile.DensityCurve = { FVector2D(0.0, 12.0), FVector2D(300.0, 22.0) };
		Profile.MaxVehicles = 200;
		return Profile;
	}

	/** Lanes of a road built by FLRRoadPath (same convention as the game). */
	TArray<FLRLaneDesc> MakeLanes(int32 LanesPerDirection, bool bTwoWay)
	{
		FLRRoadProfile RoadProfile;
		RoadProfile.LanesPerDirection = LanesPerDirection;
		RoadProfile.bTwoWay = bTwoWay;
		FLRRoadPath Path;
		Path.Reset(RoadProfile, FVector::ZeroVector, 0.f);
		return Path.GetLanes();
	}

	/** Index of the first lane of the player's direction. */
	int32 FirstPlayerLane(const TArray<FLRLaneDesc>& Lanes)
	{
		for (int32 Index = 0; Index < Lanes.Num(); ++Index)
		{
			if (Lanes[Index].Direction > 0)
			{
				return Index;
			}
		}
		return 0;
	}

	/**
	 * Scripted player: cruises in the player lanes, switches lane every 20 s and slows down to 15 m/s for 20 s
	 * every minute (exercises both spawn rules).
	 */
	struct FScriptedPlayer
	{
		FLRTrafficPlayerState State;
		TArray<int32> PlayerLanes;

		void Init(const TArray<FLRLaneDesc>& Lanes, double StartS)
		{
			for (int32 Index = 0; Index < Lanes.Num(); ++Index)
			{
				if (Lanes[Index].Direction > 0)
				{
					PlayerLanes.Add(Index);
				}
			}
			State.bValid = true;
			State.S = StartS;
			State.D = Lanes[PlayerLanes[0]].CenterD;
			State.SpeedMs = 45.f;
			State.LengthM = 4.5f;
			State.WidthM = 1.9f;
		}

		void Advance(const TArray<FLRLaneDesc>& Lanes, int32 Step)
		{
			const int32 Second = Step / 60;
			State.SpeedMs = (Second % 60) >= 40 ? 15.f : 45.f;
			State.D = Lanes[PlayerLanes[(Second / 20) % PlayerLanes.Num()]].CenterD;
			State.S += static_cast<double>(State.SpeedMs) * StepSeconds;
		}
	};
}

BEGIN_DEFINE_SPEC(FLRTrafficSimSpec, "LeftRight.Traffic.Sim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

	/**
	 * Runs NumSteps fixed steps with the scripted player, checking the invariants every step.
	 * Returns false (with errors logged) at the first violation.
	 */
	bool RunChecked(FLRTrafficSim& Sim, LRTrafficSimSpecPrivate::FScriptedPlayer& Player, const TArray<FLRLaneDesc>& Lanes, int32 NumSteps)
	{
		using namespace LRTrafficSimSpecPrivate;

		FString Error;
		for (int32 Step = 0; Step < NumSteps; ++Step)
		{
			Sim.SetPlayer(Player.State);
			Sim.Step(StepSeconds);
			Player.Advance(Lanes, Step);

			if (!Sim.ValidateNoLaneOverlap(&Error))
			{
				AddError(FString::Printf(TEXT("Step %d: %s"), Step, *Error));
				return false;
			}
			if (!Sim.ValidateFiniteState(&Error))
			{
				AddError(FString::Printf(TEXT("Step %d: %s"), Step, *Error));
				return false;
			}
			const double PlayerS = Sim.GetPlayer().S;
			if (Step % 15 == 0 && !Sim.CheckSolvability(PlayerS, PlayerS + Sim.GetProfile().Windows.SpawnAheadMaxM))
			{
				AddError(FString::Printf(TEXT("Step %d: every player lane is blocked in a %.0f m window ahead of the player"), Step, Sim.GetProfile().Fairness.SolvabilityWindowM));
				return false;
			}
		}
		return true;
	}

END_DEFINE_SPEC(FLRTrafficSimSpec)

void FLRTrafficSimSpec::Define()
{
	using namespace LRTrafficSimSpecPrivate;

	Describe("Default data", [this]()
	{
		It("loads Data/Traffic/TrafficProfile_Default.json and simulates one minute with it", [this]()
		{
			FLRTrafficProfile Profile;
			FString Error;
			const bool bLoaded = FLRDataLoader::Load(TEXT("Traffic/TrafficProfile_Default.json"), Profile, &Error);
			if (!TestTrue(FString::Printf(TEXT("Profile loads (%s)"), *Error), bLoaded))
			{
				return;
			}

			const TCHAR* Expected[] = { TEXT("sedan"), TEXT("hatchback"), TEXT("suv"), TEXT("pickup"), TEXT("van"), TEXT("truck"), TEXT("bus") };
			for (const TCHAR* Name : Expected)
			{
				const FLRTrafficArchetype* Arch = Profile.Archetypes.FindByPredicate([Name](const FLRTrafficArchetype& Candidate) { return Candidate.Name == FName(Name); });
				if (TestNotNull(FString::Printf(TEXT("Archetype %s"), Name), Arch))
				{
					const bool bHeavy = FName(Name) == FName(TEXT("truck")) || FName(Name) == FName(TEXT("bus"));
					TestEqual(FString::Printf(TEXT("%s prefers the right lanes"), Name), Arch->bPreferRightLanes, bHeavy);
				}
			}
			TestTrue(TEXT("Density curve"), Profile.DensityCurve.Num() >= 2);
			TestTrue(TEXT("Density grows with run time"), Profile.DensityCurve.Last().Y > Profile.DensityCurve[0].Y);

			const TArray<FLRLaneDesc> Lanes = MakeLanes(4, false);
			FScriptedPlayer Player;
			Player.Init(Lanes, 400.0);
			FLRTrafficSim Sim;
			Sim.Configure(Profile, Lanes, SpeedLimitMs);
			Sim.SetPlayer(Player.State);
			Sim.Reset(77, true);
			RunChecked(Sim, Player, Lanes, StepsPerMinute);
		});
	});

	Describe("Density curve", [this]()
	{
		It("interpolates linearly and clamps at both ends", [this]()
		{
			const TArray<FVector2D> Curve = { FVector2D(0.0, 10.0), FVector2D(100.0, 20.0), FVector2D(200.0, 20.0) };
			TestNearlyEqual(TEXT("Before start"), FLRTrafficSim::EvaluateDensityCurve(Curve, -5.f), 10.f, 1.0e-4f);
			TestNearlyEqual(TEXT("Middle"), FLRTrafficSim::EvaluateDensityCurve(Curve, 50.f), 15.f, 1.0e-4f);
			TestNearlyEqual(TEXT("After end"), FLRTrafficSim::EvaluateDensityCurve(Curve, 900.f), 20.f, 1.0e-4f);
			TestNearlyEqual(TEXT("Empty"), FLRTrafficSim::EvaluateDensityCurve(TArray<FVector2D>(), 5.f), 0.f, 1.0e-4f);
		});
	});

	Describe("Single lane", [this]()
	{
		It("stops behind a wreck without overlap and removes the wreck after its lifetime", [this]()
		{
			FLRTrafficProfile Profile = MakeProfile();
			Profile.WreckLifetimeSeconds = 100.f;
			const TArray<FLRLaneDesc> Lanes = MakeLanes(1, false);

			FLRTrafficSim Sim;
			Sim.Configure(Profile, Lanes, SpeedLimitMs);
			Sim.SetDensityScale(0.f);
			Sim.Reset(7, false);

			FLRTrafficSpawnRequest Leader;
			Leader.Lane = 0;
			Leader.S = 400.0;
			Leader.SpeedMs = 0.f;
			Leader.ArchetypeIndex = 0;
			Leader.DriverIndex = 0;
			Leader.bIgnoreChecks = true;
			const int32 LeaderId = Sim.AddVehicle(Leader);

			FLRTrafficSpawnRequest Follower = Leader;
			Follower.S = 150.0;
			Follower.SpeedMs = 30.f;
			Follower.DesiredSpeedMs = 33.f;
			const int32 FollowerId = Sim.AddVehicle(Follower);

			if (!TestTrue(TEXT("Spawned"), LeaderId != INDEX_NONE && FollowerId != INDEX_NONE))
			{
				return;
			}
			TestTrue(TEXT("Wrecked"), Sim.SetWreckedById(LeaderId));

			FString Error;
			for (int32 Step = 0; Step < StepsPerMinute; ++Step)
			{
				Sim.Step(StepSeconds);
				if (!Sim.ValidateNoLaneOverlap(&Error) || !Sim.ValidateFiniteState(&Error))
				{
					AddError(FString::Printf(TEXT("Step %d: %s"), Step, *Error));
					return;
				}
			}

			const int32 LeaderSlot = Sim.FindSlotById(LeaderId);
			const int32 FollowerSlot = Sim.FindSlotById(FollowerId);
			if (!TestTrue(TEXT("Both alive"), LeaderSlot != FLRTrafficSim::NoSlot && FollowerSlot != FLRTrafficSim::NoSlot))
			{
				return;
			}
			const double Gap = (Sim.GetS(LeaderSlot) - 0.5 * Sim.GetLength(LeaderSlot)) - (Sim.GetS(FollowerSlot) + 0.5 * Sim.GetLength(FollowerSlot));
			TestTrue(FString::Printf(TEXT("Follower stopped (%.3f m/s)"), Sim.GetSpeed(FollowerSlot)), Sim.GetSpeed(FollowerSlot) < 0.2f);
			TestTrue(FString::Printf(TEXT("Positive gap (%.3f m)"), Gap), Gap > 0.0 && Gap < 10.0);
			TestEqual(TEXT("Wreck does not move"), Sim.GetS(LeaderSlot), 400.0);
			TestTrue(TEXT("Wreck has brake lights"), (Sim.GetLightFlags(LeaderSlot) & LRTrafficLights::Brake) != 0);

			// Past the wreck lifetime the obstacle disappears and the follower drives on.
			for (int32 Step = 0; Step < StepsPerMinute; ++Step)
			{
				Sim.Step(StepSeconds);
			}
			TestEqual(TEXT("Wreck removed"), Sim.FindSlotById(LeaderId), static_cast<int32>(FLRTrafficSim::NoSlot));
			const int32 FollowerSlotAfter = Sim.FindSlotById(FollowerId);
			if (TestTrue(TEXT("Follower still simulated"), FollowerSlotAfter != FLRTrafficSim::NoSlot))
			{
				TestTrue(TEXT("Follower moves again"), Sim.GetSpeed(FollowerSlotAfter) > 5.f);
			}
		});

		It("never lets a vehicle overtake inside a lane", [this]()
		{
			const FLRTrafficProfile Profile = MakeProfile();
			const TArray<FLRLaneDesc> Lanes = MakeLanes(1, false);

			FLRTrafficSim Sim;
			Sim.Configure(Profile, Lanes, SpeedLimitMs);
			Sim.SetDensityScale(0.f);
			Sim.Reset(3, false);

			// A fast vehicle 12 m behind a slow truck: the hard clamp must hold even if IDM alone could not brake in time.
			FLRTrafficSpawnRequest Slow;
			Slow.Lane = 0;
			Slow.S = 300.0;
			Slow.SpeedMs = 10.f;
			Slow.DesiredSpeedMs = 10.f;
			Slow.ArchetypeIndex = 2;
			Slow.bIgnoreChecks = true;
			Sim.AddVehicle(Slow);

			FLRTrafficSpawnRequest Fast = Slow;
			Fast.S = 278.0;
			Fast.SpeedMs = 45.f;
			Fast.DesiredSpeedMs = 45.f;
			Fast.ArchetypeIndex = 0;
			Sim.AddVehicle(Fast);

			FString Error;
			for (int32 Step = 0; Step < 20 * 60; ++Step)
			{
				Sim.Step(StepSeconds);
				if (!Sim.ValidateNoLaneOverlap(&Error))
				{
					AddError(FString::Printf(TEXT("Step %d: %s"), Step, *Error));
					return;
				}
			}
			TestEqual(TEXT("Both vehicles alive"), Sim.GetNumVehicles(), 2);
		});
	});

	Describe("One-way freeway", [this]()
	{
		It("runs 10 simulated minutes without overlaps, invalid values or blocked windows", [this]()
		{
			const FLRTrafficProfile Profile = MakeProfile();
			const TArray<FLRLaneDesc> Lanes = MakeLanes(4, false);

			FScriptedPlayer Player;
			Player.Init(Lanes, 400.0);

			FLRTrafficSim Sim;
			Sim.Configure(Profile, Lanes, SpeedLimitMs);
			Sim.SetPlayer(Player.State);
			Sim.Reset(1234, true);
			TestTrue(TEXT("Prefill spawns vehicles"), Sim.GetNumVehicles() > 20);
			TestTrue(TEXT("Solvability is active"), Sim.IsSolvabilityActive());

			if (!RunChecked(Sim, Player, Lanes, 10 * StepsPerMinute))
			{
				return;
			}

			TestEqual(TEXT("No pre-existing solvability violation was ever found"), Sim.GetNumSolvabilityViolations(), 0);
			TestTrue(FString::Printf(TEXT("Traffic is alive (%d vehicles)"), Sim.GetNumVehicles()), Sim.GetNumVehicles() > 20);
			TestTrue(TEXT("Capacity respected"), Sim.GetNumVehicles() <= Sim.GetCapacity());
			TestTrue(FString::Printf(TEXT("Vehicles change lanes (%d)"), Sim.GetNumLaneChangesStarted()), Sim.GetNumLaneChangesStarted() > 0);
			TestNearlyEqual(TEXT("Simulated time"), Sim.GetSimTime(), 600.0, 0.01);
		});

		It("is deterministic: same seed gives the same state, another seed does not", [this]()
		{
			const FLRTrafficProfile Profile = MakeProfile();
			const TArray<FLRLaneDesc> Lanes = MakeLanes(4, false);

			auto MakeRun = [&Profile, &Lanes](int32 Seed, FLRTrafficSim& Sim, FScriptedPlayer& Player)
			{
				Player.Init(Lanes, 400.0);
				Sim.Configure(Profile, Lanes, SpeedLimitMs);
				Sim.SetPlayer(Player.State);
				Sim.Reset(Seed, true);
			};

			FLRTrafficSim SimA;
			FLRTrafficSim SimB;
			FLRTrafficSim SimC;
			FScriptedPlayer PlayerA;
			FScriptedPlayer PlayerB;
			FScriptedPlayer PlayerC;
			MakeRun(42, SimA, PlayerA);
			MakeRun(42, SimB, PlayerB);
			MakeRun(43, SimC, PlayerC);

			TestEqual(TEXT("Same prefill"), SimA.ComputeChecksum(), SimB.ComputeChecksum());
			for (int32 Step = 0; Step < 3 * StepsPerMinute; ++Step)
			{
				auto StepRun = [&Lanes, Step](FLRTrafficSim& Sim, FScriptedPlayer& Player)
				{
					Sim.SetPlayer(Player.State);
					Sim.Step(StepSeconds);
					Player.Advance(Lanes, Step);
				};
				StepRun(SimA, PlayerA);
				StepRun(SimB, PlayerB);
				StepRun(SimC, PlayerC);
				if (Step % 600 == 599 && SimA.ComputeChecksum() != SimB.ComputeChecksum())
				{
					AddError(FString::Printf(TEXT("Checksums diverge at step %d"), Step));
					return;
				}
			}
			TestEqual(TEXT("Same vehicle count"), SimA.GetNumVehicles(), SimB.GetNumVehicles());
			TestTrue(TEXT("Another seed gives another state"), SimA.ComputeChecksum() != SimC.ComputeChecksum());
		});

		It("spawns new vehicles only in the allowed windows", [this]()
		{
			const FLRTrafficProfile Profile = MakeProfile();
			const TArray<FLRLaneDesc> Lanes = MakeLanes(4, false);

			FScriptedPlayer Player;
			Player.Init(Lanes, 400.0);

			FLRTrafficSim Sim;
			Sim.Configure(Profile, Lanes, SpeedLimitMs);
			Sim.SetPlayer(Player.State);
			Sim.Reset(99, false);

			const FLRTrafficWindows& Win = Sim.GetProfile().Windows;
			TSet<int32> Seen;
			int32 AheadSpawns = 0;
			int32 BehindSpawns = 0;
			for (int32 Step = 0; Step < 2 * StepsPerMinute; ++Step)
			{
				Sim.SetPlayer(Player.State);
				Sim.Step(StepSeconds);
				Player.Advance(Lanes, Step);

				const FLRTrafficPlayerState& SimPlayer = Sim.GetPlayer();
				const bool bPlayerSlow = SimPlayer.SpeedMs < SpeedLimitMs * 0.8f;
				for (int32 Slot = 0; Slot < Sim.GetCapacity(); ++Slot)
				{
					if (!Sim.IsAlive(Slot) || Seen.Contains(Sim.GetId(Slot)))
					{
						continue;
					}
					Seen.Add(Sim.GetId(Slot));
					const double Relative = Sim.GetS(Slot) - SimPlayer.S;
					const bool bAhead = Relative >= Win.SpawnAheadMinM - 0.01 && Relative <= Win.SpawnAheadMaxM + 0.01;
					const bool bBehind = bPlayerSlow && Relative <= -Win.SpawnBehindMinM + 0.01 && Relative >= -Win.SpawnBehindMaxM - 0.01;
					if (!bAhead && !bBehind)
					{
						AddError(FString::Printf(TEXT("Step %d: vehicle %d spawned at %.1f m from the player (slow=%d)"), Step, Sim.GetId(Slot), Relative, bPlayerSlow ? 1 : 0));
						return;
					}
					(bAhead ? AheadSpawns : BehindSpawns)++;
				}
			}
			TestTrue(TEXT("Vehicles spawned ahead"), AheadSpawns > 0);
			TestTrue(TEXT("Vehicles spawned behind while the player was slow"), BehindSpawns > 0);
		});
	});

	Describe("Two-way road", [this]()
	{
		It("simulates oncoming lanes toward decreasing S without overlaps", [this]()
		{
			const FLRTrafficProfile Profile = MakeProfile();
			const TArray<FLRLaneDesc> Lanes = MakeLanes(2, true);
			if (!TestEqual(TEXT("Four lanes"), Lanes.Num(), 4) || !TestEqual(TEXT("Oncoming lanes on the left"), Lanes[0].Direction, -1))
			{
				return;
			}

			FScriptedPlayer Player;
			Player.Init(Lanes, 400.0);

			FLRTrafficSim Sim;
			Sim.Configure(Profile, Lanes, SpeedLimitMs);
			Sim.SetPlayer(Player.State);
			Sim.Reset(2024, true);

			if (!RunChecked(Sim, Player, Lanes, 3 * StepsPerMinute))
			{
				return;
			}

			int32 Oncoming = 0;
			int32 MovingBackwards = 0;
			const double PlayerS = Sim.GetPlayer().S;
			for (int32 Slot = 0; Slot < Sim.GetCapacity(); ++Slot)
			{
				if (!Sim.IsAlive(Slot) || Sim.GetDirection(Slot) > 0)
				{
					continue;
				}
				++Oncoming;
				TestTrue(TEXT("Oncoming vehicle in an oncoming lane"), Lanes[Sim.GetLane(Slot)].Direction < 0);
				double PrevS = 0.0;
				float PrevD = 0.f;
				Sim.GetInterpolated(Slot, 0.f, PrevS, PrevD);
				MovingBackwards += (Sim.GetS(Slot) > PrevS + 1.0e-6) ? 1 : 0;
			}
			TestTrue(FString::Printf(TEXT("Oncoming traffic exists (%d)"), Oncoming), Oncoming > 0);
			TestEqual(TEXT("No oncoming vehicle moves toward +S"), MovingBackwards, 0);
			TestTrue(TEXT("Player advanced"), PlayerS > 400.0);
		});
	});

	Describe("Fairness", [this]()
	{
		It("never starts a non-aggressive lane change into the player's lane with TTC below the limit", [this]()
		{
			FLRTrafficProfile Profile = MakeProfile();
			for (FLRTrafficDriverProfile& Driver : Profile.DriverProfiles)
			{
				Driver.AggressiveEventChance = 0.f;
			}
			const TArray<FLRLaneDesc> Lanes = MakeLanes(4, false);

			FScriptedPlayer Player;
			Player.Init(Lanes, 400.0);

			FLRTrafficSim Sim;
			Sim.Configure(Profile, Lanes, SpeedLimitMs);
			Sim.SetPlayer(Player.State);
			Sim.Reset(5, true);

			const float TtcLimit = Sim.GetProfile().Fairness.FairnessTtcSeconds;
			TArray<uint8> WasMoving;
			WasMoving.SetNumZeroed(Sim.GetCapacity());
			for (int32 Step = 0; Step < 3 * StepsPerMinute; ++Step)
			{
				Sim.SetPlayer(Player.State);
				Sim.Step(StepSeconds);
				const FLRTrafficPlayerState& P = Sim.GetPlayer();

				for (int32 Slot = 0; Slot < Sim.GetCapacity(); ++Slot)
				{
					const bool bMoving = Sim.IsAlive(Slot) && Sim.GetPhase(Slot) == ELRLaneChangePhase::Moving;
					if (bMoving && !WasMoving[Slot])
					{
						// A lane change just started: if its target lane holds the player and it is ahead, check the TTC.
						const FLRLaneDesc& Target = Lanes[Sim.GetTargetLane(Slot)];
						const bool bPlayerLane = FMath::Abs(P.D - Target.CenterD) < 0.5f * (Target.WidthMeters + P.WidthM);
						const double Gap = (Sim.GetS(Slot) - P.S) - 0.5 * (Sim.GetLength(Slot) + P.LengthM);
						const double Closing = P.SpeedMs - Sim.GetSpeed(Slot);
						if (bPlayerLane && Gap > 0.0 && Closing > 0.0 && Gap / Closing < TtcLimit - 0.05)
						{
							AddError(FString::Printf(TEXT("Step %d: lane change into the player's lane with TTC %.2f s"), Step, Gap / Closing));
							return;
						}
					}
					WasMoving[Slot] = bMoving ? 1 : 0;
				}
				Player.Advance(Lanes, Step);
			}
			TestEqual(TEXT("No aggressive event"), Sim.GetNumAggressiveEvents(), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
