// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FLRTrafficVehicleSnapshot;

/** Pedal values for the player car, normalized 0..1. */
struct FLRTitleCruisePedals
{
	float Throttle = 0.f;
	float Brake = 0.f;
};

/**
 * Pure logic of the title screen cruise (no UObject, no world): ALRGameModeEndless drives the player car with it while
 * the menu is open, so the highway moves behind the menu without the car running into the traffic ahead.
 */
struct LRGAMEPLAY_API FLRTitleCruise
{
	/**
	 * Finds the nearest vehicle ahead in the player's lane, travelling the player's way.
	 * @param Traffic        Snapshot around the player (ULRTrafficSubsystem::GetSnapshot).
	 * @param LaneHalfWidth  Lateral distance from PlayerD that still counts as the same lane, meters.
	 * @param OutGapMeters   Bumper-to-bumper gap, meters (an approximation from the vehicle length).
	 * @param OutSpeedKmh    Speed of that vehicle, km/h.
	 * @return false if there is no vehicle ahead in the lane.
	 */
	static bool FindLeader(const TArray<FLRTrafficVehicleSnapshot>& Traffic, double PlayerS, float PlayerD, float LaneHalfWidth,
		float& OutGapMeters, float& OutSpeedKmh);

	/**
	 * Speed the car should hold: CruiseKmh, lowered towards the leader's speed when the leader is closer than twice
	 * FollowGapMeters, and down to 0 when it is much closer than FollowGapMeters. Never above CruiseKmh.
	 * @param LeaderGapMeters  Gap to the leader, or a negative value without a leader.
	 */
	static float ComputeTargetSpeedKmh(float CruiseKmh, float FollowGapMeters, float LeaderGapMeters, float LeaderSpeedKmh);

	/** Pedals that move SpeedKmh towards TargetKmh: throttle near and below the target, brake well above it. */
	static FLRTitleCruisePedals ComputePedals(float SpeedKmh, float TargetKmh);
};
