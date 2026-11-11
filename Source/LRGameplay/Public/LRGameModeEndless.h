// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LRCoreTypes.h"
#include "LRGameRules.h"
#include "LRRunStateMachine.h"
#include "LRTrafficTypes.h"

#include "LRGameModeEndless.generated.h"

class ALRVehicle;
class APlayerController;
class ULRScoreComponent;
struct FLRCrashEvent;

/**
 * Game mode of LEFTRIGHT: owns the run lifecycle (FLRRunStateMachine) in every mode (endless, two-way, free ride, daily).
 *
 * One tick after play starts it spawns the player ALRVehicle with the vehicle selected in ULRProgressionSubsystem, possesses
 * it and enters the title screen: the Garage state with a live world behind the menu, where the car cruises on its own
 * (FLRTitleCruise). PlayFromTitle starts the first run. A run resets road, traffic and every other listener through
 * ULRRunContextSubsystem::BeginNewRun without reloading the map, places the car and goes through Countdown to Running.
 *
 * A contact while Running is judged by FLRGameRulesLogic::EvaluateCrash. When the run ends the result is recorded in
 * ULRProgressionSubsystem, OnRunFinished is broadcast and the Results state follows after ResultsDelaySeconds.
 *
 * Config: Data/Gameplay/GameRules.json, re-read on lr.Data.Reload.
 * Console: lr.Restart, lr.Mode <EndlessOneWay|EndlessTwoWay|FreeRide|Daily>, lr.Vehicle <id>, lr.ControlScheme <Arcade|Simcade>.
 */
UCLASS()
class LRGAMEPLAY_API ALRGameModeEndless : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALRGameModeEndless(const FObjectInitializer& ObjectInitializer);

	/**
	 * Starts a run with Seed in the current mode from any state, also during a countdown: unpauses, resets the world,
	 * places the car at the road spawn at StartSpeedKmh with input disabled and starts the countdown.
	 * Does nothing until the player car exists.
	 */
	void StartRun(int32 Seed);

	/**
	 * Player restart with a new seed (time-based, or the daily seed in Daily mode). Accepted in any state after the
	 * countdown and ignored otherwise. A run abandoned while Running is not recorded.
	 */
	void RestartRun();

	/** Switches to Mode and starts a run with a new seed, from any state. */
	void RestartInMode(ELRGameModeType Mode);

	/**
	 * Enters the title screen from any state: unpauses, moves to Garage, resets the world with a new seed and places the
	 * car on the road cruising at FLRGameRules::TitleCruiseKmh. A run abandoned while Running is not recorded.
	 * Does nothing until the player car exists.
	 */
	void EnterTitle();

	/** Title screen "Play": starts a run in the current mode. Ignored outside the title screen. */
	void PlayFromTitle();

	/** Modes offered by the title screen, in menu order. */
	static const TArray<ELRGameModeType>& GetTitleModes();

	/**
	 * Selects the next (+1) or previous (-1) mode of GetTitleModes. On the title screen the scene is rebuilt in that mode;
	 * otherwise a run starts in it.
	 */
	void CycleMode(int32 Direction);

	/**
	 * Loads Data/Vehicles/<VehicleId>.json on the player car. On the title screen the car goes back to the title scene;
	 * otherwise a run starts with a new seed. The vehicle becomes the saved selection only if the profile owns it.
	 * @return false if there is no player car or the definition cannot be loaded (nothing changes).
	 */
	bool SetPlayerVehicle(FName VehicleId);

	/** Switches to the next (+1) or previous (-1) car in Data/Vehicles, see SetPlayerVehicle. */
	void CycleVehicle(int32 Direction = 1);

	/** Re-reads Data/Gameplay/GameRules.json. Keeps the current rules on failure. */
	void ReloadRules();

	/** Mode of the current run. */
	ELRGameModeType GetModeType() const { return ModeType; }

	/** Sanitized rules in use. */
	const FLRGameRules& GetGameRules() const { return Rules; }

	/** Current run state. */
	ELRRunState GetRunState() const { return StateMachine.GetState(); }

	/** The player car, or nullptr before the first run. */
	ALRVehicle* GetPlayerVehicle() const { return PlayerVehicle; }

	/** Scrapes consumed in this run (forgiving arcade). */
	int32 GetScrapesUsed() const { return ScrapesUsed; }

	//~ AGameModeBase
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	//~ AActor
	virtual void Tick(float DeltaSeconds) override;

protected:
	//~ AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Starts the first run once play has started and a player is waiting. */
	void TryStartFirstRun();

	/** Timer callback one tick after StartPlay (the physics scene begins play after StartPlay returns). */
	void HandlePlayReady();

	/** Spawns the car with the selected definition and possesses it. Returns false if it cannot be spawned. */
	bool SpawnPlayerVehicle(APlayerController& Player);

	/** Applies a state to the machine and to ULRRunContextSubsystem. Logs and returns false if the transition is invalid. */
	bool TransitionTo(ELRRunState NewState);

	/** Timer callback: the countdown ended. */
	void HandleCountdownFinished();

	/** Timer callback: shows the results. */
	void HandleResultsDelayFinished();

	/** Timer callback: puts the car back on the road (free ride) and restores the engine sound after a surviving impact. */
	void HandleCrashRecovery();

	/** Bus callback: the player hit something. */
	void HandleCrash(const FLRCrashEvent& Event);

	/** Ends the run: wrecks the car, stops scoring, records the result and schedules the results. */
	void EndRun();

	/** Arms the crash recovery timer unless it is already running. */
	void ScheduleCrashRecovery();

	/** Stops every run timer. */
	void ClearRunTimers();

	/** Score component of the game state, or nullptr. */
	ULRScoreComponent* GetScoreComponent() const;

	/** Title screen: drives the player car with FLRTitleCruise behind the traffic ahead. */
	void UpdateTitleCruise();

	/** Traffic around the player car, reused by UpdateTitleCruise every frame. */
	TArray<FLRTrafficVehicleSnapshot> TitleTrafficScratch;

	/** The player car. Stays alive between runs. */
	UPROPERTY(Transient)
	TObjectPtr<ALRVehicle> PlayerVehicle;

	/** Player waiting for the first run. */
	TWeakObjectPtr<APlayerController> PendingPlayer;

	FLRGameRules Rules;
	FLRRunStateMachine StateMachine;
	ELRGameModeType ModeType = ELRGameModeType::EndlessOneWay;
	int32 ScrapesUsed = 0;

	/** Play has started and physics is ready. */
	bool bPlayReady = false;

	FTimerHandle CountdownTimer;
	FTimerHandle ResultsTimer;
	FTimerHandle CrashRecoveryTimer;

	FDelegateHandle CrashHandle;
	FDelegateHandle DataReloadedHandle;
};
