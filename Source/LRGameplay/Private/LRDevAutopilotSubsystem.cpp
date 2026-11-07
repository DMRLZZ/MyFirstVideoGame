// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRDevAutopilotSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "DynamicResolutionState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "RHI.h"
#include "HAL/PlatformMisc.h"
#include "LRCoreTypes.h"
#include "LRGameModeEndless.h"
#include "LRGameplay.h"
#include "LRPlayerCameraManager.h"
#include "LRRoadSubsystem.h"
#include "LRRunContextSubsystem.h"
#include "LRTrafficSubsystem.h"
#include "LRVehicle.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace LRDevAutopilotPrivate
{
	/** Distance ahead scanned for traffic when choosing a lane, meters. */
	constexpr double LookAheadMeters = 300.0;

	/** Only change lane when the car ahead is closer than this, meters. */
	constexpr double ComfortGapMeters = 160.0;

	/** A candidate lane must beat the current one by this margin, meters. */
	constexpr double MinGapAdvantageMeters = 25.0;

	constexpr double LaneDecisionPeriodSeconds = 0.8;
	constexpr double TelemetryPeriodSeconds = 2.0;
	constexpr double RestartDelaySeconds = 6.0;
	constexpr double TracePeriodSeconds = 0.05;
	constexpr double LaneSweepPeriodSeconds = 6.0;

	/** A frame longer than this counts as a visible hitch (under 30 fps). */
	constexpr double HitchThresholdSeconds = 1.0 / 30.0;
}

bool ULRDevAutopilotSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return Super::ShouldCreateSubsystem(Outer) && FParse::Param(FCommandLine::Get(), TEXT("LRDevDrive"));
#endif
}

bool ULRDevAutopilotSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULRDevAutopilotSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const TCHAR* CommandLine = FCommandLine::Get();
	FParse::Value(CommandLine, TEXT("LRDevSeconds="), QuitAfterSeconds);
	FParse::Value(CommandLine, TEXT("LRDevShotEvery="), ScreenshotPeriodSeconds);
	bCycleCameras = FParse::Param(CommandLine, TEXT("LRDevCycleCameras"));
	bSteer = !FParse::Param(CommandLine, TEXT("LRDevNoSteer"));
	bNoTraffic = FParse::Param(CommandLine, TEXT("LRDevNoTraffic"));
	bLaneSweep = FParse::Param(CommandLine, TEXT("LRDevLaneSweep"));
	bSteerTest = FParse::Param(CommandLine, TEXT("LRDevSteerTest"));
	TraceBuffer = TEXT("t,state,speed_kmh,S,D,target_d,heading_deg,yaw_rate,steering,height_m\n");
	NextScreenshotTime = ScreenshotPeriodSeconds;

	FParse::Value(CommandLine, TEXT("LRDevExecAt="), ExecAtSeconds);
	if (FParse::Value(CommandLine, TEXT("LRDevExec="), ExecCommand))
	{
		ExecCommand.ReplaceCharInline(TEXT('+'), TEXT(' '));
	}

	FString Showroom;
	if (FParse::Value(CommandLine, TEXT("LRDevShowroom="), Showroom))
	{
		Showroom.ParseIntoArray(ShowroomVehicles, TEXT("+"));
		bNoTraffic = true;
	}

	UE_LOG(LogLRGameplay, Log, TEXT("DevAutopilot: active, quit after %.0f s, screenshot every %.0f s"), QuitAfterSeconds, ScreenshotPeriodSeconds);
}

TStatId ULRDevAutopilotSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULRDevAutopilotSubsystem, STATGROUP_Tickables);
}

