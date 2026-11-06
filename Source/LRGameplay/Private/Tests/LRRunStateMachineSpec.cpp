// Copyright LEFTRIGHT. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "LRRunStateMachine.h"
#include "UObject/Class.h"

BEGIN_DEFINE_SPEC(FLRRunStateMachineSpec, "LeftRight.Gameplay.RunStateMachine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FLRRunStateMachineSpec)

void FLRRunStateMachineSpec::Define()
{
	It("starts in the garage", [this]()
	{
		const FLRRunStateMachine Machine;
		TestTrue(TEXT("Garage"), Machine.GetState() == ELRRunState::Garage);
	});

	It("follows the whole lifecycle through the replay", [this]()
	{
		FLRRunStateMachine Machine;
		TestTrue(TEXT("Loading"), Machine.TryTransition(ELRRunState::Loading));
		TestTrue(TEXT("Countdown"), Machine.TryTransition(ELRRunState::Countdown));
		TestTrue(TEXT("Running"), Machine.TryTransition(ELRRunState::Running));
		TestTrue(TEXT("Crashed"), Machine.TryTransition(ELRRunState::Crashed));
		TestTrue(TEXT("Replay"), Machine.TryTransition(ELRRunState::Replay));
		TestTrue(TEXT("Results"), Machine.TryTransition(ELRRunState::Results));
		TestTrue(TEXT("restart"), Machine.TryTransition(ELRRunState::Countdown));
		TestTrue(TEXT("state"), Machine.GetState() == ELRRunState::Countdown);
	});

	It("accepts the shortcuts of the lifecycle", [this]()
	{
		TestTrue(TEXT("Crashed -> Results"), FLRRunStateMachine::CanTransition(ELRRunState::Crashed, ELRRunState::Results));
		TestTrue(TEXT("Results -> Garage"), FLRRunStateMachine::CanTransition(ELRRunState::Results, ELRRunState::Garage));
		TestTrue(TEXT("Running -> Countdown"), FLRRunStateMachine::CanTransition(ELRRunState::Running, ELRRunState::Countdown));
		TestTrue(TEXT("Crashed -> Countdown"), FLRRunStateMachine::CanTransition(ELRRunState::Crashed, ELRRunState::Countdown));
		TestTrue(TEXT("Replay -> Countdown"), FLRRunStateMachine::CanTransition(ELRRunState::Replay, ELRRunState::Countdown));
		TestTrue(TEXT("Countdown -> Loading"), FLRRunStateMachine::CanTransition(ELRRunState::Countdown, ELRRunState::Loading));
	});

	It("rejects invalid transitions and keeps the state", [this]()
	{
		FLRRunStateMachine Machine;
		TestFalse(TEXT("Garage -> Running"), Machine.TryTransition(ELRRunState::Running));
		TestFalse(TEXT("Garage -> Countdown"), Machine.TryTransition(ELRRunState::Countdown));
		TestTrue(TEXT("state kept"), Machine.GetState() == ELRRunState::Garage);

		TestFalse(TEXT("Loading -> Running"), FLRRunStateMachine::CanTransition(ELRRunState::Loading, ELRRunState::Running));
		TestFalse(TEXT("Countdown -> Crashed"), FLRRunStateMachine::CanTransition(ELRRunState::Countdown, ELRRunState::Crashed));
		TestFalse(TEXT("Running -> Replay"), FLRRunStateMachine::CanTransition(ELRRunState::Running, ELRRunState::Replay));
		TestFalse(TEXT("Running -> Results"), FLRRunStateMachine::CanTransition(ELRRunState::Running, ELRRunState::Results));
		TestFalse(TEXT("Crashed -> Running"), FLRRunStateMachine::CanTransition(ELRRunState::Crashed, ELRRunState::Running));
		TestFalse(TEXT("Results -> Running"), FLRRunStateMachine::CanTransition(ELRRunState::Results, ELRRunState::Running));
		TestFalse(TEXT("Results -> Crashed"), FLRRunStateMachine::CanTransition(ELRRunState::Results, ELRRunState::Crashed));
	});

	It("never transitions to the same state", [this]()
	{
		const ELRRunState States[] = { ELRRunState::Garage, ELRRunState::Loading, ELRRunState::Countdown, ELRRunState::Running,
			ELRRunState::Crashed, ELRRunState::Replay, ELRRunState::Results };
		for (const ELRRunState RunState : States)
		{
			TestFalse(*UEnum::GetValueAsString(RunState), FLRRunStateMachine::CanTransition(RunState, RunState));
		}
	});

	It("allows a player restart only after the countdown", [this]()
	{
		TestFalse(TEXT("Garage"), FLRRunStateMachine::CanRestartFrom(ELRRunState::Garage));
		TestFalse(TEXT("Loading"), FLRRunStateMachine::CanRestartFrom(ELRRunState::Loading));
		TestFalse(TEXT("Countdown"), FLRRunStateMachine::CanRestartFrom(ELRRunState::Countdown));
		TestTrue(TEXT("Running"), FLRRunStateMachine::CanRestartFrom(ELRRunState::Running));
		TestTrue(TEXT("Crashed"), FLRRunStateMachine::CanRestartFrom(ELRRunState::Crashed));
		TestTrue(TEXT("Replay"), FLRRunStateMachine::CanRestartFrom(ELRRunState::Replay));
		TestTrue(TEXT("Results"), FLRRunStateMachine::CanRestartFrom(ELRRunState::Results));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
