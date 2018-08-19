#include "world_editor.h"

#include "editor/editor_icon.h"
#include "editor/gizmo.h"
#include "editor/measure_tool.h"
#include "editor/platform_interface.h"
#include "editor/prefab_system.h"
#include "engine/array.h"
#include "engine/associative_array.h"
#include "engine/blob.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/delegate_list.h"
#include "engine/engine.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/os_file.h"
#include "engine/geometry.h"
#include "engine/iplugin.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/matrix.h"
#include "engine/path.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/serializer.h"
#include "engine/system.h"
#include "engine/timer.h"
#include "engine/universe/universe.h"
#include "engine/viewport.h"
#include "ieditor_command.h"
#include "render_interface.h"


namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("model_instance");
static const ComponentType CAMERA_TYPE = Reflection::getComponentType("camera");


static void load(ComponentUID cmp, int index, InputBlob& blob)
{
	int count = blob.read<int>();
	for (int i = 0; i < count; ++i)
	{
		u32 hash = blob.read<u32>();
		int size = blob.read<int>();
		const Reflection::PropertyBase* prop = Reflection::getProperty(cmp.type, hash);
		if (!prop)
		{
			blob.skip(size);
			continue;
		}

		prop->setValue(cmp, index, blob);
	}
}


struct BeginGroupCommand final : public IEditorCommand
{
	BeginGroupCommand() = default;
	explicit BeginGroupCommand(WorldEditor&) {}

	bool execute() override { return true; }
	void undo() override { ASSERT(false); }
	void serialize(JsonSerializer& serializer) override {}
	void deserialize(JsonDeserializer& serializer) override {}
	bool merge(IEditorCommand& command) override { ASSERT(false); return false; }
	const char* getType() override { return "begin_group"; }
};


struct EndGroupCommand final : public IEditorCommand
{
	EndGroupCommand() = default;
	EndGroupCommand(WorldEditor&) {}

	bool execute() override { return true; }
	void undo() override { ASSERT(false); }
	void serialize(JsonSerializer& serializer) override {}
	void deserialize(JsonDeserializer& serializer) override {}
	bool merge(IEditorCommand& command) override { ASSERT(false); return false; }
	const char* getType() override { return "end_group"; }

	u32 group_type;
};


class SetEntityNameCommand final : public IEditorCommand
{
public:
	explicit SetEntityNameCommand(WorldEditor& editor)
		: m_new_name(m_editor.getAllocator())
		, m_old_name(m_editor.getAllocator())
		, m_editor(editor)
	{
	}

	SetEntityNameCommand(WorldEditor& editor, EntityRef entity, const char* name)
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


	void deserialize(JsonDeserializer& serializer) override
	{
		char name[100];
		serializer.deserialize("name", name, sizeof(name), "");
		m_new_name = name;
		serializer.deserialize("entity", m_entity, INVALID_ENTITY);
		if (m_entity.isValid()) {
			m_old_name = m_editor.getUniverse()->getEntityName((EntityRef)m_entity);
		}
		else {
			m_old_name = "";
		}
	}


	bool execute() override
	{
		if (m_entity.isValid()) {
			m_editor.getUniverse()->setEntityName((EntityRef)m_entity, m_new_name.c_str());
			return true;
		}
		return false;
	}


	void undo() override
	{
		if (m_entity.isValid()) {
			m_editor.getUniverse()->setEntityName((EntityRef)m_entity, m_old_name.c_str());
		}
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
	EntityPtr m_entity;
	string m_new_name;
	string m_old_name;
};


class MoveEntityCommand final : public IEditorCommand
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
		const EntityRef* entities,
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
		PrefabSystem& prefab_system = m_editor.getPrefabSystem();
		m_entities.reserve(count);
		m_new_positions.reserve(count);
		m_new_rotations.reserve(count);
		m_old_positions.reserve(count);
		m_old_rotations.reserve(count);
		for (int i = count - 1; i >= 0; --i)
		{
			u64 prefab = prefab_system.getPrefab(entities[i]);
			EntityPtr parent = universe->getParent(entities[i]);
			if (prefab != 0 && parent.isValid() && (prefab_system.getPrefab((EntityRef)parent) & 0xffffFFFF) == (prefab & 0xffffFFFF))
			{
				float scale = universe->getScale(entities[i]);
				Transform new_local_tr = universe->computeLocalTransform((EntityRef)parent, { new_positions[i], new_rotations[i], scale });
				EntityPtr instance = prefab_system.getFirstInstance(prefab);
				while (instance.isValid())
				{
					EntityRef instance_ref = (EntityRef)instance;
					m_entities.push(instance_ref);
					const EntityPtr inst_parent_ptr = universe->getParent(instance_ref);
					ASSERT(inst_parent_ptr.isValid());
					Transform new_tr = universe->getTransform((EntityRef)inst_parent_ptr);
					new_tr = new_tr * new_local_tr;
					m_new_positions.push(new_tr.pos);
					m_new_rotations.push(new_tr.rot);
					m_old_positions.push(universe->getPosition(instance_ref));
					m_old_rotations.push(universe->getRotation(instance_ref));
					instance = prefab_system.getNextInstance(instance_ref);
				}
			}
			else
			{
				m_entities.push(entities[i]);
				m_new_positions.push(new_positions[i]);
				m_new_rotations.push(new_rotations[i]);
				m_old_positions.push(universe->getPosition(entities[i]));
				m_old_rotations.push(universe->getRotation(entities[i]));
			}
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


	void deserialize(JsonDeserializer& serializer) override
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
			if(m_entities[i].isValid()) {
				m_old_positions[i] = universe->getPosition((EntityRef)m_entities[i]);
				m_old_rotations[i] = universe->getRotation((EntityRef)m_entities[i]);
			}
		}
		serializer.deserializeArrayEnd();
	}


	bool execute() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if(m_entities[i].isValid()) {
				const EntityRef entity = (EntityRef)m_entities[i];
				universe->setPosition(entity, m_new_positions[i]);
				universe->setRotation(entity, m_new_rotations[i]);
			}
		}
		return true;
	}


	void undo() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if(m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				universe->setPosition(entity, m_old_positions[i]);
				universe->setRotation(entity, m_old_rotations[i]);
			}
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
	Array<EntityPtr> m_entities;
	Array<Vec3> m_new_positions;
	Array<Quat> m_new_rotations;
	Array<Vec3> m_old_positions;
	Array<Quat> m_old_rotations;
};


class LocalMoveEntityCommand final : public IEditorCommand
{
public:
	explicit LocalMoveEntityCommand(WorldEditor& editor)
		: m_new_positions(editor.getAllocator())
		, m_old_positions(editor.getAllocator())
		, m_entities(editor.getAllocator())
		, m_editor(editor)
	{
	}


	LocalMoveEntityCommand(WorldEditor& editor,
		const EntityRef* entities,
		const Vec3* new_positions,
		int count,
		IAllocator& allocator)
		: m_new_positions(allocator)
		, m_old_positions(allocator)
		, m_entities(allocator)
		, m_editor(editor)
	{
		ASSERT(count > 0);
		Universe* universe = m_editor.getUniverse();
		PrefabSystem& prefab_system = m_editor.getPrefabSystem();
		m_entities.reserve(count);
		m_new_positions.reserve(count);
		m_old_positions.reserve(count);
		for (int i = count - 1; i >= 0; --i)
		{
			u64 prefab = prefab_system.getPrefab(entities[i]);
			EntityPtr parent = universe->getParent(entities[i]);
			if (prefab != 0 && parent.isValid() && (prefab_system.getPrefab((EntityRef)parent) & 0xffffFFFF) == (prefab & 0xffffFFFF))
			{
				EntityPtr instance = prefab_system.getFirstInstance(prefab);
				while (instance.isValid())
				{
					EntityRef e = (EntityRef)instance;
					m_entities.push(e);
					m_new_positions.push(new_positions[i]);
					m_old_positions.push(universe->getPosition(e));
					instance = prefab_system.getNextInstance(e);
				}
			}
			else
			{
				m_entities.push(entities[i]);
				m_new_positions.push(new_positions[i]);
				m_old_positions.push(universe->getPosition(entities[i]));
			}
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
		}
		serializer.endArray();
	}


	void deserialize(JsonDeserializer& serializer) override
	{
		Universe* universe = m_editor.getUniverse();
		int count;
		serializer.deserialize("count", count, 0);
		m_entities.resize(count);
		m_new_positions.resize(count);
		m_old_positions.resize(count);
		serializer.deserializeArrayBegin("entities");
		for (int i = 0; i < m_entities.size(); ++i)
		{
			serializer.deserializeArrayItem(m_entities[i], INVALID_ENTITY);
			serializer.deserializeArrayItem(m_new_positions[i].x, 0);
			serializer.deserializeArrayItem(m_new_positions[i].y, 0);
			serializer.deserializeArrayItem(m_new_positions[i].z, 0);
			if (m_entities[i].isValid()) {
				m_old_positions[i] = universe->getPosition((EntityRef)m_entities[i]);
			}
			else {
				m_old_positions[i].set(0, 0, 0);
			}
		}
		serializer.deserializeArrayEnd();
	}


	bool execute() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				universe->setLocalPosition(entity, m_new_positions[i]);
			}
		}
		return true;
	}


	void undo() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				universe->setLocalPosition(entity, m_old_positions[i]);
			}
		}
	}


	const char* getType() override { return "local_move_entity"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		LocalMoveEntityCommand& my_command = static_cast<LocalMoveEntityCommand&>(command);
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
	Array<EntityPtr> m_entities;
	Array<Vec3> m_new_positions;
	Array<Vec3> m_old_positions;
};


class ScaleEntityCommand final : public IEditorCommand
{
public:
	explicit ScaleEntityCommand(WorldEditor& editor)
		: m_old_scales(editor.getAllocator())
		, m_new_scales(editor.getAllocator())
		, m_entities(editor.getAllocator())
		, m_editor(editor)
	{
	}


