#include "world_editor.h"

#include "animation/animation_system.h"
#include "core/aabb.h"
#include "core/array.h"
#include "core/associative_array.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/delegate_list.h"
#include "core/fs/file_system.h"
#include "core/fs/memory_file_device.h"
#include "core/fs/disk_file_device.h"
#include "core/fs/tcp_file_device.h"
#include "core/fs/tcp_file_server.h"
#include "core/fs/ifile.h"
#include "core/input_system.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/matrix.h"
#include "core/mt/mutex.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "debug/allocator.h"
#include "editor/editor_icon.h"
#include "editor/entity_template_system.h"
#include "editor/gizmo.h"
#include "editor/measure_tool.h"
#include "editor/property_descriptor.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "engine/plugin_manager.h"
#include "graphics/irender_device.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "graphics/texture.h"
#include "ieditor_command.h"
#include "script/script_system.h"
#include "universe/universe.h"


namespace Lumix
{


static const uint32_t RENDERABLE_HASH = crc32("renderable");
static const uint32_t CAMERA_HASH = crc32("camera");
static const uint32_t LIGHT_HASH = crc32("light");
static const uint32_t SCRIPT_HASH = crc32("script");
static const uint32_t ANIMABLE_HASH = crc32("animable");
static const uint32_t TERRAIN_HASH = crc32("terrain");


class SetEntityNameCommand : public IEditorCommand
{
	public:
		SetEntityNameCommand(WorldEditor& editor)
			: m_new_name(m_editor.getAllocator())
			, m_old_name(m_editor.getAllocator())
			, m_editor(editor)
		{

		}

		SetEntityNameCommand(WorldEditor& editor, Entity entity, const char* name)
			: m_entity(entity)
			, m_new_name(name, m_editor.getAllocator())
			, m_old_name(entity.getName(), m_editor.getAllocator())
			, m_editor(editor)
		{}


		virtual void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("name", m_new_name.c_str());
			serializer.serialize("entity", m_entity.index);
		}


		virtual void deserialize(JsonSerializer& serializer) override
		{
			char name[100];
			serializer.deserialize("name", name, sizeof(name), "");
			m_new_name = name;
			serializer.deserialize("entity", m_entity.index, 0);
			m_entity.universe = m_editor.getEngine().getUniverse();
			m_old_name = m_entity.getName();
		}


		virtual void execute() override
		{
			m_entity.setName(m_new_name.c_str());
			m_editor.entityNameSet().invoke(m_entity, m_new_name.c_str());
		}


		virtual void undo() override
		{
			m_entity.setName(m_old_name.c_str());
			m_editor.entityNameSet().invoke(m_entity, m_old_name.c_str());

		}


		virtual uint32_t getType() override
		{
			static const uint32_t type = crc32("set_entity_name");
			return type;
		}


		virtual bool merge(IEditorCommand& command)
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
		PasteEntityCommand(WorldEditor& editor)
			: m_blob(editor.getAllocator())
			, m_editor(editor)
		{}

		PasteEntityCommand(WorldEditor& editor, OutputBlob& blob)
			: m_blob(blob, editor.getAllocator())
			, m_editor(editor)
			, m_position(editor.getCameraRaycastHit())
			, m_entity(Entity::INVALID)
		{}


		virtual void execute() override;


		virtual void serialize(JsonSerializer& serializer)
		{
			serializer.serialize("pos_x", m_position.x);
			serializer.serialize("pos_y", m_position.y);
			serializer.serialize("pos_z", m_position.z);
			serializer.serialize("entity", m_entity.index);
			serializer.serialize("size", m_blob.getSize());
			serializer.beginArray("data");
			for (int i = 0; i < m_blob.getSize(); ++i)
			{
				serializer.serializeArrayItem((int32_t)((const uint8_t*)m_blob.getData())[i]);
			}
			serializer.endArray();
		}


		virtual void deserialize(JsonSerializer& serializer)
		{
			serializer.deserialize("pos_x", m_position.x, 0);
			serializer.deserialize("pos_y", m_position.y, 0);
			serializer.deserialize("pos_z", m_position.z, 0);
			serializer.deserialize("entity", m_entity.index, 0);
			m_entity.universe = m_editor.getEngine().getUniverse();
			int size;
			serializer.deserialize("size", size, 0);
			serializer.deserializeArrayBegin("data");
			m_blob.clear();
			for (int i = 0; i < m_blob.getSize(); ++i)
			{
				int32_t data;
				serializer.deserializeArrayItem(data, 0);
				m_blob.write((uint8_t)data);
			}
			serializer.deserializeArrayEnd();
		}


		virtual void undo() override
		{
			const WorldEditor::ComponentList& cmps = m_editor.getComponents(m_entity);
			for (int i = 0; i < cmps.size(); ++i)
			{
				cmps[i].scene->destroyComponent(cmps[i]);
			}
			m_editor.getEngine().getUniverse()->destroyEntity(m_entity);
			m_entity = Entity::INVALID;
		}


		virtual uint32_t getType() override
		{
			static const uint32_t type = crc32("paste_entity");
			return type;
		}


		virtual bool merge(IEditorCommand& command)
		{
			ASSERT(command.getType() == getType());
			return false;
		}

	private:
		OutputBlob m_blob;
		WorldEditor& m_editor;
		Vec3 m_position;
		Entity m_entity;
};


class MoveEntityCommand : public IEditorCommand
{
	public:
		MoveEntityCommand(WorldEditor& editor)
			: m_new_positions(editor.getAllocator())
			, m_new_rotations(editor.getAllocator())
			, m_old_positions(editor.getAllocator())
			, m_old_rotations(editor.getAllocator())
			, m_entities(editor.getAllocator())
			, m_editor(editor)
		{
		}


		MoveEntityCommand(WorldEditor& editor, const Array<Entity>& entities, const Array<Vec3>& new_positions, const Array<Quat>& new_rotations, IAllocator& allocator)
			: m_new_positions(allocator)
			, m_new_rotations(allocator)
			, m_old_positions(allocator)
			, m_old_rotations(allocator)
			, m_entities(allocator)
			, m_editor(editor)
		{
			ASSERT(entities.size() == new_positions.size());
			for(int i = entities.size() - 1; i >= 0; --i)
			{
				m_entities.push(entities[i]);
				m_new_positions.push(new_positions[i]);
				m_new_rotations.push(new_rotations[i]);
				m_old_positions.push(entities[i].getPosition());
				m_old_rotations.push(entities[i].getRotation());
			}
		}


		virtual void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("count", m_entities.size());
			serializer.beginArray("entities");
			for (int i = 0; i < m_entities.size(); ++i)
			{
				serializer.serializeArrayItem(m_entities[i].index);
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


		virtual void deserialize(JsonSerializer& serializer) override
		{
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
				serializer.deserializeArrayItem(m_entities[i].index, 0);
				m_entities[i].universe = m_editor.getEngine().getUniverse();
				serializer.deserializeArrayItem(m_new_positions[i].x, 0);
				serializer.deserializeArrayItem(m_new_positions[i].y, 0);
				serializer.deserializeArrayItem(m_new_positions[i].z, 0);
				serializer.deserializeArrayItem(m_new_rotations[i].x, 0);
				serializer.deserializeArrayItem(m_new_rotations[i].y, 0);
				serializer.deserializeArrayItem(m_new_rotations[i].z, 0);
				serializer.deserializeArrayItem(m_new_rotations[i].w, 0);
				m_old_positions[i] = m_entities[i].getPosition();
				m_old_rotations[i] = m_entities[i].getRotation();
			}
			serializer.deserializeArrayEnd();
		}


		virtual void execute() override
		{
			for(int i = 0, c = m_entities.size(); i < c; ++i)
			{
				const Entity& entity = m_entities[i];
				entity.setPosition(m_new_positions[i]);
				entity.setRotation(m_new_rotations[i]);
			}
		}


		virtual void undo() override
		{
			for(int i = 0, c = m_entities.size(); i < c; ++i)
			{
				const Entity& entity = m_entities[i];
				entity.setPosition(m_old_positions[i]);
				entity.setRotation(m_old_rotations[i]);
			}
		}


		virtual uint32_t getType() override
		{
			static const uint32_t type = crc32("move_entity");
			return type;
		}


