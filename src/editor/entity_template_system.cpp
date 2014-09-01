#include "entity_template_system.h"
#include "core/array.h"
#include "core/crc32.h"
#include "core/iserializer.h"
#include "core/map.h"
#include "core/string.h"
#include "editor/ieditor_command.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "graphics/render_scene.h"
#include "universe/entity.h"
#include "universe/universe.h"


static const uint32_t RENDERABLE_HASH = crc32("renderable");
static const uint32_t CAMERA_HASH = crc32("camera");
static const uint32_t LIGHT_HASH = crc32("light");
static const uint32_t SCRIPT_HASH = crc32("script");
static const uint32_t ANIMABLE_HASH = crc32("animable");
static const uint32_t TERRAIN_HASH = crc32("terrain");


namespace Lumix
{


	class EntityTemplateSystemImpl : public EntityTemplateSystem
	{
		private:
			class CreateInstanceCommand : public IEditorCommand
			{
				public:
					CreateInstanceCommand(EntityTemplateSystemImpl& entity_system, const char* template_name)
						: m_entity_system(entity_system)
						, m_template_name_hash(crc32(template_name))
					{
						
						RenderScene* scene = static_cast<RenderScene*>(m_entity_system.m_editor.getEditCamera().scene);
						float width;
						float height;
						scene->getCameraWidth(m_entity_system.m_editor.getEditCamera(), width);
						scene->getCameraHeight(m_entity_system.m_editor.getEditCamera(), height);

						Vec3 origin;
						Vec3 dir;
						scene->getRay(m_entity_system.m_editor.getEditCamera(), width * 0.5f, height * 0.5f, origin, dir);
						RayCastModelHit hit = scene->castRay(origin, dir, Component::INVALID);
						if (hit.m_is_hit)
						{
							m_position = hit.m_origin + hit.m_dir * hit.m_t;
						}
						else
						{
							m_position.set(0, 0, 0);
						}
					}

					virtual void execute() override
					{
						Map<uint32_t, Array<Entity> >::iterator iter = m_entity_system.m_instances.find(m_template_name_hash);
						if (iter != m_entity_system.m_instances.end())
						{
							m_entity = m_entity_system.m_editor.getEngine().getUniverse()->createEntity();
							m_entity.setPosition(m_position);

							m_entity_system.m_instances[m_template_name_hash].push(m_entity);
							Entity template_entity = iter.second()[0];
							const Entity::ComponentList& template_cmps = template_entity.getComponents();
							for (int i = 0; i < template_cmps.size(); ++i)
							{
								m_entity_system.m_editor.cloneComponent(template_cmps[i], m_entity);
							}
						}
						else
						{
							ASSERT(false);
						}
					}


					virtual void undo() override
					{
						const Entity::ComponentList& cmps = m_entity.getComponents();
						for (int i = 0; i < cmps.size(); ++i)
						{
							cmps[i].scene->destroyComponent(cmps[i]);
						}
						m_entity_system.m_universe->destroyEntity(m_entity);
						m_entity = Entity::INVALID;
					}


					virtual bool merge(IEditorCommand&) override
					{
						return false;
					}


					virtual uint32_t getType() override
					{
						static const uint32_t hash = crc32("create_entity_template_instance");
						return hash;
					}


					const Entity& getEntity() const { return m_entity; }

				private:
					EntityTemplateSystemImpl& m_entity_system;
					uint32_t m_template_name_hash;
					Entity m_entity;
					Vec3 m_position;
			};

		public:
			EntityTemplateSystemImpl(WorldEditor& editor)
				: m_editor(editor)
				, m_universe(NULL)
			{
				editor.universeCreated().bind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onUniverseCreated>(this);
				editor.universeDestroyed().bind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onUniverseDestroyed>(this);
				setUniverse(editor.getEngine().getUniverse());
			}


			~EntityTemplateSystemImpl()
			{
				m_editor.universeCreated().unbind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onUniverseCreated>(this);
				m_editor.universeDestroyed().unbind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onUniverseDestroyed>(this);
				setUniverse(NULL);
			}


			void setUniverse(Universe* universe)
			{
				if (m_universe)
				{
					m_universe->entityDestroyed().unbind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onEntityDestroyed>(this);
				}
				m_universe = universe;
				if (m_universe)
				{
					m_universe->entityDestroyed().bind<EntityTemplateSystemImpl, &EntityTemplateSystemImpl::onEntityDestroyed>(this);
				}
			}


