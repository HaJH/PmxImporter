// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved.

#include "PmxPipeline.h"
#include "LogPMXImporter.h"
#include "PmxPhysicsBuilder.h"
#include "PmxStructs.h"
#include "PmxIKMetadata.h"

#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Nodes/InterchangeSourceNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangePhysicsAssetFactoryNode.h"
#include "InterchangeTexture2DFactoryNode.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeMaterialInstanceNode.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "HAL/PlatformProcess.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PmxPipeline)

// PMX option attribute keys
namespace PmxPipelineAttributeKeys
{
	const FString Scale = TEXT("PMX:Scale");
	const FString ImportMesh = TEXT("PMX:ImportMesh");
	const FString ImportMorphs = TEXT("PMX:ImportMorphs");
	const FString ImportArmature = TEXT("PMX:ImportArmature");
	const FString ImportPhysics = TEXT("PMX:ImportPhysics");
	const FString ImportDisplay = TEXT("PMX:ImportDisplay");
	const FString CleanModel = TEXT("PMX:CleanModel");
	const FString RemoveDoubles = TEXT("PMX:RemoveDoubles");
	const FString RenameLRBones = TEXT("PMX:RenameLRBones");
	const FString FixIKLinks = TEXT("PMX:FixIKLinks");
	const FString ApplyBoneFixedAxis = TEXT("PMX:ApplyBoneFixedAxis");
	const FString UseUnderscore = TEXT("PMX:UseUnderscore");
	const FString UseMipmap = TEXT("PMX:UseMipmap");
	const FString SphBlendFactor = TEXT("PMX:SphBlendFactor");
	const FString SpaBlendFactor = TEXT("PMX:SpaBlendFactor");
	const FString ParentMaterial = TEXT("PMX:ParentMaterial");
	const FString PhysicsType2Mode = TEXT("PMX:PhysicsType2Mode");
	const FString PhysicsMassScale = TEXT("PMX:PhysicsMassScale");
	const FString PhysicsDampingScale = TEXT("PMX:PhysicsDampingScale");
	const FString PhysicsShapeScale = TEXT("PMX:PhysicsShapeScale");
	const FString PhysicsSphereScale = TEXT("PMX:PhysicsSphereScale");
	const FString PhysicsBoxScale = TEXT("PMX:PhysicsBoxScale");
	const FString PhysicsCapsuleScale = TEXT("PMX:PhysicsCapsuleScale");
	const FString ForceStandardBonesKinematic = TEXT("PMX:ForceStandardBonesKinematic");
	const FString ForceNonStandardBonesSimulated = TEXT("PMX:ForceNonStandardBonesSimulated");
	const FString DisableConstraintBodyCollision = TEXT("PMX:DisableConstraintBodyCollision");
	const FString UsePmxCollisionGroups = TEXT("PMX:UsePmxCollisionGroups");
	const FString ConstraintStiffnessScale = TEXT("PMX:ConstraintStiffnessScale");
	const FString ConstraintDampingScale = TEXT("PMX:ConstraintDampingScale");
	const FString MaxAngularLimit = TEXT("PMX:MaxAngularLimit");
	const FString MarkSharpEdges = TEXT("PMX:MarkSharpEdges");
	const FString SharpEdgeAngle = TEXT("PMX:SharpEdgeAngle");
	const FString ImportAddUV2AsVertexColors = TEXT("PMX:ImportAddUV2AsVertexColors");
	// Mesh Build options
	const FString RecomputeNormals = TEXT("PMX:RecomputeNormals");
	const FString RecomputeTangents = TEXT("PMX:RecomputeTangents");
	const FString UseMikkTSpace = TEXT("PMX:UseMikkTSpace");
}

UPmxPipeline::UPmxPipeline()
{
	// Default values are set in header
}

bool UPmxPipeline::CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask)
{
	// PostImport needs to run on game thread for physics asset creation
	if (PipelineTask == EInterchangePipelineTask::PostImport)
	{
		return false;
	}
	return true;
}

void UPmxPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
	if (!BaseNodeContainer)
	{
		UE_LOG(LogPMXImporter, Error, TEXT("UPmxPipeline::ExecutePipeline - BaseNodeContainer is null"));
		return;
	}

	// Cache for post-import use
	CachedBaseNodeContainer = BaseNodeContainer;

	// Store options to SourceNode for Translator to read
	StoreOptionsToSourceNode(BaseNodeContainer);

	// CRITICAL: Update physics cache with current pipeline options
	// The Translator may have already created the cache with default values,
	// so we need to update it here with the user's settings from the import dialog
	UpdatePhysicsCacheOptions();

	// Create texture and material factory nodes
	CreateTextureFactoryNodes(BaseNodeContainer);
	CreateMaterialFactoryNodes(BaseNodeContainer);

	// Configure factory nodes with import options
	ConfigureFactoryNodes(BaseNodeContainer);

	UE_LOG(LogPMXImporter, Display, TEXT("UPmxPipeline::ExecutePipeline completed (Scale=%.2f, ShapeScale=%.2f, BoxScale=%.2f)"),
		Scale, PhysicsShapeScale, PhysicsBoxScale);
}

