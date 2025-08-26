// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved. 

#include "PmxReader.h"
#include "PmxStructs.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogPmxReader, Log, All);

class FPmxReader
{
public:
	FPmxReader(const TArray<uint8>& InData);
	
	bool ReadPmxModel(FPmxModel& OutModel);
	
private:
	const TArray<uint8>& Data;
	int32 Position;
	
	// Helper functions for reading binary data
	template<typename T>
	bool ReadValue(T& OutValue);
	
	bool ReadString(FString& OutString, bool bUTF8);
	bool ReadIndex(int32& OutIndex, uint8 IndexSize);
	bool ReadVector3f(FVector3f& OutVector);
	bool ReadVector2f(FVector2f& OutVector);
	bool ReadLinearColor(FLinearColor& OutColor);
	bool ReadQuatf(FQuat4f& OutQuat);
	
	// Section reading functions
	bool ReadHeader(FPmxHeader& OutHeader);
	bool ReadVertices(FPmxModel& Model);
	bool ReadIndices(FPmxModel& Model);
	bool ReadTextures(FPmxModel& Model);
	bool ReadMaterials(FPmxModel& Model);
	bool ReadBones(FPmxModel& Model);
	bool ReadMorphs(FPmxModel& Model);
	bool ReadDisplayFrames(FPmxModel& Model);
	bool ReadRigidBodies(FPmxModel& Model);
	bool ReadJoints(FPmxModel& Model);
	bool ReadSoftBodies(FPmxModel& Model);
	
	// Validation helpers
	bool IsValidPosition(int32 Size) const;
	void LogError(const FString& Message) const;
};

FPmxReader::FPmxReader(const TArray<uint8>& InData)
	: Data(InData)
	, Position(0)
{
}

bool FPmxReader::ReadPmxModel(FPmxModel& OutModel)
{
	Position = 0;
	
	// Check PMX signature
	if (Data.Num() < 4)
	{
		LogError(TEXT("File too small to contain PMX signature"));
		return false;
	}
	
	if (Data[0] != 'P' || Data[1] != 'M' || Data[2] != 'X' || Data[3] != ' ')
	{
		LogError(TEXT("Invalid PMX signature"));
		return false;
	}
	
	Position = 4;
	
	// Read sections in order
	UE_LOG(LogPmxReader, Log, TEXT("Reading PMX header at position 0x%X"), Position);
	if (!ReadHeader(OutModel.Header)) return false;
	UE_LOG(LogPmxReader, Log, TEXT("Reading vertices at position 0x%X"), Position);
	if (!ReadVertices(OutModel)) return false;
	UE_LOG(LogPmxReader, Log, TEXT("Reading indices at position 0x%X"), Position);
	if (!ReadIndices(OutModel)) return false;
	UE_LOG(LogPmxReader, Log, TEXT("Reading textures at position 0x%X"), Position);
	if (!ReadTextures(OutModel)) return false;
	UE_LOG(LogPmxReader, Log, TEXT("Reading materials at position 0x%X"), Position);
	if (!ReadMaterials(OutModel)) return false;
	UE_LOG(LogPmxReader, Log, TEXT("Reading bones at position 0x%X"), Position);
	if (!ReadBones(OutModel)) return false;
	UE_LOG(LogPmxReader, Log, TEXT("Reading morphs at position 0x%X"), Position);
	if (!ReadMorphs(OutModel)) return false;
	UE_LOG(LogPmxReader, Log, TEXT("Reading display frames at position 0x%X"), Position);
	if (!ReadDisplayFrames(OutModel))
	{
		UE_LOG(LogPmxReader, Warning, TEXT("Failed to read DisplayFrames. Continuing to attempt physics sections."));
	}
	UE_LOG(LogPmxReader, Log, TEXT("Reading rigid bodies at position 0x%X"), Position);
	int32 SavedRigidBodiesPos = Position;
	if (!ReadRigidBodies(OutModel))
	{
		UE_LOG(LogPmxReader, Warning, TEXT("Failed to read RigidBodies at 0x%08X. Attempting resync scan to locate physics section..."), SavedRigidBodiesPos);
		// Heuristic resync: scan forward up to 2MB to find a plausible RigidBody section start
		const int32 ScanLimit = FMath::Min(SavedRigidBodiesPos + 2 * 1024 * 1024, Data.Num() - 4);
		bool bResynced = false;
		for (int32 ScanPos = SavedRigidBodiesPos + 1; ScanPos < ScanLimit; ++ScanPos)
		{
			Position = ScanPos;
			int32 CandCount = -1;
			const int32 TestPos = Position;
			if (ReadValue(CandCount) && CandCount >= 0 && CandCount <= 10000)
			{
				// Try to parse one rigid body entry to validate
				const int32 OneBodyTestPos = Position;
				bool bUTF8 = (OutModel.Header.EncodeType == 1);
				FString TmpS;
				int32 SavedBeforeStrings = Position;
				if (ReadString(TmpS, bUTF8) && ReadString(TmpS, bUTF8))
				{
	    int32 TmpIdx; uint8 TmpU8; uint16 TmpU16; FVector3f TmpV3;
					// try a subset of fields to validate layout
					if (ReadIndex(TmpIdx, OutModel.Header.BoneIndexSize) && ReadValue(TmpU8) && ReadValue(TmpU16)
						&& ReadValue(TmpU8) && ReadVector3f(TmpV3) && ReadVector3f(TmpV3) && ReadVector3f(TmpV3))
					{
						// Looks plausible; rewind to candidate start and parse fully
						Position = TestPos;
						OutModel.RigidBodies.Reset();
						if (ReadRigidBodies(OutModel)) { bResynced = true; }
					}
				}
				// restore for next scan step
				Position = OneBodyTestPos; // limited restore
			}
			// early exit if resynced
			if (bResynced) break;
		}
		if (!bResynced)
		{
			UE_LOG(LogPmxReader, Warning, TEXT("RigidBodies resync scan failed. Physics bodies will be unavailable."));
		}
	}
	UE_LOG(LogPmxReader, Log, TEXT("Reading joints at position 0x%X"), Position);
	int32 SavedJointsPos = Position;
	if (!ReadJoints(OutModel))
	{
		UE_LOG(LogPmxReader, Warning, TEXT("Failed to read Joints at 0x%08X. Attempting resync scan to locate joints section..."), SavedJointsPos);
		// Heuristic resync for joints: scan forward up to 2MB
		const int32 ScanLimitJ = FMath::Min(SavedJointsPos + 2 * 1024 * 1024, Data.Num() - 4);
		bool bResyncedJ = false;
		for (int32 ScanPos = SavedJointsPos + 1; ScanPos < ScanLimitJ; ++ScanPos)
		{
			Position = ScanPos;
			int32 CandCount = -1;
			const int32 TestPos = Position;
			if (ReadValue(CandCount) && CandCount >= 0 && CandCount <= 10000)
			{
				// Try to parse one joint entry minimally
				const int32 OneJointTestPos = Position;
				bool bUTF8 = (OutModel.Header.EncodeType == 1);
				FString TmpS;
				if (ReadString(TmpS, bUTF8) && ReadString(TmpS, bUTF8))
				{
					uint8 TmpU8; int32 TmpIdx; FVector3f TmpV3;
					if (ReadValue(TmpU8) && ReadIndex(TmpIdx, OutModel.Header.RigidbodyIndexSize) && ReadIndex(TmpIdx, OutModel.Header.RigidbodyIndexSize)
						&& ReadVector3f(TmpV3) && ReadVector3f(TmpV3))
					{
						// plausible
						Position = TestPos;
						OutModel.Joints.Reset();
						if (ReadJoints(OutModel)) { bResyncedJ = true; }
					}
				}
				Position = OneJointTestPos;
			}
			if (bResyncedJ) break;
		}
		if (!bResyncedJ)
		{
			UE_LOG(LogPmxReader, Warning, TEXT("Joints resync scan failed. Physics joints will be unavailable."));
		}
	}
	
	// Soft bodies are optional (PMX 2.1 feature)
	if (Position < Data.Num())
	{
		ReadSoftBodies(OutModel);
	}
	
	UE_LOG(LogPmxReader, Log, TEXT("Successfully loaded PMX model: %s"), *OutModel.Header.ModelName);
	UE_LOG(LogPmxReader, Log, TEXT("Vertices: %d, Indices: %d, Bones: %d, Materials: %d, Morphs: %d"), 
		OutModel.Vertices.Num(), OutModel.Indices.Num(), OutModel.Bones.Num(), 
		OutModel.Materials.Num(), OutModel.Morphs.Num());
	
	return true;
}

