// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRPlayerCameraManager.h"

#include "CoreGlobals.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LRCamera.h"
#include "LRDataLoader.h"
#include "LRDataSubsystem.h"
#include "LRGameEventSubsystem.h"
#include "LRRoadPath.h"
#include "LRRoadSubsystem.h"
#include "LRRoadTypes.h"
#include "LRVehicle.h"
#include "LRVehicleTypes.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace LRPlayerCameraManagerPrivate
{
	/** Vehicle anchor mode for a Viral mount. */
	ELRCameraMode GetAnchorMode(ELRCameraMountAnchor Anchor)
	{
		switch (Anchor)
		{
		case ELRCameraMountAnchor::Hood:
			return ELRCameraMode::Hood;
		case ELRCameraMountAnchor::Bumper:
			return ELRCameraMode::Bumper;
		case ELRCameraMountAnchor::Cockpit:
			return ELRCameraMode::Cockpit;
		case ELRCameraMountAnchor::ViralMount:
		default:
			return ELRCameraMode::Viral;
		}
	}

	/** Calls Func for the LEFTRIGHT camera manager of every player controller of the world. */
	template <typename FuncType>
	void ForEachCameraManager(const UWorld* World, FuncType&& Func)
	{
		if (!World)
		{
			return;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Controller = It->Get();
			if (ALRPlayerCameraManager* Camera = Controller ? Cast<ALRPlayerCameraManager>(Controller->PlayerCameraManager.Get()) : nullptr)
			{
				Func(*Camera);
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ViralPresetCommand(
		TEXT("lr.Camera.ViralPreset"),
		TEXT("Selects the Viral camera preset: lr.Camera.ViralPreset <GoPro|Dashcam|Phone>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ELRViralPreset Preset = ELRViralPreset::GoPro;
			if (Args.Num() != 1 || !FLRCameraMath::ParseViralPreset(Args[0], Preset))
			{
				UE_LOG(LogLRCamera, Warning, TEXT("Usage: lr.Camera.ViralPreset <GoPro|Dashcam|Phone>"));
				return;
			}
			ForEachCameraManager(World, [Preset](ALRPlayerCameraManager& Camera)
			{
				Camera.SetViralPreset(Preset);
			});
		}));

	FAutoConsoleCommandWithWorldAndArgs ViralArtifactsCommand(
		TEXT("lr.Camera.ViralArtifacts"),
		TEXT("Enables (1) or disables (0) the Viral camera post-process artifacts: lr.Camera.ViralArtifacts <0|1>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() != 1)
			{
				UE_LOG(LogLRCamera, Warning, TEXT("Usage: lr.Camera.ViralArtifacts <0|1>"));
				return;
			}
			const bool bEnabled = Args[0].ToBool();
			ForEachCameraManager(World, [bEnabled](ALRPlayerCameraManager& Camera)
			{
				Camera.SetViralArtifactsEnabled(bEnabled);
			});
		}));

	FAutoConsoleCommandWithWorldAndArgs CycleCommand(
		TEXT("lr.Camera.Cycle"),
		TEXT("Switches to the next camera mode."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ForEachCameraManager(World, [](ALRPlayerCameraManager& Camera)
			{
				Camera.CycleCameraMode();
			});
		}));
}

ALRPlayerCameraManager::ALRPlayerCameraManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Config.Sanitize();
	HeadModel.Configure(Config.Cockpit.HeadModel);
	bViralArtifactsEnabled = Config.Viral.bViralArtifactsEnabled;
	CameraMode = Config.DefaultMode;
	ViralPreset = Config.Viral.DefaultPreset;
	DefaultFOV = Config.ChaseFar.Fov.MinFovDeg;
}