void ULRDevAutopilotSubsystem::Tick(float DeltaTime)
{
	using namespace LRDevAutopilotPrivate;

	ElapsedSeconds += DeltaTime;
	AccumulatedFrameSeconds += DeltaTime;
	++AccumulatedFrames;
	WorstFrameSeconds = FMath::Max(WorstFrameSeconds, static_cast<double>(DeltaTime));
	HitchFrames += DeltaTime > HitchThresholdSeconds ? 1 : 0;

	if (ElapsedSeconds >= QuitAfterSeconds && !bQuitRequested)
	{
		bQuitRequested = true;
		UE_LOG(LogLRGameplay, Log, TEXT("DevAutopilot: time is up, quitting"));
		FlushTrace();
		FPlatformMisc::RequestExit(false, TEXT("LRDevAutopilot"));
		return;
	}

	const ULRRunContextSubsystem* Context = ULRRunContextSubsystem::Get(this);
	ALRVehicle* Vehicle = Context ? Cast<ALRVehicle>(Context->GetPlayerPawn()) : nullptr;
	if (Vehicle == nullptr)
	{
		return;
	}

	if (ExecAtSeconds >= 0.0 && ElapsedSeconds >= ExecAtSeconds)
	{
		ExecAtSeconds = -1.0;
		GEngine->Exec(GetWorld(), *ExecCommand);
	}

	if (ElapsedSeconds >= NextTraceTime)
	{
		NextTraceTime = ElapsedSeconds + TracePeriodSeconds;
		SampleTrace(*Vehicle);
	}

	if (ElapsedSeconds >= NextTelemetryTime)
	{
		NextTelemetryTime = ElapsedSeconds + TelemetryPeriodSeconds;
		LogTelemetry(*Vehicle);
	}

	if (ScreenshotPeriodSeconds > 0.0 && ElapsedSeconds >= NextScreenshotTime)
	{
		NextScreenshotTime = ElapsedSeconds + ScreenshotPeriodSeconds;
		TakeScreenshot();

		// Switch camera well before the next shot so each screenshot shows a settled view.
		NextCameraCycleTime = bCycleCameras ? ElapsedSeconds + ScreenshotPeriodSeconds * 0.5 : -1.0;
	}

	if (NextCameraCycleTime >= 0.0 && ElapsedSeconds >= NextCameraCycleTime)
	{
		NextCameraCycleTime = -1.0;
		const APlayerController* Player = GetWorld()->GetFirstPlayerController();
		if (ALRPlayerCameraManager* Camera = Player ? Cast<ALRPlayerCameraManager>(Player->PlayerCameraManager) : nullptr)
		{
			Camera->CycleCameraMode();
		}
	}

	if (ShowroomVehicles.Num() > 0)
	{
		TickShowroom(DeltaTime);
		return;
	}

	const ELRRunState State = Context->GetRunState();
	if (State == ELRRunState::Running)
	{
		RestartAtTime = -1.0;
		Vehicle->SetThrottleInput(1.f);
		Vehicle->SetBrakeInput(0.f);

		if (bNoTraffic && !bTrafficCleared)
		{
			bTrafficCleared = true;
			if (ULRTrafficSubsystem* Traffic = ULRTrafficSubsystem::Get(this))
			{
				Traffic->SetDensityScale(0.f);
				Traffic->ResetTraffic(Context->GetRunSeed());
			}
		}

		if (bSteerTest)
		{
			// One second right, a rest, a tap to the left, a rest, one second left, a rest: repeats every 12 s.
			const double Phase = FMath::Fmod(ElapsedSeconds, 12.0);
			const float Script = Phase < 1.0 ? 1.f : (Phase >= 4.0 && Phase < 4.1 ? -1.f : (Phase >= 7.0 && Phase < 8.0 ? -1.f : 0.f));
			Vehicle->SetSteeringInput(Script);
		}
		else if (bSteer && ElapsedSeconds >= NextLaneDecisionTime)
		{
			if (bLaneSweep)
			{
				NextLaneDecisionTime = ElapsedSeconds + LaneSweepPeriodSeconds;
				const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
				const int32 NextLane = Vehicle->GetTargetLane() + SweepDirection;
				if (Road != nullptr && (NextLane < 0 || NextLane >= Road->GetNumLanes()))
				{
					SweepDirection = -SweepDirection;
				}
				Vehicle->RequestLaneChange(SweepDirection);
			}
			else
			{
				NextLaneDecisionTime = ElapsedSeconds + LaneDecisionPeriodSeconds;
				if (const int32 Direction = ChooseLaneChange(*Vehicle))
				{
					Vehicle->RequestLaneChange(Direction);
				}
			}
		}
	}
	else if (State == ELRRunState::Crashed || State == ELRRunState::Results)
	{
		if (RestartAtTime < 0.0)
		{
			RestartAtTime = ElapsedSeconds + RestartDelaySeconds;
		}
		else if (ElapsedSeconds >= RestartAtTime)
		{
			RestartAtTime = -1.0;
			if (ALRGameModeEndless* GameMode = GetWorld()->GetAuthGameMode<ALRGameModeEndless>())
			{
				UE_LOG(LogLRGameplay, Log, TEXT("DevAutopilot: restarting run"));
				GameMode->RestartRun();
			}
		}
	}
}

