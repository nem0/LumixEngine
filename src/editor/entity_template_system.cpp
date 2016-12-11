#include "entity_template_system.h"
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
	AssetBrowserPlugin(WorldEditor& _editor, EntityTemplateSystem& _system)
		: system(_system)
		, editor(_editor)
	{}


	bool onGUI(Resource* resource, ResourceType type) override
	{
		if (type != PREFAB_TYPE) return false;

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


	EntityTemplateSystem& system;
	WorldEditor& editor;
};


class EntityTemplateSystemImpl LUMIX_FINAL : public EntityTemplateSystem
{
public:
	explicit EntityTemplateSystemImpl(WorldEditor& editor)
		: m_editor(editor)
		, m_universe(nullptr)
		, m_instances(editor.getAllocator())
		, m_resources(editor.getAllocator())
		, m_prefabs(editor.getAllocator())
	{
		editor.universeCreated().bind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onUniverseCreated>(this);
		editor.universeDestroyed().bind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onUniverseDestroyed>(this);
		setUniverse(editor.getUniverse());
	}


	void setStudioApp(StudioApp& app) override
	{
		app.getAssetBrowser()->addPlugin(*LUMIX_NEW(m_editor.getAllocator(), AssetBrowserPlugin)(m_editor, *this));
	}


	~EntityTemplateSystemImpl()
	{
		m_editor.universeCreated()
			.unbind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onUniverseCreated>(this);
		m_editor.universeDestroyed()
			.unbind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onUniverseDestroyed>(this);
		setUniverse(nullptr);
	}


	WorldEditor& getEditor() { return m_editor; }


	void setUniverse(Universe* universe)
	{
		if (m_universe)
		{
			m_universe->entityDestroyed()
				.unbind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onEntityDestroyed>(
					this);
		}
		m_universe = universe;
		if (m_universe)
		{
			m_universe->entityDestroyed()
				.bind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onEntityDestroyed>(this);
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


	Entity instantiatePrefab(PrefabResource& prefab, const Vec3& pos, const Quat& rot, float scale) override
	{
		if (!m_resources.find(prefab.getPath().getHash()).isValid())
		{
			m_resources.insert(prefab.getPath().getHash(), &prefab);
			prefab.getResourceManager().load(prefab);
		}
		InputBlob blob(prefab.blob.getData(), prefab.blob.getPos());
		TextDeserializer deserializer(blob);
		Entity entity = INVALID_ENTITY;
		while (blob.getPosition() < blob.getSize())
		{
			u64 prefab;
			deserializer.read(&prefab);
			entity = m_universe->createEntity(pos, rot);
			reserve(entity);
			m_prefabs[entity.index].prefab = prefab;
			link(entity, prefab);
			m_universe->setScale(entity, scale);
			u32 cmp_type;
			deserializer.read(&cmp_type);
			while (cmp_type != 0)
			{
				m_universe->deserializeComponent(deserializer, entity, PropertyRegister::getComponentTypeFromHash(cmp_type));
				deserializer.read(&cmp_type);
			}
		}
		return entity;
	}


	static void serializePrefab(Universe* universe, const Entity* entities, int count,  const Path& path, TextSerializer& serializer)
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
		TextSerializer serializer(blob);

		auto& ents = m_editor.getSelectedEntities();
		serializePrefab(m_universe, &ents[0], ents.size(), path, serializer);

		file.write(blob.getData(), blob.getPos());

		file.close();
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
			instantiatePrefab(*m_resources[res_hash], pos, rot, scale);
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
}; // class EntityTemplateSystemImpl


EntityTemplateSystem* EntityTemplateSystem::create(WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), EntityTemplateSystemImpl)(editor);
}


void EntityTemplateSystem::destroy(EntityTemplateSystem* system)
{
	LUMIX_DELETE(
		static_cast<EntityTemplateSystemImpl*>(system)->getEditor().getAllocator(), system);
}


} // namespace Lumix