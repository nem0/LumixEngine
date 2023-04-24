#include "prefab_system.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/entity_folders.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/array.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/hash.h"
#include "engine/hash_map.h"
#include "engine/plugin.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/prefab.h"
#include "engine/reflection.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/world.h"

namespace Lumix
{


struct AssetBrowserPlugin final : AssetBrowser::Plugin, AssetCompiler::IPlugin
{
	AssetBrowserPlugin(StudioApp& app, PrefabSystem& system)
		: system(system)
		, app(app)
		, AssetBrowser::Plugin(app.getAllocator())
	{
		app.getAssetCompiler().registerExtension("fab", PrefabResource::TYPE);
	}

	void deserialize(InputMemoryStream& blob) override { ASSERT(false); }
	void serialize(OutputMemoryStream& blob) override {}

	bool onGUI(Span<Resource*> resources) override { return false; }
	
	
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
			World& world = *editor.getWorld();
			destroyEntityRecursive(world.getFirstChild(e));
			destroyEntityRecursive(world.getNextSibling(e));

			world.destroyEntity(e);

		}


		bool execute() override
		{
			ASSERT(entities.empty());
			if (prefab.isFailure()) return false;
			
			entities.reserve(transforms.size());
			ASSERT(prefab.isReady());
			auto& system = (PrefabSystemImpl&)editor.getPrefabSystem();

			system.doInstantiatePrefabs(prefab, transforms, entities);
			if (output) {
				*output = entities[0];
				output = nullptr;
			}

			return !entities.empty();
		}


		void undo() override
		{
			ASSERT(!entities.empty());

			World& world = *editor.getWorld();

			for (EntityRef e : entities) {
				destroyEntityRecursive(world.getFirstChild(e));
				world.destroyEntity(e);
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
		, m_world(nullptr)
		, m_roots(editor.getAllocator())
		, m_resources(editor.getAllocator())
		, m_entity_to_prefab(editor.getAllocator())
		, m_deferred_instances(editor.getAllocator())
	{
		setWorld(editor.getWorld());
	}


	~PrefabSystemImpl()
	{
		setWorld(nullptr);
	}


	WorldEditor& getEditor() const { return m_editor; }


	void setWorld(World* world) override {
		if (world == m_world) return;

		if (m_world) {
			m_world->entityDestroyed().unbind<&PrefabSystemImpl::onEntityDestroyed>(this);
		}

		m_roots.clear();
		for (const PrefabVersion& prefab : m_resources) {
			prefab.resource->decRefCount();
		}
		m_resources.clear();
		m_entity_to_prefab.clear();
		m_world = world;
		if (m_world) {
			m_world->entityDestroyed().bind<&PrefabSystemImpl::onEntityDestroyed>(this);
		}
	}


	void onEntityDestroyed(EntityRef entity)
	{
		m_roots.erase(entity);
		
		if (entity.index >= m_entity_to_prefab.size()) return;
		const PrefabHandle prefab = m_entity_to_prefab[entity.index];
		if (prefab.getHashValue() == 0) return;

		m_entity_to_prefab[entity.index] = FilePathHash();
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
		if (entity.index >= m_entity_to_prefab.size()) return FilePathHash();
		return m_entity_to_prefab[entity.index];
	}


	void reserve(EntityRef entity)
	{
		while (entity.index >= m_entity_to_prefab.size())
		{
			m_entity_to_prefab.push(FilePathHash());
		}
	}
	

	void doInstantiatePrefabs(PrefabResource& prefab_res, const Array<Transform>& transforms, Array<EntityRef>& entities)
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
			if (!engine.instantiatePrefab(*m_world, prefab_res, tr.pos, tr.rot, tr.scale, entity_map)) {
				logError("Failed to instantiate prefab ", prefab_res.getPath());
				return;
			}

			for (const EntityPtr& e : entity_map.m_map) {
				setPrefab((EntityRef)e, prefab);
			}

			const EntityRef root = (EntityRef)entity_map.m_map[0];
			m_roots.insert(root, prefab);
			entities.push(root);
		}
	}


