// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPhysicsAsset;
class USkeletalMesh;
class USkeletalBodySetup;
class UPhysicsConstraintTemplate;
struct FPmxPhysicsCache;
struct FPmxRigidBody;
struct FPmxJoint;
struct FPmxBone;
struct FKAggregateGeom;
struct FReferenceSkeleton;

/**
 * Builder class for creating PhysicsAsset from PMX rigid body and joint data.
 *
 * This class handles the conversion of PMX physics data to Unreal Engine's
 * PhysicsAsset format, including coordinate transformation, shape mapping,
 * and constraint setup.
 */
class PMXIMPORTER_API FPmxPhysicsBuilder
{
public:
	/**
	 * Build a complete PhysicsAsset from cached PMX physics data.
	 *
	 * @param PhysicsAsset	The PhysicsAsset to populate (must be valid)
	 * @param SkeletalMesh	The associated SkeletalMesh (must be valid)
	 * @param PhysicsData	The cached PMX physics data
	 * @return True if successful, false otherwise
	 */
	static bool BuildPhysicsAsset(
		UPhysicsAsset* PhysicsAsset,
		USkeletalMesh* SkeletalMesh,
		const FPmxPhysicsCache& PhysicsData
	);

private:
	/**
	 * Create a BodySetup from a PMX RigidBody.
	 *
	 * @param Asset			The parent PhysicsAsset
	 * @param RB			The PMX rigid body data
	 * @param RBIndex		Index of this rigid body in the RigidBodies array
	 * @param Bone			The associated PMX bone
	 * @param RefSkel		The reference skeleton for bone lookup
	 * @param PhysicsData	The full physics cache (for options)
	 * @return The created BodySetup, or nullptr on failure
	 */
	static USkeletalBodySetup* CreateBodySetup(
		UPhysicsAsset* Asset,
		const FPmxRigidBody& RB,
		int32 RBIndex,
		const FPmxBone& Bone,
		const FReferenceSkeleton& RefSkel,
		const FPmxPhysicsCache& PhysicsData
	);

	/**
	 * Create a Constraint from a PMX Joint.
	 *
	 * @param Asset			The parent PhysicsAsset
	 * @param Joint			The PMX joint data
	 * @param RigidBodies	Array of all rigid bodies (for index lookup)
	 * @param Bones			Array of all bones (for bone name lookup)
	 * @param PhysicsData	The full physics cache (for options)
	 * @return The created Constraint, or nullptr on failure
	 */
	static UPhysicsConstraintTemplate* CreateConstraint(
		UPhysicsAsset* Asset,
		const FPmxJoint& Joint,
		const TArray<FPmxRigidBody>& RigidBodies,
		const TArray<FPmxBone>& Bones,
		const FPmxPhysicsCache& PhysicsData
	);

	// Shape creation methods
	static void SetupSphereShape(FKAggregateGeom& Geom, const FPmxRigidBody& RB, float Scale, const FVector& LocalPos);
	static void SetupBoxShape(FKAggregateGeom& Geom, const FPmxRigidBody& RB, float Scale, const FVector& LocalPos, const FRotator& LocalRot);
	static void SetupCapsuleShape(FKAggregateGeom& Geom, const FPmxRigidBody& RB, float Scale, const FVector& LocalPos, const FRotator& LocalRot);

	/**
	 * Convert PMX RigidBody position to bone-local offset.
	 * PMX stores world coordinates, UE BodySetup needs bone-local.
	 */
	static FVector GetBodyLocalPosition(const FPmxRigidBody& RB, const FPmxBone& Bone, float Scale);

	/**
	 * Convert PMX RigidBody rotation to UE rotation.
	 * Handles coordinate system transformation.
	 */
	static FRotator GetBodyLocalRotation(const FPmxRigidBody& RB);

	// Coordinate transformation helpers
	static FVector ConvertVectorPmxToUE(const FVector3f& PmxVector, float Scale);
	static FRotator ConvertRotationPmxToUE(const FVector3f& PmxRotation);
	static FQuat ConvertQuaternionPmxToUE(const FQuat4f& PmxQuat);

	/**
	 * Check if a bone should be forced to kinematic regardless of physics type.
	 * This applies to IK bones, control bones, etc.
	 */
	static bool ShouldForceKinematic(const FString& BoneName);

	/**
	 * Check if a bone is a standard skeletal bone (core body/limbs).
	 * Standard bones include: center, groove, pelvis, spine, neck, head, eyes,
	 * shoulders, arms, elbows, wrists, fingers, legs, knees, ankles, toes.
	 * Excludes physics-only bones like cloth, hair, accessories.
	 */
	static bool IsStandardBone(const FString& BoneName);

	/**
	 * Setup collision filtering for a body based on PMX group data.
	 */
	static void SetupCollisionFiltering(USkeletalBodySetup* Body, const FPmxRigidBody& RB);

	/**
	 * Check if a rigid body is connected to any constraint (joint).
	 * Used for ForceNonStandardBonesSimulated - only bodies with constraints should be simulated.
	 *
	 * @param RigidBodyIndex	Index of the rigid body to check
	 * @param Joints			Array of all joints in the PMX model
	 * @return True if the rigid body is referenced by at least one joint
	 */
	static bool IsRigidBodyConnectedToConstraint(int32 RigidBodyIndex, const TArray<FPmxJoint>& Joints);

	/**
	 * Check if a rigid body is a chain root.
	 * Chain root = Used as RigidBodyIndexA in joints but never as RigidBodyIndexB.
	 * Chain roots should remain Kinematic to anchor physics chains to the body.
	 *
	 * @param RigidBodyIndex	Index of the rigid body to check
	 * @param Joints			Array of all joints in the PMX model
	 * @return True if the rigid body is a chain root (parent only, never child in any joint)
	 */
	static bool IsChainRoot(int32 RigidBodyIndex, const TArray<FPmxJoint>& Joints);

	/**
	 * Check if a rigid body is a chain end (leaf node).
	 * Chain end = Used as RigidBodyIndexB in joints but never as RigidBodyIndexA.
	 * Chain ends have no children in the physics chain.
	 *
	 * @param RigidBodyIndex	Index of the rigid body to check
	 * @param Joints			Array of all joints in the PMX model
	 * @return True if the rigid body is a chain end (child only, never parent in any joint)
	 */
	static bool IsChainEnd(int32 RigidBodyIndex, const TArray<FPmxJoint>& Joints);
};
