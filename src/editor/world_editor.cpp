#include "world_editor.h"

#include "engine/core/array.h"
#include "engine/core/associative_array.h"
#include "engine/core/blob.h"
#include "engine/core/crc32.h"
#include "engine/core/delegate_list.h"
#include "engine/core/fs/file_system.h"
#include "engine/core/fs/memory_file_device.h"
#include "engine/core/fs/disk_file_device.h"
#include "engine/core/fs/tcp_file_device.h"
#include "engine/core/fs/tcp_file_server.h"
#include "engine/core/geometry.h"
#include "engine/core/input_system.h"
#include "engine/core/json_serializer.h"
#include "engine/core/log.h"
#include "engine/core/matrix.h"
#include "engine/core/path.h"
#include "engine/core/path_utils.h"
#include "engine/core/profiler.h"
#include "engine/core/resource_manager.h"
#include "engine/core/resource_manager_base.h"
#include "engine/core/system.h"
#include "engine/core/timer.h"
#include "engine/debug/debug.h"
#include "editor/entity_groups.h"
#include "editor/editor_icon.h"
#include "editor/entity_template_system.h"
#include "editor/gizmo.h"
#include "editor/measure_tool.h"
#include "engine/engine.h"
#include "engine/iproperty_descriptor.h"
#include "engine/property_register.h"
#include "ieditor_command.h"
#include "engine/iplugin.h"
#include "engine/plugin_manager.h"
#include "render_interface.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "engine/universe/universe.h"


namespace Lumix
{


static const uint32 RENDERABLE_HASH = crc32("renderable");
static const uint32 CAMERA_HASH = crc32("camera");


class BeginGroupCommand : public IEditorCommand
{
	bool execute() override { ASSERT(false); return false; }
	void undo() override { ASSERT(false); }
	void serialize(JsonSerializer& serializer) override {}
	void deserialize(JsonSerializer& serializer) override {}
	bool merge(IEditorCommand& command) override { ASSERT(false); return false; }
	uint32 getType() override
	{
		static const uint32 type = crc32("begin_group");
		return type;
	}
};


struct EndGroupCommand : public IEditorCommand
{
	bool execute() override { ASSERT(false); return false; }
	void undo() override { ASSERT(false); }
	void serialize(JsonSerializer& serializer) override {}
	void deserialize(JsonSerializer& serializer) override {}
	bool merge(IEditorCommand& command) override { ASSERT(false); return false; }
	uint32 getType() override
	{
		static const uint32 type = crc32("end_group");
		return type;
	}

	uint32 group_type;
};


class SetEntityNameCommand : public IEditorCommand
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
		serializer.deserialize("entity", m_entity, 0);
		m_old_name = m_editor.getUniverse()->getEntityName(m_entity);
	}


	bool execute() override
	{
		m_editor.getUniverse()->setEntityName(m_entity, m_new_name.c_str());
		m_editor.entityNameSet().invoke(m_entity, m_new_name.c_str());
		return true;
	}


	void undo() override
	{
		m_editor.getUniverse()->setEntityName(m_entity, m_old_name.c_str());
		m_editor.entityNameSet().invoke(m_entity, m_old_name.c_str());
	}


	uint32 getType() override
	{
		static const uint32 type = crc32("set_entity_name");
		return type;
	}


	bool merge(IEditorCommand& command)
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


class PasteEntityCommand : public IEditorCommand
{
public:
	explicit PasteEntityCommand(WorldEditor& editor)
		: m_blob(editor.getAllocator())
		, m_editor(editor)
		, m_entities(editor.getAllocator())
	{
	}


	PasteEntityCommand(WorldEditor& editor, OutputBlob& blob)
		: m_blob(blob, editor.getAllocator())
		, m_editor(editor)
		, m_position(editor.getCameraRaycastHit())
		, m_entities(editor.getAllocator())
	{
	}


	bool execute() override;


	void serialize(JsonSerializer& serializer)
	{
		serializer.serialize("pos_x", m_position.x);
		serializer.serialize("pos_y", m_position.y);
		serializer.serialize("pos_z", m_position.z);
		serializer.serialize("size", m_blob.getPos());
		serializer.beginArray("data");
		for (int i = 0; i < m_blob.getPos(); ++i)
		{
			serializer.serializeArrayItem(
				(int32)((const uint8*)m_blob.getData())[i]);
		}
		serializer.endArray();
	}


	void deserialize(JsonSerializer& serializer)
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
			int32 data;
			serializer.deserializeArrayItem(data, 0);
			m_blob.write((uint8)data);
		}
		serializer.deserializeArrayEnd();
	}


	void undo() override
	{
		for (auto entity : m_entities)
		{
			const WorldEditor::ComponentList& cmps = m_editor.getComponents(entity);
			for (int i = 0; i < cmps.size(); ++i)
			{
				cmps[i].scene->destroyComponent(cmps[i].index, cmps[i].type);
			}
			m_editor.getUniverse()->destroyEntity(entity);
		}
		m_entities.clear();
	}


	uint32 getType() override
	{
		static const uint32 type = crc32("paste_entity");
		return type;
	}


	bool merge(IEditorCommand& command)
	{
		ASSERT(command.getType() == getType());
		return false;
	}

private:
	OutputBlob m_blob;
	WorldEditor& m_editor;
	Vec3 m_position;
	Lumix::Array<Entity> m_entities;
};


class MoveEntityCommand : public IEditorCommand
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
			serializer.deserializeArrayItem(m_entities[i], 0);
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


	uint32 getType() override
	{
		static const uint32 type = crc32("move_entity");
		return type;
	}


	bool merge(IEditorCommand& command)
	{
		ASSERT(command.getType() == getType());
		MoveEntityCommand& my_command =
			static_cast<MoveEntityCommand&>(command);
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


class ScaleEntityCommand : public IEditorCommand
{
public:
	explicit ScaleEntityCommand(WorldEditor& editor)
		: m_new_scales(editor.getAllocator())
		, m_old_scales(editor.getAllocator())
		, m_entities(editor.getAllocator())
		, m_editor(editor)
	{
	}


	ScaleEntityCommand(WorldEditor& editor,
		const Entity* entities,
		const float* new_scales,
		int count,
		IAllocator& allocator)
		: m_new_scales(allocator)
		, m_old_scales(allocator)
		, m_entities(allocator)
		, m_editor(editor)
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = count - 1; i >= 0; --i)
		{
			m_entities.push(entities[i]);
			m_new_scales.push(new_scales[i]);
			m_old_scales.push(universe->getScale(entities[i]));
		}
	}


	void serialize(JsonSerializer& serializer) override
	{
		serializer.serialize("count", m_entities.size());
		serializer.beginArray("entities");
		for (int i = 0; i < m_entities.size(); ++i)
		{
			serializer.serializeArrayItem(m_entities[i]);
			serializer.serializeArrayItem(m_new_scales[i]);
		}
		serializer.endArray();
	}


	void deserialize(JsonSerializer& serializer) override
	{
		Universe* universe = m_editor.getUniverse();
		int count;
		serializer.deserialize("count", count, 0);
		m_entities.resize(count);
		m_new_scales.resize(count);
		m_old_scales.resize(count);
		serializer.deserializeArrayBegin("entities");
		for (int i = 0; i < m_entities.size(); ++i)
		{
			serializer.deserializeArrayItem(m_entities[i], 0);
			serializer.deserializeArrayItem(m_new_scales[i], 0);
			m_old_scales[i] = universe->getScale(m_entities[i]);
		}
		serializer.deserializeArrayEnd();
	}


	bool execute() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i)
		{
			Entity entity = m_entities[i];
			universe->setScale(entity, m_new_scales[i]);
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


	uint32 getType() override
	{
		static const uint32 type = crc32("scale_entity");
		return type;
	}


	bool merge(IEditorCommand& command)
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
			for (int i = 0, c = m_entities.size(); i < c; ++i)
			{
				my_command.m_new_scales[i] = m_new_scales[i];
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
	Array<float> m_new_scales;
	Array<float> m_old_scales;
};


class RemoveArrayPropertyItemCommand : public IEditorCommand
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
		serializer.serialize("component_index", m_component.index);
		serializer.serialize("component_type", m_component.type);
		serializer.serialize("property_name_hash", m_descriptor->getNameHash());
	}


	void deserialize(JsonSerializer& serializer) override
	{
		serializer.deserialize("inedx", m_index, 0);
		serializer.deserialize("entity_index", m_component.entity, 0);
		serializer.deserialize("component_index", m_component.index, 0);
		serializer.deserialize("component_type", m_component.type, 0);
		m_component.scene = m_editor.getSceneByComponentType(m_component.type);
		uint32 property_name_hash;
		serializer.deserialize("property_name_hash", property_name_hash, 0);
		m_descriptor = static_cast<const IArrayDescriptor*>(
			PropertyRegister::getDescriptor(m_component.type, property_name_hash));
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
			m_descriptor->getChildren()[i]->set(
				m_component, m_index, old_values);
		}
	}


	uint32 getType() override
	{
		static const uint32 hash = crc32("remove_array_property_item");
		return hash;
	}


	bool merge(IEditorCommand&) { return false; }

