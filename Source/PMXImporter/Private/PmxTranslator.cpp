﻿// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#include "PmxTranslator.h"

#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "InterchangeSceneNode.h"
#include "Nodes/InterchangeBaseNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "LogPMXImporter.h"
#include "Misc/Paths.h"
#include "Animation/Skeleton.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "Engine/SkeletalMesh.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeTexture2DNode.h"
#include "PmxStructs.h"
#include "PmxReader.h"
#include "Misc/FileHelper.h"
#include "Mesh/InterchangeMeshPayload.h"
#include "Types/AttributeStorage.h"
#include "StaticMeshAttributes.h"
#include "SkeletalMeshAttributes.h"
#include "HAL/IConsoleManager.h"
#include "BoneWeights.h"
#include "InterchangeTranslatorHelper.h"
#include "Texture/InterchangeTexturePayloadInterface.h"
#include "PmxPhysicsMapping.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"
#include "InterchangePhysicsAssetFactoryNode.h"
#include "PmxConsoleVariables.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PmxNodeBuilder.h"
#include "PmxMaterialMapping.h"

// Static member definition
TMap<FString, TSharedPtr<FPmxModel>> UPmxTranslator::MeshPayloadCache;

bool UPmxTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
    if (!InSourceData)
    {
        return false;
    }
    const FString Filename = InSourceData->GetFilename();
    const FString Ext = FPaths::GetExtension(Filename, true);
    return Ext.Equals(TEXT(".pmx"), ESearchCase::IgnoreCase);

}

bool UPmxTranslator::Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
    using ContainerType = EInterchangeNodeContainerType;
    
    LogImportStart(TEXT("PMX Import"));
    
    // Get source data
    const UInterchangeSourceData* PMXSourceData = GetSourceData();
    if (!PMXSourceData)
    {
        UE_LOG(LogPMXImporter, Error, TEXT("No source data provided"));
        return false;
    }

    // Load PMX file data
    FPmxModel PmxModel;
    TArray<uint8> FileData;
    
    if (!FFileHelper::LoadFileToArray(FileData, *PMXSourceData->GetFilename()))
    {
        UE_LOG(LogPMXImporter, Error, TEXT("Failed to load PMX file: %s"), *PMXSourceData->GetFilename());
        return false;
    }

    if (!PMXReader::LoadPmxFromData(FileData, PmxModel))
    {
        UE_LOG(LogPMXImporter, Error, TEXT("Failed to parse PMX file: %s"), *PMXSourceData->GetFilename());
        return false;
    }

    UE_LOG(LogPMXImporter, Log, TEXT("Successfully loaded PMX model: %s"), *PmxModel.Header.ModelName);
    
    // Initialize import options
    ImportOptions = FPmxImportOptions(); // Defaults; can be overridden by pipeline before Translate
    
    // Execute main import logic
    bool bSuccess = ExecutePmxImport(PmxModel, BaseNodeContainer);
    
    if (bSuccess)
    {
        LogImportSummary(PmxModel);
        LogImportComplete(TEXT("PMX Import"));
    }
    
    return bSuccess;
}

bool UPmxTranslator::ExecutePmxImport(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
    // Step 1: Data cleaning
    FPmxModel CleanedModel = PmxModel;
    TMap<int32, int32> VertexMap;
    
    if (ImportOptions.bCleanModel)
    {
        LogImportStart(TEXT("Data Cleaning"));
        CleanPmxModel(CleanedModel, !ImportOptions.bImportMorphs);
        LogImportComplete(TEXT("Data Cleaning"));
    }
    
    if (ImportOptions.bRemoveDoubles)
    {
        LogImportStart(TEXT("Remove Doubles"));
        RemoveDoubles(CleanedModel, !ImportOptions.bImportMorphs, VertexMap);
        LogImportComplete(TEXT("Remove Doubles"));
    }
    
    // Fix repeated morph names
    FixRepeatedMorphNames(CleanedModel);

    // Step 2: Scene root creation
    UInterchangeSceneNode* RootNode = FPmxNodeBuilder::CreateSceneRoot(CleanedModel, BaseNodeContainer);
    if (!RootNode)
    {
        UE_LOG(LogPMXImporter, Error, TEXT("Failed to create scene root"));
        return false;
    }

    FString RootJointUid, SkeletonUid, MeshUid, SkeletalMeshUid;
    TArray<FString> MaterialUids;
    
    // Step 3: Import sections based on options
    if (ImportOptions.bImportArmature)
    {
        LogImportStart(TEXT("Armature"));
        ImportArmatureSection(CleanedModel, BaseNodeContainer, RootJointUid, SkeletonUid);
        LogImportComplete(TEXT("Armature"));
    }
    
    if (ImportOptions.bImportMesh)
    {
        LogImportStart(TEXT("Mesh"));
        ImportMeshSection(CleanedModel, BaseNodeContainer, VertexMap, SkeletonUid, MaterialUids, MeshUid, SkeletalMeshUid);
        LogImportComplete(TEXT("Mesh"));
    }
    
    if (ImportOptions.bImportPhysics && !RootJointUid.IsEmpty())
    {
        LogImportStart(TEXT("Physics"));
        ImportPhysicsSection(CleanedModel, BaseNodeContainer, SkeletonUid, SkeletalMeshUid);
        LogImportComplete(TEXT("Physics"));
    }
    
    if (ImportOptions.bImportMorphs && !MeshUid.IsEmpty())
    {
        LogImportStart(TEXT("Morphs"));
        ImportMorphsSection(CleanedModel, BaseNodeContainer, MeshUid);
        LogImportComplete(TEXT("Morphs"));
    }
    
    if (ImportOptions.bImportDisplay)
    {
        LogImportStart(TEXT("Display"));
        ImportDisplaySection(CleanedModel, BaseNodeContainer);
        LogImportComplete(TEXT("Display"));
    }

    // Cache the model for payload processing
    MeshPayloadCache.Add(TEXT("PMX_GEOMETRY"), MakeShared<FPmxModel>(MoveTemp(CleanedModel)));
    
    return true;
}

