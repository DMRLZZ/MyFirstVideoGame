// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRTrafficVehicleActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace LRTrafficVehicleActorPrivate
{
	/** Basic shape meshes are 100 cm cubes / cylinders: scale 1 = 1 meter. */
	constexpr float CmPerMeter = 100.f;

	const FName ColorParameter(TEXT("Color"));

	const FLinearColor GlassColor(0.02f, 0.025f, 0.03f, 1.f);
	const FLinearColor TireColor(0.015f, 0.015f, 0.015f, 1.f);
	const FLinearColor BrakeOffColor(0.08f, 0.005f, 0.005f, 1.f);
	const FLinearColor BrakeOnColor(1.f, 0.02f, 0.01f, 1.f);
	const FLinearColor BlinkerColor(1.f, 0.45f, 0.f, 1.f);
	const FLinearColor DefaultPaint(0.6f, 0.6f, 0.6f, 1.f);

	/** Far away parking spot for pooled actors, centimeters. */
	const FVector PoolLocation(0.0, 0.0, -100000.0);
}

ALRTrafficVehicleActor::ALRTrafficVehicleActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	UStaticMesh* CubeMesh = CubeFinder.Object;
	UStaticMesh* CylinderMesh = CylinderFinder.Object;
	CubeMeshAsset = CubeMesh;
	CylinderMeshAsset = CylinderMesh;
	BaseMaterial = MaterialFinder.Object;

	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	CollisionBox->SetMobility(EComponentMobility::Movable);
	CollisionBox->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	CollisionBox->SetGenerateOverlapEvents(false);
	CollisionBox->SetNotifyRigidBodyCollision(true);
	CollisionBox->SetSimulatePhysics(false);
	CollisionBox->SetBoxExtent(FVector(235.f, 92.5f, 72.5f), false);
	CollisionBox->SetHiddenInGame(true);
	CollisionBox->SetCanEverAffectNavigation(false); // Nothing navigates here, and moving it would refresh the nav octree.
	SetRootComponent(CollisionBox);

	BodyMesh = CreateVisualPart(TEXT("Body"), CubeMesh);
	CabinMesh = CreateVisualPart(TEXT("Cabin"), CubeMesh);

	static const TCHAR* WheelNames[] = { TEXT("WheelFL"), TEXT("WheelFR"), TEXT("WheelRL"), TEXT("WheelRR") };
	for (const TCHAR* Name : WheelNames)
	{
		WheelMeshes.Add(CreateVisualPart(Name, CylinderMesh));
	}

	static const TCHAR* BrakeNames[] = { TEXT("BrakeLightL"), TEXT("BrakeLightR") };
	for (const TCHAR* Name : BrakeNames)
	{
		UStaticMeshComponent* Light = CreateVisualPart(Name, CubeMesh);
		Light->SetCastShadow(false);
		BrakeLightMeshes.Add(Light);
	}

	static const TCHAR* BlinkerNames[] = { TEXT("BlinkerFL"), TEXT("BlinkerRL"), TEXT("BlinkerFR"), TEXT("BlinkerRR") };
	for (const TCHAR* Name : BlinkerNames)
	{
		UStaticMeshComponent* Light = CreateVisualPart(Name, CubeMesh);
		Light->SetCastShadow(false);
		Light->SetVisibility(false);
		BlinkerMeshes.Add(Light);
	}

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

UStaticMeshComponent* ALRTrafficVehicleActor::CreateVisualPart(const FName Name, UStaticMesh* Mesh)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Part->SetupAttachment(CollisionBox);
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	if (Mesh)
	{
		Part->SetStaticMesh(Mesh);
	}
	return Part;
}

