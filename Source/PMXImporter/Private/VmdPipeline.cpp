// Copyright Epic Games, Inc. All Rights Reserved.

#include "VmdPipeline.h"
#include "VmdTranslator.h"
#include "InterchangeAnimSequenceFactoryNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimCurveTypes.h"
#include "Curves/RichCurve.h"
#include "LogPMXImporter.h"

UVmdPipeline::UVmdPipeline()
{
	// Set default values
	TargetSkeleton = nullptr;
	bImportBoneAnimation = true;
	bImportMorphAnimation = true;
	bImportCameraAnimation = true;
	Scale = 8.0f;
	SampleRate = 30.0;
}

void UVmdPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
	if (!BaseNodeContainer)
	{
		UE_LOG(LogPMXImporter, Error, TEXT("VmdPipeline: No BaseNodeContainer"));
		return;
	}

	// Cache source file path for later use in PostImportPipeline
	if (SourceDatas.Num() > 0 && SourceDatas[0])
	{
		CachedSourceFilePath = SourceDatas[0]->GetFilename();
	}

	// Check if skeleton is assigned
	if (!TargetSkeleton)
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("VmdPipeline: No target skeleton assigned. AnimSequence import will fail."));
		UE_LOG(LogPMXImporter, Warning, TEXT("VmdPipeline: Please assign a skeleton in the import dialog."));
	}
	else
	{
		UE_LOG(LogPMXImporter, Log, TEXT("VmdPipeline: Using skeleton '%s'"), *TargetSkeleton->GetName());
	}

	// Configure AnimSequence nodes
	ConfigureAnimSequenceNode(BaseNodeContainer);
}

void UVmdPipeline::ConfigureAnimSequenceNode(UInterchangeBaseNodeContainer* BaseNodeContainer)
{
	if (!TargetSkeleton)
	{
		UE_LOG(LogPMXImporter, Error, TEXT("VmdPipeline: Cannot configure AnimSequence - no skeleton assigned"));
		return;
	}

	// Create a SkeletonFactoryNode that references the existing skeleton
	// This is required by the AnimSequence factory
	FString SkeletonNodeUid = TEXT("\\Skeleton\\VMD_TargetSkeleton");

	// Check if skeleton factory node already exists
	UInterchangeSkeletonFactoryNode* SkeletonFactoryNode = Cast<UInterchangeSkeletonFactoryNode>(
		BaseNodeContainer->GetFactoryNode(SkeletonNodeUid));

	if (!SkeletonFactoryNode)
	{
		// Create new skeleton factory node
		SkeletonFactoryNode = NewObject<UInterchangeSkeletonFactoryNode>(BaseNodeContainer);

		// Must use InitializeSkeletonNode (not InitializeNode) to properly initialize bIsNodeClassInitialized
		SkeletonFactoryNode->InitializeSkeletonNode(
			SkeletonNodeUid,
			TargetSkeleton->GetName(),
			USkeleton::StaticClass()->GetName(),
			BaseNodeContainer
		);

		// Set reference to existing skeleton
		FSoftObjectPath SkeletonPath(TargetSkeleton);
		SkeletonFactoryNode->SetCustomReferenceObject(SkeletonPath);

		// IMPORTANT: Disable this factory node so it doesn't try to create a new skeleton
		// AnimSequenceFactory will still use the ReferenceObject to get the existing skeleton
		SkeletonFactoryNode->SetEnabled(false);

		UE_LOG(LogPMXImporter, Log, TEXT("VmdPipeline: Created SkeletonFactoryNode '%s' referencing '%s' (disabled)"),
			*SkeletonNodeUid, *SkeletonPath.ToString());
	}

	// Find all AnimSequence factory nodes
	TArray<FString> AnimNodeUids;
	BaseNodeContainer->GetNodes(UInterchangeAnimSequenceFactoryNode::StaticClass(), AnimNodeUids);

	for (const FString& NodeUid : AnimNodeUids)
	{
		UInterchangeAnimSequenceFactoryNode* AnimNode = Cast<UInterchangeAnimSequenceFactoryNode>(
			BaseNodeContainer->GetFactoryNode(NodeUid));

		if (!AnimNode)
		{
			continue;
		}

		UE_LOG(LogPMXImporter, Log, TEXT("VmdPipeline: Configuring AnimSequence node '%s'"), *NodeUid);

		// Set skeleton factory node UID (required by AnimSequence factory)
		AnimNode->SetCustomSkeletonFactoryNodeUid(SkeletonNodeUid);

		// Also set the soft object path
		FSoftObjectPath SkeletonPath(TargetSkeleton);
		AnimNode->SetCustomSkeletonSoftObjectPath(SkeletonPath);

		// Add dependency on skeleton factory node
		AnimNode->AddFactoryDependencyUid(SkeletonNodeUid);

		UE_LOG(LogPMXImporter, Log, TEXT("VmdPipeline: Set skeleton UID '%s' and path '%s'"),
			*SkeletonNodeUid, *SkeletonPath.ToString());

		// Configure bone track import
		AnimNode->SetCustomImportBoneTracks(bImportBoneAnimation);
		AnimNode->SetCustomImportBoneTracksSampleRate(SampleRate);

		// Configure curve import
		AnimNode->SetCustomImportAttributeCurves(bImportMorphAnimation);
		AnimNode->SetCustomDoNotImportCurveWithZero(false);
		AnimNode->SetCustomDeleteExistingMorphTargetCurves(true);
		AnimNode->SetCustomAddCurveMetadataToSkeleton(true);
	}
}

