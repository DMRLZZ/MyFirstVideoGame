// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRRoadTypes.h"
#include "LRTrafficTypes.h"
#include "Math/RandomStream.h"
#include "Sim/LRIdm.h"
#include "Sim/LRMobil.h"

/** Lane change state of a traffic vehicle. */
enum class ELRLaneChangePhase : uint8
{
	/** Driving in its lane. */
	None,
	/** Blinker on, still in the origin lane (1.0 - 2.0 s). */
	Signaling,
	/** Lateral smootherstep motion; the vehicle counts in BOTH lanes. */
	Moving
};

/** Internal per-vehicle flag bits of FLRTrafficSim (the low 3 bits match LRTrafficLights). */
namespace LRTrafficSimFlags
{
	inline constexpr uint8 Brake = 1 << 0;
	inline constexpr uint8 BlinkerLeft = 1 << 1;
	inline constexpr uint8 BlinkerRight = 1 << 2;
	inline constexpr uint8 Wrecked = 1 << 3;
	inline constexpr uint8 Aggressive = 1 << 4;
	inline constexpr uint8 Alive = 1 << 5;
	inline constexpr uint8 LightMask = Brake | BlinkerLeft | BlinkerRight;
}

/** The player as seen by the traffic simulation: an obstacle (leader / follower) that never decides. */
struct FLRTrafficPlayerState
{
	/** False when there is no player (no obstacle; windows are centered on S anyway). */
	bool bValid = false;

	/** Road distance of the player center, meters. */
	double S = 0.0;

	/** Lateral offset of the player center, meters. */
	float D = 0.f;

	/** Signed speed along +S, m/s. */
	float SpeedMs = 0.f;

	/** Footprint length, meters. */
	float LengthM = 4.5f;

	/** Footprint width, meters. */
	float WidthM = 1.9f;
};

/** Explicit spawn request (tests, tools, scripted events). Negative values mean "pick automatically". */
struct FLRTrafficSpawnRequest
{
	/** Global lane index. */
	int32 Lane = 0;

	/** Road distance of the vehicle center, meters. */
	double S = 0.0;

	/** Initial speed, m/s (< 0 = desired speed capped by the leader). */
	float SpeedMs = -1.f;

	/** Desired speed, m/s (< 0 = random from archetype and driver factors). */
	float DesiredSpeedMs = -1.f;

	/** Index into FLRTrafficProfile::Archetypes (< 0 = weighted random). */
	int32 ArchetypeIndex = -1;

	/** Index into FLRTrafficProfile::DriverProfiles (< 0 = archetype forced profile or weighted random). */
	int32 DriverIndex = -1;

	/** Skip the gap, player and solvability checks (tests only). */
	bool bIgnoreChecks = false;
};

/**
 * Pure traffic simulation (no UObject, no world): IDM car following + MOBIL lane changes on the lanes of a road,
 * in road coordinates (S, D). Owned by ULRTrafficSubsystem, unit-testable in isolation.
 *
 * Layout: struct-of-arrays over a fixed number of slots (FLRTrafficProfile::MaxVehicles), allocated once in
 * Configure; Step() does not allocate. Per-lane member lists are kept sorted by S (vehicles never overtake inside a
 * lane, so they stay sorted; insertion sort repairs the rare disorder). A vehicle in the Moving phase of a lane change
 * is a member of both lanes. The global update order is "front first" (key S * Direction, descending) so every
 * vehicle is integrated after its leaders, which allows exact no-overlap and solvability clamps.
 *
 * Speeds are stored as magnitudes (>= 0); Direction (+1 / -1) gives the sense along S. Oncoming vehicles
 * (Direction -1) move toward decreasing S and have their own leader ordering.
 *
 * Fairness (docs/02-ARCHITECTURE.md §5.2):
 * - No lane change into a lane occupied by the player, ahead of the player, when TTC < FairnessTtcSeconds, unless an
 *   "aggressive driver" event (always announced with >= AggressiveMinBlinkSeconds of blinker).
 * - Spawns only in [playerS + SpawnAheadMin, playerS + SpawnAheadMax], or behind out of view while the player is
 *   slower than traffic. The run reset prefill is the only exception (the scene is not visible yet).
 * - Solvability invariant: with each player-direction vehicle blocking the lane nearest to its center, no window of
 *   SolvabilityWindowM meters has every player-direction lane blocked (wrecks excluded). Enforced at spawn, when
 *   approving a lane change, when a lane change crosses mid-lane, and on every forward motion (each vehicle may
 *   not advance its front into a position that would close the last free lane of a window).
 *
 * Determinism: all randomness comes from LRRandom streams TrafficSpawn / TrafficBehavior of the seed; slot order and
 * update order are deterministic, so the same seed and inputs give bit-identical states (see ComputeChecksum).
 */
