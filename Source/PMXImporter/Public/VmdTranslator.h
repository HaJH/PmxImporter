// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "InterchangeTranslatorBase.h"
#include "Animation/InterchangeAnimationPayloadInterface.h"
#include "VmdStructs.h"
#include "VmdTranslator.generated.h"

class UInterchangeSourceData;
class UInterchangeBaseNodeContainer;

/**
 * VMD import options
 */
USTRUCT()
struct FVmdImportOptions
{
	GENERATED_BODY()

	/** Scale factor for position data (default: 8.0 to match PMX) */
	UPROPERTY()
	float Scale = 8.0f;

	/** Import bone animation tracks */
	UPROPERTY()
	bool bImportBoneAnimation = true;

	/** Import morph target animation curves */
	UPROPERTY()
	bool bImportMorphAnimation = true;

	/** Import camera animation (to Level Sequence) */
	UPROPERTY()
	bool bImportCameraAnimation = true;

	/** Sample rate for animation (VMD default is 30 FPS) */
	UPROPERTY()
	double SampleRate = 30.0;

	/** Target skeleton soft object path (if not set, will try to find matching skeleton) */
	UPROPERTY()
	FSoftObjectPath TargetSkeletonPath;
};

/**
 * VMD (Vocaloid Motion Data) Translator for Interchange.
 * Imports MikuMikuDance animation files into Unreal Engine.
 *
 * Supports:
 * - Bone animation -> AnimSequence
 * - Morph target animation -> AnimSequence curves
 * - Camera animation -> Level Sequence
 */
UCLASS()
class PMXIMPORTER_API UVmdTranslator : public UInterchangeTranslatorBase, public IInterchangeAnimationPayloadInterface
{
	GENERATED_BODY()

public:
	/** Get supported file formats */
	virtual TArray<FString> GetSupportedFormats() const override
	{
		return { TEXT("vmd;MikuMikuDance Motion Data") };
	}

	/** Get translator type */
	virtual EInterchangeTranslatorType GetTranslatorType() const override
	{
		return EInterchangeTranslatorType::Scenes;
	}

	/** Get supported asset types */
	virtual EInterchangeTranslatorAssetType GetSupportedAssetTypes() const override
	{
		return EInterchangeTranslatorAssetType::Animations;
	}

	/** Check if this translator can import the given source data */
	virtual bool CanImportSourceData(const UInterchangeSourceData* InSourceData) const override;

	/** Translate VMD file to Interchange nodes */
	virtual bool Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const override;

	//~ Begin IInterchangeAnimationPayloadInterface
	virtual TArray<UE::Interchange::FAnimationPayloadData> GetAnimationPayloadData(
		const TArray<UE::Interchange::FAnimationPayloadQuery>& PayloadQueries) const override;
	//~ End IInterchangeAnimationPayloadInterface

	/** Static cache for VMD data per import session */
	static TMap<FString, TSharedPtr<FVmdModel>> VmdPayloadCache;

private:
	/** Import options */
	mutable FVmdImportOptions ImportOptions;

	/** Cached VMD model for current translation */
	mutable TSharedPtr<FVmdModel> CachedVmdModel;

	/** Source file path for current translation */
	mutable FString SourceFilePath;

	/**
	 * Create AnimSequence factory node for bone/morph animation
	 */
	void CreateAnimSequenceNode(
		const FVmdModel& VmdModel,
		UInterchangeBaseNodeContainer& BaseNodeContainer,
		const FString& SkeletonUid) const;

	/**
	 * Create Level Sequence nodes for camera animation
	 */
	void CreateCameraAnimationNodes(
		const FVmdModel& VmdModel,
		UInterchangeBaseNodeContainer& BaseNodeContainer) const;

	/**
	 * Build bone animation payload data
	 */
	UE::Interchange::FAnimationPayloadData BuildBoneAnimationPayload(
		const FVmdModel& VmdModel,
		const FString& BoneName,
		const UE::Interchange::FAnimationPayloadQuery& Query) const;

	/**
	 * Build morph target animation payload data
	 */
	UE::Interchange::FAnimationPayloadData BuildMorphAnimationPayload(
		const FVmdModel& VmdModel,
		const FString& MorphName,
		const UE::Interchange::FAnimationPayloadQuery& Query) const;

	/**
	 * Convert VMD position to UE coordinate system
	 */
	FVector ConvertPositionVmdToUE(const FVector& VmdPosition) const;

	/**
	 * Convert VMD quaternion rotation to UE coordinate system
	 */
	FQuat ConvertRotationVmdToUE(const FQuat& VmdRotation) const;

	/**
	 * Convert VMD euler rotation (radians) to UE coordinate system
	 */
	FRotator ConvertEulerVmdToUE(const FVector& VmdEulerRad) const;

	/**
	 * Apply bezier interpolation to FRichCurve key
	 */
	void ApplyBezierInterpolation(
		FRichCurve& Curve,
		int32 KeyIndex,
		const uint8* Interpolation,
		float FrameDelta) const;

	/**
	 * Generate unique payload key
	 */
	FString GeneratePayloadKey(const FString& BaseName, const FString& Type) const;

	/**
	 * Get or load VMD model from cache
	 */
	TSharedPtr<FVmdModel> GetOrLoadVmdModel(const FString& FilePath) const;
};
