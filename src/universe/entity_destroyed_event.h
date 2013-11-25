#pragma once

#include "core/lux.h"
#include "core/event_manager.h"
#include "universe.h"


namespace Lux
{


class LUX_ENGINE_API EntityDestroyedEvent : public Event
{
	public:
		static const Event::Type type;

	public:
		EntityDestroyedEvent(Entity _entity) : entity(_entity) { m_type = EntityDestroyedEvent::type; }

		Entity entity;
};


} // !namespace Lux
