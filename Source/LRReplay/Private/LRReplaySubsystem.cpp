// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRReplaySubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "LRDataLoader.h"
#include "LREvents.h"
#include "LRGameEventSubsystem.h"
#include "LRReplay.h"
#include "LRRoadSubsystem.h"
#include "LRRunContextSubsystem.h"
#include "LRTrafficSubsystem.h"
#include "LRUnits.h"
#include "LRVehicle.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

const TCHAR* ULRReplaySubsystem::SettingsFile = TEXT("Replay/Replay.json");

ULRReplaySubsystem* ULRReplaySubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext || !GEngine)
	{
		return nullptr;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<ULRReplaySubsystem>() : nullptr;
}

bool ULRReplaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULRReplaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<ULRGameEventSubsystem>();
	Collection.InitializeDependency<ULRRunContextSubsystem>();

	LoadSettingsAndAllocate();

	if (ULRGameEventSubsystem* Events = GetWorld()->GetSubsystem<ULRGameEventSubsystem>())
	{
		RunResetHandle = Events->OnRunReset.AddUObject(this, &ULRReplaySubsystem::HandleRunReset);
		RunStateChangedHandle = Events->OnRunStateChanged.AddUObject(this, &ULRReplaySubsystem::HandleRunStateChanged);
	}
}

void ULRReplaySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (ULRGameEventSubsystem* Events = World->GetSubsystem<ULRGameEventSubsystem>())
		{
			Events->OnRunReset.Remove(RunResetHandle);
			Events->OnRunStateChanged.Remove(RunStateChangedHandle);
		}
	}
	RunResetHandle.Reset();
	RunStateChangedHandle.Reset();

	bRecording = false;
	Buffer.Clear();

	Super::Deinitialize();
}

void ULRReplaySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// The run may already be live if the game mode switched to Running before this subsystem was bound.
	if (const ULRRunContextSubsystem* RunContext = InWorld.GetSubsystem<ULRRunContextSubsystem>())
	{
		if (RunContext->IsRunning() && !bRecording)
		{
			StartRecording();
		}
	}
}

TStatId ULRReplaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULRReplaySubsystem, STATGROUP_Tickables);
}

void ULRReplaySubsystem::LoadSettingsAndAllocate()
{
	const FString AbsolutePath = FLRDataLoader::ToAbsolutePath(SettingsFile);
	if (FPaths::FileExists(AbsolutePath))
	{
		FLRReplaySettings Loaded;
		FString Error;
		if (FLRDataLoader::Load(SettingsFile, Loaded, &Error))
		{
			Settings = Loaded;
		}
		else
		{
			UE_LOG(LogLRReplay, Warning, TEXT("Could not load %s, using defaults: %s"), SettingsFile, *Error);
		}
	}

	Settings.MaxTrafficVehicles = FMath::Clamp(Settings.MaxTrafficVehicles, 0, 256);
	Settings.TrafficRadiusMeters = FMath::Max(Settings.TrafficRadiusMeters, 0.f);

	Buffer.Configure(Settings);
	SampleClock.Configure(Settings.SampleRateHz);
	ScratchFrame.Traffic.Empty(Settings.MaxTrafficVehicles);
	TrafficScratch.Empty(Settings.MaxTrafficVehicles * 2);

	UE_LOG(LogLRReplay, Log, TEXT("Replay buffer: %d frames (%.0f s at %.0f Hz), %d traffic per frame, %.1f MB"),
		Buffer.GetCapacity(), Settings.DurationSeconds, Settings.SampleRateHz, Buffer.GetMaxTrafficPerFrame(),
		static_cast<double>(Buffer.GetAllocatedSize()) / (1024.0 * 1024.0));
}

void ULRReplaySubsystem::StartRecording()
{
	Buffer.Clear();
	SampleClock.Reset();
	bRecording = true;
}

void ULRReplaySubsystem::ResumeRecording()
{
	// The sample clock keeps its time, so new frames stay strictly after the recorded ones.
	bRecording = true;
}

void ULRReplaySubsystem::StopRecording()
{
	bRecording = false;
}

void ULRReplaySubsystem::ClearRecording()
{
	bRecording = false;
	Buffer.Clear();
	SampleClock.Reset();
}

bool ULRReplaySubsystem::EvaluateAt(float TimeFromStart, FLRReplayFrame& OutFrame) const
{
	return Buffer.EvaluateAt(TimeFromStart, OutFrame);
}

