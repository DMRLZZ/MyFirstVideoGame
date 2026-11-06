// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRVehicle.h"
#include "Engine/OverlapResult.h"
#include "ChaosVehicleWheel.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"

#include "Animation/AnimInstance.h"
#include "Assist/LRLaneAssist.h"
#include "Assist/LRStabilityAssist.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "LRDataLoader.h"
#include "LRGameEventSubsystem.h"
#include "LRRoadPath.h"
#include "LRRoadSubsystem.h"
#include "LRRoadTypes.h"
#include "LRUnits.h"
#include "LRVehicleMath.h"
#include "LRVehicleMovementComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace LRVehiclePrivate
{
	/** Pedal travel below which a pedal counts as released for the reverse logic. */
	constexpr float PedalThreshold = 0.05f;

	/** Below this speed the side slip is meaningless and reported as 0, cm/s. */
	constexpr double SideSlipMinSpeedCms = 200.0;

	/** Engine cube used as the fallback body (100 cm edge). */
	const TCHAR* FallbackMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/** Height of the fallback box, centimeters. */
	constexpr double FallbackHeightCm = 130.0;
}

ALRVehicle::ALRVehicle(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<ULRVehicleMovementComponent>(AWheeledVehiclePawn::VehicleMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->BodyInstance.bNotifyRigidBodyCollision = true;
	}

	FallbackBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FallbackBody"));
	FallbackBody->SetupAttachment(GetMesh());
	FallbackBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FallbackBody->SetGenerateOverlapEvents(false);
	FallbackBody->SetCanEverAffectNavigation(false);
	FallbackBody->SetVisibility(false);

	const auto CreateVisual = [this](const FName Name)
	{
		UStaticMeshComponent* Visual = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Visual->SetupAttachment(GetMesh());
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visual->SetGenerateOverlapEvents(false);
		Visual->SetCanEverAffectNavigation(false);
		return Visual;
	};
	BodyVisual = CreateVisual(TEXT("BodyVisual"));
	GlassVisual = CreateVisual(TEXT("GlassVisual"));
}

ULRVehicleMovementComponent* ALRVehicle::GetLRMovement() const
{
	return Cast<ULRVehicleMovementComponent>(GetVehicleMovementComponent());
}

void ALRVehicle::BeginPlay()
{
	Super::BeginPlay();

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetNotifyRigidBodyCollision(true);
		MeshComp->OnComponentHit.AddDynamic(this, &ALRVehicle::HandleMeshHit);
	}

	// Note: the engine makes a pawn tick AFTER its movement component, so inputs written in this actor's tick
	// are consumed on the next frame. Do not add the reverse prerequisite: it forms a tick cycle.

	if (!bHasDefinition && !DefaultVehicleId.IsNone())
	{
		LoadDefinitionById(DefaultVehicleId);
	}
}

void ALRVehicle::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->OnComponentHit.RemoveDynamic(this, &ALRVehicle::HandleMeshHit);
	}
	Super::EndPlay(EndPlayReason);
}

// --- Input API ---------------------------------------------------------------------------------

void ALRVehicle::SetThrottleInput(float Value01)
{
	ThrottleInput = FMath::Clamp(Value01, 0.f, 1.f);
}

void ALRVehicle::SetBrakeInput(float Value01)
{
	BrakeInput = FMath::Clamp(Value01, 0.f, 1.f);
}

void ALRVehicle::SetSteeringInput(float ValueMinus1To1)
{
	SteeringInput = FMath::Clamp(ValueMinus1To1, -1.f, 1.f);
}

void ALRVehicle::SetHandbrakeInput(bool bEngaged)
{
	bHandbrakeInput = bEngaged;
}

void ALRVehicle::RequestLaneChange(int32 Direction)
{
	if (ControlScheme != ELRControlScheme::Arcade || !bInputEnabled || bWrecked || Direction == 0)
	{
		return;
	}

	const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	if (Road == nullptr || !Road->HasRoad())
	{
		return;
	}

	const TArray<FLRLaneDesc>& Lanes = Road->GetPath().GetLanes();
	int32 From = Lanes.IsValidIndex(TargetLane) ? TargetLane : CurrentLane;
	if (!Lanes.IsValidIndex(From))
	{
		From = LRVehicleMath::FindNearestLane(Lanes, CachedRoadCoord.D);
	}

	TargetLane = LRVehicleMath::StepTargetLane(Lanes, From, Direction, AllowsOncomingLanes(*Road));
	if (Lanes.IsValidIndex(TargetLane))
	{
		TargetLaneCenterD = Lanes[TargetLane].CenterD;
	}
}