template<typename T>
bool FPmxReader::ReadValue(T& OutValue)
{
	if (!IsValidPosition(sizeof(T)))
	{
		return false;
	}
	
	FMemory::Memcpy(&OutValue, &Data[Position], sizeof(T));
	
	// PMX format uses little-endian, convert if needed
	if constexpr (sizeof(T) == 2)
	{
		OutValue = INTEL_ORDER16(OutValue);
	}
	else if constexpr (sizeof(T) == 4)
	{
		if constexpr (std::is_same_v<T, int32> || std::is_same_v<T, uint32> || std::is_same_v<T, float>)
		{
			OutValue = INTEL_ORDER32(OutValue);
		}
	}
	
	Position += sizeof(T);
	return true;
}

bool FPmxReader::ReadString(FString& OutString, bool bUTF8)
{
	int32 Length;
	if (!ReadValue(Length))
	{
		return false;
	}
	
	UE_LOG(LogPmxReader, VeryVerbose, TEXT("ReadString: Length=%d (0x%08X) at position 0x%08X"), Length, Length, Position - 4);
	
	// Sanity check bounds
	const int32 MaxStringLength = 1024 * 1024; // 1MB cap

	// Some exporters accidentally write the length as an unsigned value when the high bit is set.
	if (Length < 0)
	{
		// Try reinterpret as unsigned length
		const uint32 UnsignedLen = static_cast<uint32>(Length);
		if (UnsignedLen > 0 && UnsignedLen <= static_cast<uint32>(MaxStringLength) && IsValidPosition(static_cast<int32>(UnsignedLen)))
		{
			UE_LOG(LogPmxReader, Warning, TEXT("Negative string length %d encountered; interpreting as unsigned length %u"), Length, UnsignedLen);
			Length = static_cast<int32>(UnsignedLen);
		}
		else
		{
			LogError(FString::Printf(TEXT("Invalid negative string length: %d"), Length));
			return false;
		}
	}
	
	if (Length == 0)
	{
		OutString.Empty();
		return true;
	}
	
	if (Length > MaxStringLength)
	{
		LogError(FString::Printf(TEXT("String length too large: %d (max: %d)"), Length, MaxStringLength));
		LogError(FString::Printf(TEXT("Raw bytes at position 0x%08X: %02X %02X %02X %02X"), 
			Position - 4, Data[Position - 4], Data[Position - 3], Data[Position - 2], Data[Position - 1]));
		return false;
	}
	
	int32 ByteLength = Length;
	
	if (!bUTF8)
	{
		// PMX header requested UTF-16LE. 일부 익스포터가 길이를 UTF-16 코드 유닛 수로 기록하거나, 실제로 UTF-8로 저장하는 경우가 있음.
		// 길이가 홀수인 경우, 다음 순서로 안전하게 시도:
		// 1) 동일 길이로 UTF-8 디코드 시도(성공 시 인코딩 불일치 경고 후 사용)
		// 2) UTF-16 코드 유닛 수로 간주(2배 바이트)하여 시도
		if ((ByteLength & 1) != 0)
		{
			// Try UTF-8 with current ByteLength without committing position
			if (IsValidPosition(ByteLength))
			{
				const int32 SavedPos = Position;
				FUTF8ToTCHAR TryUtf8(reinterpret_cast<const ANSICHAR*>(&Data[Position]), ByteLength);
				if (TryUtf8.Length() > 0)
				{
					UE_LOG(LogPmxReader, Warning, TEXT("Odd UTF-16 length %d; treating as UTF-8 due to encoding mismatch"), ByteLength);
					OutString = FString(TryUtf8.Length(), TryUtf8.Get());
					Position += ByteLength;
					return true;
				}
				Position = SavedPos; // rollback if not UTF-8
			}
			// Fallback: treat as UTF-16 code units -> bytes
			const int64 TentativeByteLength = static_cast<int64>(ByteLength) * 2; // units -> bytes
			if (TentativeByteLength <= MaxStringLength && TentativeByteLength <= (int64)TNumericLimits<int32>::Max())
			{
				if (IsValidPosition(static_cast<int32>(TentativeByteLength)))
				{
					UE_LOG(LogPmxReader, Warning, TEXT("Odd UTF-16 length %d detected; interpreting as UTF-16 code units -> %d bytes"), ByteLength, static_cast<int32>(TentativeByteLength));
					ByteLength = static_cast<int32>(TentativeByteLength);
				}
				else
				{
					LogError(FString::Printf(TEXT("Invalid UTF-16 string length (odd) and doubling exceeds remaining data: %d units"), Length));
					return false;
				}
			}
			else
			{
				LogError(FString::Printf(TEXT("Invalid UTF-16 string length (odd) and doubling overflows/surpasses limit: %d units"), Length));
				return false;
			}
		}
	}
	
	if (!IsValidPosition(ByteLength))
	{
		LogError(FString::Printf(TEXT("String length %d exceeds remaining file data"), ByteLength));
		return false;
	}
	
	if (bUTF8)
	{
		// UTF-8 encoding
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(&Data[Position]), ByteLength);
		OutString = FString(Converter.Length(), Converter.Get());
	}
	else
	{
		// UTF-16LE encoding
		if ((ByteLength & 1) != 0)
		{
			LogError(TEXT("Invalid UTF-16 string length (not even) after correction"));
			return false;
		}
		const UTF16CHAR* UTF16Data = reinterpret_cast<const UTF16CHAR*>(&Data[Position]);
		OutString = FString(ByteLength / 2, UTF16Data);
	}
	
	Position += ByteLength;
	return true;
}

