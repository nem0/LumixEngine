#include <imgui/imgui.h>

#include "prefab_system.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/array.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/hash_map.h"
#include "engine/plugin.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/prefab.h"
#include "engine/reflection.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/serializer.h"
#include "engine/string.h"
#include "engine/universe.h"

namespace Lumix
{


struct AssetBrowserPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	AssetBrowserPlugin(StudioApp& app, PrefabSystem& system)
		: system(system)
		, editor(app.getWorldEditor())
		, app(app)
	{
		app.getAssetCompiler().registerExtension("fab", PrefabResource::TYPE);
	}


	void onGUI(Span<Resource*> resources) override
	{
		if (resources.length() > 1) return;

		if (ImGui::Button("instantiate")) {
			Array<EntityRef> entities(editor.getAllocator());
			system.instantiatePrefab(*(PrefabResource*)resources[0], editor.getCameraRaycastHit(), {0, 0, 0, 1}, 1);
		}
	}
	
	
	bool compile(const Path& src) override
	{
		return app.getAssetCompiler().copyCompile(src);
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Prefab"; }
	ResourceType getResourceType() const override { return PrefabResource::TYPE; }


	PrefabSystem& system;
	WorldEditor& editor;
	StudioApp& app;
};


struct PrefabSystemImpl final : PrefabSystem
{
	struct InstantiatePrefabCommand final : IEditorCommand
	{
		InstantiatePrefabCommand(WorldEditor& editor)
			: editor(editor)
		{
		}


		~InstantiatePrefabCommand()
		{
			prefab->getResourceManager().unload(*prefab);
		}


		bool isReady() override
		{
			return prefab->isReady() || prefab->isFailure();
		}


		void destroyEntityRecursive(EntityPtr entity) const
		{
			if (!entity.isValid()) return;
			
			EntityRef e = (EntityRef)entity;
			Universe& universe = *editor.getUniverse();
			destroyEntityRecursive(universe.getFirstChild(e));
			destroyEntityRecursive(universe.getNextSibling(e));

			universe.destroyEntity(e);

		}


		bool execute() override
		{
			entity = INVALID_ENTITY;
			if (prefab->isFailure()) return false;
			
			ASSERT(prefab->isReady());
			auto& system = (PrefabSystemImpl&)editor.getPrefabSystem();

			entity = system.doInstantiatePrefab(*prefab, position, rotation, scale);
			if (!entity.isValid()) return false;

			return true;
		}


		void undo() override
		{
			ASSERT(entity.isValid());

			Universe& universe = *editor.getUniverse();

			EntityRef e = (EntityRef)entity;
			destroyEntityRecursive(universe.getFirstChild(e));
			universe.destroyEntity(e);

			entity = INVALID_ENTITY;
		}


		const char* getType() override
		{
			return "instantiate_prefab";
		}


		bool merge(IEditorCommand& command) override { return false; }

		PrefabResource* prefab;
		DVec3 position;
		Quat rotation;
		float scale;
		WorldEditor& editor;
		EntityPtr entity = INVALID_ENTITY;
	};

public:
	explicit PrefabSystemImpl(WorldEditor& editor)
		: m_editor(editor)
		, m_universe(nullptr)
		, m_roots(editor.getAllocator())
		, m_resources(editor.getAllocator())
		, m_entity_to_prefab(editor.getAllocator())
		, m_deferred_instances(editor.getAllocator())
	{
		editor.universeCreated().bind<&PrefabSystemImpl::onUniverseCreated>(this);
		editor.universeDestroyed().bind<&PrefabSystemImpl::onUniverseDestroyed>(this);
		setUniverse(editor.getUniverse());
	}


	~PrefabSystemImpl()
	{
		m_editor.universeCreated().unbind<&PrefabSystemImpl::onUniverseCreated>(this);
		m_editor.universeDestroyed().unbind<&PrefabSystemImpl::onUniverseDestroyed>(this);
		setUniverse(nullptr);
	}


	WorldEditor& getEditor() const { return m_editor; }


	void setUniverse(Universe* universe)
	{
		if (m_universe)
		{
			m_universe->entityDestroyed().unbind<&PrefabSystemImpl::onEntityDestroyed>(this);
		}
		m_universe = universe;
		if (m_universe)
		{
			m_universe->entityDestroyed().bind<&PrefabSystemImpl::onEntityDestroyed>(this);
		}
	}


	void onUniverseCreated()
	{
		m_roots.clear();
		for (const PrefabVersion& prefab : m_resources) {
			prefab.resource->getResourceManager().unload(*prefab.resource);
		}
		m_resources.clear();
		m_entity_to_prefab.clear();
		setUniverse(m_editor.getUniverse());
	}


