// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#pragma once

#include "CoreMinimal.h"

// PMX file format structures based on PMX 2.x specification
struct FPmxHeader
{
	float Version = 2.0f;
	uint8 EncodeType = 1; // 0=UTF-16, 1=UTF-8
	uint8 AdditionalUVNum = 0;
	uint8 VertexIndexSize = 4;
	uint8 TextureIndexSize = 4;
	uint8 MaterialIndexSize = 4;
	uint8 BoneIndexSize = 4;
	uint8 MorphIndexSize = 4;
	uint8 RigidbodyIndexSize = 4;
	
	FString ModelName;
	FString ModelNameEng;
	FString Comment;
	FString CommentEng;
};

struct FPmxVertex
{
	FVector3f Position;
	FVector3f Normal;
	FVector2f UV;
	TArray<FVector4f> AdditionalUV; // Size based on header
	
	uint8 WeightType; // 0=BDEF1, 1=BDEF2, 2=BDEF4, 3=SDEF, 4=QDEF
	TArray<int32> BoneIndices; // Size based on weight type
	TArray<float> BoneWeights; // Size based on weight type
	
	// SDEF/QDEF specific
	FVector3f C;
	FVector3f R0;
	FVector3f R1;
	
	float EdgeScale = 1.0f;
};

struct FPmxTexture
{
	FString TexturePath;
};

struct FPmxMaterial
{
	FString Name;
	FString NameEng;
	
	FLinearColor Diffuse = FLinearColor::White; // float4 RGBA
	FVector3f Specular = FVector3f::ZeroVector; // float3 RGB
	float SpecularStrength = 0.0f; // float
	FVector3f Ambient = FVector3f::ZeroVector; // float3 RGB
	
	uint8 DrawingFlags = 0; // Bit flags for various drawing modes
	FLinearColor EdgeColor = FLinearColor::Black; // float4 RGBA
	float EdgeSize = 0.0f; // float
	
	int32 TextureIndex = -1;
	int32 SphereTextureIndex = -1;
	uint8 SphereMode = 0; // 0=disabled, 1=multiply, 2=add, 3=sub-texture
	
	uint8 SharedToonFlag = 0; // 0=texture, 1=shared toon
	int32 ToonTextureIndex = -1;
	
	FString Memo;
	int32 SurfaceCount = 0; // Number of surfaces (triangles * 3)
};

struct FPmxBone
{
	FString Name;
	FString NameEng;
	
	FVector3f Position;
	int32 ParentBoneIndex = -1;
	int32 Layer = 0;
	
	uint16 BoneFlags = 0;
	
	// Connection target (depends on flags)
	int32 ConnectionIndex = -1;
	FVector3f Offset;
	
	// Additional rotation/movement (depends on flags)
	int32 AdditionalParentIndex = -1;
	float AdditionalRatio = 0.0f;
	
	// Fixed axis (depends on flags)
	FVector3f AxisDirection;
	
	// Local coordinate (depends on flags)
	FVector3f XAxisDirection;
	FVector3f ZAxisDirection;
	
	// External parent (depends on flags)
	int32 ExternalKey = 0;
	
	// IK (depends on flags)
	struct FPmxIKLink
	{
		int32 BoneIndex = -1;
		uint8 AngleLimitFlag = 0;
		FVector3f LimitMin;
		FVector3f LimitMax;
	};
	
	int32 IKTargetBoneIndex = -1;
	int32 IKLoopCount = 0;
	float IKLimitAngle = 0.0f;
	TArray<FPmxIKLink> IKLinks;
};

struct FPmxVertexMorph
{
	int32 VertexIndex = -1;
	FVector3f Offset;
};

struct FPmxUVMorph
{
	int32 VertexIndex = -1;
	FVector4f Offset;
};

struct FPmxBoneMorph
{
	int32 BoneIndex = -1;
	FVector3f Translation;
	FQuat4f Rotation = FQuat4f::Identity;
};

struct FPmxMaterialMorph
{
	int32 MaterialIndex = -1;
	uint8 OffsetType = 0; // 0=multiply, 1=add
	FLinearColor Diffuse;
	FLinearColor Specular;
	float SpecularStrength = 0.0f;
	FLinearColor Ambient;
	FLinearColor EdgeColor;
	float EdgeSize = 0.0f;
	FVector4f TextureColor;
	FVector4f SphereTextureColor;
	FVector4f ToonTextureColor;
};

