// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRVehicleMovementComponent.h"

#include "ChaosVehicleWheel.h"
#include "Curves/RichCurve.h"
#include "LRUnits.h"
#include "LRVehicle.h"
#include "LRVehicleWheel.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "PhysicsEngine/BodyInstance.h"
#include "SimpleVehicle.h"

namespace LRVehicleMovementPrivate
{
	EVehicleDifferential ToChaosDifferential(ELRDriveType DriveType)
	{
		switch (DriveType)
		{
		case ELRDriveType::FrontWheelDrive:
			return EVehicleDifferential::FrontWheelDrive;
		case ELRDriveType::AllWheelDrive:
			return EVehicleDifferential::AllWheelDrive;
		case ELRDriveType::RearWheelDrive:
		default:
			return EVehicleDifferential::RearWheelDrive;
		}
	}

	/** Fills an rpm-domain Chaos torque curve from the normalized definition curve (flat 1 if invalid). */
	void FillTorqueCurve(FRichCurve& Curve, const FLREngineParams& Engine)
	{
		Curve.Reset();
		bool bHasPositive = false;
		for (const FVector2D& Point : Engine.TorqueCurve)
		{
			const float Rpm = static_cast<float>(FMath::Clamp(Point.X, 0.0, 1.0)) * Engine.MaxRpm;
			const float Factor = static_cast<float>(FMath::Clamp(Point.Y, 0.0, 1.0));
			Curve.AddKey(Rpm, Factor);
			bHasPositive |= Factor > 0.f;
		}
		if (Curve.GetNumKeys() < 2 || !bHasPositive)
		{
			// Chaos normalizes by the curve maximum, so an empty or all-zero curve would produce NaN torque.
			Curve.Reset();
			Curve.AddKey(0.f, 1.f);
			Curve.AddKey(Engine.MaxRpm, 1.f);
		}
	}

	/** Copies one axle of the definition into a wheel default object (values Chaos reads when it builds the vehicle). */
	void WriteAxleToWheel(UChaosVehicleWheel& Wheel, const FLRWheelAxleParams& Axle, const FLRStabilityAssistParams& Stability)
	{
		Wheel.WheelRadius = FMath::Max(Axle.RadiusCm, 1.f);
		Wheel.WheelWidth = FMath::Max(Axle.WidthCm, 1.f);
		Wheel.FrictionForceMultiplier = Axle.FrictionForceMultiplier;
		Wheel.CorneringStiffness = Axle.CorneringStiffness;
		Wheel.MaxSteerAngle = Axle.MaxSteerAngleDeg;
		Wheel.bAffectedBySteering = Axle.MaxSteerAngleDeg > 0.f;
		Wheel.MaxBrakeTorque = Axle.MaxBrakeTorqueNm;
		Wheel.MaxHandBrakeTorque = Axle.MaxHandBrakeTorqueNm;
		Wheel.bAffectedByHandbrake = Axle.MaxHandBrakeTorqueNm > 0.f;
		Wheel.SuspensionMaxRaise = Axle.SuspensionMaxRaiseCm;
		Wheel.SuspensionMaxDrop = Axle.SuspensionMaxDropCm;
		Wheel.SpringRate = Axle.SpringRate;
		Wheel.SpringPreload = Axle.SpringPreload;
		Wheel.SuspensionDampingRatio = Axle.SuspensionDampingRatio;
		Wheel.bABSEnabled = Stability.bABSEnabled;
		Wheel.bTractionControlEnabled = Stability.bTractionControlEnabled;
	}
}

ULRVehicleMovementComponent::ULRVehicleMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Inputs come from the player controller, an autopilot or a replay: never require a possessing controller.
	bRequiresControllerForInputs = false;

	// Reverse is engaged explicitly by ALRVehicle (only below ReverseEngageSpeedKmh) so the analog brake stays analog.
	bReverseAsBrake = false;
	bThrottleAsBrake = false;

	// Sensible defaults so the component is valid even before a definition is applied.
	WriteSetupProperties(FLRVehicleDefinition());
}

