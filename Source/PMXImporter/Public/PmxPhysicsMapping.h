// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#pragma once

#include "CoreMinimal.h"

class UInterchangeBaseNodeContainer;
struct FPmxModel;
class UPhysicsAsset;
class USkeletalMesh;

namespace PmxPhysics
{
	struct FAnnotateResult
	{
		int32 RigidBodyCount = 0;
		int32 JointCount = 0;
	};

	struct FCreateResult
	{
		int32 BodiesCreated = 0;
		int32 ConstraintsCreated = 0;
		bool bSuccess = false;
	};

	// Create physics-related scene nodes under /PMX/Physics and attach custom attributes so that
	// pipelines or later tooling can create Chaos bodies/constraints accordingly.
	// - Applies the same PMX->UE transform policy as geometry (Rotate X +90deg, uniform scale from CVar PMXImporter.Scale).
	// - Clamps extreme values for stability.
	// - Returns counts for summary logging.
	FAnnotateResult AnnotatePhysicsNodes(const FPmxModel& Model, UInterchangeBaseNodeContainer& Container);

	// Create actual PhysicsAsset bodies and constraints from PMX RigidBody/Joint data.
	// This replaces/supplements the auto-generated bodies with PMX-specific physics shapes and constraints.
	// - Transforms PMX coordinates to UE space (X+90deg rotation, scale)
	// - Maps PMX shapes (0=Sphere, 1=Box, 2=Capsule) to UE primitives
	// - Creates constraints from PMX joints with proper limits and spring settings
	// - Returns creation statistics for logging
	FCreateResult CreatePhysicsAssetFromPMX(const FPmxModel& Model, UPhysicsAsset* PhysicsAsset, USkeletalMesh* SkeletalMesh);
}
