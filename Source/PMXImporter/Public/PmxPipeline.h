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

	// =============================================
	// Physics|Body Category
	// =============================================

	/** How to handle Physics Type 2 (physics + bone follow) rigid bodies */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Body", meta = (EditCondition = "bImportPhysics"))
	EPmxPhysicsType2Handling PhysicsType2Mode = EPmxPhysicsType2Handling::ConvertToKinematic;

	/** Scale factor for physics mass (lower = lighter, more fluid movement) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Body", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "100.0"))
	float PhysicsMassScale = 0.2f;

	/** Scale factor for physics damping (lower = more bouncy/swaying, slower settling) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Body", meta = (EditCondition = "bImportPhysics", ClampMin = "0.0", ClampMax = "10.0"))
	float PhysicsDampingScale = 0.5f;

	/** Force standard skeletal bones (core body/limbs) to kinematic, ignoring PMX physics type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Body", meta = (EditCondition = "bImportPhysics"))
	bool bForceStandardBonesKinematic = true;

	/** Force non-standard bones (cloth/hair/accessories) to simulated, ignoring PMX physics type. Only applies to bodies connected to constraints. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Body", meta = (EditCondition = "bImportPhysics"))
	bool bForceNonStandardBonesSimulated = false;

	// =============================================
	// Physics|Shape Category
	// =============================================

	/** Scale factor for physics body shapes (sphere, box, capsule radius/size) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Shape", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0"))
	float PhysicsShapeScale = 1.0f;

	/** Additional scale factor for sphere shapes only (multiplied with PhysicsShapeScale) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Shape", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0"))
	float PhysicsSphereScale = 1.0f;

	/** Additional scale factor for box shapes only (multiplied with PhysicsShapeScale) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Shape", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0"))
	float PhysicsBoxScale = 1.0f;

	/** Additional scale factor for capsule shapes only (multiplied with PhysicsShapeScale) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Shape", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0"))
	float PhysicsCapsuleScale = 1.0f;

	// =============================================
	// Physics|Collision Category
	// =============================================

	/** Disable collision between bodies connected by constraints (prevents stiff behavior from overlapping bodies) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Collision", meta = (EditCondition = "bImportPhysics"))
	bool bDisableConstraintBodyCollision = true;

	/** Use PMX collision group/mask settings for filtering (disable collision between bodies with matching NonCollisionGroup) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Collision", meta = (EditCondition = "bImportPhysics"))
	bool bUsePmxCollisionGroups = true;

	/** Enable collision between standard bones (body/limbs) and non-standard bones (cloth/hair/accessories). Warning: Once penetrated, cloth may stay inverted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Collision", meta = (EditCondition = "bImportPhysics"))
	bool bEnableStandardNonStandardCollision = false;

	// =============================================
	// Physics|Constraint Category
	// =============================================

	/** Constraint 설정 모드 - PMX 설정 사용 또는 일괄 덮어쓰기 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics"))
	EPmxConstraintMode ConstraintMode = EPmxConstraintMode::UsePmxSettings;

	/** Scale factor for constraint spring stiffness (lower = softer, more flowing) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics", ClampMin = "0.01", ClampMax = "10.0"))
	float ConstraintStiffnessScale = 0.2f;

	/** Scale factor for constraint spring damping (lower = slower settling) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics", ClampMin = "0.0", ClampMax = "10.0"))
	float ConstraintDampingScale = 0.3f;

	/** Maximum angular limit for constraints in degrees (clamps PMX rotation limits). Higher values = more flexible movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::UsePmxSettings", ClampMin = "0.1", ClampMax = "180.0"))
	float MaxAngularLimit = 15.0f;

	/** Force all linear motion to Locked regardless of PMX settings. Completely prevents spring-like stretching of hair/cloth chains. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::UsePmxSettings"))
	bool bForceAllLinearMotionLocked = true;

	/** Disable linear spring drive (prevents spring-like stretching caused by SpringMoveCoefficient). Should be enabled with bForceAllLinearMotionLocked for best results. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics"))
	bool bDisableLinearSpringDrive = true;

	/** Linear motion tolerance (cm) - MoveRestriction values below this are treated as Locked. Only used when bForceAllLinearMotionLocked is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::UsePmxSettings && !bForceAllLinearMotionLocked", ClampMin = "0.0", ClampMax = "10.0"))
	float LinearMotionTolerance = 1.0f;

	/** [Override Mode] Lock all linear motion (translation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::OverrideAll"))
	bool bLockAllLinearMotion = true;

	/** [Override Mode] Angular motion type for all constraints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::OverrideAll"))
	TEnumAsByte<EAngularConstraintMotion> OverrideAngularMotion = EAngularConstraintMotion::ACM_Limited;

	/** [Override Mode] Swing1 limit in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::OverrideAll", ClampMin = "0.0", ClampMax = "180.0"))
	float OverrideSwing1Limit = 5.0f;

	/** [Override Mode] Swing2 limit in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::OverrideAll", ClampMin = "0.0", ClampMax = "180.0"))
	float OverrideSwing2Limit = 5.0f;

	/** [Override Mode] Twist limit in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics && ConstraintMode == EPmxConstraintMode::OverrideAll", ClampMin = "0.0", ClampMax = "180.0"))
	float OverrideTwistLimit = 5.0f;

	/** Use soft constraint (smoother limits with stiffness/damping). May cause stretching at high velocities. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics"))
	bool bUseSoftConstraint = false;

	/** Soft constraint stiffness (spring strength) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics && bUseSoftConstraint", ClampMin = "0.0", ClampMax = "10000.0"))
	float SoftConstraintStiffness = 50.0f;

	/** Soft constraint damping (resistance) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics|Constraint", meta = (EditCondition = "bImportPhysics && bUseSoftConstraint", ClampMin = "0.0", ClampMax = "100.0"))
	float SoftConstraintDamping = 5.0f;


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