struct FPmxGroupMorph
{
	int32 MorphIndex = -1;
	float MorphRatio = 0.0f;
};

struct FPmxMorph
{
	FString Name;
	FString NameEng;
	uint8 ControlPanel = 0; // 0=eyebrow, 1=eye, 2=mouth, 3=other
	uint8 MorphType = 0; // 0=group, 1=vertex, 2=bone, 3=UV, 4=additional UV1-4, 8=material
	
	TArray<FPmxVertexMorph> VertexMorphs;
	TArray<FPmxUVMorph> UVMorphs;
	TArray<FPmxBoneMorph> BoneMorphs;
	TArray<FPmxMaterialMorph> MaterialMorphs;
	TArray<FPmxGroupMorph> GroupMorphs;
};

struct FPmxDisplayFrame
{
	FString Name;
	FString NameEng;
	uint8 SpecialFlag = 0;
	
	struct FPmxDisplayElement
	{
		uint8 ElementTarget = 0; // 0=bone, 1=morph
		int32 ElementIndex = -1;
	};
	
	TArray<FPmxDisplayElement> Elements;
};

struct FPmxRigidBody
{
	FString Name;
	FString NameEng;
	
	int32 RelatedBoneIndex = -1;
	uint8 Group = 0;
	uint16 NonCollisionGroup = 0;
	
	uint8 Shape = 0; // 0=sphere, 1=box, 2=capsule
	FVector3f Size;
	FVector3f Position;
	FVector3f Rotation;
	
	float Mass = 1.0f;
	float MoveAttenuation = 0.0f;
	float RotationAttenuation = 0.0f;
	float Repulsion = 0.0f;
	float Friction = 0.0f;
	
	uint8 PhysicsType = 0; // 0=follow bone(static), 1=physics, 2=physics + bone position matching
};

struct FPmxJoint
{
	FString Name;
	FString NameEng;
	
	uint8 JointType = 0; // 0=spring 6DOF
	int32 RigidBodyIndexA = -1;
	int32 RigidBodyIndexB = -1;
	
	FVector3f Position;
	FVector3f Rotation;
	
	FVector3f MoveRestrictionMin;
	FVector3f MoveRestrictionMax;
	FVector3f RotationRestrictionMin;
	FVector3f RotationRestrictionMax;
	
	FVector3f SpringMoveCoefficient;
	FVector3f SpringRotationCoefficient;
};

struct FPmxSoftBody
{
	FString Name;
	FString NameEng;
	
	uint8 Shape = 0; // 0=tri-mesh, 1=rope
	int32 MaterialIndex = -1;
	uint8 Group = 0;
	uint16 NonCollisionGroup = 0;
	uint8 Flags = 0;
	
	int32 BLinkDistance = 0;
	int32 ClusterCount = 0;
	float TotalMass = 0.0f;
	float CollisionMargin = 0.0f;
	
	int32 AeroModel = 0;
	float VCF = 0.0f;
	float DP = 0.0f;
	float DG = 0.0f;
	float LF = 0.0f;
	float PR = 0.0f;
	float VC = 0.0f;
	float DF = 0.0f;
	float MT = 0.0f;
	float CHR = 0.0f;
	float KHR = 0.0f;
	float SHR = 0.0f;
	float AHR = 0.0f;
	
	float SRHR_CL = 0.0f;
	float SKHR_CL = 0.0f;
	float SSHR_CL = 0.0f;
	float SR_SPLT_CL = 0.0f;
	float SK_SPLT_CL = 0.0f;
	float SS_SPLT_CL = 0.0f;
	
	int32 V_IT = 0;
	int32 P_IT = 0;
	int32 D_IT = 0;
	int32 C_IT = 0;
	
	float LST = 0.0f;
	float AST = 0.0f;
	float VST = 0.0f;
	
	TArray<int32> AnchorRigidBodies;
	TArray<int32> PinVertexIndices;
};

struct FPmxModel
{
	FPmxHeader Header;
	
	TArray<FPmxVertex> Vertices;
	TArray<int32> Indices; // Triangle indices (groups of 3)
	TArray<FPmxTexture> Textures;
	TArray<FPmxMaterial> Materials;
	TArray<FPmxBone> Bones;
	TArray<FPmxMorph> Morphs;
	TArray<FPmxDisplayFrame> DisplayFrames;
	TArray<FPmxRigidBody> RigidBodies;
	TArray<FPmxJoint> Joints;
	TArray<FPmxSoftBody> SoftBodies; // PMX 2.1 feature
};
