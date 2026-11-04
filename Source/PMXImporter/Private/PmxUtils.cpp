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

TArray<FString> FPmxUtils::BuildUniqueBoneNames(const TArray<FPmxBone>& Bones, bool bRenameLR)
{
	TArray<FString> UniqueBoneNames;
	UniqueBoneNames.Reserve(Bones.Num());

	const auto GetBaseName = [&Bones, bRenameLR](int32 BoneIndex)
	{
		FString BaseName = Bones[BoneIndex].Name;
		BaseName.TrimStartAndEndInline();

		if (BaseName.IsEmpty())
		{
			BaseName = FString::Printf(TEXT("Bone_%d"), BoneIndex);
		}
		else if (bRenameLR && BaseName.StartsWith(TEXT("左")))
		{
			BaseName = BaseName.RightChop(1) + TEXT("_L");
		}
		else if (bRenameLR && BaseName.StartsWith(TEXT("右")))
		{
			BaseName = BaseName.RightChop(1) + TEXT("_R");
		}
		return BaseName;
	};

	// Track how many times each name has been used
	TMap<FString, int32> NameUseCount;
	int32 DuplicateCount = 0;

	// First pass: Log all bone names to identify patterns
	TMap<FString, TArray<int32>> DuplicateMap;
	for (int32 BoneIndex = 0; BoneIndex < Bones.Num(); ++BoneIndex)
	{
		DuplicateMap.FindOrAdd(GetBaseName(BoneIndex)).Add(BoneIndex);
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
	for (int32 BoneIndex = 0; BoneIndex < Bones.Num(); ++BoneIndex)
	{
		const FString BaseName = GetBaseName(BoneIndex);

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

TArray<FQuat> FPmxUtils::ComputeBoneWorldRotations(const TArray<FPmxBone>& Bones)
{
	// Bones are oriented in Blender's armature space following mmd_tools, then converted to the
	// export convention of our Blender FBXs (primary bone axis +X, secondary bone axis -Z).
	// Blender space is PMX with Y and Z swapped, and matches our UE space with Y negated.
	const auto PmxToBlender = [](const FVector3f& V)
	{
		return V.ContainsNaN() ? FVector::ZeroVector : FVector(V.X, V.Z, V.Y);
	};
	const auto BlenderToUE = [](const FVector& V)
	{
		return FVector(V.X, -V.Y, V.Z);
	};

	// mmd_tools imports at 0.08 scale and treats bones shorter than 0.001 as having no tail
	const double MinBoneLengthSquared = FMath::Square(0.001 / 0.08);
	const auto IsBoneVector = [MinBoneLengthSquared](const FVector& V)
	{
		return V.SizeSquared() >= MinBoneLengthSquared;
	};

	const int32 NumBones = Bones.Num();

	// Bone vector from head to display tail: either another bone's head or an offset
	TArray<FVector> BoneVectors;
	BoneVectors.Init(FVector::ZeroVector, NumBones);
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FPmxBone& Bone = Bones[BoneIndex];
		if (!(Bone.BoneFlags & 0x0001))
		{
			BoneVectors[BoneIndex] = PmxToBlender(Bone.Offset);
		}
		else if (Bones.IsValidIndex(Bone.ConnectionIndex))
		{
			BoneVectors[BoneIndex] = PmxToBlender(Bones[Bone.ConnectionIndex].Position - Bone.Position);
		}
	}

	// A tailless IK link aims at the next bone in its chain
	for (const FPmxBone& IKBone : Bones)
	{
		if (!(IKBone.BoneFlags & 0x0020))
		{
			continue;
		}
		int32 ChildIndex = IKBone.IKTargetBoneIndex;
		for (const FPmxBone::FPmxIKLink& Link : IKBone.IKLinks)
		{
			if (Bones.IsValidIndex(Link.BoneIndex) && Bones.IsValidIndex(ChildIndex)
				&& !IsBoneVector(BoneVectors[Link.BoneIndex]) && Bones[ChildIndex].ParentBoneIndex == Link.BoneIndex)
			{
				BoneVectors[Link.BoneIndex] = PmxToBlender(Bones[ChildIndex].Position - Bones[Link.BoneIndex].Position);
			}
			ChildIndex = Link.BoneIndex;
		}
	}

	// mmd_tools FnBone.has_auto_local_axis
	static const TCHAR* const AutoLocalAxisBones[] = {
		TEXT("左肩"), TEXT("左腕"), TEXT("左ひじ"), TEXT("左手首"), TEXT("右腕"), TEXT("右肩"), TEXT("右ひじ"), TEXT("右手首"),
		TEXT("左腕捩"), TEXT("左手捩"), TEXT("左肩P"), TEXT("左ダミー"), TEXT("右腕捩"), TEXT("右手捩"), TEXT("右肩P"), TEXT("右ダミー")
	};
	static const TCHAR* const AutoLocalAxisFingers[] = { TEXT("親指"), TEXT("人指"), TEXT("中指"), TEXT("薬指"), TEXT("小指") };
	const auto HasAutoLocalAxis = [](const FString& Name)
	{
		for (const TCHAR* Bone : AutoLocalAxisBones)
		{
			if (Name.Equals(Bone, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		for (const TCHAR* Finger : AutoLocalAxisFingers)
		{
			if (Name.Contains(Finger, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	};

	TArray<FQuat> Rotations;
	Rotations.Reserve(NumBones);
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FPmxBone& Bone = Bones[BoneIndex];

		// Bone Y axis: toward the tail, then the fixed axis, then straight up
		FVector BoneVector = BoneVectors[BoneIndex];
		if (!IsBoneVector(BoneVector))
		{
			// Fixed axis is only meaningful when flagged (the field is otherwise unset)
			const FVector FixedAxis = (Bone.BoneFlags & 0x0400) ? PmxToBlender(Bone.AxisDirection).GetSafeNormal() : FVector::ZeroVector;
			BoneVector = FixedAxis.IsZero() ? FVector::ZAxisVector : FixedAxis;
		}
		const FVector BoneY = BoneVector.GetSafeNormal();

		// Local axes to take the roll from: authored, or derived for arm and finger bones
		FVector LocalX = FVector::ZeroVector;
		FVector LocalZ = FVector::ZeroVector;
		if (Bone.BoneFlags & 0x0800)
		{
			LocalX = PmxToBlender(Bone.XAxisDirection).GetSafeNormal();
			LocalZ = PmxToBlender(Bone.ZAxisDirection).GetSafeNormal();
		}
		else if (HasAutoLocalAxis(Bone.Name))
		{
			// FnBone.update_auto_bone_roll: Z is the normal of a face which tracks Blender -Y
			const FVector2D AlongXZ = FVector2D(BoneY.X, BoneY.Z).GetSafeNormal();
			const FVector InPlane = AlongXZ.IsZero() ? FVector::ZAxisVector : FVector(-AlongXZ.Y, 0.0, AlongXZ.X);
			LocalX = BoneY;
			LocalZ = (BoneY ^ InPlane).GetSafeNormal();
		}
		const FVector LocalY = (LocalZ ^ LocalX).GetSafeNormal();

		FVector BoneX;
		if (!LocalY.IsZero())
		{
			// FnBone.update_bone_roll: bone Z aligns with the axis following (or, when the bone
			// points against it, preceding) the local axis closest to the bone
			const FVector Axes[3] = { LocalX, LocalY, (LocalX ^ LocalY).GetSafeNormal() };
			int32 ClosestAxis = 0;
			for (int32 AxisIndex = 1; AxisIndex < 3; ++AxisIndex)
			{
				if (FMath::Abs(BoneY | Axes[AxisIndex]) > FMath::Abs(BoneY | Axes[ClosestAxis]))
				{
					ClosestAxis = AxisIndex;
				}
			}
			const FVector& RollAxis = Axes[(ClosestAxis + ((BoneY | Axes[ClosestAxis]) < 0.0 ? 2 : 1)) % 3];
			BoneX = (BoneY ^ RollAxis).GetSafeNormal();
		}
		else
		{
			// Blender's default bone orientation at zero roll (vec_roll_to_mat3_normalized)
			const double X = BoneY.X;
			const double Z = BoneY.Z;
			const double XZSquared = X * X + Z * Z;
			const double Theta = 1.0 + BoneY.Y;
			if (Theta > 1.e-5)
			{
				BoneX = FVector(1.0 - X * X / Theta, -X, -X * Z / Theta);
			}
			else if (XZSquared > 2.e-9)
			{
				BoneX = FVector((X + Z) * (X - Z) / -XZSquared, -X, -2.0 * X * Z / XZSquared);
			}
			else
			{
				BoneX = -FVector::XAxisVector;
			}
		}

		// Blender FBX export with primary bone axis +X and secondary bone axis -Z, as imported by UE
		Rotations.Add(FRotationMatrix::MakeFromXZ(BlenderToUE(BoneY), -BlenderToUE(BoneX)).ToQuat().GetNormalized());
	}

	return Rotations;
}