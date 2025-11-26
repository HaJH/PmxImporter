// Copyright (c) 2024 PMXImporter. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VmdStructs.h"

/**
 * Manages IK enable/disable states across VMD animation frames
 *
 * VMD files contain PropertyKeyframes that specify when IK bones are
 * enabled or disabled. This class interpolates those states for any
 * given frame during IK baking.
 */
class PMXIMPORTER_API FVmdIKStateManager
{
public:
	FVmdIKStateManager();

	/**
	 * Initialize from VMD property keyframes
	 * @param PropertyKeyframes Array of property keyframes from VMD
	 */
	void Initialize(const TArray<FVmdPropertyKeyframe>& PropertyKeyframes);

	/**
	 * Get IK states at a specific frame
	 * States are interpolated (actually step interpolation - IK is on or off)
	 * @param FrameNumber Target frame number
	 * @return Map of IK bone name -> enabled state
	 */
	TMap<FName, bool> GetIKStatesAtFrame(uint32 FrameNumber) const;

	/**
	 * Check if a specific IK is enabled at a frame
	 * @param IKName Name of the IK bone
	 * @param FrameNumber Target frame number
	 * @return True if IK is enabled (default true if no data)
	 */
	bool IsIKEnabled(const FName& IKName, uint32 FrameNumber) const;

	/**
	 * Get all IK names that have keyframes
	 */
	TArray<FName> GetAllIKNames() const;

	/**
	 * Get frame ranges where IK is disabled for a specific bone
	 * @param IKName Name of the IK bone
	 * @return Array of (start, end) frame pairs where IK is disabled
	 */
	TArray<TPair<uint32, uint32>> GetIKDisabledRanges(const FName& IKName) const;

	/** Get total number of property keyframes */
	int32 GetNumKeyframes() const { return SortedKeyframes.Num(); }

	/** Check if manager has any IK state data */
	bool HasData() const { return SortedKeyframes.Num() > 0; }

private:
	/** Internal keyframe data structure */
	struct FIKKeyframe
	{
		uint32 FrameNumber;
		TMap<FName, bool> IKStates;
	};

	/** Property keyframes sorted by frame number */
	TArray<FIKKeyframe> SortedKeyframes;

	/** All IK names that appear in keyframes */
	TSet<FName> AllIKNames;

	/** Default IK state (true = enabled) */
	TMap<FName, bool> DefaultStates;

	/**
	 * Find the keyframe index at or before the given frame
	 * @return Index of keyframe, or -1 if none exists before this frame
	 */
	int32 FindKeyframeIndexAtOrBefore(uint32 FrameNumber) const;
};
