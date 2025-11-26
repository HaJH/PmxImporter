// Copyright (c) 2025 Jeonghyeon Ha. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VmdStructs.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVmdReader, Log, All);

/**
 * VMD (Vocaloid Motion Data) file reader.
 * Parses binary VMD files into FVmdModel structures.
 */
class PMXIMPORTER_API FVmdReader
{
public:
	FVmdReader() = default;

	/**
	 * Load VMD from file path
	 * @param FilePath Path to the .vmd file
	 * @param OutModel Output model to populate
	 * @return true if successful
	 */
	bool LoadFromFile(const FString& FilePath, FVmdModel& OutModel);

	/**
	 * Load VMD from memory buffer
	 * @param Data Pointer to data buffer
	 * @param DataSize Size of data in bytes
	 * @param OutModel Output model to populate
	 * @return true if successful
	 */
	bool LoadFromData(const uint8* Data, int64 DataSize, FVmdModel& OutModel);

	/** Get last error message */
	const FString& GetLastError() const { return LastError; }

private:
	/** Binary reader helper */
	class FBinaryReader
	{
	public:
		FBinaryReader(const uint8* InData, int64 InSize)
			: Data(InData), Size(InSize), Position(0) {}

		bool IsValid() const { return Data != nullptr && Size > 0; }
		bool HasRemaining(int64 Bytes) const { return Position + Bytes <= Size; }
		int64 GetPosition() const { return Position; }
		int64 GetRemaining() const { return Size - Position; }

		// Read raw bytes
		bool ReadBytes(void* OutBuffer, int64 Count);

		// Read primitive types (little-endian)
		bool ReadInt8(int8& OutValue);
		bool ReadUInt8(uint8& OutValue);
		bool ReadInt32(int32& OutValue);
		bool ReadUInt32(uint32& OutValue);
		bool ReadFloat(float& OutValue);

		// Read fixed-length string (Shift_JIS encoded, null-padded)
		bool ReadShiftJISString(int32 Length, FString& OutString);

		// Skip bytes
		bool Skip(int64 Count);

	private:
		const uint8* Data;
		int64 Size;
		int64 Position;
	};

	/** Decode Shift_JIS bytes to FString */
	static FString DecodeShiftJIS(const TArray<uint8>& Bytes);

	/** Parse sections */
	bool ParseHeader(FBinaryReader& Reader, FVmdModel& OutModel);
	bool ParseBoneKeyframes(FBinaryReader& Reader, FVmdModel& OutModel);
	bool ParseMorphKeyframes(FBinaryReader& Reader, FVmdModel& OutModel);
	bool ParseCameraKeyframes(FBinaryReader& Reader, FVmdModel& OutModel);
	bool ParseLightKeyframes(FBinaryReader& Reader, FVmdModel& OutModel);
	bool ParseSelfShadowKeyframes(FBinaryReader& Reader, FVmdModel& OutModel);
	bool ParsePropertyKeyframes(FBinaryReader& Reader, FVmdModel& OutModel);

	/** Set error message */
	void SetError(const FString& Message);

	FString LastError;
};
