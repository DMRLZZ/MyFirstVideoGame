// Copyright LEFTRIGHT. All Rights Reserved.

#include "LRRoadStreamer.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "LRRoadPath.h"
#include "LRUnits.h"
#include "LRWorld.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Templates/Function.h"
#include "UObject/ConstructorHelpers.h"

namespace LRRoadStreamerPrivate
{
	/** Mesh section indices of a chunk. */
	constexpr int32 SectionAsphalt = 0;
	constexpr int32 SectionWhitePaint = 1;
	constexpr int32 SectionYellowPaint = 2;
	constexpr int32 SectionBarrier = 3;

	/** Vector parameter of /Engine/BasicShapes/BasicShapeMaterial. */
	const FName ColorParameterName(TEXT("Color"));

	/** UV V coordinates wrap every this many meters (keeps float UV precision on very long runs). */
	constexpr double UVWrapMeters = 1000.0;

	/** Tolerance when checking chunk coverage by the path, meters. */
	constexpr double CoverageTolerance = 1e-3;
}

ALRRoadStreamer::ALRRoadStreamer()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	BaseMaterial = MaterialFinder.Object;
	CubeMesh = CubeFinder.Object;
	CylinderMesh = CylinderFinder.Object;

	// Warm the reusable buffers once (a 100 m chunk sampled every 5 m stays well below these sizes).
	RowS.Reserve(128);
	RowCenter.Reserve(128);
	RowForward.Reserve(128);
	RowRight.Reserve(128);
	RowUp.Reserve(128);
	MissingChunks.Reserve(64);
	Vertices.Reserve(4096);
	Triangles.Reserve(8192);
	Normals.Reserve(4096);
	UV0.Reserve(4096);
	Tangents.Reserve(4096);
	InstanceTransforms.Reserve(64);
}

void ALRRoadStreamer::EnsureMaterials()
{
	if (AsphaltMaterial || !BaseMaterial)
	{
		return;
	}

	AsphaltMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	WhitePaintMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	YellowPaintMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	ConcreteMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	MetalMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
}

void ALRRoadStreamer::Configure(const FLRRouteDefinition& Route)
{
	using namespace LRRoadStreamerPrivate;

	Profile = Route.Profile;
	Dressing = Route.Dressing;
	Streaming = Route.Streaming;
	Streaming.ChunkLengthM = FMath::Max(20.f, Streaming.ChunkLengthM);
	Streaming.MeshSampleStepM = FMath::Clamp(Streaming.MeshSampleStepM, 0.5f, Streaming.ChunkLengthM);
	Streaming.MaxChunkBuildsPerFrame = FMath::Max(1, Streaming.MaxChunkBuildsPerFrame);

	CrossSection = LRRoadLayout::ComputeCrossSection(Profile, Dressing);
	LRRoadLayout::BuildMarkings(Profile, Dressing, Markings);

	EnsureMaterials();
	if (AsphaltMaterial)
	{
		AsphaltMaterial->SetVectorParameterValue(ColorParameterName, Dressing.AsphaltColor);
		WhitePaintMaterial->SetVectorParameterValue(ColorParameterName, Dressing.WhitePaintColor);
		YellowPaintMaterial->SetVectorParameterValue(ColorParameterName, Dressing.YellowPaintColor);
		ConcreteMaterial->SetVectorParameterValue(ColorParameterName, Dressing.ConcreteColor);
		MetalMaterial->SetVectorParameterValue(ColorParameterName, Dressing.MetalColor);
	}
	else
	{
		UE_LOG(LogLRWorld, Warning, TEXT("ALRRoadStreamer: /Engine/BasicShapes/BasicShapeMaterial not found, road uses default material."));
	}

	ReleaseAllChunks();
}

void ALRRoadStreamer::ResetStreaming(const FLRRoadPath& Path, double FocusS)
{
	ReleaseAllChunks();
	UpdateWindow(Path, FocusS, /*bForceFullBuild*/ true);
}

void ALRRoadStreamer::ReleaseAllChunks()
{
	for (FLRRoadChunkSlot& Slot : Slots)
	{
		if (Slot.ChunkIndex != INDEX_NONE)
		{
			ReleaseSlot(Slot);
		}
	}
	LastFirstChunk = TNumericLimits<int64>::Max();
	LastLastChunk = TNumericLimits<int64>::Min();
	bHasPendingChunks = true;
}

