#pragma once


#include "core/lumix.h"
#include "core/delegate.h"
#include "core/array.h"
#include "core/map.h"


namespace Lumix
{


class LUX_CORE_API Event
{
	public:
		typedef uint32_t Type;

	public:
		Type getType() const { return m_type; }

	protected:
		Type m_type;
};


class LUX_CORE_API EventManager
{
	public:
		typedef Delegate<void (Event&)> Listener;

	public:
		Listener& addListener(Event::Type type);
		void removeListener(Event::Type type, const Listener& listener);
		void emitEvent(Event& event);

	private:
		typedef Map<Event::Type, Array<Listener> > ListenerMap;

	private:
		ListenerMap m_listeners;
};


} // !namespace Lumix
