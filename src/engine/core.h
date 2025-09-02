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

//@ module CoreModule core "Core"
struct CoreModule : IModule {
	virtual Spline& getSpline(EntityRef e) = 0;
	virtual Signal& getSignal(EntityRef e) = 0;
	virtual const HashMap<EntityRef, Spline>& getSplines() = 0;

	//@ component Spline spline "Spline"
	virtual void getSplineBlob(EntityRef entity, OutputMemoryStream& value) = 0;	//@ blob
	virtual void setSplineBlob(EntityRef entity, InputMemoryStream& value) = 0;
	//@ end
	virtual void createSpline(EntityRef entity) = 0;
	virtual void destroySpline(EntityRef entity) = 0;

	//@ component Signal signal "Signal"
	virtual void getSignalBlob(EntityRef entity, OutputMemoryStream& value) = 0;	//@ blob
	virtual void setSignalBlob(EntityRef entity, InputMemoryStream& value) = 0;
	//@ end
	virtual void createSignal(EntityRef entity) = 0;
	virtual void destroySignal(EntityRef entity) = 0;
};

ISystem* createCorePlugin(Engine& engine);

}