void ALRVehicle::SetControlScheme(ELRControlScheme Scheme)
{
	ControlScheme = Scheme;
	TargetLane = CurrentLane;
}

void ALRVehicle::SetInputEnabled(bool bEnabled)
{
	bInputEnabled = bEnabled;
}

void ALRVehicle::ShiftGearUp()
{
	if (ULRVehicleMovementComponent* Movement = GetLRMovement())
	{
		const int32 NumGears = FMath::Max(Definition.Transmission.ForwardGearRatios.Num(), 1);
		Movement->SetTargetGear(FMath::Min(Movement->GetTargetGear() + 1, NumGears), false);
	}
}

void ALRVehicle::ShiftGearDown()
{
	if (ULRVehicleMovementComponent* Movement = GetLRMovement())
	{
		Movement->SetTargetGear(FMath::Max(Movement->GetTargetGear() - 1, -1), false);
	}
}

// --- State -------------------------------------------------------------------------------------

float ALRVehicle::GetSpeedKmh() const
{
	return static_cast<float>(LRUnits::CmsToKmh(LastVelocityCms.Size()));
}

float ALRVehicle::GetForwardSpeedMs() const
{
	return ForwardSpeedMs;
}

float ALRVehicle::GetEngineRpm() const
{
	const UChaosWheeledVehicleMovementComponent* Movement = GetLRMovement();
	return Movement ? Movement->GetEngineRotationSpeed() : 0.f;
}

float ALRVehicle::GetEngineMaxRpm() const
{
	const UChaosWheeledVehicleMovementComponent* Movement = GetLRMovement();
	return Movement ? Movement->GetEngineMaxRotationSpeed() : Definition.Engine.MaxRpm;
}

int32 ALRVehicle::GetCurrentGear() const
{
	const UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent();
	return Movement ? Movement->GetCurrentGear() : 0;
}

void ALRVehicle::SetWrecked(bool bInWrecked)
{
	bWrecked = bInWrecked;
	if (bWrecked)
	{
		bReverseEngaged = false;
		ClearInputs();
	}
}

FVector2D ALRVehicle::GetFootprintSizeMeters() const
{
	return FVector2D(Definition.LengthMeters, Definition.WidthMeters);
}

// --- Setup -------------------------------------------------------------------------------------

bool ALRVehicle::LoadDefinitionById(FName VehicleId)
{
	if (VehicleId.IsNone())
	{
		return false;
	}

	const FString RelativePath = FString::Printf(TEXT("Vehicles/%s.json"), *VehicleId.ToString());
	FLRVehicleDefinition Loaded;
	FString Error;
	if (!FLRDataLoader::Load(RelativePath, Loaded, &Error))
	{
		UE_LOG(LogLRVehicle, Error, TEXT("Cannot load vehicle definition %s: %s"), *RelativePath, *Error);
		return false;
	}

	if (Loaded.Id.IsNone())
	{
		Loaded.Id = VehicleId;
	}
	else if (Loaded.Id != VehicleId)
	{
		UE_LOG(LogLRVehicle, Warning, TEXT("%s declares Id '%s' (file name wins)"), *RelativePath, *Loaded.Id.ToString());
		Loaded.Id = VehicleId;
	}

	TArray<FString> Problems;
	if (!LRVehicleMath::ValidateDefinition(Loaded, Problems))
	{
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogLRVehicle, Warning, TEXT("%s: %s"), *RelativePath, *Problem);
		}
	}

	ApplyDefinition(Loaded);
	return true;
}