void UPmxPipeline::StoreOptionsToSourceNode(UInterchangeBaseNodeContainer* BaseNodeContainer) const
{
	UInterchangeSourceNode* SourceNode = UInterchangeSourceNode::FindOrCreateUniqueInstance(BaseNodeContainer);
	if (!SourceNode)
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("UPmxPipeline::StoreOptionsToSourceNode - Failed to get SourceNode"));
		return;
	}

	// Store all PMX import options as custom attributes
	// These will be read by UPmxTranslator::Translate()

	// Common options
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::Scale, Scale);

	// Mesh options
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::ImportMesh, bImportMesh);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::ImportMorphs, bImportMorphs);

	// Skeleton options
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::ImportArmature, bImportArmature);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::RenameLRBones, bRenameLRBones);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::FixIKLinks, bFixIKLinks);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::ApplyBoneFixedAxis, bApplyBoneFixedAxis);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::UseUnderscore, bUseUnderscore);

	// Physics options
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::ImportPhysics, bImportPhysics);
	SourceNode->AddStringAttribute(PmxPipelineAttributeKeys::PhysicsType2Mode, FString::FromInt(static_cast<int32>(PhysicsType2Mode)));
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::PhysicsMassScale, PhysicsMassScale);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::PhysicsDampingScale, PhysicsDampingScale);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::PhysicsShapeScale, PhysicsShapeScale);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::PhysicsSphereScale, PhysicsSphereScale);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::PhysicsBoxScale, PhysicsBoxScale);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::PhysicsCapsuleScale, PhysicsCapsuleScale);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::ForceStandardBonesKinematic, bForceStandardBonesKinematic);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::ForceNonStandardBonesSimulated, bForceNonStandardBonesSimulated);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::DisableConstraintBodyCollision, bDisableConstraintBodyCollision);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::UsePmxCollisionGroups, bUsePmxCollisionGroups);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::ConstraintStiffnessScale, ConstraintStiffnessScale);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::ConstraintDampingScale, ConstraintDampingScale);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::MaxAngularLimit, MaxAngularLimit);

	// Material options
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::UseMipmap, bUseMipmap);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::SphBlendFactor, SphBlendFactor);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::SpaBlendFactor, SpaBlendFactor);
	// Always store ParentMaterial path using GetAssetPathString() for proper format
	SourceNode->AddStringAttribute(PmxPipelineAttributeKeys::ParentMaterial, ParentMaterial.GetAssetPathString());
	UE_LOG(LogPMXImporter, Display, TEXT("PmxPipeline: Storing ParentMaterial='%s'"), *ParentMaterial.GetAssetPathString());

	// Mesh Build options
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::RecomputeNormals, bRecomputeNormals);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::RecomputeTangents, bRecomputeTangents);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::UseMikkTSpace, bUseMikkTSpace);

	// Advanced options
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::CleanModel, bCleanModel);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::RemoveDoubles, bRemoveDoubles);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::MarkSharpEdges, bMarkSharpEdges);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::SharpEdgeAngle, SharpEdgeAngle);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::ImportAddUV2AsVertexColors, bImportAddUV2AsVertexColors);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::ImportDisplay, bImportDisplay);

	UE_LOG(LogPMXImporter, Display, TEXT("UPmxPipeline::StoreOptionsToSourceNode - Scale=%.2f, ShapeScale=%.2f, SphereScale=%.2f, BoxScale=%.2f, CapsuleScale=%.2f"),
		Scale, PhysicsShapeScale, PhysicsSphereScale, PhysicsBoxScale, PhysicsCapsuleScale);
}