void UVmdPipeline::ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport)
{
	if (!CreatedAsset)
	{
		return;
	}

	UE_LOG(LogPMXImporter, Log, TEXT("VmdPipeline: Post-import for '%s' (Type: %s)"),
		*CreatedAsset->GetName(), *CreatedAsset->GetClass()->GetName());

	// Check if this is an AnimSequence
	UAnimSequence* AnimSequence = Cast<UAnimSequence>(CreatedAsset);
	if (!AnimSequence)
	{
		return;
	}

	// Get VMD data from translator cache
	if (CachedSourceFilePath.IsEmpty())
	{
		UE_LOG(LogPMXImporter, Error, TEXT("VmdPipeline: No cached source file path"));
		return;
	}

	TSharedPtr<FVmdModel>* CachedModel = UVmdTranslator::VmdPayloadCache.Find(CachedSourceFilePath);
	if (!CachedModel || !CachedModel->IsValid())
	{
		UE_LOG(LogPMXImporter, Error, TEXT("VmdPipeline: VMD model not found in cache for '%s'"), *CachedSourceFilePath);
		return;
	}

	const FVmdModel& VmdModel = **CachedModel;

	UE_LOG(LogPMXImporter, Log, TEXT("VmdPipeline: Populating AnimSequence '%s' with VMD data (Bones: %d, Morphs: %d)"),
		*AnimSequence->GetName(),
		VmdModel.GetUniqueBoneNames().Num(),
		VmdModel.GetUniqueMorphNames().Num());

	// Populate the AnimSequence with actual animation data
	PopulateAnimSequenceData(AnimSequence, VmdModel);
}

void UVmdPipeline::PopulateAnimSequenceData(UAnimSequence* AnimSequence, const FVmdModel& VmdModel)
{
	if (!AnimSequence || !TargetSkeleton)
	{
		return;
	}

	IAnimationDataController& Controller = AnimSequence->GetController();

	// Begin editing
	Controller.OpenBracket(NSLOCTEXT("VmdPipeline", "ImportVMD", "Importing VMD Animation"), false);

	// Calculate animation length
	const double FrameRate = 30.0; // VMD is 30 FPS
	double RawDuration = VmdModel.GetDurationSeconds();
	int32 TotalFrames = FMath::CeilToInt(RawDuration * FrameRate);
	double AlignedDuration = TotalFrames / FrameRate;

	// Set frame rate and number of frames
	FFrameRate UEFrameRate(static_cast<uint32>(SampleRate), 1);
	Controller.SetFrameRate(UEFrameRate, false);

	int32 NumFrames = FMath::CeilToInt(AlignedDuration * SampleRate);
	Controller.SetNumberOfFrames(NumFrames, false);

	UE_LOG(LogPMXImporter, Log, TEXT("VmdPipeline: Animation duration=%.2fs, frames=%d, sampleRate=%.1f"),
		AlignedDuration, NumFrames, SampleRate);

	// Add bone animation tracks
	if (bImportBoneAnimation)
	{
		AddBoneAnimationTracks(AnimSequence, VmdModel);
	}

	// Add morph target curves
	if (bImportMorphAnimation)
	{
		AddMorphTargetCurves(AnimSequence, VmdModel);
	}

	// End editing
	Controller.CloseBracket(false);

	// Notify asset modified
	AnimSequence->PostEditChange();
	AnimSequence->MarkPackageDirty();

	UE_LOG(LogPMXImporter, Log, TEXT("VmdPipeline: AnimSequence population complete"));
}

