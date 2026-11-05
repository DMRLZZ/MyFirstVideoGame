// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRTrafficTypes.h"
#include "Sim/LRNearMissTracker.h"
#include "Sim/LRTrafficSim.h"
#include "Subsystems/WorldSubsystem.h"

#include "LRTrafficSubsystem.generated.h"

class AActor;
class ALRTrafficVehicleActor;
class APawn;
class UInstancedStaticMeshComponent;
class ULRRoadSubsystem;
struct FLRCrashEvent;
struct FLRRunResetEvent;

/**
 * Highway traffic of a game world (docs/02-ARCHITECTURE.md §5.2).
 *
 * - Owns the pure FLRTrafficSim (IDM + MOBIL, struct-of-arrays) and steps it with a fixed-step accumulator
 *   (FLRTrafficProfile::FixedStepHz, at most MaxStepsPerFrame steps per frame; excess time is dropped).
 * - Treats the player (ULRRunContextSubsystem pawn, footprint and speed through ILRRoadParticipant) as an obstacle.
 * - Detects near misses with FLRNearMissTracker and broadcasts FLRNearMissEvent on ULRGameEventSubsystem.
 * - Resets on ULRGameEventSubsystem::OnRunReset (lanes, one-way or two-way, come from ULRRoadSubsystem).
 * - On OnCrash against one of its actors: the vehicle becomes a stopped obstacle in the simulation for
 *   WreckLifetimeSeconds while its actor simulates physics, then both are removed.
 * - Presentation: a pool of ALRTrafficVehicleActor (kinematic collision) near the player, and one instanced mesh
 *   without collision for the far vehicles.
 *
 * Exists only in Game / PIE worlds. Budget: <= 1.0 ms of game thread for 200 simulated vehicles.
 */