	void onUniverseDestroyed()
	{
		m_roots.clear();
		for (const PrefabVersion& prefab : m_resources) {
			prefab.resource->getResourceManager().unload(*prefab.resource);
		}
		m_resources.clear();
		m_entity_to_prefab.clear();
		setUniverse(nullptr);
	}


	void onEntityDestroyed(EntityRef entity)
	{
		if (entity.index >= m_entity_to_prefab.size()) return;

		const PrefabHandle prefab = m_entity_to_prefab[entity.index];
		if (prefab == 0) return;

		m_entity_to_prefab[entity.index] = 0;
		m_roots.erase(entity);
	}


	void setPrefab(EntityRef entity, PrefabHandle prefab) override
	{
		// TODO remove prefab (root entity), then undo it, does it work?
		reserve(entity);
		m_entity_to_prefab[entity.index] = prefab;
	}


	PrefabResource* getPrefabResource(EntityRef entity) override
	{
		if (entity.index >= m_entity_to_prefab.size()) return nullptr;
		auto iter = m_resources.find(m_entity_to_prefab[entity.index]);
		if (!iter.isValid()) return nullptr;
		return iter.value().resource;
	}


	PrefabHandle getPrefab(EntityRef entity) const override
	{
		if (entity.index >= m_entity_to_prefab.size()) return 0;
		return m_entity_to_prefab[entity.index];
	}


	void reserve(EntityRef entity)
	{
		while (entity.index >= m_entity_to_prefab.size())
		{
			m_entity_to_prefab.push(0);
		}
	}


	EntityPtr doInstantiatePrefab(PrefabResource& prefab_res, const DVec3& pos, const Quat& rot, float scale)
	{
		ASSERT(prefab_res.isReady());
		if (!m_resources.find(prefab_res.getPath().getHash()).isValid())
		{
			m_resources.insert(prefab_res.getPath().getHash(), {prefab_res.content_hash, &prefab_res});
			prefab_res.getResourceManager().load(prefab_res);
		}
		
		EntityMap entity_map(m_editor.getAllocator());
		if (!m_editor.getEngine().instantiatePrefab(*m_universe, prefab_res, pos, rot, scale, Ref(entity_map))) {
			logError("Editor") << "Failed to instantiate prefab " << prefab_res.getPath();
			return INVALID_ENTITY;
		}

		const PrefabHandle prefab = prefab_res.getPath().getHash();
		for (const EntityPtr& e : entity_map.m_map) {
			setPrefab((EntityRef)e, prefab);
		}

		const EntityRef root = (EntityRef)entity_map.m_map[0];
		m_roots.insert(root, prefab);
		return root;
	}


	EntityPtr instantiatePrefab(PrefabResource& prefab, const DVec3& pos, const Quat& rot, float scale) override
	{
		InstantiatePrefabCommand* cmd = LUMIX_NEW(m_editor.getAllocator(), InstantiatePrefabCommand)(m_editor);
		cmd->position = pos;
		prefab.getResourceManager().load(prefab);
		cmd->prefab = &prefab;
		cmd->rotation = rot;
		cmd->scale = scale;
		m_editor.executeCommand(cmd);
		return cmd->entity;
	}


	EntityRef getPrefabRoot(EntityRef entity) const
	{
		EntityRef root = entity;
		EntityPtr parent = m_universe->getParent(root);
		while (parent.isValid() && getPrefab((EntityRef)parent) != 0)
		{
			root = (EntityRef)parent;
			parent = m_universe->getParent(root);
		}
		return root;
	}


	struct PropertyCloner : Reflection::ISimpleComponentVisitor {
		void visitProperty(const Reflection::PropertyBase& prop) override {
			stream->clear();
			prop.getValue(src, -1, *stream);
			InputMemoryStream tmp(*stream);
			prop.setValue(dst, -1, tmp);
		}

		ComponentUID src;
		ComponentUID dst;
		OutputMemoryStream* stream;
	};


	EntityRef cloneEntity(Universe& src_u, EntityRef src_e, Universe& dst_u, EntityPtr dst_parent, Ref<Array<EntityRef>> entities) {
		entities->push(src_e);
		const EntityRef dst_e = dst_u.createEntity({0, 0, 0}, {0, 0, 0, 1});
		if (dst_parent.isValid()) {
			dst_u.setParent(dst_parent, dst_e);
		}
		const char* name = src_u.getEntityName(src_e);
		if (name[0]) {
			dst_u.setEntityName(dst_e, name);
		}

		const EntityPtr c = src_u.getFirstChild(src_e);
		if (c.isValid()) {
			cloneEntity(src_u, (EntityRef)c, dst_u, dst_e, entities);
		}

		if (dst_parent.isValid()) {
			const EntityPtr s = src_u.getNextSibling(src_e);
			if (s.isValid()) {
				cloneEntity(src_u, (EntityRef)s, dst_u, dst_parent, entities);
			}
		}

		OutputMemoryStream tmp_stream(m_editor.getAllocator());
		for (ComponentUID cmp = src_u.getFirstComponent(src_e); cmp.isValid(); cmp = src_u.getNextComponent(cmp)) {
			dst_u.createComponent(cmp.type, dst_e);

			const Reflection::ComponentBase* cmp_tpl = Reflection::getComponent(cmp.type);
	
			PropertyCloner property_cloner;
			property_cloner.src = cmp;
			property_cloner.dst.type = cmp.type;
			property_cloner.dst.entity = dst_e;
			property_cloner.dst.scene = dst_u.getScene(cmp.type);
			property_cloner.stream = &tmp_stream;
			cmp_tpl->visit(property_cloner);
		}

		return dst_e;
	}