void UPmxPipeline::UpdatePhysicsCacheOptions() const
{
	// Update all physics caches with current pipeline options
	// This is necessary because Translate() may have already created caches with default values
	// before ExecutePipeline() is called with user-modified options

	int32 UpdatedCount = 0;
	for (auto& CachePair : UPmxTranslator::PhysicsPayloadCache)
	{
		if (CachePair.Value.IsValid())
		{
			FPmxPhysicsCache& Cache = *CachePair.Value;

			// Update scale options
			Cache.Scale = Scale;
			Cache.ShapeScale = PhysicsShapeScale;
			Cache.SphereScale = PhysicsSphereScale;
			Cache.BoxScale = PhysicsBoxScale;
			Cache.CapsuleScale = PhysicsCapsuleScale;

			// Update other physics options
			Cache.Type2Mode = PhysicsType2Mode;
			Cache.MassScale = PhysicsMassScale;
			Cache.DampingScale = PhysicsDampingScale;
			Cache.bForceStandardBonesKinematic = bForceStandardBonesKinematic;
			Cache.bForceNonStandardBonesSimulated = bForceNonStandardBonesSimulated;

			// Update collision filtering options
			Cache.bDisableConstraintBodyCollision = bDisableConstraintBodyCollision;
			Cache.bUsePmxCollisionGroups = bUsePmxCollisionGroups;
			Cache.bEnableStandardNonStandardCollision = bEnableStandardNonStandardCollision;

			// Update constraint scale options
			Cache.ConstraintStiffnessScale = ConstraintStiffnessScale;
			Cache.ConstraintDampingScale = ConstraintDampingScale;
			Cache.MaxAngularLimit = MaxAngularLimit;
			Cache.bForceAllLinearMotionLocked = bForceAllLinearMotionLocked;
			Cache.bDisableLinearSpringDrive = bDisableLinearSpringDrive;
			Cache.LinearMotionTolerance = LinearMotionTolerance;

			// Constraint mode options (Phase 2)
			Cache.ConstraintMode = ConstraintMode;
			Cache.bLockAllLinearMotion = bLockAllLinearMotion;
			Cache.OverrideAngularMotion = OverrideAngularMotion;
			Cache.OverrideSwing1Limit = OverrideSwing1Limit;
			Cache.OverrideSwing2Limit = OverrideSwing2Limit;
			Cache.OverrideTwistLimit = OverrideTwistLimit;

			// Soft Constraint options
			Cache.bUseSoftConstraint = bUseSoftConstraint;
			Cache.SoftConstraintStiffness = SoftConstraintStiffness;
			Cache.SoftConstraintDamping = SoftConstraintDamping;

			// Long chain optimization - hardcoded defaults (not exposed in UI)
			// These options have minimal impact and add unnecessary complexity
			Cache.bOptimizeForLongChains = true;
			Cache.bEnableProjection = true;
			Cache.ProjectionLinearTolerance = 1.0f;
			Cache.ProjectionAngularTolerance = 10.0f;
			Cache.bAutoParentDominates = false;
			Cache.bEnableMassConditioning = false;
			Cache.ContactTransferScale = 0.3f;

			UpdatedCount++;
		}
	}

	if (UpdatedCount > 0)
	{
		UE_LOG(LogPMXImporter, Display, TEXT("UPmxPipeline: Updated %d physics cache(s) with pipeline options (ShapeScale=%.2f, BoxScale=%.2f)"),
			UpdatedCount, PhysicsShapeScale, PhysicsBoxScale);
	}
}

void UPmxPipeline::ConfigureFactoryNodes(UInterchangeBaseNodeContainer* BaseNodeContainer) const
{
	// Configure SkeletalMesh factory nodes
	BaseNodeContainer->IterateNodesOfType<UInterchangeSkeletalMeshFactoryNode>(
		[this](const FString& NodeUid, UInterchangeSkeletalMeshFactoryNode* SkeletalMeshNode)
		{
			if (SkeletalMeshNode)
			{
				// Enable/disable morph target import
				SkeletalMeshNode->SetCustomImportMorphTarget(bImportMorphs);

				// Enable physics asset creation if physics import is enabled
				SkeletalMeshNode->SetCustomCreatePhysicsAsset(bImportPhysics);

				// Apply Mesh Build options
				SkeletalMeshNode->SetCustomRecomputeNormals(bRecomputeNormals);
				SkeletalMeshNode->SetCustomRecomputeTangents(bRecomputeTangents);
				SkeletalMeshNode->SetCustomUseMikkTSpace(bUseMikkTSpace);

				UE_LOG(LogPMXImporter, Verbose, TEXT("UPmxPipeline: Configured SkeletalMeshFactoryNode '%s' (Morphs=%d, Physics=%d, RecomputeNormals=%d, RecomputeTangents=%d, MikkTSpace=%d)"),
					*NodeUid, bImportMorphs, bImportPhysics, bRecomputeNormals, bRecomputeTangents, bUseMikkTSpace);
			}
		});

	// Configure PhysicsAsset factory nodes if needed
	if (bImportPhysics)
	{
		BaseNodeContainer->IterateNodesOfType<UInterchangePhysicsAssetFactoryNode>(
			[this](const FString& NodeUid, UInterchangePhysicsAssetFactoryNode* PhysicsNode)
			{
				if (PhysicsNode)
				{
					UE_LOG(LogPMXImporter, Verbose, TEXT("UPmxPipeline: Found PhysicsAssetFactoryNode '%s'"), *NodeUid);
				}
			});
	}
}

