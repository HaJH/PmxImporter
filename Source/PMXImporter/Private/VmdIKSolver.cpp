// Copyright (c) 2024 PMXImporter. All Rights Reserved.

#include "VmdIKSolver.h"
#include "LogPMXImporter.h"
#include <atomic>

FVmdIKSolver::FVmdIKSolver()
	: bInitialized(false)
{
}

void FVmdIKSolver::Initialize(
	const UPmxIKMetadataUserData* IKMetadata,
	const TArray<FTransform>& InBoneTransforms,
	const TArray<int32>& InParentIndices)
{
	bInitialized = false;
	IKChains.Empty();
	ReferencePose = InBoneTransforms;
	ParentIndices = InParentIndices;

	if (!IKMetadata)
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("FVmdIKSolver: No IK metadata provided"));
		return;
	}

	// Copy bone name to index mapping (this maps bone names to UE skeleton indices)
	BoneNameToIndex = IKMetadata->BoneNameToIndex;

	// Convert IK chain metadata to internal format
	// IMPORTANT: The metadata stores PMX bone indices, but we need UE skeleton indices
	// Use BoneNameToIndex to remap indices correctly
	for (const FPmxIKChainMetadata& ChainMeta : IKMetadata->IKChains)
	{
		FIKChainData Chain;
		Chain.IKBoneName = ChainMeta.IKBoneName;

		// Remap IK bone index using bone name
		const int32* IKBoneIdx = BoneNameToIndex.Find(ChainMeta.IKBoneName);
		Chain.IKBoneIndex = IKBoneIdx ? *IKBoneIdx : -1;

		// Remap target bone index using bone name
		const int32* TargetBoneIdx = BoneNameToIndex.Find(ChainMeta.TargetBoneName);
		Chain.TargetBoneIndex = TargetBoneIdx ? *TargetBoneIdx : -1;

		Chain.LoopCount = FMath::Max(1, ChainMeta.LoopCount);
		Chain.LimitAngle = ChainMeta.LimitAngle;

		// Convert links with proper index remapping
		for (const FPmxIKLinkMetadata& LinkMeta : ChainMeta.Links)
		{
			FIKChainData::FLinkData Link;
			Link.BoneName = LinkMeta.BoneName;

			// Remap link bone index using bone name
			const int32* LinkBoneIdx = BoneNameToIndex.Find(LinkMeta.BoneName);
			Link.BoneIndex = LinkBoneIdx ? *LinkBoneIdx : -1;

			Link.bHasAngleLimits = LinkMeta.bHasAngleLimits;
			Link.AngleLimitMin = LinkMeta.AngleLimitMin;
			Link.AngleLimitMax = LinkMeta.AngleLimitMax;

			if (Link.BoneIndex >= 0)
			{
				Chain.Links.Add(Link);
			}
			else
			{
				UE_LOG(LogPMXImporter, Warning, TEXT("FVmdIKSolver: Link bone '%s' not found in skeleton, skipping"),
					*LinkMeta.BoneName.ToString());
			}
		}

		if (Chain.Links.Num() > 0 && Chain.TargetBoneIndex >= 0)
		{
			IKChains.Add(Chain);
			UE_LOG(LogPMXImporter, Verbose, TEXT("FVmdIKSolver: Added IK chain '%s' (idx %d) -> Target idx %d, %d links, %d iterations"),
				*Chain.IKBoneName.ToString(), Chain.IKBoneIndex, Chain.TargetBoneIndex, Chain.Links.Num(), Chain.LoopCount);
		}
		else
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("FVmdIKSolver: Skipping IK chain '%s' - missing target or links"),
				*ChainMeta.IKBoneName.ToString());
		}
	}

	bInitialized = (IKChains.Num() > 0 && ReferencePose.Num() > 0);
	UE_LOG(LogPMXImporter, Verbose, TEXT("FVmdIKSolver: Initialized with %d IK chains, %d bones"),
		IKChains.Num(), ReferencePose.Num());
}

// Static counter for diagnostic logging (only log first few frames)
static std::atomic<int32> GIKSolveLogCounter(0);

