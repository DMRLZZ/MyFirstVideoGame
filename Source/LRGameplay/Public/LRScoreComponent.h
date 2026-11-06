// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "LREvents.h"
#include "LRScoring.h"

#include "LRScoreComponent.generated.h"

class ULRGameEventSubsystem;
class ULRRunContextSubsystem;

/**
 * Score of the current run. Owned by ALRGameState; the rules live in the pure FLRScoreCalculator.
 *
 * - While the run state is Running, every tick reads the player ALRVehicle (speed, road coordinate) to accumulate
 *   distance points, distance, duration and top speed. Only new road counts: driving backwards and forward again earns nothing.
 * - Listens to OnNearMiss, OnCrash (any contact breaks the combo) and OnRunReset on ULRGameEventSubsystem.
 * - Broadcasts OnScoreChanged (Reason "Distance" or "NearMiss"), OnComboChanged and OnSpeedTierChanged.
 * - Free ride scores nothing: only distance and top speed are tracked.
 *
 * Config: Data/Gameplay/Scoring.json, re-read on lr.Data.Reload. Budget: < 0.05 ms per tick, no allocations.
 */
UCLASS(ClassGroup = (LeftRight), meta = (BlueprintSpawnableComponent))
class LRGAMEPLAY_API ULRScoreComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULRScoreComponent();

	/** Statistics of the current run as a result. CreditsEarned stays 0: the caller fills it from ULRProgressionSubsystem. */
	FLRRunResult BuildRunResult() const;

	/** Points of the current run. */
	int64 GetScore() const { return Calculator.GetState().Score; }

	/** Scoring logic and statistics (read only). */
	const FLRScoreCalculator& GetCalculator() const { return Calculator; }

	/** The car was moved without driving (free ride reset): the next tick restarts the distance count from there. */
	void NotifyVehicleTeleported();

	/** Re-reads Data/Gameplay/Scoring.json. Keeps the current rules on failure. The run statistics are kept. */
	void ReloadRules();

	//~ UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	//~ UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Bus callback: scores a near miss. */
	void HandleNearMiss(const FLRNearMissEvent& Event);

	/** Bus callback: any contact while running breaks the combo. */
	void HandleCrash(const FLRCrashEvent& Event);

	/** Bus callback: a new run starts. */
	void HandleRunReset(const FLRRunResetEvent& Event);

	/** Broadcasts the current score with the points just earned. */
	void BroadcastScore(int64 Delta, FName Reason) const;

	/** Broadcasts the current combo. */
	void BroadcastCombo(bool bBroken) const;

	/** Current player speed, km/h. 0 without a player car. */
	float GetPlayerSpeedKmh() const;

	FLRScoreCalculator Calculator;

	/** Farthest road position already counted, meters. */
	double FarthestS = 0.0;

	/** False until the first running tick stores FarthestS. */
	bool bHasDistanceOrigin = false;

	/** Tier of the last OnSpeedTierChanged, INDEX_NONE after a reset. */
	int32 LastSpeedTier = INDEX_NONE;

	TWeakObjectPtr<ULRGameEventSubsystem> EventBus;
	TWeakObjectPtr<ULRRunContextSubsystem> RunContext;

	FDelegateHandle NearMissHandle;
	FDelegateHandle CrashHandle;
	FDelegateHandle RunResetHandle;
	FDelegateHandle DataReloadedHandle;
};