UCLASS()
class LRTRAFFIC_API ULRTrafficSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Returns the traffic subsystem of WorldContext's world, or nullptr (no world / unsupported world type). */
	static ULRTrafficSubsystem* Get(const UObject* WorldContext);

	/**
	 * Removes every vehicle and rebuilds the traffic deterministically from Seed on the current road lanes
	 * (prefilled around the player spawn). Called automatically on OnRunReset. If the road is not built yet
	 * (or still holds the previous run), the reset is deferred to the next tick.
	 */
	void ResetTraffic(int32 Seed);

	/** Pauses (false) or resumes (true) the simulation. Actors keep their last pose while paused. */
	void SetSimulationEnabled(bool bEnabled);

	/** True while the simulation advances. */
	bool IsSimulationEnabled() const { return bSimulationEnabled; }

	/** Multiplies the density curve of the profile (0 = no new vehicles, 1 = data value). */
	void SetDensityScale(float Scale);

	/** Current density multiplier. */
	float GetDensityScale() const { return DensityScale; }

	/** Number of simulated vehicles (both directions). */
	int32 GetNumVehicles() const { return Simulation.GetNumVehicles(); }

	/** Copies every vehicle whose S lies in [MinS, MaxS] (meters) into Out (Out is reset first). */
	void GetSnapshot(TArray<FLRTrafficVehicleSnapshot>& Out, double MinS, double MaxS) const;

	/** True if Actor is one of the pooled traffic vehicle actors of this subsystem. */
	bool IsTrafficActor(const AActor* Actor) const;

	/** Simulation id of the vehicle bound to a pooled actor, INDEX_NONE if Actor is not an active traffic actor. */
	int32 GetVehicleIdForActor(const AActor* Actor) const;

	/** Read-only access to the simulation (debug views, tests). */
	const FLRTrafficSim& GetSimulation() const { return Simulation; }

	/** Profile loaded from data (before simulation sanitization). */
	const FLRTrafficProfile& GetProfile() const { return Profile; }

	/** Re-reads Data/Traffic/TrafficProfile_Default.json and resets the traffic with the last seed. */
	void ReloadProfile();

	/** Data file of the traffic profile, relative to Data/. */
	static const TCHAR* ProfileFile;

	//~ UTickableWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Bus callback: new run. */
	void HandleRunReset(const FLRRunResetEvent& Event);

	/** Bus callback: the player hit something. */
	void HandleCrash(const FLRCrashEvent& Event);

	/** Data subsystem callback (lr.Data.Reload). */
	void HandleDataReloaded();

	/** Loads the profile file (keeps C++ defaults on failure). */
	void LoadProfile();

	/** Performs the reset now (road must be built). */
	void ResetNow(int32 Seed);

	/** Samples the player pawn (road coordinate, speed along +S, footprint). Returns false without a player on the road. */
	bool SamplePlayer(const ULRRoadSubsystem& Road, FLRTrafficPlayerState& OutPlayer);

	/** The pawn driven by the local player, or nullptr. */
	APawn* FindPlayerPawn() const;

	/** Spawns the actor pool and the far instanced mesh (once, at begin play). */
	void SpawnPresentation(UWorld& InWorld);

	/** Assigns pooled actors to the nearest vehicles, moves them, and fills the far instanced mesh. */
	void UpdatePresentation(const ULRRoadSubsystem& Road, float Alpha, float DeltaTime);

	/** Runs the near-miss tracker on interpolated positions and broadcasts the events. */
	void UpdateNearMisses(const FLRTrafficPlayerState& PlayerState, float Alpha);

	/** Returns a pooled actor to the pool. */
	void ReleaseActor(int32 ActorIndex);

	/** Releases every pooled actor. */
	void ReleaseAllActors();

	/** World transform of a vehicle body center for a road coordinate. */
	FTransform ComputeVehicleTransform(const ULRRoadSubsystem& Road, int32 Slot, double InS, float InD, bool bGroundLevel) const;

	/** Data loaded from ProfileFile. */
	FLRTrafficProfile Profile;

	/** Pure simulation. */
	FLRTrafficSim Simulation;

	/** Pure near-miss detector. */
	FLRNearMissTracker NearMissTracker;

	/** Pooled vehicle actors (spawned at begin play). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ALRTrafficVehicleActor>> ActorPool;

	/** Owner actor of the far instanced mesh. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> FarTrafficActor;

	/** Instanced mesh of the vehicles beyond the actor window. */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> FarTrafficInstances;

	/** Per pooled actor: bound simulation slot (INDEX_NONE = free). */
	TArray<int32> ActorSlot;

	/** Per simulation slot: bound pooled actor index (INDEX_NONE = none). */
	TArray<int32> SlotActor;

	/** Free pooled actor indices (stack). */
	TArray<int32> FreeActors;

	/** Scratch: (distance, slot) of vehicles waiting for an actor. Reused every frame. */
	TArray<TPair<float, int32>> ActorCandidates;

	/** Scratch: far instance transforms. Reused every frame. */
	TArray<FTransform> FarTransforms;

	/** Scratch: near-miss vehicle states. Reused every frame. */
	TArray<FLRNearMissVehicleState> NearMissVehicles;

	/** Scratch: near-miss events produced this frame. Reused every frame. */
	TArray<FLRNearMissEvent> NearMissEvents;

	FDelegateHandle RunResetHandle;
	FDelegateHandle CrashHandle;
	FDelegateHandle DataReloadedHandle;

	/** Unsimulated time carried to the next frame, seconds. */
	double Accumulator = 0.0;

	/** Seed of the current (or pending) traffic. */
	int32 CurrentSeed = 0;

	/** Number of far instances currently allocated in FarTrafficInstances. */
	int32 NumFarInstancesAllocated = 0;

	/** WorldToRoad segment cache of the player projection (reset with the road). */
	int32 PlayerSegmentHint = INDEX_NONE;

	/** Density multiplier applied to the profile curve. */
	float DensityScale = 1.f;

	/** False while paused through SetSimulationEnabled. */
	bool bSimulationEnabled = true;

	/** True once the simulation was configured for a road. */
	bool bHasTraffic = false;

	/** A reset waits for the road of the new run (performed at the next tick). */
	bool bResetPending = false;
};
