#include "animation_system.h"
#include "animation/animation.h"
#include "core/crc32.h"
#include "core/json_serializer.h"
#include "core/resource_manager.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "graphics/renderer.h"
#include "universe/universe.h"

namespace Lumix
{

	static const uint32_t RENDERABLE_HASH = crc32("renderable");
	static const uint32_t ANIMABLE_HASH = crc32("animable");


	class AnimationSceneImpl : public AnimationScene
	{
		private:
			struct Animable
			{
				bool m_manual;
				bool m_is_free;
				Component m_renderable;
				float m_time;
				class Animation* m_animation;
			};

		public:
			AnimationSceneImpl(AnimationSystem& anim_system, Engine& engine, Universe& universe)
				: m_universe(universe)
				, m_engine(engine)
				, m_anim_system(anim_system)
			{
				m_universe.componentCreated().bind<AnimationSceneImpl, &AnimationSceneImpl::onComponentCreated>(this);
			}


			~AnimationSceneImpl()
			{
				m_universe.componentCreated().unbind<AnimationSceneImpl, &AnimationSceneImpl::onComponentCreated>(this);
			}


			virtual Component createComponent(uint32_t type, const Entity& entity) override
			{
				if (type == ANIMABLE_HASH)
				{
					return createAnimable(entity);
				}
				return Component::INVALID;
			}


			virtual void destroyComponent(const Component& component)
			{
				m_animables[component.index].m_is_free = true;
				m_universe.destroyComponent(component);
				m_universe.componentDestroyed().invoke(component);
			}


			virtual void serialize(ISerializer& serializer) override
			{
				serializer.serialize("count", m_animables.size());
				serializer.beginArray("animables");
				for (int i = 0; i < m_animables.size(); ++i)
				{
					serializer.serializeArrayItem(m_animables[i].m_manual);
					serializer.serializeArrayItem(m_animables[i].m_renderable.entity.index);
					serializer.serializeArrayItem(m_animables[i].m_time);
					serializer.serializeArrayItem(m_animables[i].m_is_free);
				}
				serializer.endArray();
			}


			virtual void deserialize(ISerializer& serializer) override
			{
				int count;
				serializer.deserialize("count", count);
				serializer.deserializeArrayBegin("animables");
				m_animables.resize(count);
				for (int i = 0; i < count; ++i)
				{
					serializer.deserializeArrayItem(m_animables[i].m_manual);
					int entity_index;
					serializer.deserializeArrayItem(entity_index);
					Entity e(&m_universe, entity_index);
					const Entity::ComponentList& cmps = e.getComponents();
					m_animables[i].m_renderable = Component::INVALID;
					for (int j = 0; j < cmps.size(); ++j)
					{
						if (cmps[j].type == RENDERABLE_HASH)
						{
							m_animables[i].m_renderable = cmps[j];
							break;
						}
					}
					serializer.deserializeArrayItem(m_animables[i].m_time);
					serializer.deserializeArrayItem(m_animables[i].m_is_free);
					m_universe.addComponent(e, ANIMABLE_HASH, this, i);
				}
				serializer.deserializeArrayEnd();
			}


			virtual void setFrame(Component cmp, int frame) override
			{
				m_animables[cmp.index].m_time = m_animables[cmp.index].m_animation->getLength() * frame / 30.0f; /// TODO get rid of the constant
			}


			virtual bool isManual(Component cmp) override
			{
				return m_animables[cmp.index].m_manual;
			}


			virtual void setManual(Component cmp, bool is_manual) override
			{
				m_animables[cmp.index].m_manual = is_manual;
			}


			virtual void getPreview(Component cmp, string& path) override
			{
				path = m_animables[cmp.index].m_animation ? m_animables[cmp.index].m_animation->getPath().c_str() : "";
			}


			virtual void setPreview(Component cmp, const string& path) override
			{
				playAnimation(cmp, path.c_str());
			}


			virtual void playAnimation(const Component& cmp, const char* path) override
			{
				m_animables[cmp.index].m_animation = loadAnimation(path);
				m_animables[cmp.index].m_time = 0;
				m_animables[cmp.index].m_manual = false;
			}