	EntityPtr doInstantiatePrefab(PrefabResource& prefab_res, const DVec3& pos, const Quat& rot, const Vec3& scale)
	{
		ASSERT(prefab_res.isReady());
		if (!m_resources.find(prefab_res.getPath().getHash()).isValid())
		{
			m_resources.insert(prefab_res.getPath().getHash(), {prefab_res.content_hash, &prefab_res});
			prefab_res.incRefCount();
		}
		
		EntityMap entity_map(m_editor.getAllocator());
		if (!m_editor.getEngine().instantiatePrefab(*m_world, prefab_res, pos, rot, scale, entity_map)) {
			logError("Failed to instantiate prefab ", prefab_res.getPath());
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

	EntityPtr instantiatePrefab(PrefabResource& prefab, const DVec3& pos, const Quat& rot, const Vec3& scale) override
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
		EntityPtr parent = m_world->getParent(root);
		while (parent.isValid() && getPrefab((EntityRef)parent).getHashValue() != 0)
		{
			root = (EntityRef)parent;
			parent = m_world->getParent(root);
		}
		return root;
	}

	void cloneHierarchy(const World& src, EntityRef src_e, World& dst, bool clone_siblings, HashMap<EntityPtr, EntityPtr>& map) {
		const EntityPtr child = src.getFirstChild(src_e);
		const EntityPtr sibling = src.getNextSibling(src_e);

		const EntityRef dst_e = dst.createEntity({0, 0, 0}, Quat::IDENTITY);
		map.insert(src_e, dst_e);

		if (child.isValid()) {
			cloneHierarchy(src, (EntityRef)child, dst, true, map);
		}
		if (clone_siblings && sibling.isValid()) {
			cloneHierarchy(src, (EntityRef)sibling, dst, true, map);
		}
	}

	World& createPrefabWorld(EntityRef src_e, Array<EntityRef>& entities) {
		Engine& engine = m_editor.getEngine();
		World& dst = engine.createWorld(false);
		World& src = *m_editor.getWorld();

		HashMap<EntityPtr, EntityPtr> map(m_editor.getAllocator());
		map.reserve(256);
		cloneHierarchy(src, src_e, dst, false, map);
		m_editor.cloneEntity(src, src_e, dst, INVALID_ENTITY, entities, map);
		return dst;
	}


	static void destroySubtree(World& world, EntityPtr entity)
	{
		if (!entity.isValid()) return;

		const EntityRef e = (EntityRef)entity;

		const EntityPtr child = world.getFirstChild(e);
		destroySubtree(world, child);

		const EntityPtr sib = world.getNextSibling(e);
		destroySubtree(world, sib);

		world.destroyEntity(e);
	}

	void breakPrefabRecursive(EntityRef e) {
		m_entity_to_prefab[e.index] = FilePathHash();
		const EntityPtr child = m_world->getFirstChild(e);
		if (child.isValid()) {
			breakPrefabRecursive((EntityRef)child);
		}
		const EntityPtr sibling = m_world->getNextSibling(e);
		if (sibling.isValid()) {
			breakPrefabRecursive((EntityRef)sibling);
		}
	}

	void breakPrefab(EntityRef e) override {
		const EntityRef root = getPrefabRoot(e);
		const EntityPtr child = m_world->getFirstChild(root);
		if (child.isValid()) {
			breakPrefabRecursive((EntityRef)child);
		}
		m_entity_to_prefab[root.index] = FilePathHash();
		m_roots.erase(root);
	}

	void savePrefab(EntityRef entity, const Path& path) override
	{
		if (getPrefab(entity).getHashValue() != 0) entity = getPrefabRoot(entity);

		Engine& engine = m_editor.getEngine();
		OutputMemoryStream blob(m_editor.getAllocator());
		blob.reserve(4096);
		Array<EntityRef> src_entities(m_editor.getAllocator());
		src_entities.reserve(256);
		World& prefab_world = createPrefabWorld(entity, src_entities);
		prefab_world.serialize(blob);
		engine.destroyWorld(prefab_world);

		FileSystem& fs = engine.getFileSystem();
		
		if (!fs.saveContentSync(path, blob)) {
			logError("Failed to save ", path);
			return;
		}

		const PrefabHandle prefab = path.getHash();
		PrefabResource* prefab_res;
		if (m_resources.find(prefab).isValid()) {
			prefab_res = m_resources[prefab].resource;
			prefab_res->getResourceManager().reload(*prefab_res);

			// TODO undo/redo might keep references do prefab entities, handle that
			for (auto iter = m_roots.begin(), end = m_roots.end(); iter != end; ++iter) {
				if (iter.value() != prefab) continue;
				if (iter.key() == entity) continue;

				const Transform tr = m_world->getTransform(iter.key());
				const EntityPtr parent = m_world->getParent(iter.key());

				m_deferred_instances.push({prefab_res, tr, parent});
				destroySubtree(*m_world, m_world->getFirstChild(iter.key()));
				m_world->destroyEntity(iter.key());
			}
		}
		else {
			ResourceManagerHub& resource_manager = engine.getResourceManager();
			prefab_res = resource_manager.load<PrefabResource>(path);
			const StableHash content_hash(blob.data(), (u32)blob.size());
			m_resources.insert(path.getHash(), { content_hash, prefab_res});
			m_roots.insert(entity, prefab);
		}


		for (u32 i = 0; i < (u32)src_entities.size(); ++i) {
			setPrefab(src_entities[i], path.getHash());
		}
	}


	void recreateInstances(PrefabHandle prefab) {
		EntityFolders& folders = m_editor.getEntityFolders();
		for (PrefabHandle& p : m_entity_to_prefab) {
			if (p != prefab) continue;
			const i32 idx = i32(&p - m_entity_to_prefab.begin());
			const EntityRef e = {idx};
			if (!m_roots.find(e).isValid()) continue;

			const Transform tr = m_world->getTransform(e);
			const EntityPtr parent = m_world->getParent(e);
			EntityFolders::FolderID folder = folders.getFolder(e);

			m_deferred_instances.push({m_resources[prefab].resource, tr, parent, folder});
			destroySubtree(*m_world, m_world->getFirstChild(e));
			m_world->destroyEntity(e);
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
					logError("Failed to load '", prefab.resource->getPath(), "'");
					ASSERT(prefab.resource->isFailure()); 
				}
			}
			m_check_update = !all;
		}

