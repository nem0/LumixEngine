#pragma once
#include "core/lux.h"

namespace  Lux
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


		class LUX_CORE_API Event LUX_ABSTRACT
		{
		public:
			static Event* create(EventFlags flags);
			static void destroy(Event* event);

			virtual void reset() = 0;

			virtual void trigger() = 0;

			virtual void wait() = 0;
			virtual bool poll() = 0;

		protected:
			virtual ~Event() {};
		};
	}; // ~namespace MT
}; // ~namespace Lux