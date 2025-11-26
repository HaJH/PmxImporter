// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved.

#include "VmdPipeline.h"
#include "VmdTranslator.h"
#include "VmdIKSolver.h"
#include "VmdIKStateManager.h"
#include "PmxIKMetadata.h"
#include "InterchangeAnimSequenceFactoryNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimCurveTypes.h"
#include "Curves/RichCurve.h"
#include "Misc/ScopedSlowTask.h"
#include "Async/ParallelFor.h"
#include <atomic>
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
		UE_LOG(LogPMXImporter, Warning, TEXT("VmdPipeline: No target skeleton assigned. Please assign a skeleton in the import dialog."));
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

		// Set skeleton factory node UID (required by AnimSequence factory)
		AnimNode->SetCustomSkeletonFactoryNodeUid(SkeletonNodeUid);

		// Also set the soft object path
		FSoftObjectPath SkeletonPath(TargetSkeleton);
		AnimNode->SetCustomSkeletonSoftObjectPath(SkeletonPath);

		// Add dependency on skeleton factory node
		AnimNode->AddFactoryDependencyUid(SkeletonNodeUid);

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

	// Populate the AnimSequence with actual animation data
	PopulateAnimSequenceData(AnimSequence, VmdModel);
}

void UVmdPipeline::PopulateAnimSequenceData(UAnimSequence* AnimSequence, const FVmdModel& VmdModel)
{
	if (!AnimSequence || !TargetSkeleton)
	{
		return;
	}

	// Calculate total work for progress bar
	int32 TotalSteps = 0;
	if (bImportBoneAnimation) TotalSteps += 1;  // Bone tracks (with or without IK baking)
	if (bImportMorphAnimation) TotalSteps += 1;

	FScopedSlowTask SlowTask(TotalSteps, NSLOCTEXT("VmdPipeline", "ImportingVMD", "Importing VMD Animation..."));
	SlowTask.MakeDialog(true);

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

	// Add bone animation tracks (with optional IK baking integrated)
	if (bImportBoneAnimation)
	{
		if (bBakeIKToFK)
		{
			SlowTask.EnterProgressFrame(1, NSLOCTEXT("VmdPipeline", "AddingBoneTracksWithIK", "Adding bone tracks with IK baking..."));
			AddBoneAnimationTracksWithIK(AnimSequence, VmdModel);
		}
		else
		{
			SlowTask.EnterProgressFrame(1, NSLOCTEXT("VmdPipeline", "AddingBoneTracks", "Adding bone animation tracks..."));
			AddBoneAnimationTracks(AnimSequence, VmdModel);
		}
	}

	// Add morph target curves
	if (bImportMorphAnimation)
	{
		SlowTask.EnterProgressFrame(1, NSLOCTEXT("VmdPipeline", "AddingMorphCurves", "Adding morph target curves..."));
		AddMorphTargetCurves(AnimSequence, VmdModel);
	}

	// End editing
	Controller.CloseBracket(false);

	// Notify asset modified
	AnimSequence->PostEditChange();
	AnimSequence->MarkPackageDirty();
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

	// Get unique bone names for progress tracking
	TSet<FString> UniqueBoneNames = VmdModel.GetUniqueBoneNames();
	FScopedSlowTask BoneTrackTask(UniqueBoneNames.Num(), NSLOCTEXT("VmdPipeline", "ProcessingBones", "Processing bone tracks..."));
	BoneTrackTask.MakeDialog(false); // Don't show dialog, parent task shows it

	int32 ProcessedBones = 0;
	// Iterate through unique bone names in VMD
	for (const FString& VmdBoneName : UniqueBoneNames)
	{
		// Update progress
		BoneTrackTask.EnterProgressFrame(1);
		++ProcessedBones;

		// Find matching bone in skeleton
		int32 BoneIndex = RefSkeleton.FindBoneIndex(FName(*VmdBoneName));
		if (BoneIndex == INDEX_NONE)
		{
			continue;
		}

		FName BoneName = RefSkeleton.GetBoneName(BoneIndex);

		// Get keyframes for this bone
		TArray<FVmdBoneKeyframe> Keyframes = VmdModel.GetBoneKeyframes(VmdBoneName);
		if (Keyframes.Num() == 0)
		{
			continue;
		}

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
				DeltaRotation.Normalize();
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
				RotBefore.Normalize();
				FQuat RotAfter = ConvertRotationVmdToUE(Keyframes[KeyAfter].Rotation);
				RotAfter.Normalize();

				// Ensure shortest path SLERP (handle quaternion dual cover)
				if ((RotBefore | RotAfter) < 0.0f)
				{
					RotAfter = -RotAfter;
				}

				DeltaRotation = FQuat::Slerp(RotBefore, RotAfter, T);
				DeltaRotation.Normalize();
			}

			// Apply delta to reference pose
			// VMD rotation is applied in local space ON TOP of reference pose
			// Order: RefPose * Delta (delta applied after ref pose in local space)
			FVector FinalPosition = RefPose.GetLocation() + DeltaPosition;
			FQuat FinalRotation = RefPose.GetRotation() * DeltaRotation;
			FinalRotation.Normalize();

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

}