int32 ALRRoadStreamer::GetNumActiveChunks() const
{
	int32 Count = 0;
	for (const FLRRoadChunkSlot& Slot : Slots)
	{
		Count += Slot.ChunkIndex != INDEX_NONE ? 1 : 0;
	}
	return Count;
}

bool ALRRoadStreamer::IsChunkBuilt(int64 ChunkIndex) const
{
	for (const FLRRoadChunkSlot& Slot : Slots)
	{
		if (Slot.ChunkIndex == ChunkIndex)
		{
			return true;
		}
	}
	return false;
}

int32 ALRRoadStreamer::AcquireSlot()
{
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (Slots[Index].ChunkIndex == INDEX_NONE)
		{
			return Index;
		}
	}

	// Pool exhausted: create a new set of components (happens only while the pool warms up).
	FLRRoadChunkSlot& Slot = Slots.AddDefaulted_GetRef();

	Slot.Mesh = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
	Slot.Mesh->SetupAttachment(Root);
	Slot.Mesh->SetMobility(EComponentMobility::Movable);
	Slot.Mesh->bUseComplexAsSimpleCollision = true;
	Slot.Mesh->bUseAsyncCooking = true;
	Slot.Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Slot.Mesh->SetCanEverAffectNavigation(false);
	Slot.Mesh->RegisterComponent();

	auto MakeInstanced = [this, &Slot](UStaticMesh* Mesh, UMaterialInterface* Material, bool bCastShadow)
	{
		UInstancedStaticMeshComponent* Instanced = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
		Instanced->SetupAttachment(Slot.Mesh);
		Instanced->SetMobility(EComponentMobility::Movable);
		Instanced->SetStaticMesh(Mesh);
		Instanced->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Instanced->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Instanced->SetCanEverAffectNavigation(false);
		Instanced->SetCastShadow(bCastShadow);
		if (Material)
		{
			Instanced->SetMaterial(0, Material);
		}
		Instanced->RegisterComponent();
		return Instanced;
	};

	Slot.LampPoles = MakeInstanced(CylinderMesh, MetalMaterial, true);
	Slot.LampArms = MakeInstanced(CubeMesh, MetalMaterial, true);
	Slot.Reflectors = MakeInstanced(CylinderMesh, WhitePaintMaterial, false);

	return Slots.Num() - 1;
}

void ALRRoadStreamer::ReleaseSlot(FLRRoadChunkSlot& Slot)
{
	if (Slot.Mesh)
	{
		Slot.Mesh->ClearAllMeshSections();
	}
	if (Slot.LampPoles)
	{
		Slot.LampPoles->ClearInstances();
	}
	if (Slot.LampArms)
	{
		Slot.LampArms->ClearInstances();
	}
	if (Slot.Reflectors)
	{
		Slot.Reflectors->ClearInstances();
	}
	Slot.ChunkIndex = INDEX_NONE;
}

