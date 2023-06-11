#include "core.h"
#include "engine.h"
#include "hash_map.h"
#include "reflection.h"
#include "stream.h"
#include "world.h"

namespace Lumix {

static const ComponentType SPLINE_TYPE = reflection::getComponentType("spline");

Spline::Spline(IAllocator& allocator)
	: points(allocator)
{}

struct CoreModuleImpl : CoreModule {
	CoreModuleImpl(Engine& engine, ISystem& system, World& world)
		: m_system(system)
		, m_allocator(engine.getAllocator())
		, m_world(world)
		, m_splines(m_allocator)
	{}

	void serialize(OutputMemoryStream& serializer) override {
		serializer.write((u32)m_splines.size());
		for (auto iter = m_splines.begin(); iter.isValid(); ++iter) {
			const Spline& spline = iter.value();
			serializer.write(iter.key());
			serializer.write((u32)spline.points.size());
			serializer.write(spline.points.begin(), spline.points.byte_size());
		}
	}

	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override {
		u32 count;
		serializer.read(count);
		m_splines.reserve(m_splines.size() + count);
		for (u32 i = 0; i < count; ++i) {
			Spline spline(m_allocator);
			EntityRef e;
			serializer.read(e);
			e = entity_map.get(e);
			u32 pts_count;
			serializer.read(pts_count);
			spline.points.resize(pts_count);
			serializer.read(spline.points.begin(), spline.points.byte_size());
			
			m_splines.insert(e, static_cast<Spline&&>(spline));
			m_world.onComponentCreated(e, SPLINE_TYPE, this);
		}
	}

	const char* getName() const override { return "core"; }
	ISystem& getSystem() const override { return m_system; }
	void update(float time_delta) override {}
	World& getWorld() override { return m_world; }

	void createSpline(EntityRef e) {
		Spline spline(m_allocator);
		m_splines.insert(e, static_cast<Spline&&>(spline));
		m_world.onComponentCreated(e, SPLINE_TYPE, this);
	}
	
	void destroySpline(EntityRef e) {
		m_splines.erase(e);
		m_world.onComponentDestroyed(e, SPLINE_TYPE, this);
	}
	
	Spline& getSpline(EntityRef e) override {
		return m_splines[e];
	}

	const HashMap<EntityRef, Spline>& getSplines() override { return m_splines; }

	static void reflect() {
		LUMIX_MODULE(CoreModuleImpl, "core")
			.LUMIX_CMP(Spline, "spline", "Core / Spline")
		;
	}

	IAllocator& m_allocator;
	HashMap<EntityRef, Spline> m_splines;
	ISystem& m_system;
	World& m_world;
};

struct CorePlugin : ISystem {
	CorePlugin(Engine& engine)
		: m_engine(engine)
	{
		CoreModuleImpl::reflect();
	}

	const char* getName() const override { return "core"; }
	void serialize(OutputMemoryStream& serializer) const override {}
	bool deserialize(i32 version, InputMemoryStream& serializer) override { return version == 0; }

	void createModules(World& world) override {
		IAllocator& allocator = m_engine.getAllocator();
		UniquePtr<CoreModuleImpl> module = UniquePtr<CoreModuleImpl>::create(allocator, m_engine, *this, world);
		world.addModule(module.move());
	}

	Engine& m_engine;
};

ISystem* createCorePlugin(Engine& engine) {
	return LUMIX_NEW(engine.getAllocator(), CorePlugin)(engine);
}

} // namespace Lumix