class LRTRAFFIC_API FLRTrafficSim
{
public:
	/** Index value meaning "no vehicle". */
	static constexpr int32 NoSlot = INDEX_NONE;

	/**
	 * Stores (and sanitizes) the profile, the lanes and the speed limit, and allocates every array.
	 * Clears all vehicles. Lanes must be sorted by CenterD ascending (FLRRoadPath convention).
	 */
	void Configure(const FLRTrafficProfile& InProfile, TConstArrayView<FLRLaneDesc> InLanes, float InSpeedLimitMs);

	/** Removes every vehicle, reseeds the random streams from Seed and optionally prefills the window around the player. */
	void Reset(int32 Seed, bool bPrefill = true);

	/** Updates the player obstacle (call before Step; between steps the player S is extrapolated with its speed). */
	void SetPlayer(const FLRTrafficPlayerState& InPlayer);

	/** Changes the posted speed limit, m/s (affects new vehicles only). */
	void SetSpeedLimit(float InSpeedLimitMs);

	/** Multiplies the density curve (0 = no new vehicles). */
	void SetDensityScale(float InScale) { DensityScale = FMath::Max(0.f, InScale); }

	/** Advances the simulation by one fixed step, seconds. */
	void Step(float DeltaSeconds);

	/** Spawns a vehicle. Returns its id, or INDEX_NONE when rejected (checks) or the simulation is full. */
	int32 AddVehicle(const FLRTrafficSpawnRequest& Request);

	/** Turns a vehicle into a stopped obstacle removed after WreckLifetimeSeconds. Returns false for unknown ids. */
	bool SetWreckedById(int32 Id);

	/** Removes a vehicle immediately. Returns false for unknown ids. */
	bool RemoveById(int32 Id);

	// --- Queries ------------------------------------------------------------------------------------

	/** Number of live vehicles (both directions). */
	int32 GetNumVehicles() const { return NumAlive; }

	/** Number of slots (FLRTrafficProfile::MaxVehicles). */
	int32 GetCapacity() const { return Capacity; }

	/** True if the slot holds a live vehicle. */
	bool IsAlive(int32 Slot) const { return Flags.IsValidIndex(Slot) && (Flags[Slot] & LRTrafficSimFlags::Alive) != 0; }

	/** Slot of a vehicle id, NoSlot if unknown. O(capacity). */
	int32 FindSlotById(int32 Id) const;

	/** Fills a snapshot of a live slot (current step state, not interpolated). */
	void GetSnapshot(int32 Slot, FLRTrafficVehicleSnapshot& Out) const;

	/** Position interpolated between the previous and the current step (Alpha 0..1), for rendering. */
	void GetInterpolated(int32 Slot, float Alpha, double& OutS, float& OutD) const;

	int32 GetId(int32 Slot) const { return Id[Slot]; }
	double GetS(int32 Slot) const { return S[Slot]; }
	float GetD(int32 Slot) const { return D[Slot]; }
	float GetSpeed(int32 Slot) const { return Speed[Slot]; }
	float GetAccel(int32 Slot) const { return Accel[Slot]; }
	float GetLength(int32 Slot) const { return Length[Slot]; }
	float GetWidth(int32 Slot) const { return Width[Slot]; }
	float GetYawOffsetDeg(int32 Slot) const { return YawOffsetDeg[Slot]; }
	int32 GetDirection(int32 Slot) const { return Direction[Slot]; }
	int32 GetLane(int32 Slot) const { return Lane[Slot]; }
	int32 GetTargetLane(int32 Slot) const { return TargetLane[Slot]; }
	ELRLaneChangePhase GetPhase(int32 Slot) const { return static_cast<ELRLaneChangePhase>(Phase[Slot]); }
	int32 GetArchetypeIndex(int32 Slot) const { return Archetype[Slot]; }
	int32 GetDriverIndex(int32 Slot) const { return Driver[Slot]; }
	int32 GetColorIndex(int32 Slot) const { return ColorIndex[Slot]; }
	uint8 GetLightFlags(int32 Slot) const { return Flags[Slot] & LRTrafficSimFlags::LightMask; }
	bool IsWrecked(int32 Slot) const { return (Flags[Slot] & LRTrafficSimFlags::Wrecked) != 0; }

