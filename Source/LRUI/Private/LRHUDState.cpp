// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRHUDState.h"

#include "LREvents.h"

// --- FLRHUDPopupQueue --------------------------------------------------------------------------

void FLRHUDPopupQueue::Configure(int32 InCapacity, float InLifetimeSeconds, float InAttachWindowSeconds)
{
	const int32 Capacity = FMath::Clamp(InCapacity, 1, 32);
	LifetimeSeconds = FMath::Max(InLifetimeSeconds, 0.05f);
	AttachWindowSeconds = FMath::Max(InAttachWindowSeconds, 0.f);
	Slots.Reset();
	Slots.SetNum(Capacity);
	Reset();
}

int32 FLRHUDPopupQueue::Push(ELRNearMissTier Tier, bool bOncoming, bool bThreadTheNeedle)
{
	if (Slots.Num() == 0)
	{
		Slots.SetNum(1);
	}

	// Free slot first, otherwise recycle the oldest (lowest sequence).
	int32 SlotIndex = INDEX_NONE;
	uint32 OldestSequence = MAX_uint32;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FLRHUDNearMissPopup& Slot = Slots[Index];
		if (!Slot.bActive)
		{
			SlotIndex = Index;
			break;
		}
		if (Slot.Sequence < OldestSequence)
		{
			OldestSequence = Slot.Sequence;
			SlotIndex = Index;
		}
	}

	FLRHUDNearMissPopup& Popup = Slots[SlotIndex];
	Popup = FLRHUDNearMissPopup();
	Popup.Tier = Tier;
	Popup.bOncoming = bOncoming;
	Popup.bThreadTheNeedle = bThreadTheNeedle;
	Popup.Sequence = NextSequence++;
	Popup.bActive = true;

	if (PendingPoints != INDEX_NONE)
	{
		Popup.Points = PendingPoints;
		PendingPoints = INDEX_NONE;
		PendingPointsAge = 0.f;
	}
	return SlotIndex;
}

int32 FLRHUDPopupQueue::Push(const FLRNearMissEvent& Event)
{
	return Push(Event.Tier, Event.bOncoming, Event.bThreadTheNeedle);
}

void FLRHUDPopupQueue::ReportPoints(int64 Points)
{
	// Newest active popup without points, inside the attach window.
	FLRHUDNearMissPopup* Best = nullptr;
	for (FLRHUDNearMissPopup& Slot : Slots)
	{
		if (Slot.bActive && !Slot.HasPoints() && Slot.AgeSeconds <= AttachWindowSeconds
			&& (!Best || Slot.Sequence > Best->Sequence))
		{
			Best = &Slot;
		}
	}

	if (Best)
	{
		Best->Points = Points;
		return;
	}

	PendingPoints = Points;
	PendingPointsAge = 0.f;
}

void FLRHUDPopupQueue::Tick(float DeltaSeconds)
{
	const float Delta = FMath::Max(DeltaSeconds, 0.f);
	for (FLRHUDNearMissPopup& Slot : Slots)
	{
		if (!Slot.bActive)
		{
			continue;
		}
		Slot.AgeSeconds += Delta;
		if (Slot.AgeSeconds >= LifetimeSeconds)
		{
			Slot.bActive = false;
		}
	}

	if (PendingPoints != INDEX_NONE)
	{
		PendingPointsAge += Delta;
		if (PendingPointsAge > AttachWindowSeconds)
		{
			PendingPoints = INDEX_NONE;
			PendingPointsAge = 0.f;
		}
	}
}

void FLRHUDPopupQueue::Reset()
{
	for (FLRHUDNearMissPopup& Slot : Slots)
	{
		Slot = FLRHUDNearMissPopup();
	}
	PendingPoints = INDEX_NONE;
	PendingPointsAge = 0.f;
}

int32 FLRHUDPopupQueue::NumActive() const
{
	int32 Count = 0;
	for (const FLRHUDNearMissPopup& Slot : Slots)
	{
		Count += Slot.bActive ? 1 : 0;
	}
	return Count;
}

int32 FLRHUDPopupQueue::GetStackRank(int32 SlotIndex) const
{
	if (!Slots.IsValidIndex(SlotIndex) || !Slots[SlotIndex].bActive)
	{
		return INDEX_NONE;
	}
	const uint32 Sequence = Slots[SlotIndex].Sequence;
	int32 Rank = 0;
	for (const FLRHUDNearMissPopup& Slot : Slots)
	{
		if (Slot.bActive && Slot.Sequence > Sequence)
		{
			++Rank;
		}
	}
	return Rank;
}

// --- FLRHUDComboTracker ------------------------------------------------------------------------

void FLRHUDComboTracker::Apply(const FLRComboChangedEvent& Event)
{
	const float NewRemaining = FMath::Max(Event.TimeRemaining, 0.f);
	const bool bRestartWindow = Event.Chain > Chain || NewRemaining > TimeRemaining + KINDA_SMALL_NUMBER;

	if (Event.bBroken)
	{
		BrokenTimer = FMath::Max(BrokenDisplaySeconds, 0.f);
	}

	Chain = FMath::Max(Event.Chain, 0);
	Multiplier = FMath::Max(Event.Multiplier, 1.f);
	TimeRemaining = Chain > 0 ? NewRemaining : 0.f;

	if (Chain == 0)
	{
		WindowSeconds = 0.f;
	}
	else if (bRestartWindow || WindowSeconds < TimeRemaining)
	{
		WindowSeconds = TimeRemaining;
	}
}

void FLRHUDComboTracker::Tick(float DeltaSeconds)
{
	const float Delta = FMath::Max(DeltaSeconds, 0.f);
	TimeRemaining = FMath::Max(TimeRemaining - Delta, 0.f);
	BrokenTimer = FMath::Max(BrokenTimer - Delta, 0.f);
}

void FLRHUDComboTracker::Reset()
{
	Chain = 0;
	Multiplier = 1.f;
	TimeRemaining = 0.f;
	WindowSeconds = 0.f;
	BrokenTimer = 0.f;
}

float FLRHUDComboTracker::GetRemainingFraction() const
{
	return WindowSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp(TimeRemaining / WindowSeconds, 0.f, 1.f) : 0.f;
}
