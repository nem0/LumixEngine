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
			iter.second()[i].listener(iter.second()[i].data, event);
		}
	}
}

void EventManager::registerListener(Event::Type type, void* data, void (*listener)(void*, Event&))
{
	Listener l;
	l.data = data;
	l.listener = listener;
	m_listeners[type].push_back(l);
}



void EventManager::unregisterListener(Event::Type type, void* data, void (*listener)(void*, Event&))
{
	Listener l;
	l.data = data;
	l.listener = listener;
	vector<Listener>& listeners = m_listeners[type];
	for(int i = 0; i < listeners.size(); ++i)
	{
		if(listeners[i].data == data && listeners[i].listener == listener)
		{
			listeners.erase(i);
			break;
		}
	}
}


} // !namespace Lux