private:
	WorldEditor& m_editor;
	ComponentUID m_component;
	int m_index;
	const IArrayDescriptor* m_descriptor;
	OutputBlob m_old_values;
};


class AddArrayPropertyItemCommand : public IEditorCommand
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
		serializer.serialize("component_index", m_component.index);
		serializer.serialize("component_type", m_component.type);
		serializer.serialize("property_name_hash", m_descriptor->getNameHash());
	}


	void deserialize(JsonSerializer& serializer) override
	{
		serializer.deserialize("inedx", m_index, 0);
		serializer.deserialize("entity_index", m_component.entity, 0);
		serializer.deserialize("component_index", m_component.index, 0);
		serializer.deserialize("component_type", m_component.type, 0);
		m_component.scene = m_editor.getSceneByComponentType(m_component.type);
		uint32 property_name_hash;
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


	uint32 getType() override
	{
		static const uint32 hash = crc32("add_array_property_item");
		return hash;
	}


	bool merge(IEditorCommand&) { return false; }

private:
	ComponentUID m_component;
	int m_index;
	const IArrayDescriptor* m_descriptor;
	WorldEditor& m_editor;
};


class SetPropertyCommand : public IEditorCommand
{
public:
	explicit SetPropertyCommand(WorldEditor& editor)
		: m_editor(editor)
		, m_new_value(editor.getAllocator())
		, m_old_value(editor.getAllocator())
	{
	}


	SetPropertyCommand(WorldEditor& editor,
					   Entity entity,
					   uint32 component_type,
					   const IPropertyDescriptor& property_descriptor,
					   const void* data,
					   int size)
		: m_component_type(component_type)
		, m_entity(entity)
		, m_property_descriptor(&property_descriptor)
		, m_editor(editor)
		, m_new_value(editor.getAllocator())
		, m_old_value(editor.getAllocator())
	{
		m_index = -1;
		m_new_value.write(data, size);
		ComponentUID component = m_editor.getComponent(entity, component_type);
		m_property_descriptor->get(component, -1, m_old_value);
	}


	SetPropertyCommand(WorldEditor& editor,
					   Entity entity,
					   uint32 component_type,
					   int index,
					   const IPropertyDescriptor& property_descriptor,
					   const void* data,
					   int size)
		: m_component_type(component_type)
		, m_entity(entity)
		, m_property_descriptor(&property_descriptor)
		, m_editor(editor)
		, m_new_value(editor.getAllocator())
		, m_old_value(editor.getAllocator())
	{
		m_index = index;
		m_new_value.write(data, size);
		ComponentUID component = m_editor.getComponent(entity, component_type);
		m_property_descriptor->get(component, m_index, m_old_value);
	}


	void serialize(JsonSerializer& serializer) override
	{
		serializer.serialize("index", m_index);
		serializer.serialize("entity_index", m_entity);
		serializer.serialize("component_type", m_component_type);
		serializer.beginArray("data");
		for (int i = 0; i < m_new_value.getPos(); ++i)
		{
			serializer.serializeArrayItem(
				(int)((const uint8*)m_new_value.getData())[i]);
		}
		serializer.endArray();
		serializer.serialize("property_name_hash",
							 m_property_descriptor->getNameHash());
	}


	void deserialize(JsonSerializer& serializer) override
	{
		serializer.deserialize("index", m_index, 0);
		serializer.deserialize("entity_index", m_entity, 0);
		serializer.deserialize("component_type", m_component_type, 0);
		serializer.deserializeArrayBegin("data");
		m_new_value.clear();
		while (!serializer.isArrayEnd())
		{
			int data;
			serializer.deserializeArrayItem(data, 0);
			m_new_value.write((uint8)data);
		}
		serializer.deserializeArrayEnd();
		uint32 property_name_hash;
		serializer.deserialize("property_name_hash", property_name_hash, 0);
		m_property_descriptor =
			PropertyRegister::getDescriptor(m_component_type, property_name_hash);
	}


	bool execute() override
	{
		InputBlob blob(m_new_value);
		set(blob);
		return true;
	}


	void undo() override
	{
		InputBlob blob(m_old_value);
		set(blob);
	}


	uint32 getType() override
	{
		static const uint32 hash = crc32("set_property");
		return hash;
	}


	bool merge(IEditorCommand& command)
	{
		ASSERT(command.getType() == getType());
		SetPropertyCommand& src = static_cast<SetPropertyCommand&>(command);
		if (m_component_type == src.m_component_type &&
			m_entity == src.m_entity &&
			src.m_property_descriptor == m_property_descriptor &&
			m_index == src.m_index)
		{
			src.m_new_value = m_new_value;
			return true;
		}
		return false;
	}


	void set(InputBlob& stream)
	{
		ComponentUID component = m_editor.getComponent(m_entity, m_component_type);
		uint32 template_hash = m_editor.getEntityTemplateSystem().getTemplate(m_entity);
		if (template_hash)
		{
			const Array<Entity>& entities =
				m_editor.getEntityTemplateSystem().getInstances(template_hash);
			for (int i = 0, c = entities.size(); i < c; ++i)
			{
				stream.rewind();
				const WorldEditor::ComponentList& cmps = m_editor.getComponents(entities[i]);
				for (int j = 0, cj = cmps.size(); j < cj; ++j)
				{
					if (cmps[j].type == m_component_type)
					{
						if (m_index >= 0)
						{
							m_property_descriptor->set(cmps[j], m_index, stream);
						}
						else
						{
							m_property_descriptor->set(cmps[j], -1, stream);
						}
						break;
					}
				}
			}
		}
		else
		{
			if (m_index >= 0)
			{
				m_property_descriptor->set(component, m_index, stream);
			}
			else
			{
				m_property_descriptor->set(component, -1, stream);
			}
		}
		m_editor.propertySet().invoke(component, *m_property_descriptor);
	}


private:
	WorldEditor& m_editor;
	uint32 m_component_type;
	Entity m_entity;
	OutputBlob m_new_value;
	OutputBlob m_old_value;
	int m_index;
	const IPropertyDescriptor* m_property_descriptor;
};


struct WorldEditorImpl : public WorldEditor
{
private:
	class AddComponentCommand : public IEditorCommand
	{
	public:
		explicit AddComponentCommand(WorldEditor& editor)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
			, m_entities(editor.getAllocator())
		{
		}

		AddComponentCommand(WorldEditorImpl& editor,
							const Array<Entity>& entities,
							uint32 type)
			: m_editor(editor)
			, m_entities(editor.getAllocator())
		{
			m_type = type;
			m_entities.reserve(entities.size());
			for (int i = 0; i < entities.size(); ++i)
			{
				if (!m_editor.getComponent(entities[i], type).isValid())
				{
					uint32 tpl = editor.getEntityTemplateSystem().getTemplate(
						entities[i]);
					if (tpl == 0)
					{
						m_entities.push(entities[i]);
					}
					else
					{
						const Array<Entity>& instances =
							editor.getEntityTemplateSystem().getInstances(tpl);
						for (int i = 0; i < instances.size(); ++i)
						{
							m_entities.push(instances[i]);
						}
					}
				}
			}
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("component_type", m_type);
			serializer.beginArray("entities");
			for (int i = 0; i < m_entities.size(); ++i)
			{
				serializer.serializeArrayItem(m_entities[i]);
			}
			serializer.endArray();
		}


