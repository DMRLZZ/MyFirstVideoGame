// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"

struct FLRComboChangedEvent;
struct FLRNearMissEvent;

/** One animated near-miss popup on screen. Plain data, owned by FLRHUDPopupQueue. */
struct LRUI_API FLRHUDNearMissPopup
{
	/** Quality of the pass (decides label and color). */
	ELRNearMissTier Tier = ELRNearMissTier::None;

	/** Pass against oncoming traffic ("CONTRAMANO" tag). */
	bool bOncoming = false;

	/** Passed between two vehicles ("¡ENTRE DOS!" tag). */
	bool bThreadTheNeedle = false;

	/** Points awarded, or INDEX_NONE while unknown (the score event may arrive slightly later). */
	int64 Points = INDEX_NONE;

	/** Seconds since the popup appeared. */
	float AgeSeconds = 0.f;

	/** Monotonic spawn order (higher = newer); used to stack popups. */
	uint32 Sequence = 0;

	/** Slot in use. */
	bool bActive = false;

	/** True when Points holds a real value. */
	bool HasPoints() const { return Points != INDEX_NONE; }
};

/**
 * Fixed-capacity queue of near-miss popups (pure logic, no allocation after Configure).
 *
 * The HUD pushes one popup per FLRNearMissEvent and reports the points of FLRScoreChangedEvent (Reason "NearMiss").
 * Points are attached to the newest popup still without points if it appeared less than AttachWindowSeconds ago;
 * if the score event arrives first, the points wait the same window for the next popup.
 * When full, the oldest popup is recycled.
 */
struct LRUI_API FLRHUDPopupQueue
{
	/** Sets capacity (>= 1, slots preallocated), lifetime and the points attach window, and clears the queue. */
	void Configure(int32 InCapacity, float InLifetimeSeconds, float InAttachWindowSeconds);

	/** Adds a popup (recycling the oldest when full). Returns the slot index used. */
	int32 Push(ELRNearMissTier Tier, bool bOncoming, bool bThreadTheNeedle);

	/** Convenience overload taking the bus event. */
	int32 Push(const FLRNearMissEvent& Event);

	/** Reports the points of a near miss (see class comment). */
	void ReportPoints(int64 Points);

	/** Ages popups, expires the ones older than the lifetime and the pending points. */
	void Tick(float DeltaSeconds);

	/** Removes every popup and pending points. */
	void Reset();

	/** Number of active popups. */
	int32 NumActive() const;

	/** All slots (check bActive); fixed size equal to the capacity. */
	const TArray<FLRHUDNearMissPopup>& GetSlots() const { return Slots; }

	/** Configured lifetime of a popup, seconds. */
	float GetLifetimeSeconds() const { return LifetimeSeconds; }

	/**
	 * Rank of a popup among the active ones, 0 = newest. Used to stack them vertically.
	 * Returns INDEX_NONE for an inactive or invalid slot.
	 */
	int32 GetStackRank(int32 SlotIndex) const;

private:
	TArray<FLRHUDNearMissPopup> Slots;
	float LifetimeSeconds = 1.6f;
	float AttachWindowSeconds = 0.25f;
	uint32 NextSequence = 1;

	/** Points received before their popup, INDEX_NONE when none. */
	int64 PendingPoints = INDEX_NONE;
	float PendingPointsAge = 0.f;
};

/**
 * Combo display state (pure logic). Mirrors FLRComboChangedEvent and counts the remaining time down locally between
 * events so the bar animates smoothly even if gameplay only broadcasts on changes.
 */
struct LRUI_API FLRHUDComboTracker
{
	/** Seconds the "combo broken" feedback stays visible. */
	float BrokenDisplaySeconds = 1.0f;

	/** Applies a bus event. A longer remaining time or a longer chain restarts the full window used by the bar. */
	void Apply(const FLRComboChangedEvent& Event);

	/** Counts the remaining time and the broken feedback down. */
	void Tick(float DeltaSeconds);

	/** Clears everything (new run). */
	void Reset();

	/** Chain is running (chain > 0 and time left). */
	bool IsActive() const { return Chain > 0 && TimeRemaining > 0.f; }

	/** "Combo broken" feedback is visible. */
	bool IsShowingBroken() const { return BrokenTimer > 0.f; }

	/** Remaining fraction of the window, 0..1 (for the timer bar). */
	float GetRemainingFraction() const;

	/** Chained near misses. */
	int32 GetChain() const { return Chain; }

	/** Current combo multiplier (>= 1). */
	float GetMultiplier() const { return Multiplier; }

	/** Seconds left before the chain expires. */
	float GetTimeRemaining() const { return TimeRemaining; }

	/** Seconds left of the broken feedback. */
	float GetBrokenTimer() const { return BrokenTimer; }

private:
	int32 Chain = 0;
	float Multiplier = 1.f;
	float TimeRemaining = 0.f;
	float WindowSeconds = 0.f;
	float BrokenTimer = 0.f;
};
