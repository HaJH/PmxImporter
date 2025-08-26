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
	 */
	static void CreateMaterials(
		const FPmxModel& PmxModel,
		const TMap<int32, FString>& TextureUidMap,
		UInterchangeBaseNodeContainer& BaseNodeContainer,
		TArray<FString>& OutMaterialUids,
		TArray<FString>& OutSlotNames
	);

	// Helper functions moved to FPmxUtils class
};