bool FPmxReader::ReadIndex(int32& OutIndex, uint8 IndexSize)
{
	switch (IndexSize)
	{
	case 1:
		{
			uint8 Value;
			if (!ReadValue(Value))
				return false;
			OutIndex = (Value == 0xFF) ? -1 : static_cast<int32>(Value);
			break;
		}
	case 2:
		{
			uint16 Value;
			if (!ReadValue(Value))
				return false;
			OutIndex = (Value == 0xFFFF) ? -1 : static_cast<int32>(Value);
			break;
		}
	case 4:
		{
			if (!ReadValue(OutIndex))
				return false;
			break;
		}
	default:
		LogError(FString::Printf(TEXT("Invalid index size: %d"), IndexSize));
		return false;
	}
	
	return true;
}

bool FPmxReader::ReadVector3f(FVector3f& OutVector)
{
	return ReadValue(OutVector.X) && ReadValue(OutVector.Y) && ReadValue(OutVector.Z);
}

bool FPmxReader::ReadVector2f(FVector2f& OutVector)
{
	return ReadValue(OutVector.X) && ReadValue(OutVector.Y);
}

bool FPmxReader::ReadLinearColor(FLinearColor& OutColor)
{
	return ReadValue(OutColor.R) && ReadValue(OutColor.G) && ReadValue(OutColor.B) && ReadValue(OutColor.A);
}

bool FPmxReader::ReadQuatf(FQuat4f& OutQuat)
{
	return ReadValue(OutQuat.X) && ReadValue(OutQuat.Y) && ReadValue(OutQuat.Z) && ReadValue(OutQuat.W);
}

