#pragma once

#include "core/array.h"
#include "core/hash_map.h"
#include "engine/engine.h"
#include "engine/plugin.h"


namespace Lumix {

namespace reflection {
	struct Module;
	struct EventBase;
	struct FunctionBase;
}

struct Vec3;

struct Spline {
	Spline(struct IAllocator& allocator);
	Array<Vec3> points;
};

struct Signal {
	EntityRef entity;
	reflection::Module* event_module = nullptr;
	reflection::EventBase* event = nullptr;
	reflection::Module* function_module = nullptr;
	reflection::FunctionBase* function = nullptr;
};

struct CoreModule : IModule {
	virtual Spline& getSpline(EntityRef e) = 0;
	virtual Signal& getSignal(EntityRef e) = 0;
	virtual const HashMap<EntityRef, Spline>& getSplines() = 0;
};

ISystem* createCorePlugin(Engine& engine);

}