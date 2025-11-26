// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved.

#include "PmxPhysicsBuilder.h"
#include "PmxTranslator.h"
#include "PmxStructs.h"
#include "LogPMXImporter.h"

#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/AggregateGeom.h"

namespace
{
	// PMX to UE coordinate transformation (matches PmxNodeBuilder)
	// X-axis 90 degree rotation: Y-up → Z-up
	const FQuat PMXToUE_Rotation = FQuat(FVector::XAxisVector, FMath::DegreesToRadians(90.0));
	const FTransform PMXToUE_Transform(PMXToUE_Rotation);
}

bool FPmxPhysicsBuilder::BuildPhysicsAsset(
	UPhysicsAsset* PhysicsAsset,
	USkeletalMesh* SkeletalMesh,
	const FPmxPhysicsCache& PhysicsData)
{
	if (!PhysicsAsset || !SkeletalMesh)
	{
		UE_LOG(LogPMXImporter, Error, TEXT("BuildPhysicsAsset: Invalid PhysicsAsset or SkeletalMesh"));
		return false;
	}

	const FReferenceSkeleton& RefSkel = SkeletalMesh->GetRefSkeleton();
	if (RefSkel.GetNum() == 0)
	{
		UE_LOG(LogPMXImporter, Error, TEXT("BuildPhysicsAsset: SkeletalMesh has no bones"));
		return false;
	}

	UE_LOG(LogPMXImporter, Display, TEXT("Building PhysicsAsset: %d rigid bodies, %d joints (MassScale=%.2f, DampingScale=%.2f, MaxAngularLimit=%.1f)"),
		PhysicsData.RigidBodies.Num(), PhysicsData.Joints.Num(),
		PhysicsData.MassScale, PhysicsData.DampingScale, PhysicsData.MaxAngularLimit);

	// Clear existing data
	PhysicsAsset->SkeletalBodySetups.Empty();
	PhysicsAsset->ConstraintSetup.Empty();

	// Map from PMX RigidBody index to created BodySetup (for constraint creation)
	TMap<int32, USkeletalBodySetup*> RigidBodyToBodySetup;

	// Create BodySetups for each RigidBody
	int32 CreatedBodies = 0;
	for (int32 RBIndex = 0; RBIndex < PhysicsData.RigidBodies.Num(); ++RBIndex)
	{
		const FPmxRigidBody& RB = PhysicsData.RigidBodies[RBIndex];

		// Validate bone index
		if (RB.RelatedBoneIndex < 0 || RB.RelatedBoneIndex >= PhysicsData.Bones.Num())
		{
			UE_LOG(LogPMXImporter, Verbose,  // Warning -> Verbose
				TEXT("Skipping RigidBody '%s' (Index %d): Invalid bone reference (%d). This is normal for disconnected physics bodies in the PMX file."),
				*RB.Name, RBIndex, RB.RelatedBoneIndex);
			continue;
		}

		const FPmxBone& Bone = PhysicsData.Bones[RB.RelatedBoneIndex];

		USkeletalBodySetup* BodySetup = CreateBodySetup(
			PhysicsAsset, RB, RBIndex, Bone, RefSkel, PhysicsData);

		if (BodySetup)
		{
			RigidBodyToBodySetup.Add(RBIndex, BodySetup);
			++CreatedBodies;
		}
	}

	// Update body index maps
	PhysicsAsset->UpdateBodySetupIndexMap();
	PhysicsAsset->UpdateBoundsBodiesArray();


	// Create Constraints for each Joint
	int32 CreatedConstraints = 0;
	int32 DisabledCollisionPairs = 0;
	for (int32 JointIndex = 0; JointIndex < PhysicsData.Joints.Num(); ++JointIndex)
	{
		const FPmxJoint& Joint = PhysicsData.Joints[JointIndex];

		// Validate rigid body indices
		USkeletalBodySetup** BodyAPtr = RigidBodyToBodySetup.Find(Joint.RigidBodyIndexA);
		USkeletalBodySetup** BodyBPtr = RigidBodyToBodySetup.Find(Joint.RigidBodyIndexB);

		if (!BodyAPtr || !BodyBPtr)
		{
			UE_LOG(LogPMXImporter, Log,
				TEXT("Skipping Joint '%s': One or both bodies not found (A=%d, B=%d)"),
				*Joint.Name, Joint.RigidBodyIndexA, Joint.RigidBodyIndexB);
			continue;
		}

		UPhysicsConstraintTemplate* Constraint = CreateConstraint(
			PhysicsAsset, Joint, PhysicsData.RigidBodies, PhysicsData.Bones, PhysicsData);

		if (Constraint)
		{
			++CreatedConstraints;

			// Disable collision between constraint-connected bodies if option is enabled
			// This prevents stiff behavior from overlapping rigid bodies in PMX
			if (PhysicsData.bDisableConstraintBodyCollision && *BodyAPtr && *BodyBPtr)
			{
				const int32 BodyIndexA = PhysicsAsset->FindBodyIndex((*BodyAPtr)->BoneName);
				const int32 BodyIndexB = PhysicsAsset->FindBodyIndex((*BodyBPtr)->BoneName);

				if (BodyIndexA != INDEX_NONE && BodyIndexB != INDEX_NONE)
				{
					PhysicsAsset->DisableCollision(BodyIndexA, BodyIndexB);
					++DisabledCollisionPairs;

					UE_LOG(LogPMXImporter, Verbose,
						TEXT("Disabled collision between '%s' and '%s' (constraint: '%s')"),
						*(*BodyAPtr)->BoneName.ToString(), *(*BodyBPtr)->BoneName.ToString(), *Joint.Name);
				}
			}
		}
	}

	UE_LOG(LogPMXImporter, Display, TEXT("Created %d bodies, %d constraints"), CreatedBodies, CreatedConstraints);

	// Apply PMX collision group filtering if enabled
	// PMX uses NonCollisionGroup bitmask - bodies with matching bits should not collide
	if (PhysicsData.bUsePmxCollisionGroups)
	{
		int32 GroupDisabledPairs = 0;
		const int32 NumBodies = PhysicsData.RigidBodies.Num();

		// Check each pair of rigid bodies
		for (int32 i = 0; i < NumBodies; ++i)
		{
			USkeletalBodySetup** BodyIPtr = RigidBodyToBodySetup.Find(i);
			if (!BodyIPtr || !(*BodyIPtr))
			{
				continue;
			}

			const FPmxRigidBody& RBI = PhysicsData.RigidBodies[i];

			for (int32 j = i + 1; j < NumBodies; ++j)
			{
				USkeletalBodySetup** BodyJPtr = RigidBodyToBodySetup.Find(j);
				if (!BodyJPtr || !(*BodyJPtr))
				{
					continue;
				}

				const FPmxRigidBody& RBJ = PhysicsData.RigidBodies[j];

				// Check if bodies should not collide based on PMX group/mask
				// Body I is in group I, with NonCollisionGroup mask specifying which groups to ignore
				// If Body J's group bit is set in Body I's NonCollisionGroup mask, don't collide
				// Also check the reverse: if Body I's group bit is set in Body J's NonCollisionGroup mask
				const uint16 GroupMaskI = static_cast<uint16>(1 << RBI.Group);
				const uint16 GroupMaskJ = static_cast<uint16>(1 << RBJ.Group);

				const bool bIIgnoresJ = (RBI.NonCollisionGroup & GroupMaskJ) != 0;
				const bool bJIgnoresI = (RBJ.NonCollisionGroup & GroupMaskI) != 0;

				if (bIIgnoresJ || bJIgnoresI)
				{
					const int32 BodyIndexI = PhysicsAsset->FindBodyIndex((*BodyIPtr)->BoneName);
					const int32 BodyIndexJ = PhysicsAsset->FindBodyIndex((*BodyJPtr)->BoneName);

					if (BodyIndexI != INDEX_NONE && BodyIndexJ != INDEX_NONE)
					{
						PhysicsAsset->DisableCollision(BodyIndexI, BodyIndexJ);
						++GroupDisabledPairs;

						UE_LOG(LogPMXImporter, Verbose,
							TEXT("PMX Group Filter: Disabled collision between '%s' (Group %d, Mask 0x%04X) and '%s' (Group %d, Mask 0x%04X)"),
							*(*BodyIPtr)->BoneName.ToString(), RBI.Group, RBI.NonCollisionGroup,
							*(*BodyJPtr)->BoneName.ToString(), RBJ.Group, RBJ.NonCollisionGroup);
					}
				}
			}
		}

	}

	// Enable collision between standard bones (body/limbs) and non-standard bones (cloth/hair)
	// This ensures skirts don't pass through the body
	if (PhysicsData.bEnableStandardNonStandardCollision)
	{
		int32 EnabledPairs = 0;

		for (int32 i = 0; i < PhysicsData.RigidBodies.Num(); ++i)
		{
			USkeletalBodySetup** BodyIPtr = RigidBodyToBodySetup.Find(i);
			if (!BodyIPtr || !(*BodyIPtr))
			{
				continue;
			}

			const FPmxRigidBody& RBI = PhysicsData.RigidBodies[i];
			if (RBI.RelatedBoneIndex < 0 || RBI.RelatedBoneIndex >= PhysicsData.Bones.Num())
			{
				continue;
			}

			const FString& BoneNameI = PhysicsData.Bones[RBI.RelatedBoneIndex].Name;
			const bool bIsStandardI = IsStandardBone(BoneNameI);

			for (int32 j = i + 1; j < PhysicsData.RigidBodies.Num(); ++j)
			{
				USkeletalBodySetup** BodyJPtr = RigidBodyToBodySetup.Find(j);
				if (!BodyJPtr || !(*BodyJPtr))
				{
					continue;
				}

				const FPmxRigidBody& RBJ = PhysicsData.RigidBodies[j];
				if (RBJ.RelatedBoneIndex < 0 || RBJ.RelatedBoneIndex >= PhysicsData.Bones.Num())
				{
					continue;
				}

				const FString& BoneNameJ = PhysicsData.Bones[RBJ.RelatedBoneIndex].Name;
				const bool bIsStandardJ = IsStandardBone(BoneNameJ);

				// If one is standard and the other is non-standard, enable collision
				if (bIsStandardI != bIsStandardJ)
				{
					const int32 BodyIndexI = PhysicsAsset->FindBodyIndex((*BodyIPtr)->BoneName);
					const int32 BodyIndexJ = PhysicsAsset->FindBodyIndex((*BodyJPtr)->BoneName);

					if (BodyIndexI != INDEX_NONE && BodyIndexJ != INDEX_NONE)
					{
						// Re-enable collision (may have been disabled by previous filters)
						PhysicsAsset->EnableCollision(BodyIndexI, BodyIndexJ);
						++EnabledPairs;
					}
				}
			}
		}

		if (EnabledPairs > 0)
		{
			UE_LOG(LogPMXImporter, Display, TEXT("Enabled %d standard-nonstandard body collision pairs"), EnabledPairs);
		}
	}

	// Final update
	PhysicsAsset->UpdateBodySetupIndexMap();

	return CreatedBodies > 0;
}

