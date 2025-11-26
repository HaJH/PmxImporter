// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved.

#include "VmdTranslator.h"
#include "VmdReader.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "InterchangeAnimSequenceFactoryNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "InterchangeAnimationTrackSetNode.h"
#include "Curves/RichCurve.h"
#include "Misc/Paths.h"
#include "LogPMXImporter.h"

// Static cache initialization
TMap<FString, TSharedPtr<FVmdModel>> UVmdTranslator::VmdPayloadCache;

bool UVmdTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
	if (!InSourceData)
	{
		return false;
	}

	FString FilePath = InSourceData->GetFilename();
	FString Extension = FPaths::GetExtension(FilePath).ToLower();

	if (Extension != TEXT("vmd"))
	{
		return false;
	}

	// Quick validation: check file header
	TArray<uint8> HeaderData;
	if (!FFileHelper::LoadFileToArray(HeaderData, *FilePath))
	{
		return false;
	}

	if (HeaderData.Num() < 30)
	{
		return false;
	}

	// Check VMD signature
	FString Signature;
	for (int32 i = 0; i < 25 && HeaderData[i] != 0; ++i)
	{
		Signature.AppendChar(static_cast<TCHAR>(HeaderData[i]));
	}

	return Signature.StartsWith(TEXT("Vocaloid Motion Data"));
}

bool UVmdTranslator::Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	if (!SourceData)
	{
		UE_LOG(LogPMXImporter, Error, TEXT("VMD Translator: No source data"));
		return false;
	}

	SourceFilePath = SourceData->GetFilename();

	// Load VMD file
	CachedVmdModel = GetOrLoadVmdModel(SourceFilePath);
	if (!CachedVmdModel.IsValid())
	{
		UE_LOG(LogPMXImporter, Error, TEXT("VMD Translator: Failed to load VMD file: %s"), *SourceFilePath);
		return false;
	}

	const FVmdModel& VmdModel = *CachedVmdModel;

	// Read import options from SourceNode custom attributes
	// (These would be set by a VMD pipeline, similar to PmxPipeline)
	// For now, use defaults

	// Create AnimSequence node for bone/morph animation
	if (VmdModel.HasModelAnimation())
	{
		// We need a skeleton UID - this would typically come from:
		// 1. User-specified skeleton
		// 2. PMX file imported alongside
		// 3. Skeleton with matching bone names

		// For now, we'll create a placeholder that the pipeline can fill in
		FString SkeletonUid = TEXT(""); // Will be set by pipeline
		CreateAnimSequenceNode(VmdModel, BaseNodeContainer, SkeletonUid);
	}

	// Create camera animation nodes
	if (VmdModel.HasCameraAnimation() && ImportOptions.bImportCameraAnimation)
	{
		CreateCameraAnimationNodes(VmdModel, BaseNodeContainer);
	}

	return true;
}

