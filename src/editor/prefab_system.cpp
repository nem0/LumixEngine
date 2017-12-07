#include "prefab_system.h"
#include "editor/asset_browser.h"
#include "editor/ieditor_command.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/array.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/fs/os_file.h"
#include "engine/hash_map.h"
#include "engine/iplugin.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/matrix.h"
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


static const ResourceType PREFAB_TYPE("prefab");


class AssetBrowserPlugin LUMIX_FINAL : public AssetBrowser::IPlugin
{
public:
	AssetBrowserPlugin(WorldEditor& editor, PrefabSystem& system)
		: system(system)
		, editor(editor)
	{}


	bool onGUI(Resource* resource, ResourceType type) override
	{
		if (type != PREFAB_TYPE) return false;

		if (ImGui::Button("instantiate"))
		{
			Array<Entity> entities(editor.getAllocator());
			system.instantiatePrefab(*(PrefabResource*)resource, editor.getCameraRaycastHit(), {0, 0, 0, 1}, 1);
		}

		return true;
	}


	ResourceType getResourceType(const char* ext) override
	{
		if (equalStrings(ext, "fab")) return PREFAB_TYPE;
		return INVALID_RESOURCE_TYPE;
	}
	
	
	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Prefab"; }
	bool hasResourceManager(ResourceType type) const override { return type == PREFAB_TYPE; }


	bool acceptExtension(const char* ext, ResourceType type) const override
	{
		return type == PREFAB_TYPE && equalStrings(ext, "fab");
	}


	PrefabSystem& system;
	WorldEditor& editor;
};


class PrefabSystemImpl LUMIX_FINAL : public PrefabSystem
{
	struct InstantiatePrefabCommand LUMIX_FINAL : public IEditorCommand
	{
		InstantiatePrefabCommand(WorldEditor& editor)
			: editor(editor)
		{
		}


		~InstantiatePrefabCommand()
		{
			prefab->getResourceManager().unload(*prefab);
		}


		void createEntityGUIDRecursive(Entity entity) const
		{
			if (!entity.isValid()) return;

			editor.createEntityGUID(entity);
			
			Universe& universe = *editor.getUniverse();
			createEntityGUIDRecursive(universe.getFirstChild(entity));
			createEntityGUIDRecursive(universe.getNextSibling(entity));
		}


		void destroyEntityRecursive(Entity entity) const
		{
			if (!entity.isValid()) return;

			Universe& universe = *editor.getUniverse();
			destroyEntityRecursive(universe.getFirstChild(entity));
			destroyEntityRecursive(universe.getNextSibling(entity));

			universe.destroyEntity(entity);
			editor.destroyEntityGUID(entity);

		}


		bool execute() override
		{
			entity = INVALID_ENTITY;
			if (!prefab->isReady()) return false;
			auto& system = (PrefabSystemImpl&)editor.getPrefabSystem();

			entity = system.doInstantiatePrefab(*prefab, position, rotation, scale);
			editor.createEntityGUID(entity);
			createEntityGUIDRecursive(editor.getUniverse()->getFirstChild(entity));
			return true;
		}


		void undo() override
		{
			Universe& universe = *editor.getUniverse();

			destroyEntityRecursive(universe.getFirstChild(entity));
			universe.destroyEntity(entity);
			editor.destroyEntityGUID(entity);
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("position_x", position.x);
			serializer.serialize("position_y", position.y);
			serializer.serialize("position_z", position.z);
			serializer.serialize("rotation_x", rotation.x);
			serializer.serialize("rotation_y", rotation.y);
			serializer.serialize("rotation_z", rotation.z);
			serializer.serialize("rotation_w", rotation.w);
			serializer.serialize("scale", scale);
			serializer.serialize("path", prefab->getPath().c_str());
		}


		void deserialize(JsonSerializer& serializer) override
		{
			serializer.deserialize("position_x", position.x, 0);
			serializer.deserialize("position_y", position.y, 0);
			serializer.deserialize("position_z", position.z, 0);
			serializer.deserialize("rotation_x", rotation.x, 0);
			serializer.deserialize("rotation_y", rotation.y, 0);
			serializer.deserialize("rotation_z", rotation.z, 0);
			serializer.deserialize("rotation_w", rotation.w, 0);
			serializer.deserialize("scale", scale, 0);
			char path[MAX_PATH_LENGTH];
			serializer.deserialize("path_hash", path, lengthOf(path), "");
			prefab = (PrefabResource*)editor.getEngine().getResourceManager().get(PREFAB_TYPE)->load(Path(path));
		}