	ScaleEntityCommand(WorldEditor& editor,
		const EntityRef* entities,
		int count,
		float scale,
		IAllocator& allocator)
		: m_old_scales(allocator)
		, m_new_scales(allocator)
		, m_entities(allocator)
		, m_editor(editor)
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = count - 1; i >= 0; --i)
		{
			m_entities.push(entities[i]);
			m_old_scales.push(universe->getScale(entities[i]));
			m_new_scales.push(scale);
		}
	}


	ScaleEntityCommand(WorldEditor& editor,
		const EntityRef* entities,
		const float* scales,
		int count,
		IAllocator& allocator)
		: m_old_scales(allocator)
		, m_new_scales(allocator)
		, m_entities(allocator)
		, m_editor(editor)
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = count - 1; i >= 0; --i)
		{
			m_entities.push(entities[i]);
			m_old_scales.push(universe->getScale(entities[i]));
			m_new_scales.push(scales[i]);
		}
	}


	void serialize(JsonSerializer& serializer) override
	{
		serializer.beginArray("new_scales");
		for (int i = 0; i < m_new_scales.size(); ++i)
		{
			serializer.serializeArrayItem(m_new_scales[i]);
		}
		serializer.endArray();
		serializer.beginArray("entities");
		for (int i = 0; i < m_entities.size(); ++i)
		{
			serializer.serializeArrayItem(m_entities[i]);
		}
		serializer.endArray();
	}


	void deserialize(JsonDeserializer& serializer) override
	{
		Universe* universe = m_editor.getUniverse();
		serializer.deserializeArrayBegin("new_scales");
		while (!serializer.isArrayEnd())
		{
			float scale;
			serializer.deserializeArrayItem(scale, 1);
			m_new_scales.push(scale);
		}
		serializer.deserializeArrayEnd();
		serializer.deserializeArrayBegin("entities");
		while (!serializer.isArrayEnd())
		{
			EntityPtr entity;
			serializer.deserializeArrayItem(entity, INVALID_ENTITY);
			m_entities.push(entity);
			if (entity.isValid()) {
				m_old_scales.push(universe->getScale((EntityRef)entity));
			}
			else {
				m_old_scales.push(1);
			}
		}
		serializer.deserializeArrayEnd();
	}


	bool execute() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				universe->setScale(entity, m_new_scales[i]);
			}
		}
		return true;
	}


	void undo() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				universe->setScale(entity, m_old_scales[i]);
			}
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
				if (m_new_scales[i] != my_command.m_new_scales[i])
				{
					return false;
				}
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
	Array<EntityPtr> m_entities;
	Array<float> m_new_scales;
	Array<float> m_old_scales;
};


struct GatherResourcesVisitor : Reflection::ISimpleComponentVisitor
{
	void visitProperty(const Reflection::PropertyBase& prop) override {}

	void visit(const Reflection::IArrayProperty& prop) override
	{
		int count = prop.getCount(cmp);
		for (int i = 0; i < count; ++i)
		{
			index = i;
			prop.visit(*this);
		}
		index = -1;
	}

	void visit(const Reflection::Property<Path>& prop) override
	{
		visitProperty(prop);
		auto* attr = Reflection::getAttribute(prop, Reflection::IAttribute::RESOURCE);
		if (!attr) return;
		auto* resource_attr = (Reflection::ResourceAttribute*)attr;

		OutputBlob tmp(editor->getAllocator());
		prop.getValue(cmp, index, tmp);
		Path path((const char*)tmp.getData());
		Resource* resource = resource_manager->get(resource_attr->type)->load(path);
		if(resource) resources->push(resource);
	}

	ResourceManager* resource_manager;
	ComponentUID cmp;
	int index = -1;
	WorldEditor* editor;
	Array<Resource*>* resources;
};


struct SaveVisitor : Reflection::ISimpleComponentVisitor
{
	void begin(const Reflection::ComponentBase& cmp) override
	{
		stream->write(cmp.getPropertyCount());
	}

	void visitProperty(const Reflection::PropertyBase& prop) override
	{
		stream->write(crc32(prop.name));
		int size = stream->getPos();
		stream->write(size);
		prop.getValue(cmp, index, *stream);
		*(int*)((u8*)stream->getData() + size) = stream->getPos() - size - sizeof(int);
	}

	ComponentUID cmp;
	OutputBlob* stream;
	int index = -1;
};


class RemoveArrayPropertyItemCommand final : public IEditorCommand
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
		const Reflection::IArrayProperty& property)
		: m_component(component)
		, m_index(index)
		, m_property(&property)
		, m_old_values(editor.getAllocator())
		, m_editor(editor)
	{
		SaveVisitor save;
		save.cmp = m_component;
		save.stream = &m_old_values;
		save.index = m_index;
		m_property->visit(save);
	}


	void serialize(JsonSerializer& serializer) override
	{
		serializer.serialize("inedx", m_index);
		serializer.serialize("entity_index", m_component.entity);
		serializer.serialize("component_type", Reflection::getComponentTypeHash(m_component.type));
		serializer.serialize("property_name_hash", crc32(m_property->name));
	}


	void deserialize(JsonDeserializer& serializer) override
	{
		serializer.deserialize("inedx", m_index, 0);
		serializer.deserialize("entity_index", m_component.entity, INVALID_ENTITY);
		u32 hash;
		serializer.deserialize("component_type", hash, 0);
		m_component.type = Reflection::getComponentTypeFromHash(hash);
		m_component.scene = m_editor.getUniverse()->getScene(m_component.type);
		u32 property_name_hash;
		serializer.deserialize("property_name_hash", property_name_hash, 0);
		m_property =
			static_cast<const Reflection::IArrayProperty*>(Reflection::getProperty(m_component.type, property_name_hash));
	}


	bool execute() override
	{
		m_property->removeItem(m_component, m_index);
		return true;
	}


	void undo() override
	{
		m_property->addItem(m_component, m_index);
		InputBlob old_values(m_old_values);
		load(m_component, m_index, old_values);
	}


	const char* getType() override { return "remove_array_property_item"; }


	bool merge(IEditorCommand&) override { return false; }

private:
	WorldEditor& m_editor;
	ComponentUID m_component;
	int m_index;
	const Reflection::IArrayProperty *m_property;
	OutputBlob m_old_values;
};


class AddArrayPropertyItemCommand final : public IEditorCommand
{

public:
	explicit AddArrayPropertyItemCommand(WorldEditor& editor)
		: m_editor(editor)
	{
	}

	AddArrayPropertyItemCommand(WorldEditor& editor,
		const ComponentUID& component,
		const Reflection::IArrayProperty& property)
		: m_component(component)
		, m_index(-1)
		, m_property(&property)
		, m_editor(editor)
	{
	}


	void serialize(JsonSerializer& serializer) override
	{
		serializer.serialize("inedx", m_index);
		serializer.serialize("entity_index", m_component.entity);
		serializer.serialize("component_type", Reflection::getComponentTypeHash(m_component.type));
		serializer.serialize("property_name_hash", crc32(m_property->name));
	}


	void deserialize(JsonDeserializer& serializer) override
	{
		serializer.deserialize("inedx", m_index, 0);
		serializer.deserialize("entity_index", m_component.entity, INVALID_ENTITY);
		u32 hash;
		serializer.deserialize("component_type", hash, 0);
		m_component.type = Reflection::getComponentTypeFromHash(hash);
		m_component.scene = m_editor.getUniverse()->getScene(m_component.type);
		u32 property_name_hash;
		serializer.deserialize("property_name_hash", property_name_hash, 0);
		m_property = (const Reflection::IArrayProperty*)Reflection::getProperty(m_component.type, property_name_hash);
	}


	bool execute() override
	{
		m_property->addItem(m_component, -1);
		m_index = m_property->getCount(m_component) - 1;
		return true;
	}


	void undo() override
	{
		m_property->removeItem(m_component, m_index);
	}


	const char* getType() override { return "add_array_property_item"; }


	bool merge(IEditorCommand&) override { return false; }

private:
	ComponentUID m_component;
	int m_index;
	const Reflection::IArrayProperty *m_property;
	WorldEditor& m_editor;
};