void FVmdIKSolver::SolveFrame(
	const TMap<FName, FTransform>& IKBoneWorldTransforms,
	const TMap<FName, bool>& IKStates,
	TArray<FTransform>& InOutBoneLocalTransforms)
{
	if (!bInitialized)
	{
		return;
	}

	// Log diagnostics for first frame only
	int32 LogCount = GIKSolveLogCounter.fetch_add(1);
	bool bShouldLog = (LogCount < 4); // Log first 4 calls (one per chain on first frame)

	// Process each IK chain
	for (int32 ChainIdx = 0; ChainIdx < IKChains.Num(); ++ChainIdx)
	{
		const FIKChainData& Chain = IKChains[ChainIdx];

		// Check if IK is enabled for this chain
		const bool* bEnabled = IKStates.Find(Chain.IKBoneName);
		if (bEnabled && !(*bEnabled))
		{
			if (bShouldLog)
			{
				UE_LOG(LogPMXImporter, Display, TEXT("FVmdIKSolver: IK '%s' is DISABLED, skipping"), *Chain.IKBoneName.ToString());
			}
			continue;
		}

		// Get target position from IK bone's world transform
		const FTransform* IKBoneTransform = IKBoneWorldTransforms.Find(Chain.IKBoneName);
		if (!IKBoneTransform)
		{
			if (bShouldLog)
			{
				UE_LOG(LogPMXImporter, Warning, TEXT("FVmdIKSolver: IK bone '%s' NOT FOUND in world transforms (available: %d bones)"),
					*Chain.IKBoneName.ToString(), IKBoneWorldTransforms.Num());
				for (const auto& Pair : IKBoneWorldTransforms)
				{
					UE_LOG(LogPMXImporter, Warning, TEXT("  - Available: '%s'"), *Pair.Key.ToString());
				}
			}
			continue;
		}

		// Get effector position before solving
		FVector EffectorBefore = GetEffectorWorldPosition(Chain, InOutBoneLocalTransforms);
		FVector TargetPos = IKBoneTransform->GetLocation();

		// Solve this chain
		SolveChain(ChainIdx, TargetPos, InOutBoneLocalTransforms);

		// Get effector position after solving
		FVector EffectorAfter = GetEffectorWorldPosition(Chain, InOutBoneLocalTransforms);

		if (bShouldLog)
		{
			UE_LOG(LogPMXImporter, Display, TEXT("FVmdIKSolver: Solved '%s' - Target: (%.1f, %.1f, %.1f), Effector Before: (%.1f, %.1f, %.1f), After: (%.1f, %.1f, %.1f)"),
				*Chain.IKBoneName.ToString(),
				TargetPos.X, TargetPos.Y, TargetPos.Z,
				EffectorBefore.X, EffectorBefore.Y, EffectorBefore.Z,
				EffectorAfter.X, EffectorAfter.Y, EffectorAfter.Z);
		}
	}
}

void FVmdIKSolver::SolveChain(
	int32 ChainIndex,
	const FVector& TargetWorldPosition,
	TArray<FTransform>& InOutBoneLocalTransforms)
{
	if (!bInitialized || ChainIndex < 0 || ChainIndex >= IKChains.Num())
	{
		return;
	}

	const FIKChainData& Chain = IKChains[ChainIndex];

	if (Chain.Links.Num() == 0 || Chain.TargetBoneIndex < 0)
	{
		return;
	}

	// CCD-IK algorithm
	for (int32 Iteration = 0; Iteration < Chain.LoopCount; ++Iteration)
	{
		// Process each link from tip to root
		for (int32 LinkIdx = 0; LinkIdx < Chain.Links.Num(); ++LinkIdx)
		{
			const FIKChainData::FLinkData& Link = Chain.Links[LinkIdx];

			if (Link.BoneIndex < 0 || Link.BoneIndex >= InOutBoneLocalTransforms.Num())
			{
				continue;
			}

			// Get current link world transform
			FTransform LinkWorldTransform = CalculateWorldTransform(Link.BoneIndex, InOutBoneLocalTransforms);
			FVector LinkWorldPos = LinkWorldTransform.GetLocation();

			// Get current effector world position
			FVector EffectorWorldPos = GetEffectorWorldPosition(Chain, InOutBoneLocalTransforms);

			// Calculate vectors from link to effector and target
			FVector ToEffector = EffectorWorldPos - LinkWorldPos;
			FVector ToTarget = TargetWorldPosition - LinkWorldPos;

			// Normalize vectors
			float EffectorDist = ToEffector.Size();
			float TargetDist = ToTarget.Size();

			if (EffectorDist < KINDA_SMALL_NUMBER || TargetDist < KINDA_SMALL_NUMBER)
			{
				continue;
			}

			ToEffector /= EffectorDist;
			ToTarget /= TargetDist;

			// Calculate rotation to align effector with target
			float DotProduct = FVector::DotProduct(ToEffector, ToTarget);
			DotProduct = FMath::Clamp(DotProduct, -1.0f, 1.0f);

			float Angle = FMath::Acos(DotProduct);

			// Clamp angle to limit per iteration
			if (Chain.LimitAngle > 0.0f && Angle > Chain.LimitAngle)
			{
				Angle = Chain.LimitAngle;
			}

			// Skip if angle is too small
			if (Angle < KINDA_SMALL_NUMBER)
			{
				continue;
			}

			// Calculate rotation axis
			FVector RotationAxis = FVector::CrossProduct(ToEffector, ToTarget);
			if (RotationAxis.SizeSquared() < KINDA_SMALL_NUMBER)
			{
				continue;
			}
			RotationAxis.Normalize();

			// Create rotation quaternion in world space
			FQuat WorldRotation = FQuat(RotationAxis, Angle);

			// Convert to local space rotation
			// Get parent world transform
			FTransform ParentWorldTransform = FTransform::Identity;
			if (ParentIndices.IsValidIndex(Link.BoneIndex) && ParentIndices[Link.BoneIndex] >= 0)
			{
				ParentWorldTransform = CalculateWorldTransform(ParentIndices[Link.BoneIndex], InOutBoneLocalTransforms);
			}

			// Current local rotation
			FQuat CurrentLocalRotation = InOutBoneLocalTransforms[Link.BoneIndex].GetRotation();

			// Calculate new world rotation for the link bone
			FQuat CurrentWorldRotation = LinkWorldTransform.GetRotation();
			FQuat NewWorldRotation = WorldRotation * CurrentWorldRotation;

			// Convert back to local space
			FQuat ParentWorldRotation = ParentWorldTransform.GetRotation();
			FQuat NewLocalRotation = ParentWorldRotation.Inverse() * NewWorldRotation;
			NewLocalRotation.Normalize();

			// Ensure W is positive for consistency (quaternion dual cover)
			if (NewLocalRotation.W < 0.0f)
			{
				NewLocalRotation = -NewLocalRotation;
			}

			// Apply angle limits if this link has them
			if (Link.bHasAngleLimits)
			{
				NewLocalRotation = ApplyAngleLimits(NewLocalRotation, Link.AngleLimitMin, Link.AngleLimitMax);
			}

			// Update local transform
			InOutBoneLocalTransforms[Link.BoneIndex].SetRotation(NewLocalRotation);
		}

		// Check for convergence
		FVector CurrentEffectorPos = GetEffectorWorldPosition(Chain, InOutBoneLocalTransforms);
		float DistanceToTarget = FVector::Distance(CurrentEffectorPos, TargetWorldPosition);

		if (DistanceToTarget < ConvergenceThreshold)
		{
			break;
		}
	}
}