void ALRTrafficVehicleActor::EnsureMaterials()
{
	using namespace LRTrafficVehicleActorPrivate;

	if (PaintMaterial || !BaseMaterial)
	{
		return;
	}

	PaintMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	GlassMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	TireMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	BrakeMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	BlinkerMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);

	GlassMaterial->SetVectorParameterValue(ColorParameter, GlassColor);
	TireMaterial->SetVectorParameterValue(ColorParameter, TireColor);
	BrakeMaterial->SetVectorParameterValue(ColorParameter, BrakeOffColor);
	BlinkerMaterial->SetVectorParameterValue(ColorParameter, BlinkerColor);

	BodyMesh->SetMaterial(0, PaintMaterial);
	CabinMesh->SetMaterial(0, GlassMaterial);
	for (UStaticMeshComponent* Wheel : WheelMeshes)
	{
		Wheel->SetMaterial(0, TireMaterial);
	}
	for (UStaticMeshComponent* Light : BrakeLightMeshes)
	{
		Light->SetMaterial(0, BrakeMaterial);
	}
	for (UStaticMeshComponent* Light : BlinkerMeshes)
	{
		Light->SetMaterial(0, BlinkerMaterial);
	}
	bBrakeOn = false;
}

void ALRTrafficVehicleActor::ApplyArchetype(const FLRTrafficArchetype& InArchetype, const FLinearColor& PaintColor)
{
	using namespace LRTrafficVehicleActorPrivate;

	EnsureMaterials();

	if (!InArchetype.ModelMesh.IsNull() && ApplyModel(InArchetype, PaintColor))
	{
		return;
	}
	RestoreGreyBox();

	if (PaintMaterial)
	{
		PaintMaterial->SetVectorParameterValue(ColorParameter, PaintColor);
	}

	// Dimensions in meters.
	const float L = InArchetype.LengthMeters;
	const float W = InArchetype.WidthMeters;
	const float H = InArchetype.HeightMeters;
	const float WheelRadius = FMath::Clamp(H * 0.22f, 0.3f, 0.55f);
	const float WheelWidth = FMath::Clamp(W * 0.13f, 0.2f, 0.4f);
	const float Clearance = WheelRadius * 0.6f;
	const float LowerHeight = FMath::Max(0.3f, H * InArchetype.LowerBodyHeightFraction - Clearance);
	const float CabinHeight = H - Clearance - LowerHeight;
	const float HalfH = 0.5f * H;

	HalfHeightCm = HalfH * CmPerMeter;
	CollisionBox->SetBoxExtent(FVector(0.5f * L, 0.5f * W, HalfH) * CmPerMeter, false);
	CollisionBox->SetMassOverrideInKg(NAME_None, InArchetype.MassKg, true);

	// Relative locations are measured from the box center (HalfH above the ground).
	auto Place = [](UStaticMeshComponent* Part, const FVector& CenterM, const FVector& SizeM, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		Part->SetRelativeLocationAndRotation(CenterM * CmPerMeter, Rotation);
		Part->SetRelativeScale3D(SizeM);
	};

	Place(BodyMesh, FVector(0.f, 0.f, Clearance + 0.5f * LowerHeight - HalfH), FVector(L, W, LowerHeight));

	const bool bHasCabin = CabinHeight > 0.05f;
	CabinMesh->SetVisibility(bHasCabin);
	if (bHasCabin)
	{
		Place(CabinMesh,
			FVector(L * InArchetype.CabinOffsetFraction, 0.f, Clearance + LowerHeight + 0.5f * CabinHeight - HalfH),
			FVector(L * InArchetype.CabinLengthFraction, W * 0.92f, CabinHeight));
	}

	const float AxleX = 0.5f * L - FMath::Max(WheelRadius * 1.3f, L * 0.17f);
	const float WheelY = 0.5f * W - 0.5f * WheelWidth + 0.02f;
	const float WheelZ = WheelRadius - HalfH;
	const FVector WheelSize(2.f * WheelRadius, 2.f * WheelRadius, WheelWidth);
	const FRotator WheelRotation(0.f, 0.f, 90.f);
	const FVector WheelCenters[] = {
		FVector(AxleX, -WheelY, WheelZ), FVector(AxleX, WheelY, WheelZ),
		FVector(-AxleX, -WheelY, WheelZ), FVector(-AxleX, WheelY, WheelZ) };
	for (int32 Index = 0; Index < WheelMeshes.Num(); ++Index)
	{
		Place(WheelMeshes[Index], WheelCenters[Index], WheelSize, WheelRotation);
	}

	PlaceLights(L, W, Clearance + LowerHeight * 0.75f, HalfH);
}

