// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRGameModeEndless.h"
#include "Misc/Paths.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LRDataLoader.h"
#include "LRDataSubsystem.h"
#include "LREvents.h"
#include "LRGameEventSubsystem.h"
#include "LRGameState.h"
#include "LRGameplay.h"
#include "LRHUD.h"
#include "LRPlayerController.h"
#include "LRProgressionSubsystem.h"
#include "LRRoadPath.h"
#include "LRRoadSubsystem.h"
#include "LRRoadTypes.h"
#include "LRRunContextSubsystem.h"
#include "LRScoreComponent.h"
#include "LRTitleCruise.h"
#include "LRTrafficSubsystem.h"
#include "LRVehicle.h"
#include "LRVehicleAudioComponent.h"
#include "Misc/DateTime.h"
#include "TimerManager.h"

namespace LRGameModeEndlessPrivate
{
	ALRGameModeEndless* FindGameMode(const UWorld* World)
	{
		return World ? Cast<ALRGameModeEndless>(World->GetAuthGameMode()) : nullptr;
	}

	/** Lateral distance from the player car that still counts as its lane for the title cruise, meters. */
	constexpr float TitleLaneHalfWidthMeters = 1.6f;

	/** Extra road ahead of twice the follow gap searched for a leader by the title cruise, meters. */
	constexpr double TitleLookAheadMarginMeters = 20.0;

