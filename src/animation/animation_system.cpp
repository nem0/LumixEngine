#include "animation_system.h"
#include "universe/universe.h"
#include "core/crc32.h"
#include "Horde3D.h"
#include "Horde3DUtils.h"
#include "graphics/renderer.h"
#include "core/event_manager.h"
#include "universe/component_event.h"
#include "core/json_serializer.h"

namespace Lux
{

	static const unsigned int renderable_type = crc32("renderable");
	static const unsigned int animable_type = crc32("animable");

	struct AnimationSystemImpl
	{
		struct Animable
		{
			bool manual;
			Component renderable;
			float time;
		};

		vector<Animable> animables;
		Universe* universe;

		void onEvent(Event& event);
	};

	static void onEvent(void* data, Event& event)
	{
		static_cast<AnimationSystemImpl*>(data)->onEvent(event);
	}


	bool AnimationSystem::create()
	{
		m_impl = new AnimationSystemImpl();
		m_impl->universe = 0;
		return m_impl != 0;
	}


	void AnimationSystem::destroy()
	{
		delete m_impl;
		m_impl = 0;
	}


	void AnimationSystem::setUniverse(Universe* universe)
	{
		if(m_impl->universe)
		{
			m_impl->animables.clear();
			m_impl->universe->getEventManager()->unregisterListener(ComponentEvent::type, m_impl, &onEvent);
		}
		m_impl->universe = universe;
		if(m_impl->universe)
		{
			m_impl->universe->getEventManager()->registerListener(ComponentEvent::type, m_impl, &onEvent);
		}
	}


	void AnimationSystem::serialize(ISerializer& serializer)
	{
		serializer.serialize("count", m_impl->animables.size());
		serializer.beginArray("animables");
		for(int i = 0; i < m_impl->animables.size(); ++i)
		{
			serializer.serializeArrayItem(m_impl->animables[i].manual);
			serializer.serializeArrayItem(m_impl->animables[i].renderable.entity.index);
			serializer.serializeArrayItem(m_impl->animables[i].time);
		}
		serializer.endArray();
	}


	void AnimationSystem::deserialize(ISerializer& serializer)
	{
		int count;
		serializer.deserialize("count", count);
		serializer.deserializeArrayBegin("animables");
		m_impl->animables.clear();
		for(int i = 0; i < count; ++i)
		{
			m_impl->animables.push_back_empty();
			serializer.deserializeArrayItem(m_impl->animables[i].manual);
			int entity_index;
			serializer.deserializeArrayItem(entity_index);
			Entity e(m_impl->universe, entity_index);
			const Entity::ComponentList& cmps = e.getComponents();
			m_impl->animables[i].renderable = Component::INVALID;
			for(int j = 0; j < cmps.size(); ++j)
			{
				if(cmps[j].type == renderable_type)
				{
					m_impl->animables[i].renderable = cmps[j];
					break;
				}
			}
			serializer.deserializeArrayItem(m_impl->animables[i].time);
			Component cmp(e, animable_type, this, i);
			m_impl->universe->getEventManager()->emitEvent(ComponentEvent(cmp));
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
						animables[cmps[i].index].renderable = e.component;
						break;
					}
				}
			}
		}
	}


	Component AnimationSystem::createAnimable(const Entity& entity)
	{
		AnimationSystemImpl::Animable& animable = m_impl->animables.push_back_empty();
		animable.manual = true;
		animable.time = 0;
		animable.renderable = Component::INVALID;

		const Entity::ComponentList& cmps = entity.getComponents();
		for(int i = 0; i < cmps.size(); ++i)
		{
			if(cmps[i].type == renderable_type)
			{
				animable.renderable = cmps[i];
				break;
			}
		}

		Component cmp(entity, animable_type, this, m_impl->animables.size() - 1);
		m_impl->universe->getEventManager()->emitEvent(ComponentEvent(cmp));
		return Component(entity, animable_type, this, m_impl->animables.size() - 1);
	}

	void AnimationSystem::playAnimation(const Component& cmp, const char* path)
	{
		Component renderable = m_impl->animables[cmp.index].renderable;
		if(renderable.isValid())
		{
			Renderer* renderer = static_cast<Renderer*>(renderable.system);
			H3DNode node = renderer->getMeshNode(renderable);
			H3DRes animRes = h3dAddResource(H3DResTypes::Animation, path, 0);
			h3dutLoadResourcesFromDisk(renderer->getBasePath());
			h3dSetupModelAnimStage(node, 0, animRes, 0, "", false);
			h3dSetModelAnimParams(node, 0, 0, 1.0f);

			m_impl->animables[cmp.index].manual = false;
		}
	}


	void AnimationSystem::setAnimationTime(const Component& cmp, float time)
	{
		Renderer* renderer = static_cast<Renderer*>(m_impl->animables[cmp.index].renderable.system);
		H3DNode node = renderer->getMeshNode(m_impl->animables[cmp.index].renderable);
		m_impl->animables[cmp.index].time = time;
		h3dSetModelAnimParams(node, 0, time, 1.0f);
	}


	void AnimationSystem::update(float time_delta)
	{
		if(m_impl->animables.empty())
			return;
		Renderer* renderer = static_cast<Renderer*>(m_impl->animables[0].renderable.system);
		for(int i = 0, c = m_impl->animables.size(); i < c; ++i)
		{
			if(!m_impl->animables[i].manual)
			{
				H3DNode node = renderer->getMeshNode(m_impl->animables[i].renderable);
				float time = m_impl->animables[i].time;
				time += time_delta;
				h3dSetModelAnimParams(node, 0, time, 1.0f);
				m_impl->animables[i].time = time;
			}
		}
	}


} // ~namespace Lux