void UVmdPipeline::AddBoneAnimationTracks(UAnimSequence* AnimSequence, const FVmdModel& VmdModel)
{
	if (!TargetSkeleton)
	{
		return;
	}

	IAnimationDataController& Controller = AnimSequence->GetController();
	const FReferenceSkeleton& RefSkeleton = TargetSkeleton->GetReferenceSkeleton();
	const TArray<FTransform>& RefBonePose = RefSkeleton.GetRefBonePose();

	// Get animation duration info
	const double VmdFrameRate = 30.0; // VMD is always 30 FPS
	double AnimDuration = VmdModel.GetDurationSeconds();
	int32 TotalAnimFrames = FMath::CeilToInt(AnimDuration * SampleRate);

	int32 MatchedBones = 0;
	int32 UnmatchedBones = 0;
	int32 TotalKeyframes = 0;

	// Collect unmatched bone names for logging
	TArray<FString> UnmatchedBoneNames;

	// Iterate through unique bone names in VMD
	for (const FString& VmdBoneName : VmdModel.GetUniqueBoneNames())
	{
		// Find matching bone in skeleton
		int32 BoneIndex = RefSkeleton.FindBoneIndex(FName(*VmdBoneName));
		if (BoneIndex == INDEX_NONE)
		{
			UnmatchedBones++;
			UnmatchedBoneNames.Add(VmdBoneName);
			continue;
		}

		FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		MatchedBones++;

		// Get keyframes for this bone
		TArray<FVmdBoneKeyframe> Keyframes = VmdModel.GetBoneKeyframes(VmdBoneName);
		if (Keyframes.Num() == 0)
		{
			continue;
		}

		TotalKeyframes += Keyframes.Num();

		// Get reference pose for this bone (local space)
		const FTransform& RefPose = RefBonePose[BoneIndex];

		// Build sampled transform arrays at the target sample rate
		// We need to sample VMD keyframes to match UE's expected uniform time distribution
		TArray<FVector3f> PositionalKeys;
		TArray<FQuat4f> RotationalKeys;
		TArray<FVector3f> ScalingKeys;

		// Sample at each frame of the animation
		PositionalKeys.SetNum(TotalAnimFrames + 1);
		RotationalKeys.SetNum(TotalAnimFrames + 1);
		ScalingKeys.SetNum(TotalAnimFrames + 1);

		for (int32 FrameIdx = 0; FrameIdx <= TotalAnimFrames; ++FrameIdx)
		{
			double CurrentTime = FrameIdx / SampleRate;
			double VmdFrame = CurrentTime * VmdFrameRate;

			// Find the two keyframes to interpolate between
			int32 KeyBefore = 0;
			int32 KeyAfter = 0;

			for (int32 i = 0; i < Keyframes.Num(); ++i)
			{
				if (Keyframes[i].FrameNumber <= VmdFrame)
				{
					KeyBefore = i;
				}
				if (Keyframes[i].FrameNumber >= VmdFrame)
				{
					KeyAfter = i;
					break;
				}
				KeyAfter = i; // In case we're past all keyframes
			}

			// Interpolate between keyframes
			FVector DeltaPosition;
			FQuat DeltaRotation;

			if (KeyBefore == KeyAfter || Keyframes[KeyBefore].FrameNumber == Keyframes[KeyAfter].FrameNumber)
			{
				// No interpolation needed
				DeltaPosition = ConvertPositionDeltaVmdToUE(Keyframes[KeyBefore].Position);
				DeltaRotation = ConvertRotationVmdToUE(Keyframes[KeyBefore].Rotation);
			}
			else
			{
				// Linear interpolation (TODO: use bezier interpolation from VMD data)
				float T = (VmdFrame - Keyframes[KeyBefore].FrameNumber) /
				          (Keyframes[KeyAfter].FrameNumber - Keyframes[KeyBefore].FrameNumber);
				T = FMath::Clamp(T, 0.0f, 1.0f);

				FVector PosBefore = ConvertPositionDeltaVmdToUE(Keyframes[KeyBefore].Position);
				FVector PosAfter = ConvertPositionDeltaVmdToUE(Keyframes[KeyAfter].Position);
				DeltaPosition = FMath::Lerp(PosBefore, PosAfter, T);

				FQuat RotBefore = ConvertRotationVmdToUE(Keyframes[KeyBefore].Rotation);
				FQuat RotAfter = ConvertRotationVmdToUE(Keyframes[KeyAfter].Rotation);
				DeltaRotation = FQuat::Slerp(RotBefore, RotAfter, T);
			}

			// Apply delta to reference pose
			// VMD rotation is applied in local space ON TOP of reference pose
			// Order: RefPose * Delta (delta applied after ref pose in local space)
			FVector FinalPosition = RefPose.GetLocation() + DeltaPosition;
			FQuat FinalRotation = RefPose.GetRotation() * DeltaRotation;

			PositionalKeys[FrameIdx] = FVector3f(FinalPosition);
			RotationalKeys[FrameIdx] = FQuat4f(FinalRotation);
			ScalingKeys[FrameIdx] = FVector3f(RefPose.GetScale3D());
		}

		// Remove existing track if present (to avoid duplicates from Interchange)
		// Use GetModel to check if track exists
		TArray<FName> ExistingTrackNames;
		Controller.GetModel()->GetBoneTrackNames(ExistingTrackNames);
		if (ExistingTrackNames.Contains(BoneName))
		{
			Controller.RemoveBoneTrack(BoneName, false);
		}

		// Add bone track
		Controller.AddBoneCurve(BoneName, false);

		// Set keys
		if (PositionalKeys.Num() > 0)
		{
			Controller.SetBoneTrackKeys(BoneName, PositionalKeys, RotationalKeys, ScalingKeys, false);
		}
	}

	// Log unmatched bones
	if (UnmatchedBoneNames.Num() > 0)
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("VmdPipeline: %d VMD bones not found in skeleton:"), UnmatchedBoneNames.Num());
		for (const FString& Name : UnmatchedBoneNames)
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("  - %s"), *Name);
		}
	}

	UE_LOG(LogPMXImporter, Log, TEXT("VmdPipeline: Added %d bone tracks (sampled to %d frames), %d VMD bones unmatched"),
		MatchedBones, TotalAnimFrames, UnmatchedBones);
}

