#pragma once

#include "black.h.h"
#include "core/hash_map.h"
#include "core/string.h"

namespace black {

template <> struct HashFunc<ComponentType> {
	static u32 get(const ComponentType& key) {
		static_assert(sizeof(i32) == sizeof(key.index), "Check this");
		return HashFunc<i32>::get(key.index);
	}
};

template <> struct HashFunc<EntityRef> {
	BLACK_ENGINE_API static u32 get(const EntityRef& key) {
		static_assert(sizeof(i32) == sizeof(key.index), "Check this");
		return HashFunc<i32>::get(key.index);
	}
};

template <> struct HashFunc<EntityPtr> {
	static u32 get(const EntityPtr& key) {
		static_assert(sizeof(i32) == sizeof(key.index), "Check this");
		return HashFunc<i32>::get(key.index);
	}
};

template<>
struct HashFunc<String> {
	static u32 get(const String& key) {
		RuntimeHash32 h(key.c_str(), key.length());
		return h.getHashValue();
	}
};

template<>
struct HashFunc<StringView> {
	static u32 get(const StringView& key) {
		if (key.size() == 0) return 0;
		RuntimeHash32 h(key.begin, key.size());
		return h.getHashValue();
	}
};

} // namespace black