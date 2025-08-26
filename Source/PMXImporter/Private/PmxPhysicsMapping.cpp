// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#include "PmxPhysicsMapping.h"

#include "PmxStructs.h"
#include "InterchangeSceneNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "HAL/IConsoleManager.h"
#include "LogPMXImporter.h"
#include "PmxImporterSettings.h"

#if WITH_EDITOR
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#endif

namespace
{
	// Import scale (uniform) reader. We must not define the CVar here because of Unity builds; instead, query by name.
	static float GetPMXImporterScale()
	{
		float Baseline = 1.0f;
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("PMXImporter.Scale")))
		{
			Baseline = FMath::Max(0.0001f, Var->GetFloat());
		}
		// Default fallback if the CVars are not registered yet
		return FMath::Max(0.0001f, Baseline);
	}

	// Clamp helpers (stability first)
	template<typename T> static T ClampFinite(T V, T MinV, T MaxV, T Fallback = (T)0)
	{
		if (!FMath::IsFinite((double)V)) return Fallback;
		return FMath::Clamp(V, MinV, MaxV);
	}

	static FVector ClampFiniteVec(const FVector& V, double AbsMax = 100000.0)
	{
		if (!FMath::IsFinite(V.X) || !FMath::IsFinite(V.Y) || !FMath::IsFinite(V.Z))
		{
			return FVector::ZeroVector;
		}
		const FVector Abs = V.GetAbs();
		if (Abs.X > AbsMax || Abs.Y > AbsMax || Abs.Z > AbsMax)
		{
			return FVector::ZeroVector;
		}
		return V;
	}

	// Minimal bone role classification based on docs/PMX_MMD_StandardBones.md and docs/PMX_BoneNameCandidates.md
	// Returns true if the bone is a standard animation bone (skin/retarget core), false if it is likely a physics/IK/helper chain.
	static bool IsLikelyStandardAnimationBone(const FString& InBoneName)
	{
		if (InBoneName.IsEmpty()) return false;

		// Lowercase copy for ASCII tokens; we will also check JP tokens with original string
		FString Name = InBoneName;
		FString NameLower = Name.ToLower();

		// Quick negative filters (IK/Helper/Twist/Accessory/Physics-only chains)
		static const TCHAR* NonStandardJP[] = {
			TEXT("ＩＫ"), TEXT("IK"), TEXT("補助"), TEXT("捩"), TEXT("先EX"), TEXT("ダミー"), TEXT("髪"), TEXT("スカート"), TEXT("袖"), TEXT("リボン"), TEXT("先")
		};
		static const TCHAR* NonStandardEN[] = {
			TEXT("ik"), TEXT("helper"), TEXT("twist"), TEXT("assist"), TEXT("socket"), TEXT("accessory"), TEXT("hair"), TEXT("skirt"), TEXT("ribbon"), TEXT("dummy")
		};
		for (const TCHAR* Tok : NonStandardJP)
		{
			if (Name.Contains(Tok)) return false;
		}
		for (const TCHAR* Tok : NonStandardEN)
		{
			if (NameLower.Contains(Tok)) return false;
		}

		// Positive indicators of standard skeleton bones
		static const TCHAR* StandardJP[] = {
			TEXT("センター"), TEXT("グルーブ"), TEXT("下半身"), TEXT("上半身"), TEXT("首"), TEXT("頭"), TEXT("両目"), TEXT("左目"), TEXT("右目"),
			TEXT("肩"), TEXT("腕"), TEXT("ひじ"), TEXT("手首"),
			TEXT("親指"), TEXT("人指"), TEXT("人差指"), TEXT("中指"), TEXT("薬指"), TEXT("小指"),
			TEXT("足"), TEXT("ひざ"), TEXT("足首"), TEXT("つま先")
		};
		static const TCHAR* StandardEN[] = {
			TEXT("center"), TEXT("groove"), TEXT("lowerbody"), TEXT("upperbody"), TEXT("upperbody2"), TEXT("neck"), TEXT("head"), TEXT("eye"),
			TEXT("shoulder"), TEXT("clavicle"), TEXT("upperarm"), TEXT("lowerarm"), TEXT("elbow"), TEXT("wrist"), TEXT("hand"),
			TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky"), TEXT("little"),
			TEXT("thigh"), TEXT("calf"), TEXT("knee"), TEXT("ankle"), TEXT("toe"), TEXT("foot")
		};
		for (const TCHAR* Tok : StandardJP)
		{
			if (Name.Contains(Tok)) return true;
		}
		for (const TCHAR* Tok : StandardEN)
		{
			if (NameLower.Contains(Tok)) return true;
		}

		// Fallback: unknown → treat as non-standard (to avoid disabling physics for intended sim bones)
		return false;
	}
}

