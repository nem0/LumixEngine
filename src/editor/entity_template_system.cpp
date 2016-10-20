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
#include "engine/resource.h"
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
	uint32 path_hash;
};


class AssetBrowserPlugin LUMIX_FINAL : public AssetBrowser::IPlugin
{
public:
	AssetBrowserPlugin(WorldEditor& _editor, EntityTemplateSystem& _system)
		: system(_system)
		, editor(_editor)
	{}


	bool onGUI(Lumix::Resource* resource, Lumix::ResourceType type) override
	{
		if (type != PREFAB_TYPE) return false;

		if (ImGui::Button("Instantiate"))
		{
			system.instantiatePrefab(editor.getCameraRaycastHit(), resource->getPath());
		}

		return true;
	}


	Lumix::ResourceType getResourceType(const char* ext) override
	{
		if (equalStrings(ext, "fab")) return PREFAB_TYPE;
		return INVALID_RESOURCE_TYPE;
	}
	
	
	void onResourceUnloaded(Lumix::Resource* resource) override {}
	const char* getName() const override { return "Prefab"; }
	bool hasResourceManager(Lumix::ResourceType type) const override { return type == PREFAB_TYPE; }


	bool acceptExtension(const char* ext, Lumix::ResourceType type) const override
	{
		return type == PREFAB_TYPE && equalStrings(ext, "fab");
	}


	EntityTemplateSystem& system;
	WorldEditor& editor;
};


class EntityTemplateSystemImpl LUMIX_FINAL : public EntityTemplateSystem
{
private:
	struct InstantiatePrefabCommand LUMIX_FINAL : public IEditorCommand
	{
		InstantiatePrefabCommand(WorldEditor& _editor)
			: editor(_editor)
			, entities(_editor.getAllocator())
		{
		}


		bool execute() override
		{
			Lumix::Path path(path_hash);
			FS::OsFile file;
			if (!file.open(path.c_str(), Lumix::FS::Mode::OPEN_AND_READ, editor.getAllocator()))
			{
				g_log_error.log("Editor") << "Failed to open " << path.c_str();
				return false;
			}

			Array<uint8> data(editor.getAllocator());
			data.resize((int)file.size());
			file.read(&data[0], data.size());
			InputBlob blob(&data[0], data.size());
			entities.clear();
			editor.getEngine().pasteEntities(position, *editor.getUniverse(), blob, entities);

			auto& system = static_cast<EntityTemplateSystemImpl&>(editor.getEntityTemplateSystem());
			if (record_instance)
			{
				auto& inst = system.m_prefab_instances.emplace();
				inst.position = position;
				inst.path_hash = path.getHash();
			}

			for (int i = 0; i < entities.size(); ++i)
			{
				Entity entity = entities[i];
				PrefabEntity prefab_entity;
				prefab_entity.path_hash = path.getHash();
				prefab_entity.prev = i > 0 ? entities[i - 1] : INVALID_ENTITY;
				prefab_entity.next = i < entities.size() - 1 ? entities[i + 1] : INVALID_ENTITY;
				system.m_prefab_entities.insert(entity, prefab_entity);
				StaticString<MAX_PATH_LENGTH + 32> tmp(path.c_str(), "_", i);
				auto& instances = system.getMutableInstances(crc32(tmp));
				string tmp_str(tmp, editor.getAllocator());
				if (system.m_template_names.indexOf(tmp_str) < 0)
				{
					system.m_template_names.emplace(tmp, editor.getAllocator());
				}
				instances.push(entity);
			}

			file.close();
			return true;
		}


		void undo() override
		{
			auto& system = static_cast<EntityTemplateSystemImpl&>(editor.getEntityTemplateSystem());
			Universe& universe = *editor.getUniverse();
			for (auto entity : entities) universe.destroyEntity(entity);

			system.m_prefab_instances.pop();
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("position_x", position.x);
			serializer.serialize("position_y", position.y);
			serializer.serialize("position_z", position.z);
			serializer.serialize("path_hash", path_hash);
			serializer.serialize("record_instance", record_instance);
		}


		void deserialize(JsonSerializer& serializer) override
		{
			serializer.deserialize("position_x", position.x, 0);
			serializer.deserialize("position_y", position.y, 0);
			serializer.deserialize("position_z", position.z, 0);
			serializer.deserialize("path_hash", path_hash, 0);
			serializer.deserialize("record_instance", record_instance, true);
		}


		const char* getType() override
		{
			return "instantiate_prefab";
		}