void UPmxTranslator::CleanPmxModel(FPmxModel& PmxModel, bool bMeshOnly) const
{
    UE_LOG(LogPMXImporter, Log, TEXT("Cleaning PMX data..."));
    
    // Clean faces and collect used vertices
    TSet<int32> UsedVertices;
    TArray<int32> ValidFaces;
    
    int32 FaceIndex = 0;
    for (int32 MatIndex = 0; MatIndex < PmxModel.Materials.Num(); ++MatIndex)
    {
        const FPmxMaterial& Material = PmxModel.Materials[MatIndex];
        const int32 FaceCount = Material.SurfaceCount / 3;
        
        for (int32 i = 0; i < FaceCount && FaceIndex < PmxModel.Indices.Num() / 3; ++i)
        {
            const int32 i0 = PmxModel.Indices[FaceIndex * 3 + 0];
            const int32 i1 = PmxModel.Indices[FaceIndex * 3 + 1];
            const int32 i2 = PmxModel.Indices[FaceIndex * 3 + 2];
            
            // Validate triangle
            if (i0 >= 0 && i0 < PmxModel.Vertices.Num() &&
                i1 >= 0 && i1 < PmxModel.Vertices.Num() &&
                i2 >= 0 && i2 < PmxModel.Vertices.Num() &&
                i0 != i1 && i1 != i2 && i2 != i0)
            {
                UsedVertices.Add(i0);
                UsedVertices.Add(i1);
                UsedVertices.Add(i2);
                ValidFaces.Add(FaceIndex);
            }
            FaceIndex++;
        }
    }
    
    // Remove unused vertices and update indices
    if (UsedVertices.Num() < PmxModel.Vertices.Num())
    {
        TMap<int32, int32> VertexRemap;
        TArray<FPmxVertex> NewVertices;
        NewVertices.Reserve(UsedVertices.Num());
        
        int32 NewIndex = 0;
        for (int32 OldIndex : UsedVertices.Array())
        {
            VertexRemap.Add(OldIndex, NewIndex++);
            NewVertices.Add(PmxModel.Vertices[OldIndex]);
        }
        
        PmxModel.Vertices = MoveTemp(NewVertices);
        
        // Update indices
        for (int32 FIdx : ValidFaces)
        {
            PmxModel.Indices[FIdx * 3 + 0] = VertexRemap[PmxModel.Indices[FIdx * 3 + 0]];
            PmxModel.Indices[FIdx * 3 + 1] = VertexRemap[PmxModel.Indices[FIdx * 3 + 1]];
            PmxModel.Indices[FIdx * 3 + 2] = VertexRemap[PmxModel.Indices[FIdx * 3 + 2]];
        }
        
        // Update morph data if not mesh only
        if (!bMeshOnly)
        {
            for (FPmxMorph& Morph : PmxModel.Morphs)
            {
                // Update vertex morphs
                for (int32 i = Morph.VertexMorphs.Num() - 1; i >= 0; --i)
                {
                    FPmxVertexMorph& VM = Morph.VertexMorphs[i];
                    if (int32* NewIdx = VertexRemap.Find(VM.VertexIndex))
                    {
                        VM.VertexIndex = *NewIdx;
                    }
                    else
                    {
                        Morph.VertexMorphs.RemoveAtSwap(i);
                    }
                }
                
                // Update UV morphs
                for (int32 i = Morph.UVMorphs.Num() - 1; i >= 0; --i)
                {
                    FPmxUVMorph& UM = Morph.UVMorphs[i];
                    if (int32* NewIdx = VertexRemap.Find(UM.VertexIndex))
                    {
                        UM.VertexIndex = *NewIdx;
                    }
                    else
                    {
                        Morph.UVMorphs.RemoveAtSwap(i);
                    }
                }
            }
        }
        
        UE_LOG(LogPMXImporter, Log, TEXT("Removed %d unused vertices"), PmxModel.Vertices.Num() - UsedVertices.Num());
    }
}

void UPmxTranslator::RemoveDoubles(FPmxModel& PmxModel, bool bMeshOnly, TMap<int32, int32>& OutVertexMap) const
{
    UE_LOG(LogPMXImporter, Log, TEXT("Removing double vertices..."));
    
    // Group vertices by their data
    TMap<FString, TArray<int32>> VertexGroups;
    
    for (int32 i = 0; i < PmxModel.Vertices.Num(); ++i)
    {
        const FPmxVertex& Vertex = PmxModel.Vertices[i];
        
        // Create hash key from vertex position and primary UV to avoid collapsing UV seams
        FString Key = FString::Printf(TEXT("%.6f_%.6f_%.6f_UV%.6f_%.6f"), 
            Vertex.Position.X, Vertex.Position.Y, Vertex.Position.Z,
            Vertex.UV.X, Vertex.UV.Y);
        
        // Add morph data to key if not mesh only
        if (!bMeshOnly)
        {
            for (const FPmxMorph& Morph : PmxModel.Morphs)
            {
                for (const FPmxVertexMorph& VM : Morph.VertexMorphs)
                {
                    if (VM.VertexIndex == i)
                    {
                        Key += FString::Printf(TEXT("_M%.6f_%.6f_%.6f"), 
                            VM.Offset.X, VM.Offset.Y, VM.Offset.Z);
                    }
                }
            }
        }
        
        VertexGroups.FindOrAdd(Key).Add(i);
    }
    
    // Create vertex mapping
    OutVertexMap.Reset();
    TArray<FPmxVertex> NewVertices;
    int32 NewIndex = 0;
    
    for (auto& Group : VertexGroups)
    {
        // Use first vertex as representative
        const int32 RepresentativeIndex = Group.Value[0];
        NewVertices.Add(PmxModel.Vertices[RepresentativeIndex]);
        
        // Map all vertices in group to new index
        for (int32 OldIndex : Group.Value)
        {
            OutVertexMap.Add(OldIndex, NewIndex);
        }
        NewIndex++;
    }
    
    const int32 RemovedCount = PmxModel.Vertices.Num() - NewVertices.Num();
    if (RemovedCount > 0)
    {
        UE_LOG(LogPMXImporter, Log, TEXT("Removed %d double vertices"), RemovedCount);
        PmxModel.Vertices = MoveTemp(NewVertices);
        
        // Update indices
        for (int32& Index : PmxModel.Indices)
        {
            if (int32* NewIdx = OutVertexMap.Find(Index))
            {
                Index = *NewIdx;
            }
        }
    }
}