USkeletalBodySetup* FPmxPhysicsBuilder::CreateBodySetup(
	UPhysicsAsset* Asset,
	const FPmxRigidBody& RB,
	int32 RBIndex,
	const FPmxBone& Bone,
	const FReferenceSkeleton& RefSkel,
	const FPmxPhysicsCache& PhysicsData)
{
	// Find bone in skeleton by PMX bone name
	const FName PmxBoneName = FName(*Bone.Name);
	int32 BoneIndex = RefSkel.FindBoneIndex(PmxBoneName);
	FName ActualBoneName = PmxBoneName;

	// If not found by PMX name, try to find by iterating through skeleton bones
	// This handles cases where FName comparison might differ due to unicode handling
	if (BoneIndex == INDEX_NONE)
	{
		for (int32 i = 0; i < RefSkel.GetNum(); ++i)
		{
			const FName SkeletonBoneName = RefSkel.GetBoneName(i);
			if (SkeletonBoneName.ToString().Equals(Bone.Name, ESearchCase::CaseSensitive))
			{
				BoneIndex = i;
				ActualBoneName = SkeletonBoneName; // Use the exact FName from skeleton
				break;
			}
		}
	}

	if (BoneIndex == INDEX_NONE)
	{
		UE_LOG(LogPMXImporter, Log,
			TEXT("CreateBodySetup: Bone '%s' not found in skeleton for RigidBody '%s'"),
			*Bone.Name, *RB.Name);
		return nullptr;
	}

	// Use the actual bone name from the skeleton to ensure exact FName match
	ActualBoneName = RefSkel.GetBoneName(BoneIndex);

	// Check if body already exists for this bone
	// NOTE: We cannot use Asset->FindBodyIndex() because BodySetupIndexMap is not updated
	// during construction. We must manually search the SkeletalBodySetups array.
	int32 ExistingBodyIndex = INDEX_NONE;
	for (int32 i = 0; i < Asset->SkeletalBodySetups.Num(); ++i)
	{
		if (Asset->SkeletalBodySetups[i] && Asset->SkeletalBodySetups[i]->BoneName == ActualBoneName)
		{
			ExistingBodyIndex = i;
			break;
		}
	}

	if (ExistingBodyIndex != INDEX_NONE)
	{
		UE_LOG(LogPMXImporter, Log,
			TEXT("CreateBodySetup: BodySetup already exists for bone '%s' (index %d), skipping RigidBody '%s'"),
			*ActualBoneName.ToString(), ExistingBodyIndex, *RB.Name);
		return nullptr;
	}

	// Create body setup
	USkeletalBodySetup* BodySetup = NewObject<USkeletalBodySetup>(Asset, NAME_None, RF_Transactional);
	BodySetup->BoneName = ActualBoneName; // Use exact FName from skeleton

	// Determine physics type
	EPhysicsType PhysType = EPhysicsType::PhysType_Kinematic;
	switch (RB.PhysicsType)
	{
	case 0: // Follow Bone (Static)
		PhysType = EPhysicsType::PhysType_Kinematic;
		break;
	case 1: // Physics (Dynamic)
		PhysType = EPhysicsType::PhysType_Simulated;
		break;
	case 2: // Physics + Bone
		switch (PhysicsData.Type2Mode)
		{
		case EPmxPhysicsType2Handling::ConvertToKinematic:
			PhysType = EPhysicsType::PhysType_Kinematic;
			break;
		case EPmxPhysicsType2Handling::ConvertToDynamic:
			PhysType = EPhysicsType::PhysType_Simulated;
			break;
		case EPmxPhysicsType2Handling::Skip:
			return nullptr;
		}
		break;
	}

	// Force kinematic for IK/control bones
	if (ShouldForceKinematic(Bone.Name))
	{
		PhysType = EPhysicsType::PhysType_Kinematic;
	}

	// Apply bone type override options
	const bool bIsStandardBone = IsStandardBone(Bone.Name);

	// Force kinematic for standard skeletal bones if option is enabled
	if (PhysicsData.bForceStandardBonesKinematic && bIsStandardBone)
	{
		PhysType = EPhysicsType::PhysType_Kinematic;
	}

	// Force simulated for non-standard bones (cloth/hair/accessories) if option is enabled
	// Only apply to bodies that are connected to at least one constraint (joint)
	// Chain roots are kept Kinematic to anchor physics chains to the body
	if (PhysicsData.bForceNonStandardBonesSimulated && !bIsStandardBone)
	{
		// Check if this body is connected to any constraint
		const bool bHasConstraint = IsRigidBodyConnectedToConstraint(RBIndex, PhysicsData.Joints);

		if (bHasConstraint)
		{
			// Check if this body should stay Kinematic (conservative approach)
			// Chain root = parent only, never child in any joint
			// Original PhysicsType=0 = FollowBone (designed to be Kinematic)
			// Either condition means this body should anchor to the skeleton
			const bool bIsRoot = IsChainRoot(RBIndex, PhysicsData.Joints);
			const bool bOriginallyKinematic = (RB.PhysicsType == 0); // FollowBone

			if (bIsRoot || bOriginallyKinematic)
			{
				// Keep Kinematic to anchor the chain to the body
				PhysType = EPhysicsType::PhysType_Kinematic;
			}
			else
			{
				// Only chain middle/end nodes with original PhysicsType!=0 become Simulated
				PhysType = EPhysicsType::PhysType_Simulated;
			}
		}
	}

	BodySetup->PhysicsType = PhysType;

	// Setup physical properties
	FBodyInstance& BI = BodySetup->DefaultInstance;
	BI.SetMassOverride(RB.Mass * PhysicsData.MassScale);
	BI.bOverrideMass = true;
	BI.LinearDamping = RB.MoveAttenuation * PhysicsData.DampingScale;
	BI.AngularDamping = RB.RotationAttenuation * PhysicsData.DampingScale;

	// Setup collision
	SetupCollisionFiltering(BodySetup, RB);

	// Get body local transform
	const FVector LocalPos = GetBodyLocalPosition(RB, Bone, PhysicsData.Scale);
	const FRotator LocalRot = GetBodyLocalRotation(RB);

	// Create shape based on type
	FKAggregateGeom& AggGeom = BodySetup->AggGeom;
	const float BaseScale = PhysicsData.Scale * PhysicsData.ShapeScale;

	switch (RB.Shape)
	{
	case 0: // Sphere
		SetupSphereShape(AggGeom, RB, BaseScale * PhysicsData.SphereScale, LocalPos);
		break;
	case 1: // Box
		SetupBoxShape(AggGeom, RB, BaseScale * PhysicsData.BoxScale, LocalPos, LocalRot);
		break;
	case 2: // Capsule
		SetupCapsuleShape(AggGeom, RB, BaseScale * PhysicsData.CapsuleScale, LocalPos, LocalRot);
		break;
	default:
		UE_LOG(LogPMXImporter, Warning, TEXT("Unknown shape type %d for '%s'"), RB.Shape, *RB.Name);
		SetupSphereShape(AggGeom, RB, BaseScale * PhysicsData.SphereScale, LocalPos);
		break;
	}

	// Apply local transform to all shapes
	// Note: FKSphereElem no longer has Rotation in UE 5.7 - spheres are rotation invariant
	for (FKSphereElem& Sphere : AggGeom.SphereElems)
	{
		Sphere.Center = LocalPos;
	}
	for (FKBoxElem& Box : AggGeom.BoxElems)
	{
		Box.Center = LocalPos;
		Box.Rotation = LocalRot;
	}
	for (FKSphylElem& Sphyl : AggGeom.SphylElems)
	{
		Sphyl.Center = LocalPos;
		Sphyl.Rotation = LocalRot;
	}

	// Add to physics asset
	Asset->SkeletalBodySetups.Add(BodySetup);

	const int32 NewBodySetupCount = Asset->SkeletalBodySetups.Num();
	UE_LOG(LogPMXImporter, Verbose,
		TEXT("✓ Created BodySetup for bone '%s' (RB: '%s', Shape: %d, PhysType: %d) - Total BodySetups: %d"),
		*Bone.Name, *RB.Name, RB.Shape, static_cast<int32>(PhysType), NewBodySetupCount);

	return BodySetup;
}

