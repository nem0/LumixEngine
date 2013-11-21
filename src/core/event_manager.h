#pragma once


#include "core/lux.h"
#include "core/vector.h"
#include "core/map.h"


namespace Lux
{


class LUX_CORE_API Event
{
	public:
		typedef unsigned int Type;

	public:
		Type getType() const { return m_type; }

	protected:
		Type m_type;
};


class LUX_CORE_API EventManager
{
	public:
		void registerListener(Event::Type type, void* data, void (*listener)(void*, Event&));
		void unregisterListener(Event::Type type, void* data, void (*listener)(void*, Event&));
		void emitEvent(Event& event);

	private:
		typedef unsigned int EventType;

		struct Listener
		{
			void* data;
			void (*listener)(void*, Event&);
		};

		typedef map<Event::Type, vector<Listener> > ListenerMap;

	private:
		ListenerMap m_listeners;
};


} // !namespace Lux