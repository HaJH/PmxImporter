// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved.

#include "PmxUtils.h"
#include "LogPMXImporter.h"

FString FPmxUtils::SanitizeMorphName(const FString& InName, int32 FallbackIndex)
{
	FString Out;
	Out.Reserve(InName.Len());
	for (TCHAR C : InName)
	{
		if ((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == TCHAR('.') || C == TCHAR('_') || C == TCHAR('-'))
		{
			Out.AppendChar(C);
		}
		else
		{
			Out.AppendChar(TCHAR('_'));
		}
	}
	if (Out.IsEmpty())
	{
		Out = FString::Printf(TEXT("Morph_%d"), FallbackIndex);
	}
	return Out;
}

FString FPmxUtils::BuildUniqueSanitizedMorphName(const FPmxModel& Model, int32 TargetMorphIndex)
{
	TMap<FString, int32> NameUseCount;
	FString ResultName;
	for (int32 MIdx = 0; MIdx <= TargetMorphIndex && MIdx < Model.Morphs.Num(); ++MIdx)
	{
		const FPmxMorph& M = Model.Morphs[MIdx];
		const FString Base = SanitizeMorphName(M.Name.IsEmpty() ? FString::Printf(TEXT("Morph_%d"), MIdx) : M.Name, MIdx);
		int32& Count = NameUseCount.FindOrAdd(Base);
		FString Final = Base;
		if (Count > 0)
		{
			Final = FString::Printf(TEXT("%s_m%d"), *Base, Count);
		}
		if (MIdx == TargetMorphIndex)
		{
			ResultName = Final;
		}
		++Count;
	}
	return ResultName.IsEmpty() ? FString::Printf(TEXT("Morph_%d"), TargetMorphIndex) : ResultName;
}

FString FPmxUtils::BuildUniqueRawMorphName(const FPmxModel& Model, int32 TargetMorphIndex)
{
	TMap<FString, int32> NameUseCount;
	FString ResultName;
	for (int32 MIdx = 0; MIdx <= TargetMorphIndex && MIdx < Model.Morphs.Num(); ++MIdx)
	{
		const FPmxMorph& M = Model.Morphs[MIdx];
		FString Base = M.Name;
		Base.TrimStartAndEndInline();
		if (Base.IsEmpty())
		{
			Base = FString::Printf(TEXT("Morph_%d"), MIdx);
		}
		int32& Count = NameUseCount.FindOrAdd(Base);
		FString Final = Base;
		if (Count > 0)
		{
			Final = FString::Printf(TEXT("%s_m%d"), *Base, Count);
		}
		if (MIdx == TargetMorphIndex)
		{
			ResultName = Final;
		}
		++Count;
	}
	return ResultName.IsEmpty() ? FString::Printf(TEXT("Morph_%d"), TargetMorphIndex) : ResultName;
}

FString FPmxUtils::SanitizeAsciiToken(const FString& In, const TCHAR Replacement)
{
	FString Out;
	Out.Reserve(In.Len());
	for (TCHAR C : In)
	{
		if ((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == TCHAR('.') || C == TCHAR('_') || C == TCHAR('-'))
		{
			Out.AppendChar(C);
		}
		else
		{
			Out.AppendChar(Replacement);
		}
	}
	if (Out.IsEmpty())
	{
		Out = TEXT("Mat");
	}
	return Out;
}

FString FPmxUtils::SanitizePackagePath(const FString& InPath, const TCHAR Replacement)
{
	// AssetRegistry does not allow the following characters in package paths:
	// - Spaces
	// - Brackets []
	// - Quotes ' "
	// - Slashes / \
	// - Colons :
	// - Other system-reserved characters
	//
	// IMPORTANT: Unicode characters (Japanese, Chinese, Korean, etc.) are ALLOWED
	// We only remove the specific problematic characters listed above

	FString Out;
	Out.Reserve(InPath.Len());

	for (TCHAR C : InPath)
	{
		// Blacklist approach: only replace known problematic characters
		// Keep all other characters including Unicode (Japanese, Chinese, Korean, etc.)
		bool bIsProblematic = false;

		// Check against known problematic characters
		switch (C)
		{
			case TCHAR(' '):   // Space
			case TCHAR('['):   // Left bracket
			case TCHAR(']'):   // Right bracket
			case TCHAR('\''):  // Single quote
			case TCHAR('"'):   // Double quote
			case TCHAR('/'):   // Forward slash
			case TCHAR('\\'):  // Backslash
			case TCHAR(':'):   // Colon
			case TCHAR('*'):   // Asterisk
			case TCHAR('?'):   // Question mark
			case TCHAR('<'):   // Less than
			case TCHAR('>'):   // Greater than
			case TCHAR('|'):   // Pipe
				bIsProblematic = true;
				break;
			default:
				bIsProblematic = false;
				break;
		}

		if (bIsProblematic)
		{
			Out.AppendChar(Replacement);
		}
		else
		{
			Out.AppendChar(C);
		}
	}

	// Trim leading/trailing replacement characters and whitespace
	Out.TrimStartAndEndInline();

	// If empty after sanitization, provide a fallback
	if (Out.IsEmpty())
	{
		Out = TEXT("Asset");
	}

	return Out;
}

TArray<FString> FPmxUtils::BuildUniqueBoneNames(const FPmxModel& Model)
{
	TArray<FString> UniqueBoneNames;
	UniqueBoneNames.Reserve(Model.Bones.Num());

	// Track how many times each name has been used
	TMap<FString, int32> NameUseCount;
	int32 DuplicateCount = 0;

	// First pass: Log all bone names to identify patterns
	TMap<FString, TArray<int32>> DuplicateMap;
	for (int32 BoneIndex = 0; BoneIndex < Model.Bones.Num(); ++BoneIndex)
	{
		const FPmxBone& Bone = Model.Bones[BoneIndex];
		FString BaseName = Bone.Name;
		BaseName.TrimStartAndEndInline();

		if (BaseName.IsEmpty())
		{
			BaseName = FString::Printf(TEXT("Bone_%d"), BoneIndex);
		}

		DuplicateMap.FindOrAdd(BaseName).Add(BoneIndex);
	}

	// Count duplicates
	int32 TotalDuplicates = 0;
	for (const auto& Pair : DuplicateMap)
	{
		if (Pair.Value.Num() > 1)
		{
			TotalDuplicates += (Pair.Value.Num() - 1);  // First occurrence doesn't count as duplicate
		}
	}

	// Second pass: Generate unique names
	for (int32 BoneIndex = 0; BoneIndex < Model.Bones.Num(); ++BoneIndex)
	{
		const FPmxBone& Bone = Model.Bones[BoneIndex];
		FString BaseName = Bone.Name;
		BaseName.TrimStartAndEndInline();

		if (BaseName.IsEmpty())
		{
			BaseName = FString::Printf(TEXT("Bone_%d"), BoneIndex);
		}

		// Check if this name has been used before
		int32& UseCount = NameUseCount.FindOrAdd(BaseName, 0);

		FString UniqueName = BaseName;
		if (UseCount > 0)
		{
			// Name already used, append counter
			UniqueName = FString::Printf(TEXT("%s_%d"), *BaseName, UseCount);
			++DuplicateCount;
		}

		// Increment use count for next occurrence
		++UseCount;

		UniqueBoneNames.Add(UniqueName);
	}

	if (TotalDuplicates > 0)
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("FPmxUtils::BuildUniqueBoneNames: Found %d duplicate bone names in total, %d bones renamed"),
			TotalDuplicates, DuplicateCount);
	}

	return UniqueBoneNames;
}