UPhysicsConstraintTemplate* FPmxPhysicsBuilder::CreateConstraint(
	UPhysicsAsset* Asset,
	const FPmxJoint& Joint,
	const TArray<FPmxRigidBody>& RigidBodies,
	const TArray<FPmxBone>& Bones,
	const FPmxPhysicsCache& PhysicsData)
{
	// Validate indices
	if (Joint.RigidBodyIndexA < 0 || Joint.RigidBodyIndexA >= RigidBodies.Num() ||
		Joint.RigidBodyIndexB < 0 || Joint.RigidBodyIndexB >= RigidBodies.Num())
	{
		UE_LOG(LogPMXImporter, Warning,
			TEXT("CreateConstraint: Invalid rigid body indices for Joint '%s' (A=%d, B=%d)"),
			*Joint.Name, Joint.RigidBodyIndexA, Joint.RigidBodyIndexB);
		return nullptr;
	}

	const FPmxRigidBody& RBA = RigidBodies[Joint.RigidBodyIndexA];
	const FPmxRigidBody& RBB = RigidBodies[Joint.RigidBodyIndexB];

	// Get bone names
	if (RBA.RelatedBoneIndex < 0 || RBA.RelatedBoneIndex >= Bones.Num() ||
		RBB.RelatedBoneIndex < 0 || RBB.RelatedBoneIndex >= Bones.Num())
	{
		UE_LOG(LogPMXImporter, Warning,
			TEXT("CreateConstraint: Invalid bone indices for Joint '%s'"),
			*Joint.Name);
		return nullptr;
	}

	// Find the actual bone names from existing BodySetups
	// This ensures we use the exact same FName that was registered in the physics asset
	const FString& PmxBoneNameA = Bones[RBA.RelatedBoneIndex].Name;
	const FString& PmxBoneNameB = Bones[RBB.RelatedBoneIndex].Name;

	FName ActualBoneNameA = NAME_None;
	FName ActualBoneNameB = NAME_None;

	// Find matching BodySetups by comparing bone name strings
	for (USkeletalBodySetup* BodySetup : Asset->SkeletalBodySetups)
	{
		if (BodySetup)
		{
			const FString BodyBoneName = BodySetup->BoneName.ToString();
			if (ActualBoneNameA.IsNone() && BodyBoneName.Equals(PmxBoneNameA, ESearchCase::CaseSensitive))
			{
				ActualBoneNameA = BodySetup->BoneName;
			}
			if (ActualBoneNameB.IsNone() && BodyBoneName.Equals(PmxBoneNameB, ESearchCase::CaseSensitive))
			{
				ActualBoneNameB = BodySetup->BoneName;
			}
			if (!ActualBoneNameA.IsNone() && !ActualBoneNameB.IsNone())
			{
				break;
			}
		}
	}

	// Verify both bodies were found
	if (ActualBoneNameA.IsNone() || ActualBoneNameB.IsNone())
	{
		UE_LOG(LogPMXImporter, Warning,
			TEXT("CreateConstraint: Bodies not found for Joint '%s' (Bone1: %s, Bone2: %s)"),
			*Joint.Name, *PmxBoneNameA, *PmxBoneNameB);
		return nullptr;
	}

	// Create constraint
	UPhysicsConstraintTemplate* Constraint = NewObject<UPhysicsConstraintTemplate>(Asset, NAME_None, RF_Transactional);

	FConstraintInstance& CI = Constraint->DefaultInstance;
	CI.ConstraintBone1 = ActualBoneNameA;
	CI.ConstraintBone2 = ActualBoneNameB;

	// Joint type handling
	if (Joint.JointType != 0)
	{
		UE_LOG(LogPMXImporter, Verbose,
			TEXT("Joint '%s' has non-standard type %d, treating as Spring 6DOF"),
			*Joint.Name, Joint.JointType);
	}

	// Convert linear limits (PMX to UE coordinates)
	const FVector MinMove = ConvertVectorPmxToUE(Joint.MoveRestrictionMin, PhysicsData.Scale);
	const FVector MaxMove = ConvertVectorPmxToUE(Joint.MoveRestrictionMax, PhysicsData.Scale);

	// Setup linear motion
	// bForceAllLinearMotionLocked: Completely prevent spring-like stretching regardless of PMX settings
	// LinearMotionTolerance: Only used when bForceAllLinearMotionLocked is false
	const bool bForceLocked = PhysicsData.bForceAllLinearMotionLocked;
	const float LinearTolerance = PhysicsData.LinearMotionTolerance;
	auto SetLinearMotion = [bForceLocked, LinearTolerance](float Min, float Max) -> ELinearConstraintMotion
	{
		// Force all linear motion to locked if option is enabled
		if (bForceLocked)
		{
			return ELinearConstraintMotion::LCM_Locked;
		}
		// Otherwise use tolerance-based locking
		if (FMath::Abs(Min) < LinearTolerance && FMath::Abs(Max) < LinearTolerance)
		{
			return ELinearConstraintMotion::LCM_Locked;
		}
		return ELinearConstraintMotion::LCM_Limited;
	};

	// Set linear limits (use ProfileInstance directly - SetXMotion() only works when ConstraintHandle is valid)
	CI.ProfileInstance.LinearLimit.XMotion = SetLinearMotion(MinMove.X, MaxMove.X);
	CI.ProfileInstance.LinearLimit.YMotion = SetLinearMotion(MinMove.Y, MaxMove.Y);
	CI.ProfileInstance.LinearLimit.ZMotion = SetLinearMotion(MinMove.Z, MaxMove.Z);
	CI.ProfileInstance.LinearLimit.Limit = FMath::Max3(
		FMath::Max(FMath::Abs(MinMove.X), FMath::Abs(MaxMove.X)),
		FMath::Max(FMath::Abs(MinMove.Y), FMath::Abs(MaxMove.Y)),
		FMath::Max(FMath::Abs(MinMove.Z), FMath::Abs(MaxMove.Z)));

	// Convert angular limits (radians to degrees)
	const FVector MinRotDeg = FVector(
		FMath::RadiansToDegrees(Joint.RotationRestrictionMin.X),
		FMath::RadiansToDegrees(Joint.RotationRestrictionMin.Y),
		FMath::RadiansToDegrees(Joint.RotationRestrictionMin.Z));
	const FVector MaxRotDeg = FVector(
		FMath::RadiansToDegrees(Joint.RotationRestrictionMax.X),
		FMath::RadiansToDegrees(Joint.RotationRestrictionMax.Y),
		FMath::RadiansToDegrees(Joint.RotationRestrictionMax.Z));

	// Setup angular motion
	auto SetAngularMotion = [](float Min, float Max) -> EAngularConstraintMotion
	{
		const float Tolerance = 0.1f; // degrees
		if (FMath::Abs(Max - Min) < Tolerance)
		{
			return EAngularConstraintMotion::ACM_Locked;
		}
		if (Max - Min >= 359.0f)
		{
			return EAngularConstraintMotion::ACM_Free;
		}
		return EAngularConstraintMotion::ACM_Limited;
	};

	// Map PMX XYZ rotation to UE Swing1/Swing2/Twist
	// PMX: X=Roll, Y=Pitch, Z=Yaw -> UE: Twist=X, Swing1=Y, Swing2=Z
	// (use ProfileInstance directly - SetAngularXMotion() only works when ConstraintHandle is valid)

	// Set angular limits with optional clamping
	float TwistLimit = FMath::Max(FMath::Abs(MinRotDeg.X), FMath::Abs(MaxRotDeg.X));
	float Swing1Limit = FMath::Max(FMath::Abs(MinRotDeg.Y), FMath::Abs(MaxRotDeg.Y));
	float Swing2Limit = FMath::Max(FMath::Abs(MinRotDeg.Z), FMath::Abs(MaxRotDeg.Z));

	// Get motion types from PMX data
	EAngularConstraintMotion TwistMotion = SetAngularMotion(MinRotDeg.X, MaxRotDeg.X);
	EAngularConstraintMotion Swing1Motion = SetAngularMotion(MinRotDeg.Y, MaxRotDeg.Y);
	EAngularConstraintMotion Swing2Motion = SetAngularMotion(MinRotDeg.Z, MaxRotDeg.Z);

	// Apply MaxAngularLimit clamp if enabled (< 180 degrees)
	// IMPORTANT: Also change motion to Limited when clamping, otherwise limits are ignored
	const float MaxLimit = PhysicsData.MaxAngularLimit;
	if (MaxLimit < 180.0f)
	{
		TwistLimit = FMath::Min(TwistLimit, MaxLimit);
		Swing1Limit = FMath::Min(Swing1Limit, MaxLimit);
		Swing2Limit = FMath::Min(Swing2Limit, MaxLimit);

		// Force Limited motion when MaxAngularLimit is applied (Free/Locked -> Limited)
		if (TwistMotion != EAngularConstraintMotion::ACM_Locked || TwistLimit > 0.0f)
			TwistMotion = EAngularConstraintMotion::ACM_Limited;
		if (Swing1Motion != EAngularConstraintMotion::ACM_Locked || Swing1Limit > 0.0f)
			Swing1Motion = EAngularConstraintMotion::ACM_Limited;
		if (Swing2Motion != EAngularConstraintMotion::ACM_Locked || Swing2Limit > 0.0f)
			Swing2Motion = EAngularConstraintMotion::ACM_Limited;
	}

	// Phase 5: Apply constraint mode
	if (PhysicsData.ConstraintMode == EPmxConstraintMode::OverrideAll)
	{
		// Override mode: Use user-specified settings for all constraints
		CI.ProfileInstance.LinearLimit.XMotion = PhysicsData.bLockAllLinearMotion ? ELinearConstraintMotion::LCM_Locked : ELinearConstraintMotion::LCM_Free;
		CI.ProfileInstance.LinearLimit.YMotion = PhysicsData.bLockAllLinearMotion ? ELinearConstraintMotion::LCM_Locked : ELinearConstraintMotion::LCM_Free;
		CI.ProfileInstance.LinearLimit.ZMotion = PhysicsData.bLockAllLinearMotion ? ELinearConstraintMotion::LCM_Locked : ELinearConstraintMotion::LCM_Free;
		CI.ProfileInstance.LinearLimit.Limit = 0.0f;

		CI.ProfileInstance.ConeLimit.Swing1Motion = PhysicsData.OverrideAngularMotion;
		CI.ProfileInstance.ConeLimit.Swing2Motion = PhysicsData.OverrideAngularMotion;
		CI.ProfileInstance.ConeLimit.Swing1LimitDegrees = PhysicsData.OverrideSwing1Limit;
		CI.ProfileInstance.ConeLimit.Swing2LimitDegrees = PhysicsData.OverrideSwing2Limit;
		CI.ProfileInstance.TwistLimit.TwistMotion = PhysicsData.OverrideAngularMotion;
		CI.ProfileInstance.TwistLimit.TwistLimitDegrees = PhysicsData.OverrideTwistLimit;
	}
	else
	{
		// UsePmxSettings mode: Use PMX joint settings with optional MaxAngularLimit clamp
		CI.ProfileInstance.ConeLimit.Swing1Motion = Swing1Motion;
		CI.ProfileInstance.ConeLimit.Swing2Motion = Swing2Motion;
		CI.ProfileInstance.ConeLimit.Swing1LimitDegrees = Swing1Limit;
		CI.ProfileInstance.ConeLimit.Swing2LimitDegrees = Swing2Limit;
		CI.ProfileInstance.TwistLimit.TwistMotion = TwistMotion;
		CI.ProfileInstance.TwistLimit.TwistLimitDegrees = TwistLimit;
	}

	// Soft Constraint settings (applies to all modes)
	if (PhysicsData.bUseSoftConstraint)
	{
		CI.ProfileInstance.ConeLimit.bSoftConstraint = true;
		CI.ProfileInstance.ConeLimit.Stiffness = PhysicsData.SoftConstraintStiffness;
		CI.ProfileInstance.ConeLimit.Damping = PhysicsData.SoftConstraintDamping;

		CI.ProfileInstance.TwistLimit.bSoftConstraint = true;
		CI.ProfileInstance.TwistLimit.Stiffness = PhysicsData.SoftConstraintStiffness;
		CI.ProfileInstance.TwistLimit.Damping = PhysicsData.SoftConstraintDamping;
	}

	// Phase 6: Long chain optimization
	if (PhysicsData.bOptimizeForLongChains)
	{
		// Projection settings - corrects accumulated constraint errors in long chains
		if (PhysicsData.bEnableProjection)
		{
			CI.ProfileInstance.bEnableProjection = true;
			CI.ProfileInstance.ProjectionLinearTolerance = PhysicsData.ProjectionLinearTolerance;
			CI.ProfileInstance.ProjectionAngularTolerance = FMath::DegreesToRadians(PhysicsData.ProjectionAngularTolerance);
			CI.ProfileInstance.ProjectionLinearAlpha = 0.7f;
			CI.ProfileInstance.ProjectionAngularAlpha = 0.7f;
		}

		// Parent Dominates - prevents child bodies from affecting parent
		if (PhysicsData.bAutoParentDominates)
		{
			const bool bIsChainRootA = IsChainRoot(Joint.RigidBodyIndexA, PhysicsData.Joints);
			const bool bIsChainEndB = IsChainEnd(Joint.RigidBodyIndexB, PhysicsData.Joints);
			if (bIsChainRootA || bIsChainEndB)
			{
				CI.ProfileInstance.bParentDominates = true;
			}
		}

		// Mass Conditioning - improves stability for bodies with large mass ratio differences
		if (PhysicsData.bEnableMassConditioning)
		{
			CI.ProfileInstance.bEnableMassConditioning = true;
		}

		// Contact Transfer Scale - how much collision force transfers from child to parent
		CI.ProfileInstance.ContactTransferScale = PhysicsData.ContactTransferScale;
	}

	// Setup spring (for JointType 0 = Spring 6DOF)
	if (Joint.JointType == 0)
	{
		// Get scale factors from physics cache
		const float StiffnessScale = PhysicsData.ConstraintStiffnessScale;
		const float DampingScale = PhysicsData.ConstraintDampingScale;

		// Linear spring with stiffness/damping scale
		// NOTE: Linear spring drive causes spring-like stretching behavior
		// When bDisableLinearSpringDrive is true, we skip linear drive entirely
		if (!PhysicsData.bDisableLinearSpringDrive)
		{
			const FVector SpringMove = ConvertVectorPmxToUE(Joint.SpringMoveCoefficient, 1.0f); // Spring coefficients not scaled by position scale
			CI.ProfileInstance.LinearDrive.XDrive.bEnablePositionDrive = SpringMove.X > 0.0f;
			CI.ProfileInstance.LinearDrive.XDrive.Stiffness = SpringMove.X * StiffnessScale;
			CI.ProfileInstance.LinearDrive.XDrive.Damping = 0.1f * DampingScale; // Base damping 0.1
			CI.ProfileInstance.LinearDrive.YDrive.bEnablePositionDrive = SpringMove.Y > 0.0f;
			CI.ProfileInstance.LinearDrive.YDrive.Stiffness = SpringMove.Y * StiffnessScale;
			CI.ProfileInstance.LinearDrive.YDrive.Damping = 0.1f * DampingScale;
			CI.ProfileInstance.LinearDrive.ZDrive.bEnablePositionDrive = SpringMove.Z > 0.0f;
			CI.ProfileInstance.LinearDrive.ZDrive.Stiffness = SpringMove.Z * StiffnessScale;
			CI.ProfileInstance.LinearDrive.ZDrive.Damping = 0.1f * DampingScale;
		}

		// Angular spring with stiffness/damping scale
		const FVector SpringRot = FVector(
			Joint.SpringRotationCoefficient.X,
			Joint.SpringRotationCoefficient.Y,
			Joint.SpringRotationCoefficient.Z);
		const float AvgSpringRot = (SpringRot.X + SpringRot.Y + SpringRot.Z) / 3.0f;

		CI.ProfileInstance.AngularDrive.SlerpDrive.bEnablePositionDrive = AvgSpringRot > 0.0f;
		CI.ProfileInstance.AngularDrive.SlerpDrive.Stiffness = AvgSpringRot * StiffnessScale;
		CI.ProfileInstance.AngularDrive.SlerpDrive.Damping = 0.1f * DampingScale; // Base damping 0.1
	}

	// Convert joint position from world to bone-local coordinates
	// PMX Joint.Position is in world space, but UE constraint frames need bone-local coordinates
	const FVector JointWorldPos = ConvertVectorPmxToUE(Joint.Position, PhysicsData.Scale);
	const FRotator JointRot = ConvertRotationPmxToUE(Joint.Rotation);

	// Get bone positions for both bodies
	const FPmxBone& BoneA = Bones[RBA.RelatedBoneIndex];
	const FPmxBone& BoneB = Bones[RBB.RelatedBoneIndex];
	const FVector BonePosA = ConvertVectorPmxToUE(BoneA.Position, PhysicsData.Scale);
	const FVector BonePosB = ConvertVectorPmxToUE(BoneB.Position, PhysicsData.Scale);

	// Calculate bone-local positions for each constraint frame
	const FVector JointLocalPosA = JointWorldPos - BonePosA;
	const FVector JointLocalPosB = JointWorldPos - BonePosB;

	CI.SetRefPosition(EConstraintFrame::Frame1, JointLocalPosA);
	CI.SetRefPosition(EConstraintFrame::Frame2, JointLocalPosB);

	// UE 5.7: SetRefOrientation requires PriAxis and SecAxis vectors
	const FQuat JointQuat = JointRot.Quaternion().GetNormalized();
	const FVector PriAxis = JointQuat.GetAxisX();
	const FVector SecAxis = JointQuat.GetAxisY();
	CI.SetRefOrientation(EConstraintFrame::Frame1, PriAxis, SecAxis);
	CI.SetRefOrientation(EConstraintFrame::Frame2, PriAxis, SecAxis);

	// CRITICAL: Sync DefaultProfile with ProfileInstance
	// Without this, empty DefaultProfile values will overwrite ProfileInstance during Serialize
	// When setting values manually in editor, PostEditChangeProperty handles this,
	// but when creating via code, we must call it explicitly
#if WITH_EDITOR
	Constraint->UpdateProfileInstance();
#endif

	// Add to physics asset
	Asset->ConstraintSetup.Add(Constraint);

	UE_LOG(LogPMXImporter, Verbose,
		TEXT("Created Constraint '%s' (Bone1: %s, Bone2: %s)"),
		*Joint.Name, *ActualBoneNameA.ToString(), *ActualBoneNameB.ToString());

	return Constraint;
}