void ALRRoadStreamer::UpdateWindow(const FLRRoadPath& Path, double FocusS, bool bForceFullBuild)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALRRoadStreamer::UpdateWindow);

	if (Path.IsEmpty())
	{
		return;
	}

	const double ChunkLength = Streaming.ChunkLengthM;
	const int64 FirstChunk = static_cast<int64>(FMath::FloorToDouble((FocusS - Streaming.VisibleBehindM) / ChunkLength));
	const int64 LastChunk = static_cast<int64>(FMath::FloorToDouble((FocusS + Streaming.VisibleAheadM) / ChunkLength));
	const int64 FocusChunk = static_cast<int64>(FMath::FloorToDouble(FocusS / ChunkLength));

	if (!bForceFullBuild && !bHasPendingChunks && FirstChunk == LastFirstChunk && LastChunk == LastLastChunk)
	{
		return;
	}
	LastFirstChunk = FirstChunk;
	LastLastChunk = LastChunk;

	// 1. Free chunks that left the window.
	for (FLRRoadChunkSlot& Slot : Slots)
	{
		if (Slot.ChunkIndex != INDEX_NONE && (Slot.ChunkIndex < FirstChunk || Slot.ChunkIndex > LastChunk))
		{
			ReleaseSlot(Slot);
		}
	}

	// 2. Collect missing chunks, nearest to the player first.
	MissingChunks.Reset();
	for (int64 ChunkIndex = FirstChunk; ChunkIndex <= LastChunk; ++ChunkIndex)
	{
		if (!IsChunkBuilt(ChunkIndex))
		{
			MissingChunks.Add(ChunkIndex);
		}
	}
	MissingChunks.Sort([FocusChunk](int64 A, int64 B)
	{
		const int64 DistanceA = A >= FocusChunk ? A - FocusChunk : FocusChunk - A;
		const int64 DistanceB = B >= FocusChunk ? B - FocusChunk : FocusChunk - B;
		return DistanceA != DistanceB ? DistanceA < DistanceB : A < B;
	});

	// 3. Build them (budgeted unless forced).
	const int32 Budget = bForceFullBuild ? MissingChunks.Num() : Streaming.MaxChunkBuildsPerFrame;
	int32 Built = 0;
	int32 NotCovered = 0;
	for (const int64 ChunkIndex : MissingChunks)
	{
		if (Built >= Budget)
		{
			break;
		}

		const double ChunkCenterS = (static_cast<double>(ChunkIndex) + 0.5) * ChunkLength;
		const bool bSynchronous = bForceFullBuild && FMath::Abs(ChunkCenterS - FocusS) <= Streaming.SyncCookRadiusM + 0.5 * ChunkLength;

		const int32 SlotIndex = AcquireSlot();
		if (BuildChunk(Path, Slots[SlotIndex], ChunkIndex, bSynchronous))
		{
			++Built;
		}
		else
		{
			++NotCovered;
		}
	}

	// Chunks outside the generated path stay pending and are retried when the path grows.
	bHasPendingChunks = (MissingChunks.Num() - Built) > 0;
	if (NotCovered > 0 && bForceFullBuild)
	{
		UE_LOG(LogLRWorld, Verbose, TEXT("ALRRoadStreamer: %d chunk(s) not covered by the road path yet."), NotCovered);
	}
}

void ALRRoadStreamer::ResetBuffers()
{
	Vertices.Reset();
	Triangles.Reset();
	Normals.Reset();
	UV0.Reset();
	Colors.Reset();
	Tangents.Reset();
}

void ALRRoadStreamer::FlushSection(UProceduralMeshComponent* Mesh, int32 SectionIndex, bool bCollision, UMaterialInterface* Material)
{
	if (Vertices.Num() == 0)
	{
		Mesh->ClearMeshSection(SectionIndex);
		return;
	}

	Mesh->CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, UV0, Colors, Tangents, bCollision);
	if (Material)
	{
		Mesh->SetMaterial(SectionIndex, Material);
	}
}

void ALRRoadStreamer::SampleRows(const FLRRoadPath& Path, double S0, double S1, const FVector& Origin)
{
	RowS.Reset();
	RowCenter.Reset();
	RowForward.Reset();
	RowRight.Reset();
	RowUp.Reset();

	const int32 NumIntervals = FMath::Max(1, FMath::CeilToInt32((S1 - S0) / Streaming.MeshSampleStepM - 1e-6));
	for (int32 Row = 0; Row <= NumIntervals; ++Row)
	{
		const double S = FMath::Lerp(S0, S1, static_cast<double>(Row) / static_cast<double>(NumIntervals));
		FVector Center, Forward, Right, Up;
		Path.GetFrame(S, Center, Forward, Right, Up);
		RowS.Add(S);
		RowCenter.Add(Center - Origin);
		RowForward.Add(Forward);
		RowRight.Add(Right);
		RowUp.Add(Up);
	}
}

void ALRRoadStreamer::AppendQuadRow(const FVector& A0, const FVector& A1, const FVector& B0, const FVector& B1, const FVector& Normal,
	const FVector& Forward, float U0, float U1, float VA, float VB)
{
	// A = row at the smaller S, B = row at the larger S; index 0 = left point, 1 = right point.
	// Triangles (A0, B0, A1) and (A1, B0, B1) face along Normal in Unreal's (clockwise front face) convention.
	const int32 Base = Vertices.Num();
	Vertices.Add(A0);
	Vertices.Add(A1);
	Vertices.Add(B0);
	Vertices.Add(B1);

	for (int32 Index = 0; Index < 4; ++Index)
	{
		Normals.Add(Normal);
		Tangents.Add(FProcMeshTangent(Forward, false));
	}

	UV0.Add(FVector2D(U0, VA));
	UV0.Add(FVector2D(U1, VA));
	UV0.Add(FVector2D(U0, VB));
	UV0.Add(FVector2D(U1, VB));

	Triangles.Add(Base + 0);
	Triangles.Add(Base + 2);
	Triangles.Add(Base + 1);
	Triangles.Add(Base + 1);
	Triangles.Add(Base + 2);
	Triangles.Add(Base + 3);
}

