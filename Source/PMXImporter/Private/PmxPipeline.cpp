// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved.

#include "PmxPipeline.h"
#include "LogPMXImporter.h"
#include "PmxPhysicsBuilder.h"
#include "PmxStructs.h"

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
	const FString PhysicsType2Mode = TEXT("PMX:PhysicsType2Mode");
	const FString PhysicsMassScale = TEXT("PMX:PhysicsMassScale");
	const FString PhysicsDampingScale = TEXT("PMX:PhysicsDampingScale");
	const FString MarkSharpEdges = TEXT("PMX:MarkSharpEdges");
	const FString SharpEdgeAngle = TEXT("PMX:SharpEdgeAngle");
	const FString ImportAddUV2AsVertexColors = TEXT("PMX:ImportAddUV2AsVertexColors");
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

	// Create texture and material factory nodes
	CreateTextureFactoryNodes(BaseNodeContainer);
	CreateMaterialFactoryNodes(BaseNodeContainer);

	// Configure factory nodes with import options
	ConfigureFactoryNodes(BaseNodeContainer);

	UE_LOG(LogPMXImporter, Display, TEXT("UPmxPipeline::ExecutePipeline completed"));
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

	// Material options
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::UseMipmap, bUseMipmap);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::SphBlendFactor, SphBlendFactor);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::SpaBlendFactor, SpaBlendFactor);

	// Advanced options
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::CleanModel, bCleanModel);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::RemoveDoubles, bRemoveDoubles);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::MarkSharpEdges, bMarkSharpEdges);
	SourceNode->AddFloatAttribute(PmxPipelineAttributeKeys::SharpEdgeAngle, SharpEdgeAngle);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::ImportAddUV2AsVertexColors, bImportAddUV2AsVertexColors);
	SourceNode->AddBooleanAttribute(PmxPipelineAttributeKeys::ImportDisplay, bImportDisplay);

	UE_LOG(LogPMXImporter, Verbose, TEXT("UPmxPipeline: Options stored to SourceNode (Scale=%.2f, ImportMorphs=%d, ImportPhysics=%d)"),
		Scale, bImportMorphs, bImportPhysics);
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

				UE_LOG(LogPMXImporter, Verbose, TEXT("UPmxPipeline: Configured SkeletalMeshFactoryNode '%s' (Morphs=%d, Physics=%d)"),
					*NodeUid, bImportMorphs, bImportPhysics);
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
    // 컨테이너 변경을 동반하는 작업이므로 먼저 스냅샷(포인터)을 수집한다.
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

        // 팩토리 노드 UID 생성
        const FString FactoryUid = UInterchangeTextureFactoryNode::GetTextureFactoryNodeUidFromTextureNodeUid(NodeUid);

        // 이미 존재하면 스킵
        if (BaseNodeContainer->IsNodeUidValid(FactoryUid))
        {
            continue;
        }

        // 팩토리 노드 생성 및 초기화
        UInterchangeTexture2DFactoryNode* FactoryNode = NewObject<UInterchangeTexture2DFactoryNode>(BaseNodeContainer);
        FactoryNode->InitializeTextureNode(FactoryUid, TextureNode->GetDisplayLabel(), TextureNode->GetDisplayLabel(), BaseNodeContainer);

        // 번역 노드 UID 설정 (페이로드 검색에 필요)
        FactoryNode->SetCustomTranslatedTextureNodeUid(NodeUid);

        // 양방향 연결
        FactoryNode->AddTargetNodeUid(NodeUid);
        TextureNode->AddTargetNodeUid(FactoryUid);

        // 속성 복사
        bool bSRGB = false;
        if (TextureNode->GetCustomSRGB(bSRGB))
        {
            FactoryNode->SetCustomSRGB(bSRGB, false);
        }

        // 밉맵 설정 (파이프라인 옵션)
        // toon 텍스처 또는 작은 텍스처 감지 (mipmap 생성 실패 방지)
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
    // 컨테이너 변경을 동반하는 작업이므로 먼저 스냅샷(포인터)을 수집한다.
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

        // 팩토리 노드 UID 생성
        const FString FactoryUid = UInterchangeBaseMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(NodeUid);

        // 이미 존재하면 스킵
        if (BaseNodeContainer->IsNodeUidValid(FactoryUid))
        {
            continue;
        }

        // 팩토리 노드 생성
        UInterchangeMaterialInstanceFactoryNode* FactoryNode = NewObject<UInterchangeMaterialInstanceFactoryNode>(BaseNodeContainer);

        // 노드 설정
        BaseNodeContainer->SetNodeParentUid(FactoryUid, TEXT(""));
        FactoryNode->InitializeNode(FactoryUid, MaterialNode->GetDisplayLabel(), EInterchangeNodeContainerType::FactoryData);
        BaseNodeContainer->AddNode(FactoryNode);

        // 양방향 연결
        FactoryNode->AddTargetNodeUid(NodeUid);
        MaterialNode->AddTargetNodeUid(FactoryUid);

        // 부모 머티리얼 경로 복사
        FString ParentPath;
        if (MaterialNode->GetCustomParent(ParentPath))
        {
            FactoryNode->SetCustomParent(ParentPath);
        }

        // 인스턴스 클래스 설정 (에디터에서 MaterialInstanceConstant 사용)
        FactoryNode->SetCustomInstanceClassName(UMaterialInstanceConstant::StaticClass()->GetPathName());

        // 텍스처 의존성 추가
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
							// Skip if texture path is invalid or empty
							if (!TexPath.IsValid() || TexPath.ToString().IsEmpty())
							{
								UE_LOG(LogPMXImporter, Verbose, TEXT("UPmxPipeline: Skipping invalid/empty texture path for MI '%s'"),
									*MI->GetName());
								continue;
							}

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
			// Build physics asset from PMX data
			BuildPmxPhysicsAsset(PhysicsAsset, SkeletalMesh, **FoundCache);

			// Link physics asset to skeletal mesh
			SkeletalMesh->SetPhysicsAsset(PhysicsAsset);

			// Remove from cache after use
			UPmxTranslator::PhysicsPayloadCache.Remove(MeshName);

			UE_LOG(LogPMXImporter, Display, TEXT("UPmxPipeline: Built PhysicsAsset for '%s'"), *MeshName);
		}
		else
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("UPmxPipeline: No PMX physics cache found for '%s'"), *MeshName);
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

	// Hide morph option if not importing mesh
	if (!bImportMesh)
	{
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, bImportMorphs));
	}

	// Hide sharp edge angle if not marking sharp edges
	if (!bMarkSharpEdges)
	{
		HideProperty(this, this, GET_MEMBER_NAME_CHECKED(UPmxPipeline, SharpEdgeAngle));
	}
}
#endif