using namespace PmxPhysics;

FAnnotateResult PmxPhysics::AnnotatePhysicsNodes(const FPmxModel& Model, UInterchangeBaseNodeContainer& Container)
{
	FAnnotateResult Result;

	// Create root physics folder nodes
	UInterchangeSceneNode* PhysicsRoot = NewObject<UInterchangeSceneNode>(&Container);
	const FString PhysicsRootUid = TEXT("/PMX/Physics");
	PhysicsRoot->InitializeNode(PhysicsRootUid, TEXT("Physics"), EInterchangeNodeContainerType::TranslatedScene);
	Container.AddNode(PhysicsRoot);

	UInterchangeSceneNode* BodiesRoot = NewObject<UInterchangeSceneNode>(&Container);
	const FString BodiesRootUid = TEXT("/PMX/Physics/Bodies");
	BodiesRoot->InitializeNode(BodiesRootUid, TEXT("Bodies"), EInterchangeNodeContainerType::TranslatedScene);
	Container.AddNode(BodiesRoot);
	Container.SetNodeParentUid(BodiesRootUid, PhysicsRootUid);

	UInterchangeSceneNode* JointsRoot = NewObject<UInterchangeSceneNode>(&Container);
	const FString JointsRootUid = TEXT("/PMX/Physics/Joints");
	JointsRoot->InitializeNode(JointsRootUid, TEXT("Joints"), EInterchangeNodeContainerType::TranslatedScene);
	Container.AddNode(JointsRoot);
	Container.SetNodeParentUid(JointsRootUid, PhysicsRootUid);

	// Transform policy: rotate X +90deg then scale
	const FQuat RotX = FQuat(FVector3d::XAxisVector, FMath::DegreesToRadians(90.0f));
	const float ImportScale = GetPMXImporterScale();
	const FTransform PMXToUE_X(RotX);
	const FTransform PMXToUE_S(FQuat::Identity, FVector3d::ZeroVector, FVector3d(ImportScale));
	const FTransform PMXToUE = PMXToUE_X * PMXToUE_S;

	// Bodies
	for (int32 i = 0; i < Model.RigidBodies.Num(); ++i)
	{
		const FPmxRigidBody& RB = Model.RigidBodies[i];
		UInterchangeSceneNode* BodyNode = NewObject<UInterchangeSceneNode>(&Container);
		const FString BodyUid = FString::Printf(TEXT("/PMX/Physics/Bodies/Body_%d"), i);
		const FVector SizeUE = ClampFiniteVec(PMXToUE.TransformVector(FVector((double)RB.Size.X, (double)RB.Size.Y, (double)RB.Size.Z)));
		const float MassClamped = ClampFinite(RB.Mass, 0.0f, 100000.0f, 1.0f);
		const float LinDampClamped = ClampFinite(RB.MoveAttenuation, 0.0f, 1000.0f, 0.0f);
		const float AngDampClamped = ClampFinite(RB.RotationAttenuation, 0.0f, 1000.0f, 0.0f);
		const float RestClamped = ClampFinite(RB.Repulsion, 0.0f, 1.0f, 0.0f);
		const float FricClamped = ClampFinite(RB.Friction, 0.0f, 10.0f, 0.5f);
		FString LabelBase = RB.Name.IsEmpty() ? FString::Printf(TEXT("Body_%d"), i) : RB.Name;
		FString Label = FString::Printf(TEXT("%s [shape=%d size=(%.2f,%.2f,%.2f) mass=%.2f]"), *LabelBase, (int32)RB.Shape, (float)SizeUE.X, (float)SizeUE.Y, (float)SizeUE.Z, MassClamped);
		BodyNode->InitializeNode(BodyUid, *Label, EInterchangeNodeContainerType::TranslatedScene);
		Container.AddNode(BodyNode);
		Container.SetNodeParentUid(BodyUid, BodiesRootUid);

		// Position/Rotation in UE space
		const FVector P((double)RB.Position.X, (double)RB.Position.Y, (double)RB.Position.Z);
		const FVector RadianEuler((double)RB.Rotation.X, (double)RB.Rotation.Y, (double)RB.Rotation.Z);
		const FVector PosUE = PMXToUE.TransformPosition(P);
		const FRotator RotUE = (PMXToUE * FTransform(FRotator::MakeFromEuler(FVector(RadianEuler) * 180.0 / PI))).GetRotation().Rotator();

		// Store transform onto the node (location/rotation only)
		FTransform LocalXform = FTransform::Identity;
		LocalXform.SetLocation(PosUE);
		LocalXform.SetRotation(RotUE.Quaternion());
		BodyNode->AddSpecializedType(TEXT("PMXRigidBody"));
		BodyNode->SetCustomLocalTransform(&Container, LocalXform);

		++Result.RigidBodyCount;
	}

	// Joints
	for (int32 j = 0; j < Model.Joints.Num(); ++j)
	{
		const FPmxJoint& J = Model.Joints[j];
		UInterchangeSceneNode* JointNode = NewObject<UInterchangeSceneNode>(&Container);
		const FString JointUid = FString::Printf(TEXT("/PMX/Physics/Joints/Joint_%d"), j);
		FString Label = J.Name.IsEmpty() ? FString::Printf(TEXT("Joint_%d"), j) : J.Name;
		JointNode->InitializeNode(JointUid, *Label, EInterchangeNodeContainerType::TranslatedScene);
		Container.AddNode(JointNode);
		Container.SetNodeParentUid(JointUid, JointsRootUid);

		// Position/rotation in UE space
		const FVector JP((double)J.Position.X, (double)J.Position.Y, (double)J.Position.Z);
		const FVector JR((double)J.Rotation.X, (double)J.Rotation.Y, (double)J.Rotation.Z); // radians
		const FVector PosUE = PMXToUE.TransformPosition(JP);
		const FRotator RotUE = (PMXToUE * FTransform(FRotator::MakeFromEuler(FVector(JR) * 180.0 / PI))).GetRotation().Rotator();

		// Limits/springs (clamped) — computed for potential later use (not stored due to attribute API limits here)
		const FVector LMin = ClampFiniteVec(PMXToUE.TransformVector(FVector((double)J.MoveRestrictionMin.X, (double)J.MoveRestrictionMin.Y, (double)J.MoveRestrictionMin.Z)));
		const FVector LMax = ClampFiniteVec(PMXToUE.TransformVector(FVector((double)J.MoveRestrictionMax.X, (double)J.MoveRestrictionMax.Y, (double)J.MoveRestrictionMax.Z)));
		const FVector RMin = ClampFiniteVec(FVector((double)J.RotationRestrictionMin.X, (double)J.RotationRestrictionMin.Y, (double)J.RotationRestrictionMin.Z)); // radians
		const FVector RMax = ClampFiniteVec(FVector((double)J.RotationRestrictionMax.X, (double)J.RotationRestrictionMax.Y, (double)J.RotationRestrictionMax.Z));
		const FVector PSpring = ClampFiniteVec(PMXToUE.TransformVector(FVector((double)J.SpringMoveCoefficient.X, (double)J.SpringMoveCoefficient.Y, (double)J.SpringMoveCoefficient.Z)), 1e6);
		const FVector RSpring = ClampFiniteVec(FVector((double)J.SpringRotationCoefficient.X, (double)J.SpringRotationCoefficient.Y, (double)J.SpringRotationCoefficient.Z), 1e6);

		// Mark specialized type and set transform; encode brief info in label for now
		JointNode->AddSpecializedType(TEXT("PMXJoint"));
		FTransform LocalXform = FTransform::Identity;
		LocalXform.SetLocation(PosUE);
		LocalXform.SetRotation(RotUE.Quaternion());
		JointNode->SetCustomLocalTransform(&Container, LocalXform);

		// Update label to include link info
		FString NewLabel = FString::Printf(TEXT("%s [type=%d A=%d B=%d]"), *Label, (int32)J.JointType, J.RigidBodyIndexA, J.RigidBodyIndexB);
		JointNode->SetDisplayLabel(NewLabel);

		++Result.JointCount;
	}

	UE_LOG(LogPMXImporter, Display, TEXT("PMX Physics Annotated: RigidBodies=%d, Joints=%d"), Result.RigidBodyCount, Result.JointCount);
	return Result;
}

