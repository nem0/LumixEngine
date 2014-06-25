#pragma once

#include "core/lumix.h"
#include "core/event_manager.h"
#include "universe.h"


namespace Lumix
{


class LUMIX_ENGINE_API EntityMovedEvent : public Event
{
	public:
		static const Event::Type type;

	public:
		EntityMovedEvent(Entity _entity) : entity(_entity) { m_type = EntityMovedEvent::type; }

		Entity entity;
};


} // !namespace Lumix
