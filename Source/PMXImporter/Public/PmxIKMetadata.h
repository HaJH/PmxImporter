// Copyright (c) 2024 PMXImporter. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "PmxIKMetadata.generated.h"

/**
 * IK link metadata for a single bone in the IK chain
 */
USTRUCT(BlueprintType)
struct PMXIMPORTER_API FPmxIKLinkMetadata
{
	GENERATED_BODY()

	/** Bone name in the IK chain (e.g., "左ひざ" for left knee) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IK")
	FName BoneName;

	/** Bone index in the skeleton */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IK")
	int32 BoneIndex = -1;

	/** Whether this link has angle limits */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IK")
	bool bHasAngleLimits = false;

	/** Minimum angle limits in radians (X=pitch, Y=yaw, Z=roll) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IK")
	FVector AngleLimitMin = FVector::ZeroVector;

	/** Maximum angle limits in radians (X=pitch, Y=yaw, Z=roll) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IK")
	FVector AngleLimitMax = FVector::ZeroVector;
};

/**
 * IK chain metadata for a single IK bone
 */
USTRUCT(BlueprintType)
struct PMXIMPORTER_API FPmxIKChainMetadata
{
	GENERATED_BODY()

	/** IK control bone name (e.g., "左足ＩＫ" for left leg IK) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IK")
	FName IKBoneName;

	/** IK control bone index */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IK")
	int32 IKBoneIndex = -1;

	/** Target (effector) bone name (e.g., "左足首" for left ankle) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IK")
	FName TargetBoneName;

	/** Target bone index */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IK")
	int32 TargetBoneIndex = -1;

	/** Number of CCD-IK iterations */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IK")
	int32 LoopCount = 8;

	/** Maximum rotation angle per iteration in radians */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IK")
	float LimitAngle = 0.03f;

	/** IK chain links (from tip to root) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IK")
	TArray<FPmxIKLinkMetadata> Links;
};

/**
 * Additional bone metadata (rotation/location inheritance)
 */
USTRUCT(BlueprintType)
struct PMXIMPORTER_API FPmxAdditionalBoneMetadata
{
	GENERATED_BODY()

	/** Bone name that has additional transform */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Additional")
	FName BoneName;

	/** Bone index */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Additional")
	int32 BoneIndex = -1;

	/** Parent bone name to inherit transform from */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Additional")
	FName ParentBoneName;

	/** Parent bone index */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Additional")
	int32 ParentBoneIndex = -1;

	/** Whether to inherit rotation */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Additional")
	bool bHasAdditionalRotation = false;

	/** Whether to inherit location */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Additional")
	bool bHasAdditionalLocation = false;

	/** Influence ratio (0.0 to 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Additional")
	float Ratio = 1.0f;
};

/**
 * Asset user data to store PMX IK metadata in Skeleton/SkeletalMesh
 * This allows VMD importer to access IK chain information
 */
UCLASS(BlueprintType)
class PMXIMPORTER_API UPmxIKMetadataUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	/** All IK chains defined in the PMX model */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PMX IK")
	TArray<FPmxIKChainMetadata> IKChains;

	/** Bones with additional transform (rotation/location inheritance) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PMX Additional")
	TArray<FPmxAdditionalBoneMetadata> AdditionalBones;

	/** Original PMX model name */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PMX Info")
	FString ModelName;

	/** Bone name to index mapping for quick lookup */
	UPROPERTY()
	TMap<FName, int32> BoneNameToIndex;

	/**
	 * Find IK chain by IK bone name
	 * @param IKBoneName Name of the IK control bone
	 * @return Pointer to IK chain metadata, or nullptr if not found
	 */
	const FPmxIKChainMetadata* FindIKChainByName(const FName& IKBoneName) const
	{
		for (const FPmxIKChainMetadata& Chain : IKChains)
		{
			if (Chain.IKBoneName == IKBoneName)
			{
				return &Chain;
			}
		}
		return nullptr;
	}

	/**
	 * Find IK chain by target bone name
	 * @param TargetBoneName Name of the target (effector) bone
	 * @return Pointer to IK chain metadata, or nullptr if not found
	 */
	const FPmxIKChainMetadata* FindIKChainByTarget(const FName& TargetBoneName) const
	{
		for (const FPmxIKChainMetadata& Chain : IKChains)
		{
			if (Chain.TargetBoneName == TargetBoneName)
			{
				return &Chain;
			}
		}
		return nullptr;
	}

	/**
	 * Get bone index by name
	 * @param BoneName Name of the bone
	 * @return Bone index, or -1 if not found
	 */
	int32 GetBoneIndex(const FName& BoneName) const
	{
		const int32* Index = BoneNameToIndex.Find(BoneName);
		return Index ? *Index : -1;
	}
};
