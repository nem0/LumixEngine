#include <imgui/imgui.h>

#include "prefab_system.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/array.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/geometry.h"
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
		, app(app)
	{
		app.getAssetCompiler().registerExtension("fab", PrefabResource::TYPE);
	}


	void onGUI(Span<Resource*> resources) override {}
	
	
	bool compile(const Path& src) override
	{
		return app.getAssetCompiler().copyCompile(src);
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Prefab"; }
	ResourceType getResourceType() const override { return PrefabResource::TYPE; }


	PrefabSystem& system;
	StudioApp& app;
};


struct PrefabSystemImpl final : PrefabSystem
{
	struct InstantiatePrefabsCommand final : IEditorCommand
	{
		InstantiatePrefabsCommand(EntityPtr* output, PrefabResource& prefab, WorldEditor& editor)
			: editor(editor)
			, transforms(editor.getAllocator())
			, entities(editor.getAllocator())
			, output(output)
			, prefab(prefab)
		{
			ASSERT(prefab.isReady());
			prefab.incRefCount();
		}


		~InstantiatePrefabsCommand()
		{
			prefab.decRefCount();
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
			ASSERT(entities.empty());
			if (prefab.isFailure()) return false;
			
			entities.reserve(transforms.size());
			ASSERT(prefab.isReady());
			auto& system = (PrefabSystemImpl&)editor.getPrefabSystem();

			system.doInstantiatePrefabs(prefab, transforms, Ref(entities));
			if (output) {
				*output = entities[0];
				output = nullptr;
			}

			return !entities.empty();
		}


		void undo() override
		{
			ASSERT(!entities.empty());

			Universe& universe = *editor.getUniverse();

			for (EntityRef e : entities) {
				destroyEntityRecursive(universe.getFirstChild(e));
				universe.destroyEntity(e);
			}
			entities.clear();
		}


		const char* getType() override
		{
			return "instantiate_prefab";
		}


		bool merge(IEditorCommand& command) override { return false; }

		PrefabResource& prefab;
		Array<Transform> transforms;
		WorldEditor& editor;
		Array<EntityRef> entities;
		EntityPtr* output;
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
		setUniverse(editor.getUniverse());
	}


	~PrefabSystemImpl()
	{
		setUniverse(nullptr);
	}


	WorldEditor& getEditor() const { return m_editor; }


	void setUniverse(Universe* universe) override {
		if (universe == m_universe) return;

		if (m_universe) {
			m_universe->entityDestroyed().unbind<&PrefabSystemImpl::onEntityDestroyed>(this);
		}

		m_roots.clear();
		for (const PrefabVersion& prefab : m_resources) {
			prefab.resource->decRefCount();
		}
		m_resources.clear();
		m_entity_to_prefab.clear();
		m_universe = universe;
		if (m_universe) {
			m_universe->entityDestroyed().bind<&PrefabSystemImpl::onEntityDestroyed>(this);
		}
	}


	void onEntityDestroyed(EntityRef entity)
	{
		m_roots.erase(entity);
		
		if (entity.index >= m_entity_to_prefab.size()) return;
		const PrefabHandle prefab = m_entity_to_prefab[entity.index];
		if (prefab == 0) return;

		m_entity_to_prefab[entity.index] = 0;
	}


