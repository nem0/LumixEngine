#pragma once


#include "core/lumix.h"
#include "core/delegate_list.h"


namespace Lumix
{

	class LUMIX_CORE_API Log
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

	extern Log LUMIX_CORE_API g_log_info;
	extern Log LUMIX_CORE_API g_log_warning;
	extern Log LUMIX_CORE_API g_log_error;


} // ~namespace Lumix
