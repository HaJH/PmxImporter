// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#pragma once

#include "CoreMinimal.h"
#include "PmxStructs.h"

class UInterchangeBaseNodeContainer;

/**
 * PMX Material Mapping - Handles creation of material instances with PMX material properties
 * Separated from main translator for better maintainability
 */
class PMXIMPORTER_API FPmxMaterialMapping
{
public:
	/**
	 * Create material instance nodes for each PMX material
	 * @param PmxModel - The PMX model data
	 * @param TextureUidMap - Map of texture indices to their UIDs
	 * @param BaseNodeContainer - The node container to add material nodes to
	 * @param OutMaterialUids - Output array of material node UIDs
	 * @param OutSlotNames - Output array of material slot names
	 * @param InParentMaterialPath - Optional parent material path (from Pipeline). If empty, uses CVar default.
	 */
	static void CreateMaterials(
		const FPmxModel& PmxModel,
		const TMap<int32, FString>& TextureUidMap,
		UInterchangeBaseNodeContainer& BaseNodeContainer,
		TArray<FString>& OutMaterialUids,
		TArray<FString>& OutSlotNames,
		const FString& InParentMaterialPath = FString()
	);

	// Helper functions moved to FPmxUtils class
};