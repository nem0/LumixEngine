#include "core/allocator.h"
#include "core/log.h"
#include "engine/engine.h"
#include "engine/reflection.h"
#include "engine/world.h"
#include "gui_ng_module.h"
#include "gui_ng_system.h"

namespace Lumix {

struct GUINGModuleImpl : GUINGModule {
	GUINGModuleImpl(GUINGSystem& system, World& world, IAllocator& allocator)
		: m_system(system)
		, m_world(world)
		, m_allocator(allocator)
	{}

	~GUINGModuleImpl() {}

	static UniquePtr<GUINGModule> createInstance(GUINGSystem& system, World& world, IAllocator& allocator) {
		return UniquePtr<GUINGModuleImpl>::create(allocator, system, world, allocator);
	}

	void serialize(OutputMemoryStream& serializer) override {
		// TODO: Implement serialization
	}

	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override {
		// TODO: Implement deserialization
	}

	const char* getName() const override {
		return "gui_ng";
	}

	ISystem& getSystem() const override {
		return m_system;
	}

	void update(float time_delta) override {
		// TODO: Implement update
	}

	World& getWorld() override {
		return m_world;
	}

private:
	GUINGSystem& m_system;
	World& m_world;
	IAllocator& m_allocator;
};

UniquePtr<GUINGModule> GUINGModule::createInstance(GUINGSystem& system, World& world, IAllocator& allocator) {
	return GUINGModuleImpl::createInstance(system, world, allocator);
}

} // namespace Lumix