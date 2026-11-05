// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"
#include "LREvents.h"
#include "LRTrafficTypes.h"

/** Player state sampled by the near-miss tracker, road coordinates. */
struct FLRNearMissPlayerState
{
	/** Road distance of the player center, meters. */
	double S = 0.0;

	/** Lateral offset of the player center, meters (+ = right). */
	float D = 0.f;

	/** Footprint length, meters. */
	float LengthM = 4.5f;

	/** Footprint width, meters. */
	float WidthM = 1.9f;

	/** Signed speed along +S, m/s. */
	float SpeedMs = 0.f;
};

/** Traffic vehicle state sampled by the near-miss tracker, road coordinates. */
struct FLRNearMissVehicleState
{
	/** Stable vehicle id. */
	int32 Id = INDEX_NONE;

	/** Road distance of the vehicle center, meters. */
	double S = 0.0;

	/** Lateral offset of the vehicle center, meters. */
	float D = 0.f;

	/** Footprint length, meters. */
	float LengthM = 4.6f;

	/** Footprint width, meters. */
	float WidthM = 1.85f;

	/** Signed speed along +S, m/s (negative for oncoming vehicles). */
	float SpeedMs = 0.f;

	/** Archetype name reported in the event. */
	FName Archetype;

	/** Drives against the player's direction. */
	bool bOncoming = false;
};

/**
 * Pure near-miss detector (no UObject, no world).
 *
 * For every vehicle that overlaps the player longitudinally (|Sp - Sv| < (Lp + Lv) / 2) it tracks the minimum lateral
 * clearance |Dp - Dv| - (Wp + Wv) / 2 and the maximum relative speed. When the overlap ends without contact
 * (clearance never <= 0 and no NotifyContact), clearance < CloseThreshold and relative speed > MinRelativeSpeedKmh,
 * it emits an FLRNearMissEvent with its tier. bThreadTheNeedle is set when another qualifying pass on the opposite
 * side of the player completed within ThreadWindowSeconds.
 *
 * The caller must pass every vehicle within a few meters of the overlap range on each update; a vehicle that disappears
 * while overlapping (despawned) is dropped without event. No allocation per update after warm-up.
 */
class LRTRAFFIC_API FLRNearMissTracker
{
public:
	FLRNearMissTracker();

	/** Stores the thresholds (does not reset the tracked passes). */
	void Configure(const FLRNearMissParams& InParams);

	/** Forgets every active and recent pass (new run). */
	void Reset();

	/**
	 * Advances the tracker.
	 * @param TimeSeconds  Monotonic time of this sample, seconds.
	 * @param OutEvents    Receives the completed near misses (appended, not cleared).
	 */
	void Update(double TimeSeconds, const FLRNearMissPlayerState& Player, TConstArrayView<FLRNearMissVehicleState> Vehicles, TArray<FLRNearMissEvent>& OutEvents);

	/** The player touched this vehicle: its current (or next) pass will not produce an event. */
	void NotifyContact(int32 VehicleId);

	/** Tier of a clearance in meters (None at or above the close threshold). */
	ELRNearMissTier ClassifyClearance(float ClearanceM) const;

	/** Number of passes currently tracked. */
	int32 GetNumActivePasses() const { return Active.Num(); }

	/** Thresholds in use. */
	const FLRNearMissParams& GetParams() const { return Params; }

private:
	/** One vehicle overlapping the player. */
	struct FPass
	{
		int32 Id = INDEX_NONE;
		float MinClearanceM = TNumericLimits<float>::Max();
		float MaxRelativeSpeedMs = 0.f;
		FName Archetype;
		/** -1 = vehicle on the player's left, +1 = right (at the minimum clearance). */
		int8 Side = 0;
		bool bContact = false;
		bool bOncoming = false;
		bool bSeen = false;
		/** The vehicle overlapped the player at least once (false for a contact reported before the overlap). */
		bool bOverlapped = false;
	};

	/** A qualifying pass that completed recently (thread-the-needle memory). */
	struct FCompleted
	{
		double TimeSeconds = 0.0;
		int32 Id = INDEX_NONE;
		int8 Side = 0;
	};

	/** Finishes a pass whose overlap ended; appends an event when it qualifies. */
	void FinishPass(const FPass& Pass, double TimeSeconds, const FLRNearMissPlayerState& Player, TArray<FLRNearMissEvent>& OutEvents);

	/** Index of the active pass of a vehicle, INDEX_NONE if none. */
	int32 FindActive(int32 Id) const;

	FLRNearMissParams Params;
	TArray<FPass> Active;
	TArray<FCompleted> Recent;
};
