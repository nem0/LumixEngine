#include "world_editor.h"

#include "editor/editor_icon.h"
#include "editor/entity_groups.h"
#include "editor/gizmo.h"
#include "editor/measure_tool.h"
#include "editor/platform_interface.h"
#include "editor/prefab_system.h"
#include "engine/array.h"
#include "engine/associative_array.h"
#include "engine/blob.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug/debug.h"
#include "engine/delegate_list.h"
#include "engine/engine.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/memory_file_device.h"
#include "engine/fs/os_file.h"
#include "engine/fs/tcp_file_device.h"
#include "engine/fs/tcp_file_server.h"
#include "engine/geometry.h"
#include "engine/input_system.h"
#include "engine/iplugin.h"
#include "engine/iproperty_descriptor.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/matrix.h"
#include "engine/path.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/property_register.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/serializer.h"
#include "engine/system.h"
#include "engine/timer.h"
#include "engine/universe/universe.h"
#include "ieditor_command.h"
#include "render_interface.h"


namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = PropertyRegister::getComponentType("renderable");
static const ComponentType CAMERA_TYPE = PropertyRegister::getComponentType("camera");


struct BeginGroupCommand LUMIX_FINAL : public IEditorCommand
{
	BeginGroupCommand() {}
	BeginGroupCommand(WorldEditor&) {}

	bool execute() override { return true; }
	void undo() override { ASSERT(false); }
	void serialize(JsonSerializer& serializer) override {}
	void deserialize(JsonSerializer& serializer) override {}
	bool merge(IEditorCommand& command) override { ASSERT(false); return false; }
	const char* getType() override { return "begin_group"; }
};


struct EndGroupCommand LUMIX_FINAL : public IEditorCommand
{
	EndGroupCommand() {}
	EndGroupCommand(WorldEditor&) {}

	bool execute() override { return true; }
	void undo() override { ASSERT(false); }
	void serialize(JsonSerializer& serializer) override {}
	void deserialize(JsonSerializer& serializer) override {}
	bool merge(IEditorCommand& command) override { ASSERT(false); return false; }
	const char* getType() override { return "end_group"; }

	u32 group_type;
};


class SetEntityNameCommand LUMIX_FINAL : public IEditorCommand
{
public:
	explicit SetEntityNameCommand(WorldEditor& editor)
		: m_new_name(m_editor.getAllocator())
		, m_old_name(m_editor.getAllocator())
		, m_editor(editor)
	{
	}

	SetEntityNameCommand(WorldEditor& editor, Entity entity, const char* name)
		: m_entity(entity)
		, m_new_name(name, editor.getAllocator())
		, m_old_name(editor.getUniverse()->getEntityName(entity),
					 editor.getAllocator())
		, m_editor(editor)
	{
	}


	void serialize(JsonSerializer& serializer) override
	{
		serializer.serialize("name", m_new_name.c_str());
		serializer.serialize("entity", m_entity);
	}


	void deserialize(JsonSerializer& serializer) override
	{
		char name[100];
		serializer.deserialize("name", name, sizeof(name), "");
		m_new_name = name;
		serializer.deserialize("entity", m_entity, INVALID_ENTITY);
		m_old_name = m_editor.getUniverse()->getEntityName(m_entity);
	}


	bool execute() override
	{
		m_editor.getUniverse()->setEntityName(m_entity, m_new_name.c_str());
		return true;
	}


	void undo() override
	{
		m_editor.getUniverse()->setEntityName(m_entity, m_old_name.c_str());
	}


	const char* getType() override { return "set_entity_name"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		if (static_cast<SetEntityNameCommand&>(command).m_entity == m_entity)
		{
			static_cast<SetEntityNameCommand&>(command).m_new_name = m_new_name;
			return true;
		}
		else
		{
			return false;
		}
	}

private:
	WorldEditor& m_editor;
	Entity m_entity;
	string m_new_name;
	string m_old_name;
};


class PasteEntityCommand LUMIX_FINAL : public IEditorCommand
{
public:
	explicit PasteEntityCommand(WorldEditor& editor)
		: m_blob(editor.getAllocator())
		, m_editor(editor)
		, m_entities(editor.getAllocator())
	{
	}


	PasteEntityCommand(WorldEditor& editor, const OutputBlob& blob)
		: m_blob(blob, editor.getAllocator())
		, m_editor(editor)
		, m_position(editor.getCameraRaycastHit())
		, m_entities(editor.getAllocator())
	{
	}


	PasteEntityCommand(WorldEditor& editor, const Vec3& pos, const InputBlob& blob)
		: m_blob(blob, editor.getAllocator())
		, m_editor(editor)
		, m_position(pos)
		, m_entities(editor.getAllocator())
	{
	}


	bool execute() override;


	void serialize(JsonSerializer& serializer) override
	{
		serializer.serialize("pos_x", m_position.x);
		serializer.serialize("pos_y", m_position.y);
		serializer.serialize("pos_z", m_position.z);
		serializer.serialize("size", m_blob.getPos());
		serializer.beginArray("data");
		for (int i = 0; i < m_blob.getPos(); ++i)
		{
			serializer.serializeArrayItem((i32)((const u8*)m_blob.getData())[i]);
		}
		serializer.endArray();
	}


	void deserialize(JsonSerializer& serializer) override
	{
		serializer.deserialize("pos_x", m_position.x, 0);
		serializer.deserialize("pos_y", m_position.y, 0);
		serializer.deserialize("pos_z", m_position.z, 0);
		int size;
		serializer.deserialize("size", size, 0);
		serializer.deserializeArrayBegin("data");
		m_blob.clear();
		m_blob.reserve(size);
		for (int i = 0; i < size; ++i)
		{
			i32 data;
			serializer.deserializeArrayItem(data, 0);
			m_blob.write((u8)data);
		}
		serializer.deserializeArrayEnd();
	}


	void undo() override;


	const char* getType() override { return "paste_entity"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		return false;
	}


	const Array<Entity>& getEntities() { return m_entities; }


private:
	OutputBlob m_blob;
	WorldEditor& m_editor;
	Vec3 m_position;
	Array<Entity> m_entities;
};


class MoveEntityCommand LUMIX_FINAL : public IEditorCommand
{
public:
	explicit MoveEntityCommand(WorldEditor& editor)
		: m_new_positions(editor.getAllocator())
		, m_new_rotations(editor.getAllocator())
		, m_old_positions(editor.getAllocator())
		, m_old_rotations(editor.getAllocator())
		, m_entities(editor.getAllocator())
		, m_editor(editor)
	{
	}


	MoveEntityCommand(WorldEditor& editor,
					  const Entity* entities,
					  const Vec3* new_positions,
					  const Quat* new_rotations,
					  int count,
					  IAllocator& allocator)
		: m_new_positions(allocator)
		, m_new_rotations(allocator)
		, m_old_positions(allocator)
		, m_old_rotations(allocator)
		, m_entities(allocator)
		, m_editor(editor)
	{
		ASSERT(count > 0);
		Universe* universe = m_editor.getUniverse();
		for (int i = count - 1; i >= 0; --i)
		{
			m_entities.push(entities[i]);
			m_new_positions.push(new_positions[i]);
			m_new_rotations.push(new_rotations[i]);
			m_old_positions.push(universe->getPosition(entities[i]));
			m_old_rotations.push(universe->getRotation(entities[i]));
		}
	}


	void serialize(JsonSerializer& serializer) override
	{
		serializer.serialize("count", m_entities.size());
		serializer.beginArray("entities");
		for (int i = 0; i < m_entities.size(); ++i)
		{
			serializer.serializeArrayItem(m_entities[i]);
			serializer.serializeArrayItem(m_new_positions[i].x);
			serializer.serializeArrayItem(m_new_positions[i].y);
			serializer.serializeArrayItem(m_new_positions[i].z);
			serializer.serializeArrayItem(m_new_rotations[i].x);
			serializer.serializeArrayItem(m_new_rotations[i].y);
			serializer.serializeArrayItem(m_new_rotations[i].z);
			serializer.serializeArrayItem(m_new_rotations[i].w);
		}
		serializer.endArray();
	}


	void deserialize(JsonSerializer& serializer) override
	{
		Universe* universe = m_editor.getUniverse();
		int count;
		serializer.deserialize("count", count, 0);
		m_entities.resize(count);
		m_new_positions.resize(count);
		m_new_rotations.resize(count);
		m_old_positions.resize(count);
		m_old_rotations.resize(count);
		serializer.deserializeArrayBegin("entities");
		for (int i = 0; i < m_entities.size(); ++i)
		{
			serializer.deserializeArrayItem(m_entities[i], INVALID_ENTITY);
			serializer.deserializeArrayItem(m_new_positions[i].x, 0);
			serializer.deserializeArrayItem(m_new_positions[i].y, 0);
			serializer.deserializeArrayItem(m_new_positions[i].z, 0);
			serializer.deserializeArrayItem(m_new_rotations[i].x, 0);
			serializer.deserializeArrayItem(m_new_rotations[i].y, 0);
			serializer.deserializeArrayItem(m_new_rotations[i].z, 0);
			serializer.deserializeArrayItem(m_new_rotations[i].w, 0);
			m_old_positions[i] = universe->getPosition(m_entities[i]);
			m_old_rotations[i] = universe->getRotation(m_entities[i]);
		}
		serializer.deserializeArrayEnd();
	}


	bool execute() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i)
		{
			Entity entity = m_entities[i];
			universe->setPosition(entity, m_new_positions[i]);
			universe->setRotation(entity, m_new_rotations[i]);
		}
		return true;
	}


	void undo() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i)
		{
			Entity entity = m_entities[i];
			universe->setPosition(entity, m_old_positions[i]);
			universe->setRotation(entity, m_old_rotations[i]);
		}
	}


	const char* getType() override { return "move_entity"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		MoveEntityCommand& my_command = static_cast<MoveEntityCommand&>(command);
		if (my_command.m_entities.size() == m_entities.size())
		{
			for (int i = 0, c = m_entities.size(); i < c; ++i)
			{
				if (m_entities[i] != my_command.m_entities[i])
				{
					return false;
				}
			}
			for (int i = 0, c = m_entities.size(); i < c; ++i)
			{
				my_command.m_new_positions[i] = m_new_positions[i];
				my_command.m_new_rotations[i] = m_new_rotations[i];
			}
			return true;
		}
		else
		{
			return false;
		}
	}

private:
	WorldEditor& m_editor;
	Array<Entity> m_entities;
	Array<Vec3> m_new_positions;
	Array<Quat> m_new_rotations;
	Array<Vec3> m_old_positions;
	Array<Quat> m_old_rotations;
};


class ScaleEntityCommand LUMIX_FINAL : public IEditorCommand
{
public:
	explicit ScaleEntityCommand(WorldEditor& editor)
		: m_old_scales(editor.getAllocator())
		, m_entities(editor.getAllocator())
		, m_editor(editor)
	{
	}


	ScaleEntityCommand(WorldEditor& editor,
		const Entity* entities,
		int count,
		float scale,
		IAllocator& allocator)
		: m_old_scales(allocator)
		, m_entities(allocator)
		, m_editor(editor)
		, m_scale(scale)
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = count - 1; i >= 0; --i)
		{
			m_entities.push(entities[i]);
			m_old_scales.push(universe->getScale(entities[i]));
		}
	}


	void serialize(JsonSerializer& serializer) override
	{
		serializer.serialize("scale", m_scale);
		serializer.beginArray("entities");
		for (int i = 0; i < m_entities.size(); ++i)
		{
			serializer.serializeArrayItem(m_entities[i]);
		}
		serializer.endArray();
	}


	void deserialize(JsonSerializer& serializer) override
	{
		Universe* universe = m_editor.getUniverse();
		int count;
		serializer.deserialize("scale", m_scale, 1.0f);
		serializer.deserialize("count", count, 0);
		serializer.deserializeArrayBegin("entities");
		while (!serializer.isArrayEnd())
		{
			Entity entity;
			serializer.deserializeArrayItem(entity, INVALID_ENTITY);
			m_entities.push(entity);
			m_old_scales.push(universe->getScale(entity));
		}
		serializer.deserializeArrayEnd();
	}


	bool execute() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i)
		{
			Entity entity = m_entities[i];
			universe->setScale(entity, m_scale);
		}
		return true;
	}


	void undo() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i)
		{
			Entity entity = m_entities[i];
			universe->setScale(entity, m_old_scales[i]);
		}
	}


	const char* getType() override { return "scale_entity"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		auto& my_command = static_cast<ScaleEntityCommand&>(command);
		if (my_command.m_entities.size() == m_entities.size())
		{
			for (int i = 0, c = m_entities.size(); i < c; ++i)
			{
				if (m_entities[i] != my_command.m_entities[i])
				{
					return false;
				}
			}
			my_command.m_scale = m_scale;
			return true;
		}
		else
		{
			return false;
		}
	}