void UPmxPipeline::CreateTextureFactoryNodes(UInterchangeBaseNodeContainer* BaseNodeContainer) const
{
    // Collect node snapshots first since we'll be modifying the container
    TArray<UInterchangeTexture2DNode*> TextureNodes;
    TextureNodes.Reserve(64);
    BaseNodeContainer->IterateNodesOfType<UInterchangeTexture2DNode>(
        [&TextureNodes](const FString& /*NodeUid*/, UInterchangeTexture2DNode* TextureNode)
        {
            if (TextureNode)
            {
                TextureNodes.Add(TextureNode);
            }
        });

    for (UInterchangeTexture2DNode* TextureNode : TextureNodes)
    {
        const FString NodeUid = TextureNode->GetUniqueID();

        // Generate factory node UID
        const FString FactoryUid = UInterchangeTextureFactoryNode::GetTextureFactoryNodeUidFromTextureNodeUid(NodeUid);

        // Skip if already exists
        if (BaseNodeContainer->IsNodeUidValid(FactoryUid))
        {
            continue;
        }

        // Create and initialize factory node
        UInterchangeTexture2DFactoryNode* FactoryNode = NewObject<UInterchangeTexture2DFactoryNode>(BaseNodeContainer);
        FactoryNode->InitializeTextureNode(FactoryUid, TextureNode->GetDisplayLabel(), TextureNode->GetDisplayLabel(), BaseNodeContainer);

        // Set translated node UID (required for payload retrieval)
        FactoryNode->SetCustomTranslatedTextureNodeUid(NodeUid);

        // Bidirectional connection
        FactoryNode->AddTargetNodeUid(NodeUid);
        TextureNode->AddTargetNodeUid(FactoryUid);

        // Copy attributes
        bool bSRGB = false;
        if (TextureNode->GetCustomSRGB(bSRGB))
        {
            FactoryNode->SetCustomSRGB(bSRGB, false);
        }

        // Mipmap settings (pipeline option)
        // Detect toon/small textures to prevent mipmap generation failure
        FString TextureDisplayLabel = TextureNode->GetDisplayLabel();
        bool bIsToonOrSmallTexture = TextureDisplayLabel.Contains(TEXT("toon"), ESearchCase::IgnoreCase) ||
                                       TextureDisplayLabel.Contains(TEXT("表情")) ||
                                       TextureDisplayLabel.Contains(TEXT("expression"), ESearchCase::IgnoreCase);

        uint8 MipGenSetting = static_cast<uint8>(TextureMipGenSettings::TMGS_NoMipmaps);
        if (bUseMipmap)
        {
            if (bIsToonOrSmallTexture)
            {
                // Toon/small textures: disable mipmaps to prevent generation failure
                MipGenSetting = static_cast<uint8>(TextureMipGenSettings::TMGS_NoMipmaps);
                UE_LOG(LogPMXImporter, Display,
                    TEXT("UPmxPipeline: Texture '%s' detected as toon/small texture, disabling mipmaps"),
                    *TextureDisplayLabel);
            }
            else
            {
                // Regular textures: generate mipmaps
                MipGenSetting = static_cast<uint8>(TextureMipGenSettings::TMGS_FromTextureGroup);
            }
        }

        FactoryNode->SetCustomMipGenSettings(MipGenSetting, false);

        UE_LOG(LogPMXImporter, Verbose, TEXT("UPmxPipeline: Created Texture2DFactoryNode '%s' for '%s'"),
            *FactoryUid, *TextureNode->GetDisplayLabel());
    }
}

