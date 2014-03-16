#include "core/event_manager.h"


namespace Lux
{


void EventManager::emitEvent(Event& event)
{
	ListenerMap::iterator iter = m_listeners.find(event.getType());
	if(iter != m_listeners.end())
	{
		for(int i = 0, c = iter.second().size(); i < c; ++i)
		{
			iter.second()[i].invoke(event);
		}
	}
}


EventManager::Listener& EventManager::addListener(Event::Type type)
{
	return m_listeners[type].pushEmpty();
}


void EventManager::removeListener(Event::Type type, const Listener& listener)
{
	Array<Listener>& listeners = m_listeners[type];
	for(int i = 0; i < listeners.size(); ++i)
	{
		if(listeners[i] == listener)
		{
			listeners.eraseFast(i);
		}
	}
}


} // !namespace Lux