private:
	WorldEditor& m_editor;
	Array<Entity> m_entities;
	float m_scale;
	Array<float> m_old_scales;
};


class RemoveArrayPropertyItemCommand LUMIX_FINAL : public IEditorCommand
{

public:
	explicit RemoveArrayPropertyItemCommand(WorldEditor& editor)
		: m_old_values(editor.getAllocator())
		, m_editor(editor)
	{
	}

	RemoveArrayPropertyItemCommand(WorldEditor& editor,
		const ComponentUID& component,
		int index,
		IArrayDescriptor& descriptor)
		: m_component(component)
		, m_index(index)
		, m_descriptor(&descriptor)
		, m_old_values(editor.getAllocator())
		, m_editor(editor)
	{
		for (int i = 0, c = m_descriptor->getChildren().size(); i < c; ++i)
		{
			m_descriptor->getChildren()[i]->get(component, m_index, m_old_values);
		}
	}


	void serialize(JsonSerializer& serializer) override
	{
		serializer.serialize("inedx", m_index);
		serializer.serialize("entity_index", m_component.entity);
		serializer.serialize("component_index", m_component.handle);
		serializer.serialize("component_type", PropertyRegister::getComponentTypeHash(m_component.type));
		serializer.serialize("property_name_hash", m_descriptor->getNameHash());
	}


	void deserialize(JsonSerializer& serializer) override
	{
		serializer.deserialize("inedx", m_index, 0);
		serializer.deserialize("entity_index", m_component.entity, INVALID_ENTITY);
		serializer.deserialize("component_index", m_component.handle, INVALID_COMPONENT);
		u32 hash;
		serializer.deserialize("component_type", hash, 0);
		m_component.type = PropertyRegister::getComponentTypeFromHash(hash);
		m_component.scene = m_editor.getUniverse()->getScene(m_component.type);
		u32 property_name_hash;
		serializer.deserialize("property_name_hash", property_name_hash, 0);
		m_descriptor =
			static_cast<const IArrayDescriptor*>(PropertyRegister::getDescriptor(m_component.type, property_name_hash));
	}


	bool execute() override
	{
		m_descriptor->removeArrayItem(m_component, m_index);
		return true;
	}


	void undo() override
	{
		m_descriptor->addArrayItem(m_component, m_index);
		InputBlob old_values(m_old_values.getData(), m_old_values.getPos());
		for (int i = 0, c = m_descriptor->getChildren().size(); i < c; ++i)
		{
			m_descriptor->getChildren()[i]->set(m_component, m_index, old_values);
		}
	}


	const char* getType() override { return "remove_array_property_item"; }


	bool merge(IEditorCommand&) override { return false; }

private:
	WorldEditor& m_editor;
	ComponentUID m_component;
	int m_index;
	const IArrayDescriptor* m_descriptor;
	OutputBlob m_old_values;
};


class AddArrayPropertyItemCommand LUMIX_FINAL : public IEditorCommand
{

public:
	explicit AddArrayPropertyItemCommand(WorldEditor& editor)
		: m_editor(editor)
	{
	}

	AddArrayPropertyItemCommand(WorldEditor& editor,
								const ComponentUID& component,
								IArrayDescriptor& descriptor)
		: m_component(component)
		, m_index(-1)
		, m_descriptor(&descriptor)
		, m_editor(editor)
	{
	}


	void serialize(JsonSerializer& serializer) override
	{
		serializer.serialize("inedx", m_index);
		serializer.serialize("entity_index", m_component.entity);
		serializer.serialize("component_index", m_component.handle);
		serializer.serialize("component_type", PropertyRegister::getComponentTypeHash(m_component.type));
		serializer.serialize("property_name_hash", m_descriptor->getNameHash());
	}


	void deserialize(JsonSerializer& serializer) override
	{
		serializer.deserialize("inedx", m_index, 0);
		serializer.deserialize("entity_index", m_component.entity, INVALID_ENTITY);
		serializer.deserialize("component_index", m_component.handle, INVALID_COMPONENT);
		u32 hash;
		serializer.deserialize("component_type", hash, 0);
		m_component.type = PropertyRegister::getComponentTypeFromHash(hash);
		m_component.scene = m_editor.getUniverse()->getScene(m_component.type);
		u32 property_name_hash;
		serializer.deserialize("property_name_hash", property_name_hash, 0);
		m_descriptor = static_cast<const IArrayDescriptor*>(
			PropertyRegister::getDescriptor(m_component.type, property_name_hash));
	}


	bool execute() override
	{
		m_descriptor->addArrayItem(m_component, -1);
		m_index = m_descriptor->getCount(m_component) - 1;
		return true;
	}


	void undo() override
	{
		m_descriptor->removeArrayItem(m_component, m_index);
	}


	const char* getType() override { return "add_array_property_item"; }


	bool merge(IEditorCommand&) override { return false; }

private:
	ComponentUID m_component;
	int m_index;
	const IArrayDescriptor* m_descriptor;
	WorldEditor& m_editor;
};


class SetPropertyCommand LUMIX_FINAL : public IEditorCommand
{
public:
	explicit SetPropertyCommand(WorldEditor& editor)
		: m_editor(editor)
		, m_entities(editor.getAllocator())
		, m_new_value(editor.getAllocator())
		, m_old_value(editor.getAllocator())
	{
	}


	SetPropertyCommand(WorldEditor& editor,
		const Entity* entities,
		int count,
		ComponentType component_type,
		int index,
		const IPropertyDescriptor& property_descriptor,
		const void* data,
		int size)
		: m_component_type(component_type)
		, m_entities(editor.getAllocator())
		, m_property_descriptor(&property_descriptor)
		, m_editor(editor)
		, m_new_value(editor.getAllocator())
		, m_old_value(editor.getAllocator())
	{
		auto& prefab_system = editor.getPrefabSystem();
		m_entities.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			if (!m_editor.getUniverse()->getComponent(entities[i], m_component_type).isValid()) continue;
			u64 prefab = prefab_system.getPrefab(entities[i]);
			if (prefab == 0)
			{
				ComponentUID component = m_editor.getUniverse()->getComponent(entities[i], component_type);
				m_property_descriptor->get(component, index, m_old_value);
				m_entities.push(entities[i]);
			}
			else
			{
				Entity instance = prefab_system.getFirstInstance(prefab);
				while(isValid(instance))
				{
					ComponentUID component = m_editor.getUniverse()->getComponent(instance, component_type);
					m_property_descriptor->get(component, index, m_old_value);
					m_entities.push(instance);
					instance = prefab_system.getNextInstance(instance);
				}
			}
		}

		m_index = index;
		m_new_value.write(data, size);
	}


	void serialize(JsonSerializer& serializer) override
	{
		serializer.serialize("index", m_index);
		serializer.beginArray("entities");
		for (Entity entity : m_entities)
		{
			serializer.serializeArrayItem(entity);
		}
		serializer.endArray();
		serializer.serialize("component_type", PropertyRegister::getComponentTypeHash(m_component_type));
		serializer.beginArray("data");
		for (int i = 0; i < m_new_value.getPos(); ++i)
		{
			serializer.serializeArrayItem((int)((const u8*)m_new_value.getData())[i]);
		}
		serializer.endArray();
		serializer.serialize("property_name_hash", m_property_descriptor->getNameHash());
	}


	void deserialize(JsonSerializer& serializer) override
	{
		serializer.deserialize("index", m_index, 0);
		serializer.deserializeArrayBegin("entities");
		while (!serializer.isArrayEnd())
		{
			Entity entity;
			serializer.deserializeArrayItem(entity, INVALID_ENTITY);
			m_entities.push(entity);
		}
		serializer.deserializeArrayEnd();
		u32 hash;
		serializer.deserialize("component_type", hash, 0);
		m_component_type = PropertyRegister::getComponentTypeFromHash(hash);
		serializer.deserializeArrayBegin("data");
		m_new_value.clear();
		while (!serializer.isArrayEnd())
		{
			int data;
			serializer.deserializeArrayItem(data, 0);
			m_new_value.write((u8)data);
		}
		serializer.deserializeArrayEnd();
		u32 property_name_hash;
		serializer.deserialize("property_name_hash", property_name_hash, 0);
		m_property_descriptor = PropertyRegister::getDescriptor(m_component_type, property_name_hash);
	}


	bool execute() override
	{
		InputBlob blob(m_new_value);
		for (Entity entity : m_entities)
		{
			if (m_editor.getEditCamera().entity == entity && m_component_type == CAMERA_TYPE &&
				m_property_descriptor->getNameHash() == crc32("Slot"))
			{
				continue;
			}
			ComponentUID component = m_editor.getUniverse()->getComponent(entity, m_component_type);
			blob.rewind();
			m_property_descriptor->set(component, m_index, blob);
		}
		return true;
	}


	void undo() override
	{
		InputBlob blob(m_old_value);
		for (Entity entity : m_entities)
		{
			ComponentUID component = m_editor.getUniverse()->getComponent(entity, m_component_type);
			m_property_descriptor->set(component, m_index, blob);
		}
	}


	const char* getType() override { return "set_property_values"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		SetPropertyCommand& src = static_cast<SetPropertyCommand&>(command);
		if (m_component_type == src.m_component_type &&
			m_entities.size() == src.m_entities.size() &&
			src.m_property_descriptor == m_property_descriptor &&
			m_index == src.m_index)
		{
			for (int i = 0, c = m_entities.size(); i < c; ++i)
			{
				if (m_entities[i] != src.m_entities[i]) return false;
			}

			src.m_new_value = m_new_value;
			return true;
		}
		return false;
	}

private:
	WorldEditor& m_editor;
	ComponentType m_component_type;
	Lumix::Array<Entity> m_entities;
	OutputBlob m_new_value;
	OutputBlob m_old_value;
	int m_index;
	const IPropertyDescriptor* m_property_descriptor;
};


struct WorldEditorImpl LUMIX_FINAL : public WorldEditor
{
	friend class PasteEntityCommand;
private:
	class AddComponentCommand LUMIX_FINAL : public IEditorCommand
	{
	public:
		explicit AddComponentCommand(WorldEditor& editor)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
			, m_entities(editor.getAllocator())
		{
		}

		AddComponentCommand(WorldEditorImpl& editor,
							const Array<Entity>& entities,
							ComponentType type)
			: m_editor(editor)
			, m_entities(editor.getAllocator())
		{
			m_type = type;
			m_entities.reserve(entities.size());
			for (int i = 0; i < entities.size(); ++i)
			{
				if (!m_editor.getUniverse()->getComponent(entities[i], type).isValid())
				{
					u64 prefab = editor.getPrefabSystem().getPrefab(entities[i]);
					if (prefab == 0)
					{
						m_entities.push(entities[i]);
					}
					else
					{
						Entity instance = editor.getPrefabSystem().getFirstInstance(prefab);
						while(isValid(instance))
						{
							m_entities.push(instance);
							instance = editor.getPrefabSystem().getNextInstance(instance);
						}
					}
				}
			}
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("component_type", PropertyRegister::getComponentTypeHash(m_type));
			serializer.beginArray("entities");
			for (int i = 0; i < m_entities.size(); ++i)
			{
				serializer.serializeArrayItem(m_entities[i]);
			}
			serializer.endArray();
		}


		void deserialize(JsonSerializer& serializer) override
		{
			u32 hash;
			serializer.deserialize("component_type", hash, 0);
			m_type = PropertyRegister::getComponentTypeFromHash(hash);
			m_entities.clear();
			serializer.deserializeArrayBegin("entities");
			while (!serializer.isArrayEnd())
			{
				Entity& entity = m_entities.emplace();
				serializer.deserializeArrayItem(entity, INVALID_ENTITY);
			}
			serializer.deserializeArrayEnd();
		}


		bool merge(IEditorCommand&) override { return false; }


		const char* getType() override { return "add_component"; }


		bool execute() override
		{
			bool ret = false;
			IScene* scene = m_editor.getUniverse()->getScene(m_type);

			for (int j = 0; j < m_entities.size(); ++j)
			{
				ComponentHandle cmp = scene->createComponent(m_type, m_entities[j]);
				if (isValid(cmp))
				{
					ret = true;
				}
			}
			return ret;
		}


		void undo() override
		{
			for (int i = 0; i < m_entities.size(); ++i)
			{
				const ComponentUID& cmp = m_editor.getUniverse()->getComponent(m_entities[i], m_type);
				cmp.scene->destroyComponent(cmp.handle, cmp.type);
			}
		}


	private:
		ComponentType m_type;
		Array<Entity> m_entities;
		WorldEditorImpl& m_editor;
	};


	class DestroyEntitiesCommand LUMIX_FINAL : public IEditorCommand
	{
	public:
		explicit DestroyEntitiesCommand(WorldEditor& editor)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
			, m_entities(editor.getAllocator())
			, m_transformations(editor.getAllocator())
			, m_old_values(editor.getAllocator())
			, m_resources(editor.getAllocator())
		{
		}