bool FPmxReader::ReadHeader(FPmxHeader& OutHeader)
{
	// Read version
	if (!ReadValue(OutHeader.Version))
		return false;
	
	// Read globals count (should be 8)
	uint8 GlobalsCount;
	if (!ReadValue(GlobalsCount) || GlobalsCount != 8)
	{
		LogError(FString::Printf(TEXT("Invalid globals count: %d"), GlobalsCount));
		return false;
	}
	
	// Read globals
	if (!ReadValue(OutHeader.EncodeType)) return false;
	if (!ReadValue(OutHeader.AdditionalUVNum)) return false;
	if (!ReadValue(OutHeader.VertexIndexSize)) return false;
	if (!ReadValue(OutHeader.TextureIndexSize)) return false;
	if (!ReadValue(OutHeader.MaterialIndexSize)) return false;
	if (!ReadValue(OutHeader.BoneIndexSize)) return false;
	if (!ReadValue(OutHeader.MorphIndexSize)) return false;
	if (!ReadValue(OutHeader.RigidbodyIndexSize)) return false;
	
	// Read strings
	bool bUTF8 = (OutHeader.EncodeType == 1);
	if (!ReadString(OutHeader.ModelName, bUTF8)) return false;
	if (!ReadString(OutHeader.ModelNameEng, bUTF8)) return false;
	if (!ReadString(OutHeader.Comment, bUTF8)) return false;
	if (!ReadString(OutHeader.CommentEng, bUTF8)) return false;
	
	return true;
}

bool FPmxReader::ReadVertices(FPmxModel& Model)
{
	int32 VertexCount;
	if (!ReadValue(VertexCount))
		return false;
	
	if (VertexCount < 0 || VertexCount > 10000000) // Sanity check
	{
		LogError(FString::Printf(TEXT("Invalid vertex count: %d"), VertexCount));
		return false;
	}
	
	Model.Vertices.Reserve(VertexCount);
	
	for (int32 i = 0; i < VertexCount; ++i)
	{
		FPmxVertex Vertex;
		
		// Position, Normal, UV
		if (!ReadVector3f(Vertex.Position)) return false;
		if (!ReadVector3f(Vertex.Normal)) return false;
		if (!ReadVector2f(Vertex.UV)) return false;
		
		// Additional UV
		Vertex.AdditionalUV.SetNum(Model.Header.AdditionalUVNum);
		for (int32 j = 0; j < Model.Header.AdditionalUVNum; ++j)
		{
			FVector4f UV;
			if (!ReadValue(UV.X) || !ReadValue(UV.Y) || !ReadValue(UV.Z) || !ReadValue(UV.W))
				return false;
			Vertex.AdditionalUV[j] = UV;
		}
		
		// Weight type
		if (!ReadValue(Vertex.WeightType))
			return false;
		
		// Bone data based on weight type
		switch (Vertex.WeightType)
		{
		case 0: // BDEF1
			Vertex.BoneIndices.SetNum(1);
			Vertex.BoneWeights.SetNum(1);
			if (!ReadIndex(Vertex.BoneIndices[0], Model.Header.BoneIndexSize)) return false;
			Vertex.BoneWeights[0] = 1.0f;
			break;
			
		case 1: // BDEF2
			Vertex.BoneIndices.SetNum(2);
			Vertex.BoneWeights.SetNum(2);
			if (!ReadIndex(Vertex.BoneIndices[0], Model.Header.BoneIndexSize)) return false;
			if (!ReadIndex(Vertex.BoneIndices[1], Model.Header.BoneIndexSize)) return false;
			if (!ReadValue(Vertex.BoneWeights[0])) return false;
			Vertex.BoneWeights[1] = 1.0f - Vertex.BoneWeights[0];
			break;
			
		case 2: // BDEF4
			Vertex.BoneIndices.SetNum(4);
			Vertex.BoneWeights.SetNum(4);
			for (int32 j = 0; j < 4; ++j)
			{
				if (!ReadIndex(Vertex.BoneIndices[j], Model.Header.BoneIndexSize)) return false;
			}
			for (int32 j = 0; j < 4; ++j)
			{
				if (!ReadValue(Vertex.BoneWeights[j])) return false;
			}
			break;
			
		case 3: // SDEF
			Vertex.BoneIndices.SetNum(2);
			Vertex.BoneWeights.SetNum(2);
			if (!ReadIndex(Vertex.BoneIndices[0], Model.Header.BoneIndexSize)) return false;
			if (!ReadIndex(Vertex.BoneIndices[1], Model.Header.BoneIndexSize)) return false;
			if (!ReadValue(Vertex.BoneWeights[0])) return false;
			Vertex.BoneWeights[1] = 1.0f - Vertex.BoneWeights[0];
			if (!ReadVector3f(Vertex.C)) return false;
			if (!ReadVector3f(Vertex.R0)) return false;
			if (!ReadVector3f(Vertex.R1)) return false;
			break;
			
		default:
			LogError(FString::Printf(TEXT("Unsupported weight type: %d"), Vertex.WeightType));
			return false;
		}
		
		// Edge scale
		if (!ReadValue(Vertex.EdgeScale))
			return false;
		
		Model.Vertices.Add(MoveTemp(Vertex));
	}
	
	return true;
}