int32 ULRDevAutopilotSubsystem::ChooseLaneChange(const ALRVehicle& Vehicle) const
{
	using namespace LRDevAutopilotPrivate;

	const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	const ULRTrafficSubsystem* Traffic = ULRTrafficSubsystem::Get(this);
	if (Road == nullptr || Traffic == nullptr || !Road->HasRoad())
	{
		return 0;
	}

	const FLRRoadCoord Coord = Vehicle.GetRoadCoord();
	const int32 CurrentLane = Vehicle.GetTargetLane();
	if (CurrentLane < 0 || CurrentLane >= Road->GetNumLanes())
	{
		return 0;
	}

	Traffic->GetSnapshot(SnapshotScratch, Coord.S, Coord.S + LookAheadMeters);

	// Free gap ahead in a lane: distance to the nearest vehicle overlapping it.
	const auto GapInLane = [this, Road, &Coord](int32 Lane)
	{
		const FLRLaneDesc& Desc = Road->GetLane(Lane);
		double Gap = LookAheadMeters;
		for (const FLRTrafficVehicleSnapshot& Other : SnapshotScratch)
		{
			if (FMath::Abs(Other.D - Desc.CenterD) < Desc.WidthMeters * 0.75f)
			{
				Gap = FMath::Min(Gap, Other.S - Coord.S);
			}
		}
		return Gap;
	};

	const double CurrentGap = GapInLane(CurrentLane);
	if (CurrentGap >= ComfortGapMeters)
	{
		return 0;
	}

	int32 BestDirection = 0;
	double BestGap = CurrentGap + MinGapAdvantageMeters;
	for (const int32 Direction : { -1, 1 })
	{
		const int32 Candidate = CurrentLane + Direction;
		if (Candidate < 0 || Candidate >= Road->GetNumLanes() || Road->GetLane(Candidate).Direction < 0)
		{
			continue;
		}

		const double Gap = GapInLane(Candidate);
		if (Gap > BestGap)
		{
			BestGap = Gap;
			BestDirection = Direction;
		}
	}
	return BestDirection;
}