		DestroyEntitiesCommand(WorldEditorImpl& editor, const Entity* entities, int count)
			: m_editor(editor)
			, m_entities(editor.getAllocator())
			, m_transformations(editor.getAllocator())
			, m_old_values(editor.getAllocator())
			, m_resources(editor.getAllocator())
		{
			m_entities.reserve(count);
			m_transformations.reserve(m_entities.size());
			for (int i = 0; i < count; ++i)
			{
				m_entities.push(entities[i]);
			}
		}


		~DestroyEntitiesCommand()
		{
			for (Resource* resource : m_resources)
			{
				resource->getResourceManager().unload(*resource);
			}
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("count", m_entities.size());
			serializer.beginArray("entities");
			for (int i = 0; i < m_entities.size(); ++i)
			{
				serializer.serializeArrayItem(m_entities[i]);
				serializer.serializeArrayItem(m_transformations[i].pos.x);
				serializer.serializeArrayItem(m_transformations[i].pos.y);
				serializer.serializeArrayItem(m_transformations[i].pos.z);
				serializer.serializeArrayItem(m_transformations[i].rot.x);
				serializer.serializeArrayItem(m_transformations[i].rot.y);
				serializer.serializeArrayItem(m_transformations[i].rot.z);
				serializer.serializeArrayItem(m_transformations[i].rot.w);
			}
			serializer.endArray();
		}


		void deserialize(JsonSerializer& serializer) override
		{
			int count;
			serializer.deserialize("count", count, 0);
			serializer.deserializeArrayBegin("entities");
			m_entities.resize(count);
			m_transformations.resize(count);
			for (int i = 0; i < count; ++i)
			{
				serializer.deserializeArrayItem(m_entities[i], INVALID_ENTITY);
				serializer.deserializeArrayItem(m_transformations[i].pos.x, 0);
				serializer.deserializeArrayItem(m_transformations[i].pos.y, 0);
				serializer.deserializeArrayItem(m_transformations[i].pos.z, 0);
				serializer.deserializeArrayItem(m_transformations[i].pos.x, 0);
				serializer.deserializeArrayItem(m_transformations[i].rot.y, 0);
				serializer.deserializeArrayItem(m_transformations[i].rot.z, 0);
				serializer.deserializeArrayItem(m_transformations[i].rot.w, 0);
			}
			serializer.deserializeArrayEnd();
		}


		bool execute() override
		{
			Universe* universe = m_editor.getUniverse();
			m_transformations.clear();
			m_old_values.clear();
			ResourceManager& resource_manager = m_editor.getEngine().getResourceManager();
			for (int i = 0; i < m_entities.size(); ++i)
			{
				m_transformations.emplace(universe->getTransform(m_entities[i]));
				int count = 0;
				for (ComponentUID cmp = universe->getFirstComponent(m_entities[i]);
					cmp.isValid();
					cmp = universe->getNextComponent(cmp))
				{
					++count;
				}
				EntityGUID guid = m_editor.m_entity_map.get(m_entities[i]);
				m_old_values.write(guid.value);
				m_old_values.write(count);
				for (ComponentUID cmp = universe->getFirstComponent(m_entities[i]);
					cmp.isValid();
					cmp = universe->getNextComponent(cmp))
				{
					m_old_values.write(cmp.type);
					Array<IPropertyDescriptor*>& props = PropertyRegister::getDescriptors(cmp.type);
					for (int k = 0; k < props.size(); ++k)
					{
						props[k]->get(cmp, -1, m_old_values);
						if (props[k]->getType() == IPropertyDescriptor::RESOURCE)
						{
							auto* resource_prop = static_cast<IResourcePropertyDescriptor*>(props[k]);
							ResourceType resource_type = resource_prop->getResourceType();
							OutputBlob tmp(m_editor.getAllocator());
							props[k]->get(cmp, -1, tmp);
							Path path((const char*)tmp.getData());
							Resource* resource = resource_manager.get(resource_type)->load(path);
							m_resources.push(resource);
						}
					}
				}
				u64 prefab = m_editor.getPrefabSystem().getPrefab(m_entities[i]);
				m_old_values.write(prefab);

				universe->destroyEntity(m_entities[i]);
				m_editor.m_entity_map.erase(m_entities[i]);
			}
			return true;
		}


		bool merge(IEditorCommand&) override { return false; }


		void undo() override
		{
			Universe* universe = m_editor.getUniverse();
			InputBlob blob(m_old_values);
			for (int i = 0; i < m_entities.size(); ++i)
			{
				Entity new_entity =
					universe->createEntity(m_transformations[i].pos, m_transformations[i].rot);
				int cmps_count;
				EntityGUID guid;
				blob.read(guid.value);
				m_editor.m_entity_map.insert(guid, new_entity);
				blob.read(cmps_count);
				for (int j = 0; j < cmps_count; ++j)
				{
					ComponentType cmp_type;
					blob.read(cmp_type);
					ComponentUID new_component;
					IScene* scene = universe->getScene(cmp_type);
					ASSERT(scene);
					new_component.handle = scene->createComponent(cmp_type, new_entity);
					new_component.entity = new_entity;
					new_component.scene = scene;
					new_component.type = cmp_type;

					Array<IPropertyDescriptor*>& props = PropertyRegister::getDescriptors(cmp_type);

					for (int k = 0; k < props.size(); ++k)
					{
						props[k]->set(new_component, -1, blob);
					}
				}
				u64 tpl;
				blob.read(tpl);
				if (tpl) m_editor.getPrefabSystem().setPrefab(new_entity, tpl);
			}
		}


		const char* getType() override { return "destroy_entities"; }


	private:
		WorldEditorImpl& m_editor;
		Array<Entity> m_entities;
		Array<Transform> m_transformations;
		OutputBlob m_old_values;
		Array<Resource*> m_resources;
	};


	class DestroyComponentCommand LUMIX_FINAL : public IEditorCommand
	{
	public:
		explicit DestroyComponentCommand(WorldEditor& editor)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
			, m_old_values(editor.getAllocator())
			, m_entities(editor.getAllocator())
			, m_cmp_type(INVALID_COMPONENT_TYPE)
			, m_resources(editor.getAllocator())
		{
		}


		DestroyComponentCommand(WorldEditorImpl& editor, const Entity* entities, int count, ComponentType cmp_type)
			: m_cmp_type(cmp_type)
			, m_editor(editor)
			, m_old_values(editor.getAllocator())
			, m_entities(editor.getAllocator())
			, m_resources(editor.getAllocator())
		{
			m_entities.reserve(count);
			for (int i = 0; i < count; ++i)
			{
				if (!m_editor.getUniverse()->getComponent(entities[i], m_cmp_type).isValid()) continue;
				u64 prefab = editor.getPrefabSystem().getPrefab(entities[i]);
				if (prefab == 0)
				{
					m_entities.push(entities[i]);
				}
				else
				{
					Entity instance = editor.getPrefabSystem().getFirstInstance(prefab);
					while(isValid(instance))
					{
						m_entities.push(instance);
						instance = editor.getPrefabSystem().getNextInstance(instance);
					}
				}
			}
		}


		~DestroyComponentCommand()
		{
			for (Resource* resource : m_resources)
			{
				resource->getResourceManager().unload(*resource);
			}
		}


		void serialize(JsonSerializer& serializer) override 
		{
			serializer.beginArray("entities");
			for (Entity entity : m_entities)
			{
				serializer.serializeArrayItem(entity);
			}
			serializer.endArray();
			serializer.serialize("component_type", PropertyRegister::getComponentTypeHash(m_cmp_type));
		}


		void deserialize(JsonSerializer& serializer) override
		{
			serializer.deserializeArrayBegin("entities");
			while (!serializer.isArrayEnd())
			{
				Entity entity;
				serializer.deserializeArrayItem(entity, INVALID_ENTITY);
				m_entities.push(entity);
			}
			serializer.deserializeArrayEnd();

			u32 hash;
			serializer.deserialize("component_type", hash, 0);
			m_cmp_type = PropertyRegister::getComponentTypeFromHash(hash);
		}


		void undo() override
		{
			ComponentUID cmp;
			cmp.scene = m_editor.getUniverse()->getScene(m_cmp_type);
			cmp.type = m_cmp_type;
			ASSERT(cmp.scene);
			InputBlob blob(m_old_values);
			const Array<IPropertyDescriptor*>& props = PropertyRegister::getDescriptors(cmp.type);
			for (Entity entity : m_entities)
			{
				cmp.entity = entity;
				cmp.handle = cmp.scene->createComponent(cmp.type, cmp.entity);
				for (int i = 0; i < props.size(); ++i)
				{
					props[i]->set(cmp, -1, blob);
				}
			}
		}


		bool merge(IEditorCommand&) override { return false; }


		const char* getType() override { return "destroy_components"; }


		bool execute() override
		{
			Array<IPropertyDescriptor*>& props = PropertyRegister::getDescriptors(m_cmp_type);
			ComponentUID cmp;
			cmp.type = m_cmp_type;
			cmp.scene = m_editor.getUniverse()->getScene(m_cmp_type);
			if (m_entities.empty()) return false;
			if (!cmp.scene) return false;
			ResourceManager& resource_manager = m_editor.getEngine().getResourceManager();

			for (Entity entity : m_entities)
			{
				cmp.entity = entity;
				cmp.handle = cmp.scene->getComponent(entity, m_cmp_type);
				for (int i = 0; i < props.size(); ++i)
				{
					props[i]->get(cmp, -1, m_old_values);
					if (props[i]->getType() == IPropertyDescriptor::RESOURCE)
					{
						auto* res_prop = static_cast<IResourcePropertyDescriptor*>(props[i]);
						OutputBlob tmp(m_editor.getAllocator());
						props[i]->get(cmp, -1, tmp);
						ResourceType resource_type = res_prop->getResourceType();
						Path path((const char*)tmp.getData());
						Resource* resource = resource_manager.get(resource_type)->load(path);
						m_resources.push(resource);
					}
				}
				cmp.scene->destroyComponent(cmp.handle, m_cmp_type);
			}
			return true;
		}

	private:
		Array<Entity> m_entities;
		ComponentType m_cmp_type;
		WorldEditorImpl& m_editor;
		OutputBlob m_old_values;
		Array<Resource*> m_resources;
	};


	class AddEntityCommand LUMIX_FINAL : public IEditorCommand
	{
	public:
		explicit AddEntityCommand(WorldEditor& editor)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
		{
			m_entity = INVALID_ENTITY;
		}


		AddEntityCommand(WorldEditorImpl& editor, const Vec3& position)
			: m_editor(editor)
			, m_position(position)
		{
			m_entity = INVALID_ENTITY;
		}


		bool execute() override
		{
			if (!isValid(m_entity))
			{
				m_entity = m_editor.getUniverse()->createEntity(m_position, Quat(0, 0, 0, 1));
			}
			else
			{
				m_editor.getUniverse()->createEntity(m_entity);
				m_editor.getUniverse()->setPosition(m_entity, m_position);
			}
			((WorldEditorImpl&)m_editor).m_entity_map.create(m_entity);
			m_editor.selectEntities(&m_entity, 1);
			return true;
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("pos_x", m_position.x);
			serializer.serialize("pos_y", m_position.y);
			serializer.serialize("pos_z", m_position.z);
		}


		void deserialize(JsonSerializer& serializer) override
		{
			serializer.deserialize("pos_x", m_position.x, 0);
			serializer.deserialize("pos_y", m_position.y, 0);
			serializer.deserialize("pos_z", m_position.z, 0);
		}


		void undo() override
		{
			m_editor.getUniverse()->destroyEntity(m_entity);
			m_editor.m_entity_map.erase(m_entity);
		}


		bool merge(IEditorCommand&) override { return false; }


		const char* getType() override { return "add_entity"; }


		Entity getEntity() const { return m_entity; }


	private:
		WorldEditorImpl& m_editor;
		Entity m_entity;
		Vec3 m_position;
	};

public:
	IAllocator& getAllocator() override { return m_allocator; }


	Universe* getUniverse() override
	{
		return m_universe; 
	}


	Engine& getEngine() override { return *m_engine; }


	void showGizmos()
	{
		if (m_selected_entities.empty()) return;

		ComponentUID camera_cmp = getUniverse()->getComponent(m_camera, CAMERA_TYPE);
		Universe* universe = getUniverse();

		if (m_selected_entities.size() > 1)
		{
			AABB aabb = m_render_interface->getEntityAABB(*universe, m_selected_entities[0]);
			for (int i = 1; i < m_selected_entities.size(); ++i)
			{
				AABB entity_aabb = m_render_interface->getEntityAABB(*universe, m_selected_entities[i]);
				aabb.merge(entity_aabb);
			}

			m_render_interface->addDebugCube(aabb.min, aabb.max, 0xffffff00, 0);
			return;
		}

		for (ComponentUID cmp = universe->getFirstComponent(m_selected_entities[0]);
			cmp.isValid();
			cmp = universe->getNextComponent(cmp))
		{
			for (auto* plugin : m_plugins)
			{
				if (plugin->showGizmo(cmp)) break;
			}
		}
	}


