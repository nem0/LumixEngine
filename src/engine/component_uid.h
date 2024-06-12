#pragma once

#include "lumix.h"

namespace Lumix {

struct IModule;

// contains necessary info to fully (==no other context needed) identify component at runtime
struct LUMIX_ENGINE_API ComponentUID final {
	ComponentUID() {
		module = nullptr;
		entity = INVALID_ENTITY;
		type = { -1 };
	}

	ComponentUID(EntityPtr entity, ComponentType type, IModule* module)
		: entity(entity)
		, type(type)
		, module(module) {}

	EntityPtr entity;
	ComponentType type;
	IModule* module;

	static const ComponentUID INVALID;

	bool operator==(const ComponentUID& rhs) const { return type == rhs.type && module == rhs.module && entity == rhs.entity; }
	bool isValid() const { return entity.isValid(); }
};

}