#include "engine/mt/sync.h"
#include "engine/mt/atomic.h"
#include "engine/mt/thread.h"
#include "engine/win/simple_win.h"
#include <intrin.h>

namespace Lumix
{
namespace MT
{


Semaphore::Semaphore(int init_count, int max_count)
{
	m_id = ::CreateSemaphore(nullptr, init_count, max_count, nullptr);
}

Semaphore::~Semaphore()
{
	::CloseHandle(m_id);
}

void Semaphore::signal()
{
	::ReleaseSemaphore(m_id, 1, nullptr);
}

void Semaphore::wait()
{
	::WaitForSingleObject(m_id, INFINITE);
}

bool Semaphore::poll()
{
	return WAIT_OBJECT_0 == ::WaitForSingleObject(m_id, 0);
}


Event::Event(bool manual_reset)
{
	m_id = ::CreateEvent(nullptr, manual_reset, FALSE, nullptr);
}

Event::~Event()
{
	::CloseHandle(m_id);
}

void Event::reset()
{
	::ResetEvent(m_id);
}

void Event::trigger()
{
	::SetEvent(m_id);
}

void Event::waitTimeout(u32 timeout_ms)
{
	::WaitForSingleObject(m_id, timeout_ms);
}

void Event::wait()
{
	::WaitForSingleObject(m_id, INFINITE);
}

bool Event::poll()
{
	return WAIT_OBJECT_0 == ::WaitForSingleObject(m_id, 0);
}


SpinMutex::SpinMutex() : m_id(0) {}

SpinMutex::~SpinMutex() = default;

void SpinMutex::lock()
{
	for (;;) {
		if(m_id == 0 &&  _interlockedbittestandset(&m_id, 0) == 0) break;
		_mm_pause();
	}
}

bool SpinMutex::poll()
{
	return m_id == 0 && _interlockedbittestandset(&m_id, 0) == 0;
}

void SpinMutex::unlock()
{
	_interlockedbittestandreset(&m_id, 0);
}


} // namespace MT
} // namespace Lumix
