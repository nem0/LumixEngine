#include "prefab_system.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/ieditor_command.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/array.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/hash_map.h"
#include "engine/iplugin.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/prefab.h"
#include "engine/reflection.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/serializer.h"
#include "engine/string.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include <cstdlib>

namespace Lumix
{


class AssetBrowserPlugin final : public AssetBrowser::IPlugin, public AssetCompiler::IPlugin
{
public:
	AssetBrowserPlugin(StudioApp& app, PrefabSystem& system)
		: system(system)
		, editor(app.getWorldEditor())
		, app(app)
	{
		app.getAssetCompiler().registerExtension("fab", PrefabResource::TYPE);
	}


	void onGUI(Resource* resource) override
	{
		if (ImGui::Button("instantiate"))
		{
			Array<EntityRef> entities(editor.getAllocator());
			system.instantiatePrefab(*(PrefabResource*)resource, editor.getCameraRaycastHit(), {0, 0, 0, 1}, 1);
		}
	}
	
	
	bool compile(const Path& src) override
	{
		const char* dst_dir = app.getAssetCompiler().getCompiledDir();
		const u32 hash = crc32(src.c_str());

		const StaticString<MAX_PATH_LENGTH> dst(dst_dir, hash, ".res");

		FileSystem& fs = app.getWorldEditor().getEngine().getFileSystem();
		return fs.copyFile(src.c_str(), dst);
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Prefab"; }
	ResourceType getResourceType() const override { return PrefabResource::TYPE; }


	PrefabSystem& system;
	WorldEditor& editor;
	StudioApp& app;
};


class PrefabSystemImpl final : public PrefabSystem
{
	struct InstantiatePrefabCommand final : public IEditorCommand
	{
		InstantiatePrefabCommand(WorldEditor& editor)
			: editor(editor)
		{
		}


		~InstantiatePrefabCommand()
		{
			prefab->getResourceManager().unload(*prefab);
		}


		void createEntityGUIDRecursive(EntityPtr entity) const
		{
			if (!entity.isValid()) return;

			EntityRef e = (EntityRef)entity;
			editor.createEntityGUID(e);
			
			Universe& universe = *editor.getUniverse();
			createEntityGUIDRecursive(universe.getFirstChild(e));
			createEntityGUIDRecursive(universe.getNextSibling(e));
		}


		void destroyEntityRecursive(EntityPtr entity) const
		{
			if (!entity.isValid()) return;
			
			EntityRef e = (EntityRef)entity;
			Universe& universe = *editor.getUniverse();
			destroyEntityRecursive(universe.getFirstChild(e));
			destroyEntityRecursive(universe.getNextSibling(e));

			universe.destroyEntity(e);
			editor.destroyEntityGUID(e);

		}


		bool execute() override
		{
			entity = INVALID_ENTITY;
			if (!prefab->isReady()) return false;
			auto& system = (PrefabSystemImpl&)editor.getPrefabSystem();

			entity = system.doInstantiatePrefab(*prefab, position, rotation, scale);
			if (entity.isValid()) {
				editor.createEntityGUID((EntityRef)entity);
				createEntityGUIDRecursive(editor.getUniverse()->getFirstChild((EntityRef)entity));
				return true;
			}
			return false;
		}


		void undo() override
		{
			if (!entity.isValid()) return;

			Universe& universe = *editor.getUniverse();

			EntityRef e = (EntityRef)entity;
			destroyEntityRecursive(universe.getFirstChild(e));
			universe.destroyEntity(e);
			editor.destroyEntityGUID(e);
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
		EntityPtr entity;
	};

public:
	explicit PrefabSystemImpl(WorldEditor& editor)
		: m_editor(editor)
		, m_universe(nullptr)
		, m_instances(editor.getAllocator())
		, m_resources(editor.getAllocator())
		, m_prefabs(editor.getAllocator())
	{
		editor.universeCreated().bind<PrefabSystemImpl, &PrefabSystemImpl::onUniverseCreated>(this);
		editor.universeDestroyed().bind<PrefabSystemImpl, &PrefabSystemImpl::onUniverseDestroyed>(this);
		setUniverse(editor.getUniverse());
		editor.registerEditorCommandCreator(
			"instantiate_prefab", &PrefabSystemImpl::createInstantiatePrefabCommand);
	}