void ALRPlayerCameraManager::BeginPlay()
{
	Super::BeginPlay();

	ReloadConfig();
	if (!bHasExplicitMode)
	{
		CameraMode = Config.DefaultMode;
	}
	if (!bHasExplicitPreset)
	{
		ViralPreset = Config.Viral.DefaultPreset;
	}
	ResetSmoothing();

	if (ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
	{
		RunResetHandle = Events->OnRunReset.AddUObject(this, &ALRPlayerCameraManager::HandleRunReset);
	}
	if (ULRDataSubsystem* Data = ULRDataSubsystem::Get(this))
	{
		DataReloadedHandle = Data->OnDataReloaded.AddUObject(this, &ALRPlayerCameraManager::HandleDataReloaded);
	}
}

void ALRPlayerCameraManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
	{
		Events->OnRunReset.Remove(RunResetHandle);
	}
	RunResetHandle.Reset();

	if (ULRDataSubsystem* Data = ULRDataSubsystem::Get(this))
	{
		Data->OnDataReloaded.Remove(DataReloadedHandle);
	}
	DataReloadedHandle.Reset();

	Super::EndPlay(EndPlayReason);
}

// --- Modes ---------------------------------------------------------------------------------------

void ALRPlayerCameraManager::SetCameraMode(ELRCameraMode Mode)
{
	bHasExplicitMode = true;
	if (Mode == CameraMode)
	{
		return;
	}

	CameraMode = Mode;
	ResetSmoothing();

	// The initial broadcast reports whatever mode is active by then.
	if (!bPendingInitialBroadcast)
	{
		BroadcastModeChanged();
	}
}

void ALRPlayerCameraManager::CycleCameraMode()
{
	SetCameraMode(FLRCameraMath::GetNextCameraMode(CameraMode));
}

bool ALRPlayerCameraManager::IsInteriorCamera() const
{
	return FLRCameraMath::IsInteriorMode(CameraMode, Config.Viral.GetPreset(ViralPreset).bInterior);
}

void ALRPlayerCameraManager::SetLookBack(bool bInLookBack)
{
	bLookBack = bInLookBack;
}

// --- Viral camera --------------------------------------------------------------------------------

void ALRPlayerCameraManager::SetViralPreset(ELRViralPreset Preset)
{
	bHasExplicitPreset = true;
	if (Preset == ViralPreset)
	{
		return;
	}

	ViralPreset = Preset;
	if (CameraMode == ELRCameraMode::Viral)
	{
		ResetSmoothing();
	}

	if (!bPendingInitialBroadcast && IsInteriorCamera() != bLastBroadcastInterior)
	{
		BroadcastModeChanged();
	}
}

void ALRPlayerCameraManager::SetViralArtifactsEnabled(bool bEnabled)
{
	UserArtifactsEnabled = bEnabled ? 1 : 0;
	bViralArtifactsEnabled = bEnabled;
}

// --- Settings ------------------------------------------------------------------------------------

void ALRPlayerCameraManager::SetCockpitFov(float FovDeg)
{
	UserCockpitFovDeg = FMath::Max(FovDeg, 0.f);
	ApplyUserSettings();
}

void ALRPlayerCameraManager::SetShakeScale(float Scale)
{
	UserShakeScale = FMath::Clamp(Scale, 0.f, 3.f);
	ApplyUserSettings();
}

void ALRPlayerCameraManager::SetHeadMotionIntensity(float Intensity)
{
	UserHeadIntensity = FMath::Clamp(Intensity, 0.f, 3.f);
	ApplyUserSettings();
}

void ALRPlayerCameraManager::ApplyUserSettings()
{
	if (UserCockpitFovDeg >= 0.f)
	{
		Config.Cockpit.FovDeg = FMath::Clamp(UserCockpitFovDeg, Config.Cockpit.MinFovDeg, Config.Cockpit.MaxFovDeg);
	}
	if (UserShakeScale >= 0.f)
	{
		Config.GlobalShakeScale = UserShakeScale;
	}
	if (UserHeadIntensity >= 0.f)
	{
		Config.Cockpit.HeadModel.Intensity = UserHeadIntensity;
	}
	HeadModel.Configure(Config.Cockpit.HeadModel);

	bViralArtifactsEnabled = UserArtifactsEnabled >= 0 ? UserArtifactsEnabled != 0 : Config.Viral.bViralArtifactsEnabled;
}

// --- Config --------------------------------------------------------------------------------------

void ALRPlayerCameraManager::ReloadConfig()
{
	// Start from the C++ defaults so a key removed from the file falls back to its default.
	FLRCameraConfig Loaded;
	FString Error;
	if (FLRDataLoader::Load(FLRCameraConfig::GetDefaultDataPath(), Loaded, &Error))
	{
		ApplyConfig(Loaded);
	}
	else
	{
		UE_LOG(LogLRCamera, Warning, TEXT("Keeping the current camera config, could not load %s: %s"), FLRCameraConfig::GetDefaultDataPath(), *Error);
	}
}