void FPmxPhysicsBuilder::SetupSphereShape(FKAggregateGeom& Geom, const FPmxRigidBody& RB, float Scale, const FVector& LocalPos)
{
	FKSphereElem Sphere;
	Sphere.Radius = RB.Size.X * Scale;
	Sphere.Center = LocalPos;
	Geom.SphereElems.Add(Sphere);
}

void FPmxPhysicsBuilder::SetupBoxShape(FKAggregateGeom& Geom, const FPmxRigidBody& RB, float Scale, const FVector& LocalPos, const FRotator& LocalRot)
{
	FKBoxElem Box;
	// Box: X-axis 90 degree rotation transforms axes
	// For size (always positive), we use: X->X, Y->Z, Z->Y
	Box.X = RB.Size.X * Scale * 2.0f;
	Box.Y = RB.Size.Z * Scale * 2.0f;
	Box.Z = RB.Size.Y * Scale * 2.0f;
	Box.Center = LocalPos;
	Box.Rotation = LocalRot;
	Geom.BoxElems.Add(Box);
}

void FPmxPhysicsBuilder::SetupCapsuleShape(FKAggregateGeom& Geom, const FPmxRigidBody& RB, float Scale, const FVector& LocalPos, const FRotator& LocalRot)
{
	FKSphylElem Capsule;
	// PMX: Size.X = radius, Size.Y = height (full length including hemispheres)
	// UE: Radius = cylinder radius, Length = cylinder height (excluding hemispheres)
	Capsule.Radius = RB.Size.X * Scale;
	Capsule.Length = FMath::Max(0.0f, RB.Size.Y * Scale - 2.0f * Capsule.Radius);
	Capsule.Center = LocalPos;
	Capsule.Rotation = LocalRot;
	Geom.SphylElems.Add(Capsule);
}