	void setPrefab(EntityRef entity, PrefabHandle prefab) override
	{
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
	

	void doInstantiatePrefabs(PrefabResource& prefab_res, const Array<Transform>& transforms, Ref<Array<EntityRef>> entities)
	{
		ASSERT(prefab_res.isReady());
		if (!m_resources.find(prefab_res.getPath().getHash()).isValid())
		{
			m_resources.insert(prefab_res.getPath().getHash(), {prefab_res.content_hash, &prefab_res});
			prefab_res.incRefCount();
		}
		
		Engine& engine = m_editor.getEngine();
		EntityMap entity_map(m_editor.getAllocator());
		const PrefabHandle prefab = prefab_res.getPath().getHash();
		m_roots.reserve(m_roots.size() + transforms.size());
		
		for (const Transform& tr : transforms) {
			entity_map.m_map.clear();
			if (!engine.instantiatePrefab(*m_universe, prefab_res, tr.pos, tr.rot, tr.scale, Ref(entity_map))) {
				logError("Editor") << "Failed to instantiate prefab " << prefab_res.getPath();
				return;
			}

			for (const EntityPtr& e : entity_map.m_map) {
				setPrefab((EntityRef)e, prefab);
			}

			const EntityRef root = (EntityRef)entity_map.m_map[0];
			m_roots.insert(root, prefab);
			entities->push(root);
		}
	}


	EntityPtr doInstantiatePrefab(PrefabResource& prefab_res, const DVec3& pos, const Quat& rot, float scale)
	{
		ASSERT(prefab_res.isReady());
		if (!m_resources.find(prefab_res.getPath().getHash()).isValid())
		{
			m_resources.insert(prefab_res.getPath().getHash(), {prefab_res.content_hash, &prefab_res});
			prefab_res.incRefCount();
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

	void instantiatePrefabs(struct PrefabResource& prefab, Span<struct Transform> transforms) override {
		UniquePtr<InstantiatePrefabsCommand> cmd = UniquePtr<InstantiatePrefabsCommand>::create(m_editor.getAllocator(), nullptr, prefab, m_editor);
		cmd->transforms.resize(transforms.length());
		memcpy(cmd->transforms.begin(), transforms.begin(), transforms.length() * sizeof(transforms[0]));
		m_editor.executeCommand(cmd.move());
	}

	EntityPtr instantiatePrefab(PrefabResource& prefab, const DVec3& pos, const Quat& rot, float scale) override
	{
		ASSERT(prefab.isReady());
		EntityPtr res;
		UniquePtr<InstantiatePrefabsCommand> cmd = UniquePtr<InstantiatePrefabsCommand>::create(m_editor.getAllocator(), &res, prefab, m_editor);
		cmd->transforms.push({pos, rot, scale});
		m_editor.executeCommand(cmd.move());
		return res;
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

	struct PropertyCloner : Reflection::IPropertyVisitor {
		template <typename T>
		void clone(const Reflection::Property<T>& prop) { prop.set(dst, index, prop.get(src, index)); }

		void visit(const Reflection::Property<float>& prop) override { clone(prop); }
		void visit(const Reflection::Property<int>& prop) override { clone(prop); }
		void visit(const Reflection::Property<u32>& prop) override { clone(prop); }
		void visit(const Reflection::Property<EntityPtr>& prop) override { 
			EntityPtr e = prop.get(src, index);
			auto iter = map->find(e);
			if (iter.isValid()) {
				e = iter.value();
			}
			else {
				e = INVALID_ENTITY;
			}
			prop.set(dst, index, e);
			clone(prop); 
		}
		void visit(const Reflection::Property<Vec2>& prop) override { clone(prop); }
		void visit(const Reflection::Property<Vec3>& prop) override { clone(prop); }
		void visit(const Reflection::Property<IVec3>& prop) override { clone(prop); }
		void visit(const Reflection::Property<Vec4>& prop) override { clone(prop); }
		void visit(const Reflection::Property<Path>& prop) override { clone(prop); }
		void visit(const Reflection::Property<bool>& prop) override { clone(prop); }
		void visit(const Reflection::Property<const char*>& prop) override { clone(prop); }
		
		void visit(const Reflection::IArrayProperty& prop) override {
			const i32 c = prop.getCount(src);
			while (prop.getCount(dst) < c) { prop.addItem(dst, prop.getCount(dst) - 1); }
			while (prop.getCount(dst) > c) { prop.removeItem(dst, prop.getCount(dst) - 1); }
			
			ASSERT(index == -1);
			for (int i = 0; i < c; ++i) {
				index = i;
				prop.visit(*this);
			}
			index = -1;
		}

		void visit(const Reflection::IDynamicProperties& prop) override { 
			for (u32 i = 0, c = prop.getCount(src, index); i < c; ++i) {
				const char* name = prop.getName(src, index, i);
				Reflection::IDynamicProperties::Type type = prop.getType(src, index, i);
				Reflection::IDynamicProperties::Value val = prop.getValue(src, index, i);
				if (type == Reflection::IDynamicProperties::ENTITY) {
					auto iter = map->find(val.e);
					if (iter.isValid()) {
						val.e = iter.value();
					}
					else {
						val.e = INVALID_ENTITY;
					}
				}
				prop.set(dst, index, name, type, val);
			}
		}
		void visit(const Reflection::IBlobProperty& prop) override { 
			OutputMemoryStream tmp(*allocator);
			prop.getValue(src, index, tmp);
			InputMemoryStream blob(tmp);
			prop.setValue(dst, index, blob);
		}

		const HashMap<EntityPtr, EntityPtr>* map; 
		IAllocator* allocator;
		ComponentUID src;
		ComponentUID dst;
		int index = -1;
	};


	EntityRef cloneEntity(Universe& src_u, EntityRef src_e, Universe& dst_u, EntityPtr dst_parent, Ref<Array<EntityRef>> entities, const HashMap<EntityPtr, EntityPtr>& map) {
		entities->push(src_e);
		const EntityRef dst_e = (EntityRef)map[src_e];
		if (dst_parent.isValid()) {
			dst_u.setParent(dst_parent, dst_e);
			dst_u.setLocalTransform(dst_e, src_u.getLocalTransform(src_e));
		}
		const char* name = src_u.getEntityName(src_e);
		if (name[0]) {
			dst_u.setEntityName(dst_e, name);
		}

		const EntityPtr c = src_u.getFirstChild(src_e);
		if (c.isValid()) {
			cloneEntity(src_u, (EntityRef)c, dst_u, dst_e, entities, map);
		}

		if (dst_parent.isValid()) {
			const EntityPtr s = src_u.getNextSibling(src_e);
			if (s.isValid()) {
				cloneEntity(src_u, (EntityRef)s, dst_u, dst_parent, entities, map);
			}
		}

		for (ComponentUID cmp = src_u.getFirstComponent(src_e); cmp.isValid(); cmp = src_u.getNextComponent(cmp)) {
			dst_u.createComponent(cmp.type, dst_e);

			const Reflection::ComponentBase* cmp_tpl = Reflection::getComponent(cmp.type);
	
			PropertyCloner property_cloner;
			property_cloner.allocator = &m_editor.getAllocator();
			property_cloner.src = cmp;
			property_cloner.dst.type = cmp.type;
			property_cloner.dst.entity = dst_e;
			property_cloner.dst.scene = dst_u.getScene(cmp.type);
			property_cloner.map = &map;
			cmp_tpl->visit(property_cloner);
		}

		return dst_e;
	}

	void cloneHierarchy(const Universe& src, EntityRef src_e, Universe& dst, bool clone_siblings, Ref<HashMap<EntityPtr, EntityPtr>> map) {
		const EntityPtr child = src.getFirstChild(src_e);
		const EntityPtr sibling = src.getNextSibling(src_e);

		const EntityRef dst_e = dst.createEntity({0, 0, 0}, Quat::IDENTITY);
		map->insert(src_e, dst_e);

		if (child.isValid()) {
			cloneHierarchy(src, (EntityRef)child, dst, true, map);
		}
		if (clone_siblings && sibling.isValid()) {
			cloneHierarchy(src, (EntityRef)sibling, dst, true, map);
		}
	}

	Universe& createPrefabUniverse(EntityRef src_e, Ref<Array<EntityRef>> entities) {
		Engine& engine = m_editor.getEngine();
		Universe& dst = engine.createUniverse(false);
		Universe& src = *m_editor.getUniverse();

		HashMap<EntityPtr, EntityPtr> map(m_editor.getAllocator());
		map.reserve(256);
		cloneHierarchy(src, src_e, dst, false, Ref(map));
		cloneEntity(src, src_e, dst, INVALID_ENTITY, entities, map);
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

	void breakPrefabRecursive(EntityRef e) {
		m_entity_to_prefab[e.index] = 0;
		const EntityPtr child = m_universe->getFirstChild(e);
		if (child.isValid()) {
			breakPrefabRecursive((EntityRef)child);
		}
		const EntityPtr sibling = m_universe->getNextSibling(e);
		if (sibling.isValid()) {
			breakPrefabRecursive((EntityRef)sibling);
		}
	}

	void breakPrefab(EntityRef e) override {
		const EntityRef root = getPrefabRoot(e);
		const EntityPtr child = m_universe->getFirstChild(root);
		if (child.isValid()) {
			breakPrefabRecursive((EntityRef)child);
		}
		m_entity_to_prefab[root.index] = 0;
		m_roots.erase(root);
	}

	void savePrefab(EntityRef entity, const Path& path) override
	{
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

		if (!file.write(blob.data(), blob.size())) {
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
			const u32 content_hash = crc32(blob.data(), (u32)blob.size());
			m_resources.insert(path.getHash(), { content_hash, prefab_res});
			m_roots.insert(entity, prefab);
		}


		for (u32 i = 0; i < (u32)src_entities.size(); ++i) {
			setPrefab(src_entities[i], path.getHash());
		}
	}


	void recreateInstances(PrefabHandle prefab) {
		for (PrefabHandle& p : m_entity_to_prefab) {
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
				res->decRefCount();
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

	void serialize(OutputMemoryStream& serializer) override
	{
		serializer.write((u32)m_entity_to_prefab.size());
		if (!m_entity_to_prefab.empty()) serializer.write(m_entity_to_prefab.begin(), m_entity_to_prefab.byte_size());

		u32 res_count = 0;
		for (PrefabVersion& prefab : m_resources) prefab.instance_count = 0;
		for (auto iter = m_roots.begin(); iter.isValid(); ++iter) {
			PrefabVersion& p = m_resources[iter.value()];
			if (p.instance_count == 0) ++res_count;
			++p.instance_count;
		}

		serializer.write(res_count);

		for (const PrefabVersion& prefab : m_resources) {
			if (prefab.instance_count == 0) continue;

			serializer.writeString(prefab.resource->getPath().c_str());
			serializer.write(prefab.content_hash);
		}

		serializer.write((u32)m_roots.size());
		for (auto iter = m_roots.begin(), end = m_roots.end(); iter != end; ++iter) {
			serializer.write(iter.key());
			serializer.write(iter.value());
		}
	}


	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map) override
	{
		u32 count;
		serializer.read(count);
		m_entity_to_prefab.reserve(count);
		for (u32 i = 0; i < count; ++i) {
			const EntityPtr e = entity_map.get(EntityPtr{(i32)i});
			PrefabHandle prefab;
			serializer.read(prefab);

			if (!e.isValid()) continue;
			while (e.index >= m_entity_to_prefab.size()) {
				m_entity_to_prefab.push(0);
			}
			m_entity_to_prefab[e.index] = prefab;
		}

		serializer.read(count);
		ResourceManagerHub& resource_manager = m_editor.getEngine().getResourceManager();
		m_resources.reserve(count);
		for (u32 i = 0; i < count; ++i) {
			const char* tmp = serializer.readString();
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
			e = entity_map.get(e);
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
		u32 instance_count = 0;
	};

	Array<PrefabHandle> m_entity_to_prefab;
	HashMap<EntityRef, PrefabHandle> m_roots;
	HashMap<PrefabHandle, PrefabVersion, HashFuncDirect<u32>> m_resources;
	Array<DeferredInstance> m_deferred_instances;
	Universe* m_universe;
	WorldEditor& m_editor;
	bool m_check_update = false;
}; // struct PrefabSystemImpl


UniquePtr<PrefabSystem> PrefabSystem::create(WorldEditor& editor)
{
	return UniquePtr<PrefabSystemImpl>::create(editor.getAllocator(), editor);
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