void UVmdPipeline::AddMorphTargetCurves(UAnimSequence* AnimSequence, const FVmdModel& VmdModel)
{
	IAnimationDataController& Controller = AnimSequence->GetController();

	int32 AddedCurves = 0;

	for (const FString& MorphName : VmdModel.GetUniqueMorphNames())
	{
		TArray<FVmdMorphKeyframe> Keyframes = VmdModel.GetMorphKeyframes(MorphName);
		if (Keyframes.Num() == 0)
		{
			continue;
		}

		// Create curve identifier for morph target
		FName CurveName(*MorphName);
		FAnimationCurveIdentifier CurveId(CurveName, ERawCurveTrackTypes::RCT_Float);

		// Remove existing curve if present (to avoid duplicates from Interchange)
		if (Controller.GetModel()->FindCurve(CurveId))
		{
			Controller.RemoveCurve(CurveId, false);
		}

		// Add the curve
		if (Controller.AddCurve(CurveId, AACF_DefaultCurve, false))
		{
			// Build rich curve data
			FRichCurve RichCurve;
			for (const FVmdMorphKeyframe& Key : Keyframes)
			{
				float Time = Key.FrameNumber / 30.0f; // VMD is 30 FPS
				FKeyHandle KeyHandle = RichCurve.AddKey(Time, Key.Weight);
				RichCurve.SetKeyInterpMode(KeyHandle, RCIM_Linear);
			}

			// Set curve keys
			Controller.SetCurveKeys(CurveId, RichCurve.GetConstRefOfKeys(), false);
			AddedCurves++;
		}
	}

	UE_LOG(LogPMXImporter, Log, TEXT("VmdPipeline: Added %d morph target curves"), AddedCurves);
}

FVector UVmdPipeline::ConvertPositionVmdToUE(const FVector& VmdPosition) const
{
	// VMD/PMX: Right-handed Y-up (X right, Y up, Z forward)
	// UE: Left-handed Z-up (X forward, Y right, Z up)
	// Apply X-axis 90 degree rotation (same as PmxNodeBuilder)
	// After rotation: X stays X, Y becomes -Z, Z becomes Y
	return FVector(VmdPosition.X, -VmdPosition.Z, VmdPosition.Y) * Scale;
}

FVector UVmdPipeline::ConvertPositionDeltaVmdToUE(const FVector& VmdDelta) const
{
	// For bone animation deltas, apply same coordinate conversion with scale
	// VMD position deltas are in MMD units (1 unit = 8cm typically)
	return FVector(VmdDelta.X, -VmdDelta.Z, VmdDelta.Y) * Scale;
}

FQuat UVmdPipeline::ConvertRotationVmdToUE(const FQuat& VmdRotation) const
{
	// Convert quaternion from VMD/PMX to UE coordinate system
	// Same axis swap as PmxTranslator::ConvertQuaternionPmxToUE
	// (X, Y, Z, W) -> (X, Z, Y, W)
	return FQuat(VmdRotation.X, VmdRotation.Z, VmdRotation.Y, VmdRotation.W);
}