	void createEditorLines()
	{
		PROFILE_FUNCTION();
		showGizmos();
		m_measure_tool->createEditorLines(*m_render_interface);
	}


	void updateGoTo()
	{
		if (!isValid(m_camera) || !m_go_to_parameters.m_is_active) return;

		float t = Math::easeInOut(m_go_to_parameters.m_t);
		m_go_to_parameters.m_t += m_engine->getLastTimeDelta() * m_go_to_parameters.m_speed;
		Vec3 pos = m_go_to_parameters.m_from * (1 - t) + m_go_to_parameters.m_to * t;
		Quat rot;
		nlerp(m_go_to_parameters.m_from_rot, m_go_to_parameters.m_to_rot, &rot, t);
		if (m_go_to_parameters.m_t >= 1)
		{
			pos = m_go_to_parameters.m_to;
			m_go_to_parameters.m_is_active = false;
		}
		getUniverse()->setPosition(m_camera, pos);
		getUniverse()->setRotation(m_camera, rot);
	}


	void update() override
	{
		PROFILE_FUNCTION();
		updateGoTo();

		m_mouse_rel_x = m_mouse_rel_y = 0;
		if (!m_selected_entities.empty())
		{
			m_gizmo->add(m_selected_entities[0]);
		}

		createEditorLines();
		for (auto& i : m_is_mouse_click) i = false;
	}


	void updateEngine() override
	{
		ASSERT(m_universe);
		m_engine->update(*m_universe);
	}


	~WorldEditorImpl()
	{
		Gizmo::destroy(*m_gizmo);

		removePlugin(*m_measure_tool);
		LUMIX_DELETE(m_allocator, m_measure_tool);
		for (auto* plugin : m_plugins)
		{
			LUMIX_DELETE(getAllocator(), plugin);
		}
		destroyUndoStack();

		destroyUniverse();
		EditorIcons::destroy(*m_editor_icons);
		PrefabSystem::destroy(m_prefab_system);

		LUMIX_DELETE(m_allocator, m_render_interface);
	}


	bool isMouseClick(MouseButton::Value button) const override
	{
		return m_is_mouse_click[button];
	}


	bool isMouseDown(MouseButton::Value button) const override
	{
		return m_is_mouse_down[button];
	}


	void snapEntities(const Vec3& hit_pos)
	{
		Array<Vec3> positions(m_allocator);
		Array<Quat> rotations(m_allocator);
		if(m_gizmo->isTranslateMode())
		{
			for(auto e : m_selected_entities)
			{
				positions.push(hit_pos);
				rotations.push(m_universe->getRotation(e));
			}
		}
		else
		{
			for(auto e : m_selected_entities)
			{
				auto pos = m_universe->getPosition(e);
				auto dir = pos - hit_pos;
				dir.normalize();
				Matrix mtx = Matrix::IDENTITY;
				Vec3 y(0, 1, 0);
				if(dotProduct(y, dir) > 0.99f)
				{
					y.set(1, 0, 0);
				}
				Vec3 x = crossProduct(y, dir);
				x.normalize();
				y = crossProduct(dir, x);
				y.normalize();
				mtx.setXVector(x);
				mtx.setYVector(y);
				mtx.setZVector(dir);

				positions.push(pos);
				rotations.emplace(mtx.getRotation());
			}
		}
		MoveEntityCommand* cmd = LUMIX_NEW(m_allocator, MoveEntityCommand)(*this,
			&m_selected_entities[0],
			&positions[0],
			&rotations[0],
			positions.size(),
			m_allocator);
		executeCommand(cmd);
	}


	void onMouseDown(int x, int y, MouseButton::Value button) override
	{
		m_is_mouse_click[button] = true;
		m_is_mouse_down[button] = true;
		if(button == MouseButton::MIDDLE)
		{
			m_mouse_mode = MouseMode::PAN;
		}
		else if (button == MouseButton::RIGHT)
		{
			m_mouse_mode = MouseMode::NAVIGATE;
		}
		else if (button == MouseButton::LEFT)
		{
			Vec3 origin, dir;
			ComponentUID camera_cmp = getUniverse()->getComponent(m_camera, CAMERA_TYPE);
			if (!camera_cmp.isValid()) return;

			m_render_interface->getRay(camera_cmp.handle, (float)x, (float)y, origin, dir);
			auto hit = m_render_interface->castRay(origin, dir, INVALID_COMPONENT);
			if (m_gizmo->isActive()) return;

			if(m_is_snap_mode && !m_selected_entities.empty() && hit.is_hit)
			{
				snapEntities(origin + dir * hit.t);
				return;
			}

			auto icon_hit = m_editor_icons->raycast(origin, dir);
			if (icon_hit.entity != INVALID_ENTITY)
			{
				Entity e = icon_hit.entity;
				if (m_is_additive_selection)
				{
					addEntitiesToSelection(&e, 1);
				}
				else
				{
					selectEntities(&e, 1);
				}
			}
			else if (hit.is_hit)
			{
				onEntityMouseDown(hit, x, y);
			}
		}
	}


	void addPlugin(Plugin& plugin) override { m_plugins.push(&plugin); }


	void removePlugin(Plugin& plugin) override
	{
		m_plugins.eraseItemFast(&plugin);
	}


	void onEntityMouseDown(const RayHit& hit, int x, int y)
	{
		Entity entity = hit.entity;
		for (int i = 0; i < m_plugins.size(); ++i)
		{
			if (m_plugins[i]->onEntityMouseDown(hit, x, y))
			{
				m_mouse_handling_plugin = m_plugins[i];
				m_mouse_mode = MouseMode::CUSTOM;
				return;
			}
		}
		if (m_is_additive_selection)
		{
			addEntitiesToSelection(&entity, 1);
		}
		else
		{
			selectEntities(&entity, 1);
		}
	}


	void onMouseMove(int x, int y, int relx, int rely) override
	{
		PROFILE_FUNCTION();
		m_mouse_x = (float)x;
		m_mouse_y = (float)y;
		m_mouse_rel_x = (float)relx;
		m_mouse_rel_y = (float)rely;

		static const float MOUSE_MULTIPLIER = 1 / 200.0f;

		switch (m_mouse_mode)
		{
			case MouseMode::CUSTOM:
			{
				if (m_mouse_handling_plugin)
				{
					m_mouse_handling_plugin->onMouseMove(x, y, relx, rely);
				}
			}
			break;
			case MouseMode::NAVIGATE: rotateCamera(relx, rely); break;
			case MouseMode::PAN: panCamera(relx * MOUSE_MULTIPLIER, rely * MOUSE_MULTIPLIER); break;
			case MouseMode::NONE:
			case MouseMode::SELECT:
				break;
		}
	}


	void onMouseUp(int x, int y, MouseButton::Value button) override
	{
		m_is_mouse_down[button] = false;
		if (m_mouse_handling_plugin)
		{
			m_mouse_handling_plugin->onMouseUp(x, y, button);
			m_mouse_handling_plugin = nullptr;
		}
		m_mouse_mode = MouseMode::NONE;
	}


	float getMouseX() const override { return m_mouse_x; }
	float getMouseY() const override { return m_mouse_y; }
	float getMouseRelX() const override { return m_mouse_rel_x; }
	float getMouseRelY() const override { return m_mouse_rel_y; }


	bool isUniverseChanged() const override { return m_is_universe_changed; }


	void saveUniverse(const Path& path, bool save_path) override
	{
		g_log_info.log("Editor") << "Saving universe " << path << "...";
		FS::FileSystem& fs = m_engine->getFileSystem();
		char bkp_path[MAX_PATH_LENGTH];
		copyString(bkp_path, path.c_str());
		catString(bkp_path, ".bkp");
		copyFile(path.c_str(), bkp_path);
		FS::IFile* file = fs.open(fs.getDefaultDevice(), path, FS::Mode::CREATE_AND_WRITE);
		if (!file)
		{
			g_log_error.log("Editor") << "Could not create/open " << path.c_str();
			return;
		}
		save(*file);
		m_is_universe_changed = false;
		fs.close(*file);
		
		serialize(path);

		if (save_path) m_universe->setPath(path);
	}


	struct EntityGUIDMap : public IEntityGUIDMap
	{
		EntityGUIDMap(IAllocator& allocator)
			: guid_to_entity(allocator)
			, entity_to_guid(allocator)
		{
		}


		void clear()
		{
			entity_to_guid.clear();
			guid_to_entity.clear();
		}


		void create(Entity entity)
		{
			ASSERT(isValid(entity));
			EntityGUID guid = { Math::randGUID() };
			insert(guid, entity);
		}


		void erase(Entity entity)
		{
			EntityGUID guid = entity_to_guid[entity.index];
			if (!isValid(guid)) return;
			entity_to_guid[entity.index] = INVALID_ENTITY_GUID;
			guid_to_entity.erase(guid.value);
		}


		void insert(EntityGUID guid, Entity entity)
		{
			guid_to_entity.insert(guid.value, entity);
			while (entity.index >= entity_to_guid.size())
			{
				entity_to_guid.push(INVALID_ENTITY_GUID);
			}
			entity_to_guid[entity.index] = guid;
		}


		Entity get(EntityGUID guid) override
		{
			auto iter = guid_to_entity.find(guid.value);
			if (iter.isValid()) return iter.value();
			return INVALID_ENTITY;
		}


		EntityGUID get(Entity entity) override
		{
			if (!isValid(entity)) return INVALID_ENTITY_GUID;
			return entity_to_guid[entity.index];
		}


		bool has(EntityGUID guid) const
		{
			auto iter = guid_to_entity.find(guid.value);
			return iter.isValid();
		}


		HashMap<u64, Entity> guid_to_entity;
		Array<EntityGUID> entity_to_guid;
		u64 last_guid = 9999;
	};