	~PrefabSystemImpl()
	{
		m_editor.universeCreated()
			.unbind<PrefabSystemImpl, &PrefabSystemImpl::onUniverseCreated>(this);
		m_editor.universeDestroyed()
			.unbind<PrefabSystemImpl, &PrefabSystemImpl::onUniverseDestroyed>(this);
		setUniverse(nullptr);
	}


	static IEditorCommand* createInstantiatePrefabCommand(WorldEditor& editor)
	{
		return LUMIX_NEW(editor.getAllocator(), InstantiatePrefabCommand)(editor);
	}


	WorldEditor& getEditor() const { return m_editor; }


	void setUniverse(Universe* universe)
	{
		if (m_universe)
		{
			m_universe->entityDestroyed()
				.unbind<PrefabSystemImpl, &PrefabSystemImpl::onEntityDestroyed>(
					this);
		}
		m_universe = universe;
		if (m_universe)
		{
			m_universe->entityDestroyed()
				.bind<PrefabSystemImpl, &PrefabSystemImpl::onEntityDestroyed>(this);
		}
	}


	void onUniverseCreated()
	{
		m_instances.clear();
		for (PrefabResource* prefab : m_resources)
		{
			prefab->getResourceManager().unload(*prefab);
		}
		m_resources.clear();
		m_prefabs.clear();
		setUniverse(m_editor.getUniverse());
	}


	void onUniverseDestroyed()
	{
		m_instances.clear();
		for (PrefabResource* prefab : m_resources)
		{
			prefab->getResourceManager().unload(*prefab);
		}
		m_resources.clear();
		m_prefabs.clear();
		setUniverse(nullptr);
	}


	void link(EntityRef entity, u64 prefab)
	{
		ASSERT(prefab != 0);
		auto iter = m_instances.find(prefab);
		m_prefabs[entity.index].prev = INVALID_ENTITY;
		if (iter.isValid())
		{
			EntityRef e = iter.value();
			m_prefabs[e.index].prev = entity;
			m_prefabs[entity.index].next = e;
			m_instances[prefab] = entity;
		}
		else
		{
			m_prefabs[entity.index].next = INVALID_ENTITY;
			m_instances.insert(prefab, entity);
		}
	}


	void unlink(EntityRef entity)
	{
		EntityPrefab& p = m_prefabs[entity.index];
		if (p.prefab == 0) return;
		if (m_instances[p.prefab] == entity)
		{
			if (m_prefabs[entity.index].next.isValid())
				m_instances[p.prefab] = (EntityRef)m_prefabs[entity.index].next;
			else
				m_instances.erase(p.prefab);
		}
		if (p.prev.isValid()) m_prefabs[p.prev.index].next = p.next;
		if (p.next.isValid()) m_prefabs[p.next.index].prev = p.prev;
	}


	void onEntityDestroyed(EntityRef entity)
	{
		if (entity.index >= m_prefabs.size()) return;
		unlink(entity);
		m_prefabs[entity.index].prefab = 0;
	}


	void setPrefab(EntityRef entity, u64 prefab) override
	{
		reserve(entity);
		m_prefabs[entity.index].prefab = prefab;
		link(entity, prefab);
	}


	PrefabResource* getPrefabResource(EntityRef entity) override
	{
		if (entity.index >= m_prefabs.size()) return nullptr;
		u32 hash = u32(m_prefabs[entity.index].prefab & 0xffffFFFF);
		auto iter = m_resources.find(hash);
		if (!iter.isValid()) return nullptr;
		return iter.value();
	}


	u64 getPrefab(EntityRef entity) const override
	{
		if (entity.index >= m_prefabs.size()) return 0;
		return m_prefabs[entity.index].prefab;
	}


	int getMaxEntityIndex() const override
	{
		return m_prefabs.size();
	}


	EntityPtr getFirstInstance(u64 prefab) override
	{
		auto iter = m_instances.find(prefab);
		if (iter.isValid()) return iter.value();
		return INVALID_ENTITY;
	}


	EntityPtr getNextInstance(EntityRef entity) override
	{
		return m_prefabs[entity.index].next;
	}