		bool merge(IEditorCommand& command) { return false; }


		Vec3 position;
		uint32 path_hash;
		bool record_instance;
		WorldEditor& editor;
		Array<Entity> entities;
	};

	class CreateTemplateCommand LUMIX_FINAL : public IEditorCommand
	{
	public:
		explicit CreateTemplateCommand(WorldEditor& editor)
			: m_entity_system(
				static_cast<EntityTemplateSystemImpl&>(editor.getEntityTemplateSystem()))
			, m_editor(editor)
			, m_name(editor.getAllocator())
			, m_entity(INVALID_ENTITY)
		{
		}


		CreateTemplateCommand(WorldEditor& editor,
			const char* template_name,
			Entity entity_template)
			: m_entity_system(
				  static_cast<EntityTemplateSystemImpl&>(editor.getEntityTemplateSystem()))
			, m_name(template_name, editor.getAllocator())
			, m_entity(entity_template)
			, m_editor(editor)
		{
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("template_name", m_name.c_str());
			serializer.serialize("entity", m_entity);
		}


		void deserialize(JsonSerializer& serializer) override
		{
			char name[50];
			serializer.deserialize("template_name", name, sizeof(name), "");
			m_name = name;
			serializer.deserialize("entity", m_entity, INVALID_ENTITY);
		}


		bool execute() override
		{
			uint32 name_hash = crc32(m_name.c_str());
			if (m_entity_system.m_instances.find(name_hash) < 0)
			{
				m_entity_system.m_template_names.push(m_name);
				m_entity_system.m_instances.emplace(name_hash, m_editor.getAllocator());
				m_entity_system.m_instances.get(name_hash).push(m_entity);
				m_entity_system.m_updated.invoke();
			}
			else
			{
				return false;
			}
			return true;
		}


		void undo() override
		{
			m_entity_system.m_template_names.eraseItem(m_name);
			uint32 name_hash = crc32(m_name.c_str());
			m_entity_system.m_instances.erase(name_hash);
			m_entity_system.m_updated.invoke();
		}


		bool merge(IEditorCommand&) override { return false; }


		const char* getType() override
		{
			return "create_entity_template";
		}


		Entity getEntity() const { return m_entity; }

	private:
		EntityTemplateSystemImpl& m_entity_system;
		WorldEditor& m_editor;
		string m_name;
		Entity m_entity;
	};

	class CreateInstanceCommand LUMIX_FINAL : public IEditorCommand
	{
	public:
		explicit CreateInstanceCommand(WorldEditor& editor)
			: m_entity_system(static_cast<EntityTemplateSystemImpl&>(editor.getEntityTemplateSystem()))
			, m_editor(editor)
			, m_template_name_hash(0)
			, m_position(0, 0, 0)
			, m_rotation(0, 0, 0, 1)
			, m_size(1)
			, m_entity(INVALID_ENTITY)
		{
		}


		CreateInstanceCommand(EntityTemplateSystemImpl& entity_system,
			WorldEditor& editor,
			const char* template_name,
			const Vec3& position,
			const Quat& rot,
			float size)
			: m_entity_system(entity_system)
			, m_editor(editor)
			, m_template_name_hash(crc32(template_name))
			, m_position(position)
			, m_rotation(rot)
			, m_size(size)
			, m_entity(INVALID_ENTITY)
		{
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("template_name_hash", m_template_name_hash);
			serializer.serialize("entity", m_entity);
			serializer.serialize("position_x", m_position.x);
			serializer.serialize("position_y", m_position.y);
			serializer.serialize("position_z", m_position.z);
			serializer.serialize("rotation_x", m_rotation.x);
			serializer.serialize("rotation_y", m_rotation.y);
			serializer.serialize("rotation_z", m_rotation.z);
			serializer.serialize("rotation_w", m_rotation.w);
			serializer.serialize("size", m_size);
		}


		void deserialize(JsonSerializer& serializer) override
		{
			serializer.deserialize("template_name_hash", m_template_name_hash, 0);
			serializer.deserialize("entity", m_entity, INVALID_ENTITY);
			serializer.deserialize("position_x", m_position.x, 0);
			serializer.deserialize("position_y", m_position.y, 0);
			serializer.deserialize("position_z", m_position.z, 0);
			serializer.deserialize("rotation_x", m_rotation.x, 0);
			serializer.deserialize("rotation_y", m_rotation.y, 0);
			serializer.deserialize("rotation_z", m_rotation.z, 0);
			serializer.deserialize("rotation_w", m_rotation.w, 0);
			serializer.deserialize("size", m_size, 1);
		}