FCreateResult PmxPhysics::CreatePhysicsAssetFromPMX(const FPmxModel& Model, UPhysicsAsset* PhysicsAsset, USkeletalMesh* SkeletalMesh)
{
	FCreateResult Result;
	Result.bSuccess = false;

#if WITH_EDITOR
	if (!PhysicsAsset || !SkeletalMesh)
	{
		UE_LOG(LogPMXImporter, Error, TEXT("PMX Physics: Invalid PhysicsAsset or SkeletalMesh"));
		return Result;
	}

	const FReferenceSkeleton& RefSkel = SkeletalMesh->GetRefSkeleton();
	if (RefSkel.GetNum() == 0)
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("PMX Physics: SkeletalMesh has empty RefSkeleton"));
	}
	else if (Model.RigidBodies.Num() > RefSkel.GetNum())
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("PMX Physics: PMX RigidBodies(%d) exceed RefSkeleton bones(%d). Some bodies may map to root or be invalid."), Model.RigidBodies.Num(), RefSkel.GetNum());
	}

	// If there is no physics data in the PMX model, do not touch existing PhysicsAsset (keep auto-generated content)
	if (Model.RigidBodies.Num() == 0)
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("PMX Physics: No RigidBodies in PMX model (Joints=%d). Skipping PhysicsAsset rebuild to preserve existing content."), Model.Joints.Num());
		Result.bSuccess = true; // Not an error; intentional no-op
		return Result;
	}

	// PMX->UE coordinate transform (same as geometry)
	const FQuat RotX = FQuat(FVector3d::XAxisVector, FMath::DegreesToRadians(90.0f));
	const float ImportScale = GetPMXImporterScale();
	const FTransform PMXToUE_X(RotX);
	const FTransform PMXToUE_S(FQuat::Identity, FVector3d::ZeroVector, FVector3d(ImportScale));
	const FTransform PMXToUE = PMXToUE_X * PMXToUE_S;
	UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Physics: ImportScale=%.6f, RefSkeletonBones=%d, PMX Bodies=%d, PMX Joints=%d"), ImportScale, RefSkel.GetNum(), Model.RigidBodies.Num(), Model.Joints.Num());

	// 1) Reset existing bodies and constraints
	UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Physics: Clearing existing asset contents (Bodies=%d, Constraints=%d)"), PhysicsAsset->SkeletalBodySetups.Num(), PhysicsAsset->ConstraintSetup.Num());
	PhysicsAsset->SkeletalBodySetups.Empty();
	PhysicsAsset->ConstraintSetup.Empty();

	// Helper lambda to fetch bone name safely
	auto GetBoneNameSafe = [&RefSkel](int32 BoneIndex) -> FName
	{
		if (BoneIndex >= 0 && BoneIndex < RefSkel.GetNum())
		{
			return RefSkel.GetBoneName(BoneIndex);
		}
		// Fallback to root if available
		return RefSkel.GetNum() > 0 ? RefSkel.GetBoneName(0) : NAME_None;
	};

	// 2) Create bodies from PMX rigid bodies
	TArray<FName> BodyIndexToBoneName;
	BodyIndexToBoneName.SetNum(Model.RigidBodies.Num());

	// Helper to get component-space transform of a ref-pose bone (accumulate to root)
	auto GetBoneCSTransform = [&RefSkel](int32 BoneIndex) -> FTransform
	{
		FTransform CS = FTransform::Identity;
		int32 Idx = BoneIndex;
		while (Idx != INDEX_NONE)
		{
			const FTransform& Local = RefSkel.GetRefBonePose()[Idx];
			CS = Local * CS;
			Idx = RefSkel.GetParentIndex(Idx);
		}
		return CS;
	};

	for (int32 i = 0; i < Model.RigidBodies.Num(); ++i)
	{
		const FPmxRigidBody& RB = Model.RigidBodies[i];

		const FName BoneName = GetBoneNameSafe(RB.RelatedBoneIndex);
		BodyIndexToBoneName[i] = BoneName;
		if (BoneName.IsNone())
		{
			UE_LOG(LogPMXImporter, Warning, TEXT("PMX Physics: Body[%d] has invalid bone index %d; mapping to None/root"), i, RB.RelatedBoneIndex);
		}

  USkeletalBodySetup* BodySetup = NewObject<USkeletalBodySetup>(PhysicsAsset, USkeletalBodySetup::StaticClass(), NAME_None, RF_Transactional);
		BodySetup->BoneName = BoneName;
		BodySetup->bConsiderForBounds = true;

		// Classify bone role and set physics type accordingly: standard animation bones → Kinematic (disable simulation)
		const bool bStandard = IsLikelyStandardAnimationBone(BoneName.ToString());
		BodySetup->PhysicsType = bStandard ? EPhysicsType::PhysType_Kinematic : EPhysicsType::PhysType_Default;
		UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Physics: Body[%d] Bone=%s → PhysicsType=%s"), i, *BoneName.ToString(), bStandard ? TEXT("Kinematic(Standard)") : TEXT("Simulated(Physics)"));

		// Shape mapping
		const FVector PMXSize((double)RB.Size.X, (double)RB.Size.Y, (double)RB.Size.Z);
		const FVector SizeUE = ClampFiniteVec(PMXToUE.TransformVector(PMXSize));
		// Compute center/rotation in UE mesh space
		const FVector PMXPos((double)RB.Position.X, (double)RB.Position.Y, (double)RB.Position.Z);
		const FVector PMXRotRad((double)RB.Rotation.X, (double)RB.Rotation.Y, (double)RB.Rotation.Z);
		const FVector CenterUE = PMXToUE.TransformPosition(PMXPos);
		const FQuat RotUEQuat = (PMXToUE * FTransform(FRotator::MakeFromEuler(FVector(PMXRotRad) * 180.0 / PI))).GetRotation();
		const FRotator RotUE = RotUEQuat.Rotator();

		// Convert to bone local (PhysicsAsset expects shapes in bone local ref-pose space)
		const int32 BoneIndex = (RB.RelatedBoneIndex >= 0 && RB.RelatedBoneIndex < RefSkel.GetNum()) ? RB.RelatedBoneIndex : 0;
		const FTransform BoneCS = (RefSkel.GetNum() > 0) ? GetBoneCSTransform(BoneIndex) : FTransform::Identity;
		const FVector CenterLocal = BoneCS.InverseTransformPosition(CenterUE);
		const FQuat RotLocalQuat = BoneCS.InverseTransformRotation(RotUEQuat);
		const FRotator RotLocal = RotLocalQuat.Rotator();
		const FVector SizeLocal = BoneCS.InverseTransformVector(SizeUE);

		// Additional X-axis rotation (+90 deg) to match mesh convention
		const FQuat ExtraXRot = FQuat(FVector3d::XAxisVector, FMath::DegreesToRadians(90.0f));
		const FQuat RotLocalAdjQuat = ExtraXRot * RotLocalQuat;
		const FRotator RotLocalAdj = RotLocalAdjQuat.Rotator();

		FKAggregateGeom& AggGeom = BodySetup->AggGeom;
		UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Physics: Create Body[%d] Bone=%s Shape=%d SizeUE=(%.3f,%.3f,%.3f) CenterUE=(%.3f,%.3f,%.3f) RotUE=(%.1f,%.1f,%.1f) -> Local Center=(%.3f,%.3f,%.3f) RotLocal=(%.1f,%.1f,%.1f) SizeLocal=(%.3f,%.3f,%.3f)"),
			i, *BoneName.ToString(), (int32)RB.Shape, SizeUE.X, SizeUE.Y, SizeUE.Z, CenterUE.X, CenterUE.Y, CenterUE.Z, RotUE.Roll, RotUE.Pitch, RotUE.Yaw,
			CenterLocal.X, CenterLocal.Y, CenterLocal.Z, RotLocal.Roll, RotLocal.Pitch, RotLocal.Yaw, SizeLocal.X, SizeLocal.Y, SizeLocal.Z);

		switch (RB.Shape)
		{
			case 0: // Sphere
			{
				FKSphereElem Sphere;
				Sphere.Center = CenterLocal;
				// PMX sphere size is already a radius in each axis after transform; do not halve it.
				const FVector AbsSize = SizeLocal.GetAbs();
				Sphere.Radius = FMath::Max(0.1f, (float)FMath::Max3(AbsSize.X, AbsSize.Y, AbsSize.Z));
				AggGeom.SphereElems.Add(Sphere);
				break;
			}
			case 1: // Box
			{
				FKBoxElem Box;
				Box.Center = CenterLocal;
				Box.Rotation = RotLocalAdj;
				const FVector AbsSize = SizeLocal.GetAbs();
				// PMX box size is interpreted here to match current visual correctness; no extra scaling applied.
				Box.X = FMath::Max(0.1f, (float)AbsSize.X);
				Box.Y = FMath::Max(0.1f, (float)AbsSize.Y);
				Box.Z = FMath::Max(0.1f, (float)AbsSize.Z);
				AggGeom.BoxElems.Add(Box);
				break;
			}
			case 2: // Capsule (Sphyl)
			default:
			{
				FKSphylElem Capsule;
				Capsule.Center = CenterLocal;
				Capsule.Rotation = RotLocalAdj;
				const FVector AbsSize = SizeLocal.GetAbs();
				// PMX capsule: X/Y are radius, Z is total height including hemispheres.
				Capsule.Radius = FMath::Max(0.1f, (float)FMath::Max(AbsSize.X, AbsSize.Y));
				// UE sphyl Length is the cylinder section (totalHeight - 2*radius)
				Capsule.Length = FMath::Max(0.1f, (float)(AbsSize.Z - 2.0 * Capsule.Radius));
				AggGeom.SphylElems.Add(Capsule);
				break;
			}
		}

		// Basic physical properties — for Chaos, mass is derived from geometry/PM; PMX mass can be used later via materials if needed.

		BodySetup->InvalidatePhysicsData();
		BodySetup->CreatePhysicsMeshes();

		PhysicsAsset->SkeletalBodySetups.Add(BodySetup);
		++Result.BodiesCreated;
	}

	// 3) Create constraints from PMX joints
	for (int32 j = 0; j < Model.Joints.Num(); ++j)
	{
		const FPmxJoint& J = Model.Joints[j];
		if (!Model.RigidBodies.IsValidIndex(J.RigidBodyIndexA) || !Model.RigidBodies.IsValidIndex(J.RigidBodyIndexB))
		{
			UE_LOG(LogPMXImporter, Verbose,
			       TEXT("PMX Physics: Skipping joint %d due to invalid body indices A=%d B=%d"), j, J.RigidBodyIndexA,
			       J.RigidBodyIndexB);
			continue;
		}

		const FName BoneA = BodyIndexToBoneName[J.RigidBodyIndexA];
		const FName BoneB = BodyIndexToBoneName[J.RigidBodyIndexB];
		if (BoneA.IsNone() || BoneB.IsNone() || BoneA == BoneB)
		{
			UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Physics: Skipping joint %d due to invalid or identical bones"),
			       j);
			continue;
		}

		UPhysicsConstraintTemplate* ConstraintTemplate = NewObject<UPhysicsConstraintTemplate>(
			PhysicsAsset, UPhysicsConstraintTemplate::StaticClass(), NAME_None, RF_Transactional);
		FConstraintInstance& Inst = ConstraintTemplate->DefaultInstance;
		Inst.ConstraintBone1 = BoneA;
		Inst.ConstraintBone2 = BoneB;
		UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Physics: Create Joint[%d] BoneA=%s BoneB=%s"), j, *BoneA.ToString(),
		       *BoneB.ToString());

		// Apply constraint defaults from settings (override), otherwise fall back to PMX-derived limits
		const UPmxImporterSettings* PhysSettings = GetDefault<UPmxImporterSettings>();
		if (PhysSettings && PhysSettings->bOverrideJointConstraints)
		{
			// Linear: use settings (default Locked on all axes)
			Inst.SetLinearXMotion((ELinearConstraintMotion)PhysSettings->LinearX.GetValue());
			Inst.SetLinearYMotion((ELinearConstraintMotion)PhysSettings->LinearY.GetValue());
			Inst.SetLinearZMotion((ELinearConstraintMotion)PhysSettings->LinearZ.GetValue());
			Inst.SetLinearLimitSize(FMath::Max(0.0f, PhysSettings->LinearLimitSize));
			// Drives disabled if requested
			if (PhysSettings->bDisableAllDrives)
			{
				Inst.SetLinearPositionDrive(false, false, false);
				Inst.SetLinearVelocityDrive(false, false, false);
				Inst.SetOrientationDriveTwistAndSwing(false, false);
				Inst.SetAngularVelocityDriveTwistAndSwing(false, false);
			}
			// Angular: use settings
			Inst.SetAngularTwistLimit((EAngularConstraintMotion)PhysSettings->TwistMotion.GetValue(),
			                          PhysSettings->TwistLimitDegrees);
			Inst.SetAngularSwing1Limit((EAngularConstraintMotion)PhysSettings->Swing1Motion.GetValue(),
			                           PhysSettings->Swing1LimitDegrees);
			Inst.SetAngularSwing2Limit((EAngularConstraintMotion)PhysSettings->Swing2Motion.GetValue(),
			                           PhysSettings->Swing2LimitDegrees);
			UE_LOG(LogPMXImporter, Verbose,
			       TEXT(
				       "PMX Physics: Joint[%d] using settings override: Linear(X=%d,Y=%d,Z=%d,Size=%.2f) Angular(Twist=%d/%.1f, Swing1=%d/%.1f, Swing2=%d/%.1f)"
			       ),
			       j,
			       (int32)PhysSettings->LinearX.GetValue(), (int32)PhysSettings->LinearY.GetValue(),
			       (int32)PhysSettings->LinearZ.GetValue(), PhysSettings->LinearLimitSize,
			       (int32)PhysSettings->TwistMotion.GetValue(), PhysSettings->TwistLimitDegrees,
			       (int32)PhysSettings->Swing1Motion.GetValue(), PhysSettings->Swing1LimitDegrees,
			       (int32)PhysSettings->Swing2Motion.GetValue(), PhysSettings->Swing2LimitDegrees);
		}
		else
		{
			// Linear motions from PMX (fallback)
			const FVector MoveMin((double)J.MoveRestrictionMin.X, (double)J.MoveRestrictionMin.Y,
			                      (double)J.MoveRestrictionMin.Z);
			const FVector MoveMax((double)J.MoveRestrictionMax.X, (double)J.MoveRestrictionMax.Y,
			                      (double)J.MoveRestrictionMax.Z);
			const FVector MoveMinUE = ClampFiniteVec(PMXToUE.TransformVector(MoveMin));
			const FVector MoveMaxUE = ClampFiniteVec(PMXToUE.TransformVector(MoveMax));

			float MaxLinearLimit = 0.0f;
			auto SetupLinearAxis = [&Inst, &MaxLinearLimit](int32 Axis, float MinV, float MaxV)
			{
				const float AbsMax = FMath::Max(FMath::Abs(MinV), FMath::Abs(MaxV));
				ELinearConstraintMotion Mode = ELinearConstraintMotion::LCM_Locked;
				if (AbsMax > KINDA_SMALL_NUMBER)
				{
					Mode = ELinearConstraintMotion::LCM_Limited;
					MaxLinearLimit = FMath::Max(MaxLinearLimit, AbsMax);
				}
				switch (Axis)
				{
				case 0: Inst.SetLinearXMotion(Mode);
					break;
				case 1: Inst.SetLinearYMotion(Mode);
					break;
				case 2: Inst.SetLinearZMotion(Mode);
					break;
				}
			};
			SetupLinearAxis(0, (float)MoveMinUE.X, (float)MoveMaxUE.X);
			SetupLinearAxis(1, (float)MoveMinUE.Y, (float)MoveMaxUE.Y);
			SetupLinearAxis(2, (float)MoveMinUE.Z, (float)MoveMaxUE.Z);
			// Apply a unified linear limit size (Chaos uses a single radius-like limit)
			Inst.SetLinearLimitSize(MaxLinearLimit);
			UE_LOG(LogPMXImporter, Verbose,
			       TEXT("PMX Physics: Joint[%d] LinearLimits AbsMax=%.3f (X:[%.3f..%.3f] Y:[%.3f..%.3f] Z:[%.3f..%.3f])"
			       ), j, MaxLinearLimit, MoveMinUE.X, MoveMaxUE.X, MoveMinUE.Y, MoveMaxUE.Y, MoveMinUE.Z, MoveMaxUE.Z);
			// Zero stiffness/damping for now; can be tuned later
			Inst.SetLinearPositionDrive(false, false, false);
			Inst.SetLinearVelocityDrive(false, false, false);

			// Angular motions (radians to degrees)
			const FVector RotMinRad((double)J.RotationRestrictionMin.X, (double)J.RotationRestrictionMin.Y,
			                        (double)J.RotationRestrictionMin.Z);
			const FVector RotMaxRad((double)J.RotationRestrictionMax.X, (double)J.RotationRestrictionMax.Y,
			                        (double)J.RotationRestrictionMax.Z);
			const FVector RotAbsDeg(FMath::Max(FMath::Abs(RotMinRad.X), FMath::Abs(RotMaxRad.X)) * (180.0f / PI),
			                        FMath::Max(FMath::Abs(RotMinRad.Y), FMath::Abs(RotMaxRad.Y)) * (180.0f / PI),
			                        FMath::Max(FMath::Abs(RotMinRad.Z), FMath::Abs(RotMaxRad.Z)) * (180.0f / PI));

			auto SetAngular = [&Inst](EAngularConstraintMotion ModeTwist, float TwistLimitDeg,
			                          EAngularConstraintMotion ModeSwing1, float Swing1LimitDeg,
			                          EAngularConstraintMotion ModeSwing2, float Swing2LimitDeg)
			{
				Inst.SetAngularTwistLimit(ModeTwist, TwistLimitDeg);
				Inst.SetAngularSwing1Limit(ModeSwing1, Swing1LimitDeg);
				Inst.SetAngularSwing2Limit(ModeSwing2, Swing2LimitDeg);
			};

			EAngularConstraintMotion TwistMode = RotAbsDeg.X > KINDA_SMALL_NUMBER ? ACM_Limited : ACM_Locked;
			EAngularConstraintMotion Swing1Mode = RotAbsDeg.Y > KINDA_SMALL_NUMBER ? ACM_Limited : ACM_Locked;
			EAngularConstraintMotion Swing2Mode = RotAbsDeg.Z > KINDA_SMALL_NUMBER ? ACM_Limited : ACM_Locked;
			SetAngular(TwistMode, (float)RotAbsDeg.X, Swing1Mode, (float)RotAbsDeg.Y, Swing2Mode, (float)RotAbsDeg.Z);
			UE_LOG(LogPMXImporter, Verbose,
			       TEXT("PMX Physics: Joint[%d] AngularLimits Twist=%.1f Swing1=%.1f Swing2=%.1f (deg)"), j,
			       (float)RotAbsDeg.X, (float)RotAbsDeg.Y, (float)RotAbsDeg.Z);
		}

		// Apply projection for stability if enabled in settings
		if (PhysSettings)
		{
			ConstraintTemplate->DefaultInstance.ProfileInstance.bEnableProjection = PhysSettings->bEnableProjection;
			ConstraintTemplate->DefaultInstance.ProfileInstance.ProjectionLinearTolerance = FMath::Max(0.0f, PhysSettings->ProjectionLinearTolerance);
			ConstraintTemplate->DefaultInstance.ProfileInstance.ProjectionAngularTolerance = FMath::Max(0.0f, PhysSettings->ProjectionAngularTolerance);
			UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Physics: Joint[%d] Projection: bEnable=%s LinTol=%.2f AngTol=%.2f"), j,
				PhysSettings->bEnableProjection ? TEXT("true") : TEXT("false"),
				PhysSettings->ProjectionLinearTolerance, PhysSettings->ProjectionAngularTolerance);
		}

		PhysicsAsset->ConstraintSetup.Add(ConstraintTemplate);
		++Result.ConstraintsCreated;
	}

	// Apply stabilization: make unconstrained bodies kinematic (no parent/child constraints)
	if (const UPmxImporterSettings* PhysSettings = GetDefault<UPmxImporterSettings>())
	{
		if (PhysSettings->bKinematicForUnconstrainedBodies && Model.RigidBodies.Num() == PhysicsAsset->SkeletalBodySetups.Num())
		{
			TArray<bool> bReferenced;
			bReferenced.Init(false, Model.RigidBodies.Num());
			for (const FPmxJoint& J : Model.Joints)
			{
				if (Model.RigidBodies.IsValidIndex(J.RigidBodyIndexA)) bReferenced[J.RigidBodyIndexA] = true;
				if (Model.RigidBodies.IsValidIndex(J.RigidBodyIndexB)) bReferenced[J.RigidBodyIndexB] = true;
			}
			int32 AnchoredCount = 0;
			for (int32 i = 0; i < PhysicsAsset->SkeletalBodySetups.Num(); ++i)
			{
				if (!bReferenced.IsValidIndex(i) || bReferenced[i])
				{
					continue;
				}
				if (USkeletalBodySetup* BS = PhysicsAsset->SkeletalBodySetups[i])
				{
					if (BS->PhysicsType != EPhysicsType::PhysType_Kinematic)
					{
						BS->PhysicsType = EPhysicsType::PhysType_Kinematic;
						++AnchoredCount;
						UE_LOG(LogPMXImporter, Verbose, TEXT("PMX Physics: Body[%d] Bone=%s is unconstrained → set to Kinematic"), i, *BS->BoneName.ToString());
					}
				}
			}
			if (AnchoredCount > 0)
			{
				UE_LOG(LogPMXImporter, Display, TEXT("PMX Physics: Stabilization anchor applied — %d unconstrained bodies set to Kinematic"), AnchoredCount);
			}
		}
	}

	// Synchronize internal index maps after rebuilding arrays to avoid stale indices used by CalcAABB/thumbnail, etc.
	if (PhysicsAsset)
	{
		// Rebuild BoundsBodies from current SkeletalBodySetups to keep indices in sync for CalcAABB
		PhysicsAsset->UpdateBoundsBodiesArray();
		// Rebuild BoneName -> BodyIndex map
		PhysicsAsset->UpdateBodySetupIndexMap();
		// Notify listeners (editor tools, thumbnails) that physics asset changed
		PhysicsAsset->RefreshPhysicsAssetChange();
	}
	
	Result.bSuccess = true;
	if (Result.BodiesCreated != Model.RigidBodies.Num() || Result.ConstraintsCreated != Model.Joints.Num())
	{
		UE_LOG(LogPMXImporter, Warning, TEXT("PMX Physics: Requested Bodies=%d, Created=%d; Requested Constraints=%d, Created=%d. Check previous logs for skipped items."), Model.RigidBodies.Num(), Result.BodiesCreated, Model.Joints.Num(), Result.ConstraintsCreated);
	}
	UE_LOG(LogPMXImporter, Display, TEXT("PMX Physics: Rebuilt PhysicsAsset — Bodies=%d, Constraints=%d"), Result.BodiesCreated, Result.ConstraintsCreated);

#endif // WITH_EDITOR

	return Result;
}