void ALRRoadStreamer::AppendStripOverRows(float D0, float H0, float D1, float H1, double UVOffsetS)
{
	const int32 NumRows = RowS.Num();
	if (NumRows < 2)
	{
		return;
	}

	const double D0Cm = LRUnits::MetersToCm(D0);
	const double D1Cm = LRUnits::MetersToCm(D1);
	const double H0Cm = LRUnits::MetersToCm(H0);
	const double H1Cm = LRUnits::MetersToCm(H1);
	const float U0 = D0 + H0;
	const float U1 = D1 + H1;

	for (int32 Row = 0; Row + 1 < NumRows; ++Row)
	{
		const FVector A0 = RowCenter[Row] + RowRight[Row] * D0Cm + RowUp[Row] * H0Cm;
		const FVector A1 = RowCenter[Row] + RowRight[Row] * D1Cm + RowUp[Row] * H1Cm;
		const FVector B0 = RowCenter[Row + 1] + RowRight[Row + 1] * D0Cm + RowUp[Row + 1] * H0Cm;
		const FVector B1 = RowCenter[Row + 1] + RowRight[Row + 1] * D1Cm + RowUp[Row + 1] * H1Cm;

		// Front normal = Forward x (P1 - P0): up for the road surface, sideways for barrier faces.
		const FVector Across = (A1 - A0).GetSafeNormal();
		const FVector Normal = FVector::CrossProduct(RowForward[Row], Across).GetSafeNormal();

		AppendQuadRow(A0, A1, B0, B1, Normal, RowForward[Row], U0, U1,
			static_cast<float>(RowS[Row] - UVOffsetS), static_cast<float>(RowS[Row + 1] - UVOffsetS));
	}
}

void ALRRoadStreamer::AppendStripBetween(const FLRRoadPath& Path, double SA, double SB, float D0, float H0, float D1, float H1, const FVector& Origin, double UVOffsetS)
{
	FVector CenterA, ForwardA, RightA, UpA;
	FVector CenterB, ForwardB, RightB, UpB;
	Path.GetFrame(SA, CenterA, ForwardA, RightA, UpA);
	Path.GetFrame(SB, CenterB, ForwardB, RightB, UpB);
	CenterA -= Origin;
	CenterB -= Origin;

	const double D0Cm = LRUnits::MetersToCm(D0);
	const double D1Cm = LRUnits::MetersToCm(D1);
	const double H0Cm = LRUnits::MetersToCm(H0);
	const double H1Cm = LRUnits::MetersToCm(H1);

	const FVector A0 = CenterA + RightA * D0Cm + UpA * H0Cm;
	const FVector A1 = CenterA + RightA * D1Cm + UpA * H1Cm;
	const FVector B0 = CenterB + RightB * D0Cm + UpB * H0Cm;
	const FVector B1 = CenterB + RightB * D1Cm + UpB * H1Cm;
	const FVector Normal = FVector::CrossProduct(ForwardA, (A1 - A0).GetSafeNormal()).GetSafeNormal();

	AppendQuadRow(A0, A1, B0, B1, Normal, ForwardA, D0 + H0, D1 + H1,
		static_cast<float>(SA - UVOffsetS), static_cast<float>(SB - UVOffsetS));
}

