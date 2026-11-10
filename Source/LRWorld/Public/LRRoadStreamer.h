// Copyright LEFTRIGHT. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LRRoadLayout.h"
#include "LRRoadTypes.h"
#include "ProceduralMeshComponent.h"

#include "LRRoadStreamer.generated.h"

class FLRRoadPath;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

/** One pooled road chunk: a procedural mesh (surface, paint, barriers) plus instanced roadside objects. */
USTRUCT()
struct FLRRoadChunkSlot
{
	GENERATED_BODY()

	/** Asphalt (collision), white paint, yellow paint and concrete barriers (collision), one section each. */
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Mesh;

	/** Land on both sides of the chunk. No collision and no shadows. */
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> TerrainMesh;

	/** Vertical lamp poles (engine cylinder). */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> LampPoles;

	/** Lamp arms reaching over the road (engine cube). */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> LampArms;

	/** Reflector posts on top of the barriers (engine cylinder). */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Reflectors;

	/** One instanced component per kind of roadside object, in the order of the route's props. */
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Props;

	/** Chunk index currently built in this slot (chunk i covers [i * L, (i + 1) * L)), INDEX_NONE when free. */
	int64 ChunkIndex = INDEX_NONE;
};

/**
 * Streams greybox road geometry around the player from an FLRRoadPath.
 *
 * The road is cut in fixed chunks of ChunkLengthM along S. Chunks entering the window
 * [FocusS - VisibleBehindM, FocusS + VisibleAheadM] are built once into pooled components; chunks leaving it are
 * cleared and their components reused. Nothing is rebuilt while a chunk stays in the window.
 *
 * Each chunk component is placed at the chunk start (vertices are local) to keep float precision far from the
 * origin. Materials are dynamic instances of /Engine/BasicShapes/BasicShapeMaterial ("Color" parameter).
 * Spawned and driven by ULRRoadSubsystem; no gameplay logic.
 */
UCLASS(NotBlueprintable)
class LRWORLD_API ALRRoadStreamer : public AActor
{
	GENERATED_BODY()

public:
	ALRRoadStreamer();

	/** Stores the route profile / dressing / streaming params, updates material colors and frees every chunk. */
	void Configure(const FLRRouteDefinition& Route);

	/** Frees every chunk and immediately builds the full window around FocusS (collision near FocusS cooks synchronously). */
	void ResetStreaming(const FLRRoadPath& Path, double FocusS);

	/** Frees chunks outside the window and builds missing ones (at most MaxChunkBuildsPerFrame unless bForceFullBuild). */
	void UpdateWindow(const FLRRoadPath& Path, double FocusS, bool bForceFullBuild = false);

	/** Frees every chunk (components stay pooled). */
	void ReleaseAllChunks();

	/** Number of chunks currently built. */
	int32 GetNumActiveChunks() const;

	/** Number of pooled chunk slots (built or free). */
	int32 GetNumPooledChunks() const { return Slots.Num(); }

	/** Cross-section used for the geometry. */
	const FLRRoadCrossSection& GetCrossSection() const { return CrossSection; }

private:
	/** Creates the dynamic materials once. */
	void EnsureMaterials();

	/** Returns a free slot, creating components if the pool is exhausted. */
	int32 AcquireSlot();

	/** Clears the geometry of a slot and marks it free. */
	void ReleaseSlot(FLRRoadChunkSlot& Slot);

	/** Returns true if the chunk is built in some slot. */
	bool IsChunkBuilt(int64 ChunkIndex) const;

	/** Builds the geometry of ChunkIndex into Slot. Returns false if the path does not cover the chunk yet. */
	bool BuildChunk(const FLRRoadPath& Path, FLRRoadChunkSlot& Slot, int64 ChunkIndex, bool bSynchronousCollision);

	/** Fills the per-row frame cache for [S0, S1] relative to Origin. */
	void SampleRows(const FLRRoadPath& Path, double S0, double S1, const FVector& Origin);

	/** Appends an extruded strip between lateral points (D0, H0) -> (D1, H1) over every cached row. */
	void AppendStripOverRows(float D0, float H0, float D1, float H1, double UVOffsetS);

	/** Appends an extruded strip between two arbitrary S values (dashes). */
	void AppendStripBetween(const FLRRoadPath& Path, double SA, double SB, float D0, float H0, float D1, float H1, const FVector& Origin, double UVOffsetS);

	/** Appends one quad strip from two rows of points. */
	void AppendQuadRow(const FVector& A0, const FVector& A1, const FVector& B0, const FVector& B1, const FVector& Normal,
		const FVector& Forward, float U0, float U1, float VA, float VB);

	/** Clears the reusable mesh buffers. */
	void ResetBuffers();

	/** Uploads the reusable buffers as a mesh section. */
	void FlushSection(UProceduralMeshComponent* Mesh, int32 SectionIndex, bool bCollision, UMaterialInterface* Material);

	/** Fills the land mesh on both sides of a chunk, from the cached rows. */
	void BuildTerrain(FLRRoadChunkSlot& Slot, const FVector& Origin);

	/** Places the trees, rocks and structures of a chunk. */
	void BuildProps(const FLRRoadPath& Path, FLRRoadChunkSlot& Slot, int64 ChunkIndex, double S0, double S1, const FVector& Origin);

	/** True when a scattered object at S, DistanceM away from the barrier, would land in the clearing of a structure. */
	bool IsInStructureClearing(double S, float DistanceM) const;

	/** Fills the instanced roadside objects of a chunk. */
	void BuildRoadside(const FLRRoadPath& Path, FLRRoadChunkSlot& Slot, double S0, double S1, const FVector& Origin);

	UPROPERTY(VisibleAnywhere, Category = "Road")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TArray<FLRRoadChunkSlot> Slots;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AsphaltMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WhitePaintMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> YellowPaintMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ConcreteMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MetalMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TerrainMaterial;

	FLRRoadProfile Profile;
	FLRRoadDressing Dressing;
	FLRRoadStreamingParams Streaming;
	FLRRoadCrossSection CrossSection;
	FLRTerrainParams Terrain;
	TArray<float> TerrainColumns;
	/** Objects of the route, already sanitized. */
	TArray<FLRRoadsideProp> Props;

	/** Meshes of Props, loaded once; null when a path is wrong. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> PropMeshes;
	TArray<FLRLaneMarking> Markings;

	/** Window of the last update, used to skip work when nothing changed. */
	int64 LastFirstChunk = TNumericLimits<int64>::Max();
	int64 LastLastChunk = TNumericLimits<int64>::Min();
	bool bHasPendingChunks = true;

	/** Reusable buffers (no allocation once warmed up). */
	TArray<double> RowS;
	TArray<FVector> RowCenter;
	TArray<FVector> RowForward;
	TArray<FVector> RowRight;
	TArray<FVector> RowUp;
	TArray<int64> MissingChunks;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<FTransform> InstanceTransforms;
};