void ULRDevAutopilotSubsystem::LogTelemetry(const ALRVehicle& Vehicle)
{
	const ULRRunContextSubsystem* Context = ULRRunContextSubsystem::Get(this);
	const ULRTrafficSubsystem* Traffic = ULRTrafficSubsystem::Get(this);
	const FLRRoadCoord Coord = Vehicle.GetRoadCoord();
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	const ALRPlayerCameraManager* Camera = Player ? Cast<ALRPlayerCameraManager>(Player->PlayerCameraManager) : nullptr;
	const double AverageFrameMs = AccumulatedFrames > 0 ? 1000.0 * AccumulatedFrameSeconds / AccumulatedFrames : 0.0;

	FDynamicResolutionStateInfos Resolution;
	GEngine->GetDynamicResolutionCurrentStateInfos(Resolution);

	UE_LOG(LogLRGameplay, Log,
		TEXT("DevTelemetry t=%.1f state=%s speed=%.0f km/h gear=%d rpm=%.0f S=%.1f D=%.2f lane=%d target=%d wrecked=%d traffic=%d cam=%s res=%.0f%% frame=%.1f ms (%.0f fps) worst=%.0f hitches=%d game=%.1f render=%.1f gpu=%.1f"),
		ElapsedSeconds,
		Context ? *UEnum::GetValueAsString(Context->GetRunState()) : TEXT("?"),
		Vehicle.GetSpeedKmh(), Vehicle.GetCurrentGear(), Vehicle.GetEngineRpm(),
		Coord.S, Coord.D, Vehicle.GetCurrentLane(), Vehicle.GetTargetLane(), Vehicle.IsWrecked() ? 1 : 0,
		Traffic ? Traffic->GetNumVehicles() : -1,
		Camera ? *UEnum::GetValueAsString(Camera->GetCameraMode()) : TEXT("?"),
		100.0f * Resolution.ResolutionFractionApproximations[GDynamicPrimaryResolutionFraction],
		AverageFrameMs, AverageFrameMs > 0.0 ? 1000.0 / AverageFrameMs : 0.0,
		1000.0 * WorstFrameSeconds, HitchFrames,
		FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime),
		FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));

	AccumulatedFrameSeconds = 0.0;
	AccumulatedFrames = 0;
	WorstFrameSeconds = 0.0;
	HitchFrames = 0;
}