		void deserialize(JsonSerializer& serializer) override
		{
			serializer.deserialize("component_type", m_type, 0);
			m_entities.clear();
			serializer.deserializeArrayBegin("entities");
			while (!serializer.isArrayEnd())
			{
				Entity& entity = m_entities.emplace();
				serializer.deserializeArrayItem(entity, 0);
			}
			serializer.deserializeArrayEnd();
		}


		bool merge(IEditorCommand&) override { return false; }


		uint32 getType() override
		{
			static const uint32 hash = crc32("add_component");
			return hash;
		}


		bool execute() override
		{
			const Array<IScene*>& scenes = m_editor.getScenes();

			for (int j = 0; j < m_entities.size(); ++j)
			{
				for (int i = 0; i < scenes.size(); ++i)
				{
					ComponentUID cmp(
						m_entities[j],
						m_type,
						scenes[i],
						scenes[i]->createComponent(m_type, m_entities[j]));
					if (cmp.isValid())
					{
						break;
					}
				}
			}
			return true;
		}


		void undo() override
		{
			for (int i = 0; i < m_entities.size(); ++i)
			{
				const ComponentUID& cmp =
					m_editor.getComponent(m_entities[i], m_type);
				cmp.scene->destroyComponent(cmp.index, cmp.type);
			}
		}


	private:
		uint32 m_type;
		Array<Entity> m_entities;
		WorldEditorImpl& m_editor;
	};


	class DestroyEntitiesCommand : public IEditorCommand
	{
	public:
		explicit DestroyEntitiesCommand(WorldEditor& editor)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
			, m_entities(editor.getAllocator())
			, m_positons_rotations(editor.getAllocator())
			, m_old_values(editor.getAllocator())
		{
		}


		DestroyEntitiesCommand(WorldEditorImpl& editor,
							   const Entity* entities,
							   int count)
			: m_editor(editor)
			, m_entities(editor.getAllocator())
			, m_positons_rotations(editor.getAllocator())
			, m_old_values(editor.getAllocator())
		{
			m_entities.reserve(count);
			m_positons_rotations.reserve(m_entities.size());
			for (int i = 0; i < count; ++i)
			{
				m_entities.push(entities[i]);
			}
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("count", m_entities.size());
			serializer.beginArray("entities");
			for (int i = 0; i < m_entities.size(); ++i)
			{
				serializer.serializeArrayItem(m_entities[i]);
				serializer.serializeArrayItem(
					m_positons_rotations[i].m_position.x);
				serializer.serializeArrayItem(
					m_positons_rotations[i].m_position.y);
				serializer.serializeArrayItem(
					m_positons_rotations[i].m_position.z);
				serializer.serializeArrayItem(
					m_positons_rotations[i].m_rotation.x);
				serializer.serializeArrayItem(
					m_positons_rotations[i].m_rotation.y);
				serializer.serializeArrayItem(
					m_positons_rotations[i].m_rotation.z);
				serializer.serializeArrayItem(
					m_positons_rotations[i].m_rotation.w);
			}
			serializer.endArray();
		}


		void deserialize(JsonSerializer& serializer) override
		{
			int count;
			serializer.deserialize("count", count, 0);
			serializer.deserializeArrayBegin("entities");
			m_entities.resize(count);
			m_positons_rotations.resize(count);
			for (int i = 0; i < count; ++i)
			{
				serializer.deserializeArrayItem(m_entities[i], 0);
				serializer.deserializeArrayItem(
					m_positons_rotations[i].m_position.x, 0);
				serializer.deserializeArrayItem(
					m_positons_rotations[i].m_position.y, 0);
				serializer.deserializeArrayItem(
					m_positons_rotations[i].m_position.z, 0);
				serializer.deserializeArrayItem(
					m_positons_rotations[i].m_rotation.x, 0);
				serializer.deserializeArrayItem(
					m_positons_rotations[i].m_rotation.y, 0);
				serializer.deserializeArrayItem(
					m_positons_rotations[i].m_rotation.z, 0);
				serializer.deserializeArrayItem(
					m_positons_rotations[i].m_rotation.w, 0);
			}
			serializer.deserializeArrayEnd();
		}


		bool execute() override
		{
			Universe* universe = m_editor.getUniverse();
			m_positons_rotations.clear();
			m_old_values.clear();
			for (int i = 0; i < m_entities.size(); ++i)
			{
				const WorldEditor::ComponentList& cmps =
					m_editor.getComponents(m_entities[i]);
				PositionRotation pos_rot;
				pos_rot.m_position = universe->getPosition(m_entities[i]);
				pos_rot.m_rotation = universe->getRotation(m_entities[i]);
				m_positons_rotations.push(pos_rot);
				m_old_values.write((int)cmps.size());
				for (int j = cmps.size() - 1; j >= 0; --j)
				{
					m_old_values.write(cmps[j].type);
					Array<IPropertyDescriptor*>& props =
						PropertyRegister::getDescriptors(cmps[j].type);
					for (int k = 0; k < props.size(); ++k)
					{
						props[k]->get(cmps[j], -1, m_old_values);
					}
					cmps[j].scene->destroyComponent(cmps[j].index, cmps[j].type);
				}

				universe->destroyEntity(Entity(m_entities[i]));
			}
			return true;
		}


		bool merge(IEditorCommand&) override { return false; }


		void undo() override
		{
			Universe* universe = m_editor.getUniverse();
			const Array<IScene*>& scenes = m_editor.getScenes();
			InputBlob blob(m_old_values);
			for (int i = 0; i < m_entities.size(); ++i)
			{
				Entity new_entity = universe->createEntity(
					m_positons_rotations[i].m_position, m_positons_rotations[i].m_rotation);
				int cmps_count;
				blob.read(cmps_count);
				for (int j = cmps_count - 1; j >= 0; --j)
				{
					ComponentUID::Type cmp_type;
					blob.read(cmp_type);
					ComponentUID new_component;
					for (int i = 0; i < scenes.size(); ++i)
					{
						new_component.index = scenes[i]->createComponent(cmp_type, new_entity);
						new_component.entity = new_entity;
						new_component.scene = scenes[i];
						new_component.type = cmp_type;
						if (new_component.isValid())
						{
							break;
						}
					}

					Array<IPropertyDescriptor*>& props = PropertyRegister::getDescriptors(cmp_type);

					for (int k = 0; k < props.size(); ++k)
					{
						props[k]->set(new_component, -1, blob);
					}
				}
			}
		}


		uint32 getType() override
		{
			static const uint32 hash = crc32("destroy_entities");
			return hash;
		}


	private:
		class PositionRotation
		{
		public:
			Vec3 m_position;
			Quat m_rotation;
		};


	private:
		WorldEditorImpl& m_editor;
		Array<Entity> m_entities;
		Array<PositionRotation> m_positons_rotations;
		OutputBlob m_old_values;
	};


	class DestroyComponentCommand : public IEditorCommand
	{
	public:
		explicit DestroyComponentCommand(WorldEditor& editor)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
			, m_old_values(editor.getAllocator())
		{
		}


		DestroyComponentCommand(WorldEditorImpl& editor,
								const ComponentUID& component)
			: m_component(component)
			, m_editor(editor)
			, m_old_values(editor.getAllocator())
		{
		}


		void serialize(JsonSerializer& serializer)
		{
			serializer.serialize("entity", m_component.entity);
			serializer.serialize("component", m_component.index);
			serializer.serialize("component_type", m_component.type);
		}


		void deserialize(JsonSerializer& serializer)
		{
			serializer.deserialize("entity", m_component.entity, 0);
			serializer.deserialize("component", m_component.index, 0);
			serializer.deserialize("component_type", m_component.type, 0);
			m_component.scene =
				m_editor.getSceneByComponentType(m_component.type);
		}