void ALRVehicle::ApplyDefinition(const FLRVehicleDefinition& InDefinition)
{
	Definition = InDefinition;
	bHasDefinition = true;

	USkeletalMeshComponent* MeshComp = GetMesh();
	if (MeshComp != nullptr)
	{
		USkeletalMesh* LoadedMesh = nullptr;
		if (!Definition.SkeletalMesh.IsNull())
		{
			LoadedMesh = Cast<USkeletalMesh>(Definition.SkeletalMesh.TryLoad());
		}

		if (LoadedMesh != nullptr)
		{
			if (MeshComp->GetSkeletalMeshAsset() != LoadedMesh)
			{
				MeshComp->SetSkeletalMeshAsset(LoadedMesh);
			}
			SetFallbackBodyVisible(false);

			// AWheeledVehiclePawn creates the mesh with physics off (the engine template enables it in its Blueprint).
			// The Chaos vehicle only drives a simulating body.
			MeshComp->SetSimulatePhysics(true);
		}
		else
		{
			UE_LOG(LogLRVehicle, Error, TEXT("Vehicle '%s': cannot load skeletal mesh '%s'. Showing the fallback body; Chaos physics needs a skeletal mesh with a physics asset."),
				*Definition.Id.ToString(), *Definition.SkeletalMesh.ToString());
			SetFallbackBodyVisible(MeshComp->GetSkeletalMeshAsset() == nullptr);
		}

		if (!Definition.AnimClass.IsNull())
		{
			if (UClass* AnimClass = Definition.AnimClass.TryLoadClass<UAnimInstance>())
			{
				MeshComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
				MeshComp->SetAnimInstanceClass(AnimClass);
			}
			else
			{
				UE_LOG(LogLRVehicle, Warning, TEXT("Vehicle '%s': cannot load animation class '%s' (wheels will not animate)"),
					*Definition.Id.ToString(), *Definition.AnimClass.ToString());
			}
		}

		MeshComp->SetNotifyRigidBodyCollision(true);
	}

	ApplyVisualMeshes();

	if (ULRVehicleMovementComponent* Movement = GetLRMovement())
	{
		// Each car has its own wheelbase and track: move the physics wheels from the rig bones to where the design has them.
		TArray<FVector> Offsets;
		if (MeshComp != nullptr && MeshComp->GetSkeletalMeshAsset() != nullptr)
		{
			const auto AddOffsets = [&Offsets, MeshComp](const TArray<FName>& Bones, const FVector& Axle)
			{
				for (const FName& Bone : Bones)
				{
					const FVector Rig = MeshComp->GetSocketTransform(Bone, RTS_Component).GetLocation();
					const double Side = Bone.ToString().EndsWith(TEXT("L")) ? -1.0 : 1.0;
					Offsets.Add(FVector(Axle.X - Rig.X, Side * Axle.Y - Rig.Y, 0.0));
				}
			};
			AddOffsets(Definition.FrontWheels.BoneNames, Definition.FrontWheelCm);
			AddOffsets(Definition.RearWheels.BoneNames, Definition.RearWheelCm);
		}
		Movement->SetWheelOffsets(Offsets);
		Movement->ApplyDefinition(Definition);
	}

	UE_LOG(LogLRVehicle, Log, TEXT("Vehicle definition '%s' (%s) applied"), *Definition.Id.ToString(), *Definition.DisplayName);
}

void ALRVehicle::ApplyVisualMeshes()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (MeshComp == nullptr)
	{
		return;
	}

	const auto LoadMesh = [](const FSoftObjectPath& Path) -> UStaticMesh*
	{
		return Path.IsNull() ? nullptr : Cast<UStaticMesh>(Path.TryLoad());
	};

	UStaticMesh* Body = LoadMesh(Definition.BodyMesh);
	if (!Definition.BodyMesh.IsNull() && Body == nullptr)
	{
		UE_LOG(LogLRVehicle, Error, TEXT("Vehicle '%s': cannot load body mesh '%s'"), *Definition.Id.ToString(), *Definition.BodyMesh.ToString());
	}
	BodyVisual->SetStaticMesh(Body);
	BodyVisual->SetRelativeLocation(Definition.VisualOffsetCm);
	if (Body != nullptr)
	{
		const int32 PaintSlot = BodyVisual->GetMaterialIndex(TEXT("Paint"));
		if (PaintSlot != INDEX_NONE)
		{
			if (UMaterialInstanceDynamic* Paint = BodyVisual->CreateDynamicMaterialInstance(PaintSlot))
			{
				Paint->SetVectorParameterValue(TEXT("PaintColor"), Definition.PaintColor);
			}
		}
	}
	GlassVisual->SetStaticMesh(LoadMesh(Definition.GlassMesh));
	GlassVisual->SetRelativeLocation(Definition.VisualOffsetCm);

	// With a visual body the skeleton is only the physics rig. The visual wheels are placed from the simulation.
	MeshComp->SetVisibility(Body == nullptr, /*bPropagateToChildren*/ false);

	for (UStaticMeshComponent* Wheel : WheelVisuals)
	{
		if (Wheel != nullptr)
		{
			Wheel->DestroyComponent();
		}
	}
	WheelVisuals.Reset();

	UStaticMesh* WheelMesh = LoadMesh(Definition.WheelMesh);
	if (WheelMesh == nullptr)
	{
		return;
	}

	const int32 NumWheels = Definition.FrontWheels.BoneNames.Num() + Definition.RearWheels.BoneNames.Num();
	for (int32 Index = 0; Index < NumWheels; ++Index)
	{
		UStaticMeshComponent* Wheel = NewObject<UStaticMeshComponent>(this);
		Wheel->SetStaticMesh(WheelMesh);
		Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wheel->SetGenerateOverlapEvents(false);
		Wheel->SetCanEverAffectNavigation(false);
		Wheel->SetupAttachment(MeshComp);
		Wheel->RegisterComponent();
		WheelVisuals.Add(Wheel);
	}
	UpdateVisualWheels();
}

