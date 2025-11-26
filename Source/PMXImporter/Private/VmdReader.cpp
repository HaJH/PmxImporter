// Copyright Epic Games, Inc. All Rights Reserved.

#include "VmdReader.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMisc.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY(LogVmdReader);

//////////////////////////////////////////////////////////////////////////
// FBinaryReader Implementation
//////////////////////////////////////////////////////////////////////////

bool FVmdReader::FBinaryReader::ReadBytes(void* OutBuffer, int64 Count)
{
	if (!HasRemaining(Count))
	{
		return false;
	}
	FMemory::Memcpy(OutBuffer, Data + Position, Count);
	Position += Count;
	return true;
}

bool FVmdReader::FBinaryReader::ReadInt8(int8& OutValue)
{
	return ReadBytes(&OutValue, sizeof(int8));
}

bool FVmdReader::FBinaryReader::ReadUInt8(uint8& OutValue)
{
	return ReadBytes(&OutValue, sizeof(uint8));
}

bool FVmdReader::FBinaryReader::ReadInt32(int32& OutValue)
{
	if (!ReadBytes(&OutValue, sizeof(int32)))
	{
		return false;
	}
	// VMD is little-endian, same as x86/x64
	return true;
}

bool FVmdReader::FBinaryReader::ReadUInt32(uint32& OutValue)
{
	if (!ReadBytes(&OutValue, sizeof(uint32)))
	{
		return false;
	}
	return true;
}

bool FVmdReader::FBinaryReader::ReadFloat(float& OutValue)
{
	if (!ReadBytes(&OutValue, sizeof(float)))
	{
		return false;
	}
	return true;
}

bool FVmdReader::FBinaryReader::ReadShiftJISString(int32 Length, FString& OutString)
{
	TArray<uint8> Bytes;
	Bytes.SetNum(Length);

	if (!ReadBytes(Bytes.GetData(), Length))
	{
		return false;
	}

	OutString = FVmdReader::DecodeShiftJIS(Bytes);
	return true;
}

bool FVmdReader::FBinaryReader::Skip(int64 Count)
{
	if (!HasRemaining(Count))
	{
		return false;
	}
	Position += Count;
	return true;
}

//////////////////////////////////////////////////////////////////////////
// FVmdReader Implementation
//////////////////////////////////////////////////////////////////////////

FString FVmdReader::DecodeShiftJIS(const TArray<uint8>& Bytes)
{
	if (Bytes.Num() == 0)
	{
		return FString();
	}

	// Find null terminator
	int32 Length = 0;
	for (int32 i = 0; i < Bytes.Num(); ++i)
	{
		if (Bytes[i] == 0)
		{
			break;
		}
		Length++;
	}

	if (Length == 0)
	{
		return FString();
	}

#if PLATFORM_WINDOWS
	// Use Windows API for Shift_JIS (codepage 932) conversion
	int32 WideLength = MultiByteToWideChar(932, 0,
		reinterpret_cast<const char*>(Bytes.GetData()), Length, nullptr, 0);

	if (WideLength > 0)
	{
		TArray<WCHAR> WideChars;
		WideChars.SetNum(WideLength + 1);
		MultiByteToWideChar(932, 0,
			reinterpret_cast<const char*>(Bytes.GetData()), Length,
			WideChars.GetData(), WideLength);
		WideChars[WideLength] = 0;
		return FString(WideChars.GetData());
	}
#endif

	// Fallback: Try UTF-8 or ASCII
	TArray<uint8> NullTerminated = Bytes;
	NullTerminated.SetNum(Length + 1);
	NullTerminated[Length] = 0;

	// First try UTF-8
	FString Result = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(NullTerminated.GetData())));

	// If result contains replacement characters, fall back to raw bytes
	if (Result.Contains(TEXT("\ufffd")))
	{
		// Simple ASCII fallback - replace non-ASCII with ?
		Result.Empty();
		for (int32 i = 0; i < Length; ++i)
		{
			if (Bytes[i] >= 32 && Bytes[i] < 127)
			{
				Result.AppendChar(static_cast<TCHAR>(Bytes[i]));
			}
			else if (Bytes[i] >= 0x80)
			{
				// Skip multi-byte character second byte
				if (i + 1 < Length)
				{
					Result.AppendChar(TEXT('?'));
					i++; // Skip next byte (part of multi-byte char)
				}
			}
		}
	}

	return Result;
}

bool FVmdReader::LoadFromFile(const FString& FilePath, FVmdModel& OutModel)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		SetError(FString::Printf(TEXT("Failed to load file: %s"), *FilePath));
		return false;
	}

	return LoadFromData(FileData.GetData(), FileData.Num(), OutModel);
}