	void deserialize(const Path& path)
	{
		PROFILE_FUNCTION();
		if (isValid(m_camera)) m_universe->destroyEntity(m_camera);
		m_entity_map.clear();
		PathUtils::FileInfo file_info(path.c_str());
		StaticString<MAX_PATH_LENGTH> scn_dir(file_info.m_dir, file_info.m_basename, "/scenes/");
		auto scn_file_iter = PlatformInterface::createFileIterator(scn_dir, m_allocator);
		Array<u8> data(m_allocator);
		FS::OsFile file;
		auto loadFile = [&file, &data, this](const char* filepath, auto x) {
			if (file.open(filepath, FS::Mode::OPEN_AND_READ, m_allocator))
			{
				if (file.size() > 0)
				{
					data.resize((int)file.size());
					file.read(&data[0], data.size());
					InputBlob blob(&data[0], data.size());
					TextDeserializer deserializer(blob, m_entity_map);
					x(deserializer);
				}
				file.close();
			}
		};
		PlatformInterface::FileInfo info;
		while (PlatformInterface::getNextFile(scn_file_iter, &info))
		{
			StaticString<MAX_PATH_LENGTH> filepath(scn_dir, info.filename);
			loadFile(filepath, [&filepath, this](TextDeserializer& deserializer) {
				char plugin_name[64];
				PathUtils::getBasename(plugin_name, lengthOf(plugin_name), filepath);
				IScene* scene = m_universe->getScene(crc32(plugin_name));
				if (scene)
				{
					scene->deserialize(deserializer);
				}
				else
				{
					g_log_error.log("Editor") << "Could not open " << filepath << " since there is not plugin " << plugin_name;
				}
			});
		}
		PlatformInterface::destroyFileIterator(scn_file_iter);
		
		StaticString<MAX_PATH_LENGTH> dir(file_info.m_dir, file_info.m_basename, "/");
		auto file_iter = PlatformInterface::createFileIterator(dir, m_allocator);
		while (PlatformInterface::getNextFile(file_iter, &info))
		{
			if (info.filename[0] == '.') continue;
			if (info.is_directory) continue;
			FS::OsFile file;
			StaticString<MAX_PATH_LENGTH> filepath(dir, info.filename);
			char tmp[20];
			PathUtils::getBasename(tmp, lengthOf(tmp), filepath);
			EntityGUID guid;
			fromCString(tmp, lengthOf(tmp), &guid.value);
			Entity entity = m_universe->createEntity({0, 0, 0}, {0, 0, 0, 1});
			m_entity_map.insert(guid, entity);
		}
		PlatformInterface::destroyFileIterator(file_iter);
		
		file_iter = PlatformInterface::createFileIterator(dir, m_allocator);
		while (PlatformInterface::getNextFile(file_iter, &info))
		{
			FS::OsFile file;
			StaticString<MAX_PATH_LENGTH> filepath(dir, info.filename);
			char tmp[20];
			PathUtils::getBasename(tmp, lengthOf(tmp), filepath);
			EntityGUID guid;
			fromCString(tmp, lengthOf(tmp), &guid.value);
			loadFile(filepath, [this, guid](TextDeserializer& deserializer) {
				char name[64];
				deserializer.read(name, lengthOf(name));
				Transform tr;
				deserializer.read(&tr);
				float scale;
				deserializer.read(&scale);
				Entity entity = m_entity_map.get(guid);
				m_universe->setTransform(entity, tr);
				if(name[0]) m_universe->setEntityName(entity, name);
				m_universe->setScale(entity, scale);
				m_universe->setTransform(entity, tr);
				u32 cmp_type;
				deserializer.read(&cmp_type);
				while (cmp_type != 0)
				{
					m_universe->deserializeComponent(deserializer, entity, PropertyRegister::getComponentTypeFromHash(cmp_type));
					deserializer.read(&cmp_type);
				}
			});
		}
		PlatformInterface::destroyFileIterator(file_iter);

		StaticString<MAX_PATH_LENGTH> filepath(file_info.m_dir, file_info.m_basename, "/systems/templates.sys");
		loadFile(filepath, [this](TextDeserializer& deserializer) {
			m_prefab_system->deserialize(deserializer);
			for (int i = 0, c = m_prefab_system->getMaxEntityIndex(); i < c; ++i)
			{
				u64 prefab = m_prefab_system->getPrefab({i});
				if (prefab != 0) m_entity_map.create({i});
			}
		});
		m_camera = m_render_interface->getCameraEntity(m_render_interface->getCameraInSlot("editor"));
	}

	
	void serialize(const Path& path)
	{
		PathUtils::FileInfo file_info(path.c_str());
		StaticString<MAX_PATH_LENGTH> dir(file_info.m_dir, file_info.m_basename, "/");
		PlatformInterface::makePath(dir);
		PlatformInterface::makePath(dir + "probes/");
		PlatformInterface::makePath(dir + "scenes/");
		PlatformInterface::makePath(dir + "systems/");

		FS::OsFile file;
		OutputBlob blob(m_allocator);
		TextSerializer serializer(blob, m_entity_map);
		auto saveFile = [&file, this, &blob](const char* path) {
			if (file.open(path, FS::Mode::CREATE_AND_WRITE, m_allocator))
			{
				file.write(blob.getData(), blob.getPos());
				file.close();
			}
		};
		for (IScene* scene : m_universe->getScenes())
		{
			blob.clear();
			scene->serialize(serializer);
			StaticString<MAX_PATH_LENGTH> scene_file_path(dir, "scenes/", scene->getPlugin().getName(), ".scn");
			saveFile(scene_file_path);
		}

		blob.clear();
		m_prefab_system->serialize(serializer);
		StaticString<MAX_PATH_LENGTH> system_file_path(dir, "systems/templates.sys");
		saveFile(system_file_path);

		for (Entity entity = m_universe->getFirstEntity(); isValid(entity); entity = m_universe->getNextEntity(entity))
		{
			if (m_prefab_system->getPrefab(entity) != 0) continue;
			blob.clear();
			serializer.write("name", m_universe->getEntityName(entity));
			serializer.write("transform", m_universe->getTransform(entity));
			serializer.write("scale", m_universe->getScale(entity));
			EntityGUID guid = m_entity_map.get(entity);
			StaticString<MAX_PATH_LENGTH> entity_file_path(dir, guid.value, ".ent");
			for (ComponentUID cmp = m_universe->getFirstComponent(entity); isValid(cmp.handle);
				 cmp = m_universe->getNextComponent(cmp))
			{
				const char* cmp_name = PropertyRegister::getComponentTypeID(cmp.type.index);
				u32 type_hash = PropertyRegister::getComponentTypeHash(cmp.type);
				serializer.write(cmp_name, type_hash);
				m_universe->serializeComponent(serializer, cmp.type, cmp.handle);
			}
			serializer.write("cmp_end", (u32)0);
			saveFile(entity_file_path);
		}
		clearUniverseDir(dir);
	}


	void clearUniverseDir(const char* dir)
	{
		PlatformInterface::FileInfo file_info;
		auto file_iter = PlatformInterface::createFileIterator(dir, m_allocator);
		while (PlatformInterface::getNextFile(file_iter, &file_info))
		{
			char basename[64];
			PathUtils::getBasename(basename, lengthOf(basename), file_info.filename);
			EntityGUID guid;
			fromCString(basename, lengthOf(basename), &guid.value);
			if (!m_entity_map.has(guid))
			{
				StaticString<MAX_PATH_LENGTH> filepath(dir, file_info.filename);
				PlatformInterface::deleteFile(filepath);
			}
		}
		PlatformInterface::destroyFileIterator(file_iter);
	}


	void save(FS::IFile& file)
	{
		while (m_engine->getFileSystem().hasWork()) m_engine->getFileSystem().updateAsyncTransactions();

		ASSERT(m_universe);

		OutputBlob blob(m_allocator);
		blob.reserve(64 * 1024);

		Header header = {0xffffFFFF, (int)SerializedVersion::LATEST, 0, 0};
		blob.write(header);
		int hashed_offset = sizeof(header);

		header.engine_hash = m_engine->serialize(*m_universe, blob);
		m_entity_groups.serialize(blob);
		m_prefab_system->serialize(blob);
		header.hash = crc32((const u8*)blob.getData() + hashed_offset, blob.getPos() - hashed_offset);
		*(Header*)blob.getData() = header;

		g_log_info.log("editor") << "Universe saved";
		file.write(blob.getData(), blob.getPos());
	}


	void setRenderInterface(class RenderInterface* interface) override
	{
		m_render_interface = interface;
		m_editor_icons->setRenderInterface(m_render_interface);
		createUniverse();
	}


	RenderInterface* getRenderInterface() override
	{
		return m_render_interface;
	}


	void snapDown() override
	{
		if (m_selected_entities.empty()) return;

		Array<Vec3> new_positions(m_allocator);
		Universe* universe = getUniverse();

		for (int i = 0; i < m_selected_entities.size(); ++i)
		{
			Entity entity = m_selected_entities[i];

			ComponentUID model_instance = getUniverse()->getComponent(m_selected_entities[i], MODEL_INSTANCE_TYPE);
			Vec3 origin = universe->getPosition(entity);
			auto hit = m_render_interface->castRay(origin, Vec3(0, -1, 0), model_instance.handle);
			if (hit.is_hit)
			{
				new_positions.push(origin + Vec3(0, -hit.t, 0));
			}
			else
			{
				hit = m_render_interface->castRay(origin, Vec3(0, 1, 0), model_instance.handle);
				if (hit.is_hit)
				{
					new_positions.push(origin + Vec3(0, hit.t, 0));
				}
				else
				{
					new_positions.push(universe->getPosition(m_selected_entities[i]));
				}
			}
		}
		setEntitiesPositions(&m_selected_entities[0], &new_positions[0], new_positions.size());
	}


	void destroyEntities(const Entity* entities, int count) override
	{
		for (int i = 0; i < count; ++i)
		{
			if (m_camera == entities[i])
			{
				g_log_warning.log("Editor") << "Can not destroy editor camera.";
				return;
			}
		}

		DestroyEntitiesCommand* command =
			LUMIX_NEW(m_allocator, DestroyEntitiesCommand)(*this, entities, count);
		executeCommand(command);
	}


	void createEntityGUID(Entity entity) override
	{
		m_entity_map.create(entity);
	}


	void destroyEntityGUID(Entity entity) override
	{
		m_entity_map.erase(entity);
	}


	EntityGUID getEntityGUID(Entity entity) override
	{
		return m_entity_map.get(entity);
	}


	Entity addEntity() override
	{
		ComponentUID cmp = getUniverse()->getComponent(m_camera, CAMERA_TYPE);
		Vec2 size = m_render_interface->getCameraScreenSize(cmp.handle);
		return addEntityAt((int)size.x >> 1, (int)size.y >> 1);
	}


	Entity addEntityAt(int camera_x, int camera_y) override
	{
		ComponentUID camera_cmp = getUniverse()->getComponent(m_camera, CAMERA_TYPE);
		Universe* universe = getUniverse();
		Vec3 origin;
		Vec3 dir;

		m_render_interface->getRay(camera_cmp.handle, (float)camera_x, (float)camera_y, origin, dir);
		auto hit = m_render_interface->castRay(origin, dir, INVALID_COMPONENT);
		Vec3 pos;
		if (hit.is_hit)
		{
			pos = origin + dir * hit.t;
		}
		else
		{
			pos = universe->getPosition(m_camera) + universe->getRotation(m_camera).rotate(Vec3(0, 0, -2));
		}
		AddEntityCommand* command = LUMIX_NEW(m_allocator, AddEntityCommand)(*this, pos);
		executeCommand(command);

		return command->getEntity();
	}


	Vec3 getCameraRaycastHit() override
	{
		ComponentUID camera_cmp = getUniverse()->getComponent(m_camera, CAMERA_TYPE);
		Universe* universe = getUniverse();
		Vec2 screen_size = m_render_interface->getCameraScreenSize(camera_cmp.handle);
		screen_size *= 0.5f;

		Vec3 origin;
		Vec3 dir;
		m_render_interface->getRay(camera_cmp.handle, (float)screen_size.x, (float)screen_size.y, origin, dir);
		auto hit = m_render_interface->castRay(origin, dir, INVALID_COMPONENT);
		Vec3 pos;
		if (hit.is_hit)
		{
			pos = origin + dir * hit.t;
		}
		else
		{
			pos = universe->getPosition(m_camera) + universe->getRotation(m_camera).rotate(Vec3(0, 0, -2));
		}
		return pos;
	}


	void setEntitiesScale(const Entity* entities, int count, float scale) override
	{
		if (count <= 0) return;

		IEditorCommand* command =
			LUMIX_NEW(m_allocator, ScaleEntityCommand)(*this, entities, count, scale, m_allocator);
		executeCommand(command);
	}


	void setEntitiesRotations(const Entity* entities, const Quat* rotations, int count) override
	{
		ASSERT(entities && rotations);
		if (count <= 0) return;

		Universe* universe = getUniverse();
		Array<Vec3> positions(m_allocator);
		for (int i = 0; i < count; ++i)
		{
			positions.push(universe->getPosition(entities[i]));
		}
		IEditorCommand* command =
			LUMIX_NEW(m_allocator, MoveEntityCommand)(*this, entities, &positions[0], rotations, count, m_allocator);
		executeCommand(command);
	}