void UPmxPipeline::CreateMaterialFactoryNodes(UInterchangeBaseNodeContainer* BaseNodeContainer) const
{
    // Collect node snapshots first since we'll be modifying the container
    TArray<UInterchangeMaterialInstanceNode*> MaterialNodes;
    MaterialNodes.Reserve(64);
    BaseNodeContainer->IterateNodesOfType<UInterchangeMaterialInstanceNode>(
        [&MaterialNodes](const FString& /*NodeUid*/, UInterchangeMaterialInstanceNode* MaterialNode)
        {
            if (MaterialNode)
            {
                MaterialNodes.Add(MaterialNode);
            }
        });

    for (UInterchangeMaterialInstanceNode* MaterialNode : MaterialNodes)
    {
        const FString NodeUid = MaterialNode->GetUniqueID();

        // Generate factory node UID
        const FString FactoryUid = UInterchangeBaseMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(NodeUid);

        // Skip if already exists
        if (BaseNodeContainer->IsNodeUidValid(FactoryUid))
        {
            continue;
        }

        // Create factory node
        UInterchangeMaterialInstanceFactoryNode* FactoryNode = NewObject<UInterchangeMaterialInstanceFactoryNode>(BaseNodeContainer);

        // Configure node
        BaseNodeContainer->SetNodeParentUid(FactoryUid, TEXT(""));
        FactoryNode->InitializeNode(FactoryUid, MaterialNode->GetDisplayLabel(), EInterchangeNodeContainerType::FactoryData);
        BaseNodeContainer->AddNode(FactoryNode);

        // Bidirectional connection
        FactoryNode->AddTargetNodeUid(NodeUid);
        MaterialNode->AddTargetNodeUid(FactoryUid);

        // Copy parent material path
        FString ParentPath;
        if (MaterialNode->GetCustomParent(ParentPath))
        {
            FactoryNode->SetCustomParent(ParentPath);
        }

        // Set instance class (use MaterialInstanceConstant in editor)
        FactoryNode->SetCustomInstanceClassName(UMaterialInstanceConstant::StaticClass()->GetPathName());

        // Add texture dependencies
        FString TextureUid;
        if (MaterialNode->GetTextureParameterValue(TEXT("BaseColorTexture"), TextureUid))
        {
            const FString TexFactoryUid = UInterchangeTextureFactoryNode::GetTextureFactoryNodeUidFromTextureNodeUid(TextureUid);
            FactoryNode->AddFactoryDependencyUid(TexFactoryUid);
        }

        UE_LOG(LogPMXImporter, Verbose, TEXT("UPmxPipeline: Created MaterialInstanceFactoryNode '%s' for '%s'"),
            *FactoryUid, *MaterialNode->GetDisplayLabel());
    }

    // Now connect material factory nodes to SkeletalMesh factory nodes
    BaseNodeContainer->IterateNodesOfType<UInterchangeSkeletalMeshFactoryNode>(
        [BaseNodeContainer](const FString& SkeletalMeshUid, UInterchangeSkeletalMeshFactoryNode* SkeletalMeshNode)
        {
            if (!SkeletalMeshNode)
            {
                return;
            }

            // Get LOD data nodes
            TArray<FString> LodDataUids;
            SkeletalMeshNode->GetLodDataUniqueIds(LodDataUids);

            for (const FString& LodDataUid : LodDataUids)
            {
                const UInterchangeSkeletalMeshLodDataNode* LodDataNode =
                    Cast<UInterchangeSkeletalMeshLodDataNode>(BaseNodeContainer->GetNode(LodDataUid));

                if (!LodDataNode)
                {
                    continue;
                }

                // Get mesh nodes from LOD
                TArray<FString> MeshUids;
                LodDataNode->GetMeshUids(MeshUids);

                for (const FString& MeshUid : MeshUids)
                {
                    const UInterchangeMeshNode* MeshNode =
                        Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(MeshUid));

                    if (!MeshNode)
                    {
                        continue;
                    }

                    // Get material slot names and dependencies from mesh node
                    TMap<FString, FString> SlotMaterialDependencies;
                    MeshNode->GetSlotMaterialDependencies(SlotMaterialDependencies);

                    // For each material slot, set the factory UID on SkeletalMeshFactoryNode
                    for (const TPair<FString, FString>& Pair : SlotMaterialDependencies)
                    {
                        const FString& SlotName = Pair.Key;
                        const FString& MaterialNodeUid = Pair.Value;

                        // Convert Material Node UID to Factory UID
                        const FString MaterialFactoryUid =
                            UInterchangeBaseMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(MaterialNodeUid);

                        // Set material dependency on SkeletalMesh factory node
                        SkeletalMeshNode->SetSlotMaterialDependencyUid(SlotName, MaterialFactoryUid);

                        // Add factory dependency to ensure materials are created before the mesh
                        TArray<FString> FactoryDependencies;
                        SkeletalMeshNode->GetFactoryDependencies(FactoryDependencies);
                        if (!FactoryDependencies.Contains(MaterialFactoryUid))
                        {
                            SkeletalMeshNode->AddFactoryDependencyUid(MaterialFactoryUid);
                        }

                        UE_LOG(LogPMXImporter, Display, TEXT("UPmxPipeline: Connected material slot '%s' to factory '%s' for SkeletalMesh '%s'"),
                            *SlotName, *MaterialFactoryUid, *SkeletalMeshUid);
                    }
                }
            }
        });
}

