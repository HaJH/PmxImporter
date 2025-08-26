// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#pragma once

#include "CoreMinimal.h"
#include "PmxStructs.h"

/**
 * PMX Utility Functions - Static helper functions used across PMX importer
 * Separated for better code organization and reusability
 */
class PMXIMPORTER_API FPmxUtils
{
public:
	/**
	 * Sanitize morph names for asset-safe ASCII tokens
	 */
	static FString SanitizeMorphName(const FString& InName, int32 FallbackIndex);

	/**
	 * Build a unique sanitized morph name for a given morph index (ASCII-safe, for UIDs)
	 */
	static FString BuildUniqueSanitizedMorphName(const FPmxModel& Model, int32 TargetMorphIndex);

	/**
	 * Build a unique RAW (Unicode) morph name for display/payload
	 */
	static FString BuildUniqueRawMorphName(const FPmxModel& Model, int32 TargetMorphIndex);

	/**
	 * Sanitize to ASCII token for safe node UIDs (keeps a-zA-Z0-9 . _ -; others replaced with '_')
	 */
	static FString SanitizeAsciiToken(const FString& In, const TCHAR Replacement = TCHAR('_'));
};