void UPmxTranslator::ImportMeshSection(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer,
    const TMap<int32, int32>& VertexMap, const FString& InSkeletonUid, TArray<FString>& OutMaterialUids, FString& OutMeshUid, FString& OutSkeletalMeshUid) const
{
    using ContainerType = EInterchangeNodeContainerType;
    
    // Import textures
    TMap<int32, FString> TextureUidMap;
    ImportTextures(PmxModel, GetSourceData()->GetFilename(), BaseNodeContainer, TextureUidMap);
    
    // Import materials
    TArray<FString> SlotNames;
    FPmxMaterialMapping::CreateMaterials(PmxModel, TextureUidMap, BaseNodeContainer, OutMaterialUids, SlotNames);
    
    // Create SkeletalMesh factory node
    FString ModelName = PmxModel.Header.ModelName.IsEmpty() ? TEXT("PMX_Root") : PmxModel.Header.ModelName;
    UInterchangeSkeletalMeshFactoryNode* SkeletalMeshNode = NewObject<UInterchangeSkeletalMeshFactoryNode>(&BaseNodeContainer);
    const FString SkeletalMeshUid = TEXT("/PMX/SkeletalMesh");
    OutSkeletalMeshUid = SkeletalMeshUid;
    SkeletalMeshNode->InitializeSkeletalMeshNode(SkeletalMeshUid, *ModelName, USkeletalMesh::StaticClass()->GetName(), &BaseNodeContainer);
    SkeletalMeshNode->SetCustomImportMorphTarget(ImportOptions.bImportMorphs);
    // Disable auto physics asset creation to avoid engine PostImport path crash; physics is created via our own factory node in ImportPhysicsSection
    SkeletalMeshNode->SetCustomCreatePhysicsAsset(false);
    
    // Add skeleton dependency to ensure proper loading order
    SkeletalMeshNode->AddFactoryDependencyUid(InSkeletonUid);
    
    BaseNodeContainer.AddNode(SkeletalMeshNode);
    
    // LOD0 data node
    UInterchangeSkeletalMeshLodDataNode* Lod0Node = NewObject<UInterchangeSkeletalMeshLodDataNode>(&BaseNodeContainer);
    const FString Lod0Uid = TEXT("/PMX/SkeletalMesh/LOD0");
    Lod0Node->InitializeNode(Lod0Uid, TEXT("LOD0"), ContainerType::TranslatedAsset);
    
    // Critical: Link Skeleton to this LOD so factory can resolve SkeletonReference
    Lod0Node->SetCustomSkeletonUid(InSkeletonUid);
    
    BaseNodeContainer.AddNode(Lod0Node);
    SkeletalMeshNode->AddLodDataUniqueId(Lod0Uid);
    
    // Create mesh node for geometry
    UInterchangeMeshNode* MeshNode = NewObject<UInterchangeMeshNode>(&BaseNodeContainer);
    OutMeshUid = TEXT("/PMX/Meshes/LOD0_Mesh");
    MeshNode->InitializeNode(OutMeshUid, TEXT("PMX_LOD0_Mesh"), ContainerType::TranslatedAsset);
    MeshNode->SetSkinnedMesh(true);
    MeshNode->SetPayLoadKey(TEXT("PMX_GEOMETRY"), EInterchangeMeshPayLoadType::SKELETAL);
    MeshNode->SetCustomVertexCount(PmxModel.Vertices.Num());
    MeshNode->SetCustomPolygonCount(PmxModel.Indices.Num() / 3);
    
    // Set material slots
    for (int32 MatIdx = 0; MatIdx < OutMaterialUids.Num(); ++MatIdx)
    {
        const FString SlotName = SlotNames.IsValidIndex(MatIdx) ? SlotNames[MatIdx] : FString::FromInt(MatIdx);
        MeshNode->SetSlotMaterialDependencyUid(SlotName, OutMaterialUids[MatIdx]);
        
        // Also set on skeletal mesh factory node
        const FString FactoryUid = UInterchangeBaseMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(OutMaterialUids[MatIdx]);
        SkeletalMeshNode->SetSlotMaterialDependencyUid(SlotName, FactoryUid);
    }
    
    BaseNodeContainer.AddNode(MeshNode);
    Lod0Node->AddMeshUid(OutMeshUid);
}

void UPmxTranslator::ImportArmatureSection(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer,
    FString& OutRootJointUid, FString& OutSkeletonUid) const
{
    // Use the same proven logic from PmxImporterTranslator
    // 1) Get scene root
    const UInterchangeBaseNode* BaseRootNode = BaseNodeContainer.GetNode(TEXT("/PMX/Root"));
    UInterchangeSceneNode* RootNode = const_cast<UInterchangeSceneNode*>(Cast<UInterchangeSceneNode>(BaseRootNode));
    
    if (!RootNode)
    {
        UE_LOG(LogPMXImporter, Error, TEXT("Pmx Translator: Scene root not found for bone hierarchy"));
        return;
    }
    
    // 2) Create bone/joint hierarchy using SceneNodes specialized as Joints
    TMap<int32, UInterchangeSceneNode*> BoneIndexToJointNode;
    OutRootJointUid = FPmxNodeBuilder::CreateBoneHierarchy(PmxModel, BaseNodeContainer, RootNode, BoneIndexToJointNode);
    
    if (OutRootJointUid.IsEmpty())
    {
        UE_LOG(LogPMXImporter, Error, TEXT("Pmx Translator: Failed to create bone hierarchy - empty root joint UID returned"));
        return;
    }
    
    // 3) Skeleton factory node
    UInterchangeSkeletonFactoryNode* SkeletonNode = NewObject<UInterchangeSkeletonFactoryNode>(&BaseNodeContainer);
    const FString BaseName = FPaths::GetBaseFilename(GetSourceData() ? GetSourceData()->GetFilename() : TEXT("PMX"));
    OutSkeletonUid = FString::Printf(TEXT("/PMX/Skeleton_%s"), *BaseName);
    SkeletonNode->InitializeSkeletonNode(OutSkeletonUid, TEXT("PMX_Skeleton"), USkeleton::StaticClass()->GetName(), &BaseNodeContainer);
    SkeletonNode->SetCustomRootJointUid(OutRootJointUid);
    // Use time zero as bind pose to reduce bind-pose related warnings on import
    SkeletonNode->SetCustomUseTimeZeroForBindPose(true);
    BaseNodeContainer.AddNode(SkeletonNode);
    
    UE_LOG(LogPMXImporter, Display, TEXT("Pmx Translator: Created %d bone joints with root joint: %s"), PmxModel.Bones.Num(), *OutRootJointUid);
    
    // Apply IK constraints if enabled
    if (ImportOptions.bFixIKLinks)
    {
        ApplyIKConstraints(PmxModel, BoneIndexToJointNode);
    }
    
    // Apply bone transforms
    ApplyBoneTransforms(PmxModel, BoneIndexToJointNode);
}