		virtual bool merge(IEditorCommand& command)
		{
			ASSERT(command.getType() == getType());
			MoveEntityCommand& my_command = static_cast<MoveEntityCommand&>(command);
			if (my_command.m_entities.size() == m_entities.size())
			{
				for(int i = 0, c = m_entities.size(); i < c; ++i)
				{
					if(m_entities[i].index != my_command.m_entities[i].index)
					{
						return false;
					}
				}
				for(int i = 0, c = m_entities.size(); i < c; ++i)
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


class RemoveArrayPropertyItemCommand : public IEditorCommand
{
	
	public:
		RemoveArrayPropertyItemCommand(WorldEditor& editor)
			: m_old_values(editor.getAllocator())
			, m_editor(editor)
		{
		}

		RemoveArrayPropertyItemCommand(WorldEditor& editor, const Component& component, int index, IArrayDescriptor& descriptor)
			: m_component(component)
			, m_index(index)
			, m_descriptor(&descriptor)
			, m_old_values(editor.getAllocator())
			, m_editor(editor)
		{
			for(int i = 0, c = m_descriptor->getChildren().size(); i < c; ++i)
			{
				m_descriptor->getChildren()[i]->get(component, m_index, m_old_values);
			}
		}


		virtual void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("inedx", m_index);
			serializer.serialize("entity_index", m_component.entity.index);
			serializer.serialize("component_index", m_component.index);
			serializer.serialize("component_type", m_component.type);
			serializer.serialize("property_name_hash", m_descriptor->getNameHash());
		}


		virtual void deserialize(JsonSerializer& serializer) override
		{
			serializer.deserialize("inedx", m_index, 0);
			serializer.deserialize("entity_index", m_component.entity.index, 0);
			serializer.deserialize("component_index", m_component.index, 0);
			serializer.deserialize("component_type", m_component.type, 0);
			m_component.entity.universe = m_editor.getEngine().getUniverse();
			m_component.scene = m_editor.getEngine().getSceneByComponentType(m_component.type);
			uint32_t property_name_hash;
			serializer.deserialize("property_name_hash", property_name_hash, 0);
			m_descriptor = static_cast<const IArrayDescriptor*>(&m_editor.getPropertyDescriptor(m_component.type, property_name_hash));
		}


		virtual void execute() override
		{
			m_descriptor->removeArrayItem(m_component, m_index);
		}


		virtual void undo() override
		{
			m_descriptor->addArrayItem(m_component, m_index);
			InputBlob old_values(m_old_values.getData(), m_old_values.getSize());
			for(int i = 0, c = m_descriptor->getChildren().size(); i < c; ++i)
			{
				m_descriptor->getChildren()[i]->set(m_component, m_index, old_values);
			}		
		}


		virtual uint32_t getType() override
		{
			static const uint32_t hash = crc32("remove_array_property_item");
			return hash;
		}


		virtual bool merge(IEditorCommand&)
		{
			return false;
		}

	private:
		WorldEditor& m_editor;
		Component m_component;
		int m_index;
		const IArrayDescriptor* m_descriptor;
		OutputBlob m_old_values;
};


class AddArrayPropertyItemCommand : public IEditorCommand
{
	
	public:
		AddArrayPropertyItemCommand(WorldEditor& editor)
			: m_editor(editor)
		{
		}

		AddArrayPropertyItemCommand(WorldEditor& editor, const Component& component, IArrayDescriptor& descriptor)
			: m_component(component)
			, m_index(-1)
			, m_descriptor(&descriptor)
			, m_editor(editor)
		{
		}


		virtual void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("inedx", m_index);
			serializer.serialize("entity_index", m_component.entity.index);
			serializer.serialize("component_index", m_component.index);
			serializer.serialize("component_type", m_component.type);
			serializer.serialize("property_name_hash", m_descriptor->getNameHash());
		}


		virtual void deserialize(JsonSerializer& serializer) override
		{
			serializer.deserialize("inedx", m_index, 0);
			serializer.deserialize("entity_index", m_component.entity.index, 0);
			serializer.deserialize("component_index", m_component.index, 0);
			serializer.deserialize("component_type", m_component.type, 0);
			m_component.entity.universe = m_editor.getEngine().getUniverse();
			m_component.scene = m_editor.getEngine().getSceneByComponentType(m_component.type);
			uint32_t property_name_hash;
			serializer.deserialize("property_name_hash", property_name_hash, 0);
			m_descriptor = static_cast<const IArrayDescriptor*>(&m_editor.getPropertyDescriptor(m_component.type, property_name_hash));
		}



		virtual void execute() override
		{
			m_descriptor->addArrayItem(m_component, -1);
			m_index = m_descriptor->getCount(m_component) - 1;
		}


		virtual void undo() override
		{
			m_descriptor->removeArrayItem(m_component, m_index);
		}


		virtual uint32_t getType() override
		{
			static const uint32_t hash = crc32("add_array_property_item");
			return hash;
		}


		virtual bool merge(IEditorCommand&)
		{
			return false;
		}

	private:
		Component m_component;
		int m_index;
		const IArrayDescriptor* m_descriptor;
		WorldEditor& m_editor;
};



class SetPropertyCommand : public IEditorCommand
{
	public:
		SetPropertyCommand(WorldEditor& editor)
			: m_editor(editor)
			, m_new_value(editor.getAllocator())
			, m_old_value(editor.getAllocator())
		{
		}


		SetPropertyCommand(WorldEditor& editor, const Component& component, const IPropertyDescriptor& property_descriptor, const void* data, int size)
			: m_component(component)
			, m_property_descriptor(&property_descriptor)
			, m_editor(editor)
			, m_new_value(editor.getAllocator())
			, m_old_value(editor.getAllocator())
		{
			m_index = -1;
			m_new_value.write(data, size);
			m_property_descriptor->get(component, m_old_value);
		}


		SetPropertyCommand(WorldEditor& editor, const Component& component, int index, const IPropertyDescriptor& property_descriptor, const void* data, int size)
			: m_component(component)
			, m_property_descriptor(&property_descriptor)
			, m_editor(editor)
			, m_new_value(editor.getAllocator())
			, m_old_value(editor.getAllocator())
		{
			m_index = index;
			m_new_value.write(data, size);
			m_property_descriptor->get(component, m_index, m_old_value);
		}


		virtual void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("index", m_index);
			serializer.serialize("entity_index", m_component.entity.index);
			serializer.serialize("component_index", m_component.index);
			serializer.serialize("component_type", m_component.type);
			serializer.beginArray("data");
			for (int i = 0; i < m_new_value.getSize(); ++i)
			{
				serializer.serializeArrayItem((int)((const uint8_t*)m_new_value.getData())[i]);
			}
			serializer.endArray();
			serializer.serialize("property_name_hash", m_property_descriptor->getNameHash());
		}


		virtual void deserialize(JsonSerializer& serializer) override
		{
			serializer.deserialize("index", m_index, 0);
			serializer.deserialize("entity_index", m_component.entity.index, 0);
			serializer.deserialize("component_index", m_component.index, 0);
			serializer.deserialize("component_type", m_component.type, 0);
			m_component.entity.universe = m_editor.getEngine().getUniverse();
			m_component.scene = m_editor.getEngine().getSceneByComponentType(m_component.type);
			serializer.deserializeArrayBegin("data");
			m_new_value.clear();
			while (!serializer.isArrayEnd())
			{
				int data;
				serializer.deserializeArrayItem(data, 0);
				m_new_value.write((uint8_t)data);
			}
			serializer.deserializeArrayEnd();
			uint32_t property_name_hash;
			serializer.deserialize("property_name_hash", property_name_hash, 0);
			m_property_descriptor = &m_editor.getPropertyDescriptor(m_component.type, property_name_hash);
		}


		virtual void execute() override
		{
			InputBlob blob(m_new_value);
			set(blob);
		}


		virtual void undo() override
		{
			InputBlob blob(m_old_value);
			set(blob);
		}


		virtual uint32_t getType() override
		{
			static const uint32_t hash = crc32("set_property");
			return hash;
		}


		virtual bool merge(IEditorCommand& command)
		{
			ASSERT(command.getType() == getType());
			SetPropertyCommand& src = static_cast<SetPropertyCommand&>(command);
			if (m_component == src.m_component && &src.m_property_descriptor == &m_property_descriptor && m_index == src.m_index)
			{
				src.m_new_value = m_new_value;
				return true;
			}
			return false;
		}


		void set(InputBlob& stream)
		{
			uint32_t template_hash = m_editor.getEntityTemplateSystem().getTemplate(m_component.entity);
			if (template_hash)
			{
				const Array<Entity>& entities = m_editor.getEntityTemplateSystem().getInstances(template_hash);
				for (int i = 0, c = entities.size(); i < c; ++i)
				{
					stream.rewind();
					const WorldEditor::ComponentList& cmps = m_editor.getComponents(entities[i]);
					for (int j = 0, cj = cmps.size(); j < cj; ++j)
					{
						if (cmps[j].type == m_component.type)
						{
							if(m_index >= 0)
							{
								m_property_descriptor->set(cmps[j], m_index, stream);
							}
							else
							{
								m_property_descriptor->set(cmps[j], stream);
							}
							break;
						}
					}
				}
			}
			else
			{
				if(m_index >= 0)
				{
					m_property_descriptor->set(m_component, m_index, stream);
				}
				else
				{
					m_property_descriptor->set(m_component, stream);
				}
			}
		}


	private:
		WorldEditor& m_editor;
		Component m_component;
		OutputBlob m_new_value;
		OutputBlob m_old_value;
		int m_index;
		const IPropertyDescriptor* m_property_descriptor;
};


struct EditorIconHit
{
	EditorIcon* m_icon;
	float m_t;
};


struct WorldEditorImpl : public WorldEditor
{
	private:
		class AddComponentCommand : public IEditorCommand
		{
			public:
				AddComponentCommand(WorldEditor& editor)
					: m_editor(static_cast<WorldEditorImpl&>(editor))
					, m_entities(editor.getAllocator())
				{
				}

				AddComponentCommand(WorldEditorImpl& editor, const Array<Entity>& entities, uint32_t type)
					: m_editor(editor)
					, m_entities(editor.getAllocator())
				{
					m_type = type;
					m_entities.reserve(entities.size());
					for (int i = 0; i < entities.size(); ++i)
					{
						if (!m_editor.getComponent(entities[i], type).isValid())
						{
							uint32_t tpl = editor.getEntityTemplateSystem().getTemplate(entities[i]);
							if(tpl == 0)
							{
								m_entities.push(entities[i]);
							}
							else
							{
								const Array<Entity>& instances = editor.getEntityTemplateSystem().getInstances(tpl);
								for(int i = 0; i < instances.size(); ++i)
								{
									m_entities.push(instances[i]);
								}
							}
						}
					}
				}


				virtual void serialize(JsonSerializer& serializer) override
				{
					serializer.serialize("component_type", m_type);
					serializer.beginArray("entities");
					for (int i = 0; i < m_entities.size(); ++i)
					{
						serializer.serializeArrayItem(m_entities[i].index);
					}
					serializer.endArray();
				}


				virtual void deserialize(JsonSerializer& serializer) override
				{
					serializer.deserialize("component_type", m_type, 0);
					m_entities.clear();
					serializer.deserializeArrayBegin("entities");
					while (!serializer.isArrayEnd())
					{
						Entity& entity = m_entities.pushEmpty();
						serializer.deserializeArrayItem(entity.index, 0);
						entity.universe = m_editor.getEngine().getUniverse();
					}
					serializer.deserializeArrayEnd();
				}