		void undo() override
		{
			uint32 template_hash = m_editor.m_template_system->getTemplate(m_component.entity);
			const Array<IScene*>& scenes = m_editor.getScenes();

			if (template_hash == 0)
			{
				for (int i = 0; i < scenes.size(); ++i)
				{
					ComponentIndex cmp =
						scenes[i]->createComponent(m_component.type, m_component.entity);
					if (cmp != INVALID_COMPONENT)
					{
						m_component.index = cmp;
						m_component.scene = scenes[i];
						break;
					}
				}
				InputBlob blob(m_old_values);
				const Array<IPropertyDescriptor*>& props =
					PropertyRegister::getDescriptors(m_component.type);
				for (int i = 0; i < props.size(); ++i)
				{
					props[i]->set(m_component, -1, blob);
				}
			}
			else
			{
				const Array<Entity>& entities =
					m_editor.m_template_system->getInstances(template_hash);
				for (int entity_index = 0, c = entities.size(); entity_index < c; ++entity_index)
				{
					for (int scene_index = 0; scene_index < scenes.size(); ++scene_index)
					{
						ComponentUID cmp_new(entities[entity_index],
							m_component.type,
							scenes[scene_index],
							scenes[scene_index]->createComponent(
								m_component.type, entities[entity_index]));
						if (cmp_new.isValid())
						{
							InputBlob blob(m_old_values);
							const Array<IPropertyDescriptor*>& props =
								PropertyRegister::getDescriptors(m_component.type);
							for (int i = 0; i < props.size(); ++i)
							{
								props[i]->set(cmp_new, -1, blob);
							}
						}
					}
				}
			}
		}


		bool merge(IEditorCommand&) override { return false; }


		uint32 getType() override
		{
			static const uint32 hash = crc32("destroy_component");
			return hash;
		}


		bool execute() override
		{
			Array<IPropertyDescriptor*>& props = PropertyRegister::getDescriptors(m_component.type);
			for (int i = 0; i < props.size(); ++i)
			{
				props[i]->get(m_component, -1, m_old_values);
			}
			uint32 template_hash =
				m_editor.getEntityTemplateSystem().getTemplate(m_component.entity);
			if (template_hash)
			{
				const Array<Entity>& instances =
					m_editor.m_template_system->getInstances(template_hash);
				for (int i = 0; i < instances.size(); ++i)
				{
					ComponentUID cmp = m_editor.getComponent(instances[i], m_component.type);
					if (cmp.isValid())
					{
						cmp.scene->destroyComponent(cmp.index, cmp.type);
					}
				}
			}
			else
			{
				m_component.scene->destroyComponent(m_component.index, m_component.type);
			}
			return true;
		}

	private:
		ComponentUID m_component;
		WorldEditorImpl& m_editor;
		OutputBlob m_old_values;
	};


	class AddEntityCommand : public IEditorCommand
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
			if (m_entity < 0)
			{
				m_entity = m_editor.getUniverse()->createEntity(m_position, Quat(0, 0, 0, 1));
			}
			else
			{
				m_editor.getUniverse()->createEntity(m_entity);
				m_editor.getUniverse()->setPosition(m_entity, m_position);
			}
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
		}


		bool merge(IEditorCommand&) override { return false; }


		uint32 getType() override
		{
			static const uint32 hash = crc32("add_entity");
			return hash;
		}


		Entity getEntity() const { return m_entity; }


	private:
		WorldEditorImpl& m_editor;
		Entity m_entity;
		Vec3 m_position;
	};

