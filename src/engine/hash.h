#pragma once

#include "engine/lumix.h"

namespace Lumix
{

// use if you want fast hash with low probability of collisions and size (8 bytes) is not an issue
// can change in future, do not serialize
struct LUMIX_ENGINE_API RuntimeHash {
	static RuntimeHash fromU64(u64 hash);
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
	static RuntimeHash32 fromU32(u32 hash);
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

struct RollingStableHasher {
	void begin();
	void update(const void* data, u32 len);
	StableHash end();
};

struct RollingHasher {
	void begin();
	void update(const void* data, u32 len);
	RuntimeHash32 end();
};

template <typename Key> struct HashFunc;

template<> struct HashFunc<RuntimeHash> {
	static u32 get(const RuntimeHash& k) {
		const u64 hash = k.getHashValue();
		return u32(hash ^ (hash >> 16));
	}
};

template<> struct HashFunc<StableHash> {
	static u32 get(const StableHash& k) {
		return k.getHashValue();
	}
};

template<> struct HashFunc<RuntimeHash32> {
	static u32 get(const RuntimeHash32& k) {
		return k.getHashValue();
	}
};

} // namespace Lumix