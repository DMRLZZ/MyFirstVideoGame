// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LRCoreTypes.h"

#include "LRRunContextSubsystem.generated.h"

class APawn;

/**
 * Shared, read-mostly state of the current run.
 *
 * LRGameplay is the only WRITER (seed, state, mode, player pawn). Every other module READS from here,
 * which lets traffic, cameras or audio find the player and the seed without depending on LRGameplay.
 * State changes are re-broadcast on ULRGameEventSubsystem.
 */
UCLASS()
class LRCORE_API ULRRunContextSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static ULRRunContextSubsystem* Get(const UObject* WorldContext);

	/** Seed of the current run (see LRRandom). */
	int32 GetRunSeed() const { return RunSeed; }

	ELRRunState GetRunState() const { return RunState; }
	ELRGameModeType GetGameMode() const { return GameMode; }
	ELRControlScheme GetControlScheme() const { return ControlScheme; }

	/** The pawn driven by the local player, or nullptr. */
	APawn* GetPlayerPawn() const { return PlayerPawn.Get(); }

	/** True while the run is live (scoring, traffic reacting, crash ends the run). */
	bool IsRunning() const { return RunState == ELRRunState::Running; }

	// --- Writers (LRGameplay only) ---------------------------------------------------------

	/** Starts a new run: stores seed and mode, then broadcasts FLRRunResetEvent. */
	void BeginNewRun(int32 Seed, ELRGameModeType Mode);

	/** Changes the run state and broadcasts FLRRunStateChangedEvent (no-op if unchanged). */
	void SetRunState(ELRRunState NewState);

	void SetControlScheme(ELRControlScheme Scheme) { ControlScheme = Scheme; }
	void SetPlayerPawn(APawn* Pawn);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	int32 RunSeed = 0;
	ELRRunState RunState = ELRRunState::Garage;
	ELRGameModeType GameMode = ELRGameModeType::EndlessOneWay;
	ELRControlScheme ControlScheme = ELRControlScheme::Smooth;

	UPROPERTY()
	TWeakObjectPtr<APawn> PlayerPawn;
};
