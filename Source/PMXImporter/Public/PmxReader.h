// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#pragma once

#include "CoreMinimal.h"

struct FPmxModel;

/**
 * PMX file reader utility functions
 */
namespace PMXReader
{
	/**
	 * Load PMX model from file
	 * @param FilePath Path to the PMX file
	 * @param OutModel Output model structure
	 * @return true if successful, false otherwise
	 */
	bool LoadPmxFromFile(const FString& FilePath, FPmxModel& OutModel);
	
	/**
	 * Load PMX model from binary data
	 * @param Data Binary PMX data
	 * @param OutModel Output model structure
	 * @return true if successful, false otherwise
	 */
	bool LoadPmxFromData(const TArray<uint8>& Data, FPmxModel& OutModel);
}