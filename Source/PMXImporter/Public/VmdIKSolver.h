// Copyright (c) 2024 PMXImporter. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PmxIKMetadata.h"

/**
 * CCD-IK (Cyclic Coordinate Descent) solver for VMD animation baking
 *
 * This solver converts IK-driven animation data to FK (Forward Kinematics)
 * by solving the IK chain and storing the resulting rotations on each bone.
 */
class PMXIMPORTER_API FVmdIKSolver
{
public:
	FVmdIKSolver();

	/**
	 * Initialize the solver with IK chain information from skeleton metadata
	 * @param IKMetadata PMX IK metadata containing IK chain definitions
	 * @param InBoneTransforms Reference pose transforms for all bones (bone index -> local transform)
	 * @param InParentIndices Parent bone index for each bone (-1 for root)
	 */
	void Initialize(
		const UPmxIKMetadataUserData* IKMetadata,
		const TArray<FTransform>& InBoneTransforms,
		const TArray<int32>& InParentIndices
	);

	/**
	 * Solve IK for a single frame
	 * @param IKBoneWorldTransforms World transforms of IK control bones (bone name -> world transform)
	 * @param IKStates IK enable states (IK bone name -> enabled)
	 * @param InOutBoneLocalTransforms Current local transforms for all bones, will be modified with IK results
	 */
	void SolveFrame(
		const TMap<FName, FTransform>& IKBoneWorldTransforms,
		const TMap<FName, bool>& IKStates,
		TArray<FTransform>& InOutBoneLocalTransforms
	);

	/**
	 * Solve a single IK chain
	 * @param ChainIndex Index of the IK chain to solve
	 * @param TargetWorldPosition World position that the effector should reach
	 * @param InOutBoneLocalTransforms Local transforms to modify
	 */
	void SolveChain(
		int32 ChainIndex,
		const FVector& TargetWorldPosition,
		TArray<FTransform>& InOutBoneLocalTransforms
	);

	/** Get number of IK chains */
	int32 GetNumChains() const { return IKChains.Num(); }

	/** Check if solver is properly initialized */
	bool IsInitialized() const { return bInitialized; }

private:
	/** Internal IK chain data for solving */
	struct FIKChainData
	{
		FName IKBoneName;
		int32 IKBoneIndex;
		int32 TargetBoneIndex;  // Effector bone
		int32 LoopCount;
		float LimitAngle;       // Max rotation per iteration (radians)

		struct FLinkData
		{
			FName BoneName;
			int32 BoneIndex;
			bool bHasAngleLimits;
			FVector AngleLimitMin;  // Min angles (radians)
			FVector AngleLimitMax;  // Max angles (radians)
		};
		TArray<FLinkData> Links;
	};

	/** Calculate world transform for a bone */
	FTransform CalculateWorldTransform(int32 BoneIndex, const TArray<FTransform>& LocalTransforms) const;

	/** Calculate world position of the effector bone */
	FVector GetEffectorWorldPosition(const FIKChainData& Chain, const TArray<FTransform>& LocalTransforms) const;

	/** Apply angle limits to a rotation (euler angles in radians) */
	FQuat ApplyAngleLimits(const FQuat& Rotation, const FVector& MinAngles, const FVector& MaxAngles) const;

	/** Clamp rotation angle to limit */
	float ClampAngle(float Angle, float Limit) const;

	/** Convergence threshold for IK solving (distance in UE units) */
	static constexpr float ConvergenceThreshold = 0.1f;

	/** IK chain data */
	TArray<FIKChainData> IKChains;

	/** Reference pose local transforms */
	TArray<FTransform> ReferencePose;

	/** Parent bone indices (-1 for root) */
	TArray<int32> ParentIndices;

	/** Bone name to index mapping */
	TMap<FName, int32> BoneNameToIndex;

	/** Is solver initialized */
	bool bInitialized;
};