	/** Seconds simulated since Reset. */
	double GetSimTime() const { return SimTime; }

	/** Player obstacle (S extrapolated during the last steps). */
	const FLRTrafficPlayerState& GetPlayer() const { return Player; }

	/** Sanitized profile in use. */
	const FLRTrafficProfile& GetProfile() const { return Profile; }

	/** Lanes in use. */
	const TArray<FLRLaneDesc>& GetLanes() const { return Lanes; }

	/** Density (vehicles / km / lane) of the profile curve at a run time, seconds (not scaled). */
	float EvaluateDensity(float RunTimeSeconds) const { return EvaluateDensityCurve(Profile.DensityCurve, RunTimeSeconds); }

	/** Piecewise-linear evaluation of a density curve (X seconds, Y vehicles/km/lane), clamped at both ends. */
	static float EvaluateDensityCurve(const TArray<FVector2D>& Curve, float RunTimeSeconds);

	/** Number of vehicles per lane whose center lies in [S0, S1]; OutCounts is resized to the lane count. */
	void GetLaneOccupancy(double S0, double S1, TArray<int32>& OutCounts) const;

	/** Number of lane changes started since Reset (statistics). */
	int32 GetNumLaneChangesStarted() const { return NumLaneChangesStarted; }

	/** Number of aggressive lane change events since Reset (statistics). */
	int32 GetNumAggressiveEvents() const { return NumAggressiveEvents; }

	/** Number of times a pre-existing solvability violation was detected (should stay 0). */
	int32 GetNumSolvabilityViolations() const { return NumSolvabilityViolations; }

	// --- Validation (tests / debug) ------------------------------------------------------------------

	/** CRC of the full dynamic state of live vehicles (determinism checks). */
	uint32 ComputeChecksum() const;

	/** True if no two members of the same lane overlap longitudinally (and lane lists are sorted). */
	bool ValidateNoLaneOverlap(FString* OutError = nullptr) const;

	/** True if every live vehicle has finite S / D / speed / acceleration and a non-negative speed. */
	bool ValidateFiniteState(FString* OutError = nullptr) const;

	/** True if no solvability window starting in [FromS, ToS] has every player-direction lane blocked. */
	bool CheckSolvability(double FromS, double ToS) const;

	/** True when the solvability invariant applies (enabled and at least two player-direction lanes). */
	bool IsSolvabilityActive() const { return bSolvabilityActive; }

private:
	/** A leader or follower relative to a vehicle: another slot, the player, or nothing. */
	struct FNeighbor
	{
		int32 Slot = NoSlot;
		bool bPlayer = false;
		/** Bumper-to-bumper distance, meters. */
		double Gap = TNumericLimits<double>::Max();
		/** Speed of the neighbor projected on the asking vehicle's travel direction, m/s. */
		float SpeedInFrame = 0.f;

		bool IsValid() const { return Slot != NoSlot || bPlayer; }
	};

	/** Result of a MOBIL candidate evaluation. */
	struct FLaneChangeCandidate
	{
		int32 TargetLane = INDEX_NONE;
		float Utility = 0.f;
		bool bApproved = false;
		bool bAggressive = false;
	};

	// Setup / slots
	void SanitizeProfile();
	void ClearVehicles();
	int32 AllocateSlot();
	void FreeSlot(int32 Slot);
	void Prefill();
	int32 ChooseArchetype(int32 ForLane);
	int32 ChooseDriver(int32 ArchetypeIndex, int32 ForLane);
	float LanePreferenceWeight(bool bPreferRight, bool bPreferLeft, int32 LaneIndex) const;
	int32 ChooseLaneForArchetype(int32 ArchetypeIndex, int32 DriverIndex, int32 InDirection);
	void SpawnStep();

	// Lane lists
	void InsertIntoLane(int32 LaneIndex, int32 Slot);
	void RebuildLaneLists();
	void SortOrder();
	bool IsMemberOfLane(int32 Slot, int32 LaneIndex) const;
	int32 LowerBoundS(int32 LaneIndex, double InS) const;