void ALRPlayerCameraManager::ApplyConfig(const FLRCameraConfig& InConfig)
{
	Config = InConfig;
	Config.Sanitize();
	ApplyUserSettings();
	ResetSmoothing();

	// A reload can change the interior flag of the active preset.
	if (!bPendingInitialBroadcast && IsInteriorCamera() != bLastBroadcastInterior)
	{
		BroadcastModeChanged();
	}
}

void ALRPlayerCameraManager::ResetSmoothing()
{
	bSnapNextUpdate = true;
	CameraRoadHint = INDEX_NONE;
}

// --- Events --------------------------------------------------------------------------------------

void ALRPlayerCameraManager::BroadcastModeChanged()
{
	bLastBroadcastInterior = IsInteriorCamera();

	if (const ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
	{
		FLRCameraModeChangedEvent Event;
		Event.Mode = CameraMode;
		Event.bInterior = bLastBroadcastInterior;
		Events->BroadcastCameraModeChanged(Event);
	}
}

void ALRPlayerCameraManager::HandleRunReset(const FLRRunResetEvent& Event)
{
	ShakeTimeSeconds = 0.0;
	ResetSmoothing();
}

void ALRPlayerCameraManager::HandleDataReloaded()
{
	ReloadConfig();
}

// --- View ----------------------------------------------------------------------------------------

void ALRPlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALRPlayerCameraManager::UpdateViewTarget);

	// InitializeFor() runs a first update with a zero step before BeginPlay; listeners are bound by the first real one.
	if (bPendingInitialBroadcast && DeltaTime > 0.f && HasActorBegunPlay())
	{
		bPendingInitialBroadcast = false;
		BroadcastModeChanged();
	}

	const ALRVehicle* Vehicle = Cast<ALRVehicle>(OutVT.Target.Get());
	if (!Vehicle)
	{
		Super::UpdateViewTarget(OutVT, DeltaTime);
		return;
	}

	// Same contract as APlayerCameraManager::UpdateViewTarget: the outgoing target can be frozen during a blend.
	if (PendingViewTarget.Target != nullptr && BlendParams.bLockOutgoing && OutVT.Equal(ViewTarget))
	{
		return;
	}

	static const FMinimalViewInfo DefaultViewInfo;
	OutVT.POV = DefaultViewInfo;
	OutVT.POV.FOV = DefaultFOV;
	OutVT.POV.AspectRatio = DefaultAspectRatio;
	OutVT.POV.bConstrainAspectRatio = bDefaultConstrainAspectRatio;
	OutVT.POV.ProjectionMode = ECameraProjectionMode::Perspective;
	OutVT.POV.PostProcessBlendWeight = 1.f;

	UpdateVehicleView(*Vehicle, OutVT.POV, DeltaTime);

	ApplyCameraModifiers(DeltaTime, OutVT.POV);
	SetActorLocationAndRotation(OutVT.POV.Location, OutVT.POV.Rotation, false);
	UpdateCameraLensEffects(OutVT);
}