void ALRTrafficVehicleActor::PlaceLights(float LengthM, float WidthM, float HeightZM, float HalfHeightM)
{
	using namespace LRTrafficVehicleActorPrivate;

	const auto Place = [](UStaticMeshComponent* Part, const FVector& CenterM, const FVector& SizeM)
	{
		Part->SetRelativeLocationAndRotation(CenterM * CmPerMeter, FRotator::ZeroRotator);
		Part->SetRelativeScale3D(SizeM);
	};

	const float LightZ = HeightZM - HalfHeightM;
	const float RearX = -0.5f * LengthM - 0.02f;
	const float FrontX = 0.5f * LengthM + 0.02f;
	const float BrakeY = 0.5f * WidthM - 0.25f;
	Place(BrakeLightMeshes[0], FVector(RearX, -BrakeY, LightZ), FVector(0.06f, 0.35f, 0.14f));
	Place(BrakeLightMeshes[1], FVector(RearX, BrakeY, LightZ), FVector(0.06f, 0.35f, 0.14f));

	const float BlinkY = 0.5f * WidthM - 0.1f;
	const FVector BlinkSize(0.07f, 0.16f, 0.1f);
	Place(BlinkerMeshes[0], FVector(FrontX, -BlinkY, LightZ), BlinkSize);
	Place(BlinkerMeshes[1], FVector(RearX - 0.01f, -BlinkY, LightZ + 0.12f), BlinkSize);
	Place(BlinkerMeshes[2], FVector(FrontX, BlinkY, LightZ), BlinkSize);
	Place(BlinkerMeshes[3], FVector(RearX - 0.01f, BlinkY, LightZ + 0.12f), BlinkSize);
}

