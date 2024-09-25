#pragma once

#include "lumix.h"
#include "core/hash_map.h"

namespace Lumix {

template <> struct HashFunc<ComponentType> {
	static u32 get(const ComponentType& key) {
		static_assert(sizeof(i32) == sizeof(key.index), "Check this");
		return HashFunc<i32>::get(key.index);
	}
};

template <> struct HashFunc<EntityRef> {
	LUMIX_ENGINE_API static u32 get(const EntityRef& key) {
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

} // namespace Lumix