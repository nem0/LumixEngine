#include "animation_system.h"
#include "animation/animation.h"
#include "core/crc32.h"
#include "core/event_manager.h"
#include "core/json_serializer.h"
#include "editor/editor_server.h"
#include "engine/engine.h"
#include "graphics/renderer.h"
#include "universe/component_event.h"
#include "universe/universe.h"
#include "core/resource_manager.h"

namespace Lux
{

	static const uint32_t renderable_type = crc32("renderable");
	static const uint32_t animable_type = crc32("animable");


	struct AnimationSystemImpl
	{
		public:
			AnimationSystemImpl(Engine& engine) : m_engine(engine) {}

			struct Animable
			{
				bool m_manual;
				Component m_renderable;
				float m_time;
				class Animation* m_animation;
			};

			Array<Animable> m_animables;
			Universe* m_universe;
			Engine& m_engine;

			void onEvent(Event& event);
		
		private:
			void operator=(const AnimationSystemImpl&);
	};

	bool AnimationSystem::create(Engine& engine)
	{
		m_impl = LUX_NEW(AnimationSystemImpl)(engine);
		m_impl->m_universe = 0;
		if(engine.getEditorServer())
		{
			engine.getEditorServer()->registerCreator(animable_type, *this);
		}
		return true;
	}


	void AnimationSystem::destroy()
	{
		LUX_DELETE(m_impl);
		m_impl = 0;
	}


	void AnimationSystem::onCreateUniverse(Universe& universe)
	{
		ASSERT(!m_impl->m_universe);
		m_impl->m_universe = &universe;
		m_impl->m_universe->getEventManager()->addListener(ComponentEvent::type).bind<AnimationSystemImpl, &AnimationSystemImpl::onEvent>(m_impl);
	}


	void AnimationSystem::onDestroyUniverse(Universe&)
	{
		ASSERT(m_impl->m_universe);
		m_impl->m_animables.clear();
		EventManager::Listener cb;
		cb.bind<AnimationSystemImpl, &AnimationSystemImpl::onEvent>(m_impl);
		m_impl->m_universe->getEventManager()->removeListener(ComponentEvent::type, cb);
		m_impl->m_universe = 0;
	}


	Component AnimationSystem::createComponent(uint32_t component_type, const Entity& entity) 
	{
		if(component_type == animable_type)
		{
			return createAnimable(entity);
		}
		return Component::INVALID;
	}


	void AnimationSystem::serialize(ISerializer& serializer)
	{
		serializer.serialize("count", m_impl->m_animables.size());
		serializer.beginArray("animables");
		for(int i = 0; i < m_impl->m_animables.size(); ++i)
		{
			serializer.serializeArrayItem(m_impl->m_animables[i].m_manual);
			serializer.serializeArrayItem(m_impl->m_animables[i].m_renderable.entity.index);
			serializer.serializeArrayItem(m_impl->m_animables[i].m_time);
		}
		serializer.endArray();
	}


	void AnimationSystem::deserialize(ISerializer& serializer)
	{
		int count;
		serializer.deserialize("count", count);
		serializer.deserializeArrayBegin("animables");
		m_impl->m_animables.clear();
		for(int i = 0; i < count; ++i)
		{
			m_impl->m_animables.pushEmpty();
			serializer.deserializeArrayItem(m_impl->m_animables[i].m_manual);
			int entity_index;
			serializer.deserializeArrayItem(entity_index);
			Entity e(m_impl->m_universe, entity_index);
			const Entity::ComponentList& cmps = e.getComponents();
			m_impl->m_animables[i].m_renderable = Component::INVALID;
			for(int j = 0; j < cmps.size(); ++j)
			{
				if(cmps[j].type == renderable_type)
				{
					m_impl->m_animables[i].m_renderable = cmps[j];
					break;
				}
			}
			serializer.deserializeArrayItem(m_impl->m_animables[i].m_time);
			Component cmp(e, animable_type, this, i);
			ComponentEvent evt(cmp);
			m_impl->m_universe->getEventManager()->emitEvent(evt);
		}
		serializer.deserializeArrayEnd();
	}


	void AnimationSystemImpl::onEvent(Event& event)
	{
		if(event.getType() == ComponentEvent::type)
		{
			ComponentEvent& e = static_cast<ComponentEvent&>(event);
			if(e.component.type == renderable_type)
			{
				const Entity::ComponentList& cmps = e.component.entity.getComponents();
				for(int i = 0; i < cmps.size(); ++i)
				{
					if(cmps[i].type == animable_type)
					{
						m_animables[cmps[i].index].m_renderable = e.component;
						break;
					}
				}
			}
		}
	}


	Component AnimationSystem::createAnimable(const Entity& entity)
	{
		AnimationSystemImpl::Animable& animable = m_impl->m_animables.pushEmpty();
		animable.m_manual = true;
		animable.m_time = 0;
		animable.m_renderable = Component::INVALID;

		const Entity::ComponentList& cmps = entity.getComponents();
		for(int i = 0; i < cmps.size(); ++i)
		{
			if(cmps[i].type == renderable_type)
			{
				animable.m_renderable = cmps[i];
				break;
			}
		}

		Component cmp(entity, animable_type, this, m_impl->m_animables.size() - 1);
		ComponentEvent evt(cmp);
		m_impl->m_universe->getEventManager()->emitEvent(evt);
		return Component(entity, animable_type, this, m_impl->m_animables.size() - 1);
	}


	Animation* AnimationSystem::loadAnimation(const char* path)
	{
		ResourceManager& rm = m_impl->m_engine.getResourceManager();
		return static_cast<Animation*>(rm.get(ResourceManager::ANIMATION)->load(path));
	}


	void AnimationSystem::playAnimation(const Component& cmp, const char* path)
	{
		m_impl->m_animables[cmp.index].m_animation = loadAnimation(path);
		m_impl->m_animables[cmp.index].m_time = 0;
		m_impl->m_animables[cmp.index].m_manual = false;
	}


	void AnimationSystem::setAnimationTime(const Component& cmp, float time)
	{
		m_impl->m_animables[cmp.index].m_time = time;
	}


	void AnimationSystem::update(float time_delta)
	{
		if(m_impl->m_animables.empty())
			return;
		Renderer* renderer = static_cast<Renderer*>(m_impl->m_animables[0].m_renderable.system);
		for(int i = 0, c = m_impl->m_animables.size(); i < c; ++i)
		{
			AnimationSystemImpl::Animable& animable = m_impl->m_animables[i];
			if(!animable.m_manual && animable.m_animation->isReady())
			{
				animable.m_animation->getPose(animable.m_time, renderer->getPose(animable.m_renderable));
				float t = animable.m_time + time_delta;
				float l = animable.m_animation->getLength();
				while(t > l)
				{
					t -= l;
				}
				animable.m_time = t;
			}
		}
	}


} // ~namespace Lux