	Universe& createPrefabUniverse(EntityRef src_e, Ref<Array<EntityRef>> entities) {
		Engine& engine = m_editor.getEngine();
		Universe& dst = engine.createUniverse(false);
		Universe& src = *m_editor.getUniverse();
		
		cloneEntity(src, src_e, dst, INVALID_ENTITY, entities);
		return dst;
	}


	static void destroySubtree(Universe& universe, EntityPtr entity)
	{
		if (!entity.isValid()) return;

		const EntityRef e = (EntityRef)entity;

		const EntityPtr child = universe.getFirstChild(e);
		destroySubtree(universe, child);

		const EntityPtr sib = universe.getNextSibling(e);
		destroySubtree(universe, sib);

		universe.destroyEntity(e);
	}


	void savePrefab(const Path& path) override
	{
		auto& selected_entities = m_editor.getSelectedEntities();
		if (selected_entities.size() != 1) return;

		EntityRef entity = selected_entities[0];
		if (getPrefab(entity) != 0) entity = getPrefabRoot(entity);

		Engine& engine = m_editor.getEngine();
		FileSystem& fs = engine.getFileSystem();
		OS::OutputFile file;
		if (!fs.open(path.c_str(), Ref(file)))
		{
			logError("Editor") << "Failed to create " << path.c_str();
			return;
		}

		OutputMemoryStream blob(m_editor.getAllocator());
		blob.reserve(4096);
		Array<EntityRef> src_entities(m_editor.getAllocator());
		src_entities.reserve(256);
		Universe& prefab_universe = createPrefabUniverse(entity, Ref(src_entities));
		engine.serialize(prefab_universe, blob);
		engine.destroyUniverse(prefab_universe);

		if (!file.write(blob.getData(), blob.getPos())) {
			logError("Editor") << "Failed to write " << path.c_str();
			file.close();
			return;
		}

		file.close();

		const PrefabHandle prefab = path.getHash();
		PrefabResource* prefab_res;
		if (m_resources.find(prefab).isValid()) {
			prefab_res = m_resources[prefab].resource;
			prefab_res->getResourceManager().reload(*prefab_res);

			// TODO undo/redo might keep references do prefab entities, handle that
			for (auto iter = m_roots.begin(), end = m_roots.end(); iter != end; ++iter) {
				if (iter.value() != prefab) continue;
				if (iter.key() == entity) continue;

				const Transform tr = m_universe->getTransform(iter.key());
				const EntityPtr parent = m_universe->getParent(iter.key());

				m_deferred_instances.push({prefab_res, tr, parent});
				destroySubtree(*m_universe, m_universe->getFirstChild(iter.key()));
				m_universe->destroyEntity(iter.key());
			}
		}
		else {
			ResourceManagerHub& resource_manager = engine.getResourceManager();
			prefab_res = resource_manager.load<PrefabResource>(path);
			const u32 content_hash = crc32(blob.getData(), (u32)blob.getPos());
			m_resources.insert(path.getHash(), { content_hash, prefab_res});
		}


		for (u32 i = 0; i < (u32)src_entities.size(); ++i) {
			setPrefab(src_entities[i], path.getHash());
		}
	}


	void recreateInstances(PrefabHandle prefab) {
		for (PrefabHandle p : m_entity_to_prefab) {
			if (p != prefab) continue;
			const i32 idx = i32(&p - m_entity_to_prefab.begin());
			const EntityRef e = {idx};
			if (!m_roots.find(e).isValid()) continue;

			const Transform tr = m_universe->getTransform(e);
			const EntityPtr parent = m_universe->getParent(e);

			m_deferred_instances.push({m_resources[prefab].resource, tr, parent});
			destroySubtree(*m_universe, m_universe->getFirstChild(e));
			m_universe->destroyEntity(e);
		}
	}