	void setEntitiesCoordinate(const Entity* entities, int count, float value, Coordinate coord) override
	{
		ASSERT(entities);
		if (count <= 0) return;

		Universe* universe = getUniverse();
		Array<Quat> rots(m_allocator);
		Array<Vec3> poss(m_allocator);
		rots.reserve(count);
		poss.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			rots.push(universe->getRotation(entities[i]));
			poss.push(universe->getPosition(entities[i]));
			(&poss[i].x)[(int)coord] = value;
		}
		IEditorCommand* command =
			LUMIX_NEW(m_allocator, MoveEntityCommand)(*this, entities, &poss[0], &rots[0], count, m_allocator);
		executeCommand(command);
	}


	void setEntitiesPositions(const Entity* entities, const Vec3* positions, int count) override
	{
		ASSERT(entities && positions);
		if (count <= 0) return;

		Universe* universe = getUniverse();
		Array<Quat> rots(m_allocator);
		for (int i = 0; i < count; ++i)
		{
			rots.push(universe->getRotation(entities[i]));
		}
		IEditorCommand* command =
			LUMIX_NEW(m_allocator, MoveEntityCommand)(*this, entities, positions, &rots[0], count, m_allocator);
		executeCommand(command);
	}


	void setEntitiesPositionsAndRotations(const Entity* entities,
		const Vec3* positions,
		const Quat* rotations,
		int count) override
	{
		if (count <= 0) return;
		IEditorCommand* command =
			LUMIX_NEW(m_allocator, MoveEntityCommand)(*this, entities, positions, rotations, count, m_allocator);
		executeCommand(command);
	}


	void setEntityName(Entity entity, const char* name) override
	{
		if (isValid(entity))
		{
			IEditorCommand* command = LUMIX_NEW(m_allocator, SetEntityNameCommand)(*this, entity, name);
			executeCommand(command);
		}
	}


	void beginCommandGroup(u32 type) override
	{
		if(m_undo_index < m_undo_stack.size() - 1)
		{
			for(int i = m_undo_stack.size() - 1; i > m_undo_index; --i)
			{
				LUMIX_DELETE(m_allocator, m_undo_stack[i]);
			}
			m_undo_stack.resize(m_undo_index + 1);
		}

		if(m_undo_index >= 0)
		{
			static const u32 end_group_hash = crc32("end_group");
			if(crc32(m_undo_stack[m_undo_index]->getType()) == end_group_hash)
			{
				if(static_cast<EndGroupCommand*>(m_undo_stack[m_undo_index])->group_type == type)
				{
					LUMIX_DELETE(m_allocator, m_undo_stack[m_undo_index]);
					--m_undo_index;
					m_undo_stack.pop();
					return;
				}
			}
		}

		m_current_group_type = type;
		auto* cmd = LUMIX_NEW(m_allocator, BeginGroupCommand);
		m_undo_stack.push(cmd);
		++m_undo_index;
	}


	void endCommandGroup() override
	{
		if (m_undo_index < m_undo_stack.size() - 1)
		{
			for (int i = m_undo_stack.size() - 1; i > m_undo_index; --i)
			{
				LUMIX_DELETE(m_allocator, m_undo_stack[i]);
			}
			m_undo_stack.resize(m_undo_index + 1);
		}

		auto* cmd = LUMIX_NEW(m_allocator, EndGroupCommand);
		cmd->group_type = m_current_group_type;
		m_undo_stack.push(cmd);
		++m_undo_index;
	}


	IEditorCommand* executeCommand(IEditorCommand* command) override
	{
		if (m_is_game_mode)
		{
			command->execute();
			LUMIX_DELETE(m_allocator, command);
			return nullptr;
		}

		m_is_universe_changed = true;
		if (m_undo_index >= 0 && command->getType() == m_undo_stack[m_undo_index]->getType())
		{
			if (command->merge(*m_undo_stack[m_undo_index]))
			{
				m_undo_stack[m_undo_index]->execute();
				LUMIX_DELETE(m_allocator, command);
				return nullptr;
			}
		}

		if (command->execute())
		{
			if (m_undo_index < m_undo_stack.size() - 1)
			{
				for (int i = m_undo_stack.size() - 1; i > m_undo_index; --i)
				{
					LUMIX_DELETE(m_allocator, m_undo_stack[i]);
				}
				m_undo_stack.resize(m_undo_index + 1);
			}
			m_undo_stack.push(command);
			++m_undo_index;
			return command;
		}
		LUMIX_DELETE(m_allocator, command);
		return nullptr;
	}


	bool isGameMode() const override { return m_is_game_mode; }


	void toggleGameMode() override
	{
		ASSERT(m_universe);
		if (m_is_game_mode)
		{
			stopGameMode(true);
		}
		else
		{
			m_selected_entity_on_game_mode = m_selected_entities.empty() ? INVALID_ENTITY : m_selected_entities[0];
			auto& fs = m_engine->getFileSystem();
			m_game_mode_file = fs.open(fs.getMemoryDevice(), Path(""), FS::Mode::WRITE);
			save(*m_game_mode_file);
			m_is_game_mode = true;
			m_engine->startGame(*m_universe);
		}
	}


	void stopGameMode(bool reload)
	{
		ASSERT(m_universe);
		m_engine->getResourceManager().enableUnload(false);
		m_engine->stopGame(*m_universe);
		selectEntities(nullptr, 0);
		m_gizmo->clearEntities();
		m_editor_icons->clear();
		m_is_game_mode = false;
		if (reload)
		{
			m_universe_destroyed.invoke();
			m_game_mode_file->seek(FS::SeekMode::BEGIN, 0);
			m_entity_groups.setUniverse(nullptr);
			Path path = m_universe->getPath();
			m_engine->destroyUniverse(*m_universe);
			
			m_universe = &m_engine->createUniverse(true);
			m_universe_created.invoke();
			m_universe->setPath(path);
			m_universe->entityDestroyed().bind<WorldEditorImpl, &WorldEditorImpl::onEntityDestroyed>(this);
			m_selected_entities.clear();
			m_entity_groups.setUniverse(m_universe);
			m_camera = INVALID_ENTITY;
			load(*m_game_mode_file);
		}
		m_engine->getFileSystem().close(*m_game_mode_file);
		m_game_mode_file = nullptr;
		if(isValid(m_selected_entity_on_game_mode)) selectEntities(&m_selected_entity_on_game_mode, 1);
		m_engine->getResourceManager().enableUnload(true);
	}


	PrefabSystem& getPrefabSystem() override
	{
		return *m_prefab_system;
	}


	void showEntities(const Entity* entities, int count) override
	{
		for (int i = 0, c = count; i < c; ++i)
		{
			m_render_interface->showEntity(entities[i]);
		}
	}


	void showSelectedEntities() override
	{
		for (auto entity : m_selected_entities)
		{
			m_render_interface->showEntity(entity);
		}
	}


	void hideEntities(const Entity* entities, int count) override
	{
		for (int i = 0, c = count; i < c; ++i)
		{
			m_render_interface->hideEntity(entities[i]);
		}
	}


	void hideSelectedEntities() override
	{
		for (auto entity : m_selected_entities)
		{
			m_render_interface->hideEntity(entity);
		}
	}


	void copyEntities(const Entity* entities, int count, OutputBlob& blob) override
	{
		blob.write(count);
		for (int i = 0; i < count; ++i)
		{
			Entity entity = entities[i];
			auto mtx = m_universe->getMatrix(entity);
			blob.write(mtx);

			i32 cmp_count = 0;
			for (ComponentUID cmp = m_universe->getFirstComponent(entity); cmp.isValid();
				 cmp = m_universe->getNextComponent(cmp))
			{
				++cmp_count;
			}

			blob.write(cmp_count);
			for (ComponentUID cmp = m_universe->getFirstComponent(entity);
				cmp.isValid();
				cmp = m_universe->getNextComponent(cmp))
			{
				u32 cmp_type = PropertyRegister::getComponentTypeHash(cmp.type);
				blob.write(cmp_type);
				Array<IPropertyDescriptor*>& props = PropertyRegister::getDescriptors(cmp.type);
				i32 prop_count = props.size();
				blob.write(prop_count);
				for (int j = 0; j < prop_count; ++j)
				{
					blob.write(props[j]->getNameHash());
					i32 size = 0;
					blob.write(size);
					int pos = blob.getPos();
					props[j]->get(cmp, -1, blob);
					size = blob.getPos() - pos;
					*(i32*)((u8*)blob.getData() + pos - 4) = size;
				}
			}
		}
	}


	void copyEntities() override
	{
		if (m_selected_entities.empty()) return;

		m_copy_buffer.clear();
		copyEntities(&m_selected_entities[0], m_selected_entities.size(), m_copy_buffer);
	}


	bool canPasteEntities() const override
	{
		return m_copy_buffer.getPos() > 0;
	}


	void pasteEntities() override
	{
		PasteEntityCommand* command = LUMIX_NEW(m_allocator, PasteEntityCommand)(*this, m_copy_buffer);
		executeCommand(command);
	}


	void cloneComponent(const ComponentUID& src, Entity entity) override
	{
		IScene* scene = m_universe->getScene(src.type);
		ComponentUID clone(entity, src.type, scene, scene->createComponent(src.type, entity));

		const auto& properties = PropertyRegister::getDescriptors(src.type);
		OutputBlob stream(m_allocator);
		for (int i = 0; i < properties.size(); ++i)
		{
			stream.clear();
			properties[i]->get(src, -1, stream);
			InputBlob blob(stream.getData(), stream.getPos());
			properties[i]->set(clone, -1, blob);
		}
	}


	void destroyComponent(const Entity* entities, int count, ComponentType cmp_type) override
	{
		ASSERT(count > 0);
		if (entities[0] == m_camera && cmp_type == CAMERA_TYPE)
		{
			g_log_error.log("Editor") << "Can not destroy component from the editing camera";
			return;
		}

		IEditorCommand* command = LUMIX_NEW(m_allocator, DestroyComponentCommand)(*this, entities, count, cmp_type);
		executeCommand(command);
	}


	void addComponent(ComponentType cmp_type) override
	{
		if (!m_selected_entities.empty())
		{
			IEditorCommand* command = LUMIX_NEW(m_allocator, AddComponentCommand)(*this, m_selected_entities, cmp_type);
			executeCommand(command);
		}
	}


	void lookAtSelected() override
	{
		Universe* universe = getUniverse();
		if (m_selected_entities.empty()) return;

		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		m_go_to_parameters.m_from = universe->getPosition(m_camera);
		Quat camera_rot = universe->getRotation(m_camera);
		Vec3 dir = camera_rot.rotate(Vec3(0, 0, 1));
		m_go_to_parameters.m_to = universe->getPosition(m_selected_entities[0]) + dir * 10;
		float len = (m_go_to_parameters.m_to - m_go_to_parameters.m_from).length();
		m_go_to_parameters.m_speed = Math::maximum(100.0f / (len > 0 ? len : 1), 2.0f);
		m_go_to_parameters.m_from_rot = m_go_to_parameters.m_to_rot = camera_rot;
	}


	void loadUniverse(const Path& path) override
	{
		if (m_is_game_mode) stopGameMode(false);
		destroyUniverse();
		createUniverse();
		m_universe->setPath(path);
		g_log_info.log("Editor") << "Loading universe " << path << "...";
		FS::FileSystem& fs = m_engine->getFileSystem();
		FS::ReadCallback file_read_cb;
		file_read_cb.bind<WorldEditorImpl, &WorldEditorImpl::loadMap>(this);
		fs.openAsync(fs.getDefaultDevice(), path, FS::Mode::OPEN_AND_READ, file_read_cb);
	}


	void loadMap(FS::IFile& file, bool success)
	{
		PROFILE_FUNCTION();
		ASSERT(success);
		if (success)
		{
			deserialize(m_universe->getPath()); 
			char path[MAX_PATH_LENGTH];
			copyString(path, sizeof(path), m_universe->getPath().c_str());
			catString(path, sizeof(path), ".lst");
			copyFile(m_universe->getPath().c_str(), path);
			m_editor_icons->refresh();
		}
	}


	void newUniverse() override
	{
		destroyUniverse();
		createUniverse();
		g_log_info.log("Editor") << "Universe created.";
	}


	enum class SerializedVersion : int
	{

		LATEST
	};


	#pragma pack(1)
		struct Header
		{
			u32 magic;
			int version;
			u32 hash;
			u32 engine_hash;
		};
	#pragma pack()


	void load(FS::IFile& file)
	{
		m_is_loading = true;
		ASSERT(file.getBuffer());
		Header header;
		if (file.size() < sizeof(header))
		{
			g_log_error.log("Editor") << "Corrupted file.";
			newUniverse();
			m_is_loading = false;
			return;
		}

		Timer* timer = Timer::create(m_allocator);
		g_log_info.log("Editor") << "Parsing universe...";
		InputBlob blob(file.getBuffer(), (int)file.size());
		u32 hash = 0;
		blob.read(hash);
		header.version = -1;
		int hashed_offset = sizeof(hash);
		if (hash == 0xFFFFffff)
		{
			blob.rewind();
			blob.read(header);
			hashed_offset = sizeof(header);
			hash = header.hash;
		}
		else
		{
			u32 engine_hash = 0;
			blob.read(engine_hash);
		}
		if (crc32((const u8*)blob.getData() + hashed_offset, blob.getSize() - hashed_offset) != hash)
		{
			Timer::destroy(timer);
			g_log_error.log("Editor") << "Corrupted file.";
			newUniverse();
			m_is_loading = false;
			return;
		}

		if (isValid(m_camera)) m_universe->destroyEntity(m_camera);

		if (m_engine->deserialize(*m_universe, blob))
		{
			m_entity_groups.deserialize(blob);
			m_prefab_system->deserialize(blob);
			m_camera = m_render_interface->getCameraEntity(m_render_interface->getCameraInSlot("editor"));

			g_log_info.log("Editor") << "Universe parsed in " << timer->getTimeSinceStart() << " seconds";
		}
		else
		{
			newUniverse();
		}
		Timer::destroy(timer);
		m_is_loading = false;
	}


	template <typename T>
	static IEditorCommand* constructEditorCommand(WorldEditor& editor)
	{
		return LUMIX_NEW(editor.getAllocator(), T)(editor);
	}


	Gizmo& getGizmo() override { return *m_gizmo; }


	void renderIcons() override
	{
		if(m_editor_icons) m_editor_icons->render();
	}


	ComponentUID getEditCamera() override
	{
		if (!isValid(m_camera)) return ComponentUID::INVALID;
		return getUniverse()->getComponent(m_camera, CAMERA_TYPE);
	}


	WorldEditorImpl(const char* base_path, Engine& engine, IAllocator& allocator)
		: m_allocator(allocator)
		, m_entity_selected(m_allocator)
		, m_universe_destroyed(m_allocator)
		, m_universe_created(m_allocator)
		, m_selected_entities(m_allocator)
		, m_editor_icons(nullptr)
		, m_plugins(m_allocator)
		, m_undo_stack(m_allocator)
		, m_copy_buffer(m_allocator)
		, m_camera(INVALID_ENTITY)
		, m_editor_command_creators(m_allocator)
		, m_is_loading(false)
		, m_universe(nullptr)
		, m_is_orbit(false)
		, m_gizmo_use_step(false)
		, m_is_additive_selection(false)
		, m_entity_groups(m_allocator)
		, m_mouse_sensitivity(200, 200)
		, m_render_interface(nullptr)
		, m_selected_entity_on_game_mode(INVALID_ENTITY)
		, m_mouse_handling_plugin(nullptr)
		, m_is_game_mode(false)
		, m_is_snap_mode(false)
		, m_undo_index(-1)
		, m_engine(&engine)
		, m_entity_map(m_allocator)
		, m_is_guid_pseudorandom(false)
	{
		for (auto& i : m_is_mouse_down) i = false;
		for (auto& i : m_is_mouse_click) i = false;
		m_go_to_parameters.m_is_active = false;
		
		m_measure_tool = LUMIX_NEW(m_allocator, MeasureTool)();
		addPlugin(*m_measure_tool);

		const char* plugins[] = { "steam", "renderer", "animation", "audio", "physics", "navigation", "lua_script", "gui"
			#ifdef GAME_PROJECT_NAME
			, GAME_PROJECT_NAME
			#endif
		};

		PluginManager& plugin_manager = m_engine->getPluginManager();
		for (auto* plugin_name : plugins)
		{
			if (!plugin_manager.load(plugin_name))
			{
				g_log_info.log("Editor") << plugin_name << " plugin has not been loaded";
			}
		}

		m_prefab_system = PrefabSystem::create(*this);

		m_editor_command_creators.insert(
			crc32("begin_group"), &WorldEditorImpl::constructEditorCommand<BeginGroupCommand>);
		m_editor_command_creators.insert(
			crc32("end_group"), &WorldEditorImpl::constructEditorCommand<EndGroupCommand>);
		m_editor_command_creators.insert(
			crc32("scale_entity"), &WorldEditorImpl::constructEditorCommand<ScaleEntityCommand>);
		m_editor_command_creators.insert(
			crc32("move_entity"), &WorldEditorImpl::constructEditorCommand<MoveEntityCommand>);
		m_editor_command_creators.insert(
			crc32("set_entity_name"), &WorldEditorImpl::constructEditorCommand<SetEntityNameCommand>);
		m_editor_command_creators.insert(
			crc32("paste_entity"), &WorldEditorImpl::constructEditorCommand<PasteEntityCommand>);
		m_editor_command_creators.insert(crc32("remove_array_property_item"),
			&WorldEditorImpl::constructEditorCommand<RemoveArrayPropertyItemCommand>);
		m_editor_command_creators.insert(
			crc32("add_array_property_item"), &WorldEditorImpl::constructEditorCommand<AddArrayPropertyItemCommand>);
		m_editor_command_creators.insert(
			crc32("set_property_values"), &WorldEditorImpl::constructEditorCommand<SetPropertyCommand>);
		m_editor_command_creators.insert(
			crc32("add_component"), &WorldEditorImpl::constructEditorCommand<AddComponentCommand>);
		m_editor_command_creators.insert(
			crc32("destroy_entities"), &WorldEditorImpl::constructEditorCommand<DestroyEntitiesCommand>);
		m_editor_command_creators.insert(
			crc32("destroy_components"), &WorldEditorImpl::constructEditorCommand<DestroyComponentCommand>);
		m_editor_command_creators.insert(
			crc32("add_entity"), &WorldEditorImpl::constructEditorCommand<AddEntityCommand>);

		m_gizmo = Gizmo::create(*this);
		m_editor_icons = EditorIcons::create(*this);

		char command_line[2048];
		getCommandLine(command_line, lengthOf(command_line));
		CommandLineParser parser(command_line);
		while (parser.next())
		{
			if (parser.currentEquals("-pseudorandom_guid"))
			{
				m_is_guid_pseudorandom = true;
				break;
			}
		}
	}


	void navigate(float forward, float right, float up, float speed) override
	{
		Universe* universe = getUniverse();
		Vec3 pos = universe->getPosition(m_camera);
		Quat rot = universe->getRotation(m_camera);

		right = m_is_orbit ? 0 : right;

		pos += rot.rotate(Vec3(0, 0, -1)) * forward * speed;
		pos += rot.rotate(Vec3(1, 0, 0)) * right * speed;
		pos += rot.rotate(Vec3(0, 1, 0)) * up * speed;
		universe->setPosition(m_camera, pos);
	}


	bool isEntitySelected(Entity entity) const override
	{
		return m_selected_entities.indexOf(entity) >= 0;
	}


	const Array<Entity>& getSelectedEntities() const override
	{
		return m_selected_entities;
	}


	EntityGroups& getEntityGroups() override
	{
		return m_entity_groups;
	}


	void setSnapMode(bool enable) override
	{
		m_is_snap_mode = enable;
	}


	void setAdditiveSelection(bool additive) override { m_is_additive_selection = additive; }


	void addArrayPropertyItem(const ComponentUID& cmp, IArrayDescriptor& property) override
	{
		if (cmp.isValid())
		{
			IEditorCommand* command =
				LUMIX_NEW(m_allocator, AddArrayPropertyItemCommand)(*this, cmp, property);
			executeCommand(command);
		}
	}


	void removeArrayPropertyItem(const ComponentUID& cmp, int index, IArrayDescriptor& property) override
	{
		if (cmp.isValid())
		{
			IEditorCommand* command =
				LUMIX_NEW(m_allocator, RemoveArrayPropertyItemCommand)(*this, cmp, index, property);
			executeCommand(command);
		}
	}


	void setProperty(ComponentType component_type,
		int index,
		const IPropertyDescriptor& property,
		const Entity* entities,
		int count,
		const void* data,
		int size) override
	{
		ComponentUID cmp = getUniverse()->getComponent(m_selected_entities[0], component_type);

		IEditorCommand* command = LUMIX_NEW(m_allocator, SetPropertyCommand)(
			*this, entities, count, component_type, index, property, data, size);
		executeCommand(command);
	}


	bool isOrbitCamera() const override { return m_is_orbit; }


	void setOrbitCamera(bool enable) override
	{
		m_orbit_delta = Vec2(0, 0);
		m_is_orbit = enable;
	}


	void panCamera(float x, float y)
	{
		Universe* universe = getUniverse();
		Vec3 pos = universe->getPosition(m_camera);
		Quat rot = universe->getRotation(m_camera);

		if(m_is_orbit)
		{
			m_orbit_delta.x += x;
			m_orbit_delta.y += y;
		}

		pos += rot.rotate(Vec3(x, 0, 0));
		pos += rot.rotate(Vec3(0, -y, 0));

		universe->setPosition(m_camera, pos);
	}


	Vec2 getMouseSensitivity() override
	{
		return m_mouse_sensitivity;
	}


	void setMouseSensitivity(float x, float y) override
	{
		m_mouse_sensitivity.x = x;
		m_mouse_sensitivity.y = y;
	}


	void rotateCamera(int x, int y)
	{
		Universe* universe = getUniverse();
		Vec3 pos = universe->getPosition(m_camera);
		Quat rot = universe->getRotation(m_camera);
		Quat old_rot = rot;

		float yaw = -Math::signum(x) * (Math::pow(Math::abs((float)x / m_mouse_sensitivity.x), 1.2f));
		Quat yaw_rot(Vec3(0, 1, 0), yaw);
		rot = yaw_rot * rot;
		rot.normalize();

		Vec3 pitch_axis = rot.rotate(Vec3(1, 0, 0));
		float pitch = -Math::signum(y) * (Math::pow(Math::abs((float)y / m_mouse_sensitivity.y), 1.2f));
		Quat pitch_rot(pitch_axis, pitch);
		rot = pitch_rot * rot;
		rot.normalize();

		if (m_is_orbit && !m_selected_entities.empty())
		{
			Vec3 dir = rot.rotate(Vec3(0, 0, 1));
			Vec3 entity_pos = universe->getPosition(m_selected_entities[0]);
			Vec3 nondelta_pos = pos;

			nondelta_pos -= old_rot.rotate(Vec3(0, -1, 0)) * m_orbit_delta.y;
			nondelta_pos -= old_rot.rotate(Vec3(1, 0, 0)) * m_orbit_delta.x;

			float dist = (entity_pos - nondelta_pos).length();
			pos = entity_pos + dir * dist;
			pos += rot.rotate(Vec3(1, 0, 0)) * m_orbit_delta.x;
			pos += rot.rotate(Vec3(0, -1, 0)) * m_orbit_delta.y;
		}

		universe->setRotation(m_camera, rot);
		universe->setPosition(m_camera, pos);
	}


	void addEntitiesToSelection(const Entity* entities, int count)
	{
		for (int i = 0; i < count; ++i)
		{
			int group = m_entity_groups.getEntityGroup(entities[i]);
			if (!m_entity_groups.isGroupFrozen(group))
			{
				m_selected_entities.push(entities[i]);
			}
		}
		m_entity_selected.invoke(m_selected_entities);
	}


	void selectEntities(const Entity* entities, int count) override
	{
		m_selected_entities.clear();
		for (int i = 0; i < count; ++i)
		{
			int group = m_entity_groups.getEntityGroup(entities[i]);
			if (!m_entity_groups.isGroupFrozen(group))
			{
				m_selected_entities.push(entities[i]);
			}
		}
		m_entity_selected.invoke(m_selected_entities);
	}


	void onEntityDestroyed(Entity entity)
	{
		m_selected_entities.eraseItemFast(entity);
	}


	void destroyUniverse()
	{
		if (m_is_game_mode) stopGameMode(false);

		m_entity_groups.setUniverse(nullptr);
		ASSERT(m_universe);
		destroyUndoStack();
		m_universe_destroyed.invoke();
		m_editor_icons->clear();
		selectEntities(nullptr, 0);
		m_camera = INVALID_ENTITY;
		m_engine->destroyUniverse(*m_universe);
		m_universe = nullptr;
	}


	DelegateList<void()>& universeCreated() override
	{
		return m_universe_created;
	}


	DelegateList<void(const Array<Entity>&)>& entitySelected() override
	{
		return m_entity_selected;
	}


	DelegateList<void()>& universeDestroyed() override
	{
		return m_universe_destroyed;
	}


	void destroyUndoStack()
	{
		m_undo_index = -1;
		for (int i = 0; i < m_undo_stack.size(); ++i)
		{
			LUMIX_DELETE(m_allocator, m_undo_stack[i]);
		}
		m_undo_stack.clear();
	}


	void createUniverse()
	{
		ASSERT(!m_universe);

		m_is_universe_changed = false;
		destroyUndoStack();
		if (m_is_guid_pseudorandom) Math::seedRandomGUID(0);
		m_universe = &m_engine->createUniverse(true);
		Universe* universe = m_universe;

		universe->entityDestroyed().bind<WorldEditorImpl, &WorldEditorImpl::onEntityDestroyed>(this);

		m_is_orbit = false;
		m_selected_entities.clear();
		m_universe_created.invoke();
		m_entity_groups.setUniverse(universe);

		m_camera = universe->createEntity(Vec3(0, 0, -5), Quat(Vec3(0, 1, 0), -Math::PI));
		m_entity_map.create(m_camera);

		universe->setEntityName(m_camera, "editor_camera");
		ComponentUID cmp = m_engine->createComponent(*universe, m_camera, CAMERA_TYPE);
		ASSERT(cmp.isValid());
		m_render_interface->setCameraSlot(cmp.handle, "editor");
	}


	bool canUndo() const override
	{
		return !m_is_game_mode && m_undo_index < m_undo_stack.size() && m_undo_index >= 0;
	}


	bool canRedo() const override
	{
		return !m_is_game_mode && m_undo_index + 1 < m_undo_stack.size();
	}


	void undo() override
	{
		if (m_is_game_mode) return;

		static const u32 end_group_hash = crc32("end_group");
		static const u32 begin_group_hash = crc32("begin_group");

		if (m_undo_index >= m_undo_stack.size() || m_undo_index < 0) return;

		if(crc32(m_undo_stack[m_undo_index]->getType()) == end_group_hash)
		{
			--m_undo_index;
			while(crc32(m_undo_stack[m_undo_index]->getType()) != begin_group_hash)
			{
				m_undo_stack[m_undo_index]->undo();
				--m_undo_index;
			}
			--m_undo_index;
		}
		else
		{
			m_undo_stack[m_undo_index]->undo();
			--m_undo_index;
		}
	}


	void redo() override
	{
		if (m_is_game_mode) return;

		static const u32 end_group_hash = crc32("end_group");
		static const u32 begin_group_hash = crc32("begin_group");

		if (m_undo_index + 1 >= m_undo_stack.size()) return;

		++m_undo_index;
		if(crc32(m_undo_stack[m_undo_index]->getType()) == begin_group_hash)
		{
			++m_undo_index;
			while(crc32(m_undo_stack[m_undo_index]->getType()) != end_group_hash)
			{
				m_undo_stack[m_undo_index]->execute();
				++m_undo_index;
			}
		}
		else
		{
			m_undo_stack[m_undo_index]->execute();
		}
	}


	MeasureTool* getMeasureTool() const override
	{
		return m_measure_tool;
	}


	float getMeasuredDistance() const override
	{
		return m_measure_tool->getDistance();
	}


	bool isMeasureToolActive() const override
	{
		return m_measure_tool->isEnabled();
	}


	void toggleMeasure() override
	{
		m_measure_tool->enable(!m_measure_tool->isEnabled());
	}


	void saveUndoStack(const Path& path) override
	{
		if (m_undo_stack.empty()) return;

		FS::FileSystem& fs = m_engine->getFileSystem();
		FS::IFile* file = fs.open(fs.getDiskDevice(), path, FS::Mode::CREATE_AND_WRITE);
		if (file)
		{
			JsonSerializer serializer(*file, JsonSerializer::WRITE, path, m_allocator);
			serializer.beginObject();
			serializer.beginArray("commands");
			for (int i = 0; i < m_undo_stack.size(); ++i)
			{
				serializer.beginObject();
				serializer.serialize("undo_command_type", m_undo_stack[i]->getType());
				m_undo_stack[i]->serialize(serializer);
				serializer.endObject();
			}
			serializer.endArray();
			serializer.endObject();
			fs.close(*file);
		}
		else
		{
			g_log_error.log("Editor") << "Could not save commands to " << path;
		}
	}


	IEditorCommand* createEditorCommand(u32 command_type) override
	{
		int index = m_editor_command_creators.find(command_type);
		if (index >= 0)
		{
			return m_editor_command_creators.at(index)(*this);
		}
		return nullptr;
	}


	void registerEditorCommandCreator(const char* command_type, EditorCommandCreator creator) override
	{
		m_editor_command_creators.insert(crc32(command_type), creator);
	}


	bool executeUndoStack(const Path& path) override
	{
		destroyUndoStack();
		m_undo_index = -1;
		FS::FileSystem& fs = m_engine->getFileSystem();
		FS::IFile* file = fs.open(fs.getDiskDevice(), path, FS::Mode::OPEN_AND_READ);
		if (file)
		{
			JsonSerializer serializer(*file, JsonSerializer::READ, path, m_allocator);
			serializer.deserializeObjectBegin();
			serializer.deserializeArrayBegin("commands");
			while (!serializer.isArrayEnd())
			{
				serializer.nextArrayItem();
				serializer.deserializeObjectBegin();
				char type_name[256];
				serializer.deserialize("undo_command_type", type_name, lengthOf(type_name), "");
				IEditorCommand* command = createEditorCommand(crc32(type_name));
				if (!command)
				{
					g_log_error.log("Editor") << "Unknown command " << type_name << " in " << path;
					destroyUndoStack();
					m_undo_index = -1;
					fs.close(*file);
					return false;
				}
				command->deserialize(serializer);
				while (fs.hasWork()) fs.updateAsyncTransactions();
				executeCommand(command);
				serializer.deserializeObjectEnd();
			}
			serializer.deserializeArrayEnd();
			serializer.deserializeObjectEnd();
			fs.close(*file);
		}
		return file != nullptr;
	}


	void setTopView() override
	{
		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		auto* universe = m_universe;
		m_go_to_parameters.m_from = universe->getPosition(m_camera);
		m_go_to_parameters.m_to = m_go_to_parameters.m_from;
		if (m_is_orbit && !m_selected_entities.empty())
		{
			m_go_to_parameters.m_to = universe->getPosition(m_selected_entities[0]) + Vec3(0, 10, 0);
		}
		Quat camera_rot = universe->getRotation(m_camera);
		m_go_to_parameters.m_speed = 2.0f;
		m_go_to_parameters.m_from_rot = camera_rot;
		m_go_to_parameters.m_to_rot = Quat(Vec3(1, 0, 0), -Math::PI * 0.5f);
	}


	void setFrontView() override
	{
		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		auto* universe = m_universe;
		m_go_to_parameters.m_from = universe->getPosition(m_camera);
		m_go_to_parameters.m_to = m_go_to_parameters.m_from;
		if (m_is_orbit && !m_selected_entities.empty())
		{
			m_go_to_parameters.m_to = universe->getPosition(m_selected_entities[0]) + Vec3(0, 0, -10);
		}
		Quat camera_rot = universe->getRotation(m_camera);
		m_go_to_parameters.m_speed = 2.0f;
		m_go_to_parameters.m_from_rot = camera_rot;
		m_go_to_parameters.m_to_rot = Quat(Vec3(0, 1, 0), Math::PI);
	}


	void setSideView() override
	{
		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		auto* universe = m_universe;
		m_go_to_parameters.m_from = universe->getPosition(m_camera);
		m_go_to_parameters.m_to = m_go_to_parameters.m_from;
		if (m_is_orbit && !m_selected_entities.empty())
		{
			m_go_to_parameters.m_to = universe->getPosition(m_selected_entities[0]) + Vec3(-10, 0, 0);
		}
		Quat camera_rot = universe->getRotation(m_camera);
		m_go_to_parameters.m_speed = 2.0f;
		m_go_to_parameters.m_from_rot = camera_rot;
		m_go_to_parameters.m_to_rot = Quat(Vec3(0, 1, 0), -Math::PI * 0.5f);
	}


	bool runTest(const char* dir, const char* name) override
	{
		FS::FileSystem& fs = m_engine->getFileSystem();
		while (fs.hasWork()) fs.updateAsyncTransactions();
		newUniverse();
		Path undo_stack_path(dir, name, ".json");
		executeUndoStack(undo_stack_path);
		while (fs.hasWork()) fs.updateAsyncTransactions();

		StaticString<MAX_PATH_LENGTH> result_dir(m_engine->getDiskFileDevice()->getBasePath(), dir, "/results/");
		PlatformInterface::makePath(result_dir);
		result_dir << name << "/";
		PlatformInterface::makePath(result_dir);
		Path result_universe_path(result_dir, ".unv");
		serialize(result_universe_path);

		bool is_same = true;

		PlatformInterface::FileIterator* iter = PlatformInterface::createFileIterator(result_dir, m_allocator);
		PlatformInterface::FileInfo info;
		Array<u8> src_data(m_allocator);
		Array<u8> dst_data(m_allocator);
		while (is_same && PlatformInterface::getNextFile(iter, &info))
		{
			if (info.is_directory) continue;
			if (info.filename[0] == '.') continue;

			FS::OsFile dst_file;
			FS::OsFile src_file;
			StaticString<MAX_PATH_LENGTH> dst_path(result_dir, info.filename);
			StaticString<MAX_PATH_LENGTH> src_path(dir, "/", name, "/", info.filename);
			if (dst_file.open(dst_path, FS::Mode::OPEN_AND_READ, m_allocator))
			{
				if (src_file.open(src_path, FS::Mode::OPEN_AND_READ, m_allocator))
				{
					int dst_size = (int)dst_file.size();
					int src_size = (int)src_file.size();
					if (src_size == dst_size)
					{
						dst_data.resize(dst_size);
						dst_file.read(&dst_data[0], dst_data.size());
						src_data.resize(src_size);
						src_file.read(&src_data[0], src_data.size());

						for (int i = 0; i < src_size; ++i)
						{
							if (src_data[i] != dst_data[i])
							{
								is_same = false;
								break;
							}
						}
					}
					else
					{
						is_same = false;
					}
					src_file.close();
				}
				else
				{
					is_same = false;
				}
				dst_file.close();
			}
			else
			{
				is_same = false;
			}
		}
		PlatformInterface::destroyFileIterator(iter);

		StaticString<MAX_PATH_LENGTH> tmp(dir, "/", name);
		iter = PlatformInterface::createFileIterator(tmp, m_allocator);
		while (is_same && PlatformInterface::getNextFile(iter, &info))
		{
			if (info.is_directory) continue;
			if (info.filename[0] == '.') continue;
			StaticString<MAX_PATH_LENGTH> dst_path(result_dir, info.filename);
			if (!PlatformInterface::fileExists(dst_path))
			{
				is_same = false;
				break;
			}
		}
		PlatformInterface::destroyFileIterator(iter);

		return is_same;
	}


