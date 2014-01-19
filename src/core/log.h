#pragma once


#include "core/lux.h"
#include "core/delegate_list.h"


namespace Lux
{

	class LUX_CORE_API Log
	{
		public:
			typedef DelegateList<void (const char*, const char*)> Callback;

		public:
			Log();
			~Log();

			void log(const char* system, const char* message, ...);
			Callback& getCallback();
		
		private:
			struct LogImpl* m_impl;
	};

	extern Log LUX_CORE_API g_log_info;
	extern Log LUX_CORE_API g_log_warning;
	extern Log LUX_CORE_API g_log_error;


} // ~namespace Lux