FVector FPmxPhysicsBuilder::GetBodyLocalPosition(const FPmxRigidBody& RB, const FPmxBone& Bone, float Scale)
{
	// PMX RigidBody.Position is in world coordinates
	// UE BodySetup needs bone-local coordinates
	const FVector WorldPos = ConvertVectorPmxToUE(RB.Position, Scale);
	const FVector BonePos = ConvertVectorPmxToUE(Bone.Position, Scale);
	return WorldPos - BonePos;
}

FRotator FPmxPhysicsBuilder::GetBodyLocalRotation(const FPmxRigidBody& RB)
{
	return ConvertRotationPmxToUE(RB.Rotation);
}

FVector FPmxPhysicsBuilder::ConvertVectorPmxToUE(const FVector3f& PmxVector, float Scale)
{
	// PMX: Right-handed Y-up (X right, Y up, Z forward)
	// UE: Left-handed Z-up (X forward, Y right, Z up)
	// Apply X-axis 90 degree rotation (same as PmxNodeBuilder for bone positions)
	const FVector P(
		static_cast<double>(PmxVector.X),
		static_cast<double>(PmxVector.Y),
		static_cast<double>(PmxVector.Z));

	// Transform with X-axis 90 degree rotation
	const FVector Transformed = PMXToUE_Transform.TransformPosition(P);

	// Apply scale
	return Transformed * Scale;
}

