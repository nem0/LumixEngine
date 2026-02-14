#include "core/allocator.h"
#include "core/log.h"
#include "core/tag_allocator.h"
#include "engine/engine.h"
#include "engine/plugin.h"
#include "gui_ng_system.h"

namespace Lumix {

struct GUINGSystemImpl : GUINGSystem {
	explicit GUINGSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator(), "gui_ng")
	{}

	Engine& getEngine() override { return m_engine; }

	const char* getName() const override { return "gui_ng"; }

	void serialize(OutputMemoryStream& stream) const override {}

	bool deserialize(i32 version, InputMemoryStream& stream) override { return version == 0; }

private:
	Engine& m_engine;
	TagAllocator m_allocator;
};

UniquePtr<ISystem> createGUINGSystem(Engine& engine, IAllocator& allocator) {
	return UniquePtr<GUINGSystemImpl>::create(allocator, engine);
}

} // namespace Lumix

LUMIX_PLUGIN_ENTRY(gui_ng) {
	return LUMIX_NEW(engine.getAllocator(), Lumix::GUINGSystemImpl)(engine);
}