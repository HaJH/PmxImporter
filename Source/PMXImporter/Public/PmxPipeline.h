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

	/** Pipeline display name shown in import dialog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (StandAlonePipelineProperty = "True", PipelineInternalEditionData = "True", ToolTip = "Pipeline display name shown in import dialog"))
	FString PipelineDisplayName = TEXT("PMX Pipeline");

	/** Scale factor for imported model. PMX uses centimeters, UE uses centimeters but MMD scale is typically smaller. Default 8.0 adjusts MMD scale to UE. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0.01", ClampMax = "1000.0", ToolTip = "Scale factor for imported model (default: 8.0)"))
	float Scale = 8.0f;

	/** Use the source file name for the imported asset name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ToolTip = "Use the source file name for the imported asset name"))
	bool bUseSourceNameForAsset = true;

	// =============================================
	// Mesh Category
	// =============================================

	/** Import mesh geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh", meta = (ToolTip = "Import mesh geometry"))
	bool bImportMesh = true;

	/** Import morph targets (shape keys/blend shapes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh", meta = (EditCondition = "bImportMesh", ToolTip = "Import morph targets (shape keys)"))
	bool bImportMorphs = true;

	// =============================================
	// Mesh|Build Category (NEW)
	// =============================================

	/** Recompute normals from mesh geometry. Enable for better shading on deformed meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh|Build", meta = (EditCondition = "bImportMesh", ToolTip = "Recompute normals from mesh geometry"))
	bool bRecomputeNormals = true;

	/** Recompute tangents from mesh geometry. Enable for better normal mapping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh|Build", meta = (EditCondition = "bImportMesh", ToolTip = "Recompute tangents from mesh geometry"))
	bool bRecomputeTangents = true;

	/** Use MikkTSpace for tangent generation. Standard method for better compatibility across tools. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh|Build", meta = (EditCondition = "bImportMesh && bRecomputeTangents", ToolTip = "Use MikkTSpace for tangent generation"))
	bool bUseMikkTSpace = true;

	// =============================================
	// Skeleton Category
	// =============================================

	/** Import bone hierarchy (armature). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeleton", meta = (ToolTip = "Import bone hierarchy (armature)"))
	bool bImportArmature = true;

	/** Rename left/right bones to UE convention (_L/_R suffix). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeleton", meta = (EditCondition = "bImportArmature", ToolTip = "Rename left/right bones to UE convention (_L/_R suffix)"))
	bool bRenameLRBones = false;

	/** Apply IK link fixes for better compatibility. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeleton", meta = (EditCondition = "bImportArmature", ToolTip = "Apply IK link fixes for better compatibility"))
	bool bFixIKLinks = false;

	/** Apply bone fixed axis constraints. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeleton", meta = (EditCondition = "bImportArmature", ToolTip = "Apply bone fixed axis constraints"))
	bool bApplyBoneFixedAxis = false;

	/** Use underscore for bone name separator instead of dot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeleton", meta = (EditCondition = "bImportArmature && bRenameLRBones", ToolTip = "Use underscore for bone name separator instead of dot"))
	bool bUseUnderscore = false;

	// =============================================
	// Physics Category (Basic Options)
	// =============================================

	/** Import physics asset (rigid bodies and constraints). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ToolTip = "Import physics asset (rigid bodies and constraints)"))
	bool bImportPhysics = true;

	/** How to handle Physics Type 2 (physics + bone follow) rigid bodies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics", ToolTip = "How to handle Physics Type 2 (physics + bone follow) rigid bodies"))
	EPmxPhysicsType2Handling PhysicsType2Mode = EPmxPhysicsType2Handling::ConvertToKinematic;

	/** Scale factor for physics mass (lower = lighter, more fluid movement). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "100.0", ToolTip = "Scale factor for physics mass (lower = lighter, more fluid movement)"))
	float PhysicsMassScale = 0.2f;

	/** Scale factor for physics damping (lower = more bouncy/swaying, slower settling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics", ClampMin = "0.0", ClampMax = "10.0", ToolTip = "Scale factor for physics damping (lower = more bouncy/swaying)"))
	float PhysicsDampingScale = 0.5f;

	/** Force standard skeletal bones (core body/limbs) to kinematic, ignoring PMX physics type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics", ToolTip = "Force standard skeletal bones to kinematic"))
	bool bForceStandardBonesKinematic = true;

	/** Force non-standard bones (cloth/hair/accessories) to simulated, ignoring PMX physics type. Only applies to bodies connected to constraints. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (EditCondition = "bImportPhysics", ToolTip = "Force non-standard bones to simulated"))
	bool bForceNonStandardBonesSimulated = false;

	// =============================================
	// Physics|Advanced Category (Shape, Collision, Constraint)
	// =============================================

	/** Scale factor for physics body shapes (sphere, box, capsule radius/size). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0", ToolTip = "Scale factor for physics body shapes"))
	float PhysicsShapeScale = 1.0f;

	/** Additional scale factor for sphere shapes only (multiplied with PhysicsShapeScale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0", ToolTip = "Additional scale for sphere shapes"))
	float PhysicsSphereScale = 1.0f;

	/** Additional scale factor for box shapes only (multiplied with PhysicsShapeScale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0", ToolTip = "Additional scale for box shapes"))
	float PhysicsBoxScale = 1.0f;

	/** Additional scale factor for capsule shapes only (multiplied with PhysicsShapeScale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0", ToolTip = "Additional scale for capsule shapes"))
	float PhysicsCapsuleScale = 1.0f;

	/** Disable collision between bodies connected by constraints (prevents stiff behavior from overlapping bodies). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics", ToolTip = "Disable collision between constrained bodies"))
	bool bDisableConstraintBodyCollision = true;

	/** Use PMX collision group/mask settings for filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics", ToolTip = "Use PMX collision group/mask settings"))
	bool bUsePmxCollisionGroups = true;

	/** Enable collision between standard bones (body/limbs) and non-standard bones (cloth/hair/accessories). Warning: Once penetrated, cloth may stay inverted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics", ToolTip = "Enable collision between standard and non-standard bones"))
	bool bEnableStandardNonStandardCollision = false;

	/** Constraint configuration mode - Use PMX settings or override all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics", ToolTip = "Constraint configuration mode"))
	EPmxConstraintMode ConstraintMode = EPmxConstraintMode::UsePmxSettings;

	/** Scale factor for constraint spring stiffness (lower = softer, more flowing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0", ToolTip = "Scale for constraint spring stiffness"))
	float ConstraintStiffnessScale = 0.2f;

	/** Scale factor for constraint spring damping (lower = slower settling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics", ClampMin = "0.0", ClampMax = "10.0", ToolTip = "Scale for constraint spring damping"))
	float ConstraintDampingScale = 0.3f;

	/** Maximum angular limit for constraints in degrees. Higher values = more flexible movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::UsePmxSettings", ClampMin = "0.1", ClampMax = "180.0", ToolTip = "Maximum angular limit in degrees"))
	float MaxAngularLimit = 15.0f;

	/** Force all linear motion to Locked regardless of PMX settings. Prevents spring-like stretching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::UsePmxSettings", ToolTip = "Force all linear motion to Locked"))
	bool bForceAllLinearMotionLocked = true;

	/** Disable linear spring drive (prevents spring-like stretching). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics", ToolTip = "Disable linear spring drive"))
	bool bDisableLinearSpringDrive = true;

	/** Linear motion tolerance (cm). Values below this are treated as Locked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::UsePmxSettings && !bForceAllLinearMotionLocked", ClampMin = "0.0", ClampMax = "10.0", ToolTip = "Linear motion tolerance in cm"))
	float LinearMotionTolerance = 1.0f;

	/** [Override Mode] Lock all linear motion (translation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::OverrideAll", ToolTip = "[Override] Lock all linear motion"))
	bool bLockAllLinearMotion = true;

	/** [Override Mode] Angular motion type for all constraints. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::OverrideAll", ToolTip = "[Override] Angular motion type"))
	TEnumAsByte<EAngularConstraintMotion> OverrideAngularMotion = EAngularConstraintMotion::ACM_Limited;

	/** [Override Mode] Swing1 limit in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::OverrideAll", ClampMin = "0.0", ClampMax = "180.0", ToolTip = "[Override] Swing1 limit in degrees"))
	float OverrideSwing1Limit = 5.0f;

	/** [Override Mode] Swing2 limit in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::OverrideAll", ClampMin = "0.0", ClampMax = "180.0", ToolTip = "[Override] Swing2 limit in degrees"))
	float OverrideSwing2Limit = 5.0f;

	/** [Override Mode] Twist limit in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::OverrideAll", ClampMin = "0.0", ClampMax = "180.0", ToolTip = "[Override] Twist limit in degrees"))
	float OverrideTwistLimit = 5.0f;

	/** Use soft constraint (smoother limits with stiffness/damping). May cause stretching at high velocities. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics", ToolTip = "Use soft constraint"))
	bool bUseSoftConstraint = false;

	/** Soft constraint stiffness (spring strength). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics && bUseSoftConstraint", ClampMin = "0.0", ClampMax = "10000.0", ToolTip = "Soft constraint stiffness"))
	float SoftConstraintStiffness = 50.0f;

	/** Soft constraint damping (resistance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Advanced", meta = (EditCondition = "bImportPhysics && bUseSoftConstraint", ClampMin = "0.0", ClampMax = "100.0", ToolTip = "Soft constraint damping"))
	float SoftConstraintDamping = 5.0f;


	// =============================================
	// Material Category
	// =============================================

	/** Parent material for material instances. Uses the PMX base material from plugin content. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Material", meta = (ToolTip = "Parent material for material instances (read-only)"))
	FSoftObjectPath ParentMaterial = FSoftObjectPath(TEXT("/PMXImporter/M_PMX_Base.M_PMX_Base"));

	/** Generate mipmaps for imported textures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (ToolTip = "Generate mipmaps for imported textures"))
	bool bUseMipmap = true;

	/** Blend factor for sphere map textures (.sph). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (ClampMin = "0.0", ClampMax = "1.0", ToolTip = "Blend factor for sphere map textures (.sph)"))
	float SphBlendFactor = 1.0f;

	/** Blend factor for sphere add textures (.spa). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (ClampMin = "0.0", ClampMax = "1.0", ToolTip = "Blend factor for sphere add textures (.spa)"))
	float SpaBlendFactor = 1.0f;

	// =============================================
	// Advanced Category
	// =============================================

	/** Clean model by removing unused vertices. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (ToolTip = "Clean model by removing unused vertices"))
	bool bCleanModel = true;

	/** Remove duplicate vertices (weld vertices). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (ToolTip = "Remove duplicate vertices (weld vertices)"))
	bool bRemoveDoubles = true;

	/** Mark sharp edges based on angle threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (ToolTip = "Mark sharp edges based on angle threshold"))
	bool bMarkSharpEdges = true;

	/** Angle threshold for marking sharp edges (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (EditCondition = "bMarkSharpEdges", ClampMin = "0.0", ClampMax = "180.0", ToolTip = "Angle threshold for marking sharp edges in degrees"))
	float SharpEdgeAngle = 179.0f;

	/** Import additional UV set as vertex colors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (ToolTip = "Import additional UV set as vertex colors"))
	bool bImportAddUV2AsVertexColors = false;

	/** Import display frame data (less useful for UE). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (ToolTip = "Import display frame data (less useful for UE)"))
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
