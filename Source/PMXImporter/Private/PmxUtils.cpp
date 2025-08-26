// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#include "PmxUtils.h"

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