	FAutoConsoleCommandWithWorldAndArgs RestartCommand(
		TEXT("lr.Restart"),
		TEXT("Restarts the run with a new seed (any state after the countdown)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (ALRGameModeEndless* GameMode = FindGameMode(World))
			{
				GameMode->RestartRun();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs ModeCommand(
		TEXT("lr.Mode"),
		TEXT("Restarts in a mode: lr.Mode <EndlessOneWay|EndlessTwoWay|FreeRide|Daily>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ALRGameModeEndless* GameMode = FindGameMode(World);
			ELRGameModeType Mode = ELRGameModeType::EndlessOneWay;
			if (!Args.IsValidIndex(0) || !FLRGameRulesLogic::ParseGameMode(Args[0], Mode))
			{
				UE_LOG(LogLRGameplay, Warning, TEXT("Usage: lr.Mode <EndlessOneWay|EndlessTwoWay|FreeRide|Daily>"));
				return;
			}
			if (GameMode)
			{
				GameMode->RestartInMode(Mode);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs VehicleCommand(
		TEXT("lr.Vehicle"),
		TEXT("Loads Data/Vehicles/<id>.json on the player car and restarts: lr.Vehicle <id>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!Args.IsValidIndex(0))
			{
				UE_LOG(LogLRGameplay, Warning, TEXT("Usage: lr.Vehicle <id>"));
				return;
			}
			if (ALRGameModeEndless* GameMode = FindGameMode(World))
			{
				GameMode->SetPlayerVehicle(FName(*Args[0]));
			}
		}));

	FAutoConsoleCommandWithWorld PauseMenuCommand(
		TEXT("lr.Menu"),
		TEXT("Opens or closes the pause menu."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (ALRPlayerController* Controller = World ? Cast<ALRPlayerController>(World->GetFirstPlayerController()) : nullptr)
			{
				Controller->TogglePauseMenu();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs ControlSchemeCommand(
		TEXT("lr.ControlScheme"),
		TEXT("Selects the control scheme: lr.ControlScheme <Smooth|Arcade|Simcade>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ELRControlScheme Scheme = ELRControlScheme::Arcade;
			if (!Args.IsValidIndex(0) || !FLRGameRulesLogic::ParseControlScheme(Args[0], Scheme))
			{
				UE_LOG(LogLRGameplay, Warning, TEXT("Usage: lr.ControlScheme <Smooth|Arcade|Simcade>"));
				return;
			}
			if (ALRPlayerController* Controller = World ? Cast<ALRPlayerController>(World->GetFirstPlayerController()) : nullptr)
			{
				Controller->SetControlScheme(Scheme);
			}
		}));
}

ALRGameModeEndless::ALRGameModeEndless(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PlayerControllerClass = ALRPlayerController::StaticClass();
	GameStateClass = ALRGameState::StaticClass();
	HUDClass = ALRHUD::StaticClass();
	DefaultPawnClass = nullptr;

	// Ticks only to drive the car on the title screen.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ALRGameModeEndless::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	ReloadRules();
}

void ALRGameModeEndless::ReloadRules()
{
	FLRGameRules Loaded;
	FString Error;
	if (FLRDataLoader::Load(FLRGameRules::DataPath, Loaded, &Error))
	{
		Rules = Loaded.Sanitized();
	}
	else
	{
		UE_LOG(LogLRGameplay, Warning, TEXT("Keeping the current game rules: %s"), *Error);
	}
}

void ALRGameModeEndless::BeginPlay()
{
	Super::BeginPlay();

	if (ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
	{
		CrashHandle = Events->OnCrash.AddUObject(this, &ALRGameModeEndless::HandleCrash);
	}
	if (ULRDataSubsystem* Data = ULRDataSubsystem::Get(this))
	{
		DataReloadedHandle = Data->OnDataReloaded.AddUObject(this, &ALRGameModeEndless::ReloadRules);
	}
}

void ALRGameModeEndless::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearRunTimers();

	if (ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
	{
		Events->OnCrash.Remove(CrashHandle);
	}
	if (ULRDataSubsystem* Data = ULRDataSubsystem::Get(this))
	{
		Data->OnDataReloaded.Remove(DataReloadedHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void ALRGameModeEndless::StartPlay()
{
	Super::StartPlay();

	GetWorldTimerManager().SetTimerForNextTick(this, &ALRGameModeEndless::HandlePlayReady);
}

void ALRGameModeEndless::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// The default implementation would try to spawn DefaultPawnClass. The car is spawned by TryStartFirstRun.
	PendingPlayer = NewPlayer;
	TryStartFirstRun();
}

void ALRGameModeEndless::HandlePlayReady()
{
	bPlayReady = true;
	TryStartFirstRun();
}

void ALRGameModeEndless::TryStartFirstRun()
{
	APlayerController* Player = PendingPlayer.Get();
	if (!bPlayReady || !Player || PlayerVehicle)
	{
		return;
	}

	if (SpawnPlayerVehicle(*Player))
	{
		EnterTitle();
	}
}

bool ALRGameModeEndless::SpawnPlayerVehicle(APlayerController& Player)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FTransform SpawnTransform = FTransform::Identity;
	if (const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this); Road && Road->HasRoad())
	{
		SpawnTransform = Road->RoadToWorld(Road->GetSpawnS(), Road->GetLaneCenterD(Road->GetDefaultPlayerLane()));
	}

	// Deferred so the selected definition is in place before the components register and BeginPlay runs.
	ALRVehicle* Vehicle = World->SpawnActorDeferred<ALRVehicle>(ALRVehicle::StaticClass(), SpawnTransform, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Vehicle)
	{
		UE_LOG(LogLRGameplay, Error, TEXT("Could not spawn the player vehicle"));
		return false;
	}

	const ULRProgressionSubsystem* Progression = ULRProgressionSubsystem::Get(this);
	const FName SelectedVehicle = Progression ? Progression->GetSelectedVehicle() : NAME_None;
	if (!Vehicle->LoadDefinitionById(SelectedVehicle) && !Vehicle->LoadDefinitionById(Rules.DefaultVehicleId))
	{
		UE_LOG(LogLRGameplay, Error, TEXT("Could not load vehicle '%s' nor the default '%s'"), *SelectedVehicle.ToString(), *Rules.DefaultVehicleId.ToString());
	}

	Vehicle->FinishSpawning(SpawnTransform);
	PlayerVehicle = Vehicle;

	Player.Possess(Vehicle);

	if (ULRRunContextSubsystem* Context = ULRRunContextSubsystem::Get(this))
	{
		Context->SetPlayerPawn(Vehicle);
		Vehicle->SetControlScheme(Context->GetControlScheme());
	}

	ULRVehicleAudioComponent::FindOrAddTo(Vehicle);

	return true;
}

void ALRGameModeEndless::StartRun(int32 Seed)
{
	ULRRunContextSubsystem* Context = ULRRunContextSubsystem::Get(this);
	const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	if (!PlayerVehicle || !Context || !Road)
	{
		UE_LOG(LogLRGameplay, Warning, TEXT("StartRun ignored: the player car or a world subsystem is missing"));
		return;
	}

	ClearPause();
	ClearRunTimers();
	ScrapesUsed = 0;

	// A restart during the countdown goes through Loading so the countdown overlay starts again.
	const ELRRunState CurrentState = StateMachine.GetState();
	if (CurrentState == ELRRunState::Garage || CurrentState == ELRRunState::Countdown)
	{
		TransitionTo(ELRRunState::Loading);
	}

	// Road, traffic, score, cameras and audio reset themselves synchronously on OnRunReset.
	Context->BeginNewRun(Seed, ModeType);

	const int32 StartLane = FLRGameRulesLogic::ResolveStartLane(Road->GetPath().GetLanes(), Road->GetDefaultPlayerLane(), Rules.StartingLaneOffset);
	PlayerVehicle->ResetForRun(Road->GetSpawnS(), Road->GetLaneCenterD(StartLane), Rules.StartSpeedKmh);
	PlayerVehicle->SetInputEnabled(false);

	TransitionTo(ELRRunState::Countdown);

	if (Rules.CountdownSeconds > 0.f)
	{
		GetWorldTimerManager().SetTimer(CountdownTimer, this, &ALRGameModeEndless::HandleCountdownFinished, Rules.CountdownSeconds, false);
	}
	else
	{
		HandleCountdownFinished();
	}
}

void ALRGameModeEndless::RestartRun()
{
	if (FLRRunStateMachine::CanRestartFrom(StateMachine.GetState()))
	{
		StartRun(FLRGameRulesLogic::MakeRunSeed(ModeType, FDateTime::UtcNow()));
	}
}

void ALRGameModeEndless::RestartInMode(ELRGameModeType Mode)
{
	ModeType = Mode;
	StartRun(FLRGameRulesLogic::MakeRunSeed(ModeType, FDateTime::UtcNow()));
}

void ALRGameModeEndless::EnterTitle()
{
	ULRRunContextSubsystem* Context = ULRRunContextSubsystem::Get(this);
	const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	if (!PlayerVehicle || !Context || !Road)
	{
		UE_LOG(LogLRGameplay, Warning, TEXT("EnterTitle ignored: the player car or a world subsystem is missing"));
		return;
	}

	ClearPause();
	ClearRunTimers();
	ScrapesUsed = 0;

	if (StateMachine.GetState() != ELRRunState::Garage)
	{
		TransitionTo(ELRRunState::Garage);
	}

	Context->BeginNewRun(FLRGameRulesLogic::MakeRunSeed(ModeType, FDateTime::UtcNow()), ModeType);

	const int32 Lane = FLRGameRulesLogic::ResolveStartLane(Road->GetPath().GetLanes(), Road->GetDefaultPlayerLane(), Rules.StartingLaneOffset);
	PlayerVehicle->ResetForRun(Road->GetSpawnS(), Road->GetLaneCenterD(Lane), Rules.TitleCruiseKmh);

	// UpdateTitleCruise drives the pedals. The player sends nothing: the title menu holds the input.
	PlayerVehicle->SetInputEnabled(true);
}

void ALRGameModeEndless::PlayFromTitle()
{
	if (StateMachine.GetState() == ELRRunState::Garage)
	{
		StartRun(FLRGameRulesLogic::MakeRunSeed(ModeType, FDateTime::UtcNow()));
	}
}

const TArray<ELRGameModeType>& ALRGameModeEndless::GetTitleModes()
{
	static const TArray<ELRGameModeType> Modes = {
		ELRGameModeType::EndlessOneWay,
		ELRGameModeType::EndlessTwoWay,
		ELRGameModeType::FreeRide,
		ELRGameModeType::Daily,
	};
	return Modes;
}

void ALRGameModeEndless::CycleMode(int32 Direction)
{
	const TArray<ELRGameModeType>& Modes = GetTitleModes();
	const int32 Current = FMath::Max(Modes.IndexOfByKey(ModeType), 0);
	ModeType = Modes[(Current + (Direction < 0 ? -1 : 1) + Modes.Num()) % Modes.Num()];

	if (StateMachine.GetState() == ELRRunState::Garage)
	{
		EnterTitle();
	}
	else
	{
		StartRun(FLRGameRulesLogic::MakeRunSeed(ModeType, FDateTime::UtcNow()));
	}
}

bool ALRGameModeEndless::SetPlayerVehicle(FName VehicleId)
{
	if (!PlayerVehicle || !PlayerVehicle->LoadDefinitionById(VehicleId))
	{
		UE_LOG(LogLRGameplay, Warning, TEXT("Could not switch to vehicle '%s'"), *VehicleId.ToString());
		return false;
	}

	if (ULRProgressionSubsystem* Progression = ULRProgressionSubsystem::Get(this))
	{
		Progression->SelectVehicle(VehicleId);
	}

	if (StateMachine.GetState() == ELRRunState::Garage)
	{
		EnterTitle();
	}
	else
	{
		StartRun(FLRGameRulesLogic::MakeRunSeed(ModeType, FDateTime::UtcNow()));
	}
	return true;
}

void ALRGameModeEndless::CycleVehicle(int32 Direction)
{
	if (!PlayerVehicle)
	{
		return;
	}

	TArray<FString> Files = FLRDataLoader::ListFiles(TEXT("Vehicles"));
	if (Files.Num() == 0)
	{
		return;
	}
	for (FString& File : Files)
	{
		File = FPaths::GetBaseFilename(File);
	}

	const int32 Current = FMath::Max(Files.IndexOfByKey(PlayerVehicle->GetDefinition().Id.ToString()), 0);
	const int32 Step = Direction < 0 ? -1 : 1;
	SetPlayerVehicle(FName(*Files[(Current + Step + Files.Num()) % Files.Num()]));
}

void ALRGameModeEndless::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (StateMachine.GetState() == ELRRunState::Garage)
	{
		UpdateTitleCruise();
	}
}

void ALRGameModeEndless::UpdateTitleCruise()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALRGameModeEndless::UpdateTitleCruise);
	using namespace LRGameModeEndlessPrivate;

	if (!PlayerVehicle || PlayerVehicle->IsWrecked())
	{
		return;
	}

	const FLRRoadCoord Coord = PlayerVehicle->GetRoadCoord();
	float LeaderGap = -1.f;
	float LeaderSpeedKmh = 0.f;
	if (const ULRTrafficSubsystem* Traffic = ULRTrafficSubsystem::Get(this))
	{
		Traffic->GetSnapshot(TitleTrafficScratch, Coord.S, Coord.S + 2.0 * Rules.TitleFollowGapMeters + TitleLookAheadMarginMeters);
		if (!FLRTitleCruise::FindLeader(TitleTrafficScratch, Coord.S, Coord.D, TitleLaneHalfWidthMeters, LeaderGap, LeaderSpeedKmh))
		{
			LeaderGap = -1.f;
		}
	}

	const float TargetKmh = FLRTitleCruise::ComputeTargetSpeedKmh(Rules.TitleCruiseKmh, Rules.TitleFollowGapMeters, LeaderGap, LeaderSpeedKmh);
	const FLRTitleCruisePedals Pedals = FLRTitleCruise::ComputePedals(PlayerVehicle->GetSpeedKmh(), TargetKmh);
	PlayerVehicle->SetThrottleInput(Pedals.Throttle);
	PlayerVehicle->SetBrakeInput(Pedals.Brake);
	PlayerVehicle->SetSteeringInput(0.f);
}

bool ALRGameModeEndless::TransitionTo(ELRRunState NewState)
{
	const ELRRunState OldState = StateMachine.GetState();
	if (!StateMachine.TryTransition(NewState))
	{
		UE_LOG(LogLRGameplay, Warning, TEXT("Invalid run state transition %s -> %s"),
			*UEnum::GetValueAsString(OldState), *UEnum::GetValueAsString(NewState));
		return false;
	}

	if (ULRRunContextSubsystem* Context = ULRRunContextSubsystem::Get(this))
	{
		Context->SetRunState(NewState);
	}
	return true;
}

void ALRGameModeEndless::HandleCountdownFinished()
{
	if (TransitionTo(ELRRunState::Running) && PlayerVehicle)
	{
		PlayerVehicle->SetInputEnabled(true);
	}
}

void ALRGameModeEndless::HandleResultsDelayFinished()
{
	TransitionTo(ELRRunState::Results);
}

void ALRGameModeEndless::HandleCrash(const FLRCrashEvent& Event)
{
	if (StateMachine.GetState() == ELRRunState::Garage)
	{
		// A hard hit behind the title menu: rebuild the scene, but not from inside the physics callback.
		if (Event.bSevere)
		{
			GetWorldTimerManager().SetTimerForNextTick(this, &ALRGameModeEndless::EnterTitle);
		}
		return;
	}
	if (StateMachine.GetState() != ELRRunState::Running)
	{
		return;
	}

	const bool bFreeRide = ModeType == ELRGameModeType::FreeRide;
	switch (FLRGameRulesLogic::EvaluateCrash(Rules, bFreeRide, Event.bSevere, Event.ImpactSpeedKmh, ScrapesUsed))
	{
	case ELRCrashOutcome::EndRun:
		EndRun();
		break;
	case ELRCrashOutcome::ResetCar:
		ScheduleCrashRecovery();
		break;
	case ELRCrashOutcome::DamageScrape:
		++ScrapesUsed;
		ScheduleCrashRecovery();
		break;
	case ELRCrashOutcome::Scrape:
	default:
		// The audio subsystem cuts the engine on every severe event, so a surviving one has to restore it.
		if (Event.bSevere)
		{
			ScheduleCrashRecovery();
		}
		break;
	}
}

void ALRGameModeEndless::EndRun()
{
	if (!PlayerVehicle)
	{
		return;
	}

	ClearRunTimers();
	PlayerVehicle->SetWrecked(true);

	// Leaving Running stops the score component.
	if (!TransitionTo(ELRRunState::Crashed))
	{
		return;
	}

	FLRRunResult Result;
	if (const ULRScoreComponent* Score = GetScoreComponent())
	{
		Result = Score->BuildRunResult();
	}
	if (ULRProgressionSubsystem* Progression = ULRProgressionSubsystem::Get(this))
	{
		Result.CreditsEarned = Progression->RecordRunResult(Result, ModeType);
	}
	if (const ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
	{
		Events->BroadcastRunFinished(Result);
	}

	if (Rules.ResultsDelaySeconds > 0.f)
	{
		GetWorldTimerManager().SetTimer(ResultsTimer, this, &ALRGameModeEndless::HandleResultsDelayFinished, Rules.ResultsDelaySeconds, false);
	}
	else
	{
		HandleResultsDelayFinished();
	}
}

void ALRGameModeEndless::ScheduleCrashRecovery()
{
	FTimerManager& Timers = GetWorldTimerManager();
	if (!Timers.IsTimerActive(CrashRecoveryTimer))
	{
		Timers.SetTimer(CrashRecoveryTimer, this, &ALRGameModeEndless::HandleCrashRecovery, Rules.CrashRecoverySeconds, false);
	}
}

void ALRGameModeEndless::HandleCrashRecovery()
{
	if (!PlayerVehicle)
	{
		return;
	}

	if (ModeType == ELRGameModeType::FreeRide)
	{
		const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
		if (Road)
		{
			const FLRRoadCoord Coord = PlayerVehicle->GetRoadCoord();
			const int32 Lane = FLRGameRulesLogic::ResolveStartLane(Road->GetPath().GetLanes(), Road->GetDefaultPlayerLane(), PlayerVehicle->GetCurrentLane() - Road->GetDefaultPlayerLane());
			PlayerVehicle->ResetForRun(Coord.S + Rules.FreeRideResetForwardMeters, Road->GetLaneCenterD(Lane), Rules.StartSpeedKmh);
		}
		if (ULRScoreComponent* Score = GetScoreComponent())
		{
			Score->NotifyVehicleTeleported();
		}
	}

	if (ULRVehicleAudioComponent* Audio = PlayerVehicle->FindComponentByClass<ULRVehicleAudioComponent>())
	{
		Audio->ResetForNewRun();
	}
}

void ALRGameModeEndless::ClearRunTimers()
{
	FTimerManager& Timers = GetWorldTimerManager();
	Timers.ClearTimer(CountdownTimer);
	Timers.ClearTimer(ResultsTimer);
	Timers.ClearTimer(CrashRecoveryTimer);
}

ULRScoreComponent* ALRGameModeEndless::GetScoreComponent() const
{
	const ALRGameState* LRGameState = GetGameState<ALRGameState>();
	return LRGameState ? LRGameState->GetScoreComponent() : nullptr;
}