FRotator FPmxPhysicsBuilder::ConvertRotationPmxToUE(const FVector3f& PmxRotation)
{
	// PMX stores Euler angles in radians (XYZ order in PMX coordinate system)
	// We need to convert the rotation to UE coordinate system using the same
	// X-axis 90 degree rotation used for positions.

	// Build quaternion from PMX Euler angles (in PMX space)
	// PMX uses XYZ Euler order
	const double RollRad = static_cast<double>(PmxRotation.X);
	const double PitchRad = static_cast<double>(PmxRotation.Y);
	const double YawRad = static_cast<double>(PmxRotation.Z);

	// Create rotation quaternions for each axis in PMX space
	const FQuat RotX = FQuat(FVector::XAxisVector, RollRad);
	const FQuat RotY = FQuat(FVector::YAxisVector, PitchRad);
	const FQuat RotZ = FQuat(FVector::ZAxisVector, YawRad);

	// Combine in XYZ order (PMX convention)
	const FQuat PmxQuat = RotX * RotY * RotZ;

	// Apply coordinate system transformation:
	// Transform rotation from PMX space to UE space using similarity transform
	// UEQuat = PMXToUE * PmxQuat * PMXToUE^-1
	const FQuat UEQuat = PMXToUE_Rotation * PmxQuat * PMXToUE_Rotation.Inverse();

	return UEQuat.Rotator();
}

