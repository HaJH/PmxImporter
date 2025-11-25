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

	UE_LOG(LogPMXImporter, Display, TEXT("BuildPhysicsAsset: SkeletalMesh has %d bones, PMX has %d bones"),
		RefSkel.GetNum(), PhysicsData.Bones.Num());

	UE_LOG(LogPMXImporter, Display, TEXT("Building PhysicsAsset with %d rigid bodies and %d joints"),
		PhysicsData.RigidBodies.Num(), PhysicsData.Joints.Num());

	// Validate RigidBody -> Bone Mapping
	int32 InvalidRBBoneRefs = 0;
	for (int32 i = 0; i < PhysicsData.RigidBodies.Num(); ++i)
	{
		const FPmxRigidBody& RBCheck = PhysicsData.RigidBodies[i];
		if (RBCheck.RelatedBoneIndex < 0 || RBCheck.RelatedBoneIndex >= PhysicsData.Bones.Num())
		{
			++InvalidRBBoneRefs;
		}
	}
	UE_LOG(LogPMXImporter, Display, TEXT("  Total RigidBodies: %d, Invalid bone refs: %d"),
		PhysicsData.RigidBodies.Num(), InvalidRBBoneRefs);

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
			PhysicsAsset, RB, Bone, RefSkel, PhysicsData);

		if (BodySetup)
		{
			RigidBodyToBodySetup.Add(RBIndex, BodySetup);
			++CreatedBodies;
		}
	}

	// Update body index maps
	PhysicsAsset->UpdateBodySetupIndexMap();
	PhysicsAsset->UpdateBoundsBodiesArray();

	// 요약 통계 로그
	int32 SkippedRigidBodies = PhysicsData.RigidBodies.Num() - CreatedBodies;
	if (SkippedRigidBodies > 0)
	{
		UE_LOG(LogPMXImporter, Display,
			TEXT("Created %d BodySetups (%d/%d valid RigidBodies, %d skipped due to invalid bone references)"),
			CreatedBodies, CreatedBodies, PhysicsData.RigidBodies.Num(), SkippedRigidBodies);
	}
	else
	{
		UE_LOG(LogPMXImporter, Display, TEXT("Created %d BodySetups"), CreatedBodies);
	}

	// Create Constraints for each Joint
	int32 CreatedConstraints = 0;
	for (int32 JointIndex = 0; JointIndex < PhysicsData.Joints.Num(); ++JointIndex)
	{
		const FPmxJoint& Joint = PhysicsData.Joints[JointIndex];

		// Validate rigid body indices
		USkeletalBodySetup** BodyAPtr = RigidBodyToBodySetup.Find(Joint.RigidBodyIndexA);
		USkeletalBodySetup** BodyBPtr = RigidBodyToBodySetup.Find(Joint.RigidBodyIndexB);

		if (!BodyAPtr || !BodyBPtr)
		{
			UE_LOG(LogPMXImporter, Warning,
				TEXT("Skipping Joint '%s': One or both bodies not found (A=%d, B=%d)"),
				*Joint.Name, Joint.RigidBodyIndexA, Joint.RigidBodyIndexB);
			continue;
		}

		UPhysicsConstraintTemplate* Constraint = CreateConstraint(
			PhysicsAsset, Joint, PhysicsData.RigidBodies, PhysicsData.Bones, PhysicsData);

		if (Constraint)
		{
			++CreatedConstraints;
		}
	}

	UE_LOG(LogPMXImporter, Display, TEXT("Created %d Constraints"), CreatedConstraints);

	// Final update
	PhysicsAsset->UpdateBodySetupIndexMap();

	return CreatedBodies > 0;
}

