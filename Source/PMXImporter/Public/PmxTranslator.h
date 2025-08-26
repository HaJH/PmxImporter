// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "InterchangeTranslatorBase.h"
#include "Mesh/InterchangeMeshPayloadInterface.h"
#include "Texture/InterchangeTexturePayloadInterface.h"
#include "PmxTranslator.generated.h"

class UInterchangeSourceData;
class UInterchangeBaseNodeContainer;
class UInterchangeSceneNode;
class UInterchangeSkeletonFactoryNode;
struct FPmxModel;

USTRUCT()
struct FPmxImportOptions
{
    GENERATED_BODY()

    // Import types
    UPROPERTY()
    bool bImportMesh = true;
    
    UPROPERTY()
    bool bImportArmature = true;
    
    UPROPERTY()
    bool bImportPhysics = true;
    
    UPROPERTY()
    bool bImportMorphs = true;
    
    UPROPERTY()
    bool bImportDisplay = false; // Less critical for UE
    
    // Data cleaning options
    UPROPERTY()
    bool bCleanModel = true;
    
    UPROPERTY()
    bool bRemoveDoubles = true;
    
    // Scale and coordinate options
    UPROPERTY()
    float Scale = 8.0f;
    
    // Edge and normal options
    UPROPERTY()
    bool bMarkSharpEdges = true;
    
    UPROPERTY()
    float SharpEdgeAngle = 179.0f; // degrees
    
    // Additional UV options
    UPROPERTY()
    bool bImportAddUV2AsVertexColors = false;
    
    // Material options
    UPROPERTY()
    bool bUseMipmap = true;
    
    UPROPERTY()
    float SphBlendFactor = 1.0f;
    
    UPROPERTY()
    float SpaBlendFactor = 1.0f;
    
    // Bone options
    UPROPERTY()
    bool bFixIKLinks = false;
    
    UPROPERTY()
    bool bApplyBoneFixedAxis = false;
    
    UPROPERTY()
    bool bRenameLRBones = false;
    
    UPROPERTY()
    bool bUseUnderscore = false;
};

UCLASS()
class PMXIMPORTER_API UPmxTranslator : public UInterchangeTranslatorBase, public IInterchangeMeshPayloadInterface, public IInterchangeTexturePayloadInterface
{
    GENERATED_BODY()
    
public:
    virtual TArray<FString> GetSupportedFormats() const override
    {
        return { TEXT("pmx") };
    }

    virtual EInterchangeTranslatorType GetTranslatorType() const override
    {
        return EInterchangeTranslatorType::Scenes;
    }

    virtual EInterchangeTranslatorAssetType GetSupportedAssetTypes() const override
    {
        return EInterchangeTranslatorAssetType::Meshes;
    }

    virtual bool CanImportSourceData(const UInterchangeSourceData* InSourceData) const override;
    virtual bool Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const override;

    virtual TOptional<UE::Interchange::FMeshPayloadData> GetMeshPayloadData(const FInterchangeMeshPayLoadKey& PayLoadKey, const UE::Interchange::FAttributeStorage& PayloadAttributes) const override;
    virtual TOptional<UE::Interchange::FImportImage> GetTexturePayloadData(const FString& PayloadKey, TOptional<FString>& AlternateTexturePath) const override;

    // Static cache for PMX geometry per import session
    static TMap<FString, TSharedPtr<FPmxModel>> MeshPayloadCache;

private:
    // Import options
    mutable FPmxImportOptions ImportOptions;
    
    // Core execution method
    bool ExecutePmxImport(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const;
    
    // Data cleaning methods
    void CleanPmxModel(FPmxModel& PmxModel, bool bMeshOnly) const;
    void RemoveDoubles(FPmxModel& PmxModel, bool bMeshOnly, TMap<int32, int32>& OutVertexMap) const;
    
    // Section import methods
    void ImportMeshSection(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer, 
                          const TMap<int32, int32>& VertexMap, const FString& InSkeletonUid, TArray<FString>& OutMaterialUids, FString& OutMeshUid, FString& OutSkeletalMeshUid) const;
    void ImportArmatureSection(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer, 
                              FString& OutRootJointUid, FString& OutSkeletonUid) const;
    void ImportPhysicsSection(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer, 
                             const FString& SkeletonUid, const FString& SkeletalMeshUid) const;
    void ImportMorphsSection(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer, 
                            const FString& MeshUid) const;
    void ImportDisplaySection(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const;
    
    // Helper methods for mesh import
    void ImportTextures(const FPmxModel& PmxModel, const FString& PmxFilePath, 
                       UInterchangeBaseNodeContainer& BaseNodeContainer, TMap<int32, FString>& OutTextureUidMap) const;
    void ImportVertices(const FPmxModel& PmxModel, const TMap<int32, int32>& VertexMap) const;
    void ImportFaces(const FPmxModel& PmxModel, const TMap<int32, int32>& VertexMap) const;
    void ImportMaterials(const FPmxModel& PmxModel, const TMap<int32, FString>& TextureUidMap, 
                        UInterchangeBaseNodeContainer& BaseNodeContainer, TArray<FString>& OutMaterialUids, TArray<FString>& OutSlotNames) const;
    
    // Helper methods for armature import  
    void CreateBoneHierarchy(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer,
                           UInterchangeSceneNode* RootNode, TMap<int32, UInterchangeSceneNode*>& OutBoneNodes, FString& OutRootJointUid) const;
    void ApplyIKConstraints(const FPmxModel& PmxModel, const TMap<int32, UInterchangeSceneNode*>& BoneNodes) const;
    void ApplyBoneTransforms(const FPmxModel& PmxModel, const TMap<int32, UInterchangeSceneNode*>& BoneNodes) const;
    
    // Helper methods for physics import
    void ImportRigidBodies(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer, 
                          const TMap<int32, UInterchangeSceneNode*>& BoneNodes) const;
    void ImportJoints(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const;
    
    // Helper methods for morph import
    void ImportVertexMorphs(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer, const FString& MeshUid) const;
    void ImportBoneMorphs(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const;
    void ImportMaterialMorphs(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const;
    void ImportUVMorphs(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const;
    void ImportGroupMorphs(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const;
    
    // Coordinate transformation methods
    FVector3f ConvertVectorPmxToUE(const FVector3f& PmxVector) const;
    FVector3f ConvertRotationPmxToUE(const FVector3f& PmxRotation) const;
    FQuat4f ConvertQuaternionPmxToUE(const FQuat4f& PmxQuat) const;
    
    // Utility methods
    FString GetMorphCategoryName(uint8 ControlPanel) const;
    FString SafeObjectName(const FString& Name, int32 MaxLength = 59) const;
    void FixRepeatedMorphNames(FPmxModel& PmxModel) const;
    
    // Logging and timing
    mutable double ImportStartTime = 0.0;
    void LogImportStart(const FString& SectionName) const;
    void LogImportComplete(const FString& SectionName) const;
    void LogImportSummary(const FPmxModel& PmxModel) const;
};