		EntityFolders& folders = m_editor.getEntityFolders();
		while (!m_deferred_instances.empty()) {
			PrefabResource* res = m_deferred_instances.back().resource;
			if (res->isFailure()) {
				logError("Failed to instantiate ", res->getPath());
				res->decRefCount();
				m_deferred_instances.pop();
			} else if (res->isReady()) {
				DeferredInstance tmp = m_deferred_instances.back();
				const EntityPtr root = doInstantiatePrefab(*res, tmp.transform.pos, tmp.transform.rot, tmp.transform.scale);
				if (root.isValid()) {
					folders.moveToFolder(*root, tmp.folder);
					m_world->setParent(tmp.parent, (EntityRef)root);
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


	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, WorldEditorHeaderVersion version) override
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
				m_entity_to_prefab.push(FilePathHash());
			}
			m_entity_to_prefab[e.index] = prefab;
		}

		serializer.read(count);
		ResourceManagerHub& resource_manager = m_editor.getEngine().getResourceManager();
		m_resources.reserve(count);
		for (u32 i = 0; i < count; ++i) {
			const char* tmp = serializer.readString();
			StableHash content_hash;
			if (version <= WorldEditorHeaderVersion::HASH64) {
				u32 dummy;
				serializer.read(dummy);
			}
			else {
				serializer.read(content_hash);
			}
			auto* res = resource_manager.load<PrefabResource>(Path(tmp));
			m_resources.insert(res->getPath().getHash(), {content_hash, res});
			if (!res->isReady()) m_check_update = true;
		}

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

	void cloneTo(PrefabSystem& dst_ps, const HashMap<EntityPtr, EntityPtr>& map) override {
		ASSERT(m_deferred_instances.empty());
		ASSERT(!m_check_update);
		PrefabSystemImpl& dst = static_cast<PrefabSystemImpl&>(dst_ps);

		auto get_mapped = [&](EntityPtr src) {
			if (!src.isValid()) return INVALID_ENTITY;
			auto iter = map.find(src);
			if (!iter.isValid()) return INVALID_ENTITY;
			return iter.value();
		};

		// clone roots
		dst.m_roots.reserve(m_roots.size());
		for (auto iter = m_roots.begin(), end = m_roots.end(); iter != end; ++iter) {
			const EntityPtr dst_e = get_mapped(iter.key());
			if (dst_e.isValid()) {
				dst.m_roots.insert(*dst_e, iter.value());
			}
		}

		// clone resources
		for (auto iter = dst.m_roots.begin(), end = dst.m_roots.end(); iter != end; ++iter) {
			const PrefabHandle prefab_handle = iter.value();
			auto res_iter = dst.m_resources.find(prefab_handle);
			if (res_iter.isValid()) continue;

			const PrefabVersion& src_version = m_resources[prefab_handle];
			PrefabVersion& dst_version = dst.m_resources.insert(prefab_handle);
			dst_version.content_hash = src_version.content_hash;
			dst_version.resource = src_version.resource;
			dst_version.resource->incRefCount();
		}

		// clone entity to prefab map
		for (auto iter = map.begin(), end = map.end(); iter != end; ++iter) {
			const EntityPtr src_entity = iter.key();
			if (!src_entity.isValid()) continue;

			const EntityPtr dst_entity = iter.value();
			PrefabHandle prefab_handle = getPrefab(*src_entity);
			if (prefab_handle.getHashValue() == 0) continue;

			while(dst.m_entity_to_prefab.size() <= dst_entity.index) {
				dst.m_entity_to_prefab.push(PrefabHandle());
			}
			dst.m_entity_to_prefab[dst_entity.index] = prefab_handle;
		}
	}

private:
	struct DeferredInstance {
		PrefabResource* resource;
		Transform transform;
		EntityPtr parent;
		EntityFolders::FolderID folder;
	};

	struct PrefabVersion {
		StableHash content_hash;
		PrefabResource* resource;
		// used/valid only in serialize method
		u32 instance_count = 0;
	};

	Array<PrefabHandle> m_entity_to_prefab;
	HashMap<EntityRef, PrefabHandle> m_roots;
	HashMap<PrefabHandle, PrefabVersion> m_resources;
	Array<DeferredInstance> m_deferred_instances;
	World* m_world;
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