bool FPmxReader::ReadIndices(FPmxModel& Model)
{
	int32 IndexCount;
	if (!ReadValue(IndexCount))
		return false;
	
	if (IndexCount < 0 || IndexCount % 3 != 0 || IndexCount > 30000000) // Sanity check
	{
		LogError(FString::Printf(TEXT("Invalid index count: %d"), IndexCount));
		return false;
	}
	
	Model.Indices.Reserve(IndexCount);
	
	for (int32 i = 0; i < IndexCount; ++i)
	{
		int32 Index;
		if (!ReadIndex(Index, Model.Header.VertexIndexSize))
			return false;
		
		if (Index < 0 || Index >= Model.Vertices.Num())
		{
			LogError(FString::Printf(TEXT("Invalid vertex index: %d"), Index));
			return false;
		}
		
		Model.Indices.Add(Index);
	}
	
	return true;
}

bool FPmxReader::ReadTextures(FPmxModel& Model)
{
	int32 TextureCount;
	if (!ReadValue(TextureCount))
		return false;
	
	if (TextureCount < 0 || TextureCount > 10000) // Sanity check
	{
		LogError(FString::Printf(TEXT("Invalid texture count: %d"), TextureCount));
		return false;
	}
	
	Model.Textures.Reserve(TextureCount);
	
	bool bUTF8 = (Model.Header.EncodeType == 1);
	
	for (int32 i = 0; i < TextureCount; ++i)
	{
		FPmxTexture Texture;
		if (!ReadString(Texture.TexturePath, bUTF8))
			return false;
		
		Model.Textures.Add(MoveTemp(Texture));
	}
	
	return true;
}

bool FPmxReader::ReadMaterials(FPmxModel& Model)
{
	int32 MaterialCount;
	if (!ReadValue(MaterialCount))
		return false;
	
	if (MaterialCount < 0 || MaterialCount > 10000) // Sanity check
	{
		LogError(FString::Printf(TEXT("Invalid material count: %d"), MaterialCount));
		return false;
	}
	
	Model.Materials.Reserve(MaterialCount);
	
	bool bUTF8 = (Model.Header.EncodeType == 1);
	
	for (int32 i = 0; i < MaterialCount; ++i)
	{
		FPmxMaterial Material;
		
		if (!ReadString(Material.Name, bUTF8)) return false;
		if (!ReadString(Material.NameEng, bUTF8)) return false;
		
		if (!ReadLinearColor(Material.Diffuse)) return false;
		if (!ReadVector3f(Material.Specular)) return false;
		if (!ReadValue(Material.SpecularStrength)) return false;
		if (!ReadVector3f(Material.Ambient)) return false;
		
		if (!ReadValue(Material.DrawingFlags)) return false;
		if (!ReadLinearColor(Material.EdgeColor)) return false;
		if (!ReadValue(Material.EdgeSize)) return false;
		
		if (!ReadIndex(Material.TextureIndex, Model.Header.TextureIndexSize)) return false;
		if (!ReadIndex(Material.SphereTextureIndex, Model.Header.TextureIndexSize)) return false;
		if (!ReadValue(Material.SphereMode)) return false;
		if (!ReadValue(Material.SharedToonFlag)) return false;
		
		if (Material.SharedToonFlag)
		{
			uint8 ToonIndex;
			if (!ReadValue(ToonIndex)) return false;
			Material.ToonTextureIndex = ToonIndex;
		}
		else
		{
			if (!ReadIndex(Material.ToonTextureIndex, Model.Header.TextureIndexSize)) return false;
		}
		
		if (!ReadString(Material.Memo, bUTF8)) return false;
		if (!ReadValue(Material.SurfaceCount)) return false;
		
		Model.Materials.Add(MoveTemp(Material));
	}
	
	return true;
}

bool FPmxReader::ReadBones(FPmxModel& Model)
{
	int32 BoneCount;
	if (!ReadValue(BoneCount))
		return false;
	
	if (BoneCount < 0 || BoneCount > 50000) // Sanity check
	{
		LogError(FString::Printf(TEXT("Invalid bone count: %d"), BoneCount));
		return false;
	}
	
	Model.Bones.Reserve(BoneCount);
	
	bool bUTF8 = (Model.Header.EncodeType == 1);
	
	for (int32 i = 0; i < BoneCount; ++i)
	{
		FPmxBone Bone;
		
		if (!ReadString(Bone.Name, bUTF8)) return false;
		if (!ReadString(Bone.NameEng, bUTF8)) return false;
		
		if (!ReadVector3f(Bone.Position)) return false;
		if (!ReadIndex(Bone.ParentBoneIndex, Model.Header.BoneIndexSize)) return false;
		if (!ReadValue(Bone.Layer)) return false;
		if (!ReadValue(Bone.BoneFlags)) return false;
		
		// Process bone flags and read additional data
		// This is a simplified version - full implementation would handle all flag combinations
		if (Bone.BoneFlags & 0x0001) // Connection display
		{
			if (!ReadIndex(Bone.ConnectionIndex, Model.Header.BoneIndexSize)) return false;
		}
		else
		{
			if (!ReadVector3f(Bone.Offset)) return false;
		}
		
		if ((Bone.BoneFlags & 0x0100) || (Bone.BoneFlags & 0x0200)) // Additional parent
		{
			if (!ReadIndex(Bone.AdditionalParentIndex, Model.Header.BoneIndexSize)) return false;
			if (!ReadValue(Bone.AdditionalRatio)) return false;
		}
		
		if (Bone.BoneFlags & 0x0400) // Fixed axis
		{
			if (!ReadVector3f(Bone.AxisDirection)) return false;
		}
		
		if (Bone.BoneFlags & 0x0800) // Local coordinate
		{
			if (!ReadVector3f(Bone.XAxisDirection)) return false;
			if (!ReadVector3f(Bone.ZAxisDirection)) return false;
		}
		
		if (Bone.BoneFlags & 0x2000) // External parent
		{
			if (!ReadValue(Bone.ExternalKey)) return false;
		}
		
		if (Bone.BoneFlags & 0x0020) // IK
		{
			if (!ReadIndex(Bone.IKTargetBoneIndex, Model.Header.BoneIndexSize)) return false;
			if (!ReadValue(Bone.IKLoopCount)) return false;
			if (!ReadValue(Bone.IKLimitAngle)) return false;
			
			int32 LinkCount;
			if (!ReadValue(LinkCount)) return false;
			
			if (LinkCount < 0 || LinkCount > 1000) // Sanity check
			{
				LogError(FString::Printf(TEXT("Invalid IK link count: %d"), LinkCount));
				return false;
			}
			
			Bone.IKLinks.Reserve(LinkCount);
			
			for (int32 j = 0; j < LinkCount; ++j)
			{
				FPmxBone::FPmxIKLink Link;
				if (!ReadIndex(Link.BoneIndex, Model.Header.BoneIndexSize)) return false;
				if (!ReadValue(Link.AngleLimitFlag)) return false;
				
				if (Link.AngleLimitFlag)
				{
					if (!ReadVector3f(Link.LimitMin)) return false;
					if (!ReadVector3f(Link.LimitMax)) return false;
				}
				
				Bone.IKLinks.Add(MoveTemp(Link));
			}
		}
		
		Model.Bones.Add(MoveTemp(Bone));
	}
	
	return true;
}