void ALRVehicle::UpdateVisualWheels()
{
	const ULRVehicleMovementComponent* Movement = GetLRMovement();
	const int32 NumFront = Definition.FrontWheels.BoneNames.Num();
	for (int32 Index = 0; Index < WheelVisuals.Num(); ++Index)
	{
		UStaticMeshComponent* Wheel = WheelVisuals[Index];
		const bool bFront = Index < NumFront;
		const TArray<FName>& Bones = bFront ? Definition.FrontWheels.BoneNames : Definition.RearWheels.BoneNames;
		const FName Bone = Bones[bFront ? Index : Index - NumFront];
		// Bone names end in L / R (Phys_Wheel_FL). The mesh has its outer face on +Y, as a right wheel.
		const bool bLeft = Bone.ToString().EndsWith(TEXT("L"));
		const FVector Axle = bFront ? Definition.FrontWheelCm : Definition.RearWheelCm;

		float SteerDeg = 0.f;
		float SpinDeg = 0.f;
		float SuspensionCm = 0.f;
		if (Movement != nullptr && Movement->Wheels.IsValidIndex(Index) && Movement->Wheels[Index] != nullptr && Movement->HasPhysicsVehicle())
		{
			const UChaosVehicleWheel* Sim = Movement->Wheels[Index];
			SteerDeg = Sim->GetSteerAngle();
			SpinDeg = Sim->GetRotationAngle();
			SuspensionCm = Sim->GetSuspensionOffset();
		}

		const FVector Location(Axle.X, bLeft ? -Axle.Y : Axle.Y, Definition.VisualOffsetCm.Z + Axle.Z + SuspensionCm);
		// Spin around the axle, flip the left wheels so the rim faces out, then steer.
		const FQuat Spin(FVector::RightVector, FMath::DegreesToRadians(-SpinDeg));
		const FQuat Flip = bLeft ? FQuat(FVector::UpVector, UE_PI) : FQuat::Identity;
		const FQuat Steer(FVector::UpVector, FMath::DegreesToRadians(SteerDeg));
		Wheel->SetRelativeLocationAndRotation(Location, Steer * Spin * Flip);
		Wheel->SetRelativeScale3D(FVector(Axle.Z / FMath::Max(Definition.WheelMeshRadiusCm, 1.f)));
	}
}

void ALRVehicle::SetFallbackBodyVisible(bool bVisible)
{
	if (FallbackBody == nullptr)
	{
		return;
	}

	if (bVisible && FallbackBody->GetStaticMesh() == nullptr)
	{
		if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, LRVehiclePrivate::FallbackMeshPath))
		{
			FallbackBody->SetStaticMesh(Cube);
		}
	}

	if (bVisible)
	{
		// The engine cube has a 100 cm edge.
		FallbackBody->SetRelativeScale3D(FVector(Definition.LengthMeters, Definition.WidthMeters, LRVehiclePrivate::FallbackHeightCm / 100.0));
		FallbackBody->SetRelativeLocation(FVector(0.0, 0.0, LRVehiclePrivate::FallbackHeightCm * 0.5));
	}
	FallbackBody->SetVisibility(bVisible);
}

void ALRVehicle::ClearInputs()
{
	ThrottleInput = 0.f;
	BrakeInput = 0.f;
	SteeringInput = 0.f;
	bHandbrakeInput = false;
	AppliedThrottle = 0.f;
	AppliedSteering = 0.f;

	if (UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent())
	{
		Movement->SetThrottleInput(0.f);
		Movement->SetBrakeInput(0.f);
		Movement->SetSteeringInput(0.f);
		Movement->SetHandbrakeInput(false);
	}
}