		bool execute() override
		{
			int instance_index = m_entity_system.m_instances.find(m_template_name_hash);
			if (instance_index >= 0)
			{
				Universe* universe = m_entity_system.m_editor.getUniverse();
				m_entity = universe->createEntity(m_position, m_rotation);
				universe->setScale(m_entity, m_size);

				m_entity_system.m_instances.at(instance_index).push(m_entity);
				Entity template_entity = m_entity_system.m_instances.at(instance_index)[0];
				for (ComponentUID cmp = universe->getFirstComponent(template_entity); cmp.isValid();
					 cmp = universe->getNextComponent(cmp))
				{
					m_entity_system.m_editor.cloneComponent(cmp, m_entity);
				}
			}
			else
			{
				ASSERT(false);
			}
			return true;
		}


		void undo() override
		{
			m_entity_system.m_universe->destroyEntity(m_entity);
			m_entity = INVALID_ENTITY;
		}


		bool merge(IEditorCommand&) override { return false; }


		const char* getType() override
		{
			return "create_entity_template_instance";
		}


		Entity getEntity() const { return m_entity; }

	private:
		EntityTemplateSystemImpl& m_entity_system;
		WorldEditor& m_editor;
		uint32 m_template_name_hash;
		Entity m_entity;
		Vec3 m_position;
		Quat m_rotation;
		float m_size;
	};

public:
	explicit EntityTemplateSystemImpl(WorldEditor& editor)
		: m_editor(editor)
		, m_prefab_entities(editor.getAllocator())
		, m_prefab_instances(editor.getAllocator())
		, m_universe(nullptr)
		, m_instances(editor.getAllocator())
		, m_updated(editor.getAllocator())
		, m_template_names(editor.getAllocator())
	{
		editor.universeCreated().bind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onUniverseCreated>(this);
		editor.universeDestroyed().bind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onUniverseDestroyed>(this);
		setUniverse(editor.getUniverse());
		editor.registerEditorCommandCreator(
			"create_entity_template_instance", &EntityTemplateSystemImpl::createCreateInstanceCommand);
		editor.registerEditorCommandCreator(
			"create_entity_template", &EntityTemplateSystemImpl::createCreateTemplateCommand);
		editor.registerEditorCommandCreator(
			"instantiate_prefab", &EntityTemplateSystemImpl::createInstantiatePrefabCommand);
	}


	void setStudioApp(StudioApp& app) override
	{
		app.getAssetBrowser()->addPlugin(*LUMIX_NEW(m_editor.getAllocator(), AssetBrowserPlugin)(m_editor, *this));
	}


	static IEditorCommand* createCreateInstanceCommand(WorldEditor& editor)
	{
		return LUMIX_NEW(editor.getAllocator(), CreateInstanceCommand)(editor);
	}


	static IEditorCommand* createCreateTemplateCommand(WorldEditor& editor)
	{
		return LUMIX_NEW(editor.getAllocator(), CreateTemplateCommand)(editor);
	}


	static IEditorCommand* createInstantiatePrefabCommand(WorldEditor& editor)
	{
		return LUMIX_NEW(editor.getAllocator(), InstantiatePrefabCommand)(editor);
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
		m_template_names.clear();
		setUniverse(m_editor.getUniverse());
	}


	void onUniverseDestroyed()
	{
		m_instances.clear();
		m_template_names.clear();
		setUniverse(nullptr);
	}


	void onEntityDestroyed(Entity entity)
	{
		uint32 tpl = getTemplate(entity);
		if (tpl != 0)
		{
			Array<Entity>& instances = m_instances.get(tpl);
			instances.eraseItemFast(entity);
			if (instances.empty())
			{
				m_instances.erase(tpl);
			}
		}

		auto iter = m_prefab_entities.find(entity);
		if (iter != m_prefab_entities.end())
		{
			PrefabEntity tmp = iter.value();
			if (isValid(tmp.prev)) m_prefab_entities[tmp.prev].next = tmp.next;
			if (isValid(tmp.next)) m_prefab_entities[tmp.next].prev = tmp.prev;
			m_prefab_entities.erase(iter);
		}
	}


	void setPrefab(Entity entity, const PrefabEntity& prefab) override
	{
		if (isValid(prefab.prev)) m_prefab_entities[prefab.prev].next = entity;
		if (isValid(prefab.next)) m_prefab_entities[prefab.next].prev = entity;
		m_prefab_entities.insert(entity, prefab);
	}


