// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LRTrafficTypes.h"

#include "LRDevAutopilotSubsystem.generated.h"

class ALRVehicle;

/**
 * Development-only test driver: plays the real game without human input.
 *
 * It is inert unless the command line contains -LRDevDrive, and it is never created in Shipping builds.
 * While active it holds full throttle, weaves into the freest lane, restarts after each crash, logs telemetry,
 * and takes engine screenshots into Saved/DevShots. It drives the vehicle through the same public API as the
 * player controller, so what it exercises is what a player gets.
 *
 * Command line:
 *   -LRDevDrive               enable the autopilot
 *   -LRDevSeconds=<s>         quit the game after this time (default 120)
 *   -LRDevShotEvery=<s>       screenshot period, 0 disables (default 10)
 *   -LRDevCycleCameras        switch to the next camera mode at every screenshot
 *   -LRDevNoSteer             hold the lane instead of weaving
 *   -LRDevNoTraffic           empty road (isolates vehicle behavior)
 *   -LRDevLaneSweep           change lane on a fixed schedule instead of avoiding traffic
 *   -LRDevExecAt=<s> -LRDevExec=<cmd>   run a console command at this time; '+' stands for a space,
 *                             e.g. -LRDevExec=ProfileGPU or -LRDevExec=stat+dumpframe+-ms=0.3
 *   -LRDevShowroom=a+b        park each listed vehicle and photograph it from several angles, then quit
 */
UCLASS()
class LRGAMEPLAY_API ULRDevAutopilotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ UWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	/** Picks a lane change direction (-1, 0, +1) toward the lane with the largest free gap ahead. */
	int32 ChooseLaneChange(const ALRVehicle& Vehicle) const;

	/** Writes one telemetry line and updates the frame time statistics. */
	void LogTelemetry(const ALRVehicle& Vehicle);

	void TakeScreenshot();

	/** Advances the showroom mode: one vehicle at a time, a set of fixed camera positions around it. */
	void TickShowroom(float DeltaTime);

	/** Appends one sample of the lane assist trace (Saved/DevShots/trace.csv). */
	void SampleTrace(const ALRVehicle& Vehicle);

	/** Writes the buffered trace to disk. */
	void FlushTrace();

	double ElapsedSeconds = 0.0;
	double NextTelemetryTime = 0.0;
	double NextScreenshotTime = 0.0;
	double NextLaneDecisionTime = 0.0;
	double NextCameraCycleTime = -1.0;
	double QuitAfterSeconds = 120.0;
	double ScreenshotPeriodSeconds = 10.0;
	double RestartAtTime = -1.0;

	/** Frame statistics since the previous telemetry line. */
	double AccumulatedFrameSeconds = 0.0;
	int32 AccumulatedFrames = 0;

	int32 ScreenshotIndex = 0;
	bool bCycleCameras = false;
	bool bSteer = true;
	bool bQuitRequested = false;
	bool bNoTraffic = false;
	bool bTrafficCleared = false;
	bool bLaneSweep = false;
	int32 SweepDirection = 1;
	double NextTraceTime = 0.0;
	double ExecAtSeconds = -1.0;
	FString ExecCommand;

	/** Showroom state: vehicles still to photograph, current camera index, time until the next step. */
	TArray<FString> ShowroomVehicles;
	int32 ShowroomVehicleIndex = INDEX_NONE;
	int32 ShowroomCameraIndex = 0;
	double ShowroomNextStepTime = 0.0;
	bool bShowroomShotPending = false;

	UPROPERTY(Transient)
	TObjectPtr<AActor> ShowroomCamera;
	FString TraceBuffer;

	/** Reused every decision to avoid per-tick allocations. */
	mutable TArray<FLRTrafficVehicleSnapshot> SnapshotScratch;
};