				virtual bool merge(IEditorCommand&) override
				{
					return false;
				}


				virtual uint32_t getType() override
				{
					static const uint32_t hash = crc32("add_component");
					return hash;
				}


				virtual void execute() override
				{
					const Array<IScene*>& scenes = m_editor.getEngine().getScenes();
				
					for (int j = 0; j < m_entities.size(); ++j)
					{
						for (int i = 0; i < scenes.size(); ++i)
						{
							if (scenes[i]->createComponent(m_type, m_entities[j]).isValid())
							{
								break;
							}
						}
					}
				}


				virtual void undo() override
				{
					for (int i = 0; i < m_entities.size(); ++i)
					{
						const Component& cmp = m_editor.getComponent(m_entities[i], m_type);
						cmp.scene->destroyComponent(cmp);
					}
				}


			private:
				uint32_t m_type;
				Array<Entity> m_entities;
				WorldEditorImpl& m_editor;
		};


		class DestroyEntitiesCommand : public IEditorCommand
		{
			public:
				DestroyEntitiesCommand(WorldEditor& editor)
					: m_editor(static_cast<WorldEditorImpl&>(editor))
					, m_entities(editor.getAllocator())
					, m_positons_rotations(editor.getAllocator())
					, m_old_values(editor.getAllocator())
				{
				}


				DestroyEntitiesCommand(WorldEditorImpl& editor, const Entity* entities, int count)
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


				virtual void serialize(JsonSerializer& serializer) override
				{
					serializer.serialize("count", m_entities.size());
					serializer.beginArray("entities");
					for (int i = 0; i < m_entities.size(); ++i)
					{
						serializer.serializeArrayItem(m_entities[i].index);
						serializer.serializeArrayItem(m_positons_rotations[i].m_position.x);
						serializer.serializeArrayItem(m_positons_rotations[i].m_position.y);
						serializer.serializeArrayItem(m_positons_rotations[i].m_position.z);
						serializer.serializeArrayItem(m_positons_rotations[i].m_rotation.x);
						serializer.serializeArrayItem(m_positons_rotations[i].m_rotation.y);
						serializer.serializeArrayItem(m_positons_rotations[i].m_rotation.z);
						serializer.serializeArrayItem(m_positons_rotations[i].m_rotation.w);
					}
					serializer.endArray();
				}


				virtual void deserialize(JsonSerializer& serializer) override
				{
					int count;
					serializer.deserialize("count", count, 0);
					serializer.deserializeArrayBegin("entities");
					m_entities.resize(count);
					m_positons_rotations.resize(count);
					for (int i = 0; i < count; ++i)
					{
						serializer.deserializeArrayItem(m_entities[i].index, 0);
						m_entities[i].universe = m_editor.getEngine().getUniverse();
						serializer.deserializeArrayItem(m_positons_rotations[i].m_position.x, 0);
						serializer.deserializeArrayItem(m_positons_rotations[i].m_position.y, 0);
						serializer.deserializeArrayItem(m_positons_rotations[i].m_position.z, 0);
						serializer.deserializeArrayItem(m_positons_rotations[i].m_rotation.x, 0);
						serializer.deserializeArrayItem(m_positons_rotations[i].m_rotation.y, 0);
						serializer.deserializeArrayItem(m_positons_rotations[i].m_rotation.z, 0);
						serializer.deserializeArrayItem(m_positons_rotations[i].m_rotation.w, 0);
					}
					serializer.deserializeArrayEnd();
				}


				virtual void execute() override
				{
					m_positons_rotations.clear();
					m_old_values.clear();
					for (int i = 0; i < m_entities.size(); ++i)
					{
						const WorldEditor::ComponentList& cmps = m_editor.getComponents(m_entities[i]);
						PositionRotation pos_rot;
						pos_rot.m_position = m_entities[i].getPosition();
						pos_rot.m_rotation = m_entities[i].getRotation();
						m_positons_rotations.push(pos_rot);
						m_old_values.write((int)cmps.size());
						for (int j = cmps.size() - 1; j >= 0; --j)
						{
							m_old_values.write(cmps[j].type);
							int props_index = m_editor.m_component_properties.find(cmps[j].type);
							if (props_index >= 0)
							{
								Array<IPropertyDescriptor*>& props = m_editor.m_component_properties.at(props_index);
								for (int k = 0; k < props.size(); ++k)
								{
									props[k]->get(cmps[j], m_old_values);
								}
							}
							cmps[j].scene->destroyComponent(cmps[j]);
						}

						m_entities[i].universe->destroyEntity(m_entities[i]);
					}
				}


				virtual bool merge(IEditorCommand&) override
				{
					return false;
				}


				virtual void undo() override
				{
					const Array<IScene*>& scenes = m_editor.getEngine().getScenes();
					InputBlob blob(m_old_values);
					for (int i = 0; i < m_entities.size(); ++i)
					{
						Entity new_entity = m_editor.getEngine().getUniverse()->createEntity();
						new_entity.setPosition(m_positons_rotations[i].m_position);
						new_entity.setRotation(m_positons_rotations[i].m_rotation);
						int cmps_count;
						blob.read(cmps_count);
						for (int j = cmps_count - 1; j >= 0; --j)
						{
							Component::Type cmp_type;
							blob.read(cmp_type);
							Component new_component;
							for (int i = 0; i < scenes.size(); ++i)
							{
								new_component = scenes[i]->createComponent(cmp_type, new_entity);
								if (new_component.isValid())
								{
									break;
								}
							}

							int props_index = m_editor.m_component_properties.find(cmp_type);
							if (props_index >= 0)
							{
								Array<IPropertyDescriptor*>& props = m_editor.m_component_properties.at(props_index);

								for (int k = 0; k < props.size(); ++k)
								{
									props[k]->set(new_component, blob);
								}
							}
						}
					}
				}


				virtual uint32_t getType() override
				{
					static const uint32_t hash = crc32("destroy_entities");
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
				DestroyComponentCommand(WorldEditor& editor)
					: m_editor(static_cast<WorldEditorImpl&>(editor))
					, m_old_values(editor.getAllocator())
				{
				}


				DestroyComponentCommand(WorldEditorImpl& editor, const Component& component)
					: m_component(component)
					, m_editor(editor)
					, m_old_values(editor.getAllocator())
				{
				}


				virtual void serialize(JsonSerializer& serializer)
				{
					serializer.serialize("entity", m_component.entity.index);
					serializer.serialize("component", m_component.index);
					serializer.serialize("component_type", m_component.type);
				}


				virtual void deserialize(JsonSerializer& serializer)
				{
					serializer.deserialize("entity", m_component.entity.index, 0);
					serializer.deserialize("component", m_component.index, 0);
					serializer.deserialize("component_type", m_component.type, 0);
					m_component.entity.universe = m_editor.getEngine().getUniverse();
					m_component.scene = m_editor.getEngine().getSceneByComponentType(m_component.type);
				}


				virtual void undo() override
				{
					uint32_t template_hash = m_editor.m_template_system->getTemplate(m_component.entity);
					const Array<IScene*>& scenes = m_editor.m_engine->getScenes();
					int props_index = m_editor.m_component_properties.find(m_component.type);

					if (template_hash == 0)
					{
						for (int i = 0; i < scenes.size(); ++i)
						{
							const Lumix::Component& cmp = scenes[i]->createComponent(m_component.type, m_component.entity);
							if (cmp.isValid())
							{
								m_component = cmp;
								break;
							}
						}
						InputBlob blob(m_old_values);
						if (props_index >= 0)
						{
							const Array<IPropertyDescriptor*>& props = m_editor.m_component_properties.at(props_index);
							for (int i = 0; i < props.size(); ++i)
							{
								props[i]->set(m_component, blob);
							}
						}
					}
					else
					{
						const Array<Entity>& entities = m_editor.m_template_system->getInstances(template_hash);
						for (int entity_index = 0, c = entities.size(); entity_index < c; ++entity_index)
						{
							for (int scene_index = 0; scene_index < scenes.size(); ++scene_index)
							{
								Component cmp_new = scenes[scene_index]->createComponent(m_component.type, entities[entity_index]);
								if (cmp_new.isValid())
								{
									InputBlob blob(m_old_values);
									if (props_index >= 0)
									{
										const Array<IPropertyDescriptor*>& props = m_editor.m_component_properties.at(props_index);
										for (int i = 0; i < props.size(); ++i)
										{
											props[i]->set(cmp_new, blob);
										}
									}
								}
							}
						}
					}
				}


				virtual bool merge(IEditorCommand&) override
				{
					return false;
				}


				virtual uint32_t getType() override
				{
					static const uint32_t hash = crc32("destroy_component");
					return hash;
				}


				virtual void execute() override
				{
					Array<IPropertyDescriptor*>& props = m_editor.getPropertyDescriptors(m_component.type);
					for (int i = 0; i < props.size(); ++i)
					{
						props[i]->get(m_component, m_old_values);
					}
					uint32_t template_hash = m_editor.getEntityTemplateSystem().getTemplate(m_component.entity);
					if (template_hash)
					{
						const Array<Entity>& instances = m_editor.m_template_system->getInstances(template_hash);
						for (int i = 0; i < instances.size(); ++i)
						{
							Component cmp = m_editor.getComponent(instances[i], m_component.type);
							if(cmp.isValid())
							{
								cmp.scene->destroyComponent(cmp);
							}
						}
					}
					else
					{
						m_component.scene->destroyComponent(m_component);
					}
				}

			private:
				Component m_component;
				WorldEditorImpl& m_editor;
				OutputBlob m_old_values;
		};


		class AddEntityCommand : public IEditorCommand
		{
			public:
				AddEntityCommand(WorldEditor& editor)
					: m_editor(static_cast<WorldEditorImpl&>(editor))
				{
				}


