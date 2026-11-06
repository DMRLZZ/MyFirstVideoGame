// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "LRVehicleTypes.h"

#include "LRVehicleMovementComponent.generated.h"

/**
 * Chaos wheeled movement of the LEFTRIGHT player cars, configured at runtime from FLRVehicleDefinition.
 *
 * ApplyDefinition writes the setup UPROPERTYs (engine, transmission, steering, differential, wheel setups, mass,
 * aero, center of mass) and recreates the physics state.
 *
 * Wheels: Chaos builds every physics wheel and suspension from the wheel CLASS default object (FChaosWheelSetup::
 * WheelClass) and keeps pointing at its config, so per-car tire and suspension values (cornering stiffness, springs,
 * travel...) are written into the ULRWheelFront / ULRWheelRear default objects before the physics vehicle is rebuilt.
 * Only one player car exists at a time, which makes this safe. Values that have a plugin runtime setter
 * (SetWheelRadius, SetWheelFrictionMultiplier, SetWheelMaxBrakeTorque, SetMaxEngineTorque, SetDragCoefficient...)
 * are additionally pushed through those setters every time the physics vehicle is (re)created.
 * SetSuspensionParams is deliberately NOT used: it creates an extra suspension constraint on every call.
 *
 * Owned by ALRVehicle (replaces the default movement component of AWheeledVehiclePawn).
 */
UCLASS(ClassGroup = (LeftRight), meta = (BlueprintSpawnableComponent))
class LRVEHICLE_API ULRVehicleMovementComponent : public UChaosWheeledVehicleMovementComponent
{
	GENERATED_BODY()

public:
	ULRVehicleMovementComponent(const FObjectInitializer& ObjectInitializer);

	/**
	 * Configures the whole vehicle from a definition. Safe to call before or after the component is registered;
	 * once registered the physics state is recreated so the change takes effect immediately (call it after the
	 * skeletal mesh and animation class are set, so the new physics vehicle links to them).
	 */
	void ApplyDefinition(const FLRVehicleDefinition& Definition);

	/** True once ApplyDefinition was called. */
	bool HasDefinition() const { return bHasDefinition; }

	/** True when the Chaos physics vehicle exists (skeletal mesh with physics asset, game world). */
	bool HasPhysicsVehicle() const { return PVehicleOutput.IsValid(); }

	/** Offsets of the physics wheels from the rig bones, cm, one per wheel (front first). Call before ApplyDefinition. */
	void SetWheelOffsets(const TArray<FVector>& InOffsets) { WheelOffsets = InOffsets; }

	/** Number of wheels on the front axle (they come first in WheelSetups). */
	int32 GetNumFrontWheels() const { return NumFrontWheels; }

	/**
	 * Sets the spin of every wheel to roll without slip at SpeedMs (used after teleporting with an initial speed so the
	 * tires do not scrub on the first frame). No-op without a physics vehicle.
	 */
	void MatchWheelSpeeds(float SpeedMs);

	/** Re-pushes the per-wheel runtime values of the current definition (no-op without a definition or vehicle). */
	void ApplyRuntimeWheelOverrides();

private:
	/** See SetWheelOffsets. */
	TArray<FVector> WheelOffsets;

protected:
	//~ UActorComponent
	virtual void OnCreatePhysicsState() override;

	/** No Chaos vehicle until a definition is applied: at spawn the mesh is still empty and wheel bones cannot be located. */
	virtual bool CanCreateVehicle() const override;

private:
	/** Writes every setup UPROPERTY from Definition (no physics calls). */
	void WriteSetupProperties(const FLRVehicleDefinition& Definition);

	/** Writes the per-axle tire, brake and suspension values into the LR wheel class default objects. */
	static void WriteWheelClassDefaults(const FLRVehicleDefinition& Definition);

	/** Copy of the applied definition, re-used when the physics state is recreated (e.g. new skeletal mesh). */
	FLRVehicleDefinition AppliedDefinition;

	bool bHasDefinition = false;

	int32 NumFrontWheels = 2;
};