bool FVmdReader::LoadFromData(const uint8* Data, int64 DataSize, FVmdModel& OutModel)
{
	OutModel.Reset();
	LastError.Empty();

	if (!Data || DataSize < 50) // Minimum size for header
	{
		SetError(TEXT("Invalid data or data too small"));
		return false;
	}

	FBinaryReader Reader(Data, DataSize);

	// Parse header
	if (!ParseHeader(Reader, OutModel))
	{
		return false;
	}

	// Parse bone keyframes
	if (!ParseBoneKeyframes(Reader, OutModel))
	{
		return false;
	}

	// Parse morph keyframes
	if (!ParseMorphKeyframes(Reader, OutModel))
	{
		return false;
	}

	// Parse camera keyframes (optional - may not exist in model motion files)
	if (Reader.GetRemaining() > 0)
	{
		if (!ParseCameraKeyframes(Reader, OutModel))
		{
			// Camera parsing failure is not fatal
			UE_LOG(LogVmdReader, Warning, TEXT("Failed to parse camera keyframes, continuing..."));
		}
	}

	// Parse light keyframes (optional)
	if (Reader.GetRemaining() > 0)
	{
		if (!ParseLightKeyframes(Reader, OutModel))
		{
			UE_LOG(LogVmdReader, Warning, TEXT("Failed to parse light keyframes, continuing..."));
		}
	}

	// Parse self-shadow keyframes (optional)
	if (Reader.GetRemaining() > 0)
	{
		if (!ParseSelfShadowKeyframes(Reader, OutModel))
		{
			UE_LOG(LogVmdReader, Warning, TEXT("Failed to parse self-shadow keyframes, continuing..."));
		}
	}

	// Parse property keyframes (optional)
	if (Reader.GetRemaining() > 0)
	{
		if (!ParsePropertyKeyframes(Reader, OutModel))
		{
			UE_LOG(LogVmdReader, Warning, TEXT("Failed to parse property keyframes, continuing..."));
		}
	}

	UE_LOG(LogVmdReader, Log, TEXT("VMD loaded: %s, Bones: %d keyframes (%d unique), Morphs: %d keyframes (%d unique), Camera: %d keyframes"),
		*OutModel.Header.ModelName,
		OutModel.BoneKeyframes.Num(), OutModel.GetUniqueBoneNames().Num(),
		OutModel.MorphKeyframes.Num(), OutModel.GetUniqueMorphNames().Num(),
		OutModel.CameraKeyframes.Num());

	return true;
}

bool FVmdReader::ParseHeader(FBinaryReader& Reader, FVmdModel& OutModel)
{
	// Read signature (30 bytes)
	TArray<uint8> SignatureBytes;
	SignatureBytes.SetNum(30);
	if (!Reader.ReadBytes(SignatureBytes.GetData(), 30))
	{
		SetError(TEXT("Failed to read VMD signature"));
		return false;
	}

	// Check signature
	FString Signature = DecodeShiftJIS(SignatureBytes);
	OutModel.Header.Signature = Signature;

	if (Signature.StartsWith(TEXT("Vocaloid Motion Data 0002")))
	{
		OutModel.Header.bIsVersion2 = true;
	}
	else if (Signature.StartsWith(TEXT("Vocaloid Motion Data file")))
	{
		OutModel.Header.bIsVersion2 = false;
	}
	else
	{
		SetError(FString::Printf(TEXT("Invalid VMD signature: %s"), *Signature));
		return false;
	}

	// Read model name (10 bytes for v1, 20 bytes for v2)
	int32 NameLength = OutModel.Header.bIsVersion2 ? 20 : 10;
	if (!Reader.ReadShiftJISString(NameLength, OutModel.Header.ModelName))
	{
		SetError(TEXT("Failed to read model name"));
		return false;
	}

	UE_LOG(LogVmdReader, Log, TEXT("VMD Header: Version=%s, Model=%s"),
		OutModel.Header.bIsVersion2 ? TEXT("2") : TEXT("1"),
		*OutModel.Header.ModelName);

	return true;
}