void ALRVehicle::ResetForRun(double S, float D, float InitialSpeedKmh)
{
	ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this);
	if (Road == nullptr || !Road->HasRoad())
	{
		UE_LOG(LogLRVehicle, Warning, TEXT("ResetForRun: no road available, vehicle not moved"));
		return;
	}

	const FTransform RoadTransform = Road->RoadToWorld(S, D);
	const FQuat Rotation = RoadTransform.GetRotation();
	const FVector Location = RoadTransform.GetLocation() + Rotation.GetUpVector() * Definition.SpawnHeightOffsetCm;
	SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::ResetPhysics);

	bWrecked = false;
	bOffRoadReported = false;
	bReverseEngaged = false;
	ClearInputs();

	// ResetVehicle rebuilds the Chaos vehicle and stops the body, so it must run BEFORE the initial velocity is set.
	ULRVehicleMovementComponent* Movement = GetLRMovement();
	if (Movement != nullptr)
	{
		Movement->ResetVehicle();
	}

	const float SpeedMs = FMath::Max(static_cast<float>(LRUnits::KmhToMs(InitialSpeedKmh)), 0.f);
	const FVector VelocityCms = Rotation.GetForwardVector() * LRUnits::MetersToCm(SpeedMs);
	if (USkeletalMeshComponent* MeshComp = GetMesh(); MeshComp != nullptr && MeshComp->IsSimulatingPhysics())
	{
		MeshComp->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		MeshComp->SetAllPhysicsLinearVelocity(VelocityCms);
	}

	if (Movement != nullptr)
	{
		const int32 Gear = LRVehicleMath::SelectGearForSpeed(Definition.Transmission, Definition.RearWheels.RadiusCm, SpeedMs);
		Movement->SetTargetGear(Gear, true);
		Movement->MatchWheelSpeeds(SpeedMs);
	}

	// Road state.
	RoadSegmentHint = INDEX_NONE;
	CachedRoadCoord = FLRRoadCoord(S, D);
	bHasRoadCoord = true;
	HeadingErrorRad = 0.f;
	RoadCurvaturePerM = Road->GetPath().GetCurvature(S);
	CurrentLane = Road->FindNearestLane(D);
	TargetLane = CurrentLane;
	TargetLaneCenterD = Road->GetLaneCenterD(TargetLane);

	// Kinematics.
	LastVelocityCms = VelocityCms;
	bHasPreviousVelocity = true;
	SmoothedLocalAccelerationMs2 = FVector::ZeroVector;
	ForwardSpeedMs = SpeedMs;
	YawRateRadS = 0.f;
	SideSlipRad = 0.f;

	// Crash debounce.
	LastCrashActor.Reset();
	LastCrashTimeSeconds = -1.0e9;
}

// --- Tick --------------------------------------------------------------------------------------

void ALRVehicle::Tick(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALRVehicle::Tick);

	Super::Tick(DeltaSeconds);

	UpdateKinematics(DeltaSeconds);

	if (const ULRRoadSubsystem* Road = ULRRoadSubsystem::Get(this); Road != nullptr && Road->HasRoad())
	{
		UpdateRoadState(*Road);
	}

	UpdateDriving();
	UpdateVisualWheels();
}

void ALRVehicle::UpdateKinematics(float DeltaSeconds)
{
	const USkeletalMeshComponent* MeshComp = GetMesh();
	const bool bSimulating = MeshComp != nullptr && MeshComp->IsSimulatingPhysics();

	const FVector VelocityCms = bSimulating ? MeshComp->GetPhysicsLinearVelocity() : GetVelocity();
	const FTransform& ActorTransform = GetActorTransform();
	const FVector LocalVelocityCms = ActorTransform.InverseTransformVectorNoScale(VelocityCms);
	ForwardSpeedMs = static_cast<float>(LRUnits::CmToMeters(LocalVelocityCms.X));

	if (bHasPreviousVelocity && DeltaSeconds > UE_KINDA_SMALL_NUMBER)
	{
		DetectImpact(LastVelocityCms, VelocityCms, DeltaSeconds);

		const FVector WorldAccelerationMs2 = (VelocityCms - LastVelocityCms) * (LRUnits::MetersPerCm / DeltaSeconds);
		const FVector LocalAccelerationMs2 = ActorTransform.InverseTransformVectorNoScale(WorldAccelerationMs2);
		const float Alpha = LRVehicleMath::ComputeSmoothingAlpha(DeltaSeconds, Definition.AccelerationSmoothingSeconds);
		SmoothedLocalAccelerationMs2 = FMath::Lerp(SmoothedLocalAccelerationMs2, LocalAccelerationMs2, Alpha);
	}
	LastVelocityCms = VelocityCms;
	bHasPreviousVelocity = true;

	const FVector AngularVelocity = bSimulating ? MeshComp->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;
	YawRateRadS = static_cast<float>(FVector::DotProduct(AngularVelocity, GetActorUpVector()));

	SideSlipRad = FMath::Abs(LocalVelocityCms.X) > LRVehiclePrivate::SideSlipMinSpeedCms
		? static_cast<float>(FMath::Atan2(LocalVelocityCms.Y, FMath::Abs(LocalVelocityCms.X)))
		: 0.f;
}

