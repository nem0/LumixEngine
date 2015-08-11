#pragma once
#include "lumix.h"

namespace Lumix
{
	namespace MT
	{
		struct EventFlags
		{
			enum Value
			{
				SIGNALED	= 0x1,
				MANUAL_RESET	= SIGNALED << 1,
			};

			EventFlags(Value _value) : value(_value) { }
			EventFlags(int _value) : value(_value) { }
			operator Value() const { return (Value)value; }
			int value;
		};

		typedef void* EventHandle;

		class LUMIX_ENGINE_API Event
		{
		public:
			explicit Event(EventFlags flags);
			~Event();

			void reset();

			void trigger();

			void wait();
			bool poll();

		private:
			EventHandle m_id;
		};
	}; // ~namespace MT
}; // ~namespace Lumix
