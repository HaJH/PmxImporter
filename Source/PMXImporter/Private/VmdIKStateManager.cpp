// Copyright (c) 2024 PMXImporter. All Rights Reserved.

#include "VmdIKStateManager.h"
#include "LogPMXImporter.h"

FVmdIKStateManager::FVmdIKStateManager()
{
}

void FVmdIKStateManager::Initialize(const TArray<FVmdPropertyKeyframe>& PropertyKeyframes)
{
	SortedKeyframes.Empty();
	AllIKNames.Empty();
	DefaultStates.Empty();

	if (PropertyKeyframes.Num() == 0)
	{
		UE_LOG(LogPMXImporter, Verbose, TEXT("FVmdIKStateManager: No property keyframes provided"));
		return;
	}

	// Convert and collect all IK names
	for (const FVmdPropertyKeyframe& KeyFrame : PropertyKeyframes)
	{
		FIKKeyframe InternalKeyframe;
		InternalKeyframe.FrameNumber = KeyFrame.FrameNumber;

		for (const FVmdIKState& IKState : KeyFrame.IKStates)
		{
			FName IKName(*IKState.IKName);
			InternalKeyframe.IKStates.Add(IKName, IKState.bEnabled);
			AllIKNames.Add(IKName);
		}

		SortedKeyframes.Add(InternalKeyframe);
	}

	// Sort by frame number
	SortedKeyframes.Sort([](const FIKKeyframe& A, const FIKKeyframe& B)
	{
		return A.FrameNumber < B.FrameNumber;
	});

	// Set default states from first keyframe (or assume enabled if not specified)
	for (const FName& IKName : AllIKNames)
	{
		// Default to enabled
		DefaultStates.Add(IKName, true);
	}

	// Update defaults from first keyframe if available
	if (SortedKeyframes.Num() > 0)
	{
		for (const auto& Pair : SortedKeyframes[0].IKStates)
		{
			DefaultStates.Add(Pair.Key, Pair.Value);
		}
	}

	UE_LOG(LogPMXImporter, Display, TEXT("FVmdIKStateManager: Initialized with %d keyframes, %d IK bones"),
		SortedKeyframes.Num(), AllIKNames.Num());

	// Log IK names for debugging
	for (const FName& IKName : AllIKNames)
	{
		UE_LOG(LogPMXImporter, Verbose, TEXT("  - IK: %s"), *IKName.ToString());
	}
}

TMap<FName, bool> FVmdIKStateManager::GetIKStatesAtFrame(uint32 FrameNumber) const
{
	TMap<FName, bool> Result;

	// Start with defaults
	Result = DefaultStates;

	if (SortedKeyframes.Num() == 0)
	{
		return Result;
	}

	// Find the keyframe at or before this frame
	int32 KeyframeIndex = FindKeyframeIndexAtOrBefore(FrameNumber);

	if (KeyframeIndex >= 0)
	{
		// Override with keyframe values
		for (const auto& Pair : SortedKeyframes[KeyframeIndex].IKStates)
		{
			Result.Add(Pair.Key, Pair.Value);
		}
	}

	return Result;
}

bool FVmdIKStateManager::IsIKEnabled(const FName& IKName, uint32 FrameNumber) const
{
	// Check if this IK exists in our data
	if (!AllIKNames.Contains(IKName))
	{
		// Unknown IK, assume enabled
		return true;
	}

	// Find applicable keyframe
	int32 KeyframeIndex = FindKeyframeIndexAtOrBefore(FrameNumber);

	if (KeyframeIndex >= 0)
	{
		const bool* State = SortedKeyframes[KeyframeIndex].IKStates.Find(IKName);
		if (State)
		{
			return *State;
		}
	}

	// Fall back to default
	const bool* DefaultState = DefaultStates.Find(IKName);
	return DefaultState ? *DefaultState : true;
}

TArray<FName> FVmdIKStateManager::GetAllIKNames() const
{
	return AllIKNames.Array();
}

TArray<TPair<uint32, uint32>> FVmdIKStateManager::GetIKDisabledRanges(const FName& IKName) const
{
	TArray<TPair<uint32, uint32>> Ranges;

	if (SortedKeyframes.Num() == 0)
	{
		return Ranges;
	}

	// Track disabled ranges
	bool bCurrentlyDisabled = false;
	uint32 DisabledStartFrame = 0;

	// Check initial state
	const bool* InitialState = DefaultStates.Find(IKName);
	if (InitialState && !(*InitialState))
	{
		bCurrentlyDisabled = true;
		DisabledStartFrame = 0;
	}

	// Walk through keyframes
	for (int32 i = 0; i < SortedKeyframes.Num(); ++i)
	{
		const FIKKeyframe& Keyframe = SortedKeyframes[i];
		const bool* State = Keyframe.IKStates.Find(IKName);

		if (State)
		{
			if (*State && bCurrentlyDisabled)
			{
				// IK was disabled, now enabled - close the range
				Ranges.Add(TPair<uint32, uint32>(DisabledStartFrame, Keyframe.FrameNumber - 1));
				bCurrentlyDisabled = false;
			}
			else if (!(*State) && !bCurrentlyDisabled)
			{
				// IK was enabled, now disabled - start a new range
				DisabledStartFrame = Keyframe.FrameNumber;
				bCurrentlyDisabled = true;
			}
		}
	}

	// If still disabled at the end, close with max frame
	if (bCurrentlyDisabled)
	{
		Ranges.Add(TPair<uint32, uint32>(DisabledStartFrame, MAX_uint32));
	}

	return Ranges;
}

int32 FVmdIKStateManager::FindKeyframeIndexAtOrBefore(uint32 FrameNumber) const
{
	if (SortedKeyframes.Num() == 0)
	{
		return -1;
	}

	// Binary search for the largest keyframe <= FrameNumber
	int32 Low = 0;
	int32 High = SortedKeyframes.Num() - 1;
	int32 Result = -1;

	while (Low <= High)
	{
		int32 Mid = (Low + High) / 2;

		if (SortedKeyframes[Mid].FrameNumber <= FrameNumber)
		{
			Result = Mid;
			Low = Mid + 1;
		}
		else
		{
			High = Mid - 1;
		}
	}

	return Result;
}
