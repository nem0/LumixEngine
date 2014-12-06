#include "core/log.h"
#include "core/array.h"
#include "core/path.h"
#include "core/string.h"


namespace Lumix
{

	Log g_log_info;
	Log g_log_warning;
	Log g_log_error;


	LogProxy Log::log(const char* system)
	{
		return LogProxy(*this, system, m_allocator);
	}


	Log::Callback& Log::getCallback()
	{
		return m_callbacks;
	}

	LogProxy::LogProxy(Log& log, const char* system, IAllocator& allocator)
		: m_allocator(allocator)
		, m_log(log)
		, m_system(system, m_allocator)
		, m_message(m_allocator)
	{
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

	LogProxy& LogProxy::operator <<(float message)
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

	LogProxy& LogProxy::operator <<(const string& path)
	{
		m_message.cat(path.c_str());
		return *this;
	}

	LogProxy& LogProxy::operator <<(const Path& path)
	{
		m_message.cat(path.c_str());
		return *this;
	}
} // ~namespace Lumix
