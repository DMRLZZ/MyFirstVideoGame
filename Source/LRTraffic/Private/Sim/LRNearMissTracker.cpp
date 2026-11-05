// Copyright LEFTRIGHT. All Rights Reserved.

#include "Sim/LRNearMissTracker.h"

#include "LRUnits.h"

FLRNearMissTracker::FLRNearMissTracker()
{
	Active.Reserve(32);
	Recent.Reserve(16);
}

void FLRNearMissTracker::Configure(const FLRNearMissParams& InParams)
{
	Params = InParams;
}

void FLRNearMissTracker::Reset()
{
	Active.Reset();
	Recent.Reset();
}

ELRNearMissTier FLRNearMissTracker::ClassifyClearance(float ClearanceM) const
{
	if (ClearanceM < Params.RazorThresholdM)
	{
		return ELRNearMissTier::Razor;
	}
	if (ClearanceM < Params.VeryCloseThresholdM)
	{
		return ELRNearMissTier::VeryClose;
	}
	if (ClearanceM < Params.CloseThresholdM)
	{
		return ELRNearMissTier::Close;
	}
	return ELRNearMissTier::None;
}

int32 FLRNearMissTracker::FindActive(int32 Id) const
{
	for (int32 Index = 0; Index < Active.Num(); ++Index)
	{
		if (Active[Index].Id == Id)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void FLRNearMissTracker::NotifyContact(int32 VehicleId)
{
	const int32 Index = FindActive(VehicleId);
	if (Index != INDEX_NONE)
	{
		Active[Index].bContact = true;
		return;
	}

	// Contact reported before the overlap was sampled: open a pass already marked as contact.
	FPass& Pass = Active.AddDefaulted_GetRef();
	Pass.Id = VehicleId;
	Pass.bContact = true;
	Pass.bSeen = true;
}

void FLRNearMissTracker::Update(double TimeSeconds, const FLRNearMissPlayerState& Player, TConstArrayView<FLRNearMissVehicleState> Vehicles, TArray<FLRNearMissEvent>& OutEvents)
{
	// Forget old completed passes.
	for (int32 Index = Recent.Num() - 1; Index >= 0; --Index)
	{
		if (TimeSeconds - Recent[Index].TimeSeconds > Params.ThreadWindowSeconds)
		{
			Recent.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}

	for (FPass& Pass : Active)
	{
		Pass.bSeen = false;
	}

	for (const FLRNearMissVehicleState& Vehicle : Vehicles)
	{
		const double HalfLengths = 0.5 * (static_cast<double>(Player.LengthM) + static_cast<double>(Vehicle.LengthM));
		const bool bOverlap = FMath::Abs(Player.S - Vehicle.S) < HalfLengths;
		const int32 PassIndex = FindActive(Vehicle.Id);

		if (bOverlap)
		{
			const float Lateral = Vehicle.D - Player.D;
			const float Clearance = FMath::Abs(Lateral) - 0.5f * (Player.WidthM + Vehicle.WidthM);
			const float RelativeSpeed = FMath::Abs(Player.SpeedMs - Vehicle.SpeedMs);

			FPass* Pass = PassIndex != INDEX_NONE ? &Active[PassIndex] : nullptr;
			if (!Pass)
			{
				Pass = &Active.AddDefaulted_GetRef();
				Pass->Id = Vehicle.Id;
			}
			Pass->bSeen = true;
			Pass->bOverlapped = true;
			Pass->Archetype = Vehicle.Archetype;
			Pass->bOncoming = Vehicle.bOncoming;
			Pass->MaxRelativeSpeedMs = FMath::Max(Pass->MaxRelativeSpeedMs, RelativeSpeed);
			if (Clearance < Pass->MinClearanceM)
			{
				Pass->MinClearanceM = Clearance;
				Pass->Side = Lateral < 0.f ? -1 : 1;
			}
			if (Clearance <= 0.f)
			{
				Pass->bContact = true;
			}
		}
		else if (PassIndex != INDEX_NONE && !Active[PassIndex].bOverlapped)
		{
			// Contact reported before the overlap started: keep the pending pass while the vehicle stays nearby.
			Active[PassIndex].bSeen = true;
		}
		else if (PassIndex != INDEX_NONE)
		{
			// Overlap ended this sample: evaluate and forget.
			const FPass Finished = Active[PassIndex];
			Active.RemoveAtSwap(PassIndex, 1, EAllowShrinking::No);
			FinishPass(Finished, TimeSeconds, Player, OutEvents);
		}
	}

	// Vehicles that vanished while overlapping (despawned / removed) are dropped without event.
	for (int32 Index = Active.Num() - 1; Index >= 0; --Index)
	{
		if (!Active[Index].bSeen)
		{
			Active.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}
}

void FLRNearMissTracker::FinishPass(const FPass& Pass, double TimeSeconds, const FLRNearMissPlayerState& Player, TArray<FLRNearMissEvent>& OutEvents)
{
	if (Pass.bContact)
	{
		return;
	}

	const ELRNearMissTier Tier = ClassifyClearance(Pass.MinClearanceM);
	const float RelativeSpeedKmh = static_cast<float>(LRUnits::MsToKmh(Pass.MaxRelativeSpeedMs));
	if (Tier == ELRNearMissTier::None || RelativeSpeedKmh <= Params.MinRelativeSpeedKmh)
	{
		return;
	}

	bool bThread = false;
	for (const FCompleted& Completed : Recent)
	{
		if (Completed.Id != Pass.Id && Completed.Side == -Pass.Side && TimeSeconds - Completed.TimeSeconds <= Params.ThreadWindowSeconds)
		{
			bThread = true;
			break;
		}
	}

	FCompleted& Memory = Recent.AddDefaulted_GetRef();
	Memory.TimeSeconds = TimeSeconds;
	Memory.Id = Pass.Id;
	Memory.Side = Pass.Side;

	FLRNearMissEvent& Event = OutEvents.AddDefaulted_GetRef();
	Event.ClearanceMeters = FMath::Max(0.f, Pass.MinClearanceM);
	Event.RelativeSpeedKmh = RelativeSpeedKmh;
	Event.Tier = Tier;
	Event.bOncoming = Pass.bOncoming;
	Event.bThreadTheNeedle = bThread;
	Event.Archetype = Pass.Archetype;
	Event.TrafficVehicleId = Pass.Id;
	Event.PlayerCoord = FLRRoadCoord(Player.S, Player.D);
}
