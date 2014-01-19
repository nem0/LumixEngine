#include "core/log.h"
#include "core/pod_array.h"
#include "core/string.h"
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

		Log::Callback m_callbacks;
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

		m_impl->m_callbacks.invoke(system, tmp);
		va_end(args);
	}


	Log::Callback& Log::getCallback()
	{
		return m_impl->m_callbacks;
	}


} // ~namespace Lux