void UVmdTranslator::CreateAnimSequenceNode(
	const FVmdModel& VmdModel,
	UInterchangeBaseNodeContainer& BaseNodeContainer,
	const FString& SkeletonUid) const
{
	FString AnimName = FPaths::GetBaseFilename(SourceFilePath);
	FString AnimNodeUid = TEXT("\\AnimSequence\\") + AnimName;

	UInterchangeAnimSequenceFactoryNode* AnimNode = NewObject<UInterchangeAnimSequenceFactoryNode>(&BaseNodeContainer);
	AnimNode->InitializeNode(AnimNodeUid, AnimName, EInterchangeNodeContainerType::FactoryData);

	// Set skeleton dependency (if provided)
	if (!SkeletonUid.IsEmpty())
	{
		AnimNode->SetCustomSkeletonFactoryNodeUid(SkeletonUid);
	}

	// Animation settings
	AnimNode->SetCustomImportBoneTracks(ImportOptions.bImportBoneAnimation);
	AnimNode->SetCustomImportBoneTracksSampleRate(ImportOptions.SampleRate);
	AnimNode->SetCustomImportBoneTracksRangeStart(0.0);

	// Calculate frame-aligned duration (must be exact multiple of frame time)
	// VMD is 30 FPS, so duration must align to 1/30 second boundaries
	const double FrameRate = 30.0;
	double RawDuration = VmdModel.GetDurationSeconds();
	int32 TotalFrames = FMath::CeilToInt(RawDuration * FrameRate);
	double AlignedDuration = TotalFrames / FrameRate;
	AnimNode->SetCustomImportBoneTracksRangeStop(AlignedDuration);

	// Curve settings
	AnimNode->SetCustomImportAttributeCurves(true);
	AnimNode->SetCustomDoNotImportCurveWithZero(false);
	AnimNode->SetCustomDeleteExistingMorphTargetCurves(true);

	// Build payload maps for bone animation
	if (ImportOptions.bImportBoneAnimation)
	{
		TMap<FString, FString> BonePayloadKeyUids;
		TMap<FString, uint8> BonePayloadKeyTypes;

		for (const FString& BoneName : VmdModel.GetUniqueBoneNames())
		{
			FString PayloadKey = GeneratePayloadKey(BoneName, TEXT("Bone"));
			FString SceneNodeUid = TEXT("\\Bone\\") + BoneName;

			BonePayloadKeyUids.Add(SceneNodeUid, PayloadKey);
			BonePayloadKeyTypes.Add(SceneNodeUid, static_cast<uint8>(EInterchangeAnimationPayLoadType::CURVE));
		}

		AnimNode->SetAnimationPayloadKeysForSceneNodeUids(BonePayloadKeyUids, BonePayloadKeyTypes);
	}

	// Build payload maps for morph animation
	if (ImportOptions.bImportMorphAnimation)
	{
		TMap<FString, FString> MorphPayloadKeyUids;
		TMap<FString, uint8> MorphPayloadKeyTypes;

		for (const FString& MorphName : VmdModel.GetUniqueMorphNames())
		{
			FString PayloadKey = GeneratePayloadKey(MorphName, TEXT("Morph"));
			FString MorphNodeUid = TEXT("\\MorphTarget\\") + MorphName;

			MorphPayloadKeyUids.Add(MorphNodeUid, PayloadKey);
			MorphPayloadKeyTypes.Add(MorphNodeUid, static_cast<uint8>(EInterchangeAnimationPayLoadType::MORPHTARGETCURVE));
		}

		AnimNode->SetAnimationPayloadKeysForMorphTargetNodeUids(MorphPayloadKeyUids, MorphPayloadKeyTypes);
	}

	BaseNodeContainer.AddNode(AnimNode);
}

void UVmdTranslator::CreateCameraAnimationNodes(
	const FVmdModel& VmdModel,
	UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	if (VmdModel.CameraKeyframes.Num() == 0)
	{
		return;
	}

	FString BaseName = FPaths::GetBaseFilename(SourceFilePath);

	// Create AnimationTrackSet node for Level Sequence
	FString TrackSetUid = TEXT("\\AnimationTrackSet\\") + BaseName + TEXT("_Camera");
	UInterchangeAnimationTrackSetNode* TrackSetNode = NewObject<UInterchangeAnimationTrackSetNode>(&BaseNodeContainer);
	TrackSetNode->InitializeNode(TrackSetUid, BaseName + TEXT("_Camera"), EInterchangeNodeContainerType::TranslatedAsset);

	// Set frame rate
	TrackSetNode->SetCustomFrameRate(ImportOptions.SampleRate);

	// Create individual property tracks
	// Camera animation in VMD uses:
	// - Position (target position)
	// - Rotation (euler angles)
	// - Distance (from target)
	// - FOV

	// For Level Sequence, we need to create tracks for camera actor properties
	// This is more complex and requires proper integration with the Level Sequence system
	// For now, we'll create placeholder nodes

	BaseNodeContainer.AddNode(TrackSetNode);
}