void ULRDevAutopilotSubsystem::TickShowroom(float DeltaTime)
{
	struct FShot
	{
		const TCHAR* Name;
		FVector EyeM;
		FVector TargetM;
		float Fov;
	};
	// Positions in meters relative to the car: X forward, Y right. The last ones are close-ups.
	static const FShot Shots[] = {
		{ TEXT("front34"), FVector(5.2, -4.6, 1.1), FVector(0.1, 0.0, 0.5), 28.f },
		{ TEXT("rear34"), FVector(-5.2, -4.6, 1.3), FVector(-0.1, 0.0, 0.55), 28.f },
		{ TEXT("side"), FVector(0.0, -8.5, 0.75), FVector(0.0, 0.0, 0.55), 30.f },
		{ TEXT("front"), FVector(8.0, 0.0, 0.8), FVector(0.0, 0.0, 0.55), 22.f },
		{ TEXT("rear"), FVector(-8.0, 0.0, 0.9), FVector(0.0, 0.0, 0.6), 22.f },
		{ TEXT("nose"), FVector(3.6, -1.9, 0.75), FVector(1.9, -0.55, 0.55), 30.f },
		{ TEXT("wheel"), FVector(1.5, -2.6, 0.5), FVector(1.35, -0.9, 0.35), 28.f },
		{ TEXT("cabin"), FVector(0.7, -2.0, 1.5), FVector(-0.2, -0.2, 0.75), 40.f },
	};
	constexpr double SettleSeconds = 1.5;
	constexpr double FirstShotDelaySeconds = 8.0;

	UWorld* World = GetWorld();
	APlayerController* Player = World->GetFirstPlayerController();
	ALRGameModeEndless* GameMode = World->GetAuthGameMode<ALRGameModeEndless>();
	const ULRRunContextSubsystem* Context = ULRRunContextSubsystem::Get(this);
	ALRVehicle* Vehicle = Context ? Cast<ALRVehicle>(Context->GetPlayerPawn()) : nullptr;
	const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	if (Player == nullptr || GameMode == nullptr || Vehicle == nullptr || Road == nullptr || !Road->HasRoad() || ElapsedSeconds < ShowroomNextStepTime)
	{
		return;
	}

	if (bShowroomShotPending)
	{
		const FShot& Shot = Shots[ShowroomCameraIndex];
		const FString FileName = FPaths::ProjectSavedDir() / FString::Printf(TEXT("DevShots/%s_%s.png"), *ShowroomVehicles[ShowroomVehicleIndex], Shot.Name);
		FScreenshotRequest::RequestScreenshot(FileName, /*bInShowUI*/ false, /*bAddFilenameSuffix*/ false);
		bShowroomShotPending = false;
		++ShowroomCameraIndex;
		ShowroomNextStepTime = ElapsedSeconds + 0.5;
		return;
	}

	if (ShowroomVehicleIndex == INDEX_NONE || ShowroomCameraIndex >= UE_ARRAY_COUNT(Shots))
	{
		++ShowroomVehicleIndex;
		ShowroomCameraIndex = 0;
		if (ShowroomVehicleIndex >= ShowroomVehicles.Num())
		{
			FPlatformMisc::RequestExit(false, TEXT("LRDevShowroom"));
			ShowroomNextStepTime = ElapsedSeconds + 1000.0;
			return;
		}
		GameMode->SetPlayerVehicle(FName(*ShowroomVehicles[ShowroomVehicleIndex]));
		bTrafficCleared = false;
		ShowroomNextStepTime = ElapsedSeconds + FirstShotDelaySeconds;
		return;
	}

	if (!bTrafficCleared)
	{
		bTrafficCleared = true;
		if (ULRTrafficSubsystem* Traffic = ULRTrafficSubsystem::Get(this))
		{
			Traffic->SetDensityScale(0.f);
			Traffic->ResetTraffic(Context->GetRunSeed());
		}
		if (AHUD* Hud = Player->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		// Parked in the middle of the road.
		Vehicle->ResetForRun(Road->GetSpawnS(), Road->GetLaneCenterD(Road->GetDefaultPlayerLane()), 0.f);
		Vehicle->SetInputEnabled(true);
		Vehicle->SetHandbrakeInput(true);
		ShowroomNextStepTime = ElapsedSeconds + 3.0;
		return;
	}

	if (ShowroomCamera == nullptr)
	{
		ACameraActor* Camera = World->SpawnActor<ACameraActor>();
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		ShowroomCamera = Camera;
	}
	const FShot& Shot = Shots[ShowroomCameraIndex];
	const FTransform& CarTransform = Vehicle->GetActorTransform();
	const FVector Eye = CarTransform.TransformPositionNoScale(Shot.EyeM * 100.0);
	const FVector Target = CarTransform.TransformPositionNoScale(Shot.TargetM * 100.0);
	ShowroomCamera->SetActorLocationAndRotation(Eye, (Target - Eye).Rotation());
	Cast<ACameraActor>(ShowroomCamera)->GetCameraComponent()->SetFieldOfView(Shot.Fov);
	Player->SetViewTarget(ShowroomCamera);
	bShowroomShotPending = true;
	ShowroomNextStepTime = ElapsedSeconds + SettleSeconds;
}

void ULRDevAutopilotSubsystem::SampleTrace(const ALRVehicle& Vehicle)
{
	const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	const ULRRunContextSubsystem* Context = ULRRunContextSubsystem::Get(this);
	const FLRRoadCoord Coord = Vehicle.GetRoadCoord();
	const double HeightM = Road != nullptr && Road->HasRoad()
		? (Vehicle.GetActorLocation().Z - Road->RoadToWorld(Coord.S, Coord.D).GetLocation().Z) * 0.01
		: 0.0;

	TraceBuffer += FString::Printf(TEXT("%.2f,%d,%.1f,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f\n"),
		ElapsedSeconds, Context ? static_cast<int32>(Context->GetRunState()) : -1, Vehicle.GetSpeedKmh(), Coord.S, Coord.D,
		Vehicle.GetTargetLaneCenterD(), FMath::RadiansToDegrees(Vehicle.GetHeadingErrorRad()), Vehicle.GetYawRateRadS(),
		Vehicle.GetAppliedSteering(), HeightM);
}

void ULRDevAutopilotSubsystem::FlushTrace()
{
	FFileHelper::SaveStringToFile(TraceBuffer, *(FPaths::ProjectSavedDir() / TEXT("DevShots/trace.csv")));
}

void ULRDevAutopilotSubsystem::TakeScreenshot()
{
	const FString FileName = FPaths::ProjectSavedDir() / FString::Printf(TEXT("DevShots/shot_%03d.png"), ScreenshotIndex++);
	FScreenshotRequest::RequestScreenshot(FileName, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
}