	void reserve(EntityRef entity)
	{
		while (entity.index >= m_prefabs.size())
		{
			auto& i = m_prefabs.emplace();
			i.prefab = 0;
		}
	}


	struct LoadEntityGUIDMap : public ILoadEntityGUIDMap
	{
		explicit LoadEntityGUIDMap(const Array<EntityRef>& entities)
			: entities(entities)
		{
		}


		EntityPtr get(EntityGUID guid) override
		{
			if (guid.value >= entities.size()) return INVALID_ENTITY;
			return entities[(int)guid.value];
		}


		const Array<EntityRef>& entities;
	};


	struct SaveEntityGUIDMap : public ISaveEntityGUIDMap
	{
		explicit SaveEntityGUIDMap(const Array<EntityRef>& entities)
			: entities(entities)
		{
		}


		EntityGUID get(EntityPtr entity) override
		{
			if (!entity.isValid()) return INVALID_ENTITY_GUID;
			int idx = entities.indexOf((EntityRef)entity);
			if (idx < 0) return INVALID_ENTITY_GUID;
			return {(u64)idx};
		}


		const Array<EntityRef>& entities;
	};


	EntityPtr doInstantiatePrefab(PrefabResource& prefab_res, const DVec3& pos, const Quat& rot, float scale)
	{
		if (!prefab_res.isReady()) return INVALID_ENTITY;
		if (!m_resources.find(prefab_res.getPath().getHash()).isValid())
		{
			m_resources.insert(prefab_res.getPath().getHash(), &prefab_res);
			prefab_res.getResourceManager().load(prefab_res);
		}
		InputMemoryStream blob(prefab_res.data.begin(), prefab_res.data.byte_size());
		Array<EntityRef> entities(m_editor.getAllocator());
		LoadEntityGUIDMap entity_map(entities);
		TextDeserializer deserializer(blob, entity_map);
		u32 version;
		deserializer.read(&version);
		if (version > (int)PrefabVersion::LAST)
		{
			logError("Editor") << "Prefab " << prefab_res.getPath() << " has unsupported version.";
			return INVALID_ENTITY;
		}
		int count;
		deserializer.read(&count);
		entities.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			entities.push(m_universe->createEntity({0, 0, 0}, {0, 0, 0, 1}));
		}

		int entity_idx = 0;
		while (blob.getPosition() < blob.size() && entity_idx < count)
		{
			u64 prefab;
			deserializer.read(&prefab);
			EntityRef entity = entities[entity_idx];
			m_universe->setTransform(entity, {pos, rot, scale});
			reserve(entity);
			m_prefabs[entity.index].prefab = prefab;
			link(entity, prefab);
			
			if (version > (int)PrefabVersion::WITH_HIERARCHY)
			{
				EntityPtr parent;
				deserializer.read(&parent);
				if (parent.isValid())
				{
					RigidTransform local_tr;
					deserializer.read(&local_tr);
					float scale;
					deserializer.read(&scale);
					m_universe->setParent(parent, entity);
					m_universe->setLocalTransform(entity, {local_tr.pos, local_tr.rot, scale});
				}
			}
			u32 cmp_type_hash;
			deserializer.read(&cmp_type_hash);
			while (cmp_type_hash != 0)
			{
				ComponentType cmp_type = Reflection::getComponentTypeFromHash(cmp_type_hash);
				int scene_version;
				deserializer.read(&scene_version);
				m_universe->deserializeComponent(deserializer, entity, cmp_type, scene_version);
				deserializer.read(&cmp_type_hash);
			}
			++entity_idx;
		}
		return entities[0];
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


	static int countHierarchy(Universe* universe, EntityPtr entity)
	{
		if (!entity.isValid()) return 0;
		int children_count = countHierarchy(universe, universe->getFirstChild((EntityRef)entity));
		int siblings_count = countHierarchy(universe, universe->getNextSibling((EntityRef)entity));
		return 1 + children_count + siblings_count;
	}