void UPmxPipeline::ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport)
{
	if (!CreatedAsset || !BaseNodeContainer)
	{
		return;
	}

	// Handle Material Instance parameter setting (workaround for missing Factory API)
	if (UMaterialInstanceConstant* MI = Cast<UMaterialInstanceConstant>(CreatedAsset))
	{
		const UInterchangeFactoryBaseNode* FactoryNode = BaseNodeContainer->GetFactoryNode(NodeKey);
		if (FactoryNode)
		{
			TArray<FString> TargetUids;
			FactoryNode->GetTargetNodeUids(TargetUids);
			const UInterchangeMaterialInstanceNode* MaterialNode = nullptr;
			for (const FString& Uid : TargetUids)
			{
				if (const UInterchangeMaterialInstanceNode* Node = Cast<UInterchangeMaterialInstanceNode>(BaseNodeContainer->GetNode(Uid)))
				{
					MaterialNode = Node;
					break;
				}
			}

			if (MaterialNode)
			{
				// Set Textures
				FString TextureUid;
				if (MaterialNode->GetTextureParameterValue(TEXT("BaseColorTexture"), TextureUid))
				{
					FString TexFactoryUid = UInterchangeTextureFactoryNode::GetTextureFactoryNodeUidFromTextureNodeUid(TextureUid);
					const UInterchangeTextureFactoryNode* TexFactory = Cast<UInterchangeTextureFactoryNode>(BaseNodeContainer->GetFactoryNode(TexFactoryUid));
					if (TexFactory)
					{
						FSoftObjectPath TexPath;
						if (TexFactory->GetCustomReferenceObject(TexPath))
						{
							// Only proceed if texture path is valid and not empty
							if (TexPath.IsValid() && !TexPath.ToString().IsEmpty())
							{
								// Retry logic for texture loading
								UTexture* Texture = nullptr;
								int32 MaxRetries = 5;
								for (int32 Retry = 0; Retry < MaxRetries && !Texture; ++Retry)
								{
									if (Retry > 0)
									{
										// Wait a bit before retrying (not on first attempt)
										FPlatformProcess::Sleep(0.1f); // 100ms
									}

									Texture = Cast<UTexture>(TexPath.TryLoad());

									if (!Texture)
									{
										UE_LOG(LogPMXImporter, Verbose, TEXT("UPmxPipeline: Texture load retry %d/%d for '%s'"),
											Retry + 1, MaxRetries, *TexPath.ToString());
									}
								}

								if (Texture)
								{
									MI->SetTextureParameterValueEditorOnly(FName("BaseColorTexture"), Texture);
									UE_LOG(LogPMXImporter, Display, TEXT("UPmxPipeline: Successfully set BaseColorTexture for MI '%s'"),
										*MI->GetName());
								}
								else
								{
									UE_LOG(LogPMXImporter, Warning, TEXT("UPmxPipeline: Failed to load texture '%s' after %d retries for MI '%s'"),
										*TexPath.ToString(), MaxRetries, *MI->GetName());
								}
							}
							else
							{
								UE_LOG(LogPMXImporter, Verbose, TEXT("UPmxPipeline: Skipping invalid/empty texture path for MI '%s'"),
									*MI->GetName());
							}
						}
					}
				}

				// Set Scalars
				auto ApplyScalar = [&](const FString& ParamName)
				{
					float Val;
					if (MaterialNode->GetScalarParameterValue(ParamName, Val))
					{
						MI->SetScalarParameterValueEditorOnly(FName(*ParamName), Val);
					}
				};
				ApplyScalar(TEXT("Opacity"));
				ApplyScalar(TEXT("pmx.mat.index"));
				ApplyScalar(TEXT("pmx.sphere.mode"));
				ApplyScalar(TEXT("pmx.edge.size"));
				ApplyScalar(TEXT("pmx.specular.power"));

				// Set Vectors
				auto ApplyVector = [&](const FString& ParamName)
				{
					FLinearColor Val;
					if (MaterialNode->GetVectorParameterValue(ParamName, Val))
					{
						MI->SetVectorParameterValueEditorOnly(FName(*ParamName), Val);
					}
				};
				ApplyVector(TEXT("BaseColorTint"));
				ApplyVector(TEXT("pmx.edge.color"));
				ApplyVector(TEXT("pmx.specular.rgb"));
				ApplyVector(TEXT("pmx.ambient.rgb"));

				// Set Static Switches
				auto ApplySwitch = [&](const FString& ParamName)
				{
					bool Val;
					if (MaterialNode->GetStaticSwitchParameterValue(ParamName, Val))
					{
						MI->SetStaticSwitchParameterValueEditorOnly(FName(*ParamName), Val);
					}
				};
				ApplySwitch(TEXT("bTwoSided"));
				ApplySwitch(TEXT("bTranslucentHint"));
				ApplySwitch(TEXT("pmx.toon.mode"));
				ApplySwitch(TEXT("pmx.edge.draw"));

				UE_LOG(LogPMXImporter, Verbose, TEXT("UPmxPipeline: Configured MaterialInstance '%s' from Node '%s'"), *MI->GetName(), *MaterialNode->GetDisplayLabel());
			}
		}
	}

	// Handle SkeletalMesh creation - Store IK metadata for VMD import
	if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(CreatedAsset))
	{
		// Get cached PMX model data
		const TSharedPtr<FPmxModel>* FoundModel = UPmxTranslator::MeshPayloadCache.Find(TEXT("PMX_GEOMETRY"));
		if (FoundModel && FoundModel->IsValid())
		{
			const FPmxModel& PmxModel = **FoundModel;

			// Get skeleton first - we need to create IKMetadata as its subobject
			USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
			if (!Skeleton)
			{
				UE_LOG(LogPMXImporter, Warning, TEXT("UPmxPipeline: No skeleton found for SkeletalMesh '%s'"), *SkeletalMesh->GetName());
			}
			else
			{
				// Remove existing IK metadata if any (for reimport)
				if (UPmxIKMetadataUserData* ExistingMetadata = Skeleton->GetAssetUserData<UPmxIKMetadataUserData>())
				{
					Skeleton->RemoveUserDataOfClass(UPmxIKMetadataUserData::StaticClass());
				}

				// Create IK metadata as subobject of Skeleton (IMPORTANT: must be owned by Skeleton to avoid cross-package reference)
				UPmxIKMetadataUserData* IKMetadata = NewObject<UPmxIKMetadataUserData>(Skeleton);
				IKMetadata->ModelName = PmxModel.Header.ModelName;

				// Build bone name to UE skeleton index mapping
				// IMPORTANT: Use UE skeleton indices, not PMX bone indices!
				const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
				for (int32 BoneIdx = 0; BoneIdx < PmxModel.Bones.Num(); ++BoneIdx)
				{
					const FPmxBone& Bone = PmxModel.Bones[BoneIdx];
					FName BoneName(*Bone.Name);
					int32 UEBoneIndex = RefSkeleton.FindBoneIndex(BoneName);
					if (UEBoneIndex != INDEX_NONE)
					{
						IKMetadata->BoneNameToIndex.Add(BoneName, UEBoneIndex);
					}
				}

				// Extract IK chains
				for (int32 BoneIdx = 0; BoneIdx < PmxModel.Bones.Num(); ++BoneIdx)
				{
					const FPmxBone& Bone = PmxModel.Bones[BoneIdx];

					// Check if this bone is an IK bone (has IK links)
					if (Bone.IKLinks.Num() > 0 && Bone.IKTargetBoneIndex >= 0)
					{
						FPmxIKChainMetadata Chain;
						Chain.IKBoneName = FName(*Bone.Name);
						Chain.IKBoneIndex = BoneIdx;
						Chain.LoopCount = Bone.IKLoopCount;
						Chain.LimitAngle = Bone.IKLimitAngle;

						// Get target bone
						if (Bone.IKTargetBoneIndex >= 0 && Bone.IKTargetBoneIndex < PmxModel.Bones.Num())
						{
							Chain.TargetBoneName = FName(*PmxModel.Bones[Bone.IKTargetBoneIndex].Name);
							Chain.TargetBoneIndex = Bone.IKTargetBoneIndex;
						}

						// Extract IK links
						for (const FPmxBone::FPmxIKLink& Link : Bone.IKLinks)
						{
							FPmxIKLinkMetadata LinkMeta;
							if (Link.BoneIndex >= 0 && Link.BoneIndex < PmxModel.Bones.Num())
							{
								LinkMeta.BoneName = FName(*PmxModel.Bones[Link.BoneIndex].Name);
								LinkMeta.BoneIndex = Link.BoneIndex;
								LinkMeta.bHasAngleLimits = (Link.AngleLimitFlag != 0);
								LinkMeta.AngleLimitMin = FVector(Link.LimitMin);
								LinkMeta.AngleLimitMax = FVector(Link.LimitMax);
								Chain.Links.Add(LinkMeta);
							}
						}

						IKMetadata->IKChains.Add(Chain);
						UE_LOG(LogPMXImporter, Verbose, TEXT("UPmxPipeline: Found IK chain '%s' -> '%s' with %d links"),
							*Bone.Name, *Chain.TargetBoneName.ToString(), Chain.Links.Num());
					}
				}

				// Extract additional bone info (rotation/location inheritance)
				for (int32 BoneIdx = 0; BoneIdx < PmxModel.Bones.Num(); ++BoneIdx)
				{
					const FPmxBone& Bone = PmxModel.Bones[BoneIdx];

					// Check for additional rotation/location (flags: 0x0100 = additional rotation, 0x0200 = additional location)
					bool bHasAdditional = (Bone.BoneFlags & 0x0100) || (Bone.BoneFlags & 0x0200);
					if (bHasAdditional && Bone.AdditionalParentIndex >= 0)
					{
						FPmxAdditionalBoneMetadata AdditionalMeta;
						AdditionalMeta.BoneName = FName(*Bone.Name);
						AdditionalMeta.BoneIndex = BoneIdx;
						AdditionalMeta.bHasAdditionalRotation = (Bone.BoneFlags & 0x0100) != 0;
						AdditionalMeta.bHasAdditionalLocation = (Bone.BoneFlags & 0x0200) != 0;
						AdditionalMeta.Ratio = Bone.AdditionalRatio;

						if (Bone.AdditionalParentIndex >= 0 && Bone.AdditionalParentIndex < PmxModel.Bones.Num())
						{
							AdditionalMeta.ParentBoneName = FName(*PmxModel.Bones[Bone.AdditionalParentIndex].Name);
							AdditionalMeta.ParentBoneIndex = Bone.AdditionalParentIndex;
						}

						IKMetadata->AdditionalBones.Add(AdditionalMeta);
					}
				}

				// Add metadata to skeleton
				Skeleton->AddAssetUserData(IKMetadata);
				Skeleton->MarkPackageDirty();
				UE_LOG(LogPMXImporter, Display, TEXT("UPmxPipeline: Stored IK metadata (%d chains, %d additional bones) for skeleton '%s'"),
					IKMetadata->IKChains.Num(), IKMetadata->AdditionalBones.Num(), *Skeleton->GetName());
			}
		}
	}

	// Handle Physics Asset creation
	if (UPhysicsAsset* PhysicsAsset = Cast<UPhysicsAsset>(CreatedAsset))
	{
		if (!bImportPhysics)
		{
			return;
		}

		const UInterchangePhysicsAssetFactoryNode* PhysicsFactoryNode =
			Cast<UInterchangePhysicsAssetFactoryNode>(BaseNodeContainer->GetFactoryNode(NodeKey));

		if (!PhysicsFactoryNode)
		{
			return;
		}

		// Get the associated SkeletalMesh
		FString SkeletalMeshUid;
		if (!PhysicsFactoryNode->GetCustomSkeletalMeshUid(SkeletalMeshUid))
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("UPmxPipeline: PhysicsAsset '%s' has no associated SkeletalMesh UID"), *NodeKey);
			return;
		}

		// Find the SkeletalMesh factory node to get the created asset
		const UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode =
			Cast<UInterchangeSkeletalMeshFactoryNode>(BaseNodeContainer->GetFactoryNode(SkeletalMeshUid));

		if (!SkeletalMeshFactoryNode)
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("UPmxPipeline: Could not find SkeletalMeshFactoryNode '%s'"), *SkeletalMeshUid);
			return;
		}

		// Get the reference to the created SkeletalMesh
		FSoftObjectPath SkeletalMeshPath;
		if (!SkeletalMeshFactoryNode->GetCustomReferenceObject(SkeletalMeshPath))
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("UPmxPipeline: SkeletalMeshFactoryNode has no ReferenceObject"));
			return;
		}

		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(SkeletalMeshPath.TryLoad());
		if (!SkeletalMesh)
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("UPmxPipeline: Failed to load SkeletalMesh from '%s'"), *SkeletalMeshPath.ToString());
			return;
		}

		// Look for cached PMX physics data
		const FString MeshName = SkeletalMesh->GetName();
		TSharedPtr<FPmxPhysicsCache>* FoundCache = UPmxTranslator::PhysicsPayloadCache.Find(MeshName);

		if (FoundCache && FoundCache->IsValid())
		{
			// Set the preview skeletal mesh BEFORE building physics asset
			// This is critical for proper bone name resolution
			PhysicsAsset->SetPreviewMesh(SkeletalMesh, false);

			// Build physics asset from PMX data
			BuildPmxPhysicsAsset(PhysicsAsset, SkeletalMesh, **FoundCache);

			// Link physics asset to skeletal mesh
			SkeletalMesh->SetPhysicsAsset(PhysicsAsset);

			// Post-processing: Refresh physics asset to update all dependent components
			#if WITH_EDITOR
			PhysicsAsset->RefreshPhysicsAssetChange();
			#endif

			// Mark assets as dirty so they get saved
			PhysicsAsset->MarkPackageDirty();
			SkeletalMesh->MarkPackageDirty();

			// Remove from cache after use
			UPmxTranslator::PhysicsPayloadCache.Remove(MeshName);

			UE_LOG(LogPMXImporter, Display, TEXT("UPmxPipeline: Built PhysicsAsset for '%s'"), *MeshName);
		}
		else
		{
			UE_LOG(LogPMXImporter, Log, TEXT("UPmxPipeline: No PMX physics cache found for '%s'"), *MeshName);
		}
	}
}

