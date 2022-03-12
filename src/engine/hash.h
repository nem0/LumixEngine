#pragma once


#include "engine/lumix.h"


namespace Lumix
{

LUMIX_ENGINE_API u32 crc32(const void* data, u32 length);
LUMIX_ENGINE_API u32 crc32(const char* str);
LUMIX_ENGINE_API u32 continueCrc32(u32 original_crc, const char* str);
LUMIX_ENGINE_API u32 continueCrc32(u32 original_crc, const void* data, u32 length);

// use if you want fast hash with low probability of collisions and size (8 bytes) is not an issue
// can change in future, do not serialize
struct LUMIX_ENGINE_API RuntimeHash {
	RuntimeHash() {}
	explicit RuntimeHash(const char* string);
	RuntimeHash(const void* data, u32 len);

	bool operator != (RuntimeHash rhs) const { return hash != rhs.hash; }
	bool operator == (RuntimeHash rhs) const { return hash == rhs.hash; }
	
	u64 getHashValue() const { return hash; }
private:
	u64 hash = 0;
};

// same as RuntimeHash, but only 32 bits
struct LUMIX_ENGINE_API RuntimeHash32 {
	RuntimeHash32() {}
	explicit RuntimeHash32(const char* string);
	RuntimeHash32(const void* data, u32 len);

	bool operator != (RuntimeHash32 rhs) const { return hash != rhs.hash; }
	bool operator == (RuntimeHash32 rhs) const { return hash == rhs.hash; }
	
	u32 getHashValue() const { return hash; }
private:
	u32 hash = 0;
};

// use if you want to serialize it
struct LUMIX_ENGINE_API StableHash {
	static StableHash fromU32(u32 hash);
	StableHash() {}
	explicit StableHash(const char* string);
	StableHash(const char* string, u32 len);
	StableHash(const u8* data, u32 len);

	bool operator != (StableHash rhs) const { return hash != rhs.hash; }
	bool operator == (StableHash rhs) const { return hash == rhs.hash; }
	bool operator < (StableHash rhs) const { return hash < rhs.hash; }

	u32 getHashValue() const { return hash; }
private:
	u32 hash = 0;
};

template <typename Key> struct HashFunc;

template<> struct HashFunc<RuntimeHash> {
	static u32 get(const RuntimeHash& k) {
		const u64 key = k.getHashValue();
		u64 tmp = (~key) + (key << 18);
		tmp = tmp ^ (tmp >> 31);
		tmp = tmp * 21;
		tmp = tmp ^ (tmp >> 11);
		tmp = tmp + (tmp << 6);
		tmp = tmp ^ (tmp >> 22);
		return (u32)tmp;
	}
};

template<> struct HashFunc<StableHash> {
	static u32 get(const StableHash& k) {
		return k.getHashValue();
	}
};


} // namespace Lumix