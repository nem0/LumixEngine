#pragma once

#include "core/mt/event.h"


namespace Lumix
{
	namespace MT
	{
		template <class T> struct Transaction 
		{
			void setCompleted()		{ m_event.trigger();		}
			bool isCompleted()		{ return m_event.poll();	}
			void waitForCompletion() { return m_event.wait();	}
			void reset()	{ m_event.reset(); }

			Transaction() : m_event(MT::EventFlags::MANUAL_RESET) { }

			MT::Event	m_event;
			T			data;
		};
	} // ~namespace MT
} // ~namespace Lumix