bool FPmxReader::ReadMorphs(FPmxModel& Model)
{
	int32 MorphCount;
	if (!ReadValue(MorphCount))
		return false;
	
	if (MorphCount < 0 || MorphCount > 10000) // Sanity check
	{
		LogError(FString::Printf(TEXT("Invalid morph count: %d"), MorphCount));
		return false;
	}
	
	Model.Morphs.Reserve(MorphCount);
	
	bool bUTF8 = (Model.Header.EncodeType == 1);
	
	for (int32 i = 0; i < MorphCount; ++i)
	{
		FPmxMorph Morph;
		
		if (!ReadString(Morph.Name, bUTF8)) return false;
		if (!ReadString(Morph.NameEng, bUTF8)) return false;
		if (!ReadValue(Morph.ControlPanel)) return false;
		if (!ReadValue(Morph.MorphType)) return false;
		
		int32 MorphDataCount;
		if (!ReadValue(MorphDataCount)) return false;
		
		if (MorphDataCount < 0)
		{
			LogError(FString::Printf(TEXT("Invalid morph data count: %d"), MorphDataCount));
			return false;
		}
		
		// Clamp morph data count based on remaining bytes and minimal per-entry size
		auto ValidateOrAbort = [&](int32 MinBytesPerEntry)
		{
			const int32 Remaining = Data.Num() - Position;
			if (MinBytesPerEntry <= 0)
			{
				return true; // nothing to validate
			}
			const int64 Needed = (int64)MorphDataCount * (int64)MinBytesPerEntry;
			if (MorphDataCount > 100000 || Needed > Remaining)
			{
				UE_LOG(LogPmxReader, Warning, TEXT("Morph data count %d (type %d) exceeds remaining bytes (%d) with entry size %d. Aborting morphs parsing and continuing without morphs."),
					MorphDataCount, (int32)Morph.MorphType, Remaining, MinBytesPerEntry);
				return false; // abort morphs parsing safely
			}
			return true;
		};
		
		// Read morph data based on type
		switch (Morph.MorphType)
		{
			case 0: // Group morph
			   if (!ValidateOrAbort(Model.Header.MorphIndexSize + 4 /*ratio*/)) return false;
				Morph.GroupMorphs.Reserve(MorphDataCount);
				for (int32 j = 0; j < MorphDataCount; ++j)
				{
					FPmxGroupMorph GroupMorph;
					if (!ReadIndex(GroupMorph.MorphIndex, Model.Header.MorphIndexSize)) return false;
					if (!ReadValue(GroupMorph.MorphRatio)) return false;
					Morph.GroupMorphs.Add(MoveTemp(GroupMorph));
				}
				break;
			
			case 1: // Vertex morph
		   if (!ValidateOrAbort(Model.Header.VertexIndexSize + 12 /*3 floats*/)) return false;
				Morph.VertexMorphs.Reserve(MorphDataCount);
				for (int32 j = 0; j < MorphDataCount; ++j)
				{
					FPmxVertexMorph VertexMorph;
					if (!ReadIndex(VertexMorph.VertexIndex, Model.Header.VertexIndexSize)) return false;
					if (!ReadVector3f(VertexMorph.Offset)) return false;
					Morph.VertexMorphs.Add(MoveTemp(VertexMorph));
				}
				break;
			
			case 2: // Bone morph
		   if (!ValidateOrAbort(Model.Header.BoneIndexSize + 12 /*translation*/ + 16 /*quat*/)) return false;
				Morph.BoneMorphs.Reserve(MorphDataCount);
				for (int32 j = 0; j < MorphDataCount; ++j)
				{
					FPmxBoneMorph BoneMorph;
					if (!ReadIndex(BoneMorph.BoneIndex, Model.Header.BoneIndexSize)) return false;
					if (!ReadVector3f(BoneMorph.Translation)) return false;
					if (!ReadQuatf(BoneMorph.Rotation)) return false;
					Morph.BoneMorphs.Add(MoveTemp(BoneMorph));
				}
				break;
			
			case 3: case 4: case 5: case 6: case 7: // UV morphs
		   if (!ValidateOrAbort(Model.Header.VertexIndexSize + 16 /*4 floats*/)) return false;
				Morph.UVMorphs.Reserve(MorphDataCount);
				for (int32 j = 0; j < MorphDataCount; ++j)
				{
					FPmxUVMorph UVMorph;
					if (!ReadIndex(UVMorph.VertexIndex, Model.Header.VertexIndexSize)) return false;
					if (!ReadValue(UVMorph.Offset.X)) return false;
					if (!ReadValue(UVMorph.Offset.Y)) return false;
					if (!ReadValue(UVMorph.Offset.Z)) return false;
					if (!ReadValue(UVMorph.Offset.W)) return false;
					Morph.UVMorphs.Add(MoveTemp(UVMorph));
				}
				break;
			
			case 8: // Material morph
				// Minimum size estimate per PMX2.1: idx + offsetType + diffuse(16) + specular(12) + specPower(4) + ambient(12) + edgeColor(16) + edgeSize(4) + tex(16) + sphere(16) + toon(16)
		   if (!ValidateOrAbort(Model.Header.MaterialIndexSize + 1 + 16 + 12 + 4 + 12 + 16 + 4 + 16 + 16 + 16)) return false;
				Morph.MaterialMorphs.Reserve(MorphDataCount);
				for (int32 j = 0; j < MorphDataCount; ++j)
				{
					FPmxMaterialMorph MaterialMorph;
					if (!ReadIndex(MaterialMorph.MaterialIndex, Model.Header.MaterialIndexSize)) return false;
					if (!ReadValue(MaterialMorph.OffsetType)) return false;
					if (!ReadLinearColor(MaterialMorph.Diffuse)) return false;
					// PMX: Specular is 3 floats (RGB) + separate SpecularStrength (float)
					if (!ReadValue(MaterialMorph.Specular.R)) return false;
					if (!ReadValue(MaterialMorph.Specular.G)) return false;
					if (!ReadValue(MaterialMorph.Specular.B)) return false;
					MaterialMorph.Specular.A = 1.0f;
					if (!ReadValue(MaterialMorph.SpecularStrength)) return false;
					// PMX: Ambient is 3 floats (RGB)
					if (!ReadValue(MaterialMorph.Ambient.R)) return false;
					if (!ReadValue(MaterialMorph.Ambient.G)) return false;
					if (!ReadValue(MaterialMorph.Ambient.B)) return false;
					MaterialMorph.Ambient.A = 1.0f;
					if (!ReadLinearColor(MaterialMorph.EdgeColor)) return false;
					if (!ReadValue(MaterialMorph.EdgeSize)) return false;
					if (!ReadValue(MaterialMorph.TextureColor.X)) return false;
					if (!ReadValue(MaterialMorph.TextureColor.Y)) return false;
					if (!ReadValue(MaterialMorph.TextureColor.Z)) return false;
					if (!ReadValue(MaterialMorph.TextureColor.W)) return false;
					if (!ReadValue(MaterialMorph.SphereTextureColor.X)) return false;
					if (!ReadValue(MaterialMorph.SphereTextureColor.Y)) return false;
					if (!ReadValue(MaterialMorph.SphereTextureColor.Z)) return false;
					if (!ReadValue(MaterialMorph.SphereTextureColor.W)) return false;
					if (!ReadValue(MaterialMorph.ToonTextureColor.X)) return false;
					if (!ReadValue(MaterialMorph.ToonTextureColor.Y)) return false;
					if (!ReadValue(MaterialMorph.ToonTextureColor.Z)) return false;
					if (!ReadValue(MaterialMorph.ToonTextureColor.W)) return false;
					Morph.MaterialMorphs.Add(MoveTemp(MaterialMorph));
				}
				break;
			
		default:
			LogError(FString::Printf(TEXT("Unsupported morph type: %d"), Morph.MorphType));
			return false;
		}
		
		Model.Morphs.Add(MoveTemp(Morph));
	}
	
	return true;
}

