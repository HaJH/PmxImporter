// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * VMD (Vocaloid Motion Data) file format structures.
 * VMD is the animation format used by MikuMikuDance (MMD).
 *
 * Coordinate System:
 * - VMD: Right-handed Y-up
 * - UE: Left-handed Z-up
 *
 * Frame Rate: 30 FPS
 */

// VMD file signature
#define VMD_SIGNATURE_V1 "Vocaloid Motion Data file"
#define VMD_SIGNATURE_V2 "Vocaloid Motion Data 0002"

/**
 * VMD file header
 */
struct FVmdHeader
{
	/** File signature (30 bytes) - determines version */
	FString Signature;

	/** Target model name (10 bytes for v1, 20 bytes for v2) */
	FString ModelName;

	/** Whether this is version 2 format */
	bool bIsVersion2 = true;

	FVmdHeader() = default;
};

/**
 * Bone keyframe data (111 bytes in file)
 *
 * Interpolation data layout (64 bytes):
 * - Bytes 0-15: X-axis interpolation (repeated 4 times)
 * - Bytes 16-31: Y-axis interpolation
 * - Bytes 32-47: Z-axis interpolation
 * - Bytes 48-63: Rotation interpolation
 *
 * Each axis uses 4 bytes: [x1, y1, x2, y2] for bezier control points
 * Values range 0-127, normalized to 0.0-1.0
 */
struct FVmdBoneKeyframe
{
	/** Bone name (15 bytes, Shift_JIS encoded) */
	FString BoneName;

	/** Frame number (0-based, 30 FPS) */
	uint32 FrameNumber = 0;

	/** Position offset relative to bind pose (X, Y, Z) */
	FVector Position = FVector::ZeroVector;

	/** Rotation as quaternion (X, Y, Z, W) */
	FQuat Rotation = FQuat::Identity;

	/** Bezier interpolation parameters (64 bytes) */
	TArray<uint8> Interpolation;

	FVmdBoneKeyframe()
	{
		Interpolation.SetNum(64);
	}

	/** Get interpolation parameters for a specific axis (0=X, 1=Y, 2=Z, 3=Rotation) */
	void GetInterpolationParams(int32 Axis, uint8& OutX1, uint8& OutY1, uint8& OutX2, uint8& OutY2) const
	{
		if (Axis >= 0 && Axis < 4 && Interpolation.Num() >= 64)
		{
			int32 Offset = Axis * 16;
			OutX1 = Interpolation[Offset + 0];
			OutY1 = Interpolation[Offset + 4];
			OutX2 = Interpolation[Offset + 8];
			OutY2 = Interpolation[Offset + 12];
		}
		else
		{
			// Linear interpolation fallback
			OutX1 = 20; OutY1 = 20; OutX2 = 107; OutY2 = 107;
		}
	}

	/** Check if interpolation is linear for an axis */
	bool IsLinearInterpolation(int32 Axis) const
	{
		uint8 X1, Y1, X2, Y2;
		GetInterpolationParams(Axis, X1, Y1, X2, Y2);
		return (X1 == Y1) && (X2 == Y2);
	}
};

/**
 * Morph (facial expression) keyframe data (23 bytes in file)
 */
struct FVmdMorphKeyframe
{
	/** Morph name (15 bytes, Shift_JIS encoded) */
	FString MorphName;

	/** Frame number (0-based, 30 FPS) */
	uint32 FrameNumber = 0;

	/** Morph weight (typically 0.0-1.0, can exceed) */
	float Weight = 0.0f;

	FVmdMorphKeyframe() = default;
};

/**
 * Camera keyframe data (61 bytes in file)
 *
 * Camera model: Target-centered orbit camera
 * - Position is the target/look-at point
 * - Distance is negative (camera behind target)
 * - Rotation is applied to determine camera direction
 *
 * Interpolation data layout (24 bytes):
 * - Bytes 0-3: X position
 * - Bytes 4-7: Y position
 * - Bytes 8-11: Z position
 * - Bytes 12-15: Rotation
 * - Bytes 16-19: Distance
 * - Bytes 20-23: FOV
 */
struct FVmdCameraKeyframe
{
	/** Frame number (0-based, 30 FPS) */
	uint32 FrameNumber = 0;

	/** Distance from target to camera (negative value) */
	float Distance = 0.0f;

	/** Target/look-at position (X, Y, Z) */
	FVector TargetPosition = FVector::ZeroVector;

	/** Rotation in radians (Pitch, Yaw, Roll) - Euler angles */
	FVector Rotation = FVector::ZeroVector;

	/** Bezier interpolation parameters (24 bytes) */
	TArray<uint8> Interpolation;

	/** Field of view angle in degrees */
	uint32 FOV = 30;

	/** Perspective mode (true=perspective, false=orthographic) */
	bool bPerspective = true;

	FVmdCameraKeyframe()
	{
		Interpolation.SetNum(24);
	}

	/** Get interpolation parameters for a specific property */
	void GetInterpolationParams(int32 PropertyIndex, uint8& OutX1, uint8& OutY1, uint8& OutX2, uint8& OutY2) const
	{
		// Interpolation layout: [x1,x2,y1,y2] per property (different from bone!)
		if (PropertyIndex >= 0 && PropertyIndex < 6 && Interpolation.Num() >= 24)
		{
			int32 Offset = PropertyIndex * 4;
			OutX1 = Interpolation[Offset + 0];
			OutX2 = Interpolation[Offset + 1];
			OutY1 = Interpolation[Offset + 2];
			OutY2 = Interpolation[Offset + 3];
		}
		else
		{
			OutX1 = 20; OutY1 = 20; OutX2 = 107; OutY2 = 107;
		}
	}
};

