#pragma once


#include "lumix.h"
#include "core/delegate_list.h"
#include "core/string.h"


namespace Lumix
{
	class Log;
	class Path;

	class LUMIX_ENGINE_API LogProxy
	{
		public:

		public:
			LogProxy(Log& log, const char* system, IAllocator& allocator);
			~LogProxy();

			LogProxy& operator <<(const char* message);
			LogProxy& operator <<(float message);
			LogProxy& operator <<(int32_t message);
			LogProxy& operator <<(uint32_t message);
			LogProxy& operator <<(uint64_t message);
			LogProxy& operator <<(const Path& path);
			LogProxy& operator <<(const string& path);
			LogProxy& substring(const char* str, int start, int length);

		private:
			IAllocator& m_allocator;
			base_string<char> m_system;
			base_string<char> m_message;
			Log& m_log;

			void operator = (const LogProxy&);
	};

	class LUMIX_ENGINE_API Log
	{
		public:
			typedef DelegateList<void (const char*, const char*)> Callback;

		public:
			Log() : m_callbacks(m_allocator) {}

			LogProxy log(const char* system);
			Callback& getCallback();
		
		private:
			Log(const Log&);
			void operator =(const Log&);

		private:
			DefaultAllocator m_allocator;
			Callback m_callbacks;
	};

	extern Log LUMIX_ENGINE_API g_log_info;
	extern Log LUMIX_ENGINE_API g_log_warning;
	extern Log LUMIX_ENGINE_API g_log_error;


} // ~namespace Lumix