bool ALRTrafficVehicleActor::ApplyModel(const FLRTrafficArchetype& InArchetype, const FLinearColor& PaintColor)
{
	using namespace LRTrafficVehicleActorPrivate;

	UStaticMesh* Model = Cast<UStaticMesh>(InArchetype.ModelMesh.TryLoad());
	if (Model == nullptr)
	{
		return false;
	}

	if (CurrentModel != Model)
	{
		CurrentModel = Model;
		BodyMesh->SetStaticMesh(Model);
		BodyMesh->EmptyOverrideMaterials();
		ModelPaintMaterial = nullptr;

		const TArray<FStaticMaterial>& Slots = Model->GetStaticMaterials();
		for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
		{
			const FStaticMaterial& Candidate = Slots[Slot];
			if (Candidate.MaterialInterface != nullptr
				&& (Candidate.MaterialSlotName.ToString().Contains(InArchetype.PaintMaterialHint)
					|| Candidate.MaterialInterface->GetName().Contains(InArchetype.PaintMaterialHint)))
			{
				ModelPaintMaterial = UMaterialInstanceDynamic::Create(Candidate.MaterialInterface, this);
				BodyMesh->SetMaterial(Slot, ModelPaintMaterial);
				break;
			}
		}
	}
	if (ModelPaintMaterial)
	{
		ModelPaintMaterial->SetVectorParameterValue(InArchetype.PaintParameter, PaintColor);
	}

	// Uniform scale from the length, everything else follows the real proportions of the model.
	const FBox Bounds = Model->GetBoundingBox();
	const FRotator ModelRotation(0.f, InArchetype.ModelYawDeg, 0.f);
	const FVector RotatedSize = ModelRotation.RotateVector(Bounds.GetSize()).GetAbs();
	const float Scale = (InArchetype.LengthMeters * CmPerMeter) / FMath::Max(static_cast<float>(RotatedSize.X), 1.f);
	const FVector HalfSizeCm = RotatedSize * 0.5f * Scale;

	HalfHeightCm = static_cast<float>(HalfSizeCm.Z);
	CollisionBox->SetBoxExtent(HalfSizeCm, false);
	CollisionBox->SetMassOverrideInKg(NAME_None, InArchetype.MassKg, true);

	// The model origin goes where the bounds center lands on the box center.
	const FVector ModelOffset = -ModelRotation.RotateVector(Bounds.GetCenter()) * Scale;
	BodyMesh->SetRelativeLocationAndRotation(ModelOffset, ModelRotation);
	BodyMesh->SetRelativeScale3D(FVector(Scale));
	CabinMesh->SetVisibility(false);

	UStaticMesh* WheelModel = InArchetype.ModelWheelMesh.IsNull() ? nullptr : Cast<UStaticMesh>(InArchetype.ModelWheelMesh.TryLoad());
	if (WheelModel != CurrentWheelModel || WheelModel == nullptr)
	{
		CurrentWheelModel = WheelModel;
		for (UStaticMeshComponent* Wheel : WheelMeshes)
		{
			Wheel->SetVisibility(WheelModel != nullptr);
			if (WheelModel != nullptr)
			{
				Wheel->SetStaticMesh(WheelModel);
				Wheel->EmptyOverrideMaterials();
			}
		}
	}
	if (WheelModel != nullptr)
	{
		const FVector O = InArchetype.ModelWheelOffsetCm;
		const FVector Centers[] = {
			FVector(O.X, -O.Y, O.Z), FVector(O.X, O.Y, O.Z),
			FVector(O.X - InArchetype.ModelWheelbaseCm, -O.Y, O.Z), FVector(O.X - InArchetype.ModelWheelbaseCm, O.Y, O.Z) };
		for (int32 Index = 0; Index < WheelMeshes.Num(); ++Index)
		{
			// Left wheels (index 0 and 2) face the other way.
			const FRotator WheelRotation(0.f, InArchetype.ModelYawDeg + (Index % 2 == 0 ? 180.f : 0.f), 0.f);
			WheelMeshes[Index]->SetRelativeLocationAndRotation(ModelOffset + ModelRotation.RotateVector(Centers[Index]) * Scale, WheelRotation);
			WheelMeshes[Index]->SetRelativeScale3D(FVector(Scale));
		}
	}

	PlaceLights(static_cast<float>(HalfSizeCm.X * 2.f) / CmPerMeter, static_cast<float>(HalfSizeCm.Y * 2.f) / CmPerMeter,
		static_cast<float>(HalfSizeCm.Z * 2.f) / CmPerMeter * 0.6f, static_cast<float>(HalfSizeCm.Z) / CmPerMeter);
	return true;
}

void ALRTrafficVehicleActor::RestoreGreyBox()
{
	if (CurrentModel == nullptr && CurrentWheelModel == nullptr)
	{
		return;
	}

	CurrentModel = nullptr;
	CurrentWheelModel = nullptr;
	ModelPaintMaterial = nullptr;

	BodyMesh->SetStaticMesh(CubeMeshAsset);
	BodyMesh->EmptyOverrideMaterials();
	BodyMesh->SetMaterial(0, PaintMaterial);
	for (UStaticMeshComponent* Wheel : WheelMeshes)
	{
		Wheel->SetStaticMesh(CylinderMeshAsset);
		Wheel->EmptyOverrideMaterials();
		Wheel->SetMaterial(0, TireMaterial);
		Wheel->SetVisibility(true);
	}
}

void ALRTrafficVehicleActor::Activate(int32 InVehicleId)
{
	VehicleId = InVehicleId;
	bActive = true;
	bWrecked = false;
	bHasLastLocation = false;
	KinematicVelocityCms = FVector::ZeroVector;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
}

