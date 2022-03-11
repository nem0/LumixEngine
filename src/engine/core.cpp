#include "core.h"
#include "engine.h"
#include "hash_map.h"
#include "reflection.h"
#include "stream.h"
#include "universe.h"

namespace Lumix {

static const ComponentType SPLINE_TYPE = reflection::getComponentType("spline");

Spline::Spline(IAllocator& allocator)
	: points(allocator)
{}

struct CoreSceneImpl : CoreScene {
	CoreSceneImpl(Engine& engine, IPlugin& plugin, Universe& universe)
		: m_plugin(plugin)
		, m_allocator(engine.getAllocator())
		, m_universe(universe)
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
			m_universe.onComponentCreated(e, SPLINE_TYPE, this);
		}
	}

	IPlugin& getPlugin() const override { return m_plugin; }
	void update(float time_delta, bool paused) override {}
	Universe& getUniverse() override { return m_universe; }
	void clear() override { m_splines.clear(); }

	void createSpline(EntityRef e) {
		Spline spline(m_allocator);
		m_splines.insert(e, static_cast<Spline&&>(spline));
		m_universe.onComponentCreated(e, SPLINE_TYPE, this);
	}
	
	void destroySpline(EntityRef e) {
		m_splines.erase(e);
		m_universe.onComponentDestroyed(e, SPLINE_TYPE, this);
	}
	
	Spline& getSpline(EntityRef e) override {
		return m_splines[e];
	}

	const HashMap<EntityRef, Spline>& getSplines() override { return m_splines; }

	static void reflect() {
		LUMIX_SCENE(CoreSceneImpl, "core")
			.LUMIX_CMP(Spline, "spline", "Core / Spline")
				//.LUMIX_PROP(BoneAttachmentParent, "Parent")
			;
	}

	IAllocator& m_allocator;
	HashMap<EntityRef, Spline> m_splines;
	IPlugin& m_plugin;
	Universe& m_universe;
};

struct CorePlugin : IPlugin {
	CorePlugin(Engine& engine)
		: m_engine(engine)
	{
		CoreSceneImpl::reflect();
	}

	const char* getName() const override { return "core"; }
	u32 getVersion() const override { return 0; }
	void serialize(OutputMemoryStream& serializer) const override {}
	bool deserialize(u32 version, InputMemoryStream& serializer) override { return true; }

	void createScenes(Universe& universe) override {
		IAllocator& allocator = m_engine.getAllocator();
		UniquePtr<CoreSceneImpl> scene = UniquePtr<CoreSceneImpl>::create(allocator, m_engine, *this, universe);
		universe.addScene(scene.move());
	}

	Engine& m_engine;
};

IPlugin* createCorePlugin(Engine& engine) {
	return LUMIX_NEW(engine.getAllocator(), CorePlugin)(engine);
}

} // namespace Lumix