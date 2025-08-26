// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#pragma once

#include "CoreMinimal.h"
#include "PmxStructs.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSceneNode;

/**
 * PMX Node Builder - Handles creation of scene nodes and bone hierarchy
 * Separated from main translator for better maintainability
 */
class PMXIMPORTER_API FPmxNodeBuilder
{
public:
	/**
	 * Create scene root node for PMX model
	 */
	static UInterchangeSceneNode* CreateSceneRoot(
		const FPmxModel& PmxModel,
		UInterchangeBaseNodeContainer& BaseNodeContainer
	);

	/**
	 * Create bone hierarchy using SceneNodes specialized as Joints
	 */
	static FString CreateBoneHierarchy(
		const FPmxModel& PmxModel,
		UInterchangeBaseNodeContainer& BaseNodeContainer,
		UInterchangeSceneNode* RootNode,
		TMap<int32, UInterchangeSceneNode*>& OutBoneIndexToJointNode
	);
};