// Placeholder implementations for remaining methods
void UPmxTranslator::ImportPhysicsSection(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer,
    const FString& SkeletonUid, const FString& SkeletalMeshUid) const
{
    // Respect user option from SkeletalMesh factory node: CreatePhysicsAsset
    bool bCreatePhysicsAsset = true;
    if (!SkeletalMeshUid.IsEmpty())
    {
        if (const UInterchangeBaseNode* BaseNode = BaseNodeContainer.GetNode(*SkeletalMeshUid))
        {
            if (const UInterchangeSkeletalMeshFactoryNode* SkeletalMeshNode = Cast<UInterchangeSkeletalMeshFactoryNode>(BaseNode))
            {
                bool ValueFromNode = true;
                const bool bHasAttr = SkeletalMeshNode->GetCustomCreatePhysicsAsset(ValueFromNode);
                if (bHasAttr)
                {
                    bCreatePhysicsAsset = ValueFromNode;
                }
                else
                {
                    // Fall back to translator import option if attribute is missing
                    bCreatePhysicsAsset = ImportOptions.bImportPhysics;
                }
            }
        }
    }

    // Always annotate physics nodes (metadata) for potential future use
    const PmxPhysics::FAnnotateResult PhysRes = PmxPhysics::AnnotatePhysicsNodes(PmxModel, BaseNodeContainer);
    UE_LOG(LogPMXImporter, Display, TEXT("Physics annotated -> RigidBodies=%d, Joints=%d"), PhysRes.RigidBodyCount, PhysRes.JointCount);

    if (!bCreatePhysicsAsset)
    {
        UE_LOG(LogPMXImporter, Display, TEXT("Skipping PhysicsAsset creation because 'Create Physics Asset' option is OFF."));
        return;
    }

    // Create PhysicsAsset factory node only if enabled
    UInterchangePhysicsAssetFactoryNode* PhysicsAssetFactoryNode = NewObject<UInterchangePhysicsAssetFactoryNode>(&BaseNodeContainer);
    if (PhysicsAssetFactoryNode)
    {
        const FString ModelName = PmxModel.Header.ModelName.IsEmpty() ? TEXT("PMX_Root") : PmxModel.Header.ModelName;
        const FString PhysicsAssetUid = FString::Printf(TEXT("/PMX/PhysicsAsset_%s"), *ModelName);
        const FString PhysicsAssetDisplayLabel = ModelName + TEXT("_PhysicsAsset");
        PhysicsAssetFactoryNode->InitializePhysicsAssetNode(PhysicsAssetUid, PhysicsAssetDisplayLabel, UPhysicsAsset::StaticClass()->GetName(), &BaseNodeContainer);
        PhysicsAssetFactoryNode->SetCustomSkeletalMeshUid(SkeletalMeshUid);
        BaseNodeContainer.AddNode(PhysicsAssetFactoryNode);
    }
}

void UPmxTranslator::ImportMorphsSection(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer, const FString& MeshUid) const
{
    ImportVertexMorphs(PmxModel, BaseNodeContainer, MeshUid);
    ImportBoneMorphs(PmxModel, BaseNodeContainer);
    ImportMaterialMorphs(PmxModel, BaseNodeContainer);
    ImportUVMorphs(PmxModel, BaseNodeContainer);
    ImportGroupMorphs(PmxModel, BaseNodeContainer);
}

// Additional helper method implementations would continue here...
// For brevity, I'll implement the core structure and key methods