			virtual void setAnimationFrame(const Component& cmp, int frame) override
			{
				if (m_animables[cmp.index].m_animation)
				{
					m_animables[cmp.index].m_time = m_animables[cmp.index].m_animation->getLength() * frame / m_animables[cmp.index].m_animation->getFrameCount();
				}
			}


			virtual int getFrameCount(const Component& cmp) const override
			{
				if (m_animables[cmp.index].m_animation)
				{
					return m_animables[cmp.index].m_animation->getFrameCount();
				}
				return -1;
			}


			virtual void update(float time_delta) override
			{
				if (m_animables.empty())
					return;
				for (int i = 0, c = m_animables.size(); i < c; ++i)
				{
					Animable& animable = m_animables[i];
					if (!animable.m_is_free && animable.m_animation && animable.m_animation->isReady())
					{
						RenderScene* scene = static_cast<RenderScene*>(animable.m_renderable.scene);
						animable.m_animation->getPose(animable.m_time, scene->getPose(animable.m_renderable), *scene->getModel(animable.m_renderable));
						if (!animable.m_manual)
						{
							float t = animable.m_time + time_delta;
							float l = animable.m_animation->getLength();
							while (t > l)
							{
								t -= l;
							}
							animable.m_time = t;
						}
					}
				}
			}


		private:
			Animation* loadAnimation(const char* path)
			{
				ResourceManager& rm = m_engine.getResourceManager();
				return static_cast<Animation*>(rm.get(ResourceManager::ANIMATION)->load(path));
			}


			void onComponentCreated(Component& cmp)
			{
				if (cmp.type == RENDERABLE_HASH)
				{
					const Entity::ComponentList& cmps = cmp.entity.getComponents();
					for (int i = 0; i < cmps.size(); ++i)
					{
						if (cmps[i].type == ANIMABLE_HASH)
						{
							m_animables[cmps[i].index].m_renderable = cmp;
							break;
						}
					}
				}
			}


			Component createAnimable(const Entity& entity)
			{
				Animable* src = NULL;
				for (int i = 0, c = m_animables.size(); i < c; ++i)
				{
					if (m_animables[i].m_is_free)
					{
						src = &m_animables[i];
						break;
					}
				}
				Animable& animable = src ? *src : m_animables.pushEmpty();
				animable.m_manual = true;
				animable.m_time = 0;
				animable.m_is_free = false;
				animable.m_renderable = Component::INVALID;
				animable.m_animation = NULL;

				const Entity::ComponentList& cmps = entity.getComponents();
				for (int i = 0; i < cmps.size(); ++i)
				{
					if (cmps[i].type == RENDERABLE_HASH)
					{
						animable.m_renderable = cmps[i];
						break;
					}
				}

				Component cmp = m_universe.addComponent(entity, ANIMABLE_HASH, this, m_animables.size() - 1);
				m_universe.componentCreated().invoke(cmp);
				return cmp;
			}


			virtual IPlugin& getPlugin() const override
			{
				return m_anim_system;
			}


		private:
			Universe& m_universe;
			AnimationSystem& m_anim_system;
			Engine& m_engine;
			Array<Animable> m_animables;
	};


	struct AnimationSystemImpl : public AnimationSystem
	{
		public:
			virtual IScene* createScene(Universe& universe) override
			{
				ASSERT(m_engine);
				return LUMIX_NEW(AnimationSceneImpl)(*this, *m_engine, universe);
			}


			virtual const char* getName() const override
			{
				return "animation";
			}


			virtual bool create(Engine& engine) override
			{
				m_engine = &engine;
				engine.getWorldEditor()->registerProperty("animable", LUMIX_NEW(PropertyDescriptor<AnimationSceneImpl>)(crc32("preview"), &AnimationSceneImpl::getPreview, &AnimationSceneImpl::setPreview, IPropertyDescriptor::FILE));
				return true;
			}


			virtual void destroy() override
			{
			}

			Engine* m_engine;
	
		private:
			void operator=(const AnimationSystemImpl&);
	};


	AnimationSystem* AnimationSystem::createInstance()
	{
		return LUMIX_NEW(AnimationSystemImpl);
	}



} // ~namespace Lumix