FQuat FPmxPhysicsBuilder::ConvertQuaternionPmxToUE(const FQuat4f& PmxQuat)
{
	// Simple axis swap: (X, Y, Z, W) -> (X, Z, Y, W)
	// Matches PmxTranslator::ConvertQuaternionPmxToUE
	return FQuat(PmxQuat.X, PmxQuat.Z, PmxQuat.Y, PmxQuat.W);
}

bool FPmxPhysicsBuilder::ShouldForceKinematic(const FString& BoneName)
{
	// Control/IK/Helper bones should be kinematic (non-deforming bones)
	// Based on conventions from: Max, Maya, Blender, Unity, Unreal, MMD
	static const TArray<FString> KinematicPatterns = {
		// === IK Bones ===
		TEXT("IK"),
		TEXT("\xFF29\xFF2B"), // Full-width IK
		TEXT("\u8DB3\xFF29\xFF2B"), // Leg IK (Japanese: 足ＩＫ)
		TEXT("\u3064\u307E\u5148\xFF29\xFF2B"), // Toe IK (Japanese: つま先ＩＫ)
		TEXT("\u8155\xFF29\xFF2B"), // Arm IK (Japanese: 腕ＩＫ)
		TEXT("\u76EE\xFF29\xFF2B"), // Eye IK (Japanese: 目ＩＫ)
		TEXT("_ik"), TEXT("_IK"),
		TEXT("ik_"), TEXT("IK_"),

		// === Control Bones (Max/Maya/Blender) ===
		TEXT("CTL"), TEXT("CTRL"), TEXT("Control"), TEXT("Ctl"), TEXT("Ctrl"), TEXT("Ctr"),
		TEXT("_ctl"), TEXT("_ctrl"), TEXT("_control"), TEXT("_ctr"),
		TEXT("ctl_"), TEXT("ctrl_"), TEXT("control_"), TEXT("ctr_"),

		// === Helper/Dummy Bones (Max) ===
		TEXT("Helper"), TEXT("Hlp"), TEXT("Dummy"), TEXT("Dmmy"),
		TEXT("_helper"), TEXT("_hlp"), TEXT("_dummy"), TEXT("_dmmy"),
		TEXT("helper_"), TEXT("dummy_"),

		// === Target/Effector Bones (Maya/Unity/Unreal) ===
		TEXT("Target"), TEXT("Tgt"), TEXT("Effector"), TEXT("Eff"),
		TEXT("_target"), TEXT("_tgt"), TEXT("_effector"), TEXT("_eff"),
		TEXT("target_"), TEXT("effector_"),

		// === Pole/Hint Bones (Unity/Unreal) ===
		TEXT("Pole"), TEXT("PoleVector"), TEXT("Hint"),
		TEXT("_pole"), TEXT("_polevector"), TEXT("_hint"),
		TEXT("pole_"), TEXT("hint_"),

		// === Locator/Point (Maya) ===
		TEXT("Locator"), TEXT("Loc"), TEXT("Point"), TEXT("Pt"),
		TEXT("_locator"), TEXT("_loc"), TEXT("_point"), TEXT("_pt"),

		// === Handle (Maya) ===
		TEXT("Handle"), TEXT("Hnd"),
		TEXT("_handle"), TEXT("_hnd"),

		// === Blender Convention ===
		TEXT("MCH-"), TEXT("MCH_"),   // Mechanism/Helper bones
		TEXT("ORG-"), TEXT("ORG_"),   // Original bones (rigging helpers)

		// === Constraint/Reference ===
		TEXT("Constraint"), TEXT("Cns"), TEXT("Reference"), TEXT("Ref"),
		TEXT("_constraint"), TEXT("_cns"), TEXT("_reference"), TEXT("_ref"),

		// === Guide/Master ===
		TEXT("Guide"), TEXT("Gd"), TEXT("Master"), TEXT("Mstr"),
		TEXT("_guide"), TEXT("_gd"), TEXT("_master"), TEXT("_mstr"),

		// === MMD Specific (Japanese) ===
		TEXT("\u30BB\u30F3\u30BF\u30FC"), // Center (センター)
		TEXT("Center"),
		TEXT("\u30B0\u30EB\u30FC\u30D6"), // Groove (グルーブ)
		TEXT("Groove"),
		TEXT("\u64CD\u4F5C"), // Operation (操作)
		TEXT("\u88DC\u52A9"), // Helper (補助)
		TEXT("\u6369"), // Twist (捩)
		TEXT("W"), TEXT("WIK"), TEXT("S"), // MMD helper suffixes
		TEXT("\u5148"), // Tip/End (先) - often for control endpoints

		// === Common Suffixes/Prefixes ===
		TEXT("_root"), TEXT("root_"), TEXT("Root"),
		TEXT("_offset"), TEXT("offset_"),
		TEXT("_aim"), TEXT("aim_"),
		TEXT("_orient"), TEXT("orient_")
	};

	// Check for pattern matches
	for (const FString& Pattern : KinematicPatterns)
	{
		if (BoneName.Contains(Pattern, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	// Additional checks for common patterns
	// Bones ending with numbers followed by control indicators (e.g., "Bone_01_CTL")
	if (BoneName.EndsWith(TEXT("_CTL"), ESearchCase::IgnoreCase) ||
		BoneName.EndsWith(TEXT("_CTRL"), ESearchCase::IgnoreCase) ||
		BoneName.EndsWith(TEXT("_IK"), ESearchCase::IgnoreCase))
	{
		return true;
	}

	return false;
}

bool FPmxPhysicsBuilder::IsStandardBone(const FString& BoneName)
{
	// Standard skeletal bones (core body/limbs) based on PMX_MMD_StandardBones.md
	// These bones are typically used for character deformation/animation
	// Excludes physics-only bones (cloth, hair, accessories)

	static const TArray<FString> StandardBonePatterns = {
		// Core/Center (Japanese)
		TEXT("センター"), TEXT("グルーブ"), TEXT("下半身"), TEXT("上半身"),
		TEXT("上半身2"), TEXT("首"), TEXT("頭"), TEXT("両目"), TEXT("左目"), TEXT("右目"),

		// Core/Center (English variants)
		TEXT("Center"), TEXT("Groove"), TEXT("LowerBody"), TEXT("Pelvis"),
		TEXT("UpperBody"), TEXT("Spine"), TEXT("Neck"), TEXT("Head"),
		TEXT("Eyes"), TEXT("Eye_L"), TEXT("Eye_R"),

		// Arms/Hands (Japanese)
		TEXT("肩"), TEXT("腕"), TEXT("ひじ"), TEXT("肘"), TEXT("手首"), TEXT("手"),
		TEXT("左肩"), TEXT("左腕"), TEXT("左ひじ"), TEXT("左肘"), TEXT("左手首"), TEXT("左手"),
		TEXT("右肩"), TEXT("右腕"), TEXT("右ひじ"), TEXT("右肘"), TEXT("右手首"), TEXT("右手"),

		// Arms/Hands (English variants)
		TEXT("Shoulder"), TEXT("Clavicle"),
		TEXT("UpperArm"), TEXT("Arm"),
		TEXT("LowerArm"), TEXT("Forearm"), TEXT("Elbow"),
		TEXT("Wrist"), TEXT("Hand"),

		// Fingers (Japanese - with variants)
		TEXT("親指"), TEXT("人指"), TEXT("人差指"), TEXT("中指"), TEXT("薬指"), TEXT("小指"),

		// Fingers (English variants)
		TEXT("Thumb"), TEXT("Index"), TEXT("Middle"), TEXT("Ring"), TEXT("Pinky"), TEXT("Little"),

		// Legs/Feet (Japanese)
		TEXT("足"), TEXT("ひざ"), TEXT("膝"), TEXT("足首"), TEXT("つま先"),
		TEXT("左足"), TEXT("左ひざ"), TEXT("左膝"), TEXT("左足首"), TEXT("左つま先"),
		TEXT("右足"), TEXT("右ひざ"), TEXT("右膝"), TEXT("右足首"), TEXT("右つま先"),

		// Legs/Feet (English variants)
		TEXT("UpperLeg"), TEXT("Thigh"),
		TEXT("LowerLeg"), TEXT("Calf"), TEXT("Knee"),
		TEXT("Ankle"), TEXT("Foot"),
		TEXT("Toe"), TEXT("Ball")
	};

	// Check for exact matches or contains patterns
	for (const FString& Pattern : StandardBonePatterns)
	{
		if (BoneName.Contains(Pattern, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

void FPmxPhysicsBuilder::SetupCollisionFiltering(USkeletalBodySetup* Body, const FPmxRigidBody& RB)
{
	FBodyInstance& BI = Body->DefaultInstance;

	// Use PhysicsBody collision profile as default
	BI.SetObjectType(ECollisionChannel::ECC_PhysicsBody);
	BI.SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);

	// Log non-collision group for future expansion
	if (RB.NonCollisionGroup != 0)
	{
		UE_LOG(LogPMXImporter, Verbose,
			TEXT("RigidBody '%s': Group=%d, NonCollisionMask=0x%04X (not fully mapped)"),
			*RB.Name, RB.Group, RB.NonCollisionGroup);
	}
}

bool FPmxPhysicsBuilder::IsRigidBodyConnectedToConstraint(int32 RigidBodyIndex, const TArray<FPmxJoint>& Joints)
{
	for (const FPmxJoint& Joint : Joints)
	{
		if (Joint.RigidBodyIndexA == RigidBodyIndex || Joint.RigidBodyIndexB == RigidBodyIndex)
		{
			return true;
		}
	}
	return false;
}

bool FPmxPhysicsBuilder::IsChainRoot(int32 RigidBodyIndex, const TArray<FPmxJoint>& Joints)
{
	bool bUsedAsA = false;

	for (const FPmxJoint& Joint : Joints)
	{
		// If used as B (child), this is not a root
		if (Joint.RigidBodyIndexB == RigidBodyIndex)
		{
			return false;
		}
		// Track if used as A (parent)
		if (Joint.RigidBodyIndexA == RigidBodyIndex)
		{
			bUsedAsA = true;
		}
	}

	// Chain root = used as A (parent) but never as B (child)
	return bUsedAsA;
}

bool FPmxPhysicsBuilder::IsChainEnd(int32 RigidBodyIndex, const TArray<FPmxJoint>& Joints)
{
	bool bUsedAsB = false;

	for (const FPmxJoint& Joint : Joints)
	{
		// If used as A (parent), this is not an end
		if (Joint.RigidBodyIndexA == RigidBodyIndex)
		{
			return false;
		}
		// Track if used as B (child)
		if (Joint.RigidBodyIndexB == RigidBodyIndex)
		{
			bUsedAsB = true;
		}
	}

	// Chain end = used as B (child) but never as A (parent)
	return bUsedAsB;
}