USkeletalBodySetup* FPmxPhysicsBuilder::CreateBodySetup(
	UPhysicsAsset* Asset,
	const FPmxRigidBody& RB,
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
		UE_LOG(LogPMXImporter, Warning,
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
		UE_LOG(LogPMXImporter, Warning,
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
	switch (RB.Shape)
	{
	case 0: // Sphere
		SetupSphereShape(AggGeom, RB, PhysicsData.Scale);
		break;
	case 1: // Box
		SetupBoxShape(AggGeom, RB, PhysicsData.Scale);
		break;
	case 2: // Capsule
		SetupCapsuleShape(AggGeom, RB, PhysicsData.Scale);
		break;
	default:
		UE_LOG(LogPMXImporter, Warning,
			TEXT("CreateBodySetup: Unknown shape type %d for RigidBody '%s', using sphere"),
			RB.Shape, *RB.Name);
		SetupSphereShape(AggGeom, RB, PhysicsData.Scale);
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
	auto SetLinearMotion = [](float Min, float Max) -> ELinearConstraintMotion
	{
		const float Tolerance = 0.001f;
		if (FMath::Abs(Min) < Tolerance && FMath::Abs(Max) < Tolerance)
		{
			return ELinearConstraintMotion::LCM_Locked;
		}
		return ELinearConstraintMotion::LCM_Limited;
	};

	CI.SetLinearXMotion(SetLinearMotion(MinMove.X, MaxMove.X));
	CI.SetLinearYMotion(SetLinearMotion(MinMove.Y, MaxMove.Y));
	CI.SetLinearZMotion(SetLinearMotion(MinMove.Z, MaxMove.Z));

	// Set linear limits
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
	CI.SetAngularTwistMotion(SetAngularMotion(MinRotDeg.X, MaxRotDeg.X));
	CI.SetAngularSwing1Motion(SetAngularMotion(MinRotDeg.Y, MaxRotDeg.Y));
	CI.SetAngularSwing2Motion(SetAngularMotion(MinRotDeg.Z, MaxRotDeg.Z));

	// Set angular limits
	const float TwistLimit = FMath::Max(FMath::Abs(MinRotDeg.X), FMath::Abs(MaxRotDeg.X));
	const float Swing1Limit = FMath::Max(FMath::Abs(MinRotDeg.Y), FMath::Abs(MaxRotDeg.Y));
	const float Swing2Limit = FMath::Max(FMath::Abs(MinRotDeg.Z), FMath::Abs(MaxRotDeg.Z));

	CI.ProfileInstance.ConeLimit.Swing1Motion = SetAngularMotion(MinRotDeg.Y, MaxRotDeg.Y);
	CI.ProfileInstance.ConeLimit.Swing2Motion = SetAngularMotion(MinRotDeg.Z, MaxRotDeg.Z);
	CI.ProfileInstance.ConeLimit.Swing1LimitDegrees = Swing1Limit;
	CI.ProfileInstance.ConeLimit.Swing2LimitDegrees = Swing2Limit;
	CI.ProfileInstance.TwistLimit.TwistMotion = SetAngularMotion(MinRotDeg.X, MaxRotDeg.X);
	CI.ProfileInstance.TwistLimit.TwistLimitDegrees = TwistLimit;

	// Setup spring (for JointType 0 = Spring 6DOF)
	if (Joint.JointType == 0)
	{
		// Linear spring
		const FVector SpringMove = ConvertVectorPmxToUE(Joint.SpringMoveCoefficient, 1.0f); // Spring coefficients not scaled
		CI.ProfileInstance.LinearDrive.XDrive.bEnablePositionDrive = SpringMove.X > 0.0f;
		CI.ProfileInstance.LinearDrive.XDrive.Stiffness = SpringMove.X;
		CI.ProfileInstance.LinearDrive.YDrive.bEnablePositionDrive = SpringMove.Y > 0.0f;
		CI.ProfileInstance.LinearDrive.YDrive.Stiffness = SpringMove.Y;
		CI.ProfileInstance.LinearDrive.ZDrive.bEnablePositionDrive = SpringMove.Z > 0.0f;
		CI.ProfileInstance.LinearDrive.ZDrive.Stiffness = SpringMove.Z;

		// Angular spring
		const FVector SpringRot = FVector(
			Joint.SpringRotationCoefficient.X,
			Joint.SpringRotationCoefficient.Y,
			Joint.SpringRotationCoefficient.Z);
		const float AvgSpringRot = (SpringRot.X + SpringRot.Y + SpringRot.Z) / 3.0f;

		CI.ProfileInstance.AngularDrive.SlerpDrive.bEnablePositionDrive = AvgSpringRot > 0.0f;
		CI.ProfileInstance.AngularDrive.SlerpDrive.Stiffness = AvgSpringRot;
	}

	// Set joint position (relative to parent body)
	const FVector JointPos = ConvertVectorPmxToUE(Joint.Position, PhysicsData.Scale);
	const FRotator JointRot = ConvertRotationPmxToUE(Joint.Rotation);

	CI.SetRefPosition(EConstraintFrame::Frame1, JointPos);
	CI.SetRefPosition(EConstraintFrame::Frame2, JointPos);

	// UE 5.7: SetRefOrientation requires PriAxis and SecAxis vectors
	const FQuat JointQuat = JointRot.Quaternion().GetNormalized();
	const FVector PriAxis = JointQuat.GetAxisX();
	const FVector SecAxis = JointQuat.GetAxisY();
	CI.SetRefOrientation(EConstraintFrame::Frame1, PriAxis, SecAxis);
	CI.SetRefOrientation(EConstraintFrame::Frame2, PriAxis, SecAxis);

	// Add to physics asset
	Asset->ConstraintSetup.Add(Constraint);

	UE_LOG(LogPMXImporter, Verbose,
		TEXT("Created Constraint '%s' (Bone1: %s, Bone2: %s)"),
		*Joint.Name, *ActualBoneNameA.ToString(), *ActualBoneNameB.ToString());

	return Constraint;
}

void FPmxPhysicsBuilder::SetupSphereShape(FKAggregateGeom& Geom, const FPmxRigidBody& RB, float Scale)
{
	FKSphereElem Sphere;
	Sphere.Radius = RB.Size.X * Scale;
	Geom.SphereElems.Add(Sphere);
}

void FPmxPhysicsBuilder::SetupBoxShape(FKAggregateGeom& Geom, const FPmxRigidBody& RB, float Scale)
{
	FKBoxElem Box;
	// Box: Apply XZY axis swap (from mmd_tools analysis)
	Box.X = RB.Size.X * Scale * 2.0f;
	Box.Y = RB.Size.Z * Scale * 2.0f;
	Box.Z = RB.Size.Y * Scale * 2.0f;
	Geom.BoxElems.Add(Box);
}

void FPmxPhysicsBuilder::SetupCapsuleShape(FKAggregateGeom& Geom, const FPmxRigidBody& RB, float Scale)
{
	FKSphylElem Capsule;
	Capsule.Radius = RB.Size.X * Scale;
	Capsule.Length = RB.Size.Y * Scale;
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
	// Transform: (x, y, z) -> (x, z, y) then flip Z for handedness
	return FVector(PmxVector.X * Scale, -PmxVector.Z * Scale, PmxVector.Y * Scale);
}

FRotator FPmxPhysicsBuilder::ConvertRotationPmxToUE(const FVector3f& PmxRotation)
{
	// PMX stores Euler angles in radians (X, Y, Z order)
	// Convert to degrees and apply coordinate transformation
	const float PitchDeg = FMath::RadiansToDegrees(PmxRotation.X);
	const float YawDeg = FMath::RadiansToDegrees(PmxRotation.Y);
	const float RollDeg = FMath::RadiansToDegrees(PmxRotation.Z);

	// Apply coordinate system transformation
	// PMX XYZ -> UE Pitch/Yaw/Roll mapping with handedness flip
	return FRotator(PitchDeg, -YawDeg, -RollDeg);
}

FQuat FPmxPhysicsBuilder::ConvertQuaternionPmxToUE(const FQuat4f& PmxQuat)
{
	// Apply coordinate transformation to quaternion
	return FQuat(PmxQuat.X, -PmxQuat.Z, PmxQuat.Y, PmxQuat.W);
}

bool FPmxPhysicsBuilder::ShouldForceKinematic(const FString& BoneName)
{
	// IK bones and control bones should be kinematic
	static const TArray<FString> KinematicPatterns = {
		TEXT("IK"),
		TEXT("\xFF29\xFF2B"), // Full-width IK
		TEXT("\u8DB3\xFF29\xFF2B"), // Leg IK (Japanese)
		TEXT("\u3064\u307E\u5148\xFF29\xFF2B"), // Toe IK (Japanese)
		TEXT("\u8155\xFF29\xFF2B"), // Arm IK (Japanese)
		TEXT("\u76EE\xFF29\xFF2B"), // Eye IK (Japanese)
		TEXT("\u30BB\u30F3\u30BF\u30FC"), // Center (Japanese)
		TEXT("Center"),
		TEXT("\u30B0\u30EB\u30FC\u30D6"), // Groove (Japanese)
		TEXT("Groove"),
		TEXT("_dummy"),
		TEXT("_target"),
		TEXT("_ik")
	};

	for (const FString& Pattern : KinematicPatterns)
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
