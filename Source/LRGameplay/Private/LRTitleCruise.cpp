// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRTitleCruise.h"

#include "LRTrafficTypes.h"

namespace LRTitleCruisePrivate
{
	/** How fast the target speed falls with the missing gap, km/h per meter. */
	constexpr float SpeedPerGapMeter = 1.5f;

	/** Throttle that roughly holds a cruising speed on a flat road. */
	constexpr float HoldThrottle = 0.3f;

	/** Throttle added per km/h below the target. */
	constexpr float ThrottlePerKmh = 0.05f;

	/** Overspeed tolerated before braking, km/h. */
	constexpr float BrakeDeadbandKmh = 3.f;

	/** Brake added per km/h above the deadband. */
	constexpr float BrakePerKmh = 0.04f;

	constexpr float MsToKmh = 3.6f;
}

bool FLRTitleCruise::FindLeader(const TArray<FLRTrafficVehicleSnapshot>& Traffic, double PlayerS, float PlayerD, float LaneHalfWidth,
	float& OutGapMeters, float& OutSpeedKmh)
{
	const FLRTrafficVehicleSnapshot* Leader = nullptr;
	for (const FLRTrafficVehicleSnapshot& Vehicle : Traffic)
	{
		if (Vehicle.Direction <= 0 || Vehicle.S <= PlayerS || FMath::Abs(Vehicle.D - PlayerD) > LaneHalfWidth)
		{
			continue;
		}
		if (Leader == nullptr || Vehicle.S < Leader->S)
		{
			Leader = &Vehicle;
		}
	}
	if (Leader == nullptr)
	{
		return false;
	}

	// Center to center minus half of each car, taking the player car as long as the leader.
	OutGapMeters = FMath::Max(static_cast<float>(Leader->S - PlayerS) - Leader->LengthMeters, 0.f);
	OutSpeedKmh = Leader->SpeedMs * LRTitleCruisePrivate::MsToKmh;
	return true;
}

float FLRTitleCruise::ComputeTargetSpeedKmh(float CruiseKmh, float FollowGapMeters, float LeaderGapMeters, float LeaderSpeedKmh)
{
	const float Cruise = FMath::Max(CruiseKmh, 0.f);
	if (LeaderGapMeters < 0.f || LeaderGapMeters >= 2.f * FollowGapMeters)
	{
		return Cruise;
	}
	const float Allowed = LeaderSpeedKmh + (LeaderGapMeters - FollowGapMeters) * LRTitleCruisePrivate::SpeedPerGapMeter;
	return FMath::Clamp(Allowed, 0.f, Cruise);
}

FLRTitleCruisePedals FLRTitleCruise::ComputePedals(float SpeedKmh, float TargetKmh)
{
	using namespace LRTitleCruisePrivate;

	FLRTitleCruisePedals Pedals;
	const float Error = TargetKmh - SpeedKmh;
	if (Error >= -BrakeDeadbandKmh)
	{
		Pedals.Throttle = TargetKmh <= 0.f ? 0.f : FMath::Clamp(HoldThrottle + Error * ThrottlePerKmh, 0.f, 1.f);
	}
	else
	{
		Pedals.Brake = FMath::Clamp((-Error - BrakeDeadbandKmh) * BrakePerKmh, 0.f, 1.f);
	}
	return Pedals;
}