void ALRPlayerCameraManager::UpdateVehicleView(const ALRVehicle& Vehicle, FMinimalViewInfo& OutPOV, float DeltaTime)
{
	// Snap instead of swinging across the map when the car was replaced or teleported.
	const FVector VehicleLocation = Vehicle.GetActorLocation();
	const double TeleportDistance = Config.TeleportSnapDistanceCm;
	if (LastVehicle.Get() != &Vehicle || FVector::DistSquared(VehicleLocation, LastVehicleLocationCm) > FMath::Square(TeleportDistance))
	{
		bSnapNextUpdate = true;
	}
	LastVehicle = &Vehicle;
	LastVehicleLocationCm = VehicleLocation;

	// The view target can be evaluated twice per frame during blends: advance the state only once.
	const float StepSeconds = LastAdvanceFrame == GFrameCounter ? 0.f : FMath::Max(DeltaTime, 0.f);
	LastAdvanceFrame = GFrameCounter;
	ShakeTimeSeconds += StepSeconds;

	FrameSpeedKmh = Vehicle.GetSpeedKmh();
	const float TargetRoughness = ComputeRoadRoughness(Vehicle);
	if (bSnapNextUpdate)
	{
		RoughnessSpring.Reset(TargetRoughness);
	}
	FrameRoughness = RoughnessSpring.Update(TargetRoughness, Config.RoughnessLagSeconds, StepSeconds);

	switch (CameraMode)
	{
	case ELRCameraMode::ChaseNear:
		UpdateChaseView(Vehicle, Config.ChaseNear, true, OutPOV, StepSeconds);
		break;
	case ELRCameraMode::Hood:
		UpdateMountedView(Vehicle, Config.Hood, ELRCameraMode::Hood, OutPOV);
		break;
	case ELRCameraMode::Bumper:
		UpdateMountedView(Vehicle, Config.Bumper, ELRCameraMode::Bumper, OutPOV);
		break;
	case ELRCameraMode::Cockpit:
		UpdateCockpitView(Vehicle, OutPOV, StepSeconds);
		break;
	case ELRCameraMode::Viral:
		UpdateViralView(Vehicle, OutPOV);
		break;
	case ELRCameraMode::ChaseFar:
	default:
		UpdateChaseView(Vehicle, Config.ChaseFar, false, OutPOV, StepSeconds);
		break;
	}

	bSnapNextUpdate = false;
}

void ALRPlayerCameraManager::UpdateChaseView(const ALRVehicle& Vehicle, const FLRChaseCameraParams& Params, bool bNear, FMinimalViewInfo& OutPOV, float DeltaTime)
{
	const FLRCameraAnchors& Anchors = Vehicle.GetDefinition().Cameras;
	const FVector VehicleLocation = Vehicle.GetActorLocation();
	const float VehicleYaw = static_cast<float>(FRotator::NormalizeAxis(Vehicle.GetActorRotation().Yaw));

	// Arm length: definition distance + speed stretch + clamped acceleration stretch.
	const float BaseDistanceCm = (bNear ? Anchors.ChaseNearDistanceCm : Anchors.ChaseFarDistanceCm) * Params.DistanceScale;
	const float SpeedStretchCm = Params.SpeedDistanceCmPer100Kmh * FrameSpeedKmh * 0.01f;
	const float AccelStretchCm = FMath::Clamp(
		static_cast<float>(Vehicle.GetLocalAccelerationMs2().X) * Params.AccelDistanceCmPerMs2,
		-Params.MaxAccelDistanceCm, Params.MaxAccelDistanceCm);
	const float TargetDistanceCm = BaseDistanceCm + SpeedStretchCm + AccelStretchCm;

	if (bSnapNextUpdate)
	{
		ChaseYawSpring.Reset(VehicleYaw);
		ChaseDistanceSpring.Reset(TargetDistanceCm);
		ChasePivotZSpring.Reset(VehicleLocation.Z);
	}
	const float ArmYawDeg = ChaseYawSpring.UpdateAngleDegrees(VehicleYaw, Params.YawLagSeconds, DeltaTime);
	const float DistanceCm = ChaseDistanceSpring.Update(TargetDistanceCm, Params.DistanceLagSeconds, DeltaTime);
	const double PivotZ = ChasePivotZSpring.Update(VehicleLocation.Z, Params.HeightLagSeconds, DeltaTime);
	const float LookAheadYawDeg = UpdateLookAhead(Vehicle, Params.LookAhead, DeltaTime);

	// Looking back swings the arm in front of the car.
	const double ViewSign = bLookBack ? -1.0 : 1.0;
	const FVector ViewForward = FRotator(0.0, static_cast<double>(ArmYawDeg), 0.0).Vector() * ViewSign;
	const FVector Pivot(VehicleLocation.X, VehicleLocation.Y, PivotZ);
	const double HeightCm = (bNear ? Anchors.ChaseNearHeightCm : Anchors.ChaseFarHeightCm) * Params.HeightScale;

	FVector CameraLocation = Pivot - ViewForward * static_cast<double>(DistanceCm) + FVector::UpVector * HeightCm;

	// Ground clearance guard: on crests and under the car after a crash the arm must stay above the asphalt.
	double SurfaceZ = 0.0;
	if (GetRoadSurfaceZ(CameraLocation, SurfaceZ))
	{
		CameraLocation.Z = FMath::Max(CameraLocation.Z, SurfaceZ + Params.MinGroundClearanceCm);
	}

	const FVector LookAt = Pivot
		+ ViewForward * static_cast<double>(Params.LookAtAheadCm)
		+ FVector::UpVector * static_cast<double>(Anchors.ChaseLookAtHeightCm * Params.LookAtHeightScale);

	// The rotation built from a direction has no roll: the chase horizon is always level.
	FRotator ViewRotation = (LookAt - CameraLocation).Rotation();
	if (!bLookBack)
	{
		ViewRotation.Yaw += LookAheadYawDeg;
	}

	OutPOV.Location = CameraLocation;
	OutPOV.Rotation = ViewRotation;
	OutPOV.FOV = FLRCameraMath::EvaluateSpeedFov(Params.Fov, FrameSpeedKmh);
	ApplyShake(SampleShake(Params.Shake), OutPOV);
}