void ALRTrafficVehicleActor::Deactivate()
{
	using namespace LRTrafficVehicleActorPrivate;

	if (CollisionBox->IsSimulatingPhysics())
	{
		CollisionBox->SetSimulatePhysics(false);
	}
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	SetActorLocationAndRotation(PoolLocation, FRotator::ZeroRotator, false, nullptr, ETeleportType::ResetPhysics);
	ApplyLights(0, false);

	VehicleId = INDEX_NONE;
	bActive = false;
	bWrecked = false;
	bHasLastLocation = false;
	KinematicVelocityCms = FVector::ZeroVector;
}

void ALRTrafficVehicleActor::UpdateKinematic(const FVector& GroundLocationCm, const FRotator& Rotation, uint8 LightFlags, bool bBlinkOn, float DeltaSeconds)
{
	if (!bActive || bWrecked)
	{
		return;
	}

	const FVector Location = GroundLocationCm + Rotation.RotateVector(FVector(0.f, 0.f, HalfHeightCm));
	if (bHasLastLocation && DeltaSeconds > UE_KINDA_SMALL_NUMBER)
	{
		KinematicVelocityCms = (Location - LastLocation) / DeltaSeconds;
	}

	// First placement teleports; afterwards the kinematic target lets physics infer the velocity for collisions.
	SetActorLocationAndRotation(Location, Rotation, false, nullptr, bHasLastLocation ? ETeleportType::None : ETeleportType::TeleportPhysics);
	LastLocation = Location;
	bHasLastLocation = true;

	ApplyLights(LightFlags, bBlinkOn);
}

void ALRTrafficVehicleActor::ApplyLights(uint8 LightFlags, bool bBlinkOn)
{
	using namespace LRTrafficVehicleActorPrivate;

	const bool bBrake = (LightFlags & LRTrafficLights::Brake) != 0;
	if (bBrake != bBrakeOn && BrakeMaterial)
	{
		BrakeMaterial->SetVectorParameterValue(ColorParameter, bBrake ? BrakeOnColor : BrakeOffColor);
		bBrakeOn = bBrake;
	}
	if (CurrentModel != nullptr)
	{
		BrakeLightMeshes[0]->SetVisibility(bBrake);
		BrakeLightMeshes[1]->SetVisibility(bBrake);
	}
	else if (!BrakeLightMeshes[0]->IsVisible())
	{
		BrakeLightMeshes[0]->SetVisibility(true);
		BrakeLightMeshes[1]->SetVisibility(true);
	}

	// Bits: 0 = left pair, 1 = right pair.
	uint8 Blinkers = 0;
	if (bBlinkOn)
	{
		Blinkers |= (LightFlags & LRTrafficLights::BlinkerLeft) ? 1 : 0;
		Blinkers |= (LightFlags & LRTrafficLights::BlinkerRight) ? 2 : 0;
	}
	if (Blinkers != VisibleBlinkers)
	{
		const bool bLeft = (Blinkers & 1) != 0;
		const bool bRight = (Blinkers & 2) != 0;
		BlinkerMeshes[0]->SetVisibility(bLeft);
		BlinkerMeshes[1]->SetVisibility(bLeft);
		BlinkerMeshes[2]->SetVisibility(bRight);
		BlinkerMeshes[3]->SetVisibility(bRight);
		VisibleBlinkers = Blinkers;
	}
}

void ALRTrafficVehicleActor::EnterWreckMode(FVector Impulse)
{
	if (!bActive || bWrecked)
	{
		return;
	}
	bWrecked = true;

	ApplyLights(LRTrafficLights::Brake, false);
	CollisionBox->SetSimulatePhysics(true);
	CollisionBox->SetPhysicsLinearVelocity(KinematicVelocityCms);
	CollisionBox->AddImpulse(Impulse);
}