TArray<UE::Interchange::FAnimationPayloadData> UVmdTranslator::GetAnimationPayloadData(
	const TArray<UE::Interchange::FAnimationPayloadQuery>& PayloadQueries) const
{
	TArray<UE::Interchange::FAnimationPayloadData> PayloadResults;

	if (!CachedVmdModel.IsValid())
	{
		// Try to reload from source
		if (!SourceFilePath.IsEmpty())
		{
			CachedVmdModel = GetOrLoadVmdModel(SourceFilePath);
		}

		if (!CachedVmdModel.IsValid())
		{
			UE_LOG(LogPMXImporter, Error, TEXT("VMD Translator: No cached VMD model for payload queries"));
			return PayloadResults;
		}
	}

	const FVmdModel& VmdModel = *CachedVmdModel;

	for (const UE::Interchange::FAnimationPayloadQuery& Query : PayloadQueries)
	{
		FString PayloadKey = Query.PayloadKey.UniqueId;

		UE_LOG(LogPMXImporter, Verbose, TEXT("VMD Translator: Processing payload query: %s"), *PayloadKey);

		// Parse payload key to determine type and name
		// Format: "VMD_<Type>_<Name>"
		if (PayloadKey.StartsWith(TEXT("VMD_Bone_")))
		{
			FString BoneName = PayloadKey.RightChop(9); // Remove "VMD_Bone_"
			PayloadResults.Add(BuildBoneAnimationPayload(VmdModel, BoneName, Query));
			UE_LOG(LogPMXImporter, Verbose, TEXT("VMD Translator: Built bone payload for '%s'"), *BoneName);
		}
		else if (PayloadKey.StartsWith(TEXT("VMD_Morph_")))
		{
			FString MorphName = PayloadKey.RightChop(10); // Remove "VMD_Morph_"
			PayloadResults.Add(BuildMorphAnimationPayload(VmdModel, MorphName, Query));
			UE_LOG(LogPMXImporter, Verbose, TEXT("VMD Translator: Built morph payload for '%s'"), *MorphName);
		}
		else
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("VMD Translator: Unknown payload key format: %s"), *PayloadKey);
		}
	}

	return PayloadResults;
}

UE::Interchange::FAnimationPayloadData UVmdTranslator::BuildBoneAnimationPayload(
	const FVmdModel& VmdModel,
	const FString& BoneName,
	const UE::Interchange::FAnimationPayloadQuery& Query) const
{
	UE::Interchange::FAnimationPayloadData PayloadData(Query.SceneNodeUniqueID, Query.PayloadKey);

	TArray<FVmdBoneKeyframe> Keyframes = VmdModel.GetBoneKeyframes(BoneName);
	if (Keyframes.Num() == 0)
	{
		return PayloadData;
	}

	// Create 7 curves: PosX, PosY, PosZ, RotX, RotY, RotZ, RotW
	PayloadData.Curves.SetNum(7);

	for (int32 i = 0; i < Keyframes.Num(); ++i)
	{
		const FVmdBoneKeyframe& Key = Keyframes[i];
		float Time = Key.FrameNumber / 30.0f; // VMD is 30 FPS

		// Convert position
		FVector UEPosition = ConvertPositionVmdToUE(Key.Position);

		// Convert rotation
		FQuat UERotation = ConvertRotationVmdToUE(Key.Rotation);

		// Add position keys
		PayloadData.Curves[0].AddKey(Time, UEPosition.X);
		PayloadData.Curves[1].AddKey(Time, UEPosition.Y);
		PayloadData.Curves[2].AddKey(Time, UEPosition.Z);

		// Add rotation keys (quaternion)
		PayloadData.Curves[3].AddKey(Time, UERotation.X);
		PayloadData.Curves[4].AddKey(Time, UERotation.Y);
		PayloadData.Curves[5].AddKey(Time, UERotation.Z);
		PayloadData.Curves[6].AddKey(Time, UERotation.W);

		// Apply bezier interpolation
		int32 KeyIndex = PayloadData.Curves[0].GetNumKeys() - 1;
		if (KeyIndex > 0 && i > 0)
		{
			float FrameDelta = (Key.FrameNumber - Keyframes[i - 1].FrameNumber) / 30.0f;

			// X interpolation
			ApplyBezierInterpolation(PayloadData.Curves[0], KeyIndex, &Key.Interpolation[0], FrameDelta);
			// Y interpolation
			ApplyBezierInterpolation(PayloadData.Curves[1], KeyIndex, &Key.Interpolation[16], FrameDelta);
			// Z interpolation
			ApplyBezierInterpolation(PayloadData.Curves[2], KeyIndex, &Key.Interpolation[32], FrameDelta);
			// Rotation interpolation (applied to all rotation curves)
			ApplyBezierInterpolation(PayloadData.Curves[3], KeyIndex, &Key.Interpolation[48], FrameDelta);
			ApplyBezierInterpolation(PayloadData.Curves[4], KeyIndex, &Key.Interpolation[48], FrameDelta);
			ApplyBezierInterpolation(PayloadData.Curves[5], KeyIndex, &Key.Interpolation[48], FrameDelta);
			ApplyBezierInterpolation(PayloadData.Curves[6], KeyIndex, &Key.Interpolation[48], FrameDelta);
		}
	}

	// Set time range
	PayloadData.BakeFrequency = ImportOptions.SampleRate;
	PayloadData.RangeStartTime = 0.0;
	PayloadData.RangeEndTime = VmdModel.GetDurationSeconds();

	return PayloadData;
}

