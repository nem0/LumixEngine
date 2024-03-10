#pragma once

#include "engine/lumix.hpp"

#include "core/array.hpp"
#include "core/hash_map.hpp"

#include "engine/engine.hpp"
#include "engine/plugin.hpp"

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