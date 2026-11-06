// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRRunStateMachine.h"

bool FLRRunStateMachine::CanTransition(ELRRunState From, ELRRunState To)
{
	switch (From)
	{
	case ELRRunState::Garage:
		return To == ELRRunState::Loading;
	case ELRRunState::Loading:
		return To == ELRRunState::Countdown || To == ELRRunState::Garage;
	case ELRRunState::Countdown:
		return To == ELRRunState::Running || To == ELRRunState::Loading || To == ELRRunState::Garage;
	case ELRRunState::Running:
		return To == ELRRunState::Crashed || To == ELRRunState::Countdown || To == ELRRunState::Garage;
	case ELRRunState::Crashed:
		return To == ELRRunState::Replay || To == ELRRunState::Results || To == ELRRunState::Countdown || To == ELRRunState::Garage;
	case ELRRunState::Replay:
		return To == ELRRunState::Results || To == ELRRunState::Countdown || To == ELRRunState::Garage;
	case ELRRunState::Results:
		return To == ELRRunState::Countdown || To == ELRRunState::Garage;
	default:
		return false;
	}
}

bool FLRRunStateMachine::CanRestartFrom(ELRRunState RunState)
{
	return RunState == ELRRunState::Running || RunState == ELRRunState::Crashed || RunState == ELRRunState::Replay || RunState == ELRRunState::Results;
}

bool FLRRunStateMachine::TryTransition(ELRRunState NewState)
{
	if (!CanTransition(State, NewState))
	{
		return false;
	}
	State = NewState;
	return true;
}