			void onUniverseCreated()
			{
				m_instances.clear();
				m_template_names.clear();
				setUniverse(m_editor.getEngine().getUniverse());
			}


			void onUniverseDestroyed()
			{
				m_instances.clear();
				m_template_names.clear();
				setUniverse(NULL);
			}


			void onEntityDestroyed(const Entity& entity)
			{
				uint32_t tpl = getTemplate(entity);
				if (tpl != 0)
				{
					Array<Entity>& instances = m_instances[tpl];
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


			virtual void createTemplateFromEntity(const char* name, const Entity& entity) override
			{
				uint32_t name_hash = crc32(name);
				if (m_instances.find(name_hash) == m_instances.end())
				{
					m_template_names.push(string(name));
					m_instances[name_hash].push(entity);
					m_updated.invoke();
				}
				else
				{
					ASSERT(false);
				}
			}


			virtual uint32_t getTemplate(const Entity& entity) override
			{
				for (auto iter = m_instances.begin(), end = m_instances.end(); iter != end; ++iter)
				{
					Array<Entity>& entities = iter.second();
					for (int i = 0, c = entities.size(); i < c; ++i)
					{
						if (entities[i] == entity)
						{
							return iter.first();
						}
					}
				}
				return 0;
			}


			virtual const Array<Entity>& getInstances(uint32_t template_name_hash) override
			{
				return m_instances[template_name_hash];
			}


			virtual Entity createInstance(const char* name) override
			{
				CreateInstanceCommand* command = LUMIX_NEW(CreateInstanceCommand)(*this, name);
				m_editor.executeCommand(command);
				return command->getEntity();
			}


			virtual void serialize(ISerializer& serializer) override
			{
				serializer.serialize("templates_count", (int32_t)m_template_names.size());
				serializer.beginArray("template_names");
				for (int i = 0, c = m_template_names.size(); i < c;  ++i)
				{
					serializer.serializeArrayItem(m_template_names[i].c_str());
				}
				serializer.endArray();
				serializer.serialize("instance_count", (int32_t)m_instances.size());
				serializer.beginArray("instances");
				for (auto i = m_instances.begin(), end = m_instances.end(); i != end; ++i)
				{
					serializer.serializeArrayItem(i.first());
					serializer.serializeArrayItem((int32_t)i.second().size());
					for (int j = 0, c = i.second().size(); j < c; ++j)
					{
						serializer.serializeArrayItem(i.second()[j].index);
					}
				}
				serializer.endArray();

			}


			virtual void deserialize(ISerializer& serializer) override
			{
				m_template_names.clear();
				m_instances.clear();
				int32_t count;
				serializer.deserialize("templates_count", count);
				serializer.deserializeArrayBegin("template_names");
				for (int i = 0; i < count; ++i)
				{
					const int MAX_NAME_LENGTH = 50;
					char name[MAX_NAME_LENGTH];
					serializer.deserializeArrayItem(name, MAX_NAME_LENGTH);
					m_template_names.push(string(name));
				}
				serializer.deserializeArrayEnd();
				serializer.deserialize("instance_count", count);
				serializer.deserializeArrayBegin("instances");
				for (int i = 0; i < count; ++i)
				{
					uint32_t hash;
					serializer.deserializeArrayItem(hash);
					int32_t instances_per_template;
					serializer.deserializeArrayItem(instances_per_template);
					m_instances.insert(hash, Array<Entity>());
					Array<Entity>& entities = m_instances[hash];
					for (int j = 0; j < instances_per_template; ++j)
					{
						int32_t entity_index;
						serializer.deserializeArrayItem(entity_index);
						entities.push(Entity(m_universe, entity_index));
					}
				}
				serializer.deserializeArrayEnd();
				m_updated.invoke();
			}


			virtual Array<string>& getTemplateNames() override
			{
				return m_template_names;
			}


			virtual DelegateList<void()>& updated() override
			{
				return m_updated;
			}


		private:
			Map<uint32_t, Array<Entity> > m_instances;
			Array<string> m_template_names;
			Universe* m_universe;
			WorldEditor& m_editor;
			DelegateList<void()> m_updated;

	}; // class EntityTemplateSystemImpl


	EntityTemplateSystem* EntityTemplateSystem::create(WorldEditor& editor)
	{
		return LUMIX_NEW(EntityTemplateSystemImpl)(editor);
	}


	void EntityTemplateSystem::destroy(EntityTemplateSystem* system)
	{
		LUMIX_DELETE(system);
	}


} // namespace Lumix