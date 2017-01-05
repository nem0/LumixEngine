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
#include "engine/math_utils.h"
#include "engine/matrix.h"
#include "engine/prefab.h"
#include "engine/property_register.h"
#include "engine/resource.h"
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
	AssetBrowserPlugin(WorldEditor& _editor, PrefabSystem& _system)
		: system(_system)
		, editor(_editor)
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
		InstantiatePrefabCommand(WorldEditor& _editor)
			: editor(_editor)
			, entities(_editor.getAllocator())
		{
		}


		~InstantiatePrefabCommand()
		{
			prefab->getResourceManager().unload(*prefab);
		}


		bool execute() override
		{
			entities.clear();
			if (!prefab->isReady()) return false;;
			auto& system = (PrefabSystemImpl&)editor.getPrefabSystem();

			system.instantiatePrefab(*prefab, position, rotation, scale, &entities);
			for (Entity entity : entities)
			{
				editor.createEntityGUID(entity);
			}
			return true;
		}


		void undo() override
		{
			Universe& universe = *editor.getUniverse();
			for (auto entity : entities)
			{
				universe.destroyEntity(entity);
				editor.destroyEntityGUID(entity);
			}
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


		bool merge(IEditorCommand& command) { return false; }

		PrefabResource* prefab;
		Vec3 position;
		Quat rotation;
		float scale;
		WorldEditor& editor;
		Array<Entity> entities;
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
		app.getAssetBrowser()->addPlugin(*LUMIX_NEW(m_editor.getAllocator(), AssetBrowserPlugin)(m_editor, *this));
	}
	

	static IEditorCommand* createInstantiatePrefabCommand(WorldEditor& editor)
	{
		return LUMIX_NEW(editor.getAllocator(), InstantiatePrefabCommand)(editor);
	}


	WorldEditor& getEditor() { return m_editor; }


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
		if (m_instances[p.prefab] == entity)
		{
			if (isValid(m_prefabs[entity.index].next))
				m_instances[p.prefab] = m_prefabs[entity.index].next;
			else
				m_instances.erase(p.prefab);
		}
		if (isValid(p.prev)) m_prefabs[p.prev.index].next = p.next;
		if (isValid(p.next)) m_prefabs[p.next.index].prev = p.prev;
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


	u64 getPrefab(Entity entity) override
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


	struct EntityGUIDMap : public IEntityGUIDMap
	{
		Entity get(EntityGUID guid) override { return{ (int)guid.value }; }


		EntityGUID get(Entity entity) override { return{ (u64)entity.index }; }


		void insert(EntityGUID guid, Entity entity) {}



	};


	void instantiatePrefab(PrefabResource& prefab,
		const Vec3& pos,
		const Quat& rot,
		float scale,
		Array<Entity>* entities)
	{
		if (!m_resources.find(prefab.getPath().getHash()).isValid())
		{
			m_resources.insert(prefab.getPath().getHash(), &prefab);
			prefab.getResourceManager().load(prefab);
		}
		InputBlob blob(prefab.blob.getData(), prefab.blob.getPos());
		EntityGUIDMap entity_map;
		TextDeserializer deserializer(blob, entity_map);
		while (blob.getPosition() < blob.getSize())
		{
			u64 prefab;
			deserializer.read(&prefab);
			Entity entity = m_universe->createEntity(pos, rot);
			if(entities) entities->push(entity);
			reserve(entity);
			m_prefabs[entity.index].prefab = prefab;
			link(entity, prefab);
			m_universe->setScale(entity, scale);
			u32 cmp_type;
			deserializer.read(&cmp_type);
			while (cmp_type != 0)
			{
				m_universe->deserializeComponent(
					deserializer, entity, PropertyRegister::getComponentTypeFromHash(cmp_type));
				deserializer.read(&cmp_type);
			}
		}
	}


	Array<Entity>* instantiatePrefab(PrefabResource& prefab, const Vec3& pos, const Quat& rot, float scale) override
	{
		InstantiatePrefabCommand* cmd = LUMIX_NEW(m_editor.getAllocator(), InstantiatePrefabCommand)(m_editor);
		cmd->position = pos;
		prefab.getResourceManager().load(prefab);
		cmd->prefab = &prefab;
		cmd->rotation = rot;
		cmd->scale = scale;
		cmd = (InstantiatePrefabCommand*)m_editor.executeCommand(cmd);
		return cmd ? &cmd->entities : nullptr;
	}


	static void serializePrefab(Universe* universe,
		const Entity* entities,
		int count,
		const Path& path,
		TextSerializer& serializer)
	{
		for (int i = 0; i < count; ++i)
		{
			Entity entity = entities[i];
			u64 prefab = path.getHash();
			prefab |= ((u64)i) << 32;
			serializer.write("prefab", prefab);
			for (ComponentUID cmp = universe->getFirstComponent(entity); cmp.isValid(); cmp = universe->getNextComponent(cmp))
			{
				const char* cmp_name = PropertyRegister::getComponentTypeID(cmp.type.index);
				u32 type_hash = PropertyRegister::getComponentTypeHash(cmp.type);
				serializer.write(cmp_name, type_hash);
				universe->serializeComponent(serializer, cmp.type, cmp.handle);
			}
			serializer.write("cmp_end", 0);
		}
	}


	void savePrefab(const Path& path) override
	{
		auto& entities = m_editor.getSelectedEntities();
		if (entities.empty()) return;

		FS::OsFile file;
		if (!file.open(path.c_str(), FS::Mode::CREATE_AND_WRITE, m_editor.getAllocator()))
		{
			g_log_error.log("Editor") << "Failed to create " << path.c_str();
			return;
		}

		OutputBlob blob(m_editor.getAllocator());
		EntityGUIDMap entity_map;
		TextSerializer serializer(blob, entity_map);

		auto& ents = m_editor.getSelectedEntities();
		serializePrefab(m_universe, &ents[0], ents.size(), path, serializer);

		file.write(blob.getData(), blob.getPos());

		file.close();
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
			serializer.write(res->getPath().c_str());
		}
	}


	void deserialize(InputBlob& serializer) override
	{
		int count;
		serializer.read(count);
		m_prefabs.resize(count);
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
			serializer.read(tmp, lengthOf(tmp));
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
			while(isValid(entity))
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
			instantiatePrefab(*m_resources[res_hash], pos, rot, scale, nullptr);
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