void UVmdPipeline::AddBoneAnimationTracksWithIK(UAnimSequence* AnimSequence, const FVmdModel& VmdModel)
{
	if (!TargetSkeleton)
	{
		return;
	}

	// Get IK metadata from skeleton
	UPmxIKMetadataUserData* IKMetadata = GetIKMetadataFromSkeleton();
	if (!IKMetadata || IKMetadata->IKChains.Num() == 0)
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("VmdPipeline: No IK metadata found, falling back to standard bone import"));
		AddBoneAnimationTracks(AnimSequence, VmdModel);
		return;
	}

	IAnimationDataController& Controller = AnimSequence->GetController();
	const FReferenceSkeleton& RefSkeleton = TargetSkeleton->GetReferenceSkeleton();
	const TArray<FTransform>& RefBonePose = RefSkeleton.GetRefBonePose();

	UE_LOG(LogPMXImporter, Display, TEXT("VmdPipeline: Starting IK-integrated bone import with %d chains"), IKMetadata->IKChains.Num());

	// Log IK chain details for debugging
	// Note: Chain.TargetBoneName stores the actual bone name, Chain.TargetBoneIndex is PMX index (not UE index)
	for (const FPmxIKChainMetadata& Chain : IKMetadata->IKChains)
	{
		// Get UE skeleton index from bone name
		int32 UETargetIndex = RefSkeleton.FindBoneIndex(Chain.TargetBoneName);
		UE_LOG(LogPMXImporter, Display, TEXT("  IK Chain: %s -> Target: %s (UE idx %d), Links: %d, Loops: %d"),
			*Chain.IKBoneName.ToString(), *Chain.TargetBoneName.ToString(), UETargetIndex, Chain.Links.Num(), Chain.LoopCount);
		for (const FPmxIKLinkMetadata& Link : Chain.Links)
		{
			int32 UELinkIndex = RefSkeleton.FindBoneIndex(Link.BoneName);
			UE_LOG(LogPMXImporter, Display, TEXT("    Link: %s (UE idx %d), HasLimits: %d, Min: (%.2f, %.2f, %.2f), Max: (%.2f, %.2f, %.2f)"),
				*Link.BoneName.ToString(), UELinkIndex, Link.bHasAngleLimits ? 1 : 0,
				Link.AngleLimitMin.X, Link.AngleLimitMin.Y, Link.AngleLimitMin.Z,
				Link.AngleLimitMax.X, Link.AngleLimitMax.Y, Link.AngleLimitMax.Z);
		}
	}

	// Build bone transforms and parent indices
	TArray<FTransform> BaseBoneTransforms;
	TArray<int32> ParentIndices;
	BuildBoneTransformsFromSkeleton(BaseBoneTransforms, ParentIndices);

	// Initialize IK solver
	FVmdIKSolver Solver;
	Solver.Initialize(IKMetadata, BaseBoneTransforms, ParentIndices);

	// Initialize IK state manager
	FVmdIKStateManager StateManager;
	StateManager.Initialize(VmdModel.PropertyKeyframes);

	// Collect all bones that need animation tracks
	// This includes: VMD animated bones + IK chain link bones
	TSet<FName> BonesToAnimate;

	// Add all VMD animated bones
	for (const FString& VmdBoneName : VmdModel.GetUniqueBoneNames())
	{
		int32 BoneIndex = RefSkeleton.FindBoneIndex(FName(*VmdBoneName));
		if (BoneIndex != INDEX_NONE)
		{
			BonesToAnimate.Add(RefSkeleton.GetBoneName(BoneIndex));
		}
	}

	// Add all IK chain link bones (these need to be baked)
	for (const FPmxIKChainMetadata& Chain : IKMetadata->IKChains)
	{
		for (const FPmxIKLinkMetadata& Link : Chain.Links)
		{
			int32 BoneIndex = RefSkeleton.FindBoneIndex(Link.BoneName);
			if (BoneIndex != INDEX_NONE)
			{
				BonesToAnimate.Add(Link.BoneName);
			}
		}
	}

	// Get animation duration info
	const double VmdFrameRate = 30.0;
	double AnimDuration = VmdModel.GetDurationSeconds();
	int32 TotalAnimFrames = FMath::CeilToInt(AnimDuration * SampleRate);

	// Create progress dialog
	FScopedSlowTask SlowTask(2, NSLOCTEXT("VmdPipeline", "BakingIKBones", "Baking IK to FK..."));
	SlowTask.MakeDialog(true);

	// Prepare storage for all bone keys (indexed by bone, then frame)
	TMap<FName, TArray<FVector3f>> BonePositionalKeys;
	TMap<FName, TArray<FQuat4f>> BoneRotationalKeys;
	TMap<FName, TArray<FVector3f>> BoneScalingKeys;

	for (const FName& BoneName : BonesToAnimate)
	{
		BonePositionalKeys.Add(BoneName).SetNum(TotalAnimFrames + 1);
		BoneRotationalKeys.Add(BoneName).SetNum(TotalAnimFrames + 1);
		BoneScalingKeys.Add(BoneName).SetNum(TotalAnimFrames + 1);
	}

	// Convert BonesToAnimate to array for indexed access
	TArray<FName> BoneNameArray = BonesToAnimate.Array();

	// Pre-build bone index lookup
	TMap<FName, int32> BoneNameToIndex;
	for (const FName& BoneName : BoneNameArray)
	{
		BoneNameToIndex.Add(BoneName, RefSkeleton.FindBoneIndex(BoneName));
	}

	SlowTask.EnterProgressFrame(1, NSLOCTEXT("VmdPipeline", "ProcessingFrames", "Processing animation frames (multithreaded)..."));

	// Use atomic counter for progress logging
	std::atomic<int32> ProcessedFrames(0);

	// Process frames in parallel
	ParallelFor(TotalAnimFrames + 1, [&](int32 FrameIdx)
	{
		double CurrentTime = FrameIdx / SampleRate;
		float VmdFrame = static_cast<float>(CurrentTime * VmdFrameRate);

		// Get VMD bone transforms at this frame
		TMap<FName, FTransform> VmdBoneDeltas;
		InterpolateVmdBoneTransforms(VmdModel, VmdFrame, VmdBoneDeltas);

		// Start with reference pose (copy for this thread)
		TArray<FTransform> CurrentTransforms = BaseBoneTransforms;

		// Apply VMD deltas to all animated bones
		for (const auto& Pair : VmdBoneDeltas)
		{
			const int32* BoneIndexPtr = BoneNameToIndex.Find(Pair.Key);
			int32 BoneIndex = BoneIndexPtr ? *BoneIndexPtr : RefSkeleton.FindBoneIndex(Pair.Key);
			if (BoneIndex != INDEX_NONE && CurrentTransforms.IsValidIndex(BoneIndex))
			{
				FTransform& LocalTransform = CurrentTransforms[BoneIndex];
				const FTransform& Delta = Pair.Value;

				// Apply delta: position is additive, rotation is multiplicative
				FQuat NewRotation = LocalTransform.GetRotation() * Delta.GetRotation();
				NewRotation.Normalize();
				LocalTransform.SetRotation(NewRotation);
				LocalTransform.AddToTranslation(Delta.GetTranslation());
			}
		}

		// Get IK states at this frame
		uint32 VmdFrameInt = static_cast<uint32>(FMath::RoundToInt(VmdFrame));
		TMap<FName, bool> IKStates = StateManager.GetIKStatesAtFrame(VmdFrameInt);

		// Calculate IK bone world positions for the solver
		TMap<FName, FTransform> IKBoneWorldTransforms;
		for (const FPmxIKChainMetadata& Chain : IKMetadata->IKChains)
		{
			int32 IKBoneIndex = RefSkeleton.FindBoneIndex(Chain.IKBoneName);
			if (IKBoneIndex != INDEX_NONE && CurrentTransforms.IsValidIndex(IKBoneIndex))
			{
				// Calculate world transform for IK bone
				FTransform WorldTransform = CurrentTransforms[IKBoneIndex];
				int32 ParentIdx = RefSkeleton.GetParentIndex(IKBoneIndex);
				while (ParentIdx != INDEX_NONE)
				{
					WorldTransform = WorldTransform * CurrentTransforms[ParentIdx];
					ParentIdx = RefSkeleton.GetParentIndex(ParentIdx);
				}
				IKBoneWorldTransforms.Add(Chain.IKBoneName, WorldTransform);
			}
		}

		// Create thread-local IK solver (IK solver state is modified during solving)
		FVmdIKSolver LocalSolver;
		LocalSolver.Initialize(IKMetadata, BaseBoneTransforms, ParentIndices);

		// Solve IK chains - this modifies CurrentTransforms in place
		if (LocalSolver.IsInitialized())
		{
			LocalSolver.SolveFrame(IKBoneWorldTransforms, IKStates, CurrentTransforms);
		}

		// Store results for all bones we're animating
		for (const FName& BoneName : BoneNameArray)
		{
			const int32* BoneIndexPtr = BoneNameToIndex.Find(BoneName);
			if (BoneIndexPtr && *BoneIndexPtr != INDEX_NONE && CurrentTransforms.IsValidIndex(*BoneIndexPtr))
			{
				const FTransform& FinalTransform = CurrentTransforms[*BoneIndexPtr];

				BonePositionalKeys[BoneName][FrameIdx] = FVector3f(FinalTransform.GetLocation());
				BoneRotationalKeys[BoneName][FrameIdx] = FQuat4f(FinalTransform.GetRotation());
				BoneScalingKeys[BoneName][FrameIdx] = FVector3f(FinalTransform.GetScale3D());
			}
		}

		// Log progress occasionally
		int32 Processed = ++ProcessedFrames;
		if (Processed % 500 == 0)
		{
			UE_LOG(LogPMXImporter, Display, TEXT("VmdPipeline: Processed %d / %d frames"), Processed, TotalAnimFrames + 1);
		}
	});

	SlowTask.EnterProgressFrame(1, NSLOCTEXT("VmdPipeline", "AddingTracks", "Adding bone tracks to animation..."));

	// Now add all bone tracks to the animation
	for (const FName& BoneName : BonesToAnimate)
	{
		// Remove existing track if present
		TArray<FName> ExistingTrackNames;
		Controller.GetModel()->GetBoneTrackNames(ExistingTrackNames);
		if (ExistingTrackNames.Contains(BoneName))
		{
			Controller.RemoveBoneTrack(BoneName, false);
		}

		// Add bone track
		Controller.AddBoneCurve(BoneName, false);

		// Set keys
		const TArray<FVector3f>& PosKeys = BonePositionalKeys[BoneName];
		const TArray<FQuat4f>& RotKeys = BoneRotationalKeys[BoneName];
		const TArray<FVector3f>& ScaleKeys = BoneScalingKeys[BoneName];

		if (PosKeys.Num() > 0)
		{
			Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys, false);
		}
	}

	UE_LOG(LogPMXImporter, Display, TEXT("VmdPipeline: Completed IK-integrated bone import (%d bones, %d frames)"),
		BonesToAnimate.Num(), TotalAnimFrames + 1);
}