	// Neighbors and IDM
	bool PlayerOccupiesLane(int32 LaneIndex) const;
	FNeighbor FindLeader(int32 LaneIndex, double InS, float InLength, int32 InDirection, int32 ExcludeSlot) const;
	FNeighbor FindFollower(int32 LaneIndex, double InS, float InLength, int32 InDirection, int32 ExcludeSlot) const;
	FLRIdmParams MakeIdmParams(int32 Slot) const;
	FLRIdmParams MakePlayerIdmParams(float PlayerSpeedInFrame) const;
	float AccelWithLeader(const FLRIdmParams& Params, float OwnSpeed, const FNeighbor& Leader) const;
	float ComputeAcceleration(int32 Slot) const;

	// Lane changes
	void UpdateLaneChanges(float DeltaSeconds);
	FLaneChangeCandidate EvaluateCandidate(int32 Slot, int32 Target);
	bool EvaluateMobilFor(int32 Slot, int32 Target, bool bIgnorePlayerFollower, FLRMobilResult& OutResult) const;
	bool IsTargetLaneAllowed(int32 Slot, int32 Target) const;
	bool IsFairTowardPlayer(int32 Slot, int32 Target, bool bAggressive) const;
	bool IsLaneChangeSolvable(int32 Slot, int32 Target, double MarginM) const;
	void StartSignaling(int32 Slot, const FLaneChangeCandidate& Candidate);
	void StartMoving(int32 Slot);
	void AbortLaneChange(int32 Slot);
	void FinishLaneChange(int32 Slot);
	void SetBlinkerFlags(int32 Slot);
	bool IsInMobilWindow(int32 Slot) const;

	// Integration
	void Integrate(float DeltaSeconds);

	// Solvability
	bool QueryCoverage(int32 LaneIndex, double X, int32 ExcludeSlot, double& OutNextStart, int32& OutSlot) const;
	double FirstBlockedPoint(double FromX, int32 ExcludeLane, int32 ExcludeSlot, double MaxX, int32* OutDefiningSlot = nullptr) const;

	double FrontOf(int32 Slot) const { return S[Slot] + 0.5 * Direction[Slot] * Length[Slot]; }
	double RearOf(int32 Slot) const { return S[Slot] - 0.5 * Direction[Slot] * Length[Slot]; }

	FLRTrafficProfile Profile;
	TArray<FLRLaneDesc> Lanes;
	float SpeedLimitMs = 36.1f;
	float DensityScale = 1.f;
	int32 Capacity = 0;
	int32 NumAlive = 0;
	int32 NumPlayerLanes = 0;
	int32 NumOncomingLanes = 0;
	int32 PlayerDriverIndex = 0;
	bool bSolvabilityActive = false;

	FRandomStream SpawnStream;
	FRandomStream BehaviorStream;
	FLRTrafficPlayerState Player;
	double SimTime = 0.0;
	int32 NextId = 1;

	int32 NumLaneChangesStarted = 0;
	int32 NumAggressiveEvents = 0;
	int32 NumSolvabilityViolations = 0;

	// --- Struct of arrays (one entry per slot) ---
	TArray<double> S;
	TArray<double> PrevS;
	TArray<float> D;
	TArray<float> PrevD;
	TArray<float> Speed;
	TArray<float> DesiredSpeed;
	TArray<float> Accel;
	TArray<float> Length;
	TArray<float> Width;
	TArray<float> YawOffsetDeg;
	TArray<int8> Lane;
	TArray<int8> TargetLane;
	/** Lane blocked for the solvability invariant (nearest lane of the center). */
	TArray<int8> BlockLane;
	TArray<int8> Direction;
	TArray<uint8> Phase;
	TArray<float> LaneChangeT;
	TArray<float> LaneChangeDuration;
	TArray<float> LaneChangeFromD;
	TArray<float> SignalTimer;
	TArray<float> PauseTimer;
	TArray<float> MobilTimer;
	TArray<float> CooldownTimer;
	TArray<float> WreckTimer;
	TArray<uint16> Driver;
	TArray<uint16> Archetype;
	TArray<uint8> ColorIndex;
	TArray<uint8> Flags;
	TArray<int32> Id;

	/** Per lane: member slots sorted by S ascending. */
	TArray<TArray<int32>> LaneMembers;
	/** Live slots, front first (S * Direction descending). */
	TArray<int32> Order;
	/** Free slot stack. */
	TArray<int32> FreeSlots;
};