		const char* getType() override
		{
			return "instantiate_prefab";
		}


		bool merge(IEditorCommand& command) override { return false; }

		PrefabResource* prefab;
		Vec3 position;
		Quat rotation;
		float scale;
		WorldEditor& editor;
		Entity entity;
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


	void setStudioApp(StudioApp& app) override
	{
		app.getAssetBrowser().addPlugin(*LUMIX_NEW(m_editor.getAllocator(), AssetBrowserPlugin)(m_editor, *this));
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


	void link(Entity entity, u64 prefab)
	{
		ASSERT(prefab != 0);
		int idx = m_instances.find(prefab);
		m_prefabs[entity.index].prev = INVALID_ENTITY;
		if (idx >= 0)
		{
			Entity e = m_instances.at(idx);
			m_prefabs[e.index].prev = entity;
			m_prefabs[entity.index].next = e;
		}
		else
		{
			m_prefabs[entity.index].next = INVALID_ENTITY;
		}
		m_instances[prefab] = entity;
	}


	void unlink(Entity entity)
	{
		EntityPrefab& p = m_prefabs[entity.index];
		if (p.prefab == 0) return;
		if (m_instances[p.prefab] == entity)
		{
			if (m_prefabs[entity.index].next.isValid())
				m_instances[p.prefab] = m_prefabs[entity.index].next;
			else
				m_instances.erase(p.prefab);
		}
		if (p.prev.isValid()) m_prefabs[p.prev.index].next = p.next;
		if (p.next.isValid()) m_prefabs[p.next.index].prev = p.prev;
	}


	void onEntityDestroyed(Entity entity)
	{
		if (entity.index >= m_prefabs.size()) return;
		unlink(entity);
		m_prefabs[entity.index].prefab = 0;
	}


	void setPrefab(Entity entity, u64 prefab) override
	{
		reserve(entity);
		m_prefabs[entity.index].prefab = prefab;
		link(entity, prefab);
	}


	PrefabResource* getPrefabResource(Entity entity) override
	{
		if (entity.index >= m_prefabs.size()) return nullptr;
		u32 hash = u32(m_prefabs[entity.index].prefab & 0xffffFFFF);
		auto iter = m_resources.find(hash);
		if (!iter.isValid()) return nullptr;
		return iter.value();
	}


	u64 getPrefab(Entity entity) const override
	{
		if (entity.index >= m_prefabs.size()) return 0;
		return m_prefabs[entity.index].prefab;
	}


	int getMaxEntityIndex() const override
	{
		return m_prefabs.size();
	}


	Entity getFirstInstance(u64 prefab) override
	{
		int instances_index = m_instances.find(prefab);
		if (instances_index >= 0) return m_instances.at(instances_index);
		return INVALID_ENTITY;
	}


	Entity getNextInstance(Entity entity) override
	{
		return m_prefabs[entity.index].next;
	}


	void reserve(Entity entity)
	{
		while (entity.index >= m_prefabs.size())
		{
			auto& i = m_prefabs.emplace();
			i.prefab = 0;
		}
	}


	struct LoadEntityGUIDMap : public ILoadEntityGUIDMap
	{
		explicit LoadEntityGUIDMap(const Array<Entity>& entities)
			: entities(entities)
		{
		}


		Entity get(EntityGUID guid) override
		{
			if (guid.value >= entities.size()) return INVALID_ENTITY;
			return entities[(int)guid.value];
		}


		const Array<Entity>& entities;
	};


	struct SaveEntityGUIDMap : public ISaveEntityGUIDMap
	{
		explicit SaveEntityGUIDMap(const Array<Entity>& entities)
			: entities(entities)
		{
		}


		EntityGUID get(Entity entity) override
		{
			int idx = entities.indexOf(entity);
			if (idx < 0) return INVALID_ENTITY_GUID;
			return {(u64)idx};
		}


		const Array<Entity>& entities;
	};


	Entity doInstantiatePrefab(PrefabResource& prefab_res, const Vec3& pos, const Quat& rot, float scale)
	{
		if (!prefab_res.isReady()) return INVALID_ENTITY;
		if (!m_resources.find(prefab_res.getPath().getHash()).isValid())
		{
			m_resources.insert(prefab_res.getPath().getHash(), &prefab_res);
			prefab_res.getResourceManager().load(prefab_res);
		}
		InputBlob blob(prefab_res.blob.getData(), prefab_res.blob.getPos());
		Array<Entity> entities(m_editor.getAllocator());
		LoadEntityGUIDMap entity_map(entities);
		TextDeserializer deserializer(blob, entity_map);
		u32 version;
		deserializer.read(&version);
		if (version > (int)PrefabVersion::LAST)
		{
			g_log_error.log("Editor") << "Prefab " << prefab_res.getPath() << " has unsupported version.";
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
		while (blob.getPosition() < blob.getSize() && entity_idx < count)
		{
			u64 prefab;
			deserializer.read(&prefab);
			Entity entity = entities[entity_idx];
			m_universe->setTransform(entity, {pos, rot, scale});
			reserve(entity);
			m_prefabs[entity.index].prefab = prefab;
			link(entity, prefab);
			
			if (version > (int)PrefabVersion::WITH_HIERARCHY)
			{
				Entity parent;
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


	Entity instantiatePrefab(PrefabResource& prefab, const Vec3& pos, const Quat& rot, float scale) override
	{
		InstantiatePrefabCommand* cmd = LUMIX_NEW(m_editor.getAllocator(), InstantiatePrefabCommand)(m_editor);
		cmd->position = pos;
		prefab.getResourceManager().load(prefab);
		cmd->prefab = &prefab;
		cmd->rotation = rot;
		cmd->scale = scale;
		m_editor.executeCommand(cmd);
		return cmd ? cmd->entity : INVALID_ENTITY;
	}


	static int countHierarchy(Universe* universe, Entity entity)
	{
		if (!entity.isValid()) return 0;
		int children_count = countHierarchy(universe, universe->getFirstChild(entity));
		int siblings_count = countHierarchy(universe, universe->getNextSibling(entity));
		return 1 + children_count + siblings_count;
	}


	static void serializePrefabEntity(u64 prefab,
		int& index,
		TextSerializer& serializer,
		Universe* universe,
		Entity entity,
		bool is_root)
	{
		if (!entity.isValid()) return;

		prefab |= ((u64)index) << 32;
		++index;
		serializer.write("prefab", prefab);
		Entity parent = is_root ? INVALID_ENTITY : universe->getParent(entity);
		serializer.write("parent", parent);
		if (parent.isValid())
		{
			serializer.write("local_transform", universe->getLocalTransform(entity).getRigidPart());
			serializer.write("local_scale", universe->getLocalScale(entity));
		}
		for (ComponentUID cmp = universe->getFirstComponent(entity); cmp.isValid();
			cmp = universe->getNextComponent(cmp))
		{
			const char* cmp_name = Reflection::getComponentTypeID(cmp.type.index);
			u32 type_hash = Reflection::getComponentTypeHash(cmp.type);
			serializer.write(cmp_name, type_hash);
			int scene_version = universe->getScene(cmp.type)->getVersion();
			serializer.write("scene_version", scene_version);
			universe->serializeComponent(serializer, cmp.type, cmp.handle);
		}
		serializer.write("cmp_end", 0);

		serializePrefabEntity(prefab, index, serializer, universe, universe->getFirstChild(entity), false);
		if (!is_root)
		{
			serializePrefabEntity(prefab, index, serializer, universe, universe->getNextSibling(entity), false);
		}
	}



	static void serializePrefab(Universe* universe,
		Entity root,
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


	Entity getPrefabRoot(Entity entity) const
	{
		Entity root = entity;
		Entity parent = m_universe->getParent(root);
		while (parent.isValid() && getPrefab(parent) != 0)
		{
			root = parent;
			parent = m_universe->getParent(root);
		}
		return root;
	}


	void gatherHierarchy(Entity entity, bool is_root, Array<Entity>& out) const
	{
		if (!entity.isValid()) return;

		out.push(entity);
		gatherHierarchy(m_universe->getFirstChild(entity), false, out);
		gatherHierarchy(m_universe->getNextSibling(entity), false, out);
	}


	void savePrefab(const Path& path) override
	{
		auto& selected_entities = m_editor.getSelectedEntities();
		if (selected_entities.size() != 1) return;

		Entity entity = selected_entities[0];
		u64 prefab = getPrefab(entity);
		if (prefab != 0) entity = getPrefabRoot(entity);

		FS::OsFile file;
		if (!file.open(path.c_str(), FS::Mode::CREATE_AND_WRITE))
		{
			g_log_error.log("Editor") << "Failed to create " << path.c_str();
			return;
		}

		Array<Entity> entities(m_editor.getAllocator());
		gatherHierarchy(entity, true, entities);
		OutputBlob blob(m_editor.getAllocator());
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
			auto* resource_manager = m_editor.getEngine().getResourceManager().get(PREFAB_TYPE);
			auto* res = (PrefabResource*)resource_manager->load(path);
			FS::FileSystem& fs = m_editor.getEngine().getFileSystem();
			while (fs.hasWork()) fs.updateAsyncTransactions();
			instantiatePrefab(*res, tr.pos, tr.rot, tr.scale);

			m_editor.endCommandGroup();
		}
	}


	void serialize(OutputBlob& serializer) override
	{
		serializer.write(m_prefabs.size());
		if(!m_prefabs.empty()) serializer.write(&m_prefabs[0], m_prefabs.size() * sizeof(m_prefabs[0]));
		serializer.write(m_instances.size());
		for (int i = 0, c = m_instances.size(); i < c; ++i)
		{
			serializer.write(m_instances.getKey(i));
			serializer.write(m_instances.at(i));
		}
		serializer.write(m_resources.size());
		for (PrefabResource* res : m_resources)
		{
			serializer.writeString(res->getPath().c_str());
		}
	}


	void deserialize(InputBlob& serializer) override
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
			Entity value;
			serializer.read(key);
			serializer.read(value);
			m_instances.insert(key, value);
		}
		serializer.read(count);
		auto* resource_manager = m_editor.getEngine().getResourceManager().get(PREFAB_TYPE);
		for (int i = 0; i < count; ++i)
		{
			char tmp[MAX_PATH_LENGTH];
			serializer.readString(tmp, lengthOf(tmp));
			auto* res = (PrefabResource*)resource_manager->load(Path(tmp));
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

		for (int i = 0; i < m_instances.size(); ++i)
		{
			u64 prefab = m_instances.getKey(i);
			if ((prefab & 0xffffFFFF) != prefab) continue;
			Entity entity = m_instances.at(i);
			while(entity.isValid())
			{
				serializer.write("prefab", (u32)prefab);
				serializer.write("pos", m_universe->getPosition(entity));
				serializer.write("rot", m_universe->getRotation(entity));
				serializer.write("scale", m_universe->getScale(entity));
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
		
		auto* mng = m_editor.getEngine().getResourceManager().get(PREFAB_TYPE);
		for (;;)
		{
			char tmp[MAX_PATH_LENGTH];
			serializer.read(tmp, lengthOf(tmp));
			if (tmp[0] == 0) break;
			auto* res = (PrefabResource*)mng->load(Path(tmp));
			m_resources.insert(res->getPath().getHash(), res);
		}

		while(m_editor.getEngine().getFileSystem().hasWork())
			m_editor.getEngine().getFileSystem().updateAsyncTransactions();

		for (;;)
		{
			u32 res_hash;
			serializer.read(&res_hash);
			if (res_hash == 0) break;
			
			Vec3 pos;
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
		Entity next;
		Entity prev;
	};
	Array<EntityPrefab> m_prefabs;
	AssociativeArray<u64, Entity> m_instances;
	HashMap<u32, PrefabResource*> m_resources;
	Universe* m_universe;
	WorldEditor& m_editor;
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


} // namespace Lumix