/**
 * Light keyframe data (28 bytes in file)
 */
struct FVmdLightKeyframe
{
	/** Frame number */
	uint32 FrameNumber = 0;

	/** Light color (R, G, B) - range 0.0-1.0 */
	FVector Color = FVector(1.0f, 1.0f, 1.0f);

	/** Light direction (X, Y, Z) - normalized */
	FVector Direction = FVector(0.0f, -1.0f, 0.0f);

	FVmdLightKeyframe() = default;
};

/**
 * Self-shadow keyframe data (9 bytes in file)
 */
struct FVmdSelfShadowKeyframe
{
	/** Frame number */
	uint32 FrameNumber = 0;

	/** Shadow mode (0=none, 1=mode1, 2=mode2) */
	uint8 Mode = 0;

	/** Shadow distance */
	float Distance = 0.0f;

	FVmdSelfShadowKeyframe() = default;
};

/**
 * IK state for property keyframe
 */
struct FVmdIKState
{
	/** IK bone name */
	FString IKName;

	/** IK enabled state */
	bool bEnabled = true;

	FVmdIKState() = default;
};

/**
 * Property (visibility/IK) keyframe data
 */
struct FVmdPropertyKeyframe
{
	/** Frame number */
	uint32 FrameNumber = 0;

	/** Model visibility */
	bool bVisible = true;

	/** IK states for this frame */
	TArray<FVmdIKState> IKStates;

	FVmdPropertyKeyframe() = default;
};

/**
 * Complete VMD model containing all animation data
 */
struct FVmdModel
{
	/** File header */
	FVmdHeader Header;

	/** Bone animation keyframes */
	TArray<FVmdBoneKeyframe> BoneKeyframes;

	/** Morph animation keyframes */
	TArray<FVmdMorphKeyframe> MorphKeyframes;

	/** Camera animation keyframes */
	TArray<FVmdCameraKeyframe> CameraKeyframes;

	/** Light animation keyframes */
	TArray<FVmdLightKeyframe> LightKeyframes;

	/** Self-shadow keyframes */
	TArray<FVmdSelfShadowKeyframe> SelfShadowKeyframes;

	/** Property (visibility/IK) keyframes */
	TArray<FVmdPropertyKeyframe> PropertyKeyframes;

	FVmdModel() = default;

	/** Check if this VMD contains bone/morph animation (model motion) */
	bool HasModelAnimation() const
	{
		return BoneKeyframes.Num() > 0 || MorphKeyframes.Num() > 0;
	}

	/** Check if this VMD contains camera animation */
	bool HasCameraAnimation() const
	{
		return CameraKeyframes.Num() > 0;
	}

	/** Get total frame count (max frame number across all keyframes) */
	uint32 GetTotalFrames() const
	{
		uint32 MaxFrame = 0;

		for (const auto& Key : BoneKeyframes)
		{
			MaxFrame = FMath::Max(MaxFrame, Key.FrameNumber);
		}
		for (const auto& Key : MorphKeyframes)
		{
			MaxFrame = FMath::Max(MaxFrame, Key.FrameNumber);
		}
		for (const auto& Key : CameraKeyframes)
		{
			MaxFrame = FMath::Max(MaxFrame, Key.FrameNumber);
		}

		return MaxFrame;
	}

	/** Get animation duration in seconds (30 FPS) */
	float GetDurationSeconds() const
	{
		return GetTotalFrames() / 30.0f;
	}

	/** Get all unique bone names */
	TSet<FString> GetUniqueBoneNames() const
	{
		TSet<FString> Names;
		for (const auto& Key : BoneKeyframes)
		{
			Names.Add(Key.BoneName);
		}
		return Names;
	}

	/** Get all unique morph names */
	TSet<FString> GetUniqueMorphNames() const
	{
		TSet<FString> Names;
		for (const auto& Key : MorphKeyframes)
		{
			Names.Add(Key.MorphName);
		}
		return Names;
	}

	/** Get keyframes for a specific bone, sorted by frame number */
	TArray<FVmdBoneKeyframe> GetBoneKeyframes(const FString& BoneName) const
	{
		TArray<FVmdBoneKeyframe> Result;
		for (const auto& Key : BoneKeyframes)
		{
			if (Key.BoneName == BoneName)
			{
				Result.Add(Key);
			}
		}
		Result.Sort([](const FVmdBoneKeyframe& A, const FVmdBoneKeyframe& B)
		{
			return A.FrameNumber < B.FrameNumber;
		});
		return Result;
	}

	/** Get keyframes for a specific morph, sorted by frame number */
	TArray<FVmdMorphKeyframe> GetMorphKeyframes(const FString& MorphName) const
	{
		TArray<FVmdMorphKeyframe> Result;
		for (const auto& Key : MorphKeyframes)
		{
			if (Key.MorphName == MorphName)
			{
				Result.Add(Key);
			}
		}
		Result.Sort([](const FVmdMorphKeyframe& A, const FVmdMorphKeyframe& B)
		{
			return A.FrameNumber < B.FrameNumber;
		});
		return Result;
	}

	/** Clear all data */
	void Reset()
	{
		Header = FVmdHeader();
		BoneKeyframes.Empty();
		MorphKeyframes.Empty();
		CameraKeyframes.Empty();
		LightKeyframes.Empty();
		SelfShadowKeyframes.Empty();
		PropertyKeyframes.Empty();
	}
};