void UVmdPipeline::AddMorphTargetCurves(UAnimSequence* AnimSequence, const FVmdModel& VmdModel)
{
	IAnimationDataController& Controller = AnimSequence->GetController();

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
		}
	}
}

FVector UVmdPipeline::ConvertPositionVmdToUE(const FVector& VmdPosition) const
{
	// VMD/PMX: Right-handed Y-up (X right, Y up, Z forward)
	// UE: Left-handed Z-up (X forward, Y right, Z up)
	// Same as PmxTranslator::ConvertVectorPmxToUE: (X, Z, Y)
	return FVector(VmdPosition.X, VmdPosition.Z, VmdPosition.Y) * Scale;
}

FVector UVmdPipeline::ConvertPositionDeltaVmdToUE(const FVector& VmdDelta) const
{
	// For bone animation deltas, apply same coordinate conversion with scale
	// Same as PmxTranslator::ConvertVectorPmxToUE: (X, Z, Y)
	return FVector(VmdDelta.X, VmdDelta.Z, VmdDelta.Y) * Scale;
}

FQuat UVmdPipeline::ConvertRotationVmdToUE(const FQuat& VmdRotation) const
{
	// Convert quaternion from VMD/PMX to UE coordinate system
	// Same axis swap as PmxTranslator::ConvertQuaternionPmxToUE
	// (X, Y, Z, W) -> (X, Z, Y, W)
	return FQuat(VmdRotation.X, VmdRotation.Z, VmdRotation.Y, VmdRotation.W);
}

