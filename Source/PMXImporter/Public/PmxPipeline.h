// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "PmxTranslator.h"
#include "PmxPipeline.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class UPhysicsAsset;
class USkeletalMesh;
struct FPmxPhysicsCache;

/**
 * PMX Import Pipeline
 *
 * Custom pipeline for importing MikuMikuDance PMX models.
 * Replaces GenericAssetsPipeline with PMX-specific options and logic.
 */
UCLASS(BlueprintType, editinlinenew)
class PMXIMPORTER_API UPmxPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()

public:
	UPmxPipeline();

	//~ Begin UInterchangePipelineBase overrides
	virtual bool IsScripted() override { return false; }
	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override;
	//~ End UInterchangePipelineBase overrides

	// =============================================
	// Common Category
	// =============================================

	/** Pipeline display name shown in import dialog */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (StandAlonePipelineProperty = "True", PipelineInternalEditionData = "True"))
	FString PipelineDisplayName = TEXT("PMX Pipeline");

	/** Scale factor for imported model (PMX uses centimeters, UE uses centimeters but MMD scale is typically smaller) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.01", ClampMax = "1000.0"))
	float Scale = 8.0f;

	/** Use the source file name for the imported asset name */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common")
	bool bUseSourceNameForAsset = true;

	// =============================================
	// Mesh Category
	// =============================================

	/** Import mesh geometry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	bool bImportMesh = true;

	/** Import morph targets (shape keys) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh", meta = (EditCondition = "bImportMesh"))
	bool bImportMorphs = true;

	// =============================================
	// Skeleton Category
	// =============================================

	/** Import bone hierarchy (armature) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeleton")
	bool bImportArmature = true;

	/** Rename left/right bones to UE convention (_L/_R suffix) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeleton", meta = (EditCondition = "bImportArmature"))
	bool bRenameLRBones = false;

	/** Apply IK link fixes for better compatibility */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeleton", meta = (EditCondition = "bImportArmature"))
	bool bFixIKLinks = false;

	/** Apply bone fixed axis constraints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeleton", meta = (EditCondition = "bImportArmature"))
	bool bApplyBoneFixedAxis = false;

	/** Use underscore for bone name separator instead of dot */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeleton", meta = (EditCondition = "bImportArmature && bRenameLRBones"))
	bool bUseUnderscore = false;

	// =============================================
	// Physics Category
	// =============================================

	/** Import physics asset (rigid bodies and constraints) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	bool bImportPhysics = true;

	/** How to handle Physics Type 2 (physics + bone follow) rigid bodies */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics"))
	EPmxPhysicsType2Handling PhysicsType2Mode = EPmxPhysicsType2Handling::ConvertToKinematic;

	/** Scale factor for physics mass */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "100.0"))
	float PhysicsMassScale = 1.0f;

	/** Scale factor for physics damping */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics", ClampMin = "0.0", ClampMax = "10.0"))
	float PhysicsDampingScale = 1.0f;

	/** Scale factor for physics body shapes (sphere, box, capsule radius/size) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0"))
	float PhysicsShapeScale = 1.0f;

	/** Additional scale factor for sphere shapes only (multiplied with PhysicsShapeScale) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0"))
	float PhysicsSphereScale = 1.0f;

	/** Additional scale factor for box shapes only (multiplied with PhysicsShapeScale) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0"))
	float PhysicsBoxScale = 1.0f;

	/** Additional scale factor for capsule shapes only (multiplied with PhysicsShapeScale) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0"))
	float PhysicsCapsuleScale = 1.0f;

	/** Force standard skeletal bones (core body/limbs) to kinematic, ignoring PMX physics type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics"))
	bool bForceStandardBonesKinematic = false;

	/** Force non-standard bones (cloth/hair/accessories) to simulated, ignoring PMX physics type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics"))
	bool bForceNonStandardBonesSimulated = false;

	// =============================================
	// Material Category
	// =============================================

	/** Generate mipmaps for imported textures */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	bool bUseMipmap = true;

	/** Blend factor for sphere map textures (.sph) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SphBlendFactor = 1.0f;

	/** Blend factor for sphere add textures (.spa) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpaBlendFactor = 1.0f;

	// =============================================
	// Advanced Category
	// =============================================

	/** Clean model by removing unused vertices */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	bool bCleanModel = true;

	/** Remove duplicate vertices (weld vertices) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	bool bRemoveDoubles = true;

	/** Mark sharp edges based on angle threshold */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	bool bMarkSharpEdges = true;

	/** Angle threshold for marking sharp edges (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (EditCondition = "bMarkSharpEdges", ClampMin = "0.0", ClampMax = "180.0"))
	float SharpEdgeAngle = 179.0f;

	/** Import additional UV set as vertex colors */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	bool bImportAddUV2AsVertexColors = false;

	/** Import display frame data (less useful for UE) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	bool bImportDisplay = false;

protected:
	//~ Begin UInterchangePipelineBase overrides
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;
	virtual void ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport) override;

#if WITH_EDITOR
	virtual void FilterPropertiesFromTranslatedData(UInterchangeBaseNodeContainer* InBaseNodeContainer) override;
#endif
	//~ End UInterchangePipelineBase overrides

private:
	/** Store PMX options into SourceNode for Translator to read */
	void StoreOptionsToSourceNode(UInterchangeBaseNodeContainer* BaseNodeContainer) const;

	/** Update physics cache with current pipeline options (called after Translator has created cache) */
	void UpdatePhysicsCacheOptions() const;

	/** Create factory nodes for textures */
	void CreateTextureFactoryNodes(UInterchangeBaseNodeContainer* BaseNodeContainer) const;

	/** Create factory nodes for materials */
	void CreateMaterialFactoryNodes(UInterchangeBaseNodeContainer* BaseNodeContainer) const;

	/** Configure factory nodes with import options */
	void ConfigureFactoryNodes(UInterchangeBaseNodeContainer* BaseNodeContainer) const;

	/** Build physics asset from PMX physics data */
	void BuildPmxPhysicsAsset(UPhysicsAsset* PhysicsAsset, USkeletalMesh* SkeletalMesh, const FPmxPhysicsCache& PhysicsData) const;

	/** Cached base node container for post-import access */
	UPROPERTY(Transient)
	TObjectPtr<const UInterchangeBaseNodeContainer> CachedBaseNodeContainer;
};
