#pragma once

#include "CoreMinimal.h"

// Dependency-free SHA-256.
//
// FPlatformMisc::GetSHA256Signature deliberately check-fails on platforms
// without an override (including the Windows editor configuration used by
// ShooterRoyal), so UE 5.8's own MCPClientToolset carries a local
// implementation for the same reason.
//
// NOTE: UnrealBridgeUE58Library.cpp still has an equivalent copy inside its
// anonymous namespace (internal linkage, so no ODR conflict with these inline
// functions). Folding that copy onto this header is a follow-up cleanup that
// was deliberately kept out of this batch to avoid touching that file right
// before a build.
namespace UnrealBridgeSha256
{
	inline uint32 RotateRight(uint32 Value, uint32 Count)
	{
		return (Value >> Count) | (Value << (32u - Count));
	}

	inline void Compute(const uint8* Message, uint64 MessageLength, uint8 OutHash[32])
	{
		static const uint32 Constants[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
		};

		uint32 State[8] = {
			0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
			0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
		};

		// Message + 0x80 + zero padding to 56 mod 64 + 8-byte big-endian bit length.
		const uint64 BitLength = MessageLength * 8ull;
		const uint64 PaddedLength = ((MessageLength + 9ull + 63ull) / 64ull) * 64ull;

		TArray<uint8> Buffer;
		Buffer.SetNumZeroed(static_cast<int32>(PaddedLength));
		if (MessageLength > 0)
		{
			FMemory::Memcpy(Buffer.GetData(), Message, static_cast<SIZE_T>(MessageLength));
		}
		Buffer[static_cast<int32>(MessageLength)] = 0x80;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Buffer[static_cast<int32>(PaddedLength) - 1 - Index] =
				static_cast<uint8>((BitLength >> (8 * Index)) & 0xffull);
		}

		for (uint64 Offset = 0; Offset < PaddedLength; Offset += 64)
		{
			uint32 Schedule[64];
			const uint8* Block = Buffer.GetData() + Offset;
			for (int32 Index = 0; Index < 16; ++Index)
			{
				Schedule[Index] =
					(static_cast<uint32>(Block[Index * 4 + 0]) << 24)
					| (static_cast<uint32>(Block[Index * 4 + 1]) << 16)
					| (static_cast<uint32>(Block[Index * 4 + 2]) << 8)
					| (static_cast<uint32>(Block[Index * 4 + 3]));
			}
			for (int32 Index = 16; Index < 64; ++Index)
			{
				const uint32 S0 = RotateRight(Schedule[Index - 15], 7)
					^ RotateRight(Schedule[Index - 15], 18)
					^ (Schedule[Index - 15] >> 3);
				const uint32 S1 = RotateRight(Schedule[Index - 2], 17)
					^ RotateRight(Schedule[Index - 2], 19)
					^ (Schedule[Index - 2] >> 10);
				Schedule[Index] = Schedule[Index - 16] + S0 + Schedule[Index - 7] + S1;
			}

			uint32 A = State[0], B = State[1], C = State[2], D = State[3];
			uint32 E = State[4], F = State[5], G = State[6], H = State[7];

			for (int32 Index = 0; Index < 64; ++Index)
			{
				const uint32 S1 = RotateRight(E, 6) ^ RotateRight(E, 11) ^ RotateRight(E, 25);
				const uint32 Choice = (E & F) ^ ((~E) & G);
				const uint32 Temp1 = H + S1 + Choice + Constants[Index] + Schedule[Index];
				const uint32 S0 = RotateRight(A, 2) ^ RotateRight(A, 13) ^ RotateRight(A, 22);
				const uint32 Majority = (A & B) ^ (A & C) ^ (B & C);
				const uint32 Temp2 = S0 + Majority;

				H = G; G = F; F = E;
				E = D + Temp1;
				D = C; C = B; B = A;
				A = Temp1 + Temp2;
			}

			State[0] += A; State[1] += B; State[2] += C; State[3] += D;
			State[4] += E; State[5] += F; State[6] += G; State[7] += H;
		}

		for (int32 Index = 0; Index < 8; ++Index)
		{
			OutHash[Index * 4 + 0] = static_cast<uint8>((State[Index] >> 24) & 0xffu);
			OutHash[Index * 4 + 1] = static_cast<uint8>((State[Index] >> 16) & 0xffu);
			OutHash[Index * 4 + 2] = static_cast<uint8>((State[Index] >> 8) & 0xffu);
			OutHash[Index * 4 + 3] = static_cast<uint8>((State[Index]) & 0xffu);
		}
	}

	/** Lowercase hex SHA-256 of a string's UTF-8 encoding. */
	inline FString HexOfString(const FString& Value)
	{
		FTCHARToUTF8 Utf8(*Value);
		uint8 Hash[32];
		Compute(
			reinterpret_cast<const uint8*>(Utf8.Get()),
			static_cast<uint64>(Utf8.Length()),
			Hash);
		return BytesToHex(Hash, UE_ARRAY_COUNT(Hash)).ToLower();
	}
}