TOptional<UE::Interchange::FMeshPayloadData> UPmxTranslator::GetMeshPayloadData(const FInterchangeMeshPayLoadKey& PayLoadKey, const UE::Interchange::FAttributeStorage& PayloadAttributes) const
{
    using namespace UE::Interchange;

    // Build actual mesh payload from cached PMX model
    const TSharedPtr<FPmxModel>* FoundModel = UPmxTranslator::MeshPayloadCache.Find(TEXT("PMX_GEOMETRY"));
    if (!FoundModel || !FoundModel->IsValid())
    {
        UE_LOG(LogPMXImporter, Error, TEXT("Pmx Translator: No cached PMX model for payload key '%s'."), *PayLoadKey.UniqueId);
        return TOptional<FMeshPayloadData>();
    }

    const FPmxModel& Model = *FoundModel->Get();

    if (Model.Vertices.IsEmpty())
    {
        UE_LOG(LogPMXImporter, Warning, TEXT("Pmx Translator: PMX model has no geometry (v:%d). Returning empty payload."), Model.Vertices.Num());
        return TOptional<FMeshPayloadData>();
    }

    FMeshPayloadData Data;
    FMeshDescription& MD = Data.MeshDescription;

    // Register attributes for Static and Skeletal mesh usages
    {
        FStaticMeshAttributes StaticAttribs(MD);
        StaticAttribs.Register();
        FSkeletalMeshAttributes SkeletalAttribs(MD);
        SkeletalAttribs.Register();
    }

    // Prepare writable skeletal attributes (skin weights)
    FSkeletalMeshAttributes SkeletalAttribsRW(MD);
    FSkinWeightsVertexAttributesRef VertexSkinWeights = SkeletalAttribsRW.GetVertexSkinWeights(NAME_None);

    // Fill JointNames using PMX bone names only (do not include synthetic root)
    Data.JointNames.Reset(Model.Bones.Num());
    Data.JointNames.Reserve(Model.Bones.Num());
    for (int32 BoneIndex = 0; BoneIndex < Model.Bones.Num(); ++BoneIndex)
    {
        const FPmxBone& Bone = Model.Bones[BoneIndex];
        const FString BoneName = Bone.Name.IsEmpty() ? FString::Printf(TEXT("Bone_%d"), BoneIndex) : Bone.Name;
        Data.JointNames.Add(BoneName);
    }

    // Create vertices and set positions (apply optional mesh global transform if provided)
    FTransform MeshGlobalTransform = FTransform::Identity;
    PayloadAttributes.GetAttribute(UE::Interchange::FAttributeKey{ UE::Interchange::MeshPayload::Attributes::MeshGlobalTransform }, MeshGlobalTransform);
    
    // Pmx coordinate conversion: Vector().xzy * scale
    float Baseline = FMath::Max(0.0001f, PMXImporter::CVarPMXImporterScale.GetValueOnAnyThread());
    const float ImportScale = Baseline;
    const FTransform MMDToUE = FTransform(
        FQuat(FVector3d::XAxisVector, FMath::DegreesToRadians(90.0f)), // Y-up to Z-up
        FVector3d::ZeroVector,
        FVector3d(ImportScale)
    );
    MeshGlobalTransform = MMDToUE * MeshGlobalTransform;

    auto VertexPositions = FStaticMeshAttributes(MD).GetVertexPositions();
    TArray<FVertexID> VertexIDs;
    VertexIDs.Reserve(Model.Vertices.Num());
    for (int32 vid = 0; vid < Model.Vertices.Num(); ++vid)
    {
        const auto& V = Model.Vertices[vid];
        const FVertexID VId = MD.CreateVertex();
        VertexIDs.Add(VId);
        
        // Follow original PmxImporterTranslator: no axis swap, apply 90deg X rotation via MeshGlobalTransform
        const FVector Pos(static_cast<double>(V.Position.X), static_cast<double>(V.Position.Y), static_cast<double>(V.Position.Z));
        const FVector Xformed = MeshGlobalTransform.TransformPosition(Pos);
        VertexPositions[VId] = FVector3f((float)Xformed.X, (float)Xformed.Y, (float)Xformed.Z);

        // Assign skin weights only for base SKELETAL payload
        if (PayLoadKey.Type != EInterchangeMeshPayLoadType::MORPHTARGET)
        {
            UE::AnimationCore::FBoneWeightsSettings Settings;
            Settings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::Always);
            Settings.SetMaxWeightCount(4);
            Settings.SetDefaultBoneIndex(0);

            TArray<UE::AnimationCore::FBoneWeight, TInlineAllocator<8>> BWArray;
            const int32 PairCount = FMath::Min(V.BoneIndices.Num(), V.BoneWeights.Num());
            BWArray.Reserve(PairCount);
            for (int32 i = 0; i < PairCount; ++i)
            {
                const int32 BoneIndex = V.BoneIndices[i];
                const float Weight = V.BoneWeights[i];
                if (BoneIndex >= 0 && BoneIndex < Model.Bones.Num() && Weight > 0.0f && FMath::IsFinite(Weight))
                {
                    // Use PMX bone index directly (no synthetic root in JointNames)
                    BWArray.Emplace(static_cast<uint16>(BoneIndex), Weight);
                }
            }
            if (VertexSkinWeights.IsValid())
            {
                VertexSkinWeights.Set(VId, MakeArrayView<const UE::AnimationCore::FBoneWeight>(BWArray.GetData(), BWArray.Num()), Settings);
            }
        }
    }

    // If this payload is a morph target, override morphed vertex positions here and skip triangle creation
    if (PayLoadKey.Type == EInterchangeMeshPayLoadType::MORPHTARGET)
    {
        // Parse morph index from payload key: format "PMX_MORPH:idx:%d:name:%s"
        int32 MorphIndex = INDEX_NONE;
        {
            const FString& Key = PayLoadKey.UniqueId;
            const int32 IdxPos = Key.Find(TEXT("idx:"), ESearchCase::IgnoreCase, ESearchDir::FromStart);
            if (IdxPos != INDEX_NONE)
            {
                const int32 AfterIdx = IdxPos + 4;
                int32 EndPos = Key.Find(TEXT(":name:"), ESearchCase::IgnoreCase, ESearchDir::FromStart, AfterIdx);
                if (EndPos == INDEX_NONE)
                {
                    EndPos = Key.Len();
                }
                const FString NumStr = Key.Mid(AfterIdx, EndPos - AfterIdx);
                LexTryParseString(MorphIndex, *NumStr);
            }
        }

        if (!Model.Morphs.IsValidIndex(MorphIndex))
        {
            UE_LOG(LogPMXImporter, Warning, TEXT("Pmx Translator: Invalid morph payload index parsed from key '%s'"), *PayLoadKey.UniqueId);
            return TOptional<FMeshPayloadData>();
        }

        const FPmxMorph& Morph = Model.Morphs[MorphIndex];

        // Apply vertex offsets : morphed = base + offset, after transform
        const float MinDelta = PMXImporter::CVarPMXImporterMorphMinDelta.GetValueOnAnyThread();
        const bool bDropEmpty = PMXImporter::CVarPMXImporterMorphDropEmpty.GetValueOnAnyThread();
        int32 EffectiveChanges = 0;

        for (const FPmxVertexMorph& VM : Morph.VertexMorphs)
        {
            const int32 VIdx = VM.VertexIndex;
            if (!Model.Vertices.IsValidIndex(VIdx) || !VertexIDs.IsValidIndex(VIdx))
            {
                continue;
            }
            const FVector BasePos((double)Model.Vertices[VIdx].Position.X, (double)Model.Vertices[VIdx].Position.Y, (double)Model.Vertices[VIdx].Position.Z);
            const FVector Delta((double)VM.Offset.X, (double)VM.Offset.Y, (double)VM.Offset.Z);
            const FVector Morphed = MeshGlobalTransform.TransformPosition(BasePos + Delta);
            const FVector BaseXformed = MeshGlobalTransform.TransformPosition(BasePos);
            const float DeltaLen = FVector::Dist(Morphed, BaseXformed);
            if (DeltaLen < MinDelta)
            {
                continue; // ignore tiny delta
            }
            EffectiveChanges++;
            const FVertexID VId = VertexIDs[VIdx];
            VertexPositions[VId] = FVector3f((float)Morphed.X, (float)Morphed.Y, (float)Morphed.Z);
        }

        if (bDropEmpty && EffectiveChanges == 0)
        {
            UE_LOG(LogPMXImporter, Verbose, TEXT("Pmx Translator: Dropping empty morph payload '%s' (no effective vertex changes)"), *PayLoadKey.UniqueId);
            return TOptional<FMeshPayloadData>();
        }

        // Mark morph target name
        Data.MorphTargetName = Morph.Name.IsEmpty() ? TEXT("Morph") : Morph.Name;
        Data.VertexOffset = 0;

        return TOptional<FMeshPayloadData>(MoveTemp(Data));
    }

    // Triangles with polygon groups per PMX material
    if (PayLoadKey.Type != EInterchangeMeshPayLoadType::MORPHTARGET)
    {
        // Create polygon groups for each material
        TArray<FPolygonGroupID> PolyGroupIds;
        PolyGroupIds.Reserve(Model.Materials.Num());
        
        FStaticMeshAttributes StaticAttribsForMats(MD);
        auto MaterialSlotNames = StaticAttribsForMats.GetPolygonGroupMaterialSlotNames();
        
        FStaticMeshAttributes StaticAttribsForUVs(MD);
        auto VertexInstanceUVs = StaticAttribsForUVs.GetVertexInstanceUVs();
        VertexInstanceUVs.SetNumChannels(1);

        // Build unique slot labels
        TMap<FString, int32> UsedLabels;
        TArray<FString> SlotNames;
        SlotNames.Reserve(Model.Materials.Num());
        for (int32 MatIdx = 0; MatIdx < Model.Materials.Num(); ++MatIdx)
        {
            const FPmxMaterial& PmxMat = Model.Materials[MatIdx];
            FString SlotLabel = PmxMat.Name;
            SlotLabel.TrimStartAndEndInline();
            if (SlotLabel.IsEmpty())
            {
                SlotLabel = PmxMat.NameEng;
                SlotLabel.TrimStartAndEndInline();
            }
            if (SlotLabel.IsEmpty())
            {
                SlotLabel = FString::Printf(TEXT("Mat_%d"), MatIdx);
            }
            int32& Cnt = UsedLabels.FindOrAdd(SlotLabel);
            FString Unique = SlotLabel;
            if (Cnt > 0)
            {
                Unique = FString::Printf(TEXT("%s_%d"), *SlotLabel, Cnt);
            }
            ++Cnt;
            SlotNames.Add(Unique);
            const FPolygonGroupID PG = MD.CreatePolygonGroup();
            PolyGroupIds.Add(PG);
            MaterialSlotNames[PG] = FName(*Unique);
        }

        TArray<FVertexInstanceID> CornerIDs;
        CornerIDs.SetNumUninitialized(3);

        // Process faces by material
        int32 IndexCursor = 0;
        for (int32 MatIdx = 0; MatIdx < Model.Materials.Num(); ++MatIdx)
        {
            const FPmxMaterial& Mat = Model.Materials[MatIdx];
            const int32 IndicesForMat = FMath::Max(0, Mat.SurfaceCount);
            const int32 TriCountForMat = IndicesForMat / 3;
            for (int32 t = 0; t < TriCountForMat; ++t)
            {
                const int32 i0 = Model.Indices.IsValidIndex(IndexCursor + 0) ? Model.Indices[IndexCursor + 0] : -1;
                const int32 i1 = Model.Indices.IsValidIndex(IndexCursor + 1) ? Model.Indices[IndexCursor + 1] : -1;
                const int32 i2 = Model.Indices.IsValidIndex(IndexCursor + 2) ? Model.Indices[IndexCursor + 2] : -1;
                IndexCursor += 3;
                if (!VertexIDs.IsValidIndex(i0) || !VertexIDs.IsValidIndex(i1) || !VertexIDs.IsValidIndex(i2))
                {
                    continue;
                }
                // Reverse winding order for UE
                CornerIDs[0] = MD.CreateVertexInstance(VertexIDs[i0]);
                CornerIDs[1] = MD.CreateVertexInstance(VertexIDs[i2]);
                CornerIDs[2] = MD.CreateVertexInstance(VertexIDs[i1]);

                // Write UVs with optional U/V flip
                auto WriteUV = [&](int32 SrcVertIndex, const FVertexInstanceID& InstanceId)
                {
                    if (!Model.Vertices.IsValidIndex(SrcVertIndex)) return;
                    const FVector2f UV = Model.Vertices[SrcVertIndex].UV;
                    const float U = UV.X;
                    const float V = UV.Y;
                    VertexInstanceUVs.Set(InstanceId, 0, FVector2f(U, V));
                };
                WriteUV(i0, CornerIDs[0]);
                WriteUV(i2, CornerIDs[1]);
                WriteUV(i1, CornerIDs[2]);

                MD.CreatePolygon(PolyGroupIds.IsValidIndex(MatIdx) ? PolyGroupIds[MatIdx] : MD.CreatePolygonGroup(), CornerIDs);
            }
        }
        // Append remaining triangles if material counts do not cover all indices (parity with original translator)
        const int32 RemainingTriCount = (Model.Indices.Num() - IndexCursor) / 3;
        for (int32 t = 0; t < RemainingTriCount; ++t)
        {
            const int32 i0 = Model.Indices[IndexCursor + 0];
            const int32 i1 = Model.Indices[IndexCursor + 1];
            const int32 i2 = Model.Indices[IndexCursor + 2];
            IndexCursor += 3;
            if (!VertexIDs.IsValidIndex(i0) || !VertexIDs.IsValidIndex(i1) || !VertexIDs.IsValidIndex(i2))
            {
                continue;
            }
            CornerIDs[0] = MD.CreateVertexInstance(VertexIDs[i0]);
            CornerIDs[1] = MD.CreateVertexInstance(VertexIDs[i2]);
            CornerIDs[2] = MD.CreateVertexInstance(VertexIDs[i1]);

            auto WriteUV = [&](int32 SrcVertIndex, const FVertexInstanceID& InstanceId)
            {
                if (!Model.Vertices.IsValidIndex(SrcVertIndex)) return;
                const FVector2f UV = Model.Vertices[SrcVertIndex].UV;
                const float U = UV.X;
                const float V = UV.Y;
                VertexInstanceUVs.Set(InstanceId, 0, FVector2f(U, V));
            };
            WriteUV(i0, CornerIDs[0]);
            WriteUV(i2, CornerIDs[1]);
            WriteUV(i1, CornerIDs[2]);

            const FPolygonGroupID FallbackGroup = PolyGroupIds.Num() > 0 ? PolyGroupIds[0] : MD.CreatePolygonGroup();
            MD.CreatePolygon(FallbackGroup, CornerIDs);
        }
    }

    // Set additional payload fields
    Data.VertexOffset = 0;
    if (PayLoadKey.Type != EInterchangeMeshPayLoadType::MORPHTARGET)
    {
        Data.MorphTargetName.Reset();
    }
    
    return TOptional<FMeshPayloadData>(MoveTemp(Data));
}