class SetPropertyCommand final : public IEditorCommand
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
		const EntityRef* entities,
		int count,
		ComponentType component_type,
		int index,
		const Reflection::PropertyBase& property,
		const void* data,
		int size)
		: m_component_type(component_type)
		, m_entities(editor.getAllocator())
		, m_property(&property)
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
				m_property->getValue(component, index, m_old_value);
				m_entities.push(entities[i]);
			}
			else
			{
				EntityPtr instance = prefab_system.getFirstInstance(prefab);
				while(instance.isValid())
				{
					EntityRef inst_ref = (EntityRef)instance;
					ComponentUID component = m_editor.getUniverse()->getComponent(inst_ref, component_type);
					m_property->getValue(component, index, m_old_value);
					m_entities.push(inst_ref);
					instance = prefab_system.getNextInstance(inst_ref);
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
		for (EntityPtr entity : m_entities)
		{
			serializer.serializeArrayItem(entity);
		}
		serializer.endArray();
		serializer.serialize("component_type", Reflection::getComponentTypeHash(m_component_type));
		serializer.beginArray("data");
		for (int i = 0; i < m_new_value.getPos(); ++i)
		{
			serializer.serializeArrayItem((int)((const u8*)m_new_value.getData())[i]);
		}
		serializer.endArray();
		serializer.serialize("property_name_hash", crc32(m_property->name));
	}


	void deserialize(JsonDeserializer& serializer) override
	{
		serializer.deserialize("index", m_index, 0);
		serializer.deserializeArrayBegin("entities");
		while (!serializer.isArrayEnd())
		{
			EntityPtr entity;
			serializer.deserializeArrayItem(entity, INVALID_ENTITY);
			m_entities.push(entity);
		}
		serializer.deserializeArrayEnd();
		u32 hash;
		serializer.deserialize("component_type", hash, 0);
		m_component_type = Reflection::getComponentTypeFromHash(hash);
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
		m_property = Reflection::getProperty(m_component_type, property_name_hash);
	}


	bool execute() override
	{
		InputBlob blob(m_new_value);
		for (EntityPtr entity : m_entities) {
			if(entity.isValid()) {
				ComponentUID component = m_editor.getUniverse()->getComponent((EntityRef)entity, m_component_type);
				blob.rewind();
				m_property->setValue(component, m_index, blob);
			}
		}
		return true;
	}


	void undo() override
	{
		InputBlob blob(m_old_value);
		for (EntityPtr entity : m_entities) {
			if (entity.isValid()) {
				ComponentUID component = m_editor.getUniverse()->getComponent((EntityRef)entity, m_component_type);
				m_property->setValue(component, m_index, blob);
			}
		}
	}


	const char* getType() override { return "set_property_values"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		SetPropertyCommand& src = static_cast<SetPropertyCommand&>(command);
		if (m_component_type == src.m_component_type &&
			m_entities.size() == src.m_entities.size() &&
			src.m_property == m_property &&
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
	Array<EntityPtr> m_entities;
	OutputBlob m_new_value;
	OutputBlob m_old_value;
	int m_index;
	const Reflection::PropertyBase* m_property;
};

class PasteEntityCommand;


struct WorldEditorImpl final : public WorldEditor
{
	friend class PasteEntityCommand;
private:
	class AddComponentCommand final : public IEditorCommand
	{
	public:
		explicit AddComponentCommand(WorldEditor& editor)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
			, m_entities(editor.getAllocator())
		{
		}

		AddComponentCommand(WorldEditorImpl& editor,
							const Array<EntityRef>& entities,
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
						EntityPtr instance = editor.getPrefabSystem().getFirstInstance(prefab);
						while(instance.isValid())
						{
							const EntityRef e = (EntityRef)instance;
							m_entities.push(e);
							instance = editor.getPrefabSystem().getNextInstance(e);
						}
					}
				}
			}
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("component_type", Reflection::getComponentTypeHash(m_type));
			serializer.beginArray("entities");
			for (int i = 0; i < m_entities.size(); ++i)
			{
				serializer.serializeArrayItem(m_entities[i]);
			}
			serializer.endArray();
		}


		void deserialize(JsonDeserializer& serializer) override
		{
			u32 hash;
			serializer.deserialize("component_type", hash, 0);
			m_type = Reflection::getComponentTypeFromHash(hash);
			m_entities.clear();
			serializer.deserializeArrayBegin("entities");
			while (!serializer.isArrayEnd())
			{
				EntityPtr e;
				serializer.deserializeArrayItem(e, INVALID_ENTITY);
				if (e.isValid()) {
					m_entities.push((EntityRef)e);
				}
			}
			serializer.deserializeArrayEnd();
		}


		bool merge(IEditorCommand&) override { return false; }


		const char* getType() override { return "add_component"; }


		bool execute() override
		{
			bool ret = false;
			Universe* universe = m_editor.getUniverse();

			for (int j = 0; j < m_entities.size(); ++j)
			{
				universe->createComponent(m_type, m_entities[j]);
				if (universe->hasComponent(m_entities[j], m_type))
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
				if (cmp.isValid()) {
					m_editor.getUniverse()->destroyComponent((EntityRef)cmp.entity, cmp.type);
				}
			}
		}


	private:
		ComponentType m_type;
		Array<EntityRef> m_entities;
		WorldEditorImpl& m_editor;
	};


	class MakeParentCommand final : public IEditorCommand
	{
	public:
		explicit MakeParentCommand(WorldEditor& editor)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
		{
		}


		MakeParentCommand(WorldEditorImpl& editor, EntityPtr parent, EntityRef child)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
			, m_parent(parent)
			, m_child(child)
		{
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("parent", m_parent);
			serializer.serialize("child", m_child);
		}


		void deserialize(JsonDeserializer& serializer) override
		{
			serializer.deserialize("parent", m_parent, INVALID_ENTITY);
			serializer.deserialize("child", m_child, INVALID_ENTITY);
		}


		bool merge(IEditorCommand& cmd) override { 
			
			auto& c = (MakeParentCommand&)cmd;
			if (c.m_child != m_child) return false;
			c.m_parent = m_parent;
			return true;
		}


		const char* getType() override { return "make_parent"; }


		bool execute() override
		{
			if(m_child.isValid()) {
				const EntityRef e = (EntityRef)m_child;
				m_old_parent = m_editor.getUniverse()->getParent(e);
				m_editor.getUniverse()->setParent(m_parent, e);
			}
			return true;
		}


		void undo() override
		{
			if(m_child.isValid()) {
				m_editor.getUniverse()->setParent(m_old_parent, (EntityRef)m_child);
			}
		}

	private:
		WorldEditor& m_editor;
		EntityPtr m_parent;
		EntityPtr m_old_parent;
		EntityPtr m_child;
	};


	class DestroyEntitiesCommand final : public IEditorCommand
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


		DestroyEntitiesCommand(WorldEditorImpl& editor, const EntityRef* entities, int count)
			: m_editor(editor)
			, m_entities(editor.getAllocator())
			, m_transformations(editor.getAllocator())
			, m_old_values(editor.getAllocator())
			, m_resources(editor.getAllocator())
		{
			m_entities.reserve(count);
			for (int i = 0; i < count; ++i)
			{
				m_entities.push(entities[i]);
				pushChildren(entities[i]);
			}
			m_entities.removeDuplicates();
			m_transformations.reserve(m_entities.size());
		}


		~DestroyEntitiesCommand()
		{
			for (Resource* resource : m_resources)
			{
				resource->getResourceManager().unload(*resource);
			}
		}


		void pushChildren(EntityRef entity)
		{
			Universe* universe = m_editor.getUniverse();
			for (EntityPtr e = universe->getFirstChild(entity); e.isValid(); e = universe->getNextSibling((EntityRef)e))
			{
				m_entities.push((EntityRef)e);
				pushChildren((EntityRef)e);
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
				serializer.serializeArrayItem(m_transformations[i].scale);
			}
			serializer.endArray();
		}


		void deserialize(JsonDeserializer& serializer) override
		{
			int count;
			serializer.deserialize("count", count, 0);
			serializer.deserializeArrayBegin("entities");
			m_entities.reserve(count);
			m_transformations.reserve(count);
			for (int i = 0; i < count; ++i) {
				EntityPtr e;
				serializer.deserializeArrayItem(e, INVALID_ENTITY);
				if(e.isValid()) {
					m_entities.push((EntityRef)e);
					Transform tr = m_transformations.emplace();
					serializer.deserializeArrayItem(tr.pos.x, 0);
					serializer.deserializeArrayItem(tr.pos.y, 0);
					serializer.deserializeArrayItem(tr.pos.z, 0);
					serializer.deserializeArrayItem(tr.rot.x, 0);
					serializer.deserializeArrayItem(tr.rot.y, 0);
					serializer.deserializeArrayItem(tr.rot.z, 0);
					serializer.deserializeArrayItem(tr.rot.w, 1);
					if (serializer.isArrayEnd())
					{
						tr.scale = 1;
					}
					else
					{
						serializer.deserializeArrayItem(tr.scale, 1);
					}
				}
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
				m_old_values.writeString(universe->getEntityName(m_entities[i]));
				EntityPtr parent = universe->getParent(m_entities[i]);
				m_old_values.write(parent);
				if (parent.isValid())
				{
					Transform local_tr = universe->getLocalTransform(m_entities[i]);
					m_old_values.write(local_tr);
				}
				for (EntityPtr child = universe->getFirstChild(m_entities[i]); child.isValid(); child = universe->getNextSibling((EntityRef)child))
				{
					m_old_values.write(child);
					Transform local_tr = universe->getLocalTransform((EntityRef)child);
					m_old_values.write(local_tr);
				}
				m_old_values.write(INVALID_ENTITY);

				m_old_values.write(count);
				for (ComponentUID cmp = universe->getFirstComponent(m_entities[i]);
					cmp.isValid();
					cmp = universe->getNextComponent(cmp))
				{
					m_old_values.write(cmp.type);
					const Reflection::ComponentBase* cmp_desc = Reflection::getComponent(cmp.type);

					GatherResourcesVisitor gather;
					gather.cmp = cmp;
					gather.editor = &m_editor;
					gather.resources = &m_resources;
					gather.resource_manager = &resource_manager;
					cmp_desc->visit(gather);

					SaveVisitor save;
					save.cmp = cmp;
					save.stream = &m_old_values;
					cmp_desc->visit(save);
				}
				u64 prefab = m_editor.getPrefabSystem().getPrefab(m_entities[i]);
				m_old_values.write(prefab);
			}
			for (EntityRef e : m_entities)
			{
				universe->destroyEntity(e);
				m_editor.m_entity_map.erase(e);
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
				universe->emplaceEntity(m_entities[i]);
			}
			for (int i = 0; i < m_entities.size(); ++i)
			{
				EntityRef new_entity = m_entities[i];
				universe->setTransform(new_entity, m_transformations[i]);
				int cmps_count;
				EntityGUID guid;
				blob.read(guid.value);
				m_editor.m_entity_map.insert(guid, new_entity);
				char name[Universe::ENTITY_NAME_MAX_LENGTH];
				blob.readString(name, lengthOf(name));
				universe->setEntityName(new_entity, name);
				EntityPtr parent;
				blob.read(parent);
				if (parent.isValid())
				{
					Transform local_tr;
					blob.read(local_tr);
					universe->setParent(parent, new_entity);
					universe->setLocalTransform(new_entity, local_tr);
				}
				EntityPtr child;
				for(blob.read(child); child.isValid(); blob.read(child))
				{
					Transform local_tr;
					blob.read(local_tr);
					universe->setParent(new_entity, (EntityRef)child);
					universe->setLocalTransform((EntityRef)child, local_tr);
				}

				blob.read(cmps_count);
				for (int j = 0; j < cmps_count; ++j)
				{
					ComponentType cmp_type;
					blob.read(cmp_type);
					ComponentUID new_component;
					IScene* scene = universe->getScene(cmp_type);
					ASSERT(scene);
					universe->createComponent(cmp_type, new_entity);
					new_component.entity = new_entity;
					new_component.scene = scene;
					new_component.type = cmp_type;
					
					::Lumix::load(new_component, -1, blob);
				}
				u64 tpl;
				blob.read(tpl);
				if (tpl) m_editor.getPrefabSystem().setPrefab(new_entity, tpl);
			}
		}


		const char* getType() override { return "destroy_entities"; }


	private:
		WorldEditorImpl& m_editor;
		Array<EntityRef> m_entities;
		Array<Transform> m_transformations;
		OutputBlob m_old_values;
		Array<Resource*> m_resources;
	};


	class DestroyComponentCommand final : public IEditorCommand
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


		DestroyComponentCommand(WorldEditorImpl& editor, const EntityRef* entities, int count, ComponentType cmp_type)
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
					EntityPtr instance = editor.getPrefabSystem().getFirstInstance(prefab);
					while(instance.isValid())
					{
						m_entities.push((EntityRef)instance);
						instance = editor.getPrefabSystem().getNextInstance((EntityRef)instance);
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
			for (EntityRef entity : m_entities)
			{
				serializer.serializeArrayItem(entity);
			}
			serializer.endArray();
			serializer.serialize("component_type", Reflection::getComponentTypeHash(m_cmp_type));
		}


		void deserialize(JsonDeserializer& serializer) override
		{
			serializer.deserializeArrayBegin("entities");
			while (!serializer.isArrayEnd())
			{
				EntityPtr entity;
				serializer.deserializeArrayItem(entity, INVALID_ENTITY);
				if(entity.isValid()) {
					m_entities.push((EntityRef)entity);
				}
			}
			serializer.deserializeArrayEnd();

			u32 hash;
			serializer.deserialize("component_type", hash, 0);
			m_cmp_type = Reflection::getComponentTypeFromHash(hash);
		}


		void undo() override
		{
			ComponentUID cmp;
			Universe* universe = m_editor.getUniverse();
			cmp.scene = universe->getScene(m_cmp_type);
			cmp.type = m_cmp_type;
			ASSERT(cmp.scene);
			InputBlob blob(m_old_values);
			for (EntityRef entity : m_entities)
			{
				cmp.entity = entity;
				universe->createComponent(cmp.type, entity);
				::Lumix::load(cmp, -1, blob);
			}
		}


		bool merge(IEditorCommand&) override { return false; }


		const char* getType() override { return "destroy_components"; }


		bool execute() override
		{
			const Reflection::ComponentBase* cmp_desc = Reflection::getComponent(m_cmp_type);
			ComponentUID cmp;
			cmp.type = m_cmp_type;
			cmp.scene = m_editor.getUniverse()->getScene(m_cmp_type);
			if (m_entities.empty()) return false;
			if (!cmp.scene) return false;
			ResourceManager& resource_manager = m_editor.getEngine().getResourceManager();

			for (EntityRef entity : m_entities)
			{
				cmp.entity = entity;
				SaveVisitor save;
				save.cmp = cmp;
				save.stream = &m_old_values;
				cmp_desc->visit(save);

				GatherResourcesVisitor gather;
				gather.cmp = cmp;
				gather.editor = &m_editor;
				gather.resources = &m_resources;
				gather.resource_manager = &resource_manager;
				cmp_desc->visit(gather);

				m_editor.getUniverse()->destroyComponent(entity, m_cmp_type);
			}
			return true;
		}

	private:
		Array<EntityRef> m_entities;
		ComponentType m_cmp_type;
		WorldEditorImpl& m_editor;
		OutputBlob m_old_values;
		Array<Resource*> m_resources;
	};


	class AddEntityCommand final : public IEditorCommand
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
			if (m_entity.isValid())
			{
				m_editor.getUniverse()->emplaceEntity((EntityRef)m_entity);
				m_editor.getUniverse()->setPosition((EntityRef)m_entity, m_position);
			}
			else
			{
				m_entity = m_editor.getUniverse()->createEntity(m_position, Quat(0, 0, 0, 1));
			}
			const EntityRef e = (EntityRef)m_entity;
			((WorldEditorImpl&)m_editor).m_entity_map.create(e);
			m_editor.selectEntities(&e, 1, false);
			return true;
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("pos_x", m_position.x);
			serializer.serialize("pos_y", m_position.y);
			serializer.serialize("pos_z", m_position.z);
		}


		void deserialize(JsonDeserializer& serializer) override
		{
			serializer.deserialize("pos_x", m_position.x, 0);
			serializer.deserialize("pos_y", m_position.y, 0);
			serializer.deserialize("pos_z", m_position.z, 0);
		}


		void undo() override
		{
			if(m_entity.isValid()) {
				const EntityRef e = (EntityRef)m_entity;
				m_editor.getUniverse()->destroyEntity(e);
				m_editor.m_entity_map.erase(e);
			}
		}


		bool merge(IEditorCommand&) override { return false; }


		const char* getType() override { return "add_entity"; }


		EntityPtr getEntity() const { return m_entity; }


	private:
		WorldEditorImpl& m_editor;
		EntityPtr m_entity;
		Vec3 m_position;
	};