bool ALRRoadStreamer::BuildChunk(const FLRRoadPath& Path, FLRRoadChunkSlot& Slot, int64 ChunkIndex, bool bSynchronousCollision)
{
	using namespace LRRoadStreamerPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(ALRRoadStreamer::BuildChunk);

	const double ChunkLength = Streaming.ChunkLengthM;
	const double S0 = static_cast<double>(ChunkIndex) * ChunkLength;
	const double S1 = S0 + ChunkLength;
	if (S0 < Path.GetStartS() - CoverageTolerance || S1 > Path.GetEndS() + CoverageTolerance)
	{
		return false;
	}

	UProceduralMeshComponent* Mesh = Slot.Mesh;
	const FVector Origin = Path.RoadToWorldLocation(S0, 0.f);
	Mesh->SetWorldLocationAndRotation(Origin, FQuat::Identity);
	Mesh->bUseAsyncCooking = !bSynchronousCollision;

	const double UVOffsetS = FMath::FloorToDouble(S0 / UVWrapMeters) * UVWrapMeters;
	SampleRows(Path, S0, S1, Origin);

	// Paint first (no collision), then the collision sections, so collision is cooked as late as possible.

	// White and yellow paint.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const ELRLaneMarkingColor PassColor = Pass == 0 ? ELRLaneMarkingColor::White : ELRLaneMarkingColor::Yellow;
		ResetBuffers();

		for (const FLRLaneMarking& Marking : Markings)
		{
			if (Marking.Color != PassColor)
			{
				continue;
			}

			const float DLeft = Marking.CenterD - 0.5f * Marking.WidthMeters;
			const float DRight = Marking.CenterD + 0.5f * Marking.WidthMeters;

			if (!Marking.bDashed)
			{
				AppendStripOverRows(DLeft, Dressing.PaintHeightM, DRight, Dressing.PaintHeightM, UVOffsetS);
				continue;
			}

			// Dash pattern anchored on absolute S so it is continuous across chunks.
			const double Period = FMath::Max(0.1, static_cast<double>(Dressing.DashLengthM + Dressing.DashGapM));
			for (int64 Dash = static_cast<int64>(FMath::FloorToDouble(S0 / Period)); static_cast<double>(Dash) * Period < S1; ++Dash)
			{
				const double DashStart = FMath::Max(S0, static_cast<double>(Dash) * Period);
				const double DashEnd = FMath::Min(S1, static_cast<double>(Dash) * Period + Dressing.DashLengthM);
				if (DashEnd - DashStart > 1e-3)
				{
					AppendStripBetween(Path, DashStart, DashEnd, DLeft, Dressing.PaintHeightM, DRight, Dressing.PaintHeightM, Origin, UVOffsetS);
				}
			}
		}

		FlushSection(Mesh, Pass == 0 ? SectionWhitePaint : SectionYellowPaint, false, Pass == 0 ? WhitePaintMaterial : YellowPaintMaterial);
	}

	// Concrete barriers on both outer sides (the left one is the median barrier in a one-way profile with median).
	{
		ResetBuffers();
		const float HalfBase = 0.5f * Dressing.BarrierBaseWidthM;
		const float HalfTop = 0.5f * FMath::Min(Dressing.BarrierTopWidthM, Dressing.BarrierBaseWidthM);
		const float Height = Dressing.BarrierHeightM;

		for (const float BarrierD : { CrossSection.LeftBarrierD, CrossSection.RightBarrierD })
		{
			AppendStripOverRows(BarrierD - HalfBase, 0.f, BarrierD - HalfTop, Height, UVOffsetS);
			AppendStripOverRows(BarrierD - HalfTop, Height, BarrierD + HalfTop, Height, UVOffsetS);
			AppendStripOverRows(BarrierD + HalfTop, Height, BarrierD + HalfBase, 0.f, UVOffsetS);
		}

		FlushSection(Mesh, SectionBarrier, true, ConcreteMaterial);
	}

	// Asphalt: lanes, shoulders and the paved strip up to the barriers (collision for the wheel raycasts).
	{
		ResetBuffers();
		AppendStripOverRows(CrossSection.LeftBarrierD, 0.f, CrossSection.RightBarrierD, 0.f, UVOffsetS);
		FlushSection(Mesh, SectionAsphalt, true, AsphaltMaterial);
	}

	BuildRoadside(Path, Slot, S0, S1, Origin);

	Slot.ChunkIndex = ChunkIndex;
	return true;
}