UE::Interchange::FAnimationPayloadData UVmdTranslator::BuildMorphAnimationPayload(
	const FVmdModel& VmdModel,
	const FString& MorphName,
	const UE::Interchange::FAnimationPayloadQuery& Query) const
{
	UE::Interchange::FAnimationPayloadData PayloadData(Query.SceneNodeUniqueID, Query.PayloadKey);

	TArray<FVmdMorphKeyframe> Keyframes = VmdModel.GetMorphKeyframes(MorphName);
	if (Keyframes.Num() == 0)
	{
		return PayloadData;
	}

	// Create single curve for morph weight
	PayloadData.Curves.SetNum(1);
	FRichCurve& WeightCurve = PayloadData.Curves[0];

	for (const FVmdMorphKeyframe& Key : Keyframes)
	{
		float Time = Key.FrameNumber / 30.0f;
		FKeyHandle KeyHandle = WeightCurve.AddKey(Time, Key.Weight);

		// Morph keyframes use linear interpolation in VMD
		WeightCurve.SetKeyInterpMode(KeyHandle, RCIM_Linear);
	}

	// Set time range
	PayloadData.BakeFrequency = ImportOptions.SampleRate;
	PayloadData.RangeStartTime = 0.0;
	PayloadData.RangeEndTime = VmdModel.GetDurationSeconds();

	return PayloadData;
}

FVector UVmdTranslator::ConvertPositionVmdToUE(const FVector& VmdPosition) const
{
	// VMD/PMX: Right-handed Y-up (X right, Y up, Z forward)
	// UE: Left-handed Z-up (X forward, Y right, Z up)
	// Apply X-axis 90 degree rotation (same as PmxNodeBuilder/VmdPipeline)
	// After rotation: X stays X, Y becomes -Z, Z becomes Y
	return FVector(VmdPosition.X, -VmdPosition.Z, VmdPosition.Y) * ImportOptions.Scale;
}

FQuat UVmdTranslator::ConvertRotationVmdToUE(const FQuat& VmdRotation) const
{
	// Convert quaternion from VMD/PMX to UE coordinate system
	// Same axis swap as PmxTranslator::ConvertQuaternionPmxToUE and VmdPipeline
	// (X, Y, Z, W) -> (X, Z, Y, W) - W sign preserved
	return FQuat(VmdRotation.X, VmdRotation.Z, VmdRotation.Y, VmdRotation.W);
}

