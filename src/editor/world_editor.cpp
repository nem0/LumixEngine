#include "world_editor.h"

#include "animation/animation_system.h"
#include "core/array.h"
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
#include "core/map.h"
#include "core/matrix.h"
#include "core/mt/mutex.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/editor_icon.h"
#include "editor/entity_template_system.h"
#include "editor/gizmo.h"
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


class MoveEntityCommand : public IEditorCommand
{
	public:
		MoveEntityCommand(Entity entity, const Vec3& new_position, const Quat& new_rotation)
			: m_entity(entity)
			, m_old_position(entity.getPosition())
			, m_old_rotation(entity.getRotation())
			, m_new_position(new_position)
			, m_new_rotation(new_rotation)
		{}


		virtual void execute() override
		{
			m_entity.setPosition(m_new_position);
			m_entity.setRotation(m_new_rotation);
		}


		virtual void undo() override
		{
			m_entity.setPosition(m_old_position);
			m_entity.setRotation(m_old_rotation);
		}


		virtual uint32_t getType() override
		{
			static const uint32_t type = crc32("move_entity");
			return type;
		}


		virtual bool merge(IEditorCommand& command)
		{
			ASSERT(command.getType() == getType());
			if (static_cast<MoveEntityCommand&>(command).m_entity == m_entity)
			{
				static_cast<MoveEntityCommand&>(command).m_new_position = m_new_position;
				static_cast<MoveEntityCommand&>(command).m_new_rotation = m_new_rotation;
				return true;
			}
			else
			{
				return false;
			}
		}

	private:
		Entity m_entity;
		Vec3 m_old_position;
		Quat m_old_rotation;
		Vec3 m_new_position;
		Quat m_new_rotation;
};


class SetPropertyCommand : public IEditorCommand
{
	public:
		SetPropertyCommand(WorldEditor& editor, const Component& component, const IPropertyDescriptor& property_descriptor, const void* data, int size)
			: m_component(component)
			, m_property_descriptor(property_descriptor)
			, m_editor(editor)
		{
			m_new_value.write(data, size);
			m_property_descriptor.get(component, m_old_value);
		}


		virtual void execute() override
		{
			m_new_value.rewindForRead();
			set(m_new_value);
		}


		virtual void undo() override
		{
			m_old_value.rewindForRead();
			set(m_old_value);
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
			if (m_component == src.m_component && &src.m_property_descriptor == &m_property_descriptor)
			{
				src.m_new_value = m_new_value;
				return true;
			}
			return false;
		}


		void set(Blob& stream)
		{
			uint32_t template_hash = m_editor.getEntityTemplateSystem().getTemplate(m_component.entity);
			if (template_hash)
			{
				const Array<Entity>& entities = m_editor.getEntityTemplateSystem().getInstances(template_hash);
				for (int i = 0, c = entities.size(); i < c; ++i)
				{
					stream.rewindForRead();
					const Entity::ComponentList& cmps = entities[i].getComponents();
					for (int j = 0, cj = cmps.size(); j < cj; ++j)
					{
						if (cmps[j].type == m_component.type)
						{
							m_property_descriptor.set(cmps[j], stream);
							break;
						}
					}
				}
			}
			else
			{
				m_property_descriptor.set(m_component, stream);
			}
		}