void ALRRoadStreamer::BuildRoadside(const FLRRoadPath& Path, FLRRoadChunkSlot& Slot, double S0, double S1, const FVector& Origin)
{
	Slot.LampPoles->ClearInstances();
	Slot.LampArms->ClearInstances();
	Slot.Reflectors->ClearInstances();

	// Engine basic shapes are 1 m cubes / cylinders centered on their pivot: scale = size in meters.
	auto ForEachMultiple = [S0, S1](double Spacing, double Phase, TFunctionRef<void(double)> Callback)
	{
		if (Spacing <= 0.0)
		{
			return;
		}
		for (int64 Index = static_cast<int64>(FMath::CeilToDouble((S0 - Phase) / Spacing)); ; ++Index)
		{
			const double S = Phase + static_cast<double>(Index) * Spacing;
			if (S >= S1)
			{
				break;
			}
			if (S >= S0)
			{
				Callback(S);
			}
		}
	};

	// Lamp posts: right side at multiples of the spacing, left side staggered by half a spacing.
	const double LampSpacing = Dressing.LampPostSpacingM;
	const float PoleHeight = Dressing.LampPostHeightM;
	const float PoleDiameter = 2.f * Dressing.LampPostRadiusM;
	const float ArmLength = Dressing.LampArmLengthM;
	const float ArmThickness = FMath::Max(0.08f, Dressing.LampPostRadiusM);

	struct FLampSide
	{
		float PoleD;
		float InwardSign;
		double Phase;
	};
	const FLampSide Sides[2] = {
		{ CrossSection.RightBarrierD + Dressing.LampSetbackM, -1.f, 0.0 },
		{ CrossSection.LeftBarrierD - Dressing.LampSetbackM, 1.f, 0.5 * LampSpacing }
	};

	for (const FLampSide& Side : Sides)
	{
		InstanceTransforms.Reset();
		ForEachMultiple(LampSpacing, Side.Phase, [&](double S)
		{
			const FVector Base = Path.RoadToWorldLocation(S, Side.PoleD) - Origin;
			InstanceTransforms.Add(FTransform(FQuat::Identity, Base + FVector(0.0, 0.0, LRUnits::MetersToCm(0.5 * PoleHeight)),
				FVector(PoleDiameter, PoleDiameter, PoleHeight)));
		});
		if (InstanceTransforms.Num() > 0)
		{
			Slot.LampPoles->AddInstances(InstanceTransforms, false, false, false);
		}

		if (ArmLength > 0.f)
		{
			InstanceTransforms.Reset();
			ForEachMultiple(LampSpacing, Side.Phase, [&](double S)
			{
				const float ArmCenterD = Side.PoleD + Side.InwardSign * 0.5f * ArmLength;
				const FVector Center = Path.RoadToWorldLocation(S, ArmCenterD) - Origin
					+ FVector(0.0, 0.0, LRUnits::MetersToCm(PoleHeight - 0.5f * ArmThickness));
				const FRotator Yaw(0.0, Path.GetHeadingYawDegrees(S) + 90.0, 0.0);
				InstanceTransforms.Add(FTransform(Yaw, Center, FVector(ArmLength, ArmThickness, ArmThickness)));
			});
			if (InstanceTransforms.Num() > 0)
			{
				Slot.LampArms->AddInstances(InstanceTransforms, false, false, false);
			}
		}
	}

	// Reflector posts on top of both barriers.
	InstanceTransforms.Reset();
	const float ReflectorDiameter = 2.f * Dressing.ReflectorRadiusM;
	const float ReflectorHeight = Dressing.ReflectorHeightM;
	ForEachMultiple(Dressing.ReflectorSpacingM, 0.0, [&](double S)
	{
		FVector Center, Forward, Right, Up;
		Path.GetFrame(S, Center, Forward, Right, Up);
		const double HeightCm = LRUnits::MetersToCm(Dressing.BarrierHeightM + 0.5f * ReflectorHeight);
		for (const float BarrierD : { CrossSection.LeftBarrierD, CrossSection.RightBarrierD })
		{
			const FVector Location = Center + Right * LRUnits::MetersToCm(BarrierD) + Up * HeightCm - Origin;
			InstanceTransforms.Add(FTransform(FQuat::Identity, Location, FVector(ReflectorDiameter, ReflectorDiameter, ReflectorHeight)));
		}
	});
	if (InstanceTransforms.Num() > 0)
	{
		Slot.Reflectors->AddInstances(InstanceTransforms, false, false, false);
	}
}
