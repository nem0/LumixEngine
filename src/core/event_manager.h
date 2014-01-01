#pragma once


#include "core/lux.h"
#include "core/delegate.h"
#include "core/pod_array.h"
#include "core/map.h"


namespace Lux
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
		typedef uint32_t EventType;

		typedef map<Event::Type, PODArray<Listener> > ListenerMap;

	private:
		ListenerMap m_listeners;
};


} // !namespace Lux