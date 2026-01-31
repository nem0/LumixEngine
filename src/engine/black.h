#pragma once

#include "core/core.h"

namespace black {

struct EntityRef;
struct StringView;

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

inline EntityPtr::operator EntityRef() const {
	ASSERT(isValid());
	return {index};
}

inline EntityRef EntityPtr::operator*() const {
	ASSERT(isValid());
	return {index};
}

#ifdef STATIC_PLUGINS
	#define BLACK_ENGINE_API
#elif defined BUILDING_ENGINE
	#define BLACK_ENGINE_API BLACK_LIBRARY_EXPORT
#else
	#define BLACK_ENGINE_API BLACK_LIBRARY_IMPORT
#endif

#ifdef STATIC_PLUGINS
	#define BLACK_EDITOR_API
#elif defined BUILDING_EDITOR
	#define BLACK_EDITOR_API BLACK_LIBRARY_EXPORT
#else
	#define BLACK_EDITOR_API BLACK_LIBRARY_IMPORT
#endif

#ifdef STATIC_PLUGINS
	#define BLACK_RENDERER_API
#elif defined BUILDING_RENDERER
	#define BLACK_RENDERER_API BLACK_LIBRARY_EXPORT
#else
	#define BLACK_RENDERER_API BLACK_LIBRARY_IMPORT
#endif

namespace reflection { 
	BLACK_ENGINE_API ComponentType getComponentType(StringView id);
}

} // namespace black