	void update() override {
		if (m_check_update) {
			// TODO interaction should probably be disabled until m_check_update becomes false
			bool all = true;
			for (PrefabVersion& prefab : m_resources) {
				if (prefab.resource->isEmpty()) {
					all = false;
					break;
				}
				else if (prefab.resource->isReady()) {
					if (prefab.resource->content_hash != prefab.content_hash) {
						recreateInstances(prefab.resource->getPath().getHash());
						prefab.content_hash = prefab.resource->content_hash;
					}
				}
				else {
					// TODO what now
					ASSERT(prefab.resource->isFailure()); 
				}
			}
			m_check_update = !all;
		}

		while (!m_deferred_instances.empty()) {
			PrefabResource* res = m_deferred_instances.back().resource;
			if (res->isFailure()) {
				logError("Editor") << "Failed to instantiate " << res->getPath();
				res->getResourceManager().unload(*res);
				m_deferred_instances.pop();
			} else if (res->isReady()) {
				DeferredInstance tmp = m_deferred_instances.back();
				const EntityPtr root = doInstantiatePrefab(*res, tmp.transform.pos, tmp.transform.rot, tmp.transform.scale);
				if (root.isValid()) {
					m_universe->setParent(tmp.parent, (EntityRef)root);
				}
				m_deferred_instances.pop();
			} else {
				break;
			}
		}
	}

	void serialize(IOutputStream& serializer) override
	{
		serializer.write((u32)m_entity_to_prefab.size());
		if (!m_entity_to_prefab.empty()) serializer.write(m_entity_to_prefab.begin(), m_entity_to_prefab.byte_size());

		serializer.write((u32)m_resources.size());
		for (const PrefabVersion& prefab : m_resources)
		{
			serializer.writeString(prefab.resource->getPath().c_str());
			serializer.write(prefab.content_hash);
		}
		serializer.write((u32)m_roots.size());
		for (auto iter = m_roots.begin(), end = m_roots.end(); iter != end; ++iter) {
			serializer.write(iter.key());
			serializer.write(iter.value());
		}
	}


	void deserialize(IInputStream& serializer, const EntityMap& entity_map) override
	{
		// TODO additive loading
		u32 count;
		serializer.read(count);
		m_entity_to_prefab.resize(count);
		if (count > 0) {
			serializer.read(m_entity_to_prefab.begin(), m_entity_to_prefab.byte_size());
		}

		serializer.read(count);
		ResourceManagerHub& resource_manager = m_editor.getEngine().getResourceManager();
		m_resources.reserve(count);
		for (u32 i = 0; i < count; ++i) {
			char tmp[MAX_PATH_LENGTH];
			serializer.readString(Span(tmp));
			u32 content_hash;
			serializer.read(content_hash);
			auto* res = resource_manager.load<PrefabResource>(Path(tmp));
			m_resources.insert(res->getPath().getHash(), {content_hash, res});
		}
		m_check_update = true;

		serializer.read(count);
		m_roots.reserve(count);
		for (u32 i = 0; i < count; ++i) {
			PrefabHandle p;
			EntityRef e;
			serializer.read(e);
			serializer.read(p);
			m_roots.insert(e, p);
		}
	}


private:
	struct DeferredInstance {
		PrefabResource* resource;
		Transform transform;
		EntityPtr parent;
	};

	struct PrefabVersion {
		u32 content_hash;
		PrefabResource* resource;
	};

	Array<PrefabHandle> m_entity_to_prefab;
	HashMap<EntityRef, PrefabHandle> m_roots;
	HashMap<PrefabHandle, PrefabVersion, HashFuncDirect<u32>> m_resources;
	Array<DeferredInstance> m_deferred_instances;
	Universe* m_universe;
	WorldEditor& m_editor;
	bool m_check_update = false;
}; // struct PrefabSystemImpl


PrefabSystem* PrefabSystem::create(WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), PrefabSystemImpl)(editor);
}


void PrefabSystem::destroy(PrefabSystem* system)
{
	LUMIX_DELETE(
		static_cast<PrefabSystemImpl*>(system)->getEditor().getAllocator(), system);
}


static AssetBrowserPlugin* ab_plugin = nullptr;


void PrefabSystem::createEditorPlugins(StudioApp& app, PrefabSystem& system)
{
	ab_plugin = LUMIX_NEW(app.getAllocator(), AssetBrowserPlugin)(app, system);
	app.getAssetBrowser().addPlugin(*ab_plugin);
	const char* extensions[] = { "fab", nullptr };
	app.getAssetCompiler().addPlugin(*ab_plugin, extensions);
}


void PrefabSystem::destroyEditorPlugins(StudioApp& app)
{
	app.getAssetBrowser().removePlugin(*ab_plugin);
	app.getAssetCompiler().removePlugin(*ab_plugin);
	LUMIX_DELETE(app.getAllocator(), ab_plugin);
	ab_plugin = nullptr;
}


} // namespace Lumix