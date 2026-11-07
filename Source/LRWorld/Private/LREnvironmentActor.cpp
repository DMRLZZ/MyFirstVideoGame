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
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "LRDataLoader.h"
#include "LRTerrainField.h"
#include "LRUnits.h"
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
	GroundPlane->SetRelativeScale3D(FVector(30000.0, 30000.0, 1.0)); // The engine plane is 1 m wide: 30 km.
	GroundPlane->SetCastShadow(false);

	FarRidges = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FarRidges"));
	FarRidges->SetupAttachment(Root);
	FarRidges->SetMobility(EComponentMobility::Movable);
	FarRidges->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FarRidges->SetCanEverAffectNavigation(false);
	FarRidges->SetCastShadow(false);

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
	// Only fills what the land around the road leaves open: deep enough for the grades of the road never to reach it.
	constexpr double GroundDepthCm = 15000.0;
	GroundPlane->SetWorldLocation(FVector(FocusLocationCm.X, FocusLocationCm.Y, FocusLocationCm.Z - GroundDepthCm));
	FarRidges->SetWorldLocation(FocusLocationCm);
}

void ALREnvironmentActor::BuildRidge(int32 Section, float InnerRadiusM, float PeakRadiusM, float BaseHeightM, float AmplitudeM, int32 Seed, UMaterialInterface* Material)
{
	constexpr int32 NumSegments = 360;
	// Share of the way from the foot of the ring to its crest, with the matching share of the height.
	constexpr float RowDistance[] = { 0.f, 0.45f, 0.8f, 1.f };
	constexpr float RowHeight[] = { 0.f, 0.3f, 0.8f, 1.f };
	constexpr int32 NumRows = UE_ARRAY_COUNT(RowDistance);

	const float StepRad = 2.f * PI / NumSegments;
	const auto Point = [&](int32 Segment, int32 Row)
	{
		const float Angle = StepRad * Segment;
		const float Radius = FMath::Lerp(InnerRadiusM, PeakRadiusM, RowDistance[Row]);
		const float Crest = LRTerrainField::GetRidgeHeightM(Angle, BaseHeightM, AmplitudeM, Seed);
		return FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, RowHeight[Row] * Crest - 150.f) * LRUnits::MetersToCm(1.f);
	};

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	Vertices.Reserve((NumSegments + 1) * NumRows);
	Normals.Reserve((NumSegments + 1) * NumRows);
	for (int32 Segment = 0; Segment <= NumSegments; ++Segment)
	{
		for (int32 Row = 0; Row < NumRows; ++Row)
		{
			const FVector AlongRing = Point(Segment + 1, Row) - Point(Segment - 1, Row);
			const FVector UpSlope = Point(Segment, FMath::Min(Row + 1, NumRows - 1)) - Point(Segment, FMath::Max(Row - 1, 0));
			Vertices.Add(Point(Segment, Row));
			Normals.Add(FVector::CrossProduct(UpSlope, AlongRing).GetSafeNormal());
		}
	}
	for (int32 Segment = 0; Segment < NumSegments; ++Segment)
	{
		for (int32 Row = 0; Row + 1 < NumRows; ++Row)
		{
			const int32 A = Segment * NumRows + Row;
			const int32 B = A + NumRows;
			Triangles.Add(A);
			Triangles.Add(A + 1);
			Triangles.Add(B);
			Triangles.Add(A + 1);
			Triangles.Add(B + 1);
			Triangles.Add(B);
		}
	}

	FarRidges->CreateMeshSection_LinearColor(Section, Vertices, Triangles, Normals, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	FarRidges->SetMaterial(Section, Material);
}

void ALREnvironmentActor::ApplyPreset(const FLREnvironmentPreset& Preset)
{
	if (UMaterialInterface* Terrain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LeftRight/Materials/M_LRTerrain.M_LRTerrain")))
	{
		GroundPlane->SetMaterial(0, Terrain);
		if (FarRidges->GetNumSections() == 0)
		{
			BuildRidge(0, 2200.f, 3600.f, 90.f, 260.f, 11, Terrain);
			BuildRidge(1, 6000.f, 11000.f, 350.f, 1300.f, 23, Terrain);
		}
	}

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LeftRight/Materials/M_LRSurface.M_LRSurface"));
	if (Base == nullptr)
	{
		Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (Base != nullptr && GroundPlane->GetMaterial(0) == nullptr)
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