void ULRVehicleMovementComponent::WriteSetupProperties(const FLRVehicleDefinition& Def)
{
	using namespace LRVehicleMovementPrivate;

	// Body.
	Mass = FMath::Max(Def.MassKg, 1.f);
	bEnableCenterOfMassOverride = Def.bOverrideCenterOfMass;
	CenterOfMassOverride = Def.CenterOfMassOffsetCm;
	ChassisWidth = FMath::Max(Def.Aero.ChassisWidthCm, 1.f);
	ChassisHeight = FMath::Max(Def.Aero.ChassisHeightCm, 1.f);
	DragCoefficient = Def.Aero.DragCoefficient;
	DownforceCoefficient = Def.Aero.DownforceCoefficient;

	// Engine.
	EngineSetup.MaxTorque = Def.Engine.MaxTorqueNm;
	EngineSetup.MaxRPM = FMath::Max(Def.Engine.MaxRpm, 1000.f);
	EngineSetup.EngineIdleRPM = FMath::Clamp(Def.Engine.IdleRpm, 100.f, EngineSetup.MaxRPM - 100.f);
	EngineSetup.EngineBrakeEffect = Def.Engine.EngineBrakeEffect;
	EngineSetup.EngineRevUpMOI = FMath::Max(Def.Engine.RevUpMOI, 0.01f);
	EngineSetup.EngineRevDownRate = FMath::Max(Def.Engine.RevDownRate, 0.01f);
	if (FRichCurve* TorqueCurve = EngineSetup.TorqueCurve.GetRichCurve())
	{
		FillTorqueCurve(*TorqueCurve, Def.Engine);
	}

	// Transmission.
	TransmissionSetup.bUseAutomaticGears = Def.Transmission.bAutomatic;
	TransmissionSetup.bUseAutoReverse = true;
	TransmissionSetup.FinalRatio = Def.Transmission.FinalDriveRatio;
	TransmissionSetup.ForwardGearRatios = Def.Transmission.ForwardGearRatios;
	if (TransmissionSetup.ForwardGearRatios.Num() == 0)
	{
		TransmissionSetup.ForwardGearRatios.Add(1.f);
	}
	TransmissionSetup.ReverseGearRatios.Reset();
	TransmissionSetup.ReverseGearRatios.Add(Def.Transmission.ReverseGearRatio);
	TransmissionSetup.ChangeUpRPM = Def.Transmission.ChangeUpRpm;
	TransmissionSetup.ChangeDownRPM = Def.Transmission.ChangeDownRpm;
	TransmissionSetup.GearChangeTime = Def.Transmission.GearChangeTime;
	TransmissionSetup.TransmissionEfficiency = Def.Transmission.TransmissionEfficiency;

	// Differential.
	DifferentialSetup.DifferentialType = ToChaosDifferential(Def.Differential.DriveType);
	DifferentialSetup.FrontRearSplit = Def.Differential.FrontRearSplit;

	// Steering: speed sensitivity is owned by the LR assists, so the Chaos curve is flat.
	SteeringSetup.SteeringType = ESteeringType::Ackermann;
	SteeringSetup.AngleRatio = Def.Steering.AngleRatio;
	if (FRichCurve* SteeringCurve = SteeringSetup.SteeringCurve.GetRichCurve())
	{
		SteeringCurve->Reset();
		SteeringCurve->AddKey(0.f, 1.f);
		SteeringCurve->AddKey(500.f, 1.f);
	}
	SteeringInputRate.RiseRate = Def.Steering.InputRiseRate;
	SteeringInputRate.FallRate = Def.Steering.InputFallRate;

	// Reverse engages only below this speed (also used by the plugin wrong-direction logic), cm/s.
	WrongDirectionThreshold = static_cast<float>(LRUnits::MetersToCm(LRUnits::KmhToMs(Def.ReverseEngageSpeedKmh)));

	// Wheels: front bones first, then rear.
	WheelSetups.Reset(Def.FrontWheels.BoneNames.Num() + Def.RearWheels.BoneNames.Num());
	for (const FName& Bone : Def.FrontWheels.BoneNames)
	{
		FChaosWheelSetup& Setup = WheelSetups.AddDefaulted_GetRef();
		Setup.WheelClass = ULRWheelFront::StaticClass();
		Setup.BoneName = Bone;
	}
	NumFrontWheels = WheelSetups.Num();
	for (const FName& Bone : Def.RearWheels.BoneNames)
	{
		FChaosWheelSetup& Setup = WheelSetups.AddDefaulted_GetRef();
		Setup.WheelClass = ULRWheelRear::StaticClass();
		Setup.BoneName = Bone;
	}
}