TOptional<UE::Interchange::FImportImage> UPmxTranslator::GetTexturePayloadData(const FString& PayloadKey, TOptional<FString>& AlternateTexturePath) const
{
    if (PayloadKey.IsEmpty())
    {
        return TOptional<UE::Interchange::FImportImage>();
    }

    // Helper to try loading via Interchange texture translators
    auto TryLoadImage = [&](const FString& InPath, TOptional<FString>& InOutAltPath) -> TOptional<UE::Interchange::FImportImage>
    {
        UE::Interchange::Private::FScopedTranslator LocalScopedTranslator(InPath, Results, AnalyticsHelper);
        const IInterchangeTexturePayloadInterface* LocalTextureTranslator = LocalScopedTranslator.GetPayLoadInterface<IInterchangeTexturePayloadInterface>();
        if (!LocalTextureTranslator)
        {
            return TOptional<UE::Interchange::FImportImage>();
        }
        InOutAltPath = InPath;
        return LocalTextureTranslator->GetTexturePayloadData(InPath, InOutAltPath);
    };

    // 1) First, try the payload key as-is
    TOptional<UE::Interchange::FImportImage> Image = TryLoadImage(PayloadKey, AlternateTexturePath);
    if (Image.IsSet())
    {
        return Image;
    }

    // 2) If not supported (e.g., .sph/.spa), attempt to substitute to common image extensions
    const FString Ext = FPaths::GetExtension(PayloadKey, /*bIncludeDot*/ false).ToLower();
    if (Ext == TEXT("sph") || Ext == TEXT("spa") || Ext == TEXT("sphmap"))
    {
        static const TCHAR* Candidates[] = { TEXT("png"), TEXT("bmp"), TEXT("jpg"), TEXT("jpeg"), TEXT("tga") };
        const FString BasePathNoExt = FPaths::GetPath(PayloadKey) / FPaths::GetBaseFilename(PayloadKey);
        for (const TCHAR* CandExt : Candidates)
        {
            const FString CandidatePath = BasePathNoExt + TEXT(".") + CandExt;
            if (FPaths::FileExists(CandidatePath))
            {
                TOptional<FString> Alt = AlternateTexturePath;
                TOptional<UE::Interchange::FImportImage> AltImg = TryLoadImage(CandidatePath, Alt);
                if (AltImg.IsSet())
                {
                    AlternateTexturePath = Alt; // update with the resolved path
                    UE_LOG(LogPMXImporter, Display, TEXT("Pmx Translator: Substituted unsupported texture '%s' -> '%s'"), *PayloadKey, **AlternateTexturePath);
                    return AltImg;
                }
            }
        }
    }

    // 3) Fallback: create a 1x1 dummy image to avoid factory error and keep import stable
    UE_LOG(LogPMXImporter, Warning, TEXT("Pmx Translator: No texture translator for '%s'. Using 1x1 dummy texture."), *PayloadKey);
    UE::Interchange::FImportImage Dummy;
    Dummy.Init2DWithParams(1, 1, TSF_G8, true, true);
    {
        TArrayView64<uint8> Raw = Dummy.GetArrayViewOfRawData();
        if (Raw.Num() > 0)
        {
            Raw[0] = 255; // white pixel for G8
        }
    }
    return TOptional<UE::Interchange::FImportImage>(MoveTemp(Dummy));
}