	static void serializePrefabEntity(u64 prefab,
		int& index,
		TextSerializer& serializer,
		Universe* universe,
		EntityPtr entity,
		bool is_root)
	{
		if (!entity.isValid()) return;

		const EntityRef entity_ref = (EntityRef)entity;
		prefab |= ((u64)index) << 32;
		++index;
		serializer.write("prefab", prefab);
		EntityPtr parent = is_root ? INVALID_ENTITY : universe->getParent(entity_ref);
		serializer.write("parent", parent);
		if (parent.isValid())
		{
			serializer.write("local_transform", universe->getLocalTransform(entity_ref).getRigidPart());
			serializer.write("local_scale", universe->getLocalScale(entity_ref));
		}
		for (ComponentUID cmp = universe->getFirstComponent(entity_ref); cmp.isValid();
			cmp = universe->getNextComponent(cmp))
		{
			const char* cmp_name = Reflection::getComponentTypeID(cmp.type.index);
			u32 type_hash = Reflection::getComponentTypeHash(cmp.type);
			serializer.write(cmp_name, type_hash);
			int scene_version = universe->getScene(cmp.type)->getVersion();
			serializer.write("scene_version", scene_version);
			ASSERT(cmp.entity.isValid());
			universe->serializeComponent(serializer, cmp.type, (EntityRef)cmp.entity);
		}
		serializer.write("cmp_end", 0);

		serializePrefabEntity(prefab, index, serializer, universe, universe->getFirstChild(entity_ref), false);
		if (!is_root)
		{
			serializePrefabEntity(prefab, index, serializer, universe, universe->getNextSibling(entity_ref), false);
		}
	}