FRotator UVmdTranslator::ConvertEulerVmdToUE(const FVector& VmdEulerRad) const
{
	// Convert euler angles from VMD to UE
	// VMD uses radians, UE uses degrees
	return FRotator(
		FMath::RadiansToDegrees(-VmdEulerRad.X),  // Pitch (X rotation)
		FMath::RadiansToDegrees(-VmdEulerRad.Y),  // Yaw (Y rotation)
		FMath::RadiansToDegrees(VmdEulerRad.Z)    // Roll (Z rotation)
	);
}

void UVmdTranslator::ApplyBezierInterpolation(
	FRichCurve& Curve,
	int32 KeyIndex,
	const uint8* Interpolation,
	float FrameDelta) const
{
	if (KeyIndex <= 0 || Curve.GetNumKeys() <= KeyIndex)
	{
		return;
	}

	// VMD bezier interpolation format:
	// 16 bytes per axis, but actually uses [0], [4], [8], [12] for x1, y1, x2, y2
	// Values range 0-127, normalized to 0.0-1.0

	uint8 X1 = Interpolation[0];
	uint8 Y1 = Interpolation[4];
	uint8 X2 = Interpolation[8];
	uint8 Y2 = Interpolation[12];

	// Check if linear (x1 == y1 && x2 == y2)
	bool bIsLinear = (X1 == Y1) && (X2 == Y2);

	FRichCurveKey& CurrentKey = Curve.Keys[KeyIndex];
	FRichCurveKey& PrevKey = Curve.Keys[KeyIndex - 1];

	if (bIsLinear)
	{
		PrevKey.InterpMode = RCIM_Linear;
		CurrentKey.InterpMode = RCIM_Linear;
	}
	else
	{
		// Use cubic interpolation with bezier handles
		PrevKey.InterpMode = RCIM_Cubic;
		PrevKey.TangentMode = RCTM_User;

		CurrentKey.InterpMode = RCIM_Cubic;
		CurrentKey.TangentMode = RCTM_User;

		// Calculate tangents from bezier control points
		float NormX1 = X1 / 127.0f;
		float NormY1 = Y1 / 127.0f;
		float NormX2 = X2 / 127.0f;
		float NormY2 = Y2 / 127.0f;

		float ValueDelta = CurrentKey.Value - PrevKey.Value;

		// Convert bezier control points to tangents
		// This is an approximation - VMD uses 2D bezier, UE uses tangent slopes
		if (FrameDelta > SMALL_NUMBER)
		{
			// Leave tangent for previous key
			float LeaveTangent = (NormY1 * ValueDelta) / (NormX1 * FrameDelta);
			if (FMath::IsFinite(LeaveTangent))
			{
				PrevKey.LeaveTangent = LeaveTangent;
			}

			// Arrive tangent for current key
			float ArriveTangent = ((1.0f - NormY2) * ValueDelta) / ((1.0f - NormX2) * FrameDelta);
			if (FMath::IsFinite(ArriveTangent))
			{
				CurrentKey.ArriveTangent = ArriveTangent;
			}
		}
	}
}

FString UVmdTranslator::GeneratePayloadKey(const FString& BaseName, const FString& Type) const
{
	return FString::Printf(TEXT("VMD_%s_%s"), *Type, *BaseName);
}

TSharedPtr<FVmdModel> UVmdTranslator::GetOrLoadVmdModel(const FString& FilePath) const
{
	// Check cache first
	if (TSharedPtr<FVmdModel>* CachedModel = VmdPayloadCache.Find(FilePath))
	{
		return *CachedModel;
	}

	// Load from file
	TSharedPtr<FVmdModel> NewModel = MakeShared<FVmdModel>();
	FVmdReader Reader;

	if (!Reader.LoadFromFile(FilePath, *NewModel))
	{
		UE_LOG(LogPMXImporter, Error, TEXT("VMD Translator: Failed to load '%s': %s"),
			*FilePath, *Reader.GetLastError());
		return nullptr;
	}

	// Cache for future use
	VmdPayloadCache.Add(FilePath, NewModel);

	return NewModel;
}
