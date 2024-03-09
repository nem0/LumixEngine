#pragma once

#include "core/core.hpp"
#include "core/hash.hpp"
#include "core/hash_map.hpp"
#include "core/string.hpp"

namespace Lumix {

struct EntityRef;

struct EntityPtr
{
	EntityPtr() : index(-1) {}
	explicit EntityPtr(i32 index) : index(index) {}
	i32 index;
	bool operator==(const EntityPtr& rhs) const { return rhs.index == index; }
	bool operator<(const EntityPtr& rhs) const { return rhs.index < index; }
	bool operator>(const EntityPtr& rhs) const { return rhs.index > index; }
	bool operator!=(const EntityPtr& rhs) const { return rhs.index != index; }
	operator bool() const { return index >= 0; }
	bool isValid() const { return index >= 0; }
	inline explicit operator EntityRef() const;
	inline EntityRef operator *() const;
};

struct EntityRef
{
	i32 index;
	bool operator==(const EntityRef& rhs) const { return rhs.index == index; }
	bool operator<(const EntityRef& rhs) const { return rhs.index < index; }
	bool operator>(const EntityRef& rhs) const { return rhs.index > index; }
	bool operator!=(const EntityRef& rhs) const { return rhs.index != index; }
	operator EntityPtr() const { return EntityPtr{index}; }
};

struct ComponentType
{
	enum { MAX_TYPES_COUNT = 64 };

	i32 index;
	bool operator==(const ComponentType& rhs) const { return rhs.index == index; }
	bool operator<(const ComponentType& rhs) const { return rhs.index < index; }
	bool operator>(const ComponentType& rhs) const { return rhs.index > index; }
	bool operator!=(const ComponentType& rhs) const { return rhs.index != index; }
};
const ComponentType INVALID_COMPONENT_TYPE = {-1};
const EntityPtr INVALID_ENTITY = EntityPtr{-1};

template <> struct HashFunc<ComponentType> {
	static u32 get(const ComponentType& key) {
		static_assert(sizeof(i32) == sizeof(key.index), "Check this");
		return HashFunc<i32>::get(key.index);
	}
};

template <> struct HashFunc<EntityRef> {
	static u32 get(const EntityRef& key) {
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

inline EntityPtr::operator EntityRef() const {
	ASSERT(isValid());
	return {index};
}

inline EntityRef EntityPtr::operator*() const {
	ASSERT(isValid());
	return {index};
}


inline void toCString(EntityPtr value, Span<char> output) {
	toCString(value.index, output);
}

inline const char* fromCString(StringView input, EntityPtr& value) {
	return fromCString(input, value.index);
}

#ifdef STATIC_PLUGINS
	#define LUMIX_ENGINE_API
#elif defined BUILDING_ENGINE
	#define LUMIX_ENGINE_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_ENGINE_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef STATIC_PLUGINS
	#define LUMIX_EDITOR_API
#elif defined BUILDING_EDITOR
	#define LUMIX_EDITOR_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_EDITOR_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef STATIC_PLUGINS
	#define LUMIX_RENDERER_API
#elif defined BUILDING_RENDERER
	#define LUMIX_RENDERER_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_RENDERER_API LUMIX_LIBRARY_IMPORT
#endif

namespace reflection { LUMIX_ENGINE_API ComponentType getComponentType(const char* id); }

} // namespace Lumix