void ALRPlayerCameraManager::UpdateMountedView(const ALRVehicle& Vehicle, const FLRMountedCameraParams& Params, ELRCameraMode Mode, FMinimalViewInfo& OutPOV)
{
	const FTransform Anchor = Vehicle.GetCameraAnchorWorld(Mode);
	const FRotator MountRotation(static_cast<double>(Params.PitchOffsetDeg), bLookBack ? 180.0 : 0.0, 0.0);

	OutPOV.Location = Anchor.GetLocation();
	OutPOV.Rotation = (Anchor.GetRotation() * MountRotation.Quaternion()).Rotator();
	OutPOV.FOV = FLRCameraMath::EvaluateSpeedFov(Params.Fov, FrameSpeedKmh);
	ApplyShake(SampleShake(Params.Shake), OutPOV);
}

void ALRPlayerCameraManager::UpdateCockpitView(const ALRVehicle& Vehicle, FMinimalViewInfo& OutPOV, float DeltaTime)
{
	const FLRCockpitCameraParams& Params = Config.Cockpit;

	if (bSnapNextUpdate)
	{
		HeadModel.Reset();
	}
	else
	{
		HeadModel.Update(Vehicle.GetLocalAccelerationMs2(), DeltaTime);
	}
	const float LookAheadYawDeg = UpdateLookAhead(Vehicle, Params.LookAhead, DeltaTime);

	// The eye anchor follows the body; the view keeps only part of the body pitch and roll (stable horizon).
	const FTransform Anchor = Vehicle.GetCameraAnchorWorld(ELRCameraMode::Cockpit);
	const FRotator Body = FLRCameraMath::StabilizeHorizon(Vehicle.GetActorRotation(), Params.HorizonStabilization);
	const FRotator Head(
		static_cast<double>(HeadModel.GetPitchDeg()),
		bLookBack ? 180.0 : static_cast<double>(LookAheadYawDeg),
		static_cast<double>(HeadModel.GetRollDeg()));

	OutPOV.Location = Anchor.GetLocation() + Anchor.GetRotation().RotateVector(HeadModel.GetOffsetCm());
	OutPOV.Rotation = (Body.Quaternion() * Head.Quaternion()).Rotator();
	OutPOV.FOV = FLRCameraMath::EvaluateCockpitFov(Params, FrameSpeedKmh);
	OutPOV.PerspectiveNearClipPlane = Config.InteriorNearClipCm;
	ApplyShake(SampleShake(Params.Shake), OutPOV);
}

