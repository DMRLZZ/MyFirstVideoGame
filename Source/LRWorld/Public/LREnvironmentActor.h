// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LRRoadTypes.h"

#include "LREnvironmentActor.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;

/**
 * Greybox sky and lighting rig: atmosphere sun light, sky atmosphere, real-time sky light, height fog and an unbound
 * post process volume, all configured from an FLREnvironmentPreset (Data/World/Environment_*.json).
 *
 * Spawned (transient) by ULRRoadSubsystem at world begin play; the route chooses the preset.
 */
UCLASS(NotBlueprintable)
class LRWORLD_API ALREnvironmentActor : public AActor
{
	GENERATED_BODY()

public:
	ALREnvironmentActor();

	/** Applies every value of the preset to the components (cheap, safe to call at any time). */
	void ApplyPreset(const FLREnvironmentPreset& Preset);

	/** Loads a preset file relative to Data/ and applies it. Returns false (and keeps the current look) on failure. */
	bool LoadAndApplyPreset(const FString& RelativePath);

	/** Last applied preset. */
	const FLREnvironmentPreset& GetPreset() const { return CurrentPreset; }

	/** True when the current preset is a night preset (headlights, street lamps). */
	bool IsNight() const { return CurrentPreset.bNight; }

	/** Keeps the ground plane under the player, so the world never ends below the horizon. */
	void UpdateGround(const FVector& FocusLocationCm);

	/** Sun / moon directional light. */
	UDirectionalLightComponent* GetSunLight() const { return SunLight; }

protected:
	//~ AActor
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Environment")
	TObjectPtr<USceneComponent> Root;

	/** Large flat ground around the road, no collision (leaving the road ends the run). */
	UPROPERTY(VisibleAnywhere, Category = "Environment")
	TObjectPtr<class UStaticMeshComponent> GroundPlane;

	/** Two rings of distant hills and mountains that travel with the player: too far away for any parallax to show. */
	UPROPERTY(VisibleAnywhere, Category = "Environment")
	TObjectPtr<class UProceduralMeshComponent> FarRidges;

	UPROPERTY(VisibleAnywhere, Category = "Environment")
	TObjectPtr<UDirectionalLightComponent> SunLight;

	UPROPERTY(VisibleAnywhere, Category = "Environment")
	TObjectPtr<USkyAtmosphereComponent> SkyAtmosphere;

	UPROPERTY(VisibleAnywhere, Category = "Environment")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "Environment")
	TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	UPROPERTY(VisibleAnywhere, Category = "Environment")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** Builds one closed ring of FarRidges into a mesh section. */
	void BuildRidge(int32 Section, float InnerRadiusM, float PeakRadiusM, float BaseHeightM, float AmplitudeM, int32 Seed, class UMaterialInterface* Material);

	/** Last applied preset (defaults = day). */
	FLREnvironmentPreset CurrentPreset;

	/** True once ApplyPreset ran (BeginPlay applies the defaults otherwise). */
	bool bPresetApplied = false;
};