public:
	IAllocator& getAllocator() override { return m_allocator; }


	const Viewport& getViewport() const override { return m_viewport; }
	void setViewport(const Viewport& viewport) override { m_viewport = viewport; }


	Universe* getUniverse() override
	{
		return m_universe; 
	}


	Engine& getEngine() override { return *m_engine; }


	void showGizmos()
	{
		if (m_selected_entities.empty()) return;

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
		if (!m_go_to_parameters.m_is_active) return;

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
		m_viewport.pos = pos;
		m_viewport.rot = rot;
	}


	void inputFrame() override
	{
		m_mouse_rel_x = m_mouse_rel_y = 0;
		for (auto& i : m_is_mouse_click) i = false;
	}


	void previewSnapVertex()
	{
		if (m_snap_mode != SnapMode::VERTEX) return;

		Vec3 origin, dir;
		m_viewport.getRay(m_mouse_pos, origin, dir);
		RayHit hit = m_render_interface->castRay(origin, dir, INVALID_ENTITY);
		//if (m_gizmo->isActive()) return;
		if (!hit.is_hit) return;

		Vec3 snap_pos = getClosestVertex(hit);
		m_render_interface->addDebugCross(snap_pos, 1, 0xfff00fff, 0);
		// TODO
	}


	void update() override
	{
		PROFILE_FUNCTION();
		updateGoTo();
		previewSnapVertex();

		if (!m_selected_entities.empty())
		{
			m_gizmo->add(m_selected_entities[0]);
		}

		if (m_is_mouse_down[MouseButton::LEFT] && m_mouse_mode == MouseMode::SELECT)
		{
			m_render_interface->addRect2D(m_rect_selection_start, m_mouse_pos, 0xfffffFFF);
			m_render_interface->addRect2D(m_rect_selection_start - Vec2(1, 1), m_mouse_pos + Vec2(1, 1), 0xff000000);
		}

		createEditorLines();
	}


	void updateEngine() override
	{
		ASSERT(m_universe);
		m_engine->update(*m_universe);
	}


	~WorldEditorImpl()
	{
		destroyUniverse();

		Gizmo::destroy(*m_gizmo);
		m_gizmo = nullptr;

		removePlugin(*m_measure_tool);
		LUMIX_DELETE(m_allocator, m_measure_tool);
		ASSERT(m_plugins.empty());

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


	Vec3 getClosestVertex(const RayHit& hit)
	{
		ASSERT(hit.is_hit);
		return m_render_interface->getClosestVertex(m_universe, (EntityRef)hit.entity, hit.pos);
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
			m_viewport.getRay({(float)x, (float)y}, origin, dir);
			auto hit = m_render_interface->castRay(origin, dir, INVALID_ENTITY);
			if (m_gizmo->isActive()) return;

			for (int i = 0; i < m_plugins.size(); ++i)
			{
				if (m_plugins[i]->onMouseDown(hit, x, y))
				{
					m_mouse_handling_plugin = m_plugins[i];
					m_mouse_mode = MouseMode::CUSTOM;
					return;
				}
			}
			m_mouse_mode = MouseMode::SELECT;
			m_rect_selection_start = {(float)x, (float)y};
		}
	}


	void addPlugin(Plugin& plugin) override { m_plugins.push(&plugin); }


	void removePlugin(Plugin& plugin) override
	{
		m_plugins.eraseItemFast(&plugin);
	}



	void onMouseMove(int x, int y, int relx, int rely) override
	{
		PROFILE_FUNCTION();
		m_mouse_pos.set((float)x, (float)y);
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


	void rectSelect()
	{
		Array<EntityRef> entities(m_allocator);

		Vec2 min = m_rect_selection_start;
		Vec2 max = m_mouse_pos;
		if (min.x > max.x) Math::swap(min.x, max.x);
		if (min.y > max.y) Math::swap(min.y, max.y);
		Frustum frustum = m_viewport.getFrustum(min, max);
		m_render_interface->getModelInstaces(entities, frustum, m_viewport.pos, m_viewport.fov, m_viewport.is_ortho);
		selectEntities(entities.empty() ? nullptr : &entities[0], entities.size(), false);
	}


	void onMouseUp(int x, int y, MouseButton::Value button) override
	{
		m_mouse_pos = {(float)x, (float)y};
		if (m_mouse_mode == MouseMode::SELECT)
		{
			if (m_rect_selection_start.x != m_mouse_pos.x || m_rect_selection_start.y != m_mouse_pos.y)
			{
				rectSelect();
			}
			else
			{
				Vec3 origin, dir;
				m_viewport.getRay(m_mouse_pos, origin, dir);
				auto hit = m_render_interface->castRay(origin, dir, INVALID_ENTITY);

				if (m_snap_mode != SnapMode::NONE && !m_selected_entities.empty() && hit.is_hit)
				{
					Vec3 snap_pos = origin + dir * hit.t;
					if (m_snap_mode == SnapMode::VERTEX) snap_pos = getClosestVertex(hit);
					Vec3 offset = m_gizmo->getOffset();
					Quat rot = m_universe->getRotation(m_selected_entities[0]);
					offset = rot.rotate(offset);
					snapEntities(snap_pos - offset);
				}
				else
				{
					auto icon_hit = m_editor_icons->raycast(origin, dir);
					if (icon_hit.entity != INVALID_ENTITY)
					{
						if(icon_hit.entity.isValid()) {
							EntityRef e = (EntityRef)icon_hit.entity;
							selectEntities(&e, 1, true);
						}
					}
					else if (hit.is_hit)
					{
						if(hit.entity.isValid()) {
							EntityRef entity = (EntityRef)hit.entity;
							selectEntities(&entity, 1, true);
						}
					}
				}
			}
		}

		m_is_mouse_down[button] = false;
		if (m_mouse_handling_plugin)
		{
			m_mouse_handling_plugin->onMouseUp(x, y, button);
			m_mouse_handling_plugin = nullptr;
		}
		m_mouse_mode = MouseMode::NONE;
	}


	Vec2 getMousePos() const override { return m_mouse_pos; }
	float getMouseRelX() const override { return m_mouse_rel_x; }
	float getMouseRelY() const override { return m_mouse_rel_y; }


	bool isUniverseChanged() const override { return m_is_universe_changed; }


	void saveUniverse(const char* basename, bool save_path) override
	{
		g_log_info.log("Editor") << "Saving universe " << basename << "...";
		
		auto& fs = m_engine->getFileSystem();
		StaticString<MAX_PATH_LENGTH> dir(m_engine->getDiskFileDevice()->getBasePath(), "universes/");
		PlatformInterface::makePath(dir);
		StaticString<MAX_PATH_LENGTH> path(dir, basename, ".unv");
		FS::IFile* file = fs.open(fs.getDefaultDevice(), Path(path), FS::Mode::CREATE_AND_WRITE);
		save(*file);
		fs.close(*file);

		serialize(basename);
		m_is_universe_changed = false;

		if (save_path) m_universe->setName(basename);
	}


	// TODO split
	struct EntityGUIDMap : public ILoadEntityGUIDMap, public ISaveEntityGUIDMap
	{
		explicit EntityGUIDMap(IAllocator& allocator)
			: guid_to_entity(allocator)
			, entity_to_guid(allocator)
			, is_random(true)
		{
		}


		void clear()
		{
			nonrandom_guid = 0;
			entity_to_guid.clear();
			guid_to_entity.clear();
		}


		void create(EntityRef entity)
		{
			EntityGUID guid = { is_random ? Math::randGUID() : ++nonrandom_guid };
			insert(guid, entity);
		}


		void erase(EntityRef entity)
		{
			EntityGUID guid = entity_to_guid[entity.index];
			if (!isValid(guid)) return;
			entity_to_guid[entity.index] = INVALID_ENTITY_GUID;
			guid_to_entity.erase(guid.value);
		}


		void insert(EntityGUID guid, EntityRef entity)
		{
			guid_to_entity.insert(guid.value, entity);
			while (entity.index >= entity_to_guid.size())
			{
				entity_to_guid.push(INVALID_ENTITY_GUID);
			}
			entity_to_guid[entity.index] = guid;
		}


		EntityPtr get(EntityGUID guid) override
		{
			auto iter = guid_to_entity.find(guid.value);
			if (iter.isValid()) return iter.value();
			return INVALID_ENTITY;
		}


		EntityGUID get(EntityPtr entity) override
		{
			if (!entity.isValid()) return INVALID_ENTITY_GUID;
			if (entity.index >= entity_to_guid.size()) return INVALID_ENTITY_GUID;
			return entity_to_guid[entity.index];
		}


		bool has(EntityGUID guid) const
		{
			auto iter = guid_to_entity.find(guid.value);
			return iter.isValid();
		}


		HashMap<u64, EntityRef> guid_to_entity;
		Array<EntityGUID> entity_to_guid;
		u64 nonrandom_guid = 0;
		bool is_random = true;
	};


	static bool deserialize(Universe& universe
		, const char* basedir
		, const char* basename
		, PrefabSystem& prefab_system
		, EntityGUIDMap& entity_map
		, IAllocator& allocator)
	{
		PROFILE_FUNCTION();
		
		entity_map.clear();
		StaticString<MAX_PATH_LENGTH> scn_dir(basedir, "/", basename, "/scenes/");
		auto scn_file_iter = PlatformInterface::createFileIterator(scn_dir, allocator);
		Array<u8> data(allocator);
		FS::OsFile file;
		auto loadFile = [&file, &data, &entity_map](const char* filepath, auto callback) {
			if (file.open(filepath, FS::Mode::OPEN_AND_READ))
			{
				if (file.size() > 0)
				{
					data.resize((int)file.size());
					file.read(&data[0], data.size());
					InputBlob blob(&data[0], data.size());
					TextDeserializer deserializer(blob, entity_map);
					callback(deserializer);
				}
				file.close();
			}
		};
		PlatformInterface::FileInfo info;
		int versions[ComponentType::MAX_TYPES_COUNT];
		while (PlatformInterface::getNextFile(scn_file_iter, &info))
		{
			if (info.is_directory) continue;
			if (info.filename[0] == '.') continue;

			StaticString<MAX_PATH_LENGTH> filepath(scn_dir, info.filename);
			char plugin_name[64];
			PathUtils::getBasename(plugin_name, lengthOf(plugin_name), filepath);
			IScene* scene = universe.getScene(crc32(plugin_name));
			if (!scene)
			{
				g_log_error.log("Editor") << "Could not open " << filepath << " since there is not plugin " << plugin_name;
				return false;
			}

			loadFile(filepath, [scene, &versions, &universe](TextDeserializer& deserializer) {
				int version;
				deserializer.read(&version);
				for (int i = 0; i < ComponentType::MAX_TYPES_COUNT; ++i)
				{
					ComponentType cmp_type = {i};
					if (universe.getScene(cmp_type) == scene)
					{
						versions[i] = version;
					}
				}
				scene->deserialize(deserializer);
			});
		}
		PlatformInterface::destroyFileIterator(scn_file_iter);
		
		StaticString<MAX_PATH_LENGTH> dir(basedir, "/", basename, "/");
		auto file_iter = PlatformInterface::createFileIterator(dir, allocator);
		while (PlatformInterface::getNextFile(file_iter, &info))
		{
			if (info.is_directory) continue;
			if (info.filename[0] == '.') continue;

			FS::OsFile file;
			StaticString<MAX_PATH_LENGTH> filepath(dir, info.filename);
			char tmp[32];
			PathUtils::getBasename(tmp, lengthOf(tmp), filepath);
			EntityGUID guid;
			fromCString(tmp, lengthOf(tmp), &guid.value);
			EntityRef entity = universe.createEntity({0, 0, 0}, {0, 0, 0, 1});
			entity_map.insert(guid, entity);
		}
		PlatformInterface::destroyFileIterator(file_iter);
		
		file_iter = PlatformInterface::createFileIterator(dir, allocator);
		while (PlatformInterface::getNextFile(file_iter, &info))
		{
			if (info.is_directory) continue;
			if (info.filename[0] == '.') continue;

			FS::OsFile file;
			StaticString<MAX_PATH_LENGTH> filepath(dir, info.filename);
			char tmp[32];
			PathUtils::getBasename(tmp, lengthOf(tmp), filepath);
			EntityGUID guid;
			fromCString(tmp, lengthOf(tmp), &guid.value);
			loadFile(filepath, [&versions, &entity_map, &universe, guid](TextDeserializer& deserializer) {
				char name[64];
				deserializer.read(name, lengthOf(name));
				RigidTransform tr;
				deserializer.read(&tr);
				float scale;
				deserializer.read(&scale);

				const EntityPtr e = entity_map.get(guid);
				const EntityRef entity = (EntityRef)e;

				EntityPtr parent;
				deserializer.read(&parent);
				if (parent.isValid()) universe.setParent(parent, entity);

				if(name[0]) universe.setEntityName(entity, name);
				universe.setTransformKeepChildren(entity, {tr.pos, tr.rot, scale});
				u32 cmp_type_hash;
				deserializer.read(&cmp_type_hash);
				while (cmp_type_hash != 0)
				{
					ComponentType cmp_type = Reflection::getComponentTypeFromHash(cmp_type_hash);
					universe.deserializeComponent(deserializer, entity, cmp_type, versions[cmp_type.index]);
					deserializer.read(&cmp_type_hash);
				}
			});
		}
		PlatformInterface::destroyFileIterator(file_iter);

		StaticString<MAX_PATH_LENGTH> filepath(basedir, "/", basename, "/systems/templates.sys");
		loadFile(filepath, [&](TextDeserializer& deserializer) {
			prefab_system.deserialize(deserializer);
			for (int i = 0, c = prefab_system.getMaxEntityIndex(); i < c; ++i)
			{
				u64 prefab = prefab_system.getPrefab({i});
				if (prefab != 0) entity_map.create({i});
			}
		});
		return &universe;
	}

	
	void serialize(const char* basename)
	{
		StaticString<MAX_PATH_LENGTH> dir(m_engine->getDiskFileDevice()->getBasePath(), "universes/", basename, "/");
		PlatformInterface::makePath(dir);
		PlatformInterface::makePath(dir + "probes/");
		PlatformInterface::makePath(dir + "scenes/");
		PlatformInterface::makePath(dir + "systems/");

		FS::OsFile file;
		OutputBlob blob(m_allocator);
		TextSerializer serializer(blob, m_entity_map);
		auto saveFile = [&file, &blob](const char* path) {
			if (file.open(path, FS::Mode::CREATE_AND_WRITE))
			{
				file.write(blob.getData(), blob.getPos());
				file.close();
			}
		};
		for (IScene* scene : m_universe->getScenes())
		{
			blob.clear();
			serializer.write("version", scene->getVersion());
			scene->serialize(serializer);
			StaticString<MAX_PATH_LENGTH> scene_file_path(dir, "scenes/", scene->getPlugin().getName(), ".scn");
			saveFile(scene_file_path);
		}

		blob.clear();
		m_prefab_system->serialize(serializer);
		StaticString<MAX_PATH_LENGTH> system_file_path(dir, "systems/templates.sys");
		saveFile(system_file_path);

		for (EntityPtr entity = m_universe->getFirstEntity(); entity.isValid(); entity = m_universe->getNextEntity((EntityRef)entity))
		{
			const EntityRef e = (EntityRef)entity;
			if (m_prefab_system->getPrefab(e) != 0) continue;
			blob.clear();
			serializer.write("name", m_universe->getEntityName(e));
			serializer.write("transform", m_universe->getTransform(e).getRigidPart());
			serializer.write("scale", m_universe->getScale(e));
			EntityPtr parent = m_universe->getParent(e);
			serializer.write("parent", parent);
			EntityGUID guid = m_entity_map.get(entity);
			StaticString<MAX_PATH_LENGTH> entity_file_path(dir, guid.value, ".ent");
			for (ComponentUID cmp = m_universe->getFirstComponent(e); cmp.entity.isValid();
				 cmp = m_universe->getNextComponent(cmp))
			{
				const char* cmp_name = Reflection::getComponentTypeID(cmp.type.index);
				u32 type_hash = Reflection::getComponentTypeHash(cmp.type);
				serializer.write(cmp_name, type_hash);
				m_universe->serializeComponent(serializer, cmp.type, (EntityRef)cmp.entity);
			}
			serializer.write("cmp_end", (u32)0);
			saveFile(entity_file_path);
		}
		clearUniverseDir(dir);
	}


	void clearUniverseDir(const char* dir)
	{
		PlatformInterface::FileInfo info;
		auto file_iter = PlatformInterface::createFileIterator(dir, m_allocator);
		while (PlatformInterface::getNextFile(file_iter, &info))
		{
			if (info.is_directory) continue;
			if (info.filename[0] == '.') continue;

			char basename[64];
			PathUtils::getBasename(basename, lengthOf(basename), info.filename);
			EntityGUID guid;
			fromCString(basename, lengthOf(basename), &guid.value);
			if (!m_entity_map.has(guid))
			{
				StaticString<MAX_PATH_LENGTH> filepath(dir, info.filename);
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
		m_prefab_system->serialize(blob);
		header.hash = crc32((const u8*)blob.getData() + hashed_offset, blob.getPos() - hashed_offset);
		*(Header*)blob.getData() = header;
		file.write(blob.getData(), blob.getPos());

		g_log_info.log("editor") << "Universe saved";
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


	void setCustomPivot() override
	{
		if (m_selected_entities.empty()) return;

		Vec3 origin, dir;		
		m_viewport.getRay(m_mouse_pos, origin, dir);
		auto hit = m_render_interface->castRay(origin, dir, INVALID_ENTITY);
		if (!hit.is_hit || hit.entity != m_selected_entities[0]) return;

		Vec3 snap_pos = getClosestVertex(hit);

		Matrix mtx = m_universe->getMatrix(m_selected_entities[0]);
		mtx.inverse();
		m_gizmo->setOffset(mtx.transformPoint(snap_pos));
	}


	void snapDown() override
	{
		if (m_selected_entities.empty()) return;

		Array<Vec3> new_positions(m_allocator);
		Universe* universe = getUniverse();

		for (int i = 0; i < m_selected_entities.size(); ++i)
		{
			EntityRef entity = m_selected_entities[i];

			Vec3 origin = universe->getPosition(entity);
			auto hit = m_render_interface->castRay(origin, Vec3(0, -1, 0), m_selected_entities[i]);
			if (hit.is_hit)
			{
				new_positions.push(origin + Vec3(0, -hit.t, 0));
			}
			else
			{
				hit = m_render_interface->castRay(origin, Vec3(0, 1, 0), m_selected_entities[i]);
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


	void makeParent(EntityPtr parent, EntityRef child) override
	{
		MakeParentCommand* command = LUMIX_NEW(m_allocator, MakeParentCommand)(*this, parent, child);
		executeCommand(command);
	}


	void destroyEntities(const EntityRef* entities, int count) override
	{
		DestroyEntitiesCommand* command = LUMIX_NEW(m_allocator, DestroyEntitiesCommand)(*this, entities, count);
		executeCommand(command);
	}


	void createEntityGUID(EntityRef entity) override
	{
		m_entity_map.create(entity);
	}


	void destroyEntityGUID(EntityRef entity) override
	{
		m_entity_map.erase(entity);
	}


	EntityGUID getEntityGUID(EntityRef entity) override
	{
		return m_entity_map.get(entity);
	}


	void makeAbsolute(char* absolute, int max_size, const char* relative) const override
	{
		FS::DiskFileDevice* disk = m_engine->getDiskFileDevice();
		bool is_absolute = relative[0] == '/' || relative[0] == '/';
		is_absolute = is_absolute || (relative[0] != 0 && relative[1] == ':');

		if (is_absolute || !disk)
		{
			copyString(absolute, max_size, relative);
			return;
		}

		copyString(absolute, max_size, disk->getBasePath());
		catString(absolute, max_size, relative);
	}


	void makeRelative(char* relative, int max_size, const char* absolute) const override
	{
		FS::DiskFileDevice* disk = m_engine->getDiskFileDevice();
		if (disk) {
			if (startsWith(absolute, disk->getBasePath())) {
				copyString(relative, max_size, absolute + stringLength(disk->getBasePath()));
				return;
			}
		}
		copyString(relative, max_size, absolute);
	}


	EntityRef addEntity() override
	{
		return addEntityAt(m_viewport.w >> 1, m_viewport.h >> 1);
	}


	EntityRef addEntityAt(int camera_x, int camera_y) override
	{
		Universe* universe = getUniverse();
		Vec3 origin;
		Vec3 dir;

		m_viewport.getRay({(float)camera_x, (float)camera_y}, origin, dir);
		auto hit = m_render_interface->castRay(origin, dir, INVALID_ENTITY);
		Vec3 pos;
		if (hit.is_hit)
		{
			pos = origin + dir * hit.t;
		}
		else
		{
			pos = m_viewport.pos + m_viewport.rot.rotate(Vec3(0, 0, -2));
		}
		AddEntityCommand* command = LUMIX_NEW(m_allocator, AddEntityCommand)(*this, pos);
		executeCommand(command);

		return (EntityRef)command->getEntity();
	}


	Vec3 getCameraRaycastHit() override
	{
		Universe* universe = getUniverse();
		const Vec2 center(float(m_viewport.w >> 1), float(m_viewport.h >> 1));

		Vec3 origin;
		Vec3 dir;
		m_viewport.getRay(center, origin, dir);
		auto hit = m_render_interface->castRay(origin, dir, INVALID_ENTITY);
		Vec3 pos;
		if (hit.is_hit)
		{
			pos = origin + dir * hit.t;
		}
		else
		{
			pos = m_viewport.pos + m_viewport.rot.rotate(Vec3(0, 0, -2));
		}
		return pos;
	}



	void setEntitiesScales(const EntityRef* entities, const float* scales, int count) override
	{
		if (count <= 0) return;

		IEditorCommand* command =
			LUMIX_NEW(m_allocator, ScaleEntityCommand)(*this, entities, scales, count, m_allocator);
		executeCommand(command);
	}


	void setEntitiesScale(const EntityRef* entities, int count, float scale) override
	{
		if (count <= 0) return;

		IEditorCommand* command =
			LUMIX_NEW(m_allocator, ScaleEntityCommand)(*this, entities, count, scale, m_allocator);
		executeCommand(command);
	}


	void setEntitiesRotations(const EntityRef* entities, const Quat* rotations, int count) override
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


	void setEntitiesCoordinate(const EntityRef* entities, int count, float value, Coordinate coord) override
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


	void setEntitiesLocalCoordinate(const EntityRef* entities, int count, float value, Coordinate coord) override
	{
		ASSERT(entities);
		if (count <= 0) return;

		Universe* universe = getUniverse();
		Array<Vec3> poss(m_allocator);
		poss.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			poss.push(universe->getLocalTransform(entities[i]).pos);
			(&poss[i].x)[(int)coord] = value;
		}
		IEditorCommand* command =
			LUMIX_NEW(m_allocator, LocalMoveEntityCommand)(*this, entities, &poss[0], count, m_allocator);
		executeCommand(command);
	}


	void setEntitiesPositions(const EntityRef* entities, const Vec3* positions, int count) override
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

	void setEntitiesPositionsAndRotations(const EntityRef* entities,
		const Vec3* positions,
		const Quat* rotations,
		int count) override
	{
		if (count <= 0) return;
		IEditorCommand* command =
			LUMIX_NEW(m_allocator, MoveEntityCommand)(*this, entities, positions, rotations, count, m_allocator);
		executeCommand(command);
	}


	void setEntityName(EntityRef entity, const char* name) override
	{
		IEditorCommand* command = LUMIX_NEW(m_allocator, SetEntityNameCommand)(*this, entity, name);
		executeCommand(command);
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
			if (m_is_game_mode) ++m_game_mode_commands;
			++m_undo_index;
			return;
		}
		LUMIX_DELETE(m_allocator, command);
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
			beginCommandGroup(0);
			endCommandGroup();
			m_game_mode_commands = 2;
			m_engine->startGame(*m_universe);
		}
	}


	void stopGameMode(bool reload)
	{
		for (int i = 0; i < m_game_mode_commands; ++i)
		{
			LUMIX_DELETE(m_allocator, m_undo_stack.back());
			m_undo_stack.pop();
			--m_undo_index;
		}

		ASSERT(m_universe);
		m_engine->getResourceManager().enableUnload(false);
		m_engine->stopGame(*m_universe);
		selectEntities(nullptr, 0, false);
		m_gizmo->clearEntities();
		m_editor_icons->clear();
		m_is_game_mode = false;
		if (reload)
		{
			m_universe_destroyed.invoke();
			m_game_mode_file->seek(FS::SeekMode::BEGIN, 0);
			StaticString<64> name(m_universe->getName());
			m_engine->destroyUniverse(*m_universe);
			
			m_universe = &m_engine->createUniverse(true);
			m_universe_created.invoke();
			m_universe->setName(name);
			m_universe->entityDestroyed().bind<WorldEditorImpl, &WorldEditorImpl::onEntityDestroyed>(this);
			m_selected_entities.clear();
			load(*m_game_mode_file);
		}
		m_engine->getFileSystem().close(*m_game_mode_file);
		m_game_mode_file = nullptr;
		if(m_selected_entity_on_game_mode.isValid()) {
			EntityRef e = (EntityRef)m_selected_entity_on_game_mode;
			selectEntities(&e, 1, false);
		}
		m_engine->getResourceManager().enableUnload(true);
	}


	PrefabSystem& getPrefabSystem() override
	{
		return *m_prefab_system;
	}


	void copyEntities(const EntityRef* entities, int count, ISerializer& serializer) override
	{
		serializer.write("count", count);
		for (int i = 0; i < count; ++i)
		{
			EntityRef entity = entities[i];
			Transform tr = m_universe->getTransform(entity);
			serializer.write("transform", tr);
			serializer.write("parent", m_universe->getParent(entity));

			i32 cmp_count = 0;
			for (ComponentUID cmp = m_universe->getFirstComponent(entity); cmp.isValid();
				 cmp = m_universe->getNextComponent(cmp))
			{
				++cmp_count;
			}

			serializer.write("cmp_count", cmp_count);
			for (ComponentUID cmp = m_universe->getFirstComponent(entity);
				cmp.isValid();
				cmp = m_universe->getNextComponent(cmp))
			{
				u32 cmp_type = Reflection::getComponentTypeHash(cmp.type);
				serializer.write("cmp_type", cmp_type);
				const Reflection::ComponentBase* cmp_desc = Reflection::getComponent(cmp.type);
				
				m_universe->serializeComponent(serializer, cmp.type, (EntityRef)cmp.entity);
			}
		}
	}


	void copyEntities() override
	{
		if (m_selected_entities.empty()) return;

		m_copy_buffer.clear();

		struct : ISaveEntityGUIDMap {
			EntityGUID get(EntityPtr entity) override {
				if (!entity.isValid()) return INVALID_ENTITY_GUID;
				
				int idx = editor->m_selected_entities.indexOf((EntityRef)entity);
				if (idx >= 0) {
					return { (u64)idx };
				}
				return { ((u64)1 << 32) | (u64)entity.index };
			}

			WorldEditorImpl* editor;
		} map;
		map.editor = this;

		TextSerializer serializer(m_copy_buffer, map);

		Array<EntityRef> entities(m_allocator);
		entities = m_selected_entities;
		for (EntityRef e : entities) {
			for (EntityPtr child = m_universe->getFirstChild(e); 
				child.isValid(); 
				child = m_universe->getNextSibling((EntityRef)child)) 
			{
				if(entities.indexOf((EntityRef)child) < 0) entities.push((EntityRef)child);
			}
		}
		copyEntities(&entities[0], entities.size(), serializer);
	}


	bool canPasteEntities() const override
	{
		return m_copy_buffer.getPos() > 0;
	}


	void pasteEntities() override;
	void duplicateEntities() override;


	void cloneComponent(const ComponentUID& src, EntityRef entity) override
	{
		IScene* scene = m_universe->getScene(src.type);
		m_universe->createComponent(src.type, entity);
		ComponentUID clone(entity, src.type, scene);

		const Reflection::ComponentBase* cmp_desc = Reflection::getComponent(src.type);
		OutputBlob stream(m_allocator);
		
		SaveVisitor save;
		save.stream = &stream;
		save.cmp = src;
		cmp_desc->visit(save);

		InputBlob blob(stream);
		::Lumix::load(clone, -1, blob);
	}


	void destroyComponent(const EntityRef* entities, int count, ComponentType cmp_type) override
	{
		ASSERT(count > 0);
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
		const Universe* universe = getUniverse();
		if (m_selected_entities.empty()) return;

		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		m_go_to_parameters.m_from = m_viewport.pos;
		const Vec3 dir = m_viewport.rot.rotate(Vec3(0, 0, 1));
		m_go_to_parameters.m_to = universe->getPosition(m_selected_entities[0]) + dir * 10;
		float len = (m_go_to_parameters.m_to - m_go_to_parameters.m_from).length();
		m_go_to_parameters.m_speed = Math::maximum(100.0f / (len > 0 ? len : 1), 2.0f);
		m_go_to_parameters.m_from_rot = m_go_to_parameters.m_to_rot = m_viewport.rot;
	}


	void loadUniverse(const char* basename) override
	{
		if (m_is_game_mode) stopGameMode(false);
		destroyUniverse();
		createUniverse();
		m_universe->setName(basename);
		g_log_info.log("Editor") << "Loading universe " << basename << "...";
		if (!deserialize(*m_universe, "universes/", basename, *m_prefab_system, m_entity_map, m_allocator)) newUniverse();
		m_editor_icons->refresh();
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

		if (m_engine->deserialize(*m_universe, blob))
		{
			m_prefab_system->deserialize(blob);
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


	EditorIcons& getIcons() override
	{
		return *m_editor_icons;
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
		, m_editor_command_creators(m_allocator)
		, m_is_loading(false)
		, m_universe(nullptr)
		, m_is_orbit(false)
		, m_is_toggle_selection(false)
		, m_mouse_sensitivity(200, 200)
		, m_render_interface(nullptr)
		, m_selected_entity_on_game_mode(INVALID_ENTITY)
		, m_mouse_handling_plugin(nullptr)
		, m_is_game_mode(false)
		, m_snap_mode(SnapMode::NONE)
		, m_undo_index(-1)
		, m_engine(&engine)
		, m_entity_map(m_allocator)
		, m_is_guid_pseudorandom(false)
	{
		m_viewport.is_ortho = false;
		m_viewport.pos.set(0, 0, 0);
		m_viewport.rot.set(0, 0, 0, 1);
		m_viewport.w = -1;
		m_viewport.h = -1;
		m_viewport.fov = Math::degreesToRadians(60.f);
		m_viewport.near = 0.1f;
		m_viewport.far = 100000.f;

		for (auto& i : m_is_mouse_down) i = false;
		for (auto& i : m_is_mouse_click) i = false;
		m_go_to_parameters.m_is_active = false;
		
		m_measure_tool = LUMIX_NEW(m_allocator, MeasureTool)();
		addPlugin(*m_measure_tool);

		const char* plugins[] = { "steam" 
			#ifdef LUMIXENGINE_PLUGINS
				, LUMIXENGINE_PLUGINS
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
		const Quat rot = m_viewport.rot;

		right = m_is_orbit ? 0 : right;

		m_viewport.pos += rot.rotate(Vec3(0, 0, -1)) * forward * speed;
		m_viewport.pos += rot.rotate(Vec3(1, 0, 0)) * right * speed;
		m_viewport.pos += rot.rotate(Vec3(0, 1, 0)) * up * speed;
	}


	bool isEntitySelected(EntityRef entity) const override
	{
		return m_selected_entities.indexOf(entity) >= 0;
	}


	const Array<EntityRef>& getSelectedEntities() const override
	{
		return m_selected_entities;
	}


	void setSnapMode(bool enable, bool vertex_snap) override
	{
		m_snap_mode = enable ? (vertex_snap ? SnapMode::VERTEX : SnapMode::FREE) : SnapMode::NONE;
	}


	void setToggleSelection(bool is_toggle) override { m_is_toggle_selection = is_toggle; }


	void addArrayPropertyItem(const ComponentUID& cmp, const Reflection::IArrayProperty& property) override
	{
		if (cmp.isValid())
		{
			IEditorCommand* command =
				LUMIX_NEW(m_allocator, AddArrayPropertyItemCommand)(*this, cmp, property);
			executeCommand(command);
		}
	}


	void removeArrayPropertyItem(const ComponentUID& cmp, int index, const Reflection::IArrayProperty& property) override
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
		const Reflection::PropertyBase& property,
		const EntityRef* entities,
		int count,
		const void* data,
		int size) override
	{
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
		const Quat rot = m_viewport.rot;

		if(m_is_orbit) {
			m_orbit_delta.x += x;
			m_orbit_delta.y += y;
		}

		m_viewport.pos += rot.rotate(Vec3(x, 0, 0));
		m_viewport.pos += rot.rotate(Vec3(0, -y, 0));
	}


	Vec2 getMouseSensitivity() override
	{
		return m_mouse_sensitivity;
	}


	void setMouseSensitivity(float x, float y) override
	{
		m_mouse_sensitivity.x = 10000 / x;
		m_mouse_sensitivity.y = 10000 / y;
	}


	void rotateCamera(int x, int y)
	{
		const Universe* universe = getUniverse();
		Vec3 pos = m_viewport.pos;
		Quat rot = m_viewport.rot;
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

		m_viewport.pos = pos;
		m_viewport.rot = rot;
	}


	void selectEntities(const EntityRef* entities, int count, bool toggle) override
	{
		if (!toggle || !m_is_toggle_selection)
		{
			m_gizmo->clearEntities();
			m_selected_entities.clear();
			for (int i = 0; i < count; ++i)
			{
				m_selected_entities.push(entities[i]);
			}
		}
		else
		{
			for (int i = 0; i < count; ++i)
			{
				int idx = m_selected_entities.indexOf(entities[i]);
				if (idx < 0)
				{
					m_selected_entities.push(entities[i]);
				}
				else
				{
					m_selected_entities.eraseFast(idx);
				}
			}
		}

		m_selected_entities.removeDuplicates();
		m_entity_selected.invoke(m_selected_entities);
	}


	void onEntityDestroyed(EntityRef entity)
	{
		m_selected_entities.eraseItemFast(entity);
	}


	void destroyUniverse()
	{
		if (m_is_game_mode) stopGameMode(false);

		ASSERT(m_universe);
		destroyUndoStack();
		m_universe_destroyed.invoke();
		m_editor_icons->clear();
		m_gizmo->clearEntities();
		selectEntities(nullptr, 0, false);
		m_engine->destroyUniverse(*m_universe);
		m_universe = nullptr;
	}


	DelegateList<void()>& universeCreated() override
	{
		return m_universe_created;
	}


	DelegateList<void(const Array<EntityRef>&)>& entitySelected() override
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
		m_universe = &m_engine->createUniverse(true);
		Universe* universe = m_universe;

		universe->entityDestroyed().bind<WorldEditorImpl, &WorldEditorImpl::onEntityDestroyed>(this);

		m_is_orbit = false;
		m_selected_entities.clear();
		m_universe_created.invoke();

		m_entity_map.is_random = !m_is_guid_pseudorandom;
		m_entity_map.clear();
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
			JsonSerializer serializer(*file, path);
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
			JsonDeserializer serializer(*file, path, m_allocator);
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
		m_go_to_parameters.m_from = m_viewport.pos;
		m_go_to_parameters.m_to = m_go_to_parameters.m_from;
		if (m_is_orbit && !m_selected_entities.empty())
		{
			m_go_to_parameters.m_to = universe->getPosition(m_selected_entities[0]) + Vec3(0, 10, 0);
		}
		m_go_to_parameters.m_speed = 2.0f;
		m_go_to_parameters.m_from_rot = m_viewport.rot;
		m_go_to_parameters.m_to_rot = Quat(Vec3(1, 0, 0), -Math::PI * 0.5f);
	}


	void setFrontView() override
	{
		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		auto* universe = m_universe;
		m_go_to_parameters.m_from = m_viewport.pos;
		m_go_to_parameters.m_to = m_go_to_parameters.m_from;
		if (m_is_orbit && !m_selected_entities.empty())
		{
			m_go_to_parameters.m_to = universe->getPosition(m_selected_entities[0]) + Vec3(0, 0, -10);
		}
		m_go_to_parameters.m_speed = 2.0f;
		m_go_to_parameters.m_from_rot = m_viewport.rot;
		m_go_to_parameters.m_to_rot = Quat(Vec3(0, 1, 0), Math::PI);
	}


	void setSideView() override
	{
		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		auto* universe = m_universe;
		m_go_to_parameters.m_from = m_viewport.pos;
		m_go_to_parameters.m_to = m_go_to_parameters.m_from;
		if (m_is_orbit && !m_selected_entities.empty())
		{
			m_go_to_parameters.m_to = universe->getPosition(m_selected_entities[0]) + Vec3(-10, 0, 0);
		}
		m_go_to_parameters.m_speed = 2.0f;
		m_go_to_parameters.m_from_rot = m_viewport.rot;
		m_go_to_parameters.m_to_rot = Quat(Vec3(0, 1, 0), -Math::PI * 0.5f);
	}


	static int getEntitiesCount(Universe& universe)
	{
		int count = 0;
		for (EntityPtr e = universe.getFirstEntity(); e.isValid(); e = universe.getNextEntity((EntityRef)e)) ++count;
		return count;
	}


	bool runTest(const char* dir, const char* name) override
	{
		FS::FileSystem& fs = m_engine->getFileSystem();
		while (fs.hasWork()) fs.updateAsyncTransactions();
		newUniverse();
		Path undo_stack_path(dir, name, ".json");
		executeUndoStack(undo_stack_path);

		OutputBlob blob0(m_allocator);
		OutputBlob blob1(m_allocator);
		Universe& tpl_universe = m_engine->createUniverse(true);
		PrefabSystem* prefab_system = PrefabSystem::create(*this);
		EntityGUIDMap entity_guid_map(m_allocator);
		bool is_same = deserialize(tpl_universe, "unit_tests/editor", name, *prefab_system, entity_guid_map, m_allocator);
		if (!is_same) goto end;
		if (getEntitiesCount(tpl_universe) != getEntitiesCount(*m_universe))
		{
			is_same = false;
			goto end;
		}

		for (EntityPtr e_ptr = tpl_universe.getFirstEntity(); e_ptr.isValid(); e_ptr = tpl_universe.getNextEntity((EntityRef)e_ptr))
		{
			const EntityRef e = (EntityRef)e_ptr;
			EntityGUID guid = entity_guid_map.get(e);
			EntityPtr other_entity_ptr = m_entity_map.get(guid);
			if (!other_entity_ptr.isValid())
			{
				is_same = false;
				goto end;
			}

			EntityRef other_entity = (EntityRef)other_entity_ptr;
			for (ComponentUID cmp = tpl_universe.getFirstComponent(e); cmp.isValid(); cmp = tpl_universe.getNextComponent(cmp))
			{
				if (!m_universe->hasComponent(other_entity, cmp.type))
				{
					is_same = false;
					goto end;
				}

				ComponentUID other_cmp = m_universe->getComponent(other_entity, cmp.type);

				const Reflection::ComponentBase* base = Reflection::getComponent(cmp.type);
				struct : Reflection::ISimpleComponentVisitor
				{
					void visitProperty(const Reflection::PropertyBase& prop) override
					{
						blob0->clear();
						blob1->clear();
						prop.getValue(cmp, index, *blob0);
						prop.getValue(other_cmp, index, *blob1);
						if (blob0->getPos() != blob1->getPos() 
							|| compareMemory(blob0->getData(), blob1->getData(), blob0->getPos()) != 0)
						{
							*is_same = false;
						}
					}
					int index = -1;
					bool* is_same;
					ComponentUID cmp;
					ComponentUID other_cmp;
					OutputBlob* blob0;
					OutputBlob* blob1;
				} visitor;
				visitor.is_same = &is_same;
				visitor.cmp = cmp;
				visitor.other_cmp = other_cmp;
				visitor.blob0 = &blob0;
				visitor.blob1 = &blob1;
				base->visit(visitor);
				if (!is_same) goto end;
			}
		}

		end:
			m_engine->destroyUniverse(tpl_universe);
			PrefabSystem::destroy(prefab_system);
			return is_same;
	}


private:
	enum class MouseMode
	{
		NONE,
		SELECT,
		NAVIGATE,
		PAN,

		CUSTOM
	};

	enum class SnapMode
	{
		NONE,
		FREE,
		VERTEX
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
	Array<EntityRef> m_selected_entities;
	MouseMode m_mouse_mode;
	Vec2 m_rect_selection_start;
	EditorIcons* m_editor_icons;
	Vec2 m_mouse_pos;
	float m_mouse_rel_x;
	float m_mouse_rel_y;
	Vec2 m_orbit_delta;
	Vec2 m_mouse_sensitivity;
	bool m_is_game_mode;
	int m_game_mode_commands;
	bool m_is_orbit;
	bool m_is_toggle_selection;
	SnapMode m_snap_mode;
	FS::IFile* m_game_mode_file;
	Engine* m_engine;
	EntityPtr m_selected_entity_on_game_mode;
	DelegateList<void()> m_universe_destroyed;
	DelegateList<void()> m_universe_created;
	DelegateList<void(const Array<EntityRef>&)> m_entity_selected;
	bool m_is_mouse_down[MouseButton::RIGHT + 1];
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
	RenderInterface* m_render_interface;
	u32 m_current_group_type;
	bool m_is_universe_changed;
	bool m_is_guid_pseudorandom;
	Viewport m_viewport;
};


class PasteEntityCommand final : public IEditorCommand
{
public:
	explicit PasteEntityCommand(WorldEditor& editor)
		: m_copy_buffer(editor.getAllocator())
		, m_editor(editor)
		, m_entities(editor.getAllocator())
		, m_identity(false)
	{
	}


	PasteEntityCommand(WorldEditor& editor, const OutputBlob& copy_buffer, bool identity = false)
		: m_copy_buffer(copy_buffer)
		, m_editor(editor)
		, m_position(editor.getCameraRaycastHit())
		, m_entities(editor.getAllocator())
		, m_identity(identity)
	{
	}


	PasteEntityCommand(WorldEditor& editor, const Vec3& pos, const OutputBlob& copy_buffer, bool identity = false)
		: m_copy_buffer(copy_buffer)
		, m_editor(editor)
		, m_position(pos)
		, m_entities(editor.getAllocator())
		, m_identity(identity)
	{
	}


	void serialize(JsonSerializer& serializer) override
	{
		serializer.serialize("pos_x", m_position.x);
		serializer.serialize("pos_y", m_position.y);
		serializer.serialize("pos_z", m_position.z);
		serializer.serialize("identity", m_identity);
		serializer.serialize("size", m_copy_buffer.getPos());
		serializer.beginArray("data");
		for (int i = 0; i < m_copy_buffer.getPos(); ++i)
		{
			serializer.serializeArrayItem((i32)((const u8*)m_copy_buffer.getData())[i]);
		}
		serializer.endArray();
	}


	void deserialize(JsonDeserializer& serializer) override
	{
		serializer.deserialize("pos_x", m_position.x, 0);
		serializer.deserialize("pos_y", m_position.y, 0);
		serializer.deserialize("pos_z", m_position.z, 0);
		serializer.deserialize("identity", m_identity, false);
		int size;
		serializer.deserialize("size", size, 0);
		serializer.deserializeArrayBegin("data");
		m_copy_buffer.clear();
		m_copy_buffer.reserve(size);
		for (int i = 0; i < size; ++i)
		{
			i32 data;
			serializer.deserializeArrayItem(data, 0);
			m_copy_buffer.write((u8)data);
		}
		serializer.deserializeArrayEnd();
	}


	bool execute() override
	{
		struct Map : ILoadEntityGUIDMap {
			Map(IAllocator& allocator) : entities(allocator) {}

			EntityPtr get(EntityGUID guid) override 
			{
				if (guid == INVALID_ENTITY_GUID) return INVALID_ENTITY;

				if (guid.value > 0xffFFffFF) return { (int)guid.value }; ;
				
				return entities[(int)guid.value];
			}

			Array<EntityRef> entities;
		} map(m_editor.getAllocator());
		InputBlob input_blob(m_copy_buffer);
		TextDeserializer deserializer(input_blob, map);

		Universe& universe = *m_editor.getUniverse();
		int entity_count;
		deserializer.read(&entity_count);
		map.entities.resize(entity_count);
		bool is_redo = !m_entities.empty();
		for (int i = 0; i < entity_count; ++i)
		{
			if (is_redo)
			{
				map.entities[i] = m_entities[i];
				universe.emplaceEntity(m_entities[i]);
			}
			else
			{
				map.entities[i] = universe.createEntity(Vec3(0, 0, 0), Quat(0, 0, 0, 1));
			}
		}

		m_entities.reserve(entity_count);

		Matrix base_matrix = Matrix::IDENTITY;
		base_matrix.setTranslation(m_position);
		for (int i = 0; i < entity_count; ++i)
		{
			Transform tr;
			deserializer.read(&tr);
			Matrix mtx = tr.toMatrix();
			EntityPtr parent;
			deserializer.read(&parent);

			if (!m_identity)
			{
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
			}

			const EntityRef new_entity = map.entities[i];
			((WorldEditorImpl&)m_editor).m_entity_map.create(new_entity);
			if (!is_redo) m_entities.push(new_entity);
			universe.setMatrix(new_entity, mtx);
			universe.setParent(parent, new_entity);
			i32 count;
			deserializer.read(&count);
			for (int j = 0; j < count; ++j)
			{
				u32 hash;
				deserializer.read(&hash);
				ComponentType type = Reflection::getComponentTypeFromHash(hash);
				const int scene_version = universe.getScene(type)->getVersion();
				universe.deserializeComponent(deserializer, new_entity, type, scene_version);
			}
		}
		return true;
	}


	void undo() override
	{
		for (auto entity : m_entities)
		{
			m_editor.getUniverse()->destroyEntity(entity);
			((WorldEditorImpl&)m_editor).m_entity_map.erase(entity);
		}
	}


	const char* getType() override { return "paste_entity"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		return false;
	}


	const Array<EntityRef>& getEntities() { return m_entities; }


private:
	OutputBlob m_copy_buffer;
	WorldEditor& m_editor;
	Vec3 m_position;
	Array<EntityRef> m_entities;
	bool m_identity;
};


void WorldEditorImpl::pasteEntities()
{
	PasteEntityCommand* command = LUMIX_NEW(m_allocator, PasteEntityCommand)(*this, m_copy_buffer);
	executeCommand(command);
}


void WorldEditorImpl::duplicateEntities()
{
	copyEntities();

	PasteEntityCommand* command = LUMIX_NEW(m_allocator, PasteEntityCommand)(*this, m_copy_buffer, true);
	executeCommand(command);
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