bool FVmdReader::ParseBoneKeyframes(FBinaryReader& Reader, FVmdModel& OutModel)
{
	uint32 Count = 0;
	if (!Reader.ReadUInt32(Count))
	{
		SetError(TEXT("Failed to read bone keyframe count"));
		return false;
	}

	if (Count > 10000000) // Sanity check
	{
		SetError(FString::Printf(TEXT("Bone keyframe count too large: %u"), Count));
		return false;
	}

	OutModel.BoneKeyframes.Reserve(Count);

	for (uint32 i = 0; i < Count; ++i)
	{
		FVmdBoneKeyframe Key;

		// Bone name (15 bytes)
		if (!Reader.ReadShiftJISString(15, Key.BoneName))
		{
			SetError(FString::Printf(TEXT("Failed to read bone name at keyframe %u"), i));
			return false;
		}

		// Frame number
		if (!Reader.ReadUInt32(Key.FrameNumber))
		{
			SetError(FString::Printf(TEXT("Failed to read frame number at keyframe %u"), i));
			return false;
		}

		// Position (X, Y, Z)
		float PosX, PosY, PosZ;
		if (!Reader.ReadFloat(PosX) || !Reader.ReadFloat(PosY) || !Reader.ReadFloat(PosZ))
		{
			SetError(FString::Printf(TEXT("Failed to read position at keyframe %u"), i));
			return false;
		}
		Key.Position = FVector(PosX, PosY, PosZ);

		// Rotation (X, Y, Z, W quaternion)
		float RotX, RotY, RotZ, RotW;
		if (!Reader.ReadFloat(RotX) || !Reader.ReadFloat(RotY) ||
			!Reader.ReadFloat(RotZ) || !Reader.ReadFloat(RotW))
		{
			SetError(FString::Printf(TEXT("Failed to read rotation at keyframe %u"), i));
			return false;
		}
		Key.Rotation = FQuat(RotX, RotY, RotZ, RotW);

		// Handle zero quaternion (all components near zero)
		if (FMath::IsNearlyZero(Key.Rotation.X) && FMath::IsNearlyZero(Key.Rotation.Y) &&
			FMath::IsNearlyZero(Key.Rotation.Z) && FMath::IsNearlyZero(Key.Rotation.W))
		{
			Key.Rotation = FQuat::Identity;
		}

		// Interpolation (64 bytes)
		if (!Reader.ReadBytes(Key.Interpolation.GetData(), 64))
		{
			SetError(FString::Printf(TEXT("Failed to read interpolation at keyframe %u"), i));
			return false;
		}

		OutModel.BoneKeyframes.Add(MoveTemp(Key));
	}

	return true;
}

bool FVmdReader::ParseMorphKeyframes(FBinaryReader& Reader, FVmdModel& OutModel)
{
	uint32 Count = 0;
	if (!Reader.ReadUInt32(Count))
	{
		SetError(TEXT("Failed to read morph keyframe count"));
		return false;
	}

	if (Count > 10000000) // Sanity check
	{
		SetError(FString::Printf(TEXT("Morph keyframe count too large: %u"), Count));
		return false;
	}

	OutModel.MorphKeyframes.Reserve(Count);

	for (uint32 i = 0; i < Count; ++i)
	{
		FVmdMorphKeyframe Key;

		// Morph name (15 bytes)
		if (!Reader.ReadShiftJISString(15, Key.MorphName))
		{
			SetError(FString::Printf(TEXT("Failed to read morph name at keyframe %u"), i));
			return false;
		}

		// Frame number
		if (!Reader.ReadUInt32(Key.FrameNumber))
		{
			SetError(FString::Printf(TEXT("Failed to read morph frame number at keyframe %u"), i));
			return false;
		}

		// Weight
		if (!Reader.ReadFloat(Key.Weight))
		{
			SetError(FString::Printf(TEXT("Failed to read morph weight at keyframe %u"), i));
			return false;
		}

		OutModel.MorphKeyframes.Add(MoveTemp(Key));
	}

	return true;
}

bool FVmdReader::ParseCameraKeyframes(FBinaryReader& Reader, FVmdModel& OutModel)
{
	uint32 Count = 0;
	if (!Reader.ReadUInt32(Count))
	{
		return false;
	}

	if (Count > 1000000) // Sanity check
	{
		SetError(FString::Printf(TEXT("Camera keyframe count too large: %u"), Count));
		return false;
	}

	OutModel.CameraKeyframes.Reserve(Count);

	for (uint32 i = 0; i < Count; ++i)
	{
		FVmdCameraKeyframe Key;

		// Frame number
		if (!Reader.ReadUInt32(Key.FrameNumber))
		{
			return false;
		}

		// Distance (negative value)
		if (!Reader.ReadFloat(Key.Distance))
		{
			return false;
		}

		// Target position
		float PosX, PosY, PosZ;
		if (!Reader.ReadFloat(PosX) || !Reader.ReadFloat(PosY) || !Reader.ReadFloat(PosZ))
		{
			return false;
		}
		Key.TargetPosition = FVector(PosX, PosY, PosZ);

		// Rotation (Euler angles in radians)
		float RotX, RotY, RotZ;
		if (!Reader.ReadFloat(RotX) || !Reader.ReadFloat(RotY) || !Reader.ReadFloat(RotZ))
		{
			return false;
		}
		Key.Rotation = FVector(RotX, RotY, RotZ);

		// Interpolation (24 bytes)
		if (!Reader.ReadBytes(Key.Interpolation.GetData(), 24))
		{
			return false;
		}

		// FOV
		if (!Reader.ReadUInt32(Key.FOV))
		{
			return false;
		}

		// Perspective flag
		uint8 PerspFlag;
		if (!Reader.ReadUInt8(PerspFlag))
		{
			return false;
		}
		Key.bPerspective = (PerspFlag == 0); // 0 = perspective, 1 = orthographic

		OutModel.CameraKeyframes.Add(MoveTemp(Key));
	}

	return true;
}

