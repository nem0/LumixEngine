#pragma once


#include "engine/default_allocator.h"
#include "engine/delegate_list.h"
#include "engine/lumix.h"
#include "engine/string.h"

namespace Lumix
{
	class Log;
	class Path;

	class LUMIX_ENGINE_API LogProxy
	{
		friend class Log;
		public:
			LogProxy(Log& log, const char* system, IAllocator& allocator);
			~LogProxy();

			LogProxy& operator <<(const char* message);
			LogProxy& operator <<(float message);
			LogProxy& operator <<(i32 message);
			LogProxy& operator <<(u32 message);
			LogProxy& operator <<(u64 message);
			LogProxy& operator <<(const string& path);
			LogProxy& operator <<(const Path& path);
			LogProxy& substring(const char* str, int start, int length);

		private:
			IAllocator& m_allocator;
			string m_system;
			string m_message;
			Log& m_log;

			LogProxy(const LogProxy&);
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

	void LUMIX_ENGINE_API fatal(bool cond, const char* msg);

	extern Log LUMIX_ENGINE_API g_log_info;
	extern Log LUMIX_ENGINE_API g_log_warning;
	extern Log LUMIX_ENGINE_API g_log_error;

	#define LUMIX_FATAL(cond) Lumix::fatal((cond), #cond);

} // namespace Lumix