void ULRVehicleMovementComponent::WriteWheelClassDefaults(const FLRVehicleDefinition& Definition)
{
	using namespace LRVehicleMovementPrivate;

	if (ULRWheelFront* FrontDefaults = GetMutableDefault<ULRWheelFront>())
	{
		WriteAxleToWheel(*FrontDefaults, Definition.FrontWheels, Definition.StabilityAssist);
	}
	if (ULRWheelRear* RearDefaults = GetMutableDefault<ULRWheelRear>())
	{
		WriteAxleToWheel(*RearDefaults, Definition.RearWheels, Definition.StabilityAssist);
	}
}

void ULRVehicleMovementComponent::ApplyDefinition(const FLRVehicleDefinition& Definition)
{
	AppliedDefinition = Definition;
	bHasDefinition = true;

	WriteSetupProperties(Definition);
	WriteWheelClassDefaults(Definition);

	if (IsRegistered())
	{
		// Rebuilds the Chaos vehicle from the new setup (no-op creation outside game worlds or without a body).
		// OnCreatePhysicsState re-applies the runtime overrides once the new physics vehicle exists.
		RecreatePhysicsState();
	}
	SetUseAutomaticGears(Definition.Transmission.bAutomatic);

	UE_LOG(LogLRVehicle, Log, TEXT("Vehicle movement configured from '%s' (physics vehicle: %s)"),
		*Definition.Id.ToString(), HasPhysicsVehicle() ? TEXT("yes") : TEXT("no"));
}

void ULRVehicleMovementComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();
	ApplyRuntimeWheelOverrides();
}

void ULRVehicleMovementComponent::ApplyRuntimeWheelOverrides()
{
	if (!bHasDefinition || !HasPhysicsVehicle() || GetBodyInstance() == nullptr)
	{
		return;
	}

	const FLRVehicleDefinition& Def = AppliedDefinition;

	// Aero and engine have runtime setters: use them so the live simulation matches the definition.
	SetMaxEngineTorque(Def.Engine.MaxTorqueNm);
	SetDragCoefficient(Def.Aero.DragCoefficient);
	SetDownforceCoefficient(Def.Aero.DownforceCoefficient);
	if (Def.Differential.DriveType == ELRDriveType::AllWheelDrive)
	{
		SetDifferentialFrontRearSplit(Def.Differential.FrontRearSplit);
	}

	const int32 NumWheels = WheelSetups.Num();
	for (int32 WheelIndex = 0; WheelIndex < NumWheels; ++WheelIndex)
	{
		const FLRWheelAxleParams& Axle = WheelIndex < NumFrontWheels ? Def.FrontWheels : Def.RearWheels;

		SetWheelRadius(WheelIndex, Axle.RadiusCm);
		SetWheelFrictionMultiplier(WheelIndex, Axle.FrictionForceMultiplier);
		SetWheelMaxBrakeTorque(WheelIndex, Axle.MaxBrakeTorqueNm);
		SetWheelHandbrakeTorque(WheelIndex, Axle.MaxHandBrakeTorqueNm);
		SetAffectedByHandbrake(WheelIndex, Axle.MaxHandBrakeTorqueNm > 0.f);
		SetWheelMaxSteerAngle(WheelIndex, Axle.MaxSteerAngleDeg);
		SetAffectedBySteering(WheelIndex, Axle.MaxSteerAngleDeg > 0.f);
		SetABSEnabled(WheelIndex, Def.StabilityAssist.bABSEnabled);
		SetTractionControlEnabled(WheelIndex, Def.StabilityAssist.bTractionControlEnabled);
	}
}

void ULRVehicleMovementComponent::MatchWheelSpeeds(float SpeedMs)
{
	FBodyInstance* BodyInstance = GetBodyInstance();
	if (!HasPhysicsVehicle() || BodyInstance == nullptr)
	{
		return;
	}

	const float SpeedCms = static_cast<float>(LRUnits::MetersToCm(SpeedMs));
	FPhysicsCommand::ExecuteWrite(BodyInstance->GetPhysicsActor(), [this, SpeedCms](const FPhysicsActorHandle& /*Chassis*/)
	{
		if (!VehicleSimulationPT || !VehicleSimulationPT->PVehicle)
		{
			return;
		}
		for (Chaos::FSimpleWheelSim& Wheel : VehicleSimulationPT->PVehicle->Wheels)
		{
			// Wheel radius (Re) is in centimeters, so omega = v[cm/s] / Re[cm].
			Wheel.Omega = SpeedCms / FMath::Max(Wheel.Re, 1.f);
		}
	});
}