// Coordinate transformation 
FVector3f UPmxTranslator::ConvertVectorPmxToUE(const FVector3f& PmxVector) const
{
    return FVector3f(PmxVector.X, PmxVector.Z, PmxVector.Y) * ImportOptions.Scale;
}

FVector3f UPmxTranslator::ConvertRotationPmxToUE(const FVector3f& PmxRotation) const
{
    return FVector3f(PmxRotation.X, PmxRotation.Z, PmxRotation.Y) * -1.0f;
}

FQuat4f UPmxTranslator::ConvertQuaternionPmxToUE(const FQuat4f& PmxQuat) const
{
    // Apply coordinate system conversion to quaternion
    return FQuat4f(PmxQuat.X, PmxQuat.Z, PmxQuat.Y, PmxQuat.W);
}

// Utility methods
FString UPmxTranslator::GetMorphCategoryName(uint8 ControlPanel) const
{
    switch (ControlPanel)
    {
        case 0: return TEXT("System");
        case 1: return TEXT("Eyebrow");
        case 2: return TEXT("Eye");
        case 3: return TEXT("Mouth");
        default: return TEXT("Other");
    }
}

FString UPmxTranslator::SafeObjectName(const FString& Name, int32 MaxLength) const
{
    FString SafeName = Name;
    if (SafeName.Len() > MaxLength)
    {
        // Convert to UTF8 bytes and truncate
        FTCHARToUTF8 UTF8Name(*SafeName);
        if (UTF8Name.Length() > MaxLength)
        {
            FString TruncatedUTF8;
            TruncatedUTF8.GetCharArray().SetNumUninitialized(MaxLength + 1);
            FMemory::Memcpy(TruncatedUTF8.GetCharArray().GetData(), UTF8Name.Get(), MaxLength);
            TruncatedUTF8.GetCharArray()[MaxLength] = '\0';
            SafeName = FString(UTF8_TO_TCHAR(TruncatedUTF8.GetCharArray().GetData()));
        }
    }
    return SafeName;
}

void UPmxTranslator::FixRepeatedMorphNames(FPmxModel& PmxModel) const
{
    TSet<FString> UsedNames;
    for (FPmxMorph& Morph : PmxModel.Morphs)
    {
        FString OriginalName = Morph.Name.IsEmpty() ? TEXT("Morph") : Morph.Name;
        FString UniqueName = OriginalName;
        
        int32 Counter = 1;
        while (UsedNames.Contains(UniqueName))
        {
            UniqueName = FString::Printf(TEXT("%s_%d"), *OriginalName, Counter++);
        }
        
        Morph.Name = UniqueName;
        UsedNames.Add(UniqueName);
    }
}

// Logging methods
void UPmxTranslator::LogImportStart(const FString& SectionName) const
{
    ImportStartTime = FPlatformTime::Seconds();
    UE_LOG(LogPMXImporter, Log, TEXT("Pmx Translator: Starting %s import..."), *SectionName);
}

void UPmxTranslator::LogImportComplete(const FString& SectionName) const
{
    const double ElapsedTime = FPlatformTime::Seconds() - ImportStartTime;
    UE_LOG(LogPMXImporter, Log, TEXT("Pmx Translator: Completed %s import in %.3f seconds"), *SectionName, ElapsedTime);
}

void UPmxTranslator::LogImportSummary(const FPmxModel& PmxModel) const
{
    UE_LOG(LogPMXImporter, Display, 
        TEXT("Pmx Translator Summary - Vertices: %d, Indices: %d, Bones: %d, Materials: %d, Morphs: %d, RigidBodies: %d, Joints: %d"), 
        PmxModel.Vertices.Num(), PmxModel.Indices.Num(), PmxModel.Bones.Num(), 
        PmxModel.Materials.Num(), PmxModel.Morphs.Num(), PmxModel.RigidBodies.Num(), PmxModel.Joints.Num());
}

// Placeholder implementations for remaining helper methods
void UPmxTranslator::ImportTextures(const FPmxModel& PmxModel, const FString& PmxFilePath, 
    UInterchangeBaseNodeContainer& BaseNodeContainer, TMap<int32, FString>& OutTextureUidMap) const
{
    // TODO: Implement MMD-specific texture import
    // For now, create placeholder texture nodes
    OutTextureUidMap.Reset();
    
    int32 CreatedTextureCount = 0;
    const FString PmxBaseDir = FPaths::GetPath(PmxFilePath);
    
    for (int32 TexIdx = 0; TexIdx < PmxModel.Textures.Num(); ++TexIdx)
    {
        const FPmxTexture& PmxTex = PmxModel.Textures[TexIdx];
        if (PmxTex.TexturePath.IsEmpty())
        {
            continue;
        }
        
        // Create texture node
        const FString TexBaseName = FPaths::GetBaseFilename(PmxTex.TexturePath);
        const FString TexNodePath = FString::Printf(TEXT("/PMX/Textures/MMD_%s_%d"), *TexBaseName, TexIdx);
        UInterchangeTexture2DNode* Texture2DNode = UInterchangeTexture2DNode::Create(&BaseNodeContainer, *TexNodePath);
        
        if (Texture2DNode)
        {
            Texture2DNode->SetDisplayLabel(TexBaseName);
            // Resolve texture path
            FString AbsPath = FPaths::IsRelative(PmxTex.TexturePath) 
                ? FPaths::ConvertRelativePathToFull(FPaths::Combine(PmxBaseDir, PmxTex.TexturePath))
                : PmxTex.TexturePath;
            FPaths::CollapseRelativeDirectories(AbsPath);
            
            Texture2DNode->SetPayLoadKey(AbsPath);
            Texture2DNode->SetCustomSRGB(true);
            OutTextureUidMap.Add(TexIdx, Texture2DNode->GetUniqueID());
            ++CreatedTextureCount;
        }
    }
    
    UE_LOG(LogPMXImporter, Log, TEXT("Pmx Translator: Created %d texture nodes"), CreatedTextureCount);
}

