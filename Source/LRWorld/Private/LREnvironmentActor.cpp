// Copyright LEFTRIGHT. All Rights Reserved.

#include "LREnvironmentActor.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "LRDataLoader.h"
#include "LRWorld.h"

ALREnvironmentActor::ALREnvironmentActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	GroundPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroundPlane"));
	GroundPlane->SetupAttachment(Root);
	GroundPlane->SetMobility(EComponentMobility::Movable);
	GroundPlane->SetStaticMesh(PlaneFinder.Object);
	GroundPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GroundPlane->SetGenerateOverlapEvents(false);
	GroundPlane->SetCanEverAffectNavigation(false);
	GroundPlane->SetRelativeScale3D(FVector(6000.0, 6000.0, 1.0)); // The engine plane is 1 m wide: 6 km.

	SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunLight"));
	SunLight->SetupAttachment(Root);
	SunLight->SetMobility(EComponentMobility::Movable);
	// Plain field writes in the constructor (setters would try to dirty a render state that does not exist yet).
	SunLight->bAtmosphereSunLight = true;
	SunLight->AtmosphereSunLightIndex = 0;
	SunLight->CastShadows = true;

	SkyAtmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	SkyAtmosphere->SetupAttachment(Root);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;

	HeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	HeightFog->SetupAttachment(Root);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(Root);
	PostProcess->bUnbound = true;
	PostProcess->Priority = 0.f;
}

void ALREnvironmentActor::BeginPlay()
{
	Super::BeginPlay();

	if (!bPresetApplied)
	{
		ApplyPreset(CurrentPreset);
	}
}

bool ALREnvironmentActor::LoadAndApplyPreset(const FString& RelativePath)
{
	FLREnvironmentPreset Preset;
	FString Error;
	if (!FLRDataLoader::Load(RelativePath, Preset, &Error))
	{
		UE_LOG(LogLRWorld, Warning, TEXT("Environment preset '%s' could not be loaded: %s"), *RelativePath, *Error);
		return false;
	}

	ApplyPreset(Preset);
	return true;
}

void ALREnvironmentActor::UpdateGround(const FVector& FocusLocationCm)
{
	// Below the road so that grade changes never make it poke through the asphalt.
	constexpr double GroundDepthCm = 250.0;
	GroundPlane->SetWorldLocation(FVector(FocusLocationCm.X, FocusLocationCm.Y, FocusLocationCm.Z - GroundDepthCm));
}

void ALREnvironmentActor::ApplyPreset(const FLREnvironmentPreset& Preset)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LeftRight/Materials/M_LRSurface.M_LRSurface"));
	if (Base == nullptr)
	{
		Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (Base != nullptr)
	{
		UMaterialInstanceDynamic* GroundMaterial = UMaterialInstanceDynamic::Create(Base, this);
		GroundMaterial->SetVectorParameterValue(TEXT("Color"), Preset.GroundColor);
		GroundMaterial->SetScalarParameterValue(TEXT("Roughness"), 0.95f);
		GroundMaterial->SetScalarParameterValue(TEXT("GrainAmount"), 0.8f);
		GroundMaterial->SetScalarParameterValue(TEXT("GrainScale"), 1500.f); // The plane has one UV tile over 6 km.
		GroundPlane->SetMaterial(0, GroundMaterial);
	}

	CurrentPreset = Preset;
	bPresetApplied = true;

	SunLight->SetWorldRotation(FRotator(Preset.SunPitchDeg, Preset.SunYawDeg, 0.f));
	SunLight->SetIntensity(FMath::Max(0.f, Preset.SunIntensityLux));
	SunLight->SetLightColor(Preset.SunColor, /*bSRGB*/ false);
	SunLight->SetLightSourceAngle(FMath::Max(0.f, Preset.SunSourceAngleDeg));

	SkyLight->SetIntensity(FMath::Max(0.f, Preset.SkyLightIntensity));
	SkyLight->SetRealTimeCapture(true);

	HeightFog->SetFogDensity(FMath::Max(0.f, Preset.FogDensity));
	HeightFog->SetFogHeightFalloff(FMath::Max(0.001f, Preset.FogHeightFalloff));
	HeightFog->SetFogInscatteringColor(Preset.FogInscatteringColor);
	HeightFog->SetVolumetricFog(Preset.bVolumetricFog);

	PostProcess->Settings.bOverride_AutoExposureBias = true;
	PostProcess->Settings.AutoExposureBias = Preset.ExposureCompensation;

	UE_LOG(LogLRWorld, Verbose, TEXT("Environment preset applied (sun pitch %.1f, %.0f lux, night=%d)"),
		Preset.SunPitchDeg, Preset.SunIntensityLux, Preset.bNight ? 1 : 0);
}