void ALRVehicle::DetectImpact(const FVector& PreviousVelocityCms, const FVector& VelocityCms, float DeltaSeconds)
{
	// Horizontal only: a bump or a landing is not a crash.
	const float DeltaKmh = static_cast<float>(LRUnits::CmsToKmh((VelocityCms - PreviousVelocityCms).Size2D()));
	if (bWrecked || !LRVehicleMath::IsImpactSpike(DeltaKmh, DeltaSeconds, Definition.ImpactMinDeltaKmh, Definition.ImpactMinAccelMs2))
	{
		return;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now - LastCrashTimeSeconds < Definition.CrashDebounceSeconds)
	{
		return;
	}
	LastCrashTimeSeconds = Now;

	AActor* Other = FindActorInContact();
	LastCrashActor = Other;

	FLRCrashEvent Event;
	Event.ImpactSpeedKmh = DeltaKmh;
	Event.PlayerSpeedKmh = static_cast<float>(LRUnits::CmsToKmh(PreviousVelocityCms.Size()));
	Event.Location = GetActorLocation();
	Event.OtherActor = Other;
	Event.bSevere = DeltaKmh >= Definition.CrashImpactKmh;

	UE_LOG(LogLRVehicle, Log, TEXT("Impact with %s: delta %.1f km/h at %.1f km/h, severe=%d"),
		*GetNameSafe(Other), Event.ImpactSpeedKmh, Event.PlayerSpeedKmh, Event.bSevere ? 1 : 0);

	if (const ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
	{
		Events->BroadcastCrash(Event);
	}
}

AActor* ALRVehicle::FindActorInContact() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectTypes.AddObjectTypesToQuery(ECC_PhysicsBody);
	ObjectTypes.AddObjectTypesToQuery(ECC_Vehicle);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(LRVehicleContact), false, this);

	// A sphere reaching just past the footprint: the car has already been pushed back when the spike is seen.
	const float RadiusCm = 100.f * (0.5f * Definition.LengthMeters + 1.5f);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, GetActorLocation(), FQuat::Identity, ObjectTypes, FCollisionShape::MakeSphere(RadiusCm), Params);

	AActor* Nearest = nullptr;
	double NearestDistSq = TNumericLimits<double>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (Candidate == nullptr)
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Candidate->GetActorLocation(), GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = Candidate;
		}
	}
	return Nearest;
}