	PrefabEntity getPrefabEntity(Entity entity) override
	{
		auto iter = m_prefab_entities.find(entity);
		if (iter.isValid()) return iter.value();

		return {0, INVALID_ENTITY, INVALID_ENTITY};
	}


	bool isPrefab() override
	{
		auto& selected = m_editor.getSelectedEntities();
		if (selected.empty()) return false;

		auto iter = m_prefab_entities.find(selected[0]);
		if (!iter.isValid()) return false;

		return true;
	}


	void selectPrefab() override
	{
		auto& selected = m_editor.getSelectedEntities();
		if (selected.empty()) return;

		auto iter = m_prefab_entities.find(selected[0]);
		if (!iter.isValid()) return;

		PrefabEntity tmp = iter.value();
		Array<Entity> entities(m_editor.getAllocator());

		Entity e = selected[0];
		while (isValid(tmp.prev))
		{
			e = tmp.prev;
			tmp = m_prefab_entities[e];
		}

		while (isValid(e))
		{
			entities.push(e);
			e = m_prefab_entities[e].next;
		}

		m_editor.selectEntities(&entities[0], entities.size());
	}


	void applyPrefab() override
	{
		auto& selected = m_editor.getSelectedEntities();
		if (selected.empty()) return;

		auto iter = m_prefab_entities.find(selected[0]);
		if (!iter.isValid()) return;
		
		PrefabEntity tmp = iter.value();
		Array<Entity> entities(m_editor.getAllocator());

		Entity e = selected[0];
		while (isValid(tmp.prev))
		{
			e = tmp.prev;
			tmp = m_prefab_entities[e];
		}

		while (isValid(e))
		{
			entities.push(e);
			e = m_prefab_entities[e].next;
		}

		FS::OsFile file;
		Lumix::Path path(tmp.path_hash);
		if (!file.open(path.c_str(), Lumix::FS::Mode::CREATE_AND_WRITE, m_editor.getAllocator()))
		{
			g_log_error.log("Editor") << "Failed to create " << path.c_str();
			return;
		}

		OutputBlob blob(m_editor.getAllocator());
		m_editor.copyEntities(&entities[0], entities.size(), blob);

		file.write(blob.getData(), blob.getPos());

		file.close();

		refreshPrefabs();
	}


	void createTemplateFromEntity(const char* name, Entity entity) override
	{
		CreateTemplateCommand* command =
			LUMIX_NEW(m_editor.getAllocator(), CreateTemplateCommand)(m_editor, name, entity);
		m_editor.executeCommand(command);
	}


	void setTemplate(Entity entity, uint32 template_name_hash) override
	{
		int idx = m_instances.find(template_name_hash);
		if (idx >= 0)
		{
			m_instances.at(idx).push(entity);
			return;
		}
		auto& value = m_instances.emplace(template_name_hash, m_editor.getAllocator());
		value.push(entity);
	}


	uint32 getTemplate(Entity entity) override
	{
		for (int j = 0; j < m_instances.size(); ++j)
		{
			Array<Entity>& entities = m_instances.at(j);
			for (int i = 0, c = entities.size(); i < c; ++i)
			{
				if (entities[i] == entity)
				{
					return m_instances.getKey(j);
				}
			}
		}
		return 0;
	}


	const Array<Entity>& getInstances(uint32 template_name_hash) override
	{
		int instances_index = m_instances.find(template_name_hash);
		if (instances_index >= 0) return m_instances.at(instances_index);
		return m_instances.emplace(template_name_hash, m_editor.getAllocator());
	}


	Array<Entity>& getMutableInstances(uint32 template_name_hash)
	{
		int instances_index = m_instances.find(template_name_hash);
		if (instances_index >= 0) return m_instances.at(instances_index);
		return m_instances.emplace(template_name_hash, m_editor.getAllocator());
	}


	void savePrefab(const Lumix::Path& path) override
	{
		auto& entities = m_editor.getSelectedEntities();
		if (entities.empty()) return;

		FS::OsFile file;
		if (!file.open(path.c_str(), Lumix::FS::Mode::CREATE_AND_WRITE, m_editor.getAllocator()))
		{
			g_log_error.log("Editor") << "Failed to create " << path.c_str();
			return;
		}

		OutputBlob blob(m_editor.getAllocator());
		m_editor.copyEntities(&entities[0], entities.size(), blob);

		file.write(blob.getData(), blob.getPos());

		file.close();
	}