	private:
		WorldEditor& m_editor;
		Component m_component;
		Blob m_new_value;
		Blob m_old_value;
		const IPropertyDescriptor& m_property_descriptor;
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
				AddComponentCommand(WorldEditorImpl& editor, const Array<Entity>& entities, uint32_t type)
					: m_editor(editor)
				{
					m_type = type;
					m_entities.reserve(entities.size());
					for (int i = 0; i < entities.size(); ++i)
					{
						m_entities.push(entities[i]);
					}
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
						const Component& cmp = m_entities[i].getComponent(m_type);
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
				DestroyEntitiesCommand(WorldEditorImpl& editor, const Entity* entities, int count)
					: m_editor(editor)
				{
					m_entities.reserve(count);
					m_positons_rotations.reserve(m_entities.size());
					for (int i = 0; i < count; ++i)
					{
						m_entities.push(entities[i]);
					}
				}


				virtual void execute() override
				{
					m_positons_rotations.clear();
					m_old_values.clearBuffer();
					for (int i = 0; i < m_entities.size(); ++i)
					{
						const Entity::ComponentList& cmps = m_entities[i].getComponents();
						PositionRotation pos_rot;
						pos_rot.m_position = m_entities[i].getPosition();
						pos_rot.m_rotation = m_entities[i].getRotation();
						m_positons_rotations.push(pos_rot);
						m_old_values.write((int)cmps.size());
						for (int j = cmps.size() - 1; j >= 0; --j)
						{
							m_old_values.write(cmps[j].type);
							Array<IPropertyDescriptor*>& props = m_editor.m_component_properties[cmps[j].type];
							for (int k = 0; k < props.size(); ++k)
							{
								props[k]->get(cmps[j], m_old_values);
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
					m_old_values.rewindForRead();
					for (int i = 0; i < m_entities.size(); ++i)
					{
						Entity new_entity = m_editor.getEngine().getUniverse()->createEntity();
						new_entity.setPosition(m_positons_rotations[i].m_position);
						new_entity.setRotation(m_positons_rotations[i].m_rotation);
						int cmps_count;
						m_old_values.read(cmps_count);
						for (int j = cmps_count - 1; j >= 0; --j)
						{
							Component::Type cmp_type;
							m_old_values.read(cmp_type);
							Array<IPropertyDescriptor*>& props = m_editor.m_component_properties[cmp_type];
							Component new_component;
							for (int i = 0; i < scenes.size(); ++i)
							{
								new_component = scenes[i]->createComponent(cmp_type, new_entity);
								if (new_component.isValid())
								{
									break;
								}
							}
							for (int k = 0; k < props.size(); ++k)
							{
								props[k]->set(new_component, m_old_values);
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
				Blob m_old_values;
		};


		class DestroyComponentCommand : public IEditorCommand
		{
			public:
				DestroyComponentCommand(WorldEditorImpl& editor, const Component& component)
					: m_component(component)
					, m_editor(editor)
				{
				}


				virtual void undo() override
				{
					uint32_t template_hash = m_editor.m_template_system->getTemplate(m_component.entity);
					const Array<IPropertyDescriptor*>& props = m_editor.m_component_properties[m_component.type];
					const Array<IScene*>& scenes = m_editor.m_engine->getScenes();
					if (template_hash == 0)
					{
						for (int i = 0; i < scenes.size(); ++i)
						{
							if (scenes[i]->createComponent(m_component.type, m_component.entity).isValid())
							{
								break;
							}
						}
						m_old_values.rewindForRead();
						for (int i = 0; i < props.size(); ++i)
						{
							props[i]->set(m_component, m_old_values);
						}
					}
					else
					{
						const Array<Entity>& entities = m_editor.m_template_system->getInstances(template_hash);
						for (int i = 0, c = entities.size(); i < c; ++i)
						{
							for (int i = 0; i < scenes.size(); ++i)
							{
								Component cmp_new = scenes[i]->createComponent(m_component.type, m_component.entity);
								if (cmp_new.isValid())
								{
									m_old_values.rewindForRead();
									for (int i = 0; i < props.size(); ++i)
									{
										props[i]->set(cmp_new, m_old_values);
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
					Array<IPropertyDescriptor*>& props = m_editor.m_component_properties[m_component.type];
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
							const Entity::ComponentList& cmps = instances[i].getComponents();
							for (int j = 0; j < cmps.size(); ++j)
							{
								cmps[j].scene->destroyComponent(cmps[j]);
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
				Blob m_old_values;
		};


		class AddEntityCommand : public IEditorCommand
		{
			public:
				AddEntityCommand(WorldEditorImpl& editor, const Vec3& position)
					: m_editor(editor)
				{
					m_position = position;
				}


				virtual void execute() override
				{
					m_entity = m_editor.getEngine().getUniverse()->createEntity();
					m_entity.setPosition(m_position);
					m_editor.selectEntity(m_entity);
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


		virtual Engine& getEngine() override
		{
			return *m_engine;
		}


		virtual void registerProperty(const char* component_type, IPropertyDescriptor* descriptor) override
		{
			ASSERT(descriptor);
			m_component_properties[crc32(component_type)].push(descriptor);
		}


		virtual void tick() override
		{
			for (int i = 0; i < m_plugins.size(); ++i)
			{
				m_plugins[i]->tick();
			}
			if (m_toggle_game_mode_requested)
			{
				toggleGameMode();
				m_toggle_game_mode_requested = false;
			}
			PROFILE_FUNCTION();
			m_engine->update(m_is_game_mode);
			m_engine->getFileSystem().updateAsyncTransactions();
		}

	
		virtual ~WorldEditorImpl()
		{
			destroyUndoStack();
			auto iter = m_component_properties.begin();
			auto end = m_component_properties.end();
			while (iter != end)
			{
				for (int i = 0, c = iter.second().size(); i < c; ++i)
				{
					LUMIX_DELETE(iter.second()[i]);
				}
				++iter;
			}
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
				Component camera_cmp = m_camera.getComponent(CAMERA_HASH);
				RenderScene* scene = static_cast<RenderScene*>(camera_cmp.scene);
				scene->getRay(camera_cmp, (float)x, (float)y, origin, dir);
				RayCastModelHit hit = scene->castRay(origin, dir, Component::INVALID);
				RayCastModelHit gizmo_hit = m_gizmo.castRay(origin, dir);
				EditorIconHit icon_hit = raycastEditorIcons(origin, dir);
				if (gizmo_hit.m_is_hit && (icon_hit.m_t < 0 || gizmo_hit.m_t < icon_hit.m_t))
				{
					if (m_selected_entity.isValid())
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
					selectEntity(icon_hit.m_icon->getEntity());
				}
				else if (hit.m_is_hit)
				{
					onEntityMouseDown(hit, x, y);
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
			selectEntity(entity);
			m_mouse_mode = MouseMode::TRANSFORM;
			m_gizmo.startTransform(m_camera.getComponent(CAMERA_HASH), x, y, Gizmo::TransformMode::CAMERA_XZ);
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
					int flags = mouse_flags & (int)MouseFlags::CONTROL/*GetKeyState(VK_LCONTROL) & 0x8000*/ ? Gizmo::Flags::FIXED_STEP : 0;
					m_gizmo.transform(m_camera.getComponent(CAMERA_HASH), tmode, x, y, relx, rely, flags);
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
			FS::IFile* file = fs.open(fs.getDefaultDevice(), path, FS::Mode::OPEN_OR_CREATE | FS::Mode::WRITE);
			save(*file, path.c_str());
			fs.close(file);
			m_universe_path = path;
		}


		void save(FS::IFile& file, const char* path)
		{
			JsonSerializer serializer(file, JsonSerializer::WRITE, path);
			m_engine->serialize(serializer);
			m_template_system->serialize(serializer);
			g_log_info.log("editor") << "universe saved";
		}


		virtual void snapToTerrain() override
		{
			if (m_selected_entity.isValid())
			{
				Component renderable = m_selected_entity.getComponent(RENDERABLE_HASH);
				RenderScene* scene = static_cast<RenderScene*>(renderable.scene);
				RayCastModelHit hit = scene->castRay(m_selected_entity.getPosition(), Vec3(0, -1, 0), renderable);
				if (hit.m_is_hit)
				{
					setEntityPosition(m_selected_entity, hit.m_origin + hit.m_dir * hit.m_t);
				}
			}
		}


		virtual void destroyEntities(const Entity* entities, int count) override
		{
			DestroyEntitiesCommand* command = LUMIX_NEW(DestroyEntitiesCommand)(*this, entities, count);
			executeCommand(command);
		}


		virtual Entity addEntity() override
		{
			Component cmp = m_camera.getComponent(CAMERA_HASH);
			RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
			float width;
			float height;
			scene->getCameraWidth(cmp, width);
			scene->getCameraHeight(cmp, height);
			return addEntityAt((int)width >> 1, (int)height >> 1);
		}


		virtual Entity addEntityAt(int camera_x, int camera_y) override
		{
			Component camera_cmp = m_camera.getComponent(CAMERA_HASH);
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
			AddEntityCommand* command = LUMIX_NEW(AddEntityCommand)(*this, pos);
			executeCommand(command);

			return command->getEntity();
		}


		virtual Vec3 getCameraRaycastHit() override
		{
			Component camera_cmp = m_camera.getComponent(CAMERA_HASH);
			RenderScene* scene = static_cast<RenderScene*>(camera_cmp.scene);
			float camera_x;
			float camera_y;
			scene->getCameraWidth(camera_cmp, camera_x);
			scene->getCameraHeight(camera_cmp, camera_y);
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


		void onEntityCreated(Entity& entity)
		{
			EditorIcon* er = LUMIX_NEW(EditorIcon)();
			er->create(*m_engine, *static_cast<RenderScene*>(m_camera.getComponent(CAMERA_HASH).scene), entity);
			m_editor_icons.push(er);
		}


		virtual void setEntityPosition(const Entity& entity, const Vec3& position) override
		{
			if (entity.isValid())
			{
				IEditorCommand* command = LUMIX_NEW(MoveEntityCommand)(entity, position, entity.getRotation());
				executeCommand(command);
			}
		}


		virtual void setEntityPositionAndRotaion(const Entity& entity, const Vec3& position, const Quat& rotation) override
		{
			if (entity.isValid())
			{
				IEditorCommand* command = LUMIX_NEW(MoveEntityCommand)(entity, position, rotation);
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
					LUMIX_DELETE(m_undo_stack[i]);
				}
				m_undo_stack.resize(m_undo_index + 1);
			}
			if (m_undo_index >= 0 && command->getType() == m_undo_stack[m_undo_index]->getType())
			{
				if (command->merge(*m_undo_stack[m_undo_index]))
				{
					m_undo_stack[m_undo_index]->execute();
					LUMIX_DELETE(command);
					b = false;
					return;
				}
			}
			m_undo_stack.push(command);
			++m_undo_index;
			command->execute();
			selectEntity(m_selected_entity);
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
				save(*m_game_mode_file, "GameMode");
				m_is_game_mode = true;
			}
		}


		void stopGameMode()
		{
			m_is_game_mode = false;
			m_game_mode_file->seek(FS::SeekMode::BEGIN, 0);
			load(*m_game_mode_file, "GameMode");
			m_engine->getFileSystem().close(m_game_mode_file);
			m_game_mode_file = NULL;
		}


		virtual EntityTemplateSystem& getEntityTemplateSystem() override
		{
			return *m_template_system;
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

			const Array<IPropertyDescriptor*>& properties = m_component_properties[src.type];
			Blob stream;
			for (int i = 0; i < properties.size(); ++i)
			{
				stream.clearBuffer();
				properties[i]->get(src, stream);
				stream.rewindForRead();
				properties[i]->set(clone, stream);
			}
		}


		virtual void destroyComponent(const Component& component) override
		{
			if (component.isValid())
			{
				IEditorCommand* command = LUMIX_NEW(DestroyComponentCommand)(*this, component);
				executeCommand(command);
			}
		}


		virtual void addComponent(uint32_t type_crc) override
		{
			if (m_selected_entity.isValid())
			{
				const Entity::ComponentList& cmps = m_selected_entity.getComponents();
				for (int i = 0; i < cmps.size(); ++i)
				{
					if (cmps[i].type == type_crc)
					{
						return;
					}
				}
				
				uint32_t template_hash = m_template_system->getTemplate(m_selected_entity);
				if (template_hash == 0)
				{
					Array<Entity> entities;
					entities.push(m_selected_entity);
					IEditorCommand* command = LUMIX_NEW(AddComponentCommand)(*this, entities, type_crc);
					executeCommand(command);
				}
				else
				{
					const Array<Entity>& entities = m_template_system->getInstances(template_hash);
					IEditorCommand* command = LUMIX_NEW(AddComponentCommand)(*this, entities, type_crc);
					executeCommand(command);
				}
			}
		}


		virtual void lookAtSelected() override
		{
			if (m_selected_entity.isValid())
			{
				Matrix camera_mtx = m_camera.getMatrix();
				Vec3 dir = camera_mtx * Vec3(0, 0, 1);
				camera_mtx.setTranslation(m_selected_entity.getPosition() + dir * 10);
				m_camera.setMatrix(camera_mtx);
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
				resetAndLoad(*file, "unknown map"); /// TODO file path
			}

			fs.close(file);
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
			g_log_info.log("editor") << "universe created";
		}

		void load(FS::IFile& file, const char* path)
		{
			g_log_info.log("editor") << "parsing universe...";
			JsonSerializer serializer(file, JsonSerializer::READ, path);
			m_engine->deserialize(serializer);
			m_template_system->deserialize(serializer);
			m_camera = static_cast<RenderScene*>(m_engine->getScene(crc32("renderer")))->getCameraInSlot("editor").entity;
			g_log_info.log("editor") << "universe parsed";

			Universe* universe = m_engine->getUniverse();
			for (int i = 0; i < universe->getEntityCount(); ++i)
			{
				Entity e(universe, i);
				createEditorIcon(e);
			}
		}


		void createEditorIcon(const Entity& entity)
		{
			const Entity::ComponentList& cmps = entity.getComponents();

			bool found_renderable = false;
			for (int i = 0; i < cmps.size(); ++i)
			{
				if (cmps[i].type == RENDERABLE_HASH)
				{
					found_renderable = true;
					break;
				}
			}
			if (!found_renderable)
			{
				EditorIcon* er = LUMIX_NEW(EditorIcon)();
				er->create(*m_engine, *static_cast<RenderScene*>(m_camera.getComponent(CAMERA_HASH).scene), entity);
				m_editor_icons.push(er);
			}
		}

		void resetAndLoad(FS::IFile& file, const char* path)
		{
			destroyUniverse();
			createUniverse(false);
			load(file, path);
		}


		bool create(const char* base_path)
		{
			m_file_system = FS::FileSystem::create();
			m_tpc_file_server.start(base_path);
			m_base_path = base_path;

			m_tcp_file_device.connect("127.0.0.1", 10001);

			m_file_system->mount(&m_mem_file_device);
			m_file_system->mount(&m_disk_file_device);
			m_file_system->mount(&m_tcp_file_device);

			m_file_system->setDefaultDevice("memory:disk");
			m_file_system->setSaveGameDevice("memory:disk");

			m_engine = Engine::create(base_path, m_file_system, this);
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


		virtual Component getEditCamera() const override
		{
			return m_camera.getComponent(CAMERA_HASH);
		}


		void destroy()
		{
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


		WorldEditorImpl()
			: m_engine(NULL)
			, m_universe_mutex(false)
			, m_toggle_game_mode_requested(false)
			, m_gizmo(*this)
		{
			m_undo_index = -1;
			m_mouse_handling_plugin = NULL;
			m_is_game_mode = false;
			m_selected_entity = Entity::INVALID;
			m_edit_view_render_device = NULL;
			m_universe_path = "";
			m_terrain_brush_size = 10;
			m_terrain_brush_strength = 0.01f;
		}


		virtual void navigate(float forward, float right, float speed) override
		{
			Vec3 pos = m_camera.getPosition();
			Quat rot = m_camera.getRotation();;
			pos += rot * Vec3(0, 0, -1) * forward * speed;
			pos += rot * Vec3(1, 0, 0) * right * speed;
			m_camera.setPosition(pos);
		}


		virtual Entity getSelectedEntity() const override
		{
			return m_selected_entity;
		}


		virtual const IPropertyDescriptor& getPropertyDescriptor(uint32_t type, uint32_t name_hash) override
		{
			Array<IPropertyDescriptor*>& props = m_component_properties[type];
			for (int i = 0; i < props.size(); ++i)
			{
				if (props[i]->getNameHash() == name_hash)
				{
					return *props[i];
				}
			}
			ASSERT(false);
			return *m_component_properties[type][0];
		}


		virtual void setProperty(const char* component, const char* property, const void* data, int size) override
		{
			if (m_selected_entity.isValid())
			{
				uint32_t component_hash = crc32(component);
				Component cmp = m_selected_entity.getComponent(component_hash);
				if (cmp.isValid())
				{
					IEditorCommand* command = LUMIX_NEW(SetPropertyCommand)(*this, cmp, getPropertyDescriptor(component_hash, crc32(property)), data, size);
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

	
		virtual void selectEntity(Entity e) override
		{
			m_selected_entity = e;
			m_gizmo.setEntity(e);
			m_entity_selected.invoke(e);
		}


		void onComponentCreated(Component& cmp)
		{
			for (int i = 0; i < m_editor_icons.size(); ++i)
			{
				if (m_editor_icons[i]->getEntity() == cmp.entity)
				{
					m_editor_icons[i]->destroy();
					LUMIX_DELETE(m_editor_icons[i]);
					m_editor_icons.eraseFast(i);
					break;
				}
			}
			createEditorIcon(cmp.entity);
		}


		void onComponentDestroyed(const Component& cmp)
		{
			for (int i = 0; i < m_editor_icons.size(); ++i)
			{
				if (m_editor_icons[i]->getEntity() == cmp.entity)
				{
					m_editor_icons[i]->destroy();
					LUMIX_DELETE(m_editor_icons[i]);
					m_editor_icons.eraseFast(i);
					break;
				}
			}
			if (cmp.entity.existsInUniverse() && cmp.entity.getComponents().empty())
			{
				EditorIcon* er = LUMIX_NEW(EditorIcon)();
				er->create(*m_engine, *static_cast<RenderScene*>(m_camera.getComponent(CAMERA_HASH).scene), cmp.entity);
				m_editor_icons.push(er);
			}
		}


		void onEntityDestroyed(Entity& entity)
		{
			for (int i = 0; i < m_editor_icons.size(); ++i)
			{
				if (m_editor_icons[i]->getEntity() == entity)
				{
					m_editor_icons[i]->destroy();
					LUMIX_DELETE(m_editor_icons[i]);
					m_editor_icons.eraseFast(i);
					break;
				}
			}
		}


		void destroyUniverse()
		{
			destroyUndoStack();
			m_universe_destroyed.invoke();
			for (int i = 0; i < m_editor_icons.size(); ++i)
			{
				m_editor_icons[i]->destroy();
				LUMIX_DELETE(m_editor_icons[i]);
			}
			selectEntity(Entity::INVALID);
			m_camera = Entity::INVALID;
			m_editor_icons.clear();
			m_gizmo.setUniverse(NULL);
			m_gizmo.destroy();
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


		virtual DelegateList<void(Entity&)>& entitySelected() override
		{
			return m_entity_selected;
		}


		virtual DelegateList<void()>& universeDestroyed() override
		{
			return m_universe_destroyed;
		}


		void destroyUndoStack()
		{
			m_undo_index = -1;
			for (int i = 0; i < m_undo_stack.size(); ++i)
			{
				LUMIX_DELETE(m_undo_stack[i]);
			}
			m_undo_stack.clear();
		}


		void createUniverse(bool create_basic_entities)
		{
			destroyUndoStack();
			Universe* universe = m_engine->createUniverse();
			if (create_basic_entities)
			{
				m_camera = m_engine->getUniverse()->createEntity();
				m_camera.setPosition(0, 0, -5);
				m_camera.setRotation(Quat(Vec3(0, 1, 0), -Math::PI));
				const Array<IScene*>& scenes = m_engine->getScenes();
				Component cmp;
				for (int i = 0; i < scenes.size(); ++i)
				{
					cmp = scenes[i]->createComponent(CAMERA_HASH, m_camera);
					if (cmp.isValid())
					{
						break;
					}
				}
				RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
				scene->setCameraSlot(cmp, string("editor"));
			}
			m_gizmo.create(m_engine->getRenderer());
			m_gizmo.setUniverse(universe);
			m_gizmo.hide();

			universe->entityCreated().bind<WorldEditorImpl, &WorldEditorImpl::onEntityCreated>(this);
			universe->componentCreated().bind<WorldEditorImpl, &WorldEditorImpl::onComponentCreated>(this);
			universe->componentDestroyed().bind<WorldEditorImpl, &WorldEditorImpl::onComponentDestroyed>(this);
			universe->entityDestroyed().bind<WorldEditorImpl, &WorldEditorImpl::onEntityDestroyed>(this);

			m_selected_entity = Entity::INVALID;
			m_universe_created.invoke();

		}


		virtual void undo() override
		{
			if (m_undo_index < m_undo_stack.size() && m_undo_index >= 0)
			{
				m_undo_stack[m_undo_index]->undo();
				--m_undo_index;
			}
			selectEntity(m_selected_entity);
		}


		virtual void redo() override
		{
			if (m_undo_index + 1 < m_undo_stack.size())
			{
				++m_undo_index;
				m_undo_stack[m_undo_index]->execute();
			}
			selectEntity(m_selected_entity);
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

		MT::Mutex m_universe_mutex;
		Gizmo m_gizmo;
		Entity m_selected_entity;
		Map<uint32_t, Array<IPropertyDescriptor*> > m_component_properties;
		MouseMode::Value m_mouse_mode;
		float m_mouse_x;
		float m_mouse_y;
		Array<EditorIcon*> m_editor_icons;
		bool m_is_game_mode;
		FS::IFile* m_game_mode_file;
		Engine* m_engine;
		Entity m_camera;
		DelegateList<void()> m_universe_destroyed;
		DelegateList<void()> m_universe_created;
		DelegateList<void(Entity&)> m_entity_selected;

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
		Plugin* m_mouse_handling_plugin;
		EntityTemplateSystem* m_template_system;
		Array<IEditorCommand*> m_undo_stack;
		int m_undo_index;
};


WorldEditor* WorldEditor::create(const char* base_path)
{
	WorldEditorImpl* impl = LUMIX_NEW(WorldEditorImpl)();

	if (!impl->create(base_path))
	{
		LUMIX_DELETE(impl);
		return NULL;
	}

	return impl;
}


void WorldEditor::destroy(WorldEditor* editor)
{
	static_cast<WorldEditorImpl*>(editor)->destroy();
	LUMIX_DELETE(editor);
}


} // !namespace Lumix
