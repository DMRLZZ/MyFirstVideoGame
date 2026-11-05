// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"
#include "LRRoadGenerator.h"
#include "LRRoadLayout.h"
#include "LRRoadPath.h"
#include "LRRoadTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "LRRoadSubsystem.generated.h"

class ALREnvironmentActor;
class ALRRoadStreamer;
struct FLRRunResetEvent;

/**
 * Central highway API of a game world: road coordinates (S, D) <-> world, lanes, speed limit and streaming.
 *
 * - Owns the analytic FLRRoadPath of the current run and grows it procedurally ahead of the player.
 * - Rebuilds deterministically from the run seed when ULRGameEventSubsystem::OnRunReset fires (synchronously,
 *   before any listener bound later, e.g. vehicle / traffic resets run on a complete road).
 * - Spawns one ALRRoadStreamer (meshes + collision) and one ALREnvironmentActor (sky / lights) at begin play.
 * - Caches the player road coordinate every tick (player pawn from ULRRunContextSubsystem).
 *
 * Exists only in Game / PIE worlds. Units: meters / seconds in road space, centimeters in world space.
 */
UCLASS()
class LRWORLD_API ULRRoadSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Returns the road subsystem of WorldContext's world, or nullptr (no world / unsupported world type). */
	static ULRRoadSubsystem* Get(const UObject* WorldContext);

	/** Road model of the current run (valid for the window around the player). */
	const FLRRoadPath& GetPath() const { return Path; }

	/** Cross-section profile of the current route. */
	const FLRRoadProfile& GetProfile() const { return Route.Profile; }

	/** Full definition of the current route (profile, generator, streaming, dressing). */
	const FLRRouteDefinition& GetRoute() const { return Route; }

	/** Lateral layout (lanes, shoulders, barriers) of the current route. */
	const FLRRoadCrossSection& GetCrossSection() const { return CrossSection; }

	/** Road coordinates -> world transform (location cm, yaw = road heading, pitch from grade). */
	FTransform RoadToWorld(double S, float D) const { return Path.RoadToWorld(S, D); }

	/** World location (cm) -> road coordinates. Keep one hint per querying entity (start with INDEX_NONE). */
	bool WorldToRoad(const FVector& WorldLocationCm, FLRRoadCoord& OutCoord, int32* InOutSegmentHint = nullptr) const
	{
		return Path.WorldToRoad(WorldLocationCm, OutCoord, InOutSegmentHint);
	}

	/** Number of lanes (both directions). */
	int32 GetNumLanes() const { return Path.GetLanes().Num(); }

	/** Lane by index (0 = leftmost). Returns a default lane for invalid indices. */
	const FLRLaneDesc& GetLane(int32 LaneIndex) const;

	/** Index of the lane nearest to D, INDEX_NONE if there are no lanes. */
	int32 FindNearestLane(float D) const { return Path.FindNearestLane(D); }

	/** Center D of a lane, meters (0 for invalid indices). */
	float GetLaneCenterD(int32 LaneIndex) const { return GetLane(LaneIndex).CenterD; }

	/** Posted speed limit at S, km/h. */
	float GetSpeedLimitKmh(double S) const;

	/** Player road coordinate cached this tick. Returns false if there is no player pawn or it is off the path. */
	bool GetPlayerCoord(FLRRoadCoord& Out) const;

	/** S where the player starts each run, meters. */
	double GetSpawnS() const { return Route.SpawnS; }

	/** Global lane index where the player starts (always a player-direction lane). */
	int32 GetDefaultPlayerLane() const;

	/** Environment actor spawned by this subsystem (may be null if disabled in ULRWorldSettings). */
	ALREnvironmentActor* GetEnvironmentActor() const { return EnvironmentActor; }

	/** Road streamer spawned by this subsystem (may be null if disabled in ULRWorldSettings). */
	ALRRoadStreamer* GetRoadStreamer() const { return RoadStreamer; }

	/** Seed of the current road layout. */
	int32 GetRoadSeed() const { return RoadSeed; }

	/** True once a road was built (run reset or fallback build). */
	bool HasRoad() const { return bHasRoad; }

	/**
	 * Rebuilds the route deterministically: loads the one-way or two-way route (ULRWorldSettings), resets the path,
	 * generates the layout from LRRandom::MakeStream(Seed, RoadLayout) and rebuilds the visible window around SpawnS.
	 */
	void RebuildForSeed(int32 Seed, bool bTwoWay);

	//~ UTickableWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Bus callback: rebuilds the road for the new run. */
	void HandleRunReset(const FLRRunResetEvent& Event);

	/** Loads a route file (keeps C++ defaults on failure) and sanitizes it. */
	FLRRouteDefinition LoadRoute(const FString& RelativePath, bool bTwoWayFallback) const;

	/** Updates CachedPlayerCoord from the player pawn. */
	void UpdatePlayerCoord();

	/** Spawns the streamer / environment actors if allowed and missing. */
	void SpawnWorldActors(UWorld& InWorld);

	/** Pushes the current route to the spawned actors and rebuilds the visible window. */
	void ApplyRouteToActors();

	FLRRoadPath Path;
	FLRRoadGenerator Generator;
	FRandomStream LayoutStream;
	FLRRouteDefinition Route;
	FLRRoadCrossSection CrossSection;

	UPROPERTY(Transient)
	TObjectPtr<ALRRoadStreamer> RoadStreamer;

	UPROPERTY(Transient)
	TObjectPtr<ALREnvironmentActor> EnvironmentActor;

	FDelegateHandle RunResetHandle;

	FLRRoadCoord CachedPlayerCoord;

	/** S around which the road streams (last valid player S, SpawnS after a rebuild), meters. */
	double LastFocusS = 0.0;
	int32 PlayerSegmentHint = INDEX_NONE;
	int32 RoadSeed = 0;
	bool bHasPlayerCoord = false;
	bool bHasRoad = false;
	bool bWorldBegunPlay = false;
};
