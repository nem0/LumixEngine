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


struct PrefabInstance
{
	Vec3 position;
	u32 path_hash;
};


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
		, m_updated(editor.getAllocator())
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
		for (PrefabResource* prefab : m_prefabs)
		{
			prefab->getResourceManager().unload(*prefab);
		}
		m_prefabs.clear();
		setUniverse(m_editor.getUniverse());
	}


	void onUniverseDestroyed()
	{
		m_instances.clear();
		for (PrefabResource* prefab : m_prefabs)
		{
			prefab->getResourceManager().unload(*prefab);
		}
		m_prefabs.clear();
		setUniverse(nullptr);
	}


	void onEntityDestroyed(Entity entity)
	{
		u64 prefab = getPrefab(entity);
		if (prefab != 0)
		{
			Array<Entity>& instances = m_instances.get(prefab);
			instances.eraseItemFast(entity);
			if (instances.empty()) m_instances.erase(prefab);
		}
	}


	void setPrefab(Entity entity, u64 prefab) override
	{
		getMutableInstances(prefab).push(entity);
	}


	u64 getPrefab(Entity entity) override
	{
		for (int j = 0; j < m_instances.size(); ++j)
		{
			Array<Entity>& entities = m_instances.at(j);
			for (int i = 0, c = entities.size(); i < c; ++i)
			{
				if (entities[i] == entity) return m_instances.getKey(j);
			}
		}
		return 0;
	}


	const Array<Entity>& getInstances(u64 prefab) override
	{
		int instances_index = m_instances.find(prefab);
		if (instances_index >= 0) return m_instances.at(instances_index);
		return m_instances.emplace(prefab, m_editor.getAllocator());
	}


	Array<Entity>& getMutableInstances(u64 prefab)
	{
		int instances_index = m_instances.find(prefab);
		if (instances_index >= 0) return m_instances.at(instances_index);
		return m_instances.emplace(prefab, m_editor.getAllocator());
	}


	void instantiatePrefab(PrefabResource& prefab, const Vec3& pos, const Quat& rot, float scale) override
	{
		if (m_prefabs.indexOf(&prefab) < 0)
		{
			m_prefabs.emplace(&prefab);
			prefab.getResourceManager().load(prefab);
		}
		InputBlob blob(prefab.blob.getData(), prefab.blob.getPos());
		TextDeserializer deserializer(blob);
		while (blob.getPosition() < blob.getSize())
		{
			u64 prefab;
			deserializer.read(&prefab);
			Array<Entity>& instances = getMutableInstances(prefab);
			Entity e = m_universe->createEntity(pos, rot);
			m_universe->setScale(e, scale);
			instances.emplace(e);
			u32 cmp_type;
			deserializer.read(&cmp_type);
			while (cmp_type != 0)
			{
				m_universe->deserializeComponent(deserializer, e, PropertyRegister::getComponentTypeFromHash(cmp_type));
				deserializer.read(&cmp_type);
			}
		}
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
		serializer.write("count", m_instances.size());
		for (int i = 0; i < m_instances.size(); ++i)
		{
			serializer.write("instance", m_instances.getKey(i));
			Array<Entity>& entities = m_instances.at(i);
			serializer.write("count", (i32)entities.size());
			for (int j = 0, c = entities.size(); j < c; ++j)
			{
				serializer.write("", entities[j]);
			}
		}
	}


	void deserialize(IDeserializer& serializer) override
	{
		int count;
		serializer.read(&count);
		for (int i = 0; i < count; ++i)
		{
			u32 hash;
			serializer.read(&hash);
			i32 instances_per_template;
			serializer.read(&instances_per_template);
			Array<Entity>& entities = m_instances.emplace(hash, m_editor.getAllocator());
			for (int j = 0; j < instances_per_template; ++j)
			{
				Entity entity;
				serializer.read(&entity);
				entities.push(entity);
			}
		}
	}


	DelegateList<void()>& updated() override { return m_updated; }


private:
	AssociativeArray<u64, Array<Entity>> m_instances;
	Array<PrefabResource*> m_prefabs;
	Universe* m_universe;
	WorldEditor& m_editor;
	DelegateList<void()> m_updated;
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