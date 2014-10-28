#pragma once


#include "core/lumix.h"
#include "core/delegate_list.h"
#include "core/stack_allocator.h"
#include "core/string.h"


namespace Lumix
{
	class Log;
	class Path;

	class LUMIX_CORE_API LogProxy
	{
		public:
			LogProxy(Log& log, const char* system);
			~LogProxy();

			LogProxy& operator <<(const char* message);
			LogProxy& operator <<(float message);
			LogProxy& operator <<(int32_t message);
			LogProxy& operator <<(uint32_t message);
			LogProxy& operator <<(const Path& path);

		private:
			base_string<char, StackAllocator<64> > m_system;
			base_string<char> m_message;
			Log& m_log;

			void operator = (const LogProxy&);
	};

	class LUMIX_CORE_API Log
	{
		public:
			typedef DelegateList<void (const char*, const char*)> Callback;

		public:
			Log();
			~Log();

			LogProxy log(const char* system);
			Callback& getCallback();
		
		private:
			Log(const Log&);
			void operator =(const Log&);

		private:
			struct LogImpl* m_impl;
	};

	extern Log LUMIX_CORE_API g_log_info;
	extern Log LUMIX_CORE_API g_log_warning;
	extern Log LUMIX_CORE_API g_log_error;


} // ~namespace Lumix