// Simplified implementations for remaining sections
bool FPmxReader::ReadDisplayFrames(FPmxModel& Model)
{
	int32 Count;
	if (!ReadValue(Count)) return false;
	
	// Skip display frames for MVP
	bool bUTF8 = (Model.Header.EncodeType == 1);
	for (int32 i = 0; i < Count; ++i)
	{
		FString Name, NameEng;
		uint8 SpecialFlag;
		int32 ElementCount;
		
		if (!ReadString(Name, bUTF8)) return false;
		if (!ReadString(NameEng, bUTF8)) return false;
		if (!ReadValue(SpecialFlag)) return false;
		if (!ReadValue(ElementCount)) return false;
		
		for (int32 j = 0; j < ElementCount; ++j)
		{
			uint8 ElementTarget;
			int32 ElementIndex;
			if (!ReadValue(ElementTarget)) return false;
			if (ElementTarget == 0)
			{
				if (!ReadIndex(ElementIndex, Model.Header.BoneIndexSize)) return false;
			}
			else
			{
				if (!ReadIndex(ElementIndex, Model.Header.MorphIndexSize)) return false;
			}
		}
	}
	
	return true;
}

bool FPmxReader::ReadRigidBodies(FPmxModel& Model)
{
	int32 RigidBodyCount;
	if (!ReadValue(RigidBodyCount))
		return false;
	
	if (RigidBodyCount < 0 || RigidBodyCount > 10000) // Sanity check
	{
		LogError(FString::Printf(TEXT("Invalid rigid body count: %d"), RigidBodyCount));
		return false;
	}
	
	Model.RigidBodies.Reserve(RigidBodyCount);
	
	bool bUTF8 = (Model.Header.EncodeType == 1);
	
	for (int32 i = 0; i < RigidBodyCount; ++i)
	{
		FPmxRigidBody RigidBody;
		
		if (!ReadString(RigidBody.Name, bUTF8)) return false;
		if (!ReadString(RigidBody.NameEng, bUTF8)) return false;
		
		if (!ReadIndex(RigidBody.RelatedBoneIndex, Model.Header.BoneIndexSize)) return false;
		if (!ReadValue(RigidBody.Group)) return false;
		if (!ReadValue(RigidBody.NonCollisionGroup)) return false;
		if (!ReadValue(RigidBody.Shape)) return false;
		if (!ReadVector3f(RigidBody.Size)) return false;
		if (!ReadVector3f(RigidBody.Position)) return false;
		if (!ReadVector3f(RigidBody.Rotation)) return false;
		if (!ReadValue(RigidBody.Mass)) return false;
		if (!ReadValue(RigidBody.MoveAttenuation)) return false;
		if (!ReadValue(RigidBody.RotationAttenuation)) return false;
		if (!ReadValue(RigidBody.Repulsion)) return false;
		if (!ReadValue(RigidBody.Friction)) return false;
		if (!ReadValue(RigidBody.PhysicsType)) return false;
		
		Model.RigidBodies.Add(MoveTemp(RigidBody));
	}
	
	return true;
}

