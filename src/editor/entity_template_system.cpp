#include "entity_template_system.h"
#include "core/array.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/json_serializer.h"
#include "core/math_utils.h"
#include "core/string.h"
#include "editor/ieditor_command.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "iplugin.h"
#include "universe/universe.h"
#include <cstdlib>


namespace Lumix
{


class EntityTemplateSystemImpl : public EntityTemplateSystem
{
private:
	class CreateTemplateCommand : public IEditorCommand
	{
	public:
		explicit CreateTemplateCommand(WorldEditor& editor)
			: m_entity_system(
				  static_cast<EntityTemplateSystemImpl&>(editor.getEntityTemplateSystem()))
			, m_editor(editor)
			, m_name(editor.getAllocator())
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
			serializer.deserialize("entity", m_entity, -1);
		}


		bool execute() override
		{
			uint32 name_hash = crc32(m_name.c_str());
			if (m_entity_system.m_instances.find(name_hash) < 0)
			{
				m_entity_system.m_template_names.push(m_name);
				m_entity_system.m_instances.insert(
					name_hash, Array<Entity>(m_editor.getAllocator()));
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


		uint32 getType() override
		{
			static const uint32 hash = crc32("create_entity_template");
			return hash;
		}


		Entity getEntity() const { return m_entity; }

	private:
		EntityTemplateSystemImpl& m_entity_system;
		WorldEditor& m_editor;
		string m_name;
		Entity m_entity;
	};

	class CreateInstanceCommand : public IEditorCommand
	{
	public:
		explicit CreateInstanceCommand(WorldEditor& editor)
			: m_entity_system(
				  static_cast<EntityTemplateSystemImpl&>(editor.getEntityTemplateSystem()))
			, m_editor(editor)
		{
		}


		CreateInstanceCommand(EntityTemplateSystemImpl& entity_system,
			WorldEditor& editor,
			const char* template_name,
			const Vec3& position,
			const Quat& rot,
			float size)
			: m_entity_system(entity_system)
			, m_template_name_hash(crc32(template_name))
			, m_position(position)
			, m_rotation(rot)
			, m_size(size)
			, m_editor(editor)
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
			serializer.deserialize("entity", m_entity, -1);
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
				const WorldEditor::ComponentList& template_cmps =
					m_editor.getComponents(template_entity);
				for (int i = 0; i < template_cmps.size(); ++i)
				{
					m_entity_system.m_editor.cloneComponent(template_cmps[i], m_entity);
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
			const WorldEditor::ComponentList& cmps = m_editor.getComponents(m_entity);
			for (int i = 0; i < cmps.size(); ++i)
			{
				cmps[i].scene->destroyComponent(cmps[i].index, cmps[i].type);
			}
			m_entity_system.m_universe->destroyEntity(m_entity);
			m_entity = INVALID_ENTITY;
		}


		bool merge(IEditorCommand&) override { return false; }


		uint32 getType() override
		{
			static const uint32 hash = crc32("create_entity_template_instance");
			return hash;
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
		, m_universe(nullptr)
		, m_instances(editor.getAllocator())
		, m_updated(editor.getAllocator())
		, m_template_names(editor.getAllocator())
	{
		editor.universeCreated()
			.bind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onUniverseCreated>(this);
		editor.universeDestroyed()
			.bind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onUniverseDestroyed>(this);
		setUniverse(editor.getUniverse());
		editor.registerEditorCommandCreator("create_entity_template_instance",
			&EntityTemplateSystemImpl::createCreateInstanceCommand);
		editor.registerEditorCommandCreator(
			"create_entity_template", &EntityTemplateSystemImpl::createCreateTemplateCommand);
	}


	static IEditorCommand* createCreateInstanceCommand(WorldEditor& editor)
	{
		return LUMIX_NEW(editor.getAllocator(), CreateInstanceCommand)(editor);
	}


	static IEditorCommand* createCreateTemplateCommand(WorldEditor& editor)
	{
		return LUMIX_NEW(editor.getAllocator(), CreateTemplateCommand)(editor);
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
				for (int i = 0; i < m_template_names.size(); ++i)
				{
					if (crc32(m_template_names[i].c_str()) == tpl)
					{
						m_template_names.eraseFast(i);
						break;
					}
				}
			}
		}
	}


	void createTemplateFromEntity(const char* name, Entity entity) override
	{
		CreateTemplateCommand* command =
			LUMIX_NEW(m_editor.getAllocator(), CreateTemplateCommand)(m_editor, name, entity);
		m_editor.executeCommand(command);
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
		if (instances_index < 0)
		{
			m_instances.insert(template_name_hash, Array<Entity>(m_editor.getAllocator()));
			instances_index = m_instances.find(template_name_hash);
		}
		return m_instances.at(instances_index);
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
	}


	void deserialize(InputBlob& serializer) override
	{
		m_template_names.clear();
		m_instances.clear();
		int32 count;
		serializer.read(count);
		for (int i = 0; i < count; ++i)
		{
			const int MAX_NAME_LENGTH = 50;
			char name[MAX_NAME_LENGTH];
			serializer.readString(name, MAX_NAME_LENGTH);
			m_template_names.push(string(name, m_editor.getAllocator()));
		}
		serializer.read(count);
		for (int i = 0; i < count; ++i)
		{
			uint32 hash;
			serializer.read(hash);
			int32 instances_per_template;
			serializer.read(instances_per_template);
			m_instances.insert(hash, Array<Entity>(m_editor.getAllocator()));
			Array<Entity>& entities = m_instances.get(hash);
			for (int j = 0; j < instances_per_template; ++j)
			{
				int32 entity_index;
				serializer.read(entity_index);
				entities.push(Entity(entity_index));
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