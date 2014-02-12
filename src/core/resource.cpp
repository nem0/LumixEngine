#include "core/lux.h"
#include "core/resource.h"

#include "core/path.h"

namespace Lux
{
	Resource::Resource(const Path& path)
		: m_path(path)
		, m_ref_count(0)
		, m_state(State::EMPTY)
	{ }

	Resource::~Resource()
	{ }

	void Resource::onEmpty(void)
	{
		m_state = State::EMPTY;
	}

	void Resource::onLoading(void)
	{
		m_state = State::LOADING;
	}

	void Resource::onReady(void)
	{
		m_state = State::READY;
		m_cb.invoke(State::READY);
	}

	void Resource::onUnloading(void)
	{
		m_state = State::UNLOADING;
		m_cb.invoke(State::UNLOADING);
	}

	void Resource::onReloading(void)
	{
		m_state = State::UNLOADING;
		m_cb.invoke(State::UNLOADING);
	}
} // ~namespace Lux