private:
	struct MouseMode
	{
		enum Value
		{
			NONE,
			SELECT,
			NAVIGATE,
			PAN,

			CUSTOM
		};
	};

	struct GoToParameters
	{
		bool m_is_active;
		Vec3 m_from;
		Vec3 m_to;
		Quat m_from_rot;
		Quat m_to_rot;
		float m_t;
		float m_speed;
	};

	IAllocator& m_allocator;
	GoToParameters m_go_to_parameters;
	Gizmo* m_gizmo;
	Array<Entity> m_selected_entities;
	MouseMode::Value m_mouse_mode;
	EditorIcons* m_editor_icons;
	float m_mouse_x;
	float m_mouse_y;
	float m_mouse_rel_x;
	float m_mouse_rel_y;
	Vec2 m_orbit_delta;
	Vec2 m_mouse_sensitivity;
	bool m_gizmo_use_step;
	bool m_is_game_mode;
	bool m_is_orbit;
	bool m_is_additive_selection;
	bool m_is_snap_mode;
	FS::IFile* m_game_mode_file;
	Engine* m_engine;
	Entity m_camera;
	Entity m_selected_entity_on_game_mode;
	DelegateList<void()> m_universe_destroyed;
	DelegateList<void()> m_universe_created;
	DelegateList<void(const Array<Entity>&)> m_entity_selected;
	bool m_is_mouse_down[3];
	bool m_is_mouse_click[3];

	Array<Plugin*> m_plugins;
	MeasureTool* m_measure_tool;
	Plugin* m_mouse_handling_plugin;
	PrefabSystem* m_prefab_system;
	Array<IEditorCommand*> m_undo_stack;
	AssociativeArray<u32, EditorCommandCreator> m_editor_command_creators;
	int m_undo_index;
	OutputBlob m_copy_buffer;
	bool m_is_loading;
	Universe* m_universe;
	EntityGUIDMap m_entity_map;
	EntityGroups m_entity_groups;
	RenderInterface* m_render_interface;
	u32 m_current_group_type;
	bool m_is_universe_changed;
	bool m_is_guid_pseudorandom;
};


