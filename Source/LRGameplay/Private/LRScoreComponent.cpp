// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRScoreComponent.h"

#include "LRDataLoader.h"
#include "LRDataSubsystem.h"
#include "LRGameEventSubsystem.h"
#include "LRGameplay.h"
#include "LRRunContextSubsystem.h"
#include "LRVehicle.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

ULRScoreComponent::ULRScoreComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void ULRScoreComponent::BeginPlay()
{
	Super::BeginPlay();

	ReloadRules();

	RunContext = ULRRunContextSubsystem::Get(this);
	EventBus = ULRGameEventSubsystem::Get(this);
	if (ULRGameEventSubsystem* Events = EventBus.Get())
	{
		NearMissHandle = Events->OnNearMiss.AddUObject(this, &ULRScoreComponent::HandleNearMiss);
		CrashHandle = Events->OnCrash.AddUObject(this, &ULRScoreComponent::HandleCrash);
		RunResetHandle = Events->OnRunReset.AddUObject(this, &ULRScoreComponent::HandleRunReset);
	}

	if (ULRDataSubsystem* Data = ULRDataSubsystem::Get(this))
	{
		DataReloadedHandle = Data->OnDataReloaded.AddUObject(this, &ULRScoreComponent::ReloadRules);
	}
}

void ULRScoreComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ULRGameEventSubsystem* Events = EventBus.Get())
	{
		Events->OnNearMiss.Remove(NearMissHandle);
		Events->OnCrash.Remove(CrashHandle);
		Events->OnRunReset.Remove(RunResetHandle);
	}
	if (ULRDataSubsystem* Data = ULRDataSubsystem::Get(this))
	{
		Data->OnDataReloaded.Remove(DataReloadedHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void ULRScoreComponent::ReloadRules()
{
	FLRScoringRules Loaded;
	FString Error;
	if (FLRDataLoader::Load(FLRScoringRules::DataPath, Loaded, &Error))
	{
		Calculator.Configure(Loaded);
	}
	else
	{
		UE_LOG(LogLRGameplay, Warning, TEXT("Keeping the current scoring rules: %s"), *Error);
	}
}

void ULRScoreComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ULRScoreComponent::TickComponent);

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const ULRRunContextSubsystem* Context = RunContext.Get();
	if (!Context || !Context->IsRunning())
	{
		return;
	}

	const ALRVehicle* Vehicle = Cast<ALRVehicle>(Context->GetPlayerPawn());
	if (!Vehicle)
	{
		return;
	}

	const float SpeedKmh = Vehicle->GetSpeedKmh();
	const double RoadS = Vehicle->GetRoadCoord().S;

	if (!bHasDistanceOrigin)
	{
		FarthestS = RoadS;
		bHasDistanceOrigin = true;
	}

	if (RoadS > FarthestS)
	{
		const int64 Points = Calculator.AddDistance(RoadS - FarthestS, SpeedKmh);
		FarthestS = RoadS;
		if (Points > 0)
		{
			static const FName DistanceReason(TEXT("Distance"));
			BroadcastScore(Points, DistanceReason);
		}
	}

	const ELRComboTickResult ComboResult = Calculator.Tick(DeltaTime, SpeedKmh);
	if (ComboResult != ELRComboTickResult::None)
	{
		BroadcastCombo(ComboResult == ELRComboTickResult::Broken);
	}

	if (Calculator.ArePointsEnabled())
	{
		const int32 SpeedTier = Calculator.GetSpeedTierIndex(SpeedKmh);
		if (SpeedTier != LastSpeedTier)
		{
			LastSpeedTier = SpeedTier;
			if (const ULRGameEventSubsystem* Events = EventBus.Get())
			{
				FLRSpeedTierChangedEvent Event;
				Event.TierIndex = SpeedTier;
				Event.Multiplier = Calculator.GetSpeedMultiplier(SpeedKmh);
				Events->BroadcastSpeedTierChanged(Event);
			}
		}
	}
}

void ULRScoreComponent::HandleNearMiss(const FLRNearMissEvent& Event)
{
	const ULRRunContextSubsystem* Context = RunContext.Get();
	if (!Context || !Context->IsRunning() || !Calculator.ArePointsEnabled())
	{
		return;
	}

	const int32 PreviousChain = Calculator.GetState().Chain;
	const int64 Points = Calculator.ApplyNearMiss(Event.Tier, Event.RelativeSpeedKmh, Event.bOncoming, Event.bThreadTheNeedle, GetPlayerSpeedKmh());
	if (Points > 0)
	{
		static const FName NearMissReason(TEXT("NearMiss"));
		BroadcastScore(Points, NearMissReason);
	}
	if (Calculator.GetState().Chain != PreviousChain)
	{
		BroadcastCombo(false);
	}
}

void ULRScoreComponent::HandleCrash(const FLRCrashEvent& Event)
{
	const ULRRunContextSubsystem* Context = RunContext.Get();
	if (Context && Context->IsRunning() && Calculator.BreakCombo())
	{
		BroadcastCombo(true);
	}
}

void ULRScoreComponent::HandleRunReset(const FLRRunResetEvent& Event)
{
	Calculator.Reset();
	Calculator.SetPointsEnabled(Event.Mode != ELRGameModeType::FreeRide);
	bHasDistanceOrigin = false;
	FarthestS = 0.0;
	LastSpeedTier = INDEX_NONE;
}

void ULRScoreComponent::NotifyVehicleTeleported()
{
	bHasDistanceOrigin = false;
}

FLRRunResult ULRScoreComponent::BuildRunResult() const
{
	const FLRScoreState& State = Calculator.GetState();

	FLRRunResult Result;
	Result.FinalScore = State.Score;
	Result.DistanceMeters = static_cast<float>(State.DistanceMeters);
	Result.DurationSeconds = State.DurationSeconds;
	Result.NearMisses = State.NearMisses;
	Result.BestCombo = State.BestChain;
	Result.TopSpeedKmh = State.TopSpeedKmh;
	if (const ULRRunContextSubsystem* Context = RunContext.Get())
	{
		Result.Seed = Context->GetRunSeed();
	}
	return Result;
}

void ULRScoreComponent::BroadcastScore(int64 Delta, FName Reason) const
{
	if (const ULRGameEventSubsystem* Events = EventBus.Get())
	{
		FLRScoreChangedEvent Event;
		Event.Score = Calculator.GetState().Score;
		Event.Delta = Delta;
		Event.Reason = Reason;
		Events->BroadcastScoreChanged(Event);
	}
}

void ULRScoreComponent::BroadcastCombo(bool bBroken) const
{
	if (const ULRGameEventSubsystem* Events = EventBus.Get())
	{
		FLRComboChangedEvent Event;
		Event.Chain = Calculator.GetState().Chain;
		Event.Multiplier = Calculator.GetComboMultiplier();
		Event.TimeRemaining = Calculator.GetState().ComboTimeRemaining;
		Event.bBroken = bBroken;
		Events->BroadcastComboChanged(Event);
	}
}

float ULRScoreComponent::GetPlayerSpeedKmh() const
{
	const ULRRunContextSubsystem* Context = RunContext.Get();
	const ALRVehicle* Vehicle = Context ? Cast<ALRVehicle>(Context->GetPlayerPawn()) : nullptr;
	return Vehicle ? Vehicle->GetSpeedKmh() : 0.f;
}