bool FPmxReader::ReadJoints(FPmxModel& Model)
{
	int32 JointCount;
	if (!ReadValue(JointCount))
		return false;
	
	if (JointCount < 0 || JointCount > 10000) // Sanity check
	{
		LogError(FString::Printf(TEXT("Invalid joint count: %d"), JointCount));
		return false;
	}
	
	Model.Joints.Reserve(JointCount);
	
	bool bUTF8 = (Model.Header.EncodeType == 1);
	
	for (int32 i = 0; i < JointCount; ++i)
	{
		FPmxJoint Joint;
		
		if (!ReadString(Joint.Name, bUTF8)) return false;
		if (!ReadString(Joint.NameEng, bUTF8)) return false;
		
		if (!ReadValue(Joint.JointType)) return false;
		if (!ReadIndex(Joint.RigidBodyIndexA, Model.Header.RigidbodyIndexSize)) return false;
		if (!ReadIndex(Joint.RigidBodyIndexB, Model.Header.RigidbodyIndexSize)) return false;
		
		if (!ReadVector3f(Joint.Position)) return false;
		if (!ReadVector3f(Joint.Rotation)) return false;
		if (!ReadVector3f(Joint.MoveRestrictionMin)) return false;
		if (!ReadVector3f(Joint.MoveRestrictionMax)) return false;
		if (!ReadVector3f(Joint.RotationRestrictionMin)) return false;
		if (!ReadVector3f(Joint.RotationRestrictionMax)) return false;
		if (!ReadVector3f(Joint.SpringMoveCoefficient)) return false;
		if (!ReadVector3f(Joint.SpringRotationCoefficient)) return false;
		
		Model.Joints.Add(MoveTemp(Joint));
	}
	
	return true;
}

bool FPmxReader::ReadSoftBodies(FPmxModel& Model)
{
	int32 SoftBodyCount;
	if (!ReadValue(SoftBodyCount))
		return false;
	
	// Soft bodies are optional and complex - skip for MVP
	if (SoftBodyCount > 0)
	{
		LogError(TEXT("Soft bodies are not yet supported"));
		return false;
	}
	
	return true;
}

bool FPmxReader::IsValidPosition(int32 Size) const
{
	if (Position + Size > Data.Num())
	{
		LogError(FString::Printf(TEXT("Attempted to read beyond file end. Position: %d, Size: %d, FileSize: %d"), 
			Position, Size, Data.Num()));
		return false;
	}
	return true;
}

void FPmxReader::LogError(const FString& Message) const
{
	UE_LOG(LogPmxReader, Error, TEXT("%s (Position: 0x%08X)"), *Message, Position);
}

// Public interface functions
namespace PMXReader
{
	bool LoadPmxFromFile(const FString& FilePath, FPmxModel& OutModel)
	{
		TArray<uint8> FileData;
		if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
		{
			UE_LOG(LogPmxReader, Error, TEXT("Failed to load PMX file: %s"), *FilePath);
			return false;
		}
		
		FPmxReader Reader(FileData);
		return Reader.ReadPmxModel(OutModel);
	}
	
	bool LoadPmxFromData(const TArray<uint8>& Data, FPmxModel& OutModel)
	{
		FPmxReader Reader(Data);
		return Reader.ReadPmxModel(OutModel);
	}
}