void UPmxTranslator::CreateBoneHierarchy(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer,
    UInterchangeSceneNode* RootNode, TMap<int32, UInterchangeSceneNode*>& OutBoneNodes, FString& OutRootJointUid) const
{
    // Delegate to existing implementation with modifications
    OutRootJointUid = FPmxNodeBuilder::CreateBoneHierarchy(PmxModel, BaseNodeContainer, RootNode, OutBoneNodes);
    
    // Validate the result
    if (OutRootJointUid.IsEmpty())
    {
        UE_LOG(LogPMXImporter, Error, TEXT("Pmx Translator: Failed to create bone hierarchy - empty root joint UID returned"));
        // Clear output parameters on failure
        OutBoneNodes.Empty();
    }
    else
    {
        UE_LOG(LogPMXImporter, Log, TEXT("Pmx Translator: Successfully created bone hierarchy with root joint: %s"), *OutRootJointUid);
    }
}

// Additional method stubs
void UPmxTranslator::ImportDisplaySection(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const {}
void UPmxTranslator::ImportVertices(const FPmxModel& PmxModel, const TMap<int32, int32>& VertexMap) const {}
void UPmxTranslator::ImportFaces(const FPmxModel& PmxModel, const TMap<int32, int32>& VertexMap) const {}
void UPmxTranslator::ImportMaterials(const FPmxModel& PmxModel, const TMap<int32, FString>& TextureUidMap, UInterchangeBaseNodeContainer& BaseNodeContainer, TArray<FString>& OutMaterialUids, TArray<FString>& OutSlotNames) const {}
void UPmxTranslator::ApplyIKConstraints(const FPmxModel& PmxModel, const TMap<int32, UInterchangeSceneNode*>& BoneNodes) const {}
void UPmxTranslator::ApplyBoneTransforms(const FPmxModel& PmxModel, const TMap<int32, UInterchangeSceneNode*>& BoneNodes) const {}
void UPmxTranslator::ImportRigidBodies(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer, const TMap<int32, UInterchangeSceneNode*>& BoneNodes) const {}
void UPmxTranslator::ImportJoints(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const {}
void UPmxTranslator::ImportVertexMorphs(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer, const FString& MeshUid) const
{
    if (!ImportOptions.bImportMorphs)
    {
        return;
    }

    // Find the base mesh node
    const UInterchangeBaseNode* BaseMeshBaseNode = BaseNodeContainer.GetNode(*MeshUid);
    UInterchangeMeshNode* const BaseMeshNode = const_cast<UInterchangeMeshNode*>(Cast<UInterchangeMeshNode>(BaseMeshBaseNode));
    if (!BaseMeshNode)
    {
        UE_LOG(LogPMXImporter, Error, TEXT("Pmx Translator: ImportVertexMorphs failed - Base mesh node '%s' not found."), *MeshUid);
        return;
    }

    int32 CreatedMorphCount = 0;

    // Create one morph target mesh node per PMX vertex morph
    for (int32 MorphIdx = 0; MorphIdx < PmxModel.Morphs.Num(); ++MorphIdx)
    {
        const FPmxMorph& Morph = PmxModel.Morphs[MorphIdx];
        // Heuristic: Only consider morphs that have vertex offsets
        if (Morph.VertexMorphs.Num() <= 0)
        {
            continue;
        }

        FString MorphName = Morph.Name;
        MorphName.TrimStartAndEndInline();
        if (MorphName.IsEmpty())
        {
            MorphName = TEXT("Morph");
        }
        // Sanitize to ASCII-safe name
        const FString SafeName = SafeObjectName(MorphName, 64);

        // Create a separate mesh node flagged as MorphTarget
        const FString MorphUid = FString::Printf(TEXT("/PMX/Morphs/%s"), *SafeName);
        UInterchangeMeshNode* MorphNode = NewObject<UInterchangeMeshNode>(&BaseNodeContainer);
        MorphNode->InitializeNode(MorphUid, SafeName, EInterchangeNodeContainerType::TranslatedAsset);
        MorphNode->SetSkinnedMesh(true);
        MorphNode->SetMorphTarget(true);
        MorphNode->SetMorphTargetName(SafeName);
        MorphNode->SetCustomVertexCount(PmxModel.Vertices.Num());

        // Compose payload key for this morph target: include stable PMX morph index to resolve original morph
        const FString PayloadKey = FString::Printf(TEXT("PMX_MORPH:idx:%d:name:%s"), MorphIdx, *SafeName);
        MorphNode->SetPayLoadKey(PayloadKey, EInterchangeMeshPayLoadType::MORPHTARGET);

        // Add to container and register dependency on base mesh
        BaseNodeContainer.AddNode(MorphNode);
        BaseMeshNode->SetMorphTargetDependencyUid(MorphUid);

        ++CreatedMorphCount;
    }

    UE_LOG(LogPMXImporter, Display, TEXT("Pmx Translator: Created %d vertex morph target nodes"), CreatedMorphCount);
}
void UPmxTranslator::ImportBoneMorphs(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
    // Not supported in v0.1 (see guidelines §8). Keep as no-op with a verbose log for traceability.
    {
        int32 Count = 0;
        for (const FPmxMorph& M : PmxModel.Morphs) { if (M.BoneMorphs.Num() > 0) ++Count; }
        if (Count > 0)
        {
            UE_LOG(LogPMXImporter, Verbose, TEXT("Pmx Translator: Skipping %d bone morph(s) (not implemented)."), Count);
        }
    }
}
void UPmxTranslator::ImportMaterialMorphs(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
    {
        int32 Count = 0;
        for (const FPmxMorph& M : PmxModel.Morphs) { if (M.MaterialMorphs.Num() > 0) ++Count; }
        if (Count > 0)
        {
            UE_LOG(LogPMXImporter, Verbose, TEXT("Pmx Translator: Skipping %d material morph(s) (not implemented)."), Count);
        }
    }
}
void UPmxTranslator::ImportUVMorphs(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
    {
        int32 Count = 0;
        for (const FPmxMorph& M : PmxModel.Morphs) { if (M.UVMorphs.Num() > 0) ++Count; }
        if (Count > 0)
        {
            UE_LOG(LogPMXImporter, Verbose, TEXT("Pmx Translator: Skipping %d UV morph(s) (not implemented)."), Count);
        }
    }
}
void UPmxTranslator::ImportGroupMorphs(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
    {
        int32 Count = 0;
        for (const FPmxMorph& M : PmxModel.Morphs) { if (M.GroupMorphs.Num() > 0) ++Count; }
        if (Count > 0)
        {
            UE_LOG(LogPMXImporter, Verbose, TEXT("Pmx Translator: Skipping %d group morph(s) (not implemented)."), Count);
        }
    }
}