void UPmxPipeline::BuildPmxPhysicsAsset(UPhysicsAsset* PhysicsAsset, USkeletalMesh* SkeletalMesh, const FPmxPhysicsCache& PhysicsData) const
{
	if (!PhysicsAsset || !SkeletalMesh)
	{
		return;
	}

	// Ensure SkeletalMesh is fully compiled before building physics
	if (SkeletalMesh->IsCompiling())
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("UPmxPipeline: SkeletalMesh '%s' is still compiling, physics build may be incomplete"),
			*SkeletalMesh->GetName());
	}

	// Delegate to the physics builder
	FPmxPhysicsBuilder::BuildPhysicsAsset(PhysicsAsset, SkeletalMesh, PhysicsData);
}

#if WITH_EDITOR
void UPmxPipeline::FilterPropertiesFromTranslatedData(UInterchangeBaseNodeContainer* InBaseNodeContainer)
{
	Super::FilterPropertiesFromTranslatedData(InBaseNodeContainer);

	// Hide physics options if no physics data is available
	// (This would require checking the translated data for rigid bodies)

	// Hide skeleton options if not importing armature
	if (!bImportArmature)
	{
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, bRenameLRBones));
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, bFixIKLinks));
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, bApplyBoneFixedAxis));
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, bUseUnderscore));
	}

	// Hide physics sub-options if not importing physics
	if (!bImportPhysics)
	{
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, PhysicsType2Mode));
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, PhysicsMassScale));
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, PhysicsDampingScale));
	}

	// Hide mesh build options and morph option if not importing mesh
	if (!bImportMesh)
	{
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, bImportMorphs));
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, bRecomputeNormals));
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, bRecomputeTangents));
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, bUseMikkTSpace));
	}

	// Hide MikkTSpace option if not recomputing tangents
	if (!bRecomputeTangents)
	{
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, bUseMikkTSpace));
	}

	// Hide sharp edge angle if not marking sharp edges
	if (!bMarkSharpEdges)
	{
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, SharpEdgeAngle));
	}
}
#endif