	void instantiatePrefab(const Vec3& pos, const Lumix::Path& path, bool record_instance)
	{
		InstantiatePrefabCommand* cmd = LUMIX_NEW(m_editor.getAllocator(), InstantiatePrefabCommand)(m_editor);
		cmd->position = pos;
		cmd->path_hash = path.getHash();
		cmd->record_instance = record_instance;
		if (record_instance)
		{
			m_editor.executeCommand(cmd);
		}
		else
		{
			cmd->execute();
			LUMIX_DELETE(m_editor.getAllocator(), cmd);
		}
	}


	void instantiatePrefab(const Vec3& pos, const Lumix::Path& path) override
	{
		instantiatePrefab(pos, path, true);
	}


	Entity createInstance(const char* name, const Vec3& position, const Quat& rotation, float size) override
	{
		CreateInstanceCommand* command = LUMIX_NEW(m_editor.getAllocator(), CreateInstanceCommand)(
			*this, m_editor, name, position, rotation, size);
		m_editor.executeCommand(command);
		return command->getEntity();
	}


	void serialize(OutputBlob& serializer) override
	{
		serializer.write((int32)m_template_names.size());
		for (int i = 0, c = m_template_names.size(); i < c; ++i)
		{
			serializer.writeString(m_template_names[i].c_str());
		}
		serializer.write((int32)m_instances.size());
		for (int i = 0; i < m_instances.size(); ++i)
		{
			serializer.write(m_instances.getKey(i));
			Array<Entity>& entities = m_instances.at(i);
			serializer.write((int32)entities.size());
			for (int j = 0, c = entities.size(); j < c; ++j)
			{
				serializer.write(entities[j]);
			}
		}
		
		serializer.write(m_prefab_entities.size());
		for (auto iter = m_prefab_entities.begin(), end = m_prefab_entities.end(); iter != end; ++iter)
		{
			serializer.write(iter.key());
			serializer.write(iter.value());
		}

		serializer.write(m_prefab_instances.size());
		for (auto& inst : m_prefab_instances)
		{
			serializer.write(inst);
		}
	}


	void refreshPrefabs() override
	{
		while(!m_prefab_entities.empty())
		{
			m_universe->destroyEntity(m_prefab_entities.begin().key());
		}
		m_prefab_entities.clear();
		for (auto& inst : m_prefab_instances)
		{
			instantiatePrefab(inst.position, Lumix::Path(inst.path_hash), false);
		}
	}


	void deserialize(InputBlob& serializer, bool has_prefabs) override
	{
		m_template_names.clear();
		m_instances.clear();
		m_prefab_entities.clear();
		m_prefab_instances.clear();
		int32 count;
		serializer.read(count);
		for (int i = 0; i < count; ++i)
		{
			const int MAX_NAME_LENGTH = 50;
			char name[MAX_NAME_LENGTH];
			serializer.readString(name, MAX_NAME_LENGTH);
			m_template_names.emplace(name, m_editor.getAllocator());
		}
		serializer.read(count);
		for (int i = 0; i < count; ++i)
		{
			uint32 hash;
			serializer.read(hash);
			int32 instances_per_template;
			serializer.read(instances_per_template);
			Array<Entity>& entities = m_instances.emplace(hash, m_editor.getAllocator());
			for (int j = 0; j < instances_per_template; ++j)
			{
				Entity entity;
				serializer.read(entity);
				entities.push(entity);
			}
		}

		if (has_prefabs)
		{
			serializer.read(count);
			for (int i = 0; i < count; ++i)
			{
				Entity entity;
				serializer.read(entity);
				PrefabEntity prefab_entity;
				serializer.read(prefab_entity);
				m_prefab_entities.insert(entity, prefab_entity);
			}

			serializer.read(count);
			for (int i = 0; i < count; ++i)
			{
				PrefabInstance& inst = m_prefab_instances.emplace();
				serializer.read(inst);
			}
		}

		m_updated.invoke();
	}


	Array<string>& getTemplateNames() override { return m_template_names; }


	DelegateList<void()>& updated() override { return m_updated; }


private:
	AssociativeArray<uint32, Array<Entity>> m_instances;
	Array<string> m_template_names;
	Universe* m_universe;
	WorldEditor& m_editor;
	DelegateList<void()> m_updated;
	HashMap<Entity, PrefabEntity> m_prefab_entities;
	Array<PrefabInstance> m_prefab_instances;
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