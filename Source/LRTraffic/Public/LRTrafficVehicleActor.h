// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LRTrafficTypes.h"

#include "LRTrafficVehicleActor.generated.h"

class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Pooled visual + collision proxy of one simulated traffic vehicle (owned and driven by ULRTrafficSubsystem).
 *
 * Grey-box body built from engine basic shapes (body cube, cabin cube, 4 wheel cylinders), scaled to the archetype
 * footprint; paint color through a dynamic instance of /Engine/BasicShapes/BasicShapeMaterial ("Color" parameter);
 * brake lights and blinkers are small colored cubes toggled from the simulation light flags.
 *
 * Collision: the root is a kinematic UBoxComponent (BlockAllDynamic, hit events on) moved every frame with
 * ETeleportType::None so the physics engine derives its velocity and the player vehicle collides with a moving body.
 * EnterWreckMode switches it to a simulated rigid body. The actor never ticks: the subsystem updates it.
 */
UCLASS(NotBlueprintable, NotPlaceable)
class LRTRAFFIC_API ALRTrafficVehicleActor : public AActor
{
	GENERATED_BODY()

public:
	ALRTrafficVehicleActor();

	/** Resizes the box, body, cabin, wheels and lights for an archetype and sets the paint color. Call while inactive. */
	void ApplyArchetype(const FLRTrafficArchetype& InArchetype, const FLinearColor& PaintColor);

	/** Shows the actor, enables collision and binds it to a simulation vehicle id. */
	void Activate(int32 InVehicleId);

	/** Hides the actor, disables collision and physics, unbinds it (back to the pool). */
	void Deactivate();

	/**
	 * Moves the kinematic body.
	 * @param GroundLocationCm  Point on the road surface under the vehicle center, centimeters.
	 * @param Rotation          Vehicle rotation (yaw = travel heading, pitch from grade).
	 * @param LightFlags        LRTrafficLights bits.
	 * @param bBlinkOn          Current blinker phase (shared by every vehicle).
	 * @param DeltaSeconds      Frame time, used to estimate the kinematic velocity handed to a wreck.
	 */
	void UpdateKinematic(const FVector& GroundLocationCm, const FRotator& Rotation, uint8 LightFlags, bool bBlinkOn, float DeltaSeconds);

	/** Switches to a simulated rigid body keeping the current velocity and applies Impulse (kg*cm/s). Idempotent. */
	void EnterWreckMode(FVector Impulse);

	/** True after EnterWreckMode (until Deactivate). */
	bool IsWrecked() const { return bWrecked; }

	/** True while bound to a simulation vehicle. */
	bool IsActive() const { return bActive; }

	/** Simulation id of the bound vehicle, INDEX_NONE when pooled. */
	int32 GetTrafficVehicleId() const { return VehicleId; }

	/** Collision root. */
	UBoxComponent* GetCollisionBox() const { return CollisionBox; }

protected:
	/** Kinematic collision box (root). */
	UPROPERTY(VisibleAnywhere, Category = "Traffic")
	TObjectPtr<UBoxComponent> CollisionBox;

	/** Lower body. */
	UPROPERTY(VisibleAnywhere, Category = "Traffic")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** Cabin / greenhouse. */
	UPROPERTY(VisibleAnywhere, Category = "Traffic")
	TObjectPtr<UStaticMeshComponent> CabinMesh;

	/** Wheels: front-left, front-right, rear-left, rear-right. */
	UPROPERTY(VisibleAnywhere, Category = "Traffic")
	TArray<TObjectPtr<UStaticMeshComponent>> WheelMeshes;

	/** Brake lights: left, right. */
	UPROPERTY(VisibleAnywhere, Category = "Traffic")
	TArray<TObjectPtr<UStaticMeshComponent>> BrakeLightMeshes;

	/** Blinkers: front-left, rear-left, front-right, rear-right. */
	UPROPERTY(VisibleAnywhere, Category = "Traffic")
	TArray<TObjectPtr<UStaticMeshComponent>> BlinkerMeshes;

	/** /Engine/BasicShapes/BasicShapeMaterial. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PaintMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlassMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TireMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BrakeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BlinkerMaterial;

private:
	/** Creates the dynamic materials once (needs a live actor, not the CDO). */
	void EnsureMaterials();

	/** Creates a mesh component without collision attached to the box. */
	UStaticMeshComponent* CreateVisualPart(const FName Name, UStaticMesh* Mesh);

	/** Applies brake / blinker visuals when they changed. */
	void ApplyLights(uint8 LightFlags, bool bBlinkOn);

	/** Half height of the body, centimeters (box center above the ground). */
	float HalfHeightCm = 72.5f;

	int32 VehicleId = INDEX_NONE;
	bool bActive = false;
	bool bWrecked = false;
	bool bHasLastLocation = false;
	bool bBrakeOn = false;
	uint8 VisibleBlinkers = 0;
	FVector LastLocation = FVector::ZeroVector;
	FVector KinematicVelocityCms = FVector::ZeroVector;
};
