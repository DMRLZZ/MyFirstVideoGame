// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LRCoreTypes.h"

/**
 * Pure validator of the run lifecycle (no UObject, no world). Owned by ALRGameModeEndless, which mirrors every accepted
 * state into ULRRunContextSubsystem.
 *
 * Allowed transitions:
 *   Garage    -> Loading
 *   Loading   -> Countdown | Garage
 *   Countdown -> Running | Loading (restart during the countdown) | Garage
 *   Running   -> Crashed | Countdown (restart) | Garage
 *   Crashed   -> Replay | Results | Countdown (restart) | Garage
 *   Replay    -> Results | Countdown (restart) | Garage
 *   Results   -> Countdown (restart) | Garage
 * A state never transitions to itself.
 */
struct LRGAMEPLAY_API FLRRunStateMachine
{
	/** True if the lifecycle allows going from From to To. */
	static bool CanTransition(ELRRunState From, ELRRunState To);

	/** True if a player restart is accepted in RunState: any state after the countdown. */
	static bool CanRestartFrom(ELRRunState RunState);

	/** Moves to NewState if the transition is valid. Returns false and keeps the state otherwise. */
	bool TryTransition(ELRRunState NewState);

	/** Current state. */
	ELRRunState GetState() const { return State; }

private:
	ELRRunState State = ELRRunState::Garage;
};