				AddEntityCommand(WorldEditorImpl& editor, const Vec3& position)
					: m_editor(editor)
				{
					m_position = position;
				}


				virtual void execute() override
				{
					m_entity = m_editor.getEngine().getUniverse()->createEntity();
					m_entity.setPosition(m_position);
					m_editor.selectEntities(&m_entity, 1);
				}


				virtual void serialize(JsonSerializer& serializer) override
				{
					serializer.serialize("pos_x", m_position.x);
					serializer.serialize("pos_y", m_position.y);
					serializer.serialize("pos_z", m_position.z);
				}


				virtual void deserialize(JsonSerializer& serializer) override
				{
					serializer.deserialize("pos_x", m_position.x, 0);
					serializer.deserialize("pos_y", m_position.y, 0);
					serializer.deserialize("pos_z", m_position.z, 0);
				}


				virtual void undo() override
				{
					m_editor.getEngine().getUniverse()->destroyEntity(m_entity);
				}


				virtual bool merge(IEditorCommand&) override
				{
					return false;
				}


				virtual uint32_t getType() override
				{
					static const uint32_t hash = crc32("add_entity");
					return hash;
				}


				const Entity& getEntity() const
				{
					return m_entity;
				}


			private:
				WorldEditorImpl& m_editor;
				Entity m_entity;
				Vec3 m_position;
		};

	public:
		virtual const char* getBasePath() override
		{
			return m_base_path.c_str();
		}


		virtual IAllocator& getAllocator() override
		{
			return m_allocator;
		}


		virtual Engine& getEngine() override
		{
			return *m_engine;
		}


		virtual IPropertyDescriptor* getProperty(const char* component_type, const char* property_name) override
		{
			auto& props = getPropertyDescriptors(crc32(component_type));
			auto name_hash = crc32(property_name);
			for(int i = 0; i < props.size(); ++i)
			{
				if(props[i]->getNameHash() == name_hash)
				{
					return props[i];
				}
			}
			return NULL;
		}


		virtual void registerProperty(const char* component_type, IPropertyDescriptor* descriptor) override
		{
			ASSERT(descriptor);
			getPropertyDescriptors(crc32(component_type)).push(descriptor);
		}


		Vec3 minCoords(const Vec3& a, const Vec3& b)
		{
			return Vec3(
				Math::minValue(a.x, b.x),
				Math::minValue(a.y, b.y),
				Math::minValue(a.z, b.z)
			);
		}


		Vec3 maxCoords(const Vec3& a, const Vec3& b)
		{
			return Vec3(
				Math::maxValue(a.x, b.x),
				Math::maxValue(a.y, b.y),
				Math::maxValue(a.z, b.z)
			);
		}


		void createEditorLines()
		{
			Component camera_cmp = getComponent(m_camera, CAMERA_HASH);
			RenderScene* scene = static_cast<RenderScene*>(camera_cmp.scene);
			bool first_found = true;
			Vec3 all_min;
			Vec3 all_max;
			for(int i = 0; i < m_selected_entities.size(); ++i)
			{
				Component renderable = getComponent(m_selected_entities[i], RENDERABLE_HASH);
				if(renderable.isValid())
				{
					Model* model = scene->getRenderableModel(renderable);
					Vec3 points[8];
					if (model)
					{
						const AABB& aabb = model->getAABB();
						points[0] = aabb.getMin();
						points[7] = aabb.getMax();
						points[1].set(points[0].x, points[0].y, points[7].z);
						points[2].set(points[0].x, points[7].y, points[0].z);
						points[3].set(points[0].x, points[7].y, points[7].z);
						points[4].set(points[7].x, points[0].y, points[0].z);
						points[5].set(points[7].x, points[0].y, points[7].z);
						points[6].set(points[7].x, points[7].y, points[0].z);
						Matrix mtx = m_selected_entities[i].getMatrix();

						Vec3 this_min, this_max;
						for (int j = 0; j < 8; ++j)
						{
							points[j] = mtx.multiplyPosition(points[j]);
						}

						this_min = points[0];
						this_max = points[0];

						for (int j = 0; j < 8; ++j)
						{
							this_min = minCoords(points[j], this_min);
							this_max = maxCoords(points[j], this_max);
						}

						if (i > 0)
						{
							all_min = minCoords(this_min, all_min);
							all_max = maxCoords(this_max, all_max);
						}
						else
						{
							all_min = this_min;
							all_max = this_max;
						}

						scene->addDebugCube(this_min, this_max, Vec3(1, 0, 0), 0);
					}
				}
				else
				{
					Vec3 pos = m_selected_entities[i].getPosition();
					if(first_found)
					{
						first_found = false;
						all_min = pos - Vec3(0.5f, 0.5f, 0.5f); 
						all_max = pos + Vec3(0.5f, 0.5f, 0.5f); 
					}
					else
					{
						all_min = minCoords(pos - Vec3(0.1f, 0.1f, 0.1f), all_min);
						all_max = maxCoords(pos - Vec3(0.1f, 0.1f, 0.1f), all_max);
					}
				}
			}
			if (m_selected_entities.size() > 1)
			{
				scene->addDebugCube(all_min, all_max, Vec3(1, 1, 0), 0);
			}
			m_measure_tool->createEditorLines(*scene);
		}

		
		void updateGoTo()
		{
			if (m_camera.isValid() && m_go_to_parameters.m_is_active)
			{
				float t = Math::easeInOut(m_go_to_parameters.m_t);
				m_go_to_parameters.m_t += m_engine->getLastTimeDelta() * m_go_to_parameters.m_speed;
				Vec3 pos = m_go_to_parameters.m_from * (1 - t) + m_go_to_parameters.m_to * t;
				if (m_go_to_parameters.m_t >= 1)
				{
					pos = m_go_to_parameters.m_to;
					m_go_to_parameters.m_is_active = false;
				}
				m_camera.setPosition(pos);
			}
		}


		virtual void tick() override
		{
			char fps[100];
			copyString(fps, sizeof(fps), "FPS: ");
			toCString(m_engine->getFPS(), fps + strlen(fps), sizeof(fps) - strlen(fps), 1);
			static_cast<RenderScene*>(m_engine->getScene(crc32("renderer")))->setDebugText(m_fps_text, fps);

			updateGoTo();

			for (int i = 0; i < m_plugins.size(); ++i)
			{
				m_plugins[i]->tick();
			}
			if (m_toggle_game_mode_requested)
			{
				toggleGameMode();
				m_toggle_game_mode_requested = false;
			}
			m_engine->update(m_is_game_mode, 1);
			m_engine->getFileSystem().updateAsyncTransactions();
			createEditorLines();
		}

	
		virtual ~WorldEditorImpl()
		{
			
		}


		EditorIconHit raycastEditorIcons(const Vec3& origin, const Vec3& dir)
		{
			EditorIconHit hit;
			hit.m_t = -1;
			for (int i = 0, c = m_editor_icons.size(); i < c; ++i)
			{
				float t = m_editor_icons[i]->hit(origin, dir);
				if (t >= 0)
				{
					hit.m_icon = m_editor_icons[i];
					hit.m_t = t;
					return hit;
				}
			}
			return hit;
		}