void ALRPlayerCameraManager::UpdateViralView(const ALRVehicle& Vehicle, FMinimalViewInfo& OutPOV)
{
	const FLRViralPresetParams& Preset = Config.Viral.GetPreset(ViralPreset);

	const FTransform Anchor = Vehicle.GetCameraAnchorWorld(LRPlayerCameraManagerPrivate::GetAnchorMode(Preset.MountAnchor));
	const FRotator Body = FLRCameraMath::StabilizeHorizon(Vehicle.GetActorRotation(), Preset.HorizonStabilization);
	const FRotator Mount(static_cast<double>(Preset.MountPitchDeg), bLookBack ? 180.0 : 0.0, 0.0);

	OutPOV.Location = Anchor.GetLocation() + Anchor.GetRotation().RotateVector(Preset.MountOffsetCm);
	OutPOV.Rotation = (Body.Quaternion() * Mount.Quaternion()).Rotator();
	OutPOV.FOV = Preset.FovDeg;
	if (Preset.bInterior)
	{
		OutPOV.PerspectiveNearClipPlane = Config.InteriorNearClipCm;
	}
	ApplyShake(SampleShake(Preset.Shake), OutPOV);

	// OutPOV starts from a default view every frame, so the overrides disappear with the mode or the accessibility flag.
	if (bViralArtifactsEnabled)
	{
		FLRCameraMath::ApplyViralPostProcess(Preset.PostProcess, OutPOV.PostProcessSettings);
	}
}

float ALRPlayerCameraManager::UpdateLookAhead(const ALRVehicle& Vehicle, const FLRLookAheadParams& Params, float DeltaTime)
{
	float LateralErrorMeters = 0.f;
	float CurvaturePerM = 0.f;

	const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	if (Road && Road->HasRoad())
	{
		const FLRRoadCoord Coord = Vehicle.GetRoadCoord();
		CurvaturePerM = Road->GetPath().GetCurvature(Coord.S);

		// Only the Arcade scheme has a real target lane; in Simcade it just mirrors the lane the car happens to be in.
		const int32 TargetLane = Vehicle.GetTargetLane();
		if (Vehicle.GetControlScheme() == ELRControlScheme::Arcade && !Vehicle.IsWrecked()
			&& TargetLane >= 0 && TargetLane < Road->GetNumLanes())
		{
			LateralErrorMeters = Road->GetLaneCenterD(TargetLane) - Coord.D;
		}
	}

	const float TargetYawDeg = FLRCameraMath::ComputeLookAheadYawDeg(Params, LateralErrorMeters, Vehicle.GetForwardSpeedMs(), CurvaturePerM);
	if (bSnapNextUpdate)
	{
		LookAheadSpring.Reset(TargetYawDeg);
	}
	return LookAheadSpring.Update(TargetYawDeg, Params.LagSeconds, DeltaTime);
}

FLRCameraShakeSample ALRPlayerCameraManager::SampleShake(const FLRCameraShakeParams& Params) const
{
	const float Scale = FLRCameraMath::ComputeShakeScale(Params, FrameSpeedKmh, FrameRoughness, Config.GlobalShakeScale);
	return FLRCameraMath::EvaluateShake(Params, ShakeTimeSeconds, Scale);
}

void ALRPlayerCameraManager::ApplyShake(const FLRCameraShakeSample& Shake, FMinimalViewInfo& InOutPOV)
{
	const FQuat ViewRotation = InOutPOV.Rotation.Quaternion();
	InOutPOV.Location += ViewRotation.RotateVector(Shake.LocationCm);
	InOutPOV.Rotation = (ViewRotation * Shake.Rotation.Quaternion()).Rotator();
}

float ALRPlayerCameraManager::ComputeRoadRoughness(const ALRVehicle& Vehicle) const
{
	const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	if (!Road || !Road->HasRoad())
	{
		return Config.RoadRoughness;
	}

	const float D = Vehicle.GetRoadCoord().D;
	const int32 Lane = Road->FindNearestLane(D);
	if (Lane == INDEX_NONE)
	{
		return Config.RoadRoughness;
	}

	const FLRLaneDesc& Desc = Road->GetLane(Lane);
	return FMath::Abs(D - Desc.CenterD) > Desc.WidthMeters * 0.5f ? Config.ShoulderRoughness : Config.RoadRoughness;
}

bool ALRPlayerCameraManager::GetRoadSurfaceZ(const FVector& WorldLocationCm, double& OutZ) const
{
	const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	if (!Road || !Road->HasRoad())
	{
		return false;
	}

	FLRRoadCoord Coord;
	if (!Road->WorldToRoad(WorldLocationCm, Coord, &CameraRoadHint))
	{
		return false;
	}

	OutZ = Road->GetPath().RoadToWorldLocation(Coord.S, Coord.D).Z;
	return true;
}
