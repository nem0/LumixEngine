#pragma once


#include "core/lux.h"
#include "core/functor.h"


namespace Lux
{

	class LUX_CORE_API Log
	{
		public:
			typedef IFunctor2<void, const char*, const char*> Callback;

		public:
			Log();
			~Log();

			void log(const char* system, const char* message, ...);
			void registerCallback(Callback* callback);
		
		private:
			struct LogImpl* m_impl;
	};

	extern Log LUX_CORE_API g_log_info;
	extern Log LUX_CORE_API g_log_warning;
	extern Log LUX_CORE_API g_log_error;


} // ~namespace Lux