UPmxIKMetadataUserData* UVmdPipeline::GetIKMetadataFromSkeleton() const
{
	if (!TargetSkeleton)
	{
		return nullptr;
	}

	// Look for IK metadata in skeleton's asset user data
	return TargetSkeleton->GetAssetUserData<UPmxIKMetadataUserData>();
}

void UVmdPipeline::BuildBoneTransformsFromSkeleton(TArray<FTransform>& OutLocalTransforms, TArray<int32>& OutParentIndices) const
{
	OutLocalTransforms.Empty();
	OutParentIndices.Empty();

	if (!TargetSkeleton)
	{
		return;
	}

	const FReferenceSkeleton& RefSkeleton = TargetSkeleton->GetReferenceSkeleton();
	int32 NumBones = RefSkeleton.GetNum();

	OutLocalTransforms.Reserve(NumBones);
	OutParentIndices.Reserve(NumBones);

	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		OutLocalTransforms.Add(RefSkeleton.GetRefBonePose()[BoneIndex]);
		OutParentIndices.Add(RefSkeleton.GetParentIndex(BoneIndex));
	}
}

void UVmdPipeline::InterpolateVmdBoneTransforms(const FVmdModel& VmdModel, float Frame, TMap<FName, FTransform>& OutBoneTransforms) const
{
	OutBoneTransforms.Empty();

	// Get unique bone names
	TSet<FString> BoneNames = VmdModel.GetUniqueBoneNames();

	for (const FString& BoneName : BoneNames)
	{
		// Get keyframes for this bone (already sorted)
		TArray<FVmdBoneKeyframe> Keyframes = VmdModel.GetBoneKeyframes(BoneName);

		if (Keyframes.Num() == 0)
		{
			continue;
		}

		// Find surrounding keyframes
		int32 KeyBefore = 0;
		int32 KeyAfter = 0;

		for (int32 i = 0; i < Keyframes.Num(); ++i)
		{
			if (Keyframes[i].FrameNumber <= Frame)
			{
				KeyBefore = i;
			}
			if (Keyframes[i].FrameNumber >= Frame)
			{
				KeyAfter = i;
				break;
			}
			KeyAfter = i;
		}

		FVector Position;
		FQuat Rotation;

		if (KeyBefore == KeyAfter || Keyframes[KeyBefore].FrameNumber == Keyframes[KeyAfter].FrameNumber)
		{
			// Exact keyframe or single keyframe
			Position = ConvertPositionDeltaVmdToUE(Keyframes[KeyBefore].Position);
			Rotation = ConvertRotationVmdToUE(Keyframes[KeyBefore].Rotation);
			Rotation.Normalize();
		}
		else
		{
			// Interpolate between keyframes
			float T = (Frame - Keyframes[KeyBefore].FrameNumber) /
				static_cast<float>(Keyframes[KeyAfter].FrameNumber - Keyframes[KeyBefore].FrameNumber);

			// TODO: Use bezier interpolation from VMD data
			FVector PosBefore = ConvertPositionDeltaVmdToUE(Keyframes[KeyBefore].Position);
			FVector PosAfter = ConvertPositionDeltaVmdToUE(Keyframes[KeyAfter].Position);
			FQuat RotBefore = ConvertRotationVmdToUE(Keyframes[KeyBefore].Rotation);
			RotBefore.Normalize();
			FQuat RotAfter = ConvertRotationVmdToUE(Keyframes[KeyAfter].Rotation);
			RotAfter.Normalize();

			// Ensure shortest path SLERP (handle quaternion dual cover)
			if ((RotBefore | RotAfter) < 0.0f)
			{
				RotAfter = -RotAfter;
			}

			Position = FMath::Lerp(PosBefore, PosAfter, T);
			Rotation = FQuat::Slerp(RotBefore, RotAfter, T);
			Rotation.Normalize();
		}

		FTransform BoneTransform;
		BoneTransform.SetLocation(Position);
		BoneTransform.SetRotation(Rotation);

		OutBoneTransforms.Add(FName(*BoneName), BoneTransform);
	}
}
