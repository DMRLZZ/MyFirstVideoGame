// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LREvents.h"

#include "LRGameEventSubsystem.generated.h"

/**
 * Gameplay event bus, one per world.
 *
 * Producers call Broadcast*(); consumers bind to the native multicast delegates, e.g.
 *
 *   if (ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
 *   {
 *       NearMissHandle = Events->OnNearMiss.AddUObject(this, &UMyComponent::HandleNearMiss);
 *   }
 *
 * Always remove the binding in EndPlay / Deinitialize (Events->OnNearMiss.Remove(NearMissHandle)).
 * Native delegates are used on purpose: zero allocation, no reflection cost, C++-only game logic (ADR-002).
 */
UCLASS()
class LRCORE_API ULRGameEventSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnNearMiss, const FLRNearMissEvent&);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnCrash, const FLRCrashEvent&);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLaneChange, const FLRLaneChangeEvent&);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRunStateChanged, const FLRRunStateChangedEvent&);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRunReset, const FLRRunResetEvent&);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnScoreChanged, const FLRScoreChangedEvent&);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnComboChanged, const FLRComboChangedEvent&);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSpeedTierChanged, const FLRSpeedTierChangedEvent&);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnCameraModeChanged, const FLRCameraModeChangedEvent&);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRunFinished, const FLRRunResult&);

	/** Returns the bus of the world that owns WorldContext, or nullptr (e.g. no world / CDO). */
	static ULRGameEventSubsystem* Get(const UObject* WorldContext);

	FOnNearMiss OnNearMiss;
	FOnCrash OnCrash;
	FOnLaneChange OnLaneChange;
	FOnRunStateChanged OnRunStateChanged;
	FOnRunReset OnRunReset;
	FOnScoreChanged OnScoreChanged;
	FOnComboChanged OnComboChanged;
	FOnSpeedTierChanged OnSpeedTierChanged;
	FOnCameraModeChanged OnCameraModeChanged;
	FOnRunFinished OnRunFinished;

	void BroadcastNearMiss(const FLRNearMissEvent& Event) const { OnNearMiss.Broadcast(Event); }
	void BroadcastCrash(const FLRCrashEvent& Event) const { OnCrash.Broadcast(Event); }
	void BroadcastLaneChange(const FLRLaneChangeEvent& Event) const { OnLaneChange.Broadcast(Event); }
	void BroadcastRunStateChanged(const FLRRunStateChangedEvent& Event) const { OnRunStateChanged.Broadcast(Event); }
	void BroadcastRunReset(const FLRRunResetEvent& Event) const { OnRunReset.Broadcast(Event); }
	void BroadcastScoreChanged(const FLRScoreChangedEvent& Event) const { OnScoreChanged.Broadcast(Event); }
	void BroadcastComboChanged(const FLRComboChangedEvent& Event) const { OnComboChanged.Broadcast(Event); }
	void BroadcastSpeedTierChanged(const FLRSpeedTierChangedEvent& Event) const { OnSpeedTierChanged.Broadcast(Event); }
	void BroadcastCameraModeChanged(const FLRCameraModeChangedEvent& Event) const { OnCameraModeChanged.Broadcast(Event); }
	void BroadcastRunFinished(const FLRRunResult& Event) const { OnRunFinished.Broadcast(Event); }

protected:
	//~ UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
};
