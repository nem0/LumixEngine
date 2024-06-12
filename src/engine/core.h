#pragma once

#include "core/array.h"
#include "core/hash_map.h"

#include "engine/engine.h"
#include "engine/plugin.h"

namespace Lumix {

struct Vec3;

struct Spline {
	Spline(struct IAllocator& allocator);
	Array<Vec3> points;
};

struct CoreModule : IModule {
	virtual Spline& getSpline(EntityRef e) = 0;
	virtual const HashMap<EntityRef, Spline>& getSplines() = 0;
};

ISystem* createCorePlugin(Engine& engine);

}