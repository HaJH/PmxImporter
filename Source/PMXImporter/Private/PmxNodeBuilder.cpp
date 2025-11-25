// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved.

#include "PmxNodeBuilder.h"

#include "Nodes/InterchangeBaseNodeContainer.h"
#include "InterchangeSceneNode.h"
#include "LogPMXImporter.h"
#include "HAL/IConsoleManager.h"
#include "PmxUtils.h"

// Console variables needed for node building
UInterchangeSceneNode* FPmxNodeBuilder::CreateSceneRoot(const FPmxModel& PmxModel, 
	UInterchangeBaseNodeContainer& BaseNodeContainer)
{
	using ContainerType = EInterchangeNodeContainerType;
	
	UInterchangeSceneNode* RootNode = NewObject<UInterchangeSceneNode>(&BaseNodeContainer);
	FString ModelName = PmxModel.Header.ModelName.IsEmpty() ? TEXT("PMX_Root") : PmxModel.Header.ModelName;
	RootNode->InitializeNode(TEXT("/PMX/Root"), *ModelName, ContainerType::TranslatedScene);
	BaseNodeContainer.AddNode(RootNode);
	
	return RootNode;
}

FString FPmxNodeBuilder::CreateBoneHierarchy(const FPmxModel& PmxModel, UInterchangeBaseNodeContainer& BaseNodeContainer, 
	UInterchangeSceneNode* RootNode, TMap<int32, UInterchangeSceneNode*>& OutBoneIndexToJointNode)
{
	using ContainerType = EInterchangeNodeContainerType;
	
	// Validate input parameters
	if (!RootNode)
	{
		UE_LOG(LogPMXImporter, Error, TEXT("PMX NodeBuilder: RootNode is null in CreateBoneHierarchy"));
		return FString();
	}
	
	// Verify RootNode is properly initialized
	const FString RootNodeUid = RootNode->GetUniqueID();
	if (RootNodeUid.IsEmpty())
	{
		UE_LOG(LogPMXImporter, Error, TEXT("PMX NodeBuilder: RootNode has empty UID in CreateBoneHierarchy"));
		return FString();
	}
	
	// Create a single Joint Root node to ensure a unified bone hierarchy for the Skeleton
	UInterchangeSceneNode* RootJoint = NewObject<UInterchangeSceneNode>(&BaseNodeContainer);
	// Build a model-specific joints prefix to avoid UID collisions between imports
	FString SafeModelName = PmxModel.Header.ModelName.IsEmpty() ? TEXT("PMX") : PmxModel.Header.ModelName;
	// Sanitize name to ASCII token for UIDs
	for (TCHAR& C : SafeModelName)
	{
		if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == TCHAR('.') || C == TCHAR('_') || C == TCHAR('-')))
		{
			C = TCHAR('_');
		}
	}
	const FString JointsPrefix = FString::Printf(TEXT("/PMX/Joints/%s"), *SafeModelName);
	const FString RootJointUid = JointsPrefix + TEXT("/Root");
	RootJoint->InitializeNode(RootJointUid, TEXT("Root"), ContainerType::TranslatedScene);
	RootJoint->AddSpecializedType(UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString());
	RootJoint->SetCustomLocalTransform(&BaseNodeContainer, FTransform::Identity);
	BaseNodeContainer.AddNode(RootJoint);
	BaseNodeContainer.SetNodeParentUid(RootJoint->GetUniqueID(), RootNodeUid);
	
	// Strategy A variant: Keep a single synthetic Joint Root in the node graph, but do not add it to mesh JointNames.
	// Parent all top-level PMX bones to this Joint Root so the skeleton has a single hierarchy.
	// Precompute transformed (UE) bone positions and local offsets
	TArray<FVector> BonePosUE;
	BonePosUE.SetNum(PmxModel.Bones.Num());
	const FQuat RotX = FQuat(FVector3d::XAxisVector, FMath::DegreesToRadians(90.0f));
	const float ImportScale = 8.f;
	const FTransform PMXToUE_X(RotX);
	const FTransform PMXToUE_Scale(FQuat::Identity, FVector3d::ZeroVector, FVector3d(ImportScale));
	const FTransform PMXBoneToUE = PMXToUE_X * PMXToUE_Scale;
	for (int32 i = 0; i < PmxModel.Bones.Num(); ++i)
	{
		const FPmxBone& B = PmxModel.Bones[i];
		const FVector P(static_cast<double>(B.Position.X), static_cast<double>(B.Position.Y), static_cast<double>(B.Position.Z));
		BonePosUE[i] = PMXBoneToUE.TransformPosition(P);
	}

	// We will use the created Joint Root as the skeleton root
	const FString JointRootUid = RootJointUid;

	// Check for duplicate bone names and log warnings
	{
		TMap<FString, TArray<int32>> BoneNameMap;
		for (int32 i = 0; i < PmxModel.Bones.Num(); ++i)
		{
			BoneNameMap.FindOrAdd(PmxModel.Bones[i].Name).Add(i);
		}

		int32 DuplicateCount = 0;
		for (const auto& Pair : BoneNameMap)
		{
			if (Pair.Value.Num() > 1)
			{
				FString Indices;
				for (int32 Idx : Pair.Value)
				{
					if (!Indices.IsEmpty()) Indices += TEXT(", ");
					Indices += FString::FromInt(Idx);
				}
				UE_LOG(LogPMXImporter, Warning, TEXT("PMX NodeBuilder: Duplicate bone name '%s' at indices [%s]"),
					*Pair.Key, *Indices);
				++DuplicateCount;
			}
		}

		if (DuplicateCount > 0)
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("PMX NodeBuilder: Found %d duplicate bone names"), DuplicateCount);
		}
	}

	// Build unique bone names array (resolves duplicate names)
	// This must match the names used in PmxTranslator's JointNames array
	TArray<FString> UniqueBoneNames = FPmxUtils::BuildUniqueBoneNames(PmxModel);

	// Two-pass bone creation to handle forward references in ParentBoneIndex
	// Pass 1: Create all joint nodes first
	for (int32 BoneIndex = 0; BoneIndex < PmxModel.Bones.Num(); ++BoneIndex)
	{
		const FPmxBone& Bone = PmxModel.Bones[BoneIndex];

		UInterchangeSceneNode* JointNode = NewObject<UInterchangeSceneNode>(&BaseNodeContainer);
		const FString JointUid = FString::Printf(TEXT("%s/Bone_%d"), *JointsPrefix, BoneIndex);
		const FString& BoneName = UniqueBoneNames[BoneIndex];  // Use pre-computed unique name

		JointNode->InitializeNode(JointUid, *BoneName, ContainerType::TranslatedScene);
		// Mark as Joint specialized type
		JointNode->AddSpecializedType(UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString());

		// Set local transform (bone position relative to parent, in UE space)
		FVector LocalPos = BonePosUE[BoneIndex];
		if (Bone.ParentBoneIndex >= 0 && Bone.ParentBoneIndex < PmxModel.Bones.Num())
		{
			LocalPos -= BonePosUE[Bone.ParentBoneIndex];
		}
		// Safety clamp for abnormal bone offsets to avoid bones flying far away due to bad data/flags
		bool bInvalid = (!FMath::IsFinite(LocalPos.X) || !FMath::IsFinite(LocalPos.Y) || !FMath::IsFinite(LocalPos.Z));
		const double AbsMax = 100000.0; // 100m in UE units
		if (!bInvalid)
		{
			const FVector Abs = LocalPos.GetAbs();
			bInvalid = (Abs.X > AbsMax) || (Abs.Y > AbsMax) || (Abs.Z > AbsMax);
		}
		if (bInvalid)
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("PMX Translator: Abnormal local bone offset clamped for bone '%s' (Index %d)."), *BoneName, BoneIndex);
			LocalPos = FVector::ZeroVector;
		}
		FTransform LocalTransform = FTransform::Identity;
		LocalTransform.SetLocation(LocalPos);
		JointNode->SetCustomLocalTransform(&BaseNodeContainer, LocalTransform);

		BaseNodeContainer.AddNode(JointNode);
		OutBoneIndexToJointNode.Add(BoneIndex, JointNode);
	}

	// Pass 2: Set parent relationships now that all nodes exist
	for (int32 BoneIndex = 0; BoneIndex < PmxModel.Bones.Num(); ++BoneIndex)
	{
		const FPmxBone& Bone = PmxModel.Bones[BoneIndex];
		UInterchangeSceneNode* JointNode = OutBoneIndexToJointNode[BoneIndex];

		// Set parent relationship
		if (Bone.ParentBoneIndex >= 0 && Bone.ParentBoneIndex < PmxModel.Bones.Num())
		{
			if (UInterchangeSceneNode** ParentJoint = OutBoneIndexToJointNode.Find(Bone.ParentBoneIndex))
			{
				BaseNodeContainer.SetNodeParentUid(JointNode->GetUniqueID(), (*ParentJoint)->GetUniqueID());
			}
			else
			{
				// Fallback: Parent to Joint Root if parent not found
				UE_LOG(LogPMXImporter, Warning, TEXT("PMX NodeBuilder: Bone '%s' (Index %d) has invalid parent index %d, parenting to root"),
					*Bone.Name, BoneIndex, Bone.ParentBoneIndex);
				BaseNodeContainer.SetNodeParentUid(JointNode->GetUniqueID(), JointRootUid);
			}
		}
		else
		{
			// Parent to Joint Root if no valid parent (ensure single-root hierarchy)
			BaseNodeContainer.SetNodeParentUid(JointNode->GetUniqueID(), JointRootUid);
		}
	}

	// Always return the Joint Root as skeleton root
	return JointRootUid;
}