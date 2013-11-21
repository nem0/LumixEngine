#include "mutex.h"
#include <cassert>


namespace Lux
{


	void Mutex::create()
	{
		m_handle = CreateMutex(NULL, FALSE, "");
		m_locked = 0;
	}


	void Mutex::destroy()
	{
		CloseHandle(m_handle);
	}


	void Mutex::lock()
	{
		WaitForSingleObject(m_handle, INFINITE);
		++m_locked;
	}


	void Mutex::unlock()
	{
		assert(m_locked);
		--m_locked; 
		ReleaseMutex(m_handle);
	}


} // ~namespace Lux