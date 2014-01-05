#include "core/log.h"
#include "core/pod_array.h"
#include <cstdio>
#include <cstdarg>


namespace Lux
{

	Log g_log_info;
	Log g_log_warning;
	Log g_log_error;


	struct LogImpl
	{
		void sendMessage();

		PODArray<Log::Callback> m_callbacks;
	};


	Log::Log()
	{
		m_impl = LUX_NEW(LogImpl)();
	}


	Log::~Log()
	{
		LUX_DELETE(m_impl);
	}


	void Log::log(const char* system, const char* message, ...)
	{
		char tmp[1024];
		va_list args;
		va_start(args, message);
		vsnprintf(tmp, 1024, message, args);

		for(int i = 0, c = m_impl->m_callbacks.size(); i < c; ++i)
		{
			m_impl->m_callbacks[i].invoke(system, tmp);
		}
		va_end(args);
	}


	Log::Callback& Log::addCallback()
	{
		return m_impl->m_callbacks.pushEmpty();
	}


} // ~namespace Lux