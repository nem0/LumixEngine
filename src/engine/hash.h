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
// 64bits 
struct LUMIX_ENGINE_API StableHash {
	static StableHash fromU64(u64 hash);
	StableHash() {}
	StableHash(const char* str);
	StableHash(const void* data, u32 len);

	bool operator != (StableHash rhs) const { return hash != rhs.hash; }
	bool operator == (StableHash rhs) const { return hash == rhs.hash; }
	bool operator < (StableHash rhs) const { return hash < rhs.hash; }

	u64 getHashValue() const { return hash; }
private:
	u64 hash = 0;
};

// same as StableHash, but only 32bits
struct LUMIX_ENGINE_API StableHash32 {
	static StableHash32 fromU32(u32 hash);
	StableHash32() {}
	explicit StableHash32(const char* string);
	StableHash32(const void* data, u32 len);

	bool operator != (StableHash32 rhs) const { return hash != rhs.hash; }
	bool operator == (StableHash32 rhs) const { return hash == rhs.hash; }
	bool operator < (StableHash32 rhs) const { return hash < rhs.hash; }

	u32 getHashValue() const { return hash; }
private:
	u32 hash = 0;
};

using FilePathHash = StableHash;
using BoneNameHash = StableHash;

struct LUMIX_ENGINE_API RollingStableHasher {
	void begin();
	void update(const void* data, u32 len);
	StableHash32 end();
	StableHash end64();
};

struct LUMIX_ENGINE_API RollingHasher {
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
		const u64 hash = k.getHashValue();
		return u32(hash ^ (hash >> 16));
	}
};

template<> struct HashFunc<StableHash32> {
	static u32 get(const StableHash32& k) {
		return k.getHashValue();
	}
};

template<> struct HashFunc<RuntimeHash32> {
	static u32 get(const RuntimeHash32& k) {
		return k.getHashValue();
	}
};

} // namespace Lumix