bool PasteEntityCommand::execute()
{
	InputBlob blob(m_blob.getData(), m_blob.getPos());

	m_entities.clear();

	int entity_count;
	blob.read(entity_count);
	m_entities.reserve(entity_count);

	Universe& universe = *m_editor.getUniverse();
	Matrix base_matrix = Matrix::IDENTITY;
	base_matrix.setTranslation(m_position);
	for (int i = 0; i < entity_count; ++i)
	{
		Matrix mtx;
		blob.read(mtx);
		if (i == 0)
		{
			Matrix inv = mtx;
			inv.inverse();
			base_matrix.copy3x3(mtx);
			base_matrix = base_matrix * inv;
			mtx.setTranslation(m_position);
		}
		else
		{
			mtx = base_matrix * mtx;
		}
		Entity new_entity = universe.createEntity(Vec3(0, 0, 0), Quat(0, 0, 0, 1));
		((WorldEditorImpl&)m_editor).m_entity_map.create(new_entity);
		m_entities.push(new_entity);
		universe.setMatrix(new_entity, mtx);
		i32 count;
		blob.read(count);
		for (int i = 0; i < count; ++i)
		{
			u32 hash;
			blob.read(hash);
			ComponentType type = PropertyRegister::getComponentTypeFromHash(hash);
			ComponentUID cmp = m_editor.getEngine().createComponent(universe, new_entity, type);
			i32 prop_count;
			blob.read(prop_count);
			for (int j = 0; j < prop_count; ++j)
			{
				u32 prop_name_hash;
				blob.read(prop_name_hash);
				auto* desc = PropertyRegister::getDescriptor(type, prop_name_hash);
				i32 size;
				blob.read(size);
				if (desc)
					desc->set(cmp, -1, blob);
				else
					blob.skip(size);
			}
		}
	}

	return true;
}


void PasteEntityCommand::undo()
{
	for (auto entity : m_entities)
	{
		m_editor.getUniverse()->destroyEntity(entity);
		((WorldEditorImpl&)m_editor).m_entity_map.erase(entity);
	}
	m_entities.clear();
}


WorldEditor* WorldEditor::create(const char* base_path, Engine& engine, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, WorldEditorImpl)(base_path, engine, allocator);
}


void WorldEditor::destroy(WorldEditor* editor, IAllocator& allocator)
{
	LUMIX_DELETE(allocator, static_cast<WorldEditorImpl*>(editor));
}


} // namespace Lumix