void ALRVehicle::UpdateRoadState(const ULRRoadSubsystem& Road)
{
	FLRRoadCoord Coord;
	if (!Road.WorldToRoad(GetActorLocation(), Coord, &RoadSegmentHint))
	{
		// Off the generated window (e.g. teleported far away): keep the last coordinate, the assist stays passive.
		bHasRoadCoord = false;
		RoadSegmentHint = INDEX_NONE;
		return;
	}
	CachedRoadCoord = Coord;
	bHasRoadCoord = true;

	const FLRRoadPath& Path = Road.GetPath();
	const float RoadYawRad = FMath::DegreesToRadians(Path.GetHeadingYawDegrees(Coord.S));
	HeadingErrorRad = LRVehicleMath::WrapAngleRad(FMath::DegreesToRadians(static_cast<float>(GetActorRotation().Yaw)) - RoadYawRad);
	RoadCurvaturePerM = Path.GetCurvature(Coord.S);

	// Leaving the corridor (past a barrier or through the surface) must end the run: there is no ground out there.
	if (!bOffRoadReported && !bWrecked)
	{
		const FLRRoadCrossSection& Section = Road.GetCrossSection();
		const double HeightAboveRoadM = LRUnits::CmToMeters(GetActorLocation().Z - Road.RoadToWorld(Coord.S, Coord.D).GetLocation().Z);
		if (LRVehicleMath::IsOffRoad(Coord.D, HeightAboveRoadM, Section.LeftBarrierD, Section.RightBarrierD,
			Definition.OffRoadBelowMeters, Definition.OffRoadLateralMarginMeters))
		{
			bOffRoadReported = true;
			UE_LOG(LogLRVehicle, Log, TEXT("Off road at S=%.1f D=%.2f height=%.2f m"), Coord.S, Coord.D, HeightAboveRoadM);

			if (const ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
			{
				FLRCrashEvent Event;
				Event.PlayerSpeedKmh = GetSpeedKmh();
				Event.ImpactSpeedKmh = Event.PlayerSpeedKmh;
				Event.Location = GetActorLocation();
				Event.bSevere = true;
				Events->BroadcastCrash(Event);
			}
		}
	}

	const TArray<FLRLaneDesc>& Lanes = Path.GetLanes();
	const int32 NewLane = LRVehicleMath::UpdateCurrentLane(Lanes, CurrentLane, Coord.D, Definition.LaneAssist.LaneChangeHysteresisMeters);
	if (NewLane != CurrentLane)
	{
		const int32 PreviousLane = CurrentLane;
		CurrentLane = NewLane;
		if (PreviousLane != INDEX_NONE && NewLane != INDEX_NONE)
		{
			if (const ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
			{
				FLRLaneChangeEvent Event;
				Event.FromLane = PreviousLane;
				Event.ToLane = NewLane;
				Event.bIntoOncoming = Lanes[NewLane].Direction < 0;
				Events->BroadcastLaneChange(Event);
			}
		}
	}

	if (ControlScheme == ELRControlScheme::Simcade || !Lanes.IsValidIndex(TargetLane))
	{
		TargetLane = CurrentLane;
	}
	if (Lanes.IsValidIndex(TargetLane))
	{
		TargetLaneCenterD = Lanes[TargetLane].CenterD;
	}
}

bool ALRVehicle::AllowsOncomingLanes(const ULRRoadSubsystem& Road) const
{
	return Road.GetProfile().bTwoWay;
}

float ALRVehicle::ComputeSteering() const
{
	const float RawSteering = bInputEnabled ? SteeringInput : 0.f;

	if (ControlScheme == ELRControlScheme::Arcade)
	{
		const FLRLaneAssistParams& Params = Definition.LaneAssist;
		if (!Params.bEnabled || !bHasRoadCoord || TargetLane == INDEX_NONE || ForwardSpeedMs < Params.MinActiveSpeedMs)
		{
			return RawSteering;
		}
		return FLRLaneAssist::ComputeSteering(Params, CachedRoadCoord.D, TargetLaneCenterD, HeadingErrorRad, YawRateRadS,
			ForwardSpeedMs, RoadCurvaturePerM);
	}

	return FLRStabilityAssist::ComputeSteering(Definition.StabilityAssist, RawSteering, ForwardSpeedMs, YawRateRadS, SideSlipRad,
		Definition.WheelbaseMeters, Definition.FrontWheels.MaxSteerAngleDeg);
}

void ALRVehicle::UpdateDriving()
{
	ULRVehicleMovementComponent* Movement = GetLRMovement();
	if (Movement == nullptr)
	{
		return;
	}

	if (bWrecked)
	{
		// Coast: no driver input at all, physics keeps simulating.
		AppliedThrottle = 0.f;
		AppliedSteering = 0.f;
		Movement->SetThrottleInput(0.f);
		Movement->SetBrakeInput(0.f);
		Movement->SetSteeringInput(0.f);
		Movement->SetHandbrakeInput(false);
		return;
	}

	using LRVehiclePrivate::PedalThreshold;

	const float Throttle = bInputEnabled ? ThrottleInput : 0.f;
	const float Brake = bInputEnabled ? BrakeInput : 0.f;
	const bool bHandbrake = bInputEnabled && bHandbrakeInput;

	// Brake held near standstill engages reverse; throttle returns to first gear once (almost) stopped.
	const float ReverseEngageMs = static_cast<float>(LRUnits::KmhToMs(Definition.ReverseEngageSpeedKmh));
	if (!bReverseEngaged)
	{
		if (Brake > PedalThreshold && Throttle <= PedalThreshold && FMath::Abs(ForwardSpeedMs) < ReverseEngageMs)
		{
			bReverseEngaged = true;
			Movement->SetTargetGear(-1, true);
		}
	}
	else if (Throttle > PedalThreshold && ForwardSpeedMs > -ReverseEngageMs)
	{
		bReverseEngaged = false;
		Movement->SetTargetGear(1, true);
	}

	float ChaosThrottle = 0.f;
	float ChaosBrake = 0.f;
	if (bReverseEngaged)
	{
		// In reverse the brake pedal drives backwards and the throttle pedal brakes.
		ChaosThrottle = Brake;
		ChaosBrake = Throttle;
	}
	else
	{
		const float SpeedKmh = static_cast<float>(LRUnits::MsToKmh(ForwardSpeedMs));
		ChaosThrottle = Throttle * LRVehicleMath::ComputeTopSpeedThrottleScale(SpeedKmh, Definition.TopSpeedKmh, Definition.TopSpeedFadeFraction);
		ChaosBrake = Brake;
	}

	AppliedThrottle = ChaosThrottle;
	AppliedSteering = ComputeSteering();

	Movement->SetThrottleInput(ChaosThrottle);
	Movement->SetBrakeInput(ChaosBrake);
	Movement->SetSteeringInput(AppliedSteering);
	Movement->SetHandbrakeInput(bHandbrake);
}

// --- Crash -------------------------------------------------------------------------------------

void ALRVehicle::HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	UE_LOG(LogLRVehicle, VeryVerbose, TEXT("Hit %s normal %s"), *GetNameSafe(OtherActor), *Hit.ImpactNormal.ToString());

	if (OtherActor == this)
	{
		return;
	}

	// Contacts with a mostly vertical normal are the ground (landing, bottoming out), not crashes.
	if (FMath::Abs(Hit.ImpactNormal.Z) > Definition.CrashFloorNormalZ)
	{
		UE_LOG(LogLRVehicle, VeryVerbose, TEXT("Hit %s ignored as ground (normal Z %.2f)"), *GetNameSafe(OtherActor), Hit.ImpactNormal.Z);
		return;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (LastCrashActor.Get() == OtherActor && (Now - LastCrashTimeSeconds) < Definition.CrashDebounceSeconds)
	{
		return;
	}

	FVector OtherVelocityCms = FVector::ZeroVector;
	if (OtherComp != nullptr)
	{
		OtherVelocityCms = OtherComp->IsSimulatingPhysics() ? OtherComp->GetPhysicsLinearVelocity() : OtherComp->GetComponentVelocity();
	}
	if (OtherVelocityCms.IsNearlyZero() && OtherActor != nullptr)
	{
		OtherVelocityCms = OtherActor->GetVelocity();
	}

	// LastVelocityCms was sampled at the start of the frame, i.e. before the contact was resolved.
	const FVector RelativeVelocityCms = LastVelocityCms - OtherVelocityCms;
	const FVector Normal = Hit.ImpactNormal.IsNearlyZero() ? Hit.Normal : Hit.ImpactNormal;
	const float ImpactSpeedKmh = LRVehicleMath::ComputeImpactSpeedKmh(RelativeVelocityCms, Normal);
	if (ImpactSpeedKmh < Definition.CrashMinReportKmh)
	{
		return;
	}

	LastCrashActor = OtherActor;
	LastCrashTimeSeconds = Now;

	FLRCrashEvent Event;
	Event.ImpactSpeedKmh = ImpactSpeedKmh;
	Event.PlayerSpeedKmh = static_cast<float>(LRUnits::CmsToKmh(LastVelocityCms.Size()));
	Event.Location = Hit.ImpactPoint;
	Event.OtherActor = OtherActor;
	Event.bSevere = ImpactSpeedKmh >= Definition.CrashImpactKmh;

	UE_LOG(LogLRVehicle, Log, TEXT("Crash with %s: impact %.1f km/h, speed %.1f km/h, severe=%d"),
		*GetNameSafe(OtherActor), Event.ImpactSpeedKmh, Event.PlayerSpeedKmh, Event.bSevere ? 1 : 0);

	if (const ULRGameEventSubsystem* Events = ULRGameEventSubsystem::Get(this))
	{
		Events->BroadcastCrash(Event);
	}
}

// --- Cameras -----------------------------------------------------------------------------------

FTransform ALRVehicle::GetCameraAnchorWorld(ELRCameraMode Mode) const
{
	const FLRCameraAnchors& Anchors = Definition.Cameras;
	const FTransform& ActorTransform = GetActorTransform();
	const FQuat ActorRotation = ActorTransform.GetRotation();

	auto MountedAnchor = [&ActorTransform, &ActorRotation](const FVector& LocalOffsetCm)
	{
		return FTransform(ActorRotation, ActorTransform.TransformPositionNoScale(LocalOffsetCm));
	};

	auto ChaseAnchor = [this, &Anchors](float DistanceCm, float HeightCm)
	{
		const FRotator YawOnly(0.0, GetActorRotation().Yaw, 0.0);
		const FVector Origin = GetActorLocation();
		const FVector CameraLocation = Origin - YawOnly.Vector() * DistanceCm + FVector::UpVector * HeightCm;
		const FVector LookAt = Origin + FVector::UpVector * Anchors.ChaseLookAtHeightCm;
		return FTransform((LookAt - CameraLocation).Rotation(), CameraLocation);
	};

	switch (Mode)
	{
	case ELRCameraMode::ChaseNear:
		return ChaseAnchor(Anchors.ChaseNearDistanceCm, Anchors.ChaseNearHeightCm);
	case ELRCameraMode::Hood:
		return MountedAnchor(Anchors.HoodCm);
	case ELRCameraMode::Bumper:
		return MountedAnchor(Anchors.BumperCm);
	case ELRCameraMode::Cockpit:
		return MountedAnchor(Anchors.CockpitEyeCm);
	case ELRCameraMode::Viral:
		return MountedAnchor(Anchors.ViralMountCm);
	case ELRCameraMode::ChaseFar:
	default:
		return ChaseAnchor(Anchors.ChaseFarDistanceCm, Anchors.ChaseFarHeightCm);
	}
}