		virtual void onMouseDown(int x, int y, MouseButton::Value button) override
		{
			if (button == MouseButton::RIGHT)
			{
				m_mouse_mode = MouseMode::NAVIGATE;
			}
			else if (button == MouseButton::LEFT)
			{
				Vec3 origin, dir;
				Component camera_cmp = getComponent(m_camera, CAMERA_HASH);
				if(camera_cmp.isValid())
				{
					RenderScene* scene = static_cast<RenderScene*>(camera_cmp.scene);
					scene->getRay(camera_cmp, (float)x, (float)y, origin, dir);
					RayCastModelHit hit = scene->castRay(origin, dir, Component::INVALID);
					RayCastModelHit gizmo_hit = m_gizmo.castRay(origin, dir);
					EditorIconHit icon_hit = raycastEditorIcons(origin, dir);
					if (gizmo_hit.m_is_hit && (icon_hit.m_t < 0 || gizmo_hit.m_t < icon_hit.m_t))
					{
						if (!m_selected_entities.empty())
						{
							m_mouse_mode = MouseMode::TRANSFORM;
							if (gizmo_hit.m_mesh->getNameHash() == crc32("x_axis"))
							{
								m_gizmo.startTransform(camera_cmp, x, y, Gizmo::TransformMode::X);
							}
							else if (gizmo_hit.m_mesh->getNameHash() == crc32("y_axis"))
							{
								m_gizmo.startTransform(camera_cmp, x, y, Gizmo::TransformMode::Y);
							}
							else
							{
								m_gizmo.startTransform(camera_cmp, x, y, Gizmo::TransformMode::Z);
							}
						}
					}
					else if (icon_hit.m_t >= 0)
					{
						Entity e = icon_hit.m_icon->getEntity();
						if(GetAsyncKeyState(VK_LCONTROL) >> 8)
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


		virtual void addPlugin(Plugin* plugin) override
		{
			m_plugins.push(plugin);
		}


		void onEntityMouseDown(const RayCastModelHit& hit, int x, int y)
		{
			Entity entity = hit.m_component.entity;
			for (int i = 0; i < m_plugins.size(); ++i)
			{
				if (m_plugins[i]->onEntityMouseDown(hit, x, y))
				{
					m_mouse_handling_plugin = m_plugins[i];
					m_mouse_mode = MouseMode::CUSTOM;
					return;
				}
			}
			if(GetAsyncKeyState(VK_LCONTROL) >> 8)
			{
				addEntitiesToSelection(&entity, 1);
			}
			else
			{
				bool entity_already_selected = false;
				for(int i = 0, c = m_selected_entities.size(); i < c; ++i)
				{
					if(m_selected_entities[i] == entity)
					{
						entity_already_selected = true;
						break;
					}
				}
				if(entity_already_selected)
				{
					m_mouse_mode = MouseMode::TRANSFORM;
					m_gizmo.startTransform(getComponent(m_camera, CAMERA_HASH), x, y, Gizmo::TransformMode::CAMERA_XZ);
				}
				else
				{
					selectEntities(&entity, 1);
				}
			}
		}


		virtual void onMouseMove(int x, int y, int relx, int rely, int mouse_flags) override
		{
			m_mouse_x = (float)x;
			m_mouse_y = (float)y;
			switch (m_mouse_mode)
			{
				case MouseMode::CUSTOM:
					{
						if (m_mouse_handling_plugin)
						{
							m_mouse_handling_plugin->onMouseMove(x, y, relx, rely, mouse_flags);
						}
					}
					break;
				case MouseMode::NAVIGATE:
					rotateCamera(relx, rely);
					break;
				case MouseMode::TRANSFORM:
					{
					Gizmo::TransformOperation tmode = mouse_flags & (int)MouseFlags::ALT/*GetKeyState(VK_MENU) & 0x8000*/ ? Gizmo::TransformOperation::ROTATE : Gizmo::TransformOperation::TRANSLATE;
					int flags = mouse_flags & (int)MouseFlags::CONTROL/*GetKeyState(VK_LCONTROL) & 0x8000*/ ? (int)Gizmo::Flags::FIXED_STEP : 0;
					m_gizmo.transform(getComponent(m_camera, CAMERA_HASH), tmode, x, y, relx, rely, flags);
					}
					break;
			}
		}


		virtual void onMouseUp(int x, int y, MouseButton::Value button) override
		{
			if (m_mouse_handling_plugin)
			{
				m_mouse_handling_plugin->onMouseUp(x, y, button);
				m_mouse_handling_plugin = NULL;
			}
			m_mouse_mode = MouseMode::NONE;
		}


		virtual float getMouseX() const override
		{
			return m_mouse_x;
		}


		virtual float getMouseY() const override
		{
			return m_mouse_y;
		}


		virtual void saveUniverse(const Path& path) override
		{
			g_log_info.log("editor") << "saving universe " << path.c_str() << "...";
			FS::FileSystem& fs = m_engine->getFileSystem();
			FS::IFile* file = fs.open(fs.getDefaultDevice(), path, FS::Mode::CREATE | FS::Mode::WRITE);
			save(*file);
			fs.close(file);
			m_universe_path = path;
		}


		void save(FS::IFile& file)
		{
			OutputBlob blob(m_allocator);
			blob.reserve(1 << 20);
			uint32_t hash = 0;
			blob.write(hash);
			blob.write(hash);
			uint32_t engine_hash = m_engine->serialize(blob);
			(*(((uint32_t*)blob.getData()) + 1)) = engine_hash;
			m_template_system->serialize(blob);
			hash = crc32((const uint8_t*)blob.getData() + sizeof(hash), blob.getSize() - sizeof(hash));
			(*(uint32_t*)blob.getData()) = hash;
			g_log_info.log("editor") << "universe saved";
			file.write(blob.getData(), blob.getSize());
		}


		virtual void snapToTerrain() override
		{
			if (!m_selected_entities.empty())
			{
				Array<Vec3> new_positions(m_allocator);
				RenderScene* scene = NULL;
				const Array<IScene*>& scenes = m_engine->getScenes();

				for(int j = 0; j < scenes.size(); ++j)
				{
					if(&scenes[j]->getPlugin() == &m_engine->getRenderer())
					{
						scene = static_cast<RenderScene*>(scenes[j]);
						break;
					}
				}

				for(int i = 0; i < m_selected_entities.size(); ++i)
				{
					const Entity& entity = m_selected_entities[i];
					
					Component renderable = getComponent(m_selected_entities[i], RENDERABLE_HASH);
					RayCastModelHit hit = scene->castRay(entity.getPosition(), Vec3(0, -1, 0), renderable);
					if (hit.m_is_hit)
					{
						new_positions.push(hit.m_origin + hit.m_dir * hit.m_t);
					}
					else
					{
						new_positions.push(m_selected_entities[i].getPosition());
					}
				}
				setEntitiesPositions(m_selected_entities, new_positions);
			}
		}


		virtual void destroyEntities(const Entity* entities, int count) override
		{
			DestroyEntitiesCommand* command = m_allocator.newObject<DestroyEntitiesCommand>(*this, entities, count);
			executeCommand(command);
		}


		virtual Entity addEntity() override
		{
			Component cmp = getComponent(m_camera, CAMERA_HASH);
			RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
			float width = scene->getCameraWidth(cmp);
			float height = scene->getCameraHeight(cmp);
			return addEntityAt((int)width >> 1, (int)height >> 1);
		}


		virtual Entity addEntityAt(int camera_x, int camera_y) override
		{
			Component camera_cmp = getComponent(m_camera, CAMERA_HASH);
			RenderScene* scene = static_cast<RenderScene*>(camera_cmp.scene);
			Vec3 origin;
			Vec3 dir;
			
			scene->getRay(camera_cmp, (float)camera_x, (float)camera_y, origin, dir);
			RayCastModelHit hit = scene->castRay(origin, dir, Component::INVALID);
			Vec3 pos;
			if (hit.m_is_hit)
			{
				pos = hit.m_origin + hit.m_dir * hit.m_t;
			}
			else
			{
				pos = m_camera.getPosition() + m_camera.getRotation() * Vec3(0, 0, -2);
			}
			AddEntityCommand* command = m_allocator.newObject<AddEntityCommand>(*this, pos);
			executeCommand(command);

			return command->getEntity();
		}


		virtual Vec3 getCameraRaycastHit() override
		{
			Component camera_cmp = getComponent(m_camera, CAMERA_HASH);
			RenderScene* scene = static_cast<RenderScene*>(camera_cmp.scene);
			float camera_x = scene->getCameraWidth(camera_cmp);
			float camera_y = scene->getCameraHeight(camera_cmp);
			camera_x *= 0.5f;
			camera_y *= 0.5f;

			Vec3 origin;
			Vec3 dir;
			scene->getRay(camera_cmp, (float)camera_x, (float)camera_y, origin, dir);
			RayCastModelHit hit = scene->castRay(origin, dir, Component::INVALID);
			Vec3 pos;
			if (hit.m_is_hit)
			{
				pos = hit.m_origin + hit.m_dir * hit.m_t;
			}
			else
			{
				pos = m_camera.getPosition() + m_camera.getRotation() * Vec3(0, 0, -2);
			}
			return pos;
		}


		void onEntityCreated(const Entity& entity)
		{
			if (m_camera.isValid())
			{
				EditorIcon* er = m_allocator.newObject<EditorIcon>(*m_engine, *static_cast<RenderScene*>(getComponent(m_camera, CAMERA_HASH).scene), entity);
				m_editor_icons.push(er);
			}
		}


		virtual void setEntitiesPositions(const Array<Entity>& entities, const Array<Vec3>& positions) override
		{
			if (!entities.empty())
			{
				Array<Quat> rots(m_allocator);
				for(int i = 0; i < entities.size(); ++i)
				{
					rots.push(entities[i].getRotation());
				}
				IEditorCommand* command = m_allocator.newObject<MoveEntityCommand>(*this, entities, positions, rots, m_allocator);
				executeCommand(command);
			}
		}


		virtual void setEntityPositionAndRotaion(const Array<Entity>& entities, const Array<Vec3>& positions, const Array<Quat>& rotations) override
		{
			if (!entities.empty())
			{
				IEditorCommand* command = m_allocator.newObject<MoveEntityCommand>(*this, entities, positions, rotations, m_allocator);
				executeCommand(command);
			}
		}


		virtual void setEntityName(const Entity& entity, const char* name) override
		{
			if (entity.isValid())
			{
				IEditorCommand* command = m_allocator.newObject<SetEntityNameCommand>(*this, entity, name);
				executeCommand(command);
			}
		}


		virtual void executeCommand(IEditorCommand* command) override
		{
			static bool b = false;
			ASSERT(!b);
			b = true;
			if (m_undo_index < m_undo_stack.size() - 1)
			{
				for (int i = m_undo_stack.size() - 1; i > m_undo_index; --i)
				{
					m_allocator.deleteObject(m_undo_stack[i]);
				}
				m_undo_stack.resize(m_undo_index + 1);
			}
			if (m_undo_index >= 0 && command->getType() == m_undo_stack[m_undo_index]->getType())
			{
				if (command->merge(*m_undo_stack[m_undo_index]))
				{
					m_undo_stack[m_undo_index]->execute();
					m_allocator.deleteObject(command);
					b = false;
					return;
				}
			}
			m_undo_stack.push(command);
			++m_undo_index;
			command->execute();
			b = false;
		}


		virtual void toggleGameMode() override
		{
			if (m_is_game_mode)
			{
				stopGameMode();
			}
			else
			{
				m_game_mode_file = m_engine->getFileSystem().open("memory", "", FS::Mode::WRITE);
				save(*m_game_mode_file);
				m_is_game_mode = true;
			}
		}


		void stopGameMode()
		{
			selectEntities(NULL, 0);
			for (int i = 0; i < m_editor_icons.size(); ++i)
			{
				m_allocator.deleteObject(m_editor_icons[i]);
			}
			m_editor_icons.clear();
			m_is_game_mode = false;
			m_game_mode_file->seek(FS::SeekMode::BEGIN, 0);
			load(*m_game_mode_file);
			m_engine->getFileSystem().close(m_game_mode_file);
			m_game_mode_file = NULL;
			m_universe_loaded.invoke();
		}


		virtual EntityTemplateSystem& getEntityTemplateSystem() override
		{
			return *m_template_system;
		}


		virtual void showEntities() override
		{
			for (int i = 0, c = m_selected_entities.size(); i < c; ++i)
			{
				Component cmp = getComponent(m_selected_entities[i], RENDERABLE_HASH);
				if (cmp.isValid())
				{
					static_cast<RenderScene*>(cmp.scene)->showRenderable(cmp);
				}
			}
		}


		virtual void hideEntities() override
		{
			for (int i = 0, c = m_selected_entities.size(); i < c; ++i)
			{
				Component cmp = getComponent(m_selected_entities[i], RENDERABLE_HASH);
				if (cmp.isValid())
				{
					static_cast<RenderScene*>(cmp.scene)->hideRenderable(cmp);
				}
			}
		}


		virtual void copyEntity() override
		{
			if(!m_selected_entities.empty())
			{
				Entity entity = m_selected_entities[0];
				m_copy_buffer.clear();
				const WorldEditor::ComponentList& cmps = getComponents(entity);
				int32_t count = cmps.size();
				m_copy_buffer.write(count);
				for(int i = 0; i < count; ++i)
				{
					uint32_t cmp_type = cmps[i].type;
					m_copy_buffer.write(cmp_type);
					Array<IPropertyDescriptor*>& props = getPropertyDescriptors(cmps[i].type);
					int32_t prop_count = props.size(); 
					for(int j = 0; j < prop_count; ++j)
					{
						props[j]->get(cmps[i], m_copy_buffer);
					}
				}
			}
		}
		
		
		virtual void pasteEntity() override
		{
			PasteEntityCommand* command = m_allocator.newObject<PasteEntityCommand>(*this, m_copy_buffer);
			executeCommand(command);
		}


		virtual void cloneComponent(const Component& src, Entity& entity) override
		{
			Component clone = Component::INVALID;

			const Array<IScene*>& scenes = m_engine->getScenes();
			for (int i = 0; i < scenes.size(); ++i)
			{
				clone = scenes[i]->createComponent(src.type, entity);
				if (clone.isValid())
				{
					break;
				}
			}

			const Array<IPropertyDescriptor*>& properties = getPropertyDescriptors(src.type);
			OutputBlob stream(m_allocator);
			for (int i = 0; i < properties.size(); ++i)
			{
				stream.clear();
				properties[i]->get(src, stream);
				InputBlob blob(stream.getData(), stream.getSize());
				properties[i]->set(clone, blob);
			}
		}


		virtual void destroyComponent(const Component& component) override
		{
			if (component.isValid())
			{
				IEditorCommand* command = m_allocator.newObject<DestroyComponentCommand>(*this, component);
				executeCommand(command);
			}
		}


		virtual void addComponent(uint32_t type_crc) override
		{
			if (!m_selected_entities.empty())
			{
				IEditorCommand* command = m_allocator.newObject<AddComponentCommand>(*this, m_selected_entities, type_crc);
				executeCommand(command);
			}
		}


		virtual void lookAtSelected() override
		{
			if (!m_selected_entities.empty())
			{
				m_go_to_parameters.m_is_active = true;
				m_go_to_parameters.m_t = 0;
				m_go_to_parameters.m_from = m_camera.getPosition();
				Matrix camera_mtx = m_camera.getMatrix();
				Vec3 dir = camera_mtx * Vec3(0, 0, 1);
				m_go_to_parameters.m_to = m_selected_entities[0].getPosition() + dir * 10;
				m_go_to_parameters.m_speed = Math::maxValue(100.0f / (m_go_to_parameters.m_to - m_go_to_parameters.m_from).length(), 2.0f);
			}
		}


		virtual void loadUniverse(const Path& path) override
		{
			m_universe_path = path;
			g_log_info.log("editor") << "Loading universe " << path.c_str() << "...";
			FS::FileSystem& fs = m_engine->getFileSystem();
			FS::ReadCallback file_read_cb;
			file_read_cb.bind<WorldEditorImpl, &WorldEditorImpl::loadMap>(this);
			fs.openAsync(fs.getDefaultDevice(), path, FS::Mode::OPEN | FS::Mode::READ, file_read_cb);
		}

		void loadMap(FS::IFile* file, bool success, FS::FileSystem& fs)
		{
			ASSERT(success);
			if (success)
			{
				resetAndLoad(*file);
			}

			fs.close(file);
			m_universe_loaded.invoke();
		}

		virtual void getRelativePath(char* relative_path, int max_length, const Path& source) override
		{
			if (strncmp(m_base_path.c_str(), source.c_str(), m_base_path.length()) == 0)
			{
				strncpy(relative_path, source.c_str() + m_base_path.length(), max_length);
			}
			else
			{
				strncpy(relative_path, source.c_str(), max_length);
			}
		}

		virtual void newUniverse() override
		{
			m_universe_path = "";
			destroyUniverse();
			createUniverse(true);
			g_log_info.log("editor") << "Universe created.";
		}

		void load(FS::IFile& file)
		{
			ASSERT(file.getBuffer());
			m_components.clear();
			m_components.reserve(5000);
			Timer* timer = Timer::create(m_allocator);
			g_log_info.log("editor") << "Parsing universe...";
			InputBlob blob(file.getBuffer(), file.size());
			uint32_t hash = 0;
			blob.read(hash);
			uint32_t engine_hash = 0;
			blob.read(engine_hash);
			if (crc32((const uint8_t*)blob.getData() + sizeof(hash), blob.getSize() - sizeof(hash)) != hash)
			{
				Timer::destroy(timer);
				g_log_error.log("editor") << "Corrupted file.";
				newUniverse();
				return;
			}
			if(m_engine->deserialize(blob))
			{
				m_template_system->deserialize(blob);
				m_camera = static_cast<RenderScene*>(m_engine->getScene(crc32("renderer")))->getCameraInSlot("editor").entity;
				g_log_info.log("editor") << "Universe parsed in " << timer->getTimeSinceStart() << " seconds";

				Universe* universe = m_engine->getUniverse();
				for (int i = 0; i < universe->getEntityCount(); ++i)
				{
					Entity e(universe, i);
					createEditorIcon(e);
				}
			}
			Timer::destroy(timer);
		}


		virtual Array<Component>& getComponents(const Entity& entity) override
		{
			int cmps_index = m_components.find(entity.index);
			if (cmps_index < 0)
			{
				m_components.insert(entity.index, Array<Component>(m_allocator));
				cmps_index = m_components.find(entity.index);
			}
			return m_components.at(cmps_index);
		}

		virtual Component getComponent(const Entity& entity, uint32_t type) override
		{
			const Array<Component>& cmps = getComponents(entity);
			for (int i = 0; i < cmps.size(); ++i)
			{
				if (cmps[i].type == type)
				{
					return cmps[i];
				}
			}
			return Component::INVALID;
		}


		void createEditorIcon(const Entity& entity)
		{
			const WorldEditor::ComponentList& cmps = getComponents(entity);

			bool found_renderable = false;
			for (int i = 0; i < cmps.size(); ++i)
			{
				if (cmps[i].type == RENDERABLE_HASH)
				{
					found_renderable = true;
					break;
				}
			}
			for (int i = 0; i < m_editor_icons.size(); ++i)
			{
				if (m_editor_icons[i]->getEntity() == entity)
				{
					m_allocator.deleteObject(m_editor_icons[i]);
					m_editor_icons.eraseFast(i);
					break;
				}
			}
			if (!found_renderable)
			{
				EditorIcon* er = m_allocator.newObject<EditorIcon>(*m_engine, *static_cast<RenderScene*>(getComponent(m_camera, CAMERA_HASH).scene), entity);
				m_editor_icons.push(er);
			}
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
			return editor.getAllocator().newObject<T>(editor);
		}


		bool create(const char* base_path)
		{
			m_file_system = FS::FileSystem::create(m_allocator);
			m_tpc_file_server.start(base_path, m_allocator);
			m_base_path = base_path;

			m_tcp_file_device.connect("127.0.0.1", 10001, m_allocator);

			m_file_system->mount(&m_mem_file_device);
			m_file_system->mount(&m_disk_file_device);
			m_file_system->mount(&m_tcp_file_device);

			m_file_system->setDefaultDevice("memory:disk");
			m_file_system->setSaveGameDevice("memory:disk");

			m_engine = Engine::create(base_path, m_file_system, this, m_allocator.getSourceAllocator());
			if (!m_engine)
			{
				return false;
			}

			//glPopAttrib();

			if (!m_engine->loadPlugin("animation.dll"))
			{
				g_log_info.log("plugins") << "animation plugin has not been loaded";
			}
			if (!m_engine->loadPlugin("physics.dll"))
			{
				g_log_info.log("plugins") << "physics plugin has not been loaded";
			}
			if (!m_engine->loadPlugin("script.dll"))
			{
				g_log_info.log("plugins") << "script plugin has not been loaded";
			}
			/*if(!m_engine->loadPlugin("navigation.dll"))
			{
			g_log_info.log("plugins", "navigation plugin has not been loaded");
			}*/

			createUniverse(true);
			m_template_system = EntityTemplateSystem::create(*this);

			m_editor_command_creators.insert(crc32("move_entity"), &WorldEditorImpl::constructEditorCommand<MoveEntityCommand>);
			m_editor_command_creators.insert(crc32("set_entity_name"), &WorldEditorImpl::constructEditorCommand<SetEntityNameCommand>);
			m_editor_command_creators.insert(crc32("paste_entity"), &WorldEditorImpl::constructEditorCommand<PasteEntityCommand>);
			m_editor_command_creators.insert(crc32("remove_array_property_item"), &WorldEditorImpl::constructEditorCommand<RemoveArrayPropertyItemCommand>);
			m_editor_command_creators.insert(crc32("add_array_property_item"), &WorldEditorImpl::constructEditorCommand<AddArrayPropertyItemCommand>);
			m_editor_command_creators.insert(crc32("set_property"), &WorldEditorImpl::constructEditorCommand<SetPropertyCommand>);
			m_editor_command_creators.insert(crc32("add_component"), &WorldEditorImpl::constructEditorCommand<AddComponentCommand>);
			m_editor_command_creators.insert(crc32("destroy_entities"), &WorldEditorImpl::constructEditorCommand<DestroyEntitiesCommand>);
			m_editor_command_creators.insert(crc32("destroy_component"), &WorldEditorImpl::constructEditorCommand<DestroyComponentCommand>);
			m_editor_command_creators.insert(crc32("add_entity"), &WorldEditorImpl::constructEditorCommand<AddEntityCommand>);

			return true;
		}


		virtual Gizmo& getGizmo() override
		{
			return m_gizmo;
		}


		virtual FS::TCPFileServer& getTCPFileServer() override
		{
			return m_tpc_file_server;
		}


		virtual Component getEditCamera() override
		{
			return getComponent(m_camera, CAMERA_HASH);
		}


		void destroy()
		{
			m_allocator.deleteObject(m_measure_tool);
			destroyUndoStack();
			for (int j = 0; j < m_component_properties.size(); ++j)
			{
				Array<IPropertyDescriptor*>& props = m_component_properties.at(j);
				for (int i = 0, c = props.size(); i < c; ++i)
				{
					m_allocator.deleteObject(props[i]);
				}
			}
			
			destroyUniverse();
			EntityTemplateSystem::destroy(m_template_system);
			Engine::destroy(m_engine);
			m_engine = NULL;

			m_tcp_file_device.disconnect();
			m_tpc_file_server.stop();
			FS::FileSystem::destroy(m_file_system);
		}


		virtual void setWireframe(bool is_wireframe) override
		{
			m_engine->getRenderer().setEditorWireframe(is_wireframe);
		}


		virtual void renderIcons(IRenderDevice& render_device) override
		{
			PROFILE_FUNCTION();
			for (int i = 0, c = m_editor_icons.size(); i < c; ++i)
			{
				m_editor_icons[i]->render(&m_engine->getRenderer(), render_device);
			}

		}

		virtual void render(IRenderDevice& render_device) override
		{
			PROFILE_FUNCTION();
			m_engine->getRenderer().render(render_device);
		}


		WorldEditorImpl(IAllocator& allocator)
			: m_allocator(allocator)
			, m_engine(NULL)
			, m_universe_mutex(false)
			, m_toggle_game_mode_requested(false)
			, m_gizmo(*this)
			, m_component_properties(m_allocator)
			, m_components(m_allocator)
			, m_mem_file_device(m_allocator)
			, m_disk_file_device(m_allocator)
			, m_entity_name_set(m_allocator)
			, m_entity_selected(m_allocator)
			, m_universe_destroyed(m_allocator)
			, m_universe_created(m_allocator)
			, m_universe_loaded(m_allocator)
			, m_selected_entities(m_allocator)
			, m_editor_icons(m_allocator)
			, m_plugins(m_allocator)
			, m_undo_stack(m_allocator)
			, m_copy_buffer(m_allocator)
			, m_camera(Entity::INVALID)
			, m_editor_command_creators(m_allocator)
		{
			m_go_to_parameters.m_is_active = false;
			m_undo_index = -1;
			m_mouse_handling_plugin = NULL;
			m_is_game_mode = false;
			m_edit_view_render_device = NULL;
			m_universe_path = "";
			m_terrain_brush_size = 10;
			m_terrain_brush_strength = 0.01f;
			m_measure_tool = m_allocator.newObject<MeasureTool>();
			addPlugin(m_measure_tool);
		}


		virtual void navigate(float forward, float right, float speed) override
		{
			Vec3 pos = m_camera.getPosition();
			Quat rot = m_camera.getRotation();;
			pos += rot * Vec3(0, 0, -1) * forward * speed;
			pos += rot * Vec3(1, 0, 0) * right * speed;
			m_camera.setPosition(pos);
		}


		virtual const Array<Entity>& getSelectedEntities() const override
		{
			return m_selected_entities;
		}


		virtual Array<IPropertyDescriptor*>& getPropertyDescriptors(uint32_t type) override
		{
			int props_index = m_component_properties.find(type);
			if (props_index < 0)
			{
				m_component_properties.insert(type, Array<IPropertyDescriptor*>(m_allocator));
				props_index = m_component_properties.find(type);
			}
			return m_component_properties.at(props_index);
		}


		virtual const IPropertyDescriptor& getPropertyDescriptor(uint32_t type, uint32_t name_hash) override
		{
			Array<IPropertyDescriptor*>& props = getPropertyDescriptors(type);
			for (int i = 0; i < props.size(); ++i)
			{
				if (props[i]->getNameHash() == name_hash)
				{
					return *props[i];
				}
			}
			ASSERT(false);
			return *props[0];
		}


		virtual void addArrayPropertyItem(const Component& cmp, IArrayDescriptor& property) override
		{
			if(cmp.isValid())
			{
				IEditorCommand* command = m_allocator.newObject<AddArrayPropertyItemCommand>(*this, cmp, property);
				executeCommand(command);
			}
		}


		virtual void removeArrayPropertyItem(const Component& cmp, int index, IArrayDescriptor& property) override
		{
			if(cmp.isValid())
			{
				IEditorCommand* command = m_allocator.newObject<RemoveArrayPropertyItemCommand>(*this, cmp, index, property);
				executeCommand(command);
			}
		}


		virtual void setProperty(uint32_t component, int index, IPropertyDescriptor& property, const void* data, int size) override
		{
			if (m_selected_entities.size() == 1)
			{
				uint32_t component_hash = component;
				Component cmp = getComponent(m_selected_entities[0], component_hash);
				if (cmp.isValid())
				{
					IEditorCommand* command = m_allocator.newObject<SetPropertyCommand>(*this, cmp, index, property, data, size);
					executeCommand(command);
				}
			}
		}

		void rotateCamera(int x, int y)
		{
			Vec3 pos = m_camera.getPosition();
			Quat rot = m_camera.getRotation();

			Quat yaw_rot(Vec3(0, 1, 0), -x / 200.0f);
			rot = rot * yaw_rot;
			rot.normalize();

			Vec3 axis = rot * Vec3(1, 0, 0);
			Quat pitch_rot(axis, -y / 200.0f);
			rot = rot * pitch_rot;
			rot.normalize();

			Matrix camera_mtx;
			rot.toMatrix(camera_mtx);

			camera_mtx.setTranslation(pos);
			m_camera.setMatrix(camera_mtx);
		}


		void addEntitiesToSelection(const Entity* entities, int count)
		{
			for(int i = 0; i < count; ++i)
			{
				m_selected_entities.push(entities[i]);
			}
			m_entity_selected.invoke(m_selected_entities);
		}

	
		virtual void selectEntities(const Entity* entities, int count) override
		{
			m_selected_entities.clear();
			for(int i = 0; i < count; ++i)
			{
				m_selected_entities.push(entities[i]);
			}
			m_entity_selected.invoke(m_selected_entities);
		}


		virtual void selectEntitiesWithSameMesh() override
		{
			if(m_selected_entities.size() == 1)
			{
				Component cmp = getComponent(m_selected_entities[0], RENDERABLE_HASH);
				if(cmp.isValid())
				{
					Array<Entity> entities(m_allocator);

					RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
					Model* model = scene->getRenderableModel(cmp);
					Component renderable = scene->getFirstRenderable();
					while(renderable.isValid())
					{
						if(model == scene->getRenderableModel(renderable))
						{
							entities.push(renderable.entity);
						}
						renderable = scene->getNextRenderable(renderable);
					}

					selectEntities(&entities[0], entities.size());
				}
			}
		}


		void onComponentAdded(const Component& cmp)
		{
			getComponents(cmp.entity).push(cmp);
		}


		void onComponentCreated(const Component& cmp)
		{
			createEditorIcon(cmp.entity);
		}


		void onComponentDestroyed(const Component& cmp)
		{
			getComponents(cmp.entity).eraseItemFast(cmp);
			for (int i = 0; i < m_editor_icons.size(); ++i)
			{
				if (m_editor_icons[i]->getEntity() == cmp.entity)
				{
					m_allocator.deleteObject(m_editor_icons[i]);
					m_editor_icons.eraseFast(i);
					break;
				}
			}
			if (cmp.entity.existsInUniverse() && getComponents(cmp.entity).empty())
			{
				EditorIcon* er = m_allocator.newObject<EditorIcon>(*m_engine, *static_cast<RenderScene*>(getComponent(m_camera, CAMERA_HASH).scene), cmp.entity);
				m_editor_icons.push(er);
			}
		}


		void onEntityDestroyed(const Entity& entity)
		{
			m_selected_entities.eraseItemFast(entity);
			for (int i = 0; i < m_editor_icons.size(); ++i)
			{
				if (m_editor_icons[i]->getEntity() == entity)
				{
					m_allocator.deleteObject(m_editor_icons[i]);
					m_editor_icons.eraseFast(i);
					break;
				}
			}
		}


		void destroyUniverse()
		{
			destroyUndoStack();
			m_universe_destroyed.invoke();
			m_gizmo.setUniverse(NULL);
			m_gizmo.destroy();
			for (int i = 0; i < m_editor_icons.size(); ++i)
			{
				m_allocator.deleteObject(m_editor_icons[i]);
			}
			m_components.clear();
			selectEntities(NULL, 0);
			m_camera = Entity::INVALID;
			m_editor_icons.clear();
			m_engine->destroyUniverse();
		}


		virtual void setEditViewRenderDevice(IRenderDevice& render_device) override
		{
			m_edit_view_render_device = &render_device;
		}


		virtual Path getUniversePath() const override
		{
			return m_universe_path;
		}
	

		virtual DelegateList<void()>& universeCreated() override
		{
			return m_universe_created;
		}


		virtual DelegateList<void(const Array<Entity>&)>& entitySelected() override
		{
			return m_entity_selected;
		}


		virtual DelegateList<void()>& universeDestroyed() override
		{
			return m_universe_destroyed;
		}


		virtual DelegateList<void()>& universeLoaded() override
		{
			return m_universe_loaded;
		}


		virtual DelegateList<void(const Entity&, const char*)>& entityNameSet() override
		{
			return m_entity_name_set;
		}


		void destroyUndoStack()
		{
			m_undo_index = -1;
			for (int i = 0; i < m_undo_stack.size(); ++i)
			{
				m_allocator.deleteObject(m_undo_stack[i]);
			}
			m_undo_stack.clear();
		}


		Component createComponent(uint32_t hash, const Entity& entity)
		{
			const Array<IScene*>& scenes = m_engine->getScenes();
			Component cmp;
			for (int i = 0; i < scenes.size(); ++i)
			{
				cmp = scenes[i]->createComponent(hash, entity);
				if (cmp.isValid())
				{
					return cmp;
				}
			}
			return Component::INVALID;
		}


		void createUniverse(bool create_basic_entities)
		{
			destroyUndoStack();
			Universe* universe = m_engine->createUniverse();
			m_gizmo.create(m_engine->getRenderer());
			m_gizmo.setUniverse(universe);

			universe->entityCreated().bind<WorldEditorImpl, &WorldEditorImpl::onEntityCreated>(this);
			universe->componentCreated().bind<WorldEditorImpl, &WorldEditorImpl::onComponentCreated>(this);
			universe->componentDestroyed().bind<WorldEditorImpl, &WorldEditorImpl::onComponentDestroyed>(this);
			universe->componentAdded().bind<WorldEditorImpl, &WorldEditorImpl::onComponentAdded>(this);
			universe->entityDestroyed().bind<WorldEditorImpl, &WorldEditorImpl::onEntityDestroyed>(this);

			m_selected_entities.clear();
			m_universe_created.invoke();

			if (create_basic_entities)
			{
				m_camera = m_engine->getUniverse()->createEntity();
				m_camera.setName("editor_camera");
				m_camera.setPosition(0, 0, -5);
				m_camera.setRotation(Quat(Vec3(0, 1, 0), -Math::PI));
				Component cmp = createComponent(CAMERA_HASH, m_camera);
				ASSERT(cmp.isValid());
				RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
				scene->setCameraSlot(cmp, string("editor", m_allocator));
			}
			RenderScene* scene = static_cast<RenderScene*>(m_engine->getScene(crc32("renderer")));
			m_fps_text = scene->addDebugText("FPS: 0", 0, 0);
		}


		virtual void undo() override
		{
			if (m_undo_index < m_undo_stack.size() && m_undo_index >= 0)
			{
				m_undo_stack[m_undo_index]->undo();
				--m_undo_index;
			}
			if(!m_selected_entities.empty())
			{
				selectEntities(&m_selected_entities[0], m_selected_entities.size());
			}
		}


		virtual void redo() override
		{
			if (m_undo_index + 1 < m_undo_stack.size())
			{
				++m_undo_index;
				m_undo_stack[m_undo_index]->execute();
			}
			if(!m_selected_entities.empty())
			{
				selectEntities(&m_selected_entities[0], m_selected_entities.size());
			}
		}


		virtual MeasureTool* getMeasureTool() const override
		{
			return m_measure_tool;
		}


		virtual void toggleMeasure() override
		{
			m_measure_tool->enable(!m_measure_tool->isEnabled());
		}


		virtual void saveUndoStack(const Path& path) override
		{
			if (m_undo_stack.empty())
			{
				return;
			}
			FS::IFile* file = m_engine->getFileSystem().open("disk", path.c_str(), FS::Mode::CREATE | FS::Mode::WRITE);
			if (file)
			{
				JsonSerializer serializer(*file, JsonSerializer::WRITE, path.c_str(), m_allocator);
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
				m_engine->getFileSystem().close(file);
			}
			else
			{
				g_log_error.log("editor") << "Could not save commands to " << path.c_str();
			}
		}


		IEditorCommand* createEditorCommand(uint32_t command_type)
		{
			int index = m_editor_command_creators.find(command_type);
			if (index >= 0)
			{
				return m_editor_command_creators.at(index)(*this);
			}
			return NULL;
		}


		virtual void registerEditorCommandCreator(const char* command_type, EditorCommandCreator creator) override
		{
			m_editor_command_creators.insert(crc32(command_type), creator);
		}


		virtual bool executeUndoStack(const Path& path) override
		{
			destroyUndoStack();
			m_undo_index = -1;
			FS::IFile* file = m_engine->getFileSystem().open("disk", path.c_str(), FS::Mode::OPEN | FS::Mode::READ);
			if (file)
			{
				JsonSerializer serializer(*file, JsonSerializer::READ, path.c_str(), m_allocator);
				serializer.deserializeObjectBegin();
				serializer.deserializeArrayBegin("commands");
				while (!serializer.isArrayEnd())
				{
					serializer.nextArrayItem();
					serializer.deserializeObjectBegin();
					uint32_t type;
					serializer.deserialize("undo_command_type", type, 0);
					IEditorCommand* command = createEditorCommand(type);
					if (!command)
					{
						g_log_error.log("editor") << "Unknown command " << type << " in " << path.c_str();
						destroyUndoStack();
						m_undo_index = -1;
						return false;
					}
					command->deserialize(serializer);
					executeCommand(command);
					serializer.deserializeObjectEnd();
				}
				serializer.deserializeArrayEnd();
				serializer.deserializeObjectEnd();
				m_engine->getFileSystem().close(file);
			}
			return file != NULL;
		}


		virtual bool runTest(const Path& undo_stack_path, const Path& result_universe_path) override
		{
			newUniverse();
			executeUndoStack(undo_stack_path);
			FS::IFile* file = m_engine->getFileSystem().open("memory", "", FS::Mode::CREATE | FS::Mode::WRITE);
			if (!file)
			{
				return false;
			}
			FS::IFile* result_file = m_engine->getFileSystem().open("memory:disk", result_universe_path.c_str(), FS::Mode::OPEN | FS::Mode::READ);
			if (!result_file)
			{
				return false;
			}
			save(*file);
			bool is_same = file->size() > 8 && result_file->size() > 8 && *((const uint32_t*)result_file->getBuffer() + 1) == *((const uint32_t*)file->getBuffer() + 1);
			m_engine->getFileSystem().close(result_file);
			m_engine->getFileSystem().close(file);

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
				TRANSFORM,
				CUSTOM
			};
		};

		struct GoToParameters
		{
			bool m_is_active;
			Vec3 m_from;
			Vec3 m_to;
			float m_t;
			float m_speed;
		};

		Debug::Allocator m_allocator;
		GoToParameters m_go_to_parameters;
		MT::Mutex m_universe_mutex;
		Gizmo m_gizmo;
		Array<Entity> m_selected_entities;
		AssociativeArray<uint32_t, Array<IPropertyDescriptor*> > m_component_properties;
		MouseMode::Value m_mouse_mode;
		float m_mouse_x;
		float m_mouse_y;
		Array<EditorIcon*> m_editor_icons;
		AssociativeArray<int32_t, Array<Component> > m_components;
		bool m_is_game_mode;
		FS::IFile* m_game_mode_file;
		Engine* m_engine;
		Entity m_camera;
		int m_fps_text;
		DelegateList<void()> m_universe_destroyed;
		DelegateList<void()> m_universe_created;
		DelegateList<void()> m_universe_loaded;
		DelegateList<void(const Array<Entity>&)> m_entity_selected;
		DelegateList<void(const Entity&, const char*)> m_entity_name_set;

		FS::FileSystem* m_file_system;
		FS::TCPFileServer m_tpc_file_server;
		FS::DiskFileDevice m_disk_file_device;
		FS::MemoryFileDevice m_mem_file_device;
		FS::TCPFileDevice m_tcp_file_device;
		IRenderDevice* m_edit_view_render_device;
		bool m_toggle_game_mode_requested;
		Path m_universe_path;
		Path m_base_path;
		int m_terrain_brush_size;
		float m_terrain_brush_strength;
		Array<Plugin*> m_plugins;
		MeasureTool* m_measure_tool;
		Plugin* m_mouse_handling_plugin;
		EntityTemplateSystem* m_template_system;
		Array<IEditorCommand*> m_undo_stack;
		AssociativeArray<uint32_t, EditorCommandCreator> m_editor_command_creators;
		int m_undo_index;
		OutputBlob m_copy_buffer;
};


WorldEditor* WorldEditor::create(const char* base_path, IAllocator& allocator)
{
	WorldEditorImpl* impl = allocator.newObject<WorldEditorImpl>(allocator);

	if (!impl->create(base_path))
	{
		allocator.deleteObject(impl);
		return NULL;
	}

	return impl;
}


void WorldEditor::destroy(WorldEditor* editor)
{
	static_cast<WorldEditorImpl*>(editor)->destroy();
	IAllocator& allocator = editor->getAllocator();
	static_cast<BaseProxyAllocator&>(allocator).getSourceAllocator().deleteObject(static_cast<WorldEditorImpl*>(editor));
}


void PasteEntityCommand::execute()
{
	InputBlob blob(m_blob.getData(), m_blob.getSize());
	Entity new_entity = m_editor.getEngine().getUniverse()->createEntity();
	new_entity.setPosition(m_position);
	int32_t count;
	blob.read(count);
	for(int i = 0; i < count; ++i)
	{
		uint32_t type;
		blob.read(type);
		Component cmp = static_cast<WorldEditorImpl&>(m_editor).createComponent(type, new_entity);
		Array<IPropertyDescriptor*>& props = m_editor.getPropertyDescriptors(type);
		for(int j = 0; j < props.size(); ++j)
		{
			props[j]->set(cmp, blob);
		}
	}
	m_entity = new_entity;
}



} // !namespace Lumix