	static void serializePrefab(Universe* universe,
		EntityRef root,
		const Path& path,
		TextSerializer& serializer)
	{
		serializer.write("version", (u32)PrefabVersion::LAST);
		int count = 1 + countHierarchy(universe, universe->getFirstChild(root));
		serializer.write("entity_count", count);
		int i = 0;
		u64 prefab = path.getHash();
		serializePrefabEntity(prefab, i, serializer, universe, root, true);
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


	void gatherHierarchy(EntityPtr entity, bool is_root, Array<EntityRef>& out) const
	{
		if (!entity.isValid()) return;

		const EntityRef e = (EntityRef)entity;
		out.push(e);
		gatherHierarchy(m_universe->getFirstChild(e), false, out);
		gatherHierarchy(m_universe->getNextSibling(e), false, out);
	}


	void savePrefab(const Path& path) override
	{
		auto& selected_entities = m_editor.getSelectedEntities();
		if (selected_entities.size() != 1) return;

		EntityRef entity = selected_entities[0];
		u64 prefab = getPrefab(entity);
		if (prefab != 0) entity = getPrefabRoot(entity);

		FileSystem& fs = m_editor.getEngine().getFileSystem();
		OS::OutputFile file;
		if (!fs.open(path.c_str(), &file))
		{
			logError("Editor") << "Failed to create " << path.c_str();
			return;
		}

		Array<EntityRef> entities(m_editor.getAllocator());
		gatherHierarchy(entity, true, entities);
		OutputMemoryStream blob(m_editor.getAllocator());
		SaveEntityGUIDMap entity_map(entities);
		TextSerializer serializer(blob, entity_map);

		serializePrefab(m_universe, entities[0], path, serializer);

		file.write(blob.getData(), blob.getPos());

		file.close();

		if (prefab == 0)
		{
			m_editor.beginCommandGroup(crc32("save_prefab"));

			Transform tr = m_universe->getTransform(entity);
			m_editor.destroyEntities(&entities[0], entities.size());
			auto* res = m_editor.getEngine().getResourceManager().load<PrefabResource>(path);
			FileSystem& fs = m_editor.getEngine().getFileSystem();
			while (fs.hasWork()) fs.updateAsyncTransactions();
			instantiatePrefab(*res, tr.pos, tr.rot, tr.scale);

			m_editor.endCommandGroup();
		}
	}


	void serialize(IOutputStream& serializer) override
	{
		serializer.write(m_prefabs.size());
		if(!m_prefabs.empty()) serializer.write(&m_prefabs[0], m_prefabs.size() * sizeof(m_prefabs[0]));
		serializer.write(m_instances.size());
		for (auto iter = m_instances.begin(), end = m_instances.end(); iter != end; ++iter)
		{
			serializer.write(iter.key());
			serializer.write(iter.value());
		}
		serializer.write(m_resources.size());
		for (PrefabResource* res : m_resources)
		{
			serializer.writeString(res->getPath().c_str());
		}
	}


	void deserialize(IInputStream& serializer) override
	{
		int count;
		serializer.read(count);
		m_prefabs.resize(count);
		if (count > 0)
			serializer.read(&m_prefabs[0], m_prefabs.size() * sizeof(m_prefabs[0]));
		serializer.read(count);
		for (int i = 0; i < count; ++i)
		{
			u64 key;
			EntityRef value;
			serializer.read(key);
			serializer.read(value);
			m_instances.insert(key, value);
		}
		serializer.read(count);
		ResourceManagerHub& resource_manager = m_editor.getEngine().getResourceManager();
		for (int i = 0; i < count; ++i)
		{
			char tmp[MAX_PATH_LENGTH];
			serializer.readString(tmp, lengthOf(tmp));
			auto* res = resource_manager.load<PrefabResource>(Path(tmp));
			m_resources.insert(res->getPath().getHash(), res);
		}
	}

	
	void serialize(ISerializer& serializer) override
	{
		serializer.write("count", m_prefabs.size());

		for (PrefabResource* res : m_resources)
		{
			serializer.write("resource", res->getPath().c_str());
		}
		serializer.write("resource", "");

		for (auto iter = m_instances.begin(), end = m_instances.end(); iter != end; ++iter)
		{
			u64 prefab = iter.key();
			if ((prefab & 0xffffFFFF) != prefab) continue;
			EntityPtr entity = iter.value();
			while(entity.isValid())
			{
				const EntityRef e = (EntityRef)entity;
				serializer.write("prefab", (u32)prefab);
				serializer.write("pos", m_universe->getPosition(e));
				serializer.write("rot", m_universe->getRotation(e));
				serializer.write("scale", m_universe->getScale(e));
				entity = m_prefabs[entity.index].next;
			}
		}
		serializer.write("prefab", (u32)0);
	}


	void deserialize(IDeserializer& serializer) override
	{
		int count;
		serializer.read(&count);
		reserve({count-1});
		
		auto& mng = m_editor.getEngine().getResourceManager();
		for (;;)
		{
			char tmp[MAX_PATH_LENGTH];
			serializer.read(tmp, lengthOf(tmp));
			if (tmp[0] == 0) break;
			auto* res = mng.load<PrefabResource>(Path(tmp));
			m_resources.insert(res->getPath().getHash(), res);
		}

		while(m_editor.getEngine().getFileSystem().hasWork())
			m_editor.getEngine().getFileSystem().updateAsyncTransactions();

		for (;;)
		{
			u32 res_hash;
			serializer.read(&res_hash);
			if (res_hash == 0) break;
			
			DVec3 pos;
			serializer.read(&pos);
			Quat rot;
			serializer.read(&rot);
			float scale;
			serializer.read(&scale);
			doInstantiatePrefab(*m_resources[res_hash], pos, rot, scale);
		}
	}


private:
	struct EntityPrefab
	{
		u64 prefab;
		EntityPtr next;
		EntityPtr prev;
	};
	Array<EntityPrefab> m_prefabs;
	HashMap<u64, EntityRef> m_instances;
	HashMap<u32, PrefabResource*> m_resources;
	Universe* m_universe;
	WorldEditor& m_editor;
	StudioApp* m_app;
}; // class PrefabSystemImpl


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
	ab_plugin = LUMIX_NEW(app.getWorldEditor().getAllocator(), AssetBrowserPlugin)(app, system);
	app.getAssetBrowser().addPlugin(*ab_plugin);
	const char* extensions[] = { "fab", nullptr };
	app.getAssetCompiler().addPlugin(*ab_plugin, extensions);
}


void PrefabSystem::destroyEditorPlugins(StudioApp& app)
{
	app.getAssetBrowser().removePlugin(*ab_plugin);
	app.getAssetCompiler().removePlugin(*ab_plugin);
	LUMIX_DELETE(app.getWorldEditor().getAllocator(), ab_plugin);
	ab_plugin = nullptr;
}


} // namespace Lumix