bool FVmdReader::ParseLightKeyframes(FBinaryReader& Reader, FVmdModel& OutModel)
{
	uint32 Count = 0;
	if (!Reader.ReadUInt32(Count))
	{
		return false;
	}

	if (Count > 1000000)
	{
		return false;
	}

	OutModel.LightKeyframes.Reserve(Count);

	for (uint32 i = 0; i < Count; ++i)
	{
		FVmdLightKeyframe Key;

		// Frame number
		if (!Reader.ReadUInt32(Key.FrameNumber))
		{
			return false;
		}

		// Color (R, G, B)
		float R, G, B;
		if (!Reader.ReadFloat(R) || !Reader.ReadFloat(G) || !Reader.ReadFloat(B))
		{
			return false;
		}
		Key.Color = FVector(R, G, B);

		// Direction (X, Y, Z)
		float DirX, DirY, DirZ;
		if (!Reader.ReadFloat(DirX) || !Reader.ReadFloat(DirY) || !Reader.ReadFloat(DirZ))
		{
			return false;
		}
		Key.Direction = FVector(DirX, DirY, DirZ);

		OutModel.LightKeyframes.Add(MoveTemp(Key));
	}

	return true;
}

bool FVmdReader::ParseSelfShadowKeyframes(FBinaryReader& Reader, FVmdModel& OutModel)
{
	uint32 Count = 0;
	if (!Reader.ReadUInt32(Count))
	{
		return false;
	}

	if (Count > 1000000)
	{
		return false;
	}

	OutModel.SelfShadowKeyframes.Reserve(Count);

	for (uint32 i = 0; i < Count; ++i)
	{
		FVmdSelfShadowKeyframe Key;

		// Frame number
		if (!Reader.ReadUInt32(Key.FrameNumber))
		{
			return false;
		}

		// Mode
		if (!Reader.ReadUInt8(Key.Mode))
		{
			return false;
		}

		// Distance
		float RawDistance;
		if (!Reader.ReadFloat(RawDistance))
		{
			return false;
		}
		Key.Distance = 10000.0f - RawDistance * 100000.0f;

		OutModel.SelfShadowKeyframes.Add(MoveTemp(Key));
	}

	return true;
}

bool FVmdReader::ParsePropertyKeyframes(FBinaryReader& Reader, FVmdModel& OutModel)
{
	uint32 Count = 0;
	if (!Reader.ReadUInt32(Count))
	{
		return false;
	}

	if (Count > 1000000)
	{
		return false;
	}

	OutModel.PropertyKeyframes.Reserve(Count);

	for (uint32 i = 0; i < Count; ++i)
	{
		FVmdPropertyKeyframe Key;

		// Frame number
		if (!Reader.ReadUInt32(Key.FrameNumber))
		{
			return false;
		}

		// Visibility
		uint8 Visible;
		if (!Reader.ReadUInt8(Visible))
		{
			return false;
		}
		Key.bVisible = (Visible != 0);

		// IK count
		uint32 IKCount;
		if (!Reader.ReadUInt32(IKCount))
		{
			return false;
		}

		if (IKCount > 10000)
		{
			return false;
		}

		Key.IKStates.Reserve(IKCount);

		for (uint32 j = 0; j < IKCount; ++j)
		{
			FVmdIKState IKState;

			// IK name (20 bytes, but only first 15 are valid per MMD format)
			TArray<uint8> NameBytes;
			NameBytes.SetNum(20);
			if (!Reader.ReadBytes(NameBytes.GetData(), 20))
			{
				return false;
			}
			NameBytes.SetNum(15); // Truncate to 15 bytes
			IKState.IKName = DecodeShiftJIS(NameBytes);

			// IK enabled
			uint8 Enabled;
			if (!Reader.ReadUInt8(Enabled))
			{
				return false;
			}
			IKState.bEnabled = (Enabled != 0);

			Key.IKStates.Add(MoveTemp(IKState));
		}

		OutModel.PropertyKeyframes.Add(MoveTemp(Key));
	}

	return true;
}

void FVmdReader::SetError(const FString& Message)
{
	LastError = Message;
	UE_LOG(LogVmdReader, Error, TEXT("%s"), *Message);
}
