#include "core/log.h"
#include "core/array.h"
#include "core/string.h"


namespace Lumix
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
		m_impl = LUMIX_NEW(LogImpl)();
	}


	Log::~Log()
	{
		LUMIX_DELETE(m_impl);
	}


	LogProxy Log::log(const char* system)
	{
		return LogProxy(*this, system);
	}


	Log::Callback& Log::getCallback()
	{
		return m_impl->m_callbacks;
	}

	LogProxy::LogProxy(Log& log, const char* system)
		: m_log(log)
	{
		m_system = system;
	}

	LogProxy::~LogProxy()
	{
		m_log.getCallback().invoke(m_system.c_str(), m_message.c_str());
	}

	LogProxy& LogProxy::operator <<(const char* message)
	{
		m_message.cat(message);
		return *this;
	}

	LogProxy& LogProxy::operator <<(uint32_t message)
	{
		m_message.cat(message);
		return *this;
	}

	LogProxy& LogProxy::operator <<(int32_t message)
	{
		m_message.cat(message);
		return *this;
	}

} // ~namespace Lumix
