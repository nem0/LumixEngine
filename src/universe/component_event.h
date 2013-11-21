#pragma once

#include "core/lux.h"
#include "core/event_manager.h"
#include "universe.h"


namespace Lux
{

/// create or destroy event
class LUX_ENGINE_API ComponentEvent : public Event
{
	public:
		static Event::Type type;

	public:
		ComponentEvent(Component _component) : component(_component) { m_type = ComponentEvent::type; is_created = true; }
		ComponentEvent(Component _component, bool _is_created) : component(_component) { m_type = ComponentEvent::type; is_created = _is_created; }
		
		Component component;
		bool is_created;
};


} // !namespace Lux
