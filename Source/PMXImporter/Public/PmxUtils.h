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
	 *
	 * NOTE: This function converts ALL Unicode characters to replacement char.
	 * Only use for internal system identifiers (UIDs, keys) where ASCII-only is acceptable.
	 * For user-visible names, use SanitizePackagePath() instead to preserve Unicode.
	 */
	static FString SanitizeAsciiToken(const FString& In, const TCHAR Replacement = TCHAR('_'));

	/**
	 * Sanitize package/asset path for AssetRegistry compatibility
	 * Removes problematic characters while preserving Unicode (Japanese, Chinese, Korean, etc.)
	 *
	 * Replaces: spaces, brackets [], quotes '", slashes /\, colons :, and other system-reserved chars
	 * Preserves: Unicode characters, alphanumeric, underscore, hyphen, dot
	 *
	 * Use this for folder names, asset names, and any path components that will be used in UE package paths
	 *
	 * @param InPath The path component to sanitize (folder name, file name, etc.)
	 * @param Replacement Character to use for replacing invalid characters (default: '_')
	 * @return Sanitized path component safe for AssetRegistry
	 *
	 * Examples:
	 *   "Suit Top [JustLluiji]" -> "Suit_Top__JustLluiji_"
	 *   "初音ミク v2.1" -> "初音ミク_v2.1"
	 *   "汉字/文件夹" -> "汉字_文件夹"
	 */
	static FString SanitizePackagePath(const FString& InPath, const TCHAR Replacement = TCHAR('_'));

	/**
	 * Build unique bone names array for a PMX model
	 * Ensures no duplicate names by appending _1, _2, etc. to duplicates
	 *
	 * IMPORTANT: Use this function in both PmxNodeBuilder and PmxTranslator to ensure
	 * bone names are consistent between Interchange nodes and JointNames array.
	 *
	 * @param Model The PMX model
	 * @return Array of unique bone names (preserves original order, matches bone indices)
	 *
	 * Examples:
	 *   Input bones: ["左腕", "右腕", "左腕", "センター"]
	 *   Output: ["左腕", "右腕", "左腕_1", "センター"]
	 */
	static TArray<FString> BuildUniqueBoneNames(const FPmxModel& Model);
};