public:
	IAllocator& getAllocator() override { return m_allocator; }


	IScene* getSceneByComponentType(uint32 hash) override
	{
		for (auto* scene : m_universe->getScenes())
		{
			if (scene->ownComponentType(hash))
			{
				return scene;
			}
		}
		return nullptr;
	}


	IScene* getScene(uint32 hash) override
	{
		for (auto* scene : m_universe->getScenes())
		{
			if (crc32(scene->getPlugin().getName()) == hash)
			{
				return scene;
			}
		}
		return nullptr;
	}

	
	Universe* getUniverse() override
	{
		return m_universe; 
	}


	Engine& getEngine() override { return *m_engine; }


	void showGizmos()
	{
		if (m_selected_entities.empty()) return;

		ComponentUID camera_cmp = getComponent(m_camera, CAMERA_HASH);
		RenderScene* scene = static_cast<RenderScene*>(camera_cmp.scene);
		Universe* universe = getUniverse();

		if (m_selected_entities.size() > 1)
		{
			AABB aabb = m_render_interface->getEntityAABB(*universe, m_selected_entities[0]);
			for (int i = 1; i < m_selected_entities.size(); ++i)
			{
				AABB entity_aabb =
					m_render_interface->getEntityAABB(*universe, m_selected_entities[i]);
				aabb.merge(entity_aabb);
			}

			scene->addDebugCube(aabb.min, aabb.max, 0xffffff00, 0);
			return;
		}

		const Array<ComponentUID>& cmps = getComponents(m_selected_entities[0]);

		for (auto cmp : cmps)
		{
			for (auto* plugin : m_plugins)
			{
				if (plugin->showGizmo(cmp))
					break;
			}
		}
	}


	void createEditorLines()
	{
		PROFILE_FUNCTION();
		showGizmos();

		RenderScene* scene =
			static_cast<RenderScene*>(m_universe->getScene(crc32("renderer")));
		m_measure_tool->createEditorLines(*scene);
	}


	void updateGoTo()
	{
		if (m_camera < 0 || !m_go_to_parameters.m_is_active) return;

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
		EntityTemplateSystem::destroy(m_template_system);

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


	void snapEntities(const RayCastModelHit& hit)
	{
		Vec3 hit_pos = hit.m_origin + hit.m_dir * hit.m_t;
		Lumix::Array<Vec3> positions(m_allocator);
		Lumix::Array<Quat> rotations(m_allocator);
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
				mtx.getRotation(rotations.emplace());
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
			ComponentUID camera_cmp = getComponent(m_camera, CAMERA_HASH);
			if (camera_cmp.isValid())
			{
				RenderScene* scene = static_cast<RenderScene*>(camera_cmp.scene);
				scene->getRay(camera_cmp.index, (float)x, (float)y, origin, dir);
				RayCastModelHit hit = scene->castRay(origin, dir, INVALID_COMPONENT);
				if (m_gizmo->isActive()) return;

				if(m_is_snap_mode && !m_selected_entities.empty() && hit.m_is_hit)
				{
					snapEntities(hit);
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
				else if (hit.m_is_hit)
				{
					onEntityMouseDown(hit, x, y);
				}
			}
		}
	}


	void addPlugin(Plugin& plugin) override { m_plugins.push(&plugin); }


	void removePlugin(Plugin& plugin) override
	{
		m_plugins.eraseItemFast(&plugin);
	}


	void onEntityMouseDown(const RayCastModelHit& hit, int x, int y)
	{
		Entity entity = hit.m_entity;
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
		if (save_path) m_universe_path = path;
	}


	void save(FS::IFile& file)
	{
		ASSERT(m_universe);

		OutputBlob blob(m_allocator);
		blob.reserve(m_universe->getEntityCount() * 100);

		Header header = {0xffffFFFF, (int)SerializedVersion::LATEST, 0, 0};
		blob.write(header);
		int hashed_offset = sizeof(header);

		header.engine_hash = m_engine->serialize(*m_universe, blob);
		m_template_system->serialize(blob);
		m_entity_groups.serialize(blob);
		header.hash = crc32((const uint8*)blob.getData() + hashed_offset, blob.getPos() - hashed_offset);
		*(Header*)blob.getData() = header;

		g_log_info.log("editor") << "Universe saved";
		file.write(blob.getData(), blob.getPos());
	}


	void setRenderInterface(class RenderInterface* interface) override
	{
		m_render_interface = interface;
		m_editor_icons->setRenderInterface(m_render_interface);
	}


	RenderInterface* getRenderInterface() override
	{
		return m_render_interface;
	}


	void snapDown() override
	{
		if (m_selected_entities.empty()) return;

		Array<Vec3> new_positions(m_allocator);
		RenderScene* scene = static_cast<RenderScene*>(getScene(crc32("renderer")));
		Universe* universe = getUniverse();

		for (int i = 0; i < m_selected_entities.size(); ++i)
		{
			Entity entity = m_selected_entities[i];

			ComponentUID renderable = getComponent(m_selected_entities[i], RENDERABLE_HASH);
			RayCastModelHit hit =
				scene->castRay(universe->getPosition(entity), Vec3(0, -1, 0), renderable.index);
			if (hit.m_is_hit)
			{
				new_positions.push(hit.m_origin + hit.m_dir * hit.m_t);
			}
			else
			{
				RayCastModelHit hit =
					scene->castRay(universe->getPosition(entity), Vec3(0, 1, 0), renderable.index);
				if (hit.m_is_hit)
				{
					new_positions.push(hit.m_origin + hit.m_dir * hit.m_t);
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


	Entity addEntity() override
	{
		ComponentUID cmp = getComponent(m_camera, CAMERA_HASH);
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		float width = scene->getCameraScreenWidth(cmp.index);
		float height = scene->getCameraScreenHeight(cmp.index);
		return addEntityAt((int)width >> 1, (int)height >> 1);
	}


	Entity addEntityAt(int camera_x, int camera_y) override
	{
		ComponentUID camera_cmp = getComponent(m_camera, CAMERA_HASH);
		RenderScene* scene = static_cast<RenderScene*>(camera_cmp.scene);
		Universe* universe = getUniverse();
		Vec3 origin;
		Vec3 dir;

		scene->getRay(
			camera_cmp.index, (float)camera_x, (float)camera_y, origin, dir);
		RayCastModelHit hit = scene->castRay(origin, dir, INVALID_COMPONENT);
		Vec3 pos;
		if (hit.m_is_hit)
		{
			pos = hit.m_origin + hit.m_dir * hit.m_t;
		}
		else
		{
			pos = universe->getPosition(m_camera) +
				  universe->getRotation(m_camera) * Vec3(0, 0, -2);
		}
		AddEntityCommand* command = LUMIX_NEW(m_allocator, AddEntityCommand)(*this, pos);
		executeCommand(command);

		return command->getEntity();
	}


	Vec3 getCameraRaycastHit() override
	{
		ComponentUID camera_cmp = getComponent(m_camera, CAMERA_HASH);
		RenderScene* scene = static_cast<RenderScene*>(camera_cmp.scene);
		Universe* universe = getUniverse();
		float camera_x = scene->getCameraScreenWidth(camera_cmp.index);
		float camera_y = scene->getCameraScreenHeight(camera_cmp.index);
		camera_x *= 0.5f;
		camera_y *= 0.5f;

		Vec3 origin;
		Vec3 dir;
		scene->getRay(camera_cmp.index, (float)camera_x, (float)camera_y, origin, dir);
		RayCastModelHit hit = scene->castRay(origin, dir, INVALID_COMPONENT);
		Vec3 pos;
		if (hit.m_is_hit)
		{
			pos = hit.m_origin + hit.m_dir * hit.m_t;
		}
		else
		{
			pos = universe->getPosition(m_camera) + universe->getRotation(m_camera) * Vec3(0, 0, -2);
		}
		return pos;
	}


	void setEntitiesScales(const Entity* entities, const float* scales, int count) override
	{
		if (count <= 0) return;

		IEditorCommand* command =
			LUMIX_NEW(m_allocator, ScaleEntityCommand)(*this, entities, scales, count, m_allocator);
		executeCommand(command);
	}


	void setEntitiesRotations(const Entity* entities,
		const Quat* rotations,
		int count) override
	{
		ASSERT(entities && rotations);
		if (count <= 0) return;

		Universe* universe = getUniverse();
		Array<Vec3> positions(m_allocator);
		for (int i = 0; i < count; ++i)
		{
			positions.push(universe->getPosition(entities[i]));
		}
		IEditorCommand* command = LUMIX_NEW(m_allocator, MoveEntityCommand)(
			*this, entities, &positions[0], rotations, count, m_allocator);
		executeCommand(command);
	}


	void setEntitiesPositions(const Entity* entities,
		const Vec3* positions,
		int count) override
	{
		ASSERT(entities && positions);
		if (count <= 0) return;

		Universe* universe = getUniverse();
		Array<Quat> rots(m_allocator);
		for (int i = 0; i < count; ++i)
		{
			rots.push(universe->getRotation(entities[i]));
		}
		IEditorCommand* command = LUMIX_NEW(m_allocator, MoveEntityCommand)(
			*this, entities, positions, &rots[0], count, m_allocator);
		executeCommand(command);
	}


	void setEntitiesPositionsAndRotations(const Entity* entities,
		const Vec3* positions,
		const Quat* rotations,
		int count) override
	{
		if (count <= 0) return;
		IEditorCommand* command = LUMIX_NEW(m_allocator, MoveEntityCommand)(
			*this, entities, positions, rotations, count, m_allocator);
		executeCommand(command);
	}


	void setEntityName(Entity entity, const char* name) override
	{
		if (entity >= 0)
		{
			IEditorCommand* command =
				LUMIX_NEW(m_allocator, SetEntityNameCommand)(*this, entity, name);
			executeCommand(command);
		}
	}


	void beginCommandGroup(uint32 type) override
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
			static const uint32 end_group_hash = crc32("end_group");
			if(m_undo_stack[m_undo_index]->getType() == end_group_hash)
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
		auto* cmd = LUMIX_NEW(m_allocator, EndGroupCommand);
		cmd->group_type = m_current_group_type;
		m_undo_stack.push(cmd);
		++m_undo_index;
	}


	void executeCommand(IEditorCommand* command) override
	{
		m_is_universe_changed = true;
		if (m_undo_index >= 0 && command->getType() == m_undo_stack[m_undo_index]->getType())
		{
			if (command->merge(*m_undo_stack[m_undo_index]))
			{
				m_undo_stack[m_undo_index]->execute();
				LUMIX_DELETE(m_allocator, command);
				return;
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
		}
		else
		{
			LUMIX_DELETE(m_allocator, command);
		}
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
			auto& fs = m_engine->getFileSystem();
			m_game_mode_file = fs.open(fs.getMemoryDevice(), Lumix::Path(""), FS::Mode::WRITE);
			save(*m_game_mode_file);
			m_is_game_mode = true;
			m_engine->startGame(*m_universe);
		}
	}


	void stopGameMode(bool reload)
	{
		ASSERT(m_universe);
		m_engine->stopGame(*m_universe);
		selectEntities(nullptr, 0);
		m_editor_icons->clear();
		m_is_game_mode = false;
		if (reload)
		{
			m_game_mode_file->seek(FS::SeekMode::BEGIN, 0);
			load(*m_game_mode_file);
		}
		m_engine->getFileSystem().close(*m_game_mode_file);
		m_game_mode_file = nullptr;
		if (reload)
		{
			m_universe_loaded.invoke();
		}
	}


	EntityTemplateSystem& getEntityTemplateSystem() override
	{
		return *m_template_system;
	}


	void showEntities(const Entity* entities, int count) override
	{
		for (int i = 0, c = count; i < c; ++i)
		{
			ComponentUID cmp = getComponent(entities[i], RENDERABLE_HASH);
			if (cmp.isValid())
			{
				static_cast<RenderScene*>(cmp.scene)->showRenderable(cmp.index);
			}
		}
	}


	void showSelectedEntities() override
	{
		for (int i = 0, c = m_selected_entities.size(); i < c; ++i)
		{
			ComponentUID cmp =
				getComponent(m_selected_entities[i], RENDERABLE_HASH);
			if (cmp.isValid())
			{
				static_cast<RenderScene*>(cmp.scene)->showRenderable(cmp.index);
			}
		}
	}


	void hideEntities(const Entity* entities, int count) override
	{
		for (int i = 0, c = count; i < c; ++i)
		{
			ComponentUID cmp = getComponent(entities[i], RENDERABLE_HASH);
			if (cmp.isValid())
			{
				static_cast<RenderScene*>(cmp.scene)->hideRenderable(cmp.index);
			}
		}
	}


	void hideSelectedEntities() override
	{
		for (int i = 0, c = m_selected_entities.size(); i < c; ++i)
		{
			ComponentUID cmp = getComponent(m_selected_entities[i], RENDERABLE_HASH);
			if (cmp.isValid())
			{
				static_cast<RenderScene*>(cmp.scene)->hideRenderable(cmp.index);
			}
		}
	}


	void copyEntities() override
	{
		if (m_selected_entities.empty()) return;

		m_copy_buffer.clear();
		m_copy_buffer.write(m_selected_entities.size());
		for (auto entity : m_selected_entities)
		{
			auto mtx = m_universe->getMatrix(entity);
			m_copy_buffer.write(mtx);

			const WorldEditor::ComponentList& cmps = getComponents(entity);
			int32 count = cmps.size();
			m_copy_buffer.write(count);
			for (int i = 0; i < count; ++i)
			{
				uint32 cmp_type = cmps[i].type;
				m_copy_buffer.write(cmp_type);
				Array<IPropertyDescriptor*>& props =
					PropertyRegister::getDescriptors(cmps[i].type);
				int32 prop_count = props.size();
				for (int j = 0; j < prop_count; ++j)
				{
					props[j]->get(cmps[i], -1, m_copy_buffer);
				}
			}
		}
	}


	bool canPasteEntities() const override
	{
		return m_copy_buffer.getPos() > 0;
	}


	void pasteEntities() override
	{
		PasteEntityCommand* command =
			LUMIX_NEW(m_allocator, PasteEntityCommand)(*this, m_copy_buffer);
		executeCommand(command);
	}


	void cloneComponent(const ComponentUID& src, Entity entity) override
	{
		ComponentUID clone = ComponentUID::INVALID;

		const Array<IScene*>& scenes = getScenes();
		for (int i = 0; i < scenes.size(); ++i)
		{
			clone = ComponentUID(entity,
								 src.type,
								 scenes[i],
								 scenes[i]->createComponent(src.type, entity));
			if (clone.isValid())
			{
				break;
			}
		}

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


	bool canRemove(const ComponentUID& cmp) override
	{
		auto& cmps = getComponents(cmp.entity);
		for (auto& possible_dependent : cmps)
		{
			if (PropertyRegister::componentDepends(possible_dependent.type, cmp.type)) return false;
		}
		return true;
	}


	void destroyComponent(const ComponentUID& component) override
	{
		if (component.entity == m_camera && component.type == CAMERA_HASH)
		{
			g_log_error.log("Editor")
				<< "Can not destroy component from the editing camera";
			return;
		}

		if (component.isValid())
		{
			IEditorCommand* command =
				LUMIX_NEW(m_allocator, DestroyComponentCommand)(*this, component);
			executeCommand(command);
		}
	}


	void addComponent(uint32 type_crc) override
	{
		if (!m_selected_entities.empty())
		{
			IEditorCommand* command =
				LUMIX_NEW(m_allocator, AddComponentCommand)(*this, m_selected_entities, type_crc);
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
		Vec3 dir = camera_rot * Vec3(0, 0, 1);
		m_go_to_parameters.m_to = universe->getPosition(m_selected_entities[0]) + dir * 10;
		float len = (m_go_to_parameters.m_to - m_go_to_parameters.m_from).length();
		m_go_to_parameters.m_speed = Math::maximum(100.0f / (len > 0 ? len : 1), 2.0f);
		m_go_to_parameters.m_from_rot = m_go_to_parameters.m_to_rot = camera_rot;
	}


	void loadUniverse(const Path& path) override
	{
		if (m_is_game_mode) stopGameMode(false);
		m_universe_path = path;
		g_log_info.log("Editor") << "Loading universe " << path << "...";
		FS::FileSystem& fs = m_engine->getFileSystem();
		FS::ReadCallback file_read_cb;
		file_read_cb.bind<WorldEditorImpl, &WorldEditorImpl::loadMap>(this);
		fs.openAsync(fs.getDefaultDevice(), path, FS::Mode::OPEN_AND_READ, file_read_cb);
	}


	void loadMap(FS::IFile& file, bool success)
	{
		ASSERT(success);
		if (success)
		{
			resetAndLoad(file);
			char path[MAX_PATH_LENGTH];
			copyString(path, sizeof(path), m_universe_path.c_str());
			catString(path, sizeof(path), ".lst");
			copyFile(m_universe_path.c_str(), path);
		}
		m_universe_loaded.invoke();
	}


	void newUniverse() override
	{
		m_universe_path = "";
		destroyUniverse();
		createUniverse(true);
		g_log_info.log("Editor") << "Universe created.";
	}


	enum class SerializedVersion : int
	{
		ENTITY_GROUPS,

		LATEST
	};


	#pragma pack(1)
		struct Header
		{
			uint32 magic;
			int version;
			uint32 hash;
			uint32 engine_hash;
		};
	#pragma pack()


	void load(FS::IFile& file)
	{
		m_is_loading = true;
		ASSERT(file.getBuffer());
		m_components.clear();
		m_components.reserve(5000);
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
		uint32 hash = 0;
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
			uint32 engine_hash = 0;
			blob.read(engine_hash);
		}
		if (crc32((const uint8*)blob.getData() + hashed_offset, blob.getSize() - hashed_offset) !=
			hash)
		{
			Timer::destroy(timer);
			g_log_error.log("Editor") << "Corrupted file.";
			newUniverse();
			m_is_loading = false;
			return;
		}
		if (m_engine->deserialize(*m_universe, blob))
		{
			m_template_system->deserialize(blob);
			if (header.version > (int)SerializedVersion::ENTITY_GROUPS)
			{
				m_entity_groups.deserialize(blob);
			}
			else
			{
				m_entity_groups.allEntitiesToDefault();
			}
			auto* render_scene = static_cast<RenderScene*>(getScene(crc32("renderer")));

			m_camera = render_scene->getCameraEntity(render_scene->getCameraInSlot("editor"));

			g_log_info.log("Editor") << "Universe parsed in " << timer->getTimeSinceStart()
									 << " seconds";
		}
		else
		{
			newUniverse();
		}
		Timer::destroy(timer);
		m_is_loading = false;
	}


	Array<ComponentUID>& getComponents(Entity entity) override
	{
		int cmps_index = m_components.find(entity);
		if (cmps_index < 0)
		{
			m_components.insert(entity, Array<ComponentUID>(m_allocator));
			cmps_index = m_components.find(entity);
		}
		return m_components.at(cmps_index);
	}


	ComponentUID getComponent(Entity entity, uint32 type) override
	{
		const Array<ComponentUID>& cmps = getComponents(entity);
		for (int i = 0; i < cmps.size(); ++i)
		{
			if (cmps[i].type == type)
			{
				return cmps[i];
			}
		}
		return ComponentUID::INVALID;
	}


	void resetAndLoad(FS::IFile& file)
	{
		destroyUniverse();
		createUniverse(false);
		load(file);
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
		return getComponent(m_camera, CAMERA_HASH);
	}


	WorldEditorImpl(const char* base_path, Engine& engine, IAllocator& allocator)
		: m_allocator(allocator)
		, m_engine(nullptr)
		, m_components(m_allocator)
		, m_entity_name_set(m_allocator)
		, m_entity_selected(m_allocator)
		, m_universe_destroyed(m_allocator)
		, m_universe_created(m_allocator)
		, m_universe_loaded(m_allocator)
		, m_property_set(m_allocator)
		, m_selected_entities(m_allocator)
		, m_editor_icons(nullptr)
		, m_plugins(m_allocator)
		, m_undo_stack(m_allocator)
		, m_copy_buffer(m_allocator)
		, m_camera(INVALID_ENTITY)
		, m_editor_command_creators(m_allocator)
		, m_is_loading(false)
		, m_universe_path("")
		, m_universe(nullptr)
		, m_is_orbit(false)
		, m_gizmo_use_step(false)
		, m_is_additive_selection(false)
		, m_entity_groups(m_allocator)
		, m_mouse_sensitivity(200, 200)
	{
		for (auto& i : m_is_mouse_down) i = false;
		for (auto& i : m_is_mouse_click) i = false;
		m_go_to_parameters.m_is_active = false;
		m_undo_index = -1;
		m_mouse_handling_plugin = nullptr;
		m_is_game_mode = false;
		m_is_snap_mode = false;
		m_measure_tool = LUMIX_NEW(m_allocator, MeasureTool)();
		addPlugin(*m_measure_tool);

		m_engine = &engine;

		const char* plugins[] = {"renderer", "animation", "audio", "physics", "lua_script"};

		PluginManager& plugin_manager = m_engine->getPluginManager();
		for (auto* plugin_name : plugins)
		{
			if (!plugin_manager.load(plugin_name))
			{
				g_log_info.log("Editor") << plugin_name << " plugin has not been loaded";
			}
		}

		createUniverse(true);
		m_template_system = EntityTemplateSystem::create(*this);

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
			crc32("set_property"), &WorldEditorImpl::constructEditorCommand<SetPropertyCommand>);
		m_editor_command_creators.insert(
			crc32("add_component"), &WorldEditorImpl::constructEditorCommand<AddComponentCommand>);
		m_editor_command_creators.insert(
			crc32("destroy_entities"), &WorldEditorImpl::constructEditorCommand<DestroyEntitiesCommand>);
		m_editor_command_creators.insert(
			crc32("destroy_component"), &WorldEditorImpl::constructEditorCommand<DestroyComponentCommand>);
		m_editor_command_creators.insert(
			crc32("add_entity"), &WorldEditorImpl::constructEditorCommand<AddEntityCommand>);

		m_gizmo = Gizmo::create(*this);
		m_editor_icons = EditorIcons::create(*this);
	}


	void navigate(float forward, float right, float up, float speed) override
	{
		Universe* universe = getUniverse();
		Vec3 pos = universe->getPosition(m_camera);
		Quat rot = universe->getRotation(m_camera);

		right = m_is_orbit ? 0 : right;

		pos += rot * Vec3(0, 0, -1) * forward * speed;
		pos += rot * Vec3(1, 0, 0) * right * speed;
		pos += rot * Vec3(0, 1, 0) * up * speed;
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


	void removeArrayPropertyItem(const ComponentUID& cmp,
		int index,
		IArrayDescriptor& property) override
	{
		if (cmp.isValid())
		{
			IEditorCommand* command =
				LUMIX_NEW(m_allocator, RemoveArrayPropertyItemCommand)(*this, cmp, index, property);
			executeCommand(command);
		}
	}


	void setProperty(uint32 component,
		int index,
		IPropertyDescriptor& property,
		const void* data,
		int size) override
	{

		ASSERT(m_selected_entities.size() == 1);
		uint32 component_hash = component;
		ComponentUID cmp = getComponent(m_selected_entities[0], component_hash);
		if (cmp.isValid())
		{
			static const uint32 SLOT_HASH = crc32("Slot");
			if (component == CAMERA_HASH && property.getNameHash() == SLOT_HASH)
			{
				if (static_cast<RenderScene*>(cmp.scene)->getCameraEntity(cmp.index) == m_camera)
				{
					return;
				}
			}

			IEditorCommand* command = LUMIX_NEW(m_allocator, SetPropertyCommand)(
				*this, cmp.entity, cmp.type, index, property, data, size);
			executeCommand(command);
		}
	}


	bool isOrbitCamera() const override
	{
		return m_is_orbit;
	}


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

		pos += rot * Vec3(x, 0, 0);
		pos += rot * Vec3(0, -y, 0);

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

		float yaw =
			-Math::signum(x) * (Math::pow(Math::abs((float)x / m_mouse_sensitivity.x), 1.2f));
		Quat yaw_rot(Vec3(0, 1, 0), yaw);
		rot = rot * yaw_rot;
		rot.normalize();

		Vec3 pitch_axis = rot * Vec3(1, 0, 0);
		float pitch =
			-Math::signum(y) * (Math::pow(Math::abs((float)y / m_mouse_sensitivity.y), 1.2f));
		Quat pitch_rot(pitch_axis, pitch);
		rot = rot * pitch_rot;
		rot.normalize();

		if (m_is_orbit && !m_selected_entities.empty())
		{
			Vec3 dir = rot * Vec3(0, 0, 1);
			Vec3 entity_pos = universe->getPosition(m_selected_entities[0]);
			Vec3 nondelta_pos = pos;

			nondelta_pos -= old_rot * Vec3(0, -1, 0) * m_orbit_delta.y;
			nondelta_pos -= old_rot * Vec3(1, 0, 0) * m_orbit_delta.x;

			float dist = (entity_pos - nondelta_pos).length();
			pos = entity_pos + dir * dist;
			pos += rot * Vec3(1, 0, 0) * m_orbit_delta.x;
			pos += rot * Vec3(0, -1, 0) * m_orbit_delta.y;
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


	void selectEntitiesWithSameMesh() override
	{
		if (m_selected_entities.size() == 1)
		{
			ComponentUID cmp = getComponent(m_selected_entities[0], RENDERABLE_HASH);
			if (cmp.isValid())
			{
				Array<Entity> entities(m_allocator);

				RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
				Model* model = scene->getRenderableModel(cmp.index);
				ComponentIndex renderable = scene->getFirstRenderable();
				while (renderable >= 0)
				{
					if (model == scene->getRenderableModel(renderable))
					{
						entities.push(scene->getRenderableEntity(renderable));
					}
					renderable = scene->getNextRenderable(renderable);
				}

				selectEntities(&entities[0], entities.size());
			}
		}
	}


	void onComponentAdded(const ComponentUID& cmp)
	{
		getComponents(cmp.entity).push(cmp);
	}

	void onComponentDestroyed(const ComponentUID& cmp)
	{
		getComponents(cmp.entity).eraseItemFast(cmp);
	}

	void onEntityDestroyed(Entity entity)
	{
		m_components.erase(entity);
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
		m_components.clear();
		selectEntities(nullptr, 0);
		m_camera = INVALID_ENTITY;
		m_engine->destroyUniverse(*m_universe);
		m_universe = nullptr;
	}


	Path getUniversePath() const override { return m_universe_path; }


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


	DelegateList<void()>& universeLoaded() override
	{
		return m_universe_loaded;
	}

	
	DelegateList<void(ComponentUID, const IPropertyDescriptor&)>&
	propertySet() override
	{
		return m_property_set;
	}


	DelegateList<void(Entity, const char*)>& entityNameSet() override
	{
		return m_entity_name_set;
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


	ComponentUID createComponent(uint32 hash, Entity entity)
	{
		const Array<IScene*>& scenes = getScenes();
		ComponentUID cmp;
		for (int i = 0; i < scenes.size(); ++i)
		{
			cmp = ComponentUID(entity,
							   hash,
							   scenes[i],
							   scenes[i]->createComponent(hash, entity));

			if (cmp.isValid())
			{
				return cmp;
			}
		}
		return ComponentUID::INVALID;
	}


	void createUniverse(bool create_basic_entities)
	{
		ASSERT(!m_universe);

		m_is_universe_changed = false;
		destroyUndoStack();
		m_universe = &m_engine->createUniverse();
		Universe* universe = m_universe;

		universe->componentAdded().bind<WorldEditorImpl, &WorldEditorImpl::onComponentAdded>(this);
		universe->componentDestroyed()
			.bind<WorldEditorImpl, &WorldEditorImpl::onComponentDestroyed>(this);
		universe->entityDestroyed().bind<WorldEditorImpl, &WorldEditorImpl::onEntityDestroyed>(
			this);

		m_selected_entities.clear();
		m_universe_created.invoke();
		m_entity_groups.setUniverse(universe);

		if (create_basic_entities)
		{
			m_camera = universe->createEntity(Vec3(0, 0, -5), Quat(Vec3(0, 1, 0), -Math::PI));
			universe->setEntityName(m_camera, "editor_camera");
			ComponentUID cmp = createComponent(CAMERA_HASH, m_camera);
			ASSERT(cmp.isValid());
			RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
			scene->setCameraSlot(cmp.index, "editor");
		}
	}


	bool canUndo() const override
	{
		return m_undo_index < m_undo_stack.size() && m_undo_index >= 0;
	}


	bool canRedo() const override
	{
		return m_undo_index + 1 < m_undo_stack.size();
	}


	void undo() override
	{
		static const uint32 end_group_hash = crc32("end_group");
		static const uint32 begin_group_hash = crc32("begin_group");

		if (m_undo_index >= m_undo_stack.size() || m_undo_index < 0) return;

		if(m_undo_stack[m_undo_index]->getType() == end_group_hash)
		{
			--m_undo_index;
			while(m_undo_stack[m_undo_index]->getType() != begin_group_hash)
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
		static const uint32 end_group_hash = crc32("end_group");
		static const uint32 begin_group_hash = crc32("begin_group");

		if (m_undo_index + 1 >= m_undo_stack.size()) return;

		++m_undo_index;
		if(m_undo_stack[m_undo_index]->getType() == begin_group_hash)
		{
			++m_undo_index;
			while(m_undo_stack[m_undo_index]->getType() != end_group_hash)
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
		if (m_undo_stack.empty())
		{
			return;
		}
		FS::IFile* file = m_engine->getFileSystem().open(
			m_engine->getFileSystem().getDiskDevice(),
			path,
			FS::Mode::CREATE_AND_WRITE);
		if (file)
		{
			JsonSerializer serializer(
				*file, JsonSerializer::WRITE, path, m_allocator);
			serializer.beginObject();
			serializer.beginArray("commands");
			for (int i = 0; i < m_undo_stack.size(); ++i)
			{
				serializer.beginObject();
				serializer.serialize("undo_command_type",
									 m_undo_stack[i]->getType());
				m_undo_stack[i]->serialize(serializer);
				serializer.endObject();
			}
			serializer.endArray();
			serializer.endObject();
			m_engine->getFileSystem().close(*file);
		}
		else
		{
			g_log_error.log("Editor") << "Could not save commands to " << path;
		}
	}


	IEditorCommand* createEditorCommand(uint32 command_type) override
	{
		int index = m_editor_command_creators.find(command_type);
		if (index >= 0)
		{
			return m_editor_command_creators.at(index)(*this);
		}
		return nullptr;
	}


	void registerEditorCommandCreator(const char* command_type,
		EditorCommandCreator creator) override
	{
		m_editor_command_creators.insert(crc32(command_type), creator);
	}


	bool executeUndoStack(const Path& path) override
	{
		destroyUndoStack();
		m_undo_index = -1;
		FS::IFile* file = m_engine->getFileSystem().open(
			m_engine->getFileSystem().getDiskDevice(), path, FS::Mode::OPEN_AND_READ);
		if (file)
		{
			JsonSerializer serializer(*file, JsonSerializer::READ, path, m_allocator);
			serializer.deserializeObjectBegin();
			serializer.deserializeArrayBegin("commands");
			while (!serializer.isArrayEnd())
			{
				serializer.nextArrayItem();
				serializer.deserializeObjectBegin();
				uint32 type;
				serializer.deserialize("undo_command_type", type, 0);
				IEditorCommand* command = createEditorCommand(type);
				if (!command)
				{
					g_log_error.log("Editor") << "Unknown command " << type << " in " << path;
					destroyUndoStack();
					m_undo_index = -1;
					m_engine->getFileSystem().close(*file);
					return false;
				}
				command->deserialize(serializer);
				executeCommand(command);
				serializer.deserializeObjectEnd();
			}
			serializer.deserializeArrayEnd();
			serializer.deserializeObjectEnd();
			m_engine->getFileSystem().close(*file);
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


	bool runTest(const Path& undo_stack_path, const Path& result_universe_path) override
	{
		while (m_engine->getFileSystem().hasWork()) m_engine->getFileSystem().updateAsyncTransactions();
		newUniverse();
		executeUndoStack(undo_stack_path);
		while (m_engine->getFileSystem().hasWork()) m_engine->getFileSystem().updateAsyncTransactions();

		FS::IFile* file =
			m_engine->getFileSystem().open(m_engine->getFileSystem().getMemoryDevice(),
				Lumix::Path(""),
				FS::Mode::CREATE_AND_WRITE);
		if (!file)
		{
			return false;
		}
		FS::IFile* result_file =
			m_engine->getFileSystem().open(m_engine->getFileSystem().getDefaultDevice(),
				result_universe_path,
				FS::Mode::OPEN_AND_READ);
		if (!result_file)
		{
			m_engine->getFileSystem().close(*file);
			return false;
		}
		save(*file);
		bool is_same = file->size() > 8 && result_file->size() > 8 &&
					   *((const uint32*)result_file->getBuffer() + 3) ==
						   *((const uint32*)file->getBuffer() + 3);
		m_engine->getFileSystem().close(*result_file);
		m_engine->getFileSystem().close(*file);

		return is_same;
	}


	const Array<IScene*>& getScenes() const override
	{
		return m_universe->getScenes();
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

	struct ComponentType
	{
		explicit ComponentType(IAllocator& allocator)
			: m_name(allocator)
			, m_id(allocator)
		{
		}

		string m_name;
		string m_id;
	};

	Debug::Allocator m_allocator;
	GoToParameters m_go_to_parameters;
	Gizmo* m_gizmo;
	Array<Entity> m_selected_entities;
	MouseMode::Value m_mouse_mode;
	EditorIcons* m_editor_icons;
	float m_mouse_x;
	float m_mouse_y;
	Vec2 m_orbit_delta;
	Vec2 m_mouse_sensitivity;
	bool m_gizmo_use_step;
	AssociativeArray<Entity, Array<ComponentUID>> m_components;
	bool m_is_game_mode;
	bool m_is_orbit;
	bool m_is_additive_selection;
	bool m_is_snap_mode;
	FS::IFile* m_game_mode_file;
	Engine* m_engine;
	Entity m_camera;
	DelegateList<void()> m_universe_destroyed;
	DelegateList<void()> m_universe_created;
	DelegateList<void()> m_universe_loaded;
	DelegateList<void(ComponentUID, const IPropertyDescriptor&)> m_property_set;
	DelegateList<void(const Array<Entity>&)> m_entity_selected;
	DelegateList<void(Entity, const char*)> m_entity_name_set;
	bool m_is_mouse_down[3];
	bool m_is_mouse_click[3];

	Path m_universe_path;
	Array<Plugin*> m_plugins;
	MeasureTool* m_measure_tool;
	Plugin* m_mouse_handling_plugin;
	EntityTemplateSystem* m_template_system;
	Array<IEditorCommand*> m_undo_stack;
	AssociativeArray<uint32, EditorCommandCreator> m_editor_command_creators;
	int m_undo_index;
	OutputBlob m_copy_buffer;
	bool m_is_loading;
	Universe* m_universe;
	EntityGroups m_entity_groups;
	RenderInterface* m_render_interface;
	uint32 m_current_group_type;
	bool m_is_universe_changed;
};


WorldEditor* WorldEditor::create(const char* base_path, Engine& engine, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, WorldEditorImpl)(base_path, engine, allocator);
}


void WorldEditor::destroy(WorldEditor* editor, IAllocator& allocator)
{
	LUMIX_DELETE(allocator, static_cast<WorldEditorImpl*>(editor));
}


bool PasteEntityCommand::execute()
{
	InputBlob blob(m_blob.getData(), m_blob.getPos());
	Universe* universe = m_editor.getUniverse();
	
	int entity_count;
	blob.read(entity_count);
	m_entities.clear();
	m_entities.reserve(entity_count);
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
		Entity new_entity = universe->createEntity(Vec3(0, 0, 0), Quat(0, 0, 0, 1));
		universe->setMatrix(new_entity, mtx);
		int32 count;
		blob.read(count);
		for (int i = 0; i < count; ++i)
		{
			uint32 type;
			blob.read(type);
			ComponentUID cmp =
				static_cast<WorldEditorImpl&>(m_editor).createComponent(type, new_entity);
			Array<IPropertyDescriptor*>& props = PropertyRegister::getDescriptors(type);
			for (int j = 0; j < props.size(); ++j)
			{
				props[j]->set(cmp, -1, blob);
			}
		}
		m_entities.push(new_entity);
	}
	return true;
}


} // !namespace Lumix
