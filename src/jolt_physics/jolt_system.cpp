#include "jolt_physics/jolt_module.h"
#include "jolt_physics/jolt_system.h"
#include "engine/allocators.h"
#include "engine/engine.h"
#include "engine/profiler.h"
#include "engine/world.h"

namespace Lumix {

struct JoltSystemImpl final : JoltSystem {
	JoltSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator(), "jolt")
	{
		JoltModule::reflect();
	}

	void createModules(World& world) {
		UniquePtr<JoltModule> module = JoltModule::create(*this, world, m_engine, m_allocator);
		world.addModule(module.move());
	}

	const char* getName() const override { return "jolt"; }
	i32 getVersion() const override { return 0; }
	void serialize(OutputMemoryStream& serializer) const override {}
	bool deserialize(i32 version, InputMemoryStream& serializer) override { return true; }

	IAllocator& getAllocator() override { return m_allocator; }

	Engine& m_engine;
	TagAllocator m_allocator;
};

LUMIX_PLUGIN_ENTRY(jolt_physics) {
	PROFILE_FUNCTION();
	return LUMIX_NEW(engine.getAllocator(), JoltSystemImpl)(engine);
}

} // namespace Lumix



