#include "core/log.h"
#include "core/vector.h"
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

		vector<Log::Callback*> m_callbacks;
	};


	Log::Log()
	{
		m_impl = new LogImpl();
	}


	Log::~Log()
	{
		delete m_impl;
	}


	void Log::log(const char* system, const char* message, ...)
	{
		char tmp[1024];
		va_list args;
		va_start(args, message);
#pragma warning (disable : 4996)
		vsnprintf(tmp, 1024, message, args);
#pragma warning (default : 4996)

		for(int i = 0, c = m_impl->m_callbacks.size(); i < c; ++i)
		{
			(*m_impl->m_callbacks[i])(system, tmp);
		}
		va_end(args);
	}


	void Log::registerCallback(Callback* callback)
	{
		m_impl->m_callbacks.push_back(callback);
	}


} // ~namespace Lux