void ULRReplaySubsystem::HandleRunReset(const FLRRunResetEvent& Event)
{
	ClearRecording();

	// BeginNewRun does not change the run state: restarting from Running fires no state event, so resume here.
	const ULRRunContextSubsystem* RunContext = ULRRunContextSubsystem::Get(this);
	if (RunContext && RunContext->IsRunning())
	{
		StartRecording();
	}
}

void ULRReplaySubsystem::HandleRunStateChanged(const FLRRunStateChangedEvent& Event)
{
	if (Event.Current == ELRRunState::Running)
	{
		if (bRecording)
		{
			return;
		}
		if (Buffer.IsEmpty())
		{
			StartRecording();
		}
		else
		{
			ResumeRecording();
		}
		return;
	}

	// Crashed, Replay, Results, Garage...: freeze the buffer so it can be played back.
	StopRecording();
}

void ULRReplaySubsystem::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ULRReplaySubsystem::Tick);

	Super::Tick(DeltaTime);

	if (!bRecording)
	{
		return;
	}

	if (SampleClock.Advance(DeltaTime))
	{
		CaptureFrame(SampleClock.GetTime());
	}
}

void ULRReplaySubsystem::CaptureFrame(float Time)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ULRReplaySubsystem::CaptureFrame);

	const ULRRunContextSubsystem* RunContext = ULRRunContextSubsystem::Get(this);
	const APawn* PlayerPawn = RunContext ? RunContext->GetPlayerPawn() : nullptr;
	if (!PlayerPawn)
	{
		return;
	}

	FLRReplayFrame& Frame = ScratchFrame;
	Frame.Time = Time;
	Frame.PlayerTransform = PlayerPawn->GetActorTransform();

	if (const ALRVehicle* Vehicle = Cast<ALRVehicle>(PlayerPawn))
	{
		Frame.PlayerRoadCoord = Vehicle->GetRoadCoord();
		Frame.PlayerSpeedKmh = Vehicle->GetSpeedKmh();
		Frame.EngineRpm = Vehicle->GetEngineRpm();
		Frame.SteeringInput = Vehicle->GetAppliedSteering();
		Frame.ThrottleInput = Vehicle->GetThrottleInput();
		Frame.BrakeInput = Vehicle->GetBrakeInput();
		Frame.Gear = Vehicle->GetCurrentGear();
	}
	else
	{
		// Generic pawn (debug spectator...): only kinematics are available.
		FLRRoadCoord Coord;
		const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
		Frame.PlayerRoadCoord = (Road && Road->GetPlayerCoord(Coord)) ? Coord : FLRRoadCoord();
		Frame.PlayerSpeedKmh = static_cast<float>(LRUnits::CmsToKmh(PlayerPawn->GetVelocity().Size()));
		Frame.EngineRpm = 0.f;
		Frame.SteeringInput = 0.f;
		Frame.ThrottleInput = 0.f;
		Frame.BrakeInput = 0.f;
		Frame.Gear = 0;
	}

	// Nearby traffic: everything within the radius, then the nearest MaxTrafficVehicles along S.
	Frame.Traffic.Reset();
	const int32 MaxTraffic = Settings.MaxTrafficVehicles;
	if (MaxTraffic > 0)
	{
		if (const ULRTrafficSubsystem* Traffic = ULRTrafficSubsystem::Get(this))
		{
			const double PlayerS = Frame.PlayerRoadCoord.S;
			const double Radius = Settings.TrafficRadiusMeters;
			Traffic->GetSnapshot(TrafficScratch, PlayerS - Radius, PlayerS + Radius);

			if (TrafficScratch.Num() > MaxTraffic)
			{
				// In-place introsort, no allocation.
				TrafficScratch.Sort([PlayerS](const FLRTrafficVehicleSnapshot& Left, const FLRTrafficVehicleSnapshot& Right)
				{
					return FMath::Abs(Left.S - PlayerS) < FMath::Abs(Right.S - PlayerS);
				});
			}
			Frame.Traffic.Append(TrafficScratch.GetData(), FMath::Min(TrafficScratch.Num(), MaxTraffic));
		}
	}

	if (!Buffer.Add(Frame))
	{
		UE_LOG(LogLRReplay, Verbose, TEXT("Replay frame at %.3f s rejected (non-increasing time)."), Time);
	}
}