FTransform FVmdIKSolver::CalculateWorldTransform(int32 BoneIndex, const TArray<FTransform>& LocalTransforms) const
{
	if (BoneIndex < 0 || BoneIndex >= LocalTransforms.Num())
	{
		return FTransform::Identity;
	}

	FTransform WorldTransform = LocalTransforms[BoneIndex];

	// Walk up the parent chain
	int32 CurrentIndex = BoneIndex;
	while (ParentIndices.IsValidIndex(CurrentIndex) && ParentIndices[CurrentIndex] >= 0)
	{
		int32 ParentIndex = ParentIndices[CurrentIndex];
		if (ParentIndex >= 0 && ParentIndex < LocalTransforms.Num())
		{
			WorldTransform = WorldTransform * LocalTransforms[ParentIndex];
		}
		CurrentIndex = ParentIndex;
	}

	return WorldTransform;
}

FVector FVmdIKSolver::GetEffectorWorldPosition(const FIKChainData& Chain, const TArray<FTransform>& LocalTransforms) const
{
	// The effector is the target bone (e.g., ankle for leg IK)
	FTransform EffectorWorld = CalculateWorldTransform(Chain.TargetBoneIndex, LocalTransforms);
	return EffectorWorld.GetLocation();
}

FQuat FVmdIKSolver::ApplyAngleLimits(const FQuat& Rotation, const FVector& MinAngles, const FVector& MaxAngles) const
{
	// Convert quaternion to euler angles
	FRotator Rotator = Rotation.Rotator();

	// Convert to radians
	float PitchRad = FMath::DegreesToRadians(Rotator.Pitch);
	float YawRad = FMath::DegreesToRadians(Rotator.Yaw);
	float RollRad = FMath::DegreesToRadians(Rotator.Roll);

	// Clamp to limits (angles are in radians)
	PitchRad = FMath::Clamp(PitchRad, MinAngles.X, MaxAngles.X);
	YawRad = FMath::Clamp(YawRad, MinAngles.Y, MaxAngles.Y);
	RollRad = FMath::Clamp(RollRad, MinAngles.Z, MaxAngles.Z);

	// Convert back to quaternion
	FRotator ClampedRotator(
		FMath::RadiansToDegrees(PitchRad),
		FMath::RadiansToDegrees(YawRad),
		FMath::RadiansToDegrees(RollRad)
	);

	return ClampedRotator.Quaternion();
}

float FVmdIKSolver::ClampAngle(float Angle, float Limit) const
{
	if (Limit <= 0.0f)
	{
		return Angle;
	}
	return FMath::Clamp(Angle, -Limit, Limit);
}
