// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRVehicleWheel.h"

namespace LRVehicleWheelPrivate
{
	/** Values shared by both axles of the generic sports-car default. */
	void ApplyCommonDefaults(UChaosVehicleWheel& Wheel)
	{
		Wheel.WheelRadius = 34.f;
		Wheel.WheelWidth = 25.f;
		Wheel.WheelMass = 20.f;
		Wheel.FrictionForceMultiplier = 3.f;
		Wheel.CorneringStiffness = 1000.f;
		Wheel.SideSlipModifier = 1.f;
		Wheel.SlipThreshold = 20.f;
		Wheel.SkidThreshold = 20.f;
		Wheel.bAffectedByBrake = true;
		Wheel.bABSEnabled = true;
		Wheel.bTractionControlEnabled = true;
		Wheel.SuspensionMaxRaise = 8.f;
		Wheel.SuspensionMaxDrop = 10.f;
		Wheel.SuspensionDampingRatio = 0.5f;
		Wheel.SpringRate = 250.f;
		Wheel.SpringPreload = 50.f;
		Wheel.SuspensionSmoothing = 0;
		Wheel.RollbarScaling = 0.15f;
		Wheel.SweepShape = ESweepShape::Raycast;
		Wheel.SweepType = ESweepType::SimpleSweep;
	}
}

ULRWheelFront::ULRWheelFront(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	LRVehicleWheelPrivate::ApplyCommonDefaults(*this);
	AxleType = EAxleType::Front;
	bAffectedBySteering = true;
	MaxSteerAngle = 38.f;
	bAffectedByHandbrake = false;
	bAffectedByEngine = false;
	MaxBrakeTorque = 1800.f;
	MaxHandBrakeTorque = 0.f;
}

ULRWheelRear::ULRWheelRear(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	LRVehicleWheelPrivate::ApplyCommonDefaults(*this);
	AxleType = EAxleType::Rear;
	bAffectedBySteering = false;
	MaxSteerAngle = 0.f;
	bAffectedByHandbrake = true;
	bAffectedByEngine = true;
	MaxBrakeTorque = 1400.f;
	MaxHandBrakeTorque = 3000.f;
}
