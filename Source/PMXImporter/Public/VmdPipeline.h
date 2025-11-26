// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "InterchangeSourceData.h"
#include "VmdStructs.h"
#include "VmdPipeline.generated.h"

class USkeleton;
class UAnimSequence;
class IAnimationDataController;

/**
 * VMD Import Pipeline
 *
 * Handles VMD animation import settings and skeleton assignment.
 * VMD files require an existing skeleton to import animations.
 */
UCLASS(BlueprintType)
class PMXIMPORTER_API UVmdPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()

public:
	UVmdPipeline();

	//~ Begin UInterchangePipelineBase Interface
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;
	virtual void ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport) override;
	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override { return false; }
	//~ End UInterchangePipelineBase Interface

public:
	/** Target skeleton for the animation. Required for VMD import. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VMD|Animation")
	TObjectPtr<USkeleton> TargetSkeleton;

	/** Import bone animation tracks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VMD|Animation")
	bool bImportBoneAnimation = true;

	/** Import morph target animation curves */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VMD|Animation")
	bool bImportMorphAnimation = true;

	/** Import camera animation (creates Level Sequence) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VMD|Camera")
	bool bImportCameraAnimation = true;

	/** Scale factor for position data (should match PMX import scale, default: 8.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VMD|Transform", meta = (ClampMin = "0.01", ClampMax = "1000.0"))
	float Scale = 8.0f;

	/** Animation sample rate (VMD default is 30 FPS) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VMD|Animation", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	double SampleRate = 30.0;

private:
	/** Configure AnimSequence factory node with skeleton and settings */
	void ConfigureAnimSequenceNode(UInterchangeBaseNodeContainer* BaseNodeContainer);

	/** Populate AnimSequence with VMD animation data directly */
	void PopulateAnimSequenceData(UAnimSequence* AnimSequence, const FVmdModel& VmdModel);

	/** Add bone animation tracks to AnimSequence */
	void AddBoneAnimationTracks(UAnimSequence* AnimSequence, const FVmdModel& VmdModel);

	/** Add morph target curves to AnimSequence */
	void AddMorphTargetCurves(UAnimSequence* AnimSequence, const FVmdModel& VmdModel);

	/** Convert VMD position to UE coordinate system */
	FVector ConvertPositionVmdToUE(const FVector& VmdPosition) const;

	/** Convert VMD position delta to UE coordinate system (for bone animation) */
	FVector ConvertPositionDeltaVmdToUE(const FVector& VmdDelta) const;

	/** Convert VMD rotation (quaternion) to UE coordinate system */
	FQuat ConvertRotationVmdToUE(const FQuat& VmdRotation) const;

	/** Cached source file path for retrieving VMD data */
	mutable FString CachedSourceFilePath;
};
